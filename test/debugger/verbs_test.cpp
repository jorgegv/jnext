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
#include "debug/debug_keymap.h"
#include "debug/disasm_text.h"
#include "debug/rewind_buffer.h"
#include "debug/source_file.h"
#include "debug/symbol_table.h"
#include "debug/trace.h"
#include "debugger/breakpoint_model.h"
#include "script/bp_script.h"
#include "script/script_engine.h"
#include "debugger/breakpoint_panel.h"
#include "debugger/callstack_panel.h"
#include "debugger/cpu_panel.h"
#include "debugger/reg_edit.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/disasm_panel.h"
#include "debugger/memory_panel.h"
#include "debugger/mmu_panel.h"
#include "debugger/nextreg_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/symbols_panel.h"
#include "debugger/video_panel.h"
#include "put_on_show.h"
#include "debugger/watch_panel.h"
#include "memory/mmu.h"
#include "platform/emulator_boot.h"
#include "port/nextreg.h"
#include "video/renderer.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QTabWidget>
#include "dock_lookup.h"
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QKeySequence>
#include <QMouseEvent>
#include <QShortcutEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTimer>
#include <QToolBar>

#include <cstdarg>
#include <filesystem>
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
    // GH #278 WP2 — the loop owner's backend (QtApp::debugger()), built
    // after init() and declared before the window, so it outlives the manager.
    std::unique_ptr<jnext::dbg::Debugger> backend;
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
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        mgr = new DebuggerManager(&win, *backend, &win);   // parented -> freed
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

    /// GH #498: the open window tracks calls only while the Call Stack (or Variables) is on show.
    void callstack_on_show() { put_on_show(dbg(), dbg()->callstack_panel()); }

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
    struct Seen { QString kind, title, text, filters; int icon = 0; QString initial = QString(); };   // icon: a QMessageBox's (GH #389); initial: a file dialog's file-name text when it opened (GH #376)

    QString                    file_path;            // for a QFileDialog
    QStringList                file_paths;           // GH #389: the files of a multi-select dialog (ExistingFiles)
    QString                    dir_path;             // GH #389: the directory of a directory dialog
    bool                       cancel_files = false; // GH #389: cancel the file/directory dialog instead of answering it
    std::function<void()>      after_file_accept;    // e.g. make the file vanish
    int                        spin_value = -1;      // for a dialog with a QSpinBox
    int                        spin_seen  = -1;      // ... and what it opened showing
    QMessageBox::StandardButton box_answer = QMessageBox::Ok;
    std::vector<Seen>          seen;
    QTimer                     timer;
    static constexpr size_t    kMaxModals = 16;

    Modals() {
        QObject::connect(&timer, &QTimer::timeout, [this]() { poll(); });
        timer.start(1);
    }
    ~Modals() { timer.stop(); }

    // GH #387 review: a bound. A modal that stays open past `deadline_ms` is rejected and recorded as "stuck"
    // (the row FAILs naming it, instead of the suite waiting for an answer nobody gives), and with
    // `expect_modals` false any modal is rejected unanswered and recorded as "unexpected". Neither changes what a
    // row that expects its dialogs gets.
    bool                       expect_modals = true;
    bool                       stuck = false;
    int                        deadline_ms = 20000;
    QElapsedTimer              open_since;

    void poll() {
        QWidget* w = QApplication::activeModalWidget();
        if (!w) { open_since.invalidate(); return; }
        auto* dlg = qobject_cast<QDialog*>(w);
        if (!dlg) return;
        if (!open_since.isValid()) open_since.start();
        else if (open_since.elapsed() > deadline_ms) {
            stuck = true;
            seen.push_back({"stuck", dlg->windowTitle(), {}, {}});
            dlg->reject();
            return;
        }
        if (!expect_modals) {
            seen.push_back({"unexpected", dlg->windowTitle(), {}, {}});
            dlg->reject();
            return;
        }
        if (seen.size() >= kMaxModals) { dlg->reject(); return; }
        if (auto* fd = qobject_cast<QFileDialog*>(w)) {
            {
                Seen fs{"file", fd->windowTitle(), {}, fd->nameFilters().join(QStringLiteral(";;"))};
                if (auto* le = fd->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) fs.initial = le->text();
                seen.push_back(fs);
            }
            if (cancel_files) { dlg->reject(); return; }
            if (fd->fileMode() == QFileDialog::ExistingFiles || fd->fileMode() == QFileDialog::Directory) {
                // The widget dialog takes the typed names: a quoted list for several files, the path for a directory.
                QString typed = dir_path;
                if (fd->fileMode() == QFileDialog::ExistingFiles) {
                    typed.clear();
                    for (const QString& f : file_paths) typed += (typed.isEmpty() ? "" : " ") + QStringLiteral("\"%1\"").arg(f);
                }
                if (auto* le = fd->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) le->setText(typed);
            } else {
                fd->selectFile(file_path);
            }
            dlg->accept();          // virtual: QFileDialog::accept(), which validates
            if (after_file_accept) after_file_accept();
            return;
        }
        if (auto* mb = qobject_cast<QMessageBox*>(w)) {
            seen.push_back({"box", mb->windowTitle(), mb->text(), {}, static_cast<int>(mb->icon())});
            if (QAbstractButton* b = mb->button(box_answer)) b->click();
            else mb->done(box_answer);
            return;
        }
        seen.push_back({"dialog", dlg->windowTitle(), {}, {}});
        if (auto* spin = dlg->findChild<QSpinBox*>()) {
            spin_seen = spin->value();
            if (spin_value >= 0) spin->setValue(spin_value);
        }
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

    // QSO-07 — GH #278 WP4c: a breakpoint hit INSIDE the stepped-over call is a
    // stop, and a stop drops the Step Over's target (§4.3: a transient is
    // "auto-removed at the next stop") — so the next Run runs on and does not
    // stop at the stale target. The GUI behaved so before package Q, where
    // resume() cleared the one-shot; between WP2 and WP4c a breakpoint was a
    // legacy `BreakpointSet` stop, which the backend did not see, and the stale
    // target stopped the Run. As a subscription stop it is seen again.
    //   8000  CD 00 90   CALL $9000     8003  00   NOP  <- the target
    //   8004  18 FE      JR $                          <- the park
    //   9000  3C   INC A   9001  3C   INC A  <- breakpoint   9002  C9  RET
    {
        Fixture fx;
        const char* desc = "a breakpoint inside a stepped-over CALL stops it there, "
                           "and the next Run does not stop at the stale Step Over "
                           "target";
        if (!fx.ok) { check("QSO-07", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0xCD, 0x00, 0x90, 0x00, 0x18, 0xFE});
            fx.load(0x9000, {0x3C, 0x3C, 0xC9});
            fx.regs(PROG, [](Z80Registers& r) { r.AF = 0x0000; });
            fx.enable();
            fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x9001);
            fx.mgr->on_step_over();
            fx.tick_until_paused();
            const bool inside = fx.paused() && fx.pc() == 0x9001;
            fx.mgr->on_run();
            for (int i = 0; i < 3; ++i) fx.tick();
            check("QSO-07", desc,
                  inside && !fx.paused() && fx.pc() == 0x8004,
                  fmt("stopped inside=%d; after Run paused=%d PC=%04X (want running at "
                      "8004; 8003 is the stale target)", inside, fx.paused(), fx.pc()));
        }
    }
}

// ===========================================================================
// QIO — GH #278 WP4c: an I/O breakpoint set through the GUI's model is a Port
// subscription whose mask carries GH #222's rule — an address 00-FF is a
// LOW-BYTE match, 0100 and up an exact port. Asserted by where a guest IN/OUT
// stops the machine, since a row that only reads the panel back cannot tell a
// mask from its mirror image.
// ===========================================================================
static void test_io_breakpoints() {
    set_group("QIO");

    //   8000  01 FE 7F   LD BC,$7FFE     8003  ED 78   IN A,(C)
    //   8005  00         NOP             8006  18 FE   JR $
    {
        Fixture fx(MachineType::ZX48K, 0, /*paused=*/false);
        const char* desc = "an IO Read on 00FE (low-byte form) stops a guest IN from "
                           "port 7FFE, after the IN";
        if (!fx.ok) { check("QIO-01", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x01, 0xFE, 0x7F, 0xED, 0x78, 0x00, 0x18, 0xFE});
            fx.regs(PROG);
            fx.enable();
            fx.mgr->breakpoints().add(BreakpointModel::IoRead, 0x00FE);
            fx.tick_until_paused();
            check("QIO-01", desc, fx.paused() && fx.pc() == 0x8005,
                  fmt("paused=%d PC=%04X (want paused at 8005)", fx.paused(), fx.pc()));
        }
    }
    //   8000  01 3B 25   LD BC,$253B     8003  ED 79   OUT (C),A
    //   8005  01 3B 24   LD BC,$243B     8008  ED 79   OUT (C),A
    //   800A  00         NOP             800B  18 FE   JR $
    {
        Fixture fx(MachineType::ZX48K, 0, /*paused=*/false);
        const char* desc = "an IO Write on 243B (exact form) lets an OUT to 253B — the "
                           "same low byte — pass, and stops the OUT to 243B";
        if (!fx.ok) { check("QIO-02", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x01, 0x3B, 0x25, 0xED, 0x79, 0x01, 0x3B, 0x24, 0xED, 0x79,
                           0x00, 0x18, 0xFE});
            fx.regs(PROG, [](Z80Registers& r) { r.AF = 0x0000; });
            fx.enable();
            fx.mgr->breakpoints().add(BreakpointModel::IoWrite, 0x243B);
            fx.tick_until_paused();
            check("QIO-02", desc, fx.paused() && fx.pc() == 0x800A,
                  fmt("paused=%d PC=%04X (want paused at 800A; 8005 = stopped on the "
                      "253B OUT)", fx.paused(), fx.pc()));
        }
    }
    // QIO-03/04 — the rule's BOUNDARY, both sides of it (WP4c review, M9). 00FF
    // is the last low-byte address: an IN from 12FF stops. 0100 is the first
    // exact one: an IN from 2200 (low byte 00, as 0100's) passes and the IN from
    // 0100 itself stops.
    //   8000  01 FF 12   LD BC,$12FF     8003  ED 78   IN A,(C)
    //   8005  00         NOP             8006  18 FE   JR $
    {
        Fixture fx(MachineType::ZX48K, 0, /*paused=*/false);
        const char* desc = "an IO Read on 00FF — the last low-byte address — stops a "
                           "guest IN from port 12FF, after the IN";
        if (!fx.ok) { check("QIO-03", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x01, 0xFF, 0x12, 0xED, 0x78, 0x00, 0x18, 0xFE});
            fx.regs(PROG);
            fx.enable();
            fx.mgr->breakpoints().add(BreakpointModel::IoRead, 0x00FF);
            fx.tick_until_paused();
            check("QIO-03", desc, fx.paused() && fx.pc() == 0x8005,
                  fmt("paused=%d PC=%04X (want paused at 8005)", fx.paused(), fx.pc()));
        }
    }
    //   8000  01 00 22   LD BC,$2200     8003  ED 78   IN A,(C)
    //   8005  01 00 01   LD BC,$0100     8008  ED 78   IN A,(C)
    //   800A  00         NOP             800B  18 FE   JR $
    {
        Fixture fx(MachineType::ZX48K, 0, /*paused=*/false);
        const char* desc = "an IO Read on 0100 — the first exact address — lets an IN "
                           "from 2200 (the same low byte) pass, and stops the IN from "
                           "0100";
        if (!fx.ok) { check("QIO-04", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x01, 0x00, 0x22, 0xED, 0x78, 0x01, 0x00, 0x01, 0xED, 0x78,
                           0x00, 0x18, 0xFE});
            fx.regs(PROG);
            fx.enable();
            fx.mgr->breakpoints().add(BreakpointModel::IoRead, 0x0100);
            fx.tick_until_paused();
            check("QIO-04", desc, fx.paused() && fx.pc() == 0x800A,
                  fmt("paused=%d PC=%04X (want paused at 800A; 8005 = stopped on the "
                      "2200 IN)", fx.paused(), fx.pc()));
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
            fx.mgr->breakpoints().add(BreakpointModel::Read, 0x9000);
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
        fx.callstack_on_show();   // GH #498
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

        fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x9001);
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

    // GH #278 WP2 — the window reads the machine's pause state from the backend
    // on every tick, so a transition ANOTHER CLIENT causes reaches it too.

    // QPE-07 — a RESUME the window did not cause (another client's run()): the
    // next tick flips the actions to the running shape and the paused-only
    // panels stop following the machine. Before WP2 the window learnt of a
    // resume only from its own verbs, and stayed in the paused shape — frozen
    // on a running machine, and deaf to its next pause edge.
    {
        Fixture f;
        const char* desc = "another client's run(): on the next tick the actions "
                           "flip to the running shape and CPU / Stack stop following";
        if (!f.ok) { check("QPE-07", desc, false, "fixture"); }
        else {
            f.load(PROG, {0x18, 0xFE});                         // JR $
            f.load(TEST_SP, {0x11, 0x22});
            f.regs(PROG, [](Z80Registers& r) { r.HL = 0x1111; });
            f.enable();
            f.tick();                                           // the pause edge
            DebuggerWindow* dbg = f.dbg();
            const bool shown_paused = actions_paused_shape(dbg);
            const jnext::dbg::ClientId remote =
                f.backend->attach(jnext::dbg::ClientInfo{"remote", jnext::dbg::ClientKind::Dzrp})
                    .value;
            const jnext::dbg::Result rr = f.backend->run(remote);
            f.tick();
            const bool running = !f.paused() && actions_running_shape(dbg);
            Z80Registers r = f.emu.cpu().get_registers();
            r.HL = 0x2222;
            f.emu.cpu().set_registers(r);
            f.emu.mmu().write(TEST_SP, 0x99);
            for (int i = 0; i < 12; ++i) f.mgr->refresh_panels();
            const QString hl = cpu_value(dbg, "HL: ");
            const QString w0 = table_cell(dbg->stack_panel(), 0, 1);
            const QString want_w0 = QString::asprintf("%04X (%5d)", 0x2211, 0x2211);
            check("QPE-07", desc,
                  shown_paused && rr == jnext::dbg::Result::Ok && running && hl == "1111" &&
                      w0 == want_w0,
                  fmt("paused shape before=%d run=%s running shape after the tick=%d "
                      "HL shown %s stack0 %s",
                      shown_paused, jnext::dbg::result_name(rr), running, s(hl).c_str(),
                      s(w0).c_str()));
        }
    }

    // QPE-08 — a PAUSE another client causes while the window is open: the next
    // tick is a full pause edge — CPU and Stack on the stopped state, the
    // Disassembly on PC, the paused actions — and the pause stays that client's.
    {
        Fixture f(MachineType::ZX48K, 0, /*paused=*/false);
        const char* desc = "another client's pause() with the window open: the next "
                           "tick is a full pause edge, and the pause stays that client's";
        if (!f.ok) { check("QPE-08", desc, false, "fixture"); }
        else {
            f.load(PROG, {0x23, 0x18, 0xFD});                   // INC HL / JR $8000
            f.regs(PROG, [](Z80Registers& r) { r.HL = 0; });
            f.enable();
            DebuggerWindow* dbg = f.dbg();
            if (auto* sb = dbg->disasm_panel()->findChild<QScrollBar*>()) sb->setValue(0xC000);
            for (int i = 0; i < 3; ++i) f.tick();
            const bool ran = !f.paused() && actions_running_shape(dbg);
            const jnext::dbg::ClientId remote =
                f.backend->attach(jnext::dbg::ClientInfo{"remote", jnext::dbg::ClientKind::Dzrp})
                    .value;
            f.backend->pause(remote);
            f.tick();
            const Z80Registers r = f.emu.cpu().get_registers();
            const jnext::dbg::RunState st = f.backend->state();
            check("QPE-08", desc,
                  ran && f.paused() && actions_paused_shape(dbg) && r.HL != 0 &&
                      cpu_value(dbg, "PC: ") == QString::asprintf("%04X", r.PC) &&
                      cpu_value(dbg, "HL: ") == QString::asprintf("%04X", r.HL) &&
                      table_cell(dbg->stack_panel(), 0, 0) == QString::asprintf("%04X", r.SP) &&
                      disasm_shows(dbg->disasm_panel(), r.PC) &&
                      st.pause_reason.kind == jnext::dbg::PauseReason::Kind::User &&
                      st.pause_reason.by == remote,
                  fmt("ran=%d paused=%d actions=%d PC=%04X shown=%s HL=%04X shown=%s "
                      "disasm=%d by=%u (remote %u)",
                      ran, f.paused(), actions_paused_shape(dbg), r.PC,
                      s(cpu_value(dbg, "PC: ")).c_str(), r.HL,
                      s(cpu_value(dbg, "HL: ")).c_str(),
                      disasm_shows(dbg->disasm_panel(), r.PC), st.pause_reason.by, remote));
        }
    }

    // QPE-09 — the pause edge is an EDGE: while the machine stays paused, the
    // ticks refresh the window but never re-centre the Disassembly. A user who
    // has scrolled it away from PC keeps their view. (Re-applying the pause edge
    // on every paused tick — a pause "epoch" counted per tick rather than per
    // stop — snapped the view back to PC every 20 ms; no row saw it.)
    {
        Fixture f;
        const char* desc = "while paused, ticks refresh the Disassembly but leave "
                           "it where the user scrolled it (no re-centring on PC)";
        if (!f.ok) { check("QPE-09", desc, false, "fixture"); }
        else {
            f.load(PROG, {0x18, 0xFE});                         // JR $
            f.regs(PROG);
            for (uint16_t a = 0xC000; a < 0xC100; ++a) f.emu.mmu().write(a, 0x00);
            f.enable();
            f.tick();                                           // the pause edge
            DebuggerWindow* dbg = f.dbg();
            const bool at_pc = disasm_shows(dbg->disasm_panel(), PROG);
            if (auto* sb = dbg->disasm_panel()->findChild<QScrollBar*>()) sb->setValue(0xC000);
            QApplication::processEvents();
            for (int i = 0; i < 3; ++i) f.tick();
            uint16_t lo = 0, hi = 0;
            dbg->disasm_panel()->select_all_visible();
            dbg->disasm_panel()->selection_range(lo, hi);
            dbg->disasm_panel()->clear_selection();
            check("QPE-09", desc,
                  f.paused() && at_pc && lo == 0xC000 && !disasm_shows(dbg->disasm_panel(), PROG),
                  fmt("paused=%d showed PC first=%d span after 3 ticks %04X..%04X "
                      "shows PC=%d", f.paused(), at_pc, lo, hi,
                      disasm_shows(dbg->disasm_panel(), PROG)));
        }
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

    // QTH-02 (rewritten, owner-approved 2026-11-01, GH #428). It used to pin
    // "while paused, every manager tick refreshes the panels", which IS the
    // reported defect (a paused machine redrew every panel fifty times a
    // second), and it drove the change with a direct core write no production
    // path makes. Production changes go through the backend (every client's
    // write verbs) or follow user input. The claim now: a change made through
    // the backend shows at the next paused tick. QIDLE-01 pins the idle half.
    fx.mgr->on_pause();
    jnext::dbg::ClientInfo other_info;
    other_info.name = "other";
    other_info.kind = jnext::dbg::ClientKind::Test;
    const jnext::dbg::ClientId other = fx.backend->attach(other_info).value;
    fx.mgr->refresh_panels();
    fx.backend->nextreg_write(other, 0x7F, 0x33);
    fx.mgr->refresh_panels();
    const QString once = shown();
    fx.backend->nextreg_write(other, 0x7F, 0x44);
    fx.mgr->refresh_panels();
    check("QTH-02",
          "while paused, a NextREG change made through the backend shows at the next tick",
          once == "33" && shown() == "44",
          fmt("after 1st tick %s, 2nd %s", s(once).c_str(), s(shown()).c_str()));
}

// ===========================================================================
// QIDLE (GH #428) -- a paused machine with nothing changing costs no panel
// refresh. Oracle: the plan's contract (section 2.5): a paused window is
// redrawn when the clock moved, the backend's state revision moved (any
// client's write, metadata verb, cold boot), the user did something (key,
// mouse button, wheel anywhere in the app), or the loaded source file changed
// on disk -- once, at the next tick -- and at no other time. The count is
// DebuggerManager::take_panel_refresh_count(); every row ticks like QtApp
// (Fixture::tick() = check_breakpoint_hit() + refresh_panels()).
// ===========================================================================
// QLADDR-18/19 (GH #360 with GH #428) -- what the long-address views show while paused reaches the
// panels. Oracle: the QIDLE contract above (a paused window is redrawn when the clock, the backend's
// revision, an input or the registers moved, and at no other time) and the panels' own spelling.
//  18: the Debug > Physical Addresses toggle is the user's input and redraws the panels itself; the
//      idle ticks after it refresh nothing and do not undo it.
//  19: another client's NextREG write that remaps a slot (a backend verb, so the revision moves)
//      refreshes once at the next tick and the MMU panel names the new page ("P20").
static void test_long_address_paused() {
    set_group("QLADDR");
    Fixture fx(MachineType::ZXN_ISSUE2);
    if (!fx.ok) {
        check("QLADDR-18", "fixture", false);
        check("QLADDR-19", "fixture", false);
        return;
    }
    fx.load(PROG, {0x00, 0x00, 0x18, 0xFE});
    fx.regs(PROG);
    fx.enable();
    jnext::dbg::ClientInfo oi;
    oi.name = "other";
    oi.kind = jnext::dbg::ClientKind::Test;
    const jnext::dbg::ClientId other = fx.backend->attach(oi).value;
    fx.tick();
    fx.tick();
    fx.mgr->take_panel_refresh_count();
    DebuggerWindow* win = fx.dbg();
    auto mmu_text = [&]() {
        QString t;
        if (MmuPanel* m = win->findChild<MmuPanel*>())
            for (QLabel* l : m->findChildren<QLabel*>()) t += l->text() + "|";
        return t;
    };
    {
        const QString before = mmu_text();
        win->set_address_style(jnext::dbg::AddressStyle::Physical);
        const QString after = mmu_text();
        for (int i = 0; i < 20; ++i) fx.tick();
        const uint64_t n = fx.mgr->take_panel_refresh_count();
        const QString later = mmu_text();
        win->set_address_style(jnext::dbg::AddressStyle::Friendly);
        check("QLADDR-18",
              "Debug > Physical Addresses on a paused idle machine redraws the panels at once (the MMU panel's "
              "text changes), and 20 idle ticks after it refresh nothing and keep the new style",
              !before.isEmpty() && before != after && later == after && n == 0,
              fmt("changed=%d kept=%d n=%llu", before != after, later == after, (unsigned long long)n));
    }
    {
        fx.tick();
        fx.mgr->take_panel_refresh_count();
        const QString before = mmu_text();
        fx.backend->nextreg_write(other, 0x56, 0x20);   // slot 6 ($C000) -> 8K page $20
        fx.tick();
        const uint64_t n1 = fx.mgr->take_panel_refresh_count();
        const QString after = mmu_text();
        fx.tick();
        const uint64_t n2 = fx.mgr->take_panel_refresh_count();
        check("QLADDR-19",
              "another client's NextREG write that remaps slot 6 while paused refreshes once at the next tick and "
              "the MMU panel names the new page P20; the tick after refreshes nothing",
              n1 == 1 && n2 == 0 && !before.contains("P20") && after.contains("P20"),
              fmt("n1=%llu n2=%llu before_has=%d after_has=%d", (unsigned long long)n1, (unsigned long long)n2,
                  before.contains("P20"), after.contains("P20")));
    }
}

