#include "video/timing.h"

void VideoTiming::reset()
{
    hc_         = 0;
    vc_         = 0;
    frame_done_ = false;
    // Timing parameters are not reset here; call init() to change machine type.
}

void VideoTiming::init(MachineType type)
{
    switch (type) {
        case MachineType::ZX128K:
        case MachineType::ZX_PLUS3:
            // 128K/+3 PAL: 456 ticks/line, 311 lines/frame (VHDL c_max_hc=455).
            hc_max_ = 456;
            vc_max_ = 311;
            break;
        case MachineType::PENTAGON:
            // Pentagon: 448 ticks/line, 320 lines/frame.
            // Same active display window as 48K.
            hc_max_ = 448;
            vc_max_ = 320;
            break;
        case MachineType::ZX48K:
        default:
            // 48K PAL: 448 ticks/line, 312 lines/frame (VHDL c_max_hc=447).
            hc_max_ = 448;
            vc_max_ = 312;
            break;
        case MachineType::ZXN_ISSUE2:
            // ZX Next defaults to 128K timing (456 ticks/line, 311 lines/frame).
            hc_max_ = 456;
            vc_max_ = 311;
            break;
    }
    reset();
}

void VideoTiming::advance(int tstates)
{
    // Each CPU T-state at 3.5 MHz corresponds to 2 pixel ticks at 7 MHz.
    int ticks = tstates * 2;

    hc_ += static_cast<uint16_t>(ticks);

    // Wrap horizontal counter into next lines.
    while (hc_ >= static_cast<uint16_t>(hc_max_)) {
        hc_ -= static_cast<uint16_t>(hc_max_);
        ++vc_;

        if (vc_ >= static_cast<uint16_t>(vc_max_)) {
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
