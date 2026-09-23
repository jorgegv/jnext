#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class WatchType { READ, WRITE, READ_WRITE, IO_READ, IO_WRITE };

struct Watchpoint {
    uint16_t addr;
    WatchType type;
    /// GH #225 — the user's own Enabled checkbox for THIS watchpoint. A
    /// disabled watchpoint keeps its address and type and stays in the list;
    /// it simply is not copied into the live set the hot path scans.
    bool enabled = true;
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
/// ENABLE / DISABLE (GH #225). A breakpoint now has an ENABLED flag, and the
/// whole set has a MASTER SWITCH. Both are properties of the data model, not
/// of the panel that draws it, because the thing they change is whether the
/// hot loop stops — and the hot loop cannot see Qt.
///
/// The two compose by CONSTRUCTION, which is the interaction the issue asks to
/// get right. There is exactly one source of truth — `pc_all_` and `wp_all_`,
/// each entry carrying its own flag — plus one derived cache, `pc_live_` /
/// `wp_live_`, holding only what can actually fire. The master switch does not
/// touch the flags at all: it decides whether the cache is rebuilt from them
/// or left empty. So flipping it off and on again cannot lose a per-breakpoint
/// state, because it never read one.
///
/// THE HOT PATH PAYS NOTHING, and pays nothing EXTRA for a disabled
/// breakpoint. has_pc(), has_watchpoint(), has_io_watchpoint() and
/// has_any_watchpoints() read the derived cache, whose types are exactly what
/// they read before this feature existed and whose contents are a SUBSET of
/// the model. A disabled breakpoint is not in it, so it is not hashed, not
/// compared and not iterated — it is strictly cheaper than an armed one, not
/// dearer. Every rebuild happens in a mutator, i.e. when a human clicks
/// something, never per instruction.
///
/// should_break() and has_pc() are const readers and notify nobody; the
/// one-shot mutators (set_oneshot / clear_oneshot, driven by resume /
/// step_over / run_to on every resume) deliberately notify nobody either — a
/// one-shot is transient, is in neither pc_breakpoints() nor watchpoints(),
/// and is not something the panels draw. A one-shot is ALSO deliberately
/// outside the enable model: Step Over and Run to Here must keep working while
/// the user has every breakpoint suspended.
///
/// Copy and move carry the observers, which is what keeps a debugger window
/// subscribed across emulator_cold_boot()'s save / reconstruct / restore of
/// this set (src/platform/emulator_boot.h). Nothing else copies a live set.
/// They carry the enable flags and the master switch with them for the same
/// reason.
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

    /// Create a PC breakpoint at `addr`, ENABLED (GH #225: every newly created
    /// breakpoint is enabled, from every creation route).
    ///
    /// IDEMPOTENT ON THE FLAG: calling it for an address that already carries a
    /// breakpoint leaves that breakpoint's enabled state exactly as it was.
    /// This is what the old std::unordered_set::insert() did, and it matters
    /// now that the state is observable — a second click on an address must not
    /// silently re-arm a breakpoint the user disabled.
    void add_pc(uint16_t addr);
    void remove_pc(uint16_t addr);

    /// LIVE: will a PC breakpoint at `addr` actually stop the machine?
    ///
    /// The hot-path query, and DebugState::should_break()'s. False for a
    /// disabled breakpoint and for every breakpoint while the master switch is
    /// off — which is the point. A view that wants to DRAW a suspended
    /// breakpoint wants pc_exists() / pc_enabled() below, not this.
    bool has_pc(uint16_t addr) const;

    void clear_all_pc();

    /// THE MODEL: every PC breakpoint, enabled or not, as addr -> enabled.
    /// This is what the Breakpoints panel lists; has_pc() is what fires.
    const std::unordered_map<uint16_t, bool>& pc_breakpoints() const { return pc_all_; }

    /// Is there a PC breakpoint at `addr` at all, whatever its state?
    bool pc_exists(uint16_t addr) const { return pc_all_.count(addr) > 0; }

    /// This breakpoint's OWN Enabled flag, ignoring the master switch.
    /// False when there is no breakpoint at `addr`.
    ///
    /// Deliberately independent of the master switch: it is what the panel's
    /// checkbox shows, and it has to survive a master round trip untouched.
    bool pc_enabled(uint16_t addr) const;

    /// Set this breakpoint's own Enabled flag. No-op if there is no breakpoint
    /// at `addr`, or if the flag already has that value (so a panel that
    /// echoes the model back does not loop).
    void set_pc_enabled(uint16_t addr, bool enabled);

    void add_watchpoint(uint16_t addr, WatchType type);
    void remove_watchpoint(uint16_t addr, WatchType type);

    /// LIVE, exactly as has_pc() is live — see there.
    bool has_watchpoint(uint16_t addr, WatchType type) const;

