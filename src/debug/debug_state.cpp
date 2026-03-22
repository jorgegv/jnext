#include "debug/debug_state.h"

void DebugState::pause() {
    paused_ = true;
    step_mode_ = StepMode::NONE;
}

void DebugState::resume() {
    paused_ = false;
    step_mode_ = StepMode::NONE;
    breakpoints_.clear_oneshot();
}

void DebugState::step_into() {
    paused_ = false;
    step_mode_ = StepMode::INTO;
}

void DebugState::step_over(uint16_t next_pc) {
    breakpoints_.set_oneshot(next_pc);
    paused_ = false;
    step_mode_ = StepMode::OVER;
}

void DebugState::step_out(uint16_t current_sp) {
    step_out_sp_ = current_sp;
    paused_ = false;
    step_mode_ = StepMode::OUT;
}

void DebugState::run_to(uint16_t addr) {
    breakpoints_.set_oneshot(addr);
    paused_ = false;
    step_mode_ = StepMode::NONE;
}

bool DebugState::should_break(uint16_t pc) const {
    // Check PC breakpoints.
    if (breakpoints_.has_pc(pc)) return true;

    // Check one-shot breakpoint.
    if (breakpoints_.has_oneshot() && breakpoints_.oneshot_addr() == pc) return true;

    return false;
}

bool DebugState::check_step_out(uint16_t sp, uint8_t opcode, uint8_t prev_opcode) const {
    if (step_mode_ != StepMode::OUT) return false;

    // RET = 0xC9
    if (opcode == 0xC9 && sp >= step_out_sp_) return true;

    // RETI = ED 4D, RETN = ED 45
    if (prev_opcode == 0xED && (opcode == 0x4D || opcode == 0x45) && sp >= step_out_sp_)
        return true;

    return false;
}
