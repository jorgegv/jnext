// ===========================================================================
// The Qt Variables tab (GH #435, E353-05 of epic #353):
//
//   QVAR-*   the Variables tab: C locals per call frame, globals and statics, lazy expansion,
//            the Call Stack selection kept in step both ways, addresses, refresh and idle
//   QVARW-*  editing a value in place (GH #437): the editor, the write, a refusal, the frame, the client, the machine seeing it
//
// The real DebuggerManager + DebuggerWindow over a real ZX Next Emulator running the committed z88dk
// fixtures (test/fixtures/z88dk). The window is enabled BEFORE the program is loaded, so call tracking
// covers it from its entry.
//
// Oracle: the C sources of the fixtures (test/fixtures/z88dk/src/vars.c, other.c, bk.c, bkmain.c, types.h):
// `sum(i, g_int, pp, c)` at vars.c:41 on its first call has a = 0, b = -5, k = 120 'x', t = 0 + -5 + 120 + 7 = 122,
// acc = 3 * 115 = 345, calls = 1, and its caller main has lp = {3, 4, 5}; the committed maps (addresses, records)
// and the long-address rules (src/debug/long_address.cpp, zxnext.vhd:2964). No VHDL counterpart.
//
// Qt is required; a display is not (offscreen QPA).
// Run: ./build/test/debugger_variables_panel_test
// ===========================================================================

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "debug/events.h"
#include "debug/long_address.h"
#include "debugger/callstack_panel.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/variables_panel.h"
#include "debugger/watch_panel.h"
#include "platform/emulator_boot.h"
#include "core/log.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QMetaType>
#include <QTreeWidget>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <memory>
#include <string>
#include "../row_id.h"

namespace fs = std::filesystem;
using jnext::dbg::Debugger;
using jnext::dbg::MemSpace;

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

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
const fs::path kFixtures = JNEXT_Z88DK_FIXTURE_DIR;

jnext::dbg::Subscription execute_stop(uint16_t addr) {
    jnext::dbg::Subscription sub;
    sub.kind = jnext::dbg::EventKind::Execute;
    sub.filter.lo = sub.filter.hi = addr;
    sub.action = jnext::dbg::Action::Stop;
    return sub;
}

// A Next machine, the debugger window enabled on it, and a test client that drives the run.
struct Fix {
    Emulator emu;
    std::unique_ptr<Debugger> backend;
    QMainWindow win;
    DebuggerManager* mgr = nullptr;
    jnext::dbg::ClientId me = jnext::dbg::CLIENT_NONE;
    fs::path dir;

    Fix() {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        emu.init(cfg);
        backend = std::make_unique<Debugger>(emu);
        jnext::dbg::ClientInfo ci;
        ci.name = "test";
        ci.kind = jnext::dbg::ClientKind::Test;
        me = backend->attach(ci).value;
        backend->pause(me);
        mgr = new DebuggerManager(&win, *backend, &win);
        mgr->set_enabled(true);
        QApplication::processEvents();
    }
    ~Fix() {
        DebuggerWindow* w = mgr->debugger_window_ptr();
        mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
        delete w;
    }
    DebuggerWindow* dbg() const { return mgr->debugger_window_ptr(); }
    VariablesPanel* vp() const { return dbg()->variables_panel(); }
    CallStackPanel* cs() const { return dbg()->callstack_panel(); }
    QTreeWidget* tree() const { return vp()->findChild<QTreeWidget*>("variables_tree"); }
    QComboBox* frames() const { return vp()->findChild<QComboBox*>("variables_frame"); }
    QLabel* status() const { return vp()->findChild<QLabel*>("variables_status"); }
    QTableWidget* cs_table() const { return cs()->findChild<QTableWidget*>(); }

    // Load a fixture's NEX and its map, the way the frontends do.
    bool load(const char* fixture, const char* name, const std::function<void(const fs::path&)>& edit = {}) {
        dir = fs::path(g_tmp->path().toStdString()) / name;
        fs::create_directories(dir);
        for (const auto& e : fs::directory_iterator(kFixtures / fixture)) {
            const std::string n = e.path().filename().string();
            if (n == "prog.nex" || n == "prog.map" || (n.size() > 4 && n.compare(n.size() - 4, 4, ".bin") == 0))
                fs::copy_file(e.path(), dir / n, fs::copy_options::overwrite_existing);
        }
        if (edit) edit(dir);
        const bool ok = emulator_apply_load(emu, (dir / "prog.nex").string(), false);
        backend->load_program_sidecars((dir / "prog.nex").string());
        return ok;
    }
    // The window brought to the machine's state, and redrawn.
    void settle() {
        mgr->check_breakpoint_hit();
        dbg()->refresh_panels();
        QApplication::processEvents();
    }
    // Run until `addr` is about to execute; the window sees the stop.
    bool run_to(uint16_t addr) {
        const auto id = backend->subscribe(me, execute_stop(addr)).value;
        backend->run(me);
        mgr->check_breakpoint_hit();
        for (int i = 0; i < 200 && !backend->state().paused; ++i) emu.run_frame();
        backend->unsubscribe(me, id);
        settle();
        return backend->state().paused && backend->registers().PC == addr;
    }
    // Single-step with the interrupts off (the banked fixtures' banked_call enables them; this machine has no ROM).
    bool step_to(uint16_t addr, int limit = 40000) {
        for (int i = 0; i < limit && backend->registers().PC != addr; ++i) {
            Z80Registers r = emu.cpu().get_registers();
            r.IFF1 = r.IFF2 = 0;
            emu.cpu().set_registers(r);
            emu.execute_single_instruction();
        }
        settle();
        return backend->registers().PC == addr;
    }
    unsigned peek16(uint16_t a) { return emu.mmu().peek(a) | (emu.mmu().peek(static_cast<uint16_t>(a + 1)) << 8); }
};

QTreeWidgetItem* child(QTreeWidgetItem* parent, const QString& name) {
    for (int i = 0; parent && i < parent->childCount(); ++i)
        if (parent->child(i)->text(0) == name) return parent->child(i);
    return nullptr;
}
QTreeWidgetItem* locals(Fix& f) { return f.tree()->topLevelItem(0); }
QTreeWidgetItem* globals(Fix& f) { return f.tree()->topLevelItem(1); }
QTreeWidgetItem* global(Fix& f, const char* file, const char* name) { return child(child(globals(f), file), name); }
QString val(QTreeWidgetItem* it) { return it ? it->text(2) : QStringLiteral("<none>"); }
QString addr(QTreeWidgetItem* it) { return it ? it->text(3) : QStringLiteral("<none>"); }
QString ty(QTreeWidgetItem* it) { return it ? it->text(1) : QStringLiteral("<none>"); }
void expand(QTreeWidgetItem* it) {
    if (it) it->setExpanded(true);
    QApplication::processEvents();
}
QString names(QTreeWidgetItem* parent) {
    QStringList l;
    for (int i = 0; parent && i < parent->childCount(); ++i) l << parent->child(i)->text(0);
    return l.join(' ');
}
std::string detail_of(QTreeWidgetItem* it) {
    return it ? s(it->text(0) + "|" + it->text(1) + "|" + it->text(2) + "|" + it->text(3)) : "<none>";
}

// A CDB record the way a compiler writes it into a map: every character that is not a letter is `_xx`.
std::string esc(const std::string& t) {
    std::string o;
    for (const unsigned char c : t) {
        if (std::isalpha(c)) {
            o += static_cast<char>(c);
        } else {
            char b[8];
            std::snprintf(b, sizeof b, "_%02x", c);
            o += b;
        }
    }
    return o;
}
// Append records (`S:G$...`) and symbols to a copy of the fixture's map.
void add_to_map(const fs::path& d, const std::string& text) { std::ofstream(d / "prog.map", std::ios::app) << text; }
const char* const kScope = " = $0001 ; const, public, , vars_c, code_compiler, vars.c::main::1::6:66\n";

QTableWidget* wtable(Fix& f) { return f.dbg()->watch_panel()->findChild<QTableWidget*>(); }
QString wcell(Fix& f, int row, int col) {
    QTableWidgetItem* it = wtable(f)->item(row, col);
    return it ? it->text() : QStringLiteral("<no cell>");
}
std::string wrow(Fix& f, int row) {
    return s(wcell(f, row, 0) + "|" + wcell(f, row, 1) + "|" + wcell(f, row, 2) + "|" + wcell(f, row, 3) + "|" + wcell(f, row, 4));
}

// A synthetic program (no NEX): two modules, each with a static function `init` that has a static `done` and a local `x` (at fp - 2).
// a.c's init (at $8000) calls itself; b.c's init (at $8010) does not. The prologue is `push ix; ld ix,0; add ix,sp`, then two `push hl`.
std::string syn_rec(const std::string& r, const char* module) {
    return "__CDBINFO__" + esc(r) + " = $0001 ; const, public, , " + module + ", code_compiler, t.c:1\n";
}
bool syn_load(Fix& f, const char* name) {
    f.dir = fs::path(g_tmp->path().toStdString()) / name;
    fs::create_directories(f.dir);
    const std::string map = (f.dir / "syn.map").string();
    std::ofstream(map) << "_init = $8000 ; addr, local, , a_c, code_compiler, a.c::init::0::0:1\n"
                          "_init = $8010 ; addr, local, , b_c, code_compiler, b.c::init::0::0:1\n"
                          "__C_LINE_1_a_2ec = $8000 ; addr, local, , a_c, code_compiler, a.c::init::0::0:1\n"
                          "__C_LINE_1_b_2ec = $8010 ; addr, local, , b_c, code_compiler, b.c::init::0::0:1\n"
                          "__code_compiler_tail = $8040 ; const, public, def, , ,\n"
                          "_st_init_done = $9000 ; addr, local, , a_c, bss_compiler, a.c:2\n"
                          "_st_init_done = $9010 ; addr, local, , b_c, bss_compiler, b.c:2\n"
                       << syn_rec("F:Fa.c$init$0_0$0({0}DF,SI:S),C,0,0,0,0,0", "a_c") << syn_rec("F:Fb.c$init$0_0$0({0}DF,SI:S),C,0,0,0,0,0", "b_c")
                       << syn_rec("S:Fa.c$st_init_done$1_0$1({2}SI:S),E,0,0", "a_c") << syn_rec("S:Fb.c$st_init_done$1_0$1({2}SI:S),E,0,0", "b_c")
                       << syn_rec("S:La.c.init$x$1_0$1({2}SI:S),B,1,-2", "a_c") << syn_rec("S:Lb.c.init$x$1_0$1({2}SI:S),B,1,-2", "b_c");
    const uint8_t pro[] = {0xDD, 0xE5, 0xDD, 0x21, 0x00, 0x00, 0xDD, 0x39, 0xE5, 0xE5};
    for (uint16_t base : {0x8000, 0x8010})
        for (size_t i = 0; i < sizeof pro; ++i) f.emu.mmu().write(static_cast<uint16_t>(base + i), pro[i]);
    const uint8_t rec_call[] = {0xCD, 0x00, 0x80, 0x18, 0xFE};   // a.c's init: call init; jr $
    for (size_t i = 0; i < sizeof rec_call; ++i) f.emu.mmu().write(static_cast<uint16_t>(0x800A + i), rec_call[i]);
    f.emu.mmu().write(0x801A, 0x18);
    f.emu.mmu().write(0x801B, 0xFE);
    f.emu.mmu().write(0x9000, 7);
    f.emu.mmu().write(0x9010, 9);
    return f.backend->load_source_map(map, true).count > 0;
}
void syn_enter(Fix& f, uint16_t pc, uint16_t sp, int steps = 5) {   // run `steps` instructions from `pc` with SP = `sp`
    Z80Registers r = f.emu.cpu().get_registers();
    r.PC = pc;
    r.SP = sp;
    r.IFF1 = r.IFF2 = 0;
    f.emu.cpu().set_registers(r);
    for (int i = 0; i < steps; ++i) f.emu.execute_single_instruction();
    f.settle();
}

// vars.c:41, sum's first call (sccz80, master): the stop every row starts from.
constexpr uint16_t kS41 = 0x8D3E;

