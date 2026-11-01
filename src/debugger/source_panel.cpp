#include "debugger/source_panel.h"

#include "debugger/breakpoint_glyph.h"
#include "debugger/breakpoint_model.h"
#include "qt/palette_tint_qt.h"
#include "debug/debugger.h"
#include "debug/ram_page.h"
#include "debug/source_search.h"
#include "debug/disasm.h"
#include "debug/disasm_text.h"
#include "debug/symbol_table.h"
#include "debugger/operand_menu.h"
#include "debugger/source_highlighter.h"
#include "debugger/watch_panel.h"

#include <QAction>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QFileDialog>
#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

// GH #421 - the Source tab's editor: the stock read-only text view with a gutter
// (`BP | Line`) in a viewport margin and a header strip above it, the canonical Qt
// code-editor arrangement. A subclass because the geometry of the visible blocks
// is protected. The marks themselves are SourcePanel's (line_mark()).
namespace {

// The lines of the file shown: the final newline does not start one (GH #420).
int file_line_count(const QTextDocument* doc)
{
    return doc->blockCount() - (doc->lastBlock().length() <= 1 ? 1 : 0);
}

// GH #347 - the panel's own cursor moves (a placement, a load) are not the user's: while one of
// these is alive the editor's cursor signals announce nothing.
struct SyncScope {
    bool& flag;
    bool old;
    explicit SyncScope(bool& f) : flag(f), old(f) { flag = true; }
    ~SyncScope() { flag = old; }
};

class SourceEdit;

class SourceGutter : public QWidget {
public:
    SourceGutter(SourceEdit* edit, QWidget* parent, SourcePanel* panel);
protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void contextMenuEvent(QContextMenuEvent* ev) override;
private:
    int line_at(const QMouseEvent* ev) const;
    SourceEdit*  edit_;
    SourcePanel* panel_;
};

class SourceGutterHeader : public QWidget {
public:
    SourceGutterHeader(SourceEdit* edit, QWidget* parent) : QWidget(parent), edit_(edit) {}
protected:
    void paintEvent(QPaintEvent*) override;
private:
    SourceEdit* edit_;
};

class SourceEdit : public QPlainTextEdit {
public:
    explicit SourceEdit(SourcePanel* panel, QWidget* parent) : QPlainTextEdit(parent), panel_(panel)
    {
        gutter_ = new SourceGutter(this, this, panel);
        gutter_->setObjectName(QStringLiteral("source_gutter"));
        gutter_->setAccessibleName(QCoreApplication::translate("SourcePanel", "Breakpoints and line numbers"));
        header_ = new SourceGutterHeader(this, this);
        header_->setObjectName(QStringLiteral("source_gutter_header"));
        connect(this, &QPlainTextEdit::blockCountChanged, this, [this]() { update_margins(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& r, int dy) {
            if (dy) gutter_->scroll(0, dy);
            else    gutter_->update(0, r.y(), gutter_->width(), r.height());
        });
        update_margins();
    }

    QFont header_font() const
    {
        QFont f = font();
        f.setBold(true);
        f.setPointSize(8);
        return f;
    }
    int header_height() const { return QFontMetrics(header_font()).height() + 4; }
    int gutter_width() const
    {
        const int digits = std::max(3, static_cast<int>(QString::number(blockCount()).size()));
        const int numbers = fontMetrics().horizontalAdvance(QString(digits, QLatin1Char('9')));
        const int title = QFontMetrics(header_font()).horizontalAdvance(QStringLiteral("Line"));
        return kBreakpointColumnWidth + 4 + std::max(numbers, title) + 6;
    }
    void update_margins()
    {
        setViewportMargins(gutter_width(), header_height(), 0, 0);
        place_children();
        gutter_->update();
        header_->update();
    }
    void update_gutter() { gutter_->update(); }

    /// The one-based line under viewport-local `y`, or 0.
    int line_at(int y) const
    {
        QTextBlock b = firstVisibleBlock();
        qreal top = blockBoundingGeometry(b).translated(contentOffset()).top();
        for (; b.isValid(); b = b.next()) {
            if (y < top) return 0;
            const qreal h = blockBoundingRect(b).height();
            if (y < top + h)   // the final newline's blank block is no line (as painted and clicked)
                return b.blockNumber() + 1 <= file_line_count(document()) ? b.blockNumber() + 1 : 0;
            top += h;
        }
        return 0;
    }

    void paint_gutter(QPaintEvent* ev)
    {
        QPainter p(gutter_);
        const QPalette pal = panel_->palette();   // GH #422: the scheme sits on the editor's palette, not here
        p.fillRect(ev->rect(), palette_tint::surface(pal, QColor(255, 255, 255)));
        p.setPen(palette_tint::surface(pal, QColor(200, 200, 200)));
        p.drawLine(kBreakpointColumnWidth, ev->rect().top(), kBreakpointColumnWidth, ev->rect().bottom());
        p.drawLine(gutter_->width() - 1, ev->rect().top(), gutter_->width() - 1, ev->rect().bottom());
        if (panel_->shown_file().empty()) return;
        p.setFont(font());
        QTextBlock block = firstVisibleBlock();
        qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
        while (block.isValid() && top <= ev->rect().bottom()) {
            const qreal h = blockBoundingRect(block).height();
            if (block.isVisible() && top + h >= ev->rect().top() &&
                block.blockNumber() < file_line_count(document())) {
                const int line = block.blockNumber() + 1;
                using LM = SourcePanel::LineMark;
                const LM mark = panel_->line_mark(line);
                if (mark != LM::None)
                    paint_breakpoint_glyph(p, QPoint(kBreakpointColumnWidth / 2, int(top + h / 2)),
                                           mark == LM::Armed || mark == LM::PartArmed,
                                           mark == LM::PartArmed || mark == LM::PartSuspended);
                p.setPen(palette_tint::ink(pal, QColor(100, 100, 100)));
                p.drawText(QRect(kBreakpointColumnWidth + 4, int(top),
                                 gutter_->width() - kBreakpointColumnWidth - 4 - 6, int(h)),
                           Qt::AlignRight | Qt::AlignVCenter, QString::number(line));
                // GH #347 - the PC's line, apart from a selected one: the strip after the numbers.
                if (line == panel_->highlighted_line())
                    paint_pc_marker(p, QRect(gutter_->width() - 6, int(top), 5, int(h)),
                                    palette_tint::ink(pal, pc_marker_light()));
            }
            top += h;
            block = block.next();
        }
    }

    void paint_header()
    {
        QPainter p(header_);
        const QPalette pal = panel_->palette();
        p.fillRect(header_->rect(), palette_tint::surface(pal, QColor(255, 255, 255)));
        p.setFont(header_font());
        p.setPen(palette_tint::ink(pal, QColor(80, 80, 80)));
        const int h = header_->height();
        p.drawText(QRect(2, 0, kBreakpointColumnWidth - 2, h), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("BP"));
        p.drawText(QRect(kBreakpointColumnWidth + 4, 0, header_->width() - kBreakpointColumnWidth - 4, h),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Line"));
        p.setPen(palette_tint::surface(pal, QColor(200, 200, 200)));
        p.drawLine(kBreakpointColumnWidth, 0, kBreakpointColumnWidth, h);
        p.drawLine(header_->width() - 1, 0, header_->width() - 1, h);
    }

protected:
    // GH #423 - the stock menu (Copy, Select All, Copy Link Location) gives way to
    // the Disassembly's actions on the line and the word under the pointer.
    void contextMenuEvent(QContextMenuEvent* ev) override
    {
        const QTextCursor at = ev->reason() == QContextMenuEvent::Keyboard
                                   ? textCursor() : cursorForPosition(ev->pos());
        const int line = panel_->shown_file().empty() ? 0
                         : ev->reason() == QContextMenuEvent::Keyboard ? at.blockNumber() + 1
                                                                       : line_at(ev->pos().y());
        QMenu menu(this);
        panel_->fill_context_menu(menu, line, at.positionInBlock());
        menu.exec(ev->globalPos());
    }
    // Enter is Run to Here on the cursor line, as in the Disassembly.
    void keyPressEvent(QKeyEvent* ev) override
    {
        if (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter) {
            panel_->run_to_cursor();
            ev->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(ev);
    }
    void resizeEvent(QResizeEvent* ev) override
    {
        QPlainTextEdit::resizeEvent(ev);
        place_children();
    }
    void changeEvent(QEvent* ev) override
    {
        QPlainTextEdit::changeEvent(ev);
        if (ev->type() == QEvent::FontChange && gutter_) update_margins();
    }

private:
    void place_children()
    {
        const QRect cr = contentsRect();
        const int w = gutter_width();
        gutter_->setGeometry(QRect(cr.left(), viewport()->geometry().top(), w, viewport()->height()));
        header_->setGeometry(QRect(cr.left(), cr.top(), w, header_height()));
    }

    SourcePanel* panel_;
    QWidget* gutter_ = nullptr;
    QWidget* header_ = nullptr;
};

SourceGutter::SourceGutter(SourceEdit* edit, QWidget* parent, SourcePanel* panel)
    : QWidget(parent), edit_(edit), panel_(panel)
{
    setMouseTracking(true);
}

void SourceGutter::paintEvent(QPaintEvent* ev) { edit_->paint_gutter(ev); }

int SourceGutter::line_at(const QMouseEvent* ev) const
{
    // Qt5/Qt6 (GH #108): position() is Qt6 API; Qt 5.15 spells it localPos().
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF pos = ev->position();
#else
    const QPointF pos = ev->localPos();
#endif
    return panel_->shown_file().empty() ? 0 : edit_->line_at(static_cast<int>(pos.y()));
}

void SourceGutter::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) return;   // the right button is the context menu's (GH #423)
    const int line = line_at(ev);
    if (line > 0) panel_->toggle_breakpoint_at_line(line);
}

void SourceGutter::mouseMoveEvent(QMouseEvent* ev)
{
    const int line = line_at(ev);
    setToolTip(line > 0 ? panel_->line_tooltip(line) : QString());
}

void SourceGutter::contextMenuEvent(QContextMenuEvent* ev)
{
    // GH #423 - the line menu, without a word under the cursor.
    const int line = panel_->shown_file().empty() ? 0 : edit_->line_at(ev->pos().y());
    if (line < 1) return;
    QMenu menu(this);
    panel_->fill_context_menu(menu, line, -1);
    menu.exec(ev->globalPos());
}

void SourceGutterHeader::paintEvent(QPaintEvent*) { edit_->paint_header(); }

}  // namespace

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
    locate_btn_ = new QPushButton(tr("Locate File..."), this);
    locate_btn_->setToolTip(tr("Find the source file the map names; its directory is searched from now on"));
    locate_btn_->setEnabled(false);
    connect(locate_btn_, &QPushButton::clicked, this, &SourcePanel::locate_file);
    loads->addWidget(locate_btn_);
    // GH #347 - after the Disassembly led the view away from the PC. The caption is not the
    // Disassembly's "Go to PC" (the window's buttons are found by caption).
    goto_pc_btn_ = new QPushButton(tr("Go to PC Line"), this);
    goto_pc_btn_->setToolTip(tr("Show the line the PC is on again"));
    goto_pc_btn_->setEnabled(false);
    connect(goto_pc_btn_, &QPushButton::clicked, this, &SourcePanel::go_to_pc_line);
    loads->addWidget(goto_pc_btn_);
    loads->addStretch();
    layout->addLayout(loads);

