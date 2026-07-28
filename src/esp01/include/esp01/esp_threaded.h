#pragma once

#include "esp01/esp_at.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

/// OPTIONAL thread wrapper for the emulated ESP-01 (GH #25, branch 3.5 of 6).
///
/// ---------------------------------------------------------------------------
/// WHY IT EXISTS, AND WHY IT IS OPTIONAL
/// ---------------------------------------------------------------------------
/// The ESP is a FULLY EXTERNAL component, not part of the Next, and its
/// internal latency is not observable — no Next software depends on
/// `AT+CIPSTART` taking a particular time to parse. So the socket half can run
/// as fast as it likes, on whatever thread it likes, as long as the WIRE is
/// still paced at baud. Only the drain stage is synchronous with the guest;
/// the rest is not, and treating the whole component as synchronous because
/// one stage of it is was the reasoning error that this shape corrects.
///
/// It is OPTIONAL because a reusable component that FORCES a thread is more
/// opinionated than one that offers it. `AtEngine` is drivable inline exactly
/// as before — a consumer with its own scheduler, or one that simply does not
/// want a thread, uses the core directly and never links this class's
/// behaviour in. Both modes are exercised by the module's test suite, because
/// an unexercised alternative mode rots.
///
/// ---------------------------------------------------------------------------
/// WHAT RUNS WHERE — the whole design in four lines
/// ---------------------------------------------------------------------------
///   receive()        caller thread -> inbound queue -> WORKER feeds the core
///   poll()           WORKER, continuously: sockets, DNS, connect timeouts
///   tick()           CALLER thread, directly on the core (try-lock)
///   the ByteSink     CALLER thread, from inside tick(), never the worker
///
/// So the guest-bound byte path never leaves the caller's thread, which is
/// what makes it safe for jnext's sink to write straight into the emulated RX
/// FIFO while a socket read is in flight on the worker.
///
/// WHY `tick()` IS NOT MARSHALLED TO THE WORKER. It is called once per emulated
/// instruction and must release bytes at an exact cadence; a queue hop per call
/// would be both far too hot and far too jittery. It therefore takes the core
/// lock — with `try_lock`, see below.
///
/// ---------------------------------------------------------------------------
/// THE TRY-LOCK, which is the one subtle thing here
/// ---------------------------------------------------------------------------
/// `poll()` on the worker holds the core lock, and the transport's `getaddrinfo`
/// is SYNCHRONOUS (esp_socket.h says so at length) — seconds, on an unreachable
/// resolver. A blocking `tick()` would inherit that stall, which is precisely
/// the thing moving the socket work off the caller's thread was supposed to
/// prevent. So `tick()` uses `try_lock` and, when it loses, RETURNS HAVING DONE
/// NOTHING — the elapsed ticks are dropped, not banked.
///
/// Dropping rather than banking is deliberate: banking would accumulate credit
/// during the stall and then release a burst at unbounded speed the moment the
/// lock came free, which is exactly the RX FIFO overrun the pacing exists to
/// prevent (`AtEngine::tick`'s idle branch makes the same choice for the same
/// reason). What is lost is a few bytes' worth of delivery LATENCY while the
/// ESP is busy — during which, by construction, the ESP has nothing to say.
///
/// ---------------------------------------------------------------------------
/// LIFETIME CONTRACT — read this before constructing one
/// ---------------------------------------------------------------------------
///   1. The destructor JOINS. Not detaches, not "signals and hopes".
///   2. The transport must outlive the wrapper (the core holds a reference).
///   3. THE OWNER MUST DESTROY THE WRAPPER BEFORE ANYTHING THE SINK POINTS AT.
///      For jnext that means before `~Emulator()`, and the reason is specific
///      and nasty: `emulator_cold_boot` (platform/emulator_boot.h:67-77) does
///      `emu.~Emulator(); new (&emu) Emulator();` — placement-new at the SAME
///      address. A surviving worker thread would then be operating on a core
///      whose sink still holds a perfectly valid pointer into the NEWLY BOOTED
///      machine. Nothing crashes; the fresh machine's UART just starts
///      receiving the dead machine's bytes. Silent corruption, not a fault, so
///      no sanitiser will find it for you. Joining first removes the hazard
///      entirely, which is why the join is in the destructor rather than in a
///      shutdown method someone can forget to call.
///      (`uart_device.h:22-35` documents the same hazard for the RX sink.)
///   4. Nothing in THIS branch attaches one to a live `Emulator`; branch 4
///      owns that wiring and inherits this contract.
namespace esp {

class ThreadedEsp final : public EspDevice {
public:
    /// How long the worker sleeps between service passes when there is nothing
    /// to do. 1 ms is chosen against the ONE latency that matters — a byte
    /// arriving from the network waits at most this long before the core sees
    /// it — and against the cost of waking a thread 1000 times a second doing
    /// a handful of non-blocking `recv`s, which is noise next to a frame of
    /// emulation. Guest input does not wait for it at all: `receive` signals
    /// the worker.
    static constexpr std::chrono::milliseconds DEFAULT_POLL_INTERVAL{1};

