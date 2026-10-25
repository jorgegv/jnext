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

    std::error_code ec;
    fs::remove_all(r, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
