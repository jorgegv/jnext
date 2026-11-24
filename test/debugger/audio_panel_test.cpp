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
// Rows DAP-61..69 (GH #534) drive the Tracker sub-tab: the model's grid cell for cell, the live grouping
// of real writes, the rewind double-click and why a row is greyed.
// Rows DAP-81..87 (GH #535) drive the player timing strip under the Tracker: its objects, its statistics
// against the core helper and against hand-grouped log stamps of a real IM2 player, the per-row details,
// the dot plot, the window box and the reset / rewind / log-off paths.
// Rows DAP-88..92 (GH #538) drive the Record PSG... row of the Chips tab: its widgets, a recording through the panel and
// one started elsewhere, a refused path, and the panel's bytes against the backend verb's.
// Run: ./build/test/debugger_audio_panel_test

#include "audio/audio_mute.h"
#include "audio/mixer.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/audio_frames.h"
#include "debug/debugger.h"
#include "debug/dac_stream.h"
#include "debugger/audio_log_view.h"
#include "debugger/audio_panel.h"
#include "debugger/audio_scope_view.h"
#include "debugger/audio_stream_view.h"
#include "debugger/audio_tracker_view.h"
#include "debug/audio_math.h"
#include "platform/emulator_boot.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QImage>
#include <QProgressBar>
#include <QSpinBox>
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
#include <unistd.h>
#include <set>
#include <cstdarg>
#include <cstdint>
#include <chrono>
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
        bool all_real = tabs != nullptr;
        for (int i = 0; tabs && i < tabs->count(); ++i) {
            bool found = false;
            for (QLabel* l : tabs->widget(i)->findChildren<QLabel*>())
                found = found || l->text() == "Not available yet.";
            all_real = all_real && !found;
        }
        check("DAP-38", "the DAC & Beeper sub-tab holds the dropped-writes table and the DAC values; no Audio sub-tab is a placeholder (none shows 'Not available yet.')",
              real && all_real && tabs->count() == 5);
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
        bool all_real = tabs != nullptr;
        for (int i = 0; tabs && i < tabs->count(); ++i) {
            bool found = false;
            for (QLabel* l : tabs->widget(i)->findChildren<QLabel*>())
                found = found || l->text() == "Not available yet.";
            all_real = all_real && !found;
        }
        bool only_log = tabs != nullptr;
        for (int i = 0; tabs && i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            only_log = only_log && (panel.log_on_show() == (i == 2));
        }
        QWidget* log_tab = tabs ? tabs->widget(2) : nullptr;
        check("DAP-48", "tab 2 holds audio_log_table, a QTableView; the hex table is still the panel's only QTableWidget; no Audio sub-tab is a placeholder; log_on_show() is true only with Log current",
              log_tab && log_tab->findChild<QTableView*>("audio_log_table") && tables.size() == 1 &&
              tables[0]->objectName() != "audio_log_table" && all_real && only_log);
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


// ── DAP-STREAM: the Pacing and Sample stream groups of DAC & Beeper (GH #537) ──
//
// Oracles: the CTC rates are 28 MHz / (prescaler x TC) (ctc_chan.vhd:132-170), the DMA's 28 MHz / (32 x
// prescaler) (dma.vhd:424), the stream rows the dac_stream_stats() of the same log entries (DSS-* in
// audio_debug_test pin the helper). The DSS-13 program (halt_bench.asm CTCDAC) is hand-placed here too.

static void ctc_wr(Emulator& emu, int ch, uint8_t control, int tc = -1) {
    const uint16_t port = static_cast<uint16_t>(0x183B + (ch << 8));
    emu.port().out(port, control);
    if (tc >= 0) emu.port().out(port, static_cast<uint8_t>(tc));
}

// A burst from memory 0x9000 to port 0x00DF at prescaler 55, auto-restarting, through port 0x6B.
static void dma_to_dac(Emulator& emu) {
    for (int i = 0; i < 256; ++i) emu.mmu().write(static_cast<uint16_t>(0x9000 + i), static_cast<uint8_t>(i));
    const uint8_t prog[] = {0xA2, 0x7D, 0x00, 0x90, 0xFF, 0x00, 0x14, 0x68, 0x21, 55, 0xCD, 0xDF, 0x00, 0xCF, 0x87};
    for (uint8_t b : prog) emu.port().out(0x6B, b);
}

// The CTC-paced DAC player at 28 MHz (see build_ctcdac in audio_debug_test.cpp); returns the OUT's address.
static uint16_t ctc_dac_player(Emulator& emu, bool skip_wrap) {
    nr_write(emu, 0x07, 0x03);
    for (int i = 0; i <= 256; ++i) emu.mmu().write(static_cast<uint16_t>(0xD000 + i), 0xD1);
    emu.mmu().write(0xD1D1, 0xC3); emu.mmu().write(0xD1D2, 0x00); emu.mmu().write(0xD1D3, 0x81);
    std::vector<uint8_t> isr = {0xF5, 0xC5, 0x3A, 0x00, 0x82, 0x3C, 0x32, 0x00, 0x82};
    if (skip_wrap) { isr.push_back(0x28); isr.push_back(0x02); }
    const uint16_t out_at = static_cast<uint16_t>(0x8100 + isr.size());
    for (uint8_t b : {0xD3, 0xDF, 0xC1, 0xF1, 0xFB, 0xED, 0x4D}) isr.push_back(b);
    for (size_t i = 0; i < isr.size(); ++i) emu.mmu().write(static_cast<uint16_t>(0x8100 + i), isr[i]);
    nr_write(emu, 0x22, 0x04);
    nr_write(emu, 0xC0, 0x01);
    nr_write(emu, 0xC5, 0x01);
    nr_write(emu, 0x08, 0x08);
    ctc_wr(emu, 0, 0x87, 109);
    cpu_program(emu, {0x3E, 0xD0, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD});
    return out_at;
}

static void show_dac_tab(AudioPanel& panel) {
    if (auto* tabs = panel.findChild<QTabWidget*>("audio_tabs")) tabs->setCurrentIndex(1);
}

static QTreeWidget* tree_of(AudioPanel& panel, const char* name) { return panel.findChild<QTreeWidget*>(name); }

