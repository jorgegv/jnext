# Pass-21 CPU + Z80N + IM2 audit — independent review

Reviewer: independent agent off audit HEAD `c8f143e`.
Branch `task2/verify21-cpu-z80n-im2-reviewer`. Did NOT touch source code except
during sandwich verification (immediately reverted).

## Verdict

**APPROVE — no missed bugs.**

Justification:
- V21-IM2-01 fix is VHDL-faithful (correct gate at correct sites, semantically
  matches `i_im2_mode = z80_im_mode(1)`), sandwich-discriminative, and side-
  effect-bounded.
- Every existing-test update is a **legitimate setup correction**, NOT
  enshrinement: each test's invariant is preserved while the precondition is
  made consistent with VHDL fabric semantics.
- 147-row enumeration table is comprehensive (~140 claimed, 145 data rows
  actual); spot-check of 10 rows finds all VHDL-faithful.
- IM2 fabric gate completeness: 4 gate sites = 3 VHDL gate references
  (`o_int_n` :150, S_REQ→S_ACK :112, S_ISR→S_0 :124), with S_ISR→S_0 in both
  legacy `on_reti()` and modern `step_state_machine_with_iei()` paths. No
  missed gate site.
- All baseline test invariants hold: ctest 38/38, FUSE 1356/1356,
  cpu_int_pulse 11/11, cpu_z80n_im2 46/46, ctc_test 132/132,
  ctc_interrupts 30/30, regression 33/0/0.

## Section 1 — row-count + spot-check

Audit claims "~140-row enumeration table".

**Actual count**: 147 lines in `NEXTZXOS-BOOT-SUBSYSTEM-VERIFY21-CPU.md`
table block (header + separator + 145 data rows). Comprehensive: covers all
31 Z80N opcodes, all Z80Cpu integration hooks (M1, INT/NMI dispatch, save/
load), all Im2Controller surfaces (Im2Level enum, DevIdx, DevState, DecState,
tick(), on_reti(), on_retn(), raise/clear/set_*, int_status_mask_c8/c9/ca,
dma_delay accessors, all FSM step methods, save/load), all emulator NR
handlers (0x22, 0xC0, 0xC4-0xCE), and the scheduler/ULA/CTC/UART/DMA wirings.

**Spot-check of 10 rows (chosen to span the fabric)**:

| # | Row | C++ verified | VHDL verified | Match |
|---|---|---|---|---|
| 1 | im2.cpp:598 int_line_asserted gates on `im2_mode_ && im_mode_==2 && device_ieo` | line 606-641 confirmed | im2_device.vhd:150 `o_int_n <= '0' when state=S_REQ and i_iei='1' and i_im2_mode='1'` + zxnext.vhd:1974 wiring | ✓ |
| 2 | im2.cpp:633 ack_vector composes vec, advances to S_ACK | line 643-682 confirmed | im2_device.vhd:111-116 + :155 + zxnext.vhd:1999 vector composition | ✓ |
| 3 | im2.cpp:1007 S_ISR→S_0 gates on `reti_seen_pulse_ && iei && im_mode_==2` | line 1058 confirmed | im2_device.vhd:123-128 i_reti_seen + i_iei + i_im2_mode triple gate | ✓ |
| 4 | im2.cpp:1073 ULA exception pulse gate `!im2_mode_ \|\| (im_mode_!=2)` | line 1110-1115 confirmed | im2_peripheral.vhd:192 `((i_mode_pulse_0_im2_1 AND NOT i_im2_mode) OR (NOT i_mode_pulse_0_im2_1))` — logically equivalent | ✓ |
| 5 | im2.cpp:1079 non-exception pulse gate `!im2_mode_` only | line 1117-1121 confirmed | im2_peripheral.vhd:186 `AND NOT i_mode_pulse_0_im2_1` | ✓ |
| 6 | im2.cpp:872 im2_reset_n gate on `im2_mode_` (V17-CPU-01) | line 904 `const bool im2_reset_n = im2_mode_` | im2_peripheral.vhd:105 `im2_reset_n <= i_mode_pulse_0_im2_1 and not i_reset` | ✓ |
| 7 | z80n_ext.cpp:209 BSRA strict-UB-free shift, sign-fill for shift≥16 | line 209-242 confirmed (explicit shift-zero/shift-16/sign branch) | t80n.vhd:1006-1014 signed-17-bit `shift_right`, bit 16 = sign | ✓ |
| 8 | z80n_ext.cpp:190 BSLA uint32 shift, mask 0xFFFF, shift≥16→0 | line 190-207 confirmed | t80n.vhd:987-993 `shift_left(unsigned 16-bit, B[4:0])` — numeric_std zero-fill | ✓ |
| 9 | im2.cpp:752 IM-mode decode `b4 AND b3` → IM 2 | line 804-808 explicit branch | im2_control.vhd:223-224 `im_mode <= (cpu_opcode(4) and cpu_opcode(3)) & (cpu_opcode(4) and not cpu_opcode(3))` | ✓ |
| 10 | im2.cpp:561 dma_int_pending = `(state≠S_0) AND dma_int_en` OR-reduce | line 569-576 confirmed; **does NOT gate on im_mode_==2** | im2_device.vhd:151 `o_dma_int <= '1' when state /= S_0 and i_dma_int_en = '1'` — also doesn't gate on i_im2_mode | ✓ correctly ungated |

