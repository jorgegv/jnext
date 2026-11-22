#pragma once

// ---------------------------------------------------------------------------
// The startup stop of `--debugger` / `--break-at` (GH #548).
//
// One object per loop owner (HeadlessApp, SdlApp, QtApp), declared AFTER its
// `debugger_` (it holds a backend client). It is a host-side client of the
// hosted `jnext::dbg::Debugger` (`ClientKind::CommandLine`, arming), and what it
// does is the whole feature: the machine starts paused where the program starts,
// or runs to `--break-at`, and the loop owner's existing machinery does the rest
// (the Qt debugger window opens on any pause it did not make; the SDL and
// headless loops serve the debugger protocol ports and wait).
//
// When the stop is armed: when the command-line program LANDS (the load block,
// after its sidecar symbols and `--source-map`; or after `--inject`), because a
// NEX or snapshot load re-initialises the machine and a sidecar symbol does not
// exist before it. With no program, at power-on.
//
// The stop is the client's own pause (or its one-shot run-to), so no other
// client's detach resumes it (SES-01), and the client detaches itself once the
// machine has run again after the stop, or when its stop was cancelled (another
// client paused first, or a cold boot).
//
// Toolkit-free: the same object serves the three loop owners.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <optional>
#include <string>

#include "core/emulator_config.h"
#include "debug/debugger.h"

class Emulator;

class StartupStop {
public:
    StartupStop() = default;
    ~StartupStop();
    StartupStop(const StartupStop&)            = delete;
    StartupStop& operator=(const StartupStop&) = delete;

    /// Attach the client (no-op unless `config.debugger_start`). `programs_coming` counts
    /// what the command line still has to put into the machine (a `--load`, an
    /// `--inject`, an RZX replay): 0 arms now (a pause at power-on, or `--break-at`).
    /// `has_window`: this run has a debugger window (the Qt GUI), so it does not wait
    /// for a remote debugger on a port. False with `why` set = exit 1.
    bool start(jnext::dbg::Debugger& dbg, Emulator& emu, const EmulatorConfig& config,
               int programs_coming, bool has_window, std::string& why);

    /// One command-line program is in the machine (call it after the sidecars and
    /// `--source-map`; for `--inject` right after the injection; `injected` = it was an
    /// `--inject`). `ok` is the loader's result. The last landing arms the stop. False
    /// with `why` set = exit 1 (an unresolved `--break-at`).
    bool program_landed(const std::string& file, bool ok, bool injected, std::string& why);

    /// Once per tick, after the pump: report the stop, and detach once the machine
    /// has run after it, or when it was cancelled.
    void poll();

    /// A cold boot cancels a stop that has not happened yet.
    void on_cold_boot();

    /// The client is attached (the stop is pending, or held and not yet run past).
    bool active() const { return cid_ != jnext::dbg::CLIENT_NONE; }
    jnext::dbg::ClientId client() const { return cid_; }

private:
    enum class Phase : uint8_t { Off, Waiting, Armed, Stopped, Done };

    struct Target {
        uint16_t addr = 0;
        jnext::dbg::MemSpace space = jnext::dbg::MemSpace::cpu();
        bool source_line = false;
        bool at_pc = false;   // the machine is already at the target: it is the stop
    };

    bool resolve_target(Target& t, std::string& why) const;
    bool arm(const std::string& default_why, std::string& why);
    void finish();
    void log_stop(const char* what);

    jnext::dbg::Debugger* dbg_ = nullptr;
    Emulator*             emu_ = nullptr;
    jnext::dbg::ClientId  cid_ = jnext::dbg::CLIENT_NONE;
    Phase                 phase_ = Phase::Off;
    std::string           break_at_;
    int                   landings_left_ = 0;
    bool                  last_ok_ = true;
    uint64_t              stop_cycle_ = 0;
};
