// The z88dk `-debug` map adapter (GH #344, E23-13; design 9.1, 9.4, 9.7):
//
//   Z88MAP-*   the section-to-page rules, the line records into SourceMap
//              ranges (extents, zero-width lines, strict refusal), the program
//              identity from the section binaries, and the symbol loaders'
//              file-type residual
//   Z88DOT-*   z88dk dot commands (GH #351): rule 5 - the CODE group of a dot / dotx is
//              DivMMC RAM bank 2, a dotn is refused, detection is by the map
//
// Oracle: the committed fixtures test/fixtures/z88dk (what the two z88dk installs
// produced from our own sources, with the NEX placement and the .lis listings next
// to each map) and, for the rows that need a map no install produced, a map in the
// real line format whose expected answer is the cited rule of the design. No VHDL
// counterpart. Pure: no machine; the NEX files are read by the test itself.
// Run: ./build/test/z88dk_map_test

#include "debug/cdb_record.h"
#include "debug/disasm_text.h"
#include "debug/source_file.h"
#include "debug/source_map.h"
#include "debug/symbol_table.h"
#include "debug/variable_store.h"
#include "debug/z88dk_source_map.h"
#include "debug/z88dk_space.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>
#include "../row_id.h"
#include "../hex_value.h"

namespace fs = std::filesystem;
using jnext::dbg::FetchSource;
using jnext::dbg::MemSpace;

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

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

static void write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

static const fs::path kFixtures = JNEXT_Z88DK_FIXTURE_DIR;
static const fs::path kNoDebugMap = fs::path(JNEXT_ZDEBUGMAP_DIR) / "nodebug.map";

// What the NEX loader leaves mapped (nex_loader.cpp: MMU2-5 = 10, 11, 4, 5, then
// 0 and 1; slots 0 and 1 are the ROM).
static FetchSource nex_space(uint16_t address) {
    static const int kPage[8] = {-1, -1, 10, 11, 4, 5, 0, 1};
    const int slot = address >> 13;
    return slot < 2 ? MemSpace::rom(0) : MemSpace::page(static_cast<uint16_t>(kPage[slot]));
}

static const MemSpace P4 = MemSpace::page(4);
static const MemSpace P20 = MemSpace::page(20);
static const MemSpace P21 = MemSpace::page(21);

// The 8K pages a NEX file loads: banks in the file's order (5, 2, 0, 1, 3, 4, 6...),
// 16K each from byte 512, bank b = pages 2b and 2b + 1. Presence flags at 18 + b.
struct NexMemory {
    std::map<int, std::vector<uint8_t>> pages;
    bool ok = false;

    explicit NexMemory(const fs::path& file) {
        std::ifstream in(file, std::ios::binary);
        const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                        std::istreambuf_iterator<char>());
        if (data.size() < 512 || std::string(data.begin(), data.begin() + 4) != "Next") return;
        static const int kOrder[] = {5, 2, 0, 1, 3, 4};
        std::vector<int> order(std::begin(kOrder), std::end(kOrder));
        for (int b = 6; b < 112; ++b) order.push_back(b);
        size_t at = 512;
        for (const int bank : order) {
            if (data[18 + bank] == 0) continue;
            if (at + 0x4000 > data.size()) return;
            for (int half = 0; half < 2; ++half)
                pages[2 * bank + half].assign(data.begin() + at + half * 0x2000,
                                              data.begin() + at + (half + 1) * 0x2000);
            at += 0x4000;
        }
        ok = true;
    }

    SourceMap::SpaceReader reader() const {
        return [this](FetchSource s, uint16_t a) -> std::optional<uint8_t> {
            if (s.kind != MemSpace::Kind::Page) return std::nullopt;
            const auto it = pages.find(s.index);
            if (it == pages.end()) return std::nullopt;
            return it->second[a & 0x1FFF];
        };
    }
};

// The SHA-256 reader a z88dk map never uses: any byte but the program's.
static uint8_t logical_junk(uint16_t) { return 0xA5; }

static std::string rec(const char* name, uint64_t value, const char* module, const char* section,
                       const char* location, const char* type = "addr") {
    char buf[600];
    std::snprintf(buf, sizeof buf, "%s = $%llX ; %s, local, , %s, %s, %s\n", name,
                  static_cast<unsigned long long>(value), type, module, section, location);
    return buf;
}

static bool is_at(const SourceLocation* l, const char* file, int line) {
    return l && l->file == file && l->line == line;
}

static bool starts_are(const SourceResolution& r, std::vector<SourceAddress> want) {
    if (r.starts.size() != want.size()) return false;
    for (size_t i = 0; i < want.size(); ++i)
        if (!(r.starts[i].space == want[i].space) || r.starts[i].address != want[i].address) return false;
    return true;
}

// ── GH #433: the C variable store ───────────────────────────────────────────
using jnext::dbg::CdbHidden;
using jnext::dbg::CdbScope;
using jnext::dbg::CFunction;
using jnext::dbg::CStorage;
using jnext::dbg::CVariable;
using jnext::dbg::VariableStore;

static std::vector<std::string> map_lines(const fs::path& p) {
    std::ifstream in(p);
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    return lines;
}

static std::string join_lines(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    return out;
}

static bool is_cdb_line(const std::string& l) { return l.compare(0, 11, "__CDBINFO__") == 0; }

static size_t cdb_line_count(const fs::path& p) {
    size_t n = 0;
    for (const auto& l : map_lines(p)) n += is_cdb_line(l);
    return n;
}

// z80asm's name encoding of a record (cdb_record.h): a letter stands for itself, every other byte is `_hh`.
static std::string cdb_escape(const std::string& text) {
    std::string out;
    for (const unsigned char c : text) {
        if (std::isalpha(c)) {
            out += static_cast<char>(c);
        } else {
            char buf[8];
            std::snprintf(buf, sizeof buf, "_%02x", c);
            out += buf;
        }
    }
    return out;
}

static const VariableStore* store_of(const SourceMap& m) { return m.variables().get(); }

// The only function of that name (and module); null when none or several.
static const CFunction* fn_named(const VariableStore& s, const char* name, const char* module = nullptr) {
    const CFunction* found = nullptr;
    for (const CFunction& f : s.functions()) {
        if (f.name != name || (module && f.module != module)) continue;
        if (found) return nullptr;
        found = &f;
    }
    return found;
}

// The only variable record of that name in that CDB module (and function); null when none or several.
static const CVariable* var_named(const VariableStore& s, const char* name, const char* module,
                                  const char* function = nullptr) {
    const CVariable* found = nullptr;
    for (const CVariable& v : s.variables()) {
        if (v.record.var.name != name || v.module != module) continue;
        if (function && (v.function < 0 || s.functions()[static_cast<size_t>(v.function)].name != function))
            continue;
        if (found) return nullptr;
        found = &v;
    }
    return found;
}

static bool stored_at(const CStorage& st, MemSpace space, uint16_t address) {
    return st.resolved && st.at.space == space && st.at.address == address;
}

static std::string storage_text(const CStorage& st) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d/%u:%04X", int(st.at.space.kind), unsigned(st.at.space.index),
                  unsigned(st.at.address));
    return std::string(st.resolved ? "R " : "U ") + buf + " " + st.symbol + " [" + st.why + "]";
}

// Every field of the store, in the store's own order: two stores are the same exactly when the texts are.
static std::string store_dump(const VariableStore& s) {
    std::string out;
    for (const CFunction& f : s.functions()) {
        out += "F " + f.text + " | " + f.name + " | " + f.module + " | " + storage_text(f.entry) + "\n";
        for (const auto& c : f.scopes) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "  S %d/%u:%04X line=%d known=%d %d/%d/%d\n", int(c.at.space.kind),
                          unsigned(c.at.space.index), unsigned(c.at.address), c.line, int(c.known), c.level,
                          c.sub, c.block);
            out += buf;
        }
    }
    for (const CVariable& v : s.variables())
        out += "V " + v.text + " | " + v.module + " | " + v.c_name + " | "
            + (v.function < 0 ? std::string("-") : s.functions()[static_cast<size_t>(v.function)].text) + " | "
            + storage_text(v.storage) + "\n";
    for (const auto& t : s.types()) out += "T " + t.text + " | " + t.module + "\n";
    for (const auto& m : s.modules()) out += "M " + m.text + " | " + m.module + "\n";
    for (const auto& r : s.refused()) out += "X " + r + "\n";
    return out;
}

struct Sc {
    uint16_t address;
    int level, sub, block;
};

// The function's scope table is exactly `want`, every entry known, all in `space`.
static bool scopes_are(const CFunction* f, const std::vector<Sc>& want, MemSpace space) {
    if (!f || f->scopes.size() != want.size()) return false;
    for (size_t i = 0; i < want.size(); ++i) {
        const auto& c = f->scopes[i];
        if (!c.known || !(c.at.space == space) || c.at.address != want[i].address ||
            c.level != want[i].level || c.sub != want[i].sub || c.block != want[i].block)
            return false;
    }
    return true;
}

static size_t locals_of(const VariableStore& s, const CFunction* f) {
    size_t n = 0;
    for (const CVariable& v : s.variables())
        if (f && v.function >= 0 && &s.functions()[static_cast<size_t>(v.function)] == f) ++n;
    return n;
}

