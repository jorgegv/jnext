// Tick-delivery statistics test (GitHub issue #9 / Task 63).
//
// Like present_cadence_test this file has NO VHDL oracle: it tests a *host*
// diagnostic, not emulated hardware. Its oracle is the written specification
// in src/platform/tick_stats.h — what each accumulated and reported figure is
// defined to MEAN. Every row below is derived from that definition (and from
// the task spec that produced it), never from reading the implementation back.
//
// WHY IT EXISTS. The issue-#9 reporter's machine delivers only ~35 of the ~58
// timer callbacks/s that are due; the audio pacer double-steps to compensate,
// superseding ~20 frames/s before any paint — that alternating skip IS the
// judder. The cadence line proves frames are superseded but cannot say WHY the
// callbacks are missing. This instrument discriminates the four candidate
// mechanisms — (a) callbacks delivered late, (b) emulation too slow, (c) paint
// too slow, (d) audio-queue starvation — via per-window min/mean/max
// distributions plus a late-callback count. The QtApp wiring that feeds the
// accumulators is reachable by no test (documented blind spot), so this suite
// must pin ALL the semantics: the wiring stays a thin pair of timestamps.
//
// THE RULES PINNED HERE (from the header spec):
//   * first tick of a window contributes NO interval sample (nothing to
//     difference against);
//   * late threshold = 1.5x the period IN EFFECT AT THAT TICK, integer floor;
//     an interval EXACTLY at the threshold is NOT late, strictly above IS;
//   * a period change mid-window judges each sample against its OWN period,
//     and the report carries the LATEST sample's threshold;
//   * mean is the exact double division sum/count (no integer truncation);
//   * a stat with no samples reports count 0 / valid=false / all-zero figures
//     (queue depth without an audio device must say "no data", not "0 ms").
//
// Run: ./build/test/tick_stats_test

#include "platform/tick_stats.h"

#include <cstdio>
#include <string>

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string dump(const tick_stats::StatReport& s)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "count=%llu mean=%.6f min=%lld max=%lld valid=%d",
                  (unsigned long long)s.count, s.mean, (long long)s.min,
                  (long long)s.max, s.valid ? 1 : 0);
    return std::string(buf);
}

using tick_stats::add_interval;
using tick_stats::add_sample;
using tick_stats::add_tick;
using tick_stats::Counters;
using tick_stats::late_threshold_us;
using tick_stats::Stat;
using tick_stats::summarize;

// The 58.15 Hz Next-60 frame period from the issue-#9 report, in µs.
constexpr int64_t PERIOD_60 = 17197;
// A convenient round period for boundary arithmetic.
constexpr int64_t PERIOD_EVEN = 20000;

}  // namespace

