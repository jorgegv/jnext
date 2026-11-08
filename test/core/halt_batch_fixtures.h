#pragma once

// GH #452 - bit-identical equivalence of the cycle-batching optimisations
// (the HALT repeat batch, the Copper unmet-WAIT skip), on synthetic fixtures.
//
// Method (batch_ab.h): the same machine is built twice, once with the batching
// switches off (the unbatched path = the oracle, held to the VHDL by the other
// suites and the screenshot regression) and once with them on; both run the
// same program for the same frames; the framebuffer, the drained audio and the
// full save_state() stream must agree after EVERY frame. A row names the VHDL
// behaviour its fixture makes a batch span step over, and the mutation (a gate
// or bound dropped) it exists to kill.
//
// Every fixture also carries a liveness predicate on the final machine (its ISR
// ran, the CPU is halted, ...): a fixture that silently stops doing its job is a
// named failure, not a pass.
//
// There is no VHDL counterpart to a suite whose oracle is another path of the
// same emulator, so the rows cite the hardware behaviour a span must not move
// and the traceability matrix tombstones the suite as jnext-internal.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "batch_ab.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unistd.h>
#include <ctime>
#include <functional>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

using batch_ab::Run;
using batch_ab::Scenario;
using batch_ab::Scratch;

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using Bytes = std::vector<uint8_t>;

std::string sd_image() {
    if (const char* env = std::getenv("JNEXT_TEST_SD_IMAGE")) return env;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.jnext/sdcard/cspect-next-1gb-fixed.img";
}

// ── Machine access ───────────────────────────────────────────────────

void nr(Emulator& e, uint8_t reg, uint8_t val) {
    e.port().out(0x243B, reg);
    e.port().out(0x253B, val);
}

void poke(Emulator& e, uint16_t addr, const Bytes& b) {
    for (size_t i = 0; i < b.size(); ++i) e.mmu().write(static_cast<uint16_t>(addr + i), b[i]);
}

void ay_reg(Emulator& e, uint8_t reg, uint8_t val) {
    e.port().out(0xFFFD, reg);
    e.port().out(0xBFFD, val);
}

// ── The shared program ───────────────────────────────────────────────
//
// An IM 2 interrupt routine that toggles the beeper and counts itself, and a
// main loop that does nothing but EI; HALT. The vector table is 257 bytes of
// one value, so whichever vector byte the bus supplies the handler address is
// the same: it sits at an address whose two bytes are equal.
struct Layout {
    uint16_t main;      // main loop
    uint16_t isr;       // handler, both bytes equal
    uint16_t var;       // var+0 = beeper level, var+1 = interrupt count
    uint8_t  i_reg;     // high byte of the vector table
};
constexpr Layout kHigh = {0xC000, 0xC1C1, 0xC200, 0xFD};   // slot 6/7, external SRAM or BRAM
constexpr Layout kBank5 = {0x6000, 0x6161, 0x6200, 0x70};  // bank 5: the contended page

void install_im2_program(Emulator& e, const Layout& L) {
    const uint8_t hi = static_cast<uint8_t>(L.isr >> 8);
    for (int i = 0; i < 257; ++i)
        e.mmu().write(static_cast<uint16_t>((L.i_reg << 8) + i), hi);
    const uint8_t vlo = static_cast<uint8_t>(L.var & 0xFF), vhi = static_cast<uint8_t>(L.var >> 8);
    const uint8_t clo = static_cast<uint8_t>(vlo + 1);
    // PUSH AF; LD A,(var); XOR 10h; LD (var),A; OUT (FEh),A;
    // LD A,(var+1); INC A; LD (var+1),A; POP AF; EI; RETI
    poke(e, L.isr, {0xF5, 0x3A, vlo, vhi, 0xEE, 0x10, 0x32, vlo, vhi, 0xD3, 0xFE,
                    0x3A, clo, vhi, 0x3C, 0x32, clo, vhi, 0xF1, 0xFB, 0xED, 0x4D});
    // LD A,I_REG; LD I,A; IM 2; EI; HALT; JR HALT
    poke(e, L.main, {0x3E, L.i_reg, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD});
}

