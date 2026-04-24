# Task 3 — UART + I2C/RTC SKIP-Reduction Plan

Plan authored 2026-04-24. Follows the Task 3 staged plan template established by
`TASK3-CTC-INTERRUPTS-SKIP-REDUCTION-PLAN.md`, `TASK3-INPUT-SKIP-REDUCTION-PLAN.md`,
and `TASK3-ULA-VIDEO-SKIP-REDUCTION-PLAN.md` (0→4 phases, parallel agent waves,
independent critic review per agent, VHDL-as-oracle per
`doc/testing/UNIT-TEST-PLAN-EXECUTION.md`).

## Summary

Close the 48 remaining skips in `test/uart/uart_test.cpp` by (a) re-homing the
11 cross-subsystem rows (INT-*, GATE-*, DUAL-*) and 1 I2C-10 port-gate row to
their rightful owner plans (PortDispatch / Im2Controller-already-landed /
Emulator pin-routing), (b) landing bit-level `UartTx` / `UartRx` state machines
that model the 22 framing/parity/break/flow-control/noise-rejection rows, (c)
filling the I2cRtc feature gaps for the 12 RTC rows (write path, 12h mode, CH
bit, NVRAM 0x08-0x3F, control register) and the 2 remaining I2C-class rows
(I2C-11 pi_i2c1_scl input, I2C-P06 sequential-read re-verification). Expected
outcome: `uart_test.cpp` drops from 106/58/0/48 to approximately 106/98/0/≤5,
with a new `test/uart/uart_integration_test.cpp` covering the re-homed UART
IM2 / port-gate / joystick-mux / pin-routing rows (≈11 rows, all pass).

## Starting state

- `test/uart/uart_test.cpp`: **106 / 58 / 0 / 48** (live main, 2026-04-24,
  dashboard-confirmed at `test/SUBSYSTEM-TESTS-STATUS.md`).
- Aggregate unit (post-ULA-plan close): 26 suites, `3305 / 3041 / 0 / 264`
  (post step-4 of 2026-04-24 session — 3 new scaffolds + 2 re-home appends).
- Regression: 34/0/0. FUSE Z80: 1356/1356.
- Branch: `main`, working tree clean.

### Two already-fixed C-class bugs (plan-doc staleness)

`doc/testing/UART-I2C-TEST-PLAN-DESIGN.md` lines 16-26 claim "48/60 live pass,
12 fail, 46 skip" and list 12 failing rows blocked on two bugs:

1. `src/peripheral/i2c.cpp:101` false-STOP from stale `prev_sda_` —
   **FIXED** in commit `174fa56` (`fix(i2c): remove false STOP detection from
   write_scl`). Unblocked 9 rows (I2C-P03, I2C-P05a, I2C-P05b, RTC-01, RTC-02,
   RTC-04, RTC-05, and the flow-through for RTC-06/07 which remain as
   wall-clock-flake skips).
2. `src/peripheral/uart.cpp:299` select-register read bit 6 vs bit 3 —
   **FIXED** in commit `47ee7e2` (`fix(uart): select-register read returns bit
   3 (0x08) not bit 6 (0x40)`). Unblocked 3 rows (SEL-02, SEL-05, DUAL-02).

Live state is therefore 58 pass / 0 fail, not 48 pass / 12 fail. Phase 0 must
refresh the Current Status block in the plan doc to match reality.

### Stale skip reason strings flagged for re-check

- **I2C-P06** (`uart_test.cpp:1041`): reason says `"i2c.cpp:101 - sequential
  read gated by false-STOP bug; verified in RTC-14"`. The false-STOP bug was
  fixed in `174fa56`; re-evaluate during Phase 0.
- **RTC-08/09/10/14/15** (`uart_test.cpp:1146-1153`): reasons say `"blocked by
  i2c.cpp:101"` which is now a stale cite. The underlying issue is now the
  RTC write path at `i2c.cpp:44` ("Accept writes silently") and the
  snapshot-only / no-writeback model. Reasons must be refreshed to point at
  the real blocker during Phase 0.

### Triage of the 48 skips (categorisation)

Read each `skip()` reason string in `test/uart/uart_test.cpp` and cross-map
against VHDL. Categories per `feedback_unobservable_audit_rule.md`:

| Cluster | Rows | Count | Cat | Size | Phase-2 wave / disposition |
|---|---|---:|---|---|---|
| 1 — UART TX bit-level | TX-05/07/08/09/10/11/12/13/14 | **9** | F | L | Wave A (bit-level TX state machine) |
| 2 — UART RX bit-level | RX-08/09/10/11/12/13/14/15 | **8** | F | L | Wave B (bit-level RX state machine) |
| 3 — Frame-register bit-level | FRM-05/06 | **2** | F | S | Wave A (spill: frame snapshot, break line) |
| 4 — Prescaler bit-level | BAUD-02/03/07 | **3** | F | S | Wave A (spill: prescaler-LSB readback, start-of-tx sampling) |
| 5 — UART IM2 interrupts | INT-01..06 | **6** | F → **RE-HOME** | S | Phase 0 re-home → new `uart_integration_test.cpp`. **Already wired in Emulator** (`emulator.cpp:1658-1666` + `Im2Controller::set_int_en_c6` landed in CTC plan). Observable end-to-end at integration layer. |
| 6 — Port-enable gates | GATE-01/02/03 | **3** | F → **RE-HOME** | S | Phase 0 re-home → `uart_integration_test.cpp`. Requires Emulator to wrap its port-0x103B/0x113B/0x133B/0x143B/0x153B/0x163B handlers with `nr_82/83/84/85_internal_port_enable` gate (currently BYPASSED at `emulator.cpp:1444-1464`). Spawns a narrow follow-up edit in the Emulator's port-registration block. |
| 7 — Dual UART pin/mux | DUAL-05, DUAL-06 | **2** | F → **RE-HOME** | S | Phase 0 re-home → `uart_integration_test.cpp`. DUAL-05 is Emulator-level pin routing (no physical pins — assert hooks exist via `on_tx_byte` / `inject_rx` dispatch). DUAL-06 is joystick IO-mode mux — touches `src/input/iomode.{h,cpp}` from the Input plan. |
| 8 — I2C port-enable gate | I2C-10 | **1** | F → **RE-HOME** | S | Phase 0 re-home → `uart_integration_test.cpp`. Same gate cluster as GATE-01/02 (NR 0x83 bit 2 → `internal_port_enable(10)` → `port_i2c_io_en`). |
| 9 — I2C-11 pi_i2c1 input | I2C-11 | **1** | F | S | Wave E (add optional `I2cController::set_pi_i2c1_scl(bool)` setter mirroring `zxnext.vhd:3259` AND-gate). |
| 10 — I2C-P06 sequential read | I2C-P06 | **1** | **RE-AUDIT** | S | Phase 0: re-verify the reason string — false-STOP bug was fixed. Likely becomes `check()` directly in Phase 0; if still blocked, carry into Wave E with the refreshed reason pointing at the real blocker (probably auto-increment + NACK semantics in the current `I2cRtc`). |
| 11 — RTC feature gaps | RTC-06..17 | **12** | F | M | Wave E (write path, 12h mode, CH bit, NVRAM, control reg, date/month/year re-verify). |

