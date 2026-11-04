#include "debug/c_value.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

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

// z88dk's debug record gives a float's SIZE and no format (z88dk#3236). The map says which library the program links: the
// 4-byte formats of mbf32 and dai32 (and mbf64's, bbc's) are not IEEE, so reading their bytes as one is a silent wrong value.
// math32, am9511, math48 and a map that names no library are decided by the size, as before.
std::string float_refusal(const CFormatCtx& ctx)
{
    if (!ctx.store) return "";
    const std::string& lib = ctx.store->float_library();
    if (lib == "mbf32" || lib == "mbf64" || lib == "bbc" || lib == "dai32")
        return "float format " + lib + " is not read: the record does not say it (z88dk#3236)";
    return "";
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
            if (!float_refusal(ctx).empty()) return fail(float_refusal(ctx));
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
                // a char array reads as a string when every byte that is not a NUL is text (GDB's null-stop off: GH #437 owner decisions
                // CVALF-16 and 2026-11-04): EVERY byte is shown, a NUL as \0, so `{'h', 'i', 0, 0}` is "hi\0\0" and `{'h', 'i', 0, 'l', 'o', 0}`
                // is "hi\0lo\0". An unsigned char array of numbers (`uint8_t btable[4] = {10, 20, 30, 40}`, a union's byte view, a table
                // that starts at 0) is a list of numbers, as the CDB cannot tell them apart
                bool text = true;
                for (size_t k = 0; k < static_cast<size_t>(size); ++k)
                    text = text && (b[k] == 0 || (b[k] >= 0x20 && b[k] < 0x7F) || b[k] == '\t' || b[k] == '\n' || b[k] == '\r');
                if (text) {
                    out = string_of(b, static_cast<size_t>(size), false);
                    return true;
                }
            }
            out = "{";
            const size_t n = static_cast<size_t>(e.count);
            for (size_t k = 0; k < n && k < kMaxElements; ++k) {
                std::string one;
                if (is_char(ch, i + 1)) {   // numbers, without the 'c' of a printable one: it is a list of bytes here
                    const uint8_t byte = b[k * esz];
                    one = num(ch[i + 1].is_unsigned ? static_cast<long long>(byte) : static_cast<long long>(static_cast<int8_t>(byte)));
                } else if (!fmt(ch, i + 1, static_cast<int>(esz), b + k * esz, esz, depth + 1, one)) {
                    return false;
                }
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
            const unsigned long long field = (u >> e.bit_start) & mask;
            if (e.is_unsigned || e.bit_width <= 0 || e.bit_width >= 64) out = unum(field);
            else out = num(static_cast<long long>(field << (64 - e.bit_width)) >> (64 - e.bit_width));   // `:S`: sign-extended (GH #437)
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
    // a declared object of the context's own module first (and of the module whose `T:` record of this scope defines the tag), as
    // c_find_type() prefers its copy, then any in the store
    std::set<std::string> modules{ctx.module};
    for (const CRecord& r : ctx.store->types())
        if (r.record.var.name == tag && !ctx.scope_name.empty() && r.record.scope_name == ctx.scope_name) modules.insert(r.module);
    for (int pass = 0; pass < 2; ++pass) {
        const auto mine = [&](const std::string& module) { return pass == 1 || modules.count(module) != 0; };
        for (const CVariable& v : ctx.store->variables())
            if (mine(v.module) && exact(v.record.var.type)) return v.record.var.type.size;
        for (const CRecord& r : ctx.store->types())
            if (mine(r.module))
                for (const CdbMember& m : r.record.members)
                    if (exact(m.var.type)) return m.var.type.size;
    }
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

const CReader& reader_for(const CFormatCtx& ctx, bool rooted)
{
    return rooted && ctx.read_root ? ctx.read_root : ctx.read;
}

bool read16(const CReader& read, uint32_t addr, uint16_t* v)
{
    uint8_t b[2] = {0, 0};
    if (!read || !read(addr, 2, b)) return false;
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
    if (in.ptr_value) {
        p = *in.ptr_value;
    } else if (!read16(reader_for(ctx, in.rooted), in.addr, &p)) {
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
    out->rooted = false;   // the target is reached through the CPU view
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
        out->rooted = in.rooted;
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
    case CdbTypeKind::Bitfield: return (e.is_unsigned ? "unsigned int :" : "int :") + num(e.bit_width);   // `:S` is a signed field (GH #438)
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
    // a qualifier is a run of name characters (and '.', '/', '-') followed by "::"; `file.c::fn::name` has two
    const auto qualifier = [&](std::string* out) {
        size_t j = i;
        while (j < s.size() && (ident_char(s[j]) || s[j] == '.' || s[j] == '/' || s[j] == '-')) ++j;
        if (j > i && j + 1 < s.size() && s[j] == ':' && s[j + 1] == ':') {
            *out = std::string(s.substr(i, j - i));
            i = j + 2;
            return true;
        }
        return false;
    };
    std::string q1, q2;
    if (qualifier(&q1)) {
        if (qualifier(&q2)) {
            if (q1.find('.') == std::string::npos) return bad("`" + q1 + "::" + q2 + "::`: a file name (with its extension) comes first");
            p.file = q1;
            p.qualifier = q2;
            p.primary = CPath::Primary::Function;
        } else {
            p.qualifier = q1;
            p.primary = q1.find('.') != std::string::npos ? CPath::Primary::File : CPath::Primary::Function;
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
                next.rooted = cur.rooted;
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

namespace {

// The members of the struct at `s` that a path can name, in record order, [first, first + max) of them.
bool struct_children(const CFormatCtx& ctx, const CLoc& s, size_t first, size_t max, const char* arrow, CChildren* out)
{
    if (!ctx.store) {
        out->why = "no type information";
        return false;
    }
    const CTypeLookup lk = c_find_type(*ctx.store, s.type.chain[0].tag, ctx.scope_name, ctx.module);
    if (!lk.record) {
        out->why = lk.why;
        return false;
    }
    std::vector<std::string> names;
    for (const CdbMember& m : lk.record->record.members)
        if (m.hidden == CdbHidden::None && !m.var.name.empty()) names.push_back(m.var.name);
    out->total = names.size();
    for (size_t i = first; i < names.size() && out->items.size() < max; ++i) {
        CChild c;
        c.label = names[i];
        c.suffix = std::string(arrow) + names[i];
        std::string why;
        if (!member_loc(ctx, s, names[i], &c.loc, &why)) {
            out->why = why;
            return false;
        }
        out->items.push_back(c);
    }
    return true;
}

}  // namespace

CChildren c_children(const CFormatCtx& ctx, const CLoc& loc, size_t first, size_t max)
{
    CChildren out;
    if (loc.type.chain.empty()) {
        out.ok = true;
        return out;
    }
    const CdbTypeElem& e = loc.type.chain[0];
    if (e.kind == CdbTypeKind::Struct) {
        out.ok = struct_children(ctx, loc, first, max, ".", &out);
        if (!out.ok) {
            out.total = 0;
            out.items.clear();
        }
        return out;
    }
    if (e.kind == CdbTypeKind::Array) {
        if (e.count <= 0 || loc.type.size <= 0 || loc.type.size % e.count != 0) {
            out.why = "an array of unknown length";
            return out;
        }
        const int esz = loc.type.size / e.count;
        out.total = static_cast<size_t>(e.count);
        for (size_t i = first; i < out.total && out.items.size() < max; ++i) {
            CChild c;
            c.label = "[" + unum(i) + "]";
            c.suffix = c.label;
            c.loc.type = sliced(loc.type, 1, esz);
            c.loc.addr = loc.addr + static_cast<uint32_t>(i) * static_cast<uint32_t>(esz);
            c.loc.rooted = loc.rooted;
            out.items.push_back(c);
        }
        out.ok = true;
        return out;
    }
    if (e.kind == CdbTypeKind::Pointer) {
        if (loc.type.chain.size() > 1 &&
            (loc.type.chain[1].kind == CdbTypeKind::Void || loc.type.chain[1].kind == CdbTypeKind::Function)) {
            out.ok = true;   // void * and a function pointer have nothing to look at
            return out;
        }
        CLoc target;
        std::string why;
        if (!deref_loc(ctx, loc, &target, &why)) {
            out.why = why;
            return out;
        }
        if (!target.type.chain.empty() && target.type.chain[0].kind == CdbTypeKind::Struct) {
            out.ok = struct_children(ctx, target, first, max, "->", &out);
            if (!out.ok) {
                out.total = 0;
                out.items.clear();
            }
            return out;
        }
        out.total = 1;
        if (first == 0 && max > 0) {
            CChild c;
            c.label = "*";
            c.suffix = "[0]";
            c.loc = target;
            out.items.push_back(c);
        }
        out.ok = true;
        return out;
    }
    out.ok = true;   // a scalar
    return out;
}

CFormatted c_read_loc(const CFormatCtx& ctx, const CLoc& loc, std::vector<uint8_t>* bytes)
{
    CFormatted out;
    if (loc.type.size <= 0) {
        // an `{-1}DA-1x` record (sccz80 before z88dk#3229, 80cc's static and global arrays sized by their initialiser): the compiler
        // wrote no size, so there is nothing to read (GH #438)
        const bool unsized = !loc.type.chain.empty() && loc.type.chain[0].kind == CdbTypeKind::Array && loc.type.chain[0].count < 0;
        out.why = unsized ? "an array of unknown length: its debug record has no size" : "the size of the object is not known";
        return out;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(loc.type.size));
    size_t want = buf.size();
    if (loc.end > loc.addr && loc.addr + want > loc.end) want = loc.end - loc.addr;   // the overhang of a U2 member is zeros
    const CReader& rd = reader_for(ctx, loc.rooted);
    if (!rd || !rd(loc.addr, want, buf.data())) {
        out.why = "unreadable memory at $" + hexw(loc.addr, 4);
        return out;
    }
    if (bytes) *bytes = buf;
    out = c_format_value(ctx, loc.type, buf.data(), buf.size());
    if (out.ok && !loc.note.empty() && std::find(out.notes.begin(), out.notes.end(), loc.note) == out.notes.end())
        out.notes.push_back(loc.note);
    return out;
}

// -- scope, registers, fastcall ----------------------------------------------


// The replay of a function's trail (GH #438): stack[level] is the block open at that level, or kSkip for a level with no block (the level a
// `for` header raises without opening one, in every sccz80 before z88dk#3254). A record at a level drops every level above it. A record that
// opens a block higher than any seen (`b > max`) fills the unset levels below it with the greatest unseen block of that level in between (a
// block opened and closed without a line of its own), else kSkip: before z88dk#3227 the number was the last one OPENED, and when a record
// names a block the stack does not hold at its level (`stack[level] != b`: counted in *disagreements) the stack is kept. After z88dk#3254 a
// `for` opens a block at the raised level and the same replay holds it.
constexpr int kSkipBlock = -1;
void replay_trail(const CFunction& fn, const std::map<int, int>& blev, uint32_t upto, std::map<int, int>& stack, int* disagreements)
{
    int max_block = 0;
    for (const CScope& e : fn.trail) {
        if (e.offset > upto) break;
        if (!e.known) continue;
        const int lv = e.level, b = e.block;
        stack.erase(stack.upper_bound(lv), stack.end());
        if (lv == 0) continue;
        if (b > max_block) {
            for (int l = 1; l < lv; ++l) {
                if (stack.count(l)) continue;
                int best = kSkipBlock;
                for (const auto& kv : blev)
                    if (kv.second == l && kv.first > max_block && kv.first < b) best = std::max(best, kv.first);
                stack[l] = best;
            }
            stack[lv] = b;
            max_block = b;
        } else if (!stack.count(lv)) {
            const auto it = blev.find(b);
            stack[lv] = it != blev.end() && it->second == lv ? b : kSkipBlock;
            for (int l = 1; l < lv; ++l)
                if (!stack.count(l)) stack[l] = kSkipBlock;
        }
        if (disagreements && stack[lv] != b) ++*disagreements;
    }
}

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

    // GH #438: an sccz80 function with a trail: the open blocks are the replay of the trail up to the PC's (replay_trail()).
    constexpr int kSkip = kSkipBlock;
    const bool use_walk = !unknown && fn.compiler == CCompiler::Sccz80 && !fn.trail.empty();
    std::map<int, int> blev;      // block -> level
    std::map<int, int> stack;     // level -> block | kSkip
    if (use_walk) {
        for (const CScope& e : fn.trail)
            if (e.known && e.level > 0) blev.emplace(e.block, e.level);
        for (const CVariable* v : mine) {
            const CdbVar& cv = v->record.var;
            if (cv.space != 'E' && !is_param(cv)) blev.emplace(cv.block, cv.level);
        }
        replay_trail(fn, blev, scope->offset, stack, nullptr);
    }
    const auto walk_visible = [&](const CdbVar& cv) {
        const auto it = blev.find(cv.block);
        const int bl = it != blev.end() ? it->second : cv.level;   // the level of the block the record names: below the record's own for a `for` variable
        const auto at = [&](int l) { const auto s2 = stack.find(l); return s2 == stack.end() ? -2 : s2->second; };
        if (bl == cv.level) return at(cv.level) == cv.block;
        if (bl > cv.level) return false;
        if (at(bl) != cv.block) return false;
        for (int l = bl + 1; l <= cv.level; ++l)
            if (at(l) != kSkip) return false;
        return true;
    };

    std::set<std::pair<int, int>> open;
    if (!unknown && !use_walk) {
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
                else if (use_walk ? !walk_visible(cv) : !open.count({cv.level, cv.block})) continue;
            }
            locals.push_back(r);
        }
    }
    const auto pos = [](const CLocalRef& r) { return r.var->record.var.space == 'B' ? r.var->record.var.stack_offset() : 1 << 20; };
    std::stable_sort(params.begin(), params.end(), [&](const CLocalRef& a, const CLocalRef& b) { return pos(a) < pos(b); });
    std::vector<CLocalRef> out = params;
    out.insert(out.end(), locals.begin(), locals.end());
    out.insert(out.end(), statics.begin(), statics.end());
    // GH #438: stack variables that share bytes (two blocks that reuse a slot where the compiler's numbering cannot tell which is open, or 80cc's
    // register-parameter record): both are marked, each naming the other; neither is hidden
    const auto span = [](const CLocalRef& r) { return static_cast<int>(std::max(1, r.var->record.var.type.size)); };
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i].var->record.var.space != 'B') continue;
        const int ai = out[i].var->record.var.stack_offset();
        for (size_t j = 0; j < out.size(); ++j) {
            if (i == j || out[j].var->record.var.space != 'B') continue;
            const int aj = out[j].var->record.var.stack_offset();
            if (ai < aj + span(out[j]) && aj < ai + span(out[i]))
                out[i].shares += (out[i].shares.empty() ? "" : ", ") + out[j].var->c_name;
        }
    }
    return out;
}

