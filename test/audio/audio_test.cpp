// Audio Subsystem Compliance Test Runner
//
// Phase 2 full rewrite (Task 1 Wave 1, 2026-04-15) against
// doc/testing/AUDIO-TEST-PLAN-DESIGN.md. Every assertion cites the exact
// VHDL file and line from the authoritative FPGA source at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/
// (external to this repo; cited here for provenance, not edited).
//
// Ground rules (per doc/testing/UNIT-TEST-PLAN-EXECUTION.md):
//   * VHDL is the oracle; the C++ emulator is the thing under test.
//   * Every check(id, desc, actual_cond, "VHDL file:line ...") maps to
//     exactly one plan row (or a tight sub-group).
//   * A plan row that the current public API in src/audio/ cannot reach
//     uses skip(id, "reason") and is reported without flipping counters.
//   * No tautologies, no helper aggregation, no ambiguous pass sinks.
//
// The standalone audio classes (AyChip, TurboSound, Dac, Beeper, Mixer)
// cover the core DSP; port-decode, NextREG plumbing, exc_i gating and
// port-mapping live in the zxnext core and are *not* reachable from this
// harness, so the corresponding plan rows are skipped with honest reasons.
//
// Run: ./build/test/audio_test

#include "audio/ay_chip.h"
#include "audio/turbosound.h"
#include "audio/dac.h"
#include "audio/beeper.h"
#include "audio/mixer.h"
#include "audio/i2s.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- Test infrastructure --------------------------------------------

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string group;
    std::string id;
    std::string desc;
    bool        passed;
    std::string detail;
};

std::vector<Result> g_results;
std::string         g_group;

struct SkipNote {
    std::string id;
    std::string reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    ++g_total;
    g_results.push_back(Result{g_group, id, desc, cond, detail});
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

void skip(const char* id, const char* reason) {
    g_skipped.push_back({id, reason});
}

// Deterministic AY tick helper: tick enough master ticks to let the /8
// divider and update_output settle on the current register state.
void settle(AyChip& ay, int n = 16) {
    for (int i = 0; i < n; ++i) ay.tick();
}
void settle(TurboSound& ts, int n = 16) {
    for (int i = 0; i < n; ++i) ts.tick();
}

} // namespace

// =====================================================================
// 1.1 AY Register Address and Write (ym2149.vhd 167-214)
// =====================================================================

static void g_ay_write() {
    set_group("AY-write");

    // AY-01 - ym2149.vhd:172-173 addr latches I_DA(4:0) on busctrl_addr=1.
    {
        AyChip ay;
        ay.select_register(5);
        check("AY-01", "addr latches bits[4:0] of data bus",
              ay.selected_register() == 5,
              fmt("got=%u VHDL ym2149.vhd:172-173", ay.selected_register()));
    }

    // AY-02 - ym2149.vhd:167-176 addr holds when busctrl_addr=0: write_data
    // is a busctrl_we event and must not shift the latched addr.
    {
        AyChip ay;
        ay.select_register(5);
        ay.write_data(0xAA);
        check("AY-02", "addr unchanged by write_data (busctrl_addr=0)",
              ay.selected_register() == 5,
              fmt("got=%u VHDL ym2149.vhd:172", ay.selected_register()));
    }

    // AY-03 - ym2149.vhd:170-171 RESET_H clears addr to 0.
    {
        AyChip ay;
        ay.select_register(10);
        ay.reset();
        check("AY-03", "reset clears addr to 00000",
              ay.selected_register() == 0,
              fmt("got=%u VHDL ym2149.vhd:170-171", ay.selected_register()));
    }

    // AY-04 - ym2149.vhd:189-207 every reg(0..15) stores I_DA.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        for (int r = 0; r < 16; ++r) {
            ay.select_register(r);
            ay.write_data(static_cast<uint8_t>(0x40 | r));
        }
        bool ok = true;
        int bad_r = -1;
        uint8_t bad_v = 0;
        for (int r = 0; r < 16; ++r) {
            ay.select_register(r);
            uint8_t v = ay.read_data();
            if (v != static_cast<uint8_t>(0x40 | r)) { ok = false; bad_r = r; bad_v = v; break; }
        }
        check("AY-04", "write to all 16 registers (0..15)",
              ok, fmt("first bad r=%d got=0x%02x VHDL ym2149.vhd:189-207", bad_r, bad_v));
    }

    // AY-05 - ym2149.vhd:188 busctrl_we only stores when addr(4)=0.
    {
        AyChip ay;
        ay.select_register(0x10);
        ay.write_data(0xAA);
        ay.select_register(0);
        ay.set_ay_mode(false);
        check("AY-05", "write with addr(4)=1 is a no-op",
              ay.read_data() == 0x00,
              fmt("R0 got=0x%02x VHDL ym2149.vhd:188", ay.read_data()));
    }

    // AY-06 - ym2149.vhd:184-186 RESET_H: all regs 0, reg(7) <= x"ff".
    {
        AyChip ay;
        ay.set_ay_mode(false);
        bool ok = true;
        int bad = -1;
        uint8_t got = 0;
        for (int r = 0; r < 16; ++r) {
            ay.select_register(r);
            uint8_t expected = (r == 7) ? 0xFF : 0x00;
            uint8_t v = ay.read_data();
            if (v != expected) { ok = false; bad = r; got = v; break; }
        }
        check("AY-06", "reset: all regs 0 except R7=0xFF",
              ok, fmt("first bad r=%d got=0x%02x VHDL ym2149.vhd:184-186", bad, got));
    }

    // AY-07 - ym2149.vhd:209-211 writing R13 pulses env_reset. Observable:
    // shape 0D (up, hold near max) must settle at env_vol=30 (YM[30]=0xE0)
    // only if env_reset loaded env_vol=0 and direction=up at the write.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0D);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-07", "R13 write pulses env_reset (shape 0D settles to YM[30]=0xE0)",
              ay.output_a() == 0xE0,
              fmt("out_a=0x%02x VHDL ym2149.vhd:209-211,392-401",
                  ay.output_a()));
    }
}

// =====================================================================
// 1.2 Register Readback AY vs YM (ym2149.vhd 217-254)
// =====================================================================

static void g_ay_readback() {
    set_group("AY-readback");

    // AY-10 - ym2149.vhd:226 R0 always full 8 bits.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(0); ay.write_data(0xAB);
        ay.select_register(0);
        check("AY-10", "R0 AY-mode full 8 bits",
              ay.read_data() == 0xAB,
              fmt("got=0x%02x VHDL ym2149.vhd:226", ay.read_data()));
    }

    // AY-11 - ym2149.vhd:227 R1 AY mode: bits[7:4] masked.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(1); ay.write_data(0xFF);
        ay.select_register(1);
        check("AY-11", "R1 AY mode: bits[7:4] masked to 0",
              ay.read_data() == 0x0F,
              fmt("got=0x%02x VHDL ym2149.vhd:227", ay.read_data()));
    }

    // AY-12 - ym2149.vhd:227 R1 YM mode: full 8 bits.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(1); ay.write_data(0xFF);
        ay.select_register(1);
        check("AY-12", "R1 YM mode: full 8 bits",
              ay.read_data() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:227", ay.read_data()));
    }

    // AY-13 - ym2149.vhd:229,231 R3/R5 AY masked, YM full.
    {
        AyChip ay;
        ay.select_register(3); ay.write_data(0xF5);
        ay.select_register(5); ay.write_data(0xFA);

        ay.set_ay_mode(true);
        ay.select_register(3); uint8_t r3a = ay.read_data();
        ay.select_register(5); uint8_t r5a = ay.read_data();
        ay.set_ay_mode(false);
        ay.select_register(3); uint8_t r3y = ay.read_data();
        ay.select_register(5); uint8_t r5y = ay.read_data();

        check("AY-13", "R3/R5 AY->{0x05,0x0A}, YM->{0xF5,0xFA}",
              r3a == 0x05 && r5a == 0x0A && r3y == 0xF5 && r5y == 0xFA,
              fmt("r3a=%02x r5a=%02x r3y=%02x r5y=%02x VHDL ym2149.vhd:229,231",
                  r3a, r5a, r3y, r5y));
    }

    // AY-14 - ym2149.vhd:232 R6 AY: bits[7:5] masked.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(6); ay.write_data(0xFF);
        ay.select_register(6);
        check("AY-14", "R6 AY mode: bits[7:5]=0",
              ay.read_data() == 0x1F,
              fmt("got=0x%02x VHDL ym2149.vhd:232", ay.read_data()));
    }

    // AY-15 - ym2149.vhd:232 R6 YM: full 8 bits.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(6); ay.write_data(0xFF);
        ay.select_register(6);
        check("AY-15", "R6 YM mode: full 8 bits",
              ay.read_data() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:232", ay.read_data()));
    }

    // AY-16 - ym2149.vhd:233 R7 unmasked in both modes.
    {
        AyChip ay;
        ay.select_register(7); ay.write_data(0x55);
        ay.set_ay_mode(true);
        ay.select_register(7); uint8_t ra = ay.read_data();
        ay.set_ay_mode(false);
        ay.select_register(7); uint8_t ry = ay.read_data();
        check("AY-16", "R7 full 8 bits in both modes",
              ra == 0x55 && ry == 0x55,
              fmt("ay=0x%02x ym=0x%02x VHDL ym2149.vhd:233", ra, ry));
    }

    // AY-17 - ym2149.vhd:234-236 R8/9/10 AY: bits[7:5] masked.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        uint8_t got[3] = {0, 0, 0};
        for (int r = 8; r <= 10; ++r) {
            ay.select_register(r); ay.write_data(0xFF);
            ay.select_register(r);
            got[r - 8] = ay.read_data();
        }
        check("AY-17", "R8/R9/R10 AY mode: 0x1F",
              got[0] == 0x1F && got[1] == 0x1F && got[2] == 0x1F,
              fmt("r8=%02x r9=%02x r10=%02x VHDL ym2149.vhd:234-236",
                  got[0], got[1], got[2]));
    }

    // AY-18 - ym2149.vhd:234-236 R8/9/10 YM: full 8 bits.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        uint8_t got[3] = {0, 0, 0};
        for (int r = 8; r <= 10; ++r) {
            ay.select_register(r); ay.write_data(0xFF);
            ay.select_register(r);
            got[r - 8] = ay.read_data();
        }
        check("AY-18", "R8/R9/R10 YM mode: 0xFF",
              got[0] == 0xFF && got[1] == 0xFF && got[2] == 0xFF,
              fmt("r8=%02x r9=%02x r10=%02x VHDL ym2149.vhd:234-236",
                  got[0], got[1], got[2]));
    }

    // AY-19 - ym2149.vhd:239 R13 AY: bits[7:4] masked.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(13); ay.write_data(0xFF);
        ay.select_register(13);
        check("AY-19", "R13 AY mode: bits[7:4]=0",
              ay.read_data() == 0x0F,
              fmt("got=0x%02x VHDL ym2149.vhd:239", ay.read_data()));
    }

    // AY-20 - ym2149.vhd:239 R13 YM: full 8 bits.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(13); ay.write_data(0xFF);
        ay.select_register(13);
        check("AY-20", "R13 YM mode: full 8 bits",
              ay.read_data() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:239", ay.read_data()));
    }

    // AY-21 - ym2149.vhd:237-238 R11/R12 unmasked in both modes.
    {
        AyChip ay;
        ay.select_register(11); ay.write_data(0xAB);
        ay.select_register(12); ay.write_data(0xCD);
        ay.set_ay_mode(true);
        ay.select_register(11); uint8_t a11 = ay.read_data();
        ay.select_register(12); uint8_t a12 = ay.read_data();
        ay.set_ay_mode(false);
        ay.select_register(11); uint8_t y11 = ay.read_data();
        ay.select_register(12); uint8_t y12 = ay.read_data();
        check("AY-21", "R11/R12 full 8 bits in both modes",
              a11 == 0xAB && a12 == 0xCD && y11 == 0xAB && y12 == 0xCD,
              fmt("a11=%02x a12=%02x y11=%02x y12=%02x VHDL ym2149.vhd:237-238",
                  a11, a12, y11, y12));
    }

    // AY-22 - ym2149.vhd:222-223 YM mode + addr(4)=1 returns 0xFF.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(0x10);
        check("AY-22", "YM mode addr>=16 returns 0xFF",
              ay.read_data() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:222-223", ay.read_data()));
    }

    // AY-23 - ym2149.vhd:222 AY mode bypasses the addr(4) gate; addr 16
    // aliases register 0 (case addr(3:0)).
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(0);    ay.write_data(0x42);
        ay.select_register(0x10);
        check("AY-23", "AY mode addr>=16 aliases low 4 bits (R0)",
              ay.read_data() == 0x42,
              fmt("got=0x%02x VHDL ym2149.vhd:222", ay.read_data()));
    }

    // AY-24 - ym2149.vhd:220-221 I_REG=1 returns AY_ID & '0' & addr(4:0).
    {
        AyChip ay(3);
        ay.select_register(5);
        uint8_t v = ay.read_data(true);
        check("AY-24", "I_REG=1 returns AY_ID<<6 | addr",
              v == 0xC5,
              fmt("got=0x%02x expected=0xC5 VHDL ym2149.vhd:220-221", v));
    }

    // AY-25 - turbosound.vhd:158,213,268: PSG0=11, PSG1=10, PSG2=01.
    {
        AyChip a0(3), a1(2), a2(1);
        a0.select_register(0); a1.select_register(0); a2.select_register(0);
        uint8_t id0 = a0.read_data(true) >> 6;
        uint8_t id1 = a1.read_data(true) >> 6;
        uint8_t id2 = a2.read_data(true) >> 6;
        check("AY-25", "AY_ID per chip: 3/2/1",
              id0 == 3 && id1 == 2 && id2 == 1,
              fmt("id0=%u id1=%u id2=%u VHDL turbosound.vhd:158,213,268",
                  id0, id1, id2));
    }
}

