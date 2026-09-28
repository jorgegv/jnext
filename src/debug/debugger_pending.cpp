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
//   B4 — capture: 2 definitions
//        screenshot, save_snapshot
//        (DONE and moved: press_key (name), press_key (matrix) —
//        `debugger_input.cpp` beside IN-02; coverage_enable, coverage_enabled, coverage_clear,
//        coverage — `debugger_inspect.cpp` beside the trace; ula_screen_dump —
//        `debugger_inspect.cpp` beside the ULA screen registers;
//        bookmark_save, bookmark_restore, bookmarks — `debugger_capture.cpp`)
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
// 3 definitions, and the per-package subtotals above add to 2 plus the one
// unassigned (B2's 11, B3's 11 and ten of B4's are gone — this file held 35
// before B2, 24 before B3 and 13 before B4). The numbers are restated nowhere else: this banner is the list, and
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
// snapshot storage.
// ===========================================================================

Result Debugger::screenshot(ClientId, const std::string&, uint8_t,
                            ScreenshotFormat) {
    return Result::Unsupported;
}

Result Debugger::save_snapshot(ClientId, const std::string&) {
    return Result::Unsupported;
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
