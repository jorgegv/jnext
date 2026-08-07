// Fractional frame-deadline scheduler test (GitHub issue #9 / Task 63).
//
// Like audio_pacing_test and present_cadence_test this file has NO VHDL
// oracle: it tests HOST pacing policy, not emulated hardware. Its oracle is
// the written specification in src/platform/frame_deadline.h — what the
// scheduler is defined to DO — and the timing constants in
// src/core/emulator_config.h. Every row is derived from that definition,
// never from reading the implementation back.
//
// WHY IT EXISTS. The frame QTimer interval was round(frame_period_ms()):
// a FIXED whole-ms value. The Next-60 period is 17.198 ms (481536 master
// cycles / 28 MHz), so a 17 ms timer runs 1.1% fast; the Next-50 period is
// 20.259 ms (567264 / 28 MHz), so a 20 ms timer runs 1.3% fast. Measured:
// --silent emulated 58.77-58.79 frames/s against the true 58.15, and with
// audio the pacing band corrected the drift by skipping ~1 tick/s — one
// visibly frozen frame per second on healthy machines.
//
// THE CONTRACT PINNED HERE. An absolute deadline advances by the EXACT
// fractional period each tick; the returned whole-ms interval is the delay
// from `now` to that deadline, so intervals alternate (17/18 averaging
// 17.198) and the long-run rate is exact. Lateness beyond STALL_PERIODS
// periods resyncs to now + period (audio pacing owns catch-up; the timer
// must never machine-gun 1 ms callbacks); lateness within the threshold is
// walked off on the ORIGINAL grid. A period change GLIDES: the anchored
// deadline stands and the next advance uses the new period.
//
// Run: ./build/test/frame_deadline_test

#include "platform/frame_deadline.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

// The true frame periods, integer microseconds, derived from
// src/core/emulator_config.h machine_timing() (28 MHz master clock):
//   Next 50 Hz: 1824 * 311 = 567264 cycles -> 20259.428571 us -> 20259 us
//   Next 60 Hz: 1824 * 264 = 481536 cycles -> 17197.714285 us -> 17197 us
// (integer-us quantisation is <= 0.5 us/frame, < 0.003% — negligible next
// to the 1.1-1.3% error this scheduler exists to remove).
constexpr int64_t P60 = 17197;   // Next-60 (the task-stated 17197 us case)
constexpr int64_t P50 = 20259;   // Next-50, true period from timing constants

// Simulated host: a Qt PRECISE repeating timer plus an instantaneous tick
// handler, modelled exactly as the wiring uses the scheduler:
//   * rebase() anchors the schedule and starts the timer with the returned
//     interval;
//   * the timer fires at the scheduled instant (plus optional arrival
//     jitter);
//   * the handler calls next_interval_ms(now) and the next fire is
//     scheduled `interval` ms after `now` — QTimer::setInterval restarts
//     the period from the moment of the call.
// (The real wiring skips setInterval when the value is unchanged; Qt then
// fires on its own grid anchored at the previous fire, a sub-ms difference
// the next recompute-against-now absorbs. The simulation models the
// restart-from-now case, which is the contract the class documents.)
struct Sim {
    frame_deadline::Scheduler sched;
    int64_t now = 0;        // arrival time of the most recent tick, us
    int64_t scheduled = 0;  // next fire instant, us

    void start(int64_t period_us, int64_t t0 = 0)
    {
        now = t0;
        const int iv = sched.rebase(now, period_us);
        scheduled = now + static_cast<int64_t>(iv) * 1000;
    }

    // Fire the next tick (arrival = scheduled + jitter); returns the interval.
    int tick(int64_t period_us, int64_t jitter_us = 0)
    {
        now = scheduled + jitter_us;
        const int iv = sched.next_interval_ms(now, period_us);
        scheduled = now + static_cast<int64_t>(iv) * 1000;
        return iv;
    }
};

std::string fmt(long long v) { char b[64]; std::snprintf(b, sizeof b, "%lld", v); return b; }

}  // namespace

