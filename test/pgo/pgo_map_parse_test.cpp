// Parser contract of cmake/JnextPgo.cmake's link-map reader (GH #416).
//
// The PGO USE configure tells the members the linker pulled into jnext (profile
// REQUIRED) from those it never links (exempt from the missing-profile check)
// by reading the INSTRUMENTED link's map: GNU ld's -Map for gcc, ld64's -map for
// clang. If that reader mis-parses, the failure is silent: an `OUTPUT(...)` line
// counted as a member makes the "no archive members" guard dead, and a member
// list that comes out empty without the guard exempts EVERY unit from
// -Werror=missing-profile. The ld64 half runs only on macOS in CI, so this is
// its only local guard.
//
// Drives the real function through `cmake -P` (test/pgo/pgo_map_parse.cmake) on
// fixture maps written here. Oracle: the map formats as the two linkers emit
// them; no VHDL is involved (build tooling).
//
// Run: ./build/test/pgo_map_parse_test
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

struct Run { int status; std::string out; };

// Parse `map_text` as a jnext.map in the given format; the child gets a minimal
// environment (LANG=C) so its text does not depend on the user's locale.
Run parse(const std::string& work, const std::string& map_text, bool clang) {
    {
        std::ofstream m(work + "/jnext.map", std::ios::binary);
        m << map_text;
    }
    const std::string out = work + "/out.txt";
    const std::string cmd =
        std::string("env -i LANG=C PATH=\"$PATH\" '") + JNEXT_TEST_CMAKE + "' -DSRC='" +
        JNEXT_TEST_SRC + "' -DDIR='" + work + "' -DCLANG=" + (clang ? "ON" : "OFF") +
        " -P '" + JNEXT_TEST_SRC + "/test/pgo/pgo_map_parse.cmake' >'" + out + "' 2>&1";
    const int rc = std::system(cmd.c_str());
    std::ifstream f(out);
    std::stringstream ss;
    ss << f.rdbuf();
    return {rc, ss.str()};
}

bool has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

const char kGnuMap[] =
    "Archive member included to satisfy reference by file (symbol)\n"
    "\n"
    "/a/b/libjnext_core.a(emulator.cpp.o)\n"
    "                              CMakeFiles/jnext.dir/main.cpp.o (_ZN7EmulatorC1Ev)\n"
    "libjnext_cpu.a(z80_cpu.cpp.o)\n"
    "                              libjnext_core.a(emulator.cpp.o) (_ZN6Z80Cpu4stepEv)\n"
    "\n"
    "Linker script and memory map\n"
    "\n"
    "LOAD CMakeFiles/jnext.dir/main.cpp.o\n"
    "LOAD /a/b/libjnext_core.a\n"
    " .text          0x0000000000401000       0x20 /a/b/libjnext_core.a(emulator.cpp.o)\n"
    "OUTPUT(jnext elf64-x86-64)\n";

const char kGnuNoMembers[] =
    "Linker script and memory map\n"
    "\n"
    "LOAD CMakeFiles/jnext.dir/main.cpp.o\n"
    "OUTPUT(jnext elf64-x86-64)\n";

const char kLd64Map[] =
    "# Path: /Users/a b/build/jnext\n"
    "# Arch: arm64\n"
    "# Object files:\n"
    "[  0] linker synthesized\n"
    "[  1] /Users/a b/build/main.cpp.o\n"
    "[  2] /Users/a b/build/src/core/libjnext_core.a(emulator.cpp.o)\n"
    "[  3] /Users/a b/build/src/platform/libjnext_platform.a(sdl_app.cpp.o)\n"
    "[  4] /Applications/Xcode.app/SDKs/MacOSX.sdk/usr/lib/libSystem.tbd\n"
    "[  5] /var/folders/xx/T/lto.o\n"
    "[  6] /Users/a b/build (1)/obj.cpp.o\n"
    "# Sections:\n"
    "# Address\tSize\tSegment\tSection\n"
    "0x100000F00\t0x00000010\t__TEXT\t__text\n"
    "# Symbols:\n"
    "# Address\tSize\tFile  Name\n"
    "0x100000F00\t0x00000010\t[  2] _main\n";

const char kLd64NoMembers[] =
    "# Path: /Users/a b/build/jnext\n"
    "# Object files:\n"
    "[  0] linker synthesized\n"
    "[  1] /Users/a b/build/main.cpp.o\n"
    "[  2] /var/folders/xx/T/lto.o\n"
    "[  3] /Users/a b/build (1)/obj.cpp.o\n"
    "# Symbols:\n"
    "0x100000F00\t0x00000010\t[  1] _main\n";

}  // namespace

int main() {
    char tmpl[] = "/tmp/pgo_map_parse_XXXXXX";
    if (!mkdtemp(tmpl)) { std::printf("mkdtemp failed\n"); return 2; }
    const std::string work = tmpl;

    {
        const Run r = parse(work, kGnuMap, false);
        check("PGOMAP-01",
              "GNU ld map: exactly the archive members, no OUTPUT(...) entry",
              r.status == 0 &&
                  has(r.out, "LINKED=[libjnext_core.a(emulator.cpp.o);libjnext_cpu.a(z80_cpu.cpp.o)]") &&
                  !has(r.out, "OUTPUT"),
              r.out);
    }
    {
        const Run r = parse(work, kGnuNoMembers, false);
        check("PGOMAP-02", "GNU ld map with no archive member: configure FATALs",
              r.status != 0 && has(r.out, "no archive members"), r.out);
    }
    {
        const Run r = parse(work, kLd64Map, true);
        check("PGOMAP-03",
              "ld64 map: exactly the archive members (path with a space; .tbd, LTO object and symbol lines ignored)",
              r.status == 0 &&
                  has(r.out, "LINKED=[libjnext_core.a(emulator.cpp.o);libjnext_platform.a(sdl_app.cpp.o)]"),
              r.out);
    }
    {
        const Run r = parse(work, kLd64NoMembers, true);
        check("PGOMAP-04", "ld64 map with no archive member: configure FATALs",
              r.status != 0 && has(r.out, "no archive members"), r.out);
    }

    std::remove((work + "/jnext.map").c_str());
    std::remove((work + "/out.txt").c_str());
    rmdir(work.c_str());

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
