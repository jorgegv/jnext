#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the integer evaluator over a BOUND expression tree.
//
// WP1 scope: what `compile_expr` / `eval_expr` need — literals, operators,
// the bound built-in and payload names, the bracketed accessors and resolved
// `@symbol`s. The script-only forms (variables, snapshot fields, `changed()`,
// `depth()`, strings and their interpolation) are refused by `bind_expr`
// before a tree reaches here without a script; WP2 extends `EvalContext` with
// the interpreter state that gives them a value.
//
// SEMANTICS (dsl-frontend.md §2.1, and the "WP1 as built" notes):
//   * integers are 32-bit signed and WRAP — `+ - * << -x` are computed on the
//     unsigned 32-bit pattern, so overflow is defined;
//   * `/` truncates toward zero, `%` takes the sign of the dividend; either by
//     zero is a run-time error; INT_MIN / -1 wraps to INT_MIN (and % gives 0);
//   * shifts use the low five bits of the count (as Java/x86 do): `x << 33` is
//     `x << 1`; `>>` is arithmetic;
//   * comparisons, `not`, `and`, `or` give 1 or 0; `and` / `or` short-circuit.
//
// A run-time failure throws `EvalError`, which never leaves this library:
// `compile_expr`'s predicate and `eval_expr` catch it at their boundary.
// ---------------------------------------------------------------------------

#include <cstdint>

#include "debug/events.h"
#include "script/ast.h"
#include "script/diagnostic.h"

namespace jnext {
namespace dbg {
class Debugger;
}

namespace script {

struct EvalError {
    Diagnostic d;
};

struct EvalContext {
    const dbg::Debugger& dbg;
    /// The delivered event, when the expression was bound for one; the `P_`
    /// names read it. Null in the `None` scope (no `P_` name can be bound
    /// there).
    const dbg::Event* ev = nullptr;
};

/// Evaluate a bound expression. Throws `EvalError` on a run-time failure.
int32_t eval_int(const Expr& e, const EvalContext& ctx);

}  // namespace script
}  // namespace jnext
