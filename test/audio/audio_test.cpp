// Audio Subsystem Compliance Test Runner
//
// Phase 2 full rewrite (Task 1 Wave 1, 2026-04-15) against
// doc/testing/AUDIO-TEST-PLAN-DESIGN.md. Every assertion cites the exact
// VHDL file and line from the authoritative FPGA source at
//   cores/zxnext/src/
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

#include <algorithm>
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
        // R0..R13 read their latch back directly. R14/R15 do NOT: the read
        // mux at ym2149.vhd:240-249 returns the PIN unless R7's direction
        // bit for that port is set, and the 0x47 just written to R7 leaves
        // port B an input. Open both ports first (GH #201) so the latch is
        // observable at all — the mixer bits of R7 are left as written.
        for (int r = 0; r < 14; ++r) {
            ay.select_register(r);
            uint8_t v = ay.read_data();
            if (v != static_cast<uint8_t>(0x40 | r)) { ok = false; bad_r = r; bad_v = v; break; }
        }
        if (ok) {
            ay.select_register(7); ay.write_data(0xC7);   // both ports OUTPUT
            for (int r = 14; r < 16; ++r) {
                ay.select_register(r);
                uint8_t v = ay.read_data();
                if (v != static_cast<uint8_t>(0x40 | r)) { ok = false; bad_r = r; bad_v = v; break; }
            }
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
    // shape 0D is `/‾‾‾` (ym2149.vhd:385-386) so it must settle at the TOP
    // rail, env_vol=31 (YM[31]=0xFF) — which can only happen if env_reset
    // loaded env_vol=0 and direction=up at the write. GH #201: the expected
    // value was YM[30]=0xE0, one level short, because the shape cascade
    // evaluated is_top_m1 against the POST-step volume.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0D);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-07", "R13 write pulses env_reset (shape 0D `/‾‾‾` settles "
              "to the top rail YM[31]=0xFF)",
              ay.output_a() == 0xFF,
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

    // GH #201 — AY-30..34 were a WONT under G30, on the grounds that
    // "AyChip lacks accessors for port_a_i/port_b_i". That cost premise was
    // wrong: the Next does not route those pins anywhere. turbosound.vhd
    // hard-ties both to all-ones for all three PSGs (:174-176, :229-231,
    // :284-286 — not :158, which is psg0's AY_ID generic), so their value
    // is a constant of this hardware and needs no plumbing at all. What
    // remained was a plain register read that any guest can perform, and
    // jnext answered it with the stored byte in both directions.
    //
    // VHDL ym2149.vhd:240-249 — R7 bits 7/6 are the port DIRECTION bits:
    //   when x"E" => if (reg(7)(6) = '0') then O_DA <= port_a_i;
    //                else                      O_DA <= reg(14) and port_a_i;
    //   when x"F" => if (reg(7)(7) = '0') then O_DA <= port_b_i;
    //                else                      O_DA <= reg(15) and port_b_i;
    // An INPUT port reads the pin, not the latch.

    // AY-30 — port A in input mode reads the pin, not what was written.
    {
        AyChip ay;
        ay.select_register(7);  ay.write_data(0x3F);   // R7 b6 = 0 → A input
        ay.select_register(14); ay.write_data(0x5A);   // latch a decoy
        ay.select_register(14);
        const uint8_t got = ay.read_data();
        check("AY-30", "R14 with R7 bit 6 = 0 (port A input) reads port_a_i, "
              "not the latched byte",
              got == 0xFF,
              fmt("got=0x%02x want=0xFF (decoy 0x5A latched) "
                  "VHDL ym2149.vhd:240-242", got));
    }

    // AY-31 — port A in output mode reads `reg(14) and port_a_i`; with the
    // pin tied high that is the stored byte, unmasked.
    {
        AyChip ay;
        ay.select_register(7);  ay.write_data(0x7F);   // R7 b6 = 1 → A output
        ay.select_register(14); ay.write_data(0x5A);
        ay.select_register(14);
        const uint8_t got = ay.read_data();
        check("AY-31", "R14 with R7 bit 6 = 1 (port A output) reads "
              "reg(14) AND port_a_i",
              got == 0x5A,
              fmt("got=0x%02x want=0x5A VHDL ym2149.vhd:240-244", got));
    }

    // AY-32 — port B input, the R15 mirror of AY-30.
    {
        AyChip ay;
        ay.select_register(7);  ay.write_data(0x7F);   // R7 b7 = 0 → B input
        ay.select_register(15); ay.write_data(0xA5);
        ay.select_register(15);
        const uint8_t got = ay.read_data();
        check("AY-32", "R15 with R7 bit 7 = 0 (port B input) reads port_b_i, "
              "not the latched byte",
              got == 0xFF,
              fmt("got=0x%02x want=0xFF (decoy 0xA5 latched) "
                  "VHDL ym2149.vhd:245-247", got));
    }

    // AY-33 — port B output, the R15 mirror of AY-31.
    {
        AyChip ay;
        ay.select_register(7);  ay.write_data(0xBF);   // R7 b7 = 1 → B output
        ay.select_register(15); ay.write_data(0xA5);
        ay.select_register(15);
        const uint8_t got = ay.read_data();
        check("AY-33", "R15 with R7 bit 7 = 1 (port B output) reads "
              "reg(15) AND port_b_i",
              got == 0xA5,
              fmt("got=0x%02x want=0xA5 VHDL ym2149.vhd:245-249", got));
    }

    // AY-34 — the pull-up itself: both pins are all-ones, so an input-mode
    // read is 0xFF for a latch of 0x00 as well as 0xFF, and an output-mode
    // read of 0x00 is 0x00 (the AND is transparent, it does not force the
    // line high). The 0x00-latch arm is what distinguishes a real tie-high
    // from a read that merely happens to return 0xFF.
    {
        AyChip ay;
        ay.select_register(7);  ay.write_data(0x3F);   // both ports INPUT
        ay.select_register(14); ay.write_data(0x00);
        ay.select_register(15); ay.write_data(0x00);
        ay.select_register(14); const uint8_t in_a = ay.read_data();
        ay.select_register(15); const uint8_t in_b = ay.read_data();
        ay.select_register(7);  ay.write_data(0xFF);   // both ports OUTPUT
        ay.select_register(14); const uint8_t out_a = ay.read_data();
        ay.select_register(15); const uint8_t out_b = ay.read_data();
        check("AY-34", "port_a_i / port_b_i are tied all-ones: an input-mode "
              "read of a 0x00 latch is 0xFF, an output-mode read of it is "
              "0x00",
              in_a == 0xFF && in_b == 0xFF && out_a == 0x00 && out_b == 0x00,
              fmt("in=%02x/%02x out=%02x/%02x "
                  "VHDL ym2149.vhd:240-249, turbosound.vhd:174-176",
                  in_a, in_b, out_a, out_b));
    }
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

    // AY-41 — RETIRED 2026-09-24 (GH #201). The ZX Next instantiates all
    // three PSGs with `I_SEL_L => '1'` (turbosound.vhd:164, :219, :274), so
    // `cnt_div <= (not I_SEL_L) & "111"` (ym2149.vhd:267) can only ever
    // reload "0111". The "1111" (/16) reload is a branch of the generic
    // ym2149 IP core that no ZX Next signal can select — the hardware
    // cannot enter the state this row describes. AY-44 asserts the /8
    // reload that IS reachable, and cites turbosound.vhd:164 for exactly
    // this reason. Struck in AUDIO-TEST-PLAN-DESIGN.md §1.4; no check()
    // row exists.

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

    // AY-43 — ena_div_noise runs at HALF the ena_div rate, and lags it by
    // one ena_div period.
    //
    // ym2149.vhd:266-272, one ENA tick per AyChip::tick():
    //     if (cnt_div = "0000") then
    //        cnt_div <= (not I_SEL_L) & "111";   -- reload 7 (:267)
    //        ena_div <= '1';                     -- (:268)
    //        noise_div <= not noise_div;         -- (:270) signal assignment
    //        if (noise_div = '1') then           -- (:271) reads the OLD value
    //           ena_div_noise <= '1';            -- (:272)
    //        end if;
    //
    // `cnt_div` and `noise_div` both power on at 0 (ym2149.vhd:105-106), so
    // ena_div fires on tick 1 and every 8th thereafter, while ena_div_noise
    // fires on the SECOND ena_div and every second one after it — half the
    // rate, offset by one full ena_div period.
    //
    // Both clocks are observable on one chip at once, on separate channels:
    // R7 = 0x35 leaves tone B enabled (b1=0) with noise B off (b4=1), and
    // noise A enabled (b3=0) with tone A off (b0=1) — ym2149.vhd:470-471.
    // Tone B with period 0 (comparator 0, :309) toggles on every ena_div, so
    // channel B's edges ARE the ena_div grid. Noise with R6=0 (comparator 0,
    // :283) shifts poly17 on every ena_div_noise, and the LFSR output can
    // only change at a shift, so every channel-A edge lands on the
    // ena_div_noise grid.
    //
    // Three measurements, all differences (immune to the whole-chip
    // one-tick output-pipeline phase):
    //   tone edge spacing            == 8   (the ena_div period)
    //   noise edge spacing           %  16  == 0  (the ena_div_noise period)
    //   (first noise edge - first tone edge) % 16 == 8
    // The last one is the phase: reading the toggled `noise_div` instead of
    // the old value keeps the /2 rate but puts ena_div_noise on the FIRST,
    // third, fifth ena_div — the offset becomes 0 and this row fails.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7);  ay.write_data(0x35);  // tone B on, noise A on
        ay.select_register(8);  ay.write_data(0x0F);  // ch A volume 15
        ay.select_register(9);  ay.write_data(0x0F);  // ch B volume 15
        ay.select_register(2);  ay.write_data(0x00);  // tone B period = 0
        ay.select_register(3);  ay.write_data(0x00);
        ay.select_register(6);  ay.write_data(0x00);  // noise period -> comp 0

        int first_tone = -1, first_noise = -1, prev_noise = -1;
        bool tone_grid_8 = true, noise_grid_16 = true;
        int tone_edges = 0, noise_edges = 0;
        uint8_t pt = ay.output_b(), pn = ay.output_a();
        int last_tone = -1;
        for (int t = 1; t <= 3000; ++t) {
            ay.tick();
            if (ay.output_b() != pt) {
                pt = ay.output_b();
                ++tone_edges;
                if (first_tone < 0) first_tone = t;
                else if ((t - last_tone) != 8) tone_grid_8 = false;
                last_tone = t;
            }
            if (ay.output_a() != pn) {
                pn = ay.output_a();
                ++noise_edges;
                if (first_noise < 0) first_noise = t;
                else if (((t - prev_noise) % 16) != 0) noise_grid_16 = false;
                prev_noise = t;
            }
        }
        const int phase = (first_noise >= 0 && first_tone >= 0)
                          ? ((first_noise - first_tone) % 16) : -1;
        check("AY-43",
              "ena_div_noise is half ena_div and lags it by one ena_div "
              "period: tone edges on an 8-tick grid, noise edges on a "
              "16-tick grid, offset 8",
              tone_edges > 8 && noise_edges > 4 &&
              tone_grid_8 && noise_grid_16 && phase == 8,
              fmt("tone_edges=%d(grid8=%d) noise_edges=%d(grid16=%d) "
                  "first_tone=%d first_noise=%d phase=%d (want 8) "
                  "VHDL ym2149.vhd:266-272,283,309,470-471",
                  tone_edges, tone_grid_8 ? 1 : 0,
                  noise_edges, noise_grid_16 ? 1 : 0,
                  first_tone, first_noise, phase));
    }

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

    // AY-63 — the noise output is poly17 BIT 0, and it is the single shared
    // noise for all three channels.
    //
    // ym2149.vhd:302 `noise_gen_op <= poly17(0)`, and the channel mixers at
    // :470-472 all consume that one signal. AY-62 only asserts the stream is
    // non-constant, which every bit of poly17 satisfies; this row pins WHICH
    // bit and that all three channels see the same one.
    //
    // Part A — bit-exact stream. The reference below re-implements the VHDL
    // recurrence independently of src/:
    //     poly17_zero <= '1' when poly17 = 0                      (:284)
    //     poly17 <= (poly17(0) xor poly17(2) xor poly17_zero)
    //               & poly17(16 downto 1)                          (:293)
    //     noise_gen_op <= poly17(0)                                (:302)
    // starting from the declared power-on value `(others => '0')` (:111).
    // With R6 = 0 the comparator is 0 (:283) so poly17 shifts on every
    // ena_div_noise, i.e. every 16 ticks starting at tick 9 (see AY-43), and
    // the output sampled just after shift k must equal the reference's k-th
    // bit. Sampling bit 16 or bit 1 instead diverges inside the first dozen
    // shifts.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7); ay.write_data(0x37);  // noise A only
        ay.select_register(8); ay.write_data(0x0F);
        ay.select_register(6); ay.write_data(0x00);  // comparator 0

        uint32_t ref_poly = 0;
        int mismatches = 0, first_bad = -1, ones = 0;
        int now = 0;
        for (int k = 0; k < 96; ++k) {
            const int target = 9 + 16 * k;           // the k-th shift tick
            while (now < target) { ay.tick(); ++now; }
            // Reference shift, ym2149.vhd:284,293.
            const uint32_t fb = ((ref_poly & 1u) ^ ((ref_poly >> 2) & 1u)
                                 ^ (ref_poly == 0u ? 1u : 0u)) & 1u;
            ref_poly = (fb << 16) | (ref_poly >> 1);
            const int want = static_cast<int>(ref_poly & 1u);   // :302
            const int got  = ay.output_a() ? 1 : 0;
            ones += want;
            if (got != want && first_bad < 0) first_bad = k;
            if (got != want) ++mismatches;
        }
        check("AY-63a",
              "noise output is poly17 BIT 0: 96 shifts match an independent "
              "re-implementation of the VHDL LFSR recurrence bit for bit",
              mismatches == 0 && ones > 0 && ones < 96,
              fmt("mismatches=%d first_bad=%d ref_ones=%d/96 "
                  "VHDL ym2149.vhd:111,284,293,302",
                  mismatches, first_bad, ones));
    }

    // Part B — one shared noise generator, not one per channel. R7 = 0x07
    // sets b2:0 = 111 (all three tone terms forced high, i.e. tones off) and
    // b5:3 = 000 (noise enabled on A, B and C). Per ym2149.vhd:470-472 every
    // channel is then exactly `noise_gen_op`, so with equal volumes the three
    // outputs must be equal at every tick. Per-channel LFSRs would
    // decorrelate within a few shifts.
    {
        AyChip ay;
        ay.set_ay_mode(true);
        ay.select_register(7);  ay.write_data(0x07);
        ay.select_register(8);  ay.write_data(0x0F);
        ay.select_register(9);  ay.write_data(0x0F);
        ay.select_register(10); ay.write_data(0x0F);
        ay.select_register(6);  ay.write_data(0x00);
        int diffs = 0, transitions = 0;
        uint8_t prev = ay.output_a();
        for (int t = 0; t < 4096; ++t) {
            ay.tick();
            if (ay.output_a() != ay.output_b() || ay.output_a() != ay.output_c())
                ++diffs;
            if (ay.output_a() != prev) { ++transitions; prev = ay.output_a(); }
        }
        check("AY-63b",
              "a single shared noise drives all three channel mixers "
              "(A == B == C on every tick, with the stream actually moving)",
              diffs == 0 && transitions > 4,
              fmt("diffs=%d transitions=%d VHDL ym2149.vhd:302,470-472",
                  diffs, transitions));
    }

    // AY-64 — the noise generator is clocked at the ena_div_noise rate, not
    // at ena_div and not at the ENA rate.
    //
    // ym2149.vhd:290 gates the whole noise process on `ena_div_noise = '1'`,
    // and :291-296 shift poly17 only once the period counter has reached
    // `noise_gen_comp` (= R6[4:0] - 1, or 0 when R6[4:1] = "0000", :283).
    // So one shift every (comp + 1) ena_div_noise pulses = 16 * (comp + 1)
    // ENA ticks, and the output can change only on a shift. Measuring the
    // spacing of the output's edges therefore reads the shift period back
    // out directly, and it must scale with R6:
    //     R6 = 0  -> comp 0 -> 16 ticks
    //     R6 = 3  -> comp 2 -> 48 ticks
    //     R6 = 5  -> comp 4 -> 80 ticks
    // Clocking at ena_div instead would halve every figure; clocking at the
    // ENA rate would divide them by 16.
    {
        struct Case { uint8_t r6; int comp; int period; };
        const Case cases[3] = { {0x00, 0, 16}, {0x03, 2, 48}, {0x05, 4, 80} };
        bool all_ok = true;
        char detail[256] = {0};
        int used = 0;
        for (const Case& c : cases) {
            AyChip ay;
            ay.set_ay_mode(true);
            ay.select_register(7); ay.write_data(0x37);   // noise A only
            ay.select_register(8); ay.write_data(0x0F);
            ay.select_register(6); ay.write_data(c.r6);
            const int comp = static_cast<int>(ay.noise_comp());
            int edges = 0, prev_edge = -1;
            bool on_grid = true;
            uint8_t pv = ay.output_a();
            for (int t = 1; t <= 80 * 17 * 3; ++t) {
                ay.tick();
                if (ay.output_a() != pv) {
                    pv = ay.output_a();
                    ++edges;
                    if (prev_edge >= 0 && ((t - prev_edge) % c.period) != 0)
                        on_grid = false;
                    prev_edge = t;
                }
            }
            const bool ok = (comp == c.comp) && on_grid && edges >= 2;
            if (!ok) all_ok = false;
            used += snprintf(detail + used,
                             sizeof(detail) - static_cast<size_t>(used),
                             "[R6=%02x comp=%d(want %d) edges=%d grid%d=%d]",
                             c.r6, comp, c.comp, edges, c.period,
                             on_grid ? 1 : 0);
            if (used >= static_cast<int>(sizeof(detail)) - 1) break;
        }
        check("AY-64",
              "noise shifts once per (comp+1) ena_div_noise pulses: the "
              "output-edge grid is 16*(comp+1) ticks for R6 = 0 / 3 / 5",
              all_ok,
              fmt("%s VHDL ym2149.vhd:283,290-296", detail));
    }
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

    // AY-91 - AY 4-bit volume DAC: index = top 4 bits of 5-bit volume.
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

    // AY-111 - ym2149.vhd:412-421 shape 4 `/___` (the shape table at
    // ym2149.vhd:373-374): C=0 with Attack=1 ramps UP and, at the step out
    // of env_vol=31, the 5-bit counter WRAPS to 0 (:405-406) while is_top
    // sets env_hold — so the steady state is SILENCE, not full volume.
    // GH #201: the expected value was 0xFF, which is the shape the table
    // draws for 0x0D, not for 0x04.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x04);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-111", "shape 4 (/___): rises, wraps and holds at 0 (YM=0x00)",
              ay.output_a() == 0x00,
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

    // AY-113 - shape 9 `\___` (ym2149.vhd:377-378). VHDL :428-431 sets
    // env_hold on is_bot_p1, and is_bot_p1 is evaluated against the volume
    // as of the clock edge (:361-366 are concurrent assignments), so the
    // hold arms at the step OUT of env_vol=1 — which lands on 0. Steady
    // state is therefore the bottom rail, matching the table's `\___`.
    // GH #201: the expected value was YM[1]=0x01, one level short, because
    // the flags were evaluated against the POST-step volume.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x09);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-113", "shape 9 `\\___` H=1 Alt=0 down: holds at the bottom "
              "rail YM[0]=0x00",
              ay.output_a() == 0x00,
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
        // GH #201 - the thresholds used to be `<= 0x04` / `>= 0xC0`, which a
        // descending ramp crosses on its way down even if it then LOCKS one
        // level inside the rail (which is exactly what the emulator did).
        // Require the RAILS themselves, and the bottom rail more than once
        // so a single descent cannot satisfy it - that second visit IS the
        // turn-round the shape table draws for C/At/Al/H = 1 0 1 0.
        int rails_low = 0, rails_high = 0;
        bool at_low = false, at_high = false;
        for (int i = 0; i < 8000; ++i) {
            ay.tick();
            const uint8_t v = ay.output_a();
            if (v == 0x00) { if (!at_low)  { ++rails_low;  at_low  = true; } }
            else             at_low  = false;
            if (v == 0xFF) { if (!at_high) { ++rails_high; at_high = true; } }
            else             at_high = false;
        }
        check("AY-114", "shape 10 triangle: reaches BOTH rails and turns "
              "round (bottom rail visited more than once)",
              rails_low >= 2 && rails_high >= 1,
              fmt("low=%d high=%d VHDL ym2149.vhd:444-461",
                  rails_low, rails_high));
    }

    // AY-115 - shape 11 `\‾‾‾` (ym2149.vhd:379-381): H=1 Alt=1 down. VHDL
    // :424-427 arms env_hold on is_bot, i.e. at the step out of env_vol=0 —
    // and that step WRAPS the counter to 31 (:405-406), so the envelope
    // decays and then holds at FULL volume, exactly as the table draws it.
    // GH #201: the expected value was 0x00, the opposite rail.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0B);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-115", "shape 11 `\\‾‾‾`: decays then holds at the top rail "
              "YM[31]=0xFF",
              ay.output_a() == 0xFF,
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

    // AY-117 - shape 13 `/‾‾‾` (ym2149.vhd:385-386): H=1 Alt=0 up. VHDL
    // :438-441 arms env_hold on is_top_m1, evaluated against the volume as
    // of the edge, so the hold lands after the step out of 30 — on 31.
    // GH #201: the expected value was YM[30]=0xE0, one level short.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0D);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-117", "shape 13 `/‾‾‾` H=1 Alt=0 up: holds at the top rail "
              "YM[31]=0xFF",
              ay.output_a() == 0xFF,
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
        // GH #201 — same strengthening as AY-114, mirrored: shape 14 rises
        // first, so it is the TOP rail that must be visited more than once
        // for the turn-round to have happened. The old thresholds
        // (`<= 0x04` / `>= 0xC0`) were crossed by the first ramp alone, so
        // they passed while the ramp locked one level inside the rail.
        int rails_low = 0, rails_high = 0;
        bool at_low = false, at_high = false;
        for (int i = 0; i < 8000; ++i) {
            ay.tick();
            const uint8_t v = ay.output_a();
            if (v == 0x00) { if (!at_low)  { ++rails_low;  at_low  = true; } }
            else             at_low  = false;
            if (v == 0xFF) { if (!at_high) { ++rails_high; at_high = true; } }
            else             at_high = false;
        }
        check("AY-118", "shape 14 `/\\/\\` triangle: reaches BOTH rails and "
              "turns round (top rail visited more than once)",
              rails_high >= 2 && rails_low >= 1,
              fmt("low=%d high=%d VHDL ym2149.vhd:444-461", rails_low, rails_high));
    }

    // AY-119 - shape 15 `/___` (ym2149.vhd:389-390): H=1 Alt=1 up. VHDL
    // :434-437 arms env_hold on is_top, i.e. at the step out of env_vol=31,
    // and that step wraps the counter to 0 — so the envelope rises and then
    // goes SILENT. GH #201: the expected value was 0xFF, the opposite rail.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x0F);
        for (int i = 0; i < 4000; ++i) ay.tick();
        check("AY-119", "shape 15 `/___`: rises then holds at the bottom "
              "rail YM[0]=0x00",
              ay.output_a() == 0x00,
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
        check("AY-123", "H=1 Alt=0: `\\___` holds YM[0]=0x00, `/‾‾‾` holds "
              "YM[31]=0xFF (ym2149.vhd:377-378, :385-386)",
              a.output_a() == 0x00 && b.output_a() == 0xFF,
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
        check("AY-124", "H=1 Alt=1: `\\‾‾‾` holds YM[31]=0xFF, `/___` holds "
              "YM[0]=0x00 (ym2149.vhd:379-381, :389-390)",
              a.output_a() == 0xFF && b.output_a() == 0x00,
              fmt("down=0x%02x up=0x%02x VHDL ym2149.vhd:422-443",
                  a.output_a(), b.output_a()));
    }

    // ── GH #201: AY-103 / AY-120 / AY-121 / AY-125..128 ─────────────
    //
    // These six plan rows used to be recorded here as "covered by" other
    // rows. They were not: an inference that a row WOULD have failed if the
    // envelope were wrong is not an assertion, and the matrix published all
    // six as `missing`. Each now has its own assertion, read from the
    // 32-entry `volTableYm` at ym2149.vhd:157-162 rather than from any
    // running level.
    //
    // Observation method (env_trace below): channel A is forced permanently
    // mixed-on by R7 bits 0 and 3, because ym2149.vhd:469 computes
    //   chan_mixed(0) <= (reg(7)(0) or tone_gen_op(1)) and (reg(7)(3) or noise_gen_op)
    // so with both bits '1' the channel is on every clock; R8 bit 4 then
    // routes `env_vol` into the output (:490-491) and, in YM mode,
    // ym2149.vhd:531 emits `volTableYm(env_vol)`. The trace IS the envelope.
    //
    // Sampling rate: one envelope step per `ena_div` pulse (:344-349) and
    // `ena_div` is every 8 chip clocks on this hardware — `cnt_div` reloads
    // with `(not I_SEL_L) & "111"` (:266) and turbosound.vhd ties I_SEL_L
    // to '1' for all three PSGs (:164, :219, :274) — so sampling every 8
    // ticks yields exactly one sample per envelope step.

    // volTableYm, ym2149.vhd:157-162, index 0..31.
    static const uint8_t kYm[32] = {
        0x00,0x01,0x01,0x02,0x02,0x03,0x03,0x04,
        0x06,0x07,0x09,0x0a,0x0c,0x0e,0x11,0x13,
        0x17,0x1b,0x20,0x25,0x2c,0x35,0x3e,0x47,
        0x54,0x66,0x77,0x88,0xa1,0xc0,0xe0,0xff
    };

    auto env_trace = [](uint8_t shape, int steps) {
        AyChip ay;
        ay.set_ay_mode(false);                        // YM: full 5-bit table
        ay.select_register(7);  ay.write_data(0x3F);  // chan_mixed(0) = '1'
        ay.select_register(8);  ay.write_data(0x10);  // channel A = envelope
        ay.select_register(11); ay.write_data(0x00);  // period 0 -> comp 0
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(shape);
        std::vector<uint8_t> out;
        for (int i = 0; i < steps; ++i) {
            for (int t = 0; t < 8; ++t) ay.tick();
            out.push_back(ay.output_a());
        }
        return out;
    };
    // Index of the first sample equal to `v`, or -1.
    auto first_of = [](const std::vector<uint8_t>& v, uint8_t want) {
        for (size_t i = 0; i < v.size(); ++i) if (v[i] == want) return (int)i;
        return -1;
    };

    // AY-127 — the counter is 5 bits wide and moves by exactly 1 per step.
    // Shape 8 (C=1, At=0, Al=0, H=0) free-runs downward and wraps
    // (ym2149.vhd:403-410, `+ "11111"` = -1 mod 32; no shape branch is
    // taken for Al=0/H=0 so nothing ever holds). Anchoring on the first
    // top sample makes the assertion independent of the sampling phase:
    // the next 32 samples must be volTableYm[31] down to volTableYm[0].
    {
        auto tr = env_trace(0x08, 200);
        int  a  = first_of(tr, 0xFF);
        bool ok = (a >= 0) && (a + 32 < (int)tr.size());
        int  bad = -1;
        if (ok) {
            for (int k = 0; k < 32; ++k) {
                if (tr[a + k] != kYm[31 - k]) { ok = false; bad = k; break; }
            }
        }
        check("AY-127", "envelope walks all 32 levels, one step apart "
              "(shape 8 anchored at the top emits volTableYm[31..0])",
              ok, fmt("anchor=%d first_mismatch=%d VHDL ym2149.vhd:403-410,157-162",
                      a, bad));
    }

    // AY-120 — Attack=0 loads env_vol="11111" and env_inc='0'
    // (ym2149.vhd:393-396). Shape 8 has At=0 and never holds, so the ramp
    // must begin at the TOP of the 32-level range and walk down to the
    // bottom in exactly 31 steps. The reset value itself is on the wire for
    // a single chip clock — env_reset also forces `env_ena <= '1'` (:341),
    // so a step is taken on the very next enable — which is why the first
    // step-aligned sample may already be volTableYm[30]. The row accepts
    // either and pins the DISTANCE to the bottom, which is what proves the
    // load was 31 and not some level below it.
    {
        auto tr = env_trace(0x08, 40);
        const bool from_top = (tr[0] == kYm[31]);
        const int  want_bot = from_top ? 31 : 30;
        bool nonincreasing = true;
        for (int i = 1; i <= want_bot; ++i)
            if (tr[i] > tr[i - 1]) { nonincreasing = false; break; }
        check("AY-120", "Attack=0 loads env_vol=31 counting down: the ramp "
              "starts at the top of the range, never rises, and reaches the "
              "bottom exactly 31 steps after the reset",
              (tr[0] == kYm[31] || tr[0] == kYm[30]) && nonincreasing &&
              first_of(tr, 0x00) == want_bot,
              fmt("first=0x%02x bottom_at=%d want=%d noninc=%d "
                  "VHDL ym2149.vhd:393-396,341",
                  tr[0], first_of(tr, 0x00), want_bot, (int)nonincreasing));
    }

    // AY-121 — Attack=1 loads env_vol="00000" and env_inc='1'
    // (ym2149.vhd:397-399). Shape 12 is the At=1 mirror of shape 8.
    {
        auto tr = env_trace(0x0C, 40);
        const bool from_bot = (tr[0] == kYm[0]);
        const int  want_top = from_bot ? 31 : 30;
        bool nondecreasing = true;
        for (int i = 1; i <= want_top; ++i)
            if (tr[i] < tr[i - 1]) { nondecreasing = false; break; }
        check("AY-121", "Attack=1 loads env_vol=0 counting up: the ramp "
              "starts at the bottom of the range, never falls, and reaches "
              "the top exactly 31 steps after the reset",
              (tr[0] == kYm[0] || tr[0] == kYm[1]) && nondecreasing &&
              first_of(tr, 0xFF) == want_top,
              fmt("first=0x%02x top_at=%d want=%d nondec=%d "
                  "VHDL ym2149.vhd:397-399,341",
                  tr[0], first_of(tr, 0xFF), want_top, (int)nondecreasing));
    }

    // AY-125 — C=1, H=0, Al=1 is the triangle: at each boundary the
    // ALTERNATE branch (ym2149.vhd:444-461) flips env_inc instead of
    // letting the counter wrap, and clears env_hold so the ramp never
    // locks. Discriminator against AY-126: after the bottom sample the
    // level goes back UP.
    {
        auto tr = env_trace(0x0A, 200);            // shape 10: C=1 At=0 Al=1 H=0
        int  b  = first_of(tr, 0x00);
        // Walk past the boundary dwell (is_bot_p1 sets env_hold for one
        // step before is_bot clears it, :448-452), then require ascent.
        int  i  = b;
        while (i + 1 < (int)tr.size() && tr[i + 1] == 0x00) ++i;
        bool up = (b >= 0) && (i + 1 < (int)tr.size()) && tr[i + 1] == kYm[1];
        bool reaches_top = first_of(tr, 0xFF) >= 0;
        check("AY-125", "C=1 H=0 Al=1 is a triangle: the direction REVERSES "
              "at the bottom (next level is volTableYm[1], not the top) and "
              "the ramp keeps running to the top again",
              up && reaches_top,
              fmt("bot=%d dwell_end=%d next=0x%02x top=%d "
                  "VHDL ym2149.vhd:444-461", b, i,
                  (i + 1 < (int)tr.size()) ? tr[i + 1] : 0xFFu,
                  first_of(tr, 0xFF)));
    }

    // AY-126 — C=1, H=0, Al=0 takes NO branch of the shape cascade
    // (ym2149.vhd:411-462: C=1 skips the first arm, H=0 the second, Al=0
    // the third), so env_inc and env_hold keep the values env_reset gave
    // them and the 5-bit counter simply wraps. Discriminator against
    // AY-125: the sample after the bottom is the TOP, with no dwell.
    {
        auto tr = env_trace(0x08, 200);            // shape 8: C=1 At=0 Al=0 H=0
        int  b  = first_of(tr, 0x00);
        bool wraps = (b >= 0) && (b + 1 < (int)tr.size()) && tr[b + 1] == 0xFF;
        check("AY-126", "C=1 H=0 Al=0 is a sawtooth: the counter WRAPS at "
              "the bottom straight back to the top with no dwell and no "
              "direction change",
              wraps,
              fmt("bot=%d next=0x%02x want=0xFF VHDL ym2149.vhd:411-462,403-410",
                  b, (b >= 0 && b + 1 < (int)tr.size()) ? tr[b + 1] : 0xFFu));
    }

    // AY-103 — an R13 write RELOADS the envelope from the Attack bit
    // wherever the ramp happens to be (ym2149.vhd:209-211 pulses env_reset,
    // :392-402 reloads env_vol / env_inc / env_hold). Run a descending ramp
    // well past the top, then re-write R13 with an At=1 shape: the level
    // must jump to the bottom and ascend; re-write an At=0 shape and it
    // must jump back to the top and descend.
    {
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x00);
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x08);   // At=0, descending
        for (int i = 0; i < 8 * 12; ++i) ay.tick();    // ~12 steps down
        const uint8_t mid = ay.output_a();
        ay.select_register(13); ay.write_data(0x0C);   // At=1 -> reload bottom
        uint8_t after_up = 0xFF;
        for (int s = 0; s < 3; ++s) {
            for (int t = 0; t < 8; ++t) ay.tick();
            if (s == 0) after_up = ay.output_a();
        }
        const uint8_t up_next = ay.output_a();
        ay.select_register(13); ay.write_data(0x08);   // At=0 -> reload top
        uint8_t after_dn = 0xFF;
        for (int t = 0; t < 8; ++t) ay.tick();
        after_dn = ay.output_a();
        check("AY-103", "R13 write reloads the envelope from the Attack bit "
              "mid-ramp: At=1 jumps to the bottom and ascends, At=0 jumps "
              "back to the top",
              mid != 0xFF && mid != 0x00 &&
              after_up <= kYm[1] && up_next > after_up &&
              after_dn >= kYm[30],
              fmt("mid=0x%02x after_up=0x%02x up_next=0x%02x after_dn=0x%02x "
                  "VHDL ym2149.vhd:209-211,392-402",
                  mid, after_up, up_next, after_dn));
    }

    // AY-128 — env_reset clears the PERIOD counter too, not just the
    // level: ym2149.vhd:340-342 sets `env_gen_cnt <= 0` and `env_ena <= '1'`
    // on the pulse. With a non-zero period the step interval is constant, so
    // an R13 write part-way through a period must restart a FULL interval
    // rather than finish the remainder of the one in flight.
    //
    // The interval that follows a write is measured from the SECOND change,
    // not the first: `env_ena <= '1'` makes the step right after the write
    // arrive early by construction (:341), and that first short gap is the
    // forced step, not the period.
    {
        auto gaps_after_shape_write = [](AyChip& ay, int n) {
            std::vector<int> g;
            uint8_t last = ay.output_a();
            int prev = -1;
            for (int t = 0; t < 8000 && (int)g.size() < n; ++t) {
                ay.tick();
                if (ay.output_a() != last) {
                    last = ay.output_a();
                    if (prev >= 0) g.push_back(t - prev);
                    prev = t;
                }
            }
            return g;
        };
        AyChip ay;
        ay.set_ay_mode(false);
        ay.select_register(7);  ay.write_data(0x3F);
        ay.select_register(8);  ay.write_data(0x10);
        ay.select_register(11); ay.write_data(0x04);   // period 4 -> comp 3
        ay.select_register(12); ay.write_data(0x00);
        ay.select_register(13); ay.write_data(0x08);   // free-running saw down
        auto g0 = gaps_after_shape_write(ay, 3);
        const int steady = g0.empty() ? -1 : g0.back();
        // Re-arm THREE QUARTERS of the way through the period now in
        // flight, so an un-reset counter would fire a quarter-period later
        // and a reset one a full period later — far enough apart that the
        // one-tick skew between the write and the reload edge cannot blur
        // the two.
        for (int k = 0; k < (steady * 3) / 4; ++k) ay.tick();
        ay.select_register(13); ay.write_data(0x08);   // env_reset pulse
        auto g1 = gaps_after_shape_write(ay, 3);
        // g1[0] is the gap between the level reload the write itself
        // causes and the FIRST period step after it — the only interval
        // that carries the counter-reset evidence. (An earlier draft
        // asserted the two gaps AFTER that one, which are full periods
        // whether or not the counter was cleared; deleting `env_gen_cnt
        // <= 0` from the emulator left it green.)
        check("AY-128", "R13 write resets the envelope PERIOD counter: the "
              "first step after a mid-period re-arm is a FULL period away, "
              "not the remainder of the one that was in flight",
              steady > 0 && !g1.empty() && g1[0] > (steady * 3) / 4,
              fmt("steady=%d first_gap_after_write=%d (want > %d) "
                  "VHDL ym2149.vhd:340-342",
                  steady, g1.empty() ? -1 : g1[0], (steady * 3) / 4));
    }
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

    // TS-17 — the register WRITE-ENABLE is routed to the selected AY only,
    // and its gate is NOT the address gate.
    //
    // turbosound.vhd:141-150:
    //     psg_addr <= '1' when psg_reg_addr_i = '1'
    //                      and psg_d_i(7 downto 5) = "000" else '0';   (:141)
    //     psg0_addr <= '1' when ay_select = "11" and psg_addr = '1' ... (:143)
    //     psg0_we   <= '1' when ay_select = "11"
    //                       and psg_reg_wr_i = '1' else '0';           (:144)
    // `psgN_we` carries NO data-pattern condition — only ay_select. TS-16
    // pins the ADDRESS gate; this row pins the WRITE gate, using the very
    // asymmetry that separates them: the select byte 0xFE has bits 7:5 =
    // "111", so `psg_addr` is 0 while it is on the bus and NO PSG's address
    // latch moves. Each PSG therefore keeps its own, different latched
    // register across the selection change, and the data write that follows
    // must land in the newly selected PSG's own register while leaving every
    // other PSG's register file untouched.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(false);

        ts.reg_addr(0xFF);              // select PSG0 (bits 7:5 = 111: no addr latch)
        ts.reg_addr(0x02);              // PSG0 addr latch = 2
        ts.reg_write(0x11);             // PSG0 R2 = 0x11

        ts.reg_addr(0xFE);              // select PSG1 — again no addr latch
        ts.reg_addr(0x03);              // PSG1 addr latch = 3
        ts.reg_write(0x22);             // PSG1 R3 = 0x22 (psg0_we must be 0)

        ts.reg_addr(0xFD);              // select PSG2
        ts.reg_addr(0x02);              // PSG2 addr latch = 2 (same reg as PSG0)
        ts.reg_write(0x33);             // PSG2 R2 = 0x33 (psg0_we must be 0)

        // PSG0's address latch never moved, so re-selecting it reads R2 back.
        ts.reg_addr(0xFF);
        const uint8_t psg0_r2 = ts.reg_read();
        ts.reg_addr(0xFE);
        const uint8_t psg1_r3 = ts.reg_read();
        ts.reg_addr(0xFD);
        const uint8_t psg2_r2 = ts.reg_read();
        // PSG0's R3 must still be 0 — the 0x22 write went to PSG1 alone.
        ts.reg_addr(0xFF);
        ts.reg_addr(0x03);
        const uint8_t psg0_r3 = ts.reg_read();

        check("TS-17",
              "psgN_we follows ay_select alone: each PSG keeps its own "
              "register contents and its own address latch across selection "
              "changes; a write never leaks into an unselected PSG",
              psg0_r2 == 0x11 && psg1_r3 == 0x22 && psg2_r2 == 0x33 &&
              psg0_r3 == 0x00,
              fmt("psg0_r2=0x%02x psg1_r3=0x%02x psg2_r2=0x%02x "
                  "psg0_r3=0x%02x VHDL turbosound.vhd:141,143-150",
                  psg0_r2, psg1_r3, psg2_r2, psg0_r3));
    }

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

    // TS-24 - the single global stereo_mode bit governs ALL three PSG
    // panners simultaneously (VHDL: stereo_mode_i feeds psg0_L_mux line
    // 186, psg1_L_mux line 241, psg2_L_mux line 296). Setup: activate
    // all three PSGs with DISTINCT B values, A=C=0, mixer off. In ABC
    // mode L_mux=B so every PSG contributes B_i to L; in ACB mode
    // L_mux=C=0 so every PSG contributes 0 to L. If only one (or two)
    // PSG's panner responded to the mode bit, the ACB total would still
    // contain the residual B contributions from the non-responding
    // PSG(s); it must collapse to exactly 0 to prove all three responded.
    {
        TurboSound ts_abc;
        ts_abc.set_enabled(true);
        ts_abc.set_ay_mode(true);
        ts_abc.set_stereo_mode(false);  // ABC
        // PSG0: B = 0x0F (vol_ay[15] = 0xFF contribution)
        ts_abc.reg_addr(0xFF);
        ts_abc.reg_addr(7);  ts_abc.reg_write(0x3F);
        ts_abc.reg_addr(8);  ts_abc.reg_write(0x00);
        ts_abc.reg_addr(9);  ts_abc.reg_write(0x0F);
        ts_abc.reg_addr(10); ts_abc.reg_write(0x00);
        // PSG1: B = 0x09 (vol_ay[9] = 0x41)
        ts_abc.reg_addr(0xFE);
        ts_abc.reg_addr(7);  ts_abc.reg_write(0x3F);
        ts_abc.reg_addr(8);  ts_abc.reg_write(0x00);
        ts_abc.reg_addr(9);  ts_abc.reg_write(0x09);
        ts_abc.reg_addr(10); ts_abc.reg_write(0x00);
        // PSG2: B = 0x05 (vol_ay[5] = 0x0F)
        ts_abc.reg_addr(0xFD);
        ts_abc.reg_addr(7);  ts_abc.reg_write(0x3F);
        ts_abc.reg_addr(8);  ts_abc.reg_write(0x00);
        ts_abc.reg_addr(9);  ts_abc.reg_write(0x05);
        ts_abc.reg_addr(10); ts_abc.reg_write(0x00);
        settle(ts_abc);
        uint16_t L_abc = ts_abc.pcm_left();

        TurboSound ts_acb;
        ts_acb.set_enabled(true);
        ts_acb.set_ay_mode(true);
        ts_acb.set_stereo_mode(true);  // ACB — L_mux swaps from B to C
        ts_acb.reg_addr(0xFF);
        ts_acb.reg_addr(7);  ts_acb.reg_write(0x3F);
        ts_acb.reg_addr(8);  ts_acb.reg_write(0x00);
        ts_acb.reg_addr(9);  ts_acb.reg_write(0x0F);
        ts_acb.reg_addr(10); ts_acb.reg_write(0x00);
        ts_acb.reg_addr(0xFE);
        ts_acb.reg_addr(7);  ts_acb.reg_write(0x3F);
        ts_acb.reg_addr(8);  ts_acb.reg_write(0x00);
        ts_acb.reg_addr(9);  ts_acb.reg_write(0x09);
        ts_acb.reg_addr(10); ts_acb.reg_write(0x00);
        ts_acb.reg_addr(0xFD);
        ts_acb.reg_addr(7);  ts_acb.reg_write(0x3F);
        ts_acb.reg_addr(8);  ts_acb.reg_write(0x00);
        ts_acb.reg_addr(9);  ts_acb.reg_write(0x05);
        ts_acb.reg_addr(10); ts_acb.reg_write(0x00);
        settle(ts_acb);
        uint16_t L_acb = ts_acb.pcm_left();

        // ABC: L = B0+B1+B2 > 0. ACB: L = C0+C1+C2 = 0. A per-PSG
        // non-response would leave one of {0xFF, 0x41, 0x0F} on ACB L.
        check("TS-24", "global stereo_mode flips L_mux on all 3 PSGs",
              L_abc > 0 && L_acb == 0,
              fmt("L_abc=%u L_acb=%u (must be 0) VHDL turbosound.vhd:186,241,296",
                  L_abc, L_acb));
    }
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

    // TS-32/33/34 - per-PSG zero gating is observable via a
    // distinct-amplitude DROP test. VHDL turbosound.vhd:197/252/307 each
    // zeroes its own PSG's L/R output when ts disabled AND that PSG not
    // selected. Aggregate TS-30/31 can't distinguish which PSG contributes.
    // Strategy: enable all three PSGs (ts enabled) with DISTINCT channel-A
    // amplitudes (A0=0xFF, A1=0x41, A2=0x0F via vol_ay lookup), B=C=0,
    // mixer off. Record baseline L_all. Then silence ONE PSG at a time by
    // setting its R8=0, and assert the aggregate L drops by EXACTLY that
    // PSG's contribution — proves isolation and identifies which gate is
    // the one being exercised.
    {
        auto setup_all = [](TurboSound& ts) {
            ts.set_enabled(true);
            ts.set_ay_mode(true);
            ts.set_stereo_mode(false);
            // PSG0: A=0x0F -> out_a = vol_ay[15] = 0xFF
            ts.reg_addr(0xFF);
            ts.reg_addr(7);  ts.reg_write(0x3F);
            ts.reg_addr(8);  ts.reg_write(0x0F);
            ts.reg_addr(9);  ts.reg_write(0x00);
            ts.reg_addr(10); ts.reg_write(0x00);
            // PSG1: A=0x09 -> out_a = vol_ay[9] = 0x41
            ts.reg_addr(0xFE);
            ts.reg_addr(7);  ts.reg_write(0x3F);
            ts.reg_addr(8);  ts.reg_write(0x09);
            ts.reg_addr(9);  ts.reg_write(0x00);
            ts.reg_addr(10); ts.reg_write(0x00);
            // PSG2: A=0x05 -> out_a = vol_ay[5] = 0x0F
            ts.reg_addr(0xFD);
            ts.reg_addr(7);  ts.reg_write(0x3F);
            ts.reg_addr(8);  ts.reg_write(0x05);
            ts.reg_addr(9);  ts.reg_write(0x00);
            ts.reg_addr(10); ts.reg_write(0x00);
        };

        // Baseline: all three PSGs active, distinct contributions.
        TurboSound ts_all;
        setup_all(ts_all);
        settle(ts_all);
        uint16_t L_all = ts_all.pcm_left();

        // TS-32: silence PSG0 only (R8=0), keep PSG1+PSG2. Expected drop
        // = 0xFF (PSG0's A contribution).
        TurboSound ts_no0;
        setup_all(ts_no0);
        ts_no0.reg_addr(0xFF);
        ts_no0.reg_addr(8); ts_no0.reg_write(0x00);
        settle(ts_no0);
        uint16_t L_no0 = ts_no0.pcm_left();
        check("TS-32", "PSG0 silenced: aggregate L drops by PSG0's 0xFF",
              L_all > L_no0 && (L_all - L_no0) == 0xFF,
              fmt("L_all=%u L_no_psg0=%u drop=%u (expect 0xFF=255) "
                  "VHDL turbosound.vhd:197",
                  L_all, L_no0, L_all - L_no0));

        // TS-33: silence PSG1 only. Expected drop = 0x41 (PSG1's A).
        TurboSound ts_no1;
        setup_all(ts_no1);
        ts_no1.reg_addr(0xFE);
        ts_no1.reg_addr(8); ts_no1.reg_write(0x00);
        settle(ts_no1);
        uint16_t L_no1 = ts_no1.pcm_left();
        check("TS-33", "PSG1 silenced: aggregate L drops by PSG1's 0x41",
              L_all > L_no1 && (L_all - L_no1) == 0x41,
              fmt("L_all=%u L_no_psg1=%u drop=%u (expect 0x41=65) "
                  "VHDL turbosound.vhd:252",
                  L_all, L_no1, L_all - L_no1));

        // TS-34: silence PSG2 only. Expected drop = 0x0F (PSG2's A).
        TurboSound ts_no2;
        setup_all(ts_no2);
        ts_no2.reg_addr(0xFD);
        ts_no2.reg_addr(8); ts_no2.reg_write(0x00);
        settle(ts_no2);
        uint16_t L_no2 = ts_no2.pcm_left();
        check("TS-34", "PSG2 silenced: aggregate L drops by PSG2's 0x0F",
              L_all > L_no2 && (L_all - L_no2) == 0x0F,
              fmt("L_all=%u L_no_psg2=%u drop=%u (expect 0x0F=15) "
                  "VHDL turbosound.vhd:307",
                  L_all, L_no2, L_all - L_no2));
    }

    // TS-60 — turbosound.vhd:118-138: synchronous reset clause clears
    //   ay_select <= "11"; psg{0,1,2}_pan <= "11";
    // and nothing else. enabled / stereo_mode / mono_mode are external
    // ports (turbosound_en_i, stereo_mode_i, mono_mode_i) supplied by
    // NR 0x08 / NR 0x09 — the audio_ay_reset pulse (NR 0x06 psg_mode=11
    // toggle, zxnext.vhd:6379) must NOT clobber them.
    //
    // G115 closure: TurboSound::reset_ay_only() splits the partial reset
    // out of the full power-on reset(). This row exercises the
    // turbosound_en preservation (NR 0x08 b1 → set_enabled).
    // Reserved range: TS-35..59 left unused for future expansion.
    {
        TurboSound ts;
        ts.set_enabled(true);                   // NR 0x08 b1 = 1
        ts.set_stereo_mode(true);               // NR 0x08 b5 = 1 (ACB)
        ts.set_mono_mode(0x07);                 // NR 0x09 b7:5 = 111
        // Mutate the synchronously-reset fields so the partial reset has
        // visible work to do: select PSG1 + change all three pans.
        ts.reg_addr(0xBE);                      // PSG1 select, pan="01"
        ts.reg_addr(0x9E);                      // PSG1 pan="00" (re-set)
        ts.reg_addr(0xBD);                      // PSG2 select, pan="01"

        // Audio AY reset (NR 0x06 psg_mode=11 path).
        ts.reset_ay_only();

        check("TS-60",
              "reset_ay_only preserves NR-driven enabled/stereo/mono "
              "AND clears ay_select+pan",
              ts.enabled() == true
                  && ts.stereo_mode() == true
                  && ts.mono_mode() == 0x07,
              fmt("enabled=%d stereo=%d mono=0x%02x "
                  "VHDL turbosound.vhd:118-138 (NR-driven inputs survive)",
                  ts.enabled() ? 1 : 0,
                  ts.stereo_mode() ? 1 : 0,
                  ts.mono_mode()));
    }

    // TS-61 — zxnext.vhd:6379 — `audio_ay_reset <= '1' when reset='1' or
    //   nr_06_psg_mode = "11" else '0'`. The G115 split puts a
    //   reset_ay_only() at the NR 0x06 psg_mode=11 callsite (see
    //   src/core/emulator.cpp NR 0x06 handler). This row pins the NR 0x09
    //   mono_mode preservation specifically (TS-60 covers the en + stereo
    //   bits). Re-derive: setting only mono_mode_=0x05 (PSG0+PSG2 mono)
    //   then issuing the partial reset must leave mono_mode_ untouched.
    {
        TurboSound ts;
        ts.set_mono_mode(0x05);                 // NR 0x09 b5+b7 (PSG0,PSG2)
        // Touch the synchronously-reset fields so the partial reset has
        // observable work to do — pan + ay_select must clear back to
        // "11" while mono_mode survives.
        ts.set_enabled(true);
        ts.reg_addr(0xBE);                      // pan/select churn
        ts.reset_ay_only();
        check("TS-61",
              "reset_ay_only preserves NR 0x09 mono_mode (per-PSG triplet)",
              ts.mono_mode() == 0x05,
              fmt("mono_mode=0x%02x (want 0x05) "
                  "VHDL turbosound.vhd:118-138 (mono_mode_i survives reset)",
                  ts.mono_mode()));
    }
}

