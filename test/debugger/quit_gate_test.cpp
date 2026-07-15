// Debugger Quit-Gate Test (Task 60f — "app-quit must not be trapped by the
// corruption resume-prompt").
//
// Background. Task 60b made a failed rewind/step-back leave the machine in a
// corrupt, partially-restored state (Emulator::last_state_error() non-empty).
// Task 60e then added a resume-gate: every path that would resume or step a
// corrupt machine — Run/Step/RunTo*, AND the debugger-disable auto-resume in
// DebuggerManager::set_enabled(false) — first shows a modal Yes/No warning
// (default No). Declining keeps the debugger enabled + paused.
//
// The 60f bug. MainWindow::closeEvent (the WM [X] / app-quit) also calls
// set_enabled(false). On a corrupt machine the modal appeared there too, and
// DECLINING left the debugger window open while closeEvent still hid the main
// window. Both are parentless top-levels with default WA_QuitOnClose and no
// setQuitOnLastWindowClosed(false), so lastWindowClosed never fired → the
// process hung with a hidden main window and an orphaned debugger window.
//
// The 60f fix. set_enabled() gained a `prompt_on_corrupt` flag (default true).
// The app-quit path passes FALSE: quitting destroys the whole machine, so the
// resume-gate — which only exists to protect a machine you intend to keep
// running — does not apply; the disable proceeds unconditionally and the
// debugger window is closed so the process terminates. Every INTERACTIVE path
// keeps the default (true), so 60e's safety is untouched.
//
// This suite drives the REAL DebuggerManager::set_enabled against a headless
// Emulator forced into the corrupt+enabled+paused state, with a background
// timer that auto-answers (or merely detects) the modal. It is discriminative:
//   * QG-01 is the 60f addition. Delete the `prompt_on_corrupt` param (or make
//     the gate always run) and QG-01 flips — a modal appears on the quit path,
//     the decline aborts the disable, is_enabled() stays true.
//   * QG-02 pins 60e: an INTERACTIVE decline on the SAME corrupt state DOES
//     prompt and DOES stay enabled. Force `prompt_on_corrupt` always-false and
//     QG-02 flips — no modal, machine disabled. So QG-01+QG-02 together catch
//     the mutation in either direction.
//
// Qt is required (DebuggerManager owns real QWidgets), but no display is:
// main() forces the offscreen QPA platform (same idiom as the panel suites).
// Run: ./build/test/debugger_quit_gate_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "debug/debug_state.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"

#include <QAbstractButton>
#include <QApplication>
#include <QMainWindow>
#include <QMessageBox>
#include <QTimer>

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

// Force the machine into the Task-60b corrupt state through the REAL public
// path: feed load_state() a buffer that fails the very first desync sentinel.
// load_state clears then re-latches last_state_error_ and bumps the generation
// before restoring anything, so the Emulator's memory/registers are untouched;
// only the corruption breadcrumb is set. Returns true iff corruption latched.
bool make_corrupt(Emulator& emu) {
    // Four zero bytes: the first sentinel u32 is kStateSentinelMagic ^ 0, which
    // is not zero, so the compare fails immediately (and even if it matched, the
    // buffer is far too short → the next read is out_of_bounds). Either way the
    // restore aborts at subsystem #0 and latches last_state_error_.
    const uint8_t garbage[4] = {0, 0, 0, 0};
    StateReader r(garbage, sizeof(garbage));
    emu.load_state(r);
    return !emu.last_state_error().empty();
}

// Background watcher for the confirm_resume_if_corrupt() modal. A 1 ms repeating
// timer polls for an active modal QMessageBox while set_enabled() blocks in the
// dialog's nested event loop; when it appears it is RECORDED (saw_modal) and
// answered with `to_click`. Recording-then-answering means the suite never hangs
// even under a mutation that wrongly opens a modal on a "no-prompt" path — the
// spurious dialog is dismissed and the saw_modal assertion fails cleanly.
struct ModalWatcher {
    QMessageBox::StandardButton to_click;
    bool  saw_modal = false;
    QTimer timer;

    explicit ModalWatcher(QMessageBox::StandardButton b) : to_click(b) {
        QObject::connect(&timer, &QTimer::timeout, [this]() {
            QWidget* w = QApplication::activeModalWidget();
            if (auto* mb = qobject_cast<QMessageBox*>(w)) {
                saw_modal = true;
                if (QAbstractButton* btn = mb->button(to_click))
                    btn->click();
                else
                    mb->done(to_click);
            }
        });
        timer.start(1);
    }
    void stop() { timer.stop(); }
};

// One self-contained fixture: a headless Next Emulator, a bare QMainWindow, and
// a DebuggerManager, brought up enabled and paused. `corrupt` optionally latches
// the Task-60b corrupt state. Each row uses its own fixture for independence.
struct Fixture {
    Emulator     emu;
    QMainWindow  win;
    DebuggerManager* mgr = nullptr;
    bool         ok = false;

    explicit Fixture(bool corrupt) {
        if (!build_next_emulator(emu)) return;
        mgr = new DebuggerManager(&win, &emu, &win);  // parented → auto-freed
        mgr->set_enabled(true);                        // create + show window
        if (corrupt && !make_corrupt(emu)) return;
        emu.debug_state().pause();                     // arm the resume branch
        ok = true;
    }
};

} // namespace