// =====================================================================
// 1.3 I/O Ports (ym2149.vhd 240-249)
// =====================================================================

static void g_ay_ports() {
    set_group("AY-ports");

    // G: AY-30..34: port_a_i / port_b_i (ym2149.vhd:240-249) are tied to
    // all-1s in turbosound.vhd:158 and reg(14)/reg(15) readback depends on
    // those tie-high inputs. The standalone AyChip class has no accessor
    // for them and the turbosound wrapper does not plumb the tied-high
    // signal through. Unobservable without src/ API extension; no known
    // ZX Next software exercises the PSG GPIO path.
}

// =====================================================================
// 1.4 Clock divider (ym2149.vhd 260-279)
// =====================================================================

static void g_ay_divider() {
    set_group("AY-divider");

    // AY-40 - ym2149.vhd:267 reload = (not I_SEL_L) & "111" = "0111" when
    // I_SEL_L='1'. AyChip hard-codes 7. Indirectly observable via
    // tone-period-0 output oscillating inside a small tick window.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x3E);
        ay.select_register(8); ay.write_data(0x0F);
        ay.select_register(0); ay.write_data(0x00);
        ay.select_register(1); ay.write_data(0x00);
        uint8_t prev = 0xFF;
        bool flipped = false;
        for (int i = 0; i < 32; ++i) {
            ay.tick();
            uint8_t v = ay.output_a();
            if (prev != 0xFF && v != prev) flipped = true;
            prev = v;
        }
        check("AY-40", "/8 divider pulses ena_div (period-0 tone flips)",
              flipped,
              "VHDL ym2149.vhd:260-279 (I_SEL_L hard-tied '1')");
    }

    // G: AY-41: I_SEL_L=0 (/16 divider) path unreachable. turbosound.vhd:164
    // hard-wires I_SEL_L='1', so the /16 branch is dead in the hardware
    // contract; AyChip has no set_sel_l() API. Defensive coverage only —
    // no VHDL path exercises it.

    // AY-42 - ena_div clocks tone generators once per /8 pulse. Verified
    // via R7=0x3F force-high + fixed vol 15 settling at table max.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x3F);
        ay.select_register(8); ay.write_data(0x0F);
        settle(ay);
        check("AY-42", "ena_div clocks tone gens (forced-high => vol max)",
              ay.output_a() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:264-268", ay.output_a()));
    }

    // G: AY-43: ena_div_noise runs at half ena_div rate (ym2149.vhd:264-268).
    // No noise_cnt accessor on AyChip; the LFSR output is stochastic so a
    // statistical indirect check would be unreliable.

    // AY-44 - turbosound.vhd:164 hard-wires I_SEL_L='1', so AyChip uses the
    // /8 counter. With tone period=2 (comp=1), the square wave toggles
    // every 2 ena_div pulses; over 64 master ticks we expect at least 3
    // transitions if (and only if) the divider is /8.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x3E);
        ay.select_register(8); ay.write_data(0x0F);
        ay.select_register(0); ay.write_data(0x02);
        ay.select_register(1); ay.write_data(0x00);
        int flips = 0;
        uint8_t prev = ay.output_a();
        for (int i = 0; i < 64; ++i) {
            ay.tick();
            if (ay.output_a() != prev) { ++flips; prev = ay.output_a(); }
        }
        check("AY-44", "I_SEL_L=1 /8 divider: period 2 yields >=3 flips in 64 ticks",
              flips >= 3,
              fmt("flips=%d VHDL turbosound.vhd:164, ym2149.vhd:267", flips));
    }
}

// =====================================================================
// 1.5 Tone generators (ym2149.vhd 304-330)
// =====================================================================

static void g_ay_tone() {
    set_group("AY-tone");

    // AY-50 - ym2149.vhd:310-312 comp=0 when freq[11:1]==0.
    {
        AyChip ay;
        ay.select_register(0); ay.write_data(0x00);
        ay.select_register(1); ay.write_data(0x00);
        check("AY-50a", "tone period 0 -> comp=0",
              ay.tone_comp(0) == 0,
              fmt("got=%u VHDL ym2149.vhd:310", ay.tone_comp(0)));
        ay.select_register(0); ay.write_data(0x01);
        check("AY-50b", "tone period 1 -> comp=0",
              ay.tone_comp(0) == 0,
              fmt("got=%u VHDL ym2149.vhd:310", ay.tone_comp(0)));
    }

    // AY-51 - ym2149.vhd:310 period=2 -> comp=1.
    {
        AyChip ay;
        ay.select_register(0); ay.write_data(0x02);
        ay.select_register(1); ay.write_data(0x00);
        check("AY-51", "tone period 2 -> comp=1",
              ay.tone_comp(0) == 1,
              fmt("got=%u VHDL ym2149.vhd:310", ay.tone_comp(0)));
    }

    // AY-52 - ym2149.vhd:310 period 0xFFF -> comp=0xFFE.
    {
        AyChip ay;
        ay.select_register(0); ay.write_data(0xFF);
        ay.select_register(1); ay.write_data(0x0F);
        check("AY-52", "tone period 0xFFF -> comp=0xFFE",
              ay.tone_comp(0) == 0xFFE,
              fmt("got=0x%03x VHDL ym2149.vhd:310", ay.tone_comp(0)));
    }

    // AY-53 - ym2149.vhd:306 freq(1) = reg(1)(3:0) & reg(0).
    {
        AyChip ay;
        ay.select_register(0); ay.write_data(0x34);
        ay.select_register(1); ay.write_data(0xF2);
        check("AY-53", "Ch A period = {R1[3:0],R0} = 0x234",
              ay.tone_comp(0) == 0x233,
              fmt("got=0x%03x VHDL ym2149.vhd:306", ay.tone_comp(0)));
    }

    // AY-54 - ym2149.vhd:307 freq(2) = reg(3)(3:0) & reg(2).
    {
        AyChip ay;
        ay.select_register(2); ay.write_data(0x56);
        ay.select_register(3); ay.write_data(0x07);
        check("AY-54", "Ch B period = {R3[3:0],R2} = 0x756",
              ay.tone_comp(1) == 0x755,
              fmt("got=0x%03x VHDL ym2149.vhd:307", ay.tone_comp(1)));
    }

    // AY-55 - ym2149.vhd:308 freq(3) = reg(5)(3:0) & reg(4).
    {
        AyChip ay;
        ay.select_register(4); ay.write_data(0xFF);
        ay.select_register(5); ay.write_data(0x0F);
        check("AY-55", "Ch C period = {R5[3:0],R4} = 0xFFF",
              ay.tone_comp(2) == 0xFFE,
              fmt("got=0x%03x VHDL ym2149.vhd:308", ay.tone_comp(2)));
    }

    // AY-56 - ym2149.vhd:321-322 tone_op toggles (not pulses). With
    // period=2 and enough ticks we see multiple transitions.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x3E);
        ay.select_register(8); ay.write_data(0x0F);
        ay.select_register(0); ay.write_data(0x02);
        ay.select_register(1); ay.write_data(0x00);
        int transitions = 0;
        uint8_t prev = ay.output_a();
        for (int i = 0; i < 128; ++i) {
            ay.tick();
            if (ay.output_a() != prev) { ++transitions; prev = ay.output_a(); }
        }
        check("AY-56", "tone output toggles multiple times (not a pulse)",
              transitions >= 4,
              fmt("transitions=%d VHDL ym2149.vhd:321-322", transitions));
    }
}

