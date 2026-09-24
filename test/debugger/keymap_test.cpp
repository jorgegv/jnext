// GH #1 — redefinable debugger keys, against the RUNNING product.
//
// No VHDL oracle: host UI wiring, not emulated hardware. The oracle is what
// the real DebuggerWindow and the real MainWindow do with a real QKeyEvent.
//
// The grammar, the validation rules, the conflict resolution and the
// persistence are covered by app_config_test's DK group, which needs no
// widgets. THIS suite covers the half that only a running window can answer:
//
//   DKW  the debugger window's actions carry the keymap's combinations, its
//        toolbar captions quote the SAME combinations, and pressing a rebound
//        chord fires the action while the chord it replaced does not.
//   DKP  the Preferences tab: a capture that is illegal, or that another action
//        already holds, is REFUSED with a reason — and an untouched dialog
//        hands the bindings back unchanged, which is the GH #25 wipe hazard.
//   DKH  a binding that collides with a chord one of the HOST WINDOWS already
//        answers to. Reviewed and REJECTED as a defect in the first cut, on
//        the grounds that "conflicts are refused" was false for Ctrl+F5. It is
//        false, and the answer is not to refuse — see the group banner there.
//   DKM  the emulator window forwards the five execution keys FROM THE KEYMAP.
//        Before GH #1 that block switched on five hard-coded F-keys, so a
//        rebind half-applied: the new chord worked in the debugger while the
//        old one kept working — and kept being swallowed — in the main window.
//
// DELIVERY MECHANISM, and it is NOT the same for the two groups:
//
//   DKW's firing rows go through QTest::keyClick(QWindow*, ...), the real
//   platform key path, because a debugger shortcut is a QAction shortcut and
//   only that path reaches QShortcutMap. A plain sendEvent() would bypass the
//   shortcut map and prove nothing — the same reason debugger_accel_test uses
//   QTest for its AK rows.
//
//   DKM's rows go through QApplication::sendEvent(), because the emulator
//   window's forwarding lives in MainWindow::keyPressEvent() rather than in a
//   QAction — the same delivery debugger_menu_test's GH223-03 row uses.
//
// Every row was mutation-tested against the product; the list is in the
// handback.
//
// Both windows own QWidgets, so a QApplication is required — but not a
// display: the offscreen QPA platform is forced in main(), the same idiom as
// the other debugger suites.

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QWindow>

#include <cstdio>
#include <string>
#include <vector>

#include "core/emulator.h"
#include "debug/debug_keymap.h"
#include "debug/debug_keymap_qt.h"
#include "debug/debug_state.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "gui/host_chords.h"
#include "gui/main_window.h"
#include "gui/preferences_dialog.h"
#include "peripheral/nmi_source.h"
#include "gui/preferences_dialog.h"
#include "gui/shortcut_capture_button.h"

using namespace jnext::dbgkeys;

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    ++g_total;
    if (cond) {
        ++g_pass;
        std::printf("  PASS %s: %s\n", id, desc);
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s [%s]\n", id, desc, detail.c_str());
    }
}

void settle(int ms = 60) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/// Straight into MainWindow::keyPressEvent(), which is where the emulator
/// window's forwarding lives.
void send_key(QWidget* w, const Combo& c) {
    QKeyEvent ev(QEvent::KeyPress, to_qt_key(c.key), to_qt_mods(c.mods));
    QApplication::sendEvent(w, &ev);
}

/// Through the real platform key path — the one QShortcutMap listens on.
void press_shortcut(QWidget* w, const Combo& c) {
    if (QWindow* wh = w->windowHandle())
        QTest::keyClick(wh, static_cast<Qt::Key>(to_qt_key(c.key)), to_qt_mods(c.mods));
    settle(80);
}

EmulatorConfig next_config() {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    return cfg;
}

/// A headless Next emulator plus the real DebuggerWindow, brought up through
/// DebuggerManager exactly as the product does.
struct DebuggerFixture {
    Emulator         emu;
    QMainWindow      win;
    DebuggerManager* mgr = nullptr;
    DebuggerWindow*  dbg = nullptr;
    bool             ok  = false;

    DebuggerFixture() {
        if (!emu.init(next_config())) return;
        mgr = new DebuggerManager(&win, &emu, &win);   // parented -> auto-freed
        mgr->set_enabled(true);                        // creates + shows the window
        dbg = mgr->debugger_window_ptr();
        ok  = (dbg != nullptr);
        if (ok) {
            // show() + activateWindow() are what give the window a QWindow and
            // make QShortcutMap consider its actions at all.
            dbg->show();
            dbg->activateWindow();
        }
        settle(150);
    }
    ~DebuggerFixture() {
        if (mgr) mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
    }
};