// DI; HALT; JR HALT: the CPU halts for good, interrupts never accepted.
void install_di_halt(Emulator& e, uint16_t at) {
    poke(e, at, {0xF3, 0x76, 0x18, 0xFD});
}

void start_at(Emulator& e, uint16_t pc, uint16_t sp) {
    auto r = e.cpu().get_registers();
    r.PC = pc;
    r.SP = sp;
    e.cpu().set_registers(r);
}

void ay_tone_on(Emulator& e) {
    ay_reg(e, 7, 0x3E);       // tone A on, everything else off
    ay_reg(e, 8, 0x0F);       // volume A 15
    ay_reg(e, 0, 0x40);       // period
}

bool isr_ran(Emulator& e, const Layout& L) {
    return e.mmu().peek(static_cast<uint16_t>(L.var + 1)) != 0;
}

// ── Fixtures ─────────────────────────────────────────────────────────

// A Next at `speed` (NR 0x07 low bits: 0 = 3.5, 1 = 7, 2 = 14, 3 = 28 MHz) running the
// shared IM 2 program, whose HALT sits in the given pages (0xFF = leave the default SRAM).
Scenario im2_halt(const char* name, uint8_t speed, uint8_t page6, int frames = 120) {
    Scenario s;
    s.name = name;
    s.frames = frames;
    s.setup = [=](Emulator& e) {
        nr(e, 0x03, 0x01);                       // leave configuration mode
        if (speed) nr(e, 0x07, speed);
        if (page6 != 0xFF) { nr(e, 0x56, page6); nr(e, 0x57, static_cast<uint8_t>(page6 + 1)); }
        ay_tone_on(e);
        install_im2_program(e, kHigh);
        start_at(e, kHigh.main, 0xBFF0);
    };
    // Not is_halted(): a frame ends wherever the CPU happens to be, often inside the handler.
    s.alive = [](Emulator& e) { return isr_ran(e, kHigh); };
    return s;
}

// ULA frame interrupt masked, line interrupt at line 100 (NR 0x22 / 0x23).
Scenario line_int() {
    Scenario s = im2_halt("line-int", 0, 0xFF);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        nr(e, 0x23, 100);
        nr(e, 0x22, 0x06);       // b2 = disable the ULA interrupt, b1 = enable the line interrupt
    };
    return s;
}

// CTC channel 0 as a timer, period 59 x 16 = 944 master (28 MHz) cycles = 118 T at 3.5 MHz
// (the CTC counts the 28 MHz clock), which is no multiple of the 1824-cycle line. The
// handler (about 132 T with the acknowledge) outlasts one period, so the pattern, measured,
// is three back-to-back interrupts and then one HALT of 632 master cycles, and the fixture
// is halted only about 17% of the time. Because that pattern repeats, a CTC event lands
// at a FIXED offset from the HALT; a second fixture with a varying CTC phase is stage 3's.
// Pulse mode (NR 0xC0 b0 = 0): an enabled CTC request drives a /INT pulse there as well
// (im2_peripheral.vhd:184-187, zxnext.vhd:2270-2305, :2326-2339, :2150). The ULA interrupt
// is masked so the CTC is the only source.
Scenario ctc_int() {
    Scenario s = im2_halt("ctc-int", 0, 0xFF);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        nr(e, 0x22, 0x04);                       // ULA interrupt off
        nr(e, 0xC5, 0x01);                       // CTC channel 0 may interrupt
        e.port().out(0x183B, 0x87);              // int enable, timer, /16, auto trigger, TC follows, reset
        e.port().out(0x183B, 59);
    };
    return s;
}

// IM 2 hardware mode (NR 0xC0 bit 0) with the ULA frame interrupt and the CTC both
// going through the fabric. The handler ends in RETI, which the fabric needs.
Scenario im2_hardware() {
    Scenario s = im2_halt("im2-hardware", 0, 0xFF);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        nr(e, 0xC0, 0x01);
        nr(e, 0xC5, 0x01);
        e.port().out(0x183B, 0x87);
        e.port().out(0x183B, 59);
    };
    return s;
}

