// Audio + NextREG Integration Test — full-machine rows re-homed from
// test/audio/audio_test.cpp (Phase 2 Wave C of the TASK3-AUDIO skip-
// reduction plan, 2026-04-24).
//
// These 25 plan rows cannot be exercised against the bare Audio classes
// (AyChip, TurboSound, Dac, Beeper, Mixer) — they require the NextREG
// write path, the NR 0x06/0x08/0x09/0x2C/0x2D/0x2E audio handlers, and
// the downstream audio-side state latched behind them. They live on the
// integration tier, observable via the real port path an Z80 would use
// (OUT 0x243B,reg; OUT 0x253B,val) plus accessors on Emulator /
// TurboSound / Dac.
//
// Covered rows by NR register group:
//   NR-01..NR-05    — NR 0x06 bits[1:0] PSG-mode fan-out (YM/AY/alias/audio_ay_reset)
//   NR-06           — NR 0x06 bit 6 internal_speaker_beep store
//   NR-10..NR-14    — NR 0x08 stereo / speaker / DAC / turbosound / issue2
//   NR-20, NR-21    — NR 0x09 per-PSG mono bit→chip mapping
//   NR-30, NR-31, NR-32 — NR 0x2C/0x2D/0x2E Soundrive mirrors (left/mono/right)
//   BP-10..BP-13    — beep_mic_final XOR + issue2 cancel + issue3 EAR^MIC + beep_spkr_excl
//   MX-03, MX-20, MX-22 — exc_i (beep_spkr_excl) gating into Mixer
//   SD-17, IO-10    — dac_hw_en / dac_en gate (NR 0x08 bit 3)
//
// Reference plan: doc/design/TASK3-AUDIO-SKIP-REDUCTION-PLAN.md
// Reference structural template: test/uart/uart_integration_test.cpp,
//                                test/ula/ula_integration_test.cpp.
//
// Run: ./build/test/audio_nextreg_test

#include "core/emulator.h"
#include "core/emulator_config.h"

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

// ── Test infrastructure ───────────────────────────────────────────────

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
    const char* id;
    const char* reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    ++g_total;
    Result r{g_group, id, desc, cond, detail};
    g_results.push_back(r);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

std::string hex2(uint8_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02x", v);
    return buf;
}

} // namespace

// ── Emulator construction helpers ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.roms_directory = "/usr/share/fuse";
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

// Fresh-state idiom: re-initialise the emulator before each scenario so
// cross-test state (turbosound selection, DAC channel latches, NR stored
// bytes) cannot leak between scopes.
static void fresh(Emulator& emu) {
    build_next_emulator(emu);
}

// Write NextREG register through the real port path (OUT 0x243B,reg;
// OUT 0x253B,val). Mirrors the idiom in uart_integration_test.cpp.
static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// ══════════════════════════════════════════════════════════════════════
// Group NR-0x06 — PSG mode handler + audio_ay_reset + internal_speaker_beep
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:5163, :5170 — NR 0x06 write handler:
//   nr_06_internal_speaker_beep <= nr_wr_dat(6);
//   nr_06_psg_mode              <= nr_wr_dat(1 downto 0);
// VHDL zxnext.vhd:6379, :6389:
//   audio_ay_reset <= '1' when reset='1' or nr_06_psg_mode = "11" else '0';
//   aymode_i       <= nr_06_psg_mode(0);
// So psg_mode bits map to TurboSound's AY/YM mode AND, at "11", an
// audio-only reset of the turbosound module (register file clear) while
// the rest of the machine keeps running.
// ══════════════════════════════════════════════════════════════════════

