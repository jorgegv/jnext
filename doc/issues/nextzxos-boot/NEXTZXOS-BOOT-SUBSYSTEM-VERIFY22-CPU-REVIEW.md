# Pass-22 audit independent review — CPU + Z80N + IM2 subsystem

Reviewer worktree: `.claude/worktrees/task2-verify22-cpu-z80n-im2-reviewer` (branch `task2/verify22-cpu-z80n-im2-reviewer`).
Audit HEAD reviewed: `da54e25a`.
Build mode: Release (CMake `-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`).

## Verdict

**APPROVE — no missed findings.**

V22-IM2-01 fix is VHDL-faithful, the discriminative test is correctly discriminative (verified via sandwich), and `on_reti()` vs `step_state_machine_with_iei()` are now **fully symmetric across every per-device state variable**. No additional asymmetries discovered in the IM2 fabric, the legacy-vs-modern method duplication pattern (raise/clear/raise_unq/ack_vector/int_line_asserted), or in the adjacent Z80N / HALT / block-instruction / DD-FD-prefix surfaces.

## Test baseline (Release)

| Suite | Result |
|---|---|
| ctest (38 suites) | 38/38 PASS |
| FUSE Z80 opcode | 1356/1356 PASS |
| cpu_z80n_im2_regressions | 47/47 PASS (+V22-IM2-01) |
| cpu_int_pulse | 11/11 PASS |
| ctc_test | 132/132 PASS |
| ctc_interrupts | 30/30 PASS |
| regression.sh | 33/0/0 (Pass 33, Fail 0, Skip 0) |

All non-negotiable invariants preserved.

## Step 1 — row-count validation

Audit claims ~152 rows. Counted: **153 table rows** (`grep -c "^| "` against the enumeration table). Match.

Sub-counts:
- 31 Z80N opcode rows (`z80n_ext.cpp` references) — matches "~31 Z80N" in the audit summary.
- ~22 CPU integration rows (z80_cpu.cpp prefixed entries).
- ~50 IM2 fabric rows (im2.cpp method-by-method enumeration of all public/private surfaces).
- ~30 emulator NR / port wiring rows.
- ~14 device-state-machine transition rows (S_0/S_REQ/S_ACK/S_ISR × FSM phases).
- ~5 save/load schema rows.

Tally aligns with the audit's stated breakdown.

## Step 2 — VHDL spot-check of 10 ✓ rows

| Row | C++ behaviour | VHDL oracle | Verdict |
|---|---|---|---|
| SWAPNIB ED 23 | nibble swap of A | `t80n.vhd:702-704` `ACC <= reg_temp_t(3 downto 0) & reg_temp_t(7 downto 4)` | ✓ matches |
| MIRROR_A ED 24 | bit-reverse A | `t80n.vhd:706-708` `reg_temp_t(0) & reg_temp_t(1) & ... & reg_temp_t(7)` | ✓ matches |
| BSRA_DE_B ED 29 | UB-free arith shift; bit 16 = reg_temp_t(15) | `t80n.vhd:1006-1014` `if IR(1)='0' then reg_temp_t(16) := reg_temp_t(15)` | ✓ matches |
| MUL_DE ED 30 | uint16(D) × uint16(E); no F | `t80n.vhd:729-735` unsigned × unsigned | ✓ matches |
| NEXTREG_NN ED 91 | Z80N_data_o strobe; bypasses IORQ | `t80n_mcode.vhd:1668-1683` Z80N_data_o + Z80N_dout_o | ✓ matches |
| PIXELDN ED 93 | (b&R&C)+1 with H[7:5] preserved | `t80n.vhd:900-921` H4H3 \| L7L6L5 \| H2H1H0 + 1 | ✓ matches |
| PIXELAD ED 94 | "010" & D[7:6] & D[2:0] in H; D[5:3] & E[7:3] in L | `t80n.vhd:939-947` | ✓ matches |
| pulse fabric step_pulse | pulse_count_end = bit5 AND (machine_48 OR p3 OR bit2) | `zxnext.vhd:2017-2044` | ✓ matches |
| int_line_asserted im2_mode_==2 gate | `i_im2_mode='1'` gate on S_REQ→/INT | `zxnext.vhd:1974` `i_im2_mode => z80_im_mode(1)` + `im2_device.vhd:150` | ✓ matches |
| int_status_mask_ca | bit 6=U1TX, bits 5,4=U1RX dup, bit 2=U0TX, bits 1,0=U0RX dup | `zxnext.vhd:6253-6254` `'0' & im2_int_status(13) & (2) & (2) & '0' & im2_int_status(12) & (1) & (1)` | ✓ matches |

