#pragma once
#include <cstdint>

/// MD6 (Mega-Drive 3/6-button) dual-connector joystick state machine.
///
/// Mirrors VHDL `md6_joystick_connector_x2.vhd:66-193` — a single shared
/// 9-bit state counter drives a time-multiplexed FSM that scans the left
/// and right joystick connectors alternately, capturing the extra 4 MD6
/// buttons (MODE/X/Z/Y) into the high nibble (bits 11:8) of the per-
/// connector 12-bit `JOY_LEFT` / `JOY_RIGHT` vectors.
///
/// Per critic Issue #5 of the Input SKIP-reduction plan: this is ONE
/// class holding BOTH connectors' state — do NOT instantiate two copies.
/// The VHDL module is a single entity serving both joysticks.
///
/// NR 0xB2 read composition per zxnext.vhd:6215:
///   port_253b_dat <= JOY_RIGHT(10 downto 8) & JOY_RIGHT(11)
///                 &  JOY_LEFT(10 downto 8) & JOY_LEFT(11);
///
/// Output bit layout of `joy_left_word()` / `joy_right_word()` per
/// zxnext.vhd:3441-3442 (active-high):
///   bit 11 = MODE   bit 10 = X   bit  9 = Z   bit  8 = Y
///   bit  7 = START  bit  6 = A
///   bit  5 = C(F2)  bit  4 = B(F1)
///   bit  3 = U      bit  2 = D   bit  1 = L   bit  0 = R
class Md6ConnectorX2 {
public:
    Md6ConnectorX2() { reset(); }

    /// VHDL reset: counter=0, both latched connector words = 0 (active-
    /// high "no button pressed"). Per VHDL `o_joy_left <= (others => '0')`
    /// at reset (md6_joystick_connector_x2.vhd:185).
    void reset();

    /// Install the raw 12-bit state vector driven by the host adapter
    /// (keyboard/gamepad → VHDL JOY_LEFT/JOY_RIGHT). Layout per
    /// zxnext.vhd:3441-3442.
    void set_raw_left(uint16_t bits12);
    void set_raw_right(uint16_t bits12);

    /// 12-bit latched connector word, observable by the port-1F/37
    /// composer in Joystick. This is the VHDL `o_joy_left` / `o_joy_right`
    /// (active-high). Returns the LATCHED value produced by the FSM —
    /// not the raw input. Callers must run the FSM (via `tick()` or the
    /// test-only helpers) to refresh the latches from the raw inputs.
    uint16_t joy_left_word()  const;
    uint16_t joy_right_word() const;

    /// NR 0xB2 readback byte per zxnext.vhd:6215:
    ///   {R.X, R.Z, R.Y, R.MODE, L.X, L.Z, L.Y, L.MODE}
    /// Bit 7..0 = JOY_RIGHT(10..8) & JOY_RIGHT(11) & JOY_LEFT(10..8) & JOY_LEFT(11).
    /// MODE-INDEPENDENT: NR 0xB2 reflects the MD6 latches regardless of
    /// Joystick::Mode (no NR 0x05 gating in VHDL at this composition).
    uint8_t nr_b2_byte() const;

    /// Per-CPU-tick advance of the shared FSM. Accumulates cycles to
    /// approximate the VHDL `i_CLK_EN` enable rate (one enable per
    /// ~4.57us at i_CLK_28 per md6_joystick_connector_x2.vhd:33). At
    /// each enable, the 9-bit state counter advances by 1 and the live
    /// sub-phase action fires per md6_joystick_connector_x2.vhd:133-173.
    void tick(uint32_t cycles);

    // ==========================================================
    // Test-only accessors — exercised by input_test.cpp MD6-11*
    // rows to drive the FSM to a specific phase deterministically.
    // Production code MUST NOT call these.
    // ==========================================================

    /// Force the 9-bit state counter to a specific value. Masked to 9 bits.
    void set_state_for_test(uint16_t state_value);

    /// Read the current 9-bit state counter.
    uint16_t state_for_test() const { return state_; }

    /// Advance the FSM by exactly one step: apply the case-phase action
    /// for the CURRENT state value, then increment the state counter by
    /// 1. Mirrors the single-clock-edge behaviour of
    /// md6_joystick_connector_x2.vhd:103-114 + 123-179 where the case
    /// block operates on `state(3 downto 0)` and `state_next = state + 1`
    /// is committed at the same rising edge.
    void step_fsm_once_for_test();

    /// Six-button detect flags (internal state, active-high for "this
    /// connector is a 6-button pad"). Exposed for MD6-11d/11i test rows.
    bool six_button_left_for_test()  const { return six_button_left_; }
    bool six_button_right_for_test() const { return six_button_right_; }

    /// Directly seed the 12-bit latched connector words, bypassing the
    /// FSM. Used by the MD6-01..09 test rows that target NR 0xB2 byte
    /// composition (zxnext.vhd:6215) independently of the state machine.
    /// Each value is masked to 12 bits.
    void set_latched_left_for_test(uint16_t bits12)  { latched_left_  = bits12 & 0x0FFFu; }
    void set_latched_right_for_test(uint16_t bits12) { latched_right_ = bits12 & 0x0FFFu; }

    /// Seed the six-button-detect flags directly. Used by MD6-11e / 11i
    /// rows to exercise the bits 11:8 latch gate at phase 1010/1011
    /// without first driving through phase 1000/1001.
    void set_six_button_left_for_test(bool v)  { six_button_left_  = v; }
    void set_six_button_right_for_test(bool v) { six_button_right_ = v; }

private:
    /// Apply the case-phase action for a given 4-bit phase. Pure
    /// combinational in our model — no separate active-low register
    /// layer. The live phases mirror the VHDL case block verbatim; the
    /// 3-button-vs-6-button handshake is reduced to its observable
    /// effect on latched_*.
    void apply_phase_action(uint8_t phase);

    /// True when state(8..4) = 0 (any of state bits 4-8 set). Mirrors
    /// the VHDL `state_rest` signal at line 100. When the FSM is in a
    /// rest window the case block does not fire.
    bool in_rest_window() const;

    uint16_t raw_left_      = 0x0000;   ///< host adapter input, left
    uint16_t raw_right_     = 0x0000;   ///< host adapter input, right
    uint16_t latched_left_  = 0x0000;   ///< captured JOY_LEFT (active-high)
    uint16_t latched_right_ = 0x0000;   ///< captured JOY_RIGHT (active-high)
    uint16_t state_         = 0;        ///< 9-bit FSM state counter

    // Internal 6-button detect flags (latched at phase 1000/1001, used
    // to gate the bits-11:8 latch at phase 1010/1011). In the VHDL the
    // equivalent signals are `joy_left_six_button_n` / `joy_right_six_button_n`
    // and active-low — here we flip the polarity to active-high for
    // readability: true == "6-button pad detected, extras latch will fire".
    bool six_button_left_  = false;
    bool six_button_right_ = false;

    // CLK_EN accumulator — see `tick()` comment. VHDL
    // `md6_joystick_connector_x2.vhd:33` documents `i_CLK_EN` as firing
    // every ~4.57us; at 28 MHz that is ~128 master cycles per enable.
    // `Emulator::run_frame` feeds `tick()` in the 28 MHz master-cycle
    // domain (same domain CTC and UART consume), so the prescaler
    // constant is 128, not 16 (16 would be the equivalent count in the
    // 3.5 MHz Z80 CPU-cycle domain — wrong units for this call site).
    // The test-only helpers bypass this accumulator entirely.
    uint32_t clk_en_accum_ = 0;
    static constexpr uint32_t kCyclesPerClkEn = 128;
};
