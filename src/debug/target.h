#pragma once

// ---------------------------------------------------------------------------
// jnext::dbg::Target - the machine half of the debugger facade.
//
// Epic #486 (a debugger over a remote target), task R0.1, with the owner's
// approval of 2026-11-18 (Q1) for this header and for the additions it makes to
// `debugger.h`. Every declaration carries its CAP id (`TGT-01`..`TGT-04`); the
// map is in doc/design/debug-subsystem/b0-cap-traceability.md, the verb-by-verb
// classification that justifies the cut is section 15 of
// doc/design/DEBUGGER-REMOTE-REAL-NEXT-PLAN.md.
//
// WHAT THIS IS. A `Debugger` built over a `Target` (TGT-01) is a second facade,
// over a machine that is NOT an in-process `Emulator`: a remote jnext or a Next
// speaking DZRP. `Target` carries only the machine primitives such a remote can
// answer (the DZRP-shaped ones), and only those the facade calls. The local stop
// machinery (pause state, step modes, event delivery hooks, rewind, trace,
// coverage, captures, the loop driver) stays behind `Debugger::Impl` and is
// local-only: a facade over a `Target` refuses those verbs with
// `Result::Unsupported`, or returns the empty value of a direct-value query.
//
// WHAT THIS IS NOT. Not frozen: a published header, gated by the include-graph
// lint (`test/lint-debug-headers.sh`, in `HEADERS` and NOT in
// `STD_ONLY_HEADERS`: it includes the published `result.h`, `events.h` and
// `inspect.h` for the types it needs), but later sub-issues of the epic extend
// it with the run-control primitives (pause, resume, temporary breakpoints) when
// they implement them. Rule 1 of `debugger.h`'s banner holds here too: no
// `Emulator`, so a remote `Target` can exist in a build that has none.
//
// CHEAP READS (epic #486, R2.1): `info()`, `capabilities()`, `why_unavailable()`,
// `register_validity()`, `run_state()` and `take_stop()` are cheap, non-blocking reads
// of state the target already holds: NO I/O. `Debugger::pump()` calls `run_state()` once
// per serviced command, and a UI reads the identity queries on every repaint. Everything
// else may talk to the machine (a remote target's reads and writes are round trips).
//
// The facade, not the target, owns argument validation, the local-only guards
// and the attribution of every mutation; a target answers the machine action.
// Every answer is untrusted input (a target may be a network peer): the facade
// clamps counts, sizes containers and masks bit sets before it uses them.
// ---------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "debug/events.h"
#include "debug/inspect.h"
#include "debug/result.h"

namespace jnext {
namespace dbg {

// ---------------------------------------------------------------------------
// TGT-03 - what the target is
// ---------------------------------------------------------------------------

/// TGT-03 - which side of a link the machine is on.
enum class TargetKind : uint8_t { Local = 0, Remote };

/// TGT-03 - identity text for a title or status line. The strings are carried
/// VERBATIM (a remote's name may be long or hold control characters): sanitising
/// them for display belongs to the frontend that shows them.
struct TargetInfo {
    TargetKind  kind = TargetKind::Local;
    /// "jnext", or the remote's `CMD_INIT` program name.
    std::string name;
    /// `JNEXT_VERSION_STRING` locally; the remote's DZRP answer.
    std::string version;
    /// "local", "tcp:127.0.0.1:14000", ...
    std::string link;
};

// ---------------------------------------------------------------------------
// TGT-02 - what the target can do
// ---------------------------------------------------------------------------

/// TGT-02 - one bit per capability of section 11 of the plan ("What degrades on a
/// remote target"). Not a row-for-row copy: some rows have two bits (Run, Pause),
/// some none (register FIELDS are `RegisterValidity`; host-side rows need only the
/// capabilities of the primitives they call).
///
/// Two groups, each in the explicit order written below, which
/// `debug_types_check.cpp` pins value by value: first what a remote may serve,
/// then what is local-only for now.
/// `Count` is NOT a capability and stays last (`EventKind::Count`'s convention).
enum class Capability : uint8_t {
    // a remote may serve these
    Run = 0,
    Pause,
    InstructionStep,
    RunToCursor,
    ExecuteBreakpoints,
    Watchpoints,
    CommandsWhileRunning,
    RegisterWrite,
    Memory,
    MemoryWhileRunning,
    PhysicalMemory,
    MmuSlots,
    NextRegs,
    Ports,
    SpriteData,
    SpritePaletteClip,
    // local only
    CycleExact,
    Rewind,
    FetchOverlays,
    MachineEvents,
    Copper,
    Video,
    Audio,
    CtcDma,
    Trace,
    Coverage,
    CallStackTracked,
    Scripts,
    MagicBreakpoint,
    Load,
    Reset,
    MachineTime,
    Input,
    Capture,
    MachineState,
    PagingLatches,
    Count
};

/// TGT-02 - the number of capabilities (36).
constexpr size_t CAPABILITY_COUNT = static_cast<size_t>(Capability::Count);

/// TGT-02 - a set of capabilities. A value type; bits at or above `Count` are
/// never set, so a set built from a target's raw answer through `set()` cannot
/// carry an unknown bit.
class CapabilitySet {
public:
    constexpr CapabilitySet() = default;

