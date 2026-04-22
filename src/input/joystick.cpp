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

uint8_t Joystick::read_port_1f() const
{
    // Phase 1 stub: preserve pre-scaffold behaviour (the previous
    // port-dispatch handler at emulator.cpp:1381-1386 returned 0x00
    // when the port was decoded and 0xFF when the NR 0x82 bit-6 gate
    // was off). Gating is still enforced by the emulator.cpp wrapper;
    // this returning 0x00 mirrors "Kempston1 mode, no buttons pressed".
    // Agent B (Phase 2) replaces this with the VHDL-faithful mux per
    // zxnext.vhd:3470-3494 with bit-7/6 masking at :3478.
    return 0x00;
}

uint8_t Joystick::read_port_37() const
{
    // Phase 1 stub: preserve pre-scaffold 0x00 return. Agent B (Phase 2)
    // implements the real mux per zxnext.vhd:3480-3494.
    return 0x00;
}
