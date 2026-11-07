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

// CTC channel 0 as a timer, period 944 T (59 x 16): not a multiple of the 228 T line.
// A CTC interrupt reaches the CPU only through the IM 2 fabric in hardware mode
// (NR 0xC0 b0), where the vector comes from the device; the ULA interrupt is masked so
// the CTC is the only source.
Scenario ctc_int() {
    Scenario s = im2_halt("ctc-int", 0, 0xFF);
    auto base = s.setup;
    s.setup = [=](Emulator& e) {
        base(e);
        nr(e, 0xC0, 0x01);
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
void check_ab(const char* id, const char* desc, const std::vector<Scenario>& list) {
    std::string detail;
    bool ok = true;
    for (const Scenario& sc : list) {
        const Run a = batch_ab::run(sc, false, false, g_scratch);
        const Run b = batch_ab::run(sc, true, true, g_scratch);
        const std::string d = batch_ab::first_diff(a, b);
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

    // HBQ-09: CTC channel timer interrupts (ctc_chan.vhd:143-170) at a period that is no
    // multiple of a line.
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

    // HBQ-36: the rig itself. Two unbatched runs of one fixture are bit-identical, and the
    // three hashes cover what they claim: the run produced video, audio samples, and a
    // different save_state in different frames.
    {
        const Scenario sc = im2_halt("rig-control", 0, 0xFF, 60);
        const Run a = batch_ab::run(sc, false, false, g_scratch);
        const Run b = batch_ab::run(sc, false, false, g_scratch);
        bool varies = a.built && a.frames.size() > 2 &&
                      a.frames[1].state != a.frames[2].state &&
                      a.audio_pairs > 0;
        check("HBQ-36", "rig control: two unbatched runs are identical; the run has audio and a state that changes",
              batch_ab::first_diff(a, b).empty() && varies,
              "diff=" + batch_ab::first_diff(a, b) + " audio_pairs=" + std::to_string(a.audio_pairs));
    }

    // HBQ-37: the rig discriminates. The same fixture with its handler no longer toggling
    // the beeper must be reported as different, naming a frame.
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
        check("HBQ-37", "rig control: a fixture whose handler stops toggling the beeper is reported as different",
              d.rfind("frame ", 0) == 0, "diff=" + d);
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
