#pragma once
#include <cstdint>

/// Kempston-mouse state + port 0xFADF / 0xFBDF / 0xFFDF read composer +
/// NR 0x0A mouse-control bits.
///
/// VHDL reference:
///   zxnext.vhd:3541-3562 — port_fadf/fbdf/ffdf read processes
///   zxnext.vhd:5197-5198 — NR 0x0A mouse button reverse + DPI bits
///   zxnext.vhd:1127-1128 — reset defaults (button_reverse=0, dpi="01")
///   zxnext.vhd:2668-2670 — port_mouse_io_en enable gate (NR 0x83 bit 5)
///
/// The X and Y counters are raw 8-bit registers exposed directly at
/// 0xFBDF / 0xFFDF (zxnext.vhd:3546, 3553). Signed vs. unsigned
/// interpretation is host-side (plan row MOUSE-10 = G). Kempston wheel
/// is a 4-bit unsigned field on port 0xFADF bits 7:4. Buttons on port
/// 0xFADF are active-low:
///
///   port_fadf_dat <= wheel(3:0) & '1' & (not btn(2)) & (not btn(0)) & (not btn(1));
///
/// NR 0x0A bit 3 (button reverse) and bits 1:0 (DPI) are stored but not
/// applied here — neither has an in-core VHDL consumer; both are host-
/// adapter concerns (plan rows MOUSE-09 / MOUSE-11 = G).
///
/// The port_mouse_io_en enable gate is enforced one layer up in the
/// Emulator-level port handler (src/core/emulator.cpp, mirrors the
/// NR 0x82 bit-6 / port-0x001F gate pattern). This class does NOT
/// self-gate on port_mouse_io_en.
class KempstonMouse {
public:
    KempstonMouse() { reset(); }

    /// Reset to VHDL defaults: X=0, Y=0, no buttons, wheel=0, button_reverse=0,
    /// dpi="01" (VHDL zxnext.vhd:1127-1128).
    void reset();

    /// Inject a host-relative motion delta. dx>0 = right, dy>0 = down.
    /// Adds dx/dy to the 8-bit X/Y counters with modulo-256 wrap-around,
    /// matching the raw Kempston register exposure at 0xFBDF / 0xFFDF.
    void inject_delta(int dx, int dy);

    /// Set the 3-bit button mask. Bit 0 = Right, bit 1 = Left, bit 2 = Middle.
    /// The VHDL composes port 0xFADF as active-low; this method stores the
    /// raw (active-high from host) mask. Button reverse is NOT applied
    /// (plan row MOUSE-09 = G — VHDL has no in-core consumer).
    void set_buttons(uint8_t mask);

    /// Set the 4-bit unsigned wheel value. Bits 7:4 of port 0xFADF per VHDL.
    void set_wheel(uint8_t nibble);

    /// NR 0x0A bit 3 — mouse button reverse. VHDL zxnext.vhd:5197.
    void set_button_reverse(bool on);

    /// NR 0x0A bits 1:0 — DPI code (resolution scaler). VHDL zxnext.vhd:5198.
    void set_dpi(uint8_t code);

    /// Port 0xFADF read per zxnext.vhd:3560.
    uint8_t read_port_fadf() const;

    /// Port 0xFBDF read (mouse X register) per zxnext.vhd:3546.
    uint8_t read_port_fbdf() const;

    /// Port 0xFFDF read (mouse Y register) per zxnext.vhd:3553.
    uint8_t read_port_ffdf() const;

    uint8_t x() const { return x_; }
    uint8_t y() const { return y_; }

private:
    uint8_t x_ = 0;             // i_MOUSE_X, driven by host adapter
    uint8_t y_ = 0;             // i_MOUSE_Y, driven by host adapter
    uint8_t buttons_ = 0;       // 3-bit active-high button mask
    uint8_t wheel_ = 0;         // 4-bit unsigned wheel nibble
    bool    button_reverse_ = false;  // NR 0x0A bit 3, zxnext.vhd:5197
    uint8_t dpi_ = 0x01;        // NR 0x0A bits 1:0, zxnext.vhd:1128 default
};