int c_trail_disagreements(const VariableStore& store, int function)
{
    if (function < 0 || static_cast<size_t>(function) >= store.functions().size()) return 0;
    const CFunction& fn = store.functions()[static_cast<size_t>(function)];
    std::map<int, int> blev, stack;
    for (const CScope& e : fn.trail)
        if (e.known && e.level > 0) blev.emplace(e.block, e.level);
    for (const CVariable& v : store.variables()) {
        const CdbVar& cv = v.record.var;
        if (v.function == function && cv.space != 'E' && cv.level > 0 && cv.stack_offset() < 4) blev.emplace(cv.block, cv.level);
    }
    int n = 0;
    replay_trail(fn, blev, UINT32_MAX, stack, &n);
    return n;
}

std::string c_shares_note(const std::string& others)
{
    return "shares its stack slot with " + others + ": only one of them holds its value";
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

bool c_fastcall_records_nominal(const VariableStore& store, int function, bool prologue_fastcall)
{
    if (function < 0 || static_cast<size_t>(function) >= store.functions().size()) return false;
    const CFunction& fn = store.functions()[static_cast<size_t>(function)];
    // a listing that names the function and does not flag it is evidence it is NOT fastcall: the prologue then proves nothing
    if (fn.conv != CConv::ListedFastcall && !(prologue_fastcall && fn.conv == CConv::Unlisted)) return false;
    bool any = false;
    for (const CVariable& v : store.variables()) {
        const CdbVar& cv = v.record.var;
        if (v.function != function || v.record.hidden != CdbHidden::None || cv.space != 'B' || cv.level != 0) continue;
        if (cv.stack_offset() < 0) return false;   // a fixed sccz80 or 80cc: the register parameter is recorded below the frame
        any = true;
    }
    return any;
}

std::string c_param_location_unknown(CConv conv, const CdbVar& var)
{
    if (conv == CConv::ListedFastcall && var.space == 'B' && var.stack_offset() >= 0)
        return kCU1Text;
    return "";
}

}  // namespace dbg
}  // namespace jnext

