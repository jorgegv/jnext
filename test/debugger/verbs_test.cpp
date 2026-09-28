// ===========================================================================
// GH #278 WP0 — the Qt debugger's VERBS and window controls, pinned on the
// CURRENT tree.
//
// No VHDL oracle: this is host debugger behaviour. The oracle is where the
// machine ends up after a verb (PC, registers, frame number), what the panels
// show at each edge, what the window's rewind / trace / MAP controls do, and
// which modal (if any) the user is shown.
//
// WHY THIS SUITE EXISTS. GH #278 replaces DebuggerManager's direct
// `Emulator*` calls with the backend's verbs (src/debug/debugger.h). The
// verbs had no GUI-level rows: on_step_over(), on_step_back(),
// on_rewind_to_frame() and update_rewind_ui() appeared in no suite, the pause
// edge's panel sequence was asserted only for the disassembly, and the MAP /
// trace menus only as a means (qt-frontend.md §6.2). These rows pin today's
// behaviour so they are green on both trees by construction.
//
// HOW THE GUI IS DRIVEN. The real DebuggerManager + DebuggerWindow + panels
// against a real Emulator, with the frame loop QtApp runs simulated by
// tick(): run a frame unless paused, then check_breakpoint_hit() and
// refresh_panels() — the exact post-frames sequence (qt_app.cpp:731-733,
// frame_sequencer.h:209). Modal dialogs (message boxes, the file dialogs, the
// rewind size dialog) are answered by Modals, the quit_gate_test watcher
// widened to every kind this window opens; it RECORDS each one before
// answering it, so a row that expects none fails cleanly instead of hanging.
//
// WHAT THE ROWS ASSERT: observable results — PC after a verb, a label's text,
// a menu item's enabled state, a file on disk, a modal's title. Step Over is
// asserted by where the machine STOPS, never by the one-shot it arms.
//
// Qt is required (the panels are QWidgets); a display is not: main() forces
// the offscreen QPA platform, the same idiom as the other debugger suites.
// Run: ./build/test/debugger_verbs_test
// ===========================================================================

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/rzx_player.h"
#include "debug/breakpoints.h"
#include "debug/call_stack.h"
#include "debug/debug_state.h"
#include "debug/disasm_text.h"
#include "debug/rewind_buffer.h"
#include "debug/symbol_table.h"
#include "debug/trace.h"
#include "debugger/breakpoint_panel.h"
#include "debugger/callstack_panel.h"
#include "debugger/cpu_panel.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/disasm_panel.h"
#include "debugger/nextreg_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/video_panel.h"
#include "memory/mmu.h"
#include "port/nextreg.h"
#include "video/renderer.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

// ── Test infrastructure (mirrors the other debugger suites) ───────────

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
    report_row_id(id);
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
    char buf[2048];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

std::string s(const QString& q) { return q.toStdString(); }

QTemporaryDir* g_tmp = nullptr;

constexpr uint16_t PROG    = 0x8000;
constexpr uint16_t TEST_SP = 0xFF00;

// ── Fixture: a real Emulator, a bare main window, the real manager ────

struct Fixture {
    Emulator         emu;
    QMainWindow      win;
    DebuggerManager* mgr = nullptr;
    bool             ok  = false;

    /// `paused`: the machine is paused before the debugger is enabled, so the
    /// panels come up in their paused state.
    explicit Fixture(MachineType type = MachineType::ZX48K, int rewind_frames = 0,
                     bool paused = true) {
        EmulatorConfig cfg;
        cfg.type                 = type;
        cfg.rewind_buffer_frames = rewind_frames;
        if (!emu.init(cfg)) return;
        mgr = new DebuggerManager(&win, &emu, &win);   // parented -> freed
        if (paused) emu.debug_state().pause();
        ok = true;
    }

    // The DebuggerWindow is a parentless top-level the manager never deletes
    // (DebuggerManager::ensure_window()). Left alive it would outlive this
    // Emulator and repaint on the next row's event loop through dangling
    // panel pointers, so the fixture closes it and deletes it while the
    // Emulator it observes still exists.
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

    void load(uint16_t addr, std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes) emu.mmu().write(addr++, b);
    }

    /// PC/SP set, interrupts off unless the row asks for them.
    void regs(uint16_t pc, std::function<void(Z80Registers&)> more = {}) {
        Z80Registers r = emu.cpu().get_registers();
        r.PC = pc; r.SP = TEST_SP; r.IFF1 = 0; r.IFF2 = 0;
        if (more) more(r);
        emu.cpu().set_registers(r);
    }

    uint16_t pc() { return emu.cpu().get_registers().PC; }
    bool paused() { return emu.debug_state().paused(); }

    /// One QtApp frame tick: the frame (unless the debugger holds the
    /// machine), then the two post-frames calls.
    void tick() {
        if (!emu.debug_state().paused()) emu.run_frame();
        mgr->check_breakpoint_hit();
        mgr->refresh_panels();
    }

    /// Tick until the machine pauses. Bounded.
    bool tick_until_paused(int max_ticks = 8) {
        for (int i = 0; i < max_ticks && !paused(); ++i) tick();
        return paused();
    }
};

// ── Reading the window ────────────────────────────────────────────────

QLabel* label_with_text(QWidget* root, const QString& text) {
    if (!root) return nullptr;
    for (QLabel* l : root->findChildren<QLabel*>())
        if (l->text() == text) return l;
    return nullptr;
}

/// The value label beside a CPU-panel register name ("PC: ").
QString cpu_value(DebuggerWindow* dbg, const char* name) {
    CpuPanel* cpu = dbg ? dbg->cpu_panel() : nullptr;
    QLabel* anchor = label_with_text(cpu, QString::fromLatin1(name));
    if (!anchor) return QStringLiteral("<no label>");
    for (QGridLayout* g : cpu->findChildren<QGridLayout*>()) {
        const int idx = g->indexOf(anchor);
        if (idx < 0) continue;
        int r = 0, c = 0, rs = 0, cs = 0;
        g->getItemPosition(idx, &r, &c, &rs, &cs);
        QLayoutItem* it = g->itemAtPosition(r, c + 1);
        auto* v = it ? qobject_cast<QLabel*>(it->widget()) : nullptr;
        return v ? v->text() : QStringLiteral("<no value>");
    }
    return QStringLiteral("<no grid>");
}

QString table_cell(QWidget* panel, int row, int col) {
    auto* t = panel ? panel->findChild<QTableWidget*>() : nullptr;
    QTableWidgetItem* it = t ? t->item(row, col) : nullptr;
    return it ? it->text() : QStringLiteral("<no cell>");
}

int table_rows(QWidget* panel) {
    auto* t = panel ? panel->findChild<QTableWidget*>() : nullptr;
    return t ? t->rowCount() : -1;
}

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

QMenu* menu_named(QMenuBar* bar, const QString& title) {
    if (!bar) return nullptr;
    for (QAction* a : bar->actions())
        if (a->menu() && visible(a->text()) == title) return a->menu();
    return nullptr;
}

QAction* item_named(QMenu* menu, const QString& text) {
    if (!menu) return nullptr;
    for (QAction* a : menu->actions())
        if (visible(a->text()) == text) return a;
    return nullptr;
}

QMenu* submenu_named(QMenu* menu, const QString& text) {
    QAction* a = item_named(menu, text);
    return a ? a->menu() : nullptr;
}

QAction* debug_item(DebuggerWindow* dbg, const QString& text) {
    return item_named(menu_named(dbg ? dbg->menuBar() : nullptr, "Debug"), text);
}

QAction* debug_sub_item(DebuggerWindow* dbg, const QString& sub, const QString& text) {
    return item_named(submenu_named(menu_named(dbg ? dbg->menuBar() : nullptr, "Debug"), sub),
                      text);
}

bool action_enabled(QAction* a) { return a && a->isEnabled(); }

QPushButton* button_where(QWidget* root, const std::function<bool(const QString&)>& pred) {
    if (!root) return nullptr;
    for (QPushButton* b : root->findChildren<QPushButton*>())
        if (pred(b->text())) return b;
    return nullptr;
}

QPushButton* button_ending(QWidget* root, const QString& suffix) {
    return button_where(root, [&](const QString& t) { return t.endsWith(suffix); });
}

QToolBar* toolbar_titled(QWidget* root, const QString& title) {
    if (!root) return nullptr;
    for (QToolBar* t : root->findChildren<QToolBar*>())
        if (t->windowTitle() == title) return t;
    return nullptr;
}

QString status_of(QMainWindow* w) {
    return w && w->statusBar() ? w->statusBar()->currentMessage() : QString();
}

/// The rewind toolbar's "Frame n / m" label (the other label is its title).
QLabel* rewind_frame_label(DebuggerWindow* dbg) {
    QToolBar* tb = toolbar_titled(dbg, "Rewind");
    if (!tb) return nullptr;
    for (QLabel* l : tb->findChildren<QLabel*>())
        if (!l->text().contains(QStringLiteral("Rewind"))) return l;
    return nullptr;
}

QRgb ball_colour(QPushButton* b) {
    if (!b) return 0;
    return b->icon().pixmap(14, 14).toImage().pixel(7, 7);
}

// ── Answering every modal this window can open ────────────────────────
//
// Polled by a 1 ms timer while the GUI blocks in a nested event loop. Each
// modal is RECORDED before it is answered. Bounded: after kMaxModals answers
// it rejects whatever else appears, so a dialog that keeps re-opening (a file
// dialog refusing its answer) ends the row instead of the suite.
struct Modals {
    struct Seen { QString kind, title, text; };

    QString                    file_path;            // for a QFileDialog
    std::function<void()>      after_file_accept;    // e.g. make the file vanish
    int                        spin_value = -1;      // for a dialog with a QSpinBox
    QMessageBox::StandardButton box_answer = QMessageBox::Ok;
    std::vector<Seen>          seen;
    QTimer                     timer;
    static constexpr size_t    kMaxModals = 16;

    Modals() {
        QObject::connect(&timer, &QTimer::timeout, [this]() { poll(); });
        timer.start(1);
    }
    ~Modals() { timer.stop(); }

