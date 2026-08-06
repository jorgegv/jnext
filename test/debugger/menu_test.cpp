// ===========================================================================
// GH #215 — the debugger's two menu dead ends.
//
// No VHDL oracle: this is host menu wiring, not emulated hardware. The oracle
// is what the running product offers a user who goes looking through the
// menus, which is how both defects were reported (#207's comments).
//
// THE TWO DEFECTS
//
//   1. The debugger's Breakpoints menu offered Add Read, Add Write, Add
//      Read/Write and Clear All — and no Add EXECUTE. Execute is the ordinary
//      PC breakpoint, the one a user reaches for first, and it existed only
//      inside the Breakpoints PANEL's Add dialog (a combo box entry). It is
//      also not a fourth WatchType: it is BreakpointSet::add_pc(), a different
//      container from the watchpoint list, which is why the menu could not
//      simply pass another enum to the existing handler.
//
//   2. The main window has a top-level "De&bug" menu holding Magic Breakpoint
//      and nothing else, while the Debugger toggle sits under View. A user
//      looking for the debugger opens the menu named Debug, finds one
//      unrelated toggle, and concludes it is not in the menus at all. View
//      placement is what the design plan's Phase 7.5 asked for and does not
//      move; the Debug menu gains the SAME QAction, so the two routes cannot
//      disagree about the checked state.
//
// WHAT THESE ROWS ASSERT — the observable effect, never "an entry exists" on
// its own. GH215-02 types an address into the real modal dialog and reads the
// breakpoint back out of the real BreakpointSet, and it checks the address
// landed in pc_breakpoints() and NOT in watchpoints(): an implementation that
// wired Execute to add_watchpoint() would satisfy "a breakpoint appeared".
//
// DRIVING THE MODAL. show_add_exec_bp_dialog() blocks in QDialog::exec()'s
// nested event loop, so the answer has to be armed BEFORE the trigger. The
// watcher below is the quit_gate_test ModalWatcher idiom, widened from
// QMessageBox to any QDialog and taught to type into the address field.
// It records the dialog before answering it, so a row expecting no dialog
// still fails cleanly instead of hanging the suite.
//
// Discriminative — each row was mutation-tested against the product, one
// mutation at a time (the product reverted from a `cp` backup each time):
//   GH215-01/02/03  the Add Execute menu entry removed  -> all three fail
//                   ("Add Execute Breakpoint... not in the Breakpoints menu")
//   GH215-02        alone: the handler switched to add_watchpoint(READ)
//                   -> fails on the pc_breakpoints() half while the
//                   "something was added" half still passes
//   GH215-04/05/06  the debug_menu->addAction(debugger_action_) line removed
//                   -> all three fail
//   GH215-05        a SEPARATE QAction added to the Debug menu instead of the
//                   shared one -> GH215-04 still passes, GH215-05 fails, and
//                   GH215-06 fails on the View entry's checkmark. That is the
//                   pair's reason for existing: "an entry is there" is not
//                   "one shared checked state".
//   GH215-07        is NOT a regression row for this issue and is labelled as
//                   one rather than quietly counted: the toolbar button worked
//                   before the change. It pins the constraint the change had to
//                   preserve — that adding a second menu route did not disturb
//                   the toolbar's own sync path (DebuggerManager::
//                   enabled_changed -> QAction::setChecked).
//
// MainWindow and DebuggerWindow own QWidgets, so a QApplication is required —
// but not a display: main() forces the offscreen QPA platform, the same idiom
// as the other debugger and gui suites.
//
// ---------------------------------------------------------------------------
// GH #218 (groups BPR and BPC) — the same menu, the panel behind it.
//
// #215's Add Execute route refreshed the Breakpoints panel; the three data
// routes next to it in the SAME menu did not, so a Read/Write breakpoint added
// from the menu was invisible until the next pause or step. An audit of every
// site that mutates the BreakpointSet found two more of the same class (Clear
// All, and the disassembly's Break-on-Read/Write context items) and two that
// were already right (the panel's own Add/Edit/Remove, and the gutter's PC
// toggles, which ride DisasmPanel::breakpoint_toggled).
//
// The disassembly gutter is deliberately NOT refreshed on a watchpoint change
// anywhere: it paints bps.has_pc() only, so a watchpoint changes nothing in
// it. That asymmetry with the Execute route is intended, not an omission.
//
// See the group banners below for what each row pins and why it is not
// allowed to touch pause/step/refresh_panels.
// ---------------------------------------------------------------------------
//
// Run: ./build/test/debugger_menu_test
// ===========================================================================

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/breakpoints.h"
#include "debug/debug_state.h"
#include "debugger/breakpoint_panel.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/disasm_panel.h"
#include "gui/main_window.h"
#include "memory/mmu.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

