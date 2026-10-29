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
// Qt is required; a display is not (offscreen QPA, as the other suites).
// Run: ./build/test/debugger_source_panel_test
// ===========================================================================

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "debug/debugger.h"
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
#include "debugger/source_panel.h"
#include "debugger/watch_panel.h"

#include <QAbstractButton>
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
#include <QScrollBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QWindow>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include "../row_id.h"

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

    explicit Fixture(int rewind_frames = 0) {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
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
                     [&]() { button(sp, QStringLiteral("Load SLD..."))->click(); });
        check("QSRC-19", "the Load SLD button loads the chosen source map",
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
                [&]() { button(sp, QStringLiteral("Load SLD..."))->click(); });
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
    test_call_stack_non_page_caller();
    test_source_menu();

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
