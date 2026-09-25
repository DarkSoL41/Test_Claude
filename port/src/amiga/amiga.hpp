// Supaplex (Amiga, 1991) port — Amiga hardware layer.
//
// The original game talks to the Amiga custom chips directly. The port keeps
// the game's data in an emulated 512 KB chip RAM at the original addresses and
// implements the few pieces of hardware the game uses natively:
//   * blitter (area mode; the game only uses A->D copies),
//   * copper + bitplane display (rendered line by line into an RGB buffer),
//   * Paula audio DMA (4 channels, mixed to stereo),
//   * CIA-A (fire buttons, keyboard, timer A) and joystick/mouse counters.
//
// Time model: the game synchronises by polling the beam position
// (VPOSR/VHPOSR). Every such read advances the virtual beam by one scanline;
// other polling reads of hardware registers (fire buttons, keyboard, interrupt
// state) advance it by kPollClocks colour clocks, so busy loops terminate.
// When the beam wraps from line 312 to 0 a frame is complete; the display is
// presented and the vertical blank interrupt is raised. The same model is used
// by the reference emulator (Musashi) so both run in lockstep.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace amiga {

constexpr uint32_t kChipSize = 0x80000;   // 512 KB chip RAM
constexpr int kLinesPerFrame = 313;       // PAL
constexpr int kOutWidth = 640;            // output in hires pixels
constexpr int kOutHeight = 256;           // lines $2C..$12B
constexpr int kFirstOutLine = 0x2C;
constexpr int kClocksPerLine = 227;
constexpr int kPollClocks = 16;         // time passing per polling read of a hardware register

struct Paula;

// Interface for the "machine" around the hardware: raised interrupts and
// finished frames are forwarded here (to Musashi in the reference emulator, to
// the native interrupt handlers and the SDL front end in the port).
struct Host {
    virtual ~Host() = default;
    virtual void onFrame() {}                   // a frame is complete (beam wrapped)
    virtual void onInterrupt(int /*level*/) {}  // an interrupt became pending
    virtual void onButtonRead() {}              // the game is about to read the fire / mouse buttons
};

class Amiga {
public:
    Amiga();
    ~Amiga();

    // ---- memory bus -----------------------------------------------------
    uint8_t rd8(uint32_t a);
    uint16_t rd16(uint32_t a);
    uint32_t rd32(uint32_t a) { return (uint32_t(rd16(a)) << 16) | rd16(a + 2); }
    void wr8(uint32_t a, uint8_t v);
    void wr16(uint32_t a, uint16_t v);
    void wr32(uint32_t a, uint32_t v) { wr16(a, uint16_t(v >> 16)); wr16(a + 2, uint16_t(v)); }

    // raw chip RAM access (no side effects)
    uint8_t* chip() { return chip_.data(); }
    uint32_t chipL(uint32_t a) const { return uint32_t(chipW(a)) << 16 | chipW(a + 2); }
    uint16_t chipW(uint32_t a) const { a &= kChipSize - 1; return uint16_t(chip_[a] << 8 | chip_[(a + 1) & (kChipSize - 1)]); }
    void load(uint32_t addr, const std::vector<uint8_t>& data);

    // ---- host / input ----------------------------------------------------
    void setHost(Host* h) { host_ = h; }
    void setPaula(Paula* p) { paula_ = p; }

    // joystick in port 2 (JOY1DAT) and fire button
    void setJoystick(bool up, bool down, bool left, bool right, bool fire);
    // mouse in port 1: relative motion in lowres pixels, buttons
    void moveMouse(int dx, int dy);
    void setMouseButtons(bool left, bool right);
    // current mouse counters (JOY0DAT low byte = x, high byte = y)
    uint8_t mouseCounterX() const { return joyX_; }
    uint8_t mouseCounterY() const { return joyY_; }
    // raw Amiga key code (0..127), pressed/released. Queued and delivered
    // through the CIA-A serial port interrupt one at a time.
    void keyEvent(uint8_t rawCode, bool pressed);

