#pragma once

// ---------------------------------------------------------------------------
// jnext::script — a source position and a message about it.
//
// Every error the DSL front end reports — lexer, parser, the load-time checks,
// and a compiled expression's run-time failures — is one of these (design
// `doc/design/debug-subsystem/dsl-frontend.md` §6.5). The FILE is not part of
// it: the text handed to the front end is anonymous, and the loader that read
// it from a file prefixes the name (`guard.jds:12:5: …`).
// ---------------------------------------------------------------------------

#include <string>

namespace jnext {
namespace script {

/// A position in the source text. Both 1-based. `column` counts CHARACTERS
/// (UTF-8 code points), not bytes, so it is the column an editor shows even
/// after a comment with a non-ASCII character earlier on the line; a tab counts
/// as one.
struct SourcePos {
    int line   = 1;
    int column = 1;
};

/// One error, at one position.
struct Diagnostic {
    SourcePos   pos;
    std::string message;

    /// `LINE:COLUMN: message` — the form every jnext surface prints.
    std::string to_string() const {
        return std::to_string(pos.line) + ":" + std::to_string(pos.column) + ": " + message;
    }
};

}  // namespace script
}  // namespace jnext
