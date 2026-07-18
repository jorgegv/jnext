#include "debug/symbol_table.h"
#include "debug/symbol_loaders.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

int passed = 0;
int failed = 0;

void check(const char* name, bool condition)
{
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("FAIL: %s\n", name);
    }
}

void write_text(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path);
    output << text;
}

} // namespace

int main()
{
    namespace fs = std::filesystem;

    const auto stamp = std::chrono::high_resolution_clock::now()
                           .time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() /
                         ("jnext-nextbuild-symbols-" + std::to_string(stamp));
    fs::create_directories(dir);

    const fs::path memory = dir / "sample.Memory.txt";
    write_text(memory,
        "7320: .core.__START_PROGRAM\n"
        "7335: ._factoryFxDirty\n"
        "ACB5: ._FillScreen\n"
        "ACB5: .core.alias_at_same_address\n"
        "C735: ._RespawnPlayer\n"
        "10000: .too_wide\n"
        "GGGG: .not_hex\n"
        "missing colon\n");

    SymbolTable symbols;
    check("loads NextBuild Memory.txt", load_nextbuild_memory(symbols, memory.string()) == 5);
    check("keeps one display symbol per address", symbols.size() == 4);
    check("cleans Boriel function name",
          symbols.lookup(0xACB5) == std::optional<std::string>("FillScreen"));
    check("resolves cleaned function name", symbols.resolve("FillScreen") == 0xACB5);
    check("resolves emitted function name", symbols.resolve("._FillScreen") == 0xACB5);
    check("resolves dot-stripped alias", symbols.resolve("_FillScreen") == 0xACB5);
    check("retains alias at duplicate address",
          symbols.resolve("core.alias_at_same_address") == 0xACB5);
    check("resolves bare hexadecimal", symbols.resolve("c735") == 0xC735);
    check("resolves dollar hexadecimal", symbols.resolve(" $7335 ") == 0x7335);
    check("resolves 0x hexadecimal", symbols.resolve("0x7320") == 0x7320);
    check("rejects invalid address", !symbols.resolve("not-a-symbol"));
    check("rejects wide address", !symbols.resolve("10000"));

    const fs::path program = dir / "game.nex";
    const fs::path legacy = dir / "Memory.txt";
    write_text(legacy, "8000: ._Legacy\n");
    check("does not auto-load symbols for non-NEX programs",
          load_nextbuild_sidecar(symbols, (dir / "game.tap").string()) == -1);
    check("falls back to Memory.txt", load_nextbuild_sidecar(symbols, program.string()) == 1);
    check("loads fallback symbol", symbols.resolve("Legacy") == 0x8000);

    const fs::path named = dir / "game.Memory.txt";
    write_text(named, "9000: ._Named\n");
    check("prefers per-program sidecar", load_nextbuild_sidecar(symbols, program.string()) == 1);
    check("loads named sidecar symbol", symbols.resolve("Named") == 0x9000);
    check("replaces previous symbol table", !symbols.resolve("Legacy"));
    check("records loaded sidecar path", symbols.loaded_file() == named.string());

    const fs::path z88dk = dir / "game.map";
    write_text(z88dk,
        "Function = $8123 ; addr, local, , game, code_compiler, 0, 0\n"
        "Constant = $0042 ; const, local, , game, code_compiler, 0, 0\n");
    check("loads z88dk address symbols", load_z88dk_map(symbols, z88dk.string()) == 1);
    check("filters z88dk constants", symbols.resolve("Function") == 0x8123
          && !symbols.resolve("Constant"));
    check("failed symbol load retains active table",
          load_z88dk_map(symbols, (dir / "missing.map").string()) == -1
          && symbols.resolve("Function") == 0x8123);

    const fs::path simple = dir / "simple.map";
    write_text(simple, "; comment\nEntry = $9234\n");
    check("loads simple map symbols", load_simple_map(symbols, simple.string()) == 1);
    check("resolves simple map symbol", symbols.resolve("Entry") == 0x9234);
    check("successful adapter load replaces prior symbols", !symbols.resolve("Function"));

    fs::remove_all(dir);
    std::printf("Total: %d  Passed: %d  Failed: %d  Skipped: 0\n",
                passed + failed, passed, failed);
    return failed == 0 ? 0 : 1;
}