    void poll() {
        QWidget* w = QApplication::activeModalWidget();
        if (!w) return;
        auto* dlg = qobject_cast<QDialog*>(w);
        if (!dlg) return;
        if (seen.size() >= kMaxModals) { dlg->reject(); return; }
        if (auto* fd = qobject_cast<QFileDialog*>(w)) {
            seen.push_back({"file", fd->windowTitle(), {}});
            fd->selectFile(file_path);
            dlg->accept();          // virtual: QFileDialog::accept(), which validates
            if (after_file_accept) after_file_accept();
            return;
        }
        if (auto* mb = qobject_cast<QMessageBox*>(w)) {
            seen.push_back({"box", mb->windowTitle(), mb->text()});
            if (QAbstractButton* b = mb->button(box_answer)) b->click();
            else mb->done(box_answer);
            return;
        }
        seen.push_back({"dialog", dlg->windowTitle(), {}});
        if (auto* spin = dlg->findChild<QSpinBox*>())
            if (spin_value >= 0) spin->setValue(spin_value);
        dlg->accept();
    }

    int boxes() const {
        int n = 0;
        for (const auto& m : seen) n += m.kind == "box";
        return n;
    }
    const Seen* first(const char* kind) const {
        for (const auto& m : seen) if (m.kind == kind) return &m;
        return nullptr;
    }
    std::string describe() const {
        std::string out;
        for (const auto& m : seen)
            out += fmt("[%s '%s' '%s'] ", s(m.kind).c_str(), s(m.title).c_str(),
                       s(m.text).c_str());
        return out.empty() ? "(none)" : out;
    }
};

std::string write_file(const char* name, const char* contents) {
    const QString path = g_tmp->filePath(QString::fromLatin1(name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(contents);
    f.close();
    return path.toStdString();
}

} // namespace

static bool actions_paused_shape(DebuggerWindow* dbg) {
    return action_enabled(debug_item(dbg, "Run / Continue")) &&
           !action_enabled(debug_item(dbg, "Pause / Break")) &&
           action_enabled(debug_item(dbg, "Single Step")) &&
           action_enabled(debug_item(dbg, "Step Over")) &&
           action_enabled(debug_item(dbg, "Step Out"));
}

static bool actions_running_shape(DebuggerWindow* dbg) {
    return !action_enabled(debug_item(dbg, "Run / Continue")) &&
           action_enabled(debug_item(dbg, "Pause / Break")) &&
           !action_enabled(debug_item(dbg, "Single Step")) &&
           !action_enabled(debug_item(dbg, "Step Over")) &&
           !action_enabled(debug_item(dbg, "Step Out"));
}

static bool disasm_shows(DisasmPanel* dp, uint16_t addr) {
    if (!dp) return false;
    dp->select_all_visible();
    uint16_t lo = 0, hi = 0;
    const bool any = dp->selection_range(lo, hi);
    dp->clear_selection();
    return any && lo <= addr && addr <= hi;
}

// ===========================================================================
// QSO — Step Over through the GUI verb (debugger_manager.cpp:413-448).
//
// A call-like instruction (CALL nn, CALL cc,nn, RST n, DJNZ — disasm.h's
// is_call_like) is stepped OVER: the machine resumes and stops at the next
// instruction with the call's work done. Anything else degrades to Step Into
// and stops immediately. Asserted by where the machine STOPS.
// ===========================================================================
static void step_over_row(const char* id, const char* desc,
                          std::initializer_list<uint8_t> at_pc, uint16_t want_pc,
                          uint8_t f_in, uint8_t want_a, uint8_t b_in = 0,
                          uint8_t want_b = 0) {
    Fixture fx;
    if (!fx.ok) { check(id, desc, false, "fixture"); return; }
    fx.load(PROG, at_pc);
    fx.load(0x9000, {0x3C, 0xC9});                    // 9000 INC A / RET
    fx.regs(PROG, [&](Z80Registers& r) {
        r.AF = static_cast<uint16_t>(0x0000 | f_in);  // A = 0
        r.BC = static_cast<uint16_t>(b_in << 8);
    });
    fx.enable();

    fx.mgr->on_step_over();
    const bool resumed = !fx.paused() && actions_running_shape(fx.dbg());
    const uint16_t pc_now = fx.pc();
    fx.tick_until_paused();
    const Z80Registers r = fx.emu.cpu().get_registers();
    const uint8_t a = static_cast<uint8_t>(r.AF >> 8);
    const uint8_t b = static_cast<uint8_t>(r.BC >> 8);
    check(id, desc,
          resumed && fx.paused() && actions_paused_shape(fx.dbg()) && r.PC == want_pc &&
              a == want_a && b == want_b && r.SP == TEST_SP,
          fmt("resumed (running actions)=%d (PC then %04X) paused=%d paused actions=%d "
              "PC=%04X A=%02X B=%02X SP=%04X (want PC=%04X A=%02X B=%02X SP=%04X)",
              resumed, pc_now, fx.paused(), actions_paused_shape(fx.dbg()), r.PC, a, b,
              r.SP, want_pc, want_a, want_b, TEST_SP));
}

static void test_step_over() {
    set_group("QSO");

    //   8000  CD 00 90   CALL $9000       8003  18 FE   JR $
    step_over_row("QSO-01",
                  "CALL nn: Step Over resumes and stops at the next instruction "
                  "with the subroutine run (A incremented)",
                  {0xCD, 0x00, 0x90, 0x18, 0xFE}, 0x8003, 0x40 /*Z*/, 0x01);
    //   8000  CC 00 90   CALL Z,$9000  — Z set: taken
    step_over_row("QSO-02",
                  "CALL cc,nn TAKEN: stepped over, the subroutine ran",
                  {0xCC, 0x00, 0x90, 0x18, 0xFE}, 0x8003, 0x40 /*Z*/, 0x01);
    //   8000  C4 00 90   CALL NZ,$9000 — Z set: not taken
    step_over_row("QSO-03",
                  "CALL cc,nn NOT taken: stops at the next instruction, the "
                  "subroutine did not run",
                  {0xC4, 0x00, 0x90, 0x18, 0xFE}, 0x8003, 0x40 /*Z*/, 0x00);
    // QSO-04 — RST n. The +3's all-RAM special paging (port 0x1FFD = 0x01:
    // banks 0-3 in slots 0-7) puts RAM at $0000, so the restart routine is
    // this row's own code rather than whatever a ROM does.
    //   0028  3C  INC A / 0029  C9  RET        8000  EF  RST $28 / 8001  18 FE  JR $
    {
        Fixture fx(MachineType::ZX_PLUS3);
        const char* desc = "RST n: the restart routine runs and the machine stops "
                           "on the byte after the RST";
        if (!fx.ok) { check("QSO-04", desc, false, "fixture"); }
        else {
            fx.emu.port().write(0x1FFD, 0x01);
            fx.load(0x0028, {0x3C, 0xC9});
            fx.load(PROG, {0xEF, 0x18, 0xFE});
            const bool ram_at_0 = fx.emu.mmu().read(0x0028) == 0x3C;
            fx.regs(PROG, [](Z80Registers& r) { r.AF = 0x0000; });
            fx.enable();
            fx.mgr->on_step_over();
            const bool resumed = !fx.paused() && actions_running_shape(fx.dbg());
            fx.tick_until_paused();
            const Z80Registers r = fx.emu.cpu().get_registers();
            check("QSO-04", desc,
                  ram_at_0 && resumed && fx.paused() && actions_paused_shape(fx.dbg()) &&
                      r.PC == 0x8001 && (r.AF >> 8) == 1 && r.SP == TEST_SP,
                  fmt("RAM at $0028=%d resumed=%d paused=%d PC=%04X A=%02X SP=%04X",
                      ram_at_0, resumed, fx.paused(), r.PC, r.AF >> 8, r.SP));
        }
    }

    // QSO-05 — DJNZ with the loop taken: B = 3 at the DJNZ runs the loop to
    // completion (two more INC A) and stops after it.
    //   8000  3C       INC A       <- loop
    //   8001  10 FD    DJNZ $8000  <- PC
    //   8003  18 FE    JR $
    {
        Fixture fx;
        const char* desc = "DJNZ with the loop taken: stepped over, the loop "
                           "runs out (B=0, A=2) and the machine stops after it";
        if (!fx.ok) { check("QSO-05", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x3C, 0x10, 0xFD, 0x18, 0xFE});
            fx.regs(0x8001, [](Z80Registers& r) { r.AF = 0x0000; r.BC = 0x0300; });
            fx.enable();
            fx.mgr->on_step_over();
            const bool resumed = !fx.paused() && actions_running_shape(fx.dbg());
            fx.tick_until_paused();
            const Z80Registers r = fx.emu.cpu().get_registers();
            check("QSO-05", desc,
                  resumed && fx.paused() && actions_paused_shape(fx.dbg()) &&
                      r.PC == 0x8003 && (r.BC >> 8) == 0 && (r.AF >> 8) == 2,
                  fmt("resumed=%d PC=%04X B=%02X A=%02X", resumed, r.PC, r.BC >> 8,
                      r.AF >> 8));
        }
    }

    // QSO-06 — not call-like: degrades to Step Into, so it stops at once, on
    // the JP's target, without the machine ever resuming.
    {
        Fixture fx;
        const char* desc = "a non-call instruction (JP nn) degrades to Step Into: "
                           "paused at once, on the jump target";
        if (!fx.ok) { check("QSO-06", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0xC3, 0x00, 0x90});
            fx.load(0x9000, {0x18, 0xFE});
            fx.regs(PROG);
            fx.enable();
            fx.mgr->on_step_over();
            check("QSO-06", desc,
                  fx.paused() && fx.pc() == 0x9000 && actions_paused_shape(fx.dbg()) &&
                      cpu_value(fx.dbg(), "PC: ") == "9000",
                  fmt("paused=%d PC=%04X paused actions=%d shown PC=%s", fx.paused(), fx.pc(),
                      actions_paused_shape(fx.dbg()),
                      s(cpu_value(fx.dbg(), "PC: ")).c_str()));
        }
    }
}

