// Menu-accelerator harvesting — shared by the two accelerator-collision suites
// (test/debugger/accel_test.cpp for the debugger window, GH #124;
//  test/gui/main_window_accel_test.cpp for the main window, GH #217).
//
// WHY THIS IS ONE FILE AND NOT TWO COPIES. The debugger suite's harvest is the
// part of that suite with the subtle correctness properties — it reads Qt's own
// mnemonic parser rather than a hand-rolled one, and it reads shortcuts()
// PLURAL rather than shortcut(), because an action given alternates via
// setShortcuts() carries a colliding sequence the singular form never returns.
// A guard with a blind spot is worse than ordinary code with one, since
// everything else relies on it to catch the NEXT collision. Copying the walk
// into a second suite would mean the two guards can drift, and a blind spot
// fixed in one would silently survive in the other. One walk, one place to fix.
//
// THE SCOPES QT ACTUALLY USES — which is what makes any of this checkable:
//
//   * menu-bar titles and any QAbstractButton / buddy-QLabel mnemonic share ONE
//     window-wide Alt namespace (Qt::WindowShortcut context);
//   * the items inside one QMenu popup share a namespace of their own, live
//     only while that popup is open, and so may legitimately reuse a letter
//     another menu uses;
//   * QKeySequence shortcuts (F5, Shift+F7, ...) are a third, window-wide
//     namespace.
//
// Nothing here asserts. The suites own their rows; this owns the walk.

#pragma once

#include <QAbstractButton>
#include <QAction>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QString>
#include <QWidget>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace accel {

/// One accelerator: the namespace it lives in, what carries it, and the key.
struct Accel {
    QString scope;   ///< "<menu bar>", "Debug", "Debug > Trace", "<window>"...
    QString label;   ///< the source text, ampersands and all
    QString key;     ///< "Alt+W", "Shift+F7", ...
};

/// Scope names. The menu bar and the two window-wide namespaces are fixed
/// strings; a popup's scope is its own visible title path ("Machine > CPU
/// Speed"), built during the walk.
inline QString menu_bar_scope()  { return QStringLiteral("<menu bar>"); }
inline QString window_scope()    { return QStringLiteral("<window>"); }
inline QString window_alt_scope(){ return QStringLiteral("<window Alt>"); }

/// The visible text: Qt eats the mnemonic '&' and renders "&&" as one '&'.
inline QString visible(const QString& text) {
    QString out;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == u'&') {
            if (i + 1 < text.size() && text[i + 1] == u'&') { out += u'&'; ++i; }
            continue;
        }
        out += text[i];
    }
    return out;
}

/// Qt's OWN reading of a label's mnemonic, not a hand-rolled parser: this is
/// the same call Qt uses to turn "Step O&ut" into Alt+U, so the harvest cannot
/// drift from the rule the running menu bar applies.
inline QString mnemonic_of(const QString& text) {
    const QKeySequence seq = QKeySequence::mnemonic(text);
    return seq.isEmpty() ? QString() : seq.toString();
}

/// Recursively harvest one popup's items; each submenu opens a scope of its own.
inline void harvest_menu(QMenu* menu, const QString& scope,
                         std::vector<Accel>& out, std::set<QString>& scopes) {
    scopes.insert(scope);
    for (QAction* a : menu->actions()) {
        if (a->isSeparator() || a->text().isEmpty()) continue;
        const QString key = mnemonic_of(a->text());
        if (!key.isEmpty()) out.push_back(Accel{scope, a->text(), key});
    }
    for (QAction* a : menu->actions())
        if (a->menu())
            harvest_menu(a->menu(), scope + QStringLiteral(" > ") + visible(a->text()),
                         out, scopes);
}

/// The menu bar's own titles plus, recursively, every popup under it. Appends
/// to `out`; `scopes` collects the popup scope names (the menu bar is not one
/// of them — it is its own, always-present namespace).
inline void harvest_menu_bar(QMenuBar* bar, std::vector<Accel>& out,
                             std::set<QString>& scopes) {
    for (QAction* a : bar->actions()) {
        if (a->isSeparator() || a->text().isEmpty()) continue;
        const QString key = mnemonic_of(a->text());
        if (!key.isEmpty()) out.push_back(Accel{menu_bar_scope(), a->text(), key});
    }
    for (QAction* a : bar->actions())
        if (a->menu())
            harvest_menu(a->menu(), visible(a->text()), out, scopes);
}

