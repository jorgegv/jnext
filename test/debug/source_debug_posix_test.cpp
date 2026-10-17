// Source-level debugging: the rows whose sidecar fixtures are POSIX file
// types (a UNIX socket, a symbolic link). Windows has no sockets in the file
// system and wine cannot create symlinks, so they live apart from
// source_map_test and source_debug_test, whose counts are the same on every
// OS (GH #214). IDs and assertions are those of the suites they came from.
//
//   SLD-18           load_sld_sidecar(): a looping link is refused, not thrown
//   SRCDBG-SIDE-09   load_program_sidecars(): a looping link is no sidecar
//   SRCDBG-OWN-03    a sidecar that is there and cannot be opened
//
// A real 48K machine and a real `Debugger`, as in source_debug_test.
//
// Run: ./build/test/source_debug_posix_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "debug/sld_loader.h"
#include "debug/source_map.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "../row_id.h"

namespace fs = std::filesystem;
using jnext::dbg::ClientId;
using jnext::dbg::Debugger;

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

static void check(const char* id, const char* desc, bool cond,
                  const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc, detail.empty() ? "" : " - ",
                    detail.c_str());
    }
}

static void write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

struct Machine {
    Emulator emu;
    std::unique_ptr<Debugger> dbg;
    ClientId a = jnext::dbg::CLIENT_NONE;

    Machine() {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        dbg = std::make_unique<Debugger>(emu);
        jnext::dbg::ClientInfo ci;
        ci.name = "source";
        ci.kind = jnext::dbg::ClientKind::Test;
        a = dbg->attach(ci).value;
        dbg->pause(a);
    }
};

int main() {
    std::printf("Source-level debugging (POSIX file types)\n");
    const fs::path own = fs::temp_directory_path() /
                         ("jnext-source-debug-posix-" + std::to_string(::getpid()));
    fs::remove_all(own);
    fs::create_directories(own);

    // A sidecar that is a link to itself: no sidecar, never an exception.
    {
        std::error_code ec;
        fs::create_symlink("self.sld", own / "self.sld", ec);
        SourceMap m;
        bool threw = false;
        SourceMapLoadResult r;
        try {
            r = load_sld_sidecar(m, (own / "self.nex").string());
        } catch (...) {
            threw = true;
        }
        check("SLD-18", "a looping sidecar link is refused, not thrown", !threw && r.count == -1);
    }
    {
        Machine m;
        write_text(own / "loop.nex", "");
        std::error_code ec;
        fs::create_symlink("loop.sld", own / "loop.sld", ec);
        fs::create_symlink("loop.Memory.txt", own / "loop.Memory.txt", ec);
        bool threw = false;
        Debugger::SidecarLoad r;
        try {
            r = m.dbg->load_program_sidecars((own / "loop.nex").string());
        } catch (...) {
            threw = true;
        }
        check("SRCDBG-SIDE-09", "a sidecar that is a looping link is no sidecar, not an "
                                "exception",
              !threw && r.symbols == -1 && r.sources == -1);
    }
    {
        // A Memory.txt that is there and cannot be opened — a UNIX socket
        // node, which open() refuses for every user, root included.
        Machine m;
        write_text(own / "own3.nex", "");
        write_text(own / "own3.Memory.txt", "8000: ._Old\n");
        m.dbg->load_program_sidecars((own / "own3.nex").string());
        const bool had_old = m.dbg->lookup_name("Old").has_value();
        write_text(own / "own4.nex", "");
        const fs::path sock = own / "own4.Memory.txt";
        fs::remove(sock);
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un sa{};
        sa.sun_family = AF_UNIX;
        std::snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", sock.c_str());
        const bool made = fd >= 0 &&
                          ::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0;
        const auto r = m.dbg->load_program_sidecars((own / "own4.nex").string());
        if (fd >= 0) ::close(fd);
        fs::remove(sock);
        check("SRCDBG-OWN-03", "a sidecar Memory.txt that cannot be opened is reported "
                               "unreadable, and leaves none of the previous program's "
                               "symbols",
              had_old && made && r.symbols_unreadable && !m.dbg->lookup_name("Old"));
    }

    fs::remove_all(own);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
