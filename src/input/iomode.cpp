#include "input/iomode.h"

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