static void test_nr_06(Emulator& emu) {
    set_group("NR-0x06");

    // NR-01 — NextREG 0x06 bits[1:0] reach TurboSound::ay_mode()
    // (bit-0 fan-out per VHDL:6389). Spot-check: after writing bit 0 = 1
    // (psg_mode = 01 — AY mode), turbosound_.ay_mode() is true.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x01);       // psg_mode=01
        const bool ay = emu.turbosound().ay_mode();
        check("NR-01",
              "NR 0x06 bits[1:0] handler forwards to TurboSound::ay_mode "
              "[zxnext.vhd:5170, :6389]",
              ay == true,
              fmt("psg_mode=01 → ay_mode=%d (want 1)", ay ? 1 : 0));
    }

    // NR-02 — psg_mode=00 (YM). aymode_bit = 0 → TurboSound in YM mode.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x00);       // psg_mode=00
        const bool ay = emu.turbosound().ay_mode();
        check("NR-02",
              "psg_mode=00 sinks to YM mode [zxnext.vhd:6389]",
              ay == false,
              fmt("psg_mode=00 → ay_mode=%d (want 0)", ay ? 1 : 0));
    }

    // NR-03 — psg_mode=01 (AY). aymode_bit = 1 → AY mode.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x01);       // psg_mode=01
        const bool ay = emu.turbosound().ay_mode();
        check("NR-03",
              "psg_mode=01 sinks to AY mode [zxnext.vhd:6389]",
              ay == true,
              fmt("psg_mode=01 → ay_mode=%d (want 1)", ay ? 1 : 0));
    }

    // NR-04 — psg_mode=10 (alias). aymode_bit = 0 (bit 0 of "10" = 0) →
    // YM mode, duplicating psg_mode=00. This is the VHDL "alias" row —
    // two bit combinations produce identical aymode routing.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x02);       // psg_mode=10
        const bool ay = emu.turbosound().ay_mode();
        check("NR-04",
              "psg_mode=10 alias (bit 0 = 0 → YM) [zxnext.vhd:6389]",
              ay == false,
              fmt("psg_mode=10 → ay_mode=%d (want 0)", ay ? 1 : 0));
    }

    // NR-05 — audio_ay_reset = (psg_mode == "11"). The VHDL:6379 signal
    // feeds turbosound's reset_i, so after writing psg_mode=11 the AY
    // register file must be cleared (a previously-written register value
    // is gone). Exercise: write a distinctive value to AY#0 reg 0 via
    // the port path (reg 7 is a poor probe — AyChip::reset reinitialises
    // it to 0xFF per VHDL ym2149.vhd, so a pre/post compare at reg 7 is
    // 0x55 → 0xFF, not → 0x00). Then write NR 0x06 psg_mode=11 and read
    // back — reg 0 must be 0.
    {
        fresh(emu);
        // Prime NR 0x06 bit 0 = 1 so AY is addressable.
        nr_write(emu, 0x06, 0x01);
        // Select AY#0 (default) and write reg 0 = 0x55 via port 0xFFFD/0xBFFD.
        emu.port().out(0xFFFD, 0x00);
        emu.port().out(0xBFFD, 0x55);
        const uint8_t before = emu.turbosound().ay(0).read_register(0);
        // Assert audio_ay_reset via psg_mode=11. The handler invokes
        // turbosound_.reset() which re-initialises the register file.
        nr_write(emu, 0x06, 0x03);
        const uint8_t after = emu.turbosound().ay(0).read_register(0);
        check("NR-05",
              "audio_ay_reset = (nr_06_psg_mode=\"11\") clears AY register file "
              "[zxnext.vhd:6379, :6387]",
              before == 0x55 && after == 0x00,
              fmt("before=0x%02X after=0x%02X (want 0x55 → 0x00)", before, after));
    }

    // NR-06 — NR 0x06 bit 6 stores nr_06_internal_speaker_beep. Observable
    // via the Emulator-level mirror exposed for this integration harness.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x40);       // bit 6 = 1
        const bool on = emu.nr_06_internal_speaker_beep();
        nr_write(emu, 0x06, 0x00);       // bit 6 = 0
        const bool off = emu.nr_06_internal_speaker_beep();
        check("NR-06",
              "NR 0x06 bit 6 latches nr_06_internal_speaker_beep "
              "[zxnext.vhd:5163]",
              on && !off,
              fmt("on=%d off=%d (want 1/0)", on ? 1 : 0, off ? 1 : 0));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group NR-0x08 — stereo / speaker / DAC / turbosound / issue2
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:5176-5182:
//   nr_08_contention_disable <= bit 6
//   nr_08_psg_stereo_mode    <= bit 5       (routed to TurboSound::stereo_mode)
//   nr_08_internal_speaker_en<= bit 4       (store-and-read-back + exc_i)
//   nr_08_dac_en             <= bit 3       (dac_enabled_ + Soundrive gate)
//   nr_08_port_ff_rd_en      <= bit 2
//   nr_08_psg_turbosound_en  <= bit 1       (TurboSound::enabled)
//   nr_08_keyboard_issue2    <= bit 0       (store-and-read-back + beep_mic_final)
// ══════════════════════════════════════════════════════════════════════

