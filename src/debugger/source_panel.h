#pragma once

#include <QWidget>

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
/// Debug menu's actions do. They live on the tab, not in the menu bar, and
/// carry no mnemonic and no shortcut: the debugger's menu tree and its key
/// bindings are pinned by the accelerator and keymap suites.
class SourcePanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the backend every read goes through; null shows nothing.
    explicit SourcePanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    void refresh();

    /// What the panel shows right now (for tests and the status line).
    QString location_text() const;
    /// One-based line highlighted in the editor, 0 when none.
    int highlighted_line() const { return highlighted_line_; }

signals:
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
    void update_buttons(bool paused);

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
};
