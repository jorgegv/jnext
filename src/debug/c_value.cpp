#include "debug/c_value.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace jnext {
namespace dbg {

namespace {

// -- number text: std::to_chars only (locale independent) ---------------------

std::string num(long long v)
{
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, r.ptr);
}

std::string unum(unsigned long long v)
{
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, r.ptr);
}

std::string hexw(unsigned long long v, int width)
{
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, 16);
    std::string s(buf, r.ptr);
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    while (static_cast<int>(s.size()) < width) s.insert(s.begin(), '0');
    return s;
}

template <typename F>
std::string float_text(F v)
{
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, r.ptr);
}

std::string escape_char(unsigned char c, char quote)
{
    switch (c) {
    case '\n': return "\\n";
    case '\r': return "\\r";
    case '\t': return "\\t";
    case '\\': return "\\\\";
    case 0: return "\\0";
    default: break;
    }
    if (c == static_cast<unsigned char>(quote)) return std::string("\\") + quote;
    if (c < 0x20 || c >= 0x7F) return "\\x" + hexw(c, 2);
    return std::string(1, static_cast<char>(c));
}

constexpr size_t kMaxElements = 16;
constexpr size_t kMaxString = 32;
constexpr int kMaxDepth = 8;

bool is_char(const std::vector<CdbTypeElem>& ch, size_t i)
{
    return i < ch.size() && ch[i].kind == CdbTypeKind::Char;
}

const char* kU2 = "bitfield layout unknown (z88dk U2)";

// Whether member `idx` of a struct record shares bytes with another member, in a struct whose members are not
// all at offset 0 (a union is not one): sccz80 and 80cc write a bitfield as an overlapping int (U2).
bool overlaps_other(const CdbRecord& rec, size_t idx)
{
    bool any_nonzero = false;
    for (const CdbMember& m : rec.members) any_nonzero = any_nonzero || m.offset != 0;
    if (!any_nonzero) return false;
    const auto span = [](const CdbMember& m) { return std::max(1, m.var.type.size); };
    const CdbMember& a = rec.members[idx];
    if (!a.var.type.chain.empty() && a.var.type.chain[0].kind == CdbTypeKind::Bitfield) return false;   // zsdcc: decoded exactly
    for (size_t j = 0; j < rec.members.size(); ++j) {
        if (j == idx) continue;
        const CdbMember& b = rec.members[j];
        if (a.offset < b.offset + span(b) && b.offset < a.offset + span(a)) return true;
    }
    return false;
}

struct Formatter {
    const CFormatCtx& ctx;
    std::vector<std::string>& notes;
    std::string why;

    bool fail(const std::string& w)
    {
        why = w;
        return false;
    }

    std::string string_of(const uint8_t* b, size_t n, bool terminated_by_nul)
    {
        std::string s = "\"";
        size_t shown = 0;
        bool cut = false;
        for (size_t k = 0; k < n; ++k) {
            if (terminated_by_nul && b[k] == 0) break;
            if (shown == kMaxString) {
                cut = true;
                break;
            }
            s += escape_char(b[k], '"');
            ++shown;
        }
        s += "\"";
        if (cut) s += "...";
        return s;
    }

    bool pointer_string(uint16_t addr, std::string& out)
    {
        if (!ctx.read) return false;
        std::vector<uint8_t> got;
        for (size_t k = 0; k <= kMaxString; ++k) {
            uint8_t b = 0;
            if (!ctx.read(static_cast<uint32_t>(addr + k), 1, &b)) break;
            got.push_back(b);
            if (b == 0) break;
        }
        if (got.empty()) return false;
        out = string_of(got.data(), got.size(), true);
        return true;
    }

