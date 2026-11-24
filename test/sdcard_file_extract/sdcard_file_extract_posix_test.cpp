// --sdcard-file-extract rows that need POSIX (GH #557): a symbolic link in the way. Wine cannot make
// symlinks, so this is `# os: posix`, like sdcard_file_add_posix_test.
//
// Oracle: the issue's rule that an extraction never destroys host data, and the plan's decision that
// the existence check is symlink_status: a symbolic link (dangling or not) is an existing entry, and
// nothing is ever written through it.
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
#include "core/sdcard_file_extract.h"

#include "core/fatfs_diskio.h"
#include "core/sha256.h"
#include "../row_id.h"
#include "../scratch_dir.h"

extern "C" {
#include "third_party/fatfs/ff.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sdcard::FileAddStatus;

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

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

}  // namespace

#include "sdfx_helpers.h"

int main() {
    g_dir = make_scratch_dir("sdfxp");
    const Bytes data = payload(1500, 1);
    const std::string img = tmp_name("card") + ".img";
    make_card(img, kLba, kFat32Sectors, FM_FAT32, 512, 0, 0x0C, [&](FATFS&, Pokes&) {
        return f_mkdir("0:/GAMES") == FR_OK && fat_write("0:/GAMES/WOTEF.NEX", data);
    });
    const fs::path dest = g_dir / "dest";
    fs::create_directories(dest);
    std::error_code ec;
    fs::create_symlink(dest / "nowhere", dest / "WOTEF.NEX", ec);
    std::string err;
    const FileAddStatus st = sdcard::extract_from_image(img, "/GAMES/WOTEF.NEX", dest.string(), err);
    check("SDFX-P01", "a dangling symbolic link of the name is an existing entry: status 4, the link unchanged, nothing created at its target",
          !ec && st == FileAddStatus::DestExists && fs::is_symlink(dest / "WOTEF.NEX") &&
              fs::read_symlink(dest / "WOTEF.NEX") == dest / "nowhere" && !fs::exists(dest / "nowhere") &&
              !fs::is_symlink(dest / "nowhere"),
          "status=" + std::to_string(static_cast<int>(st)) + " err=" + err + " ec=" + ec.message());
    fs::remove_all(g_dir, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
