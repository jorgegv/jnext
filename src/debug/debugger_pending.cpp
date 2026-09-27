// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — EVERY verb B1 does not implement, in ONE file.
//
// Work package B1 of epic #276 leaves the four later sub-packages' machinery to
// them (§10.1: B2 the `EventTable` and the hooks, B3 the session, B4 input
// pulses / capture / bookmarks / coverage, B5 the suite). The published header
// declares all of it, so every declaration needs a definition or a frontend gets
// a link error instead of an answer — and a refusal a caller can test is the
// whole point of `Result` (§4: "never a silent no-op").
//
// WHY ONE FILE. "What is not implemented yet" is then a thing you can COUNT
// (`grep -c '^Result Debugger::'` and its siblings) rather than a claim in a
// comment somewhere. A B2/B3/B4 author deletes its own section as it goes, and
// when this file is empty the package is done.
//
// WHY `Result::Unsupported` and not something softer: §4 defines it as "the
// backend does not implement this", which is exactly true here. It is NOT
// `RefusedUnavailable` — that is the BENIGN "the thing you asked for is not
// there right now", and a client must be able to tell "no bookmark by that
// name" from "this build has no bookmarks at all".
//
// THE DIRECT-VALUE QUERIES CANNOT REFUSE, so each returns the neutral value its
// §4 row documents for the empty case — an empty list, `false`, a zero bit set —
// and says so at the definition. None of them fabricates a plausible answer.
//
// ── WHAT IS HERE, BY OWNING PACKAGE ─────────────────────────────────────────
//
//   B2 — events (§4.3 CAP-EVT + INS-17): 11 definitions
//        subscribe, unsubscribe, set_enabled, master_enabled,
//        set_master_enabled, client_enabled, set_client_enabled,
//        probe_execute, raise_host_event, subscriptions, events_fired_since
//
//   B3 — session (§4.8 CAP-SES): 11 definitions
//        attach, detach, set_listener, add_service, remove_service, pump,
//        set_live_raster, live_raster, attached, set_loop_driver,
//        on_cold_boot_done
//
//   B4 — input pulses, capture, bookmarks, coverage: 12 definitions
//        press_key (name), press_key (matrix), screenshot, ula_screen_dump,
//        bookmark_save, bookmark_restore, bookmarks, save_snapshot,
//        coverage_enable, coverage_enabled, coverage_clear, coverage
//
//   UNASSIGNED — 1 definition
//        render_layer (INS-14). Not B2's, B3's or B4's by §10.1, and not "over
//        an existing primitive" either: the eight views exist only inside
//        `VideoLayerView` in a `Q_OBJECT` header, and INS-14 requires
//        `render_to_image` + the `replay_*` calls to be MOVED out of
//        `video_panel.cpp` into a Qt-free function. §11 item 5 assigns the
//        validation of that move to Q WP4d ("the 106 DVP rows against the moved
//        function before the widget changes"), but the move itself has no owner
//        in §10.1's B rows. Reported as a finding rather than silently adopted
//        or silently dropped.
//
// 35 definitions, and the three per-package subtotals above add to 34 plus the
// one unassigned. The numbers are restated nowhere else: this banner is the
// list, and the file is the check.
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

