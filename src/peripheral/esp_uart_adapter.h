#pragma once

#include "esp01/esp_at.h"
#include "peripheral/uart_device.h"

/// jnext's side of the emulated ESP-01 (GH #25, branch 3.5 of 6).
///
/// THE MODULE KNOWS NOTHING ABOUT JNEXT. `src/esp01` is a self-contained
/// component with no jnext types, no jnext include root and no jnext logger;
/// this file is the whole of the coupling, and it is deliberately five
/// forwarding methods long. If it ever grows a state machine, something has
/// been put on the wrong side of the line.
///
/// What it does:
///   * implements `UartDevice`, so `Uart::attach_device` can take it;
///   * installs a `ByteSink` that pushes into the guest's RX FIFO;
///   * mirrors the core's `wants_tick()` into `UartDevice`'s hot-path gate;
///   * binds the module's logging seam to jnext's `esp01` spdlog logger, so
///     `--log-level esp01=debug` works exactly as it did before the split.
///
/// WHAT IT DOES NOT DO: construct anything, own a transport, touch `Emulator`,
/// read config or register a CLI flag. Nothing attaches one of these yet —
/// that is branch 4.
class EspUartAdapter : public UartDevice {
public:
    /// Non-owning, and the ESP MUST OUTLIVE the adapter — the reverse of the
    /// usual reading, so it is worth being explicit. The adapter installs a
    /// sink that captures `this`, so if the adapter died first the ESP would
    /// hold a dangling callback. The destructor clears the sink for exactly
    /// that reason, which makes the ordering safe rather than merely
    /// documented.
    ///
    /// Accepts an `esp::EspDevice`, not an `esp::AtEngine`, so the SAME
    /// adapter drives the passive core inline or the threaded wrapper
    /// (`esp01/esp_threaded.h`). Which one is a construction-site decision,
    /// and branch 4 makes it.
    explicit EspUartAdapter(esp::EspDevice& esp);
    ~EspUartAdapter() override;

    EspUartAdapter(const EspUartAdapter&)            = delete;
    EspUartAdapter& operator=(const EspUartAdapter&) = delete;

    /// One byte finished transmitting out of the guest's TX FIFO.
    void receive(std::uint8_t byte) override;

    /// Once per frame from the host loop. Also the point at which the tick
    /// gate is re-evaluated for work the ESP produced on its own (a socket
    /// read under the threaded wrapper), and at which the seam's threshold is
    /// re-synced from the spdlog logger so a level changed at runtime takes
    /// effect.
    void poll() override;

    /// Per Z80 instruction, gated by `tick_wanted()`. Forwards jnext's 28 MHz
    /// tick count and the channel's live `byte_transfer_ticks()`.
    void tick(std::uint32_t master_cycles, std::uint32_t byte_ticks) override;

private:
    void mirror_gate();

    esp::EspDevice& esp_;
};

/// Point the module's logging seam at jnext's `esp01` logger, and set its
/// threshold from that logger's current level.
///
/// Called by the adapter's constructor, and exposed for the suite that tests
/// the binding. Idempotent.
///
/// WHY THE THRESHOLD IS COPIED rather than queried per call: the seam's gate
/// has to be cheap enough to sit on the per-tick pacing path, and an
/// indirection into spdlog on every trace call is not that. `esp_sync_log_level`
/// re-copies it, and `EspUartAdapter::poll` calls that once per frame, so a
/// level changed at runtime is live within one frame.
void esp_bind_logging();
void esp_sync_log_level();
