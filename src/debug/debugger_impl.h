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
#include <optional>
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
    /// (§4.3, "auto-removed at the next stop"), and — for a stop an `Action::Stop`
    /// caused — apply SES-04's stop policy.
    ///
    /// `from_event` IS THE WHOLE DIFFERENCE, and it is a parameter rather than a
    /// second function because both arms must stay visibly next to each other.
    /// SES-04 is about "what a `Stop` ACTION does in this frontend": a
    /// subscription's `Stop` under `StopPolicy::ExitNonZero` is a logged event
    /// plus a non-zero exit request, whereas an explicit `Debugger::pause()` —
    /// which also passes through here, and is also a stop — is a client asking
    /// for the machine to hold still and must NEVER request an exit. A single
    /// unparameterised `apply_stop()` would give one of the two the other's
    /// behaviour, which is exactly the shape of B2's three sibling-divergence
    /// findings.
    void apply_stop(bool from_event);

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

    // ── B3 (§4.8 CAP-SES) — the session ─────────────────────────────────────

    /// One attached client. SES-01's `ClientInfo` plus everything the backend
    /// keeps PER CLIENT so a CTL-12 cold boot cannot lose it: the listener, the
    /// SES-05 live-raster request, and (B4) its bookmarks.
    ///
    /// A `std::vector` with STABLE IDS AND TOMBSTONES, the same shape
    /// `EventTable` uses and for the same reason: `detach()` may be called from
    /// a listener callback that the fan-out loop is walking, and an erase there
    /// invalidates the iterator. `compact_clients()` at the outer boundary does
    /// the removal, exactly as `EventTable::compact()` does.
    /// CAP-03 (B4) — one named, in-memory snapshot. Tagged with what a
    /// restore must match: the machine TYPE and the snapshot WIDTH at save
    /// time (§4.5: a mismatch is refused BEFORE `load_state` runs, never left
    /// to the sentinel check, which would latch corruption).
    struct Bookmark {
        std::string          name;
        std::vector<uint8_t> bytes;    ///< `serialise_machine()` output; its size IS the width
        MachineType          type = MachineType::ZX48K;
    };

    /// §4.5 CAP-03 — "bound: 8 per client (RefusedUnavailable beyond it,
    /// oldest never evicted silently)".
    static constexpr size_t kMaxBookmarks = 8;

    struct Client {
        ClientId   id = CLIENT_NONE;
        ClientInfo info;
        Listener*  listener    = nullptr;
        bool       live_raster = false;
        bool       detached    = false;   ///< tombstone
        /// CAP-03 — this client's bookmarks, in save order (a re-save of a name
        /// keeps its place). HERE, on the client row, because §4.5 makes them
        /// per client and they die with its detach; and on `Impl`, not the
        /// `Emulator`, which is what makes them survive a CTL-12 `Hard`
        /// reconstruct with nothing to re-apply. `detach()` frees them at once
        /// rather than when the tombstone is compacted.
        std::vector<Bookmark> bookmarks;
    };

    std::vector<Client> clients;
    ClientId            next_client_id = 1;

    /// Live client lookup. Null for an unknown or detached id — which is what
    /// makes `RefusedUnavailable` (benign, "no such client") the answer rather
    /// than a crash.
    Client*       find_client(ClientId cid);
    const Client* find_client(ClientId cid) const;

    /// Re-derive `DebugState::clients_attached()` and the ORed `live_raster`
    /// from the client list. THE ONE function every attach / detach / listener /
    /// live-raster change ends with, for the same reason `gates_changed()` is
    /// one function: a change that forgets it leaves the machine armed for a
    /// client that has gone, or unarmed for one that has arrived.
    void clients_changed();

    /// Drop the tombstones. Called only where no fan-out loop is walking the
    /// vector.
    void compact_clients();

    /// Re-entrancy guard for the listener fan-out: a `Listener` may `detach()`
    /// (a crashed remote's server does exactly that), and the fan-out is walking
    /// the vector when it does.
    bool notifying = false;

    // ── SES-02, the fan-out ─────────────────────────────────────────────────
    //
    // ONE function per notification, each walking the live clients. They are the
    // only writers of a `Listener*`, so "does every client get it" is a property
    // of one loop rather than of every call site.

    void notify_paused(const PausedInfo& info);
    void notify_resumed(ClientId by);
    void notify_reset(ResetKind kind);
    void notify_frame_ended(uint32_t frame);
    void notify_subscriptions_changed(EventKindMask kinds);
    void notify_exit_requested(int code);
    void notify_log(LogLevel level, const std::string& text);

    /// SES-02/SES-03 — the ONE place a `Paused` / `Resumed` / `FrameEnded` /
    /// `SubscriptionsChanged` push is decided, from the machine's own state.
    ///
    /// EDGE DETECTION IN ONE OWNER, not a push at each transition site. There
    /// are seven ways out of paused and a dozen into it (`DebugState::pause()`
    /// from the hot loop, from a control verb, from the magic hook, from a
    /// subscription's `Stop`), and a push at each is the "two lists" failure
    /// that B2's nine blocking items were three instances of. Instead this
    /// compares `paused()` + `resume_generation()` + the frame tag + the
    /// subscription revision against what was last pushed, and is called from
    /// `pump()` — the slot §4.8 SES-03 specifies ("a stop in this tick's frames
    /// is notified in this tick's pump").
    ///
    /// `Reset` is NOT here: CTL-12 rule 4 requires it to reach every listener
    /// BEFORE the verb returns, so it is pushed synchronously by the verb.
    void sync_notifications();

    bool     last_paused      = false;
    uint64_t last_resume_gen  = 0;
    uint32_t last_frame       = 0;   ///< the RAW `Emulator::frame_num()`, never the clamped tag
    uint64_t last_subs_rev    = 0;
    bool     notif_primed     = false;

    /// Build the `PausedInfo` for the machine's current stop: `state()`'s
    /// reason, plus §4.3's `matched[]` from `EventTable::hits()`.
    PausedInfo paused_info() const;

    // ── SES-03, the services ────────────────────────────────────────────────

    std::vector<Service*> services;

    /// Does any registered service have a connected peer? `ServiceHint`'s
    /// `remote_attached`, and the SES-04 override's condition.
    bool any_peer_connected() const;

    /// SES-04 — what a `Stop` ACTION does right now, override included.
    ///
    /// SEPARATE FROM `stop_policy()`, which returns what the loop owner SET.
    /// `ExitNonZero` becomes `Pause` while a remote is connected (§4.8 SES-04,
    /// owner decision §1.3 item 11: "a client blocked on `run` must get its stop
    /// reply"), and a setting that reads back as something other than what was
    /// written is a trap for the loop owner that wrote it. So the stored value
    /// round-trips and the override lives here, in the one place the policy is
    /// CONSUMED.
    StopPolicy effective_stop_policy() const;

    /// The exit code an `ExitNonZero` stop requests with no script to name one:
    /// **3** (owner decision, architecture §1.3 item 10 — never 2, which both
    /// harnesses use for a harness fault; 1 stays "jnext could not run"). A
    /// script's explicit `exit <code>` is package S's to supply.
    static constexpr int kStopExitCode = 3;

    // ── SES-07, the loop driver, and the CTL-12 reconstruct ─────────────────

    LoopDriver driver;

    /// CTL-12 rule 2 / CTL-15 — the ENABLE FLAGS a client set through a backend
    /// verb, kept OUTSIDE the `Emulator` so a reconstruct cannot lose them:
    /// call-stack tracking (INS-12), the trace (INS-13) and
    /// `--persistent-breakpoints` (§4.1). Each is the machine's own state
    /// (`Emulator::call_stack()`, `trace_log()`, `DebugState::persistent_`),
    /// which `~Emulator()` + placement-new resets to its default; the backend
    /// re-applies its copy on every route that can land a new machine.
    ///
    /// EMPTY UNTIL A CLIENT SETS IT, and only a set value is re-applied: a
    /// machine nobody configured through the backend comes back with the
    /// `Emulator`'s own defaults (and `persistent_` with what the CONFIG says,
    /// which `init()` latches). The backend re-applies its record of what a
    /// client ASKED FOR; it never reads the dead machine — which is why the guest
    /// path (`on_cold_boot_done()`, after the machine is already gone) can do it
    /// too.
    ///
    /// NOT CAPTURED: the Qt panels still switch call-stack tracking and the trace
    /// directly on the `Emulator` until package Q makes them clients, so those
    /// writes are not client intent and a cold boot resets them exactly as it
    /// always has (`emulator_cold_boot()` restores nothing transient). Coverage
    /// (INS-20) is B4's.
    std::optional<bool> want_call_stack;
    std::optional<bool> want_trace;
    std::optional<bool> want_persistent;

    // ── B4 (§4.2 INS-20) — PC coverage ──────────────────────────────────────
    //
    // THE SET AND ITS SWITCH ARE BOTH BACKEND STATE, not the machine's: the bits
    // live here and the `Emulator` only holds a pointer to them
    // (`DebugState::coverage_sink()`), so a reconstruct loses the pointer and
    // neither the switch nor the bits. `publish_coverage()` is the ONE writer of
    // that pointer from the switch — `coverage_enable()` and the re-application
    // end with it, and `~Debugger()` retires it — so the places it is set can
    // never disagree about what "on" means.
    //
    // Unlike `want_*` above this is a plain `bool`, not an `optional`: coverage
    // exists nowhere but here, so there is no machine default to leave alone —
    // "nobody asked" and "asked for off" are the same state.
    CoverageBits coverage;
    bool         coverage_on = false;
    void publish_coverage() { ds().set_coverage_sink(coverage_on ? &coverage : nullptr); }

    // ── B4 (§4.5 CAP-01) — deferred screenshots ─────────────────────────────
    //
    // "Deferred to the next RENDERED frame, for every frontend, never the stale
    // framebuffer." A capture is queued here and taken by `pump()` — the loop
    // owner's post-frames slot, where the frame it just ran is complete and no
    // command of this pump has touched the machine yet — once the machine has
    // RENDERED a frame after the capture's layer mask was armed
    // (`Emulator::rendered_frames()` moved past `after`).
    //
    // FIFO. The HEAD's `layer_mask` is the one armed on the renderer, and
    // `DebugState::capture_render()` forces the render while anything is queued
    // (`arm_capture_head()`, the ONE writer of both). A capture behind the head
    // with the SAME mask is taken from the same frame; one with a different mask
    // waits for a frame rendered with its own.
    //
    // BACKEND STATE, so it survives a reconstruct; the re-application re-arms
    // the head on the new renderer and re-bases every `after` on the new
    // machine's counter (which restarted at 0).
    struct Capture {
        ClientId         by = CLIENT_NONE;
        std::string      path;
        uint8_t          layer_mask = LAYER_MASK_ALL;
        ScreenshotFormat format     = ScreenshotFormat::Png;
        uint64_t         after      = 0;       ///< taken once rendered_frames() > after
        bool             warned     = false;   ///< the one "deferred while paused" line
    };
    std::vector<Capture> captures;

    /// Publish the queue's head into the machine: the renderer's layer mask
    /// (LAYER_ALL when the queue is empty) and the force-render bit. Called only
    /// when the queue's HEAD changes — never per pump — so a mask a frontend set
    /// for its own reasons is not clobbered on every tick.
    void arm_capture_head();

    /// `pump()`'s capture step: write every capture whose frame has been
    /// rendered, warn once about a capture held up by a paused machine.
    void service_captures();

    /// CTL-12 rule 3's input: the pause IN FORCE before a machine is replaced,
    /// and whose it is. Not a bare bool, because the owner is half of what rule
    /// 3 has to preserve: SES-01's detach releases a pause only if it is THIS
    /// client's, so a re-applied pause that forgot its owner could never be
    /// released by anyone — a crashed DeZog that hard-reset a paused machine
    /// would leave it hung (rows CTL-12-41..47).
    struct PreBoot {
        bool     paused = false;
        ClientId owner  = CLIENT_NONE;   ///< CLIENT_NONE: unowned (Magic, a legacy breakpoint, …)
    };

    /// THE ONE CAPTURE, shared by all three routes that replace the machine:
    /// `reset(Hard)` and `load()` call it before their driver runs, and the
    /// guest path's `on_cold_boot_begin()` calls it before the loop owner
    /// destroys the machine. Reads `state()`, so the owner is whatever
    /// CTL-13's precedence says it is at that instant.
    PreBoot capture_pre_boot() const;

    /// What every route does immediately before the machine it describes goes
    /// away: report that machine's pending edges (`sync_notifications()` —
    /// Paused / Resumed / FrameEnded / SubscriptionsChanged not yet pushed), then
    /// `capture_pre_boot()`. The flush is what lets the re-application re-base
    /// the edge detector on the rebuilt machine without losing a stop, a resume
    /// or a frame the OLD machine produced since the last pump (fix round 1,
    /// SES-02-20/21).
    PreBoot prepare_rebuild();

    /// §5 — "a handler may not drive the machine": a verb that would EXECUTE,
    /// CHANGE THE RUN STATE OF, REWIND, RESTORE, RESET or REPLACE the machine,
    /// called from inside an event delivery → `Unsupported` (logged), else `Ok`.
    /// ONE helper and ONE code for the whole set (fix round 1b; the set, verb by
    /// verb with the reason, is the table in the B3 report and the NEST-* rows).
    ///
    /// A handler runs with the machine stopped at a boundary but with
    /// `Emulator::run_frame()` — or the pre-instruction gate inside it — still
    /// on the stack below it, and the drain walking the latch ring and
    /// accumulating the boundary's `matched[]`. The step verbs and a
    /// `save_state_bytes()` advance would run instructions inside that frame;
    /// the rewind, restore, reset and load verbs replace the state under it and
    /// clear the ring; and the run-state verbs (`pause`, `run`, the `run_to`
    /// family, `step_out`) re-arm the stop evidence — `arm()` clears the latch
    /// and `hits()` the drain is building — which is how a handler would
    /// silently swallow the stop it is part of. A handler stops the machine by
    /// returning `Action::Stop`. The same refusal `pump()` makes, spelled per
    /// verb.
    Result refuse_inside_delivery(const char* verb) const;

    /// ST-01 — is the machine at a frame boundary a save may be taken at? The
    /// ONE predicate `at_frame_boundary()` and every save verb read.
    bool at_boundary() const;

    /// ST-01's frame-boundary rule for the three verbs that save the machine
    /// (`save_state_bytes`, `bookmark_save`, `save_snapshot`): `Ok` at a
    /// boundary; `NotAtFrameBoundary` mid-frame under `RefuseMidFrame`;
    /// `Unsupported` from inside a delivery when an advance would be needed
    /// (§5); otherwise ADVANCE — attributed to `by` in a SES-06 `MUTATE` line —
    /// and `Ok`. `verb` names the caller in the refusal and the log line.
    Result reach_frame_boundary(ClientId by, SaveStateMode mode, const char* verb);

    /// ST-01's serialisation, shared by `save_state_bytes` and `bookmark_save`.
    /// Empty if the measured and written sizes disagree.
    std::vector<uint8_t> serialise_machine();

    /// CTL-02's body without the delivery refusal — see `Debugger::run()`. Its
    /// one other caller is `detach()`'s release of the departing client's pause.
    Result run_verb(ClientId by);

    /// `on_cold_boot_begin()`'s capture, waiting for `on_cold_boot_done()`.
    /// Consumed by `done`; OVERWRITTEN by a second `begin` (last wins); CLEARED
    /// by `reset(Hard)` and `load()`, which land a machine of their own (a
    /// capture that described the machine before THEM is stale); and a detach
    /// of its owner releases the pause it recorded, exactly as SES-01 releases
    /// the live one.
    std::optional<PreBoot> pending_boot;

    /// CTL-12 — re-bind to the machine at `emu`'s address and re-apply
    /// everything the backend owns. Idempotent, and deliberately called whether
    /// or not the machine was actually replaced (see `Debugger::load`).
    ///
    /// `pre` is the pause in force from BEFORE the boot (`capture_pre_boot()`):
    /// CTL-12 rule 3 is "paused stays paused, running stays running", and a
    /// fresh `Emulator` is always running.
    void reapply_after_machine_rebuild(const PreBoot& pre);

    /// CTL-12 `Hard`. On `Impl` rather than as a `Debugger` method because
    /// `Debugger::reset()` owns the `ResetKind` switch (whose missing `default`
    /// is what makes a fourth enumerator a compile error) and dispatches the one
    /// arm that needs the reconstruct here.
    Result reset_hard(ClientId by);
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
