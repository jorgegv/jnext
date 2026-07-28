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
///   * Destroying the `Uart` leaves the device holding an `RxSink` that
///     captured a `Uart*` — detach first.
///   * A COLD BOOT is the dangerous case, and it does NOT present as a
///     dangling pointer. `emulator_cold_boot` (`platform/emulator_boot.h:67-77`)
///     does `emu.~Emulator(); new (&emu) Emulator();` — placement-new at the
///     SAME address — and `Uart uart_` is a value member (`emulator.h:852`),
///     so a sink captured before the boot still holds a perfectly valid
///     `Uart*`. It now silently addresses the NEWLY BOOTED machine's UART:
///     nothing crashes, and the device's next `send_to_guest` injects into
///     the fresh machine's RX FIFO. Silent corruption of post-reset guest
///     state is worse than a crash, so do not expect a sanitiser to catch it.
///     Every hard reset takes this path (`headless_app.cpp:593`,
///     `sdl_app.cpp:342`, `qt_app.cpp:446`). The fix is to re-bind from
///     `ColdBootHooks::rewire_host` (`emulator_boot.h:110-115`, invoked at
///     `:160`) — the seam that exists precisely to re-point frontend objects
///     at the reconstructed emulator — not an ad-hoc detach somewhere else.
///
/// The device is deliberately NOT told about UART resets, and that is a
/// narrower claim than "the ESP cannot be reset" — it can:
///
///   * A REAL ESP + expansion-bus reset line exists, driven by NextREG 0x02
///     bit 7. `zxnext.vhd:5119` latches it (`nr_02_bus_reset <= nr_wr_dat(7)`)
///     and `zxnext.vhd:1579` drives the pin (`o_RESET_PERIPHERAL <=
///     nr_02_bus_reset`); `nextreg.txt` documents the write bit verbatim as
///     "Assert and hold reset to the expansion bus and the esp wifi". jnext
///     already latches it into `nr_02_bus_reset_` (`emulator.cpp:2719`) and
///     NOTHING consumes it. nextsync uses exactly this as its "Resetting esp,
///     try again" recovery path, so a device-facing hook for it is a real
///     future requirement — one driven from the NR 0x02 WRITE, entirely
///     separate from the UART channel reset below. It is deliberately not
///     built here: this branch adds no NextREG wiring.
///
///   * What must NOT drive a device reset is `UartChannel::reset`. It fires
///     on a machine soft reset and on a guest write of framing bit 7
///     (uart.cpp:257-262), both of which reset only the Next-side UART state
///     machine — `uart_tx.vhd:166` returns TX to S_IDLE, `uart_rx.vhd:219`
///     returns RX to S_PAUSE. Neither touches the module on the far end of
///     the cable, so hanging a device reset off this path would hand the
///     guest a capability the silicon does not have.
///
/// (NR 0xA8/0xA9 are NOT a reset either: they drive ESP GPIO0, the
/// bootloader-select pin, and GPIO2 is input-only.)
///
/// So `UartDevice` has no `reset()`: the one legitimate reset source needs a
/// different hook than the one the UART could offer, and device lifecycle
/// otherwise stays with the owner.
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
    ///
    /// `poll()` COVERS ONLY THE WALL-CLOCK HALF. The emulated-time half is
    /// `tick()` below, which the AT-engine branch added together with its
    /// call site and its tests.
    virtual void poll() {}

    /// Emulated-time service point, called from `UartChannel::tick` — i.e.
    /// once per Z80 instruction, gated by `tick_wanted()`.
    ///
    /// WHY A SECOND HOOK EXISTS AT ALL. `poll()` cannot deliver RX. The
    /// measured nextsync constraints (issue #25) are that RX must be
    /// drip-fed at the live `prescaler * frame_bits` rate, because:
    ///   * nextsync reprograms the link to 1.152 Mbaud (`sync`) or 2 Mbaud
    ///     (`syncfast`) via `AT+UART_CUR`, so the comfortable "230 bytes per
    ///     frame at 115200" budget becomes 2304 bytes/frame into a 512-byte
    ///     FIFO — 4.5x over, i.e. guaranteed silent truncation; and
    ///   * each byte of an inbound `+IPD,<len>:` header gets a single ~314 us
    ///     window with no outer retry, so ANY per-frame (20 ms) chunking
    ///     fails 100% of the time regardless of FIFO size.
    /// So delivery is paced off exactly the clock the TX path already uses.
    ///
    /// @param master_cycles  28 MHz ticks elapsed, as handed to `UartChannel::tick`.
    /// @param byte_ticks     28 MHz ticks for ONE complete frame at the CURRENT
    ///                       framing and prescaler — `UartChannel::byte_transfer_ticks()`.
    ///                       Passed rather than queried so that implementations
    ///                       stay free of any UART header, exactly as `RxSink`
    ///                       does for the injection direction; and passed FRESH
    ///                       on every call so a mid-stream `AT+UART_CUR` baud
    ///                       switch changes the delivery rate immediately.
    ///
    /// THE HOT-PATH CONTRACT. `UartChannel::tick` runs per Z80 instruction, so
    /// this virtual must not be reached unless there is real work: the call
    /// site is `if (device_ && device_->tick_wanted())`. With no device
    /// attached — every pre-existing test and all of production until the
    /// CLI/config branch — the cost is one null test on a pointer already in
    /// the cache line being touched. With an attached but idle device it is
    /// one further `bool` load. `byte_transfer_ticks()` is only computed
    /// inside the guarded branch.
    virtual void tick(uint32_t master_cycles, uint32_t byte_ticks) {
        (void)master_cycles;
        (void)byte_ticks;
    }

    /// Hot-path gate for `tick()`. Non-virtual by design: the whole point is
    /// that an idle device costs a predictable branch, not a vtable
    /// dispatch. Devices raise and lower it with `set_tick_wanted`.
    bool tick_wanted() const { return tick_wanted_; }

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

    /// Raise/lower the `tick()` gate. Call it whenever the device's "do I
    /// have pending work?" answer changes; leaving it raised is merely
    /// wasteful, leaving it lowered with work outstanding STALLS the device.
    void set_tick_wanted(bool wanted) { tick_wanted_ = wanted; }

private:
    RxSink rx_sink_;
    bool   tick_wanted_ = false;
};
