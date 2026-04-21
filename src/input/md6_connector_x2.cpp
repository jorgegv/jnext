#include "input/md6_connector_x2.h"

// =============================================================================
// Phase 1 scaffold — all methods are compile-only stubs. Agent D (Phase 2)
// will implement the shared 9-bit FSM from md6_joystick_connector_x2.vhd:66-193
// and the NR 0xB2 composition at zxnext.vhd:6215.
// =============================================================================

void Md6ConnectorX2::reset()
{
    raw_left_      = 0x0000;
    raw_right_     = 0x0000;
    latched_left_  = 0x0000;
    latched_right_ = 0x0000;
    state_         = 0;
}

void Md6ConnectorX2::set_raw_left(uint16_t bits12)
{
    raw_left_ = bits12 & 0x0FFF;
}

void Md6ConnectorX2::set_raw_right(uint16_t bits12)
{
    raw_right_ = bits12 & 0x0FFF;
}

uint16_t Md6ConnectorX2::joy_left_word() const
{
    // Phase 1 stub: echo the raw word. Agent D returns latched_left_.
    return raw_left_;
}

uint16_t Md6ConnectorX2::joy_right_word() const
{
    return raw_right_;
}

uint8_t Md6ConnectorX2::nr_b2_byte() const
{
    // Phase 1 stub — Agent D implements zxnext.vhd:6215 byte composition.
    return 0;
}

void Md6ConnectorX2::tick(uint32_t /*cycles*/)
{
    // Phase 1 stub — empty. Agent D drives the 9-bit state counter.
}