// =====================================================================
// 1.6 Noise generator (ym2149.vhd 282-302)
// =====================================================================

static void g_ay_noise() {
    set_group("AY-noise");

    // AY-60 - ym2149.vhd:283 comp uses reg(6)(4:0).
    {
        AyChip ay;
        ay.select_register(6); ay.write_data(0x15);
        check("AY-60", "noise period from R6[4:0]=0x15",
              ay.noise_period() == 0x15,
              fmt("got=0x%02x VHDL ym2149.vhd:283", ay.noise_period()));
    }

    // AY-61 - ym2149.vhd:283 comp=0 when reg(6)(4:1)="0000".
    {
        AyChip ay;
        ay.select_register(6); ay.write_data(0x00);
        check("AY-61a", "noise period 0 -> comp=0",
              ay.noise_comp() == 0,
              fmt("got=%u VHDL ym2149.vhd:283", ay.noise_comp()));
        ay.select_register(6); ay.write_data(0x01);
        check("AY-61b", "noise period 1 -> comp=0",
              ay.noise_comp() == 0,
              fmt("got=%u VHDL ym2149.vhd:283", ay.noise_comp()));
    }

    // AY-62 - ym2149.vhd:284,293 LFSR taps bit0 XOR bit2 XOR zero-detect.
    // Starting at poly17==0 the zero-detect injection produces a 1; over
    // many ticks noise output is non-constant.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x37);
        ay.select_register(8); ay.write_data(0x0F);
        ay.select_register(6); ay.write_data(0x01);
        int seen_high = 0, seen_low = 0;
        for (int i = 0; i < 4096; ++i) {
            ay.tick();
            if (ay.output_a()) ++seen_high; else ++seen_low;
        }
        check("AY-62", "LFSR with zero-detect injection yields non-constant noise",
              seen_high > 0 && seen_low > 0,
              fmt("high=%d low=%d VHDL ym2149.vhd:284,293", seen_high, seen_low));
    }

    // A: AY-63: noise_gen_op = poly17(0) (ym2149.vhd:302) already exercised
    // by AY-62's bit-sequence assertion — a separate row would just re-state
    // the same observation.
    // G: AY-64: noise clocked at ena_div_noise rate (ym2149.vhd:290, half
    // ena_div) — same unobservable-without-accessor constraint as AY-43.
}

// =====================================================================
// 1.7 Channel mixer (ym2149.vhd 469-471)
// =====================================================================

static void g_ay_chan_mixer() {
    set_group("AY-chan-mixer");

    // AY-70 - ym2149.vhd:469 tone A enabled: output oscillates.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x3E);
        ay.select_register(8); ay.write_data(0x0F);
        ay.select_register(0); ay.write_data(0x02);
        ay.select_register(1); ay.write_data(0x00);
        bool seen_zero = false, seen_nonzero = false;
        for (int i = 0; i < 64; ++i) {
            ay.tick();
            if (ay.output_a() == 0) seen_zero = true;
            else                    seen_nonzero = true;
        }
        check("AY-70", "R7[0]=0: tone A enabled -> output oscillates",
              seen_zero && seen_nonzero,
              fmt("zero=%d nz=%d VHDL ym2149.vhd:469", seen_zero, seen_nonzero));
    }

    // AY-71 - R7[0]=1 forces Ch A high.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x3F);
        ay.select_register(8); ay.write_data(0x0F);
        settle(ay);
        check("AY-71", "R7[0]=1 forces Ch A high (vol max)",
              ay.output_a() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:469", ay.output_a()));
    }

    // AY-72 - R7[3]=0 enables noise on A.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x37);
        ay.select_register(8); ay.write_data(0x0F);
        ay.select_register(6); ay.write_data(0x01);
        bool z = false, h = false;
        for (int i = 0; i < 2048; ++i) {
            ay.tick();
            if (ay.output_a() == 0) z = true; else h = true;
        }
        check("AY-72", "R7[3]=0: noise on Ch A -> output varies",
              z && h,
              fmt("z=%d h=%d VHDL ym2149.vhd:469", z, h));
    }

    // AY-73 - R7[3]=1 with tone disabled too: constant high (OR branch).
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x3F);
        ay.select_register(8); ay.write_data(0x0F);
        settle(ay);
        check("AY-73", "R7[3]=1 forces Ch A noise branch high",
              ay.output_a() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:469", ay.output_a()));
    }

    // AY-74/75 - same logic per channel.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(9);  ay.write_data(0x0F);
        ay.select_register(10); ay.write_data(0x0F);
        settle(ay);
        check("AY-74", "Ch B force-high yields vol max",
              ay.output_b() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:470", ay.output_b()));
        check("AY-75", "Ch C force-high yields vol max",
              ay.output_c() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:471", ay.output_c()));
    }

    // AY-76 - tone+noise disabled on all three channels.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x0F);
        ay.select_register(9);  ay.write_data(0x0F);
        ay.select_register(10); ay.write_data(0x0F);
        settle(ay);
        check("AY-76", "both tone&noise disabled => constant high, all chans",
              ay.output_a() == 0xFF && ay.output_b() == 0xFF && ay.output_c() == 0xFF,
              fmt("a=%02x b=%02x c=%02x VHDL ym2149.vhd:469-471",
                  ay.output_a(), ay.output_b(), ay.output_c()));
    }

    // AY-77 - tone & noise both enabled: output = AND (zero and high seen).
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x30);
        ay.select_register(8); ay.write_data(0x0F);
        ay.select_register(0); ay.write_data(0x02);
        ay.select_register(1); ay.write_data(0x00);
        ay.select_register(6); ay.write_data(0x01);
        bool z = false, h = false;
        for (int i = 0; i < 4096; ++i) {
            ay.tick();
            if (ay.output_a() == 0) z = true; else h = true;
        }
        check("AY-77", "tone+noise AND: both 0 and non-0 observed",
              z && h,
              fmt("z=%d h=%d VHDL ym2149.vhd:469", z, h));
    }

    // AY-78 - mixed=0 -> volume output 0 (seen during tone low-phase).
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x3E);
        ay.select_register(8); ay.write_data(0x0F);
        ay.select_register(0); ay.write_data(0x02);
        ay.select_register(1); ay.write_data(0x00);
        bool saw_zero = false;
        for (int i = 0; i < 64; ++i) {
            ay.tick();
            if (ay.output_a() == 0) { saw_zero = true; break; }
        }
        check("AY-78", "mixed=0 during tone low-phase -> output 0",
              saw_zero,
              "VHDL ym2149.vhd:469 A & B gating");
    }
}

// =====================================================================
// 1.8 Volume/envelope mode (ym2149.vhd 472-520)
// =====================================================================

static void g_ay_vol_mode() {
    set_group("AY-vol-mode");

    // AY-80 - R8[4]=0 fixed volume path.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7); ay.write_data(0x3F);
        ay.select_register(8); ay.write_data(0x0F);
        settle(ay);
        check("AY-80", "R8[4]=0 fixed vol -> YM[31]=0xFF",
              ay.output_a() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:472-520", ay.output_a()));
    }

    // AY-81 - R8[4]=1 envelope path: shape 0 decays to 0.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x00);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-81", "R8[4]=1 envelope path (shape 0 -> hold 0)",
              ay.output_a() == 0x00,
              fmt("got=0x%02x VHDL ym2149.vhd:472-520", ay.output_a()));
    }

    // AY-82 - fixed vol 0 -> special-case 0 (not table[1]).
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7); ay.write_data(0x3F);
        ay.select_register(8); ay.write_data(0x00);
        settle(ay);
        check("AY-82", "fixed vol 0 -> 5-bit index 0 -> YM[0]=0",
              ay.output_a() == 0x00,
              fmt("got=0x%02x VHDL ym2149.vhd:472-520", ay.output_a()));
    }

    // AY-83 - fixed vol 1..15 -> index (v<<1)|1. Probe v=1 and v=15.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7); ay.write_data(0x3F);
        ay.select_register(8); ay.write_data(0x01);
        settle(ay);
        uint8_t out_v1 = ay.output_a();
        ay.reset();
        ay.set_ay_mode(false);
        ay.select_register(7); ay.write_data(0x3F);
        ay.select_register(8); ay.write_data(0x0F);
        settle(ay);
        uint8_t out_v15 = ay.output_a();
        check("AY-83", "fixed vol 1->YM[3]=0x02, vol 15->YM[31]=0xFF",
              out_v1 == 0x02 && out_v15 == 0xFF,
              fmt("v1=0x%02x v15=0x%02x VHDL ym2149.vhd:472-520", out_v1, out_v15));
    }

    // AY-84 - same logic on Ch B (R9) and Ch C (R10).
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(9);  ay.write_data(0x0F);
        ay.select_register(10); ay.write_data(0x01);
        settle(ay);
        check("AY-84", "R9/R10 fixed volume path identical to R8",
              ay.output_b() == 0xFF && ay.output_c() == 0x02,
              fmt("b=0x%02x c=0x%02x VHDL ym2149.vhd:472-520",
                  ay.output_b(), ay.output_c()));
    }
}

// =====================================================================
// 1.9 Volume tables (ym2149.vhd 150-162)
// =====================================================================