static void test_stream_view(Emulator& emu) {
    set_group("DAP-STREAM");
    using jnext::dbg::ClientInfo;
    using jnext::dbg::ClientKind;

    {
        if (!build_dac_machine(emu)) { check("DAP-76", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        QWidget* dac = tabs ? tabs->widget(1) : nullptr;
        auto* pacing = dac ? dac->findChild<QTreeWidget*>("audio_pacing") : nullptr;
        auto* stream = dac ? dac->findChild<QTreeWidget*>("audio_stream") : nullptr;
        const bool shape = pacing && stream && pacing->topLevelItemCount() == 5 && pacing->columnCount() == 8 &&
                           pacing->headerItem()->text(0) == "Engine" && pacing->headerItem()->text(7) == "Rate" &&
                           pacing->topLevelItem(0)->text(0) == "CTC 0" && pacing->topLevelItem(3)->text(0) == "CTC 3" &&
                           pacing->topLevelItem(4)->text(0) == "DMA" && stream->columnCount() == 10 &&
                           stream->headerItem()->text(0) == "Writer" && stream->headerItem()->text(9) == "Pacing" &&
                           dac->findChild<QLabel*>("audio_stream_status") && dac->findChild<QLabel*>("audio_stream_window");
        const auto tables = panel.findChildren<QTableWidget*>();
        bool only_dac = tabs != nullptr && tabs->count() == 5;
        for (int i = 0; tabs && i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            only_dac = only_dac && (panel.dac_on_show() == (i == 1));
        }
        // The pacing values: CTC 0 a timer with its interrupt on, CTC 1 a counter behind it, CTC 2 never
        // programmed, the DMA a burst at prescaler 55 that has moved one byte.
        ctc_wr(emu, 0, 0x85, 109);
        ctc_wr(emu, 1, 0x45, 2);
        emu.ctc().tick(1);
        dma_to_dac(emu);
        emu.dma().execute_burst(1);
        show_dac_tab(panel);
        panel.refresh();
        const bool ch0 = tree_cell(pacing, 0, 1) == "timer" && tree_cell(pacing, 0, 2) == "16" &&
                         tree_cell(pacing, 0, 3) == "109" && tree_cell(pacing, 0, 5) == "running" &&
                         tree_cell(pacing, 0, 6) == "on" && tree_cell(pacing, 0, 7) == "16055.046 Hz";
        const bool ch1 = tree_cell(pacing, 1, 1) == "counter <- CTC 0" && tree_cell(pacing, 1, 2) == "-" &&
                         tree_cell(pacing, 1, 3) == "2" && tree_cell(pacing, 1, 7) == "8027.523 Hz";
        const bool ch2 = tree_cell(pacing, 2, 5) == "stopped" && tree_cell(pacing, 2, 7) == "-";
        const bool dma = tree_cell(pacing, 4, 1) == "burst" && tree_cell(pacing, 4, 2) == "55" &&
                         tree_cell(pacing, 4, 3) == "1/255" && tree_cell(pacing, 4, 4) == "0x9001 -> port 0x00DF" &&
                         tree_cell(pacing, 4, 5) == "active" && tree_cell(pacing, 4, 7) == "15909.091 Hz";
        check("DAP-76", "DAC & Beeper holds audio_pacing (5 rows CTC 0..3 and DMA, 8 columns), audio_stream, audio_stream_status and audio_stream_window; the hex table is still the only QTableWidget; dac_on_show() is true only with DAC & Beeper current; with ch0 a timer (P16, TC 109, interrupt on), ch1 a counter (TC 2), ch2 idle and a burst DMA at prescaler 55 the Pacing rows read timer/16/109/running/on/16055.046 Hz, counter <- CTC 0/-/2/8027.523 Hz, stopped/-, burst/55/1/255/0x9001 -> port 0x00DF/active/15909.091 Hz",
              shape && tables.size() == 1 && only_dac && ch0 && ch1 && ch2 && dma,
              fmt("shape=%d only_dac=%d ch0=%d ch1=%d ch2=%d dma=%d", shape, only_dac, ch0, ch1, ch2, dma));
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-77", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap77", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        const uint16_t out_at = ctc_dac_player(emu, false);
        AudioPanel panel(&dbg);
        show_dac_tab(panel);
        for (int i = 0; i < 10; ++i) emu.run_frame();
        panel.refresh();
        const auto entries = dbg.audio_log_entries(0).value;
        const auto st = jnext::dbg::dac_stream_stats(entries, dbg.time().master_cycle, jnext::dbg::dac_stream_window_cycles());
        auto* stream = tree_of(panel, "audio_stream");
        const bool one = stream && stream->topLevelItemCount() == 1 && st.writers.size() == 1;
        const QString want_rate = st.writers.empty() ? QString() : QString::number(st.writers[0].rate_millihz / 1000.0, 'f', 3);
        // "covered": min(window, end - the oldest writer's first sample) / window, from the same entries.
        const uint64_t win = jnext::dbg::dac_stream_window_cycles();
        const uint64_t end_c = dbg.time().master_cycle;
        const double covered = st.writers.empty() ? 0.0 : double(std::min(win, end_c - st.writers[0].first_cycle)) / double(win);
        auto* wlabel = panel.findChild<QLabel*>("audio_stream_window");
        const bool covered_ok = covered > 0.0 && wlabel && wlabel->text().contains(QString("covered %1 s").arg(covered, 0, 'f', 3));
        check("DAP-77", "the CTC-paced player (DSS-13) for 10 frames: one audio_stream row, Writer 'CPU 0x....' (the ISR's OUT), Channels 'A D', Rate the %.3f of dac_stream_stats over the same entries and end cycle, Gaps 0, Pacing beginning 'CTC 0: 16055.046 Hz (', and the window label's 'covered N s' equal to min(1 s, end - the first sample) / 1 s",
              one && tree_cell(stream, 0, 0) == QString("CPU 0x%1").arg(out_at, 4, 16, QChar('0')).toUpper().replace("0X", "0x") &&
              tree_cell(stream, 0, 1) == "A D" && tree_cell(stream, 0, 3) == want_rate &&
              tree_cell(stream, 0, 8) == "0" && tree_cell(stream, 0, 9).startsWith("CTC 0: 16055.046 Hz (") && covered_ok,
              fmt("row=%s rate=%s pacing=%s", tree_cell(stream, 0, 0).toStdString().c_str(),
                  tree_cell(stream, 0, 3).toStdString().c_str(), tree_cell(stream, 0, 9).toStdString().c_str()));
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-78", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap78", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        nr_write(emu, 0x08, 0x08);
        dma_to_dac(emu);
        // Copper: MOVE NR 0x2D,0x66 then HALT, run once (mode 01).
        emu.nextreg().write(0x61, 0); emu.nextreg().write(0x62, 0);
        for (uint16_t w : {uint16_t((0x2D << 8) | 0x66), uint16_t(0x8000u | 0x1FFu)}) {
            emu.nextreg().write(0x63, uint8_t(w >> 8)); emu.nextreg().write(0x63, uint8_t(w & 0xFF));
        }
        emu.nextreg().write(0x61, 0); emu.nextreg().write(0x62, 0x40);
        cpu_program(emu, {0xF3, 0x18, 0xFE});
        AudioPanel panel(&dbg);
        show_dac_tab(panel);
        for (int i = 0; i < 10; ++i) emu.run_frame();
        panel.refresh();
        auto* stream = tree_of(panel, "audio_stream");
        int dma_row = -1, cop_row = -1;
        for (int i = 0; stream && i < stream->topLevelItemCount(); ++i) {
            if (tree_cell(stream, i, 0) == "DMA") dma_row = i;
            if (tree_cell(stream, i, 0) == "Copper") cop_row = i;
        }
        int clicks = 0;
        QObject::connect(&panel, &AudioPanel::show_in_disassembly_requested, [&](uint16_t) { ++clicks; });
        if (stream && dma_row >= 0) emit stream->itemDoubleClicked(stream->topLevelItem(dma_row), 0);
        if (stream && cop_row >= 0) emit stream->itemDoubleClicked(stream->topLevelItem(cop_row), 0);
        const bool silent = clicks == 0;
        // A CPU row emits its PC once: the DAP-77 player.
        if (!build_dac_machine(emu)) { check("DAP-78", "emulator init", false); return; }
        jnext::dbg::Debugger dbg2(emu);
        const auto cid2 = dbg2.attach(ClientInfo{"dap78b", ClientKind::Test}).value;
        dbg2.set_audio_log_enabled(cid2, true);
        const uint16_t out_at = ctc_dac_player(emu, false);
        AudioPanel panel2(&dbg2);
        show_dac_tab(panel2);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        panel2.refresh();
        uint32_t seen_pc = 0; int fired = 0;
        QObject::connect(&panel2, &AudioPanel::show_in_disassembly_requested, [&](uint16_t pc) { seen_pc = pc; ++fired; });
        auto* stream2 = tree_of(panel2, "audio_stream");
        if (stream2 && stream2->topLevelItemCount() > 0) emit stream2->itemDoubleClicked(stream2->topLevelItem(0), 0);
        check("DAP-78", "a DMA burst to 0x00DF and a Copper MOVE 0x2D: a 'DMA' row whose Pacing begins 'DMA: 15909.0' (the DMA's nominal rate is a little above the measured one) and a 'Copper' row whose Pacing is '-'; double-clicking either emits nothing, and double-clicking the CPU row of the CTC-paced player emits show_in_disassembly_requested(the OUT's PC) once",
              dma_row >= 0 && cop_row >= 0 && tree_cell(stream, dma_row, 9).startsWith("DMA: 15909.091 Hz (") &&
              tree_cell(stream, cop_row, 9) == "-" && silent && fired == 1 && seen_pc == out_at,
              fmt("dma_row=%d cop_row=%d dma_pacing=%s fired=%d", dma_row, cop_row,
                  tree_cell(stream, dma_row, 9).toStdString().c_str(), fired));
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-79", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap79", ClientKind::Test}).value;
        nr_write(emu, 0x08, 0x08);
        cpu_program(emu, {0xF3, 0x18, 0xFE});
        // A DAC write between two instructions of the parked CPU: each one at its own cycle (writes at one
        // cycle from one source and port are ONE sample).
        auto dac_writes = [&](int n) { for (int i = 0; i < n; ++i) { emu.port().out(0xDF, uint8_t(i)); emu.execute_single_instruction(); } };
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        auto* stream = tree_of(panel, "audio_stream");
        auto* status = panel.findChild<QLabel*>("audio_stream_status");
        // Chips current: writes and refreshes leave the stream as it was (no fetch while not on show).
        dac_writes(20);
        panel.refresh();
        const bool idle_off_show = stream && stream->topLevelItemCount() == 0;
        show_dac_tab(panel);
        panel.refresh();
        const bool filled = stream && stream->topLevelItemCount() == 1 && tree_cell(stream, 0, 2) == "20" &&
                            status && status->text() == "Recording";
        // The request dropped: "Not recording" and an empty tree.
        dbg.set_audio_log_enabled(cid, false);
        panel.refresh();
        const bool dropped = status && status->text() == "Not recording" && stream && stream->topLevelItemCount() == 0;
        // Recording again, then a soft reset mid-stream: the next refresh's Samples restart.
        dbg.set_audio_log_enabled(cid, true);
        dac_writes(20);
        panel.refresh();
        const uint32_t before = stream && stream->topLevelItemCount() ? tree_cell(stream, 0, 2).toUInt() : 0;
        dbg.reset(cid, jnext::dbg::ResetKind::Soft);
        nr_write(emu, 0x08, 0x08);
        cpu_program(emu, {0xF3, 0x18, 0xFE});
        dac_writes(5);
        panel.refresh();
        const uint32_t after = stream && stream->topLevelItemCount() ? tree_cell(stream, 0, 2).toUInt() : 0;
        const bool marker = before == 20 && after == 5;
        // A ring that overflows between two refreshes: a stale cache would show the idle stretch before the
        // new writes as a gap; the view starts over instead and shows none.
        dbg.audio_log_resize(4);
        panel.refresh();
        for (int i = 0; i < 30; ++i) emu.execute_single_instruction();
        dac_writes(40);
        panel.refresh();
        const bool overflow = stream && stream->topLevelItemCount() == 1 && tree_cell(stream, 0, 8) == "0" &&
                              tree_cell(stream, 0, 2).toUInt() <= 4;
        check("DAP-79", "the view follows its data rules: with Chips current nothing is fetched (20 writes leave the stream empty); DAC & Beeper current shows one writer with 20 samples and 'Recording'; with the request dropped it reads 'Not recording' and the tree is empty; after a soft reset mid-stream the samples restart (20 -> 5); after the ring (resized to 4) overflowed between two refreshes no gap is shown",
              idle_off_show && filled && dropped && marker && overflow,
              fmt("idle=%d filled=%d dropped=%d marker=%d(%u->%u) overflow=%d", idle_off_show, filled, dropped, marker, before, after, overflow));
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-80", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap80", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        ctc_dac_player(emu, true);
        AudioPanel panel(&dbg);
        show_dac_tab(panel);
        for (int i = 0; i < 10; ++i) emu.run_frame();
        panel.refresh();
        const auto entries = dbg.audio_log_entries(0).value;
        const auto st = jnext::dbg::dac_stream_stats(entries, dbg.time().master_cycle, jnext::dbg::dac_stream_window_cycles());
        auto* stream = tree_of(panel, "audio_stream");
        auto* window = panel.findChild<QLabel*>("audio_stream_window");
        const bool ok = stream && stream->topLevelItemCount() == 1 && !st.writers.empty() && st.writers[0].gaps >= 1 &&
                        tree_cell(stream, 0, 8) == QString::number(st.writers[0].gaps);
        check("DAP-80", "the CTC-paced player with the OUT skipped when its tick wraps (DSS-14): the CPU row's Gaps cell equals dac_stream_stats(...).writers[0].gaps and is at least 1; audio_stream_window names the 1.5 x factor",
              ok && window && window->text().contains("1.5 x the median"),
              fmt("gaps_cell=%s", tree_cell(stream, 0, 8).toStdString().c_str()));
    }
}

// ── DAP-TRK: the Tracker sub-tab (GH #534) ─────────────────────────────
//
// Oracles: the grouping rules of AFG-* (audio_debug_test), the meaningful bits of the AY registers
// (ym2149.vhd: R1/R3/R5 3:0 at :306, R6 4:0 at :283, R7 bits 3-5 noise enable, active low, at :469-471,
// R8-R10 bit 4 envelope / 3:0 level at :483-487, R12:R11 at :334, every R13 write restarts at :207-210),
// the note names from the equal-tempered table at the PSG clock (f = 1750000 / (16 * TP): TP 252 = 434.0 Hz
// = A4, TP 428 = 255.5 Hz = C4, TP 239 = 457.7 Hz = A#4, TP 5 = 21875 Hz, above MIDI 127), and the
// window's own greying rule for the rewind (RZX and an empty ring refuse, then the range).

static QTableView* trk_table(AudioPanel& panel) { return panel.findChild<QTableView*>("audio_tracker_table"); }

static int trk_rows(AudioPanel& panel) {
    QTableView* t = trk_table(panel);
    return t ? t->model()->rowCount() : -1;
}

static QString trk_cell(AudioPanel& panel, int row, int col) {
    QTableView* t = trk_table(panel);
    return t && row < t->model()->rowCount() ? t->model()->index(row, col).data().toString()
                                             : QStringLiteral("<none>");
}

static std::string trk_dump(AudioPanel& panel) {
    std::string d;
    for (int r = 0; r < trk_rows(panel); ++r)
        d += "[" + trk_cell(panel, r, 0).toStdString() + "|" + trk_cell(panel, r, 1).toStdString() + "]";
    return d;
}

static void show_tracker_tab(AudioPanel& panel) {
    if (auto* tabs = panel.findChild<QTabWidget*>("audio_tabs")) tabs->setCurrentIndex(3);
}

// A machine with the rewind ring on (50 frames), the mutes and drop counters at their defaults.
static bool build_rewind_machine(Emulator& rw) {
    EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
    if (!rw.init(cfg)) return false;
    rw.set_audio_mute_mask(AudioMute::NONE);
    rw.clear_audio_drops();
    rw.set_rewind_enabled(true);
    rw.resize_rewind_buffer(50);
    return true;
}

static jnext::dbg::AudioLogEntry tw(uint64_t& seq, uint32_t frame, unsigned chip, unsigned reg, unsigned value, unsigned prev) {
    jnext::dbg::AudioLogEntry e;
    e.seq = ++seq; e.frame = frame; e.chip = uint8_t(chip); e.reg = uint8_t(reg);
    e.value = uint8_t(value); e.prev = uint8_t(prev); e.pc = 0x8000;
    return e;
}

static jnext::dbg::AudioLogEntry tmark(uint64_t& seq, uint32_t frame, jnext::dbg::AudioMarker m) {
    jnext::dbg::AudioLogEntry e;
    e.seq = ++seq; e.frame = frame; e.marker = m;
    return e;
}

