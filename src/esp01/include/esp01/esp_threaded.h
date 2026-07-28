#pragma once

#include "esp01/esp_at.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
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
///   poll()           WORKER, continuously: sockets, connect timeouts
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
/// THE CORE LOCK IS NEVER HELD ACROSS A TRANSPORT POLL
/// ---------------------------------------------------------------------------
/// This is the single most important property of the worker loop, and it was
/// got WRONG first time round, so it is worth stating why.
///
/// `AtEngine::poll()` is deliberately available in two halves
/// (`advance_transports` / `service_transports`). The worker runs the first —
/// the half that calls `EspTransport::poll()` and touches no engine state —
/// with the core lock RELEASED, and only takes the lock for the engine work
/// either side of it. Holding the lock throughout, which is what the first
/// version did, hands any transport slowness straight to `tick()` on the
/// caller's thread.
///
/// That is not theoretical. Measured against a transport that blocks for
/// 500 ms inside `poll()`: **11 827 226** `tick()` calls over 400 ms delivered
/// **ZERO of the 39** bytes that were sitting in the engine's outbound queue
/// BEFORE the stall started. The first version of this header explained that
/// away as "a few bytes' worth of delivery latency while the ESP is busy —
/// during which, by construction, the ESP has nothing to say". That claim is
/// FALSE and the measurement is what disproves it: the bytes were already
/// queued, so the ESP had plenty to say. And because the emulation thread is
/// not blocked — the entire point of the wrapper — real T-states keep elapsing
/// throughout, which is silence on a wire whose receivers have no retry.
///
/// ---------------------------------------------------------------------------
/// THE TRY-LOCK, which is the one subtle thing left
/// ---------------------------------------------------------------------------
/// Even with the transport poll unlocked, `tick()` and the worker still
/// contend for engine state, and a transport that violates its non-blocking
/// contract in `send`/`recv` (esp_socket.h) would stall inside the LOCKED
/// half. So `tick()` uses `try_lock` and, when it loses, RETURNS HAVING DONE
/// NOTHING — the elapsed ticks are dropped, not banked.
///
/// Dropping rather than banking is deliberate: banking would accumulate credit
/// during a stall and then release a burst at unbounded speed the moment the
/// lock came free, which is exactly the RX FIFO overrun the pacing exists to
/// prevent (`AtEngine::tick`'s idle branch makes the same choice for the same
/// reason). What is lost now is the duration of one engine service pass —
/// microseconds, bounded by the non-blocking contract — rather than the
/// duration of a name lookup.
///
/// ---------------------------------------------------------------------------
/// LIFETIME CONTRACT — read this before constructing one
/// ---------------------------------------------------------------------------
///   1. The destructor JOINS. Not detaches, not "signals and hopes".
///      SO THE SHUTDOWN IS ONLY AS BOUNDED AS THE TRANSPORT. A thread parked
///      in a syscall cannot be joined in bounded time by anyone, so `stop()`
///      and the destructor are bounded by exactly ONE `EspTransport::poll()`
///      call. That is why `poll()` carries an explicit non-blocking CONTRACT
///      (esp_socket.h) rather than a preference: violate it and destroying
///      this object takes as long as your transport does. Measured against a
///      transport that blocks for 2000 ms: the destructor took 2007 ms, which
///      in jnext is the emulator frozen for 2 s when the user presses Reset.
///      The transport this module ships HONOURS the contract — its name
///      resolution runs on its own thread, so nothing it does here can
///      outlast a flag test — and the bound therefore holds in practice. A
///      third-party transport that blocks re-opens it, which is why the
///      contract is stated on `poll()` rather than assumed of it.
///      Detaching instead would bound the destructor and is NOT an option —
///      see hazard 3.
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

    /// Safe to call at ANY time, started or not, and bounded by a pointer
    /// swap.
    ///
    /// The core is given a permanent trampoline at construction, and this only
    /// swaps the user sink behind `sink_mutex_` — it never touches
    /// `core_mutex_`. An earlier version did take the core lock and described
    /// it as "briefly contends with the worker", which understated it by the
    /// same margin as the try-lock note above: contending with a worker that
    /// was inside a blocking transport call meant blocking for that call's
    /// whole duration.
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

    /// How many service passes were lost to an exception thrown by the
    /// transport. Should be 0 forever: `EspTransport`'s methods must not throw
    /// (esp_socket.h). It is exposed rather than left to the log because "the
    /// worker survived a throwing transport" is a property a test has to be
    /// able to ASSERT, not grep for — and because a host that wants to
    /// surface a broken transport can read it.
    std::uint64_t pass_exceptions() const {
        return pass_exceptions_.load(std::memory_order_acquire);
    }

private:
    void run();
    /// Log and count one exception that escaped a service pass.
    void note_pass_exception(const std::string& what);
    /// Feed the inbound queue into the core. Caller must hold `core_mutex_`.
    void drain_inbound();

    AtEngine                  core_;
    std::chrono::milliseconds poll_interval_;

    /// Guards `core_`'s ENGINE state. Explicitly NOT held across
    /// `AtEngine::advance_transports()` — see the worker loop — so its hold
    /// time is bounded by the non-blocking transport contract rather than by
    /// however long a socket operation takes. `tick()` still only try-locks
    /// it, because a contract-violating transport can still stall the locked
    /// half through `send`/`recv`.
    mutable std::mutex core_mutex_;

    /// Guards `user_sink_` and NOTHING else. Separate from `core_mutex_` so
    /// that installing a sink is never delayed by the worker, and held only
    /// for a swap or a single sink call — never across a transport operation.
    ///
    /// The trampoline installed on the core reads `user_sink_` under this;
    /// it runs from `tick()` on the caller's thread, so the contention here is
    /// between a caller installing a sink and the same caller pacing bytes.
    mutable std::mutex sink_mutex_;
    ByteSink           user_sink_;

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
    /// Service passes lost to a throwing transport. See `pass_exceptions()`.
    std::atomic<std::uint64_t> pass_exceptions_{0};

    /// Wakes the worker when guest input arrives, so a command is not held for
    /// up to `poll_interval_` before the core even sees it.
    std::mutex              wake_mutex_;
    std::condition_variable wake_;
};

}  // namespace esp