static void test_idle_refresh() {
    set_group("QIDLE");

    Fixture fx(MachineType::ZXN_ISSUE2);
    if (!fx.ok) {
        for (const char* id : {"QIDLE-01", "QIDLE-02", "QIDLE-03", "QIDLE-04", "QIDLE-05",
                               "QIDLE-07", "QIDLE-08", "QIDLE-09", "QIDLE-10", "QIDLE-11", "QIDLE-12", "QIDLE-13"})
            check(id, "fixture", false);
        return;
    }
    fx.load(PROG, {0x00, 0x00, 0x18, 0xFE});              // NOP NOP JR $
    fx.regs(PROG);
    fx.enable();
    jnext::dbg::ClientInfo oi;
    oi.name = "other";
    oi.kind = jnext::dbg::ClientKind::Test;
    const jnext::dbg::ClientId other = fx.backend->attach(oi).value;
    // Past the pause edge and settled: the first ticks may refresh, the rest not.
    fx.tick();
    fx.tick();
    fx.mgr->take_panel_refresh_count();
    DebuggerWindow* win = fx.dbg();

    // QIDLE-01: 50 idle ticks refresh nothing; one tick after a pause edge does.
    for (int i = 0; i < 50; ++i) fx.tick();
    const uint64_t idle = fx.mgr->take_panel_refresh_count();
    fx.mgr->on_run();
    fx.tick();
    fx.mgr->take_panel_refresh_count();     // the running tick's own (throttled) refresh, if any
    fx.mgr->on_pause();                      // the pause edge redraws the window (once) ...
    fx.tick();                               // ... and the tick after it has nothing to add
    const uint64_t after_edge = fx.mgr->take_panel_refresh_count();
    fx.tick();
    const uint64_t settled = fx.mgr->take_panel_refresh_count();
    check("QIDLE-01",
          "50 idle paused ticks refresh the panels 0 times; the tick after a pause edge, exactly once",
          idle == 0 && after_edge == 1 && settled == 0,
          fmt("idle=%llu after_edge=%llu then=%llu", (unsigned long long)idle,
              (unsigned long long)after_edge, (unsigned long long)settled));

    // QIDLE-02: another client's poke of a watched address shows at the next
    // tick, once.
    {
        win->watch_panel()->add_watch(0x9000, "w", 0);
        fx.tick();
        fx.mgr->take_panel_refresh_count();
        const uint8_t v = 0x5A;
        fx.backend->poke(other, jnext::dbg::MemSpace::cpu(), 0x9000, 1, &v);
        fx.tick();
        const uint64_t n1 = fx.mgr->take_panel_refresh_count();
        QString row;
        if (auto* t = win->watch_panel()->findChild<QTableWidget*>())
            for (int c = 0; c < t->columnCount(); ++c)
                if (t->item(0, c)) row += t->item(0, c)->text() + "|";
        fx.tick();
        const uint64_t n2 = fx.mgr->take_panel_refresh_count();
        check("QIDLE-02",
              "another client's poke of a watched address refreshes once at the next tick and the "
              "Watches value shows the new byte; the tick after refreshes nothing",
              n1 == 1 && n2 == 0 && row.contains("5A", Qt::CaseInsensitive),
              fmt("n1=%llu n2=%llu row=%s", (unsigned long long)n1, (unsigned long long)n2,
                  s(row).c_str()));
    }

    // QIDLE-03: another client's step moves the clock.
    {
        fx.mgr->take_panel_refresh_count();
        fx.backend->step_into(other);
        fx.tick();
        const uint64_t n1 = fx.mgr->take_panel_refresh_count();
        const QString pc = cpu_value(win, "PC: ");
        check("QIDLE-03",
              "another client's step_into refreshes once at the next tick and the CPU panel shows "
              "the new PC",
              n1 == 1 && pc.toUpper() == QString::asprintf("%04X", fx.pc()),
              fmt("n1=%llu panel=%s pc=%04X", (unsigned long long)n1, s(pc).c_str(), fx.pc()));
    }

    // QIDLE-04: another client's load_map shows in the Breakpoints Symbol cell.
    {
        fx.regs(PROG);
        fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x8000);
        fx.tick();
        fx.tick();
        fx.mgr->take_panel_refresh_count();
        const std::string path = write_file("qidle4.map", "_qidle = $8000 ; addr, public\n");
        fx.backend->load_map(path, jnext::dbg::MapFormat::Z88dk);
        fx.tick();
        const uint64_t n1 = fx.mgr->take_panel_refresh_count();
        QString cell = QStringLiteral("<no row>");
        if (auto* t = win->breakpoint_panel()->findChild<QTableWidget*>())
            for (int r = 0; r < t->rowCount(); ++r)
                if (t->item(r, 2) && t->item(r, 2)->text() == "$8000" && t->item(r, 3))
                    cell = t->item(r, 3)->text();
        check("QIDLE-04",
              "another client's load_map refreshes at the next tick and the Breakpoints Symbol cell "
              "shows the new name",
              n1 >= 1 && cell == "_qidle", fmt("n1=%llu cell=%s", (unsigned long long)n1, s(cell).c_str()));
    }

    // QIDLE-05: a host-side change with no backend call (NR 0x07 from the CPU
    // speed menu is the known one) does not refresh by itself; the user input
    // that causes it does, at the next tick, and the NextREG panel shows it.
    {
        NextRegPanel* nr = win->findChild<NextRegPanel*>();
        fx.tick();
        fx.mgr->take_panel_refresh_count();
        fx.emu.nextreg().write(0x7F, 0x6B);
        fx.tick();
        const uint64_t quiet = fx.mgr->take_panel_refresh_count();
        const QString before = table_cell(nr, 0x7F, 2);
        // The click goes to a CHILD of the main window (its menu bar, where the
        // CPU-speed menu lives), not to the main window object itself: the
        // manager's older per-object filter sees only the latter, so only the
        // qApp filter can make this row pass.
        for (QEvent::Type t : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
            QMouseEvent ev(t, QPointF(2, 2), QPointF(2, 2), Qt::LeftButton,
                           t == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                           Qt::NoModifier);
            QApplication::sendEvent(fx.win.menuBar(), &ev);
        }
        fx.tick();
        const uint64_t n1 = fx.mgr->take_panel_refresh_count();
        const QString after = table_cell(nr, 0x7F, 2);
        check("QIDLE-05",
              "a direct core NextREG write alone refreshes nothing; a mouse click on the main window "
              "makes the next tick refresh and the NextREG panel shows the value",
              quiet == 0 && n1 >= 1 && before != "6B" && after == "6B",
              fmt("quiet=%llu n1=%llu before=%s after=%s", (unsigned long long)quiet,
                  (unsigned long long)n1, s(before).c_str(), s(after).c_str()));
    }

    // QIDLE-09: a direct core write of the CPU registers (a host action or a
    // test; no backend verb, no input) is seen: one refresh at the next tick,
    // the CPU panel shows it, then nothing.
    {
        fx.tick();
        fx.mgr->take_panel_refresh_count();
        Z80Registers r = fx.emu.cpu().get_registers();
        r.BC = 0x1D2E;
        fx.emu.cpu().set_registers(r);
        fx.tick();
        const uint64_t n1 = fx.mgr->take_panel_refresh_count();
        const QString bc = cpu_value(win, "BC: ");
        fx.tick();
        const uint64_t n2 = fx.mgr->take_panel_refresh_count();
        check("QIDLE-09",
              "a direct core register write alone refreshes once at the next tick (the CPU panel "
              "shows it) and the tick after refreshes nothing",
              n1 == 1 && n2 == 0 && bc.toUpper() == "1D2E",
              fmt("n1=%llu n2=%llu BC=%s", (unsigned long long)n1, (unsigned long long)n2,
                  s(bc).c_str()));
    }

    // QIDLE-10: the clock term on its own. Another client steps the machine
    // (clock + registers move), then the registers are written back to exactly
    // what the window last drew: only the master clock differs, and that alone
    // is enough for one refresh at the next tick.
    {
        fx.tick();
        fx.mgr->take_panel_refresh_count();
        const Z80Registers drawn = fx.emu.cpu().get_registers();
        fx.backend->step_into(other);
        fx.emu.cpu().set_registers(drawn);
        fx.tick();
        const uint64_t n1 = fx.mgr->take_panel_refresh_count();
        fx.tick();
        const uint64_t n2 = fx.mgr->take_panel_refresh_count();
        check("QIDLE-10",
              "the master clock moving alone (registers back as drawn) refreshes once at the next "
              "tick, then nothing",
              n1 == 1 && n2 == 0,
              fmt("n1=%llu n2=%llu", (unsigned long long)n1, (unsigned long long)n2));
    }

    // QIDLE-11: input to the OTHER top-level window (the debugger window), which
    // no per-object filter of the manager covers: a direct NextREG write alone
    // refreshes nothing; a click in the debugger window makes the next tick do.
    {
        fx.tick();
        fx.mgr->take_panel_refresh_count();
        fx.emu.nextreg().write(0x7F, 0x7C);
        fx.tick();
        const uint64_t quiet = fx.mgr->take_panel_refresh_count();
        for (QEvent::Type t : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
            QMouseEvent ev(t, QPointF(2, 2), QPointF(2, 2), Qt::LeftButton,
                           t == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                           Qt::NoModifier);
            QApplication::sendEvent(win, &ev);
        }
        fx.tick();
        const uint64_t n1 = fx.mgr->take_panel_refresh_count();
        check("QIDLE-11",
              "a click in the debugger window (another top-level) makes the next paused tick refresh",
              quiet == 0 && n1 >= 1,
              fmt("quiet=%llu n1=%llu", (unsigned long long)quiet, (unsigned long long)n1));
    }

    // QIDLE-12 / QIDLE-13: a matched shortcut consumes the KeyPress itself, so
    // the input epoch must count the ShortcutOverride that precedes it and the
    // Shortcut event; sent to a child widget (the menu bar), with nothing else
    // having changed.
    {
        fx.tick();
        fx.mgr->take_panel_refresh_count();
        QKeyEvent so(QEvent::ShortcutOverride, Qt::Key_F20, Qt::NoModifier);
        QApplication::sendEvent(fx.win.menuBar(), &so);
        fx.tick();
        const uint64_t n1 = fx.mgr->take_panel_refresh_count();
        check("QIDLE-12", "a ShortcutOverride event makes the next paused tick refresh",
              n1 >= 1, fmt("n1=%llu", (unsigned long long)n1));
        fx.tick();
        fx.mgr->take_panel_refresh_count();
        QShortcutEvent sc(QKeySequence(Qt::Key_F20), 1);
        QApplication::sendEvent(fx.win.menuBar(), &sc);
        fx.tick();
        const uint64_t n2 = fx.mgr->take_panel_refresh_count();
        check("QIDLE-13", "a Shortcut event makes the next paused tick refresh",
              n2 >= 1, fmt("n2=%llu", (unsigned long long)n2));
    }

    // QIDLE-07: a guest cold boot reaches the backend as begin / done.
    {
        fx.tick();
        fx.mgr->take_panel_refresh_count();
        fx.backend->on_cold_boot_begin();
        fx.backend->on_cold_boot_done();
        fx.tick();
        const uint64_t n1 = fx.mgr->take_panel_refresh_count();
        check("QIDLE-07", "a cold boot reported to the backend while paused refreshes at the next tick",
              n1 >= 1, fmt("n1=%llu", (unsigned long long)n1));
    }

    // QIDLE-08: hover / mouse movement alone refreshes nothing.
    {
        fx.tick();
        fx.mgr->take_panel_refresh_count();
        for (int xy : {5, 9}) {
            QMouseEvent ev(QEvent::MouseMove, QPointF(xy, xy), QPointF(xy, xy), Qt::NoButton,
                           Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&fx.win, &ev);
        }
        fx.tick();
        const uint64_t n1 = fx.mgr->take_panel_refresh_count();
        check("QIDLE-08", "mouse movement alone does not refresh a paused window",
              n1 == 0, fmt("n1=%llu", (unsigned long long)n1));
    }
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

    // QEN-03 — GH #278 WP2: with the window open, a hard reset keeps what the
    // window switched on. The window is a backend client, which the backend
    // re-applies on the rebuilt machine (CTL-12 rule 2): still ARMED (a
    // breakpoint at the reset vector stops it), the live raster still on, and
    // call-stack tracking still on. The last half is a defect fix: the window
    // used to switch tracking on directly on the Emulator, which a cold boot
    // reconstructs with tracking off, so after a hard reset the Call Stack
    // stayed empty until the window was closed and reopened.
    {
        Fixture fx(MachineType::ZX48K, 0, /*paused=*/false);
        const char* desc = "window open: a hard reset keeps the machine armed (a "
                           "breakpoint at 0000 stops it), the live raster on and "
                           "call-stack tracking on (a CALL stepped into is listed)";
        if (!fx.ok) { check("QEN-03", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x18, 0xFE});
            fx.regs(PROG);
            fx.enable();
            fx.callstack_on_show();   // GH #498
            fx.tick();
            // GH #278 WP4c — the GUI's breakpoint, carried by the BACKEND across
            // the boot (CTL-12 rule 2): the platform restore that used to carry
            // it is retired (B3 obligation 1).
            fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x0000);
            // QtApp::cold_boot(): the backend's begin, the frontend cold boot, done.
            fx.backend->on_cold_boot_begin();
            emulator_frontend_cold_boot(fx.emu, fx.emu.config(), std::string(),
                                        ColdBootHooks{});
            fx.backend->on_cold_boot_done();
            const bool armed = fx.backend->armed();
            const bool live  = fx.backend->live_raster();
            const bool cs    = fx.backend->call_stack_enabled();
            const bool ran_before = !fx.paused();
            fx.tick();
            const bool stopped = fx.paused() && fx.pc() == 0x0000 &&
                                 actions_paused_shape(fx.dbg());
            // And the Call Stack panel still TRACKS: a CALL stepped into on the
            // rebuilt machine is listed.
            fx.load(0x8000, {0xCD, 0x00, 0x90});                 // CALL $9000
            fx.load(0x9000, {0x18, 0xFE});
            fx.regs(0x8000);
            fx.mgr->on_step_into();
            CallStackPanel* csp = fx.dbg()->callstack_panel();
            const bool tracked = fx.pc() == 0x9000 && table_rows(csp) == 1 &&
                                 table_cell(csp, 0, 3) == "9000";
            check("QEN-03", desc,
                  armed && live && cs && ran_before && stopped && tracked,
                  fmt("armed=%d live_raster=%d call_stack=%d running after boot=%d "
                      "stopped at 0000=%d (PC=%04X) CALL tracked=%d (rows=%d)",
                      armed, live, cs, ran_before, stopped, fx.pc(), tracked,
                      table_rows(csp)));
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
        const char* d11 = "an RZX playback greys both Step Back and Jump Here on the "
                          "next tick, with no verb in between, and running greys both";
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

            // An RZX started while paused, with NO debugger verb in between:
            // the next tick greys both (GH #278 WP0 fix — the greying used to
            // wait for the next verb), and the tick after the stop restores them.
            fx.emu.rzx_player().start(RzxRecording{});
            fx.tick();
            const bool rzx_grey = !action_enabled(step_back) && jump && !jump->isEnabled();
            fx.emu.rzx_player().stop();
            fx.tick();
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
                          "next create uses it); 0 frees the buffer and the next tick "
                          "greys Step Back and Jump Here";
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
            fx.tick();
            QPushButton* jump = button_ending(dbg, "Jump Here");
            const bool freed = fx.emu.rewind_buffer() == nullptr && enable &&
                               !enable->isChecked() &&
                               !action_enabled(debug_item(dbg, "Step Back")) &&
                               jump && !jump->isEnabled();
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

    {
        // WP3 review item 3 — the Buffer Size dialog opens on the ring's DEPTH
        // (the frames it holds), not its capacity: with a partly filled ring the
        // two differ, and the dialog's own comment says which one it means.
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "Rewind Buffer Size... opens pre-filled with the frames the "
                           "ring HOLDS (its depth), not its capacity";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRW-21", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 3);
            const size_t depth0 = fx.emu.rewind_buffer()->depth();
            const size_t cap0   = fx.emu.rewind_buffer()->capacity();
            Modals m;
            m.spin_value = static_cast<int>(cap0);           // answer: keep the size
            if (QAction* sz = debug_sub_item(fx.dbg(), "Rewind", "Rewind Buffer Size..."))
                sz->trigger();
            m.timer.stop();
            check("QRW-21", desc,
                  m.seen.size() == 1 && depth0 > 0 && depth0 < cap0 &&
                      m.spin_seen == static_cast<int>(depth0),
                  fmt("dialogs=%zu pre-filled %d; depth %zu capacity %zu", m.seen.size(),
                      m.spin_seen, depth0, cap0));
        }
    }

    {
        // QRW-22 — GH #278 WP4c: a GUI breakpoint inside the REPLAYED span does
        // not cut a Step Back short. A rewind restores the frame's snapshot and
        // replays forward to the target instruction; the backend consults no
        // subscription while it replays (§4.2a), so the replay lands on its
        // target. As a legacy `BreakpointSet` entry the breakpoint stopped the
        // replay at its first pass, and Step Back landed there instead — a
        // defect, fixed by the breakpoints becoming subscriptions.
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "Step Back lands on the previous instruction even with a "
                           "breakpoint on an address the replay passes";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRW-22", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 3);
            const size_t n = fx.emu.trace_log().size();
            const uint64_t want_cycle = n ? fx.emu.trace_log().at(n - 1).cycle : 0;
            const uint16_t want_pc    = n ? fx.emu.trace_log().at(n - 1).pc : 0;
            // The loop's JR runs every other instruction, so the replay passes it
            // many times between the frame's snapshot and the target.
            fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x8001);
            fx.mgr->on_step_back();
            check("QRW-22", desc,
                  n > 0 && fx.paused() && fx.emu.clock().get() == want_cycle &&
                      fx.pc() == want_pc,
                  fmt("cycle %llu (want %llu) PC %04X (want %04X)",
                      static_cast<unsigned long long>(fx.emu.clock().get()),
                      static_cast<unsigned long long>(want_cycle), fx.pc(), want_pc));
        }
    }

    {
        // QRW-23 — GH #400: Enable Rewind survives a hard reset / File > Open.
        // QtApp cold-boots from its STARTUP config (QtApp::config_), not the
        // live one, so the stale copy is taken BEFORE rewind is enabled; passing
        // fx.emu.config() (as QEN-03 does) would hide the defect. No VHDL
        // counterpart: the oracle is the host-setting rule in
        // emulator_frontend_cold_boot's contract.
        Fixture fx(MachineType::ZX48K, 0);
        const char* desc = "Enable Rewind survives a hard reset: the menu stays checked "
                           "and the ring is rebuilt at the chosen size, recording again";
        if (!fx.ok) { check("QRW-23", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            const EmulatorConfig stale = fx.emu.config();
            DebuggerWindow* dbg = fx.dbg();
            QAction* enable = debug_sub_item(dbg, "Rewind", "Enable Rewind");
            if (enable) enable->trigger();
            run_frames_then_break(fx, 2);
            fx.backend->on_cold_boot_begin();
            emulator_frontend_cold_boot(fx.emu, stale, std::string(), ColdBootHooks{});
            fx.backend->on_cold_boot_done();
            fx.tick();
            RewindBuffer* rb = fx.emu.rewind_buffer();
            const bool kept = enable && enable->isChecked() && rb && rb->capacity() == 500 &&
                              fx.emu.rewind_enabled();
            fx.mgr->on_run();
            for (int i = 0; i < 3; ++i) fx.tick();
            check("QRW-23", desc, kept && fx.emu.rewind_buffer() &&
                                      fx.emu.rewind_buffer()->depth() >= 1,
                  fmt("checked=%d cap=%zu enabled=%d depth=%zu", enable && enable->isChecked(),
                      rb ? rb->capacity() : 0, fx.emu.rewind_enabled(),
                      fx.emu.rewind_buffer() ? fx.emu.rewind_buffer()->depth() : 0));
        }
    }

    {
        // QRW-24 - GH #400: the trace was switched off BEFORE Enable Rewind; the
        // ring implies the trace, and the backend must record that, or the
        // re-application after a cold boot puts the stale "off" back and Step
        // Back is refused while rewind shows on.
        Fixture fx(MachineType::ZX48K, 0);
        const char* desc = "trace off, then Enable Rewind, then a hard reset: the trace "
                           "stays on and Step Back is still offered";
        if (!fx.ok) { check("QRW-24", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            const EmulatorConfig stale = fx.emu.config();
            DebuggerWindow* dbg = fx.dbg();
            fx.backend->set_trace_enabled(false);
            QAction* enable = debug_sub_item(dbg, "Rewind", "Enable Rewind");
            if (enable) enable->trigger();
            run_frames_then_break(fx, 2);
            fx.backend->on_cold_boot_begin();
            emulator_frontend_cold_boot(fx.emu, stale, std::string(), ColdBootHooks{});
            fx.backend->on_cold_boot_done();
            fx.mgr->on_run();
            run_frames_then_break(fx, 3);
            const bool trace = fx.emu.trace_log().enabled();
            const bool offered = action_enabled(debug_item(dbg, "Step Back"));
            check("QRW-24", desc, trace && offered && fx.emu.rewind_enabled(),
                  fmt("trace=%d step_back_enabled=%d rewind=%d", trace, offered,
                      fx.emu.rewind_enabled()));
        }
    }

    {
        // QRW-25 - GH #400: trace off, then Rewind Buffer Size 0 (no ring): the
        // resize records no "trace on" (Emulator::resize_rewind_buffer(0) never
        // touches the trace), so a hard reset keeps the trace off.
        Fixture fx(MachineType::ZX48K, 0);
        const char* desc = "trace off, then Rewind Buffer Size 0, then a hard reset: "
                           "the trace stays off and there is no ring";
        if (!fx.ok) { check("QRW-25", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            const EmulatorConfig stale = fx.emu.config();
            DebuggerWindow* dbg = fx.dbg();
            fx.backend->set_trace_enabled(false);
            Modals m;
            m.spin_value = 0;
            if (QAction* sz = debug_sub_item(dbg, "Rewind", "Rewind Buffer Size..."))
                sz->trigger();
            fx.backend->on_cold_boot_begin();
            emulator_frontend_cold_boot(fx.emu, stale, std::string(), ColdBootHooks{});
            fx.backend->on_cold_boot_done();
            const bool trace = fx.emu.trace_log().enabled();
            check("QRW-25", desc, m.seen.size() == 1 && !trace && fx.emu.rewind_buffer() == nullptr,
                  fmt("dialogs=%zu trace=%d ring=%d", m.seen.size(), trace,
                      fx.emu.rewind_buffer() != nullptr));
        }
    }

    {
        // QRW-26 - GH #400: Enable Rewind, THEN the trace off (an explicit later
        // choice): a hard reset rebuilds the ring but honours the trace choice.
        Fixture fx(MachineType::ZX48K, 0);
        const char* desc = "Enable Rewind, then trace off, then a hard reset: rewind "
                           "on at the chosen size, trace off, Step Back not offered";
        if (!fx.ok) { check("QRW-26", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            const EmulatorConfig stale = fx.emu.config();
            DebuggerWindow* dbg = fx.dbg();
            QAction* enable = debug_sub_item(dbg, "Rewind", "Enable Rewind");
            if (enable) enable->trigger();
            run_frames_then_break(fx, 2);
            fx.backend->set_trace_enabled(false);
            fx.backend->on_cold_boot_begin();
            emulator_frontend_cold_boot(fx.emu, stale, std::string(), ColdBootHooks{});
            fx.backend->on_cold_boot_done();
            fx.mgr->on_run();
            run_frames_then_break(fx, 3);
            const bool trace = fx.emu.trace_log().enabled();
            const bool offered = action_enabled(debug_item(dbg, "Step Back"));
            check("QRW-26", desc,
                  fx.emu.rewind_enabled() && fx.emu.rewind_buffer() &&
                      fx.emu.rewind_buffer()->capacity() == 500 && !trace && !offered,
                  fmt("rewind=%d cap=%zu trace=%d step_back_enabled=%d", fx.emu.rewind_enabled(),
                      fx.emu.rewind_buffer() ? fx.emu.rewind_buffer()->capacity() : 0, trace,
                      offered));
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

    // QRW-20 — GH #278 WP3: a REWIND is the way out of a corrupt state, as it
    // always was. A rewind into a torn slot warns ("Rewind Failed"); a second
    // rewind, to an intact frame, is NOT refused for that corruption — no modal,
    // no resume prompt — and its successful restore clears the corruption. The
    // backend used to gate its rewind verbs on CTL-11, which would have answered
    // the second one with another "Rewind Failed" without trying.
    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "after a torn rewind, a rewind to an intact frame "
                           "succeeds with no modal and clears the corruption";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRW-20", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            RewindBuffer* rb = fx.emu.rewind_buffer();
            const uint32_t mmu_sentinel = Emulator::kStateSentinelMagic ^ 2u;
            size_t corrupted = 0;
            for (size_t i = 0; i + 1 < rb->depth(); ++i) {     // all but the newest
                uint8_t* d = rb->slot_data_for_test(i);
                for (size_t off = 0; off + 4 <= rb->snapshot_bytes(); ++off) {
                    uint32_t v;
                    std::memcpy(&v, d + off, 4);
                    if (v == mmu_sentinel) { d[off] ^= 0xFF; ++corrupted; break; }
                }
            }
            const uint32_t oldest = rb->oldest_frame_num(), newest = rb->newest_frame_num();
            Modals m1;
            fx.mgr->on_rewind_to_frame(oldest);                  // torn
            m1.timer.stop();
            const bool torn = m1.boxes() == 1 && !fx.emu.last_state_error().empty();
            Modals m2;
            fx.mgr->on_rewind_to_frame(newest);                  // intact
            m2.timer.stop();
            check("QRW-20", desc,
                  corrupted + 1 == rb->depth() && torn && m2.seen.empty() && fx.paused() &&
                      fx.emu.last_state_error().empty() &&
                      rewind_frame_label(fx.dbg()) != nullptr,
                  fmt("corrupted %zu/%zu first=%s second=%s err='%s' paused=%d",
                      corrupted, rb->depth(), m1.describe().c_str(), m2.describe().c_str(),
                      fx.emu.last_state_error().c_str(), fx.paused()));
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

    // QTR-05 — GH #278 WP3: the window switches the trace through the backend
    // (INS-13), which re-applies a client's trace request on a rebuilt machine
    // (CTL-12 rule 2). So a trace switched on from the window SURVIVES a hard
    // reset — the ball stays green and the new machine records — where the
    // Emulator switch it used to flip was reconstructed off.
    {
        Fixture f2(MachineType::ZX48K, 0, /*paused=*/false);
        const char* desc = "a trace switched on from the window survives a hard reset: "
                           "the ball stays green and the rebuilt machine records";
        if (!f2.ok) { check("QTR-05", desc, false, "fixture"); }
        else {
            f2.load(PROG, {0x18, 0xFE});
            f2.regs(PROG);
            f2.enable();
            DebuggerWindow* d2 = f2.dbg();
            QAction* en = debug_sub_item(d2, "Trace", "Enable Trace");
            const bool off0 = !f2.emu.trace_log().enabled();
            if (en) en->trigger();                         // the menu's Enable Trace
            const bool on = f2.emu.trace_log().enabled();
            f2.backend->on_cold_boot_begin();              // QtApp::cold_boot()
            emulator_frontend_cold_boot(f2.emu, f2.emu.config(), std::string(),
                                        ColdBootHooks{});
            f2.backend->on_cold_boot_done();
            const bool kept = f2.emu.trace_log().enabled();
            f2.tick();                                     // a frame on the new machine
            QPushButton* ball2 = button_where(d2, [](const QString& t) {
                return t == "Trace" || t.endsWith(": Trace");
            });
            check("QTR-05", desc,
                  en && off0 && on && kept && f2.emu.trace_log().size() > 0 &&
                      ball_colour(ball2) == qRgb(0x00, 0xC0, 0x00),
                  fmt("off before=%d on=%d kept after reset=%d entries=%zu",
                      off0, on, kept, f2.emu.trace_log().size()));
        }
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
        for (const char* id : {"QMAP-01", "QMAP-02", "QMAP-03", "QMAP-04", "QMAP-05", "QMAP-06", "QMAP-07", "QMAP-08", "QMAP-09", "QMAP-10"})
            check(id, "fixture", false);
        return;
    }
    fx.load(PROG, {0xCD, 0x00, 0x90, 0x18, 0xFE});        // CALL $9000 / JR $
    fx.load(0x9000, {0x21, 0x00, 0x91, 0x18, 0xFE});      // LD HL,$9100 / JR $
    fx.regs(PROG);
    fx.enable();
    fx.callstack_on_show();   // GH #498
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
                  fx.backend->symbols().size() == 3,
              fmt("modals=%s table=%zu", m.describe().c_str(), fx.backend->symbols().size()));
    }

    {
        // Z88DK, a file that cannot be read: "Load Failed", the table untouched.
        const std::string gone_path = write_file("gone_z88dk.map", "_x = $1234 ; addr\n");
        Modals bad;
        bad.file_path = QString::fromStdString(gone_path);
        bad.after_file_accept = [gone_path]() { QFile::remove(QString::fromStdString(gone_path)); };
        if (QAction* a = item_named(load_menu, "Z88DK Format...")) a->trigger();
        bad.timer.stop();
        const size_t after_bad = fx.backend->symbols().size();

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
                  fx.backend->symbols().size() == 0,
              fmt("bad: %s table after=%zu; none: %s table after=%zu", bad.describe().c_str(),
                  after_bad, none.describe().c_str(), fx.backend->symbols().size()));
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
        // QMAP-07 (GH #344): a file over the size cap is refused by both loaders with the
        // reason, not the bare "Could not load"; the table is untouched.
        const std::string big_z88dk = write_file("big_z88dk.map", "_x = $1234 ; addr\n");
        const std::string big_simple = write_file("big_simple.map", "X = $1234\n");
        std::error_code ec;
        std::filesystem::resize_file(big_z88dk, SOURCE_FILE_MAX_BYTES + 1, ec);    // sparse
        std::filesystem::resize_file(big_simple, SOURCE_FILE_MAX_BYTES + 1, ec);
        const size_t before = fx.backend->symbols().size();
        Modals z;
        z.file_path = QString::fromStdString(big_z88dk);
        if (QAction* a = item_named(load_menu, "Z88DK Format...")) a->trigger();
        z.timer.stop();
        Modals sm;
        sm.file_path = QString::fromStdString(big_simple);
        if (QAction* a = item_named(load_menu, "Simple Format (48K ROM)...")) a->trigger();
        sm.timer.stop();
        const Modals::Seen* zb = z.first("box");
        const Modals::Seen* sb = sm.first("box");
        const QString why = QString::fromStdString(source_file_problem(big_z88dk));
        check("QMAP-07",
              "Z88DK Format... and Simple Format... with a file over the size cap: \"Load "
              "Failed\" / \"Could not load MAP file:\\n<path>\\n\\n<why>\", the table untouched",
              !why.isEmpty() && zb && zb->title == "Load Failed" &&
                  zb->text == QStringLiteral("Could not load MAP file:\n%1\n\n%2")
                                  .arg(QString::fromStdString(big_z88dk), why) &&
                  sb && sb->title == "Load Failed" &&
                  sb->text == QStringLiteral("Could not load MAP file:\n%1\n\n%2")
                                  .arg(QString::fromStdString(big_simple), why) &&
                  fx.backend->symbols().size() == before,
              fmt("z: %s simple: %s", z.describe().c_str(), sm.describe().c_str()));
    }

    {
        // QMAP-09 (GH #427 stage 2a): the sjasmplus item of the same menu, on a file over the
        // size cap, is refused with the reason before the file is opened; the table is untouched.
        // (A FIFO is the same refusal, "not a regular file", and would block the GUI otherwise.)
        const std::string big_equ = write_file("big_sjasmplus.sym", "X: EQU 0x00001234\n");
        std::error_code ec;
        std::filesystem::resize_file(big_equ, SOURCE_FILE_MAX_BYTES + 1, ec);    // sparse
        const size_t before = fx.backend->symbols().size();
        Modals e;
        e.file_path = QString::fromStdString(big_equ);
        if (QAction* a = item_named(load_menu, "sjasmplus Format (--sym, --exp)...")) a->trigger();
        e.timer.stop();
        const Modals::Seen* eb = e.first("box");
        const QString why = QString::fromStdString(source_file_problem(big_equ));
        check("QMAP-09",
              "sjasmplus Format... with a file over the size cap: \"Load Failed\" / \"Could not load "
              "MAP file:\\n<path>\\n\\n<why>\", the table untouched",
              !why.isEmpty() && eb && eb->title == "Load Failed" &&
                  eb->text == QStringLiteral("Could not load MAP file:\n%1\n\n%2")
                                  .arg(QString::fromStdString(big_equ), why) &&
                  fx.backend->symbols().size() == before,
              fmt("e: %s", e.describe().c_str()));
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
        fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x8000);
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

    {
        // QMAP-08 (GH #424): QMAP-03's setup with C-module symbols (module main_c). The panels show the C names;
        // the copy keeps the assembler name, so it still assembles (D4). QMAP-03 is the module-less control.
        fx.mgr->breakpoints().clear_all();
        fx.regs(PROG);
        const std::string path = write_file("prog3.map",
            "_main   = $8000 ; addr, public, , main_c, code_compiler, main.c:1\n"
            "_sub    = $9000 ; addr, public, , main_c, code_compiler, main.c:5\n"
            "_buffer = $9100 ; addr, public, , main_c, bss_compiler, main.c:2\n");
        Modals m;
        m.file_path = QString::fromStdString(path);
        if (QAction* a = item_named(load_menu, "Z88DK Format...")) a->trigger();
        m.timer.stop();

        DisasmPanel* dp = dbg->disasm_panel();
        if (auto* sb = dp->findChild<QScrollBar*>()) sb->setValue(0x9000);
        QApplication::processEvents();
        dp->select_all_visible();
        const QString asm_text = dp->selection_text(disasm_text::CopyFormat::AsmOnly);
        const QString with_addr = dp->selection_text(disasm_text::CopyFormat::WithAddresses);
        dp->clear_selection();

        fx.mgr->on_step_into();
        const QString target = table_cell(dbg->callstack_panel(), 0, 3);

        fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x8000);
        BreakpointPanel* bp = dbg->breakpoint_panel();
        QString sym_col;
        if (auto* t = bp->findChild<QTableWidget*>())
            for (int r = 0; r < t->rowCount(); ++r)
                if (t->item(r, 2) && t->item(r, 2)->text() == "$8000" && t->item(r, 3))
                    sym_col = t->item(r, 3)->text();

        check("QMAP-08",
              "C-module symbols reach the Call Stack (target `sub`) and the Breakpoints Symbol column (`main`) by "
              "their C names, and the Disassembly copy keeps the assembler name `_buffer`",
              asm_text.contains("LD HL,_buffer") && with_addr.contains("LD HL,_buffer") &&
                  !asm_text.contains("LD HL,buffer") && target == "sub" && sym_col == "main",
              fmt("asm has _buffer=%d target='%s' symbol='%s'", asm_text.contains("LD HL,_buffer"),
                  s(target).c_str(), s(sym_col).c_str()));
    }

    {
        // GH #390 -- the third item, sjasmplus Format (--sym, --exp)..., on a
        // real sjasmplus file (test/fixtures/sjasmequ/README.md): 16 entries
        // name 14 addresses, which is what the box counts (symbols().size(),
        // as for the other two items).
        const std::string path = JNEXT_SJASMEQU_DIR "/equlabels.sym";
        Modals ok;
        ok.file_path = QString::fromStdString(path);
        QAction* sjasm = item_named(load_menu, "sjasmplus Format (--sym, --exp)...");
        if (sjasm) sjasm->trigger();
        ok.timer.stop();
        const auto gw = fx.backend->lookup_name("gfx.draw");
        const auto bank = fx.backend->lookup(0xC000);
        const size_t kept = fx.backend->symbols().size();

        // A file that cannot be read: "Load Failed", the table untouched.
        const std::string gone_path = write_file("gone_sjasmplus.sym", "X: EQU 0x00001234\n");
        Modals bad;
        bad.file_path = QString::fromStdString(gone_path);
        bad.after_file_accept = [gone_path]() { QFile::remove(QString::fromStdString(gone_path)); };
        if (sjasm) sjasm->trigger();
        bad.timer.stop();
        const size_t after_bad = fx.backend->symbols().size();

        // A readable file with no entry is a successful load of zero, as for the
        // other two items (QMAP-02, QMAP-04): sjasmplus writes a 0-byte --exp when
        // nothing is EXPORTed. It replaces the table, so this one runs last.
        const std::string empty_path = write_file("empty_sjasmplus.exp", "");
        Modals empty;
        empty.file_path = QString::fromStdString(empty_path);
        if (sjasm) sjasm->trigger();
        empty.timer.stop();

        const Modals::Seen* okb = ok.first("box");
        const Modals::Seen* badb = bad.first("box");
        const Modals::Seen* emptyb = empty.first("box");
        const Modals::Seen* dlg = ok.first("file");
        check("QMAP-05",
              "sjasmplus Format...: a file dialog for *.sym and *.exp; a real --sym loads (\"MAP Loaded\" / "
              "\"Loaded 14 symbols\"), dotted names resolve and the first label at an address names it; a "
              "file that cannot be read gives \"Load Failed\" and keeps the table; a 0-byte --exp loads "
              "zero (\"MAP Loaded\" / \"Loaded 0 symbols\") and empties it",
              sjasm && dlg && dlg->title == "Load sjasmplus Symbol File" &&
                  dlg->filters == "sjasmplus Symbol Files (*.sym *.exp);;All Files (*)" &&
                  okb && okb->title == "MAP Loaded" &&
                  okb->text == QStringLiteral("Loaded 14 symbols from:\n%1")
                                   .arg(QString::fromStdString(path)) &&
                  gw == std::optional<uint16_t>(0x8016) && bank == std::optional<std::string>("banked") &&
                  kept == 14 && badb && badb->title == "Load Failed" &&
                  badb->text == QStringLiteral("Could not load MAP file:\n%1")
                                    .arg(QString::fromStdString(gone_path)) &&
                  after_bad == 14 && emptyb && emptyb->title == "MAP Loaded" &&
                  emptyb->text == QStringLiteral("Loaded 0 symbols from:\n%1")
                                      .arg(QString::fromStdString(empty_path)) &&
                  fx.backend->symbols().size() == 0,
              fmt("ok: %s bad: %s table after bad=%zu; empty: %s table after=%zu", ok.describe().c_str(),
                  bad.describe().c_str(), after_bad, empty.describe().c_str(),
                  fx.backend->symbols().size()));
    }

    {
        // QMAP-10 (GH #387): the box of each of the four symbol loads counts the file it loaded, not the
        // user's labels that share the table the panels read: with one user label in the store,
        // symbols() is one more than the box says. Z88DK (3 `; addr` symbols), Simple (2), sjasmplus
        // (the fixture's 14 addresses, QMAP-05) and NextBuild Memory.txt (2 labels).
        fx.backend->clear_symbols();
        const bool user = fx.backend->set_user_label(jnext::dbg::MemSpace::page(4), 0x7000, "mine") ==
                          jnext::dbg::Result::Ok;
        struct Case { const char* what; std::string path; const char* title; unsigned n; };
        const Case cases[] = {
            {"Z88DK", write_file("q9_z88dk.map", "_a = $8000 ; addr, public\n_b = $8001 ; addr, public\n_c = $8002 ; addr, public\n"), "MAP Loaded", 3},
            {"Simple", write_file("q9_simple.map", "QA = $8000\nQB = $8001\n"), "MAP Loaded", 2},
            {"sjasmplus", std::string(JNEXT_SJASMEQU_DIR "/equlabels.sym"), "MAP Loaded", 14},
            {"NextBuild", write_file("q9.Memory.txt", "8000: ._Main\nC000: .banked\n"), "Symbols Loaded", 2},
        };
        std::string bad;
        for (const Case& c : cases) {
            Modals m;
            m.file_path = QString::fromStdString(c.path);
            if (std::string(c.what) == "NextBuild") fx.mgr->on_load_nextbuild_memory();
            else if (QAction* a = item_named(load_menu, std::string(c.what) == "Z88DK" ? "Z88DK Format..."
                                                       : std::string(c.what) == "Simple" ? "Simple Format (48K ROM)..."
                                                                                         : "sjasmplus Format (--sym, --exp)..."))
                a->trigger();
            m.timer.stop();
            const Modals::Seen* box = m.first("box");
            if (!box || box->title != c.title ||
                box->text != QStringLiteral("Loaded %1 symbols from:\n%2").arg(c.n).arg(QString::fromStdString(c.path)) ||
                fx.backend->symbols().size() != c.n + 1 || fx.backend->toolchain_symbols().size() != c.n ||
                !fx.backend->annotations().find_name("mine"))
                bad += std::string(" ") + c.what + "(" + (box ? s(box->text) : std::string("no box")) + " table=" +
                       std::to_string(fx.backend->symbols().size()) + ")";
        }
        check("QMAP-10",
              "with one user label in the store, each of the four symbol loads (Z88DK, Simple, sjasmplus, "
              "NextBuild Memory.txt) reports the file's own count in its box, and the table the panels read is "
              "one label bigger",
              user && bad.empty(), bad);
        fx.backend->clear_symbols();
        fx.backend->remove_user_label(jnext::dbg::MemSpace::page(4), 0x7000);
    }

    {
        // GH #390 (R-1) -- every Map > Load MAP File item refreshes the panels.
        // The Breakpoints panel computes its Symbol column in refresh(), so a
        // load that did not refresh left the PREVIOUS name on a paused machine.
        // A machine of its own, paused, with an Execute breakpoint at $8000
        // before the loads; the cell is read right after each load returns,
        // with no step and no other refresh, against the backend's own name.
        Fixture f6;
        if (f6.ok) {
            f6.load(PROG, {0x18, 0xFE});                      // JR $
            f6.regs(PROG);
            f6.enable();
            DebuggerWindow* d6 = f6.dbg();
            QMenu* menu6 = submenu_named(menu_named(d6->menuBar(), "Map"), "Load MAP File");
            f6.mgr->breakpoints().add(BreakpointModel::Execute, 0x8000);
            auto cell = [&]() {
                QString out = QStringLiteral("<no row>");
                if (auto* t = d6->breakpoint_panel()->findChild<QTableWidget*>())
                    for (int r = 0; r < t->rowCount(); ++r)
                        if (t->item(r, 2) && t->item(r, 2)->text() == "$8000" && t->item(r, 3))
                            out = t->item(r, 3)->text();
                return out;
            };
            auto backend_name = [&]() {
                const auto n = f6.backend->lookup(0x8000);
                return n ? QString::fromStdString(*n) : QStringLiteral("<none>");
            };
            auto load = [&](const char* item, const std::string& file) {
                Modals m;
                m.file_path = QString::fromStdString(file);
                if (QAction* a = item_named(menu6, item)) a->trigger();
                m.timer.stop();
            };
            const QString before = cell();
            load("Z88DK Format...", write_file("q6_z88dk.map", "_qz = $8000 ; addr, public\n"));
            const QString z = cell(), zb = backend_name();
            load("Simple Format (48K ROM)...", write_file("q6_simple.map", "QS = $8000\n"));
            const QString sm = cell(), sb = backend_name();
            load("sjasmplus Format (--sym, --exp)...", std::string(JNEXT_SJASMEQU_DIR "/equlabels.sym"));
            const QString j = cell(), jb = backend_name();
            check("QMAP-06",
                  "each Map > Load MAP File item (Z88DK, Simple, sjasmplus) refreshes the panels: the "
                  "Breakpoints Symbol cell of a breakpoint at $8000 shows the new name at once",
                  before.isEmpty() && z == "_qz" && zb == "_qz" && sm == "QS" && sb == "QS" &&
                      j == "main" && jb == "main",
                  fmt("before='%s' z88dk='%s'/'%s' simple='%s'/'%s' sjasmplus='%s'/'%s'",
                      s(before).c_str(), s(z).c_str(), s(zb).c_str(), s(sm).c_str(), s(sb).c_str(),
                      s(j).c_str(), s(jb).c_str()));
        } else {
            check("QMAP-06", "fixture", false);
        }
    }
}


// ── QRF — why a rewind was refused (§4.2a wall, owner request) ───────────────
//
// Every benign refusal of Step Back / Frame Back / the slider says why: the
// backend logs `REWIND REFUSED: <verb> refused: <reason>. See the user guide:
// …` to every listener, and the debugger window shows it in its status bar
// for DebuggerWindow::kRewindRefusalMs. Each row asserts BOTH: the window's
// status text and the line a second client's listener received.
struct LogCatcher final : jnext::dbg::Listener {
    std::vector<std::string> lines;
    void on_paused(const jnext::dbg::PausedInfo&) override {}
    void on_resumed(jnext::dbg::ClientId) override {}
    void on_reset(jnext::dbg::ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(jnext::dbg::LogLevel, const std::string& t) override { lines.push_back(t); }
    bool has(const std::string& needle) const {
        for (const auto& l : lines) if (l.find(needle) != std::string::npos) return true;
        return false;
    }
};

struct RefusalProbe {
    Fixture&             fx;
    LogCatcher           log;
    jnext::dbg::ClientId cid = jnext::dbg::CLIENT_NONE;
    explicit RefusalProbe(Fixture& f) : fx(f) {
        jnext::dbg::ClientInfo ci;
        ci.name     = "qrf";
        ci.observer = true;
        cid = fx.backend->attach(ci).value;
        fx.backend->set_listener(cid, &log);
    }
    ~RefusalProbe() {
        fx.backend->set_listener(cid, nullptr);
        fx.backend->detach(cid);
    }
    QString status() const { return status_of(fx.dbg()); }
    /// The window shows `want`, and the log line carries it after the tag.
    bool shows(const QString& want) const {
        return status().startsWith(want) &&
               status().contains(QStringLiteral("See the user guide: Debugger > Functions "
                                                "> Backward execution (rewind)")) &&
               log.has("REWIND REFUSED: " + s(want));
    }
    std::string detail() const {
        return "status='" + s(status()) + "' log=" + std::to_string(log.lines.size()) +
               (log.lines.empty() ? std::string() : " last='" + log.lines.back() + "'");
    }
};

static void test_rewind_refusal_ui() {
    set_group("QRF");
    {
        Fixture fx(MachineType::ZX48K, 0);   // no ring at all
        const char* desc = "Step Back with rewind off: the status bar and the log say "
                           "\"Step Back refused: rewind is off\"";
        if (!fx.ok) { check("QRF-01", desc, false, "fixture"); }
        else {
            fx.enable();
            RefusalProbe p(fx);
            fx.mgr->on_step_back();
            check("QRF-01", desc,
                  p.shows(QStringLiteral("Step Back refused: rewind is off")), p.detail());
        }
    }
    {
        Fixture fx(MachineType::ZX48K, 10);  // a ring, no frame run yet
        const char* desc = "Step Back with an empty ring: \"…the rewind buffer holds no "
                           "frames yet\"";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRF-02", desc, false, "fixture"); }
        else {
            fx.enable();
            RefusalProbe p(fx);
            fx.mgr->on_step_back();
            check("QRF-02", desc,
                  fx.emu.rewind_buffer()->empty() &&
                      p.shows(QStringLiteral("Step Back refused: the rewind buffer holds no frames yet")),
                  p.detail());
        }
    }
    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "Step Back with the trace off: \"…the instruction trace is off\"";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRF-03", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            fx.emu.trace_log().set_enabled(false);
            RefusalProbe p(fx);
            fx.mgr->on_step_back();
            check("QRF-03", desc,
                  p.shows(QStringLiteral("Step Back refused: the instruction trace is off")),
                  p.detail());
        }
    }
    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "Step Back with an empty trace: \"…the instruction trace is empty\"";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRF-04", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            fx.emu.trace_log().clear();
            RefusalProbe p(fx);
            fx.mgr->on_step_back();
            check("QRF-04", desc,
                  p.shows(QStringLiteral("Step Back refused: the instruction trace is empty")),
                  p.detail());
        }
    }
    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "Step Back while an RZX plays: \"…an RZX recording is playing\"";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRF-05", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            fx.emu.rzx_player().start(RzxRecording{});
            RefusalProbe p(fx);
            fx.mgr->on_step_back();
            check("QRF-05", desc,
                  p.shows(QStringLiteral("Step Back refused: an RZX recording is playing")),
                  p.detail());
            fx.emu.rzx_player().stop();
        }
    }
    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "Frame Back while an RZX is being recorded: \"Rewind to frame N "
                           "refused: an RZX recording is being made\"";
        if (!fx.ok || !fx.emu.rewind_buffer() || !g_tmp) { check("QRF-09", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            const uint32_t f = fx.emu.rewind_buffer()->oldest_frame_num();
            const bool rec = fx.emu.rzx_recorder().start(
                g_tmp->filePath(QStringLiteral("qrf-09.rzx")).toStdString());
            RefusalProbe p(fx);
            fx.mgr->on_rewind_to_frame(f);
            const QString want = QStringLiteral("Rewind to frame %1 refused: an RZX recording "
                                                "is being made").arg(f);
            check("QRF-09", desc, rec && p.shows(want), p.detail());
            fx.emu.rzx_recorder().stop();
        }
    }
    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "the slider / Frame Back to a frame outside the ring: "
                           "\"Rewind to frame N refused: frame N is not in the rewind buffer\"";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRF-06", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            const uint32_t f = fx.emu.rewind_buffer()->newest_frame_num() + 50;
            RefusalProbe p(fx);
            fx.mgr->on_rewind_to_frame(f);
            const QString want = QStringLiteral("Rewind to frame %1 refused: frame %1 is not in "
                                                "the rewind buffer").arg(f);
            check("QRF-06", desc, p.shows(want), p.detail());
        }
    }
    {
        // The wall: a change at C mid-frame, two steps, Step Back past C.
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "Step Back across a debugger change: \"Step Back refused: it would "
                           "undo a change you made from the debugger in frame N - use Frame "
                           "Back (or the slider) to that frame's start, which undoes the "
                           "change, or carry on forward\"";
        if (!fx.ok || !fx.emu.rewind_buffer()) {
            check("QRF-07", desc, false, "fixture");
            check("QRF-08", "the refusal holds the status bar", false, "fixture");
            check("QRF-10", "a later refusal names its own reason", false, "fixture");
        } else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            fx.mgr->on_step_into();
            fx.mgr->on_step_into();
            RefusalProbe p(fx);
            const uint32_t frame = fx.backend->time().frame;
            const uint8_t  v     = 0x5A;
            fx.backend->poke(p.cid, jnext::dbg::MemSpace::cpu(), 0xC000, 1, &v);
            fx.mgr->on_step_into();
            fx.mgr->on_step_into();
            const uint16_t pc0 = fx.pc();
            fx.mgr->on_step_back();
            const QString want = QStringLiteral("Step Back refused: it would undo a change you "
                                                "made from the debugger in frame %1 - use "
                                                "Frame Back (or the slider) to that frame's "
                                                "start, which undoes the change, or carry on "
                                                "forward")
                                     .arg(frame);
            check("QRF-07", desc, p.shows(want) && fx.pc() == pc0, p.detail());
            // The rewind status line is refreshed on every tick; it must not
            // overwrite the refusal while it is on show.
            fx.dbg()->refresh_panels();
            check("QRF-08", "a panel refresh (the rewind status line) does not overwrite "
                            "the refusal while it is on show",
                  status_of(fx.dbg()).startsWith(want), p.detail());
            // The crossing is remembered per call, not between calls: the trace
            // switched off after a crossing refusal, the next Step Back's reason
            // is the trace, not the stale crossing.
            fx.emu.trace_log().set_enabled(false);
            fx.mgr->on_step_back();
            const QString want2 =
                QStringLiteral("Step Back refused: the instruction trace is off");
            check("QRF-10", "a crossing refusal, then the trace switched off: the next Step "
                            "Back says \"…the instruction trace is off\", not the crossing",
                  p.shows(want2) && !status_of(fx.dbg()).contains("would undo"), p.detail());
        }
    }
    {
        // A frame inside the ring's range with no snapshot: it ran while
        // snapshotting was paused (Enable Rewind off).
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "the slider / Frame Back to a frame the ring has no snapshot of "
                           "(a gap): \"Rewind to frame N refused: frame N has no snapshot in "
                           "the rewind buffer\"";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRF-11", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 3);
            fx.backend->set_rewind_enabled(false);
            run_frames_then_break(fx, 2);
            fx.backend->set_rewind_enabled(true);
            run_frames_then_break(fx, 3);
            const RewindBuffer* rb = fx.emu.rewind_buffer();
            uint32_t hole = 0;
            bool     found = false;
            for (uint32_t f = rb->oldest_frame_num(); f <= rb->newest_frame_num() && !found; ++f)
                if (rb->frame_cycle_for(f) == UINT64_MAX) { hole = f; found = true; }
            RefusalProbe p(fx);
            fx.mgr->on_rewind_to_frame(hole);
            const QString want = QStringLiteral("Rewind to frame %1 refused: frame %1 has no "
                                                "snapshot in the rewind buffer").arg(hole);
            check("QRF-11", desc, found && p.shows(want),
                  "hole=" + std::to_string(hole) + " found=" + std::to_string(found) + " " +
                      p.detail());
        }
    }
    {
        // A refusal on show describes the verb that was refused. The next rewind
        // verb clears it, so a SUCCESS does not leave the old reason beside it
        // for the rest of the hold: once for Frame Back, once for Step Back.
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "a refusal on show, then a Frame Back and a Step Back that "
                           "succeed: each success clears the refusal, and the rewind status "
                           "line comes back";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QRF-12", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            const RewindBuffer* rb  = fx.emu.rewind_buffer();
            const uint32_t      out = rb->newest_frame_num() + 50;
            RefusalProbe p(fx);
            fx.mgr->on_rewind_to_frame(out);
            const bool    shown1 = status_of(fx.dbg()).contains("refused");
            fx.mgr->on_rewind_to_frame(rb->oldest_frame_num());
            const QString after_fb = status_of(fx.dbg());
            fx.mgr->on_rewind_to_frame(out);
            const bool    shown2 = status_of(fx.dbg()).contains("refused");
            fx.mgr->on_step_into();
            fx.mgr->on_step_into();
            const uint16_t pc_before = fx.pc();
            fx.mgr->on_step_back();
            const QString after_sb = status_of(fx.dbg());
            check("QRF-12", desc,
                  shown1 && shown2 && !after_fb.contains("refused") &&
                      after_fb.startsWith(QStringLiteral("\u23EE Rewound")) &&
                      fx.pc() != pc_before && !after_sb.contains("refused") &&
                      after_sb.startsWith(QStringLiteral("\u23EE")),
                  "after Frame Back '" + s(after_fb) + "' after Step Back '" + s(after_sb) +
                      "' shown=" + std::to_string(shown1) + std::to_string(shown2));
        }
    }
}

