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
// LE-24..LE-27 (GH #89) drive Tape > Start Saving…'s post-picker half and the
// Stop Saving action.
//
// LE-19..LE-23 (GH #93) drive File > Insert SD Card Image…'s post-picker half,
// the real File > Eject SD Card action, and the report the frontend hands back
// once it has performed the change between frames.
//
// Run: ./build/test/load_error_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "gui/main_window.h"
#include <memory>
#include "debug/debugger.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QInputDialog>
#include <QAction>
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
#include "../row_id.h"
#include "../test_portable.h"
#include "../zip_fixture.h"
#include "core/program_archive.h"
#include "platform/pok_trainers.h"

namespace {

int g_pass = 0;
int g_fail = 0;
int g_total = 0;

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
    // GH #278 WP2 — the loop owner's debugger backend, which set_emulator()
    // requires in a debugger build; declared before the window, so it outlives it.
    std::unique_ptr<jnext::dbg::Debugger> backend;
    MainWindow win;
    int        callbacks = 0;
    bool       ok = false;

    explicit Fixture(bool sd_readonly = false) {
        EmulatorConfig cfg;
        cfg.type                 = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        cfg.sd_card_readonly     = sd_readonly;   // GH #93, LE-19
        if (!emu.init(cfg)) return;
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        win.set_debugger(backend.get());
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

// GH #392 - answers the "Load from Archive" QInputDialog: records its items and
// either picks one or cancels.
struct InputWatcher {
    int         seen = 0;
    QStringList items;
    QString     pick;      // empty = Cancel
    QTimer      timer;
    InputWatcher() {
        QObject::connect(&timer, &QTimer::timeout, [this]() {
            auto* d = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (!d) return;
            ++seen;
            items = d->comboBoxItems();
            if (pick.isEmpty()) { d->reject(); return; }
            d->setTextValue(pick);
            d->accept();
        });
        timer.start(1);
    }
    void stop() { timer.stop(); }
};

Bytes slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

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

    // ── GH #93 — File > Insert SD Card Image… / Eject SD Card ──────────
    //
    // The window only REQUESTS the change; the frontend performs it between
    // frames (emulator_service_sd_card_change(), emulator_boot_test EB-53..61)
    // and reports back through sd_card_change_finished().
    const QString card_path = QString::fromStdString(write_file("card.img", Bytes(4096, 0xB2)));
    // LE-19 — the picked card is requested with the SESSION's read-only flag.
    {
        Fixture f(/*sd_readonly=*/true);
        DialogWatcher w;
        f.win.handle_sd_card_path(card_path);
        w.pump(50);
        w.stop();
        const auto req = f.emu.take_sd_card_change_request();
        check("LE-19",
              "Insert SD Card Image requests the picked card, write-protected exactly "
              "when the session is (--sdcard-readonly), and asks nothing",
              f.ok && req && req->image == card_path.toStdString() && req->read_only &&
                  w.seen == 0,
              fmt("req=%d image=%s ro=%d seen=%d", req ? 1 : 0,
                  req ? req->image.c_str() : "", req && req->read_only ? 1 : 0, w.seen));
    }
    // LE-20 — the real File > Eject SD Card action requests an eject.
    {
        Fixture f;
        QAction* eject = nullptr;
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == "&Eject SD Card") eject = a;
        if (eject) eject->trigger();
        const auto req = f.emu.take_sd_card_change_request();
        check("LE-20", "File > Eject SD Card requests an eject (an empty image)",
              f.ok && eject && req && req->image.empty(),
              fmt("action=%d req=%d", eject ? 1 : 0, req ? 1 : 0));
    }
    // LE-21 — a refusal is reported at once, naming why, and nothing is queued.
    {
        Fixture f;
        f.emu.sd_card().set_read_overlay(0x100000, 1, [](uint32_t, uint8_t*) { return true; });
        DialogWatcher w;
        f.win.handle_sd_card_path(card_path);
        w.stop();
        const bool queued = f.emu.take_sd_card_change_request().has_value();
        check("LE-21",
              "a refused card change shows one warning saying why, and queues nothing",
              f.ok && w.seen == 1 && w.text.contains("keeps its own file open") && !queued,
              fmt("seen=%d text=%s queued=%d", w.seen, q(w.text).c_str(), queued ? 1 : 0));
    }
    // LE-22 — a performed change is confirmed on the status bar, no dialog.
    {
        Fixture f;
        DialogWatcher w;
        f.win.sd_card_change_finished(card_path, QString());
        const QString in = f.win.statusBar()->currentMessage();
        f.win.sd_card_change_finished(QString(), QString());
        const QString out = f.win.statusBar()->currentMessage();
        w.pump(50);
        w.stop();
        check("LE-22",
              "a performed insert or eject is confirmed on the status bar, with no dialog",
              f.ok && in.contains("SD card inserted") && in.contains("card.img") &&
                  out.contains("SD card ejected") && w.seen == 0,
              fmt("in=%s out=%s seen=%d", q(in).c_str(), q(out).c_str(), w.seen));
    }
    // LE-23 — a failed one says why: status bar, and a dialog posted to the
    // event loop (the frontend calls this from inside a frame tick).
    {
        Fixture f;
        DialogWatcher w;
        f.win.sd_card_change_finished(card_path, "cannot open 'card.img'");
        const int during = w.seen;
        const QString st = f.win.statusBar()->currentMessage();
        w.pump(200);
        w.stop();
        check("LE-23",
              "a failed card change says why on the status bar and in ONE deferred "
              "warning",
              f.ok && st.contains("failed") && st.contains("cannot open") && during == 0 &&
                  w.seen == 1 && w.text.contains("cannot open"),
              fmt("status=%s during=%d seen=%d text=%s", q(st).c_str(), during, w.seen,
                  q(w.text).c_str()));
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
        check("LE-15",
              "the Load dialog offers *.jns, both in the combined first entry "
              "and as a format of its own",
              lf.contains("*.jns") &&
                  lf.contains("Spectrum Files (*.nex *.jns") &&
                  lf.contains("jnext Snapshots (*.jns)"),
              q(lf));

        const QString sn = MainWindow::save_filter(/*next_machine=*/true);
        const QString sc = MainWindow::save_filter(/*next_machine=*/false);
        check("LE-16",
              "the Save dialog LEADS with *.jns on a Next — the leading entry "
              "is the default the dialog offers, and nothing else can "
              "represent a Next at all",
              sn.startsWith("jnext snapshot (*.jns)"), q(sn));
        check("LE-17",
              "…and leads with *.sna on 48K/128K/+3, where a .sna is what "
              "other emulators read",
              sc.startsWith("Spectrum snapshot (*.sna)"), q(sc));
        check("LE-18",
              "…but BOTH still offer every format, so the machine changes the "
              "recommendation and never the choice",
              sn.contains("*.jns") && sn.contains("*.sna") &&
                  sn.contains("*.szx") && sn.contains("*.nex") &&
                  sc.contains("*.jns") && sc.contains("*.sna") &&
                  sc.contains("*.szx") && sc.contains("*.nex"),
              q(sc));
    }

