#pragma once

#include <cstdint>

/// Present-cadence accounting (GitHub issue #9 / Task 63).
///
/// WHY THIS EXISTS — the instruments lied.
///
/// jnext had two frame indicators, and both of them measured the wrong thing:
///
///   1. The Qt status-bar "FPS" counted `run_frame()` calls, i.e. EMULATED
///      frames. A reporter saying "it judders but the counter says 59 fps"
///      was telling us nothing about how many frames reached the screen —
///      which is the entire subject of issue #9.
///
///   2. The `cadence:` log line derived `dropped` as `emulated - presented`
///      with `presented` counted as raw paintEvents. Because the frontend
///      may run TWO emulator frames in one timer tick (audio catch-up, see
///      audio_pacing::frames_for_tick) but calls the widget's update_frame()
///      only ONCE per tick, `dropped` was structurally forced to equal the
///      double-step count. It measured the audio pacing policy, not lost
///      presents. It also went NEGATIVE (Qt serves expose/resize paints the
///      emulator never asked for) and reported a nonsense `over 0ms` window
///      on its first line.
///
/// THE SEMANTICS, FROM FIRST PRINCIPLES.
///
/// A frame is "presented" when the image the emulator composited for it is
/// actually painted to the widget. From that single definition everything
/// else follows:
///
///   * If a tick emulates 2 frames and paints once, the FIRST frame's image
///     was overwritten in the framebuffer before any paint could serve it.
///     That frame genuinely never reached the screen, so it IS dropped. The
///     old code's instinct to call this "0 dropped" was wrong.
///
///   * But that drop is DELIBERATE and BENIGN — it is the audio pacing policy
///     compressing time on purpose. Reporting it in the same bucket as a
///     present that the window system silently swallowed is precisely the
///     conflation that made the original instrument useless.
///
/// So `dropped` is reported honestly as the raw total, and then DECOMPOSED
/// into its explained and unexplained parts. Nothing is quietly subtracted:
///
///     dropped = superseded + unrendered + lost      (exactly, always)
///
///   superseded  overwritten by a later frame inside the same tick — audio
///               catch-up double-steps, fastload bursts. Expected.
///   unrendered  emulated in a tick that deliberately skipped compositing
///               (the speed > 1x render throttle). Expected.
///   lost        the remainder: a FRESH frame was handed to the widget and no
///               paintEvent ever served it. THIS is the issue-#9 signal, and
///               it is the only figure a healthy machine must keep at zero.
///
/// COUNTING `presented` HONESTLY. The widget counts a present only when a
/// paintEvent consumes a framebuffer HANDED TO IT and not yet painted (see
/// EmulatorWidget::frame_pending_). A spontaneous expose/resize repaint that
/// re-blits an image already shown is NOT a new frame reaching the screen,
/// and must not inflate the count — that over-counting is what drove the old
/// `dropped` negative. Pinned by test/gui/present_count_test.cpp.
///
/// STALE RE-PRESENTS ARE NOT COUNTED, AND THIS IS LOAD-BEARING. The frontend
/// re-pushes the SAME framebuffer whenever a tick composited nothing — the
/// debugger is paused, or the audio pacer skipped the tick. Those pushes are
/// declared `new_content = false` (see EmulatorWidget::update_frame) and do
/// not arm the pending flag, so they are painted exactly as before but not
/// counted.
///
/// An earlier revision of this file counted them and claimed the inflation
/// "cannot corrupt the report, because a paused window emulates nothing so
/// `dropped` floors at 0". That claim was FALSE, and review caught it: it
/// holds only for a window lying entirely inside a pause. Nothing flushes the
/// window on a pause/resume transition, so a window can straddle one — and
/// then the pause segment's inflated `presented` cancels the resume segment's
/// real drops through the very floor that was offered as the safeguard:
///
///     emulated=10, presented=48 (40 stale + 8 real), 2 genuinely lost
///       -> dropped=0, lost=0            // two real drops, silently absorbed
///
/// A developer stepping and resuming around a suspect frame is precisely the
/// session this instrument exists to serve, so that is the worst possible
/// place to under-report. Not counting stale pushes fixes it at source: the
/// paused segment now contributes 0 emulated and 0 presented, so the same
/// window reports emulated=10, presented=8, lost=2. Pinned by PC-13
/// (arithmetic) and PC-G08 (widget).
///
/// The non-negative floor on `dropped` remains, but as a pure arithmetic
/// guarantee against inconsistent inputs — NOT as a correctness argument.
namespace present_cadence {

/// Raw tallies accumulated over one reporting window (one status tick).
/// All four are monotonic within the window and reset when it is drained.
struct Counters {
    /// `run_frame()` calls — emulated frames.
    uint64_t emulated = 0;
    /// Frames carrying NEW content that were actually served by a paintEvent.
    /// Stale re-pushes of an unchanged framebuffer are excluded — see the
    /// "STALE RE-PRESENTS" note above for why that exclusion is load-bearing.
    uint64_t presented = 0;
    /// Frames overwritten by a later frame in the same tick before any
    /// paint could serve them (multi-frame ticks).
    uint64_t superseded = 0;
    /// Frames emulated in a tick that requested no present at all.
    uint64_t unrendered = 0;
};

/// The derived, decomposed figures for one window.
struct Report {
    uint64_t emulated = 0;
    uint64_t presented = 0;
    /// Emulated frames whose image never reached the screen. Never negative:
    /// when `presented` exceeds `emulated` (paused debugger re-presenting a
    /// stale frame, window-system repaints) nothing was dropped, so it is 0.
    uint64_t dropped = 0;
    uint64_t superseded = 0;  ///< explained: multi-frame tick
    uint64_t unrendered = 0;  ///< explained: render throttle
    uint64_t lost = 0;        ///< UNEXPLAINED: a fresh frame was never painted
    int64_t elapsed_ms = 0;
    /// False when the window has no meaningful duration (elapsed <= 0), e.g.
    /// a first tick with no prior timestamp. Callers must not log such a
    /// window — reporting rates "over 0ms" is nonsense, not diagnostics.
    bool reportable = false;
};

/// Whether a tick's push to the widget carries NEWLY composited content.
///
/// A tick that ran no run_frame() at all — the debugger is paused, or the
/// audio pacer skipped the tick because the device queue is high — re-pushes
/// the framebuffer byte-for-byte as already shown. Declaring that honestly is
/// what keeps a window straddling pause->resume from cancelling real drops
/// against stale re-presents (see the note above and PC-13).
constexpr bool carries_new_content(int frames_this_tick)
{
    return frames_this_tick > 0;
}

/// Frames a tick supersedes: every frame but the last, and only when the tick
/// actually asked for a present (otherwise they are `unrendered`, not
/// superseded — the distinction is what separates a throttle from a drop).
constexpr uint64_t superseded_in_tick(int frames_this_tick, bool present_requested)
{
    if (!present_requested || frames_this_tick <= 1) return 0;
    return static_cast<uint64_t>(frames_this_tick - 1);
}

/// Frames a tick emulates without requesting any present at all.
constexpr uint64_t unrendered_in_tick(int frames_this_tick, bool present_requested)
{
    if (present_requested || frames_this_tick <= 0) return 0;
    return static_cast<uint64_t>(frames_this_tick);
}

/// Derive the decomposed report for a completed window.
///
/// The explained buckets are clamped against `dropped` before the remainder is
/// taken, so `lost` can never be driven negative by an over-tally, and the
/// identity dropped == superseded + unrendered + lost always holds.
inline Report summarize(const Counters& c, int64_t elapsed_ms)
{
    Report r;
    r.emulated = c.emulated;
    r.presented = c.presented;
    r.dropped = c.emulated > c.presented ? c.emulated - c.presented : 0;

    r.superseded = c.superseded < r.dropped ? c.superseded : r.dropped;
    const uint64_t rest = r.dropped - r.superseded;
    r.unrendered = c.unrendered < rest ? c.unrendered : rest;
    r.lost = rest - r.unrendered;

    r.elapsed_ms = elapsed_ms;
    r.reportable = elapsed_ms > 0;
    return r;
}

}  // namespace present_cadence
