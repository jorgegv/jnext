#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the value model of the DSL (dsl-frontend.md §2.1, §2.3;
// "WP2 as built", Appendix H).
//
// TWO TYPES, KNOWN AT LOAD TIME.
//   * INTEGER — 32-bit signed, wrapping. Booleans are integers: comparisons,
//     `not`, `and`, `or` yield 1 or 0, and a condition is true when non-zero.
//     Every `var`, every machine value and every payload name but one is an
//     integer.
//   * STRING — a string literal (with its `${…}` interpolations evaluated) and
//     the `stop` payload `REASON`. A string can be compared with `==` / `!=`
//     against another string and interpolated into a string; nothing else.
//
// `check_script` gives every expression its type and refuses a misuse at load
// time — a string in arithmetic, a string compared with an integer, a string
// where the grammar needs a value (a condition, a `set`, an index, a filter
// bound, a `var` initializer) — so the evaluator never meets a type error.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>

namespace jnext {
namespace script {

enum class ValueType : uint8_t { Int, Str };

/// A value of either type; `type` says which member means anything.
struct Value {
    ValueType   type = ValueType::Int;
    int32_t     i = 0;
    std::string s;

    static Value of(int32_t v) {
        Value x;
        x.i = v;
        return x;
    }
    static Value of(std::string v) {
        Value x;
        x.type = ValueType::Str;
        x.s    = std::move(v);
        return x;
    }
};

}  // namespace script
}  // namespace jnext
