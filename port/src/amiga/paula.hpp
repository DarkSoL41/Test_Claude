// Paula audio DMA emulation (4 channels, PAL timing).
#pragma once
#include <cstdint>
#include <vector>

namespace amiga {

class Amiga;

struct Paula {
    explicit Paula(Amiga& m, int outputRate = 48000);

    void write(uint32_t reg, uint16_t v);          // AUDxLC/LEN/PER/VOL/DAT
    void dmaChanged(uint16_t oldDmacon, uint16_t newDmacon);
    void runLine();                                // advance one PAL scanline

    // interleaved stereo int16 samples produced since the last call
    std::vector<int16_t>& samples() { return out_; }

private:
    struct Channel {
        uint32_t lc = 0;       // latched location
        uint16_t len = 0;      // latched length (words)
        uint16_t per = 0, vol = 0;
        bool on = false;
        uint32_t ptr = 0;      // current DMA pointer
        uint32_t remain = 0;   // words left in the current pass
        uint16_t word = 0;     // current data word
        int byteIdx = 0;       // 0 = high byte, 1 = low byte
        double counter = 0;    // colour clocks left for the current byte
        int8_t cur = 0;        // current output sample
    };
    void start(Channel& c);
    void stepByte(Channel& c, int idx);

    Amiga& m_;
    Channel ch_[4];
    int rate_;
    double ccPerSample_;
    double samplePhase_ = 0;
    double lpL_ = 0, lpR_ = 0, lpA_;
    std::vector<int16_t> out_;
};

}  // namespace amiga
