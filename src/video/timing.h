#pragma once
#include <cstdint>

struct RasterPos { uint16_t hc; uint16_t vc; };

/// Raster counter for the 48K/128K compatible machine.
///
/// Counters are in the 7 MHz pixel-clock domain.
/// The CPU runs at 3.5 MHz so each T-state advances hc by 2 pixel ticks.
///
/// 48K timing (PAL 50 Hz):
///   - 456 pixel ticks per line  (64 T-states per line in 7 MHz domain)
///   - 312 lines per frame
///   - Active display: hc [128, 383], vc [64, 255]  → 256×192 pixels
///
/// The output framebuffer is 320×256 (48 left/right border + 256 display +
/// 48 right border; 48 top + 192 display + 56 bottom border ≈ 256 rows).
class VideoTiming {
public:
    // Machine constants (48K / 128K compatible, 7 MHz domain)
    static constexpr int HC_MAX         = 456;  // pixel ticks per line
    static constexpr int VC_MAX         = 312;  // total lines per frame (PAL)

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

    /// Advance raster counters by the given number of CPU T-states.
    /// Each T-state at 3.5 MHz = 2 pixel ticks at 7 MHz.
    void advance(int tstates);

    RasterPos pos() const { return {hc_, vc_}; }

    /// True when hc/vc is within the 256×192 active display area.
    bool in_display() const;

    /// True once the counters have wrapped past the last line of the frame.
    bool frame_complete() const { return frame_done_; }
    void clear_frame_flag()     { frame_done_ = false; }

private:
    uint16_t hc_        = 0;
    uint16_t vc_        = 0;
    bool     frame_done_ = false;
};
