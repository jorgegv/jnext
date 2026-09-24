#pragma once

// GH #1 — the Qt bridge for the Qt-free model in debug_keymap.h.
//
// HEADER-ONLY, and deliberately so: it sits in src/debug/ next to the model it
// converts, but jnext_debug (a Qt-free static library, linked into builds with
// no Qt at all) never compiles it. Only Qt translation units include it, and
// both jnext_gui and jnext_debugger do — which is why it cannot live in either
// of those two directories without creating a link edge between them that the
// ENABLE_QT_UI x ENABLE_DEBUGGER build matrix forbids.

#include "debug/debug_keymap.h"

#include <QKeySequence>
#include <QtCore/qnamespace.h>

namespace jnext::dbgkeys {

/// Model key -> Qt key. Returns 0 for Key::None (and for a key this bridge
/// somehow does not know, which the bounded vocabulary makes unreachable).
inline int to_qt_key(Key k) {
    switch (k) {
        case Key::F1:  return Qt::Key_F1;   case Key::F2:  return Qt::Key_F2;
        case Key::F3:  return Qt::Key_F3;   case Key::F4:  return Qt::Key_F4;
        case Key::F5:  return Qt::Key_F5;   case Key::F6:  return Qt::Key_F6;
        case Key::F7:  return Qt::Key_F7;   case Key::F8:  return Qt::Key_F8;
        case Key::F9:  return Qt::Key_F9;   case Key::F10: return Qt::Key_F10;
        case Key::F11: return Qt::Key_F11;  case Key::F12: return Qt::Key_F12;

        case Key::A: return Qt::Key_A;  case Key::B: return Qt::Key_B;
        case Key::C: return Qt::Key_C;  case Key::D: return Qt::Key_D;
        case Key::E: return Qt::Key_E;  case Key::F: return Qt::Key_F;
        case Key::G: return Qt::Key_G;  case Key::H: return Qt::Key_H;
        case Key::I: return Qt::Key_I;  case Key::J: return Qt::Key_J;
        case Key::K: return Qt::Key_K;  case Key::L: return Qt::Key_L;
        case Key::M: return Qt::Key_M;  case Key::N: return Qt::Key_N;
        case Key::O: return Qt::Key_O;  case Key::P: return Qt::Key_P;
        case Key::Q: return Qt::Key_Q;  case Key::R: return Qt::Key_R;
        case Key::S: return Qt::Key_S;  case Key::T: return Qt::Key_T;
        case Key::U: return Qt::Key_U;  case Key::V: return Qt::Key_V;
        case Key::W: return Qt::Key_W;  case Key::X: return Qt::Key_X;
        case Key::Y: return Qt::Key_Y;  case Key::Z: return Qt::Key_Z;

        case Key::Num0: return Qt::Key_0;  case Key::Num1: return Qt::Key_1;
        case Key::Num2: return Qt::Key_2;  case Key::Num3: return Qt::Key_3;
        case Key::Num4: return Qt::Key_4;  case Key::Num5: return Qt::Key_5;
        case Key::Num6: return Qt::Key_6;  case Key::Num7: return Qt::Key_7;
        case Key::Num8: return Qt::Key_8;  case Key::Num9: return Qt::Key_9;

        case Key::Space:     return Qt::Key_Space;
        case Key::Tab:       return Qt::Key_Tab;
        case Key::Return:    return Qt::Key_Return;
        case Key::Backspace: return Qt::Key_Backspace;
        case Key::Escape:    return Qt::Key_Escape;
        case Key::Insert:    return Qt::Key_Insert;
        case Key::Delete:    return Qt::Key_Delete;
        case Key::Home:      return Qt::Key_Home;
        case Key::End:       return Qt::Key_End;
        case Key::PageUp:    return Qt::Key_PageUp;
        case Key::PageDown:  return Qt::Key_PageDown;
        case Key::Up:        return Qt::Key_Up;
        case Key::Down:      return Qt::Key_Down;
        case Key::Left:      return Qt::Key_Left;
        case Key::Right:     return Qt::Key_Right;

        case Key::None: break;
    }
    return 0;
}

/// Qt key -> model key. Key::None when the key is outside the bounded
/// vocabulary, which is how the capture widget refuses one by name.
inline Key from_qt_key(int qt_key) {
    for (int i = 1; i <= static_cast<int>(Key::Right); ++i) {
        const Key k = static_cast<Key>(i);
        if (to_qt_key(k) == qt_key) return k;
    }
    // Qt::Key_Enter is the keypad's Return; the disassembly panel already
    // treats the two as one, so the model does too.
    if (qt_key == Qt::Key_Enter) return Key::Return;
    // Shift+Tab arrives as Backtab, never as Tab.
    if (qt_key == Qt::Key_Backtab) return Key::Tab;
    return Key::None;
}

inline uint8_t from_qt_mods(Qt::KeyboardModifiers m) {
    uint8_t out = MOD_NONE;
    if (m & Qt::ShiftModifier)   out |= MOD_SHIFT;
    if (m & Qt::ControlModifier) out |= MOD_CTRL;
    if (m & Qt::AltModifier)     out |= MOD_ALT;
    if (m & Qt::MetaModifier)    out |= MOD_META;
    return out;
}

inline Qt::KeyboardModifiers to_qt_mods(uint8_t mods) {
    Qt::KeyboardModifiers out = Qt::NoModifier;
    if (mods & MOD_SHIFT) out |= Qt::ShiftModifier;
    if (mods & MOD_CTRL)  out |= Qt::ControlModifier;
    if (mods & MOD_ALT)   out |= Qt::AltModifier;
    if (mods & MOD_META)  out |= Qt::MetaModifier;
    return out;
}

/// An empty QKeySequence for an unbound combination — which is exactly what
/// QAction::setShortcut() wants in order to carry no shortcut at all.
inline QKeySequence to_key_sequence(const Combo& c) {
    if (!c.bound()) return QKeySequence();
    return QKeySequence(static_cast<int>(to_qt_mods(c.mods)) | to_qt_key(c.key));
}

/// Build a Combo from a real key event's key + modifiers. A pure modifier
/// press (Shift alone, ...) yields an unbound Combo, so a capture widget can
/// keep waiting rather than committing half a chord.
inline Combo combo_from_event(int qt_key, Qt::KeyboardModifiers mods) {
    switch (qt_key) {
        case Qt::Key_Shift: case Qt::Key_Control:
        case Qt::Key_Alt:   case Qt::Key_Meta:
        case Qt::Key_AltGr: case Qt::Key_unknown:
            return Combo{};
        default: break;
    }
    Combo c;
    c.mods = from_qt_mods(mods);
    c.key  = from_qt_key(qt_key);
    if (c.key == Key::None) return Combo{};
    // Qt reports Shift+Tab as Backtab with ShiftModifier still set, so the
    // modifier needs no fixing; nothing else needs a special case here.
    return c;
}

} // namespace jnext::dbgkeys
