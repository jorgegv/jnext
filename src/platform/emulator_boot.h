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
#include "core/program_archive.h"
#include "core/snapshot_machine.h"

#include "save/jns_container.h"
#include "save/zip_archive.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <vector>
#include <functional>
#include <new>
#include <optional>
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
    // GH #321 — a snapshot restores machine state, but the keys the HOST held
    // when it was taken (the Shift of the Alt+Shift+F5 chord) are not state to
    // come back to: their host release is long gone, so they would stay stuck.
    // After any snapshot load the guest starts with no key down; a key the
    // user still holds is re-asserted by its next event, as after a cold boot.
    auto snapshot = [&emu](bool ok) { if (ok) emu.keyboard().release_all_keys(); return ok; };
    if (ext == ".sna") return snapshot(emu.load_sna(file));
    if (ext == ".szx") return snapshot(emu.load_szx(file));
    if (ext == ".z80") return snapshot(emu.load_z80(file));
    if (ext == ".wav") return emu.load_wav(file);
    if (ext == ".rzx") return emu.load_rzx(file);
    if (ext == ".jns") return snapshot(emu.load_jns_file(file, tape_realtime));   // GH #27 S8; GH #402
    return emu.load_nex(file);   // .nex + unknown extensions
}

/// emulator_apply_load(), then tell the frontend which machine is running now
/// (GH #323). A load can switch the machine in place (a `.sna`, `.szx`, `.z80`
/// or `.jns` saved on another one), and a frontend that was not told keeps
/// naming the old machine: its next hard reset would boot that one. `keep`
/// receives the running machine whether or not the load succeeded or switched.
/// Every frontend's driver load (debugger/ZRCP/DZRP) and startup load uses it.
inline bool emulator_apply_load_keeping(Emulator& emu, const std::string& file,
                                        bool tape_realtime,
                                        const std::function<void(MachineType)>& keep) {
    const bool ok = emulator_apply_load(emu, file, tape_realtime);
    if (keep) keep(emu.config().type);
    return ok;
}

/// emulator_apply_load_keeping(), then re-assert the keys the host holds
/// (GH #386). A snapshot load wipes the guest keyboard matrix (the keys the
/// host held when the file was written are not state to come back to), and with
/// it any key the user pressed after the cold boot and before the load landed,
/// which the key Router believes is down. `router.reassert()` puts those back,
/// before the next frame. The windowed frontends' four load sites (the
/// deferred --load / menu load and the debugger driver's load, Qt and SDL) all
/// go through this one function, so none can forget it; the Router is a
/// template parameter to keep this header free of the input layer.
template <typename Router>
inline bool emulator_apply_load_keeping_keys(Emulator& emu, const std::string& file,
                                             bool tape_realtime,
                                             const std::function<void(MachineType)>& keep,
                                             Router& router) {
    const bool ok = emulator_apply_load_keeping(emu, file, tape_realtime, keep);
    router.reassert();
    return ok;
}

