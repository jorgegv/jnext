#pragma once

// ---------------------------------------------------------------------------
// jnext::dbg — the event vocabulary: kinds, cheap filters, `Event`,
// `Subscription`, `Hit`.
//
// Realises §4.3 (CAP-EVT) of doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md, with
// the client/event id scalars §4.8 and §4.1 need, for work package B0 (§10.1).
//
// THREE PROPERTIES OF THIS FILE ARE THE CONTRACT, not implementation detail:
//
//  1. FILTERS ARE CHEAP, CONDITIONS ARE NOT. `EventFilter` holds only what the
//     hot path can test with an integer compare or a bitmap lookup. A
//     `Condition` is a predicate the SUBSCRIBER compiled (the DSL from its
//     `when` clause, ZRCP by token-translating ZEsarUX's dialect into the same
//     compiler); it runs ONLY at an instruction boundary, for an event that
//     already matched its filter. There is no expression language in
//     `src/debug/` and no interpreter in the hot loop.
//
//  2. NOTHING HERE RUNS INSIDE AN INSTRUCTION. Memory, port, NextREG, Copper
//     and DMA events are LATCHED at their site and DELIVERED at the next
//     instruction boundary, with the machine stopped. `Event::pc` therefore
//     names the instruction the site belongs to, not always the one about to
//     run; the per-kind notes below say where each one is ≤1 instruction late
//     and why.
//
//  3. A DELIVERY MAY BE INCOMPLETE, AND SAYS SO. The latch ring is bounded
//     (§4.3: 512 entries); on overflow the first N entries are delivered in
//     order and every `Event` of that boundary carries `overflowed` with the
//     `dropped` count. Overflow is a specified, tested behaviour, not an
//     "unreachable" defensive flag.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "debug/result.h"