All 10 spot-check rows match VHDL.

## Section 2 — V21-IM2-01 verification

### 2a. VHDL oracle citations confirmed

Read VHDL `zxnext.vhd:1900-2000`, `device/im2_device.vhd:80-161`,
`device/im2_control.vhd:158-240`, `device/im2_peripheral.vhd:95-196`:

- **zxnext.vhd:1974** confirmed: `i_im2_mode => z80_im_mode(1)`.
- **zxnext.vhd:1905** confirmed: `o_im_mode => z80_im_mode` (2-bit "00"/"01"/"10").
- **im2_control.vhd:223-224** confirmed: `im_mode <= (cpu_opcode(4) AND cpu_opcode(3)) & (cpu_opcode(4) AND NOT cpu_opcode(3))`. For `ED 5E` (`opcode=0x5E`, bit4=1, bit3=1) → `im_mode = "10"`. For `ED 7E` (`opcode=0x7E`, bit6=1 not selected by guard `(7:6)="01"`)... wait, audit claims ED 7E decodes IM 2. Let me check the guard.

Re-check guard at VHDL :223:
```
elsif state = S_ED_T4 and ifetch_fe_t3 = '1'
      and cpu_opcode(7 downto 6) = "01"
      and cpu_opcode(2 downto 0) = "110" then
```
For `ED 7E` (opcode = 0x7E = 0111_1110): bits 7-6 = "01" ✓, bits 2-0 = "110" ✓.
Then im_mode encoding: bit4=1, bit3=1 → "10" = IM 2. ✓ Audit claim correct.

For `ED 5E` (opcode = 0x5E = 0101_1110): bits 7-6 = "01" ✓, bits 2-0 = "110" ✓.
bit4=1, bit3=1 → "10" = IM 2. ✓

`z80_im_mode(1)='1'` ⇔ `im_mode = "10"` (most-sig bit set) ⇔ IM 2 only.
Therefore `i_im2_mode='1'` ⇔ Z80 in IM 2, ⇔ `Im2Controller::im_mode_ == 2`.

**The semantic mapping in the fix is correct.**

### 2b. C++ fix sites confirmed

Read `src/cpu/im2.cpp` after the fix at HEAD `c8f143e`:

- **`int_line_asserted()` line 633-634**: added `if (im_mode_ != 2) return false;` after the existing `!im2_mode_` early-out. Matches VHDL :150.
- **`ack_vector()` line 671-672**: added `if (im_mode_ != 2) return 0xFF;` after the existing `!im2_mode_` early-out. Matches VHDL :112.
- **`on_reti()` line 281-289**: added `if (im_mode_ != 2) return;` after the existing `!im2_mode_` early-out. Matches VHDL :124 for the legacy path.
- **`step_state_machine_with_iei()` S_ISR branch line 1058**: extended gate from `reti_seen_pulse_ && iei` to `reti_seen_pulse_ && iei && im_mode_ == 2`. Matches VHDL :124 for the modern path.

The fix correctly applies the gate **only** where VHDL gates on `i_im2_mode`:
- Output to CPU (`o_int_n` aggregate → `int_line_asserted`)
- S_REQ→S_ACK FSM transition (`ack_vector`)
- S_ISR→S_0 FSM transition (both `on_reti` and `step_state_machine_with_iei`)

It correctly does NOT touch sites where VHDL does NOT gate on `i_im2_mode`:
- S_0→S_REQ transition (driven by `int_req && m1_n='1'`, no IM mode gate)
- `o_dma_int` (gates on `state /= S_0` only)
- `int_status` register (latched, no IM mode gate)
- `o_ieo` daisy-chain (driven by state + reti_decode)
- Pulse-mode pulse generation (different gate per :186 / :192)

