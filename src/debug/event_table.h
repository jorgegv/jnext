#pragma once

// ---------------------------------------------------------------------------
// jnext::dbg::EventTable — the subscription store, the cheap filters, the
// 512-entry latch ring and the INS-17 delivery history.
//
// Work package B2 of epic #276 (doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md
// §4.3 CAP-EVT + §6). The successor of `BreakpointSet`'s per-address model:
// every kind of the §4.3 table, with a filter that is a SUBSCRIPTION-TIME
// value and a hot path that reads one byte.
//
// THIS HEADER IS NOT PUBLISHED. It is internal to `jnext_debug` — except that
// `DebugState` holds a pointer to an `EventTable` and the eight `Mmu`
// watchpoint sites reach it through that pointer, so `src/memory/` compiles
// against this file too. It therefore includes `debug/events.h` (published,
// Qt-free, `Emulator`-free) and NOTHING from `src/core/`.
//
// ── FOUR PROPERTIES THAT ARE THE CONTRACT ──────────────────────────────────
//
//  1. NO MATCHING RUNS INSIDE AN INSTRUCTION BEYOND A RANGE COMPARE. A site
//     calls `mem_would_match()` / `port_would_match()` / `nr_would_match()`,
//     which are integer compares over the few armed filters of that kind and
//     evaluate NO `Condition` and run NO `Handler`. Everything else happens at
//     the boundary drain, with the machine stopped.
//
//  2. THE SLOT MASKS ARE A GATE, NOT AN ANSWER. `rd_slot_mask()` /
//     `wr_slot_mask()` are conservative: a set bit means "a precise scan is
//     worth doing for this 8 KB slot", never "this access matched". They are
//     recomputed only when a subscription changes or the MMU remaps a slot
//     (`set_slot_page()`), never per access. Their CONSUMER is
//     `BreakpointSet`, which ORs them with its own legacy-watchpoint mask so
//     the hot path reads exactly one byte — see `BreakpointSet::
//     set_event_slot_masks()` for why the bytes live there and not here.
//
//  3. THE RING IS BOUNDED AND SAYS SO. `latch()` keeps the FIRST
//     `ring_capacity()` entries in order and counts the rest in `dropped()`.
//     Overflow is a specified, tested path (§4.3), reachable in a test through
//     `shrink_ring_for_test()`.
//
//  4. A REMOVED SUBSCRIPTION IS A TOMBSTONE, NOT AN ERASE. `subs_` never
//     shrinks while a drain is in flight, because a `Handler` may
//     `unsubscribe()` — its own id included — and the drain is iterating.
//     `compact()` runs when no drain is in flight.
// ---------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "debug/events.h"

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// The latch ring
// ---------------------------------------------------------------------------

/// §4.3 — the ring bound, derived there: 512 entries covers every MOVE burst a
/// contended slot can produce; a chain of already-satisfied WAITs is the
/// overflow path, which is why overflow is specified rather than "unreachable".
constexpr size_t LATCH_RING_CAPACITY = 512;

/// One latched site entry — what a hook records before any predicate runs.
///
/// FLAT, like `Event`, and for the same reason: it is copied into the ring by
/// value on the hot path, so it must be trivially copyable. `kind` says which
/// fields mean anything. It is deliberately NOT an `Event`: an `Event` carries
/// `id`, `owner` and `seq`, which the SITE cannot know — one site entry fans
/// out to every matching subscription at the drain, and a Copper MOVE fans out
/// to two KINDS from one entry (§4.3).
///
/// SIZE: 48 bytes, not the "~16 bytes each, 8 KB" §4.3 estimates — 512 entries
/// are 24 KB. The estimate was made before the per-kind payloads were
/// enumerated (the DMA row alone needs src, dst, length, bytes, direction,
/// mode and two I/O flags). It is a one-off allocation outside the hot path and
/// costs nothing per access; recorded here rather than silently.
struct LatchEntry {
    EventKind kind = EventKind::Mem;
    /// `CopperEventKind` / `DmaEventKind`, as a raw byte.
    uint8_t sub_kind = 0;
    Access      access = Access::None;
    EventSource source = EventSource::Cpu;