    bool fmt(const std::vector<CdbTypeElem>& ch, size_t i, int size, const uint8_t* b, size_t avail, int depth,
             std::string& out)
    {
        if (depth > kMaxDepth) return fail("nested too deep");
        if (i >= ch.size()) return fail("an empty type");
        const CdbTypeElem& e = ch[i];
        switch (e.kind) {
        case CdbTypeKind::Char: {
            if (avail < 1) return fail("the object is shorter than its type");
            const long long v = e.is_unsigned ? static_cast<long long>(b[0]) : static_cast<long long>(static_cast<int8_t>(b[0]));
            out = num(v);
            if (b[0] >= 0x20 && b[0] < 0x7F) out += " '" + escape_char(b[0], '\'') + "'";
            return true;
        }
        case CdbTypeKind::Int:
        case CdbTypeKind::Long:
        case CdbTypeKind::Scalar: {
            int sz = size > 0 ? size : c_leaf_size(ctx, ch, i);
            if (sz != 1 && sz != 2 && sz != 4 && sz != 8) return fail("an integer of " + num(sz) + " bytes is not supported");
            if (avail < static_cast<size_t>(sz)) return fail("the object is shorter than its type");
            unsigned long long u = 0;
            for (int k = sz - 1; k >= 0; --k) u = (u << 8) | b[k];
            if (e.is_unsigned) {
                out = unum(u);
            } else {
                const unsigned shift = 64u - 8u * static_cast<unsigned>(sz);
                out = num(static_cast<long long>(u << shift) >> shift);
            }
            return true;
        }
        case CdbTypeKind::Float:
            if (size == 6 && avail >= 6) {
                out = c_format_float48(b);
                return true;
            }
            if (size == 4 && avail >= 4) {
                uint32_t u = static_cast<uint32_t>(b[0]) | static_cast<uint32_t>(b[1]) << 8 | static_cast<uint32_t>(b[2]) << 16 |
                             static_cast<uint32_t>(b[3]) << 24;
                out = c_format_float32(u);
                return true;
            }
            return fail("a float of " + num(size) + " bytes is not supported");
        case CdbTypeKind::Float16:
            if (avail < 2) return fail("the object is shorter than its type");
            out = c_format_float16(static_cast<uint16_t>(b[0] | b[1] << 8));
            return true;
        case CdbTypeKind::Fixed: {
            const size_t n = size > 0 ? static_cast<size_t>(size) : 0;
            if (n == 0 || n > 8 || avail < n) return fail("the object is shorter than its type");
            unsigned long long u = 0;
            for (size_t k = n; k-- > 0;) u = (u << 8) | b[k];
            out = "<fixed-point $" + hexw(u, static_cast<int>(2 * n)) + ": layout not decoded>";
            return true;
        }
        case CdbTypeKind::Pointer: {
            if (avail < 2) return fail("the object is shorter than its type");
            const uint16_t p = static_cast<uint16_t>(b[0] | b[1] << 8);
            out = "$" + hexw(p, 4);
            if (p == 0) {
                out += " (NULL)";
                return true;
            }
            if (ctx.name_at) out += ctx.name_at(p);
            if (is_char(ch, i + 1)) {
                std::string s;
                if (pointer_string(p, s)) out += " " + s;
                else out += " <unreadable>";
            }
            return true;
        }
        case CdbTypeKind::Array: {
            if (e.count <= 0) return fail("an array of unknown length");
            if (size <= 0 || size % e.count != 0) return fail("the array's size does not divide by its length");
            const size_t esz = static_cast<size_t>(size / e.count);
            if (avail < static_cast<size_t>(size)) return fail("the object is shorter than its type");
            if (is_char(ch, i + 1)) {
                out = string_of(b, static_cast<size_t>(size), true);
                return true;
            }
            out = "{";
            const size_t n = static_cast<size_t>(e.count);
            for (size_t k = 0; k < n && k < kMaxElements; ++k) {
                std::string one;
                if (!fmt(ch, i + 1, static_cast<int>(esz), b + k * esz, esz, depth + 1, one)) return false;
                out += (k ? ", " : "") + one;
            }
            if (n > kMaxElements) out += ", ...";
            out += "}";
            return true;
        }
        case CdbTypeKind::Struct: {
            if (!ctx.store) return fail("no type information");
            const CTypeLookup lk = c_find_type(*ctx.store, e.tag, ctx.scope_name, ctx.module);
            if (!lk.record) return fail(lk.why);
            const CdbRecord& rec = lk.record->record;
            out = "{";
            bool first = true;
            for (size_t m = 0; m < rec.members.size(); ++m) {
                const CdbMember& mem = rec.members[m];
                if (mem.hidden != CdbHidden::None) continue;
                const size_t off = static_cast<size_t>(mem.offset);
                const size_t msz = static_cast<size_t>(std::max(1, mem.var.type.size));
                const bool u2 = overlaps_other(rec, m);
                if (off >= avail || (off + msz > avail && !u2)) return fail("member '" + mem.var.name + "' lies beyond the object");
                std::string one;
                std::vector<uint8_t> unit(msz, 0);   // sccz80 / 80cc write a bitfield as an int that may stick out of the struct
                const size_t have = std::min(msz, avail - off);
                std::copy(b + off, b + off + have, unit.begin());
                if (!fmt(mem.var.type.chain, 0, mem.var.type.size, unit.data(), msz, depth + 1, one)) return false;
                if (u2) {
                    one += std::string(" [") + kU2 + "]";
                    if (std::find(notes.begin(), notes.end(), kU2) == notes.end()) notes.push_back(kU2);
                }
                if (!first) out += ", ";
                first = false;
                out += mem.var.name.empty() ? one : mem.var.name + " = " + one;
            }
            out += "}";
            return true;
        }
        case CdbTypeKind::Bitfield: {
            const size_t n = static_cast<size_t>(std::max(1, size));
            if (n > 4 || avail < n) return fail("the object is shorter than its type");
            unsigned long long u = 0;
            for (size_t k = n; k-- > 0;) u = (u << 8) | b[k];
            const unsigned long long mask = e.bit_width >= 64 ? ~0ull : ((1ull << e.bit_width) - 1);
            out = unum((u >> e.bit_start) & mask);
            return true;
        }
        case CdbTypeKind::Void:
        case CdbTypeKind::Function:
            return fail("a function or void is not a value");
        }
        return fail("unknown type");
    }
};

