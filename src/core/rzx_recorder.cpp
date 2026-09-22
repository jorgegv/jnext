#include "core/rzx_recorder.h"
#include "core/log.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

bool RzxRecorder::can_write(const std::string& path, std::string& why) {
    std::error_code ec;
    const bool existed = std::filesystem::exists(path, ec);
    errno = 0;
    {
        // Append mode: probing must not truncate a file that is already there.
        std::ofstream f(path, std::ios::binary | std::ios::app);
        if (!f) {
            why = errno ? std::strerror(errno) : "cannot be opened for writing";
            return false;
        }
    }
    if (!existed) std::filesystem::remove(path, ec);
    return true;
}

bool RzxRecorder::start(const std::string& output_path) {
    std::string why;
    if (!can_write(output_path, why)) {
        Log::emulator()->error("RZX: cannot record to '{}': {}", output_path, why);
        return false;
    }
    output_path_ = output_path;
    rec_ = RzxRecording{};
    rec_.creator = "JNEXT";
    rec_.creator_major = 1;
    rec_.creator_minor = 0;
    current_in_values_.clear();
    recording_ = true;
    frame_started_ = false;
    Log::emulator()->info("RZX: recording started — output: {}", output_path);
    return true;
}

bool RzxRecorder::stop() {
    if (!recording_) return false;
    recording_ = false;
    frame_started_ = false;

    bool ok = rzx::write(output_path_, rec_);
    if (ok) {
        Log::emulator()->info("RZX: recording saved — {} frames to '{}'",
                              rec_.frames.size(), output_path_);
    } else {
        Log::emulator()->error("RZX: failed to write '{}'", output_path_);
    }
    return ok;
}

void RzxRecorder::begin_frame() {
    if (!recording_) return;
    current_in_values_.clear();
    frame_started_ = true;
}

void RzxRecorder::record_in(uint8_t value) {
    if (!recording_ || !frame_started_) return;
    current_in_values_.push_back(value);
}

void RzxRecorder::end_frame(uint16_t instruction_count) {
    if (!recording_ || !frame_started_) return;

    RzxFrame frame;
    frame.instruction_count = instruction_count;
    frame.in_values = std::move(current_in_values_);
    rec_.frames.push_back(std::move(frame));

    current_in_values_.clear();
    frame_started_ = false;
}

void RzxRecorder::set_snapshot(std::vector<uint8_t> data, const std::string& ext) {
    rec_.snapshot_data = std::move(data);
    rec_.snapshot_ext = ext;
}