    /// Exactly `CAPABILITY_COUNT` bits.
    static constexpr CapabilitySet all() {
        CapabilitySet s;
        s.bits_ = (uint64_t{1} << CAPABILITY_COUNT) - 1;
        return s;
    }

    constexpr bool has(Capability c) const {
        return static_cast<size_t>(c) < CAPABILITY_COUNT &&
               ((bits_ >> static_cast<unsigned>(c)) & 1u) != 0;
    }

    /// `Capability::Count` (not a capability) is ignored.
    constexpr CapabilitySet& set(Capability c, bool on = true) {
        if (static_cast<size_t>(c) >= CAPABILITY_COUNT) return *this;
        const uint64_t m = uint64_t{1} << static_cast<unsigned>(c);
        bits_ = on ? (bits_ | m) : (bits_ & ~m);
        return *this;
    }

    constexpr uint64_t bits() const { return bits_; }

    friend constexpr CapabilitySet operator&(CapabilitySet a, CapabilitySet b) {
        CapabilitySet s;
        s.bits_ = a.bits_ & b.bits_;
        return s;
    }
    friend constexpr bool operator==(CapabilitySet a, CapabilitySet b) { return a.bits_ == b.bits_; }
    friend constexpr bool operator!=(CapabilitySet a, CapabilitySet b) { return a.bits_ != b.bits_; }

private:
    uint64_t bits_ = 0;
};

// ---------------------------------------------------------------------------
// TGT-04 - which register fields the target knows
// ---------------------------------------------------------------------------

/// TGT-04 - the register validity mask: which fields of `registers()` the target
/// actually reports. A field a remote cannot read is zero in the returned
/// `Z80Registers`, and its validity bit says that is not a real zero.
struct RegisterValidity {
    enum class Field : uint8_t {
        /// AF BC DE HL, the shadows, IX IY SP PC, I, R: what `CMD_GET_REGISTERS` carries.
        Core = 0,
        IFF1,
        IFF2,
        IM,
        Halted,
        Memptr,
        Q,
        IncDecZ,
        Count
    };

    /// Every field valid (the local machine).
    static constexpr RegisterValidity all() {
        RegisterValidity v;
        v.bits = (uint32_t{1} << static_cast<unsigned>(Field::Count)) - 1;
        return v;
    }

    constexpr bool valid(Field f) const {
        return static_cast<unsigned>(f) < static_cast<unsigned>(Field::Count) &&
               ((bits >> static_cast<unsigned>(f)) & 1u) != 0;
    }

    /// `Field::Count` (not a field) is ignored.
    constexpr RegisterValidity& set(Field f, bool on = true) {
        if (static_cast<unsigned>(f) >= static_cast<unsigned>(Field::Count)) return *this;
        const uint32_t m = uint32_t{1} << static_cast<unsigned>(f);
        bits = on ? (bits | m) : (bits & ~m);
        return *this;
    }

    /// Bits above `Field::Count` are always 0 when built through `set()` / `all()`.
    uint32_t bits = 0;
};

// ---------------------------------------------------------------------------
// TGT-05 - a stop a target reports
// ---------------------------------------------------------------------------

/// TGT-05 - a stop the target reports on its own (a remote's `NTF_PAUSE`), consumed by
/// the facade's `pump()`. The target says WHAT stopped; the facade decides what it means
/// (conditions, handlers, `once`, whether to resume at once).
struct TargetStop {
    enum class Why : uint8_t {
        Manual = 0,   ///< the stop this facade's own `pause()` asked for
        Breakpoint,   ///< an execute breakpoint, at `addr`
        WatchRead,    ///< a watchpoint on a read; `addr` is the ACCESS address
        WatchWrite,   ///< a watchpoint on a write; `addr` is the ACCESS address
        Other         ///< anything else; `text` is the remote's own words
    };
    Why         why  = Why::Other;
    uint16_t    addr = 0;           ///< the PC, or the ACCESS address of a watch stop
    uint16_t    page = PAGE_ANY;    ///< the RAM page behind `addr`; PAGE_ANY = not known (a ROM slot)
    std::string text;               ///< Other: the remote's own words
};

// ---------------------------------------------------------------------------
// TGT-01 - the machine half of the facade
// ---------------------------------------------------------------------------

/// TGT-01 - the machine primitives a `Debugger` needs from a machine it does not
/// hold in-process. Implemented by `LocalTarget` (over the `Emulator`) and by the
/// remote targets of the epic.
///
/// A method here is the ACTION of a verb: the facade has already validated the
/// arguments, applied the local-only guards and (for a write) decided what to
/// log, so a target does not repeat any of that. A target must never block
/// beyond its own link timeouts and must never throw.
class Target {
public:
    virtual ~Target() = default;

