#include "input/mouse.h"

// =============================================================================
// Phase 2 Agent H — Kempston mouse port composers per zxnext.vhd:3543-3561.
//
// Port 0xFBDF read : i_MOUSE_X             (zxnext.vhd:3546)
// Port 0xFFDF read : i_MOUSE_Y             (zxnext.vhd:3553)
// Port 0xFADF read : wheel(3:0) & '1' & (not btn(2)) & (not btn(0)) & (not btn(1))
//                                          (zxnext.vhd:3560)
//
// In VHDL, i_MOUSE_BUTTON(0/1/2) = L/R/M. The scaffold's `buttons_` mask
// uses a different convention (bit 0 = R, bit 1 = L, bit 2 = M — see
// mouse.h:33). The port composition formula below re-maps the scaffold
// bits so that:
//    port bit 0 = NOT buttons_[0] (R)   — plan row MOUSE-05
//    port bit 1 = NOT buttons_[1] (L)   — plan row MOUSE-04
//    port bit 2 = NOT buttons_[2] (M)   — plan row MOUSE-06
// i.e. ~buttons_ on all three low bits. Idle (buttons=0, wheel=0) → 0x0F.
//
// The port_mouse_io_en enable gate (zxnext.vhd:2668-2670, NR 0x83 bit 5)
// is NOT checked here; it lives one layer up in the Emulator-level port
// handler (see src/core/emulator.cpp Kempston-mouse port registration —
// mirrors the NR 0x82 bit-6 gate pattern used for port 0x001F).
//
// Deferred / out-of-scope (plan rows MOUSE-09/10/11, all G-classified):
//   - button_reverse_ (NR 0x0A bit 3): stored but not applied; the VHDL
//     has no internal consumer — button reversal is host-adapter work.
//   - Wheel signed-delta semantics: host-adapter responsibility; VHDL
//     exposes only the raw 4-bit field.
//   - DPI code (NR 0x0A bits 1:0): stored but not consumed; exposed on
//     o_MOUSE_CONTROL for an external host-adapter DPI divisor only.
// =============================================================================

void KempstonMouse::reset()
{
    // VHDL zxnext.vhd:1127-1128 reset defaults:
    //   nr_0a_mouse_button_reverse = '0'
    //   nr_0a_mouse_dpi            = "01"
    // X, Y, buttons, wheel are driven by external inputs in VHDL; in the
    // emulator we reset them to 0 (no movement, no buttons pressed).
    x_              = 0;
    y_              = 0;
    buttons_        = 0;
    wheel_          = 0;
    button_reverse_ = false;
    dpi_            = 0x01;
}

void KempstonMouse::inject_delta(int dx, int dy)
{
    // 8-bit wrap-around accumulation. dx/dy are host pixel deltas; the
    // Kempston protocol exposes the raw 8-bit counter registers directly
    // at 0xFBDF / 0xFFDF (zxnext.vhd:3546, 3553). Signed-vs-unsigned
    // interpretation is host-side (plan row MOUSE-10 = G).
    x_ = static_cast<uint8_t>(x_ + dx);
    y_ = static_cast<uint8_t>(y_ + dy);
}

void KempstonMouse::set_buttons(uint8_t mask)
{
    // Store the raw 3-bit active-high button mask from the host.
    // Convention: bit 0 = R, bit 1 = L, bit 2 = M (see mouse.h:33).
    // The port read inverts to active-low per VHDL; button_reverse_ is
    // NOT applied here (plan row MOUSE-09 = G; VHDL has no in-core
    // consumer of nr_0a_mouse_button_reverse — host-adapter remaps).
    buttons_ = mask & 0x07;
}

void KempstonMouse::set_wheel(uint8_t nibble)
{
    wheel_ = nibble & 0x0F;
}

void KempstonMouse::set_button_reverse(bool on)
{
    button_reverse_ = on;
}

void KempstonMouse::set_dpi(uint8_t code)
{
    dpi_ = code & 0x03;
}

uint8_t KempstonMouse::read_port_fadf() const
{
    // VHDL zxnext.vhd:3560 :
    //   port_fadf_dat <= i_MOUSE_WHEEL & '1'
    //                  & (not i_MOUSE_BUTTON(2))
    //                  & (not i_MOUSE_BUTTON(0))
    //                  & (not i_MOUSE_BUTTON(1));
    //
    // Our buttons_ convention (bit0=R, bit1=L, bit2=M) is chosen so that
    // the port bit layout collapses to simple ~buttons_ on the low 3 bits:
    //   bit 2 = ~buttons_[2] (M)   — plan MOUSE-06
    //   bit 1 = ~buttons_[1] (L)   — plan MOUSE-04
    //   bit 0 = ~buttons_[0] (R)   — plan MOUSE-05
    const uint8_t wheel_nibble = static_cast<uint8_t>((wheel_ & 0x0F) << 4);
    const uint8_t fixed_bit3   = 0x08;
    const uint8_t btn_inverted = static_cast<uint8_t>((~buttons_) & 0x07);
    return static_cast<uint8_t>(wheel_nibble | fixed_bit3 | btn_inverted);
}

uint8_t KempstonMouse::read_port_fbdf() const
{
    // VHDL zxnext.vhd:3546 : port_fbdf_dat <= i_MOUSE_X.
    return x_;
}

uint8_t KempstonMouse::read_port_ffdf() const
{
    // VHDL zxnext.vhd:3553 : port_ffdf_dat <= i_MOUSE_Y.
    return y_;
}
