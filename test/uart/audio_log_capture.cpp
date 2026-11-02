// AudioLogCapture of uart_posix_test: the lines the `audio` logger writes while the object lives.
// A translation unit of its own for the reason given in audio_log_capture.h.
#include "audio_log_capture.h"

#include "core/log.h"

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <sstream>

namespace nxs {

struct AudioLogCapture::Impl {
    std::ostringstream out;   // before `sink`: the sink is built on it
    std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);
};

AudioLogCapture::AudioLogCapture() : impl_(new Impl) { Log::audio()->sinks().push_back(impl_->sink); }

AudioLogCapture::~AudioLogCapture() {
    auto& sinks = Log::audio()->sinks();
    sinks.erase(std::remove(sinks.begin(), sinks.end(), impl_->sink), sinks.end());
}

std::string AudioLogCapture::text() const { return impl_->out.str(); }

} // namespace nxs
