# Pass-20 fix-of-reviewer review — CPU + Z80N + IM2 subsystem

Fix-reviewer branch `task2/verify20-cpu-z80n-im2-fix-review`. Reviewed against fix-of-reviewer HEAD `58ef471` (commit 2 of 3 — `fix(task2-pass20-cpu-review): V20R-CPU-NIT-02 — drop legacy ULA/LINE request_interrupt callbacks (poll captures same events)`). Intermediate fix commit `e7d2a8d` (`fix(task2-pass20-cpu-review): V20R-CPU-NIT-01 — persist prev_pulse_int_n_ in save/load state`). Doc-only commit `a4c9dbf` (`doc(task2-pass20-cpu-review): V20R-DOC-NIT-01+02 — audit report row-count + line-ref corrections`) skipped per fix-reviewer rule on comment-only changes — but cross-referenced. Parent reviewer HEAD `7d2c135` (APPROVE-WITH-NITS). Independent — reviewer is a different agent than both the auditor and the fixer.

## Verdict

**APPROVE — both NITs are VHDL-faithful, sandwich-discriminative, no missed-INT regression, no side effects, no adjacent missed finding within scope.**

The two code fixes precisely address the reviewer's class-(c) NITs:

- **V20R-CPU-NIT-01** appends `prev_pulse_int_n_` to the `Emulator::save_state` / `load_state` schema using the established append-only + `!r.eof()`-tolerant pattern. This pairs with the already-persisted `Im2Controller::pulse_int_n_`, restoring the "exactly ONCE per pulse" invariant of the V20-IM2-01 falling-edge poll across save/load round-trips taken mid-pulse.

- **V20R-CPU-NIT-02** (reviewer-recommended Option A) drops the legacy `cpu_.request_interrupt(0xFF)` from the ULA FRAME-INT scheduler callback (`emulator.cpp:5443+`) and the LINE-INT scheduler callback in `reschedule_line_interrupt()` (~:6716). The V20-IM2-01 poll now becomes the **sole driver** of pulse-mode /INT requests, symmetric with the V19-IM2-04 poll for IM2 mode. The latent double-stamp window (legacy callback at end of instr N + V20 poll at end of instr N+1, both re-stamping `int_requested_at_`) is eliminated. The poll's edge-detection guarantees exactly ONE stamp per pulse.

All test suites pass with zero regression: ctest 38/38, FUSE 1356/1356, cpu_int_pulse 11/11, cpu_z80n_im2_regressions 45/45, ctc_interrupts 30/30 (two new test rows added), ctc_test 132/132, regression 33/0/0.

## V20R-CPU-NIT-01 — `prev_pulse_int_n_` save/load persistence

### Fix scope

Two-block append in `src/core/emulator.cpp`:
- `Emulator::save_state()` (line ~7016-7029): `w.write_bool(prev_pulse_int_n_)` after the last existing slot (`nr_02_bus_reset_`).
- `Emulator::load_state()` (line ~7264-7277): `if (!r.eof()) prev_pulse_int_n_ = r.read_bool();` companion read with EOF tolerance for backwards compatibility.

Two header additions in `src/core/emulator.h`:
- Test-only accessors `prev_pulse_int_n_for_test()` / `set_prev_pulse_int_n_for_test()` (lines 374-375).

### VHDL faithfulness

`prev_pulse_int_n_` is not a VHDL signal — it is a jnext-internal shadow used by the V20-IM2-01 poll (emulator.cpp:5806-5811) for one-tick falling-edge detection on `pulse_int_n` (the modelled VHDL signal at `zxnext.vhd:1396`, derived by the FSM at :2017-2031). The shadow's role is to translate VHDL's combinational `pulse_int_n` edge into a single `cpu_.request_interrupt(0xFF)` call in jnext's coarser tick model.

The companion VHDL state — `Im2Controller::pulse_int_n_` (which mirrors the VHDL signal of the same name) — is already persisted by `Im2Controller::save_state()` at `src/cpu/im2.cpp:1232`. Pre-fix, persisting `pulse_int_n_=false` (mid-pulse) without also persisting `prev_pulse_int_n_=false` would cause `load_state` to restore the shadow to its constructor default `true` (Im2Controller::reset() default at :37 — matched by `Emulator::reset()` at :111). The very next tick's poll would then see `!cur && prev = !false && true = true` = phantom falling edge → spurious `request_interrupt(0xFF)`.

