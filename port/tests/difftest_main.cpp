// difftest: run the original code (reference emulator) and the C++ port side by
// side with the same input and compare the complete chip RAM after every frame.
//   difftest --adf <image> [--frames N] [--input SCRIPT] [--dump DIR] [--every K]
//            [--random SEED] [--level L] [--coverage FILE] [--keep-going]
//
// --random SEED  : random joystick (and fire) input from frame 700 on; fire is
//                  pressed regularly so that the menu starts the level again
// --level L      : at frame 690 (main menu) make level L selectable and select it
//                  (the same memory poke is applied to both machines)
// --coverage F   : write the reference's executed instructions to F
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
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
    long seed = -1, level = 0;
    std::string coverage;
    uint32_t watch = 0;
    long watchFrom = 0, traceFrame = -1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--adf") adfPath = next();
        else if (a == "--frames") frames = std::atol(next().c_str());
        else if (a == "--input") script = next();
        else if (a == "--dump") dump = next();
        else if (a == "--every") every = std::atol(next().c_str());
        else if (a == "--keep-going") keepGoing = true;
        else if (a == "--random") seed = std::atol(next().c_str());
        else if (a == "--level") level = std::atol(next().c_str());
        else if (a == "--coverage") coverage = next();
        else if (a == "--watch") watch = std::strtoul(next().c_str(), nullptr, 16);
        else if (a == "--watch-from") watchFrom = std::atol(next().c_str());
        else if (a == "--trace-frame") traceFrame = std::atol(next().c_str());
    }
    amiga::Adf adf;
    if (!adf.open(adfPath)) { std::fprintf(stderr, "%s\n", adf.error().c_str()); return 1; }
    InputScript in;
    if (!script.empty() && !in.load(script)) { std::fprintf(stderr, "cannot read %s\n", script.c_str()); return 1; }

    RefEmu ref(adf);
    ref.bootIntro();
    PortRunner port;
    port.start(adf);

    const uint32_t kStackLo = 0x7F000;  // $7F000-$7FFFF: stack (interrupt frames), not compared; level bitmap ends at $7EFFF
    int mismatches = 0;
    std::mt19937 rng(uint32_t(seed < 0 ? 0 : seed));
    int holdLeft = 0;
    bool ju = false, jd = false, jl = false, jr = false, jf = false;
    auto poke16 = [&](uint32_t a, uint16_t v) {
        for (amiga::Amiga* m : {&ref.hw, &port.hw}) { m->chip()[a] = uint8_t(v >> 8); m->chip()[a + 1] = uint8_t(v); }
    };
    for (long f = 0; f < frames; f++) {
        if (watch && f == watchFrom) {
            ref.watchLo = watch; ref.watchHi = watch + 2;
#ifdef SUPAPLEX_TRACE
            game::watchLo = watch; game::watchHi = watch + 2;
#endif
        }
        if (f == traceFrame) {
            ref.pcTrace = std::fopen("trace_ref.txt", "w");
#ifdef SUPAPLEX_TRACE
            game::tracePcFile = std::fopen("trace_port.txt", "w");
#endif
        }
        in.apply(uint64_t(f), ref.hw);
        in.apply(uint64_t(f), port.hw);
        if (level > 0 && f == 690) {
            // current player record ($113D6): +$14 = highest level reached; $140DC = selected level - 1
            uint32_t rec = ref.hw.rd32(0x113D6);
            if (rec) poke16(rec + 0x14, 111);
            poke16(0x140DC, uint16_t(level - 1));
        }
        if (seed >= 0 && f >= 700) {
            if (--holdLeft <= 0) {
                holdLeft = 1 + int(rng() % 24);
                int d = int(rng() % 6);  // 0 none, 1-4 directions, 5 random diagonal
                ju = d == 1; jl = d == 2; jd = d == 3; jr = d == 4;
                if (d == 5) { ju = rng() & 1; jd = !ju; jl = rng() & 1; jr = !jl; }
                jf = (rng() % 8) == 0;
                if ((f % 400) < 6) jf = true;  // restart from the menu regularly
            }
            ref.hw.setJoystick(ju, jd, jl, jr, jf);
            port.hw.setJoystick(ju, jd, jl, jr, jf);
        }
        if (!ref.runFrame()) { std::fprintf(stderr, "reference stuck at frame %ld\n", f); break; }
        if (!port.runFrame()) { std::fprintf(stderr, "port ended at frame %ld\n", f); break; }
        if (ref.hw.pollClock() != port.hw.pollClock() && std::getenv("CHECK_CLOCK")) {
            std::printf("frame %ld: poll clock differs ref %d port %d\n", f + 1, ref.hw.pollClock(), port.hw.pollClock());
            break;
        }
        const uint8_t* a = ref.hw.chip();
        const uint8_t* b = port.hw.chip();
        int diffs = 0;
        uint32_t first = 0;
        if (std::memcmp(a, b, kStackLo) != 0)
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
    if (!coverage.empty()) {
        // per routine coverage of the translated code
        std::ifstream li("tests/data/translated_insns.txt");
        std::map<std::string, std::pair<int, int>> per;
        std::string line;
        int total = 0, hit = 0;
        std::vector<std::string> missed;
        while (std::getline(li, line)) {
            unsigned addr = 0;
            char fn[128] = {};
            if (std::sscanf(line.c_str(), "%x %127s", &addr, fn) != 2) continue;
            bool h = ref.executed[addr & 0x7FFFF] != 0;
            total++; hit += h;
            per[fn].first++; per[fn].second += h;
            if (!h) missed.push_back(line);
        }
        std::ofstream co(coverage);
        co << "covered " << hit << " of " << total << " translated instructions\n";
        for (auto& [fn, c] : per) co << fn << " " << c.second << "/" << c.first << "\n";
        co << "\nnot executed:\n";
        for (auto& m : missed) co << m << "\n";
        std::ofstream raw(coverage + ".raw", std::ios::binary);
        raw.write(reinterpret_cast<const char*>(ref.executed.data()), std::streamsize(ref.executed.size()));
        std::printf("coverage: %d of %d instructions (%.1f%%)\n", hit, total, total ? 100.0 * hit / total : 0.0);
    }
    if (mismatches == 0) std::printf("OK: %ld frames identical (memory and picture)\n", frames);
    return mismatches ? 3 : 0;
}
