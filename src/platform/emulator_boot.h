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
#include "debug/breakpoints.h"

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
           ext != ".z80" && ext != ".wav" && ext != ".rzx";
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
/// valid) and re-run init(cfg) — the proven startup path.
///
/// The host debugger's breakpoints and its active flag are PRESERVED across the
/// reset: they belong to the host debugger, and (like a real hardware debugger)
/// a target reset must not silently discard them. The transient run/step state
/// (paused, step mode, trace log) is intentionally not restored — the machine
/// starts fresh and running.
inline void emulator_cold_boot(Emulator& emu, const EmulatorConfig& cfg) {
    BreakpointSet saved_bps    = emu.debug_state().breakpoints();
    const bool    saved_active = emu.debug_state().active();
    auto saved_esxdos_state    = emu.esxdos_stub_state();

    emu.~Emulator();
    new (&emu) Emulator();
    emu.init(cfg);

    emu.debug_state().breakpoints() = std::move(saved_bps);
    emu.debug_state().set_active(saved_active);
    emu.restore_esxdos_stub_state(std::move(saved_esxdos_state));
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
};

/// Perform a full frontend cold boot. `base_cfg` is the frontend's startup
/// config; `load_file` empty means a clean boot with nothing loaded.
///
/// Order is the contract:
///   1. the load file goes into the config;
///   2. the LIVE per-connector joystick sources and the LIVE host output gain
///      are carried across — they are host-side settings, not machine state,
///      so a source picked from the Input menu or a gain set in Preferences
///      must survive the boot (carrying `base_cfg`'s startup values instead
///      would silently revert them);
///   3. the machine is reconstructed;
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

    emulator_cold_boot(emu, cfg);

    if (hooks.rewire_host)         hooks.rewire_host(cfg);
    if (hooks.cancel_pending_work) hooks.cancel_pending_work();
    if (!load_file.empty() && hooks.schedule_load)
        hooks.schedule_load(load_file, emulator_load_delay_frames(load_file));
    if (hooks.on_booted)           hooks.on_booted();
}
