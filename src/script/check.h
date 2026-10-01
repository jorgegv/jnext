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
//   * with a resolver in `CheckOptions`, an `@symbol` no loaded MAP defines.
//
// SCOPE OF ONE CALL: one script text. How several `--script` files share
// labels, variables and snapshot names is the loader's decision (WP3); it can
// check a merged `Script`.
//
// BINDING. Every upper-case name is resolved, in place, to what it reads in
// the scope it appears in (`Expr::builtin`): `PC` in an event rule is the
// payload PC, `CVC` in a `copper` rule is the Copper step's line, and so on
// (§2.3). Filter bounds and `var` initializers bind in the `None` scope — they
// are evaluated at registration, with no event.
// ---------------------------------------------------------------------------

#include <set>
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

/// Bind one expression for `scope` — the building block `check_script` and
/// `compile_expr` share. `vars` is the script's declared variables, or null
/// when there is no script (then a variable, a snapshot form or a string is an
/// error: see expr_compiler.h). With `require_symbols`, an `@symbol` that
/// `symbols` cannot resolve — or any, when `symbols` is empty — is an error.
void bind_expr(Expr& e, const PayloadScope& scope, const std::set<std::string>* vars,
               const SymbolResolver& symbols, bool require_symbols,
               std::vector<Diagnostic>& errors);

}  // namespace script
}  // namespace jnext
