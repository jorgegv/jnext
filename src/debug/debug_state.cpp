#include "debug/debug_state.h"

void DebugState::pause() {
    paused_ = true;
    step_mode_ = StepMode::NONE;
}

void DebugState::resume() {
    paused_ = false;
    step_mode_ = StepMode::NONE;
    data_bp_hit_ = false;
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

void DebugState::run_to_cycle(uint64_t target_cycle) {
    target_cycle_ = target_cycle;
    paused_ = false;
    step_mode_ = StepMode::RUN_TO_CYCLE;
}

bool DebugState::should_break(uint16_t pc) const {
    // Check PC breakpoints.
    if (breakpoints_.has_pc(pc)) return true;

    // Check one-shot breakpoint.
    if (breakpoints_.has_oneshot() && breakpoints_.oneshot_addr() == pc) return true;

    return false;
}

void DebugState::step_back(int n) {
    step_back_count_ = (n > 0) ? n : 1;
    paused_ = false;
    step_mode_ = StepMode::STEP_BACK;
}

void DebugState::run_back_to_cycle(uint64_t target_cycle) {
    target_cycle_ = target_cycle;
    paused_ = false;
    step_mode_ = StepMode::RUN_BACK_TO_CYCLE;
}

bool DebugState::step_out_sp_qualifies(uint16_t sp_before, uint16_t sp_after) const {
    if (step_mode_ != StepMode::OUT) return false;

    // 1. The instruction must have POPPED exactly one return address. This is
    //    the taken/untaken discriminator for `RET cc` (an untaken one leaves SP
    //    alone), and it makes every step that executes no instruction at all
    //    inert here — a DMA burst, a G156 boot hold or a parked CPU leaves SP
    //    unchanged, and an accepted NMI/INT moves it DOWN by two.
    if (sp_after != static_cast<uint16_t>(sp_before + 2)) return false;

    // 3. STRICTLY past the arming point. `step_out_sp_` is SP at the moment F8
    //    was pressed, so the subroutine we are in returns by popping AT it
    //    (sp_after == step_out_sp_ + 2 at least). A nested CALL made after
    //    arming pushes below it, so the nested RET lands exactly ON it —
    //    `>=` would end the step one frame too shallow, which is the case the
    //    old dead predicate got wrong. An interrupt accepted while stepping
    //    out is the same shape: its RETI/RETN returns INTO this routine, not
    //    out of it, and is correctly ignored.
    //
    //    16-bit wrap is not modelled: a stack that wraps through 0x0000 while
    //    stepping out simply never satisfies this, and the step keeps running
    //    (the documented "subroutine that never returns" outcome).
    return sp_after > step_out_sp_;
}

bool DebugState::check_step_out(uint16_t sp_before, uint16_t sp_after,
                                uint8_t opcode, uint8_t opcode2) const {
    // Conditions 1 and 3 — see step_out_sp_qualifies(). The caller has usually
    // evaluated this already (it gates the opcode read); repeating it keeps
    // the predicate correct on its own terms rather than trusting the caller.
    if (!step_out_sp_qualifies(sp_before, sp_after)) return false;

    // Two returns are deliberately NOT matched, for the same reason — covering
    // them costs more than the case is worth, and each degrades to "the step
    // keeps running", never to a wrong stop:
    //   * a return reached through a DD/FD prefix chain (`DD C9` is a legal, if
    //     pointless, RET). `opcode` is the byte at PC, so for those it is the
    //     prefix; matching would mean walking the chain, which Z80Cpu already
    //     does once per instruction for its own M1 callbacks.
    //   * a stackless-NMI RETN (NR 0xC0 bit 3, z80_cpu.cpp stackless_retn_):
    //     the return address was never pushed, so SP does not move and there is
    //     no stack depth for ANY depth-based rule to compare against.

    // 2. ...and it must have been a return instruction, not a bare `POP rr`
    //    (which moves SP identically). Same opcode set CallStack matches on
    //    (src/debug/call_stack.cpp:57-72), extended to the six undocumented
    //    RETN aliases:
    //      C9                     RET
    //      11ccc000               RET cc  — C0 C8 D0 D8 E0 E8 F0 F8
    //      ED 01xxx101            RETI (ED 4D) / RETN (ED 45) and the
    //                             undocumented ED 55/5D/65/6D/75/7D aliases,
    //                             which the Z80 decodes as RETN too.
    const bool is_return =
        opcode == 0xC9 ||
        (opcode & 0xC7) == 0xC0 ||
        (opcode == 0xED && (opcode2 & 0xC7) == 0x45);
    return is_return;
}
