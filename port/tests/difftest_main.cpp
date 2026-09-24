// difftest: run the original code (reference emulator) and the C++ port side by
// side with the same input and compare the complete chip RAM after every frame.
//   difftest --adf <image> [--frames N] [--input SCRIPT] [--dump DIR] [--every K]
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "game/cpu.hpp"
#include "game/runtime.hpp"
#include "inputscript.hpp"
#include "refemu.hpp"

namespace {

// The port runs in its own thread and hands control back at every frame.
struct PortRunner : amiga::Host {
    amiga::Amiga hw;
    amiga::Paula paula{hw};
    std::mutex m;
    std::condition_variable cv;
    bool portTurn = false;
    bool stop = false;
    bool finished = false;
    std::thread th;

    void onInterrupt(int level) override { game::requestInterrupt(level); }
    void onFrame() override {
        std::unique_lock<std::mutex> lk(m);
        portTurn = false;
        cv.notify_all();
        cv.wait(lk, [&] { return portTurn || stop; });
        if (stop) throw game::QuitGame{};
    }
    void start(const amiga::Adf& adf) {
        hw.setHost(this);
        hw.setPaula(&paula);
        game::Environment env;
        env.adf = &adf;
        game::attach(hw, env);
        th = std::thread([this] {
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&] { return portTurn || stop; });
            }
            try {
                if (!stop) game::run();
            } catch (const game::QuitGame&) {
            }
            std::lock_guard<std::mutex> lk(m);
            finished = true;
            portTurn = false;
            cv.notify_all();
        });
    }
    // run the port until its next frame boundary
    bool runFrame() {
        std::unique_lock<std::mutex> lk(m);
        portTurn = true;
        cv.notify_all();
        cv.wait(lk, [&] { return !portTurn; });
        return !finished;
    }
    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(m);
            stop = true;
            cv.notify_all();
        }
        if (th.joinable()) th.join();
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::string adfPath = "Supaplex (1991).adf", script, dump;
    long frames = 1000, every = 0;
    bool keepGoing = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--adf") adfPath = next();
        else if (a == "--frames") frames = std::atol(next().c_str());
        else if (a == "--input") script = next();
        else if (a == "--dump") dump = next();
        else if (a == "--every") every = std::atol(next().c_str());
        else if (a == "--keep-going") keepGoing = true;
    }
    amiga::Adf adf;
    if (!adf.open(adfPath)) { std::fprintf(stderr, "%s\n", adf.error().c_str()); return 1; }
    InputScript in;
    if (!script.empty() && !in.load(script)) { std::fprintf(stderr, "cannot read %s\n", script.c_str()); return 1; }

    RefEmu ref(adf);
    ref.bootIntro();
    PortRunner port;
    port.start(adf);

    const uint32_t kStackLo = 0x7E000;  // stack contents (return frames) are not compared
    int mismatches = 0;
    for (long f = 0; f < frames; f++) {
        in.apply(uint64_t(f), ref.hw);
        in.apply(uint64_t(f), port.hw);
        if (!ref.runFrame()) { std::fprintf(stderr, "reference stuck at frame %ld\n", f); break; }
        if (!port.runFrame()) { std::fprintf(stderr, "port ended at frame %ld\n", f); break; }
        const uint8_t* a = ref.hw.chip();
        const uint8_t* b = port.hw.chip();
        int diffs = 0;
        uint32_t first = 0;
        for (uint32_t i = 0; i < kStackLo; i++) {
            if (a[i] != b[i]) {
                if (!diffs) first = i;
                diffs++;
            }
        }
        bool fbDiff = std::memcmp(ref.hw.frameBuffer(), port.hw.frameBuffer(),
                                  sizeof(uint32_t) * amiga::kOutWidth * amiga::kOutHeight) != 0;
        if (diffs || fbDiff) {
            mismatches++;
            std::printf("frame %ld: %d bytes differ, first at $%06X (ref %02X port %02X)%s\n", f + 1, diffs, first,
                        a[first], b[first], fbDiff ? ", picture differs" : "");
            if (diffs) {
                int shown = 0;
                for (uint32_t i = 0; i < kStackLo && shown < 16; i++)
                    if (a[i] != b[i]) { std::printf("   $%06X ref %02X port %02X\n", i, a[i], b[i]); shown++; }
            }
            if (!keepGoing) break;
        }
        if (!dump.empty() && every > 0 && (f + 1) % every == 0) {
            char name[512];
            std::snprintf(name, sizeof name, "%s/port_%06ld.ppm", dump.c_str(), f + 1);
            writePPM(name, port.hw.frameBuffer(), amiga::kOutWidth, amiga::kOutHeight);
        }
    }
    port.shutdown();
    if (mismatches == 0) std::printf("OK: %ld frames identical (memory and picture)\n", frames);
    return mismatches ? 3 : 0;
}
