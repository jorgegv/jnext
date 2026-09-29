// jnext::script — load-time checks and name binding. See check.h.

#include "script/check.h"

#include <map>

#include "script/names.h"

namespace jnext {
namespace script {

using dbg::Access;
using dbg::CopperEventKind;
using dbg::DmaEventKind;
using dbg::EventKind;

// ---------------------------------------------------------------------------
// The per-kind payload table (§2.3 second table, §5.4)
// ---------------------------------------------------------------------------
//
// Each name is admitted only where the backend's `Event` actually carries it
// for EVERY event the scope can deliver (events.h and the drain,
// debugger_events.cpp `build_event`). That is stricter than a literal reading
// of §2.3 in two places, both recorded in dsl-frontend.md "WP1 as built":
// `IO_SRC`/`IO_DST` are admitted on `dma byte` only (the backend fills them on
// `Byte` alone — `Dma::latch_start_`/`latch_end_` set no I/O flags), and
// `LEN`/`DMA_MODE` on `dma start`/`end` only (the `Byte` payload has neither).

bool payload_legal(Builtin p, const PayloadScope& s) {
    if (s.tag == PayloadScope::Tag::None) return false;
    if (s.tag == PayloadScope::Tag::ScriptStop)
        return p == Builtin::P_REASON || p == Builtin::P_PC;
    if (p == Builtin::P_PC) return s.kind != EventKind::Count;  // "PC | all"

    switch (s.kind) {
        case EventKind::Execute:
            return p == Builtin::P_ADDR || p == Builtin::P_PAGE;
        case EventKind::Mem:
            if (p == Builtin::P_PREV) return s.access == Access::Write;
            return p == Builtin::P_ADDR || p == Builtin::P_VALUE || p == Builtin::P_PAGE ||
                   p == Builtin::P_SOURCE;
        case EventKind::Port:
            return p == Builtin::P_PORT || p == Builtin::P_VALUE || p == Builtin::P_SOURCE;
        case EventKind::NextRegWrite:
            return p == Builtin::P_REG || p == Builtin::P_VALUE || p == Builtin::P_PREV ||
                   p == Builtin::P_SOURCE;
        case EventKind::Host:
            return p == Builtin::P_KEY;
        case EventKind::Copper:
            if (p == Builtin::P_CPC || p == Builtin::P_HC_ULA || p == Builtin::P_CVC) return true;
            if (p == Builtin::P_REG || p == Builtin::P_VALUE)
                return s.copper && *s.copper == CopperEventKind::Move;
            if (p == Builtin::P_WAIT_V || p == Builtin::P_WAIT_H)
                return s.copper && *s.copper == CopperEventKind::Wait;
            return false;
        case EventKind::Dma:
            if (p == Builtin::P_SRC || p == Builtin::P_DST) return true;
            if (p == Builtin::P_LEN || p == Builtin::P_DMA_MODE)
                return s.dma && (*s.dma == DmaEventKind::Start || *s.dma == DmaEventKind::End);
            if (p == Builtin::P_VALUE || p == Builtin::P_IO_SRC || p == Builtin::P_IO_DST)
                return s.dma && *s.dma == DmaEventKind::Byte;
            return false;
        case EventKind::Frame:
        case EventKind::Scanline:
        case EventKind::Cycle:
        case EventKind::Reset:
        case EventKind::IntAck:
        case EventKind::Nmi:
        case EventKind::Magic:
        case EventKind::Count:
            return false;
    }
    return false;
}

std::string PayloadScope::describe() const {
    if (tag == Tag::None) return "no event";
    if (tag == Tag::ScriptStop) return "a `stop` event";
    switch (kind) {
        case EventKind::Execute: return "an `execute` event";
        case EventKind::Mem:
            return access == Access::Write ? "a `write` event"
                 : access == Access::Read  ? "a `read` event"
                                           : "a memory event";
        case EventKind::Port:
            return access == Access::Write ? "an `io_write` event"
                 : access == Access::Read  ? "an `io_read` event"
                                           : "a port event";
        case EventKind::NextRegWrite: return "a `nextreg` event";
        case EventKind::Frame:        return "a `frame` event";
        case EventKind::Scanline:     return "a `scanline` event";
        case EventKind::Cycle:        return "a `cycle` event";
        case EventKind::Reset:        return "a `reset` event";
        case EventKind::IntAck:       return "an `interrupt` event";
        case EventKind::Nmi:          return "an `nmi` event";
        case EventKind::Magic:        return "a magic-breakpoint event";
        case EventKind::Host:         return "a `hostkey` event";
        case EventKind::Copper:
            if (!copper) return "a `copper` event";
            return *copper == CopperEventKind::Move ? "a `copper move` event"
                 : *copper == CopperEventKind::Wait ? "a `copper wait` event"
                                                    : "a `copper halt` event";
        case EventKind::Dma:
            if (!dma) return "a `dma` event";
            return *dma == DmaEventKind::Start ? "a `dma start` event"
                 : *dma == DmaEventKind::Byte  ? "a `dma byte` event"
                                               : "a `dma end` event";
        case EventKind::Count: return "an invalid event kind";
    }
    return "an invalid event kind";
}

PayloadScope scope_of(const EventSpec& ev) {
    switch (ev.type) {
        case EventType::Execute:   return PayloadScope(EventKind::Execute);
        case EventType::Read:      return PayloadScope::mem(Access::Read);
        case EventType::Write:     return PayloadScope::mem(Access::Write);
        case EventType::IoRead:    return PayloadScope::port(Access::Read);
        case EventType::IoWrite:   return PayloadScope::port(Access::Write);
        case EventType::NextReg:   return PayloadScope(EventKind::NextRegWrite);
        case EventType::Frame:     return PayloadScope(EventKind::Frame);
        case EventType::Scanline:  return PayloadScope(EventKind::Scanline);
        case EventType::Cycle:     return PayloadScope(EventKind::Cycle);
        case EventType::Interrupt: return PayloadScope(EventKind::IntAck);
        case EventType::Nmi:       return PayloadScope(EventKind::Nmi);
        case EventType::Reset:     return PayloadScope(EventKind::Reset);
        case EventType::HostKey:   return PayloadScope(EventKind::Host);
        case EventType::Stop:      return PayloadScope::script_stop();
        case EventType::Copper:
            return PayloadScope::copper_sub(ev.copper == CopperSub::Move ? CopperEventKind::Move
                                            : ev.copper == CopperSub::Wait ? CopperEventKind::Wait
                                                                           : CopperEventKind::Halt);
        case EventType::Dma:
            return PayloadScope::dma_sub(ev.dma == DmaSub::Start ? DmaEventKind::Start
                                         : ev.dma == DmaSub::Byte ? DmaEventKind::Byte
                                                                  : DmaEventKind::End);
    }
    return PayloadScope{};
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

namespace {

// §2.5: the snapshot record is {regs, IFF1, IFF2, IM, SP, stack[0], mmu[0..7],
// FRAME, CYCLE, PC}. "regs" is the register set §2.3 names.
bool snapshot_field_known(const std::string& f) {
    static const char* const FIELDS[] = {
        "A", "B", "C", "D", "E", "H", "L", "F", "I", "R",
        "AF", "BC", "DE", "HL", "IX", "IY", "SP", "PC", "AF2", "BC2", "DE2", "HL2",
        "IFF1", "IFF2", "IM", "STACK0", "MMU", "FRAME", "CYCLE",
    };
    for (const char* x : FIELDS)
        if (f == x) return true;
    return false;
}

struct Binder {
    const PayloadScope&          scope;
    const std::set<std::string>* vars;
    const SymbolResolver&        symbols;
    bool                         require_symbols;
    std::vector<Diagnostic>&     errors;

    void err(SourcePos p, std::string m) { errors.push_back(Diagnostic{p, std::move(m)}); }

    void script_only(const Expr& e, const char* what) {
        err(e.pos, std::string(what) + " is only available inside a script");
    }

    void walk_string(StringLit& s) {
        for (StringPart& part : s.parts)
            if (part.expr) walk(*part.expr);
    }

    void walk(Expr& e) {
        switch (e.kind) {
            case ExprKind::Int:
                return;
            case ExprKind::Str:
                if (!vars) {
                    err(e.pos, "a string is not valid here: a condition is an integer expression");
                    return;
                }
                walk_string(e.str);
                return;
            case ExprKind::Name: {
                const auto payload = payload_builtin(e.text);
                const auto live    = live_builtin(e.text);
                if (payload && payload_legal(*payload, scope)) {
                    e.builtin = *payload;
                } else if (live) {
                    e.builtin = *live;
                } else {
                    err(e.pos, "`" + e.text + "` is event payload and is not available in " +
                                   scope.describe());
                }
                return;
            }
            case ExprKind::Var:
                if (!vars) {
                    err(e.pos, "unknown name `" + e.text + "`");
                } else if (!vars->count(e.text)) {
                    err(e.pos, "unknown variable `" + e.text + "` (declare it with `var`)");
                }
                return;
            case ExprKind::Symbol: {
                if (!symbols) {
                    if (require_symbols)
                        err(e.pos, "`@" + e.text + "`: no symbol table is available here");
                    return;
                }
                const auto v = symbols(e.text);
                if (!v) {
                    err(e.pos, "unknown symbol `@" + e.text + "` (not in any loaded MAP)");
                    return;
                }
                e.value    = *v;
                e.resolved = true;
                return;
            }
            case ExprKind::Unary:
                walk(*e.a);
                return;
            case ExprKind::Binary:
                walk(*e.a);
                walk(*e.b);
                return;
            case ExprKind::Mem: case ExprKind::Mem16: case ExprKind::NextReg:
            case ExprKind::Mmu: case ExprKind::Page: case ExprKind::Stack:
                walk(*e.a);
                return;
            case ExprKind::Phys:
                walk(*e.a);
                walk(*e.b);
                return;
            case ExprKind::SnapField:
                if (!vars) {
                    script_only(e, "a snapshot field");
                    return;
                }
                if (!snapshot_field_known(e.field)) {
                    err(e.pos, "unknown snapshot field `" + e.field + "`");
                } else if (e.field == "MMU" && !e.a) {
                    err(e.pos, "`MMU` needs a slot index: `" + e.text + ".MMU[n]`");
                } else if (e.field != "MMU" && e.a) {
                    err(e.pos, "snapshot field `" + e.field + "` takes no index");
                }
                if (e.a) walk(*e.a);
                return;
            case ExprKind::Changed:
                if (!vars) script_only(e, "`changed()`");
                return;
            case ExprKind::Depth:
                if (!vars) script_only(e, "`depth()`");
                return;
        }
    }
};

struct ScriptChecker {
    Script&                  s;
    const CheckOptions&      opts;
    std::vector<Diagnostic>  errors;
    std::set<std::string>    vars;
    std::map<std::string, SourcePos> labels;

    void err(SourcePos p, std::string m) { errors.push_back(Diagnostic{p, std::move(m)}); }

    void bind(ExprPtr& e, const PayloadScope& scope) {
        if (!e) return;
        Binder b{scope, &vars, opts.symbols, /*require_symbols=*/false, errors};
        b.walk(*e);
    }

    void bind_string(std::optional<StringLit>& lit, const PayloadScope& scope) {
        if (!lit) return;
        Binder b{scope, &vars, opts.symbols, false, errors};
        b.walk_string(*lit);
    }

    void check_target(ExprPtr& t, const PayloadScope& scope) {
        if (t->kind == ExprKind::Name) {
            // A `set` target is always the MACHINE register (or flag / IFF /
            // IM / AUDIO_MUTE) — never the payload name it may share a
            // spelling with (`set PC = …` in an event rule writes the CPU's PC).
            t->builtin = *live_builtin(t->text);
            return;
        }
        if (t->kind == ExprKind::Var) {
            if (!vars.count(t->text))
                err(t->pos, "unknown variable `" + t->text + "` (declare it with `var`)");
            return;
        }
        bind(t, scope);  // mem[] / mem16[] / phys[] / nextreg[]: bind the index
    }

    void check_actions(std::vector<Action>& body, const PayloadScope& scope) {
        for (Action& a : body) {
            bind(a.e1, scope);
            bind(a.e2, scope);
            bind_string(a.s1, scope);
            bind_string(a.s2, scope);
            if (a.target) check_target(a.target, scope);
            if ((a.kind == ActionKind::Enable || a.kind == ActionKind::Disable) &&
                !labels.count(a.name))
                err(a.name_pos, "`" + a.name + "` names no labelled rule");
            check_actions(a.then_body, scope);
            check_actions(a.else_body, scope);
        }
    }

    void run() {
        for (VarDecl& v : s.vars) {
            if (!vars.insert(v.name).second)
                err(v.pos, "variable `" + v.name + "` is already declared");
        }
        for (Rule& r : s.rules) {
            if (r.label.empty()) continue;
            auto it = labels.find(r.label);
            if (it != labels.end())
                err(r.label_pos, "rule label `" + r.label + "` is already used at line " +
                                     std::to_string(it->second.line));
            else
                labels.emplace(r.label, r.label_pos);
        }
        const PayloadScope none;
        for (VarDecl& v : s.vars) bind(v.init, none);
        for (Rule& r : s.rules) {
            EventSpec& ev = r.event;
            // Filter bounds are evaluated at registration: no event.
            bind(ev.lo, none);
            bind(ev.hi, none);
            bind(ev.page_lo, none);
            bind(ev.page_hi, none);
            bind(ev.mask, none);
            bind(ev.value, none);
            bind(ev.at_lo, none);
            bind(ev.at_hi, none);
            const PayloadScope scope = scope_of(ev);
            bind(r.when, scope);
            check_actions(r.body, scope);
        }
    }
};

}  // namespace

void bind_expr(Expr& e, const PayloadScope& scope, const std::set<std::string>* vars,
               const SymbolResolver& symbols, bool require_symbols,
               std::vector<Diagnostic>& errors) {
    Binder b{scope, vars, symbols, require_symbols, errors};
    b.walk(e);
}

std::vector<Diagnostic> check_script(Script& script, const CheckOptions& opts) {
    ScriptChecker c{script, opts, {}, {}, {}};
    c.run();
    return std::move(c.errors);
}

}  // namespace script
}  // namespace jnext
