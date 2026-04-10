#pragma once

#include <cstdint>
#include "core/emulator_config.h"

/// 28 MHz master clock with derived clock-enable signals.
///
/// The ZX Spectrum Next uses a 28 MHz master clock from which all derived
/// clocks are generated:
///   - 7 MHz pixel clock  : every 4 master cycles  (÷4)
///   - 3.5 MHz CPU clock  : every 8 master cycles  (÷8, default speed)
///   - 7 MHz CPU clock    : every 4 master cycles  (÷4)
///   - 14 MHz CPU clock   : every 2 master cycles  (÷2)
///   - 28 MHz CPU clock   : every master cycle     (÷1)
///
/// cpu_enable() and pixel_enable() return true on the cycle that
/// represents the rising edge of the respective derived clock.
class Clock {
public:
    Clock();

    /// Advance the master clock by n cycles.
    void tick(int n = 1);

    /// Return the current master cycle counter.
    uint64_t get() const { return cycle_; }

    /// Reset the cycle counter to zero.
    void reset();

    /// True on every Nth master cycle corresponding to the CPU clock
    /// enable (rising edge of the CPU clock in the 28 MHz domain).
    /// The divisor is set by set_cpu_speed().
    bool cpu_enable() const;

    /// True on every 4th master cycle (7 MHz pixel clock enable).
    bool pixel_enable() const;

    /// Set the CPU speed from a CpuSpeed enum value.
    void set_cpu_speed(CpuSpeed speed);

    /// Return the current CPU divisor (8, 4, 2, or 1).
    int cpu_divisor() const { return cpu_divisor_; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    uint64_t cycle_;      ///< Master cycle counter
    int      cpu_divisor_; ///< Divisor: 8=3.5MHz, 4=7MHz, 2=14MHz, 1=28MHz
};