// DI; HALT with the ULA interrupt firing every frame and never accepted: its
// request window opens and expires with the CPU halted.
Scenario di_halt(const char* name, uint8_t speed, int frames = 120) {
    Scenario s;
    s.name = name;
    s.frames = frames;
    s.setup = [=](Emulator& e) {
        nr(e, 0x03, 0x01);
        if (speed) nr(e, 0x07, speed);
        install_di_halt(e, kHigh.main);
        start_at(e, kHigh.main, 0xBFF0);
    };
    s.alive = [](Emulator& e) { return e.cpu().is_halted(); };
    return s;
}

// ── Copper-only fixtures (a busy CPU, no HALT) ───────────────────────

void cu_nr(Emulator& e, uint8_t reg, uint8_t val) { nr(e, reg, val); }

// Programs the Copper RAM from word 0 through NR 0x61 / 0x62 / 0x63, leaving it stopped.
void cu_program(Emulator& e, const std::vector<uint16_t>& words) {
    cu_nr(e, 0x62, 0x00);
    cu_nr(e, 0x61, 0x00);
    for (uint16_t w : words) {
        cu_nr(e, 0x63, static_cast<uint8_t>(w >> 8));
        cu_nr(e, 0x63, static_cast<uint8_t>(w & 0xFF));
    }
}
constexpr uint16_t cu_move(uint8_t reg, uint8_t val) { return static_cast<uint16_t>((reg << 8) | val); }
constexpr uint16_t cu_wait(uint8_t hpos, uint16_t vpos) {
    return static_cast<uint16_t>(0x8000 | ((hpos & 0x3F) << 9) | (vpos & 0x1FF));
}

// A visible ULA screen (attribute 0x38 = black ink on white paper, paper colour = palette
// index 23) for the Copper to recolour line by line with MOVEs to NR 0x40 / 0x41.
void paint_paper(Emulator& e) {
    for (uint16_t a = 0x5800; a < 0x5B00; ++a) e.mmu().write(a, 0x38);
}

// A busy CPU: DI; JR $. No HALT, so only the Copper walk is under test.
void busy_cpu(Emulator& e) {
    poke(e, kHigh.main, {0xF3, 0x18, 0xFE});
    start_at(e, kHigh.main, 0xBFF0);
}

// Copper list: three palette changes of paper (index 23) at three lines, then HALT.
const std::vector<uint16_t> kPaletteList = {
    cu_wait(0, 60),  cu_move(0x40, 23), cu_move(0x41, 0xE0),
    cu_wait(20, 120), cu_move(0x41, 0x1C),
    cu_wait(40, 200), cu_move(0x41, 0x03),
    cu_wait(0, 511),                                    // HALT
};
constexpr uint16_t kPaletteHaltPc = 7;

// mode: NR 0x62 bits 7:6 started with (1 = start at 0, 3 = restart each frame).
Scenario copper_busy(const char* name, uint8_t mode) {
    Scenario s;
    s.name = name;
    s.frames = 120;
    s.setup = [=](Emulator& e) {
        nr(e, 0x03, 0x01);
        paint_paper(e);
        busy_cpu(e);
        cu_program(e, kPaletteList);
        cu_nr(e, 0x62, static_cast<uint8_t>(mode << 6));
    };
    s.alive = [](Emulator& e) { return e.copper().pc() == kPaletteHaltPc; };
    return s;
}

// Mode 10 (continue from the PC) entered with the Copper mid-list. The list's second WAIT
// is for cvc 280, which is only reached after the frame boundary (cvc 247..310 lie in the
// first 64 raw lines of the next frame), so at every frame boundary the PC stands on that
// WAIT. Started in mode 01, stopped (00) at frame 1 with the PC there, set to mode 10 at
// frame 3: the Copper resumes at the WAIT, meets it, runs the MOVE and reaches the HALT.
Scenario copper_mode10() {
    Scenario s;
    s.name = "copper-m10";
    s.frames = 120;
    s.setup = [](Emulator& e) {
        nr(e, 0x03, 0x01);
        paint_paper(e);
        busy_cpu(e);
        cu_program(e, {cu_wait(0, 60), cu_move(0x40, 23), cu_move(0x41, 0xE0),
                       cu_wait(0, 280), cu_move(0x41, 0x1C), cu_wait(0, 511)});
        cu_nr(e, 0x62, 0x40);
    };
    s.before_frame = [](Emulator& e, int f) {
        if (f == 1) cu_nr(e, 0x62, 0x00);
        if (f == 3) cu_nr(e, 0x62, 0x80);
    };
    s.alive = [](Emulator& e) { return e.copper().pc() == 5; };
    return s;
}