    // GH #89 — Tape > Start Saving... / Stop Saving: the post-picker half and
    // the Stop action, driven on a real window.
    auto find_action = [](MainWindow& win, const char* text) -> QAction* {
        for (QAction* a : win.findChildren<QAction*>())
            if (a->text() == text) return a;
        return nullptr;
    };
    // LE-24 — Start Saving arms the file, Stop finishes it.
    {
        Fixture f;
        DialogWatcher w;
        QAction* start = find_action(f.win, "Start &Saving...");
        QAction* stop = find_action(f.win, "Stop Sa&ving");
        const bool idle = start && stop && start->isEnabled() && !stop->isEnabled();
        const std::string path = (g_root / "menu.tzx").string();
        f.win.handle_tape_save_path(QString::fromStdString(path));
        const bool saving = f.emu.tape_save_active() && f.emu.tape_recorder().path() == path &&
                            start && !start->isEnabled() && stop && stop->isEnabled();
        if (stop) stop->trigger();
        w.stop();
        std::ifstream in(path, std::ios::binary);
        char sig[8] = {};
        in.read(sig, 8);
        const bool finished = in && std::memcmp(sig, "ZXTape!\x1A", 8) == 0 &&
                              !f.emu.tape_save_active() && start && start->isEnabled() &&
                              !stop->isEnabled();
        check("LE-24",
              "Tape > Start Saving... arms the chosen file as --tape-save would (Start "
              "disabled, Stop enabled, no dialog); Tape > Stop Saving finishes it (a TZX on "
              "disk) and disarms",
              f.ok && idle && saving && finished && w.seen == 0,
              fmt("idle=%d saving=%d finished=%d seen=%d", idle ? 1 : 0, saving ? 1 : 0,
                  finished ? 1 : 0, w.seen));
    }
    // LE-25 — a machine already saving (--tape-save) shows Stop enabled at once.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        cfg.tape_save_file = (g_root / "cli.wav").string();
        const bool init_ok = emu.init(cfg);
        auto backend = std::make_unique<jnext::dbg::Debugger>(emu);
        MainWindow win;
        win.set_debugger(backend.get());
        win.set_emulator(&emu);
        QAction* start = find_action(win, "Start &Saving...");
        QAction* stop = find_action(win, "Stop Sa&ving");
        check("LE-25",
              "with --tape-save on the command line the window binds with Stop Saving "
              "enabled and Start Saving disabled",
              init_ok && emu.tape_save_active() && start && !start->isEnabled() && stop &&
                  stop->isEnabled());
    }
    // LE-26 — a file that cannot be used is refused in a dialog naming it.
    {
        Fixture f;
        DialogWatcher w;
        const std::string path = write_file("notatape.tzx", Bytes(40, 0x5A));
        f.win.handle_tape_save_path(QString::fromStdString(path));
        w.pump(200);
        w.stop();
        QAction* start = find_action(f.win, "Start &Saving...");
        check("LE-26",
              "Start Saving on an existing file that is not a TZX shows a warning naming it, "
              "and saving stays off",
              f.ok && w.seen == 1 && w.text.contains("notatape.tzx") &&
                  !f.emu.tape_save_active() && start && start->isEnabled(),
              fmt("seen=%d text=%s", w.seen, q(w.text).c_str()));
    }

    // LE-27 — while an RZX records, Start Saving is refused in a dialog that
    // says why (the RZX), and nothing is armed. (A 48K: RZX recording is
    // refused on a Next.)
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 0;
        const bool init_ok = emu.init(cfg);
        auto backend = std::make_unique<jnext::dbg::Debugger>(emu);
        MainWindow win;
        win.set_debugger(backend.get());
        win.set_emulator(&emu);
        const bool recording = emu.start_rzx_recording((g_root / "le27.rzx").string());
        DialogWatcher w;
        win.handle_tape_save_path(QString::fromStdString((g_root / "le27.tzx").string()));
        w.pump(200);
        w.stop();
        emu.stop_rzx_recording();
        check("LE-27",
              "Start Saving while an RZX records is refused in a dialog naming the RZX, and "
              "saving stays off",
              init_ok && recording && w.seen == 1 && w.text.contains("RZX") &&
                  !emu.tape_save_active(),
              fmt("recording=%d seen=%d text=%s", recording ? 1 : 0, w.seen, q(w.text).c_str()));
    }


    // ------------------------------------------------------------------
    // GH #402 - a .jns puts the tape back and changes no setting. Oracle: the
    // owner (2026-10-23): "the snapshot should only restore the emulated machine
    // state, not the whole JNext state"; "leave the toggle alone". The Tape menu
    // toggles, the session's auto play and the saved preferences are what the
    // user set, whatever the file recorded.
    {
        auto toggles = [](Fixture& f, QAction*& fast, QAction*& autop) {
            fast = autop = nullptr;
            for (QAction* a : f.win.findChildren<QAction*>()) {
                if (a->text() == "&Fast Load") fast = a;
                if (a->text() == "Auto Play/Stop") autop = a;
            }
        };
        // LE-28: saved real time with auto play OFF; the session has both toggles checked.
        {
            Fixture f;
            QAction *fast, *autop;
            toggles(f, fast, autop);
            const std::string jns = (g_root / "le28.jns").string();
            bool saved = false;
            if (f.ok) {
                f.emu.load_tap(good_tap_path, false);
                f.emu.set_tape_auto_play(false);
                saved = f.emu.save_jns_file(jns);
                f.emu.tape().eject();
                f.emu.set_tape_auto_play(true);
            }
            if (fast) fast->setChecked(true);
            if (autop) autop->setChecked(true);
            const bool pf = f.win.app_config().data().tape_fast_load, pa = f.win.app_config().data().tape_auto_play;
            const bool loaded = saved && f.emu.load_jns_file(jns, false);
            f.win.load_finished(jns, true);
            check("LE-28",
                  "a .jns saved with a real-time tape and auto play off, loaded into a session whose Tape > Fast Load and "
                  "Auto Play/Stop are both checked, leaves both CHECKED and the session's auto play on; the tape is back "
                  "and the status bar names it",
                  f.ok && fast && autop && loaded && fast->isChecked() && autop->isChecked() &&
                      f.emu.tape_auto_play() && f.emu.tape().is_loaded() && f.status().contains("good.tap"),
                  fmt("loaded=%d fast=%d auto=%d emu_auto=%d status=%s", loaded, fast && fast->isChecked(),
                      autop && autop->isChecked(), f.emu.tape_auto_play(), q(f.status()).c_str()));
            check("LE-29",
                  "the same load leaves the saved preferences (tape_fast_load, tape_auto_play) unchanged",
                  f.ok && f.win.app_config().data().tape_fast_load == pf &&
                      f.win.app_config().data().tape_auto_play == pa);
        }
        // LE-29's mirror: saved fast with auto play on; both toggles unchecked in the session.
        {
            Fixture f;
            QAction *fast, *autop;
            toggles(f, fast, autop);
            const std::string jns = (g_root / "le29.jns").string();
            bool saved = false;
            if (f.ok) {
                f.emu.load_tap(good_tap_path, true);
                saved = f.emu.save_jns_file(jns);
                f.emu.tape().eject();
                f.emu.set_tape_auto_play(false);
            }
            if (fast) fast->setChecked(false);
            if (autop) autop->setChecked(false);
            const bool loaded = saved && f.emu.load_jns_file(jns, true);
            f.win.load_finished(jns, true);
            check("LE-30",
                  "the mirror: a .jns saved fast with auto play on, loaded into a session whose toggles are both UNCHECKED, "
                  "leaves both unchecked and auto play off",
                  f.ok && fast && autop && loaded && !fast->isChecked() && !autop->isChecked() &&
                      !f.emu.tape_auto_play() && f.emu.tape().is_loaded());
        }
    }

    // LE-31 (GH #402, owner Q3) - the Tape > Fast Load toggle IS the session's one
    // fast/real-time setting: MainWindow::tape_fast_load() (what QtApp's File >
    // Load / --load / .jns restore read) follows it, and Tape > Open follows the
    // same accessor.
    {
        Fixture f;
        QAction* fast = nullptr;
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == "&Fast Load") fast = a;
        bool off_ok = false, on_ok = false;
        if (fast) {
            fast->setChecked(false);
            off_ok = !f.win.tape_fast_load();
            f.win.handle_tape_path(QString::fromStdString(good_tap_path));
            off_ok = off_ok && f.emu.tape().is_loaded() && !f.emu.tape().fast_load();
            fast->setChecked(true);
            on_ok = f.win.tape_fast_load();
            f.win.handle_tape_path(QString::fromStdString(good_tap_path));
            on_ok = on_ok && f.emu.tape().fast_load();
        }
        check("LE-31",
              "MainWindow::tape_fast_load() is the Fast Load toggle (unchecked = real time, checked = fast) and Tape > Open "
              "attaches in the mode it reports",
              f.ok && fast && off_ok && on_ok);
    }

    // ------------------------------------------------------------------
    // GH #392 - programs inside .zip archives, File > Load's post-picker half.
    // Oracle: the issue and the user guide 5 (an archive loads the one program
    // inside it; several are asked for; every refusal names the archive and the
    // member, never the scratch path). The load callback stands in for the cold
    // boot and records what it was handed.
    const std::string zfix = JNEXT_ZIP_FIXTURE_DIR;
    const std::string zcfg = (g_root / "zipcfg").string();
    std::filesystem::create_directories(zcfg, ec);
    jtp::set_env("JNEXT_CONFIG_DIR", zcfg.c_str());
    const Bytes z_beeper = slurp(zfix + "/../../00regression/tap/beeper_demo.tap");
    const Bytes z_charset = slurp(zfix + "/../../00regression/tap/rom_charset_test.tap");
    struct Handed { int n = 0; std::string path; Bytes bytes; };
    auto hook = [](Fixture& f, Handed& h) {
        f.win.set_load_file_callback([&h](const std::string& p, bool) {
            ++h.n; h.path = p; h.bytes = slurp(p);
        });
    };
    // ZIPG-01
    {
        Fixture f; Handed h; hook(f, h);
        DialogWatcher w;
        f.win.handle_load_path(QString::fromStdString(zfix + "/beeper.tap.zip"));
        w.stop();
        check("ZIPG-01",
              "File > Load of beeper.tap.zip hands the load callback the extracted BEEPER.TAP, "
              "bytes == beeper_demo.tap",
              f.ok && h.n == 1 && std::filesystem::path(h.path).filename() == "BEEPER.TAP" &&
                  h.bytes == z_beeper && w.seen == 0,
              fmt("n=%d path=%s seen=%d", h.n, h.path.c_str(), w.seen));
    }
    // ZIPG-02
    {
        Fixture f; Handed h; hook(f, h);
        InputWatcher in; in.pick = "SIDEB.TAP";
        f.win.handle_load_path(QString::fromStdString(zfix + "/twotapes.zip"));
        in.stop();
        check("ZIPG-02",
              "twotapes.zip asks which program (SIDEA.TAP, SIDEB.TAP in that order); choosing "
              "SIDEB.TAP loads SIDEB.TAP",
              f.ok && in.seen == 1 && in.items == QStringList({"SIDEA.TAP", "SIDEB.TAP"}) &&
                  h.n == 1 && std::filesystem::path(h.path).filename() == "SIDEB.TAP" &&
                  h.bytes == z_charset,
              fmt("seen=%d items=%s n=%d path=%s", in.seen, q(in.items.join(",")).c_str(), h.n,
                  h.path.c_str()));
    }
    // ZIPG-03
    {
        Fixture f; Handed h; hook(f, h);
        InputWatcher in;   // Cancel
        f.win.handle_load_path(QString::fromStdString(zfix + "/twotapes.zip"));
        in.stop();
        check("ZIPG-03", "Cancel on the program list loads nothing and resets nothing",
              f.ok && in.seen == 1 && h.n == 0, fmt("seen=%d n=%d", in.seen, h.n));
    }
    // ZIPG-04
    {
        Fixture f; Handed h; hook(f, h);
        f.win.set_unattended(true);
        InputWatcher in; in.pick = "SIDEA.TAP";
        f.win.handle_load_path(QString::fromStdString(zfix + "/twotapes.zip"));
        in.stop();
        const QString st = f.win.statusBar()->currentMessage();
        check("ZIPG-04",
              "twotapes.zip in an unattended window opens no dialog, loads nothing, and the "
              "status bar lists both programs",
              f.ok && in.seen == 0 && h.n == 0 && st.contains("SIDEA.TAP") && st.contains("SIDEB.TAP"),
              fmt("seen=%d n=%d status=%s", in.seen, h.n, q(st).c_str()));
    }
    // ZIPG-05
    {
        Fixture f; Handed h; hook(f, h);
        DialogWatcher w;
        const std::string zp = zfix + "/noprog.zip";
        f.win.handle_load_path(QString::fromStdString(zp));
        w.stop();
        const QString st = f.win.statusBar()->currentMessage();
        check("ZIPG-05", "noprog.zip loads nothing; the message names the archive and `no program`",
              f.ok && h.n == 0 && st.contains(QString::fromStdString(zp)) && st.contains("no program") &&
                  w.seen == 1 && w.text.contains("no program"),
              fmt("n=%d seen=%d status=%s", h.n, w.seen, q(st).c_str()));
    }
    // ZIPG-06
    {
        Fixture f; Handed h; hook(f, h);
        DialogWatcher w;
        const std::string zp = zfix + "/encrypted.zip";
        f.win.handle_load_path(QString::fromStdString(zp));
        w.stop();
        const QString st = f.win.statusBar()->currentMessage();
        check("ZIPG-06",
              "encrypted.zip is refused naming `encrypted.zip (BEEPER.TAP)` and `encrypted`, "
              "never the scratch path",
              f.ok && h.n == 0 && w.seen == 1 && w.text.contains("encrypted.zip (BEEPER.TAP)") &&
                  w.text.contains("encrypted") && !w.text.contains("archive-load") &&
                  !st.contains("archive-load"),
              fmt("n=%d seen=%d text=%s", h.n, w.seen, q(w.text).c_str()));
    }
    // ZIPG-07
    {
        Fixture f; Handed h; hook(f, h);
        DialogWatcher w;
        f.win.handle_load_path(QString::fromStdString(zfix + "/beeper.tap.zip"));
        w.stop();
        const auto* s = f.win.pok_session();
        const QString st = f.win.statusBar()->currentMessage();
        check("ZIPG-07",
              "beeper.tap.zip makes its bundled POK the POK session (1 trainer, named by archive "
              "and member) without opening the dialog, and says so in the status bar",
              f.ok && h.n == 1 && s && s->file().trainers.size() == 1 &&
                  QString::fromStdString(s->path()).endsWith("beeper.tap.zip (POKES/BEEPER.POK)") &&
                  !f.win.pok_dialog_visible() && st.contains("POK Trainers"),
              fmt("n=%d session=%d dialog=%d status=%s", h.n, s ? 1 : 0,
                  f.win.pok_dialog_visible() ? 1 : 0, q(st).c_str()));
    }
    // ZIPG-08
    {
        Fixture f; Handed h; hook(f, h);
        DialogWatcher w;
        const std::string zp = (g_root / "badsna.zip").string();
        {
            zipfx::Member m; m.name = "BAD.SNA"; m.data = Bytes(10, 0x42);
            const Bytes z = zipfx::build({m});
            std::ofstream o(zp, std::ios::binary);
            o.write(reinterpret_cast<const char*>(z.data()), static_cast<std::streamsize>(z.size()));
        }
        f.win.handle_load_path(QString::fromStdString(zp));
        w.stop();
        check("ZIPG-08",
              "an archive holding a damaged BAD.SNA is refused before the callback, naming "
              "`<archive> (BAD.SNA)`, not the scratch path",
              f.ok && h.n == 0 && w.seen == 1 && w.text.contains("badsna.zip (BAD.SNA)") &&
                  !w.text.contains("archive-load"),
              fmt("n=%d seen=%d text=%s", h.n, w.seen, q(w.text).c_str()));
    }
    // ZIPG-09
    check("ZIPG-09", "the File > Load filter offers *.zip in 'Spectrum Files' and as its own 'ZIP Archives' entry",
          MainWindow::load_filter().contains("*.rzx *.zip);;") &&
              MainWindow::load_filter().contains("ZIP Archives (*.zip);;"),
          q(MainWindow::load_filter()));

    // ---- GH #392 round 2 ----
    auto write_zip = [](const char* name, const std::vector<zipfx::Member>& ms) {
        const std::string zp = (g_root / name).string();
        const Bytes z = zipfx::build(ms);
        std::ofstream o(zp, std::ios::binary);
        o.write(reinterpret_cast<const char*>(z.data()), static_cast<std::streamsize>(z.size()));
        return zp;
    };
    auto zmem = [](const std::string& n, const Bytes& d) {
        zipfx::Member m; m.name = n; m.data = d; m.method = 0; return m;
    };
    // ZIPG-10
    {
        Fixture f; Handed h; hook(f, h);
        const std::string zp = write_zip("cp437.zip", {zmem("F\x82TBOL.TAP", Bytes(8, 1)), zmem("OTHER.TAP", Bytes(8, 2))});
        InputWatcher in; in.pick = "F\\x82TBOL.TAP";
        f.win.handle_load_path(QString::fromStdString(zp));
        in.stop();
        check("ZIPG-10",
              "a program whose name is not UTF-8 (CP437 0x82) is listed escaped and loads when picked "
              "(the choice is mapped by index, not by re-encoding the text)",
              f.ok && in.seen == 1 && in.items.size() == 2 && in.items[0] == "F\\x82TBOL.TAP" && h.n == 1 &&
                  std::filesystem::path(h.path).filename() == "F_TBOL.TAP" && h.bytes == Bytes(8, 1),
              fmt("seen=%d items=%s n=%d path=%s", in.seen, q(in.items.join(",")).c_str(), h.n, h.path.c_str()));
    }
    // ZIPG-11
    {
        Fixture f; Handed h; hook(f, h);
        DialogWatcher w;
        f.win.handle_load_path(QString::fromStdString(zfix + "/beeper.tap.zip"));
        f.win.load_finished(h.path, false);   // the cold boot reports the load failed
        w.pump(300);
        w.stop();
        check("ZIPG-11",
              "when the load of an archive's program fails, the Load Failed box names `<archive> (BEEPER.TAP)`, "
              "not the scratch path",
              f.ok && h.n == 1 && w.seen == 1 && w.text.contains("beeper.tap.zip (BEEPER.TAP)") &&
                  !w.text.contains("archive-load"),
              fmt("seen=%d text=%s", w.seen, q(w.text).c_str()));
    }
    const std::string trainer = "NInfinite\r\nZ   8 32768   0   0\r\nY\r\n";
    // ZIPG-12
    {
        Fixture f; Handed h; hook(f, h);
        std::string big = trainer;
        big.append(1024 * 1024, ' ');   // over the 1 MiB limit for a trainer file
        const std::string zp = write_zip("bigpok.zip", {zmem("G.TAP", Bytes(8, 1)),
                                                        zmem("G.POK", Bytes(big.begin(), big.end()))});
        f.win.handle_load_path(QString::fromStdString(zp));
        check("ZIPG-12", "a bundled .pok over 1 MiB is not offered (no POK session) while the program still loads",
              f.ok && h.n == 1 && !f.win.pok_session(), fmt("n=%d", h.n));
    }
    // ZIPG-13
    {
        Fixture f;
        f.win.set_load_file_callback(nullptr);   // nothing takes the load
        DialogWatcher w;
        f.win.handle_load_path(QString::fromStdString(zfix + "/beeper.tap.zip"));
        w.stop();
        check("ZIPG-13", "the bundled .pok is not offered when the load was not handed on",
              f.ok && !f.win.pok_session());
    }
    // ZIPG-14
    {
        Fixture f; Handed h; hook(f, h);
        const std::string t = "this is not a trainer file\r\n";
        const std::string zp = write_zip("nopok.zip", {zmem("G.TAP", Bytes(8, 1)), zmem("G.POK", Bytes(t.begin(), t.end()))});
        f.win.handle_load_path(QString::fromStdString(zp));
        check("ZIPG-14", "a bundled .pok that holds no trainer is not offered while the program still loads",
              f.ok && h.n == 1 && !f.win.pok_session(), fmt("n=%d", h.n));
    }
    // ZIPG-15
    {
        Fixture f; Handed h; hook(f, h);
        f.win.set_unattended(true);
        const std::string zp = write_zip("esc.zip", {zmem("\x1b]0;PWNED\x07" "A.TAP", Bytes(8, 1)), zmem("\x1b[2J" "B.TAP", Bytes(8, 2))});
        f.win.handle_load_path(QString::fromStdString(zp));
        const QString st = f.win.statusBar()->currentMessage();
        bool ctl = false;
        for (QChar c : st) if (c.unicode() < 0x20 || c.unicode() == 0x7F) ctl = true;
        check("ZIPG-15", "control bytes in member names never reach the status bar (escaped as \\xNN)",
              f.ok && h.n == 0 && !st.isEmpty() && !ctl && st.contains("\\x1B"), q(st));
    }

    // ZIPG-16
    {
        bool all = true;
        std::string det;
        int i = 0;
        for (const char* name : {"\x1b[2J\xC3\xA9.TAP", "F\x82.TAP"}) {
            Fixture f; Handed h; hook(f, h);
            zipfx::Member m; m.name = name; m.data = Bytes(8, 1); m.method = 0; m.flags = 1;   // encrypted: extraction fails
            const std::string zp = write_zip(i++ ? "enc_cp437.zip" : "enc_esc.zip", {m});
            DialogWatcher w;
            f.win.handle_load_path(QString::fromStdString(zp));
            w.stop();
            const QString shown = QString::fromStdString(program_archive::display_name(name));
            const QString st = f.win.statusBar()->currentMessage();
            auto once = [&shown](const QString& t) { return t.count(shown) == 1; };
            bool ctl = false;
            for (const QString& t : {st, w.text}) for (QChar c : t) if (c.unicode() < 0x20 || c.unicode() == 0x7F) ctl = true;
            if (!(f.ok && h.n == 0 && w.seen == 1 && once(st) && once(w.text) && !ctl &&
                  st.contains("encrypted"))) {
                all = false;
                det += q(st) + " | " + q(w.text) + " || ";
            }
        }
        check("ZIPG-16",
              "an escape-named and a CP437-named program whose extraction fails are shown escaped, exactly once, "
              "in the dialog and the status bar (the line the log also carries)", all, det);
    }
    // ZIPG-17
    {
        Fixture f; Handed h; hook(f, h);
        const std::string pok = "\x1b[0m.POK";
        const std::string zp = write_zip("pokname.zip", {zmem("G.TAP", Bytes(8, 1)),
                                                         zmem(pok, Bytes(trainer.begin(), trainer.end()))});
        f.win.handle_load_path(QString::fromStdString(zp));
        const auto* s = f.win.pok_session();
        check("ZIPG-17", "the POK session of an archive names the .pok member escaped",
              f.ok && h.n == 1 && s && s->path() == zp + " (" + program_archive::display_name(pok) + ")" &&
                  s->path().find('\x1b') == std::string::npos,
              s ? s->path() : std::string("no session"));
    }

    std::filesystem::remove_all(g_root, ec);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