/// A real MainWindow bound to a real emulator — which is what builds the
/// DebuggerManager and therefore the forwarding block under test.
struct MainWindowFixture {
    Emulator   emu;
    MainWindow win;
    bool       ok = false;

    MainWindowFixture() {
        if (!emu.init(next_config())) return;
        win.set_emulator(&emu);
        QApplication::processEvents();
        ok = win.debugger_manager() != nullptr;
    }
    ~MainWindowFixture() {
        if (win.debugger_manager())
            win.debugger_manager()->set_enabled(false, /*prompt_on_corrupt=*/false);
    }
};

Combo parsed(const char* text) {
    Combo c;
    std::string why;
    parse_combo(text, c, why);
    return c;
}

/// Every QAction the window owns, whatever menu (or none) it lives in.
std::vector<QAction*> all_actions(QWidget* w) {
    std::vector<QAction*> out;
    for (QAction* a : w->findChildren<QAction*>()) out.push_back(a);
    return out;
}

/// The action whose PRIMARY shortcut is `seq`, or nullptr. Text, not pointer
/// identity, because that is what the user actually presses.
QAction* action_with_shortcut(QWidget* w, const QKeySequence& seq) {
    if (seq.isEmpty()) return nullptr;
    for (QAction* a : all_actions(w))
        if (a->shortcut() == seq) return a;
    return nullptr;
}

/// Concatenated captions of every QPushButton on the window's toolbars.
QString toolbar_captions(QMainWindow* w) {
    QStringList out;
    for (QToolBar* tb : w->findChildren<QToolBar*>())
        for (QPushButton* b : tb->findChildren<QPushButton*>())
            out << b->text();
    return out.join(QStringLiteral(" | "));
}

// ── DKW: the debugger window ──────────────────────────────────────────────

void test_window_defaults() {
    DebuggerFixture fx;
    if (!fx.ok) {
        check("DKW-01", "the shipped defaults are on the real actions", false, "fixture failed");
        check("DKW-02", "the toolbar captions quote the shipped defaults", false, "fixture failed");
        check("DKW-03", "the three default-unbound actions carry no shortcut", false, "fixture failed");
        return;
    }

    // DKW-01 is the second half of the "no default changed" guard (the first
    // is app_config_test's DK-01, on the model): these are the sequences the
    // REAL actions carry after the real wiring has run.
    struct Want { const char* chord; const char* label; };
    const Want wanted[] = {
        { "F5",       "Run / Continue" },
        { "F9",       "Pause / Break"  },
        { "F6",       "Single Step"    },
        { "F7",       "Step Over"      },
        { "F8",       "Step Out"       },
        { "Shift+F7", "Step Back"      },
        { "Shift+F6", "Frame Back"     },
        { "F2",       "Enable Trace"   },
        { "F3",       "Export Trace"   },
    };
    bool all = true;
    std::string detail;
    for (const Want& w : wanted) {
        QAction* a = action_with_shortcut(fx.dbg, to_key_sequence(parsed(w.chord)));
        if (!a) { all = false; detail += std::string(w.chord) + " unbound; "; continue; }
        if (!a->text().remove(QLatin1Char('&')).contains(QString::fromLatin1(w.label))) {
            all = false;
            detail += std::string(w.chord) + " -> '"
                      + a->text().remove(QLatin1Char('&')).toStdString() + "'; ";
        }
    }
    check("DKW-01", "the shipped defaults are on the real actions", all, detail);

    const QString caps = toolbar_captions(fx.dbg);
    check("DKW-02", "the toolbar captions quote the shipped defaults",
          caps.contains(QStringLiteral("F5: Continue"))
              && caps.contains(QStringLiteral("F6: Single Step"))
              && caps.contains(QStringLiteral("F7: Step Over"))
              && caps.contains(QStringLiteral("F8: Step Out"))
              && caps.contains(QStringLiteral("F9: Break"))
              && caps.contains(QStringLiteral("F2: Trace"))
              && caps.contains(QStringLiteral("F3: Export Trace")),
          caps.toStdString());

    // run_to_cursor / run_to_eof / run_to_eosl ship unbound, so nothing may
    // answer to a key on their behalf. Guards against a default sneaking in.
    Keymap def;
    check("DKW-03", "the three default-unbound actions carry no shortcut",
          !def.combo(Action::RunToCursor).bound()
              && !def.combo(Action::RunToEof).bound()
              && !def.combo(Action::RunToEosl).bound(),
          "");
}

