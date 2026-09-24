// refrun: run the original game code under the reference emulator.
//   refrun --adf <image> [--frames N] [--dump DIR] [--every K] [--input SCRIPT] [--main]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "inputscript.hpp"
#include "refemu.hpp"

int main(int argc, char** argv) {
    std::string adfPath = "Supaplex (1991).adf", dump, script;
    long frames = 300, every = 50;
    bool mainOnly = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--adf") adfPath = next();
        else if (a == "--frames") frames = std::atol(next().c_str());
        else if (a == "--dump") dump = next();
        else if (a == "--every") every = std::atol(next().c_str());
        else if (a == "--input") script = next();
        else if (a == "--main") mainOnly = true;
    }
    amiga::Adf adf;
    if (!adf.open(adfPath)) { std::fprintf(stderr, "%s\n", adf.error().c_str()); return 1; }
    RefEmu ref(adf);
    InputScript in;
    if (!script.empty() && !in.load(script)) { std::fprintf(stderr, "cannot read %s\n", script.c_str()); return 1; }
    if (mainOnly) ref.bootMain(); else ref.bootIntro();
    for (long f = 0; f < frames; f++) {
        in.apply(uint64_t(f), ref.hw);
        uint64_t before = ref.instructions();
        if (!ref.runFrame()) return 2;
        if (std::getenv("ICOUNT")) std::printf("frame %ld: %llu instructions\n", f + 1, (unsigned long long)(ref.instructions() - before));
        if (!dump.empty() && every > 0 && (f % every) == every - 1) {
            char name[512];
            std::snprintf(name, sizeof name, "%s/ref_%06ld.ppm", dump.c_str(), f + 1);
            writePPM(name, ref.hw.frameBuffer(), amiga::kOutWidth, amiga::kOutHeight);
        }
    }
    std::printf("done: %ld frames, pc=%06X\n", frames, ref.pc());
    return 0;
}
