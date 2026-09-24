#include "game/runtime.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "game/cpu.hpp"
#include "game/generated/generated.hpp"

namespace game {

Cpu cpu;
amiga::Amiga* bus = nullptr;
bool irqPending = false;

#ifdef SUPAPLEX_TRACE
uint32_t tracePc = 0;
uint32_t traceRing[64] = {};
uint32_t traceRingPos = 0;
uint32_t watchLo = 0, watchHi = 0;
std::FILE* tracePcFile = nullptr;
void traceWrite(uint32_t addr, uint32_t value, int size) {
    std::fprintf(stderr, "port: frame %llu line %d  pc %06X  write.%d $%06X = %X\n",
                 (unsigned long long)bus->frameCount(), bus->beamLine(), tracePc, size, addr, value);
    std::fprintf(stderr, "      trail:");
    for (uint32_t i = 16; i > 0; i--) std::fprintf(stderr, " %06X", traceRing[(traceRingPos - i) & 63]);
    std::fprintf(stderr, "\n");
}
#endif

namespace {
Environment g_env;
int g_irqDepth = 0;
}  // namespace

void attach(amiga::Amiga& hw, const Environment& env) {
    bus = &hw;
    g_env = env;
    cpu = Cpu{};
    cpu.srHigh = 0x2000;
    A7 = 0x80000;
    irqPending = false;
}

void requestInterrupt(int level) { if (level) irqPending = true; }

void setSR(uint32_t v) {
    cpu.srHigh = uint16_t(v & 0xFF00);
    setCCR(v);
    if (bus->pendingInterruptLevel()) irqPending = true;
    if (irqPending) serviceInterrupts();
}

// Take pending interrupts exactly like the 68000: when the level is above the
// current mask, push PC/SR, raise the mask and run the autovector handler.
void serviceInterrupts() {
    irqPending = false;
    for (;;) {
        int level = bus->pendingInterruptLevel();
        int mask = (cpu.srHigh >> 8) & 7;
        if (level == 0 || level <= mask) return;
        const Cpu saved = cpu;
        A7 -= 6;  // exception frame (PC + SR); the contents are not used by the game
        wr16(A7, getSR());
        wr32(A7 + 2, 0);
        cpu.srHigh = uint16_t((cpu.srHigh & 0xF8FF) | (level << 8) | 0x2000);
        if (std::getenv("TRACE_IRQ")) std::fprintf(stderr, "port irq %d frame %llu line %d\n", level, (unsigned long long)bus->frameCount(), bus->beamLine());
        g_irqDepth++;
        callAddress(rd32(0x60 + 4 * uint32_t(level)));
        g_irqDepth--;
        cpu = saved;  // rte
    }
}

uint32_t divu(uint32_t s, uint32_t d) {
    s &= 0xFFFF;
    if (s == 0) { std::fprintf(stderr, "divide by zero\n"); return d; }
    uint32_t q = d / s, r = d % s;
    if (q < 0x10000) {
        cpu.z = q == 0; cpu.n = (q & 0x8000) != 0; cpu.v = false; cpu.c = false;
        return (q & 0xFFFF) | (r << 16);
    }
    cpu.v = true;
    return d;
}

uint32_t divs(uint32_t s, uint32_t d) {
    int32_t sv = int16_t(s);
    if (sv == 0) { std::fprintf(stderr, "divide by zero\n"); return d; }
    int32_t dv = int32_t(d);
    if (dv == int32_t(0x80000000) && sv == -1) { cpu.z = true; cpu.n = false; cpu.v = false; cpu.c = false; return 0; }
    int32_t q = dv / sv, r = dv % sv;
    if (q >= -32768 && q <= 32767) {
        cpu.z = q == 0; cpu.n = (q & 0x8000) != 0; cpu.v = false; cpu.c = false;
        return (uint32_t(q) & 0xFFFF) | (uint32_t(r) << 16);
    }
    cpu.v = true;
    return d;
}

// ---------------------------------------------------------------------------
// Native replacements of the disk routines (the crack's OFS loader and the
// original drive handling). They behave like the original calls as seen by
// the game: data appears at A0, D0 = 0 and Z set on success.

namespace {
std::string fileForCode(uint32_t d1) {
    d1 &= 0xFFFF;
    if (d1 == 0x002) return "PHIL_00";
    if (d1 == 0x082) return "PHIL_01";
    if (d1 == 0x12C) return "PHIL_02";
    if (d1 == 0x0FA) return "PHIL_03";
    if (d1 >= 0x226) {  // levels: PHIL_10 .. PHIL_7E
        char buf[16];
        std::snprintf(buf, sizeof buf, "PHIL_%02X", unsigned((((d1 - 0x226) >> 1) + 0x10) & 0xFF));
        return buf;
    }
    return "";
}
}  // namespace

void native_loadFile() {
    std::string name = fileForCode(D1);
    std::vector<uint8_t> data;
    if (name == "PHIL_03" && !g_env.savePath.empty()) {
        std::ifstream f(g_env.savePath, std::ios::binary);
        if (f) data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    if (data.empty() && g_env.adf) data = g_env.adf->read(name);
    bus->load(A0, data);
    D0 = 0;
    cpu.x = cpu.n = cpu.v = cpu.c = false;
    cpu.z = true;
}

void native_diskInit() {}
void native_diskMotorOff() {}
void native_diskDelay() {}

void native_saveHiscores() {
    if (!g_env.savePath.empty()) {
        std::ofstream f(g_env.savePath, std::ios::binary);
        for (uint32_t i = 0; i < 0x800; i++) f.put(char(bus->chip()[(A0 + i) & (amiga::kChipSize - 1)]));
    }
    D0 = 0;
}

void run() {
    bus->load(0x54000, g_env.adf->read("PHIL_00"));
    intro_main();
}

}  // namespace game
