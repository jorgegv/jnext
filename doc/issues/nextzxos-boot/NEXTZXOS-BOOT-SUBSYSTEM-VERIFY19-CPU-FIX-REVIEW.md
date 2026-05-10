# Pass-19 fix-of-reviewer review — CPU + Z80N + IM2 subsystem

Fix-reviewer branch `task2/verify19-cpu-z80n-im2-fix-review`. Reviewed against fix-of-reviewer HEAD `a32cf6c` (`fix(task2-pass19-cpu-review): V19R-CPU-01 — int_req 1-cycle pulse synthesis (auto-clear at end of tick())`). Parent reviewer HEAD `087d2dd`. Independent — reviewer is a different agent than both the auditor and the fixer.

## Verdict

**APPROVE — VHDL-faithful, sandwich-discriminative, no side effects, no adjacent missed finding within scope.**

The fix correctly synthesizes VHDL `im2_peripheral.vhd:90-101`'s one-cycle-pulse semantic from jnext's level-modeled `raise_req()` API by clearing `dev_[].int_req` across all 14 devices at the END of `tick()`, AFTER `step_pulse()` and `step_devices()` have both observed the rising edge. The placement is precisely correct relative to the pipeline phases. All test suites pass with zero regression: ctest 38/38, FUSE 1356/1356, cpu_int_pulse 11/11, cpu_z80n_im2_regressions 45/45 (incl. the new test), ctc_interrupts 27/27, ctc_test 132/132, regression 33/0/0.

## VHDL faithfulness — verification

### Edge detector identity

`im2_peripheral.vhd:90-101` (re-read):
```vhdl
process (i_CLK_28)
begin
   if rising_edge(i_CLK_28) then
      if i_reset = '1' then
         int_req_d <= '0';
      else
         int_req_d <= i_int_req;
      end if;
   end if;
end process;

int_req <= i_int_req and not int_req_d;
```

This is a classic rising-edge detector on `i_int_req`. The signal `int_req` (combinational local) fires for exactly one CLK_28 cycle per rising edge of `i_int_req`. Subsequent identical cycles of `i_int_req='1'` produce `int_req='0'` because `int_req_d` is now also '1'.

### Pulse-source confirmation

`zxnext.vhd:1941`:
```vhdl
im2_int_req <= uart1_tx_empty & uart0_tx_empty & ula_int_pulse & ctc_zc_to &
   (uart1_rx_near_full or (uart1_rx_avail and not nr_c6_int_en_2_654(1))) &
   (uart0_rx_near_full or (uart0_rx_avail and not nr_c6_int_en_2_210(1))) &
   line_int_pulse;
```

The naming convention `*_pulse` is verified literal at `zxula_timing.vhd:79` (signal port `o_int_ula : out std_logic; -- 7 MHz pulse`), and the producing process at :550-559 sets `int_ula <= '1'` only when `hc=c_int_h AND vc=c_int_v`, else `'0'` — i.e. a single-cycle assertion. The same convention applies to `line_int_pulse` and `ctc_zc_to`. So `i_int_req`'s component bits are all one-cycle pulses in the VHDL.

Hence in real hardware, every ULA-frame / line-int / CTC-ZC event generates a fresh rising edge into the edge detector, which fires `int_req` for one cycle, which (gated by `i_int_en`) sets `im2_int_req` (the latch held until `isr_serviced`).

### jnext's level model — pre-fix bug

`raise_req(DevIdx d)` in `im2.cpp:358-360` simply does `dev_[i].int_req = true`. No paired `clear_req()`. Production callers (`emulator.cpp:4620` CTC, `:4665/:4683` UART, `:5390` ULA, `:6600` LINE) all set this level and leave it asserted. Pre-fix, after the first edge fires, `int_req_d=true` persists across all subsequent ticks (because step_devices Phase 1 keeps re-latching `int_req_d <= int_req = true`), so no new edge ever fires for that device for the remainder of the session.

### Fix mechanism

The fix (commit `a32cf6c`) adds at the end of `tick()` (im2.cpp:141):
```c
for (int k = 0; k < N; ++k) dev_[k].int_req = false;
```

This is placed AFTER:
1. `step_pulse()` (im2.cpp:73) — reads `d.int_req && !d.int_req_d` to recompute pulse-mode edge.
2. `step_devices()` (im2.cpp:74) — Phase 1 captures `d.int_req_d = d.int_req` (line :900).
3. `step_dma_delay()` (im2.cpp:78).
4. `int_unq` clear loop (im2.cpp:90, V19-IM2-03 fix).

Per-tick state evolution under the fix:

