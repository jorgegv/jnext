// The z88dk `--list` listing adapter (GH #348, E23-17; design 2.3, 9.5):
//
//   Z88LIS-*   a z80asm listing beside the map gives the source lines the map
//              alone cannot (every assembly line, every occurrence of a C line,
//              sdcc's lines on z88dk 2.4, the start-up code), relocated by the
//              labels the map also holds, cross-checked against the map's own
//              line records, and dropped alone, with a note, when it is not this
//              build's.
//
// Oracle: the committed fixtures test/fixtures/z88dk (the listings, maps, section
// binaries and NEX files the two z88dk installs produced from our own sources),
// the listing format of z80asm's lstfile.cpp, and, for the rows that need an input
// no install was run on, a listing built in that exact format whose expected answer
// is the cited rule of design 9.5; the three that real z80asm output backs (PHASE,
// a page end, a DivMMC section) are the output of z80asm 2.4 on the 13-, 7- and
// 11-line sources quoted in their rows. No VHDL counterpart. Pure: no machine.
// Run: ./build/test/z88dk_lis_test

#include "debug/source_map.h"
#include "debug/symbol_table.h"
#include "debug/z88dk_lis.h"
#include "debug/z88dk_source_map.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
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

static const fs::path kFixtures = JNEXT_Z88DK_FIXTURE_DIR;

static std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

// What the NEX loader leaves mapped (nex_loader.cpp: MMU2-5 = 10, 11, 4, 5, then
// 0 and 1; slots 0 and 1 are the ROM).
static FetchSource nex_space(uint16_t address) {
    static const int kPage[8] = {-1, -1, 10, 11, 4, 5, 0, 1};
    const int slot = address >> 13;
    return slot < 2 ? MemSpace::rom(0) : MemSpace::page(static_cast<uint16_t>(kPage[slot]));
}

static const MemSpace P4 = MemSpace::page(4);
static const MemSpace P5 = MemSpace::page(5);
static const MemSpace P20 = MemSpace::page(20);

// The 8K pages a NEX file loads (banks in the file's order, 16K each from byte 512).
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

static uint8_t logical_junk(uint16_t) { return 0xA5; }

static bool is_at(const SourceLocation* l, const char* file, int line) {
    return l && l->file == file && l->line == line;
}

static bool starts_are(const SourceResolution& r, std::vector<SourceAddress> want) {
    if (r.starts.size() != want.size()) return false;
    for (size_t i = 0; i < want.size(); ++i)
        if (!(r.starts[i].space == want[i].space) || r.starts[i].address != want[i].address) return false;
    return true;
}

static bool any_note_has(const SourceMapLoadResult& r, const std::string& text) {
    for (const std::string& n : r.listing_notes)
        if (n.find(text) != std::string::npos) return true;
    return false;
}

// One listing row in z80asm's format (lstfile.cpp): `%6d  `, then a 4-digit address,
// two blanks, the bytes padded to 16 characters and two blanks, or 24 blanks; then
// the text.
static std::string lis_row(int line, const char* addr, const char* bytes, const std::string& text) {
    char buf[64];
    if (line > 0) std::snprintf(buf, sizeof buf, "%6d  ", line);
    else std::snprintf(buf, sizeof buf, "        ");
    std::string out = buf;
    if (addr) {
        std::snprintf(buf, sizeof buf, "%s  %-16s  ", addr, bytes);
        out += buf;
    } else {
        out += std::string(24, ' ');
    }
    return out + text + "\n";
}

// A temp copy of a fixture directory: the committed fixtures are never edited.
static fs::path copy_fixture(const fs::path& dir, const fs::path& from, const char* name) {
    const fs::path to = dir / name;
    fs::remove_all(to);
    fs::copy(from, to, fs::copy_options::recursive);
    return to;
}

