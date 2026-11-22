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
std::vector<batch_ab::FrameHash> busy_run(bool window_open, bool audio_live = false) {
    std::vector<batch_ab::FrameHash> out;
    Fixture fx;
    if (!fx.ok) return out;
    poke(fx.emu, kMain, {0x21, 0x00, 0x90, 0x34, 0x18, 0xFD});
    Z80Registers r = fx.emu.cpu().get_registers();
    r.PC = kMain;
    fx.emu.cpu().set_registers(r);
    if (window_open) fx.enable();
    if (audio_live) fx.show(fx.dbg()->audio_panel());
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
