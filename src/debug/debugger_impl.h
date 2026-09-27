#pragma once

// ---------------------------------------------------------------------------
// jnext::dbg::Debugger::Impl — ALL of the facade's state, internal to
// `jnext_debug`.
//
// Work package B1 of epic #276 (doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md
// §10.1). `debug/debugger.h` is the FROZEN published interface and holds one
// `unique_ptr<Impl>`; everything the backend remembers lives here, so B2's event
// table, B3's client list and B4's bookmarks are added WITHOUT editing a header
// five frontends compile against.
//
// THIS HEADER IS NOT PUBLISHED. It includes `core/emulator.h` deliberately —
// that is the point of the split, and `test/lint-debug-headers.sh` checks only
// the four published headers, never this one.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>

#include "core/emulator.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "debug/resume_guard.h"
#include "debug/symbol_table.h"

namespace jnext {
namespace dbg {

struct Debugger::Impl {
    explicit Impl(Emulator& e) : emu(e) {}

    Emulator& emu;

    /// CAP-SYM — THE symbol table: the panels' `@name`, the servers' lookups
    /// and `--map` all read this one, per §4.7. `DebuggerManager` still owns a
    /// second instance of its own; retiring it needs the Qt frontend to hold a
    /// `Debugger`, which is the loop-owner wiring of B3 / Q's WP2-WP6.
    SymbolTable symbols;

    /// SES-04 — what a `Stop` action does here. Held by the backend, set by the
    /// loop owner. `Pause` is the Qt default; a headless loop owner sets
    /// `ExitNonZero` when B3 wires it.
    StopPolicy stop_policy = StopPolicy::Pause;

    /// CTL-11 — the per-incident acknowledgment of a corrupt machine. The same
    /// pure policy object the Qt gate uses, moved DOWN here so a remote client
    /// and a script are gated by the same state the modal is.
    ResumeGuard guard;

    // ── CTL-13's pause_reason ────────────────────────────────────────────────
    //
    // Nothing in the tree records WHY the machine stopped: `DebugState::pause()`
    // clears the step mode and the hot loop consumes the data-breakpoint latch
    // in the same breath as the pause (`pause(); set_data_bp_hit(false);`), so
    // the evidence is gone before any backend call can look at it. The backend
    // therefore latches the reason for the stops IT causes, and `state()`
    // derives the rest from what is still readable (see `Debugger::state()`).
    //
    // `armed_reason` is the reason the LAST resume-family verb will produce when
    // the machine next stops; `armed_target` is that verb's target address,
    // where it has one. Both are re-armed by every control verb.
    PauseReason::Kind armed_reason = PauseReason::Kind::None;
    ClientId          armed_by     = CLIENT_NONE;
    uint16_t          armed_target = 0;
    bool              has_target   = false;

    // ── Convenience ─────────────────────────────────────────────────────────

    DebugState&       ds()       { return emu.debug_state(); }
    const DebugState& ds() const { return emu.debug_state(); }

    /// Arm the reason the next stop will report. Called by every control verb,
    /// including `run()` (which arms `None`: a stop after a free run is
    /// explained by the machine, not by the verb).
    void arm(PauseReason::Kind kind, ClientId by) {
        armed_reason = kind;
        armed_by     = by;
        has_target   = false;
        armed_target = 0;
    }

    void arm_target(PauseReason::Kind kind, ClientId by, uint16_t addr) {
        arm(kind, by);
        armed_target = addr;
        has_target   = true;
    }

    /// §4.2a — the one mutation log line, emitted by the BACKEND for every
    /// client's write. `what` is the target ("mem cpu:0x8000", "reg PC",
    /// "nextreg 0x15"), already formatted by the caller.
    void log_mutate(ClientId by, const std::string& what,
                    long long old_value, long long new_value);

    /// The same, for a write whose "old -> new" is not one value (a block poke,
    /// a pattern-RAM run). `detail` completes the line after the target.
    void log_mutate_range(ClientId by, const std::string& what,
                          const std::string& detail);

    /// ST-03 / CTL-09 / CTL-10 — why a rewind would be refused right now, or
    /// `Ok` if it would not. ONE predicate: `rewind_blocked()` greys the control
    /// with it and both rewind verbs gate on it, so what the UI shows and what
    /// the verb does cannot disagree (which is the whole point of ST-03).
    Result rewind_refusal() const;

    /// CTL-02/03/…/11 — may the machine execute? `RefusedCorrupt` while a
    /// failed rewind's incident is unacknowledged. The choke point every
    /// resume-family verb passes through, which is what
    /// `DebuggerManager::confirm_resume_if_corrupt()` is in the Qt layer — the
    /// difference being that here it refuses instead of asking, and the asking
    /// stays in Qt (§4.1 CTL-11).
    Result execute_gate() const;
};

/// F2 (INS-07 / CTL-13) — the CURRENT frame's tag.
///
/// `Emulator::frame_num()` is post-incremented at `begin_new_frame`, so during
/// frame K it reads K+1. Every backend answer about "which frame is this" is
/// `frame_num() - 1`: the number the rewind slot for this frame carries, and the
/// number `--delayed-keypress-frames N` lands on. Reading the raw counter makes
/// `on frame N` fire one frame early.
///
/// Before the first `run_frame()` the counter is 0 and there is no previous
/// frame, so the tag is 0 as well — frame 0 is the first frame, as §4.2 INS-07
/// says, and an unsigned wrap to 0xFFFFFFFF is not an answer.
inline uint32_t frame_tag(const Emulator& emu) {
    const uint32_t raw = emu.frame_num();
    return raw > 0 ? raw - 1 : 0;
}

}  // namespace dbg
}  // namespace jnext
