#include "joy_uart_source.h"

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
