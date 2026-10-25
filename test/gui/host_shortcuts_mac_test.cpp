// GH #403 - the same macOS code as host_shortcuts_test, but exercised through
// its DEFAULT arguments, which resolve to this build's platform.
//
// This translation unit is compiled with JNEXT_TEST_FORCE_CTRL_IS_COMMAND, so
// kQtCtrlIsCommand is true here exactly as it is in a Q_OS_MACOS build. The
// other suite can only prove the explicit-parameter forms on a Linux host: a
// default argument flipped to `false` (the Mac build then ships the Alt set,
// ungated, with Command mapped to Symbol Shift) is invisible to it.
//
// No VHDL oracle (host UI); the oracle is the owner's decision (2026-10-25) and
// tofro's condition: Command is the host modifier, the physical Control key is
// Symbol Shift and must stay one, and an unbound Command chord must not reach
// the guest.
//
// Run: ./build/test/host_shortcuts_mac_test

#include "gui/host_shortcuts.h"

#include <QApplication>

#include <cstdio>
#include <string>
#include "../row_id.h"

static_assert(kQtCtrlIsCommand, "this suite must be built with the macOS constant");

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

QString portable(const QKeySequence& s) { return s.toString(QKeySequence::PortableText); }

}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    using namespace host_shortcuts;

    std::printf("GH #403 - macOS defaults (kQtCtrlIsCommand forced)\n");
    std::printf("==================================================\n");

    {
        QStringList got;
        for (Action a : kAllActions) got << portable(sequence(a));
        const QStringList want = {"Ctrl+O", "Ctrl+L", "Ctrl+S", "Ctrl+K", "Ctrl+Shift+S",
                                  "Ctrl+Shift+F5", "Ctrl+Shift+F9", "Ctrl+Q", "Ctrl+R",
                                  "Ctrl+U", "Ctrl+T", "Ctrl+D", "Ctrl+,"};
        check("HS403M-01", "default platform: the host set is Command (Qt::CTRL), Quit Command+Q, "
                           "Preferences Command+,",
              got == want, got.join(' ').toStdString());
    }
    check("HS403M-02", "default platform: script keys Command+1..8, record on the physical Control key "
                       "(Meta+F5/F6), no Alt+F4",
          portable(script_sequence(1)) == "Ctrl+1" && portable(script_sequence(8)) == "Ctrl+8" &&
              portable(record_sequence(true)) == "Meta+F5" && portable(record_sequence(false)) == "Meta+F6" &&
              quit_alternate().isEmpty() && quit_shortcuts().size() == 1 && modifier() == Qt::CTRL);
    check("HS403M-03", "default platform: Command (Key_Control) goes to the Router as GUI and is NOT "
                       "Symbol Shift; the physical Control key (Key_Meta) is Symbol Shift",
          modifier_scancode(Qt::Key_Control) == SDL_SCANCODE_LGUI &&
              modifier_scancode(Qt::Key_Meta) == SDL_SCANCODE_LCTRL &&
              modifier_scancode(Qt::Key_Shift) == SDL_SCANCODE_LSHIFT &&
              modifier_scancode(Qt::Key_Alt) == SDL_SCANCODE_LALT);
    {
        CommandChordGate g;   // the default: on
        const bool cmd = g.swallow_press(Qt::Key_C, Qt::ControlModifier);
        const bool cmd_rel = g.swallow_release(Qt::Key_C);
        // tofro's condition: Symbol Shift (the physical Control key) chords reach the guest.
        const bool ctrl = g.swallow_press(Qt::Key_O, Qt::MetaModifier);
        const bool ctrl_rel = g.swallow_release(Qt::Key_O);
        check("HS403M-04", "default platform: the gate is on - Command+C is swallowed (press and release), "
                           "physical Control+O (a Symbol Shift sequence) is not",
              cmd && cmd_rel && !ctrl && !ctrl_rel);
    }
    {
        using D = FKeyDecision;
        check("HS403M-05", "default platform: Command+F4 quits, Option+F1/F4 do nothing, physical "
                           "Control+F1/F4 reset nothing, plain F1/F4 reset",
              f4_decision(Qt::ControlModifier) == D::Quit && f1_decision(Qt::AltModifier) == D::Swallow &&
                  f4_decision(Qt::AltModifier) == D::Swallow && f1_decision(Qt::MetaModifier) == D::Pass &&
                  f4_decision(Qt::MetaModifier) == D::Pass && f1_decision(Qt::NoModifier) == D::Reset &&
                  f4_decision(Qt::NoModifier) == D::Reset);
    }
    check("HS403M-06", "default platform: the texts name the Command forms, never Alt, and the Quit label "
                       "has no tab",
          !pause_tooltip().contains("Alt") && !quick_screenshot_note().contains("Alt") &&
              !debugger_tooltip().contains("Alt") && !script_capture_tooltip().contains("Alt") &&
              !script_recording_text("x").contains("Alt") &&
              pause_tooltip().contains(text(Action::Pause)) &&
              quit_text("&Quit", quit_shortcuts()) == "&Quit");

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