// ===========================================================================
// QANN -- GH #387 (E23-24): the user's labels in the other views, and the Map menu's annotation items.
//
// Oracle: design 10.3 (every consumer reads the one reader, by the memory that supplies the address),
// 10.5 (the file: Open replaces the store whole or not at all; Save writes the canonical bytes) and
// D11 (unsaved changes are asked about before an Open over them). NR 0x56 picks the page in slot 6.
// ===========================================================================
static std::string slurp_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void test_annotations_ui() {
    set_group("QANN");
    const jnext::dbg::MemSpace P10 = jnext::dbg::MemSpace::page(0x10);
    {   // QANN-05: the Call Stack target and the Breakpoints Symbol column
        Fixture fx(MachineType::ZXN_ISSUE2);
        if (!fx.ok) { check("QANN-05", "fixture", false); return; }
        fx.emu.nextreg().write(0x56, 0x10);
        fx.load(0x8000, {0xCD, 0x00, 0xC0, 0x18, 0xFE});   // CALL $C000 / JR $
        fx.load(0xC000, {0x00, 0x18, 0xFE});
        fx.regs(0x8000);
        fx.backend->set_user_label(P10, 0xC000, "banked");
        fx.enable();
        fx.callstack_on_show();   // GH #498
        fx.mgr->breakpoints().add(BreakpointModel::Execute, 0xC000);
        fx.mgr->on_step_into();
        DebuggerWindow* dbg = fx.dbg();
        const QString target = table_cell(dbg->callstack_panel(), 0, 3);
        auto bp_symbol = [&]() {
            QString out = QStringLiteral("<no row>");
            if (auto* t = dbg->breakpoint_panel()->findChild<QTableWidget*>())
                for (int r = 0; r < t->rowCount(); ++r)
                    if (t->item(r, 2) && t->item(r, 2)->text() == "$C000" && t->item(r, 3)) out = t->item(r, 3)->text();
            return out;
        };
        const QString sym10 = bp_symbol();
        fx.emu.nextreg().write(0x56, 0x11);   // behind the backend's back: the stamp is unchanged, so redraw by hand
        dbg->refresh_panels();
        const QString sym11 = bp_symbol();
        check("QANN-05", "the Call Stack target of a CALL $C000 and the Breakpoints Symbol cell of a breakpoint there "
                         "show the user label 'banked' (page 0x10 mapped) and the breakpoint's cell is empty with 0x11",
              target == "banked" && sym10 == "banked" && sym11.isEmpty(),
              fmt("target='%s' bp10='%s' bp11='%s'", s(target).c_str(), s(sym10).c_str(), s(sym11).c_str()));
    }
    // The Map menu
    const std::string good = write_file("ann-good.jdp", "jnext-annotations 1\nlabel P10 $C000 banked\ncomment P10 $C000 hi\n");
    const std::string bad = write_file("ann-bad.jdp", "jnext-annotations 1\nlabel P10 $C000 ok\nlabel P10 $C001 !\n");
    {   // QANN-12
        Fixture fx(MachineType::ZXN_ISSUE2);
        if (!fx.ok) { check("QANN-12", "fixture", false); return; }
        fx.emu.nextreg().write(0x56, 0x10);
        fx.regs(0x8000);
        fx.enable();
        QMenu* map = menu_named(fx.dbg()->menuBar(), "Map");
        // three items, mnemonics unique across the whole popup
        QStringList letters;
        for (QAction* a : map->actions()) {
            const int at = a->text().indexOf(QLatin1Char('&'));
            if (at >= 0 && at + 1 < a->text().size()) letters << a->text().at(at + 1).toUpper();
        }
        QStringList uniq = letters; uniq.removeDuplicates();
        QAction* open = item_named(map, "Open Annotations...");
        QAction* save = item_named(map, "Save Annotations");
        QAction* save_as = item_named(map, "Save Annotations As...");
        const bool items = open && save && save_as && uniq.size() == letters.size();
        // Open loads the file and the Disassembly shows it
        Modals mo;
        mo.file_path = QString::fromStdString(good);
        if (open) open->trigger();
        mo.timer.stop();
        DisasmPanel* dp = fx.dbg()->disasm_panel();
        if (auto* sb = dp->findChild<QScrollBar*>()) sb->setValue(0xC000);
        QApplication::processEvents();
        const bool opened = fx.backend->annotations().find(P10, 0xC000) &&
                            fx.backend->annotations().find(P10, 0xC000)->label == "banked" &&
                            dp->line_label(0) == "banked" && dp->line_comment(0) == "; hi" && !fx.mgr->annotations_unsaved() &&
                            mo.first("file");
        // Save with a current file writes the canonical bytes, with no dialog
        fx.backend->set_user_label(P10, 0xC010, "second");
        const bool dirty = fx.mgr->annotations_unsaved();
        Modals ms;
        ms.expect_modals = false;   // Save with a current file opens no dialog: one that does is rejected and FAILs the row
        if (save) save->trigger();
        ms.timer.stop();
        const std::string written = slurp_file(good);
        const bool saved = ms.seen.empty() && !ms.stuck && !fx.mgr->annotations_unsaved() &&
                           written == "jnext-annotations 1\nlabel P10 $C000 banked\ncomment P10 $C000 hi\nlabel P10 $C010 second\n";
        // a failed Open: the box names the line, the store is as it was
        const auto before = fx.backend->annotations();
        Modals mb;
        mb.file_path = QString::fromStdString(bad);
        if (open) open->trigger();
        mb.timer.stop();
        const Modals::Seen* box = mb.first("box");
        const bool failed = box && box->title == "Open Failed" && box->text.contains("line 3:") &&
                            fx.backend->annotations() == before;
        // Save with no current file asks where (Save As), and appends .jdp
        Fixture f2(MachineType::ZXN_ISSUE2);
        f2.regs(0x8000);
        f2.enable();
        f2.backend->set_user_label(P10, 0xC000, "x");
        Modals mn;
        mn.file_path = g_tmp->filePath(QStringLiteral("ann-new"));
        if (QAction* sv = item_named(menu_named(f2.dbg()->menuBar(), "Map"), "Save Annotations")) sv->trigger();
        mn.timer.stop();
        const std::string created = slurp_file(g_tmp->filePath(QStringLiteral("ann-new.jdp")).toStdString());
        const bool asked = mn.first("file") && created == "jnext-annotations 1\nlabel P10 $C000 x\n" &&
                           !f2.mgr->annotations_unsaved() &&
                           f2.mgr->annotations_file().endsWith(QStringLiteral("ann-new.jdp"));
        check("QANN-12", "Map menu: Open, Save and Save As with unique mnemonics; Open loads the file and the Disassembly "
                         "shows its label and comment; Save with a current file writes the canonical bytes without a "
                         "dialog; a file that does not parse gives an 'Open Failed' box naming the line and keeps the "
                         "store; Save with no file asks where and adds .jdp",
              items && opened && dirty && saved && failed && asked,
              fmt("items=%d opened=%d dirty=%d saved=%d failed=%d asked=%d dialogs-on-save=%s", items, opened, dirty, saved, failed,
                  asked, ms.describe().c_str()));
    }
    {   // QANN-18: Open Annotations refreshes the panels at once, like every Map load (the GH #390 R-1 row)
        Fixture fx(MachineType::ZXN_ISSUE2);
        if (!fx.ok) { check("QANN-18", "fixture", false); return; }
        fx.emu.nextreg().write(0x56, 0x10);
        fx.regs(0x8000);
        fx.enable();
        fx.mgr->breakpoints().add(BreakpointModel::Execute, 0xC000);
        DebuggerWindow* d = fx.dbg();
        auto bp_symbol = [&]() {
            QString out = QStringLiteral("<no row>");
            if (auto* t = d->breakpoint_panel()->findChild<QTableWidget*>())
                for (int r = 0; r < t->rowCount(); ++r)
                    if (t->item(r, 2) && t->item(r, 2)->text() == "$C000" && t->item(r, 3)) out = t->item(r, 3)->text();
            return out;
        };
        const QString before = bp_symbol();
        Modals mo;
        mo.file_path = QString::fromStdString(good);
        if (QAction* open = item_named(menu_named(d->menuBar(), "Map"), "Open Annotations...")) open->trigger();
        mo.timer.stop();
        const QString after = bp_symbol();   // no tick, no other refresh
        check("QANN-18", "Map > Open Annotations refreshes the panels at once: the Breakpoints Symbol cell of a breakpoint "
                         "at $C000 shows the opened file's label right after the dialog, with no tick",
              before.isEmpty() && after == "banked" && !mo.stuck,
              fmt("before='%s' after='%s'", s(before).c_str(), s(after).c_str()));
    }
    {   // QANN-13
        Fixture fx(MachineType::ZXN_ISSUE2);
        if (!fx.ok) { check("QANN-13", "fixture", false); return; }
        fx.regs(0x8000);
        fx.enable();
        QAction* open = item_named(menu_named(fx.dbg()->menuBar(), "Map"), "Open Annotations...");
        fx.backend->set_user_label(P10, 0xC020, "mine");
        const auto before = fx.backend->annotations();
        Modals cancel;
        cancel.box_answer = QMessageBox::Cancel;
        cancel.file_path = QString::fromStdString(good);
        if (open) open->trigger();
        cancel.timer.stop();
        const Modals::Seen* ask = cancel.first("box");
        const bool cancelled = ask && ask->title == "Unsaved Annotations" && !cancel.first("file") &&
                               fx.backend->annotations() == before;
        Modals discard;
        discard.box_answer = QMessageBox::Discard;
        discard.file_path = QString::fromStdString(good);
        if (open) open->trigger();
        discard.timer.stop();
        const bool discarded = discard.first("box") && discard.first("file") && !fx.backend->annotations().find(P10, 0xC020) &&
                               fx.backend->annotations().find(P10, 0xC000);
        check("QANN-13", "Open over unsaved changes asks Save / Discard / Cancel first: Cancel opens no file dialog "
                         "and keeps the store; Discard goes on to open and replaces it",
              cancelled && discarded, fmt("cancelled=%d discarded=%d", cancelled, discarded));
    }
}

