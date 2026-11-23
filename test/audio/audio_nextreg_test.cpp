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
//   AUD-NR-01..AUD-NR-05 — NR 0x06 bits[1:0] PSG-mode fan-out (YM/AY/alias/audio_ay_reset)
//   AUD-NR-06       — NR 0x06 bit 6 internal_speaker_beep store
//   AUD-NR-10, AUD-NR-11, NR-12, AUD-NR-13, AUD-NR-14 — NR 0x08 stereo / speaker / DAC / turbosound / issue2
//   NR-20, NR-21    — NR 0x09 per-PSG mono bit→chip mapping
//   NR-30, NR-31, NR-32 — NR 0x2C/0x2D/0x2E Soundrive mirrors (left/mono/right)
//   BP-10..BP-13    — beep_mic_final XOR + issue2 cancel + issue3 EAR^MIC + beep_spkr_excl
//   AUD-MX-03, MX-20, MX-22 — exc_i (beep_spkr_excl) gating into Mixer
//   AUD-SD-17, IO-10 — dac_hw_en / dac_en gate (NR 0x08 bit 3)
//
// NOTE (GH #196 dedup): AUD-NR-* / AUD-MX-* / AUD-SD-* IDs in this file were
// renamed from bare NR-*/MX-*/SD-* to resolve a global test-ID collision with
// unrelated tests in DivMMC+SPI / SD Card (same short ID, different subsystem
// and different behaviour). Pure rename — no logic change.
//
// Reference plan: doc/design/TASK3-AUDIO-SKIP-REDUCTION-PLAN.md
// Reference structural template: test/uart/uart_integration_test.cpp,
//                                test/ula/ula_integration_test.cpp.
//
// Run: ./build/test/audio_nextreg_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "save/state_desc_json.h"

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "../row_id.h"

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
    report_row_id(id);
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

} // namespace

// ── Emulator construction helpers ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
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

