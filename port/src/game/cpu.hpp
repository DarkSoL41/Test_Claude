// Execution model of the translated 68000 code.
//
// The game logic is translated routine by routine from the original 68000
// code. Routines communicate through the 68000 register file and condition
// codes exactly like the original, so they are kept in a global `cpu` state.
// Memory accesses go through the Amiga bus (chip RAM + custom chips), so all
// game variables live at their original addresses.
#pragma once
#include <cstdint>
#include <cstdio>

#include "amiga/amiga.hpp"

namespace game {

struct Cpu {
    uint32_t d[8] = {};
    uint32_t a[8] = {};
    bool x = false, n = false, z = false, v = false, c = false;
    uint16_t srHigh = 0x2000;  // supervisor bit + interrupt mask (upper byte of SR)
};

extern Cpu cpu;
extern amiga::Amiga* bus;

#define D0 game::cpu.d[0]
#define D1 game::cpu.d[1]
#define D2 game::cpu.d[2]
#define D3 game::cpu.d[3]
#define D4 game::cpu.d[4]
#define D5 game::cpu.d[5]
#define D6 game::cpu.d[6]
#define D7 game::cpu.d[7]
#define A0 game::cpu.a[0]
#define A1 game::cpu.a[1]
#define A2 game::cpu.a[2]
#define A3 game::cpu.a[3]
#define A4 game::cpu.a[4]
#define A5 game::cpu.a[5]
#define A6 game::cpu.a[6]
#define A7 game::cpu.a[7]

// ---- debugging (test builds only) -------------------------------------------
#ifdef SUPAPLEX_TRACE
extern uint32_t tracePc;
extern uint32_t traceRing[64];
extern uint32_t traceRingPos;
extern uint32_t watchLo, watchHi;
void traceWrite(uint32_t addr, uint32_t value, int size);
extern std::FILE* tracePcFile;
#define TRACE_PC(x) (game::tracePc = (x), game::traceRing[game::traceRingPos++ & 63] = (x), \
                     game::tracePcFile ? (void)std::fprintf(game::tracePcFile, "%06X\n", unsigned(x)) : (void)0)
#define TRACE_WRITE(a, v, n) do { if ((a) + (n) > game::watchLo && (a) < game::watchHi) game::traceWrite(a, v, n); } while (0)
#define TRACE_HWREAD(a, n) do { if (game::tracePcFile) std::fprintf(game::tracePcFile, "R%d %06X line %d\n", n, unsigned(a), game::bus->beamLine()); } while (0)
#else
#define TRACE_PC(x) ((void)0)
#define TRACE_WRITE(a, v, n) ((void)0)
#define TRACE_HWREAD(a, n) ((void)0)
#endif

// ---- memory ----------------------------------------------------------------
// Accesses to chip RAM are plain memory accesses. Accesses to the custom chips
// or CIAs may raise an interrupt; like on the 68000 it is taken after the
// access has completed.
extern bool irqPending;
void serviceInterrupts();

inline uint32_t rd8(uint32_t a) {
    a &= 0xFFFFFF;
    if (a < amiga::kChipSize) return bus->chip()[a];
    TRACE_HWREAD(a, 1);
    uint32_t v = bus->rd8(a);
    if (irqPending) serviceInterrupts();
    return v;
}
inline uint32_t rd16(uint32_t a) {
    a &= 0xFFFFFF;
    if (a < amiga::kChipSize - 1) { const uint8_t* p = bus->chip() + a; return uint32_t(p[0] << 8 | p[1]); }
    TRACE_HWREAD(a, 2);
    uint32_t v = bus->rd16(a);
    if (irqPending) serviceInterrupts();
    return v;
}
inline uint32_t rd32(uint32_t a) {
    a &= 0xFFFFFF;
    if (a < amiga::kChipSize - 3) {
        const uint8_t* p = bus->chip() + a;
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }
    TRACE_HWREAD(a, 4);
    uint32_t v = bus->rd32(a);
    if (irqPending) serviceInterrupts();
    return v;
}
inline void wr8(uint32_t a, uint32_t v) {
    a &= 0xFFFFFF;
    TRACE_WRITE(a, v & 0xFF, 1);
    if (a < amiga::kChipSize) { bus->chip()[a] = uint8_t(v); return; }
    bus->wr8(a, uint8_t(v));
    if (irqPending) serviceInterrupts();
}
inline void wr16(uint32_t a, uint32_t v) {
    a &= 0xFFFFFF;
    TRACE_WRITE(a, v & 0xFFFF, 2);
    if (a < amiga::kChipSize - 1) { uint8_t* p = bus->chip() + a; p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); return; }
    bus->wr16(a, uint16_t(v));
    if (irqPending) serviceInterrupts();
}
inline void wr32(uint32_t a, uint32_t v) {
    a &= 0xFFFFFF;
    TRACE_WRITE(a, v, 4);
    if (a < amiga::kChipSize - 3) {
        uint8_t* p = bus->chip() + a;
        p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
        return;
    }
    bus->wr32(a, v);
    if (irqPending) serviceInterrupts();
}

