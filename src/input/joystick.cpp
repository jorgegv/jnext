#include "input/joystick.h"

// =============================================================================
// Phase 1 scaffold — all methods are compile-only stubs. Real behaviour is
// landed in Phase 2 by agents A (NR 0x05 decoder) and B (port 0x1F/0x37
// composition + Kempston/MD3 bit-7/6 masking).
// =============================================================================

void Joystick::reset()
{
    // VHDL zxnext.vhd:1105-1106 reset values: joy0="001" (Kempston1),
    // joy1="000" (Sinclair2). We cache the raw 8-bit byte that, when
    // written back through set_nr_05(), would reproduce those modes:
    // bit 3 = joy0[2] = 0, bits 7:6 = joy0[1:0] = 01 → bits = 0b00000000
    // bit 1 = joy1[2] = 0, bits 5:4 = joy1[1:0] = 00
    // The canonical reset byte is therefore 0x00 on bits that decode to
    // joy0=001/joy1=000 via zxnext.vhd:5157-5158. Because the decoder is
    // not yet implemented in Phase 1, we just set the Mode enums
    // directly and leave nr_05_raw_ at its default (the exact raw value
    // does not matter until Agent A wires the decoder — at that point
    // this init can be revisited).
    nr_05_raw_       = 0x00;
    joy0_mode_       = Mode::Kempston1;
    joy1_mode_       = Mode::Sinclair2;
    joy_left_bits_   = 0;
    joy_right_bits_  = 0;
}

void Joystick::set_nr_05(uint8_t v)
{
    // Phase 1 stub: record the raw byte; leave joy0/joy1 modes at their
    // current values. Agent A (Phase 2) will replace this body with the
    // real VHDL-faithful bit-extraction:
    //   joy0 = {v[3], v[7], v[6]};
    //   joy1 = {v[1], v[5], v[4]};
    nr_05_raw_ = v;
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