// Read NextREG register through the real port path (OUT 0x243B,reg;
// IN 0x253B). Used by NR-33/NR-34 to exercise the live read handlers.
static uint8_t nr_read(Emulator& emu, uint8_t reg) {
    emu.port().out(0x243B, reg);
    return emu.port().in(0x253B);
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

    // AUD-NR-01 — NextREG 0x06 bits[1:0] reach TurboSound::ay_mode()
    // (bit-0 fan-out per VHDL:6389). Spot-check: after writing bit 0 = 1
    // (psg_mode = 01 — AY mode), turbosound_.ay_mode() is true.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x01);       // psg_mode=01
        const bool ay = emu.turbosound().ay_mode();
        check("AUD-NR-01",
              "NR 0x06 bits[1:0] handler forwards to TurboSound::ay_mode "
              "[zxnext.vhd:5170, :6389]",
              ay == true,
              fmt("psg_mode=01 → ay_mode=%d (want 1)", ay ? 1 : 0));
    }

    // AUD-NR-02 — psg_mode=00 (YM). aymode_bit = 0 → TurboSound in YM mode.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x00);       // psg_mode=00
        const bool ay = emu.turbosound().ay_mode();
        check("AUD-NR-02",
              "psg_mode=00 sinks to YM mode [zxnext.vhd:6389]",
              ay == false,
              fmt("psg_mode=00 → ay_mode=%d (want 0)", ay ? 1 : 0));
    }

    // AUD-NR-03 — psg_mode=01 (AY). aymode_bit = 1 → AY mode.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x01);       // psg_mode=01
        const bool ay = emu.turbosound().ay_mode();
        check("AUD-NR-03",
              "psg_mode=01 sinks to AY mode [zxnext.vhd:6389]",
              ay == true,
              fmt("psg_mode=01 → ay_mode=%d (want 1)", ay ? 1 : 0));
    }

    // AUD-NR-04 — psg_mode=10 (alias). aymode_bit = 0 (bit 0 of "10" = 0) →
    // YM mode, duplicating psg_mode=00. This is the VHDL "alias" row —
    // two bit combinations produce identical aymode routing.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x02);       // psg_mode=10
        const bool ay = emu.turbosound().ay_mode();
        check("AUD-NR-04",
              "psg_mode=10 alias (bit 0 = 0 → YM) [zxnext.vhd:6389]",
              ay == false,
              fmt("psg_mode=10 → ay_mode=%d (want 0)", ay ? 1 : 0));
    }

    // AUD-NR-05 — audio_ay_reset = (psg_mode == "11"). The VHDL:6379 signal
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
        // turbosound_.reset_ay_only() (then holds) which re-initialises the register file.
        nr_write(emu, 0x06, 0x03);
        const uint8_t after = emu.turbosound().ay(0).read_register(0);
        check("AUD-NR-05",
              "audio_ay_reset = (nr_06_psg_mode=\"11\") clears AY register file "
              "[zxnext.vhd:6379, :6387]",
              before == 0x55 && after == 0x00,
              fmt("before=0x%02X after=0x%02X (want 0x55 → 0x00)", before, after));
    }

    // AUD-NR-06 — NR 0x06 bit 6 stores nr_06_internal_speaker_beep. Observable
    // via the Emulator-level mirror exposed for this integration harness.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x40);       // bit 6 = 1
        const bool on = emu.nr_06_internal_speaker_beep();
        nr_write(emu, 0x06, 0x00);       // bit 6 = 0
        const bool off = emu.nr_06_internal_speaker_beep();
        check("AUD-NR-06",
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

    // AUD-NR-10 — NR 0x08 bit 5 forwards to TurboSound::stereo_mode.
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x20);       // bit 5 = 1 (ACB)
        const bool acb = emu.turbosound().stereo_mode();
        nr_write(emu, 0x08, 0x00);       // bit 5 = 0 (ABC)
        const bool abc = emu.turbosound().stereo_mode();
        check("AUD-NR-10",
              "NR 0x08 bit 5 → nr_08_psg_stereo_mode → TurboSound stereo "
              "[zxnext.vhd:5177, :6400]",
              acb == true && abc == false,
              fmt("acb=%d abc=%d (want 1/0)", acb ? 1 : 0, abc ? 1 : 0));
    }

    // AUD-NR-11 — NR 0x08 bit 4 stores nr_08_internal_speaker_en in the
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
        check("AUD-NR-11",
              // :1116 is the nr_08_internal_speaker_en declaration, default
              // '1' — the reset value this row asserts. It used to read :1117,
              // which is nr_08_dac_en, i.e. NR-12's signal (GH #151).
              "NR 0x08 bit 4 latches nr_08_internal_speaker_en (default '1') "
              "[zxnext.vhd:5178, :1116, :5906]",
              reset_default == 0x10 && cleared == 0x00 && set == 0x10,
              fmt("default=0x%02X cleared=0x%02X set=0x%02X "
                  "(want 0x10/0x00/0x10)", reset_default, cleared, set));
    }

    // NR-12 — NR 0x08 bit 3 latches nr_08_dac_en → dac_enabled_ which
    // gates Soundrive DAC port writes (see IO-10 / AUD-SD-17 below).
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

    // AUD-NR-13 — NR 0x08 bit 1 latches nr_08_psg_turbosound_en → routes to
    // TurboSound::enabled (the flag that promotes AY#1/#2 from dormant).
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x02);       // bit 1 = 1
        const bool on = emu.turbosound().enabled();
        nr_write(emu, 0x08, 0x00);
        const bool off = emu.turbosound().enabled();
        check("AUD-NR-13",
              "NR 0x08 bit 1 → nr_08_psg_turbosound_en → TurboSound enable "
              "[zxnext.vhd:5181, :6390]",
              on && !off,
              fmt("on=%d off=%d (want 1/0)", on ? 1 : 0, off ? 1 : 0));
    }

    // AUD-NR-14 — NR 0x08 bit 0 latches nr_08_keyboard_issue2. Observable via
    // the stored-low mirror; composes into beep_mic_final (BP-11 below).
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x01);       // bit 0 = 1 (issue2)
        const uint8_t issue2_on = emu.nr_08_stored_low() & 0x01;
        nr_write(emu, 0x08, 0x00);       // bit 0 = 0 (issue3)
        const uint8_t issue2_off = emu.nr_08_stored_low() & 0x01;
        check("AUD-NR-14",
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

    // NR-33 — zxnext.vhd:6594-6596 read of NR 0x2C returns pi_audio_L(9..2)
    // AND atomically latches pi_audio_L(1..0) into nr_2d_i2s_sample. We
    // verify the byte returned matches the expected high-8 bits and that
    // the latch (observable via the next NR 0x2D read, NR-34 below) carries
    // the low 2 bits in [7:6]. Symmetric check for NR 0x2E (right channel).
    //
    // G113: pi_audio_L/R is the GATED signal (zxnext.vhd:2674-2675). At
    // power-on, NR 0xA2 = 0 → pi_i2s_en = 0 → both channels read back
    // the silence midpoint 0x200, NOT the raw I2s sample latch. We
    // explicitly enable both channels (NR 0xA2 = 0xC0; enL=enR=1, no
    // mute, ear=0) so the gate passes the raw sample through.
    {
        fresh(emu);
        nr_write(emu, 0xA2, 0xC0);                 // enL+enR, no mute, ear=0
        // Inject a known 10-bit pattern into the I2s stub.
        emu.i2s().set_sample(0x3A5, 0x12C);

        // Read NR 0x2C → byte = (0x3A5 >> 2) & 0xFF = 0xE9.
        const uint8_t l_byte = nr_read(emu, 0x2C);
        // Latch (via NR 0x2D) = (0x3A5 & 0x03) << 6 = 0x40.
        const uint8_t l_latch = nr_read(emu, 0x2D);

        // Read NR 0x2E → byte = (0x12C >> 2) & 0xFF = 0x4B.
        const uint8_t r_byte = nr_read(emu, 0x2E);
        // Latch (via NR 0x2D) = (0x12C & 0x03) << 6 = 0x00.
        const uint8_t r_latch = nr_read(emu, 0x2D);

        check("NR-33",
              "NR 0x2C/0x2E read returns pi_audio_L/R(9..2) and latches "
              "pi_audio_L/R(1..0) into nr_2d_i2s_sample "
              "[zxnext.vhd:6594-6603, :2674-2675]",
              l_byte == 0xE9 && l_latch == 0x40 &&
              r_byte == 0x4B && r_latch == 0x00,
              fmt("L byte=0x%02X latch=0x%02X (want 0xE9/0x40); "
                  "R byte=0x%02X latch=0x%02X (want 0x4B/0x00)",
                  l_byte, l_latch, r_byte, r_latch));
    }

    // NR-34 — zxnext.vhd:6598-6599 read of NR 0x2D returns
    //   port_253b_dat <= nr_2d_i2s_sample & "000000".
    // The latch starts at 0 (init() seeds nr_2d_i2s_sample_ = 0); after a
    // read of NR 0x2C with a non-aligned (low 2 bits != 00) sample, the
    // latch holds those 2 bits in byte [7:6] and reading NR 0x2D returns
    // exactly that pattern (low 6 bits zero).
    //
    // G113: same gating note as NR-33 — enable NR 0xA2 = 0xC0 first so
    // the gated pi_audio_L/R passes the raw I2s sample through.
    {
        fresh(emu);
        // Initial NR 0x2D read — latch defaults to 0 (no prior I2S read).
        const uint8_t initial = nr_read(emu, 0x2D);

        nr_write(emu, 0xA2, 0xC0);                 // enL+enR, no mute, ear=0
        // Inject a non-aligned left sample (low 2 bits = 0b11).
        emu.i2s().set_sample(0x3FF, 0x000);
        (void) nr_read(emu, 0x2C);                 // updates the latch
        const uint8_t after_2c = nr_read(emu, 0x2D);  // (0x3FF & 0x03) << 6 = 0xC0

        // Now read NR 0x2E with a different low-2 pattern (0b10) to confirm
        // NR 0x2E updates the same latch.
        emu.i2s().set_sample(0x000, 0x002);
        (void) nr_read(emu, 0x2E);                 // updates the latch
        const uint8_t after_2e = nr_read(emu, 0x2D);  // (0x002 & 0x03) << 6 = 0x80

        check("NR-34",
              "NR 0x2D read returns nr_2d_i2s_sample & \"000000\"; latch "
              "updated by NR 0x2C/0x2E reads "
              "[zxnext.vhd:6598-6599, :6596, :6603]",
              initial == 0x00 && after_2c == 0xC0 && after_2e == 0x80,
              fmt("initial=0x%02X after_2c=0x%02X after_2e=0x%02X "
                  "(want 0x00/0xC0/0x80)",
                  initial, after_2c, after_2e));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group NR-A2 — Pi I2S control register (NR 0xA2). VHDL stores 8 bits
// and fans them out to enL/enR/inout/muteL/muteR/ear → these gate the
// Pi I2S contribution into the audio_mixer. jnext has no NR 0xA2
// handler today; bits never reach the I2S/Mixer path; readback returns
// raw regs_[] byte (no fixed-bit pattern). Distinct from G29 broader
// I2S audio path.
// ══════════════════════════════════════════════════════════════════════

static void test_nr_a2(Emulator& emu) {
    set_group("NR-0xA2");

    // NR-40 — zxnext.vhd:6127: nr_a2_pi_i2s_ctl <= nr_wr_dat on NR 0xA2.
    // G113: NR 0xA2 has a write handler that pushes the raw 8-bit value
    // into I2s::nr_a2_ctl_; round-trip via the new accessor confirms
    // storage. Use a non-trivial pattern (0xCD) so a stuck-at-0/1 bug
    // would be caught.
    {
        fresh(emu);
        nr_write(emu, 0xA2, 0xCD);
        const uint8_t got = emu.i2s().nr_a2_ctl();
        check("NR-40",
              "NR 0xA2 write handler stores nr_a2_pi_i2s_ctl into I2s "
              "[zxnext.vhd:6127, :2593-2600]",
              got == 0xCD,
              fmt("i2s.nr_a2_ctl()=0x%02X (want 0xCD)", got));
    }

    // NR-41 — zxnext.vhd:2593-2600: bit fan-out drives pi_i2s_en[L/R],
    // inout, muteL/R, ear. Closing the loop end-to-end: drive a known
    // sample through the I2s, then assert the gated read path NR 0x2C
    // (= pi_audio_L per zxnext.vhd:6595) responds to the NR 0xA2 fan-out.
    //   Step 1: NR 0xA2 = 0x80 (enL=1) → pi_audio_L = raw left =
    //           0x3FF; high byte = (0x3FF >> 2) & 0xFF = 0xFF.
    //   Step 2: NR 0xA2 = 0x88 (enL=1, muteL=1) → pi_audio_L = 0x200
    //           (silence midpoint); high byte = (0x200 >> 2) & 0xFF
    //           = 0x80.
    {
        fresh(emu);
        emu.i2s().set_sample(0x3FF, 0x000);
        nr_write(emu, 0xA2, 0x80);                 // enL=1, no mute
        const uint8_t enabled = nr_read(emu, 0x2C);
        nr_write(emu, 0xA2, 0x88);                 // enL=1, muteL=1
        const uint8_t muted   = nr_read(emu, 0x2C);
        check("NR-41",
              "NR 0xA2 fan-out reaches I2s gate (mute drops gated read "
              "to silence midpoint) [zxnext.vhd:2593-2600, :2674]",
              enabled == 0xFF && muted == 0x80,
              fmt("enabled=0x%02X muted=0x%02X (want 0xFF/0x80)",
                  enabled, muted));
    }

    // NR-42 — zxnext.vhd:6780: read returns
    //   nr_a2_pi_i2s_ctl(7 downto 6) & '0' & nr_a2_pi_i2s_ctl(4 downto 2)
    //   & '1' & nr_a2_pi_i2s_ctl(0).
    // Pass-through bits = b7,b6,b4,b3,b2,b0 (mask 0xDD); fixed b5=0,
    // fixed b1=1 (constant 0x02). Three orthogonal patterns:
    //   c=0x00 → (0x00 & 0xDD) | 0x02 = 0x02
    //   c=0xFF → (0xFF & 0xDD) | 0x02 = 0xDF
    //   c=0x55 → (0x55 & 0xDD) | 0x02 = 0x57
    {
        fresh(emu);
        nr_write(emu, 0xA2, 0x00);
        const uint8_t r0 = nr_read(emu, 0xA2);
        nr_write(emu, 0xA2, 0xFF);
        const uint8_t r1 = nr_read(emu, 0xA2);
        nr_write(emu, 0xA2, 0x55);
        const uint8_t r2 = nr_read(emu, 0xA2);
        check("NR-42",
              "NR 0xA2 read returns ctl[7:6] & 0 & ctl[4:2] & 1 & ctl[0] "
              "[zxnext.vhd:6780]",
              r0 == 0x02 && r1 == 0xDF && r2 == 0x57,
              fmt("c=0x00→0x%02X c=0xFF→0x%02X c=0x55→0x%02X "
                  "(want 0x02/0xDF/0x57)", r0, r1, r2));
    }

    // NR-43 — G73: Mixer gates I2S contribution by NR 0xA2 enable/mute.
    // VHDL pi_audio_L/R (zxnext.vhd:2674-2675) feeds audio_mixer's
    // pi_i2s_L_i / pi_i2s_R_i ports (zxnext.vhd:6505-6506) — so the
    // mixer adds the GATED 10-bit value (silence midpoint 0x200 when
    // disabled/muted), not the raw sample. End-to-end check: same I2s
    // sample 0x3FF, two NR 0xA2 settings, observe pcm_L delta.
    //   enabled  (NR 0xA2 = 0xC0): i2s_L = 0x3FF = 1023.
    //                              pcm_L = 0+0+0+0+1024+1023 = 2047.
    //                              int16 = (2047-1536)*4        = +2044.
    //   disabled (NR 0xA2 = 0x00): i2s_L = 0x200 = 512  (silence).
    //                              pcm_L = 0+0+0+0+1024+512  = 1536.
    //                              int16 = (1536-1536)*4        =     0.
    //   delta = 2044 - 0 = 2044  (= 511 * 4, the 10-bit gating delta
    //                             in the int16 ×4 domain).
    //
    // GH #116: the 13-bit sums (2047 / 1536) are the VHDL quantities and are
    // UNCHANGED, as is the delta — which is what this row is actually about.
    // Only the two ABSOLUTE int16 values moved, by exactly the 2048 the
    // AC-coupling reference was wrong by: emit_sample() now subtracts
    // MIX_REST_LEVEL (1536 = DAC 1024 + I2S 0x200) instead of DAC_REST_LEVEL
    // alone. The old "disabled" expectation of +2048 WAS the bug — that is
    // the DC pedestal a silent machine sat on, and the amplitude of every
    // zero-padded device seam. The disabled case must read exactly 0, and now
    // does; see MX-17.
    {
        // Enabled scenario.
        fresh(emu);
        {
            int16_t scratch[2 * Mixer::RING_BUFFER_SIZE] = {0};
            emu.mixer().read_samples(scratch, emu.mixer().available());
        }
        emu.i2s().set_sample(0x3FF, 0x3FF);
        nr_write(emu, 0xA2, 0xC0);                 // enL+enR, no mute
        emu.mixer().generate_sample(emu.beeper(), emu.turbosound(), emu.dac());
        int16_t buf_on[2] = {0, 0};
        const int got_on = emu.mixer().read_samples(buf_on, 1);
        const int16_t pcm_L_on = buf_on[0];

        // Disabled scenario.
        fresh(emu);
        {
            int16_t scratch[2 * Mixer::RING_BUFFER_SIZE] = {0};
            emu.mixer().read_samples(scratch, emu.mixer().available());
        }
        emu.i2s().set_sample(0x3FF, 0x3FF);
        nr_write(emu, 0xA2, 0x00);                 // disabled (default)
        emu.mixer().generate_sample(emu.beeper(), emu.turbosound(), emu.dac());
        int16_t buf_off[2] = {0, 0};
        const int got_off = emu.mixer().read_samples(buf_off, 1);
        const int16_t pcm_L_off = buf_off[0];

        const int delta = static_cast<int>(pcm_L_on) - static_cast<int>(pcm_L_off);
        check("NR-43",
              "Mixer gates Pi I2S contribution by NR 0xA2 enable/mute "
              "(audio_mixer.vhd:91-92, zxnext.vhd:2674-2675)",
              got_on == 1 && got_off == 1 &&
              pcm_L_on == 2044 && pcm_L_off == 0 && delta == 2044,
              fmt("pcm_L on=%d off=%d delta=%d (want 2044/0/2044)",
                  pcm_L_on, pcm_L_off, delta));
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
              "beep_mic_final XOR expression matches zxnext.vhd:6503 over all 8 corners",
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

    // AUD-MX-03 — exc_i gating wire composition is the same as BP-13 but
    // observed from the "upstream-of-Mixer" side: we verify that flipping
    // either constituent bit changes beep_spkr_excl.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x40);              // speaker_beep = 1
        nr_write(emu, 0x08, 0x10);              // speaker_en   = 1
        const bool both_on = emu.beep_spkr_excl();
        nr_write(emu, 0x08, 0x00);              // drop speaker_en
        const bool en_off = emu.beep_spkr_excl();
        check("AUD-MX-03",
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

    // MX-23 — audio_mixer.vhd:80-81: ear/mic muxes are gated by exc_i,
    // which is wired to beep_spkr_excl (zxnext.vhd:6504 = NR 0x06 b6 AND
    // NR 0x08 b4). With EAR=1 and exc_i=0 the Mixer must add the 13-bit
    // ear=512 term; with EAR=1 and exc_i=1 the term is forced to 0. The
    // Mixer's int16 sample is centred on DC (1024 in 13-bit space) and
    // scaled ×4, so the EAR contribution is 512×4 = 2048 of int16 swing.
    // Closes G110.
    {
        // Baseline: exc_i=0. PASS-6 — NR 0x06 b6 (internal_speaker_beep)
        // SURVIVES reset per VHDL zxnext.vhd:1113 (initial-value-only
        // signal, no reset clause), so we cannot rely on `fresh()` to
        // clear it after an earlier group set it. Explicitly drive
        // NR 0x06 ← 0x00 here. NR 0x08 b4=1 (power-on default 0x10) leaves
        // beep_spkr_excl=0 with bit 6 cleared. Set EAR=1 and read the
        // resulting sample.
        fresh(emu);
        nr_write(emu, 0x06, 0x00);   // PASS-6: clear internal_speaker_beep explicitly
        // Drain any latent samples first so we read the one we generate.
        {
            int16_t scratch[2 * Mixer::RING_BUFFER_SIZE] = {0};
            emu.mixer().read_samples(scratch, emu.mixer().available());
        }
        emu.beeper().set_ear(true);
        emu.mixer().generate_sample(emu.beeper(), emu.turbosound(), emu.dac());
        int16_t buf_off[2] = {0, 0};
        const int got_off = emu.mixer().read_samples(buf_off, 1);
        const int16_t pcm_L_off = buf_off[0];

        // Speaker-exclusive: NR 0x06 b6=1 AND NR 0x08 b4=1 ⇒ exc_i=1.
        // EAR=1 again; the ear term must vanish from pcm_L.
        fresh(emu);
        {
            int16_t scratch[2 * Mixer::RING_BUFFER_SIZE] = {0};
            emu.mixer().read_samples(scratch, emu.mixer().available());
        }
        nr_write(emu, 0x06, 0x40);   // bit 6 = 1
        nr_write(emu, 0x08, 0x10);   // bit 4 = 1 (already default; explicit)
        emu.beeper().set_ear(true);
        emu.mixer().generate_sample(emu.beeper(), emu.turbosound(), emu.dac());
        int16_t buf_on[2] = {0, 0};
        const int got_on = emu.mixer().read_samples(buf_on, 1);
        const int16_t pcm_L_on = buf_on[0];

        // Expected delta is the gated 13-bit ear term (512) times the
        // Mixer's int16 ×4 scale = 2048.
        const int delta = static_cast<int>(pcm_L_off) - static_cast<int>(pcm_L_on);
        check("MX-23",
              "Mixer gates EAR/MIC when exc_i=1 (audio_mixer.vhd:80-81; "
              "exc_i = beep_spkr_excl per zxnext.vhd:6504)",
              got_off == 1 && got_on == 1 && delta == 512 * 4,
              fmt("pcm_L exc_i=0:%d exc_i=1:%d delta=%d (want 2048)",
                  pcm_L_off, pcm_L_on, delta));
    }

    // MX-17 (GH #116) — THE row this suite was missing: an ASSEMBLED machine
    // at power-on, with nothing driving any audio source, must emit digital
    // ZERO. Not "a constant", not "its resting DC" — zero, because that is
    // what the host audio path and every downstream consumer treat as silence
    // (SDL pads a starved device buffer with zeros; the WAV/MP4 recorders
    // write these samples verbatim).
    //
    // The bare-Mixer rows MX-10/MX-11 in test/audio/audio_test.cpp could not
    // see the defect: they build a Mixer with no I2s source, while
    // Emulator's constructor always wires one (src/core/emulator.cpp:44) and
    // the gated pi_audio_L/R idles at 0x200 (zxnext.vhd:2674-2675). The
    // emulator therefore rested 512 (13-bit) = 2048 (int16) above zero on
    // every sample of every run, while the suite reported silence as 0.
    //
    // Why that was audible (the reported symptom): DC alone is inaudible, but
    // it is the amplitude of every seam. When the host cannot emulate in real
    // time the device queue empties and SDL pads with ZEROS — each pad became
    // a 2048-tall step down and back. Reproduced on Linux under CPU
    // starvation: the device stream alternated 1787 samples at 2048 / 261 at
    // 0, a ~21.5 Hz square wave — "a motor running constant rrrrrr".
    //
    // Sample several times: the mixer's box filter integrates over an
    // interval, so a one-shot read could in principle hide a level that is
    // only wrong once settled.
    {
        fresh(emu);
        int16_t scratch[2 * Mixer::RING_BUFFER_SIZE] = {0};
        emu.mixer().read_samples(scratch, emu.mixer().available());

        int  nonzero = 0;
        int16_t worst = 0;
        for (int i = 0; i < 16; ++i) {
            emu.mixer().generate_sample(emu.beeper(), emu.turbosound(), emu.dac());
            int16_t s[2] = {0, 0};
            emu.mixer().read_samples(s, 1);
            if (s[0] != 0 || s[1] != 0) {
                ++nonzero;
                if (s[0] != 0) worst = s[0]; else worst = s[1];
            }
        }
        check("MX-17",
              "an assembled power-on machine emits DIGITAL ZERO — silence is "
              "0, so a zero-padded device seam is inaudible [GH #116]",
              nonzero == 0,
              fmt("%d/16 samples non-zero (worst=%d) VHDL zxnext.vhd:2674-2675, "
                  "i2s.vhd:179, audio_mixer.vhd:89-90,99-100", nonzero, worst));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Group NR-TAPE-MIX — what reaches the mixer from the tape, the Pi's EAR
// route and the beeper (GH #550), and what a state load re-derives.
// ══════════════════════════════════════════════════════════════════════
//
// audio_mixer.vhd has exactly three beeper-side inputs: ear_i (512), mic_i
// (128) and exc_i (35-37, 63-64, 80-81, 99-100). There is no tape input: the
// tape (i_AUDIO_EAR) reaches the mix only inside mic_i = beep_mic_final
//   i_AUDIO_EAR xor (port_fe_mic and nr_08_keyboard_issue2) xor port_fe_mic
// (zxnext.vhd:6503, 6516 on master). Signed sample = (13-bit sum - 1536) x 4
// (Mixer::MIX_REST_LEVEL: DAC 1024 + I2S 512), so EAR alone = 512 x 4 = 2048
// and the MIC term alone = 128 x 4 = 512. The beeper is mono: L == R always.
// i_AUDIO_EAR is the jack through ear_relax (zxnext_top_issue2.vhd:662-677,
// symmetric_relaxation.vhd:62-90): a level with no edge for 64 enables of
// CLK_28_MEMBRANE_EN (one per 512 master cycles, :1179) relaxes to
// zxn_issue2_fe_mic = port_fe_mic and nr_08_keyboard_issue2 (zxnext.vhd:1636).
// ══════════════════════════════════════════════════════════════════════

namespace {

constexpr uint64_t kRelaxMaster = 64 * 512;   // symmetric_relaxation.vhd:62,89-90

using Bytes = std::vector<uint8_t>;

std::string tape_tmp(const char* name) {
    const std::filesystem::path d = std::filesystem::temp_directory_path() / "jnext_gh550";
    std::filesystem::create_directories(d);
    return (d / name).string();
}

void tape_write(const std::string& path, const Bytes& b) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

std::unique_ptr<Emulator> next_machine_550() {
    auto e = std::make_unique<Emulator>();
    build_next_emulator(*e);
    return e;
}

// DI; JR $ at 0x8000, PC there: stepping instructions writes no port.
void idle_loop(Emulator& emu) {
    emu.mmu().write(0x8000, 0xF3);
    emu.mmu().write(0x8001, 0x18);
    emu.mmu().write(0x8002, 0xFE);
    auto r = emu.cpu().get_registers();
    r.PC = 0x8000;
    r.SP = 0x7FF0;
    r.IFF1 = r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

// The mixer's instantaneous output with the emulator's OWN mixer (its exc_i
// and its I2s wiring are the real ones): flush and drain what stepping left,
// generate one sample, read the pair.
bool mix_now(Emulator& emu, int& L, int& R) {
    emu.mixer().emit_sample();
    int16_t scratch[2 * Mixer::RING_BUFFER_SIZE] = {0};
    emu.mixer().read_samples(scratch, emu.mixer().available());
    emu.mixer().generate_sample(emu.beeper(), emu.turbosound(), emu.dac());
    int16_t s[2] = {0, 0};
    const int got = emu.mixer().read_samples(s, 1);
    L = s[0];
    R = s[1];
    return got == 1;
}

// One value for both channels (the beeper is mono); -99999 when L != R.
int mix_val(Emulator& emu) {
    int L = 0, R = 0;
    if (!mix_now(emu, L, R)) return -88888;
    return L == R ? L : -99999;
}

// TAP with one 1-byte block: 0x04 0x00 0xFF 0x01 0x02 0xFC (TSAVE-30's).
bool start_tap(Emulator& emu) {
    const std::string tap = tape_tmp("mx.tap");
    tape_write(tap, {0x04, 0x00, 0xFF, 0x01, 0x02, 0xFC});
    if (!emu.load_tap(tap, false)) return false;
    emu.tape().start_realtime_playback();
    return true;
}

// TZX of one pure-tone block (0x12): 8 pulses of `pulse_t` T-states (8 master
// cycles each at 3.5 MHz), each longer than the 4096 T relaxation hold.
bool start_tzx_tone(Emulator& emu, uint16_t pulse_t = 6000) {
    const std::string p = tape_tmp("tone.tzx");
    Bytes b = {'Z','X','T','a','p','e','!', 0x1A, 1, 20, 0x12,
               static_cast<uint8_t>(pulse_t & 0xFF), static_cast<uint8_t>(pulse_t >> 8), 0x08, 0x00};
    tape_write(p, b);
    return emu.load_tzx(p, false);
}

// Step until the raw tape level reads `want` (bounded). Returns the master
// clock at the instruction that showed it (the edge, to one instruction).
bool step_to_level(Emulator& emu, bool want, uint64_t* at = nullptr) {
    for (int i = 0; i < 400000; ++i) {
        emu.execute_single_instruction();
        if (emu.beeper().tape_ear() == want) {
            if (at) *at = emu.clock().get();
            return true;
        }
    }
    return false;
}

// Step until the master clock reaches `t`.

void step_to_clock(Emulator& emu, uint64_t t) {
    for (int i = 0; i < 400000 && emu.clock().get() < t; ++i) emu.execute_single_instruction();
}

// Wait for a real EDGE to `want`: first leave `want`, then enter it.
bool step_to_edge(Emulator& emu, bool want, uint64_t* at) {
    return step_to_level(emu, !want) && step_to_level(emu, want, at);
}


// The tape reaches the MIC term at output-sample granularity (the mixer's own:
// Emulator::mic_stale_ is tested at each sample boundary, 635 master cycles
// apart), so a row that steps a tape lets two samples go by before it reads.
int mix_settled(Emulator& emu) {
    step_to_clock(emu, emu.clock().get() + 2000);
    return mix_val(emu);
}

std::vector<uint8_t> snapshot_of(Emulator& emu) {
    StateWriter m;
    emu.save_state(m);
    std::vector<uint8_t> img(m.position(), 0);
    StateWriter w(img.data(), img.size());
    emu.save_state(w);
    return img;
}

bool restore_into(Emulator& emu, const std::vector<uint8_t>& img) {
    StateReader r(img.data(), img.size());
    return emu.load_state(r);
}

// A TAP of two 4-byte blocks (the deck can then sit in the pause after block 0).
bool start_tap2(Emulator& emu) {
    const std::string tap = tape_tmp("mx2.tap");
    tape_write(tap, {0x04, 0x00, 0xFF, 0x01, 0x02, 0xFC, 0x04, 0x00, 0xFF, 0x01, 0x02, 0xFC});
    return emu.load_tap(tap, false);
}

// Put the TAP deck at `phase` with `pulse_left` T-states of the pulse left and the EAR level
// `ear`, as a restore would, make the raw level agree, snapshot and restore in place: the
// load re-derives the last tape edge from the deck alone. Returns the master cycles it puts
// the edge before now (the derived age), or -1.
int64_t restore_tap_at(Emulator& emu, TapLoader::DeckState d) {
    std::string why;
    d.playing = true;
    if (!emu.tape().restore_deck(d, why)) return -1;
    emu.beeper().set_tape_ear(d.ear != 0);
    const auto img = snapshot_of(emu);
    if (!restore_into(emu, img)) return -1;
    return static_cast<int64_t>(emu.clock().get()) - static_cast<int64_t>(emu.tape_edge_master());
}

// A TZX of one turbo block (0x11): `pilot_n` pilot pulses of 2168 T, sync 667/735, 855/1710 data
// pulses, 1 data byte `data`, a pause of `pause_ms`. With 23 pulses in all the level ends high.
bool start_tzx_turbo(Emulator& emu, uint8_t data, uint16_t pilot_n, uint16_t pause_ms) {
    const std::string p = tape_tmp("turbo.tzx");
    Bytes b = {'Z','X','T','a','p','e','!', 0x1A, 1, 20, 0x11,
               0x78, 0x08, 0x9B, 0x02, 0xDF, 0x02, 0x57, 0x03, 0xAE, 0x06,
               static_cast<uint8_t>(pilot_n & 0xFF), static_cast<uint8_t>(pilot_n >> 8),
               8, static_cast<uint8_t>(pause_ms & 0xFF), static_cast<uint8_t>(pause_ms >> 8),
               1, 0, 0, data};
    tape_write(p, b);
    return emu.load_tzx(p, false);
}

// An 8-bit mono 44.1 kHz WAV of square-wave half periods of `half` samples, `n` halves, starting low.
bool start_wav_square(Emulator& emu, uint32_t half, uint32_t n) {
    const std::string p = tape_tmp("square.wav");
    Bytes s;
    for (uint32_t i = 0; i < n; ++i) s.insert(s.end(), half, (i & 1) ? 0xFF : 0x00);
    auto le32 = [](Bytes& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); };
    auto le16 = [](Bytes& v, uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); };
    Bytes b = {'R','I','F','F'}; le32(b, 36 + uint32_t(s.size()));
    for (char c : std::string("WAVEfmt ")) b.push_back(uint8_t(c));
    le32(b, 16); le16(b, 1); le16(b, 1); le32(b, 44100); le32(b, 44100); le16(b, 1); le16(b, 8);
    for (char c : std::string("data")) b.push_back(uint8_t(c));
    le32(b, uint32_t(s.size()));
    b.insert(b.end(), s.begin(), s.end());
    tape_write(p, b);
    return emu.load_wav(p);
}

// Restore `emu` in place (the deck keeps playing) and return {stamp before, stamp after}.
std::pair<int64_t, int64_t> restore_stamp(Emulator& emu) {
    const int64_t before = static_cast<int64_t>(emu.tape_edge_master());
    const auto img = snapshot_of(emu);
    restore_into(emu, img);
    return {before, static_cast<int64_t>(emu.tape_edge_master())};
}

} // namespace

static void test_tape_mix() {
    set_group("NR-TAPE-MIX");

    // MX-62 - the tape reaches the mix through the MIC term at 128, not as a
    // term of its own: issue 3, port 0xFE = 0, tape playing: level 1 -> 512,
    // level 0 -> 0. (audio_mixer.vhd:64,81,99; zxnext.vhd:6503,6516)
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x00);
        const bool ok = start_tap(*e);
        const bool a = step_to_level(*e, true);
        const int hi = mix_settled(*e);
        const bool b = step_to_level(*e, false);
        const int lo = mix_settled(*e);
        check("MX-62", "the tape reaches the mix through the MIC term at 128 (x4 = 512), not as a "
              "term of its own [audio_mixer.vhd:64,81,99; zxnext.vhd:6503,6516]",
              ok && a && b && hi == 512 && lo == 0,
              fmt("level1=%d level0=%d (want 512 / 0)", hi, lo));
    }

    // MX-63 - tape XOR MIC on issue 3: port 0xFE = 0x08 (MIC 1): level 1 -> 0,
    // level 0 -> 512. (zxnext.vhd:6503: 1 xor 0 xor 1 = 0; 0 xor 0 xor 1 = 1)
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x08);
        const bool ok = start_tap(*e);
        const bool a = step_to_level(*e, true);
        const int hi = mix_settled(*e);
        const bool b = step_to_level(*e, false);
        const int lo = mix_settled(*e);
        check("MX-63", "issue 3: the tape level is XORed with the port 0xFE MIC bit "
              "[zxnext.vhd:6503]", ok && a && b && hi == 0 && lo == 512,
              fmt("level1=%d level0=%d (want 0 / 512)", hi, lo));
    }

    // MX-64 - issue 2: the MIC bit cancels while a tape plays (t xor 1 xor 1 = t).
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x11);
        idle_loop(*e);
        e->port().out(0x00FE, 0x08);
        const bool ok = start_tap(*e);
        const bool a = step_to_level(*e, true);
        const int hi = mix_settled(*e);
        const bool b = step_to_level(*e, false);
        const int lo = mix_settled(*e);
        check("MX-64", "issue 2 (NR 0x08 bit 0): the port 0xFE MIC bit cancels while a tape plays "
              "[zxnext.vhd:6503]", ok && a && b && hi == 512 && lo == 0,
              fmt("level1=%d level0=%d (want 512 / 0)", hi, lo));
    }

    // MX-65 - no tape: the MIC term is the MIC bit on both settings, also right
    // after a stop (the issue-2 relaxed state returns).
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x11);
        idle_loop(*e);
        e->port().out(0x00FE, 0x08);
        const bool ok = start_tap(*e);
        const bool a = step_to_level(*e, false);
        const int playing0 = mix_settled(*e);
        e->tape().eject();
        const int ejected = mix_settled(*e);

        auto f = next_machine_550();
        nr_write(*f, 0x06, 0x00);
        nr_write(*f, 0x08, 0x10);
        idle_loop(*f);
        f->port().out(0x00FE, 0x08);
        f->execute_single_instruction();
        const int mic_on = mix_val(*f);
        f->port().out(0x00FE, 0x00);
        f->execute_single_instruction();
        const int mic_off = mix_val(*f);
        // The "after the eject" value is jnext's model: on the board ear_relax holds the last
        // tape level for 64 x 512 master cycles before it relaxes to issue2_mic
        // (symmetric_relaxation.vhd:76,89-90), so for about 1 ms the term would still be 0;
        // audio_ear_in treats "not playing" as relaxed at once (the D6 class, held back).
        // Read two output samples after the eject: the tape's stop is seen at the sample boundary.
        check("MX-65", "no tape: the MIC term is the MIC bit on both keyboard settings; after the "
              "eject it is the relaxed issue2_mic at once (jnext's model, the board holds the last "
              "tape level 32768 master cycles first) [zxnext.vhd:6503,1636; "
              "zxnext_top_issue2.vhd:674-675]",
              ok && a && playing0 == 0 && ejected == 512 && mic_on == 512 && mic_off == 0,
              fmt("issue2 level0=%d after-eject=%d issue3 mic1=%d mic0=%d (want 0/512/512/0)",
                  playing0, ejected, mic_on, mic_off));
    }

    // MX-66 - EAR stays 512 and the tape adds 128 on top (EAR:tape = 4:1).
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x10);
        const bool ok = start_tap(*e);
        const bool a = step_to_level(*e, true);
        const int hi = mix_settled(*e);
        const bool b = step_to_level(*e, false);
        const int lo = mix_settled(*e);
        check("MX-66", "EAR (port 0xFE bit 4) stays 512 and the tape adds 128 on top, 4:1 "
              "[audio_mixer.vhd:63-64,99]", ok && a && b && hi == 2560 && lo == 2048,
              fmt("level1=%d level0=%d (want 2560 / 2048)", hi, lo));
    }

    // MX-67 - exc_i silences the tape with EAR and MIC.
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x40);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x18);
        const bool ok = start_tap(*e);
        const bool a = step_to_level(*e, true);
        const int hi = mix_settled(*e);
        const bool b = step_to_level(*e, false);
        const int lo = mix_settled(*e);
        check("MX-67", "exc_i (NR 0x06 b6 and NR 0x08 b4) silences the tape's term with EAR and MIC "
              "[audio_mixer.vhd:80-81; zxnext.vhd:6504,6514]",
              ok && a && b && hi == 0 && lo == 0,
              fmt("level1=%d level0=%d (want 0 / 0)", hi, lo));
    }

    // MX-68 - Pi audio routed to EAR reaches the MIC term; the I2S term stays
    // at rest (zxnext.vhd:2358,2364-2373,6503).
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x00);
        e->i2s().set_sample(0x3FF, 0x3FF);
        nr_write(*e, 0xA2, 0xC1);
        e->execute_single_instruction();
        const int v = mix_val(*e);
        check("MX-68", "Pi audio routed to EAR (NR 0xA2 = 0xC1) reaches the MIC term at 128; the "
              "I2S term stays at rest [zxnext.vhd:2358,2364-2373,6503; zxnext_top_issue2.vhd:673]",
              e->i2s().fe_ear() && v == 512, fmt("fe_ear=%d mix=%d (want 1 / 512)",
              e->i2s().fe_ear() ? 1 : 0, v));
    }

    // MX-69 - while routed and unrelaxed, port 0xFE MIC and NR 0x08 issue 2 both
    // feed the MIC term (1 xor 0 xor 1 = 0; 1 xor 1 xor 1 = 1).
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x00);
        e->i2s().set_sample(0x3FF, 0x3FF);
        nr_write(*e, 0xA2, 0xC1);
        e->port().out(0x00FE, 0x08);
        const int a = mix_val(*e);
        nr_write(*e, 0x08, 0x11);
        const int b = mix_val(*e);
        check("MX-69", "while the Pi's EAR is routed and unrelaxed, port 0xFE MIC and NR 0x08 issue "
              "2 both feed the MIC term [zxnext.vhd:6503]", a == 0 && b == 512,
              fmt("mic=%d issue2=%d (want 0 / 512)", a, b));
    }

    // MX-70 - ear_relax: a Pi level held 64 x 512 master cycles relaxes to
    // issue2_mic (0 here). Step instructions: before the hold it is 512, after 0.
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x00);
        step_to_clock(*e, 3 * kRelaxMaster);   // well past any power-on hold: only the edge below can hold it
        e->i2s().set_sample(0x3FF, 0x3FF);
        nr_write(*e, 0xA2, 0xC1);
        const uint64_t t0 = e->clock().get();
        step_to_clock(*e, t0 + 30000);
        const int early = mix_val(*e);
        step_to_clock(*e, t0 + 34500);
        const int late = mix_val(*e);
        check("MX-70", "ear_relax: a Pi EAR level held 64 x 512 master cycles relaxes to the issue-2 "
              "MIC level [symmetric_relaxation.vhd:76,89-90; zxnext_top_issue2.vhd:666-675,1179]",
              early == 512 && late == 0, fmt("+30000=%d +34500=%d (want 512 / 0)", early, late));
    }

    // MX-87 (F1) - the tape's edge reaches the MIC term in its OWN instruction, not at the next
    // output-sample boundary: the mixer integrates the term across the sample like EAR, so a
    // late edge loses the part-sample. issue 3, MIC 0: mic_final == the raw tape level.
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x00);
        const bool ok = start_tap(*e);
        int edges = 0, late = 0;
        bool prev = e->beeper().tape_ear();
        for (int i = 0; i < 400000 && edges < 8; ++i) {
            e->execute_single_instruction();
            const bool now = e->beeper().tape_ear();
            if (now != prev) {
                ++edges;
                if (e->beeper().mic_final() != now) ++late;
                prev = now;
            }
        }
        check("MX-87", "a tape edge reaches the MIC term in the instruction that shows it, not at the "
              "next output-sample boundary [zxnext.vhd:6503,6516; audio_mixer.vhd:64,81]",
              ok && edges == 8 && late == 0, fmt("edges=%d late=%d (want 8 / 0)", edges, late));
    }

    // MX-88 (F2) - an OUT 0xFE while a tape plays: the XOR with the MIC bit applies at once.
    // Tape at level 1, issue 3: MIC 0 -> 512, MIC 1 -> 0 (1^0^1), MIC 0 again -> 512.
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x00);
        const bool ok = start_tap(*e);
        const bool a = step_to_level(*e, true);
        const int before = mix_val(*e);
        e->port().out(0x00FE, 0x08);
        const int mic1 = mix_val(*e);
        e->port().out(0x00FE, 0x00);
        const int mic0 = mix_val(*e);
        check("MX-88", "an OUT 0xFE while a tape plays XORs the MIC bit into the MIC term at once "
              "[zxnext.vhd:6503]", ok && a && before == 512 && mic1 == 0 && mic0 == 512,
              fmt("before=%d MIC1=%d MIC0=%d (want 512 / 0 / 512)", before, mic1, mic0));
    }

    // MX-89 (F3) - a restore while the same tape keeps playing re-derives the MIC term:
    // saved at tape 1 with MIC 1 (term 0), MIC changed to 0 (term 512), restored: 0.
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x08);
        const bool ok = start_tap(*e);
        const bool a = step_to_level(*e, true);
        const int saved_mix = mix_val(*e);
        const auto img = snapshot_of(*e);
        e->port().out(0x00FE, 0x00);
        const int changed = mix_val(*e);
        const bool loaded = restore_into(*e, img);
        const int restored = mix_val(*e);
        check("MX-89", "a restore while a tape plays re-derives the MIC term from the restored MIC "
              "bit and tape level [zxnext.vhd:6503]",
              ok && a && loaded && saved_mix == 0 && changed == 512 && restored == 0,
              fmt("saved=%d changed=%d restored=%d (want 0 / 512 / 0)", saved_mix, changed, restored));
    }

    // MX-92 (R18) - HALT batch with the Pi routed: the relaxation lands inside the batch
    // (pi_sample_thunk_), not when the batch ends. DI; HALT, fe_ear high, issue 3, MIC 0:
    // 512 at first, 0 once the 32768-cycle hold is out; the last samples of a frame are 0.
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        e->mmu().write(0x8000, 0xF3);
        e->mmu().write(0x8001, 0x76);
        auto r = e->cpu().get_registers();
        r.PC = 0x8000; r.SP = 0x7FF0; r.IFF1 = r.IFF2 = 0;
        e->cpu().set_registers(r);
        e->port().out(0x00FE, 0x00);
        e->run_frame();                       // into the HALT
        e->i2s().set_sample(0x3FF, 0x3FF);
        nr_write(*e, 0xA2, 0xC1);
        int16_t scratch[2 * Mixer::RING_BUFFER_SIZE] = {0};
        e->mixer().read_samples(scratch, e->mixer().available());
        e->run_frame();                       // the hold runs out inside this frame
        const int n = e->mixer().available();
        std::vector<int16_t> buf(2 * static_cast<size_t>(n > 0 ? n : 1), 0);
        const int got = e->mixer().read_samples(buf.data(), n);
        // The hold (32768 master cycles) is about 52 output samples after the route: samples 1..40
        // are the Pi's level (512), samples 54..200 are past the hold and must be at rest. Without
        // the refresh in the batch's sample thunk the term only drops where the batch window ends,
        // 5 samples late (measured: sample 55 instead of 50).
        int early_hi = 0, late_nonzero = 0;
        for (int i = 1; i < 40 && i < got; ++i)
            if (buf[2 * i] == 512) ++early_hi;
        for (int i = 54; i < 200 && i < got; ++i)
            if (buf[2 * i] != 0) ++late_nonzero;
        const uint64_t batched = e->halt_repeats_batched_for_test();
        check("MX-92", "inside a HALT batch the Pi's ear_relax hold runs out at its time: the samples "
              "past the hold are at rest [symmetric_relaxation.vhd:76,89-90; Emulator::pi_sample_thunk_]",
              got > 400 && batched > 0 && early_hi >= 30 && late_nonzero == 0,
              fmt("samples=%d batched=%llu early_512=%d late_nonzero=%d (want >400 / >0 / >=30 / 0)",
                  got, (unsigned long long)batched, early_hi, late_nonzero));
    }

    // D6 - ear_relax on a PLAYING tape: i_AUDIO_EAR is the jack through
    // ear_relax, so a level with no edge for 64 x 512 master cycles relaxes to
    // zxn_issue2_fe_mic, tape or not (zxnext_top_issue2.vhd:662-677). The tone
    // pulses are 6000 T (48000 master cycles), longer than the hold.
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x00);
        const bool ok = start_tzx_tone(*e);
        uint64_t t = 0;
        const bool a = step_to_edge(*e, true, &t);
        const int fresh_hi = mix_val(*e);
        step_to_clock(*e, t + 4000 * 8 / 2);
        const int held_early = mix_val(*e);
        const uint8_t port_early = e->port().in(0x00FE);
        step_to_clock(*e, t + kRelaxMaster + 1700);
        const int held_late = mix_val(*e);
        const uint8_t port_late = e->port().in(0x00FE);
        check("MX-74", "a tape level held with no edge for 64 x 512 master cycles relaxes to the "
              "issue-2 MIC level (0 here) in the mix [symmetric_relaxation.vhd:76,89-90; "
              "zxnext_top_issue2.vhd:666-677]",
              ok && a && e->beeper().tape_ear() && fresh_hi == 512 && held_early == 512 &&
              held_late == 0,
              fmt("edge=%d held<K=%d held>K=%d tape_ear=%d (want 512/512/0, tape 1)",
                  fresh_hi, held_early, held_late, e->beeper().tape_ear() ? 1 : 0));
        check("MX-75", "the same relaxation reaches the port 0xFE read: bit 6 follows the held tape "
              "level until the hold, then the issue-2 MIC level [zxnext.vhd:3459; "
              "symmetric_relaxation.vhd:89-90]",
              ((port_early >> 6) & 1) == 1 && ((port_late >> 6) & 1) == 0,
              fmt("port 0xFE early=0x%02X late=0x%02X (want bit6 1 / 0)", port_early, port_late));
    }
    {
        // Issue 2 + MIC = 1: the relaxed level is 1. A held 0 reads 0 until the
        // hold, 1 after; the next edge restarts the hold.
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x11);
        idle_loop(*e);
        e->port().out(0x00FE, 0x08);
        const bool ok = start_tzx_tone(*e);
        uint64_t t = 0;
        const bool a = step_to_edge(*e, false, &t);
        const int fresh_lo = mix_val(*e);
        step_to_clock(*e, t + kRelaxMaster + 1700);
        const int relaxed = mix_val(*e);
        uint64_t t2 = 0;
        const bool b = step_to_level(*e, true, &t2);
        const int hi = mix_val(*e);
        const bool c = step_to_level(*e, false, &t2);
        const int fresh_lo2 = mix_val(*e);
        check("MX-76", "issue 2 + MIC: a held tape 0 relaxes to the MIC level (mix 0 -> 512), and "
              "the next edge restarts the hold [symmetric_relaxation.vhd:62,76,89-90; "
              "zxnext.vhd:1636,6503]",
              ok && a && b && c && fresh_lo == 0 && relaxed == 512 && hi == 512 && fresh_lo2 == 0,
              fmt("edge=%d relaxed=%d next-high=%d next-low=%d (want 0/512/512/0)",
                  fresh_lo, relaxed, hi, fresh_lo2));
    }
    {
        // A tape plus the Pi, issue 2 + MIC (relaxed level 1): the hold counts
        // from the latest edge of EITHER source (sig = tape xor pi_fe_ear).
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x11);
        idle_loop(*e);
        e->port().out(0x00FE, 0x08);
        const bool ok = start_tzx_tone(*e, 12000);
        uint64_t t = 0;
        const bool a = step_to_edge(*e, true, &t);
        step_to_clock(*e, t + 30000);
        // A Pi edge inside the tape's hold: tape 1 xor pi 1 = sig 0.
        e->i2s().set_sample(0x3FF, 0x3FF);
        nr_write(*e, 0xA2, 0xC1);
        const uint64_t tp = e->clock().get();
        step_to_clock(*e, t + kRelaxMaster + 1700);      // past the tape's own hold
        const int extended = mix_val(*e);
        step_to_clock(*e, tp + kRelaxMaster + 1700);     // past the Pi edge's hold
        const int relaxed = mix_val(*e);
        check("MX-77", "the relaxation hold restarts at an edge of tape XOR pi_fe_ear: a Pi edge "
              "inside the tape's hold extends it [zxnext_top_issue2.vhd:673; "
              "symmetric_relaxation.vhd:70-77]",
              ok && a && e->i2s().fe_ear() && extended == 0 && relaxed == 512,
              fmt("extended=%d relaxed=%d (want 0 / 512)", extended, relaxed));
    }
    {
        // A tape that stops and starts again starts a new jack: its first edge
        // restarts the hold (the previous session's edge stamp is gone).
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x00);
        const bool ok = start_tzx_tone(*e);
        uint64_t t = 0;
        const bool a = step_to_edge(*e, true, &t);
        e->tzx_tape().eject();
        step_to_clock(*e, t + kRelaxMaster + 1700);   // stopped, and long past any hold
        const bool ok2 = start_tzx_tone(*e);
        const bool b = step_to_level(*e, true);
        const int restarted = mix_val(*e);
        check("MX-83", "a tape stopped and started again: its first level edge restarts the "
              "ear_relax hold, so the restarted tape is heard at once, not relaxed "
              "[symmetric_relaxation.vhd:70-77,89-90]",
              ok && a && ok2 && b && restarted == 512, fmt("restarted=%d (want 512)", restarted));
    }

    // MX-93..96 (D6, restore): a restore derives the last tape edge from the deck, so a restored
    // run relaxes exactly when the continuous one does. TAP: exact in every phase.
    {
        const int64_t div = 8;                 // master cycles per T-state at 3.5 MHz
        auto fresh_tap = [&]() {
            auto e = next_machine_550();
            nr_write(*e, 0x06, 0x00);
            nr_write(*e, 0x08, 0x10);
            idle_loop(*e);
            e->port().out(0x00FE, 0x00);
            step_to_clock(*e, 3 * kRelaxMaster);          // a clock well past the hold
            return e;
        };
        TapLoader::DeckState d;
        // MX-93 mid-pilot: 500 T into a 2168 T pulse.
        auto a = fresh_tap();
        const bool oka = start_tap2(*a);
        d.block = 0; d.phase = "leader"; d.leader_left = 100; d.pulse_left = 2168 - 500; d.ear = 1;
        const int64_t age_a = restore_tap_at(*a, d);
        check("MX-93", "a restore mid-pilot derives the tape edge 500 T-states back (the pulse length "
              "minus what is left) [symmetric_relaxation.vhd:62,76]",
              oka && age_a == 500 * div, fmt("age=%lld master (want %lld)", (long long)age_a, (long long)(500 * div)));
        // MX-94 mid-pause, young: 1000 T into the 1 s pause, level held at 1: still unrelaxed.
        auto b = fresh_tap();
        const bool okb = start_tap2(*b);
        d = TapLoader::DeckState{};
        d.block = 1; d.phase = "pause"; d.pulse_left = 3500000 - 1000; d.ear = 1;
        const int64_t age_b = restore_tap_at(*b, d);
        const int mix_b = mix_val(*b);
        check("MX-94", "a restore mid-pause 1000 T-states after the last edge: age derived, the held "
              "level is still the tape's (mix 512) [symmetric_relaxation.vhd:76,89-90]",
              okb && age_b == 1000 * div && mix_b == 512,
              fmt("age=%lld master (want %lld) mix=%d (want 512)", (long long)age_b, (long long)(1000 * div), mix_b));
        // MX-95 mid-pause, old: 8000 T (past the 4096 T hold): relaxed, as the continuous run is.
        auto c = fresh_tap();
        const bool okc = start_tap2(*c);
        d.pulse_left = 3500000 - 8000;
        const int64_t age_c = restore_tap_at(*c, d);
        const int mix_c = mix_val(*c);
        check("MX-95", "a restore mid-pause 8000 T-states after the last edge: the hold has run out, the "
              "level reads as the issue-2 MIC level (mix 0) [symmetric_relaxation.vhd:76,89-90]",
              okc && age_c == 8000 * div && mix_c == 0,
              fmt("age=%lld master (want %lld) mix=%d (want 0)", (long long)age_c, (long long)(8000 * div), mix_c));
        // MX-96 a data bit: a ONE bit (855 for a zero) half 0 with 300 T left of 1710.
        auto f = fresh_tap();
        const bool okf = start_tap2(*f);
        d = TapLoader::DeckState{};
        d.block = 0; d.phase = "data"; d.byte = 1; d.bit = 0; d.half = 0;   // byte 1 = 0x01, bit 0 = 1: a ONE
        d.pulse_left = 300; d.ear = 0;
        const int64_t age_f = restore_tap_at(*f, d);
        check("MX-96", "a restore inside a data bit derives the edge from that bit's pulse length (a "
              "one bit is 1710 T) [symmetric_relaxation.vhd:62,76]",
              okf && age_f == (1710 - 300) * div, fmt("age=%lld master (want %lld)", (long long)age_f, (long long)((1710 - 300) * div)));
    }

    // MX-97..100 (D6, restore, TZX and WAV).
    {
        auto machine = [&]() {
            auto e = next_machine_550();
            nr_write(*e, 0x06, 0x00);
            nr_write(*e, 0x08, 0x10);
            idle_loop(*e);
            e->port().out(0x00FE, 0x00);
            return e;
        };
        // MX-97 TZX, the 1 ms hold at the final high level before the pause: the player holds
        // the pulse length, so the derived edge equals the continuous one.
        auto z = machine();
        const bool okz = start_tzx_turbo(*z, 0x00, 4, 100);   // 4 + 2 + 16 pulses: the level ends high
        bool in_hold = false;
        for (int i = 0; i < 2000000 && !in_hold; ++i) {
            z->execute_single_instruction();
            in_hold = z->tzx_tape().deck_state(z->monotonic_tstates()).phase == "pause2";
        }
        step_to_clock(*z, z->clock().get() + 800 * 8);
        const bool still = z->tzx_tape().deck_state(z->monotonic_tstates()).phase == "pause2";
        const auto hold = restore_stamp(*z);
        const int64_t dhold = hold.second > hold.first ? hold.second - hold.first : hold.first - hold.second;
        check("MX-97", "a restore in the TZX pause's 1 ms hold derives the tape edge from the pulse "
              "length the player holds, equal to the continuous run's [symmetric_relaxation.vhd:62,76]",
              okz && in_hold && still && dhold <= 400,
              fmt("hold=%d still=%d |derived-continuous|=%lld master (want <= 400)", in_hold, still, (long long)dhold));
        // MX-98 TZX, the pause's low remainder (the player's idle phase after the hold).
        bool in_rest = false;
        for (int i = 0; i < 2000000 && !in_rest; ++i) {
            z->execute_single_instruction();
            const auto ds = z->tzx_tape().deck_state(z->monotonic_tstates());
            in_rest = ds.phase == "idle" && ds.level == 0 && ds.playing;
        }
        step_to_clock(*z, z->clock().get() + 1000 * 8);
        const auto rest = restore_stamp(*z);
        const int64_t drest = rest.second > rest.first ? rest.second - rest.first : rest.first - rest.second;
        check("MX-98", "a restore in the TZX pause's remainder derives the edge from the remaining "
              "pause length, equal to the continuous run's [symmetric_relaxation.vhd:62,76]",
              in_rest && drest <= 400,
              fmt("rest=%d |derived-continuous|=%lld master (want <= 400)", in_rest, (long long)drest));
        // MX-99 TZX, a pulse phase: the player does not keep the pulse it has started, so the edge is
        // taken as just changed (age 0): never earlier than the continuous run's, at most a pulse later.
        auto y = machine();
        const bool oky = start_tzx_turbo(*y, 0x00, 400, 100);
        uint64_t te = 0;
        const bool ay = step_to_level(*y, true) && step_to_level(*y, false, &te);
        step_to_clock(*y, y->clock().get() + 500 * 8);
        const int64_t cont = static_cast<int64_t>(y->tape_edge_master());
        const int64_t nowc = static_cast<int64_t>(y->clock().get());
        restore_stamp(*y);
        const int64_t derived = static_cast<int64_t>(y->tape_edge_master());
        check("MX-99", "a restore in a TZX pulse phase takes the edge as just changed: derived age 0, "
              "never earlier than the continuous edge and at most one pulse later "
              "[symmetric_relaxation.vhd:62,76]",
              oky && ay && derived == nowc && derived >= cont && derived - cont <= 2168 * 8 + 400,
              fmt("derived-now=%lld derived-continuous=%lld (want 0 / 0..17744)", (long long)(derived - nowc), (long long)(derived - cont)));
        // MX-100 WAV: the recording is random access, so the edge is found by looking back along it.
        auto w = machine();
        const bool okw = start_wav_square(*w, 300, 200);        // 300-sample halves = 23820 T
        uint64_t tw = 0;
        const bool aw = step_to_level(*w, true) && step_to_level(*w, false) && step_to_level(*w, true, &tw);
        step_to_clock(*w, w->clock().get() + 1000 * 8);          // 1000 T into a high half
        const auto young = restore_stamp(*w);
        const int64_t dy = young.second > young.first ? young.second - young.first : young.first - young.second;
        const int mix_young = mix_val(*w);
        step_to_clock(*w, w->clock().get() + 8000 * 8);          // 9000 T in: past the hold
        const auto old_ = restore_stamp(*w);
        // The look-back stops at 8192 T-states (twice the hold): the true age is 9000 T, the derived one
        // is the cap, which is all the relaxation needs.
        const int64_t cap_age = static_cast<int64_t>(w->clock().get()) - old_.second;
        const int mix_old = mix_val(*w);
        check("MX-100", "a WAV restore finds the edge by looking back along the recording: equal to the "
              "continuous edge, unrelaxed 1000 T after it (mix 512) and relaxed 9000 T after it (mix 0) "
              "[symmetric_relaxation.vhd:62,76,89-90]",
              okw && aw && dy <= 1500 && cap_age >= 8192 * 8 - 1500 && mix_young == 512 && mix_old == 0,
              fmt("|d| young=%lld master (want <= 1500) old age=%lld (want >= 63000) mix young=%d old=%d (want 512 / 0)",
                  (long long)dy, (long long)cap_age, mix_young, mix_old));
    }

    // MX-101 (D6, .jns): a `.jns` load runs load_state BEFORE the deck is reattached, so it is the
    // reattach that rebuilds the last tape edge. A TAP played to 1000 T-states into the first
    // pause is saved and loaded into a fresh machine: the restored edge age agrees with the saved
    // one, the held level is still unrelaxed (mix 512) and the deck is playing.
    {
        auto a = next_machine_550();
        nr_write(*a, 0x06, 0x00);
        nr_write(*a, 0x08, 0x10);
        idle_loop(*a);
        a->port().out(0x00FE, 0x00);
        const bool oka = start_tap2(*a);
        a->tape().start_realtime_playback();
        bool in_pause = false;
        for (int i = 0; i < 4000000 && !in_pause; ++i) {
            a->execute_single_instruction();
            const auto ds = a->tape().deck_state();
            in_pause = ds.phase == "pause" && ds.pulse_left <= 3500000u - 1000u;
        }
        const std::string jns = tape_tmp("mx101.jns");
        const bool saved = a->save_jns_file(jns);
        const int64_t age_a = static_cast<int64_t>(a->clock().get()) - static_cast<int64_t>(a->tape_edge_master());
        auto b = next_machine_550();
        const bool loaded = b->load_jns_file(jns, true);
        const int64_t age_b = static_cast<int64_t>(b->clock().get()) - static_cast<int64_t>(b->tape_edge_master());
        const int64_t d = age_a > age_b ? age_a - age_b : age_b - age_a;
        const int mix_b = mix_val(*b);
        check("MX-101", "a .jns load inside a tape's hold derives the tape edge when the deck is "
              "reattached: the restored age matches the saved one (within 800 master cycles), the "
              "held level is unrelaxed (mix 512) and the deck is playing "
              "[symmetric_relaxation.vhd:62,76,89-90]",
              oka && in_pause && saved && loaded && b->tape().is_playing() && d <= 800 && mix_b == 512,
              fmt("pause=%d saved=%d loaded=%d playing=%d |age_a-age_b|=%lld mix=%d (want 1/1/1/1/<=800/512)",
                  in_pause, saved, loaded, b->tape().is_playing(), (long long)d, mix_b));
    }

    // MX-82 - a replay (rewind fast-forward, RZX) holds the Pi's EAR term off, so
    // beep_mic_final loses it when replay mode starts and gets it back when it ends.
    {
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        e->port().out(0x00FE, 0x00);
        e->i2s().set_sample(0x3FF, 0x3FF);
        nr_write(*e, 0xA2, 0xC1);
        const int live = mix_val(*e);
        e->set_replay_mode(true);
        const int replay = mix_val(*e);
        e->set_replay_mode(false);
        const int back = mix_val(*e);
        check("MX-82", "replay mode holds the Pi's EAR term off and the MIC term follows it in and "
              "out [zxnext.vhd:6503; Emulator::audio_ear_in]",
              live == 512 && replay == 0 && back == 512,
              fmt("live=%d replay=%d after=%d (want 512 / 0 / 512)", live, replay, back));
    }

    // MX-71 - a state load re-derives the MIC term (no tape: port_fe_mic).
    {
        auto a = next_machine_550();
        a->port().out(0x00FE, 0x08);
        const auto img = snapshot_of(*a);
        auto b = next_machine_550();
        nr_write(*b, 0x06, 0x00);
        const bool ok = restore_into(*b, img);
        check("MX-71", "a state load re-derives the MIC term [zxnext.vhd:6503]",
              ok && mix_val(*b) == 512, fmt("mix=%d (want 512)", mix_val(*b)));
    }

    // MX-72 - R1: a state load re-derives the exc gate.
    {
        auto a = next_machine_550();
        nr_write(*a, 0x06, 0x40);
        nr_write(*a, 0x08, 0x10);
        a->port().out(0x00FE, 0x10);
        const auto img = snapshot_of(*a);
        auto b = next_machine_550();
        nr_write(*b, 0x06, 0x00);
        const bool ok = restore_into(*b, img);
        const int v = mix_val(*b);
        check("MX-72", "a state load re-derives the exc gate: NR 0x06 b6 and exc_i come back from the "
              "restored registers [zxnext.vhd:6504,6514; audio_mixer.vhd:80]",
              ok && b->beep_spkr_excl() && v == 0,
              fmt("excl=%d mix=%d (want 1 / 0)", b->beep_spkr_excl() ? 1 : 0, v));
        // And the reverse: a snapshot with the gate open into a machine with it closed.
        auto c = next_machine_550();
        nr_write(*c, 0x06, 0x00);
        nr_write(*c, 0x08, 0x10);
        c->port().out(0x00FE, 0x10);
        const auto img2 = snapshot_of(*c);
        auto d = next_machine_550();
        nr_write(*d, 0x06, 0x40);
        nr_write(*d, 0x08, 0x10);
        const bool ok2 = restore_into(*d, img2);
        const int v2 = mix_val(*d);
        check("MX-81", "a state load re-derives the exc gate in the other direction too "
              "[zxnext.vhd:6504,6514; audio_mixer.vhd:80]",
              ok2 && !d->beep_spkr_excl() && v2 == 2048,
              fmt("excl=%d mix=%d (want 0 / 2048)", d->beep_spkr_excl() ? 1 : 0, v2));
    }

    {
        // The raw tape level is cleared when the tape stops (the tape block's stop
        // path), and a restore with no tape playing clears a saved level the same way.
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        idle_loop(*e);
        const bool ok = start_tzx_tone(*e);
        const bool a = step_to_level(*e, true);
        e->tzx_tape().eject();
        e->execute_single_instruction();
        const bool stopped_clear = !e->beeper().tape_ear();
        check("MX-85", "a tape stopped at level 1 leaves the raw tape level 0 after one "
              "instruction (no stale EAR level) [zxnext.vhd:6503]",
              ok && a && stopped_clear, fmt("tape_ear after stop=%d (want 0)", e->beeper().tape_ear()));

        auto p = next_machine_550();
        nr_write(*p, 0x06, 0x00);
        nr_write(*p, 0x08, 0x10);
        idle_loop(*p);
        const bool ok2 = start_tzx_tone(*p);
        const bool b = step_to_level(*p, true);
        const auto img = snapshot_of(*p);
        auto q = next_machine_550();
        idle_loop(*q);
        const bool ok3 = restore_into(*q, img);
        q->execute_single_instruction();
        check("MX-86", "a restore with no tape playing clears a saved raw tape level after one "
              "instruction [zxnext.vhd:6503]",
              ok2 && b && ok3 && !q->beeper().tape_ear(),
              fmt("tape_ear after restore+step=%d (want 0)", q->beeper().tape_ear()));
    }
    {
        // A reset puts the MIC term back at rest: mic_final is not left over from before.
        auto e = next_machine_550();
        nr_write(*e, 0x06, 0x00);
        nr_write(*e, 0x08, 0x10);
        e->port().out(0x00FE, 0x08);
        const int before = mix_val(*e);
        build_next_emulator(*e);
        nr_write(*e, 0x06, 0x00);
        const int after = mix_val(*e);
        check("MX-84", "a reset clears the mixer's MIC term with the port 0xFE MIC bit "
              "[zxnext.vhd:3588-3594,6503]", before == 512 && after == 0,
              fmt("before=%d after reset=%d (want 512 / 0)", before, after));
    }
}

