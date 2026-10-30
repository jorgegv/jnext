#include "debugger/source_panel.h"

#include "debug/debugger.h"
#include "debug/ram_page.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

#include <filesystem>

SourcePanel::SourcePanel(const jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent), dbg_(dbg)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    // No '&' anywhere: these share the window's Alt namespace with the menu
    // bar (accel_test DACC-04), and the buttons have no key (the same verbs are Debug > Source actions, keyed from the keymap). The
    // captions do not repeat the toolbar's ("Step Back" & co.): the window's
    // buttons are found by caption.
    auto* steps = new QHBoxLayout();
    steps->setSpacing(4);
    steps->addWidget(new QLabel(tr("Source step:"), this));
    auto button = [this, steps](const QString& text, const QString& tip, auto signal) {
        auto* b = new QPushButton(text, this);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, signal);
        steps->addWidget(b);
        return b;
    };
    into_btn_ = button(tr("Into"), tr("Run to the next source statement"),
                       &SourcePanel::step_into_requested);
    over_btn_ = button(tr("Over"),
                       tr("Run to the next source statement, not inside a deeper call"),
                       &SourcePanel::step_over_requested);
    out_btn_ = button(tr("Out"), tr("Run until a statement in the calling routine"),
                      &SourcePanel::step_out_requested);
    back_btn_ = button(tr("Back"),
                       tr("Rewind to the previous source statement (needs trace and rewind)"),
                       &SourcePanel::step_back_requested);
    reverse_btn_ = button(tr("Reverse"),
                          tr("Rewind to the previous source breakpoint (needs trace and rewind)"),
                          &SourcePanel::reverse_continue_requested);
    steps->addStretch();
    layout->addLayout(steps);

    auto* loads = new QHBoxLayout();
    loads->setSpacing(4);
    auto* load_sld = new QPushButton(tr("Load Source Map..."), this);
    load_sld->setToolTip(tr("Load a source map: an sjasmplus SLD or a z88dk -debug map"));
    connect(load_sld, &QPushButton::clicked, this, &SourcePanel::load_sld_requested);
    loads->addWidget(load_sld);
    auto* load_symbols = new QPushButton(tr("Load Memory.txt..."), this);
    load_symbols->setToolTip(tr("Load NextBuild / Boriel ZX Basic symbols"));
    connect(load_symbols, &QPushButton::clicked, this, &SourcePanel::load_symbols_requested);
    loads->addWidget(load_symbols);
    loads->addStretch();
    layout->addLayout(loads);

    location_label_ = new QLabel(tr("No source map loaded"), this);
    location_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // A refused map's message names its full path before the reason; wrapped,
    // the reason stays readable in a narrow tab.
    location_label_->setWordWrap(true);
    layout->addWidget(location_label_);

    editor_ = new QPlainTextEdit(this);
    editor_->setReadOnly(true);
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    editor_->setFont(mono);
    layout->addWidget(editor_, 1);

    update_buttons(false);
}

QString SourcePanel::location_text() const { return location_label_->text(); }

QString SourcePanel::resolve_source_file(const std::string& source_name) const
{
    // A record names its file as the assembler saw it: absolute, or relative
    // to wherever the build ran. Relative names are tried against the map's
    // own directory and each of its parents, which covers the usual layouts
    // (the SLD beside the binary in a build directory under the sources).
    namespace fs = std::filesystem;
    const fs::path source(source_name);
    std::error_code ec;
    if (source.is_absolute())
        return fs::is_regular_file(source, ec) ? QString::fromStdString(source.string())
                                               : QString();
    if (!dbg_ || dbg_->source_map().loaded_file().empty()) return {};

    fs::path root = fs::path(dbg_->source_map().loaded_file()).parent_path();
    while (!root.empty()) {
        const fs::path candidate = root / source;
        ec.clear();
        if (fs::is_regular_file(candidate, ec))
            return QString::fromStdString(candidate.string());
        const fs::path parent = root.parent_path();
        if (parent == root) break;
        root = parent;
    }
    return {};
}

void SourcePanel::show_message(const QString& message)
{
    location_label_->setText(message);
    if (!loaded_source_.isEmpty()) {
        editor_->clear();
        loaded_source_.clear();
        loaded_mtime_ = -1;
    }
    editor_->setExtraSelections({});
    highlighted_line_ = 0;
}

void SourcePanel::show_running()
{
    location_label_->setText(tr("Running"));
    editor_->setExtraSelections({});
    highlighted_line_ = 0;
}

