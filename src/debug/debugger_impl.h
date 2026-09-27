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

#include <vector>

#include "core/emulator.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "debug/event_table.h"
#include "debug/resume_guard.h"
#include "debug/symbol_table.h"

namespace jnext {
namespace dbg {

struct Debugger::Impl {
    Impl(Emulator& e, Debugger& owner) : emu(e), self(&owner) {}

    Emulator& emu;

    /// The facade that owns this `Impl`. A `Condition` takes a
    /// `const Debugger&` and a `Handler` a `Debugger&` (§4.2a makes mutation
    /// from a handler a first-class capability), so the drain has to be able to
    /// hand the subscriber the facade — and the drain lives here.
    Debugger* self = nullptr;

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
        // GH #276 B2 — the event-stop latch describes the stop the machine is
        // LEAVING, and every control verb calls this. Clearing it here rather
        // than at each verb is the same argument `DebugState::unpause_()` makes
        // for the step-off arm: one place, so a verb added later cannot forget.
        event_stop_latched = false;
        event_stop         = PauseReason{};
        events.clear_hits();
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

    // ── B2 (§4.3 CAP-EVT) — the event machinery ─────────────────────────────

    /// THE subscription table, the 512-entry latch ring and the INS-17 delivery
    /// history. Held HERE and not on `DebugState` so the closures
    /// (`Condition`, `Handler`) and the `std::vector<Subscription>` stay inside
    /// `jnext_debug`; `DebugState` holds only a POINTER to it, which is what the
    /// eight `Mmu` sites reach through.
    EventTable events;

    // §4.3 `Paused{matched[]}` lives on `events` (`EventTable::hits()`), NOT
    // here: it is delivery state, and a member of this PRIVATE nested struct is
    // observable by nothing, which is how B2's "a nested raise_host_event()
    // clears the outer boundary's list" defect reached review with 744 green
    // rows. B3 reads `events.hits()` for its `Paused` push.

    /// CTL-13 — the `pause_reason` a subscription's `Stop` produced. The FIRST
    /// Stop of the boundary wins; `events.hits()` carries the rest.
    PauseReason event_stop;
    bool        event_stop_latched = false;
    /// `DebugState::resume_generation()` at the moment the latch was written.
    /// `state()` ignores a latch whose generation has moved — see there.
    uint64_t    event_stop_gen = 0;

    /// Re-entrancy guard for the drain: a `Handler` may `subscribe()`,
    /// `unsubscribe()` (its own id included) or `raise_host_event()`, and the
    /// drain is walking the table when it does.
    bool draining    = false;
    bool gates_dirty = false;

    /// Publish the `EventTable`'s cached state into every hot-path gate: the
    /// MMU slot masks, the port flag, the `Execute`/`Cycle` arms and the two
    /// per-engine Copper/DMA flags. THE ONE FUNCTION EVERY TABLE CHANGE ENDS
    /// WITH — a change that forgets it leaves a subscription that can never fire.
    void gates_changed();

    /// The boundary drain and the pre-instruction `Execute` gate — the two
    /// things the hot loop calls, through the `std::function`s `DebugState`
    /// holds. Both return true iff a `Stop` fired.
    bool drain_boundary();
    bool execute_gate(uint16_t pc);

    /// Turn one latch-ring entry into the `Event` §4.3 specifies.
    Event build_event(const LatchEntry& le) const;

    /// Match one event against every live subscription, run the accepted ones'
    /// conditions and handlers, and collect the verdicts. `stop` is OR-ed, never
    /// assigned, because one boundary may carry several events.
    void deliver_to_subscribers(Event& ev, bool& stop, ClientId raiser);

    /// What a `Stop` does besides pausing: drop the transient subscriptions
    /// (§4.3, "auto-removed at the next stop").
    void apply_stop();

    /// Latch CTL-13's reason for this stop.
    void note_event_stop(const Event& ev);

    /// The SES-06 line an `Action::Log` emits.
    std::string log_line_for(const Event& ev) const;

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
