#include "input/md6_connector_x2.h"

// =============================================================================
// Md6ConnectorX2 — dual-connector MD 3/6-button joystick FSM.
//
// VHDL oracle: md6_joystick_connector_x2.vhd:66-193 (full file).
// NR 0xB2 composition oracle: zxnext.vhd:6215.
// 12-bit signal layout: zxnext.vhd:3441-3442.
//
// Implementation strategy
// -----------------------
// The VHDL implements a 9-bit state counter driven by i_CLK_28/i_CLK_EN with
// a select-pulse protocol that latches different physical pins (pin1..pin9)
// at different phases of state(3:0). The internal active-low latches
// `joy_left_n` / `joy_right_n` are exposed on output as `not joy_*_n` during
// state_rest.
//
// Our emulator works at a higher level: the host adapter (keyboard/gamepad UI)
// already composes a 12-bit active-high vector per connector (`raw_left_` /
// `raw_right_`). We therefore simplify the FSM by:
//   1. Collapsing the active-low internal latch register + rest-window
//      publication into a single `latched_*_` field updated directly at each
//      live phase.
//   2. Dropping the physical-pin handshake checks that would be inherent in a
//      real cable — our host adapter is expected to present a well-formed
//      12-bit state. Exception: the 3-vs-6-button detection at phase 1000/1001
//      IS preserved, because plan row MD6-11i exercises it — we interpret the
//      VHDL handshake "U AND D both held at phase 1000" against the raw input
//      just like the FSM checks pins 1 and 2.
//   3. Exposing `set_state_for_test()` / `step_fsm_once_for_test()` so unit
//      tests can drive the FSM to an exact phase. This matches the
//      `Joystick::set_mode_direct` test-harness idiom from Phase 1.
//
// What this preserves
// -------------------
// * NR 0xB2 byte layout (zxnext.vhd:6215).
// * 12-bit JOY_LEFT / JOY_RIGHT bit layout (zxnext.vhd:3441-3442).
// * Per-phase latch of the different bit slices (bits 7:6, 5:0, 11:8).
// * 3-button-pad "extras not latched" behaviour (MD6-11i).
// * Mode-independence: NR 0xB2 reads only from latched_*, never gated by
//   NR 0x05 (MD6-10).
// * Reset defaults: latched words all zero (active-high, no buttons pressed).
// =============================================================================

void Md6ConnectorX2::reset()
{
    raw_left_      = 0x0000;
    raw_right_     = 0x0000;
    latched_left_  = 0x0000;   // active-high; VHDL o_joy_* reset per :185-186
    latched_right_ = 0x0000;
    state_         = 0;
    six_button_left_  = false;
    six_button_right_ = false;
    clk_en_accum_  = 0;
}

void Md6ConnectorX2::set_raw_left(uint16_t bits12)
{
    raw_left_ = bits12 & 0x0FFF;
}

void Md6ConnectorX2::set_raw_right(uint16_t bits12)
{
    raw_right_ = bits12 & 0x0FFF;
}

uint16_t Md6ConnectorX2::joy_left_word() const
{
    return latched_left_;
}

uint16_t Md6ConnectorX2::joy_right_word() const
{
    return latched_right_;
}

