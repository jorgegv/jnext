#include "video/timing.h"

void VideoTiming::reset()
{
    hc_         = 0;
    vc_         = 0;
    frame_done_ = false;
}

void VideoTiming::advance(int tstates)
{
    // Each CPU T-state at 3.5 MHz corresponds to 2 pixel ticks at 7 MHz.
    int ticks = tstates * 2;

    hc_ += static_cast<uint16_t>(ticks);

    // Wrap horizontal counter into next lines.
    while (hc_ >= HC_MAX) {
        hc_ -= static_cast<uint16_t>(HC_MAX);
        ++vc_;

        if (vc_ >= VC_MAX) {
            vc_         = 0;
            frame_done_ = true;
        }
    }
}

bool VideoTiming::in_display() const
{
    return (hc_ >= DISPLAY_LEFT && hc_ < DISPLAY_LEFT + DISPLAY_W)
        && (vc_ >= DISPLAY_TOP  && vc_ < DISPLAY_TOP  + DISPLAY_H);
}
