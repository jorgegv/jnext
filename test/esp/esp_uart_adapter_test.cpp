// jnext's ESP-01 adapter unit tests (GH #25, branch 3.5 — the module/host seam).
//
// WHY THIS SUITE EXISTS SEPARATELY FROM THE MODULE'S OWN. `src/esp01` is a
// self-contained component with no jnext types, and its two suites ship with
// it (src/esp01/test/). Everything that is specifically jnext's — the
// `UartDevice` implementation, the `Uart::tick` call site that consumes the
// hot-path gate, and the binding of the module's logging seam to jnext's
// `esp01` spdlog logger — cannot live there without dragging jnext back into
// the module. It lives here.
//
// Most of the rows below are NOT new: HOOK-03..06b came from esp_at_test when
// `AtEngine` stopped being a `UartDevice`. They test the same behaviour
// through the adapter, which is now the thing that implements the interface.
//
// Run: ./build/test/esp_uart_adapter_test

#include "core/log.h"
#include "esp01/esp_at.h"
#include "esp01/esp_log.h"
#include "esp01/esp_threaded.h"
#include "peripheral/esp_uart_adapter.h"
#include "peripheral/uart.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>

using namespace esp;

// ── Tiny test harness (matches esp_at_test.cpp style) ────────────────────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static std::string printable(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '\r')      out += "\\r";
        else if (c == '\n') out += "\\n";
        else if (c >= 0x20 && c < 0x7F) out += static_cast<char>(c);
        else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02X", c);
            out += buf;
        }
    }
    return out;
}

static void check(const char* id, const std::string& desc, bool cond) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc.c_str());
    }
}

static void check_eq(const char* id, const std::string& desc, const std::string& got,
                     const std::string& want) {
    ++g_total;
    if (got == want) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n        want \"%s\"\n        got  \"%s\"\n", id,
                    desc.c_str(), printable(want).c_str(), printable(got).c_str());
    }
}

// ── Minimal fake transport ───────────────────────────────────────────────
//
// A cut-down twin of esp_at_test's: this suite tests the ADAPTER, so the
// engine only has to be alive enough to answer `AT`. Duplicated rather than
// shared because a header shared between a module suite and a host suite would
// re-create exactly the coupling this branch removed.

namespace {

class IdleTransport : public EspTransport {
public:
    bool begin_connect(const std::string&, std::uint16_t) override { return false; }
    void poll() override {}
    TransportState     state() const override { return state_; }
    const std::string& last_error() const override { return error_; }
    const IpAddress&   peer_address() const override { return peer_; }
    std::size_t send(const std::uint8_t*, std::size_t) override { return 0; }
    std::size_t recv(std::uint8_t*, std::size_t) override { return 0; }
    void close() override {}

private:
    TransportState state_ = TransportState::Idle;
    std::string    error_;
    IpAddress      peer_ = ipv4(192, 0, 2, 1);
};

/// Counts `tick()` calls and nothing else — the probe for the gate rows, which
/// are about jnext's `Uart`, not about the ESP.
struct CountingDevice : UartDevice {
    int ticks = 0;
    void receive(std::uint8_t) override {}
    void tick(std::uint32_t, std::uint32_t) override { ++ticks; }
    using UartDevice::set_tick_wanted;
};

/// One byte time in 28 MHz ticks at the UART's 115200 8N1 default
/// (prescaler 243 x 10 frame bits).
constexpr std::uint32_t BYTE_TICKS = 243 * 10;

}  // namespace

// ─────────────────────────────────────────────────────────────────────────

