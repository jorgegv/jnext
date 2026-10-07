#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "peripheral/joy_uart_link.h"
#include "peripheral/uart_device.h"

/// The Raspberry Pi on the GPIO header, as a LIVE host endpoint on UART 1
/// (the Pi Zero, `--pizero`). Design: doc/design/PIZERO-DESIGN.md.
///
/// The Next talks to its Pi Zero accelerator (NextPi) over UART 1, whose module
/// end is the Pi GPIO header (uart_device.h: UART 0 is the ESP, UART 1 the Pi).
/// jnext does not emulate the Pi; it hands the wire to the host, where a real
/// NextPi — on real hardware or under QEMU — or any serial program can answer.
///
/// IT IS A `UartDevice`, unlike the joystick cable it borrows its plumbing from.
/// `JoyUartLink`'s header explains why that cable cannot be one: it has no
/// channel of its own. The Pi has exactly one, so it attaches like the ESP does
/// and inherits that seam whole — TX at byte boundaries through `receive()`,
/// paced RX through `tick()`, the joystick mux's isolation of UART 1
/// (`Uart::attach_device`'s sink drops what the Pi sends while NR 0x0B gives
/// the channel to the joystick connector), and no loopback while attached.
///
/// THE HOST HALF IS `JoyUartLink`, reused rather than copied: the non-blocking
/// descriptors, the pacing at the guest's own programmed baud, the bounded
/// queues, peer loss and the replay gate are the same problem with the same
/// answer, and that class is already tested against real FIFOs and ptys. Its
/// `connector` is meaningless here and fixed at 0.
///
/// THE GPIO GATE. The Pi's pins reach UART 1 only through the NR 0xA0 GPIO mux:
/// bit 4 and bit 5 select whether UART 1 is on GPIO 14/15 at all and which way
/// round its RX and TX are wired. A Pi needs them crossed against a Pi HAT's
/// orientation, and NextPi's own tools (`.pisend`) write NR 0xA0 = 0x30 before
/// talking to it. So the device asks a probe — installed by the owner, which
/// knows NR 0xA0 — whether the wire is connected, and drops bytes in both
/// directions when it is not, counting them so the loss is visible. With no
/// probe installed the wire is always connected, which is what a unit test of
/// the plumbing wants.
///
/// NOTHING HERE IS SERIALISED, for the reasons `JoyUartLink` and
/// `EspUartAdapter::set_inert` give: a live peer cannot be snapshotted or
/// rewound. The owner holds the device INERT while a replay re-executes frames.
class PiUartDevice : public UartDevice {
public:
    /// "Is the wire between UART 1 and the Pi connected right now?"
    using ConnectedProbe = std::function<bool()>;

    /// @param endpoint  the host-side descriptors. Never null.
    explicit PiUartDevice(std::unique_ptr<JoyUartEndpoint> endpoint);
    ~PiUartDevice() override;

    PiUartDevice(const PiUartDevice&)            = delete;
    PiUartDevice& operator=(const PiUartDevice&) = delete;

    void set_connected_probe(ConnectedProbe probe) { connected_ = std::move(probe); }

    /// Guest → Pi: one byte finished transmitting out of UART 1's TX FIFO.
    void receive(uint8_t byte) override;

    /// Once per frame from the host loop: flush and read the descriptors.
    void poll() override;

    /// Per Z80 instruction while host bytes are queued: pace them into UART 1's
    /// RX FIFO at the channel's live baud.
    void tick(uint32_t master_cycles, uint32_t byte_ticks) override;

    /// REPLAY GATE — see `JoyUartLink::set_inert`. Also holds the tick gate down.
    void set_inert(bool inert);

    /// "pty <slave path>" or "FIFO pair ...", for the startup log line.
    const std::string& describe() const { return link_.describe(); }

    /// The host link, for counters and fault reporting. Its `dropped()` counts
    /// Pi bytes that arrived while the GPIO gate was CLOSED, i.e. lost on a wire
    /// the Next was not listening to; `delivered()` counts those handed to UART 1.
    const JoyUartLink& link() const { return link_; }

    /// Guest bytes the Pi never heard because the GPIO gate was closed.
    std::size_t tx_disconnected() const { return tx_disconnected_; }

private:
    bool connected() const { return !connected_ || connected_(); }

    /// Raise the per-instruction tick gate exactly while there is RX to pace.
    void mirror_gate() { set_tick_wanted(!inert_ && link_.rx_pending()); }

    JoyUartLink    link_;
    ConnectedProbe connected_;
    std::size_t    tx_disconnected_ = 0;
    bool           inert_           = false;
};
