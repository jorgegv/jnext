// The C value layer (GH #434, E353-04 stage 2; src/debug/c_value.{h,cpp}):
//
//   CVALF-*   the formatting of a value from its type chain (integers, chars, z88dk's 48-bit float, IEEE 754
//             binary32 and binary16, pointers, arrays, structs, bitfields), the type text, the struct type
//             lookup, the C path grammar and its evaluation, the scope rules for locals, register locals and
//             the sccz80 fastcall marker.
//
// Oracles: the C source's semantics (test/fixtures/z88dk/src/vars.c, types.h, floats.c), the committed fixtures'
// maps and section binaries (the bytes z88dk compiled for float constants), IEEE 754's tables (binary32 and
// binary16), and the CDB records the compilers wrote. Pure: bytes come from a synthetic reader. No VHDL
// counterpart.
// Run: ./build/test/c_value_test

#include "debug/c_value.h"

#include "debug/cdb_record.h"
#include "debug/source_map.h"
#include "debug/variable_store.h"
#include "debug/z88dk_source_map.h"

#include <algorithm>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include "../row_id.h"

namespace fs = std::filesystem;
using namespace jnext::dbg;

static int g_total = 0;
static int g_pass = 0;
static int g_fail = 0;

static void check(const char* id, const char* desc, bool cond, const std::string& detail = std::string()) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc);
        if (!detail.empty()) std::printf("    detail: %s\n", detail.c_str());
    }
}

static const fs::path kFixtures = JNEXT_Z88DK_FIXTURE_DIR;
// 1.5 spelled in two pieces: a bare "1.5" literal is shaped like a row ID, and the global ID check (dup-ids) reads it as one
static const std::string k15 = std::string("1") + ".5";

// What the NEX loader leaves mapped (slots 2-5 = pages 10, 11, 4, 5).
static MemSpace nex_space(uint16_t address) {
    static const int kPage[8] = {-1, -1, 10, 11, 4, 5, 0, 1};
    const int slot = address >> 13;
    return slot < 2 ? MemSpace::rom(0) : MemSpace::page(static_cast<uint16_t>(kPage[slot]));
}

static std::vector<uint8_t> read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

// A fixture's map loaded into a source map (its C variable store inside).
struct Loaded {
    SourceMap map;
    std::shared_ptr<const VariableStore> store;
    explicit Loaded(const char* fixture) {
        load_z88dk_source_map(map, (kFixtures / fixture / "prog.map").string(), nex_space);
        store = map.variables();
    }
};

// The bytes of a map symbol in the fixture's CODE section binary.
static std::vector<uint8_t> symbol_bytes(const char* fixture, const char* symbol, size_t n) {
    std::ifstream in(kFixtures / fixture / "prog.map");
    std::string line;
    unsigned long head = 0, addr = 0;
    bool have = false;
    const std::string want = std::string(symbol) + " ";
    while (std::getline(in, line)) {
        if (line.compare(0, 14, "__CODE_head   ") == 0) head = std::stoul(line.substr(line.find('$') + 1), nullptr, 16);
        if (line.compare(0, want.size(), want) == 0 && line.find("; addr") != std::string::npos && !have) {
            addr = std::stoul(line.substr(line.find('$') + 1), nullptr, 16);
            have = true;
        }
    }
    const std::vector<uint8_t> bin = read_all(kFixtures / fixture / "prog_CODE.bin");
    if (!have || addr < head || addr - head + n > bin.size()) return {};
    return std::vector<uint8_t>(bin.begin() + static_cast<std::ptrdiff_t>(addr - head),
                                bin.begin() + static_cast<std::ptrdiff_t>(addr - head + n));
}

// The type of a record written the way a compiler writes it: `ty("{2}SI:S")`.
static CdbType ty(const std::string& chain) {
    const CdbDecoded d = cdb_decode("S:G$x$0_0$0(" + chain + "),E,0,0");
    return d.ok() ? d.record.var.type : CdbType{};
}

// A synthetic memory: address -> byte.
struct Mem {
    std::map<uint32_t, uint8_t> m;
    void put(uint32_t a, std::initializer_list<uint8_t> v) {
        for (uint8_t b : v) m[a++] = b;
    }
    void put_str(uint32_t a, const std::string& s) {
        for (char c : s) m[a++] = static_cast<uint8_t>(c);
        m[a] = 0;
    }
    CReader reader() const {
        return [this](uint32_t a, size_t n, uint8_t* out) {
            for (size_t i = 0; i < n; ++i) {
                const auto it = m.find(a + static_cast<uint32_t>(i));
                if (it == m.end()) return false;
                out[i] = it->second;
            }
            return true;
        };
    }
};

static CFormatted fmt(const CFormatCtx& ctx, const CdbType& t, std::initializer_list<uint8_t> bytes) {
    const std::vector<uint8_t> b(bytes);
    return c_format_value(ctx, t, b.data(), b.size());
}

static std::string text_of(const CFormatCtx& ctx, const std::string& chain, std::initializer_list<uint8_t> bytes) {
    const CFormatted f = fmt(ctx, ty(chain), bytes);
    return f.ok ? f.text : "!" + f.why;
}

static const CVariable* global_var(const VariableStore& s, const char* name) {
    for (const CVariable& v : s.variables())
        if (v.record.var.name == name && v.record.scope == CdbScope::Global && v.record.hidden == CdbHidden::None) return &v;
    return nullptr;
}

static int fn_index(const VariableStore& s, const char* name) {
    for (size_t i = 0; i < s.functions().size(); ++i)
        if (s.functions()[i].name == name && s.functions()[i].module == "vars_c") return static_cast<int>(i);
    return -1;
}

static const CScope* scope_of_line(const VariableStore& s, int fn, int line) {
    for (const CScope& c : s.functions()[static_cast<size_t>(fn)].scopes)
        if (c.line == line) return &c;
    return nullptr;
}

static std::string names(const std::vector<CLocalRef>& v) {
    std::string out;
    for (const CLocalRef& r : v) out += (out.empty() ? "" : " ") + r.var->c_name;
    return out;
}

// A comma decimal point, as a Spanish locale has it.
struct CommaPunct : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};

// GH #477: a synthetic store whose module t_c records a float format (or an unusable one: `why`).
struct FpCtx {
    VariableStore st;
    CFormatCtx ctx;
    FpCtx(int mode, int bias, int ms, const std::string& why = "")
        : st({}, {}, {}, {}, {}, {}, {}, "", {{"t_c", CFloatFormat{mode, bias, ms, why}}}) {
        ctx.store = &st;
        ctx.module = "t_c";
        ctx.scope_name = "t.c";
    }
};

static std::string hex_of(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (uint8_t x : b) s += std::string(s.empty() ? "" : " ") + d[x >> 4] + d[x & 15];
    return s;
}

// The bytes the encoder gives for `text` (hex), or `!<reason>`; `current` is the object's present bytes.
static std::string enc_hex(const CFormatCtx& ctx, const char* chain, const char* text, std::vector<uint8_t> current = {}) {
    CNewValue v;
    v.text = text;
    const CEncoded e = c_encode_value(ctx, ty(chain), v, current.data(), current.size());
    return e.ok ? hex_of(e.bytes) : "!" + e.why;
}

