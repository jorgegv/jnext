#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

enum class WatchType { READ, WRITE, READ_WRITE, IO_READ, IO_WRITE };

struct Watchpoint {
    uint16_t addr;
    WatchType type;
};

/// Manages PC breakpoints, watchpoints, and one-shot breakpoints.
/// Pure C++ — no GUI dependency.
class BreakpointSet {
public:
    void add_pc(uint16_t addr);
    void remove_pc(uint16_t addr);
    bool has_pc(uint16_t addr) const;
    void clear_all_pc();
    const std::unordered_set<uint16_t>& pc_breakpoints() const { return pc_bps_; }

    void add_watchpoint(uint16_t addr, WatchType type);
    void remove_watchpoint(uint16_t addr, WatchType type);
    bool has_watchpoint(uint16_t addr, WatchType type) const;
    void clear_all_watchpoints();
    const std::vector<Watchpoint>& watchpoints() const { return watchpoints_; }
    bool has_any_watchpoints() const { return !watchpoints_.empty(); }

    // One-shot breakpoints (for step over, run to cursor).
    void set_oneshot(uint16_t addr);
    void clear_oneshot();
    bool has_oneshot() const { return oneshot_active_; }
    uint16_t oneshot_addr() const { return oneshot_addr_; }

    /// Returns true if no breakpoints or watchpoints are set.
    bool empty() const;

private:
    std::unordered_set<uint16_t> pc_bps_;
    std::vector<Watchpoint> watchpoints_;
    bool oneshot_active_ = false;
    uint16_t oneshot_addr_ = 0;
};