int main()
{
    std::printf("Tick-delivery statistics tests (issue #9 / Task 63)\n");
    std::printf("===================================================\n");

    // --- TS-01: the empty window ----------------------------------------------
    // A window in which the timer never fired at all (frozen event loop) must
    // report zeros and INVALID stats — not fabricated 0-ms means.
    {
        const auto r = summarize(Counters{});
        check("TS-01a", "empty window: no ticks, no lates, no threshold",
              r.ticks == 0 && r.late == 0 && r.late_threshold_us == 0);
        check("TS-01b", "empty window: every stat has no samples and is invalid",
              !r.interval_us.valid && !r.handler_us.valid && !r.emu_us.valid &&
              !r.queue_ms.valid && !r.paint_us.valid);
        check("TS-01c", "empty window: invalid stats carry all-zero figures",
              r.interval_us.count == 0 && r.interval_us.mean == 0.0 &&
              r.interval_us.min == 0 && r.interval_us.max == 0,
              dump(r.interval_us));
    }

    // --- TS-02: a single tick has no interval ---------------------------------
    // The first tick ever has no previous timestamp, so the window carries one
    // tick, one handler sample, and ZERO interval samples. Any interval
    // fabricated here (e.g. against timestamp 0) would be a giant fake "late".
    {
        Counters c;
        add_tick(c);
        add_sample(c.handler_us, 3000);
        const auto r = summarize(c);
        check("TS-02a", "single tick: tick counted, no interval sample",
              r.ticks == 1 && r.interval_us.count == 0 && !r.interval_us.valid,
              dump(r.interval_us));
        check("TS-02b", "single tick: no late can exist without an interval",
              r.late == 0 && r.late_threshold_us == 0);
        check("TS-02c", "single tick: handler stat is that one sample",
              r.handler_us.count == 1 && r.handler_us.min == 3000 &&
              r.handler_us.max == 3000 && r.handler_us.mean == 3000.0,
              dump(r.handler_us));
    }

    // --- TS-03: a known synthetic interval sequence ---------------------------
    // Three intervals {17000, 20000, 45200} µs at the 17197 µs period.
    // min/max are the extremes, mean is EXACTLY 82200/3 = 27400, and only
    // 45200 exceeds the 25795 µs threshold.
    {
        Counters c;
        add_tick(c);                                 // first tick: no interval
        add_tick(c); add_interval(c, 17000, PERIOD_60);
        add_tick(c); add_interval(c, 20000, PERIOD_60);
        add_tick(c); add_interval(c, 45200, PERIOD_60);
        const auto r = summarize(c);
        check("TS-03a", "synthetic sequence: 4 ticks give 3 interval samples",
              r.ticks == 4 && r.interval_us.count == 3, dump(r.interval_us));
        check("TS-03b", "synthetic sequence: min/max are the extremes",
              r.interval_us.min == 17000 && r.interval_us.max == 45200,
              dump(r.interval_us));
        check("TS-03c", "synthetic sequence: mean is exactly 82200/3",
              r.interval_us.mean == 82200.0 / 3.0, dump(r.interval_us));
        check("TS-03d", "synthetic sequence: exactly one interval is late",
              r.late == 1 && r.late_threshold_us == 25795, dump(r.interval_us));
    }

    // --- TS-04: the late-threshold boundary -----------------------------------
    // threshold = period + period/2 (integer floor). EXACTLY at the threshold
    // is NOT late (grouped with jitter); one µs above IS.
    {
        check("TS-04a", "threshold arithmetic: 1.5x even period",
              late_threshold_us(PERIOD_EVEN) == 30000);
        check("TS-04b", "threshold arithmetic: odd period floors (17197 -> 25795)",
              late_threshold_us(PERIOD_60) == 25795);

        Counters c;
        add_interval(c, 30000, PERIOD_EVEN);   // exactly at threshold
        check("TS-04c", "an interval exactly at 1.5x the period is NOT late",
              c.late == 0);
        add_interval(c, 30001, PERIOD_EVEN);   // one µs above
        check("TS-04d", "an interval strictly above 1.5x the period IS late",
              c.late == 1);

        Counters c2;
        add_interval(c2, 25795, PERIOD_60);    // floored threshold, exactly at
        add_interval(c2, 25796, PERIOD_60);    // one µs above
        check("TS-04e", "the floored odd-period boundary behaves identically",
              c2.late == 1);
    }

    // --- TS-05: a period change mid-window ------------------------------------
    // A runtime 50->60 Hz switch (or speed change) changes the period between
    // samples. Each sample is judged against ITS OWN period — the same 26000 µs
    // interval is on-time under a 20000 µs period (threshold 30000) and late
    // under 17197 (threshold 25795). The report's printed threshold is the
    // LATEST sample's (documented simplification for mixed windows).
    {
        Counters c;
        add_interval(c, 26000, PERIOD_EVEN);   // not late: 26000 <= 30000
        add_interval(c, 26000, PERIOD_60);     // late:     26000 >  25795
        const auto r = summarize(c);
        check("TS-05a", "each interval is judged against its own period",
              r.late == 1);
        check("TS-05b", "the report carries the LATEST period's threshold",
              r.late_threshold_us == 25795);

        Counters c2;                           // same samples, opposite order
        add_interval(c2, 26000, PERIOD_60);    // late
        add_interval(c2, 26000, PERIOD_EVEN);  // not late
        const auto r2 = summarize(c2);
        check("TS-05c", "order changes the reported threshold, not the late count",
              r2.late == 1 && r2.late_threshold_us == 30000);
    }

    // --- TS-06: queue depth — absent vs present -------------------------------
    // --silent (or failed audio init) means NO queue samples: the report must
    // say so (valid=false), never fabricate "queue 0 ms" — an empty queue is a
    // starvation signal, and no queue at all must not look like one.
    {
        Counters silent;
        add_tick(silent);
        add_sample(silent.handler_us, 500);
        const auto r = summarize(silent);
        check("TS-06a", "no audio device: queue stat has no samples, invalid",
              r.queue_ms.count == 0 && !r.queue_ms.valid, dump(r.queue_ms));

        Counters with_audio;
        add_sample(with_audio.queue_ms, 38);
        add_sample(with_audio.queue_ms, 41);
        add_sample(with_audio.queue_ms, 55);
        const auto r2 = summarize(with_audio);
        check("TS-06b", "queue samples: count/min/max as fed",
              r2.queue_ms.valid && r2.queue_ms.count == 3 &&
              r2.queue_ms.min == 38 && r2.queue_ms.max == 55, dump(r2.queue_ms));
        check("TS-06c", "queue mean is exactly 134/3",
              r2.queue_ms.mean == 134.0 / 3.0, dump(r2.queue_ms));
    }

    // --- TS-07: mean is exact division, never integer truncation --------------
    // The spec fixes mean = sum/count in double, no rounding in summarize().
    // Integer division would turn {1,2} into 1 and {0,1} into 0 — visibly
    // wrong for the millisecond-scale figures this instrument reports.
    {
        Stat s;
        add_sample(s, 1);
        add_sample(s, 2);
        const auto r = tick_stats::summarize_stat(s);
        check("TS-07a", "mean of {1,2} is exactly 1.5", r.mean == 1.5, dump(r));

        Stat s2;
        add_sample(s2, 0);
        add_sample(s2, 1);
        const auto r2 = tick_stats::summarize_stat(s2);
        check("TS-07b", "mean of {0,1} is 0.5, not truncated to 0",
              r2.mean == 0.5, dump(r2));
    }

    // --- TS-08: min/max seeding and tracking ----------------------------------
    // The first sample must seed BOTH min and max — a zero-initialised min
    // surviving into a window whose smallest sample is 42 would report a fake
    // 0 µs minimum for every window.
    {
        Stat s;
        add_sample(s, 42);
        const auto r = tick_stats::summarize_stat(s);
        check("TS-08a", "a single sample is its own min, max and mean",
              r.min == 42 && r.max == 42 && r.mean == 42.0 && r.count == 1,
              dump(r));

        Stat s2;
        add_sample(s2, 5);
        add_sample(s2, 3);
        add_sample(s2, 9);
        const auto r2 = tick_stats::summarize_stat(s2);
        check("TS-08b", "min/max track downward and upward across samples",
              r2.min == 3 && r2.max == 9 && r2.count == 3, dump(r2));
        check("TS-08c", "mean of {5,3,9} is exactly 17/3",
              r2.mean == 17.0 / 3.0, dump(r2));

        Stat s3;
        add_sample(s3, 0);        // a genuine 0 µs sample (emulated nothing)
        add_sample(s3, 7);
        const auto r3 = tick_stats::summarize_stat(s3);
        check("TS-08d", "a genuine zero sample is a real minimum",
              r3.min == 0 && r3.max == 7, dump(r3));
    }

    // --- TS-09: every late interval counts ------------------------------------
    // The late tally is a count, not a flag: a window where most callbacks are
    // late (the reporter's machine) must say how many.
    {
        Counters c;
        for (int i = 0; i < 5; i++) add_interval(c, 40000, PERIOD_EVEN);
        add_interval(c, 20000, PERIOD_EVEN);
        const auto r = summarize(c);
        check("TS-09", "five of six intervals late counts exactly five",
              r.late == 5 && r.interval_us.count == 6, dump(r.interval_us));
    }

    // --- TS-10: the discriminative window — the reporter's machine ------------
    // The shape this instrument exists to expose, built from the issue-#9
    // numbers: ~35 callbacks/s instead of ~58, gaps of roughly two periods,
    // while the handler itself stays cheap. The report must show mechanism (a)
    // — many late, long max interval — and simultaneously clear mechanism (b):
    // handler max far below the period. A healthy window over the same period
    // shows zero lates. `late` separates the two; a bare callback count alone
    // would not say WHERE the missing time went.
    {
        Counters degraded;
        add_tick(degraded);
        // 16 on-time callbacks + 18 double-length gaps (~34400 µs ≈ 2 periods).
        for (int i = 0; i < 16; i++) {
            add_tick(degraded);
            add_interval(degraded, 17200, PERIOD_60);
        }
        for (int i = 0; i < 18; i++) {
            add_tick(degraded);
            add_interval(degraded, 34400, PERIOD_60);
        }
        for (int i = 0; i < 35; i++) add_sample(degraded.handler_us, 2000);
        const auto d = summarize(degraded);
        check("TS-10a", "degraded window: 35 ticks, 18 late callbacks",
              d.ticks == 35 && d.late == 18, dump(d.interval_us));
        check("TS-10b", "degraded window: max interval shows the missing slots",
              d.interval_us.max == 34400, dump(d.interval_us));
        check("TS-10c", "degraded window: handler stays far below the period",
              d.handler_us.valid && d.handler_us.max == 2000 &&
              d.handler_us.max < PERIOD_60, dump(d.handler_us));

        Counters healthy;
        add_tick(healthy);
        for (int i = 0; i < 57; i++) {
            add_tick(healthy);
            add_interval(healthy, 17200, PERIOD_60);
        }
        const auto h = summarize(healthy);
        check("TS-10d", "healthy window over the same period: zero late",
              h.ticks == 58 && h.late == 0, dump(h.interval_us));
    }

    std::printf("\n===================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