    /// `Mem`: logical address. `Port`: the 16-bit port. `Copper`: copper PC.
    /// `Dma`: source address.
    uint16_t addr = 0;
    /// `Mem`: the effective MMU page behind `addr`. `Dma`: destination.
    /// `Copper{Wait}`: the WAIT's target line.
    uint16_t page_or_aux = 0;
    /// `Dma{Start,End}`: block length. `Copper{Wait}`: the hpos threshold.
    uint16_t aux2 = 0;

    uint8_t value = 0;
    uint8_t prev  = 0;
    /// `NextRegWrite`, `Copper{Move}`: the register number.
    uint8_t reg = 0;
    /// `Reset`: `ResetKind`. `Nmi`: `NmiButton`. `IntAck`: the vector byte.
    uint8_t misc = 0;
    /// `IntAck`: the interrupt mode. `Dma`: direction byte.
    uint8_t misc2 = 0;
    /// `Dma`: mode byte.
    uint8_t misc3 = 0;
    bool flag_a = false;   ///< `Dma{Byte}`: `is_io_src`.
    bool flag_b = false;   ///< `Dma{Byte}`: `is_io_dst`.

    uint16_t pc = 0;
    int16_t  vc = 0;
    int16_t  hc = 0;
    /// `Copper`: VHDL `hc_ula` and `o_vc_cu`, the counters the Copper itself
    /// compares against (NOT the raw pair above — conflating them was GH #181).
    int16_t hc_ula = 0;
    int16_t cvc    = 0;

    uint64_t cycle = 0;
    uint32_t frame = 0;
    /// `Dma{End}`: bytes actually transferred.
    uint32_t dma_bytes = 0;
};

// ---------------------------------------------------------------------------
// EventTable
// ---------------------------------------------------------------------------

class EventTable {
public:
    /// One subscription as stored. `Subscription` is the caller's value; this
    /// is it plus the identity and the cached live flag.
    struct Entry {
        EventId  id    = EVENT_NONE;
        ClientId owner = CLIENT_NONE;

        EventKind   kind = EventKind::Execute;
        EventFilter filter;
        Access      access = Access::ReadWrite;
        Condition   condition;
        Handler     handler;
        bool   once      = false;
        bool   transient = false;
        Action action    = Action::Stop;

        /// The subscription's OWN flag, as set. Never the live value —
        /// `SubscriptionInfo::enabled` must survive a master round trip
        /// untouched (the GH #225 rule).
        bool enabled = true;

        /// Cached: `enabled && (transient || master) && client_enabled(owner)`.
        /// Rebuilt on every change, never evaluated per instruction.
        bool live = false;

        /// Tombstone — `unsubscribe()` sets it; `compact()` removes the row.
        bool removed = false;

        /// §4.3 — `once` has had its ONE accepted firing. Separate from
        /// `enabled` (which the firing also clears) so a client that re-enables
        /// a spent `once` subscription does not silently get a second firing:
        /// "disable after the first accepted firing" is a property of the
        /// subscription, not of the checkbox.
        bool once_fired = false;
    };

    // ── subscriptions ───────────────────────────────────────────────────

    /// Register `sub` for `owner`. Ids are never reused within a run.
    EventId add(ClientId owner, const Subscription& sub);

    /// Tombstone the subscription `id`. Returns false if there is no such live
    /// id. §4.3's owner-only rule is the CALLER's: it reads `find(id)->owner`
    /// first and refuses, because "another client's id" and "no such id" are
    /// different `Result`s and this class does not know them.
    bool erase(EventId id);

    Entry*       find(EventId id);
    const Entry* find(EventId id) const;

    /// Every entry, tombstones included. The caller filters.
    const std::vector<Entry>& entries() const { return subs_; }

    /// Drop every `transient` subscription (§4.3: "auto-removed at the next
    /// stop"). Returns how many went.
    size_t clear_transient();

    /// SES-01 — drop everything a client owns. B3's `detach` needs it; declared
    /// here because the data is here.
    size_t erase_client(ClientId cid);

    // ── switches ────────────────────────────────────────────────────────

    bool master_enabled() const { return master_; }
    void set_master_enabled(bool e);

    /// Live = master ∧ client ∧ own flag. A client id never seen is ENABLED:
    /// the switch is an opt-out, so a fresh client is not silently inert.
    bool client_enabled(ClientId cid) const;
    void set_client_enabled(ClientId cid, bool e);