// A struct's size is a declared object's when there is one: an `S:` record, or a struct member, whose chain is exactly
// `ST<tag>`. Else it is the members' extent, provided no member shares bytes with another (a union is fine): sccz80 and
// 80cc write a bitfield as an int that sticks out of the struct (z88dk U2), so the extent of THOSE overstates the size
// and is not used. -1: unknown.
int struct_size(const CFormatCtx& ctx, const std::string& tag)
{
    if (!ctx.store) return -1;
    const auto exact = [&](const CdbType& t) {
        return t.chain.size() == 1 && t.chain[0].kind == CdbTypeKind::Struct && t.chain[0].tag == tag && t.size > 0;
    };
    for (const CVariable& v : ctx.store->variables())
        if (exact(v.record.var.type)) return v.record.var.type.size;
    for (const CRecord& r : ctx.store->types())
        for (const CdbMember& m : r.record.members)
            if (exact(m.var.type)) return m.var.type.size;
    const CTypeLookup lk = c_find_type(*ctx.store, tag, ctx.scope_name, ctx.module);
    if (!lk.record) return -1;
    const CdbRecord& rec = lk.record->record;
    int end = 0;
    for (size_t m = 0; m < rec.members.size(); ++m) {
        if (rec.members[m].hidden == CdbHidden::None && overlaps_other(rec, m)) return -1;
        end = std::max(end, rec.members[m].offset + std::max(0, rec.members[m].var.type.size));
    }
    return end > 0 ? end : -1;
}

CdbType sliced(const CdbType& t, size_t n, int size)
{
    CdbType out;
    out.size = size;
    out.chain.assign(t.chain.begin() + static_cast<std::ptrdiff_t>(std::min(n, t.chain.size())), t.chain.end());
    return out;
}

bool read16(const CFormatCtx& ctx, uint32_t addr, uint16_t* v)
{
    uint8_t b[2] = {0, 0};
    if (!ctx.read || !ctx.read(addr, 2, b)) return false;
    *v = static_cast<uint16_t>(b[0] | b[1] << 8);
    return true;
}