// ── QG: the quit path is not gated; interactive paths still are ────────

static void test_quit_gate() {
    set_group("QG");

    // QG-01 — THE 60f fix. Corrupt + enabled + paused. The app-quit path calls
    // set_enabled(false, prompt_on_corrupt=false): NO modal, the disable always
    // succeeds, the debugger ends disabled. This is what lets the process
    // terminate. Mutation (drop the param / always-run the gate): a modal
    // appears → the watcher's decline aborts the disable → is_enabled() stays
    // true and saw_modal flips.
    {
        Fixture fx(/*corrupt=*/true);
        if (!fx.ok) { check("QG-01", "fixture (corrupt+paused)", false); }
        else {
            ModalWatcher watch(QMessageBox::No);   // would decline if prompted
            const bool ret = fx.mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
            watch.stop();
            check("QG-01", "app-quit path disables a corrupt machine WITHOUT the "
                  "resume prompt (returns true, debugger off, no modal shown)",
                  ret && !fx.mgr->is_enabled() && !watch.saw_modal,
                  fmt("ret=%d is_enabled=%d saw_modal=%d (expect 1,0,0)",
                      ret, fx.mgr->is_enabled(), watch.saw_modal));
        }
    }

    // QG-02 — 60e intact: the INTERACTIVE disable (default prompt_on_corrupt) on
    // the SAME corrupt state DOES prompt, and DECLINING keeps the debugger
    // enabled + paused (set_enabled returns false). Force prompt_on_corrupt
    // always-false and this flips: no modal, machine disabled.
    {
        Fixture fx(/*corrupt=*/true);
        if (!fx.ok) { check("QG-02", "fixture (corrupt+paused)", false); }
        else {
            ModalWatcher watch(QMessageBox::No);   // decline
            const bool ret = fx.mgr->set_enabled(false);   // default: prompt
            watch.stop();
            check("QG-02", "interactive disable of a corrupt machine prompts, and "
                  "declining stays enabled + paused (returns false, modal shown)",
                  !ret && fx.mgr->is_enabled() && watch.saw_modal,
                  fmt("ret=%d is_enabled=%d saw_modal=%d (expect 0,1,1)",
                      ret, fx.mgr->is_enabled(), watch.saw_modal));
        }
    }

    // QG-03 — 60e accept branch: interactive disable, corrupt, user clicks Yes →
    // the resume proceeds and the debugger disables (returns true).
    {
        Fixture fx(/*corrupt=*/true);
        if (!fx.ok) { check("QG-03", "fixture (corrupt+paused)", false); }
        else {
            ModalWatcher watch(QMessageBox::Yes);  // accept
            const bool ret = fx.mgr->set_enabled(false);   // default: prompt
            watch.stop();
            check("QG-03", "interactive disable of a corrupt machine, user accepts "
                  "→ resumes and disables (returns true, modal shown)",
                  ret && !fx.mgr->is_enabled() && watch.saw_modal,
                  fmt("ret=%d is_enabled=%d saw_modal=%d (expect 1,0,1)",
                      ret, fx.mgr->is_enabled(), watch.saw_modal));
        }
    }

    // QG-04 — control: a CLEAN machine on the quit path disables silently (no
    // modal, no gate to consult), same terminal state as QG-01.
    {
        Fixture fx(/*corrupt=*/false);
        if (!fx.ok) { check("QG-04", "fixture (clean+paused)", false); }
        else {
            ModalWatcher watch(QMessageBox::No);
            const bool ret = fx.mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
            watch.stop();
            check("QG-04", "app-quit path disables a CLEAN machine silently "
                  "(returns true, debugger off, no modal)",
                  ret && !fx.mgr->is_enabled() && !watch.saw_modal,
                  fmt("ret=%d is_enabled=%d saw_modal=%d (expect 1,0,0)",
                      ret, fx.mgr->is_enabled(), watch.saw_modal));
        }
    }

    // QG-05 — control: the INTERACTIVE disable of a CLEAN machine does NOT prompt
    // (the gate is a no-op when last_state_error() is empty). Proves QG-02's
    // modal came from the corruption, not from every disable.
    {
        Fixture fx(/*corrupt=*/false);
        if (!fx.ok) { check("QG-05", "fixture (clean+paused)", false); }
        else {
            ModalWatcher watch(QMessageBox::No);
            const bool ret = fx.mgr->set_enabled(false);   // default: prompt
            watch.stop();
            check("QG-05", "interactive disable of a CLEAN machine does not prompt "
                  "(returns true, debugger off, no modal)",
                  ret && !fx.mgr->is_enabled() && !watch.saw_modal,
                  fmt("ret=%d is_enabled=%d saw_modal=%d (expect 1,0,0)",
                      ret, fx.mgr->is_enabled(), watch.saw_modal));
        }
    }
}

int main(int argc, char** argv) {
    // DebuggerManager owns QWidgets, so a QApplication is required — but not a
    // display: force the offscreen QPA platform.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    test_quit_gate();
    std::printf("  Group: QG             — done\n");

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