Total accounting: 9 + 8 + 2 + 3 + 6 + 3 + 2 + 1 + 1 + 1 + 12 = **48**. ✓

**Post-Phase-0 expected composition**: 48 skips → 12 re-home (Cluster 5/6/7/8
 = 12) + 1 re-audit (Cluster 10 — I2C-P06) + 35 flippable skips split across
Waves A/B/E.

## Phase 0 — triage, re-home, plan-doc refresh (single agent + critic)

**Actions, all test-code / plan-doc only:**

1. Refresh `doc/testing/UART-I2C-TEST-PLAN-DESIGN.md` "Current status" block
   (lines 16-26) to match the live dashboard: `106 / 58 / 0 / 48` and the two
   fixed bugs cited by commit SHA (`174fa56`, `47ee7e2`) under
   "Historical emulator bugs that this suite used to expose (now fixed)".
2. Convert the 12 cross-subsystem rows (Clusters 5/6/7/8 = INT-01..06,
   GATE-01..03, DUAL-05/06, I2C-10) from `skip()` to `// RE-HOME: see
   test/uart/uart_integration_test.cpp <tag>` comments per
   `feedback_rehome_to_owner_plan.md`. The integration suite + the rows are
   created in Phase 3.
3. Re-audit I2C-P06: re-run the assertion locally (mental walk-through the
   i2c.cpp control flow post-174fa56). If the byte-2 read now returns the
   expected BCD-plausible value, convert to a live `check()` in Phase 3; if
   still blocked, rewrite the reason string to cite the real blocker.
4. Refresh stale skip-reason strings on RTC-08..17 and I2C-P06 to cite the
   real root cause (write-path discard at `i2c.cpp:44`, NVRAM unimplemented,
   12h-mode unimplemented, control-reg always-zero, snapshot-only model) and
   to name the Phase-2 branch that will un-skip the row (e.g.
   `"un-skip via task3-uart-e-rtc"`).
5. RTC-06 / RTC-07 specifically: user flag indicates `regs_[2]/regs_[3]`
   return 0x73 (invalid BCD). Verify whether this is a real i2c.cpp bug
   (wrong register-pointer transfer path) vs the pre-174fa56 corruption
   symptom. Reason string updated accordingly. If the bug is still live, it
   joins Wave E's scope (it's a 3-line fix).

**Expected Phase 0 delta**: `106 / 58 / 0 / 48 → 94 / 58 / 0 / 35` (12 rows
removed from file counter via re-home, 1 potentially flipped via re-audit of
I2C-P06). Independent critic (1 agent) reviews the comment dispositions
against VHDL + the plan-doc refresh.

**Files touched in Phase 0 only**:
- `test/uart/uart_test.cpp` (skip reason refreshes + 12 re-home comments).
- `doc/testing/UART-I2C-TEST-PLAN-DESIGN.md` (current-status block refresh).

## Phase 1 — scaffold (single agent + critic)

**Goal**: land compile-only scaffolding for the bit-level `UartTx` / `UartRx`
state machines and the I2cRtc feature expansion. Existing 58 passes must stay
green — the current byte-level `tick()` path is preserved as a wrapper over
the new bit-level engines until Phase 2.

### 1A — New `UartTxEngine` and `UartRxEngine` classes

Per `uart_tx.vhd:82-247` and `uart_rx.vhd:90-316`, add bit-level state machines
as inner classes of `UartChannel` (keep `src/peripheral/uart.{h,cpp}` — the
class is already well-structured; do NOT split into new files unless the
state-machine surface forces it).

**`UartTxEngine` internal state (per `uart_tx.vhd:84-99`)**:

```cpp
enum class TxState : uint8_t { S_IDLE, S_RTR, S_START, S_BITS, S_PARITY,
                               S_STOP_1, S_STOP_2 };
TxState  state_ = TxState::S_IDLE;
TxState  state_next_ = TxState::S_IDLE;
uint8_t  tx_shift_;           // uart_tx.vhd:88, 8-bit data shift register
uint32_t tx_timer_;           // uart_tx.vhd:91 (PRESCALER_BITS=17)
uint32_t tx_prescaler_snap_;  // uart_tx.vhd:86, snapshot of i_prescaler
uint8_t  tx_bit_count_;       // uart_tx.vhd:94, 3 bits (counts down)
bool     tx_parity_;          // uart_tx.vhd:95
bool     tx_frame_parity_en_; // uart_tx.vhd:84, snapshot of frame(2)
bool     tx_frame_stop_bits_; // uart_tx.vhd:85, snapshot of frame(0)
bool     cts_n_ = true;       // i_cts_n — driven externally
bool     tx_line_out_ = true; // o_Tx
bool     tx_busy_ = false;    // o_busy
```