// ── Test infrastructure (mirrors the other debugger suites) ───────────

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string group;
    std::string id;
    bool        passed;
};

std::vector<Result> g_results;
std::string         g_group;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    ++g_total;
    g_results.push_back(Result{g_group, id, cond});
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

std::string fmt(const char* fmt_str, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

/// The visible text: Qt eats the mnemonic '&' and renders "&&" as one '&'.
QString visible(const QString& text) {
    QString out;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == u'&') {
            if (i + 1 < text.size() && text[i + 1] == u'&') { out += u'&'; ++i; }
            continue;
        }
        out += text[i];
    }
    return out;
}

/// The menu-bar entry whose VISIBLE title is `title`, or nullptr.
QMenu* menu_named(QMenuBar* bar, const QString& title) {
    for (QAction* a : bar->actions())
        if (a->menu() && visible(a->text()) == title) return a->menu();
    return nullptr;
}

/// The item of `menu` whose VISIBLE text is `text`, or nullptr. Visible text,
/// not raw: which letter carries the mnemonic is accel_test's subject, not
/// this suite's, and a re-lettering there must not break these rows.
QAction* item_named(QMenu* menu, const QString& text) {
    if (!menu) return nullptr;
    for (QAction* a : menu->actions())
        if (visible(a->text()) == text) return a;
    return nullptr;
}

/// Answers the modal dialog a trigger opens: records it, types `typed` into
/// its first QLineEdit, then accepts or rejects. Armed before the trigger,
/// because QDialog::exec() does not return until it is answered.
struct ModalAnswer {
    QString typed;
    bool    accept   = true;
    bool    seen     = false;
    QString title;
};

/// Trigger `action` and answer whatever modal it opens. Both objects live on
/// this frame, which outlives the nested event loop the trigger enters.
void trigger_and_answer(QAction* action, ModalAnswer& ans) {
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&ans]() {
        QWidget* w = QApplication::activeModalWidget();
        auto* dlg = qobject_cast<QDialog*>(w);
        if (!dlg) return;
        ans.seen  = true;
        ans.title = dlg->windowTitle();
        if (QLineEdit* edit = dlg->findChild<QLineEdit*>())
            edit->setText(ans.typed);
        if (ans.accept) dlg->accept(); else dlg->reject();
    });
    timer.start(1);
    action->trigger();
    timer.stop();
    QApplication::processEvents();
}

// ── GH #218 helpers: reading the Breakpoints PANEL, driving a popup ───

/// The Breakpoints panel exactly as a user reads it: one "Type $ADDR" string
/// per visible table row, in table order. This — not the BreakpointSet — is
/// what the #218 rows assert against, because the whole defect is that the
/// two disagreed until the next pause or step.
QStringList panel_rows(DebuggerWindow* dbg) {
    QStringList out;
    BreakpointPanel* panel = dbg ? dbg->breakpoint_panel() : nullptr;
    if (!panel) return out;
    auto* table = panel->findChild<QTableWidget*>();
    if (!table) return out;
    for (int r = 0; r < table->rowCount(); ++r) {
        QTableWidgetItem* type = table->item(r, 0);
        QTableWidgetItem* addr = table->item(r, 1);
        out << QStringLiteral("%1 %2")
                   .arg(type ? type->text() : QStringLiteral("?"),
                        addr ? addr->text() : QStringLiteral("?"));
    }
    return out;
}

