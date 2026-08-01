// Debugger Audio Panel Test (Task 41 — "the audio panel decodes a field
// that does not exist").
//
// src/debugger/audio_panel.cpp used to decode NR 0x08 bits 5:4 as a bogus
// 2-bit "stereo mode" field against a 6-entry name table {ABC,ACB,BAC,BCA,
// CAB,CBA}. VHDL zxnext.vhd:5177 — `nr_08_psg_stereo_mode <= nr_wr_dat(5)`
// — is a SINGLE bit; audio/turbosound.vhd:45 documents it as "0 = ABC,
// 1 = ACB for all psg". Bit 4 (zxnext.vhd:5178) is `nr_08_internal_speaker_en`,
// an unrelated field, so toggling the internal speaker silently changed the
// panel's displayed stereo mode, and ACB rendered as the invented name "BAC".
//
// The same function also mis-decoded NR 0x06's AY/YM mode: it read bit 4
// ("Enable divmmc nmi by DRIVE button", nextreg.txt) instead of bit 0
// (`nr_06_psg_mode(0)`, VHDL zxnext.vhd:6389 — "aymode_i", 0=YM 1=AY).
//
// The fix routes both fields through TurboSound::stereo_mode() /
// TurboSound::ay_mode() (src/audio/turbosound.h), the same live accessors
// NR-01/NR-10 in test/audio/audio_nextreg_test.cpp already pin as VHDL-
// faithful, instead of re-decoding a raw NextREG byte inside the panel.
//
// Rows AP-02/AP-03 and AP-05/AP-06 are the discriminative ones: they plant
// the "wrong bit" value the old code actually read (internal-speaker-en for
// stereo, drive-button-NMI-en for AY/YM) while leaving the TRUE field at its
// opposite value, so a reversion of the fix flips the checked label text.
//
// Qt is required (the panel is a QWidget), but no display is: main() forces
// the offscreen QPA platform (same idiom as debugger_video_panel_test).
// Run: ./build/test/debugger_audio_panel_test

#include "audio/audio_mute.h"
#include "audio/mixer.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debugger/audio_panel.h"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QString>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ── Test infrastructure (mirrors the other subsystem suites) ──────────

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

void set_group(const char* name) { g_group = name; }

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

std::string fmt(const char* fmt_str, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    if (!emu.init(cfg)) return false;

    // The debugger mute mask is HOST state, not machine state: by design it
    // survives init() and reset() (see audio/audio_mute.h — otherwise the
    // panel's tick-marks would start lying after a reset). This suite reuses one
    // Emulator across rows, so that deliberate persistence would leak a mute
    // from one row into the next. Clear it explicitly here. AP-07 asserts the
    // genuine power-on default on its own fresh Emulator, so this reset cannot
    // paper over a bad default.
    emu.set_audio_mute_mask(AudioMute::NONE);
    return true;
}

// Write NextREG register through the real port path (OUT 0x243B,reg;
// OUT 0x253B,val) — same idiom as audio_nextreg_test.cpp.
void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// Find the QLabel whose text starts with `prefix` among the panel's
// children ("TurboSound:", "Mode:", "Stereo:" are distinct prefixes, so no
// objectName plumbing is needed to identify them from outside the class).
QString label_text(AudioPanel& panel, const char* prefix) {
    for (QLabel* lbl : panel.findChildren<QLabel*>()) {
        if (lbl->text().startsWith(prefix)) return lbl->text();
    }
    return QString();
}

// Find one of the five source checkboxes by its visible label. Same "identify
// by text" idiom as label_text() — no objectName plumbing needed.
QCheckBox* source_box(AudioPanel& panel, const char* label) {
    for (QCheckBox* cb : panel.findChildren<QCheckBox*>()) {
        if (cb->text() == label) return cb;
    }
    return nullptr;
}

// Program AY chip `chip` (0..2) to emit a steady maximum-amplitude level on all
// three channels, so its contribution to the stereo sum is large, constant, and
// independent of tone/noise phase:
//   R7 = 0x3F  — tone AND noise disabled on A/B/C. Per the channel mix
//                (ay_chip.cpp:322-324, ym2149.vhd) a disabled tone+noise leaves
//                the channel gated permanently ON at its volume level — the
//                standard way to get a DC level out of an AY.
//   R8/9/10 = 0x0F — fixed (non-envelope) maximum volume.
// Register access goes through the REAL port path (0xFFFD select / 0xBFFD data),
// including the TurboSound chip-select command, so nothing here bypasses the
// hardware model.
void program_ay_dc(Emulator& emu, int chip) {
    // TurboSound select command: bit7=1, bits4:2=111, pan bits6:5=11 (L+R on),
    // sel bits1:0: AY#0="11", AY#1="10", AY#2="01" (turbosound.vhd).
    static const uint8_t kSelect[3] = {0xFF, 0xFE, 0xFD};
    emu.port().out(0xFFFD, kSelect[chip]);

    const uint8_t regs[4]  = {7, 8, 9, 10};
    const uint8_t vals[4]  = {0x3F, 0x0F, 0x0F, 0x0F};
    for (int i = 0; i < 4; ++i) {
        emu.port().out(0xFFFD, regs[i]);   // select register (bits 7:5 = 000)
        emu.port().out(0xBFFD, vals[i]);   // write data
    }
}

