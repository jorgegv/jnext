// GH #452 - bit-identical equivalence of the cycle-batching optimisations, on synthetic fixtures:
// the Copper, peripheral, observer and host-input part (the Copper moves and walk, a varying CTC phase, the EI-grace slot, a replaced HALT byte, NMI hotkey and Copper NMI, DivMMC, DMA, Multiface,
// UART, the debugger, rewind and RZX observers, tape, and the NR 0x08 contention shadow).
// Split from halt_batch_equiv_test to keep each suite inside its CPU budget; the row IDs are
// unchanged. The shared method, fixtures and row helpers are in halt_batch_fixtures.h.
#include "halt_batch_fixtures.h"
#include <memory>

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

// --silent (EmulatorConfig::silent): the batched replay takes its early cluster-only branch and
// must leave the machine silent. An IM 2 EI;HALT program at the given speed.
Scenario silent_halt(const char* name, uint8_t speed) {
    Scenario s = im2_halt(name, speed, 0xFF, 60);
    s.config = [](EmulatorConfig& c) { c.silent = true; };
    return s;
}

// DI;HALT, then the slot stops running the CPU at frame 20 while the CPU is halted (plan gate
// G6): a parked CPU (a load-only NEX, GH #164) or a boot hold (G156). The slot is a 4 T stand-in,
// not a HALT repeat, so it must not be batched.
Scenario halted_then_no_cpu(const char* name, bool park) {
    Scenario s = di_halt(name, 0, 40);
    s.before_frame = [=](Emulator& e, int f) {
        if (f != 20) return;
        if (park) e.set_cpu_parked(true); else e.set_boot_hold_frames(10);
    };
    return s;
}

// ── Stage 4 fixtures: IM2 hardware mode and the DMA burst prescaler wait ──

// CTC channel 0 as a slow timer (prescaler 256, time constant 255: 65280 master cycles, about a
// ninth of a frame), interrupt through the fabric.
void ctc0_slow(Emulator& e) {
    nr(e, 0xC5, 0x01);
    e.port().out(0x183B, 0xA7);       // int enable, timer, /256, auto trigger, TC follows, reset
    e.port().out(0x183B, 255);
}

// Hardware mode, a busy main loop (EI; JR $, so it never HALTs) and a CTC0 handler that does the
// HALT: EI; HALT inside the handler, so every repeat the batch can see is one with CTC0 in S_ISR
// (im2_device.vhd:117-132: it blocks the lower-priority devices through IEI, and the LINE
// interrupt, the top priority, ends the HALT). Vectors in hardware mode are 2 * device index:
// LINE 0, CTC0 6 (the table's other entries stay the shared handler).
Scenario hw_ctc_isr_halt() {
    Scenario s;
    s.name = "hw-ctc-isr-halt";
    s.frames = 120;
    s.setup = [](Emulator& e) {
        nr(e, 0x03, 0x01);
        install_im2_program(e, kHigh);
        constexpr uint16_t kCtcIsr = 0xC300;
        e.mmu().write(static_cast<uint16_t>((kHigh.i_reg << 8) + 6), kCtcIsr & 0xFF);
        e.mmu().write(static_cast<uint16_t>((kHigh.i_reg << 8) + 7), kCtcIsr >> 8);
        // PUSH AF; LD A,(var+2); INC A; LD (var+2),A; EI; HALT; POP AF; EI; RETI
        poke(e, kCtcIsr, {0xF5, 0x3A, 0x02, 0xC2, 0x3C, 0x32, 0x02, 0xC2, 0xFB, 0x76, 0xF1, 0xFB, 0xED, 0x4D});
        // LD A,I_REG; LD I,A; IM 2; EI; JR $
        poke(e, kHigh.main, {0x3E, kHigh.i_reg, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x18, 0xFE});
        nr(e, 0xC0, 0x01);
        ctc0_slow(e);
        nr(e, 0x23, 100);
        nr(e, 0x22, 0x02);                       // line interrupt at line 100
        start_at(e, kHigh.main, 0xBFF0);
    };
    s.alive = [](Emulator& e) { return e.mmu().peek(0xC202) > 20; };   // the CTC handler ran
    return s;
}