static void g_ts_panning() {
    set_group("TS-panning");

    // TS-40 — pan "11" routes the PSG to BOTH L and R.
    //
    // turbosound.vhd:323-329 gates each side independently:
    //     psg0_L_pan <= psg0_L when psg0_pan(1) = '1' else (others => '0');
    //     psg0_R_pan <= psg0_R when psg0_pan(0) = '1' else (others => '0');
    // TS-10 asserts that RESET leaves the pan at "11"; this row asserts what
    // the value "11" DOES, and that it is reached through the select-byte
    // write path (turbosound.vhd:129-134, psg_d_i(6 downto 5) -> psgN_pan)
    // rather than only as a power-on state. Starting from pan "00" (both
    // sides gated off) and writing "11" must open both gates.
    {
        TurboSound ts;
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.reg_addr(0x9F);      // PSG0 pan = 00, select PSG0
        ts.reg_addr(0x9E);      // PSG1 pan = 00
        ts.reg_addr(0x9D);      // PSG2 pan = 00
        ts.reg_addr(0x9F);      // re-select PSG0 (pan stays 00)
        ts.reg_addr(7);  ts.reg_write(0x3F);   // all tone/noise terms high
        ts.reg_addr(8);  ts.reg_write(0x0F);   // ch A vol 15
        ts.reg_addr(9);  ts.reg_write(0x0F);   // ch B vol 15
        ts.reg_addr(10); ts.reg_write(0x0F);   // ch C vol 15
        settle(ts);
        const uint16_t l_off = ts.pcm_left();
        const uint16_t r_off = ts.pcm_right();

        ts.reg_addr(0xFF);      // PSG0 pan = 11, select PSG0
        settle(ts);
        const uint16_t l_on = ts.pcm_left();
        const uint16_t r_on = ts.pcm_right();

        check("TS-40",
              "pan \"11\" opens both pan gates: L and R both carry the PSG "
              "(and pan \"00\" had silenced both)",
              l_off == 0 && r_off == 0 && l_on > 0 && r_on > 0,
              fmt("pan00 L=%u R=%u -> pan11 L=%u R=%u "
                  "VHDL turbosound.vhd:129-134,323,327",
                  l_off, r_off, l_on, r_on));
    }

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

    // AUD-SD-02 - chA write.
    {
        Dac dac;
        dac.write_channel(0, 0xFF);
        check("AUD-SD-02", "write channel A latches value",
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

    // SD-09 — RETIRED 2026-09-24 (GH #201), was the G31 WONT.
    //
    // soundrive.vhd:80-84 is an if/elsif inside ONE clocked process:
    //     if chA_wr_i = '1' then chA <= cpu_d_i;
    //     elsif nr_mono_we_i = '1' then chA <= nr_audio_dat_i;
    // so the row only has content when both strobes are high on the SAME
    // 28 MHz edge. A CPU cycle drives exactly one `iowr`, so the CPU cannot
    // produce both: the only hardware source of a genuine collision is the
    // Copper writing NR 0x2D in the same cycle as a CPU OUT to a Soundrive
    // port. jnext serialises CPU and Copper NextREG writes — they do not
    // share a bus — and modelling their per-cycle arbitration is the
    // project-level WONT recorded in doc/design/EMULATOR-DESIGN-PLAN.md
    // Phase 11 ("Model cycle-accurate CPU/Copper NR write priority",
    // resolved as option (a): priority stays a test-harness convention,
    // documented as a known modelling limitation). The same decision is why
    // Copper ARB-01/02/03 order their stimulus by hand.
    //
    // This row is therefore a scope decision, not a coverage gap: the
    // simultaneity it needs cannot arise in jnext's execution model at all.
    // SD-02..SD-08 cover both write paths individually. Struck in
    // AUDIO-TEST-PLAN-DESIGN.md §3.1; no check() row exists.

    // RE-HOME: SD-10 — Soundrive mode 1 port decode moved to
    //   test/audio/audio_port_dispatch_test.cpp (F-skip: 0x5F unwired).
    // RE-HOME: SD-11 — Soundrive mode 2 port decode moved to
    //   test/audio/audio_port_dispatch_test.cpp.
    // RE-HOME: AUD-SD-12 (was SD-12) — Profi Covox port decode moved to
    //   test/audio/audio_port_dispatch_test.cpp (F-skip: 0x3F unwired).
    // RE-HOME: AUD-SD-13 (was SD-13) — Covox port decode moved to
    //   test/audio/audio_port_dispatch_test.cpp.
    // RE-HOME: AUD-SD-14 (was SD-14) — Pentagon/ATM mono port moved to
    //   test/audio/audio_port_dispatch_test.cpp (F-skip: 0xFB fan-out gap).
    // RE-HOME: AUD-SD-15 (was SD-15) — GS Covox port moved to
    //   test/audio/audio_port_dispatch_test.cpp (F-skip: 0xB3 unwired).
    // RE-HOME: AUD-SD-16 (was SD-16) — SpecDrum port 0xDF moved to
    //   test/audio/audio_port_dispatch_test.cpp.
    // RE-HOME: AUD-SD-17 (was SD-17) — nr_08_dac_en gating (zxnext.vhd:6436) requires the
    // Emulator + NextReg + Soundrive port-dispatch surface. Re-homed to
    // test/audio/audio_nextreg_test.cpp (2026-04-24 Wave C).
    // RE-HOME: AUD-SD-18 (was SD-18) — mono-port aliasing moved to
    //   test/audio/audio_port_dispatch_test.cpp.
    // NOTE (GH #196 dedup): SD-12..SD-19 and SD-02/20-23 in this section were
    // renamed to AUD-SD-* to resolve a global ID collision with unrelated
    // SD-card SPI-command tests in test/sdcard/sdcard_test.cpp. Pure rename.

    // AUD-SD-20 - pcm_L = chA + chB.
    {
        Dac dac;
        dac.write_channel(0, 0x10);
        dac.write_channel(1, 0x20);
        check("AUD-SD-20", "pcm_L = chA + chB",
              dac.pcm_left() == 0x30,
              fmt("L=0x%03x VHDL soundrive.vhd:112", dac.pcm_left()));
    }

    // AUD-SD-21 - pcm_R = chC + chD.
    {
        Dac dac;
        dac.write_channel(2, 0x30);
        dac.write_channel(3, 0x40);
        check("AUD-SD-21", "pcm_R = chC + chD",
              dac.pcm_right() == 0x70,
              fmt("R=0x%03x VHDL soundrive.vhd:113", dac.pcm_right()));
    }

    // AUD-SD-22 - max 0xFF+0xFF = 0x1FE.
    {
        Dac dac;
        dac.write_channel(0, 0xFF);
        dac.write_channel(1, 0xFF);
        check("AUD-SD-22", "max pcm_L = 0x1FE (9-bit)",
              dac.pcm_left() == 0x1FE,
              fmt("L=0x%03x VHDL soundrive.vhd:112", dac.pcm_left()));
    }

    // AUD-SD-23 - reset output 0x100.
    {
        Dac dac;
        check("AUD-SD-23", "reset output L=R=0x100",
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
    // RE-HOME: BP-01 — port 0xFE write into port_fe_reg[4:0] moved to
    //   test/audio/audio_port_dispatch_test.cpp (Emulator + port dispatch).
    // RE-HOME: BP-04 → test/input/input_integration_test.cpp group FE-READ
    // (Wave D 2026-04-24). The "border bits [2:0] not exposed on READ"
    // invariant is composed at the port-0xFE-read level, not in the Audio
    // subsystem; it's verified end-to-end via OUT 0xFE then IN 0xFE.
    // RE-HOME: BP-06 — port 0xFE A0=0 dispatch moved to
    //   test/audio/audio_port_dispatch_test.cpp.

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

    // RE-HOME: BP-10..BP-13 — XOR/gating at zxnext.vhd:6503-6504 compose
    // NR 0x06 bit 6 + NR 0x08 bits 0/4 + Beeper.ear/mic/tape_ear.
    // Re-homed to test/audio/audio_nextreg_test.cpp (2026-04-24 Wave C).

    // RE-HOME: BP-20..BP-23 → test/input/input_integration_test.cpp
    // group FE-READ (Wave D 2026-04-24). The port 0xFE READ composition
    // (EAR OR term, fixed-high bits 5 and 7, keyboard column mux) is
    // built on top of Keyboard::read_rows() at the Emulator wrapper
    // layer (src/core/emulator.cpp:1163-1185), not inside the Audio
    // subsystem. The Input integration suite exercises the full
    // end-to-end IN A,(0xFE) / OUT (0xFE),A path the real Z80 uses.
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

    // RE-HOME: AUD-MX-03 (was MX-03) — exc_i gating (zxnext.vhd:6504) composes
    // NR 0x06 b6 + NR 0x08 b4. Re-homed to test/audio/audio_nextreg_test.cpp
    // (2026-04-24 Wave C).

    // AUD-MX-04 - ay zero-extended 12->13 bit. Drive AY via TurboSound (max
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
        check("AUD-MX-04", "AY_L routed verbatim (signed = ay_L*4)",
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
    // to max (1023,1023), pcm_L = 0+0+0+0+1024+1023 = 2047 — the 13-bit
    // sum audio_mixer.vhd:89-90,99-100 specifies, and it is unchanged.
    //
    // The SIGNED value this row asserts changed from 4092 to 2044 with the
    // GH #116 fix, because the emulator's AC-coupling reference moved from
    // DAC_REST_LEVEL (1024) to MIX_REST_LEVEL (1536). 4092 encoded the same
    // defect this row's own subject exposes: it measured the I2S term's
    // full-scale excursion from 0, but 0 is that term's most-NEGATIVE value,
    // not its silence — i2s.vhd:179 emits offset binary, silence = 0x200.
    // Measured from silence the excursion is +511 (13-bit), i.e. 2044 signed,
    // and it is now symmetric: I2S = 0 gives -2048 (MX-07 below). Under the
    // old reference, silence itself read +2048 and only full scale read 0.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        I2s i2s;
        // NR 0xA2 enable required after G73 (Mixer reads gated
        // pi_audio_L/R per VHDL zxnext.vhd:2358-2359). Without enL/enR
        // the gate forces both channels to the 0x200 silence midpoint
        // and the raw set_sample(1023,1023) latch is not observable.
        // 0xC0 = enL=1 enR=1, muteL/R=0, ear=0.
        i2s.set_nr_a2_ctl(0xC0);
        i2s.set_sample(1023, 1023);
        mx.set_i2s_source(&i2s);
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-06", "I2S max (1023,1023) sums into L and R (10->13 zero-extend)",
              s[0] == 2044 && s[1] == 2044,
              fmt("L=%d R=%d VHDL audio_mixer.vhd:89-90,99-100", s[0], s[1]));
    }

    // MX-07 (GH #116) - the I2S term's silence is its OFFSET-BINARY MIDPOINT
    // 0x200, not zero. i2s.vhd:179 builds the 10-bit output as
    //   o_audio_pi_L <= (not audio_pi_L(12)) & audio_pi_L(11 downto 3)
    // — the sign bit is INVERTED, so a signed-zero sample leaves the entity as
    // 0x200; and zxnext.vhd:2358-2359 substitutes that same 0x200 whenever the
    // input is disabled, muted or in EAR mode. So the excursion around silence
    // must be symmetric: full scale (1023) is +511 in the 13-bit domain and
    // zero (0) is -512, i.e. +2044 / -2048 signed after the x4 scale.
    //
    // BEFORE the fix this row read L=0 for a full-negative input (and MX-16
    // below read +2048 for silence): the mixer treated the term's most-negative
    // value as its rest point, offsetting every sample of every run by +2048.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        I2s i2s;
        i2s.set_nr_a2_ctl(0xC0);        // enL=1 enR=1 — same gate as MX-06
        i2s.set_sample(0, 0);           // full-negative excursion
        mx.set_i2s_source(&i2s);
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-07", "I2S min (0,0) is a full-NEGATIVE excursion about the "
              "0x200 midpoint, not silence",
              s[0] == -2048 && s[1] == -2048,
              fmt("L=%d R=%d VHDL i2s.vhd:179, zxnext.vhd:2358-2359, "
                  "audio_mixer.vhd:89-90", s[0], s[1]));
    }

    // MX-30 — RETIRED 2026-09-24 (GH #201), was the G29 WONT.
    //
    // The row asks for a Pi I2S SOURCE delivering a continuous 10-bit
    // stream. jnext models no such source and, by project scope decision,
    // never will:
    //   * doc/design/EMULATOR-DESIGN-PLAN.md §3.1 lists `audio/i2s*.vhd`
    //     with scope "no" — "I2S; SDL audio queue used instead".
    //   * the same plan's Phase 5 records Pi GPIO (NR 0x90-0xA9) as
    //     "intentionally stubbed (cached only); no emulation effect".
    //   * src/audio/i2s.h:10-13 states the class is "a pure latched
    //     sample-pair register — no real I2S wire / clocking / protocol
    //     emulation", and nothing in src/ ever calls I2s::set_sample().
    // There is no Raspberry Pi in the emulated machine to be the producer,
    // so this is an absent SUBSYSTEM, not an untested behaviour.
    //
    // What jnext does model — the mixer's consumption of the 10-bit input
    // and its NR 0xA2 gating — stays covered by MX-06 (zero-extension into
    // the 13-bit sum) and MX-07 (the offset-binary midpoint, GH #116).
    // Struck in AUDIO-TEST-PLAN-DESIGN.md §5.1; no check() row exists.

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

    // MX-16 (GH #116) - silence with the Pi I2S input WIRED and at its
    // power-on state (NR 0xA2 = 0 → disabled → zxnext.vhd:2358-2359 forces
    // pi_audio_L/R = 0x200). This is the configuration EVERY real run is in:
    // Emulator's constructor wires the I2s unconditionally
    // (src/core/emulator.cpp:44) and no ZX software touches NR 0xA2.
    //
    // MX-10/MX-11 above construct a Mixer with NO I2s, so they asserted
    // silence on a configuration the emulator never has. That is how the
    // defect survived: the suite's silence and the product's silence were
    // different signals, and the product's was 2048 counts off. Measured on
    // a 20 s NextZXOS boot before the fix, EVERY one of 1 117 694 recorded
    // samples was exactly 2048 — never 0.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        I2s i2s;                       // power-on: nr_a2_ctl = 0 ⇒ gate closed
        mx.set_i2s_source(&i2s);
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-16", "silence with the Pi I2S input wired and idle is digital "
              "ZERO, not its 0x200 midpoint",
              s[0] == 0 && s[1] == 0,
              fmt("L=%d R=%d VHDL zxnext.vhd:2358-2359, i2s.vhd:179, "
                  "audio_mixer.vhd:89-90", s[0], s[1]));
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

    // MX-15 — the mixer does not saturate at full scale.
    //
    // audio_mixer.vhd:99-100 is a plain 13-bit addition with no clamp:
    //     pcm_L <= ear + mic + ay_L + dac_L + i2s_L;   -- 0 - 5998
    // and the VHDL's own comment gives the maximum. Every term at its
    // documented ceiling (audio_mixer.vhd:80-89) sums to
    //     512 + 128 + 2295 + 2040 + 1023 = 5998
    // which still fits the 13-bit signal (0..8191), so no term can be lost.
    // MX-05 and MX-14 assert sub-maximal sums; only driving ALL FIVE terms
    // to their ceiling at once tests the headroom claim, and it is the only
    // row that does. Reaching ay = 2295 needs all three PSGs in mono mode
    // (L = A+B+C = 765 each, turbosound.vhd:186-205).
    //
    // Observed through the suite's usual signed transform: the resting DC
    // (DAC 1024 + I2S 512 = 1536) is subtracted and the result scaled x4,
    // so the expected sample is 4 * (5998 - 1536) = 17848. A saturating
    // mixer would report less.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx; I2s i2s;
        mx.set_i2s_source(&i2s);
        bp.set_ear(true);                      // 512  (audio_mixer.vhd:63,80)
        bp.set_mic(true);                      // 128  (:64,81)
        dac.write_channel(0, 0xFF);            // dac_L = 0x1FE << 2 = 2040
        dac.write_channel(1, 0xFF);            //       (:86)
        dac.write_channel(2, 0xFF);            // same for dac_R (:87)
        dac.write_channel(3, 0xFF);
        i2s.set_nr_a2_ctl(0xC0);               // enL=1 enR=1 (zxnext.vhd:2358)
        i2s.set_sample(1023, 1023);            // i2s = 1023 (:89-90)
        ts.set_enabled(true);
        ts.set_ay_mode(true);
        ts.set_mono_mode(0x07);                // all three PSGs mono
        for (uint8_t sel : {0xFFu, 0xFEu, 0xFDu}) {
            ts.reg_addr(sel);                  // select + pan "11"
            ts.reg_addr(7);  ts.reg_write(0x3F);
            ts.reg_addr(8);  ts.reg_write(0x0F);
            ts.reg_addr(9);  ts.reg_write(0x0F);
            ts.reg_addr(10); ts.reg_write(0x0F);
        }
        settle(ts, 64);
        const uint16_t ay_l = ts.pcm_left();
        const uint16_t ay_r = ts.pcm_right();
        mx.generate_sample(bp, ts, dac);
        int16_t s[2];
        mx.read_samples(s, 1);
        check("MX-15",
              "full-scale mix does not saturate: 512+128+2295+2040+1023 "
              "= 5998 arrives intact on both channels",
              ay_l == 2295 && ay_r == 2295 && s[0] == 17848 && s[1] == 17848,
              fmt("ay_L=%u ay_R=%u L=%d R=%d (want 2295/2295/17848/17848) "
                  "VHDL audio_mixer.vhd:63-64,80-89,99-100",
                  ay_l, ay_r, s[0], s[1]));
    }
    // RE-HOME: MX-20 — exc_i silencing of EAR/MIC (zxnext.vhd:6504).
    // Re-homed to test/audio/audio_nextreg_test.cpp (2026-04-24 Wave C).
    // MX-21 — with exc_i = 0 the EAR and MIC muxes pass their full volumes.
    //
    // audio_mixer.vhd:80-81:
    //     ear <= ear_volume when (ear_i = '1' and exc_i = '0') else 0;
    //     mic <= mic_volume when (mic_i = '1' and exc_i = '0') else 0;
    // These are COMBINATIONAL muxes, so exc_i returning to '0' must reopen
    // them on the spot. MX-01/MX-02 assert the two volumes on a freshly
    // constructed Mixer, i.e. under the power-on default — they cannot tell
    // a combinational gate from a latched power-on value. MX-20 and MX-23
    // (audio_nextreg_test) cover the exc_i = 1 side and a single-source
    // delta. This row drives exc_i 1 -> 0 on one Mixer and asserts BOTH
    // terms come back with their exact weights, together and separately:
    //     EAR only  -> 512 * 4 = 2048
    //     MIC only  -> 128 * 4 =  512
    //     EAR + MIC -> 640 * 4 = 2560
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        auto sample = [&](bool ear, bool mic) {
            bp.set_ear(ear);
            bp.set_mic(mic);
            mx.generate_sample(bp, ts, dac);
            int16_t s[2] = {0, 0};
            mx.read_samples(s, 1);
            return static_cast<int>(s[0]);
        };
        // Gate shut first, so the reopening is what is measured.
        mx.set_exc_i(true);
        const int shut = sample(true, true);
        mx.set_exc_i(false);
        const int base     = sample(false, false);
        const int ear_only = sample(true,  false);
        const int mic_only = sample(false, true);
        const int both     = sample(true,  true);
        check("MX-21",
              "exc_i=0 reopens both beeper muxes combinationally: EAR "
              "contributes 512, MIC 128, together 640 (x4 into int16)",
              shut == base &&
              ear_only - base == 512 * 4 &&
              mic_only - base == 128 * 4 &&
              both     - base == 640 * 4,
              fmt("shut=%d base=%d ear=%d mic=%d both=%d "
                  "VHDL audio_mixer.vhd:63-64,80-81",
                  shut, base, ear_only, mic_only, both));
    }
    // RE-HOME: MX-22 — exc_i derivation (zxnext.vhd:6504). Re-homed to
    // test/audio/audio_nextreg_test.cpp (2026-04-24 Wave C).

    // -----------------------------------------------------------------
    // Output band-limiting (MX-BL-*).
    //
    // NOTE: unlike every other row in this file these have NO VHDL oracle.
    // They test the emulator's *output stage* — how the VHDL mixer's level is
    // resampled down to 44100 Hz. That is a host DSP concern, not hardware: the
    // FPGA has no sample rate. jnext does, and choosing samples badly is audible.
    //
    // The defect (2026-07-11, Task 23 follow-up): the emulator point-sampled the
    // mixer — it asked "what is the level right now?" once per output sample.
    // Output samples are ~635 master cycles apart and a 1-bit beeper engine
    // toggles the speaker far faster than that, so most toggles were never
    // observed at all. Their energy does not vanish: it aliases down into the
    // audible band as an inharmonic tone riding over the music. Measured on the
    // Cesare intro, point-sampling put 6.2% of all energy above 6 kHz, against
    // 0.5% for FUSE — which integrates the level across each sample period, as
    // Mixer::accumulate()/emit_sample() now do.
    // -----------------------------------------------------------------

    // MX-BL-01 — emit_sample() is the time-weighted average of the interval, not
    // the last level seen. EAR high for 1/4 of it, low for 3/4.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        bp.set_ear(true);
        mx.accumulate(bp, ts, dac, 100);   // EAR high for 100 master cycles
        bp.set_ear(false);
        mx.accumulate(bp, ts, dac, 300);   // and low for 300
        mx.emit_sample();
        int16_t s[2];
        mx.read_samples(s, 1);
        // EAR alone reads 2048 at the output (MX-01); a 25% duty cycle is 512.
        check("MX-BL-01", "emit_sample = time-weighted average of the interval",
              s[0] == 512 && s[1] == 512,
              fmt("L=%d R=%d expected 512 (25%% duty of MX-01's 2048)", s[0], s[1]));
    }

    // MX-BL-02 — the discriminative row. A beeper toggling FASTER than the output
    // sample rate must average out rather than alias: EAR flips every 50 master
    // cycles across a 635-cycle sample (~22 kHz, far above what 44.1 kHz can
    // represent), so every emitted sample must sit near the 50%-duty midpoint.
    // Point-sampling instead latches whichever level happened to be live at the
    // sample instant and swings the full 0..2048 — that IS the aliasing. This row
    // fails loudly if the integration is ever removed.
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        bool ear = false;
        int16_t lo = 32767, hi = -32768;
        for (int sample = 0; sample < 64; sample++) {
            for (int c = 0; c < 635; c += 50) {
                ear = !ear;
                bp.set_ear(ear);
                mx.accumulate(bp, ts, dac, std::min(50, 635 - c));
            }
            mx.emit_sample();
            int16_t s[2];
            if (mx.read_samples(s, 1) == 1) {
                if (s[0] < lo) lo = s[0];
                if (s[0] > hi) hi = s[0];
            }
        }
        // 50% duty of 2048 = 1024; integration holds every sample near it.
        const int pp = hi - lo;
        check("MX-BL-02", "a supersonic beeper averages out instead of aliasing",
              lo > 700 && hi < 1350 && pp < 500,
              fmt("min=%d max=%d peak-to-peak=%d (point-sampling gives ~2048)",
                  lo, hi, pp));
    }

    // MX-BL-03 — emitting with nothing accumulated invents no sample (emitting
    // silence would punch a hole in the stream, i.e. a click).
    {
        Beeper bp; TurboSound ts; Dac dac; Mixer mx;
        bp.set_ear(true);
        mx.accumulate(bp, ts, dac, 0);
        mx.emit_sample();
        check("MX-BL-03", "emit with nothing accumulated produces no sample",
              mx.available() == 0, fmt("available=%d", mx.available()));
    }

    // MX-BL-04 — generate_sample() (the point-sampling API every VHDL-transfer
    // row above uses) must stay exactly equivalent to accumulate(1)+emit_sample(),
    // so those rows keep testing the mixer's real transfer function.
    {
        Beeper bp1; TurboSound ts1; Dac dac1; Mixer mx1;
        Beeper bp2; TurboSound ts2; Dac dac2; Mixer mx2;
        bp1.set_ear(true); bp1.set_mic(true);
        bp2.set_ear(true); bp2.set_mic(true);
        mx1.generate_sample(bp1, ts1, dac1);
        mx2.accumulate(bp2, ts2, dac2, 1);
        mx2.emit_sample();
        int16_t a[2], b[2];
        mx1.read_samples(a, 1);
        mx2.read_samples(b, 1);
        check("MX-BL-04", "generate_sample == accumulate(1) + emit_sample",
              a[0] == b[0] && a[1] == b[1],
              fmt("generate=(%d,%d) accumulate=(%d,%d)", a[0], a[1], b[0], b[1]));
    }
}

