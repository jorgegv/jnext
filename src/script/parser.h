#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the parser of the debugger DSL.
//
// Recursive descent over dsl-frontend.md §2.1, the grammar of record. It
// builds the syntax tree (`ast.h`) and nothing else: names are classified
// (reserved word / built-in / user identifier) but not BOUND to a scope, and
// no expression is evaluated. `check_script()` (check.h) does the load-time
// checks that need the whole script or the rule's event kind.
//
// Errors: the FIRST lexical or syntax error stops the parse, reported as
// `LINE:COLUMN: message` (§6.5 — a script with a parse error is rejected
// whole, so later errors would describe a text that is not being run).
//
// Where §2.1 is ambiguous the choice taken is recorded, with its reason, in
// the "WP1 as built" notes of dsl-frontend.md; the ones a reader of this file
// meets:
//
//  * operators and precedence are exactly §2.1's list, all binaries
//    left-associative (`a - b - c` is `(a - b) - c`, `a < b < c` is
//    `(a < b) < c`); `not` sits BELOW the comparisons, so `not a == b` is
//    `not (a == b)`, and `&` sits ABOVE them, so `x & 0x70 == 0x10` is
//    `(x & 0x70) == 0x10` (unlike C);
//  * two expressions written side by side (`dump_mem a n`, `out p v`) end the
//    first at the first token that cannot continue it — so `dump_mem a -1` is
//    `dump_mem (a - 1)` and then a missing length: write `dump_mem a (-1)`;
//  * `once` comes before `when`, only (review round 4, R-2);
//  * after `on dma` the word `end` is the sub-kind, not the rule terminator.
// ---------------------------------------------------------------------------

#include <optional>
#include <string>

#include "script/ast.h"
#include "script/diagnostic.h"

namespace jnext {
namespace script {

struct ParseResult {
    Script                    script;
    std::optional<Diagnostic> error;
    bool ok() const { return !error.has_value(); }
};

/// Parse a whole `.jds` text.
ParseResult parse_script(const std::string& text);

struct ExprParseResult {
    ExprPtr                   expr;
    std::optional<Diagnostic> error;
    bool ok() const { return !error.has_value(); }
};

/// Parse `text` as ONE expression (§2.1 `expr`); anything after it is an
/// error. What `compile_expr` / `eval_expr` parse.
ExprParseResult parse_expression(const std::string& text);

}  // namespace script
}  // namespace jnext