// ===========================================================================
// QREX -- Map > Import and Export (GH #389 stage C): skoolkit control files and Ghidra symbol files, in and out of the
// debugger. The dialogs are the REAL QFileDialog (several files / a directory), the answer a message box.
// ===========================================================================
static void test_reexport_ui() {
    set_group("QREX");
    const jnext::dbg::MemSpace P10 = jnext::dbg::MemSpace::page(0x10);
    {   // QREX-01: the menu
        Fixture fx(MachineType::ZXN_ISSUE2);
        if (!fx.ok) { check("QREX-01", "fixture", false); return; }
        fx.regs(0x8000);
        fx.enable();
        QMenu* map = menu_named(fx.dbg()->menuBar(), "Map");
        QMenu* imp = submenu_named(map, "Import");
        QMenu* exp = submenu_named(map, "Export");
        auto unique = [](QMenu* m) {
            QStringList letters;
            for (QAction* a : m->actions()) {
                const int at = a->text().indexOf(QLatin1Char('&'));
                if (at >= 0 && at + 1 < a->text().size()) letters << a->text().at(at + 1).toUpper();
            }
            QStringList u = letters; u.removeDuplicates();
            return !letters.isEmpty() && u.size() == letters.size();
        };
        check("QREX-01", "Map has an Import and an Export submenu, each with 'skoolkit Control Files...' and 'Ghidra Symbols...'; "
                         "the mnemonics are unique in the Map popup and in each submenu",
              imp && exp && item_named(imp, "skoolkit Control Files...") && item_named(imp, "Ghidra Symbols...") &&
                  item_named(exp, "skoolkit Control Files...") && item_named(exp, "Ghidra Symbols...") &&
                  unique(map) && unique(imp) && unique(exp));
    }
    {   // QREX-02: Export writes what the backend verb writes
        Fixture fx(MachineType::ZXN_ISSUE2);
        if (!fx.ok) { check("QREX-02", "fixture", false); return; }
        fx.emu.nextreg().write(0x56, 0x10);
        fx.regs(0x8000);
        fx.backend->set_user_label(P10, 0xC000, "banked");
        fx.backend->set_user_comment(P10, 0xC000, "a note");
        fx.enable();
        QMenu* exp = submenu_named(menu_named(fx.dbg()->menuBar(), "Map"), "Export");
        const QString d_sk = g_tmp->filePath(QStringLiteral("qrex-sk")), d_gh = g_tmp->filePath(QStringLiteral("qrex-gh"));
        QDir().mkpath(d_sk); QDir().mkpath(d_gh);
        Modals m1, m2;
        m1.dir_path = d_sk;
        if (QAction* a = item_named(exp, "skoolkit Control Files...")) a->trigger();
        m1.timer.stop();
        m2.dir_path = d_gh;
        if (QAction* a = item_named(exp, "Ghidra Symbols...")) a->trigger();
        m2.timer.stop();
        const std::string ref_sk = g_tmp->filePath(QStringLiteral("qrex-ref-sk")).toStdString();
        const std::string ref_gh = g_tmp->filePath(QStringLiteral("qrex-ref-gh")).toStdString();
        jnext::dbg::ReReport rs, rg;
        std::string why;
        fx.backend->export_skoolkit(ref_sk, &rs, &why);
        fx.backend->export_ghidra(ref_gh, &rg, &why);
        const std::string a1 = slurp_file((d_sk + "/C000-P10.ctl").toStdString()), a2 = slurp_file(ref_sk + "/C000-P10.ctl");
        const std::string b1 = slurp_file((d_gh + "/P10.ghidra.txt").toStdString()), b2 = slurp_file(ref_gh + "/P10.ghidra.txt");
        const Modals::Seen* box1 = m1.first("box");
        const Modals::Seen* box2 = m2.first("box");
        check("QREX-02", "Export > skoolkit and > Ghidra write the directory the dialog chose with exactly the bytes the backend "
                         "verbs write, and the box reports the counts",
              !a1.empty() && a1 == a2 && !b1.empty() && b1 == b2 && box1 && box1->title == "Export Done" &&
                  box1->text.startsWith(QStringLiteral("2 files, 1 label, 1 comment\n")) && rs.files == 2 && rs.labels == 1 && rs.comments == 1 &&
                  box2 && box2->title == "Export Done" &&
                  box2->text.startsWith(QStringLiteral("2 files, 1 label, 0 comments\n")) && rg.files == 2 && rg.labels == 1,
              fmt("a1=%zu a2=%zu b1=%zu b2=%zu m1=%s m2=%s", a1.size(), a2.size(), b1.size(), b2.size(), m1.describe().c_str(),
                  m2.describe().c_str()));
    }
    {   // QREX-03: Import of two files; the Disassembly shows the label; quitting asks to save
        Fixture fx(MachineType::ZXN_ISSUE2);
        if (!fx.ok) { check("QREX-03", "fixture", false); return; }
        fx.emu.nextreg().write(0x56, 0x10);
        fx.regs(0x8000);
        fx.enable();
        const std::string f1 = write_file("qrex-a.ctl", "# jnext space P10\nc $C000\n@ $C000 label=imp_a\nB $C000 note a\n");
        const std::string f2 = write_file("qrex-b.ctl", "# jnext space P10\nc $C010\n@ $C010 label=imp_b\n");
        QMenu* imp = submenu_named(menu_named(fx.dbg()->menuBar(), "Map"), "Import");
        const bool clean_before = !fx.mgr->annotations_unsaved();
        fx.mgr->breakpoints().add(BreakpointModel::Execute, 0xC000);
        auto bp_symbol = [&]() {
            QString out = QStringLiteral("<no row>");
            if (auto* t = fx.dbg()->breakpoint_panel()->findChild<QTableWidget*>())
                for (int r = 0; r < t->rowCount(); ++r)
                    if (t->item(r, 2) && t->item(r, 2)->text() == "$C000" && t->item(r, 3)) out = t->item(r, 3)->text();
            return out;
        };
        const QString sym_before = bp_symbol();
        Modals m;
        m.file_paths = {QString::fromStdString(f1), QString::fromStdString(f2)};
        if (QAction* a = item_named(imp, "skoolkit Control Files...")) a->trigger();
        m.timer.stop();
        const QString sym_after = bp_symbol();   // no tick, no other refresh: the import refreshed the panels itself
        DisasmPanel* dp = fx.dbg()->disasm_panel();
        if (auto* sb = dp->findChild<QScrollBar*>()) sb->setValue(0xC000);
        QApplication::processEvents();
        const Modals::Seen* box = m.first("box");
        check("QREX-03", "Import > skoolkit Control Files with two files selected imports both (the Disassembly shows imp_a), the box "
                         "reports '2 files, 2 labels, 1 comment', the panels are refreshed at once (a Breakpoints symbol cell, no tick), and the labels count as unsaved annotations (the quit question)",
              clean_before && fx.backend->annotations().find(P10, 0xC000) && fx.backend->annotations().find(P10, 0xC010) &&
                  dp->line_label(0) == "imp_a" && fx.mgr->annotations_unsaved() && box && box->title == "Import Done" &&
                  box->text.startsWith(QStringLiteral("2 files, 2 labels, 1 comment\n")) && box->icon == QMessageBox::Information &&
                  sym_before.isEmpty() && sym_after == "imp_a",
              fmt("m=%s label0='%s'", m.describe().c_str(), s(dp->line_label(0)).c_str()));
    }
    {   // QREX-04: a failed import
        Fixture fx(MachineType::ZXN_ISSUE2);
        if (!fx.ok) { check("QREX-04", "fixture", false); return; }
        fx.regs(0x8000);
        fx.enable();
        fx.backend->set_user_label(P10, 0xC100, "keep");
        const auto before = fx.backend->annotations();
        const uint64_t rev = fx.backend->annotations_revision();
        const std::string good = write_file("qrex-ok.ctl", "# jnext space P10\nc $C000\n@ $C000 label=fine\n");
        const std::string bad = write_file("qrex-bad.ctl", "# jnext space P10\nc $ZZZZ\n");
        QMenu* imp = submenu_named(menu_named(fx.dbg()->menuBar(), "Map"), "Import");
        Modals m;
        m.file_paths = {QString::fromStdString(good), QString::fromStdString(bad)};
        if (QAction* a = item_named(imp, "skoolkit Control Files...")) a->trigger();
        m.timer.stop();
        const Modals::Seen* box = m.first("box");
        check("QREX-04", "a failed Import shows an 'Import Failed' warning naming the file and the line and leaves the store and its "
                         "revision as they were",
              box && box->title == "Import Failed" && box->icon == QMessageBox::Warning && box->text.contains("qrex-bad.ctl: line 2:") &&
                  fx.backend->annotations() == before && fx.backend->annotations_revision() == rev,
              m.describe());
    }
    {   // QREX-05: a failed export
        Fixture fx(MachineType::ZXN_ISSUE2);
        if (!fx.ok) { check("QREX-05", "fixture", false); return; }
        fx.emu.nextreg().write(0x56, 0x10);
        fx.regs(0x8000);
        fx.backend->set_user_label(P10, 0xC000, "banked");
        fx.enable();
        const QString dir = g_tmp->filePath(QStringLiteral("qrex-foreign"));
        QDir().mkpath(dir);
        const std::string foreign = (dir + "/C000-P10.ctl").toStdString();
        { std::ofstream f(foreign); f << "not a jnext file\n"; }
        QMenu* exp = submenu_named(menu_named(fx.dbg()->menuBar(), "Map"), "Export");
        Modals m;
        m.dir_path = dir;
        if (QAction* a = item_named(exp, "skoolkit Control Files...")) a->trigger();
        m.timer.stop();
        const Modals::Seen* box = m.first("box");
        check("QREX-05", "a failed Export shows an 'Export Failed' warning naming the directory and the reason (a foreign file of the "
                         "same name), and leaves that file as it was",
              box && box->title == "Export Failed" && box->icon == QMessageBox::Warning && box->text.contains("qrex-foreign") &&
                  box->text.contains("C000-P10.ctl exists and is not a jnext control file") && slurp_file(foreign) == "not a jnext file\n" &&
                  m.boxes() == 1,
              m.describe());
    }
    {   // QREX-06: a report box carries the notes, not only the counts
        Fixture fx(MachineType::ZXN_ISSUE2);
        if (!fx.ok) { check("QREX-06", "fixture", false); return; }
        fx.regs(0x8000);
        fx.backend->set_user_label(P10, 0xC000, "banked");
        fx.enable();
        const QString d = g_tmp->filePath(QStringLiteral("qrex-notes"));
        QDir().mkpath(d);
        const std::string f1 = write_file("qrex-n.ctl", "# jnext space P10\nc $C000 a title\n@ $C000 label=ln\n");
        QMenu* map = menu_named(fx.dbg()->menuBar(), "Map");
        Modals me;
        me.dir_path = d;
        if (QAction* a = item_named(submenu_named(map, "Export"), "skoolkit Control Files...")) a->trigger();
        me.timer.stop();
        Modals mi;
        mi.file_paths = {QString::fromStdString(f1)};
        if (QAction* a = item_named(submenu_named(map, "Import"), "skoolkit Control Files...")) a->trigger();
        mi.timer.stop();
        const Modals::Seen* be = me.first("box");
        const Modals::Seen* bi = mi.first("box");
        check("QREX-06", "the report boxes list the notes after the counts: an Export with coverage off says 'memory coverage is off', "
                         "an Import of a block title says '1 block title(s) not imported'",
              be && be->title == "Export Done" && be->text.contains("\n- memory coverage is off") && bi && bi->title == "Import Done" &&
                  bi->text.contains("\n- 1 block title(s) not imported"),
              fmt("%s | %s", me.describe().c_str(), mi.describe().c_str()));
    }
    {   // QREX-07: a cancelled Import dialog does nothing
        Fixture fx(MachineType::ZXN_ISSUE2);
        if (!fx.ok) { check("QREX-07", "fixture", false); return; }
        fx.regs(0x8000);
        fx.enable();
        fx.backend->set_user_label(P10, 0xC100, "keep");
        const auto before = fx.backend->annotations();
        const uint64_t rev = fx.backend->annotations_revision();
        QMenu* map = menu_named(fx.dbg()->menuBar(), "Map");
        QMenu* imp = submenu_named(map, "Import");
        QMenu* exp = submenu_named(map, "Export");
        const QStringList tree_before = QDir(g_tmp->path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
        Modals m;
        m.cancel_files = true;
        if (QAction* a = item_named(imp, "skoolkit Control Files...")) a->trigger();
        if (QAction* a = item_named(imp, "Ghidra Symbols...")) a->trigger();
        if (QAction* a = item_named(exp, "skoolkit Control Files...")) a->trigger();
        if (QAction* a = item_named(exp, "Ghidra Symbols...")) a->trigger();
        m.timer.stop();
        int file_dialogs = 0;
        for (const auto& sn : m.seen) file_dialogs += sn.kind == "file";
        check("QREX-07", "cancelling any of the four Import and Export dialogs shows no box, writes no file and leaves the store and its "
                         "revision as they were",
              file_dialogs == 4 && m.boxes() == 0 && fx.backend->annotations() == before && fx.backend->annotations_revision() == rev &&
                  QDir(g_tmp->path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name) == tree_before,
              m.describe());
    }
}

// ===========================================================================
// QTS -- the CPU panel's T-state counter (GH #369), through the real manager,
// window and panels.
//
// No VHDL oracle: host debugger UI. The oracles are the issue's contract (the
// T-states since the last stop; Accumulate; restart when time goes backwards,
// on a reset or a program load, or when the window opens) and the documented
// Z80 T-states on uncontended memory: NOP 4, LD A,n 7, CALL nn 17, RET 10. The
// fixture is a ZX48K with code at 0x8000 and the stack at 0xFF00 (only
// 0x4000-0x7FFF is contended) and IFF1=0, so nothing else adds T-states.
// ===========================================================================

/// The value shown beside the CPU panel's "T-states: " label.
static QString tstates_shown(Fixture& fx) {
    CpuPanel* cpu = fx.dbg() ? fx.dbg()->cpu_panel() : nullptr;
    QLabel* anchor = label_with_text(cpu, QStringLiteral("T-states: "));
    if (!anchor) return QStringLiteral("<no label>");
    for (QBoxLayout* b : cpu->findChildren<QBoxLayout*>()) {
        const int idx = b->indexOf(anchor);
        if (idx < 0) continue;
        QLayoutItem* it = b->itemAt(idx + 1);
        auto* v = it ? qobject_cast<QLabel*>(it->widget()) : nullptr;
        return v ? v->text() : QStringLiteral("<no value>");
    }
    return QStringLiteral("<no row>");
}

static QCheckBox* tstates_box(Fixture& fx) {
    CpuPanel* cpu = fx.dbg() ? fx.dbg()->cpu_panel() : nullptr;
    if (!cpu) return nullptr;
    for (QCheckBox* c : cpu->findChildren<QCheckBox*>())
        if (c->text() == QLatin1String("Accumulate")) return c;
    return nullptr;
}

static void test_tstates() {
    set_group("QTS");

    {
        Fixture fx;
        const char* desc = "Step Into shows each instruction's own cost: NOP 4, then LD A,n 7 (not 11)";
        if (!fx.ok) { check("QTS-01", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x00, 0x3E, 0x12, 0x18, 0xFE});   // NOP / LD A,0x12 / JR $
            fx.regs(PROG);
            fx.enable();
            fx.mgr->on_step_into();
            const QString a = tstates_shown(fx);
            fx.mgr->on_step_into();
            const QString b = tstates_shown(fx);
            check("QTS-01", desc, a == "4" && b == "7", fmt("'%s' '%s'", s(a).c_str(), s(b).c_str()));
        }
    }

    {
        Fixture fx;
        const char* desc = "Step Over across a CALL shows the call and the routine: 17 + 10 = 27";
        if (!fx.ok) { check("QTS-02", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0xCD, 0x10, 0x80, 0x18, 0xFE});   // CALL $8010 / JR $
            fx.load(0x8010, {0xC9});                          // RET
            fx.regs(PROG);
            fx.enable();
            fx.mgr->on_step_over();
            fx.tick_until_paused();
            const QString v = tstates_shown(fx);
            check("QTS-02", desc, fx.pc() == 0x8003 && v == "27",
                  fmt("PC=%04X '%s'", fx.pc(), s(v).c_str()));
        }
    }

    {
        Fixture fx;
        const char* desc = "Run to an Execute breakpoint shows the whole run: NOP, NOP, LD A,n = 15";
        if (!fx.ok) { check("QTS-03", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x00, 0x00, 0x3E, 0x12, 0x18, 0xFE});   // NOP NOP LD A,n / JR $
            fx.regs(PROG);
            fx.enable();
            fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x8004);
            fx.mgr->on_run();
            fx.tick_until_paused();
            const QString v = tstates_shown(fx);
            check("QTS-03", desc, fx.pc() == 0x8004 && v == "15",
                  fmt("PC=%04X '%s'", fx.pc(), s(v).c_str()));
        }
    }

    {
        Fixture fx;
        const char* desc = "the Accumulate box adds stops up (0, 4, 11); unticking keeps 11 and "
                           "the next stop shows its own 4";
        QCheckBox* box = nullptr;
        if (!fx.ok) { check("QTS-04", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x00, 0x3E, 0x12, 0x00, 0x18, 0xFE});   // NOP / LD A,n / NOP / JR $
            fx.regs(PROG);
            fx.enable();
            box = tstates_box(fx);
            if (!box) { check("QTS-04", desc, false, "no Accumulate box"); }
            else {
                box->setChecked(true);
                const QString z = tstates_shown(fx);
                fx.mgr->on_step_into();
                const QString a = tstates_shown(fx);
                fx.mgr->on_step_into();
                const QString b = tstates_shown(fx);
                box->setChecked(false);
                const QString k = tstates_shown(fx);
                fx.mgr->on_step_into();
                const QString n = tstates_shown(fx);
                check("QTS-04", desc, z == "0" && a == "4" && b == "11" && k == "11" && n == "4",
                      fmt("'%s' '%s' '%s' '%s' '%s'", s(z).c_str(), s(a).c_str(), s(b).c_str(),
                          s(k).c_str(), s(n).c_str()));
            }
        }
    }

    {
        Fixture fx;
        const char* desc = "paused redraws that move no time (ticks, a direct refresh) keep the value";
        if (!fx.ok) { check("QTS-05", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x00, 0x18, 0xFE});
            fx.regs(PROG);
            fx.enable();
            fx.mgr->on_step_into();
            const QString a = tstates_shown(fx);
            for (int i = 0; i < 5; ++i) fx.tick();
            fx.dbg()->cpu_panel()->refresh();
            fx.dbg()->cpu_panel()->refresh();
            const QString b = tstates_shown(fx);
            check("QTS-05", desc, a == "4" && b == "4", fmt("'%s' '%s'", s(a).c_str(), s(b).c_str()));
        }
    }

    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "Step Back restarts at 0 with Accumulate on, and the next Step Into shows "
                           "only its own cost";
        QCheckBox* box = nullptr;
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QTS-06", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 3);
            box = tstates_box(fx);
            if (!box) { check("QTS-06", desc, false, "no Accumulate box"); }
            else {
                box->setChecked(true);
                fx.mgr->on_step_into();
                fx.mgr->on_step_into();
                fx.mgr->on_step_back();
                const QString z = tstates_shown(fx);
                const uint64_t t0 = fx.backend->time().tstates_total;
                fx.mgr->on_step_into();
                const uint64_t t1 = fx.backend->time().tstates_total;
                const QString n = tstates_shown(fx);
                check("QTS-06", desc,
                      z == "0" && t1 > t0 && n == QString::number(t1 - t0),
                      fmt("after Step Back '%s'; next '%s' (clock delta %llu)", s(z).c_str(),
                          s(n).c_str(), static_cast<unsigned long long>(t1 - t0)));
            }
        }
    }

    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "a rewind to the oldest frame and a FORWARD rewind to the newest both "
                           "restart the count at 0";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QTS-07", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 5);
            const auto* rb = fx.emu.rewind_buffer();
            const uint32_t oldest = rb->oldest_frame_num();
            const uint32_t newest = rb->newest_frame_num();
            fx.mgr->on_rewind_to_frame(oldest);
            const QString a = tstates_shown(fx);
            fx.mgr->on_rewind_to_frame(newest);
            const QString b = tstates_shown(fx);
            check("QTS-07", desc, newest > oldest && a == "0" && b == "0",
                  fmt("frames %u..%u: '%s' '%s'", oldest, newest, s(a).c_str(), s(b).c_str()));
        }
    }

    {
        Fixture fx(MachineType::ZX48K, 0);
        const char* desc = "a cold boot while the machine runs restarts the count at the reset: "
                           "the next stop shows now - the new machine's start";
        if (!fx.ok) { check("QTS-08", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 1);
            fx.mgr->on_run();
            fx.backend->on_cold_boot_begin();
            emulator_frontend_cold_boot(fx.emu, fx.emu.config(), std::string(), ColdBootHooks{});
            fx.backend->on_cold_boot_done();
            const uint64_t t0 = fx.backend->time().tstates_total;
            const QString at_reset = tstates_shown(fx);   // still running: the push repaints the label itself
            for (int i = 0; i < 3; ++i) fx.tick();
            fx.mgr->on_pause();
            fx.tick();
            const uint64_t now = fx.backend->time().tstates_total;
            const QString v = tstates_shown(fx);
            check("QTS-08", desc, now > t0 && v == QString::number(now - t0) && at_reset == "0",
                  fmt("shown '%s' (at the reset '%s'), now %llu, start %llu", s(v).c_str(), s(at_reset).c_str(),
                      static_cast<unsigned long long>(now), static_cast<unsigned long long>(t0)));
        }
    }

    {
        Fixture fx(MachineType::ZX48K, 0);
        const char* desc = "opening the debugger window restarts the count at that moment";
        if (!fx.ok) { check("QTS-09", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 1);
            fx.mgr->set_enabled(false);            // resumes the machine; window kept
            for (int i = 0; i < 2; ++i) fx.emu.run_frame();
            const uint64_t t0 = fx.backend->time().tstates_total;
            fx.enable();
            const QString at_open = tstates_shown(fx);   // machine running: the restart repaints the label itself
            for (int i = 0; i < 2; ++i) fx.tick();
            fx.mgr->on_pause();
            fx.tick();
            const uint64_t now = fx.backend->time().tstates_total;
            const QString v = tstates_shown(fx);
            check("QTS-09", desc, now > t0 && v == QString::number(now - t0) && at_open == "0",
                  fmt("shown '%s', now %llu, window opened at %llu", s(v).c_str(),
                      static_cast<unsigned long long>(now), static_cast<unsigned long long>(t0)));
        }
    }

    {
        Fixture fx;
        const char* desc = "an in-place .jns restore (any in-place load) restarts the count at the next stop: 0, then the next step's own cost";
        if (!fx.ok) { check("QTS-10", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x00, 0x00, 0x00, 0x18, 0xFE});
            fx.regs(PROG);
            fx.enable();
            fx.mgr->on_step_into();
            const std::string jns = (QDir(g_tmp->path()).filePath("gh369b.jns")).toStdString();
            const bool saved = fx.emu.save_jns_file(jns);
            fx.mgr->on_step_into();
            const bool loaded = fx.emu.load_jns_file(jns);
            const uint64_t t0 = fx.backend->time().tstates_total;
            fx.mgr->on_step_into();
            const QString a = tstates_shown(fx);
            const uint64_t t1 = fx.backend->time().tstates_total;
            fx.mgr->on_step_into();
            const QString b = tstates_shown(fx);
            const uint64_t t2 = fx.backend->time().tstates_total;
            check("QTS-10", desc, saved && loaded && a == "0" && t2 > t1 && b == QString::number(t2 - t1),
                  fmt("saved=%d loaded=%d '%s' '%s' clock %llu %llu %llu", saved, loaded, s(a).c_str(), s(b).c_str(),
                      (unsigned long long)t0, (unsigned long long)t1, (unsigned long long)t2));
        }
    }

    {
        Fixture fx;
        const char* desc = "ticking Accumulate after a stop that showed 4 shows 0 at once, before any further stop";
        QCheckBox* box = nullptr;
        if (!fx.ok) { check("QTS-11", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x00, 0x18, 0xFE});
            fx.regs(PROG);
            fx.enable();
            fx.mgr->on_step_into();
            const QString before = tstates_shown(fx);
            box = tstates_box(fx);
            if (box) box->setChecked(true);
            const QString after = tstates_shown(fx);
            check("QTS-11", desc, box && before == "4" && after == "0",
                  fmt("'%s' then '%s'", s(before).c_str(), s(after).c_str()));
        }
    }

    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "a REFUSED rewind (frame outside the ring) keeps the count on show";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QTS-12", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 3);
            fx.mgr->on_step_into();
            const QString before = tstates_shown(fx);
            fx.mgr->on_rewind_to_frame(99999);
            const QString after = tstates_shown(fx);
            check("QTS-12", desc, before != "0" && after == before,
                  fmt("'%s' then '%s'", s(before).c_str(), s(after).c_str()));
        }
    }

    {
        // QtApp::cold_boot() for a menu / Quick Load, then the deferred in-place
        // load a few ticks later (qt_app.cpp pre_frames()). A .jns restores its
        // SAVED total, so time jumps forward without program_loads() moving; the
        // restore goes through load_state(), which moves time_epoch().
        Fixture fx(MachineType::ZX48K, 0);
        const char* desc = "a deferred in-place .jns load after a cold boot restarts the count: the next "
                           "stop shows 0, not the saved session (no program_loads() involved)";
        if (!fx.ok) { check("QTS-13", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 21);
            const std::string jns = (QDir(g_tmp->path()).filePath("gh369.jns")).toStdString();
            const bool saved = fx.emu.save_jns_file(jns);
            fx.backend->on_cold_boot_begin();
            emulator_frontend_cold_boot(fx.emu, fx.emu.config(), std::string(), ColdBootHooks{});
            fx.backend->on_cold_boot_done();
            const bool loaded = emulator_apply_load(fx.emu, jns, false);   // the deferred in-place load: the saved total comes back
            const uint64_t t0 = fx.backend->time().tstates_total;
            fx.mgr->on_run();
            for (int i = 0; i < 2; ++i) fx.tick();
            fx.mgr->on_pause();
            fx.tick();
            const uint64_t now = fx.backend->time().tstates_total;
            const QString v = tstates_shown(fx);
            check("QTS-13", desc, saved && loaded && t0 > 100000 && now > t0 && v == "0",
                  fmt("saved=%d loaded=%d shown '%s' (want 0; run since the load %llu), time at load %llu", saved, loaded,
                      s(v).c_str(), static_cast<unsigned long long>(now - t0),
                      static_cast<unsigned long long>(t0)));
        }
    }
}

// ── QMPW - GH #370: the Memory panel's tool messages in the real window ─────────────────────
//
// Oracle: the issue (a find moves the Disassembly; every write is one backend poke) and the
// debugger window's documented rewind wall (§4.2a: a debugger change is marked on the ring, so
// Step Back across it is refused). The answers are the window's status bar, held like a refusal.
static void test_memory_tools_window() {
    set_group("QMPW");
    using jnext::dbg::MemSpace;
    namespace mt = jnext::dbg::memtools;
    QTemporaryDir td;

    // QMPW-01
    {
        Fixture fx(MachineType::ZX48K, 0);
        const char* desc = "a Find in the Memory panel selects the hit in the Disassembly and says so in the status bar";
        if (!fx.ok) check("QMPW-01", desc, false, "fixture");
        else {
            fx.load(0x8100, {'J', 'N', 'E', 'X', 'T'});
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            dbg->memory_panel()->find(*mt::text_pattern("JNEXT", true, nullptr));
            check("QMPW-01", desc,
                  dbg->disasm_panel()->selected_address() == 0x8100 && status_of(dbg) == "Found at $8100",
                  fmt("sel=%04X status='%s'", dbg->disasm_panel()->selected_address(), s(status_of(dbg)).c_str()));
        }
    }

    // QMPW-02
    {
        Fixture fx(MachineType::ZX48K, 0);
        const char* desc = "a partial Load's message is in the status bar and survives a panel refresh (the hold)";
        if (!fx.ok) check("QMPW-02", desc, false, "fixture");
        else {
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            QFile f(td.path() + "/f.bin");
            if (!f.open(QIODevice::WriteOnly)) check("QMPW-02", desc, false, "cannot write the file");
            f.write(QByteArray(16, '\x5A'));
            f.close();
            dbg->memory_panel()->load_block(0x3FF8, std::nullopt, f.fileName());
            const QString want = "Loaded 8 of 16 bytes at $3FF8 from f.bin: the rest is ROM, which is read-only";
            const QString at_once = status_of(dbg);
            dbg->refresh_panels();
            check("QMPW-02", desc, at_once == want && status_of(dbg) == want,
                  s(at_once) + " / " + s(status_of(dbg)));
        }
    }

    // QMPW-03
    {
        // The same four steps, with and without a Memory panel Fill in the middle.
        auto run = [&](bool fill) {
            Fixture fx(MachineType::ZX48K, 10);
            if (!fx.ok || !fx.emu.rewind_buffer()) return QString("<fixture>");
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 4);
            fx.mgr->on_step_into();
            fx.mgr->on_step_into();
            if (fill) fx.dbg()->memory_panel()->fill_block(0xC000, 16, {0xAA});
            fx.mgr->on_step_into();
            fx.mgr->on_step_into();
            fx.mgr->on_step_back();
            return status_of(fx.dbg());
        };
        const QString control = run(false), with_fill = run(true);
        check("QMPW-03", "a Memory panel Fill marks the rewind ring: Step Back across it is refused, and is not without it",
              !control.startsWith("Step Back refused") &&
                  with_fill.startsWith("Step Back refused: it would undo a change you made from the debugger"),
              s(control) + " / " + s(with_fill));
    }

    // QMPW-04
    {
        Fixture fx(MachineType::ZXN_ISSUE2, 0);
        const char* desc = "a Find hit in a page no slot maps leaves the Disassembly where it was and says so";
        if (!fx.ok) check("QMPW-04", desc, false, "fixture");
        else {
            fx.enable();
            DebuggerWindow* dbg = fx.dbg();
            const std::vector<uint8_t> p8{0xF1, 0x37, 0x0A, 0x5C, 0xE2, 0x9B, 0x44, 0xC6};
            fx.backend->poke(jnext::dbg::CLIENT_NONE, MemSpace::page(0x30), 0x10, p8.size(), p8.data());
            MemoryPanel* mem = dbg->memory_panel();
            QTimer timer;                       // answers the "Page..." prompt
            QObject::connect(&timer, &QTimer::timeout, [&]() {
                auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dlg) return;
                if (auto* e = dlg->findChild<QLineEdit*>()) e->setText("30");
                dlg->accept();
            });
            timer.start(1);
            mem->findChild<QComboBox*>()->setCurrentIndex(9);
            timer.stop();
            QApplication::processEvents();
            const uint16_t before = dbg->disasm_panel()->selected_address();
            mem->find(mt::Pattern{p8, std::vector<uint8_t>(8, 0xFF)});
            check("QMPW-04", desc,
                  dbg->disasm_panel()->selected_address() == before &&
                      status_of(dbg) == "Found at offset $0010 of P30; not mapped in any slot, so the Disassembly does not move",
                  s(status_of(dbg)));
        }
    }
}

// ===========================================================================
// QBPJ (GH #376) -- Breakpoints > Save as Script... / Load from Script...: the
// GUI's own breakpoints as a `.jds` file and back, through the real menu, the
// real file dialogs and the real boxes. Oracle: the codec's grammar of record
// (dsl-frontend.md section 2.1; rows BPJ-*) and what the model lists after a
// load in a fresh session. The breakpoints are set through the model's
// mutators, which every GUI route (gutter, Add dialog, Source tab, menu) ends in.
// ===========================================================================

static const char* const kBpjSld =
    "|SLD.data.version|1\n"
    "main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
    "main.bas|10||0|20|49152|T|\n"
    "main.bas|10||0|21|49408|T|\n"
    "main.bas|11||0|20|49154|T|\n"
    "x.asm|1||0|20|49152|L|,banked,,\n";

namespace {
struct BpjFix {
    // On the heap: a Fixture holds a whole Emulator, and a stack frame with one per row overflows 8 MB.
    std::unique_ptr<Fixture> own;
    Fixture& fx;
    explicit BpjFix() : own(std::make_unique<Fixture>(MachineType::ZXN_ISSUE2)), fx(*own) {
        if (!fx.ok) return;
        fx.regs(0x8000);
        fx.backend->load_source_map(write_file("bpj.sld", kBpjSld), false);
        fx.enable();
    }
    BreakpointModel& model() { return fx.mgr->breakpoints(); }
    QMenu* bp_menu() { return menu_named(fx.dbg()->menuBar(), "Breakpoints"); }
    // Run a menu item that opens a file dialog (answered with `path`) and boxes; returns what was seen.
    std::vector<Modals::Seen> run(const char* item, const QString& path, bool cancel = false,
                                  std::function<void()> after_accept = {}) {
        Modals mo;
        mo.file_path = path;
        mo.cancel_files = cancel;
        mo.after_file_accept = std::move(after_accept);
        if (QAction* a = item_named(bp_menu(), item)) a->trigger();
        mo.timer.stop();
        return mo.seen;
    }
};
int count_kind(const std::vector<Modals::Seen>& v, const char* kind) {
    int n = 0;
    for (const auto& m : v) n += m.kind == kind;
    return n;
}
}  // namespace