    // ---- identity (TGT-02, TGT-03, TGT-04) ---------------------------------
    virtual TargetInfo       info() const = 0;
    virtual CapabilitySet    capabilities() const = 0;
    /// The reason a capability is missing, "" when the target has it.
    virtual std::string      why_unavailable(Capability c) const = 0;
    virtual RegisterValidity register_validity() const = 0;

    // ---- run state, and the one stepping primitive the local path uses -----
    /// `pause_reason` is left None: the facade owns it.
    virtual RunState run_state() const = 0;
    /// Execute exactly one instruction (LocalTarget: `Emulator::debugger_step()`).
    virtual Result   step_instruction() = 0;

    // ---- registers ----------------------------------------------------------
    virtual Z80Registers registers() const = 0;
    virtual Result       set_register(RegId reg, uint16_t value) = 0;

    // ---- memory -------------------------------------------------------------
    // The facade has already refused the identity-only kinds, `Rom` and the ROM
    // page sentinels, so a target sees `Cpu`, `Page` and the kinds it declares.
    virtual Expected<size_t> read(MemSpace space, uint32_t addr, size_t n, uint8_t* buf) const = 0;
    virtual Expected<size_t> write(MemSpace space, uint32_t addr, size_t n, const uint8_t* buf) = 0;

    // ---- banking ------------------------------------------------------------
    virtual std::array<SlotInfo, 8> mmu_slots() const = 0;
    virtual Result                  set_mmu_slot(int slot, uint8_t page) = 0;
    virtual MemSpace                rom_select() const = 0;

    // ---- NextREG and ports --------------------------------------------------
    /// The READ path of a NextREG, never a destructive one.
    virtual uint8_t           nextreg_read(uint8_t reg) const = 0;
    virtual Result            nextreg_write(uint8_t reg, uint8_t value) = 0;
    virtual Expected<uint8_t> port_in(uint16_t port) = 0;
    virtual Result            port_out(uint16_t port, uint8_t value) = 0;

    // ---- sprites ------------------------------------------------------------
    virtual std::vector<SpriteInfo> sprites() const = 0;
    virtual Expected<std::array<uint8_t, SPRITE_ATTR_BYTES>> sprite_attr_raw(uint8_t idx) const = 0;
    virtual Result                  set_sprite_attr_raw(uint8_t idx, const uint8_t* bytes, size_t n) = 0;
    virtual ConstBytes              pattern_ram() const = 0;
    virtual Result                  write_pattern_ram(uint16_t addr, const uint8_t* src, size_t n) = 0;
    virtual Expected<uint16_t>      sprite_palette_rgb333(int bank, uint8_t index) const = 0;
    virtual ClipWindow              sprite_clip() const = 0;

    // ---- run control, breakpoints, watchpoints (TGT-05, epic #486 R2.2/R2.3) ----
    // The local facade never calls these (its stop machinery is `DebugState`'s);
    // `LocalTarget` answers `Unsupported` / false. A remote target maps them to its
    // protocol's continue, pause and breakpoint commands.
    /// Run the machine; no temporaries (a later sub-issue adds them).
    virtual Result resume() = 0;
    /// Ask a running machine to stop; its `TargetStop` arrives through `take_stop()`.
    virtual Result pause() = 0;
    /// The next pending stop, non-blocking and I/O free; false = none.
    virtual bool   take_stop(TargetStop& out) = 0;
    /// An execute breakpoint at `addr`, in RAM page `page` (`PAGE_ANY`: whatever is
    /// mapped there). The handle names it for `remove_breakpoint()`.
    virtual Expected<uint32_t> add_breakpoint(uint16_t addr, uint16_t page) = 0;
    virtual Result             remove_breakpoint(uint32_t handle) = 0;
    /// A watchpoint on `lo..hi` (inclusive), in RAM page `page` or `PAGE_ANY`.
    virtual Expected<uint32_t> add_watchpoint(uint16_t lo, uint16_t hi, uint16_t page,
                                              Access access) = 0;
    virtual Result             remove_watchpoint(uint32_t handle) = 0;
};

}  // namespace dbg
}  // namespace jnext