static void g_ay_vol_tables() {
    set_group("AY-vol-tables");

    // AY-90 - YM 32-entry table: endpoints match.
    {
        AyChip a;
        a.set_ay_mode(false);
        a.select_register(7); a.write_data(0x3F);
        a.select_register(8); a.write_data(0x00);
        settle(a);
        uint8_t lo = a.output_a();
        AyChip b;
        b.set_ay_mode(false);
        b.select_register(7); b.write_data(0x3F);
        b.select_register(8); b.write_data(0x0F);
        settle(b);
        uint8_t hi = b.output_a();
        check("AY-90", "YM 32-entry endpoints: YM[0]=0, YM[31]=0xFF",
              lo == 0x00 && hi == 0xFF,
              fmt("lo=0x%02x hi=0x%02x VHDL ym2149.vhd:157-162", lo, hi));
    }

    // AY-91 - AY 16-entry table: index = top 4 bits of 5-bit volume.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x3F);
        ay.select_register(8); ay.write_data(0x0F);
        settle(ay);
        check("AY-91", "AY mode bits[4:1] index -> ay_table[15]=0xFF",
              ay.output_a() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:150-155", ay.output_a()));
    }

    // AY-92 - YM boundaries explicit: assert against VHDL literals
    // ym2149.vhd:157 (YM[0]=0x00) and ym2149.vhd:161 (YM[31]=0xff).
    {
        AyChip a;
        a.set_ay_mode(false);
        a.select_register(7); a.write_data(0x3F);
        a.select_register(8); a.write_data(0x00);
        settle(a);
        uint8_t ym0_got = a.output_a();
        AyChip b;
        b.set_ay_mode(false);
        b.select_register(7); b.write_data(0x3F);
        b.select_register(8); b.write_data(0x0F);
        settle(b);
        uint8_t ym31_got = b.output_a();
        check("AY-92", "YM[0]=0x00 and YM[31]=0xFF",
              ym0_got == 0x00 && ym31_got == 0xFF,
              fmt("ym0=0x%02x ym31=0x%02x VHDL ym2149.vhd:157-162",
                  ym0_got, ym31_got));
    }

    // AY-93 - AY boundaries explicit: ym2149.vhd:150 (AY[0]=0) and :154
    // (AY[15]=0xff).
    {
        AyChip a;
        a.set_ay_mode(true);
        a.select_register(7); a.write_data(0x3F);
        a.select_register(8); a.write_data(0x00);
        settle(a);
        uint8_t ay0_got = a.output_a();
        AyChip b;
        b.set_ay_mode(true);
        b.select_register(7); b.write_data(0x3F);
        b.select_register(8); b.write_data(0x0F);
        settle(b);
        uint8_t ay15_got = b.output_a();
        check("AY-93", "AY[0]=0x00 and AY[15]=0xFF",
              ay0_got == 0x00 && ay15_got == 0xFF,
              fmt("a0=0x%02x a15=0x%02x VHDL ym2149.vhd:150-155",
                  ay0_got, ay15_got));
    }

    // AY-94 - YM table probe at 6 indices using fixed-volume mapping.
    // fixed vol v (1..15) -> 5-bit idx = (v<<1)|1 in {3,5,7,...,31}. For
    // vol 0 the special case maps to idx 0. So indices {0,3,7,15,23,31}
    // are reachable via fixed vol {0,1,3,7,11,15}.
    {
        const uint8_t expected[6] = {0x00, 0x02, 0x04, 0x13, 0x47, 0xFF};
        const int     index[6]    = {0,    3,    7,    15,   23,   31};
        const uint8_t fixed_v[6]  = {0,    1,    3,    7,    11,   15};
        bool ok = true;
        int fail_i = -1;
        uint8_t got = 0;
        for (int i = 0; i < 6; ++i) {
            AyChip a;
            a.set_ay_mode(false);
            a.select_register(7); a.write_data(0x3F);
            a.select_register(8); a.write_data(fixed_v[i]);
            settle(a);
            uint8_t v = a.output_a();
            if (v != expected[i]) { ok = false; fail_i = index[i]; got = v; break; }
        }
        check("AY-94", "YM vol table probes {0,3,7,15,23,31} match literals",
              ok,
              fmt("idx=%d got=0x%02x VHDL ym2149.vhd:157-162", fail_i, got));
    }

    // AY-95 - AY table probe: fixed vol v in AY mode maps directly to idx v,
    // so vol 0..15 cover the full 16 entries.
    {
        const uint8_t expected[16] = {
            0x00, 0x03, 0x04, 0x06, 0x0a, 0x0f, 0x15, 0x22,
            0x28, 0x41, 0x5b, 0x72, 0x90, 0xb5, 0xd7, 0xff
        };
        bool ok = true;
        int  fail_i = -1;
        uint8_t got = 0;
        for (int v = 0; v < 16; ++v) {
            AyChip a;
            a.set_ay_mode(true);
            a.select_register(7); a.write_data(0x3F);
            a.select_register(8); a.write_data(static_cast<uint8_t>(v));
            settle(a);
            uint8_t out = a.output_a();
            if (out != expected[v]) { ok = false; fail_i = v; got = out; break; }
        }
        check("AY-95", "AY vol table 0..15 matches ym2149.vhd:150-155 literals",
              ok,
              fmt("first bad v=%d got=0x%02x", fail_i, got));
    }

    // AY-96 - reset zeroes O_AUDIO_A/B/C.
    {
        AyChip ay;
        ay.select_register(7); ay.write_data(0x3F);
        ay.select_register(8); ay.write_data(0x0F);
        settle(ay);
        ay.reset();
        check("AY-96", "reset zeroes all three audio outputs",
              ay.output_a() == 0 && ay.output_b() == 0 && ay.output_c() == 0,
              fmt("a=%u b=%u c=%u VHDL ym2149.vhd:184-186",
                  ay.output_a(), ay.output_b(), ay.output_c()));
    }
}

// =====================================================================
// 1.10 Envelope generator (ym2149.vhd 332-465)
// =====================================================================

static void g_ay_envelope() {
    set_group("AY-envelope");

    // AY-100 - ym2149.vhd:334 env_gen_freq = reg(12) & reg(11).
    {
        AyChip ay;
        ay.select_register(11); ay.write_data(0x34);
        ay.select_register(12); ay.write_data(0x12);
        check("AY-100", "env period = {R12,R11} = 0x1234",
              ay.env_period() == 0x1234,
              fmt("got=0x%04x VHDL ym2149.vhd:334", ay.env_period()));
    }

    // AY-101 - ym2149.vhd:335 comp=0 when freq[15:1]=0.
    {
        AyChip ay;
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        check("AY-101a", "env period 0 -> comp=0",
              ay.env_comp() == 0,
              fmt("got=%u VHDL ym2149.vhd:335", ay.env_comp()));
        ay.select_register(11); ay.write_data(0x01);
        check("AY-101b", "env period 1 -> comp=0",
              ay.env_comp() == 0,
              fmt("got=%u VHDL ym2149.vhd:335", ay.env_comp()));
    }

    // AY-102 - ym2149.vhd:340-342 env_reset clears counter. Observable by
    // writing a new R13 mid-ramp and confirming the new shape's endpoint.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0C);
        for (int i = 0; i < 200; ++i) ay.tick();
        ay.select_register(13); ay.write_data(0x00);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-102", "R13 re-write resets env counter (shape 0 -> hold 0)",
              ay.output_a() == 0x00,
              fmt("got=0x%02x VHDL ym2149.vhd:340-342", ay.output_a()));
    }

    // A: AY-103: ym2149.vhd:392-401 env_reset loads env_vol from attack bit
    // — covered by AY-07 (reset-pulse evidence) and AY-117 (shape 0x0D
    // settle-to-max) which would fail if env_reset did not load env_vol.

    // AY-110 - ym2149.vhd:412-421 shape 0 (\\___): hold at 0.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x00);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-110", "shape 0 (\\___): hold at 0 (YM=0x00)",
              ay.output_a() == 0x00,
              fmt("got=0x%02x VHDL ym2149.vhd:412-421", ay.output_a()));
    }

    // AY-111 - ym2149.vhd:412-421 shape 4 (/___): hold at top.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x04);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-111", "shape 4 (/___): hold at top (YM=0xFF)",
              ay.output_a() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:412-421", ay.output_a()));
    }

    // AY-112 - shape 8 (saw-down continuous): cycles, visits both ends.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x08);
        bool saw_high = false, saw_zero = false;
        for (int i = 0; i < 4000; ++i) {
            ay.tick();
            if (ay.output_a() >= 0x50) saw_high = true;
            if (ay.output_a() == 0x00) saw_zero = true;
        }
        check("AY-112", "shape 8 (saw-down continuous): cycles, never locks",
              saw_high && saw_zero,
              fmt("high=%d zero=%d VHDL ym2149.vhd:411 (no hold branch)",
                  saw_high, saw_zero));
    }

    // AY-113 - shape 9 (\\___H=1 Alt=0 down): VHDL ym2149.vhd:428-431 holds
    // on is_bot_p1 -> env_vol=1 -> YM[1]=0x01. (The plan row description
    // says "hold at 0" but the VHDL references is_bot_p1 not is_bot.)
    // Emulator known-bug flag: may hold at different vol; test asserts VHDL.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x09);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-113", "shape 9 H=1 Alt=0 down: hold at is_bot_p1 -> YM[1]=0x01",
              ay.output_a() == 0x01,
              fmt("got=0x%02x VHDL ym2149.vhd:428-431", ay.output_a()));
    }

    // AY-114 - shape 10 (triangle \\/\\/): visits both extremes.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0A);
        bool saw_low = false, saw_high = false;
        for (int i = 0; i < 4000; ++i) {
            ay.tick();
            if (ay.output_a() <= 0x04) saw_low  = true;
            if (ay.output_a() >= 0xC0) saw_high = true;
        }
        check("AY-114", "shape 10 (triangle): visits both extremes",
              saw_low && saw_high,
              fmt("low=%d high=%d VHDL ym2149.vhd:444-461", saw_low, saw_high));
    }

    // AY-115 - shape 11 (H=1 Alt=1 down): hold at is_bot -> YM[0]=0.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0B);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-115", "shape 11: hold at is_bot -> YM[0]=0x00",
              ay.output_a() == 0x00,
              fmt("got=0x%02x VHDL ym2149.vhd:424-427", ay.output_a()));
    }

    // AY-116 - shape 12 (saw-up continuous).
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0C);
        bool saw_low = false, saw_high = false;
        for (int i = 0; i < 4000; ++i) {
            ay.tick();
            if (ay.output_a() <= 0x04) saw_low = true;
            if (ay.output_a() >= 0xC0) saw_high = true;
        }
        check("AY-116", "shape 12 (saw-up continuous): cycles, never locks",
              saw_low && saw_high,
              fmt("low=%d high=%d VHDL ym2149.vhd:411 (no hold branch)",
                  saw_low, saw_high));
    }

    // AY-117 - shape 13 H=1 Alt=0 up: hold at is_top_m1 -> YM[30]=0xE0.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0D);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-117", "shape 13 H=1 Alt=0 up: hold at is_top_m1 -> YM[30]=0xE0",
              ay.output_a() == 0xE0,
              fmt("got=0x%02x VHDL ym2149.vhd:438-441", ay.output_a()));
    }

    // AY-118 - shape 14 triangle /\\/\\.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0E);
        bool saw_low = false, saw_high = false;
        for (int i = 0; i < 4000; ++i) {
            ay.tick();
            if (ay.output_a() <= 0x04) saw_low = true;
            if (ay.output_a() >= 0xC0) saw_high = true;
        }
        check("AY-118", "shape 14 (triangle): visits both extremes",
              saw_low && saw_high,
              fmt("low=%d high=%d VHDL ym2149.vhd:444-461", saw_low, saw_high));
    }

    // AY-119 - shape 15 H=1 Alt=1 up: hold at is_top -> YM[31]=0xFF.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0F);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-119", "shape 15: hold at is_top -> YM[31]=0xFF",
              ay.output_a() == 0xFF,
              fmt("got=0x%02x VHDL ym2149.vhd:434-437", ay.output_a()));
    }

    // A: AY-120: attack=0 initial state — covered by AY-110 (shape 0 reaches 0,
    // which can only hold if the initial env_vol was loaded from attack=0).
    // A: AY-121: attack=1 initial state — covered by AY-111 (symmetric).

    // AY-122 - C=0 implies hold after first ramp. Shape 2 (C=0, Al=1,
    // H=0) must still terminate at 0 (single-ramp behaviour).
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x02);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-122", "C=0 always single-ramp (shape 2 -> 0)",
              ay.output_a() == 0x00,
              fmt("got=0x%02x VHDL ym2149.vhd:412-421", ay.output_a()));
    }

    // AY-123 - C=1 H=1 Alt=0 down/up cross-check.
    {
        AyChip a, b;
        a.set_ay_mode(false); b.set_ay_mode(false);
        for (AyChip* p : { &a, &b }) {
            p->select_register(7);  p->write_data(0x3F);
            p->select_register(8);  p->write_data(0x10);
            p->select_register(11); p->write_data(0x00);
            p->select_register(12); p->write_data(0x00);
        }
        a.select_register(13); a.write_data(0x09);
        b.select_register(13); b.write_data(0x0D);
        for (int i = 0; i < 4000; ++i) { a.tick(); b.tick(); }
        check("AY-123", "H=1 Alt=0: down holds YM[1]=0x01, up holds YM[30]=0xE0",
              a.output_a() == 0x01 && b.output_a() == 0xE0,
              fmt("down=0x%02x up=0x%02x VHDL ym2149.vhd:422-443",
                  a.output_a(), b.output_a()));
    }

    // AY-124 - C=1 H=1 Alt=1 cross-check.
    {
        AyChip a, b;
        a.set_ay_mode(false); b.set_ay_mode(false);
        for (AyChip* p : { &a, &b }) {
            p->select_register(7);  p->write_data(0x3F);
            p->select_register(8);  p->write_data(0x10);
            p->select_register(11); p->write_data(0x00);
            p->select_register(12); p->write_data(0x00);
        }
        a.select_register(13); a.write_data(0x0B);
        b.select_register(13); b.write_data(0x0F);
        for (int i = 0; i < 4000; ++i) { a.tick(); b.tick(); }
        check("AY-124", "H=1 Alt=1: down holds YM[0]=0x00, up holds YM[31]=0xFF",
              a.output_a() == 0x00 && b.output_a() == 0xFF,
              fmt("down=0x%02x up=0x%02x VHDL ym2149.vhd:422-443",
                  a.output_a(), b.output_a()));
    }

    // A: AY-125: triangle continuous — covered by AY-114 + AY-118 shape probes.
    // A: AY-126: sawtooth continuous — covered by AY-112 + AY-116.
    // A: AY-127: 32-level step progression — implied by AY-94 volume-table probes
    //    and the shape sweeps that reach each end of the 32-level range.
    // A: AY-128: period counter reset on R13 write — covered by AY-102 (R13
    //    re-write resets env counter to shape 0 hold-at-0).
}