int main() {
    Loaded sc("master/sccz80-cvars");
    Loaded sd("master/sdcc-cvars");
    Loaded cc("master/80cc-cvars");
    CFormatCtx plain;   // no machine, no store
    CFormatCtx csc;     // sccz80's store: types from types.h
    csc.store = sc.store.get();
    csc.scope_name = "vars.c";
    csc.module = "vars_c";
    CFormatCtx csd;     // zsdcc's store: each module has its own copy
    csd.store = sd.store.get();
    csd.scope_name = "vars";
    csd.module = "vars_c";

    // ── CVALF-01..04: z88dk's 48-bit float ─────────────────────────────────────
    // Oracle: the bytes z88dk compiled for floats.c (v2.4 and master sccz80: the same six).
    {
        bool ok = true;
        const struct { const char* sym; std::string text; } kCases[] = {
            {"_f_15", k15}, {"_f_m25", "-2.5"}, {"_f_01", "0.1"}, {"_f_1e10", "10000000000"}, {"_f_1em10", "1e-10"}, {"_f_0", "0"}};
        for (const char* fx : {"v2.4/sccz80-floats", "master/sccz80-floats"})
            for (const auto& c : kCases) {
                const std::vector<uint8_t> b = symbol_bytes(fx, c.sym, 6);
                ok = ok && b.size() == 6 && c_format_float48(b.data()) == c.text;
            }
        check("CVALF-01", "the six float constants of floats.c, as z88dk (2.4 and master sccz80) compiled them into 48-bit bytes, decode to 1.5, -2.5, 0.1, "
                          "10000000000, 1e-10 and 0",
              ok);
    }
    {
        const uint8_t z1[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x00};
        const uint8_t z2[6] = {0x12, 0x34, 0x56, 0x78, 0xFF, 0x00};
        check("CVALF-02", "an exponent byte of 0 is zero whatever the mantissa and the sign hold", c_format_float48(z1) == "0" && c_format_float48(z2) == "0");
    }
    {
        const uint8_t pos[6] = {0, 0, 0, 0, 0x40, 0x81};
        const uint8_t neg[6] = {0, 0, 0, 0, 0xC0, 0x81};
        const uint8_t half[6] = {0, 0, 0, 0, 0x00, 0x80};   // 0.5: the implied leading 1 alone, exponent 0x80
        const uint8_t two[6] = {0, 0, 0, 0, 0x00, 0x82};    // 0.1b x 2^2 = 2
        check("CVALF-03", "the sign is bit 7 of the fifth byte, the leading 1 is implied and the exponent is biased by $80: "
                          "$40/$81 is 1.5, $C0/$81 is -1.5, $00/$80 is 0.5, $00/$82 is 2",
              c_format_float48(pos) == k15 && c_format_float48(neg) == "-1.5" && c_format_float48(half) == "0.5" &&
                  c_format_float48(two) == "2");
    }
    {
        const CFormatted f = fmt(plain, ty("{6}SF:S"), {0x00, 0x00, 0x00, 0x00, 0xA0, 0x82});
        check("CVALF-04", "a {6}SF object is read as the 48-bit format through c_format_value (-2.5), and one of 5 bytes is refused as too short",
              f.ok && f.text == "-2.5" && !fmt(plain, ty("{6}SF:S"), {0, 0, 0, 0, 0}).ok);
    }

    // ── CVALF-05..07: IEEE 754 ─────────────────────────────────────────────────
    check("CVALF-05", "binary32: 0x3FC00000 is 1.5, 0x3DCCCCCD prints 0.1, 0x80000000 -0, 0x7F800000 inf, 0xFF800000 -inf, 0x7FC00000 nan, "
                      "0x00000001 1e-45",
          c_format_float32(0x3FC00000) == k15 && c_format_float32(0x3DCCCCCD) == "0.1" && c_format_float32(0x80000000) == "-0" &&
              c_format_float32(0x7F800000) == "inf" && c_format_float32(0xFF800000) == "-inf" && c_format_float32(0x7FC00000) == "nan" &&
              c_format_float32(0x00000001) == "1e-45");
    {
        bool ok = true;
        const struct { const char* sym; std::string text; } kCases[] = {
            {"_f_15", k15}, {"_f_m25", "-2.5"}, {"_f_01", "0.1"}, {"_f_1e10", "1e+10"}, {"_f_1em10", "1e-10"}, {"_f_0", "0"}};
        for (const auto& c : kCases) {
            const std::vector<uint8_t> b = symbol_bytes("master/sdcc-floats", c.sym, 4);
            const uint32_t bits = b.size() == 4 ? static_cast<uint32_t>(b[0] | b[1] << 8 | b[2] << 16 | static_cast<uint32_t>(b[3]) << 24) : 0xDEADBEEF;
            ok = ok && b.size() == 4 && c_format_float32(bits) == c.text;
        }
        check("CVALF-06", "the six constants of floats.c as zsdcc compiled them ({4}SF, IEEE binary32 bytes) read back as 1.5, -2.5, 0.1, 1e+10, 1e-10, 0", ok);
    }
    check("CVALF-07", "binary16: 0x3E00 is 1.5, 0xC000 -2, 0x7C00 inf, 0xFC00 -inf, 0x7E00 nan, 0x0001 is 2^-24 (5.9604645e-08), 0x0400 2^-14 (6.1035156e-05)",
          c_format_float16(0x3E00) == k15 && c_format_float16(0xC000) == "-2" && c_format_float16(0x7C00) == "inf" &&
              c_format_float16(0xFC00) == "-inf" && c_format_float16(0x7E00) == "nan" && c_format_float16(0x0001) == "5.9604645e-08" &&
              c_format_float16(0x0400) == "6.1035156e-05");
    {
        // the size decides, not the noun: the same SF chain, 6 bytes and 4 bytes
        const CFormatted a = fmt(plain, ty("{6}SF:S"), {0, 0, 0, 0, 0x40, 0x81});
        const CFormatted b = fmt(plain, ty("{4}SF:S"), {0, 0, 0xC0, 0x3F});
        const CFormatted c = fmt(plain, ty("{8}SF:S"), {0, 0, 0, 0, 0, 0, 0, 0});
        check("CVALF-08", "{6}SF is the 48-bit format and {4}SF IEEE binary32 (both 1.5); any other size is refused", a.ok && a.text == k15 && b.ok && b.text == k15 && !c.ok);
    }
    {
        // a global C++ locale with a comma decimal point (and, when installed, a Spanish C locale) must not reach a number
        const std::locale before = std::locale::global(std::locale(std::locale::classic(), new CommaPunct));
        const char* c_loc = std::setlocale(LC_ALL, "es_ES.UTF-8");
        const uint8_t f48[6] = {0, 0, 0, 0, 0x40, 0x81};
        const std::string a = c_format_float48(f48);
        const std::string b = c_format_float32(0x3FC00000);
        const std::string c = c_format_float16(0x3E00);
        const std::string d = text_of(plain, "{2}SI:S", {0x39, 0x30});   // 12345: no thousands separator either
        std::setlocale(LC_ALL, "C");
        std::locale::global(before);
        (void)c_loc;
        check("CVALF-09", "under a global locale whose decimal point is ',' every float still prints '.' and an int has no separator",
              a == k15 && b == k15 && c == k15 && d == "12345");
    }

    // ── CVALF-10..12: integers ─────────────────────────────────────────────────
    check("CVALF-10", "char: {1}SC:S 0xFF is -1, {1}SC:U 0xFF is 255, 65 is `65 'A'`, 0x7F and 0x1F are the number alone, 39 escapes the quote",
          text_of(plain, "{1}SC:S", {0xFF}) == "-1" && text_of(plain, "{1}SC:U", {0xFF}) == "255" && text_of(plain, "{1}SC:S", {65}) == "65 'A'" &&
              text_of(plain, "{1}SC:U", {0x7F}) == "127" && text_of(plain, "{1}SC:U", {0x1F}) == "31" && text_of(plain, "{1}SC:U", {39}) == "39 '\\''");
    check("CVALF-11", "int and long are little-endian with the sign from :S/:U: {2}SI:S $8000 is -32768, {2}SI:U 40000, {4}SL:U 0xCAFEBABE 3405691582, "
                      "{4}SL:S 0x12345678 305419896, {4}SL:S 0xFFFFFFFE -2",
          text_of(plain, "{2}SI:S", {0x00, 0x80}) == "-32768" && text_of(plain, "{2}SI:U", {0x40, 0x9C}) == "40000" &&
              text_of(plain, "{4}SL:U", {0xBE, 0xBA, 0xFE, 0xCA}) == "3405691582" && text_of(plain, "{4}SL:S", {0x78, 0x56, 0x34, 0x12}) == "305419896" &&
              text_of(plain, "{4}SL:S", {0xFE, 0xFF, 0xFF, 0xFF}) == "-2");
    check("CVALF-12", "a long long ({8}SL) reads 8 bytes (signed -1, unsigned 18446744073709551615), a bare scalar (:U, {1}) its width, and a short object is refused",
          text_of(plain, "{8}SL:S", {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}) == "-1" &&
              text_of(plain, "{8}SL:U", {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}) == "18446744073709551615" && text_of(plain, "{1}:U", {200}) == "200" &&
              !fmt(plain, ty("{4}SL:S"), {1, 2}).ok);

    // ── CVALF-13..14: pointers ─────────────────────────────────────────────────
    {
        Mem mem;
        mem.put_str(0x9000, "abc");
        mem.put_str(0x9010, "a\nb\"c\\\x01");
        std::string long_str(40, 'x');
        mem.put_str(0x9100, long_str);
        CFormatCtx ctx;
        ctx.read = mem.reader();
        ctx.name_at = [](uint16_t a) { return a == 0x8D21 ? std::string(" <g_node>") : std::string(); };
        const CdbType pn = ty("{2}DG,SI:S");
        const CFormatted a = fmt(ctx, pn, {0x21, 0x8D});
        const CFormatted n = fmt(ctx, pn, {0x00, 0x00});
        const CFormatted u = fmt(ctx, pn, {0x00, 0x12});
        check("CVALF-13", "a pointer is `$XXXX`, `$0000 (NULL)` when null, with ` <name>` of what is mapped at its address",
              a.ok && a.text == "$8D21 <g_node>" && n.ok && n.text == "$0000 (NULL)" && u.ok && u.text == "$1200");
        const CdbType pc = ty("{2}DG,SC:S");
        const CFormatted s1 = fmt(ctx, pc, {0x00, 0x90});
        const CFormatted s2 = fmt(ctx, pc, {0x10, 0x90});
        const CFormatted s3 = fmt(ctx, pc, {0x00, 0x91});
        const CFormatted s4 = fmt(ctx, pc, {0x00, 0xA0});
        check("CVALF-14", "a char pointer adds its string: \"abc\"; escapes (\\n, \\\", \\\\, \\x01); a string over 32 bytes is cut with ...; an "
                          "unreadable target is marked, not shown as an empty string",
              s1.ok && s1.text == "$9000 \"abc\"" && s2.ok && s2.text == "$9010 \"a\\nb\\\"c\\\\\\x01\"" &&
                  s3.ok && s3.text == "$9100 \"" + std::string(32, 'x') + "\"..." && s4.ok && s4.text == "$A000 <unreadable>");
    }

    // ── CVALF-15..16: arrays ───────────────────────────────────────────────────
    {
        // the element count is the DA<n>, never the byte size: a DA4x,SI is 8 bytes and has 4 elements
        const CFormatted a = fmt(plain, ty("{8}DA4x,SI:S"), {0xD3, 0x01, 0xD7, 0x01, 0xDB, 0x01, 0xDF, 0x01});
        const CFormatted m = fmt(plain, ty("{12}DA2x,DA3x,SI:S"), {1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0});
        check("CVALF-15", "an array's count is its DA<n> (a DA4x,SI of 8 bytes is {467, 471, 475, 479}), and g_matrix's 2 x 3 ints print nested",
              a.ok && a.text == "{467, 471, 475, 479}" && m.ok && m.text == "{{1, 2, 3}, {4, 5, 6}}");
        std::vector<uint8_t> big(40);
        for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i);
        const CFormatted c = c_format_value(plain, ty("{40}DA40x,SC:U"), big.data(), big.size());   // bytes 0..39: a list (owner decision, GH #437)
        std::vector<uint8_t> ints(40);
        for (size_t i = 0; i < 20; ++i) ints[2 * i] = static_cast<uint8_t>(i);
        const CFormatted d = c_format_value(plain, ty("{40}DA20x,SI:S"), ints.data(), ints.size());
        check("CVALF-16", "at most 16 elements are shown, then `...`; a char array of text is a string with every byte shown (`{'h', 'i', 0}` is `\"hi\\0\"`), a 4-byte array with no NUL shows all four, an array of "
                          "non-text bytes ({10, 20, 30, 40}) is a list of numbers, and so are the bytes 0..39 (a leading 0 no longer hides what follows)",
              d.ok && d.text == "{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, ...}" && c.ok && c.text == "{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, ...}" &&
                  text_of(plain, "{3}DA3x,SC:S", {'h', 'i', 0}) == "\"hi\\0\"" && text_of(plain, "{4}DA4x,SC:S", {'a', 'b', 'c', 'd'}) == "\"abcd\"" &&
                  text_of(plain, "{4}DA4x,SC:U", {10, 20, 30, 40}) == "{10, 20, 30, 40}");
    }

    // ── CVALF-17: struct nesting ───────────────────────────────────────────────
    {
        // GDB's null-stop off (GH #437, owner decision): every byte that is not a NUL is text -> the whole array as a string, an embedded NUL as \0, the padding
        // after the last text byte dropped; anything else -> a list. Oracle: GDB's `set print null-stop off` output, and vars.c (`table[] = {0, 1, 4, 9}`)
        const std::string embedded = text_of(plain, "{8}DA8x,SC:S", {'h', 'i', 0, 'l', 'o', 0, 0, 0});
        const std::string padded = text_of(plain, "{4}DA4x,SC:S", {'h', 'i', 0, 0});
        check("CVALF-16a", "a char array with a NUL inside shows the bytes after it: {'h','i',0,'l','o',0,0,0} is \"hi\\0lo\\0\\0\\0\" (every byte shown, a NUL as \\0, "
                           "the trailing ones too), and a padded string {'h','i',0,0} is \"hi\\0\\0\"",
              embedded == "\"hi\\0lo\\0\\0\\0\"" && padded == "\"hi\\0\\0\"");
        check("CVALF-16b", "a table that starts at 0 is a list, not an empty string: {0, 1, 4, 9, 16} (an unsigned char array of numbers) prints as a list, and so does a "
                           "string-like array holding one non-text byte after its NUL ({'a', 0, 7})",
              text_of(plain, "{5}DA5x,SC:U", {0, 1, 4, 9, 16}) == "{0, 1, 4, 9, 16}" && text_of(plain, "{3}DA3x,SC:U", {'a', 0, 7}) == "{97, 0, 7}");
        check("CVALF-16c", "a plain NUL-terminated string shows its NUL: {'h','i',0} is \"hi\\0\", an all-zero array is four \\0, and an array with no NUL is whole",
              text_of(plain, "{3}DA3x,SC:S", {'h', 'i', 0}) == "\"hi\\0\"" && text_of(plain, "{4}DA4x,SC:S", {0, 0, 0, 0}) == "\"\\0\\0\\0\\0\"" &&
                  text_of(plain, "{4}DA4x,SC:S", {'a', 'b', 'c', 'd'}) == "\"abcd\"");
    }
    {
        const CVariable* rect = global_var(*sc.store, "g_rect");
        std::vector<uint8_t> b = {1, 0, 2, 0, 3, 4, 0, 5, 0, 6, 'h', 'i', 0, 0};
        const CFormatted f = rect ? c_format_value(csc, rect->record.var.type, b.data(), b.size()) : CFormatted{};
        const CVariable* rz = global_var(*sd.store, "g_rect");
        const CFormatted g = rz ? c_format_value(csd, rz->record.var.type, b.data(), b.size()) : CFormatted{};
        const std::string want = "{a = {x = 1, y = 2, tag = 3}, b = {x = 4, y = 5, tag = 6}, name = \"hi\\0\\0\"}";
        check("CVALF-17", "struct rect (two struct points and a char name[4]) prints nested, on sccz80's header-scoped and zsdcc's module-scoped T: records",
              f.ok && f.text == want && g.ok && g.text == want);
    }

    // ── CVALF-18..19: bitfields ────────────────────────────────────────────────
    {
        const CVariable* fl = global_var(*sd.store, "g_flags");
        const std::vector<uint8_t> b = {0x8D, 0xC8};
        const CFormatted f = fl ? c_format_value(csd, fl->record.var.type, b.data(), b.size()) : CFormatted{};
        check("CVALF-18", "zsdcc bitfields (SB<start>$<width>): the bytes 8D C8 give lo = 5 (bits 0-2), mid = 17 (bits 3-7), hi = 200",
              f.ok && f.text == "{lo = 5, mid = 17, hi = 200}" && f.notes.empty());
    }
    {
        const CVariable* fl = global_var(*sc.store, "g_flags");
        const std::vector<uint8_t> b = {0x8D, 0xC8};
        const CFormatted f = fl ? c_format_value(csc, fl->record.var.type, b.data(), b.size()) : CFormatted{};
        const CVariable* w = global_var(*sc.store, "g_word");
        const std::vector<uint8_t> wb = {1, 2};
        const CFormatted u = w ? c_format_value(csc, w->record.var.type, wb.data(), wb.size()) : CFormatted{};
        check("CVALF-19", "sccz80's overlapping members of struct flags are marked `bitfield layout unknown (z88dk U2)` with their storage unit's value; "
                          "a union (every member at offset 0) is not marked",
              f.ok && f.text.find("lo = 51341 [bitfield layout unknown (z88dk U2)]") != std::string::npos && f.notes.size() == 1 &&
                  f.notes[0] == "bitfield layout unknown (z88dk U2)" && u.ok && u.text == "{w = 513, b = {1, 2}}" && u.notes.empty());
    }

    // ── CVALF-20..21: the struct type lookup ───────────────────────────────────
    {
        // zsdcc keeps one copy per module (Fvars$point, Fother$point): the variable's own scope wins, then its module
        const CTypeLookup own = c_find_type(*sd.store, "point", "vars", "vars_c");
        const CTypeLookup other = c_find_type(*sd.store, "point", "other", "other_c");
        const CTypeLookup bymod = c_find_type(*sd.store, "point", "nosuch", "other_c");
        const CTypeLookup header = c_find_type(*sc.store, "point", "vars.c", "vars_c");
        const CTypeLookup scope_first = c_find_type(*sd.store, "point", "vars", "other_c");   // the scope outranks the module
        check("CVALF-20", "zsdcc: struct point is the T: copy of the variable's own scope (Fvars, Fother), else of its module; sccz80/80cc have one per header "
                          "(types.h), found by its unique tag",
              own.record && own.record->record.scope_name == "vars" && other.record && other.record->record.scope_name == "other" &&
                  bymod.record && bymod.record->module == "other_c" && scope_first.record && scope_first.record->record.scope_name == "vars" && header.record && header.record->record.scope_name == "types.h");
    }
    {
        const auto rec = [](const char* text, const char* module) {
            CRecord r;
            r.text = text;
            r.module = module;
            r.record = cdb_decode(text).record;
            return r;
        };
        const VariableStore diff({}, {}, {rec("T:Fa.h$t[({0}S:S$x$0_0$0({2}SI:S),Z,0,0)]", "a_c"), rec("T:Fb.h$t[({0}S:S$y$0_0$0({2}SI:S),Z,0,0)]", "b_c")}, {}, {});
        const VariableStore same({}, {}, {rec("T:Fa.h$t[({0}S:S$x$0_0$0({2}SI:S),Z,0,0)]", "a_c"), rec("T:Fb.h$t[({0}S:S$x$0_0$0({2}SI:S),Z,0,0)]", "b_c")}, {}, {});
        const CTypeLookup amb = c_find_type(diff, "t", "z", "z_c");
        const CTypeLookup one = c_find_type(same, "t", "z", "z_c");
        const CTypeLookup mod = c_find_type(diff, "t", "z", "b_c");
        const CTypeLookup none = c_find_type(same, "nosuch", "z", "z_c");
        check("CVALF-21", "two DIFFERENT definitions of a tag, none in the variable's scope or module, are refused as ambiguous (never first-wins); "
                          "equal ones in two headers are one; a module match decides; an unknown tag says so",
              !amb.record && amb.why.find("2 definitions") != std::string::npos && one.record && mod.record && mod.record->module == "b_c" &&
                  !none.record && none.why.find("no definition") != std::string::npos);
    }

    // ── CVALF-22..23: type text ────────────────────────────────────────────────
    check("CVALF-22", "type text of scalars: char, unsigned char, int, unsigned int, long, unsigned long, long long, float, void, struct point",
          c_type_text(ty("{1}SC:S")) == "char" && c_type_text(ty("{1}SC:U")) == "unsigned char" && c_type_text(ty("{2}SI:S")) == "int" &&
              c_type_text(ty("{2}SI:U")) == "unsigned int" && c_type_text(ty("{4}SL:S")) == "long" && c_type_text(ty("{4}SL:U")) == "unsigned long" &&
              c_type_text(ty("{8}SL:S")) == "long long" && c_type_text(ty("{6}SF:S")) == "float" && c_type_text(ty("{0}SV:S")) == "void" &&
              c_type_text(ty("{5}STpoint:S")) == "struct point");
    check("CVALF-23", "type text of chains: `struct node *`, `char *`, `int [2][3]`, `int (*)()`, `char **`, `unsigned char [3]`; the outer element can be skipped",
          c_type_text(ty("{2}DG,STnode:S")) == "struct node *" && c_type_text(ty("{2}DG,SC:S")) == "char *" &&
              c_type_text(ty("{12}DA2x,DA3x,SI:S")) == "int [2][3]" && c_type_text(ty("{2}DG,DF,SI:S")) == "int (*)()" &&
              c_type_text(ty("{2}DG,DG,SC:S")) == "char **" && c_type_text(ty("{3}DA3d,SC:U")) == "unsigned char [3]" &&
              c_type_text(ty("{2}DG,SC:S"), 1) == "char");

    // ── CVALF-24..26: the path grammar ─────────────────────────────────────────
    {
        const CPath a = c_parse_path("  g_rect . b[2] ->x ");
        const CPath b = c_parse_path("*p");
        const CPath c = c_parse_path("sum::calls");
        const CPath d = c_parse_path("other.c::s_dup");
        const CPath e = c_parse_path("x[0x1F]");
        const CPath f = c_parse_path("x[$1f]");
        const CPath g = c_parse_path("g_pt.tag");
        check("CVALF-24", "paths: whitespace is free; `.m`, `->m` and `[i]` chain; `*p` derefs; `fn::x` is a function-scoped name and `other.c::x` a file one; "
                          "an index is decimal, 0x<hex> or $<hex>",
              a.ok && a.name == "g_rect" && a.steps.size() == 3 && a.steps[0].kind == CPathStep::Kind::Dot && a.steps[0].name == "b" &&
                  a.steps[1].kind == CPathStep::Kind::Index && a.steps[1].index == 2 && a.steps[2].kind == CPathStep::Kind::Arrow && a.steps[2].name == "x" &&
                  b.ok && b.deref && b.name == "p" && c.ok && c.primary == CPath::Primary::Function && c.qualifier == "sum" && c.name == "calls" &&
                  d.ok && d.primary == CPath::Primary::File && d.qualifier == "other.c" && d.name == "s_dup" && e.ok && e.steps[0].index == 31 &&
                  f.ok && f.steps[0].index == 31 && g.ok && g.primary == CPath::Primary::Name && g.steps.size() == 1);
    }
    {
        const char* bad[] = {"a.", "a[", "a[-1]", "->x", "a..b", "a b", "a[1", "a[]", "", "*", "a->", "a[1]]"};
        bool ok = true;
        std::string seen;
        for (const char* t : bad) {
            const CPath p = c_parse_path(t);
            if (p.ok || p.why.empty()) {
                ok = false;
                seen += std::string(" [") + t + "]";
            }
        }
        check("CVALF-25", "malformed paths are refused, each with a reason: `a.`, `a[`, `a[-1]`, `->x`, `a..b`, trailing text, an empty index, no name", ok);
        if (!ok) std::printf("  CVALF-25 accepted:%s\n", seen.c_str());
    }
    {
        const CPath n = c_parse_path("a[-1]");
        const CPath t = c_parse_path("a[1] junk");
        const CPath u = c_parse_path("a.");
        check("CVALF-26", "the reasons name the problem: no sign in an index, the unexpected text, the missing member name",
              n.why.find("no sign") != std::string::npos && t.why.find("junk") != std::string::npos && u.why.find("member name") != std::string::npos);
    }

    // ── CVALF-27: evaluating a path, pointer indexing ──────────────────────────
    {
        // a `struct point *p` at $A000 pointing at $B000; points are 5 bytes (x, y, tag): p[1].y is at $B000 + 5 + 2
        Mem mem;
        mem.put(0xA000, {0x00, 0xB0});
        mem.put(0xB000, {1, 0, 2, 0, 3, 7, 0, 8, 0, 9});
        CFormatCtx ctx = csc;
        ctx.read = mem.reader();
        const CdbType pt = ty("{2}DG,STpoint:S");
        CLoc root;
        root.type = pt;
        root.addr = 0xA000;
        const CEval e1 = c_eval_path(ctx, root, false, c_parse_path("p[1].y").steps);
        const CEval e2 = c_eval_path(ctx, root, false, c_parse_path("p->y").steps);
        const CEval e3 = c_eval_path(ctx, root, true, {});
        const CFormatted v1 = e1.ok ? c_read_loc(ctx, e1.loc) : CFormatted{};
        const CFormatted v2 = e2.ok ? c_read_loc(ctx, e2.loc) : CFormatted{};
        const CFormatted v3 = e3.ok ? c_read_loc(ctx, e3.loc) : CFormatted{};
        check("CVALF-27", "pointer indexing scales by the element size: p[1].y reads $B007 (8), p->y reads $B002 (2), *p is the whole first struct",
              e1.ok && e1.loc.addr == 0xB007 && v1.ok && v1.text == "8" && e2.ok && e2.loc.addr == 0xB002 && v2.ok && v2.text == "2" &&
                  v3.ok && v3.text == "{x = 1, y = 2, tag = 3}");
        // refusals on the same memory: each says why
        const CEval r1 = c_eval_path(ctx, root, false, c_parse_path("p.y").steps);
        const CEval r2 = c_eval_path(ctx, root, false, c_parse_path("p->z").steps);
        CLoc ival;
        ival.type = ty("{2}SI:S");
        ival.addr = 0xB000;
        const CEval r3 = c_eval_path(ctx, ival, true, {});
        const CEval r4 = c_eval_path(ctx, ival, false, c_parse_path("x.y").steps);
        CLoc arr;
        arr.type = ty("{12}DA2x,DA3x,SI:S");
        arr.addr = 0xB000;
        const CEval r5 = c_eval_path(ctx, arr, false, c_parse_path("x[2]").steps);
        const CEval ok1 = c_eval_path(ctx, arr, false, c_parse_path("x[1][2]").steps);
        Mem nul;
        nul.put(0xA000, {0, 0});
        CFormatCtx nctx = csc;
        nctx.read = nul.reader();
        const CEval r6 = c_eval_path(nctx, root, false, c_parse_path("p->x").steps);
        check("CVALF-27a", "refusals carry their reason: `.` on a pointer, no such member, `*` on an int, `.` on an int, index 2 out of range [0, 2), a null pointer; "
                           "g_matrix[1][2] is at element offset 5",
              !r1.ok && r1.why.find("use ->") != std::string::npos && !r2.ok && r2.why.find("no member 'z'") != std::string::npos && !r3.ok &&
                  r3.why == "not a pointer" && !r4.ok && r4.why == "not a struct" && !r5.ok && r5.why == "index 2 out of range [0, 2)" &&
                  !r6.ok && r6.why == "null pointer" && ok1.ok && ok1.loc.addr == 0xB000 + 2 * 5);
    }

    // ── CVALF-28: which locals are visible ─────────────────────────────────────
    {
        const int fs_ = fn_index(*sc.store, "sum");
        const int fz = fn_index(*sd.store, "sum");
        const int fc = fn_index(*cc.store, "sum");
        const CScope* s41 = fs_ >= 0 ? scope_of_line(*sc.store, fs_, 41) : nullptr;
        const CScope* s39 = fs_ >= 0 ? scope_of_line(*sc.store, fs_, 39) : nullptr;
        const CScope* z41 = fz >= 0 ? scope_of_line(*sd.store, fz, 41) : nullptr;
        const CScope* z39 = fz >= 0 ? scope_of_line(*sd.store, fz, 39) : nullptr;
        const CScope* c41 = fc >= 0 ? scope_of_line(*cc.store, fc, 41) : nullptr;
        const auto a41 = s41 ? c_visible_locals(*sc.store, fs_, s41) : std::vector<CLocalRef>{};
        const auto a39 = s39 ? c_visible_locals(*sc.store, fs_, s39) : std::vector<CLocalRef>{};
        const auto b41 = z41 ? c_visible_locals(*sd.store, fz, z41) : std::vector<CLocalRef>{};
        const auto b39 = z39 ? c_visible_locals(*sd.store, fz, z39) : std::vector<CLocalRef>{};
        const auto c41v = c41 ? c_visible_locals(*cc.store, fc, c41) : std::vector<CLocalRef>{};
        bool unknown_marked = false;
        for (const CLocalRef& r : c41v) unknown_marked = unknown_marked || (r.var->c_name == "inner" && r.scope_unknown);
        // sum(int a, int b, struct point *p, uint8_t k) { int t; long acc; static calls; if (p) { int inner; ... } } (vars.c:28-42)
        check("CVALF-28", "visible locals of sum: at vars.c:41 `inner` (block 2) is hidden and at :39 shown on sccz80 (sccz80's `last opened block` 1/2 resolves to its level-1 "
                          "ancestor) and on zsdcc; zsdcc's parameter block 9 is always visible; 80cc (no block data) shows `inner` marked scope unknown; "
                          "parameters first in stack order, static locals last",
              a41.size() == 7 && names(a41) == "k p b a acc t calls" && names(a39) == "k p b a acc inner t calls" && names(b41) == "a b p k acc t calls" &&
                  names(b39) == "a b p k acc inner t calls" && unknown_marked && a41[0].kind == CVarKind::Param && a41.back().kind == CVarKind::StaticLocal);
    }

    {
        // sccz80 writes (level of the innermost open block, number of the LAST OPENED block): scope (2, 4) with level-2 blocks 2 and 3
        // means the open level-2 block is the greatest one at or below 4, block 3, whose parent is the level-1 block
        const auto loc = [](const char* name, int level, int block) {
            CVariable v;
            const std::string rec = std::string("S:Lt.c.f$") + name + "$" + std::to_string(level) + "_0$" + std::to_string(block) + "({2}SI:S),B,1,-2";
            v.record = cdb_decode(rec).record;
            v.text = rec;
            v.c_name = name;
            v.function = 0;
            return v;
        };
        CFunction f;
        f.name = "f";
        CScope sc4;
        sc4.known = true;
        sc4.level = 2;
        sc4.block = 4;
        f.scopes.push_back(sc4);
        const VariableStore st({f}, {loc("v1", 1, 1), loc("v2", 2, 2), loc("v3", 2, 3), loc("v4", 3, 4)}, {}, {}, {});
        const auto got = c_visible_locals(st, 0, &st.functions()[0].scopes[0]);
        check("CVALF-28a", "scope (2, 4) with level-2 blocks 2 and 3: the open block is 3 (the greatest at or below 4 at that level): v1 (level 1) and v3 are visible, "
                           "the sibling v2 and the deeper v4 are not",
              names(got) == "v1 v3");
    }

    // Locals built by hand: `S:Lt.c.f$<name>$<level>_0$<block>`.
    const auto mk_loc = [](const char* name, int level, int block) {
        CVariable v;
        const std::string rec = std::string("S:Lt.c.f$") + name + "$" + std::to_string(level) + "_0$" + std::to_string(block) + "({2}SI:S),B,1,-2";
        v.record = cdb_decode(rec).record;
        v.text = rec;
        v.c_name = name;
        v.function = 0;
        return v;
    };
    const auto mk_scope = [](uint32_t offset, int level, int block) {
        CScope c;
        c.known = true;
        c.offset = offset;
        c.level = level;
        c.block = block;
        return c;
    };
    {
        // if (x) { int t; ... } else { /* here: no locals */ }  (locals v1 (1,1), t (2,2)). sccz80 writes (2,3) on the else block's first
        // line (a block just opened is the last opened one) and (1,3) after it closed; zsdcc's entries are exact: the same (2,3).
        CFunction f;
        f.name = "f";
        f.scopes = {mk_scope(0, 0, 0), mk_scope(4, 1, 1), mk_scope(8, 2, 2), mk_scope(12, 2, 3), mk_scope(16, 1, 3)};
        const VariableStore st({f}, {mk_loc("v1", 1, 1), mk_loc("t", 2, 2)}, {}, {}, {});
        const auto& sc = st.functions()[0].scopes;
        const std::string in_then = names(c_visible_locals(st, 0, &sc[2]));
        const std::string in_else = names(c_visible_locals(st, 0, &sc[3]));
        const std::string after = names(c_visible_locals(st, 0, &sc[4]));
        check("CVALF-28b", "the else block of an if that declared a local: in the then block `t` and `v1` show, in the else block (scope (2,3), a block with no local "
                           "of its own, its level from the first entry that names it) only `v1`, and after it closed (sccz80's (1,3)) only `v1`",
              in_then == "t v1" && in_else == "v1" && after == "v1");
    }
    {
        // a level-3 block sees its level-2 parent's local and the level-1 one
        CFunction f;
        f.name = "f";
        f.scopes = {mk_scope(0, 0, 0), mk_scope(4, 1, 1), mk_scope(8, 2, 2), mk_scope(12, 3, 3)};
        const VariableStore st({f}, {mk_loc("v1", 1, 1), mk_loc("v2", 2, 2), mk_loc("v3", 3, 3)}, {}, {}, {});
        const auto& sc = st.functions()[0].scopes;
        check("CVALF-28c", "nested blocks: the level-3 scope (3,3) sees v3, its level-2 parent's v2 and v1; the level-2 scope sees v2 and v1 but not v3",
              names(c_visible_locals(st, 0, &sc[3])) == "v1 v2 v3" && names(c_visible_locals(st, 0, &sc[2])) == "v1 v2");
    }
    {
        // ffast(x) __z88dk_fastcall: sccz80 `x` is B,1,4 at level 0, 80cc's `x` B,1,-2 at level 0 (OD-14's correct record), zsdcc's `x` is the register
        // [l,h] at level 1 in a block (0) below the body's (7)
        const auto fn_in = [](const VariableStore& s, const char* name) {
            for (size_t i = 0; i < s.functions().size(); ++i)
                if (s.functions()[i].name == name && s.functions()[i].module == "fc_c") return static_cast<int>(i);
            return -1;
        };
        Loaded a("master/sccz80-fc"), b("master/80cc-fc"), c("master/sdcc-fc");
        bool ok = true;
        for (Loaded* l : {&a, &b, &c}) {
            const int fi = l->store ? fn_in(*l->store, "ffast") : -1;
            const auto v = fi >= 0 ? c_visible_locals(*l->store, fi, nullptr) : std::vector<CLocalRef>{};
            ok = ok && !v.empty() && v[0].var->c_name == "x" && v[0].kind == CVarKind::Param;
            for (size_t i = 1; i < v.size(); ++i) ok = ok && v[i].kind != CVarKind::Param;
        }
        check("CVALF-28d", "on the fc fixtures `x` of ffast is a parameter on sccz80 (B,1,4), 80cc (B,1,-2: level 0) and zsdcc (register [l,h]: a block below the body's), "
                           "and the locals after it are not",
              ok);
    }

    // ── CVALF-29: register locals ──────────────────────────────────────────────
    {
        const CVariable* t = nullptr;
        const CVariable* inner = nullptr;
        const CVariable* acc = nullptr;
        for (const CVariable& v : sd.store->variables()) {
            if (v.module != "vars_c" || v.record.function != "sum") continue;
            if (v.record.var.name == "t") t = &v;
            if (v.record.var.name == "inner") inner = &v;
            if (v.record.var.name == "acc") acc = &v;
        }
        CRegs regs;
        regs.bc = 0x1234;
        regs.hl = 0xABCD;
        regs.af = 0x5600;
        const CRegRead rt = t ? c_read_register_local(t->record.var, &regs) : CRegRead{};
        const CRegRead ri = inner ? c_read_register_local(inner->record.var, &regs) : CRegRead{};
        const CRegRead ra = acc ? c_read_register_local(acc->record.var, &regs) : CRegRead{};
        const CRegRead ro = t ? c_read_register_local(t->record.var, nullptr) : CRegRead{};
        const CdbDecoded a = cdb_decode("S:Lvars.sum$q$1_0$10({1}SC:U),R,0,0,[a]");
        const CRegRead rq = c_read_register_local(a.record.var, &regs);
        const CdbDecoded bad = cdb_decode("S:Lvars.sum$q$1_0$10({1}SC:U),R,0,0,[zz]");
        const CRegRead rb = c_read_register_local(bad.record.var, &regs);
        const CFormatted tv = rt.ok ? c_format_value(plain, t->record.var.type, rt.bytes.data(), rt.bytes.size()) : CFormatted{};
        check("CVALF-29", "register locals: `[c,b]` is C | B << 8 (BC $1234 reads 4660) with the note `in BC; may be stale outside its live range`; `[l,h]` is HL; `[a]` is A; "
                          "`[]` is optimised out; an outer frame (no registers) is not available; an unknown register name is refused with the name",
              rt.ok && rt.bytes == std::vector<uint8_t>({0x34, 0x12}) && tv.ok && tv.text == "4660" && rt.note == "in BC; may be stale outside its live range" &&
                  ri.ok && ri.bytes == std::vector<uint8_t>({0xCD, 0xAB}) && rq.ok && rq.bytes == std::vector<uint8_t>({0x56}) && !ra.ok &&
                  ra.why == "optimised out" && !ro.ok && ro.why == "not available (register, outer frame)" && !rb.ok && rb.why.find("zz") != std::string::npos);
    }

    // ── CVALF-30: the per-function U1 rule, its boundary cases ─────────────────────
    // Oracle: the definition of U1 (z88dk#3229): before the fix sccz80 records every parameter of a fastcall function at its nominal slot (>= 0); after it, the register
    // parameter is recorded below the frame. One synthetic function `ffast`, parameters (level 0, space B) and a local (level 1) given as record texts.
    {
        const auto nominal = [](CConv conv, bool prologue, const std::vector<std::pair<std::string, bool>>& recs) {
            std::vector<CVariable> vs;
            for (const auto& [text, hidden] : recs) {
                CVariable v;
                v.record = cdb_decode(text).record;
                v.record.hidden = hidden ? CdbHidden::IncompleteArray : CdbHidden::None;
                v.c_name = v.record.var.name;
                v.function = 0;
                vs.push_back(v);
            }
            CFunction f;
            f.name = "ffast";
            f.conv = conv;
            const VariableStore st({f}, vs, {}, {}, {});
            return c_fastcall_records_nominal(st, 0, prologue);
        };
        const std::string p4 = "S:Lfc.ffast$x$0_0$0({2}SI:U),B,1,4", p0 = "S:Lfc.ffast$x$0_0$0({2}SI:U),B,1,0", pm2 = "S:Lfc.ffast$y$0_0$0({2}SI:U),B,1,-2",
                          loc = "S:Lfc.ffast$t$1_0$1({2}SI:U),B,1,-4", reg = "S:Lfc.ffast$x$0_0$0({2}SI:U),R,0,0,[l,h]";
        const bool c1 = nominal(CConv::ListedFastcall, false, {{p4, false}, {loc, false}}), c2 = nominal(CConv::ListedFastcall, false, {{p0, false}}),
                   c3 = nominal(CConv::ListedFastcall, false, {{p4, false}, {pm2, false}}), c4 = nominal(CConv::ListedFastcall, false, {{pm2, true}, {p4, false}}),
                   c5 = nominal(CConv::ListedFastcall, false, {{reg, false}}), c6 = nominal(CConv::Listed, true, {{p4, false}}),
                   c7n = nominal(CConv::Unlisted, false, {{p4, false}}), c7y = nominal(CConv::Unlisted, true, {{p4, false}});
        check("CVALF-30", "c_fastcall_records_nominal, the boundary cases of U1: a fastcall function whose only parameter is at +4 is nominal even beside a level-1 local at -4; a parameter at +0 is nominal; "
                          "one at +4 with another at -2 is not; a hidden parameter record at -2 is ignored; a function with only an R-space parameter is not; a listing that names it without the "
                          "flag wins over the prologue; an unlisted function is nominal only when the caller read a fastcall prologue",
              c1 && c2 && !c3 && c4 && !c5 && !c6 && !c7n && c7y);
    }

    // ── CVALF-31: sccz80's struct flags (U2): every view agrees and none reads past the object ─────────────────
    {
        // struct flags is {2} (g_flags); its member hi is an int at offset 1: it overhangs by one byte. Memory holds ONLY the object: a read of $B002 fails.
        Mem mem;
        mem.put(0xA000, {0x00, 0xB0});
        mem.put(0xB000, {0x8D, 0xC8});
        CFormatCtx ctx = csc;
        ctx.read = mem.reader();
        CLoc p;
        p.type = ty("{2}DG,STflags:S");
        p.addr = 0xA000;
        CLoc g;
        g.type = ty("{2}STflags:S");
        g.addr = 0xB000;
        const CEval whole = c_eval_path(ctx, p, true, {});
        const CEval idx = c_eval_path(ctx, p, false, c_parse_path("p[1]").steps);
        const CEval arrow = c_eval_path(ctx, p, false, c_parse_path("p->hi").steps);
        const CEval dot = c_eval_path(ctx, g, false, c_parse_path("g_flags.hi").steps);
        const CFormatted fw = whole.ok ? c_read_loc(ctx, whole.loc) : CFormatted{};
        const CFormatted fa = arrow.ok ? c_read_loc(ctx, arrow.loc) : CFormatted{};
        const CFormatted fd = dot.ok ? c_read_loc(ctx, dot.loc) : CFormatted{};
        // a struct with no declared object has no known size: *p is refused
        const auto rec = [](const char* text, const char* module) {
            CRecord r;
            r.text = text;
            r.module = module;
            r.record = cdb_decode(text).record;
            return r;
        };
        const VariableStore bare({}, {}, {rec("T:Fz.h$u[({0}S:S$a$0_0$0({2}SI:U),Z,0,0)({1}S:S$b$0_0$0({2}SI:U),Z,0,0)]", "z_c")}, {}, {});
        CFormatCtx bctx;
        bctx.store = &bare;
        bctx.module = "z_c";
        bctx.read = mem.reader();
        const CEval none = c_eval_path(bctx, [&] { CLoc q; q.type = ty("{2}DG,STu:S"); q.addr = 0xA000; return q; }(), true, {});
        check("CVALF-31", "sccz80's struct flags is 2 bytes (its declared object's size), not 3 (its members' extent): *p, p->hi and g_flags.hi all give hi = 200 "
                          "with the overhang zero-filled and no read of the byte after the object, p[1] is at +2, and a struct with no declared object is refused as "
                          "size unknown (z88dk U2)",
              whole.ok && whole.loc.type.size == 2 && fw.ok && fw.text.find("hi = 200") != std::string::npos && idx.ok && idx.loc.addr == 0xB002 &&
                  arrow.ok && fa.ok && fa.text == "200" && dot.ok && fd.ok && fd.text == "200" && !none.ok && none.why.find("size unknown (z88dk U2)") != std::string::npos);
    }

    // ── CVALF-32: `*` applies to the whole postfix expression ─────────────────────────────────────────────────────
    {
        Mem mem;
        mem.put(0xA000, {0x00, 0xB0, 0x10, 0xB0});   // char *x[2] = {$B000, $B010}
        mem.put(0xB010, {'z'});
        mem.put(0xA100, {0x00, 0xB1});               // node_t *head = $B100
        mem.put(0xB100, {0x00, 0xB2, 0xA0, 0x86, 0x01, 0x00});   // { next = $B200, val = 100000 }
        mem.put(0xB200, {0x00, 0x00, 0x05, 0x00, 0x00, 0x00});   // { next = NULL, val = 5 }
        CFormatCtx ctx = csc;
        ctx.read = mem.reader();
        CLoc x;
        x.type = ty("{4}DA2x,DG,SC:S");
        x.addr = 0xA000;
        const CPath px = c_parse_path("*x[1]");
        const CEval ex = c_eval_path(ctx, x, px.deref, px.steps);
        const CFormatted fx = ex.ok ? c_read_loc(ctx, ex.loc) : CFormatted{};
        CLoc head;
        head.type = ty("{2}DG,STnode:S");
        head.addr = 0xA100;
        const CPath ph = c_parse_path("*head->next");
        const CEval eh = c_eval_path(ctx, head, ph.deref, ph.steps);
        const CFormatted fh = eh.ok ? c_read_loc(ctx, eh.loc) : CFormatted{};
        check("CVALF-32", "postfix binds tighter than `*`: `*x[1]` is the byte x[1] points at ('z'), `*head->next` is the whole node head->next points at",
              px.ok && px.deref && ex.ok && fx.ok && fx.text == "122 'z'" && ph.ok && eh.ok && fh.ok && fh.text == "{next = $0000 (NULL), val = 5}");
    }

    // ── CVALF-33: a pointer to an array ───────────────────────────────────────────────────────────────────────────
    {
        Mem mem;
        mem.put(0xA000, {0x00, 0xB0});
        mem.put(0xB000, {1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0});
        CFormatCtx ctx;
        ctx.read = mem.reader();
        CLoc p;
        p.type = ty("{2}DG,DA3x,SI:S");   // int (*p)[3]
        p.addr = 0xA000;
        const CEval d = c_eval_path(ctx, p, true, {});
        const CEval i1 = c_eval_path(ctx, p, false, c_parse_path("p[1]").steps);
        const CFormatted fd = d.ok ? c_read_loc(ctx, d.loc) : CFormatted{};
        const CFormatted f1 = i1.ok ? c_read_loc(ctx, i1.loc) : CFormatted{};
        check("CVALF-33", "int (*p)[3]: *p is 6 bytes ({1, 2, 3}) and p[1] is at +6 ({4, 5, 6}): the size of a pointed-to array is its whole size",
              d.ok && d.loc.type.size == 6 && fd.ok && fd.text == "{1, 2, 3}" && i1.ok && i1.loc.addr == 0xB006 && f1.ok && f1.text == "{4, 5, 6}");
    }

    // ── CVALF-34: a zsdcc padding member is not shown ─────────────────────────────────────────────────────────────
    {
        const auto rec = [](const char* text, const char* module) {
            CRecord r;
            r.text = text;
            r.module = module;
            r.record = cdb_decode(text).record;
            return r;
        };
        // struct s { uint8_t a; unsigned :2 (written __00000000); uint8_t b; }
        const VariableStore st({}, {}, {rec("T:Fv$s[({0}S:S$a$0_0$0({1}SC:U),Z,0,0)({1}S:S$__00000000$0_0$0({1}SB0$2:U),Z,0,0)({2}S:S$b$0_0$0({1}SC:U),Z,0,0)]", "v_c")}, {}, {});
        CFormatCtx ctx;
        ctx.store = &st;
        ctx.scope_name = "v";
        ctx.module = "v_c";
        const CFormatted f = fmt(ctx, ty("{3}STs:S"), {1, 0xFF, 3});
        check("CVALF-34", "a zsdcc padding member (__00000000) of a struct is skipped: bytes 01 FF 03 print {a = 1, b = 3}", f.ok && f.text == "{a = 1, b = 3}");
    }

    // ── CVALF-35: a plain struct with no declared object is still readable ───────────────────────────────────────
    {
        const auto rec = [](const char* text, const char* module) {
            CRecord r;
            r.text = text;
            r.module = module;
            r.record = cdb_decode(text).record;
            return r;
        };
        Mem mem;
        mem.put(0xA000, {0x00, 0xB0});
        mem.put(0xB000, {3, 0, 4, 0});   // only the object's 4 bytes: an over-read fails
        // struct pt { int x; int y; }: no variable of this type, no member declared as it: its size is its members' extent
        const VariableStore plain_st({}, {}, {rec("T:Fz.h$pt[({0}S:S$x$0_0$0({2}SI:S),Z,0,0)({2}S:S$y$0_0$0({2}SI:S),Z,0,0)]", "z_c")}, {}, {});
        CFormatCtx pctx;
        pctx.store = &plain_st;
        pctx.module = "z_c";
        pctx.read = mem.reader();
        CLoc p;
        p.type = ty("{2}DG,STpt:S");
        p.addr = 0xA000;
        const CEval ey = c_eval_path(pctx, p, false, c_parse_path("p->y").steps);
        const CEval ew = c_eval_path(pctx, p, true, {});
        const CEval ei = c_eval_path(pctx, p, false, c_parse_path("p[1]").steps);
        const CFormatted fy = ey.ok ? c_read_loc(pctx, ey.loc) : CFormatted{};
        const CFormatted fw = ew.ok ? c_read_loc(pctx, ew.loc) : CFormatted{};
        // struct in { char a; char b; } (extent 2) whose only declared object is the member `m` of struct outer ({4}STin: padded to 4)
        const VariableStore member_st({}, {},
                                      {rec("T:Fz.h$in[({0}S:S$a$0_0$0({1}SC:U),Z,0,0)({1}S:S$b$0_0$0({1}SC:U),Z,0,0)]", "z_c"),
                                       rec("T:Fz.h$outer[({0}S:S$m$0_0$0({4}STin:S),Z,0,0)({4}S:S$n$0_0$0({1}SC:U),Z,0,0)]", "z_c")},
                                      {}, {});
        CFormatCtx mctx = pctx;
        mctx.store = &member_st;
        CLoc q;
        q.type = ty("{2}DG,STin:S");
        q.addr = 0xA000;
        const CEval e1 = c_eval_path(mctx, q, false, c_parse_path("q[1]").steps);
        check("CVALF-35", "a struct no variable is declared as: through a pointer `p->y` reads 4 and `*p` is {x = 3, y = 4} (the size is its members' extent, 4, "
                          "as no member overlaps another; p[1] is at +4); one declared only as a member of another struct has THAT size (q[1] is at +4, not the members' +2)",
              ey.ok && fy.ok && fy.text == "4" && ew.ok && ew.loc.type.size == 4 && ei.ok && ei.loc.addr == 0xB004 && fw.ok && fw.text == "{x = 3, y = 4}" && e1.ok && e1.loc.addr == 0xB004);
    }

    // ── CVALF-36: a U2 member of a nested struct is clamped to its own struct ────────────────────────────────
    {
        const auto rec = [](const char* text, const char* module) {
            CRecord r;
            r.text = text;
            r.module = module;
            r.record = cdb_decode(text).record;
            return r;
        };
        // struct flags (sccz80: lo and mid are ints at 0, hi an int at 1: hi sticks out of the 2-byte struct); struct o { struct flags f; char c; }
        const VariableStore st({}, {},
                               {rec("T:Ft.h$flags[({0}S:S$lo$0_0$0({2}SI:U),Z,0,0)({0}S:S$mid$0_0$0({2}SI:U),Z,0,0)({1}S:S$hi$0_0$0({2}SI:U),Z,0,0)]", "t_c"),
                                rec("T:Ft.h$o[({0}S:S$f$0_0$0({2}STflags:S),Z,0,0)({2}S:S$c$0_0$0({1}SC:U),Z,0,0)]", "t_c")},
                               {}, {});
        Mem mem;
        mem.put(0xB000, {0x8D, 0xC8, 0x55});   // f = 8D C8, c = $55: a read of hi past f would pick up $55
        CFormatCtx ctx;
        ctx.store = &st;
        ctx.module = "t_c";
        ctx.read = mem.reader();
        CLoc root;
        root.type = ty("{3}STo:S");
        root.addr = 0xB000;
        const CEval e = c_eval_path(ctx, root, false, c_parse_path("s.f.hi").steps);
        const CFormatted f = e.ok ? c_read_loc(ctx, e.loc) : CFormatted{};
        check("CVALF-36", "struct o { struct flags f; char c; } holding 8D C8 55: s.f.hi is 200, read inside f (the clamp is the immediately enclosing struct's end), "
                          "not 21960 from the next member's byte",
              e.ok && f.ok && f.text == "200");
    }

    // ── CVALF-37: a declared object of the context's own module sets a struct's size ──────────────────────────────
    {
        const auto rec = [](const char* text, const char* module) {
            CRecord r;
            r.text = text;
            r.module = module;
            r.record = cdb_decode(text).record;
            return r;
        };
        const auto var = [](const char* text, const char* module) {
            CVariable v;
            v.record = cdb_decode(text).record;
            v.text = text;
            v.module = module;
            v.c_name = v.record.var.name;
            return v;
        };
        // two modules define DIFFERENT structs under the tag `s`: a_c's is 4 bytes, b_c's 6; each module declares one object of its own
        const VariableStore st({},
                               {var("S:G$ga$0_0$0({4}STs:S),E,0,0", "a_c"), var("S:G$gb$0_0$0({6}STs:S),E,0,0", "b_c")},
                               {rec("T:Fa.h$s[({0}S:S$x$0_0$0({2}SI:S),Z,0,0)({2}S:S$y$0_0$0({2}SI:S),Z,0,0)]", "a_c"),
                                rec("T:Fb.h$s[({0}S:S$x$0_0$0({2}SI:S),Z,0,0)({2}S:S$y$0_0$0({2}SI:S),Z,0,0)({4}S:S$z$0_0$0({2}SI:S),Z,0,0)]", "b_c")},
                               {}, {});
        const auto size_in = [&](const char* module, const char* scope) {
            CFormatCtx c;
            c.store = &st;
            c.module = module;
            c.scope_name = scope;
            return c_leaf_size(c, ty("{2}DG,STs:S").chain, 1);
        };
        check("CVALF-37", "two modules with different structs under one tag: the size is the declared object of the context's own module or scope (4 in a_c, 6 in "
                          "b_c, by scope too), not the first object in the store",
              size_in("a_c", "") == 4 && size_in("b_c", "") == 6 && size_in("zz", "b.h") == 6 && size_in("zz", "a.h") == 4);
    }

    // ── CVALF-38: the integer and the real a value holds ───────────────────────────────────────────────────────
    {
        const uint8_t f48[6] = {0, 0, 0, 0, 0xA0, 0x82};
        const uint8_t b8[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        const uint8_t bf[2] = {0x8D, 0xC8};
        const auto i = [&](const char* chain, const uint8_t* b, size_t n) { return c_integer_value(ty(chain), b, n); };
        check("CVALF-38", "c_integer_value: signed and unsigned chars and ints, a pointer's address, a zsdcc bitfield (mid of 8D C8 is 17), 8 bytes as the 64-bit pattern, and "
                          "nothing for a float, a struct or a short buffer; c_float_value: the 48-bit -2.5, binary32, binary16",
              i("{1}SC:S", b8, 1) == std::optional<int64_t>(-1) && i("{1}SC:U", b8, 1) == std::optional<int64_t>(255) &&
                  i("{2}SI:S", bf, 2) == std::optional<int64_t>(static_cast<int16_t>(0xC88D)) && i("{2}SI:U", bf, 2) == std::optional<int64_t>(0xC88D) &&
                  i("{2}DG,SC:S", bf, 2) == std::optional<int64_t>(0xC88D) && i("{1}SB3$5:U", bf, 1) == std::optional<int64_t>(17) &&
                  i("{8}SL:S", b8, 8) == std::optional<int64_t>(-1) && !i("{6}SF:S", f48, 6) && !i("{5}STpoint:S", b8, 5) && !i("{4}SL:S", b8, 2) &&
                  c_float_value(ty("{6}SF:S"), f48, 6) == std::optional<double>(-2.5) && c_float_value(ty("{4}SF:S"), reinterpret_cast<const uint8_t*>("\x00\x00\xC0\x3F"), 4) == std::optional<double>(1.5) &&
                  c_float_value(ty("{2}S6:S"), reinterpret_cast<const uint8_t*>("\x00\x3E"), 2) == std::optional<double>(1.5) && !c_float_value(ty("{2}SI:S"), bf, 2));
    }

    // ── CVALF-39: a root object is read through its own reader, what a pointer points at through the CPU's ─────
    {
        // g_head (a `node_t *`) is in a page: its value comes from `read_root`; the node it points at is read from `read`, and the same address
        // holds different bytes in the two views
        Mem page, cpu;
        page.put(0x8000, {0x00, 0x90});                           // g_head = $9000 (in the page)
        page.put(0x9000, {0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE});   // what the PAGE holds at $9000
        cpu.put(0x9000, {0x00, 0x00, 0x2A, 0x00, 0x00, 0x00});    // what the CPU sees at $9000: next = NULL, val = 42
        CFormatCtx ctx = csc;
        ctx.read = cpu.reader();
        ctx.read_root = page.reader();
        CLoc head;
        head.type = ty("{2}DG,STnode:S");
        head.addr = 0x8000;
        head.rooted = true;
        const CPath p = c_parse_path("g_head->val");
        const CEval e = c_eval_path(ctx, head, false, p.steps);
        const CFormatted v = e.ok ? c_read_loc(ctx, e.loc) : CFormatted{};
        CLoc node;
        node.type = ty("{6}STnode:S");
        node.addr = 0x9000;
        node.rooted = true;   // a root object: its OWN bytes are the page's
        const CEval e2 = c_eval_path(ctx, node, false, c_parse_path("n.val").steps);
        const CFormatted v2 = e2.ok ? c_read_loc(ctx, e2.loc) : CFormatted{};
        check("CVALF-39", "a rooted location reads its own bytes from read_root and a followed pointer clears it: g_head->val is 42 from the CPU view, while the root "
                          "node itself reads the page's bytes (an 4-byte $EEEEEEEE = -286331154)",
              e.ok && !e.loc.rooted && v.ok && v.text == "42" && e2.ok && e2.loc.rooted && v2.ok && v2.text == "-286331154");
    }

    // ── CVALF-40: the file-qualified function path ────────────────────────────────────────────────────────────
    {
        const CPath a = c_parse_path("a.c::init::done");
        const CPath b = c_parse_path("init::done");
        const CPath c = c_parse_path("a.c::done");
        const CPath d = c_parse_path("a::init::done");
        const CPath e = c_parse_path("a.c::init::done.x[2]");
        check("CVALF-40", "`file.c::fn::name` is a function-scoped name told apart by its file; `fn::name` has no file; `file.c::name` is a file one; `a::init::done` (no extension "
                          "on the file) is refused with a reason; steps follow",
              a.ok && a.primary == CPath::Primary::Function && a.file == "a.c" && a.qualifier == "init" && a.name == "done" && b.ok && b.file.empty() && b.qualifier == "init" &&
                  c.ok && c.primary == CPath::Primary::File && c.file.empty() && !d.ok && d.why.find("a file name") != std::string::npos && e.ok && e.steps.size() == 2);
    }

    // ── CVALF-41: a pointer whose value is given (a register) is followed, anything else on it is refused ─────
    {
        Mem mem;
        mem.put(0xA000, {3, 0, 4, 0, 5, 0, 6, 0});
        CFormatCtx ctx = csc;
        ctx.read = mem.reader();   // no memory at the root's own address: the value must not be read from it
        CLoc p;
        p.type = ty("{2}DG,STpoint:S");
        p.addr = 0;
        p.ptr_value = 0xA000;
        CLoc q = p;
        q.type = ty("{2}DG,SI:S");
        const CEval y = c_eval_path(ctx, p, false, c_parse_path("p->y").steps);
        const CEval y1 = c_eval_path(ctx, p, false, c_parse_path("p[1].y").steps);
        const CEval whole = c_eval_path(ctx, p, true, {});
        const CEval dot = c_eval_path(ctx, p, false, c_parse_path("p.x").steps);
        const CEval ii = c_eval_path(ctx, q, true, {});
        check("CVALF-41", "a pointer whose value is given (HL = $A000) is followed by `->`, `[i]` and `*` without reading memory for it: p->y at $A002, p[1].y at $A006 (the "
                          "elements are 5 bytes) and *p; `.x` on it is refused (use ->); an int pointer's *q is at $A000",
              y.ok && y.loc.addr == 0xA002 && !y.loc.rooted && y1.ok && y1.loc.addr == 0xA005 + 2 && whole.ok && whole.loc.addr == 0xA000 && !dot.ok && ii.ok && ii.loc.addr == 0xA000);
    }

    // ── CVCH-01..06: the children of an object (GH #435) ─────────────────────────────────────────────────────────
    // Oracles: test/fixtures/z88dk/src/types.h and vars.c (the declarations), and the committed sccz80 / zsdcc records (a struct point is 5
    // bytes, packed: x at +0, y at +2, tag at +4; a struct rect is {a, b, name} at +0, +5, +10).
    {
        const CVariable* gpt = global_var(*sc.store, "g_pt");
        const CVariable* grect = global_var(*sc.store, "g_rect");
        CLoc pt;
        pt.type = gpt ? gpt->record.var.type : CdbType{};
        pt.addr = 0xA000;
        CLoc rect;
        rect.type = grect ? grect->record.var.type : CdbType{};
        rect.addr = 0xB000;
        const CChildren a = c_children(csc, pt, 0, 100);
        const CChildren b = c_children(csc, rect, 0, 100);
        check("CVCH-01", "g_pt (struct point) has the children x, y, tag (suffixes .x .y .tag) at +0, +2, +4 typed int, int, unsigned char; g_rect nests a and b "
                         "(struct point) and name (char [4]) at +0, +5, +10",
              a.ok && a.total == 3 && a.items.size() == 3 && a.items[0].label == "x" && a.items[0].suffix == ".x" && a.items[0].loc.addr == 0xA000 &&
                  a.items[1].label == "y" && a.items[1].suffix == ".y" && a.items[1].loc.addr == 0xA002 && a.items[2].label == "tag" &&
                  a.items[2].suffix == ".tag" && a.items[2].loc.addr == 0xA004 && c_type_text(a.items[0].loc.type) == "int" &&
                  c_type_text(a.items[1].loc.type) == "int" && c_type_text(a.items[2].loc.type) == "unsigned char" && b.ok && b.total == 3 &&
                  b.items.size() == 3 && b.items[0].label == "a" && b.items[0].loc.addr == 0xB000 && c_type_text(b.items[0].loc.type) == "struct point" &&
                  b.items[1].label == "b" && b.items[1].loc.addr == 0xB005 && c_type_text(b.items[1].loc.type) == "struct point" &&
                  b.items[2].label == "name" && b.items[2].loc.addr == 0xB00A && c_type_text(b.items[2].loc.type) == "char [4]");
    }
    {
        Mem mem;
        mem.put(0xA000, {0x00, 0xB0});   // struct point *p = $B000
        mem.put(0xA002, {0x00, 0x00});   // struct point *q = NULL
        CFormatCtx ctx = csc;
        ctx.read = mem.reader();
        CLoc p;
        p.type = ty("{2}DG,STpoint:S");
        p.addr = 0xA000;
        CLoc q = p;
        q.addr = 0xA002;
        const CChildren a = c_children(ctx, p, 0, 100);
        const CChildren n = c_children(ctx, q, 0, 100);
        check("CVCH-02", "a pointer to a struct lists the pointee's members with `->` (p->x at $B000, p->y at $B002, p->tag at $B004, read through the pointee's "
                         "address); a NULL pointer is refused as `null pointer`, with no children",
              a.ok && a.total == 3 && a.items.size() == 3 && a.items[0].suffix == "->x" && a.items[0].loc.addr == 0xB000 && a.items[1].suffix == "->y" &&
                  a.items[1].loc.addr == 0xB002 && a.items[2].suffix == "->tag" && a.items[2].loc.addr == 0xB004 && !n.ok && n.why == "null pointer" &&
                  n.items.empty() && n.total == 0);
    }
    {
        Mem mem;
        mem.put(0xA000, {0x00, 0xB0});   // int *p = $B000
        mem.put(0xA010, {0x00, 0xB1});   // int **pp = $B100
        mem.put(0xB100, {0x00, 0xB2});   // *pp = $B200
        CFormatCtx ctx;
        ctx.read = mem.reader();
        CLoc p;
        p.type = ty("{2}DG,SI:S");
        p.addr = 0xA000;
        CLoc pp;
        pp.type = ty("{2}DG,DG,SI:S");
        pp.addr = 0xA010;
        CLoc vp = p;
        vp.type = ty("{2}DG,SV:S");
        CLoc fp = p;
        fp.type = ty("{2}DG,DF,SV:S");
        const CChildren a = c_children(ctx, p, 0, 100);
        const CChildren b = c_children(ctx, pp, 0, 100);
        const CChildren b2 = b.ok && b.items.size() == 1 ? c_children(ctx, b.items[0].loc, 0, 100) : CChildren{};
        const CChildren v = c_children(ctx, vp, 0, 100);
        const CChildren f = c_children(ctx, fp, 0, 100);
        check("CVCH-03", "a pointer to int has one child `*` (suffix [0]) at the pointee; an int ** has one child [0] which has one child [0] (the `**pp` the path "
                         "grammar cannot spell); void * and a function pointer are leaves (ok, total 0)",
              a.ok && a.total == 1 && a.items.size() == 1 && a.items[0].label == "*" && a.items[0].suffix == "[0]" && a.items[0].loc.addr == 0xB000 &&
                  b.ok && b.items.size() == 1 && b.items[0].suffix == "[0]" && b.items[0].loc.addr == 0xB100 && b2.ok && b2.items.size() == 1 &&
                  b2.items[0].suffix == "[0]" && b2.items[0].loc.addr == 0xB200 && v.ok && v.total == 0 && v.items.empty() && f.ok && f.total == 0 &&
                  f.items.empty());
    }
    {
        CFormatCtx ctx;
        CLoc arr;
        arr.type = ty("{8}DA4x,SI:S");   // int a[4]: 8 bytes, 4 elements
        arr.addr = 0xA000;
        CLoc m2;
        m2.type = ty("{12}DA2x,DA3x,SI:S");   // int m[2][3]
        m2.addr = 0xB000;
        const CChildren all = c_children(ctx, arr, 0, 100);
        const CChildren win = c_children(ctx, arr, 1, 2);
        const CChildren past = c_children(ctx, arr, 4, 10);
        const CChildren two = c_children(ctx, m2, 0, 100);
        check("CVCH-04", "int a[4] has total 4 (its element count, not its 8 bytes) with [0]..[3] at +0, +2, +4, +6; first = 1, max = 2 gives [1] and [2]; first at "
                         "the end gives none (still total 4); a 2-D array's children are the int [3] rows, [1] at +6",
              all.ok && all.total == 4 && all.items.size() == 4 && all.items[3].label == "[3]" && all.items[3].suffix == "[3]" && all.items[3].loc.addr == 0xA006 &&
                  win.ok && win.total == 4 && win.items.size() == 2 && win.items[0].label == "[1]" && win.items[0].loc.addr == 0xA002 && win.items[1].label == "[2]" &&
                  win.items[1].loc.addr == 0xA004 && past.ok && past.total == 4 && past.items.empty() && two.ok && two.total == 2 && two.items.size() == 2 &&
                  c_type_text(two.items[1].loc.type) == "int [3]" && two.items[1].loc.addr == 0xB006);
    }
    {
        const CVariable* gw = global_var(*sc.store, "g_word");
        const CVariable* gf = global_var(*sd.store, "g_flags");
        CLoc w;
        w.type = gw ? gw->record.var.type : CdbType{};
        w.addr = 0xA000;
        Mem mem;
        mem.put(0xB000, {0x8D, 0xC8});
        CFormatCtx ctx = csd;
        ctx.read = mem.reader();
        CLoc fl;
        fl.type = gf ? gf->record.var.type : CdbType{};
        fl.addr = 0xB000;
        const CChildren u = c_children(csc, w, 0, 100);
        const CChildren f = c_children(ctx, fl, 0, 100);
        std::string vals;
        for (const CChild& c : f.items) {
            const CFormatted r = c_read_loc(ctx, c.loc);
            vals += c.label + "=" + (r.ok ? r.text : "!") + " ";
        }
        check("CVCH-05", "a union's members are all at +0 (g_word: w and b at $A000); zsdcc's g_flags over the bytes 8D C8 lists lo, mid, hi and each reads back "
                         "through c_read_loc as 5, 17, 200 (the bitfield chain is kept)",
              u.ok && u.total == 2 && u.items.size() == 2 && u.items[0].label == "w" && u.items[0].loc.addr == 0xA000 && u.items[1].label == "b" &&
                  u.items[1].loc.addr == 0xA000 && f.ok && f.total == 3 && vals == "lo=5 mid=17 hi=200 ");
    }
    {
        const auto rec = [](const char* text, const char* module) {
            CRecord r;
            r.text = text;
            r.module = module;
            r.record = cdb_decode(text).record;
            return r;
        };
        // two modules each define a different `struct s`; from a module that has neither, the lookup is ambiguous
        const VariableStore amb({}, {},
                                {rec("T:Fa.h$s[({0}S:S$x$0_0$0({2}SI:S),Z,0,0)]", "a_c"), rec("T:Fb.h$s[({0}S:S$y$0_0$0({2}SI:S),Z,0,0)]", "b_c")}, {}, {});
        CFormatCtx ctx;
        ctx.store = &amb;
        ctx.module = "zz";
        CLoc s;
        s.type = ty("{2}STs:S");
        s.addr = 0xA000;
        const CTypeLookup lk = c_find_type(amb, "s", "", "zz");
        const CChildren r = c_children(ctx, s, 0, 100);
        CLoc sc16;
        sc16.type = ty("{2}SI:S");
        sc16.addr = 0xA000;
        const CChildren leaf = c_children(csc, sc16, 0, 100);
        check("CVCH-06", "a scalar has no children (ok, total 0); a struct whose type lookup is ambiguous gets the lookup's own reason, not a guess",
              leaf.ok && leaf.total == 0 && leaf.items.empty() && !lk.record && !r.ok && r.why == lk.why && !r.why.empty() && r.items.empty());
    }

    {   // CVCH-07
        const auto rec = [](const char* text, const char* module) {
            CRecord r;
            r.text = text;
            r.module = module;
            r.record = cdb_decode(text).record;
            return r;
        };
        // struct s { uint8_t a; unsigned :2 (zsdcc writes __00000000); <unnamed C11 member>; uint8_t b; }: no path can name the 2nd and the 3rd
        const VariableStore st({}, {},
                               {rec("T:Fv$s[({0}S:S$a$0_0$0({1}SC:U),Z,0,0)({1}S:S$__00000000$0_0$0({1}SB0$2:U),Z,0,0)({2}S:S$$0_0$0({1}SC:U),Z,0,0)({3}S:S$b$0_0$0({1}SC:U),Z,0,0)]",
                                    "v_c")},
                               {}, {});
        CFormatCtx ctx;
        ctx.store = &st;
        ctx.scope_name = "v";
        ctx.module = "v_c";
        CLoc s;
        s.type = ty("{4}STs:S");
        s.addr = 0xA000;
        const CChildren c = c_children(ctx, s, 0, 100);
        check("CVCH-07", "a zsdcc padding member (__00000000) and an unnamed member of a struct are not children (no path can name them): struct s lists a and b, total 2, b at +3",
              c.ok && c.total == 2 && c.items.size() == 2 && c.items[0].label == "a" && c.items[1].label == "b" && c.items[1].loc.addr == 0xA003);
    }
    {   // CVCH-08
        const CVariable* gpt = global_var(*sc.store, "g_pt");
        CLoc pt;
        pt.type = gpt ? gpt->record.var.type : CdbType{};
        pt.addr = 0xA000;
        const CChildren one = c_children(csc, pt, 1, 1);
        const CChildren two = c_children(csc, pt, 1, 100);
        const CChildren past = c_children(csc, pt, 3, 100);
        check("CVCH-08", "a struct's members honour first and max: g_pt with first = 1, max = 1 is y alone at +2 (total still 3); first = 1 is y and tag; first at the end is none",
              one.ok && one.total == 3 && one.items.size() == 1 && one.items[0].label == "y" && one.items[0].loc.addr == 0xA002 && two.items.size() == 2 &&
                  two.items[0].label == "y" && two.items[1].label == "tag" && past.ok && past.total == 3 && past.items.empty());
    }
    {   // CVCH-09
        Mem mem;
        mem.put(0xA000, {0x00, 0xB0});   // int *p = $B000
        CFormatCtx ctx;
        ctx.read = mem.reader();
        CLoc p;
        p.type = ty("{2}DG,SI:S");
        p.addr = 0xA000;
        const CChildren later = c_children(ctx, p, 1, 100);
        const CChildren none = c_children(ctx, p, 0, 0);
        CLoc arr;
        arr.type = ty("{8}DA4x,SI:S");
        arr.addr = 0xC000;
        arr.rooted = true;
        const CChildren els = c_children(ctx, arr, 0, 100);
        bool rooted = els.ok && els.items.size() == 4;
        for (const CChild& c : els.items) rooted = rooted && c.loc.rooted;
        check("CVCH-09", "a pointer's one child exists only for first = 0 and max > 0 (first = 1 and max = 0 give none, total still 1); the elements of a rooted array are rooted "
                         "(read through the root object's own view)",
              later.ok && later.total == 1 && later.items.empty() && none.ok && none.total == 1 && none.items.empty() && rooted);
    }

    // ── CVINT-01..02 (GH #436): what is an int32 scalar, the one rule behind `cvar()` and `CVAR()` ─────────
    // Oracle: the DSL's own value model (lexer.h: "the value is the 32-bit pattern", so 0xCAFEBABE is -889275714), the
    // fixture's C types (vars.c: int16_t g_int = -5, uint16_t g_uint = 40000, uint32_t g_ulong = 0xCAFEBABE, a pointer is
    // its address) and the CDB chain spellings the compilers wrote. No fixture has an 8-byte integer, a binary16 or an
    // _Accum global, so those arms are reached with synthetic types.
    {
        const auto v = [](const char* chain, int64_t integer, uint32_t addr = 0) {
            CLoc l;
            l.type = ty(chain);
            l.addr = addr;
            return c_int32_scalar(l, integer);
        };
        const CInt32 sc = v("{1}SC:S", -1), uc = v("{1}SC:U", 255), ui = v("{2}SI:U", 40000), ul = v("{4}SL:U", 3405691582LL),
                     sl = v("{4}SL:S", -2), pt = v("{2}DG,STpoint:S", 0x8D21), bf = v("{1}SB3$5:U", 17), sca = v("{1}:U", 1);
        check("CVINT-01", "an integer scalar of at most 4 bytes is accepted as its 32-bit pattern: signed char -1 is -1, unsigned char "
                          "255 is 255, uint16 40000 is 40000, uint32 0xCAFEBABE is -889275714 (the DSL's pattern for that literal), "
                          "int32 -2 is -2, a pointer is its address 0x8D21, a bitfield 17, a bare scalar 1",
              sc.ok && sc.value == -1 && uc.ok && uc.value == 255 && ui.ok && ui.value == 40000 && ul.ok && ul.value == -889275714 &&
                  sl.ok && sl.value == -2 && pt.ok && pt.value == 0x8D21 && bf.ok && bf.value == 17 && sca.ok && sca.value == 1);
    }
    {
        const auto r = [](const char* chain, std::optional<int64_t> integer, const std::string& note = {}) {
            CLoc l;
            l.type = ty(chain);
            l.note = note;
            return c_int32_scalar(l, integer);
        };
        const CInt32 st = r("{5}STpoint:S", std::nullopt), ar = r("{8}DA4x,SI:S", std::nullopt), f6 = r("{6}SF:S", std::nullopt),
                     f4 = r("{4}SF:S", std::nullopt), h2 = r("{2}S6:S", std::nullopt), fx = r("{4}SQ:S", std::nullopt),
                     vo = r("{0}SV:S", std::nullopt), fn = r("{2}DF,SI:S", std::nullopt), big = r("{8}SL:S", -1),
                     u2 = r("{2}SI:S", 7, "bitfield layout unknown (z88dk U2)"), none = r("{2}SI:S", std::nullopt);
        const auto has = [](const CInt32& x, const char* needle) { return !x.ok && x.why.find(needle) != std::string::npos; };
        check("CVINT-02", "everything else is refused with its reason, never a value: a struct and an array are aggregates (named), "
                          "a 48-bit float, a binary32, a binary16 and a fixed-point are not integers, void and a function have no "
                          "value, an 8-byte integer does not fit 32 bits, a location carrying the U2 note says the note, and an "
                          "integer that was not read says so",
              has(st, "struct point is an aggregate") && has(ar, "is an aggregate") && has(f6, "float is not an integer") &&
                  has(f4, "float is not an integer") && has(h2, "is not an integer") && has(fx, "is not an integer") && has(vo, "void has no value") && has(fn, "has no value") &&
                  has(big, "8-byte integer does not fit the script's 32 bits") && u2.why == "bitfield layout unknown (z88dk U2)" && !u2.ok &&
                  has(none, "no integer value was read"));
    }

    // ── CVW-01..12 (GH #437): the bytes a written value stores (c_encode_value) ────────────────────────────
    // Oracles: the C types' ranges and little-endian layout (as the reader), the bytes z88dk compiled for floats.c (sccz80's
    // 48-bit and zsdcc's binary32: the compiler is the oracle, read from the fixture at run time), IEEE 754 binary16's table,
    // and the CDB bitfield records zsdcc wrote. A number that is a whole string literal is ID-shaped for the dup-ids gate, so
    // those are spelled in two pieces.
    {
        const auto enc = [](const char* chain, const std::string& text, std::vector<uint8_t> cur = {}) {
            CNewValue nv;
            nv.text = text;
            return c_encode_value(ty(chain), nv, cur.data(), cur.size());
        };
        const auto enc32 = [](const char* chain, int32_t v) {
            CNewValue nv;
            nv.from = CNewValue::From::Int32;
            nv.int32 = v;
            return c_encode_value(ty(chain), nv, nullptr, 0);
        };
        const auto is = [](const CEncoded& e, std::vector<uint8_t> want) { return e.ok && e.bytes == want; };
        const auto no = [](const CEncoded& e, const char* needle) {
            return !e.ok && e.bytes.empty() && e.why.find(needle) != std::string::npos;
        };
        const std::string k01 = std::string("0") + ".1", k25 = std::string("2") + ".5";

        check("CVW-01", "integers by width and sign are stored little-endian and range-checked, never truncated: char -128..127 and unsigned char 0..255, "
                        "int16 -32768..32767, uint16 0..65535 (-1 refused), int32 and uint32 at their bounds (a negative into uint32 refused), 8-byte "
                        "bounds, 18446744073709551616 refused",
              is(enc("{1}SC:S", "-128"), {0x80}) && is(enc("{1}SC:S", "127"), {0x7F}) &&
                  no(enc("{1}SC:S", "-129"), "out of range for char (-128..127)") && no(enc("{1}SC:S", "128"), "out of range") &&
                  is(enc("{1}SC:U", "0"), {0}) && is(enc("{1}SC:U", "255"), {0xFF}) && no(enc("{1}SC:U", "256"), "out of range") &&
                  is(enc("{2}SI:S", "-32768"), {0x00, 0x80}) && is(enc("{2}SI:S", "32767"), {0xFF, 0x7F}) && no(enc("{2}SI:S", "32768"), "out of range") &&
                  is(enc("{2}SI:U", "65535"), {0xFF, 0xFF}) && no(enc("{2}SI:U", "-1"), "out of range for unsigned int (0..65535)") &&
                  is(enc("{2}SI:U", "258"), {0x02, 0x01}) &&
                  is(enc("{4}SL:S", "-2147483648"), {0, 0, 0, 0x80}) && no(enc("{4}SL:S", "2147483648"), "out of range") &&
                  is(enc("{4}SL:U", "4294967295"), {0xFF, 0xFF, 0xFF, 0xFF}) && no(enc("{4}SL:U", "4294967296"), "out of range") &&
                  no(enc("{4}SL:U", "-1"), "out of range") &&
                  is(enc("{8}SL:S", "-9223372036854775808"), {0, 0, 0, 0, 0, 0, 0, 0x80}) && no(enc("{8}SL:S", "9223372036854775808"), "out of range") &&
                  is(enc("{8}SL:U", "18446744073709551615"), {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}) &&
                  no(enc("{8}SL:U", "18446744073709551616"), "out of range") && no(enc("{8}SL:U", "-1"), "out of range"));
        check("CVW-02", "number syntax: 0x1F, $1F, -0x10, +5 and a blank-padded 7 are accepted; an empty text, 0x, $, 12ab, --1 and `0x10 1` are not "
                        "numbers; a decimal fraction into an integer is not an integer",
              is(enc("{2}SI:S", "0x1F"), {0x1F, 0}) && is(enc("{2}SI:S", "$1F"), {0x1F, 0}) && is(enc("{2}SI:S", "-0x10"), {0xF0, 0xFF}) &&
                  is(enc("{2}SI:S", "+5"), {5, 0}) && is(enc("{2}SI:S", " 7 "), {7, 0}) && no(enc("{2}SI:S", ""), "is not a number") &&
                  no(enc("{2}SI:S", "0x"), "is not a number") && no(enc("{2}SI:S", "$"), "is not a number") &&
                  no(enc("{2}SI:S", "12ab"), "is not a number") && no(enc("{2}SI:S", "--1"), "is not a number") &&
                  no(enc("{2}SI:S", "0x10 1"), "is not a number") && no(enc("{2}SI:S", k15), "is not an integer") &&
                  no(enc("{2}SI:S", "1e3"), "is not an integer"));
        check("CVW-03", "char literals: 'Z' is 5A and ' ' is 20; an empty literal, two characters, a lone backslash and an unclosed literal are refused",
              is(enc("{1}SC:S", "'Z'"), {0x5A}) && is(enc("{1}SC:S", "' '"), {0x20}) && !enc("{1}SC:S", "''").ok && !enc("{1}SC:S", "'ab'").ok &&
                  !enc("{1}SC:S", "'\\'").ok && !enc("{1}SC:S", "'A").ok);
        check("CVW-04", "pointers: 0, $8D21 and 65535 give two little-endian bytes; -1 and 65536 are out of range",
              is(enc("{2}DG,SC:S", "0"), {0, 0}) && is(enc("{2}DG,SC:S", "$8D21"), {0x21, 0x8D}) && is(enc("{2}DG,SC:S", "65535"), {0xFF, 0xFF}) &&
                  no(enc("{2}DG,SC:S", "-1"), "out of range") && no(enc("{2}DG,SC:S", "65536"), "out of range"));
        {
            // Oracle: the bytes z88dk compiled for floats.c; the fixture is read here, never copied into the test
            const struct { const char* sym; std::string text; } kCases[] = {
                {"_f_15", k15}, {"_f_m25", "-" + k25}, {"_f_01", k01}, {"_f_1e10", "1e10"}, {"_f_1em10", "1e-10"}, {"_f_0", "0"}};
            bool ok48 = true, ok32 = true;
            for (const auto& c : kCases) {
                const std::vector<uint8_t> b6 = symbol_bytes("master/sccz80-floats", c.sym, 6);
                const std::vector<uint8_t> b4 = symbol_bytes("master/sdcc-floats", c.sym, 4);
                const CEncoded e6 = enc("{6}SF:S", c.text), e4 = enc("{4}SF:S", c.text);
                ok48 = ok48 && b6.size() == 6 && is(e6, b6);
                ok32 = ok32 && b4.size() == 4 && is(e4, b4);
            }
            check("CVW-05", "48-bit float (sccz80): 1.5, -2.5, 0.1, 1e10, 1e-10 and 0 encode to exactly the bytes z88dk compiled for them (round to nearest: "
                            "0.1 and 1e-10 tell truncation apart); 3 is 00 00 00 00 40 82, an integer text is accepted",
                  ok48 && is(enc("{6}SF:S", "3"), {0, 0, 0, 0, 0x40, 0x82}) && is(enc("{6}SF:S", "0x10"), {0, 0, 0, 0, 0x00, 0x85}));
            // a host whose global locale has a comma decimal point (QApplication calls setlocale(LC_ALL, "")): the text is still read as
            // C reads it, so 1.5 is not "1" followed by junk, and a comma is no decimal point
            const std::locale before = std::locale::global(std::locale(std::locale::classic(), new CommaPunct));
            const char* c_loc = std::setlocale(LC_ALL, "es_ES.UTF-8");
            const CEncoded comma_ok = enc("{6}SF:S", k15), comma_ok32 = enc("{4}SF:S", k15), comma_bad = enc("{4}SF:S", "1,5");
            std::setlocale(LC_ALL, "C");
            std::locale::global(before);
            (void)c_loc;
            check("CVW-06", "binary32 (zsdcc): the same six texts encode to the bytes z88dk compiled; under a global locale with a comma decimal point 1.5 still "
                            "encodes and `1,5` is not a number",
                  ok32 && is(comma_ok, {0, 0, 0, 0, 0x40, 0x81}) && is(comma_ok32, {0x00, 0x00, 0xC0, 0x3F}) && no(comma_bad, "is not a number"));
        }
        check("CVW-07", "float refusals, in 48-bit and binary32: 1e39 is out of range, 1e-50 underflows, nan, inf and -inf are not finite, abc is not a "
                        "number: never an infinity, a NaN or a 0",
              [&] {
                  for (const char* chain : {"{6}SF:S", "{4}SF:S"})
                      if (!(!enc(chain, "1e39").ok && !enc(chain, "1e-50").ok && no(enc(chain, "nan"), "not a finite number") &&
                            no(enc(chain, "inf"), "not a finite number") && no(enc(chain, "-inf"), "not a finite number") &&
                            no(enc(chain, "abc"), "is not a number") && !enc(chain, "1.5x").ok))
                          return false;
                  return no(enc("{6}SF:S", "1e39"), "out of range for float (z88dk 48-bit)");
              }());
        check("CVW-08", "binary16: 1.5 is 00 3E, 65504 is FF 7B, -2 is 00 C0; 65520 rounds to infinity and is refused; the subnormals: 2^-24 is 01 00, the largest "
                        "subnormal (1023 * 2^-24) is FF 03, 2^-14 (the smallest normal) is 00 04; 1e-10 underflows and is refused, 2^-25 rounds to zero and is refused",
              is(enc("{2}S6:S", k15), {0x00, 0x3E}) && is(enc("{2}S6:S", "65504"), {0xFF, 0x7B}) && is(enc("{2}S6:S", "-2"), {0x00, 0xC0}) &&
                  !enc("{2}S6:S", "65520").ok && is(enc("{2}S6:S", "5.9604644775390625e-8"), {0x01, 0x00}) &&
                  is(enc("{2}S6:S", "6.0975551605224609375e-5"), {0xFF, 0x03}) && is(enc("{2}S6:S", "6.103515625e-5"), {0x00, 0x04}) &&
                  no(enc("{2}S6:S", "1e-10"), "would underflow") && !enc("{2}S6:S", "2.98023223876953125e-8").ok);
        check("CVW-09", "zsdcc bitfields are read-modify-write: from 00, lo = 5 then mid = 17 give 8D, hi = 200 (its own unit) gives C8, the bytes the zsdcc "
                        "bitfield decode row reads back; from FF, mid = 0 gives 07 (the other bits kept); lo = 8 and mid = 32 are refused",
              [&] {
                  const CEncoded lo = enc("{1}SB0$3:U", "5", {0x00});
                  const CEncoded mid = enc("{1}SB3$5:U", "17", lo.bytes);
                  const CEncoded hi = enc("{1}SB0$8:U", "200", {0x00});
                  return is(lo, {0x05}) && is(mid, {0x8D}) && is(hi, {0xC8}) && is(enc("{1}SB3$5:U", "0", {0xFF}), {0x07}) &&
                         is(enc("{1}SB0$3:U", "0", {0xFF}), {0xF8}) && no(enc("{1}SB0$3:U", "8", {0}), "out of range") &&
                         no(enc("{1}SB3$5:U", "32", {0}), "out of range") && no(enc("{1}SB3$5:U", "1", {}), "shorter");
              }());
        {
            const CdbType sb = ty("{1}SB0$3:S"), sb5 = ty("{1}SB3$5:S");
            const uint8_t seven[1] = {0x07}, f8[1] = {0xF8}, three[1] = {0x03};
            CFormatCtx none;
            const CFormatted f = c_format_value(none, sb, seven, 1);
            check("CVW-10", "signed bitfields: a 3-bit :S field accepts -4..3 (-1 stores 07, 4 and -5 refused), and the reader sign-extends it: 07 reads -1, "
                            "03 reads 3, a 5-bit field at bit 3 holding 31 reads -1 (the read fix)",
                  is(enc("{1}SB0$3:S", "-1", {0}), {0x07}) && is(enc("{1}SB0$3:S", "-4", {0}), {0x04}) && is(enc("{1}SB0$3:S", "3", {0}), {0x03}) &&
                      no(enc("{1}SB0$3:S", "4", {0}), "out of range") && no(enc("{1}SB0$3:S", "-5", {0}), "out of range") && f.ok && f.text == "-1" &&
                      c_integer_value(sb, seven, 1) == std::optional<int64_t>(-1) && c_integer_value(sb, three, 1) == std::optional<int64_t>(3) &&
                      c_integer_value(sb5, f8, 1) == std::optional<int64_t>(-1) && c_format_value(none, sb, three, 1).text == "3");
        }
        check("CVW-11", "a script's int32: a 4-byte integer takes the 32-bit pattern whatever its sign (-889275714 is BE BA FE CA in uint32 and int32), a "
                        "smaller one its range (uint16 -1 refused, unsigned char 255 ok and 256 refused), an 8-byte integer does not fit, a float is not "
                        "an integer, a pointer takes 0..65535",
              is(enc32("{4}SL:U", -889275714), {0xBE, 0xBA, 0xFE, 0xCA}) && is(enc32("{4}SL:S", -889275714), {0xBE, 0xBA, 0xFE, 0xCA}) &&
                  is(enc32("{4}SL:S", -2), {0xFE, 0xFF, 0xFF, 0xFF}) && no(enc32("{2}SI:U", -1), "out of range") && is(enc32("{1}SC:U", 255), {0xFF}) &&
                  no(enc32("{1}SC:U", 256), "out of range") && no(enc32("{8}SL:S", 1), "does not fit the script's 32 bits") &&
                  no(enc32("{8}SL:U", 1), "does not fit the script's 32 bits") && no(enc32("{6}SF:S", 1), "is not an integer") &&
                  no(enc32("{4}SF:S", 1), "is not an integer") && is(enc32("{2}DG,SC:S", 0xFFFF), {0xFF, 0xFF}) && no(enc32("{2}DG,SC:S", -1), "out of range") &&
                  no(enc32("{2}DG,SC:S", 65536), "out of range"));
        check("CVW-12", "what has no scalar value is refused with its reason and no bytes: a struct and an array are aggregates (a member or an element is "
                        "written), void and a function are not values, a fixed-point layout is not decoded, a size of 0 or -1 is not known",
              no(enc("{5}STpoint:S", "1"), "struct point is an aggregate: write a member or an element") &&
                  no(enc("{8}DA4x,SI:S", "1"), "is an aggregate: write a member or an element") && no(enc("{0}SV:S", "1"), "not a value") &&
                  no(enc("{2}DF,SI:S", "1"), "not a value") && no(enc("{4}SQ:S", "1"), "fixed-point layout not decoded") &&
                  no(enc("{0}SI:S", "1"), "size of the object is not known") && no(enc("{-1}SI:S", "1"), "size of the object is not known"));
    }

    // ── GH #438: the S1 markers. Oracle: the C sources (fc.c, fm.c, bits.c, fl.c), the fixtures' records, the listings, IEEE 754 ──
    // A store with the listings beside the map read (the fastcall evidence of CConv::ListedFastcall).
    const auto listed = [](const char* fixture) {
        SourceMap map;
        load_z88dk_source_map(map, (kFixtures / fixture / "prog.map").string(), nex_space, Z88dkListings::Beside);
        return map.variables();
    };
    {   // CVZB-01
        const auto nominal = [&](const char* fixture, const char* fn, bool prologue = false) {
            const auto st = listed(fixture);
            if (!st) return std::string("no store");
            for (size_t i = 0; i < st->functions().size(); ++i)
                if (st->functions()[i].name == fn) return std::string(c_fastcall_records_nominal(*st, static_cast<int>(i), prologue) ? "yes" : "no");
            return std::string("no function");
        };
        check("CVZB-01", "c_fastcall_records_nominal (per function: fastcall and no parameter recorded below the frame): true for v2.4 and master sccz80's ffast (fc.c) and fm "
                         "(fm.c: a +6 and b +4 before z88dk#3229); false for master-fixed's ffast and fm (the register parameter is at -2), master 80cc's ffast and fm, "
                         "zsdcc's ffast (register parameter), and every fcallee (callee, not fastcall), even when the caller reports entry bytes that look like a fastcall prologue (a listing that names the function without the flag wins)",
              nominal("v2.4/sccz80-fc", "ffast") == "yes" && nominal("master/sccz80-fc", "ffast") == "yes" && nominal("v2.4/sccz80-fm", "fm") == "yes" &&
                  nominal("master/sccz80-fm", "fm") == "yes" && nominal("master-fixed/sccz80-fc", "ffast") == "no" && nominal("master-fixed/sccz80-fm", "fm") == "no" &&
                  nominal("master/80cc-fc", "ffast") == "no" && nominal("master/80cc-fm", "fm") == "no" && nominal("master/sdcc-fc", "ffast") == "no" &&
                  nominal("v2.4/sccz80-fc", "fcallee") == "no" && nominal("master/sccz80-fc", "fcallee") == "no" && nominal("master-fixed/sccz80-fc", "fcallee") == "no" &&
                  nominal("master/80cc-fc", "fcallee") == "no" && nominal("master/sdcc-fc", "fcallee") == "no" &&
                  nominal("master/sccz80-fc", "fcallee", true) == "no" && nominal("v2.4/sccz80-fc", "fcallee", true) == "no" && nominal("master/sccz80-fc", "ffast", true) == "yes");
    }
    // the type text of a member of struct `tag` named `member`
    const auto member_text = [](const char* fixture, const char* tag, const char* member) {
        Loaded l(fixture);
        for (const CRecord& r : l.store ? l.store->types() : std::vector<CRecord>())
            if (r.record.var.name == tag)
                for (const CdbMember& m : r.record.members)
                    if (m.var.name == member) return c_type_text(m.var.type);
        return std::string("-");
    };
    check("CVZB-02", "the type text of a bitfield: `SB0$4:S` (neg: zsdcc and fixed sccz80 and 80cc) is `int :4`, `SB0$3:U` (lo) is `unsigned int :3`, and zsdcc's plain `int s:4` "
                     "(`SB0$4:S`) is `int :4` while the same field is `unsigned int :4` on fixed sccz80 and 80cc",
          member_text("master/sdcc-bits", "bf", "neg") == "int :4" && member_text("master-fixed/sccz80-bits", "bf", "neg") == "int :4" &&
              member_text("master-fixed/80cc-bits", "bf", "neg") == "int :4" && member_text("master/sdcc-bits", "bf", "lo") == "unsigned int :3" &&
              member_text("master-fixed/sccz80-bits", "bf", "lo") == "unsigned int :3" && member_text("master/sdcc-bits", "one", "s") == "int :4" &&
              member_text("master-fixed/sccz80-bits", "one", "s") == "unsigned int :4" && member_text("master-fixed/80cc-bits", "one", "s") == "unsigned int :4");
    // g_bf, g_lone and g_one of bits.c formatted from the bytes the program leaves (bits.c: lo 5, mid 17, hi 200, neg -3, wide 2748, tag 66)
    const auto bits_text = [](const char* fixture, const char* scope, const char* global, std::initializer_list<uint8_t> bytes, std::vector<std::string>* notes = nullptr) {
        Loaded l(fixture);
        CFormatCtx ctx;
        ctx.store = l.store.get();
        ctx.scope_name = scope;
        ctx.module = "bits_c";
        const CVariable* v = l.store ? global_var(*l.store, global) : nullptr;
        if (!v) return std::string("no variable");
        const CFormatted f = fmt(ctx, v->record.var.type, bytes);
        if (notes) *notes = f.notes;
        return f.ok ? f.text : "!" + f.why;
    };
    {   // CVZB-03
        const std::string good = "{lo = 5, mid = 17, hi = 200, neg = -3, wide = 2748, tag = 66 'B'}";   // tag is a uint8_t: a printable byte shows its character
        std::vector<std::string> n1, n2, n3, n4, n5;
        const std::string a = bits_text("master-fixed/sccz80-bits", "bits.c", "g_bf", {0x8D, 0xC8, 0x0D, 0xBC, 0x0A, 0x42}, &n1);
        const std::string b = bits_text("master-fixed/80cc-bits", "bits.c", "g_bf", {0x8D, 0xC8, 0x0D, 0xBC, 0x0A, 0x42}, &n2);
        const std::string c = bits_text("master/sdcc-bits", "bits", "g_bf", {0x8D, 0xC8, 0x0D, 0xBC, 0x0A, 0x42}, &n3);
        const std::string d = bits_text("master/sccz80-bits", "bits.c", "g_bf", {0x8D, 0xC8, 0x0D, 0xBC, 0x0A, 0x42}, &n4);
        const std::string e = bits_text("master/80cc-bits", "bits.c", "g_bf", {0x8D, 0xC8, 0x0D, 0xBC, 0x0A, 0x42}, &n5);
        const auto marked = [&](const std::string& t, const char* member) {
            const size_t at = t.find(std::string(member) + " = ");
            if (at == std::string::npos) return false;
            const size_t end = t.find_first_of(",}", at);
            return t.substr(at, end - at).find("[bitfield layout unknown (z88dk U2)]") != std::string::npos;
        };
        check("CVZB-03", "g_bf = 8D C8 0D BC 0A 42 (bits.c: lo 5, mid 17, hi 200, neg -3, wide 2748, tag 66) reads exactly `{lo = 5, mid = 17, hi = 200, neg = -3, wide = 2748, tag = 66 'B'}` with no note on master-fixed sccz80 and 80cc "
                         "and on master zsdcc; on v2.4/master sccz80 and master 80cc (whole-int records) lo, mid, hi and neg carry the U2 marker, wide and tag do not and tag is 66 'B'",
              a == good && n1.empty() && b == good && n2.empty() && c == good && n3.empty() &&
                  marked(d, "lo") && marked(d, "mid") && marked(d, "hi") && marked(d, "neg") && !marked(d, "wide") && d.find("tag = 66 'B'") != std::string::npos && n4.size() == 1 &&
                  marked(e, "lo") && marked(e, "mid") && marked(e, "hi") && marked(e, "neg") && !marked(e, "wide") && e.find("tag = 66 'B'") != std::string::npos && n5.size() == 1);
    }
    {   // CVZB-04
        // g_lone: a 12-bit field whose unit shares no byte with another member, then a char: BC FA 09 (the field's upper unit bits hold FA, c is 9)
        check("CVZB-04", "g_lone = BC FA 09 reads {a = 2748, c = 9} once the record is exact (master-fixed sccz80, 80cc, master zsdcc); on the pre-fix record (a whole `{2}SI:U`, "
                         "overlapping nothing) it is `a = 64188` with no note (the documented residual: the padding bits are in the value); g_one = 0D reads `{s = 13}` on "
                         "fixed sccz80 and 80cc (a plain int bitfield is unsigned there: `SB0$4:U`), `{s = -3}` on zsdcc (`SB0$4:S`) and is refused on the pre-fix record "
                         "(`member 's' lies beyond the object`)",
              bits_text("master-fixed/sccz80-bits", "bits.c", "g_lone", {0xBC, 0xFA, 0x09}) == "{a = 2748, c = 9}" &&
                  bits_text("master-fixed/80cc-bits", "bits.c", "g_lone", {0xBC, 0xFA, 0x09}) == "{a = 2748, c = 9}" &&
                  bits_text("master/sdcc-bits", "bits", "g_lone", {0xBC, 0xFA, 0x09}) == "{a = 2748, c = 9}" &&
                  bits_text("master/sccz80-bits", "bits.c", "g_lone", {0xBC, 0xFA, 0x09}) == "{a = 64188, c = 9}" &&
                  bits_text("master/80cc-bits", "bits.c", "g_lone", {0xBC, 0xFA, 0x09}) == "{a = 64188, c = 9}" &&
                  bits_text("master-fixed/sccz80-bits", "bits.c", "g_one", {0x0D}) == "{s = 13}" && bits_text("master-fixed/80cc-bits", "bits.c", "g_one", {0x0D}) == "{s = 13}" &&
                  bits_text("master/sdcc-bits", "bits", "g_one", {0x0D}) == "{s = -3}" &&
                  bits_text("master/sccz80-bits", "bits.c", "g_one", {0x0D}) == "!member 's' lies beyond the object");
    }
    {   // CVZB-10
        // the (struct tag, member) pairs of the store whose member read through c_eval_path carries the U2 note
        const auto u2_members = [](const char* fixture) {
            Loaded l(fixture);
            std::set<std::string> out;
            if (!l.store) return std::string("no store");
            for (const CVariable& v : l.store->variables()) {
                const auto& ch = v.record.var.type.chain;
                if (ch.empty() || ch[0].kind != CdbTypeKind::Struct) continue;
                CFormatCtx ctx;
                ctx.store = l.store.get();
                ctx.scope_name = v.record.scope_name;
                ctx.module = v.module;
                const CTypeLookup lk = c_find_type(*l.store, ch[0].tag, ctx.scope_name, ctx.module);
                if (!lk.record) continue;
                for (const CdbMember& m : lk.record->record.members) {
                    if (m.var.name.empty()) continue;
                    CLoc root;
                    root.type = v.record.var.type;
                    root.addr = 0x9000;
                    CPathStep step;
                    step.kind = CPathStep::Kind::Dot;
                    step.name = m.var.name;
                    const CEval ev = c_eval_path(ctx, root, false, {step});
                    if (ev.ok && !ev.loc.note.empty()) out.insert(ch[0].tag + "." + m.var.name);
                }
            }
            std::string text;
            for (const std::string& m : out) text += (text.empty() ? "" : " ") + m;
            return text;
        };
        int fixed_maps = 0;
        bool none = true;
        for (const auto& d : fs::directory_iterator(kFixtures / "master-fixed")) {
            if (!fs::exists(d.path() / "prog.map")) continue;
            ++fixed_maps;
            none = none && u2_members(("master-fixed/" + d.path().filename().string()).c_str()).empty();
        }
        check("CVZB-10", "U2 switch-off: no struct member of any master-fixed map carries the overlapping-int marker (every bitfield is an SB record, sized by its own width); "
                         "struct flags' lo, mid and hi do on v2.4 and master sccz80 and on master 80cc, and none does on master zsdcc",
              fixed_maps == 9 && none && u2_members("v2.4/sccz80-cvars") == "flags.hi flags.lo flags.mid" && u2_members("master/sccz80-cvars") == "flags.hi flags.lo flags.mid" &&
                  u2_members("master/80cc-cvars") == "flags.hi flags.lo flags.mid" && u2_members("master/sdcc-cvars").empty());
    }
    {   // CVZB-11
        const std::vector<uint8_t> mbf = {0x00, 0x00, 0xD0, 0x82}, ieee = {0x00, 0x00, 0x50, 0xC0};   // -3.25 as mbf32 (exponent 82, mantissa D0) and as IEEE 754 binary32 (C0500000)
        const CdbType f4 = ty("{4}SF:S");
        Loaded lmb("master/classic-mbf32"), lm3("master/classic-math32");   // alive for the whole row: the contexts point into their stores
        CFormatCtx cmb, cm3;
        cmb.store = lmb.store.get();
        cm3.store = lm3.store.get();
        const auto mb = std::make_pair(lmb.store ? lmb.store->float_library() : std::string("no store"), cmb);
        const auto m3 = std::make_pair(lm3.store ? lm3.store->float_library() : std::string("no store"), cm3);
        const CFormatted rm = c_format_value(mb.second, f4, mbf.data(), 4), r3 = c_format_value(m3.second, f4, ieee.data(), 4), none = c_format_value(CFormatCtx{}, f4, ieee.data(), 4);
        const auto lib_refuses = [&](const char* lib) {
            const VariableStore st({}, {}, {}, {}, {}, {}, {}, lib);
            CFormatCtx ctx;
            ctx.store = &st;
            const CFormatted f = c_format_value(ctx, f4, ieee.data(), 4);
            return !f.ok && f.why == std::string("float format ") + lib + " is not read: the record does not say it";
        };
        const auto lib_reads = [&](const char* lib) {
            const VariableStore st({}, {}, {}, {}, {}, {}, {}, lib);
            CFormatCtx ctx;
            ctx.store = &st;
            const CFormatted f = c_format_value(ctx, f4, ieee.data(), 4);
            return f.ok && f.text == "-3.25";
        };
        check("CVZB-11", "float format from the map's library: master classic-mbf32 (`{4}SF`, bytes 00 00 D0 82) is refused `float format mbf32 is not read: the record does not say it`; classic-math32's 00 00 50 C0 is -3.25; mbf64, bbc and dai32 are refused like mbf32; am9511 and math48 read by size; a map that names no "
                         "library reads by size as before (-3.25, and 00 00 D0 82 as IEEE, the silent wrong value the refusal exists for)",
              mb.first == "mbf32" && !rm.ok && rm.why == "float format mbf32 is not read: the record does not say it" && m3.first == "math32" && r3.ok && r3.text == "-3.25" &&
                  none.ok && none.text == "-3.25" && c_format_value(CFormatCtx{}, f4, mbf.data(), 4).ok && lib_refuses("mbf64") && lib_refuses("bbc") && lib_refuses("dai32") &&
                  lib_reads("am9511") && lib_reads("math48") && lib_reads("math32"));
    }
    {   // CVZB-12
        Loaded l("v2.4/sccz80-fm");
        CFormatCtx ctx;
        ctx.store = l.store.get();
        const CVariable* ls = nullptr;
        if (l.store)
            for (const CVariable& v : l.store->variables())
                if (v.c_name == "ls") ls = &v;
        CLoc loc;
        if (ls) loc.type = ls->record.var.type;
        loc.addr = 0x9000;
        Mem mem;
        mem.put(0x9000, {1, 2, 3, 4});
        ctx.read = mem.reader();
        const CFormatted f = ls ? c_read_loc(ctx, loc) : CFormatted{};
        check("CVZB-12", "a listed `{-1}DA-1x` record (v2.4 sccz80's `char ls[] = \"loc\"` local, no size) is refused `an array of unknown length: its debug record has no size`; "
                         "its record is listed (not hidden: it has no sized twin)",
              ls && ls->record.hidden == CdbHidden::None && ls->record.var.type.size == -1 && !f.ok && f.why == "an array of unknown length: its debug record has no size");
    }

    // ── GH #438 S2: the scope walk and the stack-slot guard ──
    // Oracle: blocks.c's C scopes (f: t; the `if` block p; the `for` header and body i; the blocks u, v and z; g2: the `if` block q, the `for (;;)` body w; h: m, the
    // `for (int j ...)` j), the fixtures' records, and the parameters left out. A local declared later in an OPEN block is listed with it (the block rule
    // since #434), so f:14 shows z. `name[other]` is a local marked as sharing its stack slot with `other`.
    const auto walk_table = [](const char* fixture, const char* module, bool with_main = false) {
        Loaded l(fixture);
        std::string out;
        if (!l.store) return std::string("no store");
        const VariableStore& st = *l.store;
        for (size_t i = 0; i < st.functions().size(); ++i) {
            const CFunction& f = st.functions()[i];
            if (f.module != module || (f.name == "main" && !with_main)) continue;
            for (const CScope& sc : f.scopes) {
                std::vector<std::string> names;
                for (const CLocalRef& r : c_visible_locals(st, static_cast<int>(i), &sc))
                    if (r.kind != CVarKind::Param) names.push_back(r.var->c_name + (r.shares.empty() ? "" : "[" + r.shares + "]"));
                std::sort(names.begin(), names.end());
                std::string row;
                for (const std::string& n : names) row += (row.empty() ? "" : " ") + n;
                out += (out.empty() ? "" : "|") + f.name + ":" + std::to_string(sc.line) + "=" + row;
            }
        }
        return out;
    };
    const std::string kPreFix =
        "f:4=t|f:5=t|f:6=t|f:7=i[p] p[i] t|f:8=i[p] p[i] t|f:9=i[p] p[i] t|f:10=t|f:11=t|f:12=t|f:14=t u|f:16=t u v|f:17=t u v|f:18=t u v|f:19=t u|f:20=t u|f:21=t u|f:22=t|"
        "g2:25=|g2:26=|g2:27=q|g2:28=q|g2:29=q|g2:31=w|g2:32=w|g2:33=w|g2:34=|h:37=t|h:38=t|h:40=m t|h:41=m t|h:42=j m t|h:43=j m t|h:44=m t|h:45=m t|h:46=t";
    const std::string kFixed =
        "f:4=t|f:5=t|f:6=t|f:7=p t|f:8=p t|f:9=p t|f:10=t|f:11=i t|f:12=i t|f:14=t u z|f:16=t u v[z] z[v]|f:17=t u v[z] z[v]|f:18=t u v[z] z[v]|f:19=t u z|f:20=t u z|f:21=t u z|f:22=t|"
        "g2:25=|g2:26=|g2:27=q|g2:28=q|g2:29=q|g2:31=w|g2:32=w|g2:33=w|g2:34=|h:37=t|h:38=t|h:40=m t|h:41=m t|h:42=j m t|h:43=j m t|h:44=m t|h:45=m t|h:46=t";
    check("CVZB-05", "c_visible_locals at every scope entry of f, g2 and h of blocks.c: v2.4 and master sccz80 (block numbers name the last block opened, `for` raises a level with no block) list "
                     "t at f:11 (the for body: its i is the same pair as p, a documented pre-#3227 limit), t u at f:20 (z likewise), w at g2:31-33 (not q), m t at h:40-41 and j m t at "
                     "h:42-43; master-fixed lists i t at f:11, t u z at f:20, w at g2:31-33, m t and j m t; the pairs that share a slot are marked (i/p at f:7-9 before the fix, v/z at "
                     "f:16-18 after it)",
          walk_table("v2.4/sccz80-blocks", "blocks_c") == kPreFix && walk_table("master/sccz80-blocks", "blocks_c") == kPreFix && walk_table("master-fixed/sccz80-blocks", "blocks_c") == kFixed,
          walk_table("master-fixed/sccz80-blocks", "blocks_c"));
    {   // CVZB-06
        // which entries mark a pair: only the three of `i`/`p` before the fix and the three of `v`/`z` after it, in blocks.c; none in any cvars or fc fixture
        int marked_blocks = 0, marked_other = 0;
        for (const char* fx : {"v2.4/sccz80-blocks", "master/sccz80-blocks", "master-fixed/sccz80-blocks"}) {
            const std::string t = walk_table(fx, "blocks_c");
            for (size_t at = t.find('['); at != std::string::npos; at = t.find('[', at + 1)) ++marked_blocks;
        }
        for (const char* fx : {"v2.4/sccz80-cvars", "master/sccz80-cvars", "master/sdcc-cvars", "master/80cc-cvars", "master-fixed/sccz80-cvars", "master-fixed/80cc-cvars", "v2.4/sccz80-cvars-banked",
                               "master/sccz80-cvars-banked", "master/sdcc-cvars-banked", "master/80cc-cvars-banked", "v2.4/sccz80-fc", "master/sccz80-fc", "master/sdcc-fc", "master/80cc-fc",
                               "master-fixed/sccz80-fc", "master/sccz80-floats", "master/sdcc-floats", "master/sccz80-cross", "master/sccz80-extern"}) {
            const std::string t = walk_table(fx, "vars_c") + walk_table(fx, "fc_c") + walk_table(fx, "bk_c") + walk_table(fx, "other_c") + walk_table(fx, "floats_c") + walk_table(fx, "cross_c") +
                                  walk_table(fx, "ext_def_c") + walk_table(fx, "ext_use_c") + walk_table(fx, "bkmain_c");
            for (size_t at = t.find('['); at != std::string::npos; at = t.find('[', at + 1)) ++marked_other;
        }
        // master 80cc fm at `return y;` (line 7): b's record (-2) is y's slot, both marked
        Loaded fm("master/80cc-fm");
        std::string fmrow;
        if (fm.store)
            for (size_t i = 0; i < fm.store->functions().size(); ++i)
                if (fm.store->functions()[i].name == "fm")
                    for (const CScope& sc : fm.store->functions()[i].scopes)
                        if (sc.line == 7)
                            for (const CLocalRef& r : c_visible_locals(*fm.store, static_cast<int>(i), &sc)) fmrow += r.var->c_name + "[" + r.shares + "] ";
        check("CVZB-06", "the stack-slot guard marks i/p (v2.4 and master blocks.c f:7-9), v/z (master-fixed f:16-18: 2 names x 3 entries x 1 fixture = 6 marks each side) and master 80cc fm's "
                         "b/y at `return y;`; no other entry of blocks.c or of any cvars, fc, floats, cross or extern fixture is marked",
              marked_blocks == 18 && marked_other == 0 && fmrow == "b[y] a[] y[b] ", fmrow + " blocks=" + std::to_string(marked_blocks) + " other=" + std::to_string(marked_other));
    }
    {   // CVZB-07
        // synthetic: a 4-byte local at -6 and a 2-byte one at -4 overlap; -6/4 and -2/2 are neighbours and do not
        const auto rec = [](const char* name, int size, int off) {
            return cdb_decode(std::string("S:Lt.c.f$") + name + "$1_0$1({" + std::to_string(size) + "}" + (size == 4 ? "SL" : "SI") + ":S),B,1," + std::to_string(off)).record;
        };
        const auto marks = [&](std::initializer_list<std::tuple<const char*, int, int>> locals) {
            std::vector<CVariable> vars;
            for (const auto& [n, sz, off] : locals) {
                CVariable v;
                v.record = rec(n, sz, off);
                v.c_name = n;
                v.function = 0;
                vars.push_back(v);
            }
            CFunction f;
            f.name = "f";
            const VariableStore st({f}, vars, {}, {}, {});
            std::string out;
            for (const CLocalRef& r : c_visible_locals(st, 0, nullptr)) out += r.var->c_name + "[" + r.shares + "] ";
            return out;
        };
        check("CVZB-07", "synthetic: a 4-byte local at -6 and a 2-byte one at -4 overlap and name each other; -6/4 and -2/2 are neighbours and are not marked; three that overlap name both others; "
                         "an equal start is an overlap",
              marks({{"a", 4, -6}, {"b", 2, -4}}) == "a[b] b[a] " && marks({{"a", 4, -6}, {"b", 2, -2}}) == "a[] b[] " && marks({{"a", 4, -6}, {"b", 2, -4}, {"c", 2, -5}}) == "a[b, c] b[a, c] c[a, b] " &&
                  marks({{"a", 2, -4}, {"b", 2, -4}}) == "a[b] b[a] ");
    }
    {   // CVZB-08
        // no regression: the scopes of vars.c's sum and main, on every cvars fixture, are the C scopes (sum: acc t calls, inner at 38-40; main: arr big c i lp pp; no marks)
        bool ok = true;
        std::string seen;
        for (const char* fx : {"v2.4/sccz80-cvars", "master/sccz80-cvars", "master-fixed/sccz80-cvars"}) {
            const std::string t = walk_table(fx, "vars_c", true);
            const bool sum = t.find("sum:33=acc calls t|") != std::string::npos && t.find("sum:36=acc calls t|") != std::string::npos && t.find("sum:38=acc calls inner t|") != std::string::npos &&
                             t.find("sum:40=acc calls inner t|") != std::string::npos && t.find("sum:41=acc calls t") != std::string::npos;
            ok = ok && sum;
            // main shows every local at every entry (all at level 1 or 3, the blocks hold none of their own)
            size_t n_main = 0;
            for (size_t at = t.find("main:"); at != std::string::npos; at = t.find("main:", at + 1)) ++n_main;
            ok = ok && n_main >= 14 && t.find("main:49=arr big c i lp pp") != std::string::npos && t.find("main:66=arr big c i lp pp") != std::string::npos && t.find('[') == std::string::npos;
            if (!sum) seen += std::string(" ") + fx;
        }
        check("CVZB-08", "no regression: on v2.4, master and master-fixed sccz80-cvars the scope entries of sum list acc calls t (lines 33-36 and 41) and add inner at 38-40, main lists arr big c i lp pp "
                         "at every entry, and nothing is marked",
              ok, seen);
    }
    {   // CVZB-09
        // U6's switch-off: how often a record names a block the replay's stack does not hold at its level (the pre-#3227 numbering the replay compensates)
        const auto disagree = [&](const char* fixture, const char* module) {
            Loaded l(fixture);
            int n = 0;
            if (l.store)
                for (size_t i = 0; i < l.store->functions().size(); ++i)
                    if (l.store->functions()[i].module == module) n += c_trail_disagreements(*l.store, static_cast<int>(i));
            return n;
        };
        check("CVZB-09", "the block-number compensation is needed on v2.4 and master sccz80-blocks (f:19 names a level-3 block at level 2: at least one disagreement) and on sccz80-cvars (sum:41), "
                         "and never on master-fixed's sccz80 maps (blocks, cvars, fc)",
              disagree("v2.4/sccz80-blocks", "blocks_c") > 0 && disagree("master/sccz80-blocks", "blocks_c") > 0 && disagree("v2.4/sccz80-cvars", "vars_c") > 0 && disagree("master/sccz80-cvars", "vars_c") > 0 &&
                  disagree("master-fixed/sccz80-blocks", "blocks_c") == 0 && disagree("master-fixed/sccz80-cvars", "vars_c") == 0 && disagree("master-fixed/sccz80-cvars", "other_c") == 0 &&
                  disagree("master-fixed/sccz80-fc", "fc_c") == 0 && disagree("master-fixed/sccz80-fm", "fm_c") == 0 && disagree("master-fixed/sccz80-bits", "bits_c") == 0);
    }
    {   // CVZB-13
        // records where a `for` DOES open a block (z88dk#3254, proposed in z88dk PR #3258): f has t (level 1), an `if` block (2, p), a `for` whose header variable i
        // is block 3 at level 2 and whose body is block 4 at level 3 (k), then a block u (2, block 5). Numbers are in pre-order, as #3227 makes them.
        const auto rec = [](const std::string& name, int level, int block, int off) {
            return cdb_decode("S:Lt.c.f$" + name + "$" + std::to_string(level) + "_0$" + std::to_string(block) + "({2}SI:S),B,1," + std::to_string(off)).record;
        };
        const std::vector<std::tuple<std::string, int, int, int>> locals = {{"t", 1, 1, -2}, {"p", 2, 2, -4}, {"i", 2, 3, -4}, {"k", 3, 4, -6}, {"u", 2, 5, -4}};
        std::vector<CVariable> vars;
        for (const auto& [n, lv, bl, off] : locals) {
            CVariable v;
            v.record = rec(n, lv, bl, off);
            v.c_name = n;
            v.function = 0;
            vars.push_back(v);
        }
        const auto sc = [](uint32_t off, int line, int level, int block) {
            CScope c;
            c.offset = off;
            c.line = line;
            c.level = level;
            c.block = block;
            c.known = true;
            return c;
        };
        CFunction f;
        f.name = "f";
        f.compiler = CCompiler::Sccz80;
        f.trail = {sc(0, 4, 0, 0), sc(3, 5, 1, 1), sc(10, 7, 2, 2), sc(20, 10, 1, 1), sc(24, 11, 2, 3), sc(30, 12, 3, 4), sc(40, 14, 2, 3), sc(50, 17, 1, 1), sc(55, 18, 2, 5)};
        f.scopes = f.trail;
        const VariableStore st({f}, vars, {}, {}, {});
        const auto at = [&](uint32_t off) {
            const CScope* s = nullptr;
            for (const CScope& c : st.functions()[0].scopes)
                if (c.offset == off) s = &c;
            std::vector<std::string> n;
            for (const CLocalRef& r : c_visible_locals(st, 0, s)) n.push_back(r.var->c_name);
            std::sort(n.begin(), n.end());
            std::string out;
            for (const std::string& x : n) out += (out.empty() ? "" : " ") + x;
            return out;
        };
        // the walk is sccz80's: the same trail on a zsdcc function keeps the block rule (zsdcc numbers blocks exactly, not in pre-order): scope (level 2, block 5) after a
        // record of block 7 at the same level shows block 5's local, where the replay would keep block 7
        std::vector<CVariable> zv;
        for (const auto& [n, lv, bl] : std::vector<std::tuple<std::string, int, int>>{{"p", 2, 7}, {"q", 2, 5}}) {
            CVariable v;
            v.record = cdb_decode("S:Lt.f$" + n + "$" + std::to_string(lv) + "_0$" + std::to_string(bl) + "({2}SI:S),B,1,-4").record;
            v.c_name = n;
            v.function = 0;
            zv.push_back(v);
        }
        CFunction zf;
        zf.name = "f";
        zf.compiler = CCompiler::Zsdcc;
        zf.trail = {sc(0, 1, 0, 0), sc(3, 2, 1, 1), sc(5, 3, 2, 7), sc(10, 4, 2, 5)};
        zf.scopes = zf.trail;
        const VariableStore zst({zf}, zv, {}, {}, {});
        std::string zrow;
        for (const CLocalRef& r : c_visible_locals(zst, 0, &zst.functions()[0].scopes[3])) zrow += r.var->c_name + " ";
        CFunction sf = zf;
        sf.compiler = CCompiler::Sccz80;
        const VariableStore sst({sf}, zv, {}, {}, {});
        std::string srow;
        for (const CLocalRef& r : c_visible_locals(sst, 0, &sst.functions()[0].scopes[3])) srow += r.var->c_name + " ";
        // before z88dk#3227 a `for` directly in the function body: the body's records are level 3 with block 1 (the last opened), its i is `$3_0$1`; no level 2 exists, no record sets it
        std::vector<CVariable> pv;
        for (const auto& [n, lv, bl] : std::vector<std::tuple<std::string, int, int>>{{"t", 1, 1}, {"i", 3, 1}}) {
            CVariable v;
            v.record = cdb_decode("S:Lt.c.f$" + n + "$" + std::to_string(lv) + "_0$" + std::to_string(bl) + "({2}SI:S),B,1,-4").record;
            v.c_name = n;
            v.function = 0;
            pv.push_back(v);
        }
        CFunction pf;
        pf.name = "f";
        pf.compiler = CCompiler::Sccz80;
        pf.trail = {sc(0, 1, 0, 0), sc(3, 2, 1, 1), sc(8, 3, 3, 1)};
        pf.scopes = pf.trail;
        const VariableStore pst({pf}, pv, {}, {}, {});
        std::string prow;
        for (const CLocalRef& r : c_visible_locals(pst, 0, &pst.functions()[0].scopes[2])) prow += r.var->c_name + " ";
        check("CVZB-13", "a trail that names the `for` block in its own records (a shape sccz80 does not write: its header line stays in the enclosing block, see CVZB-14; this is the "
                         "walk's general case): in the `if` block t p (i, k and u are not open), in a header recorded inside the for block t i, in the for body t i k, after the loop t, in the "
                         "next block t u; and the walk is sccz80's only: the same trail on a zsdcc function keeps the block rule (shows q, block 5), on sccz80 the "
                         "replay keeps block 7 (shows p); before z88dk#3227 a `for` in the function body (records at level 3 naming block 1, no level 2 ever seen) shows its i",
              at(10) == "p t" && at(24) == "i t" && at(30) == "i k t" && at(40) == "i t" && at(50) == "t" && at(55) == "t u" && zrow == "q " && srow == "p " && prow == "t i ",
              at(10) + " | " + at(24) + " | " + at(30) + " | " + at(40) + " | " + at(55) + " | z=" + zrow + " s=" + srow + " p=" + prow);
    }

    {   // CVZB-14
        // The records sccz80 writes once a `for` opens a block (z88dk#3254, PR #3258; sccz80 built at upstream 96c0efaddd from issue_3254.t's program and a second program): the `for`
        // line stays in the ENCLOSING block, the body sits at level + 2 with the next block number, and NO record names the for block: it is known only from the header variable's
        // `S:` record (`i$2_0$3`). Numbers below are that compiler's own map: (offset, line, level, block) and (name, level, block, stack offset).
        struct Rec { uint32_t off; int line, level, block; };
        struct Var { const char* name; int level, block, off; };
        const auto visible = [](const std::vector<Rec>& trail, const std::vector<Var>& vars) {
            std::vector<CVariable> vs;
            for (const Var& v : vars) {
                CVariable cv;
                cv.record = cdb_decode(std::string("S:Lt.c.f$") + v.name + "$" + std::to_string(v.level) + "_0$" + std::to_string(v.block) + "({2}SI:S),B,1," + std::to_string(v.off)).record;
                cv.c_name = v.name;
                cv.function = 0;
                vs.push_back(cv);
            }
            CFunction f;
            f.name = "f";
            f.compiler = CCompiler::Sccz80;
            for (const Rec& r : trail) {
                CScope c;
                c.offset = r.off;
                c.line = r.line;
                c.level = r.level;
                c.block = r.block;
                c.known = true;
                f.trail.push_back(c);
            }
            // scopes: the owner of each address (the highest line)
            for (size_t i = 0; i < f.trail.size(); ++i)
                if (i + 1 == f.trail.size() || f.trail[i + 1].offset != f.trail[i].offset) f.scopes.push_back(f.trail[i]);
            const VariableStore st({f}, vs, {}, {}, {});
            std::string out;
            for (const CScope& sc : st.functions()[0].scopes) {
                std::vector<std::string> n;
                for (const CLocalRef& r : c_visible_locals(st, 0, &sc))
                    if (r.kind != CVarKind::Param) n.push_back(r.var->c_name);
                std::sort(n.begin(), n.end());
                std::string row;
                for (const std::string& x : n) row += (row.empty() ? "" : " ") + x;
                out += (out.empty() ? "" : "|") + std::to_string(sc.line) + "=" + row;
            }
            return out;
        };
        // issue_3254.t's program: f(a) { t = a; if (a) { p = t; t += p; } for (int i...) { for (int j...) t += i + j; } return t; }; i and p share slot -4
        const std::string f3254 = visible({{0, 3, 0, 0}, {3, 4, 1, 1}, {7, 5, 1, 1}, {15, 6, 2, 2}, {18, 7, 2, 2}, {34, 8, 2, 2}, {35, 9, 1, 1}, {59, 10, 3, 4}, {83, 11, 4, 5}, {113, 12, 3, 4}, {117, 13, 1, 1}},
                                         {{"a", 0, 0, 4}, {"t", 1, 1, -2}, {"p", 2, 2, -4}, {"i", 2, 3, -4}, {"j", 4, 5, -6}});
        // an empty sibling block `{ int z; }` (block 2, slot -4) before a `for (int i...)` (block 3, the same slot), whose body (3/4) holds k; then a `for` with NO declaration (its block, 5, is
        // named by nothing), whose body is 3/6 with m; then a block u (2/7). K: the fill must take the greatest candidate (i, block 3, not z), and skip the unnamed block
        const std::string f2 = visible({{0, 3, 0, 0}, {3, 4, 1, 1}, {7, 6, 1, 1}, {31, 7, 3, 4}, {43, 8, 3, 4}, {61, 9, 3, 4}, {66, 10, 1, 1}, {102, 11, 3, 6}, {106, 12, 3, 6}, {122, 13, 3, 6}, {126, 14, 1, 1},
                                        {126, 15, 2, 7}, {129, 16, 2, 7}, {145, 17, 2, 7}, {146, 18, 1, 1}},
                                       {{"a", 0, 0, 4}, {"t", 1, 1, -2}, {"z", 2, 2, -4}, {"i", 2, 3, -4}, {"k", 3, 4, -6}, {"m", 3, 6, -4}, {"u", 2, 7, -4}});
        check("CVZB-14", "the records sccz80 writes once a `for` opens a block (z88dk#3254, built from upstream 96c0efaddd): the `for` line is in the enclosing block, the body is level + 2 with the next "
                         "block number, no record names the for block; issue_3254.t's program shows L4-5 t, L6-8 p t, L9 t, L10 i t, L11 i j t, L12 i t, L13 t (the nested brace-less for's j at 4/5); with an "
                         "empty sibling block `{ int z; }` before the `for` (z and i share slot -4) L7-9 show i k t and never z, and a `for` with no declaration shows m t at L11-13 (the unnamed block is "
                         "skipped), then t u at L15-17",
              f3254 == "3=t|4=t|5=t|6=p t|7=p t|8=p t|9=t|10=i t|11=i j t|12=i t|13=t" && f2 == "3=t|4=t|6=t|7=i k t|8=i k t|9=i k t|10=t|11=m t|12=m t|13=m t|15=t u|16=t u|17=t u|18=t",
              f3254 + " || " + f2);
    }

    {   // CVZB-15
        // 80cc with z88dk PR #3243 (80cc built at upstream 96c0efaddd, issue_3254.t's program and f2.c): C_LINE records carry levels and blocks, in source-unordered code (f3254's line 12 is
        // at +140, after line 13 at +75), `for` variables `i$2_0$1`. Neither the sccz80 replay nor the block rule reads them right (a closed sibling shown, the variable in scope hidden), so every
        // block local stays `scope unknown`: listed, flagged, never hidden. Numbers: (offset, line, level, block) and (name, level, block, stack offset) of that compiler's map
        struct Rec { uint32_t off; int line, level, block; };
        struct Var { const char* name; int level, block, off; };
        const auto rows = [](const std::vector<Rec>& trail, const std::vector<Var>& vars) {
            std::vector<CVariable> vs;
            for (const Var& v : vars) {
                CVariable cv;
                cv.record = cdb_decode(std::string("S:Lt.c.f$") + v.name + "$" + std::to_string(v.level) + "_0$" + std::to_string(v.block) + "({2}SI:S),B,1," + std::to_string(v.off)).record;
                cv.c_name = v.name;
                cv.function = 0;
                vs.push_back(cv);
            }
            CFunction f;
            f.name = "f";
            f.compiler = CCompiler::Cc80;
            for (const Rec& r : trail) {
                CScope c;
                c.offset = r.off;
                c.line = r.line;
                c.level = r.level;
                c.block = r.block;
                c.known = true;
                f.trail.push_back(c);
            }
            f.scopes = f.trail;
            const VariableStore st({f}, vs, {}, {}, {});
            std::string out;
            for (const CScope& sc : st.functions()[0].scopes) {
                std::vector<std::string> n;
                for (const CLocalRef& r : c_visible_locals(st, 0, &sc))
                    if (r.kind != CVarKind::Param) n.push_back(r.var->c_name + (r.scope_unknown ? "?" : ""));
                std::sort(n.begin(), n.end());
                std::string row;
                for (const std::string& x : n) row += (row.empty() ? "" : " ") + x;
                out += (out.empty() ? "" : "|") + std::to_string(sc.line) + "=" + row;
            }
            return out;
        };
        const std::string f3254 = rows({{0, 3, 0, 0}, {19, 4, 1, 1}, {23, 5, 1, 1}, {27, 6, 2, 2}, {33, 7, 2, 2}, {37, 9, 2, 1}, {63, 10, 4, 3}, {72, 11, 4, 3}, {75, 13, 1, 1}, {140, 12, 2, 1}},
                                       {{"a", 0, 0, 4}, {"i", 2, 1, -4}, {"j", 4, 3, -2}, {"p", 2, 2, -6}, {"t", 1, 1, -8}});
        const std::string f2 = rows({{0, 3, 0, 0}, {13, 4, 1, 1}, {20, 6, 2, 1}, {46, 7, 3, 3}, {56, 8, 3, 3}, {73, 9, 2, 1}, {76, 10, 2, 1}, {87, 11, 3, 4}, {98, 12, 3, 4}, {105, 13, 2, 1}, {108, 15, 2, 5},
                                     {116, 16, 2, 5}, {120, 18, 1, 1}},
                                    {{"a", 0, 0, 4}, {"i", 2, 1, -8}, {"k", 3, 3, -6}, {"m", 3, 4, -4}, {"t", 1, 1, -12}, {"u", 2, 5, -2}, {"z", 2, 2, -10}});
        const std::string every = "i? j? p? t";
        std::string expect3254, expect2;
        for (const char* l : {"3", "4", "5", "6", "7", "9", "10", "11", "13", "12"}) expect3254 += std::string(expect3254.empty() ? "" : "|") + l + "=" + (std::string(l) == "3" || std::string(l) == "4" || std::string(l) == "5" ? "i? j? p? t" : every);
        for (const char* l : {"3", "4", "6", "7", "8", "9", "10", "11", "12", "13", "15", "16", "18"}) expect2 += std::string(expect2.empty() ? "" : "|") + l + "=i? k? m? t u? z?";
        check("CVZB-15", "80cc with PR #3243's block records (levels and blocks, code out of source order): every block local of f3254 and f2 stays listed and flagged `scope unknown` at every scope entry "
                         "(never hidden, never shown without the flag), t (level 1) is plain: a store whose scopes do not come from the listing (no listing beside the map) is not exact enough for the three-valued reading (CVZB-16)",
              f3254 == expect3254 && f2 == expect2, f3254 + " || " + f2);
    }

    // ── GH #438 S4a: 80cc's PR #3243 block records read three-valued, with the listing's scopes ─────────────
    struct Rec4 { uint32_t off; int line, level, block; };
    struct Var4 { const char* name; int level, block, off; };
    struct Sc3 { int line, level, block; };
    // The rows of one function: the listing's scopes in order (the flag `scopes_from_listing` set), `name?` for a local flagged `scope unknown`, a hidden local absent
    const auto cc80_rows = [](const std::vector<Rec4>& trail, const std::vector<Var4>& vars, const std::vector<Sc3>& scopes, bool* plain_block = nullptr, bool* bad_pick = nullptr) {
        std::vector<CVariable> vs;
        for (const Var4& v : vars) {
            CVariable cv;
            cv.record = cdb_decode(std::string("S:Lt.c.f$") + v.name + "$" + std::to_string(v.level) + "_0$" + std::to_string(v.block) + "({2}SI:S),B,1," + std::to_string(v.off)).record;
            cv.c_name = v.name;
            cv.function = 0;
            vs.push_back(cv);
        }
        CFunction f;
        f.name = "f";
        f.compiler = CCompiler::Cc80;
        f.scopes_from_listing = true;
        for (const Rec4& r : trail) {
            CScope c;
            c.offset = r.off;
            c.line = r.line;
            c.level = r.level;
            c.block = r.block;
            c.known = true;
            f.trail.push_back(c);
        }
        uint32_t off = 0;
        for (const Sc3& sc : scopes) {
            CScope c;
            c.offset = off++;
            c.line = sc.line;
            c.level = sc.level;
            c.block = sc.block;
            c.known = true;
            f.scopes.push_back(c);
        }
        const VariableStore st({f}, vs, {}, {}, {});
        std::string out;
        for (const CScope& sc : st.functions()[0].scopes) {
            std::vector<std::string> n;
            const std::vector<CLocalRef> vis = c_visible_locals(st, 0, &sc);
            if (bad_pick)
                for (const CLocalRef& r : vis)
                    if (const CPicked pk = c_pick_local(vis, r.var->c_name); pk.chosen && !pk.certain && !pk.chosen->scope_unknown) *bad_pick = true;
            for (const CLocalRef& r : vis) {
                if (r.kind != CVarKind::Param) n.push_back(r.var->c_name + (r.scope_unknown ? "?" : ""));
                if (plain_block && r.kind == CVarKind::Local && r.var->record.var.level >= 2 && !r.scope_unknown) *plain_block = true;
            }
            std::sort(n.begin(), n.end());
            std::string row;
            for (const std::string& x : n) row += (row.empty() ? "" : " ") + x;
            out += (out.empty() ? "" : "|") + std::to_string(sc.line) + "=" + row;
        }
        return out;
    };
    bool bad_pick = false;   // an uncertain pick whose chosen row is not flagged `scope unknown` (the write guard relies on there being none)
    bool any_plain_block_local = false;   // CVZB-16 and CVZB-20: an 80cc block local is never listed plain
    {   // CVZB-16
        // The records and the variables are those 80cc (upstream 96c0efaddd, PR #3243) wrote for eight functions of our own programs (f3254.c, f2.c, blocks.c, t1.c: a `for` with a
        // declaration, sibling `for`s, nested blocks, a brace-less inner `for`, `for(;;)`, a `__stdc` function), read through jnext's adapter. The expected text is the C scope of each line:
        // a name is plain only where the program's block holds it, absent where its block is closed or not yet opened, and `?` where the records cannot tell (a `for` variable, a block
        // under a `for`: f2.c's second `for` has the records of one `for`, k2 the records of a `for` body, g2 the records of two nested blocks).
        struct Case { const char* name; std::vector<Rec4> trail; std::vector<Var4> vars; std::vector<Sc3> scopes; const char* expect; };
        const std::vector<Case> cases = {
        {"f3254_f", {{0,3,0,0},{19,4,1,1},{23,5,1,1},{27,6,2,2},{33,7,2,2},{37,9,2,1},{63,10,4,3},{72,11,4,3},{75,13,1,1},{140,12,2,1}}, {{"a",0,0,4},{"i",2,1,-4},{"j",4,3,-2},{"p",2,2,-6},{"t",1,1,-8}}, {{3,0,0},{4,1,1},{5,1,1},{6,2,2},{7,2,2},{9,2,1},{10,4,3},{11,4,3},{13,1,1},{12,2,1}}, "3=t|4=t|5=t|6=p? t|7=p? t|9=i? t|10=i? j? t|11=i? j? t|13=t|12=i? t"},
        {"f2_h", {{0,3,0,0},{13,4,1,1},{20,6,2,1},{46,7,3,3},{56,8,3,3},{73,9,2,1},{76,10,2,1},{87,11,3,4},{98,12,3,4},{105,13,2,1},{108,15,2,5},{116,16,2,5},{120,18,1,1}}, {{"a",0,0,4},{"i",2,1,-8},{"k",3,3,-6},{"m",3,4,-4},{"t",1,1,-12},{"u",2,5,-2},{"z",2,2,-10}}, {{3,0,0},{4,1,1},{6,2,1},{7,3,3},{8,3,3},{9,2,1},{10,2,1},{11,3,4},{12,3,4},{13,2,1},{15,2,5},{16,2,5},{18,1,1}}, "3=t|4=t|6=i? t|7=i? k? t z?|8=i? k? t z?|9=i? t|10=i? t|11=i? m? t z?|12=i? m? t z?|13=i? t|15=t u?|16=t u?|18=t"},
        {"blocks_f", {{0,4,0,0},{19,5,1,1},{23,6,1,1},{34,7,2,2},{41,8,2,2},{45,10,2,1},{71,11,3,3},{88,12,2,1},{91,14,2,4},{99,16,3,5},{105,17,3,5},{109,19,2,4},{122,20,2,4},{126,22,1,1}}, {{"a",0,0,4},{"i",2,1,-8},{"p",2,2,-10},{"t",1,1,-12},{"u",2,4,-6},{"v",3,5,-4},{"z",2,4,-2}}, {{4,0,0},{5,1,1},{6,1,1},{7,2,2},{8,2,2},{10,2,1},{11,3,3},{12,2,1},{14,2,4},{16,3,5},{17,3,5},{19,2,4},{20,2,4},{22,1,1}}, "4=t|5=t|6=t|7=p? t|8=p? t|10=i? t|11=i? t|12=i? t|14=t u? z?|16=t u? v? z?|17=t u? v? z?|19=t u? z?|20=t u? z?|22=t"},
        {"blocks_g2", {{0,25,0,0},{16,26,1,6},{20,27,2,7},{24,28,2,7},{27,31,3,8},{36,32,3,8},{40,34,1,6}}, {{"a",0,0,4},{"q",2,7,-4},{"w",3,8,-2}}, {{25,0,0},{26,1,6},{27,2,7},{28,2,7},{31,3,8},{32,3,8},{34,1,6}}, "25=|26=|27=q?|28=q?|31=q? w?|32=q? w?|34="},
        {"blocks_h", {{0,37,0,0},{13,38,1,9},{21,40,2,10},{27,41,3,10},{53,42,4,11},{70,43,3,10},{73,44,2,10},{82,46,1,9}}, {{"a",0,0,4},{"j",3,10,-2},{"m",2,10,-4},{"t",1,9,-6}}, {{37,0,0},{38,1,9},{40,2,10},{41,3,10},{42,4,11},{43,3,10},{44,2,10},{46,1,9}}, "37=t|38=t|40=m? t|41=j? m? t|42=j? m? t|43=j? m? t|44=m? t|46=t"},
        {"t1_k1", {{0,4,0,0},{19,5,1,1},{23,6,1,1},{34,7,2,2},{41,8,2,2},{41,12,2,2},{58,9,3,3},{64,10,3,3},{68,11,3,3},{78,14,2,1},{87,17,2,1},{104,15,3,4},{110,16,3,4},{130,18,2,1},{156,19,3,5},{162,20,3,5},{179,21,2,1},{182,22,1,1}}, {{"a",0,0,4},{"i",2,1,-8},{"j",2,1,-4},{"r",2,2,-12},{"t",1,1,-14},{"w",3,3,-10},{"x",3,4,-6},{"y",3,5,-2}}, {{4,0,0},{5,1,1},{6,1,1},{7,2,2},{8,2,2},{9,3,3},{10,3,3},{11,3,3},{14,2,1},{15,3,4},{16,3,4},{17,2,1},{18,2,1},{19,3,5},{20,3,5},{21,2,1},{22,1,1}}, "4=t|5=t|6=t|7=r? t|8=r? t|9=r? t w?|10=r? t w?|11=r? t w?|14=i? j? t|15=i? j? t x?|16=i? j? t x?|17=i? j? t|18=i? j? t|19=i? j? t y?|20=i? j? t y?|21=i? j? t|22=t"},
        {"t1_k2", {{0,25,0,0},{19,26,1,6},{23,27,1,6},{27,28,3,7},{36,29,3,7},{39,35,1,6},{83,30,3,7},{109,31,4,8},{116,32,4,8},{133,33,3,7}}, {{"a",0,0,4},{"i",3,7,-6},{"j",3,7,-4},{"q",4,8,-2},{"t",1,6,-8}}, {{25,0,0},{26,1,6},{27,1,6},{28,3,7},{29,3,7},{35,1,6},{30,3,7},{31,4,8},{32,4,8},{33,3,7}}, "25=t|26=t|27=t|28=i? j? t|29=i? j? t|35=t|30=i? j? t|31=i? j? q? t|32=i? j? q? t|33=i? j? t"},
        {"t1_k3", {{0,38,0,9},{13,39,1,10},{18,41,2,11},{30,43,3,12},{37,45,4,13},{44,46,4,13},{48,51,2,14},{56,52,2,14},{60,54,1,10}}, {{"a",0,9,4},{"t",1,10,-10},{"u",2,11,-8},{"v",3,12,-6},{"w",4,13,-4},{"z",2,14,-2}}, {{38,0,9},{39,1,10},{41,2,11},{43,3,12},{45,4,13},{46,4,13},{51,2,14},{52,2,14},{54,1,10}}, "38=t|39=t|41=t u?|43=t u? v?|45=t u? v? w?|46=t u? v? w?|51=t z?|52=t z?|54=t"},
        };
        bool ok = true;
        std::string seen;
        for (const Case& c : cases) {
            const std::string got = cc80_rows(c.trail, c.vars, c.scopes, &any_plain_block_local, &bad_pick);
            if (got != c.expect) { ok = false; seen += std::string(" ") + c.name + ": " + got; }
        }
        check("CVZB-16", "80cc with PR #3243's records and the listing's scopes (GH #438 S4b: never plain): eight functions (f3254 f, f2 h, blocks f, g2, h, t1 k1, k2, k3 (`__stdc`)) give at every listed scope "
                         "the block locals that are not provably closed as `scope unknown` (`name?`), and none that are provably closed (absent); level-1 locals are plain; no block local is ever plain; an uncertain name pick is always a flagged row",
              ok && cases.size() == 8 && !any_plain_block_local && !bad_pick, seen);
    }
    {   // CVZB-20
        // GH #438 S4b (review 7 F1): 80cc stamps some code with the scope of a block the code is not in, so a block local is never plain. The records are those of 14 functions of our own programs,
        // built with 80cc 0f0a1cf70e: r1 f1 / r3 f3 / r4 f4 (if/else chains, loops, a shadowing inner block, an else body on one line), c1 c1..c6 (epilogue after a trailing block, `for`, `switch`,
        // nested else), d1..d4 (hoisted loop code, `switch`), r2 f2 (`__stdc`, nested ifs, `switch`, `while`), c9 k9 (a brace-less then-branch). The expected text hides what the stamp proves closed
        // and flags the rest; at the then-branch's closing jump (r1 line 11, r3 line 25, c9 line 10, r4 line 10) the else local is `q?`, not plain, and at c1's epilogue (line 9) `q?`
        struct Case { const char* name; std::vector<Rec4> trail; std::vector<Var4> vars; std::vector<Sc3> scopes; const char* expect; };
        const std::vector<Case> cases = {
        {"r1_f1", {{0,4,0,0},{13,5,1,1},{21,6,1,1},{38,7,2,2},{47,8,2,2},{50,11,2,3},{53,10,2,3},{65,13,1,1},{78,14,2,4},{85,15,2,4},{92,18,2,5},{100,19,2,5},{127,20,1,1},{141,22,2,6},{150,23,2,6},{160,25,1,1},{163,26,1,1},{175,27,2,7},{181,28,2,7},{196,31,2,8},{199,29,1,1},{205,30,2,8},{227,33,1,1}}, {{"a",0,0,4},{"d",2,5,-8},{"e",2,7,-4},{"h",2,8,-2},{"p",2,2,-14},{"q",2,3,-12},{"t",1,1,-16},{"t",2,6,-6},{"w",2,4,-10}}, {{4,0,0},{5,1,1},{6,1,1},{7,2,2},{8,2,2},{11,2,3},{10,2,3},{13,1,1},{14,2,4},{15,2,4},{18,2,5},{19,2,5},{20,1,1},{22,2,6},{23,2,6},{25,1,1},{26,1,1},{27,2,7},{28,2,7},{31,2,8},{29,1,1},{30,2,8},{33,1,1}}, "4=t|5=t|6=t|7=p? t|8=p? t|11=q? t|10=q? t|13=t|14=t w?|15=t w?|18=d? t|19=d? t|20=t|22=t t?|23=t t?|25=t|26=t|27=e? t|28=e? t|31=h? t|29=t|30=h? t|33=t"},
        {"r3_f3", {{0,4,0,0},{13,5,1,1},{21,6,1,1},{38,7,2,2},{47,8,2,2},{50,14,3,4},{53,10,2,3},{62,11,2,3},{65,13,3,4},{85,17,1,1},{97,18,2,5},{103,19,2,5},{118,25,2,7},{121,20,1,1},{127,21,2,6},{134,22,2,6},{146,24,2,7},{169,27,1,1},{182,28,2,8},{188,29,2,8},{192,30,3,9},{198,31,3,9},{202,34,3,10},{205,33,3,10},{217,38,2,11},{225,39,2,11},{252,40,1,1},{266,41,1,1}}, {{"a",0,0,4},{"d",2,11,-2},{"e",2,5,-14},{"h",2,6,-12},{"k",2,7,-10},{"p",2,2,-20},{"q",2,3,-18},{"r",3,4,-16},{"t",1,1,-22},{"u",3,9,-6},{"v",3,10,-4},{"w",2,8,-8}}, {{4,0,0},{5,1,1},{6,1,1},{7,2,2},{8,2,2},{14,3,4},{10,2,3},{11,2,3},{13,3,4},{17,1,1},{18,2,5},{19,2,5},{25,2,7},{20,1,1},{21,2,6},{22,2,6},{24,2,7},{27,1,1},{28,2,8},{29,2,8},{30,3,9},{31,3,9},{34,3,10},{33,3,10},{38,2,11},{39,2,11},{40,1,1},{41,1,1}}, "4=t|5=t|6=t|7=p? t|8=p? t|14=p? q? r? t|10=q? t|11=q? t|13=p? q? r? t|17=t|18=e? t|19=e? t|25=k? t|20=t|21=h? t|22=h? t|24=k? t|27=t|28=t w?|29=t w?|30=t u? w?|31=t u? w?|34=t v? w?|33=t v? w?|38=d? t|39=d? t|40=t|41=t"},
        {"r4_f4", {{0,4,0,0},{13,5,1,1},{21,6,1,1},{38,7,2,2},{47,8,2,2},{50,10,2,3},{65,12,1,1}}, {{"a",0,0,4},{"p",2,2,-4},{"q",2,3,-2},{"t",1,1,-6}}, {{4,0,0},{5,1,1},{6,1,1},{7,2,2},{8,2,2},{10,2,3},{12,1,1}}, "4=t|5=t|6=t|7=p? t|8=p? t|10=q? t|12=t"},
        {"c1_c1", {{0,4,0,0},{10,5,1,1},{18,6,1,1},{21,7,1,1},{38,8,2,2},{50,9,2,2}}, {{"a",0,0,4},{"q",2,2,-2},{"t",1,1,-4}}, {{4,0,0},{5,1,1},{6,1,1},{7,1,1},{8,2,2},{9,2,2}}, "4=t|5=t|6=t|7=t|8=q? t|9=q? t"},
        {"c1_c2", {{0,13,0,0},{13,14,1,3},{21,15,2,3},{47,16,3,4},{56,17,3,4},{75,18,4,5},{88,19,4,5},{115,21,2,3},{118,22,1,3}}, {{"a",0,0,4},{"b",3,4,-4},{"i",2,3,-6},{"q",4,5,-2},{"t",1,3,-8}}, {{13,0,0},{14,1,3},{15,2,3},{16,3,4},{17,3,4},{18,4,5},{19,4,5},{21,2,3},{22,1,3}}, "13=t|14=t|15=i? t|16=b? i? t|17=b? i? t|18=b? i? q? t|19=b? i? q? t|21=i? t|22=t"},
        {"c1_c3", {{0,25,0,0},{10,26,1,6},{18,36,1,6},{29,32,2,7},{42,33,3,8},{52,34,3,8},{58,29,2,7},{63,37,1,6}}, {{"a",0,0,4},{"q",3,8,-2},{"t",1,6,-4}}, {{25,0,0},{26,1,6},{36,1,6},{32,2,7},{33,3,8},{34,3,8},{29,2,7},{37,1,6}}, "25=t|26=t|36=t|32=t|33=q? t|34=q? t|29=t|37=t"},
        {"c1_c5", {{0,40,0,0},{13,41,1,9},{21,42,1,9},{38,43,2,10},{47,44,2,10},{50,49,3,12},{53,46,2,11},{62,47,2,11},{73,48,3,12},{83,52,1,9}}, {{"a",0,0,4},{"p",2,10,-6},{"q",3,12,-2},{"t",1,9,-8},{"z",2,11,-4}}, {{40,0,0},{41,1,9},{42,1,9},{43,2,10},{44,2,10},{49,3,12},{46,2,11},{47,2,11},{48,3,12},{52,1,9}}, "40=t|41=t|42=t|43=p? t|44=p? t|49=p? q? t z?|46=t z?|47=t z?|48=p? q? t z?|52=t"},
        {"c1_c6", {{0,55,0,0},{13,56,1,13},{21,57,1,13},{34,58,2,14},{40,59,2,14},{44,60,3,15},{51,61,3,15},{58,65,2,16},{66,66,2,16},{71,67,3,17},{77,68,3,17},{83,70,1,13},{97,71,1,13}}, {{"a",0,0,4},{"d",2,16,-4},{"q",3,15,-6},{"r",3,17,-2},{"t",1,13,-10},{"w",2,14,-8}}, {{55,0,0},{56,1,13},{57,1,13},{58,2,14},{59,2,14},{60,3,15},{61,3,15},{65,2,16},{66,2,16},{67,3,17},{68,3,17},{70,1,13},{71,1,13}}, "55=t|56=t|57=t|58=t w?|59=t w?|60=q? t w?|61=q? t w?|65=d? t|66=d? t|67=d? r? t w?|68=d? r? t w?|70=t|71=t"},
        {"c7_d1", {{0,4,0,0},{13,5,2,1},{31,6,3,2},{41,7,3,2},{60,8,4,3},{73,9,4,3},{79,11,2,1}}, {{"a",0,0,4},{"b",3,2,-4},{"i",2,1,-6},{"q",4,3,-2}}, {{4,0,0},{5,2,1},{6,3,2},{7,3,2},{8,4,3},{9,4,3},{11,2,1}}, "4=|5=i?|6=b? i?|7=b? i?|8=b? i? q?|9=b? i? q?|11=i?"},
        {"c7_d2", {{0,14,0,0},{13,15,1,4},{18,16,2,4},{27,17,3,5},{77,18,3,5},{102,19,2,4},{105,20,1,4}}, {{"1licm_0",0,0,-4},{"a",0,0,6},{"c",0,0,4},{"i",2,4,-6},{"k",3,5,-2},{"t",1,4,-8}}, {{14,0,0},{15,1,4},{16,2,4},{17,3,5},{18,3,5},{19,2,4},{20,1,4}}, "14=t|15=t|16=i? t|17=i? k? t|18=i? k? t|19=i? t|20=t"},
        {"c7_d3", {{0,23,0,0},{10,24,1,6},{20,25,2,7},{23,26,2,7},{34,27,3,8},{41,28,3,8},{44,31,1,6}}, {{"a",0,0,4},{"q",3,8,-2},{"r",2,7,-4}}, {{23,0,0},{24,1,6},{25,2,7},{26,2,7},{27,3,8},{28,3,8},{31,1,6}}, "23=|24=|25=r?|26=r?|27=q? r?|28=q? r?|31="},
        {"c7_d4", {{0,34,0,0},{9,44,1,9},{20,40,2,10},{37,41,3,11},{43,42,3,11},{49,37,2,10}}, {{"a",0,0,4},{"q",3,11,-2}}, {{34,0,0},{44,1,9},{40,2,10},{41,3,11},{42,3,11},{37,2,10}}, "34=|44=|40=|41=q?|42=q?|37="},
        {"r2_f2", {{0,4,0,1},{13,5,1,2},{21,6,1,2},{31,7,2,3},{43,8,2,3},{53,9,3,4},{62,10,3,4},{68,12,2,3},{82,14,1,2},{92,15,2,5},{101,19,2,5},{104,30,1,2},{115,29,2,7},{124,16,2,5},{148,17,3,6},{160,18,3,6},{177,20,2,5},{189,24,3,8},{198,25,3,8},{204,32,2,9},{213,34,3,10},{222,35,3,10},{228,38,1,2}}, {{"a",0,1,4},{"b",0,1,6},{"n",3,10,-2},{"o",2,9,-4},{"s",3,8,-6},{"t",1,2,-16},{"x",2,3,-14},{"x",3,4,-12},{"y",2,5,-10},{"z",3,6,-8}}, {{4,0,1},{5,1,2},{6,1,2},{7,2,3},{8,2,3},{9,3,4},{10,3,4},{12,2,3},{14,1,2},{15,2,5},{19,2,5},{30,1,2},{29,2,7},{16,2,5},{17,3,6},{18,3,6},{20,2,5},{24,3,8},{25,3,8},{32,2,9},{34,3,10},{35,3,10},{38,1,2}}, "4=t|5=t|6=t|7=t x?|8=t x?|9=t x? x?|10=t x? x?|12=t x?|14=t|15=t y?|19=t y?|30=t|29=t|16=t y?|17=t y? z?|18=t y? z?|20=t y?|24=s? t|25=s? t|32=o? t|34=n? o? t|35=n? o? t|38=t"},
        {"c9_k9", {{0,4,0,0},{10,5,1,1},{18,6,1,1},{35,7,1,1},{41,10,2,2},{44,9,2,2},{56,12,1,1}}, {{"a",0,0,4},{"q",2,2,-2},{"t",1,1,-4}}, {{4,0,0},{5,1,1},{6,1,1},{7,1,1},{10,2,2},{9,2,2},{12,1,1}}, "4=t|5=t|6=t|7=t|10=q? t|9=q? t|12=t"},
        };
        bool ok = true;
        std::string seen;
        for (const Case& c : cases) {
            const std::string got = cc80_rows(c.trail, c.vars, c.scopes, &any_plain_block_local, &bad_pick);
            if (got != c.expect) { ok = false; seen += std::string(" ") + c.name + ": " + got; }
        }
        check("CVZB-20", "80cc, the stale-stamp cases of review 7: fourteen functions give at every listed scope block locals that are `scope unknown` or absent, never plain; at the jump that ends a then-branch "
                         "(r1 f1 line 11, r3 f3 line 25, r4 f4 line 10, c9 k9 line 10) and in an epilogue after a trailing block (c1 line 9) the else or trailing local is `q?`; the property holds across CVZB-16 and CVZB-20; "
                         "80cc's leading-digit temporary `1licm_0` is not a parameter or a local",
              ok && cases.size() == 14 && !any_plain_block_local && !bad_pick, seen);
    }
    {   // CVZB-18
        // F3's upper bound is strict (review 7 M06): trail (5,1,1) (6,2,2) (8,1,1) (8,3,3), y in block 2, w in block 3, PC scope (8,3,3). An entry of the lower block 1 on the PC's own line (8) closes
        // nothing: block 3 can be a child of block 2 (the same line holds both), so y stays `y?`; hiding it would be unsound
        const std::string r = cc80_rows({{0, 5, 1, 1}, {1, 6, 2, 2}, {2, 8, 1, 1}, {3, 8, 3, 3}}, {{"t", 1, 1, -2}, {"y", 2, 2, -4}, {"w", 3, 3, -6}}, {{8, 3, 3}});
        check("CVZB-18", "an entry of a lower block on the PC's own line does not close the block before it: with trail (5,1,1) (6,2,2) (8,1,1) (8,3,3) at (8,3,3), y (block 2) and w (block 3) are `scope unknown`", r == "8=t w? y?", r);
    }
    {   // CVZB-19
        // c_pick_local: the innermost declaration of a name wins (the greatest level, then block), as gdb does; `certain` is false when a candidate is flagged `scope unknown`
        const auto mk = [](const char* name, int level, int block, bool unknown) {
            static std::vector<std::unique_ptr<CVariable>> keep;
            auto v = std::make_unique<CVariable>();
            v->record = cdb_decode(std::string("S:Lt.c.f$") + name + "$" + std::to_string(level) + "_0$" + std::to_string(block) + "({2}SI:S),B,1,-2").record;
            v->c_name = name;
            CLocalRef r;
            r.var = v.get();
            r.scope_unknown = unknown;
            keep.push_back(std::move(v));
            return r;
        };
        const CLocalRef t1 = mk("t", 1, 1, false), t6 = mk("t", 2, 6, false), t6u = mk("t", 2, 6, true), par = mk("a", 0, 0, false), loc = mk("a", 2, 3, false), t7 = mk("t", 2, 7, false);
        const std::vector<CLocalRef> va = {t1, t6}, vb = {t1, t6u}, vc = {par, loc}, vd = {t1}, ve = {t6, t7};   // the picks point into these
        const CPicked a = c_pick_local(va, "t"), b = c_pick_local(vb, "t"), c = c_pick_local(vc, "a"), d = c_pick_local(vd, "t"), e = c_pick_local(ve, "t"), none = c_pick_local(vd, "zz");
        check("CVZB-19", "c_pick_local: (a) t at level 1 and level 2 block 6 pick block 6, certain, one other; (b) the inner one flagged `scope unknown`: still block 6, not certain; (c) a parameter and a block local "
                         "of one name pick the local; (d) a single candidate is certain with no others; (e) two at one level pick the greater block; no candidate picks nothing; the ambiguous note names the count and the pick",
              a.chosen && a.chosen->var->record.var.block == 6 && a.certain && a.others.size() == 1 && b.chosen && b.chosen->var->record.var.block == 6 && !b.certain && c.chosen && c.chosen->var->record.var.level == 2 &&
                  d.chosen && d.certain && d.others.empty() && e.chosen && e.chosen->var->record.var.block == 7 && !none.chosen &&
                  c_ambiguous_note(b) == "ambiguous: 2 variables named t may be in scope here; this is the innermost (level 2, block 6)");
    }
    {   // CVZB-21
        // GH #438 S4b2 (review 8 G1): a block-scoped static local is in scope only in its block, like a block local of the same (level, block). Function f of statics.c: t (level 1), u (2/2),
        // `static t` (3/3, an E-space record), then `g += t` at (13, 2/2) and `t++` at (15, 1/1) where the static is out of scope. Exact blocks (zsdcc's way): the static is listed at line 11 only;
        // 80cc with the listing's scopes: flagged at 11, hidden outside; a level-1 static is listed everywhere
        const auto rows = [](CCompiler cc, bool exact80) {
            std::vector<CVariable> vs;
            const auto add = [&](const std::string& rec, const char* name) {
                CVariable v;
                v.record = cdb_decode(rec).record;
                v.c_name = name;
                v.function = 0;
                vs.push_back(v);
            };
            add("S:Ls1.c.f$t$1_0$1({2}SI:S),B,1,-2", "t");
            add("S:Ls1.c.f$u$2_0$2({2}SI:S),B,1,-4", "u");
            add("S:Fs1.c$st_f_t$3_0$3({2}SI:S),E,0,0", "t");
            add("S:Fs1.c$st_f_k$1_0$1({2}SI:S),E,0,0", "k");
            CFunction f;
            f.name = "f";
            f.compiler = cc;
            f.scopes_from_listing = exact80;
            for (const Sc3 sc : {Sc3{5, 1, 1}, Sc3{7, 2, 2}, Sc3{11, 3, 3}, Sc3{13, 2, 2}, Sc3{15, 1, 1}}) {
                CScope c;
                c.offset = static_cast<uint32_t>(f.scopes.size());
                c.line = sc.line;
                c.level = sc.level;
                c.block = sc.block;
                c.known = true;
                f.scopes.push_back(c);
            }
            f.trail = f.scopes;
            const VariableStore st({f}, vs, {}, {}, {});
            std::string out;
            for (const CScope& sc : st.functions()[0].scopes) {
                std::vector<std::string> n;
                for (const CLocalRef& r : c_visible_locals(st, 0, &sc)) n.push_back(r.var->c_name + (r.kind == CVarKind::StaticLocal ? "s" : "") + (r.scope_unknown ? "?" : ""));
                std::sort(n.begin(), n.end());
                std::string row;
                for (const std::string& x : n) row += (row.empty() ? "" : " ") + x;
                out += (out.empty() ? "" : "|") + std::to_string(sc.line) + "=" + row;
            }
            return out;
        };
        const std::string exact = rows(CCompiler::Zsdcc, false), c80 = rows(CCompiler::Cc80, true);
        check("CVZB-21", "a block static (level 3, block 3) is listed where its block is open and hidden elsewhere, like a block local; a level-1 static (k) is listed at every line. Exact blocks list `ts` at line 11 only; "
                         "80cc with the listing's scopes lists it flagged (`ts?`) at line 11 and hides it at 13 and 15 (block 3 > block 2, F1), its block locals flagged",
              exact == "5=ks t|7=ks t u|11=ks t ts u|13=ks t u|15=ks t" && c80 == "5=ks t|7=ks t u?|11=ks t ts? u?|13=ks t u?|15=ks t", exact + " || " + c80);
    }
    {   // CVZB-22
        // review 9 H1: a LEVEL-2 block static (an `else` or a bare block directly in the function body) out-picks a parameter or a level-1 local of its name only inside its block. Function f: parameter a,
        // local t (1/1), `static a` and `static t` both at 2/2. Exact blocks: at line 7 (2/2) the statics are listed and win the pick (kind StaticLocal, the others are the shadowed candidates); at
        // lines 5 and 9 (1/1) they are hidden and the name means the parameter and the local, which is where a write goes
        std::vector<CVariable> vs;
        const auto add = [&](const std::string& rec, const char* name) {
            CVariable v;
            v.record = cdb_decode(rec).record;
            v.c_name = name;
            v.function = 0;
            vs.push_back(v);
        };
        add("S:Lt.c.f$a$0_0$0({2}SI:S),B,1,4", "a");
        add("S:Lt.c.f$t$1_0$1({2}SI:S),B,1,-2", "t");
        add("S:Ft.c$st_f_a$2_0$2({2}SI:S),E,0,0", "a");
        add("S:Ft.c$st_f_t$2_0$2({2}SI:S),E,0,0", "t");
        CFunction f;
        f.name = "f";
        f.compiler = CCompiler::Zsdcc;
        for (const Sc3 sc : {Sc3{5, 1, 1}, Sc3{7, 2, 2}, Sc3{9, 1, 1}}) {
            CScope c;
            c.offset = static_cast<uint32_t>(f.scopes.size());
            c.line = sc.line;
            c.level = sc.level;
            c.block = sc.block;
            c.known = true;
            f.scopes.push_back(c);
        }
        f.trail = f.scopes;
        const VariableStore st({f}, vs, {}, {}, {});
        const auto kind_of = [&](size_t scope_index, const char* name) {
            const std::vector<CLocalRef> vis = c_visible_locals(st, 0, &st.functions()[0].scopes[scope_index]);
            const CPicked pk = c_pick_local(vis, name);
            return pk.chosen ? static_cast<int>(pk.chosen->kind) + 10 * static_cast<int>(vis.size()) : -1;
        };
        // kind 0 Param, 1 Local, 2 StaticLocal; + 10 * the number of visible rows (4 inside the block, 2 outside)
        check("CVZB-22", "a level-2 block static (static a, static t at block 2) wins the pick over the parameter a and the level-1 local t inside its block (line 7: StaticLocal, four rows) and is hidden outside it "
                         "(lines 5 and 9: the pick is the Param a and the Local t, two rows)",
              kind_of(0, "a") == 20 + 0 && kind_of(0, "t") == 20 + 1 && kind_of(1, "a") == 40 + 2 && kind_of(1, "t") == 40 + 2 && kind_of(2, "a") == 20 + 0 && kind_of(2, "t") == 20 + 1,
              std::to_string(kind_of(0, "a")) + " " + std::to_string(kind_of(1, "a")) + " " + std::to_string(kind_of(2, "t")));
    }
    {   // CVZB-23
        // the debugger's own row handles (`#12`, then the usual steps) parse only when the caller says so (the Variables panel's rows); a typed surface parses without it and refuses a leading `#`
        const CPath a = c_parse_path("#12", true), b = c_parse_path("#7.x[2]->y", true), c = c_parse_path("*#3", true), d = c_parse_path("#", true), e = c_parse_path("#x", true), f = c_parse_path("fn::#3", true),
                    g = c_parse_path("t"), h = c_parse_path("#12"), i = c_parse_path("*#3"), j = c_parse_path("#7.x");
        check("CVZB-23", "c_parse_path with allow_handle: `#12` is a handle (index 12), `#7.x[2]->y` a handle with three steps, `*#3` a dereferenced handle; `#`, `#x` and `fn::#3` are refused; without it (every typed "
                         "surface) `#12`, `*#3` and `#7.x` are refused like any bad path; a name is a name either way",
              a.ok && a.primary == CPath::Primary::Handle && a.handle == 12 && a.steps.empty() && b.ok && b.primary == CPath::Primary::Handle && b.handle == 7 && b.steps.size() == 3 && c.ok && c.deref &&
                  c.primary == CPath::Primary::Handle && c.handle == 3 && !d.ok && !e.ok && !f.ok && g.ok && g.primary == CPath::Primary::Name && g.name == "t" && !h.ok && !i.ok && !j.ok);
    }
    {   // CVZB-17
        // The line proofs are strict, and beta is the lowest block. Expected values: the three-valued rule applied by hand to each trail (the programs would be hand-written C of these shapes)
        const std::string c1 = cc80_rows({{0, 5, 1, 1}, {1, 8, 1, 1}, {2, 8, 2, 2}, {3, 9, 3, 3}, {4, 10, 3, 3}}, {{"t", 1, 1, -2}, {"y", 2, 2, -4}, {"w", 3, 3, -6}}, {{9, 3, 3}, {10, 3, 3}});
        const std::string c2 = cc80_rows({{0, 5, 1, 1}, {1, 8, 2, 2}, {2, 9, 2, 2}, {3, 9, 3, 3}, {4, 10, 3, 3}, {5, 11, 1, 1}}, {{"t", 1, 1, -2}, {"y", 2, 2, -4}, {"w", 3, 3, -6}}, {{10, 3, 3}});
        const std::string c3 = cc80_rows({{0, 3, 2, 2}, {1, 4, 2, 2}, {2, 5, 2, 3}, {3, 6, 2, 3}}, {{"x", 2, 2, -2}, {"y", 2, 3, -4}}, {{3, 2, 2}, {5, 2, 3}});
        check("CVZB-17", "the proofs are strict: an outer entry on y's own line closes nothing (y stays `?` at lines 9 and 10 beside w); a line shared by two blocks makes no parent (y never plain, w `?`); with no level-1 data beta is "
                         "the lowest block, so at the first block's line x is `?` and y absent, and at the second's x is absent and y `?` (S4b: never plain)",
              c1 == "9=t w? y?|10=t w? y?" && c2 == "10=t w? y?" && c3 == "3=x?|5=y?", c1 + " || " + c2 + " || " + c3);
    }

    // ── GH #477: floats in the format the map records (z88dk#3240) ──────────────────────────────────────────────────────
    // Oracles: sccz80's const.c `dofloat_*` and the bytes it wrote for -3.25, 1.5, -2, 100, 2.5 and 0.1 in 20 builds (the plan's probe
    // builds of fpfmt.c, read by an independent Python decoder), z88math's bbcmath.asm (the z88 integer form), the C source's values.
    {   // CVFMT-22 (the fallback part): no committed map (z88dk 2.4, master before 2026-11-06, zsdcc) records a float format
        int maps = 0, clean = 0;
        std::string bad;
        for (const char* root : {"v2.4", "v2.4-dotn", "master", "master-fixed", "master-fixed-80cc", "master-fixed-sccz80", "master-fixed-zsdcc"}) {
            if (!fs::is_directory(kFixtures / root)) continue;
            for (const auto& d : fs::recursive_directory_iterator(kFixtures / root)) {
                if (d.path().filename() != "prog.map") continue;
                ++maps;
                std::ifstream in(d.path());
                const std::string text((std::istreambuf_iterator<char>(in)), {});
                SourceMap map;
                load_z88dk_source_map(map, d.path().string(), nex_space);
                bool none = text.find("__fp_mode") == std::string::npos;
                if (const auto st = map.variables()) {
                    for (const CVariable& v : st->variables()) none = none && !st->float_format(v.module);
                    for (const CFunction& f : st->functions()) none = none && !st->float_format(f.module);
                    for (const CRecord& r : st->types()) none = none && !st->float_format(r.module);
                }
                if (none) ++clean;
                else bad += " " + d.path().string();
            }
        }
        check("CVFMT-22", "no map of z88dk 2.4, 2.4 dotn, master before 2026-11-06 or zsdcc (every committed root) has an __fp_mode constant or a float format on any module: "
                          "they keep the size / library rule",
              maps >= 100 && clean == maps, std::to_string(maps) + " maps, " + std::to_string(clean) + " clean:" + bad);
    }
    {   // CVFMT-23: the z88 integer form
        const FpCtx z4(5, 127, 4), z3(5, 127, 3);
        const std::string two = text_of(z4.ctx, "{6}SF:S", {0x00, 0x02, 0x00, 0x00, 0x00, 0x00});
        const std::string m2 = text_of(z4.ctx, "{6}SF:S", {0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0x00});
        const std::string zero = text_of(z4.ctx, "{6}SF:S", {0, 0, 0, 0, 0, 0});
        const std::string big = text_of(z4.ctx, "{6}SF:S", {0x00, 0x00, 0x00, 0x00, 0x80, 0x00});   // -2147483648
        const std::string refused = text_of(z3.ctx, "{6}SF:S", {0, 0, 0, 0, 0, 0});
        check("CVFMT-23", "z88 (BBC) format, exponent byte 0: the mantissa bytes are a 32-bit two's complement integer (2, -2, 0, -2147483648), byte 0 unused; with a 3-byte "
                          "mantissa the integer form is refused",
              two == "2" && m2 == "-2" && zero == "0" && big == "-2147483648" && refused == "!a z88 integer with a 3-byte mantissa is not supported",
              two + " | " + m2 + " | " + zero + " | " + big + " | " + refused);
    }
    {   // CVFMT-24: every unusable format is refused with its reason, never a number
        struct Case {
            int mode, bias, ms;
            const char* chain;
            const char* why;
            const char* text;
        };
        const Case cases[] = {
            {7, 0, 3, "{4}SF:S", "", "float format __fp_mode 7 is not supported"},
            {0, 128, 6, "{6}SF:S", "", "float format z80 (mantissa 6 bytes) does not fit a 6-byte float"},
            {5, 127, 5, "{6}SF:S", "", "float format z88 (mantissa 5 bytes) does not fit a 6-byte float after its first byte"},
            {6, 0, 4, "{4}SF:S", "", "float format am9511 needs a 3-byte mantissa (the map says 4)"},
            {1, 127, 3, "{4}SF:S", "", "float format ieee with exponent bias 127 and mantissa size 3 is not IEEE binary32"},
            {1, 126, 3, "{6}SF:S", "", "a 6-byte float is not ieee (4 bytes)"},
            {0, 128, 5, "{6}SF:S", "no __fp_exponent_bias", "float format of module t_c is not usable: no __fp_exponent_bias"},
            {0, 128, 5, "{6}SF:S", "two __fp_mode values (0 and 1)", "float format of module t_c is not usable: two __fp_mode values (0 and 1)"},
        };
        std::string bad;
        for (const Case& c : cases) {
            const FpCtx f(c.mode, c.bias, c.ms, c.why);
            const std::vector<uint8_t> b(8, 0x55);
            const CFormatted r = c_format_value(f.ctx, ty(c.chain), b.data(), 6);
            const std::optional<double> real = c_float_value(f.ctx, ty(c.chain), b.data(), 6);
            const std::string w = enc_hex(f.ctx, c.chain, "1.5", std::vector<uint8_t>(6, 0));
            if (r.ok || r.why != c.text || real || w != std::string("!") + c.text) bad += std::string(" [") + c.text + " -> " + r.why + " | " + w + "]";
        }
        check("CVFMT-24", "an unknown mode, a mantissa that does not fit the record, a z88 one that leaves no room after byte 0, an am9511 without a 3-byte mantissa, an IEEE float "
                          "with another bias or size, and a module whose symbols are incomplete or conflicting: the value, c_float_value and a write are all refused with the "
                          "same stated reason",
              bad.empty(), bad);
    }
    {   // CVFMT-25: the map's parameters are used, none is assumed
        const FpCtx a0(6, 0, 3), a1(6, 1, 3), zz(5, 127, 4), m128(2, 128, 3), m129(2, 129, 3), b4(0, 129, 4);
        const std::string am0 = text_of(a0.ctx, "{4}SF:S", {0x00, 0x00, 0xC0, 0x01});
        const std::string am1 = text_of(a1.ctx, "{4}SF:S", {0x00, 0x00, 0xC0, 0x01});
        const std::string z = text_of(zz.ctx, "{6}SF:S", {0xFF, 0x00, 0x00, 0x00, 0xD0, 0x81});
        const std::string q128 = text_of(m128.ctx, "{4}SF:S", {0x00, 0x00, 0xD0, 0x82});
        const std::string q129 = text_of(m129.ctx, "{4}SF:S", {0x00, 0x00, 0xD0, 0x82});
        const std::string wide = text_of(b4.ctx, "{6}SF:S", {0x00, 0x00, 0x00, 0xD0, 0x83, 0x00});
        check("CVFMT-25", "the map's bias, mantissa size and z88 offset decide: AM9511 00 00 C0 01 is 1.5 at bias 0 and 0.75 at bias 1; z88 with byte 0 = FF reads -3.25 (byte 0 ignored); "
                          "mbf32 00 00 D0 82 is -3.25 at bias 128 and -1.625 at 129; a 4-byte mantissa at bias 129 reads -3.25",
              am0 == k15 && am1 == "0.75" && z == "-3.25" && q128 == "-3.25" && q129 == "-1.625" && wide == "-3.25",
              am0 + " " + am1 + " " + z + " " + q128 + " " + q129 + " " + wide);
    }
    {   // CVFMT-26: precision follows the mantissa
        const FpCtx m32(2, 128, 3), m40(3, 128, 4), m64(4, 128, 7), d48(0, 128, 5);
        const std::string a = text_of(m32.ctx, "{4}SF:S", {0xCD, 0xCC, 0x4C, 0x7D});
        const std::string b = text_of(m40.ctx, "{6}SF:S", {0xCD, 0xCC, 0xCC, 0x4C, 0x7D, 0x00});
        const std::string c = text_of(m64.ctx, "{8}SF:S", {0xD0, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x4C, 0x7D});
        // the 48-bit text is the old c_format_float48's, byte for byte, for any bytes
        const uint8_t pats[][6] = {{0xCD, 0xCC, 0xCC, 0xCC, 0x4C, 0x7D}, {0xA2, 0x45, 0xB6, 0xE6, 0x40, 0x8E}, {0x00, 0x00, 0x00, 0x00, 0xD0, 0x82},
                                   {0x01, 0x02, 0x03, 0x04, 0x85, 0xFF}, {0x11, 0x22, 0x33, 0x44, 0x55, 0x01}, {0x11, 0x22, 0x33, 0x44, 0x55, 0x00}};
        bool same = true;
        for (const auto& p : pats) {
            const CFormatted r = c_format_value(d48.ctx, ty("{6}SF:S"), p, 6);
            same = same && r.ok && r.text == c_format_float48(p);
        }
        check("CVFMT-26", "precision follows the mantissa: mbf32's 0.1 prints 0.1 (not 12 digits), mbf40's and mbf64's too, and the (0,128,5) text equals c_format_float48() on six byte patterns",
              a == "0.1" && b == "0.1" && c == "0.1" && same, a + " " + b + " " + c);
    }
    {   // CVFMT-27: the size of a float, for a pointer's target
        const int want[] = {6, 4, 4, 6, 8, 6, 4};
        const int bias[] = {128, 126, 128, 128, 128, 127, 0};
        const int ms[] = {5, 3, 3, 4, 7, 4, 3};
        std::string bad;
        for (int mode = 0; mode <= 6; ++mode) {
            const FpCtx f(mode, bias[mode], ms[mode]);
            if (c_leaf_size(f.ctx, ty("{4}SF:S").chain) != want[mode]) bad += " mode " + std::to_string(mode);
        }
        const VariableStore none({}, {}, {}, {}, {});
        CFormatCtx nctx;
        nctx.store = &none;
        nctx.module = "t_c";
        const FpCtx unusable(0, 128, 5, "no __fp_mode"), mode7(7, 0, 3);
        // `float *pf` at $A000 -> {1.5, -2} (mbf32); with a format `*pf` and `pf[1]` read, without one they are refused as before
        const FpCtx m32(2, 128, 3);
        Mem mem;
        mem.put(0xA000, {0x00, 0xB0});
        mem.put(0xB000, {0x00, 0x00, 0x40, 0x81, 0x00, 0x00, 0x80, 0x82});
        CFormatCtx rctx = m32.ctx;
        rctx.read = mem.reader();
        CLoc root;
        root.type = ty("{2}DG,SF:S");
        root.addr = 0xA000;
        const CEval e0 = c_eval_path(rctx, root, true, {});
        const CEval e1 = c_eval_path(rctx, root, false, c_parse_path("pf[1]").steps);
        const CFormatted v0 = e0.ok ? c_read_loc(rctx, e0.loc) : CFormatted{};
        const CFormatted v1 = e1.ok ? c_read_loc(rctx, e1.loc) : CFormatted{};
        CFormatCtx octx = nctx;
        octx.read = mem.reader();
        const CEval n0 = c_eval_path(octx, root, true, {});
        check("CVFMT-27", "the width of a float is 6, 4, 4, 6, 8, 6, 4 for modes 0..6, -1 with no format, an unusable one or an unknown mode; a pointer to a float then reads: *pf is 1.5 and "
                          "pf[1] -2 (stride 4, mbf32), and without a format *pf is refused as before",
              bad.empty() && c_leaf_size(nctx, ty("{4}SF:S").chain) == -1 && c_leaf_size(unusable.ctx, ty("{4}SF:S").chain) == -1 && c_leaf_size(mode7.ctx, ty("{4}SF:S").chain) == -1 &&
                  e0.ok && v0.ok && v0.text == k15 && e1.ok && e1.loc.addr == 0xB004 && v1.ok && v1.text == "-2" && !n0.ok,
              bad + " " + v0.text + " " + v1.text);
    }
    {   // CVFMT-28: a struct laid out by another module's float format
        const auto rec = [](const char* text, const char* module) {
            CRecord r;
            r.text = text;
            r.module = module;
            r.record = cdb_decode(text).record;
            return r;
        };
        const std::vector<CRecord> types = {
            rec("T:Fa.h$s[({0}S:S$v$0_0$0({6}SF:S),Z,0,0)({6}S:S$n$0_0$0({2}SI:S),Z,0,0)]", "a_c"),
            rec("T:Fa.h$in[({0}S:S$f$0_0$0({6}SF:S),Z,0,0)]", "a_c"),
            rec("T:Fa.h$o[({0}S:S$i$0_0$0({6}STin:S),Z,0,0)({6}S:S$n$0_0$0({2}SI:S),Z,0,0)]", "a_c"),
        };
        const VariableStore two({}, {}, types, {}, {}, {}, {}, "", {{"a_c", CFloatFormat{0, 128, 5, ""}}, {"b_c", CFloatFormat{1, 126, 3, ""}}});
        const VariableStore bare({}, {}, types, {}, {}, {}, {}, "", {{"a_c", CFloatFormat{0, 128, 5, ""}}});
        const auto ctx_in = [](const VariableStore& st, const char* module) {
            CFormatCtx c;
            c.store = &st;
            c.scope_name = "x.c";
            c.module = module;
            return c;
        };
        const std::vector<uint8_t> obj = {0x00, 0x00, 0x00, 0x00, 0x40, 0x81, 0x07, 0x00};
        const std::string why = "struct s is laid out with 6-byte floats (module a_c); this object's module b_c uses 4-byte floats";
        const CFormatCtx ob = ctx_in(two, "b_c"), oa = ctx_in(two, "a_c"), nb = ctx_in(bare, "b_c");
        const CdbType st = ty("{8}STs:S");
        const CFormatted pb = c_format_value(ob, st, obj.data(), obj.size());
        const CFormatted pa = c_format_value(oa, st, obj.data(), obj.size());
        const CFormatted pn = c_format_value(nb, st, obj.data(), obj.size());
        CLoc root;
        root.type = st;
        root.addr = 0xA000;
        const CEval mv = c_eval_path(ob, root, false, c_parse_path("x.v").steps);
        const CEval mn = c_eval_path(ob, root, false, c_parse_path("x.n").steps);
        const CChildren ch = c_children(ob, root, 0, 10);
        CFormatCtx obs = ob;
        const bool size_unknown = c_leaf_size(obs, st.chain) == -1;
        const bool size_known = c_leaf_size(oa, st.chain) == 8;
        // a float only inside a nested struct member
        const CdbType nested = ty("{8}STo:S");
        const CFormatted nn = c_format_value(ob, nested, obj.data(), obj.size());
        const CFormatted na = c_format_value(oa, nested, obj.data(), obj.size());
        check("CVFMT-28", "a struct found in another module whose floats are 6 bytes, read by an object of a 4-byte-float module: the value, its members, its children and its size "
                          "are refused with the layout text (also when the float sits in a nested struct); in its own module it reads {v = 1.5, n = 7}; with no format on the "
                          "holder's module nothing changes",
              !pb.ok && pb.why == why && !mv.ok && mv.why == why && !mn.ok && mn.why == why && !ch.ok && ch.why == why && size_unknown && size_known &&
                  pa.ok && pa.text == "{v = " + k15 + ", n = 7}" && pn.ok && pn.text == pa.text && !nn.ok && nn.why.find("is laid out with 6-byte floats") != std::string::npos &&
                  na.ok,
              pb.why + " | " + pa.why + " | " + pn.why + " | " + nn.why + " | " + na.why);
    }
    {   // CVFMT-29: writes, in each format, to the bytes the compilers wrote
        struct Build {
            int mode, bias, ms;
            const char* chain;
            const char* b[5];   // -3.25, 1.5, -2, 100, 2.5
        };
        const Build builds[] = {
            {0, 128, 5, "{6}SF:S", {"00 00 00 00 D0 82", "00 00 00 00 40 81", "00 00 00 00 80 82", "00 00 00 00 48 87", "00 00 00 00 20 82"}},
            {1, 126, 3, "{4}SF:S", {"00 00 50 C0", "00 00 C0 3F", "00 00 00 C0", "00 00 C8 42", "00 00 20 40"}},
            {2, 128, 3, "{4}SF:S", {"00 00 D0 82", "00 00 40 81", "00 00 80 82", "00 00 48 87", "00 00 20 82"}},
            {3, 128, 4, "{6}SF:S", {"00 00 00 D0 82 00", "00 00 00 40 81 00", "00 00 00 80 82 00", "00 00 00 48 87 00", "00 00 00 20 82 00"}},
            {4, 128, 7, "{8}SF:S", {"00 00 00 00 00 00 D0 82", "00 00 00 00 00 00 40 81", "00 00 00 00 00 00 80 82", "00 00 00 00 00 00 48 87", "00 00 00 00 00 00 20 82"}},
            {5, 127, 4, "{6}SF:S", {"00 00 00 00 D0 81", "00 00 00 00 40 80", "00 00 00 00 80 81", "00 00 00 00 48 86", "00 00 00 00 20 81"}},
            {6, 0, 3, "{4}SF:S", {"00 00 D0 82", "00 00 C0 01", "00 00 80 82", "00 00 C8 07", "00 00 A0 02"}},
            {0, 129, 4, "{6}SF:S", {"00 00 00 D0 83 00", "00 00 00 40 82 00", "00 00 00 80 83 00", "00 00 00 48 88 00", "00 00 00 20 83 00"}},
        };
        const char* texts[] = {"-3.25", "1.5", "-2", "100", "2.5"};
        std::string bad;
        for (const Build& bd : builds) {
            const FpCtx f(bd.mode, bd.bias, bd.ms);
            const int n = c_leaf_size(f.ctx, ty("{4}SF:S").chain);
            for (int k = 0; k < 5; ++k) {
                const std::string got = enc_hex(f.ctx, bd.chain, texts[k], std::vector<uint8_t>(static_cast<size_t>(n), 0));
                if (got != bd.b[k]) bad += " mode " + std::to_string(bd.mode) + "/" + std::to_string(bd.bias) + " " + texts[k] + " -> " + got;
            }
        }
        // 0.1 reads back as 0.1 through every mode; outside bytes are kept; zero is all-zero layout bytes; out of range is refused per format
        std::string rt;
        for (const Build& bd : builds) {
            const FpCtx f(bd.mode, bd.bias, bd.ms);
            CNewValue v;
            v.text = "0.1";
            const CEncoded e = c_encode_value(f.ctx, ty(bd.chain), v, nullptr, 0);
            const CFormatted r = e.ok ? c_format_value(f.ctx, ty(bd.chain), e.bytes.data(), e.bytes.size()) : CFormatted{};
            if (!e.ok || !r.ok || r.text != "0.1") rt += " mode " + std::to_string(bd.mode) + "/" + std::to_string(bd.bias) + ":" + r.text + e.why;
        }
        const FpCtx m40(3, 128, 4), z88(5, 127, 4), b4(0, 129, 4), m32(2, 128, 3), am(6, 0, 3), d48(0, 128, 5);
        const std::string keep40 = enc_hex(m40.ctx, "{6}SF:S", "1.5", {1, 2, 3, 4, 5, 0xAA});
        const std::string keepz = enc_hex(z88.ctx, "{6}SF:S", "1.5", {0xEE, 1, 2, 3, 4, 5});
        const std::string keep4 = enc_hex(b4.ctx, "{6}SF:S", "1.5", {1, 2, 3, 4, 5, 0xBB});
        const std::string zero = enc_hex(m40.ctx, "{6}SF:S", "0", {1, 2, 3, 4, 5, 0xAA});
        const std::string zeroz = enc_hex(z88.ctx, "{6}SF:S", "0", {0xEE, 1, 2, 3, 4, 5});
        const std::string big32 = enc_hex(m32.ctx, "{4}SF:S", "1e39", {0, 0, 0, 0});
        const std::string tiny32 = enc_hex(m32.ctx, "{4}SF:S", "1e-39", {0, 0, 0, 0});
        const std::string bigam = enc_hex(am.ctx, "{4}SF:S", "1e20", {0, 0, 0, 0});
        const std::string tinyam = enc_hex(am.ctx, "{4}SF:S", "1e-20", {0, 0, 0, 0});
        const std::string big48 = enc_hex(d48.ctx, "{6}SF:S", "1e39", std::vector<uint8_t>(6, 0));
        const std::string nan = enc_hex(m32.ctx, "{4}SF:S", "nan", {0, 0, 0, 0});
        const std::string junk = enc_hex(m32.ctx, "{4}SF:S", "abc", {0, 0, 0, 0});
        check("CVFMT-29", "writes: -3.25, 1.5, -2, 100 and 2.5 encode to exactly the bytes the compilers wrote, in every mode (and bias 129 / 4-byte mantissa); 0.1 reads back as 0.1 in "
                          "every mode; the bytes outside the layout (mbf40's 6th, z88's 1st, a 4-byte mantissa's 6th) are kept; zero is all-zero layout bytes; a value beyond the "
                          "format's exponent is refused with the format's name, never wrapped; nan and text are refused",
              bad.empty() && rt.empty() && keep40 == "00 00 00 40 81 AA" && keepz == "EE 00 00 00 40 80" && keep4 == "00 00 00 40 82 BB" && zero == "00 00 00 00 00 AA" &&
                  zeroz == "EE 00 00 00 00 00" && big32 == "!'1e39' is out of range for float (mbf32)" && tiny32 == "!'1e-39' is too small for float (mbf32): it would underflow" &&
                  bigam == "!'1e20' is out of range for float (am9511)" && tinyam == "!'1e-20' is too small for float (am9511): it would underflow" &&
                  big48 == "!'1e39' is out of range for float (z88dk 48-bit)" && nan == "!'nan' is not a finite number" && junk == "!'abc' is not a number",
              bad + rt + " | " + keep40 + " " + keepz + " " + keep4 + " " + zero + " " + zeroz + " | " + big32 + " " + tiny32 + " " + bigam + " " + tinyam + " " + big48 + " " + nan + " " + junk);
    }
    {   // CVFMT-30: the typed value follows the format; the format-free overload is untouched
        const FpCtx a(0, 128, 5), b(1, 126, 3), c(2, 128, 3), d(3, 128, 4), e(4, 128, 7), f(5, 127, 4), g(6, 0, 3);
        const auto val = [](const FpCtx& x, const char* chain, std::initializer_list<uint8_t> bytes) {
            const std::vector<uint8_t> v(bytes);
            const auto r = c_float_value(x.ctx, ty(chain), v.data(), v.size());
            return r ? *r : -9999.0;
        };
        const bool per_mode = val(a, "{6}SF:S", {0, 0, 0, 0, 0xD0, 0x82}) == -3.25 && val(b, "{4}SF:S", {0x00, 0x00, 0x50, 0xC0}) == -3.25 &&
                              val(c, "{4}SF:S", {0, 0, 0xD0, 0x82}) == -3.25 && val(d, "{6}SF:S", {0, 0, 0, 0xD0, 0x82, 0}) == -3.25 &&
                              val(e, "{8}SF:S", {0, 0, 0, 0, 0, 0, 0xD0, 0x82}) == -3.25 && val(f, "{6}SF:S", {0, 0, 0, 0, 0xD0, 0x81}) == -3.25 &&
                              val(g, "{4}SF:S", {0, 0, 0xD0, 0x82}) == -3.25 && val(g, "{4}SF:S", {0, 0, 0xC8, 0x07}) == 100.0 &&
                              val(a, "{6}SF:S", {0, 0, 0, 0, 0, 0}) == 0.0 && val(f, "{6}SF:S", {0, 2, 0, 0, 0, 0}) == 2.0;
        // a short buffer and the unusable format give nothing; an int is not a float
        const std::vector<uint8_t> five(5, 1);
        const bool none = !c_float_value(a.ctx, ty("{6}SF:S"), five.data(), 5) && !c_float_value(FpCtx(7, 0, 3).ctx, ty("{4}SF:S"), five.data(), 5) &&
                          !c_float_value(a.ctx, ty("{2}SI:S"), five.data(), 5);
        // the 3-argument overload is the size rule, whatever the module records
        const std::vector<uint8_t> f48 = {0, 0, 0, 0, 0xD0, 0x82}, f32 = {0x00, 0x00, 0xC0, 0x3F};
        const auto o48 = c_float_value(ty("{6}SF:S"), f48.data(), 6);
        const auto o32 = c_float_value(ty("{4}SF:S"), f32.data(), 4);
        check("CVFMT-30", "c_float_value(ctx, ...) is the decoded value in every mode (-3.25 in all seven, 100 in am9511, the z88 integer 2, zero), nothing for a short buffer, an unusable "
                          "format and an int; the three-argument overload still reads by size (48-bit -3.25, binary32 1.5)",
              per_mode && none && o48 && *o48 == -3.25 && o32 && *o32 == 1.5);
    }

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