namespace jnext {
namespace dbg {

class Debugger;

// ---------------------------------------------------------------------------
// Identities
// ---------------------------------------------------------------------------

/// A frontend attached through `attach()` (SES-01). Every transition and every
/// mutation is attributed to one.
using ClientId = uint32_t;

/// Not a client. Returned by nothing; passed by the backend itself when it is
/// the originator of a `log()` line or an internally generated subscription.
constexpr ClientId CLIENT_NONE = 0;

/// A subscription created by `subscribe()` (§4.3). Also the id a
/// `PauseReason::Breakpoint` / `Watch` / `RunTo` names.
using EventId = uint32_t;

/// Not a subscription.
constexpr EventId EVENT_NONE = 0;

// ---------------------------------------------------------------------------
// Kinds
// ---------------------------------------------------------------------------

/// CAP-EVT — the event kinds of the §4.3 table, in that order.
///
/// `Mem` and `Port` are ONE kind each with an `Access` mask, not separate
/// read/write kinds — the frontend designs' `MemRead`/`MemWrite` spelling is
/// sugar over this (`dsl-frontend.md` §5.1 rows 1-2, 5).
enum class EventKind : uint8_t {
    /// Pre-instruction gate: PC ∈ [lo,hi], optional page qualifier. The only
    /// kind delivered BEFORE the instruction runs, which is what lets a
    /// handler redirect PC (§4.2a).
    Execute = 0,
    /// A guest memory access at one of the eight `Mmu` sites.
    Mem,
    /// A guest port access, matched by `(port & mask) == value`.
    Port,
    /// A NextREG write, delivered after commit (see `EventFilter::source`).
    NextRegWrite,
    /// A frame boundary.
    Frame,
    /// A scanline, compared on `cvc` (the counter NR 0x1E/0x1F reads).
    Scanline,
    /// A master-cycle target; one-shot by nature.
    Cycle,
    /// A hard or soft reset happened.
    Reset,
    /// An interrupt was accepted by the CPU.
    IntAck,
    /// An NMI was accepted.
    Nmi,
    /// The magic opcode executed (CTL-14).
    Magic,
    /// `raise_host_event(name)` — a host key, `script1`..`script8`.
    Host,
    /// Copper `Move` / `Wait` / `Halt`; see `CopperEventKind`.
    Copper,
    /// DMA `Start` / `Byte` / `End`; see `DmaEventKind`.
    Dma,
};

/// Number of `EventKind` enumerators. A `static_assert`-able denominator, so a
/// new kind that forgets a mask bit or a switch arm is a build failure.
constexpr size_t EVENT_KIND_COUNT = static_cast<size_t>(EventKind::Dma) + 1;

/// CAP-EVT — sub-kind of `EventKind::Copper` (§4.3).
enum class CopperEventKind : uint8_t {
    /// A MOVE executed. Also raises `NextRegWrite{source=Copper}` — both from
    /// ONE latch entry fanned out at the drain, never two entries.
    Move = 0,
    /// A WAIT was satisfied (the Copper PC advanced past it).
    Wait,
    /// The first stall on the HALT form — the latch fires on the edge, not on
    /// every cycle of the stall.
    Halt,
};

/// CAP-EVT — sub-kind of `EventKind::Dma` (§4.3).
enum class DmaEventKind : uint8_t {
    /// One definition, one site: `phase_` enters `START_DMA` while the state
    /// is `TRANSFERRING`. An auto-restart is `End` then `Start`.
    Start = 0,
    /// One transferred byte, inside `execute_burst`. Armed only while a `Byte`
    /// subscription exists.
    Byte,
    /// Block completion.
    End,
};

/// What kind of reset (`Reset` payload, and CTL-12's argument).
enum class ResetKind : uint8_t {
    /// `Emulator::soft_reset()`.
    Soft = 0,
    /// The cold-boot reconstruct contract (CTL-12) — destroy and rebuild the
    /// machine in place through the registered loop driver.
    Hard,
};

/// Which NMI button (IN-04, and the `Nmi` payload's source).
enum class NmiButton : uint8_t {
    /// Multiface — the F9 hotkey seam, gated by NR 0x06 bit 3.
    Mf = 0,
    /// DivMMC / drive — the F10 hotkey seam, gated by NR 0x06 bit 4.
    Drive,
};

/// CAP-EVT / INS-04 — who performed the access an event reports.
///
/// `Mem` and `Port` carry `Cpu` or `Dma`, tagged at the boundary drain from the
/// slot's DMA flag. `NextRegWrite` additionally carries `Copper`. `Debugger` is
/// the source INS-04 gives a `nextreg_write` from the debugger itself — it
/// NEVER appears in a delivered `Event`, because a debugger write fires no
/// event at all (§4.2a); it exists so the NR write hook can name its caller.
/// `Any` is a filter value only.
enum class EventSource : uint8_t { Cpu = 0, Copper, Dma, Debugger, Any };

/// CAP-EVT — memory / port access direction. Used BOTH as a single value in a
/// payload and as a mask in a subscription (§4.3 "`access` ⊆ {Read, Write}").
enum class Access : uint8_t {
    None      = 0,
    Read      = 1,
    Write     = 2,
    ReadWrite = 3,
};

constexpr Access operator|(Access a, Access b) {
    return static_cast<Access>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr bool has_read(Access a) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(Access::Read)) != 0;
}
constexpr bool has_write(Access a) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(Access::Write)) != 0;
}

/// CAP-EVT / SES-02 — a set of `EventKind`s, one bit per kind. What
/// `SubscriptionsChanged` carries
/// (SES-02) — the successor of `BreakpointChange` (`src/debug/breakpoints.h`),
/// which could only say "PC breakpoints" or "watchpoints".
using EventKindMask = uint32_t;

constexpr EventKindMask kind_bit(EventKind k) {
    return static_cast<EventKindMask>(1u) << static_cast<unsigned>(k);
}

// ---------------------------------------------------------------------------
// Event
// ---------------------------------------------------------------------------

/// Longest host-event name, excluding the NUL. `raise_host_event()` refuses a
/// longer one with `Result::Unsupported` rather than truncating it.
constexpr size_t MAX_HOST_EVENT_NAME = 23;

/// CAP-EVT — one delivered event: the common header of §4.3 plus every per-kind
/// payload.
///
/// ONE FLAT STRUCT, deliberately. A `std::variant` would make the DSL's
/// payload-name lookup (`ADDR VALUE PREV PAGE PORT REG SOURCE …`, compiled once
/// per rule) a visit per delivery, and would cost `Event` its trivial
/// copyability — which `matched[]`, `events_fired_since()` and the protocol
/// adapters all rely on. `kind` says which fields mean anything; each field
/// below names the kinds that fill it, and every other kind leaves it
/// value-initialised.
///
/// `host_name` is a fixed buffer rather than a pointer so an `Event` copied out
/// of a delivery (a test, a `matched[]` list, a queued notification) stays
/// valid with no lifetime rule to get wrong.
struct Event {
    // ── common (§4.3 "the common {cycle, frame, vc, hc, pc, id, owner}") ──

    EventKind kind = EventKind::Execute;

