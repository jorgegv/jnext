// Optional thread wrapper for the emulated ESP-01. The design, the try-lock
// reasoning and the lifetime contract are all in esp_threaded.h — read that
// first; this file only implements it.

#include "esp01/esp_threaded.h"

#include "esp01/esp_log.h"

#include <utility>

namespace esp {

constexpr std::chrono::milliseconds ThreadedEsp::DEFAULT_POLL_INTERVAL;

ThreadedEsp::ThreadedEsp(EspTransport& transport, EspListener* listener,
                         std::chrono::milliseconds poll_interval)
    : core_(transport, listener), poll_interval_(poll_interval) {
    // THE TRAMPOLINE, installed once and never replaced. `set_output` then
    // swaps `user_sink_` behind `sink_mutex_` alone and never touches
    // `core_mutex_`, so installing a sink cannot be delayed by whatever the
    // worker is doing. See `set_output` in the header.
    core_.set_output([this](std::uint8_t byte) {
        std::lock_guard<std::mutex> lock(sink_mutex_);
        if (user_sink_) user_sink_(byte);
    });
}

ThreadedEsp::~ThreadedEsp() {
    stop();
    // The trampoline captured `this` and lives inside `core_`, which is
    // declared first and therefore destroyed LAST — after `user_sink_` and
    // `sink_mutex_`. Clearing it here means no ordering question survives the
    // join, rather than relying on nobody calling `tick()` mid-destruction.
    core_.set_output(nullptr);
}

void ThreadedEsp::start() {
    if (running_.load(std::memory_order_acquire)) return;
    stopping_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    worker_ = std::thread(&ThreadedEsp::run, this);
    log_debug("ESP worker thread started (poll interval {} ms)", poll_interval_.count());
}

void ThreadedEsp::stop() {
    if (!worker_.joinable()) {
        running_.store(false, std::memory_order_release);
        return;
    }
    stopping_.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(wake_mutex_);
        wake_.notify_all();
    }
    // JOIN, not detach. See the lifetime contract in the header: a surviving
    // worker after the owner is destroyed is silent corruption, not a crash.
    worker_.join();
    running_.store(false, std::memory_order_release);
    log_debug("ESP worker thread joined");
}

