#pragma once

// GH #268 — a bare Alt tap must never take the keyboard away from what the
// user is typing into.
//
// HEADER-ONLY, and in src/debug/ for exactly the reason debug_keymap_qt.h
// gives next door: BOTH jnext_gui (the emulator window) and jnext_debugger
// (the debugger window) need it, and putting it in either directory would
// create a link edge between them that the ENABLE_QT_UI x ENABLE_DEBUGGER
// build matrix forbids. jnext_debug is Qt-free and never compiles this file;
// only Qt translation units include it.
//
// WHAT IT DEFENDS AGAINST. QMenuBar arms itself on the Alt SHORTCUT-OVERRIDE
// and, on the matching Alt key-up with nothing pressed in between, calls
// setKeyboardMode(true) -> setFocus(Qt::MenuBarFocusReason)
// (qmenubar.cpp:252,1463). That runs in a qApp-level event filter, BEFORE the
// window's own handlers and regardless of whether they accept the event, so no
// keyPressEvent override can defend against it. The whole filter — the arming
// included — is gated on this one style hint, so turning it off stops the
// machinery being installed at all rather than merely neutering its effect.
//
// WHAT IT DOES NOT CHANGE, checked against the Qt source rather than assumed:
// the hint gates the arming, the keyboard-mode entry, Up/Down/Enter on a
// FOCUSED menu bar (qmenubar.cpp:1039), and whether a menu opened BY A
// MNEMONIC additionally enters keyboard mode (qmenubar.cpp:1687 — which calls
// setCurrentAction(act, true, true) unconditionally and consults the hint only
// for the extra setKeyboardMode). Alt+<letter> therefore still opens its menu,
// which is then driven by the popup's own key handling.
//
// Install it on the MENU BAR, not on the application: those two call sites are
// the only ones that matter and both read the menu bar's own style().

#include <QMenuBar>
#include <QProxyStyle>
#include <QStyle>

namespace jnext {

class NoAltMenuNavigationStyle : public QProxyStyle {
public:
    int styleHint(QStyle::StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* ret = nullptr) const override {
        if (hint == QStyle::SH_MenuBar_AltKeyNavigation) return 0;
        return QProxyStyle::styleHint(hint, option, widget, ret);
    }
};

/// Give `bar` a style that reports Alt-only menu navigation as off, owned by
/// `owner`. QWidget::setStyle does not take ownership, hence the parenting.
inline void disable_alt_menu_navigation(QMenuBar* bar, QObject* owner) {
    auto* style = new NoAltMenuNavigationStyle();
    style->setParent(owner);
    bar->setStyle(style);
}

}  // namespace jnext
