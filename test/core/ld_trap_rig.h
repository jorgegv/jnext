// Shared rig for the LD-BYTES fast-load trap rows (tap_loader_test,
// tzx_loader_test). A real 48K ROM (from the SD image, like every suite that
// executes ROM code) and a hand-written caller at 0x8000 that does the
// LD-BYTES prologue itself and CALLs 0x0562 (the entry a custom block loader
// uses) or 0x0556. The same case runs twice: the ROM routine reading the tape
// in real time (the oracle) and the fast-load trap.
#pragma once

#include "core/emulator.h"
#include "core/emulator_config.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace ldrig {

using Bytes = std::vector<uint8_t>;

inline std::string sd_image() {
    if (const char* env = std::getenv("JNEXT_TEST_SD_IMAGE")) return env;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.jnext/sdcard/cspect-next-1gb-fixed.img";
}

struct Case {
    MachineType mt = MachineType::ZX48K;
    bool rom1_128 = false;       // 128K: page ROM 1 (the 48K BASIC ROM) first
    bool tzx = false;
    std::string path;            // the tape file
    uint16_t entry = 0x0562;     // CALL target
    uint16_t ix = 0xA000;        // destination
    bool di_before_call = true;
    uint8_t flag = 0xFF;
    uint16_t de = 5;
    bool load = true;            // false: VERIFY
    Bytes prefill;               // written at 0xA000 before the run
    uint8_t bordcr = 0;          // 0x5C48
};

struct Result {
    bool built = false;          // emulator up, tape attached
    bool reached = false;        // PC got to the caller's JR $
    Z80Registers r{};
    Bytes mem;                   // 0xA000.. (32 bytes)
    bool at_end = false;
    size_t block = 0;            // TAP current_block()
    uint8_t border = 0;
    int frames = 0;
};

inline Bytes caller(const Case& c) {
    Bytes code;
    auto put = [&](std::initializer_list<uint8_t> b) { code.insert(code.end(), b); };
    if (c.rom1_128) put({0x3E, 0x17, 0x01, 0xFD, 0x7F, 0xED, 0x79});   // LD A,17; LD BC,7FFD; OUT (C),A
    put({0xDD, 0x21, static_cast<uint8_t>(c.ix & 0xFF), static_cast<uint8_t>(c.ix >> 8)});   // LD IX,dest
    put({0x11, static_cast<uint8_t>(c.de & 0xFF), static_cast<uint8_t>(c.de >> 8)});
    put({0x3E, c.flag});                                                // LD A,flag
    put({c.load ? uint8_t(0x37) : uint8_t(0xA7)});                      // SCF / AND A
    if (c.entry == 0x0562) put({0x14, 0x08, 0x15});                     // INC D; EX AF,AF'; DEC D
    if (c.di_before_call) put({0xF3});                                  // DI
    put({0xCD, static_cast<uint8_t>(c.entry & 0xFF), static_cast<uint8_t>(c.entry >> 8)});
    return code;
}

// fast=true: the trap; false: the ROM routine, tape in real time.
inline Result run(const Case& c, bool fast, int budget_frames) {
    Result res;
    auto emu = std::make_unique<Emulator>();
    EmulatorConfig cfg;
    cfg.type = c.mt;
    cfg.rewind_buffer_frames = 0;
    cfg.sd_card_image = sd_image();
    cfg.sd_card_readonly = true;
    if (!emu->init(cfg)) return res;
    if (!(c.tzx ? emu->load_tzx(c.path, fast) : emu->load_tap(c.path, fast))) return res;
    res.built = true;

    Bytes code = caller(c);
    const uint16_t jr = static_cast<uint16_t>(0x8000 + code.size());
    code.push_back(0x18); code.push_back(0xFE);                         // JR $
    for (size_t i = 0; i < code.size(); ++i)
        emu->mmu().write(static_cast<uint16_t>(0x8000 + i), code[i]);
    for (size_t i = 0; i < c.prefill.size(); ++i)
        emu->mmu().write(static_cast<uint16_t>(0xA000 + i), c.prefill[i]);
    emu->mmu().write(0x5C48, c.bordcr);

    auto r = emu->cpu().get_registers();
    r.PC = 0x8000; r.SP = 0x9000; r.IFF1 = r.IFF2 = 0;
    emu->cpu().set_registers(r);

    for (res.frames = 0; res.frames < budget_frames; ++res.frames) {
        emu->run_frame();
        if (emu->cpu().pc() == jr) { res.reached = true; break; }
    }
    res.r = emu->cpu().get_registers();
    for (uint16_t i = 0; i < 32; ++i) res.mem.push_back(emu->mmu().peek(static_cast<uint16_t>(0xA000 + i)));
    res.at_end = c.tzx ? emu->tzx_tape().at_end() : emu->tape().at_end();
    res.block = emu->tape().current_block();
    res.border = emu->renderer().ula().get_border();
    return res;
}

inline bool carry(const Result& x) { return (x.r.AF & 1) != 0; }

// Everything the routine leaves for its caller, except R, MEMPTR, Q and the
// edge-tracking byte C with what is copied from it (see below).
inline bool same_state(const Result& a, const Result& b, std::string* why = nullptr) {
    auto note = [&](const char* n) { if (why && why->empty()) *why = n; return false; };
    const Z80Registers& x = a.r; const Z80Registers& y = b.r;
    if (x.AF != y.AF) return note("AF");
    // C is the ROM's edge-tracking byte. After the sync its value depends on
    // the EAR phase at which the ROM caught the pilot (0x21 or 0xFE with bit 7
    // later lost, measured), which a trap with no tape playing cannot know;
    // A' (= C after the flag byte) and F' bits 3 and 5 (copied from it) follow.
    // The fast side pins C = 0x21 itself in TAPC-16. Every other bit is compared.
    if ((x.BC >> 8) != (y.BC >> 8)) return note("B");
    if (x.DE != y.DE) return note("DE");
    if (x.HL != y.HL) return note("HL");
    if (((x.AF2 ^ y.AF2) & 0x00D7) != 0) return note("F'");
    if (x.BC2 != y.BC2 || x.DE2 != y.DE2 || x.HL2 != y.HL2) return note("BC'DE'HL'");
    if (x.IX != y.IX || x.IY != y.IY) return note("IX/IY");
    if (x.SP != y.SP || x.PC != y.PC) return note("SP/PC");
    if (x.IFF1 != y.IFF1 || x.IFF2 != y.IFF2 || x.IM != y.IM) return note("IFF/IM");
    if (a.mem != b.mem) return note("memory");
    return true;
}

}  // namespace ldrig
