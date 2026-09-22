// Shared frontend cold-boot sequence test (GitHub issue #40).
//
// No VHDL oracle: this is the *host* boot choreography, not emulated hardware.
// Its oracle is the contract stated in issue #40 and in the doc comment on
// emulator_frontend_cold_boot() (src/platform/emulator_boot.h):
//
//   1. the load file goes into the config the machine is rebuilt with;
//   2. the LIVE per-connector joystick sources and the LIVE host output gain
//      are carried across, because they are host-side settings, not machine
//      state — a source picked from the Input menu or a gain set in
//      Preferences must survive a boot, so carrying the STARTUP config's
//      values would silently revert them;
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
// EB-20..EB-24 cover the other startup step all three frontends share: the
// command-line RZX requests, emulator_start_rzx() / emulator_finish_rzx()
// (src/platform/rzx_startup.h).
//
// Run: ./build/test/emulator_boot_test

#include "platform/emulator_boot.h"
#include "platform/rzx_startup.h"
#include "core/saveable.h"
#include "core/sna_saver.h"
#include "core/rzx.h"
#include "peripheral/esp_host_policy.h"
#include "peripheral/uart.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

/// Write `bytes` to `path`; false if any part of that fails.
bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}

/// A version-1, uncompressed 48K `.z80` image (canonical .z80 layout: 30-byte
/// header, PC at offset 6 non-zero => v1, byte 12 bit 5 clear => uncompressed,
/// then 0x4000-0xFFFF verbatim) with `marker` at `marker_addr`.
std::vector<uint8_t> z80_v1_image(uint16_t pc, uint16_t marker_addr, uint8_t marker)
{
    std::vector<uint8_t> img(30 + 49152, 0);
    img[6] = static_cast<uint8_t>(pc);
    img[7] = static_cast<uint8_t>(pc >> 8);
    img[8] = 0x00; img[9] = 0xFF;          // SP = 0xFF00
    img[12] = 0x02;                        // border 1, uncompressed
    img[29] = 0x01;                        // IM 1
    img[30 + (marker_addr - 0x4000)] = marker;
    return img;
}