/// Answers the POPUP a context-menu event opens. A QMenu is a popup widget,
/// not a modal one, so this is activePopupWidget() where ModalAnswer above is
/// activeModalWidget() — otherwise the same armed-beforehand idiom, because
/// QMenu::exec() likewise blocks in a nested event loop.
struct PopupAnswer {
    QString     wanted;          // visible text of the item to trigger
    bool        seen  = false;   // a popup appeared at all
    bool        found = false;   // ... and it held `wanted`
    QStringList items;           // what it held, for the failure detail
};

/// Send `w` a context-menu event at `pos` and trigger `ans.wanted` in whatever
/// popup it opens. Returns with the popup closed either way.
void context_menu_and_pick(QWidget* w, const QPoint& pos, PopupAnswer& ans) {
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&ans]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || ans.seen) return;
        ans.seen = true;
        for (QAction* a : menu->actions()) {
            if (a->isSeparator()) continue;
            const QString text = visible(a->text());
            ans.items << text;
            if (text == ans.wanted) {
                ans.found = true;
                a->trigger();
            }
        }
        menu->close();
    });
    timer.start(1);

    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));
    QApplication::sendEvent(w, &ev);

    timer.stop();
    QApplication::processEvents();
}

/// Right-click the disassembly and pick `wanted`. The y of a disassembly line
/// is a private layout constant, so walk the plausible ones and stop at the
/// first that produces a popup holding the item — every line carries the same
/// item here (see fill_disasm_window), so which one is hit does not matter.
PopupAnswer pick_from_disasm(DisasmPanel* panel, const QString& wanted) {
    PopupAnswer last;
    if (!panel) return last;
    const int max_y = std::min(panel->height(), 400);
    for (int y = 55; y < max_y; y += 18) {
        PopupAnswer ans;
        ans.wanted = wanted;
        context_menu_and_pick(panel, QPoint(120, y), ans);
        if (ans.found) return ans;
        if (ans.seen) last = ans;   // keep the most informative failure
    }
    return last;
}

/// Fill `base`.. with `LD HL,$4000` (21 00 40) and point the disassembly at
/// it, so every visible line offers "Break on Read/Write $4000" — the context
/// menu's immediate-operand route, which is what GH218-05/06 drive. Returns
/// false if the writes did not stick (`base` must land in RAM, not ROM).
bool fill_disasm_window(Emulator& emu, DisasmPanel* panel, uint16_t base) {
    for (int i = 0; i < 256; i += 3) {
        emu.mmu().write(static_cast<uint16_t>(base + i + 0), 0x21);
        emu.mmu().write(static_cast<uint16_t>(base + i + 1), 0x00);
        emu.mmu().write(static_cast<uint16_t>(base + i + 2), 0x40);
    }
    if (emu.mmu().read(base) != 0x21 || emu.mmu().read(base + 2) != 0x40)
        return false;

    if (!panel) return false;
    if (auto* sb = panel->findChild<QScrollBar*>())
        sb->setValue(base);
    QApplication::processEvents();
    return true;
}

bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

/// A headless Next emulator plus the real DebuggerWindow, brought up through
/// DebuggerManager exactly as the product does.
struct DebuggerFixture {
    Emulator         emu;
    QMainWindow      win;
    DebuggerManager* mgr = nullptr;
    DebuggerWindow*  dbg = nullptr;
    bool             ok  = false;

    DebuggerFixture() {
        if (!build_next_emulator(emu)) return;
        mgr = new DebuggerManager(&win, &emu, &win);   // parented → auto-freed
        mgr->set_enabled(true);                        // creates + shows the window
        dbg = mgr->debugger_window_ptr();
        ok  = (dbg != nullptr);
        QApplication::processEvents();
    }
    ~DebuggerFixture() {
        if (mgr) mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
    }
};

/// A real MainWindow bound to a real emulator, which is what builds the
/// DebuggerManager and therefore the Debug/View menu wiring under test.
struct MainWindowFixture {
    Emulator   emu;
    MainWindow win;
    bool       ok = false;

    MainWindowFixture() {
        if (!build_next_emulator(emu)) return;
        win.set_emulator(&emu);
        QApplication::processEvents();
        ok = win.debugger_manager() != nullptr;
    }
    ~MainWindowFixture() {
        if (win.debugger_manager())
            win.debugger_manager()->set_enabled(false, /*prompt_on_corrupt=*/false);
    }
};

} // namespace

