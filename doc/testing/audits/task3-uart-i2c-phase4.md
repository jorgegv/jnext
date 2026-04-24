# Task 3 — UART + I2C/RTC Subsystem SKIP-Reduction Audit (2026-04-24)

Closure audit for the Task 3 UART + I2C/RTC skip-reduction plan executed
2026-04-24 in a single session (Phases 0 → 4). Mirrors the structure of
[task3-ula-phase4.md](task3-ula-phase4.md) and
[task3-ctc-phase5.md](task3-ctc-phase5.md).

Plan doc: [doc/design/TASK3-UART-I2C-SKIP-REDUCTION-PLAN.md](../../design/TASK3-UART-I2C-SKIP-REDUCTION-PLAN.md).
Row-by-row baseline: [task3-uart.md](task3-uart.md) (pre-plan 106/58/0/48).
Closing commit: `7c50b0c` (`test(uart): Phase 3/4 close …`).

## Headline numbers

|                             | Before plan (2026-04-24) | After Phase 3 (2026-04-24) | Δ         |
|-----------------------------|-------------------------:|---------------------------:|----------:|
| `uart_test` total           |                    106   |                      92    |   −14     |
| `uart_test` pass            |                     58   |                      92    |   +34     |
| `uart_test` fail            |                      0   |                       0    |     0     |
| `uart_test` skip            |                     48   |                       0    |   −48     |
| `uart_integration_test`     |                    N/A   |                12/12/0/0   |  +12 pass |
| Aggregate unit suites       |                     26   |                      27    |    +1     |
| Aggregate pass              |                   3041   |                    3087    |   +46     |
| Aggregate skip              |                    264   |                     216    |   −48     |
| Regression                  |                 34/0/0   |                  34/0/0    | unchanged |
| FUSE Z80                    |              1356/1356   |               1356/1356    | unchanged |

Headline delta: **+34 `skip()` → `check()` flips in `uart_test`, +12 new
integration-suite rows (all pass), 14 rows removed from the row census
(12 re-homed to the integration suite + 2 reclassified to `D-UNOBSERVABLE`
inline comments). `uart_test.cpp` has zero runtime `skip()` calls.**

## Phase summary

