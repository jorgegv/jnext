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
//   B4 — input pulses, capture, bookmarks, coverage (§4.5 CAP-IN / CAP-CAP,
//        §4.2 INS-20): DONE, 0 definitions left here. All twelve moved:
//        press_key (name), press_key (matrix) → `debugger_input.cpp`, beside
//        IN-02; coverage_enable, coverage_enabled, coverage_clear, coverage and
//        ula_screen_dump → `debugger_inspect.cpp`; screenshot, bookmark_save,
//        bookmark_restore, bookmarks, save_snapshot → `debugger_capture.cpp`.
//        §5's rule came with them (B3 fix round 1b): `bookmark_restore` refuses
//        from inside a delivery first thing; `bookmark_save` and `save_snapshot`
//        refuse only on their ADVANCE arm, through the one frame-boundary helper
//        `Impl::reach_frame_boundary()` — rows REENT-31..33 and REENT-30's loop.
//
//   PACKAGE Q (WP4d) — 1 definition
//        render_layer (INS-14). Not B2's, B3's or B4's by §10.1, and not "over
//        an existing primitive" either: the eight views exist only inside
//        `VideoLayerView` in a `Q_OBJECT` header, and INS-14 requires
//        `render_to_image` + the `replay_*` calls to be MOVED out of
//        `video_panel.cpp` into a Qt-free function. B1 reported that the move
//        had no owner in §10.1's B rows; the owner has since decided it is
//        package Q's (WP4d, which already owns validating it against the 106
//        DVP rows). It stays refused here until Q moves it.
//
// 1 definition: B2's 11, B3's 11 and B4's 12 are gone — this file held 35
// before B2, 24 before B3 and 13 before B4. The numbers are restated nowhere
// else: this banner is the list, and the file is the check:
//
//     grep -c '^[A-Za-z].* Debugger::' src/debug/debugger_pending.cpp
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

namespace jnext {
namespace dbg {

// ===========================================================================
// PACKAGE Q (WP4d) — INS-14 `render_layer`
//
// See the banner: the move out of `src/debugger/video_panel.cpp` is Q's.
// Refused rather than approximated: drawing "something" into the caller's buffer
// (the composite for every view, say) would make a panel look right and be
// wrong, which is worse than a control that greys itself out.
// ===========================================================================

Result Debugger::render_layer(Layer, int, uint32_t*, size_t) const {
    return Result::Unsupported;
}

}  // namespace dbg
}  // namespace jnext
