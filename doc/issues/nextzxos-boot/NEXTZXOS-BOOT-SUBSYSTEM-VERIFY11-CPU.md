# NEXTZXOS Boot Subsystem — Pass-11 BLIND Audit (CPU / Z80N / IM2)

**Worktree**: `.claude/worktrees/task2-verify11-cpu-z80n-im2`
**Branch**: `task2/verify11-cpu-z80n-im2` (off integration HEAD `d385d5e`)
**Scope**: `src/cpu/z80_cpu.{cpp,h}`, `src/cpu/z80n_ext.{cpp,h}`,
`src/cpu/im2.{cpp,h}`, `src/cpu/im2_client.h`, `third_party/fuse-z80/`
glue, INT/NMI delivery from `src/core/emulator.cpp` to `Z80Cpu`,
contention emission from CPU side.
**Methodology**: VHDL oracle (`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`)
for FPGA-derived behaviour; FUSE Z80 (`third_party/fuse-z80/`) +
`fuse_z80_test` for base-Z80 instruction semantics. **BLIND**: no prior
pass reports read.

## Summary

| Class | Count |
| ----- | ----- |
| (a)   | 0 |
| (b)   | 0 |
| (c)   | 2 |
| (d)   | 0 |
| **Total** | **2** |

Tests at HEAD post-fix:
- `ctest --test-dir build`: 38/38 pass.
- `fuse_z80_test`: 1356/1356 pass (no regression in base-Z80 semantics).
- `cpu_z80n_im2_regressions_test`: 25/25 pass (added 3 new
  discriminative tests — see V11-CPU-01 and V11-CPU-02 below).
- `regression.sh`: 32/1 (only pre-existing `parallax-demo` 44636-pixel
  diff inherited from baseline `d385d5e`; the test fails identically
  with and without the V11-CPU fixes — confirmed by
  stash-revert-rebuild-rerun).

## Defense paragraph (areas verified clean)

The audit walked every Z80N case in `execute_z80n()` against
`t80n.vhd:702-1039` and `t80n_mcode.vhd`, the IM2 fabric in
`im2.cpp` against `im2_control.vhd`, `im2_device.vhd`,
`im2_peripheral.vhd`, and `peripherals.vhd`, and the wrapper plumbing
in `Z80Cpu::execute()` against the FUSE Z80 reference plus VHDL
INT/NMI ack semantics. Cleanly verified items include:

* **IM-mode decoder bit-pattern** (`im2.cpp:666-676` vs
  `im2_control.vhd:218-227`): the 2-bit encoding `(b4 AND b3) &
  (b4 AND NOT b3)` collapses correctly to {IM0, IM1, IM2} for the
  eight matching ED opcodes (46/4E/56/5E/66/6E/76/7E).
* **RETI/RETN/IM decoder FSM** (`im2.cpp:638-720` vs
  `im2_control.vhd:158-209`): all eight states plus the SRL "DMA-
  interruption guard" pair are present and transition identically
  AFTER the V11-CPU-01 fix (see Findings).
* **IM2 vector composition** (`im2.cpp:1008-1028` vs
  `zxnext.vhd:1999`): bits 7:5 = `nr_c0_im2_vector[2:0]`,
  bits 4:1 = device index, bit 0 = '0'.
* **IM2 daisy-chain IEO** (`im2.cpp:1030-1054` vs
  `im2_device.vhd:136-146`): S_0 passes IEI through, S_REQ gates on
  `reti_decode`, S_ACK/S_ISR force IEO=0.
* **IM2 RETI-decode simultaneity** (Pass-10 fix preserved at
  `im2.cpp:211, 784`): the pre-edge VHDL view of the FSM correctly
  treats `reti_seen pulse` as implying `reti_decode='1'` for the IEI
  snapshot (necessary for nested-ISR S_ISR→S_0 clears).
