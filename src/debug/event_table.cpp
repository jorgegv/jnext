#include "debug/event_table.h"

#include <algorithm>

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// Subscriptions
// ---------------------------------------------------------------------------

EventId EventTable::add(ClientId owner, const Subscription& sub) {
    Entry e;
    e.id        = next_id_++;
    e.owner     = owner;
    e.kind      = sub.kind;
    e.filter    = sub.filter;
    e.access    = sub.access;
    e.condition = sub.condition;
    e.handler   = sub.handler;
    e.once      = sub.once;
    e.transient = sub.transient;
    e.action    = sub.action;
    e.enabled   = sub.enabled;
    subs_.push_back(std::move(e));
    refresh();
    return subs_.back().id;
}

bool EventTable::erase(EventId id) {
    for (auto& e : subs_) {
        if (e.id != id || e.removed) continue;
        e.removed = true;
        // The closures go NOW rather than at compact(): a DSL interpreter's
        // handler captures its own state, and holding it alive after the
        // subscriber asked for the removal is a lifetime surprise waiting to
        // happen. The row itself stays as a tombstone (contract 4).
        e.condition = nullptr;
        e.handler   = nullptr;
        refresh();
        return true;
    }
    return false;
}

EventTable::Entry* EventTable::find(EventId id) {
    for (auto& e : subs_)
        if (e.id == id && !e.removed) return &e;
    return nullptr;
}

const EventTable::Entry* EventTable::find(EventId id) const {
    for (const auto& e : subs_)
        if (e.id == id && !e.removed) return &e;
    return nullptr;
}

size_t EventTable::clear_transient() {
    size_t n = 0;
    for (auto& e : subs_) {
        if (e.removed || !e.transient) continue;
        e.removed   = true;
        e.condition = nullptr;
        e.handler   = nullptr;
        ++n;
    }
    if (n) refresh();
    return n;
}

size_t EventTable::erase_client(ClientId cid) {
    size_t n = 0;
    for (auto& e : subs_) {
        if (e.removed || e.owner != cid) continue;
        e.removed   = true;
        e.condition = nullptr;
        e.handler   = nullptr;
        ++n;
    }
    if (n) refresh();
    return n;
}

// ---------------------------------------------------------------------------
// Switches
// ---------------------------------------------------------------------------

void EventTable::set_master_enabled(bool e) {
    if (master_ == e) return;
    master_ = e;
    refresh();
}

bool EventTable::client_enabled(ClientId cid) const {
    return std::find(disabled_clients_.begin(), disabled_clients_.end(), cid) ==
           disabled_clients_.end();
}

void EventTable::set_client_enabled(ClientId cid, bool e) {
    const auto it =
        std::find(disabled_clients_.begin(), disabled_clients_.end(), cid);
    const bool was_enabled = (it == disabled_clients_.end());
    if (was_enabled == e) return;
    if (e) disabled_clients_.erase(it);
    else   disabled_clients_.push_back(cid);
    refresh();
}

bool EventTable::mark_once_fired(EventId id) {
    Entry* entry = find(id);
    if (!entry) return false;
    entry->once_fired = true;
    entry->enabled    = false;
    refresh();
    return true;
}

bool EventTable::set_enabled(EventId id, bool e) {
    Entry* entry = find(id);
    if (!entry) return false;
    if (entry->enabled == e) return true;   // idempotent, and no needless rebuild
    entry->enabled = e;
    refresh();
    return true;
}

// ---------------------------------------------------------------------------
// The live cache and the masks
// ---------------------------------------------------------------------------

