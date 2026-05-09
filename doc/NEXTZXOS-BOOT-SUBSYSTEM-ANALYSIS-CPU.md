# NextZXOS Boot — CPU Subsystem (Z80 + Z80N + IM2) Analysis

**Author**: Task 2 reviewer (CPU subsystem)
**Date**: 2026-05-09
**Branch**: `task2/cpu-z80n-im2-review`
**Worktree**: `.claude/worktrees/task2-cpu-z80n-im2/`

## Executive summary

The Z80 base instruction core is the third-party FUSE Z80 implementation
(`third_party/fuse-z80/`) wrapped through `Z80Cpu`, and it passes the
FUSE opcode test fixture 1356/1356 — including all RST/RET/RETI/RETN/IM/EX
(SP),HL/PUSH/POP/HALT corner cases that the supervisor stresses on the
boot path. Audit of the wrapper glue (`z80_cpu.cpp`) found nothing that
diverges from the FUSE Z80 semantics: the M1-prefetch/post-fetch hooks,
NMI/INT acceptance window, IM2 vector path, R-register increment for
Z80N, HALT recovery, and IFF1/IFF2 handling on RETN are all consistent
with the canonical Z80 model the FUSE suite validates.

The Z80N extension layer (`z80n_ext.cpp`) was audited against the
authoritative VHDL oracle (`t80n_mcode.vhd` / `t80n.vhd`) opcode by
opcode. Functional behaviour (register effects, flags, byte order,
memory addressing) is correct everywhere. **The single notable finding
is a systematic T-state under-count** on the Z80N path:
`Z80Cpu::execute()` intercepts ED + Z80N before any FUSE callback, but
the per-opcode T-state returns omitted the ED-prefix M1 fetch (most
ops were 4 T-states short of the published spec; some by 8). This
shifts IM2 vsync timing, raster contention windows, and any timing
derived from the global `tstates` counter forward, by an amount that
compounds across the thousands of Z80N invocations the NextZXOS
supervisor issues every frame. **Fixed in this branch.**

The IM2 fabric (`im2.{cpp,h}`) is a faithful, well-commented port of the
VHDL `im2_control.vhd` / `im2_device.vhd` / `im2_peripheral.vhd` tree.
The decoder FSM, daisy-chain priority, IEO propagation, S_REQ→S_ACK
latch in `ack_vector`, S_ISR→S_0 RETI gating, pulse-mode width (32 vs
36 cycles), and the `im2_dma_delay` self-hold latch all have explicit
VHDL line citations in the source and behaviour matches.

**Top G46(b) suspect — `PUSH imm` byte order — is correct.** Verified
in three independent ways: (1) line-by-line VHDL trace of `t80n_mcode.vhd:1921-1948`;
(2) the existing Z80N test fixture `ed8a_basic` whose `tests.expected`
file embeds the canonical behaviour; (3) reasoning from Z80 stack
semantics (`mem[SP]=low, mem[SP+1]=high` after `PUSH`).

## Methodology

1. Re-read `src/cpu/{z80_cpu,z80n_ext,im2,im2_client}.{cpp,h}` end-to-end.
2. For Z80 base ops, accepted FUSE-Z80 1356/1356 as oracle (spec-validated).
3. For Z80N opcodes, line-by-line trace against `t80n_mcode.vhd` and
   `t80n.vhd` (the FPGA decoder + ALU). Each opcode's MCycle skeleton
   was re-derived to confirm the C++ implementation matches:
   - byte order of operands fetched from the instruction stream
   - direction of HL/DE/BC increment/decrement in block ops
   - flag effects in arithmetic / TEST / shift ops
   - which bits of the destination registers are written for
     PIXELAD / PIXELDN / JP (C)
4. Cross-referenced the official Spectrum Next opcode timing table
   (`next.specnext.dev`) for T-state values.
5. For IM2, validated each method against the cited VHDL line numbers.
6. Built and ran:
   - `fuse_z80_test` (1356/1356)
   - `z80n_test` (85/85)
   - Full ctest suite (36/36 — no regressions)

## Findings

### Class (a) — Bugs fixed in this branch

#### Z80N opcodes systematically under-counted T-states

**Location**: `src/cpu/z80n_ext.cpp` (every case in the dispatch).

