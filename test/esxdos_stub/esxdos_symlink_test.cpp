// GH #31 -- the symlink-refusal rows of --esxdos-stub-root.
//
// These six rows (HFS-10..12, HFS-90..92) used to sit in esxdos_stub_test's
// Tier 1 beside the rest of the sandbox rows. They need to create symbolic
// links, which a Windows build run under wine cannot (and which needs a
// privilege on real Windows), so they live in a suite of their own that the
// manifest declares `# os: posix` (GH #214). Rows, IDs and assertions are
// unchanged; the fixture is the part of esxdos_hostfs_rows.cpp's tree they read.
//
// Oracle: the policy documented at the top of src/core/esxdos_hostfs.cpp -- no
// symlink is ever traversed, whichever way it points -- and esxapi.def's
// esx_eacces (8) for the refusal.

#include "core/esxdos_hostfs.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "../row_id.h"
#include "../test_portable.h"

namespace fs = std::filesystem;

namespace {

int g_passed = 0;
int g_failed = 0;

void hcheck(const char* id, const char* desc, bool condition,
            const std::string& detail = "") {
    report_row_id(id);
    if (condition) {
        ++g_passed;
    } else {
        ++g_failed;
        std::printf("FAIL %s: %s%s%s\n", id, desc,
                    detail.empty() ? "" : " — ", detail.c_str());
    }
}

std::string hex2(unsigned v) {
    char b[8];
    std::snprintf(b, sizeof(b), "%02X", v & 0xFF);
    return b;
}

void put_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
}

}  // namespace

int main() {
    std::printf("esxdos host FS: symlink refusal (GH #31)\n");

    //   <root>/hello.txt            "hello"
    //   <root>/sub/inner.bin        "INNER"
    //   <root>/link-out             symlink -> <outside>/secret.txt
    //   <root>/link-dir             symlink -> <outside>
    //   <root>/link-in              symlink -> <root>/hello.txt
    //   <root>/link-sub             symlink -> <root>/sub
    //   <outside>/secret.txt        "SECRET"        -> must never be reachable
    const fs::path base    = fs::temp_directory_path() / ("jnext-hostfs-links-" + jtp::process_id_string());
    const fs::path root    = base / "root";
    const fs::path outside = base / "outside";
    std::error_code rm_ec;
    fs::remove_all(base, rm_ec);

    put_file(root / "hello.txt", "hello");
    put_file(root / "sub" / "inner.bin", "INNER");
    put_file(outside / "secret.txt", "SECRET");
    std::error_code ln_ec;
    fs::create_symlink(outside / "secret.txt", root / "link-out", ln_ec);
    const bool have_symlinks = !ln_ec;
    fs::create_directory_symlink(outside, root / "link-dir", ln_ec);
    fs::create_symlink(root / "hello.txt", root / "link-in", ln_ec);
    // A link whose target is INSIDE the root. Containment cannot object to it,
    // so it is the only input that isolates the symlink POLICY from the
    // canonical back-stop. One per call surface, below.
    fs::create_directory_symlink(root / "sub", root / "link-sub", ln_ec);

    {
        EsxdosHostFs hfs;
        std::string why;
        const bool configured = hfs.configure(root.string(), false, why);
        if (!configured) {
            std::printf("FAIL setup: configure() refused the fixture root: %s\n", why.c_str());
            return 2;
        }
        EsxdosHostFs::StatInfo st;

        if (have_symlinks) {
            hcheck("HFS-10",
                   "a symlink pointing OUT of the root is refused (esx_eacces), "
                   "not followed",
                   hfs.stat("link-out", st) == EsxdosHostFs::kEacces,
                   "got " + hex2(hfs.stat("link-out", st)));
            hcheck("HFS-11",
                   "a symlinked DIRECTORY component is refused, so a link "
                   "cannot be used as a bridge to a path outside",
                   hfs.stat("link-dir/secret.txt", st) == EsxdosHostFs::kEacces);
            hcheck("HFS-12",
                   "a symlink pointing INSIDE the root is refused too: the "
                   "policy is no traversal at all, not 'only escaping links'",
                   hfs.stat("link-in", st) == EsxdosHostFs::kEacces);

            // HFS-90..92 — the same policy input, once per REMAINING call
            // surface. An inside-pointing link is the only case containment
            // cannot catch, so these are what go red if open(), opendir() or
            // chdir() stops refusing links at all. See the layering note in
            // esxdos_hostfs.cpp for why no row can isolate a SINGLE check.
            // Each pairs the refusal with a control proving the TARGET is
            // reachable by its real name, so the row cannot pass merely
            // because something is broken.
            uint8_t link_h = 0;
            const uint8_t open_link = hfs.open("link-in",
                                               EsxdosHostFs::kModeRead, link_h);
            uint8_t real_h = 0;
            const uint8_t open_real = hfs.open("hello.txt",
                                               EsxdosHostFs::kModeRead, real_h);
            hfs.close(real_h);
            hcheck("HFS-90",
                   "F_OPEN refuses an inside-pointing symlink (esx_eacces) "
                   "while opening its target by the real name succeeds",
                   open_link == EsxdosHostFs::kEacces &&
                       open_real == EsxdosHostFs::kOk,
                   "link=" + hex2(open_link) + " real=" + hex2(open_real));

            uint8_t dir_link_h = 0;
            const uint8_t od_link = hfs.opendir("link-sub",
                                                EsxdosHostFs::kDirLfnOnly,
                                                dir_link_h);
            uint8_t dir_real_h = 0;
            const uint8_t od_real = hfs.opendir("sub", EsxdosHostFs::kDirLfnOnly,
                                                dir_real_h);
            hfs.close(dir_real_h);
            hcheck("HFS-91",
                   "F_OPENDIR refuses a symlinked directory inside the root "
                   "while the real directory opens",
                   od_link == EsxdosHostFs::kEacces && od_real == EsxdosHostFs::kOk,
                   "link=" + hex2(od_link) + " real=" + hex2(od_real));

            const uint8_t cd_link = hfs.chdir("link-sub");
            const uint8_t cd_real = hfs.chdir("sub");
            hfs.chdir("/");
            hcheck("HFS-92",
                   "F_CHDIR refuses it too, and the real directory still works",
                   cd_link == EsxdosHostFs::kEacces && cd_real == EsxdosHostFs::kOk,
                   "link=" + hex2(cd_link) + " real=" + hex2(cd_real));
        } else {
            hcheck("HFS-10", "symlink fixtures unavailable on this host", false,
                   "create_symlink failed");
            hcheck("HFS-11", "symlink fixtures unavailable on this host", false, "");
            hcheck("HFS-12", "symlink fixtures unavailable on this host", false, "");
        }

    }

    fs::remove_all(base, rm_ec);
    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_passed + g_failed, g_passed, g_failed, 0);
    return g_failed == 0 ? 0 : 1;
}