### 2c. Sandwich verification — DISCRIMINATIVE

Reverted all 4 gate lines (commented them out):
- `im2.cpp:289` `if (im_mode_ != 2) return;` → commented
- `im2.cpp:634` `if (im_mode_ != 2) return false;` → commented
- `im2.cpp:672` `if (im_mode_ != 2) return 0xFF;` → commented
- `im2.cpp:1058` `&& im_mode_ == 2` → commented

Rebuilt and ran `cpu_z80n_im2_regressions_test`:
```
[FAIL] V21-IM2-01-INT-LINE-GATED-ON-IM-MODE-VHDL-150-1974
  im_mode_=0: int_line=1 (post-fix:0; pre-fix:1) vec=0000
  (post-fix:0xFF; pre-fix:0x00) state=2 (post-fix:S_REQ=1; pre-fix:S_ACK=2);
  im_mode_=1: int_line=1 vec=0000 state=2; ...
Total: 46  Passed: 45  Failed: 1
```

**Sandwich-discriminative**: the V21-IM2-01 test specifically detects the
regression. The other 45 tests still pass even with the gate reverted, which
means the new test is **the only** regression catcher — no test was already
covering this case (correct: pre-V21 the bug existed and was undetected).

Restored im2.cpp from backup:
```
[PASS] V21-IM2-01-INT-LINE-GATED-ON-IM-MODE-VHDL-150-1974
Total: 46  Passed: 46  Failed: 0
```

### 2d. Boot-realistic probability

Audit characterizes the bug as a "narrow boot window" (low but non-zero
probability). Reviewer concurs:
- FPGA reset: `z80_im_mode = "00"` AND `nr_c0_int_mode_pulse_0_im2_1 = '0'`.
- Real NextZXOS supervisor executes `ED 5E` early before NR 0xC0 b0 flip.
- The bug fires in: (a) any boot ROM that flips NR 0xC0 b0 first, runs INT-
  enabled code before `ED 5E`; (b) any ISR that flips IM mode mid-handler.
- The audit-noted "asymmetric transition gates" — devices that pre-fix would
  clear during a transient IM=1 window but VHDL would keep latched — is a
  real correctness concern for nested-ISR handlers with IM-mode flips.

The dormancy in production NextZXOS is the reason this surfaced as class-(c)
not class-(b). VHDL-faithful fix posture is correct.

## Section 3 — per-test enshrinement verdict

**CRITICAL section: each updated test scrutinized for legitimacy.**