    location_label_ = new QLabel(tr("No source map loaded"), this);
    location_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // A refused map's message names its full path before the reason; wrapped,
    // the reason stays readable in a narrow tab.
    location_label_->setWordWrap(true);
    layout->addWidget(location_label_);

    editor_ = new SourceEdit(this, this);
    editor_->setReadOnly(true);
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    editor_->setFont(mono);
    layout->addWidget(editor_, 1);
    // GH #422 - colours the text by language; a child of the panel, so tests find it.
    highlighter_ = new SourceHighlighter(this);
    highlighter_->setDocument(editor_->document());

    // GH #423 - the clipboard commands are QActions on the editor, as the
    // Disassembly's are on its widget: the menu shows the chord, and the chord
    // fires only while the text has focus.
    copy_action_ = new QAction(tr("Copy"), this);
    copy_action_->setShortcut(QKeySequence::Copy);
    copy_action_->setShortcutContext(Qt::WidgetShortcut);
    connect(copy_action_, &QAction::triggered, this, [this]() { editor_->copy(); });
    editor_->addAction(copy_action_);
    copy_addresses_action_ = new QAction(tr("Copy with Addresses"), this);
    copy_addresses_action_->setToolTip(tr("Copy the selected lines with their file, line and addresses"));
    connect(copy_addresses_action_, &QAction::triggered, this, [this]() {
        const QString text = selection_text_with_addresses();
        if (!text.isEmpty()) QGuiApplication::clipboard()->setText(text);
    });
    select_all_action_ = new QAction(tr("Select All"), this);
    select_all_action_->setShortcut(QKeySequence::SelectAll);
    select_all_action_->setShortcutContext(Qt::WidgetShortcut);
    connect(select_all_action_, &QAction::triggered, this, [this]() { editor_->selectAll(); });
    editor_->addAction(select_all_action_);

