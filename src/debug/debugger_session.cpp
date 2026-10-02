// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — §4.8 CAP-SES, the session.
//
// Work package B3 of epic #276 (doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md
// §10.1): the client table (SES-01), the listener fan-out (SES-02), the service
// list and `pump()`'s drain policy (SES-03), the stop policy and its remote
// override (SES-04), `live_raster` / `attached` (SES-05), the message sink
// (SES-06) and the loop driver plus the cold-boot reconstruct contract
// (SES-07 / CTL-12 / CTL-15, in `debugger_reconstruct.cpp`).
//
// ── THE PAIRS THIS FILE IS MADE OF, AND THE INVARIANT ACROSS EACH ───────────
//
// B2's review found that three of its nine blocking items were two sides of one
// mechanism disagreeing with each other, each side defensible alone. This file
// is almost entirely pairs, so each one names the invariant that has to hold
// ACROSS it, and the suite pins THAT rather than each side:
//
//   attach / detach                 the live client count and
//                                   `DebugState::clients_attached()` agree after
//                                   BOTH — one function, `clients_changed()`,
//                                   is the only writer, and both call it.
//   detach / another client's pause `detach` resumes IFF the pause is this
//                                   client's; an unowned pause (`CLIENT_NONE`)
//                                   is nobody's, so no detach clears it.
//   per-client `live_raster` / the   the OR is recomputed from the list by the
//   ORed value                      same `clients_changed()`, never incremented
//                                   and decremented alongside it.
//   the drain / its budget          the drain stops on `Idle` OR on the budget,
//                                   and `budget_ms == 0` means "one command",
//                                   not "unbounded" — both arms pinned.
//   stored / effective stop policy  `stop_policy()` round-trips what was set;
//                                   `effective_stop_policy()` is the only reader
//                                   the `Stop` path uses.
//   the ctor's publications / the    `reapply_after_machine_rebuild()` re-runs
//   dtor's retirements              exactly what the ctor published and the dtor
//                                   retires exactly the same three — the
//                                   reconstruct file states that one.
//
// ── WALL CLOCK, ONCE, DELIBERATELY ─────────────────────────────────────────
//
// Settled decision 7 is "deterministic time (frames, T-states) is the unit
// throughout; never wall-clock". `pump()`'s budget is the one exception, and it
// is not an exception to the rule so much as outside its subject: §4.8 SES-03
// calls the budgets "HOST SERVICE PARAMETERS, NOT EMULATION SEMANTICS —
// nothing in the emulated timeline depends on any of these". They bound how long
// the loop owner lends its thread to socket I/O. A T-state bound could not
// express that, because the machine does not advance while `pump()` drains.
// `steady_clock`, not `system_clock`: a host clock step must not end a drain.
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

#include <algorithm>
#include <chrono>

#include "core/log.h"

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// SES-01 — the client table
// ---------------------------------------------------------------------------

Debugger::Impl::Client* Debugger::Impl::find_client(ClientId cid) {
    // `CLIENT_NONE` is the BACKEND, never a client, and it is what every
    // internal call passes as `by`.
    //
    // NO MUTATION CAN SEE THIS LINE, and saying so is the point rather than
    // dressing it up: `next_client_id` starts at 1 and only increments, so no
    // row can ever carry id 0 and the loop below would fail to match it anyway.
    // It is a guard on an invariant that holds elsewhere, kept because the
    // invariant is one line away in `attach()` and a future id allocator that
    // broke it would otherwise make `detach(CLIENT_NONE)` succeed against a
    // zero-initialised row. Removing it changes no answer this class gives.
    if (cid == CLIENT_NONE) return nullptr;
    for (auto& c : clients)
        if (c.id == cid && !c.detached) return &c;
    return nullptr;
}

const Debugger::Impl::Client* Debugger::Impl::find_client(ClientId cid) const {
    if (cid == CLIENT_NONE) return nullptr;
    for (const auto& c : clients)
        if (c.id == cid && !c.detached) return &c;
    return nullptr;
}