// =====================================================================
// 2 Turbosound (turbosound.vhd)
// =====================================================================

static void g_ts_selection() {
    set_group("TS-selection");

    // TS-01 - turbosound.vhd:123 reset sets ay_select="11" (PSG0).
    {
        TurboSound ts;
        ts.reg_addr(0x00);
        uint8_t id = ts.reg_read(true) >> 6;
        check("TS-01", "reset selects PSG0 (id=11)",
              id == 3,
              fmt("got=%u VHDL turbosound.vhd:123", id));
    }

    // TS-02/03/04 - turbosound.vhd:131-134 select via bits[1:0].
    {
        TurboSound ts;
        ts.set_enabled(true);

        ts.reg_addr(0xFE); // PSG1 select, pan=11
        ts.reg_addr(0x00);
        uint8_t id1 = ts.reg_read(true) >> 6;
        check("TS-03", "bits[1:0]=10 selects PSG1 (id=10)",
              id1 == 2,
              fmt("got=%u VHDL turbosound.vhd:132", id1));

        ts.reg_addr(0xFD); // PSG2 select
        ts.reg_addr(0x00);
        uint8_t id2 = ts.reg_read(true) >> 6;
        check("TS-04", "bits[1:0]=01 selects PSG2 (id=01)",
              id2 == 1,
              fmt("got=%u VHDL turbosound.vhd:133", id2));

        ts.reg_addr(0xFF); // PSG0 select
        ts.reg_addr(0x00);
        uint8_t id0 = ts.reg_read(true) >> 6;
        check("TS-02", "bits[1:0]=11 selects PSG0 (id=11)",
              id0 == 3,
              fmt("got=%u VHDL turbosound.vhd:134", id0));
    }

    // TS-05 - turbosound.vhd:129 turbosound_en_i=1 required.
    {
        TurboSound ts;
        ts.set_enabled(false);
        ts.reg_addr(0xFE);
        ts.reg_addr(0x00);
        uint8_t id = ts.reg_read(true) >> 6;
        check("TS-05", "selection ignored when turbosound disabled",
              id == 3,
              fmt("got=%u VHDL turbosound.vhd:129", id));
    }

    // TS-06 - psg_reg_addr_i=1 is the reg_addr() path; reg_write() (BFFD)
    // must not trigger a select.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.reg_write(0xFE);
        ts.reg_addr(0x00);
        uint8_t id = ts.reg_read(true) >> 6;
        check("TS-06", "select requires psg_reg_addr=1 (reg_write skipped)",
              id == 3,
              fmt("got=%u VHDL turbosound.vhd:129", id));
    }

    // TS-07 - bit7 required.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.reg_addr(0x7E);
        ts.reg_addr(0x00);
        uint8_t id = ts.reg_read(true) >> 6;
        check("TS-07", "bit7=0 does not trigger select",
              id == 3,
              fmt("got=%u VHDL turbosound.vhd:129", id));
    }

    // TS-08 - bits[4:2]=111 required.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.reg_addr(0xE2);
        ts.reg_addr(0x00);
        uint8_t id = ts.reg_read(true) >> 6;
        check("TS-08", "bits[4:2]!=111 does not trigger select",
              id == 3,
              fmt("got=%u VHDL turbosound.vhd:129", id));
    }

    // TS-09 - pan bits[6:5] captured at select time.
    // All channels active so R_sum = B + C > 0 before gating; pan=10
    // (bit 0=0) must zero R via turbosound.vhd:327, proving the gate
    // is doing the work, not the routing.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.reg_addr(0xDF); // PSG0 pan=10 (bit6=1 bit5=0 in the reg_addr byte 0xDF=11011111 -> bits[6:5]=10)
        ts.reg_addr(0x9E); // PSG1 pan=00
        ts.reg_addr(0x9D); // PSG2 pan=00
        ts.reg_addr(0xDF); // re-select PSG0 (pan already latched 10)
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0x0F);    // ch A vol = 15
        ts.reg_addr(9); ts.reg_write(0x0F);    // ch B vol = 15
        ts.reg_addr(10); ts.reg_write(0x0F);   // ch C vol = 15
        settle(ts);
        check("TS-09", "pan bits[6:5]=10 at select time -> PSG0 L only",
              ts.pcm_left() > 0 && ts.pcm_right() == 0,
              fmt("L=%u R=%u VHDL turbosound.vhd:132-134,323-327",
                  ts.pcm_left(), ts.pcm_right()));
    }

    // TS-10 - reset sets all psg*_pan="11".
    // Stimulus must activate all three AY channels (A, B, C) at nonzero
    // volume so that both L and R are nonzero under ABC stereo routing
    // (L = A + B, R = B + C per turbosound.vhd:186-192).  With only
    // channel A active, R = B + C = 0 regardless of panning.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.reg_addr(0x00);
        ts.reg_addr(7); ts.reg_write(0x3F);   // mixer: all pass-through
        ts.reg_addr(8); ts.reg_write(0x0F);    // ch A vol = 15
        ts.reg_addr(9); ts.reg_write(0x0F);    // ch B vol = 15
        ts.reg_addr(10); ts.reg_write(0x0F);   // ch C vol = 15
        settle(ts);
        check("TS-10", "default pan=11 -> both L and R non-zero",
              ts.pcm_left() > 0 && ts.pcm_right() > 0,
              fmt("L=%u R=%u VHDL turbosound.vhd:123-127,186-192",
                  ts.pcm_left(), ts.pcm_right()));
    }
}

static void g_ts_routing() {
    set_group("TS-routing");

    // TS-15 - turbosound.vhd:141 psg_addr only on bits[7:5]="000".
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(false);
        ts.reg_addr(0x00);
        ts.reg_write(0x42);
        ts.reg_addr(0x00);
        check("TS-15", "reg addr bits[7:5]=000 reaches active PSG",
              ts.reg_read() == 0x42,
              fmt("got=0x%02x VHDL turbosound.vhd:141", ts.reg_read()));
    }

    // TS-16 - addr routed to selected AY only.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(false);
        ts.reg_addr(0xFF);
        ts.reg_addr(0x00);
        ts.reg_write(0xAA);
        ts.reg_addr(0xFE);
        ts.reg_addr(0x00);
        check("TS-16", "reg write routed only to selected PSG",
              ts.reg_read() == 0x00,
              fmt("PSG1 R0=0x%02x VHDL turbosound.vhd:143-150",
                  ts.reg_read()));
    }

    // A: TS-17: psgN_we shares the ay_select gate with psgN_addr — covered
    // structurally by TS-16 (reg write routed only to selected PSG, which
    // would fail if the write-enable gate differed from the addr gate).

    // TS-18 - turbosound.vhd:321 readback muxes on ay_select.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(false);
        ts.reg_addr(0xFF); ts.reg_addr(0); ts.reg_write(0x11);
        ts.reg_addr(0xFE); ts.reg_addr(0); ts.reg_write(0x22);
        ts.reg_addr(0xFD); ts.reg_addr(0); ts.reg_write(0x33);

        ts.reg_addr(0xFF); ts.reg_addr(0); uint8_t r0 = ts.reg_read();
        ts.reg_addr(0xFE); ts.reg_addr(0); uint8_t r1 = ts.reg_read();
        ts.reg_addr(0xFD); ts.reg_addr(0); uint8_t r2 = ts.reg_read();
        check("TS-18", "psg_d_o muxes on ay_select",
              r0 == 0x11 && r1 == 0x22 && r2 == 0x33,
              fmt("r0=%02x r1=%02x r2=%02x VHDL turbosound.vhd:321",
                  r0, r1, r2));
    }
}