    /// Copper sub-kind; meaningful iff `kind == Copper`.
    CopperEventKind copper_kind = CopperEventKind::Move;
    /// DMA sub-kind; meaningful iff `kind == Dma`.
    DmaEventKind dma_kind = DmaEventKind::Start;

    /// Master cycle. For a latched kind this is the cycle captured AT THE SITE,
    /// not the cycle of the boundary that delivered it.
    uint64_t cycle = 0;
    /// Monotonic delivery sequence number, the cursor `events_fired_since()`
    /// takes (INS-17). Unique and increasing across every kind and client.
    uint64_t seq = 0;
    /// Frame tag, the pre-increment number `time().frame` reports (INS-07).
    uint32_t frame = 0;
    /// VHDL `vc` — raw frame line counter at the site.
    int16_t vc = 0;
    /// VHDL `hc` — raw frame pixel counter at the site.
    int16_t hc = 0;
    /// The instruction this event belongs to. For `Mem` it is `pc_pre_exec`;
    /// for a CPU `NextRegWrite` it names the WRITER even though the delivery is
    /// one instruction later.
    uint16_t pc = 0;
    /// The subscription that matched.
    EventId id = EVENT_NONE;
    /// That subscription's owning client.
    ClientId owner = CLIENT_NONE;

    /// True iff the latch ring overflowed at the boundary that delivered this
    /// event — some events of this boundary were dropped (§4.3).
    bool overflowed = false;
    /// How many entries that boundary dropped. Zero unless `overflowed`.
    uint16_t dropped = 0;

    // ── Mem / Port ───────────────────────────────────────────────────────

    /// `Mem`: the logical address. `Port`: unused (see `port`).
    uint16_t addr = 0;
    /// `Mem`: the physical 8 K page behind `addr` at the moment of the access.
    uint16_t phys_page = 0;
    /// `Port`: the full 16-bit port. Masking is the filter's job, not the
    /// payload's — the payload always carries what the guest actually put on
    /// the bus.
    uint16_t port = 0;
    /// `Mem`, `Port`, `NextRegWrite`, `Dma{Byte}`: the byte. For a `Port` read
    /// this is the value RETURNED (latched after dispatch).
    uint8_t value = 0;
    /// `Mem{Write}` and `NextRegWrite`: the byte that was there BEFORE — one
    /// peek at the latch site, so a handler can undo a caught write.
    uint8_t prev = 0;
    /// `Mem`, `Port`: which direction this single access was.
    Access access = Access::None;
    /// `Mem`, `Port`, `NextRegWrite`: who did it.
    EventSource source = EventSource::Cpu;

    // ── NextRegWrite ─────────────────────────────────────────────────────

    /// The NextREG number written. Shared with the Copper `Move` payload,
    /// which is the same write seen from the Copper side.
    uint8_t reg = 0;

    // ── Reset / IntAck / Nmi ─────────────────────────────────────────────

    /// `Reset`: which kind.
    ResetKind reset_kind = ResetKind::Soft;
    /// `IntAck`: the vector byte the CPU took.
    uint8_t int_vector = 0;
    /// `IntAck`: the interrupt mode in force (0, 1 or 2).
    uint8_t int_mode = 0;
    /// `Nmi`: which button/source raised it.
    NmiButton nmi_source = NmiButton::Mf;

    // ── Host ─────────────────────────────────────────────────────────────

    /// `Host`: the raised name, NUL-terminated. `script1`..`script8` for the
    /// eight script keys; any other name a frontend raises.
    char host_name[MAX_HOST_EVENT_NAME + 1] = {};

    // ── Copper ───────────────────────────────────────────────────────────

    /// `Copper`: the Copper program counter at the site (0..1023).
    uint16_t copper_pc = 0;
    /// `Copper{Wait}`: the WAIT's target line.
    uint16_t wait_vpos = 0;
    /// `Copper{Wait}`: the WAIT's horizontal threshold, as `copper.vhd`
    /// computes it — `(hpos << 3) + 12`.
    uint16_t wait_hpos_threshold = 0;
    /// `Copper`: VHDL `hc_ula`, the 7 MHz counter the Copper itself compares
    /// against (NOT the raw `hc` above — confusing the two was GH #181).
    int16_t hc_ula = 0;
    /// `Copper`: VHDL `o_vc_cu`, the line counter the Copper compares against.
    int16_t cvc = 0;

    // ── Dma ──────────────────────────────────────────────────────────────

