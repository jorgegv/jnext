// What the debugger window asks of the backend depends on what is on show (GH #498).
//
// The open window tracks calls only while the Call Stack or Variables panel is on show (or while
// a source Step Over / Out runs), and keeps the live raster only while the Video panel is. This
// suite drives the REAL DebuggerManager and DebuggerWindow against a real Emulator and reads the
// backend's answers (`call_stack_enabled()`, `live_raster()`), the panel's cells, and the
// machine's state.
//
// Oracle: the issue's switching rule for the requests; the Z80 CALL / RST encodings (UM0080) for
// the frames; the A/B row's oracle is the SAME machine run with the window closed.
//
// `put_on_show()` (put_on_show.h) is the one place a test makes a panel current; GH #377 (docked
// panels) changes it.
//
// Qt is required (DebuggerManager owns real QWidgets), no display: offscreen QPA.
// Run: ./build/test/debugger_panel_data_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "debugger/breakpoint_model.h"
#include "debugger/callstack_panel.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/audio_panel.h"
#include "debugger/audio_scope_view.h"
#include "debugger/audio_tracker_view.h"
#include "debugger/breakpoint_panel.h"
#include "debugger/cpu_panel.h"
#include "debugger/disasm_panel.h"
#include "debugger/memory_panel.h"
#include "debugger/nextreg_panel.h"
#include "debugger/script_panel.h"
#include "debugger/source_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/symbols_panel.h"
#include "debugger/variables_panel.h"
#include "debugger/video_panel.h"
#include "debugger/watch_panel.h"
#include "put_on_show.h"
#include "../core/batch_ab.h"

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

constexpr uint16_t kMain = 0x8000, kSub = 0x9000, kSp = 0xFF00;

void poke(Emulator& e, uint16_t addr, std::initializer_list<uint8_t> b) {
    for (uint8_t v : b) e.mmu().write(addr++, v);
}

// A 48K machine, the program   8000 NOP / 8001 NOP / 8002 CALL 9000 / 8005 NOP / 8006 JP 8000
// and the routine              9000 NOP / 9001 RET.
// The real manager, the window NOT opened, the machine running.
struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    QMainWindow win;
    DebuggerManager* mgr = nullptr;
    bool ok = false;

    Fixture() {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 0;
        if (!emu.init(cfg)) return;
        poke(emu, kMain, {0x00, 0x00, 0xCD, 0x00, 0x90, 0x00, 0xC3, 0x00, 0x80});
        poke(emu, kSub, {0x00, 0xC9});
        Z80Registers r = emu.cpu().get_registers();
        r.PC = kMain;
        r.SP = kSp;
        r.IFF1 = r.IFF2 = 0;
        emu.cpu().set_registers(r);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        mgr = new DebuggerManager(&win, *backend, &win);   // parented: auto-freed
        ok = true;
    }
    // The window is a parentless top-level the manager never deletes: close and delete it while
    // the Emulator it observes still exists.
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
    void show(QWidget* panel) {
        put_on_show(dbg(), panel);
        QApplication::processEvents();
    }
    uint16_t pc() { return emu.cpu().get_registers().PC; }
    bool paused() { return emu.debug_state().paused(); }
    // One QtApp frame tick: the frame (unless the machine is held), then the two post-frame calls.
    void tick() {
        if (!paused()) emu.run_frame();
        mgr->check_breakpoint_hit();
        mgr->refresh_panels();
    }
    bool tick_until_paused(int max_ticks = 8) {
        for (int i = 0; i < max_ticks && !paused(); ++i) tick();
        return paused();
    }
    bool tracking() const { return backend->call_stack_enabled(); }
    bool raster() const { return backend->live_raster(); }
};

// GH #534 -- a Next with an IM2 tune player: every frame's interrupt writes AY R8 once, with a counter
// that goes up by one (the value the frame leaves in the register), after the frame's own interrupt (D8).
//   I = 0x90, a 257-byte table at 0x9000 of 0x92 (so any bus byte vectors to 0x9292), the ISR at 0x9292
//   PUSH AF; PUSH BC; LD BC,FFFD; LD A,8; OUT (C),A; LD A,(9400); INC A; LD (9400),A; LD B,BF; OUT (C),A;
//   POP BC; POP AF; EI; RETI  (opcodes: UM0080), the main loop DI; LD A,90; LD I,A; IM 2; EI; HALT; JR -3.
// The real manager, the window NOT opened, the machine running, the rewind ring 50 frames.
struct AudioFixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    QMainWindow win;
    DebuggerManager* mgr = nullptr;
    bool ok = false;

    AudioFixture() {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 50;
        if (!emu.init(cfg)) return;
        for (int i = 0; i < 257; ++i) emu.mmu().write(uint16_t(0x9000 + i), 0x92);
        poke(emu, 0x9292, {0xF5, 0xC5, 0x01, 0xFD, 0xFF, 0x3E, 0x08, 0xED, 0x79, 0x3A, 0x00, 0x94, 0x3C,
                           0x32, 0x00, 0x94, 0x06, 0xBF, 0xED, 0x79, 0xC1, 0xF1, 0xFB, 0xED, 0x4D});
        poke(emu, 0x9400, {0x00});
        poke(emu, kMain, {0xF3, 0x3E, 0x90, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD});
        Z80Registers r = emu.cpu().get_registers();
        r.PC = kMain;
        r.SP = kSp;
        r.IFF1 = r.IFF2 = 0;
        emu.cpu().set_registers(r);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        mgr = new DebuggerManager(&win, *backend, &win);
        ok = backend->rewind_enabled();
    }
    ~AudioFixture() {
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
    void show(QWidget* panel) {
        put_on_show(dbg(), panel);
        QApplication::processEvents();
    }
    bool paused() { return emu.debug_state().paused(); }
    void tick() {
        if (!paused()) emu.run_frame();
        mgr->check_breakpoint_hit();
        mgr->refresh_panels();
    }
};

