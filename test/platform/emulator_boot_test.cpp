// Shared frontend cold-boot sequence test (GitHub issue #40).
//
// No VHDL oracle: this is the *host* boot choreography, not emulated hardware.
// Its oracle is the contract stated in issue #40 and in the doc comment on
// emulator_frontend_cold_boot() (src/platform/emulator_boot.h):
//
//   1. the load file goes into the config the machine is rebuilt with;
//   2. the LIVE per-connector joystick sources are carried across, because they
//      are host-side mappings, not machine state — a source picked from the
//      Input menu must survive a boot, so carrying the STARTUP config's values
//      would silently revert it;
//   3. the machine is reconstructed (power-on defaults restored);
//   4. the frontend re-binds / re-wires / re-enumerates its host adapters;
//   5. stale pending work is dropped BEFORE new work is scheduled;
//   6. the load is re-scheduled with the same per-format delay the CLI uses;
//   7. the frontend tail runs last.
//
// The point of the shared driver is that the ORDER is owned in one place, so
// the order is what these rows assert — not merely that each step happened.
// Every hook is optional, because SdlApp has no window to re-bind and no frame
// pacer to rebase; a missing hook must be skipped, never crash.
//
// Every row below derives from that contract, never from reading the
// implementation back.
//
// Run: ./build/test/emulator_boot_test

#include "platform/emulator_boot.h"
#include "core/saveable.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for (const auto& e : v) { if (!s.empty()) s += ","; s += e; }
    return s;
}

/// A frontend test double: records which hooks ran, in what order, and with
/// what arguments.
struct FakeFrontend {
    std::vector<std::string> order;
    EmulatorConfig           rewire_cfg{};
    bool                     rewire_seen = false;
    std::string              scheduled_file;
    int                      scheduled_delay = -1;
    int                      schedule_calls  = 0;

    ColdBootHooks hooks()
    {
        ColdBootHooks h;
        h.rewire_host = [this](const EmulatorConfig& cfg) {
            order.push_back("rewire");
            rewire_cfg  = cfg;
            rewire_seen = true;
        };
        h.cancel_pending_work = [this]() { order.push_back("cancel"); };
        h.schedule_load = [this](const std::string& f, int d) {
            order.push_back("schedule");
            scheduled_file  = f;
            scheduled_delay = d;
            ++schedule_calls;
        };
        h.on_booted = [this]() { order.push_back("booted"); };
        return h;
    }
};

/// A cheap machine to boot: 48K is the smallest RAM and the fastest init, and
/// needs no SD image (same choice rewind_test makes).
EmulatorConfig base_config()
{
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    return cfg;
}

/// The emulator's full serialised state — every subsystem's save_state(). Used
/// to compare a booted machine against a freshly constructed one WITHOUT having
/// to guess which particular field a bare init() forgets to clear.
std::vector<uint8_t> snapshot(const Emulator& emu)
{
    StateWriter measure;
    emu.save_state(measure);
    std::vector<uint8_t> buf(measure.position());
    StateWriter w(buf.data(), buf.size());
    emu.save_state(w);
    return buf;
}

/// Put visible mileage on a machine: run frames and dirty RAM, so "the boot
/// restored power-on state" is a claim with something to restore.
void dirty(Emulator& emu)
{
    for (int i = 0; i < 3; ++i) emu.run_frame();
    for (uint16_t a = 0x8000; a < 0x8100; ++a)
        emu.mmu().write(a, static_cast<uint8_t>(0xA5 ^ a));
}

}  // namespace