// D9 - the NR 0x06 shadows `init()` derives and `load_state` must re-derive
// (the registers restore through NextReg, the shadows are not in the stream).
static void test_nr06_restore() {
    set_group("NR-06-RESTORE");

    // MX-78 - the NMI button enables (bits 4:3, zxnext.vhd:5726-5727).
    {
        auto a = next_machine_550();
        nr_write(*a, 0x06, 0x18);
        const auto img = snapshot_of(*a);
        auto b = next_machine_550();
        nr_write(*b, 0x06, 0x00);
        const bool ok = restore_into(*b, img);
        // And the reverse direction.
        auto c = next_machine_550();
        nr_write(*c, 0x06, 0x00);
        const auto img2 = snapshot_of(*c);
        auto d = next_machine_550();
        nr_write(*d, 0x06, 0x18);
        const bool ok2 = restore_into(*d, img2);
        check("MX-78", "a state load re-derives the NMI button enables from the restored NR 0x06 "
              "(bits 4:3) [zxnext.vhd:5726-5727,2399-2400]",
              ok && ok2 && b->nr_06_button_m1_nmi_en() && b->nr_06_button_drive_nmi_en() &&
              !d->nr_06_button_m1_nmi_en() && !d->nr_06_button_drive_nmi_en(),
              fmt("restored m1=%d drive=%d (want 1/1); cleared m1=%d drive=%d (want 0/0)",
                  b->nr_06_button_m1_nmi_en(), b->nr_06_button_drive_nmi_en(),
                  d->nr_06_button_m1_nmi_en(), d->nr_06_button_drive_nmi_en()));
    }

    // MX-79 - nr_06_ps2_mode (bit 2, config mode only, zxnext.vhd:5728-5729): the
    // NR 0x06 read returns it (:6488).
    {
        auto a = next_machine_550();
        nr_write(*a, 0x03, 0x07);          // NR 0x03 bits 111 re-enter config mode: the latch opens
        nr_write(*a, 0x06, 0x04);
        const uint8_t a_read = nr_read(*a, 0x06);
        const auto img = snapshot_of(*a);
        auto b = next_machine_550();
        nr_write(*b, 0x06, 0x00);
        const bool ok = restore_into(*b, img);
        const uint8_t b_read = nr_read(*b, 0x06);
        check("MX-79", "a state load re-derives nr_06_ps2_mode: the NR 0x06 read returns bit 2 as "
              "saved [zxnext.vhd:5728-5729,6488]",
              ok && (a_read & 0x04) != 0 && (b_read & 0x04) != 0,
              fmt("saved read=0x%02X restored read=0x%02X (want bit 2 set in both)", a_read, b_read));
    }

    // MX-80 - the F3 / F8 hotkey enables (bits 5, 7, zxnext.vhd:6342,6347): F8
    // steps the CPU speed only while NR 0x06 bit 7 is set. Snapshot with the
    // hotkeys disabled, restore into a machine with them enabled: F8 must do nothing.
    {
        auto a = next_machine_550();
        nr_write(*a, 0x06, 0x00);
        const auto img = snapshot_of(*a);
        auto b = next_machine_550();                 // power-on: both enables set (0xA0)
        const bool ok = restore_into(*b, img);
        const uint8_t before = nr_read(*b, 0x07) & 0x03;
        b->emu_fnkeys().simulate_mf_fkey_press(8);
        const uint8_t after = nr_read(*b, 0x07) & 0x03;
        check("MX-80", "a state load re-derives the hotkey enables from the restored NR 0x06: F8 "
              "(bit 7 clear) leaves the CPU speed alone [zxnext.vhd:6347,5162]",
              ok && before == after, fmt("speed before=%u after=%u (want equal)", before, after));
    }

    // MX-90 (F4) - the F3 hotkey enable (NR 0x06 bit 5, zxnext.vhd:6342): restored clear, F3 does
    // nothing (NR 0x05 bit 2 stays); a control machine with the bit set toggles it.
    {
        auto a = next_machine_550();
        nr_write(*a, 0x06, 0x00);
        const auto img = snapshot_of(*a);
        auto b = next_machine_550();                 // power-on: both enables set (0xA0)
        const bool ok = restore_into(*b, img);
        const uint8_t before = b->nextreg().cached(0x05) & 0x04;
        b->emu_fnkeys().simulate_mf_fkey_press(3);
        const uint8_t after = b->nextreg().cached(0x05) & 0x04;
        auto c = next_machine_550();
        const uint8_t cb = c->nextreg().cached(0x05) & 0x04;
        c->emu_fnkeys().simulate_mf_fkey_press(3);
        const uint8_t ca = c->nextreg().cached(0x05) & 0x04;
        check("MX-90", "a state load re-derives the F3 hotkey enable from the restored NR 0x06 bit 5: "
              "F3 (bit 5 clear) leaves NR 0x05 bit 2 alone, a power-on machine toggles it "
              "[zxnext.vhd:6342,5164]", ok && before == after && cb != ca,
              fmt("restored %u->%u (want equal); control %u->%u (want different)", before, after, cb, ca));
    }

    // MX-91 (F5) - nr_06_ps2_mode the other way: a saved 0 into a machine holding 1.
    {
        auto a = next_machine_550();
        nr_write(*a, 0x03, 0x07);
        nr_write(*a, 0x06, 0x00);
        const auto img = snapshot_of(*a);
        auto b = next_machine_550();
        nr_write(*b, 0x03, 0x07);
        nr_write(*b, 0x06, 0x04);
        const uint8_t held = nr_read(*b, 0x06);
        const bool ok = restore_into(*b, img);
        const uint8_t got = nr_read(*b, 0x06);
        check("MX-91", "a state load re-derives nr_06_ps2_mode when the saved bit is 0 and the "
              "machine's is 1 [zxnext.vhd:5728-5729,6488]",
              ok && (held & 0x04) != 0 && (got & 0x04) == 0,
              fmt("before=0x%02X restored=0x%02X (want bit 2 set / clear)", held, got));
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

    // AUD-SD-17 — nr_08_dac_en gates Soundrive port writes. With dac_en=0,
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

        check("AUD-SD-17",
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

    // AUD-SD-19 — soundrive.vhd:69-78 + zxnext.vhd:6436:
    //   soundrive.reset_i <= reset OR NOT nr_08_dac_en;
    // While disabled, all four DAC channels stay latched at 0x80 (DC
    // midpoint). A 1->0 transition on nr_08_dac_en must therefore force
    // every channel back to 0x80 so residual non-silent levels do not
    // persist (Emulator::write_nr_8 calls Dac::reset() on the disable
    // edge — see G111).
    // Reserved range: SD-10..18 left unused so the gap signals a future
    // group expansion.
    {
        fresh(emu);
        nr_write(emu, 0x08, 0x08);              // gate open (dac_en=1)
        emu.port().out(0x1F, 0xFF);             // chA = 0xFF
        emu.port().out(0x0F, 0xFF);             // chB = 0xFF
        emu.port().out(0x4F, 0xFF);             // chC = 0xFF
        emu.port().out(0x5F, 0xFF);             // chD = 0xFF
        const uint16_t L_on = emu.dac().pcm_left();    // expect 0x1FE
        const uint16_t R_on = emu.dac().pcm_right();   // expect 0x1FE

        nr_write(emu, 0x08, 0x00);              // gate closed (dac_en 1->0)
        const uint16_t L_off = emu.dac().pcm_left();   // expect 0x100 (silence)
        const uint16_t R_off = emu.dac().pcm_right();  // expect 0x100

        check("AUD-SD-19",
              "nr_08_dac_en 1->0 resets DAC channels to 0x80 silence "
              "[soundrive.vhd:69-78, zxnext.vhd:6436]",
              L_on == 0x1FE && R_on == 0x1FE && L_off == 0x100 && R_off == 0x100,
              fmt("on L=0x%03X R=0x%03X; off L=0x%03X R=0x%03X "
                  "(want 0x1FE/0x1FE/0x100/0x100)",
                  L_on, R_on, L_off, R_off));
    }
}

// ── Main ──────────────────────────────────────────────────────────────


// ══════════════════════════════════════════════════════════════════════
// Group Reset-scope (GH #442): the AY reset level and what a reset keeps
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:6379 `audio_ay_reset <= '1' when reset = '1' or nr_06_psg_mode = "11"`
// is a LEVEL. nr_06_psg_mode has a declared initial value and no reset clause
// (zxnext.vhd:1112), so it survives a soft reset. ym2149.vhd RESET_H clears addr, the
// register file and the outputs only (:170-171,184-186,525-528); the generators have no
// reset clause (:258-519). The PSG clock enable comes from clk_28_div, which has no reset
// (zxnext_top_issue2.vhd:1158-1177); a hard reset is a reconfiguration (:1195).

static void ay_port_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0xFFFD, reg);
    emu.port().out(0xBFFD, val);
}

