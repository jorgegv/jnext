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
