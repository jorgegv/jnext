#include "core/clock.h"

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

void Clock::set_cpu_speed(int mhz)
{
    // Map requested MHz to the nearest valid divisor.
    if (mhz >= 28) {
        cpu_divisor_ = 1;
    } else if (mhz >= 14) {
        cpu_divisor_ = 2;
    } else if (mhz >= 7) {
        cpu_divisor_ = 4;
    } else {
        // 3.5 MHz (also handles values of 3 or 4)
        cpu_divisor_ = 8;
    }
}