bool deref_loc(const CFormatCtx& ctx, const CLoc& in, CLoc* out, std::string* why)
{
    if (in.type.chain.empty() || in.type.chain[0].kind != CdbTypeKind::Pointer) {
        *why = "not a pointer";
        return false;
    }
    uint16_t p = 0;
    if (!read16(ctx, in.addr, &p)) {
        *why = "unreadable memory at $" + hexw(in.addr, 4);
        return false;
    }
    if (p == 0) {
        *why = "null pointer";
        return false;
    }
    const int sz = c_leaf_size(ctx, in.type.chain, 1);
    if (sz < 0) {
        *why = in.type.chain.size() > 1 && in.type.chain[1].kind == CdbTypeKind::Struct
                   ? "size unknown (z88dk U2): no declared object of struct " + in.type.chain[1].tag
                   : "the size of the pointed-to type is not known";
        return false;
    }
    out->type = sliced(in.type, 1, sz);
    out->addr = p;
    out->note.clear();
    return true;
}

bool member_loc(const CFormatCtx& ctx, const CLoc& in, const std::string& name, CLoc* out, std::string* why)
{
    if (in.type.chain.empty() || in.type.chain[0].kind != CdbTypeKind::Struct) {
        *why = in.type.chain.empty() || in.type.chain[0].kind != CdbTypeKind::Pointer ? "not a struct" : "not a struct (use ->)";
        return false;
    }
    if (!ctx.store) {
        *why = "no type information";
        return false;
    }
    const CTypeLookup lk = c_find_type(*ctx.store, in.type.chain[0].tag, ctx.scope_name, ctx.module);
    if (!lk.record) {
        *why = lk.why;
        return false;
    }
    const CdbRecord& rec = lk.record->record;
    for (size_t m = 0; m < rec.members.size(); ++m) {
        const CdbMember& mem = rec.members[m];
        if (mem.var.name != name || mem.hidden != CdbHidden::None) continue;
        out->type = mem.var.type;
        out->addr = in.addr + static_cast<uint32_t>(mem.offset);
        // a member read stays inside the struct that holds it (the immediately enclosing object, and any outer limit)
        out->end = in.addr + static_cast<uint32_t>(std::max(0, in.type.size));
        if (in.end && in.end < out->end) out->end = in.end;
        out->note = overlaps_other(rec, m) ? kU2 : "";
        return true;
    }
    *why = "no member '" + name + "'";
    return false;
}

}  // namespace

// -- numbers -----------------------------------------------------------------

std::string c_format_float48(const uint8_t b[6])
{
    const int e = b[5];
    if (e == 0) return "0";
    const unsigned long long low = static_cast<unsigned long long>(b[0]) | static_cast<unsigned long long>(b[1]) << 8 |
                                   static_cast<unsigned long long>(b[2]) << 16 | static_cast<unsigned long long>(b[3]) << 24 |
                                   static_cast<unsigned long long>(b[4] & 0x7F) << 32;
    const unsigned long long m = (1ull << 39) | low;   // the implied leading 1 in front of 39 stored bits
    double v = std::ldexp(static_cast<double>(m), e - 0x80 - 40);
    if (b[4] & 0x80) v = -v;
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::general, 12);
    return std::string(buf, r.ptr);
}

std::string c_format_float32(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return float_text(f);
}

std::string c_format_float16(uint16_t h)
{
    const int sign = h >> 15;
    const int e = (h >> 10) & 0x1F;
    const int frac = h & 0x3FF;
    float f;
    if (e == 0) f = std::ldexp(static_cast<float>(frac), -24);
    else if (e == 31) f = frac ? std::nanf("") : HUGE_VALF;
    else f = std::ldexp(static_cast<float>(1024 + frac), e - 15 - 10);
    if (sign) f = -f;
    return float_text(f);
}

// -- types -------------------------------------------------------------------

