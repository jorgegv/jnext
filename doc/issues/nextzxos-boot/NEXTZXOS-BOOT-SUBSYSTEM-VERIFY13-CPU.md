# Pass-13 CPU Subsystem Audit Report (Z80 base + Z80N + IM2)

**Branch**: `task2/verify13-cpu-z80n-im2`
**Worktree**: `.claude/worktrees/task2-verify13-cpu-z80n-im2`
**Methodology**: Blind re-audit (no prior pass reports consulted) against
  VHDL oracle (`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`)
  and FUSE Z80 oracle (`third_party/fuse-z80/`).

## Convergence claim

This pass found **one** class-(a) finding (V13-CPU-01, polarity bug in
DJNZ IncDecZ shadow). Class-(d) note retained for the IM2-mode CPU INT
line wiring gap previously surfaced.

The subsystem is **NOT** at zero findings. Convergence is not yet achieved
for the CPU subsystem.

## Findings summary

| ID            | Severity   | Component | Status                                  |
|---------------|------------|-----------|-----------------------------------------|
| V13-CPU-01    | class-(a)  | DJNZ      | Fixed + 1 modified test + 1 new test    |
| V13-CPU-D1    | class-(d)  | IM2 line  | Architectural — listed only             |

## V13-CPU-01 — DJNZ IncDecZ shadow polarity inverted

**Severity**: class-(a)
**Files**: `src/cpu/z80_cpu.cpp:803-807` (production code) +
  `test/cpu/cpu_z80n_im2_regressions_test.cpp:1206..` (existing test
  was modified — it encoded the bug; new V13 test added).

### VHDL oracle citation

`cpu/t80n.vhd:1358-1360`:

```vhdl
if I_DJNZ = '1' and Save_ALU_r = '1' and Mode < 2 then
   IncDecZ <= F_Out(Flag_Z);
end if;
```

`cpu/t80n_mcode.vhd:1140-1144` (DJNZ MCycle 1 ALU op):

```vhdl
when 1 =>
   TStates <= "101";
   I_DJNZ <= '1';
   Set_BusB_To <= "1010";              -- "00000001"
   Set_BusA_To(2 downto 0) <= "000";   -- B
   Read_To_Reg <= '1';
   Save_ALU <= '1';
   ALU_Op <= "0010";                   -- SUB
```

`cpu/t80n_alu.vhd:189-196` (Z flag composition for SUB):

```vhdl
if Q_t(7 downto 0) = "00000000" then
   F_Out(Flag_Z) <= '1';
   ...
else
   F_Out(Flag_Z) <= '0';
end if;
```

### Diagnosis

DJNZ runs an ALU `B - 1` operation at MCycle 1 (`ALU_Op="0010"`,
`BusA=B(000)`, `BusB="00000001"`). `Save_ALU='1'` writes the F register;
the F.Z bit is **set when the result is zero**, i.e. when B was 1
entering DJNZ and is 0 after the decrement.

`IncDecZ <= F_Out(Flag_Z)` (line 1359) latches that flag. So:

| B (entering DJNZ) | B (post-dec)  | F.Z (of B-1) | IncDecZ (VHDL) |
|-------------------|---------------|---------------|----------------|
| 0                 | 0xFF (wrap)   | 0             | 0              |
| 1                 | 0             | 1             | 1              |
| 2                 | 1             | 0             | 0              |
| n>0               | n-1           | (n==1)?1:0    | (n==1)?1:0     |

The C++ shadow update at `z80_cpu.cpp:803-807` (pre-V13 fix) wrote:

```cpp
regs_.IncDecZ = ((regs_.BC >> 8) & 0xFF) ? 1u : 0u;
//               ^^^^^^^^^^^^^^^^^^^^^^^^^
//               post-decrement B != 0 → 1
```

