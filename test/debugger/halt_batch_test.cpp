// The debugger window and the HALT batch - the FRONTEND half of GH #489.
//
// test/core/halt_batch_observer_test.cpp holds the core: a backend client attached the way the
// window attaches it leaves the HALT batch engaged on an idle program, and each real observer
// (a subscription, a step, coverage) refuses it. This suite drives the REAL DebuggerManager (the
// production attach_backend() path of set_enabled(true): the window is created and shown, the
// Gui client attached with the live raster and the call-stack tracker) and the real
// BreakpointModel, against a real Emulator running an IM2 EI;HALT program at 28 MHz.
//
// The oracle for the equivalence is the unbatched run of the same machine (per-frame video,
// audio and full state hashes, and the call-stack frames).
//
// Qt is required (DebuggerManager owns real QWidgets), no display: offscreen QPA.
// Run: ./build/test/debugger_halt_batch_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "debugger/breakpoint_model.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "../core/batch_ab.h"

#include <QApplication>
#include <QMainWindow>
#include <QTemporaryDir>

#include <cstdint>
#include <cstdio>
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

constexpr uint16_t kMain = 0xC000, kIsr = 0xC1C1, kSub = 0xC020, kIsrSub = 0xC300;
constexpr uint8_t  kIReg = 0xFD;

void nr(Emulator& e, uint8_t reg, uint8_t val) {
    e.port().out(0x243B, reg);
    e.port().out(0x253B, val);
}
void poke(Emulator& e, uint16_t addr, const std::vector<uint8_t>& b) {
    for (size_t i = 0; i < b.size(); ++i) e.mmu().write(static_cast<uint16_t>(addr + i), b[i]);
}

// IM 2, the main program CALLs a routine that starts with the HALT; the ISR CALLs a subroutine
// that counts (var+1) and RETs, then RETI. 28 MHz.
void build(Emulator& emu, bool batch) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    emu.set_cycle_batching_for_test(batch, batch);
    for (int i = 0; i < 257; ++i) emu.mmu().write(static_cast<uint16_t>((kIReg << 8) + i), 0xC1);
    poke(emu, kIsr, {0xF5, 0xCD, static_cast<uint8_t>(kIsrSub & 0xFF), static_cast<uint8_t>(kIsrSub >> 8),
                     0xF1, 0xFB, 0xED, 0x4D});
    poke(emu, kIsrSub, {0x3A, 0x01, 0xC2, 0x3C, 0x32, 0x01, 0xC2, 0xC9});
    poke(emu, kSub, {0x76, 0x18, 0xFD});
    poke(emu, kMain, {0x3E, kIReg, 0xED, 0x47, 0xED, 0x5E, 0xFB,
                      0xCD, static_cast<uint8_t>(kSub & 0xFF), static_cast<uint8_t>(kSub >> 8),
                      0x18, 0xFE});
    nr(emu, 0x03, 0x01);
    nr(emu, 0x07, 0x03);
    Z80Registers r = emu.cpu().get_registers();
    r.PC = kMain;
    r.SP = 0xBFF0;
    emu.cpu().set_registers(r);
}

// A real Emulator, a bare QMainWindow and a real DebuggerManager, the window NOT opened.
struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    QMainWindow win;
    DebuggerManager* mgr = nullptr;

    explicit Fixture(bool batch = true) {
        build(emu, batch);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        mgr = new DebuggerManager(&win, *backend, &win);   // parented: auto-freed
    }

    void run(int frames) {
        for (int i = 0; i < frames; ++i) emu.run_frame();
    }
    uint64_t batched() const { return emu.halt_repeats_batched_for_test(); }
    // Frames run so that the counter's movement can be read around them.
    bool grows_over(int frames) {
        const uint64_t before = batched();
        run(frames);
        return batched() > before;
    }
};

