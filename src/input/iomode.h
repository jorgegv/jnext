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
/// Modes 10/11 ("UART"): pin7 is driven by uart0_tx / uart1_tx per
/// zxnext.vhd:3526-3531. In VHDL the case-`when others` fires for both
/// 10 and 11 (mode bit 4 is irrelevant; only iomode_0 picks the channel):
///   iomode_0='0' → pin7 = uart0_tx
///   iomode_0='1' → pin7 = uart1_tx
/// In C++ we expose `pin7()` as a computed accessor that evaluates the
/// current UART TX state when the UART modes are selected, rather than
/// clocking a stored register every cycle. UART TX values are injected
/// via `set_uart0_tx()` / `set_uart1_tx()` from the Emulator tick loop.
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

    /// Pin-7 output observable by the joystick connector. For modes
    /// 00/01 returns the clocked register `pin7_`. For modes 10/11
    /// returns the currently-injected UART TX line per zxnext.vhd:3526-3531
    /// (iomode_0=0 → uart0_tx, iomode_0=1 → uart1_tx). Reset value '1'
    /// per zxnext.vhd:3516.
    bool    pin7() const {
        const uint8_t mode = iomode_bits();
        if (mode == 2 || mode == 3) {
            return ((nr_0b_raw_ & 0x01) != 0) ? uart1_tx_ : uart0_tx_;
        }
        return pin7_;
    }

    /// Inject UART TX line state for modes 10/11. Emulator wires these
    /// every tick from Uart::channel(N).tx_line_out(). Default idle '1'
    /// matches the UART serial-line idle-high convention.
    void    set_uart0_tx(bool v) { uart0_tx_ = v; }
    void    set_uart1_tx(bool v) { uart1_tx_ = v; }

    /// joy_uart_en = iomode_en AND iomode(1) per zxnext.vhd:3537
    /// (joy_iomode_uart_en <= nr_0b_joy_iomode_en AND nr_0b_joy_iomode(1)).
    /// Controls the UART/joystick multiplexing in the UART input path.
    bool    joy_uart_en() const {
        return ((nr_0b_raw_ & 0x80) != 0) && ((nr_0b_raw_ & 0x20) != 0);
    }

    /// joy_uart_rx composition per zxnext.vhd:3539:
    ///   (NOT iomode_0 AND NOT i_JOY_LEFT(5)) OR
    ///   (    iomode_0 AND NOT i_JOY_RIGHT(5))
    /// iomode_0 selects which joystick connector's button-5 line feeds
    /// the UART RX path (active-low).
    bool    joy_uart_rx() const {
        const bool iomode0 = ((nr_0b_raw_ & 0x01) != 0);
        return iomode0 ? !joy_right_bit5_ : !joy_left_bit5_;
    }

    /// Inject joystick button-5 level (active-low at the VHDL signal
    /// level — true means button NOT pressed, idle high).
    void    set_joy_left_bit5(bool level)  { joy_left_bit5_  = level; }
    void    set_joy_right_bit5(bool level) { joy_right_bit5_ = level; }

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
    // UART TX lines (modes 10/11). Default '1' matches UART serial idle-high.
    bool    uart0_tx_  = true;
    bool    uart1_tx_  = true;
    // Joystick connector button-5 lines (VHDL signals i_JOY_LEFT/RIGHT(5),
    // active-low). Default '1' means idle / not pressed.
    bool    joy_left_bit5_  = true;
    bool    joy_right_bit5_ = true;
};
