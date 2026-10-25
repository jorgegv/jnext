#pragma once

// GH #403 - the Qt frontend's host shortcuts, per platform, in ONE place.
//
// Everywhere but macOS the host namespace is Alt (GH #115: Ctrl is the guest's
// Symbol Shift, so a Ctrl+letter host shortcut eats a Symbol Shift sequence).
// On macOS it is Command, the platform's own shortcut modifier, and the guest's
// Symbol Shift sits on the PHYSICAL Control key. Qt reports Command as
// Key_Control / ControlModifier / Qt::CTRL and Control as Key_Meta /
// MetaModifier / Qt::META (jnext does not set AA_MacDontSwapCtrlAndMeta; see
// qt_ctrl_is_command.h), so on macOS the host modifier is Qt::CTRL.
//
// Every function takes the platform as a parameter (default: this build's) so
// the macOS branch is unit-tested on Linux, the way
// pointer_capture::release_chord() is.

#include <QCoreApplication>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QtGlobal>
#include <SDL3/SDL.h>

#include <set>

#include "gui/qt_ctrl_is_command.h"

namespace host_shortcuts {

/// Every host shortcut that is Alt+X elsewhere. The label is the menu entry.
enum class Action {
    LoadNex,          ///< File > Load NEX File
    ReloadLast,       ///< File > Reload Last File
    Screenshot,       ///< File > Save Screenshot
    QuickScreenshot,  ///< File > Quick Screenshot
    SaveSnapshot,     ///< File > Save Snapshot
    QuickSave,        ///< File > Quick Save Snapshot
    QuickLoad,        ///< File > Quick Load Snapshot
    Quit,             ///< File > Quit
    Reset,            ///< Machine > Power Reset
    Pause,            ///< Machine > Pause
    TapeOpen,         ///< Tape > Open Tape File
    Debugger,         ///< View > Debugger
    Preferences,      ///< Settings > Preferences
};

inline constexpr Action kAllActions[] = {
    Action::LoadNex,   Action::ReloadLast, Action::Screenshot,   Action::QuickScreenshot,
    Action::SaveSnapshot, Action::QuickSave, Action::QuickLoad,   Action::Quit,
    Action::Reset,     Action::Pause,      Action::TapeOpen,     Action::Debugger,
    Action::Preferences,
};

/// The modifier that carries the host shortcuts.
inline constexpr Qt::Modifier modifier(bool ctrl_is_command = kQtCtrlIsCommand)
{
    return ctrl_is_command ? Qt::CTRL : Qt::ALT;
}

/// The primary key sequence of a host action.
inline QKeySequence sequence(Action a, bool ctrl_is_command = kQtCtrlIsCommand)
{
    const Qt::Modifier m = modifier(ctrl_is_command);
    switch (a) {
    case Action::LoadNex:         return QKeySequence(m | Qt::Key_O);
    case Action::ReloadLast:      return QKeySequence(m | Qt::Key_L);
    case Action::Screenshot:      return QKeySequence(m | Qt::Key_S);
    case Action::QuickScreenshot: return QKeySequence(m | Qt::Key_K);
    case Action::SaveSnapshot:    return QKeySequence(m | Qt::SHIFT | Qt::Key_S);
    case Action::QuickSave:       return QKeySequence(m | Qt::SHIFT | Qt::Key_F5);
    case Action::QuickLoad:       return QKeySequence(m | Qt::SHIFT | Qt::Key_F9);
    case Action::Quit:            return QKeySequence(m | Qt::Key_Q);
    case Action::Reset:           return QKeySequence(m | Qt::Key_R);
    case Action::Pause:           return QKeySequence(m | Qt::Key_U);
    case Action::TapeOpen:        return QKeySequence(m | Qt::Key_T);
    case Action::Debugger:        return QKeySequence(m | Qt::Key_D);
    // Command+, is macOS's Preferences chord. P is Print there, and P is free,
    // but a Mac user looks for the comma.
    case Action::Preferences:
        return ctrl_is_command ? QKeySequence(Qt::CTRL | Qt::Key_Comma)
                               : QKeySequence(Qt::ALT | Qt::Key_P);
    }
    return QKeySequence();
}

/// File > Record MPEG4 Video / Stop (F5 / F6). Ctrl+F5/F6 elsewhere; on macOS the
/// physical Control key (Qt::META), because Command+F5 is VoiceOver's system
/// toggle and macOS takes it first. F5/F6 are off the Spectrum matrix, so
/// Control (Symbol Shift) + F5 eats no guest sequence.
inline QKeySequence record_sequence(bool start, bool ctrl_is_command = kQtCtrlIsCommand)
{
    const Qt::Key k = start ? Qt::Key_F5 : Qt::Key_F6;
    return ctrl_is_command ? QKeySequence(Qt::META | k) : QKeySequence(Qt::CTRL | k);
}

/// File > Quit's second shortcut (GH #380): Alt+F4, a PC convention, so none on
/// macOS (Command+Q is the one chord there).
inline QKeySequence quit_alternate(bool ctrl_is_command = kQtCtrlIsCommand)
{
    return ctrl_is_command ? QKeySequence() : QKeySequence(Qt::ALT | Qt::Key_F4);
}

/// The script host key `n` (1..8): the host modifier + the digit.
inline QKeySequence script_sequence(int n, bool ctrl_is_command = kQtCtrlIsCommand)
{
    return QKeySequence(modifier(ctrl_is_command) | static_cast<Qt::Key>(Qt::Key_0 + n));
}

/// What a user-facing string shows for a shortcut: the platform's own spelling
/// (Alt+K; the Command symbol and K on macOS).
inline QString text(Action a, bool ctrl_is_command = kQtCtrlIsCommand)
{
    return sequence(a, ctrl_is_command).toString(QKeySequence::NativeText);
}
inline QString script_text(int n, bool ctrl_is_command = kQtCtrlIsCommand)
{
    return script_sequence(n, ctrl_is_command).toString(QKeySequence::NativeText);
}

/// What MainWindow::keyPressEvent does with an F1 / F4 press (GH #380, GH #403).
///
/// Elsewhere: F4 with Alt held quits whatever else is held; otherwise F1 resets
/// only with no Shift/Ctrl/Alt and F4 only with no Shift/Ctrl; every other
/// combination falls through to the guest path. macOS is the same with Alt
/// replaced by Command (Qt's ControlModifier), plus two rules: Option is no
/// host modifier, so Option+F1/F4 do nothing at all (Swallow), and the physical
/// Control key (Qt's MetaModifier) is Symbol Shift, so it behaves exactly as Ctrl
/// does elsewhere (Pass, no reset).
enum class FKeyDecision { Reset, Quit, Swallow, Pass };

inline FKeyDecision f1_decision(Qt::KeyboardModifiers m, bool ctrl_is_command = kQtCtrlIsCommand)
{
    if (ctrl_is_command && m.testFlag(Qt::AltModifier) && !m.testFlag(Qt::ControlModifier))
        return FKeyDecision::Swallow;
    if (m.testFlag(Qt::ShiftModifier) || m.testFlag(Qt::ControlModifier) || m.testFlag(Qt::AltModifier) ||
        (ctrl_is_command && m.testFlag(Qt::MetaModifier)))
        return FKeyDecision::Pass;
    return FKeyDecision::Reset;
}

inline FKeyDecision f4_decision(Qt::KeyboardModifiers m, bool ctrl_is_command = kQtCtrlIsCommand)
{
    if (m.testFlag(ctrl_is_command ? Qt::ControlModifier : Qt::AltModifier)) return FKeyDecision::Quit;
    if (ctrl_is_command && m.testFlag(Qt::AltModifier)) return FKeyDecision::Swallow;
    if (m.testFlag(Qt::ShiftModifier) || m.testFlag(Qt::ControlModifier) ||
        (ctrl_is_command && m.testFlag(Qt::MetaModifier)))
        return FKeyDecision::Pass;
    return FKeyDecision::Reset;
}

/// File > Quit's shortcuts: the primary, plus Alt+F4 where that exists.
inline QList<QKeySequence> quit_shortcuts(bool ctrl_is_command = kQtCtrlIsCommand)
{
    QList<QKeySequence> v{sequence(Action::Quit, ctrl_is_command)};
    if (!quit_alternate(ctrl_is_command).isEmpty()) v << quit_alternate(ctrl_is_command);
    return v;
}

/// The Quit menu label: a menu draws only the first shortcut, so with a second
/// one the label carries its own accelerator column after a tab; with one, no
/// tab (on macOS a tab in a Quit label would be read as a key equivalent).
inline QString quit_text(const QString& base, const QList<QKeySequence>& keys)
{
    if (keys.size() <= 1) return base;
    return base + QLatin1Char('\t') + QKeySequence::listToString(keys, QKeySequence::NativeText);
}

// User-visible strings that name a host shortcut. The shortcut is spelt by the
// platform (text()/script_text()), never typed here.
inline QString tr_(const char* s) { return QCoreApplication::translate("host_shortcuts", s); }

inline QString pause_tooltip(bool m = kQtCtrlIsCommand)
{
    return tr_("Pause / resume emulation (%1 or Pause)").arg(text(Action::Pause, m));
}
inline QString quick_screenshot_note(bool m = kQtCtrlIsCommand)
{
    return tr_("Quick Screenshot (%1) writes jnext-<date>-<time> into that "
               "directory with no dialog, and names the file in the status bar. "
               "A .SCR records only the classic ULA screen memory - Layer 2, the "
               "tilemap and the sprites are not in it.")
        .arg(text(Action::QuickScreenshot, m));
}
inline QString debugger_tooltip(bool m = kQtCtrlIsCommand)
{
    return tr_("Toggle Debugger (%1)").arg(text(Action::Debugger, m));
}
inline QString script_capture_tooltip(bool m = kQtCtrlIsCommand)
{
    return tr_("Capture the screen at the next frame (%1 does the same while recording)")
        .arg(script_text(8, m));
}
inline QString script_recording_text(const QString& what, bool m = kQtCtrlIsCommand)
{
    return tr_("Recording to %1 (%2 captures).").arg(what, script_text(8, m));
}

/// The guest scancode of a Qt modifier key (SDL_SCANCODE_UNKNOWN: none).
///
/// Elsewhere Control is Symbol Shift and the Meta/Windows key is nothing. On
/// macOS Qt swaps them: Key_Control is Command, which is a HOST key and goes to
/// the key Router as LGUI (the script chord reads it; the guest maps LGUI to
/// nothing), and Key_Meta is the physical Control key = Symbol Shift.
inline SDL_Scancode modifier_scancode(int qt_key, bool ctrl_is_command = kQtCtrlIsCommand)
{
    switch (qt_key) {
    case Qt::Key_Shift:   return SDL_SCANCODE_LSHIFT;
    case Qt::Key_Alt:     return SDL_SCANCODE_LALT;
    case Qt::Key_Control: return ctrl_is_command ? SDL_SCANCODE_LGUI : SDL_SCANCODE_LCTRL;
    case Qt::Key_Meta:    return ctrl_is_command ? SDL_SCANCODE_LCTRL : SDL_SCANCODE_UNKNOWN;
    default:              return SDL_SCANCODE_UNKNOWN;
    }
}

/// macOS only: a key pressed with Command held is a HOST chord and never
/// reaches the guest - an unbound Command+C must not type C. Qt's shortcut
/// map takes the bound chords before keyPressEvent; this catches the rest.
///
/// Exempt: the modifier keys themselves (Command's own press and release must
/// reach the Router) and the digits 1..8 (the Router's script chord is
/// Command+digit and swallows them itself).
///
/// A swallowed key's RELEASE is swallowed too, and only that: the set is the
/// keys whose press was, so a key pressed before Command went down still
/// releases normally, and a stale entry (a key-up that never came) is dropped
/// by the key's next unmodified press, so it cannot eat a real release.
class CommandChordGate {
public:
    explicit CommandChordGate(bool enabled = kQtCtrlIsCommand) : enabled_(enabled) {}

    /// `mods` are the event's modifiers; Command is Qt's ControlModifier. The
    /// physical Control key (MetaModifier) is Symbol Shift and never counts.
    bool swallow_press(int qt_key, Qt::KeyboardModifiers mods)
    {
        if (!enabled_ || is_exempt(qt_key)) return false;
        if (mods.testFlag(Qt::ControlModifier)) { held_.insert(qt_key); return true; }
        held_.erase(qt_key);
        return false;
    }
    bool swallow_release(int qt_key)
    {
        return enabled_ && held_.erase(qt_key) != 0;
    }
    /// The keyboard went elsewhere: every key-up still owed goes there too.
    void reset() { held_.clear(); }

private:
    static bool is_exempt(int k)
    {
        switch (k) {
        case Qt::Key_Shift: case Qt::Key_Control: case Qt::Key_Alt: case Qt::Key_Meta:
        case Qt::Key_AltGr: case Qt::Key_CapsLock:
            return true;
        default:
            return k >= Qt::Key_1 && k <= Qt::Key_8;
        }
    }
    bool enabled_;
    std::set<int> held_;
};

}  // namespace host_shortcuts
