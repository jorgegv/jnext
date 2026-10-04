// GUI frame-timer contract test (GH #155).
//
// WHAT IS UNDER TEST. gui/frame_timer.h: the GUI's frame timer is single-shot
// and is armed from NOW every time a tick hands over an interval — including
// when the value equals the previous one. Before GH #155 the timer was
// periodic and was restarted only when the interval changed, so after a tick
// that outlasted an unchanged interval the next tick fired at once on the old
// phase. On Windows that next tick is also dispatched ahead of the widget's
// low-priority repaint request, and the frame the last tick produced is never
// shown (frame_timer.h has the trace and the Qt sources). The ordering half is
// Windows-only and cannot be shown on this host (glib orders posted events
// first); the PHASE half can, on any platform, and it is what these rows pin:
//
//   FT-01  configure() gives the timer its contract (single-shot, precise).
//   FT-02  armed once and not re-armed, it fires exactly once — nothing can
//          fire during, or be queued behind, a tick that has not re-armed it.
//   FT-03  a tick that outlasts its interval and re-arms with the SAME value:
//          the next timeout comes a full interval after the re-arm, not at
//          once on the old phase.
//   FT-04  re-arming twice with the same value restarts from the second call.
//   FT-05  every arm leaves the timer running: a tick that arms cannot stop
//          the machine.
//   FT-06  the deadline schedule's 1 ms floor arms a zero timer (a host that
//          is behind pays no timer floor per tick).
//
// The timing rows assert LOWER bounds only (a timer never fires early), so a
// loaded host can delay a timeout but cannot fail a correct implementation.
//
// Qt Core only: a QCoreApplication event loop, no display.
// Run: ./build/test/frame_timer_test

#include "gui/frame_timer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>

#include <cstdint>
#include <cstdio>
#include <string>
#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

/// Burn `ms` of wall time inside the current handler, as a long tick does.
void busy_ms(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
    }
}

/// Run the event loop for `ms` (the bound every row uses to end itself).
void spin_ms(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    std::printf("frame_timer_test (GUI frame-timer contract, GH #155)\n");

    // FT-01
    {
        QTimer t;
        frame_timer::configure(t);
        check("FT-01", "configure(): single-shot and Qt::PreciseTimer",
              t.isSingleShot() && t.timerType() == Qt::PreciseTimer);
    }

    // FT-02 — a tick that never re-arms gets no second timeout.
    {
        QTimer t;
        frame_timer::configure(t);
        int fires = 0;
        QObject::connect(&t, &QTimer::timeout, [&]() { ++fires; });
        frame_timer::arm(t, 10);
        spin_ms(150);
        check("FT-02", "armed once and not re-armed: exactly one timeout in 150 ms",
              fires == 1, "fires=" + std::to_string(fires));
    }

    // FT-03 — the re-arm counts from now, even with an unchanged value.
    {
        QTimer t;
        frame_timer::configure(t);
        constexpr int IVL = 20;
        QElapsedTimer clock;
        clock.start();
        int     fires   = 0;
        int64_t rearm_at = -1, second_at = -1;
        QObject::connect(&t, &QTimer::timeout, [&]() {
            ++fires;
            if (fires == 1) {
                busy_ms(2 * IVL);              // a tick twice as long as its interval
                frame_timer::arm(t, IVL);      // ... re-armed with the SAME value
                rearm_at = clock.nsecsElapsed();
            } else if (fires == 2) {
                second_at = clock.nsecsElapsed();
            }
        });
        frame_timer::arm(t, IVL);
        spin_ms(400);
        const double gap_ms = (second_at >= 0 && rearm_at >= 0)
                                  ? static_cast<double>(second_at - rearm_at) / 1e6
                                  : -1.0;
        // A whole ms of slack for the clocks' granularity; the defect this
        // catches fires at ~0 ms (the old phase was already due).
        check("FT-03", "a long tick re-armed with the same interval: next timeout >= the interval after the re-arm",
              fires == 2 && gap_ms >= IVL - 1,
              "fires=" + std::to_string(fires) + " gap_ms=" + std::to_string(gap_ms));
    }

    // FT-04 — the second same-value arm restarts the countdown.
    {
        QTimer t;
        frame_timer::configure(t);
        frame_timer::arm(t, 60);
        busy_ms(30);
        frame_timer::arm(t, 60);
        const int remaining = t.remainingTime();
        check("FT-04", "re-armed with the same value after 30 ms: the countdown restarts (>= 55 ms left)",
              remaining >= 55, "remaining=" + std::to_string(remaining));
    }

    // FT-05 — every arm leaves the timer running.
    {
        QTimer t;
        frame_timer::configure(t);
        bool all_active = true;
        for (int k = 0; k < 5; k++) {
            frame_timer::arm(t, 5);                 // the same value, five times
            if (!t.isActive()) all_active = false;
        }
        check("FT-05", "each arm, same value or not, leaves the timer active", all_active);
    }

    // FT-06 — the schedule's 1 ms floor ("due now") arms a zero timer, which
    // fires once the event queue is drained instead of a whole ms later.
    {
        QTimer t;
        frame_timer::configure(t);
        frame_timer::arm(t, 1);
        const bool zero = t.isActive() && t.interval() == 0;
        frame_timer::arm(t, 2);
        const bool two = t.isActive() && t.interval() == 2;
        check("FT-06", "an interval at the 1 ms floor arms a zero timer; 2 ms and up are kept",
              zero && two, "interval=" + std::to_string(t.interval()));
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
