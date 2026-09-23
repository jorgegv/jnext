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

// GH #225 — the ONE place the model becomes the live set.
//
// Rebuilt whole rather than patched incrementally, and deliberately: an
// incremental update has to be right at six mutation sites plus the master
// switch, which is the same "every call site must remember" shape #220 removed
// from this class. It runs only when a human changes something, over a handful
// of entries, and never from the hot loop.
void BreakpointSet::rebuild_live_() {
    pc_live_.clear();
    wp_live_.clear();

    // The master switch, in its entirety. It changes nothing in the model, so
    // flipping it back rebuilds exactly the set that was there.
    if (!master_enabled_) return;

    for (const auto& entry : pc_all_)
        if (entry.second) pc_live_.insert(entry.first);

    for (const auto& wp : wp_all_)
        if (wp.enabled) wp_live_.push_back(wp);
}

void BreakpointSet::add_pc(uint16_t addr) {
    // emplace, not operator[]: an address that already carries a breakpoint
    // keeps ITS OWN enabled flag. See the header — a second gutter click on a
    // disabled breakpoint must not silently re-arm it.
    pc_all_.emplace(addr, true);
    rebuild_live_();
    notify(BreakpointChange::PcBreakpoints);
}

void BreakpointSet::remove_pc(uint16_t addr) {
    pc_all_.erase(addr);
    rebuild_live_();
    notify(BreakpointChange::PcBreakpoints);
}

bool BreakpointSet::has_pc(uint16_t addr) const {
    return pc_live_.count(addr) > 0;
}

bool BreakpointSet::pc_enabled(uint16_t addr) const {
    auto it = pc_all_.find(addr);
    return it != pc_all_.end() && it->second;
}

void BreakpointSet::set_pc_enabled(uint16_t addr, bool enabled) {
    auto it = pc_all_.find(addr);
    if (it == pc_all_.end() || it->second == enabled) return;
    it->second = enabled;
    rebuild_live_();
    notify(BreakpointChange::PcBreakpoints);
}

void BreakpointSet::clear_all_pc() {
    pc_all_.clear();
    rebuild_live_();
    notify(BreakpointChange::PcBreakpoints);
}

void BreakpointSet::add_watchpoint(uint16_t addr, WatchType type) {
    // Avoid duplicates — and, as in add_pc(), leave an existing one's enabled
    // flag alone.
    for (const auto& wp : wp_all_) {
        if (wp.addr == addr && wp.type == type) return;
    }
    wp_all_.push_back({addr, type, true});
    rebuild_live_();
    notify(BreakpointChange::Watchpoints);
}

void BreakpointSet::remove_watchpoint(uint16_t addr, WatchType type) {
    wp_all_.erase(
        std::remove_if(wp_all_.begin(), wp_all_.end(),
            [addr, type](const Watchpoint& wp) {
                return wp.addr == addr && wp.type == type;
            }),
        wp_all_.end());
    rebuild_live_();
    notify(BreakpointChange::Watchpoints);
}

bool BreakpointSet::has_watchpoint(uint16_t addr, WatchType type) const {
    for (const auto& wp : wp_live_) {
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

bool BreakpointSet::watchpoint_exists(uint16_t addr, WatchType type) const {
    for (const auto& wp : wp_all_)
        if (wp.addr == addr && wp.type == type) return true;
    return false;
}

bool BreakpointSet::watchpoint_enabled(uint16_t addr, WatchType type) const {
    for (const auto& wp : wp_all_)
        if (wp.addr == addr && wp.type == type) return wp.enabled;
    return false;
}

void BreakpointSet::set_watchpoint_enabled(uint16_t addr, WatchType type,
                                           bool enabled) {
    for (auto& wp : wp_all_) {
        if (wp.addr != addr || wp.type != type) continue;
        if (wp.enabled == enabled) return;
        wp.enabled = enabled;
        rebuild_live_();
        notify(BreakpointChange::Watchpoints);
        return;
    }
}

// GH #222. See the header for the decode rule and why it is not has_watchpoint().
bool BreakpointSet::has_io_watchpoint(uint16_t port, WatchType type) const {
    for (const auto& wp : wp_live_) {
        if (wp.type != type) continue;
        const bool hit = (wp.addr <= 0x00FF) ? ((port & 0x00FF) == wp.addr)
                                             : (port == wp.addr);
        if (hit) return true;
    }
    return false;
}

void BreakpointSet::clear_all_watchpoints() {
    wp_all_.clear();
    rebuild_live_();
    notify(BreakpointChange::Watchpoints);
}

// GH #225 — the master switch. BOTH halves are notified, because both views
// change: the Breakpoints panel's checkboxes and the disassembly gutter's dots
// all go suspended at once, and the gutter subscribes to PcBreakpoints only.
void BreakpointSet::set_master_enabled(bool enabled) {
    if (master_enabled_ == enabled) return;
    master_enabled_ = enabled;
    rebuild_live_();
    notify(BreakpointChange::PcBreakpoints);
    notify(BreakpointChange::Watchpoints);
}

// The one-shots below notify NOBODY, and that is the whole reason a naive
// observer would have been wrong. rebuild_entries() reads pc_breakpoints() +
// watchpoints() only, so a one-shot is not drawn anywhere; and every path that
// sets one (DebugState::resume / step_over / run_to) immediately resumes, so
// notifying here would fire on every single resume for something invisible.
//
// They are outside the GH #225 enable model for a second reason: Step Over,
// Step Out and Run to Here are how a user navigates a suspended machine, and
// suspending them along with the user's breakpoints would break stepping
// exactly when the master switch is off.
void BreakpointSet::set_oneshot(uint16_t addr) {
    oneshot_active_ = true;
    oneshot_addr_ = addr;
}

void BreakpointSet::clear_oneshot() {
    oneshot_active_ = false;
    oneshot_addr_ = 0;
}

bool BreakpointSet::empty() const {
    return pc_all_.empty() && wp_all_.empty() && !oneshot_active_;
}