**Public API additions on `UartChannel`**:

```cpp
void set_cts_n(bool v);              // i_cts_n input for TX flow control
bool tx_line_out() const;            // o_Tx current pin state (for TX-08..10)
bool tx_busy() const;                // o_busy (for FRM-05, TX-11)
void tick_one_bit_clock();           // single CLK_28 tick for bit-level mode
void set_bitlevel_mode(bool en);     // opt-in switch between byte- and bit-level;
                                     // default OFF for Phase 1 compile-only
```

**Rows un-skipped per TX sub-state**:

- `S_IDLE` → `S_START` transition + `o_Tx = 0` output during S_START → **TX-08/09/10** (start bit sample in the TX line trace).
- `S_BITS` data shift (LSB first per `uart_tx.vhd:124`) → **TX-08** (8N1 bit pattern).
- `S_PARITY` with XOR over `tx_shift` and `i_frame(1)` odd/even select (`uart_tx.vhd:156`) → **TX-13/14** (even/odd parity calc).
- `S_STOP_1/S_STOP_2` with `tx_frame_stop_bits_` decision (`uart_tx.vhd:213-226`) → **TX-09** (7E2 second stop bit).
- `S_RTR` wait on `i_cts_n=1 AND i_frame(5)=1` (`uart_tx.vhd:187-192`) → **TX-11/12** (CTS flow control).
- Break output `o_Tx = not i_frame(6)` in `S_IDLE` (`uart_tx.vhd:239`) → **TX-07** and **FRM-05** (break line state, o_busy held).
- `o_busy = 1 when state /= S_IDLE OR i_frame(7)=1 OR i_frame(6)=1` (`uart_tx.vhd:234`) → **FRM-05**.
- FIFO-edge write gate (VHDL-line edge detect) → **TX-05** (held write pulse writes only one byte). Best modelled via the opt-in `set_bitlevel_mode(true)` path where `tick_one_bit_clock` samples the write pulse at rising edge only.
- Frame/prescaler snapshot at `state == S_IDLE` per `uart_tx.vhd:107-114` (falling edge) and `uart_tx.vhd:121-126` (rising edge) → **FRM-06** and **BAUD-07** (mid-byte changes don't affect current TX).

**Rows expected to flip via Wave A**: TX-05/07/08/09/10/11/12/13/14 (9) +
FRM-05/06 (2) + BAUD-07 (1) = **12 rows**.

**`UartRxEngine` internal state (per `uart_rx.vhd:90-113`)**:

```cpp
enum class RxState : uint8_t { S_IDLE, S_START, S_BITS, S_PARITY,
                               S_STOP_1, S_STOP_2, S_ERROR, S_PAUSE };
RxState  state_, state_next_;
uint8_t  rx_shift_;                  // uart_rx.vhd:101
uint32_t rx_timer_, rx_prescaler_snap_;
bool     rx_timer_updated_;          // uart_rx.vhd:105 (mid-bit resync)
uint8_t  rx_bit_count_;
bool     rx_parity_;
uint8_t  rx_frame_bits_;             // 2-bit snapshot (frame 4:3)
bool     rx_frame_parity_en_;
bool     rx_frame_stop_bits_;

// Noise rejection debounce (uart_rx.vhd:119-131, NOISE_REJECTION_BITS=2)
uint8_t  rx_debounce_counter_ = 0;
bool     rx_raw_prev_ = true;        // Rx_d
bool     rx_debounced_ = true;       // Rx (post-debounce)
bool     rx_edge_ = false;           // Rx_e = Rx XOR Rx_d
```

**Rows un-skipped per RX sub-state**:

- `S_PARITY` comparator on `Rx == rx_parity` → **RX-09** (parity error).
- `S_STOP_1/2` with `Rx='0' → S_ERROR` → **RX-08** (framing error, missing stop bit).
- `S_ERROR` with `rx_shift = "00000000"` → `o_err_break='1'` (`uart_rx.vhd:314`) → **RX-10** (break detection).
- 9th-bit storage: the existing FIFO is byte-only. Extend the RX FIFO entry to 9 bits (or store a parallel `rx_err_bits_` array of the same size) to carry `(overflow OR framing)` per-byte; status bit 5 at `uart.vhd:359` uses `rx_o(8) AND rx_avail`. → **RX-11** (9th-bit exposed per-byte).
- Noise-rejection debouncer per `uart_rx.vhd:119-131` — short pulses < 2^NOISE_REJECTION_BITS ticks don't trigger `S_START` → **RX-12**.
- `S_PAUSE` entered on `i_frame(6) = 1` (`uart_rx.vhd:231-232`) → **RX-13** (pause state wait).
- Frame/prescaler snapshot at `S_IDLE` per `uart_rx.vhd:146-152` → **RX-14** (mid-byte config change does not affect current reception).
- `o_Rx_rtr_n = flow_ctrl_en AND fifo_full_almost` per `uart.vhd` flow-control output (search for `uart0_rx_rtr_n <=`) → **RX-15** (hardware flow control out).

**Rows expected to flip via Wave B**: RX-08/09/10/11/12/13/14/15 = **8 rows**.

**Public API additions (RX side)**:

```cpp
void drive_rx_line(bool v);          // i_Rx input — bit-level injection
bool rx_rtr_n() const;               // o_Rx_rtr_n (for RX-15)
bool rx_break() const;               // o_err_break (for RX-10)
void inject_rx_bit_frame(uint8_t byte,
                         bool framing_err,
                         bool parity_err);  // test helper
```

### 1B — I2cRtc feature-gap scaffold

Extend `src/peripheral/i2c.{h,cpp}` `I2cRtc` class (DS1307 model):

```cpp
class I2cRtc : public I2cDevice {
public:
    // ... existing API ...

    // NEW public API (Phase 1 = stub; Phase 2 Wave E = real)
    void set_use_real_time(bool v);      // default true: snapshot from host;
                                         // false = freeze, allow test-writes
    void poke_register(uint8_t addr, uint8_t val);   // test seed
    uint8_t peek_register(uint8_t addr) const;       // test readback

    // DS1307 registers 0-6 are BCD time; reg 7 is control; regs 0x08-0x3F
    // are NVRAM (56 bytes). Expand the storage array.

private:
    // CHANGED from std::array<uint8_t, 8> regs_
    std::array<uint8_t, 64> regs_{};     // 0x00-0x07 clock + control,
                                         // 0x08-0x3F NVRAM per DS1307 datasheet
    // NEW:
    bool    osc_halt_ = false;           // seconds reg bit 7 (CH)
    bool    mode_12h_ = false;           // hours reg bit 6 (12/24)
    bool    use_real_time_ = true;
};
```

**Behaviour changes in `I2cRtc::transfer`**:

- Write path (is_read=false), second-and-subsequent byte: store `data` at
  `regs_[reg_ptr_]` (non-NVRAM registers apply BCD validation; NVRAM 0x08-0x3F
  stores verbatim). Advance `reg_ptr_` by 1, wrapping at 0x3F → 0x00 (DS1307
  datasheet §"Address Autoincrement").
- `snapshot_time()` respects `use_real_time_` (default true for production,
  false for test seeding).
- Hours register 0x02: honour bit 6 when written — 24h mode (bit 6 = 0) vs
  12h mode (bit 6 = 1, with AM/PM at bit 5, hours in bits 4:0 BCD).
- Seconds register 0x00: bit 7 = CH (oscillator halt). When set, subsequent
  `snapshot_time()` calls preserve `regs_[0]` instead of re-sampling.
- Control register 0x07: bits 0-1 = RS0/RS1 (output rate), bit 4 = SQWE, bit
  7 = OUT. Stored verbatim, no side-effects (SQW pin not modelled). Full
  readback.

**Rows expected to flip via Wave E**:

- **RTC-06**: hours reg 24h-mode BCD — fix regs_[2] population bug if the
  0x73 symptom is real.
- **RTC-07**: day-of-week reg — fix regs_[3] population similarly.
- **RTC-08/09/10**: date / month / year read-back — should Just Work once the
  false-STOP fix propagates and snapshot_time populates correctly.
- **RTC-11**: control register 0x07 readback — add storage.
- **RTC-12**: write single reg + read back — add write path at `i2c.cpp:44`.
- **RTC-13**: write 12h mode + read back AM/PM.
- **RTC-14**: sequential read auto-increment — already partially works after
  false-STOP fix; confirm and extend wrap from 0x07 → 0x00 to 0x3F → 0x00.
- **RTC-15**: sequential write auto-increment — lands with RTC-12.
- **RTC-16**: CH bit (seconds reg bit 7) stops the oscillator.
- **RTC-17**: NVRAM 0x08-0x3F read/write round-trip.
- **I2C-11**: optional `pi_i2c1_scl` input — tiny: add `set_pi_i2c1_scl(bool)`
  setter whose value is AND-ed into `read_scl()` per `zxnext.vhd:3259`.

Rows expected to flip via Wave E: RTC-06..17 (12) + I2C-11 (1) +
re-audited I2C-P06 if still blocked (±1) = **13 rows**.

### 1C — Test-only accessors on `UartChannel` + `I2cRtc`

Fenced `// === TEST-ONLY ACCESSORS ===` block per the Input plan's
pattern (Issue #8 re Input critic):

- `UartChannel::tx_engine_state()`, `rx_engine_state()`, `tx_shift_reg()`,
  `rx_shift_reg()`, `tx_bit_count()`, `rx_bit_count()`, `tx_parity_live()`,
  `rx_parity_live()`, `rx_debounce_counter()` — read-only.
- `I2cRtc::use_real_time`, `poke_register`, `peek_register`, `ch_bit()`,
  `mode_12h()`.

Test-only accessors MUST NOT drive production behaviour — documented in a
header comment block.

### 1D — Compile-only integration seam

Phase 1 adds `set_bitlevel_mode(false)` default — so existing tests that drive
byte-level `tick()` + `inject_rx` still pass. Wave A/B/E tests flip the
channel into `set_bitlevel_mode(true)` for the rows that need it.

**Phase 1 gate**: build clean, 58 pass / 0 fail on `uart_test.cpp` unchanged;
critic APPROVE before Phase 2 starts.

## Phase 2 — parallel implementation waves

Wave layout per user-authorised 5-agent budget (per
`feedback_parallel_agent_budget_20260421.md` — defaulted to 3 in subsequent
sessions; confirm-or-widen for this plan in Open Questions).

### Wave 1 (up to 5 parallel agents, ~1 day)

All five waves of Phase 2 are **independent** — no agent depends on another
agent's output because Phase 1 scaffolds the required surface in all five
areas simultaneously. The dependency graph fans out from Phase 1 with no
inter-wave serialisation.

| Agent | Branch | Scope | Rows | VHDL anchors | Size |
|---|---|---|---:|---|---|
| **A** UART TX bit-level | `task3-uart-a-txbitlevel` | `UartTxEngine` full state machine, break, CTS, parity, frame+prescaler snapshot | 12 | `uart_tx.vhd:82-247`, `uart.vhd:234-245` | L |
| **B** UART RX bit-level | `task3-uart-b-rxbitlevel` | `UartRxEngine` full state machine, noise rejection, framing/parity error, break, 9th-bit FIFO, pause, mid-byte sampling, RTR output | 8 | `uart_rx.vhd:90-316`, `uart.vhd:341-378` | L |
| **E** I2C/RTC feature-gap fill | `task3-uart-e-rtc` | `I2cRtc` write path, 12h mode, CH bit, NVRAM 0x08-0x3F, control reg, RTC-06/07 BCD fix, `I2cController::set_pi_i2c1_scl` | 13 | DS1307 datasheet; `i2c.cpp:25-73`; `zxnext.vhd:3259, 3266` | M |

Phase-3 integration-suite work (Waves C + D) covers cross-subsystem rows:

| Agent | Branch | Scope | Rows | VHDL anchors | Size |
|---|---|---|---:|---|---|
| **C** UART integration (INT + GATE + I2C-10) | `task3-uart-c-integration` | new `test/uart/uart_integration_test.cpp` for INT-01..06 + GATE-01..03 + I2C-10 | 10 | `zxnext.vhd:1941-1950, 2418, 2420, 2630-2631, 2639, 5499-5509` | M |
| **D** Dual-UART routing + joystick mux | `task3-uart-d-dualrouting` | Emulator-level UART pin routing + joystick IO-mode multiplex (ties into `src/input/iomode.{h,cpp}` from Input plan); new integration tests for DUAL-05/06 | 2 | `zxnext.vhd:3340-3350` | S |

**Wave C** requires a small but necessary Emulator edit: wrap the port
handlers at `emulator.cpp:1444-1464` in the `internal_port_enable(10)` gate
(I2C) and `internal_port_enable(12)` gate (UART), mirroring the existing
DivMMC pattern at line 1467-1476. That edit is the src/ precondition for
GATE-01/02 and I2C-10.

**Wave D** depends on `src/input/iomode.{h,cpp}` landed by the Input plan.
If `IoMode::nr_0b_joy_iomode_uart_en()` and `joy_iomode_0()` are not yet
exposed, Wave D adds them (small extension — the class already models NR
0x0B). DUAL-05 becomes "Emulator pin-routing present and observable via the
byte-flow hook", DUAL-06 becomes "joystick IO-mode mux observable via
IoMode + Uart channel-0/1 dispatch".

### Agent brief template (mirror CTC+Interrupts)

```
Worktree: /home/jorgegv/src/spectrum/jnext/.claude/worktrees/<branch>
Branch:   <branch> (from task3-uart-0-scaffold)
Files to touch: <enumerated>
VHDL authoritative: <file:line ranges>
Required public API additions: <signatures with VHDL cites>
Rows unblocked: <test IDs>
Out of scope: <enumerated, including other waves>
Rules block:
  - Absolute paths only; no cd outside worktree.
  - No build/cmake/ctest.
  - No test binaries.
  - No git add/commit/push. Main session commits.
  - Report summary diff; flag surfaced bugs; don't silently fix.
```

**Per-agent flow** (mirror CTC+Interrupts Phase 2):

1. Main session creates worktree and launches agent with brief.
2. Agent returns with src/ diff summary.
3. Main session launches an independent critic (different instance) reviewing
   diff against VHDL + row coverage + skip-reason honesty + no-tautology.
4. Critic verdict: APPROVE / APPROVE-WITH-FIXES / REJECT. Author applies
   APPROVE-WITH-FIXES on same branch.
5. Main session runs `LANG=C make -C build unit-test` +
   `bash test/regression.sh` on the branch.
6. Main session commits + merges `--no-ff` into main.

**5 Phase-2 review rounds** (plus 1 Phase-1 scaffold review = 6 total
independent critic rounds).

### Wave-specific detail

#### Wave A — UART TX bit-level (L)

- **States to implement** (from `uart_tx.vhd:97`): `S_IDLE`, `S_RTR`,
  `S_START`, `S_BITS`, `S_PARITY`, `S_STOP_1`, `S_STOP_2`.
- **Transitions** per the VHDL combinational process (`uart_tx.vhd:174-230`).
- **Signals**: `tx_shift` (88), `tx_timer` (91), `tx_bit_count` (94),
  `tx_parity` (95), `tx_prescaler` snapshot (86), `tx_frame_*` snapshots
  (84-85), `tx_timer_expired` (131), `tx_bit_count_expired` (146), `state`
  (98), `state_next` (99), `o_busy` (234), `o_Tx` output case (236-245).
- **Write-pulse edge detect** for TX FIFO (`uart.vhd` around line 798, search
  "uart0_tx_fifo_we <="): sample `uart_tx_wr` rising edge only → unblocks
  **TX-05**.
- **Integration with existing byte-level model**: `set_bitlevel_mode(true)`
  selects the bit-level engine; `false` keeps the existing timed-delay
  behaviour for loopback / TCP bridge use. Must not regress the 58 existing
  passes — verified in Phase-3 regression.
- **Rows unblocked**: TX-05/07/08/09/10/11/12/13/14, FRM-05/06, BAUD-07 = 12.

#### Wave B — UART RX bit-level (L)

- **States** (from `uart_rx.vhd:111`): `S_IDLE`, `S_START`, `S_BITS`,
  `S_PARITY`, `S_STOP_1`, `S_STOP_2`, `S_ERROR`, `S_PAUSE`.
- **Transitions** per `uart_rx.vhd:227-295`.
- **Noise-rejection debouncer** per `uart_rx.vhd:119-131` (imports the
  `debounce` entity — model as a 2-bit counter per
  `NOISE_REJECTION_BITS=2`; short pulses reset).
- **Mid-bit baud-rate re-sync** per `uart_rx.vhd:188-190`.
- **9th-bit FIFO** — extend RX FIFO element width. Options:
  (a) `std::array<uint8_t,512> rx_err_bits_` parallel array indexed by the
      same head/tail pointers — compact,
  (b) store uint16_t in the FIFO, masking at read time.
  Pick (a) for minimal churn in the `FifoBuffer` template.
- **Output flow-control pin**: `o_Rx_rtr_n = framing(5) AND rx_fifo_almost_full`
  — trivial once `almost_full()` is already on `FifoBuffer`.
- **Pause state** on `framing(6)` — entered on reset and on write of
  framing with bit 6 set.
- **Rows unblocked**: RX-08/09/10/11/12/13/14/15 = 8.

#### Wave E — I2C/RTC feature-gap fill (M)

- **RTC write path** (`i2c.cpp:44` "ignored"): replace with
  `regs_[reg_ptr_] = data;` plus per-register validation:
  - reg 0x00: bits 6:0 BCD seconds (validate upper ≤ 5, lower ≤ 9);
    bit 7 sets `osc_halt_`.
  - reg 0x02: bit 6 sets `mode_12h_`; bits 5:0 BCD hours (12h: bit 5 = PM,
    bits 4:0 = hours 1-12).
  - regs 0x08-0x3F: NVRAM, verbatim, no validation.
- **Register pointer wrap**: change `& 0x07` to `& 0x3F` per DS1307 datasheet
  auto-increment.
- **`snapshot_time()` honours `osc_halt_`**: when halted, leave `regs_[0..6]`
  untouched.
- **RTC-06 / RTC-07 0x73 fix**: likely `t->tm_hour` calling
  `to_bcd()` before the pre-174fa56 STOP corruption broke mid-read — after
  the STOP fix it should be clean, but Wave E must verify empirically and
  fix any residual bug (likely a reg-pointer reset-on-STOP vs
  carry-across-repeated-start mismatch).
- **`I2cController::set_pi_i2c1_scl(bool)` / `set_pi_i2c1_sda(bool)`**: two
  `uint8_t` fields defaulting to 1 (released), AND-ed into `read_scl()` /
  `read_sda()` per `zxnext.vhd:3259, 3266`.
- **Rows unblocked**: RTC-06/07/08/09/10/11/12/13/14/15/16/17 (12) + I2C-11 (1)
  + I2C-P06 re-audit flip (±1) = 13.

#### Wave C — UART integration suite (M)

Lives in Phase 3 (per CTC template) but listed here for plan completeness:

1. New `test/uart/uart_integration_test.cpp` — full `Emulator` fixture,
   mirrors `test/ctc_int/ctc_int_integration_test.cpp` structure.
2. **GATE-01** (UART port enable NR 0x82 bit 4 → `internal_port_enable(12)`
   → `port_uart_io_en` per `zxnext.vhd:2420`): write NR 0x82 clearing bit 4,
   verify all four UART ports return 0xFF and ignore writes.
3. **GATE-02** (I2C port enable NR 0x82 bit 2 → `internal_port_enable(10)`
   per `zxnext.vhd:2418`): write NR 0x82 clearing bit 2, verify 0x103B and
   0x113B ignore.
4. **GATE-03** (full NR 0x82/0x83/0x84/0x85 behaviour per
   `zxnext.vhd:5499-5509`): exercise several port-enable bits, confirm
   mapping matches.
5. **INT-01..06**: use `Emulator::uart_.inject_rx(0, byte)` to stimulate
   UART 0 RX; verify `im2_` pending mask sets bit 1 when `NR 0xC6` bit 0
   (RX-avail enable) is set and clears when disabled. Mirror for UART 1
   (vector 2), UART 0 TX (vector 12 after `tick()` drain), UART 1 TX
   (vector 13), and the rx_near_full override (`zxnext.vhd:1942-1943`:
   near-full OR-bypasses the int_en gate).
6. **I2C-10**: same gate pattern as GATE-02.

**Emulator edit required** before Wave C tests can pass: wrap
`emulator.cpp:1444-1464` port handlers with the NR 0x82 / NR 0x83 gate
check (one-line guard per port). This is a ~10-line addition in
`register_io_ports()`. Small, low-risk, mirror of DivMMC pattern at
line 1467-1476.

**Rows unblocked**: INT-01..06 (6) + GATE-01..03 (3) + I2C-10 (1) = 10.

#### Wave D — Dual-UART routing + joystick mux (S)

1. **DUAL-05** (UART 0 = ESP / UART 1 = Pi pin routing): assert that
   `Emulator::uart_.on_tx_byte` dispatch distinguishes channel 0 vs 1 via
   the `channel` parameter. Verify integration with any future ESP / Pi
   bridging hook. For now: assert the byte flows from `write(REG_TX)` with
   UART 0 selected end up in a channel-0 queue, not channel-1.
2. **DUAL-06** (joystick IO-mode UART multiplex per `zxnext.vhd:3340-3350`):
   wire `IoMode::joy_iomode_uart_en_` → Emulator's UART 0 RX input. When
   `joy_iomode_uart_en=1 AND nr_0b_joy_iomode_0=0`, UART 0's `inject_rx`
   path is fed from the joystick connector instead of the physical UART 0
   pin. Test: drive the joystick RX pin, verify UART 0 sees the byte.

Depends on `src/input/iomode.{h,cpp}` API (already landed by Input plan —
verify at Phase 2 kickoff; if missing, minor extension).

**Rows unblocked**: DUAL-05, DUAL-06 = 2.

### Phase 2 totals

| Wave | Rows flipped (check) | New integration rows | Size |
|---|---:|---:|---|
| A — UART TX bit-level | 12 | — | L |
| B — UART RX bit-level | 8 | — | L |
| C — UART integration | — | 10 | M |
| D — Dual routing | — | 2 | S |
| E — I2C/RTC fills | 13 | — | M |
| **Total** | **33** | **12** | |

Aggregate post-Phase-2 projection for `uart_test.cpp`:
`94 / 58 pass / 0 fail / 35 skip → 94 / 91 pass / 0 fail / 3 skip` (with 33
skip→check flips in Waves A+B+E).

New `uart_integration_test.cpp`: 12/12/0/0.

## Phase 3 — un-skip + integration suite wiring (self)

1. Main session flips `skip()` → `check()` for every row unblocked by a
   merged Phase-2 branch. Each new `check()` cites VHDL `file:line`.
2. Create `test/uart/uart_integration_test.cpp` covering Waves C + D rows
   (12 rows total).
3. Update `test/CMakeLists.txt` to register the new integration binary.
4. Run `LANG=C make -C build unit-test` + `bash test/regression.sh` on
   main. Verify no new fails. Aggregate unit target:
   `~3305 / ~3093 / 0 / ~217` (+33 pass, -47 skip relative to ULA-close
   baseline plus today's scaffold-merge state).
5. Launch 1 independent critic for the integration-test file.

**Expected Phase 3 state**:
- `uart_test`: 94 / 91 / 0 / 3 (the 3 remaining skips are WONT candidates —
  see backlog section).
- `uart_integration_test`: 12 / 12 / 0 / 0.

## Phase 4 — dashboard + audit (self)

Mirror `task3-ula-phase4.md` structure:

1. Update `test/SUBSYSTEM-TESTS-STATUS.md`:
   - `UART + I2C/RTC` row → new numbers.
   - Add new `UART (integration)` row.
   - Recompute aggregate totals.
2. Update `doc/testing/TRACEABILITY-MATRIX.md` — per-row statuses, new
   `UART (int)` summary row.
3. Update `doc/testing/UART-I2C-TEST-PLAN-DESIGN.md`:
   - Current Status block reflects final numbers.
   - Per-group rows annotated with live/re-home status.
4. Update `doc/design/EMULATOR-DESIGN-PLAN.md` Phase-9 UART+I2C entry
   to `[~]` with plan summary.
5. Independent audit agent (different instance, per
   `feedback_never_self_review.md`) produces
   `doc/testing/audits/task3-uart-i2c-phase4.md` with the full plan record
   (phase summary table, per-cluster outcome, any plan-doc inconsistencies
   surfaced mid-wave, final row counts).
6. Update `.prompts/<today>.md` Task Completion Status if one exists for
   today.

## Expected outcome projection

| Suite | Before (2026-04-24) | After Phase 0 | After Phase 2 (Wave A/B/E) | After Phase 3 (+C/D integration) |
|---|---:|---:|---:|---:|
| `uart_test` total | 106 | 94 | 94 | 94 |
| `uart_test` pass | 58 | 58 | 91 | 91 |
| `uart_test` skip | 48 | 35 | 3 | 3 |
| `uart_integration_test` | — | — | — | 12/12/0/0 |
| Aggregate pass | 3041 | 3041 | 3074 | 3086 |
| Aggregate skip | 264 | 264 | 231 | 231 |
| Regression | 34/0/0 | 34/0/0 | 34/0/0 | 34/0/0 |
| FUSE Z80 | 1356/1356 | 1356/1356 | 1356/1356 | 1356/1356 |

Per-wave projections:
- Wave A lands 12 rows — TX-05/07/08/09/10/11/12/13/14, FRM-05/06, BAUD-07.
- Wave B lands 8 rows — RX-08..15.
- Wave E lands 13 rows — RTC-06..17, I2C-11, I2C-P06 if re-audit flips.
- Wave C lands 10 rows in the new integration suite.
- Wave D lands 2 rows in the new integration suite.

**Final state on `uart_test.cpp`**: ~3 skips — all WONT candidates or
upstream-NMI-style blocked rows. See backlog section.

## Open questions for user

1. **5-agent parallel budget authorisation**. The CTC+Interrupts plan ran on
   a 5-agent Wave 1 (per `feedback_parallel_agent_budget_20260421.md`). That
   memory is session-scoped to 2026-04-21. Confirm or widen budget for this
   plan. Default (if not widened): 3 concurrent, forcing Waves A/B/E
   serialised into two passes.

2. **Prompt vs VHDL — vector numbering correction**. The kickoff prompt lists
   the 5 IM2 interrupt vectors as "1 (UART 0 RX), 2 (rx_near_full override),
   12 (UART 1 RX), 13 (UART 0 TX)". Per `zxnext.vhd:1941-1944`, the actual
   priority map is:
   - vector 1 = `uart0_rx_near_full OR (uart0_rx_avail AND NOT nr_c6_210(1))`
   - vector 2 = `uart1_rx_near_full OR (uart1_rx_avail AND NOT nr_c6_654(1))`
   - vector 12 = `uart0_tx_empty`
   - vector 13 = `uart1_tx_empty`
   The "rx_near_full override" is not a separate vector — it's an OR inside
   vectors 1 and 2. Prompt text is imprecise; plan row INT-02 ("RX near-full
   always triggers") is the right assertion, just at vector 1 (UART 0) not
   vector 2. Confirm this interpretation.

3. **Bit-level TX/RX engine fold strategy**. Proposed: extend existing
   `UartChannel` with `set_bitlevel_mode(bool)` opt-in. Alternative:
   split into dedicated `UartTxEngine` / `UartRxEngine` files (cleaner but
   bigger diff). Recommendation: extend in place; re-evaluate if Wave A's
   critic pushes back on class size.

4. **RX FIFO 9th-bit storage**. Proposed: parallel `rx_err_bits_` array
   indexed by same head/tail. Alternative: change FIFO element from uint8_t
   to uint16_t and mask at read time. Either works; parallel array is
   smaller diff.

5. **RTC BCD regs_[2]/[3] 0x73 symptom**. Is this a real post-174fa56 bug
   or was it resolved by that fix? Wave E assumes it might still be live
   and bundles the fix in the RTC write-path extension. If the symptom is
   gone, RTC-06/07 flip to `check()` in Phase 0's re-audit sweep with no
   src/ change.

6. **`I2cController::set_pi_i2c1_scl` / `set_pi_i2c1_sda` lifecycle**. No
   Emulator consumer today drives these inputs — they default to 1
   (released), matching the VHDL pull-up. Should they be exposed on
   `Emulator` too for test harness access? Proposed: yes, wrapped as
   `Emulator::inject_pi_i2c1(bool scl, bool sda)` for Wave E test rows
   that need to verify the AND-gate behaviour (I2C-11).

7. **Wave D dependency on `src/input/iomode.{h,cpp}`**. The Input plan
   landed this file (per `project_task3_input_phase4_landed`). Confirm
   the exposed API: `set_nr_0b(byte)` setter, plus a readable accessor
   for `joy_iomode_uart_en` and `nr_0b_joy_iomode_0`. If accessors are
   missing, Wave D adds them (small extension, low risk).

## Backlog / WONT candidates (DO NOT pre-decide)

Flag for user review at Phase-4 close. Do **not** convert to WONT inside
this plan — these are proposals, not decisions.

1. **TX-05 write-pulse edge detect** (`uart.vhd` TX FIFO write gate). Even
   with bit-level mode, the C++ `write()` call is atomic from the test's
   perspective — the edge detect is VHDL-internal. Wave A will attempt to
   model via a per-tick "write-pulse held" flag. If the row can't be
   legitimately exercised after bit-level lands, **propose WONT** with
   rationale: "jnext `Uart::write()` is discrete; VHDL write-pulse
   edge detection is internal pipeline with no software consumer that can
   observe it via port I/O. Any host software that wanted to write twice
   per pulse would need sub-CLK_28 granularity which the CPU cycle cannot
   express anyway."

2. **BAUD-02/03 prescaler LSB readback**. The 14-bit prescaler LSB is
   write-only per VHDL (`uart.vhd:321-336`). Test harness has no way to
   read it back through the public API — it can only observe baud-rate
   effects over many TX ticks. Wave A's bit-level engine exposes the
   snapshot indirectly (tick counts per bit), which makes BAUD-07
   checkable. BAUD-02/03's "which 7 bits went where" is an
   internal-register read-modify-write and is probably a **WONT
   candidate** with rationale: "prescaler LSB is structurally write-only
   per VHDL `uart.vhd:321-336`; bit distribution between lower-7 and
   upper-7 halves cannot be observed through any read port. Outcome
   equivalence is covered by BAUD-04/05/06 (MSB readback) + BAUD-07
   (transfer-time correctness)."

3. **RTC-17 NVRAM 0x08-0x3F**. No NextZXOS firmware path known to use
   the DS1307 NVRAM. Implementing is trivial (storage is already extended)
   but if no host software exercises it the test-value is thin. **Flag
   as "implement-but-reconsider-if-maintenance-cost-rises"** — probably
   stays live.

4. **DUAL-05 UART pin routing** — no physical pin model exists. Wave D
   tests it via Emulator-layer byte-flow hooks. If the test assertion
   reduces to "channel-0 bytes go to channel-0 consumer", that's
   tautological. **Propose WONT or G-comment** if Wave D can't produce
   a non-tautological assertion. Fallback: re-frame as
   "channel-0 `on_tx_byte` callback fires with channel=0, not channel=1"
   (observable, non-tautological).

5. **TX-11/12 CTS flow control without a real CTS source**. Wave A adds
   `UartChannel::set_cts_n(bool)` which is testable directly. Lives in
   the unit suite. Integration-layer exercise (e.g. through a modem
   emulator) is out of scope for this plan. No WONT proposal — Wave A
   delivers the real test.

## Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | Bit-level `UartTxEngine` breaks the existing byte-level loopback used by non-test consumers (emulator TCP bridge) | Opt-in switch `set_bitlevel_mode(bool)` defaults to false. Existing 58 passes run byte-level path; only Wave A tests flip bit-level on. Regression catches any leak. |
| R2 | 9th-bit FIFO storage breaks save/load state schema | Add a schema version byte at the head of `FifoBuffer::save_state`; on load, version < 2 uses old 8-bit path. Rewind buffer is in-process, no on-disk compat required. |
| R3 | I2cRtc `regs_` size change from 8 to 64 bytes breaks save/load | Same version-byte pattern as R2. I2cRtc `save_state/load_state` already writes `regs_.size()` bytes. |
| R4 | Wave E reveals the RTC-06/07 0x73 symptom is actually a deeper i2c.cpp state-machine bug (e.g. register pointer reset on repeated-START inside a write+start sequence) | Triage during Wave E. If the bug is larger than a 20-line fix, spawn a dedicated branch; otherwise fold into Wave E. |
| R5 | Wave C's Emulator edit (port-gate wrap) exposes an existing test that relies on the ungated behaviour | Grep `test/` for direct port-0x103B/0x113B/0x133B-0x163B use from `Emulator` fixtures. If anything relies on ungated access, update the test to set the enable bits (defaults are 0xFF at reset — the gate is a no-op in default state, so regression should stay clean). |
| R6 | Wave D depends on `src/input/iomode.{h,cpp}` API that might not expose all needed accessors | At Phase 2 kickoff, re-verify the IoMode public surface. If accessors are missing, Wave D folds in a minimal extension (≤20 lines). |
| R7 | Prompt-vs-VHDL vector numbering confusion propagates into test code | Plan doc cites VHDL explicitly. Wave C test author briefs cite `zxnext.vhd:1941-1944` verbatim. Critic checks. |

## Kickoff checklist

Before Phase 0 launches, main session confirms:

- [ ] Full unit-test baseline matches dashboard (`106 / 58 / 0 / 48` on
      `uart_test`; 26 suites total).
- [ ] Full regression 34/0/0; FUSE 1356/1356.
- [ ] User ruling on the 7 Open Questions above (at minimum: budget +
      vector-numbering correction + class-split strategy).
- [ ] Confirm `src/input/iomode.{h,cpp}` surface for Wave D.

## Kickoff order

1. Phase 0 — single agent, ~1h.
2. Phase 1 — single agent, ~3h.
3. Phase 2 Wave 1 — 3 or 5 parallel agents (A, B, E), ~1 day, inline
   critic review per agent.
4. Phase 3 — single agent (integration suite + un-skip wrap), ~4h, +1
   integration-suite critic.
5. Phase 4 — single agent + independent audit agent, ~2h.

Total: ~2 days active session time (or one long sitting under 5-parallel
budget).

### Critical Files for Implementation

- /home/jorgegv/src/spectrum/jnext/src/peripheral/uart.h
- /home/jorgegv/src/spectrum/jnext/src/peripheral/uart.cpp
- /home/jorgegv/src/spectrum/jnext/src/peripheral/i2c.h
- /home/jorgegv/src/spectrum/jnext/src/peripheral/i2c.cpp
- /home/jorgegv/src/spectrum/jnext/test/uart/uart_test.cpp
- /home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp (Wave C port-gate wrap + Wave D joystick-UART mux)