    /// `Dma{Start,End}`: block source address. `Dma{Byte}`: this byte's source.
    uint16_t dma_src = 0;
    /// `Dma{Start,End}`: block destination. `Dma{Byte}`: this byte's
    /// destination.
    uint16_t dma_dst = 0;
    /// `Dma{Start,End}`: the programmed block length.
    uint16_t dma_length = 0;
    /// `Dma{End}`: bytes actually transferred.
    uint32_t dma_bytes = 0;
    /// `Dma{Start,End}`: the transfer's direction/mode bytes, as the DMA
    /// registers hold them.
    uint8_t dma_direction = 0;
    uint8_t dma_mode      = 0;
    /// `Dma{Byte}`: this byte came from / went to an I/O port rather than
    /// memory.
    bool dma_is_io_src = false;
    bool dma_is_io_dst = false;
};

// ---------------------------------------------------------------------------
// Filters
// ---------------------------------------------------------------------------

/// A page number that means "no page qualifier". The valid NR 0x50-0x57 page
/// numbers are 0..223, so 0xFFFF cannot collide with one.
constexpr uint16_t PAGE_ANY = 0xFFFF;

/// `EventFilter::frame` value that means "every frame" rather than one frame
/// number. Frame numbers are `uint32_t` and monotonic, so the top value is
/// unreachable in any run this emulator can perform.
constexpr uint32_t FRAME_EVERY = 0xFFFFFFFFu;

/// CAP-EVT — the cheap, hot-path-testable half of a subscription (§4.3, the "Cheap filter
/// (where)" column). Which members matter depends on the kind; the rest are
/// ignored. Nothing here allocates on the hot path and nothing here is
/// evaluated by an interpreter.
struct EventFilter {
    /// `Execute`: PC range, inclusive both ends. `Mem`: logical address range,
    /// inclusive. `Copper`: Copper-PC range. `Dma{Byte}`: source/destination
    /// range. A single address is `lo == hi`.
    uint16_t lo = 0;
    uint16_t hi = 0xFFFF;

    /// Optional physical-page qualifier, AND-ed with the range once the range
    /// has matched (`Execute`: the effective page at slot(PC); `Mem`: the page
    /// behind the address — DZRP's `bank+1` watchpoints). `PAGE_ANY` = none.
    uint16_t page = PAGE_ANY;

    /// `Mem`: match on the physical page INSTEAD of a logical range, for any
    /// page in this set. Empty = use the range above. A `Mem` filter is one or
    /// the other; a range that also carries `page` is the AND form.
    std::vector<uint16_t> pages;

    /// `Port`: `(port & port_mask) == port_value`. GH #222's low-byte rule is
    /// `port_mask = 0x00FF`; a full 16-bit decode is `port_mask = 0xFFFF`.
    uint16_t port_mask  = 0xFFFF;
    uint16_t port_value = 0;

    /// `NextRegWrite`, and `Copper` as its NR-set filter: the register numbers
    /// to match. Empty = every register.
    std::vector<uint8_t> regs;

    /// `Mem`, `Port`, `NextRegWrite`: restrict to one originator. `Any` = no
    /// restriction.
    EventSource source = EventSource::Any;

    /// `Frame`: the frame number to match, or `FRAME_EVERY` for every frame.
    uint32_t frame = FRAME_EVERY;

    /// `Scanline`: the `cvc` line to match.
    int16_t scanline = 0;

    /// `Cycle`: fire at the first boundary with `master_cycle >= cycle`.
    uint64_t cycle = 0;

    /// `Reset`: which kind to match. Both kinds need two subscriptions; there
    /// is no "any reset" value, because every caller so far wants one or the
    /// other (`Reset{Hard}` completes a blocked `run`, CTL-12).
    ResetKind reset_kind = ResetKind::Soft;

    /// `Copper`: which sub-kind. `Dma`: which sub-kind. One subscription is one
    /// sub-kind — `Byte` arming is a per-engine cost (§4.3) and must be
    /// separable from `Start`/`End`.
    CopperEventKind copper_kind = CopperEventKind::Move;
    DmaEventKind    dma_kind    = DmaEventKind::Start;

    /// `Host`: the name to match, NUL-terminated. Empty matches every name.
    char host_name[MAX_HOST_EVENT_NAME + 1] = {};
};

// ---------------------------------------------------------------------------
// Subscriptions
// ---------------------------------------------------------------------------

