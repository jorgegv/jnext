#include "video/timing.h"

void VideoTiming::reset()
{
    hc_         = 0;
    vc_         = 0;
    frame_done_ = false;
    // Wave E — pulse counters cleared; enable flags are config, not reset state
    // (VHDL nr_22/nr_23 reset defaults are applied by the NR write handlers,
    // not by the timing block itself — zxnext.vhd:4983-4985).
    ula_int_pulses_  = 0;
    line_int_pulses_ = 0;
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
    // Use int for accumulation to avoid uint16_t overflow when advancing
    // large T-state counts (e.g. 69888 T-states = 139776 ticks > 65535).
    int hc = static_cast<int>(hc_) + tstates * 2;

    // Wave E — line-interrupt pulse detection.
    //
    // VHDL zxula_timing.vhd:574-583 gates the line-int pulse on
    //   (inten_line='1') AND (hc_ula == 255) AND (cvc == int_line_num).
    // Here we coarse-grain: each line crossing represents one pixel-clock
    // tick through hc_ula=255. We fire one pulse whenever `vc_` (≈cvc,
    // copper offset 0) matches int_line_num at a line boundary, so long
    // as the line-int enable is asserted. This is the VHDL one-shot
    // semantic at frame granularity — sufficient for S14.05/06 which
    // ask "does the mechanism fire (once per frame) at target line N?".
    //
    // The per-frame ULA pulse (VHDL :547-559, fires at hc==c_int_h,
    // vc==c_int_v) is gated on `inten_ula_n == '0'`, i.e. our
    // `inten_ula_ == true` — we fire one pulse per frame wrap when
    // enabled, matching the behaviour S14.04 checks (disabled → zero).
    const uint16_t target_line_num = int_line_num();

    // Wrap horizontal counter into next lines.
    while (hc >= hc_max_) {
        hc -= hc_max_;

        // Line crossing: vc_ is about to become vc_+1. On the VHDL
        // clock edge where cvc transitions to target_line_num, the
        // line-int pulse goes high (held for one 7 MHz tick). Model
        // this as: fire a pulse when we leave a line where vc_ ==
        // target_line_num. (cvc copper-offset is 0 for Wave E scope.)
        if (inten_line_ && vc_ == target_line_num) {
            ++line_int_pulses_;
        }

        ++vc_;

        if (vc_ >= static_cast<uint16_t>(vc_max_)) {
            vc_         = 0;
            frame_done_ = true;
            // ULA per-frame pulse on frame boundary when enabled
            // (VHDL zxula_timing.vhd:551: inten_ula_n='0' gate).
            if (inten_ula_) {
                ++ula_int_pulses_;
            }
        }
    }

    hc_ = static_cast<uint16_t>(hc);
}

bool VideoTiming::in_display() const
{
    return (hc_ >= DISPLAY_LEFT && hc_ < DISPLAY_LEFT + DISPLAY_W)
        && (vc_ >= DISPLAY_TOP  && vc_ < DISPLAY_TOP  + DISPLAY_H);
}
