#pragma once

#include "debug/breakpoints.h"
#include <cstdint>

enum class StepMode { NONE, INTO, OVER, OUT, RUN_TO_CYCLE, STEP_BACK, RUN_BACK_TO_CYCLE };

/// Manages the debugger's execution control state: pause/resume, step modes,
/// and breakpoint checking.  Pure C++ — no GUI dependency.
class DebugState {
public:
    /// Is the debugger active — i.e. DRIVING the machine (the UI has it open)?
    ///
    /// This is the gate on everything that only makes sense while a human is
    /// watching, and it is deliberately NOT the gate on "are breakpoints
    /// live" — see armed() below. Its readers, all in the hot path:
    ///   * the STEP machinery (StepMode OUT / STEP_BACK / RUN_BACK_TO_CYCLE)
    ///   * Emulator::run_frame's "render every frame" hint, so the panels see
    ///     a live framebuffer
    ///   * the per-instruction VideoTiming::advance() walk (Task 27 C10),
    ///     whose only observer is a human reading the raster readout
    /// Forcing this true with the window closed would switch all of that on
    /// for nobody's benefit, which is why GH #219 did not do it.
    bool active() const { return active_; }
    void set_active(bool a) { active_ = a; refresh_gates_(); }

    /// GH #219 — `--persistent-breakpoints`: keep breakpoints live for the
    /// whole run, not just while the debugger window is open. Set once from
    /// EmulatorConfig at init(); the UI never touches it.
    bool persistent_breakpoints() const { return persistent_; }
    void set_persistent_breakpoints(bool p) { persistent_ = p; refresh_gates_(); }

    /// Are breakpoints and watchpoints LIVE? The hot-path gate.
    ///
    /// A single cached bool, so the default configuration executes exactly the
    /// same load-and-branch the old active() gate did — the flag costs nothing
    /// to the user who does not pass it. With --persistent-breakpoints it is
    /// true for the whole run: that is what "persistent" means, and the
    /// per-instruction breakpoint check is the price of it.
    bool armed() const { return armed_; }

    /// Are WATCHPOINTS live — i.e. would a memory or port access made RIGHT
    /// NOW be allowed to raise the data-breakpoint latch? The hot-path gate
    /// for the eight Mmu sites and PortDispatch::check_io_watchpoint_.
    ///
    /// armed() AND the access being the EMULATED MACHINE's, rather than the
    /// debugger's own. A watchpoint exists to catch the guest
    /// program touching an address; the panel that DISPLAYS that address is an
    /// observer and must be invisible to it. Before this gate existed, the
    /// Watches / Memory / Stack / Disassembly panels all read guest memory
    /// through the same Mmu::read() the CPU uses, so a READ watchpoint on any
    /// address a panel happened to show was latched by the panel's own
    /// refresh — ~4 times a second while running — and the next Run or Step
    /// stopped one instruction later, at an address the watchpoint had
    /// nothing to do with.
    ///
    /// It is a single cached bool, recomputed only when something a human did
    /// changed it, so the eight Mmu sites execute exactly the load-and-branch
    /// they executed when they read armed() directly.
    bool watchpoints_live() const { return wp_live_; }

    /// Is the emulated machine currently executing? See GuestExecutionScope.
    /// Public for the one assertion that enforces the invariant, and for
    /// tests.
    bool guest_access() const { return guest_access_; }

    /// RAII — "the code in this block IS the emulated machine".
    ///
    /// Taken by the three functions that drive the machine: Emulator::
    /// run_frame(), execute_single_instruction() and step_frame_slot(). Every
    /// guest memory access and every guest port access happens inside one of
    /// them, so watchpoints are live exactly there and NOWHERE ELSE.
    ///
    /// That is what makes this safe by DEFAULT rather than by discipline: a
    /// panel, a tool, a snapshot saver — anything that reads guest memory
    /// from outside the emulator's own execution — cannot fire a watchpoint,
    /// whatever entry point it calls and whether or not its author knew this
    /// problem existed. There is no call site to remember, because the gate
    /// does not depend on the caller at all.
    ///
    /// It nests (run_frame -> step_frame_slot is a real nesting under
    /// rewind replay) and restores the previous value, so it is safe on any
    /// path out, including the data-breakpoint early return.
    class GuestExecutionScope {
    public:
        explicit GuestExecutionScope(DebugState& ds)
            : ds_(ds), prev_(ds.guest_access_) { ds.set_guest_access_(true); }
        ~GuestExecutionScope() { ds_.set_guest_access_(prev_); }
        GuestExecutionScope(const GuestExecutionScope&) = delete;
        GuestExecutionScope& operator=(const GuestExecutionScope&) = delete;
    private:
        DebugState& ds_;
        bool prev_;
    };

