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

#include <clocale>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <locale>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "../row_id.h"

namespace fs = std::filesystem;
using namespace jnext::dbg;

static int g_total = 0;
static int g_pass = 0;
static int g_fail = 0;

static void check(const char* id, const char* desc, bool cond) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc);
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
        const CFormatted c = c_format_value(plain, ty("{40}DA40x,SC:U"), big.data(), big.size());   // chars: a string, cut at the NUL
        std::vector<uint8_t> ints(40);
        for (size_t i = 0; i < 20; ++i) ints[2 * i] = static_cast<uint8_t>(i);
        const CFormatted d = c_format_value(plain, ty("{40}DA20x,SI:S"), ints.data(), ints.size());
        check("CVALF-16", "at most 16 elements are shown, then `...`; a char array reads up to its NUL (`\"hi\"`), a 4-byte array with no NUL shows all four, and an array of "
                          "non-text bytes ({10, 20, 30, 40}) is a list of numbers",
              d.ok && d.text == "{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, ...}" && c.ok && c.text == "\"\"" &&
                  text_of(plain, "{3}DA3x,SC:S", {'h', 'i', 0}) == "\"hi\"" && text_of(plain, "{4}DA4x,SC:S", {'a', 'b', 'c', 'd'}) == "\"abcd\"" &&
                  text_of(plain, "{4}DA4x,SC:U", {10, 20, 30, 40}) == "{10, 20, 30, 40}");
    }

    // ── CVALF-17: struct nesting ───────────────────────────────────────────────
    {
        const CVariable* rect = global_var(*sc.store, "g_rect");
        std::vector<uint8_t> b = {1, 0, 2, 0, 3, 4, 0, 5, 0, 6, 'h', 'i', 0, 0};
        const CFormatted f = rect ? c_format_value(csc, rect->record.var.type, b.data(), b.size()) : CFormatted{};
        const CVariable* rz = global_var(*sd.store, "g_rect");
        const CFormatted g = rz ? c_format_value(csd, rz->record.var.type, b.data(), b.size()) : CFormatted{};
        const std::string want = "{a = {x = 1, y = 2, tag = 3}, b = {x = 4, y = 5, tag = 6}, name = \"hi\"}";
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

    // ── CVALF-30: the sccz80 fastcall marker ───────────────────────────────────
    {
        const CdbDecoded nominal = cdb_decode("S:Lfc.ffast$x$1_0$1({2}SI:U),B,1,4");
        const CdbDecoded correct = cdb_decode("S:Lfc.ffast$x$1_0$1({2}SI:U),B,1,-2");
        const CdbDecoded zero = cdb_decode("S:Lfc.ffast$x$1_0$1({2}SI:U),B,1,0");
        const CdbDecoded reg = cdb_decode("S:Lfc.ffast$x$1_0$1({2}SI:U),R,0,0,[l,h]");
        const std::string u1 = c_param_location_unknown(CConv::ListedFastcall, nominal.record.var);
        check("CVALF-30", "OD-14: a listed fastcall parameter recorded at a stack offset >= 0 is `location unknown (z88dk U1...)`; at a negative offset (80cc's correct "
                          "record) or with no listing evidence (a plain or unlisted function) it is read as written; a register record is never marked",
              u1.find("location unknown") != std::string::npos && u1.find("U1") != std::string::npos &&
                  !c_param_location_unknown(CConv::ListedFastcall, zero.record.var).empty() &&
                  c_param_location_unknown(CConv::ListedFastcall, correct.record.var).empty() &&
                  c_param_location_unknown(CConv::Listed, nominal.record.var).empty() &&
                  c_param_location_unknown(CConv::Unlisted, nominal.record.var).empty() &&
                  c_param_location_unknown(CConv::ListedFastcall, reg.record.var).empty());
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

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