int main()
{
    std::printf("emulator_boot_test (shared frontend cold-boot sequence, issue #40)\n");

    // --- EB-01: the whole sequence runs, in the contracted order ------------
    // A clean boot (no load file) runs every hook the frontend supplied except
    // schedule_load, which has nothing to schedule.
    {
        Emulator emu;
        emu.init(base_config());
        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, base_config(), "", fe.hooks());
        check("EB-01", "clean boot order is rewire,cancel,booted",
              fe.order == std::vector<std::string>({"rewire", "cancel", "booted"}),
              join(fe.order));
    }

    // --- EB-02: a boot WITH a load file schedules it, still in order --------
    // schedule_load lands between cancel and booted: new work is scheduled only
    // after the stale work is dropped.
    {
        Emulator emu;
        emu.init(base_config());
        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, base_config(), "game.nex", fe.hooks());
        check("EB-02", "load boot order is rewire,cancel,schedule,booted",
              fe.order ==
                  std::vector<std::string>({"rewire", "cancel", "schedule", "booted"}),
              join(fe.order));
    }

    // --- EB-03: stale pending work is cancelled BEFORE the new load ---------
    // Asserted on its own, not merely implied by the full-order rows above: if
    // the order were inverted the boot would cancel the load it just scheduled
    // and nothing would ever load.
    {
        Emulator emu;
        emu.init(base_config());
        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, base_config(), "game.nex", fe.hooks());
        auto pos = [&](const std::string& s) {
            for (size_t i = 0; i < fe.order.size(); ++i)
                if (fe.order[i] == s) return static_cast<int>(i);
            return -1;
        };
        check("EB-03", "cancel_pending_work precedes schedule_load",
              pos("cancel") >= 0 && pos("schedule") > pos("cancel"),
              join(fe.order));
    }

    // --- EB-04: cancel_pending_work runs even with nothing to load ----------
    // A clean boot must still drop a countdown left over from before it,
    // otherwise a reset would inherit a load the user already cancelled.
    {
        Emulator emu;
        emu.init(base_config());
        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, base_config(), "", fe.hooks());
        bool cancelled = false;
        for (const auto& e : fe.order) if (e == "cancel") cancelled = true;
        check("EB-04", "clean boot still cancels pending work", cancelled,
              join(fe.order));
    }

    // --- EB-05: no load file => schedule_load is NOT called ------------------
    {
        Emulator emu;
        emu.init(base_config());
        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, base_config(), "", fe.hooks());
        check("EB-05", "clean boot schedules no load", fe.schedule_calls == 0,
              "calls=" + std::to_string(fe.schedule_calls));
    }

    // --- EB-06: the load file reaches schedule_load unchanged ---------------
    {
        Emulator emu;
        emu.init(base_config());
        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, base_config(), "/tmp/some game.nex", fe.hooks());
        check("EB-06", "scheduled file is the file passed in",
              fe.scheduled_file == "/tmp/some game.nex", fe.scheduled_file);
    }

    // --- EB-07: the scheduled delay is the CLI per-format delay -------------
    // A menu load must be indistinguishable from launching with --load <file>,
    // so the delay comes from emulator_load_delay_frames(). Formats asserted
    // one per row: an assertion that ORed them together could not tell a .tzx
    // delay applied to a .nex from the correct table.
    {
        struct { const char* file; int want; } cases[] = {
            { "a.nex", 0 }, { "a.tap", 0 }, { "a.sna", 0 }, { "a.szx", 0 },
            { "a.z80", 0 }, { "a.rzx", 0 }, { "a.tzx", 100 }, { "a.wav", 100 },
        };
        for (const auto& c : cases) {
            Emulator emu;
            emu.init(base_config());
            FakeFrontend fe;
            emulator_frontend_cold_boot(emu, base_config(), c.file, fe.hooks());
            check("EB-07", "scheduled delay matches the CLI per-format delay",
                  fe.scheduled_delay == c.want,
                  std::string(c.file) + ": got " + std::to_string(fe.scheduled_delay) +
                      ", want " + std::to_string(c.want));
        }
    }

    // --- EB-08: the load file is put into the config the machine boots with -
    {
        Emulator emu;
        emu.init(base_config());
        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, base_config(), "game.nex", fe.hooks());
        check("EB-08", "boot config carries the load file",
              fe.rewire_seen && fe.rewire_cfg.load_file == "game.nex",
              fe.rewire_cfg.load_file);
    }

    // --- EB-09: a clean boot clears the load file in the config -------------
    // The base config may still hold the file from a previous launch (--load);
    // a clean boot must not silently re-load it.
    {
        Emulator emu;
        emu.init(base_config());
        FakeFrontend fe;
        EmulatorConfig stale = base_config();
        stale.load_file = "previous.nex";
        emulator_frontend_cold_boot(emu, stale, "", fe.hooks());
        check("EB-09", "clean boot clears a stale load file from the config",
              fe.rewire_seen && fe.rewire_cfg.load_file.empty(),
              fe.rewire_cfg.load_file);
    }

    // --- EB-10: the LIVE joystick source of connector 0 is carried across ---
    // The regression this exists for: carrying the startup config's value would
    // revert an Input-menu change on every boot. Connector 0 only, so a driver
    // that carried just one connector cannot pass by accident.
    {
        Emulator emu;
        emu.init(base_config());
        emu.set_joystick_source(0, JoySource::CursorKeys);
        EmulatorConfig startup = base_config();
        startup.joy_source[0] = JoySource::Sdl;   // stale startup value
        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, startup, "", fe.hooks());
        check("EB-10", "connector 0's live source is carried, not the startup one",
              fe.rewire_seen && fe.rewire_cfg.joy_source[0] == JoySource::CursorKeys,
              "got " + std::to_string(static_cast<int>(fe.rewire_cfg.joy_source[0])));
    }

    // --- EB-11: connector 1's live source is carried across, independently --
    {
        Emulator emu;
        emu.init(base_config());
        emu.set_joystick_source(1, JoySource::CursorKeys);
        EmulatorConfig startup = base_config();
        startup.joy_source[1] = JoySource::Sdl;
        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, startup, "", fe.hooks());
        check("EB-11", "connector 1's live source is carried, not the startup one",
              fe.rewire_seen && fe.rewire_cfg.joy_source[1] == JoySource::CursorKeys,
              "got " + std::to_string(static_cast<int>(fe.rewire_cfg.joy_source[1])));
    }

    // --- EB-12: the two connectors are not transposed -----------------------
    // Set them to DIFFERENT values so a driver that copied source 0 into both
    // slots (or swapped them) fails. EB-10/EB-11 alone cannot see that.
    {
        Emulator emu;
        emu.init(base_config());
        emu.set_joystick_source(0, JoySource::CursorKeys);
        emu.set_joystick_source(1, JoySource::Sdl);
        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, base_config(), "", fe.hooks());
        check("EB-12", "per-connector sources are carried without transposition",
              fe.rewire_seen &&
                  fe.rewire_cfg.joy_source[0] == JoySource::CursorKeys &&
                  fe.rewire_cfg.joy_source[1] == JoySource::Sdl,
              "0=" + std::to_string(static_cast<int>(fe.rewire_cfg.joy_source[0])) +
                  " 1=" + std::to_string(static_cast<int>(fe.rewire_cfg.joy_source[1])));
    }

    // --- EB-13: the rest of the base config survives the boot ---------------
    // Only load_file and joy_source are overwritten; the frontend's startup
    // config is otherwise the config the machine is rebuilt with.
    {
        Emulator emu;
        emu.init(base_config());
        FakeFrontend fe;
        EmulatorConfig startup = base_config();
        startup.type = MachineType::ZX128K;
        emulator_frontend_cold_boot(emu, startup, "", fe.hooks());
        check("EB-13", "the base config's machine type reaches the boot",
              fe.rewire_seen && fe.rewire_cfg.type == MachineType::ZX128K,
              "got " + std::to_string(static_cast<int>(fe.rewire_cfg.type)));
    }

    // --- EB-14: the boot leaves a machine identical to a fresh one ----------
    // The driver's whole reason to exist is that it performs a POWER-ON boot,
    // not a bare init(): "reconstruct then init" is what restores every
    // power-on default, and "a bare init() leaves behind whatever state init()
    // does not explicitly clear" (issue #40; QtApp's own comment says the
    // result must be "byte-identical to a fresh startup").
    //
    // So the oracle is a fresh machine, compared over the FULL serialised
    // state — not a hand-picked field. Picking a field means guessing which
    // one init() forgets, and guessing wrong yields a row that passes against
    // a bare init() and proves nothing (the first cut of this row did exactly
    // that: it checked one RAM byte, which init() happens to clear anyway).
    {
        Emulator emu;
        emu.init(base_config());
        dirty(emu);
        const auto dirty_state = snapshot(emu);

        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, base_config(), "", fe.hooks());

        Emulator fresh;
        fresh.init(base_config());

        const auto booted_state = snapshot(emu);
        const auto fresh_state  = snapshot(fresh);
        check("EB-14a", "the dirtied machine really did differ from a fresh one",
              dirty_state != fresh_state,
              "sizes " + std::to_string(dirty_state.size()) + "/" +
                  std::to_string(fresh_state.size()));
        check("EB-14b", "after the cold boot the machine equals a fresh one",
              booted_state == fresh_state,
              "sizes " + std::to_string(booted_state.size()) + "/" +
                  std::to_string(fresh_state.size()));
    }

    // --- EB-15: a frontend that supplies NO hooks boots without crashing ----
    // Every hook is optional (SdlApp supplies no on_booted). An empty
    // std::function must be skipped, not invoked.
    {
        Emulator emu;
        emu.init(base_config());
        ColdBootHooks none;
        emulator_frontend_cold_boot(emu, base_config(), "game.nex", none);
        check("EB-15", "a boot with no hooks at all completes", true);
    }

    // --- EB-16: hooks are individually optional -----------------------------
    // Supply ONLY on_booted: the driver must still reach it with every earlier
    // hook empty. One row per hook would be ideal; this pins the tail, which is
    // the one the SDL frontend actually omits from the other end.
    {
        Emulator emu;
        emu.init(base_config());
        bool booted = false;
        ColdBootHooks h;
        h.on_booted = [&booted]() { booted = true; };
        emulator_frontend_cold_boot(emu, base_config(), "game.nex", h);
        check("EB-16", "on_booted runs even when every other hook is empty", booted);
    }

    std::printf("Total: %d, Passed: %d, Failed: %d, Skipped: 0\n",
                g_pass + g_fail, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
