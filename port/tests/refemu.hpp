// Reference emulator: runs the ORIGINAL 68000 code of Supaplex on the Musashi
// CPU core, on top of the same Amiga hardware layer the port uses. It is the
// ground truth for the frame-by-frame comparison with the C++ port.
#pragma once
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "../src/amiga/adf.hpp"
#include "../src/amiga/amiga.hpp"
#include "../src/amiga/paula.hpp"

class RefEmu : public amiga::Host {
public:
    explicit RefEmu(const amiga::Adf& adf);
    // boot like the cracked disk does: PHIL_00 (intro) at $54000
    void bootIntro();
    // start directly at the main program (PHIL_01 at $7E00), skipping the intro
    void bootMain();
    // run until the next frame boundary; returns false if the CPU got stuck
    bool runFrame();

    amiga::Amiga hw;
    amiga::Paula paula;
    std::string savePath;  // where the hiscore file (PHIL_03) is written

    // CPU state helpers
    uint32_t pc() const;
    uint64_t instructions() const { return instrCount_; }
    void onFrame() override;
    void onInterrupt(int level) override;

    // called by the Musashi hook
    void hook(uint32_t pc);

    // debugging: report writes to [watchLo, watchHi)
    uint32_t watchLo = 0, watchHi = 0;
    uint32_t ring[64] = {};
    uint32_t ringPos = 0;
    void checkWatch(uint32_t a, uint32_t v, int n);

    std::FILE* pcTrace = nullptr;  // debugging: every executed PC
    std::set<uint32_t> reportPcs;  // debugging: report when these addresses execute

    // code coverage: executed instruction addresses (chip RAM range)
    std::vector<uint8_t> executed = std::vector<uint8_t>(0x80000, 0);

private:
    void loadFileCall(bool phil00);
    void doRts();
    const amiga::Adf& adf_;
    bool frameDone_ = false;
    bool irqDirty_ = false;
    uint64_t instrCount_ = 0;
    // execution time of each basic block of the original code, by start
    // address (same table the translated code uses)
    std::vector<uint16_t> blockCost_ = std::vector<uint16_t>(0x80000, 0);
};
