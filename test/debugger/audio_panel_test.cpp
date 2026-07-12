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

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debugger/audio_panel.h"

#include <QApplication>
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
    return emu.init(cfg);
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

int main(int argc, char** argv) {
    // A QWidget needs a QApplication, but not a display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    Emulator emu;
    test_stereo_mode(emu);
    std::printf("  Group: AP-STEREO      — done\n");
    test_ay_ym_mode(emu);
    std::printf("  Group: AP-AYMODE      — done\n");

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
