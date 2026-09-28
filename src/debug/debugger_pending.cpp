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
//   B2 — events (§4.3 CAP-EVT + INS-17): DONE, 0 definitions left here.
//        All eleven moved to `debugger_events.cpp` with the `EventTable`, the
//        512-entry latch ring and the site hooks behind them.
//
//   B3 — session (§4.8 CAP-SES): DONE, 0 definitions left here.
//        All eleven moved to `debugger_session.cpp` with the client table, the
//        listener fan-out, the service list and `pump()`'s drain policy;
//        `reset(Hard)` and `load()` (which were never in this file — they
//        refused from `debugger_control.cpp`) moved to
//        `debugger_reconstruct.cpp` with the CTL-12 re-application.
//
//   B4 — input pulses, capture, bookmarks, coverage: 12 definitions
//        press_key (name), press_key (matrix), screenshot, ula_screen_dump,
//        bookmark_save, bookmark_restore, bookmarks, save_snapshot,
//        coverage_enable, coverage_enabled, coverage_clear, coverage
//
//        B4 INHERITS §5's rule with them (B3 fix round 1b): a verb that would
//        EXECUTE, REWIND, RESTORE, RESET or REPLACE the machine refuses from
//        inside an event delivery through `Impl::refuse_inside_delivery()`,
//        with `Unsupported`, before anything else. Of these, `bookmark_restore`
//        restores the machine; `bookmark_save` and `save_snapshot` advance it
//        when a frame is in progress (the `save_state_bytes()` pattern: guard the
//        ADVANCE arm only, so a handler at a frame boundary can still save).
//        Add each one's REENT row and its entry in the REENT-30 loop.
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
// 13 definitions, and the per-package subtotals above add to 12 plus the one
// unassigned (B2's 11 and B3's 11 are gone — this file held 35 before B2 and 24
// before B3). The numbers are restated nowhere else: this banner is the list, and
// the file is the check:
//
//     grep -c '^[A-Za-z].* Debugger::' src/debug/debugger_pending.cpp
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

namespace jnext {
namespace dbg {

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