| Tick | Pre-tick     | After step_pulse | After step_devices (P1) | After auto-clear |
|------|--------------|------------------|-------------------------|------------------|
| N    | int_req=T (just raised), int_req_d=F | edge sampled (T,F)→fire | int_req_d=T | int_req=F |
| N+1  | int_req=F, int_req_d=T | no edge | int_req_d=F (settles) | int_req=F (no-op) |
| N+2  | int_req=T (re-raised), int_req_d=F | edge sampled (T,F)→fire | int_req_d=T | int_req=F |

Matches VHDL semantic exactly: every per-event raise produces one edge.

### Why each phase ordering matters (cross-check)

- `step_pulse()` must run BEFORE the clear because it uses `d.int_req && !d.int_req_d` to reconstruct the VHDL edge for pulse-mode. (If it ran AFTER the clear, `d.int_req=false` would make every pulse-mode interrupt miss.)
- `step_devices()` Phase 1 must run BEFORE the clear because Phase 1 latches `d.int_req_d = d.int_req` — this is what makes the level→pulse conversion work. If we cleared `int_req` first, the latch would store false and the next tick's settle would fire a spurious second edge.
- The auto-clear must run AFTER both phases so it doesn't interfere with edge sampling.

The placement at line 141 (right after the V19-IM2-03 `int_unq` clear at line 90) satisfies all three constraints. **Placement is correct.**

## Sandwich verification — independent

Independent run, this reviewer's session, Release build at HEAD `a32cf6c`:

| Step                                                | Result                                                              |
|-----------------------------------------------------|---------------------------------------------------------------------|
| Build Release at HEAD `a32cf6c`                     | OK                                                                  |
| Run `cpu_z80n_im2_regressions_test`                 | **45/45 PASS** incl. `V19R-CPU-01-INT-REQ-PULSE-SYNTHESIS-MULTI-FRAME-VHDL-101` |
| Revert `src/cpu/im2.cpp` to parent `087d2dd`         | OK                                                                  |
| Rebuild test, run                                   | **44/45** — V19R-CPU-01 FAILs with `Frame-2: raise+tick state=0 (post-fix: S_REQ=1; pre-fix: S_0=0 stuck) int_line=0 (post-fix: 1; pre-fix: 0)` |
| Restore `src/cpu/im2.cpp` to `a32cf6c`               | OK                                                                  |
| Rebuild test, run                                   | **45/45 PASS** again                                                |

Sandwich is correct and discriminative. The test FAILs without the fix (matches the documented pre-fix mode: `int_req_d` stuck true → no frame-2 edge → state never leaves S_0 → `int_line` never asserts) and PASSes with it.

## Side-effect inspection

Full Release rebuild at `a32cf6c`. All listed invariants met:

| Test suite                                | Result          |
|-------------------------------------------|-----------------|
| `ctest --output-on-failure`               | **38/38 PASS**  |
| `fuse_z80_test build/test/fuse`           | **1356/1356**   |
| `cpu_int_pulse_test`                      | **11/11**       |
| `cpu_z80n_im2_regressions_test`           | **45/45**       |
| `ctc_interrupts_test`                     | **27/27**       |
| `ctc_test`                                | **132/132**     |
| `test/00regression/regression.sh`         | **33/0/0**      |

### Per-concern side-effect analysis

**1. Pulse mode (`im2_mode_=false`).** `step_pulse()` consumes the edge BEFORE the auto-clear (im2.cpp:1053 — `const bool int_req_edge = d.int_req && !d.int_req_d`). For each tick with a fresh raise, the pulse fabric still fires correctly. Additionally, V17-CPU-01 holds `im2_int_req=0` whenever `im2_reset_n=false` (= pulse mode), so the IM2 latch can never spuriously fire from a residual `int_req` in pulse mode. `cpu_int_pulse_test`: **11/11 PASS**. Confirmed no regression.

**2. V18R-CPU-02 (DMA/DIVMMC/MULTIFACE legacy raise()).** The legacy `raise()` early-returns for these three sources (im2.cpp:208-216) and NEVER sets `dev_[].int_req`. The auto-clear iterates all 14 devices unconditionally; for slots whose `int_req` was already false, the assignment is a harmless no-op. Tests `V18R-CPU-02-DMA-RAISE-NO-POLLUTE-CTC7` and `V18R-CPU-02-DMA-RAISE-NO-POLLUTE-ULA`: **PASS**.

**3. V17-CPU-01 (pulse→IM2 mode flip phantom IM2 int).** That fix gates `im2_int_req` on `im2_reset_n` in `step_devices` Phase 1 (im2.cpp:864-866). Independent of `int_req` lifetime. The auto-clear runs after that phase; no interaction. cpu_z80n_im2_regressions includes V17-CPU-01 test among the 45 — all pass.

