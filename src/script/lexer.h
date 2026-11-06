#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the lexer of the debugger DSL (`.jds`).
//
// The language is dsl-frontend.md §2; the lexical rules it fixes are:
//
//  * COMMENTS: `;`, `//` and `#`, each to the end of the line, and never inside
//    a string literal. `//` is a comment even right after an operand
//    (`a / b // c` is `a / b` and a comment): the language has no unary `/`,
//    so two slashes never occur in a valid expression (§2).
//  * INTEGERS: decimal, `0x` hex, `$` hex, `0b` binary. At most 32 bits;
//    the value is the 32-bit pattern, so `0xFFFFFFFF` is -1 (§2.1: "Integers
//    are 32-bit signed and wrap"). A number running straight into a letter or
//    digit it cannot use (`0X10`, `12ab`, `0b102`) is an error, not two tokens.
//  * STRINGS: `"…"` on one line, no escapes — a backslash is an ordinary
//    character, so a Windows path is written as it is. `${expr}` and
//    `${expr:fmt}` (fmt `x2`, `x4` or `d`) are interpolations: the lexer
//    records the embedded expression's SOURCE and position, and the parser
//    lexes and parses it in place, so an error inside one reports its true
//    line and column. A `"` inside `${…}` is an error (it would end the outer
//    string), and so is a comment character: inside a string none of the three
//    comment forms is a comment, so `;` and `#` there are just bad characters.
//  * THE BRACKETED ACCESSORS are single tokens exactly as the grammar spells
//    them — `mem[`, `mem16[`, `phys[`, `nextreg[`, `mmu[`, `page[`, `stack[`,
//    `changed(`, `depth(` — with no space before the bracket. That is what
//    tells the accessor `page[s]` from the event keyword `page` of an
//    `addr_spec`, and `nextreg[r]` from the event `on nextreg`.
//  * `cvar(PATH)` (GH #436) is one token too, with the RAW text of the C path up to the
//    first `)` on the same line (trimmed) in `text`: the C path grammar (`->`, `::`, `*`,
//    `$1F`, `file.c`) is not the DSL's, and re-spelling it from DSL tokens would lose the
//    `->` and lex `x:19` as a `FileLine`. A `(`, a `"`, a line end or the end of the text
//    before the `)`, and an empty path, are lexical errors; the path itself is checked by
//    the checker. `cvar` with no `(` directly after it is an ordinary identifier.
//  * `@name` is one token (a MAP symbol reference), the name being any
//    identifier — keywords included, since a MAP symbol is not a keyword.
//    It may also be identifiers joined by `.` (`@main.loop`, an sjasmplus
//    label; GH #390): a `.` joins only when a letter or `_` follows it, so
//    `@a..@b` is still a range and `@a.1` is `@a`, `.`, `1`.
//  * `file:line` (GH #346) is one token, `[A-Za-z_.][A-Za-z0-9_./-]*:[0-9]+`,
//    recognised by lookahead: the run of path characters must be followed
//    DIRECTLY by `:` and digits that do not run into an identifier character
//    (`main.c :19`, `main.c: 19`, `x:y` and `lbl: on` are not one). The run
//    stops at `//` (a comment). Not lexed inside `${...}`. No valid script
//    changes: `NAME : NUMBER` with no space is nowhere valid otherwise.
//  * LINE ENDS ARE WHITESPACE. See the "WP1 as built" notes in
//    dsl-frontend.md for why no construct of §2.1 needs one.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "script/diagnostic.h"

namespace jnext {
namespace script {

enum class Tok : uint8_t {
    Eof,
    Ident,    ///< a word: keyword, built-in name or user identifier (`text`)
    Int,      ///< an integer literal (`value`)
    String,   ///< a string literal (`pieces`)
    Symbol,   ///< `@name` (`text` = the name without `@`)
    FileLine, ///< `main.c:19` (`text` = the whole spelling; GH #346)

    // The bracketed accessors, one token each (see the banner).
    MemL, Mem16L, PhysL, NextRegL, MmuL, PageL, StackL, ChangedL, DepthL,
    CVar,     ///< `cvar(PATH)`: the raw path in `text` (GH #436)

    LParen, RParen, LBracket, RBracket, Comma, Colon, Dot, DotDot, Assign,

    Plus, Minus, Star, Slash, Percent, Amp, Pipe, Caret, Tilde, Shl, Shr,
    Eq, Ne, Lt, Gt, Le, Ge,
};

/// Human spelling of a token kind, for error messages (`\`do\``, `end of input`).
const char* tok_name(Tok t);

/// The format of an interpolation (`${expr:fmt}`, §2.1).
enum class Fmt : uint8_t {
    None,  ///< no `:fmt` — the evaluator's default (decimal)
    X2,    ///< two hex digits
    X4,    ///< four hex digits
    D,     ///< decimal
};

/// One piece of a string literal: literal text, or an embedded expression.
struct StrPiece {
    bool        is_expr = false;
    std::string text;        ///< literal text; for an expression, its source
    SourcePos   pos;         ///< where `text` starts in the script
    Fmt         fmt = Fmt::None;
};

struct Token {
    Tok         kind = Tok::Eof;
    SourcePos   pos;
    std::string text;            ///< Ident / Symbol spelling; CVar: the raw C path
    int32_t     value = 0;       ///< Int value (the 32-bit pattern)
    std::vector<StrPiece> pieces;  ///< String contents
};

struct LexResult {
    std::vector<Token>        tokens;  ///< always ends with `Tok::Eof` when `!error`
    std::optional<Diagnostic> error;   ///< the first lexical error, if any
};

/// Lex `src`. `start` is the position of its first character in the script;
/// `embedded` lexes the inside of a `${…}`, where there are no comments.
LexResult lex(const std::string& src, SourcePos start = SourcePos{}, bool embedded = false);

}  // namespace script
}  // namespace jnext
