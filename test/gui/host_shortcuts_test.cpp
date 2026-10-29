// GH #403 - macOS: the host shortcuts ride Command, Control is Symbol Shift.
//
// No VHDL oracle (host UI). The oracle is the issue and its owner decision
// (2026-10-25):
//   * on macOS every host shortcut that is Alt+X elsewhere is Command+X; Quit is
//     Command+Q, Preferences Command+, ; the Alt+F4 second shortcut is dropped;
//   * the host chords never use the physical Control key (Qt's Meta modifier on
//     macOS): it is the guest's Symbol Shift (keymaps.vhd:84, GH #115) and a
//     host chord on it would eat a Symbol Shift sequence NextBASIC needs;
//   * Command itself is not a guest key; the physical Control key is Symbol
//     Shift; Option stays the guest's Alt;
//   * no chord collides with a macOS system chord (hide, minimise, close window,
//     window cycling, Spotlight);
//   * every other platform is byte-for-byte what GH #115 shipped (Alt).
// Qt documents Command as Qt::Key_Control / ControlModifier / Qt::CTRL on macOS
// (qt_ctrl_is_command.h), which is why the macOS set is written with Qt::CTRL.
//
// The macOS branch is a PARAMETER of the real code, so it runs here on any OS.
//
// Run: ./build/test/host_shortcuts_test

#include "gui/host_shortcuts.h"

#include "debug/debug_keymap.h"

#include <QApplication>
#include <QFile>
#include <QStringList>

#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "../row_id.h"

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

using host_shortcuts::Action;
using host_shortcuts::kAllActions;

QString portable(const QKeySequence& s) { return s.toString(QKeySequence::PortableText); }

// Every sequence the host set binds, for a platform: the actions' primaries,
// Quit's alternate (if any) and the script keys 1..8.
std::vector<QKeySequence> every_sequence(bool mac)
{
    std::vector<QKeySequence> v;
    for (Action a : kAllActions) v.push_back(host_shortcuts::sequence(a, mac));
    if (!host_shortcuts::quit_alternate(mac).isEmpty()) v.push_back(host_shortcuts::quit_alternate(mac));
    for (int n = 1; n <= 8; ++n) v.push_back(host_shortcuts::script_sequence(n, mac));
    return v;
}

// The first chord of a sequence as modifier flags | key code, whichever Qt major
// builds this (Qt5 returns an int, Qt6 a QKeyCombination).
int combined(const QKeySequence& s)
{
    if (s.isEmpty()) return 0;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return s[0].toCombined();
#else
    return s[0];
#endif
}

struct Mods {
    int v;
    bool testFlag(Qt::KeyboardModifier m) const { return (v & static_cast<int>(m)) != 0; }
};

Mods mods_of(const QKeySequence& s) { return Mods{combined(s) & static_cast<int>(Qt::KeyboardModifierMask)}; }
int key_of(const QKeySequence& s) { return combined(s) & ~static_cast<int>(Qt::KeyboardModifierMask); }

