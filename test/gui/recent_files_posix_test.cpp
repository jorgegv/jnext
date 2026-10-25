// File > Recent Files and symbolic links (GitHub issue #406, POSIX only).
//
// Oracle: the list rule in the plan -- two spellings of one file are one entry,
// "dedup by canonical path where the file exists". A symbolic link is the
// spelling that only canonicalisation resolves (lexical folding of "a/../b"
// cannot see through it). Its own suite because Windows (and wine) cannot
// create the fixture (`# os: posix` in unit-tests.conf, GH #214): a failure to
// create the link is a FAIL here, never a fallback.
//
// Run: ./build/test/recent_files_posix_test

#include "gui/recent_files.h"

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
}  // namespace

int main()
{
    const std::string root = jtp::make_temp_dir("jnext_recent_posix_");
    const fs::path r(root);
    const fs::path file = r / "a.nex", link = r / "alias.nex";
    std::ofstream(file).put('x');
    std::error_code lec;
    fs::create_symlink(file, link, lec);

    recent_files::List l;
    recent_files::push(l, file.string());
    recent_files::push(l, link.string());
    std::string detail = lec ? "symlink not created: " + lec.message() : "";
    for (const auto& e : l) detail += " | " + e;
    check("RECF-17", "a symlink to a listed file is the same entry, not a second one",
          !lec && l.size() == 1, detail);

    // RECF-18/19: a directory reached through a symlink (macOS /var/folders ->
    // /private/var). Entries are stored as given (lexical); the other spelling
    // goes through the link, and a nonexistent "x/.." or a vanished file must
    // not change the key. Reproduces the macOS CI failure of RECF-05.
    const fs::path real = r / "real", dlink = r / "dlink";
    std::error_code dec1, dec2;
    fs::create_directory(real, dec1);
    fs::create_directory_symlink(real, dlink, dec2);
    std::ofstream(real / "b.tap").put('x');
    const std::string setup = (dec1 || dec2) ? "fixture not created" : "";
    {
        recent_files::List m;
        recent_files::push(m, (real / "b.tap").string());
        recent_files::push(m, (real / "a.tap").string());
        recent_files::remove(m, (dlink / "x" / ".." / "b.tap").string());
        std::string d = setup;
        for (const auto& e : m) d += " | " + e;
        check("RECF-18", "remove through a symlinked directory with a nonexistent x/.. drops the entry",
              setup.empty() && m.size() == 1 && m[0] == (real / "a.tap").string(), d);
    }
    {
        recent_files::List m;
        const std::string gone = (real / "gone.nex").string();
        recent_files::push(m, gone);
        recent_files::push(m, (dlink / "gone.nex").string());
        const bool one = m.size() == 1;
        recent_files::remove(m, (dlink / "gone.nex").string());
        std::string d = setup;
        for (const auto& e : m) d += " | " + e;
        check("RECF-19", "a vanished file under a symlinked directory is one entry and removable via either spelling",
              setup.empty() && one && m.empty(), d);
    }

    std::error_code ec;
    fs::remove_all(r, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
