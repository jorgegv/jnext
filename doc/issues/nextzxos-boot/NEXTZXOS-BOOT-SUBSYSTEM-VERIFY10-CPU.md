# NEXTZXOS Boot Subsystem — Pass-10 Verification: CPU / Z80N / IM2

**Branch**: `task2/verify10-cpu-z80n-im2`  
**Worktree**: `.claude/worktrees/task2-verify10-cpu-z80n-im2`  
**Scope**: `src/cpu/z80_cpu.{cpp,h}`, `src/cpu/z80n_ext.{cpp,h}`, `src/cpu/im2.{cpp,h}`, `third_party/fuse-z80/`  
**Oracles**: FUSE Z80 1356/1356, VHDL `t80n.vhd`, `t80n_mcode.vhd`, `t80n_alu.vhd`, `im2_control.vhd`, `im2_device.vhd`, `im2_peripheral.vhd`.

## Verdict

**3 class-(a) findings resolved.** No class-(b) or class-(c) findings remain
within scope. One class-(d) architectural item carried forward (IM2-mode INT
acceptance bridge to CPU is missing — pre-existing gap, out of strict
convergence scope).

After fixes:

- FUSE Z80 oracle: **1356 / 1356 PASS / 0 FAIL / 0 SKIP**.
- ctest full suite: **37 / 37 PASS / 0 FAIL** (including `z80n_tests`
  85 / 85 and `ctc_tests` with full IM2 fabric coverage).

## Pass-10 angles walked

### Angle 1 — Z80N opcode-by-opcode end-state final table

For every Z80N opcode, the C++ end-state was cross-walked against
`t80n_mcode.vhd` MCycle decoder + `t80n.vhd` flag-update / register-update
blocks.

**Finding A1 (class-a)**: `LDPIRX` (ED B7) was missing the I_BT block-transfer
flag composition entirely. VHDL `t80n_mcode.vhd:1962-1991` sets `I_BT <= '1'`
at MCycle 3, which per `t80n.vhd:1277-1289` forces:

- F.X = ALU_Q[3]
- F.Y = ALU_Q[1]
- F.H = 0
- F.N = 0
- F.P = IncDecZ
- S, Z, C: preserved (no `Save_ALU` at MCycle 3)

Crucially, `LDPIRX` does NOT override `Set_BusA_To`/`ALU_Op` at MCycle 2 (the
explicit `Set_BusA_To <= "111"` and `ALU_Op <= "0000"` are commented out — see
mcode lines 1973-1974), so the registered BusA/BusB/ALU_Op_r at MCycle 3
TState=1 carry MCycle 2's defaults:

- BusA = RegBusA(15:8) of `{Alternate, "00"}` = B (post-decrement, since
  `IncDec_16="1100"` at MCycle 1 decremented BC).
- BusB = DI_Reg = bytetemp (the data byte read at MCycle 2).
- ALU_Op_r = `"0" & IR(5:3)` = `"0110"` = OR.

⇒ ALU_Q = B | bytetemp.

This is a deliberate divergence from the LDIX/LDDX/LDIRX/LDDRX/LDIRSCALE
family (which all explicitly set BusA=A, ALU_Op=ADD, giving ALU_Q = A +
bytetemp). The Z80N spec wiki ("flags affected: N, H, P/V, X, Y") does not
distinguish between ALU_Q sources; the VHDL is the authoritative oracle.

Pre-fix: `LDPIRX` left F unchanged (no F write, no Q update). Stale F.H/N/P/X/Y
from the prior instruction would survive through any LDPIRX, breaking
subsequent `JP M / JR PE / etc.` branch decisions. Class-(a).

**Fix**: `src/cpu/z80n_ext.cpp` LDPIRX case now applies the I_BT flag
composition with ALU_Q = (B post-dec) | bytetemp, plus `regs.Q = f` for
SCF/CCF X/Y propagation. Test fixtures `edb7_basic` / `edb7_skip` updated to
reflect VHDL-faithful flag output (was assuming "no F change").

**Finding A2 (class-a)**: `ADD HL,A` / `ADD DE,A` / `ADD BC,A` (ED 31/32/33) —
F.C should be **forced to 0**, not the actual carry. VHDL `t80n.vhd:778-783`:

```vhdl
reg_temp_t(15 downto 0) := std_logic_vector(
                  unsigned(unsigned(RegsH(...)) & unsigned(RegsL(...))) +
                  unsigned(ACC));
F(Flag_C) <= reg_temp_t(16);
```

The `+` operator from `numeric_std` returns `MAX(L'length, R'length)` bits =
16 bits when L=16 and R=8 — the carry-out (would-be bit 16) is discarded by
the truncated assignment. `reg_temp_t(16)` was pre-zeroed at line 698
(`reg_temp_t := (others=>'0');`) at TState=3, and is never touched by the
add. Net effect: `F(Flag_C) <= 0` unconditionally on every ADD HL/DE/BC,A.

The Z80N spec wiki says "no flags affected", which Pass-3..Pass-9 partially
honored by NOT touching S/Z/X/Y/H/P/N but kept computing the actual carry
into F.C. The VHDL clears F.C unconditionally. The strict convergence
criterion ("VHDL is authoritative") demands matching the VHDL behavior.

**Fix**: ADD_HL_A / ADD_DE_A / ADD_BC_A in `z80n_ext.cpp` now clear F.C
unconditionally. Test fixtures `ed31_carry` / `ed32_carry` / `ed33_carry`
updated (the names remain meaningful as "this is what happens when the actual
add carries" — the VHDL truncation is documented in the case comment).
`ed31_basic`, `ed31_no_carry`, `ed31_preserve_flags` all already expected
F.C=0, confirming the existing test suite was internally inconsistent with
the actual-carry implementation.

### Angle 2 — R-register increment final audit

Walked every Z80 + Z80N opcode. R increments per VHDL t80n.vhd:493
(`R(6 downto 0) <= R(6 downto 0) + 1;` at MCycle=1 TState=2).

- ED-prefixed (incl. Z80N): 2 M1 cycles → R+=2. Our wrapper does
  `z80.r = (z80.r + 2) & 0x7F;`. ✓
- DD/FD/CB chained prefixes: each prefix byte = 1 M1. FUSE handles via
  per-case `R++` and the `default → PC--, R-- → goto end_opcode` re-dispatch
  pattern. Verified DD DD <op>, DD ED <ext>, DD CB d <op>: R increments by N
  where N = number of prefix bytes + final M1. ✓
- INT acceptance: FUSE `fuse_z80_interrupt()` does `R++`. ✓
- NMI acceptance: FUSE `fuse_z80_nmi()` does `R++`. ✓
- HALT: PC++ inside fuse_z80_interrupt/nmi if halted. ✓

No findings in this angle.

### Angle 3 — WZ / MEMPTR final table

Walked every Z80N opcode for WZ writes:

- `ADD HL/DE/BC,nn` (ED 34/35/36): VHDL sets `LDZ` at MCycle 2 + `LDW` at
  MCycle 3 → WZ = nn. Our C++ sets `regs.MEMPTR = nn`. ✓
- `PUSH NN` (ED 8A): VHDL sets `LDZ` at MCycle 1 (captures hh into TmpAddr_lo)
  + `LDZ` again at MCycle 3 (captures ll, overwriting). LDW never set, so
  TmpAddr_hi unchanged. End: WZ_lo = ll, WZ_hi preserved. ✓ (Pass-8 fix).
- `JP (C)` (ED 98): VHDL has no LDZ/LDW. MEMPTR preserved. ✓
- `OUTINB` (ED 90): VHDL has `I_BTR <= '1'` but no LDZ/LDW. MEMPTR preserved.
  ✓ (Distinct from FUSE's `OUTI`/`OTIR` which set MEMPTR=BC+1 — but those
  decrement B, OUTINB doesn't.)
- `NEXTREG_NN` (ED 91), `NEXTREG_A` (ED 92): no LDZ/LDW. MEMPTR preserved. ✓
- `TEST n` (ED 27): no LDZ/LDW. MEMPTR preserved. ✓
- All other Z80N (single MCycle, e.g. SWAPNIB, MIRROR, MUL_DE,
  ADD HL/DE/BC,A, BSLA/BSRA/BSRL/BSRF/BRLC, PIXELDN, PIXELAD, SETAE):
  no MEMPTR write. ✓

No findings in this angle.

### Angle 4 — Q register final table

Q semantics (FUSE convention, used by SCF/CCF for X/Y composition): every
opcode sets Q=0 at start; F-writing opcodes set Q=F at end.

Every Z80N opcode walked. The wrapper in `z80_cpu.cpp` resets `z80.q = 0` at
the start of every Z80N dispatch (Pass-4 fix). Z80N opcodes that update F
must also set `regs.Q = f` at end:

- TEST_N, ADD_HL_A, ADD_DE_A, ADD_BC_A, LDIX, LDWS, LDDX, LDIRX, LDDRX,
  LDIRSCALE: all set `regs.Q = f`. ✓
- LDPIRX (post-fix from Angle 1): now sets `regs.Q = f`. ✓
- All non-F-writing Z80N opcodes: leave Q=0 (set by wrapper, never updated).
  ✓

No additional findings.

### Angle 5 — Save/load FUSE-internal state final inventory

Walked every member of the FUSE `processor z80` struct (`fuse_z80_shim.h:39-53`)
against `Z80Cpu::save_state` / `load_state`:

- AF, BC, DE, HL, AF', BC', DE', HL', IX, IY, SP, PC, I, R: all in
  `Z80Registers`, persisted. ✓
- memptr (WZ): persisted as `regs_.MEMPTR`. ✓ (Pass-3 fix).
- iff2_read: persisted directly into `z80.iff2_read`. ✓ (Pass-4 fix).
- iff1, iff2, im, halted: in `Z80Registers`. ✓
- q: persisted as `regs_.Q`. ✓ (Pass-3 fix).
- interrupts_enabled_at: persisted directly into `z80.interrupts_enabled_at`.
  ✓ (Pass-4 fix).
- IncDecZ shadow: NOT persisted (documented as design tradeoff at
  `z80_cpu.cpp:840-847` — adding bytes mid-stream would break snapshot
  backwards compat; worst-case effect is one LDWS reading P=0 instead of its
  prior value, immediately resynced by next BC-dec block transfer or DJNZ).
  Class-(c) accepted tradeoff, not a regression.

External global `tstates` is owned by Emulator, not Z80Cpu — out of CPU
subsystem scope.

No new findings.

### Angle 6 — IM2 fabric final corner sweep

**Finding A3 (class-a)**: `reti_decode_` derived from POST-advance state was
incorrect at the moment `reti_seen_pulse_` fires.

VHDL `im2_control.vhd:233-234`:

```vhdl
o_reti_decode <= '1' when state    = S_ED_T4   else '0';
o_reti_seen   <= '1' when state_next = S_ED4D_T4 else '0';
```

Both signals are combinational from the same pre-edge view of the FSM. At
T4 of the 0x4D fetch (ED followed by 4D):

- `state = S_ED_T4` (still — clock edge hasn't fired yet)
- `state_next = S_ED4D_T4` (will become state on next edge)
- `o_reti_decode = '1'` AND `o_reti_seen = '1'` SIMULTANEOUSLY

The IM2 device's S_ISR → S_0 transition (`im2_device.vhd:124`) gates on
`i_iei`. The chained IEI signal flows through upstream devices' `o_ieo`
equation (`im2_device.vhd:138-145`). For an upstream S_REQ device:

```
o_ieo = i_iei AND i_reti_decode
```

If `i_reti_decode = 0` at the moment `reti_seen` pulses, every upstream
S_REQ device would force IEO=0, BLOCKING any lower-priority S_ISR clear.
That would break NESTED IM2 ISRs whenever a higher-priority S_REQ is pending
while a lower-priority handler is running its RETI.

Pass-9 jnext model: `advance_decoder()` updated `dec_state_` to S_ED4D_T4
INSIDE the call, then `reti_decode_ = (dec_state_ == S_ED_T4)` evaluated
post-advance ⇒ FALSE. The on_reti() handler then computed IEI snapshots
using `reti_decode_ = false`, blocking the RETI clear chain. Class-(a).

**Fix**: keep `reti_decode_` derived from post-advance state (preserves
external test observability — see IM2C-01, IM2C-05, IM2P-02 in
`ctc_test.cpp` which exercise the post-edge view). But IN BOTH PLACES that
matter for correctness — `on_reti()` IEI snapshot and the
`step_state_machine_with_iei()` IEI snapshot in `step_devices()` — augment
the IEI computation with a synthetic "reti_decode is logically TRUE at the
same tick where reti_seen pulses":

```cpp
const bool iei_reti_decode = reti_decode_ || reti_seen_pulse_;
```

This mirrors the VHDL combinational simultaneity exactly without
disturbing the test API. Documented inline in `im2.cpp` `on_reti()` and
`step_devices()`.

### Daisy-chain priority

Walked the priority order: LINE=0 (highest), UART0_RX=1, UART1_RX=2,
CTC0..7=3..10, ULA=11, UART0_TX=12, UART1_TX=13. `int_line_asserted()` and
`ack_vector()` walk in this order. `device_ieo()` chains from device 0 with
hard-wired `i_iei='1'`. ✓

### Pulse mode

`step_pulse()` (vhdl:2017-2044): rising-edge detection of qualified sources
(`(int_req_d_edge AND int_en) OR int_unq`), pulse_int_n latching, count-up
with machine-specific termination width (32 for 48K/+3, 36 for 128K/Pentagon
/Next-default). int_unq one-shot is correctly cleared after pulse fires (line
932). The exception case (ULA, index 11) fires in pulse mode unconditionally
or in IM2 mode when CPU isn't in IM=2 — matches `im2_peripheral.vhd:192`. ✓

### NR 0xC0 / C4 / C5 / C6 / CC composition

NR 0xC8/C9/CA read packing checked against `zxnext.vhd:6247-6254`. All bit
positions match. ✓ NR 0xCC/CD/CE DMA enable mask wired through
`set_dma_int_en_mask` with O(N) fan-out. ✓

### Angle 7 — NMI / INT / HALT race conditions final

- NMI checked first (z80_cpu.cpp:416), INT second (line 452). NMI has
  priority. ✓
- IM2 + NMI overlap: NMI is unconditional (no IFF1 gate). Z80 enters NMI
  ISR; INT pending stays. ✓
- HALT + INT: `fuse_z80_interrupt()` line 130 bumps PC by 1 if halted.
  `fuse_z80_nmi()` line 167 same. ✓
- EI grace + Z80N opcode: EI sets `interrupts_enabled_at = tstates`. The
  EI-grace gate (`tstates == interrupts_enabled_at`) blocks INT for one
  instruction. Z80N opcodes do NOT touch `interrupts_enabled_at`, so EI
  grace properly survives across one Z80N instruction. ✓ (Pass-4 explicit
  `z80.iff2_read = 0` reset on Z80N path leaves EI-grace alone.)
- INT pulse expiry while in ISR: `int_pending_` is cleared on acceptance
  (line 502), so subsequent execute()s don't re-check expiry for the same
  pulse. ✓
- iff2_read NMOS quirk: FUSE's `fuse_z80_interrupt()` clears F.P when
  `iff2_read=1` (line 126-128). Our wrapper's Z80N path resets
  `z80.iff2_read = 0` at start so a Z80N opcode between LD A,I and an INT
  acceptance doesn't trigger the quirk wrongly. ✓ (Pass-4 fix.)

No new findings in this angle.

### Angle 8 — Cycle accuracy final spot-check

For each Z80N opcode the inner-MCycle TState totals were re-derived from
`t80n_mcode.vhd` and added to the ED prefix M1 (4T) baseline:

| Opcode | Inner MCycles | Inner total | + ED M1 | Our return | Status |
|--------|---------------|-------------|---------|------------|--------|
| SWAPNIB / MIRROR_A | 1 (4T forced) | 4 | 8 | 8 | ✓ |
| MUL_DE | 1 (4T) | 4 | 8 | 8 | ✓ |
| ADD HL,A / DE,A / BC,A | 1 (4T) | 4 | 8 | 8 | ✓ |
| BSLA/BSRA/BSRL/BSRF/BRLC | 1 (4T) | 4 | 8 | 8 | ✓ |
| PIXELDN / PIXELAD / SETAE | 1 (4T) | 4 | 8 | 8 | ✓ |
| TEST_N | 2 (4+3) | 7 | 11 | 11 | ✓ |
| ADD HL/DE/BC,nn | 3 (4+3+3) + 2 idle | 12 | 16 | 16 | ✓ |
| PUSH_NN | 6 (4+3+3+3+3+3) + 2 idle | 23 | 23 | 23 | ✓ |
| OUTINB | 3 (4+3+4 IORQ-wait) | 11 | 15+1idle | 16 | ✓ |
| NEXTREG_NN | 5 (4+3+3+3+3) | 16 | 20 | 20 | ✓ |
| NEXTREG_A | 4 (4+3+3+3) | 13 | 17 | 17 | ✓ |
| JP (C) | 2 (4+4 IORQ) | 8 | 12 | 12 | ✓ (Pass-8) |
| LDIX / LDDX | 3 (4+3+3) + 2 idle | 12 | 16 | 16 | ✓ |
| LDWS | 3 (4+3+3) | 10 | 14 | 14 | ✓ |
| LDIRX/LDDRX/LDIRSCALE/LDPIRX (cont) | as LDIX + 5 redecode | 17 | 21 | 21 | ✓ |
| LDIRX/etc. (term) | as LDIX | 12 | 16 | 16 | ✓ |

All cycle counts match. No new findings.

## Spot-checks during the audit

- Verified `fuse_z80_writebyte` / `fuse_z80_readbyte` route data accesses
  through `ContentionModel::contention_tick()` for VHDL-faithful per-cycle
  contention stretch on contended pages. ✓
- Verified Z80N M1 contention via `contend_read(pc, 4)` pair in the wrapper
  (Pass-5 fix). ✓
- Verified Z80N data-access contention via `fuse_z80_readbyte` /
  `fuse_z80_writebyte` for operand reads + stack writes (Pass-6 fix). ✓
- Verified Z80N internal-idle no-MREQ contention via
  `contend_write_no_mreq` for LDIX-family DE-tail cycles (Pass-7 fix) and
  for transparency-suppressed write phase (Pass-9 fix). ✓
- Verified IncDecZ shadow update on DJNZ + ED-block-transfers + Z80N block
  transfers (Pass-9 fix). LDWS now reads VHDL-faithful F.P. ✓

## Class-(d) carry-forward (out of strict-convergence scope)

**ARCH-CPU-01**: In IM2 mode (NR 0xC0 bit 0 = 1), the emulator never bridges
`Im2Controller::int_line_asserted()` to `Z80Cpu::request_interrupt()`. The
two `request_interrupt(0xFF)` call sites in `emulator.cpp` (frame INT at
line 4777, line INT at line 5862) are gated on `!im2_.is_im2_mode()`, so in
IM2 mode the CPU never observes a CPU INT — `int_line_asserted()` is dead
code. `on_int_ack` is wired to `im2_.ack_vector()` but is never invoked
because `int_pending_` never becomes true. Real-hardware path: peripherals
raise via `raise_req()`, fabric drives `o_int_n` low (vhdl im2_device:150
+ peripherals.vhd OR-reduction), Z80 enters IntAck. Pre-existing
architectural gap; out of scope for pass-10 strict convergence (would
require a per-tick `int_line_asserted()` poll + `request_interrupt(0)`
synthesis to feed `int_pending_`). Tests bypass this gap by directly
calling `cpu_.request_interrupt(0xFF)`.

## Convergence verdict

**0 pending bugs class-(a/b/c) in scope after pass-10.**

The CPU subsystem in scope (`z80_cpu`, `z80n_ext`, `im2`) is converged
against the VHDL oracle and the FUSE Z80 oracle. The three class-(a)
findings — LDPIRX I_BT flag composition, ADD HL/DE/BC,A F.C truncation,
and reti_decode timing in IM2 fabric — are all resolved.

One class-(d) architectural gap (IM2-mode INT acceptance bridge) is
documented as ARCH-CPU-01 for future work; it is not a regression introduced
by any prior pass and does not block strict convergence on the audited
opcode/state behavior.

## Test results

- FUSE Z80: **1356 / 1356 PASS / 0 FAIL / 0 SKIP**.
- ctest: **37 / 37 PASS** (z80n_tests 85/85, ctc_tests 132/132 IM2 fabric).