The harm is mitigated downstream (the 32/36T drop arm in `Z80Cpu::execute()` would clean up the phantom INT before any ISR could observe a double-fire), but the "exactly ONCE per pulse" local invariant of the V20-IM2-01 fix is violated. The reviewer correctly classified this as class-(c) (divergence-not-symptomatic).

The fix mirrors the existing pattern for `prev_nmi_generate_n_` (emulator.h:734, persisted at :6964 / :7193) — another falling-edge shadow that IS already persisted symmetrically with the underlying NMI line state.

### Schema invariants

Append-only schema with `!r.eof()`-tolerant load. The repo has 11 such EOF-guarded slots in `Emulator::load_state` (lines 7198, 7206, 7212, 7215, 7218, 7224, 7232, 7239, 7250, 7260, 7276 — the new one). The NIT-01 slot is appended at the END (after `nr_02_bus_reset_`), so it does not byte-shift any earlier slot. Older snapshots (predating Pass-20) load with the shadow at its reset default `true` — which is also the post-`init()` / `reset()` value, so the boot path is unaffected. New snapshots round-trip the shadow faithfully.

### Sandwich verification — V20R-CPU-NIT-01

Isolated revert of only NIT-01's `src/core/emulator.cpp` deltas (via `git show e7d2a8d -- src/core/emulator.cpp | git apply -R`, leaving `e7d2a8d` header/test additions AND `58ef471` NIT-02 fix intact):

| Build state | ctc_interrupts result |
|--|--|
| Post-fix (HEAD) | 30/30 PASS |
| Pre-fix (NIT-01 emulator.cpp deltas reverted, NIT-02 fix kept) | **29/30 — V20R-CPU-NIT-01-PREV-PULSE-PERSIST FAILS** with `post-load shadow=1` (slot absent → load leaves shadow at reset default `true`, accessor cannot observe the saved `0`). NIT-02 test passes. |
| Restored (HEAD) | 30/30 PASS |

The test is **discriminative** — it fails iff and only if the NIT-01 fix is removed.

### Adjacent shadow audit

I audited adjacent edge-derived shadow state in the Emulator scope:

- `prev_nmi_generate_n_` (line 734) — **already persisted** (write :6964, read :7193). Symmetric with the underlying NMI line state.
- `prev_pulse_int_n_` (line 813) — **now persisted** post-NIT-01.

No further missed shadow slots in Emulator scope.

In `Im2Controller`, the per-device `int_req_d` shadow (used for VHDL line-101 rising-edge detect) is **already persisted** at im2.cpp:1215/:1254. `pulse_int_n_` and `pulse_count_` are persisted at :1232-:1233. The state-machine `state` and the `reti_seen_pulse_` / `retn_seen_pulse_` / `reti_decode_` / `dma_delay_ctrl_` shadow latches are all persisted. The IM2 fabric is internally consistent. No adjacent miss.

## V20R-CPU-NIT-02 — drop legacy ULA/LINE callbacks

### Fix scope

Three deltas in `src/core/emulator.cpp`:

1. `run_frame()` FRAME-INT scheduler lambda (line 5443-5459): drop the conditional `if (!im2_.is_im2_mode()) cpu_.request_interrupt(0xFF);` after `im2_.raise_req(Im2Controller::DevIdx::ULA)`. Replaced with a comment-only block explaining the rationale.

2. `run_frame()` V20 poll comment (line 5773-5785): update the inline comment to reflect that the poll is now the sole driver of pulse-mode /INT.

3. `reschedule_line_interrupt()` LINE-INT scheduler lambda (line 6736+): drop the same conditional. Replaced with a back-reference comment to the FRAME-INT site.

Plus test instrumentation in `src/cpu/z80_cpu.{h,cpp}`:
- `Z80Cpu::request_interrupt_count_` monotonic counter (header line 154) incremented at `request_interrupt()` (cpp line 931).
- Public `request_interrupt_count()` getter and `reset_request_interrupt_count()` setter (header lines 140-141).
- **Not persisted in save/load** — would shift schema layout for a test-only accessor. Counter is reset-only at test setup time.

### VHDL faithfulness — critical chain of evidence

The reviewer's class-(c) NIT identified that the pre-fix legacy callbacks AND the V20 poll BOTH stamped `int_requested_at_`, where the poll's stamp was at a LATER tstate than the legacy callback's. For a slow ISR this is harmless (the 32/36T drop arm expires before any EI), but for a fast-EI ISR the re-stamped window could accept a second INT that real hardware would not.

**The critical concern for this fix-review is that dropping the legacy callbacks RELIES ON the V20 poll capturing every edge that the legacy callbacks would have caught.** I verified each link in the chain:

