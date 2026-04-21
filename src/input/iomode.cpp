#include "input/iomode.h"

// =============================================================================
// Phase 1 scaffold — all methods are compile-only stubs. Agent E (Phase 2)
// will fill in NR 0x0B mode 00 (static) and mode 01 (CTC-toggled) per
// zxnext.vhd:3512-3534. UART modes (10/11) stay stubbed pending the
// UART+I2C subsystem plan.
// =============================================================================

void IoMode::reset()
{
    nr_0b_raw_ = 0x00;   // zxnext.vhd:1129-1131 default
    pin7_      = true;   // zxnext.vhd:3516 reset '1'
}

void IoMode::set_nr_0b(uint8_t v)
{
    // Phase 1 stub: record the raw byte; pin-7 stays at reset default.
    // Agent E replaces this with the real decoder:
    //   nr_0b_joy_iomode_en <= v[7];
    //   nr_0b_joy_iomode    <= v[5:4];
    //   nr_0b_joy_iomode_0  <= v[0];
    // and the static-mode pin7 assignment path.
    nr_0b_raw_ = v;
}

void IoMode::tick_ctc_zc3()
{
    // Phase 1 stub — empty. Agent E toggles pin7_ when iomode_bits==01
    // gated by iomode_en().
}

bool IoMode::iomode_en() const
{
    // Phase 1 stub: return 0. Agent E returns bool(nr_0b_raw_ & 0x80).
    return false;
}

uint8_t IoMode::iomode_bits() const
{
    // Phase 1 stub: return 0. Agent E returns (nr_0b_raw_ >> 4) & 0x03.
    return 0;
}
