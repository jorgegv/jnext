// GH #452 - bit-identical equivalence of the cycle-batching optimisations, on synthetic fixtures:
// the Copper, peripheral, observer and host-input part (the Copper moves and walk, a varying CTC phase, the EI-grace slot, a replaced HALT byte, NMI hotkey and Copper NMI, DivMMC, DMA, Multiface,
// UART, the debugger, rewind and RZX observers, tape, and the NR 0x08 contention shadow).
// Split from halt_batch_equiv_test to keep each suite inside its CPU budget; the row IDs are
// unchanged. The shared method, fixtures and row helpers are in halt_batch_fixtures.h.
#include "halt_batch_fixtures.h"

namespace {
// NR 0x08 bit 6 (contention disable) toggled by a line-interrupt handler on a real 128K machine at
// 3.5 MHz with the program in bank 5 (a contended page): while the bit is set a contended page is
// NOT contended (zxnext.vhd:4481), and the write that clears it is a shadow that commits at the
// next hc(8) window, not at once (zxnext.vhd:5822-5823). A HALT repeat in that window is on a page
// that is uncontended now and contended after the commit: the batch must not span it (G11).
Scenario contention_shadow() {
    Scenario s;
    s.name = "contention-shadow";
    s.frames = 120;
    s.config = [](EmulatorConfig& c) {
        c.type = MachineType::ZX128K;
        c.sd_card_image = sd_image();
        c.sd_card_readonly = true;
    };
    s.setup = [](Emulator& e) {
        install_im2_program(e, kBank5);
        const uint8_t vlo = static_cast<uint8_t>(kBank5.var & 0xFF), vhi = static_cast<uint8_t>(kBank5.var >> 8);
        // The handler must write NR 0x08 within the first ~100 T of the line: the commit of the
        // shadow waits for hc(8) (the right half of the display, about T 128), and the HALT repeats
        // between the write and that edge are the ones that see the shadow pending. So it is as short
        // as it can be: EX AF,AF'; LD A,(var); XOR 40h; LD (var),A; NEXTREG 08h,A; EX AF,AF'; EI; RETI
        poke(e, kBank5.isr, {0x08, 0x3A, vlo, vhi, 0xEE, 0x40, 0x32, vlo, vhi, 0xED, 0x92, 0x08,
                             0x08, 0xFB, 0xED, 0x4D});
        e.mmu().write(kBank5.var, 0x40);          // the level the first handler run flips away from
        nr(e, 0x08, 0x40);                        // contention disabled at the start
        nr(e, 0x23, 100);
        nr(e, 0x22, 0x06);                        // ULA interrupt off, line interrupt at line 100
        start_at(e, kBank5.main, 0x8000);
    };
    // The handler ran every frame (it is the only thing that could have toggled the line-interrupt
    // count into the dozens) and the CPU ended halted or in the handler.
    s.alive = [](Emulator& e) { return e.line_int_fire_count() > 50; };
    return s;
}
} // namespace

