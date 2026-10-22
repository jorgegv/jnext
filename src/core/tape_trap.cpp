#include "core/tape_trap.h"
#include "core/emulator.h"

#include <utility>

namespace {

constexpr uint8_t F_C = 0x01, F_N = 0x02, F_PV = 0x04, F_H = 0x10, F_Z = 0x40, F_S = 0x80;

uint8_t parity_flag(uint8_t v) {
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (v & 1) ? 0 : F_PV;
}
// S, Z, X, Y from the result, plus parity in PV (logic and rotate instructions).
uint8_t logic_flags(uint8_t v) {
    return static_cast<uint8_t>((v & (F_S | 0x28)) | (v == 0 ? F_Z : 0) | parity_flag(v));
}
// CP 1 (X, Y come from the operand, which is 1: both clear).
uint8_t cp1_flags(uint8_t a) {
    uint8_t r = static_cast<uint8_t>(a - 1);
    uint8_t f = F_N;
    if (r & 0x80) f |= F_S;
    if (r == 0) f |= F_Z;
    if ((a & 0x0F) < 1) f |= F_H;
    if (((a ^ 1) & (a ^ r)) & 0x80) f |= F_PV;
    if (a < 1) f |= F_C;
    return f;
}

}  // namespace

namespace tape_trap {

// Mirrors the 48K ROM from LD-START to the exit: ROM 0x05A9-0x05E2 with the
// edge-timing routine replaced by "the next byte arrives". Every byte is
// 16 edges, so the edge-tracking register C is the same at each byte
// boundary (C0).
void rom_ld_bytes(Emulator& emu, const uint8_t* blk, size_t len) {
    auto regs = emu.cpu().get_registers();

    uint8_t a  = static_cast<uint8_t>(regs.AF >> 8);
    uint8_t f  = static_cast<uint8_t>(regs.AF);
    uint8_t a2 = static_cast<uint8_t>(regs.AF2 >> 8);
    uint8_t f2 = static_cast<uint8_t>(regs.AF2);
    uint8_t b  = static_cast<uint8_t>(regs.BC >> 8);
    uint8_t c  = 0x21;                       // C0: the ROM's edge-tracking value at a byte boundary,
                                             // as measured against jnext's real-time player
    uint8_t h  = 0, l = 1;
    uint8_t d  = static_cast<uint8_t>(regs.DE >> 8);
    uint8_t e  = static_cast<uint8_t>(regs.DE);
    uint16_t ix = regs.IX;
    size_t idx = 0;

    // 0x05CA: read one byte into L. False on the ROM's edge timeout (0x05CD
    // RET NC): the INC B in LD-EDGE-1 wrapped, A = 0, Z and H set.
    auto read_byte = [&]() -> bool {
        l = 1;
        if (idx >= len) { a = 0; f = F_Z | F_H; b = 0; return false; }
        l = blk[idx++];
        b = 0xB0;
        return true;
    };

    auto parity_step = [&]() {                                       // 0x05D8-0x05DC
        a = static_cast<uint8_t>(h ^ l); f = logic_flags(a); h = a;
        a = d; a |= e; f = logic_flags(a);
    };

    b = 0xB0;
    bool ok = read_byte();                                           // flag byte
    if (ok) {
        parity_step();
        while (a != 0) {                                             // JR NZ,05A9
            std::swap(a, a2); std::swap(f, f2);                      // EX AF,AF'
            if (!(f & F_Z)) {                                        // 0x05B3: flag byte
                uint8_t r = static_cast<uint8_t>((c << 1) | (f & F_C));      // RL C
                f = logic_flags(r) | (c >> 7);
                c = r;
                a ^= l; f = logic_flags(a);                          // XOR L
                if (a != 0) { ok = false; break; }                   // RET NZ
                a = c;                                               // LD A,C; RRA
                uint8_t cout = a & 1;
                a = static_cast<uint8_t>(a >> 1);                    // carry in is 0 (XOR)
                f = static_cast<uint8_t>((f & (F_S | F_Z | F_PV)) | (a & 0x28) | cout);
                c = a;                                               // INC DE; DEC DE
            } else {
                if (f & F_C) {                                       // LOAD
                    emu.mmu().write(ix, l);
                } else {                                             // VERIFY
                    a = emu.mmu().read(ix); a ^= l; f = logic_flags(a);
                    if (a != 0) { ok = false; break; }               // RET NZ
                }
                ix = static_cast<uint16_t>(ix + 1);
                uint16_t de = static_cast<uint16_t>(((d << 8) | e) - 1);
                d = static_cast<uint8_t>(de >> 8); e = static_cast<uint8_t>(de);
            }
            std::swap(a, a2); std::swap(f, f2);                      // 0x05C5 EX AF,AF'
            if (!read_byte()) { ok = false; break; }                 // 0x05CD RET NC
            parity_step();
        }
        if (ok) { a = h; f = cp1_flags(a); }                         // 0x05DF LD A,H; CP 1
    }

    regs.AF  = static_cast<uint16_t>((a << 8) | f);
    regs.AF2 = static_cast<uint16_t>((a2 << 8) | f2);
    regs.BC  = static_cast<uint16_t>((b << 8) | c);
    regs.DE  = static_cast<uint16_t>((d << 8) | e);
    regs.HL  = static_cast<uint16_t>((h << 8) | l);
    regs.IX  = ix;
    regs.PC  = LD_BYTES_RET_ADDR;
    emu.cpu().set_registers(regs);
}

}  // namespace tape_trap
