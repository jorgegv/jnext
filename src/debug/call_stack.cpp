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

// GH #345 (E23-14): which frame a taken RET ends. SP alone is wrong for a
// routine that moves its own return address: l_debug_push_frame (every sccz80
// -debug function) re-pushes it 2 bytes LOWER, so the frame survives the RET
// by the SP rule; a __z88dk_callee routine moves it UP, so the RET of a call
// it makes (or of an ISR that interrupted it) pops its frame too. So a RET
// that lands on the NEWEST frame's own return point ends exactly that frame:
//  * CALL: caller_pc + 3 (CALL nn is 3 bytes);
//  * INT / NMI: caller_pc is the interrupted PC and the pushed address is that
//    PC, or PC + 1 when the interrupt ended a HALT (the CPU had stepped past it).
// RST frames (whose inline DEFW operands make the return point vary) and any
// other RET keep the SP rule. The price: a routine that drops its return
// address and leaves by JP (banked_call looks the same) keeps its frame until
// a RET ends it by SP.
void CallStack::return_to(uint16_t sp, uint16_t pc) {
    if (!frames_.empty()) {
        const CallFrame& f = frames_.back();
        const bool lands =
            (f.type == CallType::CALL && static_cast<uint16_t>(f.caller_pc + 3) == pc) ||
            ((f.type == CallType::INT || f.type == CallType::NMI) &&
             (pc == f.caller_pc || pc == static_cast<uint16_t>(f.caller_pc + 1)));
        if (lands) {
            frames_.pop_back();
            return;
        }
    }
    pop_frames_to_sp(sp);
}

uint16_t CallStack::peek16_(uint16_t addr) const {
    return static_cast<uint16_t>(peek_(addr) | (peek_(static_cast<uint16_t>(addr + 1)) << 8));
}

// GH #349 R1'': after a PUSH or EX (SP),r the newest CALL frame may have stored its OWN
// return address (caller_pc + 3) above its slot: a __z88dk_callee prologue (POP r; POP args;
// PUSH r). Record where; the latest store wins. Without a reader nothing is recorded.
void CallStack::note_stack_store_(uint16_t new_sp) {
    if (!peek_ || frames_.empty()) return;
    CallFrame& f = frames_.back();
    if (f.type != CallType::CALL || new_sp <= f.sp_at_call) return;
    if (peek16_(new_sp) != static_cast<uint16_t>(f.caller_pc + 3)) return;
    f.ret_stored = true;
    f.ret_stored_at = new_sp;
}

// The stored copy is live while it sits at or above SP and still holds the return address.
bool CallStack::ret_copy_live_(const CallFrame& f, uint16_t sp) const {
    return peek_ && f.ret_stored && f.ret_stored_at >= sp &&
           peek16_(f.ret_stored_at) == static_cast<uint16_t>(f.caller_pc + 3);
}

