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
#include <utility>
#include <vector>

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

    /// A2.4 of the design: standing on the stop point counts as reaching it when another client holds
    /// the machine paused there, or a resume's step-off (GH #221) is about to skip our gate there. With
    /// neither, the gate fires at the next instruction and there is nothing to decide.
    static bool reached_here(bool paused_by_other, bool step_off_pending, bool here) {
        return here && (paused_by_other || step_off_pending);
    }

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
    void arm_tape();
    struct Watch {
        uint16_t lo = 0, hi = 0;
        jnext::dbg::MemSpace space = jnext::dbg::MemSpace::cpu();
    };
    // The subscriptions are made by subscribe() alone, never by a run-control verb: a verb would
    // resume, re-pause or re-arm a machine another client is driving (design A2.3). They are all
    // transient, so any client's stop removes them together; poll() notices that from the
    // subscriptions themselves (not from a pause it might not see) and puts them back.
    void subscribe_all();          ///< --break-at's Execute, or the tape's capture and every watch so far
    void subscribe_break_at();
    void subscribe_tape();
    bool have_subscriptions() const;
    bool here() const;             ///< the machine stands on the stop point right now
    void stop_taken(const std::string& why, bool reached_while_held);
    jnext::dbg::Subscription stopping(jnext::dbg::Subscription s);   ///< `s` + the delivery handler (fired_)
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
    std::vector<Watch>    watches_;      ///< the tape-loaded ranges seen at LD-BREAK, each once
    bool                  tape_rule1_ = false;
    bool                  fired_ = false;  ///< one of our subscriptions delivered the stop (set by its handler)
    uint16_t              fired_pc_ = 0;
    uint64_t              fired_cycle_ = 0;
    std::vector<std::pair<jnext::dbg::MemSpace, uint16_t>> starts_;   ///< --break-at's stop points, as resolved at the first arm
    bool                  break_source_line_ = false;
    std::string           armed_what_;   ///< the log's reason for a stop that is not --break-at's
};
