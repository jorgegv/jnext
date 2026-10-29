// The z88dk `-debug` map adapter (GH #344, E23-13; design 9.1, 9.4, 9.7):
//
//   Z88MAP-*   the section-to-page rules, the line records into SourceMap
//              ranges (extents, zero-width lines, strict refusal), the program
//              identity from the section binaries, and the symbol loaders'
//              file-type residual
//
// Oracle: the committed fixtures test/fixtures/z88dk (what the two z88dk installs
// produced from our own sources, with the NEX placement and the .lis listings next
// to each map) and, for the rows that need a map no install produced, a map in the
// real line format whose expected answer is the cited rule of the design. No VHDL
// counterpart. Pure: no machine; the NEX files are read by the test itself.
// Run: ./build/test/z88dk_map_test

#include "debug/source_file.h"
#include "debug/source_map.h"
#include "debug/symbol_table.h"
#include "debug/z88dk_source_map.h"
#include "debug/z88dk_space.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>
#include "../row_id.h"

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
                  rf && mf.lookup(P20, 0x8100) && !mf.verify_program(logical_junk, all22).has_value());
    }

    // ── Z88MAP-15: the symbol half is the symbol loader's ──
    {
        SymbolTable st;
        const int n = st.load_z88dk_map((v24 / "sccz80-debug" / "prog.map").string());
        const auto key = st.find_name("_main");
        check("Z88MAP-15", "the same map still gives its 19 addr symbols, with _main at $8C92 and no line record among them",
              n == 19 && st.lookup(MemSpace::cpu(), 0x8C92) == std::string("_main") && key && key->addr == 0x8C92);
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
        check("Z88MAP-19", "sccz80 splits a statement over two lines on 2.4 (19 ends at $8CBD, 20 starts at $8CBE) and on master at $8CCD",
              r && is_at(sc.lookup(P4, 0x8CBD), "main.c", 19) && is_at(sc.lookup(P4, 0x8CBE), "main.c", 20) &&
                  is_at(m.lookup(P4, 0x8CCC), "main.c", 19) && is_at(m.lookup(P4, 0x8CCD), "main.c", 20));
    }
    {
        SourceMap m20;
        const auto r = load(m20, v24 / "sccz80-page20" / "prog.map");
        check("Z88MAP-20", "a last line ends at its section's tail: asmfn.asm:10 at $8D6C (__code_user_tail $8D6D), banked.c:9 at $C023 (__PAGE_20_tail $C024)",
              is_at(sc.lookup(P4, 0x8D6C), "asmfn.asm", 10) && !sc.lookup(P4, 0x8D6D) &&
                  r && is_at(m20.lookup(P20, 0xC023), "banked.c", 9) && !m20.lookup(P20, 0xC024));
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
        check("Z88MAP-24", "a DIV section, a PAGE section with no __PAGE_n_head and a page out of range each refuse the half, naming the line",
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
        SourceMap loaded;
        load(loaded, v24 / "sccz80-debug" / "prog.map");
        const std::string before = loaded.loaded_file();
        const auto rn = load(loaded, kNoDebugMap);
        check("Z88MAP-25", "an unparsable location and a value over 24 bits refuse the half naming line 2; a map with no line records gives 0, no error, and leaves the store alone",
              !rl && rl.error.find("line 2") != std::string::npos && !rv && rv.error.find("line 2") != std::string::npos &&
                  !rb && rb.error.find("line 2") != std::string::npos && bad_bank.empty() &&
                  bad_loc.empty() && bad_value.empty() &&
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
                  z88dk_section_space("code_compiler", 0x8000, no_head).kind == Z88dkSpace::Kind::BindAtLoad);
    }

    fs::remove_all(dir);

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