// ---- sized register writes -------------------------------------------------
inline void setB(uint32_t& r, uint32_t v) { r = (r & 0xFFFFFF00u) | (v & 0xFF); }
inline void setW(uint32_t& r, uint32_t v) { r = (r & 0xFFFF0000u) | (v & 0xFFFF); }
inline void setL(uint32_t& r, uint32_t v) { r = v; }

// sign extension
inline uint32_t sxb(uint32_t v) { return uint32_t(int32_t(int8_t(v))); }
inline uint32_t sxw(uint32_t v) { return uint32_t(int32_t(int16_t(v))); }

// ---- stack -----------------------------------------------------------------
inline void push32(uint32_t v) { A7 -= 4; wr32(A7, v); }
inline uint32_t pop32() { uint32_t v = rd32(A7); A7 += 4; return v; }

// ---- condition codes -------------------------------------------------------
template <int S> struct Sz;
template <> struct Sz<1> { static constexpr uint32_t mask = 0xFF, msb = 0x80; };
template <> struct Sz<2> { static constexpr uint32_t mask = 0xFFFF, msb = 0x8000; };
template <> struct Sz<4> { static constexpr uint32_t mask = 0xFFFFFFFFu, msb = 0x80000000u; };

// logical result: N,Z from result, V=C=0
template <int S> inline uint32_t logic(uint32_t r) {
    r &= Sz<S>::mask;
    cpu.n = (r & Sz<S>::msb) != 0;
    cpu.z = r == 0;
    cpu.v = cpu.c = false;
    return r;
}

template <int S> inline uint32_t add(uint32_t s, uint32_t d) {
    const uint32_t m = Sz<S>::mask, h = Sz<S>::msb;
    s &= m; d &= m;
    uint32_t r = (s + d) & m;
    cpu.n = (r & h) != 0;
    cpu.z = r == 0;
    cpu.v = ((s ^ r) & (d ^ r) & h) != 0;
    cpu.c = cpu.x = (uint64_t(s) + uint64_t(d)) > m;
    return r;
}

template <int S> inline uint32_t sub(uint32_t s, uint32_t d) {  // d - s
    const uint32_t m = Sz<S>::mask, h = Sz<S>::msb;
    s &= m; d &= m;
    uint32_t r = (d - s) & m;
    cpu.n = (r & h) != 0;
    cpu.z = r == 0;
    cpu.v = ((s ^ d) & (r ^ d) & h) != 0;
    cpu.c = cpu.x = s > d;
    return r;
}

template <int S> inline void cmp(uint32_t s, uint32_t d) {  // flags of d - s, X unaffected
    const uint32_t m = Sz<S>::mask, h = Sz<S>::msb;
    s &= m; d &= m;
    uint32_t r = (d - s) & m;
    cpu.n = (r & h) != 0;
    cpu.z = r == 0;
    cpu.v = ((s ^ d) & (r ^ d) & h) != 0;
    cpu.c = s > d;
}

template <int S> inline uint32_t neg(uint32_t d) { return sub<S>(d, 0); }

