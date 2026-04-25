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

void VideoTiming::init(MachineType type, bool refresh_60hz)
{
    // Storage holds VHDL-faithful c_max_hc / c_max_vc / c_int_h / c_int_v
    // (max-reached-before-wrap; not line/frame counts). Period is c_max_+1.
    // ── 50 Hz baseline (VHDL i_50_60='0' branch, zxula_timing.vhd:147-280)
    switch (type) {
        case MachineType::ZX128K:
            // 128K 50 Hz (VHDL :184-204)
            hc_max_       = 455;            // :196
            vc_max_       = 310;            // :204
            min_hactive_  = 136;            // :195
            min_vactive_  = 64;             // :203
            int_h_        = 128;            // :187 (136+4-12)
            int_v_        = 1;              // :199
            break;
        case MachineType::ZX_PLUS3:
            // +3 50 Hz (VHDL :184-204, i_timing(0)='1' branch at :189)
            hc_max_       = 455;            // :196
            vc_max_       = 310;            // :204
            min_hactive_  = 136;            // :195
            min_vactive_  = 64;             // :203
            int_h_        = 126;            // :189 (136+2-12)
            int_v_        = 1;              // :199
            break;
        case MachineType::PENTAGON:
            // Pentagon (VHDL :147-176). c_max_hc=447, c_max_vc=319.
            hc_max_       = 447;            // :160
            vc_max_       = 319;            // :168
            min_hactive_  = 128;            // :159
            min_vactive_  = 80;             // :167
            int_h_        = 439;            // :155 (448+3-12)
            int_v_        = 319;            // :163
            break;
        case MachineType::ZXN_ISSUE2:
            // ZX Next defaults to 128K-style timing (256x192, 50 Hz).
            hc_max_       = 455;            // follows 128K
            vc_max_       = 310;
            min_hactive_  = 136;
            min_vactive_  = 64;
            int_h_        = 128;
            int_v_        = 1;
            break;
        case MachineType::ZX48K:
        default:
            // 48K 50 Hz (VHDL :252-278). c_max_hc=447, c_max_vc=311.
            hc_max_       = 447;            // :262
            vc_max_       = 311;            // :270
            min_hactive_  = 128;            // :261
            min_vactive_  = 64;             // :269
            int_h_        = 116;            // :257 (128+0-12)
            int_v_        = 0;              // :265
            break;
    }

    // ── 60 Hz overrides (VHDL i_50_60='1' branch, zxula_timing.vhd:214-308).
    // Pentagon has no 60 Hz VHDL branch — silently ignore the flag for it.
    // hc_max_ / min_hactive_ / int_h_ are unchanged between 50 Hz and 60 Hz
    // on the same machine (per VHDL — the 128K-vs-+3 c_int_h split is the
    // same at both refresh rates).
    refresh_60hz_ = false;
    if (refresh_60hz && type != MachineType::PENTAGON) {
        refresh_60hz_ = true;
        vc_max_      = 263;     // VHDL c_max_vc :238/:298
        min_vactive_ = 40;      // VHDL c_min_vactive :237/:297
        int_v_       = 0;       // VHDL c_int_v :233/:293
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

    // Wrap horizontal counter into next lines. Storage holds VHDL c_max_hc
    // (max-reached-before-wrap), so the line period is hc_max_+1 ticks.
    while (hc > hc_max_) {
        hc -= (hc_max_ + 1);

        // Line crossing: vc_ is about to become vc_+1. On the VHDL
        // clock edge where cvc transitions to target_line_num, the
        // line-int pulse goes high (held for one 7 MHz tick). Model
        // this as: fire a pulse when we leave a line where vc_ ==
        // target_line_num. (cvc copper-offset is 0 for Wave E scope.)
        if (inten_line_ && vc_ == target_line_num) {
            ++line_int_pulses_;
        }

        ++vc_;

        // Frame wrap at line c_max_vc + 1 (= line count). Storage holds
        // VHDL c_max_vc (max-reached-before-wrap), strict `>` matches.
        if (vc_ > static_cast<uint16_t>(vc_max_)) {
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
