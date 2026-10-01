#pragma once

// ---------------------------------------------------------------------------
// ZEsarUX breakpoint conditions → the DSL's expression grammar (GH #280 WP-4,
// zrcp-frontend.md §3).
//
// ZEsarUX 12.0 (`src/expression_parser.c`) does not have an operator-precedence
// grammar. It tokenises the text (`exp_par_exp_to_tokens`), and evaluates a
// token range by splitting it at the FIRST top-level operator of the lowest
// class present — logical (`AND OR XOR`), then comparison, then `+ -`, then the
// other arithmetic (`* / & | ^`) — and recursing on both halves
// (`exp_par_evaluate_token`). So `A AND B OR C` is `A AND (B OR C)`, `9-3-1`
// is `9-(3-1)` and `2*3&1` is `2*(3&1)`, none of which is what the DSL's own
// precedence table would make of the same text.
//
// This module therefore TRANSLATES rather than re-spells: it tokenises exactly
// as ZEsarUX does, splits exactly as ZEsarUX does, and emits a fully
// parenthesised DSL expression for each split, so the DSL's precedence never
// decides anything and its compiler (`script::compile_expr`) is the only
// evaluator. Nothing here evaluates an expression.
//
// THE ONE GAP (§11 item 8, measured here): ZEsarUX's `SEG0..7`, `ROM` and `RAM`
// are the MMU projection DeZog decodes from `get-registers`' `MMU=` field, and
// a ROM slot's value depends on whether the slot is ROM — which the DSL cannot
// read (`mmu[n]` is the raw NextREG, `page[n]` the effective page; neither
// says ROM). They are honoured as NATIVE terms, evaluated by the adapter from
// the backend's `SlotInfo`, in the one shape DeZog sends them: a comparison
// with a number, as a conjunct of the top-level AND chain
// (`PC=0abcdh AND SEG3=0005h`). Anywhere else they are refused.
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "debug/inspect.h"
#include "memory/contention.h"

namespace jnext {
namespace remote {
namespace zrcp {

/// A `SEGn` / `ROM` / `RAM` comparison the adapter evaluates itself.
struct NativeTerm {
    enum class Var : std::uint8_t { Seg, Rom, Ram };
    enum class Cmp : std::uint8_t { Eq, Ne, Lt, Gt, Le, Ge };
    Var          var   = Var::Seg;
    int          n     = 0;  ///< the slot, for `Seg`
    Cmp          cmp   = Cmp::Eq;
    std::int32_t value = 0;
};

/// A condition (or an `evaluate` expression) as the adapter keeps it.
struct Translation {
    /// Tokenised, structurally valid, and every name in it honoured.
    bool ok = false;
    /// The text did not tokenise (ZEsarUX's `exp_par_exp_to_tokens` failed) —
    /// `evaluate`'s "Error parsing". False with `!ok` = it tokenised but cannot
    /// be evaluated or names something jnext does not honour.
    bool parse_error = false;
    /// No tokens at all: a slot with no condition ("None").
    bool empty = false;
    /// What failed, for the log and the suite.
    std::string error;

    /// ZEsarUX's own re-printing of the tokens (`exp_par_tokens_to_exp`):
    /// what `get-breakpoints` lists and `Breakpoint fired:` echoes.
    std::string canonical;

    /// §3.3 — the `PC=<literal>` first conjunct, when the fast path applies.
    std::optional<std::uint16_t> fast_pc;
    /// The DSL text of everything the fast path and the native terms do not
    /// cover; empty = true.
    std::string dsl;
    /// The native conjuncts; every one must hold.
    std::vector<NativeTerm> natives;
    /// `evaluate SEG7`: the whole expression is one native variable.
    std::optional<NativeTerm> bare_native;
};

/// Translate `text`. `fast_path` = split off a leading `PC=<literal>` conjunct
/// (breakpoints); `evaluate` passes false, as its value must be the whole
/// expression's.
Translation translate_condition(const std::string& text, bool fast_path);

/// The value ZEsarUX 12.0 gives `SEGn` / `ROM` / `RAM` on this machine, from
/// the same MMU projection `get-registers` prints (`zrcp_format.h`
/// `mapped_page`): `SEGn` on the Next only, `ROM` / `RAM` on the 128K and +3
/// only, and 0 everywhere else (`exp_par_calculate_numvarreg`).
std::int32_t native_value(NativeTerm::Var var, int n,
                          const std::array<jnext::dbg::SlotInfo, 8>& slots, MachineType type);

/// Whether `t` holds on this machine.
bool native_holds(const NativeTerm& t, const std::array<jnext::dbg::SlotInfo, 8>& slots,
                  MachineType type);

}  // namespace zrcp
}  // namespace remote
}  // namespace jnext