// ===========================================================================
// QSI — Step Into through the GUI verb (debugger_manager.cpp:377-411), which
// is Emulator::debugger_step() (GH #207): it runs a HALT out, turns frames
// over, and consumes the data-breakpoint latch.
// ===========================================================================
static void test_step_into() {
    set_group("QSI");

    // QSI-01 — at a HALT with interrupts on, one Step leaves it: the CPU takes
    // the frame interrupt (IM 1 -> $0038) instead of spinning in place.
    {
        Fixture fx;
        const char* desc = "Step at a HALT runs the halt out: the CPU leaves it "
                           "on the frame interrupt (PC = $0038, not halted)";
        if (!fx.ok) { check("QSI-01", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x76, 0x00});                  // HALT / NOP
            fx.regs(PROG, [](Z80Registers& r) { r.IFF1 = 1; r.IFF2 = 1; r.IM = 1; });
            fx.enable();
            fx.mgr->on_step_into();                       // executes the HALT
            const bool halted = fx.emu.cpu().get_registers().halted;
            fx.mgr->on_step_into();                       // runs it out
            const Z80Registers r = fx.emu.cpu().get_registers();
            check("QSI-01", desc,
                  halted && fx.paused() && !r.halted && r.PC == 0x0038,
                  fmt("halted after 1st=%d; after 2nd: paused=%d halted=%d PC=%04X",
                      halted, fx.paused(), r.halted, r.PC));
        }
    }

    // QSI-02 — stepping across the end of a frame starts the next one: the
    // frame counter the rewind UI shows advances.
    {
        Fixture fx(MachineType::ZX48K, 0, /*paused=*/false);
        const char* desc = "Steps that cross the end of a frame turn it over "
                           "(frame_num + 1), with the loop still running";
        if (!fx.ok) { check("QSI-02", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x18, 0xFE});                  // JR $
            fx.regs(PROG);
            fx.enable();
            const auto& t = fx.emu.timing();
            fx.emu.debug_state().run_to_cycle(fx.emu.current_frame_cycle() +
                                              t.master_cycles_per_frame - 600);
            fx.emu.run_frame();                           // pauses 600 cycles short
            fx.mgr->check_breakpoint_hit();
            const uint32_t before = fx.emu.frame_num();
            int steps = 0;
            while (steps < 200 && fx.emu.frame_num() == before) {
                fx.mgr->on_step_into();
                ++steps;
            }
            check("QSI-02", desc,
                  fx.paused() && fx.emu.frame_num() == before + 1 && steps < 200 &&
                      (fx.pc() == PROG),
                  fmt("frame %u -> %u after %d steps, PC=%04X", before,
                      fx.emu.frame_num(), steps, fx.pc()));
        }
    }

    // QSI-03 — a watchpoint that fires INSIDE a step is consumed by it: the
    // next Run to End of Frame runs to its target instead of stopping one
    // instruction in on the stale latch.
    //   8000  3A 00 90   LD A,($9000)   <- READ watchpoint on $9000
    //   8003  00 x13     NOP
    //   8010  18 FE      JR $
    {
        Fixture fx;
        const char* desc = "a watchpoint hit inside a Step is consumed: the next "
                           "Run to EOF runs to the park instead of stopping at once";
        if (!fx.ok) { check("QSI-03", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x3A, 0x00, 0x90});
            for (uint16_t a = 0x8003; a < 0x8010; ++a) fx.load(a, {0x00});
            fx.load(0x8010, {0x18, 0xFE});
            fx.regs(PROG);
            fx.enable();
            fx.emu.debug_state().breakpoints().add_watchpoint(0x9000, WatchType::READ);
            fx.mgr->on_step_into();
            const uint16_t after_step = fx.pc();
            fx.mgr->on_run_to_eof();
            fx.tick_until_paused();
            check("QSI-03", desc,
                  after_step == 0x8003 && fx.paused() && fx.pc() == 0x8010,
                  fmt("after step PC=%04X; after Run to EOF paused=%d PC=%04X "
                      "(a leaked latch stops at 8004)",
                      after_step, fx.paused(), fx.pc()));
        }
    }

    // QSI-04 — the step's own UI: with no tick in between, the CPU panel and
    // the Disassembly already show the instruction the step landed on (the
    // Disassembly was parked far away), and the actions are the paused set.
    {
        Fixture fx;
        const char* desc = "after a Step, at once: the CPU panel shows the new PC "
                           "and registers and the Disassembly centres on it";
        if (!fx.ok) { check("QSI-04", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x21, 0x34, 0x12, 0xC3, 0x00, 0xA0});   // LD HL,$1234 / JP $A000
            fx.load(0xA000, {0x18, 0xFE});
            fx.regs(PROG);
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            if (auto* sb = dbg->disasm_panel()->findChild<QScrollBar*>()) sb->setValue(0xC000);
            fx.mgr->on_step_into();                       // LD HL,$1234
            fx.mgr->on_step_into();                       // JP $A000
            check("QSI-04", desc,
                  fx.pc() == 0xA000 && cpu_value(dbg, "PC: ") == "A000" &&
                      cpu_value(dbg, "HL: ") == "1234" &&
                      disasm_shows(dbg->disasm_panel(), 0xA000) &&
                      actions_paused_shape(dbg),
                  fmt("PC=%04X shown PC=%s HL=%s disasm=%d actions=%d", fx.pc(),
                      s(cpu_value(dbg, "PC: ")).c_str(), s(cpu_value(dbg, "HL: ")).c_str(),
                      disasm_shows(dbg->disasm_panel(), 0xA000), actions_paused_shape(dbg)));
        }
    }
}

// ===========================================================================
// QPE — the pause-edge UI sequence (debugger_manager.cpp:326-375, 682-720).
//
// CPU, Stack, Call Stack and Disassembly update only while paused: Run makes
// them stop following the machine, a breakpoint's pause edge (seen by
// check_breakpoint_hit on the next tick) refreshes them onto the stopped
// state and re-centres the disassembly on PC, and the raster read-out shows
// dashes while running and the position the machine stopped at after the
// edge's tick (refresh_panels() snapshots it first).
//
// The program is paused at a CALL that never returns, so every state the
// running machine can be in differs from the paused one in all four panels:
// SP (FF00 -> FEFE), the Call Stack (empty -> one CALL frame), HL (counting)
// and PC (8000 -> the loop at 9000).
//
//   8000  CD 00 90    CALL $9000
//   9000  23          INC HL            <- loop
//   9001  22 00 FF    LD ($FF00),HL     <- breakpoint for the pause edge
//   9004  18 FA       JR $9000
// ===========================================================================
static void load_pause_edge_program(Fixture& fx) {
    fx.load(PROG, {0xCD, 0x00, 0x90});
    fx.load(0x9000, {0x23, 0x22, 0x00, 0xFF, 0x18, 0xFA});
    fx.load(TEST_SP, {0x00, 0x00});
    fx.regs(PROG, [](Z80Registers& r) { r.HL = 0x0000; });
}