void EventTable::refresh() {
    // GH #276 B3 — the ONE bump of the subscription revision. Here rather than
    // in each mutator because every mutator already calls this, and one that
    // did not would not work at all.
    ++revision_;
    live_kinds_        = 0;
    live_dma_kinds_    = 0;
    live_copper_kinds_ = 0;

    for (auto& e : subs_) {
        if (e.removed) { e.live = false; continue; }
        // `transient` is EXEMPT from the master switch (§4.3): Step Over and
        // Run to Here must keep working while the user has everything
        // suspended. It is NOT exempt from its own flag or its client's.
        e.live = e.enabled && (e.transient || master_) && client_enabled(e.owner);
        if (!e.live) continue;
        live_kinds_ |= kind_bit(e.kind);
        if (e.kind == EventKind::Dma)
            live_dma_kinds_ |=
                static_cast<uint8_t>(1u << static_cast<unsigned>(e.filter.dma_kind));
        if (e.kind == EventKind::Copper)
            live_copper_kinds_ |=
                static_cast<uint8_t>(1u << static_cast<unsigned>(e.filter.copper_kind));
    }
    recompute_masks_();
}

void EventTable::recompute_masks_() {
    rd_mask_       = 0;
    wr_mask_       = 0;
    any_page_set_  = false;

    for (const auto& e : subs_) {
        if (!e.live || e.kind != EventKind::Mem) continue;

        uint8_t slots = 0;
        if (!e.filter.pages.empty()) {
            // Physical-page form: a slot is armed iff the page currently mapped
            // there is in the set. This is what `set_slot_page()` re-evaluates.
            any_page_set_ = true;
            for (int s = 0; s < 8; ++s) {
                const uint16_t p = slot_page_[static_cast<size_t>(s)];
                if (std::find(e.filter.pages.begin(), e.filter.pages.end(), p) !=
                    e.filter.pages.end())
                    slots |= static_cast<uint8_t>(1u << s);
            }
        } else {
            // Range form. An inverted range (lo > hi) arms nothing — the
            // precise scan would reject every address in it, so arming its
            // slots would be a gate that can never pay off.
            //
            // THE GUARD IS BEHAVIOUR, not tidiness, and it is worth saying which
            // case needs it: when the two ends are in DIFFERENT slots the span
            // loop below is empty anyway (`first > last`), so the guard changes
            // nothing — but an inverted range INSIDE one slot (0x1FFF..0x1000)
            // has `first == last` and the loop would arm that slot. Row
            // EVT-TBL-72 is that case, and with it in place removing this guard
            // FAILS — so this is not a survivor, and B2's report listing it as one
            // was stale by the time it was written.
            if (e.filter.lo > e.filter.hi) continue;
            const int first = e.filter.lo >> 13;
            const int last  = e.filter.hi >> 13;
            for (int s = first; s <= last; ++s)
                slots |= static_cast<uint8_t>(1u << s);
            // A range that ALSO carries `page` is the AND form (DZRP's
            // `bank+1`): the page qualifier narrows the precise scan, and a
            // slot whose mapped page cannot satisfy it is not worth scanning.
            if (e.filter.page != PAGE_ANY) {
                any_page_set_ = true;
                uint8_t narrowed = 0;
                for (int s = 0; s < 8; ++s) {
                    if ((slots & (1u << s)) == 0) continue;
                    if (slot_page_[static_cast<size_t>(s)] == e.filter.page)
                        narrowed |= static_cast<uint8_t>(1u << s);
                }
                slots = narrowed;
            }
        }

        if (has_read(e.access))  rd_mask_ |= slots;
        if (has_write(e.access)) wr_mask_ |= slots;
    }
}

bool EventTable::set_slot_page(int slot, uint16_t page) {
    if (slot < 0 || slot > 7) return false;
    // THE NEXT TWO EARLY-OUTS ARE PURE COST, with no observable behaviour of
    // their own, and saying so is the point: removing either one leaves every
    // answer this function gives identical (the recompute below would produce
    // the same masks and the same `false`). They exist because this function is
    // called from `Mmu::rebuild_ptr()` — every paging write — and the recompute
    // is a scan of every armed subscription. Mutating either one away therefore
    // SURVIVES the suite by construction; no row can see a cost.
    if (slot_page_[static_cast<size_t>(slot)] == page) return false;
    slot_page_[static_cast<size_t>(slot)] = page;
    // Only a page-qualified filter can change meaning when a slot's page
    // changes; a pure logical range cannot.
    if (!any_page_set_) return false;
    const uint8_t old_rd = rd_mask_;
    const uint8_t old_wr = wr_mask_;
    recompute_masks_();
    return rd_mask_ != old_rd || wr_mask_ != old_wr;
}