    /// Set one subscription's own flag. Returns false for an unknown id.
    bool set_enabled(EventId id, bool e);

    /// §4.3 — record that a `once` subscription has had its ONE accepted firing,
    /// and disable it. One call, so the two halves cannot be done separately: a
    /// `once_fired` without the `refresh()` that `set_enabled` performs left the
    /// entry reporting `live` and kept its slot-mask bit set.
    bool mark_once_fired(EventId id);

    // ── the cheap kind gate ─────────────────────────────────────────────

    /// "Is any LIVE subscription of this kind armed?" One integer test, which
    /// is what every hook outside the memory sites uses as its gate.
    bool has_kind(EventKind k) const {
        return (live_kinds_ & kind_bit(k)) != 0;
    }

    /// The whole live-kind mask, for SES-02's `SubscriptionsChanged{kinds}`
    /// payload. `has_kind()` is the hot path's question; this is the
    /// notification's.
    EventKindMask live_kinds() const { return live_kinds_; }

    /// GH #276 B3 — how many times the SUBSCRIPTION MODEL has changed.
    ///
    /// Bumped by `refresh()`, which every mutator already ends with (`add`,
    /// `erase`, `erase_client`, `clear_transient`, the three switches,
    /// `mark_once_fired`), and by nothing else. So SES-02's
    /// `SubscriptionsChanged` push can be decided by comparing ONE integer, and
    /// a mutator added later cannot forget to notify — it cannot forget
    /// `refresh()`, because without it its own change would not take effect.
    ///
    /// Deliberately NOT bumped by `set_slot_page()`, which recomputes the masks
    /// without touching the model: an MMU paging write is not a subscription
    /// change, and notifying every listener on one would push thousands of times
    /// a frame.
    uint64_t revision() const { return revision_; }

    /// `Dma{Byte}` arming is a PER-ENGINE cost (§4.3), so it is asked
    /// separately from `Dma{Start,End}`.
    bool has_dma_sub_kind(DmaEventKind k) const {
        return (live_dma_kinds_ & (1u << static_cast<unsigned>(k))) != 0;
    }
    bool has_copper_sub_kind(CopperEventKind k) const {
        return (live_copper_kinds_ & (1u << static_cast<unsigned>(k))) != 0;
    }

    // ── §6 slot masks ───────────────────────────────────────────────────

    uint8_t rd_slot_mask() const { return rd_mask_; }
    uint8_t wr_slot_mask() const { return wr_mask_; }

    /// §6.1 — the MMU remapped a slot. Recomputes the masks iff any armed
    /// `Mem` filter carries a physical-page set (nothing else can change
    /// meaning when a slot's page changes).
    ///
    /// Returns true iff a mask byte actually changed, so the caller only
    /// re-publishes when it must.
    bool set_slot_page(int slot, uint16_t page);

    /// The page currently mapped at `slot`, as last published.
    uint16_t slot_page(int slot) const {
        return (slot >= 0 && slot < 8) ? slot_page_[static_cast<size_t>(slot)]
                                       : PAGE_ANY;
    }

    // ── site-side "would any live subscription match" ────────────────────
    //
    // NO condition, NO handler, NO allocation. The one thing a hot site is
    // allowed to ask.

    bool mem_would_match(uint16_t addr, uint16_t page, Access a) const;
    bool port_would_match(uint16_t port, Access a) const;
    bool nr_would_match(uint8_t reg, EventSource src) const;

    // ── the latch ring ──────────────────────────────────────────────────

    /// Append, or count a drop. Never allocates: the storage is fixed.
    void latch(const LatchEntry& e);

    bool     pending()  const { return count_ != 0; }
    size_t   size()     const { return count_; }
    uint16_t dropped()  const { return dropped_; }
    bool     overflowed() const { return dropped_ != 0; }
    const LatchEntry& at(size_t i) const { return ring_[i]; }
    void clear_ring() { count_ = 0; dropped_ = 0; }

    /// §4.3 — the test hook that makes overflow reachable, the
    /// `RewindBuffer::shrink_expected_snapshot_bytes_for_test` idiom: shrink
    /// the EFFECTIVE capacity so a MOVE burst can be driven over it on purpose.
    /// Clamped to [1, LATCH_RING_CAPACITY]; clears whatever is in the ring,
    /// because a shrink below `count_` would otherwise leave entries that are
    /// no longer addressable.
    void shrink_ring_for_test(size_t entries);
    size_t ring_capacity() const { return capacity_; }

