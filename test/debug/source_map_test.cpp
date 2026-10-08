// Source maps and compiler symbols — the format adapters and the neutral stores
// behind source-level debugging (doc/design/SOURCE-LEVEL-DEBUGGING.md):
//
//   SRCMAP-*   SourceMap: page-aware lookup, file:line resolution, identity
//   SLD-*      the sjasmplus SLD v1 adapter, its validation and sidecar search
//   NBSYM-*    the NextBuild / Boriel ZX Basic Memory.txt adapter and
//              SymbolTable::resolve() for address fields
//
// Pure: no machine. Run: ./build/test/source_map_test

#include "debug/sld_loader.h"
#include "debug/source_map.h"
#include "debug/symbol_table.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include "../row_id.h"

namespace fs = std::filesystem;

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
    check("SLD-02", "label (L) records are not source traces; a duplicate address "
                    "keeps one record", map.size() == 4);
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

    const auto at4 = map.lookup(4, 0x8016);
    check("SRCMAP-03", "lookup on the record's page finds its file and line",
          at4 && at4->file == "main.bas" && at4->line == 6);
    const auto at5 = map.lookup(5, 0x8016);
    check("SRCMAP-04", "the same logical address on another page is that page's record",
          at5 && at5->line == 9);
    check("SRCMAP-05", "an address on a page with no record has no source",
          !map.lookup(6, 0x8016));
    const auto dup = map.lookup(4, 0x801B);
    check("SRCMAP-06", "of two records at one address the later wins", dup && dup->line == 8);
    const auto helper = map.lookup(4, 0x8039);
    check("SRCMAP-07", "an included file keeps its path, and line:column parses",
          helper && helper->file == "include/helper.bas" && helper->line == 2 &&
              helper->column == 5);

    const auto exact = map.resolve("include/helper.bas:2");
    check("SRCMAP-08", "file:line resolves to the line's address and page",
          exact && exact->page && *exact->page == 4 && exact->address == 0x8039);
    const auto base = map.resolve("helper.bas:2");
    check("SRCMAP-09", "a unique basename resolves like the full path",
          base && base->address == 0x8039);
    check("SRCMAP-10", "a line that is not a number does not resolve",
          !map.resolve("helper.bas:x"));
    check("SRCMAP-11", "a line with no code does not resolve", !map.resolve("main.bas:99"));

    SourceLocation a{"x.bas", 3, 0, std::nullopt, 0x8000};
    SourceLocation b{"x.bas", 3, 0, uint8_t{4}, 0x9000};
    SourceLocation c{"x.bas", 4, 0, std::nullopt, 0x8000};
    check("SRCMAP-12", "same_position compares file, line and column, not address or page",
          SourceMap::same_position(a, b) && !SourceMap::same_position(a, c));

    // Unqualified records are wildcards for every page.
    {
        SourceMap m;
        // An SLD record always carries a page; a map built by another adapter
        // may not. Replace directly to cover the wildcard rule.
        m.replace({{"w.asm", 1, 0, std::nullopt, 0x8000}, {"w.asm", 3, 0, uint8_t{4}, 0x8000}},
                  std::nullopt, "adapter");
        const auto on4 = m.lookup(4, 0x8000);
        const auto on9 = m.lookup(9, 0x8000);
        check("SRCMAP-13", "a page-qualified record outranks an unqualified one on its page",
              on4 && on4->line == 3);
        check("SRCMAP-14", "an unqualified record answers on every other page",
              on9 && on9->line == 1);
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
        check("SRCMAP-16", "an ambiguous basename does not resolve", !m.resolve("helper.bas:2"));
        check("SRCMAP-17", "the full path still resolves", m.resolve("one/helper.bas:2").has_value());
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
        check("SLD-13", "a refused load leaves the live map in place",
              kept.size() == 4 && kept.lookup(4, 0x8016).has_value());
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
        const auto at = m.lookup(4, 0x8000);
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

    // A sidecar that is a link to itself: no sidecar, never an exception.
    {
        std::error_code ec;
        fs::create_symlink("self.sld", dir / "self.sld", ec);
        SourceMap m;
        bool threw = false;
        SourceMapLoadResult r;
        try {
            r = load_sld_sidecar(m, (dir / "self.nex").string());
        } catch (...) {
            threw = true;
        }
        check("SLD-18", "a looping sidecar link is refused, not thrown", !threw && r.count == -1);
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
        check("NBSYM-08", "an unreadable file is -1",
              SymbolTable().load_nextbuild_memory((dir / "missing.txt").string()) == -1);

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
        check("NBSYM-12", "a symbol spelled like hex wins as a name; $ forces the number",
              hexy.resolve("BEEF") == 0x9000 && hexy.resolve("$BEEF") == 0xBEEF);
    }

    fs::remove_all(dir);

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