// -------------------------------------------------------------------------
// NR 0xB2 read composition — zxnext.vhd:6215.
//
//   port_253b_dat <= i_JOY_RIGHT(10 downto 8) & i_JOY_RIGHT(11)
//                 &  i_JOY_LEFT (10 downto 8) & i_JOY_LEFT (11);
//
// Bit 7 = R.X (JOY_RIGHT bit 10)
// Bit 6 = R.Z (JOY_RIGHT bit  9)
// Bit 5 = R.Y (JOY_RIGHT bit  8)
// Bit 4 = R.MODE (JOY_RIGHT bit 11)
// Bit 3 = L.X (JOY_LEFT bit 10)
// Bit 2 = L.Z (JOY_LEFT bit  9)
// Bit 1 = L.Y (JOY_LEFT bit  8)
// Bit 0 = L.MODE (JOY_LEFT bit 11)
//
// MODE-INDEPENDENT: no NR 0x05 / Joystick::Mode gating. Matches plan row
// MD6-10 ("Kempston mode, L.X=1 still sets NR 0xB2 bit 3 — no gating").
// -------------------------------------------------------------------------
uint8_t Md6ConnectorX2::nr_b2_byte() const
{
    const uint16_t L = latched_left_;
    const uint16_t R = latched_right_;

    auto bit = [](uint16_t v, unsigned pos) -> uint8_t {
        return static_cast<uint8_t>((v >> pos) & 1u);
    };

    const uint8_t rx   = bit(R, 10);   // R.X
    const uint8_t rz   = bit(R,  9);   // R.Z
    const uint8_t ry   = bit(R,  8);   // R.Y
    const uint8_t rm   = bit(R, 11);   // R.MODE
    const uint8_t lx   = bit(L, 10);   // L.X
    const uint8_t lz   = bit(L,  9);   // L.Z
    const uint8_t ly   = bit(L,  8);   // L.Y
    const uint8_t lm   = bit(L, 11);   // L.MODE

    return static_cast<uint8_t>(
        (rx << 7) | (rz << 6) | (ry << 5) | (rm << 4) |
        (lx << 3) | (lz << 2) | (ly << 1) | (lm << 0)
    );
}

// -------------------------------------------------------------------------
// FSM — state counter + per-phase actions.
// Oracle: md6_joystick_connector_x2.vhd:98-114, 123-179.
// -------------------------------------------------------------------------

bool Md6ConnectorX2::in_rest_window() const
{
    // VHDL line 100: state_rest <= state(8) or state(7) or state(6) or state(5) or state(4)
    return (state_ & 0x1F0u) != 0u;
}

