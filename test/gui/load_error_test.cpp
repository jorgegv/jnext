// GUI load paths report a failed load, and a refused tape leaves the tape
// that was in.
//
// Oracle: the GUI's own convention for a failed file operation — a warning
// dialog naming the file, as the debugger's Load MAP File gives ("Could not
// load MAP file"), Save Screenshot, Save Snapshot and Record all do — plus the
// user guide's account of the Tape menu (5.5: "Turn [real time] on with Tape >
// Fast Load (uncheck it)"; "The status bar shows the tape name"). There is no
// hardware in this: it is jnext's own UI.
//
// Before these rows, Tape > Open Tape File and File > Play RZX Recording threw
// the loader's result away (a refused file showed nothing at all), File >
// Open only logged it, Tape > Open always loaded fast whatever the Fast Load
// toggle said, and a WAV tape was invisible to the status bar, Eject and
// Rewind.
//
// The rows drive the REAL post-picker halves (handle_tape_path,
// handle_rzx_play_path, handle_load_path + load_finished) of a real
// offscreen MainWindow and answer the real QMessageBox through a polling
// timer, the idiom of nex_v13_dialog_test / quit_gate_test.
//
// Run: ./build/test/load_error_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "gui/main_window.h"

#include <QAbstractButton>
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QString>
#include <QTimer>

#include <unistd.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int g_pass = 0;
int g_fail = 0;
int g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
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

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

using Bytes = std::vector<uint8_t>;

std::filesystem::path g_root;

std::string write_file(const char* name, const Bytes& bytes) {
    const auto path = g_root / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

// A TAP block: [len][flag][payload][xor].
Bytes tap_block(uint8_t flag, const Bytes& payload) {
    uint8_t sum = flag;
    for (uint8_t b : payload) sum ^= b;
    const uint16_t len = static_cast<uint16_t>(payload.size() + 2);
    Bytes out = {static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8), flag};
    out.insert(out.end(), payload.begin(), payload.end());
    out.push_back(sum);
    return out;
}
Bytes good_tap() {
    Bytes out = tap_block(0x00, {3, 'T', 'E', 'S', 'T', ' ', ' ', ' ', ' ', ' ', ' ',
                                 5, 0, 0x00, 0x80, 0x00, 0x80});
    Bytes data = tap_block(0xFF, {1, 2, 3, 4, 5});
    out.insert(out.end(), data.begin(), data.end());
    return out;
}
Bytes good_tzx() {
    Bytes out = {'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 0x01, 0x14};
    Bytes tap = good_tap();
    // Re-wrap each TAP block as a $10 standard-speed block.
    size_t pos = 0;
    while (pos + 2 <= tap.size()) {
        const uint16_t len = static_cast<uint16_t>(tap[pos] | (tap[pos + 1] << 8));
        out.push_back(0x10);
        out.push_back(0xE8); out.push_back(0x03);   // pause 1000 ms
        out.push_back(tap[pos]); out.push_back(tap[pos + 1]);
        out.insert(out.end(), tap.begin() + static_cast<long>(pos + 2),
                   tap.begin() + static_cast<long>(pos + 2 + len));
        pos += 2 + len;
    }
    return out;
}
Bytes good_wav() {
    const uint32_t samples = 441;
    auto u32 = [](uint32_t v) { return Bytes{static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                                             static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)}; };
    auto u16 = [](uint16_t v) { return Bytes{static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)}; };
    Bytes out = {'R', 'I', 'F', 'F'};
    for (uint8_t b : u32(36 + samples)) out.push_back(b);
    for (char c : std::string("WAVEfmt ")) out.push_back(static_cast<uint8_t>(c));
    for (uint8_t b : u32(16)) out.push_back(b);
    for (uint8_t b : u16(1)) out.push_back(b);        // PCM
    for (uint8_t b : u16(1)) out.push_back(b);        // mono
    for (uint8_t b : u32(44100)) out.push_back(b);
    for (uint8_t b : u32(44100)) out.push_back(b);
    for (uint8_t b : u16(1)) out.push_back(b);
    for (uint8_t b : u16(8)) out.push_back(b);        // 8-bit
    for (char c : std::string("data")) out.push_back(static_cast<uint8_t>(c));
    for (uint8_t b : u32(samples)) out.push_back(b);
    for (uint32_t i = 0; i < samples; ++i) out.push_back((i / 20) % 2 ? 0xE0 : 0x20);
    return out;
}

// Records every modal QMessageBox that opens while it runs, and dismisses it.
struct DialogWatcher {
    int     seen = 0;
    QString text;
    QString title;
    QTimer  timer;

