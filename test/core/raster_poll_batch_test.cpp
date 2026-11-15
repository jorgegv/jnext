// GH #496 - bit-identical equivalence of the raster-poll batch (NR 0x1E/0x1F wait loops) on synthetic
// fixtures: the engaged cases. The refusals are raster_poll_batch_refuse_test. See raster_poll_fixtures.h.
#include "raster_poll_fixtures.h"

int main() {
    std::printf("GH #496 raster-poll batch equivalence tests (synthetic fixtures)\n");
    std::printf("===============================================\n\n");

    // RPB-01: cvc(8) steps at line 256 (zxnext.vhd:5983, zxula_timing.vhd:463-466): a window that ran past
    // the step, or stopped short of it, changes the pass that leaves the wait. The loop is run at four
    // alignments to the line grid (0..3 NOPs in the pass).
    check_poll("RPB-01", "guide viewer loop (reselect NR 0x1E, and 1; jr z, then NR 0x1F == 0), DI, 28 MHz SRAM, four pass lengths: identical per frame, engaged",
               {poll_fx("guide-0", 3, 0xFF, guide_loop()),
                poll_fx("guide-1", 3, 0xFF, guide_loop(0xC000, kCount, {}, 1)),
                poll_fx("guide-2", 3, 0xFF, guide_loop(0xC000, kCount, {}, 2)),
                poll_fx("guide-3", 3, 0xFF, guide_loop(0xC000, kCount, {}, 3))});

    // RPB-02: the ULA frame interrupt (zxula_timing.vhd:551-552) ends the window (B2): the handler runs
    // on the same cycle with the batch on.
    check_poll("RPB-02", "preselected in a,(c); cp n; jr nz, EI, IM 2 handler toggling the beeper, 28 MHz, two pass lengths: identical per frame, engaged",
               {poll_fx("preselected-ei", 3, 0xFF, preselected_loop(0xB4, true)),
                poll_fx("preselected-ei-pad", 3, 0xFF, preselected_loop(0xB4, true, 3))});

    // RPB-03: IN A,(n) puts A on the high address byte: port 0x253B (zxnext.vhd:2819).
    {
        Asm a(0xC000);
        a.op({0xF3, 0x01, 0x3B, 0x24, 0x3E, 0x1F, 0xED, 0x79});          // DI; select NR 0x1F
        a.label("L").op({0x3E, 0x25, 0xDB, 0x3B, 0xFE, 0xB4}).jr(0x20, "L");
        count_up(a);
        a.label("L3").op({0x3E, 0x25, 0xDB, 0x3B, 0xFE, 0xB4}).jr(0x28, "L3").jr(0x18, "L");
        check_poll("RPB-03", "ld a,$25; in a,($3B); cp n; jr nz, DI, 14 MHz: identical per frame, engaged",
                   {poll_fx("in-a-n", 2, 0xFF, a.done())});
    }

    // RPB-04: both registers per pass (the 9-bit line word): each of the two samples is checked on every pass.
    {
        Asm a(0xC000);
        a.op({0xF3});
        auto two_reads = [&](Asm& x) {
            read_reselect(x, 0x1E).op({0x57});                           // ld d,a (the high bit)
            read_reselect(x, 0x1F);                                      // A = the low byte
        };
        a.label("L");
        two_reads(a);
        a.op({0xFE, 0xB4}).jr(0x20, "L");
        count_up(a);
        a.label("L3");
        two_reads(a);
        a.op({0xFE, 0xB4}).jr(0x28, "L3").jr(0x18, "L");
        check_poll("RPB-04", "two reads per pass (NR 0x1E and 0x1F, the 9-bit line word), DI, 7 MHz: identical per frame, engaged",
                   {poll_fx("two-reads", 1, 0xFF, a.done())});
    }

    // RPB-05: a line interrupt (NR 0x22 / 0x23; zxula_timing.vhd:566-577) in the middle of the wait.
    check_poll("RPB-05", "NR 0x22 line interrupt at line 100 while the EI loop polls for line 180, 28 MHz: identical per frame, engaged",
               {poll_fx("line-int", 3, 0xFF, preselected_loop(0xB4, true), 30, 0xC000, [](Emulator& e) {
                    nr(e, 0x23, 100);
                    nr(e, 0x22, 0x06);
                })});

    // RPB-06: the Copper writes palette registers per line (B4: no action inside a window).
    check_poll("RPB-06", "Copper WAIT/MOVE palette splits, mode 11, while the guide loop polls, 28 MHz: identical per frame, engaged",
               {poll_fx("copper", 3, 0xFF, guide_loop(), 30, 0xC000, [](Emulator& e) {
                    paint_paper(e);
                    cu_program(e, kPaletteList);
                    cu_nr(e, 0x62, 0xC0);
                })});

    // RPB-07: CTC channel 0 timer interrupts (ctc_chan.vhd:143-170), period 200 x 16 master cycles (B3).
    check_poll("RPB-07", "CTC channel 0 timer interrupts at a period off the line grid while the EI loop polls, 28 MHz: identical per frame, engaged",
               {poll_fx("ctc", 3, 0xFF, preselected_loop(0xB4, true), 30, 0xC000, [](Emulator& e) {
                    nr(e, 0xC5, 0x01);
                    e.port().out(0x183B, 0x87);
                    e.port().out(0x183B, 200);
                })});

    // RPB-08: a burst DMA to the DAC behind a prescaler (G5, B5) while the loop polls.
    check_poll("RPB-08", "burst DMA to the DAC behind a prescaler while the guide loop polls, 28 MHz: identical per frame, engaged",
               {poll_fx("dma-dac", 3, 0xFF, guide_loop(), 30, 0xC000, [](Emulator& e) {
                    dma_program(e, 1500, 55, true, 0xCD);
                })});

    // RPB-09: the PSG and the mixer are not split-invariant: the replay ticks the PSG per slot of the pass,
    // slots of different lengths, with an AY tone and the beeper toggled by the handler.
    check_poll("RPB-09", "AY tone and a beeper-toggling handler across passes of different slot lengths (28 and 14 MHz): identical audio and state per frame, engaged",
               {poll_fx("audio-28", 3, 0xFF, preselected_loop(0xB4, true, 1)),
                poll_fx("audio-28-pad3", 3, 0xFF, preselected_loop(0xB4, true, 3)),
                poll_fx("audio-14", 2, 0xFF, preselected_loop(0xB4, true, 2))});

    // RPB-10: NR 0x64 moves the origin cvc is reloaded from (zxula_timing.vhd:462).
    check_poll("RPB-10", "NR 0x64 = 37 (the cvc origin moved) while the guide loop polls, 28 MHz: identical per frame, engaged",
               {poll_fx("cvc-offset", 3, 0xFF, guide_loop(), 30, 0xC000, [](Emulator& e) { nr(e, 0x64, 37); })});

    // RPB-11: above 3.5 MHz nothing is contended (zxnext.vhd:4481), so a loop in bank 5 costs the same
    // whatever the beam does.
    check_poll("RPB-11", "guide loop in bank 5 (contended at 3.5 MHz) at 7 MHz: identical per frame, engaged",
               {poll_fx("bank5-7MHz", 1, 0xFF, guide_loop(0x6000, 0x6210), 30, 0x6000, {}, 0x6210)});

    // RPB-13: contention switched off by NR 0x08 b6 at 3.5 MHz (zxnext.vhd:4481).
    check_poll("RPB-13", "3.5 MHz with NR 0x08 b6 (contention disable) set, guide loop in bank 5: identical per frame, engaged",
               {poll_fx("nocontention-3.5", 0, 0xFF, guide_loop(0x6000, 0x6210), 30, 0x6000, [](Emulator& e) {
                    nr(e, 0x08, static_cast<uint8_t>(e.nextreg().peek(0x08) | 0x40));
                }, 0x6210)});

    // RPB-14: a loop whose value changes every line but whose branch does not (cp n; jr c): the value
    // of each absorbed read is checked, not just the branch.
    {
        Asm a(0xC000);
        a.op({0xF3, 0x01, 0x3B, 0x24, 0x3E, 0x1F, 0xED, 0x79, 0x04});   // DI; select NR 0x1F; BC = 0x253B
        a.label("L").op({0xED, 0x78, 0xFE, 0xF0}).jr(0x38, "L");        // wait for line >= 240
        count_up(a);
        a.label("L3").op({0xED, 0x78, 0xFE, 0xF0}).jr(0x30, "L3").jr(0x18, "L");
        check_poll("RPB-14", "wait for line >= 240 with cp n; jr c (the value changes every line, the branch does not), 28 MHz: identical per frame, engaged",
                   {poll_fx("value-changes", 3, 0xFF, a.done())});
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
