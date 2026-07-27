#pragma once

#include <cmath>

/// Achieved-vs-requested emulation speed (GitHub issue #120).
///
/// WHY THIS EXISTS — the status bar's speed figure was never a measurement.
///
/// `MainWindow::update_status()` renders the emulator-speed cell from
/// `QtApp::speed_multiplier_`, which is the value the USER asked for (`--speed`,
/// or Machine > Emulator Speed). Nothing ever compares it against what the host
/// actually delivered, so the cell reads "100%" on a machine emulating five
/// frames a second exactly as it does on one that is keeping up perfectly.
///
/// That is the misleading-indicator half of issue #120: a reporter looking at
/// "100%" next to a low frame rate concludes the two instruments disagree,
/// when in truth only one of them was ever measuring anything.
///
/// THE SECOND TRAP: 50 Hz IS NOT 50 fps. The Next's "50 Hz" mode is
/// 1824 x 311 cycles of a 28 MHz clock = 20.259 ms = **49.36 Hz**, and its
/// "60 Hz" mode is 58.15 Hz (see the table in frame_deadline.h). A perfectly
/// healthy jnext therefore reports "FPS: 49.4 emu / 49.4 shown", which a user
/// expecting a round 50 reads as a 1.3% shortfall that is not a shortfall at
/// all. Reporting a PERCENTAGE OF THE MACHINE'S OWN REFRESH removes that trap:
/// 49.4 fps on a Next at 50 Hz is 100%, and says so.
///
/// The report is deliberately conservative — it only contradicts the requested
/// figure on a shortfall large enough that no measurement artefact explains it
/// (see SHORTFALL_MARGIN_PCT). An indicator that cried wolf on ordinary jitter
/// would be no more use than one that never spoke.
namespace speed_report {

/// How far below the requested percentage the achieved one must fall before it
/// is reported as a genuine shortfall.
///
/// DERIVED, NOT CHOSEN. Every source of error in a 1 s status window is a whole
/// number of FRAMES, so the margin is expressed that way and converted once.
/// One frame of a 1 s window is `frame_period_ms / 10` percentage points —
/// 2.03 at the longest machine period (20.259 ms, Next 50 Hz), which is the
/// worst case and therefore the one to size against.
///
/// The frames that can move a HEALTHY window:
///   * 1 frame — INTEGER QUANTISATION. The window counts whole frames, so a
///     49.36 Hz machine reports 49 or 50: at most one frame from the truth.
///   * 2 frames — AUDIO PACING. audio_pacing::frames_for_tick legitimately runs
///     a double or skips one to track the sound card (frame_sequencer.h step 6),
///     each shifting the count by exactly one. Steady state on a healthy host
///     is measured at ZERO of these (`audio-catchup: 0 doubles, 0 skips` in the
///     cadence log), so two is already generous headroom, not a typical figure.
/// Total 3 frames = 6.08 points; 7 is the next whole point above it.
///
/// What is deliberately NOT in this budget: the status timer's window jitter.
/// status_timer_ is a Qt::CoarseTimer (5% documented tolerance) and a raw frame
/// count over it would have contributed +-5 points on its own — more than
/// everything above combined. Rather than widen the margin to swallow that, the
/// caller divides the count by the window it ACTUALLY measured
/// (QtApp::on_status_tick), which removes the term at source. Sizing a
/// detection threshold around an instrument error that can simply be corrected
/// is how a threshold ends up too wide to ever fire.
///
/// At 7, a host delivering 92% of the true rate is flagged; the previous
/// hand-waved 10 let a real 91% shortfall show a clean "100%".
inline constexpr int SHORTFALL_MARGIN_PCT = 7;

struct Report {
    /// What the user asked for: 100 = normal speed. Always valid.
    int  requested_pct = 100;
    /// What the host actually delivered, as a percentage of the machine's own
    /// refresh rate. Only meaningful when `achieved_valid`.
    int  achieved_pct = 0;
    /// False when there is nothing to measure — the machine emulated no frames
    /// in the window (debugger paused, or the very first window), or the frame
    /// period is not yet known. Callers must show the requested figure alone.
    bool achieved_valid = false;
    /// The achieved rate is below the requested one by more than
    /// SHORTFALL_MARGIN_PCT. Implies `achieved_valid`.
    bool shortfall = false;
};

/// Frames per second from a frame COUNT and the window that was actually
/// timed. Named and tested rather than inlined at the call site because it is
/// the whole reason SHORTFALL_MARGIN_PCT can be as tight as it is: the caller's
/// status timer is a Qt::CoarseTimer with 5% tolerance, so a raw count read as
/// "per second" carries more error than every other term in the margin budget
/// combined.
///
/// A non-positive window is not a measurement, so the count is returned
/// unchanged — the pre-normalisation reading, and never a division by zero.
inline double rate_from_window(double frames, int64_t window_ms)
{
    return window_ms > 0 ? frames * 1000.0 / static_cast<double>(window_ms)
                         : frames;
}

/// Summarise one status window.
///
///   emulated_fps      frames the emulator ran in the window, per second
///   frame_period_ms   the machine's own frame period (Emulator::frame_period_ms)
///   speed_multiplier  the requested multiplier; 1.0 = normal
///
/// 100% means "one emulated frame per machine frame period", so the achieved
/// and requested figures are directly comparable at every multiplier: at 2x the
/// emulator must run at twice the machine's refresh to score 200.
inline Report summarize(double emulated_fps, double frame_period_ms,
                        double speed_multiplier)
{
    Report r;
    r.requested_pct = static_cast<int>(std::lround(speed_multiplier * 100.0));

    // A non-positive period is "not known yet", not "infinitely fast": there is
    // no rate to be a percentage of, so refuse to invent one. Likewise a window
    // that emulated nothing is a paused machine, not a 0% one.
    if (frame_period_ms <= 0.0 || emulated_fps <= 0.0) return r;

    // achieved% = fps / (1000 / period) * 100 = fps * period / 10.
    r.achieved_pct    = static_cast<int>(std::lround(emulated_fps * frame_period_ms / 10.0));
    r.achieved_valid  = true;
    r.shortfall       = r.achieved_pct < r.requested_pct - SHORTFALL_MARGIN_PCT;
    return r;
}

}  // namespace speed_report
