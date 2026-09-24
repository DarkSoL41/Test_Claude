#include "paula.hpp"

#include <cmath>

#include "amiga.hpp"

namespace amiga {

namespace {
constexpr double kColourClock = 3546895.0;  // PAL
constexpr double kPaulaClocksPerLine = 227.5;
}  // namespace

Paula::Paula(Amiga& m, int outputRate) : m_(m), rate_(outputRate) {
    ccPerSample_ = kColourClock / rate_;
    // A500 fixed output filter: first order low pass around 4.4 kHz
    const double fc = 4400.0;
    lpA_ = 1.0 - std::exp(-2.0 * 3.14159265358979 * fc / rate_);
}

void Paula::write(uint32_t reg, uint16_t v) {
    int n = int((reg - 0xA0) >> 4);
    if (n < 0 || n > 3) return;
    Channel& c = ch_[n];
    switch (reg & 0x0F) {
    case 0x0: c.lc = (c.lc & 0xFFFF) | (uint32_t(v & 0x1F) << 16); break;
    case 0x2: c.lc = (c.lc & 0xFFFF0000u) | (v & 0xFFFE); break;
    case 0x4: c.len = v; break;
    case 0x6: c.per = v; break;
    case 0x8: c.vol = v & 0x7F; if (c.vol > 64) c.vol = 64; break;
    case 0xA: break;  // AUDxDAT: manual mode not used by the game
    default: break;
    }
}

void Paula::start(Channel& c) {
    c.ptr = c.lc;
    c.remain = c.len ? c.len : 0x10000;
    c.word = m_.chipW(c.ptr);
    c.ptr += 2;
    c.byteIdx = 0;
    c.cur = int8_t(c.word >> 8);
    c.counter = c.per < 124 ? 124 : c.per;
    c.on = true;
}

void Paula::dmaChanged(uint16_t oldD, uint16_t newD) {
    for (int i = 0; i < 4; i++) {
        bool was = (oldD & 0x200) && (oldD & (1 << i));
        bool now = (newD & 0x200) && (newD & (1 << i));
        if (!was && now) start(ch_[i]);
        if (was && !now) { ch_[i].on = false; ch_[i].cur = 0; }
    }
}

void Paula::stepByte(Channel& c, int idx) {
    if (c.byteIdx == 0) {
        c.byteIdx = 1;
        c.cur = int8_t(c.word & 0xFF);
    } else {
        // word finished
        if (--c.remain == 0) {
            c.ptr = c.lc;
            c.remain = c.len ? c.len : 0x10000;
            // audio interrupt request for this channel
            m_.requestInterrupt(uint16_t(0x80 << idx));
        }
        c.word = m_.chipW(c.ptr);
        c.ptr += 2;
        c.byteIdx = 0;
        c.cur = int8_t(c.word >> 8);
    }
}

void Paula::runLine() {
    samplePhase_ += kPaulaClocksPerLine;
    while (samplePhase_ >= ccPerSample_) {
        samplePhase_ -= ccPerSample_;
        int v[4];
        for (int i = 0; i < 4; i++) {
            Channel& c = ch_[i];
            if (!c.on) { v[i] = 0; continue; }
            v[i] = c.cur * int(c.vol);
            c.counter -= ccPerSample_;
            int guard = 0;
            while (c.counter <= 0 && guard++ < 64) {
                stepByte(c, i);
                c.counter += c.per < 124 ? 124 : c.per;
            }
        }
        double l = (v[0] + v[3]) * 2.0, r = (v[1] + v[2]) * 2.0;
        lpL_ += (l - lpL_) * lpA_;
        lpR_ += (r - lpR_) * lpA_;
        auto clip = [](double x) { return int16_t(x > 32767 ? 32767 : (x < -32768 ? -32768 : x)); };
        out_.push_back(clip(lpL_));
        out_.push_back(clip(lpR_));
    }
}

}  // namespace amiga