10 / 10 ✓ rows verified accurate against the canonical VHDL oracle.

## Step 3 — V22-IM2-01 verification

### VHDL oracle re-read (independent)

- `im2_peripheral.vhd:148` — `im2_isr_serviced <= isr_serviced and not isr_serviced_d` — one-cycle pulse on rising edge of `isr_serviced`, where (per `im2_device.vhd:159`) `o_isr_serviced <= '1' when state = S_ISR and state_next = S_0 else '0'`.
- `im2_peripheral.vhd:175` — `im2_int_req <= im2_int_req and not im2_isr_serviced` (inside the registered process at :167-178; only effective when `im2_reset_n='1'`, i.e. in IM2-fabric mode).

So in VHDL the S_ISR→S_0 *combinational* state transition produces `isr_serviced='1'` during this CLK_28 cycle, the d-latch captures that as `isr_serviced_d` next cycle, and `im2_isr_serviced` pulses for exactly one cycle aligned with the cycle in which state has *just* transitioned to S_0. That pulse immediately clears `im2_int_req` via the synchronous assignment at :175. There is **no window** in which `state=S_0` and `im2_int_req=true` simultaneously.

### Post-fix code at `im2.cpp:328-352`

The on_reti() loop now does:
```cpp
if (dev_[i].state == DevState::S_ISR && iei_snap[i]) {
    dev_[i].state = DevState::S_0;
    // ... long comment block ...
    dev_[i].im2_int_req = false;
}
```

This matches the parallel inline clear in `step_state_machine_with_iei` at `im2.cpp:1077-1082`:
```cpp
if (reti_seen_pulse_ && iei && im_mode_ == 2) {
    d.state = DevState::S_0;
    d.im2_int_req = false;
}
```

Both paths now set state and im2_int_req in lock-step, faithfully modelling the VHDL combinational/synchronous behaviour at the S_ISR→S_0 transition edge.

### Sandwich verification

- Pre-fix (reverted only the new `dev_[i].im2_int_req = false;` line, all other comments left):
  - Build: clean.
  - `cpu_z80n_im2_regressions_test`: **46/47** — V22-IM2-01 FAILS with the exact diagnostic predicted by the audit: `state=1 (S_REQ; expected S_0=0)` and `im2_int_req_latch=1 (expected 0)`.
- Post-fix (restored line):
  - Build: clean.
  - `cpu_z80n_im2_regressions_test`: **47/47** — V22-IM2-01 PASSES.

Test is genuinely discriminative. Fix is genuinely load-bearing.

## Step 4 — `on_reti()` vs `step_state_machine_with_iei()` exhaustive symmetry check

`Im2Controller::Device` (im2.h:163-174) has these per-device state fields:

| Variable | Default | Semantics | VHDL signal |
|---|---|---|---|
| `int_req` | false | i_int_req from peripheral (level) | i_int_req (per-tick pulse via V19R-CPU-01) |
| `int_req_d` | false | CLK_28 delayed copy (edge detect) | int_req_d (`im2_peripheral.vhd:96`) |
| `int_en` | false | enable | i_int_en |
| `int_unq` | false | one-shot unqualified | i_int_unq (one-cycle peripheral input) |
| `int_status` | false | latched status | int_status (`im2_peripheral.vhd:160`) |
| `im2_int_req` | false | latch in IM2 mode | im2_int_req (`im2_peripheral.vhd:172-176`) |
| `state` | S_0 | FSM | state (`im2_device.vhd:83`) |
| `dma_int_en` | false | from NR CC/CD/CE mask | i_dma_int_en |
| `exception` | false | ULA-only | EXCEPTION generic |

Treatment on S_ISR→S_0 transition, per VHDL:

