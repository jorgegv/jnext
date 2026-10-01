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

// ---------------------------------------------------------------------------
// THE RECURSION BOUNDS. Every recursive pass in src/script/ is bounded by one
// of these three, enforced here, at parse time, by the only producer of trees:
//
//   1. the parser's own recursion: at most MAX_EXPR_DEPTH nested `parse_expr` /
//      `not` / unary levels (a `(`, an index `mem[…]`, an interpolation);
//   2. the HEIGHT of every expression tree it returns: at most MAX_EXPR_DEPTH
//      nodes on any root-to-leaf path — which bounds `bind_expr` / the
//      checker's walk, `eval_int`, and the shared_ptr destructor chain. A
//      left-associative chain (`1+1+…+1`) is a LOOP in the parser but a
//      left-deep tree, so bound 1 alone does not cover it (review round 1, B1);
//   3. `if` nesting: at most MAX_IF_DEPTH, which bounds `parse_actions`, the
//      checker's `check_actions` and the Action destructor.
//
// Past any of them the input is REFUSED with a positioned error ("… nested too
// deeply"); nothing in src/script/ recurses over anything else (the lexer, the
// string scanner and the top level are loops). The limits are far beyond any
// hand-written condition and small in stack terms: script_parse_test runs the
// deepest accepted shapes through parse, check, compile, evaluate and
// destruction on a 1 MB thread stack (row DEPTH-STACK).
// ---------------------------------------------------------------------------
constexpr int MAX_EXPR_DEPTH = 200;
constexpr int MAX_IF_DEPTH   = 64;

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
