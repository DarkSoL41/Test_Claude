#include "amiga.hpp"

#include <cstdio>
#include <algorithm>
#include <cstring>

#include "paula.hpp"

namespace amiga {

namespace {
constexpr uint32_t kCustomBase = 0xDFF000;
// custom register offsets used by the game
enum : uint32_t {
    DMACONR = 0x002, VPOSR = 0x004, VHPOSR = 0x006, JOY0DAT = 0x00A, JOY1DAT = 0x00C,
    ADKCONR = 0x010, POTGOR = 0x016, INTENAR = 0x01C, INTREQR = 0x01E,
    BLTCON0 = 0x040, BLTCON1 = 0x042, BLTAFWM = 0x044, BLTALWM = 0x046,
    BLTCPTH = 0x048, BLTBPTH = 0x04C, BLTAPTH = 0x050, BLTDPTH = 0x054, BLTSIZE = 0x058,
    BLTCMOD = 0x060, BLTBMOD = 0x062, BLTAMOD = 0x064, BLTDMOD = 0x066,
    BLTCDAT = 0x070, BLTBDAT = 0x072, BLTADAT = 0x074,
    COP1LCH = 0x080, COP2LCH = 0x084, COPJMP1 = 0x088, COPJMP2 = 0x08A,
    DIWSTRT = 0x08E, DIWSTOP = 0x090, DDFSTRT = 0x092, DDFSTOP = 0x094,
    DMACON = 0x096, INTENA = 0x09A, INTREQ = 0x09C, ADKCON = 0x09E,
    AUD0LCH = 0x0A0, BPL1PTH = 0x0E0, BPLCON0 = 0x100, BPLCON1 = 0x102, BPLCON2 = 0x104,
    BPL1MOD = 0x108, BPL2MOD = 0x10A, COLOR00 = 0x180, POTGO = 0x034,
};
}  // namespace

Amiga::Amiga() : chip_(kChipSize, 0), back_(kOutWidth * kOutHeight, 0), front_(kOutWidth * kOutHeight, 0) {
    for (int i = 0; i < 4096; i++) {
        uint32_t r = (i >> 8) & 15, g = (i >> 4) & 15, b = i & 15;
        rgb_[i] = 0xFF000000u | (r * 17) << 16 | (g * 17) << 8 | (b * 17);
    }
    startFrame();
}

Amiga::~Amiga() = default;

void Amiga::load(uint32_t addr, const std::vector<uint8_t>& data) {
    for (size_t i = 0; i < data.size(); i++) chip_[(addr + i) & (kChipSize - 1)] = data[i];
}

// ---------------------------------------------------------------------------
// bus

uint8_t Amiga::rd8(uint32_t a) {
    a &= 0xFFFFFF;
    if (a < kChipSize) return chip_[a];
    if ((a & 0xFFF000) == 0xDFF000) {
        uint16_t w = customRead(a & 0x1FE);
        return (a & 1) ? uint8_t(w) : uint8_t(w >> 8);
    }
    if ((a & 0xFF0000) == 0xBF0000) return ciaRead(a);
    return 0;
}

uint16_t Amiga::rd16(uint32_t a) {
    a &= 0xFFFFFF;
    if (a < kChipSize - 1) return uint16_t(chip_[a] << 8 | chip_[a + 1]);
    if ((a & 0xFFF000) == 0xDFF000) return customRead(a & 0x1FE);
    if ((a & 0xFF0000) == 0xBF0000) return uint16_t(ciaRead(a) << 8 | ciaRead(a + 1));
    return 0;
}

void Amiga::wr8(uint32_t a, uint8_t v) {
    a &= 0xFFFFFF;
    if (a < kChipSize) { chip_[a] = v; return; }
    if ((a & 0xFFF000) == 0xDFF000) {
        // byte writes to custom registers put the byte on both halves of the bus
        customWrite(a & 0x1FE, uint16_t(v << 8 | v));
        return;
    }
    if ((a & 0xFF0000) == 0xBF0000) ciaWrite(a, v);
}

void Amiga::wr16(uint32_t a, uint16_t v) {
    a &= 0xFFFFFF;
    if (a < kChipSize - 1) { chip_[a] = uint8_t(v >> 8); chip_[a + 1] = uint8_t(v); return; }
    if ((a & 0xFFF000) == 0xDFF000) { customWrite(a & 0x1FE, v); return; }
    if ((a & 0xFF0000) == 0xBF0000) { ciaWrite(a, uint8_t(v >> 8)); ciaWrite(a + 1, uint8_t(v)); }
}

// ---------------------------------------------------------------------------
// custom chips

uint16_t Amiga::customRead(uint32_t reg) {
    switch (reg) {
    case DMACONR: return uint16_t((dmacon_ & 0x03FF) | 0x2000);  // blitter never busy, BZERO set
    case VPOSR:
        advanceLines(1);
        return uint16_t(((vpos_ >> 8) & 1));
    case VHPOSR:
        advanceLines(1);
        return uint16_t((vpos_ & 0xFF) << 8 | (hclock_ & 0xFF));
    case JOY0DAT: pollTick(); return uint16_t(joyY_ << 8 | joyX_);
    case JOY1DAT: pollTick(); return joy1dat_;
    case ADKCONR: pollTick(); return adkcon_;
    case POTGOR: pollTick(); return uint16_t(0xFF00 & ~(rmb_ ? 0x0400 : 0));
    case INTENAR: pollTick(); return intena_;
    case INTREQR: pollTick(); return intreq_;
    default: return 0;
    }
}

void Amiga::customWrite(uint32_t reg, uint16_t v) {
    if (reg >= COLOR00 && reg < COLOR00 + 64) { color_[(reg - COLOR00) >> 1] = v & 0xFFF; return; }
    if (reg >= BPL1PTH && reg < BPL1PTH + 24) {
        int p = (reg - BPL1PTH) >> 2;
        if (reg & 2) bplpt_[p] = (bplpt_[p] & 0xFFFF0000u) | (v & 0xFFFE);
        else bplpt_[p] = (bplpt_[p] & 0xFFFF) | (uint32_t(v & 0x1F) << 16);
        return;
    }
    if (reg >= 0x120 && reg < 0x140) {
        int n = (reg - 0x120) >> 2;
        if (reg & 2) sprpt_[n] = (sprpt_[n] & 0xFFFF0000u) | (v & 0xFFFE);
        else sprpt_[n] = (sprpt_[n] & 0xFFFF) | (uint32_t(v & 0x1F) << 16);
        return;
    }
    if (reg >= AUD0LCH && reg < AUD0LCH + 0x40) {
        if (paula_) paula_->write(reg, v);
        return;
    }
    auto setPtr = [&](uint32_t& p) {
        if (reg & 2) p = (p & 0xFFFF0000u) | (v & 0xFFFE);
        else p = (p & 0xFFFF) | (uint32_t(v & 0x1F) << 16);
    };
    switch (reg) {
    case BLTCON0: bltcon0_ = v; break;
    case BLTCON1: bltcon1_ = v; break;
    case BLTAFWM: bltafwm_ = v; break;
    case BLTALWM: bltalwm_ = v; break;
    case BLTCPTH: case BLTCPTH + 2: setPtr(bltpt_[2]); break;
    case BLTBPTH: case BLTBPTH + 2: setPtr(bltpt_[1]); break;
    case BLTAPTH: case BLTAPTH + 2: setPtr(bltpt_[0]); break;
    case BLTDPTH: case BLTDPTH + 2: setPtr(bltpt_[3]); break;
    case BLTCMOD: bltmod_[2] = int16_t(v & 0xFFFE); break;
    case BLTBMOD: bltmod_[1] = int16_t(v & 0xFFFE); break;
    case BLTAMOD: bltmod_[0] = int16_t(v & 0xFFFE); break;
    case BLTDMOD: bltmod_[3] = int16_t(v & 0xFFFE); break;
    case BLTCDAT: bltdat_[2] = v; break;
    case BLTBDAT: bltdat_[1] = v; break;
    case BLTADAT: bltdat_[0] = v; break;
    case BLTSIZE: {
        int h = v >> 6, w = v & 63;
        blitH_ = h ? h : 1024;
        blitW_ = w ? w : 64;
        doBlit();
        break;
    }
    case COP1LCH: case COP1LCH + 2: setPtr(cop1lc_); break;
    case COP2LCH: case COP2LCH + 2: setPtr(cop2lc_); break;
    case COPJMP1: copPc_ = cop1lc_; copWaiting_ = false; break;
    case COPJMP2: copPc_ = cop2lc_; copWaiting_ = false; break;
    case DIWSTRT: diwstrt_ = v; break;
    case DIWSTOP: diwstop_ = v; break;
    case DDFSTRT: ddfstrt_ = v & 0xFC; break;
    case DDFSTOP: ddfstop_ = v & 0xFC; break;
    case BPLCON0: bplcon0_ = v; break;
    case BPLCON1: bplcon1_ = v; break;
    case BPLCON2: bplcon2_ = v; break;
    case BPL1MOD: bpl1mod_ = int16_t(v & 0xFFFE); break;
    case BPL2MOD: bpl2mod_ = int16_t(v & 0xFFFE); break;
    case DMACON: {
        uint16_t old = dmacon_;
        if (v & 0x8000) dmacon_ |= (v & 0x7FFF); else dmacon_ &= ~v;
        if (paula_) paula_->dmaChanged(old, dmacon_);
        break;
    }
    case INTENA:
        if (v & 0x8000) intena_ |= (v & 0x7FFF); else intena_ &= ~v;
        updateIrqLevel();
        break;
    case INTREQ:
        if (v & 0x8000) intreq_ |= (v & 0x7FFF); else intreq_ &= ~v;
        updateIrqLevel();
        break;
    case ADKCON:
        if (v & 0x8000) adkcon_ |= (v & 0x7FFF); else adkcon_ &= ~v;
        break;
    case POTGO: potgo_ = v; break;
    default: break;
    }
}

int Amiga::pendingInterruptLevel() const {
    if (!(intena_ & 0x4000)) return 0;
    uint16_t act = intena_ & intreq_ & 0x3FFF;
    if (!act) return 0;
    if (act & 0x2000) return 6;
    if (act & 0x1800) return 5;
    if (act & 0x0780) return 4;
    if (act & 0x0070) return 3;
    if (act & 0x0008) return 2;
    return 1;
}

void Amiga::updateIrqLevel() {
    // the host is told about every change of the interrupt level, also when
    // it drops to zero (the CPU interrupt input is level triggered)
    int l = pendingInterruptLevel();
    if (host_ && (l || lastIrqLevel_)) host_->onInterrupt(l);
    lastIrqLevel_ = l;
}

// Blitter, area mode. Executes synchronously.
void Amiga::doBlit() {
    const bool useA = bltcon0_ & 0x0800, useB = bltcon0_ & 0x0400, useC = bltcon0_ & 0x0200,
               useD = bltcon0_ & 0x0100;
    const uint8_t mt = uint8_t(bltcon0_);
    const int ash = bltcon0_ >> 12, bsh = bltcon1_ >> 12;
    const bool desc = bltcon1_ & 2;
    if (bltcon1_ & 1) {
        std::fprintf(stderr, "blitter line mode not supported\n");
        return;
    }
    const int step = desc ? -2 : 2;
    uint16_t aold = 0, bold = 0;
    bool anyOne = false;
    for (int y = 0; y < blitH_; y++) {
        for (int x = 0; x < blitW_; x++) {
            uint16_t a = bltdat_[0], b = bltdat_[1], c = bltdat_[2];
            if (useA) { a = chipW(bltpt_[0]); bltpt_[0] += step; bltdat_[0] = a; }
            if (useB) { b = chipW(bltpt_[1]); bltpt_[1] += step; bltdat_[1] = b; }
            if (useC) { c = chipW(bltpt_[2]); bltpt_[2] += step; bltdat_[2] = c; }
            uint16_t am = a;
            if (x == 0) am &= bltafwm_;
            if (x == blitW_ - 1) am &= bltalwm_;
            uint16_t as, bs;
            if (!desc) {
                as = uint16_t(((uint32_t(aold) << 16) | am) >> ash);
                bs = uint16_t(((uint32_t(bold) << 16) | b) >> bsh);
            } else {
                as = uint16_t(((uint32_t(am) << 16) | aold) >> (16 - ash));
                bs = uint16_t(((uint32_t(b) << 16) | bold) >> (16 - bsh));
            }
            aold = am;
            bold = b;
            uint16_t d = 0;
            for (int i = 0; i < 8; i++) {
                if (!(mt & (1 << i))) continue;
                uint16_t t = (i & 4) ? as : uint16_t(~as);
                t &= (i & 2) ? bs : uint16_t(~bs);
                t &= (i & 1) ? c : uint16_t(~c);
                d |= t;
            }
            if (d) anyOne = true;
            if (useD) {
                uint32_t p = bltpt_[3] & (kChipSize - 2);
                chip_[p] = uint8_t(d >> 8);
                chip_[p + 1] = uint8_t(d);
                bltpt_[3] += step;
            }
        }
        if (useA) bltpt_[0] += desc ? -bltmod_[0] : bltmod_[0];
        if (useB) bltpt_[1] += desc ? -bltmod_[1] : bltmod_[1];
        if (useC) bltpt_[2] += desc ? -bltmod_[2] : bltmod_[2];
        if (useD) bltpt_[3] += desc ? -bltmod_[3] : bltmod_[3];
    }
    (void)anyOne;
    intreq_ |= 0x0040;  // BLIT
    updateIrqLevel();
}

// ---------------------------------------------------------------------------
// beam, copper and display

void Amiga::startFrame() {
    copPc_ = cop1lc_;
    copWaiting_ = false;
}

// true if the copper cannot do anything before the end of this line
bool Amiga::copperIdleForLine(int line) const {
    if (!(dmacon_ & 0x0200) || !(dmacon_ & 0x0080)) return true;
    if (!copWaiting_) return false;
    int vm = copMaskV_ | 0x80;
    int vb = line & 0xFF & vm, vw = copWaitV_ & vm;
    if (vb < vw) return true;
    if (vb > vw) return false;
    return (0xE2 & copMaskH_) < (copWaitH_ & copMaskH_);
}

void Amiga::runCopperUntil(int line, int hpos) {
    if (!(dmacon_ & 0x0200) || !(dmacon_ & 0x0080)) return;  // DMAEN + COPEN
    for (int guard = 0; guard < 4096; guard++) {
        if (copWaiting_) {
            int vm = copMaskV_ | 0x80;
            int vb = line & 0xFF & vm, vw = copWaitV_ & vm;
            bool ok = vb > vw || (vb == vw && (hpos & copMaskH_) >= (copWaitH_ & copMaskH_));
            if (!ok) return;
            copWaiting_ = false;
        }
        uint16_t w1 = chipW(copPc_), w2 = chipW(copPc_ + 2);
        copPc_ += 4;
        if (!(w1 & 1)) {
            uint32_t reg = w1 & 0x1FE;
            if (reg < 0x40) {  // protected register: copper stops
                copWaiting_ = true; copWaitV_ = 0xFF; copWaitH_ = 0xFE; copMaskV_ = 0x7F; copMaskH_ = 0xFE;
                return;
            }
            customWrite(reg, w2);
        } else {
            uint16_t v = w1 >> 8, h = w1 & 0xFE;
            uint16_t mv = (w2 >> 8) & 0x7F, mh = w2 & 0xFE;
            if (w2 & 1) {  // SKIP
                int vm = mv | 0x80;
                int vb = line & 0xFF & vm, vw = v & vm;
                bool ok = vb > vw || (vb == vw && (hpos & mh) >= (h & mh));
                if (ok) copPc_ += 4;
            } else {
                copWaiting_ = true; copWaitV_ = v; copWaitH_ = h; copMaskV_ = mv; copMaskH_ = mh;
            }
        }
    }
}

void Amiga::spriteDma(int line) {
    if (!(dmacon_ & 0x0200) || !(dmacon_ & 0x0020)) return;  // DMAEN + SPREN
    for (int i = 0; i < 8; i++) {
        Sprite& sp = spr_[i];
        if (line == 0x19) {  // first sprite DMA line: fetch control words
            sp.armed = true;
        }
        if (!sp.armed) continue;
        if (line == 0x19 || line == sp.vstop) {
            uint16_t pos = chipW(sprpt_[i]), ctl = chipW(sprpt_[i] + 2);
            sprpt_[i] += 4;
            sp.vstart = (pos >> 8) | ((ctl & 4) << 6);
            sp.vstop = (ctl >> 8) | ((ctl & 2) << 7);
            sp.hstart = ((pos & 0xFF) << 1) | (ctl & 1);
            sp.dataA = sp.dataB = 0;
            if (pos == 0 && ctl == 0) sp.armed = false;
            continue;
        }
        if (line >= sp.vstart && line < sp.vstop) {
            sp.dataA = chipW(sprpt_[i]);
            sp.dataB = chipW(sprpt_[i] + 2);
            sprpt_[i] += 4;
        } else {
            sp.dataA = sp.dataB = 0;
        }
    }
}

void Amiga::renderLine(int line) {
    const bool inOut = line >= kFirstOutLine && line < kFirstOutLine + kOutHeight;
    uint32_t* out = inOut ? &back_[(line - kFirstOutLine) * kOutWidth] : nullptr;

    // copper activity up to the start of the display fetch
    runCopperUntil(line, 0x18);
    spriteDma(line);

    int vstart = diwstrt_ >> 8;
    int vstop = diwstop_ >> 8;
    if (!(vstop & 0x80)) vstop |= 0x100;
    const int bpu = std::min((bplcon0_ >> 12) & 7, 6);
    const bool hires = bplcon0_ & 0x8000;
    const bool dma = (dmacon_ & 0x0200) && (dmacon_ & 0x0100);
    const bool active = dma && bpu > 0 && line >= vstart && line < vstop;

    int nwords = hires ? ((ddfstop_ - ddfstrt_) >> 2) + 2 : ((ddfstop_ - ddfstrt_) >> 3) + 1;
    if (nwords < 1) nwords = 1;
    if (nwords > 64) nwords = 64;
    uint16_t data[6][64];
    if (active) {
        for (int p = 0; p < bpu; p++) {
            for (int w = 0; w < nwords; w++) data[p][w] = chipW(bplpt_[p] + 2 * w);
            bplpt_[p] += 2 * nwords + ((p & 1) ? bpl2mod_ : bpl1mod_);
        }
    }
    if (!out) {
        if (!copperIdleForLine(line))
            for (int cc = 0x19; cc < 0xE3; cc++) runCopperUntil(line, cc);
        return;
    }

    // playfield colour indices for the 640 output (hires) positions
    uint8_t pix[kOutWidth];
    std::memset(pix, 0, sizeof pix);
    const int hstart = diwstrt_ & 0xFF;
    const int hstop = (diwstop_ & 0xFF) | 0x100;
    if (active) {
        const int sc1 = bplcon1_ & 15, sc2 = (bplcon1_ >> 4) & 15;
        const int total = nwords * 16;
        const int loFrom = std::max(hstart, 0x81), loTo = std::min(hstop, 0x81 + kOutWidth / 2);
        for (int p = 0; p < bpu; p++) {
            const int sc = (p & 1) ? sc2 : sc1;
            const uint8_t bitv = uint8_t(1 << p);
            const uint16_t* pd = data[p];
            if (!hires) {
                int st = loFrom - (0x81 + 2 * (ddfstrt_ - 0x38)) - sc;
                for (int lo = loFrom; lo < loTo; lo++, st++) {
                    if (st < 0 || st >= total) continue;
                    if (pd[st >> 4] & (0x8000 >> (st & 15))) {
                        int x = 2 * (lo - 0x81);
                        pix[x] |= bitv;
                        pix[x + 1] |= bitv;
                    }
                }
            } else {
                int hxFrom = 2 * loFrom, hxTo = 2 * loTo;
                int st = hxFrom - (2 * 0x81 + 4 * (ddfstrt_ - 0x3C)) - 2 * sc;
                for (int hx = hxFrom; hx < hxTo; hx++, st++) {
                    if (st < 0 || st >= total) continue;
                    if (pd[st >> 4] & (0x8000 >> (st & 15))) pix[hx - 2 * 0x81] |= bitv;
                }
            }
        }
    }
    const bool sprOn = (dmacon_ & 0x0200) && (dmacon_ & 0x0020);
    if (sprOn) {
        // sprites are in front of the playfields (BPLCON2 = $24)
        for (int x = 0; x < kOutWidth; x += 2) {
            int lo = (2 * 0x81 + x) >> 1;
            for (int i = 0; i < 8; i++) {
                const Sprite& sp = spr_[i];
                if (!(sp.dataA | sp.dataB)) continue;
                int sx = lo - (sp.hstart + 1);
                if (sx < 0 || sx > 15) continue;
                int bit = 0x8000 >> sx;
                int v = ((sp.dataA & bit) ? 1 : 0) | ((sp.dataB & bit) ? 2 : 0);
                if (v) { pix[x] = pix[x + 1] = uint8_t(16 + (i >> 1) * 4 + v); break; }
            }
        }
    }
    // fast path: the copper waits for a later line, no register changes in this line
    if (copperIdleForLine(line)) {
        for (int x = 0; x < kOutWidth; x++) out[x] = rgb_[color_[pix[x] & 31]];
        return;
    }
    // walk the line in colour clocks; the copper may change colours mid-line
    for (int cc = 0x19; cc < 0xE3; cc++) {
        runCopperUntil(line, cc);
        int x0 = cc * 4 - 2 * 0x81;
        for (int x = x0; x < x0 + 4; x++) {
            if (x < 0 || x >= kOutWidth) continue;
            out[x] = rgb_[color_[pix[x] & 31]];
        }
    }
}

void Amiga::advanceLines(int n) {
    for (int i = 0; i < n; i++) {
        renderLine(vpos_);
        runCopperUntil(vpos_, 0xE2);
        if (paula_) paula_->runLine();
        vpos_++;
        if (vpos_ >= kLinesPerFrame) {
            vpos_ = 0;
            finishFrame();
        }
    }
}

void Amiga::finishFrame() {
    front_.swap(back_);
    frames_++;
    startFrame();
    // CIA-A keyboard: deliver one queued key per frame
    deliverKey();
    intreq_ |= 0x0020;  // VERTB
    if (host_) host_->onFrame();
    updateIrqLevel();
}

// ---------------------------------------------------------------------------
// input

void Amiga::setJoystick(bool up, bool down, bool left, bool right, bool fire) {
    uint16_t v = 0;
    if (right) v |= 0x0002;
    if (left) v |= 0x0200;
    bool b1 = v & 0x0002, b9 = v & 0x0200;
    if (down ^ b1) v |= 0x0001;
    if (up ^ b9) v |= 0x0100;
    joy1dat_ = v;
    fire1_ = fire;
}

void Amiga::moveMouse(int dx, int dy) {
    joyX_ = uint8_t(joyX_ + dx);
    joyY_ = uint8_t(joyY_ + dy);
}

void Amiga::setMouseButtons(bool left, bool right) {
    fire0_ = left;
    rmb_ = right;
}

void Amiga::keyEvent(uint8_t raw, bool pressed) {
    keyQueue_.push_back(uint8_t((raw & 0x7F) | (pressed ? 0 : 0x80)));
}

void Amiga::deliverKey() {
    if (keyQueue_.empty()) return;
    if (ciaaIcr_ & 0x08) return;  // previous byte not yet taken
    uint8_t k = keyQueue_.front();
    keyQueue_.erase(keyQueue_.begin());
    uint8_t raw = k & 0x7F, up = (k >> 7) & 1;
    ciaaSdr_ = uint8_t(~((raw << 1) | up));
    ciaaIcr_ |= 0x08;
    if (ciaaIcrMask_ & 0x08) {
        intreq_ |= 0x0008;  // PORTS
    }
}

// ---------------------------------------------------------------------------
// CIA

// Time model for polling loops that do not read the beam counter (fire
// button, keyboard): every such hardware read lets a few colour clocks pass.
void Amiga::pollTick() {
    hclock_ += kPollClocks;
    if (hclock_ >= kClocksPerLine) {
        hclock_ -= kClocksPerLine;
        advanceLines(1);
    }
}

uint8_t Amiga::ciaRead(uint32_t a) {
    pollTick();
    // timer A of CIA-A advances on every CIA access (deterministic time model)
    if (ciaaCra_ & 1) {
        if (ciaaTa_ <= 64) {
            ciaaIcr_ |= 0x01;
            ciaaTa_ = ciaaTaLatch_;
            if (ciaaCra_ & 0x08) ciaaCra_ &= ~1;  // one-shot
            if (ciaaIcrMask_ & 0x01) { intreq_ |= 0x0008; }
        } else {
            ciaaTa_ -= 64;
        }
    }
    if ((a & 0xF000) == 0xE000 && (a & 1)) {  // CIA-A, odd addresses
        switch ((a >> 8) & 15) {
        case 0x0: {
            uint8_t v = 0xFF;
            if (fire0_) v &= ~0x40;
            if (fire1_) v &= ~0x80;
            v = (v & ~ciaaDdra_) | (ciaaPra_ & ciaaDdra_);
            return v;
        }
        case 0x2: return ciaaDdra_;
        case 0x4: return uint8_t(ciaaTa_);
        case 0x5: return uint8_t(ciaaTa_ >> 8);
        case 0xC: return ciaaSdr_;
        case 0xD: {
            uint8_t v = ciaaIcr_;
            if (v & ciaaIcrMask_) v |= 0x80;
            ciaaIcr_ = 0;
            return v;
        }
        case 0xE: return ciaaCra_;
        case 0xF: return ciaaCrb_;
        default: return 0xFF;
        }
    }
    if ((a & 0xF000) == 0xD000 && !(a & 1)) {  // CIA-B, even addresses
        switch ((a >> 8) & 15) {
        case 0x1: return ciabPrb_;
        default: return 0xFF;
        }
    }
    return 0xFF;
}

void Amiga::ciaWrite(uint32_t a, uint8_t v) {
    if ((a & 0xF000) == 0xE000 && (a & 1)) {
        switch ((a >> 8) & 15) {
        case 0x0: ciaaPra_ = v; break;
        case 0x2: ciaaDdra_ = v; break;
        case 0x4: ciaaTaLatch_ = uint16_t((ciaaTaLatch_ & 0xFF00) | v); break;
        case 0x5:
            ciaaTaLatch_ = uint16_t((ciaaTaLatch_ & 0x00FF) | (v << 8));
            if (!(ciaaCra_ & 1)) ciaaTa_ = ciaaTaLatch_;
            if (ciaaCra_ & 0x08) { ciaaTa_ = ciaaTaLatch_; ciaaCra_ |= 1; }  // one-shot starts
            break;
        case 0xC: ciaaSdr_ = v; break;
        case 0xD:
            if (v & 0x80) ciaaIcrMask_ |= (v & 0x7F); else ciaaIcrMask_ &= ~v;
            break;
        case 0xE:
            if (v & 0x10) ciaaTa_ = ciaaTaLatch_;  // force load
            ciaaCra_ = v & ~0x10;
            break;
        case 0xF: ciaaCrb_ = v & ~0x10; break;
        default: break;
        }
        return;
    }
    if ((a & 0xF000) == 0xD000 && !(a & 1)) {
        if (((a >> 8) & 15) == 1) ciabPrb_ = v;
    }
}

}  // namespace amiga
