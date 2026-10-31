#include "debugger/source_panel.h"

#include "debugger/breakpoint_glyph.h"
#include "debugger/breakpoint_model.h"
#include "qt/palette_tint_qt.h"
#include "debug/debugger.h"
#include "debug/ram_page.h"
#include "debug/source_search.h"

#include <QDateTime>
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

class SourceEdit;

class SourceGutter : public QWidget {
public:
    SourceGutter(SourceEdit* edit, QWidget* parent, SourcePanel* panel);
protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
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
        const QPalette pal = palette();
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
            }
            top += h;
            block = block.next();
        }
    }

    void paint_header()
    {
        QPainter p(header_);
        const QPalette pal = palette();
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
    if (ev->button() != Qt::LeftButton) return;   // the context menu is GH #423's
    const int line = line_at(ev);
    if (line > 0) panel_->toggle_breakpoint_at_line(line);
}

void SourceGutter::mouseMoveEvent(QMouseEvent* ev)
{
    const int line = line_at(ev);
    setToolTip(line > 0 ? panel_->line_tooltip(line) : QString());
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
    location_label_->setToolTip(QString());
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
    refresh_();
    update_gutter();   // the marks follow the memory mapped now
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

    // GH #420 - a z88dk record in a `.m4` file (the CRT): the assembler numbered the
    // lines of the m4 OUTPUT, which the build did not keep, so no file on disk has them.
    if (dbg_->source_map().format() == "z88dk" &&
        QString::fromStdString(location->file).endsWith(QLatin1String(".m4"), Qt::CaseInsensitive)) {
        show_message(tr("%1:%2 - the line counts the m4 output of this file, which the build "
                        "did not keep; use the disassembly")
                         .arg(QString::fromStdString(location->file)).arg(location->line) + note);
        return;
    }
    const QString source_path = resolve_source_file(location->file);
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
        editor_->setPlainText(QString::fromUtf8(file.readAll()));
        loaded_source_ = source_path;
        loaded_mtime_ = mtime;
        placed_ = false;   // a new text, or another file, starts at its top
    }
    shown_file_ = location->file;

    location_label_->setToolTip(source_path);
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
    if (!dbg_ || !bps || shown_file_.empty() || line < 1) return st;
    st.file = shown_file_;
    const SourceMap& map = dbg_->source_map();
    const SourceResolution res = map.resolve(shown_file_ + ":" + std::to_string(line));
    st.starts = res.starts;
    st.error = res.error;
    for (const SourceAddress& s : st.starts)
        if (dbg_->source_page(s.address) == s.space) st.placed.push_back(s);
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