| Phase | Action | Critic verdict | Commits |
|---|---|---|---|
| 0 | Re-home 12 cross-subsystem rows (INT-01..06, GATE-01..03, DUAL-05/06, I2C-10) to `// RE-HOME:` comments; 6 empirical flips (I2C-P06 + RTC-06..10); refresh stale skip reasons on RTC-11..17; correct Q2 IM2 vector map (4 vectors, not 2); plan-doc "Current status" block refresh. `106/58/0/48 → 94/64/0/30`. | APPROVE | `19a4e41` → merge `0165bf0` |
| 1 | Bit-level `UartTxEngine` + `UartRxEngine` (opt-in via `set_bitlevel_mode`, default OFF) + 9-bit RX FIFO (uint16_t) + `I2cRtc` 8→64 regs_ expansion (DS1307 NVRAM/12h/CH/control) + `I2cController::set_pi_i2c1_scl/sda` + `Emulator::inject_pi_i2c1`. Compile-only seam. | APPROVE-WITH-FIXES (2 bugs caught) | `dc0566b` → merge `48bb49e` + retrospective-critic fixes `3556a01` |
| 2 Wave A | TX bit-level (`UartTxEngine`) — 12 planned rows; 8 flipped at merge (TX-05/07/08/11/12, FRM-05/06, BAUD-07); TX-09/10/13/14 deferred on TX-parity scaffold bug. | APPROVE-WITH-FIXES | `dd99fa3` → merge `dd7fe8d` |
| 2 Wave B | RX bit-level (`UartRxEngine`) — 8 planned rows; 6 flipped (RX-10..15 + status bit 5 FIFO 9th-bit); RX-08/09 deferred on RX sticky-err orphan scaffold bug. | APPROVE-WITH-FIXES | `7c7254c` → merge `33404a0` |
| 2 Wave C | New `uart_integration_test.cpp` with 10 rows (INT-01..06 + GATE-01..03 + I2C-10); Emulator port-enable gate wrap at `emulator.cpp:1444-1464` (NR 0x82/0x83 gating). | APPROVE | `a511386` → merge `e1ef2cb` |
| 2 Wave D | DUAL-05/06 dual-UART channel routing + `Emulator::inject_joy_uart_rx` joystick-UART mux (ties to `src/input/iomode.{h,cpp}`). | APPROVE | `5afa5ec` → merge `9956197` |
| 2 Wave E | I2cRtc write path + 12h mode + CH bit + NVRAM 0x08-0x3F + control reg + I2C-11 (pi_i2c1 AND-gate). 8 flips landed; left 7 stale duplicate `skip("RTC-11..17")` calls as merge-conflict residue (see drift §plan-drifts + agent-worktree note). | APPROVE | `f38457b` → merge `f47cdd0` |
| 2 scaffold bugfix | Phase 1 bugs surfaced by Wave A/B critics — TX parity pre-shift ordering + RX sticky-err OR-in on error path. | inline fix; rows un-deferred in `b86de83` | `487928c` + `b86de83` |
| 3 | Delete 7 stale duplicate `skip("RTC-11..17")` left behind by Wave E merge; convert BAUD-02/03 to D-UNOBSERVABLE block comment per `feedback_unobservable_audit_rule` (`uart.vhd:323-326` prescaler LSB is structurally write-only); `101/92/0/9 → 92/92/0/0`. | (self; Phase 2 retrospective APPROVE already absorbed) | `7c50b0c` |
| 4 | Dashboard + traceability matrix + `UART-I2C-TEST-PLAN-DESIGN.md` current-status block + `EMULATOR-DESIGN-PLAN.md` Phase-9 UART+I2C entry marked `[x]` + this audit. | (in-line) | `7c50b0c` (same commit) |

All Phase-2 waves and Phase-1 scaffold passed through independent critic
review before merge (per `feedback_never_self_review.md`). A cross-wave
retrospective critic ran after Wave E merge, absorbed into the plan doc's
Phase 2 actuals section (commit `7c50b0c`).

## Row-count evolution

| Checkpoint              | `uart_test` | `uart_integration_test` | Aggregate      |
|-------------------------|------------:|------------------------:|---------------:|
| Pre-plan (2026-04-24)   | 106/58/0/48 |                     —   | 3305/3041/0/264 |
| Post Phase 0 (`0165bf0`) |  94/64/0/30 |                     —   |          —     |
| Post Wave A (`dd7fe8d`) |       —     |                     —   |          —     |
| Post Wave B (`33404a0`) |       —     |                     —   |          —     |
| Post Wave C (`e1ef2cb`) |       —     |              10/10/0/0  |          —     |
| Post Wave D (`9956197`) |       —     |              12/12/0/0  |          —     |
| Post Wave E (`f47cdd0`) | 101/79/0/22 |              12/12/0/0  |          —     |
| Post scaffold fix (`487928c`+`b86de83`) | 101/92/0/9 | 12/12/0/0 | 3312/3087/0/225 |
| Post Phase 3 (`7c50b0c`) |  **92/92/0/0** |          **12/12/0/0**  | **3303/3087/0/216** |

The `-9` aggregate shift from Phase-3 (3312 → 3303) is the row-census
reduction: 7 duplicate `skip("RTC-11..17")` calls deleted (matching
`check()` rows for the same IDs were already passing) + 2 BAUD rows
(BAUD-02/03) reclassified to D-UNOBSERVABLE inline comments (never count
toward the row census per `feedback_unobservable_audit_rule`).

## Plan-drifts caught during execution

Four drifts surfaced between plan text and VHDL oracle; each was corrected
before the closing commit, and the plan doc now reflects the VHDL-faithful
form.

### 1. 12-fail staleness in plan's "Current Status" block

