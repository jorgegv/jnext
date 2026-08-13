#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// A host-side serial byte source on the joystick-connector UART RX pin
/// (GH #251).
///
/// This is the emulator's stand-in for the far end of a joy-port serial cable
/// — the "PC" in a dezogif / DeZog rig, whose bytes reach the Next through the
/// NR 0x0B I/O-mode multiplexer instead of through either UART header.
///
/// WHY IT IS NOT A `UartDevice`. A `UartDevice` is soldered to ONE channel's
/// wire (ESP-01 on UART 0, Pi header on UART 1 — uart_device.h). This cable
/// has no channel of its own: the channel it lands on is chosen at run time by
/// NR 0x0B bit 0 (zxnext.vhd:3340-3341), and whether it is looked at at all
/// depends on NR 0x0B bit 4 matching the connector it is plugged into
/// (zxnext.vhd:3538). Both decisions belong to the mux, so the source is owned
/// by the Emulator and pushed through `Emulator::inject_joy_uart_rx`, whose
/// sink applies them.
///
/// BYTES ARE LOST, NOT QUEUED, when the mux is not routing this connector to a
/// channel. That is what the cable does: the PC transmits on its own schedule
/// and the FPGA either is or is not looking at the pin. Holding the stream back
/// until the guest happens to enable the mux would be friendlier and would also
/// make it impossible to test the case the hardware actually has, so the
/// schedule is the caller's to choose (`--joy-uart-rx-delay-frames`) and this
/// class only reports the loss — see `delivered()` / `dropped()`, which the
/// Emulator turns into a one-shot warning. A source that ran to exhaustion
/// having delivered nothing is the silent no-op this feature exists to avoid.
class JoyUartSource {
public:
    /// Guest-bound sink, installed by the owner. Called once per byte that has
    /// finished arriving on the wire; the sink decides whether the mux lets it
    /// through, and reports that back via `note_delivered` / `note_dropped`.
    using ByteSink = std::function<void(uint8_t byte)>;

    /// @param bytes         the whole stream, read from the host up front.
    /// @param connector     which joystick connector the cable is in —
    ///                      0 = joy 1 (`i_JOY_LEFT`), 1 = joy 2 (`i_JOY_RIGHT`).
    /// @param delay_frames  hold the stream for this many complete emulated
    ///                      frames before the first byte starts arriving.
    JoyUartSource(std::vector<uint8_t> bytes, int connector, uint32_t delay_frames);

    void set_byte_sink(ByteSink sink) { sink_ = std::move(sink); }

    int  connector()   const { return connector_; }
    bool exhausted()   const { return pos_ >= bytes_.size(); }

    /// Still inside the start delay — `delay_frames` complete frames have not
    /// gone by yet. `delay_frames == 0` is never waiting, so the stream is live
    /// from the very first instruction.
    bool waiting()     const { return frames_ < delay_frames_; }

    std::size_t size()      const { return bytes_.size(); }
    std::size_t delivered() const { return delivered_; }
    std::size_t dropped()   const { return dropped_; }

    /// One emulated frame has COMPLETED. Called from the owner's once-per-frame
    /// END seam, which is what makes the count mean what the option says: with
    /// `delay_frames == N` the stream is silent for exactly N whole frames and
    /// live in the (N+1)th. Counting frame STARTS instead would be off by one,
    /// since the first frame's start also fires.
    void end_frame();

    /// Advance emulated time and emit whatever bytes have finished arriving.
    ///
    /// @param master_cycles  28 MHz ticks elapsed since the last call.
    /// @param byte_ticks     28 MHz ticks for ONE complete frame at the
    ///                       RECEIVING channel's current framing and
    ///                       prescaler (`UartChannel::byte_transfer_ticks()`),
    ///                       passed fresh on every call exactly as
    ///                       `UartDevice::tick` takes it, so a mid-stream baud
    ///                       change re-paces delivery at once.
    ///
    /// Pacing off the Next's own baud rather than a fixed host baud is the same
    /// choice the ESP RX path makes (uart_device.h): jnext's RX path is
    /// byte-level, so a real baud MISMATCH is not modelled either way, and
    /// pacing off the guest means a correctly-configured rig never overruns the
    /// 512-byte RX FIFO relative to the guest's own drain rate.
    void tick(uint32_t master_cycles, uint32_t byte_ticks);

    /// Delivery outcome, reported by the sink because only the sink knows the
    /// mux state. Kept as counters rather than a bool so that a partially
    /// routed stream (the guest enabled the mux half way through) is still
    /// distinguishable from one that never arrived at all.
    void note_delivered() { ++delivered_; }
    void note_dropped()   { ++dropped_; }

    /// State serialisation — the CURSOR, not the stream (GH #251 review round 1).
    ///
    /// A rewind restores the machine, including the UART RX FIFOs, from a
    /// snapshot and then RE-EXECUTES the intervening frames. Without this the
    /// cable kept the position it had reached at the newest frame while the
    /// machine went back to an older one, so the replay delivered a different
    /// byte stream than the run it was supposed to be reproducing — measured as
    /// `delivered()` ending HIGHER after a rewind than it had been at the frame
    /// the rewind landed before.
    ///
    /// THE CABLE IS REWOUND RATHER THAN MADE INERT, which is the opposite of
    /// what the ESP does (`EspUartAdapter::set_inert`, driven from
    /// `Emulator::service_esp_frame`), and the difference is the whole reason
    /// that call exists: a socket cannot be re-executed — a replayed
    /// `AT+CIPSTART` opens a second real connection — so the ESP has nothing to
    /// rewind TO and can only be frozen. This stream is a byte vector still
    /// sitting in memory, so it CAN be rewound, and rewinding it makes the
    /// replay exact instead of merely harmless. Freezing it here would have left
    /// the rewound timeline permanently missing every byte delivered in the
    /// interval it re-ran.
    ///
    /// `bytes_` itself is deliberately not written: it is immutable after
    /// construction, and `Emulator::save_state` is consumed only by
    /// `RewindBuffer` (`src/debug/rewind_buffer.cpp`), i.e. in memory and within
    /// one run, so the vector a snapshot would restore is the one already held.
    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    std::vector<uint8_t> bytes_;
    ByteSink             sink_;
    std::size_t          pos_       = 0;
    std::size_t          delivered_ = 0;
    std::size_t          dropped_   = 0;
    int                  connector_ = 0;
    uint32_t             delay_frames_ = 0;
    // Completed frames, saturating at `delay_frames_`: past the hold there is
    // nothing left to count, and a wrap would restart the hold and stall a
    // stream already in flight.
    uint32_t             frames_       = 0;
    // Ticks still to run before the byte at `pos_` has finished arriving. 0
    // means "not started"; `tick` arms it from the byte_ticks it is handed, so
    // the first byte takes a full frame time to arrive rather than appearing
    // instantly at the delay boundary.
    uint32_t             timer_        = 0;
};

/// Read `path` whole, for use as a `JoyUartSource` stream. Returns false and
/// leaves `bytes` untouched on any failure; `error` gets a message suitable for
/// printing to the user.
bool read_joy_uart_source_file(const std::string& path,
                               std::vector<uint8_t>& bytes,
                               std::string& error);
