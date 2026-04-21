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
/// Phase 1 scaffold: every new method is a stub. Agent D (Phase 2, Wave 1)
/// fills in the full FSM and NR 0xB2 composition.
class Md6ConnectorX2 {
public:
    Md6ConnectorX2() { reset(); }

    /// VHDL reset: counter=0, both latched connector words = 0.
    void reset();

    /// Install the raw 12-bit state vector driven by the host adapter
    /// (keyboard/gamepad → VHDL JOY_LEFT/JOY_RIGHT). Layout per
    /// zxnext.vhd:3441-3442 — see Joystick.h. Phase 1 stub just stores.
    void set_raw_left(uint16_t bits12);
    void set_raw_right(uint16_t bits12);

    /// 12-bit latched / captured connector word, observable by the
    /// port-1F/37 composer in Joystick. Phase 1 stub returns the raw
    /// (unlatched) value.
    uint16_t joy_left_word()  const;
    uint16_t joy_right_word() const;

    /// NR 0xB2 readback byte per zxnext.vhd:6215. Phase 1 stub = 0.
    uint8_t nr_b2_byte() const;

    /// Per-CLK_CPU-cycle advance of the shared FSM. Phase 1 stub empty.
    /// Agent D fills in the 9-bit counter + 8 live sub-phases per
    /// md6_joystick_connector_x2.vhd:66-193.
    void tick(uint32_t cycles);

private:
    uint16_t raw_left_      = 0x0000;   ///< host adapter input, left
    uint16_t raw_right_     = 0x0000;   ///< host adapter input, right
    uint16_t latched_left_  = 0x0000;   ///< captured JOY_LEFT (high nibble = MD6 extras)
    uint16_t latched_right_ = 0x0000;   ///< captured JOY_RIGHT
    uint16_t state_         = 0;        ///< 9-bit FSM state counter
};
