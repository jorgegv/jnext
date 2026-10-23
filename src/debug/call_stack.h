#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "debug/mem_space.h"

/// Type of call that created a call stack entry.
enum class CallType : uint8_t {
    CALL,   // CALL nn / CALL cc,nn
    RST,    // RST nn
    INT,    // Interrupt (IM1/IM2)
    NMI,    // Non-maskable interrupt
};

/// A single entry on the call stack tracker.
struct CallFrame {
    uint16_t caller_pc;    // PC of the CALL/RST/INT instruction
    uint16_t target_pc;    // Target address jumped to
    uint16_t sp_at_call;   // SP value after push (return address on stack)
    CallType type;
    // GH #340 (E23-08): WHAT supplied each address's instruction (the read
    // arbiter's answer at the M1), `MemSpace::cpu()` = none recorded.
    //  * caller_space: the CALL / RST's own recorded fetch; for an INT / NMI
    //    frame the prediction at the interrupted PC (nothing was fetched there:
    //    the interrupted instruction runs after the return).
    //  * target_space: filled at the first fetch AT target_pc, so it stays
    //    cpu() until that instruction runs (and if none ever does).
    jnext::dbg::MemSpace caller_space = jnext::dbg::MemSpace::cpu();
    jnext::dbg::MemSpace target_space = jnext::dbg::MemSpace::cpu();
};

/// Tracks CALL/RST/INT/RET to maintain a virtual call stack.
/// Not a real CPU stack — this is a debug-only shadow structure.
/// Call on_instruction_pre() before and on_instruction_post() after each
/// instruction. The post-call compares SP to detect taken calls/rets.
class CallStack {
public:
    /// Call before instruction execution to capture opcode and pre-SP.
    /// `pc_space` is the PREDICTED source of PC's fetch: used only as the
    /// caller of an INT / NMI frame taken in this slot (which records no fetch).
    /// Left out, it is cpu() = unknown.
    void on_instruction_pre(uint16_t pc, uint16_t sp, uint8_t opcode, uint8_t op2, uint8_t op3,
                            jnext::dbg::MemSpace pc_space = jnext::dbg::MemSpace::cpu());

    /// Call after instruction execution with the new SP.
    /// Compares with pre-SP to determine if a CALL/RET was actually taken.
    /// `fetched` is this instruction's RECORDED first fetch (`Mmu::last_m1()`),
    /// or cpu() when the slot fetched nothing: it is the caller of a frame this
    /// instruction opens, and fills the newest frame's pending target when the
    /// instruction ran at that target.
    void on_instruction_post(uint16_t new_sp, uint16_t new_pc,
                             jnext::dbg::MemSpace fetched = jnext::dbg::MemSpace::cpu());

    /// Notify of an interrupt being taken (hardware push of PC), IN PLACE of
    /// on_instruction_post() for that slot: an accepted INT or NMI runs no
    /// opcode fetch of the interrupted instruction, so the pre-slot capture describes an instruction that did not
    /// run and is discarded. `type` is CallType::INT or CallType::NMI.
    /// The frame's target is recorded at the ISR's first fetch.
    void on_interrupt(uint16_t caller_pc, uint16_t target_pc, uint16_t new_sp,
                      CallType type = CallType::INT);

    /// Get the current call stack (OLDEST first: frames().back() is the newest).
    const std::vector<CallFrame>& frames() const { return frames_; }

    void clear() { frames_.clear(); have_pre_ = false; }

    /// Replace the frames with ones recorded earlier — a rewind restoring the
    /// tracker to the moment its snapshot was taken. Any half-observed
    /// instruction is dropped: it belongs to the history being left.
    void restore_frames(const std::vector<CallFrame>& frames) {
        frames_ = frames;
        have_pre_ = false;
    }

    bool enabled() const { return enabled_; }
    /// A change of state empties the tracker: while it is off the hooks do not
    /// run, so the frames it holds describe a history it stopped observing.
    void set_enabled(bool e) {
        if (e != enabled_) clear();
        enabled_ = e;
    }

    static constexpr size_t MAX_DEPTH = 256;

private:
    void push_frame(uint16_t caller, uint16_t target, uint16_t sp, CallType type,
                    jnext::dbg::MemSpace caller_space);
    void pop_frames_to_sp(uint16_t sp);

    std::vector<CallFrame> frames_;
    bool enabled_ = false;

    // Pre-execution state captured by on_instruction_pre()
    uint16_t pre_pc_ = 0;
    jnext::dbg::MemSpace pre_space_ = jnext::dbg::MemSpace::cpu();
    uint16_t pre_sp_ = 0;
    uint8_t pre_opcode_ = 0;
    uint8_t pre_op2_ = 0;
    uint8_t pre_op3_ = 0;
    bool have_pre_ = false;
};