| # | Test | Update | Verdict |
|---|---|---|---|
| 1 | ctc_test.cpp `fresh(Im2Controller&)` | Adds `on_m1_cycle(0,0xED); on_m1_cycle(1,0x5E);` after `reset()` to drive `im_mode_=2`. | **LEGITIMATE.** Real boot achieves this by executing `ED 5E`; the test now provides the missing precondition for the daisy-chain tests that REQUIRE the device FSM to transition through S_REQ→S_ACK→S_ISR. Without the precondition the tests would test a state real hardware never reaches in productive use. The invariants verified by individual daisy-chain tests (priority encoding, IEI snapshot semantics, RETI clear at decode time, etc.) are unchanged. |
| 2 | ctc_test.cpp PULSE-03 | Calls `fresh()` (which now sets IM 2) then explicitly overrides with `ED 46` (IM 0) to exercise EXCEPTION-pulse "CPU not in IM=2" branch. | **LEGITIMATE.** This test specifically targets `im2_peripheral.vhd:192` EXCEPTION gate's `(i_mode_pulse_0_im2_1 AND NOT i_im2_mode)` arm, which requires NR 0xC0 b0 = 1 AND Z80 NOT in IM 2. The override is essential to exercise this branch — exactly what the test claims to do. Both old and new code expect this branch fires. Test invariant unchanged. |
| 3 | ctc_test.cpp ULA-INT-09 | Same pattern as PULSE-03: override `fresh()`'s IM 2 with IM 0 to test EXCEPTION vs non-EXCEPTION pulse in IM2 mode with CPU NOT in IM 2. | **LEGITIMATE.** Test claims: "EXCEPTION devices fire a pulse even in IM2 mode (provided CPU is not in IM=2)". Per VHDL :192 this is exactly the gate. Test invariant — that ULA pulses, CTC0 does NOT — is preserved. The override is essential to the test's stated goal. |
| 4 | cpu_z80n_im2_regressions Pass-10 nested-ISR | Adds `on_m1_cycle(0,0xED); on_m1_cycle(1,0x5E);` after `reset(); set_mode(true);`. | **LEGITIMATE.** Test name: `test_pass10_im2_reti_decode_simultaneity`. Test verifies that S_REQ→S_ACK in a nested-ISR scenario correctly snapshots IEI at the RETI decode edge. To reach S_REQ→S_ACK at all, VHDL requires `i_im2_mode='1'` (= IM=2). Without the precondition pre-V21 the test passed by accident (gate was missing); post-V21 the precondition is required for the test to reach its checked state. Invariant unchanged. |
| 5 | cpu_z80n_im2_regressions V11-CPU-01 DDFD-ED | Adds `on_m1_cycle(0,0xED); on_m1_cycle(1,0x5E);` after `reset(); set_mode(true);`. | **LEGITIMATE.** Test exercises full lifecycle req→ack→ISR, then verifies that a DDFD prefix followed by ED doesn't trigger a false RETI clear. The full lifecycle requires reaching S_ISR which requires S_REQ→S_ACK gate (`i_im2_mode='1'`). Invariant unchanged. |
| 6 | cpu_z80n_im2_regressions V19-IM2-03 | Adds `on_m1_cycle(0,0xED); on_m1_cycle(1,0x5E);` after `reset(); set_mode(true);`. | **LEGITIMATE.** Test verifies int_unq one-shot semantics across the full lifecycle through S_ISR. Same precondition requirement. Invariant — int_unq cleared after one tick + ISR boundary — unchanged. |
| 7 | cpu_z80n_im2_regressions V19R-CPU-01 | Adds `on_m1_cycle(0,0xED); on_m1_cycle(1,0x5E);` after `reset(); set_mode(true);`. | **LEGITIMATE.** Test verifies int_req 1-cycle pulse synthesis across multiple frames, requires reaching ack_vector and observing S_REQ→S_ACK→S_ISR. Same precondition. Invariant unchanged. |
| 8 | ctc_interrupts ULA-INT-V19-IM2-01 | Adds `emu.im2().on_m1_cycle(0,0xED); emu.im2().on_m1_cycle(1,0x5E);` after `set_mode(true)`. | **LEGITIMATE.** Test verifies that NR 0x22 b1=1 enables LINE int_en via the IM2 fabric path (V19-IM2-01 invariant). Verifies `int_line_asserted` becomes true after raising LINE. Post-V21 the gate also requires `im_mode_==2`, so the test needs to drive the decoder. V19 invariant (NR 0x22 b1 enables LINE) remains the checked property. |
| 9 | ctc_interrupts ULA-INT-V19-IM2-04 | Adds `emu.im2().on_m1_cycle(0,0xED); emu.im2().on_m1_cycle(1,0x5E);` after `nr_write(emu, 0xC0, 0x01);`. | **LEGITIMATE.** Test verifies IntAck path through ack_vector in IM2 mode. The setup notes the test bypasses FUSE Z80 — the IM2-control decoder needs to be driven directly with `ED 5E` since regs.IM=2 alone doesn't update the IM2 decoder's shadow. This is documented in the inline comment. V19 IntAck invariant remains the checked property. |

**Summary: 9/9 test updates are legitimate setup corrections.** None are
enshrinement.

Pattern: every update either (a) provides a missing precondition that real
hardware would have set up via `ED 5E` (cases 1, 4-9), or (b) overrides a
helper-default to exercise a specific VHDL gate branch (cases 2, 3). No test
weakened its invariant; no test now passes that wouldn't have passed on real
hardware in the same setup.

The reviewer specifically asked: "does the new expectation match what VHDL
would produce for the new setup?" For all 9 cases the answer is yes — VHDL
with `z80_im_mode(1)='1'` (= IM=2) produces the same output as the test
expects post-V21.

## Section 4 — adjacent re-audit + gate completeness

Searched `src/cpu/im2.cpp` for all methods that touch the IM2 fabric and
verified gate correctness:

| Site | Gates on `im2_mode_`? | Gates on `im_mode_==2`? | VHDL truth | Verdict |
|---|---|---|---|---|
| `int_line_asserted` | yes | yes (V21 fix) | VHDL :150 gates on both | ✓ |
| `ack_vector` | yes | yes (V21 fix) | VHDL :112 gates on i_im2_mode + i_iei + m1_n + iorq_n | ✓ |
| `on_reti` (legacy) | yes | yes (V21 fix) | VHDL :124 gates on i_im2_mode | ✓ |
| `step_state_machine_with_iei` S_ISR→S_0 | implicit via `!im2_mode_ return` | yes (V21 fix) | VHDL :124 gates on i_im2_mode | ✓ |
| `step_devices` Phase 1 (edge detect + int_status + im2_int_req latch) | yes (via im2_reset_n) | no | VHDL :154-162, :167-178 + :105 — no i_im2_mode gate | ✓ correctly ungated |
| `step_devices` Phase 2 S_0→S_REQ | yes (via early-return at line 991) | no | VHDL :106 — `state_next <= S_REQ when i_int_req='1' and i_m1_n='1'` — no i_im2_mode | ✓ correctly ungated |
| `step_state_machine_with_iei` S_ACK→S_ISR | yes (via early-return) | no | VHDL :117 — `state_next <= S_ISR when i_m1_n='1'` — no i_im2_mode | ✓ correctly ungated |
| `dma_int_pending` | no | no | VHDL :151 — no gates | ✓ correctly ungated |
| `dma_delay` | no | no | VHDL :2001-2010 — no i_im2_mode gate | ✓ correctly ungated |
| `step_pulse` non-EXCEPTION | yes (`!im2_mode_`) | no | VHDL :186 — `AND NOT i_mode_pulse_0_im2_1`, no i_im2_mode | ✓ |
| `step_pulse` EXCEPTION | yes | yes (`im_mode_!=2`) | VHDL :192 — `(i_mode_pulse_0_im2_1 AND NOT i_im2_mode) OR (NOT i_mode_pulse_0_im2_1)` | ✓ |
| `int_status(DevIdx)` (read access) | no | no | VHDL :180 `int_status OR im2_int_req` — no i_im2_mode | ✓ correctly ungated |
| `clear_status(DevIdx)` | no | no | VHDL :152-155 status_clear is unconditional | ✓ correctly ungated |
| `ieo(DevIdx)` | no | no | VHDL :136-146 IEO is state+reti_decode driven | ✓ correctly ungated |

**No missed gate site. No spurious gate addition.**

Z80N 31-opcode spot-check: confirmed 5 opcodes against VHDL t80n.vhd /
t80n_mcode.vhd:
- SWAPNIB (t80n.vhd:702-704) — nibble swap, no F write ✓
- BSLA_DE_B (t80n.vhd:987-993) — V17-Z80N-01a UB-free shift ✓
- BSRA_DE_B (t80n.vhd:1006-1014) — V17-CPU-NIT-04 UB-free sign-fill shift ✓
- BSRF_DE_B (t80n.vhd:1006-1014) — V17-Z80N-01b UB-free fill-1 shift ✓
- PUSH_NN (t80n_mcode.vhd:1928,1938) — MEMPTR-lo only ✓

All match VHDL.

## Section 5 — test invariants

| Suite | Audit claim | Reviewer observed | Status |
|---|---|---|---|
| ctest (38 suites) | 38/38 | 38/38 | ✓ |
| FUSE Z80 (1356 opcodes) | 1356/1356 | 1356/1356 | ✓ |
| cpu_int_pulse_test | 11/11 | 11/11 | ✓ |
| cpu_z80n_im2_regressions_test | 46/46 | 46/46 | ✓ |
| ctc_test | 132/132 | 132/132 | ✓ |
| ctc_interrupts_test | 30/30 | 30/30 | ✓ |
| regression.sh | 33/0/0 | 33/0/0 | ✓ |

All invariants hold.

## Section 6 — Pass-19 + Pass-20 fix re-verification (audit's own claim)

The audit claims it re-verified all P19/P20 fixes and found them clean.
Reviewer spot-checked a few:
- V19-IM2-01 (NR 0x22 b1 + NR 0xC4 b1 → LINE int_en): emulator.cpp:1887 +
  :2766 confirmed writers + init seed at emulator.cpp:261 (false reset).
- V19-IM2-02 (port_ff_reg(6) → ULA int_en): three writers verified at
  emulator.cpp:1887, :2766, :3327.
- V20-IM2-01 (pulse-mode CPU /INT poll falling-edge): emulator.cpp:5857-5862
  verified, persistence at :6807 + load at :7327.
- V20R-CPU-NIT-02 (legacy ULA/LINE request_interrupt callbacks dropped):
  grep `request_interrupt` in src/ returns only the implementation at
  z80_cpu.cpp:925 and the 2 poll sites at emulator.cpp:5790 + :5860.

Re-verification holds.

## Final HEAD SHA

Reviewer branch HEAD: `c8f143e` (no commits added by reviewer except this
review document).

## Recommendation

Accept Pass-21 audit. Convergence proceeding for CPU + Z80N + IM2 subsystem.
Pass-22 may launch with V21-IM2-01 added to the closed-bugs list.