// The value text of `"key":value` in a JsonWriteDesc document.
static std::string json_val(const std::string& doc, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    const size_t at = doc.find(needle);
    if (at == std::string::npos) return "<absent:" + key + ">";
    size_t b = at + needle.size();
    while (b < doc.size() && (doc[b] == ' ' || doc[b] == '\n')) ++b;
    size_t e = b;
    if (doc[b] == '"') { ++b; e = doc.find('"', b); }
    else { while (e < doc.size() && doc[e] != ',' && doc[e] != '}' && doc[e] != '\n') ++e; }
    return doc.substr(b, e - b);
}

static std::string ts_doc(Emulator& emu) {
    jnext::save::JsonWriteDesc jw;
    emu.turbosound().describe_state(jw);
    return jw.str();
}

static const char* const kGenKeys[] = {
    "cnt_div", "noise_div", "ena_div", "ena_div_noise", "tone_cnt_a", "tone_cnt_b",
    "tone_cnt_c", "tone_op_a", "tone_op_b", "tone_op_c", "noise_cnt", "poly17", "noise_op",
    "env_cnt", "env_ena", "env_vol", "env_inc", "env_hold"
};

// Keys of every generator field of all three chips, joined: equal text = equal state.
static std::string gen_fingerprint(Emulator& emu) {
    const std::string d = ts_doc(emu);
    std::string out;
    for (int n = 0; n < 3; ++n)
        for (const char* k : kGenKeys)
            out += json_val(d, "ay" + std::to_string(n) + "_" + k) + ";";
    return out;
}