void test_window_rebind() {
    DebuggerFixture fx;
    if (!fx.ok) {
        for (const char* id : {"DKW-10", "DKW-11", "DKW-12", "DKW-13", "DKW-14"})
            check(id, "rebinding the debugger window", false, "fixture failed");
        return;
    }

    // The reporter's own layout: F10 Step Over, F11 Step Into.
    Keymap km;
    km.set(Action::StepOver, parsed("F10"));
    km.set(Action::StepInto, parsed("F11"));
    fx.dbg->set_keymap(km);
    QApplication::processEvents();

    QAction* over = action_with_shortcut(fx.dbg, to_key_sequence(parsed("F10")));
    check("DKW-10", "a rebound action carries the NEW sequence",
          over != nullptr
              && over->text().remove(QLatin1Char('&')).contains(QStringLiteral("Step Over")),
          over ? over->text().toStdString() : "nothing answers F10");

    check("DKW-11", "the sequence it replaced is left to nobody",
          action_with_shortcut(fx.dbg, to_key_sequence(parsed("F7"))) == nullptr,
          "F7 is still claimed");

    const QString caps = toolbar_captions(fx.dbg);
    check("DKW-12", "the toolbar caption follows the rebind",
          caps.contains(QStringLiteral("F10: Step Over"))
              && !caps.contains(QStringLiteral("F7: Step Over")),
          caps.toStdString());

    // The chord really fires it, through Qt's shortcut map — the same path a
    // real keypress takes. Counted on the ACTION, which is what the binding is.
    int fired_new = 0, fired_old = 0;
    QObject::connect(over, &QAction::triggered, over, [&fired_new]() { ++fired_new; });
    fx.dbg->activateWindow();

    // The step actions are DISABLED while the machine runs (update_actions()),
    // and a disabled QAction never fires — so both rows below would be vacuous
    // without pausing first. Paused through the manager, the way the product
    // does it. Re-paused before the second press because the first one steps
    // over, which resumes.
    QAction* step_into = action_with_shortcut(fx.dbg, to_key_sequence(parsed("F11")));
    int fired_into = 0;
    if (step_into)
        QObject::connect(step_into, &QAction::triggered, step_into,
                         [&fired_into]() { ++fired_into; });

    fx.mgr->on_pause();
    settle(80);
    const bool armed_new = over && over->isEnabled();
    press_shortcut(fx.dbg, parsed("F10"));
    check("DKW-13", "pressing the new chord fires the action",
          armed_new && fired_new == 1,
          armed_new ? ("fired " + std::to_string(fired_new) + " times")
                    : "the action was disabled, so the row proved nothing");

    fx.mgr->on_pause();
    settle(80);
    const bool armed_old = step_into && step_into->isEnabled();
    press_shortcut(fx.dbg, parsed("F6"));
    fired_old = fired_into;
    check("DKW-14", "pressing the chord it replaced fires nothing",
          armed_old && fired_old == 0,
          armed_old ? ("fired " + std::to_string(fired_old) + " times")
                    : "the action was disabled, so the row proved nothing");
}

void test_window_no_ambiguity() {
    DebuggerFixture fx;
    if (!fx.ok) {
        check("DKW-20", "a rebind leaves no two actions sharing a sequence", false, "fixture failed");
        check("DKW-21", "binding a previously unbound action installs a real shortcut", false, "fixture failed");
        return;
    }

    // Qt fires two identical sequences ROUND-ROBIN (GH #124), so a rebind that
    // left a duplicate would break both. The map cannot contain one (the UI
    // refuses it, build_keymap resolves it); this asserts the WINDOW does not
    // manufacture one either — e.g. by leaving a stale shortcut behind.
    Keymap km;
    km.set(Action::StepOver, parsed("F10"));
    km.set(Action::StepInto, parsed("F11"));
    km.set(Action::RunToCursor, parsed("Ctrl+F10"));
    fx.dbg->set_keymap(km);
    QApplication::processEvents();

    QStringList seen, dupes;
    for (QAction* a : all_actions(fx.dbg)) {
        for (const QKeySequence& s : a->shortcuts()) {
            if (s.isEmpty()) continue;
            const QString t = s.toString();
            if (seen.contains(t)) dupes << t; else seen << t;
        }
    }
    check("DKW-20", "a rebind leaves no two actions sharing a sequence",
          dupes.isEmpty(), dupes.join(QStringLiteral(",")).toStdString());

    check("DKW-21", "binding a previously unbound action installs a real shortcut",
          action_with_shortcut(fx.dbg, to_key_sequence(parsed("Ctrl+F10"))) != nullptr,
          seen.join(QStringLiteral(",")).toStdString());
}

// ── DKP: the Preferences tab ──────────────────────────────────────────────

/// Drive a real chord into a real capture button, the way a user does.
void capture_into(ShortcutCaptureButton* b, const Combo& c) {
    b->start_capture();
    QKeyEvent ev(QEvent::KeyPress, to_qt_key(c.key), to_qt_mods(c.mods));
    QApplication::sendEvent(b, &ev);
}

