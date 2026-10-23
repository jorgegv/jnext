#pragma once

#include <QWidget>

#include <cstdint>
#include <string>

class QLabel;
class QPlainTextEdit;
class QPushButton;
namespace jnext { namespace dbg { class Debugger; } }

/// CAP-SRC — the Source tab: the source file the PC's statement came from,
/// with that line highlighted, read through the backend's source map
/// (doc/design/SOURCE-LEVEL-DEBUGGING.md), and the source-step buttons.
///
/// The buttons only REQUEST; DebuggerManager runs the verbs, exactly as the
/// Debug menu's actions do. The buttons carry no mnemonic and no shortcut;
/// the same verbs are Debug > Source actions on the same manager slots, keyed
/// from the keymap and enabled through steps_available().
class SourcePanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the backend every read goes through; null shows nothing.
    explicit SourcePanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    void refresh();
    /// The machine stopped (a breakpoint, a step, a Pause, a rewind): the next
    /// refresh puts the view on the current line even when the line and the PC
    /// are the ones it last showed and the user has scrolled away since.
    void follow_pc() { placed_ = false; }
    /// Enable the step buttons for this state, without re-reading anything.
    void update_buttons(bool paused);

    /// What the panel shows right now (for tests and the status line).
    QString location_text() const;
    /// One-based line highlighted in the editor, 0 when none.
    int highlighted_line() const { return highlighted_line_; }

signals:
    /// Emitted by update_buttons() with the two enabled states it applied.
    void steps_available(bool forward, bool backward);
    void step_into_requested();
    void step_over_requested();
    void step_out_requested();
    void step_back_requested();
    void reverse_continue_requested();
    void load_sld_requested();
    void load_symbols_requested();

private:
    QString resolve_source_file(const std::string& source_name) const;
    void show_message(const QString& message);
    /// While the machine runs there is no current line: say so, and drop the
    /// highlight of the last stop.
    void show_running();

    const jnext::dbg::Debugger* dbg_ = nullptr;
    QLabel* location_label_ = nullptr;
    QPlainTextEdit* editor_ = nullptr;
    QPushButton* into_btn_ = nullptr;
    QPushButton* over_btn_ = nullptr;
    QPushButton* out_btn_ = nullptr;
    QPushButton* back_btn_ = nullptr;
    QPushButton* reverse_btn_ = nullptr;
    QString loaded_source_;
    qint64 loaded_mtime_ = -1;
    int highlighted_line_ = 0;
    // Where the view was last put: the cursor is moved and centred only when
    // the line or the PC differ, or the text was loaded since (refresh() runs
    // on every tick while paused, and must not undo the user's scrolling).
    bool placed_ = false;
    int placed_line_ = 0;
    uint16_t placed_pc_ = 0;
};