/// Every QKeySequence bound under `root`. shortcuts() is PLURAL on purpose —
/// see the file header.
///
/// A Qt::WidgetShortcut / WidgetWithChildrenShortcut action fires only while its
/// widget has the focus, so two of them on DIFFERENT widgets (the Disassembly's
/// Ctrl+C and the Source tab's, GH #423) do not compete: each gets a scope of its
/// owning widget. One that shares a chord with a window-wide action still
/// competes with it (Qt finds both when that widget has the focus), so it is also
/// counted in the window scope then, where collisions() flags it. Two on widgets
/// where one is an ancestor of the other compete too (a WidgetWithChildrenShortcut
/// action sees the focus of its descendants): each such pair shares a scope of its own.
inline std::vector<Accel> harvest_shortcuts(const QWidget* root) {
    std::vector<Accel> out, narrow;
    std::vector<const QObject*> narrow_owner;   // parallel to `narrow`
    for (QAction* a : root->findChildren<QAction*>()) {
        const bool is_narrow = a->shortcutContext() == Qt::WidgetShortcut ||
                               a->shortcutContext() == Qt::WidgetWithChildrenShortcut;
        QString owner;
        const QObject* owner_obj = nullptr;
        if (is_narrow)
            for (QObject* o : a->associatedObjects())
                if (qobject_cast<QWidget*>(o)) {
                    owner_obj = o;
                    owner = QString::fromLatin1(o->metaObject()->className()) + QStringLiteral("@") +
                            QString::number(reinterpret_cast<quintptr>(o), 16);
                    break;
                }
        for (const QKeySequence& seq : a->shortcuts()) {
            if (seq.isEmpty()) continue;
            if (is_narrow && !owner.isEmpty()) {
                narrow.push_back(Accel{QStringLiteral("<widget ") + owner + QStringLiteral(">"), a->text(), seq.toString()});
                narrow_owner.push_back(owner_obj);
            }
            else
                out.push_back(Accel{window_scope(), a->text(), seq.toString()});
        }
    }
    const std::vector<Accel> wide = out;
    for (const Accel& n : narrow) {
        out.push_back(n);
        for (const Accel& w : wide)
            if (w.key == n.key) { out.push_back(Accel{window_scope(), n.label, n.key}); break; }
    }
    for (size_t i = 0; i < narrow.size(); ++i)
        for (size_t j = i + 1; j < narrow.size(); ++j) {
            const QWidget* a = qobject_cast<const QWidget*>(narrow_owner[i]);
            const QWidget* b = qobject_cast<const QWidget*>(narrow_owner[j]);
            if (narrow[i].key != narrow[j].key || a == b) continue;
            if (!a->isAncestorOf(b) && !b->isAncestorOf(a)) continue;
            const QString scope = QStringLiteral("<widget ancestry %1>").arg(i * narrow.size() + j);
            out.push_back(Accel{scope, narrow[i].label, narrow[i].key});
            out.push_back(Accel{scope, narrow[j].label, narrow[j].key});
        }
    return out;
}

/// Mnemonics carried by widgets rather than menus: a QPushButton labelled
/// "&Watch" or a QLabel with a buddy claims a letter out of the SAME
/// window-wide namespace as the menu bar, from a completely different call
/// site. A QLabel with no buddy registers no mnemonic and is skipped.
inline std::vector<Accel> harvest_widget_mnemonics(const QWidget* root) {
    std::vector<Accel> out;
    for (QAbstractButton* b : root->findChildren<QAbstractButton*>()) {
        const QString key = mnemonic_of(b->text());
        if (!key.isEmpty()) out.push_back(Accel{window_alt_scope(), b->text(), key});
    }
    for (QLabel* l : root->findChildren<QLabel*>()) {
        if (!l->buddy()) continue;
        const QString key = mnemonic_of(l->text());
        if (!key.isEmpty()) out.push_back(Accel{window_alt_scope(), l->text(), key});
    }
    return out;
}

/// Every (namespace, key) claimed by more than one label, rendered for a
/// failure detail. Empty string means no collision.
inline std::string collisions(const std::vector<Accel>& all) {
    std::map<QString, std::vector<QString>> by_key;
    for (const Accel& a : all)
        by_key[a.scope + QStringLiteral(" / ") + a.key].push_back(a.label);

    std::string out;
    for (const auto& kv : by_key) {
        if (kv.second.size() < 2) continue;
        if (!out.empty()) out += "; ";
        out += kv.first.toStdString() + " claimed by";
        for (const QString& label : kv.second)
            out += " \"" + label.toStdString() + "\"";
    }
    return out;
}

inline std::vector<Accel> only_scope(const std::vector<Accel>& all, const QString& scope) {
    std::vector<Accel> v;
    for (const Accel& a : all)
        if (a.scope == scope) v.push_back(a);
    return v;
}

inline std::vector<Accel> except_scope(const std::vector<Accel>& all, const QString& scope) {
    std::vector<Accel> v;
    for (const Accel& a : all)
        if (a.scope != scope) v.push_back(a);
    return v;
}

/// Widget mnemonics that clash inside the window-wide Alt namespace — against
/// a menu-bar title, or against another widget. Empty string means clean.
inline std::string widget_alt_conflicts(const std::vector<Accel>& widgets,
                                        const std::vector<Accel>& menu_bar) {
    std::set<QString> taken;
    for (const Accel& a : menu_bar) taken.insert(a.key);

    std::string bad;
    std::set<QString> seen;
    for (const Accel& w : widgets) {
        const char* why = taken.count(w.key) ? "already a menu-bar mnemonic"
                        : seen.count(w.key)  ? "already taken by another widget"
                                             : nullptr;
        if (why) {
            if (!bad.empty()) bad += "; ";
            bad += "\"" + w.label.toStdString() + "\" wants " + w.key.toStdString()
                 + " — " + why;
        }
        seen.insert(w.key);
    }
    return bad;
}

} // namespace accel