int main() {
    std::printf("z88dk -debug map adapter\n");

    const fs::path dir = fs::temp_directory_path() / ("jnext-z88dk-map-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    const fs::path v24 = kFixtures / "v2.4";
    const fs::path master = kFixtures / "master";
    const std::function<std::optional<uint16_t>(int)> no_head = [](int) { return std::nullopt; };

    auto load = [&](SourceMap& m, const fs::path& map, const SymbolSpaceOf& space = nex_space) {
        return load_z88dk_source_map(m, map.string(), space);
    };
    auto synth = [&](const char* name, const std::string& body) {
        const fs::path p = dir / name;
        write_text(p, body);
        return p;
    };

    // ── Z88MAP-01..07: v2.4 sccz80, the program every other row compares with ──
    SourceMap sc;
    const auto sc_r = load(sc, v24 / "sccz80-debug" / "prog.map");
    check("Z88MAP-01", "main.c:19 starts at $8CB5 in page 4 (__C_LINE_19 = $8CB5, prog_CODE.bin at bank 2)",
          sc_r && starts_are(sc.resolve("main.c:19"), {{P4, 0x8CB5}}) && sc.resolve("main.c:19").error.empty() &&
              sc.format() == "z88dk" && SourceMap().format() == "SLD");
    check("Z88MAP-02", "a range is looked up in its own page: $8CB8 is main.c:19 in page 4, nothing in page 5",
          is_at(sc.lookup(P4, 0x8CB8), "main.c", 19) && !sc.lookup(MemSpace::page(5), 0x8CB8));
    {
        SourceMap m;
        // a.c:3 runs to the next start, not past the foreign module's symbol (the same module's
        // `i_1` at $9008 bounds nothing); no section tail in the map.
        const auto r = load(m, synth("z03.map",
            rec("__C_LINE_3_a_2ec", 0x9000, "a_c", "code_compiler", "a.c::f::0::0:3") +
            rec("i_1", 0x9008, "a_c", "code_compiler", "a.c::f::0::0:3") +
            rec("_lib_fn", 0x9010, "lib_mod", "code_compiler", "lib.asm:1")));
        check("Z88MAP-03", "a line's extent stops at the next addr symbol of another module, not at one of its own module",
              r && is_at(m.lookup(P4, 0x900F), "a.c", 3) && is_at(m.lookup(P4, 0x9008), "a.c", 3) &&
                  !m.lookup(P4, 0x9010));
    }
    check("Z88MAP-04", "asmfn.asm:5 owns $8D65 and util.c:8 ends where it starts ($8D63 is util.c:8)",
          is_at(sc.lookup(P4, 0x8D65), "asmfn.asm", 5) && is_at(sc.lookup(P4, 0x8D63), "util.c", 8) &&
              is_at(sc.lookup(P4, 0x8D64), "util.c", 8));
    check("Z88MAP-05", "lines 16 and 17 share $8C95 in main::1::3: 17 owns it, 16 resolves to it, one location there",
          is_at(sc.lookup(P4, 0x8C95), "main.c", 17) && starts_are(sc.resolve("main.c:16"), {{P4, 0x8C95}}) &&
              sc.lookup_all(P4, 0x8C95).size() == 1);
    check("Z88MAP-06", "line 14 (square's scope) shares $8C92 with line 15 (main): it has no code, 15 owns the address",
          sc.resolve("main.c:14").starts.empty() && sc.resolve("main.c:14").error == "no code at main.c:14" &&
              is_at(sc.lookup(P4, 0x8C92), "main.c", 15));
    check("Z88MAP-07", "the 50 line records in a section give 46 ranges; lines in an empty section ($0000) are dropped",
          sc.size() == 46 && sc.resolve("main.c:1").error == "no code at main.c:1" &&
              !sc.lookup(MemSpace::page(0), 0) && !sc.lookup(MemSpace::rom(0), 0) &&
              !sc.lookup(MemSpace::page(10), 0));

    // ── Z88MAP-08..11: where the page comes from ──
    {
        SourceMap m;
        SourceMap mb;
        const auto r = load(m, v24 / "sccz80-page20" / "prog.map");
        const auto rb = load(mb, master / "sdcc-banked" / "prog.map");
        check("Z88MAP-08", "PAGE_20 lines are in page 20 whatever is mapped at $C000 (sccz80 on 2.4, sdcc on master)",
              r && starts_are(m.resolve("banked.c:7"), {{P20, 0xC003}}) &&
                  is_at(m.lookup(P20, 0xC003), "banked.c", 8) && !m.lookup(P21, 0xC003) &&
                  rb && starts_are(mb.resolve("banked.c:8"), {{P20, 0xC008}}));
    }
    {
        SourceMap m;
        SourceMap synthetic;
        const auto r = load(m, v24 / "sccz80-bank10" / "prog.map");
        const auto rs = load(synthetic, synth("z09.map",
            rec("__C_LINE_5_x_2ec", 0xE010, "x_c", "BANK_10", "x.c:5")));
        check("Z88MAP-09", "BANK_10 lines are in page 20 or 21 by bit 13 of the address: $C01B page 20, $E010 page 21",
              r && starts_are(m.resolve("banked.c:9"), {{P20, 0xC01B}}) &&
                  rs && starts_are(synthetic.resolve("x.c:5"), {{P21, 0xE010}}));
    }
    {
        const auto a = z88dk_section_space("BANK_10", 0xAC000, no_head);
        const auto b = z88dk_section_space("BANK_10", 0x14C000, no_head);
        const auto b2 = z88dk_section_space("BANK_10", 0x14E010, no_head);
        const auto c = z88dk_section_space("code_compiler", 0x1C000, no_head);
        SourceMap m;
        const auto d = load(m, synth("z10.map", rec("__C_LINE_3_a", 0x1C000, "a_c", "code_compiler", "a.c:3")));
        check("Z88MAP-10", "a classic 24-bit BANK_10 address is read by its low 16 bits (2.4 $AC000, master $14C000); an unnumbered section with a 24-bit value is refused",
              a.kind == Z88dkSpace::Kind::Paged && a.space == P20 && a.addr16 == 0xC000 &&
                  b.kind == Z88dkSpace::Kind::Paged && b.space == P20 && b.addr16 == 0xC000 &&
                  b2.kind == Z88dkSpace::Kind::Paged && b2.space == P21 &&
                  c.kind == Z88dkSpace::Kind::Refused &&
                  !d && d.error.find("line 1") != std::string::npos && m.empty());
    }
    {
        SourceMap m;
        SourceMap moved;
        SourceMap rom;
        const auto r = load(m, v24 / "sccz80-debug" / "prog.map");
        const SymbolSpaceOf slot4_is_40 = [](uint16_t a) {
            return (a >> 13) == 4 ? MemSpace::page(40) : nex_space(a);
        };
        const auto rm = load(moved, v24 / "sccz80-debug" / "prog.map", slot4_is_40);
        const auto rr = load(rom, synth("z11.map", rec("__C_LINE_3_a", 0x2000, "a_c", "code_compiler", "a.c:3")));
        check("Z88MAP-11", "a section that names no page takes the page the MMU maps: $8CB5 is page 4, or page 40 when slot 4 holds it; $2000 (ROM) refuses the half",
              r && starts_are(m.resolve("main.c:19"), {{P4, 0x8CB5}}) &&
                  rm && starts_are(moved.resolve("main.c:19"), {{MemSpace::page(40), 0x8CB5}}) &&
                  !rr && rr.error.find("line 1") != std::string::npos &&
                  rr.error.find("$2000") != std::string::npos && rom.empty());
    }

    // ── Z88MAP-12..13: the three location forms ──
    {
        SourceMap m;
        const auto r = load(m, master / "sdcc-debug" / "prog.map");
        const SourceLocation* l = m.lookup(P4, 0x8C5F);
        const auto t = parse_z88dk_map_line(
            "__C_LINE_11_x = $8C5F ; addr, local, , main_c, code_compiler, main.c::x::10000::11\t:11\r");
        const auto big = parse_z88dk_map_line(
            "__clib_malloc_heap_size = $FFFFFFFFFFFFFFFF ; const, local, , zxn_crt_asm_m4, , a,b.m4:482");
        const auto none = parse_z88dk_map_line("; just a comment = nothing");
        check("Z88MAP-12", "an sdcc location (file::x::level::block TAB :line) gives file and line with no TAB in the file; the tokenizer keeps the whole location (TAB, commas) and 64-bit values",
              r && starts_are(m.resolve("main.c:11"), {{P4, 0x8C5F}}) && l && l->file == "main.c" && l->line == 11 &&
                  t && t->is_addr && !t->is_const && t->value == 0x8C5F && t->module == "main_c" &&
                  t->section == "code_compiler" && t->location == "main.c::x::10000::11\t:11" &&
                  big && big->is_const && big->value == 0xFFFFFFFFFFFFFFFFull && big->location == "a,b.m4:482" &&
                  big->section.empty() && !none);
    }
    {
        SourceMap m;
        const fs::path map = v24 / "sccz80-subdir" / "prog.map";
        const auto r = load(m, map);
        const SourceLocation* l = m.lookup(P4, 0x8D5C);
        const std::string tail = "sccz80-subdir/prog.map";
        const std::string loaded = fs::path(m.loaded_file()).generic_string();   // '/' on Windows too
        check("Z88MAP-13", "a file in a subdirectory keeps its path (game/util.c), resolves by its tail; the stored map path is absolute",
              r && starts_are(m.resolve("util.c:8"), {{P4, 0x8D5C}}) && l && l->file == "game/util.c" &&
                  fs::path(m.loaded_file()).is_absolute() && loaded.size() > tail.size() &&
                  loaded.compare(loaded.size() - tail.size(), tail.size(), tail) == 0);
    }

    // ── Z88MAP-14: identity from the section binaries ──
    {
        const NexMemory nex(v24 / "sccz80-debug" / "prog.nex");
        SourceMap m;
        const auto r = load(m, v24 / "sccz80-debug" / "prog.map");
        const auto verdict = [&](NexMemory& mem) { return m.verify_program(logical_junk, mem.reader()); };
        NexMemory a = nex, b = nex, c = nex;
        a.pages[4][0x0CB5] ^= 0xFF;   // main.c:19, code
        b.pages[4][0x0E0E] ^= 0xFF;   // clib_variables.inc:35, data_alloc_malloc: data is not compared
        c.pages[4][0x0F62] ^= 0xFF;   // zxn_crt.asm.m4:9577, BSS_UNINITIALIZED
        NexMemory original = nex;
        const fs::path alone = dir / "alone" / "prog.map";
        fs::create_directories(alone.parent_path());
        fs::copy_file(v24 / "sccz80-debug" / "prog.map", alone);
        SourceMap lonely;
        const auto rl = load(lonely, alone);

        const NexMemory nex20(v24 / "sccz80-page20" / "prog.nex");
        SourceMap m20;
        const auto r20 = load(m20, v24 / "sccz80-page20" / "prog.map");
        NexMemory ok20 = nex20, bad20 = nex20;
        bad20.pages[20][3] ^= 0xFF;   // banked.c:8 in page 20; the logical reader never sees page 20
        NexMemory last20 = nex20, no20 = nex20;
        no20.pages.erase(20);
        last20.pages[20][0x23] ^= 0xFF;   // the last byte of prog_PAGE_20.bin, in banked.c:9
        // A lower-case head (__code_compiler_head) names a section, not a group: a file
        // that happens to be called prog_code_compiler.bin is not its binary (no other
        // binary here, so any check made from it would make the map verifiable).
        const fs::path decoy = dir / "decoy" / "prog.map";
        fs::create_directories(decoy.parent_path());
        fs::copy_file(v24 / "sccz80-debug" / "prog.map", decoy);
        write_text(decoy.parent_path() / "prog_code_compiler.bin", std::string(16, '\0'));
        // A binary covers a range by address only when its group puts the range in the
        // range's own space: this PAGE_20 line lies inside the CODE binary's addresses.
        const fs::path foreign = dir / "foreign" / "prog.map";
        fs::create_directories(foreign.parent_path());
        write_text(foreign,
            rec("__CODE_head", 0x8000, "", "", "", "const") + rec("__PAGE_20_head", 0x8100, "", "", "", "const") +
            rec("__C_LINE_3_a", 0x8100, "a_c", "PAGE_20", "a.c:3"));
        write_text(foreign.parent_path() / "prog_CODE.bin", std::string(0x2000, '\x11'));
        SourceMap mf;
        const auto rf = load(mf, foreign);
        const SourceMap::SpaceReader all22 = [](FetchSource, uint16_t) -> std::optional<uint8_t> { return 0x22; };
        SourceMap md;
        const auto rd = load(md, decoy);
        // Self-modifying code is data (D6): `smc_*` / `SMC_*` ranges are covered by the binary but
        // never checked, while the code range beside them is.
        const fs::path smc = dir / "smc" / "prog.map";
        fs::create_directories(smc.parent_path());
        write_text(smc,
            rec("__CODE_head", 0x9000, "", "", "", "const") +
            rec("__C_LINE_3_a", 0x9000, "a_c", "code_compiler", "a.c:3") +
            rec("__C_LINE_4_a", 0x9010, "a_c", "smc_patch", "a.c:4") +
            rec("__C_LINE_5_a", 0x9020, "a_c", "SMC_PATCH", "a.c:5") +
            rec("__C_LINE_6_a", 0x9030, "a_c", "code_compiler", "a.c:6"));
        write_text(smc.parent_path() / "prog_CODE.bin", std::string(0x400, '\x11'));
        SourceMap ms;
        const auto rs = load(ms, smc);
        std::vector<uint8_t> smc_mem(0x2000, 0x11);
        const auto smc_reader = [&smc_mem]() -> SourceMap::SpaceReader {
            return [&smc_mem](FetchSource sp, uint16_t a) -> std::optional<uint8_t> {
                if (!(sp == P4)) return std::nullopt;
                return smc_mem[a & 0x1FFF];
            };
        };
        const bool smc_intact = ms.verify_program(logical_junk, smc_reader()) == true;
        for (int i = 0x1010; i < 0x1030; ++i) smc_mem[i] = 0x99;   // the patched bytes
        const bool smc_patched_ok = ms.verify_program(logical_junk, smc_reader()) == true;
        smc_mem[0x1000] = 0x99;                                     // a code byte
        const bool smc_code_bad = ms.verify_program(logical_junk, smc_reader()) == false;
        // A section binary that fails source_file_problem() refuses the half and is named: over the
        // cap, and a directory (a FIFO would block the load, the reason the check is made).
        const fs::path badbin = dir / "badbin" / "prog.map";
        fs::create_directories(badbin.parent_path());
        fs::copy_file(v24 / "sccz80-debug" / "prog.map", badbin);
        SourceMap kept;
        load(kept, v24 / "sccz80-debug" / "prog.map");
        const std::string kept_file = kept.loaded_file();
        write_text(badbin.parent_path() / "prog_CODE.bin", "x");
        fs::resize_file(badbin.parent_path() / "prog_CODE.bin", SOURCE_FILE_MAX_BYTES + 1);
        const auto rbig = load(kept, badbin);
        fs::remove(badbin.parent_path() / "prog_CODE.bin");
        fs::create_directories(badbin.parent_path() / "prog_CODE.bin");
        const auto rdir = load(kept, badbin);
        const bool bins_refused = !rbig && rbig.error.find("prog_CODE.bin") != std::string::npos &&
            !rdir && rdir.error.find("prog_CODE.bin") != std::string::npos && rdir.error.find("not a regular file") != std::string::npos &&
            kept.size() == 46 && kept.loaded_file() == kept_file;
        check("Z88MAP-14", "the identity is the section binaries against the range's own page: code is checked, data and bss are not, no binary is unverified, a bank not mapped is checked",
              nex.ok && nex20.ok && r && verdict(original) == true && verdict(a) == false &&
                  verdict(b) == true && verdict(c) == true &&
                  rl && !lonely.verify_program(logical_junk, original.reader()).has_value() &&
                  r20 && m20.verify_program(logical_junk, ok20.reader()) == true &&
                  m20.verify_program(logical_junk, bad20.reader()) == false &&
                  m20.verify_program(logical_junk, last20.reader()) == false &&
                  m20.verify_program(logical_junk, no20.reader()) == false &&   // page 20 unreadable
                  m20.verify_program(logical_junk).value_or(true) == false &&
                  rd && !md.verify_program(logical_junk, original.reader()).has_value() &&
                  rf && mf.lookup(P20, 0x8100) && !mf.verify_program(logical_junk, all22).has_value() &&
                  rs && smc_intact && smc_patched_ok && smc_code_bad && bins_refused);
    }

    // ── Z88MAP-15: the symbol half is the symbol loader's ──
    {
        SymbolTable st;
        const int n = st.load_z88dk_map((v24 / "sccz80-debug" / "prog.map").string());
        const auto key = st.find_name("_main");
        check("Z88MAP-15", "the same map still gives its 19 addr symbols, with _main at $8C92 and no line record among them",
              n == 19 && st.lookup(MemSpace::cpu(), 0x8C92) == std::string("main") && key && key->addr == 0x8C92);
    }

    // ── Z88MAP-16..20: the other compilers, the toolchain's own quirks ──
    bool z23a = false;
    {
        SourceMap sdcc;
        const auto r = load(sdcc, master / "sdcc-debug" / "prog.map");
        check("Z88MAP-16", "a line with two ranges has two starts on sdcc (main.c:18 at $8C6F and $8C89); sccz80 has one ($8C9D)",
              r && starts_are(sdcc.resolve("main.c:18"), {{P4, 0x8C6F}, {P4, 0x8C89}}) &&
                  starts_are(sc.resolve("main.c:18"), {{P4, 0x8C9D}}));
        check("Z88MAP-17", "master sdcc reports header code compiled into util_c as the map says: $8CAC is util.h:9",
              is_at(sdcc.lookup(P4, 0x8CAC), "util.h", 9));
        z23a = starts_are(sdcc.resolve("main.c:14"), {{P4, 0x8C6B}}) &&
               starts_are(sdcc.resolve("main.c:25"), {{P4, 0x8CA6}}) &&
               starts_are(sdcc.resolve("main.c:28"), {{P4, 0x8CAA}}) &&
               is_at(sdcc.lookup(P4, 0x8C6B), "main.c", 17);
    }
    {
        SourceMap m;
        const auto r = load(m, master / "80cc-debug" / "prog.map");
        check("Z88MAP-18", "80cc: main.c:10 at $8C62; line 14 has no code; 19 and 20 share $8C88 in main::0::5 so 19 is an alias",
              r && is_at(m.lookup(P4, 0x8C62), "main.c", 10) &&
                  m.resolve("main.c:14").error == "no code at main.c:14" &&
                  starts_are(m.resolve("main.c:19"), {{P4, 0x8C88}}));
    }
    {
        SourceMap m;
        const auto r = load(m, master / "sccz80-debug" / "prog.map");
        check("Z88MAP-19", "sccz80 splits a statement over two lines on 2.4 (19 ends at $8CBD, 20 starts at $8CBE) and on master at $8CBA (GH #445: z88dk v2.4-2883 moved it)",
              r && is_at(sc.lookup(P4, 0x8CBD), "main.c", 19) && is_at(sc.lookup(P4, 0x8CBE), "main.c", 20) &&
                  is_at(m.lookup(P4, 0x8CB9), "main.c", 19) && is_at(m.lookup(P4, 0x8CBA), "main.c", 20));
    }
    {
        SourceMap m20;
        const auto r = load(m20, v24 / "sccz80-page20" / "prog.map");
        // D3: the tail bounds only when it is above the start. Fixture, v2.4 sccz80-debug:
        // `__ASM_LINE_9496 = $800C ; CODE`, `__CODE_tail = $800C`; $800C-$8031 is code_crt_init
        // (`ld hl,$8E8F` ...), $8032 is `call _main` in code_crt_main; next record 9535 at $8035.
        // Neither has a record of its own, so the extent to the next start gives line 9496
        // [$800C, $8035).
        const SourceLocation* const at_tail = sc.lookup(P4, 0x800C);
        const SourceLocation* const before_next = sc.lookup(P4, 0x8034);
        const auto ends_with = [](const std::string& text, const std::string& tail) {
            return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
        };
        const bool crt_line_9496 = at_tail && before_next && at_tail->line == 9496 && before_next->line == 9496 &&
            ends_with(at_tail->file, "zxn_crt.asm.m4") && sc.lookup(P4, 0x8035) && sc.lookup(P4, 0x8035)->line == 9535;
        check("Z88MAP-20", "a last line ends at its section's tail: asmfn.asm:10 at $8D6C (__code_user_tail $8D6D), banked.c:9 at $C023 (__PAGE_20_tail $C024)",
              is_at(sc.lookup(P4, 0x8D6C), "asmfn.asm", 10) && !sc.lookup(P4, 0x8D6D) &&
                  r && is_at(m20.lookup(P20, 0xC023), "banked.c", 9) && !m20.lookup(P20, 0xC024) &&
                  crt_line_9496);
    }

    // ── Z88MAP-21..22: the extent caps ──
    {
        SourceMap m;
        const auto r = load(m, synth("z21.map", rec("__C_LINE_3_a", 0x9000, "a_c", "code_compiler", "a.c:3")));
        check("Z88MAP-21", "with nothing to stop it a line covers 0x400 bytes (DeZog's MAX_C_LINE_SIZE)",
              r && is_at(m.lookup(P4, 0x93FF), "a.c", 3) && !m.lookup(P4, 0x9400));
    }
    {
        SourceMap m;
        const auto r = load(m, synth("z22.map",
            rec("__C_LINE_3_a", 0x9F00, "a_c", "code_compiler", "a.c:3") +
            rec("__C_LINE_4_a", 0xA010, "a_c", "code_compiler", "a.c:4")));
        check("Z88MAP-22", "a line does not run past the 8K page end: $9FFF is in the line, $A005 is not",
              r && is_at(m.lookup(P4, 0x9FFF), "a.c", 3) && !m.lookup(P4, 0xA000) && !m.lookup(P4, 0xA005));
    }

    // ── Z88MAP-23(b), 24..27: sdcc's later entry, refusal, atomicity ──
    {
        SourceMap m;
        const auto r = load(m, synth("z23.map",
            rec("__C_LINE_12_a", 0x9010, "a_c", "code_compiler", "a.c::x::10000::1\t:12") +
            rec("__C_LINE_20_a", 0x9010, "a_c", "code_compiler", "a.c::x::10000::2\t:20") +
            rec("_next", 0x9010, "a_c", "code_compiler", "a.c::x::10000::2\t:20")));
        // Two modules' lines at one address: no function is known, but they are not one function.
        SourceMap modules;
        const auto rm = load(modules, synth("z23m.map",
            rec("__C_LINE_5_x", 0x9100, "x_c", "code_compiler", "x.c:5") +
            rec("__C_LINE_7_y", 0x9100, "y_c", "code_compiler", "y.c:7") +
            rec("__C_LINE_9_x", 0x9200, "x_c", "code_compiler", "x.c:9")));
        // Not a function entry that comes later: the lower line stays an alias.
        const char* const not_entries[] = {
            "loop = $9400 ; addr, local, , m_c, code_compiler, m.c:6\n",          // not a `_` name
            "_f = $9400 ; addr, public, , n_c, code_compiler, m.c:6\n",           // another module
            "_f = $9400 ; addr, public, , m_c, code_compiler, other.c:6\n",       // another file
            "_f = $9400 ; addr, public, , m_c, code_compiler, m.c:3\n",           // not a later line
            "_f = $9500 ; addr, public, , m_c, code_compiler, m.c:6\n",           // another address
        };
        bool aliases_kept = true;
        for (const char* symbol : not_entries) {
            SourceMap a;
            const auto ra = load(a, synth("z23n.map",
                rec("__C_LINE_3_q", 0x9400, "m_c", "code_compiler", "m.c:3") +
                rec("__C_LINE_6_q", 0x9400, "m_c", "code_compiler", "m.c:6") + symbol));
            aliases_kept = aliases_kept && ra && starts_are(a.resolve("m.c:3"), {{P4, 0x9400}});
        }
        SourceMap early;
        const auto re = load(early, synth("z23e.map",
            rec("__C_LINE_12_a", 0x9010, "a_c", "code_compiler", "a.c::x::10000::1\t:12") +
            rec("__C_LINE_20_a", 0x9010, "a_c", "code_compiler", "a.c::x::10000::2\t:20") +
            rec("_cur", 0x9010, "a_c", "code_compiler", "a.c::x::10000::1\t:12")));
        check("Z88MAP-23", "sdcc has no function name (x): 14 is an alias of 17 at $8C6B (_main is located at line 14), 25 of 26, 28 of 29; a function entry located at a later line means the lower line has no code",
              z23a && r && m.resolve("a.c:12").error == "no code at a.c:12" && starts_are(m.resolve("a.c:20"), {{P4, 0x9010}}) &&
                  re && starts_are(early.resolve("a.c:12"), {{P4, 0x9010}}) &&
                  aliases_kept && rm && modules.resolve("x.c:5").error == "no code at x.c:5" &&
                  starts_are(modules.resolve("y.c:7"), {{P4, 0x9100}}));
    }
    {
        const char* const body[3] = {"__C_LINE_3_a = $C000 ; addr, local, , a_c, DIV_3, a.c:3\n",
                                     "__C_LINE_3_a = $C000 ; addr, local, , a_c, PAGE_16, a.c:3\n",
                                     "__C_LINE_3_a = $C000 ; addr, local, , a_c, PAGE_224, a.c:3\n"};
        bool all = true;
        for (int i = 0; i < 3; ++i) {
            SourceMap m;
            const auto r = load(m, synth("z24.map", body[i]));
            all = all && !r && r.count == -1 && r.error.find("line 1") != std::string::npos && m.empty();
        }
        // A PAGE line below its section's head, one that runs on past page 223, and numbers past
        // the limits (BANK 111, DIV 15) refuse the half too.
        const char* const more[] = {
            "__PAGE_16_head = $C100 ; const, public, def, , ,\n__C_LINE_3_a = $C000 ; addr, local, , a_c, PAGE_16, a.c:3\n",
            "__PAGE_223_head = $C000 ; const, public, def, , ,\n__C_LINE_3_a = $E000 ; addr, local, , a_c, PAGE_223, a.c:3\n",
            "__C_LINE_3_a = $C000 ; addr, local, , a_c, BANK_112, a.c:3\n",
            "__C_LINE_3_a = $C000 ; addr, local, , a_c, DIV_16, a.c:3\n",
        };
        for (const char* text : more) {
            SourceMap m;
            const auto r = load(m, synth("z24b.map", text));
            all = all && !r && r.count == -1 && r.error.find("line ") != std::string::npos && m.empty();
        }
        check("Z88MAP-24", "a DIV section, a PAGE section with no __PAGE_n_head or below it or running past page 223, and a number out of range each refuse the half, naming the line",
              all);
    }
    {
        SourceMap bad_loc;
        SourceMap bad_value;
        const auto rl = load(bad_loc, synth("z25a.map",
            rec("__x", 1, "a_c", "", "", "const") + rec("__C_LINE_3_a", 0x9000, "a_c", "code_compiler", "main.c:abc")));
        const auto rv = load(bad_value, synth("z25b.map",
            rec("__x", 1, "a_c", "", "", "const") + rec("__C_LINE_3_a", 0x1000000, "a_c", "code_compiler", "a.c:3")));
        SourceMap bad_bank;
        const auto rb = load(bad_bank, synth("z25c.map",
            rec("__x", 1, "a_c", "", "", "const") + rec("__C_LINE_3_a", 0x1000000, "a_c", "BANK_10", "a.c:3")));
        // Records that do not tokenize at all (no `$` value, no `;`) refuse the half too.
        SourceMap bad_tok1;
        SourceMap bad_tok2;
        const auto rt1 = load(bad_tok1, synth("z25d.map",
            rec("__x", 1, "a_c", "", "", "const") + "__C_LINE_3_a = 9000 ; addr, local, , a_c, code_compiler, a.c:3\n"));
        const auto rt2 = load(bad_tok2, synth("z25e.map",
            rec("__x", 1, "a_c", "", "", "const") + "__ASM_LINE_3_a = $9000\n"));
        SourceMap loaded;
        load(loaded, v24 / "sccz80-debug" / "prog.map");
        const std::string before = loaded.loaded_file();
        const auto rn = load(loaded, kNoDebugMap);
        check("Z88MAP-25", "an unparsable location and a value over 24 bits refuse the half naming line 2; a map with no line records gives 0, no error, and leaves the store alone",
              !rl && rl.error.find("line 2") != std::string::npos && !rv && rv.error.find("line 2") != std::string::npos &&
                  !rb && rb.error.find("line 2") != std::string::npos && bad_bank.empty() &&
                  bad_loc.empty() && bad_value.empty() &&
                  !rt1 && rt1.error.find("line 2") != std::string::npos && bad_tok1.empty() &&
                  !rt2 && rt2.error.find("line 2") != std::string::npos && bad_tok2.empty() &&
                  rn && rn.count == 0 && rn.error.empty() && loaded.size() == 46 && loaded.loaded_file() == before);
    }
    {
        SourceMap m;
        load(m, v24 / "sccz80-debug" / "prog.map");
        const std::string before = m.loaded_file();
        const auto bad = load(m, synth("z26.map", "__C_LINE_3_a = $9000 ; addr, local, , a_c, code_compiler, main.c:abc\n"));
        check("Z88MAP-26", "a refused load leaves the previous store and its file name",
              !bad && m.size() == 46 && m.loaded_file() == before && is_at(m.lookup(P4, 0x8CB8), "main.c", 19));
    }
    {
        const fs::path huge = dir / "huge.map";
        write_text(huge, "_x = $9000 ; addr, public, , m, code, a.c:1\n");
        fs::resize_file(huge, SOURCE_FILE_MAX_BYTES + 1);
        SymbolTable st;
        st.load_z88dk_map((v24 / "sccz80-debug" / "prog.map").string());
        const size_t before = st.symbols().size();
        std::string why_z88dk;
        std::string why_simple;
        const int rz = st.load_z88dk_map(huge.string(), &why_z88dk);
        const int rs = st.load_simple_map(huge.string(), &why_simple);
        SourceMap m;
        const auto rm = load(m, huge);
        const std::string expected = source_file_problem(huge.string());
        check("Z88MAP-27", "a map over the size cap is refused by both symbol loaders with the reason, the loaded table stays; the source half refuses it too",
              !expected.empty() && rz == -1 && rs == -1 && why_z88dk == expected && why_simple == expected &&
                  st.symbols().size() == before && st.lookup_name("_main") == 0x8C92 && !rm && rm.error == expected &&
                  program_loads_later("a.tap") && program_loads_later("DIR/A.TZX") && program_loads_later("a.wav") &&
                  !program_loads_later("a.nex") && !program_loads_later("a.sna") && !program_loads_later("tap"));
    }
    {
        // GH #427 stage 2a: the two sjasmplus-capable symbol loaders obey the same cap.
        // Sparse, so it costs no disk. The reads stop at the cap, they do not need the file.
        const fs::path huge = dir / "huge.sym";
        write_text(huge, "X: EQU 0x00001234\n");
        fs::resize_file(huge, SOURCE_FILE_MAX_BYTES + 1);
        const std::string equ = (fs::path(JNEXT_SJASMEQU_DIR) / "equlabels.sym").string();
        SymbolTable st;
        st.load_sjasmplus_equ(equ);
        const size_t before = st.symbols().size();
        std::string why_equ;
        const int re = st.load_sjasmplus_equ(huge.string(), &why_equ);
        check("MAPCAP-01", "load_sjasmplus_equ() refuses a file over the size cap before opening it, "
                           "with source_file_problem()'s reason, and the loaded table stays",
              !source_file_problem(huge.string()).empty() && re == -1 &&
                  why_equ == source_file_problem(huge.string()) && before > 0 &&
                  st.symbols().size() == before && st.lookup_name("main") == 0x8000);
        std::string why_both;
        const int rb = st.load_z88dk_or_sjasmplus(huge.string(), &why_both);
        std::string why_ok;
        const int ok = st.load_z88dk_or_sjasmplus(equ, &why_ok);
        check("MAPCAP-02", "load_z88dk_or_sjasmplus() refuses more than the size cap from its read, "
                           "naming the limit, with the table kept; a normal file under it still loads",
              rb == -1 && why_both == "input is over the " + std::to_string(SOURCE_FILE_MAX_BYTES) +
                                          "-byte limit" &&
                  ok == 16 && why_ok.empty() && st.lookup_name("main") == 0x8000);
        fs::remove(huge);
        // The boundary, the other side: exactly the cap is NOT over it.
        const fs::path exact = dir / "exact.sym";
        write_text(exact, "edge: EQU 0x00004321\n");
        fs::resize_file(exact, SOURCE_FILE_MAX_BYTES);
        SymbolTable edge;
        std::string why_edge;
        const int re2 = edge.load_z88dk_or_sjasmplus(exact.string(), &why_edge);
        check("MAPCAP-03", "load_z88dk_or_sjasmplus() accepts a file of exactly the size cap (the bound is "
                           "\"more than\"): it loads, says nothing, and the name resolves",
              re2 >= 0 && why_edge.empty() && edge.lookup_name("edge") == 0x4321);
        fs::remove(exact);
    }
    {
        const auto with = [](uint16_t h) {
            return [h](int) -> std::optional<uint16_t> { return h; };
        };
        const auto page = [&](const char* section, uint16_t h, uint32_t v) {
            const auto s = z88dk_section_space(section, v, with(h));
            return s.kind == Z88dkSpace::Kind::Paged ? static_cast<int>(s.space.index) : -1;
        };
        const auto bank = [&](const char* section, uint32_t v) {
            const auto s = z88dk_section_space(section, v, no_head);
            return s.kind == Z88dkSpace::Kind::Paged ? static_cast<int>(s.space.index) : -1;
        };
        check("Z88MAP-28", "the section rules: PAGE_n runs on into later pages by its head offset, BANK_n and its siblings by bit 13, _L and _H fix the half, DIV is unpaged, others bind at load",
              page("PAGE_16", 0xC000, 0xEEE0) == 17 && page("PAGE_16", 0xE100, 0xE358) == 16 &&
                  page("PAGE_16", 0xC100, 0xE000) == 17 && page("CODE_PAGE_16", 0xC000, 0xEEE0) == 17 &&
                  bank("BANK_10", 0xD000) == 20 && bank("BANK_10", 0xE000) == 21 && bank("BANK_10", 0xA000) == 21 &&
                  bank("BANK_10_L", 0xC000) == 20 && bank("BANK_10_H", 0xC000) == 21 &&
                  bank("BANK_10_H", 0xE000) == 21 && bank("CODE_10", 0xC000) == 20 &&
                  bank("RODATA_10_align_256", 0xD000) == 20 &&
                  z88dk_section_space("DIV_3", 0x2000, no_head).kind == Z88dkSpace::Kind::Unpaged &&
                  bank("BANK_111", 0xC000) == 222 && bank("BANK_111", 0xE000) == 223 &&
                  z88dk_section_space("BANK_112", 0xC000, no_head).kind == Z88dkSpace::Kind::Unpaged &&
                  z88dk_section_space("BANK_112", 0x1C000, no_head).kind == Z88dkSpace::Kind::Refused &&
                  z88dk_section_space("DIV_15", 0x2000, no_head).why.find("out of range") == std::string::npos &&
                  z88dk_section_space("DIV_16", 0x2000, no_head).why.find("out of range") != std::string::npos &&
                  z88dk_section_space("DIV_16", 0x12000, no_head).kind == Z88dkSpace::Kind::Refused &&
                  z88dk_section_space("PAGE_16", 0xC000, with(0xC100)).kind == Z88dkSpace::Kind::Unpaged &&
                  z88dk_section_space("PAGE_223", 0xE000, with(0xC000)).kind == Z88dkSpace::Kind::Unpaged &&
                  page("PAGE_223", 0xC000, 0xDFFF) == 223 &&
                  z88dk_section_space("code_compiler", 0x8000, no_head).kind == Z88dkSpace::Kind::BindAtLoad);
    }

    {
        // GH #345: newlib's CRT_ORG_PAGE_n carries the page for __banked, so a runnable
        // build's __PAGE_n_head is 24-bit ($14C000); design 9.4 rule 1 reads its low 16 bits.
        SourceMap ms;
        SourceMap mk;
        const auto rs = load(ms, master / "sdcc-banked-run" / "prog.map");
        const auto rk = load(mk, v24 / "sccz80-banked-run" / "prog.map");
        SourceMap over;
        const auto ro = load(over, synth("z29.map",
            rec("__PAGE_20_head", 0x100C000, "", "", "", "const") +
            rec("__C_LINE_3_a_2ec", 0x14C000, "a_c", "PAGE_20", "a.c:3")));
        check("Z88MAP-29", "a 24-bit __PAGE_20_head ($14C000, the runnable __banked builds) attaches: banked.c:8 is in page 20 at $C008 (master) and $C003 (v2.4); a head over 24 bits is still refused",
              rs && rs.count == 35 && starts_are(ms.resolve("banked.c:8"), {{P20, 0xC008}}) &&
                  is_at(ms.lookup(P20, 0xC000), "banked.c", 5) &&
                  rk && starts_are(mk.resolve("banked.c:8"), {{P20, 0xC003}}) &&
                  is_at(mk.lookup(P20, 0xC01B), "banked.c", 9) &&
                  !ro && ro.error.find("has no __PAGE_20_head") != std::string::npos);
    }

    // ── Z88SYM-*: the symbol half of the same map, GH #375 stage 3 ──────────────
    // Oracle: the fixtures' NEX placement (prog_PAGE_20.bin / prog_BANK_10.bin at NEX bank
    // 10 +0 = page 20; prog_CODE.bin at bank 2 +0 = page 4), design 9.4 and #344 D8.
    {
        using Key = SymbolNameKey;
        const auto key = [](const SymbolTable& t, const char* n) { return t.find_name(n); };
        const auto is_key = [&](const SymbolTable& t, const char* n, uint16_t a,
                                std::optional<FetchSource> sp, bool bind) {
            return key(t, n) == std::optional<Key>(Key{a, sp, bind});
        };
        const auto name_is = [](const std::optional<std::string>& got, const char* want) {
            return got && *got == want;
        };
        const auto load_sym = [](SymbolTable& t, const fs::path& map) {
            return t.load_z88dk_map(map.string());
        };

        {   // Z88SYM-01 / 02
            SymbolTable a, b, c;
            const int na = load_sym(a, v24 / "sccz80-page20" / "prog.map");
            const int nb = load_sym(b, master / "sdcc-banked" / "prog.map");
            check("Z88SYM-01", "rule 1 through the symbol loader: _banked_add of a PAGE_20 section is a label of page 20 "
                               "(sccz80 on 2.4, sdcc on master), shown as banked_add in page 20 only",
                  na == 10 && nb == 9 && is_key(a, "_banked_add", 0xC000, P20, false) &&
                      is_key(b, "_banked_add", 0xC000, P20, false) &&
                      name_is(a.lookup(P20, 0xC000), "banked_add") && !a.lookup(P21, 0xC000) &&
                      !a.lookup(MemSpace::cpu(), 0xC000) && name_is(b.lookup(P20, 0xC000), "banked_add") &&
                      !b.lookup(P21, 0xC000));
            const int nc = load_sym(c, v24 / "sccz80-bank10" / "prog.map");
            check("Z88SYM-02", "rule 2: _banked_add of a BANK_10 section is a label of page 20 at $C000",
                  nc == 10 && is_key(c, "_banked_add", 0xC000, P20, false) && !c.lookup(P21, 0xC000));
        }
        {   // Z88SYM-03
            SymbolTable a, b;
            const int na = load_sym(a, v24 / "classic-bank10" / "prog.map");
            const int nb = load_sym(b, master / "classic-bank10" / "prog.map");
            check("Z88SYM-03", "a classic 24-bit value in a numbered section ($AC000, $14C000) is a label of its page "
                               "(6 fixture-module symbols + 16 of the classic CRT kept by KEEP_CRT, GH #348): "
                               "the bits above 15 are not read; the 24-bit constant stays no name",
                  na == 22 && nb == 22 && is_key(a, "_banked_add", 0xC000, P20, false) &&
                      is_key(b, "_banked_add", 0xC000, P20, false) && !key(a, "__BANK_10_head") &&
                      !key(b, "__BANK_10_head"));
        }
        {   // Z88SYM-04
            SymbolTable t;
            load_sym(t, v24 / "sccz80-debug" / "prog.map");
            const bool unbound = t.symbols().find(0x8C92)->second.bind_at_load &&
                                 !t.symbols().find(0x8C92)->second.space &&
                                 name_is(t.lookup(MemSpace::cpu(), 0x8C92), "main");
            t.bind(nex_space);
            const bool in4 = name_is(t.lookup(P4, 0x8C92), "main") && !t.lookup(MemSpace::cpu(), 0x8C92) &&
                             !t.lookup(MemSpace::page(40), 0x8C92) && is_key(t, "_main", 0x8C92, P4, true);
            t.bind([](uint16_t) { return MemSpace::page(40); });
            const bool in40 = is_key(t, "_main", 0x8C92, MemSpace::page(40), true);
            t.bind({});
            const bool unbound_again = is_key(t, "_main", 0x8C92, std::nullopt, true);
            SymbolTable p;
            load_sym(p, v24 / "sccz80-page20" / "prog.map");
            p.bind(nex_space);
            check("Z88SYM-04", "rule 4: a symbol of a section naming no page is stored unbound and flagged; bind() gives it "
                               "the RAM page mapped at its address and touches no paged label",
                  unbound && in4 && in40 && unbound_again && is_key(p, "_main", 0x8C62, P4, true) &&
                      is_key(p, "_banked_add", 0xC000, P20, false) && is_key(p, "_result", 0x8E82, P4, true));
        }
        {   // Z88SYM-05
            SymbolTable t;
            load_sym(t, master / "sdcc-banked" / "prog.map");
            t.bind(nex_space);
            const bool rom = name_is(t.lookup(MemSpace::cpu(), 0x0000), "zxn") && !key(t, "zxn")->space;
            SymbolTable lo;
            lo.load_z88dk_map(synth("z5.map", rec("_low", 0x2000, "m", "code_user", "m.asm:1")).string());
            lo.bind([](uint16_t) { return MemSpace::page(30); });
            const bool ram = is_key(lo, "_low", 0x2000, MemSpace::page(30), true);
            lo.bind([](uint16_t) { return MemSpace::divmmc_ram(3); });
            check("Z88SYM-05", "a rule-4 symbol whose address is not in a RAM page (ROM, DivMMC) stays unpaged",
                  rom && ram && is_key(lo, "_low", 0x2000, std::nullopt, true));
        }
        {   // Z88SYM-06
            SymbolTable t;
            const int n = t.load_z88dk_map(synth("z6.map",
                rec("_d", 0x2100, "m", "DIV_3", "m.asm:1") +
                rec("_dbig", 0x32100, "m", "DIV_3", "m.asm:1") +
                rec("_nohead", 0xC000, "m", "PAGE_16", "m.asm:1") +
                rec("_oor", 0xD000, "m", "BANK_200", "m.asm:1") +
                rec("_big", 0x1C000, "m", "code_compiler", "m.asm:1") +
                rec("_huge", 0x1000000, "m", "BANK_10", "m.asm:1") +
                rec("_ok", 0xC010, "m", "BANK_10", "m.asm:1")).string());
            check("Z88SYM-06", "what the symbol half does with an Unpaged or Refused result: DIV, a head-less PAGE and an "
                               "out-of-range number stay unpaged (16-bit values), the rest are skipped",
                  n == 4 && is_key(t, "_d", 0x2100, std::nullopt, false) && !key(t, "_dbig") &&
                      is_key(t, "_nohead", 0xC000, std::nullopt, false) &&
                      is_key(t, "_oor", 0xD000, std::nullopt, false) && !key(t, "_big") && !key(t, "_huge") &&
                      is_key(t, "_ok", 0xC010, P20, false));
        }
        {   // Z88SYM-07
            const fs::path m = synth("z7.map",
                rec("__banked_init", 0xC010, "m", "PAGE_16", "m.asm:1") +
                rec("__PAGE_16_head", 0xC000, "", "", "", "const") +
                rec("_late", 0xEEE0, "m", "PAGE_16", "m.asm:2"));
            SymbolTable a, b, c;
            a.load_z88dk_map(m.string());
            b.load_z88dk_or_sjasmplus(m.string());
            c.load_z88dk_or_sjasmplus((v24 / "sccz80-page20" / "prog.map").string());
            check("Z88SYM-07", "the symbol loader reads the file in two passes (a page head after its symbols counts) and --map's "
                               "loader does the same: $EEE0 of PAGE_16 at $C000 is page 17",
                  is_key(a, "__banked_init", 0xC010, MemSpace::page(16), false) &&
                      is_key(a, "_late", 0xEEE0, MemSpace::page(17), false) && a.symbols() == b.symbols() &&
                      is_key(b, "_late", 0xEEE0, MemSpace::page(17), false) &&
                      is_key(c, "_banked_add", 0xC000, P20, false));
        }
        {   // Z88SYM-08
            SymbolTable a, b, c;
            const int na = load_sym(a, master / "sdcc-banked-run" / "prog.map");
            const int nb = load_sym(b, v24 / "sccz80-banked-run" / "prog.map");
            const int nc = c.load_z88dk_map(synth("z8.map",
                rec("__PAGE_21_head", 0x14E000, "", "", "", "const") +
                rec("_b21", 0x14E000, "b_c", "PAGE_21", "b.c:3") +
                rec("__PAGE_30_head", 0x100C000, "", "", "", "const") +
                rec("_over", 0xC000, "o_c", "PAGE_30", "o.c:3")).string());
            check("Z88SYM-08", "a 24-bit __PAGE_n_head ($14C000, the runnable __banked builds) places the symbols by its low "
                               "16 bits; the 24-bit constant is no name; a head past 24 bits is no head",
                  na == 9 && nb == 11 && is_key(a, "_banked_add", 0xC000, P20, false) &&
                      is_key(b, "_banked_add", 0xC000, P20, false) && name_is(a.lookup(P20, 0xC000), "banked_add") &&
                      !a.lookup(P21, 0xC000) && !a.lookup(MemSpace::cpu(), 0xC000) && !key(a, "__PAGE_20_head") &&
                      nc == 2 && is_key(c, "_b21", 0xE000, MemSpace::page(21), false) &&
                      is_key(c, "_over", 0xC000, std::nullopt, false));
        }
        {   // Z88SYM-09
            // z80asm writes a symbol of its default section with an EMPTY section field.
            SymbolTable t;
            const int n = t.load_z88dk_map(synth("z9.map",
                "start                           = $8000 ; addr, local, , p, , p.asm:2\n").string());
            const bool unbound = n == 1 && is_key(t, "start", 0x8000, std::nullopt, true) &&
                                 name_is(t.lookup(MemSpace::cpu(), 0x8000), "start");
            t.bind(nex_space);
            check("Z88SYM-09", "for the symbol half a symbol with an empty section (z80asm's default section) is rule 4: "
                               "flagged and unbound, then in the RAM page mapped at its address, and in no other page",
                  unbound && is_key(t, "start", 0x8000, P4, true) && name_is(t.lookup(P4, 0x8000), "start") &&
                      !t.lookup(MemSpace::page(40), 0x8000) && !t.lookup(MemSpace::cpu(), 0x8000));
        }
    }

    // ── Z88CN-*: C names for z88dk C symbols, GH #424 stage 1 (design 10.6) ────
    // Oracle: zcc heads each file with `MODULE <file, non-alnum -> _>` (zcc.c) and its
    // compilers write every C identifier with a leading `_` (sccz80 Z80ASM_PREFIX); the
    // fixture maps' own __CDBINFO__ records name the C identifiers; the issue text ("that
    // symbol wins") for the ambiguity rows, written in the real map line format.
    {
        const auto at = [&](const SymbolTable& t, const char* n) { return t.find_name(n); };
        const auto shown = [](const std::optional<std::string>& got, const char* want) {
            return got && *got == want;
        };
        const auto load_c = [](SymbolTable& t, const fs::path& map) {
            return t.load_z88dk_map(map.string());
        };
        // The C identifiers the map's own debug records declare: global and file-scope variables and
        // functions (`S:G$x`, `S:Ffile$x`, `F:G$x`, `F:Ffile$x`), their names un-escaped (`_xx` is a hex byte).
        const auto cdb_names = [](const fs::path& map) {
            std::set<std::string> names;
            std::ifstream in(map);
            std::string line;
            while (std::getline(in, line)) {
                if (line.compare(0, 11, "__CDBINFO__") != 0) continue;
                std::string raw, dec;
                for (size_t i = 11; i < line.size() && line[i] != ' '; ++i) raw += line[i];
                for (size_t i = 0; i < raw.size(); ++i) {
                    if (raw[i] == '_' && i + 2 < raw.size() + 0 && std::isxdigit(static_cast<unsigned char>(raw[i + 1])) &&
                        std::isxdigit(static_cast<unsigned char>(raw[i + 2]))) {
                        dec += static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16));
                        i += 2;
                    } else {
                        dec += raw[i];
                    }
                }
                if (dec.size() < 4 || (dec[0] != 'S' && dec[0] != 'F') || dec[1] != ':') continue;
                const size_t kind = dec[2] == 'G' ? 1 : dec[2] == 'F' ? 2 : 0;   // `L` records are locals
                if (!kind) continue;
                const size_t d1 = dec.find('$');
                const size_t d2 = d1 == std::string::npos ? d1 : dec.find('$', d1 + 1);
                if (d2 != std::string::npos) names.insert(dec.substr(d1 + 1, d2 - d1 - 1));
            }
            return names;
        };
        const fs::path sc_map = v24 / "sccz80-debug" / "prog.map";

        {   // Z88CN-01
            SymbolTable t;
            load_c(t, sc_map);
            struct Sym { const char* c; const char* a; uint16_t addr; };
            const Sym syms[] = {{"counter", "_counter", 0x8F59}, {"main", "_main", 0x8C92},
                                {"square", "_square", 0x8C78}, {"table", "_table", 0x8F5A},
                                {"util_twice", "_util_twice", 0x8D35}};
            const auto all_same = [&](const SymbolTable& tt) {
                for (const Sym& y : syms) {
                    const auto k = at(tt, y.a);
                    if (!k || k->addr != y.addr) return false;
                    if (!(at(tt, y.c) == k)) return false;
                    if (!(tt.resolve_key(y.c) == k) || !(tt.resolve_key(std::string("@") + y.c) == k)) return false;
                    if (tt.resolve(y.c) != std::optional<uint16_t>(y.addr)) return false;
                }
                return true;
            };
            const bool unbound = all_same(t);
            t.bind(nex_space);
            const bool bound = all_same(t) && at(t, "counter") == std::optional<SymbolNameKey>(SymbolNameKey{0x8F59, P4, true});
            t.bind({});
            const bool unbound_again = all_same(t);
            const std::set<std::string> cdb = cdb_names(sc_map);
            const std::set<std::string> want = {"counter", "main", "square", "table", "util_twice"};
            check("Z88CN-01", "a C symbol (module main_c / util_c) answers to its C name with the very key of its assembler "
                              "name, unbound, bound to page 4 and unbound again; the C names are the map's own __CDBINFO__ ones",
                  unbound && bound && unbound_again && cdb == want);
        }
        {   // Z88CN-02
            SymbolTable t;
            load_c(t, sc_map);
            const auto names_ok = [&](const SymbolTable& tt, const MemSpace& sp) {
                return shown(tt.lookup(sp, 0x8F59), "counter") && shown(tt.lookup(sp, 0x8F5A), "table") &&
                       shown(tt.lookup(sp, 0x8C92), "main") && shown(tt.lookup(sp, 0x8C78), "square") &&
                       shown(tt.lookup(sp, 0x8D35), "util_twice");
            };
            const bool unpaged = names_ok(t, MemSpace::cpu());
            t.bind(nex_space);
            const auto near = t.nearest(0x8F5B, nex_space);
            check("Z88CN-02", "views show the C name: lookup() on the unpaged branch (before bind) and on the paged one "
                              "(page 4, after), and nearest() uses the same",
                  unpaged && names_ok(t, P4) && near && near->first == 0x8F5A && near->second == "table");
        }
        {   // Z88CN-03
            SymbolTable t;
            load_c(t, sc_map);
            SymbolTable s;
            const int n = s.load_z88dk_map(synth("cn3.map",
                "_main                           = $8000 ; addr, public\n" +
                rec("__xinit_c", 0x8100, "main_c", "code_compiler", "m.c:1") +
                rec("_k", 0x8200, "main_c", "code_compiler", "m.c:2", "const") +
                rec("_y", 0x8300, "y_c_asm", "code_compiler", "y.asm:1")).string());
            check("Z88CN-03", "not C: an asm module's _asm_bump, a main_c symbol with no _ (i_2), a module-less _main, __x, "
                              "a _k constant and a module that only contains _c keep their names and their C name is unresolved",
                  shown(t.lookup(MemSpace::cpu(), 0x8D65), "_asm_bump") && !at(t, "asm_bump") &&
                      shown(t.lookup(MemSpace::cpu(), 0x8CA6), "i_2") && at(t, "i_2") &&
                      n == 3 && shown(s.lookup(MemSpace::cpu(), 0x8000), "_main") && !at(s, "main") &&
                      shown(s.lookup(MemSpace::cpu(), 0x8100), "__xinit_c") && !at(s, "_xinit_c") &&
                      at(s, "_k") && !s.lookup(MemSpace::cpu(), 0x8200) && !at(s, "k") &&
                      shown(s.lookup(MemSpace::cpu(), 0x8300), "_y") && !at(s, "y"));
        }
        {   // Z88CN-04
            struct Prog { const fs::path map; uint16_t counter; };
            const Prog progs[] = {{v24 / "sdcc-SO0" / "prog.map", 0x8ED5}, {v24 / "sdcc-SO2" / "prog.map", 0x8ECD},
                                  {master / "sccz80-debug" / "prog.map", 0x8F4A},
                                  {master / "sdcc-debug" / "prog.map", 0x8EBE},
                                  {master / "80cc-debug" / "prog.map", 0x8EF6}};   // GH #445: master sccz80/80cc moved with z88dk v2.4-2883
            bool all = true;
            for (const Prog& p : progs) {
                SymbolTable t;
                load_c(t, p.map);
                t.bind(nex_space);
                all = all && t.resolve("counter") == std::optional<uint16_t>(p.counter) &&
                      t.resolve("_counter") == std::optional<uint16_t>(p.counter) &&
                      shown(t.lookup(P4, p.counter), "counter");
            }
            SymbolTable sub;
            load_c(sub, v24 / "sccz80-subdir" / "prog.map");
            check("Z88CN-04", "every compiler and install writes the same _name in a <file>_c module: counter resolves and is shown "
                              "on sdcc-SO0, sdcc-SO2 and master sccz80, sdcc and 80cc; game/util.c (module game_util_c) is C too",
                  all && sub.resolve("util_twice") == std::optional<uint16_t>(0x8D35));
        }
        {   // Z88CN-05
            const auto banked = [&](const fs::path& map) {
                SymbolTable t;
                load_c(t, map);
                const auto k = at(t, "banked_add");
                return k && k->addr == 0xC000 && k->space == std::optional<FetchSource>(P20) && !k->bind_at_load &&
                       k == at(t, "_banked_add") && shown(t.lookup(P20, 0xC000), "banked_add") &&
                       !t.lookup(P21, 0xC000) && !t.lookup(MemSpace::cpu(), 0xC000);
            };
            SymbolTable far;
            far.load_z88dk_map(synth("cn5.map", rec("_far", 0x1C000, "m_c", "code_compiler", "m.c:1")).string());
            check("Z88CN-05", "a banked C function is C too, in its page only (v2.4 sccz80-page20, master sdcc-banked, both "
                              "classic-bank10 with their 24-bit values); a refused symbol leaves its C name unresolved",
                  banked(v24 / "sccz80-page20" / "prog.map") && banked(master / "sdcc-banked" / "prog.map") &&
                      banked(v24 / "classic-bank10" / "prog.map") && banked(master / "classic-bank10" / "prog.map") &&
                      !at(far, "_far") && !at(far, "far"));
        }
        {   // Z88CN-06
            const std::string c_line = rec("_counter", 0x9000, "main_c", "code_compiler", "m.c:1");
            const std::string a_line = rec("counter", 0x9100, "asmfn_asm", "code_compiler", "a.asm:1");
            bool a_ok = true;
            for (int order = 0; order < 2; ++order) {
                SymbolTable t;
                t.load_z88dk_map(synth("cn6a.map", order ? a_line + c_line : c_line + a_line).string());
                a_ok = a_ok && t.resolve("counter") == std::optional<uint16_t>(0x9100) &&
                       t.resolve("_counter") == std::optional<uint16_t>(0x9000) &&
                       shown(t.lookup(MemSpace::cpu(), 0x9000), "_counter") &&
                       shown(t.lookup(MemSpace::cpu(), 0x9100), "counter") && t.shown_name("_counter") == "_counter";
            }
            bool b_ok = true;
            const std::string s_line = rec("_size", 0x9200, "main_c", "code_compiler", "m.c:2");
            const std::string k_line = rec("size", 0x10, "", "", "", "const");
            for (int order = 0; order < 2; ++order) {
                SymbolTable t;
                t.load_z88dk_map(synth("cn6b.map", order ? k_line + s_line : s_line + k_line).string());
                b_ok = b_ok && t.resolve("size") == std::optional<uint16_t>(0x10) &&
                       shown(t.lookup(MemSpace::cpu(), 0x9200), "_size") && t.resolve("_size") == std::optional<uint16_t>(0x9200);
            }
            check("Z88CN-06", "an exact name wins in either file order: a real `counter` label, or a `size` constant, "
                              "beats the C name of _counter / _size, which is then shown as written",
                  a_ok && b_ok);
        }
        {   // Z88CN-07
            SymbolTable b;
            load_c(b, sc_map);
            SymbolTable a1;
            a1.add_label("counter", 0x9100, std::nullopt);
            a1.merge(b);
            SymbolTable a2;
            a2.add_label("counter", 0x9100, std::nullopt);
            SymbolTable b2 = b;
            b2.merge(a2);
            SymbolTable e;
            e.merge(b);
            SymbolTable a3;
            a3.add_label("_counter", 0x9500, std::nullopt);
            a3.merge(b);
            check("Z88CN-07", "merge(): a real `counter` wins whichever side holds it and the C one is shown as _counter; "
                              "into an empty table the C names survive; an asm `_counter` already there stays `_counter` and the C one keeps `counter`",
                  a1.resolve("counter") == std::optional<uint16_t>(0x9100) && shown(a1.lookup(MemSpace::cpu(), 0x8F59), "_counter") &&
                      b2.resolve("counter") == std::optional<uint16_t>(0x9100) &&
                      shown(b2.lookup(MemSpace::cpu(), 0x8F59), "_counter") &&
                      e.resolve("counter") == std::optional<uint16_t>(0x8F59) && shown(e.lookup(MemSpace::cpu(), 0x8F59), "counter") &&
                      a3.resolve("counter") == std::optional<uint16_t>(0x8F59) &&
                      shown(a3.lookup(MemSpace::cpu(), 0x8F59), "counter") &&
                      shown(a3.lookup(MemSpace::cpu(), 0x9500), "_counter"));
        }
        {   // Z88CN-08
            SymbolTable t;
            load_c(t, sc_map);
            t.load_z88dk_map(synth("cn8.map", rec("_counter", 0x9000, "asmfn_asm", "code_user", "a.asm:1")).string());
            const bool reloaded = !at(t, "counter") && shown(t.lookup(MemSpace::cpu(), 0x9000), "_counter");
            SymbolTable u;
            load_c(u, sc_map);
            u.load_sjasmplus_equ((fs::path(JNEXT_SJASMEQU_DIR) / "equlabels.sym").string());
            SymbolTable v;
            v.load_z88dk_or_sjasmplus(sc_map.string());
            check("Z88CN-08", "a reload replaces the C names with the new file's (an asm-only map has none), a sjasmplus file "
                              "loaded after keeps none of them, and --map's loader gives them as the .map loader does",
                  reloaded && !at(u, "_counter") && u.resolve("counter") == std::optional<uint16_t>(6) &&
                      v.resolve("counter") == std::optional<uint16_t>(0x8F59) &&
                      shown(v.lookup(MemSpace::cpu(), 0x8F59), "counter"));
        }
        {   // Z88CN-09
            SymbolTable t;
            load_c(t, sc_map);
            check("Z88CN-09", "shown_name(): a C symbol's assembler name gives its C name; its C name, an asm label, a "
                              "double-underscore name and an unknown one come back as given",
                  t.shown_name("_counter") == "counter" && t.shown_name("counter") == "counter" &&
                      t.shown_name("_asm_bump") == "_asm_bump" && t.shown_name("nope") == "nope" &&
                      t.shown_name("__xinit_c") == "__xinit_c");
        }
        {   // Z88CN-10
            std::vector<fs::path> maps;
            for (const auto& root : {v24, master})
                for (const auto& d : fs::directory_iterator(root))
                    if (fs::exists(d.path() / "prog.map")) maps.push_back(d.path() / "prog.map");
            maps.push_back(fs::path(JNEXT_ZDEBUGMAP_DIR) / "debug.map");
            maps.push_back(kNoDebugMap);
            bool all = !maps.empty();
            bool each_differs = true;
            for (const fs::path& map : maps) {
                SymbolTable t;
                load_c(t, map);
                t.bind(nex_space);
                bool differs = false;
                for (const auto& [addr, label] : t.symbols()) {
                    const FetchSource sp = label.space ? *label.space : MemSpace::cpu();
                    const auto name = t.lookup(sp, addr);
                    if (!name) { all = false; continue; }
                    // The label lookup() answers for: the shown name resolves to the same key as its own name.
                    const auto answered = t.lookup_assembler(sp, addr);
                    if (!answered) { all = false; continue; }
                    // A shown C name resolves to the very symbol it labels (address and memory).
                    if (*name != *answered) {
                        const auto k = t.find_name(*name);
                        if (!k || k->addr != addr || !(k->space == label.space)) all = false;
                    } else if (!t.find_name(*name)) {
                        all = false;
                    }
                    if (*name != *answered) differs = true;
                }
                each_differs = each_differs && differs;
            }
            SymbolTable inc;
            load_c(inc, master / "sdcc-debug" / "prog.map");
            inc.bind(nex_space);
            // Two C modules each have a local _util_inc: `util_inc` is the first one's, which shows it; the
            // second shows its assembler name, so a shown C name always resolves to its own symbol.
            const auto first_inc = inc.find_name("util_inc");
            int shown_inc = 0, shown_asm = 0;
            for (const auto& [addr, label] : inc.symbols()) {
                if (label.name != "_util_inc") continue;
                if (shown(inc.lookup(P4, addr), "util_inc") && first_inc && first_inc->addr == addr) ++shown_inc;
                if (shown(inc.lookup(P4, addr), "_util_inc") && first_inc && first_inc->addr != addr) ++shown_asm;
            }
            check("Z88CN-10", "over every committed z88dk map, after bind: the shown name of each label resolves to the "
                              "symbol the label's own name does, and each map shows at least one C name; of the two _util_inc "
                              "labels of sdcc-debug the first shows util_inc (which resolves to it), the second _util_inc",
                  all && each_differs && shown_inc == 1 && shown_asm == 1 &&
                      inc.find_name("util_inc") == inc.find_name("_util_inc"));
        }
        {   // Z88CN-11
            SymbolTable t;
            load_c(t, sc_map);
            t.bind(nex_space);
            const auto aname = disasm_text::symbol_namer(&t, nex_space, true);
            const auto cname = disasm_text::symbol_namer(&t, nex_space);
            check("Z88CN-11", "lookup_assembler() keeps the assembler name of a C symbol (and of an asm label); the namer "
                              "built with assembler_names asks it, the default namer gives the C name",
                  shown(t.lookup_assembler(P4, 0x8F59), "_counter") && shown(t.lookup(P4, 0x8F59), "counter") &&
                      shown(t.lookup_assembler(P4, 0x8D65), "_asm_bump") && aname(0x8F59) == std::optional<std::string>("_counter") &&
                      cname(0x8F59) == std::optional<std::string>("counter"));
        }
        {   // Z88CN-12 / 13 / 14: the C-name rule is per SYMBOL, not per assembler name (GH #424 round 1b)
            const std::string c_line = rec("_dup", 0x9400, "main_c", "code_compiler", "m.c:3");
            const std::string a_line = rec("_dup", 0x9300, "x_asm", "code_compiler", "x.asm:1");
            SymbolTable ca;   // C module first, asm second
            ca.load_z88dk_map(synth("cn12.map", c_line + a_line).string());
            check("Z88CN-12", "a C module's _dup then an asm module's _dup: `dup` resolves to the C one ($9400), which "
                              "shows dup; the asm one keeps showing _dup",
                  ca.resolve("dup") == std::optional<uint16_t>(0x9400) &&
                      shown(ca.lookup(MemSpace::cpu(), 0x9400), "dup") && shown(ca.lookup(MemSpace::cpu(), 0x9300), "_dup") &&
                      shown(ca.lookup_assembler(MemSpace::cpu(), 0x9400), "_dup"));
            SymbolTable ac;   // asm module first, C second
            ac.load_z88dk_map(synth("cn13.map", a_line + c_line).string());
            check("Z88CN-13", "an asm module's _dup then a C module's _dup: `dup` still resolves to the C one ($9400) "
                              "and shows dup, the asm one keeps showing _dup; `_dup` resolves to the first (asm)",
                  ac.resolve("dup") == std::optional<uint16_t>(0x9400) &&
                      shown(ac.lookup(MemSpace::cpu(), 0x9400), "dup") && shown(ac.lookup(MemSpace::cpu(), 0x9300), "_dup") &&
                      ac.resolve("_dup") == std::optional<uint16_t>(0x9300) && ac.shown_name("dup") == "dup");
            SymbolTable cc;   // two C modules, each a local _inc at its own address
            cc.load_z88dk_map(synth("cn14.map",
                rec("_inc", 0x9500, "a_c", "code_compiler", "a.c:1") +
                rec("_inc", 0x9600, "b_c", "code_compiler", "b.c:1")).string());
            check("Z88CN-14", "two C modules with the same local name: `inc` resolves to the first ($9500) and that one "
                              "shows inc; the second shows _inc, so no shown C name resolves to another symbol",
                  cc.resolve("inc") == std::optional<uint16_t>(0x9500) &&
                      shown(cc.lookup(MemSpace::cpu(), 0x9500), "inc") && shown(cc.lookup(MemSpace::cpu(), 0x9600), "_inc"));
        }
        {   // Z88CN-15: the identity of a C symbol includes its memory (review 1, F1)
            // Two banked C files each with a static _helper at $C000, one in PAGE_20, one in PAGE_21.
            SymbolTable bk;
            bk.load_z88dk_map(synth("cn15.map",
                rec("__PAGE_20_head", 0x14C000, "", "", "", "const") +
                rec("__PAGE_21_head", 0x15C000, "", "", "", "const") +
                rec("_helper", 0x14C000, "bank20_c", "PAGE_20", "bank20.c:3") +
                rec("_helper", 0x15C000, "bank21_c", "PAGE_21", "bank21.c:3")).string());
            const auto k = bk.find_name("helper");
            check("Z88CN-15", "two banked C files with a static _helper at $C000, in PAGE_20 and PAGE_21: `helper` resolves "
                              "to page 20; page 20 shows helper, page 21 shows _helper (a shown C name never names another page)",
                  k && k->addr == 0xC000 && k->space == std::optional<FetchSource>(P20) &&
                      shown(bk.lookup(P20, 0xC000), "helper") && shown(bk.lookup(P21, 0xC000), "_helper") &&
                      bk.shown_name("helper") == "helper");
        }
        {   // Z88CN-16: a C symbol whose label is not stored still answers to its C name (review 1, F2)
            SymbolTable two;
            two.load_z88dk_map(synth("cn16.map",
                rec("_first", 0x9000, "main_c", "code_compiler", "m.c:1") +
                rec("_second", 0x9000, "main_c", "code_compiler", "m.c:1")).string());
            check("Z88CN-16", "two C symbols at one address: first, second, _first and _second all resolve to it, and the view shows the first, first",
                  two.resolve("first") == std::optional<uint16_t>(0x9000) && two.resolve("second") == std::optional<uint16_t>(0x9000) &&
                      two.resolve("_first") == std::optional<uint16_t>(0x9000) && two.resolve("_second") == std::optional<uint16_t>(0x9000) &&
                      shown(two.lookup(MemSpace::cpu(), 0x9000), "first"));
        }
    }

    // ── Z88MAP-30 (GH #348): line 0 is z80asm's file-entry marker, no code ──
    {
        bool ok = true;
        const struct { fs::path map; uint16_t at; uint16_t zero; } cases[] = {
            {v24 / "sccz80-incmid" / "prog.map", 0x8C6E, 0x8C76},
            {master / "sccz80-incmid" / "prog.map", 0x8C6E, 0x8C76},
            {master / "80cc-incmid" / "prog.map", 0x8C65, 0x8C72},
        };
        for (const auto& c : cases) {
            SourceMap m;
            const auto r = load(m, c.map);
            bool zero_line = false;
            for (const SourceLocation* l : m.lookup_all(P4, c.zero)) zero_line = zero_line || l->line == 0;
            ok = ok && r && r.count > 0 && r.error.empty() && is_at(m.lookup(P4, c.at), "incmid.c", 3) &&
                 m.lookup(P4, c.zero) && !zero_line;
        }
        {   // a line-0 record alone at its address: no range of line 0 is made from it
            SourceMap m;
            const auto r = load(m, synth("z30.map",
                rec("__C_LINE_0_a_2eh", 0x9000, "a_c", "code_compiler", "a.h::f::0::1:0") +
                rec("__C_LINE_3_a_2ec", 0x9004, "a_c", "code_compiler", "a.c::f::0::1:3")));
            ok = ok && r && r.count == 1 && !m.lookup(P4, 0x9000) && is_at(m.lookup(P4, 0x9004), "a.c", 3);
        }
        {   // an empty line number is not line 0: it is refused, naming the map line
            SourceMap m;
            const auto r = load(m, synth("z30b.map", rec("__C_LINE_3_a_2ec", 0x9000, "a_c", "code_compiler", "main.c:")));
            ok = ok && !r && r.error.find("line 1") != std::string::npos;
        }
        check("Z88MAP-30", "a record of line 0 in a code section (sccz80 and 80cc write one for an #include after a function) is no code: the map attaches, incmid.c:3 is where its record puts it, and no location at the line-0 record's address has line 0",
              ok);
    }

    // ── Z88MAP-31 (GH #445): the classic compiler's -debug build, producible since z88dk v2.4-2883 ──
    // Oracle: master/classic-debug/prog.map (banked.c compiled into BANK_10, __BANK_10_head = $14C000,
    // banked.c:6 at $14C000, :7 and :8 at $14C003, :9 at $14C01D; CRT_ORG_BANK_10 = $C000 in this CRT)
    // and README "banked_add is the only function in the bank".
    {
        SourceMap m;
        const auto r = load(m, master / "classic-debug" / "prog.map");
        const VariableStore* s = store_of(m);
        const CFunction* f = s ? fn_named(*s, "banked_add") : nullptr;
        const NexMemory nex(master / "classic-debug" / "prog.nex");
        check("Z88MAP-31", "master classic-debug (-clib=classic -debug, BANK_10): the map attaches with its section binary "
                           "(identity verified against prog.nex), "
                           "banked.c:6 starts at page 20 $C000, :8 at $C003 (:7 is its alias), :9 at $C01D, and the "
                           "C store has banked_add resolved at page 20 $C000",
              r && starts_are(m.resolve("banked.c:6"), {{P20, 0xC000}}) &&
                  starts_are(m.resolve("banked.c:8"), {{P20, 0xC003}}) &&
                  starts_are(m.resolve("banked.c:7"), {{P20, 0xC003}}) &&
                  starts_are(m.resolve("banked.c:9"), {{P20, 0xC01D}}) &&
                  is_at(m.lookup(P20, 0xC01D), "banked.c", 9) && f && stored_at(f->entry, P20, 0xC000) &&
                  nex.ok && m.has_identity() &&
                  m.verify_program(logical_junk, nex.reader()) == std::optional<bool>(true));
    }

    // ── Z88DOT-*: z88dk dot commands (GH #351, design 9.4 rule 5) ───────────────
    // Oracle: the committed dot / dotx / dotn fixtures (what zcc +zxn -subtype=dot|dotx|dotn
    // produced) and NextZXOS's measured behaviour (the README, "dot / dotx / dotn"): the
    // command runs from DivMMC RAM bank 2 at $2000, so its CODE group is DivMmcRam{2} whatever
    // `space_of` says is mapped at $2000 when the map is attached (the ROM, here).
    {
        const MemSpace D2 = MemSpace::divmmc_ram(2);
        // The ROM at $0000-$3FFF, as NextZXOS's prompt leaves it; the main-memory pages as the NEX loader.
        const SymbolSpaceOf prompt = nex_space;
        // All the dot.c lines of `m` (1..22) that resolve, each with its start spaces.
        const auto only_in = [](const SourceMap& m, const MemSpace& want) {
            bool any = false;
            for (int line = 1; line <= 22; ++line)
                for (const SourceAddress& a : m.resolve("dot.c:" + std::to_string(line)).starts) {
                    any = true;
                    if (!(a.space == want)) return false;
                }
            return any;
        };
        const auto text_of = [](const fs::path& p) {
            std::ifstream in(p, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        // `text` without the lines that start with `name`, plus `extra`.
        const auto edited = [](const std::string& text, const std::string& name, const std::string& extra) {
            std::string out;
            size_t at = 0;
            while (at < text.size()) {
                size_t nl = text.find('\n', at);
                if (nl == std::string::npos) nl = text.size();
                const std::string line = text.substr(at, nl - at);
                if (line.compare(0, name.size(), name) != 0 ||
                    (line.size() > name.size() && line[name.size()] != ' ' && line[name.size()] != '='))
                    out += line + "\n";
                at = nl + 1;
            }
            return out + extra;
        };

        {   // Z88DOT-01
            SourceMap m;
            const auto r = load(m, v24 / "sccz80-dot" / "prog.map", prompt);
            check("Z88DOT-01", "a 2.4 sccz80 dot command's map attaches with the ROM mapped at $2000: every dot.c line is in DivMMC RAM bank 2, "
                               "dot.c:14 starts at $2D62 there, and the same address in ROM 0 or a RAM page names nothing (it is not the map's memory)",
                  r && r.count > 0 && m.dot() && only_in(m, D2) && starts_are(m.resolve("dot.c:14"), {{D2, 0x2D62}}) &&
                      is_at(m.lookup(D2, 0x2D62), "dot.c", 14) && !m.lookup(MemSpace::rom(0), 0x2D62) &&
                      !m.lookup(MemSpace::page(4), 0x2D62) && !m.lookup(MemSpace::page(10), 0x2D62) &&
                      !m.lookup(MemSpace::cpu(), 0x2D62));
        }
        {   // Z88DOT-02
            SourceMap sc, sd, cc;
            const auto rs = load(sc, master / "sccz80-dot" / "prog.map", prompt);
            const auto rd = load(sd, master / "sdcc-dot" / "prog.map", prompt);
            const auto rc = load(cc, master / "80cc-dot" / "prog.map", prompt);
            check("Z88DOT-02", "the master sccz80, sdcc (TAB form) and 80cc dot maps: every dot.c line in DivMMC RAM bank 2; "
                               "sccz80 dot.c:17 at $2D92, sdcc dot.c:14 (owner of $2D43 with line 11) and 80cc dot.c:14 at $2D64",
                  rs && rd && rc && sc.dot() && sd.dot() && cc.dot() && only_in(sc, D2) && only_in(sd, D2) && only_in(cc, D2) &&
                      starts_are(sc.resolve("dot.c:17"), {{D2, 0x2D92}}) &&
                      is_at(sd.lookup(D2, 0x2D43), "dot.c", 14) && starts_are(sd.resolve("dot.c:14"), {{D2, 0x2D43}}) &&
                      starts_are(cc.resolve("dot.c:14"), {{D2, 0x2D64}}) && !sc.lookup(MemSpace::rom(0), 0x2D92));
        }
        {   // Z88DOT-03
            SourceMap m;
            const auto r = load(m, v24 / "sccz80-dotx" / "prog.map", prompt);
            // crt0's lines are in the CODE group at $2000-$267C; dot.c's are `code_compiler` of MAIN at $87xx.
            const auto main_lines = m.resolve("dot.c:14").starts;
            bool crt_in_div = false, crt_elsewhere = false;
            for (int page = 0; page < 224; ++page)
                crt_elsewhere = crt_elsewhere || m.lookup(MemSpace::page(static_cast<uint16_t>(page)), 0x2010) != nullptr;
            crt_in_div = m.lookup(D2, 0x2010) != nullptr || m.lookup(D2, 0x2000) != nullptr;
            check("Z88DOT-03", "a 2.4 sccz80 dotx: its crt lines ($2000-$267C, the CODE group) are DivMMC RAM bank 2; dot.c:14 (code_compiler in MAIN, $87EB) takes the space "
                               "the MMU maps ($8000-$9FFF is page 4 here), not the DivMMC bank",
                  r && m.dot() && crt_in_div && !crt_elsewhere && main_lines.size() == 1 && main_lines[0].space == P4 &&
                      main_lines[0].address >= 0x8000 && main_lines[0].address < 0xA000 &&
                      is_at(m.lookup(P4, main_lines[0].address), "dot.c", 14) && !m.lookup(D2, main_lines[0].address));
        }
        {   // Z88DOT-04
            const fs::path dotn = kFixtures / "v2.4-dotn" / "sccz80" / "prog.map";
            SourceMap m;
            const auto r = load(m, dotn, prompt);
            SymbolTable st;
            const int n = st.load_z88dk_map(dotn.string());
            st.bind(prompt);
            bool all_in_div = n > 0 && !st.symbols().empty();
            for (const auto& [addr, label] : st.symbols())
                all_in_div = all_in_div && label.space && *label.space == D2 && addr >= 0x2000 && addr < 0x4000;
            check("Z88DOT-04", "a 2.4 sccz80 dotn: the source half is refused (count -1, the store untouched) saying NextZXOS allocates its pages when it runs; "
                               "the symbol half keeps only the DOT part ($2000-$3FFF, DivMMC RAM bank 2) and skips the main-bank symbols (_main, _add3)",
                  !r && r.count == -1 && m.empty() && !m.dot() && r.error.find("NextZXOS allocates") != std::string::npos &&
                      r.error.find("dotx") != std::string::npos && all_in_div && !st.find_name("_main") && !st.find_name("_add3"));
        }
        {   // Z88DOT-05
            const std::string base = text_of(v24 / "sccz80-dot" / "prog.map");
            fs::copy_file(v24 / "sccz80-dot" / "prog_CODE.bin", dir / "z05_CODE.bin", fs::copy_options::overwrite_existing);
            SourceMap no_offset, other_offset, moved_head, same;
            const auto a = load(no_offset, synth("z05a.map", edited(base, "startupoffset", "")), prompt);
            const auto b = load(other_offset, synth("z05b.map", edited(base, "startupoffset", "startupoffset = $0400 ; const, local, , zxn_crt_asm_m4, , x:9\n")), prompt);
            const auto c = load(moved_head, synth("z05c.map", edited(base, "__CODE_head", "__CODE_head = $2100 ; const, public, def, , ,\n")), prompt);
            const auto d = load(same, synth("z05d.map", base), prompt);
            check("Z88DOT-05", "a dot command is told by the map, not the address: the same map without `startupoffset`, or with $0400, is refused as today "
                               "(its $2D4C is not in a RAM page); with $0100 and `__CODE_head` $2100 it is refused saying CODE must start at $2000; unchanged it loads",
                  !a && a.error.find("not in a RAM page") != std::string::npos && !b && b.error.find("not in a RAM page") != std::string::npos &&
                      !c && c.error.find("$2000") != std::string::npos && !no_offset.dot() && d && same.dot());
        }
        {   // Z88DOT-06
            SymbolTable st;
            const int n = st.load_z88dk_map((v24 / "sccz80-dot" / "prog.map").string());
            check("Z88DOT-06", "the symbol half of a dot command's map: _main ($2D5F) and _add3 ($2D4C) are labels of DivMMC RAM bank 2, shown as main and add3 there; "
                               "ROM 0 and a RAM page show nothing at $2D5F, and the name does not wait for bind()",
                  n > 0 && st.find_name("_main") == std::optional<SymbolNameKey>(SymbolNameKey{0x2D5F, std::optional<FetchSource>(D2), false}) &&
                      st.find_name("_add3") == std::optional<SymbolNameKey>(SymbolNameKey{0x2D4C, std::optional<FetchSource>(D2), false}) &&
                      st.lookup(D2, 0x2D5F) == std::optional<std::string>("main") &&
                      !st.lookup(MemSpace::rom(0), 0x2D5F) && !st.lookup(MemSpace::page(4), 0x2D5F) && !st.lookup(MemSpace::cpu(), 0x2D5F));
        }
        {   // Z88DOT-07
            SourceMap m, x;
            load(m, v24 / "sccz80-dot" / "prog.map", prompt);
            load(x, v24 / "sccz80-dotx" / "prog.map", prompt);
            const auto code = [](const fs::path& f) {
                std::ifstream in(f, std::ios::binary);
                return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            };
            const auto dot_bin = code(v24 / "sccz80-dot" / "prog_CODE.bin");
            const auto x_code = code(v24 / "sccz80-dotx" / "prog_CODE.bin");
            const auto x_main = code(v24 / "sccz80-dotx" / "prog_MAIN.bin");
            // Memory as NextZXOS leaves it: CODE at $2000 of bank 2, MAIN at $8000 of page 4.
            const auto reader = [&](const std::vector<uint8_t>& c, const std::vector<uint8_t>& mn) {
                return [&c, &mn](FetchSource sp, uint16_t a) -> std::optional<uint8_t> {
                    if (sp.kind == MemSpace::Kind::DivMmcRam && sp.index == 2 && a >= 0x2000 && a - 0x2000u < c.size() && a < 0x4000)
                        return c[a - 0x2000u];
                    if (sp == P4 && a >= 0x8000 && a - 0x8000u < mn.size()) return mn[a - 0x8000u];
                    return std::nullopt;
                };
            };
            const auto spans = m.verify_spans();
            const auto xspans = x.verify_spans();
            bool dot_units_ok = !spans.empty(), x_units_ok = false, x_div = false, x_p4 = false;
            for (const auto& sp : spans) dot_units_ok = dot_units_ok && sp.unit == D2 && sp.space == D2;
            for (const auto& sp : xspans) {
                x_div = x_div || (sp.unit == D2 && sp.space == D2);
                x_p4 = x_p4 || (sp.unit == P4 && sp.space == P4);
                x_units_ok = true;
            }
            const auto dummy = [](uint16_t) { return uint8_t(0); };
            std::vector<uint8_t> bad = dot_bin;
            for (const auto& sp : spans) { bad[sp.lo - 0x2000u] ^= 0xFF; break; }
            const auto good_dot = m.verify_unit(D2, dummy, reader(dot_bin, x_main));
            const auto bad_dot = m.verify_unit(D2, dummy, reader(bad, x_main));
            const auto good_x2 = x.verify_unit(D2, dummy, reader(x_code, x_main));
            const auto good_x4 = x.verify_unit(P4, dummy, reader(x_code, x_main));
            std::vector<uint8_t> bad_main = x_main;
            for (const auto& sp : xspans) if (sp.unit == P4) { bad_main[sp.lo - 0x8000u] ^= 0xFF; break; }
            const auto bad_x4 = x.verify_unit(P4, dummy, reader(x_code, bad_main));
            check("Z88DOT-07", "the identity of a dot command: one unit, DivMMC RAM bank 2, whose bytes are prog_CODE.bin at addr - $2000; a dotx has two units "
                               "(bank 2 from prog_CODE.bin, page 4 from prog_MAIN.bin at addr - $8000), each judged against its own memory",
                  m.has_identity() && dot_units_ok && x_units_ok && x_div && x_p4 && good_dot == std::optional<bool>(true) &&
                      bad_dot == std::optional<bool>(false) && good_x2 == std::optional<bool>(true) &&
                      good_x4 == std::optional<bool>(true) && bad_x4 == std::optional<bool>(false));
        }
        {   // Z88DOT-08 (GH #348): the listing route, the third consumer of the section rules
            SourceMap m;
            const auto r = load_z88dk_source_map(m, (v24 / "sdcc-dot" / "prog.map").string(), prompt, Z88dkListings::Beside);
            check("Z88DOT-08", "a 2.4 sdcc dot command has no C line in its map (-SO2 --c-code-in-asm): the dot.c lines come from dot.c.lis and are in DivMMC RAM bank 2",
                  r && r.listings > 0 && m.dot() && only_in(m, D2) && !m.resolve("dot.c:14").starts.empty());
        }
        {   // Z88DOT-09 (review R4): the dotn refusal, pure
            const std::function<std::optional<uint16_t>(int)> head_c000 = [](int) { return std::optional<uint16_t>(0xC000); };
            const Z88dkDot dn{Z88dkDot::Variant::Dotn}, dd{Z88dkDot::Variant::Dot}, dx{Z88dkDot::Variant::Dotx};
            const auto page_n = z88dk_section_space("PAGE_20", 0xC010, head_c000, &dn);
            const auto div_n  = z88dk_section_space("DIV_3", 0x2010, head_c000, &dn);
            const auto cdiv_n = z88dk_section_space("CODE_DIV_3", 0x2010, head_c000, &dn);
            const auto main_n = z88dk_section_space("code_compiler", 0x87D5, head_c000, &dn);
            const auto dot_n  = z88dk_section_space("code_compiler", 0x2D5F, head_c000, &dn);
            bool same = true;
            for (const Z88dkDot* v : {&dd, &dx}) {
                const auto pg  = z88dk_section_space("PAGE_20", 0xC010, head_c000, v);
                const auto dv  = z88dk_section_space("DIV_3", 0x2010, head_c000, v);
                const auto mn  = z88dk_section_space("code_compiler", 0x87D5, head_c000, v);
                same = same && pg.kind == Z88dkSpace::Kind::Paged && pg.space == P20 && pg.addr16 == 0xC010 &&
                       dv.kind == Z88dkSpace::Kind::Unpaged && mn.kind == Z88dkSpace::Kind::BindAtLoad && mn.addr16 == 0x87D5;
            }
            check("Z88DOT-09", "section_space with a dotn context: PAGE_20, DIV_3, CODE_DIV_3 and an unnumbered section outside $2000-$3FFF are Refused saying NextZXOS "
                               "allocates those pages; its $2000-$3FFF code is DivMmcRam{2}; with a dot or dotx context the same PAGE_20 stays page 20, DIV_3 Unpaged and the "
                               "main part rule 4",
                  page_n.kind == Z88dkSpace::Kind::Refused && div_n.kind == Z88dkSpace::Kind::Refused &&
                      cdiv_n.kind == Z88dkSpace::Kind::Refused && main_n.kind == Z88dkSpace::Kind::Refused &&
                      page_n.why.find("NextZXOS allocates") != std::string::npos && div_n.why.find("NextZXOS allocates") != std::string::npos &&
                      dot_n.kind == Z88dkSpace::Kind::Paged && dot_n.space == MemSpace::divmmc_ram(kNextZxosDotBank) && same);
        }
        {   // Z88DOT-10 (review R3): the module-symbol bound of an extent, inside DivMMC bank 2
            // dutil.c:7 is the last line of the map; with no `__code_compiler_tail`, a library
            // symbol of another module at $2DD1 is the only thing that ends its range there.
            const std::string base = text_of(v24 / "sccz80-dot" / "prog.map");
            SourceMap m;
            const auto r = load(m, synth("z10.map", edited(base, "__code_compiler_tail",
                "l_rev_lib = $2DD1 ; addr, public, , l_rev_lib_asm, code_l, l_rev.asm:1\n")), prompt);
            SourceMap plain;
            const auto rp = load(plain, synth("z10b.map", edited(base, "__code_compiler_tail", "")), prompt);
            check("Z88DOT-10", "an addr symbol of another module bounds a dot command's line range in DivMMC bank 2: dutil.c:7 ($2DC8) ends at the module's symbol at $2DD1, "
                               "and without that symbol it runs on (to the 1 KB cap)",
                  r && rp && is_at(m.lookup(D2, 0x2DD0), "dutil.c", 7) && !m.lookup(D2, 0x2DD1) &&
                      is_at(plain.lookup(D2, 0x2DD1), "dutil.c", 7));
        }
    }



    {
        // The BOM (owner decision 2026-11-01, GH #427): the symbol loaders skip one at the very
        // start of the file, as the SLD loader does, so the first symbol is not renamed by it.
        const std::string bom = "\xEF\xBB\xBF";
        const auto slurp = [](const fs::path& f) {
            std::ifstream in(f, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        const fs::path equ = fs::path(JNEXT_SJASMEQU_DIR) / "equlabels.sym";
        const std::string equ_body = slurp(equ);
        const std::string head = "first: EQU 0x00009000\n";
        const fs::path equ_bom = synth("bom.sym", bom + head + equ_body);
        const fs::path equ_head = synth("head.sym", head + equ_body);
        SymbolTable plain_equ, a, b;
        const int pe = plain_equ.load_sjasmplus_equ(equ_head.string());
        const int ea = a.load_sjasmplus_equ(equ_bom.string());
        const int eb = b.load_z88dk_or_sjasmplus(equ_bom.string());
        check("MAPBOM-01", "a BOM before a sjasmplus EQU file is skipped by load_sjasmplus_equ() and "
                           "load_z88dk_or_sjasmplus(): same count, and the first line's symbol keeps its name",
              pe > 0 && ea == pe && eb == pe && plain_equ.lookup_name("first") == 0x9000 &&
                  a.lookup_name("first") == 0x9000 && b.lookup_name("first") == 0x9000 &&
                  a.lookup_name("main") == 0x8000 && b.lookup_name("main") == 0x8000);
        const fs::path prog = v24 / "sccz80-debug" / "prog.map";
        const std::string first = "_first = $9000 ; addr, public, , m, code_compiler, x.c:1\n";
        const fs::path map_bom = synth("bom.map", bom + first + slurp(prog));
        SymbolTable c, d;
        const int ca = c.load_z88dk_map(map_bom.string());
        const int cb = d.load_z88dk_or_sjasmplus(map_bom.string());
        // The source adapter: the BOM sits on a line record, which it would otherwise not recognise.
        const std::string recs = rec("__C_LINE_3_a_2ec", 0x9000, "a_c", "code_compiler", "a.c::f::0::1:3") +
                                 rec("__C_LINE_4_a_2ec", 0x9004, "a_c", "code_compiler", "a.c::f::0::1:4");
        SourceMap sm, smp;
        const auto rb = load(sm, synth("bomrec.map", bom + recs));
        const auto rp = load(smp, synth("plainrec.map", recs));
        check("MAPBOM-02", "a BOM before a z88dk map is skipped by load_z88dk_map(), "
                           "load_z88dk_or_sjasmplus() and the source adapter: the first line's symbol keeps "
                           "its name, and the source half loads what the plain map does",
              ca == 20 && cb == 20 && c.lookup_name("_first") == 0x9000 && d.lookup_name("_first") == 0x9000 &&
                  c.lookup_name("_main") == 0x8C92 && rb && rp && rp.count == 2 && rb.count == rp.count && sm.size() == smp.size() &&
                  is_at(sm.lookup(P4, 0x9000), "a.c", 3));
        SymbolTable two;
        const fs::path map_two = synth("bom2.map", bom + bom + first + slurp(prog));
        const int n2 = two.load_z88dk_map(map_two.string());
        check("MAPBOM-03", "only the very first BOM is skipped: with two, the second stays in the first "
                           "line's symbol name, so \"_first\" is not found",
              n2 >= 19 && !two.lookup_name("_first") && two.lookup_name("_main") == 0x8C92);
    }


    {
        // GH #427 stage 2b round 2: the --map loader skips one BOM, not several; the Simple and
        // NextBuild formats skip the one.
        const std::string bom = "\xEF\xBB\xBF";
        const std::string first = "_first = $9000 ; addr, public, , m, code_compiler, x.c:1\n";
        SymbolTable one, two, equ_one, equ_two;
        const int n1 = one.load_z88dk_or_sjasmplus(synth("one.map", bom + first).string());
        const int n2 = two.load_z88dk_or_sjasmplus(synth("two.map", bom + bom + first).string());
        const int e1 = equ_one.load_z88dk_or_sjasmplus(synth("one.sym", bom + "first: EQU 0x00009000\n").string());
        const int e2 = equ_two.load_z88dk_or_sjasmplus(synth("two.sym", bom + bom + "first: EQU 0x00009000\n").string());
        check("MAPBOM-04", "load_z88dk_or_sjasmplus() skips only the very first BOM, for a z88dk map and for "
                           "an EQU file: with one BOM the first symbol resolves, with two it does not",
              n1 == 1 && one.lookup_name("_first") == 0x9000 && n2 >= 0 && !two.lookup_name("_first") &&
                  e1 == 1 && equ_one.lookup_name("first") == 0x9000 && e2 <= 1 && !equ_two.lookup_name("first"));
        SymbolTable simple;
        const int ns = simple.load_simple_map(synth("bom.simple", bom + "alpha = $9000\nbeta = $9100\n").string());
        check("MAPBOM-05", "load_simple_map() skips a UTF-8 BOM: the first line's symbol keeps its name",
              ns == 2 && simple.lookup_name("alpha") == 0x9000 && simple.lookup_name("beta") == 0x9100);
        // GH #477: a constant wider than 32 bits (z88dk writes `__clib_malloc_heap_size = $FFFFFFFFFFFFFFFF`) must be skipped, never throw: `unsigned long` is 32 bits on Windows
        // and std::stoul threw std::out_of_range there. Oracle: z88dk's own map line (nightly maps), and an address is 16 bits.
        SymbolTable wide;
        const int nw = wide.load_simple_map(synth("wide.simple", "first = $9000\n__clib_malloc_heap_size = $FFFFFFFFFFFFFFFF\nover32 = $100000000\nover64 = $1FFFFFFFFFFFFFFFF\nlast = $9100\n").string());
        check("Z88HEX-01", "load_simple_map() skips constants that do not fit 16 bits without throwing, whatever their width ($FFFFFFFFFFFFFFFF, $100000000, 17 digits) and keeps the symbols around them",
              nw == 2 && wide.lookup_name("first") == 0x9000 && wide.lookup_name("last") == 0x9100 && !wide.lookup_name("__clib_malloc_heap_size") && !wide.lookup_name("over32") && !wide.lookup_name("over64"));
        SymbolTable nb;
        const int nn = nb.load_nextbuild_memory(synth("bom.mem", bom + "ACB5: ._FillScreen\n7335: ._Other\n").string());
        check("MAPBOM-06", "load_nextbuild_memory() skips a UTF-8 BOM: the first line's label is not dropped",
              nn == 2 && nb.lookup(jnext::dbg::MemSpace::cpu(), 0xACB5) == std::optional<std::string>("FillScreen"));
    }

    // ── Z88CDB-01..26 (GH #433, E353-03): the C variable store the adapter fills from the
    // __CDBINFO__ records. Oracle: the committed fixtures (their README "C variables" section and
    // the maps themselves), the writers cited in doc/developer-guide 7.9, and the decoder's own
    // refusals (cdb_record.h). ──
    const fs::path sc_cvars = master / "sccz80-cvars" / "prog.map";
    const fs::path sd_cvars = master / "sdcc-cvars" / "prog.map";
    const fs::path cc_cvars = master / "80cc-cvars" / "prog.map";
    SourceMap m_sc, m_sd, m_cc;
    const auto r_sc = load(m_sc, sc_cvars);
    const auto r_sd = load(m_sd, sd_cvars);
    const auto r_cc = load(m_cc, cc_cvars);
    const VariableStore* v_sc = store_of(m_sc);
    const VariableStore* v_sd = store_of(m_sd);
    const VariableStore* v_cc = store_of(m_cc);
    {
        // every committed map whose source half loads, and so has a store
        int maps = 0, with_records = 0;
        bool all = true;
        for (const char* root : {"v2.4", "master"})
            for (const auto& e : fs::directory_iterator(kFixtures / root)) {
                const fs::path map_file = e.path() / "prog.map";
                if (!fs::exists(map_file)) continue;
                SourceMap m;
                const auto r = load(m, map_file);
                if (!r || r.count == 0) continue;
                const VariableStore* s = store_of(m);
                const size_t lines = cdb_line_count(map_file);
                ++maps;
                with_records += lines > 0;
                if (!s || !s->refused().empty() ||
                    s->functions().size() + s->variables().size() + s->types().size() + s->modules().size() +
                            s->refused().size() != lines) {
                    all = false;
                    std::printf("  Z88CDB-01: %s\n", map_file.string().c_str());
                }
            }
        check("Z88CDB-01", "every committed map whose source half loads has a store that accounts for each of its "
                           "__CDBINFO__ lines exactly once (functions + variables + types + modules + refused), "
                           "none refused; master/sccz80-cvars 53",
              all && maps >= 20 && with_records >= 10 && v_sc &&
                  v_sc->functions().size() + v_sc->variables().size() + v_sc->types().size() +
                          v_sc->modules().size() == 53 &&
                  cdb_line_count(sc_cvars) == 53);
    }
    {
        SourceMap m24;
        load(m24, v24 / "sccz80-cvars" / "prog.map");
        const VariableStore* s24 = store_of(m24);
        const auto kinds = [](const VariableStore* s, size_t f, size_t mo, size_t v, size_t t) {
            return s && s->functions().size() == f && s->modules().size() == mo && s->variables().size() == v &&
                   s->types().size() == t;
        };
        check("Z88CDB-02", "record kinds per build (fixture README): sccz80 v2.4 and master 3 F / 0 M / 45 S / 5 T, "
                           "master/sdcc-cvars 3/2/48/10, master/80cc-cvars 3/2/45/5",
              kinds(s24, 3, 0, 45, 5) && kinds(v_sc, 3, 0, 45, 5) && kinds(v_sd, 3, 2, 48, 10) &&
                  kinds(v_cc, 3, 2, 45, 5));
    }
    {
        const CVariable* a = v_sc ? var_named(*v_sc, "s_dup", "vars_c") : nullptr;
        const CVariable* b = v_sc ? var_named(*v_sc, "s_dup", "other_c") : nullptr;
        const CVariable* c = v_sd ? var_named(*v_sd, "s_dup", "vars_c") : nullptr;
        const CVariable* d = v_sd ? var_named(*v_sd, "s_dup", "other_c") : nullptr;
        const CVariable* e = v_cc ? var_named(*v_cc, "s_dup", "vars_c") : nullptr;
        const CVariable* f = v_cc ? var_named(*v_cc, "s_dup", "other_c") : nullptr;
        check("Z88CDB-03", "the two file statics named s_dup are told apart by module: sccz80 vars_c P4:$909F / "
                           "other_c P4:$9069, zsdcc $8F96 / $8F62, 80cc $901A / $8FE4 (GH #445: z88dk v2.4-2883 moved the 80cc ones)",
              a && b && c && d && e && f && stored_at(a->storage, P4, 0x909F) && stored_at(b->storage, P4, 0x9069) &&
                  stored_at(c->storage, P4, 0x8F96) && stored_at(d->storage, P4, 0x8F62) &&
                  stored_at(e->storage, P4, 0x901A) && stored_at(f->storage, P4, 0x8FE4));
    }
    {
        std::vector<std::string> lines = map_lines(sc_cvars);
        const size_t before = lines.size();
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [](const std::string& l) {
                                       return l.compare(0, 8, "_s_dup  ") == 0 && l.find(", other_c,") != std::string::npos;
                                   }),
                    lines.end());
        SourceMap m;
        const auto r = load(m, synth("z88cdb04.map", join_lines(lines)));
        const VariableStore* s = store_of(m);
        const CVariable* o = s ? var_named(*s, "s_dup", "other_c") : nullptr;
        const CVariable* w = s ? var_named(*s, "s_dup", "vars_c") : nullptr;
        check("Z88CDB-04", "other_c's _s_dup removed from the map: its S:F record is unresolved with a reason naming "
                           "other_c and is NOT vars_c's address; vars_c's own stays P4:$909F",
              r && before == lines.size() + 1 && o && !o->storage.resolved &&
                  o->storage.why.find("other_c") != std::string::npos && !stored_at(o->storage, P4, 0x909F) &&
                  w && stored_at(w->storage, P4, 0x909F));
    }
    {
        const CVariable* a = v_sc ? var_named(*v_sc, "st_sum_calls", "vars_c") : nullptr;
        const CVariable* b = v_cc ? var_named(*v_cc, "st_sum_calls", "vars_c") : nullptr;
        const auto good = [&](const CVariable* v, const VariableStore* s, uint16_t addr) {
            return v && v->function >= 0 && s->functions()[static_cast<size_t>(v->function)].name == "sum" &&
                   v->c_name == "calls" && v->storage.symbol == "_st_sum_calls" && stored_at(v->storage, P4, addr);
        };
        check("Z88CDB-05", "sccz80 and 80cc static local st_sum_calls: attributed to sum, C name calls, symbol "
                           "_st_sum_calls at P4:$90A0 (sccz80) / P4:$901B (80cc; GH #445: z88dk v2.4-2883 moved it)",
              good(a, v_sc, 0x90A0) && good(b, v_cc, 0x901B));
    }
    {
        const CVariable* v = v_sd ? var_named(*v_sd, "calls", "vars_c", "sum") : nullptr;
        check("Z88CDB-06", "zsdcc static local S:Lvars.sum$calls$1_0$10: symbol _sum_calls_10000_10 (level * 10000 + "
                           "sub, _block) at P4:$8F97, function sum, C name calls",
              v && v->record.scope == CdbScope::Local && v->c_name == "calls" &&
                  v->storage.symbol == "_sum_calls_10000_10" && stored_at(v->storage, P4, 0x8F97));
    }
    {
        bool ok = true;
        for (const fs::path& f : {v24 / "sccz80-cvars-banked" / "prog.map", master / "sccz80-cvars-banked" / "prog.map",
                                  master / "80cc-cvars-banked" / "prog.map"}) {
            SourceMap m;
            load(m, f);
            const VariableStore* s = store_of(m);
            const CVariable* t = s ? var_named(*s, "btable", "bk_c") : nullptr;
            const CVariable* d = s ? var_named(*s, "bdata", "bk_c") : nullptr;
            ok = ok && t && d && stored_at(t->storage, P20, 0xC000) && stored_at(d->storage, P20, 0xC004);
        }
        check("Z88CDB-07", "banked data (PAGE_20, 24-bit $14C000): btable is P20:$C000 and bdata P20:$C004, the page "
                           "kept, on v2.4 sccz80 and master sccz80 / 80cc",
              ok);
    }
    {
        SourceMap m;
        load(m, master / "sdcc-cvars-banked" / "prog.map");
        const VariableStore* s = store_of(m);
        const CVariable* d = s ? var_named(*s, "bdata", "bk_c") : nullptr;
        bool in_map = false;
        for (const auto& l : map_lines(master / "sdcc-cvars-banked" / "prog.map"))
            in_map = in_map || l.compare(0, 8, "_btable ") == 0;
        check("Z88CDB-08", "zsdcc banked: it writes no record for btable (a const), so there is no variable although "
                           "_btable is in the map; bdata is P20:$C028",
              in_map && s && !var_named(*s, "btable", "bk_c") && d && stored_at(d->storage, P20, 0xC028));
    }
    {
        const CFunction* sum = v_sc ? fn_named(*v_sc, "sum") : nullptr;
        const CFunction* mn = v_sc ? fn_named(*v_sc, "main") : nullptr;
        const CFunction* ot = v_sc ? fn_named(*v_sc, "other") : nullptr;
        check("Z88CDB-09", "master sccz80 functions: sum (vars_c, P4:$8CB9), main (vars_c, P4:$8D54), other in other_c "
                           "P4:$8E38 though F:G$other's own line says vars_c",
              sum && mn && ot && sum->module == "vars_c" && stored_at(sum->entry, P4, 0x8CB9) &&
                  mn->module == "vars_c" && stored_at(mn->entry, P4, 0x8D54) && ot->module == "other_c" &&
                  stored_at(ot->entry, P4, 0x8E38));
    }
    {
        SourceMap m;
        load(m, v24 / "sccz80-debug" / "prog.map");
        const VariableStore* s = store_of(m);
        const CFunction* f = s ? fn_named(*s, "util_twice") : nullptr;
        bool line_says_main = false;
        for (const auto& l : map_lines(v24 / "sccz80-debug" / "prog.map"))
            line_says_main = line_says_main || (is_cdb_line(l) && l.find("F_3aG_24util_5ftwice") != std::string::npos &&
                                                l.find(", main_c,") != std::string::npos);
        const CVariable* r = s ? var_named(*s, "r", "util_c", "util_twice") : nullptr;
        const CVariable* v = s ? var_named(*s, "v", "util_c", "util_twice") : nullptr;
        check("Z88CDB-10", "v2.4/sccz80-debug: F:G$util_twice (its line says main_c) is the function of util_c at "
                           "P4:$8D35, and r and v of util_c are its locals",
              f && line_says_main && f->module == "util_c" && stored_at(f->entry, P4, 0x8D35) && r && v &&
                  locals_of(*s, f) == 2);
    }
    {
        SourceMap m;
        load(m, master / "sdcc-debug" / "prog.map");
        const VariableStore* s = store_of(m);
        const CFunction* a = s ? fn_named(*s, "util_inc", "main_c") : nullptr;
        const CFunction* b = s ? fn_named(*s, "util_inc", "util_c") : nullptr;
        const CVariable* va = s ? var_named(*s, "v", "main_c", "util_inc") : nullptr;
        const CVariable* vb = s ? var_named(*s, "v", "util_c", "util_inc") : nullptr;
        check("Z88CDB-11", "master/sdcc-debug: a static inline function in a header is two functions, main_c P4:$8C48 "
                           "and util_c P4:$8CAC, each with the one local v of its own module",
              a && b && va && vb && stored_at(a->entry, P4, 0x8C48) && stored_at(b->entry, P4, 0x8CAC) &&
                  &s->functions()[static_cast<size_t>(va->function)] == a &&
                  &s->functions()[static_cast<size_t>(vb->function)] == b && locals_of(*s, a) == 1 &&
                  locals_of(*s, b) == 1);
    }
    {
        const auto locals = [](const VariableStore* s, const char* fn) {
            return s ? locals_of(*s, fn_named(*s, fn)) : size_t(999);
        };
        check("Z88CDB-12", "locals attach to their function: sum has 8 (a b p k t acc inner calls) on sccz80 and 80cc "
                           "and 10 on zsdcc (+ sloc0 sloc1); main 6 (lp arr big c i pp), zsdcc 7 (+ sloc0); other 2 (q res)",
              locals(v_sc, "sum") == 8 && locals(v_cc, "sum") == 8 && locals(v_sd, "sum") == 10 &&
                  locals(v_sc, "main") == 6 && locals(v_cc, "main") == 6 && locals(v_sd, "main") == 7 &&
                  locals(v_sc, "other") == 2 && locals(v_cc, "other") == 2 && locals(v_sd, "other") == 2 &&
                  v_sd && var_named(*v_sd, "sloc0", "vars_c", "sum") &&
                  var_named(*v_sd, "sloc0", "vars_c", "sum")->record.hidden == CdbHidden::Temporary);
    }
    check("Z88CDB-13", "scope table of sum, master sccz80 (address level/block): $8CB9 0/0, $8CBC 1/1, $8CC7 1/1, "
                       "$8CE8 1/1, $8D05 1/1, $8D11 2/2, $8D2B 2/2, $8D3D 2/2, $8D3E 1/2 (the level is the "
                       "compiler's, the block the last one opened)",
          v_sc && scopes_are(fn_named(*v_sc, "sum"),
                             {{0x8CB9, 0, 0, 0}, {0x8CBC, 1, 0, 1}, {0x8CC7, 1, 0, 1}, {0x8CE8, 1, 0, 1},
                              {0x8D05, 1, 0, 1}, {0x8D11, 2, 0, 2}, {0x8D2B, 2, 0, 2}, {0x8D3D, 2, 0, 2},
                              {0x8D3E, 1, 0, 2}},
                             P4));
    check("Z88CDB-14", "scope table of sum, zsdcc, level 10000 * L + S decomposed (level/sub/block): $8C48 0/0/10, "
                       "$8C52 1/0/10, $8C56 1/0/10, $8C6A 1/0/10, $8C6F 2/0/11, $8C77 3/0/12, $8C94 3/0/12, "
                       "$8C97 1/0/10, $8C9A 1/0/10",
          v_sd && scopes_are(fn_named(*v_sd, "sum"),
                             {{0x8C48, 0, 0, 10}, {0x8C52, 1, 0, 10}, {0x8C56, 1, 0, 10}, {0x8C6A, 1, 0, 10},
                              {0x8C6F, 2, 0, 11}, {0x8C77, 3, 0, 12}, {0x8C94, 3, 0, 12}, {0x8C97, 1, 0, 10},
                              {0x8C9A, 1, 0, 10}},
                             P4));
    check("Z88CDB-15", "scope table of sum, 80cc: $8C62 0/0, then $8C6F $8C76 $8C8B $8CAA $8CB4 $8CB8 $8CCA all 0/2 "
                       "(no level, no block structure; GH #445: z88dk v2.4-2883 moved the last five)",
          v_cc && scopes_are(fn_named(*v_cc, "sum"),
                             {{0x8C62, 0, 0, 0}, {0x8C6F, 0, 0, 2}, {0x8C76, 0, 0, 2}, {0x8C8B, 0, 0, 2},
                              {0x8CAA, 0, 0, 2}, {0x8CB4, 0, 0, 2}, {0x8CB8, 0, 0, 2}, {0x8CCA, 0, 0, 2}},
                             P4));
    {
        const auto first = [](const VariableStore* s) -> const jnext::dbg::CScope* {
            const CFunction* f = s ? fn_named(*s, "main") : nullptr;
            return f && !f->scopes.empty() ? &f->scopes.front() : nullptr;
        };
        const auto* a = first(v_sc);
        const auto* b = first(v_cc);
        const auto* c = first(v_sd);
        const auto has_at = [](const VariableStore* s, uint16_t addr) {
            for (const auto& sc : fn_named(*s, "sum")->scopes)
                if (sc.at.address == addr) return true;
            return false;
        };
        check("Z88CDB-16", "main's first entry is its own line 45 (0/3) on sccz80 P4:$8D54 and 80cc P4:$8CD9, not "
                           "line 44's sum::0::2 that shares the address, and sum's table has no entry there; zsdcc "
                           "P4:$8C9F is 1/0/14 (GH #445: 80cc moved from $8CD8 with z88dk v2.4-2883)",
              a && b && c && a->known && a->at.address == 0x8D54 && a->line == 45 && a->level == 0 && a->block == 3 &&
                  b->known && b->at.address == 0x8CD9 && b->line == 45 && b->level == 0 && b->block == 3 &&
                  c->known && c->at.address == 0x8C9F && c->level == 1 && c->sub == 0 && c->block == 14 &&
                  !has_at(v_sc, 0x8D54) && !has_at(v_cc, 0x8CD9));
    }
    {
        std::vector<std::string> lines = map_lines(sc_cvars);
        const size_t before = lines.size();
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [](const std::string& l) { return l.compare(0, 12, "__C_LINE_45_") == 0; }),
                    lines.end());
        SourceMap m;
        const auto r = load(m, synth("z88cdb17.map", join_lines(lines)));
        const VariableStore* s = store_of(m);
        const CFunction* f = s ? fn_named(*s, "main") : nullptr;
        check("Z88CDB-17", "without line 45's record, main's entry at $8D54 is line 44 whose scope names sum: it is "
                           "not known (not main's 0/2), the function at the address being main",
              r && before == lines.size() + 1 && f && !f->scopes.empty() && f->scopes.front().at.address == 0x8D54 &&
                  f->scopes.front().line == 44 && !f->scopes.front().known && f->scopes.front().level == 0 &&
                  f->scopes.front().block == 0);
    }
    {
        std::vector<std::string> lines = map_lines(sd_cvars);
        for (std::string& l : lines)
            l.erase(std::remove(l.begin(), l.end(), '\t'), l.end());
        SourceMap m;
        const auto r = load(m, synth("z88cdb18.map", join_lines(lines)));
        const VariableStore* s = store_of(m);
        bool same = s && v_sd && s->functions().size() == v_sd->functions().size();
        for (size_t i = 0; same && i < s->functions().size(); ++i) {
            const auto& a = s->functions()[i].scopes;
            const auto& b = v_sd->functions()[i].scopes;
            same = a.size() == b.size();
            for (size_t k = 0; same && k < a.size(); ++k)
                same = a[k].at.address == b[k].at.address && a[k].known == b[k].known && a[k].level == b[k].level &&
                       a[k].sub == b[k].sub && a[k].block == b[k].block;
        }
        check("Z88CDB-18", "zsdcc's location field without its TAB (the proposed z88dk #3222 A fix): the function "
                           "field x still marks it, and every scope table is the one of Z88CDB-14",
              r && same && scopes_are(fn_named(*s, "sum"),
                                      {{0x8C48, 0, 0, 10}, {0x8C52, 1, 0, 10}, {0x8C56, 1, 0, 10}, {0x8C6A, 1, 0, 10},
                                       {0x8C6F, 2, 0, 11}, {0x8C77, 3, 0, 12}, {0x8C94, 3, 0, 12}, {0x8C97, 1, 0, 10},
                                       {0x8C9A, 1, 0, 10}},
                                      P4));
    }
    {
        // zsdcc names the scope of a record after the temp file it compiled when TMPDIR is set (measured:
        // the name below); no committed map has one, so the rescoped copy is built here.
        const std::string temp = "_home_jorgegv_tmp_plan_gh353_scratch_out_m_sdcc_iy_tmp_tmpXXz0nVuH";
        std::vector<std::string> lines = map_lines(sd_cvars);
        int rescoped = 0;
        for (std::string& l : lines) {
            if (!is_cdb_line(l)) continue;
            const size_t eq = l.find(' ');
            const auto un = jnext::dbg::cdb_unescape(l.substr(11, eq - 11));
            if (!un.ok()) continue;
            std::string t = un.text;
            for (const char* from : {"S:Lvars.", "S:Fvars$", "T:Fvars$"}) {
                if (t.compare(0, std::string(from).size(), from) != 0) continue;
                t = std::string(from).substr(0, 3) + temp + std::string(from).substr(std::string(from).size() - 1) +
                    t.substr(std::string(from).size());
                ++rescoped;
                break;
            }
            l = "__CDBINFO__" + cdb_escape(t) + l.substr(eq);
        }
        SourceMap m;
        const auto r = load(m, synth("z88cdb19.map", join_lines(lines)));
        const VariableStore* s = store_of(m);
        bool same = s && v_sd && s->variables().size() == v_sd->variables().size();
        int differ = 0;
        for (const CVariable& v : same ? v_sd->variables() : std::vector<CVariable>{}) {
            const CVariable* w = nullptr;
            for (const CVariable& c : s->variables())
                if (c.record.var.name == v.record.var.name && c.module == v.module && c.record.function == v.record.function &&
                    c.record.var.level == v.record.var.level && c.record.var.block == v.record.var.block &&
                    c.record.scope == v.record.scope && c.record.var.space == v.record.var.space &&
                    c.record.var.nums == v.record.var.nums && c.record.var.regs == v.record.var.regs &&
                    c.record.hidden == v.record.hidden)
                    w = &c;
            if (!w || w->storage.resolved != v.storage.resolved || w->storage.symbol != v.storage.symbol ||
                !(w->storage.at.space == v.storage.at.space) || w->storage.at.address != v.storage.at.address ||
                (w->function < 0) != (v.function < 0)) {
                same = false;
                break;
            }
            if (w->function >= 0 && s->functions()[static_cast<size_t>(w->function)].name !=
                                        v_sd->functions()[static_cast<size_t>(v.function)].name)
                same = false;
            differ += w->record.scope_name != v.record.scope_name;
        }
        check("Z88CDB-19", "zsdcc records rescoped to a temp-file name (S:Lvars. / S:Fvars$ / T:Fvars$): every variable "
                           "keeps its storage and its function, only scope_name differs (the join is by module)",
              r && rescoped > 20 && same && differ > 20 && s->types().size() == v_sd->types().size());
    }
    {
        std::vector<std::string> sc_lines = map_lines(sc_cvars);
        std::vector<std::string> sd_lines = map_lines(sd_cvars);
        const auto reversed = [](std::vector<std::string> l) {
            std::reverse(l.begin(), l.end());
            return l;
        };
        const auto rotated = [](std::vector<std::string> l) {
            std::rotate(l.begin(), l.begin() + static_cast<std::ptrdiff_t>(l.size() / 2), l.end());
            return l;
        };
        SourceMap a, b, c, d, e, f;
        load(a, synth("z88cdb20a.map", join_lines(reversed(sc_lines))));
        load(b, synth("z88cdb20b.map", join_lines(rotated(sc_lines))));
        load(c, synth("z88cdb20c.map", join_lines(reversed(sd_lines))));
        load(d, synth("z88cdb20d.map", join_lines(rotated(sd_lines))));
        const std::string ref_sc = v_sc ? store_dump(*v_sc) : "";
        const std::string ref_sd = v_sd ? store_dump(*v_sd) : "";
        check("Z88CDB-20", "the store does not depend on the order of the map's lines: master sccz80-cvars and "
                           "sdcc-cvars with their lines reversed, and rotated by half, dump identically",
              !ref_sc.empty() && !ref_sd.empty() && store_of(a) && store_of(b) && store_of(c) && store_of(d) &&
                  store_dump(*store_of(a)) == ref_sc && store_dump(*store_of(b)) == ref_sc &&
                  store_dump(*store_of(c)) == ref_sd && store_dump(*store_of(d)) == ref_sd);
    }
    {
        SourceMap sd, sc;
        load(sd, master / "sdcc-extern" / "prog.map");
        load(sc, master / "sccz80-extern" / "prog.map");
        const VariableStore* a = store_of(sd);
        const VariableStore* b = store_of(sc);
        const CVariable* ga = a ? var_named(*a, "g", "ext_use_c") : nullptr;
        const CVariable* gb = b ? var_named(*b, "g", "ext_def_c") : nullptr;
        check("Z88CDB-21", "S:G$g joins the PUBLIC _g of ext_def_c, whatever module its record's line names: zsdcc's "
                           "line says ext_use_c (the declarer), sccz80's ext_def_c; P4:$8E46 / P4:$8E63",
              ga && gb && ga->storage.symbol == "_g" && gb->storage.symbol == "_g" &&
                  stored_at(ga->storage, P4, 0x8E46) && stored_at(gb->storage, P4, 0x8E63));
    }
    {
        std::vector<std::string> lines = map_lines(sc_cvars);
        const std::string bad = "__CDBINFO__S_3aG_24bad_zzq = $0001 ; const, public, , vars_c, code_compiler, vars.c:1";
        lines.push_back(bad);
        SourceMap m;
        const auto r = load(m, synth("z88cdb22.map", join_lines(lines)));
        const VariableStore* s = store_of(m);
        check("Z88CDB-22", "a __CDBINFO__ line with a bad escape is kept in refused() with the decoder's reason, "
                           "never dropped, and the source half loads the same ranges as without it",
              r && r.count == r_sc.count && s && s->refused().size() == 1 &&
                  s->refused()[0].compare(0, 27, "__CDBINFO__S_3aG_24bad_zzq:") == 0 &&
                  s->refused()[0].size() > 30 && s->variables().size() == 45);
    }
    {
        SourceMap dot, dotx;
        load(dot, master / "sccz80-dot" / "prog.map");
        load(dotx, master / "sccz80-dotx" / "prog.map");
        const VariableStore* a = store_of(dot);
        const VariableStore* b = store_of(dotx);
        const CVariable* ta = a ? var_named(*a, "total", "dot_c") : nullptr;
        const CVariable* tb = b ? var_named(*b, "total", "dot_c") : nullptr;
        check("Z88CDB-23", "dot commands: total is DivMmcRam{2}:$3253 (rule 5) in master/sccz80-dot and P4:$8C3A "
                           "(rule 4, main RAM) in master/sccz80-dotx (GH #445: z88dk v2.4-2883 moved both)",
              ta && tb && stored_at(ta->storage, MemSpace::divmmc_ram(2), 0x3253) && stored_at(tb->storage, P4, 0x8C3A));
    }
    {
        SourceMap none, beside;
        load(none, master / "sccz80-cvars" / "prog.map");
        const auto rb = load_z88dk_source_map(beside, sc_cvars.string(), nex_space, Z88dkListings::Beside);
        check("Z88CDB-24", "with the listings read (listings > 0) the store is the same, scope tables included: "
                           "a listing's C_LINE keeps only its file, so the scope comes from the map's records",
              rb && rb.listings > 0 && store_of(none) && store_of(beside) &&
                  store_dump(*store_of(beside)) == store_dump(*store_of(none)));
    }
    {
        SourceMap m;
        const auto r = load_z88dk_source_map(m, (v24 / "sdcc-SO2" / "prog.map").string(), nex_space, Z88dkListings::Beside);
        const VariableStore* s = store_of(m);
        const fs::path sld = synth("z88cdb25.sld", "|SLD.data.version|1\nmain.bas|10||0|-1|-1|Z|pages.size:8192,pages.count:224,"
                                                  "slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
                                                  "main.bas|10||0|4|32768|T|\n");
        SourceMap sm;
        const auto rs = load_sld(sm, sld.string());
        check("Z88CDB-25", "v2.4/sdcc-SO2 (no record; lines from the listings only) gets an EMPTY store, so \"no "
                           "records\" is told from \"no z88dk map\"; an SLD's source map has none",
              r && r.listings > 0 && s && s->functions().empty() && s->variables().empty() && s->types().empty() &&
                  s->modules().empty() && s->refused().empty() && rs && sm.size() > 0 && !sm.variables() &&
                  !SourceMap().variables());
    }
    {
        size_t enumerators = 0;
        bool untouched = v_sc != nullptr;
        for (const CVariable& v : v_sc ? v_sc->variables() : std::vector<CVariable>{})
            if (v.record.hidden == CdbHidden::EmptyType) {
                ++enumerators;
                untouched = untouched && !v.storage.resolved && v.storage.symbol.empty() && v.storage.why.empty();
            }
        bool twin = false;
        for (const CVariable& v : v_sc ? v_sc->variables() : std::vector<CVariable>{})
            twin = twin || (v.record.var.name == "g_str" && v.record.hidden == CdbHidden::IncompleteArray);
        const CVariable* zmain = v_sd ? var_named(*v_sd, "main", "vars_c") : nullptr;
        check("Z88CDB-26", "hidden records are kept (views hide, the store keeps) and never read as memory: the sccz80 "
                           "enumerators ({0}, storage untouched), the {-1} twin of g_str (hidden IncompleteArray), "
                           "zsdcc's C-space S:G$main (hidden Function)",
              enumerators > 0 && untouched && twin && zmain && zmain->record.hidden == CdbHidden::Function &&
                  !zmain->storage.resolved && zmain->storage.symbol.empty());
    }

    // ── Z88CDB-27..40 (GH #433 review 1): the branches the first 26 rows left one-sided. Every
    // synthetic map is a committed one with lines added or edited in memory (never a fixture edit).
    const auto sym_line = [](const char* name, const char* value, const char* scope, const char* module,
                             const char* section, const char* loc = "vars.c:1") {
        return std::string(name) + " = $" + value + " ; addr, " + scope + ", , " + module + ", " + section + ", " + loc;
    };
    const auto cdb_line = [](const std::string& text, const char* module) {
        return "__CDBINFO__" + cdb_escape(text) + " = $0001 ; const, public, , " + module + ", code_compiler, x.c:1";
    };
    const auto plus = [](std::vector<std::string> lines, std::initializer_list<std::string> extra) {
        for (const auto& e : extra) lines.push_back(e);
        return lines;
    };
    {
        SourceMap m;
        const auto r = load(m, synth("z88cdb27.map", join_lines(plus(map_lines(sc_cvars),
            {sym_line("_s_dup", "90B0", "local", "vars_c", "bss_compiler")}))));
        const VariableStore* s = store_of(m);
        const CVariable* a = s ? var_named(*s, "s_dup", "vars_c") : nullptr;
        const CVariable* b = s ? var_named(*s, "s_dup", "other_c") : nullptr;
        check("Z88CDB-27", "an S:F symbol defined twice in its module is ambiguous, never first-wins: unresolved, "
                           "the reason says \"more than once\", neither $909F nor $90B0 is taken; other_c's is untouched",
              r && a && b && !a->storage.resolved && a->storage.symbol.empty() &&
                  a->storage.why.find("more than once") != std::string::npos && stored_at(b->storage, P4, 0x9069));
    }
    {
        SourceMap m;
        const auto r = load(m, synth("z88cdb28.map", join_lines(plus(map_lines(master / "sccz80-extern" / "prog.map"),
            {sym_line("_g", "8E70", "public", "ext_use_c", "bss_compiler", "ext_use.c:1")}))));
        const VariableStore* s = store_of(m);
        const CVariable* g = s ? var_named(*s, "g", "ext_def_c") : nullptr;
        check("Z88CDB-28", "two PUBLIC _g in two modules: S:G$g is ambiguous (\"more than once\"), unresolved, neither address taken",
              r && g && !g->storage.resolved && g->storage.symbol.empty() &&
                  g->storage.why.find("more than once") != std::string::npos);
    }
    {
        SourceMap m;
        const auto r = load(m, synth("z88cdb29.map", join_lines(plus(map_lines(master / "sccz80-extern" / "prog.map"),
            {sym_line("_g", "8E70", "local", "ext_use_c", "bss_compiler", "ext_use.c:1")}))));
        const VariableStore* s = store_of(m);
        const CVariable* g = s ? var_named(*s, "g", "ext_def_c") : nullptr;
        check("Z88CDB-29", "a LOCAL _g in another module does not count for S:G$g: the PUBLIC one is joined, P4:$8E63",
              r && g && g->storage.symbol == "_g" && stored_at(g->storage, P4, 0x8E63));
    }
    {
        SourceMap m;
        const auto r = load(m, synth("z88cdb30.map", join_lines(plus(map_lines(sc_cvars),
            {cdb_line("F:Fvars.c$sum$0_0$0({0}DF,SC:U),C,0,0", "vars_c")}))));
        const VariableStore* s = store_of(m);
        const CVariable* a = s ? var_named(*s, "a", "vars_c") : nullptr;
        const CVariable* lp = s ? var_named(*s, "lp", "vars_c") : nullptr;
        check("Z88CDB-30", "two F records named sum in one module: the function of a local of sum is ambiguous, so "
                           "function == -1 (not the first); main's locals still attach; each F record joins _sum",
              r && s && s->functions().size() == 4 && !fn_named(*s, "sum") && a && a->function == -1 && lp &&
                  lp->function >= 0 && s->functions()[static_cast<size_t>(lp->function)].name == "main");
    }
    const auto a_ab = synth("z88cdb31.map", join_lines(plus(map_lines(sc_cvars), {
        cdb_line("F:Fvars.c$a$0_0$0({0}DF,SI:S),C,0,0", "vars_c"),
        cdb_line("F:Fvars.c$a_b$0_0$0({0}DF,SI:S),C,0,0", "vars_c"),
        cdb_line("S:Fvars.c$st_a_b_x$1_0$1({2}SI:S),E,0,0", "vars_c"),
        cdb_line("S:Fvars.c$st_a_y$0_0$0({2}SI:S),E,0,0", "vars_c"),
        cdb_line("S:Fvars.c$st_a_z$1_0$1({2}SI:S),E,0,0", "vars_c"),
        sym_line("_a", "9120", "local", "vars_c", "code_compiler"),
        sym_line("_a_b", "9130", "local", "vars_c", "code_compiler"),
        sym_line("_st_a_b_x", "9140", "local", "vars_c", "bss_compiler")})));
    SourceMap m_ab;
    const auto r_ab = load(m_ab, a_ab);
    const VariableStore* v_ab = store_of(m_ab);
    {
        const CVariable* v = v_ab ? var_named(*v_ab, "st_a_b_x", "vars_c") : nullptr;
        check("Z88CDB-31", "functions a and a_b in one module: st_a_b_x has two candidate functions, so it is "
                           "unattributed (function -1, the raw C name st_a_b_x) and still resolves to P4:$9140",
              r_ab && v && v->function == -1 && v->c_name == "st_a_b_x" && v->storage.symbol == "_st_a_b_x" &&
                  stored_at(v->storage, P4, 0x9140));
    }
    {
        const CVariable* y = v_ab ? var_named(*v_ab, "st_a_y", "vars_c") : nullptr;
        const CVariable* z = v_ab ? var_named(*v_ab, "st_a_z", "vars_c") : nullptr;
        check("Z88CDB-32", "the sccz80 st_<fn>_ attribution needs level > 0: a level-0 file static st_a_y is NOT a "
                           "static local of a (function -1, raw name); the level-1 st_a_z is (function a, C name z)",
              y && z && y->function == -1 && y->c_name == "st_a_y" && z->function >= 0 &&
                  v_ab->functions()[static_cast<size_t>(z->function)].name == "a" && z->c_name == "z");
    }
    {
        std::vector<std::string> lines = map_lines(master / "sccz80-extern" / "prog.map");
        int edited = 0;
        for (auto& l : lines)
            if (l.compare(0, 3, "_g ") == 0) {
                const size_t at = l.find("$8E63");
                if (at != std::string::npos) { l.replace(at, 5, "$1000"); ++edited; }
            }
        SourceMap m;
        const auto r = load(m, synth("z88cdb33.map", join_lines(lines)));
        const VariableStore* s = store_of(m);
        const CVariable* g = s ? var_named(*s, "g", "ext_def_c") : nullptr;
        check("Z88CDB-33", "a rule-4 variable whose address nex_space() maps to ROM ($1000) is unresolved, the reason "
                           "says \"not in a RAM page\", and the symbol is still named",
              r && edited == 1 && g && !g->storage.resolved && g->storage.symbol == "_g" &&
                  g->storage.why.find("not in a RAM page") != std::string::npos);
    }
    {
        const std::string text = "S:G$x";
        const jnext::dbg::CdbDecoded want = jnext::dbg::cdb_decode(text);
        const std::string line = cdb_line(text, "vars_c");
        SourceMap m;
        const auto r = load(m, synth("z88cdb34.map", join_lines(plus(map_lines(sc_cvars), {line}))));
        const VariableStore* s = store_of(m);
        const std::string symbol = line.substr(0, line.find(' '));
        check("Z88CDB-34", "a record that unescapes cleanly but that cdb_decode() refuses is kept in refused() as "
                           "\"<symbol>: <the decoder's reason>\", and every CDB line is still in exactly one vector",
              r && !want.ok() && s && s->refused().size() == 1 && s->refused()[0] == symbol + ": " + want.error &&
                  s->functions().size() + s->variables().size() + s->types().size() + s->modules().size() +
                          s->refused().size() == cdb_line_count(sc_cvars) + 1);
    }
    {
        const std::string bad_unescape = "__CDBINFO__S_3aG_24bad_zzq = $0001 ; const, public, , vars_c, code_compiler, vars.c:1";
        const std::string bad_decode = cdb_line("S:G$x", "vars_c");
        const std::vector<std::string> base = plus(map_lines(sc_cvars), {bad_unescape, bad_decode});
        std::vector<std::string> rev = base;
        std::reverse(rev.begin(), rev.end());
        std::vector<std::string> rot = base;
        std::rotate(rot.begin(), rot.begin() + static_cast<std::ptrdiff_t>(rot.size() / 2), rot.end());
        SourceMap a, b, c;
        load(a, synth("z88cdb35a.map", join_lines(base)));
        load(b, synth("z88cdb35b.map", join_lines(rev)));
        load(c, synth("z88cdb35c.map", join_lines(rot)));
        check("Z88CDB-35", "two refused records: refused() is sorted, so the store is the same for the map's lines "
                           "in file order, reversed and rotated",
              store_of(a) && store_of(b) && store_of(c) && store_of(a)->refused().size() == 2 &&
                  std::is_sorted(store_of(b)->refused().begin(), store_of(b)->refused().end()) &&
                  store_dump(*store_of(a)) == store_dump(*store_of(b)) &&
                  store_dump(*store_of(a)) == store_dump(*store_of(c)));
    }
    const auto x_map = synth("z88cdb36.map", join_lines(plus(map_lines(sc_cvars), {
        cdb_line("F:Fvars.c$x$0_0$0({0}DF,SI:S),C,0,0", "vars_c"),
        sym_line("_x", "9100", "local", "vars_c", "code_compiler", "vars.c::x::0::0:70"),
        "__C_LINE_70_vars_2ec_3a_3ax_3a_3a1_3a_3a2 = $9100 ; addr, local, , vars_c, code_compiler, vars.c::x::1::2:70",
        "__C_LINE_71_vars_2ec_3a_3ax_3a_3aa_3a_3ab = $9110 ; addr, local, , vars_c, code_compiler, vars.c::x::a::b:71"})));
    SourceMap m_x;
    const auto r_x = load(m_x, x_map);
    const CFunction* f_x = store_of(m_x) ? fn_named(*store_of(m_x), "x") : nullptr;
    check("Z88CDB-36", "an sccz80 function literally named x with scope x::1::2 and no TAB is NOT zsdcc's form: "
                       "level 1, sub 0, block 2 (not 0/1/2), known",
          r_x && f_x && f_x->scopes.size() == 2 && f_x->scopes[0].known && f_x->scopes[0].at.address == 0x9100 &&
              f_x->scopes[0].level == 1 && f_x->scopes[0].sub == 0 && f_x->scopes[0].block == 2);
    check("Z88CDB-37", "a location whose level and block are not numbers (vars.c::x::a::b:71) has no scope: the "
                       "entry is unknown, 0/0/0",
          f_x && f_x->scopes.size() == 2 && f_x->scopes[1].at.address == 0x9110 && f_x->scopes[1].line == 71 &&
              !f_x->scopes[1].known && f_x->scopes[1].level == 0 && f_x->scopes[1].block == 0);
    {
        std::vector<std::string> lines = map_lines(sd_cvars);
        int cdb = 0, sym = 0;
        for (auto& l : lines) {
            if (is_cdb_line(l)) {
                const size_t eq = l.find(' ');
                const auto un = jnext::dbg::cdb_unescape(l.substr(11, eq - 11));
                if (un.ok() && un.text.compare(0, 24, "S:Lvars.sum$calls$1_0$10") == 0) {
                    std::string t = un.text;
                    t.replace(t.find("$1_0$10"), 7, "$1_2$10");   // level 1, sub 0 -> sub 2
                    l = "__CDBINFO__" + cdb_escape(t) + l.substr(eq);
                    ++cdb;
                }
            } else if (l.compare(0, 20, "_sum_calls_10000_10 ") == 0) {
                l.replace(0, 19, "_sum_calls_10002_10");
                ++sym;
            }
        }
        SourceMap m;
        const auto r = load(m, synth("z88cdb38.map", join_lines(lines)));
        const VariableStore* s = store_of(m);
        const CVariable* v = s ? var_named(*s, "calls", "vars_c", "sum") : nullptr;
        check("Z88CDB-38", "a zsdcc static local with sub != 0 (S:L...$calls$1_2$10): its symbol is _sum_calls_10002_10 "
                           "(level * 10000 + sub, as SDCC allocLocal() writes it), P4:$8F97",
              r && cdb == 1 && sym == 1 && v && v->record.var.sub == 2 && v->storage.symbol == "_sum_calls_10002_10" &&
                  stored_at(v->storage, P4, 0x8F97));
    }
    {
        int checked = 0;
        bool all = true;
        for (const char* map : {"v2.4/sccz80-fc", "master/sccz80-fc", "master/80cc-fc"}) {
            SourceMap m;
            load(m, kFixtures / map / "prog.map");
            const VariableStore* s = store_of(m);
            const CFunction* f = s ? fn_named(*s, "fcallee") : nullptr;
            bool found = false;
            for (const auto& c : f ? f->scopes : std::vector<jnext::dbg::CScope>{})
                if (c.at.address == f->entry.at.address) {
                    found = true;
                    all = all && c.line == 4 && !c.known;
                }
            checked += found;
        }
        check("Z88CDB-39", "one-line functions: at fcallee's entry two line-4 records (fcallee's and the previous "
                           "function's) disagree, so the entry is unknown, on all three fc maps",
              checked == 3 && all);
    }
    {
        // The golden is the output of test/fixtures/z88dk/derive-scopes.py, which reads only the RAW map
        // lines (no jnext code); its header holds every rule and the command that prints one map's block.
        // Re-derive it with that script when a fixture is regenerated or the equal-line rule changes;
        // never paste this suite's own output. The sum tables of Z88CDB-13..15 are the hand-checked subset.
        static const struct { const char* map; const char* golden; } kScopeGolden[] = {
        {"master/sccz80-cvars", R"GOLD(main vars_c 8D54
  8D54 L45 K 0/0/3
  8D57 L49 K 1/0/4
  8D61 L52 K 1/0/4
  8D7F L53 K 1/0/4
  8D85 L54 K 1/0/4
  8D94 L55 K 1/0/4
  8DAC L56 K 3/0/5
  8DE1 L57 K 3/0/5
  8DE4 L58 K 1/0/5
  8DEA L59 K 1/0/5
  8DF9 L60 K 1/0/5
  8E0B L61 K 1/0/5
  8E19 L62 K 1/0/5
  8E1F L64 K 2/0/6
  8E27 L65 K 2/0/6
  8E2A L66 K 1/0/6
other other_c 8E38
  8E38 L6 K 0/0/0
  8E3B L8 K 1/0/1
  8E49 L9 K 1/0/1
sum vars_c 8CB9
  8CB9 L29 K 0/0/0
  8CBC L33 K 1/0/1
  8CC7 L34 K 1/0/1
  8CE8 L35 K 1/0/1
  8D05 L36 K 1/0/1
  8D11 L38 K 2/0/2
  8D2B L39 K 2/0/2
  8D3D L40 K 2/0/2
  8D3E L41 K 1/0/2
)GOLD"},
        {"master/sdcc-cvars", R"GOLD(main vars_c 8C9F
  8C9F L44 K 1/0/14
  8CAC L52 K 1/0/14
  8CC0 L55 K 3/0/16
  8CC4 L56 K 3/0/16
  8CF0 L55 K 2/0/15
  8CFA L58 K 1/0/14
  8D00 L59 K 1/0/14
  8D0C L60 K 1/0/14
  8D18 L61 K 1/0/14
  8D24 L62 K 1/0/14
  8D2A L64 K 2/0/17
  8D31 L67 K 1/0/14
other other_c 8D33
  8D33 L5 K 0/0/8
  8D3B L8 K 1/0/8
  8D49 L10 K 1/0/8
sum vars_c 8C48
  8C48 L28 K 0/0/10
  8C52 L33 K 1/0/10
  8C56 L34 K 1/0/10
  8C6A L35 K 1/0/10
  8C6F L36 K 2/0/11
  8C77 L38 K 3/0/12
  8C94 L39 K 3/0/12
  8C97 L41 K 1/0/10
  8C9A L42 K 1/0/10
)GOLD"},
        {"master/80cc-cvars", R"GOLD(main vars_c 8CD9
  8CD9 L45 K 0/0/3
  8CE6 L49 K 0/0/6
  8CEB L52 K 0/0/6
  8D03 L53 K 0/0/6
  8D0D L54 K 0/0/6
  8D1F L55 K 0/0/6
  8D23 L56 K 0/0/6
  8D6A L57 K 0/0/6
  8D6D L58 K 0/0/6
  8D73 L59 K 0/0/6
  8D86 L60 K 0/0/6
  8D97 L61 K 0/0/6
  8DA0 L62 K 0/0/6
  8DA6 L64 K 0/0/6
  8DAD L65 K 0/0/6
other other_c 8DB0
  8DB0 L6 K 0/0/0
  8DB4 L8 K 0/0/1
  8DC6 L9 K 0/0/1
sum vars_c 8C62
  8C62 L29 K 0/0/0
  8C6F L33 K 0/0/2
  8C76 L34 K 0/0/2
  8C8B L35 K 0/0/2
  8CAA L36 K 0/0/2
  8CB4 L38 K 0/0/2
  8CB8 L39 K 0/0/2
  8CCA L41 K 0/0/2
)GOLD"},
        {"v2.4/sccz80-cvars", R"GOLD(main vars_c 8D54
  8D54 L45 K 0/0/3
  8D57 L49 K 1/0/4
  8D61 L52 K 1/0/4
  8D7F L53 K 1/0/4
  8D85 L54 K 1/0/4
  8D94 L55 K 1/0/4
  8DAC L56 K 3/0/5
  8DBC L57 K 3/0/5
  8DE2 L58 K 1/0/5
  8DE8 L59 K 1/0/5
  8DF7 L60 K 1/0/5
  8E09 L62 K 1/0/5
  8E1D L64 K 2/0/6
  8E25 L65 K 2/0/6
  8E28 L66 K 1/0/6
other other_c 8E36
  8E36 L6 K 0/0/0
  8E39 L8 K 1/0/1
  8E47 L9 K 1/0/1
sum vars_c 8CB9
  8CB9 L29 K 0/0/0
  8CBC L33 K 1/0/1
  8CC7 L34 K 1/0/1
  8CE8 L35 K 1/0/1
  8D05 L36 K 1/0/1
  8D11 L38 K 2/0/2
  8D2B L39 K 2/0/2
  8D3D L40 K 2/0/2
  8D3E L41 K 1/0/2
)GOLD"},
        {"v2.4/sccz80-debug", R"GOLD(main main_c 8C92
  8C92 L15 K 0/0/2
  8C95 L17 K 1/0/3
  8C9D L18 K 1/0/3
  8CB5 L19 K 3/0/4
  8CBE L20 K 3/0/4
  8CE9 L21 K 3/0/4
  8CEC L22 K 1/0/4
  8D0F L23 K 1/0/4
  8D12 L25 K 1/0/4
  8D22 L26 K 2/0/5
  8D2A L27 K 2/0/5
  8D2D L28 K 1/0/5
square main_c 8C78
  8C78 L10 K 0/0/0
  8C7B L11 K 1/0/1
util_twice util_c 8D35
  8D35 L4 K 0/0/0
  8D38 L6 K 1/0/1
  8D50 L7 K 1/0/1
  8D5C L8 K 1/0/1
)GOLD"},
        {"master/sdcc-debug", R"GOLD(main main_c 8C6B
  8C6B L17 K 1/0/13
  8C6F L18 K 3/0/15
  8C71 L19 K 3/0/15
  8C84 L20 K 3/0/15
  8C89 L18 K 2/0/14
  8C8F L22 K 2/0/16
  8C96 L23 K 1/0/13
  8C99 L24 K 1/0/13
  8CA6 L26 K 2/0/17
  8CAA L29 K 1/0/13
square main_c 8C57
  8C57 L9 K 1/0/11
  8C5F L11 K 1/0/11
  8C68 L12 K 1/0/11
util_inc main_c 8C48
  8C48 L7 K 0/0/8
  8C56 L7 K 1/0/8
util_inc util_c 8CAC
  8CAC L9 K 0/0/8
  8CBA L9 K 1/0/8
util_twice util_c 8CBB
  8CBB L3 K 1/0/10
  8CC3 L6 K 1/0/10
  8CC8 L8 K 1/0/10
  8CC9 L9 K 1/0/10
)GOLD"},
        {"master/sccz80-debug", R"GOLD(main main_c 8C7D
  8C7D L15 K 0/0/2
  8C80 L17 K 1/0/3
  8C88 L18 K 1/0/3
  8CA0 L19 K 3/0/4
  8CBA L20 K 3/0/4
  8CD6 L21 K 3/0/4
  8CD9 L22 K 1/0/4
  8CFC L23 K 1/0/4
  8CFF L24 K 1/0/4
  8D0F L26 K 2/0/5
  8D17 L27 K 2/0/5
  8D1A L28 K 1/0/5
square main_c 8C62
  8C62 L10 K 0/0/0
  8C65 L11 K 1/0/1
util_twice util_c 8D22
  8D22 L4 K 0/0/0
  8D25 L6 K 1/0/1
  8D3F L7 K 1/0/1
  8D4D L8 K 1/0/1
)GOLD"},
        {"master/80cc-debug", R"GOLD(main main_c 8C76
  8C76 L15 K 0/0/2
  8C7F L17 K 0/0/5
  8C84 L18 K 0/0/5
  8C88 L20 K 0/0/5
  8CBB L21 K 0/0/5
  8CBE L22 K 0/0/5
  8CD1 L23 K 0/0/5
  8CD4 L24 K 0/0/5
  8CE1 L27 K 0/0/5
square main_c 8C62
  8C62 L10 K 0/0/0
  8C65 L11 K 0/0/1
util_twice util_c 8CF1
  8CF1 L4 K 0/0/0
  8CF5 L6 K 0/0/1
  8CFB L7 K 0/0/1
  8CFC L8 K 0/0/1
)GOLD"},
        {"v2.4/sccz80-fc", R"GOLD(fcallee fc_c 8C74
  8C74 L4 U 0/0/0
ffast fc_c 8C62
  8C62 L3 K 0/0/0
main fc_c 8C92
  8C92 L5 U 0/0/0
  8C95 L6 K 1/0/4
)GOLD"},
        {"master/sccz80-fc", R"GOLD(fcallee fc_c 8C74
  8C74 L4 U 0/0/0
ffast fc_c 8C62
  8C62 L3 K 0/0/0
main fc_c 8C92
  8C92 L5 U 0/0/0
)GOLD"},
        {"master/sdcc-fc", R"GOLD(fcallee fc_c 8C4A
  8C4A L4 K 1/0/9
ffast fc_c 8C48
  8C48 L3 K 0/0/7
  8C49 L3 K 1/0/7
main fc_c 8C65
  8C65 L5 K 1/0/11
)GOLD"},
        {"master/80cc-fc", R"GOLD(fcallee fc_c 8C74
  8C74 L4 U 0/0/0
  8C78 L4 K 0/0/2
ffast fc_c 8C62
  8C62 L3 K 0/0/0
  8C6C L3 K 0/0/1
main fc_c 8C96
  8C96 L5 U 0/0/0
  8C9F L5 K 0/0/4
)GOLD"},
    };
        std::string bad;
        int n = 0;
        for (const auto& g : kScopeGolden) {
            SourceMap m;
            load(m, kFixtures / g.map / "prog.map");
            const VariableStore* s = store_of(m);
            std::vector<const CFunction*> fns;
            if (s)
                for (const CFunction& f : s->functions())
                    if (!f.scopes.empty()) fns.push_back(&f);
            std::sort(fns.begin(), fns.end(), [](const CFunction* a, const CFunction* b) {
                return a->name != b->name ? a->name < b->name : a->module < b->module;
            });
            std::string got;
            for (const CFunction* f : fns) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "%s %s %04X\n", f->name.c_str(), f->module.c_str(),
                              unsigned(f->entry.at.address));
                got += buf;
                for (const auto& c : f->scopes) {
                    std::snprintf(buf, sizeof buf, "  %04X L%d %c %d/%d/%d\n", unsigned(c.at.address), c.line,
                                  c.known ? 'K' : 'U', c.level, c.sub, c.block);
                    got += buf;
                }
            }
            ++n;
            if (got != g.golden) { bad += std::string(" ") + g.map; }
        }
        check("Z88CDB-40", "every function's whole scope table on 12 committed maps (cvars, debug and fc builds of "
                           "the three compilers) equals the golden derived from the raw map lines: the other-module, "
                           "data-section and equal-line-disagreement rules (GH #445: 4 goldens regenerated for z88dk v2.4-2883)",
              n == 12 && bad.empty());
        if (!bad.empty()) std::printf("  Z88CDB-40 differs:%s\n", bad.c_str());
    }


    // ── GH #434 stage 1: extents (F-A), the frame helpers, the listing evidence, the no-records facts ──
    // The extents golden is the output of `derive-scopes.py --extents` (raw map lines only); re-derive it
    // with that script when a fixture is regenerated, never paste this suite's output.
    const auto extent_text = [](const VariableStore& s) {
        std::vector<const CFunction*> fns;
        for (const CFunction& f : s.functions())
            if (f.entry.resolved) fns.push_back(&f);
        std::sort(fns.begin(), fns.end(), [](const CFunction* a, const CFunction* b) {
            return a->name != b->name ? a->name < b->name : a->module < b->module;
        });
        std::string got;
        for (const CFunction* f : fns) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s %s %04X\n", f->name.c_str(), f->module.c_str(),
                          unsigned(f->entry.at.address));
            got += buf;
            if (f->pieces.empty()) got += "  unknown\n";
            for (const auto& p : f->pieces) {
                std::snprintf(buf, sizeof buf, "  %04X-%04X +%X\n", unsigned(p.at.address), unsigned(p.hi_excl),
                              unsigned(p.offset));
                got += buf;
            }
        }
        return got;
    };
    {
        static const struct { const char* map; const char* golden; } kExtentGolden[] = {
        {"master/sccz80-cvars", R"GOLD(main vars_c 8D54
  8D54-8E38 +0
other other_c 8E38
  8E38-8E50 +0
sum vars_c 8CB9
  8CB9-8D54 +0
)GOLD"},
        {"master/sdcc-cvars", R"GOLD(main vars_c 8C9F
  8C9F-8D33 +0
other other_c 8D33
  8D33-8D4C +0
sum vars_c 8C48
  8C48-8C9F +0
)GOLD"},
        {"master/80cc-cvars", R"GOLD(main vars_c 8CD9
  8CD9-8DB0 +0
other other_c 8DB0
  8DB0-8DCB +0
sum vars_c 8C62
  8C62-8CD9 +0
)GOLD"},
        };
        std::string bad;
        for (const auto& g : kExtentGolden) {
            SourceMap m;
            load(m, kFixtures / g.map / "prog.map");
            const VariableStore* s = store_of(m);
            if (!s || extent_text(*s) != g.golden) bad += std::string(" ") + g.map;
        }
        check("Z88CDB-41", "every function's extent (pieces: start, exclusive end, offset) on the cvars builds of the three "
                           "compilers equals the golden derived from the raw map lines: the next function, another "
                           "module's symbol or line, and __code_compiler_tail bound it",
              bad.empty());
        if (!bad.empty()) std::printf("  Z88CDB-41 differs:%s\n", bad.c_str());
    }
    {
        // The extent's bound by ANOTHER module (plan 2.1.1): master/sccz80-cvars with one symbol, and then one line
        // record, of a module that is not vars_c added inside main ($8D54-$8E38): main ends there. A symbol of
        // vars_c's own module at the same address bounds nothing.
        const auto main_end = [&](const char* tag, const std::string& extra) {
            std::vector<std::string> lines = map_lines(kFixtures / "master/sccz80-cvars" / "prog.map");
            std::string body = join_lines(lines) + extra;
            SourceMap m;
            load(m, synth(tag, body));
            const VariableStore* s = store_of(m);
            const CFunction* f = s ? fn_named(*s, "main") : nullptr;
            return f && f->pieces.size() == 1 && f->pieces[0].at.address == 0x8D54 ? f->pieces[0].hi_excl : 0u;
        };
        const unsigned plain = main_end("z88cdb47a.map", "");
        const unsigned sym = main_end("z88cdb47b.map", "_helper = $8E00 ; addr, public, , zz_asm, code_compiler, zz.asm:3\n");
        const unsigned line = main_end("z88cdb47c.map", rec("__C_LINE_5_zz_2ec", 0x8E10, "zz_c", "code_compiler", "zz.c:5"));
        const unsigned own = main_end("z88cdb47d.map", "_helper = $8E00 ; addr, public, , vars_c, code_compiler, vars.c:70\n");
        check("Z88CDB-47", "a function's extent ends at a symbol, and at a line record, of another module inside it (main of "
                           "master/sccz80-cvars: $8E38 as built, $8E00 with a foreign symbol there, $8E10 with a foreign line "
                           "record), and a symbol of its own module bounds nothing",
              plain == 0x8E38 && sym == 0x8E00 && line == 0x8E10 && own == 0x8E38);
    }
    {
        // GH #434 F-A: cross() is 8192 bytes of padding, so its code runs from the $8000 slot into the next 8 KB
        // slot: two pieces (page 4, then page 5), and its records past $A000 are in its scope table with their
        // offsets. The golden is `derive-scopes.py --extents` on the raw map; nothing here is pasted from jnext.
        static const struct { const char* map; const char* golden; } kCrossGolden[] = {
        {"v2.4/sccz80-cross", R"GOLD(cross cross_c 8C6B
  8C6B-A000 +0
  A000-AC81 +1395
main cross_c AC81
  AC81-AC99 +0
)GOLD"},
        {"master/sccz80-cross", R"GOLD(cross cross_c 8C6B
  8C6B-A000 +0
  A000-AC81 +1395
main cross_c AC81
  AC81-AC99 +0
)GOLD"},
        {"master/sdcc-cross", R"GOLD(cross cross_c 8C48
  8C48-A000 +0
  A000-AC5B +13B8
main cross_c AC5B
  AC5B-AC68 +0
)GOLD"},
        {"master/80cc-cross", R"GOLD(cross cross_c 8C78
  8C78-A000 +0
  A000-AC97 +1388
main cross_c AC97
  AC97-ACAC +0
)GOLD"},
        };
        const MemSpace P5 = MemSpace::page(5);
        std::string bad;
        for (const auto& g : kCrossGolden) {
            SourceMap m;
            load(m, kFixtures / g.map / "prog.map");
            const VariableStore* s = store_of(m);
            const CFunction* f = s ? fn_named(*s, "cross") : nullptr;
            bool ok = s && extent_text(*s) == g.golden && f && f->pieces.size() == 2 &&
                      f->pieces[0].at.space == P4 && f->pieces[1].at.space == P5 && f->pieces[1].at.address == 0xA000 &&
                      f->pieces[0].hi_excl == 0xA000;
            // every scope record past $A000 is filed under cross, in page 5, with offset = piece offset + (address - $A000)
            int past = 0;
            for (const auto& c : f ? f->scopes : std::vector<jnext::dbg::CScope>{}) {
                if (c.at.space == P5) {
                    ++past;
                    ok = ok && c.offset == f->pieces[1].offset + (c.at.address - 0xA000);
                }
            }
            ok = ok && past == 2;   // the two lines after the padding (cross.c:20, :21)
            if (!ok) bad += std::string(" ") + g.map;
        }
        check("Z88CDB-42", "a function that crosses an 8 KB slot (cross.c, 8192 bytes of padding) on the four cross maps: "
                           "two pieces, page 4 up to $A000 and page 5 from $A000, equal to the golden derived from the raw "
                           "map; the records past $A000 are in its scope table (page 5) with offsets across the pieces",
              bad.empty());
        if (!bad.empty()) std::printf("  Z88CDB-42 differs:%s\n", bad.c_str());
    }
    {
        // GH #434 review F3: scope_at() on a store built by hand (offsets 4 and 10 of function 0), and on a real one.
        using jnext::dbg::CScope;
        CFunction f;
        f.name = "f";
        for (const uint32_t off : {4u, 10u}) {
            CScope c;
            c.offset = off;
            c.line = static_cast<int>(off);
            f.scopes.push_back(c);
        }
        const VariableStore hand({f}, {}, {}, {}, {});
        const auto line_at = [&](int fn, uint32_t off) {
            const CScope* c = hand.scope_at(fn, off);
            return c ? c->line : -1;
        };
        SourceMap m;
        load(m, kFixtures / "master/sccz80-cvars" / "prog.map");
        const VariableStore* s = store_of(m);
        int sum = -1;
        for (size_t i = 0; s && i < s->functions().size(); ++i)
            if (s->functions()[i].name == "sum") sum = static_cast<int>(i);
        const CScope* first = s ? s->scope_at(sum, 0) : nullptr;       // sum $8CB9 L29
        const CScope* exact = s ? s->scope_at(sum, 0x8CBC - 0x8CB9) : nullptr;   // L33
        const CScope* mid = s ? s->scope_at(sum, 0x8CC0 - 0x8CB9) : nullptr;     // between L33 and L34
        const CScope* last = s ? s->scope_at(sum, 0x8D3E - 0x8CB9 + 0x40) : nullptr;   // past the last: L41
        check("Z88CDB-48", "scope_at(): the entry with the greatest offset at or below the asked one: null before the first "
                           "entry, the exact entry at its own offset, the earlier one between two, the last past the end, and "
                           "null for an unknown function (a hand-built store, then master sccz80-cvars sum)",
              line_at(0, 3) == -1 && line_at(0, 4) == 4 && line_at(0, 9) == 4 && line_at(0, 10) == 10 && line_at(0, 5000) == 10 &&
                  line_at(-1, 4) == -1 && line_at(1, 4) == -1 && first && first->line == 29 && exact && exact->line == 33 &&
                  mid && mid->line == 33 && last && last->line == 41);
    }
    {
        // GH #434 review F4: locate() tells functions that sit at the same CPU address in different pages apart.
        SourceMap m;
        load(m, kFixtures / "master/sccz80-cvars-banked" / "prog.map");
        const VariableStore* s = store_of(m);
        const auto name_at = [&](MemSpace space, uint16_t a) {
            const auto loc = s ? s->locate({space, a}) : std::nullopt;
            return loc ? s->functions()[static_cast<size_t>(loc->function)].name : std::string("-");
        };
        const auto at = s ? s->locate({P20, 0xC04C}) : std::nullopt;
        check("Z88CDB-49", "locate(): bk_sum is at page 20 $C04C (offset $47 from $C005) and nowhere else: pages 21 and 4 at the same address, "
                           "and the CPU view, name no function",
              s && name_at(P20, 0xC04C) == "bk_sum" && at && at->offset == 0x47 && name_at(P21, 0xC04C) == "-" &&
                  name_at(P4, 0xC04C) == "-" && name_at(MemSpace::cpu(), 0xC04C) == "-" && name_at(P20, 0xC000) == "-");
    }
    {
        // GH #434 review F9 (R25): a record of the same module in ANOTHER section is no scope of a function of this one.
        SourceMap plain, other;
        load(plain, kFixtures / "master/sccz80-cvars" / "prog.map");
        std::string body = join_lines(map_lines(kFixtures / "master/sccz80-cvars" / "prog.map"));
        body += rec("__C_LINE_99_vars_2ec", 0x8D60, "vars_c", "code_user", "vars.c::main::0::6:99");
        load(other, synth("z88cdb50.map", body));
        const VariableStore* a = store_of(plain);
        const VariableStore* b = store_of(other);
        const CFunction* fa = a ? fn_named(*a, "main") : nullptr;
        const CFunction* fb = b ? fn_named(*b, "main") : nullptr;
        bool foreign = false;
        for (const auto& c : fb ? fb->scopes : std::vector<jnext::dbg::CScope>{}) foreign = foreign || c.line == 99;
        check("Z88CDB-50", "a line record of the same module in another code section (code_user) above main's entry is filed under no "
                           "function: main's scope table is as without it",
              fa && fb && !foreign && fa->scopes.size() == fb->scopes.size());
    }
    {
        bool resolved = true, absent = true;
        std::string seen;
        for (const char* map : {"v2.4/sccz80-cvars", "master/sccz80-cvars", "master/80cc-cvars", "v2.4/sccz80-fc",
                                "master/sccz80-fc", "master/80cc-fc", "v2.4/sccz80-cvars-banked",
                                "master/sccz80-cvars-banked"}) {
            SourceMap m;
            load(m, kFixtures / map / "prog.map");
            const VariableStore* s = store_of(m);
            resolved = resolved && s && s->helpers().framepointer.resolved && s->helpers().push_frame.resolved &&
                       s->helpers().pop_frame.resolved;
        }
        for (const char* map : {"master/sdcc-cvars", "master/sdcc-fc", "master/sdcc-cvars-banked"}) {
            SourceMap m;
            load(m, kFixtures / map / "prog.map");
            const VariableStore* s = store_of(m);
            absent = absent && s && !s->helpers().framepointer.resolved && !s->helpers().push_frame.resolved &&
                     !s->helpers().pop_frame.resolved &&
                     s->helpers().push_frame.why.find("no public symbol 'l_debug_push_frame'") != std::string::npos;
        }
        SourceMap m;
        load(m, kFixtures / "master/sccz80-cvars" / "prog.map");
        const VariableStore* s = store_of(m);
        check("Z88CDB-43", "the debug-frame helpers (__debug_framepointer, l_debug_push_frame, l_debug_pop_frame) resolve on "
                           "every sccz80 and 80cc cvars and fc map and the sccz80 banked ones (80cc bk.c keeps an IX frame and links no helper) ($906A, $8B64, $8B75 in page 4 on master "
                           "sccz80-cvars) and stay unresolved, with a reason, on zsdcc",
              resolved && absent && s && stored_at(s->helpers().framepointer, P4, 0x906A) &&
                  stored_at(s->helpers().push_frame, P4, 0x8B64) && stored_at(s->helpers().pop_frame, P4, 0x8B75));
    }
    {
        using jnext::dbg::CConv;
        bool listed = true, unlisted = true;
        for (const char* map : {"v2.4/sccz80-fc", "master/sccz80-fc", "master/80cc-fc"}) {
            SourceMap with, without;
            load_z88dk_source_map(with, (kFixtures / map / "prog.map").string(), nex_space, Z88dkListings::Beside);
            load(without, kFixtures / map / "prog.map");
            const VariableStore* a = store_of(with);
            const VariableStore* b = store_of(without);
            const CFunction* ffast = a ? fn_named(*a, "ffast") : nullptr;
            const CFunction* fcallee = a ? fn_named(*a, "fcallee") : nullptr;
            listed = listed && ffast && fcallee && ffast->conv == CConv::ListedFastcall && fcallee->conv == CConv::Listed;
            for (const CFunction& f : b ? b->functions() : std::vector<CFunction>{})
                unlisted = unlisted && f.conv == CConv::Unlisted;
        }
        SourceMap z;
        load_z88dk_source_map(z, (kFixtures / "master/sdcc-fc" / "prog.map").string(), nex_space, Z88dkListings::Beside);
        const VariableStore* zs = store_of(z);
        const CFunction* zf = zs ? fn_named(*zs, "ffast") : nullptr;
        check("Z88CDB-44", "the listings beside the map say which functions are __z88dk_fastcall: ffast is ListedFastcall "
                           "and fcallee Listed (fc.c.lis '; Function ffast flags ... __z88dk_fastcall') on both sccz80 "
                           "installs and 80cc, a zsdcc function is Listed (no flags), and every function is Unlisted "
                           "without the listings",
              listed && unlisted && zf && zf->conv == CConv::Listed);
    }
    {
        SourceMap so2;
        load_z88dk_source_map(so2, (kFixtures / "v2.4/sdcc-SO2" / "prog.map").string(), nex_space, Z88dkListings::Beside);
        const VariableStore* a = store_of(so2);
        std::vector<std::string> lines = map_lines(kFixtures / "master/sdcc-cvars" / "prog.map");
        std::vector<std::string> kept, junk;
        size_t cdb = 0;
        for (const auto& l : lines) {
            if (is_cdb_line(l)) {
                ++cdb;
                const size_t eq = l.find(' ');
                junk.push_back("__CDBINFO__" + cdb_escape("Q:bogus") + l.substr(eq));
            } else {
                kept.push_back(l);
                junk.push_back(l);
            }
        }
        SourceMap nocdb, refused;
        load(nocdb, synth("z88cdb45a.map", join_lines(kept)));
        load(refused, synth("z88cdb45b.map", join_lines(junk)));
        const VariableStore* b = store_of(nocdb);
        const VariableStore* c = store_of(refused);
        // master sccz80-cvars without its __CDBINFO__ lines: C_LINE records with a scope, in sccz80's form, not zsdcc's
        std::vector<std::string> sc_kept;
        for (const auto& l : map_lines(kFixtures / "master/sccz80-cvars" / "prog.map"))
            if (!is_cdb_line(l)) sc_kept.push_back(l);
        SourceMap sc_nocdb;
        load(sc_nocdb, synth("z88cdb45c.map", join_lines(sc_kept)));
        const VariableStore* d = store_of(sc_nocdb);
        check("Z88CDB-45", "what a map without variables looks like: v2.4 sdcc-SO2 with its listings has no C_LINE scope; "
                           "master sdcc-cvars without its __CDBINFO__ lines has C_LINE records in zsdcc's form and an "
                           "empty store; master sccz80-cvars without them has C_LINE records but not in zsdcc's form; with "
                           "every record undecodable all of them are refused and the facts stay",
              d && d->functions().empty() && d->no_record_facts().c_line_records && !d->no_record_facts().zsdcc_form && a && a->functions().empty() && !a->no_record_facts().c_line_records && !a->no_record_facts().zsdcc_form &&
                  b && b->functions().empty() && b->refused().empty() && b->no_record_facts().c_line_records &&
                  b->no_record_facts().zsdcc_form && c && c->functions().empty() && cdb > 0 &&
                  c->refused().size() == cdb && c->no_record_facts().c_line_records && c->no_record_facts().zsdcc_form);
    }
    {
        bool ok = true;
        int n = 0;
        for (const char* map : {"master/sccz80-cvars", "master/sdcc-cvars", "master/80cc-cvars", "v2.4/sccz80-cvars",
                                "v2.4/sccz80-debug", "master/sdcc-debug", "master/sccz80-debug", "master/80cc-debug",
                                "v2.4/sccz80-fc", "master/sccz80-fc", "master/sdcc-fc", "master/80cc-fc"}) {
            SourceMap m;
            load(m, kFixtures / map / "prog.map");
            const VariableStore* s = store_of(m);
            if (!s) { ok = false; continue; }
            ++n;
            for (const CFunction& f : s->functions()) {
                if (f.scopes.empty()) continue;
                ok = ok && f.pieces.size() == 1;
                uint32_t last = 0;
                for (const auto& c : f.scopes) {
                    ok = ok && c.offset == uint32_t(c.at.address - f.entry.at.address) && c.offset >= last;
                    last = c.offset;
                }
            }
        }
        check("Z88CDB-46", "on the 12 maps of Z88CDB-40 no function crosses a slot (one piece each), every scope's offset "
                           "is its address minus the entry's and the table is in offset order: the value-based "
                           "lookup changes nothing there",
              n == 12 && ok);
    }

    // ── GH #438: what the map says of the compiler, the unsized arrays, the float library and the fastcall records ──
    // Oracle: the listing banners z88dk wrote beside each map (80cc, sccz80, zsdcc), the C sources (fm.c, fl.c, xm.c) and the
    // fixtures' own record text.
    const auto banner_of = [](const fs::path& lis) {
        std::ifstream in(lis);
        std::string head((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        head.resize(std::min<size_t>(head.size(), 4000));
        return head.find("80cc z88dk") != std::string::npos        ? jnext::dbg::CCompiler::Cc80
               : head.find("Small-C/Plus") != std::string::npos ? jnext::dbg::CCompiler::Sccz80
                                                                 : jnext::dbg::CCompiler::Zsdcc;
    };
    {   // Z88ZB-01
        int modules = 0, bad = 0, c80 = 0, scc = 0, zs = 0;
        for (const char* root : {"v2.4", "v2.4-dotn", "master", "master-fixed"})
            for (const auto& d : fs::directory_iterator(kFixtures / root)) {
                if (!fs::exists(d.path() / "prog.map")) continue;
                SourceMap m;
                load(m, d.path() / "prog.map");
                const VariableStore* st = store_of(m);
                if (!st) continue;
                std::set<std::string> seen;
                for (const CFunction& f : st->functions()) {
                    if (!seen.insert(f.module).second) continue;
                    // the listing that declares `MODULE <module>`: the oracle of the module's compiler
                    std::optional<jnext::dbg::CCompiler> want;
                    for (const auto& e : fs::recursive_directory_iterator(d.path())) {
                        const std::string n = e.path().string();
                        if (n.size() < 6 || n.compare(n.size() - 6, 6, ".c.lis") != 0) continue;
                        std::ifstream in(e.path());
                        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        const std::string key = "MODULE";
                        size_t at = 0;
                        while ((at = text.find(key, at)) != std::string::npos) {
                            size_t b = text.find_first_not_of(" \t", at + key.size());
                            size_t e2 = text.find_first_of(" \t\r\n", b);
                            if (b != std::string::npos && text.substr(b, e2 - b) == f.module) { want = banner_of(e.path()); break; }
                            at += key.size();
                        }
                        if (want) break;
                    }
                    if (!want) continue;   // an assembler or library module: no compiler banner
                    ++modules;
                    if (f.compiler != *want) ++bad;
                    c80 += f.compiler == jnext::dbg::CCompiler::Cc80;
                    scc += f.compiler == jnext::dbg::CCompiler::Sccz80;
                    zs += f.compiler == jnext::dbg::CCompiler::Zsdcc;
                }
            }
        std::printf("  Z88ZB-01: %d modules (80cc %d, sccz80 %d, zsdcc %d), %d wrong\n", modules, c80, scc, zs, bad);
        check("Z88ZB-01", "every C module of every map under v2.4/, v2.4-dotn/, master/ and master-fixed/ is Cc80 iff its listing banner says `80cc z88dk`, "
                          "Sccz80 iff `Small-C/Plus`, else Zsdcc (the banners are the oracle; the module count is pinned from the tree)",
              bad == 0 && modules == 115 && c80 > 0 && scc > 0 && zs > 0);
    }

    // the records named `name` (its C name): "<count><h if hidden>" in store order
    const auto arr_records = [](const VariableStore& st, const char* name) {
        std::string out;
        for (const CVariable& v : st.variables()) {
            if (v.c_name != name || v.record.var.type.chain.empty()) continue;
            out += (out.empty() ? "" : " ") + std::to_string(v.record.var.type.chain[0].count) +
                   (v.record.hidden == CdbHidden::None ? "" : "h");
        }
        return out;
    };
    // GH #438 mutation pins: a minimal synthetic map (module t_c: functions f and g, one line record) plus the given extra map lines and records
    const auto zb_map = [&](const char* name, const std::string& extra) {
        const std::string text =
            std::string("_f = $8000 ; addr, public, , t_c, code_compiler, t.c::f::0::0:1\n_g = $8010 ; addr, public, , t_c, code_compiler, t.c::g::0::0:2\n") +
            "__C_LINE_1_t_2ec = $8000 ; addr, local, , t_c, code_compiler, t.c::f::0::0:1\n__code_compiler_tail = $8040 ; const, public, def, , ,\n" +
            "__CDBINFO__" + cdb_escape("F:G$f$0_0$0({0}DF,SI:S),C,0,0,0,0,0") + " = $0001 ; const, public, , t_c, code_compiler, t.c:1\n" +
            "__CDBINFO__" + cdb_escape("F:G$g$0_0$0({0}DF,SI:S),C,0,0,0,0,0") + " = $0001 ; const, public, , t_c, code_compiler, t.c:1\n" + extra;
        return synth(name, text);
    };
    const auto zb_cdb = [](const std::string& rec_text, const char* module = "t_c") {
        return "__CDBINFO__" + cdb_escape(rec_text) + " = $0001 ; const, public, , " + module + ", code_compiler, t.c:1\n";
    };
    {   // Z88ZB-03
        const auto all4 = [&](const char* fx) {
            SourceMap m;
            load(m, kFixtures / fx / "prog.map");
            const VariableStore* st = store_of(m);
            if (!st) return std::string("no store");
            return arr_records(*st, "ls") + " | " + arr_records(*st, "ss") + " | " + arr_records(*st, "fs") + " | " + arr_records(*st, "gs");
        };
        // what makes a sized record the twin of an unsized one: the same kind of scope, scope name, function, name and module. A sized record that differs in any
        // of them leaves the unsized one listed; two unsized records are nobody's twin
        SourceMap sm;
        load(sm, zb_map("z88zb03.map",
            zb_cdb("S:Lt.c.f$p$1_0$1({-1}DA-1x,SC:S),B,1,0") + zb_cdb("S:Lt.c.g$p$1_0$1({4}DA4x,SC:S),B,1,-4") +                  // another function
            zb_cdb("S:Lt.c.f$q$1_0$1({-1}DA-1x,SC:S),B,1,0") + zb_cdb("S:Lu.c.f$q$1_0$1({4}DA4x,SC:S),B,1,-4") +                  // another scope name
            zb_cdb("S:G$r$0_0$0({-1}DA-1x,SC:S),E,0,0") + zb_cdb("S:G$r$0_0$0({4}DA4x,SC:S),E,0,0", "u_c") +                        // another module
            zb_cdb("S:Ft.c$s$0_0$0({-1}DA-1x,SC:S),E,0,0") + zb_cdb("S:G$s$0_0$0({4}DA4x,SC:S),E,0,0") +                          // file scope vs global
            zb_cdb("S:G$w$0_0$0({-1}DA-1x,SC:S),E,0,0") + zb_cdb("S:G$w$0_0$0({-1}DA-1x,SI:S),E,0,0") +                            // two unsized records
            zb_cdb("S:G$z$0_0$0({-1}DA-1x,SC:S),E,0,0") + zb_cdb("S:G$z$0_0$0({4}DA4x,SC:S),E,0,0")));                              // a real twin: hidden
        const VariableStore* sst = store_of(sm);
        const auto listed_of = [&](const char* name) {
            int n = 0;
            for (const CVariable& v : sst ? sst->variables() : std::vector<CVariable>())
                if (v.c_name == name && v.record.hidden == CdbHidden::None && v.record.var.type.chain[0].count < 0) ++n;
            return n;
        };
        const bool synthetic_twins = sst && listed_of("p") == 1 && listed_of("q") == 1 && listed_of("r") == 1 && listed_of("s") == 1 && listed_of("w") == 2 && listed_of("z") == 0;
        // gs: the sized record and the unsized twin; the twin is hidden because the sized one is there (store order: by record text)
        const std::string pre = all4("v2.4/sccz80-fm"), mas = all4("master/sccz80-fm"), fix = all4("master-fixed/sccz80-fm"),
                          c80 = all4("master/80cc-fm"), c80f = all4("master-fixed/80cc-fm");
        std::printf("  Z88ZB-03: v2.4 [%s] master [%s] fixed [%s] 80cc [%s] 80cc-fixed [%s]\n", pre.c_str(), mas.c_str(), fix.c_str(), c80.c_str(), c80f.c_str());
        check("Z88ZB-03", "unsized arrays (fm.c): v2.4 and master sccz80 list ls, ss and fs with count -1 (their only record) and hide gs's -1 twin (a 5 record "
                          "is listed); master-fixed has four sized records and no twin; master 80cc has ls {4} and ss and fs {-1} listed, gs's twin hidden "
                          "(80cc keeps its -1 records until z88dk#3257, which sizes them on real master: master-fixed 80cc has four sized records and no twin)",
              pre == "-1 | -1 | -1 | -1h 5" && mas == pre && fix == "4 | 5 | 5 | 5" && c80 == "4 | -1 | -1 | -1h 5" && c80f == "4 | 5 | 5 | 5" && synthetic_twins);
    }
    {   // Z88ZB-04
        // the first NON-ZERO __code_fp_<lib>_size names the library (a zero size is a library not linked)
        int n_lib = 0;
        const auto synth_lib = [&](const std::string& lines) {
            SourceMap m;
            load(m, zb_map(("z88zb04_" + std::to_string(n_lib++) + ".map").c_str(), lines));
            return store_of(m) ? store_of(m)->float_library() : std::string("no store");
        };
        const auto lib = [&](const char* fx) {
            SourceMap m;
            load(m, kFixtures / fx / "prog.map");
            const VariableStore* st = store_of(m);
            return st ? st->float_library() : std::string("no store");
        };
        int newlib = 0;
        bool none = true;
        for (const char* root : {"v2.4", "master", "master-fixed"})
            for (const auto& d : fs::directory_iterator(kFixtures / root)) {
                const std::string n = d.path().filename().string();
                if (!fs::exists(d.path() / "prog.map") || n.compare(0, 7, "classic") == 0) continue;
                SourceMap m;
                load(m, d.path() / "prog.map");
                ++newlib;
                none = none && (!m.variables() || m.variables()->float_library().empty());
            }
        check("Z88ZB-04", "float library: master classic-mbf32 names mbf32 (__code_fp_mbf32_size $0007), classic-math32 names math32 ($001D); every other map "
                          "(newlib, and classic without float code) names none",
              lib("master/classic-mbf32") == "mbf32" && lib("master/classic-math32") == "math32" && lib("master/classic-debug").empty() && none && newlib == 81 &&
                  synth_lib("__code_fp_mbf32_size = $0000 ; const, public, def, , ,\n__code_fp_math32_size = $001D ; const, public, def, , ,\n") == "math32" &&
                  synth_lib("__code_fp_math32_size = $001D ; const, public, def, , ,\n__code_fp_mbf32_size = $0007 ; const, public, def, , ,\n") == "math32" &&
                  synth_lib("__code_fp_math32_size = $0000 ; const, public, def, , ,\n").empty());
    }
    {   // Z88ZB-06
        const auto var_off = [&](const char* fx, const char* fn, const char* name) {
            SourceMap m;
            load(m, kFixtures / fx / "prog.map");
            const VariableStore* st = store_of(m);
            if (st)
                for (const CVariable& v : st->variables())
                    if (v.c_name == name && v.function >= 0 && st->functions()[static_cast<size_t>(v.function)].name == fn && v.record.var.space == 'B')
                        return std::to_string(v.record.var.stack_offset());
            return std::string("-");
        };
        // the listing: ffast and fm push the register parameter right after `call l_debug_push_frame`
        const auto pushes = [&](const char* fx, const char* file, const char* sym) {
            std::ifstream in(kFixtures / fx / file);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const size_t at = text.find(std::string("._") + sym + "\n");
            const size_t call = text.find("call\tl_debug_push_frame", at);
            const size_t push = text.find("push\thl", call);
            return at != std::string::npos && call != std::string::npos && push != std::string::npos && call - at < 60 && push - call < 60;
        };
        check("Z88ZB-06", "master-fixed: fc.c's ffast x is recorded at -2 (below the frame), fm.c's a at +4 and b at -2 (the register parameter below, the others at their "
                          "nominal slots); the listings show `call l_debug_push_frame; push hl` at both entries; master (pre-fix) records ffast's x at +4 and fm's a at +6, b at +4",
              var_off("master-fixed/sccz80-fc", "ffast", "x") == "-2" && var_off("master-fixed/sccz80-fm", "fm", "a") == "4" &&
                  var_off("master-fixed/sccz80-fm", "fm", "b") == "-2" && pushes("master-fixed/sccz80-fc", "fc.c.lis", "ffast") &&
                  pushes("master-fixed/sccz80-fm", "fm.c.lis", "fm") && var_off("master/sccz80-fc", "ffast", "x") == "4" &&
                  var_off("master/sccz80-fm", "fm", "a") == "6" && var_off("master/sccz80-fm", "fm", "b") == "4");
    }
    {   // Z88ZB-07
        const auto of = [&](const char* fx, const char* fn) {
            SourceMap m;
            load(m, kFixtures / fx / "prog.map");
            const VariableStore* st = store_of(m);
            for (const CFunction& f : st ? st->functions() : std::vector<CFunction>())
                if (f.name == fn) return f.module + (f.compiler == jnext::dbg::CCompiler::Cc80 ? ":80cc" : f.compiler == jnext::dbg::CCompiler::Sccz80 ? ":sccz80" : f.compiler == jnext::dbg::CCompiler::Zsdcc ? ":zsdcc" : ":?");
            return std::string("-");
        };
        SourceMap m80;
        load(m80, kFixtures / "master/80cc-xmod" / "prog.map");
        std::set<std::string> mnames;
        for (const auto& r : m80.variables() ? m80.variables()->modules() : std::vector<jnext::dbg::CRecord>()) mnames.insert(r.record.var.name);
        check("Z88ZB-07", "X-prefixed modules (./xm.c is X__xm_c, 1st.c is X1st_c): on master 80cc-xmod both are 80cc, their M: records `./xm.c` and `1st.c` being the C_LINE "
                          "file fields; on sccz80-xmod both are sccz80; zsdcc names its modules xm_c and _1st_c and both are zsdcc (the listing banners agree: Z88ZB-01)",
              of("master/80cc-xmod", "xm") == "X__xm_c:80cc" && of("master/80cc-xmod", "first") == "X1st_c:80cc" && mnames == std::set<std::string>{"./xm.c", "1st.c"} &&
                  of("master/sccz80-xmod", "xm") == "X__xm_c:sccz80" && of("master/sccz80-xmod", "first") == "X1st_c:sccz80" &&
                  of("master/sdcc-xmod", "xm") == "xm_c:zsdcc" && of("master/sdcc-xmod", "first") == "_1st_c:zsdcc");
    }
    {   // Z88ZB-08
        // a module whose path is absolute: z80asm names it X_srv_u_g_u_x_c; the three maps differ only in the M: record and the C_LINE file
        const auto synthetic = [&](const char* name, const char* line_file, const char* m_record, const char* where = "::f::0::1:1") {
            std::string text =
                rec("__C_LINE_1_u_2dx_2ec", 0x9000, "X_srv_u_g_u_x_c", "code_compiler", (std::string(line_file) + where).c_str()) +
                rec("_f", 0x9000, "X_srv_u_g_u_x_c", "code_compiler", (std::string(line_file) + where).c_str()) +
                "__code_compiler_tail = $9010 ; const, public, def, , ,\n" +
                "__CDBINFO__" + cdb_escape("F:G$f$0_0$0({0}DF,SI:S),C,0,0,0,0,0") + " = $0001 ; const, public, , X_srv_u_g_u_x_c, code_compiler, x.c:1\n";
            std::string out = text;
            const std::string pub = "_f = $9000 ; addr, local";
            const size_t at = out.find(pub);
            out.replace(at, pub.size(), "_f = $9000 ; addr, public");
            if (*m_record) out += "__CDBINFO__" + cdb_escape(m_record) + " = $0001 ; const, public, , X_srv_u_g_u_x_c, code_compiler, x.c:1\n";
            SourceMap m;
            load(m, synth(name, out));
            const VariableStore* st = store_of(m);
            return st && st->functions().size() == 1 ? st->functions()[0].compiler : jnext::dbg::CCompiler::Unknown;
        };
        check("Z88ZB-08", "synthetic maps, module X_srv_u_g_u_x_c with F:G$f: M:/srv/u/g/u-x.c beside C_LINE records of file /srv/u/g/u-x.c is 80cc (the M: name equals the "
                          "file field); the same M: with C_LINE file u-x.c is zsdcc (zsdcc writes M:<module> against <module>.c, never a match), and so is a matching M: with C_LINE records in zsdcc's `x`+TAB form; an M: name is free text (game(v2)/main.c and a[1],b/m.c are 80cc: ( ) [ ] , are not record syntax there); no M: record is sccz80",
              synthetic("z88zb08a.map", "/srv/u/g/u-x.c", "M:/srv/u/g/u-x.c") == jnext::dbg::CCompiler::Cc80 &&
                  synthetic("z88zb08b.map", "u-x.c", "M:/srv/u/g/u-x.c") == jnext::dbg::CCompiler::Zsdcc &&
                  synthetic("z88zb08c.map", "/srv/u/g/u-x.c", "") == jnext::dbg::CCompiler::Sccz80 &&
                  synthetic("z88zb08d.map", "/srv/u/g/u-x.c", "M:/srv/u/g/u-x.c", "::x::10000::7\t:1") == jnext::dbg::CCompiler::Zsdcc &&
                  synthetic("z88zb08e.map", "game(v2)/main.c", "M:game(v2)/main.c") == jnext::dbg::CCompiler::Cc80 &&
                  synthetic("z88zb08f.map", "a[1],b/m.c", "M:a[1],b/m.c") == jnext::dbg::CCompiler::Cc80);
    }

    {   // Z88ZB-02
        // g2 of blocks.c (`for (;;)` at line 30 has no code of its own and shares its address with line 31): the trail holds EVERY C_LINE record of the extent, in
        // (offset, line) order, where scopes keeps the highest line of an address. The oracle is the raw map: the g2 records of blocks.c, counted from the text
        const auto trail_text = [&](const char* fixture) {
            SourceMap m;
            load(m, kFixtures / fixture / "prog.map");
            const VariableStore* st = store_of(m);
            const CFunction* g2 = st ? fn_named(*st, "g2", "blocks_c") : nullptr;
            if (!g2) return std::string("no g2");
            // raw: `__C_LINE_<n>_... = $<addr> ; addr, local, , blocks_c, code_compiler, blocks.c::g2::<level>::<block>:<line>`
            // (the extent is [g2's entry, h's entry): a record of another function's line at the entry address, such as f's closing line, belongs to the trail too)
            const CFunction* h = fn_named(*st, "h", "blocks_c");
            std::vector<std::pair<uint64_t, int>> raw;
            for (const std::string& l : map_lines(kFixtures / fixture / "prog.map")) {
                if (l.compare(0, 9, "__C_LINE_") != 0 || l.find(", blocks_c, code_compiler,") == std::string::npos) continue;
                uint64_t addr = 0;
                if (!parse_hex64(l, l.find('$') + 1, &addr)) continue;
                if (addr >= g2->entry.at.address && h && addr < h->entry.at.address) raw.push_back({addr, std::stoi(l.substr(l.rfind(':') + 1))});
            }
            std::sort(raw.begin(), raw.end());
            std::string out = std::to_string(raw.size()) + "/" + std::to_string(g2->trail.size()) + ":";
            bool same = raw.size() == g2->trail.size();
            for (size_t i = 0; same && i < raw.size(); ++i)
                same = g2->trail[i].at.address == raw[i].first && g2->trail[i].line == raw[i].second;
            int l30 = -1, l31 = -1;
            for (size_t i = 0; i < g2->trail.size(); ++i) {
                if (g2->trail[i].line == 30) l30 = static_cast<int>(i);
                if (g2->trail[i].line == 31) l31 = static_cast<int>(i);
            }
            const bool together = l30 >= 0 && l31 == l30 + 1 && g2->trail[static_cast<size_t>(l30)].offset == g2->trail[static_cast<size_t>(l31)].offset;
            return out + (same ? "same" : "differs") + (together ? ",30+31" : ",apart") + (g2->trail.size() > g2->scopes.size() ? ",longer" : ",NOT-longer");
        };
        const std::string a = trail_text("v2.4/sccz80-blocks"), b = trail_text("master/sccz80-blocks"), c = trail_text("master-fixed/sccz80-blocks");
        std::printf("  Z88ZB-02: %s | %s | %s\n", a.c_str(), b.c_str(), c.c_str());
        // two records of one address listed with the higher line first: the trail still reads (offset, line)
        SourceMap so;
        load(so, zb_map("z88zb02.map", "__C_LINE_9_t_2ec_9 = $8004 ; addr, local, , t_c, code_compiler, t.c::f::1::1:9\n__C_LINE_8_t_2ec_8 = $8004 ; addr, local, , t_c, code_compiler, t.c::f::2::2:8\n"));
        const CFunction* sf = store_of(so) ? fn_named(*store_of(so), "f", "t_c") : nullptr;
        const bool ordered = sf && sf->trail.size() == 3 && sf->trail[0].line == 1 && sf->trail[1].line == 8 && sf->trail[2].line == 9 && sf->trail[1].offset == 4 && sf->trail[2].offset == 4;
        check("Z88ZB-02", "blocks.c's g2 on v2.4, master and master-fixed sccz80: the trail holds every __C_LINE_ record of g2's extent (the raw map's records between g2's entry and h's, counted from the text, "
                          "in (address, line) order), lines 30 and 31 are adjacent at one offset, and the trail is longer than the scope table (which keeps one owner per address); a synthetic map that lists the higher line of one address first still gives (offset, line) order",
              a.find("same,30+31,longer") != std::string::npos && b == a && c.find("same,30+31,longer") != std::string::npos && ordered);
    }
    {   // Z88ZB-05
        // master-fixed sccz80-cvars: struct flags' members are exact bitfield records (lo 0/3 and mid 3/5 at offset 0, hi 0/8 at offset 1), g_str has ONE record (no unsized
        // twin), and `F:G$main` is block 0 where master numbers it 2 (z88dk#3227: the last block opened)
        const auto facts = [&](const char* fixture) {
            SourceMap m;
            load(m, kFixtures / fixture / "prog.map");
            const VariableStore* st = store_of(m);
            std::string out;
            if (!st) return std::string("no store");
            for (const auto& t : st->types())
                if (t.record.var.name == "flags")
                    for (const auto& mem : t.record.members) {
                        const auto& ch = mem.var.type.chain;
                        out += mem.var.name + "=" + (ch.empty() || ch[0].kind != jnext::dbg::CdbTypeKind::Bitfield ? std::string("-") : std::to_string(ch[0].bit_start) + "/" + std::to_string(ch[0].bit_width)) +
                               "@" + std::to_string(mem.offset) + " ";
                    }
            int g_str = 0;
            for (const CVariable& v : st->variables()) g_str += v.c_name == "g_str";
            const CFunction* mn = fn_named(*st, "main", "vars_c");
            return out + "g_str=" + std::to_string(g_str) + " main.block=" + (mn ? std::to_string(mn->record.var.block) : std::string("?"));
        };
        const std::string fx = facts("master-fixed/sccz80-cvars"), mx = facts("master/sccz80-cvars");
        std::printf("  Z88ZB-05: %s | %s\n", fx.c_str(), mx.c_str());
        check("Z88ZB-05", "master-fixed sccz80-cvars: struct flags' members are lo 0/3 and mid 3/5 at offset 0 and hi 0/8 at offset 1 (start/width bitfield records), g_str has one record, "
                          "F:G$main is block 0; master's flags are whole ints, g_str has its twin and main is block 2",
              fx == "lo=0/3@0 mid=3/5@0 hi=0/8@1 g_str=1 main.block=0" && mx.find("g_str=2 main.block=2") != std::string::npos && mx.find("lo=-@0") != std::string::npos);
    }

    {   // Z88ZB-09
        // zsdcc's `x` placeholder is "no function" with or without the trailing TAB (z88dk#3222 A drops it). Oracle: the same map with and without the TABs must read the same:
        // the fixture master/sdcc-cvars with every `\t:` made `:`, and a synthetic address shared by two functions' lines, which must not alias
        const auto strip_tabs = [](std::string text) {
            std::string out;
            for (const char c : text)
                if (c != '\t') out += c;
            return out;
        };
        std::string cv_text;
        for (const std::string& l : map_lines(kFixtures / "master/sdcc-cvars" / "prog.map")) cv_text += l + "\n";
        SourceMap with, without;
        load(with, synth("z88zb09a.map", cv_text));
        load(without, synth("z88zb09b.map", strip_tabs(cv_text)));
        const bool same_store = store_of(with) && store_of(without) && store_dump(*store_of(with)) == store_dump(*store_of(without)) && !store_of(with)->functions().empty();
        const auto starts41 = [](const SourceMap& m) { return m.resolve("vars.c:41").starts.size(); };
        const std::string shared =
            "_fa = $9000 ; addr, public, , t_c, code_compiler, t.c:3\n_fb = $9000 ; addr, public, , t_c, code_compiler, t.c:4\n__code_compiler_tail = $9020 ; const, public, def, , ,\n";
        const auto shared_map = [&](const char* name, const char* tab) {
            SourceMap m;
            load(m, synth(name, shared + rec("__C_LINE_3_t_2ec_3", 0x9000, "t_c", "code_compiler", (std::string("t.c::x::10000::8") + tab + ":3").c_str()) +
                                    rec("__C_LINE_4_t_2ec_4", 0x9000, "t_c", "code_compiler", (std::string("t.c::x::10000::9") + tab + ":4").c_str())));
            return std::make_pair(m.resolve("t.c:3").starts.size(), m.resolve("t.c:4").starts.size());
        };
        const auto t1 = shared_map("z88zb09c.map", "\t"), t0 = shared_map("z88zb09d.map", "");
        // the status text of a zsdcc map without records (Z88CDB-45's shape) names zsdcc whichever way the line ends
        std::vector<std::string> kept;
        for (const std::string& l : map_lines(kFixtures / "master/sdcc-cvars" / "prog.map"))
            if (!is_cdb_line(l)) kept.push_back(strip_tabs(l));
        SourceMap bare;
        load(bare, synth("z88zb09e.map", join_lines(kept)));
        // a zsdcc function really named `x`, records without the TAB: its scope entry is level 1 / block 8 (level_raw 10000 = level 1, sub 0), not level 10000
        SourceMap xf;
        load(xf, synth("z88zb09f.map",
            "_x = $9000 ; addr, public, , t_c, code_compiler, t.c:3\n__code_compiler_tail = $9020 ; const, public, def, , ,\n" +
            rec("__C_LINE_3_t_2ec_3", 0x9000, "t_c", "code_compiler", "t.c::x::10000::8:3") + rec("__C_LINE_0_t_2ec_0", 0x0, "t_c", "", "t.c:0") +
            "__CDBINFO__" + cdb_escape("F:G$x$0_0$0({0}DF,SI:S),C,0,0,0,0,0") + " = $0001 ; const, public, , t_c, code_compiler, t.c:1\n"));
        const CFunction* xfn = store_of(xf) ? fn_named(*store_of(xf), "x", "t_c") : nullptr;
        const bool x_level = xfn && xfn->scopes.size() == 1 && xfn->scopes[0].known && xfn->scopes[0].level == 1 && xfn->scopes[0].block == 8 && xfn->trail.size() == 1 && xfn->trail[0].level == 1;
        check("Z88ZB-09", "zsdcc's `x` is no function with or without the trailing TAB: master/sdcc-cvars with its TABs removed gives the same store as the original and the same starts for vars.c:41; "
                          "two functions' lines at one address ($9000, `x::...:3` and `x::...:4`, `_fb` the later entry) are not aliased either way (line 3 has no start of its own, line 4 has "
                          "one); a record-less zsdcc map without TABs still says `zsdcc_form`; a zsdcc function really named `x` without the TAB keeps zsdcc's level (1, not 10000)",
              same_store && starts41(with) == starts41(without) && starts41(with) == 1 && t1.first == 0 && t1.second == 1 && t0 == t1 && store_of(bare) && store_of(bare)->no_record_facts().zsdcc_form && x_level);
    }

    {   // Z88ZB-10
        // z88dk#3262 and #3265 on the real zsdcc records of master-fixed-zsdcc: no C_LINE carries the TAB, the -debug modules are named by the whole path (./xm.c is __xm_c)
        bool no_tab = true, all_zsdcc = true, listed = true;
        int fns = 0, known_scopes = 0;
        for (const char* d : {"sdcc-cvars", "sdcc-blocks", "sdcc-xmod", "sdcc-debug"}) {
            const fs::path map = kFixtures / "master-fixed-zsdcc" / d / "prog.map";
            for (const std::string& l : map_lines(map)) no_tab = no_tab && l.find('\t') == std::string::npos;
            SourceMap m;
            load(m, map);
            const VariableStore* st = store_of(m);
            if (!st) { listed = false; continue; }
            all_zsdcc = all_zsdcc && st->refused().empty() && st->no_record_facts().zsdcc_form;
            for (const CFunction& f : st->functions()) {
                ++fns;
                all_zsdcc = all_zsdcc && f.compiler == jnext::dbg::CCompiler::Zsdcc;
                for (const auto& c : f.scopes) known_scopes += c.known;
            }
        }
        const auto of = [&](const char* fx, const char* fn) {
            SourceMap m;
            load(m, kFixtures / fx / "prog.map");
            const VariableStore* st = store_of(m);
            for (const CFunction& f : st ? st->functions() : std::vector<CFunction>())
                if (f.name == fn) return f.module + (f.compiler == jnext::dbg::CCompiler::Zsdcc ? ":zsdcc" : ":?");
            return std::string("-");
        };
        SourceMap cv;
        load(cv, kFixtures / "master-fixed-zsdcc/sdcc-cvars" / "prog.map");
        const CFunction* sum = store_of(cv) ? fn_named(*store_of(cv), "sum") : nullptr;
        check("Z88ZB-10", "master-fixed-zsdcc (zsdcc after z88dk#3262 and #3265): no map line has a TAB; every function of the four maps is zsdcc, nothing is refused and the map is zsdcc_form; "
                          "the scope walk reads their TAB-less C_LINE records (sum has known scopes); ./xm.c is module __xm_c and 1st.c _1st_c, both zsdcc",
              no_tab && listed && all_zsdcc && fns > 0 && known_scopes > 0 && sum && !sum->scopes.empty() && sum->scopes.back().known &&
                  of("master-fixed-zsdcc/sdcc-xmod", "xm") == "__xm_c:zsdcc" && of("master-fixed-zsdcc/sdcc-xmod", "first") == "_1st_c:zsdcc");
    }
    {   // Z88ZB-11
        // z88dk#3241: master-fixed's sccz80 and 80cc cvars maps carry N: and E: records (the S: record of g_colour is still there) and no empty enumerator records
        bool ok = true;
        for (const char* fx : {"master-fixed/sccz80-cvars", "master-fixed/80cc-cvars"}) {
            SourceMap m;
            load(m, kFixtures / fx / "prog.map");
            const VariableStore* st = store_of(m);
            if (!st || !st->refused().empty()) { ok = false; continue; }
            int colour = 0, enumerators = 0, enum_records = 0;
            for (const CVariable& v : st->variables()) {
                colour += v.c_name == "g_colour" && v.record.hidden == CdbHidden::None;
                enumerators += v.c_name == "RED" || v.c_name == "GREEN" || v.c_name == "BLUE";
            }
            for (const std::string& l : map_lines(kFixtures / fx / "prog.map"))
                enum_records += l.compare(0, 13, "__CDBINFO__N_") == 0 || l.compare(0, 13, "__CDBINFO__E_") == 0;
            ok = ok && colour == 1 && enumerators == 0 && enum_records == 2;
        }
        SourceMap pre;
        load(pre, kFixtures / "master/sccz80-cvars" / "prog.map");
        int pre_enumerators = 0;
        for (const CVariable& v : store_of(pre) ? store_of(pre)->variables() : std::vector<CVariable>()) pre_enumerators += v.c_name == "RED" || v.c_name == "GREEN" || v.c_name == "BLUE";
        check("Z88ZB-11", "master-fixed sccz80-cvars and 80cc-cvars: one N: and one E: record each, none refused, g_colour listed once, and the three empty enumerator records of master/sccz80-cvars (RED, GREEN, BLUE) are gone",
              ok && pre_enumerators == 3);
    }

    {   // Z88ZB-12
        // 80cc with PR #3243's records, master-fixed-80cc/80cc-scope (f3254.c). Oracle: the listing's own C_LINE directives (f3254.c.lis; f's first byte is offset 0): L13 at +$51 (1/1), the inner loop's first
        // C_LINE 10 (4/3) at +$58, its third (`j++`) at +$75, then L9 (2/1) at +$85 and L12 at +$92
        const fs::path map = kFixtures / "master-fixed-80cc/80cc-scope" / "prog.map";
        SourceMap with, without;
        load_z88dk_source_map(with, map.string(), nex_space, Z88dkListings::Beside);
        load_z88dk_source_map(without, map.string(), nex_space, Z88dkListings::None);
        const auto fn_idx = [](const VariableStore& st, const char* name) {
            for (size_t i = 0; i < st.functions().size(); ++i)
                if (st.functions()[i].name == name) return static_cast<int>(i);
            return -1;
        };
        const VariableStore* sw = with.variables().get();
        const VariableStore* so = without.variables().get();
        bool ok = sw && so;
        if (ok) {
            const int f = fn_idx(*sw, "f"), fo = fn_idx(*so, "f");
            const auto sc = [&](const VariableStore& st, int fi, uint32_t off) { return fi >= 0 ? st.scope_at(fi, off) : nullptr; };
            const jnext::dbg::CScope* a = sc(*sw, f, 0x75);
            const jnext::dbg::CScope* b = sc(*sw, f, 0x58);
            const jnext::dbg::CScope* c = sc(*sw, f, 0x51);
            const jnext::dbg::CScope* d = sc(*sw, f, 0x92);
            const jnext::dbg::CScope* z = sc(*so, fo, 0x75);
            ok = f >= 0 && fo >= 0 && sw->functions()[static_cast<size_t>(f)].scopes_from_listing && !so->functions()[static_cast<size_t>(fo)].scopes_from_listing &&
                 a && a->line == 10 && a->level == 4 && a->block == 3 && a->known && b && b->line == 10 && b->block == 3 && c && c->line == 13 && c->level == 1 && c->block == 1 &&
                 d && d->line == 12 && d->level == 2 && d->block == 1 && z && z->line == 13 && z->level == 1;
            // before PR #3243 80cc writes level 0 everywhere: master/80cc-cvars has listings beside its map and no function takes the listing's scopes
            SourceMap pre;
            load_z88dk_source_map(pre, (kFixtures / "master/80cc-cvars" / "prog.map").string(), nex_space, Z88dkListings::Beside);
            const VariableStore* sp = pre.variables().get();
            bool any_from_listing = !sp || sp->functions().empty();
            if (sp) for (const CFunction& pf : sp->functions()) any_from_listing = any_from_listing || pf.scopes_from_listing;
            ok = ok && !any_from_listing;
        }
        check("Z88ZB-12", "master-fixed-80cc f3254.c: with the listing beside the map f's scope table holds every C_LINE occurrence (at +$75 L10 4/3, at +$58 L10 4/3, +$51 L13 1/1, +$92 L12 2/1); with Z88dkListings::None the flag is off and +$75 resolves to L13 (1/1), the map's one symbol per scope; the pre-#3243 master/80cc-cvars, listings beside it, has no function with the flag",
              ok);
    }

    {   // Z88ZB-13
        // The last C_LINE directive before a byte row owns it (z80asm puts the earlier ones on the same address; here a block opens and a statement follows with no code between). A synthetic 80cc
        // module t_c, function f: `C_LINE 5` (1/1) owns the first nop at $9000; `C_LINE 6` (2/2) then `C_LINE 7` (2/3) come before the second nop at $9001, which line 7 (block 3) owns
        const auto lrow = [](int line, const char* addr, const char* bytes, const std::string& text) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%6d  ", line);
            std::string out = buf;
            if (addr) {
                std::snprintf(buf, sizeof buf, "%s  %-16s  ", addr, bytes);
                out += buf;
            } else {
                out += std::string(24, ' ');
            }
            return out + text + "\n";
        };
        const fs::path d = dir / "z88zb13";
        fs::create_directories(d);
        write_text(d / "t.c.lis", std::string("t.c:\n") + lrow(1, nullptr, nullptr, " MODULE t_c") + lrow(2, nullptr, nullptr, " SECTION code_user") + lrow(3, nullptr, nullptr, "_f:") +
                                      lrow(4, nullptr, nullptr, " C_LINE 5,\"t.c::f::1::1\"") + lrow(5, "0000", "00", " nop") +
                                      lrow(6, nullptr, nullptr, " C_LINE 6,\"t.c::f::2::2\"") + lrow(7, nullptr, nullptr, " C_LINE 7,\"t.c::f::2::3\"") +
                                      lrow(8, "0001", "00", " nop") + lrow(9, "0002", "c9", " ret"));
        write_text(d / "t.map",
            "__code_user_head                = $9000 ; const, public, def, , ,\n__code_user_size                = $0003 ; const, public, def, , ,\n"
            "__code_user_tail                = $9003 ; const, public, def, , ,\n" +
                std::string("_f                              = $9000 ; addr, public, , t_c, code_user, t.c:3\n") + rec("__C_LINE_5_t_2ec_3a_3af_3a_3a1_3a_3a1", 0x9000, "t_c", "code_user", "t.c::f::1::1:5") +
                rec("__C_LINE_6_t_2ec_3a_3af_3a_3a2_3a_3a2", 0x9001, "t_c", "code_user", "t.c::f::2::2:6") + rec("__C_LINE_7_t_2ec_3a_3af_3a_3a2_3a_3a3", 0x9001, "t_c", "code_user", "t.c::f::2::3:7") +
                zb_cdb("F:G$f$0_0$0({0}DF,SI:S),C,0,0,0,0,0") + zb_cdb("M:t.c"));
        SourceMap m;
        load_z88dk_source_map(m, (d / "t.map").string(), nex_space, Z88dkListings::Beside);
        const VariableStore* st = m.variables().get();
        const CFunction* f = st ? fn_named(*st, "f") : nullptr;
        check("Z88ZB-13", "an 80cc function with `C_LINE 5` (1/1), then two directives (`C_LINE 6` 2/2 and `C_LINE 7` 2/3) before one byte row: the listing's scope table is [L5 1/1 at +0, L7 2/3 at +1] "
                          "(the last directive owns the row), the flag is set and the compiler is 80cc",
              f && f->compiler == jnext::dbg::CCompiler::Cc80 && f->scopes_from_listing && f->scopes.size() == 2 && f->scopes[0].offset == 0 && f->scopes[0].line == 5 && f->scopes[0].level == 1 &&
                  f->scopes[0].block == 1 && f->scopes[1].offset == 1 && f->scopes[1].line == 7 && f->scopes[1].level == 2 && f->scopes[1].block == 3);
    }

    {   // Z88ZB-14
        // a directive inside f that names ANOTHER function gives an unknown scope there (review 7 M13): the listing's second occurrence is `t.c::g::2::2`, so it is not known and f's block locals are
        // flagged at it. Same synthetic module as Z88ZB-13
        const auto lrow = [](int line, const char* addr, const char* bytes, const std::string& text) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%6d  ", line);
            std::string out = buf;
            if (addr) {
                std::snprintf(buf, sizeof buf, "%s  %-16s  ", addr, bytes);
                out += buf;
            } else {
                out += std::string(24, ' ');
            }
            return out + text + "\n";
        };
        const fs::path d = dir / "z88zb14";
        fs::create_directories(d);
        write_text(d / "t.c.lis", std::string("t.c:\n") + lrow(1, nullptr, nullptr, " MODULE t_c") + lrow(2, nullptr, nullptr, " SECTION code_user") + lrow(3, nullptr, nullptr, "_f:") +
                                      lrow(4, nullptr, nullptr, " C_LINE 5,\"t.c::f::1::1\"") + lrow(5, "0000", "00", " nop") +
                                      lrow(6, nullptr, nullptr, " C_LINE 6,\"t.c::g::2::2\"") + lrow(7, "0001", "00", " nop") + lrow(8, "0002", "c9", " ret"));
        write_text(d / "t.map",
            "__code_user_head                = $9000 ; const, public, def, , ,\n__code_user_size                = $0003 ; const, public, def, , ,\n"
            "__code_user_tail                = $9003 ; const, public, def, , ,\n" +
                std::string("_f                              = $9000 ; addr, public, , t_c, code_user, t.c:3\n") + rec("__C_LINE_5_t_2ec_3a_3af_3a_3a1_3a_3a1", 0x9000, "t_c", "code_user", "t.c::f::1::1:5") +
                rec("__C_LINE_6_t_2ec_3a_3ag_3a_3a2_3a_3a2", 0x9001, "t_c", "code_user", "t.c::g::2::2:6") + zb_cdb("F:G$f$0_0$0({0}DF,SI:S),C,0,0,0,0,0") + zb_cdb("M:t.c"));
        SourceMap m;
        load_z88dk_source_map(m, (d / "t.map").string(), nex_space, Z88dkListings::Beside);
        const VariableStore* st = m.variables().get();
        const CFunction* f = st ? fn_named(*st, "f") : nullptr;
        check("Z88ZB-14", "an 80cc function whose listing has a directive naming another function (`t.c::g::2::2` inside f): the listing's scope table is [L5 1/1 known at +0, L6 not known at +1], the flag is set",
              f && f->compiler == jnext::dbg::CCompiler::Cc80 && f->scopes_from_listing && f->scopes.size() == 2 && f->scopes[0].known && f->scopes[0].level == 1 && !f->scopes[1].known && f->scopes[1].line == 6);
    }
    {   // Z88ZB-15
        // review 7 F3: a zsdcc module whose only function has an empty body has one level-0 `x` record with no TAB and `M:z2.c` (since z88dk#3265). Oracle: the listing banners (SDCC) and the C sources
        // (stub2.c is `void p(int a) {}`, stub1.c a normal main); `p` and `main` are zsdcc, not 80cc
        SourceMap m;
        load_z88dk_source_map(m, (kFixtures / "master-fixed-zsdcc/sdcc-stub" / "prog.map").string(), nex_space, Z88dkListings::Beside);
        const VariableStore* st = m.variables().get();
        const CFunction* p = st ? fn_named(*st, "p") : nullptr;
        const CFunction* mn = st ? fn_named(*st, "main") : nullptr;
        check("Z88ZB-15", "master-fixed-zsdcc sdcc-stub (stub2.c: `void p(int a) {}`): p, whose module has only level-0 x records, and main are Zsdcc",
              p && mn && p->compiler == jnext::dbg::CCompiler::Zsdcc && mn->compiler == jnext::dbg::CCompiler::Zsdcc && p->module != mn->module);
    }
    {   // Z88ZB-16
        // the guard of the F3 clause: a module that really has a function named `x` is 80cc (x.c: `int x(int a) {...}`, 80cc writes `x` for it with an `F:` record), not the zsdcc placeholder
        SourceMap m;
        load_z88dk_source_map(m, (kFixtures / "master-fixed-80cc/80cc-x" / "prog.map").string(), nex_space, Z88dkListings::Beside);
        const VariableStore* st = m.variables().get();
        const CFunction* x = st ? fn_named(*st, "x") : nullptr;
        const CFunction* mn = st ? fn_named(*st, "main") : nullptr;
        check("Z88ZB-16", "master-fixed-80cc 80cc-x (a function named x): x and main are Cc80, the placeholder rule does not fire when the module has an `F:x`",
              x && mn && x->compiler == jnext::dbg::CCompiler::Cc80 && mn->compiler == jnext::dbg::CCompiler::Cc80);
    }

    {   // Z88ZB-17
        // review 8 G3: zsdcc writes a function with a block static local's `F:` record twice (`...,C,0,-4,...` and `...,C,0,0,...`; the same on 97731e4e29). They are one function: f and f2 of
        // statics.c are one each in the store and carry their parameter and locals (a, t, u and the static t for f; a and two t for f2); sccz80 and 80cc write one record and give the same counts
        int one = 0;
        bool vars = true;
        for (const char* fx : {"master-fixed-zsdcc/sdcc-static", "master-fixed-sccz80/sccz80-static", "master-fixed-80cc/80cc-static"}) {
            SourceMap m;
            load_z88dk_source_map(m, (kFixtures / fx / "prog.map").string(), nex_space, Z88dkListings::Beside);
            const VariableStore* st = m.variables().get();
            if (!st) { vars = false; continue; }
            int nf = 0, nf2 = 0;
            for (const CFunction& f : st->functions()) {
                nf += f.name == "f";
                nf2 += f.name == "f2";
            }
            one += nf == 1 && nf2 == 1;
            int fi = -1, f2i = -1;
            for (size_t i = 0; i < st->functions().size(); ++i) {
                if (st->functions()[i].name == "f") fi = static_cast<int>(i);
                if (st->functions()[i].name == "f2") f2i = static_cast<int>(i);
            }
            int a = 0, t = 0, u = 0, a2 = 0, t2 = 0;
            for (const CVariable& v : st->variables()) {
                if (v.record.hidden != CdbHidden::None) continue;
                if (v.function == fi) { a += v.c_name == "a"; t += v.c_name == "t"; u += v.c_name == "u"; }
                if (v.function == f2i) { a2 += v.c_name == "a"; t2 += v.c_name == "t"; }
            }
            vars = vars && a == 1 && t == 2 && u == 1 && a2 == 1 && t2 == 2 && st->refused().empty();
        }
        check("Z88ZB-17", "statics.c on zsdcc (two `F:` records per function), sccz80 and 80cc: f and f2 are one function each, with a, t (the local and the static) and u, and a and two t",
              one == 3 && vars);
    }

    {   // Z88FP-01
        // GH #477: every nightly map records, for each module of ours, exactly the float format of its build; the mixed link keeps its two modules apart
        struct B { const char* dir; int mode, bias, ms; };
        const B builds[] = {
            {"sccz80-zx-math48", 0, 128, 5}, {"sccz80-zx-genmath", 0, 128, 5}, {"sccz80-zx-lmz", 0, 128, 5}, {"sccz80-zx-math32", 1, 126, 3}, {"sccz80-zx-lm", 1, 126, 3},
            {"sccz80-zx-am9511", 1, 126, 3}, {"sccz80-zx-mbf32", 2, 128, 3}, {"sccz80-zx-mbf40", 3, 128, 4}, {"sccz80-zx-mbf64", 4, 128, 7}, {"sccz80-zx-bbc", 5, 127, 4},
            {"sccz80-zx-z88", 5, 127, 4}, {"sccz80-zx-dai32", 6, 0, 3}, {"sccz80-zx-bias129", 0, 129, 4}, {"80cc-zx-math48", 0, 128, 5}, {"80cc-zx-math32", 1, 126, 3},
            {"80cc-zx-mbf32", 2, 128, 3}, {"sccz80-zxn", 0, 128, 5}, {"sccz80-zxn-math32", 1, 126, 3}, {"80cc-zxn", 0, 128, 5}};
        std::string bad;
        int maps = 0;
        for (const B& b : builds) {
            SourceMap m;
            load(m, kFixtures / "nightly" / b.dir / "prog.map");
            const VariableStore* st = store_of(m);
            ++maps;
            const jnext::dbg::CFloatFormat* f = st ? st->float_format("fpfmt_c") : nullptr;
            if (!f || f->mode != b.mode || f->exponent_bias != b.bias || f->mantissa_size != b.ms || !f->why.empty()) bad += std::string(" ") + b.dir;
            if (st)
                for (const CVariable& v : st->variables())
                    if (const auto* g = st->float_format(v.module); g && !g->why.empty()) bad += std::string(" why:") + b.dir;
        }
        const struct { const char* dir; int mode, bias, ms; } fl[] = {{"sccz80-zx-fl-mbf32", 2, 128, 3}, {"sccz80-zx-fl-dai32", 6, 0, 3}};
        for (const auto& b : fl) {
            SourceMap m;
            load(m, kFixtures / "nightly" / b.dir / "prog.map");
            const VariableStore* st = store_of(m);
            const jnext::dbg::CFloatFormat* f = st ? st->float_format("fl_c") : nullptr;
            if (!f || f->mode != b.mode || f->exponent_bias != b.bias || f->mantissa_size != b.ms || !f->why.empty()) bad += std::string(" ") + b.dir;
        }
        SourceMap mix;
        load(mix, kFixtures / "nightly/sccz80-zx-mix/prog.map");
        const VariableStore* ms = store_of(mix);
        const jnext::dbg::CFloatFormat* a = ms ? ms->float_format("fpmix48_c") : nullptr;
        const jnext::dbg::CFloatFormat* c = ms ? ms->float_format("fpmix32_c") : nullptr;
        const bool mixed = a && c && a->mode == 0 && a->exponent_bias == 128 && a->mantissa_size == 5 && c->mode == 1 && c->exponent_bias == 126 && c->mantissa_size == 3 &&
                           a->why.empty() && c->why.empty();
        check("Z88FP-01", "the 19 fpfmt.c nightly maps carry, for module fpfmt_c, exactly the triple of their build (math48/genmath/lmz 0/128/5, math32/lm/am9511 1/126/3, mbf32 2/128/3, "
                          "mbf40 3/128/4, mbf64 4/128/7, bbc and z88 5/127/4, dai32 6/0/3, bias 129 0/129/4, 80cc and +zxn the same), none unusable; the mixed link keeps fpmix48_c 0/128/5 and fpmix32_c 1/126/3 apart; the two fl.c maps (library linked) record fl_c 2/128/3 (mbf32) and 6/0/3 (dai32)",
              maps == 19 && bad.empty() && mixed);
        if (!bad.empty()) std::printf("  Z88FP-01:%s\n", bad.c_str());
    }
    {   // Z88FP-02
        // no map built by z88dk 2.4, by master before 2026-11-06 or by zsdcc has a float format on any module (the symbols are written by sccz80 and 80cc of the nightly only)
        int maps = 0;
        std::string bad;
        for (const char* root : {"v2.4", "v2.4-dotn", "v2.4-fp", "master", "master-fixed", "master-fixed-80cc", "master-fixed-sccz80", "master-fixed-zsdcc"}) {
            if (!fs::is_directory(kFixtures / root)) { bad += std::string(" no ") + root; continue; }
            for (const auto& d : fs::recursive_directory_iterator(kFixtures / root)) {
                if (d.path().filename() != "prog.map") continue;
                ++maps;
                SourceMap m;
                load(m, d.path());
                if (const VariableStore* st = store_of(m)) {
                    bool any = false;
                    for (const CVariable& v : st->variables()) any = any || st->float_format(v.module);
                    for (const CFunction& f : st->functions()) any = any || st->float_format(f.module);
                    for (const jnext::dbg::CRecord& r : st->types()) any = any || st->float_format(r.module);
                    if (any) bad += " " + d.path().string();
                }
            }
        }
        check("Z88FP-02", "no module of any committed map of z88dk 2.4 (also v2.4-fp, the fpfmt.c build), master before 2026-11-06 or zsdcc has a float format", maps >= 100 && bad.empty());
        if (!bad.empty()) std::printf("  Z88FP-02: %d maps%s\n", maps, bad.c_str());
    }
    {   // Z88FP-03
        // GH #477: the three local constants sccz80 and 80cc write into every module they compile (z88dk#3240) are the module's float format
        int n_fp = 0;
        const auto fp = [](const char* name, int value, const char* module, const char* kind = "const") {
            char v[16];
            std::snprintf(v, sizeof v, "$%04X", value);
            return std::string(name) + " = " + v + " ; " + kind + ", local, , " + module + ", code_compiler, t.c::main::0::1:5\n";
        };
        const auto trio = [&](int mode, int bias, int ms, const char* module) {
            return fp("__fp_exponent_bias", bias, module) + fp("__fp_mantissa_size", ms, module) + fp("__fp_mode", mode, module);
        };
        const auto fmt_of = [&](const std::string& lines, const char* module) {
            SourceMap m;
            load(m, zb_map(("z88fp03_" + std::to_string(n_fp++) + ".map").c_str(), lines));
            const VariableStore* st = store_of(m);
            const jnext::dbg::CFloatFormat* f = st ? st->float_format(module) : nullptr;
            if (!st) return std::string("no store");
            if (!f) return std::string("none");
            return f->why.empty() ? std::to_string(f->mode) + "/" + std::to_string(f->exponent_bias) + "/" + std::to_string(f->mantissa_size) : "!" + f->why;
        };
        const std::string good = fmt_of(trio(1, 0x7E, 3, "t_c"), "t_c");
        const std::string only_mode = fmt_of(fp("__fp_mode", 0, "t_c"), "t_c");
        const std::string no_mant = fmt_of(fp("__fp_mode", 0, "t_c") + fp("__fp_exponent_bias", 128, "t_c"), "t_c");
        const std::string no_mode = fmt_of(fp("__fp_exponent_bias", 128, "t_c") + fp("__fp_mantissa_size", 5, "t_c"), "t_c");
        const std::string twice = fmt_of(trio(0, 128, 5, "t_c") + fp("__fp_mode", 1, "t_c"), "t_c");
        const std::string same = fmt_of(trio(0, 128, 5, "t_c") + fp("__fp_mode", 0, "t_c"), "t_c");
        const std::string two_mods_t = fmt_of(trio(0, 128, 5, "t_c") + trio(2, 128, 3, "u_c"), "t_c");
        const std::string two_mods_u = fmt_of(trio(0, 128, 5, "t_c") + trio(2, 128, 3, "u_c"), "u_c");
        const std::string addr_line = fmt_of(trio(0, 128, 5, "u_c") + fp("__fp_mode", 1, "t_c", "addr"), "t_c");
        const std::string no_module = fmt_of(fp("__fp_mode", 1, ""), "");
        const bool fp03 =
              good == "1/126/3" && only_mode == "!no __fp_exponent_bias" && no_mant == "!no __fp_mantissa_size" && no_mode == "!no __fp_mode" &&
                  twice == "!two __fp_mode values (0 and 1)" && same == "0/128/5" && two_mods_t == "0/128/5" && two_mods_u == "2/128/3" && addr_line == "none" && no_module == "none";
        if (!fp03) std::printf("  Z88FP-03: %s\n", (good + " | " + only_mode + " | " + no_mant + " | " + no_mode + " | " + twice + " | " + same + " | " + two_mods_t + " | " + two_mods_u + " | " + addr_line + " | " + no_module).c_str());
        check("Z88FP-03", "the float format of a module from its __fp_mode / __fp_exponent_bias / __fp_mantissa_size constants: $007E is 126; one without the others is unusable (no "
                          "__fp_exponent_bias, no __fp_mantissa_size, no __fp_mode); two different values of one name are unusable (two __fp_mode values (0 and 1)), the same value twice is fine; "
                          "two modules keep two formats; an addr line of that name and a line with no module are ignored",
              fp03);
    }

    fs::remove_all(dir);

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