int main()
{
    std::printf("Frame-deadline scheduler tests (issue #9 / Task 63)\n");
    std::printf("====================================================\n");

    // --- FD-01: exactness at the Next-60 period --------------------------------
    // 1000 ideal ticks at 17197 us. The old fixed-17 ms timer finishes 1000
    // ticks in 17000 ms — 197.7 ms (1.1%) early. The deadline schedule must
    // land within 1 ms of 1000 * 17.197 ms, and must never drift from the
    // ideal grid at ANY tick (drift bounded by the 0.5 ms rounding, not
    // accumulating).
    {
        Sim s;
        s.start(P60);
        int64_t max_dev = 0;
        for (int k = 1; k <= 1000; k++) {
            s.tick(P60);
            const int64_t dev = s.now - k * P60;
            const int64_t a = dev < 0 ? -dev : dev;
            if (a > max_dev) max_dev = a;
        }
        check("FD-01a", "1000 ticks at 17197us total within 1ms of 1000 periods",
              s.now >= 1000 * P60 - 1000 && s.now <= 1000 * P60 + 1000,
              "total=" + fmt(s.now));
        check("FD-01b", "no tick ever drifts more than 1ms from the ideal grid",
              max_dev <= 1000, "max_dev=" + fmt(max_dev));
    }

    // --- FD-02: exactness at the true Next-50 period ---------------------------
    // Same property at 20259 us (567264 master cycles / 28 MHz — the true
    // 50 Hz period from emulator_config.h; the old fixed timer used 20 ms,
    // 1.3% fast).
    {
        Sim s;
        s.start(P50);
        int64_t max_dev = 0;
        for (int k = 1; k <= 1000; k++) {
            s.tick(P50);
            const int64_t dev = s.now - k * P50;
            const int64_t a = dev < 0 ? -dev : dev;
            if (a > max_dev) max_dev = a;
        }
        check("FD-02a", "1000 ticks at 20259us total within 1ms of 1000 periods",
              s.now >= 1000 * P50 - 1000 && s.now <= 1000 * P50 + 1000,
              "total=" + fmt(s.now));
        check("FD-02b", "no tick ever drifts more than 1ms from the ideal grid",
              max_dev <= 1000, "max_dev=" + fmt(max_dev));
    }

    // --- FD-03: alternation at 17197 us ---------------------------------------
    // Intervals may only be 17 or 18 ms, and the 18s appear in the ratio of
    // the fractional part: frac(17.197) = 0.197 -> ~197.7 per 1000, i.e. the
    // ~80:20 mix that averages 17.197.
    {
        Sim s;
        s.start(P60);
        int n17 = 0, n18 = 0, other = 0;
        for (int k = 0; k < 1000; k++) {
            const int iv = s.tick(P60);
            if (iv == 17) ++n17;
            else if (iv == 18) ++n18;
            else ++other;
        }
        check("FD-03a", "intervals at 17197us are only ever 17 or 18 ms",
              other == 0, "other=" + fmt(other));
        check("FD-03b", "18ms share matches the fractional part (~197/1000)",
              n18 >= 190 && n18 <= 206, "n18=" + fmt(n18));
    }

    // --- FD-04: alternation at 20259 us ---------------------------------------
    // frac(20.259) = 0.259 -> ~259 intervals of 21 ms per 1000.
    {
        Sim s;
        s.start(P50);
        int n20 = 0, n21 = 0, other = 0;
        for (int k = 0; k < 1000; k++) {
            const int iv = s.tick(P50);
            if (iv == 20) ++n20;
            else if (iv == 21) ++n21;
            else ++other;
        }
        check("FD-04a", "intervals at 20259us are only ever 20 or 21 ms",
              other == 0, "other=" + fmt(other));
        check("FD-04b", "21ms share matches the fractional part (~259/1000)",
              n21 >= 250 && n21 <= 270, "n21=" + fmt(n21));
    }

    // --- FD-05: jitter immunity ------------------------------------------------
    // Callbacks arriving +-3 ms around each scheduled fire. Because every
    // interval is recomputed against actual `now`, jitter must not
    // accumulate: every arrival stays within jitter + rounding (4 ms) of the
    // ideal grid, including the last.
    {
        Sim s;
        s.start(P60);
        uint32_t rng = 12345u;
        int64_t max_dev = 0;
        for (int k = 1; k <= 1000; k++) {
            rng = rng * 1664525u + 1013904223u;
            const int64_t jitter = static_cast<int64_t>(rng % 6001u) - 3000;
            s.tick(P60, jitter);
            const int64_t dev = s.now - k * P60;
            const int64_t a = dev < 0 ? -dev : dev;
            if (a > max_dev) max_dev = a;
        }
        const int64_t final_dev = s.now - 1000 * P60;
        check("FD-05a", "+-3ms arrival jitter: every arrival within 4ms of the grid",
              max_dev <= 4000, "max_dev=" + fmt(max_dev));
        check("FD-05b", "+-3ms arrival jitter: no cumulative drift after 1000 ticks",
              final_dev >= -4000 && final_dev <= 4000, "final_dev=" + fmt(final_dev));
        check("FD-05c", "+-3ms jitter never trips the stall policy",
              s.sched.resyncs() == 0);
    }

    // --- FD-06: stall resync ---------------------------------------------------
    // A 500 ms gap (host stall / suspend) is far beyond STALL_PERIODS (2)
    // periods: the schedule must resync to now + period — exactly one
    // resync, next interval ~one period, and NO burst of 1 ms intervals.
    {
        Sim s;
        s.start(P60);
        for (int k = 0; k < 100; k++) s.tick(P60);
        check("FD-06a", "ideal running never resyncs", s.sched.resyncs() == 0);

        const int late_iv = s.tick(P60, 500000);  // arrives 500 ms late
        const int64_t resync_at = s.now;
        check("FD-06b", "500ms gap causes exactly one resync",
              s.sched.resyncs() == 1);
        check("FD-06c", "the resynced tick schedules ~one period ahead, not 1ms",
              late_iv == 17, "iv=" + fmt(late_iv));

        bool all_normal = true;
        for (int k = 0; k < 100; k++) {
            const int iv = s.tick(P60);
            if (iv != 17 && iv != 18) all_normal = false;
        }
        const int64_t dev = s.now - (resync_at + 100 * P60);
        check("FD-06d", "after the resync: no 1ms burst, intervals normal",
              all_normal);
        check("FD-06e", "after the resync: the grid is re-anchored at the stall",
              dev >= -1000 && dev <= 1000, "dev=" + fmt(dev));
    }

    // --- FD-07: lateness WITHIN the threshold walks off on the original grid ---
    // A tick 1.5 periods late is jitter, not a stall: no resync, the
    // returned interval clamps to 1 ms (catch-up), and within a few ticks
    // arrivals are back on the ORIGINAL grid — the anchor is preserved, so
    // the long-run rate is unaffected by the disturbance.
    {
        Sim s;
        s.start(P60);
        for (int k = 0; k < 10; k++) s.tick(P60);

        const int late_iv = s.tick(P60, (3 * P60) / 2);  // 1.5 periods late
        check("FD-07a", "1.5 periods late: NOT a stall, no resync",
              s.sched.resyncs() == 0);
        check("FD-07b", "1.5 periods late: interval clamps to 1ms (never 0/negative)",
              late_iv == 1, "iv=" + fmt(late_iv));

        // 10 more ideal-schedule ticks; the last must sit on the original
        // grid: 21 ticks have fired in total (10 + 1 late + 10), and tick k
        // arrives at deadline k*P, so the last arrival is ~21 periods from
        // the anchor.
        bool tail_normal = true;
        int iv = 0;
        for (int k = 0; k < 10; k++) {
            iv = s.tick(P60);
            if (k >= 5 && iv != 17 && iv != 18) tail_normal = false;
        }
        const int64_t dev = s.now - 21 * P60;
        check("FD-07c", "catch-up re-locks to the ORIGINAL grid within a few ticks",
              dev >= -1000 && dev <= 1000, "dev=" + fmt(dev));
        check("FD-07d", "post-catch-up intervals return to the normal 17/18 band",
              tail_normal);
    }

    // --- FD-08: the stall threshold is strictly "more than 2 periods" ----------
    // Direct boundary probe, no simulation: anchor at 0, deadline = P. A
    // call exactly 2 periods past the deadline glides (walk-off); one more
    // microsecond resyncs.
    {
        frame_deadline::Scheduler at, over;
        at.rebase(0, P60);
        const int iv_at = at.next_interval_ms(P60 + 2 * P60, P60);
        check("FD-08a", "exactly 2 periods past the deadline: no resync, clamped walk-off",
              at.resyncs() == 0 && iv_at == 1, "iv=" + fmt(iv_at));

        over.rebase(0, P60);
        const int iv_over = over.next_interval_ms(P60 + 2 * P60 + 1, P60);
        check("FD-08b", "one us beyond 2 periods: resyncs, ~one period ahead",
              over.resyncs() == 1 && iv_over == 17, "iv=" + fmt(iv_over));
    }

    // --- FD-09: a period change GLIDES ----------------------------------------
    // Documented choice: the anchored deadline stands and the next advance
    // uses the new period — no rebase, no resync, no discontinuity. After
    // 100 ticks at P60 the deadline is exactly 101*P60; 500 ticks at P50
    // later the arrival must sit within 1 ms of 101*P60 + 500*P50.
    {
        Sim s;
        s.start(P60);
        for (int k = 0; k < 100; k++) s.tick(P60);

        const int first_iv = s.tick(P50);  // the 50/60 switch happens here
        check("FD-09a", "period switch causes no resync", s.sched.resyncs() == 0);
        check("FD-09b", "first interval after the switch is ~one NEW period",
              first_iv == 20 || first_iv == 21, "iv=" + fmt(first_iv));

        bool all_new_band = true;
        for (int k = 0; k < 499; k++) {
            const int iv = s.tick(P50);
            if (iv != 20 && iv != 21) all_new_band = false;
        }
        // 500 P50 ticks fired; tick N arrives at ITS deadline (before its
        // own advance), so the last arrival is 101*P60 + 499*P50 from t0.
        const int64_t expect = 101 * P60 + 499 * P50;
        const int64_t dev = s.now - expect;
        check("FD-09c", "post-switch intervals are in the new 20/21 band",
              all_new_band);
        check("FD-09d", "the glide is continuous: deadline anchor is preserved",
              dev >= -1000 && dev <= 1000, "dev=" + fmt(dev));

        // And the reverse switch (50 -> 60) glides identically.
        bool back_band = true;
        for (int k = 0; k < 100; k++) {
            const int iv = s.tick(P60);
            if (iv != 17 && iv != 18) back_band = false;
        }
        check("FD-09e", "the reverse switch glides into the 17/18 band", back_band);
    }

    // --- FD-10: speed-multiplier periods --------------------------------------
    // 0.5x of Next-60 -> 34395 us (llround(17197.714*2)); 4x -> 4299 us
    // (llround(17197.714/4)). Same exactness and band properties.
    {
        Sim s;
        s.start(34395);
        bool band = true;
        for (int k = 0; k < 1000; k++) {
            const int iv = s.tick(34395);
            if (iv != 34 && iv != 35) band = false;
        }
        const int64_t dev = s.now - 1000LL * 34395;
        check("FD-10a", "0.5x speed (34395us): intervals only 34/35 ms", band);
        check("FD-10b", "0.5x speed: 1000-tick total within 1ms",
              dev >= -1000 && dev <= 1000, "dev=" + fmt(dev));
    }
    {
        Sim s;
        s.start(4299);
        bool band = true;
        for (int k = 0; k < 1000; k++) {
            const int iv = s.tick(4299);
            if (iv != 4 && iv != 5) band = false;
        }
        const int64_t dev = s.now - 1000LL * 4299;
        check("FD-10c", "4x speed (4299us): intervals only 4/5 ms", band);
        check("FD-10d", "4x speed: 1000-tick total within 1ms",
              dev >= -1000 && dev <= 1000, "dev=" + fmt(dev));
    }

    // --- FD-11: the interval never goes below 1 ms ----------------------------
    // A sub-ms period (10x speed would be ~1.7 ms, but be adversarial) must
    // clamp every returned interval to 1 ms — Qt interval 0 would spin the
    // event loop. rebase() clamps identically.
    {
        Sim s;
        s.start(300);
        bool all_ge1 = true, all_eq1 = true;
        for (int k = 0; k < 100; k++) {
            const int iv = s.tick(300);
            if (iv < 1) all_ge1 = false;
            if (iv != 1) all_eq1 = false;
        }
        check("FD-11a", "300us period: every interval clamps to >= 1ms",
              all_ge1);
        check("FD-11b", "300us period: the clamp floor IS 1ms (not higher)",
              all_eq1);
        frame_deadline::Scheduler r;
        check("FD-11c", "rebase clamps identically (300us -> 1ms)",
              r.rebase(0, 300) == 1);
    }

    // --- FD-12: rebase semantics ----------------------------------------------
    // rebase() anchors at now + period and returns ~one period; it is an
    // explicit restart, NOT a stall (resync counter untouched), and the new
    // grid runs from the rebase instant.
    {
        frame_deadline::Scheduler r;
        check("FD-12a", "rebase returns the rounded period (17197us -> 17ms)",
              r.rebase(0, P60) == 17);
        check("FD-12b", "rebase returns the rounded period (20259us -> 20ms)",
              r.rebase(0, P50) == 20);

        Sim s;
        s.start(P60);
        for (int k = 0; k < 50; k++) s.tick(P60);
        const uint64_t resyncs_before = s.sched.resyncs();
        // 10 s of silence (paused in a menu), then an explicit re-anchor.
        const int64_t t0 = s.now + 10'000'000;
        s.start(P60, t0);  // rebase at t0
        check("FD-12c", "rebase after long silence is not counted as a stall",
              s.sched.resyncs() == resyncs_before);
        bool band = true;
        for (int k = 0; k < 100; k++) {
            const int iv = s.tick(P60);
            if (iv != 17 && iv != 18) band = false;
        }
        const int64_t dev = s.now - (t0 + 100 * P60);
        check("FD-12d", "after rebase the grid runs from the rebase instant",
              band && dev >= -1000 && dev <= 1000, "dev=" + fmt(dev));
    }

    // --- FD-13: resync_now, the issue-#35 video-preference catch-up ----------
    // The frontend declined to run the pacer's second frame and wants the next
    // frame sooner instead. resync_now() anchors the schedule at now, so the
    // timer fires at its floor and the grid restarts from that instant.
    {
        frame_deadline::Scheduler s;
        s.rebase(0, P60);
        const uint64_t stalls_before = s.resyncs();
        const int iv = s.resync_now(100'000);
        check("FD-13a", "resync_now returns the 1ms floor: the next tick is due now",
              iv == 1, "iv=" + fmt(iv));
        check("FD-13b", "a requested re-anchor is not counted as a stall resync",
              s.resyncs() == stalls_before, "resyncs=" + fmt((long long)s.resyncs()));
        check("FD-13c", "the grid restarts from the resync instant",
              s.next_interval_ms(100'000, P60) == 17);
    }

    // A host that needs 1.67 periods per tick — ~60% of real time, the regime
    // the video preference exists for. ADVANCING the deadline on such a tick
    // leaves it in the past and every later tick inherits the lateness, until
    // the stall arm fires and parks the machine for a whole period. That
    // periodic idle IS judder, which is what the preference is trying to
    // remove, so the catch-up must re-anchor instead.
    {
        constexpr int TICKS = 60;
        const int64_t cost = P60 * 167 / 100;

        struct Out { int idle = 0; uint64_t stalls = 0; int64_t wall = 0; };
        auto simulate = [&](bool use_resync) {
            Out o;
            frame_deadline::Scheduler s;
            int64_t now = 0;
            int64_t scheduled = now + static_cast<int64_t>(s.rebase(now, P60)) * 1000;
            for (int k = 0; k < TICKS; k++) {
                now = scheduled + cost;   // the tick fires, then costs `cost`
                const int iv = use_resync ? s.resync_now(now)
                                          : s.next_interval_ms(now, P60);
                if (iv > 1) ++o.idle;     // a wait on a machine already behind
                scheduled = now + static_cast<int64_t>(iv) * 1000;
            }
            o.stalls = s.resyncs();
            o.wall   = now;
            return o;
        };

        const Out advanced = simulate(false);
        const Out anchored = simulate(true);

        check("FD-13d", "advancing on a 60%-speed host parks it and counts stalls",
              advanced.idle > 0 && advanced.stalls > 0,
              "idle=" + fmt(advanced.idle) + " stalls=" + fmt((long long)advanced.stalls));
        check("FD-13e", "resync_now never parks it and never counts a stall",
              anchored.idle == 0 && anchored.stalls == 0,
              "idle=" + fmt(anchored.idle) + " stalls=" + fmt((long long)anchored.stalls));
        check("FD-13f", "so the same frames finish sooner: no idle periods inserted",
              anchored.wall < advanced.wall,
              "anchored=" + fmt(anchored.wall) + " advanced=" + fmt(advanced.wall));
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
