#include "audio/psg_writer.h"

namespace jnext {
namespace audio {

void PsgWriter::separators(std::vector<uint8_t>& out, uint64_t count)
{
    // psg.c write_frame_separator(): runs of 4 frames first (0xFE n, n <= 255), then one 0xFF each.
    while (count >= 4) {
        uint64_t n = count / 4;
        if (n > 255) n = 255;
        out.push_back(0xFE);
        out.push_back(static_cast<uint8_t>(n));
        count -= 4 * n;
    }
    for (; count > 0; --count) out.push_back(0xFF);
}

void PsgWriter::begin(uint32_t start_frame)
{
    out_.assign({'P', 'S', 'G', 0x1A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    last_ = static_cast<int64_t>(start_frame) - 1;
}

void PsgWriter::frame(uint32_t frame, uint16_t mask, const std::array<uint8_t, 14>& values)
{
    mask &= 0x3FFF;
    if (mask == 0) return;
    // At the end of frame F, Fuse's empty-frame count is F - L (1 right after a block).
    // A frame not above the last block's is a caller fault; one separator, never a wrapped count
    // (an unsigned 2^64 would append separators until memory ran out).
    const int64_t gap = static_cast<int64_t>(frame) - last_;
    separators(out_, gap >= 1 ? static_cast<uint64_t>(gap) : 1);
    for (unsigned r = 0; r < 14; ++r) {
        if (mask & (1u << r)) {
            out_.push_back(static_cast<uint8_t>(r));
            out_.push_back(values[r]);
        }
    }
    last_ = frame;
}

void PsgWriter::end(uint32_t stop_frame)
{
    // psg_stop_recording(): the current frame counts as ended, so S + 1 - L frames are owed.
    const int64_t n = static_cast<int64_t>(stop_frame) + 1 - last_;
    separators(out_, n >= 1 ? static_cast<uint64_t>(n) : 1);
}

std::vector<uint8_t> PsgWriter::take()
{
    std::vector<uint8_t> r;
    r.swap(out_);
    return r;
}

}  // namespace audio
}  // namespace jnext
