#include "joy_uart_source.h"

#include "core/saveable.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

JoyUartSource::JoyUartSource(std::vector<uint8_t> bytes, int connector,
                            uint32_t delay_frames)
    : bytes_(std::move(bytes)),
      connector_((connector != 0) ? 1 : 0),
      delay_frames_(delay_frames) {}

void JoyUartSource::end_frame() {
    if (frames_ < delay_frames_) ++frames_;   // saturating — see the member
}

void JoyUartSource::tick(uint32_t master_cycles, uint32_t byte_ticks) {
    if (waiting() || exhausted() || !sink_) return;
    if (byte_ticks == 0) byte_ticks = 1;   // as UartChannel does for prescaler 0

    if (timer_ == 0) timer_ = byte_ticks;  // arm on the first tick after the hold

    // A single call can span more than one byte time — `master_cycles` is one
    // Z80 instruction (tens of ticks) and `byte_ticks` is prescaler * frame_bits,
    // which a guest may legally program down to ~10. So loop, and carry the
    // remainder into the next byte's timer instead of discarding it: dropping it
    // would under-pace the stream by up to one instruction per byte.
    uint32_t remaining = master_cycles;
    while (remaining >= timer_) {
        remaining -= timer_;
        timer_ = byte_ticks;
        sink_(bytes_[pos_++]);
        if (exhausted()) return;
    }
    timer_ -= remaining;
}

void JoyUartSource::save_state(StateWriter& w) const {
    w.write_u32(static_cast<uint32_t>(pos_));
    w.write_u32(static_cast<uint32_t>(delivered_));
    w.write_u32(static_cast<uint32_t>(dropped_));
    w.write_u32(frames_);
    w.write_u32(timer_);
}

void JoyUartSource::load_state(StateReader& r) {
    // Clamped to the stream length: a truncated or otherwise inconsistent
    // snapshot must not leave `pos_` past the end, where `exhausted()` would be
    // true but `bytes_[pos_]` would still be indexed if anything ever read it
    // before checking.
    const uint32_t pos = r.read_u32();
    pos_       = (pos > bytes_.size()) ? bytes_.size() : static_cast<std::size_t>(pos);
    delivered_ = r.read_u32();
    dropped_   = r.read_u32();
    frames_    = r.read_u32();
    timer_     = r.read_u32();
}

bool read_joy_uart_source_file(const std::string& path,
                              std::vector<uint8_t>& bytes,
                              std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = path + ": " + std::strerror(errno);
        return false;
    }
    std::vector<uint8_t> read((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    // `bad()` rather than `!in`: the stream is at EOF by construction here, so
    // `fail()` alone would reject every successful read.
    if (in.bad()) {
        error = path + ": read error";
        return false;
    }
    // An empty file is refused rather than accepted as "a source that sends
    // nothing": it can only ever produce a run that looks like a pass for a
    // path no byte ever took, which is what this whole feature exists to make
    // impossible.
    if (read.empty()) {
        error = path + ": file is empty (a source that sends nothing tests nothing)";
        return false;
    }
    bytes = std::move(read);
    return true;
}
