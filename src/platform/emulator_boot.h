#pragma once

// Task 70 — shared power-on cold-boot + load-dispatch helpers.
//
// A hard reset (Reset button / F1 / a program's NR 0x02 bit 1) is modelled as a
// power-on COLD BOOT the host frontend performs: reconstruct the Emulator in
// place and re-run the proven startup init(). These helpers live in ONE place so
// the Qt / SDL / headless frontends (and cold_boot) cannot diverge — the review
// of the first cut found the Qt menu path had silently dropped the `.rzx` branch
// because the format dispatch was copy-pasted three times.

#include "core/emulator.h"
#include "core/emulator_config.h"

#include <cctype>
#include <functional>
#include <new>
#include <string>
#include <utility>

/// Apply a load file by extension — the single source of truth for the `--load`
/// / menu-load format table. `tape_realtime` selects real-time vs fast tape
/// loading for .tap/.tzx. Returns the loader's success flag (callers may log).
inline bool emulator_apply_load(Emulator& emu, const std::string& file,
                                bool tape_realtime) {
    std::string ext;
    auto dot = file.rfind('.');
    if (dot != std::string::npos) {
        ext = file.substr(dot);
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext == ".tap") return emu.load_tap(file, !tape_realtime);
    if (ext == ".tzx") return emu.load_tzx(file, !tape_realtime);
    if (ext == ".sna") return emu.load_sna(file);
    if (ext == ".szx") return emu.load_szx(file);
    if (ext == ".z80") return emu.load_z80(file);
    if (ext == ".wav") return emu.load_wav(file);
    if (ext == ".rzx") return emu.load_rzx(file);
    if (ext == ".jns") return emu.load_jns_file(file);   // GH #27 S8
    return emu.load_nex(file);   // .nex + unknown extensions
}