// Hardware mode, DI; HALT, the ULA interrupt masked, CTC0 the only source: its request raises the
// /INT line (the device sits in S_REQ) and, with IFF1 = 0, nothing ever takes it. `mark` receives the
// batched-repeat counter at the start of frame 1, by which time the device is already waiting.
Scenario hw_di_halt_req(std::shared_ptr<uint64_t> mark) {
    Scenario s = di_halt("hw-di-halt-req", 0, 40);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        poke(e, kHigh.main, {0xED, 0x5E, 0xF3, 0x76, 0x18, 0xFD});   // IM 2; DI; HALT; JR HALT (the /INT line needs IM 2)
        nr(e, 0xC0, 0x01);
        ctc0_slow(e);
        e.port().out(0xFF, 0x40);                // ULA interrupt off (port 0xFF b6)
    };
    s.before_frame = [=](Emulator& e, int f) {
        if (f == 1) *mark = e.halt_repeats_batched_for_test();
    };
    s.alive = [](Emulator& e) {
        return e.cpu().is_halted() &&
               e.im2().state(Im2Controller::DevIdx::CTC0) == Im2Controller::DevState::S_REQ;
    };
    return s;
}

// The DMA program every burst fixture uses: A -> B burst, source memory 0x9000 incrementing, port B
// as given, `len` bytes, prescaler `presc`; LOAD, ENABLE (through port 0x6B, the ZXN mode port).
void dma_program(Emulator& e, uint16_t len, uint8_t presc, bool b_is_dac, uint8_t mode_byte) {
    for (int i = 0; i < 2048; ++i) e.mmu().write(static_cast<uint16_t>(0x9000 + i), static_cast<uint8_t>(i * 7 + 3));
    std::vector<uint8_t> prog = {
        0x7D, 0x00, 0x90, static_cast<uint8_t>(len & 0xFF), static_cast<uint8_t>(len >> 8),
        0x14,                                           // R1: A is memory, increments
        static_cast<uint8_t>(b_is_dac ? 0x68 : 0x50),   // R2: B is I/O fixed (DAC) or memory incrementing; timing follows
        0x21, presc,                                    // timing byte (prescaler follows), prescaler
        mode_byte, static_cast<uint8_t>(b_is_dac ? 0xDF : 0x00), static_cast<uint8_t>(b_is_dac ? 0x00 : 0x98),
        0xCF, 0x87};                                    // LOAD, ENABLE
    for (uint8_t b : prog) e.port().out(0x6B, b);
}