**4. V19-IM2-03 (int_unq one-shot clear).** Same pattern (clear at end of tick). The two clear loops are adjacent (im2.cpp:90 for int_unq, line :141 for int_req). Sequencing: int_unq is cleared FIRST, then int_req. Order is irrelevant because neither field references the other on this clear (the dependency was int_unq → im2_int_req → state_machine, all consumed in earlier phases). V19-IM2-03 test: **PASS**.

**5. IM2W-* tests in ctc_test.** ctc_test exercises 132 cases covering the full IM2 wrapper lifecycle (status/clear, DMA-int, unqualified, status-mask packings, integration with ULA/Line). **132/132 PASS**.

**6. Save/load state.** `int_req` and `int_req_d` are both serialized (`save_state` at im2.cpp:1214-1215, `load_state` at :1253-1254). Auto-clear sets the on-disk-saved `int_req=false` for all devices at end of every tick — this is the natural post-tick state. On load, restored state will see `int_req=false / int_req_d=true` (typical post-edge settle), or `int_req=false / int_req_d=false` (idle). Either is a quiescent state requiring a fresh raise on the next tick to fire an edge. No spurious edge possible on load. Save-load is unchanged from pre-fix behavior modulo the (correct) post-tick clear pattern.

**7. State-machine progression independence.** After a raise→tick fires the edge, the device's S_REQ/S_ACK/S_ISR walk is driven entirely off `im2_int_req` (the latch held by isr_serviced), NOT off `int_req` (the input). So clearing `int_req` after the latch is set has zero effect on the in-flight ISR sequencing. Verified via `V19R-CPU-01` test (which drives a full frame-1 ISR: raise→S_REQ→ack→S_ACK→tick→S_ISR→RETI tick→S_0).

## Adjacent re-audit — additional pulse-vs-level antipatterns?

Searched for related VHDL-pulse-but-jnext-level mismatches that might still lurk:

| Signal               | VHDL pulse? | jnext model | Status                                         |
|----------------------|-------------|-------------|------------------------------------------------|
| `i_int_req`          | Yes (cycle) | Was level (now auto-cleared) | FIXED V19R-CPU-01                    |
| `i_int_unq`          | Yes (`nr_20_we`-gated) | Was level (now auto-cleared) | FIXED V19-IM2-03            |
| `i_int_status_clear` | Yes (`nr_c8/c9/ca_we` pulse) | One-shot in `clear_status()` per-NR-write | OK — driven by per-NR-write call which is itself one-shot |
| `i_reti_seen`        | Yes (cycle) | `reti_seen_pulse_` cleared on next `on_m1_cycle` | OK — one-cycle owned by decoder |
| `i_reti_decode`      | Combinational (state == S_ED_T4) | Mirrored in decoder phase | OK |
| `nr_*_we` strobes    | Yes (per-write) | Direct method calls | OK — caller-driven one-shot |
| `pulse_int_n` | Held low for 32 or 36 CPU cycles, then high | `step_pulse()` count-down | OK — V18R-CPU-01 fix |

**No adjacent missed finding.** The two pulse-vs-level antipatterns at the IM2-wrapper interface (`int_req` and `int_unq`) have now BOTH been fixed (V19-IM2-03 and V19R-CPU-01). All other VHDL pulse signals are either (a) consumed combinationally in the same tick (no level-carryover possible), or (b) already correctly modeled as one-shots.

## Code-style / craftsmanship

- Comment at im2.cpp:92-140 is thorough, cites VHDL line numbers (`im2_peripheral.vhd:90-101`, `zxnext.vhd:1941`, `im2_peripheral.vhd:170-171`, `im2_peripheral.vhd:175`), explains the pre-fix bug mode, the per-tick state evolution, and the interaction with V17-CPU-01 / V18R-CPU-02. Style is consistent with V19-IM2-03's adjacent comment block (im2.cpp:79-89).
- No collateral changes outside `src/cpu/im2.cpp` and the test file. Minimal diff (+51 LOC fix, +115 LOC test).
- New test follows the existing `check(res, "<TEST-ID>-VHDL-<line>", <invariant>, <detail>)` convention used throughout `cpu_z80n_im2_regressions_test.cpp`; detail string captures both expected post-fix and pre-fix observable states for diagnostic clarity.

## Conclusion

**APPROVE.** The fix is VHDL-faithful, the sandwich is genuinely discriminative, every required test invariant holds, no side effect, no adjacent missed finding. This closes V19R-CPU-01 cleanly and exhausts the level-vs-pulse-input antipattern family at the IM2 wrapper interface.
