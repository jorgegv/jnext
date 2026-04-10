#include "core/clock.h"
#include "core/saveable.h"

Clock::Clock()
    : cycle_(0)
    , cpu_divisor_(8)   // default: 3.5 MHz (÷8 from 28 MHz)
{
}

void Clock::tick(int n)
{
    cycle_ += static_cast<uint64_t>(n);
}

void Clock::reset()
{
    cycle_ = 0;
}

bool Clock::cpu_enable() const
{
    // A rising edge occurs when cycle_ is a multiple of cpu_divisor_.
    return (cycle_ % static_cast<uint64_t>(cpu_divisor_)) == 0;
}

bool Clock::pixel_enable() const
{
    // Pixel clock: 28 MHz / 4 = 7 MHz.
    return (cycle_ % 4ULL) == 0;
}

void Clock::set_cpu_speed(CpuSpeed speed)
{
    cpu_divisor_ = cpu_speed_divisor(speed);
}

void Clock::save_state(StateWriter& w) const
{
    w.write_u64(cycle_);
    w.write_i32(cpu_divisor_);
}

void Clock::load_state(StateReader& r)
{
    cycle_       = r.read_u64();
    cpu_divisor_ = r.read_i32();
}
