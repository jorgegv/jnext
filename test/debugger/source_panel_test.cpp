// ===========================================================================
// The Qt side of source-level debugging — CAP-SRC
// (doc/design/SOURCE-LEVEL-DEBUGGING.md):
//
//   QSRC-*   the Source tab: the PC's line, the step buttons, a reload
//   QBPM-*   BreakpointModel: Execute breakpoints qualified with a page
//   QBPP-*   the Breakpoints panel: file:line in, the source column out
//   QCSP-*   the Call Stack panel's "Called from" column
//   QADR-*   address fields that take a symbol (watch, Add Execute Breakpoint)
//
// The real DebuggerManager + DebuggerWindow over a real 48K Emulator and its
// backend, as debugger_verbs_test drives them. The program and its source:
//
//   8000  00           NOP          main.bas:10
//   8001  00           NOP          (inside main.bas:10's range)
//   8002  CD 00 90     CALL SUB     main.bas:11
//   8005  00           NOP          main.bas:12
//   8006  C3 00 80     JP $8000     main.bas:13
//   9000  00           NOP          sub.bas:1
//   9001  C9           RET          (inside sub.bas:1's range)
//
// Every row fails on a tree without the feature: there is no Source tab, no
// page-qualified model row and no source map to read.
//
//   QSRC-120..139  language detection and syntax highlighting (GH #422)
//   QSRC-190..199  owner classes, punctuation and operators, colour schemes (GH #422 stage 2)
//   QSRC-170..189, 201..208  the current-line wedge, the also: list, selection sync, browse mode (GH #347)
//
// Qt is required; a display is not (offscreen QPA, as the other suites).
// Run: ./build/test/debugger_source_panel_test
// ===========================================================================

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "debug/debugger.h"
#include "debug/disasm.h"
#include "debug/ram_page.h"
#include "debug/source_file.h"
#include "debug/symbol_table.h"
#include "debugger/breakpoint_model.h"
#include "debugger/breakpoint_panel.h"
#include "debugger/callstack_panel.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/disasm_panel.h"
#include "debugger/memory_panel.h"
#include "debugger/source_highlighter.h"
#include "debugger/source_panel.h"
#include "debugger/watch_panel.h"

#include <QAbstractButton>
#include <QDateTime>
#include <QTextLayout>
#include <QTextDocument>
#include <QClipboard>
#include <QGuiApplication>
#include <QKeySequence>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QScrollBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QWindow>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <vector>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include "../row_id.h"
#include "qt/palette_tint_qt.h"
#include "dark_test_palette.h"
#include "debugger/coverage_colors.h"   // GH #388
#include "debugger/source_sync_colors.h"   // GH #347

namespace {

using jnext::dbg::MemSpace;

// The RAM page n as a fetch source (the qualifier a source line is tied to).
MemSpace pg(unsigned n) { return MemSpace::page(static_cast<uint16_t>(n)); }

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
        std::printf("  PASS %s: %s\n", id, desc);
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string s(const QString& q) { return q.toStdString(); }

QTemporaryDir* g_tmp = nullptr;

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

const char* const kNextDevice =
    "|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
    "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n";

struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    QMainWindow win;
    DebuggerManager* mgr = nullptr;
    unsigned p80 = 0, p90 = 0;
    std::string sld;

    explicit Fixture(int rewind_frames = 0, MachineType type = MachineType::ZX48K) {
        EmulatorConfig cfg;
        cfg.type = type;
        cfg.rewind_buffer_frames = rewind_frames;
        emu.init(cfg);
        const uint8_t prog[] = {0x00, 0x00, 0xCD, 0x00, 0x90, 0x00, 0xC3, 0x00, 0x80};
        for (size_t i = 0; i < sizeof(prog); ++i)
            emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
        emu.mmu().write(0x9000, 0x00);
        emu.mmu().write(0x9001, 0xC9);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = r.IFF2 = 0;
        emu.cpu().set_registers(r);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        mgr = new DebuggerManager(&win, *backend, &win);
        emu.debug_state().pause();
        p80 = backend->effective_page(0x8000);
        p90 = backend->effective_page(0x9000);

        const std::string dir = g_tmp->path().toStdString();
        sld = dir + "/prog.sld";
        const std::string a = std::to_string(p80), b = std::to_string(p90);
        write_text(sld, std::string("|SLD.data.version|1\n") + "main.bas|10||0" + kNextDevice +
                            "main.bas|10||0|" + a + "|32768|T|\n" +
                            "main.bas|11||0|" + a + "|32770|T|\n" +
                            "main.bas|12||0|" + a + "|32773|T|\n" +
                            "main.bas|13||0|" + a + "|32774|T|\n" +
                            "sub.bas|1||0|" + b + "|36864|T|\n");
        std::string main_bas;
        for (int i = 1; i <= 13; ++i) main_bas += "REM line " + std::to_string(i) + "\n";
        write_text(dir + "/main.bas", main_bas);
        write_text(dir + "/sub.bas", "REM sub 1\n");
    }
    ~Fixture() {
        if (!mgr) return;
        DebuggerWindow* w = mgr->debugger_window_ptr();
        mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
        delete w;
    }
    void enable() {
        mgr->set_enabled(true);
        QApplication::processEvents();
    }
    DebuggerWindow* dbg() const { return mgr->debugger_window_ptr(); }
    uint16_t pc() { return emu.cpu().get_registers().PC; }
    void load_map() { backend->load_source_map(sld, false); }
};

QPushButton* button(QWidget* root, const QString& text) {
    if (!root) return nullptr;
    for (QPushButton* b : root->findChildren<QPushButton*>())
        if (b->text() == text) return b;
    return nullptr;
}

QString cell(QWidget* panel, int row, int col) {
    auto* t = panel ? panel->findChild<QTableWidget*>() : nullptr;
    QTableWidgetItem* it = t ? t->item(row, col) : nullptr;
    return it ? it->text() : QStringLiteral("<no cell>");
}

/// Answer the next modal dialog by typing `text` into its first line edit.
template <class Fn>
bool answer_modal(const QString& text, Fn open) {
    bool seen = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) return;
        // A dialog shown again after OK refused the text: give up on it.
        if (seen) { dlg->reject(); return; }
        seen = true;
        if (QLineEdit* e = dlg->findChild<QLineEdit*>()) e->setText(text);
        dlg->accept();
    });
    timer.start(1);
    open();
    timer.stop();
    QApplication::processEvents();
    return seen;
}

/// Answer each modal that opens while `open` runs: `act(widget, n)` for the
/// n-th one, which must close it. Returns how many were answered. A dialog
/// shown again after its OK (a refused entry) counts again.
int drive_modals(const std::function<void(QWidget*, int)>& act,
                 const std::function<void()>& open) {
    int n = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        QWidget* w = QApplication::activeModalWidget();
        if (!w || !w->isVisible()) return;
        act(w, n++);
    });
    timer.start(1);
    open();
    timer.stop();
    QApplication::processEvents();
    return n;
}

/// Click the message box's `which` button; any other dialog is cancelled.
void click_box(QWidget* w, QMessageBox::StandardButton which) {
    if (auto* mb = qobject_cast<QMessageBox*>(w)) {
        if (QAbstractButton* b = mb->button(which)) { b->click(); return; }
    }
    if (auto* d = qobject_cast<QDialog*>(w)) d->reject();
}

/// Choose `path` in a file dialog, or click OK in a message box.
void choose_file(QWidget* w, const QString& path) {
    if (auto* fd = qobject_cast<QFileDialog*>(w)) {
        fd->selectFile(path);
        static_cast<QDialog*>(fd)->accept();   // QFileDialog::accept() is protected
        return;
    }
    click_box(w, QMessageBox::Ok);
}

// The disassembly panel's geometry (DisasmPanel's private constants, as
// debugger_disasm_copy_test restates them): line N is at y = 52 + N * 18.
constexpr int kPaintY = 52, kLineH = 18, kGutterW = 20;

/// A DisasmPanel over the fixture's backend and the GUI's breakpoint model,
/// with $8005 on its top line.
struct GutterView {
    QMainWindow win;
    DisasmPanel* panel = nullptr;
    GutterView(jnext::dbg::Debugger& backend, BreakpointModel& model) {
        panel = new DisasmPanel(&backend);
        panel->set_breakpoint_model(&model);
        win.setCentralWidget(panel);
        win.resize(700, kPaintY + 20 * kLineH);
        win.show();
        panel->resize(700, kPaintY + 20 * kLineH);
        QApplication::processEvents();
        if (auto* sb = panel->findChild<QScrollBar*>()) sb->setValue(0x8005);
        QApplication::processEvents();
    }
    QPoint dot() const { return QPoint(kGutterW / 2, kPaintY + kLineH / 2); }
    void click_gutter() {
        const QPointF pos(dot());
        QMouseEvent ev(QEvent::MouseButtonPress, pos, panel->mapToGlobal(dot()),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(panel, &ev);
        QApplication::processEvents();
    }
    /// Right-click the top line and trigger `wanted` from the popup; `before`
    /// runs once the popup is open and before the pick (the view may move on
    /// in between).
    void context_pick(const QString& wanted, const std::function<void()>& before = {}) {
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, [&]() {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu) return;
            if (before) before();
            for (QAction* a : menu->actions())
                if (a->text() == wanted) a->trigger();
            menu->close();
        });
        timer.start(1);
        const QPoint pos(kGutterW + 100, kPaintY + kLineH / 2);
        QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, panel->mapToGlobal(pos));
        QApplication::sendEvent(panel, &ev);
        timer.stop();
        QApplication::processEvents();
    }
    /// The gutter's centre pixel on the top line: red for a breakpoint that
    /// can fire, background for none or a suspended one (a hollow ring).
    bool red_dot() const {
        QImage img(panel->size(), QImage::Format_ARGB32);
        img.fill(Qt::magenta);
        panel->render(&img);
        const QRgb c = img.pixel(dot());
        return qRed(c) > 180 && qGreen(c) < 120 && qBlue(c) < 120;
    }
};

/// The line edit whose placeholder is `placeholder`, under `root`.
QLineEdit* line_edit(QWidget* root, const QString& placeholder) {
    if (!root) return nullptr;
    for (QLineEdit* e : root->findChildren<QLineEdit*>())
        if (e->placeholderText() == placeholder) return e;
    return nullptr;
}

// ── QSRC: the Source tab ───────────────────────────────────────────────

void test_source_tab() {
    Fixture fx;
    fx.enable();
    SourcePanel* sp = fx.dbg() ? fx.dbg()->source_panel() : nullptr;
    bool listed = false;
    if (sp)
        for (QTabWidget* t : fx.dbg()->findChildren<QTabWidget*>())
            for (int i = 0; i < t->count(); ++i)
                if (t->widget(i) == sp && t->tabText(i) == QStringLiteral("Source")) listed = true;
    check("QSRC-01", "the debugger window has a Source tab holding the Source panel", listed);
    if (!sp) return;

    sp->refresh();
    QPushButton* into = button(sp, QStringLiteral("Into"));
    check("QSRC-02", "with no source map the tab says so and the steps are disabled",
          sp->location_text() == QStringLiteral("No source map loaded") && into &&
              !into->isEnabled(),
          s(sp->location_text()));

    fx.load_map();
    fx.mgr->refresh_panels();
    check("QSRC-03", "with a map the PC's line is highlighted and named",
          sp->highlighted_line() == 10 && sp->location_text().startsWith("main.bas:10"),
          s(sp->location_text()));
    check("QSRC-04", "the forward steps are enabled while paused with a map",
          into && into->isEnabled());
    {
        // What the editor itself highlights, not the panel's own record of it.
        auto* ed = sp->findChild<QPlainTextEdit*>();
        const auto sel = ed ? ed->extraSelections() : QList<QTextEdit::ExtraSelection>{};
        const QString text = sel.size() == 1 ? sel[0].cursor.block().text() : QString();
        check("QSRC-16", "the editor's highlighted line is the file's line 10",
              text == QStringLiteral("REM line 10"), s(text));
    }

    if (into) into->click();
    QApplication::processEvents();
    check("QSRC-05", "Step Into runs a source step: the PC is on main.bas:11, shown",
          fx.pc() == 0x8002 && sp->highlighted_line() == 11, s(sp->location_text()));
    QPushButton* over = button(sp, QStringLiteral("Over"));
    if (over) over->click();
    QApplication::processEvents();
    check("QSRC-06", "Step Over runs the call through: main.bas:12",
          fx.pc() == 0x8005 && sp->highlighted_line() == 12, s(sp->location_text()));

    QPushButton* back = button(sp, QStringLiteral("Back"));
    check("QSRC-07", "the backward steps are disabled without rewind",
          back && !back->isEnabled());

    // An edit to the source on disk is shown at the next refresh.
    const std::string path = g_tmp->path().toStdString() + "/main.bas";
    std::string edited;
    for (int i = 1; i <= 13; ++i) edited += "PRINT " + std::to_string(i) + "\n";
    write_text(path, edited);
    namespace fs = std::filesystem;
    fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(5));
    fx.mgr->refresh_panels();
    auto* editor = sp->findChild<QPlainTextEdit*>();
    check("QSRC-08", "a source file changed on disk is reloaded",
          editor && editor->toPlainText().startsWith(QStringLiteral("PRINT 1")));

    fx.backend->clear_source_map();
    fx.mgr->refresh_panels();
    check("QSRC-09", "clearing the map clears the view",
          sp->highlighted_line() == 0 && sp->location_text() == QStringLiteral("No source map loaded"));
}

// QIDLE-06 (GH #428): the paused-window refresh gate must still see the loaded
// source file change on disk -- QSRC-08's production path, with nothing else
// (clock, backend state, input) having changed. Oracle: the plan's contract
// (section 2.5): the file's mtime is one of the redraw sources.
void test_idle_source_reload() {
    Fixture fx;
    fx.enable();
    fx.load_map();
    SourcePanel* sp = fx.dbg() ? fx.dbg()->source_panel() : nullptr;
    if (!sp) { check("QIDLE-06", "Source panel", false); return; }
    auto tick = [&]() { fx.mgr->check_breakpoint_hit(); fx.mgr->refresh_panels(); };
    tick();
    tick();
    fx.mgr->take_panel_refresh_count();
    tick();
    const uint64_t idle = fx.mgr->take_panel_refresh_count();

    const std::string path = g_tmp->path().toStdString() + "/main.bas";
    std::string edited;
    for (int i = 1; i <= 13; ++i) edited += "QIDLE " + std::to_string(i) + "\n";
    write_text(path, edited);
    namespace fs = std::filesystem;
    fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(5));
    tick();
    const uint64_t n1 = fx.mgr->take_panel_refresh_count();
    auto* editor = sp->findChild<QPlainTextEdit*>();
    const bool shown = editor && editor->toPlainText().startsWith(QStringLiteral("QIDLE 1"));
    tick();
    const uint64_t n2 = fx.mgr->take_panel_refresh_count();
    check("QIDLE-06",
          "a loaded source file rewritten on disk (nothing else changed) is reloaded by the next "
          "paused tick, once; the ticks before and after refresh nothing",
          idle == 0 && n1 == 1 && shown && n2 == 0,
          "idle=" + std::to_string(idle) + " n1=" + std::to_string(n1) +
              " shown=" + std::to_string(shown) + " n2=" + std::to_string(n2));
}

void test_source_tab_rewind() {
    Fixture fx(/*rewind_frames=*/8);
    fx.enable();
    fx.backend->set_trace_enabled(true);
    fx.load_map();
    SourcePanel* sp = fx.dbg() ? fx.dbg()->source_panel() : nullptr;
    if (!sp) { check("QSRC-10", "Source panel", false); return; }
    fx.mgr->refresh_panels();
    QPushButton* into = button(sp, QStringLiteral("Into"));
    if (into) { into->click(); into->click(); }
    QApplication::processEvents();
    fx.mgr->refresh_panels();
    QPushButton* back = button(sp, QStringLiteral("Back"));
    check("QSRC-10", "with trace and rewind the backward steps are enabled",
          back && back->isEnabled());
    if (back) back->click();
    QApplication::processEvents();
    check("QSRC-11", "Step Back returns to the previous statement, shown",
          fx.pc() == 0x8002 && sp->highlighted_line() == 11, s(sp->location_text()));
}

// QCOV-06 -- GH #388 (E23-25): memory coverage in the Source tab. With coverage on,
// every line the program executed (an X byte in one of its ranges) has a pale
// full-width tint and the current-line selection comes last so it wins on its own
// line; off, nothing but the current line. X only (R and W have no per-line meaning).
void test_source_coverage() {
    Fixture fx;
    fx.enable();
    fx.load_map();
    SourcePanel* sp = fx.dbg() ? fx.dbg()->source_panel() : nullptr;
    auto* ed = sp ? sp->findChild<QPlainTextEdit*>() : nullptr;
    QPushButton* into = sp ? button(sp, QStringLiteral("Into")) : nullptr;
    if (!sp || !ed || !into) { check("QCOV-06", "Source panel", false); return; }
    auto tinted_lines = [&](bool& current_last, bool& current_not_tint) {
        std::vector<int> lines;
        const auto sel = ed->extraSelections();
        const QColor tint = coverage_colors::exec_line(ed->palette());
        current_last = !sel.isEmpty() && sel.back().format.background().color() == ed->palette().highlight().color();
        current_not_tint = true;
        for (int i = 0; i + 1 < sel.size(); ++i) {
            lines.push_back(sel[i].cursor.blockNumber() + 1);
            if (sel[i].format.background().color() != tint) current_not_tint = false;
        }
        std::sort(lines.begin(), lines.end());
        return lines;
    };
    // (a) off: only the current line (QSRC-16's one selection).
    fx.mgr->refresh_panels();
    bool last0 = false, tint0 = true;
    const auto off_lines = tinted_lines(last0, tint0);
    // (b) on, nothing executed yet: still only the current line.
    fx.backend->mem_coverage_enable(true);
    fx.mgr->refresh_panels();
    bool last1 = false, tint1 = true;
    const auto none_yet = tinted_lines(last1, tint1);
    // (c) two source steps: main.bas lines 10 and 11 ran, the PC is on line 12.
    QPushButton* over = button(sp, QStringLiteral("Over"));
    into->click();
    if (over) over->click();            // the CALL runs through (sub.bas:1 runs, in another file)
    QApplication::processEvents();
    fx.mgr->refresh_panels();
    bool last2 = false, tint2 = true;
    const auto on_lines = tinted_lines(last2, tint2);
    const auto want = fx.backend->executed_source_lines("main.bas");
    std::vector<int> want_i(want.begin(), want.end());
    // (d) the current line is not tinted again: it keeps the highlight alone.
    const bool current_is_12 = sp->highlighted_line() == 12;
    // (e) switched off again: back to the single selection.
    fx.backend->mem_coverage_enable(false);
    fx.mgr->refresh_panels();
    bool last3 = false, tint3 = true;
    const auto off_again = tinted_lines(last3, tint3);
    check("QCOV-06",
          "Source tab: with coverage off only the current line is selected; on, the lines executed_source_lines "
          "returns for the shown file (main.bas 10 and 11 after two source steps) get the coverage tint, "
          "the current line (12) is appended last with the highlight, an unexecuted line has none, and "
          "switching it off drops the tint",
          off_lines.empty() && last0 && none_yet.empty() && last1 && on_lines == want_i &&
              on_lines == std::vector<int>({10, 11}) && last2 && tint2 && current_is_12 &&
              off_again.empty() && last3,
          "off=" + std::to_string(off_lines.size()) + " none=" + std::to_string(none_yet.size()) +
              " on=" + std::to_string(on_lines.size()) + " want=" + std::to_string(want_i.size()) +
              " cur12=" + std::to_string(current_is_12));
}

// A map loaded by a RELATIVE name: the panel still finds the source after
// the working directory moves on.
void test_relative_map() {
    Fixture fx;
    fx.enable();
    const QString before = QDir::currentPath();
    QDir::setCurrent(g_tmp->path());
    fx.backend->load_source_map("prog.sld", false);
    QDir::setCurrent(QDir::rootPath());
    fx.mgr->refresh_panels();
    SourcePanel* sp = fx.dbg() ? fx.dbg()->source_panel() : nullptr;
    const bool shown = sp && sp->highlighted_line() == 10;
    QDir::setCurrent(before);
    check("QSRC-12", "a map loaded by a relative name still finds its source once the "
                     "working directory has changed",
          shown, sp ? s(sp->location_text()) : "");
}

// ── QBPM / QBPP: page-qualified Execute breakpoints ────────────────────

// C8: a Memory.txt refused when loaded by hand says why, as a sidecar does.
void test_load_refused() {
    Fixture fx;
    fx.enable();
    const std::string big = g_tmp->path().toStdString() + "/big.Memory.txt";
    write_text(big, "8005: ._Landing\n");
    std::error_code ec;
    std::filesystem::resize_file(big, SOURCE_FILE_MAX_BYTES + 1, ec);   // sparse
    QString warning;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        QWidget* w = QApplication::activeModalWidget();
        if (!w || !w->isVisible()) return;
        if (auto* fd = qobject_cast<QFileDialog*>(w)) {
            fd->selectFile(QString::fromStdString(big));
            static_cast<QDialog*>(fd)->accept();   // QFileDialog::accept() is protected
        } else if (auto* mb = qobject_cast<QMessageBox*>(w)) {
            warning = mb->text();
            mb->accept();
        } else if (auto* d = qobject_cast<QDialog*>(w)) {
            d->reject();
        }
    });
    timer.start(1);
    if (QPushButton* b = button(fx.dbg()->source_panel(), QStringLiteral("Load Memory.txt...")))
        b->click();
    timer.stop();
    QApplication::processEvents();
    const std::string why = "file is " + std::to_string(SOURCE_FILE_MAX_BYTES + 1) + " bytes";
    check("QSRC-32", "a Memory.txt over the size cap loaded by hand is refused, and the "
                     "warning gives the reason",
          !ec && s(warning).find(why) != std::string::npos && !fx.backend->lookup_name("Landing"),
          s(warning));
}

void test_breakpoints() {
    Fixture fx;
    fx.enable();
    fx.load_map();
    BreakpointModel& m = fx.mgr->breakpoints();
    m.add(BreakpointModel::Execute, 0x8005, pg(fx.p80));
    const auto rows = m.rows();
    const bool one = rows.size() == 1 && rows[0].own && rows[0].type == BreakpointModel::Execute &&
                     rows[0].space == pg(fx.p80);
    check("QBPM-01", "an Execute breakpoint qualified with a page is the GUI's own row, "
                     "with its page",
          one);
    char want[32];
    std::snprintf(want, sizeof(want), "$8005 @%02X", fx.p80);
    check("QBPM-02", "its address reads with the page", one && rows[0].addr_text == want,
          one ? s(rows[0].addr_text) : "");
    bool ram_only = false;
    for (const auto& si : fx.backend->subscriptions(false))
        if (si.id == rows[0].id)
            ram_only = si.filter.space == pg(fx.p80) && si.filter.page == jnext::dbg::PAGE_ANY;
    check("QBPM-07", "the GUI's page-qualified breakpoint names RAM page p80 exactly: its filter is "
                     "space == Page{p80} with page == PAGE_ANY",
          one && ram_only);
    m.add(BreakpointModel::Execute, 0x8005);
    check("QBPM-03", "a logical breakpoint at the same address is a second row",
          m.rows().size() == 2);
    m.remove(BreakpointModel::Execute, 0x8005);
    check("QBPM-04", "removing the logical one keeps the page-qualified one",
          m.rows().size() == 1 && m.rows()[0].space == pg(fx.p80));
    check("QBPM-05", "the gutter marks it while its page is mapped, not for another page",
          m.pc_marked(0x8005, pg(fx.p80)) &&
              !m.pc_marked(0x8005, pg((fx.p80 + 1) % 224)));
    fx.backend->run(jnext::dbg::CLIENT_NONE);
    for (int i = 0; i < 4 && !fx.backend->state().paused; ++i) fx.emu.run_frame();
    check("QBPM-06", "it stops the machine at its address", fx.pc() == 0x8005);

    BreakpointPanel* bp = fx.dbg() ? fx.dbg()->breakpoint_panel() : nullptr;
    if (bp) bp->refresh();
    check("QBPP-01", "the panel's Symbol / Source column names the breakpoint's line",
          cell(bp, 0, 3) == QStringLiteral("main.bas:12"), s(cell(bp, 0, 3)));

    m.clear_all();
    const bool seen = answer_modal(QStringLiteral("main.bas:11"), [&]() { bp->on_add(); });
    const auto added = m.rows();
    check("QBPP-02", "file:line typed into Add Breakpoint creates an Execute breakpoint "
                     "at that line's address, on the page its record names",
          seen && added.size() == 1 && added[0].addr == 0x8002 && added[0].space == pg(fx.p80));
}

// ── QCSP: the Call Stack's source column ───────────────────────────────

void test_call_stack() {
    Fixture fx;
    fx.enable();
    fx.load_map();
    fx.backend->set_call_stack_enabled(true);
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);   // the CALL
    CallStackPanel* cs = fx.dbg() ? fx.dbg()->callstack_panel() : nullptr;
    if (cs) { cs->set_paused(true); cs->refresh(); }
    check("QCSP-01", "a frame's Called from column is its caller's source line",
          cell(cs, 0, 4) == QStringLiteral("main.bas:11"), s(cell(cs, 0, 4)));
}

// ── QADR: symbols in address fields ────────────────────────────────────

void test_address_fields() {
    Fixture fx;
    fx.enable();
    const std::string mem = g_tmp->path().toStdString() + "/prog.Memory.txt";
    write_text(mem, "8005: ._Landing\n");
    fx.backend->load_map(mem, jnext::dbg::MapFormat::NextBuild);

    WatchPanel* wp = fx.dbg() ? fx.dbg()->watch_panel() : nullptr;
    const bool seen = wp && answer_modal(QStringLiteral("Landing"), [&]() { wp->on_add_watch(); });
    check("QADR-01", "a watch added by symbol name watches its address, labelled with it",
          seen && cell(wp, 0, 1) == QStringLiteral("Landing") &&
              cell(wp, 0, 0).contains(QStringLiteral("8005")),
          s(cell(wp, 0, 0)) + " " + s(cell(wp, 0, 1)));
    const bool seen_at =
        wp && answer_modal(QStringLiteral("@Landing"), [&]() { wp->on_add_watch(); });
    check("QADR-04", "a watch added as @name watches the symbol and is labelled with the name",
          seen_at && cell(wp, 1, 1) == QStringLiteral("Landing") &&
              cell(wp, 1, 0).contains(QStringLiteral("8005")),
          s(cell(wp, 1, 0)) + " " + s(cell(wp, 1, 1)));

    QAction* add_exec = nullptr;
    for (QAction* a : fx.dbg()->menuBar()->actions())
        if (a->menu())
            for (QAction* i : a->menu()->actions())
                if (i->text() == QStringLiteral("Add &Execute Breakpoint...")) add_exec = i;
    const bool seen2 =
        add_exec && answer_modal(QStringLiteral("Landing"), [&]() { add_exec->trigger(); });
    check("QADR-02", "Add Execute Breakpoint takes a symbol name",
          seen2 && fx.mgr->breakpoints().pc_exists(0x8005));

    fx.mgr->breakpoints().clear_all();
    fx.load_map();
    const bool seen3 =
        add_exec && answer_modal(QStringLiteral("main.bas:12"), [&]() { add_exec->trigger(); });
    const auto rows = fx.mgr->breakpoints().rows();
    check("QADR-03", "Add Execute Breakpoint takes file:line, on the page its record names",
          seen3 && rows.size() == 1 && rows[0].addr == 0x8005 && rows[0].space == pg(fx.p80));

    fx.mgr->breakpoints().clear_all();
    QString reason;
    const int shown = add_exec ? drive_modals(
        [&](QWidget* w, int n) {
            auto* dlg = qobject_cast<QDialog*>(w);
            if (!dlg) return;
            if (n == 0) {
                if (QLineEdit* e = dlg->findChild<QLineEdit*>()) e->setText("no_such_thing");
                dlg->accept();
                return;
            }
            if (auto* err = dlg->findChild<QLabel*>(QStringLiteral("bp_dialog_error")))
                if (err->isVisible()) reason = err->text();
            dlg->reject();
        },
        [&]() { add_exec->trigger(); }) : 0;
    check("QADR-05", "text that names nothing keeps Add Execute Breakpoint open with the "
                     "reason, and adds nothing",
          shown == 2 && reason.contains(QStringLiteral("no_such_thing")) &&
              fx.mgr->breakpoints().rows().empty(),
          "shown=" + std::to_string(shown) + " reason=" + s(reason));

    // Go to Address: a symbol lands where its number does.
    DisasmPanel* dp = fx.dbg()->disasm_panel();
    QLineEdit* dgo = line_edit(dp, QStringLiteral("Address..."));
    auto* dsb = dp ? dp->findChild<QScrollBar*>() : nullptr;
    int by_number = -1, by_name = -1, start = -1;
    if (dgo && dsb) {
        dsb->setValue(0x4000);
        start = dsb->value();
        dgo->setText(QStringLiteral("8005"));
        emit dgo->returnPressed();
        by_number = dsb->value();
        dsb->setValue(0x4000);
        dgo->setText(QStringLiteral("Landing"));
        emit dgo->returnPressed();
        by_name = dsb->value();
    }
    check("QADR-06", "the disassembly's Go to Address takes a symbol",
          by_number != start && by_name == by_number,
          std::to_string(start) + " " + std::to_string(by_number) + " " + std::to_string(by_name));

    MemoryPanel* mp = fx.dbg()->memory_panel();
    QLineEdit* mgo = line_edit(mp, QStringLiteral("$0000"));
    auto* msb = mp ? mp->findChild<QScrollBar*>() : nullptr;
    by_number = by_name = start = -1;
    if (mgo && msb) {
        msb->setValue(0);
        start = msb->value();
        mgo->setText(QStringLiteral("8005"));
        emit mgo->returnPressed();
        by_number = msb->value();
        msb->setValue(0);
        mgo->setText(QStringLiteral("Landing"));
        emit mgo->returnPressed();
        by_name = msb->value();
    }
    check("QADR-07", "the memory panel's Go to Address takes a symbol",
          by_number != start && by_name == by_number,
          std::to_string(start) + " " + std::to_string(by_number) + " " + std::to_string(by_name));
}

// ── C3: a watch is labelled only with the symbol its text named ───────

void test_watch_labels() {
    Fixture fx;
    fx.enable();
    const std::string mem = g_tmp->path().toStdString() + "/labels.Memory.txt";
    write_text(mem, "8005: ._Landing\n9000: ._BEEF\n");
    fx.backend->load_map(mem, jnext::dbg::MapFormat::NextBuild);
    WatchPanel* wp = fx.dbg() ? fx.dbg()->watch_panel() : nullptr;
    // A number that is also the address of a symbol; hex spelled like a symbol
    // that sits elsewhere; and, as the control, the symbol typed by name.
    const bool seen = wp &&
        answer_modal(QStringLiteral("8005"), [&]() { wp->on_add_watch(); }) &&
        answer_modal(QStringLiteral("BEEF"), [&]() { wp->on_add_watch(); }) &&
        answer_modal(QStringLiteral("Landing"), [&]() { wp->on_add_watch(); });
    check("QADR-08", "a watch typed as a number is not labelled with the symbol at that "
                     "address, BEEF typed as hex watches $BEEF and not the symbol BEEF, and "
                     "the symbol typed by name is labelled with it",
          seen && cell(wp, 0, 0) == QStringLiteral("$8005") && cell(wp, 0, 1).isEmpty() &&
              cell(wp, 1, 0) == QStringLiteral("$BEEF") && cell(wp, 1, 1).isEmpty() &&
              cell(wp, 2, 0) == QStringLiteral("$8005") &&
              cell(wp, 2, 1) == QStringLiteral("Landing"),
          s(cell(wp, 0, 0)) + "/" + s(cell(wp, 0, 1)) + " " + s(cell(wp, 1, 0)) + "/" +
              s(cell(wp, 1, 1)) + " " + s(cell(wp, 2, 0)) + "/" + s(cell(wp, 2, 1)));
}

// ── N1: the watch dialog never cancels silently ───────────────────────

void test_watch_dialog_refused() {
    Fixture fx;
    fx.enable();
    WatchPanel* wp = fx.dbg() ? fx.dbg()->watch_panel() : nullptr;
    if (!wp) { check("QADR-09", "Watches panel", false); return; }
    QString reason;
    const int shown = drive_modals(
        [&](QWidget* w, int n) {
            auto* dlg = qobject_cast<QDialog*>(w);
            if (!dlg) return;
            if (n == 0) {
                if (QLineEdit* e = dlg->findChild<QLineEdit*>()) e->setText("no_such_thing");
                dlg->accept();
                return;
            }
            if (auto* err = dlg->findChild<QLabel*>(QStringLiteral("watch_dialog_error")))
                if (err->isVisible()) reason = err->text();
            dlg->reject();
        },
        [&]() { wp->on_add_watch(); });
    const bool none_added = cell(wp, 0, 0) == QStringLiteral("<no cell>");

    // The control: a valid entry closes the dialog at once, and adds its watch.
    const int shown_ok = drive_modals(
        [&](QWidget* w, int) {
            auto* dlg = qobject_cast<QDialog*>(w);
            if (!dlg) return;
            if (QLineEdit* e = dlg->findChild<QLineEdit*>()) e->setText("8005");
            dlg->accept();
        },
        [&]() { wp->on_add_watch(); });
    check("QADR-09", "text that names nothing keeps Add Watch open with the reason and adds "
                     "no watch; a valid entry closes it at once",
          shown == 2 && reason.contains(QStringLiteral("no_such_thing")) && none_added &&
              shown_ok == 1 && cell(wp, 0, 0) == QStringLiteral("$8005"),
          "shown=" + std::to_string(shown) + " reason=" + s(reason) +
              " ok_shown=" + std::to_string(shown_ok));
}

// ── B2: the gutter removes the breakpoint it draws ─────────────────────

void test_gutter() {
    Fixture fx;
    fx.enable();
    fx.load_map();
    BreakpointModel& m = fx.mgr->breakpoints();
    const MemSpace page = pg(fx.p80);
    {
        // ROM and RAM page numbers overlap: a breakpoint on RAM page N at $0000
        // is not drawn while ROM page N supplies the fetch.
        GutterView g(*fx.backend, m);
        if (auto* sb = g.panel->findChild<QScrollBar*>()) sb->setValue(0);
        const uint16_t rom = fx.backend->effective_page(0);
        const uint16_t fetch = ram_page_of(fx.backend->source_page(0));
        m.add(BreakpointModel::Execute, 0x0000, pg(rom));
        g.panel->refresh();
        QApplication::processEvents();
        check("QSRC-36", "the gutter draws no dot for a RAM page's breakpoint on a ROM line "
                         "whose page has the same number",
              fetch != rom && !g.red_dot(),
              "effective_page=" + std::to_string(rom) + " source_page=" + std::to_string(fetch));
        m.clear_all();
    }
    {
        GutterView g(*fx.backend, m);
        m.add(BreakpointModel::Execute, 0x8005, page);
        g.click_gutter();
        const auto rows = m.rows();
        check("QSRC-14", "one gutter click on a source breakpoint's dot removes it and adds "
                         "nothing",
              rows.empty(), std::to_string(rows.size()) + " row(s) left");

        m.add(BreakpointModel::Execute, 0x8005, page);
        g.context_pick(QStringLiteral("Toggle Breakpoint"));
        check("QSRC-15", "the context menu's Toggle Breakpoint removes it the same way",
              m.rows().empty(), std::to_string(m.rows().size()) + " row(s) left");

        // QBPM-09: the dot of a page-qualified breakpoint is live or not by its
        // own flag.
        m.add(BreakpointModel::Execute, 0x8005, page);
        m.set_enabled(BreakpointModel::Execute, 0x8005, false, page);
        g.panel->refresh();
        const bool hollow = !g.red_dot();
        m.set_enabled(BreakpointModel::Execute, 0x8005, true, page);
        g.panel->refresh();
        check("QBPM-09", "the gutter draws a page-qualified breakpoint suspended when it is "
                         "disabled and live when it is enabled",
              hollow && g.red_dot());
        m.clear_all();
    }
    {
        // While running the panel does not re-disassemble, so the click's own
        // patch is what the gutter shows: the source breakpoint still there.
        GutterView g(*fx.backend, m);
        m.add(BreakpointModel::Execute, 0x8005, page);
        m.add(BreakpointModel::Execute, 0x8005);
        g.panel->refresh();
        g.panel->set_paused(false);
        g.click_gutter();
        const bool left = m.rows().size() == 1 && m.rows()[0].space == page;
        check("QSRC-28", "while running, a click that removes the plain breakpoint still "
                         "draws the source breakpoint under it",
              left && g.red_dot());
        g.panel->set_paused(true);
        m.clear_all();
    }
    {
        // A click on an empty line while running shows its dot at once: the
        // panel does not re-disassemble then, so the click's own patch of the
        // drawn line is the only thing that can.
        GutterView g(*fx.backend, m);
        g.panel->refresh();
        g.panel->set_paused(false);
        const bool none_before = !g.red_dot();
        g.click_gutter();
        const bool added = m.rows().size() == 1 && m.rows()[0].addr == 0x8005;
        check("QSRC-38", "while running, a gutter click on an empty line draws its "
                         "breakpoint at once",
              none_before && added && g.red_dot());
        g.panel->set_paused(true);
        m.clear_all();
    }
    {
        // The pick follows the line the menu was opened on, even when the view
        // has moved on by then (a scroll or a bank switch): the entry drawn at
        // that index is no longer that line, so the page is asked of the MMU.
        GutterView g(*fx.backend, m);
        m.add(BreakpointModel::Execute, 0x8005, page);
        g.panel->refresh();
        auto* sb = g.panel->findChild<QScrollBar*>();
        g.context_pick(QStringLiteral("Toggle Breakpoint"), [&]() {
            if (sb) sb->setValue(0xC000);
            QApplication::processEvents();
        });
        std::string left;
        for (const auto& r : m.rows())
            left += " " + s(r.addr_text);
        check("QSRC-39", "the context menu's Toggle Breakpoint after the view moved on "
                         "removes the source breakpoint of the line it was opened on, and "
                         "adds nothing",
              m.rows().empty(), "rows left:" + left);
        m.clear_all();
    }
    {
        // While running the panel keeps the lines it drew. A bank switch after
        // that must not turn a click on the drawn dot into a second breakpoint.
        GutterView g(*fx.backend, m);
        const uint8_t other = static_cast<uint8_t>(page.index + 2);
        auto rows_text = [&]() {
            std::string t;
            for (const auto& r : m.rows()) t += " page=" + std::to_string(r.space.index);
            return t;
        };
        m.add(BreakpointModel::Execute, 0x8005, page);
        g.panel->refresh();
        g.panel->set_paused(false);
        fx.emu.mmu().set_page(4, other);
        g.click_gutter();
        const bool click_ok = m.rows().empty();
        const std::string click_rows = rows_text();
        fx.emu.mmu().set_page(4, static_cast<uint8_t>(page.index));
        g.panel->set_paused(true);
        check("QSRC-29", "while running, a click on a source breakpoint's dot after its bank "
                         "is switched out removes it and adds nothing",
              click_ok, "rows:" + click_rows);

        m.clear_all();
        m.add(BreakpointModel::Execute, 0x8005, page);
        g.panel->refresh();
        g.panel->set_paused(false);
        fx.emu.mmu().set_page(4, other);
        g.context_pick(QStringLiteral("Toggle Breakpoint"));
        const bool menu_ok = m.rows().empty();
        const std::string menu_rows = rows_text();
        fx.emu.mmu().set_page(4, static_cast<uint8_t>(page.index));
        g.panel->set_paused(true);
        check("QSRC-30", "the context menu's Toggle Breakpoint does the same after a bank switch",
              menu_ok, "rows:" + menu_rows);
        m.clear_all();
    }
}

// ── QBPM-08: the Breakpoints panel keeps a row's page ──────────────────

void test_breakpoint_page_rows() {
    Fixture fx;
    fx.enable();
    fx.load_map();
    BreakpointModel& m = fx.mgr->breakpoints();
    const MemSpace page = pg(fx.p80);
    BreakpointPanel* bp = fx.dbg() ? fx.dbg()->breakpoint_panel() : nullptr;
    auto* table = bp ? bp->findChild<QTableWidget*>() : nullptr;
    if (!bp || !table) { check("QBPM-08", "Breakpoints panel", false); return; }

    m.add(BreakpointModel::Execute, 0x8005, page);
    bp->refresh();
    if (QTableWidgetItem* en = table->item(0, 0)) en->setCheckState(Qt::Unchecked);
    const bool disabled = m.rows().size() == 1 && !m.rows()[0].enabled && m.rows()[0].space == page;
    table->setCurrentCell(0, 1);
    // The edit moves it to main.bas:11 ($8002): the old row must go, by its page.
    const bool edited = answer_modal(QStringLiteral("main.bas:11"), [&]() { bp->on_edit(); });
    const auto after_edit = m.rows();
    const bool kept = edited && after_edit.size() == 1 && after_edit[0].addr == 0x8002 &&
                      after_edit[0].space == page && !after_edit[0].enabled;
    bp->refresh();
    table->setCurrentCell(0, 1);
    bp->on_remove();
    check("QBPM-08", "a page-qualified row is disabled, moved by Edit and removed as itself: "
                     "the page and the disabled state survive the move, and Remove removes it",
          disabled && kept && m.rows().empty(),
          "disabled=" + std::to_string(disabled) + " kept=" + std::to_string(kept) +
              " rows=" + std::to_string(m.rows().size()));

    // The Symbol / Source column reads the breakpoint's own page, not the one
    // mapped there now.
    m.add(BreakpointModel::Execute, 0x8005, page);
    fx.emu.mmu().set_page(4, static_cast<uint8_t>(page.index + 2));
    bp->refresh();
    const QString while_other = cell(bp, 0, 3);
    fx.emu.mmu().set_page(4, static_cast<uint8_t>(page.index));
    m.remove(BreakpointModel::Execute, 0x8005, page);
    check("QBPP-03", "the Symbol / Source column names the breakpoint's line while another "
                     "page is mapped at its address",
          while_other == QStringLiteral("main.bas:12"), s(while_other));

    // A page-qualified data breakpoint is not something the Add dialog can
    // type, so even under the GUI's client it is listed read-only.
    jnext::dbg::Subscription mem;
    mem.kind = jnext::dbg::EventKind::Mem;
    mem.action = jnext::dbg::Action::Stop;
    mem.access = jnext::dbg::Access::Read;
    mem.filter.lo = mem.filter.hi = 0x9000;
    mem.filter.page = page.index;
    const bool subscribed = static_cast<bool>(fx.backend->subscribe(m.client(), mem));
    // The client that made the change is not told at once (a later pump may
    // tell it): an add and a remove of the model's own make it read the listing.
    m.add(BreakpointModel::Execute, 0x8000);
    m.remove(BreakpointModel::Execute, 0x8000);
    const auto listed = m.rows();
    check("QBPM-11", "a page-qualified Read breakpoint under the GUI's client is not one of "
                     "its own typed rows",
          subscribed && listed.size() == 1 && listed[0].type == -1 && !listed[0].own,
          "subscribed=" + std::to_string(subscribed) +
              " rows=" + std::to_string(listed.size()) +
              (listed.empty() ? std::string()
                              : " type=" + std::to_string(listed[0].type) +
                                    " own=" + std::to_string(listed[0].own)));
}

// ── A source line compiled to several places (GH #338) ─────────────────

void test_line_starts() {
    Fixture fx;
    fx.enable();
    // main.bas:11 is [$8002,$8005) and [$9000,$A000): two places.
    const std::string two = g_tmp->path().toStdString() + "/two.sld";
    write_text(two, std::string("|SLD.data.version|1\n") + "main.bas|10||0" + kNextDevice +
                        "main.bas|10||0|" + std::to_string(fx.p80) + "|32768|T|\n" +
                        "main.bas|11||0|" + std::to_string(fx.p80) + "|32770|T|\n" +
                        "main.bas|12||0|" + std::to_string(fx.p80) + "|32773|T|\n" +
                        "main.bas|11||0|" + std::to_string(fx.p90) + "|36864|T|\n");
    fx.backend->load_source_map(two, false);
    BreakpointModel& m = fx.mgr->breakpoints();
    BreakpointPanel* bp = fx.dbg() ? fx.dbg()->breakpoint_panel() : nullptr;
    auto* table = bp ? bp->findChild<QTableWidget*>() : nullptr;
    if (!bp || !table) { check("QBPP-04", "Breakpoints panel", false); return; }
    const MemSpace p80 = pg(fx.p80);
    const MemSpace p90 = pg(fx.p90);
    // E23-09: the line is ONE group row of two locations; its members are the
    // backend's two subscriptions, each tied to its own page.
    auto one_group = [&](bool enabled) {
        const auto r = m.rows();
        if (r.size() != 1 || r[0].type != BreakpointModel::Execute || !r[0].own ||
            r[0].group == jnext::dbg::GROUP_NONE || r[0].locations != 2 ||
            r[0].addr != 0x8002 || r[0].space != p80 || r[0].enabled != enabled)
            return false;
        int n = 0;
        bool a = false, b = false;
        for (const auto& si : fx.backend->subscriptions(false)) {
            if (si.owner != m.client()) continue;
            ++n;
            if (si.group != r[0].group || si.enabled != enabled) return false;
            if (si.filter.lo == 0x8002 && si.filter.space == p80) a = true;
            if (si.filter.lo == 0x9000 && si.filter.space == p90) b = true;
        }
        return n == 2 && a && b;
    };

    const bool seen = answer_modal(QStringLiteral("main.bas:11"), [&]() { bp->on_add(); });
    const bool rows = seen && one_group(true);
    fx.backend->run(jnext::dbg::CLIENT_NONE);
    for (int i = 0; i < 4 && !fx.backend->state().paused; ++i) fx.emu.run_frame();
    const bool first = fx.pc() == 0x8002;
    fx.backend->run(jnext::dbg::CLIENT_NONE);
    for (int i = 0; i < 4 && !fx.backend->state().paused; ++i) fx.emu.run_frame();
    check("QBPP-04", "file:line typed into Add Breakpoint arms every place the line "
                     "compiled to as one group of two locations, each on its own page, and a "
                     "run stops at both",
          rows && first && fx.pc() == 0x9000,
          "rows=" + std::to_string(rows) + " first=" + std::to_string(first) +
              " pc=" + std::to_string(fx.pc()));
    m.clear_all();

    QString reason;
    const int shown = drive_modals(
        [&](QWidget* w, int n) {
            auto* dlg = qobject_cast<QDialog*>(w);
            if (!dlg) return;
            if (n == 0) {
                if (QLineEdit* e = dlg->findChild<QLineEdit*>()) e->setText("main.bas:5");
                dlg->accept();
                return;
            }
            if (auto* err = dlg->findChild<QLabel*>(QStringLiteral("bp_dialog_error")))
                if (err->isVisible()) reason = err->text();
            dlg->reject();
        },
        [&]() { bp->on_add(); });
    check("QBPP-05", "a line of a known file with no code is refused with \"no code at\", "
                     "the dialog stays open and nothing is added",
          shown == 2 && reason.contains(QStringLiteral("no code at main.bas:5")) &&
              m.rows().empty(),
          "shown=" + std::to_string(shown) + " reason=" + s(reason));

    m.add(BreakpointModel::Execute, 0x8005, p80);
    bp->refresh();
    if (QTableWidgetItem* en = table->item(0, 0)) en->setCheckState(Qt::Unchecked);
    table->setCurrentCell(0, 1);
    const bool edited = answer_modal(QStringLiteral("main.bas:11"), [&]() { bp->on_edit(); });
    check("QBPP-06", "Edit to a line with several places leaves one group of two locations, "
                     "both disabled as the edited one was, and the old one gone",
          edited && one_group(false), "rows=" + std::to_string(m.rows().size()));
    m.clear_all();

    // A logical breakpoint (no page) on a mapped address names its line too.
    m.add(BreakpointModel::Execute, 0x8002);
    bp->refresh();
    check("QBPP-07", "the Symbol / Source column names the line of a breakpoint with no "
                     "page, by what is mapped at its address",
          cell(bp, 0, 3) == QStringLiteral("main.bas:11"), s(cell(bp, 0, 3)));
    m.clear_all();

    QAction* add_exec = nullptr;
    for (QAction* a : fx.dbg()->menuBar()->actions())
        if (a->menu())
            for (QAction* i : a->menu()->actions())
                if (i->text() == QStringLiteral("Add &Execute Breakpoint...")) add_exec = i;
    const bool seen2 =
        add_exec && answer_modal(QStringLiteral("main.bas:11"), [&]() { add_exec->trigger(); });
    check("QADR-10", "Add Execute Breakpoint arms every place a source line compiled to, "
                     "as one group of two locations",
          seen2 && one_group(true));
    m.clear_all();

    QString reason2;
    const int shown2 = add_exec ? drive_modals(
        [&](QWidget* w, int n) {
            auto* dlg = qobject_cast<QDialog*>(w);
            if (!dlg) return;
            if (n == 0) {
                if (QLineEdit* e = dlg->findChild<QLineEdit*>()) e->setText("main.bas:5");
                dlg->accept();
                return;
            }
            if (auto* err = dlg->findChild<QLabel*>(QStringLiteral("bp_dialog_error")))
                if (err->isVisible()) reason2 = err->text();
            dlg->reject();
        },
        [&]() { add_exec->trigger(); }) : 0;
    check("QADR-11", "Add Execute Breakpoint refuses a line with no code with \"no code at\" "
                     "and stays open",
          shown2 == 2 && reason2.contains(QStringLiteral("no code at main.bas:5")) &&
              m.rows().empty(),
          "shown=" + std::to_string(shown2) + " reason=" + s(reason2));

    // QBPM-12 (GH #341): a line compiled to two places is one row, "main.bas:11 (2 locations)",
    // and Remove on it takes both places and leaves the GUI client with no subscription.
    const bool seen3 = answer_modal(QStringLiteral("main.bas:11"), [&]() { bp->on_add(); });
    bp->refresh();
    const auto listed = m.rows();
    const QString shown_cell = cell(bp, 0, 3);
    // The row is enabled only while every location is: disable ONE member through the backend.
    jnext::dbg::EventId first_member = jnext::dbg::EVENT_NONE;
    for (const auto& si : fx.backend->subscriptions(false))
        if (si.owner == m.client() && first_member == jnext::dbg::EVENT_NONE) first_member = si.id;
    fx.backend->set_enabled(m.client(), first_member, false);
    m.add(BreakpointModel::Execute, 0x8000);    // the model re-reads the listing on its own mutations
    m.remove(BreakpointModel::Execute, 0x8000);
    const auto mixed = m.rows();
    const bool mixed_off = mixed.size() == 1 && !mixed[0].enabled && mixed[0].live &&
                           mixed[0].locations == 2;
    table->setCurrentCell(0, 1);
    bp->on_remove();
    bool own_left = false;
    for (const auto& si : fx.backend->subscriptions(false))
        if (si.owner == m.client()) own_left = true;
    check("QBPM-12", "a two-place source line is ONE Breakpoints row reading \"main.bas:11 "
                     "(2 locations)\", enabled only while every location is; Remove on it leaves "
                     "no row and no subscription of the GUI",
          seen3 && listed.size() == 1 && listed[0].locations == 2 &&
              shown_cell == QStringLiteral("main.bas:11 (2 locations)") && mixed_off &&
              m.rows().empty() && !own_left,
          "rows=" + std::to_string(listed.size()) + " cell=" + s(shown_cell) +
              " mixed_off=" + std::to_string(mixed_off) + " own_left=" + std::to_string(own_left));

    // QBPM-13 (D6): a gutter click on one place's dot removes that location only.
    {
        GutterView g(*fx.backend, m);
        if (auto* sb = g.panel->findChild<QScrollBar*>()) sb->setValue(0x8002);
        QApplication::processEvents();
        const bool added = answer_modal(QStringLiteral("main.bas:11"), [&]() { bp->on_add(); });
        g.panel->refresh();
        const bool dot_before = g.red_dot();
        g.click_gutter();
        bp->refresh();
        const auto left = m.rows();
        check("QBPM-13", "a gutter click on the $8002 dot of a two-place source line removes that "
                         "place only: one row remains, at $9000 on its page, reading \"main.bas:11\" "
                         "with no count",
              added && dot_before && left.size() == 1 && left[0].addr == 0x9000 &&
                  left[0].space == p90 && left[0].locations == 1 &&
                  cell(bp, 0, 3) == QStringLiteral("main.bas:11"),
              "rows=" + std::to_string(left.size()) + " cell=" + s(cell(bp, 0, 3)));
        m.clear_all();
    }

    // QADR-12 (closes #375's R08): Add Execute Breakpoint with a paged NAME keeps that name's space.
    // No loader puts a paged label into the backend's table before #375 stage 2, so the label is
    // injected through the same const seam panels_test uses; stage 2 replaces the injection with the
    // real SLD-label path.
    {
        const_cast<SymbolTable&>(fx.backend->symbols())
            .add_label("rvbanked", 0xC000, MemSpace::page(20));
        const bool seen = add_exec &&
            answer_modal(QStringLiteral("rvbanked"), [&]() { add_exec->trigger(); });
        const auto r = m.rows();
        check("QADR-12", "Add Execute Breakpoint with a paged name sets a plain Execute row on that "
                         "name's page",
              seen && r.size() == 1 && r[0].addr == 0xC000 && r[0].space == MemSpace::page(20) &&
                  r[0].group == jnext::dbg::GROUP_NONE,
              "rows=" + std::to_string(r.size()) +
                  (r.empty() ? std::string() : " space=" + std::to_string(unsigned(r[0].space.kind)) +
                                                   "/" + std::to_string(r[0].space.index)));
        m.clear_all();
    }

    // QBPM-14: Edit FROM a two-location group row removes the whole old group.
    {
        const bool added = answer_modal(QStringLiteral("main.bas:11"), [&]() { bp->on_add(); });
        bp->refresh();
        table->setCurrentCell(0, 1);
        const bool edited = answer_modal(QStringLiteral("main.bas:12"), [&]() { bp->on_edit(); });
        int mine = 0;
        bool new_only = false;
        for (const auto& si : fx.backend->subscriptions(false)) {
            if (si.owner != m.client()) continue;
            ++mine;
            new_only = si.filter.lo == 0x8005 && si.filter.space == p80;
        }
        check("QBPM-14", "Edit from a two-location source row to another line leaves none of the old "
                         "group's members armed, and the new target",
              added && edited && mine == 1 && new_only && m.rows().size() == 1 &&
                  m.rows()[0].addr == 0x8005,
              "mine=" + std::to_string(mine) + " rows=" + std::to_string(m.rows().size()));
        m.clear_all();
    }

    // QBPM-15: the Enabled cell of a group row acts on every location, and the run honours it.
    {
        const bool added = answer_modal(QStringLiteral("main.bas:11"), [&]() { bp->on_add(); });
        bp->refresh();
        auto members_enabled = [&](bool want) {
            int n = 0;
            for (const auto& si : fx.backend->subscriptions(false))
                if (si.owner == m.client()) {
                    if (si.enabled != want) return false;
                    ++n;
                }
            return n == 2;
        };
        if (QTableWidgetItem* en = table->item(0, 0)) en->setCheckState(Qt::Unchecked);
        const bool both_off = members_enabled(false);
        Z80Registers r = fx.emu.cpu().get_registers();
        r.PC = 0x8000;
        fx.emu.cpu().set_registers(r);
        fx.backend->run(jnext::dbg::CLIENT_NONE);
        fx.emu.run_frame();
        const bool passed = !fx.backend->state().paused;
        fx.backend->pause(jnext::dbg::CLIENT_NONE);
        bp->refresh();
        if (QTableWidgetItem* en = table->item(0, 0)) en->setCheckState(Qt::Checked);
        const bool both_on = members_enabled(true);
        check("QBPM-15", "unchecking the Enabled cell of a two-location row disables both places "
                         "(a run passes them) and checking it enables both",
              added && both_off && passed && both_on,
              "off=" + std::to_string(both_off) + " passed=" + std::to_string(passed) +
                  " on=" + std::to_string(both_on));
        m.clear_all();
    }

    // QBPM-16: a change to a NON-first member of a group is notified, with no manual refresh.
    {
        uint32_t kinds = 0;
        int n = 0;
        m.add_source_line(QStringLiteral("main.bas:11"));
        const auto c = QObject::connect(&m, &BreakpointModel::changed, [&](uint32_t k) { kinds |= k; ++n; });
        m.set_enabled(BreakpointModel::Execute, 0x9000, false, p90);
        const bool toggled = n >= 1 && (kinds & jnext::dbg::kind_bit(jnext::dbg::EventKind::Execute));
        kinds = 0;
        n = 0;
        m.remove(BreakpointModel::Execute, 0x9000, p90);
        const bool removed = n >= 1 && (kinds & jnext::dbg::kind_bit(jnext::dbg::EventKind::Execute));
        QObject::disconnect(c);
        check("QBPM-16", "disabling and removing the second location of a group emit changed() with "
                         "the Execute kind",
              toggled && removed && m.rows().size() == 1 && m.rows()[0].locations == 1,
              "toggled=" + std::to_string(toggled) + " removed=" + std::to_string(removed));
        m.clear_all();
    }

    // QBPM-17: the group row takes the LOWEST location's address and space, not the first listed
    // (resolve() sorts by space, so Page{20} $C000 lists before Page{30} $8000).
    {
        const std::string low = g_tmp->path().toStdString() + "/lowest.sld";
        write_text(low, std::string("|SLD.data.version|1\n") + "main.c|1||0" + kNextDevice +
                            "main.c|19||0|20|49152|T|\n" + "main.c|19||0|30|32768|T|\n");
        fx.backend->load_source_map(low, false);
        const jnext::dbg::GroupId g = m.add_source_line(QStringLiteral("main.c:19"));
        const auto r = m.rows();
        check("QBPM-17", "a group whose first-listed start is not the lowest address is one row at the "
                         "lowest location's address and space",
              g != jnext::dbg::GROUP_NONE && r.size() == 1 && r[0].locations == 2 &&
                  r[0].addr == 0x8000 && r[0].space == MemSpace::page(30) &&
                  r[0].addr_text == QStringLiteral("$8000 @1E"),
              r.empty() ? std::string() : s(r[0].addr_text));
        m.clear_all();
    }

    // QBPM-18: another client's legacy-page Execute (DZRP, the DSL) is a read-only row reading "$C000 @14".
    {
        jnext::dbg::ClientInfo ci;
        ci.name = "dzrp-like";
        ci.kind = jnext::dbg::ClientKind::Test;
        const jnext::dbg::ClientId other = fx.backend->attach(ci).value;
        jnext::dbg::Subscription legacy;
        legacy.kind = jnext::dbg::EventKind::Execute;
        legacy.action = jnext::dbg::Action::Stop;
        legacy.filter.lo = legacy.filter.hi = 0xC000;
        legacy.filter.page = 20;
        fx.backend->subscribe(other, legacy);
        m.add(BreakpointModel::Execute, 0x8000);   // the model re-reads the listing on its own mutations
        m.remove(BreakpointModel::Execute, 0x8000);
        const auto r = m.rows();
        check("QBPM-18", "another client's Execute with a legacy page qualifier is a typeless, "
                         "read-only row reading \"$C000 @14\"",
              r.size() == 1 && r[0].type == -1 && !r[0].own && r[0].addr_text == QStringLiteral("$C000 @14"),
              r.empty() ? std::string() : s(r[0].addr_text));
        fx.backend->detach(other);
    }
}

// ── QBPM-10: Edit never cancels silently ──────────────────────────────

void test_edit_type_refused() {
    Fixture fx;
    fx.enable();
    fx.load_map();
    BreakpointModel& m = fx.mgr->breakpoints();
    const MemSpace page = pg(fx.p80);
    BreakpointPanel* bp = fx.dbg() ? fx.dbg()->breakpoint_panel() : nullptr;
    auto* table = bp ? bp->findChild<QTableWidget*>() : nullptr;
    if (!bp || !table) { check("QBPM-10", "Breakpoints panel", false); return; }

    // Edit a source breakpoint and change only its Type. A source line names
    // an instruction, so the dialog refuses with a reason and stays open;
    // Cancel leaves the breakpoint as it was.
    m.add(BreakpointModel::Execute, 0x8005, page);
    bp->refresh();
    table->setCurrentCell(0, 1);
    QString reason;
    const int shown = drive_modals(
        [&](QWidget* w, int n) {
            auto* dlg = qobject_cast<QDialog*>(w);
            if (!dlg) return;
            if (n == 0) {
                if (auto* type = dlg->findChild<QComboBox*>()) type->setCurrentIndex(1);
                dlg->accept();
                return;
            }
            if (auto* err = dlg->findChild<QLabel*>(QStringLiteral("bp_dialog_error")))
                if (err->isVisible()) reason = err->text();
            dlg->reject();
        },
        [&]() { bp->on_edit(); });
    const auto rows = m.rows();
    check("QBPM-10", "changing only the Type of a source breakpoint is refused with a "
                     "reason and the dialog stays open; the breakpoint is unchanged",
          shown == 2 && reason.contains(QStringLiteral("Execute")) && rows.size() == 1 &&
              rows[0].type == BreakpointModel::Execute && rows[0].space == page,
          "shown=" + std::to_string(shown) + " reason=" + s(reason));
}

// ── T11: the Source tab's buttons ─────────────────────────────────────

void test_source_buttons() {
    const std::string dir = g_tmp->path().toStdString();
    {
        Fixture fx;
        fx.enable();
        fx.load_map();
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        // With no call tracked, Out is one instruction ($8000 -> $8001);
        // Into and Over would reach main.bas:11 at $8002.
        if (QPushButton* out = button(sp, QStringLiteral("Out"))) out->click();
        QApplication::processEvents();
        check("QSRC-17", "the Out button runs a source Step Out", fx.pc() == 0x8001,
              std::to_string(fx.pc()));
    }
    {
        Fixture fx(/*rewind_frames=*/8);
        fx.enable();
        fx.backend->set_trace_enabled(true);
        fx.load_map();
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        QPushButton* into = button(sp, QStringLiteral("Into"));
        for (int i = 0; i < 3 && into; ++i) { into->click(); QApplication::processEvents(); }
        // At main.bas:12. Back would land on sub.bas:1 ($9000); Reverse
        // Continue rewinds to the source breakpoint on main.bas:11.
        fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x8002, pg(fx.p80));
        fx.mgr->refresh_panels();
        const uint16_t from = fx.pc();
        if (QPushButton* rev = button(sp, QStringLiteral("Reverse"))) rev->click();
        QApplication::processEvents();
        check("QSRC-18", "the Reverse button runs Reverse Continue, not Back: back to the "
                         "breakpoint on main.bas:11",
              from == 0x8005 && fx.pc() == 0x8002,
              std::to_string(from) + " -> " + std::to_string(fx.pc()));
    }
    {
        Fixture fx;
        fx.enable();
        SourcePanel* sp = fx.dbg()->source_panel();
        const QString sld = QString::fromStdString(fx.sld);
        drive_modals([&](QWidget* w, int) { choose_file(w, sld); },
                     [&]() { button(sp, QStringLiteral("Load Source Map..."))->click(); });
        check("QSRC-19", "the Load Source Map button loads the chosen source map",
              !fx.backend->source_map().empty() &&
                  fx.backend->source_map().loaded_file() == fx.sld);

        const std::string mem = dir + "/buttons.Memory.txt";
        write_text(mem, "8005: ._Landing\n");
        drive_modals([&](QWidget* w, int) { choose_file(w, QString::fromStdString(mem)); },
                     [&]() { button(sp, QStringLiteral("Load Memory.txt..."))->click(); });
        check("QSRC-20", "the Load Memory.txt button loads the chosen NextBuild symbols",
              fx.backend->lookup_name("Landing") == std::optional<uint16_t>(0x8005));
    }
    {
        // An SLD whose binary identity is not the bytes in memory: offered,
        // and loaded only when the offer is accepted.
        Fixture fx;
        fx.enable();
        SourcePanel* sp = fx.dbg()->source_panel();
        const std::string wrong = dir + "/wrong.sld";
        const std::string a = std::to_string(fx.p80);
        write_text(wrong, std::string("|SLD.data.version|1\n||program.name:other\n") +
                              "||program.sha256:9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a\n"
                              "||program.org:32768\n||program.size:4\n" +
                              "main.bas|10||0" + kNextDevice + "main.bas|10||0|" + a + "|32768|T|\n");
        bool offered = false;
        auto run = [&](QMessageBox::StandardButton answer) {
            drive_modals(
                [&](QWidget* w, int) {
                    if (auto* mb = qobject_cast<QMessageBox*>(w))
                        if (mb->windowTitle() == QStringLiteral("Source Map Identity Mismatch")) {
                            offered = true;
                            click_box(w, answer);
                            return;
                        }
                    choose_file(w, QString::fromStdString(wrong));
                },
                [&]() { button(sp, QStringLiteral("Load Source Map..."))->click(); });
        };
        run(QMessageBox::No);
        const bool declined = offered && fx.backend->source_map().empty();
        offered = false;
        run(QMessageBox::Yes);
        check("QSRC-21", "a source map whose identity does not match is offered; No loads "
                         "nothing, Yes loads it",
              declined && offered && fx.backend->source_map().loaded_file() == wrong);
    }
    {
        // A step on a corrupt machine asks first; No leaves it where it is.
        Fixture fx;
        fx.enable();
        fx.load_map();
        fx.mgr->refresh_panels();
        const uint8_t garbage[4] = {0, 0, 0, 0};
        StateReader r(garbage, sizeof(garbage));
        fx.emu.load_state(r);
        fx.backend->load_source_map(fx.sld, false);   // the restore may have reset the view
        fx.mgr->refresh_panels();
        bool asked = false;
        drive_modals(
            [&](QWidget* w, int) {
                if (auto* mb = qobject_cast<QMessageBox*>(w))
                    asked = mb->windowTitle() == QStringLiteral("Machine State Corrupt");
                click_box(w, QMessageBox::No);
            },
            [&]() {
                if (QPushButton* into = button(fx.dbg()->source_panel(), QStringLiteral("Into")))
                    into->click();
            });
        check("QSRC-22", "on a corrupt machine a source step asks first, and No does not step",
              !fx.emu.last_state_error().empty() && asked && fx.pc() == 0x8000,
              "asked=" + std::to_string(asked) + " pc=" + std::to_string(fx.pc()));
    }
}

// The step buttons' enabled state: each condition on its own.
void test_source_button_states() {
    {
        Fixture fx;
        fx.enable();
        fx.load_map();
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        QPushButton* into = button(sp, QStringLiteral("Into"));
        const bool paused_on = into && into->isEnabled();
        fx.dbg()->update_actions(/*is_paused=*/false);
        check("QSRC-33", "with a map loaded, the forward steps are disabled while the machine runs",
              paused_on && into && !into->isEnabled() &&
                  !button(sp, QStringLiteral("Over"))->isEnabled() &&
                  !button(sp, QStringLiteral("Out"))->isEnabled());
    }
    {
        // Rewind holds frames, but there is no trace to find the target in.
        Fixture fx(/*rewind_frames=*/8);
        fx.enable();
        fx.backend->set_trace_enabled(false);   // a rewind machine starts with it on
        fx.load_map();
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        if (QPushButton* into = button(sp, QStringLiteral("Into"))) { into->click(); into->click(); }
        QApplication::processEvents();
        fx.mgr->refresh_panels();
        check("QSRC-34", "with rewind but no trace, Back and Reverse are disabled",
              !fx.backend->rewind_blocked() && !fx.backend->trace_enabled() &&
                  !button(sp, QStringLiteral("Back"))->isEnabled() &&
                  !button(sp, QStringLiteral("Reverse"))->isEnabled(),
              "blocked=" + std::to_string(fx.backend->rewind_blocked().has_value()) +
                  " trace=" + std::to_string(fx.backend->trace_enabled()));
    }
    {
        // Trace is on, but there is no rewind to go back with.
        Fixture fx;
        fx.enable();
        fx.backend->set_trace_enabled(true);
        fx.load_map();
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        check("QSRC-35", "with trace but no rewind, Back and Reverse are disabled",
              fx.backend->rewind_blocked() && fx.backend->trace_enabled() &&
                  !button(sp, QStringLiteral("Back"))->isEnabled() &&
                  !button(sp, QStringLiteral("Reverse"))->isEnabled());
    }
}

// ── M46: a refused backward step says why ─────────────────────────────

void test_source_reverse_refused() {
    Fixture fx(/*rewind_frames=*/8);
    fx.enable();
    fx.backend->set_trace_enabled(true);
    fx.load_map();
    fx.mgr->refresh_panels();
    SourcePanel* sp = fx.dbg()->source_panel();
    QPushButton* into = button(sp, QStringLiteral("Into"));
    for (int i = 0; i < 2 && into; ++i) { into->click(); QApplication::processEvents(); }
    fx.mgr->refresh_panels();
    // Two statements in, with no source breakpoint anywhere: Reverse has
    // nowhere to go.
    QPushButton* rev = button(sp, QStringLiteral("Reverse"));
    const bool enabled = rev && rev->isEnabled();
    const uint16_t from = fx.pc();
    if (rev) rev->click();
    QApplication::processEvents();
    const QString bar = fx.dbg()->statusBar()->currentMessage();
    check("QSRC-37", "Reverse with no earlier source breakpoint is refused: the machine "
                     "stays and the debugger window's status bar says why",
          enabled && from != 0x8000 && fx.pc() == from &&
              bar.contains(QStringLiteral("no earlier source breakpoint")),
          s(bar) + " pc " + std::to_string(from) + " -> " + std::to_string(fx.pc()));
}

// ── T11: the Source tab's messages and source file search ─────────────

void test_source_messages() {
    const std::string dir = g_tmp->path().toStdString();
    {
        // A record names src/main.c; the map is in build/ beside it.
        Fixture fx;
        fx.enable();
        const std::string proj = dir + "/walk";
        std::filesystem::create_directories(proj + "/build");
        std::filesystem::create_directories(proj + "/src");
        std::string main_c;
        for (int i = 1; i <= 20; ++i) main_c += "/* c line " + std::to_string(i) + " */\n";
        write_text(proj + "/src/main.c", main_c);
        write_text(proj + "/build/prog.sld",
                   std::string("|SLD.data.version|1\nsrc/main.c|19||0") + kNextDevice +
                       "src/main.c|19||0|" + std::to_string(fx.p80) + "|32768|T|\n");
        fx.backend->load_source_map(proj + "/build/prog.sld", false);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        check("QSRC-23", "a source file named relative to a parent of the map's directory is "
                         "found: ../src/main.c, line 19 shown",
              sp->highlighted_line() == 19, s(sp->location_text()));
    }
    {
        Fixture fx;
        fx.enable();
        fx.load_map();
        Z80Registers r = fx.emu.cpu().get_registers();
        r.PC = 0x0000;
        fx.emu.cpu().set_registers(r);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        check("QSRC-24", "the PC in ROM reads \"ROM or overlay\" with no source mapping",
              sp->location_text() ==
                  QStringLiteral("$0000 (ROM or overlay): no source mapping; use the disassembly"),
              s(sp->location_text()));
    }
    {
        Fixture fx;
        fx.enable();
        const std::string gone = dir + "/gone.sld";
        write_text(gone, std::string("|SLD.data.version|1\nnowhere.bas|10||0") + kNextDevice +
                             "nowhere.bas|10||0|" + std::to_string(fx.p80) + "|32768|T|\n");
        fx.backend->load_source_map(gone, false);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        check("QSRC-25", "a mapped line whose file cannot be found says so, in ASCII",
              sp->location_text() == QStringLiteral("nowhere.bas:10 - source file not found"),
              s(sp->location_text()));
    }
}

// ── N2: hex in the Source tab is upper case ──────────────────────────

void test_source_hex() {
    Fixture fx;
    fx.enable();
    // A RAM address whose page number has a letter in its hex ("0A", "2E"...),
    // and whose own hex has letters too.
    uint16_t addr = 0;
    uint8_t page = 0;
    for (uint32_t a = 0x4000; a < 0x10000 && !addr; a += 0x2000) {
        const uint8_t pg = ram_page_of(fx.backend->source_page(static_cast<uint16_t>(a)));
        if (pg != NOT_RAM_PAGE && QString::asprintf("%02X", pg).contains(QRegularExpression("[A-F]"))) {
            addr = static_cast<uint16_t>(a | 0x0BCD);
            page = pg;
        }
    }
    const std::string path = g_tmp->path().toStdString() + "/hex.sld";
    write_text(path, std::string("|SLD.data.version|1\nmain.bas|10||0") + kNextDevice +
                         "main.bas|10||0|" + std::to_string(page) + "|" + std::to_string(addr) +
                         "|T|\n");
    fx.backend->load_source_map(path, false);
    Z80Registers r = fx.emu.cpu().get_registers();
    r.PC = addr;
    fx.emu.cpu().set_registers(r);
    fx.mgr->refresh_panels();
    SourcePanel* sp = fx.dbg()->source_panel();
    const QString want = QString::asprintf("main.bas:10  $%04X  page %02X", addr, page);
    const QString mapped = sp->location_text();
    r.PC = static_cast<uint16_t>(addr - 1);   // below the only record; addr + 1 is inside its range
    fx.emu.cpu().set_registers(r);
    fx.mgr->refresh_panels();
    const QString want_unmapped =
        QString::asprintf("$%04X (page %02X): no source mapping; use the disassembly", addr - 1, page);
    check("QSRC-44", "the Source tab prints the PC and the page in upper-case hex, mapped "
                     "or not",
          addr != 0 && mapped == want && sp->location_text() == want_unmapped,
          s(mapped) + " | " + s(sp->location_text()) + " | want " + s(want));
}

// ── B-F5: a paused Source tab can be scrolled ─────────────────────────

void test_source_scroll() {
    Fixture fx;
    fx.enable();
    const std::string dir = g_tmp->path().toStdString();
    std::string long_bas;
    for (int i = 1; i <= 400; ++i) long_bas += "REM line " + std::to_string(i) + "\n";
    write_text(dir + "/long.bas", long_bas);
    const std::string a = std::to_string(fx.p80);
    write_text(dir + "/long.sld",
               std::string("|SLD.data.version|1\nlong.bas|10||0") + kNextDevice +
                   "long.bas|10||0|" + a + "|32768|T|\n" +
                   "long.bas|11||0|" + a + "|32770|T|\n");
    fx.backend->load_source_map(dir + "/long.sld", false);
    fx.mgr->refresh_panels();
    SourcePanel* sp = fx.dbg()->source_panel();
    auto* editor = sp->findChild<QPlainTextEdit*>();
    QScrollBar* bar = editor ? editor->verticalScrollBar() : nullptr;
    if (!bar) { check("QSRC-42", "Source editor", false); return; }
    const bool placed = sp->highlighted_line() == 10;

    // The user scrolls away from the line; the window refreshes every panel on
    // every tick while paused.
    bar->setValue(360);
    const int scrolled = bar->value();
    fx.mgr->refresh_panels();
    QApplication::processEvents();
    const int after_tick = bar->value();

    // A step to another statement puts the view on its line again.
    if (QPushButton* into = button(sp, QStringLiteral("Into"))) into->click();
    QApplication::processEvents();
    const int after_step = bar->value();
    check("QSRC-42", "a paused tick leaves a scrolled Source tab where the user put it, and "
                     "a step puts the view on the new line",
          placed && scrolled >= 300 && after_tick == scrolled &&
              sp->highlighted_line() == 11 && after_step < 100,
          "scrolled " + std::to_string(scrolled) + " after tick " + std::to_string(after_tick) +
              " after step " + std::to_string(after_step) + " line " +
              std::to_string(sp->highlighted_line()));
}

// The view is put on the highlighted line again whenever any one of its
// inputs changes: the PC alone, the line alone, the file alone, or the text
// being reloaded. Each is isolated from the others here.
void test_source_recentre_terms() {
    Fixture fx;
    fx.enable();
    const std::string dir = g_tmp->path().toStdString();
    std::string long_bas, long2_bas;
    for (int i = 1; i <= 400; ++i) {
        long_bas += "REM line " + std::to_string(i) + "\n";
        long2_bas += "PRINT " + std::to_string(i) + "\n";
    }
    write_text(dir + "/long.bas", long_bas);
    write_text(dir + "/long2.bas", long2_bas);
    const std::string a = std::to_string(fx.p80);
    auto map = [&](const std::string& name, const std::string& records) {
        const std::string path = dir + "/" + name;
        write_text(path, std::string("|SLD.data.version|1\nx|1||0") + kNextDevice + records);
        return path;
    };
    // $8000 and $8001 are both line 10 of long.bas; later maps put $8001 on
    // another line of the same file, then on the same line of another file.
    const std::string map_a = map("terms-a.sld", "long.bas|10||0|" + a + "|32768|T|\n" +
                                                   "long.bas|10||0|" + a + "|32769|T|\n");
    const std::string map_b = map("terms-b.sld", "long.bas|300||0|" + a + "|32769|T|\n");
    const std::string map_c = map("terms-c.sld", "long2.bas|300||0|" + a + "|32769|T|\n");
    fx.backend->load_source_map(map_a, false);
    fx.mgr->refresh_panels();
    SourcePanel* sp = fx.dbg()->source_panel();
    auto* editor = sp->findChild<QPlainTextEdit*>();
    QScrollBar* bar = editor ? editor->verticalScrollBar() : nullptr;
    if (!bar) { check("QSRC-45", "Source editor", false); return; }
    auto tick = [&]() { fx.mgr->refresh_panels(); QApplication::processEvents(); };

    // Only the PC changes: one instruction on, the same line.
    bar->setValue(360);
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);
    tick();
    const bool pc_only = fx.pc() == 0x8001 && sp->highlighted_line() == 10 && bar->value() < 100;
    const int v_pc = bar->value();

    // Only the line: the same file and PC, another map.
    bar->setValue(0);
    fx.backend->load_source_map(map_b, false);
    tick();
    const bool line_only = sp->highlighted_line() == 300 && bar->value() > 250;
    const int v_line = bar->value();

    // Only the file: the same line and PC.
    bar->setValue(0);
    fx.backend->load_source_map(map_c, false);
    tick();
    const bool file_only = sp->highlighted_line() == 300 && bar->value() > 250;
    const int v_file = bar->value();

    // Only the text: reloaded because it changed on disk.
    const std::string long2 = dir + "/long2.bas";
    namespace fs = std::filesystem;
    fs::last_write_time(long2, fs::last_write_time(long2) + std::chrono::seconds(5));
    bar->setValue(0);
    tick();
    const bool reloaded = bar->value() > 250;
    const int v_reload = bar->value();
    check("QSRC-45", "the Source tab centres the line again when only the PC, only the line, "
                     "only the file or only the text changed",
          pc_only && line_only && file_only && reloaded,
          "pc " + std::to_string(v_pc) + " line " + std::to_string(v_line) + " file " +
              std::to_string(v_file) + " reload " + std::to_string(v_reload));
}

// A stop at the very PC and line the tab last showed still puts the view on
// the line, after the user scrolled away during the previous pause: a
// breakpoint, a Pause and a Step each end in the window's follow-PC, as the
// disassembly's does.
void test_source_follow_stop() {
    Fixture fx;
    fx.enable();
    const std::string dir = g_tmp->path().toStdString();
    std::string long_bas;
    for (int i = 1; i <= 400; ++i) long_bas += "REM line " + std::to_string(i) + "\n";
    write_text(dir + "/long.bas", long_bas);
    write_text(dir + "/follow.sld",
               std::string("|SLD.data.version|1\nlong.bas|10||0") + kNextDevice +
                   "long.bas|10||0|" + std::to_string(fx.p80) + "|32768|T|\n");
    // A loop on one line, so every stop below is at $8000, line 10.
    fx.emu.mmu().write(0x8000, 0x18);   // JR $
    fx.emu.mmu().write(0x8001, 0xFE);
    fx.backend->load_source_map(dir + "/follow.sld", false);
    fx.mgr->refresh_panels();
    SourcePanel* sp = fx.dbg()->source_panel();
    auto* editor = sp->findChild<QPlainTextEdit*>();
    QScrollBar* bar = editor ? editor->verticalScrollBar() : nullptr;
    if (!bar) { check("QSRC-46", "Source editor", false); return; }
    auto scroll_away = [&]() {
        bar->setValue(360);
        QApplication::processEvents();
        return bar->value();
    };
    const bool placed = sp->highlighted_line() == 10 && fx.pc() == 0x8000 && bar->value() < 100;

    // A paused tick leaves the view where the user put it.
    const int away = scroll_away();
    fx.mgr->refresh_panels();
    const bool tick_keeps = away >= 300 && bar->value() == away;

    // A breakpoint hit at $8000: Run, the stop is processed by the tick.
    scroll_away();
    fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x8000);
    fx.mgr->on_run();
    for (int i = 0; i < 8 && !fx.backend->state().paused; ++i) fx.emu.run_frame();
    fx.mgr->check_breakpoint_hit();
    QApplication::processEvents();
    const int at_break = bar->value();
    const bool break_ok = fx.backend->state().paused && fx.pc() == 0x8000 &&
                          sp->highlighted_line() == 10 && at_break < 100;
    fx.mgr->breakpoints().clear_all();

    // A Pause: Run, then Pause the loop at $8000.
    fx.mgr->on_run();
    fx.emu.run_frame();
    scroll_away();
    fx.mgr->on_pause();
    QApplication::processEvents();
    const int at_pause = bar->value();
    const bool pause_ok = fx.backend->state().paused && fx.pc() == 0x8000 &&
                          sp->highlighted_line() == 10 && at_pause < 100;

    // A Step: JR $ lands on $8000 again.
    scroll_away();
    fx.mgr->on_step_into();
    QApplication::processEvents();
    const int at_step = bar->value();
    const bool step_ok = fx.pc() == 0x8000 && sp->highlighted_line() == 10 && at_step < 100;
    check("QSRC-46", "a stop at the PC and line already shown (a breakpoint, a Pause, a Step) "
                     "puts the Source tab back on the line after the user scrolled away",
          placed && tick_keeps && break_ok && pause_ok && step_ok,
          "break " + std::to_string(at_break) + " pause " + std::to_string(at_pause) +
              " step " + std::to_string(at_step) + " tick_keeps " +
              std::to_string(tick_keeps));
}

// ── C4: while the machine runs the Source tab says so ─────────────────

void test_source_running() {
    Fixture fx;
    fx.enable();
    fx.load_map();
    fx.mgr->refresh_panels();
    SourcePanel* sp = fx.dbg()->source_panel();
    auto* editor = sp->findChild<QPlainTextEdit*>();
    const bool stopped = sp->highlighted_line() == 10 && editor &&
                         !editor->extraSelections().isEmpty();

    // Running: the last stop's line is not where the machine is.
    fx.backend->run(jnext::dbg::CLIENT_NONE);
    sp->refresh();
    const QString running = sp->location_text();
    const bool cleared = sp->highlighted_line() == 0 && editor &&
                         editor->extraSelections().isEmpty();

    // The next stop shows its line again.
    fx.backend->pause(jnext::dbg::CLIENT_NONE);
    sp->refresh();
    check("QSRC-43", "while the machine runs the Source tab says Running and drops the "
                     "highlight of the last stop; the next pause shows the line again",
          stopped && running == QStringLiteral("Running") && cleared &&
              sp->highlighted_line() == 10 && sp->location_text().startsWith("main.bas:10"),
          s(running) + " | " + s(sp->location_text()));
}

// ── C1: a refused sidecar says why ────────────────────────────────────

void test_sidecar_reason() {
    const std::string dir = g_tmp->path().toStdString();
    {
        // A program whose SLD is refused: the Source tab and the main
        // window's status bar say why.
        Fixture fx;
        fx.enable();
        const std::string nex = dir + "/refused.nex";
        write_text(nex, "not really a nex");
        write_text(dir + "/refused.sld", "this is not an SLD\n");
        fx.mgr->report_sidecars(fx.backend->load_program_sidecars(nex));
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        const QString bar = fx.win.statusBar()->currentMessage();
        check("QSRC-26", "a refused sidecar names its reason in the Source tab and the "
                         "status bar",
              sp->location_text().contains(QStringLiteral("rejected: missing SLD.data.version")) &&
                  bar.contains(QStringLiteral("rejected: missing SLD.data.version")),
              s(sp->location_text()) + " | " + s(bar));
        QLabel* where = nullptr;
        for (QLabel* l : sp->findChildren<QLabel*>())
            if (l->text() == sp->location_text()) where = l;
        check("QSRC-31", "the message wraps, so a long path does not push the reason out "
                         "of a narrow tab",
              where && where->wordWrap());
    }
}

// ── C1: a reason reported before the window exists reaches the tab ────

void test_sidecar_reason_late_window() {
    const std::string dir = g_tmp->path().toStdString();
    Fixture fx;
    // A program whose SLD is refused, reported while there is no debugger
    // window yet (a program loaded from the command line).
    write_text(dir + "/late.nex", "not really a nex");
    write_text(dir + "/late.sld", "this is not an SLD\n");
    fx.mgr->report_sidecars(fx.backend->load_program_sidecars(dir + "/late.nex"));
    fx.enable();
    fx.mgr->refresh_panels();
    SourcePanel* sp = fx.dbg()->source_panel();
    const QString first = sp->location_text();

    // The next program has no sidecars: the tab goes back to its plain text.
    fx.mgr->report_sidecars(fx.backend->load_program_sidecars(dir + "/clean.nex"));
    fx.mgr->refresh_panels();
    check("QSRC-40", "a refused sidecar's reason reported before the window exists shows in "
                     "the Source tab built later, and a later clean load clears it",
          first.contains(QStringLiteral("rejected: missing SLD.data.version")) &&
              sp->location_text() == QStringLiteral("No source map loaded"),
          s(first) + " | " + s(sp->location_text()));
}

// ── GH #342: the tab reads the backend's reason, only the source half's ──

void test_source_map_lifetime() {
    const std::string dir = g_tmp->path().toStdString();
    {
        // A map loaded by hand with no identity, then another landing (a
        // reset): stale, the tab names the file and the backend's reason, and
        // the steps are disabled.
        Fixture fx;
        fx.enable();
        fx.load_map();
        fx.backend->reset(jnext::dbg::CLIENT_NONE, jnext::dbg::ResetKind::Soft);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        const auto st = fx.backend->source_map_status();
        const QString text = sp->location_text();
        check("QSRC-58", "a map loaded by hand that went stale: the Source tab names the file "
                         "and the backend's reason, and the steps are disabled",
              !st.reason.empty() && text.contains(QString::fromStdString(fx.sld)) &&
                  text.contains(QString::fromStdString(st.reason)) &&
                  !button(sp, QStringLiteral("Into"))->isEnabled() &&
                  !button(sp, QStringLiteral("Over"))->isEnabled(),
              s(text));
    }
    {
        // Memory.txt refused, SLD attached, then a reset drops the SLD: the tab
        // shows why the map went, not the Memory.txt refusal.
        Fixture fx;
        fx.enable();
        const std::string nex = dir + "/q59.nex";
        write_text(nex, "");
        write_text(dir + "/q59.Memory.txt", "8000: ._Old\n");
        std::error_code ec;
        std::filesystem::resize_file(dir + "/q59.Memory.txt", SOURCE_FILE_MAX_BYTES + 1, ec);
        std::filesystem::copy_file(fx.sld, dir + "/q59.sld",
                                   std::filesystem::copy_options::overwrite_existing);
        const auto r = fx.backend->load_program_sidecars(nex);
        fx.mgr->report_sidecars(r);
        fx.backend->reset(jnext::dbg::CLIENT_NONE, jnext::dbg::ResetKind::Soft);
        fx.mgr->refresh_panels();
        const QString text = fx.dbg()->source_panel()->location_text();
        check("QSRC-59", "after a reset drops the attached map the tab gives the drop's reason, "
                         "not the Memory.txt refusal of the load",
              !ec && r.symbols_unreadable && r.sources > 0 &&
                  text.startsWith(QStringLiteral("No source map loaded: ")) &&
                  !text.contains(QStringLiteral("NextBuild symbols")) &&
                  text.contains(QStringLiteral("binary identity")),
              s(text));
    }
    {
        // The panel reads the load result's reason with no refresh of its own:
        // report_sidecars() refreshes the open tab.
        Fixture fx;
        fx.enable();
        const std::string nex = dir + "/q60.nex";
        write_text(nex, "");
        write_text(dir + "/q60.sld", "this is not an SLD\n");
        fx.mgr->report_sidecars(fx.backend->load_program_sidecars(nex));
        const QString text = fx.dbg()->source_panel()->location_text();
        check("QSRC-60", "report_sidecars() refreshes the open Source tab: the new reason is "
                         "shown without another refresh",
              text.contains(QStringLiteral("rejected: missing SLD.data.version")), s(text));
    }
    {
        // A panel that holds &symbols(): after a rewind to before the attach, the
        // first refresh shows the address, not the symbol of a program not yet loaded.
        Fixture fx(/*rewind_frames=*/8);
        fx.emu.mmu().write(0x9000, 0x18);   // JR $ : the call never returns
        fx.emu.mmu().write(0x9001, 0xFE);
        fx.enable();
        fx.backend->set_call_stack_enabled(true);
        fx.backend->run(jnext::dbg::CLIENT_NONE);
        for (int i = 0; i < 2; ++i) fx.emu.run_frame();
        fx.backend->pause(jnext::dbg::CLIENT_NONE);
        const uint32_t before = fx.backend->rewind_range().newest_frame;
        write_text(dir + "/q61.nex", "");
        write_text(dir + "/q61.Memory.txt", "9000: ._SubFn\n");
        fx.backend->load_program_sidecars(dir + "/q61.nex");
        fx.backend->run(jnext::dbg::CLIENT_NONE);
        for (int i = 0; i < 2; ++i) fx.emu.run_frame();
        fx.backend->pause(jnext::dbg::CLIENT_NONE);
        const auto rr = fx.backend->rewind_to_frame(jnext::dbg::CLIENT_NONE, before);
        CallStackPanel* cs = fx.dbg()->callstack_panel();
        if (cs) cs->set_paused(true);
        fx.mgr->refresh_panels();
        check("QSRC-61", "a panel holding the symbol table shows no symbol of a program that has "
                         "not been loaded at the point a rewind landed on",
              rr == jnext::dbg::Result::Ok && cs && cell(cs, 0, 3) == QStringLiteral("9000"),
              s(cell(cs, 0, 3)));
    }
}

// ── QCSP-02: the caller's page, not the target's ───────────────────────

void test_call_stack_pages() {
    Fixture fx;
    fx.enable();
    // CALL $C000 from main.bas:11: $C000 is another 8K page than $8000.
    fx.emu.mmu().write(0x8003, 0x00);
    fx.emu.mmu().write(0x8004, 0xC0);
    fx.emu.mmu().write(0xC000, 0x00);
    fx.emu.mmu().write(0xC001, 0xC9);
    const unsigned pc0 = fx.backend->effective_page(0xC000);
    const std::string a = std::to_string(fx.p80);
    const std::string path = g_tmp->path().toStdString() + "/far.sld";
    write_text(path, std::string("|SLD.data.version|1\nmain.bas|10||0") + kNextDevice +
                         "main.bas|10||0|" + a + "|32768|T|\n" +
                         "main.bas|11||0|" + a + "|32770|T|\n" +
                         "main.bas|12||0|" + a + "|32773|T|\n" +
                         "sub.bas|1||0|" + std::to_string(pc0) + "|49152|T|\n");
    fx.backend->load_source_map(path, false);
    fx.backend->set_call_stack_enabled(true);
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);   // the CALL
    CallStackPanel* cs = fx.dbg() ? fx.dbg()->callstack_panel() : nullptr;
    if (cs) { cs->set_paused(true); cs->refresh(); }
    check("QCSP-02", "Called from names the caller's line on the caller's page when the "
                     "target is on another page",
          pc0 != fx.p80 && fx.pc() == 0xC000 && cell(cs, 0, 4) == QStringLiteral("main.bas:11"),
          "pages " + std::to_string(fx.p80) + "/" + std::to_string(pc0) + " " + s(cell(cs, 0, 4)));
}

// ── Paged symbol labels in the panels (GH #375) ────────────────────────
// Two labels at $C000, assembled for pages 20 and 30. The tables below are the
// test's own (set_symbol_table), so no loader is involved: what is under test
// is which memory each panel asks about.

void test_paged_symbols() {
    Fixture fx;
    fx.enable();
    SymbolTable t;
    t.add_label("banked", 0xC000, MemSpace::page(20));
    t.add_label("other_bank", 0xC000, MemSpace::page(30));
    t.add_label("scr", 0x4000, std::nullopt);
    BreakpointModel& m = fx.mgr->breakpoints();
    BreakpointPanel* bp = fx.dbg() ? fx.dbg()->breakpoint_panel() : nullptr;
    CallStackPanel* cs = fx.dbg() ? fx.dbg()->callstack_panel() : nullptr;
    if (!bp || !cs) {
        check("QSYM-03", "panels", false);
        check("QSYM-04", "panels", false);
        check("QSYM-05", "panels", false);
        check("QSYM-10", "panels", false);
        check("QSYM-11", "panels", false);
        check("QSYM-12", "panels", false);
        return;
    }
    bp->set_symbol_table(&t);
    cs->set_symbol_table(&t);

    // The Symbol column reads the breakpoint's own page, else the page mapped now.
    fx.emu.mmu().set_page(6, 20);
    m.add(BreakpointModel::Execute, 0xC000, MemSpace::page(30));
    bp->refresh();
    const QString own = cell(bp, 0, 3);
    m.remove(BreakpointModel::Execute, 0xC000, MemSpace::page(30));
    m.add(BreakpointModel::Execute, 0xC000);
    bp->refresh();
    const QString mapped20 = cell(bp, 0, 3);
    fx.emu.mmu().set_page(6, 30);
    bp->refresh();
    const QString mapped30 = cell(bp, 0, 3);
    fx.emu.mmu().set_page(6, 21);
    bp->refresh();
    const QString mapped21 = cell(bp, 0, 3);
    m.clear_all();
    check("QSYM-03", "the Breakpoints Symbol column names a page-qualified row after its own page, an "
                     "unqualified one after the page mapped at its address now, and nothing for a third page",
          own == QStringLiteral("other_bank") && mapped20 == QStringLiteral("banked") &&
              mapped30 == QStringLiteral("other_bank") && mapped21.isEmpty(),
          s(own) + " / " + s(mapped20) + " / " + s(mapped30) + " / " + s(mapped21));

    // The Add dialog keeps the page of a paged name; a number or an unpaged name has none.
    const bool seen_a = answer_modal(QStringLiteral("banked"), [&]() { bp->on_add(); });
    const auto after_a = m.rows();
    const bool paged = seen_a && after_a.size() == 1 && after_a[0].addr == 0xC000 && after_a[0].space == MemSpace::page(20);
    m.clear_all();
    const bool seen_b = answer_modal(QStringLiteral("scr"), [&]() { bp->on_add(); });
    const auto after_b = m.rows();
    const bool unpaged = seen_b && after_b.size() == 1 && after_b[0].addr == 0x4000 &&
                         after_b[0].space == MemSpace::cpu();
    m.clear_all();
    const bool seen_c = answer_modal(QStringLiteral("C000"), [&]() { bp->on_add(); });
    const auto after_c = m.rows();
    const bool number = seen_c && after_c.size() == 1 && after_c[0].addr == 0xC000 &&
                        after_c[0].space == MemSpace::cpu();
    m.clear_all();
    check("QSYM-04", "the Add dialog sets a breakpoint on a paged name in that name's page, and on an "
                     "unpaged name or a number in any page",
          paged && unpaged && number,
          "paged=" + std::to_string(paged) + " unpaged=" + std::to_string(unpaged) +
              " number=" + std::to_string(number));

    // The page qualifier is an Execute breakpoint's: a Read breakpoint by a paged name has none.
    bool seen_r = false;
    drive_modals(
        [&](QWidget* w, int n) {
            auto* dlg = qobject_cast<QDialog*>(w);
            if (!dlg) return;
            if (n == 0) {
                seen_r = true;
                if (QLineEdit* e = dlg->findChild<QLineEdit*>()) e->setText("banked");
                if (auto* c = dlg->findChild<QComboBox*>()) c->setCurrentIndex(BreakpointModel::Read);
                dlg->accept();
                return;
            }
            dlg->reject();
        },
        [&]() { bp->on_add(); });
    const auto after_r = m.rows();
    check("QSYM-10", "the Add dialog gives a Read breakpoint on a paged name no page: only an Execute "
                     "breakpoint is page-qualified",
          seen_r && after_r.size() == 1 && after_r[0].type == BreakpointModel::Read &&
              after_r[0].addr == 0xC000 && after_r[0].space == MemSpace::cpu(),
          "rows=" + std::to_string(after_r.size()) +
              (after_r.empty() ? std::string() : " type=" + std::to_string(after_r[0].type) +
                                                     " space=" + std::to_string(after_r[0].space.index)));
    m.clear_all();

    // Edit moves a DISABLED Read breakpoint to a paged name: it stays disabled and page-less. The edit
    // re-enables through set_enabled() with the dialog's page, which finds no row if the page leaked.
    m.add(BreakpointModel::Read, 0x4000);
    bp->refresh();
    auto* table = bp->findChild<QTableWidget*>();
    if (table && table->item(0, 0)) table->item(0, 0)->setCheckState(Qt::Unchecked);
    if (table) table->setCurrentCell(0, 1);
    const bool edited = table && answer_modal(QStringLiteral("banked"), [&]() { bp->on_edit(); });
    const auto after_e = m.rows();
    check("QSYM-11", "editing a disabled Read breakpoint to a paged name keeps it disabled and page-less",
          edited && after_e.size() == 1 && after_e[0].type == BreakpointModel::Read &&
              after_e[0].addr == 0xC000 && after_e[0].space == MemSpace::cpu() && !after_e[0].enabled,
          "rows=" + std::to_string(after_e.size()) +
              (after_e.empty() ? std::string() : " space=" + std::to_string(after_e[0].space.index) +
                                                     " enabled=" + std::to_string(after_e[0].enabled)));
    m.clear_all();

    // The Call Stack names the target in the memory it was entered in, not the one mapped now.
    SymbolTable ct;
    ct.add_label("sub_recorded", 0x9000, MemSpace::page(static_cast<uint16_t>(fx.p90)));
    ct.add_label("sub_now", 0x9000, MemSpace::page(30));
    cs->set_symbol_table(&ct);
    fx.backend->set_call_stack_enabled(true);
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);   // CALL $9000
    const bool at_call_target = fx.pc() == 0x9000;
    // Before that fetch the frame has no target_space: the target is named by the page mapped now.
    fx.emu.mmu().set_page(4, 30);
    cs->set_paused(true);
    cs->refresh();
    const QString unrecorded = cell(cs, 0, 3);
    fx.emu.mmu().set_page(4, static_cast<uint8_t>(fx.p80));
    check("QSYM-12", "a Call Stack frame whose target has not been fetched yet is named after the page mapped now",
          at_call_target && unrecorded == QStringLiteral("sub_now"), s(unrecorded));
    // The frame's target_space is filled by the first fetch AT the target: run the NOP there.
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);
    const bool at_target = fx.pc() == 0x9001;
    fx.emu.mmu().set_page(4, 30);                      // another page now supplies $9000
    cs->set_paused(true);
    cs->refresh();
    const QString target = cell(cs, 0, 3);
    fx.emu.mmu().set_page(4, static_cast<uint8_t>(fx.p80));
    check("QSYM-05", "the Call Stack names a frame's target after the page it was entered in, "
                     "though another page is mapped there now",
          at_target && target == QStringLiteral("sub_recorded"), s(target));
}

// ── QCSP-03 (GH #340): a caller fetched from ROM is not RAM page 0 ─────
//
// The frame records WHAT supplied the CALL/RST (here Rom{0}), and "Called from"
// looks that up in the source map. An SLD line on Page{0} at the same address
// must not answer: the ROM's index 0 is not RAM page 0.

void test_call_stack_non_page_caller() {
    Fixture fx;
    fx.enable();
    const std::string path = g_tmp->path().toStdString() + "/rom0.sld";
    write_text(path, std::string("|SLD.data.version|1\nmain.bas|10||0") + kNextDevice +
                         "main.bas|10||0|0|0|T|\n");
    fx.backend->load_source_map(path, false);
    fx.backend->set_call_stack_enabled(true);
    Z80Registers r = fx.emu.cpu().get_registers();
    r.PC = 0x0000;   // the bare test ROM is 0xFF: RST $38, fetched from ROM 0
    fx.emu.cpu().set_registers(r);
    fx.backend->step_into(jnext::dbg::CLIENT_NONE);
    CallStackPanel* cs = fx.dbg() ? fx.dbg()->callstack_panel() : nullptr;
    if (cs) { cs->set_paused(true); cs->refresh(); }
    const auto& f = fx.backend->call_stack();
    const bool fixture = f.size() == 1 && f[0].caller_pc == 0x0000 &&
                         f[0].caller_space == jnext::dbg::MemSpace::rom(0) &&
                         fx.backend->source_map().lookup(jnext::dbg::MemSpace::page(0), 0x0000);
    check("QCSP-03", "a caller fetched from ROM 0 shows no Called from line, though the map "
                     "has a line for RAM page 0 at the same address",
          fixture && cell(cs, 0, 4).isEmpty(),
          "frames " + std::to_string(f.size()) + " cell '" + s(cell(cs, 0, 4)) + "'");
}

}  // namespace

// ── GH #337: Debug > Source ───────────────────────────────────────────
//
// Oracle: issue #337 and design doc 8.2 (the menu); section 7 via the PC
// signatures QSRC-05/06/11/17/18 already pin (the verbs); the one handler is
// DebuggerManager::run_source_step().

/// The Debug > Source popup of the debugger window, or nullptr.
QMenu* source_popup(DebuggerWindow* dbg) {
    if (!dbg) return nullptr;
    for (QAction* top : dbg->menuBar()->actions()) {
        QMenu* debug = top->menu();
        if (!debug) continue;
        for (QAction* sub : debug->actions())
            if (sub->menu() && sub->menu()->title().remove(QLatin1Char('&')) == QStringLiteral("Source"))
                return sub->menu();
    }
    return nullptr;
}

QAction* source_item(DebuggerWindow* dbg, const QString& text) {
    if (QMenu* m = source_popup(dbg))
        for (QAction* a : m->actions())
            if (a->text().remove(QLatin1Char('&')) == text) return a;
    return nullptr;
}

void trigger(DebuggerWindow* dbg, const QString& text) {
    if (QAction* a = source_item(dbg, text)) a->trigger();
    QApplication::processEvents();
}

/// Paused, rewind 8, trace on, map loaded, Into x3 (main.bas:12), and an
/// Execute breakpoint at $8002 so Back ($9000) and Reverse ($8002) differ.
void at_line_12(Fixture& fx) {
    fx.enable();
    fx.backend->set_trace_enabled(true);
    fx.load_map();
    fx.mgr->refresh_panels();
    for (int i = 0; i < 3; ++i) trigger(fx.dbg(), QStringLiteral("Step Into"));
    fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x8002, pg(fx.p80));
    fx.mgr->refresh_panels();
}

void settle_ms(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void test_source_menu() {
    const std::string dir = g_tmp->path().toStdString();
    {
        Fixture fx;
        fx.enable();
        QStringList items;
        if (QMenu* m = source_popup(fx.dbg()))
            for (QAction* a : m->actions())
                if (!a->isSeparator()) items << a->text().remove(QLatin1Char('&'));
        const QStringList want{"Step Into", "Step Over", "Step Out", "Step Back",
                               "Reverse Continue", "Load Source Map...",
                               "Load Symbols (Memory.txt)..."};
        check("QSRC-47", "Debug > Source holds, in order: Step Into, Step Over, Step Out, "
                         "Step Back, Reverse Continue, Load Source Map..., "
                         "Load Symbols (Memory.txt)...",
              items == want, s(items.join(QStringLiteral(" | "))));
    }
    {
        Fixture fx;
        fx.enable(); fx.load_map(); fx.mgr->refresh_panels();
        trigger(fx.dbg(), QStringLiteral("Step Into"));
        const uint16_t a = fx.pc();
        trigger(fx.dbg(), QStringLiteral("Step Into"));
        check("QSRC-48", "Debug > Source > Step Into, twice from $8000: $8002 then $9000",
              a == 0x8002 && fx.pc() == 0x9000,
              std::to_string(a) + " -> " + std::to_string(fx.pc()));
    }
    {
        Fixture fx;
        fx.enable(); fx.load_map(); fx.mgr->refresh_panels();
        trigger(fx.dbg(), QStringLiteral("Step Over"));
        const uint16_t a = fx.pc();
        trigger(fx.dbg(), QStringLiteral("Step Over"));
        check("QSRC-49", "Debug > Source > Step Over, twice from $8000: $8002 then $8005",
              a == 0x8002 && fx.pc() == 0x8005,
              std::to_string(a) + " -> " + std::to_string(fx.pc()));
    }
    {
        Fixture fx;
        fx.enable(); fx.load_map(); fx.mgr->refresh_panels();
        trigger(fx.dbg(), QStringLiteral("Step Out"));
        check("QSRC-50", "Debug > Source > Step Out from $8000 stops at $8001",
              fx.pc() == 0x8001, std::to_string(fx.pc()));
    }
    {
        Fixture fx(/*rewind_frames=*/8);
        at_line_12(fx);
        const uint16_t from = fx.pc();
        trigger(fx.dbg(), QStringLiteral("Step Back"));
        check("QSRC-51", "Debug > Source > Step Back from main.bas:12 lands on sub.bas:1 ($9000)",
              from == 0x8005 && fx.pc() == 0x9000,
              std::to_string(from) + " -> " + std::to_string(fx.pc()));
    }
    {
        Fixture fx(/*rewind_frames=*/8);
        at_line_12(fx);
        const uint16_t from = fx.pc();
        trigger(fx.dbg(), QStringLiteral("Reverse Continue"));
        check("QSRC-52", "Debug > Source > Reverse Continue from main.bas:12 lands on the "
                         "breakpoint at $8002",
              from == 0x8005 && fx.pc() == 0x8002,
              std::to_string(from) + " -> " + std::to_string(fx.pc()));
    }
    {
        Fixture fx;
        fx.enable();
        const QString sld = QString::fromStdString(fx.sld);
        drive_modals([&](QWidget* w, int) { choose_file(w, sld); },
                     [&]() { trigger(fx.dbg(), QStringLiteral("Load Source Map...")); });
        check("QSRC-53", "Debug > Source > Load Source Map... loads the chosen SLD and no symbols",
              !fx.backend->source_map().empty() &&
                  fx.backend->source_map().loaded_file() == fx.sld &&
                  !fx.backend->lookup_name("Landing").has_value());
    }
    {
        Fixture fx;
        fx.enable();
        const std::string mem = dir + "/menu.Memory.txt";
        write_text(mem, "8005: ._Landing\n");
        drive_modals([&](QWidget* w, int) { choose_file(w, QString::fromStdString(mem)); },
                     [&]() { trigger(fx.dbg(), QStringLiteral("Load Symbols (Memory.txt)...")); });
        check("QSRC-54", "Debug > Source > Load Symbols (Memory.txt)... loads the chosen "
                         "symbols and no source map",
              fx.backend->lookup_name("Landing") == std::optional<uint16_t>(0x8005) &&
                  fx.backend->source_map().empty());
    }
    {
        // The five step actions against the buttons, over five states. Both the
        // pairwise equality and the expected value, so all-disabled cannot pass.
        const char* const names[5] = {"Step Into", "Step Over", "Step Out", "Step Back",
                                      "Reverse Continue"};
        const char* const btns[5]  = {"Into", "Over", "Out", "Back", "Reverse"};
        std::string detail;
        bool ok = true;
        auto state = [&](Fixture& fx, const char* what, const bool want[5]) {
            for (int i = 0; i < 5; ++i) {
                QAction* a = source_item(fx.dbg(), QString::fromLatin1(names[i]));
                QPushButton* b = button(fx.dbg()->source_panel(), QString::fromLatin1(btns[i]));
                if (!a || !b || a->isEnabled() != b->isEnabled() || a->isEnabled() != want[i]) {
                    ok = false;
                    detail += std::string(what) + "/" + names[i] + " action=" +
                              (a ? std::to_string(a->isEnabled()) : "null") + " button=" +
                              (b ? std::to_string(b->isEnabled()) : "null") + "; ";
                }
            }
        };
        const bool none[5] = {false, false, false, false, false};
        const bool fwd[5]  = {true, true, true, false, false};
        const bool all[5]  = {true, true, true, true, true};
        {   // (a) no map
            Fixture fx; fx.enable(); fx.mgr->refresh_panels();
            state(fx, "a", none);
        }
        {   // (b) a map loaded behind the window, then the manager's paused
        // refresh (panels, then update_actions()); (c) then running
            Fixture fx; fx.enable(); fx.load_map(); fx.mgr->refresh_panels();
            state(fx, "b", fwd);
            fx.dbg()->update_actions(/*is_paused=*/false);
            state(fx, "c", none);
        }
        {   // (d) rewind + trace + map, two steps in
            Fixture fx(/*rewind_frames=*/8);
            fx.enable(); fx.backend->set_trace_enabled(true); fx.load_map();
            fx.mgr->refresh_panels();
            for (int i = 0; i < 2; ++i) trigger(fx.dbg(), QStringLiteral("Step Into"));
            fx.mgr->refresh_panels();
            state(fx, "d", all);
        }
        {   // (e) rewind, no trace
            Fixture fx(/*rewind_frames=*/8);
            fx.enable(); fx.load_map(); fx.mgr->refresh_panels();
            state(fx, "e", fwd);
        }
        check("QSRC-55", "the five step actions are enabled exactly when their buttons are",
              ok, detail);
    }
    {
        // QSRC-22's corrupt machine, through the menu: the gate is in the manager.
        Fixture fx;
        fx.enable(); fx.load_map(); fx.mgr->refresh_panels();
        const uint8_t garbage[4] = {0, 0, 0, 0};
        StateReader r(garbage, sizeof(garbage));
        fx.emu.load_state(r);
        fx.backend->load_source_map(fx.sld, false);
        fx.mgr->refresh_panels();
        bool asked = false;
        drive_modals(
            [&](QWidget* w, int) {
                if (auto* mb = qobject_cast<QMessageBox*>(w))
                    asked = mb->windowTitle() == QStringLiteral("Machine State Corrupt");
                click_box(w, QMessageBox::No);
            },
            [&]() { trigger(fx.dbg(), QStringLiteral("Step Into")); });
        check("QSRC-27", "the menu runs the button's handler: on a corrupt machine Debug > "
                         "Source > Step Into asks first, and No does not step",
              !fx.emu.last_state_error().empty() && asked && fx.pc() == 0x8000,
              "asked=" + std::to_string(asked) + " pc=" + std::to_string(fx.pc()));
    }
    {
        Fixture fx(/*rewind_frames=*/8);
        fx.enable();
        fx.backend->set_trace_enabled(true);
        fx.load_map();
        fx.mgr->refresh_panels();
        for (int i = 0; i < 2; ++i) trigger(fx.dbg(), QStringLiteral("Step Into"));
        fx.mgr->refresh_panels();
        QAction* rev = source_item(fx.dbg(), QStringLiteral("Reverse Continue"));
        const bool enabled = rev && rev->isEnabled();
        const uint16_t from = fx.pc();
        trigger(fx.dbg(), QStringLiteral("Reverse Continue"));
        const QString bar = fx.dbg()->statusBar()->currentMessage();
        check("QSRC-56", "a refused Reverse Continue from the menu leaves the machine and names "
                         "the reason in the debugger window's status bar",
              enabled && from != 0x8000 && fx.pc() == from &&
                  bar.contains(QStringLiteral("no earlier source breakpoint")),
              s(bar) + " pc " + std::to_string(from) + " -> " + std::to_string(fx.pc()));
    }
    {
        // The default keys, through the real platform key path.
        struct Press { Qt::Key key; int times; uint16_t want; };
        const Press presses[3] = {{Qt::Key_F6, 2, 0x9000}, {Qt::Key_F7, 2, 0x8005},
                                  {Qt::Key_F8, 1, 0x8001}};
        std::string detail;
        bool ok = true;
        for (const Press& p : presses) {
            Fixture fx;
            fx.enable(); fx.load_map(); fx.mgr->refresh_panels();
            DebuggerWindow* w = fx.dbg();
            w->show();
            w->activateWindow();
            settle_ms(150);
            if (!w->windowHandle()) { ok = false; detail += "no window handle; "; continue; }
            for (int i = 0; i < p.times; ++i) {
                QTest::keyClick(w->windowHandle(), p.key, Qt::ControlModifier);
                settle_ms(80);
            }
            if (fx.pc() != p.want) {
                ok = false;
                detail += "Ctrl+F" + std::to_string(p.key - Qt::Key_F1 + 1) + " -> " +
                          std::to_string(fx.pc()) + "; ";
            }
        }
        check("QSRC-57", "the default keys pressed in the debugger window run the source steps: "
                         "Ctrl+F6 twice gives $9000, Ctrl+F7 twice gives $8005, Ctrl+F8 gives $8001",
              ok, detail);
    }
}

// ── SLD labels through the real load paths (GH #375 stage 2) ───────────
// equlabels.sld is sjasmplus's own output: `banked` is $C000 on page 20, `SCREEN` unpaged
// at $4000; 18 source traces and 12 labels.

QAction* find_action(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions()) {
        if (a->text() == text) return a;
        if (a->menu())
            if (QAction* sub = find_action(a->menu(), text)) return sub;
    }
    return nullptr;
}

QAction* menu_action(DebuggerWindow* w, const QString& text) {
    for (QAction* a : w->menuBar()->actions())
        if (a->menu())
            if (QAction* found = find_action(a->menu(), text)) return found;
    return nullptr;
}

// QSYM-06: Source > Load Source Map... fills the labels, and the Breakpoints panel names a row by them.
void test_sld_labels_menu() {
    Fixture fx;
    fx.enable();
    BreakpointModel& m = fx.mgr->breakpoints();
    BreakpointPanel* bp = fx.dbg() ? fx.dbg()->breakpoint_panel() : nullptr;
    QAction* load = fx.dbg() ? menu_action(fx.dbg(), QStringLiteral("Load Source &Map...")) : nullptr;
    if (!bp || !load) {
        check("QSYM-06", "panel and menu item", false);
        return;
    }
    m.add(BreakpointModel::Execute, 0xC000, MemSpace::page(20));
    bp->refresh();
    const bool premise = cell(bp, 0, 3).isEmpty();
    const QString sld = QStringLiteral(JNEXT_SJASMEQU_DIR "/equlabels.sld");
    QString box;
    drive_modals(
        [&](QWidget* w, int) {
            if (auto* mb = qobject_cast<QMessageBox*>(w)) box = mb->text();
            choose_file(w, sld);
        },
        [&]() { load->trigger(); });
    const QString named = cell(bp, 0, 3);
    const bool box_named = named == QStringLiteral("banked");
    // An SLD with no labels reports the old text, with no mention of labels.
    const QString plain = QString::fromStdString(fx.sld);
    QString plain_box;
    drive_modals(
        [&](QWidget* w, int) {
            if (auto* mb = qobject_cast<QMessageBox*>(w)) plain_box = mb->text();
            choose_file(w, plain);
        },
        [&]() { load->trigger(); });
    check("QSYM-06", "Load Source Map... with an sjasmplus SLD reports its traces and labels, and the "
                     "Breakpoints panel names a page-20 row by the label at once; one with no labels "
                     "reports its traces only",
          premise && box.contains(QStringLiteral("18 source traces and 12 labels")) &&
              box_named && plain_box.startsWith(QStringLiteral("Loaded 5 source traces from:")) &&
              !plain_box.contains(QStringLiteral("label")),
          "premise=" + std::to_string(premise) + " box='" + s(box) + "' cell=" + s(named) +
              " plain='" + s(plain_box) + "'");
    m.clear_all();
}

// QSYM-13 (closes R08): Add Execute Breakpoint with a name from a loaded SLD keeps that name's page.
void test_sld_labels_add_execute() {
    Fixture fx;
    fx.enable();
    BreakpointModel& m = fx.mgr->breakpoints();
    const auto loaded = fx.backend->load_source_map(JNEXT_SJASMEQU_DIR "/equlabels.sld", false);
    QAction* add_exec = fx.dbg() ? menu_action(fx.dbg(), QStringLiteral("Add &Execute Breakpoint...")) : nullptr;
    if (!add_exec) {
        check("QSYM-13", "menu item", false);
        return;
    }
    const bool seen = answer_modal(QStringLiteral("banked"), [&]() { add_exec->trigger(); });
    const auto paged = m.rows();
    const bool ok_paged = seen && paged.size() == 1 && paged[0].addr == 0xC000 &&
                          paged[0].space == MemSpace::page(20) && paged[0].group == jnext::dbg::GROUP_NONE;
    m.clear_all();
    const bool seen2 = answer_modal(QStringLiteral("SCREEN"), [&]() { add_exec->trigger(); });
    const auto plain = m.rows();
    const bool ok_plain = seen2 && plain.size() == 1 && plain[0].addr == 0x4000 &&
                          plain[0].space == MemSpace::cpu();
    check("QSYM-13", "Add Execute Breakpoint with a label of a loaded SLD sets the row on that label's page; "
                     "an unpaged label gets no page",
          loaded.labels == 12 && ok_paged && ok_plain,
          "labels=" + std::to_string(loaded.labels) + " paged=" + std::to_string(ok_paged) +
              " plain=" + std::to_string(ok_plain));
    m.clear_all();
}

// ── QSRC-62..64: a z88dk -debug map through Load Source Map... (GH #344) ──────────

// Copy `from`'s file `name` (a committed z88dk fixture) to `to`, whose directory is made.
void copy_z88dk(const char* fixture, const char* name, const std::string& to) {
    std::filesystem::create_directories(std::filesystem::path(to).parent_path());
    std::filesystem::copy_file(std::filesystem::path(JNEXT_Z88DK_FIXTURE_DIR) / fixture / name, to,
                               std::filesystem::copy_options::overwrite_existing);
}

void test_z88dk_source_map() {
    const std::string tmp = g_tmp->path().toStdString();
    {   // QSRC-62: the dialog offers .map, and a z88dk map loads as source lines only
        Fixture fx;
        fx.enable();
        QAction* load = menu_action(fx.dbg(), QStringLiteral("Load Source &Map..."));
        const std::string map = tmp + "/q62/prog.map";
        copy_z88dk("v2.4/sccz80-debug", "prog.map", map);
        QStringList filters;
        QString box;
        if (load)
            drive_modals(
                [&](QWidget* w, int) {
                    if (auto* fd = qobject_cast<QFileDialog*>(w)) filters = fd->nameFilters();
                    if (auto* mb = qobject_cast<QMessageBox*>(w)) box = mb->text();
                    choose_file(w, QString::fromStdString(map));
                },
                [&]() { load->trigger(); });
        bool offers_map = false;
        for (const QString& f : filters)
            if (f.contains(QStringLiteral("*.map"))) offers_map = true;
        check("QSRC-62", "Load Source Map... offers *.map, and a z88dk map loads as source lines "
                         "only: 46 of them, no symbols",
              load && offers_map && fx.backend->source_map().format() == "z88dk" &&
                  fx.backend->source_map().size() == 46 && !fx.backend->lookup_name("_main") &&
                  box.startsWith(QStringLiteral("Loaded 46 source traces from:")),
              "filters=" + s(filters.join("|")) + " box=" + s(box));
    }
    {   // QSRC-63: a source file is found under the map's directory
        Fixture fx;
        fx.enable();
        const std::string map = tmp + "/q63/prog.map";
        copy_z88dk("v2.4/sccz80-subdir", "prog.map", map);
        std::string util;
        for (int i = 1; i <= 9; ++i) util += "// line " + std::to_string(i) + "\n";
        std::filesystem::create_directories(tmp + "/q63/game");
        write_text(tmp + "/q63/game/util.c", util);
        fx.backend->load_source_map(map, true);
        Z80Registers r = fx.emu.cpu().get_registers();
        r.PC = 0x8D5C;
        fx.emu.cpu().set_registers(r);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg() ? fx.dbg()->source_panel() : nullptr;
        check("QSRC-63", "the Source tab shows game/util.c, found under the map's directory, with line 8 "
                         "highlighted at $8D5C",
              sp && sp->highlighted_line() == 8 && sp->location_text().startsWith("game/util.c:8"),
              sp ? s(sp->location_text()) + " line=" + std::to_string(sp->highlighted_line()) : "");
    }
    {   // QSRC-64: a mismatching z88dk map is offered in words that do not say SLD
        Fixture fx;
        fx.enable();
        QAction* load = menu_action(fx.dbg(), QStringLiteral("Load Source &Map..."));
        const std::string map = tmp + "/q64/prog.map";
        copy_z88dk("v2.4/sccz80-debug", "prog.map", map);
        copy_z88dk("v2.4/sccz80-debug", "prog_CODE.bin", tmp + "/q64/prog_CODE.bin");
        QString offer;
        auto run = [&](QMessageBox::StandardButton answer) {
            drive_modals(
                [&](QWidget* w, int) {
                    if (auto* mb = qobject_cast<QMessageBox*>(w))
                        if (mb->windowTitle() == QStringLiteral("Source Map Identity Mismatch")) {
                            offer = mb->text();
                            click_box(w, answer);
                            return;
                        }
                    choose_file(w, QString::fromStdString(map));
                },
                [&]() { load->trigger(); });
        };
        run(QMessageBox::No);
        const bool declined = !offer.isEmpty() && fx.backend->source_map().empty();
        run(QMessageBox::Yes);
        check("QSRC-64", "a z88dk map whose binaries are not in memory is offered without naming SLD; No "
                         "loads nothing, Yes loads it",
              load && declined && !offer.contains(QStringLiteral("SLD")) &&
                  fx.backend->source_map().format() == "z88dk" &&
                  fx.backend->source_map().size() == 46,
              s(offer));
    }
    {   // QSRC-65: a tape program's map is pending until the program first runs its code
        Fixture fx;
        fx.enable();
        const std::string map = tmp + "/q65/prog.map";
        copy_z88dk("v2.4/sccz80-tape", "prog.map", map);
        copy_z88dk("v2.4/sccz80-tape", "prog_CODE.bin", tmp + "/q65/prog_CODE.bin");
        const auto out = fx.backend->load_source_map(map, false, /*verify_at_first_execution=*/true);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg() ? fx.dbg()->source_panel() : nullptr;
        const QString note = QStringLiteral("Source map not verified yet: it is checked when the program "
                                            "first runs its code");
        const bool shown = sp && sp->location_text().contains(note);
        std::ifstream in(tmp + "/q65/prog_CODE.bin", std::ios::binary);
        const std::string code((std::istreambuf_iterator<char>(in)), {});
        for (size_t i = 0; i < code.size(); ++i)
            fx.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), static_cast<uint8_t>(code[i]));
        Z80Registers r = fx.emu.cpu().get_registers();
        r.PC = 0x8000;
        fx.emu.cpu().set_registers(r);
        fx.emu.debugger_step();   // the program's first instruction: the map is judged
        fx.mgr->refresh_panels();
        check("QSRC-65", "a map attached for a tape program shows 'not verified yet' in the Source tab "
                         "until the program first runs its code, and not after",
              out.count == 46 && out.pending && shown && sp && !sp->location_text().contains(note) &&
                  fx.backend->source_map().size() > 0,
              sp ? s(sp->location_text()) : "");
    }
}

// ── QDOT-01/02: a z88dk dot command's map in the Source tab (GH #351) ────────────
// Oracle: the committed v2.4 sccz80 dot fixture (dot.c:14 $2D62, dot.c:15 $2D6A, the CODE group
// placed in DivMMC RAM bank 2 as NextZXOS does) and the user-visible wording of the note.
void test_dot_command() {
    const std::string tmp = g_tmp->path().toStdString();
    const std::string map = tmp + "/qdot/prog.map";
    copy_z88dk("v2.4/sccz80-dot", "prog.map", map);
    copy_z88dk("v2.4/sccz80-dot", "prog_CODE.bin", tmp + "/qdot/prog_CODE.bin");
    std::filesystem::copy_file(std::filesystem::path(JNEXT_Z88DK_FIXTURE_DIR) / "src" / "dot.c",
                               tmp + "/qdot/dot.c", std::filesystem::copy_options::overwrite_existing);
    const std::string code = read_text(tmp + "/qdot/prog_CODE.bin");
    Fixture fx(0, MachineType::ZXN_ISSUE2);
    fx.enable();
    uint8_t* bank2 = fx.emu.divmmc().ram_data() + 2 * 0x2000;
    for (size_t i = 0; i < code.size(); ++i) bank2[i] = static_cast<uint8_t>(code[i]);
    fx.emu.port().write(0xE3, 0x82);   // conmem + bank 2, as NextZXOS runs a dot command
    Z80Registers r = fx.emu.cpu().get_registers();
    r.PC = 0x6000;
    r.SP = 0xFF00;
    fx.emu.cpu().set_registers(r);
    const auto out = fx.backend->load_source_map(map, false);   // loaded outside the command: pending
    r.PC = 0x2D62;   // dot.c:14
    fx.emu.cpu().set_registers(r);
    fx.mgr->refresh_panels();
    SourcePanel* sp = fx.dbg() ? fx.dbg()->source_panel() : nullptr;
    const QString note = QStringLiteral("Source map not verified yet: it is checked when the dot command runs");
    const bool pending_note = sp && sp->location_text().contains(note) &&
                              sp->highlighted_line() == 14 && sp->location_text().contains(QStringLiteral("dot.c"));
    trigger(fx.dbg(), QStringLiteral("Step Into"));
    check("QDOT-01", "a dot command's map, PC at dot.c:14 in DivMMC bank 2: Debug > Source > Step Into shows dot.c with line 15 "
                     "current, the PC at $2D6A",
          out.count > 0 && sp && fx.pc() == 0x2D6A && sp->highlighted_line() == 15 &&
              sp->location_text().contains(QStringLiteral("dot.c")) && sp->location_text().contains(QStringLiteral("2D6A")),
          sp ? s(sp->location_text()) : "");
    // QDOT-02: the note names the dot command, until the command starts at $2000.
    const bool still = sp && sp->location_text().contains(note);
    fx.emu.cpu().set_registers([&] { Z80Registers q = fx.emu.cpu().get_registers(); q.PC = 0x2000; return q; }());
    fx.emu.debugger_step();
    fx.mgr->refresh_panels();
    check("QDOT-02", "a dot command's pending map says 'checked when the dot command runs' (not 'the program'), and the note is gone once $2000 "
                     "has run from DivMMC bank 2 with matching bytes",
          pending_note && still && out.pending && sp && !sp->location_text().contains(note) &&
              !sp->location_text().contains(QStringLiteral("not verified yet")),
          sp ? s(sp->location_text()) : "");
}

// ── QSRC-66..74: the source search path and Locate File... (GH #420) ─────────
// Oracle: the issue (the committed z88dk fixture map in a build/ directory with the
// sources in a sibling src/ is found through a search path, and Locate File... adds the
// directory), the z88dk fixtures' recorded names, and the design doc (SOURCE-LEVEL-
// DEBUGGING.md 2.3 G4: PC $8CB5 is main.c:19).
/// write_text() for a path whose directories may not exist yet.
void put_file(const std::string& path, const std::string& text) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    write_text(path, text);
}

std::string numbered_lines(int n, const char* prefix = "/* line ") {
    std::string out;
    for (int i = 1; i <= n; ++i) out += prefix + std::to_string(i) + " */\n";
    return out;
}

/// The z88dk fixture map copied to <root>/build/prog.map and loaded, with the PC on main.c:19.
std::string z88dk_in_build(Fixture& fx, const std::string& root, uint16_t pc = 0x8CB5) {
    const std::string map = root + "/build/prog.map";
    copy_z88dk("v2.4/sccz80-debug", "prog.map", map);
    fx.backend->load_source_map(map, true);
    Z80Registers r = fx.emu.cpu().get_registers();
    r.PC = pc;
    fx.emu.cpu().set_registers(r);
    return map;
}

/// Click Locate File... and answer its dialogs: pick `picked`, then `answer` the question.
/// Returns what the warning box said, when there was one.
QString locate(SourcePanel* sp, const std::string& picked, QMessageBox::StandardButton answer,
               int* modals = nullptr) {
    QString warning;
    const int n = drive_modals(
        [&](QWidget* w, int) {
            if (auto* mb = qobject_cast<QMessageBox*>(w)) {
                if (mb->icon() == QMessageBox::Warning) { warning = mb->text(); click_box(w, QMessageBox::Ok); }
                else click_box(w, answer);
                return;
            }
            choose_file(w, QString::fromStdString(picked));
        },
        [&]() { button(sp, QStringLiteral("Locate File..."))->click(); });
    if (modals) *modals = n;
    return warning;
}

// ── QSRC-140/141 (GH #348): a z88dk map's --list listings in the Source tab and the Load box ──

void test_z88dk_listings() {
    const std::string tmp = g_tmp->path().toStdString();
    {   // QSRC-140
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q140";
        const std::string map = root + "/build/prog.map";
        copy_z88dk("master/sdcc-debug", "prog.map", map);
        copy_z88dk("master/sdcc-debug", "prog.lis", root + "/build/prog.lis");
        put_file(root + "/build/z88dk/lib/crt/newlib/crt_start_di.inc", numbered_lines(10, "; di "));
        fx.backend->load_source_map(map, true);
        auto at = [&](uint16_t pc) {
            Z80Registers r = fx.emu.cpu().get_registers();
            r.PC = pc;
            fx.emu.cpu().set_registers(r);
            fx.mgr->refresh_panels();
        };
        at(0x8001);
        SourcePanel* sp = fx.dbg()->source_panel();
        auto* editor = sp->findChild<QPlainTextEdit*>();
        const bool text_ok = editor &&
            editor->document()->findBlockByNumber(9469).text() == QStringLiteral("      push iy") &&
            editor->document()->findBlockByNumber(9461).text() == QStringLiteral("__Start:") &&
            sp->location_tooltip().endsWith(QStringLiteral("/q140/build/prog.lis"));
        const bool m4_ok = text_ok && editor && editor->document()->blockCount() == 9689 && sp->highlighted_line() == 9470 &&
                           sp->location_text().endsWith(QStringLiteral("(m4 output, from prog.lis)")) &&
                           !button(sp, QStringLiteral("Locate File..."))->isEnabled();
        const QString m4_text = sp->location_text();
        at(0x8000);
        const bool disk_ok = editor && editor->document()->blockCount() == 11 && sp->highlighted_line() == 3 &&
                             !sp->location_text().contains(QStringLiteral("m4 output"));
        check("QSRC-140", "with prog.lis loaded beside the map, $8001 shows the CRT's m4 text from the listing "
                          "(9689 lines, line 9470 highlighted, labelled '(m4 output, from prog.lis)', no Locate "
                          "File...), and $8000 goes back to the disk file crt_start_di.inc with line 3 highlighted",
              m4_ok && disk_ok, s(m4_text) + " | " + s(sp->location_text()));
    }
    {   // QSRC-141
        Fixture fx;
        fx.enable();
        QAction* load = menu_action(fx.dbg(), QStringLiteral("Load Source &Map..."));
        const std::string map = tmp + "/q141/prog.map";
        copy_z88dk("v2.4/sdcc-SO2", "prog.map", map);
        for (const char* lis : {"main.c.lis", "util.c.lis", "asmfn.asm.lis", "prog.lis"})
            copy_z88dk("v2.4/sdcc-SO2", lis, tmp + "/q141/" + lis);
        QString box;
        if (load)
            drive_modals(
                [&](QWidget* w, int) {
                    if (auto* mb = qobject_cast<QMessageBox*>(w)) box = mb->text();
                    choose_file(w, QString::fromStdString(map));
                },
                [&]() { load->trigger(); });
        check("QSRC-141", "Load Source Map... of a z88dk map with its listings reports 230 traces and 'with 4 z88dk listings'",
              load && box.startsWith(QStringLiteral("Loaded 230 source traces from:")) &&
                  box.contains(QStringLiteral("with 4 z88dk listings")),
              s(box));
    }
    {   // QSRC-142: dropped listings in the Load box
        Fixture fx;
        fx.enable();
        QAction* load = menu_action(fx.dbg(), QStringLiteral("Load Source &Map..."));
        const std::string map = tmp + "/q142/prog.map";
        copy_z88dk("v2.4/sdcc-SO2", "prog.map", map);
        copy_z88dk("v2.4/sdcc-SO2", "prog_CODE.bin", tmp + "/q142/prog_CODE.bin");
        for (const char* lis : {"main.c.lis", "util.c.lis", "asmfn.asm.lis", "prog.lis"})
            copy_z88dk("v2.4/sdcc-SO2", lis, tmp + "/q142/" + lis);
        {
            std::string text = read_text(tmp + "/q142/main.c.lis");
            const size_t at = text.find("0023  af");
            if (at != std::string::npos) text.replace(at, 8, "0023  b7");
            write_text(tmp + "/q142/main.c.lis", text);
        }
        QString box;
        if (load)
            drive_modals(
                [&](QWidget* w, int) {
                    if (auto* mb = qobject_cast<QMessageBox*>(w)) {
                        // The test machine does not hold the program: accept the mismatch question.
                        if (mb->text().contains(QStringLiteral("Load it anyway"))) { click_box(w, QMessageBox::Yes); return; }
                        box = mb->text();
                    }
                    choose_file(w, QString::fromStdString(map));
                },
                [&]() { load->trigger(); });
        check("QSRC-142", "Load Source Map... with one stale listing reports the 3 that were used and the note naming main.c.lis",
              load && box.contains(QStringLiteral("with 3 z88dk listings")) &&
                  box.contains(QStringLiteral("main.c.lis")) && box.contains(QStringLiteral("$8C6B")),
              s(box));
    }
    {   // QSRC-144: one listing is singular in the Load box
        Fixture fx;
        fx.enable();
        QAction* load = menu_action(fx.dbg(), QStringLiteral("Load Source &Map..."));
        const std::string map = tmp + "/q144/prog.map";
        copy_z88dk("v2.4/sccz80-incmid", "prog.map", map);
        copy_z88dk("v2.4/sccz80-incmid", "incmid.c.lis", tmp + "/q144/incmid.c.lis");
        QString box;
        if (load)
            drive_modals(
                [&](QWidget* w, int) {
                    if (auto* mb = qobject_cast<QMessageBox*>(w)) box = mb->text();
                    choose_file(w, QString::fromStdString(map));
                },
                [&]() { load->trigger(); });
        check("QSRC-144", "Load Source Map... of a map with exactly one listing says 'with 1 z88dk listing', not 'listings'",
              load && box.contains(QStringLiteral("with 1 z88dk listing")) &&
                  !box.contains(QStringLiteral("z88dk listings")),
              s(box));
    }
    {   // QSRC-143: a reloaded listing of the same name refreshes the shown text
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q143";
        const std::string map = root + "/build/prog.map";
        copy_z88dk("master/sdcc-debug", "prog.map", map);
        copy_z88dk("master/sdcc-debug", "prog.lis", root + "/build/prog.lis");
        fx.backend->load_source_map(map, true);
        Z80Registers r = fx.emu.cpu().get_registers();
        r.PC = 0x8001;
        fx.emu.cpu().set_registers(r);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        auto* editor = sp->findChild<QPlainTextEdit*>();
        const bool before = editor && editor->document()->findBlockByNumber(9469).text() == QStringLiteral("      push iy");
        std::string lis = read_text(root + "/build/prog.lis");
        const size_t at = lis.find("      push iy");
        if (at != std::string::npos) lis.replace(at, 13, "      push ix");
        write_text(root + "/build/prog.lis", lis);
        fx.backend->load_source_map(map, true);
        fx.mgr->refresh_panels();
        check("QSRC-143", "loading the map again after its prog.lis changed shows the new m4 text, not the old",
              before && editor && at != std::string::npos &&
                  editor->document()->findBlockByNumber(9469).text() == QStringLiteral("      push ix"));
    }
}

void test_source_search() {
    const std::string tmp = g_tmp->path().toStdString();
    {   // QSRC-66
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q66";
        put_file(root + "/src/main.c", numbered_lines(25));
        z88dk_in_build(fx, root);
        fx.mgr->set_source_dirs({QString::fromStdString(root + "/src")});
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        check("QSRC-66", "the z88dk fixture map in build/ and its sources in a sibling src/: with src/ on the "
                         "search path the Source tab shows main.c with line 19 highlighted, and the label's "
                         "tooltip names the file found",
              sp->highlighted_line() == 19 && sp->location_text().startsWith(QStringLiteral("main.c:19")) &&
                  s(sp->location_tooltip()) == root + "/src/main.c",
              s(sp->location_text()) + " | " + s(sp->location_tooltip()));
    }
    {   // QSRC-67
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q67";
        put_file(root + "/src/main.c", numbered_lines(25));
        z88dk_in_build(fx, root);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        QPushButton* loc = button(sp, QStringLiteral("Locate File..."));
        const bool off_before = loc && !loc->isEnabled();   // nothing shown yet: disabled
        fx.mgr->refresh_panels();
        const bool missing = sp->location_text() == QStringLiteral("main.c:19 - source file not found");
        const bool on_when_missing = loc && loc->isEnabled();
        fx.mgr->set_source_dirs({QString::fromStdString(root + "/src")});
        fx.mgr->refresh_panels();
        check("QSRC-67", "with the file missing the tab says 'main.c:19 - source file not found' and Locate "
                         "File... is enabled; once a search directory finds it, the button is disabled",
              loc && missing && on_when_missing && !loc->isEnabled() && sp->highlighted_line() == 19,
              std::to_string(off_before) + " " + s(sp->location_text()));
    }
    {   // QSRC-68
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q68";
        put_file(root + "/src/main.c", numbered_lines(25));
        z88dk_in_build(fx, root);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        QSignalSpy saved(fx.mgr, &DebuggerManager::source_dir_save_requested);
        int modals = 0;
        locate(sp, root + "/src/main.c", QMessageBox::No, &modals);
        check("QSRC-68", "Locate File... with a pick and No: the file shows at once (line 19), the manager "
                         "searches the picked file's directory, and nothing is asked to be saved",
              modals == 2 && sp->highlighted_line() == 19 &&
                  fx.mgr->source_dirs() == QStringList{QString::fromStdString(root + "/src")} &&
                  saved.count() == 0,
              std::to_string(modals) + " " + s(sp->location_text()));
    }
    {   // QSRC-69
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q69";
        put_file(root + "/src/main.c", numbered_lines(25));
        z88dk_in_build(fx, root);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        QSignalSpy saved(fx.mgr, &DebuggerManager::source_dir_save_requested);
        QString question;
        drive_modals(
            [&](QWidget* w, int) {
                if (auto* mb = qobject_cast<QMessageBox*>(w)) { question = mb->text(); click_box(w, QMessageBox::Yes); return; }
                choose_file(w, QString::fromStdString(root + "/src/main.c"));
            },
            [&]() { button(sp, QStringLiteral("Locate File..."))->click(); });
        check("QSRC-69", "Locate File... answered Yes asks once for the directory to be saved, and the question "
                         "names it",
              saved.count() == 1 && saved.value(0).value(0).toString() == QString::fromStdString(root + "/src") &&
                  question.contains(QString::fromStdString(root + "/src")) &&
                  question.contains(QStringLiteral("Paths > Source search path")),
              s(question));
    }
    {   // QSRC-70
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q70";
        put_file(root + "/src/main.c", numbered_lines(25));
        put_file(root + "/src/other.c", numbered_lines(25));
        z88dk_in_build(fx, root);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        QSignalSpy located(sp, &SourcePanel::source_dir_located);
        int modals = 0;
        const QString warning = locate(sp, root + "/src/other.c", QMessageBox::Yes, &modals);
        check("QSRC-70", "Locate File... with a file of another name is refused in a box ('other.c is not "
                         "main.c'), and nothing is added",
              modals == 2 && warning.startsWith(QStringLiteral("other.c is not main.c")) &&
                  located.count() == 0 && fx.mgr->source_dirs().isEmpty() &&
                  sp->location_text() == QStringLiteral("main.c:19 - source file not found"),
              s(warning));
    }
    {   // QSRC-71
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q71";
        const std::string inst = root + "/inst";
        put_file(inst + "/libsrc/_DEVELOPMENT/target/zxn/zxn_crt.asm.m4", numbered_lines(1051, "; crt "));
        z88dk_in_build(fx, root, 0x8000);
        fx.mgr->set_source_dirs({QString::fromStdString(inst)});
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        const SourceLocation* loc = fx.backend->source_location(0x8000);
        const QString want = loc ? QString::fromStdString(loc->file) + ":" + QString::number(loc->line) +
                                       QStringLiteral(" - the line counts the m4 output of this file, which "
                                                      "the build did not keep; use the disassembly")
                                 : QString();
        const bool z88_ok = loc && loc->line == 9462 && sp->location_text() == want &&
                            sp->highlighted_line() == 0 &&
                            !button(sp, QStringLiteral("Locate File..."))->isEnabled();
        // The rule is the z88dk format's: an SLD naming x.m4 shows the file.
        Fixture fy;
        fy.enable();
        put_file(root + "/sld/x.m4", numbered_lines(12));
        put_file(root + "/sld/prog.sld",
                   std::string("|SLD.data.version|1\nx.m4|10||0") + kNextDevice + "x.m4|10||0|" +
                       std::to_string(fy.p80) + "|32768|T|\n");
        fy.backend->load_source_map(root + "/sld/prog.sld", false);
        fy.mgr->refresh_panels();
        SourcePanel* sy = fy.dbg()->source_panel();
        check("QSRC-71", "a z88dk record in a .m4 file (the CRT's line 9462 at $8000) says the line counts the "
                         "m4 output the build did not keep, with no highlight and no Locate; an SLD record in "
                         "x.m4 is shown and highlighted",
              z88_ok && sy->highlighted_line() == 10,
              s(sp->location_text()) + " | " + s(sy->location_text()));
    }
    {   // QSRC-72
        const std::string root = tmp + "/q72";
        put_file(root + "/nowhere.bas", numbered_lines(9, "REM "));
        auto sld_at = [&](Fixture& f, int line) {
            put_file(root + "/prog.sld",
                       std::string("|SLD.data.version|1\nnowhere.bas|") + std::to_string(line) + "||0" +
                           kNextDevice + "nowhere.bas|" + std::to_string(line) + "||0|" +
                           std::to_string(f.p80) + "|32768|T|\n");
            f.backend->load_source_map(root + "/prog.sld", false);
            f.mgr->refresh_panels();
        };
        Fixture fx;
        fx.enable();
        sld_at(fx, 10);
        SourcePanel* sp = fx.dbg()->source_panel();
        const QString past = sp->location_text();
        const int past_line = sp->highlighted_line();
        Fixture fy;
        fy.enable();
        sld_at(fy, 9);
        SourcePanel* sy = fy.dbg()->source_panel();
        check("QSRC-72", "a record past the end of the file found says the file has only 9 lines and is not "
                         "the text the build used, with no highlight; the last line (9) is highlighted",
              past == QString::fromStdString("nowhere.bas:10 - " + root + "/nowhere.bas has only 9 lines; "
                                             "it is not the text the build used") &&
                  past_line == 0 && sy->highlighted_line() == 9,
              s(past) + " | " + s(sy->location_text()));
    }
    {   // QSRC-73
        Fixture fx;
        const std::string root = tmp + "/q73";
        put_file(root + "/src/main.c", numbered_lines(25));
        fx.mgr->set_source_dirs({QString::fromStdString(root + "/src")});   // no window yet
        const bool none_yet = fx.dbg() == nullptr;
        fx.enable();
        z88dk_in_build(fx, root);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        check("QSRC-73", "a search path set before the debugger window exists reaches the Source tab when "
                         "the window is built",
              none_yet && sp->highlighted_line() == 19 &&
                  sp->search_dirs() == std::vector<std::string>{root + "/src"},
              s(sp->location_text()));
    }
    {   // QSRC-74
        Fixture fx;
        fx.enable();
        fx.mgr->on_source_dir_located(QStringLiteral("/b"));
        fx.mgr->on_source_dir_located(QStringLiteral("/b"));   // twice: kept once
        fx.mgr->set_source_dirs({QStringLiteral("/a")});
        const QStringList both = fx.mgr->source_dirs();
        fx.mgr->set_source_dirs({QStringLiteral("/b"), QStringLiteral("/c")});   // /b is configured now
        SourcePanel* sp = fx.dbg()->source_panel();
        check("QSRC-74", "a directory Locate File... added survives a change of the configured list: "
                         "configured then located, an exact duplicate dropped",
              both == QStringList({QStringLiteral("/a"), QStringLiteral("/b")}) &&
                  fx.mgr->source_dirs() == QStringList({QStringLiteral("/b"), QStringLiteral("/c")}) &&
                  sp->search_dirs() == std::vector<std::string>({"/b", "/c"}),
              s(fx.mgr->source_dirs().join("|")));
    }
    {   // QSRC-75
        // The same past-the-end rule for a file WITHOUT a final newline: its last line is still
        // line 9, so line 9 is highlighted and line 10 says "has only 9 lines".
        const std::string root = tmp + "/q75";
        std::string text = numbered_lines(9, "REM ");
        while (!text.empty() && text.back() == '\n') text.pop_back();
        put_file(root + "/nowhere.bas", text);
        auto sld_at = [&](Fixture& f, int line) {
            put_file(root + "/prog.sld",
                       std::string("|SLD.data.version|1\nnowhere.bas|") + std::to_string(line) + "||0" +
                           kNextDevice + "nowhere.bas|" + std::to_string(line) + "||0|" +
                           std::to_string(f.p80) + "|32768|T|\n");
            f.backend->load_source_map(root + "/prog.sld", false);
            f.mgr->refresh_panels();
        };
        Fixture fx;
        fx.enable();
        sld_at(fx, 10);
        const QString past = fx.dbg()->source_panel()->location_text();
        Fixture fy;
        fy.enable();
        sld_at(fy, 9);
        check("QSRC-75", "a file with no final newline still has its last line counted: line 9 of 9 is "
                         "highlighted and line 10 says the file has only 9 lines",
              past == QString::fromStdString("nowhere.bas:10 - " + root + "/nowhere.bas has only 9 lines; "
                                             "it is not the text the build used") &&
                  fx.dbg()->source_panel()->highlighted_line() == 0 && fy.dbg()->source_panel()->highlighted_line() == 9,
              s(past) + " | " + std::to_string(fy.dbg()->source_panel()->highlighted_line()));
    }
    {   // QSRC-76
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q76";
        put_file(root + "/src/main.c", numbered_lines(25));
        z88dk_in_build(fx, root);
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        bool asked = false, no_default = false;
        drive_modals(
            [&](QWidget* w, int) {
                if (auto* mb = qobject_cast<QMessageBox*>(w)) {
                    asked = true;
                    no_default = mb->defaultButton() && mb->defaultButton() == mb->button(QMessageBox::No);
                    click_box(w, QMessageBox::No);
                    return;
                }
                choose_file(w, QString::fromStdString(root + "/src/main.c"));
            },
            [&]() { button(sp, QStringLiteral("Locate File..."))->click(); });
        check("QSRC-76", "the 'save this directory?' question of Locate File... has No as its default button",
              asked && no_default, std::to_string(asked) + std::to_string(no_default));
    }
    {   // QSRC-77
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q77";
        put_file(root + "/src/main.c", numbered_lines(25));
        z88dk_in_build(fx, root);
        fx.mgr->set_source_dirs({QString::fromStdString(root + "/src")});
        fx.mgr->refresh_panels();
        SourcePanel* sp = fx.dbg()->source_panel();
        const bool shown = s(sp->location_tooltip()) == root + "/src/main.c";
        fx.mgr->set_source_dirs({});   // the file is no longer found: a message replaces it
        fx.mgr->refresh_panels();
        check("QSRC-77", "the location tooltip names the file found, and is empty once the tab shows a "
                         "message instead",
              shown && sp->location_text() == QStringLiteral("main.c:19 - source file not found") &&
                  sp->location_tooltip().isEmpty(),
              s(sp->location_text()) + " | " + s(sp->location_tooltip()));
    }
    {   // QSRC-78
        Fixture fx;
        fx.enable();
        QSignalSpy changed(fx.mgr, &DebuggerManager::source_dirs_changed);
        fx.mgr->on_source_dir_located(QStringLiteral("/b"));
        const int after_first = changed.count();
        fx.mgr->on_source_dir_located(QStringLiteral("/b"));   // the same directory again
        const int after_again = changed.count();
        fx.mgr->set_source_dirs({QStringLiteral("/a")});       // /a, /b
        const int after_set = changed.count();
        fx.mgr->set_source_dirs({QStringLiteral("/a")});       // unchanged
        fx.mgr->set_source_dirs({QStringLiteral("/a"), QStringLiteral("/b")});   // /b was located: same list
        check("QSRC-78", "the search path is announced once per real change: a directory located twice, "
                         "and a configured list that leaves the effective list as it was, announce nothing",
              after_first == 1 && after_again == 1 && after_set == 2 && changed.count() == 2,
              std::to_string(after_first) + std::to_string(after_again) + std::to_string(after_set) +
                  std::to_string(changed.count()));
    }
}

// ── GH #421: line numbers and a breakpoint column in the Source tab ────────
//
// Oracle: the issue's text and the Disassembly's gutter (the same glyphs, the same
// click); jnext-internal GUI, no VHDL counterpart. The marks are read from the
// gutter's PIXELS, as GutterView::red_dot() reads the Disassembly's: the glyph is
// centred at x = 10 on the line's row (a disc r=5, a ring r=4 pen 2, or the left
// half of either).

struct SrcView {
    QMainWindow win;
    SourcePanel* sp = nullptr;
    QPlainTextEdit* ed = nullptr;
    QWidget* gutter = nullptr;
    QWidget* header = nullptr;
    struct Counter : QObject {
        int n = 0;
        bool eventFilter(QObject*, QEvent* e) override {
            if (e->type() == QEvent::Paint) ++n;
            return false;
        }
    } paints;

    SrcView(Fixture& fx, BreakpointModel& m) {
        sp = new SourcePanel(fx.backend.get());
        sp->set_breakpoint_model(&m);
        win.setCentralWidget(sp);
        win.resize(700, 520);
        win.show();
        QApplication::processEvents();
        sp->refresh();
        QApplication::processEvents();
        ed = sp->findChild<QPlainTextEdit*>();
        gutter = ed ? ed->findChild<QWidget*>(QStringLiteral("source_gutter")) : nullptr;
        header = ed ? ed->findChild<QWidget*>(QStringLiteral("source_gutter_header")) : nullptr;
        if (gutter) gutter->installEventFilter(&paints);
    }
    QRect cell(int line) const {
        return ed->cursorRect(QTextCursor(ed->document()->findBlockByNumber(line - 1)));
    }
    int row_y(int line) const { return cell(line).center().y(); }
    QImage gutter_image() const {
        QImage img(gutter->size(), QImage::Format_ARGB32);
        img.fill(Qt::magenta);
        gutter->render(&img);
        return img;
    }
    static bool red(const QImage& img, int x, int y) {
        const QRgb c = img.pixel(x, y);
        return qRed(c) > 180 && qGreen(c) < 120 && qBlue(c) < 120;
    }
    /// "none", "disc", "ring", "half-disc" or "half-ring", from the pixels of the line's row.
    std::string mark(int line) const {
        const QImage img = gutter_image();
        const int cx = kGutterW / 2, y = row_y(line);
        const bool l2 = red(img, cx - 2, y), r2 = red(img, cx + 2, y);
        const bool l3 = red(img, cx - 3, y), r3 = red(img, cx + 3, y);
        const bool l4 = red(img, cx - 4, y), r4 = red(img, cx + 4, y);
        const bool l5 = red(img, cx - 5, y);   // the r=5 disc's edge; a smaller disc misses it
        if (l2 && r2 && l3 && r3 && l5) return "disc";
        if (!l2 && !r2 && l4 && r4) return "ring";
        if (l2 && l3 && l5 && !r2 && !r3) return "half-disc";
        if (!l2 && !r2 && l4 && !r4) return "half-ring";
        if (!l2 && !r2 && !l3 && !r3 && !l4 && !r4) return "none";
        return "?";
    }
    void mouse(QEvent::Type type, int line, Qt::MouseButton button) {
        const QPoint pos(kGutterW / 2, row_y(line));
        QMouseEvent ev(type, QPointF(pos), gutter->mapToGlobal(pos), button,
                       type == QEvent::MouseButtonPress ? button : Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(gutter, &ev);
        QApplication::processEvents();
    }
    void click(int line) { mouse(QEvent::MouseButtonPress, line, Qt::LeftButton); }
    void hover(int line) { mouse(QEvent::MouseMove, line, Qt::NoButton); }
    /// The number cell of `line` is empty: nothing but the gutter's background.
    bool number_blank(const QImage& img, int line) const {
        const QRect c = cell(line);
        const QImage cellimg = img.copy(QRect(kGutterW + 4, c.top(), gutter->width() - kGutterW - 4 - 6, c.height()));
        for (int y = 0; y < cellimg.height(); ++y)
            for (int x = 0; x < cellimg.width(); ++x)
                if (cellimg.pixel(x, y) != qRgb(255, 255, 255)) return false;
        return true;
    }
    /// The number cell of `line` as the gutter paints it equals `text` painted by hand.
    bool number_is(const QImage& img, int line, const QString& text) const {
        const QRect c = cell(line);
        const QRect r(kGutterW + 4, c.top(), gutter->width() - kGutterW - 4 - 6, c.height());
        QImage ref(r.size(), QImage::Format_ARGB32);
        ref.fill(Qt::white);
        QPainter p(&ref);
        p.setFont(ed->font());
        p.setPen(QColor(100, 100, 100));
        p.drawText(QRect(QPoint(0, 0), r.size()), Qt::AlignRight | Qt::AlignVCenter, text);
        p.end();
        return img.copy(r) == ref;
    }
};

int gui_subs(Fixture& fx, BreakpointModel& m) {
    int n = 0;
    for (const auto& si : fx.backend->subscriptions(false))
        if (si.owner == m.client()) ++n;
    return n;
}

std::string write_sld(const std::string& name, const std::string& body) {
    const std::string path = g_tmp->path().toStdString() + "/" + name;
    write_text(path, std::string("|SLD.data.version|1\n") + "main.bas|10||0" + kNextDevice + body);
    return path;
}

void test_source_gutter() {
    const std::string tmp = g_tmp->path().toStdString();
    {   // The default map: main.bas lines 10-13 at $8000/$8002/$8005/$8006
        Fixture fx;
        fx.enable();
        fx.load_map();
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        if (!v.ed || !v.gutter || !v.header) { check("QSRC-79", "the Source tab has a gutter", false); return; }
        const QImage img = v.gutter_image();
        const bool cells = v.number_is(img, 1, "1") && v.number_is(img, 10, "10") &&
                           v.number_is(img, 13, "13");
        // the file's final newline starts no line: its blank last block is not numbered
        const bool distinct = !v.number_is(img, 10, "9") && !v.number_is(img, 10, "11") &&
                              v.number_blank(img, 14);
        const bool margin = v.ed->viewport()->geometry().left() >= v.gutter->geometry().right() &&
                            v.gutter->geometry().top() == v.ed->viewport()->geometry().top() &&
                            v.ed->viewport()->geometry().top() > v.header->geometry().bottom() &&
                            v.gutter->accessibleName() == QStringLiteral("Breakpoints and line numbers");
        check("QSRC-79", "the gutter numbers the file's own lines (1, 10, 13), line 10's cell is not "
                         "9's or 11's, the final newline's blank block has no number, and the text "
                         "starts right of and below the gutter and its header, and the gutter is named for "
                         "accessibility",
              cells && distinct && margin,
              "cells=" + std::to_string(cells) + " distinct=" + std::to_string(distinct) +
                  " margin=" + std::to_string(margin));

        {   // QSRC-81
            QImage h(v.header->size(), QImage::Format_ARGB32);
            h.fill(Qt::magenta);
            v.header->render(&h);
            QFont f = v.ed->font();
            f.setBold(true);
            f.setPointSize(8);
            auto label = [&](const QRect& r, const QString& text) {
                QImage ref(r.size(), QImage::Format_ARGB32);
                ref.fill(Qt::white);
                QPainter p(&ref);
                p.setFont(f);
                p.setPen(QColor(80, 80, 80));
                p.drawText(QRect(QPoint(0, 0), r.size()), Qt::AlignLeft | Qt::AlignVCenter, text);
                p.end();
                return ref;
            };
            const QRect bp(2, 0, kGutterW - 2, v.header->height() - 0);
            const QRect line(kGutterW + 4, 0, v.header->width() - kGutterW - 4 - 1, v.header->height());
            const bool bp_ok = h.copy(bp) == label(bp, "BP");
            const bool line_ok = h.copy(line) == label(line, "Line");
            const bool swapped = h.copy(bp) == label(bp, "Line");
            check("QSRC-81", "the header strip reads BP over the breakpoint column and Line over the "
                             "numbers, in bold 8 pt",
                  bp_ok && line_ok && !swapped && v.header->width() == v.gutter->width(),
                  "bp=" + std::to_string(bp_ok) + " line=" + std::to_string(line_ok));
        }
    }
    {   // QSRC-80: a long file scrolled to line 120
        Fixture fx;
        fx.enable();
        const std::string sld = write_sld("long.sld",
            "main.bas|120||0|" + std::to_string(fx.p80) + "|32768|T|\n");
        std::string text;
        for (int i = 1; i <= 200; ++i) text += "REM long " + std::to_string(i) + "\n";
        write_text(tmp + "/main.bas", text);
        fx.backend->load_source_map(sld, false);
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        QScrollBar* sb = v.ed ? v.ed->verticalScrollBar() : nullptr;
        if (!sb || !v.gutter) { check("QSRC-80", "the Source tab has a gutter", false); return; }
        sb->setValue(119);
        QApplication::processEvents();
        v.paints.n = 0;
        sb->setValue(118);
        QApplication::processEvents();
        const int repainted = v.paints.n;
        sb->setValue(119);
        QApplication::processEvents();
        // The top visible block is line 120; its cell is at the viewport's top.
                const QImage img = v.gutter_image();
        const bool shows = v.ed->cursorForPosition(QPoint(0, 0)).blockNumber() == 119 && v.number_is(img, 120, "120");
        v.click(120);
        const auto rows = m.rows();
        const bool armed = rows.size() == 1 && rows[0].addr == 0x8000;
        m.clear_all();
        text.clear();
        for (int i = 1; i <= 13; ++i) text += "REM line " + std::to_string(i) + "\n";
        write_text(tmp + "/main.bas", text);
        check("QSRC-80", "scrolled to line 120, the gutter repaints, its top cell reads 120, and a click "
                         "on it sets that line's breakpoint",
              repainted > 0 && shows && armed,
              "repainted=" + std::to_string(repainted) + " shows=" + std::to_string(shows) +
                  " armed=" + std::to_string(armed));
    }
    {   // two.sld: main.bas:11 at $8002 p80 and $9000 p90
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80), b = std::to_string(fx.p90);
        const std::string two = write_sld("gut-two.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + a + "|32770|T|\n" +
            "main.bas|12||0|" + a + "|32773|T|\n" + "main.bas|11||0|" + b + "|36864|T|\n");
        fx.backend->load_source_map(two, false);
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        if (!v.gutter) { check("QSRC-82", "the Source tab has a gutter", false); return; }
        const MemSpace p80 = pg(fx.p80), p90 = pg(fx.p90);
        v.click(11);
        {
            const auto rows = m.rows();
            bool a_ok = false, b_ok = false;
            for (const auto& si : fx.backend->subscriptions(false)) {
                if (si.owner != m.client()) continue;
                if (si.filter.lo == 0x8002 && si.filter.space == p80) a_ok = true;
                if (si.filter.lo == 0x9000 && si.filter.space == p90) b_ok = true;
            }
            check("QSRC-82", "one click on line 11 sets ONE group of two locations ($8002 in its page, "
                             "$9000 in its page) and the line shows the filled disc, its neighbours none",
                  rows.size() == 1 && rows[0].group != jnext::dbg::GROUP_NONE && rows[0].locations == 2 &&
                      a_ok && b_ok && v.mark(11) == "disc" && v.mark(10) == "none" && v.mark(12) == "none",
                  "rows=" + std::to_string(rows.size()) + " mark11=" + v.mark(11));
        }
        // QSRC-97 while the group is armed: the hover words
        v.hover(11);
        const QString tip = v.gutter->toolTip();
        const QString hex80 = QString::asprintf("%02X", fx.p80), hex90 = QString::asprintf("%02X", fx.p90);
        check("QSRC-97", "hovering line 11's gutter row sets the tooltip to the line's words, naming "
                         "main.bas:11 and both places with their pages",
              tip == v.sp->line_tooltip(11) && tip.contains(QStringLiteral("main.bas:11")) &&
                  tip.contains("$8002 @" + hex80) && tip.contains("$9000 @" + hex90),
              s(tip));

        // QSRC-86 / QSRC-87: one location removed (the Disassembly gutter's click): a half mark
        m.remove(BreakpointModel::Execute, 0x9000, p90);
        check("QSRC-86", "with one of line 11's two places removed the line shows the half disc, and "
                         "the tooltip says 1 of 2 places",
              v.mark(11) == "half-disc" && v.sp->line_tooltip(11).contains(QStringLiteral("1 of 2 places")),
              v.mark(11) + " | " + s(v.sp->line_tooltip(11)));
        m.set_enabled(BreakpointModel::Execute, 0x8002, false, p80);
        check("QSRC-87", "the same partial line, disabled, shows the half ring and its tooltip says suspended, 1 of 2",
              v.mark(11) == "half-ring" &&
                  v.sp->line_tooltip(11) == QStringLiteral("main.bas:11: suspended breakpoint at $8002 @") +
                                                QString::asprintf("%02X", fx.p80) +
                                                QStringLiteral(" (1 of 2 places)"),
              v.mark(11) + " | " + s(v.sp->line_tooltip(11)));
        m.clear_all();

        v.click(11);
        v.click(11);
        check("QSRC-83", "a second click on line 11 removes the whole group: the GUI owns no "
                         "subscription and the line shows nothing",
              gui_subs(fx, m) == 0 && v.mark(11) == "none",
              std::to_string(gui_subs(fx, m)) + " " + v.mark(11));
        v.click(3);
        check("QSRC-84", "a click on line 3 (no code) adds nothing and the gutter's tooltip says why",
              gui_subs(fx, m) == 0 && v.gutter->toolTip() == QStringLiteral("no code at main.bas:3"),
              std::to_string(gui_subs(fx, m)) + " " + s(v.gutter->toolTip()));

        v.click(11);
        const auto row = m.rows().empty() ? BreakpointModel::Row() : m.rows()[0];
        m.set_enabled(row, false);
        const std::string off = v.mark(11);
        const bool words = v.sp->line_tooltip(11).contains(QStringLiteral("main.bas:11: suspended breakpoint at"));
        m.set_enabled(row, true);
        m.set_master_enabled(false);
        const std::string master_off = v.mark(11);
        m.set_master_enabled(true);
        const std::string armed_again = v.mark(11);
        // a live group with a disabled logical breakpoint on the line can still fire: the disc
        m.clear_all();
        v.click(11);
        m.add(BreakpointModel::Execute, 0x8002);
        m.set_enabled(BreakpointModel::Execute, 0x8002, false);
        const std::string mixed_live = v.mark(11);
        check("QSRC-85", "a group unticked, or the master switch off, shows the ring; armed again, the disc; "
                         "a live group with a disabled logical breakpoint is still the disc (live while any is)",
              off == "ring" && master_off == "ring" && armed_again == "disc" && words &&
                  mixed_live == "disc",
              off + " " + master_off + " " + armed_again + " words=" + std::to_string(words) + " " + mixed_live);
        m.clear_all();
    }
    {   // The default map again, for breakpoints set by address
        Fixture fx;
        fx.enable();
        fx.load_map();
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        if (!v.gutter) { check("QSRC-88", "the Source tab has a gutter", false); return; }
        m.add(BreakpointModel::Execute, 0x8005);
        const std::string drawn = v.mark(12);
        v.click(12);
        check("QSRC-88", "a logical breakpoint set by address at line 12's start shows the filled disc "
                         "there, and a click on the line removes it and adds no group",
              drawn == "disc" && gui_subs(fx, m) == 0, drawn + " " + std::to_string(gui_subs(fx, m)));

        m.add(BreakpointModel::Execute, 0x8003);   // inside main.bas:11's range, not a start
        const std::string mid = v.mark(11);
        v.click(11);
        check("QSRC-89", "a breakpoint inside line 11 (not at its start) shows the half disc, and a "
                         "click removes it",
              mid == "half-disc" && gui_subs(fx, m) == 0, mid + " " + std::to_string(gui_subs(fx, m)));

        m.add(BreakpointModel::Execute, 0x8005, pg(fx.p80 + 1));
        check("QSRC-92", "a breakpoint at line 12's address tied to ANOTHER page marks nothing",
              v.mark(12) == "none", v.mark(12));
        m.clear_all();

        jnext::dbg::ClientInfo ci;
        ci.name = "other";
        ci.kind = jnext::dbg::ClientKind::Test;
        const jnext::dbg::ClientId other = fx.backend->attach(ci).value;
        jnext::dbg::Subscription sub;
        sub.kind = jnext::dbg::EventKind::Execute;
        sub.action = jnext::dbg::Action::Stop;
        sub.filter.lo = sub.filter.hi = 0x8005;
        fx.backend->subscribe(other, sub);
        m.add(BreakpointModel::Execute, 0x8000);   // the model re-reads the listing on its own mutations
        m.remove(BreakpointModel::Execute, 0x8000);
        const auto rows = m.rows();
        check("QSRC-93", "another client's Execute breakpoint at line 12's address is listed but not "
                         "drawn in the BP column",
              rows.size() == 1 && !rows[0].own && v.mark(12) == "none",
              std::to_string(rows.size()) + " " + v.mark(12));
        fx.backend->detach(other);
    }
    {   // line 12 compiled into a page that is not mapped
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80), b = std::to_string(fx.p80 + 2);
        const std::string sld = write_sld("gut-banked.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + a + "|32770|T|\n" +
            "main.bas|12||0|" + b + "|32773|T|\n" + "main.bas|13||0|" + a + "|32774|T|\n" +
            "main.bas|13||0|" + b + "|40960|T|\n");
        fx.backend->load_source_map(sld, false);
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        if (!v.gutter) { check("QSRC-90", "the Source tab has a gutter", false); return; }
        // line 13 has a place that is mapped and one that is not: only the mapped one counts
        v.click(13);
        const std::string mixed = v.mark(13);
        v.click(13);
        v.click(12);
        const bool set = gui_subs(fx, m) == 1;
        check("QSRC-90", "a breakpoint on a line whose page is not mapped now is set, shows no mark, and "
                         "the tooltip names that breakpoint and says its memory is not mapped now; a line with one mapped and one "
                         "unmapped place shows the full disc once the mapped place is armed",
              set && v.mark(12) == "none" && v.sp->line_tooltip(12) == QStringLiteral("main.bas:12: breakpoint at $8005 @") +
                                                QString::asprintf("%02X", fx.p80 + 2) +
                                                QStringLiteral(", not mapped now") &&
                  mixed == "disc",
              std::to_string(gui_subs(fx, m)) + " " + v.mark(12) + " | " + s(v.sp->line_tooltip(12)) + " mixed=" + mixed);
        v.click(12);
        const int left = gui_subs(fx, m);
        // a logical breakpoint at the address of a start whose page is not mapped marks nothing on that line
        m.add(BreakpointModel::Execute, 0x8005);
        const std::string logical = v.mark(12);
        m.clear_all();
        check("QSRC-91", "a click on that line removes the hidden group instead of stacking a second; a logical "
                         "breakpoint at the unmapped place's address does not mark the line",
              left == 0 && logical == "none", std::to_string(left) + " " + logical);
    }
    {   // the default map: repaint, palette, running
        Fixture fx;
        fx.enable();
        fx.load_map();
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        if (!v.gutter) { check("QSRC-94", "the Source tab has a gutter", false); return; }
        QTest::qWait(50);   // let the first paints of the shown window settle
        v.paints.n = 0;
        v.sp->refresh();    // the manager's tick while paused: the mapping may have changed
        QApplication::processEvents();
        const int refresh_paints = v.paints.n;
        QTest::qWait(50);
        v.paints.n = 0;
        m.add_source_line(QStringLiteral("main.bas:12"));   // what the Breakpoints panel's Add does
        QApplication::processEvents();
        const int repainted = v.paints.n;   // before mark(): rendering the gutter paints it too
        const std::string disc = v.mark(12);
        check("QSRC-94", "a breakpoint added through the model repaints the gutter without a panel "
                         "refresh, a panel refresh repaints it too, and line 12 then shows the disc",
              repainted > 0 && disc == "disc" && refresh_paints > 0,
              std::to_string(repainted) + " " + disc + " refresh=" + std::to_string(refresh_paints));
        m.clear_all();

        const QPalette light = QApplication::palette();
        m.add(BreakpointModel::Execute, 0x8005);
        QApplication::setPalette(dark_test_palette());
        QApplication::processEvents();
        const QImage dark = v.gutter_image();
        const QColor bg = QColor::fromRgb(dark.pixel(kGutterW + 1, 1));
        int ink = 0;
        const QRect c = v.cell(10);
        for (int y = c.top(); y <= c.bottom(); ++y)
            for (int x = kGutterW + 4; x < v.gutter->width() - 6; ++x)
                ink = std::max(ink, QColor::fromRgb(dark.pixel(x, y)).lightness());
        const std::string dark_mark = v.mark(12);
        QApplication::setPalette(light);
        QApplication::processEvents();
        const QColor bg_back = QColor::fromRgb(v.gutter_image().pixel(kGutterW + 1, 1));
        check("QSRC-95", "on a dark palette the gutter is dark with lighter numbers and the breakpoint "
                         "stays red; the light palette restores white",
              bg.lightness() < 100 && ink > 150 && dark_mark == "disc" &&
                  bg_back.lightness() > 240,
              std::to_string(bg.lightness()) + " ink=" + std::to_string(ink) + " " + dark_mark +
                  " back=" + std::to_string(bg_back.lightness()));
        m.clear_all();

        v.sp->refresh();
        fx.backend->run(jnext::dbg::CLIENT_NONE);
        v.sp->refresh();   // the manager's tick while running
        v.click(12);
        check("QSRC-98", "while the machine runs, a click on line 12 sets its group and the next paint "
                         "shows the disc, without a pause",
              !fx.backend->state().paused && gui_subs(fx, m) == 1 && v.mark(12) == "disc",
              std::to_string(gui_subs(fx, m)) + " " + v.mark(12));
        m.clear_all();
    }
    {   // a z88dk sdcc map: an alias line, and a line with two starts
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q96";
        copy_z88dk("master/sdcc-debug", "prog.map", root + "/build/prog.map");
        std::filesystem::create_directories(root + "/src");
        std::filesystem::copy_file(std::filesystem::path(JNEXT_Z88DK_FIXTURE_DIR) / "src/main.c",
                                   root + "/src/main.c", std::filesystem::copy_options::overwrite_existing);
        fx.backend->load_source_map(root + "/build/prog.map", true);
        Z80Registers r = fx.emu.cpu().get_registers();
        r.PC = 0x8C6B;
        fx.emu.cpu().set_registers(r);
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        v.sp->set_search_dirs({root + "/src"});
        QApplication::processEvents();
        if (!v.gutter || v.sp->shown_file().empty()) { check("QSRC-96", "the Source tab shows main.c", false, s(v.sp->location_text())); return; }
        const QString file = QString::fromStdString(v.sp->shown_file());
        m.add_source_line(file + ":14");   // an alias of 17: they share $8C6B
        const std::string alias = v.mark(14) + "/" + v.mark(17);
        m.clear_all();
        m.add_source_line(file + ":18");   // two starts: $8C6F and $8C89
        const std::string both = v.mark(18);
        m.remove(BreakpointModel::Execute, 0x8C89, pg(4));
        const std::string one = v.mark(18);
        check("QSRC-96", "z88dk sdcc map: a breakpoint on main.c:14 marks it and its target line 17; "
                         "main.c:18 shows the disc with both starts armed and the half disc with one",
              alias == "disc/disc" && both == "disc" && one == "half-disc",
              alias + " " + both + " " + one);
        m.clear_all();
    }
    {   // GH #421 review 1: line 12 compiled to $8005 in TWO pages
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80), b = std::to_string(fx.p80 + 2);
        const std::string sld = write_sld("gut-twopage.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + a + "|32770|T|\n" +
            "main.bas|12||0|" + a + "|32773|T|\n" + "main.bas|12||0|" + b + "|32773|T|\n" +
            "main.bas|13||0|" + a + "|32774|T|\n");
        fx.backend->load_source_map(sld, false);
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        if (!v.gutter) { check("QSRC-150", "the Source tab has a gutter", false); return; }
        v.click(12);
        const std::string both = v.mark(12);
        m.remove(BreakpointModel::Execute, 0x8005, pg(fx.p80 + 2));   // the place out of view, removed
        const std::string part = v.mark(12);
        const QString part_tip = v.sp->line_tooltip(12);
        m.clear_all();
        v.click(12);
        m.remove(BreakpointModel::Execute, 0x8005, pg(fx.p80));       // the mapped place, removed
        const std::string none = v.mark(12);
        const QString none_tip = v.sp->line_tooltip(12);
        m.clear_all();
        check("QSRC-150", "a line compiled to the same address in two pages: both places armed show the disc; "
                          "with the place in the other page removed, the half disc and 1 of 2 places; with the "
                          "mapped place removed, no mark and the tooltip says not mapped now",
              both == "disc" && part == "half-disc" && part_tip.contains(QStringLiteral("(1 of 2 places)")) &&
                  none == "none" && none_tip.contains(QStringLiteral("not mapped now")),
              both + " " + part + " " + s(part_tip) + " | " + none + " " + s(none_tip));
    }
    {   // GH #421 review 1: the default map
        Fixture fx;
        fx.enable();
        fx.load_map();
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        if (!v.gutter) { check("QSRC-151", "the Source tab has a gutter", false); return; }
        v.mouse(QEvent::MouseButtonPress, 12, Qt::RightButton);
        check("QSRC-151", "a right click in the gutter sets nothing (the context menu is another feature's)",
              gui_subs(fx, m) == 0, std::to_string(gui_subs(fx, m)));

        m.add(BreakpointModel::Write, 0x8005);   // a watchpoint at a code address
        const std::string watch = v.mark(12);
        v.click(12);
        bool group = false;
        for (const auto& r : m.rows())
            if (r.type == BreakpointModel::Execute && r.group != jnext::dbg::GROUP_NONE) group = true;
        check("QSRC-152", "a data breakpoint at a line's address marks nothing, and a click then sets the "
                          "line's execute group",
              watch == "none" && group, watch + " group=" + std::to_string(group));
        m.clear_all();

        m.add(BreakpointModel::Execute, 0x9000);   // sub.bas:1, not main.bas
        const std::string other = v.mark(1) + "/" + v.mark(10);
        m.clear_all();
        check("QSRC-153", "an execute breakpoint in another source file marks no line of this one",
              other == "none/none", other);

        // a message in place of the file: the gutter has nothing to number or toggle
        fx.backend->clear_source_map();
        v.sp->refresh();
        QApplication::processEvents();
        v.hover(1);
        const QString tip = v.gutter->toolTip();
        v.click(1);
        check("QSRC-154", "after the tab shows a message instead of a file, it names no file, its gutter "
                          "has no tooltip and a click sets nothing",
              v.sp->shown_file().empty() && tip.isEmpty() && gui_subs(fx, m) == 0,
              s(tip) + " " + std::to_string(gui_subs(fx, m)));
    }
    {   // GH #421 review 1: hover bounds (the default map shows 13 lines and the final newline's blank block)
        Fixture fx;
        fx.enable();
        fx.load_map();
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        if (!v.gutter) { check("QSRC-155", "the Source tab has a gutter", false); return; }
        v.hover(13);
        const bool real = !v.gutter->toolTip().isEmpty();
        v.hover(14);   // the blank block after the final newline is no line
        const QString blank = v.gutter->toolTip();
        v.hover(13);
        const QPoint below(kGutterW / 2, v.gutter->height() - 3);
        QMouseEvent ev(QEvent::MouseMove, QPointF(below), v.gutter->mapToGlobal(below), Qt::NoButton,
                       Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(v.gutter, &ev);
        const QString under = v.gutter->toolTip();
        check("QSRC-155", "hovering the gutter beside the final newline's blank block, or below the text, "
                          "shows no tooltip (a stale one is cleared)",
              real && blank.isEmpty() && under.isEmpty(), s(blank) + " | " + s(under));
    }
}

// ── QCN-01..04: z88dk C names in every address field (GH #424, design 10.6) ──
// Oracle: the fixture map (`_counter` $8F59, `_main` $8C92, `_square` $8C78; module main_c) and zcc's MODULE
// naming. Every dialog and Go to resolves through the one SymbolTable.
void test_z88dk_c_names() {
    const std::string tmp = g_tmp->path().toStdString();
    Fixture fx;
    fx.enable();
    const std::string map = tmp + "/qcn/prog.map";
    copy_z88dk("v2.4/sccz80-debug", "prog.map", map);
    fx.backend->load_map(map, jnext::dbg::MapFormat::Z88dk);
    // A symbol of a section naming no page is bound to the page mapped at its address when loaded.
    const MemSpace page = fx.backend->source_page(0x8F59);
    BreakpointModel& m = fx.mgr->breakpoints();
    WatchPanel* wp = fx.dbg() ? fx.dbg()->watch_panel() : nullptr;
    BreakpointPanel* bp = fx.dbg() ? fx.dbg()->breakpoint_panel() : nullptr;
    QAction* add_exec = fx.dbg() ? menu_action(fx.dbg(), QStringLiteral("Add &Execute Breakpoint...")) : nullptr;
    if (!wp || !bp || !add_exec) {
        check("QCN-01", "panels and menu", false);
        return;
    }
    {   // QCN-01
        const bool seen = answer_modal(QStringLiteral("counter"), [&]() { wp->on_add_watch(); }) &&
                          answer_modal(QStringLiteral("_counter"), [&]() { wp->on_add_watch(); }) &&
                          answer_modal(QStringLiteral("@counter"), [&]() { wp->on_add_watch(); });
        bool all = seen;
        for (int row = 0; row < 3; ++row)
            all = all && cell(wp, row, 0).contains(QStringLiteral("8F59")) &&
                  cell(wp, row, 1) == QStringLiteral("counter");
        check("QCN-01", "Add Watch with counter, _counter and @counter watches $8F59 three times, each labelled "
                        "with the C name",
              all, s(cell(wp, 0, 0)) + "/" + s(cell(wp, 0, 1)) + " " + s(cell(wp, 1, 0)) + "/" +
                       s(cell(wp, 1, 1)) + " " + s(cell(wp, 2, 0)) + "/" + s(cell(wp, 2, 1)));
    }
    {   // QCN-02
        const bool seen = answer_modal(QStringLiteral("main"), [&]() { add_exec->trigger(); });
        const auto rows = m.rows();
        bp->refresh();
        check("QCN-02", "Debug > Add Execute Breakpoint with `main` sets an Execute row at $8C92 on the name's "
                        "page, and the Breakpoints Symbol column reads main",
              seen && rows.size() == 1 && rows[0].type == BreakpointModel::Execute && rows[0].addr == 0x8C92 &&
                  rows[0].space == page && cell(bp, 0, 3) == QStringLiteral("main"),
              "rows=" + std::to_string(rows.size()) + " sym=" + s(cell(bp, 0, 3)));
        m.clear_all();
    }
    {   // QCN-03
        const auto drive = [&](const QString& name, int type, bool edit) {
            return drive_modals(
                [&](QWidget* w, int n) {
                    auto* dlg = qobject_cast<QDialog*>(w);
                    if (!dlg) return;
                    if (n == 0) {
                        if (QLineEdit* e = dlg->findChild<QLineEdit*>()) e->setText(name);
                        if (auto* c = dlg->findChild<QComboBox*>()) c->setCurrentIndex(type);
                        dlg->accept();
                        return;
                    }
                    dlg->reject();
                },
                [&]() { edit ? bp->on_edit() : bp->on_add(); });
        };
        drive(QStringLiteral("counter"), BreakpointModel::Write, false);
        const auto added = m.rows();
        bp->refresh();
        const QString sym_added = cell(bp, 0, 3);
        if (auto* table = bp->findChild<QTableWidget*>()) table->setCurrentCell(0, 1);
        drive(QStringLiteral("square"), BreakpointModel::Write, true);
        const auto edited = m.rows();
        bp->refresh();
        check("QCN-03", "the Breakpoints panel's own dialog takes C names: `counter` adds a Write row at $8F59 "
                        "with Symbol counter, and editing it to `square` moves it to $8C78",
              added.size() == 1 && added[0].type == BreakpointModel::Write && added[0].addr == 0x8F59 &&
                  sym_added == QStringLiteral("counter") && edited.size() == 1 &&
                  edited[0].type == BreakpointModel::Write && edited[0].addr == 0x8C78 &&
                  cell(bp, 0, 3) == QStringLiteral("square"),
              "added=" + std::to_string(added.size()) + " sym=" + s(sym_added) + " edited=" +
                  std::to_string(edited.size()) + " sym=" + s(cell(bp, 0, 3)));
        m.clear_all();
    }
    {   // QCN-04
        DisasmPanel* dp = fx.dbg()->disasm_panel();
        QLineEdit* dgo = line_edit(dp, QStringLiteral("Address..."));
        auto* dsb = dp ? dp->findChild<QScrollBar*>() : nullptr;
        int by_number = -1, by_name = -1, start = -1;
        if (dgo && dsb) {
            dsb->setValue(0x4000);
            start = dsb->value();
            dgo->setText(QStringLiteral("8C92"));
            emit dgo->returnPressed();
            by_number = dsb->value();
            dsb->setValue(0x4000);
            dgo->setText(QStringLiteral("main"));
            emit dgo->returnPressed();
            by_name = dsb->value();
        }
        MemoryPanel* mp = fx.dbg()->memory_panel();
        QLineEdit* mgo = line_edit(mp, QStringLiteral("$0000"));
        auto* msb = mp ? mp->findChild<QScrollBar*>() : nullptr;
        int m_number = -1, m_name = -1, m_start = -1;
        if (mgo && msb) {
            msb->setValue(0);
            m_start = msb->value();
            mgo->setText(QStringLiteral("8F59"));
            emit mgo->returnPressed();
            m_number = msb->value();
            msb->setValue(0);
            mgo->setText(QStringLiteral("counter"));
            emit mgo->returnPressed();
            m_name = msb->value();
        }
        check("QCN-04", "Go to Address takes a C name: the disassembly's `main` lands where 8C92 does, the memory "
                        "panel's `counter` where 8F59 does",
              by_number != start && by_name == by_number && m_number != m_start && m_name == m_number,
              std::to_string(start) + " " + std::to_string(by_number) + " " + std::to_string(by_name) + " | " +
                  std::to_string(m_start) + " " + std::to_string(m_number) + " " + std::to_string(m_name));
    }
}

// ── GH #423: the Source tab's context menu ───────────────────────────────
// Oracle for all: the issue text, the owner's parity rule (the Source tab offers the
// Disassembly's actions) and the Disassembly's own menu; jnext-internal GUI, no VHDL.

QStringList menu_texts(const QMenu& m) {
    QStringList t;
    for (QAction* a : m.actions())
        if (!a->isSeparator()) t << a->text();
    return t;
}

QAction* menu_action(const QMenu& m, const QString& text) {
    for (QAction* a : m.actions())
        if (!a->isSeparator() && a->text() == text) return a;
    return nullptr;
}

/// main.bas with "REM line N" lines, `over` replacing the given ones.
void write_main(std::initializer_list<std::pair<int, const char*>> over) {
    std::string text;
    for (int i = 1; i <= 13; ++i) {
        std::string line = "REM line " + std::to_string(i);
        for (const auto& o : over)
            if (o.first == i) line = o.second;
        text += line + "\n";
    }
    write_text(g_tmp->path().toStdString() + "/main.bas", text);
}

/// What a real context-menu event opens: its items (with their chords) and, when
/// `pick` is non-empty, that item triggered.
struct Popup {
    bool seen = false;
    QStringList items;
    QStringList chords;   // "Copy=Ctrl+C", ...
};

Popup right_click(QWidget* target, const QPoint& pos, const QString& pick = QString()) {
    Popup out;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || out.seen) return;
        out.seen = true;
        for (QAction* a : menu->actions()) {
            if (a->isSeparator()) continue;
            out.items << a->text();
            if (!a->shortcut().isEmpty())
                out.chords << a->text() + "=" + a->shortcut().toString(QKeySequence::PortableText);
            if (!pick.isEmpty() && a->text() == pick) a->trigger();
        }
        menu->close();
    });
    timer.start(1);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, target->mapToGlobal(pos));
    QApplication::sendEvent(target, &ev);
    timer.stop();
    QApplication::processEvents();
    return out;
}

void set_cpu(Fixture& fx, uint16_t pc, uint16_t hl = 0, uint16_t ix = 0) {
    Z80Registers r = fx.emu.cpu().get_registers();
    r.PC = pc;
    r.HL = hl;
    r.IX = ix;
    fx.emu.cpu().set_registers(r);
}

void run_until_stop(Fixture& fx, int frames = 4) {
    for (int i = 0; i < frames && !fx.backend->state().paused; ++i) fx.emu.run_frame();
    QApplication::processEvents();
}

int transient_subs(Fixture& fx) {
    int n = 0;
    for (const auto& si : fx.backend->subscriptions(true))
        if (si.transient) ++n;
    return n;
}

/// The character column just inside `needle` in `text` (where a click lands on it).
int col_in(const QString& text, const char* needle) { return text.indexOf(QLatin1String(needle)) + 1; }

SourcePanel* window_source_panel(Fixture& fx) {
    SourcePanel* sp = fx.dbg()->source_panel();
    for (auto* tw : fx.dbg()->findChildren<QTabWidget*>())
        if (tw->indexOf(sp) >= 0) tw->setCurrentWidget(sp);
    fx.dbg()->show();
    QApplication::processEvents();
    return sp;
}

void test_source_context_menu() {
    const QStringList base{"Copy", "Copy with Addresses", "Select All", "Toggle Breakpoint", "Run to Here",
                           "Show in Disassembly"};
    const std::string tmp = g_tmp->path().toStdString();
    {
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        if (!v.ed) { check("QSRC-99", "the Source tab has an editor", false); return; }
        const Popup p = right_click(v.ed->viewport(), QPoint(5, v.row_y(12)));
        check("QSRC-99", "a real right click on line 12's text pops the menu: Copy, Copy with Addresses, "
                         "Select All, Toggle Breakpoint, Run to Here, Show in Disassembly; Copy is Ctrl+C and "
                         "Select All Ctrl+A; Qt's stock Copy Link Location is gone",
              p.seen && p.items == base && p.chords.contains("Copy=Ctrl+C") &&
                  p.chords.contains("Select All=Ctrl+A"),
              s(p.items.join("|")) + " " + s(p.chords.join("|")));

        // QSRC-118: the gutter's right click is the same menu
        const Popup g = right_click(v.gutter, QPoint(kGutterW / 2, v.row_y(12)), "Toggle Breakpoint");
        check("QSRC-118", "a real right click on the gutter at line 12 pops the line menu (QSRC-99's items, "
                          "no operand entry) and its Toggle Breakpoint arms $8005",
              g.seen && g.items == base && v.sp->line_mark(12) == SourcePanel::LineMark::Armed &&
                  m.exists(BreakpointModel::Execute, 0x8005, pg(fx.p80)),
              s(g.items.join("|")));
        m.clear_all();
    }
    {   // QSRC-100: Toggle Breakpoint is the gutter's toggle
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80), b = std::to_string(fx.p90);
        fx.backend->load_source_map(write_sld("ctx-two.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + a + "|32770|T|\n" +
            "main.bas|12||0|" + a + "|32773|T|\n" + "main.bas|11||0|" + b + "|36864|T|\n" +
            "main.bas|10||0|" + std::to_string(fx.p80 + 2) + "|32768|T|\n"), false);   // line 10 also in an unmapped page
        write_main({});
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        QMenu menu1;
        v.sp->fill_context_menu(menu1, 11, 0);
        menu_action(menu1, "Toggle Breakpoint")->trigger();
        const auto rows = m.rows();
        const bool one = rows.size() == 1 && rows[0].group != jnext::dbg::GROUP_NONE && rows[0].locations == 2 &&
                         v.sp->line_mark(11) == SourcePanel::LineMark::Armed;
        QMenu menu2;
        v.sp->fill_context_menu(menu2, 11, 0);
        menu_action(menu2, "Toggle Breakpoint")->trigger();
        check("QSRC-100", "two-place line 11, Toggle Breakpoint from its menu: one GUI row (a group of 2), the "
                          "line armed; triggered again: no GUI subscription and no mark",
              one && gui_subs(fx, m) == 0 && v.sp->line_mark(11) == SourcePanel::LineMark::None,
              std::to_string(rows.size()) + " subs=" + std::to_string(gui_subs(fx, m)));

        // QSRC-110 (selection rule), QSRC-111 (Copy with Addresses) on this map
        QGuiApplication::clipboard()->clear();
        QMenu c1;
        v.sp->fill_context_menu(c1, 11, 0);
        const QString one_line = v.ed->textCursor().selectedText();
        menu_action(c1, "Copy")->trigger();
        const QString clip1 = QGuiApplication::clipboard()->text();
        QTextCursor range(v.ed->document()->findBlockByNumber(9));
        range.setPosition(v.ed->document()->findBlockByNumber(11).position() +
                              v.ed->document()->findBlockByNumber(11).length() - 1,
                          QTextCursor::KeepAnchor);
        v.ed->setTextCursor(range);
        QMenu c2;
        v.sp->fill_context_menu(c2, 11, 2);
        const int kept = v.ed->textCursor().selectedText().split(QChar::ParagraphSeparator).size();
        QMenu c3;
        v.sp->fill_context_menu(c3, 5, 0);
        const QString five = v.ed->textCursor().selectedText();
        check("QSRC-110", "right click on line 11 with no selection selects \"REM line 11\" and Copy copies it; "
                          "inside a 3-line selection it keeps the 3 lines; on line 5 outside it, the selection "
                          "becomes \"REM line 5\"",
              one_line == "REM line 11" && clip1 == "REM line 11" && kept == 3 && five == "REM line 5",
              s(one_line) + "|" + s(clip1) + "|" + std::to_string(kept) + "|" + s(five));

        const QString t = [&]() {
            auto at = [&](unsigned ad, unsigned page) { return BreakpointModel::address_text(ad, pg(page)); };
            return QString("main.bas:9  -  REM line 9\nmain.bas:10  ") + at(0x8000, fx.p80) + ", " + at(0x8000, fx.p80 + 2) +
                   "  REM line 10\nmain.bas:11  " + at(0x8002, fx.p80) + ", " + at(0x9000, fx.p90) +
                   "  REM line 11\n";
        }();
        QTextCursor sel(v.ed->document()->findBlockByNumber(8));
        sel.setPosition(v.ed->document()->findBlockByNumber(10).position() +
                            v.ed->document()->findBlockByNumber(10).length() - 1,
                        QTextCursor::KeepAnchor);
        v.ed->setTextCursor(sel);
        const QString got = v.sp->selection_text_with_addresses();
        QMenu c4;
        v.sp->fill_context_menu(c4, 10, 2);
        QGuiApplication::clipboard()->clear();
        menu_action(c4, "Copy with Addresses")->trigger();
        check("QSRC-111", "lines 9-11 selected: selection_text_with_addresses() is file:line, every place "
                          "(mapped or not, with its page) or '-', and the text; the menu action puts it on the "
                          "clipboard",
              got == t && QGuiApplication::clipboard()->text() == t, s(got) + " || " + s(t));
    }
    {   // QSRC-101: a line with no code
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        set_cpu(fx, 0x8000);
        QMenu menu;
        v.sp->fill_context_menu(menu, 3, 0);
        bool all = true;
        for (const char* t : {"Toggle Breakpoint", "Run to Here", "Show in Disassembly"}) {
            QAction* a = menu_action(menu, t);
            all = all && a && !a->isEnabled() && a->toolTip() == "no code at main.bas:3";
            if (a) a->trigger();
        }
        check("QSRC-101", "line 3 (no code): Toggle Breakpoint, Run to Here and Show in Disassembly are present, "
                          "disabled, with the tooltip \"no code at main.bas:3\", and no operand entry; "
                          "triggering them adds nothing and runs nothing",
              all && menu_texts(menu) == base && gui_subs(fx, m) == 0 && fx.backend->state().paused &&
                  fx.pc() == 0x8000,
              s(menu_texts(menu).join("|")));
    }
    {   // QSRC-102 / 103 / 104 / 105: Run to Here through the real manager
        Fixture fx;
        fx.enable();
        fx.load_map();
        set_cpu(fx, 0x8000);
        fx.mgr->refresh_panels();
        SourcePanel* sp = window_source_panel(fx);
        QMenu menu;
        sp->fill_context_menu(menu, 12, 0);
        menu_action(menu, "Run to Here")->trigger();
        run_until_stop(fx);
        fx.mgr->refresh_panels();
        check("QSRC-102", "Run to Here on line 12 from $8000, through the real manager: paused at $8005 on "
                          "line 12, the backend lists no subscription, the Breakpoints panel no row",
              fx.pc() == 0x8005 && fx.backend->state().paused && sp->highlighted_line() == 12 &&
                  fx.backend->subscriptions(true).empty() && fx.mgr->breakpoints().rows().empty(),
              std::to_string(fx.pc()) + " line=" + std::to_string(sp->highlighted_line()));

        // QSRC-105: a second Run to Here before any frame runs
        set_cpu(fx, 0x8000);
        fx.mgr->refresh_panels();
        QMenu m12, m13;
        sp->fill_context_menu(m12, 12, 0);
        menu_action(m12, "Run to Here")->trigger();
        sp->fill_context_menu(m13, 13, 0);
        menu_action(m13, "Run to Here")->trigger();
        run_until_stop(fx);
        check("QSRC-105", "Run to Here on line 12 and, before a frame ran, on line 13: the machine stops at "
                          "$8006, not $8005",
              fx.pc() == 0x8006 && fx.backend->subscriptions(true).empty(), std::to_string(fx.pc()));
    }
    {
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80), b = std::to_string(fx.p90);
        fx.backend->load_source_map(write_sld("ctx-both.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + a + "|32770|T|\n" +
            "main.bas|12||0|" + a + "|32773|T|\n" + "main.bas|12||0|" + b + "|36864|T|\n" +
            "main.bas|13||0|" + a + "|32774|T|\n"), false);
        write_main({});
        set_cpu(fx, 0x8002);
        fx.mgr->refresh_panels();
        SourcePanel* sp = window_source_panel(fx);
        QMenu menu;
        sp->fill_context_menu(menu, 12, 0);
        menu_action(menu, "Run to Here")->trigger();
        run_until_stop(fx);
        check("QSRC-103", "line 12 at $8005 and $9000, PC at the CALL: Run to Here stops at $9000 (reached "
                          "first), no transient left",
              fx.pc() == 0x9000 && transient_subs(fx) == 0, std::to_string(fx.pc()));
    }
    {
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80), far = std::to_string(fx.p80 + 2);
        fx.backend->load_source_map(write_sld("ctx-banked.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + a + "|32770|T|\n" +
            "main.bas|12||0|" + far + "|32773|T|\n" + "main.bas|13||0|" + a + "|32774|T|\n"), false);
        write_main({});
        set_cpu(fx, 0x8000);
        fx.mgr->refresh_panels();
        SourcePanel* sp = window_source_panel(fx);
        QMenu menu;
        sp->fill_context_menu(menu, 12, 0);
        QAction* rt = menu_action(menu, "Run to Here");
        const bool enabled = rt && rt->isEnabled();
        rt->trigger();
        const auto armed = fx.backend->subscriptions(true);
        const bool one = armed.size() == 1 && armed[0].transient && armed[0].filter.space == pg(fx.p80 + 2);
        for (int i = 0; i < 20; ++i) fx.emu.run_frame();
        const bool running = !fx.backend->state().paused;
        fx.backend->pause(jnext::dbg::CLIENT_NONE);
        check("QSRC-104", "line 12 only in a page that is not mapped: Run to Here is enabled and arms one "
                          "transient member tied to that page; 20 frames passing $8005 do not stop it; a Pause "
                          "leaves nothing armed",
              enabled && one && running && transient_subs(fx) == 0,
              "enabled=" + std::to_string(enabled) + " one=" + std::to_string(one) +
                  " running=" + std::to_string(running));

        // QSRC-109: Show in Disassembly for the same unmapped place
        QMenu sm;
        sp->fill_context_menu(sm, 12, 0);
        QAction* show = menu_action(sm, "Show in Disassembly");
        const uint16_t before = fx.dbg()->disasm_panel()->selected_address();
        if (show) show->trigger();
        check("QSRC-109", "line 12 at $8005 in an unmapped page: Show in Disassembly is disabled with a "
                          "\"not mapped now\" tooltip, and the Disassembly's selection is unchanged",
              show && !show->isEnabled() && show->toolTip().contains("not mapped now") &&
                  fx.dbg()->disasm_panel()->selected_address() == before,
              show ? s(show->toolTip()) : "no item");
    }
    {   // QSRC-106 / 107: Enter, and Run to Cursor by focus
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        set_cpu(fx, 0x8000);
        fx.mgr->refresh_panels();
        SourcePanel* sp = window_source_panel(fx);
        auto* ed = sp->findChild<QPlainTextEdit*>();
        auto click_line = [&](int line) {
            QTextBlock b = ed->document()->findBlockByNumber(line - 1);
            const QRect r = ed->cursorRect(QTextCursor(b));
            QTest::mouseClick(ed->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(5, r.center().y()));
        };
        click_line(12);
        const int cur12 = ed->textCursor().blockNumber() + 1;
        QTest::keyClick(ed, Qt::Key_Return);
        run_until_stop(fx);
        check("QSRC-106", "cursor on line 12 (a left click), Return in the editor: stops at $8005, as Run to Here",
              cur12 == 12 && fx.pc() == 0x8005, "cursor=" + std::to_string(cur12) + " pc=" + std::to_string(fx.pc()));

        QAction* rtc = nullptr;
        for (QAction* a : fx.dbg()->findChildren<QAction*>())
            if (a->text() == "Run to Cursor") rtc = a;
        set_cpu(fx, 0x8000);
        fx.mgr->refresh_panels();
        click_line(13);
        ed->setFocus();
        const bool focused = sp->has_text_focus();
        if (rtc) rtc->trigger();
        run_until_stop(fx);
        const uint16_t from_source = fx.pc();

        set_cpu(fx, 0x8000);
        fx.mgr->refresh_panels();
        DisasmPanel* dp = fx.dbg()->disasm_panel();
        dp->show_address(0x8005);
        dp->setFocus();
        const bool lost = !sp->has_text_focus();
        if (rtc) rtc->trigger();
        run_until_stop(fx);
        check("QSRC-107", "Run to Cursor with the focus in the Source editor (cursor on line 13) stops at $8006; "
                          "with the focus in the Disassembly (caret $8005) at $8005",
              rtc && focused && lost && from_source == 0x8006 && fx.pc() == 0x8005,
              "focused=" + std::to_string(focused) + " lost=" + std::to_string(lost) + " src=" +
                  std::to_string(from_source) + " dis=" + std::to_string(fx.pc()));
    }
    {   // QSRC-108: Show in Disassembly
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        fx.mgr->refresh_panels();
        SourcePanel* sp = window_source_panel(fx);
        DisasmPanel* dp = fx.dbg()->disasm_panel();
        QMenu menu;
        sp->fill_context_menu(menu, 12, 0);
        menu_action(menu, "Show in Disassembly")->trigger();
        uint16_t lo = 0, hi = 0;
        const bool sel = dp->selection_range(lo, hi);
        const bool plain = dp->selected_address() == 0x8005 && sel && lo == 0x8005 && hi == 0x8005;

        const std::string a = std::to_string(fx.p80), b = std::to_string(fx.p90);
        fx.backend->load_source_map(write_sld("ctx-show.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + a + "|32770|T|\n" +
            "main.bas|11||0|" + b + "|36864|T|\n" + "main.bas|12||0|" + a + "|32773|T|\n"), false);
        fx.mgr->refresh_panels();
        QMenu two;
        sp->fill_context_menu(two, 11, 0);
        QAction* sub = menu_action(two, "Show in Disassembly");
        QStringList items;
        if (sub && sub->menu())
            for (QAction* it : sub->menu()->actions()) items << it->text();
        const QStringList want{BreakpointModel::address_text(0x8002, pg(fx.p80)),
                               BreakpointModel::address_text(0x9000, pg(fx.p90))};
        if (sub && sub->menu() && sub->menu()->actions().size() == 2) sub->menu()->actions()[1]->trigger();
        check("QSRC-108", "line 12: Show in Disassembly selects $8005 (the range is that one line); a two-place "
                          "line 11 has a submenu \"$8002 @pp\", \"$9000 @qq\" and the second selects $9000",
              plain && items == want && dp->selected_address() == 0x9000,
              s(items.join("|")) + " sel=" + std::to_string(plain));
    }
    {   // QSRC-112: the keyboard
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        v.ed->setFocus();
        QGuiApplication::clipboard()->clear();
        QTest::keyClick(v.ed, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(v.ed, Qt::Key_C, Qt::ControlModifier);
        std::string full;
        {
            std::ifstream in(tmp + "/main.bas", std::ios::binary);
            full.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        check("QSRC-112", "Ctrl+A then Ctrl+C in the editor: the clipboard holds the file's whole text",
              QGuiApplication::clipboard()->text() == QString::fromStdString(full),
              s(QGuiApplication::clipboard()->text()));
    }
    // The operands: one fixture per row, main.bas rewritten before the panel loads it.
    auto operands = [&](std::initializer_list<std::pair<int, const char*>> text, const std::function<void(Fixture&)>& setup,
                        const std::function<void(Fixture&, SrcView&, BreakpointModel&)>& body) {
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main(text);
        setup(fx);
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        v.sp->set_watch_panel(fx.dbg()->watch_panel());
        body(fx, v, m);
    };
    auto menu_of = [](SrcView& v, int line, const char* needle) {
        auto m = std::make_unique<QMenu>();
        const QString text = v.ed->document()->findBlockByNumber(line - 1).text();
        v.sp->fill_context_menu(*m, line, needle ? col_in(text, needle) : -1);
        return m;
    };
    operands({{11, "ld a,(counter)"}},
             [](Fixture& fx) { const_cast<SymbolTable&>(fx.backend->symbols()).add_label("counter", 0x9000, std::nullopt); },
             [&](Fixture& fx, SrcView& v, BreakpointModel& m) {
        auto menu = menu_of(v, 11, "counter");
        const QStringList t = menu_texts(*menu);
        const bool listed = t.contains("Watch 'counter' ($9000)") && t.contains("Break on Read 'counter' ($9000)") &&
                            t.contains("Break on Write 'counter' ($9000)");
        WatchPanel* wp = fx.dbg()->watch_panel();
        const int w0 = wp->watch_count();
        menu_action(*menu, "Watch 'counter' ($9000)")->trigger();
        menu_action(*menu, "Break on Read 'counter' ($9000)")->trigger();
        const bool read_only = m.exists(BreakpointModel::Read, 0x9000) && !m.exists(BreakpointModel::Write, 0x9000);
        menu_action(*menu, "Break on Write 'counter' ($9000)")->trigger();
        check("QSRC-113", "a click on the label `counter` ($9000) in a source line offers Watch, Break on Read "
                          "and Break on Write for 'counter' ($9000); they add a watch labelled counter and a Read "
                          "and a Write data breakpoint at $9000, each of its own kind",
              listed && wp->watch_count() == w0 + 1 && cell(wp, w0, 1) == "counter" &&
                  cell(wp, w0, 0).contains("9000") && read_only && m.exists(BreakpointModel::Read, 0x9000) &&
                  m.exists(BreakpointModel::Write, 0x9000),
              s(t.join("|")));
    });
    operands({{1, "ld hl,$5800"}, {2, "ld hl,0x5800"}, {3, "ld hl,5800h"}, {4, "ld hl,#5800"}, {5, "ld hl,22528"},
              {6, "ld hl,$10000"}},
             [](Fixture&) {},
             [&](Fixture&, SrcView& v, BreakpointModel&) {
        const QStringList want{"Watch $5800", "Break on Read $5800", "Break on Write $5800"};
        bool all = true;
        std::string seen;
        const char* needles[] = {"5800", "5800", "5800", "5800", "225"};
        for (int i = 0; i < 5; ++i) {
            auto menu = menu_of(v, i + 1, needles[i]);
            const QStringList t = menu_texts(*menu);
            const bool has = t.contains(want[0]) && t.contains(want[1]) && t.contains(want[2]);
            all = all && has;
            seen += has ? "y" : "n";
        }
        auto big = menu_of(v, 6, "1000");
        bool none = true;
        for (const QString& t : menu_texts(*big)) none = none && !t.startsWith("Watch") && !t.startsWith("Break");
        check("QSRC-114", "the literals $5800, 0x5800, 5800h, #5800 and 22528 each offer Watch $5800 and Break on "
                          "Read/Write $5800 (the Disassembly's words); $10000 offers none",
              all && none, seen);
    });
    operands({{1, "ld (hl),a"}, {2, "ld b,(ix+2)"}, {3, "push hl"}},
             [](Fixture& fx) { set_cpu(fx, 0x8000, 0x4000, 0x6000); },
             [&](Fixture&, SrcView& v, BreakpointModel&) {
        const QStringList hl = menu_texts(*menu_of(v, 1, "hl"));
        const QStringList ix = menu_texts(*menu_of(v, 2, "ix"));
        const QStringList push = menu_texts(*menu_of(v, 3, "hl"));
        bool none = true;
        for (const QString& t : push) none = none && !t.startsWith("Watch") && !t.startsWith("Break");
        check("QSRC-115", "a click on hl in `ld (hl),a` offers Watch and Break on Read/Write (HL) = $4000, on ix in "
                          "`ld b,(ix+2)` the (IX) = $6000 ones, and on hl in `push hl` nothing",
              hl.contains("Watch (HL) = $4000") && hl.contains("Break on Read (HL) = $4000") &&
                  hl.contains("Break on Write (HL) = $4000") && ix.contains("Watch (IX) = $6000") &&
                  ix.contains("Break on Read (IX) = $6000") && ix.contains("Break on Write (IX) = $6000") && none,
              s(hl.join("|")) + " / " + s(ix.join("|")));
    });
    operands({{11, "jp BEEF"}},
             [](Fixture& fx) { const_cast<SymbolTable&>(fx.backend->symbols()).add_label("BEEF", 0x8005, std::nullopt); },
             [&](Fixture&, SrcView& v, BreakpointModel&) {
        auto plain = menu_of(v, 10, "REM");   // no label on line 10, no operand under "REM"
        auto word = menu_of(v, 11, "BEEF");
        const QStringList wt = menu_texts(*word);
        check("QSRC-116", "no symbol under the click: no watch or break entry and no separator after Show in "
                          "Disassembly; with a label BEEF at $8005 a click on BEEF is the NAME ('BEEF' ($8005)), "
                          "not the number $BEEF",
              menu_texts(*plain) == base && plain->actions().last()->text() == "Show in Disassembly" &&
                  wt.contains("Watch 'BEEF' ($8005)") && !wt.contains("Watch $beef"),
              s(wt.join("|")));
    });
    operands({{12, "Landing: nop"}},
             [](Fixture& fx) { const_cast<SymbolTable&>(fx.backend->symbols()).add_label("Landing", 0x8005, std::nullopt); },
             [&](Fixture&, SrcView& v, BreakpointModel&) {
        auto gutter = menu_of(v, 12, nullptr);
        auto word = menu_of(v, 12, "Landing");
        check("QSRC-117", "label Landing at line 12's start: the gutter's menu (no word) has Watch 'Landing' "
                          "($8005); a click on the word lists it once",
              menu_texts(*gutter).contains("Watch 'Landing' ($8005)") &&
                  menu_texts(*word).count("Watch 'Landing' ($8005)") == 1,
              s(menu_texts(*gutter).join("|")) + " / " + s(menu_texts(*word).join("|")));
    });
    {   // QSRC-119: the Disassembly's menu, as before the shared builder
        Fixture fx;
        fx.enable();
        DisasmPanel* dp = fx.dbg()->disasm_panel();
        for (int i = -300; i < 300; i += 3) {   // every visible line is the same instruction
            fx.emu.mmu().write(static_cast<uint16_t>(0xA000 + i + 0), 0x21);
            fx.emu.mmu().write(static_cast<uint16_t>(0xA000 + i + 1), 0x00);
            fx.emu.mmu().write(static_cast<uint16_t>(0xA000 + i + 2), 0x40);
        }
        dp->show_address(0xA000);
        const Popup p = right_click(dp, QPoint(120, 52 + 9));
        const QStringList want{"Copy", "Copy with Addresses", "Select All", "Toggle Breakpoint", "Run to Here",
                               "Go to Address...", "Watch $4000", "Break on Read $4000", "Break on Write $4000"};
        check("QSRC-119", "the Disassembly's menu on a `LD HL,$4000` line (watch panel set, no symbols) lists "
                          "exactly Copy, Copy with Addresses, Select All, Toggle Breakpoint, Run to Here, Go to "
                          "Address..., Watch $4000, Break on Read $4000, Break on Write $4000, in that order",
              p.seen && p.items == want, s(p.items.join("|")));
    }
}


// ── GH #423 review 1 ──────────────────────────────────────────────────────

void test_source_menu_review() {
    {   // QSRC-160 / 161: Run to Here on a corrupt machine goes through the corruption prompt
        auto make = [](Fixture& fx) {
            fx.enable();
            fx.load_map();
            fx.mgr->refresh_panels();
            const uint8_t garbage[4] = {0, 0, 0, 0};
            StateReader r(garbage, sizeof(garbage));
            fx.emu.load_state(r);   // the Task-60b corrupt state, through the real path
            fx.backend->load_source_map(fx.sld, false);   // the restore may have reset the view
            fx.mgr->refresh_panels();
        };
        Fixture no;
        make(no);
        SourcePanel* sp = window_source_panel(no);
        QMenu menu;
        sp->fill_context_menu(menu, 12, 0);
        QAction* rt = menu_action(menu, "Run to Here");
        const int asked = drive_modals([](QWidget* w, int) { click_box(w, QMessageBox::No); },
                                       [&]() { if (rt) rt->trigger(); });
        run_until_stop(no, 2);
        check("QSRC-160", "on a corrupt machine Run to Here asks first; answering No leaves it paused at the same PC "
                          "with nothing armed",
              rt && asked == 1 && no.backend->state().paused && no.pc() == 0x8000 &&
                  no.backend->subscriptions(true).empty() && no.backend->resume_blocked_by_corruption().has_value(),
              "asked=" + std::to_string(asked) + " pc=" + std::to_string(no.pc()));

        Fixture yes;
        make(yes);
        SourcePanel* sy = window_source_panel(yes);
        QMenu menu2;
        sy->fill_context_menu(menu2, 12, 0);
        QAction* rt2 = menu_action(menu2, "Run to Here");
        const int asked2 = drive_modals([](QWidget* w, int) { click_box(w, QMessageBox::Yes); },
                                        [&]() { if (rt2) rt2->trigger(); });
        run_until_stop(yes);
        check("QSRC-161", "answering Yes acknowledges the corruption and runs: stopped at $8005, no incident left",
              rt2 && asked2 == 1 && yes.pc() == 0x8005 && !yes.backend->resume_blocked_by_corruption().has_value(),
              "asked=" + std::to_string(asked2) + " pc=" + std::to_string(yes.pc()));
    }
    {   // QSRC-162: the submenu's unmapped place
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80), far = std::to_string(fx.p80 + 2);
        fx.backend->load_source_map(write_sld("ctx-sub.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + a + "|32770|T|\n" +
            "main.bas|11||0|" + far + "|36864|T|\n" + "main.bas|12||0|" + a + "|32773|T|\n"), false);
        write_main({});
        fx.mgr->refresh_panels();
        SourcePanel* sp = window_source_panel(fx);
        DisasmPanel* dp = fx.dbg()->disasm_panel();
        dp->show_address(0x8005);
        uint16_t lo = 0, hi = 0;
        dp->selection_range(lo, hi);
        QMenu menu;
        sp->fill_context_menu(menu, 11, 0);
        QAction* sub = menu_action(menu, "Show in Disassembly");
        QMenu* sm = sub ? sub->menu() : nullptr;
        bool shape = sm && sm->actions().size() == 2;
        QAction* mapped = shape ? sm->actions()[0] : nullptr;
        QAction* unmapped = shape ? sm->actions()[1] : nullptr;
        const bool states = shape && mapped->isEnabled() && !unmapped->isEnabled() &&
                            unmapped->toolTip().contains("not mapped now");
        if (unmapped) unmapped->trigger();
        uint16_t lo2 = 0, hi2 = 0;
        const bool kept = dp->selection_range(lo2, hi2) && lo2 == lo && hi2 == hi && dp->selected_address() == 0x8005;
        check("QSRC-162", "line 11 at one mapped and one unmapped place: the submenu's unmapped item is disabled with "
                          "the \"not mapped now\" tooltip, and triggering it leaves the Disassembly's selection alone",
              states && kept, "states=" + std::to_string(states) + " kept=" + std::to_string(kept));
    }
    {   // QSRC-163: Go to Address only centres the view
        Fixture fx;
        fx.enable();
        DisasmPanel* dp = fx.dbg()->disasm_panel();
        dp->select_all_visible();
        uint16_t lo = 0, hi = 0, lo2 = 0, hi2 = 0;
        const bool had = dp->selection_range(lo, hi);
        QLineEdit* box = dp->findChild<QLineEdit*>();
        if (box) {
            box->setText(QStringLiteral("9000"));
            QTest::keyClick(box, Qt::Key_Return);
        }
        const bool still = dp->selection_range(lo2, hi2);
        check("QSRC-163", "Go to Address (the address box) centres the view and leaves a selected range alone",
              box && had && still && lo2 == lo && hi2 == hi,
              "had=" + std::to_string(had) + " still=" + std::to_string(still));
    }
    {   // QSRC-164: a click never removes another client's breakpoint
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        jnext::dbg::ClientInfo ci;
        ci.name = "gdb-like";
        ci.kind = jnext::dbg::ClientKind::Test;
        const jnext::dbg::ClientId other = fx.backend->attach(ci).value;
        jnext::dbg::Subscription sub;
        sub.kind = jnext::dbg::EventKind::Execute;
        sub.action = jnext::dbg::Action::Stop;
        sub.filter.lo = sub.filter.hi = 0x8005;
        fx.backend->subscribe(other, sub);
        auto foreign = [&]() {
            int n = 0;
            for (const auto& si : fx.backend->subscriptions(false))
                if (si.owner == other) ++n;
            return n;
        };
        const std::string before = v.mark(12);
        v.click(12);
        const std::string added = v.mark(12);
        const int own1 = gui_subs(fx, m), foreign1 = foreign();
        v.click(12);
        const int own2 = gui_subs(fx, m), foreign2 = foreign();
        check("QSRC-164", "another client's Execute breakpoint on line 12's address: the BP column draws nothing, a click "
                          "adds the window's own beside it, and the next click removes only the window's own",
              before == "none" && added == "disc" && own1 == 1 && foreign1 == 1 && own2 == 0 && foreign2 == 1,
              before + " " + added + " own=" + std::to_string(own1) + "/" + std::to_string(own2) +
                  " foreign=" + std::to_string(foreign1) + "/" + std::to_string(foreign2));
        fx.backend->detach(other);
    }
}

// ── QSRC-120..139: language detection and syntax highlighting (GH #422) ──

/// A document with a highlighter, read back through the block layouts' formats (the
/// highlighter's formats are layout formats).
struct Hl {
    QTextDocument doc;
    SourceHighlighter hl{nullptr};
    // A document emits contentsChange (which drives a highlighter) only when it has a layout:
    // documentLayout() creates the default one, which also lays the text out for QSRC-132.
    Hl() {
        QFont mono("Monospace", 10);   // the tab's font: column alignment is its point
        mono.setStyleHint(QFont::Monospace);
        doc.setDefaultFont(mono);
        doc.documentLayout();
        hl.setDocument(&doc);
    }
    void load(SourceLanguage l, const QString& t) {
        hl.set_language(l);
        doc.setPlainText(t);
    }
};

/// One char per column of the block: K keyword, I instruction, P preprocessor, L label,
/// S string, Q char, N integer, F float, C comment, '.' no colour (Text has no format).
std::string role_mask(const SourceHighlighter& hl, const QTextBlock& b) {
    static const char kChars[SourceHighlighter::kRoles + 1] = ".KIPLSQNFC";
    std::string m(static_cast<size_t>(b.length() - 1), '.');
    for (const QTextLayout::FormatRange& fr : b.layout()->formats())
        for (int r = 0; r < SourceHighlighter::kRoles; ++r)
            if (fr.format.foreground().color() == hl.colour(static_cast<SourceHighlighter::Role>(r)))
                for (int c = fr.start; c < fr.start + fr.length && c < static_cast<int>(m.size()); ++c)
                    m[static_cast<size_t>(c)] = kChars[r];
    return m;
}
std::string role_mask(const Hl& h, int line) {
    return role_mask(h.hl, h.doc.findBlockByNumber(line));
}
/// The role char of `token`'s first occurrence on `line` ('?' when its chars differ).
char tok(const Hl& h, int line, const QString& token, int from = 0) {
    const QTextBlock b = h.doc.findBlockByNumber(line);
    const int at = b.text().indexOf(token, from);
    if (at < 0) return '!';
    const std::string m = role_mask(h, line);
    char c = m[static_cast<size_t>(at)];
    for (int i = 0; i < token.size(); ++i)
        if (m[static_cast<size_t>(at + i)] != c) return '?';
    return c;
}
bool has_role(const Hl& h, int line, char role) {
    return role_mask(h, line).find(role) != std::string::npos;
}

QString lines(const QStringList& l) { return l.join(QLatin1Char('\n')); }
QString repeat_line(const QString& line, int n) { return lines(QStringList(n, line)); }

SourceLanguage lang(const char* name, const QString& text = QString()) {
    return source_language_for(name, text);
}

void test_source_highlight_detection() {
    const QString prose = QStringLiteral("Some notes about the build.\nMore words here.\nNothing else.");
    check("QSRC-120", "the extension picks the language, case-insensitively: .c .h C; .asm .s .z80 .inc "
                      "and the m4 source zxn_crt.asm.m4 assembly; .bas Boriel ZX Basic; a prose .txt "
                      "and a bare .m4 plain",
          lang("main.c") == SourceLanguage::C && lang("x.H") == SourceLanguage::C &&
              lang("a.asm") == SourceLanguage::Asm && lang("b.S") == SourceLanguage::Asm &&
              lang("c.z80") == SourceLanguage::Asm && lang("d.inc") == SourceLanguage::Asm &&
              lang("zxn_crt.asm.m4") == SourceLanguage::Asm &&
              lang("dir.d/zxn_crt.ASM.M4") == SourceLanguage::Asm &&
              lang("p.bas") == SourceLanguage::Basic && lang("P.BAS") == SourceLanguage::Basic &&
              lang("notes.txt", prose) == SourceLanguage::Plain &&
              lang("x.m4", prose) == SourceLanguage::Plain &&
              lang("dir.c/noext", prose) == SourceLanguage::Plain);

    const QString asm_text = lines({
        QStringLiteral("start:  ld a,1"), QStringLiteral("        ld b,2"), QStringLiteral("        nop"),
        QStringLiteral("        halt"),   QStringLiteral("        ret"),     QStringLiteral("; end"),
        QStringLiteral("        push hl"), QStringLiteral("        pop hl"),  QStringLiteral("        di"),
        QStringLiteral("        ei")});
    const QString c_text = lines({
        QStringLiteral("static void f(void)"), QStringLiteral("{"), QStringLiteral("    unsigned char c;"),
        QStringLiteral("    c = 1;"), QStringLiteral("}"), QStringLiteral(""),
        QStringLiteral("typedef struct S S;"), QStringLiteral("void g(void);"), QStringLiteral(""),
        QStringLiteral("// end")});
    const QString bas_text = lines({
        QStringLiteral("10 PRINT \"hi\""), QStringLiteral("20 DIM x AS UBYTE"),
        QStringLiteral("30 BORDER 1"), QStringLiteral("40 CLS"), QStringLiteral("50 PAUSE 0"),
        QStringLiteral("60 BEEP 1,2"), QStringLiteral("70 PAPER 1"), QStringLiteral("80 INK 2"),
        QStringLiteral("90 PLOT 1,1"), QStringLiteral("100 DRAW 5,5")});
    bool by_content = true;
    for (const char* name : {"x.m4", "prog.txt", "noext"})
        by_content = by_content && lang(name, asm_text) == SourceLanguage::Asm &&
                     lang(name, c_text) == SourceLanguage::C &&
                     lang(name, bas_text) == SourceLanguage::Basic &&
                     lang(name, prose) == SourceLanguage::Plain &&
                     lang(name, QString()) == SourceLanguage::Plain;
    const QString tie = lines({
        QStringLiteral("  ld a,1"), QStringLiteral("  ld b,2"), QStringLiteral("  nop"),
        QStringLiteral("10 PRINT 1"), QStringLiteral("20 CLS"), QStringLiteral("30 BEEP 1,1")});
    check("QSRC-121", "an unknown extension is read by content (3 evidence lines, strictly more than "
                      "either other language): assembly, C and Boriel texts are told apart, prose and "
                      "an empty text are plain, and a 3 + 3 tie is plain",
          by_content && lang("t.txt", tie) == SourceLanguage::Plain);

    const QString asm_lines = repeat_line(QStringLiteral("  ld a,1"), 50);
    auto padded = [&](int prose_lines) {
        return repeat_line(QStringLiteral("just some words"), prose_lines) + QLatin1Char('\n') + asm_lines;
    };
    check("QSRC-122", "the extension wins over the content, and only the first 200 lines are read: "
                      "assembly on lines 198-200 decides, on 199-250 and on 201-250 it does not",
          lang("x.c", asm_lines) == SourceLanguage::C &&
              lang("x.txt", padded(197)) == SourceLanguage::Asm &&
              lang("x.txt", padded(198)) == SourceLanguage::Plain &&
              lang("x.txt", padded(200)) == SourceLanguage::Plain);

    // QSRC-145: what counts as evidence. Each condition has a text on either side of it.
    {
        const QString braces = QStringLiteral("{\n}\n{\n");
        const QString two_braces = QStringLiteral("{\n}\n");
        const QString labelled = QStringLiteral("a1: ld a,1\nb2: nop\nc3: ret\n");
        const QString label_unknown = QStringLiteral("a1: zork\nb2: zork\nc3: zork\n");
        const QString under = QStringLiteral("__naked\n__banked\n__critical\n");
        const QString single = QStringLiteral("_naked\n_banked\n_critical\n");
        check("QSRC-145", "content evidence: three brace-only lines are C and two are not enough; a "
                          "column-0 label is skipped to the instruction behind it (and an unknown "
                          "word behind it is no evidence); three __ words are C and three _ words are not",
              lang("t.txt", braces) == SourceLanguage::C && lang("t.txt", two_braces) == SourceLanguage::Plain &&
                  lang("t.txt", labelled) == SourceLanguage::Asm &&
                  lang("t.txt", label_unknown) == SourceLanguage::Plain &&
                  lang("t.txt", under) == SourceLanguage::C && lang("t.txt", single) == SourceLanguage::Plain);
    }
}

void test_source_highlight_rules() {
    // QSRC-123: mnemonics and registers are the disassembler's, enumerated here independently.
    {
        QSet<QString> mn, rg;
        static const uint8_t pre[7][2] = {{0, 0}, {0xCB, 0}, {0xED, 0}, {0xDD, 0}, {0xFD, 0},
                                          {0xDD, 0xCB}, {0xFD, 0xCB}};
        static const int plen[7] = {0, 1, 1, 1, 1, 2, 2};
        for (int p = 0; p < 7; ++p)
            for (int op = 0; op < 256; ++op) {
                uint8_t b[4] = {0, 0, 0, 0};
                int n = 0;
                for (int k = 0; k < plen[p]; ++k) b[n++] = pre[p][k];
                if (p >= 5) n = 3;
                b[n] = static_cast<uint8_t>(op);
                const DisasmLine l = disasm_one(0, [&](uint16_t a) -> uint8_t { return a < 4 ? b[a] : 0; });
                const QString text = QString::fromLatin1(l.mnemonic);
                const QString first = text.section(QLatin1Char(' '), 0, 0);
                if (first == QLatin1String("DB")) continue;
                mn.insert(first);
                const QString ops = text.section(QLatin1Char(' '), 1);
                static const QRegularExpression word(QStringLiteral("\\$[0-9A-F]+|[A-Z][A-Z0-9]*'?"));
                auto it = word.globalMatch(ops);
                while (it.hasNext()) {
                    const QString w = it.next().captured(0);
                    if (!w.startsWith(QLatin1Char('$'))) rg.insert(w);
                }
            }
        const auto& am = SourceHighlighter::asm_mnemonics();
        const auto& ar = SourceHighlighter::asm_registers();
        check("QSRC-123", "the mnemonics and registers are the disassembler's: LDIRSCALE is a mnemonic, "
                          "LOOP and DB are not, AF' IXH and NZ are registers",
              am == mn && ar == rg && am.contains(QStringLiteral("LDIRSCALE")) &&
                  am.contains(QStringLiteral("NEXTREG")) && am.contains(QStringLiteral("LD")) &&
                  !am.contains(QStringLiteral("LOOP")) && !am.contains(QStringLiteral("DB")) &&
                  ar.contains(QStringLiteral("AF'")) && ar.contains(QStringLiteral("IXH")) &&
                  ar.contains(QStringLiteral("NZ")) && !ar.contains(QStringLiteral("FF")),
              "mn=" + std::to_string(am.size()) + "/" + std::to_string(mn.size()) +
                  " rg=" + std::to_string(ar.size()) + "/" + std::to_string(rg.size()));
    }
    {   // QSRC-124
        bool z80asm = true, sjasm = true;
        for (const char* d : {"SECTION", "DEFC", "DEFVARS", "C_LINE", "IFNDEF", "PHASE", "EXTERN", "PUBLIC"})
            z80asm = z80asm && SourceHighlighter::is_asm_directive(QString::fromLatin1(d)) &&
                     SourceHighlighter::is_asm_directive(QString::fromLatin1(d).toLower());
        for (const char* d : {"DEVICE", "SAVENEX", "SLDOPT", "MMU", "DISP", "ENT", ".db", "STRUCT", "ENDS", "EQU", "DEFL"})
            sjasm = sjasm && SourceHighlighter::is_asm_directive(QString::fromLatin1(d)) &&
                    SourceHighlighter::is_asm_directive(QString::fromLatin1(d).toLower());
        Hl h;
        h.load(SourceLanguage::Asm, QStringLiteral("  db 1\n  ld a,1\n  .DEFB 2"));
        check("QSRC-124", "directives: the z80asm and the sjasmplus names are directives in either case "
                          "and with a leading dot; db is a directive (Keyword class, not Instruction) and ld is not",
              z80asm && sjasm && !SourceHighlighter::is_asm_directive(QStringLiteral("ld")) &&
                  !SourceHighlighter::is_asm_directive(QStringLiteral("ldir")) &&
                  tok(h, 0, QStringLiteral("db")) == 'K' && tok(h, 1, QStringLiteral("ld")) == 'I' &&
                  tok(h, 2, QStringLiteral(".DEFB")) == 'K');
    }
    {   // QSRC-125
        Hl h;
        h.load(SourceLanguage::Asm,
               QStringLiteral("loop:  ld a,(hl) ; next\n"
                              "  db $FF #FF 0xFF 0FFh %0101 0b0101 255\n"
                              "  db FFh,$,$\n"
                              "  LD A,(HL)"));
        const bool line = role_mask(h, 0) == "LLLLL..II.IKKIIK.CCCCCC";
        const bool nums = role_mask(h, 1) == "..KK.NNN.NNN.NNNN.NNNN.NNNNN.NNNNNN.NNN";
        const bool not_nums = role_mask(h, 2) == "..KK....K.K.";
        const bool upper = role_mask(h, 3) == "..II.IKKIIK";
        check("QSRC-125", "an assembly line: the label, mnemonic, registers and comment are coloured and "
                          "the punctuation is Keyword; $FF #FF 0xFF 0FFh %0101 0b0101 255 are numbers, FFh "
                          "and a bare $ are not; upper case is the same as lower",
              line && nums && not_nums && upper,
              role_mask(h, 0) + " " + role_mask(h, 1) + " " + role_mask(h, 2) + " " + role_mask(h, 3));
    }
    {   // QSRC-126
        Hl h;
        h.load(SourceLanguage::Asm, QStringLiteral("  ex af,af' ; swap\n  ld a,\";\"\n  cp 'x'"));
        check("QSRC-126", "the apostrophe of AF' belongs to the register, not to a string, and a ';' in a "
                          "string or a Char literal starts no comment",
              role_mask(h, 0) == "..II.IIKIII.CCCCCC" && role_mask(h, 1) == "..II.IKSSS" &&
                  role_mask(h, 2) == "..II.QQQ",
              role_mask(h, 0) + " " + role_mask(h, 1) + " " + role_mask(h, 2));
    }
    {   // QSRC-127
        Hl h;
        h.load(SourceLanguage::Asm,
               QStringLiteral("  nextreg $07,3\n  ldirx\n  mul d,e\n  pixelad\n  test $80\n  ldirscale\n"
                              "  SECTION code_user\n  defc X = 1\n.db 1\n  DEVICE ZXSPECTRUMNEXT\nx EQU 5"));
        bool z80n = tok(h, 0, QStringLiteral("nextreg")) == 'I' && tok(h, 1, QStringLiteral("ldirx")) == 'I' &&
                    tok(h, 2, QStringLiteral("mul")) == 'I' && tok(h, 3, QStringLiteral("pixelad")) == 'I' &&
                    tok(h, 4, QStringLiteral("test")) == 'I' && tok(h, 5, QStringLiteral("ldirscale")) == 'I';
        bool dirs = tok(h, 6, QStringLiteral("SECTION")) == 'K' && tok(h, 7, QStringLiteral("defc")) == 'K' &&
                    tok(h, 8, QStringLiteral(".db")) == 'K' && tok(h, 9, QStringLiteral("DEVICE")) == 'K' &&
                    tok(h, 10, QStringLiteral("x")) == 'L' && tok(h, 10, QStringLiteral("EQU")) == 'K';
        check("QSRC-127", "the Z80N mnemonics are keywords, the z80asm and sjasmplus directive lines are "
                          "directives, and in `x EQU 5` x is the label and EQU the directive",
              z80n && dirs);
    }
    {   // QSRC-128
        Hl h;
        h.load(SourceLanguage::Asm, QStringLiteral("ld a,1 // c\n/* a\nb\n*/ nop"));
        check("QSRC-128", "sjasmplus comments: // to the end of the line, and /* ... */ across lines, "
                          "after which the code is coloured again",
              tok(h, 0, QStringLiteral("ld")) == 'I' && tok(h, 0, QStringLiteral("// c")) == 'C' &&
                  role_mask(h, 1) == "CCCC" && role_mask(h, 2) == "C" &&
                  tok(h, 3, QStringLiteral("*/")) == 'C' && tok(h, 3, QStringLiteral("nop")) == 'I');
    }
    {   // QSRC-129
        Hl h;
        h.load(SourceLanguage::C,
               QStringLiteral("static unsigned char f(void) __z88dk_fastcall {\n"
                              "#include <arch/zxn.h>\n"
                              "char *s = \"a/*b\";\n"
                              "int x = 0x5800 + 10u + 017;\n"
                              "char c = '\\n';\n"
                              "/* a\n"
                              "   b */\n"
                              "  return RETURN;"));
        const QString l0 = h.doc.findBlockByNumber(0).text();
        const int f = l0.indexOf(QStringLiteral(" f(")) + 1;
        const std::string m0 = role_mask(h, 0);
        const bool kw = tok(h, 0, QStringLiteral("static")) == 'K' && tok(h, 0, QStringLiteral("unsigned")) == 'K' &&
                        tok(h, 0, QStringLiteral("char")) == 'K' && tok(h, 0, QStringLiteral("void")) == 'K' &&
                        tok(h, 0, QStringLiteral("__z88dk_fastcall")) == 'K' && m0[static_cast<size_t>(f)] == '.';
        const bool pre = role_mask(h, 1) == std::string(h.doc.findBlockByNumber(1).text().size(), 'P');
        const bool str = tok(h, 2, QStringLiteral("\"a/*b\"")) == 'S' && !has_role(h, 2, 'C') &&
                         !has_role(h, 3, 'C') && tok(h, 4, QStringLiteral("'\\n'")) == 'Q';
        const bool nums = tok(h, 3, QStringLiteral("0x5800")) == 'N' && tok(h, 3, QStringLiteral("10u")) == 'N' &&
                          tok(h, 3, QStringLiteral("017")) == 'N';
        const bool cmt = role_mask(h, 5) == "CCCC" && role_mask(h, 6) == "CCCCCCC";
        const bool ret = tok(h, 7, QStringLiteral("return")) == 'K' && tok(h, 7, QStringLiteral("RETURN")) == '.';
        check("QSRC-129", "C: keywords including the __ names (the function's own name is not), a # line "
                          "is a preprocessor line, a /* inside a string is no comment, numbers with suffixes, a "
                          "character literal, a /* */ over lines, and keywords are case-sensitive",
              kw && pre && str && nums && cmt && ret,
              std::to_string(kw) + std::to_string(pre) + std::to_string(str) + std::to_string(nums) +
                  std::to_string(cmt) + std::to_string(ret));
    }
    {   // QSRC-146
        Hl h;
        h.load(SourceLanguage::C, QStringLiteral("\"a\\\"b\" int x;\n'\\'' int y;"));
        check("QSRC-146", "C string and character literals honour a backslash escape: the quote of "
                          "\"a\\\"b\" and of '\\'' does not end the literal, so int after each is a keyword",
              tok(h, 0, QStringLiteral("\"a\\\"b\"")) == 'S' && tok(h, 0, QStringLiteral("int")) == 'K' &&
                  tok(h, 1, QStringLiteral("'\\''")) == 'Q' && tok(h, 1, QStringLiteral("int")) == 'K',
              role_mask(h, 0) + " " + role_mask(h, 1));
    }
    {   // QSRC-147
        Hl h;
        h.load(SourceLanguage::Asm, QStringLiteral("ld a,1 /* x */ nop\nld b,2"));
        check("QSRC-147", "an assembly /* ... */ closed on its own line ends there: the code behind "
                          "it is coloured and the next line is not a comment",
              tok(h, 0, QStringLiteral("/* x */")) == 'C' && tok(h, 0, QStringLiteral("nop")) == 'I' &&
                  tok(h, 1, QStringLiteral("ld")) == 'I' && !has_role(h, 1, 'C'),
              role_mask(h, 0) + " " + role_mask(h, 1));
    }
    {   // QSRC-148
        Hl h;
        h.load(SourceLanguage::Asm,
               QStringLiteral(".local: jr .local\n.end: jr nz,.end\n  db word\n  .end\nx EQU 5\n  nop : db 1"));
        check("QSRC-148", "a column-0 word ending in ':' is a label even when it is named like a "
                          "directive, and a word behind the instruction word is never a directive; "
                          "a directive that opens its statement, also behind a ':' statement separator, still is one",
              tok(h, 0, QStringLiteral(".local:")) == 'L' && tok(h, 0, QStringLiteral("jr")) == 'I' &&
                  tok(h, 0, QStringLiteral(".local"), 8) == '.' &&
                  tok(h, 1, QStringLiteral(".end:")) == 'L' && tok(h, 1, QStringLiteral("nz")) == 'I' &&
                  tok(h, 1, QStringLiteral(".end"), 6) == '.' &&
                  tok(h, 2, QStringLiteral("db")) == 'K' && tok(h, 2, QStringLiteral("word")) == '.' &&
                  tok(h, 3, QStringLiteral(".end")) == 'K' && tok(h, 4, QStringLiteral("EQU")) == 'K' &&
                  tok(h, 5, QStringLiteral("nop")) == 'I' && tok(h, 5, QStringLiteral("db")) == 'K',
              role_mask(h, 0) + " " + role_mask(h, 1) + " " + role_mask(h, 2));
    }
    {   // QSRC-149
        Hl h;
        h.load(SourceLanguage::C,
               QStringLiteral("#define X 1 /* long\ncomment */ int z;\n#define Y \"/*\"\nint w;\n"
                              "#if X // note /*\nint a;\n#define D \"x\" /* y */ 1\nint c;"));
        check("QSRC-149", "a /* opened on a # line goes on to the next line (the # line stays a "
                          "preprocessor line), a /* inside a string, after a //, or closed on the line opens nothing",
              role_mask(h, 0) == std::string(h.doc.findBlockByNumber(0).text().size(), 'P') &&
                  tok(h, 1, QStringLiteral("comment */")) == 'C' && tok(h, 1, QStringLiteral("int")) == 'K' &&
                  role_mask(h, 2) == std::string(h.doc.findBlockByNumber(2).text().size(), 'P') &&
                  tok(h, 3, QStringLiteral("int")) == 'K' && !has_role(h, 3, 'C') &&
                  tok(h, 5, QStringLiteral("int")) == 'K' && !has_role(h, 5, 'C') &&
                  tok(h, 7, QStringLiteral("int")) == 'K' && !has_role(h, 7, 'C'),
              role_mask(h, 1) + " " + role_mask(h, 3));
    }
    {   // QSRC-130
        Hl h;
        h.load(SourceLanguage::C,
               QStringLiteral("#asm\n  ld a,1 ; x\n#endasm\nint y;\n__asm\n  ld a,1 ; x\n__endasm;\n"
                              "int z; ld a,1\n"));
        const bool a1 = tok(h, 1, QStringLiteral("ld")) == 'I' && tok(h, 1, QStringLiteral("; x")) == 'C';
        const bool a2 = tok(h, 5, QStringLiteral("ld")) == 'I' && tok(h, 5, QStringLiteral("; x")) == 'C';
        const bool dirs = role_mask(h, 0) == "PPPP" && role_mask(h, 2) == "PPPPPPP";
        const bool after = tok(h, 3, QStringLiteral("int")) == 'K' && tok(h, 7, QStringLiteral("int")) == 'K' &&
                           tok(h, 7, QStringLiteral("ld")) == '.';
        check("QSRC-130", "embedded assembly in C is coloured as assembly between #asm and #endasm and "
                          "between __asm and __endasm, and C rules resume after each",
              a1 && a2 && dirs && after,
              std::to_string(a1) + std::to_string(a2) + std::to_string(dirs) + std::to_string(after));
    }
    {   // QSRC-131
        Hl h;
        h.load(SourceLanguage::Basic,
               QStringLiteral("print \"x\"\nPRINT chr$(65)\nDIM x AS UBYTE\nREM a b\n' a\n"
                              "/' a /' b '/ c '/ print\n/' a /'\n b '/ c '/ print\n"
                              "#include <x.bas>\n10 PRINT\nasm\n  ld a,1 ; x\nend asm\nld a,1\nprint"));
        const bool kw = tok(h, 0, QStringLiteral("print")) == 'K' && tok(h, 1, QStringLiteral("PRINT")) == 'K' &&
                        tok(h, 1, QStringLiteral("chr$")) == 'K' && tok(h, 2, QStringLiteral("DIM")) == 'K' &&
                        tok(h, 2, QStringLiteral("x")) == '.' && tok(h, 2, QStringLiteral("AS")) == 'K' &&
                        tok(h, 2, QStringLiteral("UBYTE")) == 'K';
        const bool rem = role_mask(h, 3) == "CCCCCCC" && role_mask(h, 4) == "CCC";
        const bool nest1 = tok(h, 5, QStringLiteral("/' a /' b '/ c '/")) == 'C' && tok(h, 5, QStringLiteral("print")) == 'K';
        const bool nest2 = role_mask(h, 6) == "CCCCCCC" && tok(h, 7, QStringLiteral(" b '/ c '/")) == 'C' &&
                           tok(h, 7, QStringLiteral("print")) == 'K';
        const bool pre = role_mask(h, 8) == std::string(16, 'P');
        const bool lineno = tok(h, 9, QStringLiteral("10")) == 'L' && tok(h, 9, QStringLiteral("PRINT")) == 'K';
        const bool a = tok(h, 10, QStringLiteral("asm")) == 'K' && tok(h, 11, QStringLiteral("ld")) == 'I' &&
                       tok(h, 11, QStringLiteral("a")) == 'I' && tok(h, 11, QStringLiteral("; x")) == 'C' &&
                       tok(h, 12, QStringLiteral("end")) == 'K' && tok(h, 12, QStringLiteral("asm")) == 'K' &&
                       tok(h, 13, QStringLiteral("ld")) == '.' && tok(h, 14, QStringLiteral("print")) == 'K';
        check("QSRC-131", "Boriel ZX Basic: keywords in either case with the $ names, REM and ' comments, "
                          "/' '/ comments nested and across lines, a # line, a line number, and asm ... "
                          "end asm coloured as assembly",
              kw && rem && nest1 && nest2 && pre && lineno && a,
              std::to_string(kw) + std::to_string(rem) + std::to_string(nest1) + std::to_string(nest2) +
                  std::to_string(pre) + std::to_string(lineno) + std::to_string(a));
    }
    {   // QSRC-132
        const QString samples[] = {
            QStringLiteral("loop: ld a,(hl) ; c\n  db $FF,\"s\",'c'\n  SECTION x\n/* a\nb */"),
            QStringLiteral("static int f(void) {\n#define X 1\n  return 0x10 + 'c'; // c\n}\n/* a\nb */"),
            QStringLiteral("10 PRINT \"s\" ' c\nDIM x AS UBYTE\n/' a '/\nasm\nld a,1\nend asm")};
        const SourceLanguage langs[] = {SourceLanguage::Asm, SourceLanguage::C, SourceLanguage::Basic};
        bool plain_empty = true, colour_only = true, geometry = true;
        for (int i = 0; i < 3; ++i) {
            Hl p;
            p.load(SourceLanguage::Plain, samples[i]);
            for (QTextBlock b = p.doc.begin(); b.isValid(); b = b.next())
                plain_empty = plain_empty && b.layout()->formats().isEmpty();
            for (bool dark : {false, true}) {
                Hl h;
                if (dark) h.hl.apply_palette(dark_test_palette());
                h.load(langs[i], samples[i]);
                int ranges = 0;
                for (QTextBlock b = h.doc.begin(); b.isValid(); b = b.next())
                    for (const QTextLayout::FormatRange& fr : b.layout()->formats()) {
                        ++ranges;
                        const auto props = fr.format.properties();
                        colour_only = colour_only && props.size() == 1 &&
                                      props.contains(QTextFormat::ForegroundBrush);
                    }
                colour_only = colour_only && ranges > 5;
                // The same text as Plain has the same size, and the same line widths.
                if (!dark) {
                    geometry = geometry && h.doc.size() == p.doc.size();
                    QTextBlock b = h.doc.begin(), q = p.doc.begin();
                    for (; b.isValid() && q.isValid(); b = b.next(), q = q.next()) {
                        geometry = geometry && b.layout()->lineCount() == q.layout()->lineCount();
                        for (int k = 0; k < b.layout()->lineCount() && k < q.layout()->lineCount(); ++k)
                            geometry = geometry &&
                                b.layout()->lineAt(k).naturalTextWidth() == q.layout()->lineAt(k).naturalTextWidth() &&
                                b.layout()->lineAt(k).height() == q.layout()->lineAt(k).height();
                    }
                    geometry = geometry && !b.isValid() && !q.isValid();
                }
            }
        }
        check("QSRC-132", "a Plain text has no format at all; every format of every language, on a light "
                          "and a dark palette, sets the foreground and nothing else; and a highlighted "
                          "text has the size and line widths of the same text as Plain",
              plain_empty && colour_only && geometry,
              std::to_string(plain_empty) + std::to_string(colour_only) + std::to_string(geometry));
    }
    {   // QSRC-133
        const QPalette light = QPalette();
        const QPalette dark = dark_test_palette();
        SourceHighlighter hl(nullptr);
        bool light_ok = !palette_tint::is_dark(light), dark_ok = palette_tint::is_dark(dark);
        bool d_light = true, d_dark = true;
        auto distinct = [&](auto get) {
            for (int i = 0; i < SourceHighlighter::kRoles; ++i)
                for (int j = i + 1; j < SourceHighlighter::kRoles; ++j)
                    if (get(i) == get(j)) return false;
            return true;
        };
        hl.apply_palette(light);
        for (int r = 0; r < SourceHighlighter::kRoles; ++r)
            light_ok = light_ok && hl.colour(static_cast<SourceHighlighter::Role>(r)) ==
                                       SourceHighlighter::light_colour(static_cast<SourceHighlighter::Role>(r));
        d_light = distinct([&](int r) { return hl.colour(static_cast<SourceHighlighter::Role>(r)); });
        const bool again_light = hl.apply_palette(light);
        const bool to_dark = hl.apply_palette(dark);
        for (int r = 0; r < SourceHighlighter::kRoles; ++r)
            dark_ok = dark_ok && hl.colour(static_cast<SourceHighlighter::Role>(r)) ==
                                     palette_tint::ink(dark, SourceHighlighter::light_colour(static_cast<SourceHighlighter::Role>(r)));
        d_dark = distinct([&](int r) { return hl.colour(static_cast<SourceHighlighter::Role>(r)); });
        const bool again_dark = hl.apply_palette(dark);
        const bool to_light = hl.apply_palette(light);
        check("QSRC-133", "light colours on a light palette, palette_tint::ink() of them on a dark one, "
                          "the ten pairwise distinct on both, and apply_palette() reports a change "
                          "only on a scheme switch",
              light_ok && dark_ok && d_light && d_dark && !again_light && to_dark && !again_dark && to_light,
              std::to_string(light_ok) + std::to_string(dark_ok) + std::to_string(d_light) +
                  std::to_string(d_dark) + std::to_string(again_light) + std::to_string(to_dark) +
                  std::to_string(again_dark) + std::to_string(to_light));
    }
}

/// The role mask of `line` (1-based) of the Source tab's own document.
std::string panel_mask(SourcePanel* sp, int line) {
    auto* hl = sp->findChild<SourceHighlighter*>();
    auto* ed = sp->findChild<QPlainTextEdit*>();
    return (hl && ed) ? role_mask(*hl, ed->document()->findBlockByNumber(line - 1)) : std::string("<none>");
}

/// The format ranges of `line` as "role@start+length" in document order, one entry per range
/// (Qt merges adjacent equal formats, so a Keyword run like "->" is one range).
std::string range_list(const Hl& h, int line) {
    static const char kChars[SourceHighlighter::kRoles + 1] = ".KIPLSQNFC";
    std::string out;
    for (const QTextLayout::FormatRange& fr : h.doc.findBlockByNumber(line).layout()->formats()) {
        char c = '?';
        for (int r = 0; r < SourceHighlighter::kRoles; ++r)
            if (fr.format.foreground().color() == h.hl.colour(static_cast<SourceHighlighter::Role>(r))) c = kChars[r];
        out += std::string(1, c) + "@" + std::to_string(fr.start) + "+" + std::to_string(fr.length) + " ";
    }
    return out;
}

// ── QSRC-190..194: the owner's classes and punctuation per language (GH #422 stage 2) ──

void test_source_highlight_classes() {
    {   // QSRC-190
        // Oracle: ISO C11 6.4.4.1 (integer constants), 6.4.4.2 (floating constants, incl. the
        // hexadecimal `p` exponent), 6.4.4.4 (character constants), 6.4.5 (string literals);
        // 6.4.4 defines no signed constant, so a leading '-' is an operator.
        Hl h;
        h.load(SourceLanguage::C,
               QStringLiteral("a = 3.75; b = .5; c = 2.f; d = 1e-3; e = 0x1p-3; g = 0x1.8p3;\n"
                              "f = 0x5800; g = 10u; h = 017; i = 0x1E;\n"
                              "j = 'x'; k = \"s\";\n"
                              "x-1;\n-5;"));
        const bool floats = tok(h, 0, QStringLiteral("3.75")) == 'F' && tok(h, 0, QStringLiteral(".5")) == 'F' &&
                            tok(h, 0, QStringLiteral("2.f")) == 'F' && tok(h, 0, QStringLiteral("1e-3")) == 'F' &&
                            tok(h, 0, QStringLiteral("0x1p-3")) == 'F' &&
                            tok(h, 0, QStringLiteral("0x1.8p3")) == 'F' &&
                            // C11 6.4.4.2: a hexadecimal floating constant is ONE span
                            range_list(h, 0).find("F@" + std::to_string(h.doc.findBlockByNumber(0).text().indexOf(QStringLiteral("0x1.8p3"))) + "+7 ") != std::string::npos;
        const bool ints = tok(h, 1, QStringLiteral("0x5800")) == 'N' && tok(h, 1, QStringLiteral("10u")) == 'N' &&
                          tok(h, 1, QStringLiteral("017")) == 'N' && tok(h, 1, QStringLiteral("0x1E")) == 'N';
        const bool lit = tok(h, 2, QStringLiteral("'x'")) == 'Q' && tok(h, 2, QStringLiteral("\"s\"")) == 'S';
        const bool minus = tok(h, 3, QStringLiteral("-")) == 'K' && tok(h, 3, QStringLiteral("1")) == 'N' &&
                           tok(h, 4, QStringLiteral("-")) == 'K' && tok(h, 4, QStringLiteral("5")) == 'N';
        check("QSRC-190", "C numbers and characters: 3.75 .5 2.f 1e-3 0x1p-3 are Float, 0x5800 10u 017 0x1E "
                          "(not a float) are Integer, 'x' is Char, \"s\" String, and a leading '-' is a "
                          "Keyword operator in x-1 and -5",
              floats && ints && lit && minus,
              range_list(h, 0) + "| " + range_list(h, 1) + "| " + range_list(h, 3));
    }
    {   // QSRC-191
        // Oracle: the owner's rulings (2026-10-31): punctuation and operators, "." and
        // multi-character operators included, are Keyword; comments, strings, chars and
        // numbers keep their own characters. C11 6.4.6 lists the punctuators.
        Hl h;
        h.load(SourceLanguage::C,
               QStringLiteral("if (a[i] == b) { f(x, y); }\n"
                              "p->x\n"
                              "a<<=2\n"
                              "a.b f(...)\n"
                              "x ? y : z !a ~a ^ b % c & d | e\n"
                              "\"a,b;(c)\" '(' // a, (b)\n"
                              "/* ( */\n"
                              "#define F(a,b) (a+b)"));
        const std::string m0 = role_mask(h, 0);
        const bool punct0 = m0 == "KK.K.K.K.KK..K.K..K.K..KK.K";
        const bool arrow = tok(h, 1, QStringLiteral("p")) == '.' && range_list(h, 1) == "K@1+2 " &&
                           tok(h, 1, QStringLiteral("x")) == '.';
        const bool shift = range_list(h, 2) == "K@1+3 N@4+1 " && tok(h, 2, QStringLiteral("2")) == 'N';
        const bool dots = tok(h, 3, QStringLiteral(".")) == 'K' && tok(h, 3, QStringLiteral("...")) == 'K';
        const bool ops = tok(h, 4, QStringLiteral("?")) == 'K' && tok(h, 4, QStringLiteral(":")) == 'K' &&
                         tok(h, 4, QStringLiteral("!")) == 'K' && tok(h, 4, QStringLiteral("~")) == 'K' &&
                         tok(h, 4, QStringLiteral("^")) == 'K' && tok(h, 4, QStringLiteral("%")) == 'K' &&
                         tok(h, 4, QStringLiteral("&")) == 'K' && tok(h, 4, QStringLiteral("|")) == 'K';
        const bool wins = tok(h, 5, QStringLiteral("\"a,b;(c)\"")) == 'S' && tok(h, 5, QStringLiteral("'('")) == 'Q' &&
                          tok(h, 5, QStringLiteral("// a, (b)")) == 'C' &&
                          tok(h, 6, QStringLiteral("/* ( */")) == 'C' &&
                          role_mask(h, 7) == std::string(h.doc.findBlockByNumber(7).text().size(), 'P');
        check("QSRC-191", "C punctuation and operators are Keyword (one span for -> and <<=, '.' and '...' too) "
                          "and the other classes win: a comma in a string, a '(' char, a // or /* */ comment "
                          "and a # line keep their own class",
              punct0 && arrow && shift && dots && ops && wins,
              m0 + " | " + range_list(h, 1) + "| " + range_list(h, 2));
    }
    {   // QSRC-192
        // Oracle: the owner's rulings; z80asm and sjasmplus syntax ('.'/'@' lead a word, '$' is the
        // program counter, '%0101' is binary, '%' alone is modulo, a leading '-' is an operator).
        Hl h;
        h.load(SourceLanguage::Asm,
               QStringLiteral("  ld a,(ix+5)\n"
                              "  ld a,-5\n"
                              "  ld hl,lbl*2/3\n"
                              "  and %0101\n"
                              "  ld a,7%3\n"
                              "  cp 'a'\n"
                              "  db \"s\",1\n"
                              "  jp $\n"
                              "  nop : nop\n"
                              "lbl: nop\n"
                              "  jr nz,.end\n"
                              "  ld a,1 ; x, (y)"));
        const bool l0 = role_mask(h, 0) == "..II.IKKIIKNK";
        const bool l1 = tok(h, 1, QStringLiteral("-")) == 'K' && tok(h, 1, QStringLiteral("5")) == 'N';
        const bool l2 = tok(h, 2, QStringLiteral("*")) == 'K' && tok(h, 2, QStringLiteral("/")) == 'K' &&
                        tok(h, 2, QStringLiteral("lbl")) == '.';
        const bool l3 = tok(h, 3, QStringLiteral("%0101")) == 'N';
        const bool l4 = tok(h, 4, QStringLiteral("%")) == 'K' && tok(h, 4, QStringLiteral("7")) == 'N' &&
                        tok(h, 4, QStringLiteral("3")) == 'N';
        const bool l5 = tok(h, 5, QStringLiteral("'a'")) == 'Q';
        const bool l6 = tok(h, 6, QStringLiteral("\"s\"")) == 'S' && tok(h, 6, QStringLiteral(",")) == 'K' &&
                        tok(h, 6, QStringLiteral("1")) == 'N';
        const bool l7 = tok(h, 7, QStringLiteral("$")) == '.';
        const bool l8 = tok(h, 8, QStringLiteral(":")) == 'K';
        const bool l9 = tok(h, 9, QStringLiteral("lbl:")) == 'L';
        const bool l10 = tok(h, 10, QStringLiteral(".end")) == '.';
        const bool l11 = tok(h, 11, QStringLiteral("; x, (y)")) == 'C';
        check("QSRC-192", "assembly: ( ) , + * / : are Keyword, -5 is an operator and an Integer, %0101 is "
                          "Integer and a lone % an operator, 'a' is Char, the label's colon stays Label, "
                          "$ and a .label reference stay Text, and a ; comment swallows its punctuation",
              l0 && l1 && l2 && l3 && l4 && l5 && l6 && l7 && l8 && l9 && l10 && l11,
              role_mask(h, 0) + " " + role_mask(h, 4) + " " + role_mask(h, 8));
    }
    {   // QSRC-193
        // Oracle: boriel-basic/zxbasic src/zxbc/zxblex.py @29f0b2f lines 534-605 (t_HEXA, t_OCTAL,
        // t_BIN, t_NUMBER, t_ID with its $ and % suffix).
        Hl h;
        h.load(SourceLanguage::Basic,
               QStringLiteral("10 LET a = 3.75 + .5e-3 + 1e3 + 2 + $FF + %0101 + 0FFh + 0E3h + 17o + 0xFF + 101b : PRINT a$, n%; CHR$(65)\n"
                              "IF a <> b THEN\n"
                              "PRINT \"a,b\"\n"
                              "REM a,(b)\n"
                              "' a(b)\n"
                              "#include <x.bas>\n"
                              "LET b = 1. + 2"));
        const bool ln = tok(h, 0, QStringLiteral("10")) == 'L';
        const bool fl = tok(h, 0, QStringLiteral("3.75")) == 'F' && tok(h, 0, QStringLiteral(".5e-3")) == 'F' &&
                        tok(h, 0, QStringLiteral("1e3")) == 'F';
        const bool in = tok(h, 0, QStringLiteral("2")) == 'N' &&
                        tok(h, 0, QStringLiteral("$FF")) == 'N' && tok(h, 0, QStringLiteral("%0101")) == 'N' &&
                        tok(h, 0, QStringLiteral("0FFh")) == 'N' && tok(h, 0, QStringLiteral("0E3h")) == 'N' &&
                        tok(h, 0, QStringLiteral("17o")) == 'N' &&
                        // t_HEXA 0x.. and t_BIN [01]+b: one Integer span each
                        tok(h, 0, QStringLiteral("0xFF")) == 'N' && tok(h, 0, QStringLiteral("101b")) == 'N' &&
                        range_list(h, 0).find("N@" + std::to_string(h.doc.findBlockByNumber(0).text().indexOf(QStringLiteral("0xFF"))) + "+4 ") != std::string::npos &&
                        range_list(h, 0).find("N@" + std::to_string(h.doc.findBlockByNumber(0).text().indexOf(QStringLiteral("101b"))) + "+4 ") != std::string::npos;
        const bool pu = tok(h, 0, QStringLiteral("=")) == 'K' && tok(h, 0, QStringLiteral("+")) == 'K' &&
                        tok(h, 0, QStringLiteral(":")) == 'K' && tok(h, 0, QStringLiteral(",")) == 'K' &&
                        tok(h, 0, QStringLiteral(";")) == 'K' && tok(h, 0, QStringLiteral("(")) == 'K' &&
                        tok(h, 0, QStringLiteral(")")) == 'K';
        const bool id = tok(h, 0, QStringLiteral("a$")) == '.' && tok(h, 0, QStringLiteral("n%")) == '.' &&
                        tok(h, 0, QStringLiteral("CHR$")) == 'K';
        const bool ne = range_list(h, 1).find("K@5+2 ") != std::string::npos;
        // zxblex t_NUMBER needs a digit after the '.': "1." is the Integer 1 and a Keyword '.'
        const bool dot = tok(h, 6, QStringLiteral("1")) == 'N' && tok(h, 6, QStringLiteral(".")) == 'K';
        const bool rest = dot && tok(h, 2, QStringLiteral("\"a,b\"")) == 'S' && role_mask(h, 3) == "CCCCCCCCC" &&
                          role_mask(h, 4) == "CCCCCC" &&
                          role_mask(h, 5) == std::string(h.doc.findBlockByNumber(5).text().size(), 'P');
        check("QSRC-193", "Boriel: 3.75 .5e-3 1e3 are Float, 2 $FF %0101 0FFh 0E3h 17o 0xFF 101b Integer (the "
                          "last two one span each), 1. is the Integer 1 and a Keyword dot, punctuation "
                          "and <> are Keyword, a$ and n% are plain identifiers, CHR$ a keyword, the line "
                          "number a Label, and strings, comments and # lines keep their class",
              ln && fl && in && pu && id && ne && rest,
              role_mask(h, 0) + " | " + range_list(h, 1));
    }
    {   // QSRC-194
        // Oracle: sccz80 #asm/#endasm, sdcc __asm/__endasm, Boriel asm/end asm (stage-1 QSRC-130/131)
        // coloured with the owner's classes.
        Hl h;
        h.load(SourceLanguage::C, QStringLiteral("#asm\n  ld (hl),'x'\n#endasm\n__asm\n  ld a,1\n__endasm;"));
        const bool a = role_mask(h, 0) == "PPPP" && role_mask(h, 2) == "PPPPPPP" &&
                       tok(h, 1, QStringLiteral("ld")) == 'I' && tok(h, 1, QStringLiteral("hl")) == 'I' &&
                       tok(h, 1, QStringLiteral("(")) == 'K' && tok(h, 1, QStringLiteral(")")) == 'K' &&
                       tok(h, 1, QStringLiteral(",")) == 'K' && tok(h, 1, QStringLiteral("'x'")) == 'Q';
        const bool b = tok(h, 3, QStringLiteral("__asm")) == 'K' && tok(h, 4, QStringLiteral("ld")) == 'I' &&
                       tok(h, 4, QStringLiteral("a")) == 'I' && tok(h, 5, QStringLiteral("__endasm")) == 'K' &&
                       tok(h, 5, QStringLiteral(";")) == 'K';
        Hl bh;
        bh.load(SourceLanguage::Basic, QStringLiteral("asm\n  ld a,1\nend asm"));
        const bool c = tok(bh, 0, QStringLiteral("asm")) == 'K' && tok(bh, 1, QStringLiteral("ld")) == 'I' &&
                       tok(bh, 1, QStringLiteral("a")) == 'I' && tok(bh, 2, QStringLiteral("end")) == 'K' &&
                       tok(bh, 2, QStringLiteral("asm")) == 'K';
        check("QSRC-194", "embedded assembly takes the owner's classes: #asm/#endasm lines Preprocessor, "
                          "sdcc __asm/__endasm and Boriel asm/end asm Keyword, mnemonics and registers "
                          "Instruction, the punctuation Keyword and 'x' a Char",
              a && b && c, role_mask(h, 1) + " " + role_mask(h, 5));
    }
}

// ── QSRC-195..199: colour schemes (GH #422 stage 2) ──

// WCAG 2.1 relative luminance and contrast ratio (SC 1.4.3 / 1.4.11).
double wcag_lum(const QColor& c) {
    auto lin = [](int v) {
        const double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.red()) + 0.7152 * lin(c.green()) + 0.0722 * lin(c.blue());
}
double contrast(const QColor& a, const QColor& b) {
    const double la = wcag_lum(a), lb = wcag_lum(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
// CIE 1976 L*a*b* (D65) and the CIE76 colour difference.
double delta_e76(const QColor& a, const QColor& b) {
    auto lab = [](const QColor& c, double out[3]) {
        auto lin = [](int v) {
            const double s = v / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        const double r = lin(c.red()), g = lin(c.green()), bl = lin(c.blue());
        const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * bl) / 0.95047;
        const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * bl;
        const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * bl) / 1.08883;
        auto f = [](double t) { return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0; };
        out[0] = 116.0 * f(y) - 16.0;
        out[1] = 500.0 * (f(x) - f(y));
        out[2] = 200.0 * (f(y) - f(z));
    };
    double la[3], lb[3];
    lab(a, la);
    lab(b, lb);
    return std::sqrt((la[0] - lb[0]) * (la[0] - lb[0]) + (la[1] - lb[1]) * (la[1] - lb[1]) +
                     (la[2] - lb[2]) * (la[2] - lb[2]));
}

void test_source_highlight_panel() {
    const std::string tmp = g_tmp->path().toStdString();
    {   // QSRC-134
        bool bas = false, c = false;
        std::string c_mask;
        {
            Fixture fx;
            fx.enable();
            fx.load_map();
            BreakpointModel& m = fx.mgr->breakpoints();
            SrcView v(fx, m);
            bas = v.sp->source_language() == SourceLanguage::Basic &&
                  panel_mask(v.sp, 12).substr(0, 3) == "CCC";
        }
        {
            Fixture fx;
            fx.enable();
            const std::string map = tmp + "/h134/prog.map";
            copy_z88dk("v2.4/sccz80-subdir", "prog.map", map);
            std::string util;
            for (int i = 1; i <= 9; ++i) util += "int v" + std::to_string(i) + "; // line\n";
            std::filesystem::create_directories(tmp + "/h134/game");
            write_text(tmp + "/h134/game/util.c", util);
            fx.backend->load_source_map(map, true);
            Z80Registers r = fx.emu.cpu().get_registers();
            r.PC = 0x8D5C;
            fx.emu.cpu().set_registers(r);
            fx.mgr->refresh_panels();
            SourcePanel* sp = fx.dbg() ? fx.dbg()->source_panel() : nullptr;
            c_mask = sp ? panel_mask(sp, 8) : "<no panel>";
            c = sp && sp->source_language() == SourceLanguage::C && sp->shown_file() == "game/util.c" &&
                c_mask.substr(0, 3) == "KKK";
        }
        check("QSRC-134", "the Source tab colours what it shows: main.bas is Boriel (REM is a comment), "
                          "and the z88dk fixture's game/util.c is C (int is a keyword)",
              bas && c, std::to_string(bas) + std::to_string(c) + " " + c_mask);
    }
    {   // QSRC-135
        Fixture fx;
        fx.enable();
        fx.load_map();
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        bool sel_ok = true, never_role = true, control = true;
        for (bool dark : {false, true}) {
            const QPalette light = QApplication::palette();
            if (dark) QApplication::setPalette(dark_test_palette());
            QApplication::processEvents();
            v.sp->refresh();
            QApplication::processEvents();
            const auto sels = v.ed->extraSelections();
            sel_ok = sel_ok && sels.size() == 1 &&
                     sels[0].format.background() == v.ed->palette().highlight() &&
                     sels[0].format.foreground() == v.ed->palette().highlightedText() &&
                     sels[0].format.boolProperty(QTextFormat::FullWidthSelection);
            QImage img(v.ed->viewport()->size(), QImage::Format_ARGB32);
            img.fill(Qt::magenta);
            v.ed->viewport()->render(&img);
            auto* hl = v.sp->findChild<SourceHighlighter*>();
            auto count_role_pixels = [&](int line) {
                const QRect c = v.cell(line);
                int n = 0;
                for (int y = std::max(c.top(), 0); y <= std::min(c.bottom(), img.height() - 1); ++y)
                    for (int x = 0; x < img.width(); ++x)
                        for (int r = 0; r < SourceHighlighter::kRoles; ++r)
                            if (QColor::fromRgb(img.pixel(x, y)) == hl->colour(static_cast<SourceHighlighter::Role>(r))) ++n;
                return n;
            };
            never_role = never_role && count_role_pixels(10) == 0;
            control = control && count_role_pixels(11) > 0;   // the colours really are drawn elsewhere
            if (dark) {
                QApplication::setPalette(light);
                QApplication::processEvents();
                v.sp->refresh();
            }
        }
        m.add(BreakpointModel::Execute, 0x8005);
        QApplication::processEvents();
        const std::string disc = v.mark(12);
        check("QSRC-135", "the current line keeps the palette's highlight and highlighted-text selection "
                          "and none of the syntax colours is drawn on it (on light and dark), the other "
                          "lines do show them, and a breakpoint on a coloured line still shows its disc",
              sel_ok && never_role && control && disc == "disc",
              std::to_string(sel_ok) + std::to_string(never_role) + std::to_string(control) + " " + disc);
        m.clear_all();
    }
    {   // QSRC-136
        Fixture fx;
        fx.enable();
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);   // no map yet: the tab shows a message, the document is empty
        auto* hl = v.sp->findChild<SourceHighlighter*>();
        if (!hl) { check("QSRC-136", "the Source tab has a highlighter", false); return; }
        const int b0 = hl->blocks_highlighted();
        fx.load_map();
        v.sp->refresh();
        QApplication::processEvents();
        const int blocks = v.ed->document()->blockCount();
        const int load_pass = hl->blocks_highlighted() - b0;
        const int b1 = hl->blocks_highlighted();
        const uint16_t pcs[] = {0x8000, 0x8002, 0x8005, 0x8006};
        for (int i = 0; i < 20; ++i) {
            Z80Registers r = fx.emu.cpu().get_registers();
            r.PC = pcs[i % 4];
            fx.emu.cpu().set_registers(r);
            v.sp->refresh();
        }
        const int ticks = hl->blocks_highlighted() - b1;
        // a changed mtime reloads the file: one more pass over its blocks
        std::string text;
        for (int i = 1; i <= 20; ++i) text += "REM again " + std::to_string(i) + "\n";
        write_text(tmp + "/main.bas", text);
        QFile f(QString::fromStdString(tmp + "/main.bas"));
        const bool opened = f.open(QIODevice::ReadWrite);
        f.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime);
        f.close();
        const int b2 = hl->blocks_highlighted();
        v.sp->refresh();
        const int reload = hl->blocks_highlighted() - b2;
        const int blocks2 = v.ed->document()->blockCount();
        const QPalette light = QApplication::palette();
        const int b3 = hl->blocks_highlighted();
        QApplication::setPalette(dark_test_palette());
        QApplication::processEvents();
        v.sp->refresh();
        const int to_dark = hl->blocks_highlighted() - b3;
        const int b4 = hl->blocks_highlighted();
        v.sp->refresh();
        v.sp->refresh();
        const int dark_ticks = hl->blocks_highlighted() - b4;
        const int b5 = hl->blocks_highlighted();
        QApplication::setPalette(light);
        QApplication::processEvents();
        v.sp->refresh();
        const int to_light = hl->blocks_highlighted() - b5;
        // A load (and a reload) replaces the text: Qt clears it first, which highlights the one
        // empty block left, then inserts the new blocks, so a pass is blockCount + 1.
        check("QSRC-136", "a load highlights each block once, twenty paused refreshes with the PC "
                          "moving highlight nothing, a reload is one more pass, and a scheme switch "
                          "(there and back) is one pass each",
              opened && load_pass == blocks + 1 && ticks == 0 && reload == blocks2 + 1 && to_dark == blocks2 &&
                  dark_ticks == 0 && to_light == blocks2,
              "load=" + std::to_string(load_pass) + "/" + std::to_string(blocks) + " ticks=" +
                  std::to_string(ticks) + " reload=" + std::to_string(reload) + "/" +
                  std::to_string(blocks2) + " dark=" + std::to_string(to_dark) + " dticks=" +
                  std::to_string(dark_ticks) + " light=" + std::to_string(to_light));
    }
    {   // QSRC-139
        Fixture fx;
        fx.enable();
        fx.load_map();
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        auto go = [&](uint16_t pc) {
            Z80Registers r = fx.emu.cpu().get_registers();
            r.PC = pc;
            fx.emu.cpu().set_registers(r);
            v.sp->refresh();
            QApplication::processEvents();
        };
        const bool loaded = v.sp->source_language() == SourceLanguage::Basic;
        go(0xA000);   // no source record here: the tab shows a message
        const bool msg = v.sp->source_language() == SourceLanguage::Plain &&
                         v.ed->document()->toPlainText().isEmpty();
        go(0x8000);
        check("QSRC-139", "a message in the tab leaves no language behind: Plain while it shows, and "
                          "the file is coloured again when it returns",
              loaded && msg && v.sp->source_language() == SourceLanguage::Basic &&
                  panel_mask(v.sp, 12).substr(0, 3) == "CCC");
    }
    {   // QSRC-137
        Fixture fx;
        fx.enable();
        write_text(tmp + "/sub.asm", "  ld a,1\n  ret\n");
        const std::string sld = write_sld("h137.sld",
            "main.bas|10||0|" + std::to_string(fx.p80) + "|32768|T|\n" +
            "sub.asm|1||0|" + std::to_string(fx.p90) + "|36864|T|\n");
        fx.backend->load_source_map(sld, false);
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        auto go = [&](uint16_t pc) {
            Z80Registers r = fx.emu.cpu().get_registers();
            r.PC = pc;
            fx.emu.cpu().set_registers(r);
            v.sp->refresh();
            QApplication::processEvents();
        };
        const bool bas1 = v.sp->source_language() == SourceLanguage::Basic && panel_mask(v.sp, 1).substr(0, 3) == "CCC";
        go(0x9000);
        const bool asm_ok = v.sp->source_language() == SourceLanguage::Asm &&
                            panel_mask(v.sp, 1) == "..II.IKN";
        go(0x8000);
        const std::string back = panel_mask(v.sp, 1);
        const bool bas2 = v.sp->source_language() == SourceLanguage::Basic && back.substr(0, 3) == "CCC" &&
                          back.find('K') == std::string::npos && back.find('I') == std::string::npos;
        check("QSRC-137", "the language follows the file: sub.asm is assembly (ld a,1 coloured), and "
                          "going back to main.bas is Boriel again with no assembly colour left",
              bas1 && asm_ok && bas2, panel_mask(v.sp, 1) + " " + back);
    }
    {   // QSRC-138
        // The listing's m4 text is highlighted as assembly, in one pass, like a disk file.
        // Oracle: z88dk's own listing text (the real fixture prog.lis, found by its text, not
        // by a line number) and the QSRC-125 role rules (directives K, mnemonics and registers I).
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q138";
        const std::string map = root + "/build/prog.map";
        copy_z88dk("master/sdcc-debug", "prog.map", map);
        copy_z88dk("master/sdcc-debug", "prog.lis", root + "/build/prog.lis");
        put_file(root + "/build/z88dk/lib/crt/newlib/crt_start_di.inc", numbered_lines(10, "; di "));
        fx.backend->load_source_map(map, true);
        auto at = [&](uint16_t pc) {
            Z80Registers r = fx.emu.cpu().get_registers();
            r.PC = pc;
            fx.emu.cpu().set_registers(r);
            fx.mgr->refresh_panels();
        };
        SourcePanel* sp = fx.dbg()->source_panel();
        auto* hl = sp->findChild<SourceHighlighter*>();
        auto* editor = sp->findChild<QPlainTextEdit*>();
        const int b0 = hl ? hl->blocks_highlighted() : 0;
        at(0x8001);
        const int pass = hl ? hl->blocks_highlighted() - b0 : -1;
        int ifndef_line = 0, defc_line = 0;
        if (editor)
            for (QTextBlock b = editor->document()->begin(); b.isValid(); b = b.next()) {
                const QString t = b.text().trimmed();
                if (!ifndef_line && t.startsWith(QStringLiteral("IFNDEF startup"))) ifndef_line = b.blockNumber() + 1;
                if (!defc_line && t.startsWith(QStringLiteral("defc startup"))) defc_line = b.blockNumber() + 1;
            }
        auto word_mask = [&](int line, QChar first, int len) {
            if (!line || !editor) return std::string("<none>");
            const int col = editor->document()->findBlockByNumber(line - 1).text().indexOf(first);
            return panel_mask(sp, line).substr(static_cast<size_t>(col), static_cast<size_t>(len));
        };
        const std::string m_ifndef = word_mask(ifndef_line, QLatin1Char('I'), 6);
        const std::string m_defc = word_mask(defc_line, QLatin1Char('d'), 4);
        const std::string m_start = panel_mask(sp, 9462);
        const std::string m_push = panel_mask(sp, 9470);
        const bool single_pass = editor && pass == editor->document()->blockCount() + 1;
        const bool m4_asm = sp->source_language() == SourceLanguage::Asm;
        const bool label = !m_start.empty() && m_start == std::string(m_start.size(), 'L');
        const bool m4_label = sp->location_text().endsWith(QStringLiteral("(m4 output, from prog.lis)"));
        at(0x8000);
        const bool inc_ok = sp->source_language() == SourceLanguage::Asm &&
                            panel_mask(sp, 1) == std::string(panel_mask(sp, 1).size(), 'C') &&
                            !panel_mask(sp, 1).empty();
        check("QSRC-138", "the start-up code's m4 text from the listing is coloured as assembly in one pass: "
                          "IFNDEF and defc are directives (Keyword class), __Start: a label, push iy a mnemonic and a register, "
                          "and crt_start_di.inc after it is assembly with its comment line coloured",
              m4_asm && m_ifndef == "KKKKKK" && m_defc == "KKKK" && label && single_pass &&
                  m_push == "......IIII.II" && m4_label && inc_ok,
              m_ifndef + " | " + m_defc + " | " + m_start + " | " + m_push + " | pass=" + std::to_string(pass));
    }
}

// ── QSRC-195..199 ──

void test_source_highlight_schemes() {
    using R = SourceHighlighter::Role;
    using SC = SourceHighlighter::SchemeColours;
    auto rc = [](const SC& c, R r) { return c.role[static_cast<int>(r)]; };
    {   // QSRC-195
        // Oracle: the owner's colours (issue #422 comment 6548350574) for Borland C++ 3.1; WCAG 2.1
        // SC 1.4.3 / 1.4.11 for the legibility rule; CIE 1976 L*a*b* for the distinctness rule.
        const SC& light = SourceHighlighter::scheme_colours(SourceScheme::Light);
        const SC& dark = SourceHighlighter::scheme_colours(SourceScheme::Dark);
        const SC& bor = SourceHighlighter::scheme_colours(SourceScheme::Borland);
        std::string bad;
        // Literal values.
        auto is = [&](const QColor& c, const char* hex) { return c == QColor(QLatin1String(hex)); };
        if (!(is(bor.background, "#0000A8") && is(bor.text, "#FFFF57") && is(rc(bor, R::Keyword), "#FFFFFF") &&
              is(rc(bor, R::Instruction), "#FFFFFF") && is(rc(bor, R::Preprocessor), "#57FF57") &&
              is(rc(bor, R::Label), "#FFFF57") && is(rc(bor, R::String), "#57FFFF") &&
              is(rc(bor, R::Char), "#57FFFF") && is(rc(bor, R::Integer), "#57FFFF") &&
              is(rc(bor, R::Float), "#57FFFF") && is(rc(bor, R::Comment), "#A8A8A8") &&
              is(bor.pc_background, "#A8A8A8") && is(bor.pc_text, "#0000A8")))
            bad += "borland ";
        if (!(is(light.background, "#FFFFFF") && is(light.text, "#000000") && is(rc(light, R::Keyword), "#0000C0") &&
              is(rc(light, R::Instruction), "#006E6E") && is(rc(light, R::Preprocessor), "#800080") &&
              is(rc(light, R::Label), "#795E26") && is(rc(light, R::String), "#A31515") &&
              is(rc(light, R::Char), "#C2185B") && is(rc(light, R::Integer), "#A05000") &&
              is(rc(light, R::Float), "#3949AB") && is(rc(light, R::Comment), "#007F00") &&
              is(light.pc_background, "#1E5AA8") && is(light.pc_text, "#FFFFFF")))
            bad += "light ";
        // The tints: Light is #388's pale green (what coverage_colors::exec_line() gives a light
        // palette), Dark a dark green, Borland the owner-to-confirm dark teal; both differ from their
        // background by CIE76 12 so the line is visible.
        if (!is(light.coverage_line, "#E0F5E3") || !is(bor.coverage_line, "#004040") ||
            delta_e76(bor.coverage_line, bor.background) < 12.0 ||
            delta_e76(dark.coverage_line, dark.background) < 12.0)
            bad += "tints ";
        // Dark: ink() of the Light colours over a dark palette with Window = Base = #242424 and
        // WindowText = Text = #F0F0F0, written out here independently of the implementation.
        QPalette p;
        p.setColor(QPalette::Window, QColor(0x24, 0x24, 0x24));
        p.setColor(QPalette::WindowText, QColor(0xF0, 0xF0, 0xF0));
        p.setColor(QPalette::Base, QColor(0x24, 0x24, 0x24));
        p.setColor(QPalette::Text, QColor(0xF0, 0xF0, 0xF0));
        for (int r = 0; r < SourceHighlighter::kRoles; ++r)
            if (dark.role[r] != palette_tint::ink(p, light.role[r])) bad += "dark_role" + std::to_string(r) + " ";
        if (!is(dark.coverage_line, "#0A3014")) bad += "dark_tint ";
        if (!(is(dark.background, "#242424") && is(dark.text, "#F0F0F0") && is(dark.pc_background, "#3D72AA") &&
              is(dark.pc_text, "#FFFFFF")))
            bad += "dark_fixed ";
        // The rules, for every scheme, with the shared groups declared per scheme.
        struct Entry { const SC* c; const char* name; std::vector<std::vector<R>> shared; };
        const Entry entries[] = {
            {&light, "light", {}},
            {&dark, "dark", {}},
            {&bor, "borland", {{R::Text, R::Label}, {R::Keyword, R::Instruction},
                               {R::String, R::Char, R::Integer, R::Float}}}};
        for (const Entry& e : entries) {
            const SC& c = *e.c;
            if (c.role[0] != c.text) bad += std::string(e.name) + ":text ";
            for (int r = 0; r < SourceHighlighter::kRoles; ++r)
                if (contrast(c.role[r], c.background) < 4.5) bad += std::string(e.name) + ":R1(" + std::to_string(r) + ") ";
            if (contrast(c.pc_text, c.pc_background) < 4.5) bad += std::string(e.name) + ":R2 ";
            if (contrast(c.pc_background, c.background) < 3.0) bad += std::string(e.name) + ":R3 ";
            // R5: every class colour and Text reach 4.5:1 on the scheme's coverage tint (GH #388 x #422)
            for (int r = 0; r < SourceHighlighter::kRoles; ++r)
                if (contrast(c.role[r], c.coverage_line) < 4.5) bad += std::string(e.name) + ":R5(" + std::to_string(r) + ") ";
            if (c.coverage_line == c.background) bad += std::string(e.name) + ":tint=bg ";
            for (int i = 0; i < SourceHighlighter::kRoles; ++i)
                for (int j = i + 1; j < SourceHighlighter::kRoles; ++j) {
                    bool same = false;
                    for (const auto& g : e.shared) {
                        const bool hi = std::find(g.begin(), g.end(), static_cast<R>(i)) != g.end();
                        const bool hj = std::find(g.begin(), g.end(), static_cast<R>(j)) != g.end();
                        same = same || (hi && hj);
                    }
                    if (same) {
                        if (c.role[i] != c.role[j]) bad += std::string(e.name) + ":shared(" + std::to_string(i) + "," + std::to_string(j) + ") ";
                    } else if (delta_e76(c.role[i], c.role[j]) < 12.0) {
                        bad += std::string(e.name) + ":R4(" + std::to_string(i) + "," + std::to_string(j) + ") ";
                    }
                }
        }
        check("QSRC-195", "Light, Dark (ink() of Light over #242424/#F0F0F0) and Borland C++ 3.1 have the owner's "
                          "colours; every scheme's text and classes reach 4.5:1 on its background and on its "
                          "coverage tint, its bar text 4.5:1 and its bar 3:1, and any two colours differ by CIE76 12 unless declared shared (equal)",
              bad.empty(), bad);
    }
    {   // QSRC-196
        // Oracle: the owner's spec "Auto (the default: Light or Dark, following the application theme)".
        SourceHighlighter fresh(nullptr);
        const bool dflt = fresh.scheme() == SourceScheme::Auto;
        Hl h;
        h.hl.apply_palette(QPalette());
        const bool light = h.hl.scheme_in_force() == SourceScheme::Light;
        h.hl.apply_palette(dark_test_palette());
        const bool dark = h.hl.scheme_in_force() == SourceScheme::Dark;
        QPalette other;
        for (auto g : {QPalette::Active, QPalette::Inactive}) {
            other.setColor(g, QPalette::Window, QColor(0x2B, 0x2B, 0x2B));
            other.setColor(g, QPalette::WindowText, QColor(0xDD, 0xDD, 0xDD));
            other.setColor(g, QPalette::Base, QColor(0x1A, 0x1A, 0x1A));
            other.setColor(g, QPalette::Text, QColor(0xDD, 0xDD, 0xDD));
        }
        SourceHighlighter o(nullptr);
        o.apply_palette(other);
        const SC& dk = SourceHighlighter::scheme_colours(SourceScheme::Dark);
        const bool exact = o.scheme_in_force() == SourceScheme::Dark &&
                           o.colour(R::Keyword) == rc(dk, R::Keyword) &&
                           o.colour(R::Keyword) != palette_tint::ink(other, SourceHighlighter::light_colour(R::Keyword));
        SourceHighlighter fx(nullptr);
        fx.set_scheme(SourceScheme::Light);
        fx.apply_palette(dark_test_palette());
        const bool light_on_dark = fx.scheme_in_force() == SourceScheme::Light;
        SourceHighlighter bx(nullptr);
        bx.set_scheme(SourceScheme::Borland);
        bx.apply_palette(QPalette());
        const bool b1 = bx.scheme_in_force() == SourceScheme::Borland;
        bx.apply_palette(dark_test_palette());
        const bool b2 = bx.scheme_in_force() == SourceScheme::Borland;
        Hl c;
        c.load(SourceLanguage::Asm, QStringLiteral("  ld a,1\n  nop"));
        const int n0 = c.hl.blocks_highlighted();
        c.hl.set_scheme(SourceScheme::Borland);
        const bool lazy = c.hl.blocks_highlighted() == n0;
        const bool changed = c.hl.apply_palette(QPalette());
        const bool passed = c.hl.blocks_highlighted() > n0;
        const bool repeat = c.hl.apply_palette(QPalette());
        check("QSRC-196", "Auto is Light on a light palette and Dark on a dark one, and Dark is the scheme's own "
                          "colours whatever the dark palette; Light and Borland stay what they are on either; "
                          "set_scheme() alone highlights nothing and apply_palette() reports a change once",
              dflt && light && dark && exact && light_on_dark && b1 && b2 && lazy && changed && passed && !repeat,
              std::to_string(dflt) + std::to_string(light) + std::to_string(dark) + std::to_string(exact) +
                  std::to_string(light_on_dark) + std::to_string(b1) + std::to_string(b2) + std::to_string(lazy) +
                  std::to_string(changed) + std::to_string(passed) + std::to_string(repeat));
    }
    {   // QSRC-197
        // Oracle: G3 of the plan; the editor's palette carries the scheme (QPlainTextEdit paints Base,
        // Text, Highlight and HighlightedText from its own palette).
        Fixture fx;
        fx.enable();
        fx.load_map();
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        auto* hl = v.sp->findChild<SourceHighlighter*>();
        auto is = [&](const QColor& c, const char* hex) { return c == QColor(QLatin1String(hex)); };
        const QPalette ep0 = v.ed->palette();
        const bool start = is(ep0.color(QPalette::Base), "#FFFFFF") && is(ep0.color(QPalette::Text), "#000000") &&
                           is(ep0.color(QPalette::Highlight), "#1E5AA8") &&
                           is(ep0.color(QPalette::HighlightedText), "#FFFFFF");
        const int b0 = hl->blocks_highlighted();
        v.sp->set_colour_scheme(SourceScheme::Borland);   // no refresh()
        const int pass = hl->blocks_highlighted() - b0;
        const QPalette ep = v.ed->palette();
        const bool applied = is(ep.color(QPalette::Base), "#0000A8") && is(ep.color(QPalette::Text), "#FFFF57") &&
                             is(ep.color(QPalette::Highlight), "#A8A8A8") &&
                             is(ep.color(QPalette::HighlightedText), "#0000A8");
        // the four roles in EVERY colour group: a window that is not the active one paints Inactive
        bool groups = true;
        for (auto g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
            groups = groups && is(ep.color(g, QPalette::Base), "#0000A8") && is(ep.color(g, QPalette::Text), "#FFFF57") &&
                     is(ep.color(g, QPalette::Highlight), "#A8A8A8") &&
                     is(ep.color(g, QPalette::HighlightedText), "#0000A8");
        v.sp->refresh();
        QApplication::processEvents();
        const auto sels = v.ed->extraSelections();
        const bool bar = sels.size() == 1 && is(sels[0].format.background().color(), "#A8A8A8") &&
                         is(sels[0].format.foreground().color(), "#0000A8") &&
                         sels[0].format.boolProperty(QTextFormat::FullWidthSelection);
        QImage img(v.ed->viewport()->size(), QImage::Format_ARGB32);
        img.fill(Qt::magenta);
        v.ed->viewport()->render(&img);
        const QRgb px = img.pixel(img.width() - 3, v.row_y(11));
        const bool background = QColor::fromRgb(px) == QColor(0x00, 0x00, 0xA8);
        const int b1 = hl->blocks_highlighted();
        v.sp->set_colour_scheme(SourceScheme::Borland);
        v.sp->refresh();
        const int again = hl->blocks_highlighted() - b1;
        const bool same_palette = v.ed->palette() == ep;
        const bool panel_untouched = v.sp->palette().color(QPalette::Base) != QColor(0x00, 0x00, 0xA8);
        v.sp->set_colour_scheme(SourceScheme::Auto);
        const bool restored = hl->colour(R::Keyword) == SourceHighlighter::light_colour(R::Keyword) &&
                              is(v.ed->palette().color(QPalette::Base), "#FFFFFF");
        check("QSRC-197", "set_colour_scheme(Borland) puts the scheme on the editor's palette at once, without a "
                          "refresh and in one highlight pass; the current-line bar and the empty viewport then "
                          "show it, a repeat changes nothing, the panel's own palette is not touched, and Auto "
                          "brings the Light colours back",
              start && applied && groups && pass == v.ed->document()->blockCount() && bar && background && again == 0 &&
                  same_palette && panel_untouched && restored,
              std::to_string(start) + std::to_string(applied) + std::to_string(bar) + std::to_string(background) +
                  std::to_string(same_palette) + std::to_string(panel_untouched) + std::to_string(restored) +
                  " pass=" + std::to_string(pass) + "/" + std::to_string(v.ed->document()->blockCount()) +
                  " again=" + std::to_string(again));
    }
    {   // QSRC-198
        // Oracle: issue #422 ("never hides the current-line marker or breakpoint marks"); stage-1 QSRC-135.
        Fixture fx;
        fx.enable();
        fx.load_map();
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        m.add(BreakpointModel::Execute, 0x8005);
        QApplication::processEvents();
        auto* hl = v.sp->findChild<SourceHighlighter*>();
        std::string bad;
        const QPalette app0 = QApplication::palette();
        for (bool dark_app : {false, true}) {
            if (dark_app) QApplication::setPalette(dark_test_palette());
            QApplication::processEvents();
            QImage first_gutter, first_header;
            for (SourceScheme sch : {SourceScheme::Light, SourceScheme::Dark, SourceScheme::Borland, SourceScheme::Auto}) {
                v.sp->set_colour_scheme(sch);
                v.sp->refresh();
                QApplication::processEvents();
                const std::string tag = std::string(dark_app ? "dark" : "light") + "/" + std::to_string(static_cast<int>(sch)) + " ";
                const SC& c = hl->colours_in_force();
                QImage img(v.ed->viewport()->size(), QImage::Format_ARGB32);
                img.fill(Qt::magenta);
                v.ed->viewport()->render(&img);
                const QRect cell = v.cell(10);
                bool edge = false, text = false, role_leak = false;
                for (int y = std::max(cell.top(), 0); y <= std::min(cell.bottom(), img.height() - 1); ++y) {
                    // the bar runs to within a few pixels of the viewport's right edge
                    if (QColor::fromRgb(img.pixel(img.width() - 4, y)) == c.pc_background) edge = true;
                    for (int x = 0; x < img.width(); ++x) {
                        const QColor px = QColor::fromRgb(img.pixel(x, y));
                        if (std::abs(px.red() - c.pc_text.red()) <= 8 && std::abs(px.green() - c.pc_text.green()) <= 8 &&
                            std::abs(px.blue() - c.pc_text.blue()) <= 8)
                            text = true;
                        for (int r = 0; r < SourceHighlighter::kRoles; ++r)
                            if (c.role[r] != c.pc_background && c.role[r] != c.pc_text && px == c.role[r]) role_leak = true;
                    }
                }
                if (!edge) bad += tag + "no-full-width ";
                if (!text) bad += tag + "no-text ";
                if (role_leak) bad += tag + "role-colour-on-bar ";
                if (v.mark(12) != "disc") bad += tag + "mark=" + v.mark(12) + " ";
                const QImage g = v.gutter_image();
                if (first_gutter.isNull()) first_gutter = g;
                else if (g != first_gutter) bad += tag + "gutter-differs ";
                QImage hd(v.header->size(), QImage::Format_ARGB32);
                hd.fill(Qt::magenta);
                v.header->render(&hd);
                if (first_header.isNull()) first_header = hd;
                else if (hd != first_header) bad += tag + "header-differs ";
            }
            if (dark_app) {
                QApplication::setPalette(app0);
                QApplication::processEvents();
            }
        }
        m.clear_all();
        check("QSRC-198", "in Light, Dark, Borland and Auto, on a light and on a dark application palette, the "
                          "current line keeps a full-width bar in the scheme's colours with its text drawn and no "
                          "class colour on it, the breakpoint disc shows, and the gutter and its header are pixel-identical "
                          "across the four schemes",
              bad.empty(), bad);
    }
    {   // QSRC-199
        // Oracle: stage-1 QSRC-132 (colour-only, geometry-neutral formats), now under Borland.
        const QString samples[] = {
            QStringLiteral("loop: ld a,(hl) ; c\n  db $FF,\"s\",'c'\n  SECTION x\n/* a\nb */"),
            QStringLiteral("static int f(void) {\n#define X 1\n  return 0x10 + 'c'; // c\n}\n/* a\nb */"),
            QStringLiteral("10 PRINT \"s\" ' c\nDIM x AS UBYTE\n/' a '/\nasm\nld a,1\nend asm")};
        const SourceLanguage langs[] = {SourceLanguage::Asm, SourceLanguage::C, SourceLanguage::Basic};
        bool colour_only = true, geometry = true;
        for (int i = 0; i < 3; ++i) {
            Hl p;
            p.load(SourceLanguage::Plain, samples[i]);
            Hl h;
            h.hl.set_scheme(SourceScheme::Borland);
            h.hl.apply_palette(QPalette());
            h.load(langs[i], samples[i]);
            int ranges = 0;
            for (QTextBlock b = h.doc.begin(); b.isValid(); b = b.next())
                for (const QTextLayout::FormatRange& fr : b.layout()->formats()) {
                    ++ranges;
                    const auto props = fr.format.properties();
                    colour_only = colour_only && props.size() == 1 && props.contains(QTextFormat::ForegroundBrush);
                }
            colour_only = colour_only && ranges > 5;
            geometry = geometry && h.doc.size() == p.doc.size();
            QTextBlock b = h.doc.begin(), q = p.doc.begin();
            for (; b.isValid() && q.isValid(); b = b.next(), q = q.next()) {
                geometry = geometry && b.layout()->lineCount() == q.layout()->lineCount();
                for (int k = 0; k < b.layout()->lineCount() && k < q.layout()->lineCount(); ++k)
                    geometry = geometry &&
                        b.layout()->lineAt(k).naturalTextWidth() == q.layout()->lineAt(k).naturalTextWidth() &&
                        b.layout()->lineAt(k).height() == q.layout()->lineAt(k).height();
            }
            geometry = geometry && !b.isValid() && !q.isValid();
        }
        check("QSRC-199", "under Borland every format of every language is one foreground colour and nothing "
                          "else (the background is the editor's palette), and the text has the geometry of Plain",
              colour_only && geometry);
    }
    {   // QSRC-200
        // Oracle: GH #388 (an executed line has a whole-line tint, the current line wins) x GH #422
        // (the scheme owns the colours): on a LIGHT application palette Dark and Borland must tint
        // with their own colour, never the pale green that is unreadable under their text; and on a
        // DARK application palette the pale green would be invisible in Light and unreadable in the rest.
        Fixture fx;
        fx.enable();
        fx.load_map();
        SourcePanel* sp = fx.dbg() ? fx.dbg()->source_panel() : nullptr;
        auto* ed = sp ? sp->findChild<QPlainTextEdit*>() : nullptr;
        auto* hl = sp ? sp->findChild<SourceHighlighter*>() : nullptr;
        QPushButton* into = sp ? button(sp, QStringLiteral("Into")) : nullptr;
        QPushButton* over = sp ? button(sp, QStringLiteral("Over")) : nullptr;
        std::string bad;
        if (!sp || !ed || !hl || !into || !over) bad = "no panel ";
        else {
            fx.mgr->refresh_panels();
            fx.backend->mem_coverage_enable(true);
            fx.mgr->refresh_panels();
            into->click();
            over->click();
            QApplication::processEvents();
            fx.mgr->refresh_panels();
            const QColor pale(0xE0, 0xF5, 0xE3);
            BreakpointModel& bm = fx.mgr->breakpoints();
            SrcView sv(fx, bm);
            SourcePanel* vp = sv.sp;
            QPlainTextEdit* ved = sv.ed;
            const QPalette app0 = QApplication::palette();
            for (bool dark_app : {false, true}) {
              if (dark_app) QApplication::setPalette(dark_test_palette());
              QApplication::processEvents();
              for (SourceScheme sch : {SourceScheme::Light, SourceScheme::Dark, SourceScheme::Borland, SourceScheme::Auto}) {
                sp->set_colour_scheme(sch);
                fx.mgr->refresh_panels();
                QApplication::processEvents();
                const std::string tag = std::string(dark_app ? "dark-app/" : "light-app/") + std::to_string(static_cast<int>(sch)) + " ";
                const SC& c = hl->colours_in_force();
                const auto sels = ed->extraSelections();
                bool tinted = sels.size() >= 2;
                const bool current_last = !sels.isEmpty() && sels.back().format.background().color() == c.pc_background;
                for (int i = 0; i + 1 < sels.size(); ++i)
                    tinted = tinted && sels[i].format.background().color() == c.coverage_line;
                if (!tinted) bad += tag + "selection-not-scheme-tint ";
                if (!current_last) bad += tag + "current-line-not-last ";
                if ((sch == SourceScheme::Dark || sch == SourceScheme::Borland) && c.coverage_line == pale)
                    bad += tag + "pale-green ";
                // Light is #388's pale green whatever the application palette is (white on white
                // would hide it if the tint followed the application)
                if (sch == SourceScheme::Light && c.coverage_line != pale) bad += tag + "light-not-pale ";
                // The same backend (coverage and PC state) painted in a shown panel: an executed
                // line (10) carries the tint near the right edge, the current line (12) the bar.
                vp->set_colour_scheme(sch);
                vp->refresh();
                QApplication::processEvents();
                QImage img(ved->viewport()->size(), QImage::Format_ARGB32);
                img.fill(Qt::magenta);
                ved->viewport()->render(&img);
                const int y10 = ved->cursorRect(QTextCursor(ved->document()->findBlockByNumber(9))).center().y();
                const int y12 = ved->cursorRect(QTextCursor(ved->document()->findBlockByNumber(11))).center().y();
                if (QColor::fromRgb(img.pixel(img.width() - 4, y10)) != c.coverage_line) bad += tag + "pixel-line10 ";
                if (QColor::fromRgb(img.pixel(img.width() - 4, y12)) != c.pc_background) bad += tag + "pixel-bar ";
              }
              if (dark_app) {
                QApplication::setPalette(app0);
                QApplication::processEvents();
              }
            }
        }
        check("QSRC-200", "with coverage on, an executed line is tinted with the SCHEME's colour (Light the pale "
                          "green, Dark and Borland their own, never the pale green), on a light AND on a dark "
                          "application palette, in the selections and in the pixels (line 10 the tint, line 12 "
                          "the bar), and the current line's bar stays last and on top",
              bad.empty(), bad);
    }
}

// ── QCN-05: the Source tab's word menu offers a z88dk C symbol under its C name (GH #424 stage 2) ──
// Oracle: the owner's parity rule (the Source tab offers what the Disassembly offers, which shows
// `counter` for `_counter`) and zcc's MODULE naming (main_c); a map in the real line format.
void test_z88dk_c_names_source() {
    Fixture fx;
    fx.enable();
    fx.load_map();
    const std::string map = g_tmp->path().toStdString() + "/qcn5.map";
    write_text(map,
               "_counter                        = $9000 ; addr, public, , main_c, bss_compiler, main.c:2\n"
               "_main                           = $8005 ; addr, public, , main_c, code_compiler, main.c:12\n"
               "_asm_bump                       = $9100 ; addr, public, , asmfn_asm, code_user, asmfn.asm:5\n");
    fx.backend->load_map(map, jnext::dbg::MapFormat::Z88dk);
    write_main({{10, "    counter = 0;"}, {11, "    ld hl,_counter"}, {12, "int main(void)"}, {13, "    ld hl,_asm_bump"}});
    BreakpointModel& m = fx.mgr->breakpoints();
    SrcView v(fx, m);
    v.sp->set_watch_panel(fx.dbg()->watch_panel());
    const auto menu_of = [&](int line, const char* needle) {
        auto menu = std::make_unique<QMenu>();
        const QString text = v.ed->document()->findBlockByNumber(line - 1).text();
        v.sp->fill_context_menu(*menu, line, col_in(text, needle));
        return menu;
    };
    WatchPanel* wp = fx.dbg()->watch_panel();
    const auto c_word = menu_of(10, "counter");       // the C word, as in main.c
    const auto asm_word = menu_of(11, "_counter");    // the assembler word, as in asmfn.asm
    const auto main_word = menu_of(12, "main");       // the function's own line: its label is main too
    const auto asm_fn = menu_of(13, "_asm_bump");     // an asm symbol keeps its name
    const int w0 = wp->watch_count();
    QAction* watch = menu_action(*asm_word, "Watch 'counter' ($9000)");
    if (watch) watch->trigger();
    check("QCN-05", "the Source tab offers a z88dk C symbol under its C name: `counter` and `_counter` both give "
                    "Watch/Break on Read/Write 'counter' ($9000) and the watch is labelled counter; `main` on its own "
                    "line gives one 'main' entry; an asm symbol keeps `_asm_bump`",
          menu_texts(*c_word).contains("Watch 'counter' ($9000)") &&
              menu_texts(*c_word).contains("Break on Read 'counter' ($9000)") &&
              menu_texts(*asm_word).contains("Break on Write 'counter' ($9000)") &&
              !menu_texts(*asm_word).contains("Watch '_counter' ($9000)") && watch &&
              wp->watch_count() == w0 + 1 && cell(wp, w0, 1) == "counter" &&
              menu_texts(*main_word).count("Watch 'main' ($8005)") == 1 &&
              menu_texts(*asm_fn).contains("Watch '_asm_bump' ($9100)"),
          s(menu_texts(*c_word).join("|")) + " / " + s(menu_texts(*asm_word).join("|")) + " / " +
              s(menu_texts(*main_word).join("|")));
    m.clear_all();
}

// ===========================================================================
// GH #347 stage 1 - the current-line marker (an amber wedge in the gutter's
// trailing strip) and the "also:" list of the other places of an address (OD-7).
//
// Oracle: the issue's text and the plan's decisions D1-D3; SourceMap::lookup_all()
// order (SRCRNG-04/05). jnext-internal GUI, no VHDL counterpart. The wedge is
// read from the gutter's PIXELS: the strip is x in [width-6, width-2], clear of
// the number cell (QSRC-79) and of the BP column's samples (mark()).
// ===========================================================================

constexpr QRgb kAmber = 0xFFE09A00;   // #E09A00 (the plan's D1 colour), opaque

/// The wedge's pixels at `line`'s row, strip x in [w-6, w-2]: the number of those
/// pixels equal to `colour`, and whether any pixel of the strip there is not the
/// gutter's background `bg`.
struct StripRead { int coloured = 0; bool any = false; };
StripRead strip_of(const SrcView& v, const QImage& img, int line, QRgb colour, QRgb bg) {
    StripRead r;
    const QRect c = v.cell(line);
    const int w = v.gutter->width();
    for (int y = c.top(); y <= c.bottom(); ++y)
        for (int x = w - 6; x <= w - 2; ++x) {
            if (img.pixel(x, y) == colour) ++r.coloured;
            if (img.pixel(x, y) != bg) r.any = true;
        }
    return r;
}

void set_pc(Fixture& fx, uint16_t pc) {
    Z80Registers r = fx.emu.cpu().get_registers();
    r.PC = pc;
    fx.emu.cpu().set_registers(r);
}

void test_source_current_line() {
    {   // QSRC-170, 171, 172
        Fixture fx;
        fx.enable();
        fx.load_map();
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        if (!v.ed || !v.gutter) { check("QSRC-170", "the Source tab has a gutter", false); return; }
        const QRgb white = 0xFFFFFFFF;
        QImage img = v.gutter_image();
        const StripRead on10 = strip_of(v, img, 10, kAmber, white);
        bool others_clear = true;
        for (int line = 1; line <= 13; ++line)
            if (line != 10 && strip_of(v, img, line, kAmber, white).any) others_clear = false;
        // the wedge's tip is on the row's centre line, 5 px wide: the first two columns are filled
        const int cy = v.row_y(10), w = v.gutter->width();
        const bool shape = img.pixel(w - 6, cy) == kAmber && img.pixel(w - 2, cy) == kAmber &&
                           img.pixel(w - 6, cy - 5) == kAmber && img.pixel(w - 6, cy + 5) == kAmber &&
                           img.pixel(w - 2, cy - 2) != kAmber && img.pixel(w - 6, cy - 6) != kAmber;
        check("QSRC-170", "at a stop on line 10 the gutter's trailing strip carries an amber wedge on "
                          "line 10 and on no other line; the number cell and the BP column keep their "
                          "pixels (the number reads 10, no mark)",
              on10.coloured == 35 && shape && others_clear && v.number_is(img, 10, "10") &&
                  v.mark(10) == "none",
              "amber=" + std::to_string(on10.coloured) + " shape=" + std::to_string(shape) +
                  " others_clear=" + std::to_string(others_clear));

        set_pc(fx, 0x8002);   // what a source Step Into to line 11 leaves
        v.sp->refresh();
        QApplication::processEvents();
        img = v.gutter_image();
        const bool moved = strip_of(v, img, 11, kAmber, white).coloured == 35 &&
                           !strip_of(v, img, 10, kAmber, white).any && v.sp->highlighted_line() == 11;
        fx.backend->run(jnext::dbg::CLIENT_NONE);
        v.sp->refresh();
        QApplication::processEvents();
        img = v.gutter_image();
        bool running_clear = true;
        for (int line = 1; line <= 13; ++line)
            if (strip_of(v, img, line, kAmber, white).any) running_clear = false;
        fx.emu.debug_state().pause();
        v.sp->refresh();
        QApplication::processEvents();
        img = v.gutter_image();
        const bool back = strip_of(v, img, 11, kAmber, white).coloured == 35;
        m.add_source_line(QStringLiteral("main.bas:11"));
        QApplication::processEvents();
        img = v.gutter_image();
        const bool both = v.mark(11) == "disc" && strip_of(v, img, 11, kAmber, white).coloured == 35;
        m.clear_all();
        check("QSRC-171", "the wedge follows the PC (line 11 after a step: line 10's strip is clear), "
                          "is gone while the machine runs, returns on the pause, and shares line 11 with "
                          "a breakpoint's disc",
              moved && running_clear && back && both,
              "moved=" + std::to_string(moved) + " running_clear=" + std::to_string(running_clear) +
                  " back=" + std::to_string(back) + " both=" + std::to_string(both));

        const QPalette light = QApplication::palette();
        QApplication::setPalette(dark_test_palette());
        QApplication::processEvents();
        const QImage dark = v.gutter_image();
        const QRgb want = palette_tint::ink(dark_test_palette(), QColor(0xE0, 0x9A, 0x00)).rgb();
        const QRgb dark_bg = dark.pixel(v.gutter->width() - 3, 1);
        const int dark_on = strip_of(v, dark, 11, want, dark_bg).coloured;
        QApplication::setPalette(light);
        QApplication::processEvents();
        set_pc(fx, 0x8002);
        v.sp->refresh();   // a message instead of a line: the map is gone
        fx.backend->clear_source_map();
        v.sp->refresh();
        QApplication::processEvents();
        const QImage msg = v.gutter_image();
        bool msg_clear = true;
        for (int y = 0; y < msg.height(); ++y)
            for (int x = msg.width() - 6; x <= msg.width() - 2; ++x)
                if (msg.pixel(x, y) != white) msg_clear = false;
        check("QSRC-172", "on the dark palette the wedge is the palette's mirror of the amber, distinct "
                          "from the dark gutter; with the map cleared (a message shown) no wedge is drawn",
              dark_on == 35 && want != 0xFFE09A00 && want != dark_bg && msg_clear,
              "dark_on=" + std::to_string(dark_on) + " msg_clear=" + std::to_string(msg_clear));
    }
    {   // QSRC-173: the PC line is past the end of the text found: no highlight, no wedge
        Fixture fx;
        fx.enable();
        fx.load_map();
        std::string short_main;
        for (int i = 1; i <= 5; ++i) short_main += "REM line " + std::to_string(i) + "\n";
        write_text(g_tmp->path().toStdString() + "/main.bas", short_main);
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        const QImage img = v.gutter_image();
        bool clear = true;
        for (int line = 1; line <= 5; ++line)
            if (strip_of(v, img, line, kAmber, 0xFFFFFFFF).any) clear = false;
        check("QSRC-173", "a PC line past the end of the file found (line 10 of a 5-line text) has no "
                          "highlight and no wedge on any line",
              v.sp->highlighted_line() == 0 && clear && v.sp->location_text().contains(QStringLiteral("has only 5 lines")),
              "hl=" + std::to_string(v.sp->highlighted_line()) + " clear=" + std::to_string(clear));
        write_main({});
    }
    {   // QSRC-174, 175: the "also" list
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80);
        fx.backend->load_source_map(write_sld("also-one.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "util.bas|7||0|" + a + "|32770|T|\n" +
            "main.bas|11||0|" + a + "|32770|T|\n" + "main.bas|12||0|" + a + "|32773|T|\n"), false);
        write_main({});
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        set_pc(fx, 0x8002);
        v.sp->refresh();
        const QString page = QString::asprintf("%02X", ram_page_of(fx.backend->source_page(0x8002)));
        const QString at8002 = v.sp->location_text();
        set_pc(fx, 0x8000);
        v.sp->refresh();
        const QString at8000 = v.sp->location_text();
        // a map still pending verification: the note is its own line AFTER the position's
        write_text(g_tmp->path().toStdString() + "/also-pending.sld",
            std::string("|SLD.data.version|1\n||program.name:prog\n") +
            "||program.sha256:04bb9c5fbc1b0517a33504a5fbb7b9de7dd72a1de84987241364d2674495a823\n" +
            "||program.org:32768\n||program.size:4\n" + "main.bas|10||0" + kNextDevice +
            "main.bas|10||0|" + a + "|32768|T|\n" + "util.bas|7||0|" + a + "|32770|T|\n" +
            "main.bas|11||0|" + a + "|32770|T|\n" + "main.bas|12||0|" + a + "|32773|T|\n");
        fx.backend->load_source_map(g_tmp->path().toStdString() + "/also-pending.sld", false, true);
        set_pc(fx, 0x8002);
        v.sp->refresh();
        const QStringList pend = v.sp->location_text().split(QLatin1Char('\n'));
        const bool order = pend.size() == 2 &&
                           pend[0] == QStringLiteral("main.bas:11  $8002  page %1  also: util.bas:7").arg(page) &&
                           pend[1].startsWith(QStringLiteral("Source map not verified yet"));
        check("QSRC-174", "two places at $8002 (util.bas:7 emitted first, main.bas:11 later and primary): "
                          "the label names the primary and lists the other as 'also: util.bas:7'; "
                          "a one-place address carries no 'also'; under a pending map 'also' ends the "
                          "position line and the note follows on the next line",
              at8002 == QStringLiteral("main.bas:11  $8002  page %1  also: util.bas:7").arg(page) &&
                  !at8000.contains(QStringLiteral("also")) && order,
              s(at8002) + " | " + s(at8000) + " | " + s(v.sp->location_text()));
    }
    {
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80);
        fx.backend->load_source_map(write_sld("also-many.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "c.bas|3||0|" + a + "|32770|T|\n" +
            "b.bas|2||0|" + a + "|32770|T|\n" + "main.bas|11:5||0|" + a + "|32770|T|\n" +
            "b.bas|2||0|" + a + "|32770|T|\n" + "main.bas|20||0|" + a + "|32770|T|\n" +
            "util.bas|11||0|" + a + "|32770|T|\n" + "b.bas|5||0|" + a + "|32770|T|\n" +
            "main.bas|11||0|" + a + "|32770|T|\n" +
            "main.bas|12||0|" + a + "|32773|T|\n"), false);
        write_main({});
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        set_pc(fx, 0x8002);
        v.sp->refresh();
        // the expectation is derived from lookup_all() itself: its order, minus the
        // primary's file:line and repeats
        const auto all = fx.backend->source_map().lookup_all(fx.backend->source_page(0x8002), 0x8002);
        std::vector<std::string> want;
        for (size_t i = 1; i < all.size(); ++i) {
            const std::string k = all[i]->file + ":" + std::to_string(all[i]->line);
            if (k == all[0]->file + ":" + std::to_string(all[0]->line)) continue;
            if (std::find(want.begin(), want.end(), k) == want.end()) want.push_back(k);
        }
        std::string joined;
        for (const auto& k : want) joined += (joined.empty() ? "" : ", ") + k;
        const QString text = v.sp->location_text();
        const int at = text.indexOf(QStringLiteral("also: "));
        const QString listed = at < 0 ? QString() : text.mid(at + 6);
        // the dedup key is the PAIR file:line: the same file on another line, another file on the
        // primary's line number, and one other file on two lines are all listed
        const bool pair_key = text.count(QStringLiteral("main.bas:20")) == 1 &&
                              text.count(QStringLiteral("util.bas:11")) == 1 &&
                              text.count(QStringLiteral("b.bas:5")) == 1;
        check("QSRC-175", "eight records at $8002 (b.bas:2 twice, c.bas:3, main.bas:11 primary, with and "
                          "without a column, main.bas:20, util.bas:11, b.bas:5): 'also' lists each other "
                          "file:line once, in lookup_all order, without the primary's line, and the key is "
                          "the pair (same file other line, other file same line, one file on two lines)",
              all.size() == 8 && want.size() == 5 && listed.toStdString() == joined &&
                  text.count(QStringLiteral("b.bas:2")) == 1 && text.count(QStringLiteral("c.bas:3")) == 1 &&
                  pair_key &&
                  !listed.contains(QStringLiteral("main.bas:11")),
              "n=" + std::to_string(all.size()) + " want=" + joined + " text=" + s(text));
    }
}

// ===========================================================================
// GH #347 stage 2 - selection sync both ways, browse mode, Show in Source, Go to PC Line.
//
// Oracle: the issue's scope (QSRC-30/31), OD-7, the owner's parity rule (the Source tab and the
// Disassembly offer the same actions), SourceMap::lookup_all/resolve semantics (SRCRNG-04/05) and the
// plan's decisions D5-D10. jnext-internal GUI, no VHDL counterpart. The default fixture is main.bas
// 10-13 at $8000/$8002/$8005/$8006 and sub.bas:1 at $9000, PC $8000; the code there is
// NOP NOP CALL $9000 NOP JP $8000, so the Disassembly's rows are $8000 $8001 $8002 $8005 $8006.
// ===========================================================================

int disasm_row_y(int row) { return kPaintY + row * kLineH + kLineH / 2; }

struct SyncWin {
    Fixture& fx;
    SourcePanel* sp;
    DisasmPanel* dp;
    QPlainTextEdit* ed;
    QScrollBar* scroll;
    explicit SyncWin(Fixture& f)
        : fx(f), sp(window_source_panel(f)), dp(f.dbg()->disasm_panel()),
          ed(sp->findChild<QPlainTextEdit*>()), scroll(dp->findChild<QScrollBar*>()) {
        dp->set_paused(true);   // the fixture is paused; the panel's own flag follows the manager's notification
        tick();
        dp->activate_follow_pc();   // the PC on the centre row
        QApplication::processEvents();
    }
    /// The row an address centred in the view is on (the view is as high as the window made it).
    int centre() const { return std::max(4, (dp->height() - kPaintY) / kLineH) / 2; }
    void tick() { fx.mgr->refresh_panels(); QApplication::processEvents(); }
    /// A cursor move the user makes in the Source tab (the editor's signals do not tell it from a click).
    void src_move(int line) { ed->setTextCursor(QTextCursor(ed->document()->findBlockByNumber(line - 1))); }
    void src_select(int first, int last) {
        QTextCursor c(ed->document()->findBlockByNumber(first - 1));
        const QTextBlock b = ed->document()->findBlockByNumber(last - 1);
        c.setPosition(b.position() + b.length() - 1, QTextCursor::KeepAnchor);
        ed->setTextCursor(c);
    }
    void mouse(QEvent::Type type, int row) {
        const QPoint pos(kGutterW + 60, disasm_row_y(row));
        QMouseEvent ev(type, QPointF(pos), dp->mapToGlobal(pos), Qt::LeftButton,
                       type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(dp, &ev);
        QApplication::processEvents();
    }
    void press(int row) { mouse(QEvent::MouseButtonPress, row); mouse(QEvent::MouseButtonRelease, row); }
    /// Centre `addr` (no signal) and click its row, as the user does.
    void dis_click(uint16_t addr) { dp->show_address(addr); press(centre()); }
    QImage dis_image() const {
        QImage img(dp->size(), QImage::Format_ARGB32);
        img.fill(Qt::magenta);
        dp->render(&img);
        return img;
    }
    /// The blocks the editor tints with the scheme's linked colour.
    std::set<int> tinted() const {
        std::set<int> out;
        const QColor c = sp->findChild<SourceHighlighter*>()->colours_in_force().linked_line;
        for (const auto& sel : ed->extraSelections())
            if (sel.format.background().color() == c) out.insert(sel.cursor.blockNumber());
        return out;
    }
    bool linked(std::initializer_list<uint16_t> yes, std::initializer_list<uint16_t> no) const {
        for (uint16_t a : yes) if (!dp->source_linked(a)) return false;
        for (uint16_t a : no) if (dp->source_linked(a)) return false;
        return true;
    }
};

/// The two-place map of QSRC-100 (line 11 at $8002 in the mapped page and at $9000 in the one mapping $9000).
void load_two_place(Fixture& fx, const char* name) {
    const std::string a = std::to_string(fx.p80), b = std::to_string(fx.p90);
    fx.backend->load_source_map(write_sld(name,
        "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + a + "|32770|T|\n" +
        "main.bas|12||0|" + a + "|32773|T|\n" + "main.bas|11||0|" + b + "|36864|T|\n"), false);
}

struct MenuProbe { bool seen = false, present = false, enabled = false; QString tip; QStringList items; };

/// Right-click the Disassembly row `row`; read "Show in Source" and, when `trigger`, trigger it.
MenuProbe disasm_menu(DisasmPanel* dp, int row, bool trigger) {
    MenuProbe out;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || out.seen) return;
        out.seen = true;
        for (QAction* a : menu->actions()) {
            if (a->isSeparator()) continue;
            out.items << a->text();
            if (a->text() != QStringLiteral("Show in Source")) continue;
            out.present = true;
            out.enabled = a->isEnabled();
            out.tip = a->toolTip();
            if (trigger && a->isEnabled()) a->trigger();
        }
        menu->close();
    });
    timer.start(1);
    const QPoint pos(kGutterW + 60, disasm_row_y(row));
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, dp->mapToGlobal(pos));
    QApplication::sendEvent(dp, &ev);
    timer.stop();
    QApplication::processEvents();
    return out;
}

void test_source_sync() {
    const std::string tmp = g_tmp->path().toStdString();
    {   // QSRC-176: the label names the address browsed, with its "also" list and where the PC is
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80);
        fx.backend->load_source_map(write_sld("sync-also.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "util.bas|7||0|" + a + "|32770|T|\n" +
            "main.bas|11||0|" + a + "|32770|T|\n" + "main.bas|12||0|" + a + "|32773|T|\n"), false);
        write_main({});
        SyncWin w(fx);
        const QString page = QString::asprintf("%02X", ram_page_of(fx.backend->source_page(0x8002)));
        w.dis_click(0x8002);
        const QString browsing = w.sp->location_text();
        const QString want = QStringLiteral("main.bas:11  $8002  page %1  also: util.bas:7  "
                                            "(browsing; the PC is at main.bas:10, $8000)").arg(page);
        const bool label = browsing == want && w.sp->browsing() && w.sp->highlighted_line() == 10;
        // a map still pending verification: the note stays its own line AFTER the position's
        write_text(tmp + "/sync-also-pending.sld",
            std::string("|SLD.data.version|1\n||program.name:prog\n") +
            "||program.sha256:04bb9c5fbc1b0517a33504a5fbb7b9de7dd72a1de84987241364d2674495a823\n" +
            "||program.org:32768\n||program.size:4\n" + "main.bas|10||0" + kNextDevice +
            "main.bas|10||0|" + a + "|32768|T|\n" + "util.bas|7||0|" + a + "|32770|T|\n" +
            "main.bas|11||0|" + a + "|32770|T|\n" + "main.bas|12||0|" + a + "|32773|T|\n");
        fx.backend->load_source_map(tmp + "/sync-also-pending.sld", false, true);
        w.tick();
        const bool ended = !w.sp->browsing();   // another map ends a browse
        w.dis_click(0x8002);
        const QStringList lines = w.sp->location_text().split(QLatin1Char('\n'));
        const bool order = lines.size() == 2 && lines[0] == want && lines[1].startsWith(QStringLiteral("Source map not verified yet"));
        // the PC statement's own instructions are not browsing
        w.dis_click(0x8001);
        check("QSRC-176", "clicking $8002 (util.bas:7 also, main.bas:11 primary) with the PC at $8000: the label "
                          "names $8002, lists 'also: util.bas:7' and ends '(browsing; the PC is at main.bas:10, "
                          "$8000)', the PC line stays highlighted; under a pending map the note is the next line, "
                          "after both; loading a map ends the browse; a click inside the PC's own statement does not browse",
              label && ended && order && !w.sp->browsing() && !w.sp->location_text().contains(QStringLiteral("browsing")),
              s(browsing) + " | " + s(w.sp->location_text()));
    }
    {   // QSRC-177: a Source line lights every address of the line, in both places
        Fixture fx;
        fx.enable();
        load_two_place(fx, "sync-two.sld");
        write_main({});
        SyncWin w(fx);
        w.src_move(11);
        const bool on11 = w.linked({0x8002, 0x9000}, {0x8000, 0x8005, 0x8006});
        w.src_move(10);
        const bool on10 = w.linked({0x8000, 0x8001}, {0x8002, 0x9000, 0x8005});
        check("QSRC-177", "a click on Source line 11 (compiled to $8002 and to $9000) tints both addresses in the "
                          "Disassembly and none of $8000/$8005/$8006; line 10 tints $8000-$8001 and no other",
              on11 && on10, "on11=" + std::to_string(on11) + " on10=" + std::to_string(on10));
    }
    {   // QSRC-178: the tint as painted, under the PC's yellow and the selection
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        w.src_move(11);
        QImage img = w.dis_image();
        const auto px = [&](const QImage& i, int row) { return QColor::fromRgb(i.pixel(i.width() - 40, disasm_row_y(row))); };
        const QColor lk = source_sync_colors::linked(w.dp->palette()), yellow(255, 255, 204), white(255, 255, 255);
        const int c = w.centre();
        const bool rows = px(img, c + 2) == lk && px(img, c) == yellow && px(img, c + 1) == white && px(img, c + 3) == white;
        w.src_move(10);   // the PC's own line: its rows are linked, and the PC row keeps its yellow
        img = w.dis_image();
        const bool pc_wins = px(img, c) == yellow && px(img, c + 1) == lk && px(img, c + 2) == white;
        w.src_move(11);
        w.press(w.centre() + 2);      // select the linked row: the translucent blue over the tint
        img = w.dis_image();
        const QColor both = px(img, c + 2);
        const bool over = both != lk && both != white && both != yellow && both != QColor(229, 238, 255);
        // the colour is distinct from the PC yellow, the selection blue and #388's green, light and dark
        bool distinct = true;
        for (const QPalette& p : {QApplication::palette(), dark_test_palette()}) {
            const QColor l = source_sync_colors::linked(p);
            for (const QColor& o : {palette_tint::surface(p, yellow), palette_tint::surface(p, QColor(204, 221, 255)),
                                    coverage_colors::exec_line(p)})
                distinct = distinct && delta_e76(l, o) >= 10.0;
        }
        check("QSRC-178", "painted: with the Source cursor on line 11 the $8002 row has the shared tint, the PC row "
                          "$8000 its yellow and the others the background; on the PC's own line 10 the PC row keeps "
                          "the yellow over a linked row; a selected linked row is neither; the tint differs from "
                          "the PC yellow, the selection blue and the coverage green on light and dark",
              rows && pc_wins && over && distinct,
              "rows=" + std::to_string(rows) + " pc_wins=" + std::to_string(pc_wins) + " over=" + std::to_string(over) +
                  " distinct=" + std::to_string(distinct));
    }
    {   // QSRC-179: a multi-line selection, and a line any of whose locations is the address
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        w.src_select(10, 12);
        const bool lines3 = w.linked({0x8000, 0x8001, 0x8002, 0x8005}, {0x8006, 0x9000});
        const std::string a = std::to_string(fx.p80);
        fx.backend->load_source_map(write_sld("sync-nonprimary.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + a + "|32770|T|\n" +
            "main.bas|12||0|" + a + "|32770|T|\n" + "main.bas|13||0|" + a + "|32773|T|\n"), false);
        w.tick();
        w.src_move(11);   // $8002 has two locations, line 12 later and primary
        const bool secondary = w.linked({0x8002}, {0x8000, 0x8005});
        w.src_move(13);
        const bool next = w.linked({0x8005}, {0x8002, 0x8000});
        check("QSRC-179", "selecting lines 10-12 tints $8000-$8002 and $8005 but not $8006; with $8002 compiled to "
                          "lines 11 and 12 (12 primary) selecting 11 still tints $8002 and selecting 13 only $8005",
              lines3 && secondary && next,
              "lines3=" + std::to_string(lines3) + " secondary=" + std::to_string(secondary) + " next=" + std::to_string(next));
    }
    {   // QSRC-180: the Disassembly scrolls only for a move the user made in the Source tab
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        w.dp->show_address(0xA000);
        const int away = w.scroll->value();
        w.src_move(11);
        const int after = w.scroll->value();
        const bool revealed = after <= 0x8002 && after + 100 > 0x8002;
        w.src_move(12);   // $8005: in view
        const bool still = w.scroll->value() == after;
        w.dp->show_address(0xA000);
        set_pc(fx, 0x8006);
        w.sp->follow_pc();
        w.sp->refresh();   // a stop onto line 13: the Disassembly is not moved by it
        QApplication::processEvents();
        const bool stop_quiet = w.scroll->value() == away && w.dp->source_linked(0x8006);
        check("QSRC-180", "from a Disassembly at $A000 a user move to Source line 11 centres it on $8002; a second move "
                          "to line 12 (in view) does not move it; a stop onto line 13 tints $8006 but never scrolls it",
              revealed && still && stop_quiet,
              "away=" + std::to_string(away) + " after=" + std::to_string(after) +
                  " revealed=" + std::to_string(revealed) + " still=" + std::to_string(still) +
                  " stop_quiet=" + std::to_string(stop_quiet));
    }
    {   // QSRC-181: pages, lines without code, a message, running
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80), far = std::to_string(fx.p80 + 2);
        fx.backend->load_source_map(write_sld("sync-pages.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + far + "|32770|T|\n" +
            "main.bas|12||0|" + a + "|32773|T|\n"), false);
        write_main({});
        SyncWin w(fx);
        w.dp->show_address(0xA000);
        const int away = w.scroll->value();
        w.src_move(11);   // its only place is in a page that is not mapped now
        const bool unmapped = w.linked({}, {0x8000, 0x8002, 0x8005, 0x9000}) && w.scroll->value() == away;
        w.src_move(5);    // no code
        const bool nocode = w.linked({}, {0x8000, 0x8002, 0x8005}) && w.scroll->value() == away;
        w.src_move(12);
        const bool lit = w.linked({0x8005}, {});
        fx.backend->clear_source_map();
        w.tick();
        const bool cleared = w.linked({}, {0x8000, 0x8002, 0x8005, 0x9000});
        fx.load_map();
        w.tick();
        w.src_move(12);
        const bool relit = w.linked({0x8005}, {});
        fx.backend->run(jnext::dbg::CLIENT_NONE);
        w.sp->refresh();   // the manager's tick while running
        const bool running = w.linked({}, {0x8000, 0x8002, 0x8005, 0x9000});
        w.dis_click(0x8002);   // a selection made while the machine runs is not followed
        bool ignored = !w.sp->browsing() && w.linked({}, {0x8000, 0x8002, 0x8005, 0x9000});
        // ... and a move the user makes in the Source tab while it runs announces nothing: no signal, no tint,
        // and the Disassembly (which does not refresh then) is not scrolled
        w.dp->show_address(0xA000);
        const int run_scroll = w.scroll->value();
        QSignalSpy run_lines(w.sp, &SourcePanel::source_lines_selected);
        w.src_move(11);
        ignored = ignored && run_lines.count() == 0 && w.linked({}, {0x8000, 0x8002, 0x8005, 0x9000}) &&
                  w.scroll->value() == run_scroll;
        fx.emu.debug_state().pause();
        check("QSRC-181", "a line whose only place is in an unmapped page links nothing and does not scroll; a line "
                          "without code links nothing; clearing the map and running drop the tint, and a Disassembly click or a Source move while it runs is not followed",
              unmapped && nocode && lit && cleared && relit && running && ignored,
              "unmapped=" + std::to_string(unmapped) + " nocode=" + std::to_string(nocode) + " lit=" + std::to_string(lit) +
                  " cleared=" + std::to_string(cleared) + " relit=" + std::to_string(relit) + " running=" + std::to_string(running) +
                  " ignored=" + std::to_string(ignored));
    }
    {   // QSRC-182: a Disassembly click puts the Source cursor on the line and tints it
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        const int before = w.scroll->value();
        w.dp->show_address(0x8005);
        w.dp->show_address(0x8000);   // back to the view the click is made in
        const int view = w.scroll->value();
        w.press(w.centre() + 3);                   // the $8005 row
        uint16_t lo = 0, hi = 0;
        const bool sel = w.dp->selection_range(lo, hi);
        const bool cursor = w.ed->textCursor().blockNumber() == 11 && !w.ed->textCursor().hasSelection();
        const auto sels = w.ed->extraSelections();
        const bool tinted = w.tinted() == std::set<int>{11} && !sels.isEmpty() &&
                            sels.back().cursor.blockNumber() == 9;   // the PC line is still the last, on top
        check("QSRC-182", "a user click on the $8005 row: the Source cursor is on line 12 (block 11) and that line is "
                          "tinted, the PC line 10 is still highlighted, and the Disassembly keeps its view and takes "
                          "the click as its selection",
              cursor && tinted && w.sp->highlighted_line() == 10 && sel && lo == 0x8005 && hi == 0x8005 &&
                  w.dp->selected_address() == 0x8005 && w.scroll->value() == view && before >= 0,
              "cursor=" + std::to_string(cursor) + " tinted=" + std::to_string(tinted) + " sel=" + std::to_string(sel) +
                  " scroll=" + std::to_string(w.scroll->value()) + "/" + std::to_string(view));
    }
    {   // QSRC-183: a dragged range tints its lines; an address with no line does nothing
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        w.mouse(QEvent::MouseButtonPress, w.centre());       // $8000
        w.mouse(QEvent::MouseMove, w.centre() + 3);              // ... to $8005
        w.mouse(QEvent::MouseButtonRelease, w.centre() + 3);
        const bool range = w.tinted() == std::set<int>{9, 10, 11} && w.ed->textCursor().blockNumber() == 11;
        Fixture fy;
        fy.enable();
        fy.load_map();
        SyncWin v(fy);
        const int cursor0 = v.ed->textCursor().blockNumber();
        v.dp->show_address(0x0000);
        v.press(0);   // ROM: no record
        const bool rom = v.tinted().empty() && v.ed->textCursor().blockNumber() == cursor0 && !v.sp->browsing() &&
                         v.sp->highlighted_line() == 10;
        check("QSRC-183", "dragging the Disassembly from $8000 to $8005 tints Source lines 10, 11 and 12 (not 13) and "
                          "puts the cursor on 12; a click on ROM $0000 (no record) tints nothing, moves nothing, browses nothing",
              range && rom, "range=" + std::to_string(range) + " rom=" + std::to_string(rom));
    }
    {   // QSRC-184: another file is browsed, until a stop
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        const QString page = QString::asprintf("%02X", ram_page_of(fx.backend->source_page(0x9000)));
        w.dis_click(0x9000);
        const bool shown = w.sp->shown_file() == "sub.bas" && w.ed->textCursor().blockNumber() == 0 &&
                           w.tinted() == std::set<int>{0} && w.sp->highlighted_line() == 0 && w.sp->browsing() &&
                           w.sp->location_text() == QStringLiteral("sub.bas:1  $9000  page %1  "
                                                  "(browsing; the PC is at main.bas:10, $8000)").arg(page);
        const int top = w.ed->verticalScrollBar()->value();
        for (int i = 0; i < 20; ++i) w.tick();
        const bool kept = w.sp->shown_file() == "sub.bas" && w.sp->browsing() && w.ed->verticalScrollBar()->value() == top &&
                          w.ed->textCursor().blockNumber() == 0;
        w.fx.dbg()->activate_follow_pc();   // what a stop does
        w.tick();
        const bool back = w.sp->shown_file() == "main.bas" && w.sp->highlighted_line() == 10 && !w.sp->browsing() &&
                          w.ed->textCursor().blockNumber() == 9;
        // the PC line number exists in the browsed file (util.bas has 12 lines): still no PC highlight
        const std::string a = std::to_string(fx.p80), b = std::to_string(fx.p90);
        fx.backend->load_source_map(write_sld("sync-util.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "util.bas|7||0|" + b + "|36864|T|\n"), false);
        {
            std::string util;
            for (int i = 1; i <= 12; ++i) util += "REM util " + std::to_string(i) + "\n";
            write_text(g_tmp->path().toStdString() + "/util.bas", util);
        }
        w.tick();
        w.dis_click(0x9000);
        const auto usels = w.ed->extraSelections();
        const bool other_file = w.sp->shown_file() == "util.bas" && w.sp->highlighted_line() == 0 && w.tinted() == std::set<int>{6} &&
                                usels.size() == 1 && w.ed->textCursor().blockNumber() == 6;
        check("QSRC-184", "a click on $9000 shows sub.bas with the cursor on line 1 tinted, no PC highlight and the "
                          "'browsing' label; 20 ticks keep it; a stop returns to main.bas with line 10 highlighted; in a 12-line "
                          "util.bas the PC's own line number 10 is not highlighted either",
              shown && kept && back && other_file,
              "other_file=" + std::to_string(other_file) + " shown=" + std::to_string(shown) + " kept=" + std::to_string(kept) + " back=" + std::to_string(back) +
                  " " + s(w.sp->location_text()));
    }
    {   // QSRC-185: Go to PC Line
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        QPushButton* b = button(w.sp, QStringLiteral("Go to PC Line"));
        bool no_clash = true;   // the Disassembly's caption is not repeated in the Source tab
        for (QPushButton* x : w.sp->findChildren<QPushButton*>()) no_clash = no_clash && x->text() != QStringLiteral("Go to PC");
        const bool off = b && !b->isEnabled();
        w.dis_click(0x9000);
        const bool on = b && b->isEnabled();
        if (b) b->click();
        QApplication::processEvents();
        const bool pressed = w.sp->shown_file() == "main.bas" && w.sp->highlighted_line() == 10 && !w.sp->browsing() &&
                             b && !b->isEnabled();
        w.dis_click(0x9000);
        fx.backend->run(jnext::dbg::CLIENT_NONE);
        w.sp->refresh();   // the manager's tick while running
        const bool running = b && !b->isEnabled() && !w.sp->browsing();
        fx.emu.debug_state().pause();
        // the gutter acts on the file browsed: a click on sub.bas line 1 arms $9000 in its page
        Fixture fg;
        fg.enable();
        fg.load_map();
        BreakpointModel& m = fg.mgr->breakpoints();
        SrcView v(fg, m);
        v.sp->set_disasm_selection(0x9000, 0x9000, 0x9000);
        QApplication::processEvents();
        v.click(1);
        const bool armed = v.sp->shown_file() == "sub.bas" && m.exists(BreakpointModel::Execute, 0x9000, pg(fg.p90));
        m.clear_all();
        check("QSRC-185", "'Go to PC Line' (not the Disassembly's 'Go to PC') is disabled when not browsing, enabled "
                          "while browsing sub.bas, back to main.bas:10 highlighted when pressed, disabled again; "
                          "disabled while running; a gutter click on sub.bas line 1 arms $9000 in its page",
              b && no_clash && off && on && pressed && running && armed,
              "off=" + std::to_string(off) + " on=" + std::to_string(on) + " pressed=" + std::to_string(pressed) +
                  " running=" + std::to_string(running) + " armed=" + std::to_string(armed));
    }
    {   // QSRC-186: Show in Source
        Fixture fx;
        fx.enable();
        SyncWin w(fx);
        const MenuProbe none = disasm_menu(w.dp, w.centre(), false);   // no source map yet
        fx.load_map();
        write_main({});
        w.tick();
        w.dp->show_address(0x9000);
        QTabWidget* tabs = nullptr;
        for (auto* tw : fx.dbg()->findChildren<QTabWidget*>())
            if (tw->indexOf(w.sp) >= 0) tabs = tw;
        if (tabs) tabs->setCurrentIndex(tabs->indexOf(w.sp) == 0 ? 1 : 0);
        const MenuProbe on = disasm_menu(w.dp, w.centre(), true);
        const bool raised = tabs && tabs->currentWidget() == w.sp && w.sp->shown_file() == "sub.bas" &&
                            w.ed->textCursor().blockNumber() == 0 && w.sp->browsing();
        w.dp->show_address(0xA000);
        const MenuProbe off = disasm_menu(w.dp, w.centre(), false);
        check("QSRC-186", "the Disassembly menu has no 'Show in Source' without a map; with one, on $9000 it is "
                          "enabled and raises the Source tab on sub.bas:1; on $A000 it is disabled, tooltip "
                          "'no source line at $A000'",
              none.seen && !none.present && on.present && on.enabled && raised && off.present && !off.enabled &&
                  off.tip == QStringLiteral("no source line at $A000"),
              "none=" + std::to_string(none.present) + " on=" + std::to_string(on.enabled) + " raised=" + std::to_string(raised) +
                  " tip=" + s(off.tip) + " items=" + s(off.items.join("|")));
    }
    {   // QSRC-187: no feedback loop
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        QSignalSpy sel(w.dp, &DisasmPanel::selection_changed);
        w.dp->show_address(0x8005);   // the Source tab's own request: no selection_changed
        w.dp->show_address(0x8000);
        const bool quiet_show = sel.count() == 0;
        QSignalSpy lines(w.sp, &SourcePanel::source_lines_selected);
        QSignalSpy cur(w.ed, &QPlainTextEdit::cursorPositionChanged);
        QSignalSpy scr(w.scroll, &QScrollBar::valueChanged);
        w.press(w.centre() + 3);   // a click on the $8005 row
        const std::string d187 = "sel=" + std::to_string(sel.count()) + " scr=" + std::to_string(scr.count()) +
                                 " cur=" + std::to_string(cur.count()) + " lines=" + std::to_string(lines.count());
        const bool dis_click = quiet_show && sel.count() == 1 && scr.count() == 0 && cur.count() == 1 && lines.count() == 1 &&
                               lines.at(0).at(3).toBool() == false && lines.at(0).at(1).toInt() == 12;
        w.press(w.centre() + 3);   // the same row again: the same selection, nothing to announce
        const bool same_row = sel.count() == 1;
        sel.clear(); lines.clear(); cur.clear(); scr.clear();
        w.src_move(11);   // a move in the Source tab
        const bool src_click = lines.count() == 1 && lines.at(0).at(3).toBool() == true && lines.at(0).at(1).toInt() == 11 &&
                               sel.count() == 0;
        w.press(w.centre() + 3);   // the Source tab has moved since: the same row is a change again
        const bool again = sel.count() == 1;
        sel.clear(); lines.clear(); cur.clear(); scr.clear();
        for (int i = 0; i < 10; ++i) w.tick();
        const bool ticks = sel.count() == 0 && lines.count() == 0 && scr.count() == 0;
        check("QSRC-187", "show_address() never emits selection_changed; one Disassembly click: one selection_changed, no scroll of the Disassembly, one Source cursor "
                          "move, one source_lines_selected without reveal; one Source move: one source_lines_selected "
                          "with reveal and no selection_changed; the same row twice announces once, and again after the Source tab moved; ten ticks: neither signal",
              dis_click && same_row && src_click && again && ticks,
              "dis=" + std::to_string(dis_click) + " (" + d187 + ") same_row=" + std::to_string(same_row) +
                  " src=" + std::to_string(src_click) + " again=" + std::to_string(again) + " ticks=" + std::to_string(ticks));
    }
    {   // QSRC-188: the real z88dk map; main.c:18 has two starts
        Fixture fx;
        fx.enable();
        const std::string root = tmp + "/q188";
        copy_z88dk("master/sdcc-debug", "prog.map", root + "/build/prog.map");
        std::filesystem::create_directories(root + "/src");
        std::filesystem::copy_file(std::filesystem::path(JNEXT_Z88DK_FIXTURE_DIR) / "src/main.c",
                                   root + "/src/main.c", std::filesystem::copy_options::overwrite_existing);
        fx.backend->load_source_map(root + "/build/prog.map", true);
        set_cpu(fx, 0x8C6B);
        SourcePanel* sp = window_source_panel(fx);
        sp->set_search_dirs({root + "/src"});
        SyncWin w(fx);
        w.src_move(18);
        const bool both = w.linked({0x8C6F, 0x8C70, 0x8C89}, {0x8C71, 0x8C6B});
        w.dis_click(0x8C89);
        const bool back = w.sp->shown_file().size() > 0 && w.ed->textCursor().blockNumber() == 17 &&
                          w.linked({0x8C6F, 0x8C89}, {0x8C71});
        check("QSRC-188", "z88dk sdcc map: a Source click on main.c:18 tints both its starts $8C6F and $8C89 and not "
                          "main.c:19's $8C71; a Disassembly click on $8C89 puts the cursor on line 18 (block 17) and "
                          "tints the other start too",
              both && back, "both=" + std::to_string(both) + " back=" + std::to_string(back) + " " + s(w.sp->location_text()));
    }
    {   // QSRC-189: the browse tint and the PC bar in every scheme, on a light and a dark application palette
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        using SC = SourceHighlighter::SchemeColours;
        std::string bad;
        const SC& light = SourceHighlighter::scheme_colours(SourceScheme::Light);
        const SC& dark = SourceHighlighter::scheme_colours(SourceScheme::Dark);
        const SC& bor = SourceHighlighter::scheme_colours(SourceScheme::Borland);
        for (const auto& e : {std::make_pair("light", &light), std::make_pair("dark", &dark), std::make_pair("borland", &bor)}) {
            const SC& c = *e.second;
            for (int r = 0; r < SourceHighlighter::kRoles; ++r)
                if (contrast(c.role[r], c.linked_line) < 4.5) bad += std::string(e.first) + ":R5(" + std::to_string(r) + ") ";
            if (delta_e76(c.linked_line, c.background) < 10.0) bad += std::string(e.first) + ":bg ";
            if (delta_e76(c.linked_line, c.coverage_line) < 10.0) bad += std::string(e.first) + ":cov ";
            if (delta_e76(c.linked_line, c.pc_background) < 10.0) bad += std::string(e.first) + ":bar ";
        }
        if (light.linked_line != source_sync_colors::linked(QApplication::palette())) bad += "light-not-shared ";
        const QPalette app0 = QApplication::palette();
        for (bool dark_app : {false, true}) {
            if (dark_app) QApplication::setPalette(dark_test_palette());
            QApplication::processEvents();
            for (SourceScheme sch : {SourceScheme::Auto, SourceScheme::Light, SourceScheme::Dark, SourceScheme::Borland}) {
                const std::string tag = std::string(dark_app ? "dark-app/" : "light-app/") + std::to_string(static_cast<int>(sch)) + " ";
                v.sp->set_colour_scheme(sch);
                v.sp->set_disasm_selection(0x8005, 0x8005, 0x8005);   // browse line 12; the PC stays on 10
                QApplication::processEvents();
                const SC& c = v.sp->findChild<SourceHighlighter*>()->colours_in_force();
                const auto sels = v.ed->extraSelections();
                const bool order = sels.size() == 2 && sels[0].format.background().color() == c.linked_line &&
                                   sels[0].cursor.blockNumber() == 11 && sels[1].format.background().color() == c.pc_background &&
                                   sels[1].cursor.blockNumber() == 9;
                if (!order) bad += tag + "selections ";
                QImage img(v.ed->viewport()->size(), QImage::Format_ARGB32);
                img.fill(Qt::magenta);
                v.ed->viewport()->render(&img);
                const auto at = [&](int line) { return QColor::fromRgb(img.pixel(img.width() - 4, v.row_y(line))); };
                if (at(12) != c.linked_line) bad += tag + "pixel-linked ";
                if (at(10) != c.pc_background) bad += tag + "pixel-bar ";
                if (at(13) != c.background) bad += tag + "pixel-bg ";
                const QImage g = v.gutter_image();
                const QRgb amber = palette_tint::ink(QApplication::palette(), QColor(0xE0, 0x9A, 0x00)).rgb();
                const QRgb gbg = g.pixel(v.gutter->width() - 3, 1);
                if (strip_of(v, g, 10, amber, gbg).coloured != 35 || strip_of(v, g, 12, amber, gbg).any) bad += tag + "wedge ";
            }
            if (dark_app) {
                QApplication::setPalette(app0);
                QApplication::processEvents();
            }
        }
        check("QSRC-189", "in Auto, Light, Dark and Borland, on a light and on a dark application palette: the browsed "
                          "line has the scheme's linked tint (selection and pixel), the PC line keeps the scheme's bar on "
                          "top and the wedge stays on it alone; every scheme's tint is legible under all ten class "
                          "colours and differs from its background, coverage tint and bar",
              bad.empty(), bad);
    }
}


// ── GH #347 stage 2, review round 1: messages about the browsed line, placement, user paths, two files ──

void test_source_sync_review() {
    const std::string tmp = g_tmp->path().toStdString();
    {   // QSRC-201: a message about the browsed line stays; only the map going away ends the browse
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80), b = std::to_string(fx.p90);
        fx.backend->load_source_map(write_sld("sync-missing.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "missing.bas|3||0|" + b + "|36864|T|\n"), false);
        write_main({});
        SyncWin w(fx);
        w.dis_click(0x9000);
        for (int i = 0; i < 5; ++i) w.tick();
        QPushButton* goto_pc = button(w.sp, QStringLiteral("Go to PC Line"));
        QPushButton* locate = button(w.sp, QStringLiteral("Locate File..."));
        const bool stays = w.sp->location_text() == QStringLiteral("missing.bas:3 - source file not found") &&
                           w.sp->browsing() && locate->isEnabled() && goto_pc->isEnabled();
        goto_pc->click();
        QApplication::processEvents();
        const bool back = w.sp->location_text().startsWith(QStringLiteral("main.bas:10")) && !w.sp->browsing() &&
                          !locate->isEnabled() && !goto_pc->isEnabled() && w.ed->textCursor().blockNumber() == 9;
        // the map cleared, then loaded again: the browse is over
        fx.load_map();
        w.tick();
        w.dis_click(0x9000);
        const bool before = w.sp->browsing() && w.sp->shown_file() == "sub.bas";
        fx.backend->clear_source_map();
        w.tick();
        fx.load_map();
        w.tick();
        const bool cleared = before && !w.sp->browsing() && w.sp->shown_file() == "main.bas" && w.sp->highlighted_line() == 10;
        // a map gone stale (a soft reset) ends it too, so loading the same map again does not bring it back
        fx.load_map();
        set_cpu(fx, 0x8000);
        w.tick();
        w.dis_click(0x9000);
        const bool again = w.sp->browsing();
        fx.backend->reset(jnext::dbg::CLIENT_NONE, jnext::dbg::ResetKind::Soft);
        w.tick();
        const bool stale = !w.sp->browsing() && w.sp->location_text().contains(QStringLiteral("does not match"));
        fx.load_map();
        set_cpu(fx, 0x8000);
        w.tick();
        const bool gone = again && stale && !w.sp->browsing();
        check("QSRC-201", "browsing a line whose file is not found keeps the message through five ticks, Locate File... and "
                          "Go to PC Line enabled, and Go to PC Line returns to main.bas:10; a map cleared and loaded again "
                          "ends a browse, and so does a map gone stale",
              stays && back && cleared && gone,
              "stays=" + std::to_string(stays) + " back=" + std::to_string(back) + " cleared=" + std::to_string(cleared) + " gone=" + std::to_string(gone) +
                  " " + s(w.sp->location_text()));
    }
    {   // QSRC-202: every accepted Disassembly selection places the cursor on the caret's line
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        w.dis_click(0x8002);
        const bool first = w.ed->textCursor().blockNumber() == 10;
        w.src_move(13);
        w.dis_click(0x8002);
        const bool again = w.ed->textCursor().blockNumber() == 10 && w.tinted() == std::set<int>{10} &&
                           w.dp->source_linked(0x8002) && !w.dp->source_linked(0x8006);
        w.src_move(12);
        w.dis_click(0x8001);   // the PC's own statement: the cursor goes back to line 10
        const bool pc_stmt = w.ed->textCursor().blockNumber() == 9 && !w.sp->browsing();
        check("QSRC-202", "clicking $8002 puts the cursor on line 11; after a Source move to line 13, clicking $8002 again "
                          "puts it back on 11 and the Disassembly tints $8002, not $8006; after a move to 12 a click in the "
                          "PC's own statement ($8001) returns it to line 10",
              first && again && pc_stmt,
              "first=" + std::to_string(first) + " again=" + std::to_string(again) + " pc_stmt=" + std::to_string(pc_stmt));
    }
    {   // QSRC-203: a browsed line in view is not scrolled to; one out of view is
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80);
        fx.backend->load_source_map(write_sld("sync-long.sld",
            "main.bas|100||0|" + a + "|32768|T|\n" + "main.bas|102||0|" + a + "|32770|T|\n" +
            "main.bas|200||0|" + a + "|32773|T|\n"), false);
        std::string text;
        for (int i = 1; i <= 300; ++i) text += "REM line " + std::to_string(i) + "\n";
        write_text(tmp + "/main.bas", text);
        BreakpointModel& m = fx.mgr->breakpoints();
        SrcView v(fx, m);
        const int top = v.ed->verticalScrollBar()->value();
        v.sp->set_disasm_selection(0x8002, 0x8002, 0x8002);   // line 102, two below the PC's, in view
        QApplication::processEvents();
        const bool kept = v.ed->verticalScrollBar()->value() == top && v.ed->textCursor().blockNumber() == 101;
        v.sp->set_disasm_selection(0x8005, 0x8005, 0x8005);   // line 200: far out of view
        QApplication::processEvents();
        const QRect r = v.ed->cursorRect();
        const bool shown = v.ed->verticalScrollBar()->value() != top && v.ed->viewport()->rect().contains(r) &&
                           std::abs(r.center().y() - v.ed->viewport()->rect().center().y()) <= 2 * r.height() &&
                           v.ed->textCursor().blockNumber() == 199;
        write_main({});
        check("QSRC-203", "in a 300-line file with the PC on line 100, browsing line 102 keeps the scroll position and puts the "
                          "cursor there; browsing line 200 centres it",
              kept && shown, "kept=" + std::to_string(kept) + " shown=" + std::to_string(shown));
    }
    {   // QSRC-204: a browsed address that has no line any more ends the browse
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        w.dis_click(0x9000);
        const bool on = w.sp->browsing();
        const uint8_t old_page = static_cast<uint8_t>(fx.p80);
        fx.emu.mmu().set_page(4, static_cast<uint8_t>(old_page + 2));   // the page the record names is gone from $8000-$9FFF
        w.tick();
        const bool ended = !w.sp->browsing();
        fx.emu.mmu().set_page(4, old_page);
        w.tick();
        check("QSRC-204", "browsing $9000 and then unmapping its page while paused ends the browse",
              on && ended && !w.sp->browsing(), "on=" + std::to_string(on) + " ended=" + std::to_string(ended));
    }
    {   // QSRC-205: the Disassembly's keys, Select All and the menu's collapse are user selections too
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        w.dis_click(0x8002);
        w.dp->setFocus();
        QTest::keyClick(w.dp, Qt::Key_Down);   // the caret moves to $8005 (line 12)
        const bool keys = w.ed->textCursor().blockNumber() == 11 && w.tinted() == std::set<int>{11};
        w.dis_click(0x8002);
        w.dp->select_all_visible();            // the whole view: lines 10-13 are in it
        const bool all = w.tinted().count(9) && w.tinted().count(10) && w.tinted().count(11) && w.tinted().count(12);
        w.dis_click(0x8002);
        w.ed->setFocus();
        disasm_menu(w.dp, w.centre() + 1, false);   // right-click $8005, outside the selection: it collapses onto it
        const bool menu = w.ed->textCursor().blockNumber() == 11;
        check("QSRC-205", "an arrow key in the Disassembly moves the Source cursor to the new row's line; Select All tints "
                          "the lines of the view; a right click outside the selection moves the Source cursor to that row's line",
              keys && all && menu,
              "keys=" + std::to_string(keys) + " all=" + std::to_string(all) + " menu=" + std::to_string(menu));
    }
    {   // QSRC-207: after a Disassembly click the Source cursor and scroll stay where the user puts them
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        bool cursor = true;
        for (uint16_t addr : {uint16_t(0x8001), uint16_t(0x8002)}) {   // the PC's own statement, then a browse
            w.dis_click(addr);
            w.src_move(13);
            for (int i = 0; i < 4; ++i) w.tick();
            cursor = cursor && w.ed->textCursor().blockNumber() == 12;
        }
        // the scroll too: a 300-line file, the PC on line 100, a click on line 102's address and a click on the PC's statement
        Fixture fy;
        fy.enable();
        const std::string a = std::to_string(fy.p80);
        fy.backend->load_source_map(write_sld("sync-keep.sld",
            "main.bas|100||0|" + a + "|32768|T|\n" + "main.bas|102||0|" + a + "|32770|T|\n"), false);
        std::string text;
        for (int i = 1; i <= 300; ++i) text += "REM line " + std::to_string(i) + "\n";
        write_text(tmp + "/main.bas", text);
        BreakpointModel& m = fy.mgr->breakpoints();
        SrcView v(fy, m);
        bool scroll = true;
        for (uint16_t addr : {uint16_t(0x8001), uint16_t(0x8002)}) {
            v.sp->set_disasm_selection(addr, addr, addr);
            QApplication::processEvents();
            v.ed->verticalScrollBar()->setValue(200);
            for (int i = 0; i < 4; ++i) { v.sp->refresh(); QApplication::processEvents(); }
            scroll = scroll && v.ed->verticalScrollBar()->value() == 200;
        }
        write_main({});
        check("QSRC-207", "after a Disassembly click on the PC's statement and on another line, a Source move to line 13 and "
                          "a scroll to line 200 both survive four paused ticks",
              cursor && scroll, "cursor=" + std::to_string(cursor) + " scroll=" + std::to_string(scroll));
    }
    {   // QSRC-208: a run ends the Source tab's tint of the Disassembly's range
        Fixture fx;
        fx.enable();
        fx.load_map();
        write_main({});
        SyncWin w(fx);
        w.dis_click(0x8002);
        const bool lit = w.tinted() == std::set<int>{10};
        fx.backend->run(jnext::dbg::CLIENT_NONE);
        w.sp->refresh();
        fx.emu.debug_state().pause();
        w.fx.dbg()->activate_follow_pc();   // the stop
        w.tick();
        check("QSRC-208", "a Disassembly selection tints Source line 11; after a run and a stop no line of the Source tab is "
                          "tinted by it and the PC line is shown",
              lit && w.tinted().empty() && !w.sp->browsing() && w.sp->highlighted_line() == 10,
              "lit=" + std::to_string(lit) + " tinted=" + std::to_string(w.tinted().size()));
    }
    {   // QSRC-206: two files whose lines share a number
        Fixture fx;
        fx.enable();
        const std::string a = std::to_string(fx.p80), b = std::to_string(fx.p90);
        fx.backend->load_source_map(write_sld("sync-twofiles.sld",
            "main.bas|10||0|" + a + "|32768|T|\n" + "main.bas|11||0|" + a + "|32770|T|\n" +
            "util.bas|10||0|" + b + "|36864|T|\n"), false);
        write_main({});
        {
            std::string util;
            for (int i = 1; i <= 12; ++i) util += "REM util " + std::to_string(i) + "\n";
            write_text(tmp + "/util.bas", util);
        }
        SyncWin w(fx);
        QSignalSpy lines(w.sp, &SourcePanel::source_lines_selected);
        w.dis_click(0x9000);   // util.bas:10 against the PC on main.bas:10: another line, browsed
        bool quiet = true;     // loading the file is no user move
        for (int i = 0; i < lines.count(); ++i) quiet = quiet && lines.at(i).at(3).toBool() == false;
        const bool browse = w.sp->browsing() && w.sp->shown_file() == "util.bas" && w.ed->textCursor().blockNumber() == 9;
        // a range over both files tints only the lines of the file shown
        w.sp->set_disasm_selection(0x8000, 0x9000, 0x9000);
        const bool only = w.tinted() == std::set<int>{9};
        // Source line 10 of main.bas lights $8000 and not util.bas's line 10 at $9000
        w.sp->go_to_pc_line();
        w.src_move(10);
        const bool other = w.linked({0x8000}, {0x9000});
        check("QSRC-206", "util.bas:10 against the PC on main.bas:10 is browsed, and loading it announces no user move; a range "
                          "over both files tints only util.bas's line 10; Source line 10 of main.bas lights $8000 and not "
                          "util.bas's line 10 at $9000",
              quiet && browse && only && other,
              "quiet=" + std::to_string(quiet) + " browse=" + std::to_string(browse) + " only=" + std::to_string(only) +
                  " other=" + std::to_string(other));
    }
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qunsetenv("QT_QPA_PLATFORMTHEME");
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    g_tmp = &cfg;
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", cfg.filePath(QStringLiteral("xdg-config")).toUtf8());
    QApplication app(argc, argv);

    test_source_tab();
    test_source_tab_rewind();
    test_source_coverage();
    test_idle_source_reload();
    test_relative_map();
    test_load_refused();
    test_breakpoints();
    test_call_stack();
    test_address_fields();
    test_watch_labels();
    test_watch_dialog_refused();
    test_gutter();
    test_edit_type_refused();
    test_breakpoint_page_rows();
    test_line_starts();
    test_source_hex();
    test_source_scroll();
    test_source_recentre_terms();
    test_source_follow_stop();
    test_source_running();
    test_sidecar_reason();
    test_sidecar_reason_late_window();
    test_source_map_lifetime();
    test_source_buttons();
    test_source_button_states();
    test_source_reverse_refused();
    test_source_messages();
    test_call_stack_pages();
    test_paged_symbols();
    test_sld_labels_menu();
    test_sld_labels_add_execute();
    test_call_stack_non_page_caller();
    test_source_menu();
    test_z88dk_source_map();
    test_dot_command();
    test_z88dk_listings();
    test_source_search();
    test_source_gutter();
    test_z88dk_c_names();
    test_source_context_menu();
    test_source_menu_review();
    test_source_highlight_detection();   // GH #422
    test_source_highlight_rules();
    test_source_highlight_panel();
    test_source_highlight_classes();   // GH #422 stage 2
    test_source_highlight_schemes();
    test_z88dk_c_names_source();
    test_source_current_line();   // GH #347 stage 1
    test_source_sync();           // GH #347 stage 2
    test_source_sync_review();    // GH #347 stage 2, review 1

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