QString cell(QWidget* panel, int row, int col) {
    auto* t = panel ? panel->findChild<QTableWidget*>() : nullptr;
    QTableWidgetItem* it = t ? t->item(row, col) : nullptr;
    return it ? it->text() : QStringLiteral("<no cell>");
}

const QString kNote =
    "Frames marked * were rebuilt from the stack when call tracking started: "
    "interrupts are missing, and stale stack data can look like a call.";

std::string b2s(bool b) { return b ? "1" : "0"; }

// The SLD map and the two source files of the source-step rows: main.bas lines 10..13 at 8000, 8002, 8005, 8006 and
// sub.bas line 1 at 9000. Returns the SLD path.
std::string write_source_files(Fixture& fx, const std::string& dir) {
    const std::string sld = dir + "/lazy.sld";
    const unsigned p80 = fx.backend->effective_page(0x8000), p90 = fx.backend->effective_page(0x9000);
    const std::string a = std::to_string(p80), b = std::to_string(p90);
    {
        std::ofstream out(sld, std::ios::binary);
        out << "|SLD.data.version|1\n"
            << "main.bas|10||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
               "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
            << "main.bas|10||0|" << a << "|32768|T|\n"
            << "main.bas|11||0|" << a << "|32770|T|\n"
            << "main.bas|12||0|" << a << "|32773|T|\n"
            << "main.bas|13||0|" << a << "|32774|T|\n"
            << "sub.bas|1||0|" << b << "|36864|T|\n";
    }
    {
        std::ofstream m(dir + "/main.bas"), s(dir + "/sub.bas");
        for (int i = 1; i <= 13; ++i) m << "REM line " << i << "\n";
        s << "REM sub 1\n";
    }
    return sld;
}