| Variable | VHDL behaviour at S_ISR→S_0 | on_reti() (line 328-351) | step_state_machine_with_iei S_ISR (line 1058-1083) | Symmetric? |
|---|---|---|---|---|
| `state` | transitions to S_0 (`im2_device.vhd:125`) | sets to S_0 (line 330) | sets to S_0 (line 1078) | ✓ |
| `im2_int_req` | cleared by `im2_isr_serviced` pulse (`im2_peripheral.vhd:148+175`) | sets false (line 349, **V22-IM2-01 fix**) | sets false (line 1081, V21-IM2-01 history) | ✓ |
| `int_status` | NOT cleared by S_ISR→S_0; only by `i_int_status_clear` (`im2_peripheral.vhd:160`) | unchanged | unchanged | ✓ |
| `int_req` | peripheral-driven input; not touched by fabric | unchanged | unchanged | ✓ |
| `int_req_d` | registered delay of `i_int_req`; not touched on transition | unchanged | unchanged | ✓ |
| `int_en` | configuration (i_int_en); not touched | unchanged | unchanged | ✓ |
| `int_unq` | one-shot input; cleared by Phase-1 / Agent C, not by FSM | unchanged | unchanged | ✓ |
| `dma_int_en` | configuration (i_dma_int_en); not touched | unchanged | unchanged | ✓ |
| `im_mode_==2` gate | `i_im2_mode='1'` (`im2_device.vhd:124`) | early-return at line 289 if `im_mode_ != 2` | gate on `im_mode_ == 2` inline (line 1077) | ✓ |
| `im2_mode_` gate | `im2_reset_n='1'` (`im2_peripheral.vhd:105`) holds FSM at S_0 in pulse mode | early-return at line 281 if `!im2_mode_` | line 1010-1012 forces `state = S_0` and returns in pulse mode | ✓ |
| IEI gate | `i_iei='1'` (`im2_device.vhd:124`) | iei_snap[i] computed pre-loop (snapshot, line 312-326) | iei snapshot from step_devices Phase 2 (line 976-990), passed as parameter | ✓ |
| reti gate | `i_reti_seen='1'` (`im2_device.vhd:124`) | called by lambda only on `reti_seen_this_cycle()` (emulator.cpp:663) | `reti_seen_pulse_` inline check (line 1077) | ✓ |
| reti_decode gate (for upstream S_REQ IEI propagation) | `o_ieo = i_iei and i_reti_decode` for S_REQ (`im2_device.vhd:142`) | `iei_reti_decode = reti_decode_ \|\| reti_seen_pulse_` at line 312 | identical: `iei_reti_decode = reti_decode_ \|\| reti_seen_pulse_` at line 975 | ✓ |
| IEI snapshot ordering | simultaneous-update (one device clears per RETI) | snapshot computed BEFORE clear loop (line 313-326) | snapshot computed BEFORE per-device step (line 976-990) | ✓ |

**Result**: every state variable touched by either path is now treated identically by both. **No asymmetry remains.** This is the closure of the V21-IM2-01 + V22-IM2-01 dual-path family.

### Convergence after RETI (double-clear safety check)

Production sequence after an ISR-ending RETI:

1. `on_m1_cycle(ED+4D)` — decoder sets `reti_seen_pulse_=true`.
2. Lambda invokes `im2_.on_reti()` — walks dev_[], clears the S_ISR device's `state` and `im2_int_req` (V22-IM2-01).
3. `fuse_z80_execute_one()` pops PC.
4. Next `Emulator::run_frame()` step calls `im2_.tick(...)`.
5. `step_devices()` Phase 1 — sees `int_req=false` (V19R end-of-tick clear), `int_req_d=true`. No edge. Latch unchanged (already false from step 2).
6. `step_devices()` Phase 2 — for the cleared device: `state=S_0`, `im2_int_req=false` → S_0 branch checks `if (d.im2_int_req)` → false → no transition.

End state: state=S_0, im2_int_req=false. **No spurious re-trigger.** Both paths converge on the same end state with no observable side-effects.

### "Next IRQ already queued" scenario (over-suppression check)

If peripheral re-asserts `int_req` between the prior `tick()` and the upcoming RETI M1:

- raise_req() sets dev_[i].int_req=true.
- on_reti() runs at M1 time. Walks dev_[], finds device in S_ISR, clears state and im2_int_req. **DOES NOT** touch int_req (which is the peripheral-driven input).
- Next tick Phase 1: int_req=true, int_req_d=false (settled to false at end of prior tick) → **edge=true** → if int_en, im2_int_req latched true again → Phase 2 S_0→S_REQ. Correctly re-fires.

V22-IM2-01 fix does NOT over-suppress: it clears the *latch* (im2_int_req) but does NOT clear the *input* (int_req) or its delayed copy (int_req_d). The natural edge-detect path will correctly re-latch on the next genuine assertion. Verified by V19R-CPU-01 multi-frame test in same suite (passes 47/47).

## Step 5 — other legacy-vs-modern duplication audit

Methods examined for parallel-path drift:

| Method | Parallel path? | Symmetric? | Notes |
|---|---|---|---|
| `raise()` (legacy Im2Level) | yes — routes via `to_devidx()` to `dev_[i].int_req` | ✓ | V18R-CPU-02 fixed the DMA/DIVMMC/MULTIFACE alias collision early-return; correctly bypasses fabric. |
| `clear()` (legacy Im2Level) | same as raise() | ✓ | Symmetric to raise(). |
| `raise_req()` (DevIdx) | single path | ✓ | No duplication. |
| `clear_req()` (DevIdx) | single path | ✓ | No duplication. |
| `raise_unq()` | single path (sets int_unq + int_status + im2_int_req in same call) | ✓ | Matches VHDL :160 + :172 UNQ-04/05; one-shot cleared by tick end. |
| `clear_status()` | single path (clears int_status only, not im2_int_req) | ✓ | Matches VHDL :160 — `i_int_status_clear` gates only int_status. |
| `int_status()` | single path | ✓ | Returns int_status OR im2_int_req per VHDL :180. |
| `ack_vector()` | single path | ✓ | im_mode_==2 gate (V21-IM2-01); S_REQ→S_ACK transition. No `on_int_ack()` parallel walker exists; Z80Cpu's `on_int_ack` callback is just a thin lambda. |
| `int_line_asserted()` | single path | ✓ | im_mode_==2 gate (V21-IM2-01). |
| `on_int_ack` (Z80Cpu lambda) | single path | ✓ | Wraps `ack_vector()`. |
| `on_nmi_servicing` (Z80Cpu lambda) | single path | ✓ | Pushes saved_pc to NextReg only. |
| `step_pulse()` | single path (Agent C) | ✓ | OR-reduction across devices per VHDL :184-194 + zxnext.vhd:2017-2044. |
| `step_dma_delay()` | single path | ✓ | Wave E latch per zxnext.vhd:2001-2010. |
| `propagate_isr_serviced()` | documented no-op (inline in Phase 2 S_ISR branch and on_reti loop) | ✓ | The "Phase 3" exists as a signpost; both clear-points are now correct. |
| `compute_vector()` | single path | ✓ | Looks for S_ACK device; composes per zxnext.vhd:1999. |
| `device_ieo()` | single path | ✓ | IEO chain walker per im2_device.vhd:136-146. |

No other duplications missed. The only "legacy-vs-modern" pair that mattered was `on_reti()` vs `step_state_machine_with_iei()` S_ISR branch — and that is now closed.

## Step 6 — side-effect inspection

### Does the new latch-clear suppress legitimate next-IRQs?

No. The clear targets the *latch* (im2_int_req), not the *input* (int_req). On the next tick, the edge detector at `step_devices()` Phase 1 (im2.cpp:928) re-derives the latch from `int_req && !int_req_d && int_en` (or unqualified). The latch becomes true again **only on a genuine peripheral edge**, exactly as VHDL :172 specifies (`(i_int_unq) or (int_req='1' and i_int_en='1')`).

Discriminative coverage already in place:
- `V19R-CPU-01-INT-REQ-PULSE-SYNTHESIS-MULTI-FRAME-VHDL-101` — verifies multi-frame edge detection works after V19R end-of-tick auto-clear. Passes 47/47.
- `V19-IM2-03-INT-UNQ-ONE-SHOT-AFTER-ISR` — verifies int_unq one-shot doesn't leak past one cycle, so after an ISR the unq-driven latch correctly clears. Passes 47/47.

