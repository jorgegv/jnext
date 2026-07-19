#pragma once
#include <cstdint>

class MembraneStick;  // forward — wired via set_membrane_stick(); not owned.

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

    /// Install a non-owning back-pointer to the MembraneStick that the
    /// joystick→membrane fold (VHDL `membrane_stick.vhd:117-149`) is wired
    /// to. Set once at Emulator construction time
    /// (`emulator.cpp` ctor — mirrors `Keyboard::set_membrane_stick`
    /// pattern). When set, `set_nr_05()` and `set_mode_direct()` propagate
    /// the per-connector decoded modes to `MembraneStick::set_mode()` so
    /// the membrane fold's per-mode keymap region stays in sync with the
    /// VHDL `joy_type` driver. Pass `nullptr` to detach (test isolation).
    void set_membrane_stick(MembraneStick* m) { membrane_ = m; }

    Mode mode_left()  const { return joy0_mode_; }   ///< joy0
    Mode mode_right() const { return joy1_mode_; }   ///< joy1

    /// Raw 12-bit connector input vectors (as last installed by the host
    /// adapter / restored by load_state). Exposed so JoystickDispatcher can
    /// re-seed its shadow copy from the canonical state after a rewind /
    /// save-load restore (Task 60c resync — otherwise the dispatcher's stale
    /// `bits_` shadow would stomp the restored vector on the next event).
    uint16_t joy_left_bits()  const { return joy_left_bits_; }
    uint16_t joy_right_bits() const { return joy_right_bits_; }

    /// Raw NR 0x05 last-write byte. Pass-8: used by Emulator::init() to
    /// detect whether the cached NR 0x05 byte (preserved across reset by
    /// NextReg) is in sync with the joystick subsystem state, so the
    /// re-fan-out call can be skipped when both are already coherent.
    uint8_t nr_05_raw() const { return nr_05_raw_; }

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

    /// NR 0xB2 (Extended MD Pad Buttons) read composer, per
    /// zxnext.vhd:6214-6215:
    ///   R.X R.Z R.Y R.MODE L.X L.Z L.Y L.MODE  (bits 7..0)
    /// sourced from bits 11:8 of the same two connector vectors the port
    /// composers read. Read-only and MODE-INDEPENDENT: no NR 0x05 gating
    /// and no Kempston masking — these four bits per connector reach no
    /// port under any mode, so this register is their only path to a guest.
    uint8_t nr_b2_byte() const;

    /// Mode-conditional port-decode enable for port 0x1F (G129).
    ///
    /// VHDL `zxnext.vhd:2454`: `port_1f_hw_en <= joyL_1f_en or joyR_1f_en;`
    /// where (`:3475`, `:3487`):
    ///     joyL_1f_en = '1' when nr_05_joy0 = "001" or mdL_1f_en = '1';
    ///     joyR_1f_en = '1' when nr_05_joy1 = "001" or mdR_1f_en = '1';
    /// and `mdL_1f_en` / `mdR_1f_en` go high in mode "101" (Md3Left).
    /// So `port_1f_hw_en` is true ONLY when at least one connector is in
    /// Kempston1 (001) or Md3Left (101). VHDL `:2674` AND-merges this
    /// with the NR 0x82 b6 io_en gate to decide whether port 0x1F is
    /// claimed at all — when both gates are zero the port falls through
    /// to the floating-bus path (0xFF). The port-handler in `emulator.cpp`
    /// is the level that returns 0xFF; this method exposes the lane-side
    /// signal so the handler can compute the AND.
    bool port_1f_hw_en() const;

    /// Mode-conditional port-decode enable for port 0x37 (G129).
    ///
    /// VHDL `zxnext.vhd:2455`: `port_37_hw_en <= joyL_37_en or joyR_37_en;`
    /// where (`:3476`, `:3488`):
    ///     joyL_37_en = '1' when nr_05_joy0 = "100" or mdL_37_en = '1';
    ///     joyR_37_en = '1' when nr_05_joy1 = "100" or mdR_37_en = '1';
    /// and `mdL_37_en` / `mdR_37_en` go high in mode "110" (Md3Right).
    /// So `port_37_hw_en` is true ONLY when at least one connector is in
    /// Kempston2 (100) or Md3Right (110).
    bool port_37_hw_en() const;

    // Task 60c — state serialisation. Persists the decoded connector modes,
    // the raw NR 0x05 byte and the two 12-bit raw input vectors. The
    // non-owning membrane_ back-pointer is NOT serialised (rewired by the
    // Emulator). On load the modes are restored directly, so no NR 0x05
    // re-fanout is required.
    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

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

    // Non-owning back-pointer to the membrane-fold module. Set once at
    // Emulator construction; nullptr in unit-test fixtures that don't
    // care about the propagation path. See set_membrane_stick() for the
    // contract.
    MembraneStick* membrane_ = nullptr;
};
