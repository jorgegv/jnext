#pragma once

#include <cstdint>
#include <string>
#include "core/rzx.h"

/// Plays back an RZX recording by feeding recorded IN values to the CPU.
class RzxPlayer {
public:
    /// Start playback from a parsed recording.
    void start(RzxRecording recording);

    /// Stop playback and discard state.
    void stop();

    /// Whether playback is active.
    bool is_playing() const { return playing_; }

    /// Call at the beginning of each emulated frame.
    void begin_frame();

    /// Return the next IN value for the current frame.
    /// If all recorded values have been consumed, returns 0xFF.
    uint8_t next_in_value();

    /// Whether we've reached the end of all recorded frames.
    bool finished() const { return frame_index_ >= recording_.frames.size(); }

    /// Get the instruction count for the current frame.
    uint16_t current_instruction_count() const;

    /// Get the initial tstates value from the recording.
    uint32_t initial_tstates() const { return recording_.initial_tstates; }

    /// Access the recording (e.g. to get snapshot data).
    const RzxRecording& recording() const { return recording_; }

private:
    RzxRecording recording_;
    size_t frame_index_ = 0;
    size_t in_index_ = 0;
    bool playing_ = false;
};
