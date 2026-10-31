#pragma once

#include <QPointer>
#include <QWidget>

#include <cstdint>
#include <string>
#include <vector>

class BreakpointModel;
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

    /// GH #420 - the directories searched for the files the map names, after the
    /// map's own directory and its parents (source_search::resolve). Takes effect
    /// at once; DebuggerManager owns the list and pushes it.
    void set_search_dirs(std::vector<std::string> dirs);
    const std::vector<std::string>& search_dirs() const { return search_dirs_; }

    /// GH #421 - the GUI's breakpoints, which the gutter's BP column draws and
    /// a click on it edits (the Disassembly's gutter follows the same model).
    void set_breakpoint_model(BreakpointModel* model);

    /// What the BP column draws on a line of the file shown (one-based).
    /// `Armed`/`Suspended`: a breakpoint at every place of the line (a disc, a
    /// ring); `Part*`: at some places only, or only inside the line (its left half).
    enum class LineMark { None, Armed, Suspended, PartArmed, PartSuspended };
    LineMark line_mark(int line) const;
    /// The gutter's hover words for a line: its breakpoint, or why it has none.
    QString line_tooltip(int line) const;
    /// The one toggle of a source line (the gutter click; the context menu of
    /// GH #423 calls it too): removes every breakpoint the line shows, else sets
    /// ONE group at every place the line compiled to. A line without code adds
    /// nothing and says so.
    void toggle_breakpoint_at_line(int line);
    /// The map's name of the file shown, empty when the tab shows a message.
    const std::string& shown_file() const { return shown_file_; }

    /// What the panel shows right now (for tests and the status line).
    QString location_text() const;
    /// The label's tooltip: the path of the file shown (empty when none is).
    QString location_tooltip() const;
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
    /// GH #420 - Locate File... found the directory that holds the file the map
    /// names: search it from now on.
    void source_dir_located(const QString& dir);
    /// ... and the user chose to keep it for later sessions.
    void source_dir_save_requested(const QString& dir);

private:
    void refresh_();
    void update_gutter();
    /// Everything the BP column derives for one line (see source_panel.cpp).
    struct LineState;
    LineState line_state(int line) const;
    QString resolve_source_file(const std::string& source_name) const;
    /// Locate File...: ask for the file the map names and derive the directory to search.
    void locate_file();
    /// The name the tab could not find, or empty; enables Locate File... exactly then.
    void set_unresolved(const std::string& name);
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
    QPushButton* locate_btn_ = nullptr;
    QPointer<BreakpointModel> bp_model_;
    std::string shown_file_;
    std::vector<std::string> search_dirs_;
    std::string unresolved_name_;
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