**Mechanism**: `Z80Cpu::execute()` (`src/cpu/z80_cpu.cpp:451-494`) detects
the ED prefix and a Z80N ext byte using two **raw**
`mem_.read()` calls — these bypass the FUSE-Z80 contention/timing
shim, so they neither contend nor add T-states. The path then advances
PC by 2, increments R by 2, and dispatches to `execute_z80n()`. The
returned T-state count from `execute_z80n()` therefore must include
the **full** instruction time, including BOTH M1 fetches (ED + ext =
8 T-states baseline).

The pre-fix returns omitted the M1 fetch contribution on most opcodes:

| Opcode             | Spec | Pre-fix | Post-fix |
|--------------------|------|---------|----------|
| SWAPNIB            | 8    | 4       | 8        |
| MIRROR A           | 8    | 4       | 8        |
| TEST n             | 11   | 7       | 11       |
| BSLA/BSRA/BSRL/BSRF/BRLC | 8 | 4    | 8        |
| MUL D,E            | 8    | 4       | 8        |
| ADD HL/DE/BC,A     | 8    | 4       | 8        |
| ADD HL/DE/BC,nn    | 16   | 12      | 16       |
| PUSH nn            | 23   | 23      | 23       |
| OUTINB             | 16   | 10      | 16       |
| NEXTREG nn,nn      | 20   | 16      | 20       |
| NEXTREG nn,A       | 17   | 13      | 17       |
| PIXELDN            | 8    | 4       | 8        |
| PIXELAD            | 8    | 4       | 8        |
| SETAE              | 8    | 4       | 8        |
| JP (C)             | 13   | 12      | 13       |
| LDIX               | 16   | 13      | 16       |
| LDWS               | 14   | 14      | 14       |
| LDDX               | 16   | 13      | 16       |
| LDIRX (per iter)   | 21/16 | 13     | 21 / 16  |
| LDIRSCALE          | 21/16 | 13     | 21 / 16  |
| LDPIRX             | 21/16 | 13     | 21 / 16  |
| LDDRX              | 21/16 | 13     | 21 / 16  |

`PUSH nn` and `LDWS` were already correct pre-fix; everything else
gained 1-8 T-states per execution. The repeating block ops (LDIRX
family) now distinguish "still repeating" (21) from "terminal iteration"
(16), matching the standard LDIR shape.

**Boot relevance**: NextZXOS supervisor invokes `NEXTREG`,
`MUL D,E`, and `ADD HL/DE/BC,A` continuously (the bank-3 wrapper
at `$5B48` is literally `ED 91 8E 03 C9` — exactly the NEXTREG path).
A 4-T-state under-count per NEXTREG, accumulated over the dozens of
NEXTREG writes per supervisor cycle, drifts the global `tstates`
counter early, which:
- shifts IM2 vsync interrupt firing earlier than VHDL would emit it,
- shifts raster-contention windows leftward,
- desynchronises the deferred-NR-write window
  (`Emulator::defer_cpu_nr_writes_` per `emulator.cpp:4408+`).

The fix is a pure timing correction; behaviour (register effects /
flags / memory) is unchanged. FUSE Z80 test suite is **unaffected** (the
suite never executes Z80N opcodes), so 1356/1356 remains. The Z80N
`tests.expected` file already encodes the spec values; the in-tree
runner has them parked behind a `NOTE` (`z80n_test.cpp:243-247`) that
explains why it doesn't compare T-states yet — but the canonical values
in the fixture are authoritative.