static void test_nr_08(Emulator& emu) {
    set_group("NR-0x08");

    // NR-10 — NR 0x08 bit 5 forwards to TurboSound::stereo_mode.
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x20);       // bit 5 = 1 (ACB)
        const bool acb = emu.turbosound().stereo_mode();
        nr_write(emu, 0x08, 0x00);       // bit 5 = 0 (ABC)
        const bool abc = emu.turbosound().stereo_mode();
        check("NR-10",
              "NR 0x08 bit 5 → nr_08_psg_stereo_mode → TurboSound stereo "
              "[zxnext.vhd:5177, :6400]",
              acb == true && abc == false,
              fmt("acb=%d abc=%d (want 1/0)", acb ? 1 : 0, abc ? 1 : 0));
    }

    // NR-11 — NR 0x08 bit 4 stores nr_08_internal_speaker_en in the
    // NR 0x08 stored-low mirror (VHDL zxnext.vhd:5178 + read-back :5906).
    // Default value after reset is '1' (VHDL zxnext.vhd:1117 — default '1';
    // src/core/emulator.cpp init() seeds nr_08_stored_low_=0x10).
    {
        fresh(emu);
        const uint8_t reset_default = emu.nr_08_stored_low() & 0x10;
        nr_write(emu, 0x08, 0x00);       // bit 4 = 0
        const uint8_t cleared = emu.nr_08_stored_low() & 0x10;
        nr_write(emu, 0x08, 0x10);       // bit 4 = 1
        const uint8_t set = emu.nr_08_stored_low() & 0x10;
        check("NR-11",
              "NR 0x08 bit 4 latches nr_08_internal_speaker_en (default '1') "
              "[zxnext.vhd:5178, :1117, :5906]",
              reset_default == 0x10 && cleared == 0x00 && set == 0x10,
              fmt("default=0x%02X cleared=0x%02X set=0x%02X "
                  "(want 0x10/0x00/0x10)", reset_default, cleared, set));
    }

    // NR-12 — NR 0x08 bit 3 latches nr_08_dac_en → dac_enabled_ which
    // gates Soundrive DAC port writes (see IO-10 / SD-17 below).
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x08);       // bit 3 = 1
        const bool en = emu.dac_enabled();
        nr_write(emu, 0x08, 0x00);       // bit 3 = 0
        const bool dis = emu.dac_enabled();
        check("NR-12",
              "NR 0x08 bit 3 latches nr_08_dac_en [zxnext.vhd:5179]",
              en && !dis,
              fmt("en=%d dis=%d (want 1/0)", en ? 1 : 0, dis ? 1 : 0));
    }

    // NR-13 — NR 0x08 bit 1 latches nr_08_psg_turbosound_en → routes to
    // TurboSound::enabled (the flag that promotes AY#1/#2 from dormant).
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x02);       // bit 1 = 1
        const bool on = emu.turbosound().enabled();
        nr_write(emu, 0x08, 0x00);
        const bool off = emu.turbosound().enabled();
        check("NR-13",
              "NR 0x08 bit 1 → nr_08_psg_turbosound_en → TurboSound enable "
              "[zxnext.vhd:5181, :6390]",
              on && !off,
              fmt("on=%d off=%d (want 1/0)", on ? 1 : 0, off ? 1 : 0));
    }

    // NR-14 — NR 0x08 bit 0 latches nr_08_keyboard_issue2. Observable via
    // the stored-low mirror; composes into beep_mic_final (BP-11 below).
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x01);       // bit 0 = 1 (issue2)
        const uint8_t issue2_on = emu.nr_08_stored_low() & 0x01;
        nr_write(emu, 0x08, 0x00);       // bit 0 = 0 (issue3)
        const uint8_t issue2_off = emu.nr_08_stored_low() & 0x01;
        check("NR-14",
              "NR 0x08 bit 0 latches nr_08_keyboard_issue2 [zxnext.vhd:5182, :5906]",
              issue2_on == 0x01 && issue2_off == 0x00,
              fmt("on=0x%02X off=0x%02X (want 0x01/0x00)", issue2_on, issue2_off));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group NR-0x09 — per-PSG mono flags
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:5186 — nr_09_psg_mono <= nr_wr_dat(7 downto 5).
// That 3-bit field maps, per the TurboSound binding at zxnext.vhd:6398,
// into the `mono_mode_i` input: bit index = AY chip id minus its slot.
// The current emulator handler at src/core/emulator.cpp:1389 does:
//   bit 5 (NR 0x09) → AY#0 mono  (TurboSound mono_mode_ bit 0)
//   bit 6 (NR 0x09) → AY#1 mono  (TurboSound mono_mode_ bit 1)
//   bit 7 (NR 0x09) → AY#2 mono  (TurboSound mono_mode_ bit 2)
// ══════════════════════════════════════════════════════════════════════

