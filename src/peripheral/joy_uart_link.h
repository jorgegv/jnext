#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

class JoyUartEndpoint;

/// A LIVE, BIDIRECTIONAL host endpoint on the joystick-connector UART
/// (GH #252) — the far end of a real joy-port serial cable, as a DeZog /
/// dezogif_ng rig has it.
///
/// WHAT IT IS NOT. `JoyUartSource` (GH #251, joy_uart_source.h) is a
/// PRE-RECORDED, one-way injector: the whole stream is read from disk at
/// startup and clocked out on a schedule. That is the right shape for a bench
/// whose input is fixed and whose pass/fail is decided by the emulator, and it
/// stays exactly as it was — but it cannot hold a conversation, which is what a
/// debugger protocol is. This class is the conversation, and the two are
/// mutually exclusive at the command line: one cable, one socket.
///
/// WHY IT IS NOT A `UartDevice` — the design constraint of the issue.
/// A `UartDevice` (uart_device.h) is soldered to ONE channel's wire: the ESP-01
/// is UART 0's module, the Pi header is UART 1's. This cable has no channel of
/// its own. Which channel it lands on is chosen at run time by NR 0x0B bit 0
/// (zxnext.vhd:3340-3341, :3527-3529), and whether the machine looks at the RX
/// pin at all depends on NR 0x0B bit 4 matching the socket the cable is in
/// (zxnext.vhd:3538). Attaching it as a channel device would therefore have to
/// re-attach itself on every NR 0x0B write, and — worse — it would be
/// isolated by the very mux it is the far end of: `UartChannel::device_` is
/// muted precisely WHEN the joystick connector owns the channel
/// (`device_isolated()`, uart.h). So the seam is at the MUX:
///
///   * guest ← host: `tick()` paces bytes out and hands them to a sink the
///     Emulator installs, which applies the enable (bits 7+5), the connector
///     (bit 4) and the channel selection (bit 0) — the same sink shape
///     `JoyUartSource` uses;
///   * guest → host: `UartChannel::deliver_tx_byte` hands the byte to the
///     mux-level TX sink (`Uart::set_joy_uart_tx_sink`) on the branch that
///     previously dropped it, i.e. exactly when `device_isolated()` is true.
///
/// THE TWO DIRECTIONS ARE NOT GATED THE SAME WAY, and that asymmetry is the
/// VHDL's, not a simplification:
///
///   * RX is connector-selected. `joy_uart_rx <= ((not nr_0b_joy_iomode(0)) and
///     not i_JOY_LEFT(5)) or (nr_0b_joy_iomode(0) and not i_JOY_RIGHT(5))`
///     (zxnext.vhd:3538) — NR 0x0B bit 4 picks ONE socket's pin 6 to listen on,
///     and a cable in the other socket is a pin the FPGA is not reading.
///   * TX is NOT. The transmit bit is `joy_iomode_pin7` (zxnext.vhd:3526-3531),
///     which leaves the core as the single output `o_JOY_IO_MODE_PIN_7`
///     (zxnext.vhd:1593) with no connector selection anywhere in it. The board
///     drives it through `md6_joystick_connector_x2`, where in io mode
///     `o_joy_7 <= i_io_mode_pin_7` and `o_joy_select <= state(1)`
///     (md6_joystick_connector_x2.vhd:116-117) with `state` free-running on
///     CLK_28 (`state <= "1111100" & state_next(1 downto 0)`, :109) — so
///     `joysel` alternates at 7 MHz and pin 7 is presented to BOTH connectors,
///     each for half of every 4 CLK_28 ticks. One bit at the default 115200
///     lasts 243 of those ticks, so both sockets see the same transmitted bit.
///     A cable in the socket bit 4 does NOT select can therefore still HEAR the
///     Next; it just cannot be heard by it. Gating TX on the connector would be
///     a fiction, and a user-visible one: it would silently half-break a rig
///     whose guest selected the other socket.
///
/// Both directions are still gated on `joy_iomode_uart_en` (bit 7 AND bit 5,
/// zxnext.vhd:3536) and on bit 0's channel choice — for TX that gate is already
/// `UartChannel::device_isolated()`, which is exactly those two conditions, so
/// the TX sink applies no further test of its own.
///
/// NOTHING HERE IS SERIALISED, and the cable is held INERT during replay — the
/// `EspUartAdapter::set_inert` posture, for the same reason and one more. A
/// live descriptor cannot be snapshotted; bytes already written to the peer
/// cannot be unsent; and a replayed frame that re-read the descriptor would
/// CONSUME host bytes that the post-replay timeline still needs. Inert means
/// neither read nor written, so the host's bytes simply wait in the pipe and
/// arrive after the replay finishes. (That is strictly better than the ESP's
/// behaviour, which loses whatever arrives while it is inert — the ESP's
/// transport is a socket owned by a worker thread, this one is a descriptor we
/// choose not to read.)
class JoyUartLink {
public:
    /// Guest-bound sink, installed by the owner. Called once per byte that has
    /// finished arriving on the wire; the sink applies the NR 0x0B gates and
    /// reports the outcome back through `note_delivered` / `note_dropped`.
    using ByteSink = std::function<void(uint8_t byte)>;

