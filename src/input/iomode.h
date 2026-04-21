#pragma once
#include <cstdint>

/// NR 0x0B joystick I/O-mode pin-7 multiplexer.
///
/// VHDL reference:
///   zxnext.vhd:1129-1131 — signal declarations (no explicit reset;
///                          defaults to '0' / "00" via VHDL default-init
///                          rules — see below)
///   zxnext.vhd:5200-5203 — NR 0x0B write handler (bits 7, 5:4, 0)
///   zxnext.vhd:3512-3534 — joy_iomode_pin7 state update
///
/// The relevant NR 0x0B bit layout is:
///
///   bit 7    = nr_0b_joy_iomode_en      (enable)
///   bits 5:4 = nr_0b_joy_iomode         ("00" static, "01" CTC-toggled,
///                                        "10"/"11" UART-driven)
///   bit 0    = nr_0b_joy_iomode_0       (static value / initial level)
///
/// Agent E (Phase 2, Wave 2) fills in the static (mode 00) and CTC-toggled
/// (mode 01) behaviour. UART-driven modes (10/11) stay stubbed — the 6
/// IOMODE-05..10 test rows are F-skipped until the UART+I2C plan lands.
///
/// Phase 1 scaffold: every new method is a stub. The pin-7 output is
/// already owned by Emulator::joy_iomode_pin7_ (see emulator.h:444);
/// Phase 2 migrates that shadow into this class.
class IoMode {
public:
    IoMode() { reset(); }

    /// Reset to VHDL defaults. VHDL declares `nr_0b_joy_iomode_en`,
    /// `nr_0b_joy_iomode`, `nr_0b_joy_iomode_0` without an explicit
    /// reset branch — they default to '0'/"00"/'0'. The separate
    /// `joy_iomode_pin7` signal (zxnext.vhd:3516) resets to '1'.
    void reset();

    /// Real NR 0x0B decoder per zxnext.vhd:5200-5203:
    ///   nr_0b_joy_iomode_en <= v[7];
    ///   nr_0b_joy_iomode    <= v[5:4];
    ///   nr_0b_joy_iomode_0  <= v[0];
    /// Phase 1 stub just stores the raw byte.
    void set_nr_0b(uint8_t v);

    /// Called when CTC channel 3 emits a ZC/TO pulse. In iomode="01"
    /// (gated by iomode_en and pin7), pin7 toggles. Phase 1 stub empty.
    void tick_ctc_zc3();

    /// Pin-7 output observable by the joystick connector. Phase 1 stub
    /// returns the VHDL default '1' — same as Emulator::joy_iomode_pin7_.
    bool    pin7() const { return pin7_; }

    /// NR 0x0B bit 7. Phase 1 stub returns 0.
    bool    iomode_en() const;

    /// NR 0x0B bits 5:4 — mode selector. Phase 1 stub returns 0.
    uint8_t iomode_bits() const;

private:
    uint8_t nr_0b_raw_ = 0x00;   // zxnext.vhd:1129-1131 defaults (no reset)
    bool    pin7_      = true;    // zxnext.vhd:3516 reset '1'
};