std::string frames_of(Emulator& e) {
    std::string s;
    for (const CallFrame& f : e.call_stack().frames()) {
        s += std::to_string(f.caller_pc) + "," + std::to_string(f.target_pc) + "," +
             std::to_string(f.sp_at_call) + "," + std::to_string(static_cast<int>(f.type)) + "," +
             std::to_string(static_cast<int>(f.caller_space.index)) + "," +
             std::to_string(static_cast<int>(f.target_space.index)) + ";";
    }
    return s;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // The debugger window saves into jnext.conf: the real ~/.jnext belongs to the user.
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    QApplication app(argc, argv);

    std::printf("\n======================================================\n");
    std::printf("Debugger window and the HALT batch - frontend (GH #489)\n");
    std::printf("======================================================\n\n");

    // DHB-01 - the window open (the production attach path) on an idle IM2 EI;HALT program: the batch
    // engages, and a twin with the batching off gives the identical per-frame state, video, audio and
    // call-stack frames. Also settles that opening the window creates no event subscription of its
    // own (a live subscription would refuse the batch): the backend lists none.
    {
        // Two machines cannot be stepped side by side (the CPU core's contention hooks are process
        // globals), so each run is made whole, one after the other.
        struct Out { std::vector<batch_ab::FrameHash> h; std::vector<std::string> cs; uint64_t batched = 0;
                     bool open = false, no_subs = false; size_t frames = 0; };
        batch_ab::Scratch scratch;
        auto run = [&](bool batch) {
            Out o;
            Fixture fx(batch);
            fx.mgr->set_enabled(true);
            for (int f = 0; f < 50; ++f) {
                fx.emu.run_frame();
                o.h.push_back(batch_ab::capture(fx.emu, scratch, true));
                o.cs.push_back(frames_of(fx.emu));
            }
            o.batched = fx.batched();
            o.open = fx.mgr->is_enabled();
            o.no_subs = fx.backend->subscriptions(true).empty();
            o.frames = fx.emu.call_stack().frames().size();
            return o;
        };
        const Out off = run(false);
        const Out on = run(true);
        bool same = true;
        std::string detail;
        for (size_t f = 0; f < on.h.size() && same; ++f) {
            const auto& a = on.h[f];
            const auto& b = off.h[f];
            if (a.state != b.state || a.video != b.video || a.audio != b.audio || on.cs[f] != off.cs[f]) {
                same = false;
                detail = "frame " + std::to_string(f) + (a.state != b.state ? " state" : "") +
                         (a.video != b.video ? " video" : "") + (a.audio != b.audio ? " audio" : "") +
                         (on.cs[f] != off.cs[f] ? " callstack [" + on.cs[f] + "] vs [" + off.cs[f] + "]" : "");
            }
        }
        check("DHB-01", "debugger window open on an idle EI;HALT program: the HALT batch engages (the window holds no subscription) and the state, video, audio and call-stack frames equal the unbatched run's every frame",
              same && on.batched > 0 && off.batched == 0 && on.open && on.no_subs && on.frames >= 1,
              detail + " batched=" + std::to_string(on.batched));
    }

    // DHB-02 - a breakpoint added through the window's BreakpointModel (the Qt path), at an unrelated
    // address, keeps the batch off while it exists; removing it through the model lets it engage again.
    {
        Fixture fx;
        fx.mgr->set_enabled(true);
        fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x9000);
        fx.run(2);                                    // settle
        const bool while_set = fx.grows_over(20);
        fx.mgr->breakpoints().remove(BreakpointModel::Execute, 0x9000);
        const bool after_remove = fx.grows_over(20);
        check("DHB-02", "a breakpoint added through the window's BreakpointModel keeps the HALT batch off for 20 frames; removing it lets the batch engage again",
              !while_set && after_remove,
              std::string("while_set=") + (while_set ? "grew" : "flat") + " after_remove=" + (after_remove ? "grew" : "flat"));
    }

    // DHB-03 - Step Over from the window on a CALL (a transient Execute at the next instruction, the
    // machine running into a routine that HALTs): no batching while the transient is in flight;
    // cancelled by a pause and a Run, the batch engages.
    {
        Fixture fx;
        fx.mgr->set_enabled(true);
        fx.run(3);
        // Stand on the CALL and pause there, as a user does before pressing Step Over.
        fx.mgr->on_pause();
        Z80Registers r = fx.emu.cpu().get_registers();
        r.PC = kMain + 7;
        r.IFF1 = r.IFF2 = 0;
        fx.emu.cpu().set_registers(r);
        fx.mgr->on_step_over();
        fx.run(1);
        const bool in_flight = !fx.grows_over(20);
        fx.mgr->on_pause();
        fx.mgr->on_run();
        const bool after = fx.grows_over(20);
        check("DHB-03", "Step Over from the window into a routine that HALTs: the HALT batch stays off while the transient breakpoint is in flight and engages after a pause and Run drops it",
              in_flight && after,
              std::string("in_flight=") + (in_flight ? "flat" : "grew") + " after=" + (after ? "grew" : "flat"));
    }

    // DHB-04 - the window CLOSED after use, with a breakpoint kept in the model (the GUI's observer
    // client holds it, dead because nothing arms the machine): the batch engages. Regression guard
    // for the closed-window behaviour that existed before the window-open case was opened up.
    {
        Fixture fx;
        fx.mgr->set_enabled(true);
        fx.mgr->breakpoints().add(BreakpointModel::Execute, 0x9000);
        fx.mgr->set_enabled(false);
        fx.run(2);
        const bool grows = fx.grows_over(20);
        check("DHB-04", "debugger window closed with a breakpoint kept in the model (nothing arms the machine): the HALT batch engages",
              !fx.emu.debug_state().armed() && grows,
              std::string("armed=") + (fx.emu.debug_state().armed() ? "1" : "0") + " " + (grows ? "grew" : "flat"));
    }

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