void test_preferences_tab() {
    AppConfigData before;   // every field at its default, keymap included
    PreferencesDialog dlg(before);
    const auto buttons = dlg.findChildren<ShortcutCaptureButton*>();

    if (buttons.size() != ACTION_COUNT) {
        for (const char* id : {"DKP-01", "DKP-02", "DKP-03", "DKP-04", "DKP-05"})
            check(id, "the Debugger Keys tab", false,
                  "found " + std::to_string(buttons.size()) + " capture buttons");
        return;
    }

    // The captured result is only observable through what the dialog would
    // hand MainWindow, so the rows read apply_requested's payload — the same
    // AppConfigData OK and Apply emit.
    AppConfigData emitted;
    int emits = 0;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested, &dlg,
                     [&emitted, &emits](const AppConfigData& cfg) {
                         emitted = cfg;
                         ++emits;
                     });
    auto press_apply = [&dlg]() {
        for (QPushButton* b : dlg.findChildren<QPushButton*>())
            if (b->text().remove(QLatin1Char('&')) == QStringLiteral("Apply"))
                b->click();
    };

    // DKP-05 first, and it is the one that matters most: a dialog nobody
    // touched must hand the bindings back IDENTICAL. collect() builds a fresh
    // AppConfigData, so a field it forgets is silently reset the moment the
    // user presses OK — which is exactly what happened to the ESP settings
    // (GH #25) before they had a page.
    press_apply();
    check("DKP-05", "an untouched dialog hands the bindings back unchanged",
          emits == 1 && emitted.debug_keys == before.debug_keys,
          "emits=" + std::to_string(emits));

    const int over = static_cast<int>(Action::StepOver);
    capture_into(buttons[over], parsed("F10"));
    press_apply();
    check("DKP-01", "a legal capture reaches the collected settings",
          render_combo(emitted.debug_keys.combo(Action::StepOver)) == "F10",
          render_combo(emitted.debug_keys.combo(Action::StepOver)));

    // F5 belongs to Run. Refused, not taken over: Qt fires two identical
    // sequences round-robin and breaks both (GH #124).
    capture_into(buttons[over], parsed("F5"));
    press_apply();
    check("DKP-02", "a capture another action already holds is refused",
          render_combo(emitted.debug_keys.combo(Action::StepOver)) == "F10"
              && render_combo(emitted.debug_keys.combo(Action::Run)) == "F5",
          render_combo(emitted.debug_keys.combo(Action::StepOver)) + "/"
              + render_combo(emitted.debug_keys.combo(Action::Run)));

    // A bare letter would be taken from whichever panel has focus.
    capture_into(buttons[over], parsed("K"));
    press_apply();
    check("DKP-03", "an illegal capture is refused",
          render_combo(emitted.debug_keys.combo(Action::StepOver)) == "F10",
          render_combo(emitted.debug_keys.combo(Action::StepOver)));

    for (QPushButton* b : dlg.findChildren<QPushButton*>())
        if (b->text().remove(QLatin1Char('&'))
                == QStringLiteral("Reset All to Defaults"))
            b->click();
    press_apply();
    check("DKP-04", "Reset All to Defaults restores every binding",
          emitted.debug_keys == before.debug_keys,
          render_combo(emitted.debug_keys.combo(Action::StepOver)));
}

// ── DKH: colliding with a chord a host window already owns ────────────────
//
// THE FINDING, AND WHY THE ANSWER IS NOT "REFUSE IT".
//
// Conflict detection covers the twelve debugger actions. It does NOT cover the
// chords the EMULATOR window already binds, and four of those survive every
// refusal rule. Measured, not listed by hand — DKH-01 harvests them:
//
//     Ctrl+F5  Record MPEG4 Video...      F4   Soft Reset
//     Ctrl+F6  Stop MPEG4 Recording       F11  Fullscreen
//
// Bind Run to Ctrl+F5 and, in the EMULATOR window, the recording starts and Run
// never fires. That is real, and the first cut of this feature said nothing
// about it at all.
//
// But refusing the class is the wrong fix, and the measurement is what says so.
// The two windows are separate top levels, so Qt::WindowShortcut matches
// against whichever is ACTIVE: there is no ambiguity, nothing is broken, and
// each window keeps its own binding (DKH-03/04/05). Refusing would make **F11
// unbindable — and F11 = Step Into is the binding GH #1's reporter asked for**,
// so the "fix" would defeat the issue.
//
// So: accept, and SAY what will happen (DKH-06). The defect was the silence.
//
// The one case that IS a true ambiguity is a chord claimed twice inside the
// SAME window, which Qt answers round-robin (GH #124). DKH-02 pins that the
// debugger window's non-keymap chords are exactly Copy/Select All and that both
// are already refused statically — so the moment anyone adds a debugger
// shortcut, that row fails and forces the reserve list to be extended.

