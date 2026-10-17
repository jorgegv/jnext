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
//   8001  00           NOP          (unmapped)
//   8002  CD 00 90     CALL SUB     main.bas:11
//   8005  00           NOP          main.bas:12
//   8006  C3 00 80     JP $8000     main.bas:13
//   9000  00           NOP          sub.bas:1
//   9001  C9           RET          (unmapped)
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
#include "debug/source_file.h"
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
#include <QScrollBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
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
    /// Right-click the top line and trigger `wanted` from the popup.
    void context_pick(const QString& wanted) {
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, [&]() {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu) return;
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
    m.add(BreakpointModel::Execute, 0x8005, static_cast<uint16_t>(fx.p80));
    const auto rows = m.rows();
    const bool one = rows.size() == 1 && rows[0].own && rows[0].type == BreakpointModel::Execute &&
                     rows[0].page == fx.p80;
    check("QBPM-01", "an Execute breakpoint qualified with a page is the GUI's own row, "
                     "with its page",
          one);
    char want[32];
    std::snprintf(want, sizeof(want), "$8005 @%02X", fx.p80);
    check("QBPM-02", "its address reads with the page", one && rows[0].addr_text == want,
          one ? s(rows[0].addr_text) : "");
    bool ram_only = false;
    for (const auto& si : fx.backend->subscriptions(false))
        if (si.id == rows[0].id) ram_only = si.filter.page_ram_only;
    check("QBPM-07", "the GUI's page-qualified breakpoint names RAM: it is page_ram_only",
          one && ram_only);
    m.add(BreakpointModel::Execute, 0x8005);
    check("QBPM-03", "a logical breakpoint at the same address is a second row",
          m.rows().size() == 2);
    m.remove(BreakpointModel::Execute, 0x8005);
    check("QBPM-04", "removing the logical one keeps the page-qualified one",
          m.rows().size() == 1 && m.rows()[0].page == fx.p80);
    check("QBPM-05", "the gutter marks it while its page is mapped, not for another page",
          m.pc_marked(0x8005, static_cast<uint16_t>(fx.p80)) &&
              !m.pc_marked(0x8005, static_cast<uint16_t>((fx.p80 + 1) % 224)));
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
          seen && added.size() == 1 && added[0].addr == 0x8002 && added[0].page == fx.p80);
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
          seen3 && rows.size() == 1 && rows[0].addr == 0x8005 && rows[0].page == fx.p80);

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

// ── B2: the gutter removes the breakpoint it draws ─────────────────────

void test_gutter() {
    Fixture fx;
    fx.enable();
    fx.load_map();
    BreakpointModel& m = fx.mgr->breakpoints();
    const auto page = static_cast<uint16_t>(fx.p80);
    {
        // ROM and RAM page numbers overlap: a breakpoint on RAM page N at $0000
        // is not drawn while ROM page N supplies the fetch.
        GutterView g(*fx.backend, m);
        if (auto* sb = g.panel->findChild<QScrollBar*>()) sb->setValue(0);
        const uint16_t rom = fx.backend->effective_page(0);
        const uint16_t fetch = fx.backend->source_page(0);
        m.add(BreakpointModel::Execute, 0x0000, rom);
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
        const bool left = m.rows().size() == 1 && m.rows()[0].page == page;
        check("QSRC-28", "while running, a click that removes the plain breakpoint still "
                         "draws the source breakpoint under it",
              left && g.red_dot());
        g.panel->set_paused(true);
        m.clear_all();
    }
    {
        // While running the panel keeps the lines it drew. A bank switch after
        // that must not turn a click on the drawn dot into a second breakpoint.
        GutterView g(*fx.backend, m);
        const uint8_t other = static_cast<uint8_t>(page + 2);
        auto rows_text = [&]() {
            std::string t;
            for (const auto& r : m.rows()) t += " page=" + std::to_string(r.page);
            return t;
        };
        m.add(BreakpointModel::Execute, 0x8005, page);
        g.panel->refresh();
        g.panel->set_paused(false);
        fx.emu.mmu().set_page(4, other);
        g.click_gutter();
        const bool click_ok = m.rows().empty();
        const std::string click_rows = rows_text();
        fx.emu.mmu().set_page(4, static_cast<uint8_t>(page));
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
        fx.emu.mmu().set_page(4, static_cast<uint8_t>(page));
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
    const auto page = static_cast<uint16_t>(fx.p80);
    BreakpointPanel* bp = fx.dbg() ? fx.dbg()->breakpoint_panel() : nullptr;
    auto* table = bp ? bp->findChild<QTableWidget*>() : nullptr;
    if (!bp || !table) { check("QBPM-08", "Breakpoints panel", false); return; }

    m.add(BreakpointModel::Execute, 0x8005, page);
    bp->refresh();
    if (QTableWidgetItem* en = table->item(0, 0)) en->setCheckState(Qt::Unchecked);
    const bool disabled = m.rows().size() == 1 && !m.rows()[0].enabled && m.rows()[0].page == page;
    table->setCurrentCell(0, 1);
    // The edit moves it to main.bas:11 ($8002): the old row must go, by its page.
    const bool edited = answer_modal(QStringLiteral("main.bas:11"), [&]() { bp->on_edit(); });
    const auto after_edit = m.rows();
    const bool kept = edited && after_edit.size() == 1 && after_edit[0].addr == 0x8002 &&
                      after_edit[0].page == page && !after_edit[0].enabled;
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
    fx.emu.mmu().set_page(4, static_cast<uint8_t>(page + 2));
    bp->refresh();
    const QString while_other = cell(bp, 0, 3);
    fx.emu.mmu().set_page(4, static_cast<uint8_t>(page));
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
    mem.filter.page = page;
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

// ── QBPM-10: Edit never cancels silently ──────────────────────────────

void test_edit_type_refused() {
    Fixture fx;
    fx.enable();
    fx.load_map();
    BreakpointModel& m = fx.mgr->breakpoints();
    const auto page = static_cast<uint16_t>(fx.p80);
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
              rows[0].type == BreakpointModel::Execute && rows[0].page == page,
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
        fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x8002,
                                  static_cast<uint16_t>(fx.p80));
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
        check("QSRC-25", "a mapped line whose file cannot be found says so",
              sp->location_text().startsWith(QStringLiteral("nowhere.bas:10")) &&
                  sp->location_text().endsWith(QStringLiteral("source file not found")),
              s(sp->location_text()));
    }
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

}  // namespace

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
    test_gutter();
    test_edit_type_refused();
    test_breakpoint_page_rows();
    test_sidecar_reason();
    test_source_buttons();
    test_source_button_states();
    test_source_messages();

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
