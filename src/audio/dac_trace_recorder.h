#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

/// Writes guest DAC activity to CSV. A cold boot starts a new segment when
/// the emulated T-state clock returns to zero.
class DacTraceRecorder {
public:
    DacTraceRecorder() = default;
    ~DacTraceRecorder();

    DacTraceRecorder(const DacTraceRecorder&) = delete;
    DacTraceRecorder& operator=(const DacTraceRecorder&) = delete;

    bool start(const std::string& path);
    void capture(uint64_t tstate, int channel, uint8_t value);
    bool stop();

    bool is_recording() const { return file_ != nullptr; }
    const std::string& last_error() const { return last_error_; }

private:
    void set_error(const std::string& message);

    std::FILE* file_ = nullptr;
    uint64_t last_tstate_ = 0;
    unsigned segment_ = 0;
    bool have_tstate_ = false;
    bool failed_ = false;
    std::string last_error_;
};