**Tests**:
- FUSE Z80 1356/1356 (unchanged)
- Z80N 85/85 (unchanged — runner doesn't compare tstates)
- Full ctest 36/36 (unchanged)

### Class (b) — Confirmed conformant; no action

#### `PUSH imm` (ED 8A hh ll) — VERIFIED CORRECT

The G46(b) prompt named this as the prime suspect for the "3 missing
PUSHes / 3 extra POPs vs CSpect" observation. Triple-source verification:

1. **VHDL trace** (`t80n_mcode.vhd:1921-1948`):
   - MCycle 1: `Inc_PC + LDZ` — fetch FIRST operand byte, latch low byte of TmpAddr (= DI_Reg)
   - MCycle 2: `IncDec_16=1111` (SP--), `Set_BusB_To="0110"` (BusB := DI_Reg = first byte)
   - MCycle 3: `Inc_PC + LDZ + Write` — write BusB (first byte) to mem[SP], read SECOND byte into DI_Reg
   - MCycle 4: SP--, BusB := DI_Reg (second byte)
   - MCycle 5: Write — write BusB (second byte) to mem[new SP]
   - **Net**: first operand fetched → mem[SP+1]; second operand fetched → mem[SP].
2. **Test fixture** (`tests.in:283-298, tests.expected:281-301`):
   `ED 8A 12 34` → `mem[$ffee]=$34, mem[$ffef]=$12, SP=$ffee, tstates=23`.
3. **Z80 stack semantics**: after `PUSH AB`, mem[SP]=B (low), mem[SP+1]=A (high).

C++ implementation (`z80n_ext.cpp:200-205`):
```cpp
uint8_t hh = cpu.memory().read(regs.PC);     // first operand  → high byte of pushed value
uint8_t ll = cpu.memory().read(regs.PC + 1); // second operand → low byte of pushed value
regs.SP = (regs.SP - 2) & 0xFFFF;
cpu.memory().write(regs.SP + 1, hh);          // high at high addr
cpu.memory().write(regs.SP, ll);              // low at low addr
```

Matches all three sources exactly. The bank 0 wrapper site that
prompted the suspicion (`ED 8A 00 7B` at `$0072`, expected to push
`$007B`) produces: hh=$00, ll=$7B → `mem[SP+1]=$00, mem[SP]=$7B` →
RET reads $7B then $00 → returns to $007B. ✓

#### `NEXTREG nn, imm / nn, A` — operand order, target dispatch

The bank-3 supervisor wrapper at RAM `$5B48` is `ED 91 8E 03 C9`
(`NEXTREG $8E, $03; RET`). VHDL `t80n_mcode.vhd:1668-1709`:
- ED 91 (NEXTREG nn, vv): MCycle 1 reads register byte (`Z80N_data_o_strobe_hi`), MCycle 2 reads value (`_strobe_lo`).
- ED 92 (NEXTREG nn, A): MCycle 1 reads register byte, value sourced from `ext_ACC_i` (= A register).

C++ dispatches both via the standard port path: `OUT 0x243B, reg; OUT 0x253B, val`. `Emulator::init` (`emulator.cpp:2418-2438`) wires both ports to the same `nextreg_.select()` / `nextreg_.write_selected()` functions, so the in-opcode path converges with port-write semantics — including the deferred-write logic for the per-instruction tick window.

#### `JP (C)` (ED 98) — PC bit composition

VHDL `t80n.vhd:979-983`: `PC(13:6) <= DI_Reg; PC(5:0) := "000000"`.
PC[15:14] are NOT written → preserved.

C++ (`z80n_ext.cpp:289`): `regs.PC = (regs.PC & 0xC000) | ((uint16_t)val << 6);`
- `0xC000` masks out everything except bits 15-14 (preserve)
- `val << 6` puts `val` into bits 13-6
- Bits 5-0 are zero by construction.

Test cross-check: `ed98_ff` (`tests.in:386`, `tests.expected:389-393`)
with PC=$8000, BC=$ff00 → expected PC=$BFC0:
- `0x8000 & 0xC000 = 0x8000` (preserves "10")
- `0xFF << 6 = 0x3FC0`
- `0x8000 | 0x3FC0 = 0xBFC0` ✓

#### Z80N flag semantics (boot-critical paths)

- **TEST n** (ED 27): H=1, N=0, C=0; S/Z/P/V/X/Y from `A AND n`. Matches VHDL ALU AND semantics.
- **ADD HL/DE/BC,A** (ED 31/32/33): only Carry flag updated, all others preserved. VHDL `t80n.vhd:783` (`F(Flag_C) <= reg_temp_t(16)`).
- **ADD HL/DE/BC,nn** (ED 34/35/36): no flag effect. VHDL has no `F(...)` assignment in those cases.
- **MUL D,E** (ED 30): no flag effect. VHDL `t80n.vhd:729-741` writes register only.

#### Block-op direction conventions

| Opcode | HL | DE | BC | Notes |
|--------|----|----|----|----|
| LDIX (A4)   | ++ | ++ | -- | Skip-byte transparency |
| LDDX (AC)   | -- | ++ | -- | DE INCs while HL DECs (verified VHDL :2240-2250) |
| LDIRX (B4)  | ++ | ++ | -- | Repeats while BC≠0 |
| LDDRX (BC)  | -- | ++ | -- | Repeats while BC≠0; HL/DE same as LDDX |
| LDIRSCALE (B6) | ++ | ++ | -- | VHDL has `BC'/DE'` add commented out — plain HL++/DE++ stands |
| LDPIRX (B7) | unchanged | ++ | -- | Source = `(HL & 0xFFF8) | (E & 0x07)` |
| LDWS (A5)   | L++ only | D++ only | unchanged | Flags = INC D |

All match VHDL.

#### IM2 fabric

`im2.{cpp,h}` is well-cited against the VHDL line numbers it implements.
Audit confirmed:

- **Decoder FSM** (`advance_decoder`, `im2.cpp:607-689`): mirrors
  `im2_control.vhd:158-210` exactly — S_0 → S_ED_T4 → S_ED4D_T4 (RETI)
  / S_ED45_T4 (RETN) / IM-mode decode; CB and DD/FD prefix paths
  isolate the RETI/RETN look-ahead correctly.
- **IM-mode decode** (`im2.cpp:635-647`): bit-decodes ED 46/4E/66/6E
  → IM 0; ED 56/76 → IM 1; ED 5E/7E → IM 2 per VHDL `:223-224`.
- **`reti_seen_pulse_` / `retn_seen_pulse_`**: one-cycle pulses cleared
  at next `on_m1_cycle()`, matching VHDL `:234/:236`.
- **Daisy-chain IEO** (`device_ieo`, `im2.cpp:990-1014`): walks 0..i,
  S_0 passes IEI through, S_REQ passes only when `reti_decode`,
  S_ACK/S_ISR break chain. Matches `im2_device.vhd:136-146`.
- **S_REQ → S_ACK in `ack_vector`** (`im2.cpp:502-530`): walks priority
  order, picks first device with IEI=1, transitions atomically.
- **S_ISR → S_0 on RETI** (`im2.cpp:208-213, :827-840`): IEI snapshot
  taken pre-walk so cascading clears do not fire on the same RETI.
- **Pulse fabric** (`step_pulse`, `im2.cpp:875-939`): ULA `exception=1`
  fires in pulse mode OR in IM2 mode when CPU is not in IM=2; non-ULA
  devices fire only in pulse mode. Pulse width = 32 (48K/+3) or 36
  (128K/Pentagon/Next-default), gated on `pulse_count` bits 5 and 2.
- **`im2_dma_delay` self-hold** (`step_dma_delay`, `im2.cpp:961-966`):
  three-term OR with self-hold matches `zxnext.vhd:2001-2010`.

#### Z80 wrapper — INT pulse window

`z80_cpu.cpp:418` uses a 32-T-state INT-pulse window:
`static constexpr uint32_t INT_PULSE_TSTATES = 32;`. Per VHDL
`zxnext.vhd:2017-2031`, the pulse width is 32 on 48K/+3 but 36 on
128K/Pentagon/Next-default. This 32 fits 48K-only; for 128K/Next a
just-fired interrupt has 4 fewer T-states of opportunity to be
acknowledged inside an ISR. This is a minor accuracy concern but
unlikely to affect the G46(b) failure mode (interrupts are not the
diverging path between jnext and CSpect per the EOD-23 / EOD-24
investigations). **Not changed in this branch** — would warrant a
machine-aware constant if surfaced by future testing.

#### RETI / RETN IFF1 restore (FUSE)

`third_party/fuse-z80/z80_ed.c:61-72` treats both RETI (ED 4D) and
RETN (ED 45) as `IFF1 = IFF2; RET()`. This is undocumented-but-real
silicon behaviour (RETI also restores IFF1 on actual hardware,
contradicting Zilog docs). The FUSE test suite validates this — and
1356/1356 passes. Not a bug.

### Class (c) — Areas where conformance was reasoned but not exhaustively re-derived

- **BSRF fill-with-1 trick** (`z80n_ext.cpp:90-100`): correct for shift values
  0-31 by inspection of the 32-bit cast + arithmetic-shift idiom (compiler
  arithmetic shift on signed int is well-defined since C++20 and de
  facto on GCC/Clang/MSVC since forever).
- **BRLC `>> 16` UB at `rot=16`** (`z80n_ext.cpp:107`): nominally UB
  for `uint16_t >> 16`, but C integer promotion converts the operand
  to `int` (≥32 bits) before the shift, making the result well-defined
  zero. Result for rot=16: `regs.DE | 0 = regs.DE` — correct (rotate by 16
  is identity on a 16-bit value). No fix needed.
- **NEXTREG via I/O port path**: dispatches via `OUT $243B; OUT $253B`,
  which is functionally equivalent to the VHDL's direct `Z80N_dout_o`
  path because the I/O port handlers in `Emulator::init` route to the
  same `NextReg::write_selected()` function. Cycle accounting is
  preserved by the explicit `return 17/20` per spec.

## G46(b) cross-check

The investigation log identifies the proximate cause as: between RST $08
hits #2 and #3 in jnext, the supervisor stack ascends by 6 bytes (from
`$FF53` to `$FF59`) compared to CSpect's stable `$FF42` region —
the equivalent of "3 missing PUSHes / 3 extra POPs". This shows up
downstream as `post_rst_pc=$423C` (corrupt — = font glyph 'A' bytes
read as Z80 ops) instead of CSpect's `$5CCB / $0D7B / $5CFB` (BASIC
interpreter).