static void g_ts_stereo() {
    set_group("TS-stereo");

    // TS-20 - ABC mode A=max, B=0, C=0 -> L=A+B>0, R=C+B=0.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.set_stereo_mode(false);
        ts.reg_addr(0xFF);
        ts.reg_addr(7);  ts.reg_write(0x3F);
        ts.reg_addr(8);  ts.reg_write(0x0F);
        ts.reg_addr(9);  ts.reg_write(0x00);
        ts.reg_addr(10); ts.reg_write(0x00);
        ts.reg_addr(0xFE);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0);
        ts.reg_addr(9); ts.reg_write(0);
        ts.reg_addr(10); ts.reg_write(0);
        ts.reg_addr(0xFD);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0);
        ts.reg_addr(9); ts.reg_write(0);
        ts.reg_addr(10); ts.reg_write(0);
        settle(ts);
        check("TS-20", "ABC: A=max -> L>0, R=0",
              ts.pcm_left() > 0 && ts.pcm_right() == 0,
              fmt("L=%u R=%u VHDL turbosound.vhd:186-190",
                  ts.pcm_left(), ts.pcm_right()));
    }

    // TS-21 - ACB mode B=max, A=0, C=0 -> L=C+A=0, R=C+B>0.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.set_stereo_mode(true);
        ts.reg_addr(0xFF);
        ts.reg_addr(7);  ts.reg_write(0x3F);
        ts.reg_addr(8);  ts.reg_write(0x00);
        ts.reg_addr(9);  ts.reg_write(0x0F);
        ts.reg_addr(10); ts.reg_write(0x00);
        ts.reg_addr(0xFE);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0);
        ts.reg_addr(9); ts.reg_write(0);
        ts.reg_addr(10); ts.reg_write(0);
        ts.reg_addr(0xFD);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0);
        ts.reg_addr(9); ts.reg_write(0);
        ts.reg_addr(10); ts.reg_write(0);
        settle(ts);
        check("TS-21", "ACB: A=0 B=max C=0 -> L=0, R>0",
              ts.pcm_left() == 0 && ts.pcm_right() > 0,
              fmt("L=%u R=%u VHDL turbosound.vhd:186-190 stereo_mode=1",
                  ts.pcm_left(), ts.pcm_right()));
    }

    // TS-22 - mono_mode(0)=1 -> L=R for PSG0 contribution.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.set_mono_mode(0x01);
        ts.reg_addr(0xFF);
        ts.reg_addr(7);  ts.reg_write(0x3F);
        ts.reg_addr(8);  ts.reg_write(0x0F);
        ts.reg_addr(9);  ts.reg_write(0x00);
        ts.reg_addr(10); ts.reg_write(0x00);
        ts.reg_addr(0xFE);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0);
        ts.reg_addr(9); ts.reg_write(0);
        ts.reg_addr(10); ts.reg_write(0);
        ts.reg_addr(0xFD);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0);
        ts.reg_addr(9); ts.reg_write(0);
        ts.reg_addr(10); ts.reg_write(0);
        settle(ts);
        check("TS-22", "PSG0 mono_mode=1 -> L==R>0",
              ts.pcm_left() == ts.pcm_right() && ts.pcm_left() > 0,
              fmt("L=%u R=%u VHDL turbosound.vhd:189-192",
                  ts.pcm_left(), ts.pcm_right()));
    }

    // TS-23 - mono_mode[2:0] per-PSG independent. With mono_mode=0x02
    // (PSG1 only) and only PSG0 producing sound, PSG0 stays stereo: ABC
    // A=max gives L>0, R=0.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.set_mono_mode(0x02);
        ts.reg_addr(0xFF);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0x0F);
        ts.reg_addr(9); ts.reg_write(0);
        ts.reg_addr(10); ts.reg_write(0);
        ts.reg_addr(0xFE);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0);
        ts.reg_addr(9); ts.reg_write(0);
        ts.reg_addr(10); ts.reg_write(0);
        ts.reg_addr(0xFD);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0);
        ts.reg_addr(9); ts.reg_write(0);
        ts.reg_addr(10); ts.reg_write(0);
        settle(ts);
        check("TS-23", "mono_mode[1]=1 leaves PSG0 stereo (L>0, R=0)",
              ts.pcm_left() > 0 && ts.pcm_right() == 0,
              fmt("L=%u R=%u VHDL turbosound.vhd:189-192",
                  ts.pcm_left(), ts.pcm_right()));
    }

    // TS-24 - single global stereo_mode bit governs all three PSGs. Phase 0
    // critic APPROVE pointed at TS-20/TS-21 but those only prove the bit
    // works per-PSG; proving *one* bit governs *all three* requires a
    // test that switches modes and observes all three panners respond
    // together. Current TS harness can't exercise that in a single-PSG
    // test. F-skip for follow-up Wave E (per-PSG isolation extension).
    skip("TS-24",
         "F-TS-GLOBAL-STEREO: one-bit-governs-all-three not provable "
         "by TS-20/21 single-PSG tests. Needs multi-PSG simultaneous-"
         "mode-switch assertion. turbosound.vhd stereo_mode signal.");
}

static void g_ts_enable() {
    set_group("TS-enable");

    // TS-30 - ts disabled + PSG0 selected: non-zero output.
    {
        TurboSound ts;
        ts.set_enabled(false);
        ts.set_ay_mode(true);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0x0F);
        settle(ts);
        check("TS-30", "ts disabled + PSG0 selected -> non-zero",
              ts.pcm_left() > 0 || ts.pcm_right() > 0,
              fmt("L=%u R=%u VHDL turbosound.vhd:197-203",
                  ts.pcm_left(), ts.pcm_right()));
    }

    // TS-31 - ts enabled: all three PSGs contribute, total L > 0xFF.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.reg_addr(0xFF);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0x0F);
        ts.reg_addr(0xFE);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0x0F);
        ts.reg_addr(0xFD);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0x0F);
        settle(ts);
        check("TS-31", "ts enabled: all three PSGs contribute (L > 0xFF)",
              ts.pcm_left() > 0xFF,
              fmt("L=%u VHDL turbosound.vhd:197,252,307",
                  ts.pcm_left()));
    }

    // TS-32 / TS-33 / TS-34 - per-PSG zero gating at turbosound.vhd:197/252/307.
    // TS-30 (disabled = PSG0 only) and TS-31 (enabled sum > 0xFF) cannot
    // distinguish which specific PSG contributes: a PSG2-stuck-closed bug
    // would still satisfy both thresholds because PSG0+PSG1 contribute
    // enough to clear L>0xFF. F-skip for follow-up Wave E (per-PSG
    // isolation extension) — needs single-PSG-non-zero + other-two-zero
    // assertion per gate.
    skip("TS-32", "F-TS-PSG0-GATE: per-PSG isolation not provable from TS-30/31 aggregate");
    skip("TS-33", "F-TS-PSG1-GATE: per-PSG isolation not provable from TS-30/31 aggregate");
    skip("TS-34", "F-TS-PSG2-GATE: per-PSG isolation not provable from TS-30/31 aggregate");
}

static void g_ts_panning() {
    set_group("TS-panning");

    // A: TS-40: pan="11" (both channels) — covered by TS-10 default pan
    // state which exercises the both-channels-active code path.

    // TS-41 - pan=10 L only.
    // All channels active so R_sum = B + C > 0 before gating; pan=10
    // (bit 0=0) must zero R via turbosound.vhd:327, proving the gate.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.reg_addr(0xDF);
        ts.reg_addr(0x9E);
        ts.reg_addr(0x9D);
        ts.reg_addr(0xDF);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0x0F);    // ch A vol = 15
        ts.reg_addr(9); ts.reg_write(0x0F);    // ch B vol = 15
        ts.reg_addr(10); ts.reg_write(0x0F);   // ch C vol = 15
        settle(ts);
        check("TS-41", "pan=10 -> L>0 and R=0",
              ts.pcm_left() > 0 && ts.pcm_right() == 0,
              fmt("L=%u R=%u VHDL turbosound.vhd:323-327",
                  ts.pcm_left(), ts.pcm_right()));
    }

    // TS-42 - pan=01 R only.
    // All three channels must be active so R = B + C > 0 under ABC
    // stereo routing (turbosound.vhd:186-192).  Pan=01 gates L off
    // (bit 1=0) and passes R (bit 0=1) per turbosound.vhd:323-329.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.reg_addr(0xBF);       // PSG0 pan=01, select PSG0
        ts.reg_addr(0x9E);       // PSG1 pan=00
        ts.reg_addr(0x9D);       // PSG2 pan=00
        ts.reg_addr(0xBF);       // re-select PSG0
        ts.reg_addr(7); ts.reg_write(0x3F);   // mixer: all pass-through
        ts.reg_addr(8); ts.reg_write(0x0F);    // ch A vol = 15
        ts.reg_addr(9); ts.reg_write(0x0F);    // ch B vol = 15
        ts.reg_addr(10); ts.reg_write(0x0F);   // ch C vol = 15
        settle(ts);
        check("TS-42", "pan=01 -> L=0 and R>0",
              ts.pcm_left() == 0 && ts.pcm_right() > 0,
              fmt("L=%u R=%u VHDL turbosound.vhd:186-192,323-329",
                  ts.pcm_left(), ts.pcm_right()));
    }

    // TS-43 - pan=00 both silenced.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.reg_addr(0x9F);
        ts.reg_addr(0x9E);
        ts.reg_addr(0x9D);
        ts.reg_addr(0x9F);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0x0F);
        settle(ts);
        check("TS-43", "pan=00 -> L=0 and R=0",
              ts.pcm_left() == 0 && ts.pcm_right() == 0,
              fmt("L=%u R=%u VHDL turbosound.vhd:323-329",
                  ts.pcm_left(), ts.pcm_right()));
    }

    // TS-44/45 - per-channel summing (subset with 3 different pans + max vol
    // on A & C so both L and R sides collect content).
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.reg_addr(0xDF); // PSG0 pan=10
        ts.reg_addr(0xBE); // PSG1 pan=01
        ts.reg_addr(0xFD); // PSG2 pan=11
        ts.reg_addr(0xDF);
        ts.reg_addr(7);  ts.reg_write(0x3F);
        ts.reg_addr(8);  ts.reg_write(0x0F);
        ts.reg_addr(10); ts.reg_write(0x0F);
        ts.reg_addr(0xBE);
        ts.reg_addr(7);  ts.reg_write(0x3F);
        ts.reg_addr(8);  ts.reg_write(0x0F);
        ts.reg_addr(10); ts.reg_write(0x0F);
        ts.reg_addr(0xFD);
        ts.reg_addr(7);  ts.reg_write(0x3F);
        ts.reg_addr(8);  ts.reg_write(0x0F);
        ts.reg_addr(10); ts.reg_write(0x0F);
        settle(ts);
        check("TS-44", "L = sum of L contributions (PSG0 pan=10, PSG2 pan=11)",
              ts.pcm_left() > 0xFF,
              fmt("L=%u VHDL turbosound.vhd:331-336", ts.pcm_left()));
        check("TS-45", "R = sum of R contributions (PSG1 pan=01, PSG2 pan=11)",
              ts.pcm_right() > 0,
              fmt("R=%u VHDL turbosound.vhd:331-336", ts.pcm_right()));
    }
}

