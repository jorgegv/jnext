// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — construction, the mutation log, symbols (CAP-SYM),
// `state()` (CTL-13) and the state/rewind verbs (CAP-ST).
//
// Work package B1 of epic #276 (doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md
// §10.1: "B1 facade + control + inspection over the existing primitives, no
// hot-path change"). The control verbs are in `debugger_control.cpp`, the
// inspection surface in `debugger_inspect.cpp`, and everything a LATER
// sub-package owns is in `debugger_pending.cpp` — one file, so what is not yet
// implemented is countable rather than scattered.
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

#include <cinttypes>
#include <cstdio>
#include <utility>

#include "core/log.h"
#include "core/saveable.h"
#include "debug/rewind_buffer.h"

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Debugger::Debugger(Emulator& emu) : impl_(new Impl(emu, *this)) {
    // GH #276 B2 — publish the event machinery to the hot path. From here on the
    // eight `Mmu` sites, the port sites, `NextReg::write`, `Copper::execute` and
    // `Dma::execute_burst` can all reach the subscription table through the
    // `DebugState*` they already hold, and the hot loop can reach the drain and
    // the pre-instruction gate through these two `std::function`s.
    //
    // NOTHING IS ARMED YET: with no subscription the slot masks are zero, the
    // per-engine flags are false and `events_pending()` is false, so
    // constructing a `Debugger` does not by itself put a single test back on the
    // hot path that was not there before.
    impl_->ds().set_event_table(&impl_->events);
    impl_->ds().set_event_hooks([this]() { return impl_->drain_boundary(); },
                                [this](uint16_t pc) { return impl_->execute_gate(pc); });

    // §6.1 — SEED THE EIGHT LIVE PAGES. `DebugState::on_slot_remapped()`
    // early-returns while the table is null, so every `rebuild_ptr()` during
    // `Emulator::init()` was discarded before this object existed. B2 shipped
    // without this and `EventTable::slot_page_` stayed all zeros, which makes a
    // page-qualified `Mem` filter wrong in BOTH directions: a filter naming page
    // 0x00 arms all eight slots, and one naming the page actually mapped arms
    // none. DZRP's `bank+1` watchpoints (REQ-dzrp-11) are built on this.
    for (int s = 0; s < 8; ++s)
        impl_->ds().on_slot_remapped(s, impl_->emu.mmu().get_effective_page(s));

    impl_->gates_changed();
}

// Out of line, and it must be: `Impl` is incomplete in the published header, so
// `unique_ptr`'s deleter can only be instantiated here. B0 declared it this way
// already.
//
// GH #276 B2 — and it now has a BODY, for a reason that is not tidiness: the
// `DebugState` outlives this object (it is an `Emulator` member), and it holds a
// pointer to `Impl::events` plus two `std::function`s that capture `this`. Left
// installed, the next guest memory write past a stale slot mask would be a
// use-after-free on the hot path.
Debugger::~Debugger() {
    impl_->ds().set_event_table(nullptr);
    impl_->ds().set_event_hooks(nullptr, nullptr);
    impl_->ds().set_latch_stamper(nullptr);
    // The masks are `BreakpointSet`'s, so they survive this object, and the event
    // half has to be zeroed or a retired subscription keeps the gate open for
    // ever. `refresh_event_gates()` DOES that — the table pointer was retired two
    // lines up, so it takes its `!events_` branch and publishes (0, 0, false).
    // An explicit `set_event_slot_masks(0, 0, false)` stood here and was removed:
    // no mutation could distinguish it from nothing, which is what "redundant"
    // means, and a second writer of the same two bytes is how the two come to
    // disagree. The ORDER is therefore load-bearing.
    impl_->ds().refresh_event_gates();
    impl_->emu.copper().set_events_armed(false, false, false);
    impl_->emu.dma().set_events_armed(false, false, false);
}

