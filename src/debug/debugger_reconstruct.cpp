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
// ── WHAT THIS DELIBERATELY DOES NOT DO ─────────────────────────────────────
//
// It does not retire the platform-side `BreakpointSet` / `active()` save and
// restore at `emulator_boot.h:133-146`. §4.1 CTL-12 says B3 retires it as "a
// second owner of the same state"; measured against the tree, it is not the same
// state, and the reasoning is recorded at that site and in the B3 report. In
// short: the legacy `BreakpointSet` holds the QT PANELS' breakpoint model (and
// their observers, which travel on its copy), and `active_` is set by the Qt
// debugger window and by the magic-breakpoint hook. None of those three is
// backend-owned until package Q moves the Qt frontend onto a `Debugger`, so
// retiring the restore now loses a user's breakpoints on every hard reset,
// permanently unsubscribes two panels, and leaves an open debugger window
// unarmed — three functional regressions in the Qt GUI, which settled owner
// decision 8 forbids. What B3 DOES fix is the genuine double-write: the stale
// `ev_mask_*` bytes that ride along on the restored copy, which
// `gates_changed()` below overwrites from the live subscription table.
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// The one re-application
// ---------------------------------------------------------------------------

void Debugger::Impl::reapply_after_machine_rebuild(bool was_paused) {
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
    //     breakpoint would report `User` and silently swallow it.
    arm(PauseReason::Kind::None, CLIENT_NONE);

    // (4) RULE 3 — "paused stays paused, running stays running". A fresh
    //     `Emulator` is always running, so only the paused direction needs an
    //     action; and there is no direction in which this PAUSES a machine that
    //     was running, which is the other half of rule 3 ("a client's
    //     `reset(Hard)` never pauses a running machine — there is no `Reset` in
    //     `pause_reason`").
    if (was_paused) ds().pause();

    // (5) RULE 2 — re-apply what every client asked for. The client-owned model
    //     makes this mechanical: subscriptions, switches and the symbol table
    //     live on `Impl` and never went anywhere, and the only per-client state
    //     the MACHINE holds is the arm bit and the live-raster OR, which
    //     `clients_changed()` re-derives from the list.
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
// polls it BEFORE `pump()` and turns it into its own cold boot without the
// backend being involved at all; this is how it tells the backend afterwards, so
// rules 2-4 apply identically.
//
// IT DOES NOT REQUIRE A REGISTERED DRIVER, and that is deliberate. Rule 6's
// `RefusedUnavailable` is about `reset(Hard)`, which needs the driver to PERFORM
// the boot; this verb is a NOTIFICATION that a boot has already happened, and the
// re-application it triggers needs nothing from `LoopDriver` at all. Requiring one
// would force a loop owner that only ever sees guest resets to register a closure
// it never calls — `HeadlessApp`'s `JNEXT_BENCH_WATCH` fixture is exactly that
// case, and it is the one real caller in the tree today. (An earlier draft of this
// function did refuse without a driver, on the reasoning that such a call "is a
// wiring error"; the fixture disproves it.)
Result Debugger::on_cold_boot_done() {
    // The loop owner has ALREADY rebuilt the machine, so the pause state to
    // re-apply is the one the machine is in now — which for a guest reset is
    // "running". Read rather than assumed: a loop owner that paused the machine
    // before calling this (a debugger window open across a guest reset) must not
    // have it resumed by the notification.
    const bool was_paused = impl_->ds().paused();
    impl_->reapply_after_machine_rebuild(was_paused);

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

    // RULE 3's input, read BEFORE the boot. Nothing survives it.
    const bool was_paused = ds().paused();

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
    reapply_after_machine_rebuild(was_paused);

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

    // "A paused caller stays paused, at the new PC" (CTL-15). Same input as
    // CTL-12's rule 3, read before the load for the same reason.
    const bool was_paused = impl_->ds().paused();

    const bool loaded = impl_->driver.load(path);

    // RE-APPLIED UNCONDITIONALLY, and that is the point rather than laziness.
    // CTL-15 says "a load that reconstructs the machine honours the same contract
    // as CTL-12 `Hard`" — and the backend CANNOT KNOW whether it did: the closure
    // is the loop owner's, `emulator_apply_load()` loads in place, the Qt menu
    // route cold-boots first, and `load_rzx` replaces the machine (re-`init()`s
    // it in place; it never reconstructs) only when the recording carries an
    // embedded snapshot. Asking the question would mean guessing at
    // it. Re-applying always is idempotent — the same three publications, the same
    // eight pages, the same gates — and removes the question entirely.
    //
    // It is not free of consequence: it arms `Kind::None` and clears the stop
    // evidence, which is what EVERY machine landing does already
    // (`Emulator::debug_after_machine_transition_()`), so a load that did not
    // replace the machine gets the same reconciliation a `load_state()` gets.
    impl_->reapply_after_machine_rebuild(was_paused);

    impl_->log_mutate_range(by, "machine",
                            loaded ? ("loaded \"" + path + "\"")
                                   : ("load FAILED \"" + path + "\""));

    // NO `Reset` PUSH. `ResetKind` has two values and neither means "a program
    // was loaded"; a client that asked for a load knows what it asked for, and
    // manufacturing a `Reset{Hard}` would tell every OTHER client the machine had
    // been reset, which it may not have been. Flagged in the B3 report as the one
    // place CTL-15's "the same contract as CTL-12 `Hard`" is ambiguous and this
    // is the reading taken.
    return loaded ? Result::Ok : Result::RefusedUnavailable;
}

}  // namespace dbg
}  // namespace jnext