/// True when emulator_apply_load() above would route `file` to
/// Emulator::load_nex() — i.e. the extension is `.nex` or unrecognised (the
/// fallback). The GH #228 experimental-V1.3 gate applies to exactly that
/// route, so the GUI uses this to decide whether a selected file needs the
/// V1.3 probe at all. Must mirror emulator_apply_load()'s chain: every
/// extension with its own branch there is listed here.
inline bool emulator_load_routes_to_nex(const std::string& file) {
    // One extension table (program_archive::is_program_ext), shared with the
    // archive layer (GH #392): every extension with its own branch above is in it.
    auto dot = file.rfind('.');
    std::string ext;
    if (dot != std::string::npos) {
        ext = file.substr(dot);
        for (auto& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return !program_archive::is_program_ext(file) || ext == ".nex";
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

/// True when emulator_apply_load() routes `file` to Emulator::load_sna() /
/// load_szx() / load_z80() — the `.sna`, `.szx` and `.z80` extensions. Such a
/// file runs on the machine it was saved on (GH #323), so the GUI checks it is
/// loadable before it cold-boots the machine for it.
inline bool emulator_load_routes_to_snapshot(const std::string& file) {
    const std::string ext = snapshot_file_ext(file);
    return ext == "sna" || ext == "szx" || ext == "z80";
}

/// The machine a boot that loads `load_file` must build. A `.jns`, `.sna`,
/// `.szx` or `.z80` runs on the machine it was saved on (GH #321, #323), and
/// an RZX recording replays only on the machine it was made on, so for one that
/// names it (rzx::recorded_machine()) that machine; otherwise `configured` (a
/// snapshot jnext cannot run is refused by the load itself). Every boot that
/// loads one asks this: the command-line startup (main.cpp) and every cold boot
/// (emulator_cold_boot()), the route of the GUI's File > Load and File > Play
/// RZX Recording.
inline MachineType emulator_boot_machine(const std::string& load_file, MachineType configured) {
    if (emulator_load_routes_to_snapshot(load_file)) {
        const SnapshotMachine m = snapshot_file_machine(load_file);
        return m.ok ? m.machine : configured;
    }
    if (jnext::is_jns_path(load_file)) {
        // GH #321 — a `.jns` names its machine too. Boot that machine, so the
        // frontend's keep_machine hook records it; load_jns would otherwise
        // reconfigure behind the frontend's back and a later reset would boot
        // the old machine. An unreadable manifest returns `configured`: the
        // load refuses the file anyway.
        std::ifstream f(load_file, std::ios::binary);
        if (!f) return configured;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
        jnext::zip::Reader zip;
        std::string why, text;
        jnext::jns::Manifest m;
        std::vector<std::string> unknown;
        MachineType want = configured;
        if (zip.open(bytes.data(), bytes.size(), why) &&
            zip.read_text(jnext::jns::kManifestMember, text, why) &&
            jnext::jns::manifest_from_json(text, m, unknown, why) &&
            parse_machine_type(m.model.machine, want))
            return want;
        return configured;
    }
    if (!emulator_load_routes_to_rzx(load_file)) return configured;
    RzxRecording rec;
    MachineType  recorded = configured;
    if (rzx::parse(load_file, rec) && rzx::recorded_machine(rec, recorded)) return recorded;
    return configured;
}

/// GH #321 — the machine the COMMAND-LINE startup boots. An explicit --machine
/// wins over an RZX's recorded machine (load_rzx() warns), but never over a
/// `.jns`'s, `.sna`'s, `.szx`'s or `.z80`'s (GH #323): their loads always
/// switch to the snapshot's machine, so booting any other would leave the
/// frontend naming a machine that is not running.
inline MachineType emulator_startup_machine(const std::string& load_file,
                                            MachineType configured, bool machine_explicit) {
    if (jnext::is_jns_path(load_file) || emulator_load_routes_to_snapshot(load_file) ||
        !machine_explicit)
        return emulator_boot_machine(load_file, configured);
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
    // GH #93 — a card change still pending dies with the machine unless it is
    // carried. The loop owners service it BEFORE their cold-boot polls, so this
    // only catches a reconstruct from elsewhere (a debugger client's reset in
    // the pump): the change is then made right after the boot, not lost.
    auto pending_sd_change     = emu.take_sd_card_change_request();

    EmulatorConfig boot_cfg = cfg;
    boot_cfg.type = emulator_boot_machine(cfg.load_file, cfg.type);
    // GH #89 — a tape save survives the power-on reset, like a recorder left
    // recording: the live setting (the GUI may have started or stopped it
    // since `cfg` was made). The old machine finishes the file; the new one
    // appends to it.
    boot_cfg.tape_save_file = emu.config().tape_save_file;

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
    if (pending_sd_change) emu.carry_sd_card_change(std::move(*pending_sd_change));
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
    /// from the frontend's when the boot loads an RZX recording made on another
    /// machine or a snapshot saved on one (emulator_cold_boot()): that machine
    /// stays selected, as Machine > Machine Type would leave it.
    std::function<void(MachineType)> keep_machine;
};

/// Perform a full frontend cold boot. `base_cfg` is the frontend's startup
/// config; `load_file` empty means a clean boot with nothing loaded.
///
/// Order is the contract:
///   1. the load file goes into the config;
///   2. the LIVE per-connector joystick sources, the LIVE host output gain,
///      the LIVE magic-breakpoint toggle and the LIVE rewind setting (size,
///      paused) are carried across — they are
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
    cfg.joy_device[0]  = emu.joystick_device(0);   // GH #311
    cfg.joy_device[1]  = emu.joystick_device(1);
    cfg.audio_gain_db  = emu.mixer().output_gain_db();
    cfg.audio_gain_beeper_db = emu.mixer().beeper_gain_db();
    for (int chip = 0; chip < 3; ++chip)
        cfg.audio_gain_ay_db[chip] = emu.mixer().ay_gain_db(chip);
    cfg.audio_gain_dac_db = emu.mixer().dac_gain_db();
    cfg.magic_breakpoint  = emu.config().magic_breakpoint;
    cfg.rewind_buffer_frames = emu.config().rewind_buffer_frames;   // GH #400
    cfg.rewind_paused        = emu.config().rewind_paused;
    // GH #402 - Tape > Auto Play/Stop writes only the running machine; the frontends
    // boot from their STARTUP config, so without this every File > Load (a cold
    // boot) put auto play back to its startup value while the menu showed the
    // user's choice, and a .jns restored with the wrong one.
    cfg.tape_auto_play       = emu.tape_auto_play();

    emulator_cold_boot(emu, cfg);

    if (hooks.keep_machine)        hooks.keep_machine(emu.config().type);
    if (hooks.rewire_host)         hooks.rewire_host(cfg);
    if (hooks.cancel_pending_work) hooks.cancel_pending_work();
    if (!load_file.empty() && hooks.schedule_load)
        hooks.schedule_load(load_file, emulator_load_delay_frames(load_file));
    if (hooks.on_booted)           hooks.on_booted();
}

// ---------------------------------------------------------------------------
// GH #93 — a live SD-card change, performed at the end of a loop tick.
// ---------------------------------------------------------------------------

/// What a serviced card change did: the request, and why it failed (empty on
/// success).
struct SdCardChangeOutcome {
    Emulator::SdCardChange change;
    std::string            error;
};

/// Perform the card change a frontend action requested
/// (Emulator::request_sd_card_change), if any. Polled by a loop owner after its
/// frames, BEFORE its cold-boot polls, so a reset raised in the same tick boots
/// the new card.
///
/// The card lives in TWO configs and both must follow it: the emulator's, which
/// change_sd_card() updates and an in-place init() (a program load) reads; and
/// the frontend's own, from which every cold boot (F1, NR 0x02, Reset, a menu
/// load, a debugger reset) rebuilds the machine. Update only the first and the
/// next hard reset silently puts the OLD card back. An eject changes neither:
/// jnext reads the machine's ROMs from that card at power-on.
inline std::optional<SdCardChangeOutcome>
emulator_service_sd_card_change(Emulator& emu, EmulatorConfig& frontend_cfg,
                                bool& frontend_cfg_set) {
    std::optional<Emulator::SdCardChange> request = emu.take_sd_card_change_request();
    if (!request) return std::nullopt;
    SdCardChangeOutcome outcome{*request, emu.change_sd_card(*request)};
    if (outcome.error.empty() && !request->image.empty()) {
        frontend_cfg.sd_card_image    = request->image;
        frontend_cfg.sd_card_readonly = request->read_only;
        frontend_cfg_set              = true;
    }
    return outcome;
}
