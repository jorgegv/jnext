#pragma once
#include <cstdint>

/// Kempston-mouse state + port 0xFADF / 0xFBDF / 0xFFDF read composer +
/// NR 0x0A mouse-control bits.
///
/// VHDL reference:
///   zxnext.vhd:3541-3562 — port_fadf/fbdf/ffdf read processes
///   zxnext.vhd:5197-5198 — NR 0x0A mouse button reverse + DPI bits
///   zxnext.vhd:1127-1128 — reset defaults (button_reverse=0, dpi="01")
///
/// The X and Y counters are 8-bit signed deltas maintained by the host
/// adapter (SDL mouse events). Kempston wheel is a 4-bit unsigned field
/// on port 0xFADF bits 7:4. Buttons on port 0xFADF are active-low:
///
///   port_fadf_dat <= wheel(3:0) & '1' & (not btn(2)) & (not btn(0)) & (not btn(1));
///
/// Phase 1 scaffold: every new method is a compile-only stub. Agent H
/// (Phase 2) fills in the real X/Y counter update, wheel nibble handling,
/// button-reverse XOR, and DPI-scaled delta accumulation.
class KempstonMouse {
public:
    KempstonMouse() { reset(); }

    /// Reset to VHDL defaults: X=0, Y=0, no buttons, wheel=0, button_reverse=0,
    /// dpi="01" (VHDL zxnext.vhd:1127-1128).
    void reset();

    /// Inject a host-relative motion delta. dx>0 = right, dy>0 = down.
    /// Phase 1 stub — does nothing.
    void inject_delta(int dx, int dy);

    /// Set the 3-bit button mask. Bit 0 = Right, bit 1 = Left, bit 2 = Middle.
    /// The VHDL composes port 0xFADF as active-low; this method stores the
    /// raw (active-high from host) mask. Phase 1 stub stores only.
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