static void test_pause_edge() {
    set_group("QPE");

    Fixture fx;
    if (!fx.ok) {
        for (const char* id : {"QPE-01", "QPE-02", "QPE-03"})
            check(id, "fixture", false);
    } else {
        load_pause_edge_program(fx);
        fx.enable();
        DebuggerWindow* dbg = fx.dbg();

        // Park the disassembly far from the program while paused, on a page
        // of one-byte NOPs, and note the address span it lists.
        for (uint16_t a = 0xC000; a < 0xC100; ++a) fx.emu.mmu().write(a, 0x00);
        if (auto* sb = dbg->disasm_panel()->findChild<QScrollBar*>()) sb->setValue(0xC000);
        QApplication::processEvents();
        uint16_t span_lo0 = 0, span_hi0 = 0, span_lo1 = 1, span_hi1 = 1;
        dbg->disasm_panel()->select_all_visible();
        dbg->disasm_panel()->selection_range(span_lo0, span_hi0);
        dbg->disasm_panel()->clear_selection();
        const QString pc0 = cpu_value(dbg, "PC: ");
        const QString hl0 = cpu_value(dbg, "HL: ");
        const QString stk0 = table_cell(dbg->stack_panel(), 0, 0);

        fx.mgr->on_run();
        const bool running_actions = actions_running_shape(dbg);
        // Rewrite the parked page as three-byte instructions: a Disassembly
        // that re-read memory now would list three times the span.
        for (uint16_t a = 0xC000; a < 0xC0FF; a += 3)
            fx.load(a, {0x21, 0x00, 0x00});
        for (int i = 0; i < 13; ++i) fx.tick();          // > one throttled refresh
        dbg->disasm_panel()->select_all_visible();
        dbg->disasm_panel()->selection_range(span_lo1, span_hi1);
        dbg->disasm_panel()->clear_selection();
        const uint16_t live_hl = fx.emu.cpu().get_registers().HL;
        const bool frozen =
            fx.emu.cpu().get_registers().HL != 0 &&
            fx.emu.cpu().get_registers().SP == TEST_SP - 2 &&
            cpu_value(dbg, "PC: ") == pc0 && cpu_value(dbg, "HL: ") == hl0 &&
            table_cell(dbg->stack_panel(), 0, 0) == stk0 &&
            table_rows(dbg->callstack_panel()) == 0 &&
            span_lo0 == 0xC000 && span_lo1 == span_lo0 && span_hi1 == span_hi0 &&
            !disasm_shows(dbg->disasm_panel(), PROG);
        check("QPE-01",
              "Run: the actions flip to the running shape and CPU / Stack / Call "
              "Stack / Disassembly stop following the machine through refreshes",
              pc0 == "8000" && hl0 == "0000" && stk0 == "FF00" && running_actions && frozen,
              fmt("seed PC=%s HL=%s stk0=%s; running actions=%d; after 13 ticks "
                  "live HL=%04X shown PC=%s HL=%s stk0=%s cs rows=%d disasm@8000=%d "
                  "disasm span %04X..%04X -> %04X..%04X",
                  s(pc0).c_str(), s(hl0).c_str(), s(stk0).c_str(), running_actions,
                  live_hl, s(cpu_value(dbg, "PC: ")).c_str(),
                  s(cpu_value(dbg, "HL: ")).c_str(),
                  s(table_cell(dbg->stack_panel(), 0, 0)).c_str(),
                  table_rows(dbg->callstack_panel()),
                  disasm_shows(dbg->disasm_panel(), PROG), span_lo0, span_hi0, span_lo1,
                  span_hi1));

        fx.emu.debug_state().breakpoints().add_pc(0x9001);
        fx.tick_until_paused();
        const Z80Registers r = fx.emu.cpu().get_registers();
        const uint16_t word0 = static_cast<uint16_t>(fx.emu.mmu().read(r.SP) |
                                                     (fx.emu.mmu().read(r.SP + 1) << 8));
        const bool updated =
            r.PC == 0x9001 && cpu_value(dbg, "PC: ") == "9001" &&
            cpu_value(dbg, "HL: ") == QString::asprintf("%04X", r.HL) &&
            table_cell(dbg->stack_panel(), 0, 0) == QString::asprintf("%04X", r.SP) &&
            table_cell(dbg->stack_panel(), 0, 1) ==
                QString::asprintf("%04X (%5d)", word0, word0) &&
            table_rows(dbg->callstack_panel()) == 1 &&
            table_cell(dbg->callstack_panel(), 0, 1) == "CALL" &&
            table_cell(dbg->callstack_panel(), 0, 2) == "8000" &&
            table_cell(dbg->callstack_panel(), 0, 3) == "9000";
        check("QPE-02",
              "a breakpoint's pause edge refreshes CPU, Stack and Call Stack onto "
              "the stopped state and flips the actions to the paused shape",
              updated && actions_paused_shape(dbg),
              fmt("PC=%04X shown PC=%s HL=%s(live %04X) stk0=%s|%s cs rows=%d %s %s %s "
                  "paused actions=%d",
                  r.PC, s(cpu_value(dbg, "PC: ")).c_str(),
                  s(cpu_value(dbg, "HL: ")).c_str(), r.HL,
                  s(table_cell(dbg->stack_panel(), 0, 0)).c_str(),
                  s(table_cell(dbg->stack_panel(), 0, 1)).c_str(),
                  table_rows(dbg->callstack_panel()),
                  s(table_cell(dbg->callstack_panel(), 0, 1)).c_str(),
                  s(table_cell(dbg->callstack_panel(), 0, 2)).c_str(),
                  s(table_cell(dbg->callstack_panel(), 0, 3)).c_str(),
                  actions_paused_shape(dbg)));

        check("QPE-03",
              "the pause edge re-centres the Disassembly on the new PC (it was "
              "parked at $C000 and did not follow while running)",
              disasm_shows(dbg->disasm_panel(), 0x9001),
              fmt("disasm shows 9001=%d", disasm_shows(dbg->disasm_panel(), 0x9001)));
    }

    // QPE-04 — the raster read-out: dashes while running, and after a pause
    // edge's tick the line the machine stopped on (Run to EOF: the last
    // visible raw line, FB_HEIGHT-1 + vblank_top), not the previous snapshot.
    {
        Fixture f2;
        const char* desc = "raster read-out: dashes while running; after the pause "
                           "edge's tick, the raw line the machine stopped on";
        if (!f2.ok) { check("QPE-04", desc, false, "fixture"); }
        else {
            f2.load(PROG, {0x18, 0xFE});
            f2.regs(PROG);
            f2.enable();
            f2.tick();                                    // paused: snapshot at vc 0
            auto* vp = f2.dbg()->findChild<VideoPanel*>();
            QLabel* raw = vp ? vp->findChild<QLabel*>(QStringLiteral("rasterRaw")) : nullptr;
            const QString at_start = raw ? raw->text() : QString();

            f2.mgr->on_run();
            for (int i = 0; i < 13; ++i) f2.tick();
            const QString running = raw ? raw->text() : QString();

            f2.mgr->on_pause();
            f2.mgr->on_run_to_eof();
            f2.tick_until_paused();
            const QString stopped = raw ? raw->text() : QString();
            const int want_vc = Renderer::FB_HEIGHT - 1 + f2.emu.video_timing().vblank_top();
            const QString want = QString::asprintf("vc:%4d", want_vc);
            check("QPE-04", desc,
                  raw && !at_start.contains(want) && running.contains("----") &&
                      f2.paused() && stopped.contains(want) && !stopped.contains("----"),
                  fmt("start='%s' running='%s' stopped='%s' want '%s'",
                      s(at_start).c_str(), s(running).c_str(), s(stopped).c_str(),
                      s(want).c_str()));
        }
    }

    // QPE-05 — Break (on_pause) is its own pause edge: without waiting for a
    // tick the panels show the machine where it stopped, the Disassembly
    // centres on PC, and the actions flip.
    {
        Fixture f3;
        const char* desc = "Break: at once, CPU / Stack show where the machine "
                           "stopped, the Disassembly centres on PC, actions flip";
        if (!f3.ok) { check("QPE-05", desc, false, "fixture"); }
        else {
            load_pause_edge_program(f3);
            f3.enable();
            if (auto* sb = f3.dbg()->disasm_panel()->findChild<QScrollBar*>())
                sb->setValue(0xC000);
            f3.mgr->on_run();
            for (int i = 0; i < 3; ++i) f3.tick();
            f3.mgr->on_pause();
            const Z80Registers r = f3.emu.cpu().get_registers();
            DebuggerWindow* dbg = f3.dbg();
            check("QPE-05", desc,
                  f3.paused() && cpu_value(dbg, "PC: ") == QString::asprintf("%04X", r.PC) &&
                      cpu_value(dbg, "HL: ") == QString::asprintf("%04X", r.HL) &&
                      r.HL != 0 &&
                      table_cell(dbg->stack_panel(), 0, 0) == QString::asprintf("%04X", r.SP) &&
                      disasm_shows(dbg->disasm_panel(), r.PC) && actions_paused_shape(dbg),
                  fmt("PC=%04X shown=%s HL=%04X shown=%s stk0=%s disasm=%d actions=%d", r.PC,
                      s(cpu_value(dbg, "PC: ")).c_str(), r.HL,
                      s(cpu_value(dbg, "HL: ")).c_str(),
                      s(table_cell(dbg->stack_panel(), 0, 0)).c_str(),
                      disasm_shows(dbg->disasm_panel(), r.PC), actions_paused_shape(dbg)));
        }
    }

    // QPE-06 — every OTHER verb that resumes the machine leaves the paused-only
    // panels in running mode too, exactly as Run does: Step Over on a call,
    // Step Out, Run to Here (the disassembly's request), Run to End of Frame
    // and Run to End of Scan Line. Observed without running a frame: after
    // the verb the registers and the top stack word are changed by hand, and
    // a throttled refresh (12 manager ticks) must leave the CPU and Stack
    // panels showing the values from before the verb.
    {
        struct Verb {
            const char* name;
            std::function<void(Fixture&)> go;
        };
        const Verb verbs[] = {
            {"Step Over (CALL)", [](Fixture& f) { f.mgr->on_step_over(); }},
            {"Step Out",         [](Fixture& f) { f.mgr->on_step_out(); }},
            {"Run to Here",      [](Fixture& f) { f.dbg()->disasm_panel()->run_to_selected(); }},
            {"Run to EOF",       [](Fixture& f) { f.mgr->on_run_to_eof(); }},
            {"Run to EOSL",      [](Fixture& f) { f.mgr->on_run_to_eosl(); }},
        };
        std::string bad;
        for (const Verb& v : verbs) {
            Fixture f;
            if (!f.ok) { bad += fmt("%s: fixture ", v.name); continue; }
            f.load(PROG, {0xCD, 0x00, 0x90, 0x18, 0xFE});        // CALL $9000 / JR $
            f.load(0x9000, {0x18, 0xFE});
            f.load(TEST_SP, {0x11, 0x22});
            f.regs(PROG, [](Z80Registers& r) { r.HL = 0x1111; });
            f.enable();
            DebuggerWindow* dbg = f.dbg();
            v.go(f);
            const bool resumed = !f.paused() && actions_running_shape(dbg);
            Z80Registers r = f.emu.cpu().get_registers();
            r.HL = 0x2222;
            f.emu.cpu().set_registers(r);
            f.emu.mmu().write(TEST_SP, 0x99);
            for (int i = 0; i < 12; ++i) f.mgr->refresh_panels();
            const QString hl = cpu_value(dbg, "HL: ");
            const QString w0 = table_cell(dbg->stack_panel(), 0, 1);
            const QString want_w0 = QString::asprintf("%04X (%5d)", 0x2211, 0x2211);
            if (!resumed || hl != "1111" || w0 != want_w0)
                bad += fmt("%s: resumed=%d HL shown %s stack0 %s; ", v.name, resumed,
                           s(hl).c_str(), s(w0).c_str());
        }
        check("QPE-06",
              "Step Over on a call, Step Out, Run to Here, Run to EOF and Run to "
              "EOSL all put the panels in running mode, like Run: frozen through "
              "a throttled refresh, actions flipped",
              bad.empty(), bad);
    }
}

// ===========================================================================
// QTH — the refresh throttle (debugger_manager.cpp:665-680): while running
// the window refreshes on every 12th tick, while paused on every tick. Counted
// through a register the NextREG panel displays (NR 0x7F, the user register).
// ===========================================================================
static void test_throttle() {
    set_group("QTH");

    Fixture fx(MachineType::ZXN_ISSUE2, 0, /*paused=*/false);
    if (!fx.ok) {
        check("QTH-01", "fixture", false);
        check("QTH-02", "fixture", false);
        return;
    }
    fx.enable();
    NextRegPanel* nr = fx.dbg()->findChild<NextRegPanel*>();
    auto shown = [&]() { return table_cell(nr, 0x7F, 2); };

    // Sync to the throttle's phase: tick until a refresh shows 11.
    fx.emu.nextreg().write(0x7F, 0x11);
    int sync = 0;
    while (sync < 13 && shown() != "11") { fx.mgr->refresh_panels(); ++sync; }

    fx.emu.nextreg().write(0x7F, 0x22);
    std::string seq;
    int first_change = -1;
    for (int i = 1; i <= 12; ++i) {
        fx.mgr->refresh_panels();
        seq += s(shown()) + " ";
        if (first_change < 0 && shown() == "22") first_change = i;
    }
    check("QTH-01",
          "while running, the panels refresh on the 12th manager tick and not "
          "on the 11 before it",
          nr && sync <= 12 && first_change == 12,
          fmt("sync after %d; values over 12 ticks: %s", sync, seq.c_str()));

    fx.mgr->on_pause();
    fx.emu.nextreg().write(0x7F, 0x33);
    fx.mgr->refresh_panels();
    const QString once = shown();
    fx.emu.nextreg().write(0x7F, 0x44);
    fx.mgr->refresh_panels();
    check("QTH-02",
          "while paused, every manager tick refreshes the panels",
          once == "33" && shown() == "44",
          fmt("after 1st tick %s, 2nd %s", s(once).c_str(), s(shown()).c_str()));
}

