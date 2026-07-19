#include "input/joystick.h"
#include "input/membrane_stick.h"
#include "core/saveable.h"

// =============================================================================
// Phase 2 Agent A — NR 0x05 mode decoder implemented VHDL-faithfully per
// zxnext.vhd:5157-5158. The port 0x1F / 0x37 read composers and the raw
// 12-bit joy_left_bits_ / joy_right_bits_ Kempston/MD3 masking are still
// stubs; Agent B lands those in Wave 2.
// =============================================================================

void Joystick::reset()
{
    // PASS-8 NR 0x05 reset preservation. VHDL zxnext.vhd:1105-1106 declare
    // `nr_05_joy0` / `nr_05_joy1` as initial-value-only signals (defaults
    // "001" / "000"). They DO NOT appear in the master reset block at
    // zxnext.vhd:4930+, nor in the dedicated processes for `nr_05_5060`
    // (:5832-5843) and `nr_05_scandouble_en` (:5845-5854) — both of which
    // have NO `if reset='1'` branch either. The four NR 0x05 sub-signals
    // therefore SURVIVE both hard and soft reset.
    //
    // Pass-7 fixed the NextReg byte cache to preserve regs_[0x05] across
    // reset, but the joystick's authoritative `joy0_mode_` / `joy1_mode_`
    // shadows were still being clobbered here on every reset, leaking out
    // through the NR 0x05 read handler (which sources joy bits from the
    // joystick subsystem, not from regs_[]). The cache and the subsystem
    // therefore disagreed after any post-write soft reset. PASS-8 fix:
    // do NOT clobber `nr_05_raw_` / `joy0_mode_` / `joy1_mode_` here.
    // Member defaults supply the FPGA power-on values on first
    // construction (`nr_05_raw_ = 0x40`, joy0=Kempston1, joy1=Sinclair2);
    // every subsequent reset preserves whatever value was last latched
    // by `set_nr_05()` / `set_mode_direct()`.
    //
    // Host-side input state (the raw 12-bit `joy_left_bits_` /
    // `joy_right_bits_` vectors) is NOT a Next NR register state — it
    // tracks the host adapter's pin levels, which in real hardware are
    // not in any reset domain (they're driven by the connected joystick).
    // Clearing them on reset is harmless and matches the "no joystick
    // pressed at reset" convention.
    joy_left_bits_   = 0;
    joy_right_bits_  = 0;

    // Re-propagate the (preserved) decoded modes to the membrane-fold
    // module.  `set_membrane_stick()` is wired AFTER construction, so the
    // first reset() runs with `membrane_ == nullptr` and the `set_mode()`
    // forward only happens on subsequent resets — ensuring MembraneStick
    // tracks the cached joy0/joy1 mode after a soft reset (which would
    // otherwise leave the membrane fold pinned to its own constructor
    // default if no NR 0x05 write happened post-reset).
    if (membrane_) {
        membrane_->set_mode(0, joy0_mode_);
        membrane_->set_mode(1, joy1_mode_);
    }
}

void Joystick::set_nr_05(uint8_t v)
{
    // VHDL-faithful decode per zxnext.vhd:5157-5158:
    //   nr_05_joy0 <= nr_wr_dat(3) & nr_wr_dat(7) & nr_wr_dat(6);
    //   nr_05_joy1 <= nr_wr_dat(1) & nr_wr_dat(5) & nr_wr_dat(4);
    // VHDL `&` is concatenation with the leftmost operand as MSB, so:
    //   joy0_bits[2:0] = { v[3], v[7], v[6] }
    //   joy1_bits[2:0] = { v[1], v[5], v[4] }
    // The 3-bit value maps directly onto Mode (enum values chosen to
    // align with the VHDL bit patterns at zxnext.vhd:3429-3438):
    //   000 Sinclair2 / 001 Kempston1 / 010 Cursor    / 011 Sinclair1
    //   100 Kempston2 / 101 Md3Left   / 110 Md3Right  / 111 IoMode
    const uint8_t joy0_bits =
        static_cast<uint8_t>((((v >> 3) & 1u) << 2) |
                             (((v >> 7) & 1u) << 1) |
                             ( (v >> 6) & 1u));
    const uint8_t joy1_bits =
        static_cast<uint8_t>((((v >> 1) & 1u) << 2) |
                             (((v >> 5) & 1u) << 1) |
                             ( (v >> 4) & 1u));

    nr_05_raw_ = v;
    joy0_mode_ = static_cast<Mode>(joy0_bits);
    joy1_mode_ = static_cast<Mode>(joy1_bits);

    // G126 — propagate the decoded modes to the membrane-fold module.
    // VHDL membrane_stick.vhd:117-149 — `joy_type` (== nr_05_joy0 /
    // nr_05_joy1) drives the COE address-start selector that picks the
    // per-mode keymap region. Without this forward, the membrane fold
    // stays pinned to its constructor default and switching mode via
    // NR 0x05 produces stale rows. See `set_membrane_stick()` for the
    // wiring contract (set once during Emulator construction).
    if (membrane_) {
        membrane_->set_mode(0, joy0_mode_);
        membrane_->set_mode(1, joy1_mode_);
    }
}