bool Debugger::Impl::client_gone(ClientId cid) const {
    // ISSUED AND GONE — not merely "not attached". A loop owner files its own
    // captures under `CLIENT_NONE`, which is never issued, and a caller may use
    // an id it never attached (every `flush_captures()` row that does): both
    // keep their records. Only an id `attach()` handed out and `detach()` took
    // back can never be asked about again by its owner.
    return cid != CLIENT_NONE && cid < next_client_id && !find_client(cid);
}

// THE ONE WRITER of the two derived values, and that is the whole point: the
// arm bit and the ORed live-raster flag are both FUNCTIONS OF THE LIST, so they
// are recomputed from it rather than adjusted alongside it. An `attach` that
// incremented a counter and a `detach` that decremented one is the pair that
// drifts; this cannot.
void Debugger::Impl::clients_changed() {
    bool any_raster = false;
    size_t live     = 0;
    for (const auto& c : clients) {
        if (c.detached) continue;
        // REQ-qt-32 — an OBSERVER counts toward no arm bit, and that is the
        // whole of its difference from any other client (GH #278 WP4c). Its
        // live-raster request is still honoured below: that is a render hint,
        // not an arm, and it is asked for explicitly.
        if (!c.info.observer) ++live;
        any_raster = any_raster || c.live_raster;
    }
    // SES-05 — "per client, ORed", published into `DebugState`, where the
    // raster walk and the render hint read it (`raster_live()`), and where
    // `Debugger::live_raster()` reads it back: one copy, not a cache on `Impl`
    // beside the machine's.
    ds().set_live_raster(any_raster);
    // SES-05 / §5 — the `attached` half of `armed()`. Its OWN bit on
    // `DebugState`: see `DebugState::clients_attached()` for why each armed()
    // contributor keeps one.
    ds().set_clients_attached(live > 0);
}

void Debugger::Impl::compact_clients() {
    for (size_t i = clients.size(); i-- > 0;)
        if (clients[i].detached) clients.erase(clients.begin() + static_cast<long>(i));
}

Expected<ClientId> Debugger::attach(const ClientInfo& info) {
    Impl::Client c;
    c.id   = impl_->next_client_id++;
    c.info = info;
    impl_->clients.push_back(c);
    impl_->clients_changed();

    // SES-06 — an attach is a host-initiated lifecycle event, which the level
    // policy puts at `info`: one line per user action.
    impl_->self->log(CLIENT_NONE, LogLevel::Info,
                     "ATTACH client " + std::to_string(c.id) + " \"" + info.name +
                         "\" kind=" + std::to_string(static_cast<unsigned>(info.kind)) +
                         (info.observer ? " observer" : ""));
    return make_ok<ClientId>(c.id);
}

