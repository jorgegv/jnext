// GH #452 - bit-identical equivalence of the cycle-batching optimisations (the HALT repeat
// batch, the Copper unmet-WAIT skip), on synthetic fixtures: the core part (interrupt sources,
// CPU speed and wait states, contention, the frame end, the rig's own controls).
// The Copper, peripheral, observer and host-input fixtures are halt_batch_periph_test, the NextZXOS
// cold boot halt_batch_boot_test, and every NEX program halt_batch_corpus_*_test; the shared
// method, fixtures and row helpers are in halt_batch_fixtures.h (see its header comment).
#include "halt_batch_fixtures.h"

int main() {
    std::printf("GH #452 cycle-batching equivalence tests (synthetic fixtures, core)\n");
    std::printf("===============================================\n\n");
    // HBQ-01: the ULA frame interrupt (zxula_timing.vhd:551-552, pin zxnext.vhd:1840) ends
    // a HALT: a span that runs past it, or past the pulse window, changes the frame.
    check_pair("HBQ-01", "IM 2 pulse mode, EI;HALT, ULA frame interrupt, 3.5 MHz: identical per frame",
               "HBQ-02", "same: the HALT batch engaged in the 'on' run (a fixture that stops engaging is a named failure)",
               {im2_halt("im2-3.5", 0, 0xFF)});

    // HBQ-03: at 28 MHz an opcode fetch from external SRAM takes one wait state
    // (zxnext.vhd:3171-3181), so a HALT repeat is 5 T there: a fixed 4 T per repeat drifts.
    check_pair("HBQ-03", "same at 28 MHz, HALT in external SRAM (one wait per fetch): identical per frame",
               "HBQ-04", "same: the HALT batch engaged",
               {im2_halt("im2-28-sram", 3, 0xFF)});

    // HBQ-05: the same HALT in bank 7 (BRAM, no wait): 4 T per repeat. A fixed 5 T drifts.
    check_pair("HBQ-05", "same at 28 MHz, HALT in bank-7 BRAM (no wait): identical per frame",
               "HBQ-06", "same: the HALT batch engaged",
               {im2_halt("im2-28-bram", 3, 0x0E)});

    // HBQ-07: the line interrupt (zxula_timing.vhd:574-583) lands mid-frame.
    check_pair("HBQ-07", "line interrupt at line 100 (NR 0x22 / 0x23) while halted: identical per frame",
               "HBQ-08", "same: the HALT batch engaged",
               {line_int()});

    // HBQ-09: CTC channel timer interrupts (ctc_chan.vhd:143-170), pulse mode, at a period
    // that is no multiple of a line.
    check_pair("HBQ-09", "CTC channel 0 timer interrupts, period off the line grid: identical per frame",
               "HBQ-10", "same: the HALT batch engaged (between the bursts of interrupts)",
               {ctc_int()});

    // HBQ-11: with interrupts disabled the CPU stays halted while the ULA interrupt request
    // window opens and expires (t80n.vhd:1742-1772).
    check_pair("HBQ-11", "DI;HALT with the ULA interrupt window expiring unaccepted, 3.5 and 28 MHz: identical per frame",
               "HBQ-12", "same: the HALT batch engaged in both",
               {di_halt("di-halt-3.5", 0), di_halt("di-halt-28", 3)});

    // HBQ-22: a contended page is not a fixed cost (zxula.vhd:579-600, zxnext.vhd:4489-4493).
    check_pair("HBQ-22", "128K, 3.5 MHz, HALT in bank 5 while the display is drawn: identical per frame",
               "HBQ-23", "same: the HALT batch REFUSED on a contended page (the counter stays 0)",
               {contended_bank5()}, /*expect_engaged=*/false);

    // HBQ-24: IM 2 hardware mode (im2_device.vhd:111-150) takes CTC and ULA through the fabric
    // (im2_peripheral.vhd:154-194, zxnext.vhd:1840; ctc_chan.vhd:162-182). The batch engages in it
    // (stage 4): a quiescent fabric is the same no-op per repeat in either mode.
    check_pair("HBQ-24", "IM 2 hardware mode (NR 0xC0 b0) with CTC and ULA through the fabric: identical per frame",
               "HBQ-25", "same: the HALT batch ENGAGED in hardware mode (stage 4; the counter grows)",
               {im2_hardware()});

    // HBQ-28: an idle DI;HALT across 60 frames reaches every frame's last line and the
    // frame end (run_frame's loop, emulator.cpp).
    check_ab("HBQ-28", "DI;HALT idle across 60 frames, every frame end reached: identical per frame, the batch engaged",
             {di_halt("idle-frames", 3, 60)}, false, nullptr, Halt::Engaged);

    // HBQ-30: execute_single_instruction() is exactly one slot. A HALT repeat is the real
    // opcode fetch (t80n.vhd:496-503): 4 T (32 master cycles) at 3.5 MHz, 5 T (5 master
    // cycles) at 28 MHz from external SRAM, 4 T (4) from BRAM.
    {
        struct Step { int t; uint64_t dc; };
        auto one_repeat = [](uint8_t speed, uint8_t page6) -> Step {
            Emulator e;
            EmulatorConfig cfg;
            cfg.type = MachineType::ZXN_ISSUE2;
            cfg.rewind_buffer_frames = 0;
            if (!e.init(cfg)) return {-1, 0};
            e.set_cycle_batching_for_test(true, true);
            nr(e, 0x03, 0x01);
            if (speed) nr(e, 0x07, speed);
            if (page6 != 0xFF) { nr(e, 0x56, page6); nr(e, 0x57, static_cast<uint8_t>(page6 + 1)); }
            install_di_halt(e, kHigh.main);
            start_at(e, kHigh.main, 0xBFF0);
            e.execute_single_instruction();   // DI
            e.execute_single_instruction();   // HALT
            e.execute_single_instruction();   // first repeat
            const uint64_t c0 = e.clock().get();
            const int t = e.execute_single_instruction();
            return {t, e.clock().get() - c0};
        };
        const Step s35 = one_repeat(0, 0xFF);
        const Step s28 = one_repeat(3, 0xFF);
        const Step s28b = one_repeat(3, 0x0E);
        check("HBQ-30", "execute_single_instruction() on a HALT is one repeat: 4 T / 32 mc at 3.5 MHz, 5 T / 5 mc at 28 MHz SRAM, 4 T / 4 mc BRAM",
              s35.t == 4 && s35.dc == 32 && s28.t == 5 && s28.dc == 5 && s28b.t == 4 && s28b.dc == 4,
              "3.5: t=" + std::to_string(s35.t) + " mc=" + std::to_string(s35.dc) +
              "; 28 sram: t=" + std::to_string(s28.t) + " mc=" + std::to_string(s28.dc) +
              "; 28 bram: t=" + std::to_string(s28b.t) + " mc=" + std::to_string(s28b.dc));
    }

    // The rig's own controls. An "identical" row proves nothing if the rig is blind to a
    // component or to a frame, so each control disturbs EXACTLY ONE component of EXACTLY ONE
    // late frame (k = 40, past frame 0) and asserts the exact first_diff text. Every
    // comparison is unbatched vs unbatched: only the disturbance differs.
    constexpr int kCtl = 40;
    auto control_run = [&](std::function<void(Emulator&, int)> after) {
        Scenario sc = im2_halt("rig-control", 0, 0xFF, 60);
        Scenario mut = sc;
        mut.after_frame = after;
        const Run a = batch_ab::run(sc, false, false, g_scratch);
        const Run b = batch_ab::run(mut, false, false, g_scratch);
        return batch_ab::first_diff(a, b);
    };

    // HBQ-90: determinism, and that the hashes cover what they claim: two unbatched runs of
    // one fixture are identical; the run has audio samples, an audio hash that changes from
    // frame to frame (the beeper toggles), a video hash and a state that change.
    {
        const Scenario sc = im2_halt("rig-control", 0, 0xFF, 60);
        const Run a = batch_ab::run(sc, false, false, g_scratch);
        const Run b = batch_ab::run(sc, false, false, g_scratch);
        bool varies = a.built && a.frames.size() > 2 && a.audio_pairs > 0;
        if (varies) {
            bool audio_changes = false, state_changes = false;
            for (size_t i = 1; i < a.frames.size(); ++i) {
                audio_changes |= a.frames[i].audio != a.frames[i - 1].audio;
                state_changes |= a.frames[i].state != a.frames[i - 1].state;
            }
            varies = audio_changes && state_changes;
        }
        check("HBQ-90", "rig control: two unbatched runs are identical; the run has audio samples, an audio hash and a state that change from frame to frame",
              batch_ab::first_diff(a, b).empty() && varies,
              "diff=" + batch_ab::first_diff(a, b) + " audio_pairs=" + std::to_string(a.audio_pairs));
    }

    // HBQ-91: a one-pixel framebuffer change (not in save_state) at frame 40 is reported as
    // exactly "frame 40 video": the video hash is live, and compared on every frame.
    {
        const std::string d = control_run([&](Emulator& e, int f) {
            if (f == kCtl) e.get_framebuffer()[1000] ^= 0x00010101u;
        });
        check("HBQ-91", "rig control: one framebuffer pixel changed at frame 40 is reported as exactly 'frame 40 video'",
              d == "frame 40 video", "diff=" + d);
    }

    // HBQ-92: one stereo pair taken out of the mixer at frame 40 is reported as exactly
    // "frame 40 audio": the audio hash (samples and count) is live. Taking a pair out is
    // not machine state.
    {
        const std::string d = control_run([&](Emulator& e, int f) {
            if (f == kCtl) { int16_t pair[2]; e.mixer().read_samples(pair, 1); }
        });
        check("HBQ-92", "rig control: one audio pair taken from the mixer at frame 40 is reported as exactly 'frame 40 audio'",
              d == "frame 40 audio", "diff=" + d);
    }

    // HBQ-93: one unused RAM byte written at frame 40 (no video or audio effect) is
    // reported as exactly "frame 40 state": the save_state hash is live.
    {
        const std::string d = control_run([&](Emulator& e, int f) {
            if (f == kCtl) e.mmu().write(0x9000, 0x5A);
        });
        check("HBQ-93", "rig control: one unused RAM byte written at frame 40 is reported as exactly 'frame 40 state'",
              d == "frame 40 state", "diff=" + d);
    }

    // HBQ-94: a fixture that differs from the first instruction (its handler no longer
    // toggles the beeper) is reported as different at frame 0.
    {
        Scenario sc = im2_halt("rig-discriminates", 0, 0xFF, 60);
        Scenario mutated = sc;
        auto base = sc.setup;
        mutated.setup = [=](Emulator& e) {
            base(e);
            e.mmu().write(static_cast<uint16_t>(kHigh.isr + 5), 0x00);   // XOR 10h -> XOR 00h
        };
        const Run a = batch_ab::run(sc, false, false, g_scratch);
        const Run b = batch_ab::run(mutated, false, false, g_scratch);
        const std::string d = batch_ab::first_diff(a, b);
        check("HBQ-94", "rig control: a fixture whose handler stops toggling the beeper is reported as different",
              d.rfind("frame ", 0) == 0, "diff=" + d);
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}