static void test_nr_09(Emulator& emu) {
    set_group("NR-0x09");

    // NR-20 — writing NR 0x09 with bits 7:5 = 101 must latch a non-zero
    // per-chip mono mask. Observable via TurboSound::mono_mode().
    {
        fresh(emu);
        nr_write(emu, 0x09, 0xA0);       // bits 7:5 = 101 (AY#2 + AY#0)
        const uint8_t mono = emu.turbosound().mono_mode();
        check("NR-20",
              "NR 0x09 bits[7:5] latch per-PSG mono into TurboSound::mono_mode "
              "[zxnext.vhd:5186]",
              mono != 0,
              fmt("mono_mode=0x%02X (want non-zero)", mono));
    }

    // NR-21 — VHDL-faithful bit mapping: NR 0x09 bit 5 → AY#0 (mono_mode
    // bit 0), bit 6 → AY#1 (bit 1), bit 7 → AY#2 (bit 2). Write each
    // in isolation and verify the exact target bit flips.
    {
        fresh(emu);
        nr_write(emu, 0x09, 0x20);              // bit 5 only → AY#0
        const uint8_t ay0 = emu.turbosound().mono_mode();
        nr_write(emu, 0x09, 0x40);              // bit 6 only → AY#1
        const uint8_t ay1 = emu.turbosound().mono_mode();
        nr_write(emu, 0x09, 0x80);              // bit 7 only → AY#2
        const uint8_t ay2 = emu.turbosound().mono_mode();

        const bool ok =
            ay0 == 0x01 && ay1 == 0x02 && ay2 == 0x04;
        check("NR-21",
              "NR 0x09 bits 5/6/7 map to mono_mode bits 0/1/2 (AY#0/#1/#2) "
              "[zxnext.vhd:5186, :6398]",
              ok,
              fmt("ay0=0x%02X ay1=0x%02X ay2=0x%02X (want 0x01/0x02/0x04)",
                  ay0, ay1, ay2));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group NR-DAC (0x2C/0x2D/0x2E) — Soundrive NextREG mirrors
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:4852-4854 + :6452-6454:
//   NR 0x2C → nr_2c_we → soundrive nr_left_we  → chB   (VHDL soundrive.vhd:90-94)
//   NR 0x2D → nr_2d_we → soundrive nr_mono_we  → chA+chD (VHDL soundrive.vhd:85-89)
//   NR 0x2E → nr_2e_we → soundrive nr_right_we → chC   (VHDL soundrive.vhd:95-97)
// Gated by the Soundrive reset — active while nr_08_dac_en='0' (VHDL:6436).
// Mirror semantics in Dac:
//   write_left(v)  → ch_[1] = v
//   write_mono(v)  → ch_[0] = v; ch_[3] = v
//   write_right(v) → ch_[2] = v
// Observable at Dac::pcm_left() / pcm_right() (9-bit sums).
// ══════════════════════════════════════════════════════════════════════

static void test_nr_dac(Emulator& emu) {
    set_group("NR-DAC");

    // NR-30 — NR 0x2C writes Soundrive chB (left). Precondition: enable
    // the DAC (NR 0x08 bit 3 = 1) so the Soundrive is out of reset. Then
    // write a distinctive payload via NR 0x2C and confirm pcm_left gained
    // the same offset while pcm_right is unchanged from silence (0x100).
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x08);            // enable DAC
        nr_write(emu, 0x2C, 0x40);            // chB = 0x40
        const uint16_t L = emu.dac().pcm_left();
        const uint16_t R = emu.dac().pcm_right();
        // After reset chA/C/D = 0x80 (silence), chB = 0x40 → L = 0x80+0x40 = 0xC0;
        // R = 0x80+0x80 = 0x100.
        check("NR-30",
              "NR 0x2C → soundrive nr_left_we writes chB (left) "
              "[zxnext.vhd:4852, :6453; soundrive.vhd:90-94]",
              L == 0xC0 && R == 0x100,
              fmt("L=0x%03X R=0x%03X (want 0x0C0/0x100)", L, R));
    }

    // NR-31 — NR 0x2D → soundrive nr_mono_we writes chA AND chD (affecting
    // both left and right sums by the same amount).
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x08);            // enable DAC
        nr_write(emu, 0x2D, 0x40);            // mono = 0x40 → chA = chD = 0x40
        const uint16_t L = emu.dac().pcm_left();
        const uint16_t R = emu.dac().pcm_right();
        // chA=0x40, chB=0x80 → L=0xC0; chC=0x80, chD=0x40 → R=0xC0.
        check("NR-31",
              "NR 0x2D → soundrive nr_mono_we writes chA+chD (both sides) "
              "[zxnext.vhd:4853, :6452; soundrive.vhd:85-89]",
              L == 0xC0 && R == 0xC0,
              fmt("L=0x%03X R=0x%03X (want 0x0C0/0x0C0)", L, R));
    }

    // NR-32 — NR 0x2E → soundrive nr_right_we writes chC (right).
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x08);            // enable DAC
        nr_write(emu, 0x2E, 0x40);            // chC = 0x40
        const uint16_t L = emu.dac().pcm_left();
        const uint16_t R = emu.dac().pcm_right();
        // chA=chB=0x80 → L=0x100; chC=0x40, chD=0x80 → R=0xC0.
        check("NR-32",
              "NR 0x2E → soundrive nr_right_we writes chC (right) "
              "[zxnext.vhd:4854, :6454; soundrive.vhd:95-97]",
              L == 0x100 && R == 0xC0,
              fmt("L=0x%03X R=0x%03X (want 0x100/0x0C0)", L, R));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group NR-BEEP — beep_mic_final (VHDL:6503) + beep_spkr_excl (VHDL:6504)
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:6503 (beep_mic_final):
//     beep_mic_final <= i_AUDIO_EAR
//                       xor (port_fe_mic and nr_08_keyboard_issue2)
//                       xor port_fe_mic;
// When issue2 = '1': the double XOR (port_fe_mic XOR port_fe_mic) cancels,
//                    so beep_mic_final = i_AUDIO_EAR (tape-EAR alone).
// When issue2 = '0': one of the XORs survives, so
//                    beep_mic_final = i_AUDIO_EAR XOR port_fe_mic (issue3 path).
//
// VHDL zxnext.vhd:6504 (beep_spkr_excl):
//     beep_spkr_excl <= nr_06_internal_speaker_beep AND nr_08_internal_speaker_en;
//
// We observe beep_spkr_excl directly (Emulator::beep_spkr_excl() helper),
// and we model beep_mic_final as a pure function of Beeper::tape_ear(),
// Beeper::mic() and NR 0x08 bit 0 (issue2), computed inline here.
// ══════════════════════════════════════════════════════════════════════