void EventTable::compact() {
    ++compactions_;
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                               [](const Entry& e) { return e.removed; }),
                subs_.end());
}

// ---------------------------------------------------------------------------
// Site-side "would any live subscription match"
// ---------------------------------------------------------------------------

bool EventTable::mem_would_match(uint16_t addr, uint16_t page, Access a) const {
    for (const auto& e : subs_) {
        if (!e.live || e.kind != EventKind::Mem) continue;
        if (a == Access::Read  && !has_read(e.access))  continue;
        if (a == Access::Write && !has_write(e.access)) continue;
        if (!e.filter.pages.empty()) {
            if (std::find(e.filter.pages.begin(), e.filter.pages.end(), page) !=
                e.filter.pages.end())
                return true;
            continue;
        }
        if (addr < e.filter.lo || addr > e.filter.hi) continue;
        if (e.filter.page != PAGE_ANY && e.filter.page != page) continue;
        return true;
    }
    return false;
}

bool EventTable::port_would_match(uint16_t port, Access a) const {
    for (const auto& e : subs_) {
        if (!e.live || e.kind != EventKind::Port) continue;
        if (a == Access::Read  && !has_read(e.access))  continue;
        if (a == Access::Write && !has_write(e.access)) continue;
        if ((port & e.filter.port_mask) != e.filter.port_value) continue;
        return true;
    }
    return false;
}