    /// Constructs the core; does NOT start the thread. Call `start()`.
    /// Leaving construction and starting separate means an owner can build the
    /// object, install the sink, and only then let anything run.
    explicit ThreadedEsp(EspTransport& transport,
                         std::chrono::milliseconds poll_interval = DEFAULT_POLL_INTERVAL);
    ~ThreadedEsp() override;

    ThreadedEsp(const ThreadedEsp&)            = delete;
    ThreadedEsp& operator=(const ThreadedEsp&) = delete;

    /// Idempotent. After this, socket work happens on the worker.
    void start();
    /// Idempotent, and joins. Called by the destructor; exposed so an owner can
    /// order the shutdown explicitly.
    void stop();
    bool running() const { return running_.load(std::memory_order_acquire); }

    // ── EspDevice ─────────────────────────────────────────────────────────

    /// Installs the sink on the core. Call it BEFORE `start()`, or accept that
    /// it briefly contends with the worker for the core lock (it blocks; it is
    /// not on the hot path).
    void set_output(ByteSink sink) override;

    /// Never blocks on socket work: the byte goes into the inbound queue and
    /// the worker feeds it to the core. When the wrapper is not started, it is
    /// fed straight through, so the class behaves identically either way.
    void receive(std::uint8_t byte) override;

    /// A no-op while the worker is running — it is already polling, far more
    /// often than a host would. Forwards to the core when stopped, so a host
    /// that calls `poll()` per frame works whether or not the thread is up.
    void poll() override;

    /// Paces guest-bound bytes on the CALLER's thread. Skips (dropping the
    /// elapsed ticks) if the worker holds the core — see the try-lock note.
    void tick(std::uint32_t elapsed_ticks, std::uint32_t ticks_per_byte) override;

    /// Reads a snapshot taken by whichever thread last touched the core. It can
    /// go stale between the read and the caller acting on it — harmless in both
    /// directions: a stale `true` costs one wasted `tick()`, and a stale
    /// `false` costs at most one host service interval of delivery latency.
    bool wants_tick() const override { return wants_tick_.load(std::memory_order_acquire); }

    // ── Test support ──────────────────────────────────────────────────────

    /// Block until the worker has drained the inbound queue and completed a
    /// service pass, or `timeout` elapses. Returns true if it settled.
    ///
    /// EXISTS FOR THE SUITE, and says so rather than pretending otherwise: a
    /// threaded component whose test sleeps for a guessed interval is a
    /// flaky test, and the alternative — exposing the mutex — would be worse.
    /// Production code has no reason to call it.
    bool wait_idle(int timeout_ms);

private:
    void run();
    /// Feed the inbound queue into the core. Caller must hold `core_mutex_`.
    void drain_inbound();

    AtEngine                  core_;
    std::chrono::milliseconds poll_interval_;

    /// Guards `core_`. Held by the worker across a whole service pass —
    /// including the transport's synchronous DNS lookup, which is exactly why
    /// `tick()` only ever try-locks it.
    mutable std::mutex core_mutex_;

    /// Guest -> ESP. Its own mutex, never held across anything but a deque
    /// push/pop, so the caller's `receive()` cannot be delayed by socket work.
    /// A plain deque rather than a lock-free ring: the hold is O(1) and spans
    /// no syscall, and a hand-rolled lock-free queue would be more code to get
    /// subtly wrong for no measurable gain at 230 bytes/frame.
    std::mutex               inbound_mutex_;
    std::deque<std::uint8_t> inbound_;

    std::thread             worker_;
    std::atomic<bool>       running_{false};
    std::atomic<bool>       stopping_{false};
    std::atomic<bool>       wants_tick_{false};
    /// Bumped after every completed service pass, so `wait_idle` can observe
    /// progress rather than guess at it.
    std::atomic<std::uint64_t> passes_{0};

    /// Wakes the worker when guest input arrives, so a command is not held for
    /// up to `poll_interval_` before the core even sees it.
    std::mutex              wake_mutex_;
    std::condition_variable wake_;
};

}  // namespace esp