// The machine for the A/B row: LD HL,$9000 / loop: INC (HL) / JR loop. State moves every frame.
// `audio_live`: the Audio panel on show, and every frame is a full QtApp tick (so the live refresh runs).
std::vector<batch_ab::FrameHash> busy_run(bool window_open, bool audio_live = false, bool scopes = false) {
    std::vector<batch_ab::FrameHash> out;
    Fixture fx;
    if (!fx.ok) return out;
    poke(fx.emu, kMain, {0x21, 0x00, 0x90, 0x34, 0x18, 0xFD});
    Z80Registers r = fx.emu.cpu().get_registers();
    r.PC = kMain;
    fx.emu.cpu().set_registers(r);
    if (window_open) fx.enable();
    if (audio_live) fx.show(fx.dbg()->audio_panel());
    if (scopes) {
        // GH #536: the Scopes sub-tab current, so the tap is requested and refreshed every frame.
        auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        if (tabs) tabs->setCurrentIndex(4);
    }
    batch_ab::Scratch scratch;
    for (int f = 0; f < 50; ++f) {
        if (audio_live) fx.tick(); else fx.emu.run_frame();
        out.push_back(batch_ab::capture(fx.emu, scratch, true));
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    QApplication app(argc, argv);

    std::printf("\n======================================================\n");
    std::printf("Debugger window: data collected per panel on show (GH #498)\n");
    std::printf("======================================================\n\n");

    // QLAZY-01 - the default layout (Video, Stack and Watches current): the machine is armed by the
    // window's client, call tracking is OFF, the live raster is ON (Video is on show).
    {
        Fixture fx;
        fx.enable();
        check("QLAZY-01",
              "window opened, default layout: client attached and machine armed, call-stack tracking off, live raster on",
              fx.ok && fx.backend->attached() && fx.backend->armed() && !fx.tracking() && fx.raster() &&
                  !fx.emu.call_stack().enabled(),
              "attached=" + b2s(fx.backend->attached()) + " armed=" + b2s(fx.backend->armed()) +
                  " tracking=" + b2s(fx.tracking()) + " raster=" + b2s(fx.raster()));
    }

    // QLAZY-02 / 03 - the two panels that need the frames.
    {
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->callstack_panel());
        const bool on = fx.tracking();
        fx.show(fx.dbg()->stack_panel());
        check("QLAZY-02", "Call Stack put on show: call tracking on; the Stack tab again: off",
              on && !fx.tracking(), "on=" + b2s(on) + " after=" + b2s(fx.tracking()));
    }
    {
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->variables_panel());
        const bool on = fx.tracking();
        fx.show(fx.dbg()->watch_panel());
        check("QLAZY-03", "Variables put on show: call tracking on; the Watches tab again: off",
              on && !fx.tracking(), "on=" + b2s(on) + " after=" + b2s(fx.tracking()));
    }

    // QLAZY-04 - the raster follows the Video panel.
    {
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->nextreg_panel());
        const bool off = !fx.raster();
        fx.show(fx.dbg()->video_panel());
        check("QLAZY-04", "another top tab put on show: live raster off; the Video tab again: on",
              off && fx.raster(), "off=" + b2s(off) + " after=" + b2s(fx.raster()));
    }

    // QLAZY-05 - paused inside a routine reached with tracking off; the Call Stack is shown now.
    {
        Fixture fx;
        fx.enable();
        fx.mgr->breakpoints().add(BreakpointModel::Execute, kSub + 1);
        const bool stopped = fx.tick_until_paused() && fx.pc() == kSub + 1;
        const bool untracked = !fx.tracking();
        fx.show(fx.dbg()->callstack_panel());
        CallStackPanel* csp = fx.dbg()->callstack_panel();
        QLabel* note = csp->findChild<QLabel*>("callStackNote");
        check("QLAZY-05",
              "paused inside a routine entered with tracking off, Call Stack put on show: row 0 is CALL* (8002 to 9000) and the note is shown with its text",
              stopped && untracked && cell(csp, 0, 1) == "CALL*" && cell(csp, 0, 2) == "8002" &&
                  cell(csp, 0, 3) == "9000" && note && note->isVisible() && note->text() == kNote,
              "stopped=" + b2s(stopped) + " untracked=" + b2s(untracked) + " type=" + cell(csp, 0, 1).toStdString() +
                  " note=" + b2s(note != nullptr && note->isVisible()));
    }

    // QLAZY-06 - the Call Stack on show BEFORE the CALL runs: a tracked frame, no note.
    {
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->callstack_panel());
        fx.mgr->breakpoints().add(BreakpointModel::Execute, kSub + 1);
        const bool stopped = fx.tick_until_paused() && fx.pc() == kSub + 1;
        CallStackPanel* csp = fx.dbg()->callstack_panel();
        QLabel* note = csp->findChild<QLabel*>("callStackNote");
        check("QLAZY-06",
              "Call Stack on show before the CALL runs: row 0 is CALL (tracked), the rebuilt-frames note is hidden",
              stopped && cell(csp, 0, 1) == "CALL" && note && note->isHidden(),
              "stopped=" + b2s(stopped) + " type=" + cell(csp, 0, 1).toStdString());
    }

    // QLAZY-07 - closing the window with the Call Stack on show leaves nothing on.
    {
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->callstack_panel());
        const bool was_on = fx.tracking() && fx.raster();
        fx.mgr->set_enabled(false);
        check("QLAZY-07", "window closed with the Call Stack on show: tracking off, raster off, no client attached",
              was_on && !fx.tracking() && !fx.raster() && !fx.backend->attached(),
              "tracking=" + b2s(fx.tracking()) + " raster=" + b2s(fx.raster()) + " attached=" + b2s(fx.backend->attached()));
    }

    // QLAZY-08 - reopened with the Call Stack still the current tab: tracking on at once.
    {
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->callstack_panel());
        fx.mgr->set_enabled(false);
        fx.enable();
        check("QLAZY-08", "window reopened with the Call Stack still the current tab: tracking on at once",
              fx.tracking() && fx.backend->attached(), "tracking=" + b2s(fx.tracking()));
    }

    // QLAZY-09 - a source Step Over over a CALL, the Call Stack hidden: tracking on for the step
    // only, so it lands on the next line of the caller, and tracking is off again afterwards.
    {
        Fixture fx;
        const std::string sld = write_source_files(fx, cfg.path().toStdString());
        fx.emu.debug_state().pause();
        fx.enable();
        fx.backend->load_source_map(sld, false);
        // Put the CPU on the CALL (line 11) and make the Call Stack hidden.
        Z80Registers r = fx.emu.cpu().get_registers();
        r.PC = 0x8002;
        r.SP = kSp;
        fx.emu.cpu().set_registers(r);
        fx.show(fx.dbg()->stack_panel());
        const bool off_before = !fx.tracking();
        fx.mgr->on_source_step_over();
        QApplication::processEvents();
        const bool landed = fx.pc() == 0x8005;
        check("QLAZY-09",
              "a source Step Over over a CALL with the Call Stack hidden lands on the next line of the caller, and tracking is off again after it",
              off_before && landed && !fx.tracking(),
              "off_before=" + b2s(off_before) + " pc=" + std::to_string(fx.pc()) + " tracking=" + b2s(fx.tracking()));
    }

    // QLAZY-12 - a source Step Out with the Call Stack hidden, stopped inside the routine: the tracker is on for the
    // step only (rebuilt from the stack at its start), so it lands on the caller's next line (main.bas:12 at $8005),
    // and tracking is off again after. Without the request the depth is 0 and Out would run one instruction.
    {
        Fixture fx;
        const std::string sld = write_source_files(fx, cfg.path().toStdString());
        fx.emu.debug_state().pause();
        fx.enable();
        fx.backend->load_source_map(sld, false);
        // Inside the routine: PC at its first instruction, the return address 8005 (CALL at 8002) on the stack.
        fx.emu.mmu().write(0xFEFE, 0x05);
        fx.emu.mmu().write(0xFEFF, 0x80);
        Z80Registers r = fx.emu.cpu().get_registers();
        r.PC = kSub;
        r.SP = 0xFEFE;
        fx.emu.cpu().set_registers(r);
        fx.show(fx.dbg()->stack_panel());
        const bool off_before = !fx.tracking();
        fx.mgr->on_source_step_out();
        QApplication::processEvents();
        const bool landed = fx.pc() == 0x8005;
        check("QLAZY-12",
              "a source Step Out inside a routine with the Call Stack hidden lands on the caller's next line, and tracking is off again after it",
              off_before && landed && !fx.tracking(),
              "off_before=" + b2s(off_before) + " pc=" + std::to_string(fx.pc()) + " tracking=" + b2s(fx.tracking()));
    }

    // QLAZY-10 - the seam: which panels are on show, and one signal per tab switch.
    {
        Fixture fx;
        fx.enable();
        DebuggerWindow* w = fx.dbg();
        int changes = 0;
        QObject::connect(w, &DebuggerWindow::panels_on_show_changed, [&changes]() { ++changes; });
        const bool shown = w->panel_on_show(w->video_panel()) && w->panel_on_show(w->stack_panel()) &&
                           w->panel_on_show(w->watch_panel()) && w->panel_on_show(w->cpu_panel()) &&
                           w->panel_on_show(w->disasm_panel());
        const bool hidden = !w->panel_on_show(w->callstack_panel()) && !w->panel_on_show(w->variables_panel()) &&
                            !w->panel_on_show(w->memory_panel()) && !w->panel_on_show(w->breakpoint_panel()) &&
                            !w->panel_on_show(w->nextreg_panel()) && !w->panel_on_show(w->audio_panel()) &&
                            !w->panel_on_show(w->script_panel()) && !w->panel_on_show(w->source_panel()) &&
                            !w->panel_on_show(w->symbols_panel());
        fx.show(w->nextreg_panel());
        const int after_switch = changes;
        fx.show(w->nextreg_panel());   // re-selecting the current tab
        const int after_reselect = changes;
        fx.show(w->video_panel());
        check("QLAZY-10",
              "panel_on_show() is true exactly for Video, Stack, Watches and the untabbed panels; one change signal per tab switch, none for re-selecting the current tab",
              shown && hidden && after_switch == 1 && after_reselect == 1 && changes == 2,
              "shown=" + b2s(shown) + " hidden=" + b2s(hidden) + " changes=" + std::to_string(after_switch) + "," +
                  std::to_string(after_reselect) + "," + std::to_string(changes));
    }

    // QLAZY-11 - the A/B: the window open (default layout) changes nothing the machine does.
    {
        const auto closed = busy_run(false);
        const auto open = busy_run(true);
        bool same = closed.size() == 50 && open.size() == 50;
        std::string detail;
        for (size_t f = 0; same && f < closed.size(); ++f) {
            if (closed[f].state != open[f].state || closed[f].video != open[f].video ||
                closed[f].audio != open[f].audio) {
                same = false;
                detail = "frame " + std::to_string(f);
            }
        }
        check("QLAZY-11",
              "a busy program, 50 frames with the window closed and 50 with it open (default layout): identical state, video and audio hashes every frame",
              same, detail);
    }

    // QLAZY-13..16 - the Audio panel's live refresh (GH #529): every tick while the window is open, the Audio
    // dock is on show and its Chips sub-tab is current; otherwise nothing, and never through refresh_window().
    {
        uint64_t live_on = 0, panels_on = 0, live_off = 0, panels_off = 0;
        {
            Fixture fx;
            fx.enable();
            fx.show(fx.dbg()->audio_panel());
            fx.mgr->take_audio_live_refresh_count();
            fx.mgr->take_panel_refresh_count();
            for (int i = 0; i < 8; ++i) fx.tick();
            live_on = fx.mgr->take_audio_live_refresh_count();
            panels_on = fx.mgr->take_panel_refresh_count();
        }
        {
            Fixture fx;
            fx.enable();
            fx.show(fx.dbg()->nextreg_panel());   // the default layout tabs Audio away behind it
            fx.mgr->take_audio_live_refresh_count();
            fx.mgr->take_panel_refresh_count();
            for (int i = 0; i < 8; ++i) fx.tick();
            live_off = fx.mgr->take_audio_live_refresh_count();
            panels_off = fx.mgr->take_panel_refresh_count();
        }
        check("QLAZY-13",
              "running, Audio dock shown and Chips current: one live refresh per tick (8 in 8 ticks), none with the dock tabbed away, and the other panels' refresh count is the same either way",
              live_on == 8 && live_off == 0 && panels_on == panels_off,
              "live=" + std::to_string(live_on) + "/" + std::to_string(live_off) + " panels=" +
                  std::to_string(panels_on) + "/" + std::to_string(panels_off));
    }
    {
        uint64_t away = 0, closed_dock = 0, closed_window = 0;
        {
            Fixture fx;
            fx.enable();
            fx.show(fx.dbg()->nextreg_panel());
            fx.mgr->take_audio_live_refresh_count();
            for (int i = 0; i < 4; ++i) fx.tick();
            away = fx.mgr->take_audio_live_refresh_count();
        }
        {
            Fixture fx;
            fx.enable();
            fx.show(fx.dbg()->audio_panel());
            for (QDockWidget* d : fx.dbg()->findChildren<QDockWidget*>())
                if (d->widget() == fx.dbg()->audio_panel()) d->hide();
            QApplication::processEvents();
            fx.mgr->take_audio_live_refresh_count();
            for (int i = 0; i < 4; ++i) fx.tick();
            closed_dock = fx.mgr->take_audio_live_refresh_count();
        }
        {
            Fixture fx;
            fx.enable();
            fx.show(fx.dbg()->audio_panel());
            fx.mgr->set_enabled(false);
            fx.mgr->take_audio_live_refresh_count();
            for (int i = 0; i < 4; ++i) fx.tick();
            closed_window = fx.mgr->take_audio_live_refresh_count();
        }
        check("QLAZY-14",
              "no live refresh with the Audio dock tabbed away, with its dock closed, or with the window closed",
              away == 0 && closed_dock == 0 && closed_window == 0,
              "away=" + std::to_string(away) + " closed_dock=" + std::to_string(closed_dock) + " closed_window=" +
                  std::to_string(closed_window));
    }
    {
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->audio_panel());
        auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        if (tabs) tabs->setCurrentIndex(2);   // Log
        fx.mgr->take_audio_live_refresh_count();
        for (int i = 0; i < 4; ++i) fx.tick();
        const uint64_t log_tab = fx.mgr->take_audio_live_refresh_count();
        if (tabs) tabs->setCurrentIndex(0);   // Chips again
        for (int i = 0; i < 4; ++i) fx.tick();
        const uint64_t chips_tab = fx.mgr->take_audio_live_refresh_count();
        check("QLAZY-15", "the Audio dock shown with the Log sub-tab current: no live refresh; with Chips current again: one per tick",
              tabs && log_tab == 0 && chips_tab == 4, "log=" + std::to_string(log_tab) + " chips=" + std::to_string(chips_tab));
    }
    {
        const auto closed = busy_run(false, false);
        const auto live = busy_run(true, true);
        bool same = closed.size() == 50 && live.size() == 50;
        std::string detail;
        for (size_t f = 0; same && f < closed.size(); ++f) {
            if (closed[f].state != live[f].state || closed[f].video != live[f].video ||
                closed[f].audio != live[f].audio) {
                same = false;
                detail = "frame " + std::to_string(f);
            }
        }
        check("QLAZY-16",
              "a busy program, 50 frames with the window closed and 50 with the Audio panel refreshing live every frame: identical state, video and audio hashes every frame",
              same, detail);
    }


    // QLAZY-17 / 18 - the audio write log (GH #531): the window asks the backend to record it only while the
    // Audio dock is on show with its Log sub-tab current, and takes back only its own request.
    {
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->audio_panel());
        auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        const bool chips = !fx.backend->audio_log_enabled();   // Chips is the current sub-tab
        if (tabs) tabs->setCurrentIndex(2);                    // Log
        QApplication::processEvents();
        const bool log = fx.backend->audio_log_enabled();
        const uint64_t rev = fx.backend->state_revision();
        for (int i = 0; i < 4; ++i) fx.tick();
        const bool once = fx.backend->state_revision() == rev && fx.backend->audio_log_enabled();
        if (tabs) tabs->setCurrentIndex(0);                    // Chips again
        QApplication::processEvents();
        const bool chips_again = !fx.backend->audio_log_enabled();
        if (tabs) tabs->setCurrentIndex(2);
        fx.show(fx.dbg()->nextreg_panel());                    // the Audio dock tabbed away behind it
        const bool away = !fx.backend->audio_log_enabled();
        fx.show(fx.dbg()->audio_panel());
        const bool back = fx.backend->audio_log_enabled();
        fx.mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
        QApplication::processEvents();
        const bool closed = !fx.backend->audio_log_enabled();
        // Re-opened with Log current: the window asks again (the latch of the earlier request must not
        // survive the close, or nothing would ask and the tab would read "Not recording").
        fx.enable();
        fx.show(fx.dbg()->audio_panel());
        auto* tabs2 = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        if (tabs2) tabs2->setCurrentIndex(2);
        QApplication::processEvents();
        const bool reopened = tabs2 && fx.backend->audio_log_enabled();
        check("QLAZY-17",
              "window open, Audio dock shown: the log is on with Log current and off with Chips current, off with the dock tabbed away, on when shown again, off with the window closed, and on again when the window is re-opened with Log current; ticks change nothing",
              fx.ok && tabs && chips && log && once && chips_again && away && back && closed && reopened,
              "chips=" + b2s(chips) + " log=" + b2s(log) + " once=" + b2s(once) + " chips_again=" + b2s(chips_again) +
                  " away=" + b2s(away) + " back=" + b2s(back) + " closed=" + b2s(closed) + " reopened=" + b2s(reopened));
    }
    // QLAZY-25 / 26 - the DAC & Beeper sub-tab also needs the write log (GH #537): its Sample stream is
    // computed from it. Same request bit as Log, so the ring survives a switch between the two.
    {
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->audio_panel());
        auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        const bool chips = !fx.backend->audio_log_enabled();   // Chips is the current sub-tab
        if (tabs) tabs->setCurrentIndex(1);                    // DAC & Beeper
        QApplication::processEvents();
        const bool dac = fx.backend->audio_log_enabled();
        const uint64_t rev = fx.backend->state_revision();
        for (int i = 0; i < 4; ++i) fx.tick();
        const bool once = fx.backend->state_revision() == rev && fx.backend->audio_log_enabled();
        if (tabs) tabs->setCurrentIndex(0);                    // Chips again
        QApplication::processEvents();
        const bool chips_again = !fx.backend->audio_log_enabled();
        if (tabs) tabs->setCurrentIndex(1);
        fx.show(fx.dbg()->nextreg_panel());                    // the Audio dock tabbed away behind it
        const bool away = !fx.backend->audio_log_enabled();
        fx.show(fx.dbg()->audio_panel());
        const bool back = fx.backend->audio_log_enabled();
        fx.mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
        QApplication::processEvents();
        const bool closed = !fx.backend->audio_log_enabled();
        check("QLAZY-25",
              "window open, Audio dock shown: the log is on with DAC & Beeper current and off with Chips current, off with the dock tabbed away, on when shown again, off with the window closed; ticks change nothing",
              fx.ok && tabs && chips && dac && once && chips_again && away && back && closed,
              "chips=" + b2s(chips) + " dac=" + b2s(dac) + " once=" + b2s(once) + " chips_again=" + b2s(chips_again) +
                  " away=" + b2s(away) + " back=" + b2s(back) + " closed=" + b2s(closed));
    }
    {
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->audio_panel());
        auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        if (tabs) tabs->setCurrentIndex(2);                    // Log
        QApplication::processEvents();
        fx.emu.port().out(0x1F, 0x55);                         // a DAC write: an entry (dropped or applied)
        fx.emu.port().out(0xFE, 0x10);                         // and a beeper one
        const auto first = fx.backend->audio_log_entries(0);
        const size_t before = first ? first.value.size() : 0;
        if (tabs) tabs->setCurrentIndex(1);                    // DAC & Beeper: the same request bit
        QApplication::processEvents();
        const auto after = fx.backend->audio_log_entries(0);
        check("QLAZY-26",
              "Log current, entries recorded, then DAC & Beeper current: the log stays on and the entries recorded before the switch are still in audio_log_entries(0) (one request bit, the ring is not freed)",
              fx.ok && tabs && before >= 2 && fx.backend->audio_log_enabled() && bool(after) && after.value.size() >= before,
              "before=" + std::to_string(before) + " after=" + std::to_string(after ? after.value.size() : 0));
    }
    {
        Fixture fx;
        const auto other = fx.backend->attach(jnext::dbg::ClientInfo{"other", jnext::dbg::ClientKind::Test}).value;
        fx.backend->set_audio_log_enabled(other, true);
        fx.enable();
        fx.show(fx.dbg()->audio_panel());
        auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        if (tabs) tabs->setCurrentIndex(2);
        QApplication::processEvents();
        fx.mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
        QApplication::processEvents();
        const bool kept = fx.backend->audio_log_enabled();
        fx.backend->detach(other);
        const bool off = !fx.backend->audio_log_enabled();
        check("QLAZY-18",
              "another client asks for the log; the window opens with Log current and then closes: the log stays on (the window takes back only its own request); the other client's detach turns it off",
              fx.ok && tabs && kept && off, "kept=" + b2s(kept) + " off=" + b2s(off));
    }

    // QLAZY-19..21 - the Audio panel's Scopes sub-tab (GH #536): the source tap is requested only while the
    // window is open, the Audio dock is on show and Scopes is the current sub-tab; the live refresh follows.
    {
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->audio_panel());
        auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        const bool chips_off = !fx.backend->audio_scope_enabled();
        if (tabs) tabs->setCurrentIndex(4);   // Scopes
        QApplication::processEvents();
        const bool scopes_on = fx.backend->audio_scope_enabled();
        if (tabs) tabs->setCurrentIndex(0);   // Chips again, through sub_tab_changed
        QApplication::processEvents();
        const bool chips_again_off = !fx.backend->audio_scope_enabled();
        if (tabs) tabs->setCurrentIndex(4);
        QApplication::processEvents();
        const bool back_on = fx.backend->audio_scope_enabled();
        fx.show(fx.dbg()->nextreg_panel());   // the dock is tabbed away
        const bool away_off = !fx.backend->audio_scope_enabled();
        fx.show(fx.dbg()->audio_panel());
        const bool shown_on = fx.backend->audio_scope_enabled();
        fx.mgr->set_enabled(false);
        const bool closed_off = !fx.backend->audio_scope_enabled();
        // Reopen the window: the Audio dock and its Scopes sub-tab are still current, so the request is made again.
        fx.mgr->set_enabled(true);
        QApplication::processEvents();
        auto* tabs2 = fx.dbg() ? fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs") : nullptr;
        const bool still_scopes = tabs2 && tabs2->currentIndex() == 4 && fx.dbg()->audio_panel()->scopes_on_show();
        const bool reopened_on = still_scopes && fx.backend->audio_scope_enabled();
        check("QLAZY-19",
              "Audio on show: the tap is off with Chips current, on with Scopes current, off again with Chips, off with the dock tabbed away, on when it is shown again, off with the window closed, and on again when the window is reopened with Scopes still current",
              fx.ok && tabs && chips_off && scopes_on && chips_again_off && back_on && away_off && shown_on && closed_off && reopened_on,
              b2s(chips_off) + b2s(scopes_on) + b2s(chips_again_off) + b2s(back_on) + b2s(away_off) + b2s(shown_on) + b2s(closed_off) + b2s(still_scopes) + b2s(reopened_on));
    }
    // QLAZY-27 - a stem recording (GH #539) is not a view request: it belongs to the backend.
    {
        Fixture fx;
        QTemporaryDir td;
        fx.enable();
        fx.show(fx.dbg()->audio_panel());
        auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        if (tabs) tabs->setCurrentIndex(4);   // Scopes
        QApplication::processEvents();
        auto* view = fx.dbg()->audio_panel()->findChild<AudioScopeView*>("audio_scope_view");
        const bool started = td.isValid() && view && view->start_stems_recording(td.path());
        if (tabs) tabs->setCurrentIndex(0);   // Chips: the scope request goes, the recording stays
        QApplication::processEvents();
        const bool after_switch = !fx.backend->audio_scope_enabled() && fx.backend->stems_recording() &&
                                  fx.emu.audio_scope_tap() != nullptr;
        const uint64_t w0 = fx.emu.audio_scope_tap() ? fx.emu.audio_scope_tap()->stems->written : 0;
        for (int i = 0; i < 3; ++i) fx.tick();
        const uint64_t w1 = fx.emu.audio_scope_tap() ? fx.emu.audio_scope_tap()->stems->written : 0;
        fx.mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
        QApplication::processEvents();
        const bool closed = fx.backend->stems_recording() && fx.emu.audio_scope_tap() != nullptr;
        for (int i = 0; i < 3; ++i) fx.emu.run_frame();
        const uint64_t w2 = fx.emu.audio_scope_tap() ? fx.emu.audio_scope_tap()->stems->written : 0;
        const bool stopped = fx.backend->stems_record_stop(jnext::dbg::CLIENT_NONE) == jnext::dbg::Result::Ok &&
                             fx.emu.audio_scope_tap() == nullptr;
        check("QLAZY-27",
              "stems started from the Scopes tab, then Chips current, then the window closed: the scope request goes (audio_scope_enabled false) but the recording and the tap stay and the frames keep growing; after the stop the tap is null",
              fx.ok && started && after_switch && w1 > w0 && closed && w2 > w1 && stopped,
              "w0=" + std::to_string(w0) + " w1=" + std::to_string(w1) + " w2=" + std::to_string(w2));
    }
    {
        uint64_t scope_n = 0, chips_in_scope = 0, panels_scope = 0, panels_away = 0, scope_in_chips = 0, scope_in_siblings = 0;
        {
            Fixture fx;
            fx.enable();
            fx.show(fx.dbg()->audio_panel());
            auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
            if (tabs) tabs->setCurrentIndex(4);
            QApplication::processEvents();
            fx.mgr->take_audio_scope_refresh_count();
            fx.mgr->take_audio_live_refresh_count();
            fx.mgr->take_panel_refresh_count();
            for (int i = 0; i < 8; ++i) fx.tick();
            scope_n = fx.mgr->take_audio_scope_refresh_count();
            chips_in_scope = fx.mgr->take_audio_live_refresh_count();
            panels_scope = fx.mgr->take_panel_refresh_count();
        }
        {
            Fixture fx;
            fx.enable();
            fx.show(fx.dbg()->nextreg_panel());
            fx.mgr->take_panel_refresh_count();
            for (int i = 0; i < 8; ++i) fx.tick();
            panels_away = fx.mgr->take_panel_refresh_count();
        }
        {
            Fixture fx;
            fx.enable();
            fx.show(fx.dbg()->audio_panel());
            fx.mgr->take_audio_scope_refresh_count();
            for (int i = 0; i < 8; ++i) fx.tick();
            scope_in_chips = fx.mgr->take_audio_scope_refresh_count();
        }
        {
            // The sibling sub-tabs (DAC & Beeper, Log, Tracker) refresh no scopes either.
            Fixture fx;
            fx.enable();
            fx.show(fx.dbg()->audio_panel());
            auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
            for (int idx : {1, 2, 3}) {
                if (tabs) tabs->setCurrentIndex(idx);
                QApplication::processEvents();
                fx.mgr->take_audio_scope_refresh_count();
                for (int i = 0; i < 4; ++i) fx.tick();
                scope_in_siblings += fx.mgr->take_audio_scope_refresh_count();
            }
        }
        check("QLAZY-20",
              "running with the Audio dock shown and Scopes current: 8 scope refreshes in 8 ticks, no Chips live refresh, and the other panels' refresh count equals the tabbed-away run; with Chips, DAC & Beeper, Log or Tracker current: no scope refresh",
              scope_n == 8 && chips_in_scope == 0 && panels_scope == panels_away && scope_in_chips == 0 && scope_in_siblings == 0,
              "scope=" + std::to_string(scope_n) + " chips=" + std::to_string(chips_in_scope) + " panels=" +
                  std::to_string(panels_scope) + "/" + std::to_string(panels_away) + " scope_in_chips=" +
                  std::to_string(scope_in_chips) + " siblings=" + std::to_string(scope_in_siblings));
    }
    {
        const auto closed = busy_run(false, false);
        const auto scopes = busy_run(true, true, true);
        bool same = closed.size() == 50 && scopes.size() == 50;
        std::string detail;
        for (size_t f = 0; same && f < closed.size(); ++f) {
            if (closed[f].state != scopes[f].state || closed[f].video != scopes[f].video ||
                closed[f].audio != scopes[f].audio) {
                same = false;
                detail = "frame " + std::to_string(f);
            }
        }
        check("QLAZY-21",
              "a busy program, 50 frames with the window closed and 50 with the Scopes sub-tab on show and refreshing every frame (the tap on): identical state, video and audio hashes every frame",
              same, detail);
    }

    {
        // GH #534: the log is asked for while the Audio dock is on show with Log OR Tracker current.
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->audio_panel());
        auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        const bool chips = tabs && !fx.backend->audio_log_enabled();
        if (tabs) tabs->setCurrentIndex(3);                    // Tracker
        QApplication::processEvents();
        const bool tracker = fx.backend->audio_log_enabled();
        if (tabs) tabs->setCurrentIndex(2);                    // Log
        QApplication::processEvents();
        const uint64_t rev = fx.backend->state_revision();
        if (tabs) tabs->setCurrentIndex(3);                    // Tracker again: not a request change
        QApplication::processEvents();
        const bool switched = fx.backend->audio_log_enabled() && fx.backend->state_revision() == rev;
        if (tabs) tabs->setCurrentIndex(4);                    // Scopes: no log
        QApplication::processEvents();
        const bool scopes = !fx.backend->audio_log_enabled();
        if (tabs) tabs->setCurrentIndex(3);
        fx.show(fx.dbg()->nextreg_panel());                    // the Audio dock tabbed away behind it
        const bool away = !fx.backend->audio_log_enabled();
        fx.show(fx.dbg()->audio_panel());
        const bool back = fx.backend->audio_log_enabled();
        fx.mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
        QApplication::processEvents();
        const bool closed = !fx.backend->audio_log_enabled();
        check("QLAZY-22",
              "window open, Audio dock shown: the log is off with Chips current and on with Tracker current; Log then Tracker leaves it on and does not move state_revision (no request change); off with Scopes current, off with the dock tabbed away, on when shown again, off with the window closed",
              fx.ok && tabs && chips && tracker && switched && scopes && away && back && closed,
              "chips=" + b2s(chips) + " tracker=" + b2s(tracker) + " switched=" + b2s(switched) + " scopes=" + b2s(scopes) +
                  " away=" + b2s(away) + " back=" + b2s(back) + " closed=" + b2s(closed));
    }
    {
        // GH #534: a double-click on a Tracker row lands the machine on the START of that frame, paused.
        AudioFixture fx;
        fx.enable();
        fx.show(fx.dbg()->audio_panel());
        auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        if (tabs) tabs->setCurrentIndex(3);
        QApplication::processEvents();
        for (int i = 0; i < 10; ++i) fx.tick();
        fx.emu.debug_state().pause();
        fx.tick();                                             // the paused refresh
        auto* view = fx.dbg()->audio_panel()->findChild<AudioTrackerView*>();
        auto* table = fx.dbg()->audio_panel()->findChild<QTableView*>("audio_tracker_table");
        AudioTrackerModel* model = view ? view->model() : nullptr;
        int newest_row = -1;
        for (int r = model ? model->rowCount() - 1 : -1; r >= 0 && newest_row < 0; --r)
            if (model->frame_at(r)) newest_row = r;
        const uint32_t newest = newest_row >= 0 ? model->frame_at(newest_row)->frame : 0;
        const uint32_t target = newest - 3;
        int row = -1;
        for (int r = 0; model && r < model->rowCount(); ++r)
            if (model->frame_at(r) && model->frame_at(r)->frame == target) row = r;
        const int16_t start_r8 = row >= 0 ? model->frame_at(row)->before[0][8] : int16_t(-2);
        const bool found = row >= 0 && newest_row >= 0 && start_r8 >= 0;
        if (found) emit table->doubleClicked(model->index(row, 0));
        QApplication::processEvents();
        const uint32_t landed = fx.backend->time().frame;
        const bool at_frame = found && landed == target;
        const bool paused = fx.paused();
        const bool r8 = found && fx.backend->audio_state().chips[0].regs[8] == uint8_t(start_r8);
        fx.tick();
        bool no_later = model && model->rowCount() > 0;
        for (int r = 0; model && r < model->rowCount(); ++r)
            if (model->frame_at(r) && model->frame_at(r)->frame >= target) no_later = false;
        const bool rewind_last = model && model->rowCount() > 0 &&
                                 model->index(model->rowCount() - 1, 1).data().toString() == "rewind";
        // The rows before the `rewind` row are the same timeline: an in-range one stays rewindable.
        bool earlier_ok = false;
        for (int r = 0; model && r + 1 < model->rowCount() && !earlier_ok; ++r) {
            const jnext::dbg::AudioFrame* f = model->frame_at(r);
            if (!f) continue;
            const QModelIndex ix = model->index(r, 0);
            earlier_ok = ix.data(AudioTrackerModel::RewindableRole).toBool() &&
                         ix.data(Qt::ToolTipRole).toString() ==
                             QString("Double-click to rewind to the start of frame %1").arg(f->frame);
        }
        check("QLAZY-23",
              "Tracker current, 10 frames of an IM2 player (one R8 write per frame), paused: a double-click on the frame row three before the newest lands the machine on that frame (time().frame), paused, with R8 equal to the row's start value (what the frame before wrote); after the next refresh the last row is 'rewind' and no frame row is at or after the target; a frame row before the 'rewind' row is still rewindable (same timeline), with the Double-click tooltip",
              fx.ok && found && at_frame && paused && r8 && no_later && rewind_last && earlier_ok,
              "found=" + b2s(found) + " newest=" + std::to_string(newest) + " landed=" + std::to_string(landed) + " paused=" + b2s(paused) +
                  " r8=" + b2s(r8) + " no_later=" + b2s(no_later) + " rewind_last=" + b2s(rewind_last) + " earlier_ok=" + b2s(earlier_ok));
    }
    {
        // GH #534: while the machine is RUNNING a double-click does nothing.
        AudioFixture fx;
        fx.enable();
        fx.show(fx.dbg()->audio_panel());
        auto* tabs = fx.dbg()->audio_panel()->findChild<QTabWidget*>("audio_tabs");
        if (tabs) tabs->setCurrentIndex(3);
        QApplication::processEvents();
        for (int i = 0; i < 30; ++i) fx.tick();            // a running window redraws every 12th tick
        auto* view = fx.dbg()->audio_panel()->findChild<AudioTrackerView*>();
        auto* table = fx.dbg()->audio_panel()->findChild<QTableView*>("audio_tracker_table");
        AudioTrackerModel* model = view ? view->model() : nullptr;
        const int rows = model ? model->rowCount() : 0;
        const uint32_t before = fx.backend->time().frame;
        if (model && rows > 3) emit table->doubleClicked(model->index(rows / 2, 0));
        QApplication::processEvents();
        const bool unmoved = fx.backend->time().frame == before && !fx.paused();
        fx.tick();
        fx.tick();
        bool no_rewind = fx.backend->time().frame > before && !fx.paused();
        for (int r = 0; model && r < model->rowCount(); ++r)
            if (model->index(r, 1).data().toString() == "rewind") no_rewind = false;
        check("QLAZY-24",
              "Tracker current with rows (30 running ticks), the machine RUNNING: a double-click on a row changes nothing (time().frame does not move, the machine stays running), the machine keeps running afterwards and no 'rewind' row appears",
              fx.ok && rows > 3 && unmoved && no_rewind,
              "rows=" + std::to_string(rows) + " unmoved=" + b2s(unmoved) + " no_rewind=" + b2s(no_rewind));
    }
    {
        // The Audio tab's dropped-write table names the OUT it discarded: the window connects the
        // panel's signal to the Disassembly (GH #530).
        Fixture fx;
        fx.enable();
        fx.show(fx.dbg()->disasm_panel());
        const uint16_t before = fx.dbg()->disasm_panel()->selected_address();
        emit fx.dbg()->audio_panel()->show_in_disassembly_requested(0x8005);
        const uint16_t after = fx.dbg()->disasm_panel()->selected_address();
        check("QAUD-01", "the Audio panel's show_in_disassembly_requested(0x8005) makes the Disassembly select 0x8005 (the window's connect)",
              fx.ok && before != 0x8005 && after == 0x8005,
              "before=" + std::to_string(before) + " after=" + std::to_string(after));
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