static void test_bp_script() {
    set_group("QBPJ");
    const jnext::dbg::MemSpace P20 = jnext::dbg::MemSpace::page(20);
    const jnext::dbg::MemSpace P22 = jnext::dbg::MemSpace::page(22);
    using BM = BreakpointModel;
    {   // QBPJ-01
        BpjFix g;
        if (!g.fx.ok) { check("QBPJ-01", "fixture", false); return; }
        QMenu* m = g.bp_menu();
        int clear = -1, save = -1, load = -1, i = 0;
        QStringList letters;
        for (QAction* a : m->actions()) {
            if (visible(a->text()) == "Clear All Breakpoints") clear = i;
            if (visible(a->text()) == "Save as Script...") save = i;
            if (visible(a->text()) == "Load from Script...") load = i;
            const int at = a->text().indexOf(QLatin1Char('&'));
            if (at >= 0 && at + 1 < a->text().size()) letters << a->text().at(at + 1).toUpper();
            ++i;
        }
        QStringList uniq = letters;
        uniq.removeDuplicates();
        check("QBPJ-01", "the Breakpoints menu has Save as Script... and Load from Script... after Clear All, and its "
                         "mnemonic letters are unique",
              clear >= 0 && save > clear && load > save && uniq.size() == letters.size(),
              fmt("clear=%d save=%d load=%d letters=%d/%d", clear, save, load, int(uniq.size()), int(letters.size())));
    }
    {   // QBPJ-02
        BpjFix g;
        g.model().add(BM::Execute, 0x8000);
        g.model().add(BM::Read, 0x9000);
        const auto seen = g.run("Save as Script...", g_tmp->filePath(QStringLiteral("bpj-out")));
        const std::string got = slurp_file(g_tmp->filePath(QStringLiteral("bpj-out.jds")).toStdString());
        const std::string want = jnext::script::bp_script_text(g.model().script_specs(), *g.fx.backend).text;
        check("QBPJ-02", "Save through the menu: a name without a suffix gets .jds, the bytes are the codec's text for the "
                         "model's own rows, the dialog offers the .jds filter, and no box opens",
              got == want && !want.empty() && count_kind(seen, "box") == 0 && count_kind(seen, "file") == 1 &&
                  seen[0].filters.contains("*.jds"),
              got);
    }
    std::vector<jnext::script::BpSpec> saved;
    {   // QBPJ-03: session A saves, is destroyed; session B loads
        {
            BpjFix a;
            BM& m = a.model();
            m.add(BM::Execute, 0x8000);
            m.add(BM::Execute, 0xC000, P20);
            m.add_source_line(QStringLiteral("main.bas:10"));
            m.add(BM::Read, 0x9000);
            m.add(BM::Write, 0x9001);
            m.add(BM::ReadWrite, 0x9002);
            m.add(BM::IoRead, 0x00FE);
            m.add(BM::IoWrite, 0x243B);
            m.set_enabled(BM::Write, 0x9001, false);
            for (const auto& r : m.rows())
                if (r.group != jnext::dbg::GROUP_NONE) { m.set_enabled(r, false); break; }
            jnext::dbg::ClientInfo oi;
            oi.name = "other";
            oi.kind = jnext::dbg::ClientKind::Test;
            const jnext::dbg::ClientId other = a.fx.backend->attach(oi).value;
            a.fx.backend->pump(jnext::dbg::PumpBudget{});   // settle the attach: the first pump only primes
            jnext::dbg::Subscription sub;
            sub.kind = jnext::dbg::EventKind::Execute;
            sub.filter.lo = sub.filter.hi = 0x8100;
            sub.action = jnext::dbg::Action::Stop;
            a.fx.backend->subscribe(other, sub);
            a.fx.backend->pump(jnext::dbg::PumpBudget{});   // delivers the push, as a tick's pump does
            m.sync();
            saved = m.script_specs();
            a.run("Save as Script...", g_tmp->filePath(QStringLiteral("bpj-rt.jds")));
            const std::string text = slurp_file(g_tmp->filePath(QStringLiteral("bpj-rt.jds")).toStdString());
            // QBPJ-09 rides on this session: the other client's $8100 is listed but not saved.
            bool listed = false;
            for (const auto& r : m.rows()) listed = listed || (!r.own && r.addr == 0x8100);
            check("QBPJ-09", "another client's breakpoint is listed read-only but is not in the saved file",
                  listed && text.find("0x8100") == std::string::npos && text.find("0x8000") != std::string::npos,
                  fmt("listed=%d text=%s", listed, text.c_str()));
        }
        BpjFix b;
        const auto seen = b.run("Load from Script...", g_tmp->filePath(QStringLiteral("bpj-rt.jds")));
        bool all_own = true;
        for (const auto& r : b.model().rows()) all_own = all_own && r.own;
        const auto got = b.model().script_specs();
        check("QBPJ-03", "round trip into a fresh session: every kind, the symbol-qualified Execute, a source-line group "
                         "and the On states come back equal (type, address, memory, places, enabled), no other client's "
                         "row, no box",
              saved.size() == 8 && got == saved && all_own && b.model().rows().size() == 8 && count_kind(seen, "box") == 0,
              fmt("saved=%d got=%d rows=%d seen=%s", int(saved.size()), int(got.size()), int(b.model().rows().size()),
                  b.fx.ok ? "" : "nofix"));
    }
    {   // QBPJ-04
        BpjFix g;
        g.model().add(BM::Execute, 0x8004, P22);
        g.model().add(BM::Read, 0x9000);
        const auto seen = g.run("Save as Script...", g_tmp->filePath(QStringLiteral("bpj-ref.jds")));
        const std::string text = slurp_file(g_tmp->filePath(QStringLiteral("bpj-ref.jds")).toStdString());
        const Modals::Seen* box = nullptr;
        for (const auto& m : seen) if (m.kind == "box") box = &m;
        check("QBPJ-04", "a qualified Execute no symbol names gives one 'Breakpoints Not Saved' box naming it; the file "
                         "still holds the other rule and the `; not saved:` comment",
              count_kind(seen, "box") == 1 && box && box->title == "Breakpoints Not Saved" &&
                  box->text.contains("$8004 P16") && text.find("bp1: on read 0x9000 do stop end") != std::string::npos &&
                  text.find("; not saved: Execute $8004 P16") != std::string::npos,
              text);
    }
    {   // QBPJ-05
        BpjFix g;
        g.model().add(BM::Execute, 0x8000);
        const auto before = g.model().script_specs();
        const QString path = g_tmp->filePath(QStringLiteral("bpj-bad.jds"));
        write_file("bpj-bad.jds", "on frame do log \"x\" end\n");
        const auto seen = g.run("Load from Script...", path);
        const Modals::Seen* box = nullptr;
        for (const auto& m : seen) if (m.kind == "box") box = &m;
        check("QBPJ-05", "loading a file that is not plain breakpoints gives a 'Load Failed' box with FILE:1: and changes "
                         "no row",
              box && box->title == "Load Failed" && box->text.contains(path + ":1:") &&
                  g.model().script_specs() == before,
              box ? s(box->text) : std::string("(no box)"));
    }
    {   // QBPJ-06
        BpjFix g;
        g.model().add(BM::Execute, 0x8000);
        g.model().add(BM::Read, 0x9000);
        write_file("bpj-merge.jds", "disabled bp1: on execute 0x8000 do stop end\n");
        const auto seen = g.run("Load from Script...", g_tmp->filePath(QStringLiteral("bpj-merge.jds")));
        int execs = 0;
        bool off = false, read_on = false;
        for (const auto& r : g.model().rows()) {
            if (r.type == BM::Execute && r.addr == 0x8000) { ++execs; off = !r.enabled; }
            if (r.type == BM::Read && r.addr == 0x9000) read_on = r.enabled;
        }
        check("QBPJ-06", "Load merges: the existing $8000 row takes the file's disabled state (still one row) and the "
                         "other breakpoint is untouched",
              execs == 1 && off && read_on && g.model().rows().size() == 2 && count_kind(seen, "box") == 0);
    }
    {   // QBPJ-07
        BpjFix g;
        write_file("bpj-twice.jds", "on execute \"main.bas:10\" do stop end\non execute main.bas:10 do stop end\n");
        const QString path = g_tmp->filePath(QStringLiteral("bpj-twice.jds"));
        g.run("Load from Script...", path);
        g.run("Load from Script...", path);
        check("QBPJ-07", "a source line twice in one file, then the file loaded again, leaves one group of two places",
              g.model().rows().size() == 1 && g.model().rows()[0].locations == 2 &&
                  g.model().rows()[0].group != jnext::dbg::GROUP_NONE);
    }
    {   // QBPJ-08
        BpjFix g;
        g.model().add_source_line(QStringLiteral("main.bas:10"));
        // The panel's Edit: remove the row, add the new line.
        const BM::Row old = g.model().rows()[0];
        g.model().remove(old);
        g.model().add_source_line(QStringLiteral("main.bas:11"));
        g.run("Save as Script...", g_tmp->filePath(QStringLiteral("bpj-edit.jds")));
        const std::string text = slurp_file(g_tmp->filePath(QStringLiteral("bpj-edit.jds")).toStdString());
        check("QBPJ-08", "a group edited to another line saves the NEW line",
              text.find("\"main.bas:11\"") != std::string::npos && text.find("main.bas:10") == std::string::npos, text);
    }
    {   // QBPJ-10
        BpjFix g;
        g.model().add(BM::Execute, 0x8000);
        const auto before = g.model().script_specs();
        const int files = QDir(g_tmp->path()).entryList(QDir::Files).size();
        const auto s1 = g.run("Save as Script...", g_tmp->filePath(QStringLiteral("bpj-cancel")), true);
        const auto s2 = g.run("Load from Script...", g_tmp->filePath(QStringLiteral("bpj-out.jds")), true);
        check("QBPJ-10", "cancelled Save and Load dialogs write no file, open no box and change no row",
              count_kind(s1, "file") == 1 && count_kind(s2, "file") == 1 && count_kind(s1, "box") + count_kind(s2, "box") == 0 &&
                  QDir(g_tmp->path()).entryList(QDir::Files).size() == files && g.model().script_specs() == before);
    }
}

// Split from test_bp_script(): every row's fixture holds a whole Emulator on the stack, and one frame
// with all of them overflows the 8 MB stack.
static void test_bp_script_more() {
    set_group("QBPJ");
    using BM = BreakpointModel;
    {   // QBPJ-11
        BpjFix g;
        g.model().add_source_line(QStringLiteral("main.bas:10"));
        // (indexed through a guard: with no own row a failure must read as a FAIL, not crash the suite)
        auto first_places = [&g]() {
            const auto specs = g.model().script_specs();
            return specs.empty() ? std::vector<::SourceAddress>{} : specs[0].locations;
        };
        const auto places = first_places();
        write_file("bpj-off.jds", "disabled bp1: on execute \"main.bas:10\" do stop end\n");
        write_file("bpj-on.jds", "bp1: on execute \"main.bas:10\" do stop end\n");
        g.run("Load from Script...", g_tmp->filePath(QStringLiteral("bpj-off.jds")));
        const bool off = g.model().rows().size() == 1 && !g.model().rows()[0].enabled &&
                         g.model().rows()[0].group != jnext::dbg::GROUP_NONE &&
                         jnext::script::same_locations(first_places(), places);
        g.run("Load from Script...", g_tmp->filePath(QStringLiteral("bpj-on.jds")));
        const bool on = g.model().rows().size() == 1 && g.model().rows()[0].enabled &&
                        jnext::script::same_locations(first_places(), places);
        check("QBPJ-11", "loading a source line onto the group already armed there gives it the file's On state, both ways "
                         "(enabled group + `disabled` line: off; then an enabled line: on), one row, same places",
              places.size() == 2 && off && on);
    }
    {   // QBPJ-12
        BpjFix g;
        g.model().add(BM::Execute, 0x8000);
        const QString dir = g_tmp->filePath(QStringLiteral("bpj-gone"));
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/x.jds");
        const auto seen = g.run("Save as Script...", path, false, [dir]() { QDir(dir).removeRecursively(); });
        const Modals::Seen* box = nullptr;
        for (const auto& m : seen) if (m.kind == "box") box = &m;
        check("QBPJ-12", "a Save that cannot write gives one 'Save Failed' box naming the path, no 'Not Saved' box and no file",
              count_kind(seen, "box") == 1 && box && box->title == "Save Failed" && box->text.contains(path) &&
                  !QFile::exists(path),
              box ? s(box->title) + " " + s(box->text) : std::string("(no box)"));
    }
    {   // QBPJ-13
        BpjFix g;
        g.model().add(BM::Execute, 0x8000);
        const auto before = g.model().script_specs();
        write_file("bpj-vanish.jds", "on read 0x9000 do stop end\n");
        const QString path = g_tmp->filePath(QStringLiteral("bpj-vanish.jds"));
        const auto seen = g.run("Load from Script...", path, false, [path]() { QFile::remove(path); });
        const Modals::Seen* box = nullptr;
        for (const auto& m : seen) if (m.kind == "box") box = &m;
        check("QBPJ-13", "a Load of a file that cannot be read gives one 'Load Failed' box naming it and changes no row",
              count_kind(seen, "box") == 1 && box && box->title == "Load Failed" && box->text.contains(path) &&
                  g.model().script_specs() == before,
              box ? s(box->text) : std::string("(no box)"));
    }
    {   // QBPJ-14
        BpjFix g;
        g.model().add(BM::Execute, 0x8000);
        const QString saved = g_tmp->filePath(QStringLiteral("bpj-mem.jds"));
        g.run("Save as Script...", saved);
        const auto s1 = g.run("Load from Script...", saved);          // opens on the file the Save used
        write_file("bpj-mem2.jds", "on read 0x9000 do stop end\n");
        const QString other = g_tmp->filePath(QStringLiteral("bpj-mem2.jds"));
        g.run("Load from Script...", other);
        const auto s2 = g.run("Save as Script...", other + QStringLiteral(".keep"));   // opens on the file the Load used
        check("QBPJ-14", "the last path seeds both dialogs: a Load opens on the file the Save wrote, and a Save on the file "
                         "the Load read",
              count_kind(s1, "file") == 1 && s1[0].initial == QStringLiteral("bpj-mem.jds") &&
                  count_kind(s2, "file") == 1 && s2[0].initial == QStringLiteral("bpj-mem2.jds"),
              s1.empty() || s2.empty() ? std::string("(no dialog)") : s(s1[0].initial) + " | " + s(s2[0].initial));
    }
}

// ===========================================================================
// QREGW -- GH #368: a register edit in the real debugger window.
//
// No VHDL oracle: host debugger UI. The oracles are the facade's documented
// `set_register` (a write bumps state_revision(), so the next paused tick redraws
// every panel; refused under RZX; it marks the rewind ring) and the existing
// rewind refusal contract (QRF-07/08). The edit is made as a user makes it:
// double-click the value in the window's CPU panel, type, Enter.
// ===========================================================================

/// The window's CPU-panel value label beside `name` ("PC: ").
static QLabel* window_reg_label(Fixture& fx, const char* name) {
    CpuPanel* cpu = fx.dbg() ? fx.dbg()->cpu_panel() : nullptr;
    QLabel* anchor = label_with_text(cpu, QString::fromLatin1(name));
    if (!anchor) return nullptr;
    for (QGridLayout* g : cpu->findChildren<QGridLayout*>()) {
        const int idx = g->indexOf(anchor);
        if (idx < 0) continue;
        int r = 0, c = 0, rs = 0, cs = 0;
        g->getItemPosition(idx, &r, &c, &rs, &cs);
        QLayoutItem* it = g->itemAtPosition(r, c + 1);
        return it ? qobject_cast<QLabel*>(it->widget()) : nullptr;
    }
    return nullptr;
}

/// Double-click `name`'s value in the window's CPU panel, type `text`, press Enter.
static bool window_edit_reg(Fixture& fx, const char* name, const char* text) {
    QLabel* l = window_reg_label(fx, name);
    CpuPanel* cpu = fx.dbg()->cpu_panel();
    if (!l || !cpu) return false;
    const QPoint pos(2, 2);
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(pos), QPointF(l->mapToGlobal(pos)),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(l, &dbl);
    QLineEdit* e = nullptr;
    for (QLineEdit* c : cpu->findChildren<QLineEdit*>()) e = c;
    if (!e || e->isHidden()) return false;
    e->setText(QString::fromLatin1(text));
    QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(e, &key);
    return true;
}

static void test_cpu_edit_window() {
    set_group("QREGW");

    {
        Fixture fx;
        const char* desc = "an edit of PC through the window's CPU panel moves the Disassembly's PC "
                           "line at the next paused tick: the only hint in the view is that of the new PC line "
                           "(LD A,(HL) -> [($9005)=$3C]), and there was none before";
        if (!fx.ok) { check("QREGW-01", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x00, 0x7E, 0x00, 0x18, 0xFD});   // NOP / LD A,(HL) / NOP / JR $8000
            fx.load(0x9005, {0x3C});
            fx.regs(PROG, [](Z80Registers& r) { r.HL = 0x9005; });
            fx.enable();
            if (auto* sb = fx.dbg()->disasm_panel()->findChild<QScrollBar*>()) sb->setValue(PROG);
            fx.tick();
            // The scroll bar clamps below $8000, so the PC line is further down: scan the view.
            auto hints = [&]() {
                QStringList h;
                for (int i = 0; i < 80; ++i) {
                    const QString t = fx.dbg()->disasm_panel()->line_hint(i);
                    if (!t.isEmpty()) h << t;
                }
                return h;
            };
            const QStringList before = hints();
            const bool done = window_edit_reg(fx, "PC: ", "8001");
            fx.tick();
            const QStringList after = hints();
            check("QREGW-01", desc,
                  done && fx.pc() == 0x8001 && before.isEmpty() &&
                      after == QStringList{QStringLiteral("[($9005)=$3C]")},
                  fmt("done=%d PC=%04X hints before '%s', after '%s'", done, fx.pc(),
                      s(before.join('|')).c_str(), s(after.join('|')).c_str()));
        }
    }

    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "a refused edit (RZX playing) puts its text in the window's status bar, "
                           "and it is still there after more paused ticks with a non-empty rewind ring";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QREGW-02", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            run_frames_then_break(fx, 3);
            fx.emu.rzx_player().start(RzxRecording{});
            const bool done = window_edit_reg(fx, "BC: ", "1234");
            const QString want = QString::fromStdString(
                jnext::regedit::refusal_text("BC", jnext::dbg::Result::RefusedRzx));
            const QString at_once = status_of(fx.dbg());
            fx.tick();
            fx.dbg()->refresh_panels();
            const QString later = status_of(fx.dbg());
            fx.emu.rzx_player().stop();
            check("QREGW-02", desc,
                  done && at_once == want && later == want && fx.backend->rewind_range().depth > 0,
                  fmt("done=%d status now '%s', later '%s'", done, s(at_once).c_str(), s(later).c_str()));
        }
    }

    {
        Fixture fx(MachineType::ZX48K, 10);
        const char* desc = "with rewind on: after a register edit Step Back is refused (\"would undo a "
                           "change you made from the debugger\") with BC still edited; Frame Back to "
                           "that frame succeeds and BC is back to its pre-edit value";
        if (!fx.ok || !fx.emu.rewind_buffer()) { check("QREGW-03", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.regs(PROG, [](Z80Registers& r) { r.HL = 0; r.BC = 0x0102; });
            fx.enable();
            run_frames_then_break(fx, 4);
            fx.mgr->on_step_into();
            fx.mgr->on_step_into();
            const uint32_t frame = fx.backend->time().frame;
            const bool done = window_edit_reg(fx, "BC: ", "1234");
            fx.mgr->on_step_into();
            fx.mgr->on_step_into();
            const uint16_t pc0 = fx.pc();
            fx.mgr->on_step_back();
            const QString st = status_of(fx.dbg());
            const bool refused = st.startsWith(QStringLiteral(
                "Step Back refused: it would undo a change you made from the debugger")) &&
                fx.pc() == pc0 && fx.backend->registers().BC == 0x1234;
            fx.mgr->on_rewind_to_frame(frame);
            fx.tick();
            check("QREGW-03", desc,
                  done && refused && fx.backend->registers().BC == 0x0102,
                  fmt("done=%d refused=%d status '%s' BC after Frame Back %04X", done, refused,
                      s(st).c_str(), fx.backend->registers().BC));
        }
    }

    {
        Fixture fx;
        const char* desc = "the MUTATE line of a window CPU-panel edit names the window's client "
                           "(set_panels_client reaches the panel)";
        if (!fx.ok) { check("QREGW-04", desc, false, "fixture"); }
        else {
            load_counter(fx);
            fx.enable();
            RefusalProbe p(fx);
            fx.mgr->on_pause();
            const jnext::dbg::ClientId win = fx.backend->state().pause_reason.by;
            const uint16_t de0 = fx.backend->registers().DE;
            const bool done = window_edit_reg(fx, "DE: ", "4321");
            const std::string want = fmt("MUTATE reg DE 0x%X -> 0x4321 by %u", de0, static_cast<unsigned>(win));
            check("QREGW-04", desc,
                  done && win != jnext::dbg::CLIENT_NONE && win != p.cid && p.log.has(want),
                  fmt("win=%u probe=%u lines=%d want '%s'", static_cast<unsigned>(win),
                      static_cast<unsigned>(p.cid), (int)p.log.lines.size(), want.c_str()));
        }
    }
}

// ===========================================================================
// QNAVW -- GH #366: navigation through the real window.
// Oracle: the issue's contract (Follow a memory operand shows it in the Memory panel; clicking a
// register opens its value in the Disassembly; Back returns).
// ===========================================================================
static void test_nav_window() {
    set_group("QNAVW");

    {
        Fixture fx;
        const char* desc = "Follow on a memory operand raises the Memory tab and selects the operand address in the "
                           "Memory panel's CPU View, also when the panel was in a Slot view before";
        if (!fx.ok) { check("QNAVW-04", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0x3A, 0x40, 0x5C, 0x18, 0xFB});   // LD A,($5C40) / JR $8000
            fx.regs(PROG);
            fx.enable();
            MemoryPanel* mem = fx.dbg()->memory_panel();
            auto* combo = mem->findChild<QComboBox*>();
            if (combo) combo->setCurrentIndex(1);   // Slot 0
            // GH #377: Memory is a dock now; "its tab is shown" reads the group's tab bar.
            QDockWidget* mem_dock = dock_of_panel(fx.dbg(), mem);
            QDockWidget* stack_dock = dock_of_panel(fx.dbg(), fx.dbg()->stack_panel());
            if (stack_dock) stack_dock->raise();    // Stack
            DisasmPanel* dis = fx.dbg()->disasm_panel();
            dis->show_address(PROG);                // the caret on the LD
            dis->follow();
            check("QNAVW-04", desc,
                  combo && mem_dock && combo->currentIndex() == 0 && mem->selected_address() == 0x5C40 &&
                      dock_group_shown_title(mem_dock) == QStringLiteral("Memory"),
                  fmt("view %d selected %04X tab is memory %d", combo ? combo->currentIndex() : -1,
                      mem->selected_address(), mem_dock ? dock_group_shown_title(mem_dock) == QStringLiteral("Memory") : 0));
        }
    }

    {
        Fixture fx;
        const char* desc = "a click on PC in the window's CPU panel moves the Disassembly to PC and records it; Back "
                           "returns the view to where it was";
        if (!fx.ok) { check("QNAVW-05", desc, false, "fixture"); }
        else {
            fx.load(0x6000, {0x00, 0x00, 0x18, 0xFE});
            fx.regs(0x6000);
            fx.enable();
            fx.dbg()->show();
            QApplication::processEvents();
            DisasmPanel* dis = fx.dbg()->disasm_panel();
            dis->show_address(0x2000);   // somewhere else (a recorded jump of its own)
            const uint16_t before = dis->view_address();
            QLabel* pc = window_reg_label(fx, "PC: ");
            if (pc) {
                const QPoint pos(2, 2);
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), QPointF(pc->mapToGlobal(pos)),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(pc, &press);
            }
            const uint16_t at_pc = dis->view_address();
            const bool recorded = dis->can_go_back();
            const bool caret_at_pc = dis->selected_address() == 0x6000;
            const bool back = dis->go_back() && dis->view_address() == before;
            check("QNAVW-05", desc, pc && at_pc != before && caret_at_pc && recorded && back,
                  fmt("before %04X after click %04X recorded=%d back=%d", before, at_pc, recorded, back));
        }
    }

    {
        Fixture fx;
        const char* desc = "the window's Back and Forward are enabled per the history: both off at the start, Back "
                           "after a Follow, Forward after a Back";
        if (!fx.ok) { check("QNAVW-06", desc, false, "fixture"); }
        else {
            fx.load(PROG, {0xCD, 0x00, 0x90, 0x18, 0xFB});
            fx.regs(PROG);
            fx.enable();
            QAction* back = nullptr;
            QAction* fwd = nullptr;
            for (QAction* a : fx.dbg()->findChildren<QAction*>()) {
                if (a->text() == QLatin1String("Back")) back = a;
                if (a->text() == QLatin1String("Forward")) fwd = a;
            }
            DisasmPanel* dis = fx.dbg()->disasm_panel();
            const bool start = back && fwd && !back->isEnabled() && !fwd->isEnabled();
            dis->show_address(PROG);
            dis->follow();
            const bool after_follow = back && fwd && back->isEnabled() && !fwd->isEnabled();
            dis->go_back();
            dis->go_back();   // the show_address() above recorded the view it left, too
            const bool after_back = back && fwd && !back->isEnabled() && fwd->isEnabled();
            check("QNAVW-06", desc, start && after_follow && after_back,
                  fmt("start=%d follow=%d back=%d", start, after_follow, after_back));
        }
    }

    {
        Fixture fx;
        const char* desc = "Go > Symbols raises the Symbols tab and puts the keyboard in its filter box";
        if (!fx.ok) { check("QNAVW-07", desc, false, "fixture"); }
        else {
            fx.enable();
            fx.dbg()->show();
            QApplication::processEvents();
            QAction* sym = nullptr;
            for (QAction* top : fx.dbg()->menuBar()->actions())
                if (top->menu() && top->text().remove(QLatin1Char('&')) == QLatin1String("Go"))
                    for (QAction* a : top->menu()->actions())
                        if (a->text().remove(QLatin1Char('&')) == QLatin1String("Symbols")) sym = a;
            SymbolsPanel* sp = fx.dbg()->symbols_panel();
            QDockWidget* sym_dock = dock_of_panel(fx.dbg(), sp);   // GH #377: a dock, not a tab page
            QDockWidget* video_dock = dock_of_panel(fx.dbg(), fx.dbg()->findChild<VideoPanel*>());
            if (video_dock) video_dock->raise();                   // Video, the first tab
            if (sym) sym->trigger();
            QLineEdit* filter = sp ? sp->findChild<QLineEdit*>() : nullptr;
            check("QNAVW-07", desc,
                  sym && sym_dock && filter && dock_group_shown_title(sym_dock) == QStringLiteral("Symbols") &&
                      fx.dbg()->focusWidget() == filter,
                  fmt("action=%d tab raised=%d filter focused=%d", sym != nullptr,
                      sym_dock && dock_group_shown_title(sym_dock) == QStringLiteral("Symbols"),
                      filter && fx.dbg()->focusWidget() == filter));
        }
    }
}


// ===========================================================================
// QBPX / QRTI -- GH #372: the Breakpoints panel's Interrupt and NMI types, and
// Debug > Run to Next Interrupt.
//
// Oracles: the backend's own rows WK-INT-01 / WK-NMI-01 (an IntAck stop lands on
// PC 0x0038 in IM 1, an NMI on 0x0066: Zilog UM0080) and the machine itself:
// each row is asserted by where a REAL 48K stops, never by reading a
// subscription back. The panel's cells are read as the user sees them.
// ===========================================================================
namespace {

struct BpoFix {
    std::unique_ptr<Fixture> own;
    Fixture& fx;
    explicit BpoFix(bool paused = false, MachineType t = MachineType::ZX48K)
        : own(std::make_unique<Fixture>(t, 0, paused)), fx(*own) {}
    BreakpointModel& model() { return fx.mgr->breakpoints(); }
    BreakpointPanel* panel() { return fx.dbg()->breakpoint_panel(); }
    QTableWidget* table() { return panel()->findChild<QTableWidget*>(); }
    QString cell(int row, int col) { return table_cell(panel(), row, col); }
    /// The post-frames sequence of the real loop: the frame, THEN the backend's pump (which delivers
    /// a stop to the listeners), then the manager's check and the panels.
    bool primed = false;
    void tick() {
        // The backend's first pump only primes its push edge detector, so a stop reached before any pump
        // would never be pushed. The real loop has pumped on every tick since start-up: prime first.
        if (!primed) { fx.backend->pump(jnext::dbg::PumpBudget{}); primed = true; }
        if (!fx.emu.debug_state().paused()) fx.emu.run_frame();
        fx.backend->pump(jnext::dbg::PumpBudget{});
        fx.mgr->check_breakpoint_hit();
        fx.mgr->refresh_panels();
    }
    bool tick_until_paused(int max_ticks = 8) {
        for (int i = 0; i < max_ticks && !fx.paused(); ++i) tick();
        return fx.paused();
    }
    /// The first backend subscription (any owner, transients included) that `pred` accepts.
    bool any_sub(const std::function<bool(const jnext::dbg::SubscriptionInfo&)>& pred) {
        for (const auto& si : fx.backend->subscriptions(true))
            if (pred(si)) return true;
        return false;
    }
    bool any_intack() {
        return any_sub([](const jnext::dbg::SubscriptionInfo& si) { return si.kind == jnext::dbg::EventKind::IntAck; });
    }
};

// What the Add / Edit dialog showed and did while `drive_bp_dialog` drove it.
struct BpoDlg {
    bool        opened = false;
    QStringList items;                    // the Type combo's texts, in order
    int         seed_type = -1;           // the combo's index when it opened
    QString     seed_addr;
    bool        seed_count = false;
    QString     seed_to, seed_mask, seed_cond;   // GH #372 stage 3: the optional fields as the dialog opened
    bool        addr_enabled_after = true;   // the address field's state once `act` ran
    QString     error;                    // the reason shown when OK was refused
};

BpoDlg drive_bp_dialog(const std::function<void(QDialog*)>& act, const std::function<void()>& open,
                       bool accept = true) {
    BpoDlg r;
    int n = 0;
    QTimer timer;
    QElapsedTimer since;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg || !dlg->isVisible()) { since.invalidate(); return; }
        if (!since.isValid()) since.start();
        else if (since.elapsed() > 20000) { dlg->reject(); return; }
        auto* combo = dlg->findChild<QComboBox*>();
        auto* addr  = dlg->findChild<QLineEdit*>(QStringLiteral("bp_dialog_address"));
        auto* cnt   = dlg->findChild<QCheckBox*>(QStringLiteral("bp_dialog_count_only"));
        if (n++ == 0) {
            r.opened = true;
            if (combo) {
                for (int i = 0; i < combo->count(); ++i) r.items << combo->itemText(i);
                r.seed_type = combo->currentIndex();
            }
            if (addr) r.seed_addr = addr->text();
            r.seed_count = cnt && cnt->isChecked();
            if (auto* e = dlg->findChild<QLineEdit*>(QStringLiteral("bp_dialog_to"))) r.seed_to = e->text();
            if (auto* e = dlg->findChild<QLineEdit*>(QStringLiteral("bp_dialog_mask"))) r.seed_mask = e->text();
            if (auto* e = dlg->findChild<QLineEdit*>(QStringLiteral("bp_dialog_condition"))) r.seed_cond = e->text();
            if (act) act(dlg);
            r.addr_enabled_after = addr && addr->isEnabled();
            if (accept) dlg->accept(); else dlg->reject();
            return;
        }
        // The dialog is up again: OK was refused. Take the reason and give up.
        auto* err = dlg->findChild<QLabel*>(QStringLiteral("bp_dialog_error"));
        r.error = err ? err->text() : QString();
        dlg->reject();
    });
    timer.start(1);
    open();
    timer.stop();
    QApplication::processEvents();
    return r;
}