// =====================================================================
// 6 NextREG Configuration
// =====================================================================

static void g_nextreg() {
    set_group("NextREG");

    // RE-HOME: AUD-NR-01..AUD-NR-06 (was NR-01..NR-06), AUD-NR-10, AUD-NR-11,
    // NR-12, AUD-NR-13, AUD-NR-14 (was NR-10..NR-14), NR-20/21, NR-30..32 —
    // the full NR 0x06 / 0x08 / 0x09 / 0x2C-0x2E handler surface requires the
    // Emulator + NextReg integration fixture. All 14 rows re-homed to
    // test/audio/audio_nextreg_test.cpp (2026-04-24 Wave C).
}

// =====================================================================
// 7 I/O port wiring
// =====================================================================

static void g_io() {
    set_group("IO");

    // RE-HOME: IO-01 — port FFFD AY register select moved to
    //   test/audio/audio_port_dispatch_test.cpp (Emulator + port dispatch).
    // RE-HOME: IO-02 — port BFFD AY data write moved to
    //   test/audio/audio_port_dispatch_test.cpp.
    // RE-HOME: IO-03 — port BFF5 AY reg-query mode moved to
    //   test/audio/audio_port_dispatch_test.cpp (F-skip: unmodelled).
    // RE-HOME: IO-04 — FFFD falling-edge latch moved to
    //   test/audio/audio_port_dispatch_test.cpp (F-skip: not modelled).
    // RE-HOME: IO-05 — BFFD/+3 timing alias moved to
    //   test/audio/audio_port_dispatch_test.cpp (F-skip: no handler).

    // RE-HOME: IO-10 — dac_hw_en gate (zxnext.vhd:2775-2778/6436) — needs
    // the port-dispatch + NextReg surface. Re-homed to
    // test/audio/audio_nextreg_test.cpp (2026-04-24 Wave C).
    // RE-HOME: IO-11 — port→channel alias fan-in moved to
    //   test/audio/audio_port_dispatch_test.cpp.
    // RE-HOME: IO-12 — port FD F1/F9 AY-conflict guard moved to
    //   test/audio/audio_port_dispatch_test.cpp.
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
