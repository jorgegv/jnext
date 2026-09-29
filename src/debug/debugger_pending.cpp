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
//   PACKAGE Q (WP4d) — render_layer (INS-14): DONE, 0 definitions left here.
//        Package Q moved `render_to_image` + the `replay_*` helpers out of
//        `src/debugger/video_panel.cpp` into `debugger_render.cpp`, a Qt-free
//        function the Video panel now calls (GH #278 WP4d).
//
// 0 definitions: B2's 11, B3's 11, B4's 12 and Q's 1 are gone — this file
// held 35 before B2, 24 before B3, 13 before B4 and 1 before Q WP4d. Nothing
// the published header declares is refused for want of an owner any more. The
// numbers are restated nowhere else: this banner is the list, and the file is
// the check:
//
//     grep -c '^[A-Za-z].* Debugger::' src/debug/debugger_pending.cpp
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

namespace jnext {
namespace dbg {

}  // namespace dbg
}  // namespace jnext
