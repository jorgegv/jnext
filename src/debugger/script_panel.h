#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstdint>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace jnext { namespace script { class ScriptHost; } }

/// GH #26 WP5 — the debugger window's Script tab (dsl-frontend.md §6.4).
///
/// The loaded scripts' rules (file, rule, event, state, hits), the run's
/// verdict line (pass / fail / unreached, the last stop, an `exit` a script
/// requested — which in the GUI pauses rather than ends the program) and the
/// script log. Load / Reload / Unload act on the loop owner's `ScriptHost`,
/// the same one `--script` loads into; the panel reaches the machine through
/// nothing else (the script engine is a backend client).
class ScriptPanel : public QWidget {
    Q_OBJECT
public:
    explicit ScriptPanel(QWidget* parent = nullptr);

    /// The host the panel drives; null shows the panel as unavailable.
    void set_host(jnext::script::ScriptHost* host);

    /// Load one script; false with `errors` (one `file:line:column: message`
    /// per line) when it did not load. The menu's Load Script… and the Load
    /// button call this after their file dialog.
    bool load_path(const QString& path, QString* errors = nullptr);
    /// Reload every loaded script; the combined errors, empty on success.
    QString reload_all();
    /// Unload every script.
    void unload_all();

    /// Bring the table, the verdict line and the log up to date.
    void refresh();

    /// The verdict line as shown (rows).
    QString verdict_text() const;
    /// The log as shown (rows).
    QStringList log_lines() const;
    QTableWidget* rule_table() const { return table_; }
    jnext::script::ScriptHost* host() const { return host_; }

    QSize sizeHint() const override { return QSize(380, 400); }

public slots:
    /// Load… : a file dialog, then load_path(); errors in a message box.
    void on_load_clicked();
    void on_reload_clicked();
    void on_unload_clicked();

private:
    jnext::script::ScriptHost* host_ = nullptr;
    QPushButton*    load_btn_   = nullptr;
    QPushButton*    reload_btn_ = nullptr;
    QPushButton*    unload_btn_ = nullptr;
    QLabel*         verdict_    = nullptr;
    QTableWidget*   table_      = nullptr;
    QPlainTextEdit* log_        = nullptr;
    uint64_t        log_seq_    = 0;
};