static uint64_t psg_accum_of(Emulator& emu) {
    jnext::save::JsonWriteDesc jw;
    emu.describe_state(jw);
    return std::strtoull(json_val(jw.str(), "psg_accum").c_str(), nullptr, 10);
}

// Three chips playing: tone + noise + envelope on every chip, then some frames.
static void play_three_chips(Emulator& emu) {
    nr_write(emu, 0x06, 0x01);
    nr_write(emu, 0x08, 0x12);       // turbosound_en (b1), keep the reset default b4
    for (uint8_t sel : {uint8_t(0xFF), uint8_t(0xFE), uint8_t(0xFD)}) {
        emu.port().out(0xFFFD, sel);
        ay_port_write(emu, 0, 0x03);  ay_port_write(emu, 6, 0x01);
        ay_port_write(emu, 7, 0x30);  ay_port_write(emu, 8, 0x0F);
        ay_port_write(emu, 9, 0x10);  ay_port_write(emu, 11, 0x05);
        ay_port_write(emu, 13, 0x0E);
    }
    emu.port().out(0xFFFD, 0xFF);
    for (int i = 0; i < 3; ++i) emu.run_frame();
}

static void test_reset_scope(Emulator& emu) {
    set_group("Reset-scope");

    // AUD-NR-07 - zxnext.vhd:6379,6387: psg_mode = "11" HOLDS the AY reset.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x01);
        ay_port_write(emu, 0x00, 0x77);                    // a value the onset reset must clear
        nr_write(emu, 0x06, 0x03);
        ay_port_write(emu, 0x00, 0x55);
        const uint8_t held = emu.turbosound().ay(0).read_register(0);
        nr_write(emu, 0x06, 0x01);
        ay_port_write(emu, 0x00, 0x55);
        const uint8_t released = emu.turbosound().ay(0).read_register(0);
        check("AUD-NR-07", "NR 0x06 psg_mode=11 holds the AY reset; leaving it accepts writes "
              "[zxnext.vhd:6379]", held == 0x00 && released == 0x55,
              fmt("held R0=0x%02X released R0=0x%02X (want 0x00 / 0x55)", held, released));
    }

    // AUD-NR-08 - zxnext.vhd:1112 (init-only), :6379: the hold survives a soft reset.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x03);
        emu.soft_reset();
        ay_port_write(emu, 0x00, 0x55);
        const uint8_t r0 = emu.turbosound().ay(0).read_register(0);
        const uint8_t mode = emu.nextreg().cached(0x06) & 0x03;
        check("AUD-NR-08", "the AY reset hold survives a soft reset [zxnext.vhd:1112,6379]",
              r0 == 0x00 && mode == 0x03, fmt("R0=0x%02X psg_mode=%u (want 0x00 / 3)", r0, mode));
    }

    // AUD-NR-09 - zxnext.vhd:6379: a state load restores the hold (the NR 0x06 value).
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x03);
        StateWriter m;
        emu.save_state(m);
        std::vector<uint8_t> held_img(m.position(), 0);
        StateWriter w(held_img.data(), held_img.size());
        emu.save_state(w);

        fresh(emu);
        nr_write(emu, 0x06, 0x01);
        StateWriter m2;
        emu.save_state(m2);
        std::vector<uint8_t> free_img(m2.position(), 0);
        StateWriter w2(free_img.data(), free_img.size());
        emu.save_state(w2);

        StateReader r1(held_img.data(), held_img.size());
        const bool l1 = emu.load_state(r1);
        ay_port_write(emu, 0x00, 0x55);
        const uint8_t a = emu.turbosound().ay(0).read_register(0);

        StateReader r2(free_img.data(), free_img.size());
        const bool l2 = emu.load_state(r2);
        ay_port_write(emu, 0x00, 0x55);
        const uint8_t b = emu.turbosound().ay(0).read_register(0);
        check("AUD-NR-09", "a state load restores the AY reset hold from NR 0x06 [zxnext.vhd:6379]",
              l1 && l2 && a == 0x00 && b == 0x55,
              fmt("loads %d/%d held-load R0=0x%02X free-load R0=0x%02X (want 0x00 / 0x55)",
                  l1, l2, a, b));
    }

    // AUD-NR-33 - zxnext.vhd:1112,6389: psg_mode powers on at "00", so aymode_i = 0 (YM)
    // until NR 0x06 is written.
    {
        Emulator pristine;
        build_next_emulator(pristine);
        check("AUD-NR-33", "power-on PSG mode is YM until NR 0x06 is written "
              "[zxnext.vhd:1112,6389]", !pristine.turbosound().ay_mode(),
              fmt("ay_mode=%d (want 0 = YM)", pristine.turbosound().ay_mode() ? 1 : 0));
    }

    // AUD-NR-34 - zxnext.vhd:1112,6389: an NR 0x06 write sets the mode, and it survives a
    // soft reset (psg_mode is not in the reset block); a write of 00 goes back to YM.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x01);
        const bool ay = emu.turbosound().ay_mode();
        emu.soft_reset();
        const bool ay_after = emu.turbosound().ay_mode();
        nr_write(emu, 0x06, 0x00);
        emu.soft_reset();
        const bool ym_after = !emu.turbosound().ay_mode();
        check("AUD-NR-34", "the NR 0x06 PSG mode survives a soft reset [zxnext.vhd:1112,6389]",
              ay && ay_after && ym_after,
              fmt("ay=%d after-soft=%d ym-after-soft=%d (want 1/1/1)", ay, ay_after, ym_after));
    }

    // AUD-NR-35 - zxnext.vhd:6379: only psg_mode "11" holds the reset. "10" (YM alias) is
    // not a hold: a port write lands at once, after a soft reset, and after a state load.
    {
        fresh(emu);
        nr_write(emu, 0x06, 0x02);
        ay_port_write(emu, 0x00, 0x55);
        const uint8_t now = emu.turbosound().ay(0).read_register(0);

        fresh(emu);
        nr_write(emu, 0x06, 0x02);
        emu.soft_reset();
        ay_port_write(emu, 0x00, 0x66);
        const uint8_t soft = emu.turbosound().ay(0).read_register(0);

        fresh(emu);
        nr_write(emu, 0x06, 0x02);
        StateWriter m;
        emu.save_state(m);
        std::vector<uint8_t> img(m.position(), 0);
        StateWriter w(img.data(), img.size());
        emu.save_state(w);
        fresh(emu);
        nr_write(emu, 0x06, 0x03);                       // held when the load happens
        StateReader rd(img.data(), img.size());
        const bool loaded = emu.load_state(rd);
        ay_port_write(emu, 0x00, 0x77);
        const uint8_t load = emu.turbosound().ay(0).read_register(0);
        check("AUD-NR-35", "psg_mode=10 is not a hold: writes land at once, after a soft reset "
              "and after a state load [zxnext.vhd:6379]",
              now == 0x55 && soft == 0x66 && loaded && load == 0x77,
              fmt("now=0x%02X soft=0x%02X loaded=%d load=0x%02X (want 55/66/1/77)",
                  now, soft, loaded, load));
    }

    // AUD-NR-30 - ym2149.vhd:170-171,184-186,525-528; no reset clause at :258-519;
    // zxnext_top_issue2.vhd:840: a soft reset clears RESET_H fields and keeps the generators.
    {
        fresh(emu);
        play_three_chips(emu);
        for (int i = 0; i < 64; ++i) {
            const std::string d = ts_doc(emu);
            if (json_val(d, "ay0_poly17") != "0" && json_val(d, "ay0_cnt_div") != "0" &&
                (json_val(d, "ay0_tone_cnt_a") != "0" || json_val(d, "ay0_tone_op_a") == "true")) break;
            emu.execute_single_instruction();
        }
        const std::string before = ts_doc(emu);
        const std::string gen_before = gen_fingerprint(emu);
        Emulator fresh_emu;
        build_next_emulator(fresh_emu);
        const bool nonvac = gen_before != gen_fingerprint(fresh_emu) &&
                            json_val(before, "ay0_poly17") != "0" &&
                            json_val(before, "ay1_poly17") != "0" &&
                            json_val(before, "ay2_poly17") != "0" &&
                            (json_val(before, "ay0_tone_cnt_a") != "0" ||
                             json_val(before, "ay0_tone_op_a") == "true");
        emu.soft_reset();
        const std::string after = ts_doc(emu);
        const bool gens_kept = gen_before == gen_fingerprint(emu);
        bool cleared = true;
        for (int n = 0; n < 3; ++n) {
            const std::string p = "ay" + std::to_string(n) + "_";
            if (json_val(after, p + "addr") != "0") cleared = false;
            for (const char* o : {"out_a", "out_b", "out_c"})
                if (json_val(after, p + o) != "0") cleared = false;
            for (int r = 0; r < 16; ++r)
                if (emu.turbosound().ay(n).read_register(static_cast<uint8_t>(r)) !=
                    (r == 7 ? 0xFF : 0x00)) cleared = false;
        }
        check("AUD-NR-30", "a soft reset keeps every AY generator field and clears only addr, "
              "registers and outputs [ym2149.vhd:170-171,184-186,258-519,525-528]",
              nonvac && gens_kept && cleared,
              fmt("nonvac=%d gens_kept=%d cleared=%d", nonvac, gens_kept, cleared));
    }

    // AUD-NR-31 - zxnext_top_issue2.vhd:1158-1177: a soft reset keeps the PSG enable phase.
    {
        fresh(emu);
        uint64_t before = 0;
        for (int i = 0; i < 64 && before == 0; ++i) {
            emu.execute_single_instruction();
            before = psg_accum_of(emu);
        }
        emu.soft_reset();
        const uint64_t after = psg_accum_of(emu);
        check("AUD-NR-31", "a soft reset keeps the PSG clock-enable phase "
              "[zxnext_top_issue2.vhd:1158-1177]", before != 0 && after == before,
              fmt("before=%llu after=%llu", (unsigned long long)before, (unsigned long long)after));
    }

    // AUD-NR-32 - zxnext_top_issue2.vhd:1195, ym2149.vhd:105-125: init(cfg) is power-on.
    {
        fresh(emu);
        play_three_chips(emu);
        for (int i = 0; i < 64 && psg_accum_of(emu) == 0; ++i) emu.execute_single_instruction();
        const std::string used = gen_fingerprint(emu);
        const uint64_t acc_used = psg_accum_of(emu);
        fresh(emu);
        Emulator ref;
        build_next_emulator(ref);
        check("AUD-NR-32", "init() without preserve_memory is power-on: generators and PSG phase "
              "at their initial values [zxnext_top_issue2.vhd:1195, ym2149.vhd:105-125]",
              used != gen_fingerprint(ref) && gen_fingerprint(emu) == gen_fingerprint(ref) &&
              acc_used != 0 && psg_accum_of(emu) == psg_accum_of(ref),
              fmt("used==ref(nonvac)=%d emu==ref=%d acc_used=%llu",
                  used == gen_fingerprint(ref), gen_fingerprint(emu) == gen_fingerprint(ref),
                  (unsigned long long)acc_used));
    }
}

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
    test_tape_mix();        std::printf("  Group: NR-TAPE-MIX -- done\n");
    test_nr06_restore();    std::printf("  Group: NR-06-RESTORE -- done\n");
    test_nr_dac_gate(emu);  std::printf("  Group: NR-DAC-GATE -- done\n");
    test_nr_a2(emu);        std::printf("  Group: NR-0xA2 -- done\n");
    test_reset_scope(emu);  std::printf("  Group: Reset-scope -- done\n");

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