// GH #349 (R1, R1', R1''): a routine that returns without a RET. A JP (HL) / JP (IX) /
// JP (IY) (the jump moves no SP) ends the NEWEST frame, when it is a CALL frame, if:
//  * A. it lands on the frame's own return point (caller_pc + 3), whatever SP is. sccz80's
//    `l_gintspsp` family swaps a value into the slot (`EX (SP),HL; JP (HL)`, SP at or
//    below the slot); `l_long_sub/and/or/xor/eq/ne` `POP IX`, pop their arguments and
//    `JP (IX)` (SP above the slot). Nothing but a return jumps to the return point.
//  * B. SP is above the frame's slot and the routine has no live stored copy of its return
//    address. `l_case`, `l_long_case`, `l_i64_case` `POP HL` the "return address" as the inline
//    case table and `JP (HL)` into the caller, storing nothing back: the routine has left.
// Then the older frames whose slot is below SP end by the SP rule.
// Negatives, kept: a __z88dk_callee body (`bit_beep_raw_callee`: POP AF; POP HL; POP DE;
// PUSH AF; JP asm_bit_beep_raw, which loops by JP (IX)) has SP above its slot but PUSHed its
// return address back, which note_stack_store_() recorded and which is still at or above SP;
// the classic retrofit callee shims jump with SP AT the slot; `banked_call` pops its return
// address but enters the target with `CALL l_jphl`, and that JP (HL) is in l_jphl's own frame at
// its slot. Recursion (deeper calls are newer frames) and an interrupt routine (its INT frame is
// newest) cannot close an outer frame.
// Residual, stated: a routine that moves its return address above its slot by anything but
// PUSH / EX (SP) (LD (IX+n),r, LDIR) or holds it in a register, then jumps internally with SP
// above the slot, loses its frame early; a routine that drops its return address with SP left
// at or below its slot and jumps elsewhere keeps a stale frame until a RET ends it by SP; so does
// one that pushes a value equal to caller_pc + 3 that is not its return address.
void CallStack::indirect_jump_to(uint16_t sp, uint16_t pc) {
    if (frames_.empty()) return;
    const CallFrame& f = frames_.back();
    if (f.type != CallType::CALL) return;
    const bool to_return_point = static_cast<uint16_t>(f.caller_pc + 3) == pc;
    const bool slot_consumed   = sp > f.sp_at_call && !ret_copy_live_(f, sp);
    if (!to_return_point && !slot_consumed) return;
    frames_.pop_back();
    pop_frames_to_sp(sp);
}

// The esxDOS shim returns from the RST $08 it serviced: that frame ends, and
// only that one. newlib's sccz80 esxDOS callees move their return slot up and
// `jp` to the RST, so the SP rule would end the callee's live CALL frame too.
void CallStack::on_slot_without_fetch(uint16_t new_sp) {
    if (!enabled_ || !have_pre_) return;
    have_pre_ = false;   // the captured opcode did not run in this slot
    if (!frames_.empty() && frames_.back().type == CallType::RST &&
        frames_.back().target_pc == 0x0008) {
        frames_.pop_back();
        return;
    }
    pop_frames_to_sp(new_sp);
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

        // PUSH qq / IX / IY / Z80N PUSH nn: the routine may be storing its return address back.
        if (op == 0xC5 || op == 0xD5 || op == 0xE5 || op == 0xF5 ||
            ((op == 0xDD || op == 0xFD) && pre_op2_ == 0xE5) || (op == 0xED && pre_op2_ == 0x8A))
            note_stack_store_(new_sp);
    }

    // Detect taken RETs: SP increased by 2 (return address was popped)
    if (new_sp == pre_sp_ + 2) {
        uint8_t op = pre_opcode_;

        // RET (unconditional + conditional)
        if (op == 0xC9 || op == 0xC0 || op == 0xC8 || op == 0xD0 || op == 0xD8 ||
            op == 0xE0 || op == 0xE8 || op == 0xF0 || op == 0xF8) {
            return_to(new_sp, new_pc);
            return;
        }

        // RETI (ED 4D) / RETN (ED 45)
        if (op == 0xED && (pre_op2_ == 0x4D || pre_op2_ == 0x45)) {
            return_to(new_sp, new_pc);
            return;
        }
    }

    // EX (SP),HL / IX / IY: the same store, SP unchanged (GH #349 R1'').
    if (new_sp == pre_sp_ &&
        (pre_opcode_ == 0xE3 || ((pre_opcode_ == 0xDD || pre_opcode_ == 0xFD) && pre_op2_ == 0xE3)))
        note_stack_store_(new_sp);

    // JP (HL), JP (IX), JP (IY): a return by jump (GH #349 R1).
    if (new_sp == pre_sp_ &&
        (pre_opcode_ == 0xE9 || ((pre_opcode_ == 0xDD || pre_opcode_ == 0xFD) && pre_op2_ == 0xE9)))
        indirect_jump_to(new_sp, new_pc);
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