void Joystick::set_mode_direct(Mode joy0, Mode joy1)
{
    // Test-only: bypass the NR 0x05 decoder. Documented in the header.
    joy0_mode_ = joy0;
    joy1_mode_ = joy1;

    // G126 — same propagation contract as set_nr_05(): keep MembraneStick
    // in sync so test rows that go through the test-bypass path also
    // exercise the membrane-fold's per-mode keymap region.
    if (membrane_) {
        membrane_->set_mode(0, joy0_mode_);
        membrane_->set_mode(1, joy1_mode_);
    }
}

void Joystick::set_joy_left(uint16_t bits12)
{
    // Phase 1 stub: mask to 12 bits and store. Agent D/B (Phase 2) will
    // also feed this value into Md6ConnectorX2 for the MD6 latch path.
    joy_left_bits_ = bits12 & 0x0FFF;
}

void Joystick::set_joy_right(uint16_t bits12)
{
    joy_right_bits_ = bits12 & 0x0FFF;
}

// =============================================================================
// Phase 2 Agent B — port 0x1F / 0x37 read composer.
//
// VHDL-faithful mux per zxnext.vhd:3470-3506. The mux is split into a
// bits-7:6 "MD pass-through" lane and a bits-5:0 "Kempston-or-MD"
// lane. The VHDL excerpts (paraphrased; see lines for the exact text):
//
//   3472  mdL_1f_en  <= '1' when nr_05_joy0 = "101"  -- Md3Left
//   3473  mdL_37_en  <= '1' when nr_05_joy0 = "110"  -- Md3Right
//   3475  joyL_1f_en <= '1' when nr_05_joy0 = "001" or mdL_1f_en = '1'
//   3476  joyL_37_en <= '1' when nr_05_joy0 = "100" or mdL_37_en = '1'
//
//   3478  joyL_1f(7:6) <= JOY_LEFT(7:6) when mdL_1f_en = '1'  else 0
//   3479  joyL_1f(5:0) <= JOY_LEFT(5:0) when joyL_1f_en = '1' else 0
//   3481  joyL_37(7:6) <= JOY_LEFT(7:6) when mdL_37_en = '1'  else 0
//   3482  joyL_37(5:0) <= JOY_LEFT(5:0) when joyL_37_en = '1' else 0
//
// joyR_*  are identical with nr_05_joy1 and JOY_RIGHT.
//
//   3499  port_1f_dat <= joyL_1f or joyR_1f;
//   3506  port_37_dat <= joyL_37 or joyR_37;
//
// Key consequences exercised by the KEMP-* / MD-* test rows:
//   - Kempston modes (001 / 100) clear bits 7:6 (KEMP-07/08, MD-06).
//   - MD3 modes (101 / 110) pass bits 7:6 through (KEMP-15, MD-02/03/05).
//   - Both connectors OR into the same port byte, so K1+K1 doubles up
//     on 0x1F (KEMP-13), and K1+K2 splits L→0x1F, R→0x37 (KEMP-14).
//   - Modes outside {001, 100, 101, 110} contribute 0x00 to that port,
//     so port-1F with joy0=Sinclair2/joy1=Sinclair2 reads 0x00 from the
//     joystick lane (KEMP-12). The "0xFF when not decoded" headline
//     behaviour is enforced one level up by emulator.cpp:1414-1422 via
//     the NR 0x82 bit-6 gate; the joystick lane itself produces 0x00 in
//     that case, which OR-merges harmlessly with the gate's 0xFF.
//
// Note on raw-state plumbing: this composer reads `joy_left_bits_` /
// `joy_right_bits_` directly (option a from the brief). Md6ConnectorX2
// owns the MD6 latches for NR 0xB2 (bits 11:8) but the port-1F/37
// composer in the VHDL reads the same `i_JOY_LEFT(7:0)` lower nibble +
// pass bits — that lower nibble is sourced upstream of the MD6 FSM by
// the host adapter. Mirroring that here as a Joystick-local store keeps
// the composer self-contained and avoids the cross-class coupling the
// brief flagged as the "simpler alternative". The Emulator/UI plumbing
// (Wave 2 Agent E) will fan-out host input to both Joystick and Md6.
// =============================================================================