// NR 0x64 (the Copper line offset) rewritten by the CPU all the time, so it changes across
// every CVC reload (GH #290): LD A,0; loop: NEXTREG 0x64,A; INC A; JR loop.
Scenario copper_offset_stress() {
    Scenario s = copper_busy("copper-offset-stress", 3);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        // LD A,0; loop: NEXTREG 0x64,A; INC A; LD (var),A; JR loop
        poke(e, kHigh.main, {0x3E, 0x00, 0xED, 0x92, 0x64, 0x3C, 0x32, 0x00, 0xC2, 0x18, 0xF7});
        start_at(e, kHigh.main, 0xBFF0);
    };
    // The CPU loop ran (it stores its counter) and the Copper list was walked.
    s.alive = [](Emulator& e) { return e.mmu().peek(0xC200) != 0 && e.copper().is_running(); };
    return s;
}

// A WAIT reachable only in the line that straddles two frames (cvc 246 = hc_ula line lpf-1 of
// the 64-line display origin): hc_ula 332 is in the tail
// that belongs to raw line 0 of the NEXT jnext frame (GH181-HCULA-04's case). Mode 11.
Scenario copper_straddle() {
    Scenario s;
    s.name = "copper-straddle";
    s.frames = 120;
    s.setup = [](Emulator& e) {
        nr(e, 0x03, 0x01);
        paint_paper(e);
        busy_cpu(e);
        cu_program(e, {cu_wait(40, 246), cu_move(0x40, 23), cu_move(0x41, 0x77), cu_wait(0, 511)});
        cu_nr(e, 0x62, 0xC0);
    };
    // The straddling WAIT was met and both MOVEs ran: the Copper stands at its HALT (word 3).
    s.alive = [](Emulator& e) { return e.copper().pc() == 3; };
    return s;
}

// CTC channel 0, period 200 x 16 = 3200 master cycles (400 T at 3.5 MHz), with the ULA frame
// interrupt left ON: the handler (~132 T) now fits between two CTC interrupts, so the CPU
// halts for stretches, and because 568k / 3200 is not an integer the CTC lands at a different
// offset from the frame interrupt, the HALT and the line grid in every frame. A CTC event
// then falls in the middle of a would-be batch at every phase (bound B3).
Scenario ctc_phase() {
    Scenario s = im2_halt("ctc-phase", 0, 0xFF);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        nr(e, 0xC5, 0x01);                       // CTC channel 0 may interrupt
        e.port().out(0x183B, 0x87);              // int enable, timer, /16, auto trigger, TC follows, reset
        e.port().out(0x183B, 200);
    };
    return s;
}

// The Copper changes what the next HALT repeat fetches and costs: WAIT then MOVE NR 0x07 (28 MHz,
// then 3.5 again), then MOVE NR 0x56 / 0x57 to remap the page holding the HALT onto a copy of the
// program in pages 0x0E / 0x0F, and back (copper.vhd:80, :94). `mode`: 1 = once, 3 = every frame.
Scenario copper_moves(const char* name, uint8_t mode) {
    Scenario s = im2_halt(name, 0, 0xFF);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        // A copy of the program in pages 0x0E / 0x0F, mapped in only for the copy.
        nr(e, 0x56, 0x0E); nr(e, 0x57, 0x0F);
        install_im2_program(e, kHigh);
        nr(e, 0x56, 0x00); nr(e, 0x57, 0x01);
        cu_program(e, {
            cu_wait(0, 80),   cu_move(0x07, 0x03),                       // 28 MHz
            cu_wait(0, 120),  cu_move(0x07, 0x00),                       // back to 3.5 MHz
            cu_wait(0, 160),  cu_move(0x56, 0x0E), cu_move(0x57, 0x0F),  // HALT page -> the copy
            cu_wait(0, 200),  cu_move(0x56, 0x00), cu_move(0x57, 0x01),  // and back
            cu_wait(0, 511)});
        cu_nr(e, 0x62, static_cast<uint8_t>(mode << 6));
    };
    s.alive = [](Emulator& e) { return isr_ran(e, kHigh); };
    return s;
}