// ── BPM: the Breakpoints menu can add an Execute breakpoint ───────────

static void test_breakpoints_menu()
{
    set_group("BPM");

    DebuggerFixture fx;
    if (!fx.ok) {
        check("GH215-01", "the Breakpoints menu offers an Add Execute entry", false, "fixture failed");
        check("GH215-02", "it adds a PC breakpoint at the typed address, not a watchpoint", false, "fixture failed");
        check("GH215-03", "cancelling the dialog adds no breakpoint at all", false, "fixture failed");
        return;
    }

    QMenu* bp_menu = menu_named(fx.dbg->menuBar(), QStringLiteral("Breakpoints"));
    QAction* exec_item = item_named(bp_menu, QStringLiteral("Add Execute Breakpoint..."));

    // GH215-01 — THE issue's first half. The menu listed every breakpoint type
    // except the ordinary one.
    {
        QStringList items;
        if (bp_menu)
            for (QAction* a : bp_menu->actions())
                if (!a->isSeparator()) items << visible(a->text());
        check("GH215-01", "the Breakpoints menu offers an Add Execute entry",
              exec_item != nullptr,
              bp_menu ? fmt("Breakpoints menu holds: %s",
                            items.join(QStringLiteral(" | ")).toUtf8().constData())
                      : "no menu titled \"Breakpoints\" on the debugger menu bar");
    }

    BreakpointSet& bps = fx.emu.debug_state().breakpoints();
    bps.clear_all_pc();
    bps.clear_all_watchpoints();

    // GH215-02 — the effect, end to end: real menu action, real modal, real
    // BreakpointSet. Both halves matter — "in pc_breakpoints()" AND "not in
    // watchpoints()" — because add_watchpoint() would also make the set
    // non-empty while breaking on the wrong event.
    {
        ModalAnswer ans;
        ans.typed  = QStringLiteral("8000");
        ans.accept = true;
        if (exec_item) trigger_and_answer(exec_item, ans);

        const bool is_pc    = bps.has_pc(0x8000);
        const bool no_watch = bps.watchpoints().empty();
        check("GH215-02", "it adds a PC breakpoint at the typed address, not a watchpoint",
              exec_item && ans.seen && is_pc && no_watch,
              fmt("dialog seen=%d title=\"%s\" pc_bps=%zu has_pc($8000)=%d watchpoints=%zu",
                  ans.seen ? 1 : 0, ans.title.toUtf8().constData(),
                  bps.pc_breakpoints().size(), is_pc ? 1 : 0,
                  bps.watchpoints().size()));
    }

    // GH215-03 — Cancel means cancel. Without it, GH215-02 would still pass
    // against a handler that added the breakpoint before showing the dialog.
    {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();

        ModalAnswer ans;
        ans.typed  = QStringLiteral("9000");
        ans.accept = false;
        if (exec_item) trigger_and_answer(exec_item, ans);

        check("GH215-03", "cancelling the dialog adds no breakpoint at all",
              exec_item && ans.seen && bps.empty(),
              fmt("dialog seen=%d pc_bps=%zu watchpoints=%zu",
                  ans.seen ? 1 : 0, bps.pc_breakpoints().size(),
                  bps.watchpoints().size()));
    }
}

// ── BPR: every breakpoint route repaints the Breakpoints panel ────────
//
// GH #218. The panel lists Execute AND data breakpoints, and NOTHING else
// repaints it while the emulator runs — DebuggerManager::refresh_panels() is
// driven by the host frame loop and only reaches the panel on a pause/step.
// So a route that mutated the set without calling breakpoint_panel_->refresh()
// left the list lying for as long as the user kept running. Three routes did:
//
//   show_add_data_bp_dialog()        Breakpoints > Add Read/Write/Read-Write
//   the Clear All Breakpoints lambda (refreshed the disassembly, not the list)
//   DisasmPanel's Break-on-Read/Write context items (refreshed nothing)
//
// The rows below therefore assert the PANEL's contents, never the
// BreakpointSet's — the set was always correct, which is exactly why the bug
// survived. The set is still read, but only to make a failure say WHICH half
// broke: "the breakpoint was never added" reads differently from "it was
// added and the panel did not notice".
//
// No pause(), step() or refresh_panels() call may appear in these rows. Each
// one would repaint the panel by itself and make the row pass against the
// unfixed product — the defect is precisely that the user had to do one of
// those things.

