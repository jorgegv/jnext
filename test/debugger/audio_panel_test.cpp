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
// AUD-NR-01/AUD-NR-10 (was NR-01/NR-10, renamed GH #196 dedup) in
// test/audio/audio_nextreg_test.cpp already pin as VHDL-faithful, instead
// of re-decoding a raw NextREG byte inside the panel.
//
// Rows DAP-02/DAP-03 and DAP-05/DAP-06 are the discriminative ones: they plant
// the "wrong bit" value the old code actually read (internal-speaker-en for
// stereo, drive-button-NMI-en for AY/YM) while leaving the TRUE field at its
// opposite value, so a reversion of the fix flips the checked label text.
//
// Qt is required (the panel is a QWidget), but no display is: main() forces
// the offscreen QPA platform (same idiom as debugger_video_panel_test).
// Rows DAP-48..59 (GH #531) drive the Log sub-tab: real guest, DMA and debugger writes, then the
// table's cells, its filters, the double-click, the CSV export and the mirror of the backend's ring.
// Run: ./build/test/debugger_audio_panel_test

#include "audio/audio_mute.h"
#include "audio/mixer.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "debugger/audio_log_view.h"
#include "debugger/audio_panel.h"
#include "platform/emulator_boot.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QProgressBar>
#include <QString>
#include <QTabWidget>
#include <QPushButton>
#include <QTableView>
#include <QTabBar>
#include <QTableWidget>
#include <QToolButton>
#include <QPushButton>
#include <QTreeWidget>
#include <QStringList>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include "../row_id.h"

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
    report_row_id(id);
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
    // from one row into the next. Clear it explicitly here. DAP-07 asserts the
    // genuine power-on default on its own fresh Emulator, so this reset cannot
    // paper over a bad default.
    emu.set_audio_mute_mask(AudioMute::NONE);
    emu.set_audio_channel_mute_mask(AudioChannelMute::NONE);   // the same, for the per-channel mask
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


// The decoded widgets are found by objectName (chip<c>_<a|b|c>_period, ...).
QLabel* lbl(AudioPanel& panel, const char* name) { return panel.findChild<QLabel*>(name); }
QString txt(AudioPanel& panel, const char* name) {
    QLabel* l = lbl(panel, name);
    return l ? l->text() : QStringLiteral("<no label>");
}
bool lit(AudioPanel& panel, const char* name) {
    QLabel* l = lbl(panel, name);
    return l && l->property("lit").toBool();
}

QString cell_text(QTableWidget* t, int row, int col) {
    QTableWidgetItem* it = t ? t->item(row, col) : nullptr;
    return it ? it->text() : QStringLiteral("<no cell>");
}

// One register of the CURRENT chip through the real ports.
void ay_reg(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0xFFFD, reg);
    emu.port().out(0xBFFD, val);
}

} // namespace

// ── DAP-STEREO: NR 0x08 bit 5 is a single bit, not bits 5:4 ────────────