// A host NMI press (F9, Multiface) while the CPU sits on DI;HALT and a frame is in flight: the
// debugger's run-to-cycle stops the frame half-way (it needs no client), the machine is
// resumed, the press lands, and the next run_frame() finishes that frame. The press is pending
// at the first slot start of the resumed frame: it must not be spanned (zxnext.vhd:2090,
// :2164-2170). Between frames the first slot is never a batch, so only this shape shows it.
Scenario nmi_hotkey() {
    Scenario s = di_halt("nmi-hotkey", 0, 40);
    auto base = s.setup;
    auto stops = std::make_shared<int>(0);
    s.setup = [=](Emulator& e) {
        base(e);
        *stops = 0;
        e.nmi_source().set_mf_enable(true);
    };
    s.before_frame = [=](Emulator& e, int f) {
        if (f != 10 && f != 25) return;
        // run-to-cycle only stops an ARMED machine (the stop test is in run_frame()'s armed
        // block), so arm it with persistent breakpoints for the stop and disarm it again, as
        // a detaching client does: the resumed frame is then unarmed and may batch.
        e.debug_state().set_persistent_breakpoints(true);
        e.debug_state().run_to_cycle(e.clock().get() + 400000);
        e.run_frame();                       // stops mid-frame
        if (e.debug_state().paused()) ++*stops;
        e.debug_state().resume();
        e.debug_state().set_persistent_breakpoints(false);
        e.on_hotkey_f9_mf_nmi();
    };
    // Both presses landed mid-frame (the frame really was stopped), and the NMI was taken.
    s.alive = [=](Emulator& e) { return *stops == 2 && e.cpu().registers().SP != 0xBFF0; };
    return s;
}

// NR 0x02 software NMI (bit 3 = Multiface) written by a Copper MOVE while the CPU halts on DI;HALT
// (zxnext.vhd:3830-3872): the Copper acts mid-batch otherwise (bound B4).
Scenario copper_nmi() {
    Scenario s = di_halt("copper-nmi", 0, 60);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        e.nmi_source().set_mf_enable(true);
        cu_program(e, {cu_wait(0, 100), cu_move(0x02, 0x08), cu_wait(0, 511)});
        cu_nr(e, 0x62, 0x40);
    };
    s.alive = [](Emulator& e) { return e.cpu().registers().SP != 0xBFF0; };
    return s;
}

// DI; HALT at 0x3000 in RAM mapped over the ROM slot (NR 0x51 -> page 0x10): below 0x4000 the
// Multiface and the DivMMC overlay memory over the fetch, so a state change in either decides
// what the NEXT repeat fetches. (Above 0x4000 their transitions are invisible to the CPU and
// settle at the next real slot, which is why a HALT up there cannot tell the gates apart.)
Scenario low_halt(const char* name, int frames) {
    Scenario s;
    s.name = name;
    s.frames = frames;
    s.setup = [](Emulator& e) {
        nr(e, 0x03, 0x01);
        nr(e, 0x51, 0x10);
        poke(e, 0x3000, {0xF3, 0x76, 0x18, 0xFD});
        start_at(e, 0x3000, 0xBFF0);
    };
    s.alive = [](Emulator& e) { return e.cpu().is_halted(); };
    return s;
}

// A Multiface state transition in flight at the start of a halted stretch: the port-enable
// pulse sets the Multiface FSM going and it takes MORE than one M1 edge to show in the memory
// it overlays (multiface.vhd:169-186), so the second repeat's fetch is not the first's.
Scenario multiface_edge() {
    Scenario s = low_halt("multiface-edge", 40);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        e.multiface().set_enabled(true);
    };
    s.before_frame = [](Emulator& e, int f) {
        if (f == 10) e.multiface().on_port_enable_rd(true);
    };
    return s;
}

