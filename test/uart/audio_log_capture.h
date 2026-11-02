// The lines the `audio` logger writes while the object lives (the Pi audio tape-mode underrun warning
// goes through it): a sink on a stream is added to the logger and taken off again. Defined in a
// translation unit of its own, audio_log_capture.cpp, for the reason nextpi_log_capture.cpp gives:
// spdlog's `fmt` namespace clashes with the suite's own `fmt()` helper.
#pragma once
#include <memory>
#include <string>

namespace nxs {

class AudioLogCapture {
public:
    AudioLogCapture();
    ~AudioLogCapture();
    AudioLogCapture(const AudioLogCapture&)            = delete;
    AudioLogCapture& operator=(const AudioLogCapture&) = delete;
    std::string text() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nxs