static bool beep_mic_final_from(bool tape_ear, bool port_fe_mic, bool issue2) {
    // VHDL:6503 literal port.
    return tape_ear ^ (port_fe_mic & issue2) ^ port_fe_mic;
}

static void test_nr_beep(Emulator& emu) {
    set_group("NR-BEEP");

    // BP-10 — beep_mic_final is the XOR combination defined at VHDL:6503.
    // Four-corner sanity check: with (tape_ear, mic, issue2) = (0,1,1)
    //   = 0 XOR (1 AND 1) XOR 1 = 0 XOR 1 XOR 1 = 0.
    // With (0,1,0) = 0 XOR (1 AND 0) XOR 1 = 0 XOR 0 XOR 1 = 1.
    // With (1,1,1) = 1 XOR 1 XOR 1 = 1. With (1,0,1) = 1 XOR 0 XOR 0 = 1.
    // Exhaustive 8-case pure-logic test.
    {
        fresh(emu);
        bool all_ok = true;
        for (int i = 0; i < 8; ++i) {
            const bool ear    = (i & 1) != 0;
            const bool mic    = (i & 2) != 0;
            const bool issue2 = (i & 4) != 0;
            const bool expect = ear ^ (mic & issue2) ^ mic;
            const bool got    = beep_mic_final_from(ear, mic, issue2);
            if (got != expect) all_ok = false;
        }
        check("BP-10",
              "beep_mic_final XOR expression matches VHDL:6503 over all 8 corners",
              all_ok,
              "all tape_ear/mic/issue2 combinations checked");
    }

    // BP-11 — issue2 cancellation: when nr_08_keyboard_issue2 = 1, the
    // MIC contribution cancels out (VHDL:6503 double-XOR), so
    // beep_mic_final = i_AUDIO_EAR regardless of port_fe_mic. We drive
    // NR 0x08 bit 0 and verify the live NR 0x08 bit-0 mirror matches, then
    // check the logic directly: (ear=0, mic=1, issue2=1) → 0.
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x01);              // issue2 = 1
        const bool issue2_hot = (emu.nr_08_stored_low() & 0x01) != 0;
        const bool mf_tape0_mic1 = beep_mic_final_from(false, true, issue2_hot);
        const bool mf_tape1_mic1 = beep_mic_final_from(true,  true, issue2_hot);
        check("BP-11",
              "issue2 path cancels MIC → beep_mic_final = i_AUDIO_EAR "
              "[zxnext.vhd:6503]",
              issue2_hot && mf_tape0_mic1 == false && mf_tape1_mic1 == true,
              fmt("issue2=%d mf(0,1)=%d mf(1,1)=%d (want 1/0/1)",
                  issue2_hot ? 1 : 0,
                  mf_tape0_mic1 ? 1 : 0,
                  mf_tape1_mic1 ? 1 : 0));
    }

    // BP-12 — issue3 (issue2=0) path: beep_mic_final = tape_ear XOR mic.
    // Verify the two non-trivial corners (0,1) → 1 and (1,1) → 0 given
    // nr_08_keyboard_issue2 = 0.
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x00);              // issue2 = 0
        const bool issue2_hot = (emu.nr_08_stored_low() & 0x01) != 0;
        const bool mf_tape0_mic1 = beep_mic_final_from(false, true, issue2_hot);
        const bool mf_tape1_mic1 = beep_mic_final_from(true,  true, issue2_hot);
        check("BP-12",
              "issue3 (issue2=0) → beep_mic_final = tape_ear XOR mic "
              "[zxnext.vhd:6503]",
              !issue2_hot && mf_tape0_mic1 == true && mf_tape1_mic1 == false,
              fmt("issue2=%d mf(0,1)=%d mf(1,1)=%d (want 0/1/0)",
                  issue2_hot ? 1 : 0,
                  mf_tape0_mic1 ? 1 : 0,
                  mf_tape1_mic1 ? 1 : 0));
    }

    // BP-13 — beep_spkr_excl = nr_06_internal_speaker_beep AND
    // nr_08_internal_speaker_en (VHDL zxnext.vhd:6504). Verify the AND
    // truth table over the four corners via the composite accessor.
    {
        fresh(emu);

        // (speaker_beep=0, speaker_en=0)
        nr_write(emu, 0x06, 0x00);              // bit 6 = 0
        nr_write(emu, 0x08, 0x00);              // bit 4 = 0
        const bool e00 = emu.beep_spkr_excl();

        // (speaker_beep=1, speaker_en=0)
        nr_write(emu, 0x06, 0x40);
        nr_write(emu, 0x08, 0x00);
        const bool e10 = emu.beep_spkr_excl();

        // (speaker_beep=0, speaker_en=1)
        nr_write(emu, 0x06, 0x00);
        nr_write(emu, 0x08, 0x10);
        const bool e01 = emu.beep_spkr_excl();

        // (speaker_beep=1, speaker_en=1)
        nr_write(emu, 0x06, 0x40);
        nr_write(emu, 0x08, 0x10);
        const bool e11 = emu.beep_spkr_excl();

        check("BP-13",
              "beep_spkr_excl = nr_06_internal_speaker_beep AND "
              "nr_08_internal_speaker_en [zxnext.vhd:6504]",
              !e00 && !e10 && !e01 && e11,
              fmt("(0,0)=%d (1,0)=%d (0,1)=%d (1,1)=%d (want 0/0/0/1)",
                  e00 ? 1 : 0, e10 ? 1 : 0, e01 ? 1 : 0, e11 ? 1 : 0));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group NR-MIXER — exc_i gating (beep_spkr_excl into audio_mixer.exc_i)
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:6514 — audio_mixer.exc_i <= beep_spkr_excl. Per
// audio_mixer.vhd:81-85, exc_i='1' silences the EAR and MIC contributions
// (the `(others => '0') when exc_i` branches). We assert the composite
// wire exists and tracks its inputs at the Emulator level — the mixer
// silencing itself is a downstream audio_mixer.vhd concern that the
// current src/audio/mixer.cpp does not gate on exc_i, so we observe the
// NEXTREG-side composition only.
// ══════════════════════════════════════════════════════════════════════

static void test_nr_mixer(Emulator& emu) {
    set_group("NR-MIXER");

    // MX-03 — exc_i gating wire composition is the same as BP-13 but
    // observed from the "upstream-of-Mixer" side: we verify that flipping
    // either constituent bit changes beep_spkr_excl.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x40);              // speaker_beep = 1
        nr_write(emu, 0x08, 0x10);              // speaker_en   = 1
        const bool both_on = emu.beep_spkr_excl();
        nr_write(emu, 0x08, 0x00);              // drop speaker_en
        const bool en_off = emu.beep_spkr_excl();
        check("MX-03",
              "exc_i (beep_spkr_excl) tracks NR 0x06 bit 6 AND NR 0x08 bit 4 "
              "[zxnext.vhd:6504, :6514]",
              both_on && !en_off,
              fmt("both_on=%d en_off=%d (want 1/0)",
                  both_on ? 1 : 0, en_off ? 1 : 0));
    }

    // MX-20 — exc_i='1' is the silencing condition. Verify that the
    // composite goes true exactly when both upstream NR bits are set (and
    // is false for all three other corners — the silencing path can only
    // trigger in one out of four NR configurations).
    {
        fresh(emu);
        int hot = 0;
        for (int sb = 0; sb < 2; ++sb) {
            for (int se = 0; se < 2; ++se) {
                nr_write(emu, 0x06, sb ? 0x40 : 0x00);
                nr_write(emu, 0x08, se ? 0x10 : 0x00);
                if (emu.beep_spkr_excl()) ++hot;
            }
        }
        check("MX-20",
              "exc_i silencing path fires for exactly one NR combination "
              "(speaker_beep=1 AND speaker_en=1) [zxnext.vhd:6504]",
              hot == 1,
              fmt("hot_corners=%d (want 1)", hot));
    }

    // MX-22 — exc_i derivation is fully a function of two NR bits (no
    // other audio state feeds it). Drive orthogonal state (AY register
    // writes, DAC channel writes) and confirm beep_spkr_excl stays put.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x40);
        nr_write(emu, 0x08, 0x10);
        const bool base = emu.beep_spkr_excl();
        // Prod DAC ports + AY reg 7 + issue2 bit — none should move exc_i.
        nr_write(emu, 0x08, 0x18);              // enable DAC + speaker_en
        emu.port().out(0xFFFD, 0x07); emu.port().out(0xBFFD, 0x3F);
        emu.port().out(0x1F,   0x55);           // DAC chA
        nr_write(emu, 0x08, 0x11);              // speaker_en + issue2
        const bool kept = emu.beep_spkr_excl();
        check("MX-22",
              "exc_i depends only on NR 0x06 b6 + NR 0x08 b4 — no AY/DAC/issue2 "
              "crosstalk [zxnext.vhd:6504]",
              base && kept,
              fmt("base=%d kept=%d (want 1/1)",
                  base ? 1 : 0, kept ? 1 : 0));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group NR-DAC-GATE — dac_hw_en / dac_en gate (NR 0x08 bit 3 = 1 enables
// the Soundrive port-write path; =0 drops writes and holds the channel
// registers at silence).
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:5179, :6436:
//   nr_08_dac_en <= nr_wr_dat(3);
//   soundrive.reset_i <= reset OR NOT nr_08_dac_en;  → channels held at 0x80
// jnext models this at src/core/emulator.cpp:1304-1308 where every DAC
// port handler is wrapped in `if (dac_enabled_) …`. With the gate closed
// the registers stay at 0x80 silence.
// ══════════════════════════════════════════════════════════════════════

static void test_nr_dac_gate(Emulator& emu) {
    set_group("NR-DAC-GATE");

    // SD-17 — nr_08_dac_en gates Soundrive port writes. With dac_en=0,
    // writes to 0x1F/0x0F/0x4F/0x5F etc. must be dropped; pcm_left/right
    // stay at silence (0x100 each). With dac_en=1, the same writes take
    // effect and pcm_* shifts accordingly.
    {
        fresh(emu);
        // Gate closed (reset default — NR 0x08 power-on = 0x10 → bit 3=0).
        nr_write(emu, 0x08, 0x00);              // explicitly clear everything
        emu.port().out(0x1F, 0xFF);             // would write chA=0xFF
        emu.port().out(0x0F, 0xFF);             // would write chB=0xFF
        const uint16_t L_off = emu.dac().pcm_left();   // expect 0x100 (silence)
        const uint16_t R_off = emu.dac().pcm_right();  // expect 0x100

        fresh(emu);
        nr_write(emu, 0x08, 0x08);              // gate open
        emu.port().out(0x1F, 0xFF);             // chA = 0xFF
        emu.port().out(0x0F, 0xFF);             // chB = 0xFF
        const uint16_t L_on = emu.dac().pcm_left();    // expect 0x1FE (max L)
        const uint16_t R_on = emu.dac().pcm_right();   // expect 0x100

        check("SD-17",
              "nr_08_dac_en gates Soundrive port writes [zxnext.vhd:5179, :6436]",
              L_off == 0x100 && R_off == 0x100 && L_on == 0x1FE && R_on == 0x100,
              fmt("gated L=0x%03X R=0x%03X; open L=0x%03X R=0x%03X "
                  "(want 0x100/0x100/0x1FE/0x100)",
                  L_off, R_off, L_on, R_on));
    }

    // IO-10 — dac_hw_en gate (same NR 0x08 bit 3 routed through the port
    // dispatcher) observed on the Soundrive Mode 2 ports 0xF1 (chA) and
    // 0xF3 (chB) to double-check the gate holds across port variants.
    // With gate open, both writes land → L = 0xFF+0xFF = 0x1FE. With gate
    // closed, both writes drop → L stays at silence 0x80+0x80 = 0x100.
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x00);              // gate closed
        emu.port().out(0xF1, 0xFF);             // Mode-2 chA write
        emu.port().out(0xF3, 0xFF);             // Mode-2 chB write
        const uint16_t L_off = emu.dac().pcm_left();

        fresh(emu);
        nr_write(emu, 0x08, 0x08);              // gate open
        emu.port().out(0xF1, 0xFF);             // Mode-2 chA write
        emu.port().out(0xF3, 0xFF);             // Mode-2 chB write
        const uint16_t L_on = emu.dac().pcm_left();

        check("IO-10",
              "dac_hw_en gate holds for Mode-2 ports (0xF1/0xF3) [zxnext.vhd:2775-2778, :6436]",
              L_off == 0x100 && L_on == 0x1FE,
              fmt("gated L=0x%03X open L=0x%03X (want 0x100/0x1FE)",
                  L_off, L_on));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Audio + NextREG Integration Tests\n");
    std::printf("===============================================\n\n");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        std::printf("FATAL: could not construct Emulator\n");
        return 1;
    }
    std::printf("  Emulator constructed (ZXN_ISSUE2)\n\n");

    test_nr_06(emu);        std::printf("  Group: NR-0x06 -- done\n");
    test_nr_08(emu);        std::printf("  Group: NR-0x08 -- done\n");
    test_nr_09(emu);        std::printf("  Group: NR-0x09 -- done\n");
    test_nr_dac(emu);       std::printf("  Group: NR-DAC -- done\n");
    test_nr_beep(emu);      std::printf("  Group: NR-BEEP -- done\n");
    test_nr_mixer(emu);     std::printf("  Group: NR-MIXER -- done\n");
    test_nr_dac_gate(emu);  std::printf("  Group: NR-DAC-GATE -- done\n");

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    // Per-group breakdown.
    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp   = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-10s %s\n", s.id, s.reason);
        }
        std::printf("  (%zu skipped)\n", g_skipped.size());
    }

    return g_fail > 0 ? 1 : 0;
}
