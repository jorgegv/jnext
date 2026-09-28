// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — §4.3 CAP-EVT and INS-17.
//
// Work package B2 of epic #276. The eleven verbs `debugger_pending.cpp` used to
// refuse, plus the two things the hot loop calls into: the PRE-INSTRUCTION
// `Execute` gate and the BOUNDARY DRAIN.
//
// ── THE PIPELINE, END TO END ────────────────────────────────────────────────
//
//   site  →  cheap filter  →  LATCH into the ring        (inside an instruction)
//   ────────────────────────────────────────────────────────────────────────
//   boundary  →  drain  →  build `Event`  →  per-subscription filter
//             →  `Condition`  →  `Handler`  →  verdict  →  `Stop` / `Log`
//                                                        (machine stopped)
//
// Nothing between the two halves runs user code, and nothing in the second half
// runs inside an instruction. The whole of the second half — every condition and
// every handler — executes under ONE `DebugState::InspectionScope`, which drops
// `guest_access()`: that is what makes a handler's own reads and its own
// mutations invisible to the event machinery (§4.2a), and it is not optional —
// a `poke()` from a handler would otherwise latch a watch on itself, because a
// delivery happens INSIDE `run_frame()`'s `GuestExecutionScope`.
//
// ── WHAT IS DELIBERATELY NOT HERE ───────────────────────────────────────────
//
// The `Paused` PUSH. §4.3's `Paused{matched[]}` is a listener notification, and
// listeners are B3's. The drain BUILDS the `matched[]` list and the
// `pause_reason` and leaves both in `Impl` for B3 to send; inventing a delivery
// channel here would be a second one to retire.
// ---------------------------------------------------------------------------

#include <cstring>

#include "debug/debugger_impl.h"
#include "debug/event_table.h"

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// Gate publication — the one function every table change ends with
// ---------------------------------------------------------------------------

void Debugger::Impl::gates_changed() {
    // The MMU slot masks, the port flag, and the `Execute` / `Cycle` arms.
    ds().refresh_event_gates();

    // The two per-ENGINE flags §4.3 specifies, which are NOT derived from the
    // table pointer at the site: `Copper::execute` runs once per master cycle
    // and `Dma::execute_burst` once per byte, so three dependent loads per
    // iteration is exactly what a plain bool member exists to avoid.
    //
    // ONE TERM PER SUB-KIND. B2 armed the whole Copper engine from
    // `has_kind(Copper) || has_kind(NextRegWrite)`, so a `Halt`-only subscriber
    // paid ~16 `Move` ring entries per slot and `has_copper_sub_kind()` — written
    // for exactly this — had zero callers. The `Move` term keeps the
    // `NextRegWrite` half because one MOVE latch fans out to both kinds.
    emu.copper().set_events_armed(
        /*move=*/events.has_copper_sub_kind(CopperEventKind::Move) ||
                 events.has_kind(EventKind::NextRegWrite),
        /*wait=*/events.has_copper_sub_kind(CopperEventKind::Wait),
        /*halt=*/events.has_copper_sub_kind(CopperEventKind::Halt));
    emu.dma().set_events_armed(
        /*start=*/events.has_dma_sub_kind(DmaEventKind::Start),
        /*per_byte=*/events.has_dma_sub_kind(DmaEventKind::Byte),
        /*end=*/events.has_dma_sub_kind(DmaEventKind::End));
}

// ---------------------------------------------------------------------------
// §4.3 — subscribe / unsubscribe / enable
// ---------------------------------------------------------------------------