/// An RZX file holding `snapshot` (typed `ext`) and two empty input frames.
bool write_rzx(const std::string& path, std::vector<uint8_t> snapshot, const std::string& ext)
{
    RzxRecording rec;
    rec.creator       = "EBTEST";
    rec.snapshot_data = std::move(snapshot);
    rec.snapshot_ext  = ext;
    rec.frames.resize(2);
    for (auto& fr : rec.frames) fr.instruction_count = 1;
    return rzx::write(path, rec);
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
    // Only load_file, joy_source and audio_gain_db are overwritten; the
    // frontend's startup config is otherwise the config the machine is
    // rebuilt with.
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

    // --- EB-15: a frontend that supplies NO hooks still boots the machine ---
    // Every hook is optional (SdlApp supplies no on_booted). An empty
    // std::function must be skipped, not invoked.
    //
    // Asserted on the resulting MACHINE, not on merely reaching the next line:
    // "it did not crash" is a tautology as an assertion (the linter is right to
    // reject it), and it would also pass if the driver bailed out before doing
    // any work at all. The reference is a fresh machine built with the same
    // config the driver constructs — so an empty hook must cost the boot
    // nothing. A driver that called an empty hook still dies here.
    {
        Emulator emu;
        emu.init(base_config());
        dirty(emu);
        ColdBootHooks none;
        emulator_frontend_cold_boot(emu, base_config(), "game.nex", none);

        EmulatorConfig ref_cfg = base_config();
        ref_cfg.load_file = "game.nex";
        Emulator fresh;
        fresh.init(ref_cfg);

        check("EB-15", "a boot with no hooks at all still boots the machine",
              snapshot(emu) == snapshot(fresh),
              "sizes " + std::to_string(snapshot(emu).size()) + "/" +
                  std::to_string(snapshot(fresh).size()));
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

    // --- EB-17: the LIVE host output gain is carried across (PR #41) --------
    // Same reason as EB-10: gain is a host-side setting, not machine state. A
    // Preferences change goes straight to the live Mixer, so carrying the
    // STARTUP config's value would silently revert it on every cold boot,
    // Machine > Power Reset and File > Load.
    {
        Emulator emu;
        emu.init(base_config());
        emu.mixer().set_output_gain_db(6.0f);      // live Preferences change
        EmulatorConfig startup = base_config();
        startup.audio_gain_db = 0.0f;              // stale startup value
        FakeFrontend fe;
        emulator_frontend_cold_boot(emu, startup, "", fe.hooks());
        check("EB-17a", "the live output gain is carried, not the startup one",
              fe.rewire_seen && fe.rewire_cfg.audio_gain_db == 6.0f,
              "got " + std::to_string(fe.rewire_cfg.audio_gain_db));
        check("EB-17b", "the booted machine's mixer runs at the live gain",
              emu.mixer().output_gain_db() == 6.0f,
              "got " + std::to_string(emu.mixer().output_gain_db()));
    }

    // --- EB-18: a LIVE emulated ESP survives the cold boot ------------------
    //
    // GH #25. This is the one hazard the ESP's whole ownership design exists to
    // defeat, and its failure mode is the nastiest in the codebase: this helper
    // does `emu.~Emulator(); new (&emu) Emulator();` — placement-new at the SAME
    // address — so a worker thread that outlived the destructor would go on
    // servicing a core whose sink points into the NEWLY BOOTED machine. Nothing
    // crashes and no sanitiser fires; the fresh machine's UART just starts
    // receiving a dead machine's bytes.
    //
    // The defence is that `~ThreadedEsp()` joins, and that the ESP members are
    // declared so they are destroyed before `uart_`. Nothing about that is
    // visible at a call site, so it is exercised HERE, against the real helper,
    // with a worker that has actually been started and run.
    //
    // Identity is checked by the connection log's instance id, not its address:
    // the replacement routinely lands on the block the old one was freed from,
    // so a pointer compare would pass for the wrong reason.
    {
        EmulatorConfig cfg = base_config();
        cfg.esp_enabled = true;

        Emulator emu;
        emu.init(cfg);
        check("EB-18a", "the ESP is up and attached to UART 0 before the boot",
              emu.esp_enabled() && emu.uart().device(0) != nullptr &&
                  emu.esp_events() != nullptr);

        emu.run_frame();   // the worker is started and has serviced the device
        const std::uint64_t id_before =
            emu.esp_events() ? emu.esp_events()->instance_id() : 0;

        emulator_cold_boot(emu, cfg);   // ~Emulator() -> placement-new -> init()

        check("EB-18b", "the cold boot returns with a REBUILT ESP, not the old one",
              emu.esp_enabled() && emu.esp_events() != nullptr &&
                  emu.esp_events()->instance_id() > id_before,
              "before=" + std::to_string(id_before) + " after=" +
                  std::to_string(emu.esp_events() ? emu.esp_events()->instance_id() : 0));
        check("EB-18c", "...attached to UART 0 of the machine that now exists",
              emu.uart().device(0) != nullptr);
        check("EB-18d", "...and the rebuilt machine runs",
              (emu.run_frame(), true));
    }

    // --- EB-19: a cold boot with the ESP disabled leaves it disabled --------
    // The discriminator for EB-18: the boot must not conjure an ESP the config
    // never asked for, which is the default and every run before GH #25.
    {
        Emulator emu;
        emu.init(base_config());
        emulator_cold_boot(emu, base_config());
        check("EB-19", "a cold boot with no --esp leaves UART 0 empty",
              !emu.esp_enabled() && emu.uart().device(0) == nullptr &&
                  emu.esp_events() == nullptr);
    }

    // --- EB-20..EB-24: the command-line RZX requests ------------------------
    // emulator_start_rzx() / emulator_finish_rzx() are what every frontend's
    // run() / shutdown() call for --rzx-play (and `--load x.rzx`, and a bare
    // x.rzx argument) and --rzx-record. Their contract, from the doc comment:
    // start playback and/or recording; return false exactly when the playback
    // file failed to load (the caller exits non-zero, and the machine keeps
    // running); finishing a recording is what writes the file. Which frontend
    // calls them, and when, is rzx-frontends-func's business, not these rows'.
    {
        const auto stamp = std::to_string(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const auto tmp       = std::filesystem::temp_directory_path();
        const std::string rec = (tmp / ("jnext-eb-rzx-" + stamp + ".rzx")).string();
        const std::string rec2 = (tmp / ("jnext-eb-rzx2-" + stamp + ".rzx")).string();
        const std::string bad = (tmp / ("jnext-eb-bad-" + stamp + ".rzx")).string();
        { std::ofstream f(bad, std::ios::binary); f << "NOTRZX NOTRZX NOTRZX"; }
        auto magic = [](const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            char m[4] = {0, 0, 0, 0};
            f.read(m, 4);
            return f.gcount() == 4 ? std::string(m, 4) : std::string();
        };

        // EB-20: nothing asked for, nothing started, and that is a success.
        {
            Emulator emu;
            emu.init(base_config());
            const bool ok = emulator_start_rzx(emu, "", "");
            check("EB-20", "no RZX request: returns true, starts neither playback nor recording",
                  ok && !emu.rzx_player().is_playing() && !emu.rzx_recorder().is_recording());
        }

        // EB-21: --rzx-record starts a recording; finishing it writes the file.
        std::size_t recorded_frames = 0;
        {
            Emulator emu;
            emu.init(base_config());
            const bool ok = emulator_start_rzx(emu, "", rec);
            check("EB-21a", "--rzx-record: returns true and the recorder is running",
                  ok && emu.rzx_recorder().is_recording());
            for (int i = 0; i < 3; ++i) emu.run_frame();
            emulator_finish_rzx(emu, rec);
            recorded_frames = emu.rzx_recorder().recording().frames.size();
            check("EB-21b", "finishing stops the recorder and writes an RZX! file",
                  !emu.rzx_recorder().is_recording() && magic(rec) == "RZX!" &&
                      recorded_frames > 0,
                  "magic='" + magic(rec) + "' frames=" + std::to_string(recorded_frames));
        }

        // EB-22: --rzx-play starts playback of every recorded frame.
        {
            Emulator emu;
            emu.init(base_config());
            const bool ok = emulator_start_rzx(emu, rec, "");
            check("EB-22a", "--rzx-play of a valid file: returns true and playback runs",
                  ok && emu.rzx_player().is_playing());
            check("EB-22b", "...with exactly the frames that were recorded",
                  emu.rzx_player().recording().frames.size() == recorded_frames,
                  "playing " + std::to_string(emu.rzx_player().recording().frames.size()) +
                      ", recorded " + std::to_string(recorded_frames));
        }

        // EB-23: a playback file that does not load is reported, so the
        // frontend can exit non-zero — and nothing pretends to play.
        {
            Emulator emu;
            emu.init(base_config());
            const bool ok = emulator_start_rzx(emu, bad, "");
            check("EB-23", "--rzx-play of a garbage file: returns false, no playback",
                  !ok && !emu.rzx_player().is_playing());
        }

        // EB-24: the two requests are independent — a failed playback does not
        // cancel a recording that was also asked for (the machine keeps running,
        // so the session it runs is still recorded).
        {
            Emulator emu;
            emu.init(base_config());
            const bool ok = emulator_start_rzx(emu, bad, rec2);
            emu.run_frame();
            emulator_finish_rzx(emu, rec2);
            check("EB-24", "failed playback + --rzx-record: returns false, still records",
                  !ok && magic(rec2) == "RZX!", "magic='" + magic(rec2) + "'");
        }

        std::remove(rec.c_str());
        std::remove(rec2.c_str());
        std::remove(bad.c_str());
    }

    // --- EB-25..EB-27: an RZX file's embedded snapshot ----------------------
    // Contract (Emulator::load_rzx / load_snapshot_from_memory): the snapshot
    // an RZX file carries is loaded from MEMORY — nothing is written to disk,
    // so two instances cannot race on a shared temporary file and the load
    // works where /tmp does not exist (Windows); every type jnext can load as
    // a file (sna, szx, z80) is accepted; a type it cannot load is a FAILURE,
    // because playing the input against a machine it was not recorded on
    // reproduces nothing.
    {
        const auto stamp = std::to_string(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const auto tmp = std::filesystem::temp_directory_path();
        const std::string sna_rzx = (tmp / ("jnext-eb-snap-sna-" + stamp + ".rzx")).string();
        const std::string z80_rzx = (tmp / ("jnext-eb-snap-z80-" + stamp + ".rzx")).string();
        const std::string odd_rzx = (tmp / ("jnext-eb-snap-odd-" + stamp + ".rzx")).string();
        constexpr uint16_t MARK_AT = 0x8000;
        constexpr uint8_t  MARK    = 0x5A;

        // A 48K SNA of a machine carrying the marker (the saver the recorder
        // itself embeds).
        std::vector<uint8_t> sna;
        {
            Emulator src;
            src.init(base_config());
            src.mmu().write(MARK_AT, MARK);
            sna = SnaSaver::save(src);
        }
        const bool fixtures_ok =
            !sna.empty() && write_rzx(sna_rzx, sna, "sna") &&
            write_rzx(z80_rzx, z80_v1_image(0x1234, MARK_AT, MARK), "z80") &&
            write_rzx(odd_rzx, z80_v1_image(0x1234, MARK_AT, MARK), "tzx");

        // EB-25: loads with the process UNABLE to create or grow any file
        // (RLIMIT_FSIZE 0, in a child so the limit dies with it). A snapshot
        // routed through a temporary file cannot load there.
        bool eb25 = false;
        std::string eb25_detail = fixtures_ok ? "" : "fixtures not written";
        if (fixtures_ok) {
#ifndef _WIN32
            std::fflush(stdout);
            std::fflush(stderr);
            const pid_t pid = fork();
            if (pid == 0) {
                // stdout/stderr may be redirected to a regular file, which the
                // limit would also stop: send them to a character device.
                std::freopen("/dev/null", "w", stdout);
                std::freopen("/dev/null", "w", stderr);
                Emulator emu;
                emu.init(base_config());
                const bool clean = emu.mmu().read(MARK_AT) != MARK;
                std::signal(SIGXFSZ, SIG_IGN);
                struct rlimit none{0, 0};
                setrlimit(RLIMIT_FSIZE, &none);
                const bool ok = clean && emu.load_rzx(sna_rzx) &&
                                emu.rzx_player().is_playing() &&
                                emu.mmu().read(MARK_AT) == MARK;
                std::_Exit(ok ? 0 : 1);
            }
            int status = 0;
            eb25 = pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
                   WEXITSTATUS(status) == 0;
            eb25_detail = "child status=" + std::to_string(status);
#else
            Emulator emu;
            emu.init(base_config());
            eb25 = emu.load_rzx(sna_rzx) && emu.rzx_player().is_playing() &&
                   emu.mmu().read(MARK_AT) == MARK;
#endif
        }
        check("EB-25", "an embedded SNA loads from memory, even when no file can be written",
              eb25, eb25_detail);

        // EB-26: an embedded .z80 snapshot is applied (registers AND memory),
        // not skipped with playback running against the current machine.
        {
            Emulator emu;
            emu.init(base_config());
            const bool ok = fixtures_ok && emu.load_rzx(z80_rzx);
            check("EB-26", "an embedded .z80 snapshot is loaded: its PC and memory",
                  ok && emu.rzx_player().is_playing() && emu.cpu().pc() == 0x1234 &&
                      emu.mmu().read(MARK_AT) == MARK,
                  "ok=" + std::to_string(ok) + " pc=" + std::to_string(emu.cpu().pc()));
        }

        // EB-27: a snapshot type jnext cannot load fails the load outright.
        {
            Emulator emu;
            emu.init(base_config());
            const bool ok = emu.load_rzx(odd_rzx);
            check("EB-27", "an unsupported embedded snapshot type fails, and nothing plays",
                  fixtures_ok && !ok && !emu.rzx_player().is_playing());
        }

        std::remove(sna_rzx.c_str());
        std::remove(z80_rzx.c_str());
        std::remove(odd_rzx.c_str());
    }

    // --- EB-28..EB-32: a recording that cannot be written is an error --------
    // Contract (Emulator::start_rzx_recording / stop_rzx_recording,
    // emulator_start_rzx / emulator_finish_rzx): a path that cannot be written
    // is refused at the start; a write that fails when the file is saved is
    // reported and latched per path; either way the frontend is told, so the
    // run exits non-zero. A running recording is never silently replaced.
    {
        const auto stamp = std::to_string(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const auto tmp = std::filesystem::temp_directory_path();
        const std::string nodir =
            (tmp / ("jnext-eb-nodir-" + stamp) / "sub" / "out.rzx").string();
        const std::string ok_path = (tmp / ("jnext-eb-ok-" + stamp + ".rzx")).string();
        const std::string ok2_path = (tmp / ("jnext-eb-ok2-" + stamp + ".rzx")).string();
        const std::string full = "/dev/full";   // opens; every flush fails ENOSPC

        // EB-28: an unwritable path is refused up front.
        {
            Emulator emu;
            emu.init(base_config());
            const bool ok = emu.start_rzx_recording(nodir);
            std::error_code ec;
            check("EB-28", "start_rzx_recording to an unwritable path: false, not recording",
                  !ok && !emu.rzx_recorder().is_recording() &&
                      !std::filesystem::exists(nodir, ec));
        }

        // EB-29: ...and the command-line helper reports it, so the frontend
        // exits non-zero.
        {
            Emulator emu;
            emu.init(base_config());
            const bool ok = emulator_start_rzx(emu, "", nodir);
            check("EB-29", "emulator_start_rzx with an unwritable --rzx-record: returns false",
                  !ok && !emu.rzx_recorder().is_recording());
        }

        // EB-30: a write that fails when the file is saved (a full disk) is
        // reported by stop, latched for that path, and makes the exit-time
        // helper fail — also when the stop happened earlier (the GUI's Stop).
        {
            Emulator emu;
            emu.init(base_config());
            const bool started = emu.start_rzx_recording(full);
            emu.run_frame();
            const bool stopped = emu.stop_rzx_recording();
            const bool finish = emulator_finish_rzx(emu, full);
            check("EB-30", "a failed write: stop false, latched, emulator_finish_rzx false",
                  started && !stopped && emu.rzx_output_failed(full) && !finish,
                  "started=" + std::to_string(started) + " stopped=" +
                      std::to_string(stopped) + " finish=" + std::to_string(finish));
        }

        // EB-31: starting a recording over a running one is refused; the
        // running recording keeps going and is written in full.
        {
            Emulator emu;
            emu.init(base_config());
            const bool first = emu.start_rzx_recording(ok_path);
            emu.run_frame();
            const bool second = emu.start_rzx_recording(ok2_path);
            emu.run_frame();
            const std::size_t frames = emu.rzx_recorder().recording().frames.size();
            const bool finish = emulator_finish_rzx(emu, ok_path);
            std::error_code ec;
            check("EB-31", "a second start is refused; the first recording keeps all its frames",
                  first && !second && frames == 2 && finish &&
                      std::filesystem::exists(ok_path, ec) &&
                      !std::filesystem::exists(ok2_path, ec),
                  "frames=" + std::to_string(frames));
        }

        // EB-32: control — a recording that was written leaves nothing latched
        // and the exit-time helper succeeds.
        {
            Emulator emu;
            emu.init(base_config());
            const bool started = emu.start_rzx_recording(ok2_path);
            emu.run_frame();
            const bool finish = emulator_finish_rzx(emu, ok2_path);
            check("EB-32", "control: a written recording finishes true and latches nothing",
                  started && finish && !emu.rzx_output_failed(ok2_path));
        }

        // EB-33: no recording during playback — every IN is answered from the
        // file being played and never reaches the recorder, so it would hold
        // no input at all.
        {
            Emulator emu;
            emu.init(base_config());
            bool playing = false;
            if (emu.start_rzx_recording(ok_path)) {
                emu.run_frame();
                playing = emu.stop_rzx_recording() && emu.load_rzx(ok_path);
            }
            const bool ok = emu.start_rzx_recording(ok2_path);
            check("EB-33", "start_rzx_recording during playback: refused, nothing records",
                  playing && !ok && !emu.rzx_recorder().is_recording());
        }

        std::remove(ok_path.c_str());
        std::remove(ok2_path.c_str());
    }

    // --- EB-34..EB-37: a reset the host performs ends a recording ------------
    // Contract (emulator_cold_boot, Emulator::end_rzx_at_reset): the power-on
    // cold boot and the host's F4 soft reset WRITE a running recording and end
    // it there — recorded input cannot replay a reset — instead of destroying
    // it unwritten; a failed write stays latched across the boot. A soft
    // reset the program itself asks for replays by itself and ends nothing.
    {
        const auto stamp = std::to_string(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const auto tmp = std::filesystem::temp_directory_path();
        const std::string cb_path = (tmp / ("jnext-eb-cb-" + stamp + ".rzx")).string();
        const std::string f4_path = (tmp / ("jnext-eb-f4-" + stamp + ".rzx")).string();
        const std::string sr_path = (tmp / ("jnext-eb-sr-" + stamp + ".rzx")).string();
        auto magic = [](const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            char m[4] = {0, 0, 0, 0};
            f.read(m, 4);
            return f.gcount() == 4 ? std::string(m, 4) : std::string();
        };

        // EB-34: the cold boot writes the recording — every frame of it — and
        // the file plays back.
        {
            Emulator emu;
            emu.init(base_config());
            const bool started = emu.start_rzx_recording(cb_path);
            for (int i = 0; i < 4; ++i) emu.run_frame();
            emulator_cold_boot(emu, base_config());
            Emulator player;
            player.init(base_config());
            const bool plays = player.load_rzx(cb_path);
            const std::size_t n = player.rzx_player().recording().frames.size();
            check("EB-34", "a cold boot writes the running recording (all 4 frames), ends it, "
                  "and the file plays back",
                  started && !emu.rzx_recorder().is_recording() && magic(cb_path) == "RZX!" &&
                      plays && n == 4 && !emu.rzx_output_failed(cb_path),
                  "frames=" + std::to_string(n));
        }

        // EB-35: a write that fails at the cold boot stays latched across it,
        // so the exit-time helper still reports it.
        {
            Emulator emu;
            emu.init(base_config());
            const bool started = emu.start_rzx_recording("/dev/full");
            emu.run_frame();
            emulator_cold_boot(emu, base_config());
            check("EB-35", "a write failed at the cold boot is still latched after it",
                  started && emu.rzx_output_failed("/dev/full") &&
                      !emulator_finish_rzx(emu, "/dev/full"));
        }

        // EB-36: the host's F4 soft reset writes and ends the recording too.
        {
            Emulator emu;
            emu.init(base_config());
            const bool started = emu.start_rzx_recording(f4_path);
            for (int i = 0; i < 3; ++i) emu.run_frame();
            emu.on_hotkey_f4_soft_reset();
            check("EB-36", "the F4 soft reset writes the running recording and ends it",
                  started && !emu.rzx_recorder().is_recording() && magic(f4_path) == "RZX!");
        }

        // EB-37: control — a soft reset the program performs (NR 0x02 bit 0
        // reaches Emulator::soft_reset()) is replayable, and ends nothing.
        {
            Emulator emu;
            emu.init(base_config());
            const bool started = emu.start_rzx_recording(sr_path);
            emu.run_frame();
            emu.soft_reset();
            emu.run_frame();
            const bool still = emu.rzx_recorder().is_recording();
            const std::size_t frames = emu.rzx_recorder().recording().frames.size();
            emulator_finish_rzx(emu, sr_path);
            check("EB-37", "control: a program's own soft reset leaves the recording running",
                  started && still && frames == 2, "frames=" + std::to_string(frames));
        }

        // EB-38: starting a playback ends a running recording by writing it
        // (it would otherwise run on, recording nothing: every IN now comes
        // from the file being played).
        {
            Emulator emu;
            emu.init(base_config());
            bool ready = emu.start_rzx_recording(f4_path);   // any finished RZX to play
            emu.run_frame();
            ready = ready && emu.stop_rzx_recording();
            const bool started = ready && emu.start_rzx_recording(sr_path);
            emu.run_frame();
            emu.run_frame();
            const bool plays = started && emu.load_rzx(f4_path);
            Emulator check_emu;
            check_emu.init(base_config());
            const bool saved = check_emu.load_rzx(sr_path) &&
                               check_emu.rzx_player().recording().frames.size() == 2;
            check("EB-38", "playing an RZX writes and ends the running recording",
                  plays && !emu.rzx_recorder().is_recording() && emu.rzx_player().is_playing() &&
                      saved);
        }

        std::remove(cb_path.c_str());
        std::remove(f4_path.c_str());
        std::remove(sr_path.c_str());
    }

    // --- EB-39..EB-41: the recording starts once the program is loaded -------
    // Contract (emulator_start_rzx_record_when_loaded): the command-line
    // recording starts only once nothing the command line puts into the
    // machine is pending, so the snapshot it embeds is the machine the recorded
    // input belongs to — the loaded program, not the one before the load.
    {
        const auto stamp = std::to_string(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const auto tmp = std::filesystem::temp_directory_path();
        const std::string rec_path = (tmp / ("jnext-eb-late-" + stamp + ".rzx")).string();
        constexpr uint16_t MARK_AT = 0x9000;
        constexpr uint8_t  MARK    = 0xC3;

        // EB-39: while a load is pending nothing records; once it is in, the
        // recording starts, and its snapshot holds what the load put there.
        {
            Emulator emu;
            emu.init(base_config());
            bool started = false;
            const bool ok1 = emulator_start_rzx_record_when_loaded(emu, rec_path, started, true);
            const bool early = emu.rzx_recorder().is_recording();
            emu.mmu().write(MARK_AT, MARK);          // the load lands
            const bool ok2 = emulator_start_rzx_record_when_loaded(emu, rec_path, started, false);
            const bool late = emu.rzx_recorder().is_recording();
            emu.run_frame();
            emulator_finish_rzx(emu, rec_path);
            Emulator player;
            player.init(base_config());
            const bool plays = player.load_rzx(rec_path);
            check("EB-39", "the recording waits for the pending load, and its snapshot holds it",
                  ok1 && !early && ok2 && late && plays && player.mmu().read(MARK_AT) == MARK,
                  "early=" + std::to_string(early) + " late=" + std::to_string(late));
        }

        // EB-40: it starts once — a later call (or a later load) does not
        // restart, and so cannot discard, the running recording.
        {
            Emulator emu;
            emu.init(base_config());
            bool started = false;
            emulator_start_rzx_record_when_loaded(emu, rec_path, started, false);
            emu.run_frame();
            emulator_start_rzx_record_when_loaded(emu, rec_path, started, false);
            emu.run_frame();
            const std::size_t frames = emu.rzx_recorder().recording().frames.size();
            emulator_finish_rzx(emu, rec_path);
            check("EB-40", "the recording starts once; a second call leaves it running",
                  started && frames == 2, "frames=" + std::to_string(frames));
        }

        // EB-41: the embedded snapshot carries the border the machine shows —
        // it was written as 0, so a program that set its border once replayed
        // with a black one.
        {
            Emulator emu;
            emu.init(base_config());
            emu.port().out(0x00FE, 0x05);
            const std::vector<uint8_t> sna = SnaSaver::save(emu);
            Emulator back;
            back.init(base_config());
            const bool loaded = back.load_snapshot_from_memory(sna, "sna", "EB-41");
            check("EB-41", "the SNA an RZX embeds records the border (5), and it loads back",
                  sna.size() > 26 && sna[26] == 5 && loaded && back.ula().get_border() == 5,
                  "byte26=" + std::to_string(sna.size() > 26 ? sna[26] : -1));
        }

        std::remove(rec_path.c_str());
    }

    // --- EB-42: a recording that continues from a second snapshot -----------
    // Contract (rzx::parse): jnext plays one snapshot and the input recorded
    // after it. A file that goes on from a second snapshot (the RZX format
    // allows it; FUSE writes one when a snapshot is inserted) is played up to
    // that second snapshot — never the second machine state with the first
    // session's input, which is what taking the LAST snapshot and every frame
    // amounted to.
    {
        const auto stamp = std::to_string(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const auto tmp = std::filesystem::temp_directory_path();
        const std::string a_path = (tmp / ("jnext-eb-seg-a-" + stamp + ".rzx")).string();
        const std::string b_path = (tmp / ("jnext-eb-seg-b-" + stamp + ".rzx")).string();
        constexpr uint16_t MARK_AT = 0x8800;
        auto session = [&](uint8_t mark, int frames) {
            RzxRecording rec;
            rec.creator = "EBTEST";
            rec.snapshot_data = z80_v1_image(0x1234, MARK_AT, mark);
            rec.snapshot_ext = "z80";
            rec.frames.resize(static_cast<std::size_t>(frames));
            for (auto& fr : rec.frames) fr.instruction_count = 1;
            return rec;
        };
        bool built = rzx::write(a_path, session(0xA1, 2)) && rzx::write(b_path, session(0xB2, 3));
        std::vector<uint8_t> a, b;
        if (built) {
            std::ifstream fa(a_path, std::ios::binary), fb(b_path, std::ios::binary);
            a.assign(std::istreambuf_iterator<char>(fa), {});
            b.assign(std::istreambuf_iterator<char>(fb), {});
            // Session B's blocks, minus its 10-byte header and 29-byte creator
            // block, appended to file A: [A snapshot][A input][B snapshot][B input].
            built = b.size() > 39;
            if (built) a.insert(a.end(), b.begin() + 39, b.end());
            built = built && write_bytes(a_path, a);
        }
        Emulator emu;
        emu.init(base_config());
        const bool plays = built && emu.load_rzx(a_path);
        const std::size_t n = emu.rzx_player().recording().frames.size();
        const uint8_t mark = emu.mmu().read(MARK_AT);
        check("EB-42", "a file continuing from a second snapshot plays its FIRST session only",
              plays && mark == 0xA1 && n == 2 &&
                  emu.rzx_player().recording().later_snapshots == 1,
              "mark=" + std::to_string(mark) + " frames=" + std::to_string(n));
        std::remove(a_path.c_str());
        std::remove(b_path.c_str());
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