* **NR 0xC8/C9/CA mask packing** (`im2.cpp:316-364` vs
  `zxnext.vhd:6248-6254`): bit-for-bit faithful, including the UART
  RX duplication into both "near-full" and "avail" positions.
* **Pulse-mode `pulse_count_end` formula** (`im2.cpp:957-963` vs
  `zxnext.vhd:2033`): bit5 AND (machine_48 OR machine_p3 OR bit2)
  yielding 32-cycle window for 48K/+3 and 36-cycle for 128K/Pentagon/
  Next-default.
* **Pulse fabric `o_pulse_en` per-device gate** (`im2.cpp:915-938` vs
  `im2_peripheral.vhd:184-194`): non-exception devices fire only in
  pulse mode; ULA (the only `EXCEPTION='1'` device per
  `zxnext.vhd:1964`) fires in pulse mode always or in IM2 mode when
  the CPU isn't in IM=2.
* **Z80N M1 contention** (`z80_cpu.cpp:579-643`): two `contend_read()`
  per Z80N opcode (ED prefix M1 + extended-byte M1) routes through
  `ContentionModel::contention_tick()` via the G141 CORETEST
  function-override path. Operand reads/writes use
  `fuse_z80_readbyte`/`writebyte`/`readport`/`writeport` for
  contention parity (Pass-6).
* **Z80N opcode coverage table** (`z80_cpu.cpp:282-316`): all 31
  Z80N opcodes recognised; LOOP (ED FB) treated as NOP-equivalent
  per the FPGA "not implemented" note.
* **Q register hygiene at Z80N dispatch** (`z80_cpu.cpp:609-610`):
  `z80.q = 0` and `z80.iff2_read = 0` at the top of every Z80N case,
  matching FUSE's `fuse_z80_execute_one()` invariant. Z80N opcodes
  that DO write F (TEST, ADD HL/DE/BC,A, all LDIX-family + LDWS +
  LDPIRX) update Q with the new F at exit.
* **IncDecZ shadow latch** (`z80_cpu.cpp:665-668, 793-804`,
  `z80n_ext.cpp:550, 654, 705, 754, 818, 878`): every BC-decrementing
  block transfer (LDI/LDD/LDIR/LDDR/CPI/CPD/CPIR/CPDR + Z80N LDIX/
  LDDX/LDIRX/LDDRX/LDPIRX/LDIRSCALE) and DJNZ updates the shadow per
  `t80n.vhd:1361-1367`. LDWS reads it via the I_BC/I_BT P override.
* **Block-transfer flag composition** (`z80n_ext.cpp:69-78`,
  `ldi_family_flags`): F.X = ALU_Q[3], F.Y = ALU_Q[1], F.H=0, F.N=0,
  F.P = (BC != 0), C/Z/S preserved — matches `t80n.vhd:1277-1289`
  I_BT block plus the ALU ADD-flag block.
* **LDPIRX flag composition** (Pass-10 fix at `z80n_ext.cpp:820-830`):
  ALU_Q = B(post-dec) | bytetemp via OR (default ALU_Op="0110" at
  MCycle 3); I_BT/I_BC override F.X/F.Y/F.H/F.N/F.P, S/Z/C
  preserved.
* **ADD HL/DE/BC,A force-clear F.C** (Pass-10 fix at
  `z80n_ext.cpp:262-296`): VHDL `t80n.vhd:778-783` truncates the
  16-bit add at bit 15, leaving `reg_temp_t(16)=0` from the TState=3
  pre-clear; F.C is therefore unconditionally cleared regardless of
  the actual carry. Spec wiki says "no flags affected" but VHDL
  oracle wins per CLAUDE.md.
* **PUSH NN MEMPTR end-state** (Pass-8 fix at `z80n_ext.cpp:373-374`):
  WZ-lo = ll (LDZ at MCycle 3 overwrites the MCycle-1 hh capture);
  WZ-hi unchanged (LDW never asserted).