// ===========================================================================
// QEN — set_enabled()'s seeds (debugger_manager.cpp:84-165): enabling hands
// the four paused-only panels the CURRENT pause state and refreshes them at
// once; re-enabling a reused window re-seeds it.
// ===========================================================================
static void test_enable_seeds() {
    set_group("QEN");

    {
        Fixture fx;   // paused before the debugger is enabled
        const char* desc = "enabling on a paused machine shows its state at once "
                           "(CPU, Stack, Call Stack) — no tick needed";
        if (!fx.ok) { check("QEN-01", desc, false, "fixture"); }
        else {
            // A real CALL with call tracking on: PC 7000 -> 8123, the return
            // address $7003 pushed at $FF00, one Call Stack frame.
            fx.emu.call_stack().set_enabled(true);
            fx.load(0x7000, {0xCD, 0x23, 0x81});           // CALL $8123
            fx.regs(0x7000, [](Z80Registers& r) { r.SP = TEST_SP + 2; r.HL = 0xBEEF; });
            fx.emu.execute_single_instruction();
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            const bool ok =
                cpu_value(dbg, "PC: ") == "8123" && cpu_value(dbg, "HL: ") == "BEEF" &&
                table_cell(dbg->stack_panel(), 0, 0) == "FF00" &&
                table_cell(dbg->stack_panel(), 0, 1) == QString::asprintf("%04X (%5d)", 0x7003, 0x7003) &&
                table_rows(dbg->callstack_panel()) == 1 &&
                table_cell(dbg->callstack_panel(), 0, 3) == "8123";
            check("QEN-01", desc, ok,
                  fmt("PC=%s HL=%s stk0=%s|%s cs rows=%d target=%s",
                      s(cpu_value(dbg, "PC: ")).c_str(), s(cpu_value(dbg, "HL: ")).c_str(),
                      s(table_cell(dbg->stack_panel(), 0, 0)).c_str(),
                      s(table_cell(dbg->stack_panel(), 0, 1)).c_str(),
                      table_rows(dbg->callstack_panel()),
                      s(table_cell(dbg->callstack_panel(), 0, 3)).c_str()));
        }
    }

    {
        Fixture fx;   // paused: the first session leaves the panels paused
        const char* desc = "re-enabling a reused window on a RUNNING machine "
                           "re-seeds the panels as running: they stop following it";
        if (!fx.ok) { check("QEN-02", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x23, 0x18, 0xFD});            // INC HL / JR $8000
            fx.regs(PROG, [](Z80Registers& r) { r.HL = 0; });
            fx.enable();                                   // panels seeded PAUSED
            fx.mgr->set_enabled(false);                    // auto-resumes; window kept
            fx.emu.run_frame();
            fx.mgr->set_enabled(true);                     // re-enable while running
            const QString hl_at_enable = cpu_value(fx.dbg(), "HL: ");
            for (int i = 0; i < 13; ++i) fx.tick();       // frames + a refresh
            const uint16_t live = fx.emu.cpu().get_registers().HL;
            check("QEN-02", desc,
                  fx.dbg() && !fx.paused() && cpu_value(fx.dbg(), "HL: ") == hl_at_enable &&
                      QString::asprintf("%04X", live) != hl_at_enable,
                  fmt("HL shown at enable=%s, after 13 ticks=%s, live=%04X",
                      s(hl_at_enable).c_str(), s(cpu_value(fx.dbg(), "HL: ")).c_str(),
                      live));
        }
    }
}

// ===========================================================================
// QRW — the rewind UI (debugger_window.cpp:255-334, 509-520, 580-617,
// 699-721, 774-890) and the Step Back / Frame Back verbs
// (debugger_manager.cpp:562-622), against a REAL rewind buffer.
//
// The program is a counter loop, so every frame's snapshot holds a different
// machine: 8000 INC HL / 8001 JR $8000.
// ===========================================================================
static void load_counter(Fixture& fx) {
    fx.load(PROG, {0x23, 0x18, 0xFD});
    fx.regs(PROG, [](Z80Registers& r) { r.HL = 0; });
}

/// Run `n` frames through the tick loop, then Break.
static void run_frames_then_break(Fixture& fx, int n) {
    fx.mgr->on_run();
    for (int i = 0; i < n; ++i) fx.tick();
    fx.mgr->on_pause();
    fx.tick();
}