namespace {

std::string base_text(const CdbType& t, size_t i)
{
    const CdbTypeElem& e = t.chain[i];
    switch (e.kind) {
    case CdbTypeKind::Char: return e.is_unsigned ? "unsigned char" : "char";
    case CdbTypeKind::Int: return e.is_unsigned ? "unsigned int" : "int";
    case CdbTypeKind::Long:
        if (t.size == 8 && t.chain.size() == 1) return e.is_unsigned ? "unsigned long long" : "long long";
        return e.is_unsigned ? "unsigned long" : "long";
    case CdbTypeKind::Float: return "float";
    case CdbTypeKind::Fixed: return "_Accum";
    case CdbTypeKind::Float16: return "_Float16";
    case CdbTypeKind::Void: return "void";
    case CdbTypeKind::Scalar: return e.is_unsigned ? "unsigned int" : "int";
    case CdbTypeKind::Struct: return "struct " + e.tag;
    case CdbTypeKind::Bitfield: return "unsigned int :" + num(e.bit_width);
    default: return "?";
    }
}

std::string type_text(const CdbType& t, size_t i, const std::string& inner)
{
    if (i >= t.chain.size()) return "?" + (inner.empty() ? std::string() : " " + inner);
    const CdbTypeElem& e = t.chain[i];
    switch (e.kind) {
    case CdbTypeKind::Pointer: {
        const bool wrap = i + 1 < t.chain.size() &&
                          (t.chain[i + 1].kind == CdbTypeKind::Function || t.chain[i + 1].kind == CdbTypeKind::Array);
        return type_text(t, i + 1, wrap ? "(*" + inner + ")" : "*" + inner);
    }
    case CdbTypeKind::Array:
        return type_text(t, i + 1, inner + "[" + (e.count > 0 ? num(e.count) : std::string()) + "]");
    case CdbTypeKind::Function:
        return type_text(t, i + 1, inner + "()");
    default:
        return base_text(t, i) + (inner.empty() ? std::string() : " " + inner);
    }
}

}  // namespace

std::string c_type_text(const CdbType& type, size_t from)
{
    CdbType t = type;
    if (from > 0) t = sliced(type, from, type.size);
    return type_text(t, 0, "");
}

CTypeLookup c_find_type(const VariableStore& store, const std::string& tag, const std::string& scope_name,
                        const std::string& module)
{
    CTypeLookup out;
    std::vector<const CRecord*> all;
    for (const CRecord& r : store.types())
        if (r.record.var.name == tag) all.push_back(&r);
    const auto pick = [&](const std::vector<const CRecord*>& c, bool* ambiguous) -> const CRecord* {
        if (c.empty()) return nullptr;
        const auto body = [](const CRecord* r) { return r->text.substr(r->text.find('$') == std::string::npos ? 0 : r->text.find('$')); };
        for (const CRecord* r : c)
            if (body(r) != body(c.front())) {
                *ambiguous = true;
                return nullptr;
            }
        return c.front();
    };
    bool ambiguous = false;
    std::vector<const CRecord*> step;
    for (const CRecord* r : all)
        if (r->record.scope_name == scope_name) step.push_back(r);
    if (const CRecord* r = pick(step, &ambiguous)) {
        out.record = r;
        return out;
    }
    if (!ambiguous) {
        step.clear();
        for (const CRecord* r : all)
            if (r->module == module) step.push_back(r);
        if (const CRecord* r = pick(step, &ambiguous)) {
            out.record = r;
            return out;
        }
    }
    if (!ambiguous) {
        if (const CRecord* r = pick(all, &ambiguous)) {
            out.record = r;
            return out;
        }
    }
    if (all.empty()) out.why = "struct " + tag + ": no definition";
    else out.why = "struct " + tag + ": " + num(static_cast<long long>(all.size())) + " definitions, none in this module and they differ";
    return out;
}

int c_leaf_size(const CFormatCtx& ctx, const std::vector<CdbTypeElem>& chain, size_t from)
{
    if (from >= chain.size()) return -1;
    const CdbTypeElem& e = chain[from];
    switch (e.kind) {
    case CdbTypeKind::Char: return 1;
    case CdbTypeKind::Int: return 2;
    case CdbTypeKind::Long: return 4;
    case CdbTypeKind::Float16: return 2;
    case CdbTypeKind::Pointer: return 2;
    case CdbTypeKind::Struct: return struct_size(ctx, e.tag);
    case CdbTypeKind::Array: {
        if (e.count <= 0) return -1;
        const int inner = c_leaf_size(ctx, chain, from + 1);
        return inner < 0 ? -1 : inner * e.count;
    }
    default: return -1;
    }
}

