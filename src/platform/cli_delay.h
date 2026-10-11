#pragma once

// A command-line delay (--delayed-screenshot*, --delayed-automatic-exit*,
// --delayed-keypress*, --delayed-nmi*): either a frame count or a number of
// EMULATED seconds. One implementation shared by the three frontends.
//
// "N seconds" is emulated time at the machine's nominal rate: a frame run at
// 50 Hz counts 1/50 s, one run at 60 Hz counts 1/60 s, and the delay comes due
// at the first frame boundary at which the total reaches N. A machine that
// stays at 50 Hz therefore fires on frame N*50 exactly; one that stays at 60 Hz
// on frame N*60; a program that switches with NR 0x05 is timed in its own
// seconds on each side of the switch (GH #320: the rate is always 50 Hz at
// startup, so converting once, when the option is read, could never see it).
//
// Time is held in 1/300 s units (the LCM of 50 and 60), so a 50 Hz frame is 6
// units and a 60 Hz frame 5, with no rounding drift across a switch.

#include <cstdint>

namespace cli {

class Delay {
public:
    static Delay frames(int n)  { return Delay(n, false); }
    static Delay seconds(int n) { return Delay(static_cast<int64_t>(n) * UNITS_PER_SECOND, true); }

    bool armed()   const { return left_ >= 0; }
    bool due()     const { return left_ == 0; }
    bool pending() const { return left_ > 0; }
    void disarm()        { left_ = -1; }

    /// One frame elapsed, run at `refresh_60hz`. A no-op unless pending.
    void tick(bool refresh_60hz) {
        if (left_ <= 0) return;
        left_ -= step(refresh_60hz);
        if (left_ < 0) left_ = 0;
    }

    /// Frames still to run at the given refresh rate (for messages).
    int64_t frames_left(bool refresh_60hz) const {
        if (left_ <= 0) return 0;
        const int64_t s = step(refresh_60hz);
        return (left_ + s - 1) / s;
    }

    Delay() = default;

private:
    static constexpr int64_t UNITS_PER_SECOND = 300;
    Delay(int64_t left, bool secs) : left_(left), seconds_(secs) {}
    int64_t step(bool refresh_60hz) const {
        if (!seconds_) return 1;
        return refresh_60hz ? UNITS_PER_SECOND / 60 : UNITS_PER_SECOND / 50;
    }

    int64_t left_    = -1;
    bool    seconds_ = false;
};

}  // namespace cli
