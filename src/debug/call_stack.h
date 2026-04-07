#pragma once

#include <cstdint>
#include <vector>

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
};

/// Tracks CALL/RST/INT/RET to maintain a virtual call stack.
/// Not a real CPU stack — this is a debug-only shadow structure.
/// Call on_instruction_pre() before and on_instruction_post() after each
/// instruction. The post-call compares SP to detect taken calls/rets.
class CallStack {
public:
    /// Call before instruction execution to capture opcode and pre-SP.
    void on_instruction_pre(uint16_t pc, uint16_t sp, uint8_t opcode, uint8_t op2, uint8_t op3);

    /// Call after instruction execution with the new SP.
    /// Compares with pre-SP to determine if a CALL/RET was actually taken.
    void on_instruction_post(uint16_t new_sp, uint16_t new_pc);

    /// Notify of an interrupt being taken (hardware push of PC).
    void on_interrupt(uint16_t caller_pc, uint16_t target_pc, uint16_t new_sp);

    /// Get the current call stack (most recent first).
    const std::vector<CallFrame>& frames() const { return frames_; }

    void clear() { frames_.clear(); }

    bool enabled() const { return enabled_; }
    void set_enabled(bool e) { enabled_ = e; }

    static constexpr size_t MAX_DEPTH = 256;

private:
    void push_frame(uint16_t caller, uint16_t target, uint16_t sp, CallType type);
    void pop_frames_to_sp(uint16_t sp);

    std::vector<CallFrame> frames_;
    bool enabled_ = false;

    // Pre-execution state captured by on_instruction_pre()
    uint16_t pre_pc_ = 0;
    uint16_t pre_sp_ = 0;
    uint8_t pre_opcode_ = 0;
    uint8_t pre_op2_ = 0;
    uint8_t pre_op3_ = 0;
    bool have_pre_ = false;
};
