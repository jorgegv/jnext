#include "z80n_ext.h"
#include "z80_cpu.h"

// Flag bit positions for TEST_N
static constexpr uint8_t FLAG_C = 0x01;
static constexpr uint8_t FLAG_N = 0x02;
static constexpr uint8_t FLAG_P = 0x04;
static constexpr uint8_t FLAG_X = 0x08;  // bit 3 (undocumented)
static constexpr uint8_t FLAG_H = 0x10;
static constexpr uint8_t FLAG_Y = 0x20;  // bit 5 (undocumented)
static constexpr uint8_t FLAG_Z = 0x40;
static constexpr uint8_t FLAG_S = 0x80;

// Even parity: returns true when number of 1-bits is even
static inline bool parity_even(uint8_t v) {
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1) == 0;
}

// Pass-3 fix: VHDL t80n.vhd:1277-1285 — block-transfer flag update
// applied when I_BT='1'. LDI/LDIR/LDIX/LDIRX/LDDX/LDDRX/LDIRSCALE all
// set I_BT=1 at MCycle 3 with ALU_Op=ADD and BusA=A, BusB=(HL), so
// ALU_Q = A + bytetemp. The VHDL sets:
//   F.X = ALU_Q[3], F.Y = ALU_Q[1], F.H = 0, F.N = 0
//   F.P = (BC != 0 after dec)
// S, Z, C are preserved.
//
// Spec reference (https://wiki.specnext.dev/Extended_Z80_instruction_set):
//   LDIX  flags affected: N, H, P/V, X, Y
//   LDDX  flags affected: N, H, P/V, X, Y
//   LDIRX flags affected: N, H, P/V, X, Y
//   LDDRX flags affected: N, H, P/V, X, Y
//
// Mirrors FUSE LDI/LDIR pattern (third_party/fuse-z80/z80_ed.c:280-291),
// adapted for the LDIX-family bytetemp+A composition.
static inline uint8_t ldi_family_flags(uint8_t bytetemp, uint8_t A,
                                       uint16_t BC_post_dec, uint8_t f_in) {
    const uint8_t lookup = static_cast<uint8_t>(bytetemp + A);
    uint8_t f = f_in & (FLAG_C | FLAG_Z | FLAG_S);   // preserved
    if (BC_post_dec) f |= FLAG_P;                     // V/P = (BC != 0)
    if (lookup & 0x08) f |= FLAG_X;                   // X = ALU_Q[3]
    if (lookup & 0x02) f |= FLAG_Y;                   // Y = ALU_Q[1]
    // H = 0, N = 0 (left clear)
    return f;
}