namespace {

// VHDL joyL_1f / joyR_1f composition for one connector, given its 12-bit
// raw input vector and its Mode. The two enables are computed locally so
// the function stays branch-flat and straight-line:
//
//   md_en   == (mode == Md3Left)        →  bits 7:6 lane active
//   joy_en  == md_en || (mode == Kempston1)  → bits 5:0 lane active
inline uint8_t compose_1f_lane(uint16_t raw12, Joystick::Mode mode) {
    const bool md_en  = (mode == Joystick::Mode::Md3Left);
    const bool joy_en = md_en || (mode == Joystick::Mode::Kempston1);

    const uint8_t hi = md_en  ? static_cast<uint8_t>(raw12 & 0xC0) : 0u;
    const uint8_t lo = joy_en ? static_cast<uint8_t>(raw12 & 0x3F) : 0u;
    return static_cast<uint8_t>(hi | lo);
}

// VHDL joyL_37 / joyR_37 composition for one connector. Symmetric with
// the 0x1F lane but selecting Md3Right + Kempston2.
inline uint8_t compose_37_lane(uint16_t raw12, Joystick::Mode mode) {
    const bool md_en  = (mode == Joystick::Mode::Md3Right);
    const bool joy_en = md_en || (mode == Joystick::Mode::Kempston2);

    const uint8_t hi = md_en  ? static_cast<uint8_t>(raw12 & 0xC0) : 0u;
    const uint8_t lo = joy_en ? static_cast<uint8_t>(raw12 & 0x3F) : 0u;
    return static_cast<uint8_t>(hi | lo);
}

} // namespace

uint8_t Joystick::read_port_1f() const
{
    // VHDL zxnext.vhd:3499 — port_1f_dat <= joyL_1f or joyR_1f.
    const uint8_t l = compose_1f_lane(joy_left_bits_,  joy0_mode_);
    const uint8_t r = compose_1f_lane(joy_right_bits_, joy1_mode_);
    return static_cast<uint8_t>(l | r);
}

uint8_t Joystick::read_port_37() const
{
    // VHDL zxnext.vhd:3506 — port_37_dat <= joyL_37 or joyR_37.
    const uint8_t l = compose_37_lane(joy_left_bits_,  joy0_mode_);
    const uint8_t r = compose_37_lane(joy_right_bits_, joy1_mode_);
    return static_cast<uint8_t>(l | r);
}

// =============================================================================
// NR 0xB2 — Extended MD Pad Buttons (read-only).
//
// VHDL zxnext.vhd:6214-6215:
//   port_253b_dat <= i_JOY_RIGHT(10 downto 8) & i_JOY_RIGHT(11)
//                 &  i_JOY_LEFT (10 downto 8) & i_JOY_LEFT (11);
//
// The register reads the SAME 12-bit connector vectors that the port
// 0x1F / 0x37 composers above read (`i_JOY_LEFT` / `i_JOY_RIGHT`), which is
// why it lives here rather than on the Md6ConnectorX2 cable model: in jnext
// the host adapter presents those vectors directly (JoystickDispatcher ->
// set_joy_left/right), so `joy_left_bits_` / `joy_right_bits_` ARE the
// emulator's i_JOY_* signal.
//
// MODE-INDEPENDENT: unlike the port lanes, this mux applies no NR 0x05
// gating and no Kempston bit-7/6 masking — it reads the raw vectors. Those
// four bits per connector reach NO port under ANY mode (zxnext.vhd:3470-3494
// drives at most bits 7:0, and only 5:0 outside MD mode), so NR 0xB2 is the
// only path by which a guest can observe X / Y / Z / MODE.
//
// On hardware these bits arrive via the MD6 cable FSM, which only latches
// them after detecting a 6-button pad — U and D asserted together during a
// select pulse. That handshake is electrical: a host gamepad reports button
// states, never drives it, so `Md6ConnectorX2` cannot be fed from host input
// and is a reference model only. The 3-button case still behaves correctly
// here: a source that binds no extras leaves bits 11:8 at their reset 0.
// =============================================================================
uint8_t Joystick::nr_b2_byte() const
{
    auto bit = [](uint16_t v, unsigned pos) -> uint8_t {
        return static_cast<uint8_t>((v >> pos) & 1u);
    };
    const uint16_t L = joy_left_bits_;
    const uint16_t R = joy_right_bits_;

    return static_cast<uint8_t>(
        (bit(R, 10) << 7) |   // R.X
        (bit(R,  9) << 6) |   // R.Z
        (bit(R,  8) << 5) |   // R.Y
        (bit(R, 11) << 4) |   // R.MODE
        (bit(L, 10) << 3) |   // L.X
        (bit(L,  9) << 2) |   // L.Z
        (bit(L,  8) << 1) |   // L.Y
        (bit(L, 11) << 0));   // L.MODE
}