Expected<EventId> Debugger::subscribe(ClientId by, const Subscription& sub) {
    // GH #276 B5 — a client that has DETACHED cannot own a subscription: its
    // `erase_client()` already ran, so one made now would stay armed for ever
    // with an owner no detach will ever sweep again — the "per-client state left
    // behind" class, re-created after the fact. An id never issued (a test, a
    // loop owner's CLIENT_NONE) is not "gone" and is accepted as before.
    if (impl_->client_gone(by)) return make_refused<EventId>(Result::RefusedUnavailable);

    // A kind out of range would index `live_kinds_` past its width and match
    // nothing for ever; refused rather than stored.
    if (static_cast<size_t>(sub.kind) >= EVENT_KIND_COUNT)
        return make_refused<EventId>(Result::RefusedUnavailable);

    // `Mem` / `Port` with `Access::None` can never match: `has_read` and
    // `has_write` are both false, so the subscription would be a silent no-op
    // that nonetheless reports `live` in `subscriptions()`.
    if ((sub.kind == EventKind::Mem || sub.kind == EventKind::Port) &&
        sub.access == Access::None)
        return make_refused<EventId>(Result::RefusedUnavailable);

    // `EventSource::Debugger` is documented in `events.h` as a source that NEVER
    // appears in a delivered `Event` — a debugger write fires no event at all
    // (§4.2a) — so a filter naming it can never match while reporting `live`.
    if (sub.filter.source == EventSource::Debugger)
        return make_refused<EventId>(Result::RefusedUnavailable);

    // A `Port` subscription left with the DEFAULT filter matches only port
    // 0x0000. Every other kind's default matches everything (`lo=0`,
    // `hi=0xFFFF`, `FRAME_EVERY`, an empty reg set), so `port_mask = 0xFFFF` with
    // `port_value = 0` is the one default that is a trap rather than a
    // wildcard — and no row in the suite exercised it, because all six Port rows
    // set the mask. Refused rather than silently matching one unused port; the
    // frozen default stays as it is.
    if (sub.kind == EventKind::Port &&
        sub.filter.port_mask == 0xFFFF && sub.filter.port_value == 0x0000)
        return make_refused<EventId>(Result::RefusedUnavailable);

    // WHY `RefusedUnavailable` FOR ALL OF THESE, and it is a compromise: `result.h` is
    // frozen by B0 and its eleven values have NO argument-validation member.
    // `Unsupported` is the wrong one — §4 defines it as "the backend does not
    // implement this", which a client uses to disable a whole capability, and a
    // malformed subscription must not read as "this build has no events".
    // `RefusedUnavailable` is the benign "what you asked for is not there",
    // which is true of a subscription that cannot exist. Recorded as a finding
    // against `result.h` rather than worked around silently.

    // §4.3 — `Cycle` is "one-shot BY NATURE", and nothing enforced it: the filter
    // is `master_cycle >= N`, so once the target passes it matches at EVERY
    // boundary for the rest of the session, `cycle_armed_` keeps
    // `events_pending()` permanently true (a drain call per instruction), and
    // `action = Stop` makes the machine unadvanceable — it re-pauses on every
    // resume. Forced here rather than "retired at delivery" so the model a client
    // lists is the model that runs: `subscriptions()` reports `once`.
    Subscription fixed = sub;
    if (fixed.kind == EventKind::Cycle) fixed.once = true;

    const EventId id = impl_->events.add(by, fixed);
    impl_->gates_changed();
    return make_ok<EventId>(id);
}

Result Debugger::unsubscribe(ClientId by, EventId id) {
    const EventTable::Entry* e = impl_->events.find(id);
    if (!e) return Result::RefusedUnavailable;
    // §4.3 — only its owner may. A different client gets the same refusal an
    // unknown id gets, deliberately: telling a client that an id it does not own
    // EXISTS is a fact about another client's session.
    if (e->owner != by) return Result::RefusedUnavailable;
    impl_->events.erase(id);
    impl_->gates_changed();
    return Result::Ok;
}

