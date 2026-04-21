#include "input/mouse.h"

// =============================================================================
// Phase 1 scaffold — all methods are compile-only stubs. Agent H (Phase 2)
// will fill in the real X/Y counter update with DPI scaling + button-reverse
// XOR and the three port composers per zxnext.vhd:3541-3562.
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

void KempstonMouse::inject_delta(int /*dx*/, int /*dy*/)
{
    // Phase 1 stub — no-op. Agent H implements dpi-scaled accumulation.
}

void KempstonMouse::set_buttons(uint8_t mask)
{
    // Phase 1 stub: store only (no reverse applied until Agent H).
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
    // Phase 1 stub: preserve pre-scaffold 0x00 return (matches the
    // previous port-dispatch stub at emulator.cpp:1393-1395). Agent H
    // replaces this with:
    //   wheel(3:0) & '1' & (not btn(2)) & (not btn(0)) & (not btn(1))
    // per zxnext.vhd:3560, optionally XOR'd with button_reverse_.
    return 0x00;
}

uint8_t KempstonMouse::read_port_fbdf() const
{
    // Phase 1 stub: pre-scaffold default 0x00. Real = i_MOUSE_X per VHDL.
    return 0x00;
}

uint8_t KempstonMouse::read_port_ffdf() const
{
    // Phase 1 stub: pre-scaffold default 0x00. Real = i_MOUSE_Y per VHDL.
    return 0x00;
}
