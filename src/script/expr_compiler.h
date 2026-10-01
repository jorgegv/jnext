#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the DSL's expression compiler, as a LIBRARY.
//
// dsl-frontend.md §5.4: `src/script/` exports two entry points other frontends
// may call without a script context. The ZRCP adapter (package Z) token-
// translates ZEsarUX's dialect into this grammar and calls them instead of
// owning a second parser; the script engine (WP3) compiles every `when` clause
// through the same code. THIS HEADER IS THE STABLE SURFACE — it names no AST
// type, and nothing a caller needs lives anywhere else.
//
//   compile_expr(text, scope)  -> a CAP-EVT predicate
//                                 (`dbg::Condition`, i.e.
//                                 std::function<bool(const Event&, const Debugger&)>)
//   eval_expr(text, debugger)  -> int32, one-shot, over the inspection surface
//
// THE LANGUAGE is §2.1's `expr`: integers (32-bit signed, wrapping), `true` /
// `false`, `CPU` / `DMA` / `COPPER`, the upper-case state names of §2.3
// (registers, flags, IFF1/IFF2/IM/HALTED, FRAME/CYCLE/TFRAME, the VHDL raster
// counters, AUDIO_MUTE, MACHINE), `mem[a]`, `mem16[a]`, `phys[page, off]`,
// `nextreg[r]`, `mmu[s]`, `page[s]`, `stack[n]`, `@symbol`, and the payload
// names of the scope's event kind. Operators, low to high: `or`; `and`;
// `not`; `== != < > <= >=`; `| ^`; `&`; `<< >>`; `+ -`; `* / %`; unary `- ~`.
// Comparisons and `and`/`or`/`not` yield 1 or 0; `and`/`or` short-circuit.
// A predicate is true when its value is non-zero.
//
// WHAT IS REFUSED AT COMPILE TIME, so a compiled predicate can never read
// something that is not there (§5.4):
//   * a payload name the scope's event does not carry — with the `None` scope,
//     every payload name (ADDR VALUE PREV PAGE PORT REG SOURCE KEY REASON CPC
//     WAIT_V WAIT_H SRC DST LEN DMA_MODE IO_SRC IO_DST);
//   * an `@symbol` the resolver does not know, or any `@symbol` when no
//     resolver is given;
//   * the script-only forms — a `var`, `NAME.field`, `changed()`, `depth()` —
//     and string literals: a condition is an integer expression.
//
// WHAT FAILS AT RUN TIME (dsl-frontend.md §6.5): division or modulo by zero,
// `mem[]` / `mem16[]` / `stack[]` addressing outside 0..0xFFFF, `nextreg[]`
// outside 0..255, `mmu[]` / `page[]` outside 0..7, `phys[]` naming a page the
// backend refuses or an offset outside 0..0x1FFF. A predicate that fails
// returns FALSE and reports the failure to `CompileOptions::on_runtime_error`
// (with the failing node's position); `eval_expr` returns it as an error.
//
// READS ARE SIDE-EFFECT FREE. Every name reads through the `Debugger`'s const
// inspection surface (`registers()`, `peek()`, `nextreg_peek()`,
// `mmu_slots()`, `raster()`, `time()`, `machine()`, `audio_mute_mask()`), which
// is non-perturbing by contract — a read here fires no watchpoint and no
// destructive NextREG read handler.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "debug/events.h"
#include "script/diagnostic.h"

namespace jnext {
namespace dbg {
class Debugger;
}

namespace script {

/// The event an expression is compiled for — §5.4's `EventKind|None`.
///
/// Default-constructed it is `None`: no payload name is legal. Built from a
/// bare `dbg::EventKind` (implicitly, so `compile_expr(text, EventKind::Port)`
/// reads as §5.4 spells it) it admits the payload names EVERY event of that
/// kind carries; a refinement — the access of a `Mem`/`Port` rule, the
/// sub-kind of a `Copper`/`Dma` rule — admits the rest. So a bare `Mem` scope
/// refuses `PREV` (a read carries none) and `Mem` + `Access::Write` admits it;
/// a bare `Copper` scope admits `CPC` but not `WAIT_V`.
struct PayloadScope {
    enum class Tag : uint8_t {
        None,        ///< no event: payload names are compile errors
        Event,       ///< a backend event of `kind`
        ScriptStop,  ///< a script `on stop` rule (REASON, PC). Script engine
                     ///< only: `compile_expr` refuses it — a stop is not a
                     ///< backend event, so there is no predicate to build.
    };