void test_host_chord_enumeration() {
    MainWindowFixture fx;
    if (!fx.ok) {
        check("DKH-01", "the emulator window's bindable chords are exactly the four known",
              false, "fixture failed");
        return;
    }

    // Pinned deliberately, in the manner of the manifest's row counts: this IS
    // the enumeration the design doc talks about, so it is a value the project
    // states rather than a set that drifts. A new menu shortcut that a user
    // could also bind fails this row and forces the doc to be updated with it.
    QStringList got;
    for (const HostChord& h : harvest_host_chords(&fx.win))
        got << QStringLiteral("%1=%2")
                   .arg(QString::fromStdString(render_combo(h.combo)), h.label);
    got.sort();
    const QString want =
        QStringLiteral("Ctrl+F5=Record MPEG4 Video...|Ctrl+F6=Stop MPEG4 Recording|"
                       "F11=Fullscreen|F4=Soft Reset");
    check("DKH-01", "the emulator window's bindable chords are exactly the four known",
          got.join(QStringLiteral("|")) == want, got.join(QStringLiteral("|")).toStdString());
}

void test_debugger_window_has_no_foreign_chord() {
    DebuggerFixture fx;
    if (!fx.ok) {
        check("DKH-02", "no chord inside the debugger window is bindable but unclaimed",
              false, "fixture failed");
        return;
    }

    // Same window = same shortcut map = a REAL ambiguity, which Qt answers by
    // firing both round-robin (GH #124). There must therefore be nothing in
    // this window, outside the twelve, that a user could also bind. Today that
    // holds because the only two (the disassembly panel's Copy and Select All)
    // are refused by validate_combo(). This row is what makes the next debugger
    // shortcut somebody adds a loud failure instead of a silent ambiguity.
    const Keymap km = fx.dbg->keymap();
    QStringList offenders;
    for (QAction* a : all_actions(fx.dbg)) {
        for (const QKeySequence& seq : a->shortcuts()) {
            if (seq.isEmpty()) continue;
            Combo c;
            std::string why;
            if (!parse_combo(seq.toString().toStdString(), c, why)) continue;
            if (!validate_combo(c, why)) continue;     // a user could not bind it
            if (km.action_for(c)) continue;            // one of the twelve: fine
            offenders << QStringLiteral("%1=%2").arg(
                seq.toString(), a->text().remove(QLatin1Char('&')));
        }
    }
    check("DKH-02", "no chord inside the debugger window is bindable but unclaimed",
          offenders.isEmpty(), offenders.join(QStringLiteral("|")).toStdString());
}

