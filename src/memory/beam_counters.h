#pragma once

// Raw-hc positions of the video pipeline's own horizontal counters, shared by
// every beam-time replay (AttributeMux for bank 5 / bank 7 VRAM, SramBeamLog for
// Layer 2). GH #362.
//
// VHDL authority: cores/zxnext/src/video/zxula_timing.vhd. `hc_origin` is
// VideoTiming::ula_prefetch_origin_hc() = c_min_hactive - 12 (the only
// per-machine input; AttributeMux::start_frame() / SramBeamLog::start_frame()
// store it). Both helpers clamp at 0 so bare fixtures with origin 0 stay in
// range.
namespace beam {

/// Raw hc at which the wide counter `whc` reads `w` (the tilemap's and the
/// 320x256 / 640x256 Layer 2 modes' horizontal counter).
/// zxula_timing.vhd:476-492: `whc` is loaded with -16 on the 7 MHz edge after
/// hc = c_min_hactive - 48 (`wide_min_hactive`, :473), i.e. at raw hc
/// c_min_hactive - 47, and counts +1 per tick, so whc = w at raw hc
/// c_min_hactive - 31 + w. c_min_hactive = hc_origin + 12.
inline int whc_raw_hc(int hc_origin, int w) {
    const int h = ((hc_origin < 0) ? 0 : hc_origin) + 12 - 31 + w;
    return h < 0 ? 0 : h;
}

/// Raw hc at which the 256-pixel counter `phc` (LoRes, Layer 2 256x192) reads
/// `p`. zxula_timing.vhd:510-522: loaded with -48 on the same edge as `whc`,
/// so phc = p at raw hc c_min_hactive + 1 + p.
inline int phc_raw_hc(int hc_origin, int p) {
    const int h = ((hc_origin < 0) ? 0 : hc_origin) + 12 + 1 + p;
    return h < 0 ? 0 : h;
}

} // namespace beam