// shifts / rotates with an immediate or register count (count already & 63)
template <int S> inline uint32_t lsl(uint32_t d, uint32_t cnt) {
    const uint32_t m = Sz<S>::mask; constexpr int bits = S * 8;
    d &= m;
    if (cnt == 0) { cpu.c = false; cpu.v = false; cpu.n = (d & Sz<S>::msb) != 0; cpu.z = d == 0; return d; }
    uint32_t r;
    if (int(cnt) <= bits) { cpu.c = cpu.x = ((uint64_t(d) << cnt) >> bits) & 1; r = uint32_t((uint64_t(d) << cnt) & m); }
    else { cpu.c = cpu.x = false; r = 0; }
    cpu.v = false; cpu.n = (r & Sz<S>::msb) != 0; cpu.z = r == 0;
    return r;
}
template <int S> inline uint32_t lsr(uint32_t d, uint32_t cnt) {
    const uint32_t m = Sz<S>::mask; constexpr int bits = S * 8;
    d &= m;
    if (cnt == 0) { cpu.c = false; cpu.v = false; cpu.n = (d & Sz<S>::msb) != 0; cpu.z = d == 0; return d; }
    uint32_t r;
    if (int(cnt) <= bits) { cpu.c = cpu.x = (uint64_t(d) >> (cnt - 1)) & 1; r = uint32_t(uint64_t(d) >> cnt); }
    else { cpu.c = cpu.x = false; r = 0; }
    cpu.v = false; cpu.n = (r & Sz<S>::msb) != 0; cpu.z = r == 0;
    return r;
}
template <int S> inline uint32_t asr(uint32_t d, uint32_t cnt) {
    const uint32_t m = Sz<S>::mask; constexpr int bits = S * 8;
    d &= m;
    int64_t sv = (d & Sz<S>::msb) ? int64_t(d) - (int64_t(m) + 1) : int64_t(d);
    if (cnt == 0) { cpu.c = false; cpu.v = false; cpu.n = (d & Sz<S>::msb) != 0; cpu.z = d == 0; return d; }
    uint32_t r;
    if (int(cnt) < bits) { cpu.c = cpu.x = (sv >> (cnt - 1)) & 1; r = uint32_t(sv >> cnt) & m; }
    else { cpu.c = cpu.x = sv < 0; r = sv < 0 ? m : 0; }
    cpu.v = false; cpu.n = (r & Sz<S>::msb) != 0; cpu.z = r == 0;
    return r;
}
template <int S> inline uint32_t asl(uint32_t d, uint32_t cnt) {
    const uint32_t m = Sz<S>::mask; constexpr int bits = S * 8;
    d &= m;
    if (cnt == 0) { cpu.c = false; cpu.v = false; cpu.n = (d & Sz<S>::msb) != 0; cpu.z = d == 0; return d; }
    // V is set if the sign bit changes at any time during the shift
    bool v = false;
    uint32_t r = d;
    bool c = false;
    for (uint32_t i = 0; i < cnt; i++) {
        c = (r & Sz<S>::msb) != 0;
        r = (r << 1) & m;
        if (((r & Sz<S>::msb) != 0) != c) v = true;
    }
    if (int(cnt) > bits) c = false;
    cpu.c = cpu.x = c; cpu.v = v; cpu.n = (r & Sz<S>::msb) != 0; cpu.z = r == 0;
    return r;
}
template <int S> inline uint32_t rol(uint32_t d, uint32_t cnt) {
    const uint32_t m = Sz<S>::mask; constexpr int bits = S * 8;
    d &= m;
    uint32_t r = d;
    if (cnt) {
        uint32_t k = cnt % bits;
        r = k ? (((d << k) | (d >> (bits - k))) & m) : d;
        cpu.c = r & 1;
    } else cpu.c = false;
    cpu.v = false; cpu.n = (r & Sz<S>::msb) != 0; cpu.z = r == 0;
    return r;
}
template <int S> inline uint32_t ror(uint32_t d, uint32_t cnt) {
    const uint32_t m = Sz<S>::mask; constexpr int bits = S * 8;
    d &= m;
    uint32_t r = d;
    if (cnt) {
        uint32_t k = cnt % bits;
        r = k ? (((d >> k) | (d << (bits - k))) & m) : d;
        cpu.c = (r & Sz<S>::msb) != 0;
    } else cpu.c = false;
    cpu.v = false; cpu.n = (r & Sz<S>::msb) != 0; cpu.z = r == 0;
    return r;
}
template <int S> inline uint32_t roxl(uint32_t d, uint32_t cnt) {
    const uint32_t m = Sz<S>::mask;
    d &= m;
    uint32_t r = d;
    bool x = cpu.x;
    for (uint32_t i = 0; i < cnt; i++) {
        bool out = (r & Sz<S>::msb) != 0;
        r = ((r << 1) | (x ? 1 : 0)) & m;
        x = out;
    }
    cpu.x = x; cpu.c = x;
    cpu.v = false; cpu.n = (r & Sz<S>::msb) != 0; cpu.z = r == 0;
    return r;
}
template <int S> inline uint32_t roxr(uint32_t d, uint32_t cnt) {
    const uint32_t m = Sz<S>::mask;
    d &= m;
    uint32_t r = d;
    bool x = cpu.x;
    for (uint32_t i = 0; i < cnt; i++) {
        bool out = r & 1;
        r = (r >> 1) | (x ? Sz<S>::msb : 0);
        x = out;
    }
    cpu.x = x; cpu.c = x;
    cpu.v = false; cpu.n = (r & Sz<S>::msb) != 0; cpu.z = r == 0;
    return r;
}