// ── GH #437: editing ──────────────────────────────────────────────────────────────────────────────────
QLineEdit* editor(Fix& f) { return f.tree()->viewport()->findChild<QLineEdit*>(); }
void settle_editor() {
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
}
// A double-click on the Value cell; true when an editor opened.
bool dbl(Fix& f, QTreeWidgetItem* it) {
    if (!it) return false;
    emit f.tree()->itemDoubleClicked(it, 2);
    QApplication::processEvents();
    return editor(f) != nullptr;
}
// Type `t` into the open editor and press Enter.
void commit(Fix& f, const QString& t) {
    QLineEdit* e = editor(f);
    if (!e) return;
    e->setText(t);
    QTest::keyClick(e, Qt::Key_Return);
    settle_editor();
}
QString editor_text(Fix& f) { return editor(f) ? editor(f)->text() : QStringLiteral("<no editor>"); }
void cancel(Fix& f) {
    if (QLineEdit* e = editor(f)) QTest::keyClick(e, Qt::Key_Escape);
    settle_editor();
}
QLabel* message(Fix& f) { return f.vp()->findChild<QLabel*>("variables_message"); }
bool editable(QTreeWidgetItem* it) { return it && (it->flags() & Qt::ItemIsEditable); }
std::vector<uint8_t> cpu_bytes(Fix& f, uint32_t a, size_t n) {
    std::vector<uint8_t> b;
    for (size_t i = 0; i < n; ++i) b.push_back(f.emu.mmu().peek(static_cast<uint16_t>(a + i)));
    return b;
}
// The debugger channel's lines while `fn` runs.
std::string log_of(const std::function<void()>& fn) {
    auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(64);
    Log::debugger()->sinks().push_back(ring);
    fn();
    Log::debugger()->sinks().pop_back();
    std::string all;
    for (const auto& l : ring->last_formatted()) all += l;
    return all;
}
size_t count_of(const std::string& hay, const std::string& needle) {
    size_t n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
    return n;
}
using Bytes = std::vector<uint8_t>;
// Manager ticks until one redraws nothing (a new stop is drawn by the first), then the redraw count starts from 0.
bool quiesce(Fix& f) {
    for (int i = 0; i < 6; ++i) {
        (void)f.mgr->take_panel_refresh_count();
        f.mgr->refresh_panels();
        if (f.mgr->take_panel_refresh_count() == 0) return true;
    }
    return false;
}

