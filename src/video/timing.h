#pragma once
#include <cstdint>
#include "memory/contention.h"

struct RasterPos { uint16_t hc; uint16_t vc; };

/// Raster counter supporting multiple machine timing variants.
///
/// Counters are in the 7 MHz pixel-clock domain.
/// The CPU runs at 3.5 MHz so each T-state advances hc by 2 pixel ticks.
///
/// Counters (hc_max_ / vc_max_) hold VHDL c_max_hc / c_max_vc
/// (max-reached-before-wrap), so the period is c_max_+1.
///
/// 48K timing (PAL 50 Hz):
///   - VHDL c_max_hc=447 → 448 pixel ticks per line  (224 T-states per line)
///   - VHDL c_max_vc=311 → 312 lines per frame
///   - Active display: hc [128, 383], vc [64, 255]  → 256×192 pixels
///
/// 128K timing (ZX Spectrum 128K PAL):
///   - VHDL c_max_hc=455 → 456 pixel ticks per line
///   - VHDL c_max_vc=310 → 311 lines per frame
///   - Same active display window as 48K
///
/// Pentagon timing:
///   - VHDL c_max_hc=447 → 448 pixel ticks per line
///   - VHDL c_max_vc=319 → 320 lines per frame
///   - Same active display window
///
/// The output framebuffer is 320×256 (48 left/right border + 256 display +
/// 48 right border; 48 top + 192 display + 56 bottom border ≈ 256 rows).
class VideoTiming {
public:
    // Default machine constants (48K, 7 MHz domain) — VHDL-faithful c_max_*
    // (max-reached-before-wrap, NOT count). Period is c_max_+1.
    static constexpr int HC_MAX_DEFAULT  = 447;  // VHDL c_max_hc (48K); period = 448 ticks/line
    static constexpr int VC_MAX_DEFAULT  = 311;  // VHDL c_max_vc (48K); period = 312 lines/frame

    // Active display window (pixel addresses within 7 MHz domain)
    static constexpr int DISPLAY_LEFT   = 128;  // hc where active pixels start
    static constexpr int DISPLAY_TOP    = 64;   // vc where active pixels start
    static constexpr int DISPLAY_W      = 256;  // active pixel columns
    static constexpr int DISPLAY_H      = 192;  // active pixel rows

    // Border sizes in output pixels
    static constexpr int BORDER_LEFT    = 48;
    static constexpr int BORDER_RIGHT   = 48;
    static constexpr int BORDER_TOP     = 48;
    static constexpr int BORDER_BOTTOM  = 56;

    // Output framebuffer dimensions derived from borders + display
    static constexpr int FB_WIDTH  = BORDER_LEFT  + DISPLAY_W + BORDER_RIGHT;   // 352 — clipped to 320
    static constexpr int FB_HEIGHT = BORDER_TOP   + DISPLAY_H + BORDER_BOTTOM;  // 296 — clipped to 256

    void reset();

    /// Configure timing for the given machine type.
    /// Sets hc_max_ and vc_max_ from the machine-specific constants and resets counters.
    void init(MachineType type);

    /// Advance raster counters by the given number of CPU T-states.
    /// Each T-state at 3.5 MHz = 2 pixel ticks at 7 MHz.
    void advance(int tstates);

    RasterPos pos() const { return {hc_, vc_}; }

    /// True when hc/vc is within the 256×192 active display area.
    bool in_display() const;

    /// True once the counters have wrapped past the last line of the frame.
    bool frame_complete() const { return frame_done_; }
    void clear_frame_flag()     { frame_done_ = false; }

    /// Active HC_MAX for current machine type.
    int hc_max() const { return hc_max_; }
    /// Active VC_MAX for current machine type.
    int vc_max() const { return vc_max_; }

