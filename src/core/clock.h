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

    /// **Immediate** commit of CPU speed: both pending shadow and effective
    /// divisor are updated. Used by Emulator::init() (reset path), config
    /// load, and bare-class tests that need a known speed without modelling
    /// the deferred bus-idle commit edge. Production NR 0x07 dispatch should
    /// instead call set_pending_cpu_speed() and let
    /// commit_pending_cpu_speed_on_bus_idle() latch on the next bus-idle
    /// CLK_CPU rising edge per zxnext.vhd:5796-5828.
    void set_cpu_speed(CpuSpeed speed);

    /// Update the **shadow** value of NR 0x07 cpu_speed only — the next
    /// commit_pending_cpu_speed_on_bus_idle() call with bus-idle=true will
    /// promote it to the effective divisor. Models the latch at
    /// zxnext.vhd:5788-5789 (immediate on nr_07_we) feeding 5816-5817
    /// (cpu_speed <= nr_07_cpu_speed only when
    /// cpu_mreq_n='1' AND cpu_iorq_n='1' AND cpu_m1_n='1' AND
    /// dma_holds_bus='0'). This is the production NR 0x07 write path.
    void set_pending_cpu_speed(CpuSpeed speed);

    /// Commit the pending → effective divisor when the CPU bus is idle.
    /// VHDL zxnext.vhd:5809 — the assignment fires on every CLK_CPU
    /// rising edge that satisfies
    ///     cpu_mreq_n='1' AND cpu_iorq_n='1' AND cpu_m1_n='1' AND dma_holds_bus='0'.
    /// In jnext, the natural sample point is the post-instruction tick
    /// (CPU is between bus cycles). Caller passes the live bus-idle gate;
    /// the commit is a no-op when the gate is false. Idempotent.
    void commit_pending_cpu_speed_on_bus_idle(bool bus_idle, bool dma_holds_bus = false);

    /// Return the current CPU divisor (8, 4, 2, or 1).
    int cpu_divisor() const { return cpu_divisor_; }

    /// Return the divisor implied by the pending shadow speed (the value
    /// that would become effective on the next bus-idle commit).
    int pending_cpu_divisor() const { return cpu_speed_divisor(pending_cpu_speed_); }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    uint64_t cycle_;              ///< Master cycle counter
    int      cpu_divisor_;        ///< Effective divisor: 8=3.5MHz, 4=7MHz, 2=14MHz, 1=28MHz
    CpuSpeed pending_cpu_speed_;  ///< Shadow CPU speed (pending bus-idle commit)
};
