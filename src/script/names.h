#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the name tables of the DSL: the reserved lower-case words,
// the upper-case built-in state names and the event payload names
// (dsl-frontend.md §2.1, §2.3).
//
// ONE TABLE, READ BY THE PARSER (which names are words, which are values),
// THE CHECKER (which payload name is legal in which rule) AND THE EVALUATOR
// (what each bound name reads). Nothing else spells these names.
// ---------------------------------------------------------------------------

#include <optional>
#include <string>

#include "script/ast.h"

namespace jnext {
namespace script {

/// True for every lower-case word of the grammar (`on`, `when`, `execute`,
/// `log`, `page`, `value`, `and`, …) and the accessor stems (`mem`, `phys`,
/// `changed`, …). A reserved word is never a user variable, a rule label or
/// a snapshot name.
bool is_reserved_word(const std::string& word);

/// The LIVE meaning of an upper-case state name (§2.3 first table): `PC` →
/// `Builtin::PC`, `CVC` → `Builtin::CVC`, … Empty for payload-only names and
/// for anything that is not a built-in.
std::optional<Builtin> live_builtin(const std::string& name);

/// The PAYLOAD meaning of a name (§2.3 second table): `ADDR` → `P_ADDR`, and
/// the three that shadow a live name — `PC` → `P_PC`, `HC_ULA` → `P_HC_ULA`,
/// `CVC` → `P_CVC`. Empty otherwise.
std::optional<Builtin> payload_builtin(const std::string& name);

/// True iff `name` is a built-in value of any kind — live, payload, or one of
/// the `CPU`/`DMA`/`COPPER` constants. Such a name is never a user identifier.
bool is_builtin_name(const std::string& name);

/// The `CPU`/`DMA`/`COPPER` literal (§2.1) — 0, 1, 2 — or empty.
std::optional<int32_t> source_constant(const std::string& name);

/// True iff a `set` may write this built-in (§2.1 `lvalue`): the 22
/// registers, the 6 flags, IFF1, IFF2, IM and AUDIO_MUTE.
bool is_assignable(Builtin b);

/// The name as a script spells it (`P_ADDR` → "ADDR").
const char* builtin_spelling(Builtin b);

}  // namespace script
}  // namespace jnext