namespace jnext {
namespace dbg {

// ===========================================================================
// B2 — §4.3 CAP-EVT and INS-17
//
// All eleven need the `EventTable` (the successor of `BreakpointSet`), the
// 512-entry latch ring and the site hooks. Until then, today's PC breakpoints
// and watchpoints are reached through `DebugState::breakpoints()` by the code
// that already does so; the backend deliberately does NOT offer a half of the
// subscription model over `BreakpointSet`, because a `Subscription` whose
// `kind`, `filter`, `condition`, `handler`, `once` and `transient` were all
// silently ignored would be worse than a refusal.
// ===========================================================================

Expected<EventId> Debugger::subscribe(ClientId, const Subscription&) {
    return make_refused<EventId>(Result::Unsupported);
}

Result Debugger::unsubscribe(ClientId, EventId) { return Result::Unsupported; }

Result Debugger::set_enabled(ClientId, EventId, bool) { return Result::Unsupported; }

/// The GH #225 master switch lives on `BreakpointSet` today and defaults ON.
/// Reported as it stands there, because that IS the live value the hot loop
/// obeys — a hard `true` here would be a guess at the same answer.
bool Debugger::master_enabled() const {
    return impl_->ds().breakpoints().master_enabled();
}

Result Debugger::set_master_enabled(bool) { return Result::Unsupported; }

/// No per-client switch exists until B3 has clients. Every client is therefore
/// enabled, which is what `true` says.
bool Debugger::client_enabled(ClientId) const { return true; }

Result Debugger::set_client_enabled(ClientId, bool) { return Result::Unsupported; }

/// "Would an `Execute` subscription match here?" `BreakpointSet::has_pc()` is
/// exactly that question for today's model, and the GH #221 step-off arm is
/// already asking it inside `DebugState::should_break()`. Answered for real
/// rather than refused: it is a PURE query with a correct answer available, and
/// B2 only widens what counts as a match.
bool Debugger::probe_execute(uint16_t pc) const {
    return impl_->ds().breakpoints().has_pc(pc);
}

Result Debugger::raise_host_event(ClientId, const std::string&) {
    return Result::Unsupported;
}

/// INS-17. An empty list, and it means "the subscription MODEL does not exist
/// yet" — not "no subscriptions". A frontend that wants today's breakpoints
/// reads `BreakpointSet` directly, as the Qt panels do.
std::vector<SubscriptionInfo> Debugger::subscriptions(bool) const { return {}; }

/// INS-17. Nothing is delivered until B2 delivers it, so there is nothing after
/// any sequence number.
std::vector<Event> Debugger::events_fired_since(uint64_t) const { return {}; }

// ===========================================================================
// B3 — §4.8 CAP-SES
//
// The client table, the listeners, the service list and `pump()`'s drain
// policy, plus the loop driver that CTL-12 `Hard` and CTL-15 route through
// (both of which already refuse with `RefusedUnavailable` in
// `debugger_control.cpp`, which is their §4.1 behaviour with no driver
// registered — so those two are NOT in this file).
// ===========================================================================

Expected<ClientId> Debugger::attach(const ClientInfo&) {
    return make_refused<ClientId>(Result::Unsupported);
}

Result Debugger::detach(ClientId) { return Result::Unsupported; }

Result Debugger::set_listener(ClientId, Listener*) { return Result::Unsupported; }

Result Debugger::add_service(Service&) { return Result::Unsupported; }

Result Debugger::remove_service(Service&) { return Result::Unsupported; }

/// SES-03. Not a query and not a refusing verb — the third bucket of the
/// taxonomy (see debugger.h). With no services to drain and no listeners to
/// notify there is nothing for it to do, so it reports the truth about the
/// session: nothing attached, and whether the machine is paused.
ServiceHint Debugger::pump(const PumpBudget&) {
    ServiceHint hint;
    hint.remote_attached = false;
    hint.paused          = impl_->ds().paused();
    return hint;
}

Result Debugger::set_live_raster(ClientId, bool) { return Result::Unsupported; }

/// SES-05, the ORed-across-clients render hint. There are no clients to OR, and
/// `DebugState::active()` is NOT the same thing (it gates the step machinery
/// too), so this reports `false` rather than conflating the two.
bool Debugger::live_raster() const { return false; }

/// SES-05 — "is any client attached?", the gate on the step machinery.
/// `DebugState::active()` is what that means on today's tree: it is set by the
/// Qt frontend when the debugger window opens and by the magic-breakpoint hook,
/// and it is the first term of `armed()`. B3 makes it the client count.
bool Debugger::attached() const { return impl_->ds().active(); }

Result Debugger::set_loop_driver(const LoopDriver&) { return Result::Unsupported; }

Result Debugger::on_cold_boot_done() { return Result::Unsupported; }

// ===========================================================================
// B4 — input pulses, capture, bookmarks, coverage
//
// IN-01's APPEND semantics need the auto-type queue rework; CAP-01 needs the
// deferred-to-next-rendered-frame capture path; CAP-03 needs per-client
// snapshot storage; INS-20 needs a per-instruction bit set inside the
// attached-gated branch, which is a hot-path change B1 is specified not to make.
// ===========================================================================

Expected<size_t> Debugger::press_key(ClientId, const std::string&, int) {
    return make_refused<size_t>(Result::Unsupported);
}

Expected<size_t> Debugger::press_key(ClientId, const MatrixKey&, int) {
    return make_refused<size_t>(Result::Unsupported);
}

Result Debugger::screenshot(ClientId, const std::string&, uint8_t,
                            ScreenshotFormat) {
    return Result::Unsupported;
}

/// CAP-02. One line over `Ula::screen_dump()` when B4 arrives; empty until then,
/// which a caller distinguishes from a real dump by its size (a `.SCR` is 6912
/// bytes and never 0).
std::vector<uint8_t> Debugger::ula_screen_dump() const { return {}; }

Result Debugger::bookmark_save(ClientId, const std::string&, SaveStateMode) {
    return Result::Unsupported;
}

Result Debugger::bookmark_restore(ClientId, const std::string&) {
    return Result::Unsupported;
}

/// CAP-03. An empty list is what a client that has saved nothing gets, which is
/// every client here.
std::vector<std::string> Debugger::bookmarks(ClientId) const { return {}; }

Result Debugger::save_snapshot(ClientId, const std::string&) {
    return Result::Unsupported;
}

Result Debugger::coverage_enable(bool) { return Result::Unsupported; }

/// INS-20. False, and `coverage()` below is all-zero — which is exactly the pair
/// §4.2 describes for coverage switched off, and why the two accessors exist
/// together: a caller can tell "nothing ran" from "not recording".
bool Debugger::coverage_enabled() const { return false; }

Result Debugger::coverage_clear() { return Result::Unsupported; }

/// INS-20. A single zero bit set, static because the signature hands out a
/// reference to 8 KB. Never written, so it cannot be mistaken for a recording.
const CoverageBits& Debugger::coverage() const {
    static const CoverageBits none;
    return none;
}

// ===========================================================================
// UNASSIGNED — INS-14 `render_layer`
//
// See the banner: the move out of `src/debugger/video_panel.cpp` has no owner in
// §10.1's B rows, and this is a finding rather than a decision to take here.
// Refused rather than approximated: drawing "something" into the caller's buffer
// (the composite for every view, say) would make a panel look right and be
// wrong, which is worse than a control that greys itself out.
// ===========================================================================

Result Debugger::render_layer(Layer, int, uint32_t*, size_t) const {
    return Result::Unsupported;
}

}  // namespace dbg
}  // namespace jnext
