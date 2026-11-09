#pragma once
#include "platform/frame_deadline.h"
#include <cstdint>

// The browser frontend's requestAnimationFrame callback policy, with no
// Emscripten include so a native test drives it with a fake clock.
//
// SdlApp::tick() never sleeps; it reports the sleep it wants before the next
// tick. A rAF callback cannot sleep either, so that sleep becomes the earliest
// moment the next tick may start, kept on an absolute GRID: due += (the tick's
// own cost + the sleep), the tick's own period. A rAF callback that arrives
// before `due` runs no tick (the canvas keeps the last frame). Why a grid and
// not due = t_after + sleep: at 60 Hz rAF and a 20 ms period that lands just
// after one rAF, so ticks fall on every second rAF (30/s); the grid gives 5
// ticks per 6 rAFs (50/s) on 60, 120 and 144 Hz displays alike.
//
// One grid tick per callback: after a tick that slept, the loop runs ANOTHER tick
// in the same callback only if a whole slot was already overdue when the callback
// STARTED (due <= start). The browser composites once per callback, so a second
// tick in it presents two frames and shows one; content slower than the display
// (49.36 or 58.15 Hz on 60 Hz) never has a whole slot overdue at a callback's
// start in steady state, content faster than the display (30 Hz rAF, --speed 200)
// always does and runs its slots, so the rate is unchanged.
//
// A stall (a hidden tab, a long warm start) resyncs per frame_deadline.h: one
// tick at once, then the grid, never a burst.
namespace browser_pacing {

/// Upper bound on one rAF callback's work, so a catch-up burst cannot hold the
/// main thread for several frames. The first due tick always runs.
inline constexpr int64_t CALLBACK_BUDGET_US = 8000;

/// Diagnostics, filled by run_callback and the frontend's tick lambda; nothing
/// in the pacing decisions reads them. Index 3 means "3 or more".
struct PacingStats {
    uint64_t callbacks_by_ticks[4] = {0, 0, 0, 0};   // callbacks that ran 0, 1, 2, 3+ ticks
    uint64_t ticks_by_frames[4]    = {0, 0, 0, 0};   // ticks that ran 0, 1, 2, 3+ frames

    static int bucket(uint64_t n) { return n > 3 ? 3 : static_cast<int>(n); }
    void on_callback(uint64_t ticks) { ++callbacks_by_ticks[bucket(ticks)]; }
    void on_tick(uint64_t frames) { ++ticks_by_frames[bucket(frames)]; }
};

class Gate {
public:
    bool    anchored() const { return anchored_; }
    int64_t due_us() const { return due_us_; }
    int     resyncs() const { return resyncs_; }

    PacingStats stats;

    // Used by run_callback.
    bool    anchored_ = false;
    int64_t due_us_ = 0;
    int     resyncs_ = 0;
};

enum class Result { Continue, End };

/// One rAF callback. `now` returns the monotonic time in microseconds,
/// `tick(sleep_ms)` runs one SdlApp::tick and returns false when the loop must
/// end, `fastload()` tells whether a sleep of 0 may keep ticking in this
/// callback. Returns End as soon as a tick says so.
template <class Now, class Tick, class Fast>
Result run_callback(Gate& g, Now now, Tick tick, Fast fastload)
{
    const int64_t start = now();
    if (!g.anchored_) { g.anchored_ = true; g.due_us_ = start; }
    int ticks = 0;
    const auto done = [&](Result r) { g.stats.on_callback(static_cast<uint64_t>(ticks)); return r; };
    for (;;) {
        const int64_t before = now();
        if (before < g.due_us_) return done(Result::Continue);
        uint32_t s = 0;
        const bool more = tick(s);
        ++ticks;
        const int64_t after = now();
        if (!more) return done(Result::End);
        const int64_t cost = after - before;
        if (s > 0) {
            const int64_t period = cost + static_cast<int64_t>(s) * 1000;
            if (after - g.due_us_ > frame_deadline::STALL_PERIODS * period) {
                g.due_us_ = after;
                ++g.resyncs_;
                return done(Result::Continue);   // one tick now, the next on its own rAF: never a burst
            }
            g.due_us_ += period;
            // One grid tick per callback: a second only if a whole slot was already overdue when
            // this callback started (the browser shows one picture per callback).
            if (g.due_us_ > start) return done(Result::Continue);
        } else {
            g.due_us_ = after;
            if (!fastload()) return done(Result::Continue);
        }
        if (after - start >= CALLBACK_BUDGET_US) return done(Result::Continue);
    }
}

}  // namespace browser_pacing
