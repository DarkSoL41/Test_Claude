#include "refemu.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>

extern "C" {
#include "../third_party/musashi/m68k.h"
}

static RefEmu* g_ref = nullptr;

extern "C" {
static inline void logHw(unsigned a, int n) {
    if (g_ref->pcTrace && (a & 0xFFFFFF) >= 0x80000) std::fprintf(g_ref->pcTrace, "R%d %06X line %d\n", n, a & 0xFFFFFF, g_ref->hw.beamLine());
}
unsigned int m68k_read_memory_8(unsigned int a) { logHw(a, 1); return g_ref->hw.rd8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { logHw(a, 2); return g_ref->hw.rd16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { logHw(a, 4); return g_ref->hw.rd32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { g_ref->checkWatch(a, v & 0xFF, 1); g_ref->hw.wr8(a, uint8_t(v)); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { g_ref->checkWatch(a, v & 0xFFFF, 2); g_ref->hw.wr16(a, uint16_t(v)); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { g_ref->checkWatch(a, v, 4); g_ref->hw.wr32(a, v); }
unsigned int m68k_read_disassembler_8(unsigned int a) { return g_ref->hw.chip()[a & 0x7FFFF]; }
unsigned int m68k_read_disassembler_16(unsigned int a) { return g_ref->hw.chipW(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return (g_ref->hw.chipW(a) << 16) | g_ref->hw.chipW(a + 2); }
}
void ref_instruction_hook(unsigned int pc) { g_ref->hook(pc); }

RefEmu::RefEmu(const amiga::GameFiles& files) : paula(hw), files_(files) {
    g_ref = this;
    hw.setHost(this);
    hw.setPaula(&paula);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
}

uint32_t RefEmu::pc() const { return m68k_get_reg(nullptr, M68K_REG_PC); }

void RefEmu::bootIntro() {
    hw.load(0x54000, files_.read("PHIL_00"));
    m68k_pulse_reset();
    m68k_set_reg(M68K_REG_SR, 0x2000);
    m68k_set_reg(M68K_REG_A7, 0x80000);
    m68k_set_reg(M68K_REG_SP, 0x80000);
    m68k_set_reg(M68K_REG_PC, 0x54000);
}

void RefEmu::bootMain() {
    hw.load(0x7E00, files_.read("PHIL_01"));
    m68k_pulse_reset();
    m68k_set_reg(M68K_REG_SR, 0x2000);
    m68k_set_reg(M68K_REG_A7, 0x80000);
    m68k_set_reg(M68K_REG_SP, 0x80000);
    m68k_set_reg(M68K_REG_PC, 0x7E00);
}

void RefEmu::onFrame() {
    frameDone_ = true;
    m68k_end_timeslice();
}

void RefEmu::onInterrupt(int level) {
    // the new level is sampled by the CPU before the next instruction
    m68k_set_irq(unsigned(level));
    if (level) m68k_end_timeslice();
}

bool RefEmu::runFrame() {
    frameDone_ = false;
    const uint64_t startFrame = hw.frameCount();
    uint64_t guard = 0;
    while (!frameDone_) {
        m68k_set_irq(hw.pendingInterruptLevel());
        irqDirty_ = false;
        m68k_execute(100000);
        if (++guard > 20000) {
            std::fprintf(stderr, "ref: no frame boundary reached, pc=%06X\n", pc());
            return false;
        }
    }
    if (hw.frameCount() != startFrame + 1) {
        std::fprintf(stderr, "ref: %llu frame boundaries in one instruction, pc=%06X\n",
                     (unsigned long long)(hw.frameCount() - startFrame), pc());
        return false;
    }
    return true;
}

void RefEmu::doRts() {
    uint32_t sp = m68k_get_reg(nullptr, M68K_REG_A7);
    uint32_t ret = hw.rd32(sp);
    m68k_set_reg(M68K_REG_A7, sp + 4);
    m68k_set_reg(M68K_REG_PC, ret);
    // Musashi fetches the next opcode from the new PC without calling the
    // instruction hook again: account for the instruction at the return address here
    hook(ret);
}

static std::string fileForCode(uint32_t d1) {
    d1 &= 0xFFFF;
    if (d1 == 0x002) return "PHIL_00";
    if (d1 == 0x082) return "PHIL_01";
    if (d1 == 0x12C) return "PHIL_02";
    if (d1 == 0x0FA) return "PHIL_03";
    if (d1 >= 0x226) {
        unsigned n = ((d1 - 0x226) >> 1) + 0x10;
        char buf[16];
        std::snprintf(buf, sizeof buf, "PHIL_%02X", n & 0xFF);
        return buf;
    }
    return "";
}

// Native replacement of the (cracked) OFS file loader: d1 = file code, a0 = destination.
void RefEmu::loadFileCall(bool) {
    uint32_t d1 = m68k_get_reg(nullptr, M68K_REG_D1);
    uint32_t a0 = m68k_get_reg(nullptr, M68K_REG_A0);
    std::string name = fileForCode(d1);
    std::vector<uint8_t> data;
    if (name == "PHIL_03" && !savePath.empty()) {
        std::ifstream f(savePath, std::ios::binary);
        if (f) data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    if (data.empty()) data = files_.read(name);
    hw.load(a0, data);
    m68k_set_reg(M68K_REG_D0, 0);
    uint32_t sr = m68k_get_reg(nullptr, M68K_REG_SR);
    m68k_set_reg(M68K_REG_SR, (sr & ~0x1F) | 0x04);  // Z set
    doRts();
}

void RefEmu::checkWatch(uint32_t a, uint32_t v, int n) {
    a &= 0xFFFFFF;
    if (!(a + uint32_t(n) > watchLo && a < watchHi)) return;
    std::fprintf(stderr, "ref : frame %llu line %d  pc %06X  write.%d $%06X = %X\n", (unsigned long long)hw.frameCount(),
                 hw.beamLine(), m68k_get_reg(nullptr, M68K_REG_PPC), n, a, v);
    std::fprintf(stderr, "      trail:");
    for (uint32_t i = 16; i > 0; i--) std::fprintf(stderr, " %06X", ring[(ringPos - i) & 63]);
    std::fprintf(stderr, "\n");
}

void RefEmu::hook(uint32_t pc) {
    instrCount_++;
    if (!reportPcs.empty() && reportPcs.count(pc))
        std::fprintf(stderr, "ref: frame %llu reached $%06X\n", (unsigned long long)hw.frameCount(), pc);
    ring[ringPos++ & 63] = pc;
    if (pcTrace) std::fprintf(pcTrace, "%06X\n", pc);
    executed[pc & 0x7FFFF] = 1;
    switch (pc) {
    // PHIL_00 (intro) disk routines
    case 0x54264: loadFileCall(true); break;
    case 0x541AE: case 0x54212: doRts(); break;
    // PHIL_01 disk routines
    case 0x10404: loadFileCall(false); break;
    case 0x1035E: case 0x103C2: case 0x10A56: doRts(); break;
    // crack save routine (copied by the loader to $7000): write the hiscore file
    case 0x7000: {
        uint32_t a0 = m68k_get_reg(nullptr, M68K_REG_A0);
        if (!savePath.empty()) {
            std::ofstream f(savePath, std::ios::binary);
            for (uint32_t i = 0; i < 0x800; i++) f.put(char(hw.chip()[(a0 + i) & 0x7FFFF]));
        }
        m68k_set_reg(M68K_REG_D0, 0);
        doRts();
        break;
    }
    default: break;
    }
}