/// Make the dialog say: type index `type` (-1 = keep), address text (null = keep), count-only (-1 = keep).
std::function<void(QDialog*)> fill(int type, const QString& addr = QString(), int count = -1) {
    return [=](QDialog* d) {
        if (type >= 0)
            if (auto* c = d->findChild<QComboBox*>()) c->setCurrentIndex(type);
        if (!addr.isNull())
            if (auto* a = d->findChild<QLineEdit*>(QStringLiteral("bp_dialog_address"))) a->setText(addr);
        if (count >= 0)
            if (auto* k = d->findChild<QCheckBox*>(QStringLiteral("bp_dialog_count_only"))) k->setChecked(count != 0);
    };
}

/// `fill()` with the optional fields: To, Mask and Condition (null = leave as seeded).
std::function<void(QDialog*)> fill3(int type, const QString& addr, const QString& to, const QString& mask,
                                    const QString& cond, int count = -1) {
    const auto base = fill(type, addr, count);
    return [=](QDialog* d) {
        base(d);
        const std::pair<const char*, QString> f[] = {{"bp_dialog_to", to}, {"bp_dialog_mask", mask}, {"bp_dialog_condition", cond}};
        for (const auto& p : f)
            if (!p.second.isNull())
                if (auto* e = d->findChild<QLineEdit*>(QString::fromLatin1(p.first))) e->setText(p.second);
    };
}

/// Another client's subscription, made and pushed as a tick's pump would.
jnext::dbg::ClientId other_client_sub(Fixture& fx, BreakpointModel& m, const jnext::dbg::Subscription& sub) {
    jnext::dbg::ClientInfo oi;
    oi.name = "other";
    oi.kind = jnext::dbg::ClientKind::Test;
    const jnext::dbg::ClientId other = fx.backend->attach(oi).value;
    fx.backend->pump(jnext::dbg::PumpBudget{});   // settle the attach: the first pump only primes
    fx.backend->subscribe(other, sub);
    fx.backend->pump(jnext::dbg::PumpBudget{});
    m.sync();
    return other;
}

// 8000  FB        EI
// 8001  76        HALT
// 8002  18 FD     JR $8001
const std::initializer_list<uint8_t> kEiHalt = {0xFB, 0x76, 0x18, 0xFD};

void regs_im1(Fixture& fx, uint16_t pc) {
    fx.regs(pc, [](Z80Registers& r) { r.IM = 1; });
}

}  // namespace

static void test_bp_interrupt() {
    set_group("QBPX");
    using BM = BreakpointModel;
    {   // QBPX-01
        BpoFix g(/*paused=*/true);
        const char* desc = "the Add dialog's Type combo holds the nine names in order (the first six unchanged, Audio Write last); choosing "
                           "Interrupt turns the address field off; OK adds one own row Interrupt with an empty Address: a "
                           "static IntAck Stop with no handler or condition";
        if (!g.fx.ok) { check("QBPX-01", desc, false, "fixture"); }
        else {
            g.fx.enable();
            const BpoDlg d = drive_bp_dialog(fill(BM::Interrupt), [&] { g.panel()->on_add(); });
            const QStringList want = {"Execute", "Read", "Write", "Read/Write", "IO Read", "IO Write", "Interrupt", "NMI", "Audio Write"};   // GH #532: grown by the new type
            bool sub_ok = false;
            if (g.model().rows().size() == 1)
                for (const auto& si : g.fx.backend->subscriptions(false))
                    if (si.id == g.model().rows()[0].id)
                        sub_ok = si.kind == jnext::dbg::EventKind::IntAck && si.action == jnext::dbg::Action::Stop &&
                                 !si.has_handler && !si.has_condition && si.owner == g.model().client();
            const bool row_ok = g.model().rows().size() == 1 && g.model().rows()[0].own &&
                                g.model().rows()[0].type == BM::Interrupt &&
                                g.model().rows()[0].type_text == QString::fromLatin1(jnext::script::bp_kind_name(jnext::script::BpSpec::Interrupt)) &&
                                g.model().rows()[0].addr_text.isEmpty();
            check("QBPX-01", desc,
                  d.opened && d.items == want && !d.addr_enabled_after && row_ok && sub_ok &&
                      g.cell(0, 1) == "Interrupt" && g.cell(0, 2).isEmpty(),
                  fmt("opened=%d items=%s addr_enabled=%d row_ok=%d sub_ok=%d cells='%s'|'%s'", d.opened,
                      s(d.items.join(",")).c_str(), d.addr_enabled_after, row_ok, sub_ok, s(g.cell(0, 1)).c_str(),
                      s(g.cell(0, 2)).c_str()));
        }
    }
    {   // QBPX-02
        BpoFix g;
        const char* desc = "on a real 48K (IM 1, EI, HALT loop) an Interrupt breakpoint pauses at PC 0x0038, and a Run pauses "
                           "there again at the NEXT frame interrupt";
        if (!g.fx.ok) { check("QBPX-02", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kEiHalt);
            regs_im1(g.fx, PROG);
            g.fx.enable();
            g.model().add(BM::Interrupt, 0);
            const bool first = g.tick_until_paused();
            const uint16_t pc1 = g.fx.pc();
            const uint64_t c1 = g.fx.emu.clock().get();
            // Back into the HALT loop with interrupts on: the handler at 0x0038 is the ROM's, which
            // this fixture may not hold (0xFF fill = RST 38h forever), so the row does not rely on it.
            g.fx.regs(PROG + 1, [](Z80Registers& r) { r.IM = 1; r.IFF1 = 1; r.IFF2 = 1; });
            g.fx.mgr->on_run();
            const bool second = g.tick_until_paused();
            const uint64_t c2 = g.fx.emu.clock().get();
            check("QBPX-02", desc,
                  first && pc1 == 0x0038 && second && g.fx.pc() == 0x0038 &&
                      c2 - c1 >= g.fx.emu.timing().master_cycles_per_frame / 2,
                  fmt("first=%d pc=%04X second=%d pc=%04X cycles=%llu", first, pc1, second, g.fx.pc(),
                      static_cast<unsigned long long>(c2 - c1)));
        }
    }
    {   // QBPX-03
        BpoFix g;
        const char* desc = "an NMI breakpoint pauses at PC 0x0066 after the Multiface button is pressed";
        if (!g.fx.ok) { check("QBPX-03", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, {0x00, 0x18, 0xFD});
            g.fx.regs(PROG);
            g.fx.enable();
            g.model().add(BM::Nmi, 0);
            g.fx.emu.nmi_source().set_mf_enable(true);
            g.fx.backend->press_nmi(g.model().client(), jnext::dbg::NmiButton::Mf);
            const bool hit = g.tick_until_paused();
            check("QBPX-03", desc, hit && g.fx.pc() == 0x0066, fmt("paused=%d pc=%04X", hit, g.fx.pc()));
        }
    }
    {   // QBPX-04
        BpoFix g(/*paused=*/true);
        const char* desc = "another client's IntAck Stop lists as `Interrupt (client N)` with an empty Address and a read-only "
                           "On cell: the text is what it was before the type existed";
        if (!g.fx.ok) { check("QBPX-04", desc, false, "fixture"); }
        else {
            g.fx.enable();
            jnext::dbg::Subscription sub;
            sub.kind   = jnext::dbg::EventKind::IntAck;
            sub.action = jnext::dbg::Action::Stop;
            const jnext::dbg::ClientId other = other_client_sub(g.fx, g.model(), sub);
            g.fx.mgr->refresh_panels();
            const QString want = QStringLiteral("Interrupt (client %1)").arg(other);
            const bool one = g.model().rows().size() == 1;
            const bool row_ok = one && !g.model().rows()[0].own && g.model().rows()[0].type_text == want &&
                                g.model().rows()[0].addr_text.isEmpty();
            QTableWidgetItem* on = g.table() ? g.table()->item(0, 0) : nullptr;
            check("QBPX-04", desc,
                  row_ok && g.cell(0, 1) == want && g.cell(0, 2).isEmpty() && on &&
                      !(on->flags() & Qt::ItemIsUserCheckable),
                  fmt("rows=%d type='%s' addr='%s'", int(g.model().rows().size()),
                      one ? s(g.model().rows()[0].type_text).c_str() : "", one ? s(g.model().rows()[0].addr_text).c_str() : ""));
        }
    }
    {   // QBPX-05
        BpoFix g(/*paused=*/true);
        const char* desc = "Edit on an Interrupt row seeds the Type with Interrupt; changing it to NMI replaces the row and "
                           "carries the disabled On state";
        if (!g.fx.ok) { check("QBPX-05", desc, false, "fixture"); }
        else {
            g.fx.enable();
            g.model().add(BM::Interrupt, 0);
            g.model().set_enabled(BM::Interrupt, 0, false);
            g.table()->setCurrentCell(0, 1);
            const BpoDlg d = drive_bp_dialog(fill(BM::Nmi), [&] { g.panel()->on_edit(); });
            check("QBPX-05", desc,
                  d.opened && d.seed_type == BM::Interrupt && g.model().rows().size() == 1 &&
                      g.model().rows()[0].type == BM::Nmi && !g.model().rows()[0].enabled && g.model().rows()[0].own,
                  fmt("seed=%d rows=%d type=%d enabled=%d", d.seed_type, int(g.model().rows().size()),
                      g.model().rows().empty() ? -2 : g.model().rows()[0].type,
                      g.model().rows().empty() ? -1 : g.model().rows()[0].enabled));
        }
    }
}

static void test_run_to_interrupt() {
    set_group("QRTI");
    using BM = BreakpointModel;
    const char* kItem = "Run to Next Interrupt";
    {   // QRTI-01
        BpoFix g(/*paused=*/true);
        const char* desc = "the Debug menu holds Run to Next Interrupt right after Run to End of Scan Line, with no shortcut "
                           "by default, and its keymap id is run_to_interrupt";
        if (!g.fx.ok) { check("QRTI-01", desc, false, "fixture"); }
        else {
            g.fx.enable();
            QMenu* debug = menu_named(g.fx.dbg()->menuBar(), "Debug");
            int eosl = -1, rti = -1, i = 0;
            QAction* a = nullptr;
            if (debug)
                for (QAction* act : debug->actions()) {
                    if (visible(act->text()) == "Run to End of Scan Line") eosl = i;
                    if (visible(act->text()) == kItem) { rti = i; a = act; }
                    ++i;
                }
            check("QRTI-01", desc,
                  eosl >= 0 && rti == eosl + 1 && a && a->shortcuts().isEmpty() &&
                      std::string(jnext::dbgkeys::info(jnext::dbgkeys::Action::RunToInterrupt).id) == "run_to_interrupt",
                  fmt("eosl=%d rti=%d", eosl, rti));
        }
    }
    {   // QRTI-02
        BpoFix g(/*paused=*/true);
        const char* desc = "paused on a 48K with EI and a HALT loop, the action runs the machine and pauses at PC 0x0038; "
                           "afterwards no IntAck subscription is left, and the panel never listed one";
        if (!g.fx.ok) { check("QRTI-02", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kEiHalt);
            regs_im1(g.fx, PROG);
            g.fx.enable();
            QAction* a = debug_item(g.fx.dbg(), kItem);
            if (a) a->trigger();
            const bool ran = !g.fx.paused();
            bool listed = false;
            for (int i = 0; i < 8 && !g.fx.paused(); ++i) {
                g.tick();
                listed = listed || !g.model().rows().empty() || g.table()->rowCount() != 0;
            }
            check("QRTI-02", desc,
                  a && ran && g.fx.paused() && g.fx.pc() == 0x0038 && !g.any_intack() && !listed,
                  fmt("ran=%d paused=%d pc=%04X armed=%d listed=%d", ran, g.fx.paused(), g.fx.pc(), g.any_intack(), listed));
        }
    }
    {   // QRTI-03
        BpoFix g(/*paused=*/true);
        const char* desc = "with the master switch off the action still pauses at 0x0038 (a transient is exempt from it)";
        if (!g.fx.ok) { check("QRTI-03", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kEiHalt);
            regs_im1(g.fx, PROG);
            g.fx.enable();
            g.model().set_master_enabled(false);
            QAction* a = debug_item(g.fx.dbg(), kItem);
            if (a) a->trigger();
            g.tick_until_paused();
            check("QRTI-03", desc, a && g.fx.paused() && g.fx.pc() == 0x0038 && !g.model().master_enabled(),
                  fmt("paused=%d pc=%04X", g.fx.paused(), g.fx.pc()));
        }
    }
    {   // QRTI-04
        BpoFix g(/*paused=*/true);
        const char* desc = "an own Execute breakpoint reached before the interrupt stops there first; nothing IntAck stays "
                           "armed, and a following Run does not stop at 0x0038";
        if (!g.fx.ok) { check("QRTI-04", desc, false, "fixture"); }
        else {
            // 8000 EI; 8001 NOP; 8002 NOP (breakpoint); 8003 HALT; 8004 JR 8003
            g.fx.load(PROG, {0xFB, 0x00, 0x00, 0x76, 0x18, 0xFD});
            regs_im1(g.fx, PROG);
            g.fx.enable();
            g.model().add(BM::Execute, 0x8002);
            QAction* a = debug_item(g.fx.dbg(), kItem);
            if (a) a->trigger();
            g.tick_until_paused();
            const bool at_bp = g.fx.paused() && g.fx.pc() == 0x8002;
            const bool clean = !g.any_intack();
            g.fx.mgr->on_run();
            for (int i = 0; i < 4; ++i) g.tick();
            check("QRTI-04", desc, a && at_bp && clean && !g.fx.paused(),
                  fmt("at_bp=%d pc=%04X clean=%d paused-after-run=%d", at_bp, g.fx.pc(), clean, g.fx.paused()));
        }
    }
    {   // QRTI-05
        BpoFix g(/*paused=*/false);
        const char* desc = "on a running machine the action is a no-op (nothing armed, still running); an explicit Pause right "
                           "after arming leaves no transient IntAck behind";
        if (!g.fx.ok) { check("QRTI-05", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kEiHalt);
            regs_im1(g.fx, PROG);
            g.fx.enable();
            QAction* a = debug_item(g.fx.dbg(), kItem);
            if (a) a->trigger();
            const bool noop = !g.any_intack() && !g.fx.paused();
            g.fx.mgr->on_pause();
            // Arm from the paused machine, then pause before any interrupt has come.
            if (a) a->trigger();
            g.fx.mgr->on_pause();
            const bool dropped = !g.any_intack() && g.fx.paused();
            check("QRTI-05", desc, a && noop && dropped, fmt("noop=%d dropped=%d armed=%d", noop, dropped, g.any_intack()));
        }
    }
}


// ===========================================================================
// QBPX (stage 2) -- GH #372: hit counts, Reset Hits and count-only breakpoints.
//
// Oracles: the engine's own `RuleView::hits` for the same rule on the same
// Debugger (QBPX-15), and loop counts fixed by the test program: the loop
//     8000  06 05     LD B,5
//     8002  00        NOP            <- the breakpoint
//     8003  10 FD     DJNZ $8002
//     8005  18 FE     JR $           <- the end-of-program sentinel
// runs the NOP exactly five times (B = 5 down to 0).
// ===========================================================================
namespace {

const std::initializer_list<uint8_t> kLoop = {0x06, 0x05, 0x00, 0x10, 0xFD, 0x18, 0xFE};

jnext::script::BpSpec spec_of(BreakpointModel::Type k, uint16_t addr, bool count_only = false) {
    jnext::script::BpSpec s;
    s.kind       = static_cast<jnext::script::BpSpec::Kind>(k);
    s.addr       = addr;
    s.count_only = count_only;
    return s;
}

/// The table row whose Type cell starts with `prefix` and whose Address cell is `addr` (null = any).
int row_of(BpoFix& g, const QString& prefix, const QString& addr = QString()) {
    for (int i = 0; i < g.table()->rowCount(); ++i)
        if (g.cell(i, 1).startsWith(prefix) && (addr.isNull() || g.cell(i, 2) == addr)) return i;
    return -1;
}

/// The model's own row of the given type and address (by value, for `hits()`).
const BreakpointModel::Row* own_row(BpoFix& g, int type, uint16_t addr, bool count_only = false) {
    for (const auto& r : g.model().rows())
        if (r.own && r.type == type && r.addr == addr && r.count_only == count_only) return &r;
    return nullptr;
}

/// Rewind the program to its start (PC 8000, B reloaded by the first instruction) and Run.
bool rerun(BpoFix& g) {
    g.fx.regs(PROG);
    g.fx.mgr->on_run();
    return g.tick_until_paused();
}

}  // namespace

static void test_bp_hits() {
    set_group("QBPX");
    using BM = BreakpointModel;
    {   // QBPX-06
        BpoFix g(/*paused=*/true);
        const char* desc = "the Breakpoints table's headers are On, Type, Address, Symbol / Source, Hits, Condition; columns 0-3 keep "
                           "their meaning";
        if (!g.fx.ok) { check("QBPX-06", desc, false, "fixture"); }
        else {
            g.fx.enable();
            g.model().add(BM::Execute, 0x8000);
            QStringList heads;
            for (int c = 0; c < g.table()->columnCount(); ++c)
                heads << (g.table()->horizontalHeaderItem(c) ? g.table()->horizontalHeaderItem(c)->text() : QString());
            QTableWidgetItem* on = g.table()->item(0, 0);
            check("QBPX-06", desc,
                  heads == QStringList({"On", "Type", "Address", "Symbol / Source", "Hits", "Condition"}) && on &&
                      on->checkState() == Qt::Checked && g.cell(0, 1) == "Execute" && g.cell(0, 2) == "$8000" &&
                      g.cell(0, 4) == "0",
                  s(heads.join("|")) + " " + s(g.cell(0, 1)) + " " + s(g.cell(0, 2)));
        }
    }
    {   // QBPX-07
        BpoFix g;
        const char* desc = "a Stop Execute breakpoint inside a DJNZ loop of B = 5: five Runs give five stops, and the Hits "
                           "cell reads 1..5 after each";
        if (!g.fx.ok) { check("QBPX-07", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kLoop);
            g.fx.regs(PROG);
            g.fx.enable();
            g.model().add(BM::Execute, 0x8002);
            std::string seen;
            bool ok = true;
            for (int n = 1; n <= 5; ++n) {
                const bool hit = g.tick_until_paused();
                ok = ok && hit && g.fx.pc() == 0x8002 && g.cell(0, 4) == QString::number(n);
                seen += s(g.cell(0, 4)) + " ";
                g.fx.mgr->on_run();
            }
            for (int i = 0; i < 3; ++i) g.tick();
            check("QBPX-07", desc, ok && !g.fx.paused(), "hits seen: " + seen + (g.fx.paused() ? " (stopped a sixth time)" : ""));
        }
    }
    {   // QBPX-08
        BpoFix g;
        const char* desc = "the same loop with the breakpoint count-only: the run never stops at it; at the end-of-program "
                           "stop Hits is 5; the row is action Continue with a handler and its Type reads `Execute (count only)`";
        if (!g.fx.ok) { check("QBPX-08", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kLoop);
            g.fx.regs(PROG);
            g.fx.enable();
            g.model().add(spec_of(BM::Execute, 0x8002, true));
            g.model().add(BM::Execute, 0x8005);
            const bool hit = g.tick_until_paused();
            const auto* r = own_row(g, BM::Execute, 0x8002, true);
            bool sub_ok = false;
            if (r)
                for (const auto& si : g.fx.backend->subscriptions(false))
                    if (si.id == r->id) sub_ok = si.action == jnext::dbg::Action::Continue && si.has_handler;
            check("QBPX-08", desc,
                  hit && g.fx.pc() == 0x8005 && r && g.model().hits(*r) == 5 && g.cell(0, 4) == "5" &&
                      g.cell(0, 1) == "Execute (count only)" && sub_ok,
                  fmt("paused=%d pc=%04X hits=%llu type='%s' sub_ok=%d", hit, g.fx.pc(),
                      r ? static_cast<unsigned long long>(g.model().hits(*r)) : 0ull, s(g.cell(0, 1)).c_str(), sub_ok));
        }
    }
    {   // QBPX-09
        BpoFix g;
        BpoFix h;
        const char* desc = "a count-only Read on an address the program reads 3 times gives 3; a count-only IO Write on 0xFE "
                           "with 4 `OUT (0xFE),A` gives 4";
        if (!g.fx.ok || !h.fx.ok) { check("QBPX-09", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, {0x3A, 0x00, 0x90, 0x3A, 0x00, 0x90, 0x3A, 0x00, 0x90, 0x18, 0xFE});
            g.fx.regs(PROG);
            g.fx.enable();
            g.model().add(spec_of(BM::Read, 0x9000, true));
            g.model().add(BM::Execute, 0x8009);
            g.tick_until_paused();
            const auto* rr = own_row(g, BM::Read, 0x9000, true);
            const uint64_t reads = rr ? g.model().hits(*rr) : 99;
            h.fx.load(PROG, {0xD3, 0xFE, 0xD3, 0xFE, 0xD3, 0xFE, 0xD3, 0xFE, 0x18, 0xFE});
            h.fx.regs(PROG);
            h.fx.enable();
            h.model().add(spec_of(BM::IoWrite, 0x00FE, true));
            h.model().add(BM::Execute, 0x8008);
            h.tick_until_paused();
            const auto* ro = own_row(h, BM::IoWrite, 0x00FE, true);
            const uint64_t outs = ro ? h.model().hits(*ro) : 99;
            check("QBPX-09", desc,
                  g.fx.pc() == 0x8009 && reads == 3 && h.fx.pc() == 0x8008 && outs == 4,
                  fmt("read: pc=%04X hits=%llu; io: pc=%04X hits=%llu", g.fx.pc(), static_cast<unsigned long long>(reads),
                      h.fx.pc(), static_cast<unsigned long long>(outs)));
        }
    }
    {   // QBPX-10
        BpoFix g;
        const char* desc = "Reset Hits zeroes every own row and counting restarts from 0; another client's row has an empty "
                           "Hits cell";
        if (!g.fx.ok) { check("QBPX-10", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kLoop);
            g.fx.regs(PROG);
            g.fx.enable();
            g.model().add(BM::Execute, 0x8002);
            g.model().add(BM::Execute, 0x8003);
            jnext::dbg::Subscription sub;
            sub.kind = jnext::dbg::EventKind::Execute;
            sub.filter.lo = sub.filter.hi = 0x8002;   // the same address: the other client's stops too, and its row must show no count
            sub.action = jnext::dbg::Action::Stop;
            other_client_sub(g.fx, g.model(), sub);
            // Four stops: 8002, 8003, 8002, 8003.
            for (int n = 0; n < 4; ++n) {
                g.tick_until_paused();
                if (n < 3) g.fx.mgr->on_run();
            }
            const int ra = row_of(g, "Execute", "$8002"), rb = row_of(g, "Execute", "$8003");
            int ro = -1;
            for (int i = 0; i < g.table()->rowCount(); ++i)
                if (g.cell(i, 1).contains("(client")) ro = i;
            const QString before_a = ra >= 0 ? g.cell(ra, 4) : "?", before_b = rb >= 0 ? g.cell(rb, 4) : "?";
            const QString other = ro >= 0 ? g.cell(ro, 4) : "?";
            bool other_zero = false;
            for (const auto& r : g.model().rows())
                if (!r.own && r.type_text.contains("(client")) other_zero = g.model().hits(r) == 0;
            QPushButton* reset = button_ending(g.panel(), "Reset Hits");
            if (reset) reset->click();
            const QString after_a = g.cell(ra, 4), after_b = g.cell(rb, 4);
            const auto* rzero = own_row(g, BM::Execute, 0x8002);
            const bool model_zero = rzero && g.model().hits(*rzero) == 0;
            g.fx.mgr->on_run();
            g.tick_until_paused();   // 8002 again
            const QString again = g.cell(ra, 4);
            check("QBPX-10", desc,
                  reset && before_a == "2" && before_b == "2" && other.isEmpty() && other_zero && after_a == "0" && after_b == "0" &&
                      model_zero && again == "1",
                  s(before_a) + "," + s(before_b) + " other='" + s(other) + "' after " + s(after_a) + "," + s(after_b) +
                      " again " + s(again));
        }
    }
}

