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
#include "debug/debugger.h"
#include "debugger/breakpoint_model.h"
#include "debugger/breakpoint_panel.h"
#include "debugger/callstack_panel.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/source_panel.h"
#include "debugger/watch_panel.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

    QAction* add_exec = nullptr;
    for (QAction* a : fx.dbg()->menuBar()->actions())
        if (a->menu())
            for (QAction* i : a->menu()->actions())
                if (i->text() == QStringLiteral("Add &Execute Breakpoint...")) add_exec = i;
    const bool seen2 =
        add_exec && answer_modal(QStringLiteral("Landing"), [&]() { add_exec->trigger(); });
    check("QADR-02", "Add Execute Breakpoint takes a symbol name",
          seen2 && fx.mgr->breakpoints().pc_exists(0x8005));
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
    test_breakpoints();
    test_call_stack();
    test_address_fields();

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
