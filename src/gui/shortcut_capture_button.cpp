#include "gui/shortcut_capture_button.h"

#include "debug/debug_keymap_qt.h"

#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>

using namespace jnext::dbgkeys;

ShortcutCaptureButton::ShortcutCaptureButton(QWidget* parent)
    : QPushButton(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    refresh_text();
    connect(this, &QPushButton::clicked, this, [this]() { start_capture(); });
}

void ShortcutCaptureButton::set_combo(const Combo& c) {
    combo_ = c;
    refresh_text();
}

void ShortcutCaptureButton::start_capture() {
    if (capturing_) return;
    capturing_ = true;
    setText(tr("Press a key... (Esc cancels)"));
    setFocus(Qt::OtherFocusReason);
}

void ShortcutCaptureButton::end_capture() {
    capturing_ = false;
    refresh_text();
}

void ShortcutCaptureButton::refresh_text() {
    setText(combo_.bound()
                ? QString::fromStdString(render_combo(combo_))
                : tr("(unbound)"));
}

bool ShortcutCaptureButton::event(QEvent* e) {
    // The documented way to out-rank an existing shortcut: accepting a
    // ShortcutOverride tells Qt to deliver the KeyPress to THIS widget instead
    // of firing whatever QAction owns the chord. Without it, capturing F5
    // while F5 is bound would trigger the binding rather than replace it.
    if (capturing_ && e->type() == QEvent::ShortcutOverride) {
        e->accept();
        return true;
    }
    return QPushButton::event(e);
}

void ShortcutCaptureButton::focusOutEvent(QFocusEvent* e) {
    // Clicking elsewhere abandons the capture rather than leaving a button
    // stuck saying "Press a key...".
    if (capturing_) end_capture();
    QPushButton::focusOutEvent(e);
}

void ShortcutCaptureButton::keyPressEvent(QKeyEvent* e) {
    if (!capturing_) {
        QPushButton::keyPressEvent(e);
        return;
    }

    // Esc on its own cancels. It costs nothing: a bare Esc is not a legal
    // binding, because only F1-F12 may be bound without Ctrl/Alt/Meta.
    if (e->key() == Qt::Key_Escape && e->modifiers() == Qt::NoModifier) {
        end_capture();
        e->accept();
        return;
    }

    const Combo c = combo_from_event(e->key(), e->modifiers());
    if (!c.bound()) {
        // A modifier on its own, or a key outside the bounded vocabulary.
        // A held modifier is not an error — keep waiting for the real key.
        switch (e->key()) {
            case Qt::Key_Shift: case Qt::Key_Control:
            case Qt::Key_Alt:   case Qt::Key_Meta:
            case Qt::Key_AltGr:
                e->accept();
                return;
            default:
                break;
        }
        end_capture();
        emit combo_refused(tr("That key is not one jnext can bind."));
        e->accept();
        return;
    }

    std::string why;
    if (!validate_combo(c, why)) {
        end_capture();
        emit combo_refused(tr("%1 cannot be used: %2.")
                               .arg(QString::fromStdString(render_combo(c)),
                                    QString::fromStdString(why)));
        e->accept();
        return;
    }

    capturing_ = false;
    // The owning tab decides whether this collides with another action; it
    // answers by calling set_combo() with either the new value or the old one.
    emit combo_captured(c);
    refresh_text();
    e->accept();
}
