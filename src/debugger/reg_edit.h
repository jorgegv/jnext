#pragma once

// GH #368 -- the CPU panel's register editing policy: which fields are
// editable, how the typed text is read, how a flag toggles, and what a refused
// write says. Pure (no Qt, no I/O), so a pure suite pins it (reg_edit_test).
//
// No VHDL oracle: this is host debugger UI. The oracles are the issue's
// contract, UM0080's register and F layout, and the facade's `set_register`
// (src/debug/debugger.h).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "debug/inspect.h"       // RegId
#include "debug/result.h"        // Result, result_name
#include "debug/symbol_table.h"  // SymbolTable

namespace jnext { namespace regedit {

/// How a field's text is read.
enum class FieldKind { Word, Byte, Iff, Im };

/// One editable field of the CPU panel. `navigable` marks the values that are
/// addresses (the GH #366 click shortcuts use it).
struct Field {
    const char*        label;
    FieldKind          kind;
    jnext::dbg::RegId  reg;      // for Iff: IFF1 (IFF2 is written as well)
    bool               navigable;
};

/// The panel's 16 editable fields, in its display order.
inline const std::array<Field, 16>& fields()
{
    using R = jnext::dbg::RegId;
    static const std::array<Field, 16> f = {{
        {"AF",  FieldKind::Word, R::AF,   false},
        {"BC",  FieldKind::Word, R::BC,   true},
        {"DE",  FieldKind::Word, R::DE,   true},
        {"HL",  FieldKind::Word, R::HL,   true},
        {"AF'", FieldKind::Word, R::AF2,  false},
        {"BC'", FieldKind::Word, R::BC2,  true},
        {"DE'", FieldKind::Word, R::DE2,  true},
        {"HL'", FieldKind::Word, R::HL2,  true},
        {"IX",  FieldKind::Word, R::IX,   true},
        {"IY",  FieldKind::Word, R::IY,   true},
        {"SP",  FieldKind::Word, R::SP,   true},
        {"PC",  FieldKind::Word, R::PC,   true},
        {"I",   FieldKind::Byte, R::I,    false},
        {"R",   FieldKind::Byte, R::R,    false},
        {"IFF", FieldKind::Iff,  R::IFF1, false},
        {"IM",  FieldKind::Im,   R::IM,   false},
    }};
    return f;
}

struct Parsed {
    bool        ok = false;
    uint16_t    v0 = 0;      // the value; for Iff, IFF1
    uint16_t    v1 = 0;      // for Iff, IFF2
    std::string why;         // when !ok
};

namespace detail {

inline std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

inline int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace detail

/// Word: `SymbolTable::resolve()` (a number first, `@name` a symbol, `$`/`0x`
///       hex: the rule every address box uses).
/// Byte: 1-2 hex digits, optional `$`/`0x`, no symbols.
/// Iff:  "a/b" with a, b in {0,1}, blanks around trimmed: v0 = IFF1, v1 = IFF2.
/// Im:   "0", "1" or "2" (the Z80's three modes; the backend masks to 0..3 but
///       mode 3 runs as IM 1 in the core, so the panel refuses it).
/// Empty text is refused.
inline Parsed parse(FieldKind k, std::string_view text, const SymbolTable& syms)
{
    Parsed p;
    text = detail::trim(text);
    if (text.empty()) { p.why = "is empty"; return p; }

    switch (k) {
    case FieldKind::Word: {
        const auto v = syms.resolve(text);
        if (!v) { p.why = "is not a 16-bit hex number or a symbol"; return p; }
        p.ok = true; p.v0 = *v;
        return p;
    }
    case FieldKind::Byte: {
        std::string_view t = text;
        if (t.front() == '$') t.remove_prefix(1);
        else if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.remove_prefix(2);
        if (t.empty() || t.size() > 2) { p.why = "is not a 1-2 digit hex byte"; return p; }
        unsigned v = 0;
        for (char c : t) {
            const int d = detail::hex_digit(c);
            if (d < 0) { p.why = "is not a 1-2 digit hex byte"; return p; }
            v = v * 16 + static_cast<unsigned>(d);
        }
        p.ok = true; p.v0 = static_cast<uint16_t>(v);
        return p;
    }
    case FieldKind::Iff: {
        const size_t slash = text.find('/');
        if (slash == std::string_view::npos) { p.why = "is not IFF1/IFF2 (0 or 1 each, e.g. 1/0)"; return p; }
        const std::string_view a = detail::trim(text.substr(0, slash));
        const std::string_view b = detail::trim(text.substr(slash + 1));
        const auto bit = [](std::string_view s) { return s.size() == 1 && (s[0] == '0' || s[0] == '1'); };
        if (!bit(a) || !bit(b)) { p.why = "is not IFF1/IFF2 (0 or 1 each, e.g. 1/0)"; return p; }
        p.ok = true; p.v0 = static_cast<uint16_t>(a[0] - '0'); p.v1 = static_cast<uint16_t>(b[0] - '0');
        return p;
    }
    case FieldKind::Im: {
        if (text.size() != 1 || text[0] < '0' || text[0] > '2') { p.why = "is not an interrupt mode (0, 1 or 2)"; return p; }
        p.ok = true; p.v0 = static_cast<uint16_t>(text[0] - '0');
        return p;
    }
    }
    p.why = "is not valid";
    return p;
}

/// F with bit `bit` flipped; every other bit kept (the undocumented 5 and 3 too).
constexpr uint8_t toggle_flag(uint8_t f, int bit)
{
    return static_cast<uint8_t>(f ^ (1u << bit));
}

/// The status-bar text for a refused write.
inline std::string refusal_text(const char* label, jnext::dbg::Result r)
{
    std::string s = std::string(label) + " not changed: ";
    if (r == jnext::dbg::Result::RefusedRzx)
        s += "registers cannot be edited while an RZX recording is being made or played (stop it first)";
    else
        s += jnext::dbg::result_name(r);
    return s;
}

}}  // namespace jnext::regedit
