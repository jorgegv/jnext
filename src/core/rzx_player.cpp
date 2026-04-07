#include "core/rzx_player.h"
#include "core/log.h"

void RzxPlayer::start(RzxRecording recording) {
    recording_ = std::move(recording);
    frame_index_ = 0;
    in_index_ = 0;
    playing_ = true;
    Log::emulator()->info("RZX: playback started — {} frames", recording_.frames.size());
}

void RzxPlayer::stop() {
    playing_ = false;
    frame_index_ = 0;
    in_index_ = 0;
    Log::emulator()->info("RZX: playback stopped");
}

void RzxPlayer::begin_frame() {
    if (!playing_) return;

    if (frame_index_ >= recording_.frames.size()) {
        Log::emulator()->info("RZX: playback complete (all frames consumed)");
        playing_ = false;
        return;
    }

    in_index_ = 0;
    ++frame_index_;
}

uint8_t RzxPlayer::next_in_value() {
    if (!playing_ || frame_index_ == 0 || frame_index_ > recording_.frames.size())
        return 0xFF;

    const auto& frame = recording_.frames[frame_index_ - 1];
    if (in_index_ < frame.in_values.size()) {
        return frame.in_values[in_index_++];
    }
    return 0xFF;
}

uint16_t RzxPlayer::current_instruction_count() const {
    if (!playing_ || frame_index_ == 0 || frame_index_ > recording_.frames.size())
        return 0;
    return recording_.frames[frame_index_ - 1].instruction_count;
}