// Recompute TurboSound's stereo output from the current register state.
uint32_t ay_level(Emulator& emu) {
    emu.turbosound().tick();
    return static_cast<uint32_t>(emu.turbosound().pcm_left()) +
           emu.turbosound().pcm_right();
}

// Emit one mixer sample from the machine's live audio sources and return it.
// generate_sample() is the point-sampling entry the mixer keeps for exactly
// this purpose (mixer.h) — it exercises the same Mixer::mix() the emulator's
// accumulate() path uses.
void mixer_sample(Emulator& emu, int16_t& L, int16_t& R) {
    int16_t pair[2] = {0, 0};
    // Drain anything already queued so we read back OUR sample.
    int16_t sink[2];
    while (emu.mixer().available() > 0) emu.mixer().read_samples(sink, 1);

    emu.mixer().generate_sample(emu.beeper(), emu.turbosound(), emu.dac());
    emu.mixer().read_samples(pair, 1);
    L = pair[0];
    R = pair[1];
}

} // namespace

// ── AP-STEREO: NR 0x08 bit 5 is a single bit, not bits 5:4 ────────────

static void test_stereo_mode(Emulator& emu) {
    set_group("AP-STEREO");

    // AP-01 — bit 5 = 0 (ABC), all other bits clear.
    {
        if (!build_next_emulator(emu)) { check("AP-01", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x00);
        AudioPanel panel(&emu);
        panel.refresh();
        const QString got = label_text(panel, "Stereo:");
        check("AP-01", "NR 0x08 = 0x00 (bit5=0) shows Stereo: ABC "
              "[zxnext.vhd:5177, turbosound.vhd:45]",
              got == "Stereo: ABC",
              fmt("got=\"%s\"", got.toUtf8().constData()));
    }

    // AP-02 — bit 5 = 1 (ACB), all other bits clear.
    {
        if (!build_next_emulator(emu)) { check("AP-02", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x20);
        AudioPanel panel(&emu);
        panel.refresh();
        const QString got = label_text(panel, "Stereo:");
        check("AP-02", "NR 0x08 = 0x20 (bit5=1) shows Stereo: ACB "
              "[zxnext.vhd:5177, turbosound.vhd:45]",
              got == "Stereo: ACB",
              fmt("got=\"%s\"", got.toUtf8().constData()));
    }

    // AP-03 — the discriminative row: bit 4 (internal_speaker_en) set,
    // bit 5 (stereo) clear. The pre-fix panel read bits 5:4 as a single
    // field and stereo_bits=(reg08>>4)&3=1 → displayed "ACB" (wrongly, from
    // the speaker bit). VHDL says bit 4 is unrelated to stereo mode
    // (zxnext.vhd:5178), so the correct display is still ABC.
    {
        if (!build_next_emulator(emu)) { check("AP-03", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x10);   // bit4=1 (speaker), bit5=0 (stereo=ABC)
        AudioPanel panel(&emu);
        panel.refresh();
        const QString got = label_text(panel, "Stereo:");
        check("AP-03", "NR 0x08 bit 4 (internal speaker) does not affect the "
              "displayed stereo mode [zxnext.vhd:5177 vs :5178 are independent bits]",
              got == "Stereo: ABC",
              fmt("got=\"%s\" (bit4 must not leak into stereo decode)",
                  got.toUtf8().constData()));
    }
}

// ── AP-AYMODE: NR 0x06 bit 0 selects AY/YM, not bit 4 ──────────────────

static void test_ay_ym_mode(Emulator& emu) {
    set_group("AP-AYMODE");

    // AP-04 — psg_mode bit 0 = 1 (AY).
    {
        if (!build_next_emulator(emu)) { check("AP-04", "emulator init", false); return; }
        nr_write(emu, 0x06, 0x01);
        AudioPanel panel(&emu);
        panel.refresh();
        const QString got = label_text(panel, "Mode:");
        check("AP-04", "NR 0x06 = 0x01 (psg_mode bit0=1) shows Mode: AY "
              "[zxnext.vhd:6389, ym2149.vhd:86]",
              got == "Mode: AY",
              fmt("got=\"%s\"", got.toUtf8().constData()));
    }

    // AP-05 — psg_mode bit 0 = 0 (YM).
    {
        if (!build_next_emulator(emu)) { check("AP-05", "emulator init", false); return; }
        nr_write(emu, 0x06, 0x00);
        AudioPanel panel(&emu);
        panel.refresh();
        const QString got = label_text(panel, "Mode:");
        check("AP-05", "NR 0x06 = 0x00 (psg_mode bit0=0) shows Mode: YM "
              "[zxnext.vhd:6389, ym2149.vhd:86]",
              got == "Mode: YM",
              fmt("got=\"%s\"", got.toUtf8().constData()));
    }

    // AP-06 — the discriminative row: bit 4 ("Enable divmmc nmi by DRIVE
    // button", nextreg.txt) set, psg_mode bit 0 clear (YM). The pre-fix
    // panel read `reg06 & 0x10` as "1=AY" — an unrelated bit — so this
    // would have wrongly shown AY. Correct display is still YM.
    {
        if (!build_next_emulator(emu)) { check("AP-06", "emulator init", false); return; }
        nr_write(emu, 0x06, 0x10);   // bit4=1 (drive-button NMI en), psg_mode=00 (YM)
        AudioPanel panel(&emu);
        panel.refresh();
        const QString got = label_text(panel, "Mode:");
        check("AP-06", "NR 0x06 bit 4 (drive-button NMI enable) does not "
              "affect the displayed AY/YM mode [nextreg.txt 0x06 bit4 vs "
              "bits1:0 are independent fields]",
              got == "Mode: YM",
              fmt("got=\"%s\" (bit4 must not leak into AY/YM decode)",
                  got.toUtf8().constData()));
    }
}

// ── AP-MUTE: the five source checkboxes actually mute (Task 48) ────────
//
// Before Task 48 the checkboxes were pure decoration: there was not a single
// connect() on any of them anywhere in the tree, and nothing ever called
// isChecked(). Every row below CLICKS the real QCheckBox — QAbstractButton::
// click() is the actual user-click path (toggle + emit clicked/toggled), not a
// back-door setChecked() — so the Qt signal, the panel slot,
// Emulator::set_audio_mute_mask() and the TurboSound/Mixer output gates are all
// in the loop, and then asserts on the REAL audio output.
//
// Mutation-tested: commenting out the connect() in audio_panel.cpp (i.e.
// restoring the original bug) fails AP-08/09/10/11/12/13 — the clicks land, the
// mask stays 0x00 and the sound keeps playing. Muting the DAC to 0 instead of to
// its midpoint fails AP-12 (muted reads -2048, not silence).
//
// There is no VHDL for any of this — the Next has no per-source mute — so the
// spec these rows encode is the one property such a facility MUST have: it may
// silence the output stage and NOTHING else. AP-14/AP-15 pin that.

static void test_source_mutes(Emulator& emu) {
    set_group("AP-MUTE");

    // AP-07 — a fresh machine mutes nothing, and the panel says so. Uses its OWN
    // Emulator (not the shared fixture, which force-clears the mask) so this
    // genuinely pins the power-on default.
    {
        Emulator fresh;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        if (!fresh.init(cfg)) { check("AP-07", "emulator init", false); return; }
        Emulator& emu = fresh;

        AudioPanel panel(&emu);
        const bool all_checked =
            source_box(panel, "AY #0")->isChecked() &&
            source_box(panel, "AY #1")->isChecked() &&
            source_box(panel, "AY #2")->isChecked() &&
            source_box(panel, "DAC")->isChecked() &&
            source_box(panel, "Beeper")->isChecked();
        check("AP-07", "default: nothing muted, all five source boxes checked",
              all_checked && emu.audio_mute_mask() == AudioMute::NONE,
              fmt("mask=0x%02X all_checked=%d", emu.audio_mute_mask(), all_checked));
    }

    // AP-08 — THE bug. Untick "AY #1" and AY#1 must go silent. Only AY#1 is
    // programmed to make sound, so the whole PSG sum must collapse to zero.
    {
        if (!build_next_emulator(emu)) { check("AP-08", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);           // turbosound_en (b1) — all 3 chips live
        program_ay_dc(emu, 1);               // AY#1 sounds; AY#0/#2 stay at volume 0
        AudioPanel panel(&emu);

        const uint32_t before = ay_level(emu);
        source_box(panel, "AY #1")->click();   // a real user click
        const uint32_t after = ay_level(emu);

        check("AP-08", "unticking \"AY #1\" silences AY#1 (its contribution to the "
              "3-PSG sum goes to zero)",
              before != 0 && after == 0,
              fmt("level before=%u after=%u mask=0x%02X (before must be >0, "
                  "after must be 0)", before, after, emu.audio_mute_mask()));
    }

    // AP-09 — re-ticking restores it, bit-exactly (the chip kept ticking while
    // muted, and its registers were never touched).
    {
        if (!build_next_emulator(emu)) { check("AP-09", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);
        program_ay_dc(emu, 1);
        AudioPanel panel(&emu);

        const uint32_t before = ay_level(emu);
        source_box(panel, "AY #1")->click();   // a real user click
        const uint32_t muted = ay_level(emu);
        source_box(panel, "AY #1")->click();   // click again — back to audible
        const uint32_t after = ay_level(emu);

        check("AP-09", "re-ticking \"AY #1\" restores its exact level",
              before != 0 && muted == 0 && after == before,
              fmt("before=%u muted=%u after=%u", before, muted, after));
    }

    // AP-10 — the mute is per-source, not a blanket kill. With AY#0 and AY#1
    // both sounding, muting AY#1 must leave exactly AY#0's contribution.
    {
        if (!build_next_emulator(emu)) { check("AP-10", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);
        program_ay_dc(emu, 0);
        const uint32_t ay0_only = ay_level(emu);   // AY#0 alone
        program_ay_dc(emu, 1);
        const uint32_t both = ay_level(emu);       // AY#0 + AY#1

        AudioPanel panel(&emu);
        source_box(panel, "AY #1")->click();   // a real user click
        const uint32_t ay1_muted = ay_level(emu);

        check("AP-10", "muting \"AY #1\" leaves AY#0 audible and untouched "
              "(per-source, not a blanket mute)",
              ay0_only != 0 && both > ay0_only && ay1_muted == ay0_only,
              fmt("ay0_only=%u both=%u ay1_muted=%u (ay1_muted must == ay0_only)",
                  ay0_only, both, ay1_muted));
    }

    // AP-11 — Beeper. EAR high is a 512-per-channel term (audio_mixer.vhd);
    // muting it must land on the SAME sample as EAR genuinely low, i.e. real
    // silence, not merely "a different number".
    {
        if (!build_next_emulator(emu)) { check("AP-11", "emulator init", false); return; }
        int16_t sL, sR;

        emu.port().out(0x00FE, 0x00);            // EAR low — the silence reference
        mixer_sample(emu, sL, sR);
        const int16_t quiet_L = sL;

        emu.port().out(0x00FE, 0x10);            // EAR high (port 0xFE bit 4)
        mixer_sample(emu, sL, sR);
        const int16_t loud_L = sL;

        AudioPanel panel(&emu);
        source_box(panel, "Beeper")->click();   // a real user click
        mixer_sample(emu, sL, sR);
        const int16_t muted_L = sL;

        check("AP-11", "unticking \"Beeper\" removes the EAR term from the mix "
              "(muted == genuinely-silent)",
              loud_L != quiet_L && muted_L == quiet_L,
              fmt("quiet=%d loud=%d muted=%d (muted must == quiet)",
                  quiet_L, loud_L, muted_L));
    }

    // AP-12 — DAC. The trap row. The DAC's silence is its MIDPOINT (0x80/ch,
    // soundrive.vhd), not zero, and emit_sample() subtracts that midpoint as the
    // mix's DC reference. A mute implemented as "dac_term = 0" would therefore
    // not silence the DAC, it would pin the output at a large negative DC offset.
    // So: assert the muted sample equals the IDLE-DAC sample exactly.
    {
        if (!build_next_emulator(emu)) { check("AP-12", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x08);               // nr_08_dac_en (b3)
        int16_t sL, sR;

        mixer_sample(emu, sL, sR);               // DAC idle at 0x80 — the reference
        const int16_t idle_L = sL;

        emu.port().out(0x001F, 0xFF);            // Soundrive channel A (left)
        emu.port().out(0x004F, 0xFF);            // Soundrive channel C (right)
        mixer_sample(emu, sL, sR);
        const int16_t loud_L = sL;

        AudioPanel panel(&emu);
        source_box(panel, "DAC")->click();      // a real user click
        mixer_sample(emu, sL, sR);
        const int16_t muted_L = sL;

        check("AP-12", "unticking \"DAC\" mutes it to its 0x80 midpoint, not to "
              "zero (muted sample == idle-DAC sample)",
              loud_L != idle_L && muted_L == idle_L,
              fmt("idle=%d loud=%d muted=%d (muted must == idle; a zeroed DAC "
                  "term would read ~%d)", idle_L, loud_L, muted_L,
                  idle_L - Mixer::DAC_REST_LEVEL * 4));
    }
}

// ── AP-INVIS: a debugger mute must be invisible to the emulated machine ─

static void test_mute_is_not_machine_state(Emulator& emu) {
    set_group("AP-INVIS");

    // AP-13 — a muted AY still reads back its register file over the real port
    // path (0xFFFD select / 0xBFFD data). The mute gates the mixer sum, never
    // the registers: a Z80 program cannot detect it.
    {
        if (!build_next_emulator(emu)) { check("AP-13", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);
        program_ay_dc(emu, 1);                   // leaves AY#1 selected

        AudioPanel panel(&emu);
        const uint32_t audible = ay_level(emu);
        source_box(panel, "AY #1")->click();   // a real user click
        const uint32_t silenced = ay_level(emu);

        // AY#1 is now genuinely silent; the Z80 selects R8 and reads it back.
        // Both clauses matter: the mute must have BITTEN (audible→silenced) and
        // the register file must nonetheless be unchanged.
        emu.port().out(0xFFFD, 8);
        const uint8_t got = emu.port().in(0xFFFD);

        check("AP-13", "muting AY#1 silences it yet does not alter what the Z80 "
              "reads from its register file (R8 still 0x0F) — CPU-invisible",
              audible != 0 && silenced == 0 && got == 0x0F,
              fmt("audible=%u silenced=%u R8=0x%02X (expected >0, 0, 0x0F)",
                  audible, silenced, got));
    }

    // AP-14 — the mute is NOT machine state: a reset must not silently un-mute.
    // If it did, the panel's tick-marks (which are never re-pushed) would start
    // lying about what is audible.
    {
        if (!build_next_emulator(emu)) { check("AP-14", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);
        program_ay_dc(emu, 1);

        AudioPanel panel(&emu);
        source_box(panel, "AY #1")->click();   // a real user click
        const uint8_t before = emu.audio_mute_mask();

        emu.reset();

        check("AP-14", "a machine reset does not clear the debugger mute mask "
              "(host facility, not machine state)",
              before == AudioMute::AY1 && emu.audio_mute_mask() == AudioMute::AY1 &&
                  emu.turbosound().chip_mute_mask() == AudioMute::AY1,
              fmt("mask before reset=0x%02X after=0x%02X ts=0x%02X",
                  before, emu.audio_mute_mask(),
                  emu.turbosound().chip_mute_mask()));
    }

    // AP-15 — a panel rebuilt against an already-muted machine comes up with the
    // muted box UNTICKED. (The debugger re-creates its panels; seeding the boxes
    // from the live mask is what stops the UI from claiming AY#1 is audible when
    // it is not.)
    {
        if (!build_next_emulator(emu)) { check("AP-15", "emulator init", false); return; }
        emu.set_audio_mute_mask(AudioMute::AY1 | AudioMute::BEEPER);

        AudioPanel panel(&emu);
        check("AP-15", "a freshly-created panel reflects the machine's existing "
              "mute mask instead of claiming everything is audible",
              source_box(panel, "AY #0")->isChecked() &&
                  !source_box(panel, "AY #1")->isChecked() &&
                  source_box(panel, "AY #2")->isChecked() &&
                  source_box(panel, "DAC")->isChecked() &&
                  !source_box(panel, "Beeper")->isChecked(),
              fmt("ay0=%d ay1=%d ay2=%d dac=%d beep=%d (expect 1,0,1,1,0)",
                  source_box(panel, "AY #0")->isChecked(),
                  source_box(panel, "AY #1")->isChecked(),
                  source_box(panel, "AY #2")->isChecked(),
                  source_box(panel, "DAC")->isChecked(),
                  source_box(panel, "Beeper")->isChecked()));
    }
}

int main(int argc, char** argv) {
    // A QWidget needs a QApplication, but not a display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    Emulator emu;
    test_stereo_mode(emu);
    std::printf("  Group: AP-STEREO      — done\n");
    test_ay_ym_mode(emu);
    std::printf("  Group: AP-AYMODE      — done\n");
    test_source_mutes(emu);
    std::printf("  Group: AP-MUTE        — done\n");
    test_mute_is_not_machine_state(emu);
    std::printf("  Group: AP-INVIS       — done\n");

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