    // ── INS-17 history ──────────────────────────────────────────────────

    /// The next delivery sequence number. Monotonic across every kind and
    /// client, which is what makes it a cursor.
    uint64_t next_seq() { return ++seq_; }
    uint64_t last_seq() const { return seq_; }

    // ── §4.3 `Paused{matched[]}` ─────────────────────────────────────────
    //
    // Every subscription that matched at a stop, transient ones included. HELD
    // HERE rather than in `Debugger::Impl` for two reasons: it is delivery state
    // like the ring and the history beside it, and `Impl` is a private nested
    // type, so nothing could observe it — which is why B2's bug of clearing it
    // from a nested `raise_host_event()` had no row that could see it. Reachable
    // from a test through `DebugState::event_table()`.
    //
    // The BOUNDARY owns the clear: whoever starts a delivery batch
    // (`drain_boundary`, `execute_gate`, a TOP-LEVEL `raise_host_event`) clears
    // it; a nested delivery must not.
    const std::vector<Hit>& hits() const { return hits_; }
    void clear_hits() { hits_.clear(); }
    void record_hit(const Hit& h) { hits_.push_back(h); }

    /// Record a delivered event for `events_fired_since()`. Bounded — the
    /// history is a poll buffer, not a trace log.
    void record(const Event& ev);
    std::vector<Event> since(uint64_t seq) const;
    void clear_history() { history_.clear(); history_first_ = 0; }

    /// How many delivered events the history keeps. A poller that falls further
    /// behind than this loses the oldest, which is why `Event::seq` is in the
    /// payload: the gap is detectable.
    static constexpr size_t HISTORY_CAPACITY = 1024;

    // ── matching, at the boundary ────────────────────────────────────────

    /// Does `e`'s cheap filter accept `ev`? The whole of §4.3's "Cheap filter
    /// (where)" column, for every kind, in one place. Evaluates no condition.
    bool filter_matches(const Entry& e, const Event& ev) const;

    /// Re-derive `live` for every entry and the cached kind masks. Public
    /// because `set_client_enabled` on an id with no subscription yet must
    /// still take effect when one arrives.
    void refresh();

    /// Remove tombstoned rows. NEVER call while a drain is iterating.
    void compact();

    /// How many times `compact()` has run. Contract 4 says compaction is
    /// deferred to the OUTER delivery frame, and that is otherwise pinned only by
    /// UB: erasing a row under the drain's index is undefined, not reliably
    /// observable, and a row that depends on UB being visible is not a row. This
    /// counter makes the MECHANISM checkable instead — a nested delivery that
    /// removes a subscription must leave it at exactly one.
    size_t compactions() const { return compactions_; }

private:
    void recompute_masks_();

    std::vector<Entry> subs_;
    EventId next_id_ = 1;

    bool master_ = true;
    /// Clients explicitly DISABLED. An absent id is enabled (see
    /// `client_enabled`), so the common case stores nothing.
    std::vector<ClientId> disabled_clients_;

    EventKindMask live_kinds_        = 0;
    uint8_t       live_dma_kinds_    = 0;
    uint8_t       live_copper_kinds_ = 0;

    uint8_t rd_mask_ = 0;
    uint8_t wr_mask_ = 0;
    std::array<uint16_t, 8> slot_page_{};
    /// Does any live `Mem` entry carry a physical-page set? Cached, because
    /// `set_slot_page()` is called from the MMU's dispatch rebuild and must
    /// cost nothing when no subscription can care.
    bool any_page_set_ = false;

    std::array<LatchEntry, LATCH_RING_CAPACITY> ring_{};
    size_t   capacity_ = LATCH_RING_CAPACITY;
    size_t   count_    = 0;
    uint16_t dropped_  = 0;

    uint64_t seq_ = 0;
    std::vector<Event> history_;
    size_t history_first_ = 0;
    std::vector<Hit> hits_;
    size_t compactions_ = 0;

    /// GH #276 B3 — see `revision()`. Bumped in `refresh()`.
    uint64_t revision_ = 0;
};

}  // namespace dbg
}  // namespace jnext
