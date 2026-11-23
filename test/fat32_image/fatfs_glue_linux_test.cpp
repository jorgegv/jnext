// FatFs glue sector cache: the failed-write paths (GH #547 stage 5).
//
// A write that fails must DROP the cache lines of its request: the file may hold
// none, some or all of it, and a line kept or updated would serve bytes the file
// does not have. The two rows need Linux: /dev/full (every write fails with
// ENOSPC) and RLIMIT_FSIZE (a write cut in its middle), so they live in a suite
// the manifest declares `# os: linux`, as sdcard_file_add_linux_test does.
// No VHDL counterpart: a host-tool layer. The oracle is the image file read by
// an independent stream.
//
//   FGC-L01  a 32-sector write to /dev/full fails (it exceeds the stream's 8 KiB
//            buffer, so the failure is seen at once) and its lines are dropped:
//            the sectors read as /dev/full holds them (zeros), not as written.
//   FGC-L02  a 32-sector write cut by RLIMIT_FSIZE after 16 sectors fails, and a
//            sector of the part that did reach the file reads as the file holds
//            it, not as the stale line.
//
// Output follows the project-wide line:
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d

#include "core/fatfs_diskio.h"

extern "C" {
#include "third_party/fatfs/ff.h"
#include "third_party/fatfs/diskio.h"
}

#include <sys/resource.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string tmp_path(const char* name) {
    const char* base = std::getenv("TMPDIR");
    std::string dir = base && *base ? base : "/tmp";
    return dir + "/jnext_fgc_linux_" + std::to_string(::getpid()) + "_" + name;
}

std::vector<uint8_t> rd(uint32_t n, UINT count, DRESULT* r) {
    std::vector<uint8_t> v(static_cast<size_t>(count) * 512, 0xEE);
    *r = disk_read(0, v.data(), n, count);
    return v;
}

} // namespace

int main() {
    // FGC-L01
    {
        std::string e;
        const bool at = fatfs_glue::attach(0, "/dev/full", 0, 100, e);
        DRESULT a = RES_ERROR, b = RES_ERROR;
        const auto z = rd(4, 32, &a); // /dev/full reads as zeros; cached now
        const std::vector<uint8_t> w(32 * 512, 0x5A);
        const DRESULT wr = disk_write(0, w.data(), 4, 32);
        const auto r = rd(4, 32, &b);
        fatfs_glue::detach(0);
        check("FGC-L01", "a failed write to /dev/full drops its cache lines",
              at && a == RES_OK && z == std::vector<uint8_t>(32 * 512, 0) && wr == RES_ERROR &&
              b == RES_OK && r == std::vector<uint8_t>(32 * 512, 0),
              "at=" + std::to_string(at) + " wr=" + std::to_string(wr) + " r0=" + std::to_string(r[0]) + " " + e);
    }
    // FGC-L02
    {
        const std::string img = tmp_path("fsz.img");
        constexpr uint32_t kLba = 7;
        {
            std::ofstream f(img, std::ios::binary | std::ios::trunc);
            std::vector<char> z((kLba + 64) * 512, 0x11);
            f.write(z.data(), static_cast<std::streamsize>(z.size()));
        }
        std::string e;
        const bool at = fatfs_glue::attach(0, img, kLba, 64, e);
        DRESULT r0 = RES_ERROR, r1 = RES_ERROR;
        (void)rd(20, 32, &r0); // the 0x11 sectors are cached
        struct rlimit old{};
        getrlimit(RLIMIT_FSIZE, &old);
        struct rlimit lim = old;
        lim.rlim_cur = static_cast<rlim_t>(kLba + 36) * 512; // cuts the write after 16 sectors
        auto prev = std::signal(SIGXFSZ, SIG_IGN);
        setrlimit(RLIMIT_FSIZE, &lim);
        const std::vector<uint8_t> w(32 * 512, 0x5A);
        const DRESULT wr = disk_write(0, w.data(), 20, 32);
        setrlimit(RLIMIT_FSIZE, &old);
        std::signal(SIGXFSZ, prev);
        const auto r = rd(20, 1, &r1);
        std::vector<uint8_t> f20(512, 0xEE);
        {
            std::ifstream f(img, std::ios::binary);
            f.seekg(static_cast<std::streamoff>(static_cast<uint64_t>(kLba + 20) * 512));
            f.read(reinterpret_cast<char*>(f20.data()), 512);
        }
        fatfs_glue::detach(0);
        std::remove(img.c_str());
        check("FGC-L02", "a write cut in the middle by RLIMIT_FSIZE reads back as the file holds it",
              at && r0 == RES_OK && wr == RES_ERROR && r1 == RES_OK && r == f20 && f20[0] == 0x5A,
              "wr=" + std::to_string(wr) + " r1=" + std::to_string(r1) + " r0=" + std::to_string(r[0]) +
              " f0=" + std::to_string(f20[0]));
    }

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total + g_skip, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
