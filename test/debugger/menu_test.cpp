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
// GH #220 (groups BPO and BPX) — the same defect class, closed at the source.
//
// #218 fixed three forgetful call sites by hand. #220 removed the requirement
// to remember: BreakpointSet notifies, and the two panels that draw it
// subscribe. The rows above still pass unchanged — they must, since the refactor
// is not allowed to move any panel update to a different moment — but they all
// drive GUI ROUTES, and every GUI route was already fixed. None of them can tell
// a set that notifies from twelve call sites that were each patched.
//
// So the rows below mutate the BreakpointSet DIRECTLY, with no repaint call
// anywhere near them. That is the future call site the issue is about, in its
// purest form: code that changes the set and tells nobody. On main every one of
// them fails; the panels only catch up on the next refresh_panels() tick.
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
#include "input/emu_fnkeys.h"
#include "memory/mmu.h"
#include "platform/emulator_boot.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QComboBox>
#include <QMenuBar>
#include <QPushButton>
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

// ── GH #222 helper: driving the Breakpoints PANEL's own Add dialog ────
//
// trigger_and_answer() above cannot do this. The panel's Add/Edit/Remove are
// QPushButtons, not QActions, and the dialog they open carries a TYPE COMBO
// that an address-only answer has nothing to say about — which is exactly what
// the two new I/O entries live in. Same armed-beforehand idiom, for the same
// reason: QDialog::exec() does not return until the dialog is answered.

struct PanelAnswer {
    QString typed;                 // into the address field
    int     combo_index = 0;       // into the type combo
    bool    seen        = false;   // a dialog appeared at all
    int     combo_count = 0;       // how many types it offered
};

/// Click `button` and answer whatever modal it opens.
void click_and_answer(QPushButton* button, PanelAnswer& ans) {
    if (!button) return;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&ans]() {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) return;
        ans.seen = true;
        if (QComboBox* combo = dlg->findChild<QComboBox*>()) {
            ans.combo_count = combo->count();
            combo->setCurrentIndex(ans.combo_index);
        }
        if (QLineEdit* edit = dlg->findChild<QLineEdit*>())
            edit->setText(ans.typed);
        dlg->accept();
    });
    timer.start(1);
    button->click();
    timer.stop();
    QApplication::processEvents();
}

/// The Breakpoints panel's button whose VISIBLE text is `text`, or nullptr.
QPushButton* panel_button(DebuggerWindow* dbg, const QString& text) {
    BreakpointPanel* panel = dbg ? dbg->breakpoint_panel() : nullptr;
    if (!panel) return nullptr;
    for (QPushButton* b : panel->findChildren<QPushButton*>())
        if (visible(b->text()) == text) return b;
    return nullptr;
}