static void test_bp_hits_more() {
    set_group("QBPX");
    using BM = BreakpointModel;
    {   // QBPX-11
        BpoFix g;
        const char* desc = "a count survives an On toggle; a disabled breakpoint does not count; with the master switch off "
                           "nothing counts; Edit starts again from 0";
        if (!g.fx.ok) { check("QBPX-11", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kLoop);
            g.fx.regs(PROG);
            g.fx.enable();
            g.model().add(spec_of(BM::Execute, 0x8002, true));
            g.model().add(BM::Execute, 0x8005);   // the end-of-program sentinel
            auto hits = [&]() -> uint64_t {
                const auto* r = own_row(g, BM::Execute, 0x8002, true);
                return r ? g.model().hits(*r) : 9999;
            };
            const jnext::dbg::MemSpace cpu = jnext::dbg::MemSpace::cpu();
            g.tick_until_paused();
            const uint64_t first = hits();
            g.model().set_enabled(BM::Execute, 0x8002, false, cpu, true);
            g.model().set_enabled(BM::Execute, 0x8002, true, cpu, true);
            const uint64_t kept = hits();
            g.model().set_enabled(BM::Execute, 0x8002, false, cpu, true);
            const bool off_stop = rerun(g) && g.fx.pc() == 0x8005;
            const uint64_t while_off = hits();
            g.model().set_enabled(BM::Execute, 0x8002, true, cpu, true);
            const bool on_stop = rerun(g) && g.fx.pc() == 0x8005;
            const uint64_t on_again = hits();
            g.model().set_master_enabled(false);
            g.fx.regs(PROG);
            g.fx.mgr->on_run();
            for (int i = 0; i < 3; ++i) g.tick();
            const bool ran_free = !g.fx.paused();
            const uint64_t master_off = hits();
            g.fx.mgr->on_pause();
            g.model().set_master_enabled(true);
            g.table()->setCurrentCell(row_of(g, "Execute (count only)"), 1);
            const BpoDlg d = drive_bp_dialog(nullptr, [&] { g.panel()->on_edit(); });
            const uint64_t edited = hits();
            check("QBPX-11", desc,
                  first == 5 && kept == 5 && off_stop && while_off == 5 && on_stop && on_again == 10 && ran_free &&
                      master_off == 10 && d.opened && d.seed_count && edited == 0,
                  fmt("first=%llu kept=%llu off_stop=%d while_off=%llu on_stop=%d on_again=%llu ran_free=%d master_off=%llu "
                      "edit: opened=%d seed=%d hits=%llu",
                      static_cast<unsigned long long>(first), static_cast<unsigned long long>(kept), off_stop,
                      static_cast<unsigned long long>(while_off), on_stop, static_cast<unsigned long long>(on_again), ran_free,
                      static_cast<unsigned long long>(master_off), d.opened, d.seed_count,
                      static_cast<unsigned long long>(edited)));
        }
    }
    {   // QBPX-12
        BpoFix g(/*paused=*/false, MachineType::ZXN_ISSUE2);
        const char* desc = "a source-line group compiled at two places, each executed once, reads Hits 2 (one row)";
        if (!g.fx.ok) { check("QBPX-12", desc, false, "fixture"); }
        else {
            // main.bas:10 sits at $8000 and $8004 of page 4 (the Next's default $8000 page), line 11 between them so the two
            // places are two starts. The program starts one byte before so the
            // first place is not the instruction the Run steps off.
            g.fx.backend->load_source_map(write_file("bpo-group.sld",
                "|SLD.data.version|1\n"
                "main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
                "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
                "main.bas|10||0|4|32768|T|\n"
                "main.bas|11||0|4|32770|T|\n"
                "main.bas|10||0|4|32772|T|\n").c_str(), false);
            g.fx.load(PROG - 1, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
            g.fx.regs(PROG - 1);
            g.fx.enable();
            const jnext::dbg::GroupId grp = g.model().add_source_line(QStringLiteral("main.bas:10"), true);
            g.model().add(BM::Execute, 0x8005);
            g.tick_until_paused();
            const auto* gr = g.model().rows().empty() ? nullptr : &g.model().rows()[0];
            check("QBPX-12", desc,
                  grp != jnext::dbg::GROUP_NONE && gr && gr->group == grp && gr->locations == 2 && g.fx.pc() == 0x8005 &&
                      g.model().hits(*gr) == 2 && g.cell(0, 4) == "2" && g.model().rows().size() == 2,
                  fmt("group=%d locations=%d pc=%04X hits=%llu rows=%d", int(grp), gr ? gr->locations : -1, g.fx.pc(),
                      gr ? static_cast<unsigned long long>(g.model().hits(*gr)) : 0ull, int(g.model().rows().size())));
        }
    }
    {   // QBPX-13
        BpoFix g;
        const char* desc = "a burst of hits in one frame emits hits_changed() once per sync() and no changed(): no gutter "
                           "repaint per frame";
        if (!g.fx.ok) { check("QBPX-13", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kLoop);
            g.fx.regs(PROG);
            g.fx.enable();
            g.model().add(spec_of(BM::Execute, 0x8002, true));
            g.model().add(BM::Execute, 0x8005);
            int hits_signals = 0, changed_signals = 0;
            QObject::connect(&g.model(), &BreakpointModel::hits_changed, [&] { ++hits_signals; });
            QObject::connect(&g.model(), &BreakpointModel::changed, [&](uint32_t) { ++changed_signals; });
            g.fx.emu.run_frame();   // five hits and the sentinel stop
            g.fx.backend->pump(jnext::dbg::PumpBudget{});
            const int before = hits_signals;
            g.model().sync();
            const int one = hits_signals;
            g.model().sync();   // nothing new: no second emission
            const int idle = hits_signals;
            check("QBPX-13", desc, before == 0 && one == 1 && idle == 1 && changed_signals == 0,
                  fmt("before=%d after_sync=%d idle_sync=%d changed=%d", before, one, idle, changed_signals));
        }
    }
    {   // QBPX-14
        std::vector<jnext::script::BpSpec> saved;
        {
            BpjFix a;
            BreakpointModel& m = a.model();
            m.add(spec_of(BreakpointModel::Execute, 0x8000, true));
            m.add(BreakpointModel::Read, 0x9000);
            m.add(spec_of(BreakpointModel::Interrupt, 0, true));
            m.add(spec_of(BreakpointModel::IoRead, 0x00FE, true));
            m.add_source_line(QStringLiteral("main.bas:10"), true);
            saved = m.script_specs();
            a.run("Save as Script...", g_tmp->filePath(QStringLiteral("bpx-count.jds")));
        }
        BpjFix b;
        const auto seen = b.run("Load from Script...", g_tmp->filePath(QStringLiteral("bpx-count.jds")));
        bool zero = true, all_own = true;
        for (const auto& r : b.model().rows()) {
            zero = zero && b.model().hits(r) == 0;
            all_own = all_own && r.own;
        }
        int counted = 0;
        for (const auto& sp : saved) counted += sp.count_only;
        check("QBPX-14", "Save then Load in a fresh session keeps count_only on every row (an address, an IO port, "
                         "Interrupt, a source line); the loaded rows read Hits 0",
              saved.size() == 5 && counted == 4 && b.model().script_specs() == saved && b.model().rows().size() == 5 &&
                  zero && all_own && count_kind(seen, "box") == 0,
              fmt("saved=%d counted=%d got=%d rows=%d", int(saved.size()), counted, int(b.model().script_specs().size()),
                  int(b.model().rows().size())));
    }
    {   // QBPX-15
        const char* desc = "oracle cross-check: a GUI count-only Execute and a ScriptEngine `on execute X do end` on the same "
                           "Debugger count alike after a free Run and after Single Step x3 across X; a GUI Stop breakpoint "
                           "and `on execute X do stop end` count alike over three stops";
        BpoFix g;
        BpoFix h;
        if (!g.fx.ok || !h.fx.ok) { check("QBPX-15", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kLoop);
            g.fx.regs(PROG);
            g.fx.enable();
            jnext::script::ScriptEngine eng(*g.fx.backend);
            const auto lr = eng.load("on execute 0x8002 do end\n", "x.jds");
            g.model().add(spec_of(BM::Execute, 0x8002, true));
            g.model().add(BM::Execute, 0x8005);
            g.fx.mgr->on_run();
            g.tick_until_paused();
            const auto* r = own_row(g, BM::Execute, 0x8002, true);
            const uint64_t gui_run = r ? g.model().hits(*r) : 9999;
            const uint64_t eng_run = eng.rules().empty() ? 8888 : eng.rules()[0].hits;
            g.fx.regs(PROG);
            for (int i = 0; i < 3; ++i) g.fx.mgr->on_step_into();
            g.tick();
            const auto* rstep = own_row(g, BM::Execute, 0x8002, true);
            const uint64_t gui_step = rstep ? g.model().hits(*rstep) : 9999;
            const uint64_t eng_step = eng.rules().empty() ? 8888 : eng.rules()[0].hits;

            h.fx.load(PROG, kLoop);
            h.fx.regs(PROG);
            h.fx.enable();
            jnext::script::ScriptEngine eng2(*h.fx.backend);
            const auto lr2 = eng2.load("on execute 0x8002 do stop end\n", "y.jds");
            h.model().add(BM::Execute, 0x8002);
            for (int n = 0; n < 3; ++n) {
                h.tick_until_paused();
                if (n < 2) h.fx.mgr->on_run();
            }
            const auto* rs = own_row(h, BM::Execute, 0x8002);
            const uint64_t gui_stop = rs ? h.model().hits(*rs) : 9999;
            const uint64_t eng_stop = eng2.rules().empty() ? 8888 : eng2.rules()[0].hits;
            check("QBPX-15", desc,
                  lr.ok() && lr2.ok() && gui_run == eng_run && gui_run == 5 && gui_step == eng_step && gui_stop == eng_stop &&
                      gui_stop == 3,
                  fmt("run gui=%llu eng=%llu; step gui=%llu eng=%llu; stop gui=%llu eng=%llu",
                      static_cast<unsigned long long>(gui_run), static_cast<unsigned long long>(eng_run),
                      static_cast<unsigned long long>(gui_step), static_cast<unsigned long long>(eng_step),
                      static_cast<unsigned long long>(gui_stop), static_cast<unsigned long long>(eng_stop)));
        }
    }
    {   // QBPX-16
        BpoFix g(/*paused=*/true);
        const char* desc = "the count-only checkbox in the dialog creates a count-only row, and Edit seeds the checkbox";
        if (!g.fx.ok) { check("QBPX-16", desc, false, "fixture"); }
        else {
            g.fx.enable();
            const BpoDlg a = drive_bp_dialog(fill(BM::Execute, "8000", 1), [&] { g.panel()->on_add(); });
            const bool made = g.model().rows().size() == 1 && g.model().rows()[0].count_only && g.model().rows()[0].own &&
                              g.model().rows()[0].type_text == "Execute (count only)";
            g.table()->setCurrentCell(0, 1);
            const BpoDlg e = drive_bp_dialog(nullptr, [&] { g.panel()->on_edit(); }, /*accept=*/false);
            check("QBPX-16", desc, a.opened && !a.seed_count && made && e.opened && e.seed_count && g.model().rows().size() == 1,
                  fmt("opened=%d seed_before=%d made=%d edit: opened=%d seed=%d", a.opened, a.seed_count, made, e.opened,
                      e.seed_count));
        }
    }
    {   // QBPX-17
        BpoFix g(/*paused=*/true);
        const char* desc = "fast path: a plain Execute, Read, Read/Write, IO Read and Interrupt row is exactly the subscription "
                           "it was before count-only existed: no condition, no handler, action Stop, a single address (or the "
                           "port rule's mask)";
        if (!g.fx.ok) { check("QBPX-17", desc, false, "fixture"); }
        else {
            g.fx.enable();
            g.model().add(BM::Execute, 0x8000);
            g.model().add(BM::Read, 0x9000);
            g.model().add(BM::ReadWrite, 0x9001);
            g.model().add(BM::IoRead, 0x00FE);
            g.model().add(BM::Interrupt, 0);
            int seen = 0;
            std::string bad;
            for (const auto& si : g.fx.backend->subscriptions(false)) {
                if (si.owner != g.model().client()) continue;
                ++seen;
                bool ok = !si.has_condition && !si.has_handler && !si.once && si.action == jnext::dbg::Action::Stop;
                if (si.kind == jnext::dbg::EventKind::Execute || si.kind == jnext::dbg::EventKind::Mem)
                    ok = ok && si.filter.lo == si.filter.hi;
                if (si.kind == jnext::dbg::EventKind::Port)
                    ok = ok && si.filter.port_mask == jnext::script::bp_port_mask_for(si.filter.port_value);
                if (!ok) bad += fmt("[id %u kind %d] ", unsigned(si.id), int(si.kind));
            }
            check("QBPX-17", desc, seen == 5 && bad.empty(), fmt("seen=%d %s", seen, bad.c_str()));
        }
    }
}


// ===========================================================================
// QBPX (stage 3) -- GH #372: ranges, IO masks and conditions in the dialog.
//
// Oracles: the machine itself (where a REAL 48K stops), the backend's
// `EventFilter` for what a range and a port mask match, and the script
// engine's own text for a condition's run-time error.
// ===========================================================================
namespace {

jnext::script::BpSpec ranged_spec(BreakpointModel::Type k, uint16_t lo, uint16_t hi) {
    jnext::script::BpSpec s = spec_of(k, lo);
    s.hi = hi;
    return s;
}

jnext::script::BpSpec cond_spec(BreakpointModel::Type k, uint16_t a, const char* cond, bool count_only = false) {
    jnext::script::BpSpec s = spec_of(k, a, count_only);
    s.condition = cond;
    return s;
}

}  // namespace

static void test_bp_ranges() {
    set_group("QBPX");
    using BM = BreakpointModel;
    {   // QBPX-18
        BpoFix g(/*paused=*/true);
        const char* desc = "the dialog holds To, Mask and Condition, and their enabled state follows the type: To for Execute, Read, Write "
                           "and Read/Write; Mask for the IO types; Address off for Interrupt and NMI";
        if (!g.fx.ok) { check("QBPX-18", desc, false, "fixture"); }
        else {
            g.fx.enable();
            std::string seen;
            bool widgets = true;
            drive_bp_dialog([&](QDialog* d) {
                auto* a = d->findChild<QLineEdit*>("bp_dialog_address");
                auto* to = d->findChild<QLineEdit*>("bp_dialog_to");
                auto* mk = d->findChild<QLineEdit*>("bp_dialog_mask");
                auto* cd = d->findChild<QLineEdit*>("bp_dialog_condition");
                auto* cb = d->findChild<QComboBox*>();
                widgets = a && to && mk && cd && cb;
                if (!widgets) return;
                for (int t = 0; t < 8; ++t) {
                    cb->setCurrentIndex(t);
                    seen += fmt("%d%d%d ", a->isEnabled(), to->isEnabled(), mk->isEnabled());
                    widgets = widgets && cd->isEnabled();
                }
            }, [&] { g.panel()->on_add(); }, /*accept=*/false);
            // address / to / mask per type 0..7
            check("QBPX-18", desc, widgets && seen == "110 110 110 110 101 101 000 000 ", seen);
        }
    }
    {   // QBPX-19
        BpoFix g;
        BpoFix h(/*paused=*/true, MachineType::ZXN_ISSUE2);
        const char* desc = "Execute 8000 To 80FF is one row `$8000-$80FF` with a lo/hi subscription and no space, even when the address is a "
                           "symbol with a memory; on a real 48K it stops at the first instruction fetched inside the range";
        if (!g.fx.ok || !h.fx.ok) { check("QBPX-19", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kLoop);
            g.fx.regs(PROG);
            g.fx.enable();
            drive_bp_dialog(fill3(BM::Execute, "8002", "8003", QString(), QString()), [&] { g.panel()->on_add(); });
            g.tick_until_paused();
            bool sub_ok = false;
            if (g.model().rows().size() == 1)
                for (const auto& si : g.fx.backend->subscriptions(false))
                    if (si.id == g.model().rows()[0].id)
                        sub_ok = si.filter.lo == 0x8002 && si.filter.hi == 0x8003 &&
                                 si.filter.space == jnext::dbg::MemSpace::cpu() && si.action == jnext::dbg::Action::Stop;
            // A symbol with a memory ($C000 in page 20), with a To: the range is unqualified.
            h.fx.backend->load_source_map(write_file("bpo-range.sld", kBpjSld).c_str(), false);
            h.fx.enable();
            drive_bp_dialog(fill3(BM::Execute, "banked", "C0FF", QString(), QString()), [&] { h.panel()->on_add(); });
            const bool sym_ok = h.model().rows().size() == 1 && h.model().rows()[0].space == jnext::dbg::MemSpace::cpu() &&
                                h.model().rows()[0].addr_text == "$C000-$C0FF";
            check("QBPX-19", desc,
                  g.model().rows().size() == 1 && g.model().rows()[0].addr_text == "$8002-$8003" && sub_ok && g.fx.paused() &&
                      g.fx.pc() == 0x8002 && sym_ok,
                  fmt("row_text='%s' sub_ok=%d paused=%d pc=%04X sym_ok=%d rows=%d",
                      g.model().rows().empty() ? "" : s(g.model().rows()[0].addr_text).c_str(), sub_ok, g.fx.paused(),
                      g.fx.pc(), sym_ok, int(h.model().rows().size())));
        }
    }
    {   // QBPX-20
        BpoFix g;
        BpoFix h(/*paused=*/true, MachineType::ZXN_ISSUE2);
        const char* desc = "a Read range stops on a read inside it and not on one outside; a source line with a To, and a To below the address, "
                           "keep the dialog open with the reason and add nothing";
        if (!g.fx.ok || !h.fx.ok) { check("QBPX-20", desc, false, "fixture"); }
        else {
            // 8000 LD A,(9100)  8003 LD A,(9005)  8006 NOP  8007 JR $
            g.fx.load(PROG, {0x3A, 0x00, 0x91, 0x3A, 0x05, 0x90, 0x00, 0x18, 0xFE});
            g.fx.regs(PROG);
            g.fx.enable();
            drive_bp_dialog(fill3(BM::Read, "9000", "90FF", QString(), QString()), [&] { g.panel()->on_add(); });
            g.tick_until_paused();
            h.fx.backend->load_source_map(write_file("bpo-range2.sld", kBpjSld).c_str(), false);
            h.fx.enable();
            const BpoDlg line = drive_bp_dialog(fill3(BM::Execute, "main.bas:10", "C0FF", QString(), QString()),
                                                [&] { h.panel()->on_add(); });
            const BpoDlg below = drive_bp_dialog(fill3(BM::Execute, "8000", "7FFF", QString(), QString()),
                                                 [&] { h.panel()->on_add(); });
            check("QBPX-20", desc,
                  g.fx.paused() && g.fx.pc() == 0x8006 && !line.error.isEmpty() && !below.error.isEmpty() && h.model().rows().empty(),
                  fmt("paused=%d pc=%04X line_err='%s' below_err='%s' rows=%d", g.fx.paused(), g.fx.pc(), s(line.error).c_str(),
                      s(below.error).c_str(), int(h.model().rows().size())));
        }
    }
    {   // QBPX-21
        BpoFix g;
        BpoFix h;
        const char* desc = "IO Read FE with Mask FFFF is the row `$00FE/FFFF`; a real IN with BC = 00FE stops and with BC = 7FFE does not; "
                           "with an empty Mask both stop (the address rule)";
        if (!g.fx.ok || !h.fx.ok) { check("QBPX-21", desc, false, "fixture"); }
        else {
            // 8000 LD BC,7FFE  8003 IN A,(C)  8005 LD BC,00FE  8008 IN A,(C)  800A NOP  800B JR $
            const std::initializer_list<uint8_t> prog = {0x01, 0xFE, 0x7F, 0xED, 0x78, 0x01, 0xFE, 0x00, 0xED, 0x78, 0x00, 0x18, 0xFE};
            g.fx.load(PROG, prog);
            g.fx.regs(PROG);
            g.fx.enable();
            drive_bp_dialog(fill3(BM::IoRead, "FE", QString(), "FFFF", QString()), [&] { g.panel()->on_add(); });
            const bool row = g.model().rows().size() == 1 && g.model().rows()[0].addr_text == "$00FE/FFFF" &&
                             g.model().rows()[0].type_text == "IO Read";
            g.tick_until_paused();
            h.fx.load(PROG, prog);
            h.fx.regs(PROG);
            h.fx.enable();
            drive_bp_dialog(fill3(BM::IoRead, "FE", QString(), QString(), QString()), [&] { h.panel()->on_add(); });
            h.tick_until_paused();
            check("QBPX-21", desc, row && g.fx.paused() && g.fx.pc() == 0x800A && h.fx.paused() && h.fx.pc() == 0x8005,
                  fmt("row=%d masked: pc=%04X, rule: pc=%04X", row, g.fx.pc(), h.fx.pc()));
        }
    }
    {   // QBPX-22
        BpoFix g(/*paused=*/true);
        const char* desc = "a port value with bits outside the mask is refused (it can never match), and so is FFFF with port 0000; neither adds anything";
        if (!g.fx.ok) { check("QBPX-22", desc, false, "fixture"); }
        else {
            g.fx.enable();
            const BpoDlg a = drive_bp_dialog(fill3(BM::IoRead, "01FE", QString(), "00FF", QString()), [&] { g.panel()->on_add(); });
            const BpoDlg b = drive_bp_dialog(fill3(BM::IoWrite, "0000", QString(), "FFFF", QString()), [&] { g.panel()->on_add(); });
            check("QBPX-22", desc,
                  a.error.contains("never match") && !b.error.isEmpty() && g.model().rows().empty(),
                  fmt("a='%s' b='%s' rows=%d", s(a.error).c_str(), s(b.error).c_str(), int(g.model().rows().size())));
        }
    }
    {   // QBPX-23
        BpoFix g;
        const char* desc = "Execute inside the B = 5 loop with the condition `B == 2` stops exactly once, with B == 2, and Hits is 1; the "
                           "Condition cell reads `B == 2`";
        if (!g.fx.ok) { check("QBPX-23", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kLoop);
            g.fx.regs(PROG);
            g.fx.enable();
            drive_bp_dialog(fill3(BM::Execute, "8002", QString(), QString(), "B == 2"), [&] { g.panel()->on_add(); });
            const bool hit = g.tick_until_paused();
            const unsigned b = g.fx.emu.cpu().get_registers().BC >> 8;
            const QString hits = g.cell(0, 4), cond = g.cell(0, 5);
            g.fx.mgr->on_run();
            for (int i = 0; i < 3; ++i) g.tick();
            check("QBPX-23", desc, hit && g.fx.pc() != 0 && b == 2 && hits == "1" && cond == "B == 2" && !g.fx.paused(),
                  fmt("hit=%d B=%u hits='%s' cond='%s' stopped-again=%d", hit, b, s(hits).c_str(), s(cond).c_str(), g.fx.paused()));
        }
    }
    {   // QBPX-24
        BpoFix g(/*paused=*/true);
        const char* desc = "the dialog shows the reason and adds nothing for each condition the codec cannot write back: PREV on a Read, an unknown "
                           "@symbol, a syntax error, a `;` comment, a script-only form";
        if (!g.fx.ok) { check("QBPX-24", desc, false, "fixture"); }
        else {
            g.fx.enable();
            struct C { int type; const char* text; };
            const C cases[] = {{BM::Read, "PREV == 1"}, {BM::Execute, "@nosuch == 1"}, {BM::Execute, "A =="},
                               {BM::Execute, "A == 1 ; c"}, {BM::Execute, "changed(A)"}};
            std::string miss;
            for (const C& c : cases) {
                const BpoDlg d = drive_bp_dialog(fill3(c.type, "8000", QString(), QString(), c.text), [&] { g.panel()->on_add(); });
                if (!d.opened || d.error.isEmpty()) miss += std::string("[") + c.text + "] ";
            }
            check("QBPX-24", desc, miss.empty() && g.model().rows().empty(), miss);
        }
    }
    {   // QBPX-25
        BpoFix g(/*paused=*/true);
        const char* desc = "another client's conditional Execute stays typeless and read-only with its Type text as before, and its Condition cell "
                           "reads `(condition)`";
        if (!g.fx.ok) { check("QBPX-25", desc, false, "fixture"); }
        else {
            g.fx.enable();
            jnext::dbg::Subscription sub;
            sub.kind = jnext::dbg::EventKind::Execute;
            sub.filter.lo = sub.filter.hi = 0x8000;
            sub.action = jnext::dbg::Action::Stop;
            sub.condition = [](const jnext::dbg::Event&, const jnext::dbg::Debugger&) { return true; };
            const jnext::dbg::ClientId other = other_client_sub(g.fx, g.model(), sub);
            g.fx.mgr->refresh_panels();
            const bool one = g.model().rows().size() == 1;
            check("QBPX-25", desc,
                  one && g.model().rows()[0].type == -1 && !g.model().rows()[0].own &&
                      g.model().rows()[0].type_text == QStringLiteral("Execute (client %1)").arg(other) &&
                      g.cell(0, 5) == "(condition)",
                  fmt("rows=%d type=%d text='%s' cond='%s'", int(g.model().rows().size()), one ? g.model().rows()[0].type : -9,
                      one ? s(g.model().rows()[0].type_text).c_str() : "", s(g.cell(0, 5)).c_str()));
        }
    }
    {   // QBPX-26
        BpoFix g;
        const char* desc = "a condition that fails at run time (`1 / (B - B) == 0`) never stops, and its row's Condition tooltip holds the error text";
        if (!g.fx.ok) { check("QBPX-26", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kLoop);
            g.fx.regs(PROG);
            g.fx.enable();
            g.model().add(cond_spec(BM::Execute, 0x8002, "1 / (B - B) == 0"));
            g.model().add(BM::Execute, 0x8005);
            g.tick_until_paused();
            g.tick();
            QTableWidgetItem* c = g.table()->item(row_of(g, "Execute", "$8002"), 5);
            const QString tip = c ? c->toolTip() : QString("<no cell>");
            check("QBPX-26", desc, g.fx.paused() && g.fx.pc() == 0x8005 && tip.contains("division by zero"), s(tip));
        }
    }
    {   // QBPX-27
        BpoFix g(/*paused=*/true);
        const char* desc = "a range, a conditional or a count-only Execute at $8000 draws no gutter breakpoint; a gutter click there adds a plain one "
                           "(a second row), and pc_breakpoints() lists only the plain one";
        if (!g.fx.ok) { check("QBPX-27", desc, false, "fixture"); }
        else {
            g.fx.enable();
            g.model().add(ranged_spec(BM::Execute, 0x8000, 0x8010));
            g.model().add(cond_spec(BM::Execute, 0x8000, "A == 1"));
            g.model().add(spec_of(BM::Execute, 0x8000, true));
            const bool none = !g.model().pc_exists(0x8000) && !g.model().pc_marked(0x8000, jnext::dbg::MemSpace::cpu()) &&
                              g.model().pc_breakpoints().empty() && g.model().rows().size() == 3;
            // The gutter's toggle: exists() says no, so it adds.
            if (!g.model().exists(BM::Execute, 0x8000)) g.model().add(BM::Execute, 0x8000);
            check("QBPX-27", desc,
                  none && g.model().rows().size() == 4 && g.model().pc_exists(0x8000) && g.model().pc_breakpoints().size() == 1,
                  fmt("none=%d rows=%d plain=%zu", none, int(g.model().rows().size()), g.model().pc_breakpoints().size()));
        }
    }
    {   // QBPX-28
        BpoFix g(/*paused=*/true);
        const char* desc = "a plain and a conditional Execute at $8000 are two rows; add() of an existing full spec keeps its disabled On state; "
                           "add(Execute, 0x8000) and remove(Execute, 0x8000) touch only the plain one";
        if (!g.fx.ok) { check("QBPX-28", desc, false, "fixture"); }
        else {
            g.fx.enable();
            const auto cs = cond_spec(BM::Execute, 0x8000, "A == 1");
            g.model().add(BM::Execute, 0x8000);
            g.model().add(cs);
            const bool two = g.model().rows().size() == 2;
            g.model().set_enabled(cs, false);
            g.model().add(cs);   // idempotent: no second row, and still disabled
            bool cond_off = false;
            for (const auto& r : g.model().rows()) if (!r.condition.isEmpty()) cond_off = !r.enabled;
            const bool still_two = g.model().rows().size() == 2 && cond_off;
            g.model().add(BM::Execute, 0x8000);     // the plain one exists: nothing new
            const bool add_plain = g.model().rows().size() == 2;
            g.model().remove(BM::Execute, 0x8000);  // only the plain one
            const bool left = g.model().rows().size() == 1 && g.model().rows()[0].condition == "A == 1";
            check("QBPX-28", desc, two && still_two && add_plain && left,
                  fmt("two=%d still_two=%d add_plain=%d left=%d", two, still_two, add_plain, left));
        }
    }
    {   // QBPX-29
        std::vector<jnext::script::BpSpec> saved;
        {
            BpjFix a;
            BreakpointModel& m = a.model();
            m.add(ranged_spec(BM::Read, 0x9000, 0x90FF));
            jnext::script::BpSpec io = spec_of(BM::IoRead, 0x00FE);
            io.mask = 0xFFFF;
            m.add(io);
            m.add(cond_spec(BM::Execute, 0x8000, "B == 2"));
            m.add(spec_of(BM::Execute, 0x8100, true));
            m.add(spec_of(BM::Interrupt, 0));
            m.add(spec_of(BM::Nmi, 0));
            m.add_source_line(QStringLiteral("main.bas:10"), false, "A == 1");
            saved = m.script_specs();
            a.run("Save as Script...", g_tmp->filePath(QStringLiteral("bpx-s3.jds")));
        }
        BpjFix b;
        // B already holds a PLAIN group on the same line: the file's conditional group is another breakpoint on the
        // same places, so it must be added, not merged into the plain one.
        b.model().add_source_line(QStringLiteral("main.bas:10"));
        const auto seen = b.run("Load from Script...", g_tmp->filePath(QStringLiteral("bpx-s3.jds")));
        const auto got = b.model().script_specs();
        bool all_own = true;
        for (const auto& r : b.model().rows()) all_own = all_own && r.own;
        int present = 0;
        for (const auto& sv : saved)
            for (const auto& g2 : got)
                if (g2 == sv) { ++present; break; }
        check("QBPX-29", "Save then Load in a fresh session keeps a range, a port mask, a condition, count-only, Interrupt, NMI and a source "
                         "line with a condition: every saved spec comes back equal on every BpSpec field, beside B's own plain group on that "
                         "same line",
              saved.size() == 7 && present == 7 && got.size() == 8 && all_own && b.model().rows().size() == 8 &&
                  count_kind(seen, "box") == 0,
              fmt("saved=%d present=%d got=%d rows=%d", int(saved.size()), present, int(got.size()), int(b.model().rows().size())));
    }
    {   // QBPX-30
        BpoFix g(/*paused=*/true);
        const char* desc = "another client's Execute range and Port with a non-rule mask read `Execute (client N)` `$8000-$80FF` and `IO Read "
                           "(client N)` `$00FE/FFFF`, as before the dialog could make them, and are read-only";
        if (!g.fx.ok) { check("QBPX-30", desc, false, "fixture"); }
        else {
            g.fx.enable();
            jnext::dbg::Subscription a;
            a.kind = jnext::dbg::EventKind::Execute;
            a.filter.lo = 0x8000;
            a.filter.hi = 0x80FF;
            a.action = jnext::dbg::Action::Stop;
            const jnext::dbg::ClientId other = other_client_sub(g.fx, g.model(), a);
            jnext::dbg::Subscription b;
            b.kind = jnext::dbg::EventKind::Port;
            b.access = jnext::dbg::Access::Read;
            b.filter.port_mask = 0xFFFF;
            b.filter.port_value = 0x00FE;
            b.action = jnext::dbg::Action::Stop;
            other_client_sub(g.fx, g.model(), b);
            QString exec_t, exec_a, io_t, io_a;
            bool all_ro = true;
            for (const auto& r : g.model().rows()) {
                all_ro = all_ro && !r.own;
                if (r.type_text.startsWith("Execute")) { exec_t = r.type_text; exec_a = r.addr_text; }
                if (r.type_text.startsWith("IO Read")) { io_t = r.type_text; io_a = r.addr_text; }
            }
            check("QBPX-30", desc,
                  all_ro && g.model().rows().size() == 2 && exec_a == "$8000-$80FF" && io_a == "$00FE/FFFF" &&
                      exec_t == QStringLiteral("Execute (client %1)").arg(other) && io_t.startsWith("IO Read (client"),
                  fmt("%s %s | %s %s", s(exec_t).c_str(), s(exec_a).c_str(), s(io_t).c_str(), s(io_a).c_str()));
        }
    }
    {   // QBPX-31
        BpoFix g(/*paused=*/true);
        const char* desc = "Edit seeds To, Mask, Condition and Count only from the row, and an unchanged OK carries the On state";
        if (!g.fx.ok) { check("QBPX-31", desc, false, "fixture"); }
        else {
            g.fx.enable();
            auto sp = ranged_spec(BM::Execute, 0x8000, 0x80FF);
            sp.condition = "B == 2";
            sp.count_only = true;
            g.model().add(sp);
            g.model().set_enabled(sp, false);
            auto io = spec_of(BM::IoRead, 0x00FE);
            io.mask = 0xFFFF;
            g.model().add(io);
            g.table()->setCurrentCell(row_of(g, "Execute"), 1);
            const BpoDlg a = drive_bp_dialog(nullptr, [&] { g.panel()->on_edit(); });
            // (the rows are sorted by address: the IO row at $00FE comes first)
            auto want = sp;
            want.enabled = false;
            bool kept = g.model().rows().size() == 2;
            int execs = 0;
            for (const auto& got : g.model().script_specs())
                if (got.kind == jnext::script::BpSpec::Execute) { ++execs; kept = kept && got == want; }
            kept = kept && execs == 1;
            g.table()->setCurrentCell(row_of(g, "IO Read"), 1);
            const BpoDlg b = drive_bp_dialog(nullptr, [&] { g.panel()->on_edit(); }, /*accept=*/false);
            check("QBPX-31", desc,
                  a.opened && a.seed_to == "80FF" && a.seed_cond == "B == 2" && a.seed_count && kept && b.opened && b.seed_mask == "FFFF",
                  fmt("to='%s' cond='%s' count=%d kept=%d mask='%s'", s(a.seed_to).c_str(), s(a.seed_cond).c_str(), a.seed_count, kept,
                      s(b.seed_mask).c_str()));
        }
    }
    {   // QBPX-32
        BpoFix g(/*paused=*/true);
        const char* desc = "To and Mask typed for one type and left behind by switching to another are not read: IO Read after a To, and Execute "
                           "after a Mask, each give a PLAIN row (no range, no mask) that saves";
        if (!g.fx.ok) { check("QBPX-32", desc, false, "fixture"); }
        else {
            g.fx.enable();
            auto stale = [&](int first_type, const QString& addr1, const QString& to, const QString& mask, int then_type, const QString& addr2) {
                return [=](QDialog* d) {
                    auto* cb = d->findChild<QComboBox*>();
                    auto* a  = d->findChild<QLineEdit*>("bp_dialog_address");
                    auto* t  = d->findChild<QLineEdit*>("bp_dialog_to");
                    auto* m  = d->findChild<QLineEdit*>("bp_dialog_mask");
                    cb->setCurrentIndex(first_type);
                    a->setText(addr1);
                    t->setText(to);       // typed while enabled for the first type ...
                    m->setText(mask);
                    cb->setCurrentIndex(then_type);   // ... then the type changes and the field greys out, keeping its text
                    a->setText(addr2);
                };
            };
            drive_bp_dialog(stale(BM::Execute, "8000", "80FF", QString(), BM::IoRead, "FE"), [&] { g.panel()->on_add(); });
            // (address 0010: the address rule's mask for it is 00FF, so a stale FFFF would NOT normalise away)
            drive_bp_dialog(stale(BM::IoRead, "0100", QString(), "FFFF", BM::Execute, "0010"), [&] { g.panel()->on_add(); });
            const auto specs = g.model().script_specs();
            bool plain = specs.size() == 2;
            for (const auto& sp : specs) plain = plain && !sp.hi && !sp.mask && sp.condition.empty();
            const auto saved = jnext::script::bp_script_text(specs, *g.fx.backend);
            // a second OK of each dialog must not duplicate either row (a stale field makes the spec miss its own row)
            drive_bp_dialog(stale(BM::Execute, "8000", "80FF", QString(), BM::IoRead, "FE"), [&] { g.panel()->on_add(); });
            drive_bp_dialog(stale(BM::IoRead, "0100", QString(), "FFFF", BM::Execute, "0010"), [&] { g.panel()->on_add(); });
            check("QBPX-32", desc, plain && saved.refused.empty() && g.model().rows().size() == 2,
                  fmt("specs=%zu plain=%d refused=%zu rows=%d", specs.size(), plain, saved.refused.size(), int(g.model().rows().size())));
        }
    }
    {   // QBPX-33
        BpoFix g;
        const char* desc = "a condition that fails at run time is the ONLY activity (no stop, no count moving): the Condition cell's tooltip still "
                           "shows the error after a few ticks";
        if (!g.fx.ok) { check("QBPX-33", desc, false, "fixture"); }
        else {
            g.fx.load(PROG, kLoop);
            g.fx.regs(PROG);
            g.fx.enable();
            g.model().add(cond_spec(BM::Execute, 0x8002, "1 / (B - B) == 0"));
            for (int i = 0; i < 3; ++i) g.tick();
            QTableWidgetItem* c = g.table()->rowCount() ? g.table()->item(0, 5) : nullptr;
            const QString tip = c ? c->toolTip() : QString("<no cell>");
            check("QBPX-33", desc, !g.fx.paused() && tip.contains("division by zero"), s(tip));
        }
    }
    {   // QBPX-34
        BpoFix g;
        const char* desc = "a failing condition evaluated many times (twice per loop run here) logs exactly one line through the backend's sink";
        if (!g.fx.ok) { check("QBPX-34", desc, false, "fixture"); }
        else {
            LogCatcher log;
            jnext::dbg::ClientInfo ci;
            ci.name     = "qbpx34";
            ci.observer = true;
            const jnext::dbg::ClientId cid = g.fx.backend->attach(ci).value;
            g.fx.backend->set_listener(cid, &log);
            g.fx.load(PROG, kLoop);
            g.fx.regs(PROG);
            g.fx.enable();
            g.model().add(cond_spec(BM::Execute, 0x8002, "1 / (B - B) == 0"));
            g.tick();
            g.fx.regs(PROG);   // the loop again: five more failing evaluations of the same error
            g.tick();
            int lines = 0;
            for (const auto& l : log.lines) lines += l.find("breakpoint condition") != std::string::npos;
            g.fx.backend->set_listener(cid, nullptr);
            g.fx.backend->detach(cid);
            check("QBPX-34", desc, lines == 1, fmt("log lines=%d (of %zu)", lines, log.lines.size()));
        }
    }
}

// ===========================================================================
// QBA -- GH #532: the Breakpoints panel's Audio Write type.
//
// Oracles: the backend's own AEV rows (audio_debug_test: an AudioWrite subscription stops at the writer's
// boundary, with the filter each dialog field means), the machine itself (each row that says "stops" is a
// real Next stopping, never a subscription read back), and the plan's row texts. A dialog field is read
// back as the EventFilter field it maps to, one row per field.
// ===========================================================================
namespace {

constexpr int kAudioWrite = 8;   // the Type combo's index

/// The dialog filled for an Audio Write breakpoint: target (BpAudio::Target), chip (data: 0xFF any), register
/// (data: -1 any), value and mask texts, dropped-only. The chip is set AFTER the target (the target rebuilds the chip list).
std::function<void(QDialog*)> audio_fill(int target, int chip, int reg, const QString& value = QString(),
                                         const QString& mask = QString(), bool dropped = false) {
    return [=](QDialog* d) {
        if (auto* c = d->findChild<QComboBox*>()) c->setCurrentIndex(kAudioWrite);
        auto pick = [&](const char* name, int data) {
            if (auto* c = d->findChild<QComboBox*>(QString::fromLatin1(name))) {
                const int i = c->findData(data);
                if (i >= 0) c->setCurrentIndex(i);
            }
        };
        pick("bp_dialog_audio_target", target);
        pick("bp_dialog_audio_chip", chip);
        pick("bp_dialog_audio_reg", reg);
        if (auto* e = d->findChild<QLineEdit*>(QStringLiteral("bp_dialog_audio_value"))) e->setText(value);
        if (auto* e = d->findChild<QLineEdit*>(QStringLiteral("bp_dialog_audio_mask"))) e->setText(mask);
        if (auto* k = d->findChild<QCheckBox*>(QStringLiteral("bp_dialog_audio_dropped"))) k->setChecked(dropped);
    };
}

/// This GUI's own AudioWrite subscriptions' filters, in creation order.
std::vector<jnext::dbg::EventFilter> own_audio_filters(Fixture& fx, BreakpointModel& m) {
    std::vector<jnext::dbg::EventFilter> out;
    for (const auto& si : fx.backend->subscriptions(false))
        if (si.owner == m.client() && si.kind == jnext::dbg::EventKind::AudioWrite) out.push_back(si.filter);
    return out;
}

struct QbaListener final : jnext::dbg::Listener {
    std::vector<jnext::dbg::PausedInfo> paused;
    void on_paused(const jnext::dbg::PausedInfo& i) override { paused.push_back(i); }
    void on_resumed(jnext::dbg::ClientId) override {}
    void on_reset(jnext::dbg::ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(jnext::dbg::LogLevel, const std::string&) override {}
};

/// A Next with TurboSound and the DAC on, R7 preset.
void next_audio(Fixture& fx) {
    fx.emu.nextreg().write(0x08, 0x0A);
    fx.emu.nextreg().write(0x84, 0xFF);
}

}  // namespace

static void test_bp_audio() {
    set_group("QBA");
    using BM = BreakpointModel;
    namespace AudioFlag = jnext::dbg::AudioFlag;
    using jnext::dbg::AudioTarget;
    using jnext::dbg::audio_target_bit;
    using jnext::script::BpAudio;
    using jnext::script::BpSpec;
    constexpr auto T_AY  = audio_target_bit(AudioTarget::AyRegister);
    constexpr auto T_SEL = audio_target_bit(AudioTarget::AyChipSelect);
    constexpr auto T_DAC = audio_target_bit(AudioTarget::Dac) | audio_target_bit(AudioTarget::DacMirror);
    // LD BC,0xFFFD; LD A,7; OUT (C),A; LD B,0xBF; LD A,0x38; OUT (C),A; JR $
    const std::initializer_list<uint8_t> kR7 = {0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x38, 0xED, 0x79, 0x18, 0xFE};
    {   // QBA-01
        BpoFix g(/*paused=*/true, MachineType::ZXN_ISSUE2);
        const char* desc = "the Add dialog's Type combo has `Audio Write` at index 8; choosing it enables the six audio widgets "
                           "and turns Address, To and Mask off; choosing Execute reverses that";
        if (!g.fx.ok) { check("QBA-01", desc, false, "fixture"); }
        else {
            g.fx.enable();
            bool on_ok = false, off_ok = false;
            const BpoDlg d = drive_bp_dialog([&](QDialog* dlg) {
                auto* type = dlg->findChild<QComboBox*>();
                const char* audio[] = {"bp_dialog_audio_target", "bp_dialog_audio_chip", "bp_dialog_audio_reg"};
                auto state = [&](bool want_audio) {
                    bool ok = true;
                    for (const char* n : audio) {
                        auto* c = dlg->findChild<QComboBox*>(QString::fromLatin1(n));
                        ok = ok && c && c->isEnabled() == want_audio;
                    }
                    for (const char* n : {"bp_dialog_audio_value", "bp_dialog_audio_mask"}) {
                        auto* e = dlg->findChild<QLineEdit*>(QString::fromLatin1(n));
                        ok = ok && e && e->isEnabled() == want_audio;
                    }
                    auto* k = dlg->findChild<QCheckBox*>(QStringLiteral("bp_dialog_audio_dropped"));
                    ok = ok && k && k->isEnabled() == want_audio;
                    for (const char* n : {"bp_dialog_address", "bp_dialog_to", "bp_dialog_mask"}) {
                        auto* e = dlg->findChild<QLineEdit*>(QString::fromLatin1(n));
                        ok = ok && e && e->isEnabled() == !want_audio && (want_audio || std::string(n) != "bp_dialog_mask");
                    }
                    return ok;
                };
                type->setCurrentIndex(kAudioWrite);
                on_ok = state(true);
                type->setCurrentIndex(BM::Execute);
                // Execute: address and range on, port mask off, the audio fields off.
                auto* addr = dlg->findChild<QLineEdit*>(QStringLiteral("bp_dialog_address"));
                auto* to   = dlg->findChild<QLineEdit*>(QStringLiteral("bp_dialog_to"));
                auto* mask = dlg->findChild<QLineEdit*>(QStringLiteral("bp_dialog_mask"));
                auto* tgt  = dlg->findChild<QComboBox*>(QStringLiteral("bp_dialog_audio_target"));
                off_ok = addr && to && mask && tgt && addr->isEnabled() && to->isEnabled() && !mask->isEnabled() && !tgt->isEnabled();
            }, [&] { g.panel()->on_add(); }, /*accept=*/false);
            check("QBA-01", desc, d.opened && d.items.size() == 9 && d.items[8] == "Audio Write" && on_ok && off_ok,
                  fmt("opened=%d items=%d last=%s on=%d off=%d", d.opened, int(d.items.size()),
                      d.items.isEmpty() ? "" : s(d.items.last()).c_str(), on_ok, off_ok));
        }
    }
    {   // QBA-02..06, one dialog each
        BpoFix g(/*paused=*/true, MachineType::ZXN_ISSUE2);
        if (!g.fx.ok) {
            for (const char* id : {"QBA-02", "QBA-03", "QBA-04", "QBA-05", "QBA-06"}) check(id, "audio dialog field", false, "fixture");
        } else {
            g.fx.enable();
            auto add = [&](const std::function<void(QDialog*)>& act) {
                drive_bp_dialog(act, [&] { g.panel()->on_add(); });
                return own_audio_filters(g.fx, g.model());
            };
            auto f = add(audio_fill(BpAudio::AyRegister, 1, 7, "38", "FF"));
            check("QBA-02", "AY register, chip 1, R7, value 38, mask FF: the subscription's filter is exactly targets bit(AyRegister), chip 1, regs {7}, "
                            "value_mask 0xFF, value_match 0x38, flags_all 0, flags_none Dropped",
                  f.size() == 1 && f[0].audio_targets == T_AY && f[0].chip == 1 && f[0].regs == jnext::dbg::EventFilter::RegSet{7} &&
                      f[0].value_mask == 0xFF && f[0].value_match == 0x38 && f[0].audio_flags_all == 0 &&
                      f[0].audio_flags_none == AudioFlag::Dropped);
            f = add(audio_fill(BpAudio::Dac, 1, -1));
            check("QBA-03", "target DAC, channel B: targets Dac|DacMirror, chip 1, no registers, no value",
                  f.size() == 2 && f[1].audio_targets == T_DAC && f[1].chip == 1 && f[1].regs.empty() && f[1].value_mask == 0 &&
                      f[1].value_match == 0);
            f = add(audio_fill(BpAudio::AyChipSelect, 2, -1));
            check("QBA-04", "target AY chip select, chip 2: targets bit(AyChipSelect), chip 2",
                  f.size() == 3 && f[2].audio_targets == T_SEL && f[2].chip == 2);
            bool chip_disabled = false;
            f = add([&](QDialog* d) {
                audio_fill(BpAudio::Any, 0xFF, -1)(d);
                auto* c = d->findChild<QComboBox*>(QStringLiteral("bp_dialog_audio_chip"));
                chip_disabled = c && !c->isEnabled();
            });
            check("QBA-05", "target Any: targets 0xFF and chip 0xFF, with the chip combo disabled",
                  f.size() == 4 && f[3].audio_targets == 0xFF && f[3].chip == 0xFF && chip_disabled);
            f = add(audio_fill(BpAudio::AyRegister, 0, 8, QString(), QString(), true));
            check("QBA-06", "Dropped writes only: flags_all Dropped, flags_none 0 (unchecked, the default, is flags_none Dropped as in QBA-02)",
                  f.size() == 5 && f[4].audio_flags_all == AudioFlag::Dropped && f[4].audio_flags_none == 0);
        }
    }
    {   // QBA-07
        BpoFix g(/*paused=*/true, MachineType::ZXN_ISSUE2);
        const char* desc = "the register item `R13 (envelope restart)` gives regs {13}, and the row reads `AY1 R13 restart`";
        if (!g.fx.ok) { check("QBA-07", desc, false, "fixture"); }
        else {
            g.fx.enable();
            QString item;
            drive_bp_dialog([&](QDialog* d) {
                audio_fill(BpAudio::AyRegister, 1, 13)(d);
                auto* r = d->findChild<QComboBox*>(QStringLiteral("bp_dialog_audio_reg"));
                if (r) item = r->itemText(r->findData(13));
            }, [&] { g.panel()->on_add(); });
            const auto f = own_audio_filters(g.fx, g.model());
            check("QBA-07", desc,
                  item == "R13 (envelope restart)" && f.size() == 1 && f[0].regs == jnext::dbg::EventFilter::RegSet{13} &&
                      g.model().rows().size() == 1 && g.model().rows()[0].addr_text == "AY1 R13 restart" &&
                      g.model().rows()[0].type_text == "Audio Write" && g.cell(0, 2) == "AY1 R13 restart",
                  s(item) + " | " + (g.model().rows().empty() ? std::string("(no row)") : s(g.model().rows()[0].addr_text)));
        }
    }
    {   // QBA-08
        auto txt = [](BpAudio::Target t, uint8_t chip, std::optional<uint8_t> reg, std::optional<uint8_t> v, uint8_t m, bool dr) {
            BpAudio a;
            a.target = t; a.chip = chip; a.reg = reg; a.value = v; a.mask = m; a.dropped_only = dr;
            return jnext::script::bp_audio_text(a);
        };
        const std::string got[] = {
            txt(BpAudio::AyRegister, 1, 13, {}, 0xFF, false), txt(BpAudio::AyRegister, 0xFF, 7, 0x38, 0xFF, false),
            txt(BpAudio::AyRegister, 0, {}, 0x08, 0x0F, false), txt(BpAudio::AyChipSelect, 2, {}, {}, 0xFF, false),
            txt(BpAudio::Dac, 1, {}, {}, 0xFF, false), txt(BpAudio::Dac, 0xFF, {}, {}, 0xFF, true),
            txt(BpAudio::Any, 0xFF, {}, {}, 0xFF, false), txt(BpAudio::Any, 0xFF, {}, {}, 0xFF, true)};
        const char* want[] = {"AY1 R13 restart", "AY R7 value 0x38", "AY0 value 0x08 mask 0x0F", "AY select 2", "DAC B", "DAC dropped",
                              "Any audio", "Any audio dropped"};
        std::string bad;
        for (int i = 0; i < 8; ++i) if (got[i] != want[i]) bad += "[" + got[i] + " != " + want[i] + "]";
        check("QBA-08", "the row texts: AY1 R13 restart, AY R7 value 0x38, AY0 value 0x08 mask 0x0F, AY select 2, DAC B, DAC dropped, Any audio, Any audio dropped",
              bad.empty(), bad);
    }
    {   // QBA-09
        BpoFix g(/*paused=*/true, MachineType::ZXN_ISSUE2);
        const char* desc = "the dialog refuses, with a reason and creating nothing: a value `1G`, a mask with no value, and value 10 with mask 0F";
        if (!g.fx.ok) { check("QBA-09", desc, false, "fixture"); }
        else {
            g.fx.enable();
            const BpoDlg a = drive_bp_dialog(audio_fill(BpAudio::AyRegister, 0xFF, -1, "1G"), [&] { g.panel()->on_add(); });
            const BpoDlg b = drive_bp_dialog(audio_fill(BpAudio::AyRegister, 0xFF, -1, QString(), "0F"), [&] { g.panel()->on_add(); });
            const BpoDlg c = drive_bp_dialog(audio_fill(BpAudio::AyRegister, 0xFF, -1, "10", "0F"), [&] { g.panel()->on_add(); });
            check("QBA-09", desc,
                  a.error.contains("value") && a.error.contains("1G") && b.error.contains("mask needs a value") &&
                      c.error.contains("outside the mask") && own_audio_filters(g.fx, g.model()).empty() && g.model().rows().empty(),
                  s(a.error) + " | " + s(b.error) + " | " + s(c.error));
        }
    }
    {   // QBA-10
        BpoFix g(/*paused=*/false, MachineType::ZXN_ISSUE2);
        const char* desc = "a GUI audio breakpoint on an AEV-02 machine stops on the data write and its row's hit count is 1; the same breakpoint count-only moves the count and never stops";
        if (!g.fx.ok) { check("QBA-10", desc, false, "fixture"); }
        else {
            next_audio(g.fx);
            g.fx.load(PROG, kR7);
            g.fx.regs(PROG);
            g.fx.enable();
            BpSpec sp;
            sp.kind = BpSpec::AudioWrite;
            sp.audio.reg = 7;
            sp.audio.chip = 0;
            g.model().add(sp);
            const bool stopped = g.tick_until_paused();
            g.model().sync();
            const uint64_t h = g.model().rows().size() == 1 ? g.model().hits(g.model().rows()[0]) : 99;
            BpoFix c(/*paused=*/false, MachineType::ZXN_ISSUE2);
            next_audio(c.fx);
            c.fx.load(PROG, kR7);
            c.fx.regs(PROG);
            c.fx.enable();
            sp.count_only = true;
            c.model().add(sp);
            for (int i = 0; i < 3; ++i) c.tick();
            c.model().sync();
            const uint64_t hc = c.model().rows().size() == 1 ? c.model().hits(c.model().rows()[0]) : 99;
            check("QBA-10", desc, stopped && g.fx.pc() == 0x800D && h == 1 && !c.fx.paused() && hc == 1,
                  fmt("stopped=%d pc=%04X hits=%llu | count-only paused=%d hits=%llu", stopped, g.fx.pc(), (unsigned long long)h,
                      c.fx.paused(), (unsigned long long)hc));
        }
    }
    {   // QBA-11
        BpoFix g(/*paused=*/false, MachineType::ZXN_ISSUE2);
        const char* desc = "a GUI breakpoint {DAC, B} and a test client's backend sub {Dac|DacMirror, chip 1} stop on the same write: both ids in one matched[]";
        if (!g.fx.ok) { check("QBA-11", desc, false, "fixture"); }
        else {
            next_audio(g.fx);
            g.fx.load(PROG, {0x3E, 0x11, 0xD3, 0x0F, 0x18, 0xFE});   // OUT (0x0F),A: channel B
            g.fx.regs(PROG);
            g.fx.enable();
            BpSpec sp;
            sp.kind = BpSpec::AudioWrite;
            sp.audio.target = BpAudio::Dac;
            sp.audio.chip = 1;
            g.model().add(sp);
            jnext::dbg::ClientInfo oi;
            oi.name = "qba11";
            oi.kind = jnext::dbg::ClientKind::Test;
            const jnext::dbg::ClientId other = g.fx.backend->attach(oi).value;
            QbaListener lis;
            g.fx.backend->set_listener(other, &lis);
            jnext::dbg::Subscription sub;
            sub.kind = jnext::dbg::EventKind::AudioWrite;
            sub.action = jnext::dbg::Action::Stop;
            sub.filter.audio_targets = T_DAC;
            sub.filter.chip = 1;
            const auto oid = g.fx.backend->subscribe(other, sub).value;
            const bool stopped = g.tick_until_paused();
            const jnext::dbg::EventId mine = g.model().rows().empty() ? 0 : g.model().rows()[0].id;
            size_t found = 0;
            if (!lis.paused.empty())
                for (const auto& h : lis.paused.back().matched) found += (h.event_id == oid || h.event_id == mine);
            check("QBA-11", desc, stopped && mine != 0 && oid != 0 && found == 2 && lis.paused.back().matched.size() == 2,
                  fmt("stopped=%d found=%zu", stopped, found));
            g.fx.backend->set_listener(other, nullptr);
            g.fx.backend->detach(other);
        }
    }
    {   // QBA-12
        BpoFix g(/*paused=*/true, MachineType::ZXN_ISSUE2);
        const char* desc = "another client's `reg 8..10` AudioWrite subscription (a script's) is listed read-only: type `Audio Write (client N)`, type -1, not own";
        if (!g.fx.ok) { check("QBA-12", desc, false, "fixture"); }
        else {
            g.fx.enable();
            jnext::dbg::Subscription sub;
            sub.kind = jnext::dbg::EventKind::AudioWrite;
            sub.filter.audio_targets = T_AY;
            sub.filter.regs = {8, 9, 10};
            sub.filter.audio_flags_none = AudioFlag::Dropped;   // as the script's `ay_write` sets it: only the register range is foreign
            const auto other = other_client_sub(g.fx, g.model(), sub);
            const auto& rows = g.model().rows();
            check("QBA-12", desc,
                  rows.size() == 1 && rows[0].type == -1 && !rows[0].own &&
                      rows[0].type_text == QStringLiteral("Audio Write (client %1)").arg(other),
                  rows.empty() ? std::string("(no row)") : s(rows[0].type_text));
        }
    }
    {   // QBA-15
        BpoFix g(/*paused=*/true, MachineType::ZXN_ISSUE2);
        const char* desc = "another client's {ANY, chip 1} AudioWrite subscription is not the dialog's `Any audio` (which has no chip): listed read-only, type -1";
        if (!g.fx.ok) { check("QBA-15", desc, false, "fixture"); }
        else {
            g.fx.enable();
            jnext::dbg::Subscription sub;
            sub.kind = jnext::dbg::EventKind::AudioWrite;
            sub.filter.audio_targets = jnext::dbg::AUDIO_TARGETS_ANY;
            sub.filter.chip = 1;
            sub.filter.audio_flags_none = AudioFlag::Dropped;
            other_client_sub(g.fx, g.model(), sub);
            const auto& rows = g.model().rows();
            check("QBA-15", desc, rows.size() == 1 && rows[0].type == -1 && !rows[0].own, rows.empty() ? std::string("(no row)") : s(rows[0].type_text));
        }
    }
    {   // QBA-16
        BpoFix g(/*paused=*/true, MachineType::ZXN_ISSUE2);
        const char* desc = "DAC target, channel B, then Dropped ticked: the channel combo goes to Any and is disabled, and the breakpoint made has chip 0xFF (a dropped DAC write has no channel); unticked, the channel is enabled again";
        if (!g.fx.ok) { check("QBA-16", desc, false, "fixture"); }
        else {
            g.fx.enable();
            bool disabled = false, any = false, reenabled = false;
            drive_bp_dialog([&](QDialog* d) {
                audio_fill(BpAudio::Dac, 1, -1, QString(), QString(), true)(d);
                auto* c = d->findChild<QComboBox*>(QStringLiteral("bp_dialog_audio_chip"));
                disabled = c && !c->isEnabled();
                any = c && c->currentData().toInt() == 0xFF;
                d->findChild<QCheckBox*>(QStringLiteral("bp_dialog_audio_dropped"))->setChecked(false);
                reenabled = c && c->isEnabled();
                d->findChild<QCheckBox*>(QStringLiteral("bp_dialog_audio_dropped"))->setChecked(true);
            }, [&] { g.panel()->on_add(); });
            const auto f = own_audio_filters(g.fx, g.model());
            check("QBA-16", desc, disabled && any && reenabled && f.size() == 1 && f[0].chip == 0xFF && f[0].audio_flags_all == AudioFlag::Dropped &&
                                      f[0].audio_targets == T_DAC,
                  fmt("disabled=%d any=%d reenabled=%d n=%zu", disabled, any, reenabled, f.size()));
        }
    }
    {   // QBA-17
        BpoFix g(/*paused=*/true, MachineType::ZXN_ISSUE2);
        const char* desc = "the model makes a DAC + Dropped breakpoint given a channel with chip 0xFF (the backend would refuse a channel there): one own subscription";
        if (!g.fx.ok) { check("QBA-17", desc, false, "fixture"); }
        else {
            g.fx.enable();
            BpSpec sp;
            sp.kind = BpSpec::AudioWrite;
            sp.audio.target = BpAudio::Dac;
            sp.audio.chip = 1;
            sp.audio.dropped_only = true;
            g.model().add(sp);
            const auto f = own_audio_filters(g.fx, g.model());
            check("QBA-17", desc, f.size() == 1 && f[0].chip == 0xFF && f[0].audio_flags_all == AudioFlag::Dropped);
        }
    }
    {   // QBA-13
        BpoFix g(/*paused=*/true, MachineType::ZXN_ISSUE2);
        const char* desc = "Edit seeds the dialog from the row (type 8, target, chip, register, value, mask, dropped) and an unchanged OK keeps the same breakpoint";
        if (!g.fx.ok) { check("QBA-13", desc, false, "fixture"); }
        else {
            g.fx.enable();
            BpSpec sp;
            sp.kind = BpSpec::AudioWrite;
            sp.audio.target = BpAudio::AyRegister;
            sp.audio.chip = 2;
            sp.audio.reg = 9;
            sp.audio.value = 0x34;
            sp.audio.mask = 0xF0 | 0x04;
            sp.audio.dropped_only = true;
            g.model().add(sp);
            g.table()->selectRow(0);
            g.table()->setCurrentCell(0, 0);
            struct Seed { int type = -1, target = -1, chip = -1, reg = -2; QString value, mask; bool dropped = false; } seed;
            const BpoDlg d = drive_bp_dialog([&](QDialog* dlg) {
                auto data = [&](const char* n) { auto* c = dlg->findChild<QComboBox*>(QString::fromLatin1(n)); return c ? c->currentData().toInt() : -9; };
                seed.type = dlg->findChild<QComboBox*>()->currentIndex();
                seed.target = data("bp_dialog_audio_target");
                seed.chip = data("bp_dialog_audio_chip");
                seed.reg = data("bp_dialog_audio_reg");
                seed.value = dlg->findChild<QLineEdit*>(QStringLiteral("bp_dialog_audio_value"))->text();
                seed.mask = dlg->findChild<QLineEdit*>(QStringLiteral("bp_dialog_audio_mask"))->text();
                seed.dropped = dlg->findChild<QCheckBox*>(QStringLiteral("bp_dialog_audio_dropped"))->isChecked();
            }, [&] { g.panel()->on_edit(); });
            const auto script = g.model().script_specs();
            check("QBA-13", desc,
                  d.opened && seed.type == kAudioWrite && seed.target == BpAudio::AyRegister && seed.chip == 2 && seed.reg == 9 &&
                      seed.value == "34" && seed.mask == "F4" && seed.dropped && g.model().rows().size() == 1 &&
                      script.size() == 1 && script[0] == sp,
                  fmt("opened=%d type=%d target=%d chip=%d reg=%d value=%s mask=%s dropped=%d rows=%zu", d.opened, seed.type, seed.target,
                      seed.chip, seed.reg, s(seed.value).c_str(), s(seed.mask).c_str(), seed.dropped, g.model().rows().size()));
        }
    }
    {   // QBA-14
        BpoFix g(/*paused=*/true, MachineType::ZXN_ISSUE2);
        const char* desc = "every audio target saves as a script and loads back: the model's rows -> bp_script_text -> bp_specs_from_script -> a cleared model, the same eight breakpoints, none refused";
        if (!g.fx.ok) { check("QBA-14", desc, false, "fixture"); }
        else {
            g.fx.enable();
            std::vector<BpSpec> in;
            auto mk = [&](BpAudio::Target t, uint8_t chip, std::optional<uint8_t> reg, std::optional<uint8_t> v, uint8_t m, bool dr) {
                BpSpec x;
                x.kind = BpSpec::AudioWrite;
                x.audio = {t, chip, reg, v, m, dr};
                in.push_back(x);
            };
            mk(BpAudio::AyRegister, 1, 13, {}, 0xFF, false);
            mk(BpAudio::AyRegister, 0xFF, 7, 0x38, 0xFF, false);
            mk(BpAudio::AyChipSelect, 2, {}, {}, 0xFF, false);
            mk(BpAudio::Dac, 0xFF, {}, 0x80, 0xC0, true);
            mk(BpAudio::Dac, 0xFF, {}, {}, 0xFF, false);
            mk(BpAudio::Any, 0xFF, {}, {}, 0xFF, false);
            mk(BpAudio::Any, 0xFF, {}, {}, 0xFF, true);
            mk(BpAudio::AyRegister, 0, 8, {}, 0xFF, true);
            g.model().add_specs(in);
            const auto saved = g.model().script_specs();
            const jnext::script::BpScript text = jnext::script::bp_script_text(saved, *g.fx.backend);
            g.model().clear_all();
            const jnext::script::BpRead back = jnext::script::bp_specs_from_script(text.text, *g.fx.backend);
            g.model().add_specs(back.specs);
            const auto again = g.model().script_specs();
            bool same = saved.size() == 8 && again.size() == 8;
            for (size_t i = 0; same && i < 8; ++i) same = again[i] == saved[i];
            check("QBA-14", desc, same && text.refused.empty() && back.ok(), text.text + (back.ok() ? "" : back.errors[0].to_string()));
        }
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
    // QFileDialog saves its history to $XDG_CONFIG_HOME/QtProject.conf, which
    // is the user's real ~/.config otherwise (and shared by concurrent runs).
    qputenv("XDG_CONFIG_HOME", cfg.filePath(QStringLiteral("xdg-config")).toUtf8());
    QApplication app(argc, argv);

    test_step_over();
    test_io_breakpoints();
    test_step_into();
    test_pause_edge();
    test_throttle();
    test_idle_refresh();
    test_long_address_paused();
    test_enable_seeds();
    test_rewind_ui();
    test_rewind_refusal_ui();
    test_trace_ui();
    test_map_load();
    test_annotations_ui();
    test_reexport_ui();
    test_tstates();
    test_cpu_edit_window();
    test_nav_window();
    test_bp_script();
    test_bp_script_more();
    test_bp_interrupt();
    test_bp_audio();
    test_run_to_interrupt();
    test_bp_hits();
    test_bp_hits_more();
    test_bp_ranges();
    test_memory_tools_window();

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
