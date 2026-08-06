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
    void set_active(bool a) { active_ = a; refresh_armed_(); }

    /// GH #219 — `--persistent-breakpoints`: keep breakpoints live for the
    /// whole run, not just while the debugger window is open. Set once from
    /// EmulatorConfig at init(); the UI never touches it.
    bool persistent_breakpoints() const { return persistent_; }
    void set_persistent_breakpoints(bool p) { persistent_ = p; refresh_armed_(); }

    /// Are breakpoints and watchpoints LIVE? The hot-path gate.
    ///
    /// A single cached bool, so the default configuration executes exactly the
    /// same load-and-branch the old active() gate did — the flag costs nothing
    /// to the user who does not pass it. With --persistent-breakpoints it is
    /// true for the whole run: that is what "persistent" means, and the
    /// per-instruction breakpoint check is the price of it.
    bool armed() const { return armed_; }

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
    void refresh_armed_() { armed_ = active_ || persistent_; }

    bool active_ = false;
    bool persistent_ = false;
    bool armed_ = false;
    bool paused_ = false;
    bool data_bp_hit_ = false;
    uint16_t data_bp_addr_ = 0;
    StepMode step_mode_ = StepMode::NONE;
    uint16_t step_out_sp_ = 0;
    uint64_t target_cycle_ = 0;
    int      step_back_count_ = 1;
    BreakpointSet breakpoints_;
};