void test_rows() {
    const std::string k15 = std::string("1") + ".5";   // a bare 1.5 literal is shaped like a row ID

    {   // QVAR-01
        Fix f;
        f.load("master/sccz80-cvars", "q01");
        const bool at = f.run_to(kS41);
        auto* tabs = qobject_cast<QTabWidget*>(f.vp()->parentWidget() ? f.vp()->parentWidget()->parentWidget() : nullptr);
        QStringList tab_names;
        for (int i = 0; tabs && i < tabs->count(); ++i) tab_names << tabs->tabText(i);
        // not paused: nothing is read or redrawn
        QTreeWidgetItem* a = child(locals(f), "a");
        const QString before = val(a);
        if (a) a->setText(2, "SENTINEL");
        f.vp()->set_paused(false);
        f.vp()->refresh();
        const bool untouched = val(a) == "SENTINEL";
        f.vp()->set_paused(true);
        f.vp()->refresh();
        check("QVAR-01", "the bottom-right tabs are Watches, Variables, Breakpoints in that order and the Variables panel is the second; a refresh while the panel is not "
                         "paused redraws nothing, and the next paused one does",
              at && tabs && tab_names == QStringList({"Watches", "Variables", "Breakpoints"}) && tabs->widget(1) == f.vp() && a && before == "0" && untouched &&
                  val(a) == "0",
              s(tab_names.join("|")) + " before=" + s(before));
    }

    {   // QVAR-02
        bool ok = true;
        std::string seen;
        const auto judge = [&](Fix& f, const char* what) {
            f.dbg()->refresh_panels();
            const std::string why = f.backend->c_variables_status().why;
            const bool good = !f.backend->c_variables_status().available && !why.empty() && s(f.status()->text()) == why && !f.status()->isHidden() &&
                              f.tree()->topLevelItem(0)->childCount() == 0 && f.tree()->topLevelItem(1)->childCount() == 0;
            if (!good) {
                ok = false;
                seen += std::string(" ") + what + "[" + why + " | " + s(f.status()->text()) + "]";
            }
        };
        {
            Fix f;
            judge(f, "no map");
        }
        {
            Fix f;
            const std::string sld = (fs::path(g_tmp->path().toStdString()) / "q02.sld").string();
            std::ofstream(sld) << "|SLD.data.version|1\nmain.bas|10||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
                                  "main.bas|10||0|0|32768|T|\n";
            f.backend->load_source_map(sld, true);
            judge(f, "sld");
        }
        {
            Fix f;
            f.load("v2.4/sdcc-SO2", "q02c");
            judge(f, "no records");
        }
        check("QVAR-02", "with no source map, with an SLD map and with v2.4/sdcc-SO2's map (a z88dk map with no C records) the tab shows the facade's c_variables_status text "
                         "and an empty tree: never an empty tree with nothing said",
              ok, seen);
    }

    {   // QVAR-03
        Fix f;
        f.load("master/sccz80-cvars", "q03");
        const bool at = f.run_to(kS41);
        const auto rows = f.backend->c_locals(0).rows;
        QStringList want;
        for (const auto& r : rows) want << QString::fromStdString(r.name);
        QTreeWidgetItem* a = child(locals(f), "a");
        QTreeWidgetItem* k = child(locals(f), "k");
        QTreeWidgetItem* t = child(locals(f), "t");
        QTreeWidgetItem* acc = child(locals(f), "acc");
        QTreeWidgetItem* calls = child(locals(f), "calls");
        QTreeWidgetItem* p = child(locals(f), "p");
        check("QVAR-03", "at vars.c:41 the frame selector is `sum at vars.c:41 (current)` and Locals lists c_locals(0)'s names in its order, with a = 0, b = -5, k = 120 'x', "
                         "t = 122, acc = 345, calls = 1, no `inner`; types int, unsigned char, struct point *, long",
              at && s(f.frames()->currentText()) == "sum at vars.c:41 (current)" && !want.isEmpty() && names(locals(f)) == want.join(' ') && val(a) == "0" &&
                  val(child(locals(f), "b")) == "-5" && val(k) == "120 'x'" && val(t) == "122" && val(acc) == "345" && val(calls) == "1" &&
                  !child(locals(f), "inner") && ty(a) == "int" && ty(k) == "unsigned char" && ty(p) == "struct point *" && ty(acc) == "long" && f.status()->isHidden(),
              s(f.frames()->currentText()) + " | " + s(names(locals(f))));
    }

    {   // QVAR-04
        Fix f;
        f.load("master/sccz80-cvars", "q04");
        const bool at = f.run_to(kS41);
        check("QVAR-04", "at vars.c:41 Globals has the group vars.c with g_int -5, g_uint 40000, g_char 65 'A', g_float 1.5, g_str \"hi\\0\", g_matrix {{1, 2, 3}, {4, 5, 6}}, "
                         "s_file 9, s_dup 0, the static local sum::calls 1, and the group other.c with s_dup 4",
              at && val(global(f, "vars.c", "g_int")) == "-5" && val(global(f, "vars.c", "g_uint")) == "40000" && val(global(f, "vars.c", "g_char")) == "65 'A'" &&
                  val(global(f, "vars.c", "g_float")) == QString::fromStdString(k15) && val(global(f, "vars.c", "g_str")) == "\"hi\\0\"" &&
                  val(global(f, "vars.c", "g_matrix")) == "{{1, 2, 3}, {4, 5, 6}}" && val(global(f, "vars.c", "s_file")) == "9" &&
                  val(global(f, "vars.c", "s_dup")) == "0" && val(global(f, "other.c", "s_dup")) == "4" && val(global(f, "vars.c", "sum::calls")) == "1",
              detail_of(global(f, "vars.c", "g_float")) + " " + detail_of(global(f, "other.c", "s_dup")));
    }

    {   // QVAR-05
        Fix f;
        f.load("master/sccz80-cvars", "q05");
        const bool at = f.run_to(kS41);
        const int rows_before = f.cs_table()->rowCount();
        f.cs_table()->setCurrentCell(0, 0);   // the user picks the newest call: main's CALL of sum
        QApplication::processEvents();
        QTreeWidgetItem* lp = child(locals(f), "lp");
        check("QVAR-05", "choosing Call Stack row 0 (the main-to-sum CALL) shows frame 1: the selector is on `main at vars.c:56` and Locals has lp = {x = 3, y = 4, tag = 5}, "
                         "i = 0, c = 120 'x', big = 100000 (row i is frame i + 1)",
              at && rows_before >= 1 && f.vp()->selected_frame() == 1 && s(f.frames()->currentText()) == "main at vars.c:56" &&
                  val(lp) == "{x = 3, y = 4, tag = 5}" && val(child(locals(f), "i")) == "0" && val(child(locals(f), "c")) == "120 'x'" &&
                  val(child(locals(f), "big")) == "100000",
              s(f.frames()->currentText()) + " | " + detail_of(lp));
    }

    {   // QVAR-06
        Fix f;
        f.load("master/sccz80-cvars", "q06");
        const bool at = f.run_to(kS41);
        QSignalSpy spy(f.vp(), &VariablesPanel::frame_changed);
        emit f.frames()->activated(1);
        QApplication::processEvents();
        const bool row0 = f.cs_table()->currentRow() == 0 && f.vp()->call_stack_row() == 0;
        const int n1 = spy.count();
        f.dbg()->refresh_panels();
        const bool still = f.cs_table()->currentRow() == 0 && f.vp()->selected_frame() == 1;
        emit f.frames()->activated(0);
        QApplication::processEvents();
        const bool cleared = f.cs_table()->currentRow() == -1 && f.vp()->call_stack_row() == -1 && f.vp()->selected_frame() == 0;
        const int n2 = spy.count();
        // a frame the tracker has no CALL for (found through the saved frame pointers) has no Call Stack row
        f.emu.call_stack().clear();
        f.dbg()->refresh_panels();
        f.vp()->select_frame(1);
        const bool chain = f.frames()->count() >= 2 && f.vp()->selected_frame() == 1 && f.vp()->call_stack_row() == -1 && f.cs_table()->rowCount() == 0 &&
                           f.cs_table()->currentRow() == -1;
        check("QVAR-06", "choosing frame 1 in the selector selects Call Stack row 0 and choosing frame 0 clears the Call Stack selection; frame_changed fires once per choice "
                         "(no loop between the two) and a refresh keeps the row in step; with the tracker emptied, frame 1 (found through the frame pointers) has no row",
              at && row0 && n1 == 1 && still && cleared && n2 == 2 && chain,
              "n1=" + std::to_string(n1) + " total=" + std::to_string(spy.count()) + " chain=" + std::to_string(chain));
    }

    {   // QVAR-07
        Fix f;
        f.load("master/sccz80-cvars", "q07");
        const bool at = f.run_to(kS41);
        f.cs_table()->setCurrentCell(0, 0);
        QApplication::processEvents();
        f.mgr->refresh_panels();                                              // an idle paused tick
        f.dbg()->set_address_style(jnext::dbg::AddressStyle::Physical);       // a style switch (it refreshes)
        f.dbg()->set_address_style(jnext::dbg::AddressStyle::Friendly);
        const bool kept = f.vp()->selected_frame() == 1 && val(child(locals(f), "i")) == "0";
        const bool at2 = f.run_to(kS41);   // sum's second call
        check("QVAR-07", "frame 1 stays chosen over an idle paused tick and a style switch; continuing to the second vars.c:41 is a new stop and shows frame 0 (sum) with "
                         "a = 1, t = 123 (i + 122), calls = 2",
              at && kept && at2 && f.vp()->selected_frame() == 0 && s(f.frames()->currentText()) == "sum at vars.c:41 (current)" && val(child(locals(f), "a")) == "1" &&
                  val(child(locals(f), "t")) == "123" && val(child(locals(f), "calls")) == "2" && f.cs_table()->currentRow() == -1,
              std::to_string(f.vp()->selected_frame()) + " " + s(f.frames()->currentText()));
    }

    {   // QVAR-08
        Fix f;
        f.load("master/sccz80-cvars", "q08");
        const bool at = f.run_to(kS41);
        f.vp()->select_frame(1);
        QTreeWidgetItem* lp = child(locals(f), "lp");
        const bool lazy = lp && lp->childIndicatorPolicy() == QTreeWidgetItem::ShowIndicator && lp->childCount() == 0;
        expand(lp);
        const bool lp_ok = lp && lp->childCount() == 3 && val(child(lp, "x")) == "3" && ty(child(lp, "x")) == "int" && val(child(lp, "y")) == "4" &&
                           val(child(lp, "tag")) == "5" && ty(child(lp, "tag")) == "unsigned char";
        f.vp()->select_frame(0);
        QTreeWidgetItem* p = child(locals(f), "p");
        expand(p);
        bool paths = p && p->childCount() == 3 && val(child(p, "x")) == "3" && val(child(p, "y")) == "4" && val(child(p, "tag")) == "5";
        for (int i = 0; p && i < p->childCount(); ++i) {
            const Debugger::CValueResult r = f.backend->read_c_value(0, s(p->child(i)->data(0, VariablesPanel::RolePath).toString()));
            paths = paths && r.ok && QString::fromStdString(r.value) == p->child(i)->text(2) && p->child(i)->data(0, VariablesPanel::RolePath).toString().startsWith("p->");
        }
        check("QVAR-08", "children are read on expansion: frame 1's lp has the indicator and no children until expanded, then x 3 (int), y 4, tag 5 (unsigned char); frame 0's p "
                         "expands to x, y, tag = 3, 4, 5 whose paths (p->x ...) read back through read_c_value to the shown text",
              at && lazy && lp_ok && paths, detail_of(p));
    }

    {   // QVAR-09
        Fix f;
        f.load("master/sccz80-cvars", "q09");
        const bool at = f.run_to(kS41);
        QTreeWidgetItem* m = global(f, "vars.c", "g_matrix");
        expand(m);
        QTreeWidgetItem* r1 = child(m, "[1]");
        expand(r1);
        QTreeWidgetItem* str = global(f, "vars.c", "g_str");
        expand(str);
        check("QVAR-09", "g_matrix expands to [0] and [1], and [1] to 4, 5, 6; g_str expands to [0] 104 'h', [1] 105 'i', [2] 0",
              at && names(m) == "[0] [1]" && val(r1) == "{4, 5, 6}" && names(r1) == "[0] [1] [2]" && val(child(r1, "[0]")) == "4" && val(child(r1, "[1]")) == "5" &&
                  val(child(r1, "[2]")) == "6" && names(str) == "[0] [1] [2]" && val(child(str, "[0]")) == "104 'h'" && val(child(str, "[1]")) == "105 'i'" &&
                  val(child(str, "[2]")) == "0",
              s(names(m)) + " | " + s(names(str)));
    }

    {   // QVAR-10
        Fix f;
        f.load("master/sccz80-cvars", "q10");
        const bool at = f.run_to(0x8E1F);   // main's `while (1) g_int++`, vars.c:64: g_head = &g_node, g_node.val = 100000
        QTreeWidgetItem* head = global(f, "vars.c", "g_head");
        const bool named = head && val(head).contains("<g_node>");
        expand(head);
        QTreeWidgetItem* next = child(head, "next");
        QTreeWidgetItem* v = child(head, "val");
        const bool kids = next && v && val(next) == "$0000 (NULL)" && val(v) == "100000";
        expand(next);
        QTreeWidgetItem* nn = next && next->childCount() == 1 ? next->child(0) : nullptr;
        check("QVAR-10", "at vars.c:64 g_head shows `<g_node>` after its address; expanded: next `$0000 (NULL)` and val 100000; `next` expanded has one item whose Value is the "
                         "null-pointer refusal (`null pointer`), not 0",
              at && named && kids && nn && val(nn) == "null pointer", detail_of(head) + " | " + detail_of(nn));
    }

    {   // QVAR-11
        bool ok = true;
        std::string seen;
        {   // sccz80: in l_debug_push_frame, sum's first call: frame 0 is not C code; frame 1 (sum) has t and acc not allocated yet
            Fix f;
            f.load("master/sccz80-cvars", "q11a");
            const bool at = f.run_to(0x8CB9) && f.run_to(0x8B64);   // _sum's entry first: main calls the helper too, earlier
            const std::string why0 = f.backend->c_locals(0).why;
            QTreeWidgetItem* msg = locals(f)->childCount() == 1 ? locals(f)->child(0) : nullptr;
            const bool frame0 = at && !why0.empty() && f.frames()->currentText().startsWith("<not C code>") && msg && val(msg) == QString::fromStdString(why0);
            f.cs_table()->setCurrentCell(0, 0);   // sum's CALL of the helper: frame 1 is sum
            QApplication::processEvents();
            const bool frame1 = f.vp()->selected_frame() == 1 && val(child(locals(f), "t")) == "not allocated" && val(child(locals(f), "acc")) == "not allocated" &&
                                val(child(locals(f), "t")) != "0";
            // a refusal is drawn in the placeholder colour, a value is not
            QTreeWidgetItem* tt = child(locals(f), "t");
            QTreeWidgetItem* aa = child(locals(f), "a");
            const bool tint = tt && aa && tt->foreground(2).color() == f.vp()->palette().color(QPalette::PlaceholderText) && aa->data(2, Qt::ForegroundRole).isNull() &&
                              val(aa) != "";
            if (!tint) {
                ok = false;
                seen += " tint";
            }
            if (!frame0 || !frame1) {
                ok = false;
                seen += " sccz80[" + std::to_string(frame0) + std::to_string(frame1) + " " + detail_of(child(locals(f), "t")) + "]";
            }
        }
        {   // zsdcc: acc is optimised out, t is in BC with the may-be-stale note
            Fix f;
            f.load("master/sdcc-cvars", "q11b");
            const bool at = f.run_to(0x8C97);
            QTreeWidgetItem* acc = child(locals(f), "acc");
            QTreeWidgetItem* t = child(locals(f), "t");
            const bool good = at && val(acc) == "optimised out" && val(t).startsWith("122") && val(t).contains("may be stale") && addr(t) == "BC";
            if (!good) {
                ok = false;
                seen += " sdcc[" + detail_of(acc) + " " + detail_of(t) + "]";
            }
        }
        check("QVAR-11", "a refusal is text: at $8B64 (sccz80, sum's first call) frame 0 shows the facade's not-C-code text and frame 1's t and acc say `not allocated`, never 0; "
                         "on zsdcc at vars.c:41 acc says `optimised out` and t shows 122 at Address BC with the may-be-stale note",
              ok, seen);
    }

    {   // QVAR-12
        Fix f;
        f.load("master/sccz80-cvars", "q12");
        const bool at = f.run_to(kS41);
        const bool next = f.backend->machine().type == MachineType::ZXN_ISSUE2;
        QTreeWidgetItem* sd = global(f, "vars.c", "s_dup");
        const QString friendly = addr(sd);
        // a stack slot: `$XXXX <tag of the memory mapped there now>`, XXXX = the frame pointer (sum$a is at +10)
        const uint16_t xxxx = static_cast<uint16_t>(f.peek16(0x906A) + 10);
        const auto mmu = f.backend->mmu_slots();
        const auto spell = [&](jnext::dbg::AddressStyle st) {
            return QString::fromStdString(jnext::dbg::long_address(xxxx, mmu[xxxx >> 13].now, st, next));
        };
        const QString a_friendly = addr(child(locals(f), "a"));
        f.dbg()->set_address_style(jnext::dbg::AddressStyle::Physical);
        const QString physical = addr(global(f, "vars.c", "s_dup"));
        const QString a_physical = addr(child(locals(f), "a"));
        check("QVAR-12", "s_dup of vars.c is `$909F P04`; Debug > Physical Addresses makes it `$909F $04909F` ((4 + $20) << 13 | $109F, long_address.cpp:124); a stack slot (a, "
                         "at frame pointer + 10) is spelled with the tag of the memory mapped at it now, and in Physical with that memory's SRAM text",
              at && next && friendly == "$909F P04" && physical == "$909F $04909F" && a_friendly == spell(jnext::dbg::AddressStyle::Friendly) &&
                  a_physical == spell(jnext::dbg::AddressStyle::Physical) && a_friendly != a_physical,
              s(friendly) + " " + s(physical) + " | " + s(a_friendly) + " vs " + s(spell(jnext::dbg::AddressStyle::Friendly)) + " | " + s(a_physical));
    }

    {   // QVAR-13
        Fix f;
        f.load("master/sccz80-cvars-banked", "q13");
        const bool at = f.step_to(0x8CE8);   // bkmain.c:8, main's loop, after bk_sum returned: bank 20 is unmapped again
        QTreeWidgetItem* bt = global(f, "bk.c", "btable");
        const QString friendly = addr(bt);
        const QString shown = val(bt);
        const QString tip = bt ? bt->toolTip(2) : QString();
        f.dbg()->set_address_style(jnext::dbg::AddressStyle::Physical);
        check("QVAR-13", "banked, in main's loop: btable = {10, 20, 30, 40} at `$C000 P14` (Physical `$C000 $068000`), and the facade's `page 20 is not mapped at $C000 now` note "
                         "is in the Value text and the tooltip",
              at && shown.startsWith("{10, 20, 30, 40}") && shown.contains("page 20 is not mapped at $C000 now") && tip.contains("page 20 is not mapped at $C000 now") &&
                  friendly == "$C000 P14" && addr(global(f, "bk.c", "btable")) == "$C000 $068000",
              detail_of(bt));
    }

    {   // QVAR-14
        Fix f;
        f.load("master/sccz80-cvars", "q14");
        const bool at = f.run_to(0x8CC7);   // vars.c:34, `t = a + b + k` about to run
        QTreeWidgetItem* p = child(locals(f), "p");
        expand(p);
        QTreeWidgetItem* b = child(locals(f), "b");
        f.tree()->setCurrentItem(b);
        const bool before = p && p->childCount() == 3;
        const bool at2 = f.run_to(0x8CE8);   // vars.c:35: t = 0 + -5 + 120 has run
        QTreeWidgetItem* p2 = child(locals(f), "p");
        QTreeWidgetItem* cur = f.tree()->currentItem();
        check("QVAR-14", "a refresh after a step keeps what the user opened: stopped at vars.c:34 with p expanded and b current, at vars.c:35 t = 115 (0 + -5 + 120), p is still "
                         "expanded with x 3 and b is still the current item",
              at && before && at2 && val(child(locals(f), "t")) == "115" && p2 && p2->isExpanded() && val(child(p2, "x")) == "3" && cur && cur->text(0) == "b" &&
                  f.vp()->selected_frame() == 0,
              detail_of(child(locals(f), "t")) + " | " + (cur ? s(cur->text(0)) : "<no current>"));
    }

    {   // QVAR-15
        Fix f;
        f.load("master/sccz80-cvars", "q15");
        const bool at = f.run_to(kS41);
        expand(child(locals(f), "p"));
        f.mgr->refresh_panels();
        (void)f.mgr->take_panel_refresh_count();
        f.mgr->refresh_panels();
        f.mgr->refresh_panels();
        const uint64_t idle = f.mgr->take_panel_refresh_count();
        f.backend->c_frames();   // settle any pending recheck
        const uint64_t rev = f.backend->state_revision();
        f.vp()->refresh();
        f.vp()->refresh();
        check("QVAR-15", "idle (GH #428): at vars.c:41 with p expanded, two idle paused ticks redraw nothing (take_panel_refresh_count() == 0), and redrawing the Variables tab "
                         "leaves the backend's state_revision() unchanged",
              at && idle == 0 && f.backend->state_revision() == rev, "idle=" + std::to_string(idle));
    }

    {   // QVAR-22
        // a 300-byte global added to the committed map: one record the way the compiler writes it (every character that is not a letter is
        // `_xx`), and its symbol at $9200 (page 4). Memory is poked with (i % 30) + 1, so every element is a plain number.
        Fix f;
        f.load("master/sccz80-cvars", "q22", [&](const fs::path& d) {
            std::ofstream out(d / "prog.map", std::ios::app);
            out << "__CDBINFO__" << esc("S:G$g_big$0_0$0({300}DA300x,SC:U),E,0,0") << " = $0001 ; const, public, , vars_c, code_compiler, vars.c::main::1::6:66\n"
                << "_g_big = $9200 ; addr, public, , vars_c, bss_compiler, vars.c::main::1::6:66\n";
        });
        const bool at = f.run_to(kS41);
        for (int i = 0; i < 300; ++i) f.emu.mmu().write(static_cast<uint16_t>(0x9200 + i), static_cast<uint8_t>(i % 30 + 1));
        f.dbg()->refresh_panels();
        QTreeWidgetItem* big = global(f, "vars.c", "g_big");
        expand(big);
        const QString ranges = names(big);
        QTreeWidgetItem* last = child(big, "[200..299]");
        expand(last);
        bool elems = last && last->childCount() == 100 && last->child(0)->text(0) == "[200]" && last->child(99)->text(0) == "[299]";
        for (int i = 0; elems && i < 100; ++i) elems = last->child(i)->text(2) == QString::number((200 + i) % 30 + 1);
        QTreeWidgetItem* first = child(big, "[0..99]");
        expand(first);
        check("QVAR-22", "a 300-element unsigned char global expands to the ranges [0..99], [100..199] and [200..299] (100 elements each, not 99 or 101); [200..299] expands to "
                         "[200] to [299], each the byte poked at its address ((i % 30) + 1)",
              at && big && ranges == "[0..99] [100..199] [200..299]" && elems && first && first->childCount() == 100 && first->child(0)->text(0) == "[0]" &&
                  first->child(0)->text(2) == "1",
              s(ranges) + " " + detail_of(last));
    }

    {   // QVAR-16
        Fix f;
        f.load("master/sccz80-cvars", "q16");
        const bool at = f.run_to(kS41);
        const bool pinned = f.vp()->watch_item(child(locals(f), "t"));
        const bool first = wtable(f)->rowCount() == 1 && wcell(f, 0, 1) == "t (in sum)" && wcell(f, 0, 2) == "int" && wcell(f, 0, 3) == "122" &&
                           wcell(f, 0, 0).startsWith("$") && !wcell(f, 0, 4).isEmpty();
        const bool at2 = f.run_to(kS41);
        const bool second = wcell(f, 0, 3) == "123";
        const bool at3 = f.run_to(0x8E1F);   // main's loop: sum is not on the call stack
        check("QVAR-16", "Watch on sum's local t adds a Watches row `t (in sum)`, int, 122 with its address and memory; at the second vars.c:41 it is 123; at vars.c:64 (sum is "
                         "not on the call stack) it says `out of scope: sum is not on the call stack` with empty Address and Memory",
              at && pinned && first && at2 && second && at3 && wcell(f, 0, 3) == "out of scope: sum is not on the call stack" && wcell(f, 0, 0).isEmpty() &&
                  wcell(f, 0, 4).isEmpty(),
              wrow(f, 0));
    }

    {   // QVAR-17
        Fix f;
        f.load("master/sccz80-cvars", "q17");
        const bool at = f.run_to(kS41);
        f.vp()->select_frame(1);
        const bool pinned = f.vp()->watch_item(child(locals(f), "i"));
        const bool first = wcell(f, 0, 1) == "i (in main)" && wcell(f, 0, 3) == "0";   // read in main's frame while sum is frame 0
        const bool at2 = f.run_to(kS41);
        const bool second = wcell(f, 0, 3) == "1";
        const bool at3 = f.run_to(0x8E1F);   // main is frame 0 now; the loop ended with i = 4
        check("QVAR-17", "Watch on main's i (chosen in frame 1 while sum runs) is 0, then 1 at the second vars.c:41, then 4 at vars.c:64 where main is frame 0: a local follows its "
                         "function, not a frame index",
              at && pinned && first && at2 && second && at3 && wcell(f, 0, 3) == "4", wrow(f, 0));
    }

    {   // QVAR-18
        Fix f;
        f.load("master/sccz80-cvars", "q18");
        const bool at = f.run_to(kS41);
        const bool p1 = f.vp()->watch_item(global(f, "vars.c", "s_dup"));
        const bool p2 = f.vp()->watch_item(child(locals(f), "calls"));   // a static local of sum, pinned from Locals
        const bool first = wcell(f, 0, 3) == "0" && wcell(f, 1, 1) == "sum::calls" && wcell(f, 1, 3) == "1";
        const bool at2 = f.run_to(0x8E49);   // inside other(): a bare s_dup there is other.c's, 4
        const bool inside = wcell(f, 0, 3) == "0" && wcell(f, 1, 3) == "4";
        const bool at3 = f.run_to(0x8E1F);
        check("QVAR-18", "a file static is not shadowed: Watch on vars.c's s_dup is 0 at vars.c:41, still 0 inside other() (where a bare s_dup is other.c's 4) and 178 at vars.c:64; a "
                         "static local pinned from Locals (sum::calls) is unbound: 1, then 4 at vars.c:64 where sum is not running",
              at && p1 && p2 && first && at2 && inside && at3 && wcell(f, 0, 3) == "178" && wcell(f, 1, 3) == "4", wrow(f, 0) + " / " + wrow(f, 1));
    }

    {   // QVAR-19
        Fix f;
        f.load("master/sccz80-cvars", "q19");
        const bool at = f.run_to(0x8E1F);
        f.vp()->watch_item(global(f, "vars.c", "g_int"));
        f.dbg()->watch_panel()->add_watch(0x903E, "", 1);   // the same word, an address watch: the independent reader
        const QString c_before = wcell(f, 0, 3);
        const QString w_before = wcell(f, 1, 3);
        f.backend->run(f.me);
        f.mgr->check_breakpoint_hit();
        for (int i = 0; i < 5; ++i) {
            f.emu.run_frame();
            f.dbg()->refresh_panels();
        }
        const bool moved = wcell(f, 1, 3) != w_before;
        const bool kept = wcell(f, 0, 3) == c_before;
        f.backend->pause(f.me);
        f.settle();
        const QString w_now = wcell(f, 1, 3);   // "$XXXX (Nd)"
        bool okhex = false;
        const int v = w_now.mid(1, 4).toInt(&okhex, 16);
        const QString want = QString::number(static_cast<int16_t>(v));
        check("QVAR-19", "running: a C watch on g_int and a Word address watch on $903E, five frames with refresh ticks: the address watch's value moves and the C row keeps -5; after "
                         "the pause the C row is the address watch's value as a signed 16-bit number",
              at && c_before == "-5" && moved && kept && okhex && wcell(f, 0, 3) == want && want != "-5", s(wcell(f, 0, 3)) + " vs " + s(want) + " (" + s(w_now) + ")");
    }

    {   // QVAR-20
        Fix f;
        f.load("master/sccz80-cvars", "q20");
        const bool at = f.run_to(kS41);
        f.vp()->watch_item(child(locals(f), "t"));
        f.dbg()->watch_panel()->add_watch(0x903E, "mem", 1);
        wtable(f)->setCurrentCell(0, 0);
        bool dialog = false;
        QTimer closer;
        QObject::connect(&closer, &QTimer::timeout, [&]() {
            if (auto* d = QApplication::activeModalWidget()) {
                dialog = true;
                d->close();
            }
        });
        closer.start(20);
        f.dbg()->watch_panel()->on_edit_watch();
        QTest::qWait(60);
        closer.stop();
        const bool kept = !dialog && wcell(f, 0, 1) == "t (in sum)" && wcell(f, 0, 3) == "122";
        f.dbg()->watch_panel()->remove_selected();   // the C row is the current one
        check("QVAR-20", "Edit with a C row selected opens no dialog and the row stays a C row; Remove deletes it and the address row beside it is unchanged",
              at && kept && wtable(f)->rowCount() == 1 && wcell(f, 0, 1) == "mem" && wcell(f, 0, 2) == "Word", wrow(f, 0));
    }

    {   // QVAR-21
        Fix f;
        f.load("master/sccz80-cvars", "q21");
        const bool at = f.run_to(kS41);
        f.vp()->watch_item(child(locals(f), "t"));
        f.vp()->watch_item(global(f, "vars.c", "g_int"));
        const bool had = wcell(f, 0, 3) == "122" && wcell(f, 1, 3) == "-5";
        const std::string map = (f.dir / "prog.map").string();
        f.backend->clear_source_map();
        f.dbg()->refresh_panels();
        const std::string why = f.backend->c_variables_status().why;
        const bool gone = !why.empty() && wcell(f, 0, 3) == QString::fromStdString(why) && wcell(f, 1, 3) == QString::fromStdString(why) &&
                          locals(f)->childCount() == 0 && globals(f)->childCount() == 0;
        f.backend->load_source_map(map, true);
        f.dbg()->refresh_panels();
        check("QVAR-21", "when the store goes the C rows show the status text, never the last value, and both Variables sections are empty; when the map is loaded again the rows bind "
                         "again (t = 122, g_int = -5)",
              at && had && gone && wcell(f, 0, 3) == "122" && wcell(f, 1, 3) == "-5", wrow(f, 0) + " / " + wrow(f, 1));
    }

    {   // QVAR-27
        Fix f;
        f.load("master/sccz80-cvars", "q27");
        const bool at = f.run_to(kS41);
        f.vp()->set_paused(false);   // the machine runs: a choice is ignored
        f.frames()->setCurrentIndex(1);   // what a click on the selector does first
        emit f.frames()->activated(1);
        f.cs_table()->setCurrentCell(0, 0);
        QApplication::processEvents();
        const bool ignored = f.vp()->selected_frame() == 0 && f.frames()->currentIndex() == 0 && f.cs_table()->currentRow() == -1;
        f.vp()->set_paused(true);
        check("QVAR-27", "a frame chosen in the selector or in the Call Stack while the machine runs is ignored, and both go back to the frame the tree shows (selector on frame 0, no "
                         "Call Stack row selected)",
              at && ignored, "frame=" + std::to_string(f.vp()->selected_frame()) + " combo=" + std::to_string(f.frames()->currentIndex()) +
                                 " row=" + std::to_string(f.cs_table()->currentRow()));
    }

    {   // QVAR-28
        // finding of review 3: a static local pinned from Locals must name ITS variable even when another module has a function of the same name
        Fix f;
        const bool loaded = syn_load(f, "q28");
        syn_enter(f, 0x8010, 0xFF00);   // in b.c's init, past its prologue
        QSignalSpy spy(f.vp(), &VariablesPanel::watch_requested);
        QTreeWidgetItem* done = child(locals(f), "done");
        const bool pinned = done && f.vp()->watch_item(done);
        const bool spelled = spy.count() == 1 && spy.at(0).at(0).value<CVarWatch>().path == "b.c::init::done" && spy.at(0).at(0).value<CVarWatch>().function.empty();
        const bool inside_b = wcell(f, 0, 3) == "9";
        syn_enter(f, 0x8000, 0xFE00);   // now in a.c's init: a bare `init::done` there is a.c's (7)
        const bool inside_a = wcell(f, 0, 3) == "9";
        Z80Registers r = f.emu.cpu().get_registers();
        r.PC = 0x7000;                   // in neither function
        f.emu.cpu().set_registers(r);
        f.dbg()->refresh_panels();
        check("QVAR-28", "a static local pinned from Locals is spelled as c_globals() spells it (b.c::init::done, the file because init is not unique, unbound) and reads b.c's 9 while "
                         "stopped in b.c's init, in a.c's init (where a bare init::done is a.c's 7) and in neither",
              loaded && pinned && spelled && inside_b && inside_a && wcell(f, 0, 3) == "9", wrow(f, 0));
    }

    {   // QVAR-29
        // a bound watch reads the NEWEST frame of its function and matches the module too: a.c's init recursing once (two frames), x poked per frame
        Fix f;
        const bool loaded = syn_load(f, "q29");
        syn_enter(f, 0x8000, 0xFF00, 3);
        const uint16_t ix1 = f.emu.cpu().get_registers().IX;
        for (int i = 0; i < 2 + 1 + 3; ++i) f.emu.execute_single_instruction();   // two pushes, the call, the next prologue
        const uint16_t ix2 = f.emu.cpu().get_registers().IX;
        for (int i = 0; i < 2; ++i) f.emu.execute_single_instruction();            // its two pushes: x is allocated
        f.settle();
        f.emu.mmu().write(static_cast<uint16_t>(ix1 - 2), 11);
        f.emu.mmu().write(static_cast<uint16_t>(ix1 - 1), 0);
        f.emu.mmu().write(static_cast<uint16_t>(ix2 - 2), 22);
        f.emu.mmu().write(static_cast<uint16_t>(ix2 - 1), 0);
        f.dbg()->watch_panel()->add_c_watch(CVarWatch{"x", "init", "a_c"});
        f.dbg()->watch_panel()->add_c_watch(CVarWatch{"x", "init", "b_c"});   // b.c's init is not on the call stack
        check("QVAR-29", "with a.c's init on the stack twice, a watch on its local x reads the newest activation (22, not the older 11); a watch bound to b.c's init (same function name, "
                         "another module) says out of scope",
              loaded && ix1 != ix2 && f.backend->c_frames().size() >= 2 && wcell(f, 0, 3) == "22" && wcell(f, 1, 3) == "out of scope: init is not on the call stack",
              "ix1=" + std::to_string(ix1) + " ix2=" + std::to_string(ix2) + " " + wrow(f, 0) + " / " + wrow(f, 1));
    }

    {   // QVAR-30
        Fix f;
        f.load("master/sccz80-cvars", "q30");
        const bool at = f.run_to(kS41);
        f.vp()->set_paused(false);   // the machine runs: the tab shows the last stop, and a binding taken now would not be what the user pointed at
        const bool refused = !f.vp()->watch_item(child(locals(f), "t")) && wtable(f)->rowCount() == 0;
        f.vp()->set_paused(true);
        const bool added = f.vp()->watch_item(child(locals(f), "t")) && wtable(f)->rowCount() == 1 && wcell(f, 0, 1) == "t (in sum)";
        check("QVAR-30", "Watch is refused while the machine runs (nothing is added) and works at the next pause",
              at && refused && added, wrow(f, 0));
    }

    {   // QVAR-31
        Fix f;
        f.load("master/sccz80-cvars", "q31");
        const bool at = f.run_to(kS41);
        f.vp()->watch_item(global(f, "vars.c", "s_dup"));
        f.vp()->watch_item(child(locals(f), "t"));
        const bool page = wcell(f, 0, 0) == "$909F" && wcell(f, 0, 4) == "P04" && wcell(f, 0, 2) == "unsigned char";
        // a rebuild of the table while the machine runs keeps the last stop's texts
        f.dbg()->watch_panel()->set_paused(false);
        f.dbg()->watch_panel()->add_watch(0x903E, "m", 1);
        const bool shown = wcell(f, 0, 3) == "0" && wcell(f, 1, 3) == "122" && wcell(f, 0, 2) == "unsigned char";
        f.dbg()->watch_panel()->set_paused(true);
        const bool at2 = f.run_to(0x8E1F);   // sum is not on the call stack
        QTableWidgetItem* oos = wtable(f)->item(1, 3);
        QTableWidgetItem* good = wtable(f)->item(0, 3);
        const bool tint = oos && good && oos->foreground().color() == f.dbg()->watch_panel()->palette().color(QPalette::PlaceholderText) &&
                          good->data(Qt::ForegroundRole).isNull();
        // the symbol goes from the map: a refusal with no type keeps the Type column
        std::ifstream in(f.dir / "prog.map");
        std::string text, line;
        while (std::getline(in, line))
            if (line.find("S_3aFvars_2ec_24s_5fdup_24") == std::string::npos) text += line + "\n";
        in.close();
        const std::string map2 = (f.dir / "nodup.map").string();
        std::ofstream(map2) << text;
        f.backend->load_source_map(map2, true);
        f.dbg()->refresh_panels();
        const bool kept = wcell(f, 0, 3).contains("s_dup") && wcell(f, 0, 3) != "0" && wcell(f, 0, 2) == "unsigned char";
        check("QVAR-31", "a global's C row has its address and memory ($909F, P04) and type; a table rebuild while running keeps the last stop's Values (0, 122); a refused row is drawn "
                         "in the placeholder colour and a valued one is not; a refusal that has no type keeps the row's last Type",
              at && page && shown && at2 && tint && kept, wrow(f, 0) + " / " + wrow(f, 1));
    }

    {   // QVAR-32
        Fix f;
        f.load("master/sdcc-cvars", "q32");
        const bool at = f.run_to(0x8C97);
        f.vp()->watch_item(child(locals(f), "t"));
        check("QVAR-32", "a watch on zsdcc's register local t shows its value with the may-be-stale note in the Value, the register names in Address and no Memory",
              at && wcell(f, 0, 3).startsWith("122") && wcell(f, 0, 3).contains("may be stale") && wcell(f, 0, 0) == "BC" && wcell(f, 0, 4).isEmpty(), wrow(f, 0));
    }

    {   // QVAR-23
        Fix f;
        f.load("master/sccz80-cvars", "q23");
        const bool at = f.run_to(kS41);
        QTreeWidgetItem* a = child(locals(f), "a");
        QTreeWidgetItem* p = child(locals(f), "p");
        f.backend->c_frames();
        const uint64_t rev = f.backend->state_revision();
        QSignalSpy spy(f.vp(), &VariablesPanel::frame_changed);
        if (a) a->setText(2, "SENTINEL");
        f.vp()->set_paused(false);   // the machine runs
        f.vp()->refresh();
        expand(p);                    // on_expanded
        f.vp()->select_frame(1);
        const bool still = a && val(a) == "SENTINEL" && p && p->childCount() == 0 && f.vp()->selected_frame() == 0 && spy.count() == 0 &&
                           s(f.frames()->currentText()) == "sum at vars.c:41 (current)" && f.backend->state_revision() == rev;
        f.vp()->set_paused(true);
        f.vp()->refresh();
        check("QVAR-23", "while the machine runs, refresh, expanding an item and choosing a frame read and redraw nothing: the tree keeps its text, the item gets no children, the frame "
                         "stays 0 with no frame_changed and no state_revision() bump; the next paused refresh reads again",
              at && still && val(a) == "0" && p->childCount() == 3, "a=" + s(val(a)));
    }

    {   // QVAR-24
        Fix f;
        f.load("master/sccz80-cvars", "q24");
        const bool at = f.run_to(kS41);
        QTreeWidgetItem* node = global(f, "vars.c", "g_node");
        expand(node);
        const bool zero = node && val(child(node, "val")) == "0";
        node->setExpanded(false);
        QApplication::processEvents();
        const bool at2 = f.run_to(0x8E1F);   // vars.c:64: g_node.val = 100000 has run
        node = global(f, "vars.c", "g_node");
        expand(node);
        check("QVAR-24", "a collapsed item's children are not kept: g_node.val is 0 when closed at vars.c:41, and opening it again at vars.c:64 reads 100000",
              at && zero && at2 && node && val(child(node, "val")) == "100000", detail_of(child(node, "val")));
    }

    {   // QVAR-25
        // main gets a second local `p` (an alias of lp), so that the two functions have an item with the same path
        Fix f;
        f.load("master/sccz80-cvars", "q25", [&](const fs::path& d) {
            add_to_map(d, "__CDBINFO__" + esc("S:Lvars.c.main$p$1_0$4({5}STpoint:S),B,1,-5") + kScope);
        });
        const bool at = f.run_to(kS41);
        QTreeWidgetItem* p = child(locals(f), "p");
        expand(p);
        f.vp()->select_frame(1);
        QTreeWidgetItem* mp = child(locals(f), "p");
        const bool other_closed = mp && !mp->isExpanded() && mp->childCount() == 0;   // main's p: same path, another function
        f.vp()->select_frame(0);
        QTreeWidgetItem* back = child(locals(f), "p");
        const bool reopened = back && back->isExpanded() && back->childCount() == 3 && val(child(back, "x")) == "3";
        // a file group the user opened is opened again when the store goes and comes back
        QTreeWidgetItem* grp = child(globals(f), "vars.c");
        expand(grp);
        const std::string map = (f.dir / "prog.map").string();
        f.backend->clear_source_map();
        f.dbg()->refresh_panels();
        const bool emptied = locals(f)->childCount() == 0 && globals(f)->childCount() == 0 && !f.status()->isHidden();
        f.backend->load_source_map(map, true);
        f.dbg()->refresh_panels();
        QTreeWidgetItem* grp2 = child(globals(f), "vars.c");
        check("QVAR-25", "what the user opened stays open when items are rebuilt, by key and per function: sum's p is open again after a trip to frame 1 (whose own p, the same path, "
                         "is closed); with the store gone both sections are empty and the status says why, and when it is back the vars.c group is open again",
              at && p && other_closed && reopened && emptied && grp2 && grp2->isExpanded() && f.status()->isHidden(),
              std::to_string(other_closed) + std::to_string(reopened) + std::to_string(emptied));
    }

    {   // QVAR-26
        Fix f;
        f.load("master/sccz80-cvars", "q26", [&](const fs::path& d) {
            add_to_map(d, "__CDBINFO__" + esc("S:G$g_vp$0_0$0({2}DG,SV:S),E,0,0") + kScope +
                              "_g_vp = $9300 ; addr, public, , vars_c, bss_compiler, vars.c::main::1::6:66\n");
        });
        const bool at = f.run_to(kS41);
        QTreeWidgetItem* cp = global(f, "vars.c", "g_cptr");
        QTreeWidgetItem* vp = global(f, "vars.c", "g_vp");
        const bool ind = cp && vp && cp->childIndicatorPolicy() == QTreeWidgetItem::ShowIndicator && vp->childIndicatorPolicy() == QTreeWidgetItem::DontShowIndicator;
        expand(cp);
        QTreeWidgetItem* star = cp && cp->childCount() == 1 ? cp->child(0) : nullptr;
        check("QVAR-26", "a pointer opens onto its pointee, named after it: g_cptr (const char *, \"abc\") has the indicator and one child `*g_cptr` = 97 'a'; a void * (g_vp) has "
                         "nothing to open and no indicator",
              at && ind && star && star->text(0) == "*g_cptr" && val(star) == "97 'a'", detail_of(star));
    }

    // ── QVARW-01..08 (GH #437): editing a value in place ──────────────────────────────────────────────────
    // Oracle: vars.c at the vars.c:41 stop of the first call (a = 0, k = 120 'x', t = 122, p = &lp, g_uint = 40000, g_float = 1.5, main's c = 'x'
    // and lp = {3, 4, 5}; `return t + (int16_t)acc` with acc = 345), the maps' addresses and record offsets (sccz80 t = fp-2), an independent peek
    // of the machine, and the debugger backend's own contract (one write = one MUTATE line and one state_revision() bump).
    {   // QVARW-01
        Fix f;
        f.load("master/sccz80-cvars", "w01");
        const bool at = f.run_to(kS41);
        const uint16_t fp = f.backend->c_frames()[0].fp;
        QTreeWidgetItem* t = child(locals(f), "t");
        const bool opened = dbl(f, t);
        const QString held = editor_text(f);
        cancel(f);
        const bool idle = quiesce(f);   // a key or a click is host input and redraws the window, so the redraw count is measured on commit_edit()   // the manager's own ticks for this stop (settle() redraws the window, not the manager's stamp)
        const uint64_t rev = f.backend->state_revision();
        f.vp()->commit_edit(t, "1000");
        const uint64_t rev1 = f.backend->state_revision();
        f.mgr->refresh_panels();   // the manager's next paused tick
        const uint64_t redraws = f.mgr->take_panel_refresh_count();
        check("QVARW-01", "sccz80 at vars.c:41: double-clicking t's Value opens an editor holding 122; committing 1000 bumps the revision by 1, the next manager tick redraws "
                          "once, t shows 1000 and the CPU bytes at fp-2 are E8 03",
              at && idle && opened && held == "122" && rev1 == rev + 1 && redraws == 1 && val(child(locals(f), "t")) == "1000" && cpu_bytes(f, fp - 2u, 2) == Bytes{0xE8, 0x03},
              s(held) + " rev " + std::to_string(rev) + "->" + std::to_string(rev1) + " redraws " + std::to_string(redraws) + " " + detail_of(child(locals(f), "t")));
    }
    {   // QVARW-02
        Fix f;
        f.load("master/sccz80-cvars", "w02");
        const bool at = f.run_to(kS41);
        const bool ok_k = dbl(f, child(locals(f), "k"));
        const QString k = editor_text(f);
        cancel(f);
        const bool ok_p = dbl(f, child(locals(f), "p"));
        const QString p = editor_text(f);
        cancel(f);
        const bool ok_f = dbl(f, global(f, "vars.c", "g_float"));
        const QString fl = editor_text(f);
        cancel(f);
        const bool idle = quiesce(f);
        const uint64_t rev = f.backend->state_revision();
        std::string log;
        QTreeWidgetItem* gf = global(f, "vars.c", "g_float");
        log = log_of([&] { f.vp()->commit_edit(gf, fl); });   // the same text: nothing is written
        f.mgr->refresh_panels();
        const uint64_t redraws2 = f.mgr->take_panel_refresh_count();
        check("QVARW-02", "the editor holds the first word of the value: k (120 'x') gives 120, p gives its address `$XXXX` (no label, no type), g_float gives its number; "
                          "committing the unchanged text writes nothing: no MUTATE line, no revision bump, no redraw",
              at && idle && ok_k && k == "120" && ok_p && p.startsWith("$") && !p.contains(' ') && p.size() >= 5 && ok_f && fl == QString::fromStdString(k15) &&
                  f.backend->state_revision() == rev && log.find("MUTATE") == std::string::npos && redraws2 == 0 && val(gf) == fl,
              s(k) + " " + s(p) + " " + s(fl) + " rev=" + std::to_string(f.backend->state_revision() == rev) + " log=" + log + " val=" + s(val(gf)) + " redraws=" + std::to_string(redraws2));
    }
    {   // QVARW-03
        Fix f;
        f.load("master/sdcc-cvars", "w03a");
        const bool zat = f.run_to(0x8C97);
        QTreeWidgetItem* t = child(locals(f), "t");
        const QString before = val(t);
        const bool opened = dbl(f, t);
        cancel(f);
        const bool idle = quiesce(f);
        const uint64_t rev = f.backend->state_revision();
        f.vp()->commit_edit(t, "1");
        f.mgr->refresh_panels();
        const QString msg_t = message(f)->text();
        const bool shown_t = !message(f)->isHidden();
        const uint64_t redraws3 = f.mgr->take_panel_refresh_count();
        const bool same = val(child(locals(f), "t")) == before && f.backend->state_revision() == rev && redraws3 == 0;
        Fix g;
        g.load("master/sccz80-cvars", "w03b");
        const bool at = g.run_to(kS41);
        const bool op2 = dbl(g, global(g, "vars.c", "g_uint"));
        commit(g, "-1");
        const QString msg_u = message(g)->text();
        const bool cell = val(global(g, "vars.c", "g_uint")) == "40000";
        const bool op3 = dbl(g, global(g, "vars.c", "g_int"));
        commit(g, "7");
        check("QVARW-03", "a refused edit: zsdcc's t (a register local) shows `t: <why>` in the message label and the cell shows its value again, with no revision bump and no "
                          "redraw; sccz80's g_uint = -1 shows `vars.c::g_uint: ... out of range` (the facade path); the next successful edit (g_int = 7) clears the message",
              zat && idle && opened && shown_t && msg_t.startsWith("t: ") && msg_t.contains("register variable") && same && at && op2 && msg_u.startsWith("vars.c::g_uint: ") &&
                  msg_u.contains("out of range") && cell && op3 && message(g)->isHidden(),
              s(msg_t) + " | " + s(msg_u) + " same=" + std::to_string(same) + " redraws=" + std::to_string(redraws3) + " before=" + s(before) + " now=" + s(val(child(locals(f), "t"))));
    }
    {   // QVARW-04
        Fix z;
        z.load("master/sdcc-cvars", "w04a");
        const bool zat = z.run_to(0x8C97);
        QTreeWidgetItem* acc = child(locals(z), "acc");   // optimised out
        const bool acc_ok = acc && !editable(acc) && !dbl(z, acc);
        Fix f;
        f.load("master/sccz80-cvars", "w04b");
        f.run_to(kS41);
        f.vp()->select_frame(1);
        QTreeWidgetItem* lp = child(locals(f), "lp");
        QTreeWidgetItem* matrix = global(f, "vars.c", "g_matrix");
        const bool aggr = lp && matrix && !editable(lp) && !editable(matrix) && !dbl(f, lp) && !dbl(f, matrix);
        const bool groups = !editable(locals(f)) && !editable(globals(f)) && !editable(child(globals(f), "vars.c")) && !dbl(f, locals(f));
        // frame 1 at l_debug_push_frame's first instruction: sum's t is not allocated
        Fix n;
        n.load("master/sccz80-cvars", "w04c");
        n.run_to(0x8CB9);
        const bool nat = n.run_to(0x8B64);
        n.vp()->select_frame(1);
        QTreeWidgetItem* nt = child(locals(n), "t");
        const bool unalloc = nat && nt && val(nt) == "not allocated" && !editable(nt) && !dbl(n, nt);
        // a scalar and a pointer ARE editable
        const bool scalars = editable(child(locals(f), "lp")) == false && editable(global(f, "vars.c", "g_int")) && editable(global(f, "vars.c", "g_cptr"));
        check("QVARW-04", "not editable (no flag, a double-click opens no editor): aggregates (lp, g_matrix), a group (Locals, Globals, vars.c), a refusal (zsdcc's optimised-out "
                          "acc, frame 1's `not allocated` t at $8B64); a scalar (g_int) and a pointer (g_cptr) are",
              zat && acc_ok && aggr && groups && unalloc && scalars);
    }
    {   // QVARW-05
        Fix f;
        f.load("master/sccz80-cvars", "w05");
        const bool at = f.run_to(kS41);
        f.cs_table()->setCurrentCell(0, 0);   // frame 1 (main) through the Call Stack
        QApplication::processEvents();
        const uint16_t fp1 = f.backend->c_frames()[1].fp;
        const bool o1 = dbl(f, child(locals(f), "c"));
        commit(f, "100");
        f.mgr->refresh_panels();
        const bool c_ok = val(child(locals(f), "c")) == "100 'd'" && f.vp()->selected_frame() == 1 && f.frames()->currentIndex() == 1 && cpu_bytes(f, fp1 - 15u, 1) == Bytes{100};
        emit f.frames()->activated(0);
        QApplication::processEvents();
        QTreeWidgetItem* p = child(locals(f), "p");
        expand(p);
        QTreeWidgetItem* star = p && p->childCount() ? p->child(0) : nullptr;
        QTreeWidgetItem* px = star ? child(star, "x") : nullptr;
        QTreeWidgetItem* ptr_x = px ? px : (star ? star : nullptr);
        const std::string ptr_detail = detail_of(ptr_x);
        const bool o2 = dbl(f, ptr_x);
        commit(f, "10");
        f.mgr->refresh_panels();
        const bool kept = child(locals(f), "p") && child(locals(f), "p")->isExpanded();
        emit f.frames()->activated(1);
        QApplication::processEvents();
        QTreeWidgetItem* lp = child(locals(f), "lp");
        const bool lp_ok = val(lp).startsWith("{x = 10, y = 4");
        check("QVARW-05", "frame 1 and a child: with frame 1 chosen through the Call Stack, main's c = 100 shows 100 'd', lands at main's fp-15 and frame 1 stays chosen after the "
                          "redraw; back on frame 0, p's pointee member x = 10 keeps p open; main's lp then reads x = 10",
              at && o1 && c_ok && ptr_x && o2 && kept && lp_ok, s(val(lp)) + " " + ptr_detail);
    }
    {   // QVARW-06
        Fix f;
        f.load("master/sccz80-cvars", "w06a");
        const bool at = f.run_to(kS41);
        const bool o1 = dbl(f, global(f, "vars.c", "s_dup"));
        commit(f, "7");
        f.mgr->refresh_panels();
        const bool dup = val(global(f, "vars.c", "s_dup")).startsWith("7") && val(global(f, "other.c", "s_dup")).startsWith("4") && cpu_bytes(f, 0x909F, 1) == Bytes{7} &&
                         cpu_bytes(f, 0x9069, 1) == Bytes{4};
        Fix b;
        b.load("master/sccz80-cvars-banked", "w06b");
        const auto l7 = b.backend->source_map().resolve("bkmain.c:7");
        const bool bat = !l7.starts.empty() && b.run_to(l7.starts[0].address);
        const Bytes cpu0 = cpu_bytes(b, 0xC000, 4);
        QTreeWidgetItem* bt = global(b, "bk.c", "btable");
        expand(bt);
        QTreeWidgetItem* e0 = bt ? child(bt, "[0]") : nullptr;
        const bool o2 = dbl(b, e0);
        commit(b, "50");
        b.mgr->refresh_panels();
        QTreeWidgetItem* e0b = child(global(b, "bk.c", "btable"), "[0]");
        check("QVARW-06", "globals: vars.c's s_dup = 7 leaves other.c's s_dup at 4 (bytes $909F and $9069); banked, at bkmain.c:7: btable[0] = 50 shows 50 at `$C000 P14` "
                          "(Page 20, not mapped now) and the CPU's bytes at $C000 are unchanged",
              at && o1 && dup && bat && o2 && e0b && val(e0b).startsWith("50") && addr(e0b).contains("$C000") && addr(e0b).contains("P14") && cpu_bytes(b, 0xC000, 4) == cpu0,
              detail_of(e0b));
    }
    {   // QVARW-07
        Fix f;
        f.load("master/sccz80-cvars", "w07");
        const bool at = f.run_to(kS41);
        const bool o = dbl(f, child(locals(f), "t"));
        const std::string log = log_of([&] { commit(f, "5"); });
        // the MUTATE line names the window's client, never 0 (CLIENT_NONE) and not the test's own client
        const std::string tag = "MUTATE";
        const size_t at_line = log.find(tag);
        unsigned by = 0;
        if (at_line != std::string::npos) {
            const size_t b = log.find(" by ", at_line);
            if (b != std::string::npos) by = static_cast<unsigned>(std::atoi(log.c_str() + b + 4));
        }
        f.vp()->set_paused(false);   // while running: no editor
        const bool closed = !f.vp()->edit_item(child(locals(f), "t")) && !dbl(f, child(locals(f), "t"));
        f.vp()->set_paused(true);
        check("QVARW-07", "the client: the MUTATE line of a panel edit ends `by <the window's client>`, never 0 and not the test's client; while running a double-click opens no editor",
              at && o && count_of(log, "MUTATE") == 1 && by != 0 && by != f.me && closed, log + " by=" + std::to_string(by));
    }
    {   // QVARW-08
        Fix f;
        f.load("master/sccz80-cvars", "w08");
        const bool at = f.run_to(kS41);
        const bool o = dbl(f, child(locals(f), "t"));
        commit(f, "1000");
        f.mgr->refresh_panels();
        const bool at2 = f.run_to(kS41);   // sum's second call
        f.vp()->select_frame(1);
        QTreeWidgetItem* arr = child(locals(f), "arr");
        expand(arr);
        QTreeWidgetItem* a0 = arr ? child(arr, "[0]") : nullptr;
        check("QVARW-08", "the machine sees it: t = 1000 at the first vars.c:41 makes sum return 1345; at the second call, with frame 1 chosen and arr expanded, [0] is 1345",
              at && o && at2 && a0 && val(a0) == "1345", detail_of(a0));
    }
    {   // QVARW-09
        // review 1 F1: every keystroke is host input, so the manager's next paused tick redraws the window; that redraw must not put the old text
        // back over what is being typed. Real key events, a tick, then Enter.
        Fix f;
        f.load("master/sccz80-cvars", "w09");
        const bool at = f.run_to(kS41);
        const uint16_t fp = f.backend->c_frames()[0].fp;
        const bool idle = quiesce(f);
        const bool opened = dbl(f, child(locals(f), "t"));
        QLineEdit* e = editor(f);
        if (e) e->selectAll();
        if (e) QTest::keyClicks(e, "999");
        const QString typed = editor_text(f);
        f.mgr->refresh_panels();   // the keystrokes redraw the window
        const uint64_t redraws = f.mgr->take_panel_refresh_count();
        const QString after_tick = editor_text(f);
        const uint64_t rev = f.backend->state_revision();
        if (QLineEdit* e2 = editor(f)) QTest::keyClick(e2, Qt::Key_Return);
        settle_editor();
        check("QVARW-09", "typing with real key events survives a manager redraw: 999 typed into t's editor is still 999 after the tick the keystrokes cause (it was reset to 122), and "
                          "Enter writes it: the bytes at fp-2 are E7 03 and the revision moved by 1",
              at && idle && opened && typed == "999" && redraws >= 1 && after_tick == "999" && f.backend->state_revision() == rev + 1 && cpu_bytes(f, fp - 2u, 2) == Bytes{0xE7, 0x03},
              s(typed) + " after_tick=" + s(after_tick) + " redraws=" + std::to_string(redraws));
    }
    {   // QVARW-10
        // review 1 F2 (Q07): the message of a refused edit goes at a new stop
        Fix f;
        f.load("master/sdcc-cvars", "w10");
        const bool at = f.run_to(0x8C97);
        f.vp()->commit_edit(child(locals(f), "t"), "1");
        const bool shown = !message(f)->isHidden() && message(f)->text().startsWith("t: ");
        const bool at2 = f.run_to(0x8C97);   // sum's second call: a new stop
        check("QVARW-10", "the message of a refused edit is cleared at a new stop: shown after a refused t = 1, hidden after continuing to the next vars.c:41",
              at && shown && at2 && message(f)->isHidden());
    }
    {   // QVARW-11
        // review 1 F2 (Q09): an item reused in place that turns into a refusal is no longer editable
        Fix f;
        f.load("master/sccz80-cvars", "w11");
        const bool at = f.run_to(kS41);
        QTreeWidgetItem* t = child(locals(f), "t");
        const bool was = editable(t) && val(t) == "122";
        const bool at2 = f.run_to(0x8D53);   // the ret after l_debug_pop_frame: sum's locals are below the stack it owns
        QTreeWidgetItem* t2 = child(locals(f), "t");
        const bool same_item = t2 == t;
        const bool refused = t2 && val(t2) == "not allocated";
        check("QVARW-11", "an item reused in place that becomes a refusal loses its editable flag: t is editable at vars.c:41, and after the epilogue (the same tree item, now `not allocated`) "
                          "it is not and a double-click opens no editor",
              at && was && at2 && same_item && refused && !editable(t2) && !dbl(f, t2), detail_of(t2) + " same=" + std::to_string(same_item));
    }
    {   // QVARW-12
        // review 1 F2 (Q11): an editor still open when the machine resumes must not write
        Fix f;
        f.load("master/sccz80-cvars", "w12");
        const bool at = f.run_to(kS41);
        const bool opened = dbl(f, child(locals(f), "t"));
        f.vp()->set_paused(false);   // a remote client or a script resumed the machine
        const uint64_t rev = f.backend->state_revision();
        const std::string log = log_of([&] { commit(f, "7"); });
        const bool quiet = f.backend->state_revision() == rev && log.find("MUTATE") == std::string::npos;
        f.vp()->set_paused(true);
        check("QVARW-12", "committing an editor that was open when the machine resumed writes nothing: no MUTATE line and no revision bump",
              at && opened && quiet, log);
    }
    {   // QVARSH-01
        // GH #353 review F1: sccz80-shadow at `g += t` ($8C76): the outer t (3, marked `shadowed`) and the inner t (5). A row acts on the variable it shows: editing the shadowed row changes the outer, not the
        // inner; the two rows have two keys; a Watch is a name, so the shadowed row cannot be watched (and says why), while the inner row's Watch is `t`
        Fix f;
        f.load("master-fixed-sccz80/sccz80-shadow", "sh01", [](const fs::path& d) {
            fs::copy_file(kFixtures / "master-fixed-sccz80/sccz80-shadow" / "shadow.c.lis", d / "shadow.c.lis", fs::copy_options::overwrite_existing);
        });
        const bool at = f.run_to(0x8C76);
        const auto find = [&](bool shadowed) -> QTreeWidgetItem* {
            QTreeWidgetItem* L = locals(f);
            for (int i = 0; L && i < L->childCount(); ++i)
                if (L->child(i)->text(0) == "t" && L->child(i)->text(2).contains("shadowed") == shadowed) return L->child(i);
            return nullptr;
        };
        QTreeWidgetItem* outer = find(true);
        QTreeWidgetItem* inner = find(false);
        const bool two = outer && inner && outer != inner;
        const bool keys = two && outer->data(0, Qt::UserRole + 3).toString() != inner->data(0, Qt::UserRole + 3).toString();
        const bool before = two && outer->text(2).startsWith("3") && inner->text(2).startsWith("5");
        QSignalSpy spy(f.vp(), &VariablesPanel::watch_requested);
        const bool no_watch = two && !f.vp()->watch_item(outer) && spy.count() == 0 && !outer->data(0, Qt::UserRole + 9).toString().isEmpty();
        const bool watch_inner = two && f.vp()->watch_item(inner) && spy.count() == 1 && spy.at(0).at(0).value<CVarWatch>().path == "t";
        if (two) f.vp()->commit_edit(outer, "9");
        f.settle();
        outer = find(true);
        inner = find(false);
        check("QVARSH-01", "sccz80 shadow.c at `g += t`: the shadowed t row (3) and the inner row (5) have two keys; editing the shadowed row writes THAT variable (it reads 9, the inner still 5); Watch on the shadowed row is "
                           "refused with a reason, Watch on the inner row is the name t",
              at && two && keys && before && no_watch && watch_inner && outer && inner && outer->text(2).startsWith("9") && inner->text(2).startsWith("5"), detail_of(outer) + " | " + detail_of(inner));
    }
    {   // QVARSH-02
        // aggregates: zsdcc shadowagg.c at line 9 (inside the inner block, $8C8B): the outer `s` {x = 3, y = 1} (marked `shadowed`) and the inner `s` {x = 50, y = 60}. Expanding the shadowed row
        // lists ITS members, not the inner's
        Fix f;
        f.load("master-fixed-zsdcc/sdcc-shadowagg", "sh02");
        const bool at = f.run_to(0x8C8B);
        QTreeWidgetItem* outer = nullptr;
        QTreeWidgetItem* inner = nullptr;
        QTreeWidgetItem* L = locals(f);
        for (int i = 0; L && i < L->childCount(); ++i)
            if (L->child(i)->text(0) == "s") (L->child(i)->text(2).contains("shadowed") ? outer : inner) = L->child(i);
        expand(outer);
        expand(inner);
        const bool two = outer && inner;
        check("QVARSH-02", "zsdcc shadowagg.c: expanding the shadowed struct s lists its own members (x 3, y 1), the inner s lists x 50 and y 60; the two rows have two keys",
              at && two && val(child(outer, "x")).startsWith("3") && val(child(outer, "y")).startsWith("1") && val(child(inner, "x")).startsWith("50") && val(child(inner, "y")).startsWith("60") &&
                  outer->data(0, Qt::UserRole + 3).toString() != inner->data(0, Qt::UserRole + 3).toString(),
              detail_of(child(outer, "x")) + " | " + detail_of(child(inner, "x")));
    }
    {   // QVARSH-03
        // block statics (sccz80 statics.c, inside f's innermost block at `g = t + u`, $8C9F): the stack t is the shadowed row and the static (a data page) is the other. Editing the shadowed stack row writes
        // the stack variable; the static row keeps its value; Watch on the shadowed row is refused. The same on 80cc ($8C75), where the plain level-1 local is `may be shadowed`
        std::string seen;
        bool ok = true;
        struct Case { const char* fx; const char* tag; uint16_t pc; const char* mark; };
        for (const Case& c : {Case{"master-fixed-sccz80/sccz80-static", "sh03a", 0x8C9F, "shadowed"}, Case{"master-fixed-80cc/80cc-static", "sh03b", 0x8C75, "may be shadowed"}}) {
            Fix f;
            f.load(c.fx, c.tag, [&](const fs::path& d) {
                for (const auto& e : fs::directory_iterator(kFixtures / c.fx))
                    if (e.path().extension() == ".lis") fs::copy_file(e.path(), d / e.path().filename(), fs::copy_options::overwrite_existing);
            });
            const bool at = f.run_to(c.pc);
            QTreeWidgetItem* marked = nullptr;
            QTreeWidgetItem* other = nullptr;
            QTreeWidgetItem* L = locals(f);
            for (int i = 0; L && i < L->childCount(); ++i)
                if (L->child(i)->text(0) == "t") (L->child(i)->text(2).contains(c.mark) ? marked : other) = L->child(i);
            const bool two = marked && other;
            const QString static_before = two ? other->text(2) : QString();
            QSignalSpy spy(f.vp(), &VariablesPanel::watch_requested);
            const bool no_watch = two && !f.vp()->watch_item(marked) && spy.count() == 0;
            if (two) f.vp()->commit_edit(marked, "42");
            f.settle();
            marked = nullptr;
            other = nullptr;
            L = locals(f);
            for (int i = 0; L && i < L->childCount(); ++i)
                if (L->child(i)->text(0) == "t") (L->child(i)->text(2).contains(c.mark) ? marked : other) = L->child(i);
            const bool good = at && two && no_watch && marked && other && marked->text(2).startsWith("42") && other->text(2) == static_before;
            ok = ok && good;
            seen += std::string(c.tag) + "[" + detail_of(marked) + " | " + detail_of(other) + "] ";
        }
        check("QVARSH-03", "block statics shadowing a stack t, on sccz80 (`shadowed`) and 80cc (`may be shadowed`): editing the marked row writes that row's stack variable (42) and leaves the static row's text as it was; "
                           "Watch on the marked row is refused",
              ok, seen);
    }
    {   // QVARSH-04
        // two block statics named n in one function (zsdcc shadowagg.c): Globals has two `f::n` rows with two keys; each reads its own static (7 and 100) and neither can be watched by its (shared) path
        Fix f;
        f.load("master-fixed-zsdcc/sdcc-shadowagg", "sh04");
        const bool at = f.run_to(0x8C8B);
        QTreeWidgetItem* G = globals(f);
        std::vector<QTreeWidgetItem*> ns;
        for (int g = 0; G && g < G->childCount(); ++g)
            for (int i = 0; i < G->child(g)->childCount(); ++i)
                if (G->child(g)->child(i)->text(0) == "f::n") ns.push_back(G->child(g)->child(i));
        QSignalSpy spy(f.vp(), &VariablesPanel::watch_requested);
        const bool two = ns.size() == 2;
        const bool keys = two && ns[0]->data(0, Qt::UserRole + 3).toString() != ns[1]->data(0, Qt::UserRole + 3).toString();
        QStringList vals;
        for (auto* n : ns) vals << n->text(2).section(' ', 0, 0);
        vals.sort();
        const bool no_watch = two && !f.vp()->watch_item(ns[0]) && !f.vp()->watch_item(ns[1]) && spy.count() == 0;
        // a refused edit of either row names the static, not the internal handle
        QString msg;
        if (two) {
            f.vp()->commit_edit(ns[0], "99999");
            msg = message(f) ? message(f)->text() : QString("<none>");
        }
        check("QVARSH-04", "zsdcc shadowagg.c Globals: two rows named f::n with two keys, reading 100 and 7 (each its own static), a Watch on either is refused (the path names both), and a refused edit (99999) reads "
                           "`f::n (static): ...` with no `#`",
              at && two && keys && vals == QStringList({"100", "7"}) && no_watch && msg.startsWith("f::n (static): ") && !msg.contains('#'),
              (two ? detail_of(ns[0]) + " | " + detail_of(ns[1]) : std::string("rows=") + std::to_string(ns.size())) + " | " + s(msg));
    }
    {   // QVARSH-05
        // the edit message names the variable, never the internal handle: a refused edit of the shadowed t of sccz80 shadow.c reads `t (shadowed, in f): ...`
        Fix f;
        f.load("master-fixed-sccz80/sccz80-shadow", "sh05", [](const fs::path& d) {
            fs::copy_file(kFixtures / "master-fixed-sccz80/sccz80-shadow" / "shadow.c.lis", d / "shadow.c.lis", fs::copy_options::overwrite_existing);
        });
        const bool at = f.run_to(0x8C76);
        QTreeWidgetItem* outer = nullptr;
        QTreeWidgetItem* L = locals(f);
        for (int i = 0; L && i < L->childCount(); ++i)
            if (L->child(i)->text(0) == "t" && L->child(i)->text(2).contains("shadowed")) outer = L->child(i);
        if (outer) f.vp()->commit_edit(outer, "99999");
        const QString msg = message(f) ? message(f)->text() : QString("<none>");
        check("QVARSH-05", "a refused edit of the shadowed t (99999 is out of range) is reported as `t (shadowed, in f): ...`, with no `#` anywhere in the message",
              at && outer && msg.startsWith("t (shadowed, in f): ") && !msg.contains('#'), s(msg));
    }
    {   // QVARSH-06
        // a Locals row of a block static whose `function::name` is shared (zsdcc shadowagg.c:14, the second block of `static n`): its Watch is disabled like the Globals rows', with the same kind of tooltip
        Fix f;
        f.load("master-fixed-zsdcc/sdcc-shadowagg", "sh06");
        const bool at = f.run_to(0x8CA3);
        QTreeWidgetItem* n = child(locals(f), "n");
        QSignalSpy spy(f.vp(), &VariablesPanel::watch_requested);
        const bool refused = n && !f.vp()->watch_item(n) && spy.count() == 0 && !n->data(0, Qt::UserRole + 9).toString().isEmpty();
        check("QVARSH-06", "zsdcc shadowagg.c:14: the Locals row of the static n (its path f::n names two statics) cannot be watched and carries the tooltip",
              at && n && refused, detail_of(n));
    }
    {   // QVARSH-07
        // the flags of a handle row (synthetic: init has x at level 1 (-2), x at level 2 (-2, the same slot) and x at level 3 (-6), no block data, so the deep ones are `scope unknown`): the level-1 x is
        // shadowed and shares its slot with the middle x; editing it is refused `shares`; the middle x is shadowed and `scope unknown`: editing it is refused `scope unknown`; both messages name the variable
        Fix f;
        f.dir = fs::path(g_tmp->path().toStdString()) / "sh07";
        fs::create_directories(f.dir);
        const std::string map = (f.dir / "syn.map").string();
        std::ofstream(map) << "_init = $8000 ; addr, local, , a_c, code_compiler, a.c::init::0::0:1\n"
                              "__C_LINE_1_a_2ec = $8000 ; addr, local, , a_c, code_compiler, a.c::init::0::0:1\n"
                              "__code_compiler_tail = $8040 ; const, public, def, , ,\n"
                           << syn_rec("F:Fa.c$init$0_0$0({0}DF,SI:S),C,0,0,0,0,0", "a_c") << syn_rec("S:La.c.init$x$1_0$1({2}SI:S),B,1,-2", "a_c")
                           << syn_rec("S:La.c.init$x$2_0$2({2}SI:S),B,1,-2", "a_c") << syn_rec("S:La.c.init$x$3_0$3({2}SI:S),B,1,-6", "a_c");
        const uint8_t pro[] = {0xDD, 0xE5, 0xDD, 0x21, 0x00, 0x00, 0xDD, 0x39, 0xE5, 0xE5};
        for (size_t i = 0; i < sizeof pro; ++i) f.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), pro[i]);
        const bool loaded = f.backend->load_source_map(map, true).count > 0;
        syn_enter(f, 0x8000, 0xFE00);
        QTreeWidgetItem* L = locals(f);
        std::vector<QTreeWidgetItem*> xs;
        for (int i = 0; L && i < L->childCount(); ++i)
            if (L->child(i)->text(0) == "x") xs.push_back(L->child(i));
        QTreeWidgetItem* level1 = nullptr;
        QTreeWidgetItem* middle = nullptr;
        for (auto* x : xs) {
            if (x->text(2).contains("shares") && !x->text(2).contains("scope unknown")) level1 = x;
            else if (x->text(2).contains("shares") && x->text(2).contains("scope unknown") && x->text(2).contains("may be shadowed")) middle = x;
        }
        QString m1, m2;
        if (level1) { f.vp()->commit_edit(level1, "5"); m1 = message(f)->text(); }
        if (middle) { f.vp()->commit_edit(middle, "5"); m2 = message(f)->text(); }
        check("QVARSH-07", "three x in init: the level-1 x (shadowed, shares its slot) refuses an edit with `x (may be shadowed, in init): shares its stack slot ...` (the innermost x is `scope unknown`, so the choice is not certain), the middle x (may be shadowed, scope unknown) with `scope unknown`; no `#` in either",
              loaded && xs.size() == 3 && level1 && middle && m1.startsWith("x (may be shadowed, in init): ") && m1.contains("shares its stack slot") && m2.startsWith("x (may be shadowed, in init): ") && m2.contains("scope unknown") &&
                  !m1.contains('#') && !m2.contains('#'), s(m1) + " | " + s(m2) + " | " + std::to_string(xs.size()));
    }
    {   // QVARSH-08
        // the success-with-a-note branch of the edit message (80cc shadow.c at `g += t`, $8C64): the level-1 t is `may be shadowed`, a write to it succeeds and carries the 80cc note; the message names the variable
        Fix f;
        f.load("master-fixed-80cc/80cc-shadow", "sh08", [](const fs::path& d) {
            fs::copy_file(kFixtures / "master-fixed-80cc/80cc-shadow" / "shadow.c.lis", d / "shadow.c.lis", fs::copy_options::overwrite_existing);
        });
        const bool at = f.run_to(0x8C64);
        QTreeWidgetItem* marked = nullptr;
        QTreeWidgetItem* L = locals(f);
        for (int i = 0; L && i < L->childCount(); ++i)
            if (L->child(i)->text(0) == "t" && L->child(i)->text(2).contains("may be shadowed")) marked = L->child(i);
        if (marked) f.vp()->commit_edit(marked, "9");
        const QString msg = message(f) ? message(f)->text() : QString("<none>");
        check("QVARSH-08", "80cc shadow.c: editing the `may be shadowed` t to 9 succeeds with the 80cc note, and the message reads `t (may be shadowed, in f): 80cc may not keep it ...` with no `#`",
              at && marked && msg.startsWith("t (may be shadowed, in f): 80cc may not keep it") && !msg.contains('#'), s(msg));
    }
    {   // QVARSH-09
        // range items under a handle row (synthetic: init has x[150] of int at level 1, shadowed by a scalar x of level 2): expanding the shadowed array lists the ranges [0..99] and [100..149], and a refused edit
        // of element 100 names the variable (`x (shadowed, in init)[100]: ...`), not `[100] (shadowed, in init)[100]`
        Fix f;
        f.dir = fs::path(g_tmp->path().toStdString()) / "sh09";
        fs::create_directories(f.dir);
        const std::string map = (f.dir / "syn.map").string();
        std::ofstream(map) << "_init = $8000 ; addr, local, , a_c, code_compiler, a.c::init::0::0:1\n"
                              "__C_LINE_1_a_2ec = $8000 ; addr, local, , a_c, code_compiler, a.c::init::0::0:1\n"
                              "__code_compiler_tail = $8040 ; const, public, def, , ,\n"
                           << syn_rec("F:Fa.c$init$0_0$0({0}DF,SI:S),C,0,0,0,0,0", "a_c") << syn_rec("S:La.c.init$x$1_0$1({300}DA150x,SI:S),B,1,-302", "a_c")
                           << syn_rec("S:La.c.init$x$2_0$2({2}SI:S),B,1,-2", "a_c");
        const uint8_t pro[] = {0xDD, 0xE5, 0xDD, 0x21, 0x00, 0x00, 0xDD, 0x39, 0x21, 0xD0, 0xFE, 0x39, 0xF9};   // push ix; ld ix,0; add ix,sp; ld hl,-304; add hl,sp; ld sp,hl
        for (size_t i = 0; i < sizeof pro; ++i) f.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), pro[i]);
        const bool loaded = f.backend->load_source_map(map, true).count > 0;
        syn_enter(f, 0x8000, 0xFE00, 6);
        QTreeWidgetItem* L = locals(f);
        QTreeWidgetItem* arr = nullptr;
        for (int i = 0; L && i < L->childCount(); ++i)
            if (L->child(i)->text(0) == "x" && L->child(i)->text(2).contains("shadowed")) arr = L->child(i);
        expand(arr);
        QTreeWidgetItem* second = arr && arr->childCount() == 2 ? arr->child(1) : nullptr;
        expand(second);
        QTreeWidgetItem* el = second ? child(second, "[100]") : nullptr;
        if (el) f.vp()->commit_edit(el, "99999");
        const QString msg = message(f) ? message(f)->text() : QString("<none>");
        check("QVARSH-09", "a refused edit of element 100 of the shadowed array x (two range items) reads `x (may be shadowed, in init)[100]: ...`",
              loaded && arr && second && el && msg.startsWith("x (may be shadowed, in init)[100]: ") && !msg.contains('#'), s(msg));
    }
    {   // QVARZB-02
        // GH #438 review note: a C watch shows the facade's notes, the 80cc frame-slot one included
        Fix f;
        f.load("master/80cc-cvars", "zb02");
        const bool at = f.run_to(0x8CCA);
        const QString note = "80cc may not keep it in the frame slot its record names: the value shown can be stale or another variable's";
        const bool pinned = f.vp()->watch_item(child(locals(f), "t"));
        const QString cell = wcell(f, 0, 3);
        Fix g;
        g.load("master/sccz80-cvars", "zb02b");
        const bool gat = g.run_to(kS41);
        g.vp()->watch_item(child(locals(g), "t"));
        check("QVARZB-02", "master 80cc-cvars at vars.c:41: a C watch of t reads `122  (<the 80cc note>)`; the same watch on sccz80 reads plain 122",
              at && pinned && cell == "122  (" + note + ")" && gat && wcell(g, 0, 3) == "122", s(cell) + " | " + s(wcell(g, 0, 3)));
    }
    {   // QVARZB-01
        // GH #438: master 80cc-cvars at vars.c:41: t is an 80cc frame-slot variable (z88dk#3233); the note is in the Value text, the tooltip and, after an edit, the message
        Fix f;
        f.load("master/80cc-cvars", "zb01");
        const bool at = f.run_to(0x8CCA);
        const QString note = "80cc may not keep it in the frame slot its record names: the value shown can be stale or another variable's";
        QTreeWidgetItem* t = child(locals(f), "t");
        QTreeWidgetItem* calls = child(locals(f), "calls");
        const bool row = t && val(t).endsWith("  (" + note + ")") && t->toolTip(2).contains(note);
        const bool plain = calls && val(calls) == "1";
        f.vp()->commit_edit(t, "1000");
        const bool fb = !message(f)->isHidden() && message(f)->text() == "t: " + note;
        Fix g;
        g.load("master/sccz80-cvars", "zb01b");
        const bool gat = g.run_to(kS41);
        g.vp()->commit_edit(child(locals(g), "t"), "1000");
        check("QVARZB-01", "master 80cc-cvars at vars.c:41: the row t ends `  (80cc may not keep it ...)`, its tooltip has the note and the static local calls has none; an "
                           "edit of t succeeds and the message says `t: <the note>`; the same edit on sccz80 leaves the message hidden",
              at && row && plain && fb && gat && message(g)->isHidden(), detail_of(t) + " msg=" + s(message(f)->text()));
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
    test_rows();
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