// EI; HALT at `speed` with a long burst DMA of memory to the DAC port 0xDF behind a prescaler that is
// no multiple of a line, and an AY tone. `aux` receives, after the last frame, the repeats the batch
// absorbed while the DMA waited. `mark1`/`mark4` the DMA state at frames 1 and 4 (still running).
Scenario dma_burst_run(const char* name, uint8_t speed, uint16_t len, uint8_t presc, bool dac, uint8_t mode_byte,
                       int frames, std::shared_ptr<uint64_t> aux) {
    Scenario s = im2_halt(name, speed, 0xFF, frames);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        dma_program(e, len, presc, dac, mode_byte);
    };
    s.after_frame = [=](Emulator& e, int f) {
        if (f == frames - 1) *aux = e.halt_repeats_batched_dma_wait_for_test();
    };
    s.alive = [=](Emulator& e) {
        return isr_ran(e, kHigh) && e.dma().state() == Dma::State::IDLE;
    };
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

    // HBQ-51 / HBQ-52: the --silent branch of the batched replay, at 3.5 and 28 MHz.
    check_ab("HBQ-51", "EI;HALT at 3.5 MHz with --silent: identical per frame, the batch engaged",
             {silent_halt("silent-3.5", 0)}, false, nullptr, Halt::Engaged);
    check_ab("HBQ-52", "EI;HALT at 28 MHz with --silent: identical per frame, the batch engaged",
             {silent_halt("silent-28", 3)}, false, nullptr, Halt::Engaged);

    // HBQ-53 / HBQ-54: the CPU stops running (parked / boot hold) at frame 20 while halted (G6).
    check_ab("HBQ-53", "CPU parked at frame 20 while halted on DI;HALT: identical per frame (no batching of the parked slots)",
             {halted_then_no_cpu("parked-while-halted", true)});
    check_ab("HBQ-54", "boot hold set at frame 20 while halted on DI;HALT: identical per frame (no batching of the held slots)",
             {halted_then_no_cpu("hold-while-halted", false)});

    // HBQ-24/25 (halt_batch_equiv_test) cover hardware mode with the ULA and CTC through the fabric.
    // HBQ-36 / HBQ-37: hardware mode, a HALT inside the CTC0 handler (CTC0 in S_ISR while it waits for
    // the LINE interrupt): identical, and engaged (the only HALTs in this program are those).
    check_pair("HBQ-36", "hardware mode, EI;HALT inside the CTC0 handler (CTC0 in S_ISR) woken by the LINE interrupt: identical per frame",
               "HBQ-37", "same: the HALT batch engaged in hardware mode while CTC0 sat in S_ISR",
               {hw_ctc_isr_halt()});

    // HBQ-38 / HBQ-55: hardware mode, DI;HALT with CTC0 waiting in S_REQ: the /INT line is asserted and
    // never taken, and the batch must not span it. The counter does not grow once the device waits.
    {
        auto mark = std::make_shared<uint64_t>(0);
        std::vector<uint64_t> halt;
        check_ab("HBQ-38", "hardware mode, DI;HALT with CTC0 waiting in S_REQ (the /INT line asserted, IFF1 = 0): identical per frame",
                 {hw_di_halt_req(mark)}, false, &halt);
        check("HBQ-55", "same: the HALT batch does not engage once the device waits (the counter at frame 1 is the counter at the end)",
              !halt.empty() && halt[0] == *mark,
              "mark=" + std::to_string(*mark) + " final=" + (halt.empty() ? std::string("none") : std::to_string(halt[0])));
    }

    // HBQ-26 / HBQ-27: a long burst of memory to the DAC (port 0xDF) behind a prescaler of 55 (a gate period
    // of 1760 master cycles, no multiple of the line), the CPU on EI;HALT at 28 MHz with an AY tone: the
    // audio hash covers the DAC level each byte sets; the batch engages WHILE the DMA waits.
    {
        auto aux = std::make_shared<uint64_t>(0);
        check_ab("HBQ-26", "DMA burst of memory to the DAC behind prescaler 55, CPU on EI;HALT at 28 MHz with an AY tone: identical per frame (video, audio, state), the block completed",
                 {dma_burst_run("dma-dac-28", 3, 1500, 55, true, 0xCD, 60, aux)});
        check("HBQ-27", "same: the HALT batch engaged while the DMA sat in its burst prescaler wait", *aux > 0,
              "repeats batched in the wait=" + std::to_string(*aux));
    }

    // HBQ-40 / HBQ-41: the same at 3.5 MHz with a short block: the HALT spans the wait after the last byte
    // and the block's FINISH_DMA at the gate opening (dma.vhd:458-464).
    {
        auto aux = std::make_shared<uint64_t>(0);
        check_ab("HBQ-40", "DMA burst memory to memory behind prescaler 200, a 3-byte block, CPU on EI;HALT at 3.5 MHz: identical per frame, the block completed",
                 {dma_burst_run("dma-short-35", 0, 3, 200, false, 0xCD, 40, aux)});
        check("HBQ-41", "same: the HALT batch engaged while the DMA sat in its prescaler wait (including after the last byte)", *aux > 0,
              "repeats batched in the wait=" + std::to_string(*aux));
    }

    // HBQ-42: continuous mode with a prescaler holds the bus through the wait, so every slot of it is a DMA
    // slot: identical per frame, and no HALT repeat is ever batched while the DMA waits (a stalled slot is
    // not a repeat: G6's DMA term).
    {
        auto aux = std::make_shared<uint64_t>(0);
        check_ab("HBQ-42", "DMA continuous mode behind prescaler 55 (the bus held through the wait), CPU on EI;HALT at 28 MHz: identical per frame",
                 {dma_burst_run("dma-continuous-28", 3, 300, 55, true, 0xAD, 40, aux)});
        check("HBQ-56", "same: the HALT batch never absorbs a repeat while the DMA holds the bus through its wait", *aux == 0,
              "repeats batched in the wait=" + std::to_string(*aux));
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}

