#include "debug/breakpoints.h"

#include <algorithm>

BreakpointSet::ObserverId
BreakpointSet::add_observer(std::function<void(BreakpointChange)> fn) {
    const ObserverId id = next_observer_id_++;
    observers_.push_back({id, std::move(fn)});
    return id;
}

void BreakpointSet::remove_observer(ObserverId id) {
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
            [id](const Observer& o) { return o.id == id; }),
        observers_.end());
}

void BreakpointSet::notify(BreakpointChange what) {
    for (const auto& o : observers_) o.fn(what);
}

void BreakpointSet::add_pc(uint16_t addr) {
    pc_bps_.insert(addr);
    notify(BreakpointChange::PcBreakpoints);
}

void BreakpointSet::remove_pc(uint16_t addr) {
    pc_bps_.erase(addr);
    notify(BreakpointChange::PcBreakpoints);
}

bool BreakpointSet::has_pc(uint16_t addr) const {
    return pc_bps_.count(addr) > 0;
}

void BreakpointSet::clear_all_pc() {
    pc_bps_.clear();
    notify(BreakpointChange::PcBreakpoints);
}

void BreakpointSet::add_watchpoint(uint16_t addr, WatchType type) {
    // Avoid duplicates.
    for (const auto& wp : watchpoints_) {
        if (wp.addr == addr && wp.type == type) return;
    }
    watchpoints_.push_back({addr, type});
    notify(BreakpointChange::Watchpoints);
}

void BreakpointSet::remove_watchpoint(uint16_t addr, WatchType type) {
    watchpoints_.erase(
        std::remove_if(watchpoints_.begin(), watchpoints_.end(),
            [addr, type](const Watchpoint& wp) {
                return wp.addr == addr && wp.type == type;
            }),
        watchpoints_.end());
    notify(BreakpointChange::Watchpoints);
}

bool BreakpointSet::has_watchpoint(uint16_t addr, WatchType type) const {
    for (const auto& wp : watchpoints_) {
        if (wp.addr == addr) {
            if (wp.type == type) return true;
            // READ_WRITE matches both READ and WRITE.
            if (wp.type == WatchType::READ_WRITE &&
                (type == WatchType::READ || type == WatchType::WRITE))
                return true;
        }
    }
    return false;
}

// GH #222. See the header for the decode rule and why it is not has_watchpoint().
bool BreakpointSet::has_io_watchpoint(uint16_t port, WatchType type) const {
    for (const auto& wp : watchpoints_) {
        if (wp.type != type) continue;
        const bool hit = (wp.addr <= 0x00FF) ? ((port & 0x00FF) == wp.addr)
                                             : (port == wp.addr);
        if (hit) return true;
    }
    return false;
}

void BreakpointSet::clear_all_watchpoints() {
    watchpoints_.clear();
    notify(BreakpointChange::Watchpoints);
}

// The one-shots below notify NOBODY, and that is the whole reason a naive
// observer would have been wrong. rebuild_entries() reads pc_breakpoints() +
// watchpoints() only, so a one-shot is not drawn anywhere; and every path that
// sets one (DebugState::resume / step_over / run_to) immediately resumes, so
// notifying here would fire on every single resume for something invisible.
void BreakpointSet::set_oneshot(uint16_t addr) {
    oneshot_active_ = true;
    oneshot_addr_ = addr;
}

void BreakpointSet::clear_oneshot() {
    oneshot_active_ = false;
    oneshot_addr_ = 0;
}

bool BreakpointSet::empty() const {
    return pc_bps_.empty() && watchpoints_.empty() && !oneshot_active_;
}