static void test_breakpoint_panel_refresh()
{
    set_group("BPR");

    DebuggerFixture fx;
    struct Case {
        const char* id;
        const char* item;     // visible menu text
        const char* typed;    // address typed into the modal
        const char* row;      // the panel row it must produce
    };
    static const Case cases[] = {
        {"GH218-01", "Add Read Breakpoint...",       "4000", "Read $4000"},
        {"GH218-02", "Add Write Breakpoint...",      "5000", "Write $5000"},
        {"GH218-03", "Add Read/Write Breakpoint...", "6000", "Read/Write $6000"},
    };

    if (!fx.ok) {
        check("GH218-01", "Add Read Breakpoint shows up in the Breakpoints panel at once", false, "fixture failed");
        check("GH218-02", "Add Write Breakpoint shows up in the Breakpoints panel at once", false, "fixture failed");
        check("GH218-03", "Add Read/Write Breakpoint shows up in the Breakpoints panel at once", false, "fixture failed");
        check("GH218-04", "Clear All Breakpoints empties the Breakpoints panel at once", false, "fixture failed");
        return;
    }

    QMenu* bp_menu = menu_named(fx.dbg->menuBar(), QStringLiteral("Breakpoints"));
    BreakpointSet& bps = fx.emu.debug_state().breakpoints();

    // GH218-01/02/03 — THE reported defect, once per data type. Each type is
    // its own row because each is its own menu entry passing its own WatchType,
    // and a fix that reached only one of them would still leave two lying.
    for (const Case& c : cases) {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        fx.dbg->breakpoint_panel()->refresh();   // setup, not the assertion:
                                                 // start from a truthful panel

        QAction* item = item_named(bp_menu, QString::fromLatin1(c.item));
        ModalAnswer ans;
        ans.typed  = QString::fromLatin1(c.typed);
        ans.accept = true;
        if (item) trigger_and_answer(item, ans);

        const QStringList rows  = panel_rows(fx.dbg);
        const bool in_panel = rows.contains(QString::fromLatin1(c.row));
        const bool in_set   = bps.watchpoints().size() == 1;

        check(c.id,
              fmt("%s shows up in the Breakpoints panel at once", c.item).c_str(),
              item && ans.seen && in_set && in_panel,
              fmt("menu item=%d dialog seen=%d watchpoints=%zu panel holds: [%s] (want \"%s\")",
                  item ? 1 : 0, ans.seen ? 1 : 0, bps.watchpoints().size(),
                  rows.join(QStringLiteral(", ")).toUtf8().constData(), c.row));
    }

    // GH218-04 — the same gap the other way round. Seeded through the
    // BreakpointSet directly (not through the menu) so this row keeps failing
    // for its OWN reason if the Add fix above is ever reverted: what it pins
    // is that Clear All repaints the list, and its precondition is a panel
    // that really was showing those four.
    {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        bps.add_pc(0x1234);
        bps.add_watchpoint(0x4000, WatchType::READ);
        bps.add_watchpoint(0x5000, WatchType::WRITE);
        bps.add_watchpoint(0x6000, WatchType::READ_WRITE);
        fx.dbg->breakpoint_panel()->refresh();

        const int before = panel_rows(fx.dbg).size();

        QAction* clear_item = item_named(bp_menu, QStringLiteral("Clear All Breakpoints"));
        if (clear_item) {
            clear_item->trigger();
            QApplication::processEvents();
        }

        const QStringList after = panel_rows(fx.dbg);
        check("GH218-04", "Clear All Breakpoints empties the Breakpoints panel at once",
              clear_item && before == 4 && bps.empty() && after.isEmpty(),
              fmt("menu item=%d rows before=%d set empty after=%d panel after: [%s]",
                  clear_item ? 1 : 0, before, bps.empty() ? 1 : 0,
                  after.join(QStringLiteral(", ")).toUtf8().constData()));
    }
}

