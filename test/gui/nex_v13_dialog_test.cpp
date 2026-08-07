// GH #228 — the experimental-NEX-V1.3 GUI warning dialog, driven for real.
//
// Oracle: the GH #228 policy decision (NEX V1.3 is experimental/unsupported;
// the GUI warns with Proceed/Cancel, Cancel the default, and only Proceed
// loads). This is jnext policy, not hardware, so there is no VHDL citation.
//
// WHY THIS SUITE EXISTS. The pure decision pieces (routes-to-NEX, the version
// probe, the opt-in predicate, the Emulator::load_nex enforcement) are pinned
// by nex_loader_test NEXGATE-01..10 — but the GLUE between the dialog and the
// decision (`if (box.clickedButton() != proceed) return; allow = true;`) had
// no test at all, and the independent review proved the gap the hard way:
// mutating that `!=` to `==` makes Cancel silently GRANT the V1.3 load and
// Proceed abort — the gate fails OPEN — with every suite and the full
// regression staying green. Same failure shape as #208 round 1: miswired glue
// failing in the dangerous direction with zero signal.
//
// So these rows construct a real offscreen-QPA MainWindow, call the
// post-picker seam MainWindow::handle_load_path() directly with synthetic
// fixture files, and answer the REAL QMessageBox through a background timer
// polling QApplication::activeModalWidget() — the exact idiom of
// test/debugger/quit_gate_test.cpp QG-01..05 (see also
// test/gui/quit_cleanup_test.cpp). What each row asserts is the wiring:
// which button was clicked vs whether/with-what the load callback fired.
//
// Qt is required (MainWindow is a real QWidget), no display is: main() forces
// the offscreen QPA platform. Run: ./build/test/nex_v13_dialog_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "gui/main_window.h"

#include <QAbstractButton>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTimer>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>   // getpid() — per-process fixture paths (same reason as
                      // nex_loader_test: concurrent worktree runs share /tmp)

// ── Test infrastructure (mirrors quit_gate_test) ──────────────────────

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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

std::string fmt(const char* fmt_str, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

// ── Fixture files ─────────────────────────────────────────────────────

// Minimal NEX header carrying the given version string. handle_load_path()
// only probes the 8-byte prologue and never loads, so no banks are needed;
// a full 512-byte header keeps the file shaped like a real one.
std::string write_nex(const char* tag, const char* version) {
    const std::string path =
        (std::filesystem::temp_directory_path() /
         ("jnext_nexv13dlg_" + std::string(tag) + "_" +
          std::to_string(::getpid()) + ".nex")).string();
    std::vector<uint8_t> file(512, 0x00);
    std::memcpy(file.data() + 0, "Next", 4);
    std::memcpy(file.data() + 4, version, 4);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(file.data()),
            static_cast<std::streamsize>(file.size()));
    return f ? path : std::string();
}

// ── The modal answerer ────────────────────────────────────────────────

// Background watcher for the warning dialog. A 1 ms repeating timer polls for
// an active modal QMessageBox while handle_load_path() blocks in the dialog's
// nested event loop; when it appears it is RECORDED (saw_modal, plus whether
// Cancel is the default button) and answered. Recording-then-answering means
// the suite never hangs, even under a mutation that wrongly opens a modal on
// a "no-dialog" path — the spurious dialog is dismissed and the saw_modal
// assertion fails cleanly. (Idiom: quit_gate_test.cpp ModalWatcher; adapted
// because Proceed is a CUSTOM AcceptRole button, not a StandardButton, and
// the Escape case must go through QMessageBox's own escape-button routing.)
struct DialogWatcher {
    enum class Action { Proceed, Cancel, Escape };

    Action action;
    bool   saw_modal         = false;
    bool   cancel_is_default = false;
    QTimer timer;

    explicit DialogWatcher(Action a) : action(a) {
        QObject::connect(&timer, &QTimer::timeout, [this]() {
            QWidget* w = QApplication::activeModalWidget();
            auto*    mb = qobject_cast<QMessageBox*>(w);
            if (!mb || saw_modal) return;
            saw_modal = true;
            cancel_is_default =
                mb->defaultButton() != nullptr &&
                static_cast<QAbstractButton*>(mb->defaultButton()) ==
                    mb->button(QMessageBox::Cancel);
            switch (action) {
            case Action::Proceed: {
                // The Proceed button is the dialog's one AcceptRole button.
                QAbstractButton* proceed = nullptr;
                for (QAbstractButton* b : mb->buttons())
                    if (mb->buttonRole(b) == QMessageBox::AcceptRole) proceed = b;
                if (proceed) proceed->click();
                else         mb->done(QMessageBox::Cancel);  // malformed dialog: bail
                break;
            }
            case Action::Cancel:
                if (QAbstractButton* btn = mb->button(QMessageBox::Cancel)) btn->click();
                else mb->done(QMessageBox::Cancel);
                break;
            case Action::Escape:
                // close() routes through the dialog's escape button — the
                // real Esc-key path, which must behave exactly like Cancel.
                mb->close();
                break;
            }
        });
        timer.start(1);
    }
    void stop() { timer.stop(); }
};

// ── Fixture: a real MainWindow with a recording load callback ─────────

struct LoadRecord {
    int         calls = 0;
    std::string file;
    bool        allow = false;
};

