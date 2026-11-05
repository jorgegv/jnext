#include "debug/cdb_record.h"

#include <cstdio>
#include <limits>

namespace jnext {
namespace dbg {

namespace {

bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

int hex_val(char c) {
    if (is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool all_digits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!is_digit(c)) return false;
    return true;
}

bool starts_with(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

// A recursive-descent reader over one unescaped record. The first failure wins:
// `err` is set once, every later step is a no-op.
class Reader {
public:
    explicit Reader(std::string_view s) : s_(s) {}

    bool failed() const { return !err_.empty(); }
    const std::string& error() const { return err_; }
    bool at_end() const { return p_ >= s_.size(); }
    char peek() const { return p_ < s_.size() ? s_[p_] : '\0'; }
    size_t pos() const { return p_; }

    bool fail(const char* what) {
        if (err_.empty()) {
            char b[96];
            std::snprintf(b, sizeof(b), "%s at column %zu of the record", what, p_ + 1);
            err_ = b;
        }
        return false;
    }

    bool expect(char c, const char* what) {
        if (failed()) return false;
        if (peek() != c || at_end()) return fail(what);
        ++p_;
        return true;
    }

    // Text up to (not including) the next `stop`, which must exist.
    std::string_view until(char stop, const char* what) {
        if (failed()) return {};
        const size_t e = s_.find(stop, p_);
        if (e == std::string_view::npos) {
            fail(what);
            return {};
        }
        const std::string_view r = s_.substr(p_, e - p_);
        p_ = e;
        return r;
    }

    // [-]digits, no sign other than '-', fits an int.
    bool integer(int& out, const char* what) {
        if (failed()) return false;
        size_t q = p_;
        bool neg = false;
        if (q < s_.size() && s_[q] == '-') {
            neg = true;
            ++q;
        }
        const size_t first = q;
        long long v = 0;
        while (q < s_.size() && is_digit(s_[q])) {
            v = v * 10 + (s_[q] - '0');
            if (v > std::numeric_limits<int>::max()) return fail("number too large");
            ++q;
        }
        if (q == first) return fail(what);
        out = static_cast<int>(neg ? -v : v);
        p_ = q;
        return true;
    }

    // `{n}`
    bool braced(int& out, const char* what) {
        return expect('{', what) && integer(out, what) && expect('}', what);
    }

private:
    std::string_view s_;
    size_t p_ = 0;
    std::string err_;
};

// One type-chain element, `text` being exactly the characters between commas.
bool parse_elem(std::string_view t, CdbTypeElem& e, std::string& why) {
    auto bad = [&](const char* m) {
        why = std::string(m) + " '" + std::string(t) + "'";
        return false;
    };
    if (t.size() < 2) return bad("unknown type element");
    // SDCC cdbFile.c:515-518: a noun it has no case for (bool, _BitInt, ...) prints
    // nothing, then still ':' and S or U.
    if (t == ":S" || t == ":U") {
        e.kind = CdbTypeKind::Scalar;
        e.is_unsigned = t[1] == 'U';
        return true;
    }
    if (t[0] == 'D') {
        const char c = t[1];
        if (t == "DF") {
            e.kind = CdbTypeKind::Function;
        } else if (t == "DV") {
            e.kind = CdbTypeKind::Void;
        } else if (t.size() == 2 && (c == 'G' || c == 'C' || c == 'X' || c == 'D' || c == 'I' || c == 'P')) {
            e.kind = CdbTypeKind::Pointer;
            e.pointer_class = c;
        } else if (c == 'A') {
            // DA<n>x (sccz80, 80cc) or DA<n>d (zsdcc): n is decimal (cdbfile.c prints
            // "%d", SDCC "%ud"), the letter is a literal.
            std::string_view num = t.substr(2);
            if (num.empty()) return bad("array without a count or suffix");
            const char suffix = num.back();
            if (suffix != 'x' && suffix != 'd') return bad("array suffix is neither x nor d");
            num.remove_suffix(1);
            const bool neg = !num.empty() && num[0] == '-';
            if (neg) num.remove_prefix(1);
            if (!all_digits(num) || num.size() > 6) return bad("array count is not a number");
            e.kind = CdbTypeKind::Array;
            int n = 0;
            for (char d : num) n = n * 10 + (d - '0');
            e.count = neg ? -n : n;
        } else {
            return bad("unknown type element");
        }
        return true;
    }
    if (t[0] != 'S') return bad("unknown type element");
    // S<kind>:<S|U>; the signedness is the text after the LAST ':'.
    const size_t colon = t.rfind(':');
    if (colon == std::string_view::npos || colon + 2 != t.size() || (t[colon + 1] != 'S' && t[colon + 1] != 'U'))
        return bad("type element lacks :S or :U");
    e.is_unsigned = t[colon + 1] == 'U';
    const std::string_view body = t.substr(1, colon - 1);
    if (body.empty()) return bad("unknown type element");
    if (body.size() == 1) {
        switch (body[0]) {
            case 'C': e.kind = CdbTypeKind::Char; return true;
            case 'I': e.kind = CdbTypeKind::Int; return true;
            case 'L': e.kind = CdbTypeKind::Long; return true;
            case 'F': e.kind = CdbTypeKind::Float; return true;
            case 'Q': e.kind = CdbTypeKind::Fixed; return true;
            case '6': e.kind = CdbTypeKind::Float16; return true;
            case 'V': e.kind = CdbTypeKind::Void; return true;
            case 'T': return bad("struct without a tag");
            case 'B': return bad("bitfield is not SB<start>$<width>");
            default: return bad("unknown type element");
        }
    }
    if (body[0] == 'T') {
        e.kind = CdbTypeKind::Struct;
        e.tag = std::string(body.substr(1));
        return true;
    }
    if (body[0] == 'B') {
        // SB<start>$<width>
        const std::string_view rest = body.substr(1);
        const size_t d = rest.find('$');
        if (d == std::string_view::npos || !all_digits(rest.substr(0, d)) || !all_digits(rest.substr(d + 1)) ||
            d > 4 || rest.size() - d - 1 > 4)
            return bad("bitfield is not SB<start>$<width>");
        e.kind = CdbTypeKind::Bitfield;
        for (char c : rest.substr(0, d)) e.bit_start = e.bit_start * 10 + (c - '0');
        for (char c : rest.substr(d + 1)) e.bit_width = e.bit_width * 10 + (c - '0');
        return true;
    }
    return bad("unknown type element");
}

// `({size}chain)`
bool parse_type(Reader& r, CdbType& type) {
    if (!r.expect('(', "expected '(' before the type")) return false;
    if (!r.braced(type.size, "expected {size} in the type")) return false;
    const std::string_view chain = r.until(')', "type is not closed by ')'");
    if (r.failed()) return false;
    size_t b = 0;
    while (b < chain.size()) {
        size_t e = chain.find(',', b);
        if (e == std::string_view::npos) e = chain.size();
        CdbTypeElem elem;
        std::string why;
        if (!parse_elem(chain.substr(b, e - b), elem, why)) return r.fail(why.c_str());
        type.chain.push_back(std::move(elem));
        b = e + 1;
        if (e + 1 == chain.size()) return r.fail("type chain ends with ','");
    }
    return r.expect(')', "type is not closed by ')'");
}

// `,<space>(,<number>|,[reg,...])*`, up to the end or a ')' (a struct member's
// closing paren).
bool parse_storage(Reader& r, CdbVar& v) {
    if (!r.expect(',', "expected ',' before the address space")) return false;
    const char sp = r.peek();
    if (sp != 'E' && sp != 'B' && sp != 'R' && sp != 'C' && sp != 'D' && sp != 'Z' && sp != 'I')
        return r.fail("unknown address space");
    v.space = sp;
    r.expect(sp, "address space");
    bool listed = false;
    while (!r.failed() && r.peek() == ',') {
        r.expect(',', "comma");
        if (r.peek() == '[') {
            if (listed) return r.fail("two register lists");
            listed = true;
            r.expect('[', "register list");
            const std::string_view list = r.until(']', "register list is not closed by ']'");
            if (r.failed()) return false;
            size_t b = 0;
            while (!list.empty() && b <= list.size()) {
                size_t e = list.find(',', b);
                if (e == std::string_view::npos) e = list.size();
                const std::string_view name = list.substr(b, e - b);
                if (name.empty()) return r.fail("empty register name");
                for (char c : name)
                    if (!is_alpha(c) && !is_digit(c)) return r.fail("register name is not alphanumeric");
                v.regs.emplace_back(name);
                b = e + 1;
            }
            r.expect(']', "register list");
        } else {
            int n = 0;
            if (!r.integer(n, "expected a number or a register list")) return false;
            v.nums.push_back(n);
        }
    }
    if (r.failed()) return false;
    if (v.space == 'R') {
        if (!listed) return r.fail("register space without a register list");
    } else if (listed) {
        return r.fail("register list outside register space");
    }
    if (v.space == 'B' || v.space == 'E' || v.space == 'R') {
        if (v.nums.size() != 2) return r.fail("address space needs exactly two numbers");
    }
    return true;
}

// `name$level_sub$block(type),space,...`, the cursor just after the scope's '$'.
bool parse_var(Reader& r, CdbVar& v, bool allow_empty_name) {
    const std::string_view name = r.until('$', "expected '$' after the name");
    if (r.failed()) return false;
    if (name.empty() && !allow_empty_name) return r.fail("empty name");
    v.name = std::string(name);
    r.expect('$', "'$'");
    if (!r.integer(v.level, "expected the block level")) return false;
    if (!r.expect('_', "expected '_' between level and sub-level")) return false;
    if (!r.integer(v.sub, "expected the sub-level")) return false;
    if (!r.expect('$', "expected '$' before the block")) return false;
    if (!r.integer(v.block, "expected the block number")) return false;
    return parse_type(r, v.type) && parse_storage(r, v);
}

// zsdcc names an unnamed bitfield member `__<digits>`.
bool is_padding(std::string_view n) { return starts_with(n, "__") && all_digits(n.substr(2)); }

CdbHidden classify(const CdbRecord& rec) {
    const std::string& n = rec.var.name;
    if (rec.var.space == 'I') return CdbHidden::Port;
    auto digits_after = [&](std::string_view prefix) {
        return starts_with(n, prefix) && all_digits(std::string_view(n).substr(prefix.size()));
    };
    if (digits_after("sloc") || digits_after("__str_") || digits_after("__")) return CdbHidden::Temporary;
    const auto& chain = rec.var.type.chain;
    if (chain.empty()) return CdbHidden::EmptyType;
    if (chain[0].kind == CdbTypeKind::Array && chain[0].count < 0) return CdbHidden::IncompleteArray;
    if (chain[0].kind == CdbTypeKind::Function) return CdbHidden::Function;
    return CdbHidden::None;
}

CdbDecoded refuse(const Reader& r) {
    CdbDecoded d;
    d.error = r.error();
    return d;
}

CdbDecoded refuse_text(const char* what) {
    CdbDecoded d;
    d.error = what;
    return d;
}

}  // namespace

CdbUnescaped cdb_unescape(std::string_view s) {
    CdbUnescaped out;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        char msg[64];
        if (is_alpha(c)) {
            out.text.push_back(c);
        } else if (c == '_') {
            // `_hh` is a byte below 0x80. A byte >= 0x80 is printed through a signed char
            // as `_ffffffhh` (the writers' "%02x" of a negative char, sign-extended to int);
            // it decodes to the byte hh. A bare `_ff` or `_c3` is never written: refused.
            int v = -1;
            size_t len = 3;
            if (i + 8 < s.size() && s.compare(i + 1, 6, "ffffff") == 0) {
                const int a = hex_val(s[i + 7]), b = hex_val(s[i + 8]);
                if (a >= 8 && b >= 0) v = a * 16 + b;
                len = 9;
            } else if (i + 2 < s.size()) {
                const int a = hex_val(s[i + 1]), b = hex_val(s[i + 2]);
                if (a >= 0 && a < 8 && b >= 0) v = a * 16 + b;
            }
            if (v < 0) {
                std::snprintf(msg, sizeof(msg), "bad escape '_' at offset %zu", i);
                return {std::string(), msg};
            }
            if (v == 0) {
                std::snprintf(msg, sizeof(msg), "escape _00 (NUL) at offset %zu", i);
                return {std::string(), msg};
            }
            out.text.push_back(static_cast<char>(v));
            i += len - 1;
        } else {
            std::snprintf(msg, sizeof(msg), "unescaped byte 0x%02x at offset %zu", static_cast<unsigned char>(c), i);
            return {std::string(), msg};
        }
    }
    return out;
}

CdbDecoded cdb_decode(std::string_view rec) {
    Reader r(rec);
    CdbDecoded d;
    CdbRecord& out = d.record;
    if (rec.size() < 3 || rec[1] != ':') return refuse_text("not a CDB record: no 'K:' prefix");
    const char k = rec[0];
    if (k == 'M') {
        out.kind = CdbKind::Module;
        const std::string_view name = rec.substr(2);
        // 80cc writes `M:<file>`: the file name as free text, so a path such as `game(v2)/main.c` is a module name. `$` stays refused
        // (CDB row `M:a$b`, pinned since GH #432): a path with one loses the 80cc note until the owner decides that row
        if (name.empty() || name.find('$') != std::string_view::npos)
            return refuse_text("module record has an empty or malformed name");
        out.var.name = std::string(name);
        return d;
    }
    if (k == 'N' || k == 'E') {
        // z88dk#3241: `N:F<file>$<tag>({<size>}<base>)[(<name>$<value>)...]` and `E:<scope>$<name>$...({<size>}...SEF<file>$<tag>:<sign>...)`.
        // Recognised, not read: the variable's `S:` record is still written. The name is kept; the rest must at least be non-empty.
        out.kind = CdbKind::Enum;
        const std::string_view body = rec.substr(2);
        if (k == 'N') {
            out.scope = CdbScope::File;
            const size_t dollar = body.find('$'), open = body.find('(');
            if (body.empty() || body[0] != 'F' || dollar == std::string_view::npos || dollar < 2 || open == std::string_view::npos ||
                open <= dollar + 1 || body.back() != ']')
                return refuse_text("malformed enum record");
            out.scope_name = std::string(body.substr(1, dollar - 1));
            out.var.name = std::string(body.substr(dollar + 1, open - dollar - 1));
        } else {
            const size_t d1 = body.find('$');
            if (body.empty() || d1 == std::string_view::npos || body.find("({") == std::string_view::npos) return refuse_text("malformed enum record");
            out.scope = body[0] == 'G' ? CdbScope::Global : body[0] == 'F' ? CdbScope::File : body[0] == 'L' ? CdbScope::Local : CdbScope::None;
            if (out.scope == CdbScope::None) return refuse_text("unknown scope (expected G, F or L)");
            const size_t d2 = body.find('$', d1 + 1);
            if (d2 == std::string_view::npos || d2 == d1 + 1) return refuse_text("malformed enum record");
            out.var.name = std::string(body.substr(d1 + 1, d2 - d1 - 1));
        }
        return d;
    }
    if (k != 'F' && k != 'S' && k != 'T') return refuse_text("unknown record kind");
    r.expect(k, "kind");
    r.expect(':', "':'");

    if (k == 'T') {
        out.kind = CdbKind::Type;
        out.scope = CdbScope::File;
        if (!r.expect('F', "a type record is scoped 'F'")) return refuse(r);
        out.scope_name = std::string(r.until('$', "expected '$' after the type scope"));
        if (r.failed()) return refuse(r);
        if (out.scope_name.empty()) return refuse_text("type record with an empty scope");
        r.expect('$', "'$'");
        const std::string_view tag = r.until('[', "expected '[' before the members");
        if (r.failed()) return refuse(r);
        if (tag.empty()) return refuse_text("type record with an empty tag");
        out.var.name = std::string(tag);
        r.expect('[', "'['");
        while (!r.failed() && r.peek() == '(') {
            CdbMember m;
            r.expect('(', "'('");
            if (!r.braced(m.offset, "expected {offset} of the member")) break;
            if (!r.expect('S', "member scope") || !r.expect(':', "':'") || !r.expect('S', "a member is scoped 'S'") ||
                !r.expect('$', "'$'"))
                break;
            if (!parse_var(r, m.var, true)) break;
            m.hidden = is_padding(m.var.name) ? CdbHidden::Temporary : CdbHidden::None;
            if (!r.expect(')', "member is not closed by ')'")) break;
            out.members.push_back(std::move(m));
        }
        if (!r.expect(']', "members are not closed by ']'")) return refuse(r);
        if (!r.at_end()) return refuse_text("text after the type record");
        return d;
    }

    out.kind = (k == 'F') ? CdbKind::Function : CdbKind::Variable;
    const char sc = r.peek();
    if (sc == 'G') {
        out.scope = CdbScope::Global;
        r.expect('G', "scope");
    } else if (sc == 'F' || (sc == 'L' && k == 'S')) {
        out.scope = sc == 'F' ? CdbScope::File : CdbScope::Local;
        r.expect(sc, "scope");
        std::string_view where = r.until('$', "expected '$' after the scope");
        if (r.failed()) return refuse(r);
        if (out.scope == CdbScope::Local) {
            // A function name has no dots, a path has: split at the LAST one.
            const size_t dot = where.rfind('.');
            if (dot == std::string_view::npos || dot == 0 || dot + 1 == where.size())
                return refuse_text("local scope is not <file>.<function>");
            out.function = std::string(where.substr(dot + 1));
            where = where.substr(0, dot);
        }
        if (where.empty()) return refuse_text("record with an empty scope");
        out.scope_name = std::string(where);
    } else {
        return refuse_text("unknown scope (expected G, F or L)");
    }
    if (!r.expect('$', "expected '$' after the scope")) return refuse(r);
    if (!parse_var(r, out.var, false)) return refuse(r);
    if (!r.at_end()) return refuse_text("text after the record");
    if (out.kind == CdbKind::Variable) out.hidden = classify(out);
    return d;
}

CdbDecoded cdb_decode_symbol(std::string_view name) {
    if (!starts_with(name, kCdbPrefix)) return refuse_text("symbol does not start with __CDBINFO__");
    const CdbUnescaped u = cdb_unescape(name.substr(kCdbPrefix.size()));
    if (!u.ok()) return refuse_text(u.error.c_str());
    return cdb_decode(u.text);
}

}  // namespace dbg
}  // namespace jnext