- **Plan text (pre-Phase-0)**: `doc/testing/UART-I2C-TEST-PLAN-DESIGN.md:16-26`
  claimed `48/60 live pass, 12 fail, 46 skip` with 12 failing rows blocked
  on two bugs.
- **Live on main**: `106/58/0/48` (zero fails). Both C-class bugs had
  already been fixed months earlier:
  - `src/peripheral/i2c.cpp:101` false-STOP from stale `prev_sda_` —
    FIXED in `174fa56`.
  - `src/peripheral/uart.cpp:299` select-register read bit 6 vs bit 3 —
    FIXED in `47ee7e2`.
- **Fix**: Phase 0 rewrote the Current Status block to match reality
  (commit `19a4e41`).

### 2. IM2 interrupt vector numbering (Q2)

- **Plan kickoff text**: listed the 5 IM2 interrupt vectors as
  "1 (UART 0 RX), 2 (rx_near_full override), 12 (UART 1 RX), 13 (UART 0 TX)"
  — 2 vectors with "rx_near_full" as a separate third vector.
- **VHDL says** (`zxnext.vhd:1941-1944`):
  - vector 1 = `uart0_rx_near_full OR (uart0_rx_avail AND NOT nr_c6_210(1))`
  - vector 2 = `uart1_rx_near_full OR (uart1_rx_avail AND NOT nr_c6_654(1))`
  - vector 12 = `uart0_tx_empty`
  - vector 13 = `uart1_tx_empty`

  "rx_near_full" is NOT a separate vector — it's an OR-bypass inside
  vectors 1 and 2.
- **Fix**: Phase 0 corrected the plan's Q2 answer and the Wave C
  integration-test row specs (commit `19a4e41`); INT-02 asserts
  "RX near-full always triggers vector 1 regardless of
  `nr_c6_int_en_2_210(1)`" — semantics preserved, vector id correct.

### 3. NR 0x82 vs NR 0x83 gate-register for UART/I2C port enable

- **Plan Wave C text (draft)**: said NR 0x82 bits gate the UART and I2C
  port-enable lines.
- **VHDL says** (`zxnext.vhd:2392, 2418, 2420`):
  `internal_port_enable <= (nr_85 & nr_84 & nr_83 & nr_82 …)`, and bits 10
  (I2C) and 12 (UART) fall in the **NR 0x83** byte (`nr_83_internal_port_enable`).
- **Fix**: corrected pre-Wave-C-landing in the plan text; Wave C's
  `uart_integration_test.cpp` writes NR 0x83 to gate the UART/I2C ports
  (commit `a511386`).

### 4. RTC-06/07 "0x73 BCD read" symptom was already resolved

- **Plan Q5 text**: bundled a potential post-`174fa56` BCD-fix into Wave
  E's scope, on the hypothesis that regs_[2]/regs_[3] might still return
  0x73 (invalid BCD) due to a residual register-pointer-on-STOP bug.
- **VHDL + live trace**: the 0x73 symptom was an observable artefact of
  the pre-`174fa56` false-STOP corruption; after that fix the read path is
  clean. Wave E's scope therefore needed only the write path + 12h + CH
  + NVRAM + control reg, not a BCD-read fix.
- **Fix**: empirical re-audit during Phase 0 flipped RTC-06..10 from
  `skip()` to `check()` with no src/ changes (commit `19a4e41`). The
  plan's Wave E scope row count stayed at 13 because Phase 0 picked up
  RTC-06..10 "for free" and Wave E shipped RTC-11..17 + I2C-11 (8 flips);
  total across Phase 0 + Phase 2 still 13. Plan doc's Phase 2 actuals
  section documents this (commit `7c50b0c`).

## Bugs discovered + fixed during execution

Four emulator bugs surfaced across Phases 1 + 2 — all in the Phase 1
scaffold. Two were caught by the retrospective critic after the Phase 1
merge (`3556a01`), and two more were caught by Phase 2 Wave A/B critics
after their wave merges (`487928c`), then closed by un-deferring the
previously-blocked flips (`b86de83`).

### (a) RX edge-detector XOR order — `3556a01`

