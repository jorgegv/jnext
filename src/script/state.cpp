// jnext::script — variables and snapshot stacks. See state.h.

#include "script/state.h"

#include <cstdio>

#include "debug/debugger.h"
#include "script/evaluator.h"

namespace jnext {
namespace script {

Snapshot capture_snapshot(const dbg::Debugger& dbg) {
    Snapshot s;
    s.regs = dbg.registers();
    uint8_t w[2] = {0, 0};
    dbg.peek(dbg::MemSpace::cpu(), s.regs.SP, 2, w);  // Cpu peek wraps at 0xFFFF
    s.stack0 = static_cast<uint16_t>(w[0] | (w[1] << 8));
    const auto slots = dbg.mmu_slots();
    for (size_t k = 0; k < 8; ++k) s.mmu[k] = slots[k].nr_page;
    const dbg::Time t = dbg.time();
    s.frame = t.frame;
    s.cycle = t.master_cycle;
    return s;
}

namespace {

// The regs group of `changed()` (§2.5), in `dump_diff` order.
struct Reg16 {
    const char* name;
    uint16_t Z80Registers::*field;
};
const Reg16 REGS_GROUP[] = {
    {"AF", &Z80Registers::AF},   {"BC", &Z80Registers::BC},   {"DE", &Z80Registers::DE},
    {"HL", &Z80Registers::HL},   {"IX", &Z80Registers::IX},   {"IY", &Z80Registers::IY},
    {"AF2", &Z80Registers::AF2}, {"BC2", &Z80Registers::BC2}, {"DE2", &Z80Registers::DE2},
    {"HL2", &Z80Registers::HL2}, {"SP", &Z80Registers::SP},
};

std::string line(const char* name, unsigned a, unsigned b, int width) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s %0*X -> %0*X", name, width, a, width, b);
    return buf;
}

}  // namespace

bool snapshot_changed(const Snapshot& then, const Snapshot& now, SnapGroup g) {
    switch (g) {
        case SnapGroup::Regs:
            for (const Reg16& r : REGS_GROUP)
                if (then.regs.*r.field != now.regs.*r.field) return true;
            return false;
        case SnapGroup::Mmu:
            return then.mmu != now.mmu;
        case SnapGroup::Iff1:
            return (then.regs.IFF1 != 0) != (now.regs.IFF1 != 0);
        case SnapGroup::Stack0:
            return then.stack0 != now.stack0;
    }
    return false;
}

std::vector<std::string> snapshot_diff(const Snapshot& then, const Snapshot& now) {
    std::vector<std::string> out;
    for (const Reg16& r : REGS_GROUP)
        if (then.regs.*r.field != now.regs.*r.field)
            out.push_back(line(r.name, then.regs.*r.field, now.regs.*r.field, 4));
    if ((then.regs.IFF1 != 0) != (now.regs.IFF1 != 0))
        out.push_back(line("IFF1", then.regs.IFF1 != 0, now.regs.IFF1 != 0, 1));
    if ((then.regs.IFF2 != 0) != (now.regs.IFF2 != 0))
        out.push_back(line("IFF2", then.regs.IFF2 != 0, now.regs.IFF2 != 0, 1));
    if (then.regs.IM != now.regs.IM) out.push_back(line("IM", then.regs.IM, now.regs.IM, 1));
    if (then.stack0 != now.stack0) out.push_back(line("STACK0", then.stack0, now.stack0, 4));
    for (size_t k = 0; k < 8; ++k) {
        if (then.mmu[k] != now.mmu[k]) {
            const std::string name = "MMU" + std::to_string(k);
            out.push_back(line(name.c_str(), then.mmu[k], now.mmu[k], 2));
        }
    }
    return out;
}

ScriptState::ScriptState(const Script& script)
    : vars_(script.vars.size(), 0),
      stack_names_(script.snapshots),
      stacks_(script.snapshots.size()) {
    for (const VarDecl& v : script.vars) var_names_.push_back(v.name);
}

namespace {
[[noreturn]] void internal(const std::string& what) {
    throw EvalError{Diagnostic{SourcePos{}, "internal: " + what}};
}
}  // namespace

int32_t ScriptState::var(int slot) const {
    if (slot < 0 || static_cast<size_t>(slot) >= vars_.size()) internal("bad variable slot");
    return vars_[static_cast<size_t>(slot)];
}

void ScriptState::set_var(int slot, int32_t v) {
    if (slot < 0 || static_cast<size_t>(slot) >= vars_.size()) internal("bad variable slot");
    vars_[static_cast<size_t>(slot)] = v;
}

const std::string& ScriptState::var_name(int slot) const {
    if (slot < 0 || static_cast<size_t>(slot) >= var_names_.size()) internal("bad variable slot");
    return var_names_[static_cast<size_t>(slot)];
}

const std::string& ScriptState::stack_name(int slot) const {
    if (slot < 0 || static_cast<size_t>(slot) >= stack_names_.size()) internal("bad snapshot slot");
    return stack_names_[static_cast<size_t>(slot)];
}

size_t ScriptState::depth(int slot) const {
    if (slot < 0 || static_cast<size_t>(slot) >= stacks_.size()) internal("bad snapshot slot");
    return stacks_[static_cast<size_t>(slot)].size();
}

const Snapshot& ScriptState::top(int slot, SourcePos at) const {
    if (depth(slot) == 0)
        throw EvalError{Diagnostic{at, "snapshot stack `" + stack_name(slot) + "` is empty"}};
    return stacks_[static_cast<size_t>(slot)].back();
}

void ScriptState::snap(int slot, const dbg::Debugger& dbg, SourcePos at) {
    if (depth(slot) >= MAX_SNAPSHOT_DEPTH)
        throw EvalError{Diagnostic{at, "snapshot stack `" + stack_name(slot) + "` is full (" +
                                           std::to_string(MAX_SNAPSHOT_DEPTH) + " entries)"}};
    stacks_[static_cast<size_t>(slot)].push_back(capture_snapshot(dbg));
}

void ScriptState::unsnap(int slot, SourcePos at) {
    if (depth(slot) == 0)
        throw EvalError{Diagnostic{at, "`unsnap`: snapshot stack `" + stack_name(slot) + "` is empty"}};
    stacks_[static_cast<size_t>(slot)].pop_back();
}

bool ScriptState::changed(int slot, SnapGroup g, const dbg::Debugger& dbg, SourcePos at) const {
    const Snapshot& t = top(slot, at);
    return snapshot_changed(t, capture_snapshot(dbg), g);
}

std::vector<std::string> ScriptState::diff(int slot, const dbg::Debugger& dbg, SourcePos at) const {
    const Snapshot& t = top(slot, at);
    return snapshot_diff(t, capture_snapshot(dbg));
}

}  // namespace script
}  // namespace jnext
