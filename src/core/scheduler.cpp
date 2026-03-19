#include "core/scheduler.h"

void Scheduler::schedule(uint64_t cycle, EventType type, std::function<void()> cb)
{
    queue_.push(Event{cycle, type, std::move(cb)});
}

void Scheduler::run_until(uint64_t target_cycle)
{
    while (!queue_.empty() && queue_.top().cycle <= target_cycle) {
        // Copy the event out before popping so the callback can safely
        // schedule new events into the queue without invalidating iterators.
        Event ev = queue_.top();
        queue_.pop();
        if (ev.callback) {
            ev.callback();
        }
    }
}

void Scheduler::reset()
{
    // std::priority_queue has no clear(); replace with an empty heap.
    queue_ = {};
}
