// Optional thread wrapper for the emulated ESP-01. The design, the try-lock
// reasoning and the lifetime contract are all in esp_threaded.h — read that
// first; this file only implements it.

#include "esp01/esp_threaded.h"

#include "esp01/esp_log.h"

#include <utility>

namespace esp {

constexpr std::chrono::milliseconds ThreadedEsp::DEFAULT_POLL_INTERVAL;

ThreadedEsp::ThreadedEsp(EspTransport& transport, std::chrono::milliseconds poll_interval)
    : core_(transport), poll_interval_(poll_interval) {}

ThreadedEsp::~ThreadedEsp() { stop(); }

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
        {
            std::lock_guard<std::mutex> lock(core_mutex_);
            drain_inbound();
            core_.poll();
            wants_tick_.store(core_.wants_tick(), std::memory_order_release);
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
    // One last pass so bytes the guest handed over just before the stop are
    // not silently discarded.
    std::lock_guard<std::mutex> lock(core_mutex_);
    drain_inbound();
    core_.poll();
    wants_tick_.store(core_.wants_tick(), std::memory_order_release);
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
    std::lock_guard<std::mutex> lock(core_mutex_);
    core_.set_output(std::move(sink));
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
    // TRY-lock, never lock: the worker holds this across the transport's
    // synchronous DNS lookup, and inheriting that stall on the caller's thread
    // is exactly what this class exists to avoid. On contention the elapsed
    // ticks are DROPPED rather than banked — banking would release a burst at
    // unbounded speed once the lock came free, which is the RX FIFO overrun
    // the pacing exists to prevent.
    std::unique_lock<std::mutex> lock(core_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        log_trace("tick skipped: the ESP worker holds the core ({} ticks dropped)",
                  elapsed_ticks);
        return;
    }
    core_.tick(elapsed_ticks, ticks_per_byte);
    wants_tick_.store(core_.wants_tick(), std::memory_order_release);
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