    // ---------------------------------------------------------------
    // Wave E — Line interrupt + ULA interrupt mechanism
    // (zxula_timing.vhd:547-583). The VHDL feeds three inputs to the
    // timing block:
    //   * i_inten_ula_n  — active-LOW ULA-interrupt gate. Drives the
    //     per-frame pulse at (hc==c_int_h, vc==c_int_v) per :551.
    //   * i_inten_line   — active-HIGH line-interrupt enable; fed from
    //     nr_22_line_interrupt_en (zxnext.vhd:6752).
    //   * i_int_line     — 9-bit target-line register fed from
    //     nr_23_line_interrupt (zxnext.vhd:6753). The VHDL stores the
    //     target as `int_line_num = (line==0) ? c_max_vc : line - 1`
    //     (:566-570), so target=0 fires at the frame-boundary wrap
    //     (cvc == c_max_vc) while target=N fires one line earlier
    //     than the CPU-visible line.
    //
    // The pulses are 7 MHz one-shots on real hardware. For Wave E we
    // expose them as per-advance() counters so tests can assert the
    // mechanism fires (or not) across a whole frame. The per-machine
    // INT position (S14.01/02/03) is out of scope — those rows stay
    // as VideoTiming F-skips until the per-machine accessor expansion.
    // ---------------------------------------------------------------

    /// Mirror of the VHDL `i_inten_ula_n` gate. Stored in ACTIVE-HIGH
    /// form here (`true` = interrupts enabled = VHDL `inten_ula_n='0'`).
    /// Default at reset is `true` (enabled) — matches the practical
    /// VHDL power-on where port_ff_interrupt_disable defaults '0'.
    void set_interrupt_enable(bool enabled) { inten_ula_ = enabled; }
    bool interrupt_enable() const            { return inten_ula_; }

    /// NR 0x22 bit 1 — line-interrupt enable (VHDL `i_inten_line`,
    /// zxnext.vhd:6752). Active-HIGH.
    void set_line_interrupt_enable(bool enabled) { inten_line_ = enabled; }
    bool line_interrupt_enable() const            { return inten_line_; }

    /// 9-bit line-interrupt target (VHDL `i_int_line`,
    /// zxnext.vhd:6753 — nr_22 bit 0 = MSB, nr_23 = low 8 bits).
    /// Stored raw; render-time conversion to `int_line_num` per VHDL
    /// zxula_timing.vhd:566-570.
    void     set_line_interrupt_target(uint16_t target) { int_line_target_ = static_cast<uint16_t>(target & 0x1FF); }
    uint16_t line_interrupt_target() const              { return int_line_target_; }

    /// Pulse counters: number of times the corresponding pulse has
    /// fired since the last `clear_int_counts()`. Each advance() that
    /// crosses the trigger condition increments the counter once.
    ///
    /// *** Test-observable surface only (2026-04-23). *** The jnext
    /// Emulator currently schedules frame/line interrupts through local
    /// fields in `Emulator` (see emulator.cpp NR 0x22/0x23 handlers +
    /// the scheduler in run_frame). No production VideoTiming instance
    /// is wired through the NR path, so these counters reflect only
    /// what test harnesses drive via `set_*_enable` + `advance`. Making
    /// them production-wired requires funnelling the Emulator scheduler
    /// through a `VideoTiming` member — tracked as a post-ULA-plan
    /// follow-up.
    int ula_int_pulse_count() const  { return ula_int_pulses_; }
    int line_int_pulse_count() const { return line_int_pulses_; }
    void clear_int_counts() {
        ula_int_pulses_  = 0;
        line_int_pulses_ = 0;
    }

    /// Compute the VHDL `int_line_num` from the raw target register.
    /// target==0 → c_max_vc (frame-boundary). target!=0 → target-1
    /// (VHDL zxula_timing.vhd:566-570). Public so the videotiming
    /// compliance suite (Section 6) can observe it directly.
    uint16_t int_line_num() const {
        if (int_line_target_ == 0)
            return static_cast<uint16_t>(vc_max_);
        return static_cast<uint16_t>(int_line_target_ - 1);
    }

private:
    uint16_t hc_        = 0;
    uint16_t vc_        = 0;
    bool     frame_done_ = false;

    // Machine-type-dependent timing parameters (defaulting to 48K values).
    int hc_max_ = HC_MAX_DEFAULT;  // pixel ticks per line
    int vc_max_ = VC_MAX_DEFAULT;  // total lines per frame

    // Wave E — line interrupt mechanism.
    bool     inten_ula_        = true;   // active-HIGH; true = VHDL inten_ula_n='0'
    bool     inten_line_       = false;
    uint16_t int_line_target_  = 0;
    int      ula_int_pulses_   = 0;
    int      line_int_pulses_  = 0;
};