// ---------------------------------------------------------------------------
// SES-06 — the message sink, and the §4.2a mutation log over it
// ---------------------------------------------------------------------------
//
// B1 routes SES-06 to the `emulator` spdlog channel. It is NOT its own channel
// yet on purpose: a new `--log-level` name is a documented user surface, gated
// in both directions by `log_test` LOG-09..11 against the man page's LOGGING
// list, and it belongs with B3's listener fan-out (where `on_log()` gives the
// line somewhere else to go) rather than bundled into the write verbs.
// ---------------------------------------------------------------------------

Result Debugger::log(ClientId by, LogLevel level, const std::string& text) {
    auto& l = *Log::emulator();
    // `by` is carried in the line rather than as spdlog metadata: a client id is
    // not a logger name, and every consumer of these lines (a CI transcript, a
    // script's own log) wants the two together.
    const std::string line =
        by == CLIENT_NONE ? text : (text + " [client " + std::to_string(by) + "]");
    switch (level) {
        case LogLevel::Trace: l.trace("{}", line);    break;
        case LogLevel::Debug: l.debug("{}", line);    break;
        case LogLevel::Info:  l.info("{}", line);     break;
        case LogLevel::Warn:  l.warn("{}", line);     break;
        case LogLevel::Error: l.error("{}", line);    break;
    }
    return Result::Ok;
}

// §4.2a — "every mutation from every client emits one SES-06 info line
// `MUTATE <what> <old> -> <new> by <client>`", and the BACKEND emits it, not the
// writer. Both helpers below are that one line; every write verb calls one of
// them, so there is no path by which a mutation is unlogged.
void Debugger::Impl::log_mutate(ClientId by, const std::string& what,
                                long long old_value, long long new_value) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "MUTATE %s 0x%llX -> 0x%llX by %u",
                  what.c_str(), static_cast<unsigned long long>(old_value),
                  static_cast<unsigned long long>(new_value),
                  static_cast<unsigned>(by));
    Log::emulator()->info("{}", buf);
}

// A BLOCK write has no single "old -> new" to report, and inventing one (the
// first byte's) would read as a claim about the whole range. §4.2a's format is
// for a single value; a range says what it actually did instead.
void Debugger::Impl::log_mutate_range(ClientId by, const std::string& what,
                                      const std::string& detail) {
    Log::emulator()->info("MUTATE {} {} by {}", what, detail,
                          static_cast<unsigned>(by));
}

// ---------------------------------------------------------------------------
// SES-04 — stop policy
//
// Plain backend state, with no session behind it: the loop owner sets it once
// and every `Stop` action consults it. B3 adds the `peer_connected()` override
// (§4.8 SES-04: `ExitNonZero` becomes `Pause` while a remote is attached),
// which needs the service list it introduces.
// ---------------------------------------------------------------------------

StopPolicy Debugger::stop_policy() const { return impl_->stop_policy; }

Result Debugger::set_stop_policy(StopPolicy policy) {
    impl_->stop_policy = policy;
    return Result::Ok;
}

// ---------------------------------------------------------------------------
// §4.7 — CAP-SYM, symbols
//
// THE one table. `SymbolTable` itself already lived in `src/debug/`; what moves
// here is OWNERSHIP of the instance, out of `DebuggerManager` (a `Q_OBJECT`).
// The Qt member cannot be deleted until the Qt frontend holds a `Debugger` —
// B3's loop-owner wiring and Q's WP2/WP6 — so for now a Qt-loaded MAP and a
// backend-loaded MAP are two tables. That is stated in the B1 report as the one
// item of this package that cannot complete alone.
// ---------------------------------------------------------------------------

Expected<int> Debugger::load_map(const std::string& path, MapFormat format) {
    const int n = (format == MapFormat::Z88dk) ? impl_->symbols.load_z88dk_map(path)
                                               : impl_->symbols.load_simple_map(path);
    // Both loaders return -1 on a file that cannot be read or parsed. That is
    // not a refusal of the verb by the machine's state, and it is not "there is
    // no such thing" either — the caller asked for a file that is not there.
    if (n < 0) return make_refused<int>(Result::RefusedUnavailable);
    return make_ok<int>(n);
}