* **JP (C) PC composition** (`z80n_ext.cpp:500-505`): PC[15:14]
  preserved, PC[13:6] = port_byte, PC[5:0] = 0 — matches
  `t80n.vhd:980-983`.
* **JP (C) T-state count** (Pass-8 fix at `z80n_ext.cpp:504`): 12T
  per VHDL `t80n_mcode.vhd:1839-1840` (2 × 4T-MCycles + 4T ED prefix
  M1), not the 13T quoted by the Z80N spec wiki. CLAUDE.md mandates
  VHDL oracle.
* **NEXTREG_NN / NEXTREG_A T-states**
  (`z80n_ext.cpp:403-422, 425-438`): 20T / 17T. NextReg writes
  bypass `fuse_z80_writeport` (= 4T per port write) per VHDL
  `t80n_mcode.vhd:1672-1707` (NextReg fabric is direct-driven via
  `Z80N_data_o` strobes, no IORQ on the external pin).
* **DD/FD/CB/ED chained-prefix M1 callback delivery**
  (`z80_cpu.cpp:719-754`): walks the prefix chain, fires
  `on_m1_cycle` for every prefix byte plus the final inner opcode,
  matches FUSE re-dispatch shape (each `case 0xdd/0xfd/0xed/0xcb`
  does one extra `contend_read(PC,4)`). DD CB d op / FD CB d op
  correctly stops M1 callbacks at CB (the d byte and op byte are 3T
  data reads, not M1).
* **NMI HALT exit** (`z80_cpu.cpp:425`): when `z80.halted`, the
  saved PC is `pc + 1` (skips the HALT opcode), matching
  `fuse_z80_nmi()` and the Z80 specification.
* **NMI IFF state** (`fuse_z80_core.c:165-178`): IFF1 ← 0,
  IFF2 unchanged, +5T extended M1, push PC. Matches the Zilog
  manual.
* **INT IFF state** (`fuse_z80_core.c:118-163`): IFF1 ← IFF2 ← 0,
  +7T extended M1, push PC, IM2 vector read via `readbyte` (3T
  + contention per byte). Matches the Zilog manual.
* **EI-grace gate** (`z80_cpu.cpp:479-510`, Pass-8 fix): the gate
  is checked BEFORE invoking `on_int_ack()` so the IM2 daisy-chain
  device is not falsely advanced from S_REQ to S_ACK when FUSE
  rejects the cycle.
* **`on_int_ack()` byte-identical fallback** (`z80_cpu.cpp:495`):
  when the callback is unset (FUSE Z80 test harness path), uses the
  legacy `int_vector_` member, preserving 1356/1356.
* **R register increment for Z80N** (`z80_cpu.cpp:586`): +2 (one
  per M1 cycle for ED prefix + ext byte). Non-Z80N ED handled by
  FUSE's R increment in `case 0xed:` (`opcodes_base.c:1075`).
* **VHDL device priority order** (`zxnext.vhd:1933-1944`): jnext
  `Im2Controller::DevIdx` (LINE=0, UART0_RX=1, UART1_RX=2, CTC0..7
  = 3..10, ULA=11, UART0_TX=12, UART1_TX=13) matches the VHDL
  device-vector index (priority and vector value both = `I-1`).
* **MEMPTR persistence in save/load** (Pass-3 fix): MEMPTR + Q +
  `interrupts_enabled_at` + `iff2_read` are all serialised, no
  hidden Z80 state lost across snapshot boundaries.
* **Magic-breakpoint paths** (`z80_cpu.cpp:524-535, 673-683`):
  ED FF and DD 01 act as 2-byte NOPs when triggered; do NOT fire
  M1 callbacks (decoder FSM correctly stays in S_0); R is NOT
  incremented (matches the "skip the instruction" semantic).
* **PIXELAD H/L composition** (`z80n_ext.cpp:464-472` vs
  `t80n.vhd:939-947`): H = "010" & D[7:6] & D[2:0],
  L = D[5:3] & E[7:3] — bit-for-bit faithful.