This subsystem audit clears the most attractive Z80N suspects:

- **PUSH imm byte order**: correct (verified three ways).
- **NEXTREG nn,vv**: dispatches correctly through the deferred-write
  path; the 5-byte boot wrapper `ED 91 8E 03 C9` semantics match VHDL.
- **Block-move HL/DE direction**: LDIX/LDDX/LDIRX/LDDRX/LDPIRX/LDIRSCALE
  all match VHDL.
- **Stack ops in Z80 base** (PUSH/POP, CALL/RET, EX (SP),HL): FUSE
  Z80 1356/1356 — covers all corner cases including IX/IY prefix and
  the contended-T-state shapes the FUSE suite encodes.
- **IM2 vector handling**: faithful to VHDL with explicit cite, and the
  RST $08 path is in the FUSE-validated Z80 base; the IM2 fabric does
  not interpose on the supervisor's RST $08 calls.

The CPU subsystem is therefore **unlikely to be the source of the
3-PUSH stack divergence**. The fix in this branch (Z80N T-state
under-count) is a real bug but its effect is timing-only — it shifts
when interrupts fire, not what gets pushed/popped on the stack. The
G46(b) divergence is more likely upstream, in the supervisor's API
dispatch / NextReg state / MMU paging machinery (per the EOD-21..24
log entries already pointing toward NEXTREG $8E,$03 at RAM $5B48 and
the bank-flip wrapper paths).