/// CAP-EVT — what the backend does when a subscription matches and its
/// condition passes.
enum class Action : uint8_t {
    /// Pause at the boundary, with `pause_reason` set and the `Paused` push
    /// carrying `matched[]`.
    Stop = 0,
    /// Emit an SES-06 line and keep running.
    Log,
    /// Neither — the delivery itself (the handler) was the point.
    Continue,
};

/// CAP-EVT — a predicate the SUBSCRIBER compiled. Runs only at an instruction boundary,
/// only for an event that already matched its `EventFilter`, and only when
/// `!replay_mode_`. Takes the `Debugger` by const reference: a condition
/// OBSERVES — it may read the whole inspection surface, and nothing else.
using Condition = std::function<bool(const Event&, const Debugger&)>;

/// CAP-EVT / §4.2a — the subscriber's body for a delivery — the DSL's rule body, a Qt panel's
/// hook. Runs with the machine stopped at an instruction boundary and the whole
/// body under one `InspectionScope`, so its reads and its mutations alike are
/// invisible to the event machinery (§4.2a). Takes the `Debugger` by
/// non-const reference because §4.2a makes mutation a first-class capability
/// from a handler; it may NOT issue control verbs (§5, "a script observes, it
/// does not drive").
///
/// The returned `Action` is the handler's verdict and OVERRIDES the
/// subscription's static `action` (§4.3). A subscription with no handler simply
/// uses its static action.
using Handler = std::function<Action(const Event&, Debugger&)>;

/// CAP-EVT — what `subscribe()` takes (§4.3: "`{kind, filter, access, condition?, once,
/// transient, action, enabled, owner}`", plus the `handler` §4.2a's contract
/// requires and the sub-kinds the Copper/DMA rows need).
/// (`owner` is not a field: it is the `ClientId by` of the `subscribe()` call
/// that created this subscription, and is reported back by `SubscriptionInfo`.)
struct Subscription {
    EventKind   kind   = EventKind::Execute;
    EventFilter filter;

    /// `Mem`, `Port`: which directions to watch. A mask here, unlike the single
    /// value in the delivered `Event`.
    Access access = Access::ReadWrite;

    /// Optional; empty = always true.
    Condition condition;

    /// Optional; empty = use `action` unchanged.
    Handler handler;

    /// Disable after the first ACCEPTED firing (filter matched, condition
    /// passed). Not "after the first match".
    bool once = false;

    /// Exempt from the master switch, auto-removed at the next stop, hidden
    /// from `subscriptions(false)`, unlimited in number. Step Over, Run to Here
    /// and DeZog's two temp breakpoints per `CMD_CONTINUE` are all this,
    /// replacing today's single one-shot (`BreakpointSet::set_oneshot`).
    bool transient = false;

    /// The static verdict, which a `handler` may override.
    Action action = Action::Stop;

    /// The subscription's own enable flag, independent of the master switch and
    /// of the per-client switch. Live = master ∧ client ∧ this.
    bool enabled = true;
};

/// INS-17 — what `subscriptions()` lists: the model, including disabled and
/// (optionally) transient entries, with the owner.
///
/// Deliberately NOT `Subscription`: the `Condition` and `Handler` closures are
/// meaningless to a lister and unsafe to hand out (they capture the
/// subscriber's interpreter state), so the model reports only WHETHER each is
/// present.
struct SubscriptionInfo {
    EventId     id = EVENT_NONE;
    EventKind   kind = EventKind::Execute;
    EventFilter filter;
    Access      access = Access::ReadWrite;
    bool        has_condition = false;
    bool        has_handler   = false;
    bool        once      = false;
    bool        transient = false;
    Action      action  = Action::Stop;
    /// This subscription's own flag, as set. NOT the live value — a lister
    /// draws the user's checkbox, which must survive a master-switch round
    /// trip untouched (the GH #225 rule `BreakpointSet` already follows).
    bool        enabled = true;
    /// Whether it can fire right now: `enabled` ∧ master ∧ its client's switch.
    bool        live = true;
    ClientId    owner = CLIENT_NONE;
};

/// CAP-EVT / SES-02 — one subscription that matched at a stop
/// (§4.3 `Paused{matched[]}`).
///
/// EVERY subscription that matched is listed, transient ones included — "temp
/// beats user" is adapter policy over this list, not a backend rule.
struct Hit {
    EventId  event_id = EVENT_NONE;
    /// The address (`Mem`, `Execute`) or port (`Port`) that matched.
    uint16_t addr = 0;
    Access   access = Access::None;
    uint8_t  value  = 0;
};

}  // namespace dbg
}  // namespace jnext
