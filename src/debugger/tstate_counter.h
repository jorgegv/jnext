#pragma once

// GH #369 -- the debugger CPU panel's "T-states since the last stop" counter.
//
// The backend clock is Debugger::time().tstates_total (INS-07), a monotonic
// total. WHERE a count starts is a frontend choice, as in ZRCP's partial
// counter (zrcp_server.cpp keeps its own base): so the start point and the
// Accumulate policy live here, pure (no Qt, no I/O), and the panel feeds it.
//
// A "stop" is any paused observation, `observe(now, epoch)`, where `now` is
// Time::tstates_total and `epoch` is Debugger::time_epoch().
//
//   * not accumulating: value = T-states since the previous stop;
//   * accumulating: value = T-states since Accumulate was ticked (or since the
//     last restart);
//   * a stop seen at the same time as the last one changes nothing (a redraw
//     with no time moved);
//   * the count restarts (value 0, counting from `now`) when the machine was
//     never seen, or when `epoch` (Debugger::time_epoch()) moved since the last
//     stop: time was moved without executing, by anyone: a rewind (either
//     direction), Step Back, a state load, a cold boot, an in-place load. One
//     mechanism for all of them, whoever caused them.
//   * what epoch cannot say is WHERE the count starts for a restart seen only
//     at a later stop (a cold boot while running: the count is wanted from the
//     reset, not from the next stop), and the window opening (nothing is
//     observed while it is closed): the panel's owner pushes restart() there.

#include <cstdint>

namespace jnext {

class TstateCounter {
public:
    /// The machine is seen STOPPED at `now` (Time::tstates_total), with
    /// `epoch` = Debugger::time_epoch().
    void observe(uint64_t now, uint64_t epoch)
    {
        if (!seen_ || epoch != epoch_) {
            restart(now, epoch);
            return;
        }
        if (now == last_) return;
        if (!accumulate_) anchor_ = last_;
        value_ = now - anchor_;
        last_ = now;
    }

    /// Count from `now`: the value is 0 and the next stop counts from here.
    void restart(uint64_t now, uint64_t epoch)
    {
        seen_ = true;
        anchor_ = last_ = now;
        value_ = 0;
        epoch_ = epoch;
    }

    /// Ticking starts a new total at the current stop; unticking keeps the
    /// total on show until the next stop, which then shows only its own delta.
    void set_accumulate(bool on)
    {
        if (on == accumulate_) return;
        accumulate_ = on;
        anchor_ = last_;
        if (on) value_ = 0;
    }

    bool accumulate() const { return accumulate_; }
    uint64_t value() const { return value_; }

private:
    bool seen_ = false;
    bool accumulate_ = false;
    uint64_t anchor_ = 0;
    uint64_t last_ = 0;
    uint64_t value_ = 0;
    uint64_t epoch_ = 0;
};

} // namespace jnext