struct Fixture {
    Emulator   emu;
    MainWindow win;
    LoadRecord rec;
    bool       ok = false;

    Fixture() {
        EmulatorConfig cfg;
        cfg.type                 = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        if (!emu.init(cfg)) return;
        win.set_emulator(&emu);
        win.set_load_file_callback(
            [this](const std::string& f, bool allow_experimental_nex_v13) {
                ++rec.calls;
                rec.file  = f;
                rec.allow = allow_experimental_nex_v13;
            });
        QApplication::processEvents();
        ok = true;
    }
};

} // namespace

// ── NEXGATE-GUI: dialog-to-decision wiring ────────────────────────────

static void test_dialog_wiring() {
    // NEXGATE-GUI-01 — Proceed loads WITH the opt-in, and Cancel is the
    // dialog's default button. Kills the reviewer's `!=` → `==` inversion:
    // under it, Proceed aborts and this row's callback never fires.
    {
        Fixture f;
        const std::string path = write_nex("proceed", "V1.3");
        if (!f.ok || path.empty()) { check("NEXGATE-GUI-01", "fixture", false); }
        else {
            DialogWatcher watch(DialogWatcher::Action::Proceed);
            f.win.handle_load_path(QString::fromStdString(path));
            watch.stop();
            check("NEXGATE-GUI-01",
                  "V1.3 file: warning dialog shown with Cancel as the DEFAULT button; "
                  "Proceed fires the load callback exactly once with allow=true (GH #228)",
                  watch.saw_modal && watch.cancel_is_default &&
                      f.rec.calls == 1 && f.rec.allow && f.rec.file == path,
                  fmt("saw_modal=%d cancel_default=%d calls=%d allow=%d file_ok=%d "
                      "(expect 1,1,1,1,1)",
                      watch.saw_modal ? 1 : 0, watch.cancel_is_default ? 1 : 0,
                      f.rec.calls, f.rec.allow ? 1 : 0, f.rec.file == path ? 1 : 0));
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    // NEXGATE-GUI-02 — Cancel aborts: the callback never fires, nothing is
    // loaded. The other half of the inversion kill: under `==`, Cancel
    // GRANTS the load and this row sees calls=1.
    {
        Fixture f;
        const std::string path = write_nex("cancel", "V1.3");
        if (!f.ok || path.empty()) { check("NEXGATE-GUI-02", "fixture", false); }
        else {
            DialogWatcher watch(DialogWatcher::Action::Cancel);
            f.win.handle_load_path(QString::fromStdString(path));
            watch.stop();
            check("NEXGATE-GUI-02",
                  "V1.3 file: Cancel on the warning dialog aborts — the load callback "
                  "never fires (GH #228)",
                  watch.saw_modal && f.rec.calls == 0,
                  fmt("saw_modal=%d calls=%d (expect 1,0)",
                      watch.saw_modal ? 1 : 0, f.rec.calls));
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    // NEXGATE-GUI-03 — Escape (window close / Esc key) behaves exactly like
    // Cancel: dialog dismissed, callback never fires. This is what makes
    // Cancel-as-default meaningful — the dismissive paths all refuse.
    {
        Fixture f;
        const std::string path = write_nex("escape", "V1.3");
        if (!f.ok || path.empty()) { check("NEXGATE-GUI-03", "fixture", false); }
        else {
            DialogWatcher watch(DialogWatcher::Action::Escape);
            f.win.handle_load_path(QString::fromStdString(path));
            watch.stop();
            check("NEXGATE-GUI-03",
                  "V1.3 file: Escape dismisses the warning dialog through its escape "
                  "button and behaves as Cancel — the load callback never fires (GH #228)",
                  watch.saw_modal && f.rec.calls == 0,
                  fmt("saw_modal=%d calls=%d (expect 1,0)",
                      watch.saw_modal ? 1 : 0, f.rec.calls));
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    // NEXGATE-GUI-04 — control: a conforming V1.2 file shows NO dialog and
    // fires the callback once with allow=false. Proves rows 01-03's modal
    // came from the V1.3 version, not from every load, and that conforming
    // loads carry no opt-in.
    {
        Fixture f;
        const std::string path = write_nex("conform", "V1.2");
        if (!f.ok || path.empty()) { check("NEXGATE-GUI-04", "fixture", false); }
        else {
            DialogWatcher watch(DialogWatcher::Action::Cancel);  // would refuse if shown
            f.win.handle_load_path(QString::fromStdString(path));
            watch.stop();
            check("NEXGATE-GUI-04",
                  "control: a V1.2 file shows no dialog and fires the load callback "
                  "once with allow=false — conforming loads are unchanged (GH #228)",
                  !watch.saw_modal && f.rec.calls == 1 && !f.rec.allow &&
                      f.rec.file == path,
                  fmt("saw_modal=%d calls=%d allow=%d file_ok=%d (expect 0,1,0,1)",
                      watch.saw_modal ? 1 : 0, f.rec.calls, f.rec.allow ? 1 : 0,
                      f.rec.file == path ? 1 : 0));
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

int main(int argc, char** argv) {
    // MainWindow is a real QWidget, so a QApplication is required — but not a
    // display: force the offscreen QPA platform (quit_gate_test idiom).
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("NEX V1.3 GUI warning-dialog tests (GH #228)\n\n");

    test_dialog_wiring();

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
