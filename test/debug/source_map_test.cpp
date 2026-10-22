// Source maps and compiler symbols — the format adapters and the neutral stores
// behind source-level debugging (doc/design/SOURCE-LEVEL-DEBUGGING.md):
//
//   SRCMAP-*   SourceMap: page-aware lookup, file:line resolution, identity
//   SRCRNG-*   SourceMap ranges: address ranges, overlap, line entries, "no code at"
//   SLD-*      the sjasmplus SLD v1 adapter, its validation and sidecar search
//   NBSYM-*    the NextBuild / Boriel ZX Basic Memory.txt adapter and
//              SymbolTable::resolve() for address fields
//
// Pure: no machine. The looping-link row (SLD-18) needs a symlink, so it is in
// source_debug_posix_test. Run: ./build/test/source_map_test

#include "debug/sld_loader.h"
#include "debug/source_file.h"
#include "debug/source_map.h"
#include "debug/symbol_table.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>
#include <unistd.h>
#include "../row_id.h"

namespace fs = std::filesystem;
using jnext::dbg::MemSpace;

// Counts every allocation, so SRCRNG-09 can prove lookup() makes none.
static long g_allocations = 0;
void* operator new(std::size_t n) {
    ++g_allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

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

// The device record sjasmplus writes for DEVICE ZXSPECTRUMNEXT.
static const char* const kNextDevice =
    "|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
    "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n";

int main() {
    std::printf("Source maps and compiler symbols\n");

    // A scratch directory unique to this process: concurrent runs from other
    // worktrees share the temp directory.
    const fs::path dir = fs::temp_directory_path() /
                         ("jnext-source-map-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    // ── SourceMap + SLD ─────────────────────────────────────────────────
    // Identity: SHA-256 of the four bytes 01 02 03 04 at $8000.
    const fs::path sld = dir / "game.sld";
    write_text(sld, std::string("|SLD.data.version|1\n") +
        "||program.name:game.bin\n"
        "||program.sha256:9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a\n"
        "||program.org:32768\n"
        "||program.size:4\n"
        "main.bas|6||0" + kNextDevice +
        "main.bas|6||0|4|32790|T|\n"
        "main.bas|7||0|4|32795|T|\n"
        "main.bas|8||0|4|32795|T|\n"
        "include/helper.bas|2:5:12||0|4|32825|T|\n"
        "main.bas|9||0|5|32790|T|\n"
        "main.bas|6||0|4|32790|L|,_Start,\n");

    SourceMap map;
    const SourceMapLoadResult loaded = load_sld(map, sld.string());
    check("SLD-01", "an SLD with five trace records loads five", loaded.count == 5);
    check("SLD-02", "label (L) records are not source traces; both records at one "
                    "address are kept", map.size() == 5);
    check("SLD-03", "the map remembers the file it came from",
          map.loaded_file() == sld.string());

    const uint8_t bytes[] = {1, 2, 3, 4};
    const auto identity = map.verify_program(
        [&](uint16_t a) { return static_cast<uint8_t>(bytes[(a - 0x8000) & 3]); });
    check("SRCMAP-01", "binary identity matches the bytes it was taken from",
          identity && *identity);
    const auto mismatch = map.verify_program([](uint16_t) { return uint8_t{0}; });
    check("SRCMAP-02", "binary identity reports different bytes as a mismatch",
          mismatch && !*mismatch);

    const auto at4 = map.lookup(MemSpace::page(4), 0x8016);
    check("SRCMAP-03", "lookup on the record's page finds its file and line",
          at4 && at4->file == "main.bas" && at4->line == 6);
    const auto at5 = map.lookup(MemSpace::page(5), 0x8016);
    check("SRCMAP-04", "the same logical address on another page is that page's record",
          at5 && at5->line == 9);
    check("SRCMAP-05", "an address on a page with no record has no source",
          !map.lookup(MemSpace::page(6), 0x8016));
    const auto dup = map.lookup(MemSpace::page(4), 0x801B);
    check("SRCMAP-06", "of two records at one address the later wins", dup && dup->line == 8);
    const auto helper = map.lookup(MemSpace::page(4), 0x8039);
    check("SRCMAP-07", "an included file keeps its path, and line:column parses",
          helper && helper->file == "include/helper.bas" && helper->line == 2 &&
              helper->column == 5);

    const auto exact = map.resolve("include/helper.bas:2").starts;
    check("SRCMAP-08", "file:line resolves to the line's address and page",
          exact.size() == 1 && exact[0].space == MemSpace::page(4) &&
              exact[0].address == 0x8039);
    const auto base = map.resolve("helper.bas:2").starts;
    check("SRCMAP-09", "a unique basename resolves like the full path",
          base.size() == 1 && base[0].address == 0x8039);
    check("SRCMAP-10", "a line that is not a number does not resolve",
          map.resolve("helper.bas:x").starts.empty());
    check("SRCMAP-11", "a line with no code does not resolve",
          map.resolve("main.bas:99").starts.empty());

    SourceMap same;
    same.replace({{MemSpace::page(0), 0x8000, 0x8001, {"x.bas", 3, 0}},
                  {MemSpace::page(4), 0x9000, 0x9001, {"x.bas", 3, 0}},
                  {MemSpace::page(0), 0xA000, 0xA001, {"x.bas", 4, 0}}},
                 {}, std::nullopt, "mem");
    const SourceLocation a = *same.lookup(MemSpace::page(0), 0x8000);
    const SourceLocation b = *same.lookup(MemSpace::page(4), 0x9000);
    const SourceLocation c = *same.lookup(MemSpace::page(0), 0xA000);
    check("SRCMAP-12", "same_position compares file, line and column, not address or page",
          SourceMap::same_position(a, b) && !SourceMap::same_position(a, c));

    {
        SourceMap m;
        m.replace({{MemSpace::page(4), 0x8000, 0x8001, {"w.asm", 3, 0}}}, {},
                  std::nullopt, "adapter");
        check("SRCMAP-15", "no identity metadata: verify_program() has no answer",
              !m.verify_program([](uint16_t) { return uint8_t{0}; }).has_value());
    }

    // Ambiguous basenames.
    {
        const fs::path amb = dir / "ambiguous.sld";
        write_text(amb, std::string("|SLD.data.version|1\n") +
            "one/helper.bas|2||0" + kNextDevice +
            "one/helper.bas|2||0|4|32768|T|\n"
            "two/helper.bas|2||0|4|32769|T|\n");
        SourceMap m;
        check("SLD-04", "two files with one basename load", load_sld(m, amb.string()).count == 2);
        check("SRCMAP-16", "an ambiguous basename does not resolve",
              m.resolve("helper.bas:2").starts.empty());
        check("SRCMAP-17", "the full path still resolves",
              !m.resolve("one/helper.bas:2").starts.empty());
    }

    // Validation, and a failed load leaves the live map alone.
    {
        SourceMap kept;
        load_sld(kept, sld.string());
        auto refused = [&](const char* name, const std::string& text) {
            const fs::path p = dir / name;
            write_text(p, text);
            const SourceMapLoadResult r = load_sld(kept, p.string());
            return r.count == -1 && !r.error.empty();
        };
        check("SLD-05", "a file that is not an SLD is refused with a reason",
              refused("bad.sld", "not an sld\n"));
        check("SLD-06", "a malformed line:column is refused",
              refused("pos.sld", std::string("|SLD.data.version|1\n") + "m.bas|1||0" +
                                     kNextDevice + "m.bas|2:5:x||0|4|32768|T|\n"));
        check("SLD-07", "a device model with 300 pages is refused",
              refused("pages.sld", std::string("|SLD.data.version|1\n") +
                  "m.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:300,slots.count:1,slots.adr:0\n"
                  "m.bas|1||0|4|32768|T|\n"));
        check("SLD-08", "pages that are not 8K are refused",
              refused("size.sld", std::string("|SLD.data.version|1\n") +
                  "m.bas|1||0|-1|-1|Z|pages.size:16384,pages.count:8,slots.count:4,"
                  "slots.adr:0,16384,32768,49152\n"
                  "m.bas|1||0|4|32768|T|\n"));
        check("SLD-09", "a trace before the device record is refused",
              refused("order.sld", std::string("|SLD.data.version|1\n") +
                  "m.bas|1||0|4|32768|T|\n" + "m.bas|1||0" + kNextDevice));
        check("SLD-10", "a trace on a page the device does not have is refused",
              refused("range.sld", std::string("|SLD.data.version|1\n") + "m.bas|1||0" +
                                       kNextDevice + "m.bas|1||0|224|32768|T|\n"));
        check("SLD-11", "an SLD with no traces is refused",
              refused("empty.sld", std::string("|SLD.data.version|1\n") + "m.bas|1||0" +
                                       kNextDevice));
        check("SLD-12", "identity metadata with a field missing is refused",
              refused("partial.sld", std::string("|SLD.data.version|1\n") +
                  "||program.sha256:9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a\n"
                  "m.bas|1||0" + kNextDevice + "m.bas|1||0|4|32768|T|\n"));
        {
            const fs::path p = dir / "sha63.sld";
            write_text(p, std::string("|SLD.data.version|1\n") +
                "||program.name:game.bin\n"
                "||program.sha256:9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806\n"
                "||program.org:32768\n"
                "||program.size:4\n"
                "m.bas|1||0" + kNextDevice + "m.bas|1||0|4|32768|T|\n");
            const SourceMapLoadResult r = load_sld(kept, p.string());
            check("SLD-21", "a program.sha256 of 63 hex digits is refused as an invalid SHA-256",
                  r.count == -1 && r.error.find("invalid SHA-256") != std::string::npos);
        }
        check("SLD-13", "a refused load leaves the live map in place",
              kept.size() == 5 && kept.lookup(MemSpace::page(4), 0x8016) != nullptr);
    }

    // CRLF line ends (an SLD edited or produced on Windows).
    {
        const fs::path crlf = dir / "crlf.sld";
        write_text(crlf, std::string("|SLD.data.version|1\r\n") +
            "c.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
            "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\r\n"
            "c.bas|4||0|4|32768|T|\r\n");
        SourceMap m;
        const auto r = load_sld(m, crlf.string());
        const auto at = m.lookup(MemSpace::page(4), 0x8000);
        check("SLD-14", "CRLF line ends load", r.count == 1 && at && at->line == 4);
    }

    // Sidecar search beside a program.
    {
        write_text(dir / "prog.sld.txt", std::string("|SLD.data.version|1\n") +
            "p.bas|1||0" + kNextDevice + "p.bas|1||0|4|32768|T|\n");
        SourceMap m;
        const auto r = load_sld_sidecar(m, (dir / "prog.nex").string());
        check("SLD-15", "<stem>.sld.txt beside the program is found",
              r.count == 1 && m.loaded_file() == (dir / "prog.sld.txt").string());
        write_text(dir / "prog.sld", std::string("|SLD.data.version|1\n") +
            "p.bas|1||0" + kNextDevice + "p.bas|1||0|4|32768|T|\np.bas|2||0|4|32769|T|\n");
        const auto r2 = load_sld_sidecar(m, (dir / "prog.nex").string());
        check("SLD-16", "<stem>.sld is preferred to <stem>.sld.txt",
              r2.count == 2 && m.loaded_file() == (dir / "prog.sld").string());
        const auto r3 = load_sld_sidecar(m, (dir / "other.nex").string());
        check("SLD-17", "no sidecar: refused, and the map is untouched",
              r3.count == -1 && m.size() == 2);
    }

    {
        // 225 pages: more than the Next's RAM, and page 0xFF must never be one.
        SourceMap m;
        const fs::path p = dir / "pages225.sld";
        write_text(p, std::string("|SLD.data.version|1\n") +
            "m.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:225,slots.count:1,slots.adr:0\n"
            "m.bas|1||0|4|0|T|\n");
        check("SLD-19", "a device model with more pages than the Next's 224 is refused",
              load_sld(m, p.string()).count == -1);
        const fs::path rel = dir / "rel";
        fs::create_directories(rel);
        write_text(rel / "r.sld", std::string("|SLD.data.version|1\n") + "m.bas|1||0" +
                                      kNextDevice + "m.bas|1||0|4|32768|T|\n");
        const fs::path before = fs::current_path();
        fs::current_path(rel);
        load_sld(m, "r.sld");
        fs::current_path(before);
        check("SLD-20", "a map loaded by a relative name records its absolute path",
              fs::path(m.loaded_file()).is_absolute());
    }

    // A file over the size cap is refused before it is read. Sparse, so the
    // fixture costs no disk.
    {
        const std::uintmax_t big = SOURCE_FILE_MAX_BYTES + 1;
        const fs::path huge_sld = dir / "huge.sld";
        write_text(huge_sld, std::string("|SLD.data.version|1\n") + "m.bas|1||0" +
                                 kNextDevice + "m.bas|1||0|4|32768|T|\n");
        fs::resize_file(huge_sld, big);
        SourceMap m;
        const auto r = load_sld(m, huge_sld.string());
        check("SLD-22", "an SLD over the 64 MiB cap is refused with its size in the reason",
              r.count == -1 && r.error.find(std::to_string(big)) != std::string::npos &&
                  m.size() == 0);

        const fs::path huge_mem = dir / "huge.Memory.txt";
        write_text(huge_mem, "8000: ._Big\n");
        fs::resize_file(huge_mem, big);
        const fs::path small_mem = dir / "small.Memory.txt";
        write_text(small_mem, "9000: ._Kept\n");
        SymbolTable st;
        st.load_nextbuild_memory(small_mem.string());
        std::string why;
        check("NBSYM-13", "a Memory.txt over the 64 MiB cap is refused with its size in the "
                          "reason, and the loaded symbols stay",
              st.load_nextbuild_memory(huge_mem.string(), &why) == -1 &&
                  why.find(std::to_string(big)) != std::string::npos &&
                  st.lookup_name("Kept") == 0x9000 && !st.lookup_name("Big"));

        // The cap is inclusive: a file of exactly SOURCE_FILE_MAX_BYTES is
        // not over it, and one byte more is.
        const fs::path edge = dir / "edge.sld";
        write_text(edge, "|SLD.data.version|1\n");
        fs::resize_file(edge, SOURCE_FILE_MAX_BYTES);
        const bool at_cap = source_file_problem(edge.string()).empty();
        fs::resize_file(edge, big);
        const std::string over = source_file_problem(edge.string());
        check("SLD-23", "a file of exactly the 64 MiB cap passes the size check, one byte "
                        "more is refused with its size",
              at_cap && over.find(std::to_string(big)) != std::string::npos);
    }

    // ── Ranges (GH #338, design 5.1 / 9.1) ──────────────────────────────
    // Oracle: the SLD records each row writes (sjasmplus emits one T record per
    // instruction) and the design's range semantics. No VHDL counterpart.
    {
        auto rec = [](const char* file, const char* pos, int page, unsigned addr) {
            return std::string(file) + "|" + pos + "||0|" + std::to_string(page) + "|" +
                   std::to_string(addr) + "|T|\n";
        };
        auto load = [&](SourceMap& m, const char* name, const std::string& first_file,
                        const std::string& records) {
            const fs::path p = dir / name;
            write_text(p, std::string("|SLD.data.version|1\n") + first_file + "|1||0" +
                              kNextDevice + records);
            return load_sld(m, p.string()).count;
        };
        const MemSpace p4 = MemSpace::page(4);
        auto is = [](const SourceLocation* l, const char* file, int line) {
            return l && l->file == file && l->line == line;
        };

        {
            SourceMap m;
            load(m, "rng1.sld", "x.asm", rec("x.asm", "1", 4, 0x8000) + rec("x.asm", "2", 4, 0x8003));
            check("SRCRNG-01", "an address inside a record's range, not only its first byte, "
                               "has the record's location; below the first record, none",
                  is(m.lookup(p4, 0x8001), "x.asm", 1) && is(m.lookup(p4, 0x8002), "x.asm", 1) &&
                      !m.lookup(p4, 0x7FFF));
            check("SRCRNG-02", "a record ends where the next one starts: the next address "
                               "has one location, not two",
                  is(m.lookup(p4, 0x8003), "x.asm", 2) && m.lookup_all(p4, 0x8003).size() == 1);
        }
        {
            SourceMap m;
            load(m, "rng3.sld", "x.asm", rec("x.asm", "1", 4, 0x9FFE) + rec("x.asm", "2", 4, 0xA002));
            check("SRCRNG-03", "an SLD record ends at its 8K page end: $A000-$A001 are no "
                               "statement, $A002 is the next",
                  is(m.lookup(p4, 0x9FFF), "x.asm", 1) && !m.lookup(p4, 0xA000) &&
                      !m.lookup(p4, 0xA001) && is(m.lookup(p4, 0xA002), "x.asm", 2));
        }
        {
            SourceMap m;
            m.replace({{p4, 0x8004, 0x8008, {"a.c", 7, 0}}, {p4, 0x8000, 0x8010, {"a.c", 3, 0}}},
                      {}, std::nullopt, "mem");
            const auto all = m.lookup_all(p4, 0x8005);
            check("SRCRNG-04", "of overlapping ranges the shortest is primary, even when a "
                               "longer one was emitted later; lookup_all lists both",
                  is(m.lookup(p4, 0x8005), "a.c", 7) && all.size() == 2 &&
                      is(all[0], "a.c", 7) && is(all[1], "a.c", 3) &&
                      is(m.lookup(p4, 0x8002), "a.c", 3));
        }
        {
            SourceMap m;
            m.replace({{p4, 0x8C95, 0x8C9A, {"main.c", 17, 0}}, {p4, 0x8C95, 0x8C9A, {"main.c", 16, 0}}},
                      {}, std::nullopt, "mem");
            const auto all = m.lookup_all(p4, 0x8C95);
            check("SRCRNG-05", "of equal ranges the later-emitted is primary, not the higher line",
                  is(m.lookup(p4, 0x8C95), "main.c", 16) && all.size() == 2 &&
                      is(all[0], "main.c", 16) && is(all[1], "main.c", 17));
        }
        {
            SourceMap m;
            load(m, "rng6.sld", "a.asm",
                 rec("a.asm", "10", 4, 0x8000) + rec("a.asm", "11", 4, 0x8003) +
                 rec("a.asm", "10", 4, 0x9000) + rec("a.asm", "12", 4, 0x9002) +
                 rec("a.asm", "10", 20, 0xC000));
            const auto st = m.resolve("a.asm:10").starts;
            check("SRCRNG-06", "a line compiled to several places resolves to every one, "
                               "sorted by space then address",
                  st.size() == 3 && st[0].space == p4 && st[0].address == 0x8000 &&
                      st[1].space == p4 && st[1].address == 0x9000 &&
                      st[2].space == MemSpace::page(20) && st[2].address == 0xC000);
        }
        {
            SourceMap m;
            m.replace({{p4, 0x8C92, 0x8C92, {"main.c", 14, 0}},
                       {p4, 0x8C92, 0x8C95, {"main.c", 15, 0}},
                       {p4, 0x8C95, 0x8C95, {"main.c", 16, 0}},
                       {p4, 0x8C95, 0x8C9A, {"main.c", 17, 0}}},
                      {{"main.c", 16, 3}}, std::nullopt, "mem");
            const auto r16 = m.resolve("main.c:16");
            const auto r14 = m.resolve("main.c:14");
            const auto all = m.lookup_all(p4, 0x8C95);
            check("SRCRNG-07", "a zero-width line resolves through its alias; one with none "
                               "is refused as \"no code at\"; a zero-width range covers nothing",
                  r16.starts.size() == 1 && r16.starts[0].space == p4 &&
                      r16.starts[0].address == 0x8C95 && r14.starts.empty() &&
                      r14.error == "no code at main.c:14" && all.size() == 1 &&
                      is(all[0], "main.c", 17));
        }
        {
            SourceMap m;
            load(m, "rng8.sld", "x.asm",
                 rec("x.asm", "5:3", 4, 0x8000) + rec("x.asm", "5:7", 4, 0x8001) +
                 rec("x.asm", "5:3", 4, 0x8002));
            const SourceLocation* a = m.lookup(p4, 0x8000);
            const SourceLocation* b = m.lookup(p4, 0x8001);
            const SourceLocation* c = m.lookup(p4, 0x8002);
            check("SRCRNG-08", "positions on one line differ by column",
                  a && b && c && a->line == 5 && b->line == 5 &&
                      !SourceMap::same_position(*a, *b) && SourceMap::same_position(*a, *c));
        }
        {
            SourceMap m;
            load(m, "rng9.sld", "x.asm", rec("x.asm", "1", 4, 0x8000) + rec("x.asm", "2", 4, 0x8003));
            const SourceLocation* first = m.lookup(p4, 0x8001);
            const bool same = first && first == m.lookup(p4, 0x8001) && first == m.lookup(p4, 0x8002) &&
                              first == m.lookup_all(p4, 0x8001)[0];
            const long before = g_allocations;
            long hits = 0;
            for (int i = 0; i < 1000; ++i)
                hits += m.lookup(p4, static_cast<uint16_t>(0x8000 + (i & 3))) != nullptr;
            const long made = g_allocations - before;
            static_assert(std::is_same_v<decltype(m.lookup(p4, 0)), const SourceLocation*>,
                          "lookup() returns the stored location by pointer");
            check("SRCRNG-09", "lookup() returns the one stored location, without copying "
                               "or allocating",
                  same && hits == 1000 && made == 0);
        }
        {
            SourceMap m;
            load(m, "rng10.sld", "src/game/main.asm",
                 rec("src/game/main.asm", "1", 4, 0x8000) + rec("lib/util.asm", "1", 4, 0x8010));
            const auto a = m.resolve("main.asm:1").starts;
            const auto b = m.resolve("game/main.asm:1").starts;
            const auto c = m.resolve("src/game/main.asm:1").starts;
            const SourceResolution d = m.resolve("ain.asm:1");
            const SourceResolution e = m.resolve("me/main.asm:1");
            check("SRCRNG-10", "a path matches by whole trailing components, never by "
                               "characters, and a refusal says nothing",
                  a.size() == 1 && b.size() == 1 && c.size() == 1 && a[0].address == 0x8000 &&
                      b[0].address == 0x8000 && c[0].address == 0x8000 && d.starts.empty() &&
                      d.error.empty() && e.starts.empty() && e.error.empty());
        }
        {
            SourceMap m;
            load(m, "rng11.sld", "x.asm",
                 rec("x.asm", "5:1", 4, 0x8000) + rec("x.asm", "5:13", 4, 0x8002) +
                 rec("x.asm", "7", 4, 0x8004) + rec("x.asm", "7", 4, 0x8005) +
                 rec("x.asm", "8", 4, 0x8006));
            const auto l5 = m.resolve("x.asm:5").starts;
            const auto l7 = m.resolve("x.asm:7").starts;
            check("SRCRNG-11", "a line's several instructions are one start: a range "
                               "that continues another of the same line is not an entry",
                  l5.size() == 1 && l5[0].address == 0x8000 && l7.size() == 1 &&
                      l7[0].address == 0x8004);
        }
        {
            SourceMap m;
            load(m, "rng12.sld", "x.asm", rec("x.asm", "1", 4, 0x8000));
            check("SRCRNG-12", "a record names its memory space: no other space, and no "
                               "other page, answers for it",
                  m.lookup(MemSpace::page(4), 0x8000) &&
                      !m.lookup(MemSpace::divmmc_ram(4), 0x8000) &&
                      !m.lookup(MemSpace::layer2(4), 0x8000) &&
                      !m.lookup(MemSpace::config(4), 0x8000) &&
                      !m.lookup(MemSpace::page(5), 0x8000) &&
                      !m.lookup(MemSpace::cpu(), 0x8000));
        }
        {
            // One copy ends exactly where the copy in another page starts: the
            // second is not a continuation of the first.
            SourceMap m;
            load(m, "rng13.sld", "lib.asm",
                 rec("lib.asm", "10", 4, 0x8000) + rec("other.asm", "1", 4, 0x8003) +
                 rec("lib.asm", "10", 5, 0x8003));
            const auto st = m.resolve("lib.asm:10").starts;
            check("SRCRNG-13", "copies of a line in two pages, one ending where the other "
                               "starts, are two starts",
                  st.size() == 2 && st[0].space == p4 && st[0].address == 0x8000 &&
                      st[1].space == MemSpace::page(5) && st[1].address == 0x8003);
        }
        {
            // Records written out of order, one of them twice.
            SourceMap m;
            load(m, "rng14.sld", "a.asm",
                 rec("a.asm", "10", 20, 0xC000) + rec("a.asm", "10", 4, 0x9000) +
                 rec("a.asm", "10", 4, 0x8000) + rec("b.asm", "1", 4, 0x8003) +
                 rec("a.asm", "10", 4, 0x8000));
            const auto st = m.resolve("a.asm:10").starts;
            check("SRCRNG-14", "starts come back sorted by space then address, each once, "
                               "whatever order the records were written in",
                  st.size() == 3 && st[0].space == p4 && st[0].address == 0x8000 &&
                      st[1].space == p4 && st[1].address == 0x9000 &&
                      st[2].space == MemSpace::page(20) && st[2].address == 0xC000);
        }
        {
            SourceMap m;
            load(m, "rng15.sld", "x.asm", rec("x.asm", "1", 4, 0x8000));
            check("SRCRNG-15", "the last record of a page ends at that page's 8K end, not "
                               "at the end of the address space",
                  is(m.lookup(p4, 0x9FFF), "x.asm", 1) && !m.lookup(p4, 0xA000) &&
                      !m.lookup(p4, 0xFFFF));
        }
    }

    // ── NextBuild Memory.txt ────────────────────────────────────────────
    {
        const fs::path mem = dir / "sample.Memory.txt";
        write_text(mem,
            "7320: .core.__START_PROGRAM\n"
            "7335: ._factoryFxDirty\n"
            "ACB5: ._FillScreen\n"
            "ACB5: .core.alias_at_same_address\n"
            "C735: ._RespawnPlayer\r\n"
            "10000: .too_wide\n"
            "GGGG: .not_hex\n"
            "missing colon\n");
        SymbolTable st;
        check("NBSYM-01", "five labels load; out-of-range, non-hex and malformed lines do not",
              st.load_nextbuild_memory(mem.string()) == 5);
        check("NBSYM-02", "one name per address: the first label there",
              st.size() == 4 && st.lookup(0xACB5) == std::optional<std::string>("FillScreen"));
        check("NBSYM-03", "a Boriel user label shows without '.' and the compiler's '_'",
              st.lookup(0x7335) == std::optional<std::string>("factoryFxDirty"));
        check("NBSYM-04", "a runtime label keeps its double underscore",
              st.lookup(0x7320) == std::optional<std::string>("core.__START_PROGRAM"));
        check("NBSYM-05", "the shown name, the raw label and the label without '.' all "
                          "resolve by name",
              st.lookup_name("FillScreen") == 0xACB5 && st.lookup_name("._FillScreen") == 0xACB5 &&
                  st.lookup_name("_FillScreen") == 0xACB5);
        check("NBSYM-06", "a second label at an address still resolves by name",
              st.lookup_name("core.alias_at_same_address") == 0xACB5);
        check("NBSYM-07", "a CRLF line loads",
              st.lookup(0xC735) == std::optional<std::string>("RespawnPlayer"));
        std::string why08;
        check("NBSYM-08", "an unreadable file is -1, and the reason is \"could not open "
                          "Memory.txt file\"",
              SymbolTable().load_nextbuild_memory((dir / "missing.txt").string(), &why08) == -1 &&
                  why08 == "could not open Memory.txt file");

        check("NBSYM-09", "resolve(): a symbol name", st.resolve(" RespawnPlayer ") == 0xC735);
        check("NBSYM-10", "resolve(): bare hex, $ and 0x",
              st.resolve("4000") == 0x4000 && st.resolve("$c000") == 0xC000 &&
                  st.resolve("0xFFFF") == 0xFFFF);
        check("NBSYM-11", "resolve(): wider than 16 bits, or not hex, is nothing",
              !st.resolve("10000") && !st.resolve("$10000") && !st.resolve("nope") &&
                  !st.resolve(""));
        SymbolTable hexy;
        const fs::path h = dir / "hexy.Memory.txt";
        write_text(h, "9000: .BEEF\n");
        hexy.load_nextbuild_memory(h.string());
        // OD-12 (#23): a typed number is the number; `@` forces the name.
        check("NBSYM-12", "a symbol spelled like hex is the number; @ forces the name",
              hexy.resolve("BEEF") == 0xBEEF && hexy.resolve("$BEEF") == 0xBEEF &&
                  hexy.resolve("@BEEF") == 0x9000);
        check("NBSYM-14", "@ resolves a name only: @ with no such symbol is nothing, "
                          "never the number",
              !hexy.resolve("@4000") && !hexy.resolve("@") &&
                  st.resolve("@ RespawnPlayer") == 0xC735);
    }

    fs::remove_all(dir);

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
