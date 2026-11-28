// A sentinel for the stall rows: it measures how long THIS PROCESS was not running, so that
// a row can bound the gaps between the backend's ProgressFn calls net of the host's own
// stalls (a process frozen by the OS, a VM paused, a loaded host that does not schedule it).
// Those stalls are not the backend's: they delay every thread alike. A gap the backend
// causes by itself (a long wait in its loop) is not excused, because the sentinel was not
// delayed by it.
//
// One thread wakes every 10 ms. A wake later than kLateS after its intended time records
// the span [intended, woke], in seconds since the `t0` the row gave it. The sentinel
// waits with the same class of primitive the backend uses: a timed sleep on POSIX, a
// timed wait on a never-signalled condition variable under Windows (a futex under wine),
// so a stalled wineserver delays it no more than it delays the backend.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hs {

constexpr double kLateS = 0.020;    // a wake later than this is a host stall
constexpr int    kPeriodMs = 10;

struct Span { double from, to; };   // seconds since t0

/// Total length of `spans` inside [a, b]. Pure interval arithmetic.
inline double stall_overlap(const std::vector<Span>& spans, double a, double b) {
    double sum = 0;
    for (const Span& s : spans) {
        const double lo = std::max(a, s.from), hi = std::min(b, s.to);
        if (hi > lo) sum += hi - lo;
    }
    return sum;
}

/// Record the wake as a span when it was more than kLateS late. Returns whether it was recorded.
inline bool note_wake(std::vector<Span>& spans, double intended, double woke) {
    if (woke - intended <= kLateS) return false;
    spans.push_back({intended, woke});
    return true;
}

class HostStalls {
public:
    using Clock = std::chrono::steady_clock;
    explicit HostStalls(Clock::time_point t0) : t0_(t0) { thread_ = std::thread([this] { run(); }); }
    ~HostStalls() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
    }
    HostStalls(const HostStalls&)            = delete;
    HostStalls& operator=(const HostStalls&) = delete;

    std::vector<Span> spans() const { std::lock_guard<std::mutex> g(m_); return spans_; }
    /// The length of [a, b] minus the host's stalls inside it.
    double net_gap(double a, double b) const {
        std::lock_guard<std::mutex> g(m_);
        return (b - a) - stall_overlap(spans_, a, b);
    }

private:
    double since() const { return std::chrono::duration<double>(Clock::now() - t0_).count(); }

    void run() {
#ifdef _WIN32
        SRWLOCK lock = SRWLOCK_INIT;
        CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;
#endif
        double intended = since() + kPeriodMs / 1000.0;
        while (!stop_) {
#ifdef _WIN32
            AcquireSRWLockExclusive(&lock);
            SleepConditionVariableSRW(&cv, &lock, kPeriodMs, 0);
            ReleaseSRWLockExclusive(&lock);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(kPeriodMs));
#endif
            const double woke = since();
            {
                std::lock_guard<std::mutex> g(m_);
                note_wake(spans_, intended, woke);
            }
            intended = woke + kPeriodMs / 1000.0;
        }
    }

    Clock::time_point    t0_;
    std::atomic<bool>    stop_{false};
    mutable std::mutex   m_;
    std::vector<Span>    spans_;
    std::thread          thread_;
};

}  // namespace hs