// -- formatting --------------------------------------------------------------

CFormatted c_format_value(const CFormatCtx& ctx, const CdbType& type, const uint8_t* bytes, size_t avail)
{
    CFormatted out;
    Formatter f{ctx, out.notes, {}};
    std::string text;
    if (f.fmt(type.chain, 0, type.size, bytes, avail, 0, text)) {
        out.ok = true;
        out.text = text;
    } else {
        out.why = f.why;
    }
    return out;
}

// -- paths -------------------------------------------------------------------

CPath c_parse_path(std::string_view s)
{
    CPath p;
    size_t i = 0;
    const auto ws = [&] {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    };
    const auto ident_start = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; };
    const auto ident_char = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    const auto ident = [&](std::string* out) {
        if (i >= s.size() || !ident_start(s[i])) return false;
        const size_t b = i;
        while (i < s.size() && ident_char(s[i])) ++i;
        *out = std::string(s.substr(b, i - b));
        return true;
    };
    const auto bad = [&](const std::string& w) {
        p.ok = false;
        p.why = w;
        return p;
    };
    ws();
    if (i < s.size() && s[i] == '*') {
        p.deref = true;
        ++i;
        ws();
    }
    // a qualifier is a run of name characters (and '.', '/', '-') followed by "::"
    {
        size_t j = i;
        while (j < s.size() && (ident_char(s[j]) || s[j] == '.' || s[j] == '/' || s[j] == '-')) ++j;
        if (j > i && j + 1 < s.size() && s[j] == ':' && s[j + 1] == ':') {
            p.qualifier = std::string(s.substr(i, j - i));
            p.primary = p.qualifier.find('.') != std::string::npos ? CPath::Primary::File : CPath::Primary::Function;
            i = j + 2;
        }
    }
    if (!ident(&p.name)) return bad("expected a variable name");
    for (;;) {
        ws();
        if (i >= s.size()) break;
        CPathStep st;
        if (s[i] == '.') {
            ++i;
            ws();
            st.kind = CPathStep::Kind::Dot;
            if (!ident(&st.name)) return bad("expected a member name after '.'");
        } else if (s[i] == '-' && i + 1 < s.size() && s[i + 1] == '>') {
            i += 2;
            ws();
            st.kind = CPathStep::Kind::Arrow;
            if (!ident(&st.name)) return bad("expected a member name after '->'");
        } else if (s[i] == '[') {
            ++i;
            ws();
            st.kind = CPathStep::Kind::Index;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) return bad("an index has no sign");
            int base = 10;
            if (i < s.size() && s[i] == '$') {
                base = 16;
                ++i;
            } else if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
                base = 16;
                i += 2;
            }
            const size_t b = i;
            unsigned long long v = 0;
            while (i < s.size() && std::isxdigit(static_cast<unsigned char>(s[i])) && (base == 16 || std::isdigit(static_cast<unsigned char>(s[i])))) {
                const char c = s[i];
                const int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(c) - 'a' + 10;
                v = v * static_cast<unsigned>(base) + static_cast<unsigned>(d);
                if (v > 0xFFFFFFFFull) return bad("the index is too large");
                ++i;
            }
            if (i == b) return bad(i >= s.size() ? "unterminated index" : "expected a number in the index");
            ws();
            if (i >= s.size() || s[i] != ']') return bad("unterminated index");
            ++i;
            st.index = static_cast<uint32_t>(v);
        } else {
            return bad(std::string("unexpected text '") + std::string(s.substr(i)) + "'");
        }
        p.steps.push_back(st);
    }
    p.ok = true;
    return p;
}

