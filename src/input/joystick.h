#pragma once
#include <cstdint>

/// Joystick state + NR 0x05 mode decoder + port 0x1F / 0x37 read composer.
///
/// Mirrors the VHDL `nr_05_joy0` / `nr_05_joy1` mode fields
/// (zxnext.vhd:1105-1106 defaults, decoded at :5157-5158) and the
/// `joyL_1f` / `joyR_37` / `joyL_37` / `joyR_1f` muxing at
/// zxnext.vhd:3470-3494. The per-connector 12-bit raw input vector uses
/// the VHDL layout documented at zxnext.vhd:3441-3442:
///
///   bit 11:8 = MODE, X, Z, Y   (MD6 latch, read via NR 0xB2)
///   bit  7   = START            (MD3: START; Kempston: masked 0)
///   bit  6   = A                (MD3: A;     Kempston: masked 0)
///   bit  5:0 = C, B, U, D, L, R (Kempston Fire2/Fire1/U/D/L/R;
///                                MD3: C, B, U, D, L, R)
///
/// Phase 1 scaffold (Task 3 Input): every new method is a compile-only
/// stub. Real behaviour lands in Phase 2 agents A (NR 0x05 decoder) and
/// B (port-1F/37 composition + Kempston / MD3 masking).
class Joystick {
public:
    /// NR 0x05 mode codes per zxnext.vhd:3429-3438.
    enum class Mode : uint8_t {
        Sinclair2 = 0,   ///< keys 0, 9, 8, 7, 6 on row 4
        Kempston1 = 1,   ///< port 0x1F
        Cursor    = 2,   ///< keys 5, 6, 7, 8, 0 (Protek/Cursor via caps-shift)
        Sinclair1 = 3,   ///< keys 1..5 on row 3
        Kempston2 = 4,   ///< port 0x37
        Md3Left   = 5,   ///< MD 3-button, port 0x1F (bits 7:6 pass)
        Md3Right  = 6,   ///< MD 3-button, port 0x37
        IoMode    = 7,   ///< Both joystick pins free for I/O; NR 0x0B owns pin 7
    };

    Joystick() { reset(); }

    /// VHDL reset defaults: joy0 = "001" (Kempston1), joy1 = "000"
    /// (Sinclair2). See zxnext.vhd:1105-1106. Also clears the raw joy
    /// state vectors to all-zero (no buttons pressed).
    void reset();

    /// NR 0x05 mode decoder per zxnext.vhd:5157-5158:
    ///   nr_05_joy0 <= nr_wr_dat(3) & nr_wr_dat(7 downto 6);
    ///   nr_05_joy1 <= nr_wr_dat(1) & nr_wr_dat(5 downto 4);
    /// Extracts the two 3-bit mode fields and latches them into the
    /// joy0_mode_ / joy1_mode_ enums; also stores the raw byte for
    /// readback traceability.
    void set_nr_05(uint8_t v);

    /// Test-harness bypass: directly set both connector modes without
    /// running the NR 0x05 bit-extraction. Used by Wave-1 agents D and E
    /// so they can drive the Md6 / IoMode / Mouse tests without waiting
    /// for Agent A's NR 0x05 decoder to land. Production UI code MUST
    /// not call this — it exists only for test parallelism.
    void set_mode_direct(Mode joy0, Mode joy1);

    Mode mode_left()  const { return joy0_mode_; }   ///< joy0
    Mode mode_right() const { return joy1_mode_; }   ///< joy1

    /// Install the raw 12-bit joystick-state vector for the left / right
    /// connector. Layout per zxnext.vhd:3441-3442 (see class comment).
    /// Phase 1 stub just stores the value.
    void set_joy_left(uint16_t bits12);
    void set_joy_right(uint16_t bits12);

    /// Port 0x1F read composer. Phase 1 stub returns 0x00 (matches the
    /// pre-scaffold Kempston-mode "no buttons" default and preserves the
    /// existing port-dispatch behaviour until Agent B wires the real
    /// mux per zxnext.vhd:3470-3479 and the bit-7/6 masking at
    /// zxnext.vhd:3478.
    uint8_t read_port_1f() const;

    /// Port 0x37 read composer. Phase 1 stub returns 0x00. Real
    /// implementation lands in Agent B per zxnext.vhd:3480-3494.
    uint8_t read_port_37() const;

private:
    // Raw NR 0x05 byte (latched pre-decode so that set_nr_05 round-trips
    // the value for subsequent debug reads). 0x40 is the raw byte that
    // decodes to (joy0=001 Kempston1, joy1=000 Sinclair2) per the VHDL
    // reset defaults at zxnext.vhd:1105-1106 (extraction at :5157-5158).
    uint8_t nr_05_raw_ = 0x40;

    // Decoded connector modes. VHDL resets: joy0=Kempston1, joy1=Sinclair2.
    Mode joy0_mode_ = Mode::Kempston1;
    Mode joy1_mode_ = Mode::Sinclair2;

    // 12-bit raw connector inputs, one per side. Layout per class comment.
    uint16_t joy_left_bits_  = 0;
    uint16_t joy_right_bits_ = 0;
};
