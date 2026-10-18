// UartLogCapture of nextpi_test (GH #385): the lines the `uart` logger writes while the object lives.
// A translation unit of its own because spdlog declares a `fmt` namespace, which the suite's own
// `fmt()` helper (printf-style detail strings) would then share a name with.
#include "nextpi_support.h"

#include "core/log.h"

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <sstream>

namespace nxs {

struct UartLogCapture::Impl {
    std::ostringstream out;   // before `sink`: the sink is built on it
    std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);
};

UartLogCapture::UartLogCapture() : impl_(new Impl) { Log::uart()->sinks().push_back(impl_->sink); }

UartLogCapture::~UartLogCapture() {
    auto& sinks = Log::uart()->sinks();
    sinks.erase(std::remove(sinks.begin(), sinks.end(), impl_->sink), sinks.end());
}

std::string UartLogCapture::text() const { return impl_->out.str(); }

} // namespace nxs
