#pragma once

#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

// ---------------------------------------------------------------------------
// Event types
// ---------------------------------------------------------------------------

enum class EventType {
    SCANLINE,     ///< End-of-scanline processing (render + audio accumulate)
    VSYNC,        ///< Vertical sync / start of new frame
    CPU_INT,      ///< CPU interrupt assertion (IM2 frame IRQ)
    CTC_TICK,     ///< CTC channel underflow / trigger
    DMA_BURST,    ///< ZXN-DMA burst transfer
    AUDIO_SAMPLE, ///< Audio sample accumulation point
    INPUT_POLL,   ///< Keyboard / joystick state poll
};

// ---------------------------------------------------------------------------
// Event
// ---------------------------------------------------------------------------

struct Event {
    uint64_t              cycle;    ///< Master cycle at which the event fires
    EventType             type;     ///< Event category
    std::function<void()> callback; ///< Action to execute when event fires

    /// Comparison for min-heap ordering (earliest cycle first).
    bool operator>(const Event& other) const {
        return cycle > other.cycle;
    }
};

// ---------------------------------------------------------------------------
// Scheduler
// ---------------------------------------------------------------------------

/// Min-heap priority-queue event scheduler ordered by master cycle timestamp.
///
/// Usage:
///   scheduler.schedule(cycle + SCANLINE_PERIOD, EventType::SCANLINE, [&]{ ... });
///   scheduler.run_until(frame_end_cycle);
class Scheduler {
public:
    Scheduler() = default;

    /// Schedule a callback to fire at the given master cycle.
    void schedule(uint64_t cycle, EventType type, std::function<void()> cb);

    /// Drain and execute all events whose cycle <= target_cycle.
    /// Events are executed in cycle order; multiple events at the same
    /// cycle are processed in FIFO insertion order (stable).
    void run_until(uint64_t target_cycle);

    /// Remove all pending events.
    void reset();

    /// True if no events are pending.
    bool empty() const { return queue_.empty(); }

    /// Cycle of the next pending event (undefined if empty()).
    uint64_t next_cycle() const { return queue_.top().cycle; }

private:
    using MinHeap = std::priority_queue<Event, std::vector<Event>, std::greater<Event>>;
    MinHeap queue_;
};