// SES-01, THE ONE RULE, and both halves of it are here rather than split:
// remove what this client owns, and release a pause only if it is THIS
// client's.
Result Debugger::detach(ClientId cid) {
    Impl::Client* c = impl_->find_client(cid);
    // BENIGN: "no such client" is an answer, not a failure — a server that
    // detaches twice on a dropped socket must not be told its build lacks the
    // capability. `Unsupported` would say that.
    if (!c) return Result::RefusedUnavailable;

    // Is the machine paused BY THIS CLIENT? Asked of the state the client is
    // leaving, before anything of it is removed. THE ORDER IS NOT LOAD-BEARING
    // TODAY, and an earlier draft of this comment said it was: a subscription's
    // stop is reported from the event-stop latch, which is a COPY of the
    // `PauseReason` (owner included) held on `Impl`, and `erase_client()` below
    // does not touch it — reading after the erase gives the same answer
    // (mutation M28, equivalent). Row SES-01-17 pins the behaviour, not the
    // order.
    //
    // AN UNOWNED PAUSE IS NEVER RESUMED (§4.8 SES-01, owner decision Revision
    // 6): `Magic` and `Corrupt` carry `by == CLIENT_NONE`, and so does the
    // "stopped and nothing explains it" fallback. `find_client(CLIENT_NONE)`
    // returns null for exactly this reason, but the test below is on the id
    // rather than on a lookup, because the question is "is this stop MINE",
    // and `cid` is never `CLIENT_NONE` here (`find_client` already refused it).
    const RunState st         = impl_->self->state();
    const bool     mine       = st.paused && st.pause_reason.by == cid;

    // WHO INHERITS IT (GH #280 N1, owner decision 2026-10-01: "the pause should
    // belong to the one remaining"). A pause this client owns is RELEASED only
    // when no other client remains; otherwise it passes, and the machine stays
    // paused. The heir is the client the machine was paused by before this one
    // stepped it (`pause_origin`), if that client is still here, and otherwise
    // the EARLIEST-ATTACHED remaining client (the lowest id: ids are handed out
    // in attach order and never reused, and `clients` is in that order).
    //
    // "REMAINING" MEANS AN ARMING CLIENT — the population `attached()` counts.
    // An OBSERVER (REQ-qt-32) arms nothing and the Qt GUI keeps one for its
    // whole life, so counting it would make a detach in a GUI session never
    // release anything, and a crashed DeZog would leave the machine hung —
    // the case SES-01's release exists for. So an observer neither inherits a
    // pause nor stops the last arming client's detach from releasing one.
    const auto heir_of = [&](ClientId leaving) -> ClientId {
        const Impl::Client* origin = impl_->find_client(impl_->pause_origin);
        if (origin && origin->id != leaving && !origin->info.observer) return origin->id;
        for (const auto& o : impl_->clients)
            if (!o.detached && o.id != leaving && !o.info.observer) return o.id;
        return CLIENT_NONE;
    };
    const ClientId heir = mine ? heir_of(cid) : CLIENT_NONE;
    if (heir != CLIENT_NONE) {
        // The owner `state()` reports comes from one of two places — the armed
        // verb (`User`, `Step`, `RunTo` at its target) or the event-stop latch
        // (a subscription's `Stop`) — and only `state()`'s precedence knows
        // which answered. Both are re-attributed: the one that did not answer
        // is not read for this stop, and rewriting it changes nothing.
        impl_->armed_by = heir;
        if (impl_->event_stop_latched) impl_->event_stop.by = heir;
    }

    // THE SAME RULE FOR A PAUSE THAT IS WAITING OUT A GUEST COLD BOOT: an
    // `on_cold_boot_begin()` capture that recorded THIS client's pause would
    // otherwise re-apply it at `on_cold_boot_done()` for a client that is gone,
    // with an owner no detach can ever match again. So it is released here,
    // exactly as the live one is below (row CTL-12-46).
    //
    // N1 applies here as well: while another client remains, the captured pause
    // passes to the same heir instead of being dropped.
    if (impl_->pending_boot && impl_->pending_boot->owner == cid) {
        const ClientId boot_heir = heir_of(cid);
        if (boot_heir == CLIENT_NONE) impl_->pending_boot->paused = false;
        impl_->pending_boot->owner = boot_heir;
    }

    // THE TOMBSTONE IS THE ONE GUARD. The listener pointer is deliberately
    // left as it was: the fan-out skips a detached row on `detached` alone, and
    // a second guard (nulling the pointer here too) made either one removable
    // without any row noticing. Row SES-01-23 is a client detached by ANOTHER
    // client's listener in the middle of a fan-out, which is the one moment a
    // tombstoned row is still walked.
    c->detached = true;
    impl_->events.erase_client(cid);
    // PER-CLIENT STATE DIES WITH THE CLIENT (GH #276 B5, carried from B4's
    // reviews). Client ids are never reused, so anything keyed by one that a
    // detach leaves behind is kept for a client that can never come back — a
    // record that grows with every session. Swept BY HAZARD, not by type: the
    // two containers outside the client row that are keyed by a `ClientId` and
    // were never pruned —
    //   * the per-client event switch (`set_client_enabled(cid, false)` stores
    //     the id in `EventTable::disabled_clients_`; an absent id is enabled,
    //     so dropping it is the whole prune);
    //   * the capture-failure record `flush_captures()` reports and consumes.
    // A capture the client queued still SURVIVES its detach (CAP-01-17 — it is a
    // request about the machine's next frame, not the session): it is taken or
    // fails later and is logged either way, but its outcome is no longer
    // recorded for a client that is gone (`Impl::client_gone`). The client row's
    // own fields (listener, live_raster, bookmarks) go with the compaction below;
    // `erase_client()` above takes its subscriptions; `pending_boot`'s owner is
    // released above. Rows DETACH-01..03.
    impl_->events.set_client_enabled(cid, true);
    {
        auto& fs = impl_->capture_failures;
        fs.erase(std::remove_if(fs.begin(), fs.end(),
                                [cid](const std::pair<ClientId, size_t>& f) {
                                    return f.first == cid;
                                }),
                 fs.end());
    }
    // `erase_client()` tombstoned rows and called `refresh()`; the hot-path
    // gates still carry the retired subscriptions' bits until this publishes.
    impl_->gates_changed();

    impl_->clients_changed();
    // CAP-03 (GH #276 B4) — "a client's bookmarks die with its detach" (§4.5):
    // they live on the client row, so they go when this compaction erases it —
    // at once, or at the end of the fan-out that is walking the rows. A freeing
    // step of their own here was tried and removed: `bookmarks()` and
    // `bookmark_restore()` refuse a detached id either way, so it changed nothing
    // any row can see (mutation K37).
    if (!impl_->notifying) impl_->compact_clients();

    impl_->self->log(CLIENT_NONE, LogLevel::Info,
                     "DETACH client " + std::to_string(cid) +
                         (!mine                 ? ""
                          : heir != CLIENT_NONE ? " (its pause passes to client " +
                                                      std::to_string(heir) + ")"
                                                : " (released its pause)"));

    if (mine && heir == CLIENT_NONE) {
        // Through the facade's own verb, so the corruption gate, the armed
        // reason and the transient rules are the ones every other resume gets.
        // Attributed to the DEPARTING client: it is its pause that is being
        // released, and `CLIENT_NONE` would read as the backend resuming a
        // machine on its own initiative.
        impl_->run_verb(cid);
    }
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// SES-02 — listeners
// ---------------------------------------------------------------------------

Result Debugger::set_listener(ClientId cid, Listener* listener) {
    Impl::Client* c = impl_->find_client(cid);
    if (!c) return Result::RefusedUnavailable;
    c->listener = listener;
    // A listener installed mid-session must not be told about transitions it
    // did not see, and must not MISS the next one. `sync_notifications()`
    // compares against the last PUSHED state, which is session-wide rather than
    // per client, so nothing is re-sent and nothing is lost: the next real
    // transition reaches this listener like any other.
    return Result::Ok;
}

// The seven fan-outs. Each walks the live clients once, under `notifying`, so a
// listener that detaches from inside its own callback tombstones a row the loop
// is walking instead of erasing it.
//
// THE NULL CHECK IS PER CLIENT, not per call: `set_listener(cid, nullptr)` is
// how a client stops receiving pushes without detaching (that is why the
// signature takes a pointer), so a session may have attached clients with no
// listener at all, and must.
#define JNEXT_DBG_FANOUT(body)                                   \
    do {                                                         \
        const bool outer = !notifying;                           \
        notifying        = true;                                 \
        for (size_t i = 0; i < clients.size(); ++i) {             \
            Client& c = clients[i];                              \
            if (c.detached || !c.listener) continue;             \
            Listener& l = *c.listener;                           \
            (void)l;                                             \
            body;                                                \
        }                                                        \
        if (outer) {                                             \
            notifying = false;                                   \
            compact_clients();                                   \
        }                                                        \
    } while (0)

void Debugger::Impl::notify_paused(const PausedInfo& info) {
    JNEXT_DBG_FANOUT(l.on_paused(info));
}
void Debugger::Impl::notify_resumed(ClientId by) {
    JNEXT_DBG_FANOUT(l.on_resumed(by));
}
void Debugger::Impl::notify_reset(ResetKind kind) {
    JNEXT_DBG_FANOUT(l.on_reset(kind));
}
void Debugger::Impl::notify_frame_ended(uint32_t frame) {
    JNEXT_DBG_FANOUT(l.on_frame_ended(frame));
}
void Debugger::Impl::notify_subscriptions_changed(EventKindMask kinds) {
    JNEXT_DBG_FANOUT(l.on_subscriptions_changed(kinds));
}
void Debugger::Impl::notify_exit_requested(int code) {
    JNEXT_DBG_FANOUT(l.on_exit_requested(code));
}
void Debugger::Impl::notify_log(LogLevel level, const std::string& text) {
    JNEXT_DBG_FANOUT(l.on_log(level, text));
}

#undef JNEXT_DBG_FANOUT

PausedInfo Debugger::Impl::paused_info() const {
    const RunState st = self->state();
    PausedInfo     pi;
    pi.by     = st.pause_reason.by;
    pi.reason = st.pause_reason;
    pi.cycle  = st.cycle;
    pi.pc     = st.pc;
    // §4.3 makes `matched[]` part of the `Paused` contract: EVERY subscription
    // that stopped at this boundary, transient ones included. It lives on the
    // `EventTable` (`hits()`), not on `Impl`, and is cleared by the next control
    // verb's `arm()`.
    pi.matched = events.hits();
    return pi;
}

// ---------------------------------------------------------------------------
// SES-02/03 — the ONE edge detector
//
// Called from `pump()` and from nowhere else. See the declaration for why this
// is not a push at each transition site.
// ---------------------------------------------------------------------------

void Debugger::Impl::sync_notifications() {
    const bool     paused = ds().paused();
    const uint64_t gen    = ds().resume_generation();
    // The frame's START CYCLE, not the frame number. Not `frame_tag()`: the tag
    // clamps (`raw > 0 ? raw - 1 : 0`) and reads 0 both before anything has run
    // and after frame 0 has ended, so comparing tags never saw the first frame
    // end — on a fresh session or on a rebuilt machine (fix round 1, SES-02-22).
    // Not the raw counter either (GH #278): a rewind lands on a frame start the
    // ring snapshot has already counted, so the first frame run again after it
    // ends without the counter moving. `current_frame_cycle()` advances exactly
    // when a frame ends and goes back on a rewind. The PAYLOAD is still the tag,
    // the frame that ended.
    const uint64_t frame  = emu.current_frame_cycle();
    const uint64_t rev    = events.revision();

    if (!notif_primed) {
        // FIRST pump of the session: adopt the machine's state as the baseline
        // and push nothing. A `Debugger` constructed against an already-paused
        // machine has not "just paused", and a first pump that claimed so would
        // make every frontend's first tick report a stop that never happened.
        notif_primed    = true;
        last_paused     = paused;
        last_resume_gen = gen;
        last_frame_cycle = frame;
        last_subs_rev   = rev;
        return;
    }

    // A SUBSCRIPTION CHANGE is notified before the transitions: a client that
    // subscribed and then had its subscription stop the machine in the same tick
    // must learn of the subscription first, or its `Paused{matched}` names an
    // event id it has never been told about.
    if (rev != last_subs_rev) {
        last_subs_rev = rev;
        notify_subscriptions_changed(events.live_kinds());
    }

    // PAUSED / RESUMED. `resume_generation()` is what makes a stop-resume-stop
    // inside one tick two pushes rather than none: `paused` is true at both
    // ends, and only the generation shows the machine moved in between.
    if (paused) {
        if (!last_paused || gen != last_resume_gen) notify_paused(paused_info());
    } else if (last_paused) {
        // The resume's owner is the armed verb's client — `run()` arms
        // `Kind::None` with its own `by`, and `detach()`'s release arms it with
        // the departing client's id.
        notify_resumed(armed_by);
    }
    last_paused     = paused;
    last_resume_gen = gen;

    // FRAMEENDED. Forward motion only: `step_back()` and `rewind_to_frame()`
    // move the counter BACKWARD, and a loop from the old tag to the new one
    // would run about four billion times. A rewind re-baselines silently —
    // going back to frame N is not "frame N ended".
    //
    // THE CADENCE: ONE push per pump, carrying the most recent completed frame,
    // even when the tick ran several frames (a 400% fast-forward runs four).
    // Neither §4.8 nor the frozen header states a cadence — `FrameEnded{frame}`
    // is all either says — so this is the backend's choice, confirmed by the
    // manager (B3 milestone 2), and the reason is the division of labour: the
    // listener push is the per-TICK session notification a frontend refreshes
    // on, while per-FRAME precision already exists as `EventKind::Frame`, latched
    // at the site and delivered once per frame to a subscription. N pushes from
    // one pump would also tell a listener it had seen N ticks. Rows SES-02-12/13.
    if (frame != last_frame_cycle) {
        const bool forward = frame > last_frame_cycle;
        last_frame_cycle   = frame;
        if (forward) notify_frame_ended(frame_tag(emu));
    }
}

// ---------------------------------------------------------------------------
// SES-03 — services and `pump()`
// ---------------------------------------------------------------------------

Result Debugger::add_service(Service& service) {
    // Idempotent in the same direction `remove_service` is: registering the same
    // adapter twice would drain it twice per pump and double every
    // `flush_notifications()`.
    for (Service* s : impl_->services)
        if (s == &service) return Result::Ok;
    impl_->services.push_back(&service);
    return Result::Ok;
}

Result Debugger::remove_service(Service& service) {
    for (size_t i = 0; i < impl_->services.size(); ++i) {
        if (impl_->services[i] != &service) continue;
        impl_->services.erase(impl_->services.begin() + static_cast<long>(i));
        return Result::Ok;
    }
    // "Idempotent" (the header): removing one that is not registered is Ok.
    return Result::Ok;
}

bool Debugger::Impl::any_peer_connected() const {
    for (const Service* s : services)
        if (s->peer_connected()) return true;
    return false;
}

ServiceHint Debugger::pump(const PumpBudget& budget) {
    ServiceHint hint;

    // §5 — "`pump` is not called from inside `run_frame`; the backend asserts
    // `!in_delivery_` in `pump`". `Impl::draining` IS that flag: it is true for
    // exactly the span of a boundary delivery.
    //
    // NOT a bare `assert`: it compiles away in a release build, which is the one
    // where a frontend bug would ship. A handler that calls `pump()` would
    // re-enter the drain and deliver the boundary's events twice, so this
    // refuses the call, says so loudly, and reports the truth about the session.
    if (impl_->draining) {
        Log::debugger()->error(
            "pump() called from inside an event delivery — refused (§5: a "
            "handler may not drive the machine)");
        hint.remote_attached = impl_->any_peer_connected();
        hint.paused          = impl_->ds().paused();
        return hint;
    }

    // GH #276 B4 — CAP-01's deferred screenshots, FIRST: this is the loop
    // owner's post-frames slot, so the frame it just ran is complete and
    // rendered, and no command below has touched the machine yet — a `poke`
    // later in this drain must not change the `.SCR` of a frame that has ended.
    impl_->service_captures();

    // THE DRAIN. §4.8 SES-03 and §9: "a queued command chain is drained in one
    // `pump` while paused, `pump(0)` while running services exactly one".
    //
    // BOTH ARMS ARE REAL AND DIFFERENT, which is why the running arm is not
    // simply the paused arm with a zero budget: while RUNNING the loop owner
    // needs its thread back for the next frame, so each service is asked for at
    // most one command whatever the budget says; while PAUSED the machine is not
    // advancing and the thread is better spent answering the chain (REQ-zrcp-01:
    // a DeZog zrcp step is ~15 sequential round trips, which at one per tick is
    // 300 ms).
    //
    // "WHILE PAUSED" IS THE LIVE STATE, asked after every command — not the
    // state at entry, which is what B3's first cut read. A command can change
    // it: a `run` (or a `step_over`, which resumes) gives the loop owner its
    // frames back at once instead of holding the thread for the rest of the
    // budget, and a `pause` arriving while running lets the chain behind it
    // (ZRCP's `enter-cpu-step` followed by its reads) be answered in the same
    // pump instead of one tick per command. §4.8's wording is "while paused,
    // after answering a command"; both arms are pinned, SES-03-20/21.
    const auto start = std::chrono::steady_clock::now();
    auto elapsed_ms  = [start]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start)
            .count();
    };

    for (Service* s : impl_->services) {
        // The FIRST command may block up to `max_wait_ms`. A headless loop
        // paused with a remote attached passes ~50 here, which is what turns its
        // busy spin into a `poll()`.
        ServiceStep step = s->service_once(budget.max_wait_ms);

        // Keep answering while the peer keeps talking AND the machine is paused,
        // bounded by `budget_ms`. `budget_ms == 0` therefore means ONE command,
        // not "unbounded" — the zero form is the Qt/SDL "never block" budget and
        // must not become an unbounded drain by omission.
        while (step == ServiceStep::Serviced && impl_->ds().paused() &&
               elapsed_ms() < budget.budget_ms)
            step = s->service_once(budget.drain_ms);
    }

    // AFTER the drain, so a stop a command caused in this pump is notified in
    // this pump — and so a `Paused` packet is written by the same
    // `flush_notifications()` that writes the command replies.
    impl_->sync_notifications();
    for (Service* s : impl_->services) s->flush_notifications();

    hint.remote_attached = impl_->any_peer_connected();
    hint.paused          = impl_->ds().paused();
    return hint;
}

