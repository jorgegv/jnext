#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the interpreter state a script's expressions read: its
// variables and its snapshot stacks (dsl-frontend.md §2.5).
//
// A SNAPSHOT is the fixed record §2.5 names — the registers (with IFF1, IFF2,
// IM, SP, PC), the word at SP, the eight MMU slots (as `mmu[s]` reads them,
// NR 0x50+s), FRAME and CYCLE — captured through the backend's side-effect-free
// inspection surface. Each snapshot NAME is a stack, so nested entries pair
// with their own exits (#279(c), ChaseTheBug's FunctionStackEntry).
//
// BOUNDED, AND LOUD AT BOTH ENDS. A stack holds at most MAX_SNAPSHOT_DEPTH
// entries. `snap` on a full stack, and `unsnap` / a field / `changed()` /
// `dump_diff` on an empty one, are RUN-TIME ERRORS (§6.5): they throw
// `EvalError` at the action's or expression's position, the stack is left as
// it was, and the engine disables the rule. Silently dropping the oldest entry
// would make every later exit compare against the wrong entry — a silently
// wrong script, which §6.5 ranks below a stopped one. `depth()` of an empty
// stack is 0, not an error: it is how a script asks.
// ---------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cpu/z80_cpu.h"
#include "script/ast.h"
#include "script/diagnostic.h"

namespace jnext {
namespace dbg {
class Debugger;
}

namespace script {

/// One §2.5 snapshot record.
struct Snapshot {
    Z80Registers           regs{};    ///< AF..HL2 IX IY SP PC I R IFF1 IFF2 IM
    uint16_t               stack0 = 0;  ///< the word at SP
    std::array<uint8_t, 8> mmu{};       ///< `mmu[0..7]` — NR 0x50..0x57 as read
    uint32_t               frame = 0;   ///< FRAME
    uint64_t               cycle = 0;   ///< CYCLE (64-bit here; read wraps to 32)
};

/// Capture the machine now, through the inspection surface only.
Snapshot capture_snapshot(const dbg::Debugger& dbg);

/// `changed(NAME, group)` against a captured entry (§2.5):
///   regs   — any of AF BC DE HL IX IY AF2 BC2 DE2 HL2 SP differs;
///   mmu    — any of the 8 slots differs;
///   iff1   — IFF1 differs;
///   stack0 — the word at the CURRENT SP differs from the captured word.
bool snapshot_changed(const Snapshot& then, const Snapshot& now, SnapGroup g);

/// `dump_diff`: one line per field that differs, `NAME old -> new`, in the
/// order AF BC DE HL IX IY AF2 BC2 DE2 HL2 SP IFF1 IFF2 IM STACK0 MMU0..MMU7.
/// PC, I, R, FRAME and CYCLE are not compared: between an entry and its exit
/// they always differ, so they would bury the one line that matters.
std::vector<std::string> snapshot_diff(const Snapshot& then, const Snapshot& now);

class ScriptState {
public:
    /// Entries one snapshot stack holds (see the banner).
    static constexpr size_t MAX_SNAPSHOT_DEPTH = 4096;

    /// Variables zero, stacks empty, sized from a CHECKED script (its
    /// `vars` and `snapshots`).
    explicit ScriptState(const Script& script);

    size_t var_count() const { return vars_.size(); }
    int32_t var(int slot) const;
    void    set_var(int slot, int32_t v);
    const std::string& var_name(int slot) const;

    size_t stack_count() const { return stacks_.size(); }
    const std::string& stack_name(int slot) const;
    size_t depth(int slot) const;
    /// The top entry. Throws `EvalError` at `at` when the stack is empty.
    const Snapshot& top(int slot, SourcePos at) const;
    /// `snap NAME`: push a capture. Throws at `at` when the stack is full.
    void snap(int slot, const dbg::Debugger& dbg, SourcePos at);
    /// `unsnap NAME`: pop. Throws at `at` when the stack is empty.
    void unsnap(int slot, SourcePos at);
    /// `changed(NAME, group)`. Throws at `at` when the stack is empty.
    bool changed(int slot, SnapGroup g, const dbg::Debugger& dbg, SourcePos at) const;
    /// `dump_diff NAME`. Throws at `at` when the stack is empty.
    std::vector<std::string> diff(int slot, const dbg::Debugger& dbg, SourcePos at) const;

private:
    std::vector<std::string>           var_names_;
    std::vector<int32_t>               vars_;
    std::vector<std::string>           stack_names_;
    std::vector<std::vector<Snapshot>> stacks_;
};

}  // namespace script
}  // namespace jnext