Result Debugger::set_enabled(ClientId by, EventId id, bool enabled) {
    const EventTable::Entry* e = impl_->events.find(id);
    if (!e) return Result::RefusedUnavailable;
    if (e->owner != by) return Result::RefusedUnavailable;
    impl_->events.set_enabled(id, enabled);
    impl_->gates_changed();
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// §4.3 — the master and per-client switches
// ---------------------------------------------------------------------------

bool Debugger::master_enabled() const {
    // ONE user-visible switch (GH #225). The legacy `BreakpointSet` half is
    // what the Qt panels still drive directly until package Q, and the two are
    // kept in lockstep by set_master_enabled() below, so either one is the
    // answer. Reported from BreakpointSet because that is where a Qt-side
    // toggle lands.
    return impl_->ds().breakpoints().master_enabled();
}

Result Debugger::set_master_enabled(bool enabled) {
    // BOTH halves, because there is one switch: the `EventTable`'s
    // subscriptions and `BreakpointSet`'s PC breakpoints and watchpoints are two
    // models of the same user-facing list during the Q transition, and a switch
    // that suspended only one of them would suspend half the user's breakpoints.
    impl_->ds().breakpoints().set_master_enabled(enabled);
    impl_->events.set_master_enabled(enabled);
    impl_->gates_changed();
    return Result::Ok;
}

bool Debugger::client_enabled(ClientId cid) const {
    return impl_->events.client_enabled(cid);
}

Result Debugger::set_client_enabled(ClientId cid, bool enabled) {
    // GH #276 B5 — the switch is per-client state and dies with the detach
    // (`Debugger::detach`); setting it for a gone client would re-create it.
    if (impl_->client_gone(cid)) return Result::RefusedUnavailable;
    impl_->events.set_client_enabled(cid, enabled);
    impl_->gates_changed();
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// §4.3 — probe_execute
// ---------------------------------------------------------------------------

bool Debugger::probe_execute(uint16_t pc) const {
    // A PURE query, and it must cover BOTH models: the GH #221 step-off arm
    // asks "is there something at the address I am standing on", and during the
    // Q transition that something may be a legacy PC breakpoint or an `Execute`
    // subscription. Transient ones count — a Step Over's target IS a reason to
    // skip the address on the next resume.
    if (impl_->ds().breakpoints().has_pc(pc)) return true;
    for (const auto& e : impl_->events.entries()) {
        if (!e.live || e.kind != EventKind::Execute) continue;
        if (pc < e.filter.lo || pc > e.filter.hi) continue;
        if (e.filter.page != PAGE_ANY &&
            e.filter.page != impl_->emu.mmu().get_effective_page(pc >> 13))
            continue;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// §4.3 `Host` — raise_host_event
// ---------------------------------------------------------------------------

Result Debugger::raise_host_event(ClientId by, const std::string& name) {
    if (name.size() > MAX_HOST_EVENT_NAME) return Result::Unsupported;

    if (!impl_->events.has_kind(EventKind::Host)) return Result::Ok;

    // NOT latched into the ring, and the reason is the ring's shape rather than
    // a shortcut: every ring entry is a fixed 48-byte POD with no room for a
    // 24-byte name, and this verb is called BY A FRONTEND at an instruction
    // boundary — the machine is already stopped, which is the whole precondition
    // a drain exists to establish. So it is delivered directly.
    Event ev;
    ev.kind  = EventKind::Host;
    ev.cycle = impl_->emu.clock().get();
    ev.frame = frame_tag(impl_->emu);
    ev.pc    = impl_->emu.cpu().get_registers().PC;
    std::memcpy(ev.host_name, name.c_str(), name.size());

    bool stop = false;
    // NOT unconditional: `drain_boundary` accumulates `Hit`s ACROSS a boundary's
    // events, and a `Handler` may call this verb from inside that loop — which
    // `deliver_to_subscribers` documents as supported. B2 cleared it anyway, so a
    // nested raise threw away the outer boundary's `matched[]`, which §4.3 makes
    // part of the `Paused` contract and B3 is the first consumer of. The
    // `draining` flag already guards the ring and `compact()`; it guards this too
    // now.
    if (!impl_->draining) impl_->events.clear_hits();
    impl_->deliver_to_subscribers(ev, stop, by);
    if (stop) {
        // A `Stop` verdict PAUSES, here as everywhere else. This verb is the one
        // delivery point with no hot-loop caller to act on a return value, so
        // the pause is taken here or not at all — and "the subscription said
        // Stop and the machine kept running" is exactly the silent no-op §4
        // forbids.
        impl_->ds().pause();
        // `from_event=true`: a `Host` subscription's `Stop` is an `Action::Stop`
        // verdict like any other, so SES-04's policy applies to it. The verb that
        // RAISED the event is not the stop — the subscription is.
        impl_->apply_stop(/*from_event=*/true);
    }
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// INS-17 — the subscription model and the delivery history
// ---------------------------------------------------------------------------

std::vector<SubscriptionInfo> Debugger::subscriptions(bool include_transient) const {
    std::vector<SubscriptionInfo> out;
    for (const auto& e : impl_->events.entries()) {
        if (e.removed) continue;
        if (e.transient && !include_transient) continue;
        SubscriptionInfo si;
        si.id            = e.id;
        si.kind          = e.kind;
        si.filter        = e.filter;
        si.access        = e.access;
        si.has_condition = static_cast<bool>(e.condition);
        si.has_handler   = static_cast<bool>(e.handler);
        si.once          = e.once;
        si.transient     = e.transient;
        si.action        = e.action;
        // `enabled` is the flag AS SET, never the live value: a lister draws the
        // user's checkbox, which must survive a master-switch round trip
        // untouched (the GH #225 rule). `live` is the computed one.
        si.enabled = e.enabled;
        si.live    = e.live;
        si.owner   = e.owner;
        out.push_back(std::move(si));
    }
    return out;
}

std::vector<Event> Debugger::events_fired_since(uint64_t seq) const {
    return impl_->events.since(seq);
}

// ---------------------------------------------------------------------------
// The drain — §4.3's delivery point
// ---------------------------------------------------------------------------

Event Debugger::Impl::build_event(const LatchEntry& le) const {
    Event ev;
    ev.kind  = le.kind;
    ev.cycle = le.cycle;
    ev.frame = le.frame;
    ev.pc    = le.pc;
    ev.vc    = le.vc;
    ev.hc    = le.hc;

    switch (le.kind) {
        case EventKind::Mem:
            ev.addr      = le.addr;
            ev.phys_page = le.page_or_aux;
            ev.value     = le.value;
            ev.prev      = le.prev;
            ev.access    = le.access;
            // §4.3 — `source` is tagged AT THE DRAIN from the slot's DMA flag.
            // A slot is DMA *or* CPU, never both (emulator.cpp's arbitration),
            // which is what makes one flag per slot sufficient.
            ev.source = emu.slot_ran_dma() ? EventSource::Dma : EventSource::Cpu;
            break;

        case EventKind::Port:
            ev.port   = le.addr;
            ev.value  = le.value;
            ev.access = le.access;
            ev.source = emu.slot_ran_dma() ? EventSource::Dma : EventSource::Cpu;
            break;

        case EventKind::NextRegWrite:
            ev.reg    = le.reg;
            ev.value  = le.value;
            ev.prev   = le.prev;
            ev.source = le.source;
            break;

        case EventKind::Scanline:
            ev.cvc = le.cvc;
            break;

        case EventKind::Reset:
            ev.reset_kind = static_cast<ResetKind>(le.misc);
            break;

        case EventKind::IntAck:
            ev.int_vector = le.misc;
            ev.int_mode   = le.misc2;
            break;

        case EventKind::Nmi:
            ev.nmi_source = static_cast<NmiButton>(le.misc);
            break;

        case EventKind::Copper:
            ev.copper_kind         = static_cast<CopperEventKind>(le.sub_kind);
            ev.copper_pc           = le.addr;
            ev.reg                 = le.reg;
            ev.value               = le.value;
            ev.wait_vpos           = le.page_or_aux;
            ev.wait_hpos_threshold = le.aux2;
            ev.hc_ula              = le.hc_ula;
            ev.cvc                 = le.cvc;
            ev.source              = EventSource::Copper;
            break;

        case EventKind::Dma:
            ev.dma_kind      = static_cast<DmaEventKind>(le.sub_kind);
            ev.dma_src       = le.addr;
            ev.dma_dst       = le.page_or_aux;
            ev.dma_length    = le.aux2;
            ev.dma_bytes     = le.dma_bytes;
            ev.dma_direction = le.misc2;
            ev.dma_mode      = le.misc3;
            ev.dma_is_io_src = le.flag_a;
            ev.dma_is_io_dst = le.flag_b;
            ev.value         = le.value;
            ev.source        = EventSource::Dma;
            break;

        // No payload beyond the common header.
        case EventKind::Execute:
        case EventKind::Frame:
        case EventKind::Cycle:
        case EventKind::Magic:
        case EventKind::Host:
        case EventKind::Count:
            break;
    }
    return ev;
}

// The address a `Hit` and a subscription stop's `pause_reason.addr` report —
// events.h's `Hit`: "The address (`Mem`, `Execute`) or port (`Port`) that
// matched". ONE function for both, because they are the same fact: B2 read
// `ev.addr` for both, which an `Execute` event never fills (its address is the
// PC), so every `Execute` hit — a user breakpoint AND the transients DeZog's
// "temp beats user" rule compares — reported 0x0000 (GH #276 B5, MATCH-02/03).
static uint16_t matched_addr(const Event& ev) {
    switch (ev.kind) {
        case EventKind::Port:    return ev.port;
        case EventKind::Execute: return ev.pc;
        default:                 return ev.addr;
    }
}

// §4.2a — WHY THIS TAKES A `Debugger&` AND RUNS UNDER `InspectionScope`.
//
// `Condition` observes, `Handler` may mutate, and both may read the whole
// inspection surface. Every one of those reads goes through the same `Mmu::read`
// the CPU uses, and a delivery happens inside `run_frame()`'s
// `GuestExecutionScope` where `watchpoints_live()` is TRUE — so without the
// scope a handler that pokes an address it is watching would latch a watch on
// itself, and one that merely READS a watched address would too.
void Debugger::Impl::deliver_to_subscribers(Event& ev, bool& stop,
                                            ClientId /*raiser*/) {
    Debugger& dbg = *self;

    // Re-entrancy: a handler may call `raise_host_event()`, which delivers
    // synchronously. The flag is what `unsubscribe()`-from-a-handler relies on
    // to defer compaction, and what keeps a nested delivery from clearing the
    // ring the outer drain is walking.
    const bool outer = !draining;
    draining = true;

    {
        DebugState::InspectionScope scope(ds());

        // INDEX-BASED, not iterator-based: a handler may `subscribe()` (a script
        // arming a follow-up rule) and invalidate any iterator into `subs_`. A
        // subscription ADDED during this delivery is deliberately not visited
        // for this event — it did not exist when the site latched.
        const size_t n = events.entries().size();
        for (size_t i = 0; i < n; ++i) {
            // Re-read through the container each time: the vector may have
            // reallocated under a handler's `subscribe()`.
            {
                const EventTable::Entry& probe = events.entries()[i];
                if (!probe.live || probe.once_fired) continue;
                if (!events.filter_matches(probe, ev)) continue;
            }

            // Identity, then the predicate, then the body. `seq` is allocated
            // only for an event that actually matched a filter, so a cursor
            // never has holes for events nobody subscribed to.
            ev.id    = events.entries()[i].id;
            ev.owner = events.entries()[i].owner;
            ev.seq   = events.next_seq();

            const Condition cond = events.entries()[i].condition;
            if (cond && !cond(ev, dbg)) continue;

            Action verdict = events.entries()[i].action;
            const Handler handler = events.entries()[i].handler;
            if (handler) {
                // §4.3 — "the returned `Action` is the handler's verdict and
                // OVERRIDES the subscription's static `action`".
                verdict = handler(ev, dbg);
            }

            events.record(ev);

            // §4.3 — `once` disables after the first ACCEPTED firing (filter
            // matched AND condition passed), not after the first match.
            //
            // Through `set_enabled()`, NOT by writing `enabled` directly: that
            // setter is what re-derives `live` and the slot masks, and a
            // hand-written flag left `subscriptions()` reporting a spent `once`
            // subscription as still LIVE and left its mask bit set — caught by
            // EVT-EXEC-31. The vector does not resize, so the loop index stays
            // valid across the call.
            if (const EventTable::Entry* live = events.find(ev.id)) {
                if (live->once) {
                    events.mark_once_fired(ev.id);
                    gates_dirty = true;
                }
            }

            switch (verdict) {
                case Action::Stop: {
                    stop = true;
                    Hit hit;
                    hit.event_id = ev.id;
                    hit.addr     = matched_addr(ev);
                    hit.access   = ev.access;
                    hit.value    = ev.value;
                    events.record_hit(hit);
                    note_event_stop(ev);
                    break;
                }
                case Action::Log:
                    self->log(CLIENT_NONE, LogLevel::Info, log_line_for(ev));
                    break;
                case Action::Continue:
                    break;
            }
        }
    }

    if (outer) {
        draining = false;
        // A handler's `unsubscribe()` only tombstoned its row; removing it is
        // safe now that no loop is walking the vector.
        events.compact();
        if (gates_dirty) {
            gates_dirty = false;
            gates_changed();
        }
    }
}

bool Debugger::Impl::drain_boundary() {
    // §4.2a — handlers do not run in `replay_mode_`, and the backend consults
    // the `EventTable` only when `!replay_mode_`: `rewind_to_cycle()`
    // fast-forwards with the gate live, and an `Execute` subscription inside the
    // replayed span would otherwise pause the replay short. The ring is still
    // cleared, so the replay does not hand its latches to the next real boundary.
    if (emu.replay_mode()) {
        events.clear_ring();
        return false;
    }

    events.clear_hits();
    bool stop = false;

    const uint16_t dropped    = events.dropped();
    const bool     overflowed = events.overflowed();
    const size_t   n          = events.size();

    for (size_t i = 0; i < n; ++i) {
        Event ev = build_event(events.at(i));
        // §4.3's overflow contract: the first N are delivered in order and
        // EVERY delivery of this boundary is marked, because a subscriber whose
        // own event survived still has to know that the boundary was lossy.
        ev.overflowed = overflowed;
        ev.dropped    = dropped;
        deliver_to_subscribers(ev, stop, CLIENT_NONE);

        // §4.3 — a Copper MOVE is ONE latch entry fanned out at the drain to
        // both `Copper{Move}` and `NextRegWrite{source=Copper}`. Never two
        // entries in the ring, so never two chances to overflow, and the NR-side
        // hook skips a Copper write for exactly this reason.
        if (ev.kind == EventKind::Copper &&
            ev.copper_kind == CopperEventKind::Move &&
            events.has_kind(EventKind::NextRegWrite)) {
            Event nr;
            nr.kind       = EventKind::NextRegWrite;
            nr.cycle      = ev.cycle;
            nr.frame      = ev.frame;
            nr.pc         = ev.pc;
            nr.vc         = ev.vc;
            nr.hc         = ev.hc;
            nr.reg        = ev.reg;
            nr.value      = ev.value;
            nr.source     = EventSource::Copper;
            nr.overflowed = overflowed;
            nr.dropped    = dropped;
            // `prev` is NOT carried: the Copper site latches before the write,
            // but it does not peek the register (the NR-side hook is what does,
            // and it is suppressed for a Copper write precisely so there is one
            // entry). Reported as a limitation rather than filled with `value`.
            deliver_to_subscribers(nr, stop, CLIENT_NONE);
        }
    }

    // `Cycle` has no latch site: its filter is a comparison against the master
    // clock, so it is evaluated HERE, once per boundary, while one is armed.
    if (events.has_kind(EventKind::Cycle)) {
        Event ev;
        ev.kind  = EventKind::Cycle;
        ev.cycle = emu.clock().get();
        ev.frame = frame_tag(emu);
        ev.pc    = emu.cpu().get_registers().PC;
        deliver_to_subscribers(ev, stop, CLIENT_NONE);
    }

    events.clear_ring();

    if (stop) apply_stop(/*from_event=*/true);
    return stop;
}

bool Debugger::Impl::execute_gate(uint16_t pc) {
    if (emu.replay_mode()) return false;
    if (!events.has_kind(EventKind::Execute)) return false;

    Event ev;
    ev.kind      = EventKind::Execute;
    ev.cycle     = emu.clock().get();
    ev.frame     = frame_tag(emu);
    ev.pc        = pc;
    ev.phys_page = emu.mmu().get_effective_page(pc >> 13);

    events.clear_hits();
    bool stop = false;
    deliver_to_subscribers(ev, stop, CLIENT_NONE);
    if (stop) apply_stop(/*from_event=*/true);
    return stop;
}

// §4.3 — "`transient` … auto-removed at the next stop". Done HERE, on the stop
// the backend can see, which is the one a subscription caused. A stop the
// backend did not cause (a legacy PC breakpoint, the data-breakpoint latch)
// leaves a transient armed — which is exactly what today's single one-shot does,
// so the Step Over / Run to Here behaviour is unchanged by the replacement.
void Debugger::Impl::apply_stop(bool from_event) {
    if (events.clear_transient()) gates_changed();
    if (!from_event) return;

    // ── SES-04, the ONE place the stop policy is consumed ────────────────────
    //
    // `effective_stop_policy()`, never `stop_policy`: the stored value is what
    // the loop owner set and the effective one is what a `Stop` does right now
    // (`ExitNonZero` becomes `Pause` while a remote client is connected, so a
    // DeZog session blocked on `run` gets its stop reply instead of the process
    // exiting under it — §4.8 SES-04, owner decision §1.3 item 11).
    //
    // THE MACHINE IS PAUSED EITHER WAY: the hot loop pauses on a `Stop` verdict
    // SHORTLY AFTER THIS RETURNS — `drain_events()` sets `event_stop_pending_`
    // and `emulator.cpp`'s `if (data_bp_hit() || event_stop_pending_)` pauses at
    // the end of the slice; the pre-instruction gate's caller pauses on the next
    // line. So the machine is NOT yet paused at this point, and an earlier draft
    // of this comment said it was. `ExitNonZero` does not suppress that pause
    // either way — it asks the
    // LOOP OWNER to log the event and exit non-zero, which is the whole of
    // owner decision §1.3 item 12 ("one rule for every stop"). A frontend that
    // registers no listener therefore behaves exactly as it does today, which is
    // why the CLI contract change that decision names (the man-page line under
    // `--magic-breakpoint`, and `magic-bp-func`'s re-pin) lands with the
    // loop-owner wiring rather than here: B3 provides the mechanism and changes
    // no frontend's behaviour.
    if (effective_stop_policy() != StopPolicy::ExitNonZero) return;

    self->log(CLIENT_NONE, LogLevel::Warn,
              "STOP under StopPolicy::ExitNonZero — requesting exit " +
                  std::to_string(kStopExitCode));
    notify_exit_requested(kStopExitCode);
}

void Debugger::Impl::note_event_stop(const Event& ev) {
    // CTL-13's `pause_reason` for a subscription stop. ONE reason per stop: the
    // FIRST `Stop` of the boundary wins, because that is the one the machine
    // stopped on; `matched[]` carries all of them for a client that wants the
    // rest.
    if (event_stop_latched) return;
    event_stop_latched = true;
    event_stop_gen     = ds().resume_generation();
    event_stop.id      = ev.id;
    event_stop.by      = ev.owner;
    event_stop.addr    = matched_addr(ev);
    event_stop.access  = ev.access;
    switch (ev.kind) {
        case EventKind::Execute:
            event_stop.kind = PauseReason::Kind::Breakpoint;
            break;
        case EventKind::Mem:
        case EventKind::Port:
            event_stop.kind = PauseReason::Kind::Watch;
            break;
        case EventKind::Magic:
            // UNOWNED (Revision 6): the magic opcode is the guest's, not any
            // client's verb, so no client's detach may resume it.
            event_stop.kind = PauseReason::Kind::Magic;
            event_stop.by   = CLIENT_NONE;
            break;
        default:
            // Everything else — a `Frame`, `Scanline`, `Cycle`, `NextRegWrite`,
            // `Copper`, `Dma`, `Reset`, `IntAck`, `Nmi` or `Host` subscription
            // that asked to stop. `Script` is the closed set's name for "a
            // subscriber's explicit stop", and a subscriber is what every one of
            // these is: no panel stops the machine on a scanline.
            //
            // `PauseReason::text` stays EMPTY, and that is a finding rather than
            // an omission: a `Handler` returns an `Action` and nothing else, so
            // the published interface has no channel for the message §4.2's
            // `Script` row describes. Recorded in the B2 report.
            event_stop.kind = PauseReason::Kind::Script;
            break;
    }
}

std::string Debugger::Impl::log_line_for(const Event& ev) const {
    // SES-06's `Log` action. Terse and machine-greppable: the kind, the
    // subscription, and the payload that identifies the instance.
    char buf[160];
    switch (ev.kind) {
        case EventKind::Mem:
            std::snprintf(buf, sizeof(buf),
                          "EVENT mem %s 0x%04X = 0x%02X (prev 0x%02X) page %u "
                          "pc 0x%04X id %u",
                          has_write(ev.access) ? "write" : "read", ev.addr,
                          ev.value, ev.prev, unsigned(ev.phys_page), ev.pc,
                          unsigned(ev.id));
            break;
        case EventKind::Port:
            std::snprintf(buf, sizeof(buf),
                          "EVENT port %s 0x%04X = 0x%02X pc 0x%04X id %u",
                          has_write(ev.access) ? "out" : "in", ev.port, ev.value,
                          ev.pc, unsigned(ev.id));
            break;
        case EventKind::NextRegWrite:
            std::snprintf(buf, sizeof(buf),
                          "EVENT nextreg 0x%02X = 0x%02X (prev 0x%02X) id %u",
                          ev.reg, ev.value, ev.prev, unsigned(ev.id));
            break;
        case EventKind::Execute:
            std::snprintf(buf, sizeof(buf), "EVENT execute 0x%04X id %u", ev.pc,
                          unsigned(ev.id));
            break;
        default:
            std::snprintf(buf, sizeof(buf),
                          "EVENT kind %u pc 0x%04X cycle %llu id %u",
                          unsigned(ev.kind), ev.pc,
                          static_cast<unsigned long long>(ev.cycle),
                          unsigned(ev.id));
            break;
    }
    if (ev.overflowed) {
        const size_t len = std::strlen(buf);
        std::snprintf(buf + len, sizeof(buf) - len, " [overflowed, dropped %u]",
                      unsigned(ev.dropped));
    }
    return buf;
}

}  // namespace dbg
}  // namespace jnext