This is the OPPOSITE polarity. The accompanying inline comment ("F_Out(Flag_Z)
of B-1") describes the correct VHDL semantic, but the code stores the
"BC nonzero" convention from the BC-decrementing block-transfer path
(`t80n.vhd:1361-1366`, where `IncDecZ <= '1' if ID16/=0`). The two
latch sites in VHDL use **inverted** meanings; jnext was applying the
block-transfer polarity to the DJNZ branch.

### Discriminative impact

`IncDecZ` feeds `F.Flag_P` for any subsequent `I_BC` or `I_BT` instruction
via `t80n.vhd:1283-1284`:

```vhdl
if I_BC = '1' or I_BT = '1' then
   F(Flag_P) <= IncDecZ;
end if;
```

The Z80N `LDWS` (ED A5) opcode sets `I_BT='1'` at MCycle 3
(`t80n_mcode.vhd:2171`), so a `LDWS` immediately after a `DJNZ` reads
the IncDecZ shadow and emits it as F.P.

| Sequence                          | VHDL F.P (LDWS) | jnext pre-V13 F.P |
|-----------------------------------|-----------------|-------------------|
| `B=2; DJNZ +n; LDWS` (taken)      | 0               | 1 (WRONG)         |
| `B=1; DJNZ +n; LDWS` (not taken)  | 1               | 0 (WRONG)         |
| `B=5; DJNZ +n; LDWS` (taken)      | 0               | 1 (WRONG)         |

In all cases the F.P bit is INVERTED. Software using LDWS for screen
manipulation followed by JP PE / JP PO would take the wrong branch.

### Fix

`src/cpu/z80_cpu.cpp:803-807`:

```cpp
regs_.IncDecZ = (((regs_.BC >> 8) & 0xFF) == 0) ? 1u : 0u;
//               ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
//               post-decrement B == 0 → 1 (matches VHDL F.Z semantic)
```

Plus an explanatory comment block citing the two inverted VHDL latch sites.

### Tests

1. **Modified `test_pass9_ldws_incdecz_after_djnz`** — the prior Pass-9
   test expected `IncDecZ=1` and LDWS `F.P=1` for the B=2→1 (branch-taken)
   case. That expectation matched the BUG. Renamed to
   `V13-CPU-01-Z80N-LDWS-INCDECZ-FROM-DJNZ-TAKEN`, flipped the
   expectations to `IncDecZ=0` and `F.P=0`. The trail of the original
   `b40af13` reference is preserved in the test name parenthetical.

2. **New `test_v13_cpu_01_ldws_incdecz_after_djnz_not_taken`** — adds
   coverage for the OPPOSITE polarity side: B=1 entering DJNZ (no branch,
   B→0). VHDL F.Z=1 → IncDecZ=1 → LDWS F.P=1. Pre-V13 jnext stored
   IncDecZ=0 → F.P=0. Discriminative.

The two tests together pin BOTH polarity sides of the inverted shadow:

| Case             | VHDL IncDecZ | Pre-V13 IncDecZ |
|------------------|--------------|------------------|
| B=2→1 (taken)    | 0            | 1                |
| B=1→0 (not taken)| 1            | 0                |

Reverting the production code fix immediately fails BOTH tests. Confirmed
via revert-check (commit revert-check-only): production code reverted to
`(B != 0) ? 1 : 0` → both V13 tests fail; restored to `(B == 0) ? 1 : 0`
→ all 27 CPU tests pass.

## V13-CPU-D1 — IM2 mode does not drive CPU INT line (architectural)

**Severity**: class-(d) — listed only, no fix attempted in this audit.

`Im2Controller::int_line_asserted()` is declared in `src/cpu/im2.h:122`
and defined in `src/cpu/im2.cpp:505-519`, but no caller invokes it.
Searching the source tree:

```text
$ grep -rn 'int_line_asserted' src/
src/cpu/im2.h:122
src/cpu/im2.cpp:505
src/cpu/im2.cpp:870 (comment)
src/core/emulator.cpp:4872 (comment only)
```

The two emulator-side INT-firing paths (`Emulator::run_frame` ULA-int
schedule at `:4878-4884` and line-int schedule at `:5961-5970`) both
gate `cpu_.request_interrupt(0xFF)` on `!im2_.is_im2_mode()`:

```cpp
im2_.raise_req(Im2Controller::DevIdx::ULA);
if (!im2_.is_im2_mode()) {
    cpu_.request_interrupt(0xFF);
}
```

Result: when NR 0xC0 bit 0 is set (HW IM2 fabric mode), `raise_req`
correctly latches the device into `S_REQ`, but the CPU's
`int_pending_` flag is never set, so `Z80Cpu::execute()` never enters
its INT-acceptance branch and `on_int_ack` is never invoked. The IM2
fabric becomes a no-op for the actual Z80 once the application enables
HW IM2 mode.

The intended design (per the comment at `emulator.cpp:4872`) is that
`Im2Controller::int_line_asserted()` would be polled per-tick to drive
`cpu_.request_interrupt(...)`. That polling does not exist.

### Why class-(d)

- The fabric scaffold is correct (advance_decoder, daisy chain, vector
  composition all match VHDL).
- The CPU side accepts INT correctly when `request_interrupt` is called
  (Pass-8 EI grace gate, etc.).
- The missing piece is the bridge wiring in `Emulator::run_frame` that
  consults `int_line_asserted()` and forwards to `request_interrupt`,
  with vector latching via `on_int_ack -> ack_vector`.
- This is a multi-call-site change in `emulator.cpp` (run_frame loop +
  scheduler interaction) plus an audit of all peripheral paths
  (CTC/UART/Md6) that currently rely on the legacy pulse path.
- Scope: NOT a CPU subsystem fix; NOT a single-instruction emulator
  bug. It's an architectural bridge.

NextZXOS boot does not exercise HW IM2 mode (it stays in pulse/IM1),
so this gap does not affect the boot regressions that the broader
Task 2 audit targets.

## Pass-13 angles checked (negative results)

The following were re-audited fresh against VHDL/FUSE; all clean (no
new findings beyond V13-CPU-01):

- **All Z80N opcodes** (ED 23/24/27/28/29/2A/2B/2C, 30..36, 8A, 90/91/92/93/94/95/98, A4/A5, AC, B4/B6/B7, BC) verified against `t80n_mcode.vhd` cases + `t80n.vhd` Z80N command block (lines 727-948). T-state totals, flag composition, register effects, MEMPTR/WZ updates all match. No new wrap-corruption family bugs found in PIXELAD/SETAE (V11-CPU-02 PIXELDN already fixed).
- **Z80N OUTINB extended-M1 cycle** (V12-CPU-NIT-02 fix) re-confirmed correct — `contend_read_no_mreq(IR, 1)` before the operand read matches VHDL `t80n_mcode.vhd:2528-2530` MCycle 1 `TStates="101"` for OUTINB. Other Z80N block-transfer wait-state siblings (LDIRX, LDDRX, LDIRSCALE, LDPIRX) use `contend_write_no_mreq` for the per-cycle internal idle on DE — matches VHDL `t80n_mcode.vhd:2135-2137` MCycle 4 `NoRead='1'; TStates="101"` and the LDIR/LDDR-equivalent per-iteration re-decode pause.
- **Z80N I_BT flag composition** (LDIX/LDIRX/LDDX/LDDRX/LDPIRX/LDIRSCALE/LDWS) all use the documented `ldi_family_flags()` helper or LDWS/LDPIRX-specific composition; X=ALU_Q[3], Y=ALU_Q[1], H=0, N=0, P=IncDecZ matches VHDL `t80n.vhd:1277-1289`. (V13-CPU-01 fixes the `IncDecZ` polarity for the DJNZ-fed input to that override; the I_BT override math itself is correct.)
- **Q register hygiene** at Z80N dispatch (`z80_cpu.cpp:619`) and per-instruction Q updates verified — non-F-writing Z80N opcodes leave Q=0 (set at dispatch entry); F-writing ones write `regs.Q = f`. SCF/CCF X/Y composition reads `last_Q` correctly via FUSE.
- **IM2 FSM transitions** (im2_control.vhd lines 158-209) re-walked: all 7 states (S_0, S_ED_T4, S_ED4D_T4, S_ED45_T4, S_CB_T4, S_DDFD_T4, S_SRL_T1, S_SRL_T2) and their transitions match the C++ `advance_decoder`. V11-CPU-01 DDFD→ED→S_0 fix is preserved.
- **IM2 vector composition**: `nr_c0_im2_vector(2:0) & im2_vec(3:0) & '0'` matches `compute_vector()`. I=0xFF + vector LSB does NOT collide (vector low bit is always 0). Daisy chain re-arbitration on nested ISRs walks priority correctly (`ack_vector` walks index 0..N-1, `iei` snapshot pre-transition).
- **EI grace gate**: `tstates == interrupts_enabled_at` check matches FUSE `fuse_z80_interrupt:123`. Pass-8 fix that gates `on_int_ack` BEFORE the FUSE call is correctly preserved (avoiding fabric S_REQ→S_ACK advance during EI grace).
- **HALT exit semantics**: FUSE `fuse_z80_interrupt` and `fuse_z80_nmi` both do `if (z80.halted) { PC++; halted=0 }` before pushing — PC is correctly advanced past the HALT byte. Z80Cpu::execute NMI path captures the post-HALT-exit PC for `on_nmi_servicing` shadow latch (`z80_cpu.cpp:425-428`).
- **DD/FD/CB/ED prefix-chain inner-byte M1 delivery**: walk loop in `z80_cpu.cpp:738-758` re-verified to deliver on_m1_cycle for every prefix byte plus the inner ED ext byte. CB inside DD/FD chain correctly stops after CB (the displacement and inner op are data reads, not M1).
- **R register on M1**: `R++` happens for ED prefix M1 + inner M1 (z80_cpu.cpp:586 explicit +2) and for FUSE main-switch M1 fetches (FUSE handles internally). DD/FD chain: each prefix byte gets `R++` via FUSE end_opcode loop. Verified consistent.
- **LDIR/LDDR/CPIR/CPDR/INIR/INDR/OTIR/OTDR INT-checkpoint mid-instruction**: G89 per-iteration re-fetch shape (PC-=2 when continuing) is implemented for FUSE block transfers natively. Z80N variants (LDIRX/LDDRX/LDIRSCALE/LDPIRX) implement the same shape inline — `regs.PC = (regs.PC - 2) & 0xFFFF` when BC≠0. Top of `Z80Cpu::execute()` samples INT_pending before the next M1 fetch, so each iteration of LDIR/LDIRX/etc. is interruptible at the right boundary.
- **Memory contention t-state insertion for I/O cycles**: `fuse_z80_writeport`/`fuse_z80_readport` model 1T pre-IORQ + contention_tick + 3T post-IORQ = 4T total + stretch. Matches VHDL's `IOWait=1` standard I/O cycle.
- **Contention table for 48k vs 128k vs +2A/+3 vs Next vs Pentagon**: handled by `ContentionModel::contention_tick()` (out of CPU subsystem scope, but the entry points are CPU side and verified to forward (mreq_n, iorq_n, rd_n, wr_n, address, hc, vc) correctly.
- **Turbo mode (NR $07) contention emission**: 7/14/28 MHz scaling handled by `s_tstates_per_line/_frame` configuration in `z80_set_contention_runtime`. CPU side does not emit contention itself — the gate logic is uniform across speed tiers; turbo affects only the master cycle accounting outside CPU scope.

## Tests

```text
ctest --test-dir build --output-on-failure  →  100% (38/38)
./build/test/fuse_z80_test build/test/fuse  →  1356/1356 PASS
./build/test/cpu_z80n_im2_regressions_test  →  27/27 PASS
```

Revert-check on V13-CPU-01: production code reverted to pre-V13
polarity → 2 tests fail (V13-CPU-01-LDWS-INCDECZ-FROM-DJNZ-TAKEN +
V13-CPU-01-LDWS-INCDECZ-FROM-DJNZ-NOT-TAKEN). Production restored →
all 27 pass. Revert-check confirmed discriminative.