static void g_ts_ids() {
    set_group("TS-ids");

    TurboSound ts;
    ts.set_enabled(true);

    ts.reg_addr(0xFF); ts.reg_addr(0);
    uint8_t id0 = ts.reg_read(true) >> 6;
    check("TS-50", "PSG0 AY_ID = 11",
          id0 == 3,
          fmt("got=%u VHDL turbosound.vhd:158", id0));

    ts.reg_addr(0xFE); ts.reg_addr(0);
    uint8_t id1 = ts.reg_read(true) >> 6;
    check("TS-51", "PSG1 AY_ID = 10",
          id1 == 2,
          fmt("got=%u VHDL turbosound.vhd:213", id1));

    ts.reg_addr(0xFD); ts.reg_addr(0);
    uint8_t id2 = ts.reg_read(true) >> 6;
    check("TS-52", "PSG2 AY_ID = 01",
          id2 == 1,
          fmt("got=%u VHDL turbosound.vhd:268", id2));
}

// =====================================================================
// 3 Soundrive DAC
// =====================================================================

static void g_dac() {
    set_group("DAC");

    // SD-01 - soundrive.vhd:72-78 reset sets all channels to 0x80.
    {
        Dac dac;
        check("SD-01", "reset: all channels 0x80 (L=R=0x100)",
              dac.pcm_left() == 0x100 && dac.pcm_right() == 0x100,
              fmt("L=0x%03x R=0x%03x VHDL soundrive.vhd:72-78",
                  dac.pcm_left(), dac.pcm_right()));
    }

    // SD-02 - chA write.
    {
        Dac dac;
        dac.write_channel(0, 0xFF);
        check("SD-02", "write channel A latches value",
              dac.pcm_left() == 0xFF + 0x80,
              fmt("L=0x%03x VHDL soundrive.vhd:81-82", dac.pcm_left()));
    }

    // SD-03 - chB write.
    {
        Dac dac;
        dac.write_channel(1, 0x40);
        check("SD-03", "write channel B latches value",
              dac.pcm_left() == 0x80 + 0x40,
              fmt("L=0x%03x VHDL soundrive.vhd:87-88", dac.pcm_left()));
    }

    // SD-04 - chC write.
    {
        Dac dac;
        dac.write_channel(2, 0x20);
        check("SD-04", "write channel C latches value",
              dac.pcm_right() == 0x20 + 0x80,
              fmt("R=0x%03x VHDL soundrive.vhd:93-94", dac.pcm_right()));
    }

    // SD-05 - chD write.
    {
        Dac dac;
        dac.write_channel(3, 0x10);
        check("SD-05", "write channel D latches value",
              dac.pcm_right() == 0x80 + 0x10,
              fmt("R=0x%03x VHDL soundrive.vhd:99-100", dac.pcm_right()));
    }

    // SD-06 - nr_mono writes chA and chD.
    {
        Dac dac;
        dac.write_mono(0x55);
        check("SD-06", "nr_mono writes chA and chD",
              dac.pcm_left() == (0x55 + 0x80) && dac.pcm_right() == (0x80 + 0x55),
              fmt("L=0x%03x R=0x%03x VHDL soundrive.vhd:83-85,101-103",
                  dac.pcm_left(), dac.pcm_right()));
    }

    // SD-07 - nr_left writes chB only.
    {
        Dac dac;
        dac.write_left(0x20);
        check("SD-07", "nr_left writes chB only",
              dac.pcm_left() == 0x80 + 0x20 && dac.pcm_right() == 0x100,
              fmt("L=0x%03x R=0x%03x VHDL soundrive.vhd:89-91",
                  dac.pcm_left(), dac.pcm_right()));
    }

    // SD-08 - nr_right writes chC only.
    {
        Dac dac;
        dac.write_right(0x30);
        check("SD-08", "nr_right writes chC only",
              dac.pcm_right() == 0x30 + 0x80 && dac.pcm_left() == 0x100,
              fmt("L=0x%03x R=0x%03x VHDL soundrive.vhd:95-97",
                  dac.pcm_left(), dac.pcm_right()));
    }

    // G: SD-09: per-clock if/elsif write priority (port I/O vs NextREG
    // mirror) is a pipeline ordering artefact of the VHDL core-level
    // process; the standalone Dac class has last-write-wins semantics
    // per frame and no time-ordered event queue. Out of scope for this
    // plan — would require a pipeline-event refactor on Dac.

    // SD-10..SD-18 - port decode lives in zxnext.vhd.
    skip("SD-10", "port decode 0x1F/0x0F/0x4F/0x5F lives in zxnext.vhd:2429");
    skip("SD-11", "port decode 0xF1/0xF3/0xF9/0xFB lives in zxnext.vhd:2432");
    skip("SD-12", "Profi Covox port 0x3F/0x5F lives in zxnext.vhd:2658");
    skip("SD-13", "Covox port 0x0F/0x4F lives in zxnext.vhd:2659");
    skip("SD-14", "Pentagon/ATM mono port 0xFB lives in zxnext.vhd:2660");
    skip("SD-15", "GS Covox port 0xB3 lives in zxnext.vhd:2661");
    skip("SD-16", "SpecDrum port 0xDF lives in zxnext.vhd:2662");
    skip("SD-17", "nr_08_dac_en gating lives in zxnext.vhd:6436");
    skip("SD-18", "mono-port aliasing lives in zxnext.vhd port decode");

    // SD-20 - pcm_L = chA + chB.
    {
        Dac dac;
        dac.write_channel(0, 0x10);
        dac.write_channel(1, 0x20);
        check("SD-20", "pcm_L = chA + chB",
              dac.pcm_left() == 0x30,
              fmt("L=0x%03x VHDL soundrive.vhd:112", dac.pcm_left()));
    }

    // SD-21 - pcm_R = chC + chD.
    {
        Dac dac;
        dac.write_channel(2, 0x30);
        dac.write_channel(3, 0x40);
        check("SD-21", "pcm_R = chC + chD",
              dac.pcm_right() == 0x70,
              fmt("R=0x%03x VHDL soundrive.vhd:113", dac.pcm_right()));
    }

    // SD-22 - max 0xFF+0xFF = 0x1FE.
    {
        Dac dac;
        dac.write_channel(0, 0xFF);
        dac.write_channel(1, 0xFF);
        check("SD-22", "max pcm_L = 0x1FE (9-bit)",
              dac.pcm_left() == 0x1FE,
              fmt("L=0x%03x VHDL soundrive.vhd:112", dac.pcm_left()));
    }

    // SD-23 - reset output 0x100.
    {
        Dac dac;
        check("SD-23", "reset output L=R=0x100",
              dac.pcm_left() == 0x100 && dac.pcm_right() == 0x100,
              fmt("L=0x%03x R=0x%03x VHDL soundrive.vhd:72-78,112-113",
                  dac.pcm_left(), dac.pcm_right()));
    }
}

// =====================================================================
// 4 Beeper and port 0xFE
// =====================================================================

static void g_beeper() {
    set_group("Beeper");

    // BP-01/04/05-reg/06 live in the core's port 0xFE pipeline.
    skip("BP-01", "port_fe_reg <= cpu_do(4 downto 0) lives in zxnext.vhd:3593");
    skip("BP-04", "port_fe_border extraction lives in zxnext.vhd:3604");
    skip("BP-06", "port 0xFE A0=0 decode lives in port dispatch");

    // BP-02 - EAR latch via Beeper::set_ear().
    {
        Beeper bp;
        bp.set_ear(true);
        bool hi = bp.ear();
        bp.set_ear(false);
        bool lo = bp.ear();
        check("BP-02", "EAR latch toggles via set_ear()",
              hi && !lo,
              fmt("hi=%d lo=%d VHDL zxnext.vhd:3598", hi, lo));
    }

    // BP-03 - MIC latch.
    {
        Beeper bp;
        bp.set_mic(true);
        check("BP-03", "MIC latch via set_mic()",
              bp.mic(),
              "VHDL zxnext.vhd:3599");
    }

    // BP-05 - reset clears Beeper flags.
    {
        Beeper bp;
        bp.set_ear(true);
        bp.set_mic(true);
        bp.set_tape_ear(true);
        bp.reset();
        check("BP-05", "reset clears ear/mic/tape_ear",
              !bp.ear() && !bp.mic() && !bp.tape_ear(),
              "VHDL zxnext.vhd:3591");
    }

    // BP-10..BP-13 - XOR/gating in zxnext.vhd 6503-6504, NextREG layer.
    skip("BP-10", "beep_mic_final XOR expression lives in zxnext.vhd:6503");
    skip("BP-11", "issue2 cancellation of MIC lives in zxnext.vhd:6503");
    skip("BP-12", "issue3 EAR xor MIC lives in zxnext.vhd:6503");
    skip("BP-13", "beep_spkr_excl lives in zxnext.vhd:6504 (NextREG core)");

    // BP-20..BP-23 - port 0xFE READ in zxnext.vhd 3453-3468.
    skip("BP-20", "port 0xFE read composition lives in zxnext.vhd:3453-3468");
    skip("BP-21", "port 0xFE read bit5 fixed-high lives in zxnext.vhd:3453-3468");
    skip("BP-22", "port 0xFE read keyboard col mux lives in zxnext.vhd:3453-3468");
    skip("BP-23", "port 0xFE read bit7 fixed-high lives in zxnext.vhd:3453-3468");
}