bool EventTable::nr_would_match(uint8_t reg, EventSource src) const {
    for (const auto& e : subs_) {
        if (!e.live || e.kind != EventKind::NextRegWrite) continue;
        if (e.filter.source != EventSource::Any && e.filter.source != src) continue;
        if (!e.filter.regs.empty() &&
            std::find(e.filter.regs.begin(), e.filter.regs.end(), reg) ==
                e.filter.regs.end())
            continue;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// The latch ring
// ---------------------------------------------------------------------------

void EventTable::latch(const LatchEntry& e) {
    if (count_ < capacity_) {
        ring_[count_++] = e;
        return;
    }
    // §4.3's overflow contract: keep the FIRST N in order, count the rest.
    // Saturating rather than wrapping — a `dropped` of 0 must mean "nothing
    // dropped", and 65 536 drops in one boundary is a number no reader needs
    // exactly.
    if (dropped_ != 0xFFFF) ++dropped_;
}

void EventTable::shrink_ring_for_test(size_t entries) {
    if (entries < 1) entries = 1;
    if (entries > LATCH_RING_CAPACITY) entries = LATCH_RING_CAPACITY;
    capacity_ = entries;
    clear_ring();
}

// ---------------------------------------------------------------------------
// INS-17 history
// ---------------------------------------------------------------------------

void EventTable::record(const Event& ev) {
    if (history_.size() < HISTORY_CAPACITY) {
        history_.push_back(ev);
        return;
    }
    history_[history_first_] = ev;
    history_first_ = (history_first_ + 1) % HISTORY_CAPACITY;
}

std::vector<Event> EventTable::since(uint64_t seq) const {
    std::vector<Event> out;
    // Oldest first. `history_first_` is the write cursor once the buffer is
    // full, which is also the oldest entry; before that it is 0 and the vector
    // is already in order.
    const size_t n = history_.size();
    for (size_t i = 0; i < n; ++i) {
        const Event& ev = history_[(history_first_ + i) % n];
        if (ev.seq > seq) out.push_back(ev);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Matching, at the boundary
// ---------------------------------------------------------------------------

bool EventTable::filter_matches(const Entry& e, const Event& ev) const {
    if (e.kind != ev.kind) return false;

    switch (e.kind) {
        case EventKind::Execute:
            if (ev.pc < e.filter.lo || ev.pc > e.filter.hi) return false;
            if (e.filter.page != PAGE_ANY && e.filter.page != ev.phys_page)
                return false;
            return true;

        case EventKind::Mem:
            if (ev.access == Access::Read  && !has_read(e.access))  return false;
            if (ev.access == Access::Write && !has_write(e.access)) return false;
            if (e.filter.source != EventSource::Any &&
                e.filter.source != ev.source) return false;
            if (!e.filter.pages.empty()) {
                return std::find(e.filter.pages.begin(), e.filter.pages.end(),
                                 ev.phys_page) != e.filter.pages.end();
            }
            if (ev.addr < e.filter.lo || ev.addr > e.filter.hi) return false;
            if (e.filter.page != PAGE_ANY && e.filter.page != ev.phys_page)
                return false;
            return true;

        case EventKind::Port:
            if (ev.access == Access::Read  && !has_read(e.access))  return false;
            if (ev.access == Access::Write && !has_write(e.access)) return false;
            if (e.filter.source != EventSource::Any &&
                e.filter.source != ev.source) return false;
            return (ev.port & e.filter.port_mask) == e.filter.port_value;

        case EventKind::NextRegWrite:
            if (e.filter.source != EventSource::Any &&
                e.filter.source != ev.source) return false;
            if (e.filter.regs.empty()) return true;
            return std::find(e.filter.regs.begin(), e.filter.regs.end(), ev.reg) !=
                   e.filter.regs.end();

        case EventKind::Frame:
            return e.filter.frame == FRAME_EVERY || e.filter.frame == ev.frame;

        case EventKind::Scanline:
            return e.filter.scanline == ev.cvc;

        case EventKind::Cycle:
            return ev.cycle >= e.filter.cycle;

        case EventKind::Reset:
            // F8 (owner decision) — `Any` is a FILTER value and matches both
            // kinds. A delivered `Event::reset_kind` is never `Any`, and
            // `reset(Any)` is refused `Unsupported`: "either way" is not a
            // reset. This arm is the whole of the matching half.
            return e.filter.reset_kind == ResetKind::Any ||
                   e.filter.reset_kind == ev.reset_kind;

        case EventKind::IntAck:
        case EventKind::Nmi:
        case EventKind::Magic:
            // No cheap filter in §4.3's table — the seam IS the filter. A
            // client that wants only NMIs from one button, or only IM 2
            // acknowledges, says so in its `Condition`.
            return true;

        case EventKind::Host: {
            if (e.filter.host_name[0] == '\0') return true;   // every name
            for (size_t i = 0; i <= MAX_HOST_EVENT_NAME; ++i) {
                if (e.filter.host_name[i] != ev.host_name[i]) return false;
                if (e.filter.host_name[i] == '\0') break;
            }
            return true;
        }

        case EventKind::Copper:
            if (e.filter.copper_kind != ev.copper_kind) return false;
            // The Copper-PC range, and — for a MOVE — the NR set, which §4.3
            // gives this kind as "copper-PC range and/or NR set".
            if (ev.copper_pc < e.filter.lo || ev.copper_pc > e.filter.hi)
                return false;
            if (ev.copper_kind == CopperEventKind::Move && !e.filter.regs.empty())
                return std::find(e.filter.regs.begin(), e.filter.regs.end(),
                                 ev.reg) != e.filter.regs.end();
            return true;

        case EventKind::Dma:
            if (e.filter.dma_kind != ev.dma_kind) return false;
            if (ev.dma_kind != DmaEventKind::Byte) return true;
            // EITHER endpoint, as `EventFilter::lo`'s doc-comment specifies:
            // one pair cannot express two independent ranges, and "either" is
            // what makes `on dma byte 0x4000..0x5AFF` catch a transfer into the
            // screen whichever direction it runs. The default filter is the
            // whole 16-bit space, so an unqualified `Byte` subscription matches
            // every byte.
            return (ev.dma_src >= e.filter.lo && ev.dma_src <= e.filter.hi) ||
                   (ev.dma_dst >= e.filter.lo && ev.dma_dst <= e.filter.hi);

        case EventKind::Count:
            break;
    }
    return false;
}

}  // namespace dbg
}  // namespace jnext