    // GH #347 - a cursor or selection move the USER made tells the Disassembly (see emit_lines_()).
    auto user_move = [this]() {
        if (!syncing_ && !shown_file_.empty()) emit_lines_(true, false);
    };
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, user_move);
    connect(editor_, &QPlainTextEdit::selectionChanged, this, user_move);

    update_buttons(false);
}

QString SourcePanel::location_text() const { return location_label_->text(); }

QString SourcePanel::location_tooltip() const { return location_label_->toolTip(); }

QString SourcePanel::resolve_source_file(const std::string& source_name) const
{
    // GH #420 - the walk is source_search::resolve's: an absolute name, then the
    // map's directory and its parents, then the search path.
    const std::string map = dbg_ ? dbg_->source_map().loaded_file() : std::string();
    return QString::fromStdString(source_search::resolve(source_name, map, search_dirs_));
}

void SourcePanel::set_search_dirs(std::vector<std::string> dirs)
{
    search_dirs_ = std::move(dirs);
    refresh();
}

void SourcePanel::set_unresolved(const std::string& name)
{
    unresolved_name_ = name;
    locate_btn_->setEnabled(!name.empty());
}

void SourcePanel::locate_file()
{
    if (unresolved_name_.empty()) return;
    const QString base = QFileInfo(QString::fromStdString(unresolved_name_)).fileName();
    const QString start = QFileInfo(QString::fromStdString(dbg_->source_map().loaded_file())).absolutePath();
    const QString picked = QFileDialog::getOpenFileName(
        this, tr("Locate %1").arg(base), start, tr("%1 (%1);;All files (*)").arg(base));
    if (picked.isEmpty()) return;
    const QString root = QString::fromStdString(
        source_search::root_for(unresolved_name_, picked.toStdString()));
    if (root.isEmpty()) {
        QMessageBox::warning(this, tr("Locate File"),
                             tr("%1 is not %2. Choose the file the source map names.")
                                 .arg(QFileInfo(picked).fileName(), base));
        return;
    }
    emit source_dir_located(root);   // the manager pushes the list back, which refreshes the tab
    const auto keep = QMessageBox::question(
        this, tr("Locate File"),
        tr("Source files are now also searched for in:\n%1\n\n"
           "Save this directory in Preferences (Paths > Source search path) for later sessions?")
            .arg(root),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (keep == QMessageBox::Yes) emit source_dir_save_requested(root);
}

void SourcePanel::show_message(const QString& message)
{
    location_label_->setText(message);
    location_label_->setToolTip(QString());
    shown_file_.clear();
    browse_addr_.reset();   // GH #347: no map, or no line to show
    if (!loaded_source_.isEmpty()) {
        SyncScope sync(syncing_);
        highlighter_->set_language(SourceLanguage::Plain);
        editor_->clear();
        loaded_source_.clear();
        loaded_mtime_ = -1;
    }
    editor_->setExtraSelections({});
    highlighted_line_ = 0;
    emit_lines_(false, false);
}

void SourcePanel::show_running()
{
    location_label_->setText(tr("Running"));
    location_label_->setToolTip(QString());
    editor_->setExtraSelections({});
    highlighted_line_ = 0;
    browse_addr_.reset();     // GH #347: the stop that follows shows the PC
    disasm_range_.reset();
    emit_lines_(false, false);
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

SourceLanguage SourcePanel::source_language() const { return highlighter_->language(); }

// GH #422 - THE way a text enters the editor: the language is chosen first, so the
// text is highlighted once, with its own language.
void SourcePanel::set_source_text(const QString& text, const std::string& file)
{
    SyncScope sync(syncing_);   // GH #347: a load puts the cursor at the top; that is nobody's selection
    highlighter_->set_language(source_language_for(file, text));
    editor_->setPlainText(text);
}

// GH #422 stage 2 - the scheme in force: resolved against the PANEL's palette (the
// application's; never the editor's, which carries the scheme), its class colours go to
// the highlighter and its background, text and current-line bar to the editor's palette.
// The gutter and its header read the panel's palette, so no scheme reaches them.
void SourcePanel::apply_scheme_()
{
    highlighter_->apply_palette(palette());   // rehighlights only when a class colour changed
    const SourceHighlighter::SchemeColours& c = highlighter_->colours_in_force();
    QPalette p = editor_->palette();
    if (p.color(QPalette::Base) == c.background && p.color(QPalette::Text) == c.text &&
        p.color(QPalette::Highlight) == c.pc_background && p.color(QPalette::HighlightedText) == c.pc_text)
        return;
    for (auto g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(g, QPalette::Base, c.background);
        p.setColor(g, QPalette::Text, c.text);
        p.setColor(g, QPalette::Highlight, c.pc_background);
        p.setColor(g, QPalette::HighlightedText, c.pc_text);
    }
    editor_->setPalette(p);
}

void SourcePanel::set_colour_scheme(SourceScheme scheme)
{
    highlighter_->set_scheme(scheme);
    apply_scheme_();   // at once: a Preferences Apply does not wait for a paused tick
}

void SourcePanel::refresh()
{
    apply_scheme_();
    refresh_();
    goto_pc_btn_->setEnabled(browse_addr_.has_value());
    update_gutter();   // the marks follow the memory mapped now
}

// GH #347 (OD-7) - the other places the toolchain attributes `address` to, after the
// primary one the label names: "  also: f:l, f:l" in lookup_all() order, without the
// primary's own file:line (a column-only difference) or a repeat.
QString SourcePanel::also_text(uint16_t address) const
{
    const auto all = dbg_->source_map().lookup_all(dbg_->source_page(address), address);
    std::vector<std::pair<std::string, int>> seen;
    QStringList others;
    for (size_t i = 0; i < all.size(); ++i) {
        const auto key = std::make_pair(all[i]->file, all[i]->line);
        if (i == 0) { seen.push_back(key); continue; }
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        others << QStringLiteral("%1:%2").arg(QString::fromStdString(key.first)).arg(key.second);
    }
    return others.isEmpty() ? QString() : tr("  also: %1").arg(others.join(QStringLiteral(", ")));
}

void SourcePanel::refresh_()
{
    if (!dbg_) return;
    set_unresolved({});
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
    // GH #351 - a dot command's is judged when the command starts.
    const QString note = status.pending
        ? (status.dot ? tr("\nSource map not verified yet: it is checked when the dot command runs")
                      : tr("\nSource map not verified yet: it is checked when the program first runs its code"))
        : QString();

    const uint16_t pc = dbg_->registers().PC;
    const SourceLocation* const pc_loc = dbg_->source_location(pc);
    // GH #347 - the label and the view describe the address the Disassembly led to while
    // browsing, else the PC's. A browse ends with its map, or when its address has no line now.
    if (browse_addr_ && dbg_->source_map().loaded_file() != browse_map_) browse_addr_.reset();
    const SourceLocation* location = pc_loc;
    uint16_t addr = pc;
    if (browse_addr_) {
        if (const SourceLocation* b = dbg_->source_location(*browse_addr_)) {
            location = b;
            addr = *browse_addr_;
        } else {
            browse_addr_.reset();
        }
    }
    const bool browsing = browse_addr_.has_value();
    const uint8_t page = ram_page_of(dbg_->source_page(addr));
    // Upper-case hex, as the rest of the debugger and the user guide print it.
    const QString pc_text = QString::asprintf("%04X", addr);
    const QString where = page == NOT_RAM_PAGE ? tr("ROM or overlay")
                                               : tr("page %1").arg(QString::asprintf("%02X", page));
    if (!location) {
        show_message(tr("$%1 (%2): no source mapping; use the disassembly")
                         .arg(pc_text).arg(where) + note);
        return;
    }

    // GH #348 - the start-up code's m4 output, which the build did not keep, is in the
    // listing (`prog.lis`) the map was loaded with: shown from there, labelled so.
    const SourceText* embedded = dbg_->source_map().embedded_text(location->file);
    QString source_path;
    if (embedded) {
        // A key no real path can equal, so switching back to a disk file reloads it.
        source_path = QStringLiteral("\x01m4:") + QString::fromStdString(location->file) +
                      QLatin1Char('\x01') + QString::fromStdString(embedded->origin) + QLatin1Char('\x01') +
                      QString::number(reinterpret_cast<quintptr>(embedded), 16) + QLatin1Char(':') +
                      QString::number(embedded->lines.size());
        if (source_path != loaded_source_) {
            QStringList lines;
            lines.reserve(static_cast<int>(embedded->lines.size()));
            for (const std::string& l : embedded->lines) lines << QString::fromUtf8(l.c_str());
            set_source_text(lines.join(QLatin1Char('\n')), location->file);
            loaded_source_ = source_path;
            loaded_mtime_ = -1;
            placed_ = false;
        }
    } else {
        // GH #420 - a z88dk record in a `.m4` file (the CRT): the assembler numbered the
        // lines of the m4 OUTPUT, which the build did not keep, so no file on disk has them.
        if (dbg_->source_map().format() == "z88dk" &&
            QString::fromStdString(location->file).endsWith(QLatin1String(".m4"), Qt::CaseInsensitive)) {
            show_message(tr("%1:%2 - the line counts the m4 output of this file, which the build "
                            "did not keep; use the disassembly")
                             .arg(QString::fromStdString(location->file)).arg(location->line) + note);
            return;
        }
        source_path = resolve_source_file(location->file);
        if (source_path.isEmpty()) {
            show_message(tr("%1:%2 - source file not found")
                             .arg(QString::fromStdString(location->file))
                             .arg(location->line) + note);
            set_unresolved(location->file);
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
            set_source_text(QString::fromUtf8(file.readAll()), location->file);
            loaded_source_ = source_path;
            loaded_mtime_ = mtime;
            placed_ = false;   // a new text, or another file, starts at its top
        }
    }
    shown_file_ = location->file;

    location_label_->setToolTip(embedded ? QString::fromStdString(embedded->origin) : source_path);
    // GH #420 - a line past the end of the file found: it is not the text the build used.
    const QTextDocument* doc = editor_->document();
    const int file_lines = file_line_count(doc);
    if (location->line > file_lines) {
        location_label_->setText(tr("%1:%2 - %3 has only %4 lines; it is not the text the build used")
                                     .arg(QString::fromStdString(location->file))
                                     .arg(location->line).arg(source_path).arg(file_lines) + note);
        editor_->setExtraSelections({});
        highlighted_line_ = 0;
        return;
    }
    QString browse_text;
    if (browsing)
        browse_text = pc_loc ? tr("  (browsing; the PC is at %1:%2, $%3)")
                                   .arg(QString::fromStdString(pc_loc->file)).arg(pc_loc->line)
                                   .arg(QString::asprintf("%04X", pc))
                             : tr("  (browsing; the PC is in no source line)");
    location_label_->setText(
        (embedded ? tr("%1:%2  $%3  %4 (m4 output, from %5)")
                        .arg(QString::fromStdString(location->file))
                        .arg(location->line)
                        .arg(pc_text)
                        .arg(where)
                        .arg(QFileInfo(QString::fromStdString(embedded->origin)).fileName())
                  : tr("%1:%2  $%3  %4")
                        .arg(QString::fromStdString(location->file))
                        .arg(location->line)
                        .arg(pc_text)
                        .arg(where)) + also_text(addr) + browse_text + note);
    const QTextBlock block = editor_->document()->findBlockByNumber(location->line - 1);
    if (!block.isValid()) {
        editor_->setExtraSelections({});
        highlighted_line_ = 0;
        return;
    }

    // GH #388 - memory coverage: with it on, every line of this file the program
    // executed (an X byte in one of the line's ranges) has a pale full-width
    // background. X only: the R and W of a line's bytes have no per-source-line
    // meaning (a line's bytes are its code). The current-line selection is
    // appended LAST so it wins on its own line.
    QList<QTextEdit::ExtraSelection> selections;
    if (dbg_->mem_coverage_enabled()) {
        const QColor tint = highlighter_->colours_in_force().coverage_line;   // the scheme's (GH #422)
        for (const uint32_t line : dbg_->executed_source_lines(location->file)) {
            const QTextBlock b = doc->findBlockByNumber(static_cast<int>(line) - 1);
            if (line == 0 || !b.isValid()) continue;
            QTextEdit::ExtraSelection t;
            t.cursor = QTextCursor(b);
            t.cursor.clearSelection();
            t.format.setBackground(tint);
            t.format.setProperty(QTextFormat::FullWidthSelection, true);
            selections.append(t);
        }
    }

    // GH #347 - the lines of the Disassembly's selection, then the PC's line LAST so it wins on
    // its own: the PC's own line is the highlighted one only when the file shown is the PC's.
    if (disasm_range_) {
        const QColor linked = highlighter_->colours_in_force().linked_line;   // the scheme's
        for (const int line : linked_lines_(location->file)) {
            const QTextBlock b = doc->findBlockByNumber(line - 1);
            if (line < 1 || line > file_lines || !b.isValid()) continue;
            QTextEdit::ExtraSelection t;
            t.cursor = QTextCursor(b);
            t.cursor.clearSelection();
            t.format.setBackground(linked);
            t.format.setProperty(QTextFormat::FullWidthSelection, true);
            selections.append(t);
        }
    }
    int pc_line = 0;
    if (pc_loc && pc_loc->file == location->file && pc_loc->line >= 1 && pc_loc->line <= file_lines &&
        doc->findBlockByNumber(pc_loc->line - 1).isValid()) {
        pc_line = pc_loc->line;
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(doc->findBlockByNumber(pc_line - 1));
        selection.cursor.clearSelection();
        selection.format.setBackground(editor_->palette().highlight());
        selection.format.setForeground(editor_->palette().highlightedText());
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selections.append(selection);
    }
    editor_->setExtraSelections(selections);
    // Placed only when something moved. This runs on every tick while paused,
    // and centring again each time would snap the view back to the line
    // whenever the user scrolled away from it.
    if (!placed_ || location->line != placed_line_ || addr != placed_pc_) {
        QTextCursor at(block);
        at.clearSelection();
        {
            SyncScope sync(syncing_);
            // A browse (GH #347) centres only a line out of view: the user is looking at the text.
            const bool fresh = !placed_;
            editor_->setTextCursor(at);
            if (fresh || !browsing || !editor_->viewport()->rect().contains(editor_->cursorRect(at)))
                editor_->centerCursor();
        }
        placed_ = true;
        placed_line_ = location->line;
        placed_pc_ = addr;
    }
    highlighted_line_ = pc_line;
    emit_lines_(false, false);   // a stop put the cursor on the PC's line: the Disassembly tints it
}

// GH #347 - the Disassembly's selection leads the view.
void SourcePanel::set_disasm_selection(uint16_t lo, uint16_t hi, uint16_t caret)
{
    if (!dbg_ || !dbg_->state().paused || dbg_->source_map().empty()) return;
    disasm_range_ = DisasmRange{lo, hi, caret};
    if (const SourceLocation* loc = dbg_->source_location(caret)) {
        const SourceLocation* pc_loc = dbg_->source_location();
        if (pc_loc && pc_loc->file == loc->file && pc_loc->line == loc->line) {
            browse_addr_.reset();   // the PC's own statement
            placed_ = false;        // the cursor goes back to it
        } else {
            browse_addr_ = caret;
            browse_map_ = dbg_->source_map().loaded_file();
        }
        refresh();
        emit_lines_(false, true);   // the Disassembly tints the lines the cursor is on now
    } else {
        refresh();   // only the tint follows the range
    }
}

void SourcePanel::show_address(uint16_t addr)
{
    placed_ = false;   // centred, even when the view is on the line already
    set_disasm_selection(addr, addr, addr);
}

void SourcePanel::go_to_pc_line()
{
    browse_addr_.reset();
    placed_ = false;
    refresh();
}

void SourcePanel::selected_blocks_(QTextBlock& first, QTextBlock& last) const
{
    const QTextCursor cur = editor_->textCursor();
    const QTextDocument* doc = editor_->document();
    first = doc->findBlock(cur.selectionStart());
    last = doc->findBlock(cur.selectionEnd());
    // A selection that ends at the very start of a block (a line selected through
    // its newline) does not take that block.
    if (last.position() == cur.selectionEnd() && last != first) last = last.previous();
}

void SourcePanel::emit_lines_(bool reveal, bool force)
{
    QString file;
    int first_line = 0, last_line = 0;
    const int lines = file_line_count(editor_->document());
    if (!shown_file_.empty() && lines > 0) {
        QTextBlock first, last;
        selected_blocks_(first, last);
        first_line = std::min(first.blockNumber() + 1, lines);
        last_line = std::min(last.blockNumber() + 1, lines);
        file = QString::fromStdString(shown_file_);
    }
    if (!force && file == lines_file_ && first_line == lines_first_ && last_line == lines_last_) return;
    lines_file_ = file;
    lines_first_ = first_line;
    lines_last_ = last_line;
    emit source_lines_selected(file, first_line, last_line, reveal && !file.isEmpty());
}

std::vector<int> SourcePanel::linked_lines_(const std::string& file) const
{
    std::set<int> lines;
    if (!disasm_range_ || !dbg_) return {};
    const DisasmReadFn read = dbg_->memory_reader();   // a peek
    for (uint32_t a = disasm_range_->lo; a <= disasm_range_->hi;) {
        const uint16_t addr = static_cast<uint16_t>(a);
        for (const SourceLocation* l : dbg_->source_map().lookup_all(dbg_->source_page(addr), addr))
            if (l->file == file) lines.insert(l->line);
        a += static_cast<uint32_t>(std::max(1, instruction_length(addr, read)));
    }
    return std::vector<int>(lines.begin(), lines.end());
}

// ── GH #421: the gutter ──────────────────────────────────────────────────

void SourcePanel::update_gutter() { static_cast<SourceEdit*>(editor_)->update_gutter(); }

void SourcePanel::set_breakpoint_model(BreakpointModel* model)
{
    if (bp_model_) disconnect(bp_model_, nullptr, this, nullptr);
    bp_model_ = model;
    // Repaint on any change of the GUI's execute breakpoints, however made (the
    // model notifies its own mutations synchronously, another client's on its sync).
    if (bp_model_)
        connect(bp_model_, &BreakpointModel::changed, this, [this](uint32_t kinds) {
            if (kinds & jnext::dbg::kind_bit(jnext::dbg::EventKind::Execute)) update_gutter();
        });
    update_gutter();
}

// What the BP column derives for one line L of the file shown, F.
//  - starts: where the map says L begins (an alias line shares its target's);
//  - placed: those starts the memory mapped NOW supplies;
//  - drawn: this GUI's execute breakpoints the Disassembly would draw at an address
//    of L (a logical one, or one tied to the memory supplying it now) that either
//    sit on a placed start or on any instruction whose primary line is L;
//  - hidden: breakpoints tied to exactly a start whose page is not mapped now.
struct SourcePanel::LineState {
    std::string file;
    std::string error;
    std::vector<SourceAddress> starts, placed;
    std::vector<BreakpointModel::PcBreakpoint> drawn, hidden;
    int armed = 0;   // starts (mapped now or not) carrying one of this GUI's execute breakpoints
};

SourcePanel::LineState SourcePanel::line_state(int line) const
{
    LineState st;
    BreakpointModel* bps = bp_model_.data();
    if (!dbg_ || shown_file_.empty() || line < 1) return st;
    st.file = shown_file_;
    const SourceMap& map = dbg_->source_map();
    const SourceResolution res = map.resolve(shown_file_ + ":" + std::to_string(line));
    st.starts = res.starts;
    st.error = res.error;
    for (const SourceAddress& s : st.starts)
        if (dbg_->source_page(s.address) == s.space) st.placed.push_back(s);
    if (!bps) return st;   // the places are known without the GUI's breakpoints (GH #423)
    for (const BreakpointModel::PcBreakpoint& m : bps->pc_breakpoints()) {
        const jnext::dbg::MemSpace sp = dbg_->source_page(m.addr);
        if (m.space == jnext::dbg::MemSpace::cpu() || m.space == sp) {
            bool on_line = std::any_of(st.placed.begin(), st.placed.end(), [&](const SourceAddress& s) {
                return s.address == m.addr;
            });
            if (!on_line) {
                const SourceLocation* loc = map.lookup(sp, m.addr);
                on_line = loc && loc->line == line && loc->file == shown_file_;
            }
            if (on_line) {
                st.drawn.push_back(m);
                continue;
            }
        }
        for (const SourceAddress& s : st.starts)
            if (s.address == m.addr && s.space == m.space && dbg_->source_page(s.address) != s.space)
                st.hidden.push_back(m);
    }
    // A place is armed by a breakpoint at its address that is logical or tied to its
    // memory, whether that memory is mapped now or not: a place removed in a bank that
    // is out of view still makes the line partial.
    const auto all = bps->pc_breakpoints();
    for (const SourceAddress& p : st.starts)
        if (std::any_of(all.begin(), all.end(), [&](const BreakpointModel::PcBreakpoint& m) {
                return m.addr == p.address &&
                       (m.space == jnext::dbg::MemSpace::cpu() || m.space == p.space);
            }))
            ++st.armed;
    return st;
}

SourcePanel::LineMark SourcePanel::line_mark(int line) const
{
    const LineState st = line_state(line);
    if (st.drawn.empty()) return LineMark::None;
    const bool live = std::any_of(st.drawn.begin(), st.drawn.end(),
                                  [](const BreakpointModel::PcBreakpoint& m) { return m.live; });
    const bool full = !st.starts.empty() && st.armed == static_cast<int>(st.starts.size());
    if (full) return live ? LineMark::Armed : LineMark::Suspended;
    return live ? LineMark::PartArmed : LineMark::PartSuspended;
}

QString SourcePanel::line_tooltip(int line) const
{
    const LineState st = line_state(line);
    if (st.file.empty()) return QString();
    const QString where = QString::fromStdString(st.file) + QLatin1Char(':') + QString::number(line);
    auto list = [](const std::vector<BreakpointModel::PcBreakpoint>& v) {
        QStringList parts;
        for (const auto& m : v) parts << BreakpointModel::address_text(m.addr, m.space);
        return parts.join(QStringLiteral(", "));
    };
    const LineMark mark = line_mark(line);
    if (mark != LineMark::None) {
        const bool suspended = mark == LineMark::Suspended || mark == LineMark::PartSuspended;
        const bool partial = mark == LineMark::PartArmed || mark == LineMark::PartSuspended;
        QString text = where + QStringLiteral(": ") +
                       (suspended ? tr("suspended breakpoint at %1") : tr("breakpoint at %1")).arg(list(st.drawn));
        if (partial)
            text += tr(" (%1 of %2 places)").arg(st.armed).arg(st.starts.size());
        return text;
    }
    if (st.starts.empty())
        return st.error.empty() ? tr("no code at %1").arg(where) : QString::fromStdString(st.error);
    if (!st.hidden.empty())
        return where + QStringLiteral(": ") + tr("breakpoint at %1, not mapped now").arg(list(st.hidden));
    if (st.placed.empty())
        return where + QStringLiteral(": ") +
               tr("code at %1, not mapped now; click to set")
                   .arg(BreakpointModel::address_text(st.starts[0].address, st.starts[0].space));
    return where + QStringLiteral(": ") + tr("no breakpoint; click to set");
}

void SourcePanel::toggle_breakpoint_at_line(int line)
{
    BreakpointModel* bps = bp_model_.data();
    if (!bps || shown_file_.empty() || line < 1 || line > file_line_count(editor_->document())) return;
    const LineState st = line_state(line);
    std::vector<BreakpointModel::PcBreakpoint> remove = st.drawn;
    remove.insert(remove.end(), st.hidden.begin(), st.hidden.end());
    if (!remove.empty()) {
        for (const auto& m : remove) bps->remove(BreakpointModel::Execute, m.addr, m.space);
        return;
    }
    const QString where = QString::fromStdString(shown_file_) + QLatin1Char(':') + QString::number(line);
    if (st.starts.empty() || bps->add_source_line(where) == jnext::dbg::GROUP_NONE) {
        const QString why = st.error.empty() ? tr("no code at %1").arg(where) : QString::fromStdString(st.error);
        if (QWidget* g = editor_->findChild<QWidget*>(QStringLiteral("source_gutter"))) {
            g->setToolTip(why);
            QToolTip::showText(QCursor::pos(), why, g);
        }
    }
}

// ── GH #423: the context menu ────────────────────────────────────────────

void SourcePanel::set_watch_panel(WatchPanel* watches) { watch_panel_ = watches; }

std::optional<SymbolNameKey> SourcePanel::symbol_for_word(const QString& word) const
{
    return dbg_->symbols().resolve_key(("@" + word).toStdString());
}

bool SourcePanel::has_text_focus() const
{
    if (!editor_->isVisible()) return false;
    const QWidget* f = window()->focusWidget();
    return f && (f == editor_ || editor_->isAncestorOf(f));
}

void SourcePanel::run_to_cursor()
{
    if (shown_file_.empty()) return;
    const int line = editor_->textCursor().blockNumber() + 1;
    if (line < 1 || line > file_line_count(editor_->document())) return;
    if (line_state(line).starts.empty()) return;
    emit run_to_line_requested(QString::fromStdString(shown_file_) + QLatin1Char(':') + QString::number(line));
}

QString SourcePanel::selection_text_with_addresses() const
{
    const QTextCursor cur = editor_->textCursor();
    if (!cur.hasSelection() || shown_file_.empty()) return QString();
    const QTextDocument* doc = editor_->document();
    QTextBlock first, last;
    selected_blocks_(first, last);
    const int lines = file_line_count(doc);
    QString out;
    for (QTextBlock b = first; b.isValid() && b.blockNumber() <= last.blockNumber(); b = b.next()) {
        const int line = b.blockNumber() + 1;
        if (line > lines) break;
        QStringList places;
        for (const SourceAddress& s : line_state(line).starts)
            places << BreakpointModel::address_text(s.address, s.space);
        out += QString::fromStdString(shown_file_) + QLatin1Char(':') + QString::number(line) +
               QStringLiteral("  ") + (places.isEmpty() ? QStringLiteral("-") : places.join(QStringLiteral(", "))) +
               QStringLiteral("  ") + b.text() + QLatin1Char('\n');
    }
    return out;
}

namespace {

bool word_char(QChar c)
{
    const ushort u = c.unicode();
    return u < 128 && (std::isalnum(u) || u == '_' || u == '.');
}

bool all_hex(const QString& t)
{
    if (t.isEmpty()) return false;
    for (QChar c : t)
        if (c.unicode() >= 128 || !std::isxdigit(c.unicode())) return false;
    return true;
}

bool all_dec(const QString& t)
{
    if (t.isEmpty()) return false;
    for (QChar c : t)
        if (c.unicode() < '0' || c.unicode() > '9') return false;
    return true;
}

// The value of a numeric literal in `run` (`$FF`/`#FF` with the character before it
// passed in, `0xFF`, `FFh`, decimal), or none when it is not one; a value too
// big for the type is 0x10000, i.e. out of the 16-bit range the caller refuses.
std::optional<unsigned> literal_value(const QString& run, QChar before)
{
    auto value = [](const QString& digits, int base) -> unsigned {
        bool ok = false;
        const qulonglong v = digits.toULongLong(&ok, base);
        return ok && v <= 0xFFFF ? static_cast<unsigned>(v) : 0x10000u;
    };
    if ((before == QLatin1Char('$') || before == QLatin1Char('#')) && all_hex(run)) return value(run, 16);
    if (run.size() > 2 && (run.startsWith(QLatin1String("0x")) || run.startsWith(QLatin1String("0X"))) &&
        all_hex(run.mid(2)))
        return value(run.mid(2), 16);
    if (run.size() > 1 && run[0].isDigit() && (run.endsWith(QLatin1Char('h')) || run.endsWith(QLatin1Char('H'))) &&
        all_hex(run.left(run.size() - 1)))
        return value(run.left(run.size() - 1), 16);
    if (all_dec(run)) return value(run, 10);
    return std::nullopt;
}

}  // namespace

void SourcePanel::fill_context_menu(QMenu& menu, int line, int column)
{
    menu.setToolTipsVisible(true);
    menu.addAction(copy_action_);
    const QTextDocument* doc = editor_->document();
    const bool on_line = !shown_file_.empty() && line >= 1 && line <= file_line_count(doc);
    if (on_line) menu.addAction(copy_addresses_action_);
    menu.addAction(select_all_action_);
    if (!on_line) return;

    // The Disassembly's rule: a right click inside the selection keeps it (that is how
    // a range just dragged out is copied); outside it, the selection becomes that line.
    const QTextBlock block = doc->findBlockByNumber(line - 1);
    const QTextCursor cur = editor_->textCursor();
    bool inside = false;
    if (cur.hasSelection()) {
        const int s = cur.selectionStart(), e = cur.selectionEnd();
        if (column >= 0)
            inside = block.position() + std::min(column, block.length() - 1) >= s &&
                     block.position() + std::min(column, block.length() - 1) <= e;
        else
            inside = block.position() < e && block.position() + block.length() - 1 >= s;
    }
    if (!inside) {
        QTextCursor c(block);
        c.movePosition(QTextCursor::StartOfBlock);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        editor_->setTextCursor(c);
    }

    const LineState st = line_state(line);
    const QString file = QString::fromStdString(shown_file_);
    const QString where = file + QLatin1Char(':') + QString::number(line);
    const QString nocode = st.error.empty() ? tr("no code at %1").arg(where) : QString::fromStdString(st.error);

    menu.addSeparator();
    auto* toggle = menu.addAction(tr("Toggle Breakpoint"));
    const bool can_toggle = bp_model_ && (!st.starts.empty() || !st.drawn.empty() || !st.hidden.empty());
    toggle->setEnabled(can_toggle);
    if (!can_toggle) toggle->setToolTip(nocode);
    connect(toggle, &QAction::triggered, this, [this, line]() { toggle_breakpoint_at_line(line); });

    auto* run_to = menu.addAction(tr("Run to Here"));
    run_to->setEnabled(!st.starts.empty());
    if (st.starts.empty()) run_to->setToolTip(nocode);
    connect(run_to, &QAction::triggered, this, [this, where]() { emit run_to_line_requested(where); });

    menu.addSeparator();
    auto place_text = [](const SourceAddress& s) { return BreakpointModel::address_text(s.address, s.space); };
    auto mapped = [this](const SourceAddress& s) { return dbg_->source_page(s.address) == s.space; };
    if (st.starts.size() > 1) {
        QMenu* sub = menu.addMenu(tr("Show in Disassembly"));
        sub->setToolTipsVisible(true);
        for (const SourceAddress& s : st.starts) {
            QAction* a = sub->addAction(place_text(s));
            a->setEnabled(mapped(s));
            if (!mapped(s)) a->setToolTip(tr("%1 is not mapped now").arg(place_text(s)));
            const uint16_t addr = s.address;
            connect(a, &QAction::triggered, this, [this, addr]() { emit show_address_requested(addr); });
        }
    } else {
        QAction* a = menu.addAction(tr("Show in Disassembly"));
        if (st.starts.empty()) {
            a->setEnabled(false);
            a->setToolTip(nocode);
        } else if (!mapped(st.starts[0])) {
            a->setEnabled(false);
            a->setToolTip(tr("%1 is not mapped now").arg(place_text(st.starts[0])));
        }
        const uint16_t addr = st.starts.empty() ? 0 : st.starts[0].address;
        connect(a, &QAction::triggered, this, [this, addr]() { emit show_address_requested(addr); });
    }

    // The operand entries: the line's own labels, and the word under the pointer.
    MenuOperands ops;
    const auto namer = disasm_text::symbol_namer(&dbg_->symbols(), [this](uint16_t a) {
        return dbg_->source_page(a);
    });
    std::optional<std::pair<std::string, uint16_t>> word_symbol;
    if (column >= 0) {
        const QString text = block.text();
        int at = -1;
        if (column < text.size() && word_char(text[column])) at = column;
        else if (column > 0 && column <= text.size() && word_char(text[column - 1])) at = column - 1;
        if (at >= 0) {
            int a = at, b = at;
            while (a > 0 && word_char(text[a - 1])) --a;
            while (b + 1 < text.size() && word_char(text[b + 1])) ++b;
            const QString run = text.mid(a, b - a + 1);
            const QChar before = a > 0 ? text[a - 1] : QChar();
            int k = a - 1;
            while (k >= 0 && text[k].isSpace()) --k;
            const bool paren = k >= 0 && text[k] == QLatin1Char('(');
            // The Disassembly's six register-indirect operands; a bare register is
            // not an operand of the menu.
            const auto regs = dbg_->registers();
            struct { const char* name; uint16_t value; } reg_table[] = {
                {"HL", regs.HL}, {"DE", regs.DE}, {"BC", regs.BC},
                {"IX", regs.IX}, {"IY", regs.IY}, {"SP", regs.SP}};
            bool is_reg = false;
            for (const auto& r : reg_table)
                if (run.compare(QLatin1String(r.name), Qt::CaseInsensitive) == 0) {
                    is_reg = true;
                    if (paren) ops.regs.push_back({r.name, r.value});
                }
            if (!is_reg) {
                if (const auto num = literal_value(run, before)) {
                    if (*num <= 0xFFFF) {
                        ops.immediate = static_cast<uint16_t>(*num);
                        if (const auto n = namer(*ops.immediate)) ops.immediate_label = *n;
                    }
                } else if (run[0].isLetter() || run[0] == QLatin1Char('_') || run[0] == QLatin1Char('.')) {
                    auto key = symbol_for_word(run);
                    QString used = run;
                    if (!key && run.contains(QLatin1Char('.'))) {
                        int ca = at - a, cs = ca, ce = ca;   // the dotted component under the pointer
                        while (cs > 0 && run[cs - 1] != QLatin1Char('.')) --cs;
                        while (ce + 1 < run.size() && run[ce + 1] != QLatin1Char('.')) ++ce;
                        used = run.mid(cs, ce - cs + 1);
                        if (!used.isEmpty()) key = symbol_for_word(used);
                    }
                    // The shown name, as the Disassembly names it: a z88dk C symbol is `counter`, not `_counter`.
                    if (key)
                        word_symbol = std::make_pair(dbg_->symbols().shown_name(used.toStdString()), key->addr);
                }
            }
        }
    }
    if (word_symbol) ops.symbol = word_symbol;
    for (const SourceAddress& s : st.placed)
        if (const auto n = namer(s.address)) {
            const MenuOperands::Named label{*n, s.address};
            const bool dup = word_symbol && *word_symbol == label;
            if (!dup && std::find(ops.labels.begin(), ops.labels.end(), label) == ops.labels.end())
                ops.labels.push_back(label);
        }
    add_operand_actions(menu, this, ops, watch_panel_.data(), bp_model_.data());
}