namespace jnext {
namespace dbg {

std::optional<int64_t> c_integer_value(const CdbType& type, const uint8_t* b, size_t avail)
{
    if (type.chain.empty()) return std::nullopt;
    const CdbTypeElem& e = type.chain[0];
    size_t n = 0;
    switch (e.kind) {
    case CdbTypeKind::Char: n = 1; break;
    case CdbTypeKind::Int: n = type.size > 0 ? static_cast<size_t>(type.size) : 2; break;
    case CdbTypeKind::Long:
    case CdbTypeKind::Scalar: n = type.size > 0 ? static_cast<size_t>(type.size) : 0; break;
    case CdbTypeKind::Pointer: n = 2; break;
    case CdbTypeKind::Bitfield: n = static_cast<size_t>(std::max(1, type.size)); break;
    default: return std::nullopt;
    }
    if (n == 0 || n > 8 || avail < n) return std::nullopt;
    unsigned long long u = 0;
    for (size_t k = n; k-- > 0;) u = (u << 8) | b[k];
    if (e.kind == CdbTypeKind::Bitfield) {
        const unsigned long long mask = e.bit_width >= 64 ? ~0ull : ((1ull << e.bit_width) - 1);
        const unsigned long long field = (u >> e.bit_start) & mask;
        if (e.is_unsigned || e.bit_width <= 0 || e.bit_width >= 64) return static_cast<int64_t>(field);
        return static_cast<int64_t>(field << (64 - e.bit_width)) >> (64 - e.bit_width);   // `:S`: sign-extended (GH #437)
    }
    if (e.kind == CdbTypeKind::Pointer || e.is_unsigned || n == 8) return static_cast<int64_t>(u);
    const unsigned shift = 64u - 8u * static_cast<unsigned>(n);
    return static_cast<int64_t>(u << shift) >> shift;
}

CInt32 c_int32_scalar(const CLoc& loc, std::optional<int64_t> integer)
{
    CInt32 r;
    const CdbType& t = loc.type;
    if (t.chain.empty()) {
        r.why = "the object has no type";
        return r;
    }
    const std::string text = c_type_text(t);
    switch (t.chain[0].kind) {
    case CdbTypeKind::Float:
    case CdbTypeKind::Float16:
    case CdbTypeKind::Fixed:
        r.why = text + " is not an integer";
        return r;
    case CdbTypeKind::Struct:
    case CdbTypeKind::Array:
        r.why = text + " is an aggregate, not a scalar: name a member or an element";
        return r;
    case CdbTypeKind::Void:
    case CdbTypeKind::Function:
        r.why = text + " has no value";
        return r;
    case CdbTypeKind::Char:
    case CdbTypeKind::Int:
    case CdbTypeKind::Long:
    case CdbTypeKind::Scalar:
    case CdbTypeKind::Bitfield:
    case CdbTypeKind::Pointer:
        break;
    }
    if (t.size > 4) {
        r.why = "a " + std::to_string(t.size) + "-byte integer does not fit the script's 32 bits";
        return r;
    }
    if (!loc.note.empty()) {
        r.why = loc.note;
        return r;
    }
    if (!integer) {
        r.why = "no integer value was read";
        return r;
    }
    r.ok = true;
    r.value = static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(*integer)));
    return r;
}

