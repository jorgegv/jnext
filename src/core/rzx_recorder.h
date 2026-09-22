#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "core/rzx.h"

/// Records RZX input data: captures every IN port read per frame.
class RzxRecorder {
public:
    /// Start recording to the given output file path. Returns false — and
    /// does not start — when the path cannot be written (see can_write()),
    /// so a recording that is going to be lost is refused up front rather
    /// than discovered when it is saved.
    bool start(const std::string& output_path);

    /// Stop recording and write the RZX file. Returns true only when the
    /// whole file reached the disk; false (logged) when it did not, or when
    /// no recording was running.
    bool stop();

    /// The file the running (or last) recording is written to.
    const std::string& output_path() const { return output_path_; }

    /// Whether `path` can be opened for writing. Leaves an existing file
    /// untouched and removes a file it had to create. On failure `why` says
    /// what the system reported.
    static bool can_write(const std::string& path, std::string& why);

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

    /// Name the machine the recording is made on (rzx::set_recorded_machine()),
    /// which playback then runs on.
    void set_machine(MachineType t) { rzx::set_recorded_machine(rec_, t); }

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