/// True when emulator_apply_load() above would route `file` to
/// Emulator::load_nex() — i.e. the extension is `.nex` or unrecognised (the
/// fallback). The GH #228 experimental-V1.3 gate applies to exactly that
/// route, so the GUI uses this to decide whether a selected file needs the
/// V1.3 probe at all. Must mirror emulator_apply_load()'s chain: every
/// extension with its own branch there is listed here.
inline bool emulator_load_routes_to_nex(const std::string& file) {
    std::string ext;
    auto dot = file.rfind('.');
    if (dot != std::string::npos) {
        ext = file.substr(dot);
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext != ".tap" && ext != ".tzx" && ext != ".sna" && ext != ".szx" &&
           ext != ".z80" && ext != ".wav" && ext != ".rzx" && ext != ".jns";
}

/// True when emulator_apply_load() above routes `file` to Emulator::load_rzx()
/// — the `.rzx` extension. The GUI checks such a file is playable before it
/// cold-boots the machine for it.
inline bool emulator_load_routes_to_rzx(const std::string& file) {
    std::string ext;
    auto dot = file.rfind('.');
    if (dot != std::string::npos) {
        ext = file.substr(dot);
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".rzx";
}

/// The machine a boot that loads `load_file` must build. An RZX recording
/// replays only on the machine it was made on, so for one that names it
/// (rzx::recorded_machine()) that machine; otherwise `configured`. Every boot
/// that plays a recording asks this: the command-line startup (main.cpp, where
/// an explicit --machine wins) and every cold boot (emulator_cold_boot()), the
/// route of the GUI's File > Play RZX Recording and File > Load NEX File....
inline MachineType emulator_boot_machine(const std::string& load_file, MachineType configured) {
    if (!emulator_load_routes_to_rzx(load_file)) return configured;
    RzxRecording rec;
    MachineType  recorded = configured;
    if (rzx::parse(load_file, rec) && rzx::recorded_machine(rec, recorded)) return recorded;
    return configured;
}

/// The per-format boot delay the CLI startup uses (main.cpp): tape formats that
/// still key through BASIC need the machine at its prompt first; everything else
/// loads immediately. Kept here so cold_boot schedules the load identically.
inline int emulator_load_delay_frames(const std::string& file) {
    std::string ext;
    auto dot = file.rfind('.');
    if (dot != std::string::npos) {
        ext = file.substr(dot);
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return (ext == ".tzx" || ext == ".wav") ? 100 : 0;
}

/// Power-on cold boot: reconstruct the emulator in place (placement-new keeps
/// `&emu` stable, so host holders bound to the address / its sub-objects stay
/// valid) and re-run init(cfg) — the proven startup path. A `cfg.load_file`
/// that is an RZX recording boots the machine it was made on
/// (emulator_boot_machine()); the frontend keeps that machine for its later
/// boots (ColdBootHooks::keep_machine).
///
/// An RZX recording is finalised first (written, and ended — see
/// Emulator::end_rzx_at_reset()), and the per-path record of failed RZX writes
/// is carried across, so a recording lost here still fails the exit status.
///
/// The host debugger's breakpoints are NOT carried here: since GH #278 WP4c
/// they are subscriptions of the debugger backend (`jnext::dbg::Debugger`),
/// which lives outside `Emulator` and re-applies them after the reconstruct
/// (CTL-12 rule 2) — like a real hardware debugger, a target reset must not
/// silently discard them, and it does not. The debugger's per-source audio mute
/// mask IS preserved here: it is not machine state
/// (Emulator::set_audio_mute_mask()), and the Audio panel that set it survives
/// the boot without re-pushing it, so dropping it would leave a muted source
/// audible under an unticked box (GH #239, DAP-14). The transient run/step state
/// (paused, step mode, trace log) is intentionally not restored — the machine
/// starts fresh and running.
inline void emulator_cold_boot(Emulator& emu, const EmulatorConfig& cfg) {
    // An RZX recording running now is WRITTEN before the machine is destroyed
    // (it used to be destroyed with it, unwritten and unannounced), and ends
    // here: see Emulator::end_rzx_at_reset(). Whether that write worked is
    // carried across, so the exit status still reports it.
    emu.end_rzx_at_reset("the power-on reset");
    auto saved_rzx_failed      = emu.rzx_failed_outputs();

    const uint8_t saved_mute   = emu.audio_mute_mask();
    auto saved_esxdos_state    = emu.esxdos_stub_state();

    EmulatorConfig boot_cfg = cfg;
    boot_cfg.type = emulator_boot_machine(cfg.load_file, cfg.type);

    emu.~Emulator();
    new (&emu) Emulator();
    emu.init(boot_cfg);

    // ── GH #276 B3: THE BACKEND'S SHARE OF THIS RECONSTRUCT ─────────────────
    //
    // A `jnext::dbg::Debugger` may be ALIVE across this call, and B3's
    // `Impl::reapply_after_machine_rebuild()` (`src/debug/debugger_reconstruct.cpp`)
    // is what makes that survivable. It runs from the three routes that can land a
    // new machine — `Debugger::reset(Hard)`, `Debugger::load()` and
    // `Debugger::on_cold_boot_done()` — never from here, because this header sits
    // BELOW nothing: `src/debug/` must not be reachable from a platform header
    // that the pure core includes (rule 1 of `debug/debugger.h`'s banner). So the
    // items below are a CONTRACT WITH A CALLER, and it is the caller's job to
    // honour it after this function returns.
    //
    // AN EARLIER VERSION OF THIS COMMENT SAID "nothing in the tree holds a
    // `jnext::dbg::Debugger` across this call yet". THAT WAS FALSE when it was
    // written: `src/platform/headless_app.cpp`'s `JNEXT_BENCH_WATCH` fixture
    // (`bench_watch_dbg`) holds one for the whole of `HeadlessApp::run()`, cold
    // boots included, and is reachable via a guest NR 0x02 hard reset or
    // `JNEXT_DELAYED_RESET_TYPE=loadnex:` while the fixture is armed. It was
    // dormant only because the two bench workloads never reset mid-run. That
    // fixture now calls `on_cold_boot_done()` from its loop owner's `cold_boot`
    // lambda, which is rule 5 of CTL-12 and makes it the first real consumer of
    // the contract rather than its first casualty.
    //
    //  1. **Re-install the THREE publications.** `~Emulator()` + placement-new
    //     gives a BRAND-NEW `DebugState` at the same address, with
    //     `events_ == nullptr`, no drain/gate hooks and no machine-replaced hook.
    //     A surviving `Debugger` is then silently DISCONNECTED: every
    //     subscription still exists and lists as live, and not one can ever
    //     fire. The re-application re-runs the `Debugger` constructor's calls —
    //     `set_event_table`, `set_event_hooks`, `set_machine_replaced_hook` —
    //     plus `gates_changed()` and the eight-page seed. (The fourth hook, the
    //     latch stamper, is the Emulator's own: `init()` below re-installs it.)
    //
    //     `set_machine_replaced_hook` is the one that is easy to miss and the one
    //     whose absence is worst here, so it is named rather than left inside
    //     "the constructor's calls": this reset IS a machine transition, `init()`
    //     below calls `debug_after_machine_transition_()` for it, and with a null
    //     hook that call cannot reach `Debugger::Impl`. A `Debugger` that survived
    //     a cold boot would keep reporting the `PauseReason` of a machine that has
    //     been destructed. Re-installing it after `init()` means this boot's own
    //     transition is not reconciled through it, so the re-application ALSO arms
    //     `Kind::None` explicitly once, exactly as the hook would have.
    //  2. **Nothing of the debugger's is carried across by THIS function.**
    //     Until B3 it copied the whole `BreakpointSet` (the Qt panels' model,
    //     their observers, and the backend's event-mask half) and `active()`
    //     over the reconstruct; B3 retired the event-mask half, and GH #278
    //     WP4c the rest (B3 obligation 1): the Qt panels' breakpoints are the
    //     backend's subscriptions, their views follow the backend-side model,
    //     and `active()` is gone. So the rebuilt machine's gates are CLOSED and
    //     its `BreakpointSet` empty until the backend's re-application — the
    //     SINGLE owner — re-opens them from the live subscription table and
    //     re-mirrors the master switch (rows CTL-12-14/16/32/33).
    //  3. **The LATCH RING survives and its contents do not.** `EventTable` lives
    //     on `Debugger::Impl`, not on `Emulator`, so every entry latched by the
    //     destroyed machine is still in the ring and would be delivered at the
    //     rebuilt machine's first boundary as though it were current.
    //     `Emulator::load_state()` passes `discard_ring=true` for exactly this;
    //     a cold boot cannot, because the hook was null when `init()` fired it.
    //     The re-application clears the ring.
    //  4. **`debug_latch_reset(true)` must come AFTER (1) and (3).** Issued
    //     before `~Emulator()` it stamps through the OLD emulator's latch stamper
    //     into the OLD table and is thrown away with it; issued after `init()`
    //     but before (1) it is dropped on the null table; issued before (3) it is
    //     discarded with the stale entries.
    //
    // ── THE TWO RESTORES B3 LEFT STANDING ARE RETIRED (GH #278 WP4c) ─────────
    //
    // B3 kept the `BreakpointSet` / `active()` save-and-restore because, before
    // package Q, it was the only owner of the Qt panels' breakpoints, of the
    // observers that kept those panels subscribed, and of the bit that kept an
    // open window armed. Q moved all three to the backend: the breakpoints are
    // an observer client's subscriptions (REQ-qt-32), the panels follow
    // `BreakpointModel`, which never touches this object, and the open window
    // is an arming client whose attach `clients_changed()` re-derives. A
    // restore here would now be a second owner, so it is gone (qt-frontend.md
    // §7, B3 obligation 1).
    // ────────────────────────────────────────────────────────────────────────
    emu.set_audio_mute_mask(saved_mute);
    emu.restore_esxdos_stub_state(std::move(saved_esxdos_state));
    emu.restore_rzx_failed_outputs(std::move(saved_rzx_failed));
}

// ---------------------------------------------------------------------------
// Issue #40 — the WHOLE frontend cold-boot sequence, in one place.
// ---------------------------------------------------------------------------
//
// emulator_cold_boot() above only reconstructs the machine. Around it sits a
// fixed sequence of host-side steps that must happen in a fixed ORDER, and that
// sequence had been written out three times: QtApp::cold_boot(),
// SdlApp::cold_boot(), and MainWindow::on_machine_type() — which was a bare
// init() that did none of it, so a machine-type change came back with stale
// subsystem state and a dead gamepad. Divergence had already bitten twice
// before (the dropped `.rzx` branch that motivated emulator_apply_load(); the
// pad that died on every cold boot, issue #13 point 3).
//
// So the shared driver below owns the ORDER and the emulator-side steps, and
// the frontends supply ONLY the steps that are genuinely theirs.

/// The frontend-specific steps of a cold boot. Every hook is optional (an empty
/// std::function is simply not called), because the two frontends do not have
/// the same set: SdlApp has no window to re-bind and no frame pacer to rebase.
///
/// Hooks rather than a virtual interface: each of these is a one-line lambda
/// capturing the frontend `this`, the frontends are concrete singletons (there
/// is no polymorphic family to dispatch over), and every other seam in this
/// codebase between the core and a frontend already has this shape
/// (`MainWindow::LoadFileCallback`, `Emulator::on_joystick_source_changed`,
/// `Emulator::on_input_state_restored`). It also keeps this header free of Qt,
/// SDL and GamepadHost — it must stay includable by the pure core.
struct ColdBootHooks {
    /// Re-bind frontend objects to the reconstructed emulator and re-create,
    /// re-wire and RE-ENUMERATE the host input adapters. Receives the config
    /// the boot actually used, so the per-connector joystick sources carried
    /// across by the driver can be applied. Re-enumeration is not optional:
    /// SDL only emits DEVICEADDED for arrivals *after* a host is built, so a
    /// host rebuilt here never sees an already-plugged pad without it.
    std::function<void(const EmulatorConfig&)> rewire_host;

    /// Drop any inject / load countdown left pending from before the boot.
    /// Always called, so a boot can never inherit stale scheduled work.
    std::function<void()> cancel_pending_work;

    /// Schedule the load file with the per-format delay the CLI startup uses,
    /// so a menu load is indistinguishable from launching with --load <file>.
    std::function<void(const std::string& file, int delay_frames)> schedule_load;

    /// Frontend tail, after the machine is up (QtApp re-anchors the frame
    /// pacer here: a cold boot is a restart, so the schedule rebases).
    std::function<void()> on_booted;

    /// Store the machine this boot built as the frontend's own, so every later
    /// boot (a hard reset, F1, Machine > Power Reset) builds it too. It differs
    /// from the frontend's only when the boot played an RZX recording made on
    /// another machine (emulator_cold_boot()): that machine stays selected, as
    /// Machine > Machine Type would leave it.
    std::function<void(MachineType)> keep_machine;
};

/// Perform a full frontend cold boot. `base_cfg` is the frontend's startup
/// config; `load_file` empty means a clean boot with nothing loaded.
///
/// Order is the contract:
///   1. the load file goes into the config;
///   2. the LIVE per-connector joystick sources, the LIVE host output gain
///      and the LIVE magic-breakpoint toggle are carried across — they are
///      host-side settings, not machine state, so a source picked from the
///      Input menu, a gain set in Preferences or Magic Breakpoint ticked in
///      the Debug menu must survive the boot (carrying `base_cfg`'s startup
///      values instead would silently revert them under a menu that still
///      shows them);
///   3. the machine is reconstructed, and the frontend keeps its type;
///   4. the frontend re-binds and re-wires its host adapters;
///   5. stale pending work is dropped BEFORE new work is scheduled;
///   6. the load is re-scheduled;
///   7. the frontend tail runs.
inline void emulator_frontend_cold_boot(Emulator& emu, EmulatorConfig base_cfg,
                                        const std::string& load_file,
                                        const ColdBootHooks& hooks) {
    EmulatorConfig cfg = std::move(base_cfg);
    cfg.load_file      = load_file;
    cfg.joy_source[0]  = emu.joystick_source(0);
    cfg.joy_source[1]  = emu.joystick_source(1);
    cfg.audio_gain_db  = emu.mixer().output_gain_db();
    cfg.audio_gain_beeper_db = emu.mixer().beeper_gain_db();
    for (int chip = 0; chip < 3; ++chip)
        cfg.audio_gain_ay_db[chip] = emu.mixer().ay_gain_db(chip);
    cfg.audio_gain_dac_db = emu.mixer().dac_gain_db();
    cfg.magic_breakpoint  = emu.config().magic_breakpoint;

    emulator_cold_boot(emu, cfg);

    if (hooks.keep_machine)        hooks.keep_machine(emu.config().type);
    if (hooks.rewire_host)         hooks.rewire_host(cfg);
    if (hooks.cancel_pending_work) hooks.cancel_pending_work();
    if (!load_file.empty() && hooks.schedule_load)
        hooks.schedule_load(load_file, emulator_load_delay_frames(load_file));
    if (hooks.on_booted)           hooks.on_booted();
}