// ---------------------------------------------------------------------------
// SES-04 — the effective stop policy
// ---------------------------------------------------------------------------

StopPolicy Debugger::Impl::effective_stop_policy() const {
    if (stop_policy == StopPolicy::Pause) return StopPolicy::Pause;
    // §4.8 SES-04 / architecture §1.3 item 11: `ExitNonZero` becomes `Pause`
    // while a remote client is connected — "a client blocked on `run` must get
    // its stop reply". A CONNECTED PEER, not an attached client: an open Qt
    // debugger window or a loaded script is an attached client too, and neither
    // is waiting on a stop reply.
    return any_peer_connected() ? StopPolicy::Pause : StopPolicy::ExitNonZero;
}

// ---------------------------------------------------------------------------
// SES-05 — live raster, attached
// ---------------------------------------------------------------------------

Result Debugger::set_live_raster(ClientId cid, bool enabled) {
    Impl::Client* c = impl_->find_client(cid);
    if (!c) return Result::RefusedUnavailable;
    c->live_raster = enabled;
    impl_->clients_changed();
    return Result::Ok;
}

// The CLIENTS' OR. Since GH #278 WP4c retired `DebugState::active()` it is
// also exactly `raster_live()`, which had that bit as a second term.
bool Debugger::live_raster() const { return impl_->ds().live_raster(); }

// §5's `attached`: is any ARMING client attached (an observer, REQ-qt-32, is
// not counted — `clients_changed()`)?
//
// Read back from `DebugState::attached()`, the SAME precomputed bit the step
// machinery reads (GH #276 B3), rather than re-derived: one formula, so the verb
// a client asks and the gate the hot loop obeys cannot disagree. Until GH #278
// WP4c that bit was ORed with the Qt window's `active()`, the transition's second
// contributor; the window is a client now and the bit is retired, so `attached`
// is the client half alone, which `clients_changed()` keeps equal to the list
// (row SES-01-02). `armed() == attached() || persistent()` holds except while a
// HOLD arms the machine (a rewind's replay, a magic stop), neither of which is
// anyone driving it.
bool Debugger::attached() const { return impl_->ds().attached(); }

}  // namespace dbg
}  // namespace jnext
