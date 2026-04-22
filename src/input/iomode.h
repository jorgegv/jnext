#pragma once
#include <cstdint>

/// NR 0x0B joystick I/O-mode pin-7 multiplexer.
///
/// VHDL reference:
///   zxnext.vhd:1129-1131 — signal declarations (default '0' / "00" / '0'
///                          via VHDL default-init; the NR-write reset path
///                          at zxnext.vhd:4939-4941 sets nr_0b_joy_iomode_0
///                          to '1' on reset, giving NR 0x0B = 0x01).
///   zxnext.vhd:5200-5203 — NR 0x0B write handler (bits 7, 5:4, 0)
///   zxnext.vhd:3512-3534 — joy_iomode_pin7 state update
///
/// The relevant NR 0x0B bit layout is:
///
///   bit 7    = nr_0b_joy_iomode_en      (enable, also gates UART path)
///   bits 5:4 = nr_0b_joy_iomode         ("00" static, "01" CTC-toggled,
///                                        "10"/"11" UART-driven)
///   bit 0    = nr_0b_joy_iomode_0       (static value / initial level)
///
/// Mode-00 ("static"): pin7 continuously tracks `nr_0b_joy_iomode_0`. The
/// VHDL implementation re-asserts the value on every `i_CLK_28` rising
/// edge; from the test harness's point of view this collapses to "pin7
/// equals iomode_0 immediately after the NR 0x0B write".
///
/// Mode-01 ("CTC-toggled"): on every `ctc_zc_to(3)` pulse, pin7 toggles
/// IF `iomode_0='1' OR pin7='0'`. This second condition (zxnext.vhd:3522)
/// is a one-cycle re-assertion guard — without it, if iomode_0 is '0' and
/// pin7 happened to be '1' on entering mode 01, the very first CTC pulse
/// would still flip pin7 to '0' and then immediately oscillate. The guard
/// keeps pin7 stable at the iomode_0 level once it reaches it.
///
/// Modes 10/11 ("UART"): pin7 is driven by uart0_tx / uart1_tx. Out of
/// scope for this agent — the 6 IOMODE-05..10 test rows are F-skipped
/// pending the UART+I2C subsystem plan. Writes that select these modes
/// are accepted (raw byte stored), but pin7 is left at its last value;
/// downstream UART code can override it once that subsystem lands.
class IoMode {
public:
    IoMode() { reset(); }

    /// Reset pin7 to '1' per zxnext.vhd:3516. The NR 0x0B logical reset
    /// value is 0x01 (en=0, mode=00, iomode_0=1) per zxnext.vhd:4939-4941
    /// — which keeps pin7 at '1' under mode-00 (pin7 = iomode_0).
    void reset();

    /// Real NR 0x0B decoder per zxnext.vhd:5200-5203:
    ///   nr_0b_joy_iomode_en <= v[7];
    ///   nr_0b_joy_iomode    <= v[5:4];
    ///   nr_0b_joy_iomode_0  <= v[0];
    /// Also runs the mode-00 continuous-assign per zxnext.vhd:3520:
    /// pin7 immediately tracks the new iomode_0 if the freshly-written
    /// mode is "00".
    void set_nr_0b(uint8_t v);

    /// Called when CTC channel 3 emits a ZC/TO pulse. Toggles pin7 IFF
    /// the current mode is "01" AND `(iomode_0='1' OR pin7='0')`
    /// per zxnext.vhd:3521-3524. No-op for any other mode.
    void tick_ctc_zc3();

    /// Pin-7 output observable by the joystick connector.
    /// Reset value '1' per zxnext.vhd:3516.
    bool    pin7() const { return pin7_; }

    /// NR 0x0B bit 7. Used by the (future) UART subsystem to gate the
    /// UART data path on/off.
    bool    iomode_en() const { return (nr_0b_raw_ & 0x80) != 0; }

    /// NR 0x0B bits 5:4 — mode selector.
    ///   0 = static (pin7 = iomode_0)
    ///   1 = CTC-toggled (pin7 toggles on ctc_zc_to(3) under guard)
    ///   2 = UART0 tx → pin7 (out of scope)
    ///   3 = UART1 tx → pin7 (out of scope)
    uint8_t iomode_bits() const { return static_cast<uint8_t>((nr_0b_raw_ >> 4) & 0x03); }

    /// NR 0x0B bit 0 — the static-mode source / mode-01 toggle guard.
    bool    iomode_0() const { return (nr_0b_raw_ & 0x01) != 0; }

    /// Raw NR 0x0B value (last byte written, or reset default 0x01 after
    /// reset()). Used by save/load and by NextReg readback.
    uint8_t nr_0b_raw() const { return nr_0b_raw_; }

    /// Save-state restore for the pin7 register. Only used by
    /// Emulator::load_state — production code never sets pin7 directly;
    /// it changes in response to NR 0x0B writes and CTC ch3 ZC/TO pulses.
    void set_pin7_for_load(bool v) { pin7_ = v; }

private:
    // Reset to the logical NR 0x0B value 0x01 (en=0, mode=00, iomode_0=1)
    // per zxnext.vhd:4939-4941. Pin7 starts at '1' per zxnext.vhd:3516.
    uint8_t nr_0b_raw_ = 0x01;
    bool    pin7_      = true;
};