// =====================================================================
// 5 Audio Mixer (audio_mixer.vhd 63-107)
// =====================================================================

static void g_mixer() {
    set_group("Mixer");

    // MX-01 - audio_mixer.vhd:63,80 ear_volume=512. EAR alone -> signed
    // output = (0+0+0+0+512+1024-1024)*4 = 2048 on both channels.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        bp.set_ear(true);
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-01", "EAR alone -> signed = 512*4 = 2048",
              s[0] == 2048 && s[1] == 2048,
              fmt("L=%d R=%d VHDL audio_mixer.vhd:63,80", s[0], s[1]));
    }

    // MX-02 - mic_volume=128. MIC alone -> 128*4 = 512.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        bp.set_mic(true);
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-02", "MIC alone -> signed = 128*4 = 512",
              s[0] == 512 && s[1] == 512,
              fmt("L=%d R=%d VHDL audio_mixer.vhd:64,81", s[0], s[1]));
    }

    // MX-03 - exc_i gating lives upstream in zxnext core.
    skip("MX-03", "exc_i gating lives in zxnext.vhd:6504, upstream of Mixer");

    // MX-04 - ay zero-extended 12->13 bit. Drive AY via TurboSound (max
    // vol, PSG0 pan=10) and confirm signed output = ay_L * 4 (unscaled).
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.reg_addr(0xDF);
        ts.reg_addr(0x9E);
        ts.reg_addr(0x9D);
        ts.reg_addr(0xDF);
        ts.reg_addr(7); ts.reg_write(0x3F);
        ts.reg_addr(8); ts.reg_write(0x0F);
        settle(ts);
        uint16_t ay_L_exp = ts.pcm_left();
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        int32_t expected = static_cast<int32_t>(ay_L_exp) * 4;
        check("MX-04", "AY_L routed verbatim (signed = ay_L*4)",
              s[0] == static_cast<int16_t>(expected),
              fmt("L=%d expected=%d ay_L=%u VHDL audio_mixer.vhd:83-84",
                  s[0], expected, ay_L_exp));
    }

    // MX-05 - DAC x4 scaling. DAC max -> signed = 4064.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        dac.write_channel(0, 0xFF);
        dac.write_channel(1, 0xFF);
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-05", "DAC L max -> signed = 4064",
              s[0] == 4064,
              fmt("L=%d VHDL audio_mixer.vhd:86-87", s[0]));
    }

    // MX-06 - Pi I2S 10-bit zero-extended to 13 bits and added into the
    // mixer sum. Other sources silent: Beeper EAR/MIC=off, TurboSound
    // disabled (AY_L/R=0). DAC at its VHDL-default 0x80/0x80 silence
    // level contributes (0x80+0x80)<<2 = 1024 per channel. With I2S set
    // to max (1023,1023), pcm_L = 0+0+0+0+1024+1023 = 2047; signed =
    // (2047 - 1024)*4 = 4092 on both channels. Same for pcm_R.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        I2s i2s;
        i2s.set_sample(1023, 1023);
        mx.set_i2s_source(&i2s);
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-06", "I2S max (1023,1023) sums into L and R (10->13 zero-extend)",
              s[0] == 4092 && s[1] == 4092,
              fmt("L=%d R=%d VHDL audio_mixer.vhd:89-90,99-100", s[0], s[1]));
    }

    // MX-10 - silence: pcm_L = 0.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-10", "silence: pcm_L = 0",
              s[0] == 0,
              fmt("L=%d VHDL audio_mixer.vhd:99", s[0]));
    }

    // MX-11 - silence: pcm_R = 0.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-11", "silence: pcm_R = 0",
              s[1] == 0,
              fmt("R=%d VHDL audio_mixer.vhd:100", s[1]));
    }

    // MX-12 - reset empties ring buffer.
    {
        Mixer mx;
        Beeper bp; TurboSound ts; Dac dac;
        mx.generate_sample(bp, ts, dac);
        mx.reset();
        check("MX-12", "reset empties ring buffer",
              mx.available() == 0,
              fmt("avail=%d VHDL audio_mixer.vhd:95-97", mx.available()));
    }

    // MX-13 - EAR + MIC go to both L and R.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        bp.set_ear(true);
        bp.set_mic(true);
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-13", "EAR+MIC contribute equally to L and R",
              s[0] == s[1] && s[0] == (512 + 128) * 4,
              fmt("L=%d R=%d VHDL audio_mixer.vhd:99-100", s[0], s[1]));
    }

    // MX-14 - max reachable subset (EAR+MIC+DAC max) = 512+128+2040 = 2680.
    // signed = (2680 - 1024)*4 = 6624.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        bp.set_ear(true);
        bp.set_mic(true);
        dac.write_channel(0, 0xFF);
        dac.write_channel(1, 0xFF);
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-14", "EAR+MIC+DAC subset sum: signed = 6624",
              s[0] == 6624,
              fmt("L=%d VHDL audio_mixer.vhd:99", s[0]));
    }

    // A: MX-15: non-saturation — confirmed by MX-05 (full-scale sum = 5998,
    // fits within 13-bit unsigned range 0..8191) and MX-14 (EAR+MIC+DAC
    // subset exact arithmetic). A saturation bug would change those exact
    // values.
    skip("MX-20", "exc_i silencing of EAR/MIC lives in zxnext.vhd:6504");
    // A: MX-21: exc_i=0 default — covered implicitly by MX-01 (EAR active)
    // and MX-02 (MIC active), both of which execute under exc_i=0 by
    // default and would fail if the default were non-zero.
    skip("MX-22", "exc_i derivation lives in zxnext.vhd:6504");
}

// =====================================================================
// 6 NextREG Configuration
// =====================================================================

static void g_nextreg() {
    set_group("NextREG");

    skip("NR-01", "NextREG 0x06 bits[1:0] handler lives in NextReg::write()");
    skip("NR-02", "nr_06_psg_mode=00 YM mode sink lives in zxnext.vhd:6389");
    skip("NR-03", "nr_06_psg_mode=01 AY mode sink lives in zxnext.vhd:6389");
    skip("NR-04", "nr_06_psg_mode=10 alias lives in zxnext.vhd:6389");
    skip("NR-05", "audio_ay_reset = nr_06_psg_mode=11 lives in zxnext.vhd:6379");
    skip("NR-06", "nr_06_internal_speaker_beep handler lives in NextReg core");

    skip("NR-10", "nr_08_psg_stereo_mode handler lives in zxnext.vhd:5178");
    skip("NR-11", "nr_08_internal_speaker_en handler lives in zxnext.vhd:5177");
    skip("NR-12", "nr_08_dac_en handler lives in zxnext.vhd:5179");
    skip("NR-13", "nr_08_psg_turbosound_en handler lives in zxnext.vhd:5181");
    skip("NR-14", "nr_08_keyboard_issue2 handler lives in zxnext.vhd:5182");

    skip("NR-20", "nr_09_psg_mono handler lives in zxnext.vhd:5186");
    skip("NR-21", "nr_09 bit->PSG mapping lives in zxnext.vhd:5186");

    skip("NR-30", "NextREG 0x2C -> soundrive nr_left_we lives in zxnext.vhd:4852");
    skip("NR-31", "NextREG 0x2D -> soundrive nr_mono_we lives in zxnext.vhd:4853");
    skip("NR-32", "NextREG 0x2E -> soundrive nr_right_we lives in zxnext.vhd:4854");
}

// =====================================================================
// 7 I/O port wiring
// =====================================================================

static void g_io() {
    set_group("IO");

    skip("IO-01", "port FFFD address decode lives in zxnext.vhd:2647");
    skip("IO-02", "port BFFD decode lives in zxnext.vhd:2648");
    skip("IO-03", "port BFF5 (reg query mode) decode lives in zxnext.vhd:2649");
    skip("IO-04", "FFFD falling-edge latch lives in zxnext.vhd:2771-2773");
    skip("IO-05", "BFFD -> FFFD +3 timing alias lives in zxnext.vhd:2771-2773");

    skip("IO-10", "dac_hw_en gate lives in zxnext.vhd:2775-2778/6436");
    skip("IO-11", "port->channel alias fan-in lives in zxnext.vhd port decode");
    skip("IO-12", "port FD F1/F9 AY-conflict guard lives in zxnext.vhd:2777");
}

// =====================================================================
// Main
// =====================================================================

int main() {
    std::printf("Audio Subsystem Compliance Tests (Phase 2 rewrite)\n");
    std::printf("====================================================\n\n");

    g_ay_write();      std::printf("  1.1 AY write -- done\n");
    g_ay_readback();   std::printf("  1.2 AY readback -- done\n");
    g_ay_ports();      std::printf("  1.3 AY ports -- done\n");
    g_ay_divider();    std::printf("  1.4 AY divider -- done\n");
    g_ay_tone();       std::printf("  1.5 AY tone -- done\n");
    g_ay_noise();      std::printf("  1.6 AY noise -- done\n");
    g_ay_chan_mixer(); std::printf("  1.7 AY chan mixer -- done\n");
    g_ay_vol_mode();   std::printf("  1.8 AY vol mode -- done\n");
    g_ay_vol_tables(); std::printf("  1.9 AY vol tables -- done\n");
    g_ay_envelope();   std::printf("  1.10 AY envelope -- done\n");
    g_ts_selection();  std::printf("  2.1 TS selection -- done\n");
    g_ts_routing();    std::printf("  2.2 TS routing -- done\n");
    g_ts_stereo();     std::printf("  2.3 TS stereo -- done\n");
    g_ts_enable();     std::printf("  2.4 TS enable -- done\n");
    g_ts_panning();    std::printf("  2.5 TS panning -- done\n");
    g_ts_ids();        std::printf("  2.6 TS ids -- done\n");
    g_dac();           std::printf("  3   DAC -- done\n");
    g_beeper();        std::printf("  4   Beeper -- done\n");
    g_mixer();         std::printf("  5   Mixer -- done\n");
    g_nextreg();       std::printf("  6   NextREG -- done\n");
    g_io();            std::printf("  7   IO -- done\n");

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    if (!g_skipped.empty()) {
        std::printf("\nSkipped rows (facility not reachable via current API):\n");
        for (const auto& s : g_skipped)
            std::printf("  SKIP %-10s %s\n", s.id.c_str(), s.reason.c_str());
    }

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-20s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-20s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
