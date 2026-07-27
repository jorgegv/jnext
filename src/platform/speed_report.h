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
/// The measurement floor this has to clear, at the 1 s status window:
///   * INTEGER FRAME QUANTISATION — a window of a 49.36 Hz machine reports 49
///     or 50 whole frames, i.e. 99.3% or 101.3%: +-1.5%.
///   * AUDIO PACING — audio_pacing::frames_for_tick legitimately runs a double
///     or skips a frame to track the sound card's clock, moving a 1 s window by
///     one more frame: another +-2%.
/// Together ~4%. Ten points is comfortably clear of that, while a real 10%
/// shortfall (44 fps on a 49.36 Hz machine) is already visible judder.
inline constexpr int SHORTFALL_MARGIN_PCT = 10;

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