- **Symptom**: `rx_edge_` computed against the *post-update* `rx_d_`
  instead of the prior tick's `rx_d_`, causing the start-bit detector to
  miss the falling edge.
- **VHDL oracle**: `uart_rx.vhd:136` (`Rx_d <= Rx` in clocked process) +
  `:140` (`Rx_e <= Rx xor Rx_d` — combinational: compares against the
  prior-tick latched value, because `Rx_d` updates only on the next rising
  edge).
- **Fix** (`src/peripheral/uart.cpp:460-465`): compute `rx_edge_` BEFORE
  the `rx_d_ <- rx_debounced_` assignment (which moved to the tail of
  `rx_engine_step()`), matching the VHDL clocked-process semantics.
- **Rows unblocked**: RX-08/09 (start-bit sampling) — eventually flipped
  together with the sticky-err fix in `b86de83`.

### (b) TX S_IDLE break-line output — `3556a01`

- **Symptom**: when `i_frame(7)=1` (reset) AND `i_frame(6)=0` (non-break),
  the scaffold forced `tx_line_out_ = 1` hard-coded, ignoring the break
  case on a subsequent reset+break combo.
- **VHDL oracle**: `uart_tx.vhd:239` — `S_IDLE => o_Tx <= not i_frame(6)`.
  The idle-line output depends on the break bit of the framing register,
  not a hard-coded '1'.
- **Fix** (`src/peripheral/uart.cpp:293`):
  `tx_line_out_ = !(framing_ & 0x40)` on the reset gate.
- **Rows unblocked**: TX-07 + FRM-05 (break-line output asserted).

### (c) TX parity pre-shift ordering — `487928c`

- **Symptom**: `tx_parity_live_ ^= (tx_shift_ & 0x01)` was being evaluated
  AFTER the shift-register right-shift, so parity captured bit 1 instead
  of bit 0, inverting every byte's computed parity.
- **VHDL oracle**: `uart_tx.vhd:148-159` — the `process (i_CLK)` block
  registers `tx_parity <= tx_parity XOR tx_shift(0)` and
  `tx_shift <= '0' & tx_shift(7 downto 1)` **concurrently** on the same
  clock edge, so parity must capture the **pre-shift** LSB.
- **Fix** (`src/peripheral/uart.cpp:370-384`): moved the parity-update
  block ABOVE the shift-register update; added an in-source comment
  explaining the VHDL concurrency semantics to protect against regression.
- **Rows unblocked**: TX-13/14 (even/odd parity) — flipped in `b86de83`.

### (d) RX sticky-err OR-in on error path — `487928c`

- **Symptom**: when RX framing/parity errors were detected on the
  `S_PARITY → S_ERROR` or `S_STOP_1/2 → S_ERROR` transitions, the
  per-byte `rx_byte_framing_err_` / `rx_byte_parity_err_` flags were set,
  but the sticky `err_framing_` flag (status bit 6) was never OR-ed in —
  the S_ERROR transition orphaned the latch because the byte never
  traversed the `STOP_* → IDLE` path that would normally propagate it.
- **VHDL oracle**: `uart.vhd:541` — sticky error latches on
  `uart0_rx_err_framing OR uart0_rx_err_parity` each tick; cleared only
  by `uart0_fifo_reset` or `uart0_tx_rd_fe`. It does NOT depend on the
  byte committing through STOP.
- **Fix** (`src/peripheral/uart.cpp:580-592`): set `err_framing_ = true`
  on BOTH error-transition branches, additive to the per-byte flag.
- **Rows unblocked**: RX-08/09 (sticky framing/parity error accumulation)
  — flipped in `b86de83`.

## WONT / D-UNOBSERVABLE decisions documented

### BAUD-02 and BAUD-03 — D-UNOBSERVABLE (prescaler LSB not readable)

- **VHDL**: `uart.vhd:323-326` writes the prescaler's low 14 bits via
  half-selective assignment (bits 6:0 or 13:7 depending on input MSB),
  but there is NO VHDL read path that exposes those bits to any port.
  Confirmed by reading `uart.vhd:339-378` register-read process.