    /// RAII — "the code in this block is the DEBUGGER looking, not the guest".
    ///
    /// The counterpart, for the handful of debugger reads that necessarily
    /// happen INSIDE GuestExecutionScope because they are interleaved with
    /// execution: the trace log's 4-byte opcode capture and the call-stack
    /// tracker's 3-byte opcode peek, both of which read bytes past a short
    /// instruction that the CPU never fetches. Both are switched on by the
    /// debugger itself, so without this they fired spurious READ watchpoints
    /// on data bytes for exactly the user who had the debugger open.
    class InspectionScope {
    public:
        explicit InspectionScope(DebugState& ds)
            : ds_(ds), prev_(ds.guest_access_) { ds.set_guest_access_(false); }
        ~InspectionScope() { ds_.set_guest_access_(prev_); }
        InspectionScope(const InspectionScope&) = delete;
        InspectionScope& operator=(const InspectionScope&) = delete;
    private:
        DebugState& ds_;
        bool prev_;
    };

    bool paused() const { return paused_; }
    void pause();
    void resume();

    // Step modes.
    void step_into();
    void step_over(uint16_t next_pc);
    void step_out(uint16_t current_sp);
    void run_to(uint16_t addr);
    void run_to_cycle(uint64_t target_cycle);
    void step_back(int n = 1);
    void run_back_to_cycle(uint64_t target_cycle);
    uint64_t target_cycle() const { return target_cycle_; }
    int step_back_count() const { return step_back_count_; }

    /// Called before each CPU instruction in the hot loop.
    /// Returns true if execution should break (pause).
    bool should_break(uint16_t pc) const;

    /// GH #221 — the one-instruction step-off, consumed by the hot loop's
    /// breakpoint gate immediately before should_break().
    ///
    /// Returns true (and disarms) on the FIRST breakpoint test of a resumed
    /// run, telling the gate to skip that one test. Every transition out of
    /// paused arms it — resume(), step_into/over/out(), run_to(),
    /// run_to_cycle(), step_back(), run_back_to_cycle() — because all of them
    /// leave PC exactly where the user is looking at it, and the gate runs
    /// BEFORE the instruction at that PC. Without the skip, F5 at a breakpoint
    /// cleared paused_, the gate re-matched the unchanged PC and the machine
    /// re-paused having made no progress: #221.
    ///
    /// It suppresses the RESUMED-FROM address and no other, and it does so
    /// without storing one — but that rests on a PRECONDITION, stated here
    /// because it is not self-evident and was wrong in the first cut of this
    /// fix: **the arm is only ever raised on a real paused -> running edge**
    /// (see unpause_()). Given that, no instruction can execute between the
    /// arm and its consumption — the machine was stopped — so the address the
    /// gate is looking at when it consumes the arm IS the address the resume
    /// was issued at. Drop the precondition and the property goes with it: a
    /// resume issued while the machine is already running arms against a PC
    /// that keeps moving, and the arm lands on some later, unrelated
    /// breakpoint test and swallows it.
    ///
    /// It is also exactly one instruction wide, so a breakpoint at the NEXT
    /// address — or at this one, on the next pass round a loop — still fires.
    bool consume_step_off();

    /// Step Out predicate — called after EVERY instruction while
    /// StepMode::OUT is armed (GH #203).
    ///
    /// Returns true when the instruction that just completed was the return
    /// that left the subroutine Step Out was armed in, so the caller must
    /// re-pause. The caller supplies:
    ///   @param sp_before  SP immediately before the instruction executed
    ///   @param sp_after   SP immediately after it executed
    ///   @param opcode     first opcode byte, read at the PRE-execution PC
    ///   @param opcode2    second opcode byte (only read for the 0xED prefix)
    ///
    /// Three conditions, all required:
    ///   1. the stack actually shrank by one return address (sp_after ==
    ///      sp_before + 2) — this is what distinguishes a TAKEN `RET cc` from
    ///      an untaken one, and what makes the predicate inert on the steps
    ///      that execute no CPU instruction at all (DMA burst, boot hold,
    ///      parked CPU);
    ///   2. the opcode was a return form (see the .cpp) — this is what stops
    ///      a bare `POP rr` from ending the step;
    ///   3. the pop unwound the stack PAST the arming point (sp_after >
    ///      step_out_sp_, strictly). A nested call's own RET lands exactly ON
    ///      it and must NOT end the step; likewise an interrupt taken while
    ///      stepping out returns to the same routine, not out of it.
    bool check_step_out(uint16_t sp_before, uint16_t sp_after,
                        uint8_t opcode, uint8_t opcode2) const;