std::optional<double> c_float_value(const CdbType& type, const uint8_t* b, size_t avail)
{
    if (type.chain.empty()) return std::nullopt;
    const CdbTypeElem& e = type.chain[0];
    if (e.kind == CdbTypeKind::Float && type.size == 6 && avail >= 6) {
        const int ex = b[5];
        if (ex == 0) return 0.0;
        const unsigned long long low = static_cast<unsigned long long>(b[0]) | static_cast<unsigned long long>(b[1]) << 8 |
                                       static_cast<unsigned long long>(b[2]) << 16 | static_cast<unsigned long long>(b[3]) << 24 |
                                       static_cast<unsigned long long>(b[4] & 0x7F) << 32;
        double v = std::ldexp(static_cast<double>((1ull << 39) | low), ex - 0x80 - 40);
        return (b[4] & 0x80) ? -v : v;
    }
    if (e.kind == CdbTypeKind::Float && type.size == 4 && avail >= 4) {
        const uint32_t u = static_cast<uint32_t>(b[0]) | static_cast<uint32_t>(b[1]) << 8 | static_cast<uint32_t>(b[2]) << 16 |
                           static_cast<uint32_t>(b[3]) << 24;
        float f;
        std::memcpy(&f, &u, sizeof f);
        return static_cast<double>(f);
    }
    if (e.kind == CdbTypeKind::Float16 && avail >= 2) {
        const int h = b[0] | b[1] << 8;
        const int e5 = (h >> 10) & 0x1F, frac = h & 0x3FF;
        double v;
        if (e5 == 0) v = std::ldexp(static_cast<double>(frac), -24);
        else if (e5 == 31) v = frac ? std::nan("") : HUGE_VAL;
        else v = std::ldexp(static_cast<double>(1024 + frac), e5 - 25);
        return (h >> 15) ? -v : v;
    }
    return std::nullopt;
}

}  // namespace dbg
}  // namespace jnext