QString read_all(const QString& rel)
{
    QFile f(QStringLiteral(JNEXT_SRC_DIR "/") + rel);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

// The file's lines that are code: a line whose first non-blank characters are
// "//" is a comment, and the rows below look at code only.
QStringList code_lines(const QString& text)
{
    QStringList out;
    for (const QString& l : text.split(QLatin1Char('\n')))
        if (!l.trimmed().startsWith(QLatin1String("//"))) out << l;
    return out;
}

int count_in_code(const QString& text, const QString& needle)
{
    int n = 0;
    for (const QString& l : code_lines(text)) n += l.count(needle);
    return n;
}

}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("GH #403 - macOS host shortcuts on Command\n");
    std::printf("=========================================\n");

    // --- the set, per platform --------------------------------------------
    {
        QStringList mac, other;
        for (Action a : kAllActions) {
            mac << portable(host_shortcuts::sequence(a, true));
            other << portable(host_shortcuts::sequence(a, false));
        }
        // Order of kAllActions: Load NEX, Reload Last, Save Screenshot, Quick
        // Screenshot, Save Snapshot, Quick Save, Quick Load, Quit, Power Reset,
        // Pause, Tape Open, Debugger, Preferences. Qt's portable text spells
        // Qt::CTRL "Ctrl" (the NATIVE text is the Command symbol on macOS).
        const QStringList want_mac = {"Ctrl+O", "Ctrl+L", "Ctrl+S", "Ctrl+K", "Ctrl+Shift+S",
                                      "Ctrl+Shift+F5", "Ctrl+Shift+F9", "Ctrl+Q", "Ctrl+R",
                                      "Ctrl+U", "Ctrl+T", "Ctrl+D", "Ctrl+,"};
        const QStringList want_other = {"Alt+O", "Alt+L", "Alt+S", "Alt+K", "Alt+Shift+S",
                                        "Alt+Shift+F5", "Alt+Shift+F9", "Alt+Q", "Alt+R",
                                        "Alt+U", "Alt+T", "Alt+D", "Alt+P"};
        check("HS403-01", "macOS: the host set is Command (Qt::CTRL) + the Alt letters, Quit is Command+Q and "
                          "Preferences is Command+,",
              mac == want_mac, mac.join(' ').toStdString());
        check("HS403-02", "every other platform: the host set is exactly GH #115's Alt set, unchanged",
              other == want_other, other.join(' ').toStdString());
        check("HS403-03", "the default platform argument is this build's (kQtCtrlIsCommand)",
              host_shortcuts::sequence(Action::Quit) ==
                  host_shortcuts::sequence(Action::Quit, kQtCtrlIsCommand) &&
              host_shortcuts::modifier() == host_shortcuts::modifier(kQtCtrlIsCommand));
    }

    // --- nothing on macOS can eat a Symbol Shift sequence ------------------
    {
        std::string bad;
        for (const QKeySequence& s : every_sequence(true)) {
            if (mods_of(s).testFlag(Qt::MetaModifier)) bad += portable(s).toStdString() + " ";
        }
        check("HS403-04", "macOS: no host chord uses the Meta modifier (the physical Control key = Symbol "
                          "Shift), so none can eat a Symbol Shift sequence",
              bad.empty(), bad);
    }
    {
        std::string bad;
        for (const QKeySequence& s : every_sequence(true)) {
            if (!mods_of(s).testFlag(Qt::ControlModifier)) bad += portable(s).toStdString() + " ";
            if (mods_of(s).testFlag(Qt::AltModifier)) bad += portable(s).toStdString() + "(alt) ";
        }
        check("HS403-05", "macOS: every host chord carries Command (Qt's ControlModifier) and none uses "
                          "Option, which stays the guest's Alt (EDIT/GRAPH/CAPS LOCK)",
              bad.empty(), bad);
    }
    {
        std::string bad;
        for (const QKeySequence& s : every_sequence(false)) {
            if (mods_of(s).testFlag(Qt::ControlModifier) || mods_of(s).testFlag(Qt::MetaModifier))
                bad += portable(s).toStdString() + " ";
            if (!mods_of(s).testFlag(Qt::AltModifier)) bad += portable(s).toStdString() + "(no alt) ";
        }
        check("HS403-06", "every other platform: every host chord is Alt and none uses Ctrl (Symbol Shift), "
                          "the GH #115 rule",
              bad.empty(), bad);
    }

    // --- no collisions ------------------------------------------------------
    for (int mac = 0; mac < 2; ++mac) {
        std::set<QString> seen;
        std::string dup;
        for (const QKeySequence& s : every_sequence(mac != 0)) {
            if (!seen.insert(portable(s)).second) dup += portable(s).toStdString() + " ";
        }
        // The two Command+function-key shortcuts the main window already
        // binds (File > Record MPEG4 Video: Ctrl+F5 / Ctrl+F6) and the full
        // screen chord are part of the same namespace on macOS.
        const QKeySequence extra[] = {host_shortcuts::record_sequence(true, mac != 0),
                                      host_shortcuts::record_sequence(false, mac != 0),
                                      QKeySequence(Qt::CTRL | Qt::META | Qt::Key_F),
                                      QKeySequence(Qt::Key_F11), QKeySequence(Qt::Key_F4),
                                      QKeySequence(Qt::Key_Pause)};
        for (const QKeySequence& s : extra) {
            if (!seen.insert(portable(s)).second) dup += portable(s).toStdString() + "(window) ";
        }
        if (mac)
            check("HS403-07", "macOS: the host set, the script keys and the window's other shortcuts "
                              "are pairwise distinct (an ambiguous Qt shortcut breaks both)",
                  dup.empty(), dup);
        else
            check("HS403-08", "every other platform: the same holds for the Alt set",
                  dup.empty(), dup);
    }
    {
        // Hide (H), minimise (M), close window (W), cycle windows (`), Quit is
        // meant (Q). Tab and Space (Command+Tab, Command+Space) are not letters
        // any chord here uses either.
        const std::set<int> reserved = {Qt::Key_H, Qt::Key_M, Qt::Key_W, Qt::Key_QuoteLeft,
                                        Qt::Key_Tab, Qt::Key_Space};
        std::string bad;
        for (const QKeySequence& s : every_sequence(true)) {
            if (s.isEmpty()) continue;
            if (reserved.count(key_of(s))) bad += portable(s).toStdString() + " ";
        }
        check("HS403-09", "macOS: no host chord is a system chord (Command+H hide, +M minimise, +W close, "
                          "+` window cycle, +Tab, +Space)",
              bad.empty(), bad);
    }

    // --- Quit, Preferences, script keys -----------------------------------
    check("HS403-10", "Quit's second shortcut is Alt+F4 elsewhere and none on macOS (Command+Q only)",
          portable(host_shortcuts::quit_alternate(false)) == "Alt+F4" &&
              host_shortcuts::quit_alternate(true).isEmpty());
    {
        bool ok = true;
        std::string d;
        for (int n = 1; n <= 8; ++n) {
            const QString m = portable(host_shortcuts::script_sequence(n, true));
            const QString o = portable(host_shortcuts::script_sequence(n, false));
            if (m != QString("Ctrl+%1").arg(n) || o != QString("Alt+%1").arg(n)) {
                ok = false;
                d += (m + "/" + o + " ").toStdString();
            }
        }
        check("HS403-11", "the script keys 1..8 are Command+n on macOS and Alt+n elsewhere", ok, d);
    }
    check("HS403-12", "user-facing texts name the chord the platform spells: Alt+K and Alt+8 here, and on "
                      "macOS the Command form (never the Alt one)",
          host_shortcuts::text(Action::QuickScreenshot, false) == "Alt+K" &&
              host_shortcuts::script_text(8, false) == "Alt+8" &&
              !host_shortcuts::text(Action::QuickScreenshot, true).contains("Alt") &&
              !host_shortcuts::script_text(8, true).contains("Alt") &&
              host_shortcuts::script_text(8, true).endsWith("8"));

    check("HS403-31", "Record start/stop: Ctrl+F5/F6 elsewhere; on macOS the physical Control key "
                      "(Meta+F5/F6, not Command: Command+F5 is VoiceOver's), and not a Symbol Shift "
                      "sequence the guest needs since F5/F6 are no Spectrum keys",
          portable(host_shortcuts::record_sequence(true, true)) == "Meta+F5" &&
              portable(host_shortcuts::record_sequence(false, true)) == "Meta+F6" &&
              portable(host_shortcuts::record_sequence(true, false)) == "Ctrl+F5" &&
              portable(host_shortcuts::record_sequence(false, false)) == "Ctrl+F6");

    // --- the guest side: which Qt key is which guest modifier -------------
    {
        const SDL_Scancode ctl_m = host_shortcuts::modifier_scancode(Qt::Key_Control, true);
        const SDL_Scancode meta_m = host_shortcuts::modifier_scancode(Qt::Key_Meta, true);
        check("HS403-13", "macOS: the Command key (Key_Control) is NOT Symbol Shift - it goes to the key "
                          "Router as GUI, which the guest ignores - and the physical Control key "
                          "(Key_Meta) is Symbol Shift (LCTRL)",
              ctl_m == SDL_SCANCODE_LGUI && ctl_m != SDL_SCANCODE_LCTRL && meta_m == SDL_SCANCODE_LCTRL);
        check("HS403-14", "every other platform: Control is Symbol Shift (LCTRL) and the Meta/Windows key is "
                          "unmapped, as before",
              host_shortcuts::modifier_scancode(Qt::Key_Control, false) == SDL_SCANCODE_LCTRL &&
                  host_shortcuts::modifier_scancode(Qt::Key_Meta, false) == SDL_SCANCODE_UNKNOWN);
        check("HS403-15", "Shift and Alt are the same on both platforms (Caps Shift, and the guest's Alt = "
                          "Option on macOS); a non-modifier key is not a modifier",
              host_shortcuts::modifier_scancode(Qt::Key_Shift, true) == SDL_SCANCODE_LSHIFT &&
                  host_shortcuts::modifier_scancode(Qt::Key_Shift, false) == SDL_SCANCODE_LSHIFT &&
                  host_shortcuts::modifier_scancode(Qt::Key_Alt, true) == SDL_SCANCODE_LALT &&
                  host_shortcuts::modifier_scancode(Qt::Key_Alt, false) == SDL_SCANCODE_LALT &&
                  host_shortcuts::modifier_scancode(Qt::Key_A, true) == SDL_SCANCODE_UNKNOWN);
    }

    // --- F1 / F4 decisions ----------------------------------------------------
    {
        using D = host_shortcuts::FKeyDecision;
        using host_shortcuts::f1_decision;
        using host_shortcuts::f4_decision;
        const Qt::KeyboardModifiers none, alt = Qt::AltModifier, ctl = Qt::ControlModifier,
                                    shift = Qt::ShiftModifier, meta = Qt::MetaModifier;
        // Elsewhere (the GH #380 behaviour, unchanged): Alt+F4 quits whatever else is held;
        // F1 resets only with no Shift/Ctrl/Alt; F4 resets with no Shift/Ctrl.
        check("HS403-16", "elsewhere: F4 with Alt quits (any company), plain F4 / F1 reset, Shift or Ctrl "
                          "pass through, Alt+F1 does not reset, and the Meta key changes nothing",
              f4_decision(alt, false) == D::Quit && f4_decision(alt | shift | ctl, false) == D::Quit &&
                  f4_decision(none, false) == D::Reset && f4_decision(meta, false) == D::Reset &&
                  f4_decision(shift, false) == D::Pass && f4_decision(ctl, false) == D::Pass &&
                  f1_decision(none, false) == D::Reset && f1_decision(meta, false) == D::Reset &&
                  f1_decision(alt, false) == D::Pass && f1_decision(shift, false) == D::Pass &&
                  f1_decision(ctl, false) == D::Pass);
        // macOS: Command (ControlModifier) is what Alt is elsewhere; Option does nothing at all;
        // the physical Control key (MetaModifier) behaves as Ctrl does elsewhere.
        check("HS403-32", "macOS: Command+F4 quits (any company), Command+F1 resets nothing, Option+F1 / "
                          "Option+F4 do nothing (swallowed, no reset, no quit), physical Control+F1 / "
                          "Control+F4 reset nothing (as Ctrl on Linux), plain F1 / F4 reset",
              f4_decision(ctl, true) == D::Quit && f4_decision(ctl | alt | meta | shift, true) == D::Quit &&
                  f1_decision(ctl, true) == D::Pass &&
                  f1_decision(alt, true) == D::Swallow && f4_decision(alt, true) == D::Swallow &&
                  f1_decision(meta, true) == D::Pass && f4_decision(meta, true) == D::Pass &&
                  f1_decision(none, true) == D::Reset && f4_decision(none, true) == D::Reset &&
                  f1_decision(shift, true) == D::Pass && f4_decision(shift, true) == D::Pass);
    }

    // --- GH #404's Control+Command+F still works under the Command gate ---------
    {
        host_shortcuts::CommandChordGate g(true);
        const Qt::KeyboardModifiers both = Qt::ControlModifier | Qt::MetaModifier;
        const QString mw = read_all("src/gui/main_window.cpp");
        const int at_chord = mw.indexOf("host_shortcuts::is_fullscreen_chord(key, modifiers)");
        const int at_guest = mw.indexOf("handle_key(event, true);");
        check("HS403-38", "macOS Control+Command+F (GH #404): recognised only with both modifiers; MainWindow "
                          "takes it in keyPressEvent BEFORE the guest path, so F never reaches the guest "
                          "(the gate would swallow it anyway); the physical Control key alone is Symbol Shift "
                          "and stays a guest key",
              mods_of(host_shortcuts::fullscreen_sequence()).testFlag(Qt::ControlModifier) &&
                  mods_of(host_shortcuts::fullscreen_sequence()).testFlag(Qt::MetaModifier) &&
                  key_of(host_shortcuts::fullscreen_sequence()) == Qt::Key_F &&
                  host_shortcuts::is_fullscreen_chord(Qt::Key_F, both) &&
                  !host_shortcuts::is_fullscreen_chord(Qt::Key_F, Qt::ControlModifier) &&
                  !host_shortcuts::is_fullscreen_chord(Qt::Key_F, Qt::MetaModifier) &&
                  !host_shortcuts::is_fullscreen_chord(Qt::Key_G, both) &&
                  g.swallow_press(Qt::Key_F, both) &&
                  !host_shortcuts::CommandChordGate(true).swallow_press(Qt::Key_F, Qt::MetaModifier) &&
                  at_chord > 0 && at_guest > at_chord &&
                  count_in_code(mw, "host_shortcuts::fullscreen_sequence()") == 1);
    }

    // --- the Command chord gate ---------------------------------------------
    {
        host_shortcuts::CommandChordGate off(false);
        check("HS403-17", "the gate is inert off macOS: an unbound Ctrl+C reaches the guest, press and release",
              !off.swallow_press(Qt::Key_C, Qt::ControlModifier) && !off.swallow_release(Qt::Key_C));
        host_shortcuts::CommandChordGate g(true);
        const bool p = g.swallow_press(Qt::Key_C, Qt::ControlModifier);
        const bool r = g.swallow_release(Qt::Key_C);
        check("HS403-18", "macOS: a key pressed with Command held is a host chord - press and release are "
                          "swallowed, so an unbound Command+C never types C in the guest",
              p && r);
        check("HS403-33", "macOS: a key pressed with the PHYSICAL Control key held (MetaModifier = Symbol "
                          "Shift) is never swallowed, press or release; Command+Control still is",
              !g.swallow_press(Qt::Key_O, Qt::MetaModifier) && !g.swallow_release(Qt::Key_O) &&
                  g.swallow_press(Qt::Key_O, Qt::MetaModifier | Qt::ControlModifier) &&
                  g.swallow_release(Qt::Key_O));
        check("HS403-19", "macOS: a key pressed WITHOUT Command reaches the guest, and so does its release "
                          "even if Command went down meanwhile",
              !g.swallow_press(Qt::Key_D, Qt::NoModifier) && !g.swallow_release(Qt::Key_D));
        host_shortcuts::CommandChordGate g2(true);
        bool mods_pass = true;
        for (int k : {int(Qt::Key_Shift), int(Qt::Key_Control), int(Qt::Key_Alt), int(Qt::Key_Meta),
                      int(Qt::Key_AltGr), int(Qt::Key_CapsLock)})
            mods_pass = mods_pass && !g2.swallow_press(k, Qt::ControlModifier) && !g2.swallow_release(k);
        bool digits = true;
        for (int k = Qt::Key_1; k <= Qt::Key_8; ++k) digits = digits && !g2.swallow_press(k, Qt::ControlModifier);
        check("HS403-20", "macOS: the modifier keys themselves and the digits 1..8 (the Router's Command+n "
                          "script chord) are never swallowed; Command+9 and Command+0 are",
              mods_pass && digits && g2.swallow_press(Qt::Key_9, Qt::ControlModifier) && g2.swallow_press(Qt::Key_0, Qt::ControlModifier));
        host_shortcuts::CommandChordGate g3(true);
        g3.swallow_press(Qt::Key_X, Qt::ControlModifier);          // swallowed, its key-up never arrives
        const bool again = g3.swallow_press(Qt::Key_X, Qt::NoModifier);   // later plain press: forwarded
        const bool rel = g3.swallow_release(Qt::Key_X);          // ... and its release is real
        check("HS403-21", "macOS: a stale entry (a swallowed key whose key-up never came) is dropped by the "
                          "key's next plain press, so it cannot eat a real release",
              !again && !rel);
        host_shortcuts::CommandChordGate g4(true);
        g4.swallow_press(Qt::Key_Z, Qt::ControlModifier);
        g4.reset();
        check("HS403-22", "reset() (the keyboard went elsewhere) forgets every swallowed key",
              !g4.swallow_release(Qt::Key_Z));
    }

    // --- texts and the Quit label -------------------------------------------
    {
        using namespace host_shortcuts;
        check("HS403-34", "elsewhere the texts are byte-identical to GH #115's: Pause tooltip, Quick Screenshot "
                          "note, debugger tooltip, script panel strings",
              pause_tooltip(false) == "Pause / resume emulation (Alt+U or Pause)" &&
                  quick_screenshot_note(false).startsWith("Quick Screenshot (Alt+K) writes jnext-") &&
                  debugger_tooltip(false) == "Toggle Debugger (Alt+D)" &&
                  script_capture_tooltip(false) ==
                      "Capture the screen at the next frame (Alt+8 does the same while recording)" &&
                  script_recording_text("x", false) == "Recording to x (Alt+8 captures).");
        check("HS403-35", "macOS: none of those texts names Alt; each names the Command form of its action",
              !pause_tooltip(true).contains("Alt") && pause_tooltip(true).contains(text(Action::Pause, true)) &&
                  !quick_screenshot_note(true).contains("Alt") &&
                  quick_screenshot_note(true).contains(text(Action::QuickScreenshot, true)) &&
                  !debugger_tooltip(true).contains("Alt") &&
                  debugger_tooltip(true).contains(text(Action::Debugger, true)) &&
                  !script_capture_tooltip(true).contains("Alt") &&
                  script_capture_tooltip(true).contains(script_text(8, true)) &&
                  !script_recording_text("x", true).contains("Alt") &&
                  script_recording_text("x", true).contains(script_text(8, true)));
        check("HS403-36", "the Quit label carries a tab and both shortcuts elsewhere (Alt+Q and Alt+F4) and "
                          "none on macOS, where it has one shortcut",
              quit_shortcuts(false).size() == 2 && quit_shortcuts(true).size() == 1 &&
                  quit_text("&Quit", quit_shortcuts(false)).startsWith("&Quit\tAlt+Q") &&
                  quit_text("&Quit", quit_shortcuts(false)).endsWith("Alt+F4") &&
                  quit_text("&Quit", quit_shortcuts(true)) == "&Quit",
              quit_text("&Quit", quit_shortcuts(false)).toStdString());
    }

    // --- the debugger keymap's reserved chords follow the host modifier ----
    {
        using namespace jnext::dbgkeys;
        auto refused = [](Combo c, bool mac) { std::string why; return !validate_combo(c, why, mac); };
        auto ctrl_n = [](Key k) { return Combo{MOD_CTRL, k}; };
        auto alt_n = [](Key k) { return Combo{MOD_ALT, k}; };
        check("HS403-23", "macOS: Ctrl(=Command)+1..8 and Ctrl+Shift+F5/F9 are refused as debugger bindings "
                          "(script keys, quick snapshots); Ctrl+9 and Ctrl+Shift+F6 are bindable",
              refused(ctrl_n(Key::Num1), true) && refused(ctrl_n(Key::Num8), true) &&
                  refused(Combo{MOD_CTRL | MOD_SHIFT, Key::F5}, true) &&
                  refused(Combo{MOD_CTRL | MOD_SHIFT, Key::F9}, true) &&
                  !refused(ctrl_n(Key::Num9), true) &&
                  !refused(Combo{MOD_CTRL | MOD_SHIFT, Key::F6}, true));
        check("HS403-24", "the default is unchanged: Alt+1..8 and Alt+Shift+F5/F9 are refused, Ctrl+1 and "
                          "Ctrl+Shift+F5 are bindable",
              refused(alt_n(Key::Num1), false) && refused(alt_n(Key::Num8), false) &&
                  refused(Combo{MOD_ALT | MOD_SHIFT, Key::F5}, false) &&
                  refused(Combo{MOD_ALT | MOD_SHIFT, Key::F9}, false) &&
                  !refused(ctrl_n(Key::Num1), false) &&
                  !refused(Combo{MOD_CTRL | MOD_SHIFT, Key::F5}, false));
        check("HS403-25", "on macOS Alt+1 (Option+1) is no longer reserved: it is not a host chord there",
              !refused(alt_n(Key::Num1), true));
        std::vector<LoadIssue> issues;
        const Keymap km = build_keymap({{"step_back", "Ctrl+3"}}, issues, true);
        check("HS403-26", "a hand-edited [debugger_keys] entry on Command+3 is refused on macOS and the "
                          "action keeps its default",
              issues.size() == 1 && km.is_default(jnext::dbgkeys::Action::StepBack));
    }

    // --- the wiring: the product reads the table, not literals -------------
    {
        const QString mw = read_all("src/gui/main_window.cpp");
        const QString dw = read_all("src/debugger/debugger_window.cpp");
        check("HS403-27", "main_window.cpp and debugger_window.cpp bind no literal Qt::ALT shortcut in code: "
                          "every host shortcut goes through host_shortcuts.h",
              !mw.isEmpty() && !dw.isEmpty() && count_in_code(mw, "Qt::ALT") == 0 &&
                  count_in_code(dw, "Qt::ALT") == 0);
        check("HS403-28", "MainWindow takes the modifier scancodes, the F4 quit test and the Command gate "
                          "from host_shortcuts.h",
              count_in_code(mw, "host_shortcuts::modifier_scancode(") == 1 &&
                  count_in_code(mw, "case Qt::Key_Meta") == 0 &&
                  count_in_code(mw, "host_shortcuts::f1_decision(") == 1 &&
                  count_in_code(mw, "host_shortcuts::f4_decision(") == 1 &&
                  count_in_code(mw, "host_shortcuts::quit_shortcuts()") == 1 &&
                  count_in_code(mw, "host_shortcuts::quit_text(") == 1 &&
                  count_in_code(mw, "host_shortcuts::pause_tooltip()") == 1 &&
                  count_in_code(mw, "command_gate_.swallow_press(event->key(), event->modifiers())") == 1 &&
                  count_in_code(mw, "host_shortcuts::record_sequence(") == 2 &&
                                    count_in_code(mw, "command_gate_.swallow_release(") == 1 &&
                  count_in_code(mw, "command_gate_.reset()") == 1);
        const QString qa = read_all("src/gui/qt_app.cpp");
        const QString sa = read_all("src/platform/sdl_app.cpp");
        check("HS403-29", "both frontends switch the Router's script chord (and the SDL tracker) to the "
                          "platform's host modifier",
              count_in_code(qa, "key_router_.set_script_chord_gui(kQtCtrlIsCommand)") == 1 &&
                  count_in_code(sa, "key_router_.set_script_chord_gui(host_modifier::kIsGui)") == 1 &&
                  count_in_code(sa, "quick_chords_.set_host_is_gui(host_modifier::kIsGui)") == 1);
        check("HS403-37", "the text sites use the helpers, not literals: Preferences note, debugger toolbar tooltip, "
                          "script panel capture tooltip and recording line",
              count_in_code(read_all("src/gui/preferences_dialog.cpp"), "host_shortcuts::quick_screenshot_note()") == 1 &&
                  count_in_code(read_all("src/debugger/debugger_manager.cpp"), "host_shortcuts::debugger_tooltip()") == 1 &&
                  count_in_code(read_all("src/debugger/script_panel.cpp"), "host_shortcuts::script_capture_tooltip()") == 1 &&
                  count_in_code(read_all("src/debugger/script_panel.cpp"), "host_shortcuts::script_recording_text(") == 1);
        const QString ac = read_all("src/gui/app_config.cpp");
        const QString hc = read_all("src/gui/host_chords.cpp");
        const QString sc = read_all("src/gui/shortcut_capture_button.cpp");
        check("HS403-30", "the three debugger-keymap validators pass the platform's host modifier",
              count_in_code(ac, "build_keymap(entries, debug_key_issues_, kQtCtrlIsCommand)") == 1 &&
                  count_in_code(hc, "validate_combo(c, why, kQtCtrlIsCommand)") == 1 &&
                  count_in_code(sc, "validate_combo(c, why, kQtCtrlIsCommand)") == 1);
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