CEval c_eval_path(const CFormatCtx& ctx, const CLoc& root, bool deref, const std::vector<CPathStep>& steps)
{
    CEval out;
    CLoc cur = root;
    std::string why;
    for (const CPathStep& st : steps) {
        CLoc next;
        switch (st.kind) {
        case CPathStep::Kind::Dot:
            if (!member_loc(ctx, cur, st.name, &next, &why)) {
                out.why = why;
                return out;
            }
            break;
        case CPathStep::Kind::Arrow: {
            CLoc target;
            if (!deref_loc(ctx, cur, &target, &why)) {
                out.why = why;
                return out;
            }
            if (!member_loc(ctx, target, st.name, &next, &why)) {
                out.why = why;
                return out;
            }
            break;
        }
        case CPathStep::Kind::Index: {
            if (cur.type.chain.empty()) {
                out.why = "not an array or pointer";
                return out;
            }
            const CdbTypeElem& e = cur.type.chain[0];
            if (e.kind == CdbTypeKind::Array) {
                if (e.count <= 0 || cur.type.size <= 0 || cur.type.size % e.count != 0) {
                    out.why = "an array of unknown length";
                    return out;
                }
                if (st.index >= static_cast<uint32_t>(e.count)) {
                    out.why = "index " + unum(st.index) + " out of range [0, " + num(e.count) + ")";
                    return out;
                }
                const int esz = cur.type.size / e.count;
                next.type = sliced(cur.type, 1, esz);
                next.addr = cur.addr + st.index * static_cast<uint32_t>(esz);
            } else if (e.kind == CdbTypeKind::Pointer) {
                CLoc target;
                if (!deref_loc(ctx, cur, &target, &why)) {
                    out.why = why;
                    return out;
                }
                next.type = target.type;
                next.addr = target.addr + st.index * static_cast<uint32_t>(target.type.size);
            } else {
                out.why = "not an array or pointer";
                return out;
            }
            break;
        }
        }
        cur = next;
    }
    if (deref) {   // C: postfix operators bind tighter, so `*x[1]` is `*(x[1])`
        CLoc next;
        if (!deref_loc(ctx, cur, &next, &why)) {
            out.why = why;
            return out;
        }
        cur = next;
    }
    out.ok = true;
    out.loc = cur;
    return out;
}

CFormatted c_read_loc(const CFormatCtx& ctx, const CLoc& loc)
{
    CFormatted out;
    if (loc.type.size <= 0) {
        out.why = "the size of the object is not known";
        return out;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(loc.type.size));
    size_t want = buf.size();
    if (loc.end > loc.addr && loc.addr + want > loc.end) want = loc.end - loc.addr;   // the overhang of a U2 member is zeros
    if (!ctx.read || !ctx.read(loc.addr, want, buf.data())) {
        out.why = "unreadable memory at $" + hexw(loc.addr, 4);
        return out;
    }
    out = c_format_value(ctx, loc.type, buf.data(), buf.size());
    if (out.ok && !loc.note.empty() && std::find(out.notes.begin(), out.notes.end(), loc.note) == out.notes.end())
        out.notes.push_back(loc.note);
    return out;
}

// -- scope, registers, fastcall ----------------------------------------------

