// ===========================================================================
// The Qt Variables tab (GH #435, E353-05 of epic #353):
//
//   QVAR-*   the Variables tab: C locals per call frame, globals and statics, lazy expansion,
//            the Call Stack selection kept in step both ways, addresses, refresh and idle
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
#include "platform/emulator_boot.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>

#include <cstdint>
#include <cstdio>
#include <filesystem>
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
    bool load(const char* fixture, const char* name) {
        dir = fs::path(g_tmp->path().toStdString()) / name;
        fs::create_directories(dir);
        for (const auto& e : fs::directory_iterator(kFixtures / fixture)) {
            const std::string n = e.path().filename().string();
            if (n == "prog.nex" || n == "prog.map" || (n.size() > 4 && n.compare(n.size() - 4, 4, ".bin") == 0))
                fs::copy_file(e.path(), dir / n, fs::copy_options::overwrite_existing);
        }
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

// vars.c:41, sum's first call (sccz80, master): the stop every row starts from.
constexpr uint16_t kS41 = 0x8D3E;

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
                  !child(locals(f), "inner") && ty(a) == "int" && ty(k) == "unsigned char" && ty(p) == "struct point *" && ty(acc) == "long",
              s(f.frames()->currentText()) + " | " + s(names(locals(f))));
    }

    {   // QVAR-04
        Fix f;
        f.load("master/sccz80-cvars", "q04");
        const bool at = f.run_to(kS41);
        check("QVAR-04", "at vars.c:41 Globals has the group vars.c with g_int -5, g_uint 40000, g_char 65 'A', g_float 1.5, g_str \"hi\", g_matrix {{1, 2, 3}, {4, 5, 6}}, "
                         "s_file 9, s_dup 0, and the group other.c with s_dup 4",
              at && val(global(f, "vars.c", "g_int")) == "-5" && val(global(f, "vars.c", "g_uint")) == "40000" && val(global(f, "vars.c", "g_char")) == "65 'A'" &&
                  val(global(f, "vars.c", "g_float")) == QString::fromStdString(k15) && val(global(f, "vars.c", "g_str")) == "\"hi\"" &&
                  val(global(f, "vars.c", "g_matrix")) == "{{1, 2, 3}, {4, 5, 6}}" && val(global(f, "vars.c", "s_file")) == "9" &&
                  val(global(f, "vars.c", "s_dup")) == "0" && val(global(f, "other.c", "s_dup")) == "4",
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
        check("QVAR-06", "choosing frame 1 in the selector selects Call Stack row 0 and choosing frame 0 clears the Call Stack selection; frame_changed fires once per choice "
                         "(no loop between the two) and a refresh keeps the row in step",
              at && row0 && n1 == 1 && still && cleared && spy.count() == 2,
              "n1=" + std::to_string(n1) + " total=" + std::to_string(spy.count()));
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
              names(m) + " | " + names(str));
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
            const bool at = f.run_to(0x8B64);
            const std::string why0 = f.backend->c_locals(0).why;
            QTreeWidgetItem* msg = locals(f)->childCount() == 1 ? locals(f)->child(0) : nullptr;
            const bool frame0 = at && !why0.empty() && f.frames()->currentText().startsWith("<not C code>") && msg && val(msg) == QString::fromStdString(why0);
            f.cs_table()->setCurrentCell(0, 0);   // sum's CALL of the helper: frame 1 is sum
            QApplication::processEvents();
            const bool frame1 = f.vp()->selected_frame() == 1 && val(child(locals(f), "t")) == "not allocated" && val(child(locals(f), "acc")) == "not allocated" &&
                                val(child(locals(f), "t")) != "0";
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
        const auto slots = f.backend->mmu_slots();
        const auto spell = [&](jnext::dbg::AddressStyle st) {
            return QString::fromStdString(jnext::dbg::long_address(xxxx, slots[xxxx >> 13].now, st, next));
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