Result Debugger::clear_symbols() {
    impl_->symbols.clear();
    return Result::Ok;
}

std::optional<std::string> Debugger::lookup(uint16_t addr) const {
    return impl_->symbols.lookup(addr);
}

std::optional<uint16_t> Debugger::lookup_name(const std::string& name) const {
    return impl_->symbols.lookup_name(name);
}

const SymbolTable& Debugger::symbols() const { return impl_->symbols; }

// ---------------------------------------------------------------------------
// CTL-13 — `state()`
//
// WHICH PAUSE REASONS B1 CAN PRODUCE, and which it cannot. This is a real
// partial, not an oversight, and the reason is that the tree destroys the
// evidence as it stops:
//
//   None, User, Step, RunTo, Breakpoint   — produced here.
//
//   Watch    NOT produced. The hot loop consumes the data-breakpoint latch in
//            the same breath as the pause (`pause(); set_data_bp_hit(false);`,
//            emulator.cpp), so by the time any backend call can look, the
//            address and the direction are gone. B2's `Mem` latch records them.
//   Magic    NOT produced. The magic hook pauses from inside the CPU callback
//            (emulator.cpp) and leaves no mark a later read can find. B2's
//            `Magic` event.
//   Corrupt  NOT produced. CTL-11 latches the INCIDENT, but nothing pauses for
//            it: the gate refuses the resume instead. B3 wires the pause.
//   Script   NOT produced. There is no script engine (package S).
//
// A stop this cannot explain reports `User{CLIENT_NONE}` — "the machine was
// stopped and it was not a verb of mine", which is true. Deliberately NOT a
// fabricated `Breakpoint` (there may be no breakpoint), and deliberately not
// `None` (which would contradict `paused`). `CLIENT_NONE` also makes it safe
// under SES-01's detach rule: no client's departure resumes a stop nobody owns,
// which is exactly the property §4.1 demands for `Magic` and `Corrupt`.
// ---------------------------------------------------------------------------