static void test_rewind_ui() {
    set_group("QRW");

    // ── Toolbar, slider, labels, status ──────────────────────────────
    //
    // ONE FRAME NUMBERING (GH #278 WP0 fix): the snapshot tags the slider and
    // rewind_to_frame() speak, where snapshot t is the machine at the START of
    // frame t. The label, the "Rewound" status and Frame Back name the frame the
    // machine is in — the one running, the one just run at an ordinary frame
    // boundary, or the one a rewind restored — in that same numbering. (They
    // used to read Emulator::frame_num(), which counts frames BEGUN and runs one
    // ahead; and a restored frame used to be counted twice when it ran again.)
    // The machine each control restores is asserted by the HL a counter loop
    // held at the start of each tagged frame.
    {
        Fixture fx(MachineType::ZX48K, 10);
        if (!fx.ok || !fx.emu.rewind_buffer()) {
            for (const char* id : {"QRW-01", "QRW-02", "QRW-03", "QRW-04", "QRW-05"})
                check(id, "fixture: 48K with a 10-frame rewind buffer", false);
        } else {
            load_counter(fx);
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            QToolBar* tb = toolbar_titled(dbg, "Rewind");
            RewindBuffer* rb = fx.emu.rewind_buffer();

            fx.mgr->on_run();
            fx.tick();                                     // one snapshot
            // A running refresh happens only every 12th tick: ask the window,
            // so the toolbar is DECIDED at depth 1 rather than left at its
            // construction state.
            dbg->refresh_panels();
            const size_t d1 = rb->depth();
            const bool hidden_at_one = tb && tb->isHidden();
            for (int i = 0; i < 2; ++i) fx.tick();
            // A running refresh happens only every 12th tick: ask the window.
            dbg->refresh_panels();
            const size_t d3 = rb->depth();
            check("QRW-01",
                  "the rewind toolbar is hidden until the buffer holds more than "
                  "one snapshot, then shown",
                  tb && d1 == 1 && hidden_at_one && d3 > 1 && !tb->isHidden(),
                  fmt("depth %zu hidden=%d; depth %zu hidden=%d", d1, hidden_at_one, d3,
                      tb ? tb->isHidden() : -1));

            for (int i = 0; i < 12; ++i) fx.tick();       // wrap the 10-frame ring
            dbg->refresh_panels();
            auto* slider = dbg->findChild<QSlider*>();
            QLabel* fl = rewind_frame_label(dbg);
            const bool range_ok = slider && rb->depth() == 10 &&
                                  slider->minimum() == static_cast<int>(rb->oldest_frame_num()) &&
                                  slider->maximum() == static_cast<int>(rb->newest_frame_num());
            const QString label = fl ? fl->text() : QString();
            const bool label_ok = label == QStringLiteral("Frame %1 / %1")
                                               .arg(rb->newest_frame_num());
            check("QRW-02",
                  "the slider spans the snapshot tags [oldest, newest] and at the "
                  "live end the label reads \"Frame <newest> / <newest>\"",
                  range_ok && label_ok,
                  fmt("slider %d..%d buffer %u..%u label '%s'",
                      slider ? slider->minimum() : -1, slider ? slider->maximum() : -1,
                      rb->oldest_frame_num(), rb->newest_frame_num(), s(label).c_str()));

            check("QRW-03",
                  "while running the slider thumb sits at the live end (the newest "
                  "snapshot)",
                  slider && slider->value() == static_cast<int>(rb->newest_frame_num()),
                  fmt("thumb %d newest %u", slider ? slider->value() : -1,
                      rb->newest_frame_num()));

            fx.mgr->on_pause();
            if (slider) slider->setValue(static_cast<int>(rb->oldest_frame_num()) + 2);
            fx.tick();
            fx.tick();
            check("QRW-04",
                  "while paused the thumb stays where the user put it through "
                  "refreshes",
                  slider && slider->value() == static_cast<int>(rb->oldest_frame_num()) + 2,
                  fmt("thumb %d want %u", slider ? slider->value() : -1,
                      rb->oldest_frame_num() + 2));

            const size_t mb = (rb->depth() * rb->snapshot_bytes() + 524288) / 1048576;
            const QString want = QStringLiteral("⏮ Rewind: %1 frames / %2 MB")
                                     .arg(rb->depth()).arg(mb);
            check("QRW-05",
                  "the status bar reads \"Rewind: N frames / M MB\" at the live end",
                  status_of(dbg) == want,
                  fmt("status '%s' want '%s'", s(status_of(dbg)).c_str(), s(want).c_str()));
        }
    }

    // ── Rewinding: Frame Back, the slider, Jump Here, the status ──────
    //
    // hl_at[t] is HL at the start of the frame tagged t — what its snapshot
    // holds — recorded as each frame begins.
    auto run_recording = [](Fixture& fx, int n, std::map<uint32_t, uint16_t>& hl_at) {
        fx.mgr->on_run();
        for (int i = 0; i < n; ++i) {
            hl_at[fx.emu.frame_num()] = fx.emu.cpu().get_registers().HL;
            fx.tick();
        }
        fx.mgr->on_pause();
        fx.tick();
    };

    // Frame Back from the live end (paused at the boundary after the newest
    // frame K), pressed three times: K, K-1, K-2 — one frame further back each
    // press, the label and status naming the frame restored. It used to
    // restore K on every press.
    auto frame_back_row = [&](const char* id, const char* desc, bool via_menu) {
        Fixture fx(MachineType::ZX48K, 10);
        if (!fx.ok || !fx.emu.rewind_buffer()) { check(id, desc, false, "fixture"); return; }
        load_counter(fx);
        fx.enable();
        DebuggerWindow* dbg = fx.dbg();
        std::map<uint32_t, uint16_t> hl_at;
        run_recording(fx, 6, hl_at);
        RewindBuffer* rb = fx.emu.rewind_buffer();
        const uint32_t newest = rb->newest_frame_num();
        const uint16_t hl_end = fx.emu.cpu().get_registers().HL;
        QLabel* fl = rewind_frame_label(dbg);
        std::string bad;
        for (int press = 0; press < 3; ++press) {
            const uint32_t want = newest - static_cast<uint32_t>(press);
            if (via_menu) {
                if (QAction* a = debug_item(dbg, "|< Frame Back")) a->trigger();
            } else if (QPushButton* b = button_ending(dbg, "Frame Back")) {
                b->click();
            }
            const Z80Registers r = fx.emu.cpu().get_registers();
            const QString label = fl ? fl->text() : QString();
            const QString status = status_of(dbg);
            const bool ok =
                hl_at.count(want) && r.HL == hl_at[want] && r.HL != hl_end && fx.paused() &&
                cpu_value(dbg, "HL: ") == QString::asprintf("%04X", r.HL) &&
                label == QStringLiteral("Frame %1 / %2").arg(want).arg(newest) &&
                status == QStringLiteral("⏮ Rewound: frame %1 of %2  (F5 / Continue to resume)")
                              .arg(want).arg(newest);
            if (!ok)
                bad += fmt("press %d: HL %04X (want %04X) label '%s' status '%s'; ", press + 1,
                           r.HL, hl_at.count(want) ? hl_at[want] : 0, s(label).c_str(),
                           s(status).c_str());
        }
        check(id, desc, bad.empty(), bad);
    };
    frame_back_row("QRW-07",
                   "Frame Back (button) from the live end restores frame K, K-1, K-2 on "
                   "three presses, paused, CPU panel, label and status on the frame",
                   /*via_menu=*/false);
    frame_back_row("QRW-07b",
                   "Debug > Frame Back does the same as the button",
                   /*via_menu=*/true);

    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* d09 = "releasing the slider restores the snapshot it names, and "
                          "Jump Here does the same";
        const char* d06 = "after a jump to frame t the status reads \"Rewound: frame t of "
                          "<newest>  (<Run key> / Continue to resume)\" and the label "
                          "\"Frame t / <newest>\"";
        if (!fx.ok || !fx.emu.rewind_buffer()) {
            check("QRW-09", d09, false, "fixture");
            check("QRW-06", d06, false, "fixture");
        } else {
            load_counter(fx);
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            std::map<uint32_t, uint16_t> hl_at;
            run_recording(fx, 8, hl_at);
            RewindBuffer* rb = fx.emu.rewind_buffer();
            auto* slider = dbg->findChild<QSlider*>();
            const uint32_t t1 = rb->oldest_frame_num() + 1;
            const uint32_t t2 = rb->oldest_frame_num() + 3;
            uint16_t hl1 = 0, hl2 = 0;
            if (slider) {
                slider->setValue(static_cast<int>(t1));
                emit slider->sliderPressed();
                emit slider->sliderReleased();
                hl1 = fx.emu.cpu().get_registers().HL;
                slider->setValue(static_cast<int>(t2));
            }
            if (QPushButton* jh = button_ending(dbg, "Jump Here")) jh->click();
            hl2 = fx.emu.cpu().get_registers().HL;
            check("QRW-09", d09,
                  slider && hl1 == hl_at[t1] && hl2 == hl_at[t2] && hl1 != hl2 && fx.paused(),
                  fmt("release to tag %u: HL %04X (want %04X); Jump Here to %u: HL %04X "
                      "(want %04X)", t1, hl1, hl_at[t1], t2, hl2, hl_at[t2]));

            const QString st = status_of(dbg);
            QLabel* fl = rewind_frame_label(dbg);
            const QString label = fl ? fl->text() : QString();
            check("QRW-06", d06,
                  st == QStringLiteral("⏮ Rewound: frame %1 of %2  (F5 / Continue to resume)")
                            .arg(t2).arg(rb->newest_frame_num()) &&
                      label == QStringLiteral("Frame %1 / %2").arg(t2).arg(rb->newest_frame_num()),
                  fmt("status '%s' label '%s' (jumped to %u, newest %u)", s(st).c_str(),
                      s(label).c_str(), t2, rb->newest_frame_num()));
        }
    }

    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "Step Back (button, then menu) undoes one instruction "
                           "each time: PC back to the previous one, paused, shown";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRW-08", desc, false, "fixture"); }
        else {
            // 8000 INC HL / 8001 INC DE / 8002 JR $8000: three distinct PCs.
            fx.load(PROG, {0x23, 0x13, 0x18, 0xFC});
            fx.regs(PROG);
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            run_frames_then_break(fx, 3);
            fx.mgr->on_step_into();
            fx.mgr->on_step_into();
            fx.mgr->on_step_into();
            // Three steps from anywhere in the loop: the last three PCs are
            // the loop's three instructions in order, ending where we are.
            const uint16_t here = fx.pc();
            auto prev = [](uint16_t pc) -> uint16_t {
                return pc == 0x8000 ? 0x8002 : (pc == 0x8001 ? 0x8000 : 0x8001);
            };
            QPushButton* sb = button_ending(dbg, "Step Back");
            if (sb) sb->click();
            const uint16_t back1 = fx.pc();
            const QString shown1 = cpu_value(dbg, "PC: ");
            if (QAction* a = debug_item(dbg, "Step Back")) a->trigger();
            const uint16_t back2 = fx.pc();
            check("QRW-08", desc,
                  sb && back1 == prev(here) && back2 == prev(back1) && fx.paused() &&
                      shown1 == QString::asprintf("%04X", back1),
                  fmt("here %04X -> %04X (want %04X) -> %04X (want %04X) shown %s", here,
                      back1, prev(here), back2, prev(back1), s(shown1).c_str()));
        }
    }

    // QRW-19 — run forward again from a rewind: the frame restored runs under
    // its own number, and the history the rewind left is gone from the ring.
    // The slider then ends at that frame, the label reads it at both ends, and
    // the status is back at the live end. Before the fix the restored frame was
    // counted a second time as it ran (snapshotted as t+1) and the abandoned
    // snapshots stayed, one tag held twice.
    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "after Jump Here to frame t and one frame of Run, the slider "
                           "ends at t, the label reads \"Frame t / t\" and the status "
                           "is the live end's";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRW-19", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            run_frames_then_break(fx, 8);
            RewindBuffer* rb = fx.emu.rewind_buffer();
            auto* slider = dbg->findChild<QSlider*>();
            const uint32_t target = rb->oldest_frame_num() + 2;
            if (slider) slider->setValue(static_cast<int>(target));
            if (QPushButton* jh = button_ending(dbg, "Jump Here")) jh->click();
            fx.mgr->on_run();
            fx.tick();                                   // frame `target` runs again
            fx.mgr->on_pause();
            fx.tick();
            QLabel* fl = rewind_frame_label(dbg);
            const QString label = fl ? fl->text() : QString();
            const QString st = status_of(dbg);
            check("QRW-19", desc,
                  slider && slider->maximum() == static_cast<int>(target) &&
                      rb->newest_frame_num() == target &&
                      rb->depth() == target - rb->oldest_frame_num() + 1 &&
                      label == QStringLiteral("Frame %1 / %1").arg(target) &&
                      st.startsWith(QStringLiteral("⏮ Rewind: ")),
                  fmt("target %u: slider max %d newest %u depth %zu label '%s' status '%s'",
                      target, slider ? slider->maximum() : -1, rb->newest_frame_num(),
                      rb->depth(), s(label).c_str(), s(st).c_str()));
        }
    }

    // ── Greying ───────────────────────────────────────────────────────
    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* d10 = "with the trace off, Step Back greys and Jump Here stays "
                          "enabled; trace back on re-enables Step Back";
        const char* d11 = "an RZX playback greys both Step Back and Jump Here, and "
                          "running greys both";
        if (!fx.ok || !fx.emu.rewind_buffer()) {
            check("QRW-10", d10, false, "fixture");
            check("QRW-11", d11, false, "fixture");
        } else {
            load_counter(fx);
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            run_frames_then_break(fx, 3);
            QAction* step_back = debug_item(dbg, "Step Back");
            QPushButton* jump = button_ending(dbg, "Jump Here");
            const bool base = action_enabled(step_back) && jump && jump->isEnabled();
            QAction* trace = debug_sub_item(dbg, "Trace", "Enable Trace");
            if (trace) trace->trigger();                  // off
            const bool off = !action_enabled(step_back) && jump && jump->isEnabled();
            if (trace) trace->trigger();                  // on again
            const bool on_again = action_enabled(step_back);
            check("QRW-10", d10, base && off && on_again,
                  fmt("baseline=%d trace-off=%d trace-on=%d", base, off, on_again));

            // update_actions() runs on the manager's verbs, not on a refresh,
            // so the greying is observed after one (Break, a no-op pause).
            fx.emu.rzx_player().start(RzxRecording{});
            fx.mgr->on_pause();
            const bool rzx_grey = !action_enabled(step_back) && jump && !jump->isEnabled();
            fx.emu.rzx_player().stop();
            fx.mgr->on_pause();
            const bool restored = action_enabled(step_back) && jump->isEnabled();
            fx.mgr->on_run();
            const bool run_grey = !action_enabled(step_back) && !jump->isEnabled();
            check("QRW-11", d11, rzx_grey && restored && run_grey,
                  fmt("rzx grey=%d after stop=%d running grey=%d", rzx_grey, restored,
                      run_grey));
        }
    }

    // ── Enable Rewind / Buffer Size ──────────────────────────────────
    {
        Fixture fx(MachineType::ZX48K, 0);
        const char* d12 = "Enable Rewind with no buffer creates one (500 frames), "
                          "turns the trace on and snapshots start";
        const char* d16 = "Rewind Buffer Size... resizes to the value entered (the "
                          "next create uses it) and 0 frees the buffer";
        if (!fx.ok) {
            check("QRW-12", d12, false, "fixture");
            check("QRW-16", d16, false, "fixture");
        } else {
            load_counter(fx);
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            QAction* enable = debug_sub_item(dbg, "Rewind", "Enable Rewind");
            const bool none_before = fx.emu.rewind_buffer() == nullptr &&
                                     enable && !enable->isChecked();
            fx.emu.trace_log().set_enabled(false);
            if (enable) enable->trigger();
            RewindBuffer* rb = fx.emu.rewind_buffer();
            const bool created = rb && rb->capacity() == 500 && fx.emu.rewind_enabled() &&
                                 fx.emu.trace_log().enabled() && enable->isChecked();
            run_frames_then_break(fx, 2);
            check("QRW-12", d12, none_before && created && rb && rb->depth() >= 2,
                  fmt("before none=%d; created=%d cap=%zu depth=%zu", none_before, created,
                      rb ? rb->capacity() : 0, rb ? rb->depth() : 0));

            Modals m;
            m.spin_value = 7;
            if (QAction* sz = debug_sub_item(dbg, "Rewind", "Rewind Buffer Size..."))
                sz->trigger();
            const bool resized = fx.emu.rewind_buffer() &&
                                 fx.emu.rewind_buffer()->capacity() == 7 &&
                                 fx.emu.rewind_buffer()->depth() == 0;
            m.spin_value = 0;
            if (QAction* sz = debug_sub_item(dbg, "Rewind", "Rewind Buffer Size..."))
                sz->trigger();
            const bool freed = fx.emu.rewind_buffer() == nullptr && enable &&
                               !enable->isChecked();
            if (enable) {
                if (enable->isChecked()) enable->trigger();   // uncheck first
                enable->trigger();                             // create again
            }
            const bool remembered = fx.emu.rewind_buffer() &&
                                    fx.emu.rewind_buffer()->capacity() == 7;
            check("QRW-16", d16, m.seen.size() == 2 && resized && freed && remembered,
                  fmt("dialogs=%zu resized=%d freed=%d next create cap=%zu", m.seen.size(),
                      resized, freed,
                      fx.emu.rewind_buffer() ? fx.emu.rewind_buffer()->capacity() : 0));
        }
    }

    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* d17 = "Enable Rewind off keeps the buffer and its history but "
                          "stops snapshots (keep-but-pause)";
        const char* d18 = "Enable Rewind on again resumes snapshots on the SAME "
                          "buffer and re-enables a trace switched off meanwhile";
        if (!fx.ok || !fx.emu.rewind_buffer()) {
            check("QRW-17", d17, false, "fixture");
            check("QRW-18", d18, false, "fixture");
        } else {
            load_counter(fx);
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            run_frames_then_break(fx, 3);
            RewindBuffer* rb = fx.emu.rewind_buffer();
            const size_t depth0 = rb->depth();
            QAction* enable = debug_sub_item(dbg, "Rewind", "Enable Rewind");
            const bool checked0 = enable && enable->isChecked();
            if (enable) enable->trigger();                // off
            run_frames_then_break(fx, 3);
            check("QRW-17", d17,
                  checked0 && enable && !enable->isChecked() && fx.emu.rewind_buffer() == rb &&
                      !fx.emu.rewind_enabled() && rb->depth() == depth0,
                  fmt("checked %d->%d same buffer=%d enabled=%d depth %zu->%zu", checked0,
                      enable ? enable->isChecked() : -1, fx.emu.rewind_buffer() == rb,
                      fx.emu.rewind_enabled(), depth0, rb->depth()));

            fx.emu.trace_log().set_enabled(false);
            if (enable) enable->trigger();                // on again
            const bool resumed = fx.emu.rewind_buffer() == rb && fx.emu.rewind_enabled() &&
                                 fx.emu.trace_log().enabled() && enable->isChecked();
            run_frames_then_break(fx, 2);
            check("QRW-18", d18, resumed && rb->depth() > depth0,
                  fmt("same buffer=%d enabled=%d trace=%d depth %zu->%zu",
                      fx.emu.rewind_buffer() == rb, fx.emu.rewind_enabled(),
                      fx.emu.trace_log().enabled(), depth0, rb->depth()));
        }
    }

    // ── Failure classes (Task 60e): refused, benign, corrupt ─────────
    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "while an RZX plays, Step Back and Rewind To Frame are "
                           "REFUSED: no modal, no status message, machine untouched";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRW-13", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            const uint32_t f0 = fx.emu.frame_num();
            const uint16_t pc0 = fx.pc();
            fx.emu.rzx_player().start(RzxRecording{});
            Modals m;
            fx.mgr->on_step_back();
            fx.mgr->on_rewind_to_frame(fx.emu.rewind_buffer()->oldest_frame_num());
            m.timer.stop();
            check("QRW-13", desc,
                  m.seen.empty() && status_of(&fx.win).isEmpty() && fx.emu.frame_num() == f0 &&
                      fx.pc() == pc0 && fx.emu.last_state_error().empty(),
                  fmt("modals=%s status='%s' frame %u->%u PC %04X->%04X", m.describe().c_str(),
                      s(status_of(&fx.win)).c_str(), f0, fx.emu.frame_num(), pc0, fx.pc()));
            fx.emu.rzx_player().stop();
        }
    }

    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "a BENIGN failure (trace off, frame out of range) shows "
                           "no modal and no status message";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRW-14", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            fx.emu.trace_log().set_enabled(false);
            Modals m;
            fx.mgr->on_step_back();                        // step_back: trace disabled
            fx.mgr->on_rewind_to_frame(fx.emu.rewind_buffer()->newest_frame_num() + 50);
            m.timer.stop();
            check("QRW-14", desc,
                  m.seen.empty() && status_of(&fx.win).isEmpty() &&
                      fx.emu.last_state_error().empty(),
                  fmt("modals=%s status='%s' err='%s'", m.describe().c_str(),
                      s(status_of(&fx.win)).c_str(), fx.emu.last_state_error().c_str()));
        }
    }

    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "a CORRUPT restore shows the \"Rewind Failed\" modal and "
                           "the status message naming the operation and subsystem";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRW-15", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            // Corrupt the 'mmu' sentinel (ordinal 2) in every stored slot —
            // rewind_test's SENT-CHAIN idiom.
            RewindBuffer* rb = fx.emu.rewind_buffer();
            const uint32_t mmu_sentinel = Emulator::kStateSentinelMagic ^ 2u;
            size_t corrupted = 0;
            for (size_t i = 0; i < rb->depth(); ++i) {
                uint8_t* d = rb->slot_data_for_test(i);
                for (size_t off = 0; off + 4 <= rb->snapshot_bytes(); ++off) {
                    uint32_t v;
                    std::memcpy(&v, d + off, 4);
                    if (v == mmu_sentinel) { d[off] ^= 0xFF; ++corrupted; break; }
                }
            }
            Modals m;
            fx.mgr->on_step_back();
            m.timer.stop();
            const Modals::Seen* box = m.first("box");
            const QString status = status_of(&fx.win);
            check("QRW-15", desc,
                  corrupted == rb->depth() && m.boxes() == 1 && box &&
                      box->title == "Rewind Failed" && box->text.contains("Step Back") &&
                      box->text.contains("'mmu'") &&
                      status.startsWith("Step Back failed: snapshot restore desynced at 'mmu'"),
                  fmt("corrupted %zu/%zu modals=%s status='%s'", corrupted, rb->depth(),
                      m.describe().c_str(), s(status).c_str()));
        }
    }
}