// Replace the first `from` in a file; false when it is not there.
static bool edit_file(const fs::path& path, const std::string& from, const std::string& to) {
    std::string text = read_text(path);
    const size_t at = text.find(from);
    if (at == std::string::npos) return false;
    text.replace(at, from.size(), to);
    write_text(path, text);
    return true;
}

int main() {
    std::printf("z88dk --list listing adapter\n");

    const fs::path dir = fs::temp_directory_path() / ("jnext-z88dk-lis-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    const fs::path v24 = kFixtures / "v2.4";
    const fs::path master = kFixtures / "master";
    const char* const CRT24 = "z88dk/lib/config/../..//libsrc/_DEVELOPMENT/target/zxn/zxn_crt.asm.m4";
    const char* const DI24 = "z88dk/lib/config/../../libsrc/_DEVELOPMENT/target/zxn/../crt_start_di.inc";
    const char* const CRTM = "z88dk/lib/config/../..//libsrc/newlib/target/zxn/zxn_crt.asm.m4";
    const char* const DIM = "z88dk/lib/crt/newlib/crt_start_di.inc";
    const char* const EXITM = "z88dk/libsrc/newlib/target/zxn/crt_exit_basic.inc";

    auto load = [&](SourceMap& m, const fs::path& map, bool listings = true) {
        return load_z88dk_source_map(m, map.string(), nex_space,
                                     listings ? Z88dkListings::Beside : Z88dkListings::None);
    };

    // ── Z88LIS-01: relocation by the piece's labels (sdcc, z88dk 2.4) ──
    SourceMap so2;
    const auto so2_r = load(so2, v24 / "sdcc-SO2" / "prog.map");
    check("Z88LIS-01", "sdcc -SO2 on 2.4: the listings put main.c:17 at $8C6B (3 bytes at $8C6C), :18 at $8C6F, util.c:3 at $8CCA, asmfn.asm:6 at $8CDB; 4 listings, 230 ranges, no note",
          so2_r && is_at(so2.lookup(P4, 0x8C6B), "main.c", 17) && is_at(so2.lookup(P4, 0x8C6C), "main.c", 17) &&
              is_at(so2.lookup(P4, 0x8C6E), "main.c", 17) && is_at(so2.lookup(P4, 0x8C6F), "main.c", 18) &&
              is_at(so2.lookup(P4, 0x8CCA), "util.c", 3) && is_at(so2.lookup(P4, 0x8CDB), "asmfn.asm", 6) &&
              so2_r.listings == 4 && so2_r.count == 230 && so2_r.listing_notes.empty());

    // ── Z88LIS-02: every occurrence of a C line (sdcc comments; the map has no line record) ──
    check("Z88LIS-02", "main.c:18 starts at $8C6F and $8C89 (the for loop's init and its back edge); $8C84 is :20, $8C8D :18, $8C8F :22",
          starts_are(so2.resolve("main.c:18"), {{P4, 0x8C6F}, {P4, 0x8C89}}) &&
              is_at(so2.lookup(P4, 0x8C84), "main.c", 20) && is_at(so2.lookup(P4, 0x8C89), "main.c", 18) &&
              is_at(so2.lookup(P4, 0x8C8D), "main.c", 18) && is_at(so2.lookup(P4, 0x8C8F), "main.c", 22));

    // ── Z88LIS-03: zero-width lines and the optimiser ──
    SourceMap so0;
    const auto so0_r = load(so0, v24 / "sdcc-SO0" / "prog.map");
    check("Z88LIS-03", "-SO2: main.c:29 owns $8CB9-$8CBA and :28, :25, :14 resolve to $8CB9, $8CB5, $8C6B; -SO0 (no CRT listing): the jp is $8CBF-$8CC1 as :26, :29 is the ret at $8CC2, :28 resolves to it",
          is_at(so2.lookup(P4, 0x8CB9), "main.c", 29) && is_at(so2.lookup(P4, 0x8CBA), "main.c", 29) &&
              starts_are(so2.resolve("main.c:28"), {{P4, 0x8CB9}}) &&
              starts_are(so2.resolve("main.c:25"), {{P4, 0x8CB5}}) &&
              starts_are(so2.resolve("main.c:14"), {{P4, 0x8C6B}}) &&
              so0_r && so0_r.listings == 3 && is_at(so0.lookup(P4, 0x8CBF), "main.c", 26) &&
              is_at(so0.lookup(P4, 0x8CC1), "main.c", 26) && is_at(so0.lookup(P4, 0x8CC2), "main.c", 29) &&
              starts_are(so0.resolve("main.c:28"), {{P4, 0x8CC2}}));

    // ── Z88LIS-04: one line, many addresses ──
    check("Z88LIS-04", "util.h:7 starts in main.c's copy of the inline function ($8C48), util.c's ($8CBB) and the one inlined into util_twice ($8CD6); $8CD5 is util.c:6, $8CD7 util.c:8",
          starts_are(so2.resolve("util.h:7"), {{P4, 0x8C48}, {P4, 0x8CBB}, {P4, 0x8CD6}}) &&
              is_at(so2.lookup(P4, 0x8CD5), "util.c", 6) && is_at(so2.lookup(P4, 0x8CD6), "util.h", 7) &&
              is_at(so2.lookup(P4, 0x8CD7), "util.c", 8));

    // ── Z88LIS-05: assembly, every line, exact ──
    SourceMap sc_none;
    SourceMap sc;
    const auto sc_r = load(sc, v24 / "sccz80-debug" / "prog.map");
    load(sc_none, v24 / "sccz80-debug" / "prog.map", false);
    check("Z88LIS-05", "asmfn.asm: ld hl,_counter is exactly $8CDB-$8CDD, then :7, :8 (2 bytes), :9, :11; :5 and :10 are the labels' addresses, :4 has no code; with the map alone sccz80's $8D68 is :5, with the listing :7",
          is_at(so2.lookup(P4, 0x8CDB), "asmfn.asm", 6) && is_at(so2.lookup(P4, 0x8CDD), "asmfn.asm", 6) &&
              is_at(so2.lookup(P4, 0x8CDE), "asmfn.asm", 7) && is_at(so2.lookup(P4, 0x8CDF), "asmfn.asm", 8) &&
              is_at(so2.lookup(P4, 0x8CE0), "asmfn.asm", 8) && is_at(so2.lookup(P4, 0x8CE1), "asmfn.asm", 9) &&
              is_at(so2.lookup(P4, 0x8CE2), "asmfn.asm", 11) &&
              starts_are(so2.resolve("asmfn.asm:5"), {{P4, 0x8CDB}}) &&
              starts_are(so2.resolve("asmfn.asm:10"), {{P4, 0x8CE2}}) &&
              so2.resolve("asmfn.asm:4").error == "no code at asmfn.asm:4" &&
              sc_r && is_at(sc.lookup(P4, 0x8D68), "asmfn.asm", 7) &&
              is_at(sc_none.lookup(P4, 0x8D68), "asmfn.asm", 5));

    // ── Z88LIS-06: a PAGE_20 section ──
    {
        SourceMap m;
        const auto r = load(m, v24 / "sdcc-banked" / "prog.map");
        const NexMemory nex(v24 / "sdcc-banked" / "prog.nex");
        check("Z88LIS-06", "sdcc-banked on 2.4 (PAGE_20): $C000 is banked.c:5 and $C008 :8 in page 20 only; the byte checks of the listing's ranges verify against prog.nex",
              r && nex.ok && is_at(m.lookup(P20, 0xC000), "banked.c", 5) && is_at(m.lookup(P20, 0xC008), "banked.c", 8) &&
                  !m.lookup(MemSpace::page(0), 0xC000) &&
                  m.verify_program(logical_junk, nex.reader()) == std::optional<bool>(true));
    }

    // ── Z88LIS-07: the cross-check agrees under z80asm's own rules ──
    {
        SourceMap m1;
        SourceMap m2;
        SourceMap m3;
        const auto r1 = load(m1, master / "sccz80-debug" / "prog.map");
        const auto r2 = load(m2, master / "80cc-debug" / "prog.map");
        SourceMap m4;
        const auto r4 = load(m4, master / "sdcc-debug" / "prog.map");
        check("Z88LIS-07", "every map line record is where the listing first defines it: 2.4 sccz80 24/0, master sccz80 24/0, 80cc 20/0, master sdcc 54/0 (28 user + 26 CRT records), no note",
              sc_r && sc_r.listing_checks == 24 && sc_r.listing_disagreements == 0 && sc_r.listing_notes.empty() &&
                  r1 && r1.listing_checks == 24 && r1.listing_disagreements == 0 && r1.listing_notes.empty() &&
                  r2 && r2.listing_checks == 20 && r2.listing_disagreements == 0 && r2.listing_notes.empty() &&
                  r4 && r4.listing_checks == 54 && r4.listing_disagreements == 0 && r4.listing_notes.empty());
    }

    // ── Z88LIS-08: a disagreement is reported and the listing wins ──
    {
        const fs::path d = copy_fixture(dir, v24 / "sccz80-debug", "z08");
        const bool edited = edit_file(d / "prog.map", "_3a_3a3_3a_3a4 = $8CBE", "_3a_3a3_3a_3a4 = $8CC0");
        SourceMap m;
        const auto r = load(m, d / "prog.map");
        check("Z88LIS-08", "a map record moved from $8CBE to $8CC0: the load succeeds, 1 disagreement, the note names main.c::main::3::4:20 with both addresses, and the listing's $8CBE is still main.c:20",
              edited && r && r.listing_disagreements == 1 && r.listing_checks == 24 &&
                  any_note_has(r, "main.c::main::3::4:20") && any_note_has(r, "$8CC0") && any_note_has(r, "$8CBE") &&
                  is_at(m.lookup(P4, 0x8CBE), "main.c", 20));
    }

    // ── Z88LIS-09: a module without a listing keeps its map records ──
    {
        SourceMap m;
        const fs::path d = copy_fixture(dir, v24 / "sccz80-debug", "z09");
        fs::remove(d / "asmfn.asm.lis");
        const auto r = load(m, d / "prog.map");
        check("Z88LIS-09", "no prog.lis: the start-up code keeps the map's records ($8000 is the CRT's :9462), 161 ranges = 135 from the listings + 26 CRT; without asmfn.asm.lis that module falls back to the map ($8D68 is :5), 2 listings",
              sc_r && sc_r.count == 161 && sc_r.listings == 3 && sc.lookup(P4, 0x8000) &&
                  sc.lookup(P4, 0x8000)->file == CRT24 && sc.lookup(P4, 0x8000)->line == 9462 &&
                  r && r.listings == 2 && is_at(m.lookup(P4, 0x8D68), "asmfn.asm", 5));
    }

    // ── Z88LIS-10: the start-up code from prog.lis ──
    {
        SourceMap mm;
        const auto rm = load(mm, master / "sdcc-debug" / "prog.map");
        const SourceText* tx = so2.embedded_text(CRT24);
        const SourceText* txm = mm.embedded_text(CRTM);
        check("Z88LIS-10", "prog.lis gives the CRT its lines on 2.4 and master: $8000 is the .inc's :3, $8001 the m4's :9470, :9462 resolves to $8000, $800C (code_crt_init, no label) :9148, $8032 :9524, $8036 the exit .inc's :3; the m4 text has 9689 lines (9470 = push iy, 9462 = __Start:) from prog.lis; the .inc has none",
              so2_r && rm &&
                  is_at(so2.lookup(P4, 0x8000), DI24, 3) && is_at(so2.lookup(P4, 0x8001), CRT24, 9470) &&
                  is_at(so2.lookup(P4, 0x8002), CRT24, 9470) &&
                  starts_are(so2.resolve("zxn_crt.asm.m4:9462"), {{P4, 0x8000}}) &&
                  is_at(so2.lookup(P4, 0x800C), CRT24, 9148) && is_at(so2.lookup(P4, 0x8032), CRT24, 9524) &&
                  is_at(mm.lookup(P4, 0x8000), DIM, 3) && is_at(mm.lookup(P4, 0x8001), CRTM, 9470) &&
                  is_at(mm.lookup(P4, 0x800C), CRTM, 9148) && is_at(mm.lookup(P4, 0x8032), CRTM, 9524) &&
                  is_at(mm.lookup(P4, 0x8036), EXITM, 3) &&
                  tx && tx->lines.size() == 9689 && tx->lines[9469].find("push iy") != std::string::npos &&
                  tx->lines[9461].find("__Start:") != std::string::npos &&
                  tx->origin.size() >= 8 && tx->origin.compare(tx->origin.size() - 8, 8, "prog.lis") == 0 &&
                  txm && txm->lines.size() == 9689 && !so2.embedded_text(DI24));
    }

    // ── Z88LIS-11: a label in a false IF branch is not an anchor ──
    {
        SourceMap mm;
        load(mm, master / "sdcc-debug" / "prog.map");
        check("Z88LIS-11", "__stdio_closed_file_list is listed at m4 :9207 (bytes) and :9266 (a false IF branch, no bytes): $8EC4 is :9207 and prog.lis has no unplaced line; master has it at $8EB5",
              so2_r && is_at(so2.lookup(P4, 0x8EC4), CRT24, 9207) && !any_note_has(so2_r, "prog.lis") &&
                  is_at(mm.lookup(P4, 0x8EB5), CRTM, 9207));
    }

    // ── Z88LIS-12: a section switch ends a C attribution ──
    check("Z88LIS-12", "sccz80: _counter's defs 1 in bss_compiler after the code's last C_LINE 28 has no line ($8F59), the code's own last line ($8D2D) is main.c:28",
          sc_r && !sc.lookup(P4, 0x8F59) && is_at(sc.lookup(P4, 0x8D2D), "main.c", 28));

    // ── Z88LIS-13: a stale listing is dropped and the rest is used ──
    {
        const fs::path a = copy_fixture(dir, v24 / "sdcc-SO2", "z13a");
        const bool ea = edit_file(a / "main.c.lis", "0023  af", "0023  b7");
        SourceMap ma;
        const auto ra = load(ma, a / "prog.map");
        const fs::path b = copy_fixture(dir, v24 / "sdcc-SO2", "z13b");
        const bool eb = edit_file(b / "prog.map", "l_main_00107                    = $8C71", "l_main_00107                    = $8C72");
        SourceMap mb;
        const auto rb = load(mb, b / "prog.map");
        check("Z88LIS-13", "(a) one opcode byte of main.c.lis changed: that listing alone is dropped, the note names it, its line 294 and $8C6B, the others are used; (b) a label moved by one in the map: main.c.lis is dropped and the note names _util_inc and l_main_00107",
              ea && ra && ra.listings == 3 && ra.count > 0 && any_note_has(ra, "main.c.lis") &&
                  any_note_has(ra, "line 294") && any_note_has(ra, "$8C6B") && !ma.lookup(P4, 0x8C6B) &&
                  is_at(ma.lookup(P4, 0x8CDB), "asmfn.asm", 6) &&
                  eb && rb && rb.listings == 3 && any_note_has(rb, "main.c.lis") &&
                  any_note_has(rb, "_util_inc") && any_note_has(rb, "l_main_00107") && !mb.lookup(P4, 0x8C6B));
    }

    // ── Z88LIS-14: an sdcc listing without line information ──
    {
        const fs::path d = copy_fixture(dir, v24 / "sdcc-SO2", "z14");
        std::istringstream in(read_text(d / "main.c.lis"));
        std::string out;
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() > 33 && line[32] == ';' &&
                (line.compare(33, 7, "main.c:") == 0 || line.compare(33, 7, "util.h:") == 0))
                continue;
            out += line + "\n";
        }
        write_text(d / "main.c.lis", out);
        SourceMap m;
        const auto r = load(m, d / "prog.map");
        check("Z88LIS-14", "main.c.lis without its ;main.c:N: comments: no main.c ranges, a note naming it and --c-code-in-asm, the util, asm and CRT ranges are still there",
              r && !m.lookup(P4, 0x8C6B) && m.resolve("main.c:17").starts.empty() &&
                  any_note_has(r, "main.c.lis") && any_note_has(r, "--c-code-in-asm") &&
                  is_at(m.lookup(P4, 0x8CCA), "util.c", 3) && is_at(m.lookup(P4, 0x8CDB), "asmfn.asm", 6) &&
                  is_at(m.lookup(P4, 0x8001), CRT24, 9470));
    }

    // ── Z88LIS-15: the master sdcc C_LINE form ──
    {
        SourceMap mm;
        SourceMap mmap;
        const fs::path map = master / "sdcc-debug" / "prog.map";
        const auto rm = load(mm, map);
        load(mmap, map, false);
        check("Z88LIS-15", "master sdcc: $8CAC is util.h:9 as the listing's C_LINE says; main.c:18 starts at $8C6F and $8C89, :14 at $8C6B; _counter's DEFS ($8EBE, before the first C_LINE) has no line, and main.c:224 (the generated asm line the map names) is the map's alone",
              rm && is_at(mm.lookup(P4, 0x8CAC), "util.h", 9) &&
                  starts_are(mm.resolve("main.c:18"), {{P4, 0x8C6F}, {P4, 0x8C89}}) &&
                  starts_are(mm.resolve("main.c:14"), {{P4, 0x8C6B}}) && !mm.lookup(P4, 0x8EBE) &&
                  mm.resolve("main.c:224").error == "no code at main.c:224" &&
                  starts_are(mmap.resolve("main.c:224"), {{P4, 0x8EBE}}));
    }

    // ── Z88LIS-16: discovery ──
    {
        SourceMap m;
        const auto r = load(m, v24 / "sccz80-subdir" / "prog.map");
        const fs::path d = copy_fixture(dir, v24 / "sdcc-SO2", "z16");
        fs::copy_file(d / "asmfn.asm.lis", d / "util.c.lis", fs::copy_options::overwrite_existing);
        SourceMap ms;
        const auto rs = load(ms, d / "prog.map");
        check("Z88LIS-16", "game/util.c.lis is found from the map's module symbols (3 listings; game/util.c:4 and :6 at $8D35, $8D38; :5 an alias of :6); a util.c.lis that is asmfn_asm's is ignored with a note naming both modules and gives util_c no ranges",
              r && r.listings == 3 && is_at(m.lookup(P4, 0x8D35), "game/util.c", 4) &&
                  is_at(m.lookup(P4, 0x8D38), "game/util.c", 6) &&
                  starts_are(m.resolve("game/util.c:5"), {{P4, 0x8D38}}) &&
                  rs && rs.listings == 3 && any_note_has(rs, "util_c") && any_note_has(rs, "asmfn_asm") &&
                  !ms.lookup(P4, 0x8CCA));
    }

    // ── Z88LIS-17: PHASE (z80asm 2.4 output of p.asm) ──
    {
        std::string lis = "p.asm:\n";
        lis += lis_row(1, nullptr, nullptr, " SECTION code_user");
        lis += lis_row(2, nullptr, nullptr, " ORG $8000");
        lis += lis_row(3, nullptr, nullptr, " PUBLIC _f");
        lis += lis_row(4, nullptr, nullptr, "_f:");
        lis += lis_row(5, "0000", "00", " nop");
        lis += lis_row(6, "0001", "210060", " ld hl,copy");
        lis += lis_row(7, nullptr, nullptr, " PHASE $6000");
        lis += lis_row(8, nullptr, nullptr, "copy:");
        lis += lis_row(9, "6000", "3e01", " ld a,1");
        lis += lis_row(10, "6002", "c30060", " jp copy");
        lis += lis_row(11, nullptr, nullptr, " DEPHASE");
        lis += lis_row(12, nullptr, nullptr, "after:");
        lis += lis_row(13, "0009", "c9", " ret");
        lis += lis_row(14, nullptr, nullptr, "");
        write_text(dir / "z17" / "p.asm.lis", lis);
        write_text(dir / "z17" / "p.map",
            "__code_user_head                = $8000 ; const, public, def, , ,\n"
            "__code_user_size                = $000A ; const, public, def, , ,\n"
            "__code_user_tail                = $800A ; const, public, def, , ,\n"
            "__head                          = $0000 ; const, public, def, , ,\n"
            "__size                          = $800A ; const, public, def, , ,\n"
            "__tail                          = $800A ; const, public, def, , ,\n"
            "_f                              = $8000 ; addr, public, , p, code_user, p.asm:4\n"
            "after                           = $8009 ; addr, local, , p, code_user, p.asm:12\n"
            "copy                            = $6000 ; const, local, , p, code_user, p.asm:8\n");
        SourceMap m;
        const auto r = load(m, dir / "z17" / "p.map");
        check("Z88LIS-17", "PHASE: the pc runs on by the byte count, not the phased address ($8009 is p.asm:13 after 9 bytes), the phased rows are not placed ($8004 and $6000 have no line), and a note counts the 2 lines",
              r && is_at(m.lookup(P4, 0x8000), "p.asm", 5) && is_at(m.lookup(P4, 0x8001), "p.asm", 6) &&
                  is_at(m.lookup(P4, 0x8009), "p.asm", 13) && !m.lookup(P4, 0x8004) &&
                  !m.lookup(MemSpace::page(0), 0xE000) && any_note_has(r, "2 listed lines"));
    }

    // ── Z88LIS-18: CRLF ──
    {
        const fs::path d = copy_fixture(dir, v24 / "sdcc-SO2", "z18");
        std::istringstream in(read_text(d / "asmfn.asm.lis"));
        std::string out;
        std::string line;
        while (std::getline(in, line)) out += line + "\r\n";
        write_text(d / "asmfn.asm.lis", out);
        SourceMap m;
        const auto r = load(m, d / "prog.map");
        check("Z88LIS-18", "a CRLF asmfn.asm.lis gives the same lines as the LF one",
              r && r.count == so2_r.count && r.listings == 4 && is_at(m.lookup(P4, 0x8CDB), "asmfn.asm", 6) &&
                  is_at(m.lookup(P4, 0x8CE2), "asmfn.asm", 11) &&
                  starts_are(m.resolve("asmfn.asm:5"), {{P4, 0x8CDB}}));
    }

    // ── Z88LIS-19: the size rule needs equal sizes ──
    {
        const fs::path d = copy_fixture(dir, master / "sdcc-debug", "z19");
        const bool edited = edit_file(d / "prog.map", "__code_crt_init_tail            = $8032", "__code_crt_init_tail            = $8033");
        SourceMap m;
        const auto r = load(m, d / "prog.map");
        check("Z88LIS-19", "code_crt_init one byte longer in the map than in prog.lis: its 17 label-less lines are not placed ($800C has no line, a note counts 17), the rest of the CRT is",
              edited && r && !m.lookup(P4, 0x800C) && any_note_has(r, "17 listed lines") &&
                  is_at(m.lookup(P4, 0x8001), CRTM, 9470));
    }

    // ── Z88LIS-20: sccz80 zero-width lines ──
    check("Z88LIS-20", "sccz80: main.c:14 (square's scope, before _main) has no code, main.c:16 resolves to $8C95",
          sc_r && sc.resolve("main.c:14").error == "no code at main.c:14" &&
              starts_are(sc.resolve("main.c:16"), {{P4, 0x8C95}}));

    // ── Z88LIS-21: an instruction across a page end (z80asm 2.4 output of s.asm) ──
    {
        std::string lis = "s.asm:\n";
        lis += lis_row(1, nullptr, nullptr, " SECTION code_user");
        lis += lis_row(2, nullptr, nullptr, " ORG $9FFE");
        lis += lis_row(3, nullptr, nullptr, " PUBLIC _g");
        lis += lis_row(4, nullptr, nullptr, "_g:");
        lis += lis_row(5, "0000", "00", " nop");
        lis += lis_row(6, "0001", "210100", " ld hl,1");
        lis += lis_row(7, "0004", "c9", " ret");
        lis += lis_row(8, nullptr, nullptr, "");
        write_text(dir / "z21" / "s.asm.lis", lis);
        write_text(dir / "z21" / "s.map",
            "__code_user_head                = $9FFE ; const, public, def, , ,\n"
            "__code_user_size                = $0005 ; const, public, def, , ,\n"
            "__code_user_tail                = $A003 ; const, public, def, , ,\n"
            "_g                              = $9FFE ; addr, public, , s, code_user, s.asm:4\n");
        SourceMap m;
        const auto r = load(m, dir / "z21" / "s.map");
        check("Z88LIS-21", "ld hl,1 at $9FFF runs over the page end: its range stops at $A000 in page 4 (nothing at $A000 in page 5), and the ret at $A002 is in page 5",
              r && is_at(m.lookup(P4, 0x9FFF), "s.asm", 6) && !m.lookup(P5, 0xA000) &&
                  is_at(m.lookup(P5, 0xA002), "s.asm", 7));
    }

    // ── Z88LIS-22: a listing record in an unsupported space drops the whole listing ──
    {
        std::string lis = "d.asm:\n";
        lis += lis_row(1, nullptr, nullptr, " SECTION DIV_1");
        lis += lis_row(2, nullptr, nullptr, " ORG $2000");
        lis += lis_row(3, nullptr, nullptr, " PUBLIC _d");
        lis += lis_row(4, nullptr, nullptr, "_d:");
        lis += lis_row(5, "0000", "00", " nop");
        lis += lis_row(6, "0001", "c9", " ret");
        lis += lis_row(7, nullptr, nullptr, " SECTION code_user");
        lis += lis_row(8, nullptr, nullptr, " ORG $8000");
        lis += lis_row(9, nullptr, nullptr, " PUBLIC _e");
        lis += lis_row(10, nullptr, nullptr, "_e:");
        lis += lis_row(11, "0000", "c9", " ret");
        lis += lis_row(12, nullptr, nullptr, "");
        write_text(dir / "z22" / "d.asm.lis", lis);
        write_text(dir / "z22" / "d.map",
            "__DIV_1_head                    = $2000 ; const, public, def, , ,\n"
            "__DIV_1_size                    = $0002 ; const, public, def, , ,\n"
            "__DIV_1_tail                    = $2002 ; const, public, def, , ,\n"
            "__code_user_head                = $8000 ; const, public, def, , ,\n"
            "__code_user_size                = $0001 ; const, public, def, , ,\n"
            "__code_user_tail                = $8001 ; const, public, def, , ,\n"
            "_d                              = $2000 ; addr, public, , d, DIV_1, d.asm:4\n"
            "_e                              = $8000 ; addr, public, , d, code_user, d.asm:10\n");
        SourceMap m;
        const auto r = load(m, dir / "z22" / "d.map");
        check("Z88LIS-22", "a DivMMC section in the listing drops the whole listing: no source lines, a note naming DIV_1, and the code_user part is not kept either",
              r && r.count == 0 && r.listings == 0 && any_note_has(r, "DIV_1") && !m.lookup(P4, 0x8000));
    }

    fs::remove_all(dir);

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