    // ---- state visible to the host --------------------------------------
    uint16_t intena() const { return intena_; }
    uint16_t intreq() const { return intreq_; }
    int pendingInterruptLevel() const;   // highest enabled & requested level, 0 if none
    int beamLine() const { return vpos_; }
    const uint32_t* frameBuffer() const { return front_.data(); }  // kOutWidth x kOutHeight ARGB
    uint64_t frameCount() const { return frames_; }
    int pollClock() const { return hclock_; }

    // advance the beam by n lines (used by the VPOSR/VHPOSR read model)
    void advanceLines(int n);

private:
    // custom chip registers
    uint16_t customRead(uint32_t reg);
    void customWrite(uint32_t reg, uint16_t v);
    uint8_t ciaRead(uint32_t a);
    uint8_t ciaReadValue(uint32_t a);
    void ciaWrite(uint32_t a, uint8_t v);

    void doBlit();
    void startFrame();
    void runCopperUntil(int line, int hpos);
    bool copperIdleForLine(int line) const;
    void renderLine(int line);
    void spriteDma(int line);
    void finishFrame();
    void deliverKey();
    void updateIrqLevel();
    void pollTick();

    std::vector<uint8_t> chip_;
    Host* host_ = nullptr;
    Paula* paula_ = nullptr;

    // registers
    uint16_t dmacon_ = 0, intena_ = 0, intreq_ = 0, adkcon_ = 0;
    uint16_t bltcon0_ = 0, bltcon1_ = 0, bltafwm_ = 0xffff, bltalwm_ = 0xffff;
    uint32_t bltpt_[4] = {};     // A,B,C,D
    int16_t bltmod_[4] = {};
    uint16_t bltdat_[3] = {};    // A,B,C data registers
    int blitW_ = 0, blitH_ = 0;
    uint32_t cop1lc_ = 0, cop2lc_ = 0, copPc_ = 0;
    bool copWaiting_ = false;
    uint16_t copWaitV_ = 0, copWaitH_ = 0, copMaskV_ = 0, copMaskH_ = 0;
    uint16_t diwstrt_ = 0x2c81, diwstop_ = 0x2cc1, ddfstrt_ = 0x38, ddfstop_ = 0xd0;
    uint16_t bplcon0_ = 0, bplcon1_ = 0, bplcon2_ = 0;
    int16_t bpl1mod_ = 0, bpl2mod_ = 0;
    uint32_t bplpt_[6] = {};
    uint16_t color_[32] = {};
    uint32_t sprpt_[8] = {};
    struct Sprite {
        int vstart = 0, vstop = 0, hstart = 0;
        bool armed = false;
        uint16_t dataA = 0, dataB = 0;
    } spr_[8];
    uint16_t potgo_ = 0;

    // beam
    int vpos_ = 0;
    int hclock_ = 0;  // colour clocks consumed by polling reads within the current line
    int lastIrqLevel_ = 0;
    uint64_t frames_ = 0;

    // input
    uint8_t joyX_ = 0, joyY_ = 0;           // port 1 (mouse) counters
    uint16_t joy1dat_ = 0;                 // port 2 (joystick) value
    bool fire0_ = false, fire1_ = false, rmb_ = false;

    // CIA-A
    uint8_t ciaaPra_ = 0xff, ciaaDdra_ = 0x03, ciaaIcrMask_ = 0, ciaaIcr_ = 0;
    uint8_t ciaaCra_ = 0, ciaaCrb_ = 0, ciaaSdr_ = 0;
    uint16_t ciaaTaLatch_ = 0xffff, ciaaTa_ = 0xffff;
    uint8_t ciabPrb_ = 0xff;
    std::vector<uint8_t> keyQueue_;
    int keyHold_ = 0;

    std::vector<uint32_t> back_, front_;
    uint32_t rgb_[4096];
};

}  // namespace amiga