    DialogWatcher() {
        QObject::connect(&timer, &QTimer::timeout, [this]() {
            auto* mb = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!mb) return;
            ++seen;
            text = mb->text();
            title = mb->windowTitle();
            mb->done(QMessageBox::Ok);
        });
        timer.start(1);
    }
    // Let queued work (a deferred dialog) run for up to `ms`.
    void pump(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    void stop() { timer.stop(); }
};

struct Fixture {
    Emulator   emu;
    MainWindow win;
    int        callbacks = 0;
    bool       ok = false;

    Fixture() {
        EmulatorConfig cfg;
        cfg.type                 = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        if (!emu.init(cfg)) return;
        win.set_emulator(&emu);
        // Stands in for QtApp::cold_boot(): the load itself runs later.
        win.set_load_file_callback([this](const std::string&, bool) { ++callbacks; });
        QApplication::processEvents();
        ok = true;
    }
    QString status() {
        QString out;
        for (QLabel* l : win.statusBar()->findChildren<QLabel*>())
            if (l->text().startsWith("Tape:")) out = l->text();
        return out;
    }
};

std::string q(const QString& s) { return s.toStdString(); }

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    std::printf("GUI load-failure reporting tests\n\n");

    g_root = std::filesystem::temp_directory_path() /
             ("jnext-load-error-test-" + std::to_string(static_cast<long>(::getpid())));
    std::error_code ec;
    std::filesystem::remove_all(g_root, ec);
    std::filesystem::create_directories(g_root, ec);

    const std::string good_tap_path = write_file("good.tap", good_tap());
    const std::string good_tzx_path = write_file("good.tzx", good_tzx());
    const std::string good_wav_path = write_file("good.wav", good_wav());
    Bytes trunc = good_tap();
    trunc.resize(trunc.size() - 3);
    const std::string bad_tap_path = write_file("bad.tap", trunc);
    const std::string bad_tzx_path = write_file("bad.tzx", Bytes(300, 0x5A));
    const std::string bad_wav_path = write_file("bad.wav", Bytes(100, 0x00));
    const std::string bad_rzx_path = write_file("bad.rzx", Bytes(120, 0x33));

    // LE-01 — a malformed .tap from the Tape menu: one warning naming it, and
    // the tape that was already in stays in.
    {
        Fixture f;
        f.win.handle_tape_path(QString::fromStdString(good_tap_path));
        DialogWatcher w;
        f.win.handle_tape_path(QString::fromStdString(bad_tap_path));
        w.stop();
        check("LE-01",
              "Tape > Open of a malformed .tap shows one 'Load Failed' warning naming the "
              "file, and the tape already in (good.tap, 2 blocks) stays attached",
              f.ok && w.seen == 1 && w.title == "Load Failed" &&
              w.text.contains("bad.tap") && f.emu.tape().is_loaded() &&
              f.emu.tape().filename().find("good.tap") != std::string::npos &&
              f.emu.tape().block_count() == 2 && f.status().contains("good.tap"),
              fmt("seen=%d title=%s text=%s tap=%s blocks=%zu status=%s", w.seen,
                  q(w.title).c_str(), q(w.text).c_str(), f.emu.tape().filename().c_str(),
                  f.emu.tape().block_count(), q(f.status()).c_str()));
    }
    // LE-02 — the same for a .tzx.
    {
        Fixture f;
        f.win.handle_tape_path(QString::fromStdString(good_tzx_path));
        DialogWatcher w;
        f.win.handle_tape_path(QString::fromStdString(bad_tzx_path));
        w.stop();
        check("LE-02",
              "Tape > Open of a malformed .tzx shows one warning naming it; the loaded "
              "TZX stays",
              f.ok && w.seen == 1 && w.text.contains("bad.tzx") &&
              f.emu.tzx_tape().is_loaded() && f.emu.tzx_tape().filename() == "good.tzx",
              fmt("seen=%d text=%s tzx=%s", w.seen, q(w.text).c_str(),
                  f.emu.tzx_tape().filename().c_str()));
    }
    // LE-03 — the same for a .wav.
    {
        Fixture f;
        f.win.handle_tape_path(QString::fromStdString(good_wav_path));
        DialogWatcher w;
        f.win.handle_tape_path(QString::fromStdString(bad_wav_path));
        w.stop();
        check("LE-03",
              "Tape > Open of a malformed .wav shows one warning naming it; the loaded "
              "WAV stays",
              f.ok && w.seen == 1 && w.text.contains("bad.wav") &&
              f.emu.wav_tape().is_loaded() && f.emu.wav_tape().filename() == "good.wav",
              fmt("seen=%d text=%s wav_loaded=%d", w.seen, q(w.text).c_str(),
                  f.emu.wav_tape().is_loaded() ? 1 : 0));
    }
    // LE-04 — control: a good tape shows no dialog and is named in the status bar.
    {
        Fixture f;
        DialogWatcher w;
        f.win.handle_tape_path(QString::fromStdString(good_tap_path));
        w.stop();
        check("LE-04",
              "control: Tape > Open of a good .tap shows no dialog and the status bar "
              "names it",
              f.ok && w.seen == 0 && f.emu.tape().is_loaded() &&
              f.status().contains("good.tap"),
              fmt("seen=%d status=%s", w.seen, q(f.status()).c_str()));
    }
    // LE-05 — Tape > Open follows the Fast Load toggle (user guide 5.5).
    {
        Fixture f;
        QAction* fast = nullptr;
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == "&Fast Load") fast = a;
        if (fast) fast->setChecked(false);
        f.win.handle_tape_path(QString::fromStdString(good_tap_path));
        const bool tap_real = f.emu.tape().is_loaded() && !f.emu.tape().fast_load();
        f.win.handle_tape_path(QString::fromStdString(good_tzx_path));
        const bool tzx_real = f.emu.tzx_tape().is_loaded() && !f.emu.tzx_tape().fast_load();
        if (fast) fast->setChecked(true);
        f.win.handle_tape_path(QString::fromStdString(good_tap_path));
        const bool tap_fast = f.emu.tape().is_loaded() && f.emu.tape().fast_load();
        check("LE-05",
              "with Tape > Fast Load unchecked, Tape > Open attaches a .tap and a .tzx in "
              "real time; checked, in fast mode",
              f.ok && fast && tap_real && tzx_real && tap_fast,
              fmt("toggle=%d tap_real=%d tzx_real=%d tap_fast=%d", fast ? 1 : 0,
                  tap_real ? 1 : 0, tzx_real ? 1 : 0, tap_fast ? 1 : 0));
    }
    // LE-06 — a WAV is visible to the status bar and to Eject.
    {
        Fixture f;
        f.win.handle_tape_path(QString::fromStdString(good_wav_path));
        const QString shown = f.status();
        QAction* eject = nullptr;
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == "&Eject Tape") eject = a;
        const bool enabled = eject && eject->isEnabled();
        if (eject) eject->trigger();
        check("LE-06",
              "a WAV tape is named in the status bar, enables Eject, and Eject removes it",
              f.ok && shown.contains("good.wav") && enabled && !f.emu.wav_tape().is_loaded() &&
              f.status() == "Tape: none",
              fmt("status=%s eject_enabled=%d wav_after=%d status_after=%s", q(shown).c_str(),
                  enabled ? 1 : 0, f.emu.wav_tape().is_loaded() ? 1 : 0, q(f.status()).c_str()));
    }
    // LE-11 — Rewind on a WAV starts it again (a WAV always plays in real
    // time, so there is no position to rewind to but the start). The fixture
    // WAV is 441 samples, low for 20, high for 20; two frames run it past its
    // end, where it reads 0; after Rewind, sample 30 (high) is 2381 T-states
    // ahead.
    {
        Fixture f;
        f.win.handle_tape_path(QString::fromStdString(good_wav_path));
        f.emu.run_frame();
        f.emu.run_frame();
        const uint64_t before = f.emu.monotonic_tstates();
        const uint8_t past_end = f.emu.wav_tape().get_ear_bit(before + 2381);
        QAction* rewind = nullptr;
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == "&Rewind") rewind = a;
        const bool enabled = rewind && rewind->isEnabled();
        if (rewind) rewind->trigger();
        const uint64_t now = f.emu.monotonic_tstates();
        const uint8_t sample30 = f.emu.wav_tape().get_ear_bit(now + 2381);
        check("LE-11",
              "Tape > Rewind is enabled for a WAV and starts it again: past its end it "
              "reads 0, after Rewind sample 30 (high) is back",
              f.ok && enabled && past_end == 0 && sample30 == 1,
              fmt("rewind_enabled=%d past_end=%u sample30=%u", enabled ? 1 : 0, past_end,
                  sample30));
    }
    // LE-07 — a TAP opened after a TZX replaces it: the status bar and Rewind
    // are on the new tape, not the old one.
    {
        Fixture f;
        f.win.handle_tape_path(QString::fromStdString(good_tzx_path));
        f.win.handle_tape_path(QString::fromStdString(good_tap_path));
        check("LE-07",
              "Tape > Open of a .tap after a .tzx leaves only the .tap, and the status bar "
              "names it",
              f.ok && f.emu.tape().is_loaded() && !f.emu.tzx_tape().is_loaded() &&
              f.status().contains("good.tap"),
              fmt("tap=%d tzx=%d status=%s", f.emu.tape().is_loaded() ? 1 : 0,
                  f.emu.tzx_tape().is_loaded() ? 1 : 0, q(f.status()).c_str()));
    }
    // LE-08 — File > Open: the frontend reports the scheduled load failed; the
    // window shows the warning once the event loop runs (it is called from
    // inside the frame tick, so the dialog is deferred).
    {
        Fixture f;
        f.win.handle_load_path(QString::fromStdString(bad_tap_path));
        DialogWatcher w;
        f.win.load_finished(bad_tap_path, false);
        const int immediate = w.seen;
        w.pump(200);
        w.stop();
        check("LE-08",
              "File > Open of a file whose load then fails: one warning naming it, shown "
              "from the event loop, not inside the call",
              f.ok && f.callbacks == 1 && immediate == 0 && w.seen == 1 &&
              w.text.contains("bad.tap"),
              fmt("callbacks=%d immediate=%d seen=%d text=%s", f.callbacks, immediate,
                  w.seen, q(w.text).c_str()));
    }
    // LE-09 — a load the window did not ask for (--load, M_EXECCMD) that fails
    // is not the window's to report; a menu load that succeeds shows nothing.
    {
        Fixture f;
        DialogWatcher w;
        f.win.load_finished(bad_tap_path, false);            // not a menu load
        f.win.handle_load_path(QString::fromStdString(good_tap_path));
        f.win.load_finished(good_tap_path, true);            // menu load, fine
        w.pump(200);
        w.stop();
        check("LE-09",
              "no dialog for a failed load the window did not start, nor for a menu load "
              "that succeeded",
              f.ok && w.seen == 0, fmt("seen=%d", w.seen));
    }
    // LE-10 — File > Play RZX Recording of a file that is not an RZX.
    {
        Fixture f;
        DialogWatcher w;
        f.win.handle_rzx_play_path(QString::fromStdString(bad_rzx_path));
        w.stop();
        check("LE-10",
              "File > Play RZX Recording of a file that is not an RZX shows one warning "
              "naming it",
              f.ok && w.seen == 1 && w.text.contains("bad.rzx"),
              fmt("seen=%d text=%s", w.seen, q(w.text).c_str()));
    }

    // ── GH #27 S8 — the `.jns` file-dialog FILTERS ──────────────────────
    //
    // A widening of this suite's scope, stated rather than slipped in: its
    // other rows drive the post-picker halves, and these are about what the
    // picker OFFERS. They live here because the alternative is a filter string
    // that exists only inside a `QFileDialog` call, which no test can reach —
    // and jnext has shipped a format the dialog did not list before.
    {
        const QString lf = MainWindow::load_filter();
        check("LE-11",
              "the Load dialog offers *.jns, both in the combined first entry "
              "and as a format of its own",
              lf.contains("*.jns") &&
                  lf.contains("Spectrum Files (*.nex *.jns") &&
                  lf.contains("jnext Snapshots (*.jns)"),
              q(lf));

        const QString sn = MainWindow::save_filter(/*next_machine=*/true);
        const QString sc = MainWindow::save_filter(/*next_machine=*/false);
        check("LE-12",
              "the Save dialog LEADS with *.jns on a Next — the leading entry "
              "is the default the dialog offers, and nothing else can "
              "represent a Next at all",
              sn.startsWith("jnext snapshot (*.jns)"), q(sn));
        check("LE-13",
              "…and leads with *.sna on 48K/128K/+3, where a .sna is what "
              "other emulators read",
              sc.startsWith("Spectrum snapshot (*.sna)"), q(sc));
        check("LE-14",
              "…but BOTH still offer every format, so the machine changes the "
              "recommendation and never the choice",
              sn.contains("*.jns") && sn.contains("*.sna") &&
                  sn.contains("*.szx") && sn.contains("*.nex") &&
                  sc.contains("*.jns") && sc.contains("*.sna") &&
                  sc.contains("*.szx") && sc.contains("*.nex"),
              q(sc));
    }

    std::filesystem::remove_all(g_root, ec);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
