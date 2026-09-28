// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — SES-07's loop driver and the CTL-12 / CTL-15 COLD-BOOT
// RECONSTRUCT CONTRACT.
//
// Work package B3 of epic #276. §4.1 CAP-CTL-12 states the contract in six
// numbered rules; this file is those rules, and each one is named at the line
// that implements it.
//
// ── WHY THE BACKEND HAS TO DO ANYTHING AT ALL ───────────────────────────────
//
// `emulator_frontend_cold_boot()` (`src/platform/emulator_boot.h`) destroys the
// `Emulator` and placement-news a new one AT THE SAME ADDRESS, then re-runs
// `init()`. `&emu` therefore stays valid — which is what lets a `Debugger` live
// across it at all — but every SUB-OBJECT is new. In particular the
// `DebugState` is brand new, which means:
//
//   * `events_ == nullptr`           every subscription still exists and lists
//                                    as live, and NOT ONE CAN EVER FIRE.
//   * no drain / execute-gate hooks  the boundary drain is unreachable.
//   * no machine-replaced hook       `init()`'s own
//                                    `debug_after_machine_transition_()` cannot
//                                    reach `Impl`, so the backend keeps
//                                    reporting the `PauseReason` of a machine
//                                    that has been destructed.
//   * `clients_attached_ == false`   the machine is unarmed although clients are
//                                    attached.
//
// None of that is an error the frontend could detect; it is silent. So the ONE
// function below re-establishes all of it, and every route that can land a new
// machine goes through it.
//
// ── THE PAIR: THE CONSTRUCTOR, THE DESTRUCTOR, AND THIS ─────────────────────
//
// `Debugger::Debugger()` publishes THREE things into `DebugState`
// (`set_event_table`, `set_event_hooks`, `set_machine_replaced_hook`), seeds the
// eight live pages and calls `gates_changed()`. `Debugger::~Debugger()` retires
// exactly those three. `reapply_after_machine_rebuild()` re-publishes exactly
// those three. The FOURTH hook on `DebugState`, the latch stamper, belongs to
// the `Emulator` — `init()` installs it, capturing the `Emulator` — so none of
// the three touches it (B2's destructor did retire it, and every later
// `Debugger` on the machine then got unstamped events; row LIFE-06).
//
// THE INVARIANT ACROSS THE THREE is what the suite pins, not each one
// separately: after a cold boot with a `Debugger` alive, a subscription that
// could fire before can fire again — and after `~Debugger()`, nothing the
// machine does can reach the freed `Impl`. A row per side passes while the trio
// disagrees; B2's review reproduced a real segfault from the dtor's half alone
// and found no row that saw it.
//
// ── THE SINGLE-OWNER RULE, SPLIT (manager decision, B3 milestone 2) ────────
//
// §4.1 CTL-12 says the platform-side `BreakpointSet` / `active()` save and
// restore in `emulator_cold_boot()` is "a second owner of the same state" once
// the backend re-applies subscriptions. Measured, only ONE part of what it
// carries is backend state: the event-mask half (`ev_mask_*`) of the hot-path
// gate. B3 retired THAT half — `emulator_cold_boot()` zeroes it on its copy and
// `gates_changed()` below is its single owner. The rest is the Qt panels'
// breakpoint model, the observers that travel on its copy, and `active_` (the
// Qt window's and the magic hook's bit); before package Q the restore is their
// ONLY owner, so retiring it now would lose a user's breakpoints on every hard
// reset, unsubscribe two panels and leave an open window unarmed. That half is
// retired by package Q when the panels become clients (§10.1 Q WP2/WP6) —
// recorded in `backend.md` CAP-CTL-12 and in `qt-frontend.md`.
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// The one re-application
// ---------------------------------------------------------------------------

Debugger::Impl::PreBoot Debugger::Impl::capture_pre_boot() const {
    const RunState st = self->state();
    PreBoot pre;
    pre.paused = st.paused;
    // The owner only of a pause. `pause_reason.by` of a running machine is
    // CLIENT_NONE anyway; saying so here keeps the struct's meaning local.
    pre.owner  = st.paused ? st.pause_reason.by : CLIENT_NONE;
    return pre;
}