/// The precedence itself, through QTest's real platform key path in BOTH
/// directions. sendEvent() cannot see this: it bypasses QShortcutMap, which is
/// the very thing doing the pre-empting — which is exactly why the first cut's
/// DKM rows, all sendEvent(), could not have caught the finding.
void test_host_chord_precedence() {
    MainWindowFixture fx;
    DebuggerManager* mgr = fx.ok ? fx.win.debugger_manager() : nullptr;
    if (!mgr) {
        for (const char* id : {"DKH-03", "DKH-04", "DKH-05"})
            check(id, "host-chord precedence", false, "fixture failed");
        return;
    }

    AppConfigData cfg = fx.win.app_config().data();
    cfg.debug_keys.set(Action::Run, parsed("Ctrl+F5"));        // vs Record
    cfg.debug_keys.set(Action::StepInto, parsed("F11"));       // vs Fullscreen
    fx.win.apply_preferences(cfg);
    QApplication::processEvents();

    mgr->set_enabled(true);
    settle(120);
    DebuggerWindow* dbg = mgr->debugger_window_ptr();
    if (!dbg) {
        for (const char* id : {"DKH-03", "DKH-04", "DKH-05"})
            check(id, "host-chord precedence", false, "no debugger window");
        return;
    }
    dbg->show();
    dbg->activateWindow();
    settle(150);

    auto named = [](QWidget* w, const QString& text) -> QAction* {
        for (QAction* a : w->findChildren<QAction*>())
            if (a->text().remove(QLatin1Char('&')).contains(text)) return a;
        return nullptr;
    };
    QAction* rec  = named(&fx.win, QStringLiteral("Record MPEG4 Video"));
    QAction* full = named(&fx.win, QStringLiteral("Fullscreen"));
    QAction* run  = action_with_shortcut(dbg, to_key_sequence(parsed("Ctrl+F5")));
    QAction* into = action_with_shortcut(dbg, to_key_sequence(parsed("F11")));
    if (!rec || !full || !run || !into) {
        for (const char* id : {"DKH-03", "DKH-04", "DKH-05"})
            check(id, "host-chord precedence", false, "an action was not found");
        return;
    }

    // Their real slots open a modal save dialog and toggle fullscreen; replace
    // them with counters so the rows measure DISPATCH and nothing else.
    rec->disconnect();
    full->disconnect();
    run->disconnect();
    into->disconnect();
    int nrec = 0, nfull = 0, nrun = 0, ninto = 0;
    QObject::connect(rec,  &QAction::triggered, rec,  [&nrec]()  { ++nrec;  });
    QObject::connect(full, &QAction::triggered, full, [&nfull]() { ++nfull; });
    QObject::connect(run,  &QAction::triggered, run,  [&nrun]()  { ++nrun;  });
    QObject::connect(into, &QAction::triggered, into, [&ninto]() { ++ninto; });

    // The step actions are disabled while the machine runs, so pause before
    // each press — otherwise a row would pass on a disabled action.
    dbg->activateWindow();
    settle(120);
    mgr->on_pause(); settle(80);
    const bool run_armed = run->isEnabled();
    press_shortcut(dbg, parsed("Ctrl+F5"));
    check("DKH-03", "in the DEBUGGER window the debugger binding wins",
          run_armed && nrun == 1 && nrec == 0,
          run_armed ? ("run=" + std::to_string(nrun) + " rec=" + std::to_string(nrec))
                    : "Run was disabled, so the row proved nothing");

    mgr->on_pause(); settle(80);
    const bool into_armed = into->isEnabled();
    press_shortcut(dbg, parsed("F11"));
    check("DKH-05", "F11 = Step Into really works in the debugger window",
          into_armed && ninto == 1 && nfull == 0,
          into_armed ? ("into=" + std::to_string(ninto) + " full=" + std::to_string(nfull))
                     : "Step Into was disabled, so the row proved nothing");

    // ... and the emulator window keeps its own, which is the half the review
    // measured and the half the user guide has to describe correctly.
    nrec = nfull = nrun = ninto = 0;
    fx.win.show();
    fx.win.activateWindow();
    settle(150);
    mgr->on_pause(); settle(80);
    press_shortcut(&fx.win, parsed("Ctrl+F5"));
    press_shortcut(&fx.win, parsed("F11"));
    check("DKH-04", "in the EMULATOR window its own menu shortcut wins",
          nrec == 1 && nrun == 0 && nfull == 1 && ninto == 0,
          "rec=" + std::to_string(nrec) + " run=" + std::to_string(nrun)
              + " full=" + std::to_string(nfull) + " into=" + std::to_string(ninto));
}

/// The part that was actually broken: the dialog said nothing.
void test_host_chord_is_accepted_with_a_warning() {
    MainWindowFixture fx;
    if (!fx.ok) {
        check("DKH-06", "a host-claimed chord is accepted AND named in the message",
              false, "fixture failed");
        check("DKH-07", "an unclaimed chord gets the plain message", false, "fixture failed");
        return;
    }

    AppConfigData before;
    PreferencesDialog dlg(before, nullptr, {}, harvest_host_chords(&fx.win));
    const auto buttons = dlg.findChildren<ShortcutCaptureButton*>();
    QLabel* msg_label = dlg.findChild<QLabel*>(QStringLiteral("debug_keys_message"));
    if (buttons.size() != ACTION_COUNT || !msg_label) {
        check("DKH-06", "a host-claimed chord is accepted AND named in the message",
              false, "capture buttons not found");
        check("DKH-07", "an unclaimed chord gets the plain message", false, "");
        return;
    }
    auto message = [msg_label]() { return msg_label->text(); };

    capture_into(buttons[static_cast<int>(Action::Run)], parsed("Ctrl+F5"));
    AppConfigData got;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested, &dlg,
                     [&got](const AppConfigData& c) { got = c; });
    for (QPushButton* b : dlg.findChildren<QPushButton*>())
        if (b->text().remove(QLatin1Char('&')) == QStringLiteral("Apply")) b->click();

    const QString msg = message();
    check("DKH-06", "a host-claimed chord is accepted AND named in the message",
          render_combo(got.debug_keys.combo(Action::Run)) == "Ctrl+F5"
              && msg.contains(QStringLiteral("Record MPEG4 Video"))
              && msg.contains(QStringLiteral("emulator window")),
          render_combo(got.debug_keys.combo(Action::Run)) + " / " + msg.toStdString());

    capture_into(buttons[static_cast<int>(Action::Run)], parsed("Ctrl+F12"));
    const QString plain = message();
    check("DKH-07", "an unclaimed chord gets the plain message",
          plain.contains(QStringLiteral("Ctrl+F12"))
              && !plain.contains(QStringLiteral("emulator window")),
          plain.toStdString());
}

