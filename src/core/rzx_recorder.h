#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "core/rzx.h"

/// Records RZX input data: captures every IN port read per frame.
class RzxRecorder {
public:
    /// Start recording to the given output file path.
    void start(const std::string& output_path);

    /// Stop recording and write the RZX file.
    bool stop();

    /// Whether recording is active.
    bool is_recording() const { return recording_; }

    /// Call at the beginning of each emulated frame.
    void begin_frame();

    /// Record an IN port read value.
    void record_in(uint8_t value);

    /// Call at the end of each emulated frame with the instruction count.
    void end_frame(uint16_t instruction_count);

    /// Set the embedded snapshot data (SNA format).
    void set_snapshot(std::vector<uint8_t> data, const std::string& ext);

    /// Set initial tstates.
    void set_initial_tstates(uint32_t ts) { rec_.initial_tstates = ts; }

    /// Access the recording data.
    const RzxRecording& recording() const { return rec_; }

private:
    RzxRecording rec_;
    std::string output_path_;
    std::vector<uint8_t> current_in_values_;
    bool recording_ = false;
    bool frame_started_ = false;
};