    Tag            tag    = Tag::None;
    dbg::EventKind kind   = dbg::EventKind::Execute;
    /// `Mem` / `Port`: the directions the rule sees (a mask). `ReadWrite` =
    /// either, which admits only what both carry.
    dbg::Access    access = dbg::Access::ReadWrite;
    /// `Copper`: the sub-kind, when the rule names one.
    std::optional<dbg::CopperEventKind> copper;
    /// `Dma`: the sub-kind, when the rule names one.
    std::optional<dbg::DmaEventKind> dma;

    PayloadScope() = default;
    PayloadScope(dbg::EventKind k) : tag(Tag::Event), kind(k) {}  // NOLINT: implicit on purpose

    static PayloadScope none() { return PayloadScope{}; }
    static PayloadScope mem(dbg::Access a) {
        PayloadScope s(dbg::EventKind::Mem);
        s.access = a;
        return s;
    }
    static PayloadScope port(dbg::Access a) {
        PayloadScope s(dbg::EventKind::Port);
        s.access = a;
        return s;
    }
    static PayloadScope copper_sub(dbg::CopperEventKind k) {
        PayloadScope s(dbg::EventKind::Copper);
        s.copper = k;
        return s;
    }
    static PayloadScope dma_sub(dbg::DmaEventKind k) {
        PayloadScope s(dbg::EventKind::Dma);
        s.dma = k;
        return s;
    }
    static PayloadScope script_stop() {
        PayloadScope s;
        s.tag = Tag::ScriptStop;
        return s;
    }

    /// "a `write` event", "a `copper wait` event", "no event" — for messages.
    std::string describe() const;
};

/// Resolves `@name` to its address, or empty when the name is unknown. For a
/// frontend this is `[&](const std::string& n) { return dbg.lookup_name(n); }`
/// — `symbols_of()` below.
using SymbolResolver = std::function<std::optional<uint16_t>(const std::string&)>;

/// A resolver over the backend's one symbol table (CAP-SYM).
SymbolResolver symbols_of(const dbg::Debugger& dbg);

/// Receives a compiled predicate's run-time failure. Called on the emulation
/// thread, at the delivery, once per failed evaluation.
using RuntimeErrorHandler = std::function<void(const Diagnostic&)>;

struct CompileOptions {
    /// `@symbol` resolution — done ONCE, at compile time (§6.5: an unknown
    /// symbol is a load-time error). Empty: every `@symbol` is refused.
    SymbolResolver symbols;
    /// Where run-time failures go. Empty: a failing predicate is simply false,
    /// which is silent — every jnext caller passes one.
    RuntimeErrorHandler on_runtime_error;
};

/// The result of `compile_expr`: a predicate, or why there is none.
struct CompiledPredicate {
    dbg::Condition          predicate;  ///< set iff `errors` is empty
    std::vector<Diagnostic> errors;     ///< lexical, syntax and scope errors
    explicit operator bool() const { return errors.empty(); }
};

/// §5.4 — compile `text` into a CAP-EVT predicate for events of `scope`.
///
/// The predicate evaluates the expression against the delivered `Event` (for
/// the payload names) and the `Debugger` (for everything else) and returns
/// whether the value is non-zero. `PC` means the payload's PC — the
/// instruction that CAUSED the event (REQ-dsl-12) — in any event scope, and
/// the CPU's PC in the `None` scope; `HC_ULA` / `CVC` are the Copper step's
/// position in a `Copper` scope and the live raster elsewhere (§2.3).
CompiledPredicate compile_expr(const std::string& text, PayloadScope scope,
                               const CompileOptions& opts = {});

/// The result of `eval_expr`.
struct EvalResult {
    bool                    ok = false;
    int32_t                 value = 0;
    std::vector<Diagnostic> errors;  ///< compile-time or run-time; empty iff `ok`
};

/// §5.4 — evaluate `text` once, now, in the `None` scope (ZRCP `evaluate`).
/// `@symbol`s resolve through `dbg`'s symbol table.
EvalResult eval_expr(const std::string& text, const dbg::Debugger& dbg);

}  // namespace script
}  // namespace jnext