void ThreadedEsp::run() {
    while (!stopping_.load(std::memory_order_acquire)) {
        // THE SERVICE PASS IS THREE STEPS, AND THE MIDDLE ONE IS UNLOCKED.
        // That is the whole point of `AtEngine`'s poll split: the transport is
        // the one part of this system that can sit in a syscall, and holding
        // `core_mutex_` across it starves `tick()` on the caller's thread for
        // as long as the transport takes. Measured on the version that held
        // the lock throughout: 11 827 226 `tick()` calls over 400 ms of a
        // 500 ms transport stall delivered ZERO of the 39 bytes that were already
        // queued in `out_` before the stall began. The emulated CPU does not
        // stop while the ESP is busy, so those are real T-states of silence on
        // a wire whose receivers have no retry.
        //
        // AND THE WHOLE PASS IS GUARDED. Every step below reaches consumer
        // code: `advance_transports` calls `EspTransport::poll`,
        // `service_transports` reaches `send`/`recv`/`close`/`state`, and
        // `drain_inbound` reaches `core_.receive`, whose queue growth can throw
        // `bad_alloc` even with a well-behaved transport. An exception that
        // escapes a `std::thread` entry point does not propagate to anyone: it
        // calls `std::terminate` and kills the host program. A wrapper whose
        // contract is "own me as a member and I stay out of your way" cannot
        // answer a consumer's exception with a process abort. So a throwing
        // transport costs THIS pass and nothing else; the worker stays alive,
        // `stop()` still joins, and the host keeps running. The same policy,
        // and for the same reason, as the resolver thread's guard in
        // esp_socket.cpp.
        //
        // WHAT A LOST PASS ACTUALLY COSTS, so nobody has to rediscover it:
        // `drain_inbound` pops a byte from `inbound_` BEFORE handing it to
        // `core_.receive`, deliberately, so the inbound mutex is not held
        // across a call that can queue a whole response. A throw from
        // `receive` therefore loses that ONE already-popped byte — a dropped
        // character in the guest's TX stream, not a stalled queue. Bounded at
        // one byte per throwing pass, and strictly better than terminate.
        try {
            {
                std::lock_guard<std::mutex> lock(core_mutex_);
                drain_inbound();
            }
            core_.advance_transports();  // UNLOCKED — see above
            {
                std::lock_guard<std::mutex> lock(core_mutex_);
                core_.service_transports();
                wants_tick_.store(core_.wants_tick(), std::memory_order_release);
            }
        } catch (const std::exception& e) {
            note_pass_exception(e.what());
        } catch (...) {
            note_pass_exception("non-std exception");
        }
        passes_.fetch_add(1, std::memory_order_release);

        // Sleep on the condition variable rather than plain sleep, so guest
        // input is serviced immediately instead of up to an interval later.
        std::unique_lock<std::mutex> lock(wake_mutex_);
        wake_.wait_for(lock, poll_interval_, [this] {
            if (stopping_.load(std::memory_order_acquire)) return true;
            std::lock_guard<std::mutex> in(inbound_mutex_);
            return !inbound_.empty();
        });
    }
    // DELIBERATELY NO FINAL PASS. An earlier version did one, to catch bytes
    // the guest handed over just before the stop. The original reason to drop
    // it was that `poll()` could block for the shipped transport's synchronous
    // DNS lookup, making a trailing pass wait out a SECOND resolver timeout;
    // that transport now resolves off-thread, so the reason has narrowed but
    // not gone: shutdown is bounded by ONE `EspTransport::poll()` and a
    // trailing pass would double it for ANY transport, including a
    // third-party one that blocks. Shutdown is also exactly when nobody wants
    // those bytes. A host that does want the queue settled has `wait_idle`,
    // which drains inline once the worker is down.
}

void ThreadedEsp::note_pass_exception(const std::string& what) {
    // FIRST one at error, the rest at debug. A transport that throws once
    // throws every pass, and an error line every `poll_interval_` would bury
    // the log that has to carry the diagnosis — while dropping it entirely
    // would hide a real defect in the consumer's transport. The counter is the
    // honest middle, and `pass_exceptions()` exposes it so a host (or a test)
    // can see it happened at all rather than inferring it from log text.
    const std::uint64_t n = pass_exceptions_.fetch_add(1, std::memory_order_release);
    if (n == 0)
        log_error("ESP worker: the transport threw ({}) — this service pass is lost. "
                  "EspTransport methods must not throw; see esp_socket.h.", what);
    else
        log_debug("ESP worker: the transport threw again ({}), occurrence {}", what, n + 1);
}

void ThreadedEsp::drain_inbound() {
    for (;;) {
        std::uint8_t byte = 0;
        {
            std::lock_guard<std::mutex> lock(inbound_mutex_);
            if (inbound_.empty()) return;
            byte = inbound_.front();
            inbound_.pop_front();
        }
        // OUTSIDE the inbound lock: `receive` can queue a whole response, and
        // holding a lock the caller's thread wants across that would defeat
        // the point of having a separate one.
        core_.receive(byte);
    }
}

void ThreadedEsp::set_output(ByteSink sink) {
    // DELIBERATELY NOT `core_mutex_`. An earlier version took it, and inherited
    // exactly the hazard the poll split removes elsewhere: installing a sink
    // would wait for whatever the worker was doing. `sink_mutex_` guards one
    // assignment and is never held across anything, so this is bounded by a
    // pointer swap no matter what else is in flight.
    std::lock_guard<std::mutex> lock(sink_mutex_);
    user_sink_ = std::move(sink);
}

