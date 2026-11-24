#pragma once

// ---------------------------------------------------------------------------
// One AY chip's register stream in the layout of Fuse 1.6.0's AY log (psg.c).
// A pure encoder: no debugger, no machine, no file. Frame numbers are the
// caller's, so the same writer serves the live recorder and the unit rows.
//
// Layout (psg.c: psg_start_recording, psg_frame, write_frame_separator):
//   16-byte header "PSG" 0x1A + 12 zero bytes;
//   per frame that wrote any of R0-R13: the separators for the frames since the
//   previous block, then "register, value" for each written register in
//   register order;
//   at the end: the separators up to and including the stop frame.
//   Separators: 0xFF = 1 frame; "0xFE n" = 4n frames (n <= 255).
// R14/R15 are never emitted, and a frame that wrote only them is empty.
// ---------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jnext {
namespace audio {

class PsgWriter {
public:
    static constexpr size_t HEADER_SIZE = 16;

    /// The header. The frame before `start_frame` is taken as the last block.
    void begin(uint32_t start_frame);
    /// A frame in which the registers of `mask` (bits 0-13; bits 14-15 ignored) were
    /// written, with their values. `frame` is above the last block's frame. A mask with
    /// no bit in 0-13 adds nothing.
    void frame(uint32_t frame, uint16_t mask, const std::array<uint8_t, 14>& values);
    /// The trailing separators up to and including `stop_frame`.
    void end(uint32_t stop_frame);
    /// The bytes produced since the last take().
    std::vector<uint8_t> take();
    /// Separators for `count` frames (count >= 1), exactly as write_frame_separator().
    static void separators(std::vector<uint8_t>& out, uint64_t count);

private:
    std::vector<uint8_t> out_;
    int64_t last_ = -1;   ///< frame of the last block (start frame - 1 until one is written)
};

}  // namespace audio
}  // namespace jnext
