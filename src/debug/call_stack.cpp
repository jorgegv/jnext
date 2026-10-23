#include "debug/call_stack.h"

void CallStack::push_frame(uint16_t caller, uint16_t target, uint16_t sp, CallType type,
                           jnext::dbg::MemSpace caller_space) {
    if (frames_.size() >= MAX_DEPTH)
        frames_.erase(frames_.begin());  // drop oldest if too deep
    // The target is pending (cpu()) until the first fetch at it.
    frames_.push_back({caller, target, sp, type, caller_space, jnext::dbg::MemSpace::cpu()});
}

void CallStack::pop_frames_to_sp(uint16_t sp) {
    // RET pops 2 bytes, so SP increases. A frame's return address sits at
    // sp_at_call..sp_at_call+1, so the frames returned from are the ones whose
    // sp_at_call is strictly BELOW the new SP. A frame AT the new SP still has
    // its return address on the stack. GH #278: `<=` popped it — which is
    // exactly the frame an interrupt routine's RET returns into when the
    // interrupted routine had pushed nothing since its CALL, so every ISR
    // emptied the call stack of the routine it interrupted.
    while (!frames_.empty() && frames_.back().sp_at_call < sp) {
        frames_.pop_back();
    }
}

void CallStack::on_instruction_pre(uint16_t pc, uint16_t sp, uint8_t opcode, uint8_t op2, uint8_t op3,
                                   jnext::dbg::MemSpace pc_space) {
    if (!enabled_) return;
    pre_pc_ = pc;
    pre_space_ = pc_space;
    pre_sp_ = sp;
    pre_opcode_ = opcode;
    pre_op2_ = op2;
    pre_op3_ = op3;
    have_pre_ = true;
}

void CallStack::on_instruction_post(uint16_t new_sp, uint16_t new_pc,
                                    jnext::dbg::MemSpace fetched) {
    if (!enabled_ || !have_pre_) return;
    have_pre_ = false;

    // The newest frame's target is recorded by the first fetch AT it, before
    // this instruction can open a frame of its own (a target that starts with
    // a CALL fills the outer frame first). A fetch elsewhere (the debugger
    // moved PC, the esxdos shim serviced the call) leaves it unrecorded.
    if (fetched != jnext::dbg::MemSpace::cpu() && !frames_.empty() &&
        frames_.back().target_space == jnext::dbg::MemSpace::cpu() &&
        frames_.back().target_pc == pre_pc_)
        frames_.back().target_space = fetched;

    // Detect taken CALLs/RSTs: SP decreased by 2 (return address was pushed)
    if (new_sp == pre_sp_ - 2) {
        uint8_t op = pre_opcode_;

        // CALL nn (unconditional + conditional)
        if (op == 0xCD || op == 0xC4 || op == 0xCC || op == 0xD4 || op == 0xDC ||
            op == 0xE4 || op == 0xEC || op == 0xF4 || op == 0xFC) {
            uint16_t target = pre_op2_ | (pre_op3_ << 8);
            push_frame(pre_pc_, target, new_sp, CallType::CALL, fetched);
            return;
        }

        // RST instructions
        switch (op) {
            case 0xC7: push_frame(pre_pc_, 0x00, new_sp, CallType::RST, fetched); return;
            case 0xCF: push_frame(pre_pc_, 0x08, new_sp, CallType::RST, fetched); return;
            case 0xD7: push_frame(pre_pc_, 0x10, new_sp, CallType::RST, fetched); return;
            case 0xDF: push_frame(pre_pc_, 0x18, new_sp, CallType::RST, fetched); return;
            case 0xE7: push_frame(pre_pc_, 0x20, new_sp, CallType::RST, fetched); return;
            case 0xEF: push_frame(pre_pc_, 0x28, new_sp, CallType::RST, fetched); return;
            case 0xF7: push_frame(pre_pc_, 0x30, new_sp, CallType::RST, fetched); return;
            case 0xFF: push_frame(pre_pc_, 0x38, new_sp, CallType::RST, fetched); return;
            default: break;
        }
    }

    // Detect taken RETs: SP increased by 2 (return address was popped)
    if (new_sp == pre_sp_ + 2) {
        uint8_t op = pre_opcode_;

        // RET (unconditional + conditional)
        if (op == 0xC9 || op == 0xC0 || op == 0xC8 || op == 0xD0 || op == 0xD8 ||
            op == 0xE0 || op == 0xE8 || op == 0xF0 || op == 0xF8) {
            pop_frames_to_sp(new_sp);
            return;
        }

        // RETI (ED 4D) / RETN (ED 45)
        if (op == 0xED && (pre_op2_ == 0x4D || pre_op2_ == 0x45)) {
            pop_frames_to_sp(new_sp);
            return;
        }
    }
}

void CallStack::on_interrupt(uint16_t caller_pc, uint16_t target_pc, uint16_t new_sp,
                             CallType type) {
    if (!enabled_) return;
    // The pre-slot capture was taken at the interrupted PC, so its predicted
    // source is the caller's even though the opcode it read did not run (the
    // INT acknowledge fetches nothing, t80na.vhd:278-279). With none (the
    // tracker was enabled after the slot began) it is not known: cpu().
    const jnext::dbg::MemSpace caller_space = have_pre_ ? pre_space_ : jnext::dbg::MemSpace::cpu();
    have_pre_ = false;   // the captured opcode did not run in this slot
    push_frame(caller_pc, target_pc, new_sp, type, caller_space);
}
