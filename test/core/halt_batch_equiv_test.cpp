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

// mode: NR 0x62 bits 7:6 started with (1 = start at 0, 3 = restart each frame);
// mode_10_at >= 0 starts mode 1 and writes mode 10 (continue from the PC) at that frame.
Scenario copper_busy(const char* name, uint8_t mode, int mode_10_at = -1) {
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
    if (mode_10_at >= 0)
        s.before_frame = [=](Emulator& e, int f) { if (f == mode_10_at) cu_nr(e, 0x62, 0x80); };
    s.alive = [](Emulator& e) { return e.copper().pc() == kPaletteHaltPc; };
    return s;
}

// NR 0x64 (the Copper line offset) rewritten by the CPU all the time, so it changes across
// every CVC reload (GH #290): LD A,0; loop: NEXTREG 0x64,A; INC A; JR loop.
Scenario copper_offset_stress() {
    Scenario s = copper_busy("copper-offset-stress", 3);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        poke(e, kHigh.main, {0x3E, 0x00, 0xED, 0x92, 0x64, 0x3C, 0x18, 0xFA});
        start_at(e, kHigh.main, 0xBFF0);
    };
    s.alive = [](Emulator& e) { return e.copper().pc() > 0 || e.copper().is_running(); };
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
void check_ab(const char* id, const char* desc, const std::vector<Scenario>& list,
              bool copper_engaged = false) {
    std::string detail;
    bool ok = true;
    for (const Scenario& sc : list) {
        const Run a = batch_ab::run(sc, false, false, g_scratch);
        const Run b = batch_ab::run(sc, true, true, g_scratch);
        const std::string d = batch_ab::first_diff(a, b);
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

} // namespace

int main() {
    std::printf("GH #452 cycle-batching equivalence tests (synthetic fixtures)\n");
    std::printf("===============================================\n\n");

    // HBQ-01: the ULA frame interrupt (zxula_timing.vhd:551-552, pin zxnext.vhd:1840) ends
    // a HALT: a span that runs past it, or past the pulse window, changes the frame.
    check_ab("HBQ-01", "IM 2 pulse mode, EI;HALT, ULA frame interrupt, 3.5 MHz: identical per frame",
             {im2_halt("im2-3.5", 0, 0xFF)});

    // HBQ-03: at 28 MHz an opcode fetch from external SRAM takes one wait state
    // (zxnext.vhd:3171-3181), so a HALT repeat is 5 T there: a fixed 4 T per repeat drifts.
    check_ab("HBQ-03", "same at 28 MHz, HALT in external SRAM (one wait per fetch): identical per frame",
             {im2_halt("im2-28-sram", 3, 0xFF)});

    // HBQ-05: the same HALT in bank 7 (BRAM, no wait): 4 T per repeat. A fixed 5 T drifts.
    check_ab("HBQ-05", "same at 28 MHz, HALT in bank-7 BRAM (no wait): identical per frame",
             {im2_halt("im2-28-bram", 3, 0x0E)});

    // HBQ-07: the line interrupt (zxula_timing.vhd:574-583) lands mid-frame.
    check_ab("HBQ-07", "line interrupt at line 100 (NR 0x22 / 0x23) while halted: identical per frame",
             {line_int()});

    // HBQ-09: CTC channel timer interrupts (ctc_chan.vhd:143-170), pulse mode, at a period
    // that is no multiple of a line.
    check_ab("HBQ-09", "CTC channel 0 timer interrupts, period off the line grid: identical per frame",
             {ctc_int()});

    // HBQ-11: with interrupts disabled the CPU stays halted while the ULA interrupt request
    // window opens and expires (t80n.vhd:1742-1772).
    check_ab("HBQ-11", "DI;HALT with the ULA interrupt window expiring unaccepted, 3.5 and 28 MHz: identical per frame",
             {di_halt("di-halt-3.5", 0), di_halt("di-halt-28", 3)});

    // HBQ-22: a contended page is not a fixed cost (zxula.vhd:579-600, zxnext.vhd:4489-4493).
    check_ab("HBQ-22", "128K, 3.5 MHz, HALT in bank 5 while the display is drawn: identical per frame",
             {contended_bank5()});

    // HBQ-24: IM 2 hardware mode (im2_device.vhd:111-150) takes CTC and ULA through the fabric.
    check_ab("HBQ-24", "IM 2 hardware mode (NR 0xC0 b0) with CTC and ULA through the fabric: identical per frame",
             {im2_hardware()});

    // HBQ-28: an idle DI;HALT across 60 frames reaches every frame's last line and the
    // frame end (run_frame's loop, emulator.cpp).
    check_ab("HBQ-28", "DI;HALT idle across 60 frames, every frame end reached: identical per frame",
             {di_halt("idle-frames", 3, 60)});

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

    // HBQ-34: the Copper walk alone, with a busy CPU: the same palette list started in mode
    // 01 (once), mode 11 (every frame) and mode 10 (continue from the PC, written at frame
    // 5), and a CPU rewriting NR 0x64 across every CVC reload (GH #290). WAIT is met at
    // cvc == vpos && hc >= (hpos << 3) + 12 (copper.vhd:80, :94); the skip must engage.
    check_ab("HBQ-34", "Copper WAIT/MOVE palette splits, modes 01, 10 and 11 and a rewritten NR 0x64, busy CPU: identical per frame, Copper skip engaged",
             {copper_busy("copper-m01", 1), copper_busy("copper-m11", 3),
              copper_busy("copper-m10", 1, 5), copper_offset_stress()}, true);

    // HBQ-35: a WAIT reachable only in the tail of the line that straddles two frames
    // (zxula_timing.vhd:455-472; GH181-HCULA-04 in copper_integration_test).
    check_ab("HBQ-35", "Copper WAIT reachable only in the line that straddles two frames, mode 11, busy CPU: identical per frame, Copper skip engaged",
             {copper_straddle()}, true);

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