/// Select row `row` of the Breakpoints panel's table (what Edit/Remove act on).
void select_panel_row(DebuggerWindow* dbg, int row) {
    BreakpointPanel* panel = dbg ? dbg->breakpoint_panel() : nullptr;
    if (!panel) return;
    if (auto* table = panel->findChild<QTableWidget*>())
        table->setCurrentCell(row, 0);
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

// ── GH #220 helpers: seeing a re-disassembly without depending on paint ──
//
// The one thing the #220 rows must observe about the DISASSEMBLY is whether it
// re-read memory — "did the gutter repaint?" is not answerable through the
// panel's public surface, and counting Qt paint events would make the rows
// depend on offscreen backing-store delivery. So instead: change the bytes the
// view is showing WITHOUT telling the panel, and watch the selected line's
// address. Three-byte instructions put line N at base + 3N; single-byte NOPs
// put it at base + N. The address therefore moves the instant the panel
// re-disassembles, and holds still for as long as it does not.

/// Overwrite the bytes under the disassembly view with single-byte NOPs. The
/// panel is deliberately not told, and nothing here repaints it.
void poke_nops(Emulator& emu, uint16_t base) {
    for (int i = 0; i < 256; ++i)
        emu.mmu().write(static_cast<uint16_t>(base + i), 0x00);
}

/// Select disassembly line `line` (a plain left-click would do, but the popup
/// helper above already reaches contextMenuEvent's `selected_line_ = line`).
/// `wanted` is deliberately unmatched, so nothing is triggered.
void select_disasm_line(DisasmPanel* panel, int line) {
    PopupAnswer ans;
    ans.wanted = QStringLiteral("\x01 no such item");
    // paint_y_offset_ (52) + line * LINE_HEIGHT (18), landing mid-line.
    context_menu_and_pick(panel, QPoint(120, 52 + line * 18 + 5), ans);
}

/// Arm the probe: three-byte instructions under the view, the panel actually
/// showing them, line 3 selected. Returns that line's address — `base + 9`
/// while the view holds three-byte instructions, `base + 3` once it has
/// re-read memory that poke_nops() shortened. 0 if the setup did not take.
///
/// The refresh() here is SETUP, and it is not optional: fill_disasm_window()
/// nudges the scrollbar, and QScrollBar::setValue() is silent when the value
/// does not change — so a probe armed twice in a row would otherwise keep the
/// first run's stale entries_ and measure nothing.
uint16_t arm_disasm_probe(Emulator& emu, DisasmPanel* panel, uint16_t base) {
    if (!panel) return 0;
    panel->set_paused(true);   // the gutter only tracks the machine while paused
    if (!fill_disasm_window(emu, panel, base)) return 0;
    panel->refresh();
    select_disasm_line(panel, 3);
    return panel->selected_address();
}

/// The fixture's machine, named separately because GH220-06 re-boots with it.
EmulatorConfig next_config() {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    return cfg;
}

bool build_next_emulator(Emulator& emu) {
    return emu.init(next_config());
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

// ── BPO: the set notifies; nobody has to remember to repaint ──────────
//
// GH #220. Every row here mutates the BreakpointSet directly — the ONE thing
// none of the thirteen rows above can do, because every one of them drives a
// GUI route and every GUI route was already patched by hand in #215/#218. A
// bare add_pc() with no repaint call anywhere is precisely the "thirteenth call
// site somebody adds next year" the issue exists to make safe.
//
// The clear_all_* + refresh() before each row is SETUP, not the assertion: it
// establishes a truthful panel by a route that works on main too, so the row
// keeps failing on main for its own reason (the mutation went unnoticed) rather
// than for a stale precondition.
//
// As in the #218 groups: no pause(), no step(), no refresh_panels(). The one
// exception is DisasmPanel::set_paused(true) in GH220-04/05, which is not a
// repaint — it only flips the "gutter tracks the machine" flag that the panel
// has always required (refresh() is a documented no-op while running), and it
// rebuilds nothing.

static void test_observer_notifies()
{
    set_group("BPO");

    DebuggerFixture fx;
    if (!fx.ok) {
        check("GH220-01", "a bare add_pc() with no repaint call still reaches the panel", false, "fixture failed");
        check("GH220-02", "a bare add_watchpoint() with no repaint call still reaches the panel", false, "fixture failed");
        check("GH220-03", "bare removals with no repaint call still reach the panel", false, "fixture failed");
        check("GH220-04", "a PC change re-disassembles the gutter; a watchpoint change does not", false, "fixture failed");
        check("GH220-05", "a one-shot breakpoint reaches neither panel", false, "fixture failed");
        return;
    }

    BreakpointSet& bps = fx.emu.debug_state().breakpoints();

    // GH220-01 — THE point of the exercise, Execute half.
    {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        fx.dbg->breakpoint_panel()->refresh();   // setup: a truthful panel

        bps.add_pc(0x1234);                      // no repaint call, anywhere

        const QStringList rows = panel_rows(fx.dbg);
        check("GH220-01", "a bare add_pc() with no repaint call still reaches the panel",
              rows == QStringList{QStringLiteral("Execute $1234")},
              fmt("panel holds: [%s] (want exactly \"Execute $1234\")",
                  rows.join(QStringLiteral(", ")).toUtf8().constData()));
    }

    // GH220-02 — the data half. Separate row: the two live in different
    // containers (pc_bps_ vs watchpoints_) and notify with different kinds, so
    // a mechanism wired to only one of them passes exactly one of these.
    {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        fx.dbg->breakpoint_panel()->refresh();

        bps.add_watchpoint(0x4000, WatchType::READ);

        const QStringList rows = panel_rows(fx.dbg);
        check("GH220-02", "a bare add_watchpoint() with no repaint call still reaches the panel",
              rows == QStringList{QStringLiteral("Read $4000")},
              fmt("panel holds: [%s] (want exactly \"Read $4000\")",
                  rows.join(QStringLiteral(", ")).toUtf8().constData()));
    }

    // GH220-03 — the other direction, per item rather than Clear All (which
    // GH218-04 already covers through the menu). Removal is its own pair of
    // mutators and would be its own pair of forgotten repaints.
    {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        bps.add_pc(0x1234);
        bps.add_watchpoint(0x4000, WatchType::READ);
        fx.dbg->breakpoint_panel()->refresh();
        const int before = panel_rows(fx.dbg).size();

        bps.remove_pc(0x1234);
        const QStringList after_pc = panel_rows(fx.dbg);

        bps.remove_watchpoint(0x4000, WatchType::READ);
        const QStringList after_wp = panel_rows(fx.dbg);

        check("GH220-03", "bare removals with no repaint call still reach the panel",
              before == 2
                  && after_pc == QStringList{QStringLiteral("Read $4000")}
                  && after_wp.isEmpty(),
              fmt("rows before=%d after remove_pc: [%s] after remove_watchpoint: [%s]",
                  before,
                  after_pc.join(QStringLiteral(", ")).toUtf8().constData(),
                  after_wp.join(QStringLiteral(", ")).toUtf8().constData()));
    }

    DisasmPanel* disasm = fx.dbg->disasm_panel();

    // GH220-04 — the asymmetry the issue insists must SURVIVE. The gutter
    // paints has_pc() and nothing else, so an observer that re-disassembles on
    // every watchpoint change is doing useless work, not fixing a bug. Both
    // halves are one row on purpose: "no re-disassembly on a watchpoint" is
    // vacuously true of a panel that never re-disassembles at all, so it is
    // only meaningful next to a PC change that does.
    {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();

        // Three-byte instructions, line 3 selected, address noted.
        const uint16_t at_3byte = arm_disasm_probe(fx.emu, disasm, 0x8000);
        const bool     armed    = (at_3byte != 0);

        // Shorten every instruction behind the panel's back. Nothing repaints.
        poke_nops(fx.emu, 0x8000);

        bps.add_watchpoint(0x5000, WatchType::WRITE);
        const uint16_t after_watch = disasm ? disasm->selected_address() : 0;

        bps.add_pc(0x8001);
        const uint16_t after_pc = disasm ? disasm->selected_address() : 0;

        check("GH220-04", "a PC change re-disassembles the gutter; a watchpoint change does not",
              armed && at_3byte == 0x8009 && after_watch == 0x8009 && after_pc == 0x8003,
              fmt("armed=%d line 3 at $%04X (want $8009); after add_watchpoint $%04X "
                  "(want $8009, unmoved); after add_pc $%04X (want $8003, re-read)",
                  armed ? 1 : 0, at_3byte, after_watch, after_pc));
    }

    // GH220-05 — one-shots stay invisible. rebuild_entries() reads
    // pc_breakpoints() + watchpoints() only, and every path that sets a one-shot
    // (resume / step_over / run_to) immediately resumes — so a set that notified
    // on set_oneshot()/clear_oneshot() would fire on every resume, for something
    // no panel draws. Both surfaces are checked: no new list row, no re-read of
    // memory by the disassembly.
    {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        fx.dbg->breakpoint_panel()->refresh();

        const uint16_t at_3byte = arm_disasm_probe(fx.emu, disasm, 0x8000);
        const bool     armed    = (at_3byte != 0);
        poke_nops(fx.emu, 0x8000);

        fx.emu.debug_state().run_to(0x9000);        // set_oneshot()
        const bool         armed_oneshot = bps.has_oneshot();
        const QStringList  rows_set      = panel_rows(fx.dbg);
        const uint16_t     after_set     = disasm ? disasm->selected_address() : 0;

        fx.emu.debug_state().resume();              // clear_oneshot()
        const QStringList rows_clear  = panel_rows(fx.dbg);
        const uint16_t    after_clear = disasm ? disasm->selected_address() : 0;

        // Positive control, on the SAME armed probe: a row that only asserts
        // "nothing happened" passes against a set that notifies nobody at all.
        // A real PC breakpoint must still move both surfaces here.
        bps.add_pc(0x8001);
        const QStringList rows_real  = panel_rows(fx.dbg);
        const uint16_t    after_real = disasm ? disasm->selected_address() : 0;

        check("GH220-05", "a one-shot breakpoint reaches neither panel",
              armed && armed_oneshot && at_3byte == 0x8009
                  && rows_set.isEmpty() && rows_clear.isEmpty()
                  && after_set == 0x8009 && after_clear == 0x8009
                  && rows_real == QStringList{QStringLiteral("Execute $8001")}
                  && after_real == 0x8003,
              fmt("armed=%d oneshot=%d line 3 at $%04X (want $8009) -> after set $%04X, "
                  "after clear $%04X (both want $8009); panel after set: [%s] after clear: [%s]; "
                  "control add_pc -> $%04X (want $8003) panel: [%s] (want \"Execute $8001\")",
                  armed ? 1 : 0, armed_oneshot ? 1 : 0, at_3byte, after_set, after_clear,
                  rows_set.join(QStringLiteral(", ")).toUtf8().constData(),
                  rows_clear.join(QStringLiteral(", ")).toUtf8().constData(),
                  after_real,
                  rows_real.join(QStringLiteral(", ")).toUtf8().constData()));
    }
}

// ── BPX: the subscription's two lifetime edges ────────────────────────
//
// GH #220. Observation is only structurally safe if it survives what the
// product does to the set and to the panels. Two things do:
//
//   * emulator_cold_boot() (a hard reset / F1 / NR 0x02 bit 1) SAVES this set
//     by value, destroys the whole Emulator, reconstructs it in place, and
//     restores the set — deliberately, so a target reset does not silently
//     discard the user's breakpoints. If the observers did not travel with it,
//     both panels would go quietly stale after every reset: a fresh instance of
//     the very defect this issue closes.
//
//   * a panel that goes away must stop being called, and must not take the
//     other subscriber with it.

static void test_observer_lifetime()
{
    set_group("BPX");

    DebuggerFixture fx;
    if (!fx.ok) {
        check("GH220-06", "the panel keeps observing across a cold boot", false, "fixture failed");
        check("GH220-07", "a destroyed subscriber unsubscribes without disturbing the others", false, "fixture failed");
        return;
    }

    // GH220-06
    {
        fx.emu.debug_state().breakpoints().clear_all_pc();
        fx.emu.debug_state().breakpoints().clear_all_watchpoints();
        fx.dbg->breakpoint_panel()->refresh();

        emulator_cold_boot(fx.emu, next_config());

        // A different BreakpointSet object now, restored from the saved copy.
        fx.emu.debug_state().breakpoints().add_pc(0x2000);

        const QStringList rows = panel_rows(fx.dbg);
        check("GH220-06", "the panel keeps observing across a cold boot",
              rows == QStringList{QStringLiteral("Execute $2000")},
              fmt("panel holds: [%s] (want exactly \"Execute $2000\")",
                  rows.join(QStringLiteral(", ")).toUtf8().constData()));
    }

    // GH220-07 — a second, throwaway BreakpointPanel subscribes on construction
    // and must unsubscribe on destruction. What is asserted is the SURVIVOR:
    // remove_observer() taking out the wrong entry (or all of them) leaves the
    // window's own panel deaf, which this row catches directly. The dangling
    // half — remove_observer() doing nothing, so the freed panel keeps being
    // called — is caught here only under a sanitiser, and is not claimed
    // otherwise.
    {
        auto* extra = new BreakpointPanel(&fx.emu);
        delete extra;

        BreakpointSet& bps = fx.emu.debug_state().breakpoints();
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        fx.dbg->breakpoint_panel()->refresh();

        bps.add_pc(0x3000);

        const QStringList rows = panel_rows(fx.dbg);
        check("GH220-07", "a destroyed subscriber unsubscribes without disturbing the others",
              rows == QStringList{QStringLiteral("Execute $3000")},
              fmt("panel holds: [%s] (want exactly \"Execute $3000\")",
                  rows.join(QStringLiteral(", ")).toUtf8().constData()));
    }
}

// ── BPI: the panel can create an I/O watchpoint ───────────────────────
//
// GH #222. WatchType::IO_READ and IO_WRITE existed in the enum and no route in
// the product could produce one: the panel's Add dialog offered Execute /
// Read / Write / Read-Write, so the two values were unreachable by a user.
//
// These rows drive the REAL dialog through the REAL Add button and read the
// REAL BreakpointSet, exactly as GH215-02 does for Execute — "the combo has
// six entries" is not the assertion, "a user who picks the sixth gets an
// IO_WRITE watchpoint" is. Each also checks the WRONG type is absent, because
// the four index->WatchType sites in breakpoint_panel.cpp all default to READ:
// an off-by-one there produces a breakpoint, just not the requested one.
//
// Mutation-tested one at a time against the product (reverted from a `cp`
// backup each time):
//   the two addItem() calls removed          -> GH222-01/02/03 all fail; the
//                                               combo reports 4 types and an
//                                               out-of-range setCurrentIndex
//                                               leaves it at -1, so the panel
//                                               shows "Read $00FE"
//   watch_type_for() case 5 -> IO_READ       -> GH222-02/03 fail on the type,
//                                               GH222-01 still passes
//   rebuild_entries() IO_WRITE -> ti = 2     -> GH222-02 fails on the panel
//                                               row ("Write $243B"), and
//                                               GH222-03 on the removal it
//                                               then misdirects
//   type_name() cases 4/5 removed            -> all three fail ("? $243B")
//   on_remove() reverted to the old inline
//   map (which cannot express 4/5)           -> GH222-03 alone fails: the row
//                                               is still in the panel after
//                                               Remove

static void test_panel_io_breakpoints()
{
    set_group("BPI");

    DebuggerFixture fx;
    if (!fx.ok) {
        check("GH222-01", "the panel's Add dialog creates an IO_READ watchpoint", false, "fixture failed");
        check("GH222-02", "the panel's Add dialog creates an IO_WRITE watchpoint", false, "fixture failed");
        check("GH222-03", "an I/O watchpoint can be removed again from the panel", false, "fixture failed");
        return;
    }

    BreakpointSet& bps = fx.emu.debug_state().breakpoints();
    QPushButton* add_btn = panel_button(fx.dbg, QStringLiteral("Add"));

    // GH222-01 — index 4, "IO Read". FE is the partial-decode form: any port
    // with that low byte (see test/debug/io_watchpoint_test.cpp).
    {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        fx.dbg->breakpoint_panel()->refresh();

        PanelAnswer ans;
        ans.typed       = QStringLiteral("00FE");
        ans.combo_index = 4;
        click_and_answer(add_btn, ans);

        const QStringList rows = panel_rows(fx.dbg);
        check("GH222-01", "the panel's Add dialog creates an IO_READ watchpoint",
              add_btn && ans.seen
                  && bps.has_watchpoint(0x00FE, WatchType::IO_READ)
                  && !bps.has_watchpoint(0x00FE, WatchType::READ)
                  && bps.pc_breakpoints().empty()
                  && rows == QStringList{QStringLiteral("IO Read $00FE")},
              fmt("button=%d dialog seen=%d combo offered %d types; panel holds: [%s] "
                  "(want exactly \"IO Read $00FE\")",
                  add_btn ? 1 : 0, ans.seen ? 1 : 0, ans.combo_count,
                  rows.join(QStringLiteral(", ")).toUtf8().constData()));
    }

    // GH222-02 — index 5, "IO Write", and on a FULL-decode address: 243B is
    // the NextREG select port and not 253B, whose low byte is the same.
    {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        fx.dbg->breakpoint_panel()->refresh();

        PanelAnswer ans;
        ans.typed       = QStringLiteral("243B");
        ans.combo_index = 5;
        click_and_answer(add_btn, ans);

        const QStringList rows = panel_rows(fx.dbg);
        check("GH222-02", "the panel's Add dialog creates an IO_WRITE watchpoint",
              add_btn && ans.seen
                  && bps.has_watchpoint(0x243B, WatchType::IO_WRITE)
                  && !bps.has_watchpoint(0x243B, WatchType::IO_READ)
                  && !bps.has_watchpoint(0x243B, WatchType::WRITE)
                  && rows == QStringList{QStringLiteral("IO Write $243B")},
              fmt("button=%d dialog seen=%d combo offered %d types; panel holds: [%s] "
                  "(want exactly \"IO Write $243B\")",
                  add_btn ? 1 : 0, ans.seen ? 1 : 0, ans.combo_count,
                  rows.join(QStringLiteral(", ")).toUtf8().constData()));
    }

    // GH222-03 — the round trip. Remove reconstructs the WatchType from the
    // table row's type index, so a mapping that cannot express the two I/O
    // values removes the wrong type — which removes nothing, and leaves the
    // row sitting in the panel for ever.
    {
        bps.clear_all_pc();
        bps.clear_all_watchpoints();
        fx.dbg->breakpoint_panel()->refresh();

        PanelAnswer ans;
        ans.typed       = QStringLiteral("243B");
        ans.combo_index = 5;
        click_and_answer(add_btn, ans);
        const QStringList before = panel_rows(fx.dbg);

        select_panel_row(fx.dbg, 0);
        QPushButton* remove_btn = panel_button(fx.dbg, QStringLiteral("Remove"));
        if (remove_btn) {
            remove_btn->click();
            QApplication::processEvents();
        }

        const QStringList after = panel_rows(fx.dbg);
        check("GH222-03", "an I/O watchpoint can be removed again from the panel",
              remove_btn && before == QStringList{QStringLiteral("IO Write $243B")}
                  && !bps.has_watchpoint(0x243B, WatchType::IO_WRITE)
                  && bps.watchpoints().empty() && after.isEmpty(),
              fmt("button=%d panel before: [%s] after: [%s] (want empty)",
                  remove_btn ? 1 : 0,
                  before.join(QStringLiteral(", ")).toUtf8().constData(),
                  after.join(QStringLiteral(", ")).toUtf8().constData()));
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

// ── F5R: Run on an already-running machine is a no-op (GH #223) ───────
//
// DebuggerManager::on_run() had no pause check, unlike on_run_to_eof() and
// on_run_to_eosl(), which both open with `if (!paused()) return;`. So F5 on a
// machine that was already running ran DebugState::resume()'s body anyway.
// After #221 the dangerous half of that is gone (no step-off arm is raised
// without a real paused -> running edge, so no breakpoint is swallowed) and
// exactly one observable effect was left:
//
//     void DebugState::resume() {
//         unpause_();                   // no-op when not paused
//         step_mode_ = StepMode::NONE;
//         data_bp_hit_ = false;
//         breakpoints_.clear_oneshot(); // <-- the pending Run to Here, gone
//     }
//
// It is easy to hit and gives no feedback: MainWindow's F5 handler forwards to
// on_run() whenever the debugger is ENABLED — no debugger focus or visibility
// required — so set a Run to Here, alt-tab to the emulator, press F5 out of
// habit, and the target you are waiting for has been cancelled. The machine
// runs on and nothing says why.
//
// Owner's decision: F5 on a running machine is a no-op, and is NOT passed to
// the guest either.
//
// WHAT THE ROWS PIN
//
//   GH223-01  the normal path, unchanged: on_run() while PAUSED still resumes.
//             This is the row that fails if the guard is written the wrong way
//             round, which is the one plausible way to get this wrong.
//   GH223-02  THE DEFECT: on_run() while RUNNING leaves a pending one-shot
//             alone, so the Run to Here still fires.
//   GH223-03  the key never reaches the emulated machine.
//
// GH223-03 is not decoration, but be precise about what it defends. A "no-op"
// F5 that fell through would reach `case Qt::Key_F5` further down MainWindow::
// keyPressEvent and drive the guest's F-key FSM — and TODAY that is INERT:
// jnext implements no F5 side effect at all (emu_fnkeys.h:68-69 puts F5/F6 out
// of scope for G132, tracked under G147; fire_mf_side_effects() dispatches
// F2/F3/F7/F8 only), so no NextREG is written. The fall-through mutation below
// measures exactly that — the FSM moves and nothing else does.
//
// On real HARDWARE F5 is the expansion-bus enable hotkey (zxnext.vhd:6344
// `hotkey_expbus_enable`, effect at :2190 `nr_80_expbus(7) <= '1'`), and NR 0x80
// bit 7 does have live consumers in jnext — NmiSource, and NR 0x07's
// actual-speed readback (emulator.cpp:1460, :8972). It is only the F5 WRITE
// path into that bit which is missing. So this row pins the contract against a
// future G147 completion rather than against a coupling that exists now, and it
// fails if someone later "tidies" the intercept into a fall-through.
//
// Observing it needs a trick, because a completed F5 dispatch leaves no trace:
// simulate_mf_fkey_press() drives IDLE -> A11 -> A12 -> CHECK -> DONE -> IDLE
// and clears local_fnkeys on the way out, so before and after are identical on
// a fresh FSM. So the FSM is ARMED into a distinctive mid-scan state first
// (a membrane F3 press plus one tick = MF_ROW_A11): a dispatched F5 would call
// press_membrane_fkey(5) + 5 ticks + release_membrane() straight over the top
// of it and leave the FSM back at IDLE, while a consumed F5 leaves it exactly
// where it was. Both directions are asserted in the one row — the
// debugger-disabled arm is what proves the observation has teeth.
//
// Discriminative — each row was mutation-tested against the product, one
// mutation at a time (the product reverted from a `cp` backup each time):
//   the new `if (!paused()) return;` removed from on_run()
//                                 -> GH223-02 fails (PC=$800E: the one-shot was
//                                    cancelled), -01 and -03 still pass.
//   the guard inverted to `if (paused()) return;`
//                                 -> BOTH -01 and -02 fail; -03 still passes.
//                                    -02 dies here too, which is worth stating
//                                    because it is not obvious: inverting the
//                                    guard blocks only the paused case, so the
//                                    running case still falls through to
//                                    resume() and still cancels the one-shot.
//                                    -01 is therefore the row that distinguishes
//                                    this mutation from the one above.
//   MainWindow's F5 `event->accept(); return;` replaced by a fall-through
//                                 -> GH223-03 fails (debugger-on state moves to
//                                    MF_CHECK: the FSM was driven), -01/-02 pass.
//                                    This one also settles a question the row
//                                    raises: it proves -03 is measuring
//                                    MainWindow's intercept and not the debugger
//                                    window's own F5 QAction, which would
//                                    otherwise be an alternative explanation for
//                                    a consumed key.

namespace {

/// A 48K machine with a real DebuggerManager attached. The program is
/// byte-identical to test/debug/resume_step_off_test.cpp's LINEAR fixture, so
/// the DebugState-level suite and this DebuggerManager-level one are talking
/// about the same machine:
///
///   8000  00 x6        NOP x6
///   8006  3A 00 90     LD A,(0x9000)   <- the breakpoint we stop at first
///   8009  3E 5A        LD A,0x5A
///   800B  32 00 90     LD (0x9000),A   <- the Run to Here target
///   800E  18 FE        JR $            <- park; where a cancelled one-shot ends
struct RunGuardFixture {
    static constexpr uint16_t PROG = 0x8000;
    static constexpr uint16_t BP   = 0x8006;
    static constexpr uint16_t TGT  = 0x800B;
    static constexpr uint16_t PARK = 0x800E;

    Emulator         emu;
    QMainWindow      win;
    DebuggerManager* mgr = nullptr;
    bool             ok  = false;

    RunGuardFixture() {
        EmulatorConfig cfg;
        cfg.type                 = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 0;
        if (!emu.init(cfg)) return;

        // Interrupts off, so no frame interrupt lands in the middle of a
        // measured run; SP parked well away from the program.
        Z80Registers r = emu.cpu().get_registers();
        r.PC   = PROG;
        r.SP   = 0xFF00;
        r.IFF1 = 0;
        r.IFF2 = 0;
        emu.cpu().set_registers(r);

        static const uint8_t prog[] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x3A, 0x00, 0x90,
            0x3E, 0x5A,
            0x32, 0x00, 0x90,
            0x18, 0xFE,
        };
        for (size_t i = 0; i < sizeof(prog); ++i)
            emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);
        emu.mmu().write(0x9000, 0x00);

        mgr = new DebuggerManager(&win, &emu, &win);   // parented → auto-freed
        ok  = mgr->set_enabled(true);                  // also set_active(true)
    }
    ~RunGuardFixture() {
        if (mgr) mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
    }

    uint16_t pc() { return emu.cpu().get_registers().PC; }
    bool paused() { return emu.debug_state().paused(); }
    void run_until_paused(int max_frames = 4) {
        for (int i = 0; i < max_frames && !paused(); ++i)
            emu.run_frame();
    }
};

} // namespace

static void test_run_guard()
{
    set_group("F5R");

    // GH223-01 — the normal path is untouched. Stop at a breakpoint, clear it,
    // press Run: the machine must resume and reach the park. The guard is a
    // one-line early return and the obvious way to get it wrong is to test the
    // wrong sense of paused(), which this row and GH223-02 catch between them.
    {
        RunGuardFixture fx;
        if (!fx.ok) {
            check("GH223-01", "Run on a PAUSED machine still resumes it",
                  false, "fixture failed");
        } else {
            fx.emu.debug_state().breakpoints().add_pc(RunGuardFixture::BP);
            fx.run_until_paused();
            const bool stopped = fx.paused() && fx.pc() == RunGuardFixture::BP;

            fx.emu.debug_state().breakpoints().remove_pc(RunGuardFixture::BP);
            fx.mgr->on_run();
            const bool released = !fx.paused();

            fx.run_until_paused();
            check("GH223-01", "Run on a PAUSED machine still resumes it",
                  stopped && released && !fx.paused()
                      && fx.pc() == RunGuardFixture::PARK,
                  fmt("stopped=%d released=%d after-run paused=%d PC=$%04X "
                      "(want $%04X)", stopped ? 1 : 0, released ? 1 : 0,
                      fx.paused() ? 1 : 0, fx.pc(), RunGuardFixture::PARK));
        }
    }

    // GH223-02 — THE DEFECT. Stop at a breakpoint, ask for Run to Here at
    // 0x800B (which unpauses and leaves a one-shot armed), then press F5 as the
    // user does out of habit at the emulator window. The one-shot must survive
    // and stop the machine at 0x800B.
    //
    // Before the guard, on_run()'s resume() called clear_oneshot() and the
    // machine sailed past 0x800B to the park at 0x800E without ever pausing —
    // so PC=$800E with paused=0 is precisely the failure this row names.
    //
    // The Run to Here is issued through DebugState::run_to(), which is exactly
    // what DebuggerManager's run_to_requested handler does. That handler is
    // deliberately NOT guarded (a fresh one-shot is sensible on a running
    // machine); on_run() is the one that had no business touching it.
    {
        RunGuardFixture fx;
        if (!fx.ok) {
            check("GH223-02", "Run on a RUNNING machine leaves a pending "
                  "Run-to-Here one-shot alone", false, "fixture failed");
        } else {
            fx.emu.debug_state().breakpoints().add_pc(RunGuardFixture::BP);
            fx.run_until_paused();
            const bool stopped = fx.paused() && fx.pc() == RunGuardFixture::BP;

            fx.emu.debug_state().breakpoints().remove_pc(RunGuardFixture::BP);
            fx.emu.debug_state().run_to(RunGuardFixture::TGT);   // Run to Here
            const bool running = !fx.paused();

            fx.mgr->on_run();          // the habitual, redundant F5
            fx.run_until_paused();

            check("GH223-02", "Run on a RUNNING machine leaves a pending "
                  "Run-to-Here one-shot alone",
                  stopped && running && fx.paused()
                      && fx.pc() == RunGuardFixture::TGT,
                  fmt("stopped=%d running=%d paused=%d PC=$%04X (want $%04X; "
                      "$%04X means the one-shot was cancelled)",
                      stopped ? 1 : 0, running ? 1 : 0, fx.paused() ? 1 : 0,
                      fx.pc(), RunGuardFixture::TGT, RunGuardFixture::PARK));
        }
    }

    // GH223-03 — and the no-op F5 does not leak into the guest. See the group
    // banner for why the FSM has to be pre-armed to see this at all, and for
    // what a fall-through would and would not reach.
    {
        MainWindowFixture fx;
        DebuggerManager*  mgr = fx.ok ? fx.win.debugger_manager() : nullptr;
        if (!mgr) {
            check("GH223-03", "F5 with the debugger enabled never reaches the "
                  "guest F-key FSM", false, "fixture failed");
        } else {
            mgr->set_enabled(true);
            // The machine must be RUNNING: that is the case under test.
            if (fx.emu.debug_state().paused())
                fx.emu.debug_state().resume();
            fx.win.activateWindow();

            // Arm the guest FSM mid-scan. No processEvents() from here to the
            // read-back: MainWindow drives run_frame() off a QTimer, and a
            // frame would tick the FSM underneath the measurement.
            auto arm = [&fx]() {
                fx.emu.emu_fnkeys().reset();
                fx.emu.emu_fnkeys().press_membrane_fkey(3);
                fx.emu.emu_fnkeys().tick();      // IDLE -> MF_ROW_A11
            };

            arm();
            const auto armed = fx.emu.emu_fnkeys().state();
            QKeyEvent on(QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier);
            QApplication::sendEvent(&fx.win, &on);
            const auto after_enabled = fx.emu.emu_fnkeys().state();

            // Control: with the debugger OFF the very same key DOES drive the
            // FSM to completion. Without this half the row would pass against a
            // build where F5 never reached the FSM for some unrelated reason.
            mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
            arm();
            QKeyEvent off(QEvent::KeyPress, Qt::Key_F5, Qt::NoModifier);
            QApplication::sendEvent(&fx.win, &off);
            const auto after_disabled = fx.emu.emu_fnkeys().state();

            // The FSM only ever advances when something ticks it, so "did not
            // move" is exactly "was never dispatched". The control arm's
            // landing state is deliberately NOT pinned to a literal: five
            // unsolicited ticks from a mid-scan state land where the FSM's own
            // transition table puts them (MF_CHECK, as it happens), and that is
            // an artefact of the arming trick rather than part of the contract.
            // What is asserted is the only thing that matters — it moved.
            const bool armed_ok = (armed == EmuFnKeys::State::MF_ROW_A11);
            const bool held     = (after_enabled == armed);
            const bool control  = (after_disabled != armed);

            check("GH223-03", "F5 with the debugger enabled never reaches the "
                  "guest F-key FSM",
                  armed_ok && held && control,
                  fmt("armed=%d (want MF_ROW_A11=1) debugger-on state=%d "
                      "(want unchanged, =%d) debugger-off state=%d (want "
                      "anything else — the FSM must have been driven)",
                      static_cast<int>(armed), static_cast<int>(after_enabled),
                      static_cast<int>(armed),
                      static_cast<int>(after_disabled)));
        }
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
    test_observer_notifies();
    std::printf("  Group: BPO            — done\n");
    test_observer_lifetime();
    std::printf("  Group: BPX            — done\n");
    test_panel_io_breakpoints();
    std::printf("  Group: BPI            — done\n");
    test_debug_menu();
    std::printf("  Group: DBG            — done\n");
    test_run_guard();
    std::printf("  Group: F5R            — done\n");

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