#### Link 1 — VHDL z80_int_n composition

VHDL `zxnext.vhd:1840`:
```vhdl
z80_int_n <= ((pulse_int_n and im2_int_n) or not expbus_disable_int) and (i_BUS_INT_n or expbus_disable_int);
```

In the default scenario (expbus_disable_int='1', i.e. no expansion bus pulling /INT), this simplifies to `z80_int_n <= pulse_int_n AND im2_int_n`. Either signal driving low asserts /INT. Pre-NIT-02 jnext modelled this via TWO separate paths (legacy callback for pulse mode + V20 poll for the same pulse signal); post-NIT-02 only the V20 poll, which polls `pulse_int_n` directly via `im2_.pulse_int_n()`. The poll path is verifiably VHDL-faithful.

#### Link 2 — pulse_int_n FSM

VHDL `zxnext.vhd:2017-2031`:
```vhdl
process (i_CLK_28)
begin
   if falling_edge(i_CLK_28) then
      if reset = '1' then
         pulse_int_n <= '1';
      elsif pulse_int_n = '1' then
         if pulse_int_en = '1' then
            pulse_int_n <= '0';
         end if;
      elsif pulse_count_end = '1' then
         pulse_int_n <= '1';
      end if;
   end if;
end process;
```

`pulse_int_n` drops on `pulse_int_en` rising edge while idle. `pulse_int_en` is the OR-reduction of all 14 device `o_pulse_en` outputs. jnext's `Im2Controller::step_pulse()` (im2.cpp:1047-1111) implements exactly this FSM: scan all devices for `(int_req_edge && int_en) || int_unq`, gated by exception+mode rules, OR-reduce into local `pulse_en`, then drive the same `pulse_int_n_` / `pulse_count_` state machine.

#### Link 3 — o_pulse_en derivation

VHDL `im2_peripheral.vhd:186` / `:192`:
```vhdl
-- non-exception:
o_pulse_en <= ((int_req and i_int_en) or i_int_unq) and not i_mode_pulse_0_im2_1;
-- exception (ULA):
o_pulse_en <= ((int_req and i_int_en) or i_int_unq) and ((i_mode_pulse_0_im2_1 and not i_im2_mode) or (not i_mode_pulse_0_im2_1));
```

Here `int_req` is the one-cycle pulse produced by the line-101 edge detector. jnext's `step_pulse()` reproduces this via `const bool int_req_edge = d.int_req && !d.int_req_d;` (im2.cpp:1053) — and the `int_req_d` shadow is updated in `step_devices()` Phase 1 on the same tick (im2.cpp:898), AFTER `step_pulse` has observed the edge. The ULA exception path (im2.cpp:1057-1062) fires in pulse mode always — matching VHDL :192.

So when the ULA FRAME-INT scheduler does `im2_.raise_req(DevIdx::ULA)`:
- `dev_[ULA].int_req = true, dev_[ULA].int_req_d = false` (just-set).
- Next inner-loop iteration's `im2_.tick()` calls `step_pulse()`:
  - `int_req_edge = true && !false = true`; `int_en` is true by default; ULA has `exception=true`; im2_mode_ is false → `pulse_en=true`.
  - `pulse_int_n_` was `true` (idle) → drops to `false`. `pulse_count_=0`.
- Then `step_devices()` Phase 1 latches `int_req_d <= int_req` → both `true`.
- Returns from `tick()`. Now the V20 poll at emulator.cpp:5806-5811 reads `cur=im2_.pulse_int_n()=false`, `prev=prev_pulse_int_n_=true (its reset default)` → falling edge → `cpu_.request_interrupt(0xFF)`.

This is exactly ONE stamp per logical pulse. The pulse_count_end FSM at :2027 (here mirrored in step_pulse() at im2.cpp:1097-1110) drives `pulse_int_n_` back high after 32T (48K/+3) or 36T (128K/Pentagon/Next), and `prev_pulse_int_n_` follows. No re-fire until the next frame.

#### Link 4 — poll frequency

The V20 poll runs **after every Z80 instruction execution** inside `Emulator::run_frame()`'s inner loop:
1. Z80 instruction executes (`cpu_.execute()`).
2. `im2_.tick()` is called (line 5703).
3. V19 IM2-mode poll (line 5738-5740).
4. V20 pulse-mode poll (line 5806-5811).
5. `scheduler_.run_until(clock_.get())` (line 6007) — fires due scheduled events.
6. Next iteration.