// Z80N T-state counts — per Spectrum Next official spec (next.specnext.dev) /
// VHDL t80n_mcode.vhd MCycles + per-cycle TStates overrides.
//
// IMPORTANT (Task 2, 2026-05-09): the C++ Z80N path is invoked from
// Z80Cpu::execute() AFTER `mem.read(PC)` + `mem.read(PC+1)` raw reads of the
// ED + ext bytes. Those raw reads do NOT add T-states (they bypass FUSE
// callbacks). The returned T-state count from execute_z80n() must therefore
// include the FULL instruction time, which always includes BOTH M1 fetches
// (ED prefix + ext byte = 4+4 = 8 T-states baseline). Pre-fix returns omitted
// the ED prefix M1 (4 T-states), under-counting every Z80N opcode and
// drifting IM2/vsync/contention timing for any code that invokes Z80N
// extensively (the NextZXOS supervisor invokes NEXTREG, MUL, ADD HL/DE/BC,A
// constantly).
int execute_z80n(uint8_t opcode, Z80Cpu& cpu) {
    switch (static_cast<Z80NOpcode>(opcode)) {

        case Z80NOpcode::SWAPNIB: {
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            a = ((a & 0x0F) << 4) | ((a >> 4) & 0x0F);
            regs.AF = ((uint16_t)a << 8) | (regs.AF & 0xFF);
            cpu.set_registers(regs);
            return 8;   // M1+M1
        }

        case Z80NOpcode::MIRROR_A: {
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            uint8_t r = 0;
            for (int i = 0; i < 8; i++) {
                r = (r << 1) | (a & 1);
                a >>= 1;
            }
            regs.AF = ((uint16_t)r << 8) | (regs.AF & 0xFF);
            cpu.set_registers(regs);
            return 8;   // M1+M1
        }

        case Z80NOpcode::TEST_N: {
            auto regs = cpu.get_registers();
            uint8_t nn = cpu.memory().read(regs.PC);
            regs.PC = (regs.PC + 1) & 0xFFFF;
            uint8_t a = regs.AF >> 8;
            uint8_t temp = a & nn;
            uint8_t f = FLAG_H;  // H always set
            if (temp & 0x80) f |= FLAG_S;
            if (temp == 0)   f |= FLAG_Z;
            if (parity_even(temp)) f |= FLAG_P;
            if (temp & 0x08) f |= FLAG_X;  // undocumented bit 3
            if (temp & 0x20) f |= FLAG_Y;  // undocumented bit 5
            // N=0, C=0 (already 0)
            regs.AF = ((uint16_t)a << 8) | f;
            // Pass-3 fix: Q tracks last F write (FUSE convention, used by
            // SCF/CCF for undocumented X/Y flag composition). VHDL t80n
            // doesn't model Q, but the FUSE Z80 core consults Q on the very
            // next SCF/CCF; without this update, those instructions read a
            // stale pre-Z80N Q and emit wrong X/Y flags.
            regs.Q = f;
            cpu.set_registers(regs);
            return 11;  // M1+M1+R(+1 internal) per spec
        }

        case Z80NOpcode::BSLA_DE_B: {
            auto regs = cpu.get_registers();
            uint8_t shift = (regs.BC >> 8) & 0x1F;
            regs.DE = (regs.DE << shift) & 0xFFFF;
            cpu.set_registers(regs);
            return 8;   // M1+M1
        }

        case Z80NOpcode::BSRA_DE_B: {
            auto regs = cpu.get_registers();
            uint8_t shift = (regs.BC >> 8) & 0x1F;
            int16_t de_signed = static_cast<int16_t>(regs.DE);
            regs.DE = static_cast<uint16_t>(de_signed >> shift);
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::BSRL_DE_B: {
            auto regs = cpu.get_registers();
            uint8_t shift = (regs.BC >> 8) & 0x1F;
            regs.DE = regs.DE >> shift;
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::BSRF_DE_B: {
            // VHDL: 17-bit signed right shift, bit 16 = IR[0] = 1 (fills with 1s)
            auto regs = cpu.get_registers();
            uint8_t shift = (regs.BC >> 8) & 0x1F;
            int32_t val = (1 << 16) | regs.DE;  // bit 16 = 1
            val = static_cast<int32_t>(val << 15) >> 15;  // sign-extend bit 16
            val >>= shift;
            regs.DE = static_cast<uint16_t>(val & 0xFFFF);
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::BRLC_DE_B: {
            auto regs = cpu.get_registers();
            uint8_t rot = (regs.BC >> 8) & 0x1F;  // VHDL: B[4:0], 5-bit mask
            if (rot != 0) {
                rot &= 0x0F;  // rotate_left on 16-bit wraps mod 16
                regs.DE = ((regs.DE << rot) | (regs.DE >> (16 - rot))) & 0xFFFF;
            }
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::MUL_DE: {
            auto regs = cpu.get_registers();
            uint8_t d = regs.DE >> 8;
            uint8_t e = regs.DE & 0xFF;
            regs.DE = (uint16_t)d * (uint16_t)e;
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::ADD_HL_A: {
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            uint32_t result = (uint32_t)regs.HL + (uint32_t)a;
            regs.HL = result & 0xFFFF;
            // Only C flag changes; preserve all other flags
            uint8_t f = regs.AF & 0xFF;
            f = (f & ~FLAG_C) | ((result >> 16) & 1);
            regs.AF = ((uint16_t)a << 8) | f;
            regs.Q = f;  // Pass-3 fix: track last F write (see TEST_N comment)
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::ADD_DE_A: {
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            uint32_t result = (uint32_t)regs.DE + (uint32_t)a;
            regs.DE = result & 0xFFFF;
            uint8_t f = regs.AF & 0xFF;
            f = (f & ~FLAG_C) | ((result >> 16) & 1);
            regs.AF = ((uint16_t)a << 8) | f;
            regs.Q = f;  // Pass-3 fix: track last F write (see TEST_N comment)
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::ADD_BC_A: {
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            uint32_t result = (uint32_t)regs.BC + (uint32_t)a;
            regs.BC = result & 0xFFFF;
            uint8_t f = regs.AF & 0xFF;
            f = (f & ~FLAG_C) | ((result >> 16) & 1);
            regs.AF = ((uint16_t)a << 8) | f;
            regs.Q = f;  // Pass-3 fix: track last F write (see TEST_N comment)
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::ADD_HL_NN: {
            // ED 34 ll hh — ADD HL, nn (no flags affected)
            auto regs = cpu.get_registers();
            uint8_t lo = cpu.memory().read(regs.PC);
            uint8_t hi = cpu.memory().read((regs.PC + 1) & 0xFFFF);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            regs.HL = (regs.HL + ((uint16_t)hi << 8 | lo)) & 0xFFFF;
            cpu.set_registers(regs);
            return 16;  // M1+M1+R+R+(2 internal); spec=16
        }

        case Z80NOpcode::ADD_DE_NN: {
            // ED 35 ll hh — ADD DE, nn (no flags affected)
            auto regs = cpu.get_registers();
            uint8_t lo = cpu.memory().read(regs.PC);
            uint8_t hi = cpu.memory().read((regs.PC + 1) & 0xFFFF);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            regs.DE = (regs.DE + ((uint16_t)hi << 8 | lo)) & 0xFFFF;
            cpu.set_registers(regs);
            return 16;
        }

        case Z80NOpcode::ADD_BC_NN: {
            // ED 36 ll hh — ADD BC, nn (no flags affected)
            auto regs = cpu.get_registers();
            uint8_t lo = cpu.memory().read(regs.PC);
            uint8_t hi = cpu.memory().read((regs.PC + 1) & 0xFFFF);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            regs.BC = (regs.BC + ((uint16_t)hi << 8 | lo)) & 0xFFFF;
            cpu.set_registers(regs);
            return 16;
        }

        case Z80NOpcode::PUSH_NN: {
            // ED 8A hh ll — push 16-bit immediate value onto stack.
            // Instruction stream: high byte first, then low byte (big-endian).
            // PC already points past ED 8A; read 2 operand bytes.
            auto regs = cpu.get_registers();
            uint8_t hh = cpu.memory().read(regs.PC);
            uint8_t ll = cpu.memory().read(regs.PC + 1);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            regs.SP = (regs.SP - 2) & 0xFFFF;
            cpu.memory().write(regs.SP + 1, hh);
            cpu.memory().write(regs.SP, ll);
            cpu.set_registers(regs);
            return 23;
        }

        case Z80NOpcode::OUTINB: {
            auto regs = cpu.get_registers();
            uint8_t temp = cpu.memory().read(regs.HL);
            uint16_t port = regs.BC;
            cpu.io().out(port, temp);
            regs.HL = (regs.HL + 1) & 0xFFFF;
            // B is NOT decremented (unlike OUTI)
            cpu.set_registers(regs);
            return 16;  // M1+M1+R+IO ≈ 16 per spec
        }

        case Z80NOpcode::NEXTREG_NN: {
            auto regs = cpu.get_registers();
            uint8_t reg = cpu.memory().read(regs.PC);
            uint8_t val = cpu.memory().read((regs.PC + 1) & 0xFFFF);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            cpu.set_registers(regs);
            cpu.io().out(0x243B, reg);
            cpu.io().out(0x253B, val);
            return 20;  // VHDL MCycles=5, M1+M1+R+R+(internal)=20 per spec
        }

        case Z80NOpcode::NEXTREG_A: {
            auto regs = cpu.get_registers();
            uint8_t reg = cpu.memory().read(regs.PC);
            regs.PC = (regs.PC + 1) & 0xFFFF;
            uint8_t a = regs.AF >> 8;
            cpu.set_registers(regs);
            cpu.io().out(0x243B, reg);
            cpu.io().out(0x253B, a);
            return 17;  // VHDL MCycles=4, M1+M1+R+(internal)=17 per spec
        }

        case Z80NOpcode::PIXELDN: {
            auto regs = cpu.get_registers();
            uint8_t H = regs.HL >> 8;
            uint8_t L = regs.HL & 0xFF;
            // ULA screen layout: H = 010BBLLL, L = CCCCCRRR
            // PIXELDN increments the row counter encoded across H and L
            uint8_t inner = ((H & 0x07) + 1);
            if ((inner & 0x08) == 0) {
                H = (H & 0xF8) | (inner & 0x07);
            } else {
                H = H & 0xF8;
                uint8_t mid = ((L >> 5) + 1) & 0x07;
                L = (L & 0x1F) | (mid << 5);
                if (mid == 0) {
                    H = H + 0x08;
                }
            }
            regs.HL = ((uint16_t)H << 8) | L;
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::PIXELAD: {
            auto regs = cpu.get_registers();
            uint8_t D = regs.DE >> 8;    // Y coord (row 0-191)
            uint8_t E = regs.DE & 0xFF;  // X coord (pixel col 0-255)
            // VHDL: H = "010" & D[7:6] & D[2:0], L = D[5:3] & E[7:3]
            uint8_t H = 0x40 | ((D & 0xC0) >> 3) | (D & 0x07);
            uint8_t L = ((D & 0x38) << 2) | (E >> 3);
            regs.HL = ((uint16_t)H << 8) | L;
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::SETAE: {
            auto regs = cpu.get_registers();
            uint8_t e = regs.DE & 0xFF;
            uint8_t bit = e & 0x07;
            uint8_t a = 0x80 >> bit;
            regs.AF = ((uint16_t)a << 8) | (regs.AF & 0xFF);
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::JP_C: {
            // ED 98 — JP (C): read byte from port BC, set PC[13:6] = byte, PC[5:0] = 0
            // PC[15:14] are preserved (VHDL only writes bits 13:0)
            auto regs = cpu.get_registers();
            uint8_t val = cpu.io().in(regs.BC);
            regs.PC = (regs.PC & 0xC000) | ((uint16_t)val << 6);
            cpu.set_registers(regs);
            return 13;  // M1+M1+IO per spec
        }

        case Z80NOpcode::LDIX: {
            // ED A4 — single iteration block transfer with transparency.
            // Pass-3 fix: VHDL I_BT block-transfer flag update — see
            // ldi_family_flags() above and t80n.vhd:1277-1285.
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = cpu.memory().read(regs.HL);
            if (temp != A) {
                cpu.memory().write(regs.DE, temp);
            }
            regs.DE = (regs.DE + 1) & 0xFFFF;
            regs.HL = (regs.HL + 1) & 0xFFFF;
            regs.BC = (regs.BC - 1) & 0xFFFF;
            uint8_t f = ldi_family_flags(temp, A, regs.BC,
                                         static_cast<uint8_t>(regs.AF & 0xFF));
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q  = f;
            cpu.set_registers(regs);
            return 16;  // M1+M1+R+W per spec
        }

        case Z80NOpcode::LDWS: {
            // ED A5 — LDWS: copy (HL) to (DE), then increment only L and D
            // This is a single-byte-at-a-time copy with stride 256 on dest, 1 on source low byte
            // Flags: same as INC D (S, Z, H, P/V, N=0 from the D increment)
            auto regs = cpu.get_registers();
            uint8_t temp = cpu.memory().read(regs.HL);
            cpu.memory().write(regs.DE, temp);
            // Increment L only (wrap within low byte)
            uint8_t L = (regs.HL & 0xFF) + 1;
            regs.HL = (regs.HL & 0xFF00) | L;
            // Increment D only (wrap within high byte of DE)
            uint8_t D = (regs.DE >> 8) + 1;
            // Set flags from D increment (same as INC r)
            uint8_t f = regs.AF & 0xFF;
            f &= FLAG_C;  // preserve carry only
            if (D == 0)    f |= FLAG_Z;
            if (D & 0x80)  f |= FLAG_S;
            if (D & 0x08)  f |= FLAG_X;  // undoc bit 3
            if (D & 0x20)  f |= FLAG_Y;  // undoc bit 5
            if (D == 0x80) f |= FLAG_P;  // overflow: 0x7F+1=0x80
            if ((D & 0x0F) == 0) f |= FLAG_H;  // half carry
            // N=0 (already cleared)
            regs.DE = ((uint16_t)D << 8) | (regs.DE & 0xFF);
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q = f;  // Pass-3 fix: track last F write (see TEST_N comment)
            cpu.set_registers(regs);
            return 14;
        }

        case Z80NOpcode::LDDX: {
            // ED AC — single iteration, HL decrements.
            // Pass-3 fix: VHDL I_BT block-transfer flag update.
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = cpu.memory().read(regs.HL);
            if (temp != A) {
                cpu.memory().write(regs.DE, temp);
            }
            regs.DE = (regs.DE + 1) & 0xFFFF;  // DE still increments
            regs.HL = (regs.HL - 1) & 0xFFFF;  // HL decrements
            regs.BC = (regs.BC - 1) & 0xFFFF;
            uint8_t f = ldi_family_flags(temp, A, regs.BC,
                                         static_cast<uint8_t>(regs.AF & 0xFF));
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q  = f;
            cpu.set_registers(regs);
            return 16;
        }

        case Z80NOpcode::LDIRX: {
            // ED B4 — repeating block transfer with transparency, HL/DE increment.
            // G89: per-iteration step (LDIR-style). Each call performs ONE
            // iteration and rewinds PC by 2 if BC!=0 so the next
            // Z80Cpu::execute() re-fetches ED B4 — and gets the chance to take
            // any pending INT first (z80_cpu.cpp samples INT at the top of
            // execute()). Mirrors VHDL t80n_mcode.vhd:2095-2138 where
            // MCycles="100" runs once per iteration and the t80n core handles
            // repetition by rewinding PC. BC=0 on entry => 65536 iterations:
            // post-decrement gives BC=0xFFFF, loop continues, BC underflows
            // back to 0 after exactly 65536 calls.
            //
            // T-states: 21 per iteration when BC!=0 after decrement (still
            // repeating), 16 on the terminal iteration. Spec values per
            // Spectrum Next reference; matches the standard LDIR shape.
            //
            // Pass-3 fix: VHDL I_BT block-transfer flag update.
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = cpu.memory().read(regs.HL);
            if (temp != A) {
                cpu.memory().write(regs.DE, temp);
            }
            regs.DE = (regs.DE + 1) & 0xFFFF;
            regs.HL = (regs.HL + 1) & 0xFFFF;
            regs.BC = (regs.BC - 1) & 0xFFFF;
            uint8_t f = ldi_family_flags(temp, A, regs.BC,
                                         static_cast<uint8_t>(regs.AF & 0xFF));
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q  = f;
            int t = 16;
            if (regs.BC != 0) {
                regs.PC = (regs.PC - 2) & 0xFFFF;
                t = 21;
            }
            cpu.set_registers(regs);
            return t;
        }

        case Z80NOpcode::LDDRX: {
            // ED BC — repeating block transfer with transparency, HL decrements,
            // DE increments. G89 inter-iteration INT-sample shape.
            // Mirrors VHDL t80n_mcode.vhd:2230-2256 (LDDX/LDDRX share the same
            // MCycles="100" decoder block; per VHDL DE INCREMENTS while HL
            // DECREMENTS — confirmed by IncDec_16 codes "0101" (inc DE) and
            // "1110" (dec HL) at lines 2240+2244).
            //
            // Pass-3 fix: VHDL I_BT block-transfer flag update.
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = cpu.memory().read(regs.HL);
            if (temp != A) {
                cpu.memory().write(regs.DE, temp);
            }
            regs.DE = (regs.DE + 1) & 0xFFFF;  // DE still increments
            regs.HL = (regs.HL - 1) & 0xFFFF;  // HL decrements
            regs.BC = (regs.BC - 1) & 0xFFFF;
            uint8_t f = ldi_family_flags(temp, A, regs.BC,
                                         static_cast<uint8_t>(regs.AF & 0xFF));
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q  = f;
            int t = 16;
            if (regs.BC != 0) {
                regs.PC = (regs.PC - 2) & 0xFFFF;
                t = 21;
            }
            cpu.set_registers(regs);
            return t;
        }

        case Z80NOpcode::LDPIRX: {
            // ED B7 — pattern fill with transparency, repeats until BC=0.
            // G89 inter-iteration INT-sample shape. Mirrors VHDL
            // t80n_mcode.vhd:1953-1991 (LDPIRX MCycles="100" with PC rewind).
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            // Source: upper 13 bits of HL | lower 3 bits of E (HL stays fixed)
            uint16_t src_addr = (regs.HL & 0xFFF8) | (regs.DE & 0x0007);
            uint8_t temp = cpu.memory().read(src_addr);
            if (temp != A) {
                cpu.memory().write(regs.DE, temp);
            }
            regs.DE = (regs.DE + 1) & 0xFFFF;
            // HL does NOT change (pattern base)
            regs.BC = (regs.BC - 1) & 0xFFFF;
            int t = 16;
            if (regs.BC != 0) {
                regs.PC = (regs.PC - 2) & 0xFFFF;
                t = 21;
            }
            cpu.set_registers(regs);
            return t;
        }

        case Z80NOpcode::LDIRSCALE: {
            // ED B6 — scaled block load with transparency. G89 inter-iteration
            // INT-sample shape. Mirrors VHDL t80n_mcode.vhd:2188-2226 (same
            // MCycles="100" + PC-rewind repeat shape as LDIRX).
            // VHDL note: the BC'/DE' alternate register additions are commented
            // out in the FPGA source. The mcode uses standard HL++, DE++.
            //
            // Pass-3 fix: VHDL I_BT block-transfer flag update — LDIRSCALE's
            // mcode (line 2208-2209) explicitly sets Set_BusA_To=A,
            // ALU_Op=ADD, so the flag composition matches LDI/LDIRX.
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = cpu.memory().read(regs.HL);
            if (temp != A) {
                cpu.memory().write(regs.DE, temp);
            }
            regs.DE = (regs.DE + 1) & 0xFFFF;
            regs.HL = (regs.HL + 1) & 0xFFFF;
            regs.BC = (regs.BC - 1) & 0xFFFF;
            uint8_t f = ldi_family_flags(temp, A, regs.BC,
                                         static_cast<uint8_t>(regs.AF & 0xFF));
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q  = f;
            int t = 16;
            if (regs.BC != 0) {
                regs.PC = (regs.PC - 2) & 0xFFFF;
                t = 21;
            }
            cpu.set_registers(regs);
            return t;
        }

        case Z80NOpcode::LOOP: {
            // Not implemented in FPGA. Treat as NOP-equivalent (M1+M1).
            return 8;
        }
        default: return -1;
    }
}
