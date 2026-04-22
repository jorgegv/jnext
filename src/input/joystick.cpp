#include "input/joystick.h"

// =============================================================================
// Phase 2 Agent A — NR 0x05 mode decoder implemented VHDL-faithfully per
// zxnext.vhd:5157-5158. The port 0x1F / 0x37 read composers and the raw
// 12-bit joy_left_bits_ / joy_right_bits_ Kempston/MD3 masking are still
// stubs; Agent B lands those in Wave 2.
// =============================================================================

void Joystick::reset()
{
    // VHDL zxnext.vhd:1105-1106 reset values: joy0="001" (Kempston1),
    // joy1="000" (Sinclair2). The raw NR 0x05 byte that decodes to those
    // two 3-bit fields via zxnext.vhd:5157-5158 is:
    //   joy0 = v[3] & v[7] & v[6] = 0 & 0 & 1  → bit 6 set
    //   joy1 = v[1] & v[5] & v[4] = 0 & 0 & 0  → all clear
    // Composite raw byte = 0b01000000 = 0x40. Storing this keeps
    // nr_05_raw_ round-trip-consistent with the decoded Mode fields.
    nr_05_raw_       = 0x40;
    joy0_mode_       = Mode::Kempston1;
    joy1_mode_       = Mode::Sinclair2;
    joy_left_bits_   = 0;
    joy_right_bits_  = 0;
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
}

void Joystick::set_mode_direct(Mode joy0, Mode joy1)
{
    // Test-only: bypass the NR 0x05 decoder. Documented in the header.
    joy0_mode_ = joy0;
    joy1_mode_ = joy1;
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
