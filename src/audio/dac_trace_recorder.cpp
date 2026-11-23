#include "audio/dac_trace_recorder.h"
#include "core/output_files.h"

#include <cerrno>
#include <cstring>

DacTraceRecorder::~DacTraceRecorder()
{
    stop();
}

bool DacTraceRecorder::start(const std::string& path)
{
    if (is_recording()) {
        last_error_ = "a DAC trace is already active";
        return false;
    }

    last_error_.clear();
    failed_ = false;
    have_tstate_ = false;
    segment_ = 0;
    file_ = std::fopen(path.c_str(), "wb");   // binary: "\n" lines on every host (GH #319)
    if (!file_) {
        set_error("cannot open '" + path + "': " + std::strerror(errno));
        return false;
    }
    if (std::fputs("segment,tstate,channel,value\n", file_) < 0) {
        set_error("failed to write DAC trace header");
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }
    path_ = path;
    output_files::begin(path_);
    return true;
}

void DacTraceRecorder::capture(uint64_t tstate, int channel, uint8_t value)
{
    if (!file_ || failed_) return;
    if (have_tstate_ && tstate < last_tstate_) ++segment_;
    last_tstate_ = tstate;
    have_tstate_ = true;

    if (std::fprintf(file_, "%u,%llu,%d,%u\n", segment_,
                     static_cast<unsigned long long>(tstate), channel,
                     static_cast<unsigned>(value)) < 0) {
        set_error("failed while writing DAC trace");
    }
}

bool DacTraceRecorder::stop()
{
    if (!file_) return !failed_;
    if (std::fclose(file_) != 0 && !failed_) {
        set_error("failed to close DAC trace");
    }
    file_ = nullptr;
    output_files::end(path_);
    return !failed_;
}

void DacTraceRecorder::set_error(const std::string& message)
{
    failed_ = true;
    if (last_error_.empty()) last_error_ = message;
}
