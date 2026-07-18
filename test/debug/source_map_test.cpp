#include "debug/source_map.h"
#include "debug/sld_loader.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {

int passed = 0;
int failed = 0;

void check(const char* name, bool condition)
{
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::cout << "FAIL: " << name << '\n';
    }
}

} // namespace

int main()
{
    const auto stamp = std::chrono::high_resolution_clock::now()
                           .time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() /
                         ("jnext-source-map-" + std::to_string(stamp));
    fs::create_directories(dir);
    const fs::path sld = dir / "game.sld";
    {
        std::ofstream out(sld);
        out << "|SLD.data.version|1\n"
            << "||program.name:game.bin\n"
            << "||program.sha256:9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a\n"
            << "||program.org:32768\n"
            << "||program.size:4\n"
            << "main.bas|6||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
            << "main.bas|6||0|4|32790|T|\n"
            << "main.bas|7||0|4|32795|T|\n"
            << "main.bas|8||0|4|32795|T|\n"
            << "include/helper.bas|2:5:12||0|4|32825|T|\n"
            << "main.bas|6||0|4|32790|L|,_Start,\n";
    }

    SourceMap map;
    check("loads SLD traces", load_sld(map, sld.string()).count == 4);
    check("deduplicates live-PC addresses", map.size() == 3);
    const uint8_t bytes[] = {1, 2, 3, 4};
    auto identity = map.verify_program([&](uint16_t address) {
        return bytes[address - 0x8000];
    });
    check("verifies matching program identity", identity && *identity);
    auto mismatch = map.verify_program([](uint16_t) { return uint8_t{0}; });
    check("rejects mismatched program identity", mismatch && !*mismatch);

    auto main = map.lookup(4, 0x8016);
    check("looks up page-aware source address",
          main && main->file == "main.bas" && main->line == 6);
    check("does not cross physical pages", !map.lookup(5, 0x8016));

    auto duplicate = map.lookup(4, 0x801B);
    check("later duplicate trace wins", duplicate && duplicate->line == 8);

    auto helper = map.lookup(4, 0x8039);
    check("retains included source path",
          helper && helper->file == "include/helper.bas");
    check("parses source line and column",
          helper && helper->line == 2 && helper->column == 5);

    auto exact = map.resolve("include/helper.bas:2");
    check("resolves exact file and line",
          exact && exact->page == 4 && exact->address == 0x8039);
    auto base = map.resolve("helper.bas:2");
    check("resolves unique source basename", base && base->address == 0x8039);
    check("rejects invalid source line", !map.resolve("helper.bas:not-a-line"));

    const fs::path ambiguous = dir / "ambiguous.sld";
    {
        std::ofstream out(ambiguous);
        out << "|SLD.data.version|1\n"
            << "one/helper.bas|2||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
            << "one/helper.bas|2||0|4|32768|T|\n"
            << "two/helper.bas|2||0|4|32769|T|\n";
    }
    SourceMap ambiguous_map;
    check("loads duplicate source basenames",
          load_sld(ambiguous_map, ambiguous.string()).count == 2);
    check("rejects ambiguous source basename",
          !ambiguous_map.resolve("helper.bas:2"));
    check("resolves exact path despite ambiguous basename",
          ambiguous_map.resolve("one/helper.bas:2").has_value());

    SourceMap retained;
    check("loads transactional fixture", load_sld(retained, sld.string()).count == 4);
    const fs::path malformed = dir / "bad.sld";
    { std::ofstream out(malformed); out << "not an sld\n"; }
    check("rejects malformed SLD", load_sld(retained, malformed.string()).count == -1);
    check("failed load retains active map", retained.lookup(4, 0x8016).has_value());

    const fs::path malformed_position = dir / "bad-position.sld";
    {
        std::ofstream out(malformed_position);
        out << "|SLD.data.version|1\n"
            << "main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
            << "main.bas|2:5:nope||0|4|32768|T|\n";
    }
    check("rejects malformed source column range",
          load_sld(retained, malformed_position.string()).count == -1);

    const fs::path oversized_pages = dir / "bad-pages.sld";
    {
        std::ofstream out(oversized_pages);
        out << "|SLD.data.version|1\n"
            << "main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:300,slots.count:1,slots.adr:0\n"
            << "main.bas|1||0|260|32768|T|\n";
    }
    check("rejects physical pages wider than JNext page keys",
          load_sld(retained, oversized_pages.string()).count == -1);

    const fs::path malformed_device = dir / "bad-device.sld";
    {
        std::ofstream out(malformed_device);
        out << "|SLD.data.version|1\n"
            << "main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:2,slots.adr:0,0\n"
            << "main.bas|1||0|4|32768|T|\n";
    }
    check("rejects duplicate device slots",
          load_sld(retained, malformed_device.string()).count == -1);
    check("failed device validation retains active map",
          retained.lookup(4, 0x8016).has_value());
    check("retained map still verifies its program identity",
          retained.verify_program([&](uint16_t address) {
              return bytes[address - 0x8000];
          }) == std::optional<bool>(true));

    const fs::path wrong_page_size = dir / "wrong-page-size.sld";
    {
        std::ofstream out(wrong_page_size);
        out << "|SLD.data.version|1\n"
            << "main.bas|1||0|-1|-1|Z|pages.size:16384,pages.count:112,slots.count:4,slots.adr:0,16384,32768,49152\n"
            << "main.bas|1||0|2|32768|T|\n";
    }
    check("rejects non-8K pages that cannot match the Next MMU",
          load_sld(retained, wrong_page_size.string()).count == -1);

    const fs::path incomplete_identity = dir / "incomplete-identity.sld";
    {
        std::ofstream out(incomplete_identity);
        out << "|SLD.data.version|1\n"
            << "||program.sha256:nope\n"
            << "main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:1,slots.adr:0\n"
            << "main.bas|1||0|4|32768|T|\n";
    }
    const auto identity_error = load_sld(retained, incomplete_identity.string());
    check("rejects incomplete program identity", !identity_error);
    check("identity failure retains active map",
          retained.lookup(4, 0x8016).has_value());

    const fs::path program = dir / "game.nex";
    { std::ofstream out(program); out << "NEX"; }
    SourceMap adjacent;
    check("loads adjacent SLD sidecar",
          load_sld_sidecar(adjacent, program.string()).count == 4);

    const fs::path crlf = dir / "crlf.sld";
    {
        std::ofstream out(crlf, std::ios::binary);
        out << "|SLD.data.version|1\r\n"
            << "main.bas|1||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:1,slots.adr:0\r\n"
            << "main.bas|1||0|4|32768|T|\r\n";
    }
    SourceMap crlf_map;
    check("loads CRLF SLD files", load_sld(crlf_map, crlf.string()).count == 1);

    SourceMap neutral;
    neutral.replace({{"logical.bas", 9, 0, std::nullopt, 0x9000},
                     {"banked.bas", 3, 0, uint8_t{4}, 0x9000}},
                    std::nullopt, "connector fixture");
    check("unqualified source address matches any physical page",
          neutral.lookup(5, 0x9000)->file == "logical.bas");
    check("qualified source address takes precedence",
          neutral.lookup(4, 0x9000)->file == "banked.bas");
    const auto logical = neutral.resolve("logical.bas:9");
    check("resolved logical source address has no physical page",
          logical && !logical->page && logical->address == 0x9000);

    fs::remove_all(dir);
    std::cout << "Total: " << passed + failed << "  Passed: " << passed
              << "  Failed: " << failed << "  Skipped: 0\n";
    return failed == 0 ? 0 : 1;
}