When the FRAME-INT or LINE-INT scheduled lambda fires at step 5, the `raise_req()` call sets the device int_req level. The very next iteration's `im2_.tick()` at step 2 detects the edge in `step_pulse()` and drops `pulse_int_n_`. The V20 poll at step 4 catches the falling edge and stamps `request_interrupt(0xFF)`.

**Net effect**: a one-Z80-instruction delay between scheduler fire and CPU /INT request vs pre-fix. This is acknowledged in the commit message and is VHDL-faithful (pulse_int_n drops on the next CLK_28 falling-edge after the rising edge that asserted `pulse_int_en`, and the Z80 /INT pin is sampled at the rising edge of the last clock of M1 — both real-hardware properties that admit a sub-instruction propagation delay).

The 32/36T pulse window is plenty for the next instruction's poll to fire well within the acceptance window; FRAME-INT recurs at ~70k tstates so there's no chance of overlap.

### Missed-event analysis (this fix's primary risk)

The reviewer's concern: any frame where the poll misses a ULA/LINE rising edge that the legacy callback would have caught loses that interrupt.

Verified this cannot happen:

1. **Every scheduled callback fires at exactly one inner-loop iteration boundary** — `scheduler_.run_until()` is the LAST thing in each iteration; the scheduled lambda runs synchronously inside that call.
2. **The next iteration ALWAYS calls `im2_.tick()` before the poll** — there is no branch in `run_frame()`'s inner loop that skips `tick()` without exiting the frame. (Exits at end-of-frame or DMA active; both are bounded conditions that resume tick polling on the next frame / cycle.)
3. **`step_pulse()` always evaluates pulse_en on every tick** — even if no device asserted in the current iteration, it's a pure combinational evaluation. So any rising edge produced by the previous iteration's scheduler-callback is unconditionally observed.
4. **The falling-edge poll detects the transition exactly once** — `prev_pulse_int_n_` is updated AFTER the comparison, so consecutive tick polls with the same `cur` produce zero edges. Re-fire only on the next `pulse_int_n` high → low transition (= next FRAME / LINE / CTC pulse).

The chain `raise_req → next-tick tick() → step_pulse() drops pulse_int_n_ → V20 poll fires` is **mandatory** under run_frame's inner loop — no path skips it.

### CTC / UART independence

