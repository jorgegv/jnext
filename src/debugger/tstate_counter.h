#pragma once

// GH #369 -- the debugger CPU panel's "T-states since the last stop" counter.
//
// The backend clock is Debugger::time().tstates_total (INS-07), a monotonic
// total. WHERE a count starts is a frontend choice, as in ZRCP's partial
// counter (zrcp_server.cpp keeps its own base): so the start point and the
// Accumulate policy live here, pure (no Qt, no I/O), and the panel feeds it.
//
// A "stop" is any paused observation, `observe(now, loads)`, where `now` is
// Time::tstates_total and `loads` is Debugger::program_loads().
//
//   * not accumulating: value = T-states since the previous stop;
//   * accumulating: value = T-states since Accumulate was ticked (or since the
//     last restart);
//   * a stop seen at the same time as the last one changes nothing (a redraw
//     with no time moved);
//   * the count restarts (value 0, counting from `now`) when the machine was
//     never seen, when a program load happened since the last stop (`loads`
//     moved), or when time went BACKWARDS (`now` below the last stop): Step
//     Back, source Step Back, Reverse Continue, another client's rewind, a cold
//     boot seen while paused. Those backward verbs need no explicit hook: their
//     landings are strictly earlier than the stop before them.
//   * discontinuities this net cannot see (a cold boot while running, a rewind
//     jumping FORWARD through the ring, the window opening) are pushed with an
//     explicit restart() by the panel's owner.

#include <cstdint>

namespace jnext {

class TstateCounter {
public:
    /// The machine is seen STOPPED at `now`.
    void observe(uint64_t now, uint64_t loads)
    {
        if (!seen_ || loads != loads_ || now < last_) {   // loads moved or time went backwards
            restart(now, loads);
            return;
        }
        if (now == last_) return;
        if (!accumulate_) anchor_ = last_;
        value_ = now - anchor_;
        last_ = now;
    }

    /// Count from `now`: the value is 0 and the next stop counts from here.
    void restart(uint64_t now, uint64_t loads)
    {
        seen_ = true;
        anchor_ = last_ = now;
        value_ = 0;
        loads_ = loads;
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
    uint64_t loads_ = 0;
};

} // namespace jnext
