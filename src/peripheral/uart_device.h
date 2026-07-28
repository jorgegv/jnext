#pragma once
#include <cstdint>
#include <functional>
#include <utility>

/// Backend for one UART channel — the thing on the other end of the wire.
///
/// On real hardware each of the two Next UARTs terminates in a physical
/// module: UART 0 goes to the ESP-01 WiFi header, UART 1 to the Raspberry Pi
/// GPIO header (zxnext.vhd:1611, :3381 — UART 0 is the ESP, NOT UART 1).
/// `UartDevice` is the emulator's stand-in for whatever is soldered there.
///
/// Attachment is non-owning, matching the established local convention for
/// `SpiDevice` (spi.h:6-26, `SpiMaster::attach_device`) and `I2cDevice`
/// (i2c.h:8-22): the caller constructs the device, attaches it, and outlives
/// it. Two lifetime hazards follow from that and are the ATTACHER's problem:
///
///   * Destroying a still-attached device leaves `UartChannel::device_`
///     dangling — detach first.
///   * Destroying the `Uart` (or the whole `Emulator`) leaves the device
///     holding an `RxSink` bound to freed memory — detach first. This is not
///     hypothetical: a hard reset is a power-on cold boot that RECONSTRUCTS
///     the Emulator (`headless_app.cpp:593`, `sdl_app.cpp:342`,
///     `qt_app.cpp:446` → `cold_boot()`), so anything holding a device across
///     a hard reset must detach and re-attach around it.
///
/// The device is deliberately NOT told about UART resets. `UartChannel::reset`
/// fires on a machine soft reset and on a guest write of framing bit 7
/// (uart.cpp:244-248) — both of which reset the Next's UART, not the module
/// on the far end of the cable. There is no ESP reset line in the VHDL at all
/// (NR 0xA8/0xA9 drive ESP GPIO0, the bootloader-select pin, and GPIO2 is
/// input-only), so `AT+RST` is the only reset path a guest has. Giving this
/// interface a `reset()` that the UART called would invent a capability the
/// hardware does not have; device lifecycle stays with the owner.
class UartDevice {
public:
    /// Guest-bound injection sink, handed to the device by
    /// `Uart::attach_device` and cleared by `Uart::detach_device`.
    ///
    /// A callback rather than a `Uart&` on purpose: it keeps implementations
    /// free of any UART header, so a device can be unit-tested by pointing
    /// the sink at a `std::vector`.
    using RxSink = std::function<void(uint8_t byte)>;

    virtual ~UartDevice() = default;

    /// One byte has finished transmitting out of the guest's TX FIFO.
    ///
    /// Called from `UartChannel::deliver_tx_byte` at byte boundaries — NOT
    /// per instruction. At 115200 8N1 that caps out around 230 calls/frame,
    /// so a virtual call here is free; the per-instruction `UartChannel::tick`
    /// fast path (idle + empty FIFO → immediate return) is untouched.
    virtual void receive(uint8_t byte) = 0;

    /// Coarse service point for devices that need to make progress in
    /// wall-clock time (draining a socket, timing out a connection).
    ///
    /// NOTHING CALLS THIS YET. It is declared here because the cadence
    /// decision belongs to the device, not the UART: `Uart::tick` runs once
    /// per Z80 instruction (~10^5-10^6 times/frame — `emulator.cpp:7944`),
    /// which is far too hot for socket work, so `poll()` must never be
    /// hung off it. The intended call site is once per frame from the host
    /// loop, wired by the CLI/config branch that owns the frame-loop change.
    virtual void poll() {}

    /// Install (or clear, with `nullptr`) the guest-bound sink.
    /// Called by `Uart::attach_device` / `Uart::detach_device`.
    void set_rx_sink(RxSink sink) { rx_sink_ = std::move(sink); }

    /// True while a sink is installed, i.e. while attached.
    bool has_rx_sink() const { return static_cast<bool>(rx_sink_); }

protected:
    /// Push one byte toward the guest's RX FIFO. No-op when detached.
    ///
    /// Lands in the FIFO via `Uart::inject_rx`, so it drives the RX-available
    /// path and the IM2 UART RX vector exactly like any other received byte.
    /// There is no baud pacing on this path (uart.cpp:107-116 paces TX only)
    /// and the RX FIFO is 512 bytes with drop-newest overflow — a device that
    /// pushes faster than the guest drains WILL lose bytes. Pacing is the
    /// device's responsibility.
    void send_to_guest(uint8_t byte) const {
        if (rx_sink_) rx_sink_(byte);
    }

private:
    RxSink rx_sink_;
};