void SourcePanel::update_buttons(bool paused)
{
    const bool mapped = dbg_ && !dbg_->source_map().empty();
    const bool forward = paused && mapped;
    // The backward steps need what Step Back needs: a rewind the backend would
    // not refuse, and the trace to find the target in.
    const bool backward = forward && !dbg_->rewind_blocked().has_value() &&
                          dbg_->trace_enabled();
    emit steps_available(forward, backward);
    into_btn_->setEnabled(forward);
    over_btn_->setEnabled(forward);
    out_btn_->setEnabled(forward);
    back_btn_->setEnabled(backward);
    reverse_btn_->setEnabled(backward);
}

void SourcePanel::refresh()
{
    if (!dbg_) return;
    const bool paused = dbg_->state().paused;
    update_buttons(paused);
    if (!paused) {
        show_running();
        return;
    }

    const auto status = dbg_->source_map_status();
    if (status.state == jnext::dbg::Debugger::SourceMapStatus::State::Stale) {
        show_message(tr("Source map '%1' does not match the program in memory: %2. "
                        "Load it again to use it.")
                         .arg(QString::fromStdString(status.file),
                              QString::fromStdString(status.reason)));
        return;
    }
    if (status.state == jnext::dbg::Debugger::SourceMapStatus::State::None) {
        show_message(status.reason.empty()
                         ? tr("No source map loaded")
                         : tr("No source map loaded: %1")
                               .arg(QString::fromStdString(status.reason)));
        return;
    }

    // GH #344 stage 3 - a tape program's map is attached and works, but its identity is
    // judged where the program first runs its code: say so until it is.
    const QString note = status.pending
        ? tr("\nSource map not verified yet: it is checked when the program first runs its code")
        : QString();

    const uint16_t pc = dbg_->registers().PC;
    const uint8_t page = ram_page_of(dbg_->source_page(pc));
    // Upper-case hex, as the rest of the debugger and the user guide print it.
    const QString pc_text = QString::asprintf("%04X", pc);
    const QString where = page == NOT_RAM_PAGE ? tr("ROM or overlay")
                                               : tr("page %1").arg(QString::asprintf("%02X", page));
    const SourceLocation* location = dbg_->source_location(pc);
    if (!location) {
        show_message(tr("$%1 (%2): no source mapping; use the disassembly")
                         .arg(pc_text).arg(where) + note);
        return;
    }

    const QString source_path = resolve_source_file(location->file);
    if (source_path.isEmpty()) {
        show_message(tr("%1:%2 - source file not found")
                         .arg(QString::fromStdString(location->file))
                         .arg(location->line) + note);
        return;
    }
    // Reloaded when the file changes on disk, so an edit-and-rebuild cycle
    // never highlights a line of the old text.
    const qint64 mtime = QFileInfo(source_path).lastModified().toMSecsSinceEpoch();
    if (source_path != loaded_source_ || mtime != loaded_mtime_) {
        QFile file(source_path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            show_message(tr("Could not open %1").arg(source_path));
            return;
        }
        editor_->setPlainText(QString::fromUtf8(file.readAll()));
        loaded_source_ = source_path;
        loaded_mtime_ = mtime;
        placed_ = false;   // a new text, or another file, starts at its top
    }

    location_label_->setText(tr("%1:%2  $%3  %4")
                                 .arg(QString::fromStdString(location->file))
                                 .arg(location->line)
                                 .arg(pc_text)
                                 .arg(where) + note);
    const QTextBlock block = editor_->document()->findBlockByNumber(location->line - 1);
    if (!block.isValid()) {
        editor_->setExtraSelections({});
        highlighted_line_ = 0;
        return;
    }

    QTextEdit::ExtraSelection selection;
    selection.cursor = QTextCursor(block);
    selection.cursor.clearSelection();
    selection.format.setBackground(editor_->palette().highlight());
    selection.format.setForeground(editor_->palette().highlightedText());
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    editor_->setExtraSelections({selection});
    // Placed only when something moved. This runs on every tick while paused,
    // and centring again each time would snap the view back to the line
    // whenever the user scrolled away from it.
    if (!placed_ || location->line != placed_line_ || pc != placed_pc_) {
        editor_->setTextCursor(selection.cursor);
        editor_->centerCursor();
        placed_ = true;
        placed_line_ = location->line;
        placed_pc_ = pc;
    }
    highlighted_line_ = location->line;
}