    /// Host bytes not yet clocked into the guest. A hard cap rather than a
    /// growing buffer, and it produces BACKPRESSURE rather than loss: once the
    /// queue is full the endpoint is simply not read, so the bytes stay in the
    /// pipe and the host's own `write()` blocks or returns EAGAIN. That is what
    /// a slow serial link does. Sized well above one frame's worth at any baud
    /// a real rig uses (2 Mbaud is ~4 KB/frame).
    static constexpr std::size_t RX_QUEUE_MAX = 64 * 1024;

    /// Guest bytes not yet accepted by the host. Bounded for the opposite
    /// reason: there is no backpressure available toward the guest — the Z80
    /// has already transmitted — so a peer that has stopped reading must
    /// eventually cost bytes rather than memory. Drop-newest, counted in
    /// `unsent()`.
    static constexpr std::size_t TX_QUEUE_MAX = 64 * 1024;

    /// @param endpoint  the host-side descriptors. Never null.
    /// @param connector which joystick socket the cable is in —
    ///                  0 = joy 1 (`i_JOY_LEFT`), 1 = joy 2 (`i_JOY_RIGHT`).
    JoyUartLink(std::unique_ptr<JoyUartEndpoint> endpoint, int connector);
    ~JoyUartLink();

    JoyUartLink(const JoyUartLink&)            = delete;
    JoyUartLink& operator=(const JoyUartLink&) = delete;

    void set_byte_sink(ByteSink sink) { sink_ = std::move(sink); }

    int connector() const { return connector_; }

    /// What the user should be told this cable is — "FIFO pair <base>.rx /
    /// <base>.tx", or "pty <slave path>". The pty form is the one a DeZog
    /// launch configuration is pointed at, so it has to be printable.
    const std::string& describe() const;

    /// ONCE PER FRAME, from the host loop's frame seam — never from the
    /// per-instruction tick. This is where the descriptors are touched: a
    /// `read()`/`write()` per Z80 instruction would be a syscall per
    /// instruction, which is the exact mistake `UartDevice::poll`'s header
    /// comment exists to prevent.
    void poll();

    /// Advance emulated time and emit whatever has finished arriving.
    ///
    /// @param master_cycles  28 MHz ticks elapsed since the last call.
    /// @param byte_ticks     28 MHz ticks for ONE complete frame at the
    ///                       RECEIVING channel's current framing and prescaler
    ///                       (`UartChannel::byte_transfer_ticks()`), passed
    ///                       fresh on every call so a mid-stream baud change
    ///                       re-paces delivery at once.
    ///
    /// PACED, not dumped, and the reason is the FIFO arithmetic rather than
    /// taste. `uart_rx.vhd` clocks in one frame per `prescaler * frame_bits`
    /// CLK_28 ticks, and the Next-side RX FIFO is 512 bytes with drop-newest
    /// overflow (uart.h). A host that hands us a 4 KB DeZog message would, if
    /// delivered as fast as the host supplies it, silently lose 7/8 of it
    /// before the guest's first read. "As fast as the guest reads" is not an
    /// option either: nothing on this side knows when the Z80 will next touch
    /// port 0x143B. Pacing off the guest's OWN programmed baud is the only rate
    /// that cannot overrun a correctly configured rig, and it is the rate both
    /// `JoyUartSource::tick` and `UartDevice::tick` already use.
    void tick(uint32_t master_cycles, uint32_t byte_ticks);

    /// Guest → host. Called from the mux TX seam at BYTE boundaries (the
    /// byte-level TX engine already held the byte for `byte_transfer_ticks()`),
    /// so there is nothing left to pace here: the guest's own baud did it.
    void send_to_host(uint8_t byte);

    /// Delivery outcome of a paced RX byte, reported by the sink because only
    /// the sink knows the mux state.
    void note_delivered() { ++delivered_; }
    void note_dropped()   { ++dropped_; }

