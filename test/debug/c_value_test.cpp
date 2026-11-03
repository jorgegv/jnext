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
        check("CVALF-16", "at most 16 elements are shown, then `...`; a char array reads up to its NUL (`\"hi\"`), and a 4-byte array with no NUL shows all four",
              d.ok && d.text == "{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, ...}" && c.ok && c.text == "\"\"" &&
                  text_of(plain, "{3}DA3x,SC:S", {'h', 'i', 0}) == "\"hi\"" && text_of(plain, "{4}DA4x,SC:S", {'a', 'b', 'c', 'd'}) == "\"abcd\"");
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
                  f.notes[0] == "bitfield layout unknown (z88dk U2)" && u.ok && u.text == "{w = 513, b = \"\\x01\\x02\"}" && u.notes.empty());
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

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