// ── BPC: the disassembly's own Break-on-Read/Write items ──────────────
//
// The third #218 route, and the one furthest from the panel: DisasmPanel has
// no pointer to BreakpointPanel and must not grow one. It already emits
// breakpoint_toggled for its gutter's PC toggles, and DebuggerWindow already
// answers that by refreshing the list — these two rows pin that the data
// items now ride the same wire.

static void test_disasm_data_breakpoints()
{
    set_group("BPC");

    DebuggerFixture fx;
    struct Case { const char* id; const char* item; const char* row; WatchType type; };
    static const Case cases[] = {
        {"GH218-05", "Break on Read $4000",  "Read $4000",  WatchType::READ},
        {"GH218-06", "Break on Write $4000", "Write $4000", WatchType::WRITE},
    };

    if (!fx.ok) {
        for (const Case& c : cases)
            check(c.id, "a data breakpoint set from the disassembly shows up in the panel at once",
                  false, "fixture failed");
        return;
    }

    DisasmPanel* disasm = fx.dbg->disasm_panel();
    BreakpointSet& bps  = fx.emu.debug_state().breakpoints();

    for (const Case& c : cases) {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        fx.dbg->breakpoint_panel()->refresh();

        const bool armed = fill_disasm_window(fx.emu, disasm, 0x8000);
        PopupAnswer ans;
        if (armed) ans = pick_from_disasm(disasm, QString::fromLatin1(c.item));

        const QStringList rows = panel_rows(fx.dbg);
        const bool in_set   = bps.has_watchpoint(0x4000, c.type);
        const bool in_panel = rows.contains(QString::fromLatin1(c.row));

        check(c.id,
              fmt("\"%s\" shows up in the Breakpoints panel at once", c.item).c_str(),
              armed && ans.found && in_set && in_panel,
              fmt("disasm armed=%d popup seen=%d item found=%d in set=%d "
                  "popup held: [%s] panel holds: [%s] (want \"%s\")",
                  armed ? 1 : 0, ans.seen ? 1 : 0, ans.found ? 1 : 0, in_set ? 1 : 0,
                  ans.items.join(QStringLiteral(", ")).toUtf8().constData(),
                  rows.join(QStringLiteral(", ")).toUtf8().constData(), c.row));
    }
}

// ── DBG: the main window's Debug menu is not a dead end ───────────────