    std::size_t delivered() const { return delivered_; }   ///< reached the guest
    std::size_t dropped()   const { return dropped_; }     ///< lost to the mux
    std::size_t received()  const { return received_; }    ///< read from the host
    std::size_t sent()      const { return sent_; }        ///< written to the host
    std::size_t unsent()    const { return unsent_; }      ///< dropped, peer gone or full

    /// Unexpected `errno`s seen on the descriptors, and the last one's text.
    /// A peer coming and going is NOT a fault (that is the normal life of this
    /// cable and is handled silently); anything else is, and the owner turns a
    /// non-zero count into a one-shot warning. A cable that has stopped working
    /// for a reason nobody reports is the silent no-op this feature exists to
    /// avoid, exactly as an all-dropped `--joy-uart-rx` stream is.
    std::size_t        faults() const;
    const std::string& last_error() const;

    /// REPLAY GATE, mirroring `EspUartAdapter::set_inert`. While inert nothing
    /// is read from or written to the descriptors and no RX is paced out; bytes
    /// the guest transmits during a replayed frame are dropped (it already sent
    /// them once). See the class comment for why this is a gate rather than a
    /// teardown.
    void set_inert(bool inert) { inert_ = inert; }
    bool inert() const { return inert_; }

private:
    std::unique_ptr<JoyUartEndpoint> endpoint_;
    ByteSink                         sink_;
    std::deque<uint8_t>              rx_queue_;   ///< host → guest, awaiting pacing
    std::deque<uint8_t>              tx_queue_;   ///< guest → host, awaiting the fd
    std::size_t                      delivered_ = 0;
    std::size_t                      dropped_   = 0;
    std::size_t                      received_  = 0;
    std::size_t                      sent_      = 0;
    std::size_t                      unsent_    = 0;
    int                              connector_ = 0;
    bool                             inert_     = false;
    /// Ticks still to run before the byte at the head of `rx_queue_` has
    /// finished arriving. 0 means "not started"; armed from the `byte_ticks`
    /// handed in, so the first byte of a burst takes a full frame time rather
    /// than appearing instantly.
    uint32_t                         timer_     = 0;

    /// Push as much of `tx_queue_` as the endpoint will take right now, and
    /// handle a peer that went away while we were doing it.
    ///
    /// WHEN THE FAR END DISAPPEARS MID-SESSION both queues are DISCARDED, and
    /// that is a deliberate choice rather than a consequence. What is in them
    /// is half of a conversation with a process that has exited: handing the
    /// remains of it to whatever connects next would give that peer a
    /// truncated message it cannot parse and cannot recognise as stale, which
    /// is a worse failure than the loss. The discarded transmit bytes are
    /// counted in `unsent()` so the loss is visible rather than silent.
    ///
    /// WHEN IT REAPPEARS nothing special happens: the FIFO is re-opened on the
    /// next flush and traffic resumes from that point, which is exactly what
    /// unplugging a serial cable and plugging it back in does. The emulated
    /// machine is not told, because the hardware is not either — the Next has
    /// no carrier-detect on the joystick connector.
    void flush_tx();
};

/// The host-side descriptors behind a `JoyUartLink`: a FIFO pair, a pty, or (in
/// tests) a pair of descriptors handed in directly.
///
/// NON-BLOCKING IS THE WHOLE CONTRACT. The emulator's frame loop calls into
/// this from `JoyUartLink::poll()`, so every operation here must return
/// immediately whatever the far end is doing — including when there is no far
/// end at all. Concretely, for the FIFO form:
///
///   * HOST → NEXT (`<base>.rx`) is opened `O_RDONLY | O_NONBLOCK`, which
///     succeeds at once even with no writer. `read()` then returns 0 while no
///     writer is attached and `EAGAIN` while one is attached but silent; both
///     mean "nothing right now", neither is an error, and neither ends the
///     session. jnext keeps the descriptor for the life of the run, which keeps
///     the pipe object alive, so a writer that comes, goes and comes back again
///     reattaches to the same pipe and its bytes flow again. That is the
///     "far end disappears and reappears" answer for this direction: nothing
///     happens, and it resumes.
///
///   * NEXT → HOST (`<base>.tx`) CANNOT be opened eagerly: `O_WRONLY |
///     O_NONBLOCK` on a FIFO with no reader fails with `ENXIO` — that is the
///     exact failure the issue reports for the one-way path. So it is opened
///     LAZILY, retried on every flush until a reader appears, and bytes
///     transmitted before then are dropped and counted (`JoyUartLink::unsent`),
///     which is what the wire does when nothing is plugged in. When the reader
///     goes away mid-session `write()` fails with `EPIPE`; the descriptor is
///     closed and the lazy open resumes, so a reader that restarts is picked up
///     within one frame. `SIGPIPE` is ignored process-wide when the first
///     endpoint is created — without that the default disposition would kill
///     jnext outright the moment a DeZog session closed.
///
/// The pty form has one descriptor used both ways. It survives its peer
/// perfectly: with no slave open, `read()` gives `EIO` on Linux (treated as
/// "nothing right now") and `write()` simply buffers, and when a process opens
/// the slave again the same descriptor carries the traffic.
///
/// POSIX ONLY. Both factories fail with an explanatory message on Windows,
/// which has neither FIFOs nor ptys in this form; the CLI refuses there rather
/// than pretending. The flags stay in the option table on every platform so
/// that `--help`, the man page and `cli_options_test` do not have to be
/// conditional.
class JoyUartEndpoint {
public:
    ~JoyUartEndpoint();