int main() {
    std::printf("\n=== ESP-01 jnext adapter tests (GH #25 branch 3.5) ===\n\n");

    Log::init();
    Log::parse_levels("esp01=off");

    // ══ Group A — the UartDevice gate and its Uart::tick call site ═══════
    //
    // Moved from esp_at_test (HOOK-03..06b) when the engine stopped being a
    // `UartDevice`. Unchanged in substance: they were always about jnext's
    // `Uart`, not about the ESP.

    {   // The gate must really gate: a device that never asks is never ticked.
        // Driven through `Uart::tick`, which is the real call site — the hook
        // deliberately lives there and not inside `UartChannel::tick`, for the
        // hot-path reason documented on `service_attached_device`.
        CountingDevice dev;
        Uart uart;
        uart.attach_device(0, &dev);
        for (int i = 0; i < 100; ++i) uart.tick(10);
        check("HOOK-03", "Uart does not tick a device that lowered its gate", dev.ticks == 0);
        dev.set_tick_wanted(true);
        for (int i = 0; i < 100; ++i) uart.tick(10);
        check("HOOK-03b", "...and ticks it once per call when raised", dev.ticks == 100);
        uart.detach_device(0);
        dev.ticks = 0;
        for (int i = 0; i < 100; ++i) uart.tick(10);
        check("HOOK-03c", "...and stops entirely once detached", dev.ticks == 0); }

    {   // End to end through the REAL UartChannel: guest TX -> adapter ->
        // engine -> paced RX FIFO, at the channel's own byte time.
        IdleTransport  tr;
        AtEngine       eng{tr};
        EspUartAdapter adapter{eng};
        Uart           uart;
        uart.attach_device(0, &adapter);
        UartChannel&   ch = uart.channel(0);

        for (char c : std::string("AT\r\n")) ch.write_tx(static_cast<std::uint8_t>(c));
        // 4 TX byte times to get the command out, then 6 more for the reply.
        for (int i = 0; i < 4 + 6; ++i) uart.tick(BYTE_TICKS);
        std::string got;
        while (ch.rx_avail()) got.push_back(static_cast<char>(ch.read_rx()));
        check_eq("HOOK-04", "a real Uart round-trips a command through the adapter", got,
                 "\r\nOK\r\n");
        uart.detach_device(0); }

    {   // Same path, but the guest reprograms its prescaler first — the
        // delivery rate must follow, with nothing else changed.
        IdleTransport  tr;
        AtEngine       eng{tr};
        EspUartAdapter adapter{eng};
        Uart           uart;
        uart.attach_device(0, &adapter);
        UartChannel&   ch = uart.channel(0);

        // Prescaler LSB write: bit 7 clear sets the low 7 bits, set sets the
        // next 7 (uart.h write_prescaler_lsb). 10 => a 10x faster link.
        ch.write_prescaler_lsb(0x0A);
        ch.write_prescaler_lsb(0x80);
        for (char c : std::string("AT\r\n")) ch.write_tx(static_cast<std::uint8_t>(c));
        const std::uint32_t fast_byte = 10 * 10;
        for (int i = 0; i < 4 + 6; ++i) uart.tick(fast_byte);
        std::string got;
        while (ch.rx_avail()) got.push_back(static_cast<char>(ch.read_rx()));
        check_eq("HOOK-05", "delivery follows the live prescaler, not a hardcoded 115200", got,
                 "\r\nOK\r\n");
        uart.detach_device(0); }

    {   CountingDevice dev;
        dev.set_tick_wanted(true);
        Uart uart;
        uart.attach_device(0, &dev);
        uart.channel(0).write_frame(0x98);   // framing bit 7 = UART held in reset
        for (int i = 0; i < 100; ++i) uart.tick(10);
        check("HOOK-06",
              "a UART held in reset stops device ticking — its RX FIFO is about to be cleared",
              dev.ticks == 0);
        uart.channel(0).write_frame(0x18);   // released: 8N1
        for (int i = 0; i < 100; ++i) uart.tick(10);
        check("HOOK-06b", "...and resumes when the guest releases it", dev.ticks == 100);
        uart.detach_device(0); }

    // ══ Group B — the adapter's own contract ════════════════════════════

    {   IdleTransport  tr;
        AtEngine       eng{tr};
        std::string    guest;
        EspUartAdapter adapter{eng};
        adapter.set_rx_sink([&guest](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });

        check("ADP-01", "a fresh adapter mirrors the engine's lowered gate",
              !adapter.tick_wanted());
        for (char c : std::string("AT\r\n")) adapter.receive(static_cast<std::uint8_t>(c));
        check("ADP-02", "receive() forwards to the engine AND raises the mirrored gate",
              adapter.tick_wanted());
        for (int i = 0; i < 32 && adapter.tick_wanted(); ++i)
            adapter.tick(BYTE_TICKS, BYTE_TICKS);
        check_eq("ADP-03", "tick() forwards, and the engine's output reaches the RX sink",
                 guest, "\r\nOK\r\n");
        check("ADP-04", "...and the gate falls again once the engine is idle",
              !adapter.tick_wanted()); }

    {   // The adapter must not out-live its own sink installation. Destroying
        // it clears the sink, so the engine cannot call back into freed
        // memory — which is what makes "the ESP outlives the adapter" a safe
        // ordering rather than an undocumented landmine.
        IdleTransport tr;
        AtEngine      eng{tr};
        int           calls = 0;
        {
            EspUartAdapter adapter{eng};
            adapter.set_rx_sink([&calls](std::uint8_t) { ++calls; });
            for (char c : std::string("AT\r\n")) adapter.receive(static_cast<std::uint8_t>(c));
            for (int i = 0; i < 32 && adapter.tick_wanted(); ++i)
                adapter.tick(BYTE_TICKS, BYTE_TICKS);
        }
        const int before = calls;
        check("ADP-05", "the adapter delivered while it was alive", before == 6);
        // The engine survives; anything it now wants to say must go nowhere.
        for (char c : std::string("AT\r\n")) eng.receive(static_cast<std::uint8_t>(c));
        for (int i = 0; i < 32 && eng.wants_tick(); ++i) eng.tick(BYTE_TICKS, BYTE_TICKS);
        check("ADP-06", "...and after its destruction the engine's sink is cleared, not dangling",
              calls == before); }

    {   // The adapter accepts an EspDevice, so it drives the threaded wrapper
        // with no change at all. This row is the proof that the abstraction is
        // real rather than decorative.
        IdleTransport  tr;
        ThreadedEsp    esp{tr};
        std::string    guest;
        EspUartAdapter adapter{esp};
        adapter.set_rx_sink([&guest](std::uint8_t b) { guest.push_back(static_cast<char>(b)); });
        esp.start();
        for (char c : std::string("AT\r\n")) adapter.receive(static_cast<std::uint8_t>(c));
        // poll() is what re-evaluates the mirrored gate for work the ESP did on
        // its own thread; jnext calls it once per frame.
        for (int i = 0; i < 2000 && guest.size() < 6; ++i) {
            adapter.poll();
            for (int j = 0; j < 32 && adapter.tick_wanted(); ++j)
                adapter.tick(BYTE_TICKS, BYTE_TICKS);
            esp.wait_idle(1);
        }
        check_eq("ADP-07", "the same adapter drives the THREADED wrapper unchanged", guest,
                 "\r\nOK\r\n"); }

    // ══ Group C — the logging binding ═══════════════════════════════════
    //
    // The module logs through its own seam; these rows prove jnext's end of it
    // is wired. LOG-01/LOG-02 came from esp_socket_test, which no longer knows
    // spdlog exists.

    check("LOG-01", "Log::init() registers the esp01 logger",
          spdlog::get("esp01") != nullptr);
    {
        Log::parse_levels("esp01=trace");
        check("LOG-02", "--log-level esp01=trace reaches the logger",
              spdlog::get("esp01") &&
                  spdlog::get("esp01")->level() == spdlog::level::trace);
        Log::parse_levels("esp01=off");
    }
    {
        // The binding, end to end: a line the ENGINE emits through the module
        // seam must come out of jnext's spdlog logger.
        auto ring  = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(64);
        auto saved = Log::esp01()->sinks();
        Log::esp01()->sinks() = {ring};
        Log::parse_levels("esp01=debug");

        IdleTransport  tr;
        AtEngine       eng{tr};
        EspUartAdapter adapter{eng};   // constructor binds the seam
        // poll() re-syncs the threshold from the logger, which is what makes a
        // level changed after construction take effect.
        adapter.poll();
        for (char c : std::string("AT\r\n")) adapter.receive(static_cast<std::uint8_t>(c));

        std::string all;
        for (const auto& line : ring->last_formatted()) all += line;
        Log::parse_levels("esp01=off");
        Log::esp01()->sinks() = saved;

        check("LOG-03", "a module log line reaches jnext's esp01 logger through the seam",
              all.find("AT <- \"AT\"") != std::string::npos);
        check("LOG-04", "...and carries the level the module chose, not a flattened one",
              all.find("[debug]") != std::string::npos);
    }
    {
        // The other direction: with the logger turned down, the seam threshold
        // follows, so the module does not even format the line.
        Log::parse_levels("esp01=off");
        IdleTransport  tr;
        AtEngine       eng{tr};
        EspUartAdapter adapter{eng};
        adapter.poll();
        check("LOG-05", "an esp01 logger at 'off' raises the module's threshold to error",
              log_threshold() == LogLevel::Error);
        Log::parse_levels("esp01=trace");
        adapter.poll();
        check("LOG-06", "...and turning it up lowers the threshold within one poll",
              log_threshold() == LogLevel::Trace);
        Log::parse_levels("esp01=off");
    }

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail,
                g_skip);
    return g_fail == 0 ? 0 : 1;
}