// ===========================================================================
// QTR — the trace controls (debugger_window.cpp:213-245, 542-575, 753-772):
// toolbar ball button and Debug > Trace > Enable Trace drive the same flag and
// keep each other (and the ball colour, and Step Back's greying) in step;
// Clear empties the log; Export writes it to the chosen file.
// ===========================================================================
static void test_trace_ui() {
    set_group("QTR");

    Fixture fx(MachineType::ZX48K, 10);
    if (!fx.ok || !fx.emu.rewind_buffer()) {
        for (const char* id : {"QTR-01", "QTR-02", "QTR-03", "QTR-04"})
            check(id, "fixture", false);
        return;
    }
    load_counter(fx);
    fx.enable();
    DebuggerWindow* dbg = fx.dbg();
    run_frames_then_break(fx, 2);

    const QRgb green = qRgb(0x00, 0xC0, 0x00), red = qRgb(0xC0, 0x00, 0x00);
    QPushButton* ball = button_where(dbg, [](const QString& t) {
        return t == "Trace" || t.endsWith(": Trace");
    });
    QAction* menu = debug_sub_item(dbg, "Trace", "Enable Trace");
    QAction* step_back = debug_item(dbg, "Step Back");

    {
        const bool on0 = fx.emu.trace_log().enabled() && menu && menu->isChecked() &&
                         ball_colour(ball) == green && action_enabled(step_back);
        if (ball) ball->click();
        const bool off = !fx.emu.trace_log().enabled() && menu && !menu->isChecked() &&
                         ball_colour(ball) == red && !action_enabled(step_back);
        if (ball) ball->click();
        const bool on1 = fx.emu.trace_log().enabled() && menu->isChecked() &&
                         ball_colour(ball) == green && action_enabled(step_back);
        check("QTR-01",
              "the toolbar Trace button toggles the trace; the menu check, the "
              "ball colour (green/red) and Step Back's greying follow at once",
              ball && on0 && off && on1,
              fmt("on=%d off=%d on-again=%d ball=%08X", on0, off, on1, ball_colour(ball)));
    }

    {
        if (menu) menu->trigger();
        const bool off = !fx.emu.trace_log().enabled() && ball_colour(ball) == red &&
                         !action_enabled(step_back);
        if (menu) menu->trigger();
        const bool on = fx.emu.trace_log().enabled() && ball_colour(ball) == green &&
                        action_enabled(step_back);
        check("QTR-02",
              "Debug > Trace > Enable Trace toggles the same flag; the ball and "
              "Step Back follow",
              menu && off && on, fmt("off=%d on=%d", off, on));
    }

    {
        const size_t before = fx.emu.trace_log().size();
        if (QAction* clear = debug_sub_item(dbg, "Trace", "Clear Trace")) clear->trigger();
        check("QTR-03", "Clear Trace empties the trace log",
              before > 0 && fx.emu.trace_log().size() == 0,
              fmt("size %zu -> %zu", before, fx.emu.trace_log().size()));
    }

    {
        run_frames_then_break(fx, 1);
        fx.mgr->on_step_into();
        const size_t n = fx.emu.trace_log().size();

        const QString via_menu = g_tmp->filePath("trace_menu.txt");
        Modals m1;
        m1.file_path = via_menu;
        if (QAction* ex = debug_sub_item(dbg, "Trace", "Export Trace...")) ex->trigger();
        m1.timer.stop();

        const QString via_button = g_tmp->filePath("trace_button.txt");
        Modals m2;
        m2.file_path = via_button;
        if (QPushButton* b = button_ending(dbg, "Export Trace")) b->click();
        m2.timer.stop();

        // A write that fails: the chosen directory is gone by the time the
        // export runs.
        const QString gone_dir = g_tmp->filePath("gone");
        QDir().mkpath(gone_dir);
        Modals m3;
        m3.file_path = gone_dir + "/trace.txt";
        m3.after_file_accept = [gone_dir]() { QDir(gone_dir).removeRecursively(); };
        if (QAction* ex = debug_sub_item(dbg, "Trace", "Export Trace...")) ex->trigger();
        m3.timer.stop();

        auto lines_in = [](const QString& p) {
            QFile f(p);
            if (!f.open(QIODevice::ReadOnly)) return -1;
            return static_cast<int>(f.readAll().count('\n'));
        };
        const Modals::Seen* fail = m3.first("box");
        check("QTR-04",
              "Export Trace (menu and button) writes the log to the chosen file; a "
              "write that fails shows \"Export Failed\"",
              n > 0 && lines_in(via_menu) >= static_cast<int>(n) &&
                  lines_in(via_button) >= static_cast<int>(n) && fail &&
                  fail->title == "Export Failed" && fail->text.contains(m3.file_path),
              fmt("trace=%zu menu-file lines=%d button-file lines=%d modals: %s | %s | %s",
                  n, lines_in(via_menu), lines_in(via_button), m1.describe().c_str(),
                  m2.describe().c_str(), m3.describe().c_str()));
    }
}