RunState Debugger::state() const {
    const DebugState& ds = impl_->ds();
    const Z80Registers regs = impl_->emu.cpu().get_registers();

    RunState st;
    st.paused = ds.paused();
    st.cycle  = impl_->emu.clock().get();
    st.frame  = frame_tag(impl_->emu);
    st.pc     = regs.PC;

    // The published mirror of the internal enum. `debug_types_check.cpp` pins
    // the two value by value, which is what makes this switch a relabelling
    // rather than a mapping that could be wrong.
    switch (ds.step_mode()) {
        case ::StepMode::NONE:              st.step_mode = StepMode::None;           break;
        case ::StepMode::INTO:              st.step_mode = StepMode::Into;           break;
        case ::StepMode::OVER:              st.step_mode = StepMode::Over;           break;
        case ::StepMode::OUT:               st.step_mode = StepMode::Out;            break;
        case ::StepMode::RUN_TO_CYCLE:      st.step_mode = StepMode::RunToCycle;     break;
        case ::StepMode::STEP_BACK:         st.step_mode = StepMode::StepBack;       break;
        case ::StepMode::RUN_BACK_TO_CYCLE: st.step_mode = StepMode::RunBackToCycle; break;
    }

    if (!st.paused) {
        st.pause_reason.kind = PauseReason::Kind::None;
        return st;
    }

    // ── THE PRECEDENCE, IN ORDER (GH #276 B2 extended it) ────────────────
    //
    //  0. `Corrupt`   — an unacknowledged failed rewind or state load.
    //  1. the ARMED verb — `User`, `Step`, or `RunTo` at its target.
    //  2. the EVENT-STOP latch — a subscription's `Stop`, which knows its own
    //     id, addr and access.
    //  3. `Magic`     — the magic-opcode latch.
    //  4. `Watch`     — the legacy watchpoint latch.
    //  5. `Breakpoint`— a legacy PC breakpoint at PC.
    //  6. `User`, unowned — the machine is paused and nothing explains it.
    //
    // CORRUPT IS FIRST, ahead even of the armed verb, and that is deliberate:
    // CTL-11 makes an unacknowledged corruption the thing that REFUSES every
    // resume, so it is what the user has to be told. A failed `step_back` arms
    // `Step` and would otherwise report a step that did not happen.
    if (impl_->guard.needs_confirmation(!impl_->emu.last_state_error().empty(),
                                        impl_->emu.state_error_generation())) {
        st.pause_reason.kind = PauseReason::Kind::Corrupt;
        // UNOWNED (Revision 6): the corruption is the machine's, not any
        // client's verb, so no client's detach may resume it.
        st.pause_reason.by = CLIENT_NONE;
        return st;
    }

    // The machine is stopped. If the last control verb was one whose completion
    // IS the stop, that verb is the reason: it armed it and nothing has resumed
    // since.
    switch (impl_->armed_reason) {
        case PauseReason::Kind::User:
        case PauseReason::Kind::Step:
            st.pause_reason.kind = impl_->armed_reason;
            st.pause_reason.by   = impl_->armed_by;
            return st;
        case PauseReason::Kind::RunTo:
            // A run-to stops at its target OR earlier, on a breakpoint the run
            // crossed. Which one it was is readable: the target address.
            if (!impl_->has_target || impl_->armed_target == st.pc) {
                st.pause_reason.kind = PauseReason::Kind::RunTo;
                st.pause_reason.by   = impl_->armed_by;
                st.pause_reason.addr = impl_->armed_target;
                return st;
            }
            break;
        default:
            break;
    }

    // Not the verb, so the machine stopped itself.
    //
    // A SUBSCRIPTION's `Stop` is the most specific answer available: it carries
    // the id, the address and the access, none of which any other source has.
    // The latch was written by the drain or the pre-instruction gate at the
    // moment of the stop, and cleared by the next control verb (`Impl::arm`).
    // The generation test is what makes this latch describe THIS stop.
    // `Impl::arm()` clears it, but the Qt panels still drive `DebugState`
    // directly until package Q, so a Qt-driven Run never reaches a backend verb
    // and the latch would explain the next, unrelated stop.
    if (impl_->event_stop_latched &&
        impl_->event_stop_gen == ds.resume_generation()) {
        st.pause_reason = impl_->event_stop;
        return st;
    }

    // The magic opcode (CTL-14). Latched unconditionally by the hook, NOT
    // behind a `Magic` subscription, because §4.2's closed set has a value for
    // it whether or not anyone subscribed. UNOWNED.
    if (ds.magic_stop()) {
        st.pause_reason.kind = PauseReason::Kind::Magic;
        st.pause_reason.by   = CLIENT_NONE;
        st.pause_reason.addr = ds.magic_stop_pc();
        return st;
    }

    // A legacy `BreakpointSet` watchpoint — the Qt panels' model until package
    // Q. `data_bp_hit_` is already gone by the time anything can ask (the hot
    // loop consumes it in the same breath as the pause), which is what
    // `DebugState::note_watch_stop()` exists to survive.
    if (ds.watch_stop()) {
        st.pause_reason.kind   = PauseReason::Kind::Watch;
        st.pause_reason.addr   = ds.watch_stop_addr();
        st.pause_reason.access = ds.watch_stop_is_write() ? Access::Write
                                                         : Access::Read;
        return st;
    }

    // A user breakpoint at PC.
    if (ds.breakpoints().has_pc(st.pc)) {
        st.pause_reason.kind = PauseReason::Kind::Breakpoint;
        st.pause_reason.addr = st.pc;
        return st;
    }

    st.pause_reason.kind = PauseReason::Kind::User;
    st.pause_reason.by   = CLIENT_NONE;
    return st;
}

// ---------------------------------------------------------------------------
// §4.6 — CAP-ST, state and rewind
// ---------------------------------------------------------------------------

