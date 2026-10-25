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

#include <QKeySequence>
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

/// The same, as the event-side flag a QKeyEvent carries.
inline constexpr Qt::KeyboardModifier event_modifier(bool ctrl_is_command = kQtCtrlIsCommand)
{
    return ctrl_is_command ? Qt::ControlModifier : Qt::AltModifier;
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

/// GH #380 - an F4 with the host-quit modifier held is File > Quit. On macOS
/// there is no such chord (Command+Q only): F4 with Option held does nothing.
inline bool f4_quits(Qt::KeyboardModifiers mods, bool ctrl_is_command = kQtCtrlIsCommand)
{
    return !ctrl_is_command && mods.testFlag(Qt::AltModifier);
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

    bool swallow_press(int qt_key, bool command_held)
    {
        if (!enabled_ || is_exempt(qt_key)) return false;
        if (command_held) { held_.insert(qt_key); return true; }
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