- **Disposition**: per `feedback_unobservable_audit_rule`, D-class rows
  (structurally unobservable at the C++ API surface) MUST NOT emit
  `skip()` calls — they get an inline block-comment at the row's
  location explaining why, plus a pointer to the indirect coverage.
- **Inline block** at `test/uart/uart_test.cpp:449-…` covers BAUD-02
  (LSB low-7-bit write dispatch) and BAUD-03 (LSB high-7-bit write
  dispatch) together; indirect coverage via BAUD-07 (post-prescaler
  TX-empty latency end-to-end — observable because it affects the
  timing of subsequent byte transmits).
- **Plan-doc status**: backlog section flagged these as WONT candidates
  before Phase 3; Phase 3's audit confirmed and converted them per the
  feedback rule.

## Backlog / follow-ups unblocked or newly surfaced

1. **Input suite's 6 NR 0x0B UART pin-7 skips** — `test/input/input_test.cpp`
   has 6 remaining skips gated on UART+I2C infrastructure. With the
   Phase-1 bit-level engine and Wave D's joystick-UART mux landed, a
   dedicated re-audit of these 6 rows is now unblocked (one-session
   task). Tracked on the dashboard (`test/SUBSYSTEM-TESTS-STATUS.md:16`:
   "F-blocked — now unblocked; backlog re-audit pending").

2. **Esp8266 AT-command emulation formally unblocked** — the plan's
   `.prompts/2026-04-24.md` dependency note called out that ESP8266 AT
   stack work required a hardened UART 1 TX/RX, which is now in place
   (bit-level engine with CTS flow control, parity, break, error
   latches). Esp8266 peripheral work can proceed.

3. **Bit-level engine is opt-in** — `UartChannel::set_bitlevel_mode(bool)`
   defaults to `false` per Phase 1 design (R1 mitigation in the plan's
   risks section). Any production integration (e.g. the Emulator TCP
   bridge) that wants byte-level loopback retains the existing behaviour;
   any production integration wanting full bit-accurate simulation must
   opt in deliberately. `uart_test.cpp` flips the bit-level mode per-row;
   the 58 pre-plan passing rows remained green under byte-level semantics
   throughout Phase 2 (no regression).

4. **Agent-tool worktree-caching bug (session-level)** — see next section.

5. **Save/load state schema versioning for FifoBuffer + I2cRtc** — R2/R3
   in the plan risks table. Phase 1 added version-byte prefixes so pre-v2
   snapshots still load via legacy 8-bit FIFO / 8-register RTC paths.
   Rewind buffer is in-process; no on-disk compat story needed. Tracked
   but not verified under live compatibility load testing — backlog item
   for any future snapshot-format change.

## Agent-tool worktree-caching bug surfaced by this plan

Per `feedback_agent_worktree_stale_base.md` (recorded 2026-04-24 from
this session): the 5 parallel Phase-2 agent worktrees launched in rapid
succession showed a cache-invalidation race — only Waves A and B got the
current main tip (`3556a01`). Waves C/D/E branched from pre-everything-today
main (`d6348ff`), which caused:

- Missing Phase 0 re-home comments (re-landed pre-Phase-0 skip counts of
  106 instead of 94).
- Missing Phase 1 scaffold (Wave E re-implemented the `I2cRtc` expansion
  from scratch against a stale baseline).
- Heavy merge conflicts at Wave E (scaffold + transfer-path both in
  `i2c.{h,cpp}`); light conflicts at Wave D (new file + new method, no
  scaffold overlap).

The most visible fallout: Wave E's merge-conflict resolution **left behind**
7 stale `skip("RTC-11..17", …)` calls at `test/uart/uart_test.cpp:1785-1805`
ABOVE the new `check()` rows at lines 1844-1989 (which were already
passing). Phase 3 detected and deleted the stale skips (commit `7c50b0c`).