CTC and UART interrupts NEVER had legacy `request_interrupt(0xFF)` callbacks — they always relied solely on the V20-IM2-01 poll (the original Pass-20 fix from `7d2c135`'s parent commits added the poll specifically because CTC/UART were silently dropped pre-fix). So NIT-02 does NOT change CTC/UART behavior. Verified: `ctc_test` 132/132, `cpu_int_pulse_test` 11/11 — all pass post-fix.

### Sandwich verification — V20R-CPU-NIT-02

Isolated revert of only NIT-02's `src/core/emulator.cpp` deltas (via `git show 58ef471 -- src/core/emulator.cpp | git apply -R`, leaving `e7d2a8d` NIT-01 fix intact):

| Build state | ctc_interrupts result | cpu_int_pulse result |
|--|--|--|
| Post-fix (HEAD) | 30/30 PASS | 11/11 PASS |
| Pre-fix (NIT-02 emulator.cpp deltas reverted, NIT-01 fix kept) | **29/30 — V20R-CPU-NIT-02-NO-DOUBLE-STAMP FAILS** with `request_interrupt_count after 1 frame = 2` (legacy callback + V20 poll both stamp). NIT-01 test passes. | **11/11 PASS** (pulse-window timing tests independent of callback path). |
| Restored (HEAD) | 30/30 PASS | 11/11 PASS |

The test is **discriminative** — it fails iff and only if the NIT-02 fix is removed. And critically, `cpu_int_pulse_test` 11/11 passes both **with and without** the legacy callbacks — confirming that pulse-window timing is correctly handled by the V20 poll alone.

### Test instrumentation review

`Z80Cpu::request_interrupt_count_`:
- Declared at `z80_cpu.h:154`. Single `uint32_t`, default-init 0.
- Incremented unconditionally inside `Z80Cpu::request_interrupt()` at `z80_cpu.cpp:931` — a single `++` with no branches, no side effects, no observable behavior change to non-test callers.
- **Not persisted in save/load** — explicitly noted in commit message. This is correct: a test-only accessor must NOT shift the save/load schema or it would break backwards compatibility across saves taken before vs after this build.
- Reset via public `reset_request_interrupt_count()` — only called from tests.

No leakage; the instrumentation is invisible to production code paths.

## Side-effect analysis (cross-NIT)

### NIT-01 side effects

- Save side: appends 1 bool at end of `Emulator::save_state` — backwards-compatible for `load_state` callers (older code reading new save will encounter the extra bool at end-of-stream and either skip it or fail at EOF, depending on caller; but since `load_state` itself is the only reader, and it's EOF-tolerant, this is safe).
- Load side: `!r.eof()` guard ensures older saves (without the new slot) load to the reset-default value.
- Init/reset side: `prev_pulse_int_n_` already has `init()` / `reset()` assignments at emulator.cpp:111, :6249.

### NIT-02 side effects

- One-instruction delay before INT acceptance vs pre-fix. Acknowledged in commit message; VHDL-faithful per pulse_int_n FSM.
- No change to CTC/UART/other peripheral INT paths — they never had legacy callbacks.
- `request_interrupt_count_` is test-only instrumentation; not persisted; no behavioral side-effects.

### Cross-NIT interaction

NIT-01 ensures save/load preserves the V20 poll's edge-detection state. NIT-02 makes the V20 poll the sole driver of pulse-mode /INT. Together they form a coherent invariant: "the V20 poll captures exactly ONE falling edge per pulse, and that invariant survives save/load round-trips." Both fixes co-locate the same architectural decision in the same direction. Verified consistent.

## Test invariants — all green at HEAD `58ef471`

| Suite | Result |
|--|--|
| `ctest --output-on-failure` (Release) | **38/38 PASS** |
| `fuse_z80_test` | **1356/1356 PASS** (NON-NEGOTIABLE — held) |
| `regression.sh` | **Pass: 33  Fail: 0  Skip: 0** |
| `ctc_interrupts_test` | **30/30 PASS** (2 new rows added) |
| `cpu_int_pulse_test` | **11/11 PASS** |
| `cpu_z80n_im2_regressions_test` | **45/45 PASS** |
| `ctc_test` | **132/132 PASS** |

## Adjacent re-audit (in-scope V20R only)

In-scope: Pass-20 CPU + Z80N + IM2 reviewer NITs from `7d2c135`. Reviewer flagged exactly two class-(c) items (NIT-01, NIT-02) and additional doc-only NITs (DOC-NIT-01/02, addressed in `a4c9dbf`). I re-audited the fix scope for adjacent missed items:

1. **`prev_pulse_int_n_` analog in IM2 fabric** — `Im2Controller::pulse_int_n_` is persisted (im2.cpp:1232) so it survives save/load. Pairs correctly with NIT-01's `prev_pulse_int_n_` persistence.

2. **Other Emulator-scope edge shadows** — `prev_nmi_generate_n_` is already persisted (emulator.cpp:6964/:7193). No others.

3. **Other duplicate-stamp paths** — searched for all `cpu_.request_interrupt(` callsites:
```
grep -n "cpu_\.request_interrupt(" src/core/emulator.cpp
```
   Surviving callsites post-NIT-02:
   - The V19 IM2-mode poll (`request_interrupt(0xFE)`) — sole driver for IM2 mode, by design (V19-IM2-04 fix).
   - The V20 pulse-mode poll (`request_interrupt(0xFF)`) — sole driver for pulse mode, by NIT-02 design.
   - No other peripheral paths.
   
   No remaining duplicate stamps. The architectural cleanup is complete and symmetric across both INT modes.

4. **Schema slot positioning** — NIT-01's new slot is at the very end of `save_state` / `load_state`. No byte-shift to earlier slots. Existing 10 EOF-guarded slots all preserve their positions.

5. **`request_interrupt_count` instrumentation** — not persisted, no leakage, no schema impact.

No missed findings.

## Final summary

- **Verdict**: **APPROVE**.
- **NIT-01**: VHDL-faithful save/load schema append; sandwich confirms discriminative test; companion to existing `Im2Controller::pulse_int_n_` persistence; matches existing `prev_nmi_generate_n_` pattern.
- **NIT-02**: VHDL-faithful single-driver design (V20 poll is now sole pulse-mode /INT driver, symmetric with V19 IM2 poll); sandwich confirms discriminative test; `cpu_int_pulse` 11/11 confirms pulse-window timing intact without legacy callbacks; no missed-INT regression in the scheduler→raise_req→tick→poll chain.
- **No side effects detected**: 38/38 ctest, 1356/1356 FUSE, 33/0/0 regression, all targeted suites green.
- **No adjacent missed findings within scope.**
- **Final HEAD SHA**: `58ef471`.
