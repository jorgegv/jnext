#include "log_capture.h"

#include "core/log.h"
#include <spdlog/sinks/ostream_sink.h>

#include <memory>
#include <sstream>

std::string capture_emulator_log(const std::function<void()>& body) {
    std::ostringstream oss;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
    sink->set_level(spdlog::level::trace);
    auto lg = Log::emulator();
    const auto old = lg->level();
    lg->set_level(spdlog::level::trace);
    lg->sinks().push_back(sink);
    body();
    lg->flush();
    lg->sinks().pop_back();
    lg->set_level(old);
    return oss.str();
}
