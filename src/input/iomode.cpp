#include "input/iomode.h"
#include "core/saveable.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

// =============================================================================
// Phase 2 Wave 2 Agent E — NR 0x0B pin-7 mux for modes 00 (static) and 01
// (CTC-toggled). Modes 10/11 (UART) are intentionally left unhandled here;
// the IOMODE-05..10 test rows stay F-skipped pending the future UART+I2C
// subsystem plan. VHDL anchors:
//   zxnext.vhd:1129-1131 — signal declarations
//   zxnext.vhd:3512-3534 — joy_iomode_pin7 update process
//   zxnext.vhd:5200-5203 — NR 0x0B write decoder
// =============================================================================

void IoMode::reset()
{
    // VHDL zxnext.vhd:4939-4941 NR-write reset clears the iomode bits and
    // sets nr_0b_joy_iomode_0 to '1', giving NR 0x0B = 0x01.
    // VHDL zxnext.vhd:3516 sets joy_iomode_pin7 to '1' on hard reset.
    // Mode-00 with iomode_0=1 also yields pin7=1, so the two reset paths
    // agree.
    nr_0b_raw_ = 0x01;
    pin7_      = true;
}

void IoMode::set_nr_0b(uint8_t v)
{
    // VHDL-faithful decode per zxnext.vhd:5200-5203. The fields are
    // accessed via the iomode_en() / iomode_bits() / iomode_0() inline
    // accessors; we just store the raw byte here.
    nr_0b_raw_ = v;

    // VHDL zxnext.vhd:3518-3520 — when the joy_iomode mode is "00" the
    // pin7 register is continuously re-assigned to nr_0b_joy_iomode_0 on
    // every i_CLK_28 rising edge. Synchronously: as soon as the new mode
    // bits indicate "00", pin7 should latch to the new iomode_0 value.
    // (For mode 01 we do NOT touch pin7 here — it only changes on a CTC
    // ch3 ZC/TO pulse, handled by tick_ctc_zc3(). For modes 10/11 the
    // VHDL drives pin7 from uart{0,1}_tx; we leave pin7 untouched here.)
    if (iomode_bits() == 0x00) {
        pin7_ = iomode_0();
    }
}

void IoMode::tick_ctc_zc3()
{
    // VHDL zxnext.vhd:3521-3524:
    //   when "01" =>
    //     if ctc_zc_to(3) = '1' and
    //        (nr_0b_joy_iomode_0 = '1' or joy_iomode_pin7 = '0') then
    //        joy_iomode_pin7 <= not joy_iomode_pin7;
    //     end if;
    // The ctc_zc_to(3)='1' clause is implicit — this method is only
    // called on a ZC/TO pulse from CTC channel 3.
    if (iomode_bits() != 0x01) {
        return;
    }
    if (iomode_0() || !pin7_) {
        pin7_ = !pin7_;
    }
}

// =============================================================================
// Task 60c — state serialisation. Persists the full NR 0x0B mux state. The
// pin7 register is ALSO round-tripped through the legacy single-bool slot in
// Emulator::save_state (kept for save-format backwards-compat); restoring it
// twice to the same value is idempotent.
// =============================================================================

// GH #27 S5 — the ONE field list (design §9.2). Declaration order IS the
// binary stream order, so it must not be disturbed: the byte-identity gate
// (§17.1) pins these 6 bytes inside the `input` block of the 2 292 965-byte
// stream.
//
// `nr_0b_raw_` is the whole NR 0x0B byte; the five booleans are the pin-7 mux
// inputs it selects between.
void IoMode::describe_state(jnext::save::StateDesc& d)
{
    d.u8("nr_0b_raw", nr_0b_raw_);
    d.boolean("pin7", pin7_);
    d.boolean("uart0_tx", uart0_tx_);
    d.boolean("uart1_tx", uart1_tx_);
    d.boolean("joy_left_bit5", joy_left_bit5_);
    d.boolean("joy_right_bit5", joy_right_bit5_);
}

void IoMode::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void IoMode::load_state(StateReader& r)
{
    jnext::save::load_via_desc(*this, r, /*machine_level=*/false);
}