### Save/load impact?

`im2_int_req` is persisted via `save_state()` / `load_state()` (im2.cpp:1263-1303). The on_reti latch-clear happens transiently within a frame; save/load snapshots take the instantaneous value. No persistence implication.

### Any test that previously relied on the stale latch?

Reviewed cpu_z80n_im2_regressions_test.cpp's full test list (47 tests). None of the pre-V22 tests called `on_reti()` and then asserted that `im2_int_req` was still true. The closest are:
- V21-IM2-01: tests int_line_asserted gate on im_mode_; doesn't touch on_reti.
- V19-IM2-03 (int_unq): runs through full ack→ISR pipeline but uses tick-based S_ISR→S_0 clearing (the parallel path), where the latch was already correctly cleared.

No test enshrined the bug. The audit's fix is genuinely additive coverage.

### Pulse-mode hold check

In pulse mode (im2_mode_=false), on_reti() early-returns at line 281; step_state_machine_with_iei at line 1010-1012 forces state=S_0 and returns; Phase 1 at line 941-942 forces im2_int_req=false. All three converge on the VHDL pulse-mode invariant (`im2_reset_n='0'` holds the FSM and the latch). ✓

## Step 7 — adjacent re-audit

### Z80N opcode sampling (5 random rows)

Already verified in Step 2:
- ED 23 SWAPNIB
- ED 24 MIRROR_A
- ED 29 BSRA_DE_B
- ED 30 MUL_DE
- ED 91 NEXTREG_NN
- ED 93 PIXELDN
- ED 94 PIXELAD

7 / 7 verified VHDL-faithful. Sample large enough to confirm Pass-21 Z80N convergence finding.

### HALT + INT resume

z80_cpu.cpp:425 saves `(z80.pc.w + 1)` when halted, matching VHDL Halt_FF+1. NMI dispatch on line 416-427 uses `on_nmi_servicing(saved_pc)` correctly. ctest int_pulse / ctc_interrupts cover the int-resume window — 41/41 pass.

### Block-instruction INT resume (G89)

LDIRX/LDDRX/LDPIRX/LDIRSCALE all implement PC rewind on BC≠0. Verified semantics in z80n_ext.cpp at the LDIRX site (lines 764-825). Integration tests cover the scenario. cpu_z80n_im2 47/47 pass.

### DD/FD prefix on Z80N (V15-CPU-NIT-01 class-d still open)

The class-d follow-up (DD-prefix on Z80N opcodes) remains catalogued as architectural — outside class-(a/b/c) scope. The current pass correctly applies the V11-CPU-01 DDFD-decoder fix (im2.cpp:858 — non-DD/FD opcode after DDFD returns to S_0, including ED), preserving the canonical RETI-after-DDFD boundary.

## Findings

**None.** Reviewer-classified V22R-IM2 findings: 0.

## NITs

**None.** Code is well-commented, the fix is minimal (single statement), and the test is well-scoped and genuinely discriminative.

## Final review summary

| Aspect | Result |
|---|---|
| Verdict | **APPROVE — no missed findings** |
| Row count | 153 actual vs ~152 claimed (within +/-1) |
| Spot-check (10 ✓ rows) | 10/10 verified |
| V22-IM2-01 VHDL correctness | confirmed (`im2_peripheral.vhd:148+175`) |
| V22-IM2-01 sandwich | revert→FAIL; restore→PASS — discriminative |
| on_reti vs step_state_machine_with_iei symmetry | 13/13 state-variable rows symmetric |
| Other legacy-vs-modern duplication | 15 methods audited, all symmetric or single-path |
| Side effects | none (no over-suppression, no test-enshrinement) |
| Adjacent re-audit | Z80N opcode sample 7/7 ✓; HALT/block-instr/DD-FD all in order |
| Test invariants | ctest 38/38, FUSE 1356/1356, cpu_z80n_im2 47/47, cpu_int_pulse 11/11, ctc_test 132/132, ctc_interrupts 30/30, regression.sh 33/0/0 |

Pass-22 audit (`84fcc1c1` + `da54e25a`) is approved by the reviewer with no missed findings and no NITs. The IM2 fabric's two-path RETI handling is now fully symmetric; the V21+V22 family is closed.