void Debugger::Impl::reapply_after_machine_rebuild(const PreBoot& pre) {
    // (1) THE THREE PUBLICATIONS. The same calls the constructor makes, in the
    //     same order, and the order matters for the same reason it does there:
    //     the table pointer before the gates, because `refresh_event_gates()`
    //     takes its `!events_` branch without it.
    //
    //     The latch stamper is NOT re-installed here, because it is not the
    //     backend's: `Emulator::init()` installs it
    //     (`install_debug_latch_stamper_()`) on the new `DebugState`, and
    //     `init()` has already run by the time the driver returns.
    ds().set_event_table(&events);
    ds().set_event_hooks([this]() { return drain_boundary(); },
                         [this](uint16_t pc) { return execute_gate(pc); });
    ds().set_machine_replaced_hook(
        [this]() { arm(PauseReason::Kind::None, CLIENT_NONE); });

    // (1b) THE EIGHT-PAGE SEED. `DebugState::on_slot_remapped()` early-returns
    //      while the table is null, so every `rebuild_ptr()` during the new
    //      `init()` was discarded — exactly the defect B2 shipped and fixed in
    //      the constructor. A page-qualified `Mem` filter would otherwise be
    //      wrong in BOTH directions after every cold boot.
    for (int s = 0; s < 8; ++s)
        ds().on_slot_remapped(s, emu.mmu().get_effective_page(s));

    // (1c) PUBLISH THE GATES — the ONLY writer of the `ev_mask_rd_` /
    //      `ev_mask_wr_` / `ev_port_` bytes across a cold boot. `emulator_boot.h`
    //      used to carry them over on its `BreakpointSet` copy, as last published
    //      against the DESTROYED machine; it now zeroes them on the copy (its
    //      item 2), so the rebuilt machine's event gate is closed until this line
    //      re-opens it from the live table. That was the "second owner" of
    //      backend state the design names, and it is why skipping this call
    //      passed every row until it was retired.
    gates_changed();

    // (2) THE RING. Every latch entry in it describes a machine that no longer
    //     exists — `EventTable` lives here, not on `Emulator`, so the ring
    //     SURVIVES the reconstruct while its contents do not mean anything any
    //     more. `Emulator::load_state()` passes `discard_ring=true` for exactly
    //     this reason; a cold boot never reaches that path because the hook was
    //     null when `init()` called it.
    //
    //     BEFORE the `Reset` latch below, or the event that reports this very
    //     transition would be discarded with the stale ones.
    events.clear_ring();
    ds().clear_ring_flag();

    // (3) RECONCILE THE BACKEND'S OWN STOP EVIDENCE — what the machine-replaced
    //     hook would have done had it been installed when `init()` fired it.
    //     `init()` calls `debug_after_machine_transition_()` at its tail, and at
    //     that moment the hook was null (the `DebugState` was seconds old), so
    //     this boot's transition reached `DebugState` and not `Impl`. Doing it
    //     explicitly, once, here is what `emulator_boot.h`'s note asks for.
    //
    //     `Kind::None` and not `Kind::User`: `state()`'s precedence switch falls
    //     THROUGH `None` to the legacy PC-breakpoint check, and matches `User`
    //     immediately. A reconstruct that landed on an address carrying a
    //     breakpoint would report `User` and silently swallow it — which is
    //     exactly right for an UNOWNED pause (below), and why (4) re-arms `User`
    //     only for an owned one.
    arm(PauseReason::Kind::None, CLIENT_NONE);

    // (4) RULE 3 — "paused stays paused, running stays running". A fresh
    //     `Emulator` is always running, so only the paused direction needs an
    //     action; and there is no direction in which this PAUSES a machine that
    //     was running, which is the other half of rule 3 ("a client's
    //     `reset(Hard)` never pauses a running machine — there is no `Reset` in
    //     `pause_reason`").
    //
    //     AND THE PAUSE KEEPS ITS OWNER. A pause some client owned (its
    //     `pause()`, its step, its subscription's `Stop`) comes back as
    //     `User{that client}`: the machine is being HELD for it across the boot,
    //     and SES-01's detach — which releases only a pause that is the detaching
    //     client's — must still be able to release it. Re-applied as the bare
    //     `None` above it would read as the unowned `User{CLIENT_NONE}` fallback,
    //     which no detach ever releases: a remote that hard-reset a paused
    //     machine and then crashed would leave it hung. The ORIGINAL kind is not
    //     kept, and cannot be: `Step` or `Breakpoint{id}` described a PC in a
    //     machine that no longer exists. An UNOWNED pause (Magic, a legacy PC
    //     breakpoint, a pause the loop owner made) stays unowned — `None`, whose
    //     fall-through reports a breakpoint at the landing address if one is
    //     there.
    if (pre.paused) {
        ds().pause();
        if (pre.owner != CLIENT_NONE) arm(PauseReason::Kind::User, pre.owner);
    }

    // (5) RULE 2 — re-apply what every client asked for. Subscriptions, the
    //     switches and the symbol table live on `Impl` and never went anywhere.
    //     What the MACHINE held and the reconstruct reset is two kinds of
    //     thing: the ENABLE FLAGS a client set through a verb (call-stack
    //     tracking, the trace, `persistent_breakpoints` — rule 2 names the first
    //     two, CTL-15 "enable flags"), re-applied from the backend's own record
    //     of the request, and only where a client made one (`Impl::want_*`); and
    //     the per-client arm bit and live-raster OR, which `clients_changed()`
    //     re-derives from the list.
    if (want_call_stack) emu.call_stack().set_enabled(*want_call_stack);
    if (want_trace)      emu.trace_log().set_enabled(*want_trace);
    if (want_persistent) ds().set_persistent_breakpoints(*want_persistent);
    //
    //     ORDER RELATIVE TO THE PAUSE IS NOT LOAD-BEARING, and an earlier draft
    //     of this comment claimed it was ("a window in which the machine is
    //     paused and unarmed"). There is no such window: nothing executes
    //     between the two statements, and no mutation can tell the two orders
    //     apart. Saying so is the point — a justification that cannot be
    //     demonstrated is the defect class this branch has been rejected for six
    //     times.
    clients_changed();
}