    JoyUartEndpoint(const JoyUartEndpoint&)            = delete;
    JoyUartEndpoint& operator=(const JoyUartEndpoint&) = delete;

    /// A FIFO PAIR named from `base`: `<base>.rx` carries host → Next and
    /// `<base>.tx` carries Next → host. Both names are from the GUEST's point
    /// of view, matching `--joy-uart-rx`. Either is created with `mkfifo` if it
    /// does not exist; an existing path that is not a FIFO is refused rather
    /// than opened, because opening a regular file there would give a run that
    /// looks alive and talks to nobody.
    static std::unique_ptr<JoyUartEndpoint> open_fifo(const std::string& base,
                                                      std::string& error);

    /// A PSEUDO-TERMINAL. jnext holds the master; the slave device path is what
    /// a serial client (DeZog's `serial` remote, `picocom`, ...) opens, and it
    /// is returned in `describe()` so the caller can print it. Raw mode is set
    /// on the master so no line discipline mangles the byte stream.
    static std::unique_ptr<JoyUartEndpoint> open_pty(std::string& error);

    /// TEST SEAM: adopt two already-open, already-non-blocking descriptors
    /// (e.g. the two ends of a `socketpair`). Takes ownership of both; pass the
    /// same value twice for a single bidirectional descriptor. No lazy reopen,
    /// because there is no path to reopen from.
    static std::unique_ptr<JoyUartEndpoint> adopt_fds(int rx_fd, int tx_fd,
                                                      std::string description);

    /// Non-blocking read of at most `cap` bytes. Returns how many landed in
    /// `buf`; 0 means "nothing right now", which covers no-writer, no-data and
    /// a pty with no slave alike.
    std::size_t read(uint8_t* buf, std::size_t cap);

    /// Non-blocking write of at most `len` bytes. Returns how many the far end
    /// took; 0 means it took none (not attached, or its buffer is full), which
    /// is never an error and never blocks.
    std::size_t write(const uint8_t* buf, std::size_t len);

    const std::string& describe() const { return description_; }

    /// See `JoyUartLink::faults` — errors that are NOT part of a peer's normal
    /// come-and-go (EOF, EAGAIN, EPIPE, a pty's EIO).
    std::size_t        faults() const { return faults_; }
    const std::string& last_error() const { return last_error_; }

    /// True ONCE for each time a peer that had been attached went away — i.e.
    /// a `write()` that failed with `EPIPE`. Cleared by the call.
    ///
    /// Only the FIFO form can report this, and that is a property of the
    /// kernel rather than a simplification: closing the read end of a FIFO
    /// destroys the write end's target and the next write says so, whereas a
    /// pty master survives its slave closing and is re-shared by whatever
    /// opens the slave next — there is no event to report and nothing that
    /// needs discarding, because anything already written is sitting in the
    /// pty's own buffer where jnext cannot reach it either way.
    bool take_peer_lost() {
        const bool lost = peer_lost_;
        peer_lost_ = false;
        return lost;
    }

private:
    JoyUartEndpoint() = default;

    /// Try to open the lazy TX FIFO. No-op when already open or when there is
    /// no path to open (pty / adopted descriptors).
    void open_tx_if_needed();

    int         rx_fd_ = -1;
    int         tx_fd_ = -1;
    /// True when `tx_fd_` is the same descriptor as `rx_fd_` (pty), so the
    /// destructor closes it once and an EPIPE never discards the read side.
    bool        shared_fd_ = false;
    /// Non-empty only for the FIFO form; the path `open_tx_if_needed` retries.
    std::string tx_path_;
    std::string description_;
    std::size_t faults_ = 0;
    std::string last_error_;
    bool        peer_lost_ = false;
};