static void test_tracker_tab(Emulator& emu) {
    set_group("DAP-TRK");
    using jnext::dbg::ClientInfo;
    using jnext::dbg::ClientKind;
    using jnext::dbg::AudioLogEntry;

    {
        // The stream, then the grid cell for cell. Derivations:
        //  row 0 (f100): R0 = 0xFC, R1 = 0 give TP 252 = A4 (A-4); chip 1 channel B TP 0x1AC = 428 = C4; R9 = 0x1F
        //    has bit 4 = envelope (E); R7 = 0x30: noise A on (bit 3 clear) with R6 = 0x0A, B and C off (bits 4, 5
        //    set); A's N is highlighted (enable changed from 0xFF, and R6 changed), B/C are not (enable unchanged).
        //  row 1: f101 holds only a DAC entry (not a write) and f102 nothing: the empty run is ONE row, 2 frames.
        //  row 2 (f103): R0 rewritten with its own value is no note; R8 written twice (0x0F -> 0x0E -> 0x0D) is
        //    `double` with before 0x0F, after 0x0D; R12:R11 = 0x0064; R13 = 0x0E from 0 is a change.
        //  row 3 (f104): R13 rewritten with the same shape is shown (a restart) but not highlighted, and so is R9 written
        //    with its own value 0 (B V `0`, not highlighted); R7 0x30 -> 0x38 turns noise A off (`--`, highlighted).
        //  row 4: the Reset marker. Row 5 (f106): R1 is unknown after the marker: TP is `???`.
        uint64_t seq = 0;
        std::vector<AudioLogEntry> in;
        in.push_back(tw(seq, 100, 0, 0, 0xFC, 0x00)); in.push_back(tw(seq, 100, 0, 1, 0x00, 0x00));
        in.push_back(tw(seq, 100, 0, 7, 0x30, 0xFF)); in.push_back(tw(seq, 100, 0, 6, 0x0A, 0x00));
        in.push_back(tw(seq, 100, 0, 8, 0x0F, 0x00)); in.push_back(tw(seq, 100, 1, 2, 0xAC, 0x00));
        in.push_back(tw(seq, 100, 1, 3, 0x01, 0x00)); in.push_back(tw(seq, 100, 1, 9, 0x1F, 0x00));
        { AudioLogEntry d; d.seq = ++seq; d.frame = 101; d.target = jnext::dbg::AudioTarget::Dac; d.value = 0x80; in.push_back(d); }
        in.push_back(tw(seq, 103, 0, 8, 0x0E, 0x0F)); in.push_back(tw(seq, 103, 0, 8, 0x0D, 0x0E));
        in.push_back(tw(seq, 103, 0, 0, 0xFC, 0xFC)); in.push_back(tw(seq, 103, 0, 11, 0x64, 0x00));
        in.push_back(tw(seq, 103, 0, 12, 0x00, 0x00)); in.push_back(tw(seq, 103, 0, 13, 0x0E, 0x00));
        in.push_back(tw(seq, 104, 0, 13, 0x0E, 0x0E)); in.push_back(tw(seq, 104, 0, 9, 0x00, 0x00));
        in.push_back(tw(seq, 104, 0, 7, 0x38, 0x30));
        in.push_back(tmark(seq, 104, jnext::dbg::AudioMarker::Reset));
        in.push_back(tw(seq, 106, 0, 0, 0x05, 0x00)); in.push_back(tw(seq, 106, 0, 8, 0x0C, 0x00));

        using Chip = std::vector<std::string>;
        const Chip U = {"....", ".", "---", ".", "..", "---", ".", "..", "---", ".", ".."};
        auto row = [&](std::string frame, std::string mark, Chip c0, Chip c1, Chip c2) {
            std::vector<std::string> r = {frame, mark};
            for (const Chip* c : {&c0, &c1, &c2}) r.insert(r.end(), c->begin(), c->end());
            return r;
        };
        const std::vector<std::vector<std::string>> want = {
            row("100", "", {"....", ".", "A-4", "F", "0A", "---", ".", "--", "---", ".", "--"},
                           {"....", ".", "---", ".", "..", "C-4", "E", "..", "---", ".", ".."}, U),
            row("101-102", "empty x2", U, U, U),
            row("103", "double", {"0064", "E", "---", "D", "..", "---", ".", "..", "---", ".", ".."}, U, U),
            row("104", "", {"....", "E", "---", ".", "--", "---", "0", "--", "---", ".", "--"}, U, U),
            std::vector<std::string>{"", "reset"} /* + 33 empty cells below */,
            row("106", "", {"....", ".", "???", "C", "..", "---", ".", "..", "---", ".", ".."}, U, U),
        };
        const std::set<std::pair<int, int>> hot = {{0, 4}, {0, 5}, {0, 6}, {0, 18}, {0, 19}, {2, 2}, {2, 3}, {2, 5},
                                                   {3, 6}, {5, 4}, {5, 5}};
        AudioTrackerModel model;
        model.set_clock(1750000);
        model.set_entries(in);
        bool grid = model.rowCount() == 6 && model.columnCount() == 35;
        bool marks = grid;
        std::string first_bad;
        for (int r = 0; grid && r < 6; ++r) {
            for (int c = 0; c < 35; ++c) {
                const std::string got = model.index(r, c).data().toString().toStdString();
                const std::string exp = c < int(want[size_t(r)].size()) ? want[size_t(r)][size_t(c)] : std::string();
                const bool is_hot = hot.count({r, c}) != 0;
                const bool changed = model.index(r, c).data(AudioTrackerModel::ChangedRole).toBool();
                const bool brush = model.index(r, c).data(Qt::BackgroundRole).isValid();
                if (got != exp && first_bad.empty()) first_bad = "cell " + std::to_string(r) + "," + std::to_string(c) + " got '" + got + "' want '" + exp + "'";
                grid = grid && got == exp;
                if (changed != is_hot || brush != is_hot) {
                    marks = false;
                    if (first_bad.empty()) first_bad = "highlight at " + std::to_string(r) + "," + std::to_string(c);
                }
            }
        }
        check("DAP-61", "AudioTrackerModel::set_entries over the 6-row stream (frame, empty run of 2, double frame, frame, reset marker, open frame after the marker): all 6 x 35 cells equal the hand-written grid, and ChangedRole and BackgroundRole are set on exactly the 11 changed cells",
              grid && marks, first_bad);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-62", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        QWidget* tab = tabs ? tabs->widget(3) : nullptr;
        QTableView* t = trk_table(panel);
        const auto tables = panel.findChildren<QTableWidget*>();
        std::vector<QString> want = {"Frame", "Mark"};
        for (int c = 0; c < 3; ++c) {
            want.push_back(QString("%1 Env").arg(c));
            want.push_back(QString("%1 Sh").arg(c));
            for (const char* ch : {"A", "B", "C"})
                for (const char* k : {"Note", "V", "N"}) want.push_back(QString("%1%2 %3").arg(c).arg(ch).arg(k));
        }
        bool headers = t && t->model()->columnCount() == int(want.size());
        for (int i = 0; headers && i < int(want.size()); ++i)
            headers = t->model()->headerData(i, Qt::Horizontal).toString() == want[size_t(i)];
        bool only_tracker = tabs != nullptr;
        for (int i = 0; tabs && i < tabs->count(); ++i) {
            tabs->setCurrentIndex(i);
            only_tracker = only_tracker && (panel.tracker_on_show() == (i == 3));
        }
        bool no_placeholder = tabs != nullptr;
        if (tabs) for (QLabel* l : tabs->widget(4)->findChildren<QLabel*>()) no_placeholder = no_placeholder && l->text() != "Not available yet.";
        QLabel* st = panel.findChild<QLabel*>("audio_tracker_status");
        check("DAP-62", "tab 3 holds audio_tracker_table, a QTableView with the 35 headers in order (Frame, Mark, then per chip Env, Sh and A, B, C of Note, V, N); the hex table is still the panel's only QTableWidget; tracker_on_show() is true only with Tracker current; Scopes is no placeholder; the status label exists",
              tab && t && tab->findChild<QTableView*>("audio_tracker_table") && headers && tables.size() == 1 &&
              tables[0]->objectName() != "audio_tracker_table" && only_tracker && no_placeholder && st && !st->text().isEmpty());
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-63", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap63", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_tracker_tab(panel);
        cpu_program(emu, {0x18, 0xFE});                 // JR $
        emu.run_frame();                                // a frame boundary: the writes below are stamped by it
        const uint32_t f1 = dbg.time().frame;
        ay_wr(emu, 0, 0xFC); ay_wr(emu, 1, 0x00); ay_wr(emu, 8, 0x0F);
        emu.run_frame();
        const uint32_t f2 = dbg.time().frame;
        emu.run_frame();
        const uint32_t f3 = dbg.time().frame;
        ay_wr(emu, 8, 0x0A);
        panel.refresh();
        const bool rows = trk_rows(panel) == 3;
        const bool frames = rows && f2 == f1 + 1 && f3 == f2 + 1 &&
                            trk_cell(panel, 0, 0) == QString::number(f1) && trk_cell(panel, 1, 0) == QString::number(f2) &&
                            trk_cell(panel, 1, 1) == "empty" && trk_cell(panel, 2, 0) == QString::number(f3);
        const bool cells = rows && trk_cell(panel, 0, 4) == "A-4" && trk_cell(panel, 0, 5) == "F" && trk_cell(panel, 2, 5) == "A";
        dbg.reset(cid, jnext::dbg::ResetKind::Soft);
        panel.refresh();
        const int n = trk_rows(panel);
        const bool marker = n > 0 && trk_cell(panel, n - 1, 1) == "reset" && trk_cell(panel, n - 1, 0).isEmpty();
        check("DAP-63", "a CPU parked on JR $ and three frames of real AY writes (frame 1: R0=0xFC R1=0 R8=0x0F; frame 2 none; frame 3: R8=0x0A): the Tracker shows [frame row, 'empty' row, frame row] whose Frame cells are the debugger's own consecutive time().frame values; frame 1's 0A Note is A-4 and V F, frame 3's V is A; a soft reset then adds a 'reset' marker row last",
              rows && frames && cells && marker,
              "rows=" + trk_dump(panel) + " f1=" + std::to_string(f1) + " f2=" + std::to_string(f2) + " f3=" + std::to_string(f3));
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-64", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap64", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_tracker_tab(panel);
        cpu_program(emu, {0x18, 0xFE});
        emu.run_frame();                                // a frame boundary: the writes below are stamped by it
        ay_wr(emu, 8, 0x01);
        panel.refresh();
        const int after1 = trk_rows(panel);
        ay_wr(emu, 9, 0x02);                            // the same frame
        panel.refresh();
        const int after2 = trk_rows(panel);
        const bool replaced = trk_cell(panel, 0, 8) == "2" && trk_cell(panel, 0, 5) == "1";
        std::vector<QString> row0;
        for (int c = 0; c < 35; ++c) row0.push_back(trk_cell(panel, 0, c));
        emu.run_frame();
        ay_wr(emu, 8, 0x03);
        panel.refresh();
        bool unchanged = true;
        for (int c = 0; c < 35; ++c) unchanged = unchanged && trk_cell(panel, 0, c) == row0[size_t(c)];
        check("DAP-64", "writes in the frame being shown replace its row instead of adding one: R8 then R9 in one frame is 1 row, then 1 row with 0B V = 2; a write in the next frame makes 2 rows and row 0 is unchanged",
              after1 == 1 && after2 == 1 && replaced && trk_rows(panel) == 2 && unchanged && trk_cell(panel, 1, 5) == "3",
              "after1=" + std::to_string(after1) + " after2=" + std::to_string(after2) + " replaced=" + std::to_string(replaced) +
              " unchanged=" + std::to_string(unchanged) + " rows=" + trk_dump(panel));
    }
    {
        Emulator rw;
        if (!build_rewind_machine(rw)) { check("DAP-65", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(rw);
        const auto cid = dbg.attach(ClientInfo{"dap65", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_tracker_tab(panel);
        cpu_program(rw, {0x18, 0xFE});
        rw.run_frame();
        for (int f = 0; f < 8; ++f) { ay_wr(rw, 8, uint8_t(f + 1)); rw.run_frame(); }
        rw.debug_state().pause();
        std::vector<uint32_t> asked;
        QObject::connect(&panel, &AudioPanel::rewind_to_frame_requested, [&asked](uint32_t f) { asked.push_back(f); });
        panel.refresh();
        QTableView* t = trk_table(panel);
        const jnext::dbg::RewindRange range = dbg.rewind_range();
        const int inside = 3;
        const QModelIndex ix = t->model()->index(inside, 0);
        const uint32_t fr = trk_cell(panel, inside, 0).toUInt();
        const bool in_range = trk_rows(panel) == 8 && fr >= range.oldest_frame && fr <= range.newest_frame;
        const bool rewindable = ix.data(AudioTrackerModel::RewindableRole).toBool() &&
                                ix.data(Qt::ToolTipRole).toString() == QString("Double-click to rewind to the start of frame %1").arg(fr) &&
                                ix.data(AudioTrackerModel::RewindFrameRole).toUInt() == fr;
        emit t->doubleClicked(ix);
        const bool once = asked.size() == 1 && asked[0] == fr;
        // An empty run emits its FIRST frame: two frames without writes, then a write.
        rw.debug_state().resume();
        rw.run_frame(); rw.run_frame(); rw.run_frame(); ay_wr(rw, 8, 0x0E);
        rw.debug_state().pause();
        panel.refresh();
        int empty_row = -1;
        for (int r = 0; r < trk_rows(panel); ++r) if (trk_cell(panel, r, 1).startsWith("empty")) empty_row = r;
        bool empty_ok = empty_row >= 0;
        if (empty_ok) {
            const QModelIndex ei = t->model()->index(empty_row, 0);
            const uint32_t first = trk_cell(panel, empty_row, 0).section('-', 0, 0).toUInt();
            asked.clear();
            emit t->doubleClicked(ei);
            empty_ok = ei.data(AudioTrackerModel::RewindableRole).toBool() == (first >= dbg.rewind_range().oldest_frame) &&
                       (!ei.data(AudioTrackerModel::RewindableRole).toBool() || (asked.size() == 1 && asked[0] == first));
        }
        // A row older than the ring: a 3-frame ring that 4 more frames of writes have rolled past row 0
        // (a resize empties the ring, so the frames are run after it); greyed, reason, nothing emitted.
        rw.resize_rewind_buffer(3);
        rw.debug_state().resume();
        for (int f = 0; f < 4; ++f) { ay_wr(rw, 8, uint8_t(f + 0x20)); rw.run_frame(); }
        rw.debug_state().pause();
        panel.refresh();
        const QModelIndex old = t->model()->index(0, 0);
        const jnext::dbg::RewindRange small = dbg.rewind_range();
        const uint32_t ofr = trk_cell(panel, 0, 0).toUInt();
        asked.clear();
        emit t->doubleClicked(old);
        const QBrush fg = old.data(Qt::ForegroundRole).value<QBrush>();
        const bool outside = ofr < small.oldest_frame && !old.data(AudioTrackerModel::RewindableRole).toBool() &&
                             fg.color() == QGuiApplication::palette().color(QPalette::Disabled, QPalette::WindowText) &&
                             old.data(Qt::ToolTipRole).toString() ==
                                 QString("Frame %1 is outside the rewind buffer (frames %2 to %3)").arg(ofr).arg(small.oldest_frame).arg(small.newest_frame) &&
                             asked.empty();
        // A marker row emits nothing.
        dbg.reset(cid, jnext::dbg::ResetKind::Soft);
        panel.refresh();
        const int last = trk_rows(panel) - 1;
        asked.clear();
        emit t->doubleClicked(t->model()->index(last, 0));
        const bool marker = trk_cell(panel, last, 1) == "reset" && asked.empty() &&
                            !t->model()->index(last, 0).data(AudioTrackerModel::RewindableRole).toBool();
        check("DAP-65", "paused with a rewind ring: a row inside [oldest_frame, newest_frame] is rewindable with the 'Double-click to rewind' tooltip and a double-click emits rewind_to_frame_requested(its frame) once; an empty-run row emits its first frame (when that is in the ring); a row older than the ring (a 3-frame ring rolled past it) is greyed with the 'outside the rewind buffer' tooltip and emits nothing; a marker row emits nothing",
              in_range && rewindable && once && empty_ok && outside && marker,
              "in_range=" + std::to_string(in_range) + " rewindable=" + std::to_string(rewindable) + " once=" + std::to_string(once) +
              " empty=" + std::to_string(empty_ok) + " outside=" + std::to_string(outside) + " marker=" + std::to_string(marker) +
              " fr=" + std::to_string(fr) + " range=" + std::to_string(range.oldest_frame) + "-" + std::to_string(range.newest_frame) +
              " small=" + std::to_string(small.oldest_frame) + "-" + std::to_string(small.newest_frame) + " ofr=" + std::to_string(ofr) +
              " rows=" + trk_dump(panel));
    }
    {
        Emulator rw;
        if (!build_rewind_machine(rw)) { check("DAP-66", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(rw);
        const auto cid = dbg.attach(ClientInfo{"dap66", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_tracker_tab(panel);
        cpu_program(rw, {0x18, 0xFE});
        rw.run_frame();
        for (int f = 0; f < 6; ++f) { ay_wr(rw, 8, uint8_t(f + 1)); rw.run_frame(); }
        QTableView* t = trk_table(panel);
        std::vector<uint32_t> asked;
        QObject::connect(&panel, &AudioPanel::rewind_to_frame_requested, [&asked](uint32_t f) { asked.push_back(f); });
        auto tip = [&](int row) { return t->model()->index(row, 0).data(Qt::ToolTipRole).toString(); };
        auto can = [&](int row) { return t->model()->index(row, 0).data(AudioTrackerModel::RewindableRole).toBool(); };
        panel.refresh();                                                       // running
        bool running = trk_rows(panel) == 6;
        for (int r = 0; r < trk_rows(panel); ++r)
            running = running && !can(r) && tip(r) == "Pause the machine to rewind to a frame";
        asked.clear();
        emit t->doubleClicked(t->model()->index(2, 0));
        running = running && asked.empty();
        // A change of the rewind snapshot repaints the rows (dataChanged over them, with the greying roles);
        // a refresh that changes nothing emits nothing.
        int changed_signals = 0;
        bool covers = true, has_fg = true;
        auto conn = QObject::connect(t->model(), &QAbstractItemModel::dataChanged,
                                     [&](const QModelIndex& a, const QModelIndex& b, const QList<int>& roles) {
                                         ++changed_signals;
                                         covers = covers && a.row() == 0 && b.row() == t->model()->rowCount() - 1;
                                         has_fg = has_fg && roles.contains(Qt::ForegroundRole);
                                     });
        rw.debug_state().pause();
        panel.refresh();
        const bool emitted = changed_signals == 1 && covers && has_fg;
        changed_signals = 0;
        panel.refresh();
        const bool quiet = changed_signals == 0;
        QObject::disconnect(conn);
        rw.rzx_player().start(RzxRecording{});
        panel.refresh();
        const bool rzx = !can(2) && tip(2) == "Rewind is not available while an RZX is recording or playing";
        rw.rzx_player().stop();
        panel.refresh();
        const bool back = can(2);                                              // the control: rewindable again
        rw.resize_rewind_buffer(0);
        panel.refresh();
        const bool empty = !can(2) && tip(2) == "The rewind buffer is empty";
        rw.resize_rewind_buffer(50);
        rw.debug_state().resume();
        dbg.reset(cid, jnext::dbg::ResetKind::Soft);
        for (int f = 0; f < 3; ++f) { ay_wr(rw, 8, uint8_t(f + 10)); rw.run_frame(); }
        rw.debug_state().pause();
        panel.refresh();
        int reset_row = -1;
        for (int r = 0; r < trk_rows(panel); ++r) if (trk_cell(panel, r, 1) == "reset") reset_row = r;
        bool stale = reset_row > 0 && reset_row + 1 < trk_rows(panel);
        for (int r = 0; stale && r < reset_row; ++r)
            stale = !can(r) && tip(r) == "This frame is from before a reset or a state load";
        const jnext::dbg::RewindRange rr = dbg.rewind_range();
        bool after_ok = stale;
        for (int r = reset_row + 1; after_ok && r < trk_rows(panel); ++r) {
            const uint32_t f = trk_cell(panel, r, 0).toUInt();
            after_ok = can(r) == (f >= rr.oldest_frame && f <= rr.newest_frame);
        }
        bool some_after = false;
        for (int r = reset_row + 1; r < trk_rows(panel) && reset_row > 0; ++r) some_after = some_after || can(r);
        check("DAP-66", "why a row is not rewindable, in order: running -> 'Pause the machine to rewind to a frame' (and a double-click emits nothing); paused with an RZX playing -> the RZX text, and rewindable again once it stops; ring freed -> 'The rewind buffer is empty'; rows before a reset marker -> 'This frame is from before a reset or a state load' while rows after it follow the ring range (at least one rewindable); pausing repaints the rows (one dataChanged over all of them with the ForegroundRole) and a refresh that changes nothing emits none",
              running && emitted && quiet && rzx && back && empty && stale && after_ok && some_after,
              "running=" + std::to_string(running) + " (" + trk_dump(panel) + ") rzx=" + std::to_string(rzx) + " back=" + std::to_string(back) + " empty=" + std::to_string(empty) +
              " stale=" + std::to_string(stale) + " after_ok=" + std::to_string(after_ok) + " some_after=" + std::to_string(some_after));
    }
    {
        // kMaxRows + 10 frames of one write each: the model keeps the newest kMaxRows rows.
        uint64_t seq = 0;
        std::vector<AudioLogEntry> in;
        const int n = AudioTrackerModel::kMaxRows + 10;
        for (int f = 0; f < n; ++f) in.push_back(tw(seq, 1000 + uint32_t(f), 0, 8, uint32_t(f & 0x0F), 0));
        AudioTrackerModel model;
        model.set_entries(in);
        const bool reset_cap = model.rowCount() == AudioTrackerModel::kMaxRows &&
                               model.index(0, 0).data().toString() == QString::number(1010) &&
                               model.index(model.rowCount() - 1, 0).data().toString() == QString::number(1000 + n - 1);
        // The same through append_entries (the 4 Hz path): the oldest go, the newest stay.
        AudioTrackerModel m2;
        m2.append_entries(in);
        const bool append_cap = m2.rowCount() == AudioTrackerModel::kMaxRows &&
                                m2.index(0, 0).data().toString() == QString::number(1010) &&
                                m2.index(m2.rowCount() - 1, 0).data().toString() == QString::number(1000 + n - 1);
        check("DAP-67", "one counted write in each of kMaxRows + 10 consecutive frames: the model holds exactly kMaxRows rows, row 0 is the 11th frame and the last row the newest, by set_entries and by append_entries",
              reset_cap && append_cap);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-68", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap68", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);                          // Chips is current
        cpu_program(emu, {0x18, 0xFE});
        emu.run_frame();                                // a frame boundary: the writes below are stamped by it
        ay_wr(emu, 8, 0x01);
        emu.run_frame();
        ay_wr(emu, 8, 0x02);
        panel.refresh();
        const int while_chips = trk_rows(panel);
        show_tracker_tab(panel);
        panel.refresh();
        const bool filled = trk_rows(panel) == 2;
        dbg.set_audio_log_enabled(cid, false);           // the log goes off: the rows go with it
        panel.refresh();
        QLabel* st = panel.findChild<QLabel*>("audio_tracker_status");
        const bool off = trk_rows(panel) == 0 && st && st->text() == "Not recording";
        check("DAP-68", "writes over 2 frames with the Chips tab current: refresh() leaves the Tracker model empty (the grouping did not run); with Tracker current the same refresh() fills it; with the log then switched off a refresh() empties it again and the status reads 'Not recording'",
              while_chips == 0 && filled && off,
              "while_chips=" + std::to_string(while_chips) + " rows=" + trk_dump(panel));
    }
    {
        // Notes and unknowns: one frame, chip 0.
        uint64_t seq = 0;
        std::vector<AudioLogEntry> in = {
            tw(seq, 50, 0, 0, 0x05, 0x00), tw(seq, 50, 0, 1, 0x00, 0x00),     // TP 5: 21875 Hz, above the named range
            tw(seq, 50, 0, 2, 0xEF, 0x00), tw(seq, 50, 0, 3, 0x00, 0x00),     // TP 239: A#4
            tw(seq, 50, 0, 11, 0x20, 0x00),                                    // R12 never written
            tw(seq, 50, 0, 7, 0x37, 0xFF),                                     // noise A on, B and C off; R6 never written
            tw(seq, 50, 0, 10, 0x1F, 0x00),
        };
        AudioTrackerModel model;
        model.set_clock(1750000);
        model.set_entries(in);
        auto cell = [&](int c) { return model.rowCount() == 1 ? model.index(0, c).data().toString() : QString("<none>"); };
        check("DAP-69", "one frame: TP 5 (21875 Hz) is '^^^' (above MIDI 127), TP 239 is 'A#4', R11 written with R12 never seen shows four question marks, noise A on with R6 unknown shows two, noise B off is '--', R10 = 0x1F is 'E' and its noise (off) '--'",
              cell(4) == "^^^" && cell(7) == "A#4" && cell(2) == "\?\?\?\?" && cell(6) == "\?\?" && cell(9) == "--" &&
              cell(11) == "E" && cell(12) == "--",
              cell(4).toStdString() + "," + cell(7).toStdString() + "," + cell(2).toStdString() + "," + cell(6).toStdString() + "," + cell(9).toStdString() + "," + cell(11).toStdString() + "," + cell(12).toStdString());
    }
}

// ── GH #535: the player timing strip ────────────────────────────────────

static jnext::dbg::AudioLogEntry tt(uint64_t& seq, uint32_t frame, unsigned reg, int vc, uint32_t tstate, uint16_t pc,
                                    jnext::dbg::EventSource src = jnext::dbg::EventSource::Cpu) {
    jnext::dbg::AudioLogEntry e = tw(seq, frame, 0, reg, 1, 0);
    e.vc = int16_t(vc); e.hc = 0; e.tstate_in_frame = tstate; e.pc = pc; e.source = src;
    return e;
}

// Stream T: f200 R8 (1,300,0x9300) R9 (1,340,0x9310); f201 R8 (1,302) R9 (2,460) R10 (3,700,0x9320); f202 R8
// (15,3600) R9 (15,3640); f203 nothing; f204 R8 (1,304) by DMA; f205 R8 (1,300) left open.
static std::vector<jnext::dbg::AudioLogEntry> stream_t() {
    uint64_t seq = 0;
    return {tt(seq, 200, 8, 1, 300, 0x9300), tt(seq, 200, 9, 1, 340, 0x9310),
            tt(seq, 201, 8, 1, 302, 0x9300), tt(seq, 201, 9, 2, 460, 0x9310), tt(seq, 201, 10, 3, 700, 0x9320),
            tt(seq, 202, 8, 15, 3600, 0x9300), tt(seq, 202, 9, 15, 3640, 0x9310),
            tt(seq, 204, 8, 1, 304, 0x0000, jnext::dbg::EventSource::Dma),
            tt(seq, 205, 8, 1, 300, 0x9300)};
}

static QString tlabel(AudioPanel& panel, const char* name) {
    auto* l = panel.findChild<QLabel*>(name);
    return l ? l->text() : QStringLiteral("<none>");
}

static bool same_range(const jnext::dbg::AudioTimingRange& a, const jnext::dbg::AudioTimingRange& b) {
    return a.min == b.min && a.max == b.max && a.avg == b.avg;
}

static bool same_stats(const jnext::dbg::AudioTimingStats& a, const jnext::dbg::AudioTimingStats& b) {
    return a.window_first == b.window_first && a.window_last == b.window_last && a.frames == b.frames &&
           a.empty == b.empty && same_range(a.first_line, b.first_line) && same_range(a.first_tstate, b.first_tstate) &&
           same_range(a.last_line, b.last_line) && same_range(a.last_tstate, b.last_tstate) &&
           same_range(a.writes, b.writes) && a.pc_min == b.pc_min && a.pc_max == b.pc_max;
}

// A Next with an IM2 player whose interrupt routine writes AY R8 and R9 once a frame (opcodes: UM0080).
//   main 0x8000: DI; LD A,0x90; LD I,A; IM 2; EI; loop: HALT; JR loop       (a 257-byte table of 0x92 at 0x9000)
//   ISR 0x9292: PUSH AF; PUSH BC; LD A,(0x9400); INC A; LD (0x9400),A
//               CP 4; JR Z,exit        the 4th interrupt writes nothing: a missed frame
//               CP 8; JR NZ,go         the 8th interrupt is late
//               LD B,255; dly: DJNZ dly
//         go:   LD BC,0xFFFD; LD A,8; OUT (C),A; LD B,0xBF; OUT (C),A      data write 1 (R8) at 0x92B0
//               LD B,0xFF; LD A,9; OUT (C),A; LD B,0xBF; OUT (C),A         data write 2 (R9) at 0x92BA
//         exit: POP BC; POP AF; EI; RETI
// The late path against the steady one: JR NZ not taken 7 instead of taken 12, LD B,255 7, DJNZ 254 x 13 + 8 =
// 3310: 7 - 12 + 7 + 3310 = 3312 T. The steady prefix up to the first data OUT is PUSH 11 + 11, LD A,(nn) 13,
// INC 4, LD (nn),A 13, CP 7, JR Z not taken 7, CP 7, JR NZ taken 12, LD BC 10, LD A,n 7, OUT 12, LD B,n 7 = 121 T.
static bool build_player_machine(Emulator& emu) {
    EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
    if (!emu.init(cfg)) return false;
    emu.set_audio_mute_mask(AudioMute::NONE);
    emu.clear_audio_drops();
    for (int i = 0; i < 257; ++i) emu.mmu().write(uint16_t(0x9000 + i), 0x92);
    auto poke = [&](uint16_t a, std::initializer_list<uint8_t> b) { for (uint8_t v : b) emu.mmu().write(a++, v); };
    poke(0x9292, {0xF5, 0xC5,                                  // PUSH AF; PUSH BC
                  0x3A, 0x00, 0x94, 0x3C, 0x32, 0x00, 0x94,    // LD A,(9400); INC A; LD (9400),A
                  0xFE, 0x04, 0x28, 0x1D,                      // CP 4; JR Z,exit (0x92BC)
                  0xFE, 0x08, 0x20, 0x04,                      // CP 8; JR NZ,go (0x92A7)
                  0x06, 0xFF, 0x10, 0xFE,                      // LD B,255; DJNZ $
                  0x01, 0xFD, 0xFF, 0x3E, 0x08, 0xED, 0x79,    // go: LD BC,FFFD; LD A,8; OUT (C),A
                  0x06, 0xBF, 0xED, 0x79,                      // LD B,BF; OUT (C),A    (0x92B0)
                  0x06, 0xFF, 0x3E, 0x09, 0xED, 0x79,          // LD B,FF; LD A,9; OUT (C),A
                  0x06, 0xBF, 0xED, 0x79,                      // LD B,BF; OUT (C),A    (0x92BA)
                  0xC1, 0xF1, 0xFB, 0xED, 0x4D});              // exit: POP BC; POP AF; EI; RETI
    poke(0x9400, {0x00});
    poke(0x8000, {0xF3, 0x3E, 0x90, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD});
    Z80Registers r = emu.cpu().get_registers();
    r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
    emu.cpu().set_registers(r);
    return true;
}

static void test_timing_strip(Emulator& emu) {
    set_group("DAP-TIM");
    using jnext::dbg::ClientInfo;
    using jnext::dbg::ClientKind;
    using jnext::dbg::AudioLogEntry;

    {
        if (!build_dac_machine(emu)) { check("DAP-81", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        QWidget* tab = tabs ? tabs->widget(3) : nullptr;
        auto in_tab = [&](auto* type_tag, const char* name) {
            using T = std::remove_pointer_t<decltype(type_tag)>;
            return tab && tab->findChild<T*>(name) != nullptr;
        };
        const bool objs = in_tab((QSpinBox*)nullptr, "audio_timing_window") && in_tab((QLabel*)nullptr, "audio_timing_span") &&
                          in_tab((QLabel*)nullptr, "audio_timing_first") && in_tab((QLabel*)nullptr, "audio_timing_last") &&
                          in_tab((QLabel*)nullptr, "audio_timing_writes") && in_tab((QLabel*)nullptr, "audio_timing_frame") &&
                          in_tab((AudioJitterPlot*)nullptr, "audio_timing_plot") &&
                          in_tab((QLabel*)nullptr, "audio_timing_plot_range") && in_tab((QLabel*)nullptr, "audio_timing_note");
        auto* spin = panel.findChild<QSpinBox*>("audio_timing_window");
        const bool spin_ok = spin && spin->minimum() == 1 && spin->maximum() == 1000 && spin->value() == 50 &&
                             spin->suffix() == " frames";
        const bool note = tlabel(panel, "audio_timing_note") == "Exact routine cost: the CPU panel's T-states since the last stop, or --profile.";
        bool no_na = tab != nullptr, no_prefix = tab != nullptr;
        if (tab) for (QLabel* l : tab->findChildren<QLabel*>()) {
            no_na = no_na && l->text() != "Not available yet.";
            for (const char* pre : {"TurboSound:", "Mode:", "Stereo:", "PSG mode:"})
                no_prefix = no_prefix && !l->text().startsWith(pre);
        }
        QTableView* t = trk_table(panel);
        check("DAP-81", "tab 3 holds the timing strip objects audio_timing_window (a QSpinBox, 1-1000, value 50, suffix ' frames'), the labels span, first, last, writes, frame, plot_range and note, and the audio_timing_plot AudioJitterPlot; the note text is exact; the panel still holds one QTableWidget (the hex table), the tracker table still has 35 columns, no label of tab 3 reads 'Not available yet.' or starts with a label prefix the other rows search for",
              objs && spin_ok && note && no_na && no_prefix && panel.findChildren<QTableWidget*>().size() == 1 &&
              t && t->model()->columnCount() == 35);
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-82", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        show_tracker_tab(panel);
        auto* view = panel.findChild<AudioTrackerView*>();
        view->model()->set_entries(stream_t());
        view->update_timing();
        const auto want = jnext::dbg::audio_timing_stats(view->model()->timing_frames(50), 50);
        check("DAP-82", "Stream T (frames 200, 201, 202, 204 completed, 205 open; 203 empty), window 50: span 'Last 50 frames: 200 to 204, 4 with AY writes, 1 without'; first 'line 1 / 4.5 / 15, T-state 300 / 1126.5 / 3600'; last 'line 1 / 5.0 / 15, T-state 304 / 1246.0 / 3640'; writes '1 / 2.0 / 3 ...; PC 0x9300-0x9320'; and the strip's statistics equal audio_timing_stats() over the model's own frames, field by field",
              tlabel(panel, "audio_timing_span") == "Last 50 frames: 200 to 204, 4 with AY writes, 1 without" &&
              tlabel(panel, "audio_timing_first") == "First write: line 1 / 4.5 / 15, T-state 300 / 1126.5 / 3600 (min / avg / max)" &&
              tlabel(panel, "audio_timing_last") == "Last write: line 1 / 5.0 / 15, T-state 304 / 1246.0 / 3640 (min / avg / max)" &&
              tlabel(panel, "audio_timing_writes") == "Writes per frame: 1 / 2.0 / 3 (min / avg / max); PC 0x9300-0x9320" &&
              view->timing_stats().frames == 4 && same_stats(view->timing_stats(), want),
              tlabel(panel, "audio_timing_span").toStdString() + " | " + tlabel(panel, "audio_timing_writes").toStdString());
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-83", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        show_tracker_tab(panel);
        auto* view = panel.findChild<AudioTrackerView*>();
        view->model()->set_entries(stream_t());
        view->update_timing();
        const QString none = tlabel(panel, "audio_timing_frame");
        QTableView* t = trk_table(panel);
        auto pick = [&](int row) { t->setCurrentIndex(t->model()->index(row, 0)); return tlabel(panel, "audio_timing_frame"); };
        const QString f202 = pick(2), empty = pick(3), f204 = pick(4);
        uint64_t seq = 0;
        view->model()->set_entries({tt(seq, 10, 8, 1, 5, 0x8000), tmark(seq, 10, jnext::dbg::AudioMarker::Reset), tt(seq, 11, 8, 1, 5, 0x8000)});
        const QString marker = pick(1);
        seq = 0;
        view->model()->set_entries({tt(seq, 10, 8, 1, 5, 0x8000), tt(seq, 13, 8, 1, 5, 0x8000), tt(seq, 14, 8, 1, 5, 0x8000)});
        const QString run2 = pick(1);                    // the empty run of f11 and f12
        check("DAP-83", "Stream T: with no current row the details are the last row's (the open frame 205); on f202's row 'Frame 202: first write line 15, T-state 3600; last write line 15, T-state 3640; writes 2; PC 0x9300-0x9310'; on the empty run 'Frame 203: no AY write'; on f204 (a DMA write) 'PC none (no CPU write)'; on a reset marker row the text is empty; on an empty run of two frames (writes in f10 and f13) 'Frames 11-12: no AY write'",
              none == "Frame 205: first write line 1, T-state 300; last write line 1, T-state 300; writes 1; PC 0x9300" &&
              f202 == "Frame 202: first write line 15, T-state 3600; last write line 15, T-state 3640; writes 2; PC 0x9300-0x9310" &&
              empty == "Frame 203: no AY write" &&
              f204 == "Frame 204: first write line 1, T-state 304; last write line 1, T-state 304; writes 1; PC none (no CPU write)" &&
              marker.isEmpty() && run2 == "Frames 11-12: no AY write",
              none.toStdString() + " | " + f202.toStdString() + " | " + empty.toStdString() + " | " + f204.toStdString() + " | " + marker.toStdString() + " | " + run2.toStdString());
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-84", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        show_tracker_tab(panel);
        auto* view = panel.findChild<AudioTrackerView*>();
        auto* spin = panel.findChild<QSpinBox*>("audio_timing_window");
        auto* plot = panel.findChild<AudioJitterPlot*>("audio_timing_plot");
        view->model()->set_entries(stream_t());
        spin->setValue(6);
        plot->setFixedSize(240, 60);       // fixed: a grab() lays the parent out again
        const auto dots = plot->dots();
        const QImage img = plot->grab().toImage();
        const QColor hi = plot->palette().color(QPalette::Highlight);
        struct W { uint32_t frame; int line, x, y; };
        const W want[] = {{200, 1, 60, 2}, {201, 1, 100, 2}, {202, 15, 140, 58}, {204, 1, 220, 2}};
        bool exact = dots.size() == 4;
        bool painted = img.width() == 240 && img.height() == 60;
        for (size_t i = 0; exact && i < 4; ++i) {
            exact = dots[i].frame == want[i].frame && dots[i].line == want[i].line &&
                    dots[i].at == QPoint(want[i].x, want[i].y);
            painted = painted && img.pixelColor(want[i].x, want[i].y) == hi;
        }
        bool gap = img.width() == 240;
        for (int y = 0; gap && y < 60; ++y) gap = img.pixelColor(180, y) != hi;
        const bool range6 = plot->range_lo() == 1 && plot->range_hi() == 15 &&
                            tlabel(panel, "audio_timing_plot_range") == "Dots: first write per frame, line 1 at the top to 15 at the bottom";
        spin->setValue(2);
        const auto d2 = plot->dots();
        const bool two = d2.size() == 1 && d2[0].frame == 204 && d2[0].at.y() == 3 && plot->range_lo() == 1 &&
                         plot->range_hi() == 9 &&
                         tlabel(panel, "audio_timing_plot_range") == "Dots: first write per frame, line 1 at the top to 9 at the bottom";
        check("DAP-84", "Stream T, window 6, plot 240 x 60: the dots are exactly f200 line 1 at (60,2), f201 line 1 at (100,2), f202 line 15 at (140,58), f204 line 1 at (220,2) (none for the empty 203 or the open 205); range 1 to 15 and its label; the rendered image has the Highlight colour at each dot centre and no Highlight pixel in column 180 (frame 203); window 2: one dot (f204) at y 3 with the 9-line minimum span, label 'line 1 at the top to 9 at the bottom'",
              exact && painted && gap && range6 && two,
              "dots=" + std::to_string(dots.size()) + " exact=" + std::to_string(exact) + " painted=" + std::to_string(painted) +
              " gap=" + std::to_string(gap) + " img=" + std::to_string(img.width()) + "x" + std::to_string(img.height()) +
              " plot=" + std::to_string(plot->width()) + "x" + std::to_string(plot->height()) + " range6=" + std::to_string(range6) + " two=" + std::to_string(two));
    }
    {
        Emulator pm;
        if (!build_player_machine(pm)) { check("DAP-85", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(pm);
        const auto cid = dbg.attach(ClientInfo{"dap85", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_tracker_tab(panel);
        const bool timing = dbg.machine().lines == 311 && dbg.machine().tstates_per_line == 228;   // zxula_timing.vhd:196,204
        for (int i = 0; i < 40 && pm.mmu().read(0x9400) < 12; ++i) pm.run_frame();
        pm.run_frame();                                  // the 12th interrupt's frame is now completed
        panel.refresh();

        // The log's own stamps, grouped by hand: counted AY entries by frame, the newest (open) frame dropped.
        struct HF { uint32_t frame; int vc1; uint32_t t1; int vc2; uint32_t t2; unsigned n; uint16_t pcmin, pcmax; };
        std::vector<HF> hand;
        const auto log = dbg.audio_log_entries(0);
        if (log) for (const AudioLogEntry& e : log.value) {
            if (e.marker != jnext::dbg::AudioMarker::None || e.target != jnext::dbg::AudioTarget::AyRegister ||
                (e.flags & jnext::dbg::AudioFlag::Dropped) || e.chip >= 3 || e.reg >= 16) continue;
            if (hand.empty() || hand.back().frame != e.frame)
                hand.push_back({e.frame, e.vc, e.tstate_in_frame, e.vc, e.tstate_in_frame, 0, 0xFFFF, 0});
            HF& h = hand.back();
            h.vc2 = e.vc; h.t2 = e.tstate_in_frame; ++h.n;
            h.pcmin = std::min(h.pcmin, e.pc); h.pcmax = std::max(h.pcmax, e.pc);
        }
        if (!hand.empty()) hand.pop_back();              // the open frame
        const size_t nf = hand.size();
        bool steady_line = nf >= 10;
        uint32_t smin = UINT32_MAX, smax = 0, late_t = 0, late_frame = 0;
        if (nf >= 10) {
            // The frame of the 8th interrupt is the 8th counted one after the missed 4th: ISR k is the k-th
            // frame number of the run; the late one has the largest first T-state.
            for (const HF& h : hand) if (h.t1 > late_t) { late_t = h.t1; late_frame = h.frame; }
            for (const HF& h : hand) {
                if (h.frame == late_frame) continue;
                steady_line = steady_line && h.vc1 == 1;
                smin = std::min(smin, h.t1); smax = std::max(smax, h.t1);
            }
        }
        const bool steady_spread = steady_line && smax - smin <= 3;
        const bool late_ok = nf >= 10 && late_t >= smin + 3309 && late_t <= smin + 3315;
        const uint32_t span_first = nf ? hand.front().frame : 0, span_last = nf ? hand.back().frame : 0;
        const uint32_t empties = nf ? (span_last - span_first + 1) - uint32_t(nf) : 99;
        uint32_t gap_frame = 0;
        for (size_t i = 1; i < nf; ++i) if (hand[i].frame != hand[i - 1].frame + 1) gap_frame = hand[i - 1].frame + 1;
        const bool one_gap = nf == 11 && empties == 1 && gap_frame > span_first && gap_frame < span_last;

        // The strip against numbers computed from those stamps.
        auto triple = [&](auto get) {
            double sum = 0; uint32_t lo = UINT32_MAX, hi = 0;
            for (const HF& h : hand) { const uint32_t v = get(h); sum += v; lo = std::min(lo, v); hi = std::max(hi, v); }
            return QString("%1 / %2 / %3").arg(lo).arg(QString::number(nf ? sum / double(nf) : 0, 'f', 1)).arg(hi);
        };
        const QString first = "First write: line " + triple([](const HF& h) { return uint32_t(h.vc1); }) + ", T-state " +
                              triple([](const HF& h) { return h.t1; }) + " (min / avg / max)";
        const QString last = "Last write: line " + triple([](const HF& h) { return uint32_t(h.vc2); }) + ", T-state " +
                             triple([](const HF& h) { return h.t2; }) + " (min / avg / max)";
        // Two data OUTs per interrupt: 0x92B0 and 0x92BA (the layout in the comment above).
        const QString writes = "Writes per frame: " + triple([](const HF& h) { return h.n; }) + " (min / avg / max); PC 0x92B0-0x92BA";
        const QString span = QString("Last 50 frames: %1 to %2, %3 with AY writes, %4 without").arg(span_first).arg(span_last).arg(nf).arg(empties);
        const bool labels = tlabel(panel, "audio_timing_first") == first && tlabel(panel, "audio_timing_last") == last &&
                            tlabel(panel, "audio_timing_writes") == writes && tlabel(panel, "audio_timing_span") == span;

        // The plot: the late frame's dot is the lowest, every steady dot is level, none at the skipped frame.
        auto* plot = panel.findChild<AudioJitterPlot*>("audio_timing_plot");
        plot->setFixedSize(400, 64);
        const auto dots = plot->dots();
        bool level = dots.size() == nf && nf >= 10, late_lowest = false, no_gap_dot = true;
        int steady_y = -1;
        for (const auto& d : dots) {
            if (d.frame == gap_frame) no_gap_dot = false;
            if (d.frame == late_frame) continue;
            if (steady_y < 0) steady_y = d.at.y();
            level = level && d.at.y() == steady_y;
        }
        for (const auto& d : dots) if (d.frame == late_frame) late_lowest = d.at.y() > steady_y;
        check("DAP-85", "a real IM2 player (interrupt routine writes R8 and R9 once a frame; the 4th interrupt writes nothing, the 8th is delayed by a 3312 T loop): the machine runs 311 lines of 228 T (zxula_timing.vhd:196,204); the log's stamps grouped by hand give 11 frames with writes and exactly one empty frame number inside the span; every steady frame's first write is on line 1 (c_int_v, zxula_timing.vhd:199) with first T-states within 3 T of each other; the late frame's first T-state is 3309 to 3315 above the steady minimum; the span, first, last and writes labels equal the texts built from those hand numbers (PC 0x92B0-0x92BA); the late dot is strictly the lowest, the steady dots level, and the skipped frame has none",
              timing && bool(log) && steady_spread && late_ok && one_gap && labels && level && late_lowest && no_gap_dot,
              "timing=" + std::to_string(timing) + " nf=" + std::to_string(nf) + " steady_line=" + std::to_string(steady_line) +
              " spread=" + std::to_string(smax - smin) + " late_t=" + std::to_string(late_t) + " smin=" + std::to_string(smin) +
              " gaps=" + std::to_string(empties) + " labels=" + std::to_string(labels) + " span=" + tlabel(panel, "audio_timing_span").toStdString() +
              " first=" + tlabel(panel, "audio_timing_first").toStdString() + " dots=" + std::to_string(dots.size()) +
              " level=" + std::to_string(level) + " late_lowest=" + std::to_string(late_lowest));
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-86", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        show_tracker_tab(panel);
        auto* view = panel.findChild<AudioTrackerView*>();
        auto* spin = panel.findChild<QSpinBox*>("audio_timing_window");
        view->model()->set_entries(stream_t());
        view->update_timing();
        spin->setValue(2);                               // no refresh() and no update_timing() from here on
        const QString span2 = tlabel(panel, "audio_timing_span"), first2 = tlabel(panel, "audio_timing_first"),
                      writes2 = tlabel(panel, "audio_timing_writes");
        spin->setValue(3);                               // frames below the window; f202 sits exactly on its first number
        const QString span3 = tlabel(panel, "audio_timing_span"), first3 = tlabel(panel, "audio_timing_first"),
                      writes3 = tlabel(panel, "audio_timing_writes");
        spin->setValue(1);
        const QString span1 = tlabel(panel, "audio_timing_span");
        check("DAP-86", "Stream T: setting the window box to 2 updates the strip at once (span 'Last 2 frames: 203 to 204, 1 with AY writes, 1 without', first and writes of f204 alone, PC none); with window 3 (f200 and f201 below it, f202 exactly on its first number) 'Last 3 frames: 202 to 204, 2 with AY writes, 1 without' and the first and writes lines include f202; to 1 it reads 'Last 1 frame: 204 to 204, 1 with AY writes, 0 without'",
              span2 == "Last 2 frames: 203 to 204, 1 with AY writes, 1 without" &&
              first2 == "First write: line 1 / 1.0 / 1, T-state 304 / 304.0 / 304 (min / avg / max)" &&
              writes2 == "Writes per frame: 1 / 1.0 / 1 (min / avg / max); PC none (no CPU write)" &&
              span3 == "Last 3 frames: 202 to 204, 2 with AY writes, 1 without" &&
              first3 == "First write: line 1 / 8.0 / 15, T-state 304 / 1952.0 / 3600 (min / avg / max)" &&
              writes3 == "Writes per frame: 1 / 1.5 / 2 (min / avg / max); PC 0x9300-0x9310" &&
              span1 == "Last 1 frame: 204 to 204, 1 with AY writes, 0 without",
              span2.toStdString() + " | " + first2.toStdString() + " | " + writes2.toStdString() + " | " + span3.toStdString() + " | " + first3.toStdString() + " | " + writes3.toStdString() + " | " + span1.toStdString());
    }
    {
        if (!build_dac_machine(emu)) { check("DAP-87", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(ClientInfo{"dap87", ClientKind::Test}).value;
        dbg.set_audio_log_enabled(cid, true);
        AudioPanel panel(&dbg);
        show_tracker_tab(panel);
        auto* view = panel.findChild<AudioTrackerView*>();
        uint64_t seq = 0;
        using jnext::dbg::AudioMarker;
        view->model()->set_entries({tt(seq, 10, 8, 1, 5, 0x8000), tt(seq, 11, 8, 1, 5, 0x8000), tmark(seq, 11, AudioMarker::Reset),
                                    tt(seq, 30, 8, 1, 5, 0x8000), tt(seq, 31, 8, 1, 5, 0x8000), tt(seq, 32, 8, 1, 5, 0x8000)});
        view->update_timing();
        const QString a = tlabel(panel, "audio_timing_span");
        seq = 0;
        view->model()->set_entries({tt(seq, 20, 8, 1, 5, 0x8000), tt(seq, 21, 8, 1, 5, 0x8000), tmark(seq, 22, AudioMarker::Rewind),
                                    tt(seq, 22, 8, 1, 5, 0x8000), tt(seq, 23, 8, 1, 5, 0x8000)});
        view->update_timing();
        const QString b = tlabel(panel, "audio_timing_span");

        cpu_program(emu, {0x18, 0xFE});
        emu.run_frame();
        ay_wr(emu, 8, 0x01);
        emu.run_frame();
        ay_wr(emu, 8, 0x02);
        emu.run_frame();
        ay_wr(emu, 8, 0x03);
        panel.refresh();
        const bool live = view->timing_stats().frames == 2 && tlabel(panel, "audio_timing_frame").startsWith("Frame ") &&
                          !panel.findChild<AudioJitterPlot*>("audio_timing_plot")->dots().empty();
        dbg.set_audio_log_enabled(cid, false);
        view->refresh();
        const bool off = tlabel(panel, "audio_timing_span") == "No AY writes in the last 50 frames" &&
                         tlabel(panel, "audio_timing_first") == "First write: -" && tlabel(panel, "audio_timing_last") == "Last write: -" &&
                         tlabel(panel, "audio_timing_writes") == "Writes per frame: -" && tlabel(panel, "audio_timing_frame").isEmpty() &&
                         panel.findChild<AudioJitterPlot*>("audio_timing_plot")->dots().empty() &&
                         tlabel(panel, "audio_timing_plot_range").isEmpty();
        panel.findChild<QSpinBox*>("audio_timing_window")->setValue(1);
        const bool off1 = tlabel(panel, "audio_timing_span") == "No AY writes in the last 1 frame";
        check("DAP-87", "(a) f10, f11, a Reset marker, f30, f31, open f32: 'Last 50 frames: 30 to 31, 2 with AY writes, 0 without' (a reset starts afresh); (b) f20, f21, a Rewind marker, f22, open f23: 'Last 50 frames: 20 to 22, 3 with AY writes, 0 without' (a landing does not split); (c) a real log with frames of writes shows 2 frames, a details line and dots, and after the log goes off the strip reads 'No AY writes in the last 50 frames', dashes, no details, no dots, no range, and with the window set to 1 'No AY writes in the last 1 frame'",
              a == "Last 50 frames: 30 to 31, 2 with AY writes, 0 without" &&
              b == "Last 50 frames: 20 to 22, 3 with AY writes, 0 without" && live && off && off1,
              a.toStdString() + " | " + b.toStdString() + " live=" + std::to_string(live) + " off=" + std::to_string(off) + " off1=" + std::to_string(off1) +
              " span=" + tlabel(panel, "audio_timing_span").toStdString());
    }
}

// ── DAP-SCP: the Scopes sub-tab (GH #536) ───────────────────────────────

// DI; JR $ at 0x8000: frames run with the CPU parked and only the audio chain moving.
static void park_cpu(Emulator& emu) {
    const uint8_t prog[] = {0xF3, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(prog); ++i) emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
    Z80Registers r = emu.cpu().get_registers();
    r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

// TurboSound and the DAC on; AY1 a square on channel A (tone period 100, volume 12) with the tone and
// noise gates open on B and C; AY0 channel A a fixed volume 15 (R7 0x3F: the output IS the level,
// ym2149.vhd:467-520).
static void scope_machine(Emulator& emu) {
    nr_write(emu, 0x08, 0x0A);
    emu.port().out(0xFFFD, 0xFF);
    ay_reg(emu, 7, 0x3F); ay_reg(emu, 8, 0x0F);
    emu.port().out(0xFFFD, 0xFE);
    ay_reg(emu, 0, 100); ay_reg(emu, 1, 0); ay_reg(emu, 7, 0x3E); ay_reg(emu, 8, 0x0C);
    park_cpu(emu);
}

static void test_scopes(Emulator& emu) {
    set_group("DAP-SCP");
    static const char* const kTitle[16] = {"AY0 A", "AY0 B", "AY0 C", "AY1 A", "AY1 B", "AY1 C", "AY2 A", "AY2 B",
                                           "AY2 C", "DAC A", "DAC B", "DAC C", "DAC D", "Beeper", "Left", "Right"};
    {
        if (!build_next_emulator(emu)) { check("DAP-70", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        AudioScopeView* view = panel.findChild<AudioScopeView*>("audio_scope_view");
        bool ok = tabs && view && tabs->widget(4) == view && tabs->count() == 5;
        for (int i = 0; ok && i < 16; ++i) {
            QWidget* tr = panel.findChild<QWidget*>(QString("scope_trace_%1").arg(i));
            QLabel* title = panel.findChild<QLabel*>(QString("scope_title_%1").arg(i));
            ok = ok && tr && title && title->text() == kTitle[i] &&
                 tr->property("title").toString() == kTitle[i] &&
                 panel.findChild<QProgressBar*>(QString("scope_meter_%1").arg(i)) != nullptr;
        }
        const bool off_before = !panel.scopes_on_show();
        if (tabs) tabs->setCurrentIndex(4);
        const bool on = panel.scopes_on_show() && !panel.chips_on_show();
        if (tabs) tabs->setCurrentIndex(0);
        QStringList names;
        for (int i = 0; tabs && i < tabs->count(); ++i) names << tabs->tabText(i).replace("&&", "&");
        check("DAP-70", "Scopes is tab 4 and holds audio_scope_view with 16 titled scope_trace_<i> in AudioSource order and 16 scope_meter_<i> bars; scopes_on_show() is true only while it is current; the tab order is unchanged",
              ok && off_before && on && !panel.scopes_on_show() &&
              names == QStringList({"Chips", "DAC & Beeper", "Log", "Tracker", "Scopes"}));
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-71", "emulator init", false); return; }
        scope_machine(emu);
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(jnext::dbg::ClientInfo{"dap71", jnext::dbg::ClientKind::Test}).value;
        dbg.set_audio_scope_enabled(cid, true);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        AudioScopeView* view = panel.findChild<AudioScopeView*>("audio_scope_view");
        tabs->setCurrentIndex(4);
        constexpr int AY1A = 3;
        bool ok = view != nullptr;
        uint64_t first[2] = {0, 0};
        for (int round = 0; ok && round < 2; ++round) {
            for (int f = 0; f < 3; ++f) emu.run_frame();
            panel.refresh();
            const auto blk = dbg.audio_scope_samples(2 * AudioScopeView::WINDOW);
            const std::vector<int16_t>& v = blk.value.samples[AY1A];
            const ScopeTrigger t = scope_trigger(v.data(), v.size(), AudioScopeView::WINDOW);
            const auto& st = view->trace(AY1A);
            const auto& sh = view->shown(AY1A);
            ok = ok && blk.status == jnext::dbg::Result::Ok && st.triggered && t.triggered && st.start == t.start &&
                 st.start > 0 && v[st.start - 1] < t.level && v[st.start] >= t.level &&
                 sh.size() == AudioScopeView::WINDOW && sh[0] == v[st.start];
            for (size_t i = 0; ok && i < 15; ++i) ok = ok && sh[i] >= t.level;
            first[round] = st.first_sample;
        }
        check("DAP-71", "an AY1 A square (period 100): both refreshes are triggered on the latest rising edge (the sample before it is below the level, the first 15 shown are at or above it), and the second block is new data",
              ok && first[1] > first[0]);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-72", "emulator init", false); return; }
        scope_machine(emu);
        emu.port().out(0x1F, 0xC0);
        emu.port().out(0xFE, 0x10);
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(jnext::dbg::ClientInfo{"dap72", jnext::dbg::ClientKind::Test}).value;
        dbg.set_audio_scope_enabled(cid, true);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        AudioScopeView* view = panel.findChild<AudioScopeView*>("audio_scope_view");
        tabs->setCurrentIndex(4);
        for (int f = 0; f < 3; ++f) emu.run_frame();
        panel.refresh();
        auto* dac = panel.findChild<QProgressBar*>("scope_meter_9");
        auto* ay = panel.findChild<QProgressBar*>("scope_meter_0");
        auto* beeper = panel.findChild<QProgressBar*>("scope_meter_13");
        auto* dac_text = panel.findChild<QLabel*>("scope_meter_text_9");
        // DAC A 0xC0 x 4 = 768 against rest 512: peak 256 of 512 = 50, native 64. AY0 A 255 x 4 = 1020 of 1020 = 100.
        // Beeper EAR 512 x 4 = 2048 of (512 + 128) x 4 = 2560 (audio_mixer.vhd:80-81) = 80.
        check("DAP-72", "DAC A = 0xC0: meter 50, text 'pk 64'; AY0 A volume 15: meter 100; EAR high: Beeper meter 80",
              view && dac && ay && beeper && dac_text && dac->value() == 50 && ay->value() == 100 && beeper->value() == 80 &&
              dac_text->text().startsWith("pk 64 "),
              dac_text ? dac_text->text().toStdString() : std::string("<none>"));
    }
    {
        Emulator silent;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0; cfg.silent = true;
        silent.init(cfg);
        jnext::dbg::Debugger dbg(silent);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        tabs->setCurrentIndex(4);
        panel.refresh();
        auto* off = panel.findChild<QLabel*>("audio_scope_off");
        check("DAP-73", "under --silent the Scopes tab shows audio_scope_off with its text and the backend tap stays off",
              off && !off->isHidden() && off->text().contains("--silent") && !dbg.audio_scope_enabled());
    }
    {
        // The refresh on a tab change: the tap is already held by another client and the machine is paused, so no tick will draw
        // the view; switching to Scopes must draw it at once.
        if (!build_next_emulator(emu)) { check("DAP-75", "emulator init", false); return; }
        scope_machine(emu);
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(jnext::dbg::ClientInfo{"dap75", jnext::dbg::ClientKind::Test}).value;
        dbg.set_audio_scope_enabled(cid, true);
        for (int f = 0; f < 3; ++f) emu.run_frame();
        AudioPanel panel(&dbg);
        AudioScopeView* view = panel.findChild<AudioScopeView*>("audio_scope_view");
        const bool empty_before = view && view->shown(3).empty();
        panel.findChild<QTabWidget*>("audio_tabs")->setCurrentIndex(4);
        check("DAP-75", "switching to the Scopes sub-tab with the tap already held and the machine paused draws the view at once (no tick, no refresh call)",
              view && empty_before && view->shown(3).size() == AudioScopeView::WINDOW && view->trace(3).triggered);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-74", "emulator init", false); return; }
        scope_machine(emu);
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(jnext::dbg::ClientInfo{"dap74", jnext::dbg::ClientKind::Test}).value;
        dbg.set_audio_scope_enabled(cid, true);
        AudioPanel panel(&dbg);
        panel.findChild<QTabWidget*>("audio_tabs")->setCurrentIndex(4);
        for (int f = 0; f < 3; ++f) emu.run_frame();
        panel.refresh();
        auto* quiet = panel.findChild<QLabel*>("scope_trig_2");   // AY0 C: silent
        auto* square = panel.findChild<QLabel*>("scope_trig_3");  // AY1 A: the square
        check("DAP-74", "the lamp of a silent channel (AY0 C) reads 'free' and the square's (AY1 A) reads 'trig'",
              quiet && square && quiet->text() == "free" && square->text() == "trig");
    }
    // DAP-93..98 - the Scopes sub-tab's stems row (GH #539).
    {
        if (!build_next_emulator(emu)) { check("DAP-93", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* tabs = panel.findChild<QTabWidget*>("audio_tabs");
        auto* rec = panel.findChild<QPushButton*>("audio_stems_record");
        auto* stop = panel.findChild<QPushButton*>("audio_stems_stop");
        auto* status = panel.findChild<QLabel*>("audio_stems_status");
        QStringList names;
        for (int i = 0; tabs && i < tabs->count(); ++i) names << tabs->tabText(i).replace("&&", "&");
        int traces = 0;
        for (int i = 0; i < 16; ++i) traces += panel.findChild<QWidget*>(QString("scope_trace_%1").arg(i)) != nullptr;
        check("DAP-93", "the Scopes tab has audio_stems_record 'Record stems...', audio_stems_stop 'Stop' (disabled) and audio_stems_status 'Not recording stems'; the five tabs and the 16 traces are unchanged",
              rec && stop && status && rec->text() == "Record stems..." && stop->text() == "Stop" && !stop->isEnabled() &&
              rec->isEnabled() && status->text() == "Not recording stems" && traces == 16 &&
              names == QStringList({"Chips", "DAC & Beeper", "Log", "Tracker", "Scopes"}));
    }
    auto stems_dir = [](const char* tag) {
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        return (std::filesystem::temp_directory_path() / (std::string("jnext-dap-") + tag + "-" + std::to_string(stamp))).string();
    };
    static const char* const kStemFiles[7] = {"ay0.wav", "ay1.wav", "ay2.wav", "dac.wav", "beeper.wav", "pi.wav", "mix.wav"};
    auto all_files = [&](const std::string& dir) {
        bool ok = true;
        for (const char* n : kStemFiles) ok = ok && std::filesystem::exists(std::filesystem::path(dir) / n);
        return ok;
    };
    {
        if (!build_next_emulator(emu)) { check("DAP-94", "emulator init", false); return; }
        scope_machine(emu);
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        panel.findChild<QTabWidget*>("audio_tabs")->setCurrentIndex(4);
        AudioScopeView* view = panel.findChild<AudioScopeView*>("audio_scope_view");
        auto* rec = panel.findChild<QPushButton*>("audio_stems_record");
        auto* stop = panel.findChild<QPushButton*>("audio_stems_stop");
        auto* status = panel.findChild<QLabel*>("audio_stems_status");
        const std::string dir = stems_dir("94");
        const bool started = view && view->start_stems_recording(QString::fromStdString(dir));
        const bool during = status && status->text() == "Recording stems" && stop && stop->isEnabled() && rec && !rec->isEnabled() &&
                            dbg.stems_recording() && status->toolTip() == QString::fromStdString(dir);
        for (int f = 0; f < 3; ++f) emu.run_frame();
        const bool stopped = view && view->stop_stems_recording();
        const bool after = status->text() == "Not recording stems" && !stop->isEnabled() && rec->isEnabled() && !dbg.stems_recording() && all_files(dir);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        check("DAP-94", "start_stems_recording(dir), 3 frames, stop_stems_recording(): true; 'Recording stems' with Stop enabled and Record disabled while it runs; afterwards 'Not recording stems' and the seven files exist",
              started && during && stopped && after);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-95", "emulator init", false); return; }
        scope_machine(emu);
        jnext::dbg::Debugger dbg(emu);
        const auto cid = dbg.attach(jnext::dbg::ClientInfo{"dap95", jnext::dbg::ClientKind::Test}).value;
        AudioPanel panel(&dbg);
        panel.findChild<QTabWidget*>("audio_tabs")->setCurrentIndex(4);
        auto* stop = panel.findChild<QPushButton*>("audio_stems_stop");
        auto* status = panel.findChild<QLabel*>("audio_stems_status");
        const std::string dir = stems_dir("95");
        const bool outside = dbg.stems_record_start(cid, dir) == jnext::dbg::Result::Ok;
        panel.refresh();
        const bool shows = status && status->text() == "Recording stems" && stop && stop->isEnabled();
        if (stop) stop->click();
        const bool stopped = !dbg.stems_recording() && status->text() == "Not recording stems";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        check("DAP-95", "a recording started outside the panel shows 'Recording stems' with Stop enabled at the next refresh, and Stop stops it (last verb wins)",
              outside && shows && stopped);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-96", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        panel.findChild<QTabWidget*>("audio_tabs")->setCurrentIndex(4);
        AudioScopeView* view = panel.findChild<AudioScopeView*>("audio_scope_view");
        auto* status = panel.findChild<QLabel*>("audio_stems_status");
        const std::string base = stems_dir("96");
        std::filesystem::create_directories(base);
        const std::string file = (std::filesystem::path(base) / "plain").string();
        if (std::FILE* f = std::fopen(file.c_str(), "wb")) std::fclose(f);
        const QString bad = QString::fromStdString((std::filesystem::path(file) / "sub").string());
        const bool refused = view && !view->start_stems_recording(bad);
        const bool label = status && status->text().startsWith("Cannot write") && status->text().contains(bad);
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
        check("DAP-96", "start_stems_recording on an unwritable path: false, not recording, the label starts 'Cannot write' and contains the path",
              refused && !dbg.stems_recording() && label);
    }
    {
        Emulator silent;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0; cfg.silent = true;
        silent.init(cfg);
        jnext::dbg::Debugger dbg(silent);
        AudioPanel panel(&dbg);
        panel.findChild<QTabWidget*>("audio_tabs")->setCurrentIndex(4);
        panel.refresh();
        auto* row = panel.findChild<QWidget*>("audio_stems_row");
        auto* off = panel.findChild<QLabel*>("audio_scope_off");
        auto* rec = panel.findChild<QPushButton*>("audio_stems_record");
        check("DAP-97", "under --silent the stems row is hidden (its Record button with it) and audio_scope_off is shown",
              row && row->isHidden() && rec && !rec->isVisible() && off && !off->isHidden());
    }
    {
        // The same 3 frames recorded through the panel seam and through the backend verb: the seven files are byte-identical.
        auto record = [&](bool through_panel, const std::string& dir) {
            Emulator e;
            if (!build_next_emulator(e)) return false;
            scope_machine(e);
            jnext::dbg::Debugger dbg(e);
            AudioPanel panel(&dbg);
            panel.findChild<QTabWidget*>("audio_tabs")->setCurrentIndex(4);
            AudioScopeView* view = panel.findChild<AudioScopeView*>("audio_scope_view");
            const bool on = through_panel ? (view && view->start_stems_recording(QString::fromStdString(dir)))
                                          : dbg.stems_record_start(jnext::dbg::CLIENT_NONE, dir) == jnext::dbg::Result::Ok;
            for (int f = 0; f < 3; ++f) e.run_frame();
            const bool off = through_panel ? (view && view->stop_stems_recording())
                                           : dbg.stems_record_stop(jnext::dbg::CLIENT_NONE) == jnext::dbg::Result::Ok;
            return on && off;
        };
        const std::string a = stems_dir("98a"), b = stems_dir("98b");
        const bool ran = record(true, a) && record(false, b);
        auto slurp = [](const std::filesystem::path& p) {
            std::string s;
            if (std::FILE* f = std::fopen(p.string().c_str(), "rb")) {
                char buf[4096];
                size_t n;
                while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
                std::fclose(f);
            }
            return s;
        };
        bool same = ran;
        for (const char* n : kStemFiles) {
            const std::string x = slurp(std::filesystem::path(a) / n), y = slurp(std::filesystem::path(b) / n);
            same = same && x.size() > 44 + 4 * 1000 && x == y;
        }
        std::error_code ec;
        std::filesystem::remove_all(a, ec);
        std::filesystem::remove_all(b, ec);
        check("DAP-98", "the same 3 frames recorded through the panel and through the backend verb give seven byte-identical files (the GUI and the CLI share one path)", same);
    }
}

// ── DAP-88..92: the .psg recording row of the Chips tab (GH #538) ───────────────────────────────
static std::string psg_tmp_path(const char* tag) {
    return (std::filesystem::temp_directory_path() /
            (std::string("jnext-dap-psg-") + tag + "-" + std::to_string(::getpid()) + ".psg")).string();
}

static std::string psg_slurp(const std::string& path) {
    std::string out;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) out.append(b, n);
        std::fclose(f);
    }
    return out;
}

static void psg_cleanup(const std::string& p) {
    std::remove(p.c_str());
    for (const char* sfx : {"-ay1", "-ay2"}) {
        std::string q = p;
        q.insert(q.size() - 4, sfx);   // before ".psg"
        std::remove(q.c_str());
    }
}

static void test_psg_row(Emulator& emu) {
    set_group("DAP-PSG");
    const std::string hdr("PSG\x1A\0\0\0\0\0\0\0\0\0\0\0\0", 16);
    {
        if (!build_next_emulator(emu)) { check("DAP-88", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        auto* rec = panel.findChild<QPushButton*>("audio_psg_record");
        auto* stop = panel.findChild<QPushButton*>("audio_psg_stop");
        auto* st = panel.findChild<QLabel*>("audio_psg_status");
        bool reserved = false;
        for (const QString& pfx : {QString("TurboSound:"), QString("Mode:"), QString("Stereo:"), QString("PSG mode:")})
            for (QLabel* l : panel.findChildren<QLabel*>())
                if ((l == st) && l->text().startsWith(pfx)) reserved = true;
        for (QPushButton* b : {rec, stop})
            for (const QString& pfx : {QString("TurboSound:"), QString("Mode:"), QString("Stereo:"), QString("PSG mode:")})
                if (b && b->text().startsWith(pfx)) reserved = true;
        check("DAP-88", "the Chips tab holds audio_psg_record ('Record PSG...'), audio_psg_stop ('Stop', disabled) and audio_psg_status ('Not recording PSG'); none of their texts starts with a reserved label prefix",
              rec && stop && st && rec->text() == "Record PSG..." && rec->isEnabled() && stop->text() == "Stop" &&
              !stop->isEnabled() && st->text() == "Not recording PSG" && !reserved);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-89", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        const std::string p = psg_tmp_path("g89");
        const bool a = panel.start_psg_recording(QString::fromStdString(p));
        panel.refresh();
        auto* rec = panel.findChild<QPushButton*>("audio_psg_record");
        auto* stop = panel.findChild<QPushButton*>("audio_psg_stop");
        auto* st = panel.findChild<QLabel*>("audio_psg_status");
        const bool during = dbg.psg_recording() && st->text() == "Recording PSG" &&
                            st->toolTip() == QString::fromStdString(p) && !rec->isEnabled() && stop->isEnabled();
        const bool b = panel.stop_psg_recording();
        panel.refresh();
        const bool after = !dbg.psg_recording() && st->text() == "Not recording PSG" && rec->isEnabled() &&
                           !stop->isEnabled();
        const std::string f = psg_slurp(p);
        psg_cleanup(p);
        check("DAP-89", "start_psg_recording: true, the backend records, the label reads 'Recording PSG' with the path as its tooltip, Record is disabled and Stop enabled; stop_psg_recording: true, the label 'Not recording PSG', the buttons back, and the file starts with the 16-byte .psg header",
              a && during && b && after && f.size() >= 16 && f.compare(0, 16, hdr) == 0);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-90", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        const std::string p = psg_tmp_path("g90");
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);   // as --psg-record does
        panel.refresh();
        auto* stop = panel.findChild<QPushButton*>("audio_psg_stop");
        auto* st = panel.findChild<QLabel*>("audio_psg_status");
        const bool seen = st->text() == "Recording PSG" && stop->isEnabled();
        stop->click();
        const bool stopped = !dbg.psg_recording() && st->text() == "Not recording PSG" && !stop->isEnabled();
        psg_cleanup(p);
        check("DAP-90", "a recording started through the backend (the command line's way) shows in the panel after refresh() ('Recording PSG', Stop enabled), and clicking Stop ends it",
              seen && stopped);
    }
    {
        if (!build_next_emulator(emu)) { check("DAP-91", "emulator init", false); return; }
        jnext::dbg::Debugger dbg(emu);
        AudioPanel panel(&dbg);
        const QString bad = QString::fromStdString(
            (std::filesystem::temp_directory_path() / "jnext-no-such-dir" / "x.psg").string());
        const bool r = panel.start_psg_recording(bad);
        panel.refresh();
        auto* st = panel.findChild<QLabel*>("audio_psg_status");
        check("DAP-91", "start_psg_recording on an uncreatable path: false, nothing records, and the label (still there after refresh()) starts 'Cannot write' and holds the path",
              !r && !dbg.psg_recording() && st->text().startsWith("Cannot write") && st->text().contains(bad));
    }
    {
        // Two identical machines with the same port writes over 3 frames: the panel's recording and the
        // backend's own (what --psg-record starts) are the same bytes.
        auto run = [&](bool through_panel, const std::string& path) {
            Emulator m;
            if (!build_next_emulator(m)) return false;
            m.run_frame();
            jnext::dbg::Debugger dbg(m);
            AudioPanel panel(&dbg);
            if (through_panel) panel.start_psg_recording(QString::fromStdString(path));
            else dbg.psg_record_start(jnext::dbg::CLIENT_NONE, path);
            nr_write(m, 0x08, 0x0A);
            m.port().out(0xFFFD, 0xFE);
            ay_wr(m, 0, 0x34); ay_wr(m, 7, 0x38);
            m.run_frame();
            m.port().out(0xFFFD, 0xFF);
            ay_wr(m, 8, 0x0F); ay_wr(m, 13, 0x0E);
            m.run_frame(); m.run_frame();
            if (through_panel) panel.stop_psg_recording();
            else dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
            return true;
        };
        const std::string pa = psg_tmp_path("g92a"), pb = psg_tmp_path("g92b");
        const bool ok = run(true, pa) && run(false, pb);
        const std::string a0 = psg_slurp(pa), b0 = psg_slurp(pb);
        std::string a1 = pa, b1 = pb;
        a1.insert(a1.size() - 4, "-ay1"); b1.insert(b1.size() - 4, "-ay1");
        const std::string a1d = psg_slurp(a1), b1d = psg_slurp(b1);
        psg_cleanup(pa); psg_cleanup(pb);
        check("DAP-92", "the same port writes over 3 frames, recorded through the panel and through the backend verb on two identical machines, give byte-identical files for AY#0 and AY#1 (and AY#0's holds a register block)",
              ok && a0 == b0 && a1d == b1d && a0.size() > 16 + 4 && a1d.size() > 16 + 4);
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
    test_scopes(emu);
    std::printf("  Group: DAP-SCP        -- done\n");
    test_stream_view(emu);
    std::printf("  Group: DAP-STREAM     -- done\n");
    test_tracker_tab(emu);
    std::printf("  Group: DAP-TRK        -- done\n");
    test_timing_strip(emu);
    std::printf("  Group: DAP-TIM        -- done\n");
    test_psg_row(emu);
    std::printf("  Group: DAP-PSG        -- done\n");

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
