#pragma once

#include <cstdint>

/// "The CPU is polling the tape" (GH #395, automatic tape play/stop).
///
/// The signal is every CPU `IN` of the ULA port (0xFE), with the T-state it
/// samples the tape at. A loader that measures pulses reads the port faster
/// than the shortest standard pulse (855 T); the ROM's widest gap between
/// samples is about 400 T. Anything that reads the port less often than
/// every GAP T-states, in bursts shorter than MIN_RUN reads (the ROM's
/// keyboard scan: bursts of 8 reads 59 T apart, then about 69000 T of
/// nothing), is not listening to the tape.
///
/// No PC range and no register heuristic: those serve only ROM loaders, and
/// FUSE's "B changes by one" rule cannot see a pure-EAR poll (DeciLoad polls
/// at one address with B constant).
class TapePollDetector {
public:
    static constexpr uint64_t GAP     = 1000;   // T-states between reads of one run
    static constexpr uint32_t MIN_RUN = 32;     // reads in a row that mean "polling"

    void reset() { valid_ = false; run_ = 0; last_ = 0; }

    /// One CPU read of the ULA port, sampling the tape at `ts`. A `ts` before
    /// the previous read (a state load moved time backwards) starts a new run.
    void on_read(uint64_t ts) {
        if (valid_ && ts >= last_ && ts - last_ <= GAP) ++run_;
        else run_ = 1;
        last_ = ts;
        valid_ = true;
    }

    /// True while a run of MIN_RUN reads or more is current at `now`.
    bool polling(uint64_t now) const {
        return valid_ && run_ >= MIN_RUN && now >= last_ && now - last_ <= GAP;
    }

private:
    bool     valid_ = false;
    uint32_t run_   = 0;
    uint64_t last_  = 0;
};
