#pragma once

#include <cstdint>

/// Fractional frame-deadline scheduler (GitHub issue #9 / Task 63).
///
/// WHY THIS EXISTS — the frame timer ran measurably fast, and the audio
/// pacer had to stutter once a second to hide it.
///
/// The GUI paces emulation with a repeating QTimer, and a QTimer interval is
/// a WHOLE number of milliseconds. The emulated frame periods are not
/// (src/core/emulator_config.h machine_timing, master clock 28 MHz):
///
///     Next 50 Hz:  1824*311 = 567264 cycles -> 20.259 ms  (49.36 Hz)
///     Next 60 Hz:  1824*264 = 481536 cycles -> 17.198 ms  (58.15 Hz)
///
/// The old scheme set a FIXED interval of round(period) — 20 ms and 17 ms —
/// which runs 1.3% / 1.1% fast respectively. Both consequences were measured,
/// not theorised (issue #9, jnext + contributor jon263):
///
///   * --silent (wall-clock paced): the machine emulates 58.77-58.79
///     frames/s in Next-60 mode against the true 58.15 — every emulated
///     second is ~11 ms short, and A/V timings drift accordingly.
///   * with audio: the audio pacer (audio_pacing::frames_for_tick) corrects
///     the systematic drift for it — the device queue creeps up ~1%/s and
///     the band answers with a 0-frame tick roughly once a second. That is
///     one frozen frame per second, a visible micro-stutter on a perfectly
///     healthy machine (measured: ~19 skipped ticks per 20 s).
///
/// THE FIX. Keep an ABSOLUTE deadline on the host monotonic clock and
/// advance it by the EXACT fractional period every tick; the interval handed
/// back to the timer is whatever whole-ms delay reaches that deadline from
/// *now*. Consecutive intervals then alternate (17,17,17,18,... averaging
/// 17.198) and the long-run rate is exact by construction: rounding error
/// cannot accumulate, because every interval is recomputed against the wall
/// clock rather than chained onto the previous interval.
///
/// Computing against `now` is load-bearing: QTimer::setInterval() on a live
/// timer restarts its period from the moment of the call, so the value
/// handed over must be "ms from now to the deadline", not "the ideal
/// period". It is also why the caller invokes next_interval_ms() at the END
/// of its tick handler — the emulation work already done this tick is
/// thereby subtracted from the wait.
///
/// STALL POLICY. If `now` has fallen more than STALL_PERIODS periods past
/// the deadline (host stall, modal dialog, fastload burst overrunning the
/// tick, laptop suspend), the schedule RESYNCS to now + period instead of
/// walking the backlog off with a burst of 1 ms intervals. WHY: catching up
/// emulated time is the AUDIO pacer's job (frames_for_tick runs 2 frames per
/// tick when the device queue is low); if the timer also machine-gunned
/// callbacks after a stall, the two mechanisms would fight — the burst
/// floods the very queue the pacer is levelling. Lateness WITHIN the
/// threshold is walked off (clamped-to-1-ms intervals, at most ~2 periods'
/// worth) so ordinary scheduling jitter never shifts the long-run grid.
///
/// PERIOD CHANGES GLIDE. next_interval_ms() takes the period on every call,
/// so a runtime 50/60 Hz switch (NR 0x05 bit 2) or a speed change needs no
/// separate code path: the anchored deadline stands and the NEXT advance
/// simply uses the new period — continuous, no dropped or doubled tick.
/// rebase() exists for moments that are semantically a restart (timer
/// start, cold boot, an explicit speed change), where gliding from a stale
/// anchor would be wrong.
namespace frame_deadline {

/// A stall is declared when `now` is MORE than this many periods past the
/// deadline. Two periods: a single late tick is ordinary jitter and must be
/// walked off to preserve the grid; longer silence is a genuine stall.
inline constexpr int64_t STALL_PERIODS = 2;

class Scheduler {
public:
    /// (Re)anchor the schedule: the next deadline becomes `now + period`.
    /// Returns the whole-ms timer interval to that deadline (>= 1).
    /// Use at timer start, cold boot, and speed changes.
    int rebase(int64_t now_us, int64_t period_us)
    {
        deadline_us_ = now_us + period_us;
        return interval_to(now_us);
    }

    /// Advance the deadline by exactly one period and return the whole-ms
    /// timer interval from `now` to it (>= 1). Called once per timer tick,
    /// at the end of the tick's work. Applies the stall policy above.
    int next_interval_ms(int64_t now_us, int64_t period_us)
    {
        if (now_us - deadline_us_ > STALL_PERIODS * period_us) {
            deadline_us_ = now_us + period_us;  // resync — never machine-gun
            ++resyncs_;
        } else {
            deadline_us_ += period_us;          // the exact fractional grid
        }
        return interval_to(now_us);
    }

    /// Stall resyncs since construction (diagnostics/tests).
    uint64_t resyncs() const { return resyncs_; }

private:
    /// Whole-ms delay from `now` to the deadline: rounded to nearest ms,
    /// clamped to >= 1 (an interval of 0 would make the event loop spin).
    int interval_to(int64_t now_us) const
    {
        const int64_t delta_us = deadline_us_ - now_us;
        int64_t ms = (delta_us + 500) / 1000;
        if (ms < 1) ms = 1;
        return static_cast<int>(ms);
    }

    int64_t  deadline_us_ = 0;
    uint64_t resyncs_     = 0;
};

}  // namespace frame_deadline