## Open questions

1. **INT pulse window machine-awareness**: should `INT_PULSE_TSTATES`
   be machine-derived (32 vs 36) per VHDL `zxnext.vhd:2017-2031`?
   Currently hard-coded 32. Would need a machine-type accessor in
   `Z80Cpu` or external setter (cf. `Im2Controller::set_machine_timing_48_or_p3`).

2. **Z80N test runner T-state comparison**: `z80n_test.cpp:243-247`
   has the comparison parked behind a `NOTE`. With the T-state fix
   in this branch, enabling the comparison should now PASS for the
   spec-canonical fixture values. Worth gating on a follow-up
   wave that exposes a per-instruction tstate counter from
   `Z80Cpu::execute()`.

3. **Z80N `LDWS` 14 T-states pre-fix vs spec 14**: this opcode was
   already correct pre-fix, suggesting an inconsistency in how the
   author of the original implementation modeled the M1 prefix —
   some opcodes counted ED-prefix M1 (LDWS, PUSH_NN) and most did
   not. Worth a short audit comment in `z80n_ext.cpp`.

## References

- VHDL: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/cpu/t80n_mcode.vhd`
- VHDL: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/cpu/t80n.vhd`
- Spectrum Next opcode timing: <https://wiki.specnext.dev/Extended_Z80_instruction_set>
- G46(b) investigation: `doc/issues/G46B-INVESTIGATION-LIVE.md`
- G46(b) EOD-23: `doc/issues/g46b-eod23-slide-entry-rambank0-empty.md`
- FUSE Z80 test suite: `test/fuse/`
- Z80N test fixture: `test/z80n/tests.{in,expected}`