// A DivMMC delayed automap in flight at the start of a halted stretch: the entry latches the
// hold, the NEXT M1 promotes hold to held (divmmc.vhd:123-148), so repeats of one M1 differ.
Scenario divmmc_delayed() {
    Scenario s = low_halt("divmmc-delayed", 40);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        e.divmmc().set_enabled(true);
        e.divmmc().set_port_io_enable(true);
    };
    s.before_frame = [](Emulator& e, int f) {
        if (f == 10) e.divmmc().check_automap(0x3D00, true, true, true);   // the delayed entry's M1
    };
    return s;
}

// The HALT byte overwritten while the CPU is halted: the halted flag stays set but the opcode at PC is
// no longer 0x76 (here LDIR, which leaves PC where it is while BC counts down): a "HALT repeat" by PC
// and flag alone, though every execution changes registers and memory.
Scenario halt_byte_replaced() {
    Scenario s = di_halt("halt-byte-replaced", 0, 40);
    s.before_frame = [](Emulator& e, int f) {
        if (f != 10) return;
        auto r = e.cpu().get_registers();
        r.BC = 0x0200; r.HL = 0x9000; r.DE = 0x9800;
        e.cpu().set_registers(r);
        e.mmu().write(kHigh.main + 1, 0xED);
        e.mmu().write(kHigh.main + 2, 0xB0);
    };
    s.alive = [](Emulator& e) { return e.mmu().peek(0x9800) == e.mmu().peek(0x9000); };
    return s;
}

// The EI-grace slot: ISR returns WITHOUT EI and the main loop is EI; HALT; JR, so every HALT is the
// instruction right after an EI, where an interrupt is not accepted (t80n.vhd:1768) even if its request
// is already pending. CTC channel 0 interrupts at 3200 master cycles (and the ULA's) land in that slot often.
Scenario ctc_grace() {
    Scenario s = ctc_phase();
    s.name = "ctc-grace";
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        const uint8_t vlo = static_cast<uint8_t>(kHigh.var & 0xFF), vhi = static_cast<uint8_t>(kHigh.var >> 8);
        const uint8_t clo = static_cast<uint8_t>(vlo + 1);
        // ISR without the EI: PUSH AF; LD A,(var); XOR 10h; LD (var),A; OUT (FEh),A; LD A,(var+1); INC A;
        // LD (var+1),A; POP AF; RETI
        poke(e, kHigh.isr, {0xF5, 0x3A, vlo, vhi, 0xEE, 0x10, 0x32, vlo, vhi, 0xD3, 0xFE,
                            0x3A, clo, vhi, 0x3C, 0x32, clo, vhi, 0xF1, 0xED, 0x4D});
        // LD A,I_REG; LD I,A; IM 2; loop: EI; HALT; JR loop
        poke(e, kHigh.main, {0x3E, kHigh.i_reg, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFC});
    };
    return s;
}

// NR 0x07 written mid-frame while the CPU sits on DI;HALT: the new speed is a pending
// shadow that commits at the end of the next slot, so the first repeat after the write runs at the old
// one and the rest at the new (zxnext.vhd:5796-5828).
Scenario speed_switch() {
    Scenario s = di_halt("speed-switch", 0, 60);
    auto stops = std::make_shared<int>(0);
    s.setup = [=, base = s.setup](Emulator& e) { base(e); *stops = 0; };
    // The write must land mid-frame (a frame start commits a pending speed itself): stop the frame
    // half-way as nmi_hotkey() does, write, resume.
    s.before_frame = [=](Emulator& e, int f) {
        if (f != 10 && f != 30 && f != 45) return;
        e.debug_state().set_persistent_breakpoints(true);
        e.debug_state().run_to_cycle(e.clock().get() + 400000);
        e.run_frame();
        if (e.debug_state().paused()) ++*stops;
        e.debug_state().resume();
        e.debug_state().set_persistent_breakpoints(false);
        nr(e, 0x07, f == 10 ? 0x03 : f == 30 ? 0x00 : 0x02);
    };
    s.alive = [=](Emulator& e) { return *stops == 3 && e.cpu().is_halted(); };
    return s;
}