inline uint32_t mulu(uint32_t s, uint32_t d) {
    uint32_t r = (s & 0xFFFF) * (d & 0xFFFF);
    cpu.n = (r & 0x80000000u) != 0; cpu.z = r == 0; cpu.v = cpu.c = false;
    return r;
}
inline uint32_t muls(uint32_t s, uint32_t d) {
    uint32_t r = uint32_t(int32_t(int16_t(s)) * int32_t(int16_t(d)));
    cpu.n = (r & 0x80000000u) != 0; cpu.z = r == 0; cpu.v = cpu.c = false;
    return r;
}
// returns the new register value (unchanged on overflow)
uint32_t divu(uint32_t s, uint32_t d);
uint32_t divs(uint32_t s, uint32_t d);

// bit test: sets Z = !bit
inline void btst(uint32_t v, uint32_t bit) { cpu.z = !((v >> bit) & 1); }

// SR / CCR
inline uint32_t getCCR() { return (cpu.x << 4) | (cpu.n << 3) | (cpu.z << 2) | (cpu.v << 1) | uint32_t(cpu.c); }
inline void setCCR(uint32_t v) { cpu.x = v & 16; cpu.n = v & 8; cpu.z = v & 4; cpu.v = v & 2; cpu.c = v & 1; }
inline uint32_t getSR() { return (cpu.srHigh & 0xFF00) | getCCR(); }
void setSR(uint32_t v);  // may unmask pending interrupts

// conditions
#define CC_HI (!game::cpu.c && !game::cpu.z)
#define CC_LS (game::cpu.c || game::cpu.z)
#define CC_CC (!game::cpu.c)
#define CC_CS (game::cpu.c)
#define CC_NE (!game::cpu.z)
#define CC_EQ (game::cpu.z)
#define CC_VC (!game::cpu.v)
#define CC_VS (game::cpu.v)
#define CC_PL (!game::cpu.n)
#define CC_MI (game::cpu.n)
#define CC_GE (game::cpu.n == game::cpu.v)
#define CC_LT (game::cpu.n != game::cpu.v)
#define CC_GT (!game::cpu.z && game::cpu.n == game::cpu.v)
#define CC_LE (game::cpu.z || game::cpu.n != game::cpu.v)
#define CC_T true
#define CC_F false

// execution time of a basic block (68000 cycles, emitted at every block start)
inline void tick(uint32_t cycles) { bus->addCpuCycles(cycles); }

// dispatch of computed jumps/calls (generated)
void callAddress(uint32_t addr);

// bsr/jsr: the return address goes on the emulated stack like on the 68000
inline void call(void (*routine)(), uint32_t returnAddress) {
    push32(returnAddress);
    routine();
    A7 += 4;
}
inline void callIndirect(uint32_t target, uint32_t returnAddress) {
    push32(returnAddress);
    callAddress(target);
    A7 += 4;
}

}  // namespace game