bool Debugger::at_frame_boundary() const {
    return !impl_->emu.frame_in_progress();
}

// ST-01. The frame boundary is not negotiable — only who waits for it is.
Expected<std::vector<uint8_t>> Debugger::save_state_bytes(ClientId by,
                                                          SaveStateMode mode) {
    if (impl_->emu.frame_in_progress()) {
        if (mode == SaveStateMode::RefuseMidFrame)
            return make_refused<std::vector<uint8_t>>(Result::NotAtFrameBoundary);
        // GH #27 S6's advance: run the half-executed frame out with the
        // debugger suspended, so a pending Run to Here or step survives it.
        DebugState::SuspendScope suspend(impl_->ds());
        impl_->emu.advance_to_frame_boundary();
        // The SES-06 line for this advance is B4's, with the rest of CAP-ST —
        // the `by` is carried for it and is meaningful only on this path
        // (`RefuseMidFrame` advances nothing). B1 records who asked, and does
        // not pretend to log it.
        (void)by;
    }

    // Measure, then write: the same two-pass shape RewindBuffer uses, so a
    // snapshot is exactly as long as the machine says it is.
    StateWriter measure;
    impl_->emu.save_state(measure);
    std::vector<uint8_t> bytes(measure.position());

    StateWriter w(bytes.data(), bytes.size());
    impl_->emu.save_state(w);
    if (w.overflow() || w.position() != bytes.size())
        return make_refused<std::vector<uint8_t>>(Result::RefusedUnavailable);

    return make_ok(std::move(bytes));
}

// ST-02. A failure LATCHES corruption, which CTL-11 then gates resumption on —
// the Emulator does that latching itself (`last_state_error()` +
// `state_error_generation()`), which is why this verb only has to report it.
Result Debugger::load_state_bytes(ClientId by, const uint8_t* data, size_t n) {
    if (!data || n == 0) return Result::RefusedUnavailable;
    if (impl_->emu.rzx_recorder().is_recording() ||
        impl_->emu.rzx_player().is_playing())
        return Result::RefusedRzx;

    StateReader r(data, n);
    const bool ok_load = impl_->emu.load_state(r);
    impl_->log_mutate_range(by, "state", ok_load ? "loaded" : "load FAILED (machine corrupt)");
    return ok_load ? Result::Ok : Result::RefusedCorrupt;
}

bool Debugger::rewind_enabled() const { return impl_->emu.rewind_enabled(); }

Result Debugger::set_rewind_enabled(bool enabled) {
    impl_->emu.set_rewind_enabled(enabled);
    return Result::Ok;
}

RewindRange Debugger::rewind_range() const {
    RewindRange rr;
    const RewindBuffer* rb = impl_->emu.rewind_buffer();
    if (!rb) return rr;
    rr.depth          = rb->depth();
    rr.capacity       = rb->capacity();
    rr.snapshot_bytes = rb->snapshot_bytes();
    if (rb->empty()) return rr;
    rr.oldest_cycle = rb->oldest_frame_cycle();
    rr.newest_cycle = rb->newest_frame_cycle();
    rr.oldest_frame = rb->oldest_frame_num();
    rr.newest_frame = rb->newest_frame_num();
    return rr;
}

// ST-03 — the same `Result` the verb would return, for PRE-CLICK greying, or
// empty if it would succeed. One predicate, used by both this and the two rewind
// verbs (see `Impl::rewind_refusal()` in debugger_control.cpp), so a greyed control
// and a refused verb can never disagree.
std::optional<Result> Debugger::rewind_blocked() const {
    const Result r = impl_->rewind_refusal();
    if (r == Result::Ok) return std::nullopt;
    return r;
}

Result Debugger::resize_rewind_buffer(size_t frames) {
    if (frames == 0) return Result::RefusedUnavailable;
    impl_->emu.resize_rewind_buffer(static_cast<int>(frames));
    return Result::Ok;
}

}  // namespace dbg
}  // namespace jnext