// A UART TX byte in flight on EI;HALT with the TX / RX interrupts on and the ULA interrupt off: the
// bit engine completes inside what would be a batch and its interrupt must end the HALT at once
// (uart.vhd).
Scenario uart_tx() {
    Scenario s = im2_halt("uart-tx", 0, 0xFF);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        nr(e, 0x22, 0x04);                       // ULA interrupt off
        nr(e, 0xC6, 0x05);                       // UART 0: RX near-full and TX empty interrupts on
    };
    s.before_frame = [](Emulator& e, int f) {
        if (f == 10 || f == 20 || f == 30) e.uart().write(3, 0x55);
    };
    return s;
}

// A real 128K machine, the shared program in bank 5 (contended while the display is
// drawn), 3.5 MHz: a HALT repeat there costs what the display position says.
Scenario contended_bank5() {
    Scenario s;
    s.name = "contended-bank5";
    s.config = [](EmulatorConfig& c) {
        c.type = MachineType::ZX128K;
        c.sd_card_image = sd_image();
        c.sd_card_readonly = true;
    };
    s.setup = [](Emulator& e) {
        install_im2_program(e, kBank5);
        start_at(e, kBank5.main, 0x8000);
    };
    s.alive = [](Emulator& e) { return isr_ran(e, kBank5); };
    return s;
}

// ── Row helper ───────────────────────────────────────────────────────

Scratch g_scratch;

// A row: every fixture in `list` is identical frame by frame (batching off vs on)
// and alive in both runs.
enum class Halt { Any, Engaged, Refused };

void check_ab(const char* id, const char* desc, const std::vector<Scenario>& list,
              bool copper_engaged = false, std::vector<uint64_t>* halt_out = nullptr,
              Halt expect_halt = Halt::Any) {
    std::string detail;
    bool ok = true;
    for (const Scenario& sc : list) {
        const Run a = batch_ab::run(sc, false, false, g_scratch);
        const Run b = batch_ab::run(sc, true, true, g_scratch);
        if (halt_out) halt_out->push_back(b.halt_batched);
        const std::string d = batch_ab::first_diff(a, b);
        // The "off" run must really be off: a switch ignored on both sides would make every
        // identical row pass while comparing the optimisation with itself.
        if (a.copper_skipped != 0 || a.halt_batched != 0) {
            ok = false;
            detail += std::string(sc.name) + ": the OFF run batched (copper_skipped=" +
                      std::to_string(a.copper_skipped) + " halt_batched=" +
                      std::to_string(a.halt_batched) + "); ";
        }
        if ((expect_halt == Halt::Refused && b.halt_batched != 0) ||
            (expect_halt == Halt::Engaged && b.halt_batched == 0)) {
            ok = false;
            detail += std::string(sc.name) + ": halt_batched=" + std::to_string(b.halt_batched) +
                      (expect_halt == Halt::Refused ? " (a refusal row)" : " (never engaged)") + "; ";
        }
        if (copper_engaged && b.copper_skipped == 0) {
            // A fixture that stops engaging the optimisation proves nothing: named failure.
            ok = false;
            detail += std::string(sc.name) + ": the Copper skip never engaged; ";
        }
        if (!d.empty() || !a.alive || !b.alive) {
            ok = false;
            detail += std::string(sc.name) + ": " +
                      (!d.empty() ? d : std::string("identical")) +
                      (a.alive ? "" : " (unbatched fixture dead)") +
                      (b.alive ? "" : " (batched fixture dead)") + "; ";
        }
    }
    check(id, desc, ok, detail);
}

// A pair of rows from ONE A/B run per fixture: `id` = identical per frame (and alive, the OFF
// run really off); `id2` = the HALT batch ENGAGED in the "on" run of every fixture
// (`expect_engaged`), or REFUSED in every one of them (a refusal row: the counter stays 0).
void check_pair(const char* id, const char* desc, const char* id2, const char* desc2,
                const std::vector<Scenario>& list, bool expect_engaged = true,
                bool copper_engaged = false) {
    std::vector<uint64_t> halt;
    check_ab(id, desc, list, copper_engaged, &halt);
    bool ok = !halt.empty();
    std::string detail;
    for (size_t i = 0; i < halt.size(); ++i) {
        const bool engaged = halt[i] != 0;
        if (engaged != expect_engaged) {
            ok = false;
            detail += std::string(list[i].name) + ": halt_batched=" + std::to_string(halt[i]) + "; ";
        }
    }
    check(id2, desc2, ok, detail);
}

} // namespace

