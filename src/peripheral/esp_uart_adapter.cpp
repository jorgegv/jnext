// jnext's adapter for the self-contained ESP-01 module. Rationale in the
// header; this file only implements it.

#include "peripheral/esp_uart_adapter.h"

#include "core/log.h"
#include "esp01/esp_log.h"

namespace {

/// esp::LogLevel -> spdlog. The module's five levels are a subset of spdlog's,
/// so this direction is total and lossless.
spdlog::level::level_enum to_spdlog(esp::LogLevel level) {
    switch (level) {
        case esp::LogLevel::Trace: return spdlog::level::trace;
        case esp::LogLevel::Debug: return spdlog::level::debug;
        case esp::LogLevel::Info:  return spdlog::level::info;
        case esp::LogLevel::Warn:  return spdlog::level::warn;
        case esp::LogLevel::Error: return spdlog::level::err;
    }
    return spdlog::level::info;
}

/// spdlog -> esp::LogLevel, for the threshold only. The direction that is NOT
/// total: spdlog has `critical` and `off` above `err`, and the module has no
/// level above Error. Both map to Error, which means an `off` logger still
/// pays to FORMAT an error line before spdlog drops it. That is the right
/// trade — errors are rare, and the alternative (an "off" value in the
/// module's enum) would put a host's filtering policy inside the module.
esp::LogLevel from_spdlog(spdlog::level::level_enum level) {
    switch (level) {
        case spdlog::level::trace: return esp::LogLevel::Trace;
        case spdlog::level::debug: return esp::LogLevel::Debug;
        case spdlog::level::info:  return esp::LogLevel::Info;
        case spdlog::level::warn:  return esp::LogLevel::Warn;
        default:                   return esp::LogLevel::Error;
    }
}

}  // namespace

void esp_bind_logging() {
    esp::set_log_sink([](esp::LogLevel level, const std::string& message) {
        // spdlog applies its own level check too, so a message that passes the
        // seam's threshold can still be dropped here. Deliberate: the seam's
        // threshold exists to avoid FORMATTING work, and jnext's logger stays
        // the single place the user's `--log-level` decision is honoured.
        Log::esp01()->log(to_spdlog(level), message);
    });
    esp_sync_log_level();
}

void esp_sync_log_level() {
    esp::set_log_threshold(from_spdlog(Log::esp01()->level()));
}

EspUartAdapter::EspUartAdapter(esp::EspDevice& esp) : esp_(esp) {
    esp_bind_logging();
    // The sink runs on whichever thread calls `tick()` — the emulation thread
    // — because the module only ever emits from there (EspDevice::ByteSink).
    // That is what makes writing straight into the guest's RX FIFO safe even
    // when the ESP itself is running on its own thread.
    esp_.set_output([this](std::uint8_t byte) { send_to_guest(byte); });
    mirror_gate();
}

EspUartAdapter::~EspUartAdapter() {
    // The sink captured `this`. Clearing it is what makes "the ESP outlives
    // the adapter" safe rather than merely required.
    esp_.set_output(nullptr);
}

void EspUartAdapter::receive(std::uint8_t byte) {
    if (inert_) return;
    esp_.receive(byte);
    mirror_gate();
}

void EspUartAdapter::poll() {
    esp_sync_log_level();
    if (inert_) return;
    esp_.poll();
    mirror_gate();
}

void EspUartAdapter::tick(std::uint32_t master_cycles, std::uint32_t byte_ticks) {
    if (inert_) return;
    esp_.tick(master_cycles, byte_ticks);
    mirror_gate();
}

void EspUartAdapter::set_inert(bool inert) {
    if (inert == inert_) return;
    inert_ = inert;
    // Lower the hot-path gate immediately on the way in, so `UartChannel::tick`
    // stops reaching the virtual at once rather than after one more call; on
    // the way out, re-read the ESP so work that arrived while we were silent
    // starts flowing again without waiting for the next `poll()`.
    if (inert_) set_tick_wanted(false);
    else        mirror_gate();
}

void EspUartAdapter::mirror_gate() {
    // `UartDevice::tick_wanted()` is non-virtual by design (it is tested once
    // per Z80 instruction), so it cannot delegate to the ESP — it has to be
    // MIRRORED, and only at the points jnext calls into the adapter.
    //
    // THE CONSEQUENCE, stated because it is the one thing a reader should
    // know: under the threaded wrapper the ESP can decide it has work while
    // jnext is not calling anything at all (a socket read on the wrapper's
    // thread). The gate then stays lowered until the next call — which is
    // `poll()`, once per frame. So a byte that arrives from the network starts
    // flowing to the guest within one frame of arriving, never later. That is
    // an order of magnitude inside every timeout in the evidenced clients, and
    // it is not the RX PACING (which is per-tick once the gate is up); it is
    // only the latency of noticing.
    set_tick_wanted(esp_.wants_tick());
}
