// Source-level debugging: the rows whose sidecar fixtures are POSIX file
// types (a symbolic link, a FIFO). wine cannot create symlinks and Windows
// has no FIFOs in the file system, so they live apart from source_map_test
// and source_debug_test, whose counts are the same on every OS (GH #214).
//
//   SLD-18           load_sld_sidecar(): a looping link is refused, not thrown
//   SRCDBG-SIDE-09   load_program_sidecars(): a looping link is no sidecar
//   SRCDBG-SIDE-10   a FIFO named <stem>.sld is no sidecar, and the load
//                    does not wait on it
//   SRCDBG-SIDE-11   the same for <stem>.Memory.txt
//   SRCDBG-SIDE-12   a FIFO loaded by hand is refused, not waited on
//
// A real 48K machine and a real `Debugger`, as in source_debug_test.
//
// Run: ./build/test/source_debug_posix_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "debug/sld_loader.h"
#include "debug/source_map.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <thread>
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
    // A FIFO beside the program. Opening one for reading blocks until a writer
    // comes, so a search that took it would hang the load. From 1.5 s on, the
    // helper keeps opening the write end, which ends each such wait, so a row
    // that fails reports instead of hanging the suite.
    auto with_fifo = [&](const fs::path& fifo, auto&& load) {
        fs::remove(fifo);
        const bool made = ::mkfifo(fifo.c_str(), 0600) == 0;
        std::atomic<bool> done{false};
        std::thread unblock([fifo, &done] {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            while (!done) {
                const int fd = ::open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
                if (fd >= 0) ::close(fd);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
        const auto t0 = std::chrono::steady_clock::now();
        load();
        const auto took = std::chrono::steady_clock::now() - t0;
        done = true;
        unblock.join();
        fs::remove(fifo);
        return made && took < std::chrono::seconds(1);
    };
    {
        Machine m;
        write_text(own / "pipe.nex", "");
        SourceMapLoadResult direct;
        Debugger::SidecarLoad r;
        const bool quick = with_fifo(own / "pipe.sld", [&] {
            r = m.dbg->load_program_sidecars((own / "pipe.nex").string());
            SourceMap sm;
            direct = load_sld_sidecar(sm, (own / "pipe.nex").string());
        });
        check("SRCDBG-SIDE-10", "a FIFO named <stem>.sld is no sidecar, and the load "
                                "returns within 1 s",
              quick && r.sources == -1 && direct.error == "no adjacent SLD sidecar",
              direct.error);
    }
    {
        Machine m;
        write_text(own / "pipe2.nex", "");
        Debugger::SidecarLoad r;
        const bool quick = with_fifo(own / "pipe2.Memory.txt", [&] {
            r = m.dbg->load_program_sidecars((own / "pipe2.nex").string());
        });
        check("SRCDBG-SIDE-11", "a FIFO named <stem>.Memory.txt is no sidecar, and the "
                                "load returns within 1 s",
              quick && r.symbols == -1 && !r.symbols_unreadable);
    }
    {
        SourceMap sm;
        SourceMapLoadResult r;
        const bool quick = with_fifo(own / "byhand.sld", [&] {
            r = load_sld(sm, (own / "byhand.sld").string());
        });
        check("SRCDBG-SIDE-12", "a FIFO loaded by hand as an SLD is refused, not waited on",
              quick && r.count == -1 && r.error == "not a regular file", r.error);
    }

    fs::remove_all(own);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
