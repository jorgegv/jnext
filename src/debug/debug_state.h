#pragma once

#include "debug/breakpoints.h"
#include <cstdint>

enum class StepMode { NONE, INTO, OVER, OUT };

/// Manages the debugger's execution control state: pause/resume, step modes,
/// and breakpoint checking.  Pure C++ — no GUI dependency.
class DebugState {
public:
    /// Is the debugger active (has been activated by the UI)?
    bool active() const { return active_; }
    void set_active(bool a) { active_ = a; }

    bool paused() const { return paused_; }
    void pause();
    void resume();

    // Step modes.
    void step_into();
    void step_over(uint16_t next_pc);
    void step_out(uint16_t current_sp);
    void run_to(uint16_t addr);

    /// Called before each CPU instruction in the hot loop.
    /// Returns true if execution should break (pause).
    bool should_break(uint16_t pc) const;

    /// Called after a step-out instruction completes to check RET condition.
    /// Returns true if the step-out is satisfied (should re-pause).
    bool check_step_out(uint16_t sp, uint8_t opcode, uint8_t prev_opcode) const;

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
    bool active_ = false;
    bool paused_ = false;
    bool data_bp_hit_ = false;
    uint16_t data_bp_addr_ = 0;
    StepMode step_mode_ = StepMode::NONE;
    uint16_t step_out_sp_ = 0;
    BreakpointSet breakpoints_;
};