* **SETAE bit decode** (`z80n_ext.cpp:476-482` vs
  `t80n.vhd:923-937`): `A = 0x80 >> (E[2:0])`.
* **TEST n flag composition** (`z80n_ext.cpp:171-185`): VHDL
  default `ALU_Op="0100"` (AND) at MCycle 2 of the TEST opcode
  emits H=1, X=Q[3], Y=Q[5], Z=(Q==0), S=Q[7], P=parity(Q), C=N=0.
  The lack of `Read_To_Reg` means A is preserved; only F is
  written.
* **OUTINB T-state count** (`z80n_ext.cpp:399-400`): 16T
  (8 M1 + 3 read HL + 4 port write + 1 internal). B is NOT
  decremented (unlike OUTI/OUTD), per `t80n_mcode.vhd:2521,2534`.
* **BSRA/BSRL/BSRF fill bit semantics** (`z80n_ext.cpp:198-225`
  vs `t80n.vhd:1001-1020`): IR(0)=0 for BSRL, IR(0)=1 for BSRF,
  reg_temp_t(16)=reg_temp_t(15) for BSRA (sign-extend). C++
  `int >> shift` of integer-promoted `int16_t` yields arithmetic
  shift on x86_64; for `shift in [16, 31]` the high-bits behaviour
  matches the VHDL `shift_right(signed(reg_temp_t(16:0)), n)`
  full-fill-of-bit-16 pattern.
* **BRLC mod-16 rotation** (`z80n_ext.cpp:227-236`): `rot &= 0x0F`
  prior to shift correctly mod-16's the rotation amount per
  IEEE.numeric_std `rotate_left(unsigned(...), n)` semantics on
  16-bit vectors.

## Investigated-and-cleared (no finding)

These angles were explicitly checked and matched the oracle:

* **BSLA/BSRL UB on 16+ shift**: signed-integer left-shift into the
  sign bit is C++14+ UB, but `regs.DE << shift` runs through
  integer-promotion to 32-bit `int`; for `shift ∈ [0, 31]` and
  `DE ∈ [0, 0xFFFF]` the result fits in `int32_t` without
  signed-overflow UB (DE bits live in positions [shift, shift+15]
  ≤ 31). The result is masked with `& 0xFFFF` for shifts ≥ 16,
  yielding 0 — VHDL-faithful. Not a bug on the host platform.
* **`int_pulse_tstates > vs >=` off-by-one**
  (`z80_cpu.cpp:453, 451`): the legacy `int_pending_` expiry path
  uses `> int_pulse_tstates`, fires at delta = 33/37 instead of the
  VHDL-precise 32/36. But this path is only used when `on_int_ack`
  is NOT installed (FUSE Z80 test harness), and even there it
  affects behaviour only when IFF1=0 (the gate `&& !z80.iff1`).
  Production IM2 path drives the pulse window via
  `Im2Controller::step_pulse()`, which is bit-for-bit VHDL-faithful.
  Off-by-one in a non-production path is below the audit
  threshold.
* **Idle pulse_count divergence**: VHDL resets `pulse_count` every
  rising-edge while `pulse_int_n='1'`; jnext leaves
  `pulse_count_` stale until the next pulse starts (then sets it
  to 0). External observability is identical (the next pulse always
  starts at 0); no test reads `pulse_count_` at idle. Internal-state
  visibility only.
* **`int_line_asserted()` not gated by Z80 IM=2**
  (`im2.cpp:512`): VHDL `o_int_n` from `im2_device` requires
  `i_im2_mode='1'` (= Z80 IM=2). jnext checks only `im2_mode_`
  (= NR 0xC0 fabric enable). However, the bridge from
  `int_line_asserted()` to `Z80Cpu::int_pending_` does not exist
  (it's the pre-existing class-(d) escalation noted in earlier
  passes); the helper is only consumed by tests, where the Z80 IM
  is set as part of the fixture. No new finding.