    /// Is there a watchpoint of this exact (addr, type) at all? The model
    /// query; has_watchpoint() additionally applies READ_WRITE matching and
    /// the enable state, which is wrong for a list and for a checkbox.
    bool watchpoint_exists(uint16_t addr, WatchType type) const;

    /// This watchpoint's own Enabled flag, ignoring the master switch.
    bool watchpoint_enabled(uint16_t addr, WatchType type) const;
    void set_watchpoint_enabled(uint16_t addr, WatchType type, bool enabled);

    /// I/O watchpoint lookup — the port-side counterpart of has_watchpoint(),
    /// and a SEPARATE entry point because ports are not matched like memory.
    ///
    /// A ZX port is decoded by address-line masking, not by a 16-bit compare
    /// (`src/port/port_dispatch.h`): `OUT (254),A` puts A in the high byte, so
    /// the ULA is reached at 0x01FE, 0x7FFE, 0xFEFE ... and an exact-match
    /// watchpoint on 0x00FE would never fire. The stored `addr` is therefore
    /// read the way the port map itself writes a port (GH #222):
    ///
    ///   * high byte ZERO  (0x0000-0x00FF) — a PARTIAL decode: matches any
    ///     port whose LOW BYTE equals it. 0xFE catches every ULA access.
    ///   * high byte NON-ZERO             — a FULL 16-bit decode: matches
    ///     that port exactly. 0x243B catches the NextREG select port and not
    ///     0x253B, whose low byte is the same.
    ///
    /// Consequence, accepted and documented: the exact 16-bit port 0x00xx
    /// cannot be named. No Next port is a full-16-bit decode with a zero high
    /// byte, so nothing real is lost.
    ///
    /// `type` is IO_READ or IO_WRITE. There is no I/O READ_WRITE: the enum has
    /// no such value, so a user who wants both adds both.
    ///
    /// LIVE, like the two above.
    bool has_io_watchpoint(uint16_t port, WatchType type) const;

    void clear_all_watchpoints();

    /// THE MODEL: every watchpoint, enabled or not, each carrying its flag.
    const std::vector<Watchpoint>& watchpoints() const { return wp_all_; }

    /// LIVE: is there any watchpoint that could fire? The hot path's pre-gate,
    /// read once per memory access and once per port access before the scan.
    ///
    /// Live, not model, and that is what makes a disabled watchpoint FREE
    /// rather than merely cheap: disable the only one and this goes false
    /// again, so the eight Mmu sites and PortDispatch short-circuit exactly as
    /// they do when no watchpoint was ever set.
    bool has_any_watchpoints() const { return !wp_live_.empty(); }

    /// GH #225 — THE MASTER SWITCH. Suspends every breakpoint and watchpoint
    /// without deleting any and without touching any per-breakpoint flag;
    /// switching it back restores exactly the set that was there.
    ///
    /// One-shots are NOT suspended (see the class comment): Step Over, Step
    /// Out and Run to Here must still work while breakpoints are suspended.
    bool master_enabled() const { return master_enabled_; }
    void set_master_enabled(bool enabled);

    // One-shot breakpoints (for step over, run to cursor).
    void set_oneshot(uint16_t addr);
    void clear_oneshot();
    bool has_oneshot() const { return oneshot_active_; }
    uint16_t oneshot_addr() const { return oneshot_addr_; }

    /// Returns true if no breakpoints or watchpoints are set.
    ///
    /// THE MODEL, not the live set: a disabled breakpoint is still a
    /// breakpoint the user created and can see, so a set holding one is not
    /// empty. Reading the live set here would make "clear all" and "disable
    /// all" indistinguishable to every caller.
    bool empty() const;

private:
    void notify(BreakpointChange what);

    /// Recompute the live cache from the model. Called by every mutator and by
    /// the master switch — never from the hot path.
    void rebuild_live_();

    struct Observer {
        ObserverId id;
        std::function<void(BreakpointChange)> fn;
    };

    // ── THE LIVE CACHE — what the hot path reads ──────────────────────
    //
    // FIRST, and the same four members in the same order the class had before
    // observation or enabling existed, so the hot path's reads keep the
    // offsets they had. They are a strict subset of the model below.
    std::unordered_set<uint16_t> pc_live_;
    std::vector<Watchpoint>      wp_live_;
    bool oneshot_active_ = false;
    uint16_t oneshot_addr_ = 0;

    // ── THE MODEL — what the panels list (GH #225) ────────────────────
    std::unordered_map<uint16_t, bool> pc_all_;   // addr -> individually enabled
    std::vector<Watchpoint>            wp_all_;   // each carries .enabled
    bool master_enabled_ = true;

    // APPENDED, never interleaved.
    std::vector<Observer> observers_;
    ObserverId next_observer_id_ = 1;
};