// -- GH #437: the encoder of a written value --------------------------------------------------------------------------

namespace jnext {
namespace dbg {

namespace {

struct IntText {
    bool ok = false;
    bool neg = false;
    bool overflow = false;   // the magnitude does not fit 64 bits
    unsigned long long mag = 0;
};

// `[+-]digits`, `[+-]0x<hex>`, `[+-]$<hex>` or `'c'` (one printable ASCII character but `'` and `\`), all of `t`.
IntText parse_int_text(std::string_view t)
{
    IntText r;
    if (t.size() == 3 && t[0] == '\'' && t[2] == '\'') {
        const unsigned char c = static_cast<unsigned char>(t[1]);
        if (c >= 0x20 && c < 0x7F && c != '\'' && c != '\\') {
            r.ok = true;
            r.mag = c;
        }
        return r;
    }
    size_t i = 0;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) r.neg = t[i++] == '-';
    unsigned base = 10;
    if (t.size() - i >= 2 && t[i] == '0' && (t[i + 1] == 'x' || t[i + 1] == 'X')) {
        base = 16;
        i += 2;
    } else if (i < t.size() && t[i] == '$') {
        base = 16;
        ++i;
    }
    if (i >= t.size()) return IntText{};
    unsigned long long m = 0;
    for (; i < t.size(); ++i) {
        const char c = t[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
        else return IntText{};
        if (r.overflow || m > (~0ull - d) / base) r.overflow = true;
        else m = m * base + d;
    }
    r.ok = true;
    r.mag = m;
    return r;
}

std::string_view trim_blanks(std::string_view t)
{
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.remove_prefix(1);
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.remove_suffix(1);
    return t;
}

// `[+-]digits[.digits][e[+-]digits]` (or `.digits`), all of `t`. `nonzero`: some mantissa digit is not a 0.
bool decimal_float_syntax(std::string_view t, bool* nonzero)
{
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    size_t i = 0, nd = 0;
    *nonzero = false;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
    for (; i < t.size() && digit(t[i]); ++i, ++nd)
        if (t[i] != '0') *nonzero = true;
    if (i < t.size() && t[i] == '.') {
        for (++i; i < t.size() && digit(t[i]); ++i, ++nd)
            if (t[i] != '0') *nonzero = true;
    }
    if (nd == 0) return false;
    if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
        ++i;
        if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
        size_t ne = 0;
        for (; i < t.size() && digit(t[i]); ++i, ++ne) {
        }
        if (ne == 0) return false;
    }
    return i == t.size();
}

// `nan`, `inf`, `infinity`, with an optional sign, in any case.
bool nonfinite_word(std::string_view t)
{
    if (!t.empty() && (t[0] == '+' || t[0] == '-')) t.remove_prefix(1);
    std::string w;
    for (const char c : t) w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return w == "nan" || w == "inf" || w == "infinity";
}

// A decimal number of all of `t`, read through a stream imbued with the classic locale (no host locale: QApplication calls
// setlocale(LC_ALL, ""), and std::from_chars for floating point is missing from libc++ before LLVM 20). False: not a decimal
// number. `out_of_range`: it overflows `F`, or is not zero and underflows to it.
template <typename F>
bool float_from_text(std::string_view t, F* out, bool* out_of_range)
{
    *out_of_range = false;
    bool nonzero = false;
    if (!decimal_float_syntax(t, &nonzero)) return false;
    std::istringstream is{std::string(t)};
    is.imbue(std::locale::classic());
    F v{};
    is >> v;
    if (is.fail() || (nonzero && v == 0)) {
        *out_of_range = true;
        return true;
    }
    *out = v;
    return true;
}

// Does `neg`/`mag` fit an integer of `bits` bits, unsigned or two's-complement signed?
bool int_fits(bool neg, unsigned long long mag, bool overflow, int bits, bool is_unsigned)
{
    if (overflow) return false;
    if (is_unsigned) return (!neg || mag == 0) && (bits >= 64 || mag <= (1ull << bits) - 1);
    const unsigned long long half = 1ull << (bits - 1);
    return neg ? mag <= half : mag <= half - 1;
}

std::string int_range_text(int bits, bool is_unsigned)
{
    if (is_unsigned) return "0.." + unum(bits >= 64 ? ~0ull : (1ull << bits) - 1);
    return "-" + unum(1ull << (bits - 1)) + ".." + unum((1ull << (bits - 1)) - 1);
}

// The two's-complement pattern of neg/mag, low `bits` bits.
unsigned long long int_pattern(bool neg, unsigned long long mag, int bits)
{
    const unsigned long long u = neg ? 0ull - mag : mag;
    return bits >= 64 ? u : u & ((1ull << bits) - 1);
}

void put_le(std::vector<uint8_t>& out, unsigned long long u, size_t n)
{
    for (size_t k = 0; k < n; ++k) out.push_back(static_cast<uint8_t>(u >> (8 * k)));
}

// z88dk's 48-bit: five mantissa bytes (bit 39 = the sign), then the biased exponent; m = 0.1xxx binary, rounded to nearest.
bool encode_float48(double d, std::vector<uint8_t>& out, std::string* why)
{
    if (d == 0) {
        out.assign(6, 0);
        return true;
    }
    int ex = 0;
    const double m = std::frexp(std::fabs(d), &ex);   // [0.5, 1)
    long long mant = std::llround(std::ldexp(m, 40));
    if (mant >= (1ll << 40)) {
        mant >>= 1;
        ++ex;
    }
    const int biased = ex + 0x80;
    if (biased > 255) {
        *why = "out of range for float (z88dk 48-bit)";
        return false;
    }
    if (biased < 1) {
        *why = "too small for float (z88dk 48-bit): it would underflow";
        return false;
    }
    unsigned long long low = static_cast<unsigned long long>(mant) & ((1ull << 39) - 1);
    if (d < 0) low |= 1ull << 39;
    out.clear();
    put_le(out, low, 5);
    out.push_back(static_cast<uint8_t>(biased));
    return true;
}

// IEEE 754 binary16 of a finite float, nearest even. False: it rounds to infinity or to zero.
bool encode_float16(float f, uint16_t* bits, std::string* why)
{
    const double a = std::fabs(static_cast<double>(f));
    const unsigned sign = std::signbit(f) ? 0x8000u : 0u;
    if (a == 0) {
        *bits = static_cast<uint16_t>(sign);
        return true;
    }
    if (a >= 65520.0) {
        *why = "out of range for binary16 (65504 is the largest)";
        return false;
    }
    if (a < std::ldexp(1.0, -14)) {
        const long long q = std::llrint(std::ldexp(a, 24));   // current rounding mode: nearest even
        if (q == 0) {
            *why = "too small for binary16: it would underflow";
            return false;
        }
        *bits = static_cast<uint16_t>(sign | static_cast<unsigned>(q));
        return true;
    }
    int ex = 0;
    const double m = std::frexp(a, &ex);
    long long sig = std::llrint(std::ldexp(m, 11));   // [1024, 2048]
    if (sig == 2048) {
        sig = 1024;
        ++ex;
    }
    *bits = static_cast<uint16_t>(sign | static_cast<unsigned>(ex - 1 + 15) << 10 | static_cast<unsigned>(sig - 1024));
    return true;
}

}  // namespace

CEncoded c_encode_value(const CdbType& type, const CNewValue& v, const uint8_t* current, size_t avail)
{
    CEncoded out;
    const auto refuse = [&out](std::string why) {
        out.ok = false;
        out.why = std::move(why);
        out.bytes.clear();
        return out;
    };
    if (type.chain.empty()) return refuse("the object has no type");
    const CdbTypeElem& e = type.chain[0];
    const std::string tt = c_type_text(type);
    const bool from_text = v.from == CNewValue::From::Text;
    const std::string text = from_text ? std::string(trim_blanks(v.text)) : num(v.int32);
    const auto out_of_range = [&](int bits, bool uns) {
        return refuse("'" + text + "' is out of range for " + tt + " (" + int_range_text(bits, uns) + ")");
    };

    switch (e.kind) {
    case CdbTypeKind::Struct:
    case CdbTypeKind::Array:
        return refuse(tt + " is an aggregate: write a member or an element");
    case CdbTypeKind::Void:
    case CdbTypeKind::Function:
        return refuse("a function or void is not a value");
    case CdbTypeKind::Fixed:
        return refuse("fixed-point layout not decoded");
    default:
        break;
    }
    if (type.size <= 0) return refuse("the size of the object is not known");

    if (e.kind == CdbTypeKind::Float || e.kind == CdbTypeKind::Float16) {
        if (!from_text) return refuse(tt + " is not an integer");
        const bool is48 = e.kind == CdbTypeKind::Float && type.size == 6;
        const bool is32 = e.kind == CdbTypeKind::Float && type.size == 4;
        if (e.kind == CdbTypeKind::Float && !is48 && !is32) return refuse("a float of " + num(type.size) + " bytes is not supported");
        if (nonfinite_word(text)) return refuse("'" + text + "' is not a finite number");
        double d = 0;
        float f = 0;
        bool range = false;
        bool parsed;
        if (is48) {
            parsed = float_from_text<double>(text, &d, &range);
        } else {
            parsed = float_from_text<float>(text, &f, &range);
        }
        if (!parsed) {
            const IntText it = parse_int_text(text);   // 0x10, $10, 'c'
            if (!it.ok) return refuse("'" + text + "' is not a number");
            d = it.overflow ? HUGE_VAL : static_cast<double>(it.mag);
            if (it.neg) d = -d;
            f = static_cast<float>(d);
            range = std::isinf(d);
        }
        if (range) return refuse("'" + text + "' is out of range for " + tt);
        std::string why;
        if (is48) {
            if (!encode_float48(d, out.bytes, &why)) return refuse("'" + text + "' is " + why);
        } else if (is32) {
            uint32_t u;
            std::memcpy(&u, &f, sizeof u);
            put_le(out.bytes, u, 4);
        } else {
            uint16_t h = 0;
            if (!encode_float16(f, &h, &why)) return refuse("'" + text + "' is " + why);
            put_le(out.bytes, h, 2);
        }
        out.ok = true;
        return out;
    }

    // an integer-like object: Char, Int, Long, Scalar, Bitfield, Pointer
    int bits = 0;
    bool uns = e.is_unsigned;
    size_t n = 0;
    switch (e.kind) {
    case CdbTypeKind::Char: bits = 8; n = 1; break;
    case CdbTypeKind::Pointer: bits = 16; n = 2; uns = true; break;
    case CdbTypeKind::Bitfield:
        n = static_cast<size_t>(std::max(1, type.size));
        if (n > 4) return refuse("a bitfield unit of " + num(static_cast<long long>(n)) + " bytes is not supported");
        if (e.bit_width <= 0 || e.bit_start < 0 || e.bit_start + e.bit_width > 8 * static_cast<int>(n))
            return refuse("the bitfield does not fit its storage unit");
        bits = e.bit_width;
        break;
    default:
        if (type.size != 1 && type.size != 2 && type.size != 4 && type.size != 8)
            return refuse("an integer of " + num(type.size) + " bytes is not supported");
        n = static_cast<size_t>(type.size);
        bits = 8 * type.size;
        break;
    }
    const bool plain = e.kind == CdbTypeKind::Int || e.kind == CdbTypeKind::Long || e.kind == CdbTypeKind::Scalar;
    IntText it;
    if (from_text) {
        it = parse_int_text(text);
        if (!it.ok) {
            double d = 0;
            bool range = false;
            if (float_from_text<double>(text, &d, &range) && !range && std::isfinite(d)) return refuse("'" + text + "' is not an integer");
            return refuse("'" + text + "' is not a number");
        }
    } else {
        if (plain && n == 8) return refuse("a 8-byte integer does not fit the script's 32 bits");
        it.ok = true;
        it.neg = v.int32 < 0;
        it.mag = it.neg ? 0ull - static_cast<unsigned long long>(static_cast<long long>(v.int32)) : static_cast<unsigned long long>(v.int32);
    }
    // a script's 32-bit value is a bit pattern in a 4-byte integer (c_int32_scalar's mirror)
    const bool pattern = !from_text && plain && n == 4;
    if (!pattern && !int_fits(it.neg, it.mag, it.overflow, bits, uns)) return out_of_range(bits, uns);
    unsigned long long u = pattern ? static_cast<uint32_t>(v.int32) : int_pattern(it.neg, it.mag, bits);
    if (e.kind == CdbTypeKind::Bitfield) {
        if (!current || avail < n) return refuse("the object is shorter than its type");
        unsigned long long unit = 0;
        for (size_t k = n; k-- > 0;) unit = (unit << 8) | current[k];
        const unsigned long long mask = ((1ull << e.bit_width) - 1) << e.bit_start;
        unit = (unit & ~mask) | ((u << e.bit_start) & mask);
        u = unit;
    }
    put_le(out.bytes, u, n);
    out.ok = true;
    return out;
}

}  // namespace dbg
}  // namespace jnext