static void test_debug_menu()
{
    set_group("DBG");

    MainWindowFixture fx;
    if (!fx.ok) {
        check("GH215-04", "the Debug menu offers the Debugger toggle", false, "fixture failed");
        check("GH215-05", "Debug and View carry the SAME Debugger QAction", false, "fixture failed");
        check("GH215-06", "toggling from Debug enables the debugger and checks both entries", false, "fixture failed");
        check("GH215-07", "the toolbar Debug button still toggles the debugger", false, "fixture failed");
        return;
    }

    QMenuBar* bar = fx.win.menuBar();
    QMenu* debug_menu = menu_named(bar, QStringLiteral("Debug"));
    QMenu* view_menu  = menu_named(bar, QStringLiteral("View"));
    QAction* from_debug = item_named(debug_menu, QStringLiteral("Debugger"));
    QAction* from_view  = item_named(view_menu,  QStringLiteral("Debugger"));

    // GH215-04 — THE issue's second half. The Debug menu held Magic Breakpoint
    // and nothing else, so the menu named after the debugger did not offer it.
    {
        QStringList items;
        if (debug_menu)
            for (QAction* a : debug_menu->actions())
                if (!a->isSeparator()) items << visible(a->text());
        check("GH215-04", "the Debug menu offers the Debugger toggle",
              from_debug != nullptr && from_debug->isCheckable(),
              debug_menu ? fmt("Debug menu holds: %s",
                               items.join(QStringLiteral(" | ")).toUtf8().constData())
                         : "no menu titled \"Debug\" on the main menu bar");
    }

    // GH215-05 — one QAction, two menus. A COPY would give the user two
    // checkmarks that drift apart, which is worse than the dead end being
    // fixed. Pointer identity is the whole mechanism, so it is asserted
    // directly rather than inferred from the states agreeing once.
    check("GH215-05", "Debug and View carry the SAME Debugger QAction",
          from_debug != nullptr && from_debug == from_view,
          fmt("debug=%p view=%p", static_cast<void*>(from_debug),
              static_cast<void*>(from_view)));

    DebuggerManager* mgr = fx.win.debugger_manager();

    // GH215-06 — the observable consequence: driving the Debug-menu entry
    // really enables the debugger, and the View entry shows it. trigger() is
    // used bare, never preceded by setChecked(): QAction::trigger() TOGGLES a
    // checkable action itself and then emits triggered(newState), so pre-
    // setting the state inverts the argument the manager's slot receives — a
    // trigger meant to enable arrives as set_enabled(false).
    {
        const bool was_enabled = mgr->is_enabled();
        if (from_debug && !was_enabled) {
            from_debug->trigger();
            QApplication::processEvents();
        }
        const bool now_enabled = mgr->is_enabled();
        const bool debug_checked = from_debug && from_debug->isChecked();
        const bool view_checked  = from_view  && from_view->isChecked();
        check("GH215-06", "toggling from Debug enables the debugger and checks both entries",
              now_enabled && debug_checked && view_checked,
              fmt("was_enabled=%d now_enabled=%d debug_checked=%d view_checked=%d",
                  was_enabled ? 1 : 0, now_enabled ? 1 : 0,
                  debug_checked ? 1 : 0, view_checked ? 1 : 0));
    }

    // GH215-07 — NOT a regression row for #215: the toolbar button worked
    // before the change and works after. It is recorded rather than assumed
    // because "the toolbar keeps working" was an explicit constraint on this
    // fix, and the toolbar's checkmark rides a DIFFERENT wire from the menu's
    // (DebuggerManager::enabled_changed, not the shared QAction) — so nothing
    // above would notice if the second menu route had broken it.
    {
        QAction* toolbar_btn = nullptr;
        for (QToolBar* tb : fx.win.findChildren<QToolBar*>())
            for (QAction* a : tb->actions())
                if (a->text() == QStringLiteral("Debug") && a->isCheckable())
                    toolbar_btn = a;

        bool ok = false;
        std::string detail = "no checkable \"Debug\" toolbar action";
        if (toolbar_btn) {
            // Whatever state GH215-06 left, drive the button to the opposite
            // and back, and require the manager to follow both times. Bare
            // trigger() again — see GH215-06 on why setChecked() must not
            // precede it. The button starts in sync because DebuggerManager::
            // enabled_changed is wired to its setChecked, which is the wire
            // this row is here to keep alive.
            const bool start = mgr->is_enabled();
            toolbar_btn->trigger();
            QApplication::processEvents();
            const bool flipped = mgr->is_enabled();

            toolbar_btn->trigger();
            QApplication::processEvents();
            const bool restored = mgr->is_enabled();

            ok = (flipped == !start) && (restored == start)
                 && toolbar_btn->isChecked() == start;
            detail = fmt("start=%d flipped=%d restored=%d button_checked=%d",
                         start ? 1 : 0, flipped ? 1 : 0, restored ? 1 : 0,
                         toolbar_btn->isChecked() ? 1 : 0);
        }
        check("GH215-07", "the toolbar Debug button still toggles the debugger",
              ok, detail);
    }
}

int main(int argc, char** argv)
{
    // Both windows own QWidgets, so a QApplication is required — but not a
    // display: force the offscreen QPA platform, the same idiom as the other
    // debugger suites.
    qputenv("QT_QPA_PLATFORM", "offscreen");

    // Isolate the config file: the debugger window restores and saves a
    // geometry, and the real ~/.jnext belongs to the user.
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());

    QApplication app(argc, argv);

    test_breakpoints_menu();
    std::printf("  Group: BPM            — done\n");
    test_breakpoint_panel_refresh();
    std::printf("  Group: BPR            — done\n");
    test_disasm_data_breakpoints();
    std::printf("  Group: BPC            — done\n");
    test_debug_menu();
    std::printf("  Group: DBG            — done\n");

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