    /// The memory-free half of the test above: conditions 1 and 3 only.
    ///
    /// check_step_out() calls it, so there is one definition of the SP rules.
    /// It is public because the caller needs it FIRST, as a gate on whether
    /// fetching the opcode bytes is safe at all: an accepted NMI/INT executes
    /// no instruction and fetches nothing, so reading the opcode at PC in that
    /// slot is a read the CPU never makes — enough to fire a READ watchpoint
    /// and end the step in the wrong place. Every such slot moves SP the wrong
    /// way, so a false here means the read must not be taken.
    bool step_out_sp_qualifies(uint16_t sp_before, uint16_t sp_after) const;

    BreakpointSet& breakpoints() { return breakpoints_; }
    const BreakpointSet& breakpoints() const { return breakpoints_; }

    StepMode step_mode() const { return step_mode_; }

    /// Set by MMU when a data breakpoint (read/write) is hit.
    /// Checked after each instruction in the hot loop.
    bool data_bp_hit() const { return data_bp_hit_; }
    void set_data_bp_hit(bool h) { data_bp_hit_ = h; }
    uint16_t data_bp_addr() const { return data_bp_addr_; }
    void set_data_bp_addr(uint16_t a) { data_bp_addr_ = a; }

private:
    /// The ONE writer of guest_access_ — private, so the flag can only be
    /// moved by the two RAII scopes above and can never be left stuck by an
    /// early return, an exception or a forgotten reset.
    void set_guest_access_(bool g) { guest_access_ = g; refresh_gates_(); }

    /// Recompute BOTH cached hot-path gates from the three inputs that feed
    /// them (active_, persistent_, guest_access_). Named for the gates rather
    /// than for armed_ alone, which is what it used to maintain: it now also
    /// owns wp_live_, and a name that mentions only half of what a function
    /// maintains is how the next person misses the other half.
    ///
    /// Called only from the three setters — i.e. only when a human opened the
    /// debugger, passed --persistent-breakpoints, or the machine entered or
    /// left execution (twice per frame, or twice per debugger Step). Never
    /// from the hot path.
    void refresh_gates_() {
        armed_ = active_ || persistent_;
        wp_live_ = armed_ && guest_access_;
        // Disarming breakpoints drops any pending step-off with them. The gate
        // that consumes it does not run while !armed(), so PC moves on freely
        // and a surviving arm would suppress an unrelated test the moment
        // breakpoints came back (GH #221).
        if (!armed_) step_off_pending_ = false;
    }

    /// The ONE way out of paused. Every resume-family transition goes through
    /// it so the GH #221 step-off arm cannot be forgotten by a path added
    /// later — the defect was that F5 resumed onto its own breakpoint, and
    /// Step Over / Step Out / Run to Here resumed onto it in exactly the same
    /// way.
    ///
    /// It arms ONLY on a real paused -> running edge, and that guard is the
    /// whole of consume_step_off()'s correctness, not a tidiness check. A
    /// resume issued while the machine is ALREADY RUNNING has no "address the
    /// user is standing on" to protect: PC is wherever the free run has got
    /// to, and by the time the gate consumes the arm it has moved on again. An
    /// unconditional arm there would swallow the next legitimate hit, and the
    /// UI reaches this state by ordinary use — the debugger window's
    /// "F5: Continue" button is not enable-gated, and the main window's global
    /// F5 handler fires whenever the debugger is enabled, whatever the machine
    /// is doing. Guarding here rather than at those call sites covers the ones
    /// that exist and the ones that do not yet.
    void unpause_() {
        if (paused_) step_off_pending_ = true;
        paused_ = false;
    }

    bool active_ = false;
    bool persistent_ = false;
    bool armed_ = false;
    // Kept adjacent to armed_ deliberately: the eight Mmu watchpoint sites
    // read wp_live_ on every memory access, and it is the one member of this
    // class the hot path touches.
    //
    // guest_access_ defaults FALSE, which is the whole design: watchpoints are
    // OFF until the emulator itself declares that it is executing. Everything
    // else — every panel, every tool, every saver — is an observer by
    // construction rather than by remembering to say so.
    bool guest_access_ = false;
    bool wp_live_ = false;
    bool paused_ = false;
    bool step_off_pending_ = false;
    bool data_bp_hit_ = false;
    uint16_t data_bp_addr_ = 0;
    StepMode step_mode_ = StepMode::NONE;
    uint16_t step_out_sp_ = 0;
    uint64_t target_cycle_ = 0;
    int      step_back_count_ = 1;
    BreakpointSet breakpoints_;
};
