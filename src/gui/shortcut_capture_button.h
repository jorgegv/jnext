#pragma once

// GH #1 — the "press the key you want" button used by Preferences > Debugger
// Keys. One button per bindable action.
//
// Not QKeySequenceEdit: that widget captures up to four chords, speaks Qt's
// whole Qt::Key vocabulary rather than the bounded one this project can test
// and document, and has no way to refuse a combination WITH A REASON. Refusing
// at the keystroke, by name, is the entire user-facing half of this feature.

#include <QPushButton>

#include "debug/debug_keymap.h"

/// Shows a combination; click it and the next chord you press becomes the new
/// one. Esc (on its own) cancels — it is not a legal binding anyway, since
/// every non-function key needs Ctrl, Alt or Meta.
class ShortcutCaptureButton : public QPushButton {
    Q_OBJECT
public:
    explicit ShortcutCaptureButton(QWidget* parent = nullptr);

    const jnext::dbgkeys::Combo& combo() const { return combo_; }

    /// Set the shown combination WITHOUT emitting anything. Used to seed the
    /// row and to put a refused capture back.
    void set_combo(const jnext::dbgkeys::Combo& c);

    /// Begin capture programmatically (what a click does). Exposed so a test
    /// can drive the real widget rather than a stand-in.
    void start_capture();
    bool capturing() const { return capturing_; }

signals:
    /// A legal, non-conflicting chord was captured.
    void combo_captured(jnext::dbgkeys::Combo combo);
    /// A chord was captured and REFUSED. `why` is a complete sentence fragment
    /// ready to show; the button has already put the old combination back.
    void combo_refused(QString why);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void end_capture();
    void refresh_text();

    jnext::dbgkeys::Combo combo_;
    bool capturing_ = false;
};