int main() {
    std::printf("GH #452 cycle-batching equivalence tests (synthetic fixtures, peripherals and observers)\n");
    std::printf("===============================================\n\n");
    // HBQ-15: a host NMI press (F9, Multiface) lands while a frame is in flight and the CPU is
    // halted: not spanned (zxnext.vhd:2090, :2164-2170). The frame is stopped half-way by the
    // debugger's run-to-cycle and resumed; no client is needed, so no new command either.
    check_pair("HBQ-15", "Multiface NMI hotkey pressed mid-frame while halted on DI;HALT (frame stopped by run-to-cycle, resumed): identical per frame",
               "HBQ-16", "same: the NMI was taken (SP moved) and the HALT batch engaged around it",
               {nmi_hotkey()});

    // HBQ-17: NR 0x02 bit 3, a software Multiface NMI, written by a Copper MOVE while halted
    // (zxnext.vhd:3830-3872).
    check_pair("HBQ-17", "NR 0x02 software NMI written by a Copper MOVE while halted: identical per frame",
               "HBQ-18", "same: the NMI was taken and the HALT batch engaged around it",
               {copper_nmi()});

    // HBQ-20: a DivMMC delayed automap in flight when a halted stretch starts: the hold is
    // promoted to held by the NEXT M1 (divmmc.vhd:123-148).
    check_pair("HBQ-20", "DivMMC delayed automap entry in flight at the start of a halted stretch: identical per frame",
               "HBQ-21", "same: the HALT batch engaged once the automap state settled",
               {divmmc_delayed()});

    // HBQ-26: a DMA burst with a prescaler about one frame long, the CPU on EI;HALT
    // (dma.vhd:424, :451-464); engaged after the block ends.
    check_pair("HBQ-26", "DMA burst with a prescaler, CPU on EI;HALT: identical per frame, the block completed",
               "HBQ-27", "same: the HALT batch engaged (after the block)",
               {dma_burst()});

    // HBQ-29: a Multiface state transition in flight at the start of a halted stretch
    // (multiface.vhd:169-186).
    check_ab("HBQ-29", "Multiface port-enable pulse in flight at the start of a HALT below 0x4000: identical per frame",
             {multiface_edge()});

    // HBQ-49: a UART TX byte completing inside what would be a batch, with its interrupt on (G10).
    check_ab("HBQ-49", "UART TX byte in flight on EI;HALT, TX/RX interrupts on, ULA interrupt off: identical per frame, the batch engaged",
             {uart_tx()}, false, nullptr, Halt::Engaged);

    // HBQ-31: a debugger Step is exactly one slot even on a HALT: debugger_step() on DI;HALT and
    // on EI;HALT gives the same T-states, PC and clock with the batching on as off, and batches
    // nothing (GH #207 contract).
    {
        auto run = [](bool batch, bool ei, std::vector<int>& ts, uint64_t& clk, uint16_t& pc, uint64_t& batched) {
            Emulator e;
            EmulatorConfig cfg;
            cfg.type = MachineType::ZXN_ISSUE2;
            cfg.rewind_buffer_frames = 0;
            if (!e.init(cfg)) return false;
            e.set_cycle_batching_for_test(batch, batch);
            nr(e, 0x03, 0x01);
            if (ei) install_im2_program(e, kHigh); else install_di_halt(e, kHigh.main);
            start_at(e, kHigh.main, 0xBFF0);
            for (int i = 0; i < 400; ++i) ts.push_back(e.debugger_step());
            clk = e.clock().get();
            pc = e.cpu().pc();
            batched = e.halt_repeats_batched_for_test();
            return true;
        };
        bool ok = true;
        std::string detail;
        for (bool ei : {false, true}) {
            std::vector<int> ta, tb;
            uint64_t ca = 0, cb = 0, ba = 0, bb = 0;
            uint16_t pa = 0, pb = 0;
            const bool built = run(false, ei, ta, ca, pa, ba) && run(true, ei, tb, cb, pb, bb);
            if (!built || ta != tb || ca != cb || pa != pb || bb != 0) {
                ok = false;
                detail += std::string(ei ? "EI;HALT" : "DI;HALT") + ": clock " + std::to_string(ca) + "/" +
                          std::to_string(cb) + " batched " + std::to_string(bb) + "; ";
            }
        }
        check("HBQ-31", "debugger_step() on DI;HALT and EI;HALT: the same T-states, PC and clock with batching on as off, and nothing batched",
              ok, detail);
    }

    // HBQ-32: every debugger observer refuses the batch (G2) and RZX recording does (G3): a
    // persistent breakpoint set elsewhere, the rewind buffer (it turns the trace on), an RZX
    // recording. Identical per frame, the counter stays 0.
    {
        Scenario brk = di_halt("observer-breakpoint", 0, 40);
        auto b0 = brk.setup;
        brk.setup = [=](Emulator& e) { b0(e); e.debug_state().set_persistent_breakpoints(true); };
        Scenario rew = di_halt("observer-rewind", 0, 40);
        rew.config = [](EmulatorConfig& c) { c.rewind_buffer_frames = 4; };
        // RZX recording is refused on the Next (rzx_refused_by_machine()): a real 48K machine.
        Scenario rzx;
        rzx.name = "observer-rzx";
        rzx.frames = 40;
        const std::string rzx_path = std::string("/tmp/hbq32-") + std::to_string(static_cast<long>(::getpid())) + ".rzx";
        rzx.config = [](EmulatorConfig& c) {
            c.type = MachineType::ZX48K;
            c.sd_card_image = sd_image();
            c.sd_card_readonly = true;
        };
        rzx.setup = [=](Emulator& e) {
            install_di_halt(e, kHigh.main);
            start_at(e, kHigh.main, 0xBFF0);
            e.start_rzx_recording(rzx_path);
        };
        rzx.alive = [](Emulator& e) { return e.cpu().is_halted() && e.rzx_recorder().is_recording(); };
        // A breakpoint AT the HALT: every resume steps off it and the next test stops again, so one
        // resume is exactly one repeat; a batch would carry it far past the breakpoint.
        Scenario bph = di_halt("observer-breakpoint-at-halt", 0, 40);
        auto h0 = bph.setup;
        bph.setup = [=](Emulator& e) {
            h0(e);
            e.debug_state().set_persistent_breakpoints(true);
            e.debug_state().breakpoints().add_pc(static_cast<uint16_t>(kHigh.main + 1));
        };
        bph.before_frame = [](Emulator& e, int) { e.debug_state().resume(); };
        check_ab("HBQ-32", "a persistent breakpoint elsewhere and at the HALT, the rewind buffer, an RZX recording: each refuses the HALT batch (the counter stays 0), identical per frame",
                 {brk, bph, rew, rzx}, false, nullptr, Halt::Refused);
        std::remove(rzx_path.c_str());
    }

    // HBQ-33: a tape playing in real time ticks per instruction and refuses the batch (G4).
    {
        const std::string path = std::string("/tmp/hbq33-") + std::to_string(static_cast<long>(::getpid())) + ".tap";
        {
            // One header block and one data block, 19 + 4 bytes: a pilot alone is seconds long.
            Bytes tap = {19, 0, 0x00, 3, 'h', 'b', 'q', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 4, 0, 0x00, 0x80, 0x00, 0x80, 0};
            uint8_t sum = 0;
            for (size_t i = 2; i + 1 < tap.size(); ++i) sum ^= tap[i];
            tap.back() = sum;
            std::FILE* f = std::fopen(path.c_str(), "wb");
            if (f) { std::fwrite(tap.data(), 1, tap.size(), f); std::fclose(f); }
        }
        Scenario tp = di_halt("tape-playing", 0, 40);
        auto t0 = tp.setup;
        tp.setup = [=](Emulator& e) {
            t0(e);
            e.load_tap(path, /*fast_load=*/false);
            e.tape().start_realtime_playback();
        };
        tp.alive = [](Emulator& e) { return e.cpu().is_halted() && e.tape().is_playing(); };
        check_ab("HBQ-33", "a TAP playing in real time while halted: the HALT batch REFUSED (the counter stays 0), identical per frame",
                 {tp}, false, nullptr, Halt::Refused);
        std::remove(path.c_str());
    }

    // HBQ-34: the Copper walk alone, with a busy CPU: the same palette list started in mode
    // 01 (once), mode 11 (every frame) and mode 10 (continue from the PC, written at frame
    // 5), and a CPU rewriting NR 0x64 across every CVC reload (GH #290). WAIT is met at
    // cvc == vpos && hc >= (hpos << 3) + 12 (copper.vhd:80, :94); the skip must engage.
    check_ab("HBQ-34", "Copper WAIT/MOVE palette splits, modes 01, 10 and 11 and a rewritten NR 0x64, busy CPU: identical per frame, Copper skip engaged",
             {copper_busy("copper-m01", 1), copper_busy("copper-m11", 3),
              copper_mode10(), copper_offset_stress()}, true);

    // HBQ-35: a WAIT reachable only in the tail of the line that straddles two frames
    // (zxula_timing.vhd:455-472; GH181-HCULA-04 in copper_integration_test).
    check_ab("HBQ-35", "Copper WAIT reachable only in the line that straddles two frames, mode 11, busy CPU: identical per frame, Copper skip engaged",
             {copper_straddle()}, true);

    // HBQ-13: the Copper changes what the next HALT repeat fetches and costs: NR 0x07 (the CPU
    // speed) and NR 0x56 / 0x57 (the page holding the HALT), in modes 01 and 11 (copper.vhd:80,
    // :94). Bound B4: the Copper acts in the middle of a would-be batch otherwise.
    check_pair("HBQ-13", "Copper MOVEs to NR 0x07 and to NR 0x56/0x57 (the HALT's page) while halted, modes 01 and 11: identical per frame",
               "HBQ-14", "same: the HALT batch engaged around the Copper actions",
               {copper_moves("copper-moves-m01", 1), copper_moves("copper-moves-m11", 3)});

    // HBQ-46: the HALT byte overwritten while halted (P1: the opcode must really be 0x76).
    check_ab("HBQ-46", "the HALT opcode replaced by LDIR while the CPU is halted (halted flag set, PC unchanged per slot, registers and memory changing): identical per frame",
             {halt_byte_replaced()});

    // HBQ-47: every HALT is the instruction after an EI (the acceptance grace slot) with CTC and ULA
    // interrupts arriving all the time (G7).
    check_ab("HBQ-47", "EI; HALT with the acceptance grace slot, CTC and ULA interrupts pending around it: identical per frame, the batch engaged",
             {ctc_grace()}, false, nullptr, Halt::Engaged);

    // HBQ-48: NR 0x07 written between frames while halted (G11: a pending speed commits at the end of
    // the next slot).
    check_ab("HBQ-48", "NR 0x07 written mid-frame while halted on DI;HALT: identical per frame, the batch engaged",
             {speed_switch()}, false, nullptr, Halt::Engaged);

    // HBQ-44: a CTC channel at a period the handler fits in, ULA interrupt on: the CTC lands at a
    // different phase of the HALT in every frame (bound B3).
    check_pair("HBQ-44", "CTC channel 0 at 3200 master cycles with the ULA interrupt on (a varying CTC phase): identical per frame",
               "HBQ-45", "same: the HALT batch engaged",
               {ctc_phase()});

    // HBQ-50: the NR 0x08 bit-6 shadow (G11): a contended page that is uncontended only until
    // the pending re-enable commits.
    check_ab("HBQ-50", "contention disable toggled by a line-interrupt handler on a contended page (the shadow commit pending while halted): identical per frame, the batch engaged",
             {contention_shadow()}, false, nullptr, Halt::Engaged);

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}