/// DKH-08 — the WIRING, not the component.
///
/// DKH-06 builds a PreferencesDialog by hand and hands it the harvest, so it
/// proves the dialog warns when it is TOLD about a chord. It says nothing about
/// whether the product ever tells it: deleting `harvest_host_chords(this)` from
/// MainWindow::on_open_preferences() — exactly the pre-review state — left
/// every row green. That is the same shape of hole as the defect this group
/// exists for, so it gets an end-to-end row of its own.
///
/// It goes through the REAL Settings > Preferences... menu action and inspects
/// the REAL modal dialog from inside its own event loop, which is the only way
/// to see an exec()-ing dialog without blocking forever.
void test_preferences_menu_passes_the_host_chords() {
    MainWindowFixture fx;
    if (!fx.ok) {
        check("DKH-08", "the real Preferences menu hands the dialog this window's chords",
              false, "fixture failed");
        return;
    }

    QAction* prefs = nullptr;
    for (QAction* a : fx.win.findChildren<QAction*>())
        if (a->text().remove(QLatin1Char('&')).startsWith(QStringLiteral("Preferences")))
            prefs = a;
    if (!prefs) {
        check("DKH-08", "the real Preferences menu hands the dialog this window's chords",
              false, "no Preferences action");
        return;
    }

    QString seen;
    bool found_dialog = false;
    QTimer::singleShot(0, &fx.win, [&seen, &found_dialog]() {
        auto* dlg = qobject_cast<PreferencesDialog*>(QApplication::activeModalWidget());
        if (dlg) {
            found_dialog = true;
            const auto buttons = dlg->findChildren<ShortcutCaptureButton*>();
            QLabel* msg = dlg->findChild<QLabel*>(QStringLiteral("debug_keys_message"));
            if (buttons.size() == ACTION_COUNT && msg) {
                capture_into(buttons[static_cast<int>(Action::Run)], parsed("Ctrl+F5"));
                seen = msg->text();
            }
            dlg->reject();   // let exec() return, and discard the edit
        }
    });
    prefs->trigger();        // the production path: menu -> on_open_preferences -> exec()

    check("DKH-08", "the real Preferences menu hands the dialog this window's chords",
          found_dialog && seen.contains(QStringLiteral("Record MPEG4 Video"))
              && seen.contains(QStringLiteral("emulator window")),
          found_dialog ? seen.toStdString() : "the modal dialog was never seen");
}

// ── DKM: the emulator window's forwarding ─────────────────────────────────

void test_main_window_forwarding() {
    MainWindowFixture fx;
    DebuggerManager* mgr = fx.ok ? fx.win.debugger_manager() : nullptr;
    if (!mgr) {
        for (const char* id : {"DKM-01", "DKM-02", "DKM-03", "DKM-04"})
            check(id, "the emulator window forwards from the keymap", false, "fixture failed");
        return;
    }
    mgr->set_enabled(true);

    auto run_machine = [&fx]() {
        if (fx.emu.debug_state().paused()) fx.emu.debug_state().resume();
    };

    // Control: the SHIPPED binding still works through this window. Without
    // it, every row below would pass against a build where the forwarding
    // block had simply been deleted.
    run_machine();
    fx.win.activateWindow();
    send_key(&fx.win, parsed("F9"));
    check("DKM-01", "the default Pause chord still pauses from the emulator window",
          fx.emu.debug_state().paused(), "not paused");

    // Modifiers are matched EXACTLY now. This block used to switch on the key
    // alone, so Shift+F9 paused too — which is what made Shift+F7 unusable for
    // Step Back.
    run_machine();
    const bool mf_before = fx.emu.nmi_source().mf_button();
    send_key(&fx.win, parsed("Shift+F9"));
    check("DKM-02", "a modified variant of a bound chord is NOT forwarded",
          !fx.emu.debug_state().paused(), "Shift+F9 paused the machine");

    // DKM-02 on its own only says the debugger did not take it. This says
    // where it WENT: straight on to the emulator window's own F9 hotkey, the
    // Multiface NMI, exactly as with the debugger closed. It is the half of
    // the exact-modifier change the user guide describes, and describing it
    // without measuring it is how a doc goes stale.
    check("DKM-05", "the unforwarded variant reaches the emulator's own F9 hotkey",
          !mf_before && fx.emu.nmi_source().mf_button(),
          mf_before ? "the MF button line was already raised"
                    : "the Multiface NMI hotkey did not fire");

    // Rebind, and the forwarding must move with it.
    AppConfigData cfg = fx.win.app_config().data();
    cfg.debug_keys.set(Action::Pause, parsed("Ctrl+F12"));
    fx.win.apply_preferences(cfg);
    QApplication::processEvents();

    run_machine();
    send_key(&fx.win, parsed("Ctrl+F12"));
    check("DKM-03", "the emulator window forwards the REBOUND chord",
          fx.emu.debug_state().paused(), "Ctrl+F12 did not pause the machine");

    run_machine();
    send_key(&fx.win, parsed("F9"));
    check("DKM-04", "the emulator window no longer forwards the old chord",
          !fx.emu.debug_state().paused(), "F9 still pauses after the rebind");
}