// ===========================================================================
// QMAP — Map > Load MAP File (debugger_manager.cpp:624-659): the file is
// chosen in the REAL file dialog, the result is reported in a message box,
// and the loaded symbols reach the Disassembly, Call Stack and Breakpoints
// panels through the manager's table.
//
// QMAP-04 pins the Z88DK loader's failure and zero-symbol cases (GH #278 WP0
// fix): on_load_map_z88dk() used to test load_z88dk_map()'s int result as a
// bool, so an unreadable file (-1) reported "MAP Loaded" over the old table and
// a valid map with no `; addr` symbols (0) reported "Load Failed" after
// clearing it.
// ===========================================================================
static void test_map_load() {
    set_group("QMAP");

    Fixture fx;
    if (!fx.ok) {
        for (const char* id : {"QMAP-01", "QMAP-02", "QMAP-03", "QMAP-04"})
            check(id, "fixture", false);
        return;
    }
    fx.load(PROG, {0xCD, 0x00, 0x90, 0x18, 0xFE});        // CALL $9000 / JR $
    fx.load(0x9000, {0x21, 0x00, 0x91, 0x18, 0xFE});      // LD HL,$9100 / JR $
    fx.regs(PROG);
    fx.enable();
    DebuggerWindow* dbg = fx.dbg();
    QMenu* load_menu = submenu_named(menu_named(dbg->menuBar(), "Map"), "Load MAP File");

    {
        const std::string path = write_file("prog.map",
            "_main          = $8000 ; addr, public, , main_c, code_compiler, main.c:3\n"
            "_sub           = $9000 ; addr, public, , main_c, code_compiler, main.c:9\n"
            "_buffer        = $9100 ; addr, public, , main_c, bss_compiler, main.c:1\n"
            "__SIZE         = $0010 ; const, public, , , , \n");
        Modals m;
        m.file_path = QString::fromStdString(path);
        if (QAction* a = item_named(load_menu, "Z88DK Format...")) a->trigger();
        m.timer.stop();
        const Modals::Seen* box = m.first("box");
        check("QMAP-01",
              "Z88DK Format...: the chosen map's `; addr` symbols load and the box "
              "says \"MAP Loaded\" / \"Loaded N symbols from:\\n<path>\"",
              box && box->title == "MAP Loaded" &&
                  box->text == QStringLiteral("Loaded 3 symbols from:\n%1")
                                   .arg(QString::fromStdString(path)) &&
                  fx.mgr->symbol_table().size() == 3,
              fmt("modals=%s table=%zu", m.describe().c_str(), fx.mgr->symbol_table().size()));
    }

    {
        // Z88DK, a file that cannot be read: "Load Failed", the table untouched.
        const std::string gone_path = write_file("gone_z88dk.map", "_x = $1234 ; addr\n");
        Modals bad;
        bad.file_path = QString::fromStdString(gone_path);
        bad.after_file_accept = [gone_path]() { QFile::remove(QString::fromStdString(gone_path)); };
        if (QAction* a = item_named(load_menu, "Z88DK Format...")) a->trigger();
        bad.timer.stop();
        const size_t after_bad = fx.mgr->symbol_table().size();

        // Z88DK, a readable map with nothing but a `; const`: a load of zero.
        const std::string none_path = write_file("consts.map", "__SIZE = $0010 ; const, public\n");
        Modals none;
        none.file_path = QString::fromStdString(none_path);
        if (QAction* a = item_named(load_menu, "Z88DK Format...")) a->trigger();
        none.timer.stop();

        const Modals::Seen* badb = bad.first("box");
        const Modals::Seen* noneb = none.first("box");
        check("QMAP-04",
              "Z88DK Format...: an unreadable file gives \"Load Failed\" and keeps the "
              "table; a map with no `; addr` symbols loads zero (\"MAP Loaded\", "
              "\"Loaded 0 symbols\")",
              badb && badb->title == "Load Failed" &&
                  badb->text == QStringLiteral("Could not load MAP file:\n%1")
                                    .arg(QString::fromStdString(gone_path)) &&
                  after_bad == 3 && noneb && noneb->title == "MAP Loaded" &&
                  noneb->text == QStringLiteral("Loaded 0 symbols from:\n%1")
                                     .arg(QString::fromStdString(none_path)) &&
                  fx.mgr->symbol_table().size() == 0,
              fmt("bad: %s table after=%zu; none: %s table after=%zu", bad.describe().c_str(),
                  after_bad, none.describe().c_str(), fx.mgr->symbol_table().size()));
    }

    {
        const std::string ok_path = write_file("rom.map", "; comment\nSTART = $0000\nCLS = $0D6B\n");
        Modals ok;
        ok.file_path = QString::fromStdString(ok_path);
        QAction* simple = item_named(load_menu, "Simple Format (48K ROM)...");
        if (simple) simple->trigger();
        ok.timer.stop();

        const std::string gone_path = write_file("gone.map", "X = $1234\n");
        Modals bad;
        bad.file_path = QString::fromStdString(gone_path);
        bad.after_file_accept = [gone_path]() { QFile::remove(QString::fromStdString(gone_path)); };
        if (simple) simple->trigger();
        bad.timer.stop();

        // A readable map with no symbols is a successful load of zero.
        const std::string empty_path = write_file("empty.map", "; nothing but a comment\n");
        Modals empty;
        empty.file_path = QString::fromStdString(empty_path);
        if (simple) simple->trigger();
        empty.timer.stop();

        const Modals::Seen* okb = ok.first("box");
        const Modals::Seen* badb = bad.first("box");
        const Modals::Seen* emptyb = empty.first("box");
        check("QMAP-02",
              "Simple Format...: \"MAP Loaded\" with the count (0 included); a file "
              "that cannot be read gives \"Load Failed\" / \"Could not load MAP "
              "file:\\n<path>\"",
              okb && okb->title == "MAP Loaded" &&
                  okb->text == QStringLiteral("Loaded 2 symbols from:\n%1")
                                   .arg(QString::fromStdString(ok_path)) &&
                  badb && badb->title == "Load Failed" &&
                  badb->text == QStringLiteral("Could not load MAP file:\n%1")
                                    .arg(QString::fromStdString(gone_path)) &&
                  emptyb && emptyb->title == "MAP Loaded" &&
                  emptyb->text == QStringLiteral("Loaded 0 symbols from:\n%1")
                                      .arg(QString::fromStdString(empty_path)),
              fmt("ok: %s bad: %s empty: %s", ok.describe().c_str(), bad.describe().c_str(),
                  empty.describe().c_str()));
    }

    {
        // Reload the Z88DK map and look at the three panels that read the table.
        const std::string path = write_file("prog2.map",
            "_main   = $8000 ; addr, public\n_sub    = $9000 ; addr, public\n"
            "_buffer = $9100 ; addr, public\n");
        Modals m;
        m.file_path = QString::fromStdString(path);
        if (QAction* a = item_named(load_menu, "Z88DK Format...")) a->trigger();
        m.timer.stop();

        // Disassembly: the painter's substitution is the copy text's.
        DisasmPanel* dp = dbg->disasm_panel();
        if (auto* sb = dp->findChild<QScrollBar*>()) sb->setValue(0x9000);
        QApplication::processEvents();
        dp->select_all_visible();
        const QString asm_text = dp->selection_text(disasm_text::CopyFormat::AsmOnly);
        dp->clear_selection();

        // Call Stack: enter the CALL with tracking on, then pause.
        fx.mgr->on_step_into();
        const QString target = table_cell(dbg->callstack_panel(), 0, 3);

        // Breakpoints: a PC breakpoint on a symbol's address.
        fx.emu.debug_state().breakpoints().add_pc(0x8000);
        BreakpointPanel* bp = dbg->breakpoint_panel();
        QString sym_col;
        if (auto* t = bp->findChild<QTableWidget*>())
            for (int r = 0; r < t->rowCount(); ++r)
                if (t->item(r, 2) && t->item(r, 2)->text() == "$8000" && t->item(r, 3))
                    sym_col = t->item(r, 3)->text();

        check("QMAP-03",
              "loaded symbols reach the Disassembly (operand), the Call Stack "
              "(target) and the Breakpoints panel (Symbol column)",
              asm_text.contains("LD HL,_buffer") && target == "_sub" && sym_col == "_main",
              fmt("asm has LD HL,_buffer=%d target='%s' symbol='%s'",
                  asm_text.contains("LD HL,_buffer"), s(target).c_str(), s(sym_col).c_str()));
    }
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // The MAP and trace rows drive the REAL QFileDialog. A desktop platform
    // theme (QT_QPA_PLATFORMTHEME) could substitute a native dialog this
    // process cannot answer; the widget-based one is the only one testable.
    qunsetenv("QT_QPA_PLATFORMTHEME");
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    g_tmp = &cfg;
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    QApplication app(argc, argv);

    test_step_over();
    test_step_into();
    test_pause_edge();
    test_throttle();
    test_enable_seeds();
    test_rewind_ui();
    test_trace_ui();
    test_map_load();

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
