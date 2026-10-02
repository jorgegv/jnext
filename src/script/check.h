#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the load-time checks of a parsed script, and name BINDING.
//
// What `check_script()` refuses (dsl-frontend.md §6.5 "load time"), all
// reported with the offending position, every error found (not just the
// first — the text is syntactically whole, so each report is about a real
// construct):
//
//   * a payload name used in a rule whose event does not carry it (§5.4 — the
//     per-kind check: `CPC` outside a `copper` rule, `PREV` in `on read`,
//     `ADDR` in a filter bound or a `var` initializer, which have no event);
//   * a rule label used twice; `enable` / `disable` naming no labelled rule;
//   * a `var` declared twice; a variable used or `set` but never declared;
//   * an unknown snapshot field (`NAME.XYZ`), `MMU` without an index or
//     another field with one;
//   * with a resolver in `CheckOptions`, an `@symbol` no loaded MAP defines;
//   * a `var` initializer reading a variable declared AFTER it (initializers
//     run in declaration order at load time — evaluator.h `init_vars`);
//   * a TYPE error (value.h): a string in arithmetic or in a condition, a
//     string compared with an integer, a string where the grammar needs a
//     value, a format on a string interpolation.
//
// SCOPE OF ONE CALL: one script text. How several `--script` files share
// labels, variables and snapshot names is the loader's decision (WP3); it can
// check a merged `Script`.
//
// BINDING. Every upper-case name is resolved, in place, to what it reads in
// the scope it appears in (`Expr::builtin`): `PC` in an event rule is the
// payload PC, `CVC` in a `copper` rule is the Copper step's line, and so on
// (§2.3). Filter bounds and `var` initializers bind in the `None` scope — they
// are evaluated at registration, with no event. Every expression gets its
// static type (`Expr::type`), every variable reference its slot, and every
// snapshot name — in an expression or an action — its stack's slot, the
// stacks being listed in `Script::snapshots` (state.h).
// ---------------------------------------------------------------------------

#include <limits>
#include <map>
#include <string>
#include <vector>

#include "script/ast.h"
#include "script/diagnostic.h"
#include "script/expr_compiler.h"

namespace jnext {
namespace script {

struct CheckOptions {
    /// `@symbol` resolution. Empty: symbols are left unresolved and unchecked
    /// (the loader resolves them against the MAP it loaded).
    SymbolResolver symbols;
};

/// The payload scope of a rule's event (`on write` → `Mem` + `Write`, `on copper
/// wait` → `Copper` + `Wait`, `on stop` → `ScriptStop`, …).
PayloadScope scope_of(const EventSpec& ev);

/// Check and bind `script` in place. Empty = the script may be loaded.
std::vector<Diagnostic> check_script(Script& script, const CheckOptions& opts = {});

/// Whether `payload` (one of the `P_` builtins) is carried by every event of
/// `scope`. The table §5.4's per-kind check reads.
bool payload_legal(Builtin payload, const PayloadScope& scope);

/// The names a script declares, as binding needs them: variables and snapshot
/// stacks, each with its slot (ast.h `Expr::slot`).
struct ScriptNames {
    std::map<std::string, int> vars;   ///< variable -> slot (its index in `Script::vars`)
    /// A `var` initializer sees only the variables declared before it: slots
    /// below this. Unlimited everywhere else.
    int visible_vars = std::numeric_limits<int>::max();
    std::map<std::string, int> snaps;  ///< snapshot stack -> slot
    std::vector<std::string>   snap_order;
    /// The slot of snapshot stack `name`, created on first use.
    int snapshot_slot(const std::string& name);
};

/// Bind one expression for `scope` and return its static type — the building
/// block `check_script` and `compile_expr` share. `names` is the script's, or
/// null when there is no script (then a variable, a snapshot form or a string
/// is an error: see expr_compiler.h). With `require_symbols`, an `@symbol` that
/// `symbols` cannot resolve — or any, when `symbols` is empty — is an error.
ValueType bind_expr(Expr& e, const PayloadScope& scope, ScriptNames* names,
                    const SymbolResolver& symbols, bool require_symbols,
                    std::vector<Diagnostic>& errors);

}  // namespace script
}  // namespace jnext
