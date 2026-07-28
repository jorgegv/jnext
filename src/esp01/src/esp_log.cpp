// The module's logging seam (GH #25, branch 3.5). Rationale is in esp_log.h.

#include "esp01/esp_log.h"

#include <cstdio>
#include <mutex>
#include <utility>

namespace esp {
namespace {

/// The sink is shared mutable state (see esp_log.h on why the binding is
/// global), and the threaded wrapper logs from its own thread, so it is
/// guarded. The mutex is held only for the duration of the sink call itself —
/// never across a socket operation, never across formatting, which happens
/// before `log_emit` is reached.
std::mutex& sink_mutex() {
    static std::mutex m;
    return m;
}

LogSink& sink() {
    static LogSink s;
    return s;
}

/// Read on EVERY log call, on the hot pacing path, so it is atomic rather than
/// mutex-guarded: `log_wanted` must not take a lock just to decide there is
/// nothing to do. `sink_installed` is a separate atomic for the same reason —
/// checking the `std::function` itself would need the mutex.
std::atomic<int>  g_threshold{static_cast<int>(LogLevel::Info)};
std::atomic<bool> g_sink_installed{false};

}  // namespace

const char* log_level_text(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info:  return "info";
        case LogLevel::Warn:  return "warn";
        case LogLevel::Error: return "error";
    }
    return "unknown";
}

void set_log_sink(LogSink s) {
    std::lock_guard<std::mutex> lock(sink_mutex());
    const bool installed = static_cast<bool>(s);
    sink()                = std::move(s);
    // Set AFTER the assignment, so no other thread can observe "installed"
    // while the function object is being replaced.
    g_sink_installed.store(installed, std::memory_order_release);
}

void set_log_threshold(LogLevel level) {
    g_threshold.store(static_cast<int>(level), std::memory_order_relaxed);
}

LogLevel log_threshold() {
    return static_cast<LogLevel>(g_threshold.load(std::memory_order_relaxed));
}

namespace detail {

bool log_wanted(LogLevel level) {
    if (!g_sink_installed.load(std::memory_order_acquire)) return false;
    return static_cast<int>(level) >= g_threshold.load(std::memory_order_relaxed);
}

void log_emit(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(sink_mutex());
    // Re-checked under the lock: `log_wanted` said yes, but the sink can be
    // cleared between that check and here.
    if (sink()) sink()(level, message);
}

}  // namespace detail

std::string log_hex_byte(std::uint8_t value) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02X", value);
    return std::string(buf);
}

}  // namespace esp