void ThreadedEsp::receive(std::uint8_t byte) {
    if (!running_.load(std::memory_order_acquire)) {
        // Not started: behave exactly like the passive core, so a host can
        // drive this class inline and get identical results.
        std::lock_guard<std::mutex> lock(core_mutex_);
        core_.receive(byte);
        wants_tick_.store(core_.wants_tick(), std::memory_order_release);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(inbound_mutex_);
        inbound_.push_back(byte);
    }
    std::lock_guard<std::mutex> lock(wake_mutex_);
    wake_.notify_one();
}

void ThreadedEsp::poll() {
    if (running_.load(std::memory_order_acquire)) return;  // the worker owns it
    std::lock_guard<std::mutex> lock(core_mutex_);
    drain_inbound();
    core_.poll();
    wants_tick_.store(core_.wants_tick(), std::memory_order_release);
}

void ThreadedEsp::tick(std::uint32_t elapsed_ticks, std::uint32_t ticks_per_byte) {
    // TRY-lock, never lock. The worker no longer holds this across the
    // transport poll (see `run()`), so the expected hold is microseconds — but
    // a transport that violates its non-blocking contract in `send`/`recv`
    // would stall inside the LOCKED half, and inheriting that on the caller's
    // thread is exactly what this class exists to avoid. On contention the
    // elapsed ticks are DROPPED rather than banked — banking would release a
    // burst at unbounded speed once the lock came free, which is the RX FIFO
    // overrun the pacing exists to prevent.
    //
    // AND DELIBERATELY NO try/catch HERE, unlike `run()`. This runs on the
    // CALLER's thread, so an exception — from the core, or from the host's own
    // output sink, which `tick()` is the only thing that invokes — propagates
    // to a caller who can decide what to do about it. `run()` is a thread ENTRY
    // POINT: it has no caller to propagate to, which is what makes
    // `std::terminate` the only outcome there and the guard necessary. Adding
    // one here would be swallowing exceptions raised on somebody else's thread,
    // which is not this class's to swallow.
    std::unique_lock<std::mutex> lock(core_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        log_trace("tick skipped: the ESP worker holds the core ({} ticks dropped)",
                  elapsed_ticks);
        return;
    }
    core_.tick(elapsed_ticks, ticks_per_byte);
    wants_tick_.store(core_.wants_tick(), std::memory_order_release);
}

void ThreadedEsp::set_associated(bool v) {
    std::lock_guard<std::mutex> lock(core_mutex_);
    core_.set_associated(v);
}

bool ThreadedEsp::associated() const {
    std::lock_guard<std::mutex> lock(core_mutex_);
    return core_.associated();
}

void ThreadedEsp::set_station_ip(std::string ip) {
    std::lock_guard<std::mutex> lock(core_mutex_);
    core_.set_station_ip(std::move(ip));
}

std::string ThreadedEsp::station_ip() const {
    std::lock_guard<std::mutex> lock(core_mutex_);
    return core_.station_ip();
}

bool ThreadedEsp::wait_idle(int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    if (!running_.load(std::memory_order_acquire)) {
        poll();  // stopped: settle it inline, which is what "idle" means here
        return true;
    }
    // "Idle" is TWO conditions, and one alone is not enough. An empty queue on
    // its own proves nothing: the worker may have popped the last byte a
    // moment ago and not yet handed it to the core. So: observe the queue
    // empty, mark the pass counter at that instant, and wait for it to
    // ADVANCE — the pass that finishes after the queue emptied is necessarily
    // the one that processed the last byte and then polled.
    bool          empty_seen = false;
    std::uint64_t mark       = 0;
    for (;;) {
        bool empty = false;
        {
            std::lock_guard<std::mutex> lock(inbound_mutex_);
            empty = inbound_.empty();
        }
        if (!empty) {
            empty_seen = false;  // more arrived; start over
        } else if (!empty_seen) {
            empty_seen = true;
            mark       = passes_.load(std::memory_order_acquire);
        } else if (passes_.load(std::memory_order_acquire) > mark) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace esp
