#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the evaluator over a CHECKED (bound and typed) tree.
//
// It serves two callers. `compile_expr` / `eval_expr` (no script: no
// variables, snapshots or strings — `bind_expr` refuses them first), and a
// script (WP2): variables, the snapshot stacks and their fields, `changed()`,
// `depth()`, string comparisons and `${…}` interpolation, reading the
// interpreter state in `EvalContext::state`.
//
// SEMANTICS (dsl-frontend.md §2.1, §2.3, §2.5; Appendices G and H):
//   * integers are 32-bit signed and WRAP — `+ - * << -x` are computed on the
//     unsigned 32-bit pattern, so overflow is defined;
//   * `/` truncates toward zero, `%` takes the sign of the dividend; either by
//     zero is a run-time error; INT_MIN / -1 wraps to INT_MIN (and % gives 0);
//   * shifts use the low five bits of the count (as Java/x86 do): `x << 33` is
//     `x << 1`; `>>` is arithmetic;
//   * comparisons, `not`, `and`, `or` give 1 or 0; `and` / `or` short-circuit;
//   * `==` / `!=` of two strings compare their text (value.h: the only string
//     operations besides interpolation).
//
// EVERY READ IS SIDE-EFFECT FREE: the machine is read only through the const
// `Debugger` inspection surface (`registers()`, `peek()`, `nextreg_peek()`,
// `mmu_slots()`, `raster()`, `time()`, `machine()`, `audio_mute_mask()`).
//
// A run-time failure (§6.5) throws `EvalError` with the failing node's
// position, and never leaves this library through a predicate: `compile_expr`'s
// predicate, `eval_expr` and `make_condition`'s predicate catch it at their
// boundary. The engine (WP3) catches it around a rule body and disables the
// rule.
//
// RECURSION: `eval_int` / `eval_str` / `interpolate` recurse over the tree only,
// whose height the parser bounds (parser.h bound 2); a string node's height
// includes its interpolations', so the three together stay inside that bound.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <string>

#include "debug/events.h"
#include "script/ast.h"
#include "script/diagnostic.h"
#include "script/expr_compiler.h"
#include "script/value.h"

namespace jnext {
namespace dbg {
class Debugger;
}

namespace script {

class ScriptState;

struct EvalError {
    Diagnostic d;
};

struct EvalContext {
    const dbg::Debugger& dbg;
    /// The delivered event, when the expression was bound for one; the `P_`
    /// names read it. Null in the `None` scope (no `P_` name can be bound
    /// there).
    const dbg::Event* ev = nullptr;
    /// A script's variables and snapshot stacks. Null without a script.
    ScriptState* state = nullptr;
    /// An `on stop` rule's `REASON`. Null elsewhere.
    const std::string* reason = nullptr;
};

/// Evaluate an integer-typed expression. Throws `EvalError`.
int32_t eval_int(const Expr& e, const EvalContext& ctx);

/// Evaluate a string-typed expression (a string literal, `REASON`). Throws
/// `EvalError`.
std::string eval_str(const Expr& e, const EvalContext& ctx);

/// Evaluate either type.
Value evaluate(const Expr& e, const EvalContext& ctx);

/// An integer as `${…}` prints it: none or `d` signed decimal; `x2` / `x4`
/// upper-case hex of the 32-bit pattern, zero-padded to AT LEAST 2 / 4 digits
/// (a wider value keeps all its digits: `${0x1234:x2}` is `1234`, never a
/// silently truncated `34`; `${-1:x4}` is `FFFFFFFF`).
std::string format_int(int32_t v, Fmt f);

/// A string literal with its interpolations evaluated. Throws `EvalError`.
std::string interpolate(const StringLit& s, const EvalContext& ctx);

/// Evaluate a checked script's `var` initializers into `state`, in
/// declaration order (each may read the ones before it). Throws `EvalError`.
void init_vars(const Script& script, ScriptState& state, const dbg::Debugger& dbg);

/// A rule's `when` clause as the backend's CAP-EVT predicate: evaluated against
/// the delivered Event, the Debugger and the script's state (§5.2: the closure
/// may read variables and snapshot fields). A run-time failure makes it false
/// and is reported to `on_error`.
dbg::Condition make_condition(ExprPtr when, std::shared_ptr<ScriptState> state,
                              RuntimeErrorHandler on_error);

}  // namespace script
}  // namespace jnext