static void test_stereo_mode(Emulator& emu) {
    set_group("DAP-STEREO");

    // DAP-01 — bit 5 = 0 (ABC), all other bits clear.
    {
        if (!build_next_emulator(emu)) { check("DAP-01", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x00);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        panel.refresh();
        const QString got = label_text(panel, "Stereo:");
        check("DAP-01", "NR 0x08 = 0x00 (bit5=0) shows Stereo: ABC "
              "[zxnext.vhd:5177, turbosound.vhd:45]",
              got == "Stereo: ABC",
              fmt("got=\"%s\"", got.toUtf8().constData()));
    }

    // DAP-02 — bit 5 = 1 (ACB), all other bits clear.
    {
        if (!build_next_emulator(emu)) { check("DAP-02", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x20);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        panel.refresh();
        const QString got = label_text(panel, "Stereo:");
        check("DAP-02", "NR 0x08 = 0x20 (bit5=1) shows Stereo: ACB "
              "[zxnext.vhd:5177, turbosound.vhd:45]",
              got == "Stereo: ACB",
              fmt("got=\"%s\"", got.toUtf8().constData()));
    }

    // DAP-03 — the discriminative row: bit 4 (internal_speaker_en) set,
    // bit 5 (stereo) clear. The pre-fix panel read bits 5:4 as a single
    // field and stereo_bits=(reg08>>4)&3=1 → displayed "ACB" (wrongly, from
    // the speaker bit). VHDL says bit 4 is unrelated to stereo mode
    // (zxnext.vhd:5178), so the correct display is still ABC.
    {
        if (!build_next_emulator(emu)) { check("DAP-03", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x10);   // bit4=1 (speaker), bit5=0 (stereo=ABC)
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        panel.refresh();
        const QString got = label_text(panel, "Stereo:");
        check("DAP-03", "NR 0x08 bit 4 (internal speaker) does not affect the "
              "displayed stereo mode [zxnext.vhd:5177 vs :5178 are independent bits]",
              got == "Stereo: ABC",
              fmt("got=\"%s\" (bit4 must not leak into stereo decode)",
                  got.toUtf8().constData()));
    }
}

// ── DAP-AYMODE: NR 0x06 bit 0 selects AY/YM, not bit 4 ──────────────────

static void test_ay_ym_mode(Emulator& emu) {
    set_group("DAP-AYMODE");

    // DAP-04 — psg_mode bit 0 = 1 (AY).
    {
        if (!build_next_emulator(emu)) { check("DAP-04", "emulator init", false); return; }
        nr_write(emu, 0x06, 0x01);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        panel.refresh();
        const QString got = label_text(panel, "Mode:");
        check("DAP-04", "NR 0x06 = 0x01 (psg_mode bit0=1) shows Mode: AY "
              "[zxnext.vhd:6389, ym2149.vhd:86]",
              got == "Mode: AY",
              fmt("got=\"%s\"", got.toUtf8().constData()));
    }

    // DAP-05 — psg_mode bit 0 = 0 (YM).
    {
        if (!build_next_emulator(emu)) { check("DAP-05", "emulator init", false); return; }
        nr_write(emu, 0x06, 0x00);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        panel.refresh();
        const QString got = label_text(panel, "Mode:");
        check("DAP-05", "NR 0x06 = 0x00 (psg_mode bit0=0) shows Mode: YM "
              "[zxnext.vhd:6389, ym2149.vhd:86]",
              got == "Mode: YM",
              fmt("got=\"%s\"", got.toUtf8().constData()));
    }

    // DAP-06 — the discriminative row: bit 4 ("Enable divmmc nmi by DRIVE
    // button", nextreg.txt) set, psg_mode bit 0 clear (YM). The pre-fix
    // panel read `reg06 & 0x10` as "1=AY" — an unrelated bit — so this
    // would have wrongly shown AY. Correct display is still YM.
    {
        if (!build_next_emulator(emu)) { check("DAP-06", "emulator init", false); return; }
        nr_write(emu, 0x06, 0x10);   // bit4=1 (drive-button NMI en), psg_mode=00 (YM)
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        panel.refresh();
        const QString got = label_text(panel, "Mode:");
        check("DAP-06", "NR 0x06 bit 4 (drive-button NMI enable) does not "
              "affect the displayed AY/YM mode [nextreg.txt 0x06 bit4 vs "
              "bits1:0 are independent fields]",
              got == "Mode: YM",
              fmt("got=\"%s\" (bit4 must not leak into AY/YM decode)",
                  got.toUtf8().constData()));
    }
}

// ── DAP-MUTE: the five source checkboxes actually mute (Task 48) ────────
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
// restoring the original bug) fails DAP-08/09/10/11/12/13 — the clicks land, the
// mask stays 0x00 and the sound keeps playing. Muting the DAC to 0 instead of to
// its midpoint fails DAP-12 (muted reads -2048, not silence).
//
// There is no VHDL for any of this — the Next has no per-source mute — so the
// spec these rows encode is the one property such a facility MUST have: it may
// silence the output stage and NOTHING else. DAP-14/DAP-15 pin that.

static void test_source_mutes(Emulator& emu) {
    set_group("DAP-MUTE");

    // DAP-07 — a fresh machine mutes nothing, and the panel says so. Uses its OWN
    // Emulator (not the shared fixture, which force-clears the mask) so this
    // genuinely pins the power-on default.
    {
        Emulator fresh;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        if (!fresh.init(cfg)) { check("DAP-07", "emulator init", false); return; }
        Emulator& emu = fresh;

        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        const bool all_checked =
            source_box(panel, "AY #0")->isChecked() &&
            source_box(panel, "AY #1")->isChecked() &&
            source_box(panel, "AY #2")->isChecked() &&
            source_box(panel, "DAC")->isChecked() &&
            source_box(panel, "Beeper")->isChecked();
        check("DAP-07", "default: nothing muted, all five source boxes checked",
              all_checked && emu.audio_mute_mask() == AudioMute::NONE,
              fmt("mask=0x%02X all_checked=%d", emu.audio_mute_mask(), all_checked));
    }

    // DAP-08 — THE bug. Untick "AY #1" and AY#1 must go silent. Only AY#1 is
    // programmed to make sound, so the whole PSG sum must collapse to zero.
    {
        if (!build_next_emulator(emu)) { check("DAP-08", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);           // turbosound_en (b1) — all 3 chips live
        program_ay_dc(emu, 1);               // AY#1 sounds; AY#0/#2 stay at volume 0
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);

        const uint32_t before = ay_level(emu);
        source_box(panel, "AY #1")->click();   // a real user click
        const uint32_t after = ay_level(emu);

        check("DAP-08", "unticking \"AY #1\" silences AY#1 (its contribution to the "
              "3-PSG sum goes to zero)",
              before != 0 && after == 0,
              fmt("level before=%u after=%u mask=0x%02X (before must be >0, "
                  "after must be 0)", before, after, emu.audio_mute_mask()));
    }

    // DAP-09 — re-ticking restores it, bit-exactly (the chip kept ticking while
    // muted, and its registers were never touched).
    {
        if (!build_next_emulator(emu)) { check("DAP-09", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);
        program_ay_dc(emu, 1);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);

        const uint32_t before = ay_level(emu);
        source_box(panel, "AY #1")->click();   // a real user click
        const uint32_t muted = ay_level(emu);
        source_box(panel, "AY #1")->click();   // click again — back to audible
        const uint32_t after = ay_level(emu);

        check("DAP-09", "re-ticking \"AY #1\" restores its exact level",
              before != 0 && muted == 0 && after == before,
              fmt("before=%u muted=%u after=%u", before, muted, after));
    }

    // DAP-10 — the mute is per-source, not a blanket kill. With AY#0 and AY#1
    // both sounding, muting AY#1 must leave exactly AY#0's contribution.
    {
        if (!build_next_emulator(emu)) { check("DAP-10", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);
        program_ay_dc(emu, 0);
        const uint32_t ay0_only = ay_level(emu);   // AY#0 alone
        program_ay_dc(emu, 1);
        const uint32_t both = ay_level(emu);       // AY#0 + AY#1

        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        source_box(panel, "AY #1")->click();   // a real user click
        const uint32_t ay1_muted = ay_level(emu);

        check("DAP-10", "muting \"AY #1\" leaves AY#0 audible and untouched "
              "(per-source, not a blanket mute)",
              ay0_only != 0 && both > ay0_only && ay1_muted == ay0_only,
              fmt("ay0_only=%u both=%u ay1_muted=%u (ay1_muted must == ay0_only)",
                  ay0_only, both, ay1_muted));
    }

    // DAP-11 — Beeper. EAR high is a 512-per-channel term (audio_mixer.vhd);
    // muting it must land on the SAME sample as EAR genuinely low, i.e. real
    // silence, not merely "a different number".
    {
        if (!build_next_emulator(emu)) { check("DAP-11", "emulator init", false); return; }
        int16_t sL, sR;

        emu.port().out(0x00FE, 0x00);            // EAR low — the silence reference
        mixer_sample(emu, sL, sR);
        const int16_t quiet_L = sL;

        emu.port().out(0x00FE, 0x10);            // EAR high (port 0xFE bit 4)
        mixer_sample(emu, sL, sR);
        const int16_t loud_L = sL;

        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        source_box(panel, "Beeper")->click();   // a real user click
        mixer_sample(emu, sL, sR);
        const int16_t muted_L = sL;

        check("DAP-11", "unticking \"Beeper\" removes the EAR term from the mix "
              "(muted == genuinely-silent)",
              loud_L != quiet_L && muted_L == quiet_L,
              fmt("quiet=%d loud=%d muted=%d (muted must == quiet)",
                  quiet_L, loud_L, muted_L));
    }

    // DAP-12 — DAC. The trap row. The DAC's silence is its MIDPOINT (0x80/ch,
    // soundrive.vhd), not zero, and emit_sample() subtracts that midpoint as the
    // mix's DC reference. A mute implemented as "dac_term = 0" would therefore
    // not silence the DAC, it would pin the output at a large negative DC offset.
    // So: assert the muted sample equals the IDLE-DAC sample exactly.
    {
        if (!build_next_emulator(emu)) { check("DAP-12", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x08);               // nr_08_dac_en (b3)
        int16_t sL, sR;

        mixer_sample(emu, sL, sR);               // DAC idle at 0x80 — the reference
        const int16_t idle_L = sL;

        emu.port().out(0x001F, 0xFF);            // Soundrive channel A (left)
        emu.port().out(0x004F, 0xFF);            // Soundrive channel C (right)
        mixer_sample(emu, sL, sR);
        const int16_t loud_L = sL;

        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        source_box(panel, "DAC")->click();      // a real user click
        mixer_sample(emu, sL, sR);
        const int16_t muted_L = sL;

        check("DAP-12", "unticking \"DAC\" mutes it to its 0x80 midpoint, not to "
              "zero (muted sample == idle-DAC sample)",
              loud_L != idle_L && muted_L == idle_L,
              fmt("idle=%d loud=%d muted=%d (muted must == idle; a zeroed DAC "
                  "term would read ~%d)", idle_L, loud_L, muted_L,
                  idle_L - Mixer::DAC_REST_LEVEL * 4));
    }
}

// ── DAP-INVIS: a debugger mute must be invisible to the emulated machine ─

static void test_mute_is_not_machine_state(Emulator& emu) {
    set_group("DAP-INVIS");

    // DAP-13 — a muted AY still reads back its register file over the real port
    // path (0xFFFD select / 0xBFFD data). The mute gates the mixer sum, never
    // the registers: a Z80 program cannot detect it.
    {
        if (!build_next_emulator(emu)) { check("DAP-13", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);
        program_ay_dc(emu, 1);                   // leaves AY#1 selected

        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        const uint32_t audible = ay_level(emu);
        source_box(panel, "AY #1")->click();   // a real user click
        const uint32_t silenced = ay_level(emu);

        // AY#1 is now genuinely silent; the Z80 selects R8 and reads it back.
        // Both clauses matter: the mute must have BITTEN (audible→silenced) and
        // the register file must nonetheless be unchanged.
        emu.port().out(0xFFFD, 8);
        const uint8_t got = emu.port().in(0xFFFD);

        check("DAP-13", "muting AY#1 silences it yet does not alter what the Z80 "
              "reads from its register file (R8 still 0x0F) — CPU-invisible",
              audible != 0 && silenced == 0 && got == 0x0F,
              fmt("audible=%u silenced=%u R8=0x%02X (expected >0, 0, 0x0F)",
                  audible, silenced, got));
    }

    // DAP-14 — the mute is NOT machine state: a reset must not silently un-mute.
    // If it did, the panel's tick-marks (which are never re-pushed) would start
    // lying about what is audible.
    //
    // GH #239: the reset is the production HARD reset (F1, Machine > Power
    // Reset), i.e. the frontend cold boot, which destroys and reconstructs the
    // Emulator — the debugger's panels survive it (MainWindow::set_emulator()
    // keeps the DebuggerManager). This row used to call the in-place
    // Emulator::reset(), which never touched the mask, so it passed while the
    // real reset reconstructed the mask to NONE under an unticked box.
    {
        if (!build_next_emulator(emu)) { check("DAP-14", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);
        program_ay_dc(emu, 1);

        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        source_box(panel, "AY #1")->click();   // a real user click
        const uint8_t before = emu.audio_mute_mask();

        emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{});

        check("DAP-14", "a hard reset (frontend cold boot) does not clear the debugger "
              "mute mask (host facility, not machine state)",
              before == AudioMute::AY1 && emu.audio_mute_mask() == AudioMute::AY1 &&
                  emu.turbosound().chip_mute_mask() == AudioMute::AY1,
              fmt("mask before reset=0x%02X after=0x%02X ts=0x%02X",
                  before, emu.audio_mute_mask(),
                  emu.turbosound().chip_mute_mask()));
    }

    // DAP-15 — a panel rebuilt against an already-muted machine comes up with the
    // muted box UNTICKED. (The debugger re-creates its panels; seeding the boxes
    // from the live mask is what stops the UI from claiming AY#1 is audible when
    // it is not.)
    {
        if (!build_next_emulator(emu)) { check("DAP-15", "emulator init", false); return; }
        emu.set_audio_mute_mask(AudioMute::AY1 | AudioMute::BEEPER);

        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
        AudioPanel panel(&dbg);
        check("DAP-15", "a freshly-created panel reflects the machine's existing "
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

// ── DAP-REGS: the AY register table ────────────────────────────────────
//
// GH #278 WP4b — the table is the one read of this panel no row pinned
// (qt-frontend.md §1.8 row 19), and WP4b moved it onto the backend's
// ay_registers(chip). Every one of the 48 cells gets its own value, written
// through the guest's ports: chip c, register r holds 0x80 + 0x10*c + r, so a
// row, a column or a chip read from the wrong place shows a different number,
// and chip 2's A0..AF check the upper-case hex.

static void test_ay_register_table(Emulator& emu) {
    set_group("DAP-REGS");

    if (!build_next_emulator(emu)) { check("DAP-16", "emulator init", false); return; }
    nr_write(emu, 0x08, 0x02);                  // turbosound_en: all three chips
    static const uint8_t kSelect[3] = {0xFF, 0xFE, 0xFD};
    for (int chip = 0; chip < 3; ++chip) {
        emu.port().out(0xFFFD, kSelect[chip]);
        for (uint8_t reg = 0; reg < 16; ++reg) {
            emu.port().out(0xFFFD, reg);
            emu.port().out(0xBFFD, static_cast<uint8_t>(0x80 + 0x10 * chip + reg));
        }
    }
    jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
    AudioPanel panel(&dbg);
    panel.refresh();

    auto* table = panel.findChild<QTableWidget*>();
    std::string bad;
    for (int chip = 0; chip < 3; ++chip)
        for (int reg = 0; reg < 16; ++reg) {
            const QString want = QString::asprintf("%02X", 0x80 + 0x10 * chip + reg);
            QTableWidgetItem* it = table ? table->item(reg, chip) : nullptr;
            const QString got = it ? it->text() : QStringLiteral("<no cell>");
            if (got != want)
                bad += fmt("AY#%d R%d '%s'(want %s) ", chip, reg,
                           got.toUtf8().constData(), want.toUtf8().constData());
        }
    check("DAP-16", "the AY table shows chip N's register R in row R, column N, as "
          "two upper-case hex digits",
          table && bad.empty(), bad);
}

// ── DAP-TS: the TurboSound line ────────────────────────────────────────
//
// GH #278 WP4b — "TurboSound: Yes/No" is the third live signal of the Info box
// (`nr_08_psg_turbosound_en`, NR 0x08 bit 1, zxnext.vhd — TurboSound::enabled()),
// now the backend's turbosound_enabled(). No row read it: a label printing the
// signal inverted survived every other row (measured).

static void test_turbosound_label(Emulator& emu) {
    set_group("DAP-TS");

    if (!build_next_emulator(emu)) { check("DAP-17", "emulator init", false); return; }
    nr_write(emu, 0x08, 0x00);
    jnext::dbg::Debugger dbg(emu);   // GH #278 WP4b: the panel reads through it
    AudioPanel panel(&dbg);
    panel.refresh();
    const QString off = label_text(panel, "TurboSound:");
    nr_write(emu, 0x08, 0x02);                  // bit 1: turbosound_en
    panel.refresh();
    const QString on = label_text(panel, "TurboSound:");
    check("DAP-17", "NR 0x08 bit 1 clear shows TurboSound: No, set shows "
          "TurboSound: Yes (the live enable)",
          off == "TurboSound: No" && on == "TurboSound: Yes",
          fmt("clear=\"%s\" set=\"%s\"", off.toUtf8().constData(),
              on.toUtf8().constData()));
}

// ── DAP-DEC: the decoded Chips view (GH #529) ───────────────────────────
//
// Oracles: ym2149.vhd:302-325 and 467-537 (periods, gates, levels), :371-391
// (the shape glyphs), turbosound.vhd:129-136 (selection and pan), zxnext.vhd
// :5731 (NR 0x06 psg mode); the note arithmetic is the one pinned by ANO-* in
// audio_debug_test. The widgets are found by objectName.

static void test_decoded_view(Emulator& emu) {
    set_group("DAP-DEC");

    {
        if (!build_next_emulator(emu)) { check("DAP-18", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        QStringList names;
        for (int i = 0; tabs && i < tabs->count(); ++i) names << tabs->tabText(i).replace("&&", "&");
        bool outside = tabs != nullptr;
        for (const char* t : {"AY #0", "AY #1", "AY #2", "DAC", "Beeper"}) {
            QCheckBox* cb = source_box(panel, t);
            outside = outside && cb && !tabs->isAncestorOf(cb);
        }
        check("DAP-18", "the sub-tabs are Chips, DAC & Beeper, Log, Tracker, Scopes in that order; the five source checkboxes sit outside the tab widget",
              names == QStringList({"Chips", "DAC & Beeper", "Log", "Tracker", "Scopes"}) && outside,
              names.join("|").toStdString());
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-19", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        ay_reg(emu, 0, 252); ay_reg(emu, 1, 0);
        panel.refresh();
        const QString n = txt(panel, "chip0_a_note"), c = txt(panel, "chip0_a_cents");
        ay_reg(emu, 0, 0);
        panel.refresh();
        const QString n0 = txt(panel, "chip0_a_note");
        check("DAP-19", "ym2149.vhd:306-325 tone period 252 on AY0 A at 1.75 MHz shows A4 and -23.7 cents; period 0 shows no note",
              n == "A4" && c == "-23.7" && n0 == "-", (n + " " + c + " " + n0).toStdString());
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-20", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        bool ok = true;
        std::string bad;
        // Channel B's (tone, noise) enables x the other channels equal to B or opposite to it.
        for (int combo = 0; combo < 8; ++combo) {
            const bool bt = combo & 1, bn = combo & 2, opposite = combo & 4;
            const bool ot = opposite ? !bt : bt, on = opposite ? !bn : bn;
            const bool tone[3] = {ot, bt, ot}, noise[3] = {on, bn, on};
            uint8_t r7 = 0;
            for (int k = 0; k < 3; ++k) {
                if (!tone[k])  r7 |= static_cast<uint8_t>(1 << k);
                if (!noise[k]) r7 |= static_cast<uint8_t>(8 << k);
            }
            ay_reg(emu, 7, r7);
            panel.refresh();
            static const char* const tn[3] = {"chip0_a_tone", "chip0_b_tone", "chip0_c_tone"};
            static const char* const nn[3] = {"chip0_a_noise", "chip0_b_noise", "chip0_c_noise"};
            for (int k = 0; k < 3; ++k)
                if (lit(panel, tn[k]) != tone[k] || lit(panel, nn[k]) != noise[k]) {
                    ok = false;
                    bad += fmt("combo %d ch %d ", combo, k);
                }
        }
        check("DAP-20", "ym2149.vhd:467-469 the T and N indicators of every channel are lit exactly when R7 enables them (channel B in all four combinations, the others equal or opposite)",
              ok, bad);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-21", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        ay_reg(emu, 8, 0x10); ay_reg(emu, 9, 0x0B); ay_reg(emu, 10, 0x1F);
        panel.refresh();
        const QString a = txt(panel, "chip0_a_vol"), b = txt(panel, "chip0_b_vol"), c = txt(panel, "chip0_c_vol");
        check("DAP-21", "ym2149.vhd:476-520 the volume column reads Env when R8-R10 bit 4 is set, else the decimal volume",
              a == "Env" && b == "11" && c == "Env", (a + " " + b + " " + c).toStdString());
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-22", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        // ym2149.vhd:371-391.
        static const char* const want[16] = {
            "\\___", "\\___", "\\___", "\\___", "/___", "/___", "/___", "/___",
            "\\\\\\\\", "\\___", "\\/\\/", "\\'''", "////", "/'''", "/\\/\\", "/___",
        };
        std::string bad;
        for (int sh = 0; sh < 16; ++sh) {
            ay_reg(emu, 13, static_cast<uint8_t>(sh));
            panel.refresh();
            if (txt(panel, "chip0_env_glyph") != want[sh]) bad += fmt("shape %d ", sh);
        }
        check("DAP-22", "ym2149.vhd:371-391 the envelope glyph of each of the 16 shapes", bad.empty(), bad);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-23", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        ay_reg(emu, 11, 100); ay_reg(emu, 12, 0);
        std::string bad;
        for (int sh = 0; sh < 16; ++sh) {
            ay_reg(emu, 13, static_cast<uint8_t>(sh));
            panel.refresh();
            const char* want = (sh == 8 || sh == 12) ? "C#2" : (sh == 10 || sh == 14) ? "C#1" : "-";
            if (txt(panel, "chip0_env_note") != want) bad += fmt("shape %d ", sh);
        }
        check("DAP-23", "ym2149.vhd:334-350,402-462 envelope period 100: the envelope note is C#2 for shapes 8 and 12, C#1 for 10 and 14, none for the other twelve",
              bad.empty(), bad);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-24", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* hex = panel.findChild<QCheckBox*>("audio_hex");
        auto* table = panel.findChild<QTableWidget*>();
        const bool default_hidden = table && table->isHidden();
        if (hex) hex->setChecked(true);
        const bool shown = table && !table->isHidden();
        ay_reg(emu, 8, 0x5A);
        panel.refresh();
        const QString on = cell_text(table, 8, 0);
        if (hex) hex->setChecked(false);
        const bool hidden = table && table->isHidden();
        ay_reg(emu, 8, 0xA5);
        panel.refresh();
        const QString off = cell_text(table, 8, 0);
        check("DAP-24", "the Hex toggle swaps the decoded view for the register table; the table is refreshed while hidden",
              hex && default_hidden && shown && hidden && on == "5A" && off == "A5",
              (on + " " + off).toStdString());
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-25", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);
        nr_write(emu, 0x09, 0x80);                 // chip 2 mono
        emu.port().out(0xFFFD, 0xDD);              // AY2 selected, pan 10 (left only)
        emu.port().out(0xFFFD, 0x1D);              // 5-bit address 0x1D on it
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        panel.refresh();
        const bool marker = !txt(panel, "chip2_active").isEmpty() && txt(panel, "chip0_active").isEmpty() &&
                            txt(panel, "chip1_active").isEmpty();
        const bool header = txt(panel, "chip2_addr") == "0x1D" && txt(panel, "chip2_pan") == "L -" &&
                            txt(panel, "chip2_mono") == "mono" && txt(panel, "chip0_mono") == "stereo" &&
                            txt(panel, "chip0_pan") == "L R";
        const QString mix_on = txt(panel, "chip0_mix");
        nr_write(emu, 0x08, 0x00);
        panel.refresh();
        const QString mix0_off = txt(panel, "chip0_mix"), mix2_off = txt(panel, "chip2_mix");
        check("DAP-25", "turbosound.vhd:129-136,197 chip header: active marker on the selected chip only, address 0x1D, pan 'L -' for pan 10, mono flag; in mix for all with TurboSound on, only the selected chip with it off",
              marker && header && mix_on == "in mix" && mix0_off == "not in mix" && mix2_off == "in mix",
              (mix_on + "|" + mix0_off + "|" + mix2_off).toStdString());
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-26", "emulator init", false); return; }
        nr_write(emu, 0x06, 0x01);                  // the AY curve
        ay_reg(emu, 7, 0x00);                       // gates open: the bar still shows the level
        ay_reg(emu, 8, 3); ay_reg(emu, 9, 9); ay_reg(emu, 10, 15);
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        panel.refresh();
        // ym2149.vhd:529-537 AY table at index volume (R << 1 | 1) >> 1.
        const int want[3] = {0x06, 0x41, 0xFF};
        static const char* const names[3] = {"chip0_a_level", "chip0_b_level", "chip0_c_level"};
        bool ok = true;
        std::string got;
        for (int k = 0; k < 3; ++k) {
            auto* bar = panel.findChild<QProgressBar*>(names[k]);
            ok = ok && bar && bar->value() == want[k];
            got += fmt("%d ", bar ? bar->value() : -1);
        }
        check("DAP-26", "ym2149.vhd:476-537 the level bar of channels A, B, C at fixed volumes 3, 9, 15 reads 0x06, 0x41, 0xFF (AY curve)", ok, got);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-27", "emulator init", false); return; }
        ay_reg(emu, 13, 0x08);
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        panel.refresh();
        const QString s = txt(panel, "chip0_env_step");
        check("DAP-27", "ym2149.vhd:393-401 after an R13 write and before any tick the envelope reads its reload: 31 falling",
              s == "31 falling", s.toStdString());
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-28", "emulator init", false); return; }
        nr_write(emu, 0x06, 0x03);
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        panel.refresh();
        const QString held = label_text(panel, "PSG mode:");
        nr_write(emu, 0x06, 0x01);
        panel.refresh();
        check("DAP-28", "zxnext.vhd:5731 NR 0x06 = 0x03 reads 'PSG mode: held in reset'; = 0x01 still reads exactly 'Mode: AY'",
              held == "PSG mode: held in reset" && label_text(panel, "Mode:") == "Mode: AY",
              (held + "|" + label_text(panel, "Mode:")).toStdString());
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-29", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        const bool chips = panel.chips_on_show();
        if (tabs) tabs->setCurrentIndex(2);
        check("DAP-29", "chips_on_show() is true with the Chips sub-tab current and false with Log current",
              tabs && chips && !panel.chips_on_show());
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-30", "emulator init", false); return; }
        ay_reg(emu, 11, 2); ay_reg(emu, 12, 0); ay_reg(emu, 13, 13);
        for (int i = 0; i < 1000; ++i) emu.turbosound().tick();
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        panel.refresh();
        const QString s = txt(panel, "chip0_env_step");
        check("DAP-30", "ym2149.vhd:422-443 shape 13 settles at the top: after 1000 ticks the envelope reads 31 holding",
              s == "31 holding", s.toStdString());
    }
}

// ── DAP-CHM: per-channel mute and solo (GH #533) ────────────────────────
//
// The M and S buttons of the 13 channels act on AUDIO_CHANNEL_MUTE (bit 3*chip +
// channel for the AY, bits 9-12 for DAC A-D). The bits are chosen asymmetric so
// that a swapped 3*k+c shows: chip 2 A is bit 6 (swapped: 2), chip 1 C is bit 5
// (swapped: 7).

static QToolButton* tbtn(AudioPanel& panel, const char* name) {
    return panel.findChild<QToolButton*>(name);
}

// "chip<c>_<a|b|c>" / "dac_<a..d>" stem of channel `i`, then the suffix.
static QString chan_name(int i, const char* suffix) {
    const QString stem = i < 9 ? QString("chip%1_%2").arg(i / 3).arg(QChar('a' + i % 3))
                               : QString("dac_%1").arg(QChar('a' + (i - 9)));
    return stem + suffix;
}

static QToolButton* mbtn(AudioPanel& panel, int i) { return tbtn(panel, chan_name(i, "_mute").toLatin1().constData()); }
static QToolButton* sbtn(AudioPanel& panel, int i) { return tbtn(panel, chan_name(i, "_solo").toLatin1().constData()); }

// Which M buttons are checked, as a mask; and the soloed channels as a mask.
static uint16_t m_mask(AudioPanel& panel) {
    uint16_t m = 0;
    for (int i = 0; i < 13; ++i) if (mbtn(panel, i) && mbtn(panel, i)->isChecked()) m |= static_cast<uint16_t>(1u << i);
    return m;
}
static uint16_t s_mask(AudioPanel& panel) {
    uint16_t m = 0;
    for (int i = 0; i < 13; ++i) if (sbtn(panel, i) && sbtn(panel, i)->isChecked()) m |= static_cast<uint16_t>(1u << i);
    return m;
}

static void test_channel_mute_solo(Emulator& emu) {
    set_group("DAP-CHM");

    {
        if (!build_next_emulator(emu)) { check("DAP-31", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        int chips_m = 0, chips_s = 0, dac_m = 0, dac_s = 0, other = 0;
        for (QToolButton* b : panel.findChildren<QToolButton*>()) {
            const QString n = b->objectName();
            if (n.startsWith("chip") && n.endsWith("_mute")) ++chips_m;
            else if (n.startsWith("chip") && n.endsWith("_solo")) ++chips_s;
            else if (n.startsWith("dac_") && n.endsWith("_mute")) ++dac_m;
            else if (n.startsWith("dac_") && n.endsWith("_solo")) ++dac_s;
            else if (!qobject_cast<QTabBar*>(b->parent())) ++other;   // the tab bar's own scroll buttons are not ours
        }
        int boxes = 0;
        for (const char* t : {"AY #0", "AY #1", "AY #2", "DAC", "Beeper"}) if (source_box(panel, t)) ++boxes;
        check("DAP-31", "the Chips tab has 9 M and 9 S buttons and the DAC & Beeper tab 4 + 4, all QToolButtons, none checked on a fresh machine "
              "(mask NONE); the Sources strip is still exactly its five checkboxes (plus Hex, plus the Log tab's two filter boxes)",
              chips_m == 9 && chips_s == 9 && dac_m == 4 && dac_s == 4 && other == 0 && m_mask(panel) == 0 &&
                  s_mask(panel) == 0 && emu.audio_channel_mute_mask() == 0 && boxes == 5 &&
                  panel.findChildren<QCheckBox*>().size() == 6 + 2,   // + the Log tab's two filter boxes (GH #531)
              fmt("chips %d/%d dac %d/%d other %d m=%04X s=%04X boxes %d checkboxes %d", chips_m, chips_s, dac_m, dac_s,
                  other, m_mask(panel), s_mask(panel), boxes, static_cast<int>(panel.findChildren<QCheckBox*>().size())));
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-32", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x02);
        program_ay_dc(emu, 2);
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        const uint32_t before = ay_level(emu);
        // The chip is mono here (measured: the total is 6 x one level, not 4): mono sums A+B+C into
        // BOTH sides (turbosound.vhd:186-192), so muting A removes its output twice.
        const uint32_t share = 2u * emu.turbosound().ay(2).output_a();
        mbtn(panel, 6)->click();
        const uint16_t m1 = emu.audio_channel_mute_mask();
        const uint32_t muted = ay_level(emu);
        mbtn(panel, 6)->click();
        const uint16_t m2 = emu.audio_channel_mute_mask();
        const uint32_t back = ay_level(emu);
        check("DAP-32", "clicking chip 2 A's M sets mask 0x0040 and the AY level drops by exactly that channel's output; clicking again clears it",
              share != 0 && m1 == 0x0040 && muted == before - share && m2 == 0 && back == before,
              fmt("before=%u share=%u m1=%04X muted=%u m2=%04X back=%u", before, share, m1, muted, m2, back));
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-33", "emulator init", false); return; }
        emu.set_audio_channel_mute_mask(AudioChannelMute::DAC_C);
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        sbtn(panel, 5)->click();
        const uint16_t m1 = emu.audio_channel_mute_mask();
        const bool on = sbtn(panel, 5)->isChecked();
        sbtn(panel, 5)->click();
        check("DAP-33", "with DAC C muted, soloing chip 1 C mutes the other twelve and checks its S; clicking it again restores the earlier mask 0x0800, not 0",
              m1 == (0x1FFF & ~0x0020) && on && emu.audio_channel_mute_mask() == 0x0800 && !sbtn(panel, 5)->isChecked() &&
                  s_mask(panel) == 0);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-34", "emulator init", false); return; }
        emu.set_audio_channel_mute_mask(AudioChannelMute::DAC_C);
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        sbtn(panel, 0)->click();
        sbtn(panel, 12)->click();
        const uint16_t m1 = emu.audio_channel_mute_mask();
        const uint16_t s1 = s_mask(panel);
        sbtn(panel, 12)->click();
        check("DAP-34", "soloing chip 0 A then DAC D moves the solo (only D's S checked, mask ALL minus D) and keeps the first saved mask: un-solo gives 0x0800",
              m1 == (0x1FFF & ~AudioChannelMute::DAC_D) && s1 == AudioChannelMute::DAC_D && emu.audio_channel_mute_mask() == 0x0800 &&
                  s_mask(panel) == 0);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-35", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto other = dbg.attach(jnext::dbg::ClientInfo{"dap35", jnext::dbg::ClientKind::Test});
        AudioPanel panel(&dbg);
        sbtn(panel, 0)->click();
        dbg.set_audio_channel_mute_mask(other.value, 0x0003);   // another client changes the mutes
        panel.refresh();
        const bool cancelled = s_mask(panel) == 0 && m_mask(panel) == 0x0003;
        sbtn(panel, 0)->click();
        const uint16_t m1 = emu.audio_channel_mute_mask();
        sbtn(panel, 0)->click();
        check("DAP-35", "a mask changed from outside ends the solo on the next refresh; soloing again saves that mask (0x0003) and un-solo restores it",
              cancelled && m1 == (0x1FFF & ~0x0001) && emu.audio_channel_mute_mask() == 0x0003);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-36", "emulator init", false); return; }
        emu.set_audio_channel_mute_mask(0x1041);
        jnext::dbg::Debugger dbg(emu);
        const auto other = dbg.attach(jnext::dbg::ClientInfo{"dap36", jnext::dbg::ClientKind::Test});
        AudioPanel panel(&dbg);
        const uint16_t seeded = m_mask(panel);
        dbg.set_audio_channel_mute_mask(other.value, 0x0002);
        const uint16_t stale = m_mask(panel);
        panel.refresh();
        check("DAP-36", "a panel built against mask 0x1041 shows exactly those M buttons checked; a later change to 0x0002 shows after refresh()",
              seeded == 0x1041 && stale == 0x1041 && m_mask(panel) == 0x0002);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-37", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto other = dbg.attach(jnext::dbg::ClientInfo{"dap37", jnext::dbg::ClientKind::Test});
        AudioPanel panel(&dbg);
        dbg.set_audio_mute_mask(other.value, AudioMute::AY0 | AudioMute::BEEPER);
        const uint64_t rev = dbg.state_revision();
        panel.refresh();
        check("DAP-37", "a source mute set from outside shows in the Sources boxes after refresh() (AY #0 and Beeper unticked, the rest ticked), "
              "and the refresh pushes nothing: no MUTATE line, the revision does not move",
              !source_box(panel, "AY #0")->isChecked() && source_box(panel, "AY #1")->isChecked() &&
                  source_box(panel, "AY #2")->isChecked() && source_box(panel, "DAC")->isChecked() &&
                  !source_box(panel, "Beeper")->isChecked() && dbg.state_revision() == rev &&
                  emu.audio_mute_mask() == (AudioMute::AY0 | AudioMute::BEEPER));
    }
}

// ── DAP-DAC: the DAC & Beeper sub-tab (GH #530) ────────────────────────
//
// zxnext.vhd:2744-2751,2974-2980 (the port families), :7114 (beep_spkr_excl),
// :2593-2598 (NR 0xA2); the dropped-write reasons are those of DRP-* in
// audio_debug_test. Widgets are found by objectName.

static QString tree_cell(QTreeWidget* t, int row, int col) {
    return t && t->topLevelItem(row) ? t->topLevelItem(row)->text(col) : QStringLiteral("<none>");
}

static const char* const kDropRows[8] = {
    "AY ports off (NR 0x84 bit 0)", "AY held in reset (NR 0x06 = 11)", "Chip select, TurboSound off",
    "Ignored 0xFFFD value", "Register address 16-31", "DAC port family off (NR 0x84)",
    "DAC off (NR 0x08 bit 3)", "DAC mirror, DAC off (NR 0x08 bit 3)"};

// The dropped-write counters are host state, like the mute mask: init() leaves them, and this
// suite reuses one Emulator, so every DAP-DAC row starts from zero counts.
static bool build_dac_machine(Emulator& emu) {
    if (!build_next_emulator(emu)) return false;
    emu.clear_audio_drops();
    return true;
}

static void test_dac_tab(Emulator& emu) {
    set_group("DAP-DAC");

    {
        if (!build_dac_machine(emu)) { check("DAP-38", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        QWidget* dac = tabs ? tabs->widget(1) : nullptr;
        const bool real = dac && dac->findChild<QTreeWidget*>("audio_drops") && dac->findChild<QLabel*>("dac_a_value") &&
                          !dac->findChild<QLabel*>("dac_a_value")->text().isEmpty();
        bool placeholders = tabs != nullptr;
        for (int i = 3; tabs && i < 5; ++i) {
            bool found = false;
            for (QLabel* l : tabs->widget(i)->findChildren<QLabel*>())
                found = found || l->text() == "Not available yet.";
            placeholders = placeholders && found;
        }
        check("DAP-38", "the DAC & Beeper sub-tab holds the dropped-writes table and the DAC values (not the placeholder); Tracker and Scopes still show 'Not available yet.'",
              real && placeholders);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-39", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x08);
        emu.port().out(0x1F, 0x11); emu.port().out(0x0F, 0x22);
        emu.port().out(0x4F, 0x33); emu.port().out(0x5F, 0x44);
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        panel.refresh();
        bool on = txt(panel, "dac_enabled") == "DAC: on";
        static const char* const v[4] = {"dac_a_value", "dac_b_value", "dac_c_value", "dac_d_value"};
        static const char* const b[4] = {"dac_a_bar", "dac_b_bar", "dac_c_bar", "dac_d_bar"};
        for (int i = 0; i < 4; ++i) {
            auto* bar = panel.findChild<QProgressBar*>(b[i]);
            on = on && txt(panel, v[i]) == QString::asprintf("0x%02X", 0x11 * (i + 1)) &&
                 bar && bar->value() == 17 * (i + 1);
        }
        nr_write(emu, 0x08, 0x00);
        panel.refresh();
        bool off = txt(panel, "dac_enabled") == "DAC: off";
        for (int i = 0; i < 4; ++i) {
            auto* bar = panel.findChild<QProgressBar*>(b[i]);
            off = off && txt(panel, v[i]) == "0x80" && bar && bar->value() == 0x80;
        }
        check("DAP-39", "ports 0x1F/0x0F/0x4F/0x5F = 0x11..0x44: the four values and bars (17, 34, 51, 68; the raw byte) and 'DAC: on'; DAC off: 'DAC: off' and every channel 0x80",
              on && off);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-40", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        bool ok = true;
        std::string bad;
        for (int bit = 0; bit < 8; ++bit) {
            nr_write(emu, 0x84, static_cast<uint8_t>(1 << bit));
            panel.refresh();
            for (int n = 1; n <= 7; ++n) {
                const bool want = (n == bit);
                if (lit(panel, QString("dac_family_%1").arg(n).toLatin1().constData()) != want) {
                    ok = false;
                    bad += fmt("bit%d lamp%d ", bit, n);
                }
            }
        }
        nr_write(emu, 0x84, 0x24);
        panel.refresh();
        const bool both = lit(panel, "dac_family_2") && !lit(panel, "dac_family_5");
        nr_write(emu, 0x84, 0x20);
        panel.refresh();
        const bool pent = lit(panel, "dac_family_5") && !lit(panel, "dac_family_2");
        const bool texts = txt(panel, "dac_family_1") == "Soundrive 1: 1F 0F 4F 5F" &&
                           txt(panel, "dac_family_2") == "Soundrive 2: F1 F3 F9 FB" &&
                           txt(panel, "dac_family_3") == "Profi Covox: 3F 5F" &&
                           txt(panel, "dac_family_4") == "Covox: 0F 4F" &&
                           txt(panel, "dac_family_5") == "Pentagon/ATM: FB" &&
                           txt(panel, "dac_family_6") == "GS Covox: B3" &&
                           txt(panel, "dac_family_7") == "Specdrum: DF";
        check("DAP-40", "zxnext.vhd:2744-2751,2749 NR 0x84 one bit at a time: exactly that lamp is lit (bit 0 none); 0x24: Soundrive 2 lit, Pentagon/ATM dark; 0x20: Pentagon/ATM lit; the seven texts",
              ok && both && pent && texts, bad);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-41", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        emu.port().out(0xFE, 0x10);
        panel.refresh();
        const bool ear = lit(panel, "beeper_ear") && !lit(panel, "beeper_mic");
        emu.port().out(0xFE, 0x08);
        panel.refresh();
        const bool mic = !lit(panel, "beeper_ear") && lit(panel, "beeper_mic");
        emu.beeper().set_tape_ear(true);
        panel.refresh();
        const bool tape = lit(panel, "beeper_tape_ear");
        bool combos = true;
        for (int sp = 0; sp < 2; ++sp)
            for (int en = 0; en < 2; ++en) {
                nr_write(emu, 0x06, sp ? 0x40 : 0x00);
                nr_write(emu, 0x08, en ? 0x10 : 0x00);
                panel.refresh();
                combos = combos &&
                    txt(panel, "speaker_enabled") == (en ? "Internal speaker: on" : "Internal speaker: off") &&
                    txt(panel, "beep_speaker_only") == (sp ? "Beep to speaker only: on" : "Beep to speaker only: off") &&
                    txt(panel, "beeper_excluded") == ((sp && en) ? "EAR/MIC reach the mix: no" : "EAR/MIC reach the mix: yes");
            }
        check("DAP-41", "zxnext.vhd:7114 EAR (0x10) and MIC (0x08) and tape EAR lamps; NR 0x06 bit 6 x NR 0x08 bit 4 in all four combinations: the two lines follow their bits and EAR/MIC leave the mix only when both are set",
              ear && mic && tape && combos);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-42", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        nr_write(emu, 0xA2, 0x81); panel.refresh();
        const bool a = lit(panel, "pi_left") && !lit(panel, "pi_right") && txt(panel, "pi_ear") == "Pi to EAR: on";
        nr_write(emu, 0xA2, 0x41); panel.refresh();
        const bool b = !lit(panel, "pi_left") && lit(panel, "pi_right") && txt(panel, "pi_ear") == "Pi to EAR: on";
        nr_write(emu, 0xA2, 0x01); panel.refresh();
        const bool c = !lit(panel, "pi_left") && !lit(panel, "pi_right") && txt(panel, "pi_ear") == "Pi to EAR: off";
        check("DAP-42", "zxnext.vhd:2593-2598 NR 0xA2 = 0x81, 0x41, 0x01: lamps (L,R) = (lit,dark), (dark,lit), (dark,dark); 'Pi to EAR' follows the routing",
              a && b && c);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-43", "emulator init", false); return; }
        emu.mixer().set_output_gain_db(-3.0f); emu.mixer().set_ay_gain_db(0, -1.0f);
        emu.mixer().set_ay_gain_db(1, -2.0f); emu.mixer().set_ay_gain_db(2, -4.0f);
        emu.mixer().set_dac_gain_db(-9.0f); emu.mixer().set_beeper_gain_db(-6.0f);
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        panel.refresh();
        const bool gains = txt(panel, "gain_output") == "-3.0 dB" && txt(panel, "gain_ay0") == "-1.0 dB" &&
                           txt(panel, "gain_ay1") == "-2.0 dB" && txt(panel, "gain_ay2") == "-4.0 dB" &&
                           txt(panel, "gain_dac") == "-9.0 dB" && txt(panel, "gain_beeper") == "-6.0 dB";
        const bool none = txt(panel, "host_mutes") == "Muted: none" && txt(panel, "host_synthesis") == "Synthesis: on";
        emu.set_audio_mute_mask(AudioMute::AY1 | AudioMute::DAC);
        panel.refresh();
        const QString muted = txt(panel, "host_mutes");
        emu.set_audio_mute_mask(AudioMute::AY0 | AudioMute::AY2 | AudioMute::BEEPER);
        panel.refresh();
        const QString muted2 = txt(panel, "host_mutes");
        // GH #533: the channel mutes join the line (AY channels as "AY #c X", DAC channels as "DAC X").
        emu.set_audio_mute_mask(AudioMute::AY1);
        emu.set_audio_channel_mute_mask(AudioChannelMute::DAC_C | AudioChannelMute::ay_bit(0, 1));
        panel.refresh();
        const QString muted3 = txt(panel, "host_mutes");
        emu.set_audio_channel_mute_mask(AudioChannelMute::NONE);
        emu.set_audio_mute_mask(AudioMute::NONE);
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0; cfg.silent = true;
        Emulator quiet; quiet.init(cfg);
        jnext::dbg::Debugger dq(quiet);
        AudioPanel pq(&dq);
        pq.refresh();
        check("DAP-43", "host settings: gains -3/-1/-2/-4/-9/-6 dB as set; 'Muted: none' then 'Muted: AY #1, DAC' and 'Muted: AY #0, AY #2, Beeper'; with a source mute and two channel mutes 'Muted: AY #1, AY #0 B, DAC C'; 'Synthesis: on', and 'Synthesis: off (--silent)' with silent set",
              gains && none && muted == "Muted: AY #1, DAC" && muted2 == "Muted: AY #0, AY #2, Beeper" &&
              muted3 == "Muted: AY #1, AY #0 B, DAC C" && txt(pq, "host_synthesis") == "Synthesis: off (--silent)", (muted + "|" + muted2).toStdString());
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-44", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const jnext::dbg::ClientId cid = dbg.attach(jnext::dbg::ClientInfo{"dap44", jnext::dbg::ClientKind::Test}).value;
        AudioPanel panel(&dbg);
        auto* t = panel.findChild<QTreeWidget*>("audio_drops");
        // Two frames with the CPU parked (DI; JR $), so the frame tag is not 0.
        {
            const uint8_t prog[] = {0xF3, 0x18, 0xFE};
            for (size_t i = 0; i < sizeof(prog); ++i) emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
            Z80Registers r = emu.cpu().get_registers();
            r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
            emu.cpu().set_registers(r);
            emu.run_frame(); emu.run_frame();
        }
        panel.refresh();
        bool fresh_ok = t && t->topLevelItemCount() == 8;
        for (int i = 0; fresh_ok && i < 8; ++i) {
            fresh_ok = tree_cell(t, i, 0) == kDropRows[i] && tree_cell(t, i, 1) == "0";
            for (int col = 2; col < 7; ++col) fresh_ok = fresh_ok && tree_cell(t, i, col) == "-";
        }
        nr_write(emu, 0x84, 0xBF);
        nr_write(emu, 0x08, 0x00);
        // A DMA byte to port 0x00DF (b7 on, DAC off): a DacDisabled drop with source DMA.
        emu.mmu().write(0xA000, 0x5A);
        {
            auto w = [&](uint8_t v) { emu.dma().write(v, false); };
            w(0x7D); w(0x00); w(0xA0); w(0x01); w(0x00);
            w(0x14); w(0x28); w(0xAD); w(0xDF); w(0x00);
            w(0xCF); w(0x87);
            emu.run_frame();
        }
        dbg.port_out(cid, 0x12B3, 0x55);
        nr_write(emu, 0x2D, 0x66);
        panel.refresh();
        const uint32_t frame = emu.frame_num() > 0 ? emu.frame_num() - 1 : 0;
        const bool fam = tree_cell(t, 5, 1) == "1" && tree_cell(t, 5, 2) == "0x12B3" && tree_cell(t, 5, 3) == "0x55" &&
                         tree_cell(t, 5, 4) == "-" && tree_cell(t, 5, 5) == QString::number(frame) &&
                         tree_cell(t, 5, 6) == "Debugger";
        const bool mir = tree_cell(t, 7, 1) == "1" && tree_cell(t, 7, 2) == "NR 0x2D" && tree_cell(t, 7, 3) == "0x66" &&
                         tree_cell(t, 7, 4) == "-" && tree_cell(t, 7, 6) == "Debugger";
        const bool dma = tree_cell(t, 6, 1) == "1" && tree_cell(t, 6, 2) == "0x00DF" && tree_cell(t, 6, 3) == "0x5A" &&
                         tree_cell(t, 6, 4) == "-" && tree_cell(t, 6, 6) == "DMA" && tree_cell(t, 6, 5).toUInt() > 0;
        bool rest = true;
        for (int i : {0, 1, 2, 3, 4}) rest = rest && tree_cell(t, i, 1) == "0" && tree_cell(t, i, 2) == "-";
        check("DAP-44", "the table has 8 rows in reason order with their texts; fresh: Count 0 and '-'; a debugger write to 0x12B3 with b6 off fills the family row (1, 0x12B3, 0x55, PC '-', a nonzero frame, Debugger); an NR 0x2D drop shows 'NR 0x2D'; a DMA byte shows source DMA",
              fresh_ok && fam && mir && dma && rest && frame > 0);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-45", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const jnext::dbg::ClientId cid = dbg.attach(jnext::dbg::ClientInfo{"dap45", jnext::dbg::ClientKind::Test}).value;
        AudioPanel panel(&dbg);
        auto* t = panel.findChild<QTreeWidget*>("audio_drops");
        nr_write(emu, 0x84, 0xBF);
        const uint8_t prog[] = {0x01, 0xB3, 0x12, 0x3E, 0x55, 0xED, 0x79, 0x18, 0xFE};
        for (size_t i = 0; i < sizeof(prog); ++i) emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
        emu.cpu().set_registers(r);
        for (int i = 0; i < 3; ++i) emu.execute_single_instruction();
        panel.refresh();
        int emitted = 0, addr = -1;
        QObject::connect(&panel, &AudioPanel::show_in_disassembly_requested, &panel, [&](uint16_t a) { ++emitted; addr = a; });
        const bool pc = tree_cell(t, 5, 4) == "0x8005" && tree_cell(t, 5, 6) == "CPU";
        emit t->itemDoubleClicked(t->topLevelItem(5), 0);
        const bool cpu_row = emitted == 1 && addr == 0x8005;
        emit t->itemDoubleClicked(t->topLevelItem(0), 0);                // a zero row
        dbg.port_out(cid, 0x12B3, 0x77);                                  // now the latest drop is the debugger's
        panel.refresh();
        emit t->itemDoubleClicked(t->topLevelItem(5), 0);                // a Debugger-source row
        check("DAP-45", "a CPU-source drop (OUT at 0x8005) shows PC 0x8005 and source CPU; double-click on it emits show_in_disassembly_requested(0x8005) once; on a zero row or a Debugger-source row it emits nothing",
              pc && cpu_row && emitted == 1);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-46", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* t = panel.findChild<QTreeWidget*>("audio_drops");
        auto* clear = panel.findChild<QPushButton*>("audio_drops_clear");
        nr_write(emu, 0x84, 0xBC);
        nr_write(emu, 0x08, 0x00);
        emu.port().out(0xFFFD, 0x08);
        emu.port().out(0x1F, 0x01);
        nr_write(emu, 0x2D, 0x02);
        // The last family drop is a CPU one: OUT (C),A to 0x12B3 at PC 0x8005.
        const uint8_t prog[] = {0x01, 0xB3, 0x12, 0x3E, 0x55, 0xED, 0x79, 0x18, 0xFE};
        for (size_t i = 0; i < sizeof(prog); ++i) emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
        emu.cpu().set_registers(r);
        for (int i = 0; i < 3; ++i) emu.execute_single_instruction();
        panel.refresh();
        const bool filled = tree_cell(t, 0, 1) == "1" && tree_cell(t, 5, 1) == "2" && tree_cell(t, 7, 1) == "1" &&
                            tree_cell(t, 5, 4) == "0x8005";
        int emitted = 0;
        QObject::connect(&panel, &AudioPanel::show_in_disassembly_requested, &panel, [&](uint16_t) { ++emitted; });
        if (clear) clear->click();
        bool zero = clear != nullptr;
        for (const auto& d : dbg.audio_state().drops) zero = zero && d.count == 0;
        for (int i = 0; i < 8; ++i) {
            zero = zero && tree_cell(t, i, 1) == "0";
            for (int col = 2; col < 7; ++col) zero = zero && tree_cell(t, i, col) == "-";
        }
        emit t->itemDoubleClicked(t->topLevelItem(5), 0);   // the former CPU row
        check("DAP-46", "Clear zeroes every counter in the backend; the table shows every Count 0 and '-' in the port, value, PC, frame and source columns, and a double-click on the former CPU row emits nothing",
              filled && zero && emitted == 0);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-47", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        auto* link = panel.findChild<QLabel*>("ay_drops");
        nr_write(emu, 0x84, 0xFC);          // AY ports off, Soundrive 1 off
        nr_write(emu, 0x08, 0x00);
        emu.port().out(0xFFFD, 0x08);       // AyPortDisabled
        emu.port().out(0x1F, 0x01);         // DAC family drop
        nr_write(emu, 0x84, 0xFF);
        nr_write(emu, 0x06, 0x03);
        emu.port().out(0xFFFD, 0x08);       // AyHeldInReset
        nr_write(emu, 0x06, 0x01);
        emu.port().out(0xFFFD, 0xFE);       // AyChipSelectTurboOff (TurboSound off)
        emu.port().out(0xFFFD, 0x40);       // AySelectIgnored
        emu.port().out(0xFFFD, 0x15);
        emu.port().out(0xBFFD, 0x01);       // AyAddressHigh
        emu.port().out(0x1F, 0x02);         // DAC off drop
        nr_write(emu, 0x2D, 0x03);          // DAC mirror drop
        panel.refresh();
        const bool text = link && link->text().startsWith("AY writes dropped: 5 ");
        const bool on_chips = panel.chips_on_show();
        if (link) emit link->linkActivated("drops");
        check("DAP-47", "one drop under each of the five AY reasons and three DAC drops: the Chips header reads 'AY writes dropped: 5'; its link makes DAC & Beeper the current sub-tab",
              text && on_chips && tabs && tabs->currentIndex() == 1 && !panel.chips_on_show());
    }
}

// ── DAP-LOG: the Log sub-tab (GH #531) ─────────────────────────────────
//
// The rows drive real writes (a guest program, DMA, the debugger) and read the table's cells.
// Oracles are those of AWL-* in audio_debug_test (ym2149.vhd:188,207-210; zxnext.vhd:2965,
// 2974-2980, 5412-5414; soundrive.vhd:85-101); the cell texts are this tab's own spec.

static QTableView* log_table(AudioPanel& panel) { return panel.findChild<QTableView*>("audio_log_table"); }

static QString log_cell(AudioPanel& panel, int row, int col) {
    QTableView* t = log_table(panel);
    return t && row < t->model()->rowCount() ? t->model()->index(row, col).data().toString()
                                             : QStringLiteral("<none>");
}

static int log_rows(AudioPanel& panel) {
    QTableView* t = log_table(panel);
    return t ? t->model()->rowCount() : -1;
}

static void show_log_tab(AudioPanel& panel) {
    if (auto* tabs = panel.findChild<QTabWidget*>("audio_tabs")) tabs->setCurrentIndex(2);
}

static void set_combo(AudioPanel& panel, const char* name, int data) {
    if (auto* c = panel.findChild<QComboBox*>(name)) c->setCurrentIndex(c->findData(data));
}

// Every visible row is a marker or passes `keep(row)`, and the count of the others is `want`.
template <class Keep>
static bool only_rows(AudioPanel& panel, int want, Keep keep) {
    int others = 0;
    bool ok = true;
    for (int r = 0; r < log_rows(panel); ++r) {
        if (log_cell(panel, r, 5) == "marker") continue;
        ++others;
        ok = ok && keep(r);
    }
    return ok && others == want;
}

static int marker_rows(AudioPanel& panel) {
    int n = 0;
    for (int r = 0; r < log_rows(panel); ++r) n += log_cell(panel, r, 5) == "marker";
    return n;
}

// The guest program at 0x8000, the CPU parked on it.
static void cpu_program(Emulator& emu, std::initializer_list<uint8_t> code) {
    uint16_t a = 0x8000;
    for (uint8_t b : code) emu.mmu().write(a++, b);
    Z80Registers r = emu.cpu().get_registers();
    r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

static void run_instructions(Emulator& emu, int n) {
    for (int i = 0; i < n; ++i) emu.execute_single_instruction();
}

static void ay_wr(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0xFFFD, reg);
    emu.port().out(0xBFFD, val);
}

// A machine whose log holds a known mix, a soft reset marker first:
//   marker | AY0 select | AY0 R7 | AY1 select | AY1 R7 | AY1 R8 | DAC A (0x1F) | DAC B (0x0F) |
//   DAC A+D (0xDF) | AY1 R13 (restart) | a dropped 0xB3 (b6 off)
// The DAC B row (channel 1, reg 0) sits on the other side of every target condition of the chip, reg
// and DAC filters: an AY chip 1 filter must not pick it, and a DAC channel A filter must not pick the
// AY chip 0 rows.
static void fill_mix(Emulator& emu, jnext::dbg::Debugger& dbg, jnext::dbg::ClientId cid) {
    dbg.reset(cid, jnext::dbg::ResetKind::Soft);
    nr_write(emu, 0x08, 0x0A);
    emu.port().out(0xFFFD, 0xFF);
    ay_wr(emu, 7, 0x38);
    emu.port().out(0xFFFD, 0xFE);
    ay_wr(emu, 7, 0x11);
    ay_wr(emu, 8, 0x05);
    emu.port().out(0x1F, 0x11);
    emu.port().out(0x0F, 0x33);
    emu.port().out(0xDF, 0x22);
    ay_wr(emu, 13, 0x08);
    nr_write(emu, 0x84, 0xBF);
    emu.port().out(0xB3, 0x55);
}

static void test_log_tab(Emulator& emu) {
    set_group("DAP-LOG");
    using jnext::dbg::ClientInfo;
    using jnext::dbg::ClientKind;

    {
        if (!build_dac_machine(emu)) { check("DAP-48", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        const auto tables = panel.findChildren<QTableWidget*>();
        bool placeholders = tabs != nullptr;
        for (int i = 3; tabs && i < 5; ++i) {
            bool found = false;
            for (QLabel* l : tabs->widget(i)->findChildren<QLabel*>())
                found = found || l->text() == "Not available yet.";
            placeholders = placeholders && found;
        }
        bool only_log = tabs != nullptr;
        for (int i = 0; tabs && i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            only_log = only_log && (panel.log_on_show() == (i == 2));
        }
        QWidget* log_tab = tabs ? tabs->widget(2) : nullptr;
        check("DAP-48", "tab 2 holds audio_log_table, a QTableView; the hex table is still the panel's only QTableWidget; Tracker and Scopes still show 'Not available yet.'; log_on_show() is true only with Log current",
              log_tab && log_tab->findChild<QTableView*>("audio_log_table") && tables.size() == 1 &&
              tables[0]->objectName() != "audio_log_table" && placeholders && only_log);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-49", "emulator init", false); return; }
        nr_write(emu, 0x08, 0x08);
        ay_wr(emu, 7, 0x3F);
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap49", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        // 8000 LD BC,FFFD  8003 LD A,7  8005 OUT (C),A  8007 LD B,BF  8009 LD A,38  800B OUT (C),A
        // 800D LD A,11  800F OUT (1F),A  8011 NEXTREG 2D,22  8015 LD A,10  8017 OUT (FE),A
        cpu_program(emu, {0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x38, 0xED, 0x79,
                          0x3E, 0x11, 0xD3, 0x1F, 0xED, 0x91, 0x2D, 0x22, 0x3E, 0x10, 0xD3, 0xFE, 0x18, 0xFE});
        run_instructions(emu, 11);
        panel.refresh();
        const bool ay = log_rows(panel) == 5 && log_cell(panel, 0, 5) == "CPU" && log_cell(panel, 0, 6) == "AY register" &&
                        log_cell(panel, 0, 7) == "0" && log_cell(panel, 0, 8) == "R7" && log_cell(panel, 0, 9) == "0x38" &&
                        log_cell(panel, 0, 10) == "0x3F" && log_cell(panel, 0, 4) == "0x800B";
        const bool dac = log_cell(panel, 1, 6) == "DAC" && log_cell(panel, 1, 7) == "A" && log_cell(panel, 1, 8) == "-" &&
                         log_cell(panel, 1, 9) == "0x11" && log_cell(panel, 1, 4) == "0x800F";
        const bool mir = log_cell(panel, 2, 6) == "DAC mirror" && log_cell(panel, 2, 7) == "A" &&
                         log_cell(panel, 2, 8) == "NR 0x2D" && log_cell(panel, 2, 9) == "0x22" &&
                         log_cell(panel, 3, 7) == "D" && log_cell(panel, 3, 8) == "NR 0x2D";
        const bool beep = log_cell(panel, 4, 6) == "Beeper" && log_cell(panel, 4, 7) == "-" &&
                          log_cell(panel, 4, 9) == "0x10" && log_cell(panel, 4, 10) == "0x00";
        check("DAP-49", "the AWL-01 write, a 0x1F write, an NR 0x2D write and an EAR change show rows: R7/0x38/0x3F/CPU/'AY register'/PC 0x800B; DAC A 0x11; the mirror as A and D with 'NR 0x2D'; Beeper 0x10 over 0x00",
              ay && dac && mir && beep);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-50", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap50", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        ay_wr(emu, 13, 0x08);
        nr_write(emu, 0x84, 0xBF);
        nr_write(emu, 0x08, 0x08);
        emu.port().out(0xB3, 0x55);
        panel.refresh();
        auto* drops = panel.findChild<QTreeWidget*>("audio_drops");
        const QString family = tree_cell(drops, 5, 0);
        check("DAP-50", "an R13 row's Flags cell reads 'restart'; a dropped 0xB3 row reads 'dropped: ' followed by exactly the text the DAC & Beeper drop table shows for DacFamilyDisabled",
              log_rows(panel) == 2 && log_cell(panel, 0, 11) == "restart" &&
              log_cell(panel, 1, 11) == QString("dropped: ") + family && family == kDropRows[5]);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-51", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap51", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        fill_mix(emu, dbg, cid);
        panel.refresh();
        const int all = log_rows(panel);
        set_combo(panel, "audio_log_target", int(jnext::dbg::AudioTarget::Dac));
        const bool dac = only_rows(panel, 5, [&](int r) { return log_cell(panel, r, 6) == "DAC"; }) && marker_rows(panel) == 1;
        set_combo(panel, "audio_log_target", -1);
        check("DAP-51", "the audio_log_target filter 'DAC' leaves only the 5 DAC rows plus the marker row; 'Any' restores every row",
              all == 12 && dac && log_rows(panel) == all);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-52", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap52", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        fill_mix(emu, dbg, cid);
        panel.refresh();
        set_combo(panel, "audio_log_chip", 1);
        check("DAP-52", "audio_log_chip = 1 (AY writes on chips 0 and 1, and a DAC channel B write, chip 1) leaves chip 1's four AY rows (select, R7, R8, R13) plus the marker, not the DAC B row",
              only_rows(panel, 4, [&](int r) { return log_cell(panel, r, 7) == "1" && log_cell(panel, r, 6).startsWith("AY"); }) &&
              marker_rows(panel) == 1);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-53", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap53", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        fill_mix(emu, dbg, cid);
        panel.refresh();
        set_combo(panel, "audio_log_reg", 7);
        const bool r7 = only_rows(panel, 2, [&](int r) { return log_cell(panel, r, 8) == "R7"; }) && marker_rows(panel) == 1;
        // The DAC rows carry reg 0 underneath ("-"): a register-0 filter must not pick them.
        set_combo(panel, "audio_log_reg", 0);
        const bool r0 = only_rows(panel, 0, [&](int) { return false; }) && marker_rows(panel) == 1;
        check("DAP-53", "audio_log_reg = 7 leaves the two R7 rows plus the marker; audio_log_reg = 0 leaves no write (no AY R0 write was made, and the DAC rows, whose reg is 0 underneath, are not AY register rows)",
              r7 && r0);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-54", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap54", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        fill_mix(emu, dbg, cid);
        panel.refresh();
        set_combo(panel, "audio_log_dac", 3);
        const bool d = only_rows(panel, 1, [&](int r) { return log_cell(panel, r, 7) == "D" && log_cell(panel, r, 6) == "DAC"; }) &&
                       marker_rows(panel) == 1;
        // Channel A is also AY chip 0 (chip cell "0"): the AY rows of chip 0 must not be picked.
        set_combo(panel, "audio_log_dac", 0);
        const bool a = only_rows(panel, 2, [&](int r) { return log_cell(panel, r, 7) == "A" && log_cell(panel, r, 6) == "DAC"; }) &&
                       marker_rows(panel) == 1;
        check("DAP-54", "audio_log_dac = D leaves the one channel-D row plus the marker; audio_log_dac = A leaves the two channel-A rows (0x1F, 0xDF), not the AY chip 0 rows",
              d && a);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-55", "emulator init", false); return; }
        nr_write(emu, 0x84, 0xFF); nr_write(emu, 0x08, 0x08);
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap55", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        cpu_program(emu, {0xF3, 0x18, 0xFE});   // parked while the DMA runs
        emu.mmu().write(0xA000, 0x5A);
        {
            auto w = [&](uint8_t v) { emu.dma().write(v, false); };
            w(0x7D); w(0x00); w(0xA0); w(0x01); w(0x00); w(0x14); w(0x28);
            w(0xAD); w(0xDF); w(0x00); w(0xCF); w(0x87);
            emu.run_frame();
        }
        cpu_program(emu, {0x3E, 0x33, 0xD3, 0x1F, 0x18, 0xFE});
        run_instructions(emu, 2);
        panel.refresh();
        const int all = log_rows(panel);
        set_combo(panel, "audio_log_source", int(jnext::dbg::EventSource::Dma));
        const bool dma = only_rows(panel, 2, [&](int r) { return log_cell(panel, r, 5) == "DMA"; });
        check("DAP-55", "audio_log_source = DMA (a DMA byte to 0xDF and a CPU write) leaves the two DMA rows; Any shows all three",
              all == 3 && dma);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-56", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap56", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        fill_mix(emu, dbg, cid);
        panel.refresh();
        auto* restarts = panel.findChild<QCheckBox*>("audio_log_restarts");
        auto* dropped = panel.findChild<QCheckBox*>("audio_log_dropped");
        if (restarts) restarts->setChecked(true);
        const bool r13 = only_rows(panel, 1, [&](int r) { return log_cell(panel, r, 11) == "restart"; }) && marker_rows(panel) == 1;
        if (restarts) restarts->setChecked(false);
        if (dropped) dropped->setChecked(true);
        const bool drop = only_rows(panel, 1, [&](int r) { return log_cell(panel, r, 11).startsWith("dropped: "); }) &&
                          marker_rows(panel) == 1;
        set_combo(panel, "audio_log_target", int(jnext::dbg::AudioTarget::Beeper));
        const bool still = marker_rows(panel) == 1 && log_cell(panel, 0, 6) == "reset";
        check("DAP-56", "'Envelope restarts only' leaves the R13 row and 'Dropped only' the dropped row; the soft-reset marker row stays visible under every filter (and its Target reads 'reset')",
              restarts && dropped && r13 && drop && still);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-57", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap57", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        dbg.reset(cid, jnext::dbg::ResetKind::Soft);
        cpu_program(emu, {0x3E, 0x33, 0xD3, 0x1F, 0x18, 0xFE});
        run_instructions(emu, 2);
        emu.port().out(0x0F, 0x44);
        panel.refresh();
        int emitted = 0, addr = -1;
        QObject::connect(&panel, &AudioPanel::show_in_disassembly_requested, &panel, [&](uint16_t a) { ++emitted; addr = a; });
        QTableView* t = log_table(panel);
        auto dbl = [&](int row) { if (t) emit t->doubleClicked(t->model()->index(row, 0)); };
        const bool rows = log_rows(panel) == 3 && log_cell(panel, 0, 5) == "marker" && log_cell(panel, 1, 5) == "CPU" &&
                          log_cell(panel, 2, 5) == "Debugger";
        dbl(1);
        const bool cpu = emitted == 1 && addr == 0x8002;
        dbl(2);
        dbl(0);
        check("DAP-57", "a double-click on a CPU row (the OUT at 0x8002) emits show_in_disassembly_requested(0x8002) once; on a Debugger-source row or a marker row it emits nothing",
              rows && cpu && emitted == 1);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-58", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap58", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        fill_mix(emu, dbg, cid);
        panel.refresh();
        const std::string a = (std::filesystem::temp_directory_path() / "jnext-dap58-a.csv").string();
        const std::string b = (std::filesystem::temp_directory_path() / "jnext-dap58-b.csv").string();
        auto* view = panel.findChild<AudioLogView*>();
        const bool wrote = view && view->export_to(QString::fromStdString(a)) &&
                           dbg.audio_log_export(b) == jnext::dbg::Result::Ok;
        auto slurp = [](const std::string& p) {
            std::string s;
            if (std::FILE* f = std::fopen(p.c_str(), "rb")) {
                char buf[4096]; size_t n;
                while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
                std::fclose(f);
            }
            return s;
        };
        const std::string sa = slurp(a), sb = slurp(b);
        std::remove(a.c_str()); std::remove(b.c_str());
        check("DAP-58", "export_to(path) writes a file whose bytes equal Debugger::audio_log_export of the same log (the header and 12 rows)",
              wrote && !sa.empty() && sa == sb && std::count(sa.begin(), sa.end(), '\n') == 13);
    }
    {
        Emulator rw;
        {
            EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
            rw.init(cfg);
            rw.set_audio_mute_mask(AudioMute::NONE);
            rw.clear_audio_drops();
            rw.set_rewind_enabled(true);
            rw.resize_rewind_buffer(50);
        }
        jnext::dbg::Debugger dbg(rw);
        const auto cid = dbg.attach(ClientInfo{"dap59", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        cpu_program(rw, {0xF3, 0x3C, 0xD3, 0x1F, 0x06, 0xFF, 0x10, 0xFE, 0x18, 0xF7});
        for (int i = 0; i < 6; ++i) rw.run_frame();
        panel.refresh();
        dbg.pause(cid);
        const size_t n0 = dbg.audio_log_entries(0).value.size();
        for (int i = 0; i < 100000 && dbg.audio_log_entries(0).value.size() == n0; ++i) rw.execute_single_instruction();
        panel.refresh();
        const jnext::dbg::Result r = dbg.step_back(cid, 1);
        panel.refresh();
        const auto entries = dbg.audio_log_entries(0).value;
        bool mirror = r == jnext::dbg::Result::Ok && log_rows(panel) == int(entries.size()) && !entries.empty() &&
                      entries.back().marker == jnext::dbg::AudioMarker::Rewind;
        for (size_t i = 0; mirror && i < entries.size(); ++i) {
            const QString want = entries[i].marker != jnext::dbg::AudioMarker::None
                                     ? QString() : QString::asprintf("0x%02X", entries[i].value);
            mirror = log_cell(panel, int(i), 9) == want && log_cell(panel, int(i), 0) == QString::number(entries[i].frame);
        }
        const bool rec = txt(panel, "audio_log_status") == "Recording";
        dbg.set_audio_log_enabled(cid, false);
        panel.refresh();
        const bool off = txt(panel, "audio_log_status") == "Not recording";
        dbg.set_audio_log_enabled(cid, true);
        for (int i = 0; i < 100000 && dbg.audio_log_entries(0).value.empty(); ++i) rw.execute_single_instruction();
        panel.refresh();
        const auto fresh = dbg.audio_log_entries(0).value;
        check("DAP-59", "after writes, a step back landing and refresh() the rows equal audio_log_entries(0) (truncated, the Rewind marker last); after the request is dropped and taken again the rows are only the new entries; the status label reads 'Recording', then 'Not recording'",
              mirror && rec && off && log_rows(panel) == int(fresh.size()) && !fresh.empty() &&
              fresh.front().seq > entries.back().seq);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-60", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap60", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_log_tab(panel);
        auto* view = panel.findChild<AudioLogView*>();
        if (view) view->set_row_cap(5);
        nr_write(emu, 0x08, 0x08);
        // 12 writes in three refreshes: the model is bounded to the cap and holds the NEWEST rows, and the
        // incremental fetch keeps working across the trim (no gap, no spurious resync).
        for (uint8_t v = 1; v <= 4; ++v) emu.port().out(0x1F, v);
        panel.refresh();
        for (uint8_t v = 5; v <= 9; ++v) emu.port().out(0x1F, v);
        panel.refresh();
        for (uint8_t v = 10; v <= 12; ++v) emu.port().out(0x1F, v);
        panel.refresh();
        bool newest = log_rows(panel) == 5;
        for (int i = 0; newest && i < 5; ++i) newest = log_cell(panel, i, 9) == QString::asprintf("0x%02X", 8 + i);
        // A marker resyncs from the whole ring: still capped.
        dbg.reset(cid, jnext::dbg::ResetKind::Soft);
        panel.refresh();
        const bool marker = log_rows(panel) <= 5 && marker_rows(panel) == 1;
        check("DAP-60", "the Log view holds at most its row cap (5): after 12 writes over three refreshes it shows the newest five (values 8-12) in order, and a resync after a marker is capped too",
              view && newest && marker);
    }
}

int main(int argc, char** argv) {
    // A QWidget needs a QApplication, but not a display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    Emulator emu;
    test_stereo_mode(emu);
    std::printf("  Group: DAP-STEREO     — done\n");
    test_ay_ym_mode(emu);
    std::printf("  Group: DAP-AYMODE     — done\n");
    test_source_mutes(emu);
    std::printf("  Group: DAP-MUTE       — done\n");
    test_mute_is_not_machine_state(emu);
    std::printf("  Group: DAP-INVIS      — done\n");
    test_ay_register_table(emu);
    std::printf("  Group: DAP-REGS       — done\n");
    test_turbosound_label(emu);
    std::printf("  Group: DAP-TS         — done\n");
    test_decoded_view(emu);
    std::printf("  Group: DAP-DEC        -- done\n");
    test_channel_mute_solo(emu);
    std::printf("  Group: DAP-CHM        -- done\n");

    test_dac_tab(emu);
    std::printf("  Group: DAP-DAC        -- done\n");
    test_log_tab(emu);
    std::printf("  Group: DAP-LOG        -- done\n");

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
