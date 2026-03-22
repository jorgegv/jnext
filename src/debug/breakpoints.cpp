#include "debug/breakpoints.h"

#include <algorithm>

void BreakpointSet::add_pc(uint16_t addr) {
    pc_bps_.insert(addr);
}

void BreakpointSet::remove_pc(uint16_t addr) {
    pc_bps_.erase(addr);
}

bool BreakpointSet::has_pc(uint16_t addr) const {
    return pc_bps_.count(addr) > 0;
}

void BreakpointSet::clear_all_pc() {
    pc_bps_.clear();
}

void BreakpointSet::add_watchpoint(uint16_t addr, WatchType type) {
    // Avoid duplicates.
    for (const auto& wp : watchpoints_) {
        if (wp.addr == addr && wp.type == type) return;
    }
    watchpoints_.push_back({addr, type});
}

void BreakpointSet::remove_watchpoint(uint16_t addr, WatchType type) {
    watchpoints_.erase(
        std::remove_if(watchpoints_.begin(), watchpoints_.end(),
            [addr, type](const Watchpoint& wp) {
                return wp.addr == addr && wp.type == type;
            }),
        watchpoints_.end());
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

void BreakpointSet::clear_all_watchpoints() {
    watchpoints_.clear();
}

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