* **IM2 vector returned 0xFF when no S_REQ device**
  (`im2.cpp:548`): VHDL OR-reduction of 14 device `o_vec` lines
  yields 0 when none is S_ACK, and `im2_vector` is then
  `nr_c0_im2_vector & 0 & 0`. jnext returns 0xFF instead of
  `(nr_c0 << 5)`. But `ack_vector()` is only called when
  `int_pending_` is true; the IM2 path doesn't currently raise
  `int_pending_` for spurious bus events (= the same class-(d)
  bridge). No observable difference in production paths.
* **Magic-breakpoint M1 callback omission** for ED FF / DD 01:
  the IM2 RETI decoder doesn't see the ED prefix when the magic
  breakpoint fires. Decoder stays in S_0; the next non-magic-
  breakpoint M1 starts cleanly. Behavioural parity with "the
  instruction never executed" is the documented intent.
* **IM2 `state_next = S_ACK` vector window**: jnext computes
  vector AFTER setting `state = S_ACK`, so `compute_vector()`
  finds the right device. The VHDL `o_vec <= i_vec when state =
  S_ACK or state_next = S_ACK` covers both edges of the ACK
  transition; the synchronous-update model in `ack_vector()`
  collapses both into the same observable byte.
* **NMI HALT-fix interaction with `on_nmi_servicing`**
  (`z80_cpu.cpp:425-428`): the saved PC computed BEFORE
  `fuse_z80_nmi()` is the post-HALT-fix PC, which is the value
  pushed to stack. NR 0xC2/0xC3 (NMI return-address shadow)
  therefore latches the correct value.

## Findings

### V11-CPU-01 — IM2 RETI decoder treats `DD ED 4D` as RETI (class-c)

**Class**: (c) — observable on a rare bus pattern, not boot-blocking.

**Files**: `src/cpu/im2.cpp` (`advance_decoder` S_DDFD_T4 branch).
**VHDL oracle**: `im2_control.vhd:199-206`.

**Bug**: jnext's IM2 RETI decoder transitioned `S_DDFD_T4 → S_ED_T4`
on an ED byte after a DD/FD prefix. Per VHDL line 199-206 the
S_DDFD_T4 fall-through is `state_next <= S_0` for **any** non-DDFD
opcode (the `elsif ifetch_fe_t3='1' then state_next <= S_0;` at
line 202-203 has no ED special-case). The IM2 control block keys on
the *physical bus opcode pattern*, not on the CPU's internal re-
dispatch behaviour. So `DD ED 4D` on the bus does NOT register as
RETI in the VHDL fabric — but jnext was emitting a spurious
`reti_seen` pulse on the 4D, clearing S_ISR daisy-chain devices that
VHDL would leave standing.

**Reproduction**: a CTC0 device latched into S_ISR. Drive the
decoder with bytes DD, ED, 4D in sequence. Pre-fix: CTC0 transitions
to S_0 (wrong — RETI was never valid through the DDFD chain). Post-
fix: CTC0 stays in S_ISR.

**Why class-(c)**: assemblers emit bare `ED 4D` for RETI, not
`DD ED 4D`. The bug only fires on hand-crafted byte sequences or on
a stray DD before an actual RETI in unusual code. No NextZXOS boot
path observed to depend on this.

**Fix**: remove the `else if (opcode == 0xED) dec_state_ =
DecState::S_ED_T4;` branch from `Im2Controller::advance_decoder`
S_DDFD_T4 case; ED, CB, or any other opcode after DDFD all return
to S_0.

**Test**: `test_v11_cpu_01_im2_ddfd_ed_no_reti` in
`test/cpu/cpu_z80n_im2_regressions_test.cpp`.

**Discriminative confirmation**: stash-revert-rebuild-rerun of
`src/cpu/im2.cpp` yields the test FAILing (CTC0 ends in S_0 with
reti_seen=true). Applying the fix restores PASS (CTC0 stays in
S_ISR, no reti_seen).

