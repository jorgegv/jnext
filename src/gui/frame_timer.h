#pragma once

#include <QTimer>

/// The GUI frame timer's contract (GH #155).
///
/// QtApp drives emulation from one QTimer and paces it with the fractional
/// deadline schedule in platform/frame_deadline.h, which hands back "whole ms
/// from NOW to the next deadline" at the end of every tick. That value only
/// means what it says if the timer is armed from NOW every time it is handed
/// over. These two functions are the whole of that contract, kept apart from
/// QtApp so frame_timer_test can drive a real QTimer without a display.
///
/// WHY SINGLE-SHOT, AND WHY ARM UNCONDITIONALLY. Until GH #155 the timer was
/// periodic and was restarted only when the interval CHANGED, on the theory
/// that a same-value restart was "harmless churn". Skipping it is not harmless:
/// the periodic timer then keeps the phase of an earlier tick, so when a tick
/// takes longer than the interval it has already fired during the tick. On
/// Windows that is a lost frame, every time:
///
///   * a Qt::PreciseTimer is a timeSetEvent(TIME_PERIODIC) timer whose callback
///     POSTS a QTimerEvent at normal priority (qeventdispatcher_win.cpp,
///     qt_fast_timer_proc / registerTimer);
///   * QWidget::update() posts its UpdateRequest at Qt::LowEventPriority
///     (qwidgetrepaintmanager.cpp, sendUpdateRequest), and the posted-event
///     queue is priority-sorted (QPostEventList::addEvent);
///   * so the next tick is dispatched BEFORE the repaint of the frame the last
///     tick produced, overwrites the framebuffer, and that frame is never
///     shown.
///
/// Traced under wine (real qwindows platform): 118 of 118 ticks ending with an
/// unchanged interval lost their frame, 422 of 422 restarted ones were shown.
/// It is janko-jj's "49 emulated / 37 shown" on real Windows, and on a host at
/// the edge of real time, where the interval is clamped to 1 ms on every tick
/// and therefore never "changes", it lost every other frame: the shown rate
/// halved while the emulated rate held. Linux never showed it, because the
/// glib dispatcher demotes a timer that has just fired below posted events
/// until they have been sent (qeventdispatcher_glib.cpp, runWithIdlePriority),
/// so the paint always got in between.
///
/// A single-shot timer armed at the end of each tick cannot fire, or have an
/// event queued, while a tick runs, and arming from now is exactly the
/// semantic the deadline schedule assumes.
///
/// THE CALLER'S OBLIGATION. Single-shot means a tick that ends without arming
/// stops the machine. QtApp arms at the end of every tick (the sequencer's step
/// 15) and at every re-anchor (rebase), and checks after each tick that the
/// timer is armed, re-anchoring if a path ever ended one without it.
namespace frame_timer {

/// Give a newly created frame timer its contract: millisecond-precise (the
/// default CoarseTimer may fire up to 5% early or late, which would swallow the
/// 17/18 ms alternation of the deadline schedule) and single-shot.
inline void configure(QTimer& timer)
{
    timer.setTimerType(Qt::PreciseTimer);
    timer.setSingleShot(true);
}

/// Arm the timer to fire `ms` milliseconds from NOW. Unconditional by design:
/// QTimer::start() restarts a running timer, and the same value twice in a row
/// must still mean "from now" (see above).
///
/// `ms` <= 1 is the deadline schedule's floor: the next deadline is due now or
/// within the next ms (frame_deadline.h, interval_to). That arms a ZERO
/// timer, which Qt fires "as soon as all the events in the window system's
/// event queue have been processed" (QTimer docs) — input and paints first,
/// then the tick. A 1 ms timer instead charged every tick of a host that is
/// behind (or fast-forwarding at --speed above what it can keep up with) the
/// timer's floor plus its wake-up latency: -4.7% ticks/s at --speed 1000 in
/// review. It cannot spin: the schedule only reports the floor when a tick
/// has done its work and the next deadline is already due, and every tick
/// advances the deadline by a full period. Running up to ~1 ms early when the
/// deadline is <= 1.5 ms away cannot drift the rate either — the deadlines are
/// absolute, so the next interval is simply that much longer. On Windows a
/// zero timer is a posted QZeroTimerEvent at normal priority, so it too can
/// run ahead of the widget's low-priority repaint request; that is exactly the
/// case QtApp's tick-entry flush_pending_present() paints synchronously.
inline void arm(QTimer& timer, int ms)
{
    timer.start(ms <= 1 ? 0 : ms);
}

}  // namespace frame_timer
