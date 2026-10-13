#pragma once

// GH #363 — colours the Qt panels pick for themselves, made to work on a dark
// palette as well as a light one.
//
// Several panels were written against a white background: a light-yellow PC
// line, light-green stack-pointer row, grey separators, a #CC0000 "HALTED"
// label. On a dark palette the table text turns light while those hand-picked
// backgrounds stay light, and the result cannot be read. These helpers keep the
// colour a panel names, unchanged, on a light palette (so light mode looks
// exactly as before), and give its dark-palette counterpart otherwise.
//
// HEADER-ONLY and in src/qt/ for the reason debug_keymap_qt.h gives: both
// jnext_gui and jnext_debugger use it, and neither may depend on the other.

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QCoreApplication>
#include <QPalette>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace palette_tint {

/// A dark palette: the window is darker than its text.
inline bool is_dark(const QPalette& p) {
    return p.color(QPalette::Window).lightness() < p.color(QPalette::WindowText).lightness();
}

/// A colour's lightness mirrored around a dark palette's Base: what is near
/// white on a light panel lands near the dark Base, what is darker lands
/// lighter. Hue and saturation are kept.
inline QColor mirror(const QPalette& p, const QColor& c) {
    const int base = p.color(QPalette::Base).lightness();
    const int l = std::clamp(255 - c.lightness() + base, 0, 255);
    int h = 0, s = 0, ll = 0, a = 255;
    c.getHsl(&h, &s, &ll, &a);
    return QColor::fromHsl(h, s, l, a).toRgb();
}

/// A background picked for a light panel (white, a light tint, a grey band
/// or separator): itself on a light palette, its mirror on a dark one, at half
/// the saturation (a pale tint's saturation is a strong colour once dark).
/// White becomes the dark palette's Base.
inline QColor surface(const QPalette& p, const QColor& light) {
    if (!is_dark(p)) return light;
    const QColor m = mirror(p, light);
    int h = 0, s = 0, l = 0, a = 255;
    m.getHsl(&h, &s, &l, &a);
    return QColor::fromHsl(h, s / 2, l, a).toRgb();   // RGB spec: QColor == compares the spec
}

/// A text or line colour picked to read on a light background (#CC0000,
/// #888888, grey address columns): itself on a light palette, its mirror
/// (as far above the dark Base as it was below white) on a dark one. Black
/// text becomes the palette's own Text.
inline QColor ink(const QPalette& p, const QColor& light) {
    if (!is_dark(p)) return light;
    if (light.lightness() == 0) return p.color(QPalette::Text);
    return mirror(p, light);
}

/// "#RRGGBB" in upper case, the spelling the style sheets use.
inline QString css(const QColor& c) {
    return c.name(QColor::HexRgb).toUpper();
}

/// Keep `w`'s style sheet built from the application palette: set it now, and
/// again after every application palette change (a light/dark switch).
/// Built from QApplication::palette(), never from `w`'s own, which its style
/// sheet has already recoloured.
///
/// Measured on Qt 6.11: an application palette change sends
/// ApplicationPaletteChange to the application and the top-level widgets
/// only, and a widget with a style sheet, with everything inside it, keeps the
/// palette it had when the sheet was applied. So one listener on the
/// application re-sets every followed sheet, queued until the change has been
/// delivered, and re-sets it even when the text is unchanged: that re-polish
/// is what moves the subtree (the CPU panel inside its titled box) onto the
/// new palette.
namespace detail {
class Followers : public QObject {
public:
    static Followers& get() {
        static QPointer<Followers> hub;
        if (!hub) {
            hub = new Followers(QCoreApplication::instance());
            QCoreApplication::instance()->installEventFilter(hub);
        }
        return *hub;
    }
    void add(QWidget* w, std::function<QString(const QPalette&)> f) {
        w->setStyleSheet(f(QApplication::palette()));
        entries_.push_back({w, std::move(f)});
    }
protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() == QEvent::ApplicationPaletteChange && obj == QCoreApplication::instance() &&
            !queued_) {
            queued_ = true;
            QTimer::singleShot(0, this, [this]() { restyle(); });
        }
        return false;
    }
private:
    explicit Followers(QObject* parent) : QObject(parent) {}
    void restyle() {
        queued_ = false;
        const QPalette pal = QApplication::palette();
        std::vector<Entry> live;
        for (Entry& e : entries_) {
            if (!e.w) continue;
            e.w->setStyleSheet(QString());
            e.w->setStyleSheet(e.f(pal));
            live.push_back(std::move(e));
        }
        entries_ = std::move(live);
    }
    struct Entry { QPointer<QWidget> w; std::function<QString(const QPalette&)> f; };
    std::vector<Entry> entries_;
    bool queued_ = false;
};
} // namespace detail

inline void follow(QWidget* w, std::function<QString(const QPalette&)> sheet) {
    detail::Followers::get().add(w, std::move(sheet));
}

} // namespace palette_tint