// ---------------------------------------------------------------------------
// SES-07 — registration
// ---------------------------------------------------------------------------

Result Debugger::set_loop_driver(const LoopDriver& d) {
    impl_->driver = d;
    // A `LoopDriver{}` with both closures empty is how a loop owner CLEARS the
    // registration (at shutdown, or a test restoring the bare-harness case), so
    // this is not refused. The refusal lives where the driver is USED —
    // `reset(Hard)` and `load()` return `RefusedUnavailable` with no closure for
    // them, which is §4.1's "with no driver registered (a bare test harness) →
    // `RefusedUnavailable`" and is testable per verb rather than per
    // registration. The two closures are independent: a loop owner may register
    // one and not the other, and each verb asks only about its own.
    return Result::Ok;
}

// RULE 5 — the GUEST-initiated path. NR 0x02 raises a flag, every loop owner
// polls it and turns it into its own cold boot without the backend being
// involved in the boot itself; these two verbs are how it tells the backend, so
// rules 2-4 apply identically: `on_cold_boot_begin()` immediately BEFORE the
// machine is destroyed, `on_cold_boot_done()` after it is rebuilt.
//
// NEITHER REQUIRES A REGISTERED DRIVER, and that is deliberate. Rule 6's
// `RefusedUnavailable` is about `reset(Hard)`, which needs the driver to PERFORM
// the boot; these are NOTIFICATIONS about a boot the loop owner performs, and
// nothing they do needs `LoopDriver`. Requiring one would force a loop owner that
// only ever sees guest resets to register a closure it never calls —
// `HeadlessApp`'s `JNEXT_BENCH_WATCH` fixture is exactly that case, and it is the
// one real caller in the tree today. (An earlier draft did refuse without a
// driver, on the reasoning that such a call "is a wiring error"; the fixture
// disproves it.) Nor does either refuse on a corrupt machine: the guest has
// already decided to reboot it, and the notification changes nothing about that.

// RULE 3 ON THIS PATH — owner decision 2026-09-28, the ONE change to the frozen
// header B3 makes. `on_cold_boot_done()` runs after the machine is gone, so it
// cannot read the pause that was in force, and a pause that landed in the same
// tick as the guest's NR 0x02 write (a breakpoint later in the frame that raised
// the reset) used to come back RUNNING. The loop owner now calls this first, and
// the backend captures exactly what `reset(Hard)` captures before its driver —
// `capture_pre_boot()`, the pause and its OWNER — so the three routes share one
// capture and one re-application (rows CTL-12-41..47).
Result Debugger::on_cold_boot_begin() {
    // Last wins: a second `begin` with no `done` between them is a loop owner
    // that announced a boot, did not perform it, and is announcing another —
    // the machine the SECOND one captures is the one about to be destroyed.
    impl_->pending_boot = impl_->capture_pre_boot();
    return Result::Ok;
}