void Md6ConnectorX2::apply_phase_action(uint8_t phase)
{
    // Case block at md6_joystick_connector_x2.vhd:133-175. Only fires when
    // state_rest = '0' (VHDL :131). Each phase updates a slice of the
    // active-high latched_* vector. Raw_* is indexed per the 12-bit layout
    // at zxnext.vhd:3441-3442.
    //
    // Note on polarity: the VHDL internally drives active-low `joy_*_n`
    // and publishes `o_joy_* <= not joy_*_n` during rest. Our latched_* is
    // the active-high equivalent, so:
    //   * VHDL "joy_*_n <= (others => '1')" (no buttons)   → latched_* = 0
    //   * VHDL "joy_*_n(7:6) <= i_joy_9_n & i_joy_6_n"     → latched_*(7:6) = raw_*(7:6)
    //   * VHDL "joy_*_n(5:0) <= joy_raw"                   → latched_*(5:0) = raw_*(5:0)
    //   * VHDL "joy_*_n(11:8) <= i_joy_{4,3,1,2}_n"        → latched_*(11:8) = raw_*(11:8)
    //
    // The physical-pin handshakes (e.g. "if i_joy_3_n=0 and i_joy_4_n=0")
    // are NOT re-checked here — the host adapter is expected to present a
    // consistent 12-bit state. The ONE exception is the 6-button detect at
    // phase 1000/1001, which gates the bits 11:8 latch and is observable
    // in plan row MD6-11i.

    switch (phase) {
        // Phase 0000 (md6_joystick_connector_x2.vhd:135-139): full clear
        // of both connectors' latches + 6-button flags. Fires once per
        // 16-phase sweep regardless of which connector is being read.
        case 0x0:
            latched_left_     = 0;
            latched_right_    = 0;
            six_button_left_  = false;
            six_button_right_ = false;
            break;

        // Phase 0100 (VHDL:141-144): latch left bits 7:6 (START, A).
        case 0x4: {
            const uint16_t slice = raw_left_ & 0x00C0u;   // bits 7:6
            latched_left_ = (latched_left_ & ~0x00C0u) | slice;
            break;
        }

        // Phase 0101 (VHDL:146-149): latch right bits 7:6.
        case 0x5: {
            const uint16_t slice = raw_right_ & 0x00C0u;
            latched_right_ = (latched_right_ & ~0x00C0u) | slice;
            break;
        }

        // Phase 0110 (VHDL:151-152): latch left bits 5:0 (C, B, U, D, L, R).
        case 0x6: {
            const uint16_t slice = raw_left_ & 0x003Fu;
            latched_left_ = (latched_left_ & ~0x003Fu) | slice;
            break;
        }

        // Phase 0111 (VHDL:154-155): latch right bits 5:0.
        case 0x7: {
            const uint16_t slice = raw_right_ & 0x003Fu;
            latched_right_ = (latched_right_ & ~0x003Fu) | slice;
            break;
        }

        // Phase 1000 (VHDL:157-158): detect 6-button on left.
        // VHDL: joy_left_six_button_n <= i_joy_1_n or i_joy_2_n;
        // Active-low → flag=0 (six-button) iff BOTH pin1 AND pin2 are low.
        // Physical pin1 = U (bit 3 active-high), pin2 = D (bit 2).
        // Active-high equivalent: six-button detected iff raw_left_[3]=1
        // AND raw_left_[2]=1 at this phase.
        case 0x8:
            six_button_left_ = ((raw_left_ & 0x0008u) != 0u) &&
                               ((raw_left_ & 0x0004u) != 0u);
            break;

        // Phase 1001 (VHDL:160-161): detect 6-button on right.
        case 0x9:
            six_button_right_ = ((raw_right_ & 0x0008u) != 0u) &&
                                ((raw_right_ & 0x0004u) != 0u);
            break;

        // Phase 1010 (VHDL:163-166): if left is 6-button, latch bits 11:8
        // (MODE, X, Z, Y). The VHDL assignment order
        // "i_joy_4_n & i_joy_3_n & i_joy_1_n & i_joy_2_n" maps pin4→bit11,
        // pin3→bit10, pin1→bit9, pin2→bit8 (MSB-first concat). In our
        // active-high model these four bits already live at raw_*[11:8] by
        // the host-adapter contract, so the slice copy is direct.
        case 0xA: {
            if (six_button_left_) {
                const uint16_t slice = raw_left_ & 0x0F00u;
                latched_left_ = (latched_left_ & ~0x0F00u) | slice;
            }
            break;
        }

        // Phase 1011 (VHDL:168-171): if right is 6-button, latch bits 11:8.
        case 0xB: {
            if (six_button_right_) {
                const uint16_t slice = raw_right_ & 0x0F00u;
                latched_right_ = (latched_right_ & ~0x0F00u) | slice;
            }
            break;
        }

        // All other phases (0001, 0010, 0011, 1100..1111) are idle — the
        // VHDL `when others => null` clause at line 173. Their only role
        // is providing rest gaps between sampling bursts.
        default:
            break;
    }
}

// -------------------------------------------------------------------------
// tick() — accumulate CPU cycles, fire one FSM step per ~16 cycles to
// approximate the VHDL CLK_EN pulse rate (~4.57us @ 28MHz base clock ≈
// every 16 CPU cycles at the 3.5 MHz Z80 base). Not cycle-accurate to the
// VHDL but preserves the observable behaviour: periodic advancement of
// the 9-bit state counter with per-phase latch updates.
// -------------------------------------------------------------------------
void Md6ConnectorX2::tick(uint32_t cycles)
{
    clk_en_accum_ += cycles;
    while (clk_en_accum_ >= kCyclesPerClkEn) {
        clk_en_accum_ -= kCyclesPerClkEn;
        step_fsm_once_for_test();
    }
}

// -------------------------------------------------------------------------
// Test-only helpers.
// -------------------------------------------------------------------------

void Md6ConnectorX2::set_state_for_test(uint16_t state_value)
{
    state_ = state_value & 0x01FFu;   // 9 bits per VHDL :66
}

void Md6ConnectorX2::step_fsm_once_for_test()
{
    // Mirror VHDL :103-114: at the rising CLK_28 edge with CLK_EN=1, the
    // case block fires against the CURRENT state(3:0), and then
    // state <= state_next (= state + 1). We do the action first, then
    // increment — matching the same-edge semantics.
    if (!in_rest_window()) {
        apply_phase_action(static_cast<uint8_t>(state_ & 0x0Fu));
    }
    state_ = (state_ + 1u) & 0x01FFu;
}