void test_main_window_pushes_keymap() {
    MainWindowFixture fx;
    DebuggerManager* mgr = fx.ok ? fx.win.debugger_manager() : nullptr;
    if (!mgr) {
        check("DKM-10", "Preferences pushes the new bindings into the debugger window",
              false, "fixture failed");
        return;
    }
    mgr->set_enabled(true);
    settle(80);
    DebuggerWindow* dbg = mgr->debugger_window_ptr();
    if (!dbg) {
        check("DKM-10", "Preferences pushes the new bindings into the debugger window",
              false, "no debugger window");
        return;
    }

    // The two windows must never disagree: one keymap, pushed on Apply.
    AppConfigData cfg = fx.win.app_config().data();
    cfg.debug_keys.set(Action::StepOver, parsed("Ctrl+F11"));
    fx.win.apply_preferences(cfg);
    QApplication::processEvents();

    QAction* over = action_with_shortcut(dbg, to_key_sequence(parsed("Ctrl+F11")));
    check("DKM-10", "Preferences pushes the new bindings into the debugger window",
          over != nullptr
              && over->text().remove(QLatin1Char('&')).contains(QStringLiteral("Step Over")),
          over ? over->text().toStdString() : "nothing answers Ctrl+F11");
}

/// DKM-11 — the order DKM-10 does NOT cover, and the one a user actually hits.
///
/// The debugger window is created LAZILY, the first time the debugger is
/// enabled. A rebind made before that has nowhere to go: pushing it straight
/// at `debugger_window_ptr()` is a no-op against a null pointer, and the
/// window is then built on the compiled-in defaults. This shipped, and was
/// found by driving the real GUI rather than by any test — DKM-10 enables the
/// debugger FIRST, so it cannot see it. The keymap now lives on
/// DebuggerManager and is applied inside ensure_window().
void test_keymap_survives_a_late_window() {
    MainWindowFixture fx;
    DebuggerManager* mgr = fx.ok ? fx.win.debugger_manager() : nullptr;
    if (!mgr) {
        check("DKM-11", "a rebind made before the debugger was ever opened survives",
              false, "fixture failed");
        check("DKM-12", "and the toolbar caption of that late window follows it",
              false, "fixture failed");
        return;
    }
    // Deliberately NOT enabled yet: there is no debugger window at this point.
    AppConfigData cfg = fx.win.app_config().data();
    cfg.debug_keys.set(Action::StepOver, parsed("F10"));
    fx.win.apply_preferences(cfg);
    QApplication::processEvents();

    mgr->set_enabled(true);          // the window is built HERE
    settle(80);
    DebuggerWindow* dbg = mgr->debugger_window_ptr();
    if (!dbg) {
        check("DKM-11", "a rebind made before the debugger was ever opened survives",
              false, "no debugger window");
        check("DKM-12", "and the toolbar caption of that late window follows it",
              false, "no debugger window");
        return;
    }

    QAction* over = action_with_shortcut(dbg, to_key_sequence(parsed("F10")));
    check("DKM-11", "a rebind made before the debugger was ever opened survives",
          over != nullptr
              && over->text().remove(QLatin1Char('&')).contains(QStringLiteral("Step Over"))
              && action_with_shortcut(dbg, to_key_sequence(parsed("F7"))) == nullptr,
          over ? over->text().toStdString() : "nothing answers F10");

    const QString caps = toolbar_captions(dbg);
    check("DKM-12", "and the toolbar caption of that late window follows it",
          caps.contains(QStringLiteral("F10: Step Over"))
              && !caps.contains(QStringLiteral("F7: Step Over")),
          caps.toStdString());
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");

    // The debugger window restores and saves a geometry, and MainWindow reads
    // and writes the real config. Both belong to the user, not to this test.
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());

    QApplication app(argc, argv);

    test_window_defaults();
    test_window_rebind();
    test_window_no_ambiguity();
    test_preferences_tab();
    test_host_chord_enumeration();
    test_debugger_window_has_no_foreign_chord();
    test_host_chord_precedence();
    test_host_chord_is_accepted_with_a_warning();
    test_preferences_menu_passes_the_host_chords();
    test_main_window_forwarding();
    test_main_window_pushes_keymap();
    test_keymap_survives_a_late_window();

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