Result Debugger::on_cold_boot_done() {
    // WITH a `begin`: re-apply what it captured from the machine that was
    // destroyed, and consume it so it cannot leak into a later, unrelated boot.
    //
    // WITHOUT one — a loop owner not yet updated to call `begin`, which is the
    // behaviour B3 shipped before the header change and is kept rather than
    // guessed at: the only pause state left is the REBUILT machine's, so that
    // is what is re-applied — running for a guest reset, paused if the loop
    // owner paused the new machine before calling this — and it is re-applied
    // UNOWNED, because a pause the loop owner made is no client's.
    Impl::PreBoot pre;
    if (impl_->pending_boot) {
        pre = *impl_->pending_boot;
    } else {
        pre.paused = impl_->ds().paused();
        pre.owner  = CLIENT_NONE;
    }
    impl_->pending_boot.reset();
    impl_->reapply_after_machine_rebuild(pre);

    // RULE 5 defers to rules 2-4, and rule 4 is the `Reset{Hard}` event and
    // push. The LATCH (the §4.3 `Reset` event a subscription can see) must come
    // after the re-application or it is dropped on a null table —
    // `emulator_boot.h` item 3 says so at the site.
    impl_->emu.debug_latch_reset(/*hard=*/true);
    impl_->self->log(CLIENT_NONE, LogLevel::Info,
                     "RESET hard (guest-initiated cold boot, reconstruct re-applied)");
    impl_->notify_reset(ResetKind::Hard);
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// CTL-12 — `reset(Hard)`
//
// `Soft` stays in `debugger_control.cpp` with the rest of CAP-CTL: it is one
// `Emulator::soft_reset()` call and needs nothing from this file. `Hard` is
// here because it is the reconstruct.
// ---------------------------------------------------------------------------

Result Debugger::Impl::reset_hard(ClientId by) {
    // RULE 6 — no driver, `RefusedUnavailable`. BENIGN: the capability exists,
    // the sequence it needs has not been registered. `Unsupported` would say
    // this build cannot do it at all.
    if (!driver.cold_boot) return Result::RefusedUnavailable;

    // The corruption gate, as for every other verb that makes the machine
    // execute. A cold boot REPLACES the corrupt machine rather than running it,
    // so one could argue it should be exempt — but CTL-11's rule is that an
    // unacknowledged incident refuses the resume, and a cold boot leaves the
    // machine RUNNING, which is a resume by any other name. A client that wants
    // out of a corrupt machine acknowledges the incident first; that is what the
    // acknowledgement is for.
    const Result gate = execute_gate();
    if (gate != Result::Ok) return gate;

    // RULE 3's input, read BEFORE the boot — the pause and its owner. Nothing
    // survives the boot. A pending `on_cold_boot_begin()` capture describes the
    // machine this verb is about to replace, not the one it will land, so it is
    // stale from here on and dropped.
    const PreBoot pre = capture_pre_boot();
    pending_boot.reset();

    // RULE 1 — run the loop owner's sequence SYNCHRONOUSLY. Called from inside
    // `pump()` in practice (the post-frames slot the flag poll lives in), so
    // later commands in the same drain see the new machine: a ZRCP
    // `hard-reset-cpu` → `enter-cpu-step` → `smartload` chain works.
    //
    // NOT deferred through `Emulator::request_hard_reset()`. That would make the
    // verb return before the machine changed, and rule 1 exists because a client
    // that asked for a reset and then asked for a register has to get the new
    // machine's register.
    const bool booted = driver.cold_boot();

    // RULE 2 — re-apply WHETHER OR NOT the boot reported success. A failed cold
    // boot still ran `~Emulator()` and the placement-new (that is the first thing
    // `emulator_cold_boot()` does), so the `DebugState` is new either way and
    // leaving the publications un-restored would silently disconnect every
    // subscription. Re-applying and then reporting the failure is the only order
    // in which the refusal is honest.
    reapply_after_machine_rebuild(pre);

    // The §4.3 `Reset{Hard}` EVENT, after the re-application (see
    // `on_cold_boot_done`). It is latched, not delivered here: §4.3 delivers
    // every event at an instruction boundary with the machine stopped, and this
    // is not one.
    emu.debug_latch_reset(/*hard=*/true);
    log_mutate_range(by, "machine", booted ? "hard reset (cold boot)"
                                           : "hard reset FAILED (cold boot)");

    // RULE 4 — `Reset{Hard}` reaches every listener BEFORE the verb returns. An
    // adapter whose client is blocked in a `run` completes that reply from here;
    // that is adapter policy and not a pause, so no other client sees a stop.
    //
    // Pushed even when the boot failed: a client blocked on `run` is blocked
    // either way, and a notification it never receives is a hang.
    notify_reset(ResetKind::Hard);

    return booted ? Result::Ok : Result::RefusedUnavailable;
}

// ---------------------------------------------------------------------------
// CTL-15 — `load(path)`
// ---------------------------------------------------------------------------

Result Debugger::load(ClientId by, const std::string& path) {
    if (!impl_->driver.load) return Result::RefusedUnavailable;

    const Result gate = impl_->execute_gate();
    if (gate != Result::Ok) return gate;

    // "A paused caller stays paused, at the new PC" (CTL-15). The same capture
    // as CTL-12's rule 3, read before the load for the same reason, and a
    // pending `on_cold_boot_begin()` capture is stale after it for the same
    // reason `reset(Hard)` drops one.
    const Impl::PreBoot pre = impl_->capture_pre_boot();
    impl_->pending_boot.reset();

    const bool loaded = impl_->driver.load(path);

    // DID THE DRIVER RECONSTRUCT THE MACHINE? The backend can tell, from its OWN
    // publication rather than from a guess about the closure: the constructor
    // pointed `DebugState::events_` at `Impl::events`, and nothing but
    // `~Debugger()` or a brand-new `DebugState` — i.e. `~Emulator()` +
    // placement-new — can make it point anywhere else. An in-place load
    // (`emulator_apply_load()`, `load_rzx`'s re-`init()`) leaves the same
    // `DebugState`, and `init()` does not touch the pointer. Read BEFORE the
    // re-application, which restores it.
    //
    // (It would misread only with a SECOND `Debugger` alive on the same machine,
    // whose constructor had repointed it. One `Debugger` per `Emulator` is the
    // design — §4 "the one type a debugger frontend holds".)
    const bool reconstructed = impl_->ds().event_table() != &impl_->events;

    // RE-APPLIED UNCONDITIONALLY, reconstruct or not. It is idempotent on an
    // in-place load — the same three publications, the same eight pages, the
    // same gates — and it is also the reconciliation every machine landing gets
    // anyway: it arms `Kind::None` and clears the stop evidence, which is what
    // `Emulator::debug_after_machine_transition_()` does for a `load_state()`.
    // Knowing whether the machine was reconstructed matters only for the RESET,
    // below; the re-application never needed the answer.
    impl_->reapply_after_machine_rebuild(pre);

    // CTL-12 ends "the same contract covers CTL-15 when a `.nex` load routes to
    // the cold boot", and rule 4 is part of that contract: after a reconstruct
    // every other client's cache of the machine is invalid and nothing else tells
    // it so. So a load that RECONSTRUCTED latches the §4.3 `Reset{Hard}` event
    // (after the re-application, for the reason `reset_hard()` gives) and pushes
    // `Reset{Hard}` to every listener before returning — exactly what
    // `reset(Hard)` does. A load that did NOT reconstruct pushes nothing: the
    // machine was not reset, and saying it was would be false to every other
    // client (rows CTL-15-13/14).
    if (reconstructed) impl_->emu.debug_latch_reset(/*hard=*/true);

    impl_->log_mutate_range(by, "machine",
                            loaded ? ("loaded \"" + path + "\"" +
                                      (reconstructed ? " (cold boot)" : ""))
                                   : ("load FAILED \"" + path + "\""));

    if (reconstructed) impl_->notify_reset(ResetKind::Hard);
    return loaded ? Result::Ok : Result::RefusedUnavailable;
}

}  // namespace dbg
}  // namespace jnext