// =============================================================================
// G129 — port-decode hw_en gates. VHDL zxnext.vhd:2454-2455:
//   port_1f_hw_en <= joyL_1f_en or joyR_1f_en;
//   port_37_hw_en <= joyL_37_en or joyR_37_en;
// where (zxnext.vhd:3475-3488):
//   joyL_1f_en = '1' when nr_05_joy0 = "001" or mdL_1f_en = '1';
//   joyL_37_en = '1' when nr_05_joy0 = "100" or mdL_37_en = '1';
//   joyR_1f_en = '1' when nr_05_joy1 = "001" or mdR_1f_en = '1';
//   joyR_37_en = '1' when nr_05_joy1 = "100" or mdR_37_en = '1';
//   mdL_1f_en  = '1' when nr_05_joy0 = "101";
//   mdR_1f_en  = '1' when nr_05_joy1 = "101";
//   mdL_37_en  = '1' when nr_05_joy0 = "110";
//   mdR_37_en  = '1' when nr_05_joy1 = "110";
//
// Substituting: port_1f_hw_en is true iff EITHER joy mode ∈ {001, 101}
// (Kempston1 or Md3Left). port_37_hw_en is true iff EITHER joy mode ∈
// {100, 110} (Kempston2 or Md3Right). When the gate is false the port
// decode at zxnext.vhd:2674-2675 fails and the bus floats (0xFF).
//
// The data lane (read_port_1f / read_port_37) already returns 0x00 when
// no joy is in the relevant mode (compose_*_lane masks zero), so the
// final port byte is 0xFF | 0x00 = 0xFF when the handler returns 0xFF
// on a hw_en=0 condition — but the handler must explicitly check this
// gate; the lane on its own is silent.
//
// Pre-G129 emulator.cpp gated only on NR 0x82 b6 (port_1f_io_en); the
// hw_en gate is the OTHER half of VHDL's `port_1f` AND. Both must hold
// for the port to claim the read.
// =============================================================================

bool Joystick::port_1f_hw_en() const
{
    auto active = [](Mode m) {
        return m == Mode::Kempston1 || m == Mode::Md3Left;
    };
    return active(joy0_mode_) || active(joy1_mode_);
}

bool Joystick::port_37_hw_en() const
{
    auto active = [](Mode m) {
        return m == Mode::Kempston2 || m == Mode::Md3Right;
    };
    return active(joy0_mode_) || active(joy1_mode_);
}

// =============================================================================
// Task 60c — state serialisation
// =============================================================================

void Joystick::save_state(StateWriter& w) const
{
    w.write_u8(nr_05_raw_);
    w.write_u8(static_cast<uint8_t>(joy0_mode_));
    w.write_u8(static_cast<uint8_t>(joy1_mode_));
    w.write_u16(joy_left_bits_);
    w.write_u16(joy_right_bits_);
}

void Joystick::load_state(StateReader& r)
{
    nr_05_raw_      = r.read_u8();
    joy0_mode_      = static_cast<Mode>(r.read_u8());
    joy1_mode_      = static_cast<Mode>(r.read_u8());
    joy_left_bits_  = r.read_u16();
    joy_right_bits_ = r.read_u16();
}