The standing mitigation (`feedback_agent_worktree_stale_base.md`) is to
brief every agent to rebase onto current main and report the base commit
used before starting work.

## Commits landed (Task 3 UART + I2C plan)

```
Plan doc:     22ab4d2 (TASK3-UART-I2C-SKIP-REDUCTION-PLAN.md authored)
Phase 0:      19a4e41 → 0165bf0 (merge)
Phase 1:      dc0566b → 48bb49e (merge) + 3556a01 (retrospective critic fixes)
Wave A:       dd99fa3 → dd7fe8d (merge)
Wave B:       7c7254c → 33404a0 (merge)
Wave C:       a511386 → e1ef2cb (merge)
Wave D:       5afa5ec → 9956197 (merge)
Wave E:       f38457b → f47cdd0 (merge)
Wave-critic:  487928c (TX parity pre-shift + RX sticky-err scaffold fix)
Un-defer:     b86de83 (TX-09/10/13/14 + RX-08/09 flip after scaffold fix)
Phase 3/4:    7c50b0c (delete 7 stale skips + BAUD-02/03 D-UNOBSERVABLE +
              dashboards + plan-doc Phase 2 actuals absorbed)
```

All Phase-1 and Phase-2 merges passed independent critic review before
landing on main. Scaffold bugfix commits (`3556a01`, `487928c`) and the
un-defer flip (`b86de83`) were produced as direct emulator/test-code
commits against main after the cross-wave retrospective critic completed.

## Final verdict — CLOSED

- `uart_test`: **92 / 92 / 0 / 0** — zero runtime skips.
- `uart_integration_test`: **12 / 12 / 0 / 0** (new suite).
- Aggregate: **3303 / 3087 / 0 / 216 across 27 suites**.
- Regression: **34/0/0**. FUSE Z80: **1356/1356**.

The UART + I2C/RTC subsystem is now production-grade for:

- Byte-level UART TX/RX (pre-existing, 58 rows preserved).
- Bit-level UART TX/RX (opt-in: `set_bitlevel_mode(true)`): state
  machines for S_IDLE/S_RTR/S_START/S_BITS/S_PARITY/S_STOP_1/S_STOP_2
  (TX) and S_IDLE/S_START/S_BITS/S_PARITY/S_STOP_1/S_STOP_2/S_ERROR/S_PAUSE
  (RX); noise-rejection debouncer; mid-bit baud re-sync; 9-bit FIFO with
  per-byte error bit; CTS/RTR hardware flow control; break line output;
  sticky framing/parity error accumulator cleared by FIFO-reset or
  status-read side effect.
- I2cRtc full DS1307 model: BCD clock regs 0x00-0x06, control reg 0x07,
  NVRAM 0x08-0x3F, CH (oscillator halt) bit, 12h/24h mode, write path
  with per-register validation, auto-increment wrap at 0x3F → 0x00.
- `I2cController::set_pi_i2c1_scl/sda` AND-gate inputs per
  `zxnext.vhd:3259, 3266`; `Emulator::inject_pi_i2c1` harness hook.
- IM2 UART interrupt dispatch on vectors 1 (UART 0 RX), 2 (UART 1 RX),
  12 (UART 0 TX), 13 (UART 1 TX), with `rx_near_full` OR-bypass of the
  `nr_c6_int_en_2_*(1)` gate per `zxnext.vhd:1942-1943`.
- NR 0x82/0x83/0x84/0x85 `internal_port_enable` gating around the UART
  port handlers (bit 10 = I2C, bit 12 = UART) at `emulator.cpp:1444-1464`.
- Dual-UART channel routing + joystick-UART mux via `IoMode` + the new
  `Emulator::inject_joy_uart_rx` harness hook.

BAUD-02/BAUD-03 are formally documented as D-UNOBSERVABLE (prescaler LSB
structurally not exposed through any VHDL read path) with indirect
coverage via BAUD-07.

All initially-planned deferred rows were ultimately un-deferred via the
two scaffold-bug fixes. No residual plan debt. No lazy-skips, no
tautologies, no coverage theatre.