std::vector<CLocalRef> c_visible_locals(const VariableStore& store, int function, const CScope* scope)
{
    std::vector<CLocalRef> params, locals, statics;
    if (function < 0 || static_cast<size_t>(function) >= store.functions().size()) return {};
    const CFunction& fn = store.functions()[static_cast<size_t>(function)];

    std::vector<const CVariable*> mine;
    for (const CVariable& v : store.variables())
        if (v.function == function && v.record.hidden == CdbHidden::None) mine.push_back(&v);

    // A parameter: level 0 (sccz80, 80cc), a stack offset >= 4, or (zsdcc) a block below the function's first body block
    // (the block of the scope entry at the function's entry, level 0), which is where its register and fastcall parameters sit.
    const bool entry_scope = !fn.scopes.empty() && fn.scopes.front().known && fn.scopes.front().offset == 0 && fn.scopes.front().level == 0;
    const int first_body = entry_scope ? fn.scopes.front().block : -1;
    const auto is_param = [&](const CdbVar& cv) {
        if (cv.space == 'E') return false;
        if (cv.level == 0) return true;
        if (cv.space == 'B' && cv.stack_offset() >= 4) return true;
        return first_body > 0 && cv.block < first_body;
    };

    // The blocks: each local's own (level, block); a block that holds no local takes its level from the first scope entry
    // (in offset order) that names it: sccz80 numbers blocks in pre-order, so a block just opened is the last opened one.
    bool deep_local = false;
    std::map<int, int> level_of;   // block -> level
    for (const CVariable* v : mine) {
        const CdbVar& cv = v->record.var;
        if (cv.space == 'E' || is_param(cv)) continue;
        level_of.emplace(cv.block, cv.level);
        deep_local = deep_local || cv.level >= 2;
    }
    bool block_data = false;
    for (const CScope& sc : fn.scopes) {
        if (!sc.known) continue;
        block_data = block_data || sc.level > 0;
        if (sc.level > 0) level_of.emplace(sc.block, sc.level);
    }
    const bool unknown = !scope || !scope->known || (deep_local && !block_data);

    std::set<std::pair<int, int>> open;
    if (!unknown) {
        const auto below = [&](int level, int limit, bool inclusive) {
            int best = -1;
            for (const auto& kv : level_of)
                if (kv.second == level && (inclusive ? kv.first <= limit : kv.first < limit)) best = std::max(best, kv.first);
            return best;
        };
        int blk = below(scope->level, scope->block, true);
        for (int lv = scope->level; lv >= 1 && blk >= 0; --lv) {
            open.insert({lv, blk});
            blk = below(lv - 1, blk, false);
        }
    }

    for (const CVariable* v : mine) {
        const CdbVar& cv = v->record.var;
        CLocalRef r;
        r.var = v;
        if (cv.space == 'E') {
            r.kind = CVarKind::StaticLocal;
            statics.push_back(r);
        } else if (is_param(cv)) {
            r.kind = CVarKind::Param;
            params.push_back(r);
        } else {
            r.kind = CVarKind::Local;
            if (cv.level >= 2) {
                if (unknown) r.scope_unknown = true;
                else if (!open.count({cv.level, cv.block})) continue;
            }
            locals.push_back(r);
        }
    }
    const auto pos = [](const CLocalRef& r) { return r.var->record.var.space == 'B' ? r.var->record.var.stack_offset() : 1 << 20; };
    std::stable_sort(params.begin(), params.end(), [&](const CLocalRef& a, const CLocalRef& b) { return pos(a) < pos(b); });
    std::vector<CLocalRef> out = params;
    out.insert(out.end(), locals.begin(), locals.end());
    out.insert(out.end(), statics.begin(), statics.end());
    return out;
}

CRegRead c_read_register_local(const CdbVar& var, const CRegs* regs)
{
    CRegRead out;
    if (var.regs.empty()) {
        out.why = "optimised out";
        return out;
    }
    if (!regs) {
        out.why = "not available (register, outer frame)";
        return out;
    }
    std::string pair;
    for (const std::string& n : var.regs) {
        uint8_t v = 0;
        if (n == "a") v = static_cast<uint8_t>(regs->af >> 8);
        else if (n == "b") v = static_cast<uint8_t>(regs->bc >> 8);
        else if (n == "c") v = static_cast<uint8_t>(regs->bc);
        else if (n == "d") v = static_cast<uint8_t>(regs->de >> 8);
        else if (n == "e") v = static_cast<uint8_t>(regs->de);
        else if (n == "h") v = static_cast<uint8_t>(regs->hl >> 8);
        else if (n == "l") v = static_cast<uint8_t>(regs->hl);
        else if (n == "ixh") v = static_cast<uint8_t>(regs->ix >> 8);
        else if (n == "ixl") v = static_cast<uint8_t>(regs->ix);
        else if (n == "iyh") v = static_cast<uint8_t>(regs->iy >> 8);
        else if (n == "iyl") v = static_cast<uint8_t>(regs->iy);
        else {
            out.bytes.clear();
            out.why = "unknown register '" + n + "'";
            return out;
        }
        out.bytes.push_back(v);
        std::string up = n;
        for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        pair.insert(0, up);
    }
    out.ok = true;
    out.note = "in " + pair + "; may be stale outside its live range";
    return out;
}

std::string c_param_location_unknown(CConv conv, const CdbVar& var)
{
    if (conv == CConv::ListedFastcall && var.space == 'B' && var.stack_offset() >= 0)
        return "location unknown (z88dk U1: sccz80 records a fastcall parameter at its nominal slot)";
    return "";
}

}  // namespace dbg
}  // namespace jnext
