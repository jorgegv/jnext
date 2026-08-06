#pragma once

#include <cstdint>
#include <functional>
#include <unordered_set>
#include <vector>

enum class WatchType { READ, WRITE, READ_WRITE, IO_READ, IO_WRITE };

struct Watchpoint {
    uint16_t addr;
    WatchType type;
};

/// What changed, so an observer can act on the half it actually draws.
///
/// The distinction is not decoration: the disassembly gutter paints
/// has_pc() only, so repainting it on a watchpoint change is pure waste —
/// it subscribes and ignores `Watchpoints`. The Breakpoints panel lists
/// both and acts on either.
enum class BreakpointChange { PcBreakpoints, Watchpoints };

/// Manages PC breakpoints, watchpoints, and one-shot breakpoints.
/// Pure C++ — no GUI dependency.
///
/// OBSERVATION (GH #220). Twelve GUI call sites mutate this set, and each one
/// used to be individually responsible for repainting the two views that show
/// it. Nothing enforced completeness, and the omission shipped twice (#215
/// exposed it, #218's audit found three wrong sites). So the NOTIFICATION IS
/// EMITTED HERE, by the mutators themselves: a call site cannot forget, because
/// it is not asked. Views subscribe with add_observer() instead.
///
/// std::function, not a signal — src/debug/ is deliberately Qt-free and this
/// class is the debugger's pure-C++ backend.
///
/// The hot path pays nothing. should_break() and has_pc() are const readers and
/// notify nobody; the one-shot mutators (set_oneshot / clear_oneshot, driven by
/// resume / step_over / run_to on every resume) deliberately notify nobody
/// either — a one-shot is transient, is in neither pc_breakpoints() nor
/// watchpoints(), and is not something the panels draw.
///
/// Copy and move carry the observers, which is what keeps a debugger window
/// subscribed across emulator_cold_boot()'s save / reconstruct / restore of
/// this set (src/platform/emulator_boot.h). Nothing else copies a live set.
class BreakpointSet {
public:
    /// Handle returned by add_observer(); 0 is "not subscribed".
    using ObserverId = int;

    /// Subscribe to every change of the drawn breakpoint state. The callback
    /// runs synchronously, inside the mutator, with the set already updated.
    ///
    /// The callback typically captures a raw `this`, so EVERY SUBSCRIBER MUST
    /// remove_observer() IN ITS DESTRUCTOR. A callback must not add or remove
    /// observers itself — the notification loop iterates the list in place.
    ObserverId add_observer(std::function<void(BreakpointChange)> fn);
    void remove_observer(ObserverId id);

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
    void notify(BreakpointChange what);

    struct Observer {
        ObserverId id;
        std::function<void(BreakpointChange)> fn;
    };

    std::unordered_set<uint16_t> pc_bps_;
    std::vector<Watchpoint> watchpoints_;
    bool oneshot_active_ = false;
    uint16_t oneshot_addr_ = 0;

    // APPENDED, never interleaved: the four members above keep the offsets they
    // had before observation existed, so the hot path's reads are untouched.
    std::vector<Observer> observers_;
    ObserverId next_observer_id_ = 1;
};