### V11-CPU-02 — PIXELDN corrupts H[7:5] when band counter wraps from 11 (class-c)

**Class**: (c) — observable divergence on a rare boundary, not
boot-blocking, no FUSE Z80 test impact.

**Files**: `src/cpu/z80n_ext.cpp` (PIXELDN case).
**VHDL oracle**: `t80n.vhd:900-921`.

**Bug**: PIXELDN performed the row-counter increment in four steps
(low-counter, mid-counter, band-counter, screen-prefix), with the
band-counter increment implemented as `H = H + 0x08;`. When
`H[4:3] = 11` (= band 3, off-screen) and the increment fires (i.e.
the lower counters have just rolled over from "111 111"), the +0x08
on H propagates a carry from H[4] into H[5], corrupting the
preserved screen-prefix field H[7:5]. Per VHDL, PIXELDN is a single
8-bit add `(b & R & C) + 1` truncated at bit 7 — the band carry-out
is lost, so H[7:5] is preserved verbatim (line 914:
`reg_direct_val_H_b <= reg_temp_t(31 downto 29) & ...`).

**Reproduction**: HL = 0x5FE0
- H = 0x5F (010 11 111): H[7:5]="010", b=11, C=111
- L = 0xE0 (111 00000): R=111, L[4:0]=00000

Composite "11 111 111" + 1 = "00 000 000" (8-bit overflow).
- Pre-fix: jnext yields HL=0x6000 (H[7:5] flipped to "011")
- Post-fix: HL = 0x4000 (H[7:5]="010" preserved per VHDL)

**Fix**: Replace the 4-step carry chain with a single 8-bit
composite add and re-extraction. The composite at bits 7:6 (band),
5:3 (row-in-band), 2:0 (cell-row) is `+ 1` with C truncation, then
distributed back. H[7:5] is preserved by ANDing with 0xE0 before OR-
ing in the new band/cell bits.

**Test**: `test_v11_cpu_02_pixeldn_band3_wrap_preserves_h_high`
(plus the regression-guard `_pixeldn_row191_wrap_unchanged`).

**Discriminative confirmation**: stash-revert-rebuild-rerun of
`src/cpu/z80n_ext.cpp` reproduces HL = 0x6000 (test FAIL); applying
the fix restores HL = 0x4000 (test PASS).

**Existing PIXELDN fixtures** in `test/z80n/tests.in/expected`
(ed93_basic, ed93_col_wrap, ed93_third_wrap, ed93_first_to_second,
ed93_second_to_third, ed93_last_line_wrap) all continue to pass —
verified by running `./build/test/z80n_test` before and after the
fix (85/85 in both cases).

## Class-(d) escalations (NOT fixed)

None new in this pass. Pre-existing class-(d) escalations from
earlier passes (CPU IM2 controller bridge to Z80Cpu, in particular)
remain pending user authorization per the project rules and were
neither attempted nor re-classified here.

## Test results

```
$ ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 38

$ ./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0

$ ./build/test/cpu_z80n_im2_regressions_test
Total:   25  Passed:   25  Failed:    0
(was 22; +3 for V11-CPU-01 + V11-CPU-02 + guard)

$ ./build/test/z80n_test
Total:   85  Passed:   85  Failed:    0  Skipped:    0

$ bash test/00regression/regression.sh
Pass: 32  Fail: 1  Skip: 0
(only failure: parallax-demo 44636-pixel diff — pre-existing in
baseline d385d5e, fails identically with and without the V11-CPU
fixes)
```

## Commit

`fix(task2-verify11-cpu): pass-11 audit — V11-CPU-01 (IM2 DDFD-ED no
RETI, class-c) + V11-CPU-02 (PIXELDN preserves H[7:5] on band-3
wrap, class-c); 3 disc tests; report`
