#include "z80n_ext.h"
#include "z80_cpu.h"

extern "C" {
#include "fuse_z80_shim.h"

// Pass-7 — VHDL-faithful "no-MREQ tail cycle" contention for LDIX-family
// internal idle. These are implemented in z80_cpu.cpp (extern "C") and
// already used by the FUSE opcode TU via the CORETEST function-override
// path (see G141 in z80_cpu.cpp). The Z80N path needs the same gate so
// that LDIX/LDDX/LDIRX/LDDRX/LDPIRX/LDIRSCALE internal idle on contended
// destination pages stretches identically to FUSE's LDI/LDIR/LDD/LDDR.
//
// VHDL oracle: t80n_mcode.vhd MCycle 4 of LDIX-family asserts NoRead=1
// with TStates="101" (=5T) on Set_Addr_To=aDE, i.e. DE on the address
// bus while the M-cycle idles. Per zxula.vhd:582-600 the contention gate
// fires on (hc_adj × vc × contention_en) regardless of MREQ, so each of
// those tail T-states emits the standard contention stretch when DE is
// on a contended page during the active raster window.
//
// FUSE LDI (z80_ed.c case 0xa0:285) confirms the per-T-state model:
//     contend_write_no_mreq(DE, 1); contend_write_no_mreq(DE, 1);
// (i.e. two single-cycle no-MREQ contention calls on DE — one per
// internal idle T-state). FUSE LDIR (case 0xb0:429-431) adds five more
// of the same for the BC≠0 re-decode pause, giving the full 7T tail.
//
// Pre-fix: jnext used `tstates += 2` (or `+= 7`) raw, bypassing the
// contention gate entirely. Demos doing LDIRX into Layer-2 / Bank-5
// during the active raster lost the per-cycle stretch — for a 6 144-byte
// blit that's ≈ 36 K T-states drift, a full frame.
extern void contend_read_no_mreq( libspectrum_word address, libspectrum_dword time );
extern void contend_write_no_mreq( libspectrum_word address, libspectrum_dword time );
}

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
//
// Pass-6 fix (2026-05-09): Z80N OPERAND READS / DATA WRITES were previously
// done via `cpu.memory().read()` / `cpu.memory().write()` — the raw
// MemoryInterface — which:
//   (a) bypasses ContentionModel::contention_tick() so the per-cycle
//       contention stretch on contended pages during the active raster
//       window is NEVER applied;
//   (b) does NOT advance the FUSE `tstates` counter incrementally during
//       the operand reads — only the static post-instruction `tstates +=`
//       in the wrapper compensates, which loses sub-instruction-precision
//       (hc, vc) for derived contention.
// Pass-5 promoted M1 contention to class-(a) and fixed it via the wrapper's
// contend_read pair. Pass-6 quantifies operand-access drift:
//   - LDIRX over screen RAM (6144 bytes contended, 6 T avg stretch per pair):
//     ≈ 36 K T-states drift per full-screen blit — a full FRAME's worth of
//     timing. Demos using LDIRX for fade transitions sit on this.
//   - NextZXOS supervisor invokes NEXTREG_NN/NEXTREG_A constantly; even
//     non-contended-page reads still under-count the +3 T per-byte advance
//     until the static post-instruction add fires. The /INT pulse-window
//     check in Z80Cpu::execute() (`tstates - int_requested_at_`) sees a
//     stale tstates during long Z80N bursts, so a pulse can outlive its
//     32/36-cycle window.
// → Reclassified to class-(a). Refactor: route operand/data accesses
// through fuse_z80_readbyte / fuse_z80_writebyte (contention + 3 T per
// byte), real port I/O through fuse_z80_readport / fuse_z80_writeport
// (1 T pre-IORQ + contention + 3 T post-IORQ = 4 T per cycle).
//
// Each case is responsible for advancing FUSE `tstates` by the FULL
// post-M1 portion of the instruction (the wrapper's contend_read pair
// already added the 8 T M1 prefix + ext-byte baseline + their contention
// stretch). The case adds:
//   - data-access T-states via fuse_z80_readbyte / fuse_z80_writebyte /
//     fuse_z80_readport / fuse_z80_writeport (each advances tstates +
//     contention internally);
//   - whatever "internal idle" cycles remain to reach the spec total
//     via raw `tstates += internal_idle;` at the case tail.
// The wrapper in z80_cpu.cpp no longer adds (t - 8); execute_z80n's
// returned T-state count is purely informational.
//
// NextReg writes via cpu.io().out(0x243B/0x253B, …) STAY on the raw
// IoInterface path — real hardware bypasses the I/O bus entirely for
// NEXTREG (the VHDL drives the NextReg fabric directly via Z80N_data_o
// strobes — see t80n_mcode.vhd:1672-1707). Charging fuse_z80_writeport's
// 4 T per "port write" would over-count by 8 T per NEXTREG_NN. The 6 T
// internal idle on NEXTREG_NN (and NEXTREG_A) covers the actual NextReg
// fabric write timing in hardware.
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
            // Pass-6 fix: operand read via fuse_z80_readbyte (contention + 3T).
            uint8_t nn = fuse_z80_readbyte(regs.PC);
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
            // Spec total = 11T = 8 (M1) + 3 (operand). No internal idle.
            return 11;
        }

        case Z80NOpcode::BSLA_DE_B: {
            // V17-Z80N-01: shift count is B[4:0] = 0..31. VHDL t80n.vhd:987-993
            // uses `shift_left(unsigned(16-bit), shift)` on a 16-bit value;
            // for shift >= 16 the result is 0 (numeric_std-defined). Pre-fix
            // jnext used `(uint16_t)x << shift` which promotes to int (signed)
            // — this is C++ undefined behaviour for shift >= 16 (signed overflow
            // on platforms where int is 32-bit; technically also UB if the
            // shift count >= width_of_promoted_type). On x86 GCC this happens
            // to give the right truncated answer after `& 0xFFFF`, but it's
            // strictly UB and other compilers/optimisations could break it.
            // Use 32-bit unsigned arithmetic, no shift past width.
            auto regs = cpu.get_registers();
            uint8_t shift = (regs.BC >> 8) & 0x1F;
            uint32_t v = static_cast<uint32_t>(regs.DE) << shift;
            regs.DE = static_cast<uint16_t>(v & 0xFFFF);
            cpu.set_registers(regs);
            return 8;   // M1+M1
        }

        case Z80NOpcode::BSRA_DE_B: {
            // V17-CPU-NIT-04 (reviewer NIT) — BSRA strict-UB-free shift handling.
            // VHDL t80n.vhd:1006-1014 — for BSRA the 17-bit pre-shift value has
            // bit 16 = bit 15 (sign-extended), then signed(17-bit) >> shift_count
            // with shift_count = B[4:0] = 0..31. For shift >= 16 the result is
            // sign-fill (0xFFFF if DE bit 15 set; 0x0000 else).
            //
            // Pre-fix: `int16_t >> shift` with shift up to 31 on a negative
            // value is C++17 implementation-defined (signed >> negative is
            // arithmetic on x86 GCC by accident, but strictly impl-defined
            // pre-C++20). Same family as V17-Z80N-01a/b (BSLA/BSRF). Rewrite
            // with explicit branching + unsigned arithmetic, no UB possible.
            auto regs = cpu.get_registers();
            uint8_t shift = (regs.BC >> 8) & 0x1F;
            uint16_t result;
            const bool sign_bit = (regs.DE & 0x8000) != 0;
            if (shift == 0) {
                result = regs.DE;
            } else if (shift >= 16) {
                result = sign_bit ? 0xFFFFu : 0x0000u;
            } else if (sign_bit) {
                // Arithmetic right shift via unsigned: shift right + OR in
                // a mask of 1-bits in the top `shift` positions.
                const uint16_t mask_hi = static_cast<uint16_t>(
                    (0xFFFFu << (16 - shift)) & 0xFFFFu);
                result = static_cast<uint16_t>((regs.DE >> shift) | mask_hi);
            } else {
                // Non-negative: simple unsigned shift right.
                result = static_cast<uint16_t>(regs.DE >> shift);
            }
            regs.DE = result;
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
            // V17-Z80N-01: VHDL t80n.vhd:1006-1014 — 17-bit signed right shift,
            // bit 16 = IR[0] = 1 (fills with 1s). For shift >= 16 the result
            // is all 1s (= 0xFFFF). Pre-fix jnext used a 32-bit construction
            // that's strictly UB (signed left shift of a value with high bit
            // potentially set) but happened to produce correct results on x86.
            // We rewrite using explicit branching for clarity + portability.
            auto regs = cpu.get_registers();
            uint8_t shift = (regs.BC >> 8) & 0x1F;
            uint16_t result;
            if (shift == 0) {
                result = regs.DE;
            } else if (shift >= 16) {
                result = 0xFFFF;
            } else {
                // Build 17-bit value: bit 16 = 1, bits 15:0 = DE.
                // Shift right, then take bits 15:0 — equivalent to ORing
                // a mask of 1-bits into the high `shift` bits.
                const uint16_t mask_hi = static_cast<uint16_t>(
                    (0xFFFFu << (16 - shift)) & 0xFFFFu);
                result = static_cast<uint16_t>((regs.DE >> shift) | mask_hi);
            }
            regs.DE = result;
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
            // Pass-10 fix: VHDL t80n.vhd:778-783 — F.C is FORCED TO 0 by the
            // hardware, NOT the actual carry. The VHDL writes
            //     reg_temp_t(15 downto 0) := std_logic_vector(unsigned(HL) + unsigned(ACC));
            // (16-bit truncated addition; numeric_std `+` returns MAX(L,R) bits
            // = 16 bits when L=16 and R=8). Then reads
            //     F(Flag_C) <= reg_temp_t(16);
            // Bit 16 was never written by the addition (truncated) and is
            // pre-zeroed at TState=3 (line 698: `reg_temp_t := (others=>'0');`).
            // Net effect: F.C is unconditionally cleared after every ADD HL,A.
            // The Z80N spec wiki says "no flags affected" but the VHDL oracle
            // overrides — real Spectrum Next FPGA hardware clears F.C here.
            // S, Z, X, Y, H, P, N are all preserved (no other F write fires).
            // Pass-3..Pass-9 incorrectly computed the actual carry.
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            regs.HL = (regs.HL + a) & 0xFFFF;
            uint8_t f = static_cast<uint8_t>(regs.AF & 0xFF);
            f &= ~FLAG_C;   // VHDL: F.C <= reg_temp_t(16) = 0
            regs.AF = (static_cast<uint16_t>(a) << 8) | f;
            regs.Q = f;  // Q tracks last F write (FUSE convention for SCF/CCF)
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::ADD_DE_A: {
            // Pass-10 fix: see ADD_HL_A — VHDL clears F.C unconditionally.
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            regs.DE = (regs.DE + a) & 0xFFFF;
            uint8_t f = static_cast<uint8_t>(regs.AF & 0xFF);
            f &= ~FLAG_C;   // VHDL: F.C <= reg_temp_t(16) = 0
            regs.AF = (static_cast<uint16_t>(a) << 8) | f;
            regs.Q = f;
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::ADD_BC_A: {
            // Pass-10 fix: see ADD_HL_A — VHDL clears F.C unconditionally.
            auto regs = cpu.get_registers();
            uint8_t a = regs.AF >> 8;
            regs.BC = (regs.BC + a) & 0xFFFF;
            uint8_t f = static_cast<uint8_t>(regs.AF & 0xFF);
            f &= ~FLAG_C;   // VHDL: F.C <= reg_temp_t(16) = 0
            regs.AF = (static_cast<uint16_t>(a) << 8) | f;
            regs.Q = f;
            cpu.set_registers(regs);
            return 8;
        }

        case Z80NOpcode::ADD_HL_NN: {
            // ED 34 ll hh — ADD HL, nn (no flags affected)
            // Pass-4 fix: VHDL t80n_mcode.vhd:1872-1878 sets LDZ at MCycle 2
            // and LDW at MCycle 3, loading the operand into WZ/MEMPTR low
            // then high. End-of-instruction WZ = nn. The next BIT (HL) /
            // BIT (IX+d) / IN A,(n) etc. that consults WZ for X/Y flag
            // composition would see stale MEMPTR otherwise.
            // Pass-6 fix: operand reads via fuse_z80_readbyte (contention + 3T).
            auto regs = cpu.get_registers();
            uint8_t lo = fuse_z80_readbyte(regs.PC);
            uint8_t hi = fuse_z80_readbyte((regs.PC + 1) & 0xFFFF);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            uint16_t nn = (static_cast<uint16_t>(hi) << 8) | lo;
            regs.HL = (regs.HL + nn) & 0xFFFF;
            regs.MEMPTR = nn;
            cpu.set_registers(regs);
            // Spec = 16T = 8 (M1) + 6 (2× operand) + 2 (internal).
            tstates += 2;
            return 16;
        }

        case Z80NOpcode::ADD_DE_NN: {
            // ED 35 ll hh — ADD DE, nn (no flags affected). See ADD_HL_NN
            // for WZ/MEMPTR rationale (VHDL t80n_mcode.vhd:1872-1878).
            // Pass-6 fix: operand reads via fuse_z80_readbyte.
            auto regs = cpu.get_registers();
            uint8_t lo = fuse_z80_readbyte(regs.PC);
            uint8_t hi = fuse_z80_readbyte((regs.PC + 1) & 0xFFFF);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            uint16_t nn = (static_cast<uint16_t>(hi) << 8) | lo;
            regs.DE = (regs.DE + nn) & 0xFFFF;
            regs.MEMPTR = nn;
            cpu.set_registers(regs);
            tstates += 2;
            return 16;
        }

        case Z80NOpcode::ADD_BC_NN: {
            // ED 36 ll hh — ADD BC, nn (no flags affected). See ADD_HL_NN
            // for WZ/MEMPTR rationale (VHDL t80n_mcode.vhd:1872-1878).
            // Pass-6 fix: operand reads via fuse_z80_readbyte.
            auto regs = cpu.get_registers();
            uint8_t lo = fuse_z80_readbyte(regs.PC);
            uint8_t hi = fuse_z80_readbyte((regs.PC + 1) & 0xFFFF);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            uint16_t nn = (static_cast<uint16_t>(hi) << 8) | lo;
            regs.BC = (regs.BC + nn) & 0xFFFF;
            regs.MEMPTR = nn;
            cpu.set_registers(regs);
            tstates += 2;
            return 16;
        }

        case Z80NOpcode::PUSH_NN: {
            // ED 8A hh ll — push 16-bit immediate value onto stack.
            // Instruction stream: high byte first, then low byte (big-endian).
            // PC already points past ED 8A; read 2 operand bytes.
            // Pass-6 fix: operand reads + stack writes via fuse_z80_*byte
            // (contention + 3T per byte). Spec = 23T = 8 (M1) + 6 (2× operand
            // read) + 6 (2× stack write) + 3 (internal idle).
            // Pass-8 fix: WZ end-state. VHDL t80n_mcode.vhd:1928 sets
            // LDZ='1' at MCycle 1 (capturing hh into TmpAddr(7..0) per
            // t80n.vhd:1181-1182), and t80n_mcode.vhd:1938 sets LDZ='1'
            // again at MCycle 3 (capturing ll, overwriting). LDW is
            // never asserted, so TmpAddr(15..8) (= WZ-hi) is unchanged.
            // End-of-instruction: WZ-lo = ll, WZ-hi = (prior WZ-hi).
            // The next IN A,(n) / BIT (HL) / IN r,(C) etc. that consults
            // WZ for X/Y composition would otherwise see stale WZ-lo.
            auto regs = cpu.get_registers();
            uint8_t hh = fuse_z80_readbyte(regs.PC);
            uint8_t ll = fuse_z80_readbyte((regs.PC + 1) & 0xFFFF);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            regs.SP = (regs.SP - 2) & 0xFFFF;
            // VHDL-faithful WZ update: only the low byte is written
            // (LDZ-only path). WZ-hi is preserved from the prior MEMPTR.
            regs.MEMPTR = static_cast<uint16_t>(
                (regs.MEMPTR & 0xFF00) | static_cast<uint16_t>(ll));
            // Sync FUSE z80 state so that the global s_mem write callback uses
            // up-to-date register snapshots when contention triggers a side
            // effect (none of jnext's contention models look at z80 regs, but
            // sync keeps callback semantics symmetric with the FUSE path).
            cpu.set_registers(regs);
            fuse_z80_writebyte((regs.SP + 1) & 0xFFFF, hh);
            fuse_z80_writebyte(regs.SP, ll);
            tstates += 3;
            return 23;
        }

        case Z80NOpcode::OUTINB: {
            // ED 90 — OUT (BC),(HL); HL++. Real I/O cycle (VHDL line 2552
            // sets IORQ=1 + Write=1 at MCycle 3). Spec = 16T = 8 (M1) +
            // 3 (mem read HL) + 4 (port write IORQ) + 1 (internal).
            // Pass-6 fix: data read via fuse_z80_readbyte, port write via
            // fuse_z80_writeport — both add contention.
            //
            // V12-CPU-NIT-02 (Pass-12 reviewer NIT) — VHDL fidelity for the
            // 1T extended-M1 tail: per t80n_mcode.vhd:2528-2530, OUTINB's
            // MCycle 1 sets `TStates <= "101"` (=5T), i.e. the inner-M1 of
            // the 0x90 byte is extended by 1T. This matches the standard
            // OUTI extended-M1 cycle: per FUSE z80_ed.c:334
            //     contend_read_no_mreq(IR, 1);
            // is emitted BEFORE the readbyte(HL) — IR is on the address bus
            // during the M1 refresh + extended T-state. Per zxula.vhd:582-600
            // the contention gate fires on (hc_adj × vc × contention_en)
            // regardless of MREQ, so the 1T extended-M1 must traverse the
            // gate to emit the per-cycle stretch on contended IR addresses.
            //
            // Pre-fix: raw `tstates += 1` advanced the FUSE clock but
            // bypassed the contention gate entirely. Functionally equivalent
            // when contention is OFF (ZX48K static fixture), but at runtime
            // on real ZX Next hardware the gate's stretch contribution is
            // missed when IR sits in a contended bank.
            auto regs = cpu.get_registers();
            // Emit the 1T extended-M1 BEFORE the operand read, mirroring
            // FUSE OUTI / VHDL MCycle 1. Address bus = IR (refresh pair).
            uint16_t ir = (static_cast<uint16_t>(regs.I) << 8)
                        | static_cast<uint16_t>(regs.R);
            contend_read_no_mreq(ir, 1);
            uint8_t temp = fuse_z80_readbyte(regs.HL);
            uint16_t port = regs.BC;
            fuse_z80_writeport(port, temp);
            regs.HL = (regs.HL + 1) & 0xFFFF;
            // B is NOT decremented (unlike OUTI)
            cpu.set_registers(regs);
            return 16;
        }

        case Z80NOpcode::NEXTREG_NN: {
            // ED 91 rr vv — NextReg write. NEXTREG bypasses the I/O bus on
            // real hardware (VHDL t80n_mcode.vhd:1672-1707 — Z80N_data_o
            // strobes drive the NextReg fabric directly, no IORQ on the
            // external pin). cpu.io().out() to 0x243B/0x253B is jnext's
            // internal shortcut: it routes through the IoMux dispatch but
            // does NOT trigger fuse_z80_writeport's 4 T per port write
            // (which would over-count by 8 T per NEXTREG_NN).
            // Spec = 20T = 8 (M1) + 6 (2× operand read) + 6 (internal —
            // covers the two NextReg fabric mcycles).
            // Pass-6 fix: operand reads via fuse_z80_readbyte for contention.
            auto regs = cpu.get_registers();
            uint8_t reg = fuse_z80_readbyte(regs.PC);
            uint8_t val = fuse_z80_readbyte((regs.PC + 1) & 0xFFFF);
            regs.PC = (regs.PC + 2) & 0xFFFF;
            cpu.set_registers(regs);
            cpu.io().out(0x243B, reg);
            cpu.io().out(0x253B, val);
            tstates += 6;
            return 20;
        }

        case Z80NOpcode::NEXTREG_A: {
            // ED 92 rr — NextReg write from A. See NEXTREG_NN comment for
            // the I/O-bus bypass rationale.
            // Spec = 17T = 8 (M1) + 3 (operand read) + 6 (internal).
            // Pass-6 fix: operand read via fuse_z80_readbyte for contention.
            auto regs = cpu.get_registers();
            uint8_t reg = fuse_z80_readbyte(regs.PC);
            regs.PC = (regs.PC + 1) & 0xFFFF;
            uint8_t a = regs.AF >> 8;
            cpu.set_registers(regs);
            cpu.io().out(0x243B, reg);
            cpu.io().out(0x253B, a);
            tstates += 6;
            return 17;
        }

        case Z80NOpcode::PIXELDN: {
            // ULA screen layout: H = pPbbCCC, L = RRRrrrrr  (where p = H[7],
            // P = H[6], b = H[4:3] = band, C = H[2:0] = cell-row, R = L[7:5]
            // = row-in-band, r = L[4:0] = column).  Per VHDL t80n.vhd:900-921,
            // PIXELDN performs ONE 8-bit add `(b & R & C) + 1` and assigns
            // the result back to those positions:
            //   new_H = H[7:5] (preserved) | new_b (= bits 7:6 of result)
            //                              | new_C (= bits 2:0 of result)
            //   new_L = new_R (= bits 5:3 of result) | L[4:0] (preserved)
            //
            // V11-CPU-01 fix (Pass-11): the prior algorithm did
            //     if (mid == 0) H = H + 0x08;
            // which propagated the carry from b[1] (bit 4 of H) into b[2]
            // (bit 5 of H) when b was already 11 — corrupting the preserved
            // H[7:5] field.  VHDL truncates the 8-bit add at bit 7, so a
            // full overflow (b=11, R=111, C=111) wraps to b=00, R=000,
            // C=000 with H[7:5] untouched.  The new code does the
            // composite increment in one 8-bit add and re-extracts the
            // fields, exactly matching the VHDL.
            //
            // Discriminative case: HL=0x5FE0 (b=11, R=111, C=111).
            //   Pre-fix:  HL → 0x6000   (jnext propagated +0x08 into H[5])
            //   Post-fix: HL → 0x4000   (VHDL: H[7:5]=010 preserved)
            auto regs = cpu.get_registers();
            const uint8_t H = static_cast<uint8_t>(regs.HL >> 8);
            const uint8_t L = static_cast<uint8_t>(regs.HL & 0xFF);
            // Compose the 8-bit value `b & R & C` per VHDL line 904-908.
            const uint8_t composite = static_cast<uint8_t>(
                ((H & 0x18) << 3)        // b   → bits 7:6 of composite
                | ((L & 0xE0) >> 2)      // R   → bits 5:3 of composite
                | (H & 0x07));           // C   → bits 2:0 of composite
            // 8-bit add with truncation (carry from bit 7 is lost — VHDL
            // assigns to reg_temp_t(7:0), so the high carry never reaches
            // H[7:5]).
            const uint8_t inc = static_cast<uint8_t>(composite + 1);
            const uint8_t new_b = static_cast<uint8_t>((inc >> 6) & 0x03);
            const uint8_t new_R = static_cast<uint8_t>((inc >> 3) & 0x07);
            const uint8_t new_C = static_cast<uint8_t>(inc & 0x07);
            const uint8_t new_H = static_cast<uint8_t>(
                (H & 0xE0)              // H[7:5] preserved
                | (new_b << 3)          // b → H[4:3]
                | new_C);               // C → H[2:0]
            const uint8_t new_L = static_cast<uint8_t>(
                (L & 0x1F)              // L[4:0] preserved
                | (new_R << 5));        // R → L[7:5]
            regs.HL = static_cast<uint16_t>(
                (static_cast<uint16_t>(new_H) << 8) | new_L);
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
            // VHDL t80n_mcode.vhd:1837-1848: MCycles="010" (2 M-cycles total),
            // TStates="100" (=4T per MCycle). MCycle 1 = inner-M1 of 0x98
            // (4T), MCycle 2 emits IORQ=1 + Set_Addr_To<=aBC = port read (4T).
            // The ED prefix M1 itself adds 4T BEFORE this case fires.
            // Total = 4 (ED M1) + 4 (98 M1) + 4 (port read) = 12T.
            // Pass-6 fix: port read via fuse_z80_readport for contention.
            // Pass-8 fix: corrected total from 13T to 12T per VHDL — the +1
            // idle assumed in pass-6's comment was a Z80N spec-wiki figure
            // that the VHDL does NOT emit. CLAUDE.md mandates VHDL as the
            // authoritative oracle. tests.expected for ed98_* already lists
            // 12T (z80n_test.cpp doesn't compare tstates today, but the
            // expected column is now consistent with both VHDL and emulator).
            auto regs = cpu.get_registers();
            uint8_t val = fuse_z80_readport(regs.BC);
            regs.PC = (regs.PC & 0xC000) | ((uint16_t)val << 6);
            cpu.set_registers(regs);
            return 12;
        }

        case Z80NOpcode::LDIX: {
            // ED A4 — single iteration block transfer with transparency.
            // Pass-3 fix: VHDL I_BT block-transfer flag update — see
            // ldi_family_flags() above and t80n.vhd:1277-1285.
            // Pass-6 fix: source read + dest write via fuse_z80_readbyte /
            // fuse_z80_writebyte (contention + 3T per access).
            // Pass-7 fix: internal idle 2T on DE address via
            // contend_write_no_mreq, mirroring FUSE LDI (z80_ed.c:285).
            // Spec = 16T = 8 (M1) + 3 (read HL) + 3 (write DE) + 2 (internal).
            //
            // VHDL transparency: the write fires only when temp != A. When the
            // byte matches the transparency value the write IS suppressed in
            // hardware — but the bus cycle for the write phase still consumes
            // its T-states. Pass-9 fix: route the suppressed-write 3 T through
            // the contention gate (contend_write_no_mreq) so demos blitting
            // into contended pages keep the per-cycle stretch even on
            // transparency-matching bytes. Per VHDL zxula.vhd:582-600 the
            // contention window gates only on (hc, vc, contention_en) and
            // the registered MREQ — wr_n is irrelevant — so the suppressed
            // cycle hits the same gate as a real write would.
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = fuse_z80_readbyte(regs.HL);
            if (temp != A) {
                fuse_z80_writebyte(regs.DE, temp);
            } else {
                // Transparency-suppressed write: 3 T-states with contention
                // gate active on DE. Matches actual-write timing exactly.
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
            }
            const uint16_t de_pre_inc = regs.DE;
            regs.DE = (regs.DE + 1) & 0xFFFF;
            regs.HL = (regs.HL + 1) & 0xFFFF;
            regs.BC = (regs.BC - 1) & 0xFFFF;
            uint8_t f = ldi_family_flags(temp, A, regs.BC,
                                         static_cast<uint8_t>(regs.AF & 0xFF));
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q  = f;
            // Pass-9 fix: VHDL t80n.vhd:1361-1367 — block transfers latch
            // IncDecZ from the BC dec result. Visible to subsequent LDWS
            // and other I_BT instructions via the I_BT P override.
            regs.IncDecZ = (regs.BC != 0) ? 1u : 0u;
            cpu.set_registers(regs);
            // Pass-7: per-cycle no-MREQ contention on DE (pre-increment) for
            // the 2T internal idle. Mirrors FUSE LDI (z80_ed.c:285).
            contend_write_no_mreq(de_pre_inc, 1);
            contend_write_no_mreq(de_pre_inc, 1);
            return 16;
        }

        case Z80NOpcode::LDWS: {
            // ED A5 — LDWS: copy (HL) to (DE), then increment L and increment D.
            // Spec = 14T = 8 (M1) + 3 (read HL) + 3 (write DE) + 0 (no internal).
            //
            // Pass-8 fix: flag composition follows VHDL I_BT block-transfer
            // pattern (t80n_mcode.vhd:2169-2181 sets I_BT='1' at MCycle 3 with
            // ALU_Op="0000" ADD on D + 1; t80n.vhd:1255-1289 then runs the ALU
            // ADD-flag block AND the I_BT post-override block on the same cycle).
            //
            // ALU result ALU_Q = D + 1 (post-increment). Flag composition:
            //   - ALU ADD path:        S = ALU_Q[7], Z = (ALU_Q==0),
            //                          P = (overflow: D==0x7F → set),
            //                          H = (carry from bit 3: (D&0x0F)==0x0F),
            //                          N = 0 (ADD doesn't set N), C unchanged.
            //   - I_BT post-override (t80n.vhd:1281-1285):
            //                          F.X = ALU_Q[3], F.Y = ALU_Q[1],
            //                          F.H = 0, F.N = 0.
            //   - I_BT/I_BC P override (t80n.vhd:1286-1289):
            //                          F.P = IncDecZ.
            //
            // IncDecZ for LDWS: VHDL latches IncDecZ from the most recent
            // 16-bit inc/dec (IncDec_16(2:0)="100" path) and from DJNZ
            // (F_Out(Flag_Z) of B-1). LDWS does NOT set IncDec_16 anywhere,
            // so IncDecZ retains its prior value. Pass-9 fix: jnext now
            // tracks IncDecZ as a 1-bit shadow in Z80Registers (updated
            // by Z80Cpu::execute() on DJNZ + ED block transfers, and by
            // the Z80N block-transfer cases below). LDWS reads it directly
            // — VHDL-faithful, no longer an approximation.
            //
            // Pre-fix (P6 spec-INC-D path):
            //   F.Y = D[5]            ← bug: VHDL emits ALU_Q[1]
            //   F.H = (D&0x0F)==0     ← bug: VHDL forces H=0 (I_BT override)
            //   F.P = D==0x80         ← bug: VHDL emits IncDecZ (prior latch)
            // P7 fix:
            //   F.Y = D[1]            ← matches VHDL I_BT override
            //   F.H = 0               ← matches VHDL I_BT override
            //   F.P = prior(F.P)      ← approximation; Pass-9 promotes to
            //                           the real IncDecZ shadow.
            auto regs = cpu.get_registers();
            uint8_t temp = fuse_z80_readbyte(regs.HL);
            fuse_z80_writebyte(regs.DE, temp);
            // Increment L only (wrap within low byte)
            uint8_t L = (regs.HL & 0xFF) + 1;
            regs.HL = (regs.HL & 0xFF00) | L;
            // Increment D only (wrap within high byte of DE)
            const uint8_t D_pre = static_cast<uint8_t>(regs.DE >> 8);
            const uint8_t D = static_cast<uint8_t>(D_pre + 1);  // ALU_Q
            const uint8_t f_in = static_cast<uint8_t>(regs.AF & 0xFF);
            uint8_t f = f_in & FLAG_C;        // preserve carry only
            // ALU ADD-flag block (t80n.vhd:1163-1198 via Save_ALU_r):
            if (D & 0x80)              f |= FLAG_S;     // S = ALU_Q[7]
            if (D == 0)                f |= FLAG_Z;     // Z
            // I_BT block (t80n.vhd:1281-1285) overrides X/Y/H/N:
            if (D & 0x08)              f |= FLAG_X;     // X = ALU_Q[3]
            if (D & 0x02)              f |= FLAG_Y;     // Y = ALU_Q[1] (VHDL!)
            // F.H = 0 (forced by I_BT override) — already cleared.
            // F.N = 0 (forced by I_BT override) — already cleared.
            // P = IncDecZ (latch from prior 16-bit inc/dec or DJNZ).
            // Pass-9: read the real shadow from Z80Registers. (Pre-fix:
            // approximated by `f_in & FLAG_P`.)
            if (regs.IncDecZ) f |= FLAG_P;
            regs.DE = static_cast<uint16_t>(static_cast<uint16_t>(D) << 8) | (regs.DE & 0xFF);
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q = f;
            cpu.set_registers(regs);
            return 14;
        }

        case Z80NOpcode::LDDX: {
            // ED AC — single iteration, HL decrements.
            // Pass-3 fix: VHDL I_BT block-transfer flag update.
            // Pass-6 fix: source read + dest write via fuse_z80_*byte.
            // Pass-7 fix: internal idle 2T on DE address via
            // contend_write_no_mreq (see LDIX comment).
            // Spec = 16T = 8 (M1) + 3 (read HL) + 3 (write DE) + 2 (internal).
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = fuse_z80_readbyte(regs.HL);
            if (temp != A) {
                fuse_z80_writebyte(regs.DE, temp);
            } else {
                // Pass-9 fix: route suppressed write through contention gate.
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
            }
            const uint16_t de_pre_inc = regs.DE;
            regs.DE = (regs.DE + 1) & 0xFFFF;  // DE still increments
            regs.HL = (regs.HL - 1) & 0xFFFF;  // HL decrements
            regs.BC = (regs.BC - 1) & 0xFFFF;
            uint8_t f = ldi_family_flags(temp, A, regs.BC,
                                         static_cast<uint8_t>(regs.AF & 0xFF));
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q  = f;
            // Pass-9 fix: VHDL t80n.vhd:1361-1367 IncDecZ latch on BC dec.
            regs.IncDecZ = (regs.BC != 0) ? 1u : 0u;
            cpu.set_registers(regs);
            contend_write_no_mreq(de_pre_inc, 1);
            contend_write_no_mreq(de_pre_inc, 1);
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
            // Pass-6 fix: source read + dest write via fuse_z80_*byte.
            // Pass-7 fix: 2T post-write idle + 5T re-decode pause both on DE
            // address via contend_write_no_mreq, mirroring FUSE LDIR
            // (z80_ed.c:422 + 429-431).
            // Term spec = 16T = 8 (M1) + 6 (read+write) + 2 (internal).
            // Cont spec = 21T = 8 (M1) + 6 (read+write) + 7 (internal — the
            // standard LDIR 5-extra-cycle re-decode pause).
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = fuse_z80_readbyte(regs.HL);
            if (temp != A) {
                fuse_z80_writebyte(regs.DE, temp);
            } else {
                // Pass-9 fix: route suppressed write through contention gate.
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
            }
            const uint16_t de_pre_inc = regs.DE;
            regs.DE = (regs.DE + 1) & 0xFFFF;
            regs.HL = (regs.HL + 1) & 0xFFFF;
            regs.BC = (regs.BC - 1) & 0xFFFF;
            uint8_t f = ldi_family_flags(temp, A, regs.BC,
                                         static_cast<uint8_t>(regs.AF & 0xFF));
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q  = f;
            // Pass-9 fix: VHDL t80n.vhd:1361-1367 IncDecZ latch on BC dec.
            regs.IncDecZ = (regs.BC != 0) ? 1u : 0u;
            int t = 16;
            int internal_idle = 2;
            if (regs.BC != 0) {
                regs.PC = (regs.PC - 2) & 0xFFFF;
                t = 21;
                internal_idle = 7;
            }
            cpu.set_registers(regs);
            // Pass-7: per-cycle no-MREQ contention on DE (pre-increment) for
            // both the 2T post-write idle and the 5T re-decode pause.
            for (int i = 0; i < internal_idle; ++i) {
                contend_write_no_mreq(de_pre_inc, 1);
            }
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
            // Pass-6 fix: source read + dest write via fuse_z80_*byte.
            // Pass-7 fix: internal idle on DE via contend_write_no_mreq
            // (see LDIRX comment).
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = fuse_z80_readbyte(regs.HL);
            if (temp != A) {
                fuse_z80_writebyte(regs.DE, temp);
            } else {
                // Pass-9 fix: route suppressed write through contention gate.
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
            }
            const uint16_t de_pre_inc = regs.DE;
            regs.DE = (regs.DE + 1) & 0xFFFF;  // DE still increments
            regs.HL = (regs.HL - 1) & 0xFFFF;  // HL decrements
            regs.BC = (regs.BC - 1) & 0xFFFF;
            uint8_t f = ldi_family_flags(temp, A, regs.BC,
                                         static_cast<uint8_t>(regs.AF & 0xFF));
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q  = f;
            // Pass-9 fix: VHDL t80n.vhd:1361-1367 IncDecZ latch on BC dec.
            regs.IncDecZ = (regs.BC != 0) ? 1u : 0u;
            int t = 16;
            int internal_idle = 2;
            if (regs.BC != 0) {
                regs.PC = (regs.PC - 2) & 0xFFFF;
                t = 21;
                internal_idle = 7;
            }
            cpu.set_registers(regs);
            for (int i = 0; i < internal_idle; ++i) {
                contend_write_no_mreq(de_pre_inc, 1);
            }
            return t;
        }

        case Z80NOpcode::LDPIRX: {
            // ED B7 — pattern fill with transparency, repeats until BC=0.
            // G89 inter-iteration INT-sample shape. Mirrors VHDL
            // t80n_mcode.vhd:1953-1991 (LDPIRX MCycles="100" with PC rewind).
            // Pass-6 fix: pattern read + dest write via fuse_z80_*byte.
            // Pass-7 fix: internal idle on DE via contend_write_no_mreq
            // (see LDIRX comment).
            // Pass-10 fix: VHDL I_BT block-transfer flag composition. The
            // LDPIRX mcode (t80n_mcode.vhd:1962-1991) sets I_BT='1' at MCycle 3
            // — same as LDIX/LDDX/LDIRX/LDDRX/LDIRSCALE — but does NOT override
            // Set_BusA_To/ALU_Op. So defaults at MCycle 2 carry over via the
            // registered BusA/BusB/ALU_Op_r at MCycle 3 TState=1:
            //   BusA = RegBusA(15:8) of {Alternate, "00"} = B (post-dec)
            //   BusB = DI_Reg = bytetemp (data byte read at MCycle 2)
            //   ALU_Op_r = "0" & IR(5:3) = "0" & "110" = "0110" (OR)
            // → ALU_Q = B | bytetemp (where B is the POST-DECREMENT high byte
            //   of BC, since IncDec_16="1100" at MCycle 1 decremented BC).
            //
            // Per t80n.vhd:1277-1289 with I_BT='1':
            //   F.X = ALU_Q[3] = (B | bytetemp)[3]
            //   F.Y = ALU_Q[1] = (B | bytetemp)[1]
            //   F.H = 0 (forced)
            //   F.N = 0 (forced)
            //   F.P = IncDecZ
            //   S, Z, C: preserved (no Save_ALU at MCycle 3, so F-update from
            //   ALU at line 1224 doesn't fire; the I_BT block at 1277 only
            //   touches X/Y/H/N).
            //
            // Pass-9 and earlier: F was completely UNTOUCHED, leaving stale
            // F.H/N/P/X/Y from the prior instruction — observable when LDPIRX
            // followed by JP M / JR PE / etc. takes wrong branch. Class-(a).
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            // Source: upper 13 bits of HL | lower 3 bits of E (HL stays fixed)
            uint16_t src_addr = (regs.HL & 0xFFF8) | (regs.DE & 0x0007);
            uint8_t temp = fuse_z80_readbyte(src_addr);
            if (temp != A) {
                fuse_z80_writebyte(regs.DE, temp);
            } else {
                // Pass-9 fix: route suppressed write through contention gate.
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
            }
            const uint16_t de_pre_inc = regs.DE;
            regs.DE = (regs.DE + 1) & 0xFFFF;
            // HL does NOT change (pattern base)
            regs.BC = (regs.BC - 1) & 0xFFFF;
            // Pass-9 fix: VHDL t80n.vhd:1361-1367 IncDecZ latch on BC dec.
            regs.IncDecZ = (regs.BC != 0) ? 1u : 0u;
            // Pass-10 fix: I_BT flag composition (see comment block above).
            // ALU_Q = B (post-dec high byte) OR bytetemp.
            const uint8_t b_post_dec = static_cast<uint8_t>(regs.BC >> 8);
            const uint8_t alu_q = static_cast<uint8_t>(b_post_dec | temp);
            uint8_t f = static_cast<uint8_t>(regs.AF & 0xFF);
            f &= (FLAG_S | FLAG_Z | FLAG_C);   // preserve S, Z, C
            if (alu_q & 0x08)   f |= FLAG_X;   // X = ALU_Q[3]
            if (alu_q & 0x02)   f |= FLAG_Y;   // Y = ALU_Q[1]
            // H = 0, N = 0 (left clear)
            if (regs.IncDecZ)   f |= FLAG_P;   // P = IncDecZ (I_BC/I_BT override)
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q = f;                        // track last F write
            // V18R-CPU-NIT-01 fix (Pass-18 reviewer NIT). VHDL
            // t80n_mcode.vhd:1967 sets `LDZ <= '1'` at MCycle 1 of LDPIRX.
            // Per t80n.vhd:1181-1182, LDZ='1' writes `DI_Reg` (the byte
            // just fetched on the inner M1) into `TmpAddr(7 downto 0)` =
            // MEMPTR_lo. At MCycle 1 the inner-M1 fetch's DI_Reg is the
            // LDPIRX opcode byte 0xB7. WZ-hi is unchanged (LDW is never
            // asserted in this mcode). Software-visible impact is zero
            // (no public test/program reads MEMPTR after LDPIRX), but
            // this brings the C++ emulator into 1:1 alignment with the
            // VHDL oracle for completeness.
            regs.MEMPTR = static_cast<uint16_t>(
                (regs.MEMPTR & 0xFF00) | 0x00B7u);
            int t = 16;
            int internal_idle = 2;
            if (regs.BC != 0) {
                regs.PC = (regs.PC - 2) & 0xFFFF;
                t = 21;
                internal_idle = 7;
            }
            cpu.set_registers(regs);
            for (int i = 0; i < internal_idle; ++i) {
                contend_write_no_mreq(de_pre_inc, 1);
            }
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
            // Pass-6 fix: source read + dest write via fuse_z80_*byte.
            // Pass-7 fix: internal idle on DE via contend_write_no_mreq
            // (see LDIRX comment).
            auto regs = cpu.get_registers();
            uint8_t A = regs.AF >> 8;
            uint8_t temp = fuse_z80_readbyte(regs.HL);
            if (temp != A) {
                fuse_z80_writebyte(regs.DE, temp);
            } else {
                // Pass-9 fix: route suppressed write through contention gate.
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
                contend_write_no_mreq(regs.DE, 1);
            }
            const uint16_t de_pre_inc = regs.DE;
            regs.DE = (regs.DE + 1) & 0xFFFF;
            regs.HL = (regs.HL + 1) & 0xFFFF;
            regs.BC = (regs.BC - 1) & 0xFFFF;
            uint8_t f = ldi_family_flags(temp, A, regs.BC,
                                         static_cast<uint8_t>(regs.AF & 0xFF));
            regs.AF = (regs.AF & 0xFF00) | f;
            regs.Q  = f;
            // Pass-9 fix: VHDL t80n.vhd:1361-1367 IncDecZ latch on BC dec.
            regs.IncDecZ = (regs.BC != 0) ? 1u : 0u;
            int t = 16;
            int internal_idle = 2;
            if (regs.BC != 0) {
                regs.PC = (regs.PC - 2) & 0xFFFF;
                t = 21;
                internal_idle = 7;
            }
            cpu.set_registers(regs);
            for (int i = 0; i < internal_idle; ++i) {
                contend_write_no_mreq(de_pre_inc, 1);
            }
            return t;
        }

        case Z80NOpcode::LOOP: {
            // Not implemented in FPGA. Treat as NOP-equivalent (M1+M1).
            return 8;
        }
        default: return -1;
    }
}
