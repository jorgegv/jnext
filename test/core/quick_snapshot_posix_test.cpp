// GH #321 -- prune_quick_snapshots() and symbolic links (POSIX only).
//
// Oracle: the header contract of prune_quick_snapshots(): it deletes only
// regular files that jnext's own quick-snapshot naming produced; a symbolic
// link with such a name is not a file jnext wrote and is never deleted, and
// neither is its target. Its own suite because Windows (and wine) cannot create
// the fixtures (`# os: posix` in unit-tests.conf, GH #214).

#include "core/quick_snapshot.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "../row_id.h"
#include "../test_portable.h"

namespace fs = std::filesystem;

namespace {
int g_pass = 0, g_fail = 0;
void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    if (cond) { ++g_pass; return; }
    ++g_fail;
    std::printf("  FAIL %s: %s", id, desc);
    if (!detail.empty()) std::printf(" [%s]", detail.c_str());
    std::printf("\n");
}
void touch(const fs::path& p) { std::ofstream f(p, std::ios::binary); f << "x"; }
}  // namespace

int main()
{
    std::printf("quick_snapshot_posix_test (GH #321)\n");
    const std::string tmp = jtp::make_temp_dir("jnext-qsp-");
    const fs::path root(tmp);
    const auto t0 = fs::file_time_type::clock::now() - std::chrono::hours(2);

    // Three real quick snapshots (newest last) plus symlinks with quick names that
    // are OLDER than all of them, so a chooser that followed links would delete them.
    const fs::path d = root / "dir";
    fs::create_directories(d);
    for (int i = 0; i < 3; ++i) {
        char b[48];
        std::snprintf(b, sizeof(b), "jnext-20260101-%06d.jns", i);
        touch(d / b);
        fs::last_write_time(d / b, t0 + std::chrono::seconds(10 + i));
    }
    const fs::path target = root / "precious.jns";
    touch(target);
    std::error_code ec;
    fs::create_symlink(target, d / "jnext-20250101-000000.jns", ec);
    const bool link_made = !ec;
    fs::create_symlink(root / "missing-target", d / "jnext-20250101-000001.jns", ec);
    const bool dangling_made = !ec;

    const int deleted = prune_quick_snapshots(d.string(), 1);
    check("QS-31", "pruning never deletes a symbolic link with a quick-snapshot name, nor its target",
          link_made && fs::is_symlink(d / "jnext-20250101-000000.jns") && fs::exists(target),
          "link_made=" + std::to_string(link_made));
    check("QS-32", "a dangling symbolic link with a quick name survives too, and the regular files "
                   "beyond the keep count are still pruned",
          dangling_made && fs::is_symlink(d / "jnext-20250101-000001.jns") && deleted == 2,
          "deleted=" + std::to_string(deleted));

    fs::remove_all(root, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
