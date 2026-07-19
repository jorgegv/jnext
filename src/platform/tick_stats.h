#pragma once

#include <cstdint>

/// Tick-delivery statistics (GitHub issue #9 / Task 63).
///
/// WHY THIS EXISTS — the cadence line names the symptom, not the mechanism.
///
/// On the issue-#9 reporter's machine the honest cadence accounting
/// (src/platform/present_cadence.h) showed: ~55 frames/s emulated, ~35
/// presented, ~20 superseded, lost ~0, ~20 audio doubles/s. Arithmetic on
/// those figures (callbacks = emulated - doubles + skips) proves the frame
/// timer's callbacks arrive only ~35 times/s when ~58 are due — the audio
/// pacer then double-steps to keep the audio clock fed, and every double
/// supersedes a frame before any paint. That alternating frame-skip IS the
/// judder. But the cadence line cannot say WHY two of every five callbacks
/// never arrive. Four candidate mechanisms remain:
///
///   (a) timer callbacks delivered late  -> tick count + interval min/mean/max
///                                          + late count against 1.5x period;
///   (b) emulation too slow in the tick  -> handler + emulation-time
///                                          min/mean/max;
///   (c) paint/prescale too slow         -> paint-cost min/mean/max (sampled
///                                          by EmulatorWidget, drained here);
///   (d) audio queue starvation          -> queue-depth min/mean/max.
///
/// This header holds ALL the logic for that instrument: accumulation and
/// summarising are pure functions so the unit suite can pin them, because the
/// QtApp::on_frame_tick wiring that feeds them is reachable by no test (a
/// known, documented blind spot). The wiring stays trivially thin.
///
/// UNITS. A Stat is unit-agnostic: the caller feeds integer samples and reads
/// back the same unit. The frontend uses microseconds for every duration
/// (interval, handler, emulation, paint) and milliseconds for the audio queue
/// depth (SdlAudio::queued_ms() already quantises to ms).
///
/// FIRST TICK. A tick-to-tick interval needs a previous timestamp, so the
/// first tick ever contributes NO interval sample — the caller simply does not
/// call add_interval() for it. The frontend keeps the previous timestamp
/// ACROSS window drains, so only process startup loses one sample; a Counters
/// filled from scratch (as in the unit tests) likewise starts interval-less.
///
/// LATE THRESHOLD. late_threshold_us() = 1.5x the frame period in effect at
/// that tick, integer floor. An interval EXACTLY at the threshold is NOT
/// late; only strictly greater is (an on-time tick after one entirely missed
/// slot measures ~2.0x and must count; ordinary jitter around 1.0x must not —
/// the boundary itself is grouped with jitter, not with loss). The period is
/// passed per sample because it can change mid-window (runtime 50/60 Hz
/// switch, speed change): each sample is judged against ITS OWN period, and
/// the report carries only the LATEST sample's threshold for the log line —
/// printing one number for a mixed window is a documented simplification.
namespace tick_stats {

/// One accumulating distribution: count/sum/min/max of integer samples.
/// Unit is whatever the caller feeds (us for durations, ms for queue depth).
struct Stat {
    uint64_t count = 0;
    int64_t  sum   = 0;
    int64_t  min   = 0;
    int64_t  max   = 0;
};

inline void add_sample(Stat& s, int64_t v)
{
    if (s.count == 0) {
        // First sample seeds min AND max — the zero-initialised min must not
        // survive into a window whose smallest sample is above zero.
        s.min = v;
        s.max = v;
    } else {
        if (v < s.min) s.min = v;
        if (v > s.max) s.max = v;
    }
    s.sum += v;
    ++s.count;
}

/// 1.5x the period, integer floor (17197 us -> 25795, not 25796).
constexpr int64_t late_threshold_us(int64_t period_us)
{
    return period_us + period_us / 2;
}

/// Raw tallies accumulated over one reporting window (one status tick).
/// All monotonic within the window; reset by the caller when drained.
struct Counters {
    uint64_t ticks = 0;      ///< timer callbacks seen this window
    Stat interval_us;        ///< tick-to-tick spacing (see add_interval)
    uint64_t late = 0;       ///< intervals strictly above 1.5x their own period
    int64_t late_threshold_us_latest = 0;  ///< threshold of the most recent sample
    Stat handler_us;         ///< on_frame_tick entry-to-exit duration
    Stat emu_us;             ///< time inside the run_frame loops of a tick
                             ///  (pacing loop + fastload burst; 0 when a tick
                             ///  emulated nothing — paused, or audio-skip)
    Stat queue_ms;           ///< audio device queue depth; NO samples when no
                             ///  audio device exists (--silent / init failure)
    Stat paint_us;           ///< new-frame paintEvent cost (widget-drained)
};

inline void add_tick(Counters& c) { ++c.ticks; }

/// Record one tick-to-tick interval, judged late against the frame period in
/// effect at that tick (see the LATE THRESHOLD note above).
inline void add_interval(Counters& c, int64_t interval_us, int64_t period_us)
{
    add_sample(c.interval_us, interval_us);
    const int64_t thr = late_threshold_us(period_us);
    c.late_threshold_us_latest = thr;
    if (interval_us > thr) ++c.late;
}

/// Summary of one Stat. `mean` is the EXACT double division sum/count — no
/// rounding here, presentation rounds. count == 0 => mean/min/max all 0 and
/// valid == false, so callers print "n/a" instead of a fabricated zero.
struct StatReport {
    uint64_t count = 0;
    double   mean  = 0.0;
    int64_t  min   = 0;
    int64_t  max   = 0;
    bool     valid = false;
};

inline StatReport summarize_stat(const Stat& s)
{
    StatReport r;
    r.count = s.count;
    if (s.count > 0) {
        r.mean  = static_cast<double>(s.sum) / static_cast<double>(s.count);
        r.min   = s.min;
        r.max   = s.max;
        r.valid = true;
    }
    return r;
}

/// The derived figures for one window — same units as the Counters.
struct Report {
    uint64_t   ticks = 0;
    StatReport interval_us;
    uint64_t   late = 0;
    /// Threshold actually used for the LAST interval sample of the window
    /// (0 when the window had no interval sample). Carried so the log line
    /// can print the number the `late` count was judged against.
    int64_t    late_threshold_us = 0;
    StatReport handler_us;
    StatReport emu_us;
    StatReport queue_ms;
    StatReport paint_us;
};

inline Report summarize(const Counters& c)
{
    Report r;
    r.ticks             = c.ticks;
    r.interval_us       = summarize_stat(c.interval_us);
    r.late              = c.late;
    r.late_threshold_us = c.late_threshold_us_latest;
    r.handler_us        = summarize_stat(c.handler_us);
    r.emu_us            = summarize_stat(c.emu_us);
    r.queue_ms          = summarize_stat(c.queue_ms);
    r.paint_us          = summarize_stat(c.paint_us);
    return r;
}

}  // namespace tick_stats
