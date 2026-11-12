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

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/esxdos_hostfs.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

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

    // HFW-130..134 (GH #457) -- the calls that change the host folder never act on
    // a symlink, whichever way it points (a link whose target is INSIDE the root,
    // which containment cannot object to, so the policy is the only refusal).
    // The answer for a link is esx_eacces, as for every other call.
    {
        const fs::path wbase = fs::temp_directory_path() /
                               ("jnext-hfw-links-" + jtp::process_id_string());
        const fs::path wroot = wbase / "root";
        fs::remove_all(wbase, rm_ec);
        put_file(wroot / "real.txt", "REAL");
        put_file(wroot / "other.txt", "OTHER");
        put_file(wroot / "sub" / "in.bin", "IN");
        fs::create_directories(wroot / "only-link");
        fs::create_directories(wroot / "emptyd");
        std::error_code wln;
        fs::create_symlink(wroot / "real.txt", wroot / "l.txt", wln);
        const bool f1 = !wln;
        fs::create_directory_symlink(wroot / "sub", wroot / "link-sub", wln);
        const bool f2 = !wln;
        fs::create_symlink(wroot / "real.txt", wroot / "only-link" / "k.txt", wln);
        const bool f3 = !wln;
        fs::create_directory_symlink(wroot / "emptyd", wroot / "link-empty", wln);
        const bool f4 = !wln;
        const bool fixtures = f1 && f2 && f3 && f4;
        if (fixtures) {
            EsxdosHostFs w;
            std::string why;
            w.configure(wroot.string(), true, why);
            const bool real_was_writable =
                (fs::status(wroot / "real.txt").permissions() & fs::perms::owner_write) !=
                fs::perms::none;
            const uint8_t ul = w.unlink("l.txt");
            hcheck("HFW-130",
                   "F_UNLINK of a symlink (target inside the root) is esx_eacces; the link "
                   "and its target are intact",
                   ul == EsxdosHostFs::kEacces && fs::is_symlink(wroot / "l.txt") &&
                       fs::exists(wroot / "real.txt"),
                   "A=" + hex2(ul));
            const uint8_t wu = w.unlink("only-link/*.txt");
            hcheck("HFW-131",
                   "wildcard F_UNLINK does not delete a symlink: \"*.txt\" in a directory "
                   "holding only a link matches nothing (esx_enoent), and in the root deletes "
                   "the regular files only",
                   wu == EsxdosHostFs::kEnoent && fs::is_symlink(wroot / "only-link" / "k.txt") &&
                       w.unlink("*.txt") == EsxdosHostFs::kOk && !fs::exists(wroot / "other.txt") &&
                       !fs::exists(wroot / "real.txt") && fs::is_symlink(wroot / "l.txt"),
                   "A=" + hex2(wu));
            put_file(wroot / "real.txt", "REAL");
            put_file(wroot / "other.txt", "OTHER");
            const uint8_t r1 = w.rename("l.txt", "moved.txt");
            const uint8_t r2 = w.rename("other.txt", "l.txt");
            const uint8_t r3 = w.rename("other.txt", "link-sub/x.txt");
            hcheck("HFW-132",
                   "F_RENAME of a link, onto a link and through a linked directory is "
                   "esx_eacces, and nothing moves",
                   r1 == EsxdosHostFs::kEacces && r2 == EsxdosHostFs::kEacces &&
                       r3 == EsxdosHostFs::kEacces && fs::is_symlink(wroot / "l.txt") &&
                       fs::exists(wroot / "other.txt") && !fs::exists(wroot / "moved.txt") &&
                       !fs::exists(wroot / "sub" / "x.txt"),
                   "A=" + hex2(r1) + "," + hex2(r2) + "," + hex2(r3));
            const uint8_t m1 = w.mkdir("link-sub/made");
            const uint8_t t1 = w.truncate("l.txt", 1);
            const uint8_t c1 = w.chmod("l.txt", 0, EsxdosHostFs::kChmodWrite);
            const bool still_writable =
                (fs::status(wroot / "real.txt").permissions() & fs::perms::owner_write) !=
                fs::perms::none;
            hcheck("HFW-133",
                   "F_MKDIR through a linked directory, F_TRUNCATE and F_CHMOD of a link are "
                   "esx_eacces; nothing changes",
                   m1 == EsxdosHostFs::kEacces && t1 == EsxdosHostFs::kEacces &&
                       c1 == EsxdosHostFs::kEacces && !fs::exists(wroot / "sub" / "made") &&
                       fs::file_size(wroot / "real.txt") == 4 && still_writable == real_was_writable,
                   "A=" + hex2(m1) + "," + hex2(t1) + "," + hex2(c1));
            const uint8_t d1 = w.rmdir("link-empty");
            const uint8_t d2 = w.rmdir("only-link");
            hcheck("HFW-134",
                   "F_RMDIR of a link to a directory is esx_eacces; of a directory holding "
                   "only a link esx_enospc (empty means nothing on the host); both stay",
                   d1 == EsxdosHostFs::kEacces && d2 == EsxdosHostFs::kEnospc &&
                       fs::is_symlink(wroot / "link-empty") && fs::exists(wroot / "emptyd") &&
                       fs::is_symlink(wroot / "only-link" / "k.txt"),
                   "A=" + hex2(d1) + "," + hex2(d2));
        } else {
            for (const char* id : {"HFW-130", "HFW-131", "HFW-132", "HFW-133", "HFW-134"})
                hcheck(id, "symlink fixtures unavailable on this host", false,
                       "create_symlink failed");
        }
        fs::remove_all(wbase, rm_ec);
    }

    // NXROOT-13 (GH #490) -- the implicit root of --esxdos-stub is the directory
    // the NEX's PATH names, not its link target's: L/link.nex -> R/real.nex
    // serves L (the directory `.RUN` and the kept-open sibling lookup already
    // use). Needs a symlink, hence here and POSIX-only.
    {
        const fs::path nbase = fs::temp_directory_path() /
                               ("jnext-gh490-links-" + jtp::process_id_string());
        fs::remove_all(nbase, rm_ec);
        fs::create_directories(nbase / "L");
        fs::create_directories(nbase / "R");
        put_file(nbase / "L" / "data.bin", "L");
        put_file(nbase / "R" / "data.bin", "R");
        {   // V1.2, one bank (bank 2), PC $8000 = JR $
            std::vector<char> f(512, 0);
            std::copy_n("NextV1.2", 8, f.begin());
            f[9] = 1; f[12] = static_cast<char>(0xF0); f[13] = static_cast<char>(0xBF);
            f[15] = static_cast<char>(0x80); f[18 + 2] = 1;
            std::vector<char> bank(0x4000, 0);
            bank[0] = 0x18; bank[1] = static_cast<char>(0xFE);
            f.insert(f.end(), bank.begin(), bank.end());
            std::ofstream o(nbase / "R" / "real.nex", std::ios::binary | std::ios::trunc);
            o.write(f.data(), static_cast<std::streamsize>(f.size()));
        }
        std::error_code nln;
        fs::create_symlink(nbase / "R" / "real.nex", nbase / "L" / "link.nex", nln);
        bool ok = false;
        std::string got;
        std::string root;
        if (!nln) {
            EmulatorConfig c;
            c.type = MachineType::ZXN_ISSUE2;
            c.esxdos_stub = true;
            c.rewind_buffer_frames = 0;
            auto ep = std::make_unique<Emulator>();
            Emulator& e = *ep;
            ok = e.init(c) && e.load_nex((nbase / "L" / "link.nex").string());
            root = e.esxdos_hostfs().root().string();
            if (ok && e.cpu().on_esxdos_call) {
                const std::string nm = "data.bin";
                for (std::size_t i = 0; i <= nm.size(); ++i)
                    e.mmu().write(static_cast<uint16_t>(0x9100 + i),
                                  i < nm.size() ? static_cast<uint8_t>(nm[i]) : 0);
                Z80Registers r{};
                r.AF = static_cast<uint16_t>('*' << 8);
                r.IX = 0x9100;
                r.BC = static_cast<uint16_t>(EsxdosHostFs::kModeRead << 8);
                if (e.cpu().on_esxdos_call(0x9A, r) && !(r.AF & 1)) {
                    Z80Registers rd{};
                    rd.AF = static_cast<uint16_t>(r.AF & 0xFF00);
                    rd.IX = 0x9200;
                    rd.BC = 1;
                    if (e.cpu().on_esxdos_call(0x9D, rd) && !(rd.AF & 1))
                        got.push_back(static_cast<char>(e.mmu().read(0x9200)));
                }
            }
        }
        hcheck("NXROOT-13",
               "a NEX reached through a symlink serves the directory of the link, "
               "not of its target",
               !nln && ok && root == fs::canonical(nbase / "L").string() && got == "L",
               "root=" + root + " got='" + got + "'");
        fs::remove_all(nbase, rm_ec);
    }

    fs::remove_all(base, rm_ec);
    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_passed + g_failed, g_passed, g_failed, 0);
    return g_failed == 0 ? 0 : 1;
}
