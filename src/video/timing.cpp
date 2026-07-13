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
    // The VHDL constants are keyed on the TIMING axis (i_timing / tim_sel),
    // not the machine personality (typ_sel) — delegate through the canonical
    // typ_sel → tim_sel mapping (ZXN defaults to 128K-style timing, matching
    // the pre-Task-51 dedicated ZXN_ISSUE2 case which held the identical
    // constants).
    init_timing(default_machine_timing_for(type), refresh_60hz);
}

void VideoTiming::init_timing(MachineTimingMode mode, bool refresh_60hz)
{
    // Storage holds VHDL-faithful c_max_hc / c_max_vc / c_int_h / c_int_v
    // (max-reached-before-wrap; not line/frame counts). Period is c_max_+1.
    // ── 50 Hz baseline (VHDL i_50_60='0' branch, zxula_timing.vhd:147-280)
    switch (mode) {
        case MachineTimingMode::Timing128:
            // 128K 50 Hz (VHDL :184-204)
            hc_max_       = 455;            // :196
            vc_max_       = 310;            // :204
            min_hactive_  = 136;            // :195
            min_vactive_  = 64;             // :203
            int_h_        = 128;            // :187 (136+4-12)
            int_v_        = 1;              // :199
            break;
        case MachineTimingMode::TimingPlus3:
            // +3 50 Hz (VHDL :184-204, i_timing(0)='1' branch at :189)
            hc_max_       = 455;            // :196
            vc_max_       = 310;            // :204
            min_hactive_  = 136;            // :195
            min_vactive_  = 64;             // :203
            int_h_        = 126;            // :189 (136+2-12)
            int_v_        = 1;              // :199
            break;
        case MachineTimingMode::TimingPentagon:
            // Pentagon (VHDL :150-168 — no 50/60 split; i_timing(2)='1').
            // Task 51: this block was lost when the standalone Pentagon
            // MachineType was retired (Wave 0.3); NR 0x03 tim_sel can
            // still select Pentagon timing at runtime.
            hc_max_       = 447;            // :160 c_max_hc
            vc_max_       = 319;            // :168 c_max_vc
            min_hactive_  = 128;            // :159 c_min_hactive
            min_vactive_  = 80;             // :167 c_min_vactive
            int_h_        = 439;            // :155 c_int_h (448+3-12)
            int_v_        = 319;            // :163 c_int_v
            break;
        case MachineTimingMode::Timing48:
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
    // hc_max_ / min_hactive_ / int_h_ are unchanged between 50 Hz and 60 Hz
    // on the same machine (per VHDL — the 128K-vs-+3 c_int_h split is the
    // same at both refresh rates). Pentagon has no 60 Hz branch (the whole
    // i_timing(2)='1' block at :150-168 sits outside the i_50_60 split);
    // the flag is ignored there.
    refresh_60hz_ = false;
    if (refresh_60hz && mode != MachineTimingMode::TimingPentagon) {
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
    // tick through hc_ula=255. The VHDL `cvc` (zxula_timing.vhd:455-466)
    // reloads to `'0' & i_cu_offset` at `ula_min_vactive` and increments
    // each line, wrapping at `c_max_vc`. We compute cvc on each line
    // crossing as
    //   cvc(vc) = (vc - min_vactive + cu_offset) mod (c_max_vc + 1)
    // and fire when cvc matches int_line_num. This is the VHDL one-shot
    // semantic at frame granularity — sufficient for S14.05/06 which
    // ask "does the mechanism fire (once per frame) at target line N?".
    //
    // The per-frame ULA pulse (VHDL :547-559, fires at hc==c_int_h,
    // vc==c_int_v) is gated on `inten_ula_n == '0'`, i.e. our
    // `inten_ula_ == true` — we fire one pulse per frame wrap when
    // enabled, matching the behaviour S14.04 checks (disabled → zero).
    const uint16_t target_line_num = int_line_num();
    const int      lines_per_frame = vc_max_ + 1;

    // Wrap horizontal counter into next lines. Storage holds VHDL c_max_hc
    // (max-reached-before-wrap), so the line period is hc_max_+1 ticks.
    while (hc > hc_max_) {
        hc -= (hc_max_ + 1);

        // Line crossing: vc_ is about to become vc_+1. On the VHDL
        // clock edge where cvc transitions to target_line_num, the
        // line-int pulse goes high (held for one 7 MHz tick). Model
        // this as: fire a pulse when we leave a line where
        //   cvc(vc_) == target_line_num,
        // where cvc = (vc - min_vactive + cu_offset) mod lines_per_frame
        // per VHDL zxula_timing.vhd:455-466.
        const int cvc = (static_cast<int>(vc_)
                         - static_cast<int>(min_vactive_)
                         + static_cast<int>(cu_offset_)
                         + lines_per_frame * 2)  // bias to keep mod positive
                        % lines_per_frame;
        if (inten_line_ && cvc == static_cast<int>(target_line_num)) {
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
