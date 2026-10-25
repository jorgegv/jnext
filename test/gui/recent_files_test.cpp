// File > Recent Files list logic test (GitHub issue #406).
//
// No VHDL oracle: host UI behaviour. The oracle is the issue's request (a
// "Recent Files" list like SimCoupe's, so a rebuilt .nex is one click away and
// the dialog opens on the last file) and the list rules stated in the plan:
// most recent first, no duplicates, ten entries, a vanished file removable.
//
// Run: ./build/test/recent_files_test

#include "gui/recent_files.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "../row_id.h"
#include "../test_portable.h"

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
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

namespace fs = std::filesystem;
using recent_files::List;

std::string touch(const fs::path& p)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p).put('x');
    return p.string();
}

std::string join(const List& l)
{
    std::string s;
    for (const auto& e : l) s += e + "|";
    return s;
}

}  // namespace

int main()
{
    const std::string root = jtp::make_temp_dir("jnext_recent_");
    const fs::path r(root);
    const std::string a = touch(r / "a.nex"), b = touch(r / "b.tap"), c = touch(r / "c.sna");

    { List l; recent_files::push(l, a); recent_files::push(l, b); recent_files::push(l, c);
      check("RECF-01", "push puts the newest first",
            l.size() == 3 && l[0] == c && l[1] == b && l[2] == a,
            join(l)); }

    { List l; recent_files::push(l, a); recent_files::push(l, b); recent_files::push(l, c);
      recent_files::push(l, a);
      check("RECF-02", "re-pushing an entry moves it to the top, no duplicate",
            l.size() == 3 && l[0] == a && l[1] == c && l[2] == b, join(l)); }

    { List l;
      std::string last;
      for (int i = 0; i < 12; ++i) {
          last = touch(r / ("many" + std::to_string(i) + ".nex"));
          recent_files::push(l, last);
      }
      check("RECF-03", "the list is cut to ten, oldest dropped, newest first",
            l.size() == 10 && recent_files::kMax == 10 && l.front() == last &&
                l.back() == (r / "many2.nex").string(), join(l)); }

    { List l; recent_files::push(l, a);
      recent_files::push(l, (r / "sub" / ".." / "a.nex").string());
      recent_files::push(l, (r / "." / "a.nex").string());
      check("RECF-04", "two spellings of one existing file are one entry",
            l.size() == 1 && l[0] == (r / "." / "a.nex").string(), join(l)); }
    // (the entry holds the LAST spelling pushed, as given: it is not folded
    //  lexically, since "link/.." would cross a symlink -- see RECF-21)

    { List l; recent_files::push(l, a); recent_files::push(l, b); recent_files::push(l, c);
      recent_files::remove(l, (r / "x" / ".." / "b.tap").string());
      check("RECF-05", "remove drops the entry (by another spelling) and keeps the order of the rest",
            l.size() == 2 && l[0] == c && l[1] == a, join(l)); }

    { List l; recent_files::push(l, a);
      recent_files::remove(l, b);
      check("RECF-06", "removing a path that is not listed changes nothing",
            l.size() == 1 && l[0] == a, join(l)); }

    { List l; recent_files::push(l, a); recent_files::push(l, "");
      check("RECF-07", "an empty path is ignored", l.size() == 1 && l[0] == a, join(l)); }

    { const std::string n = recent_files::normalise((r / "q" / ".." / "a.nex").string());
      check("RECF-08", "normalise gives an absolute path with the dot segments folded",
            fs::path(n).is_absolute() && n == (r / "a.nex").lexically_normal().string(), n); }

    { const std::string gone = (r / "gone.nex").string();
      List l; recent_files::push(l, gone);
      recent_files::push(l, (r / "z" / ".." / "gone.nex").string());
      check("RECF-09", "a file that does not exist is still de-duplicated and removable",
            l.size() == 1, join(l));
      recent_files::remove(l, gone);
      check("RECF-10", "a vanished file can be removed from the list", l.empty(), join(l)); }

    { check("RECF-11", "no recent file: the dialog seed is the last directory",
            recent_files::dialog_seed({}, root) == root); }

    { List l; recent_files::push(l, a);
      check("RECF-12", "the most recent file, still in the last directory, is the dialog seed",
            recent_files::dialog_seed(l, root) == a, recent_files::dialog_seed(l, root)); }

    { List l; const std::string t = touch(r / "tmp.nex"); recent_files::push(l, t);
      fs::remove(t);
      check("RECF-13", "a most recent file that is gone falls back to the last directory",
            recent_files::dialog_seed(l, root) == root); }

    { List l; const std::string o = touch(r / "other" / "o.nex"); recent_files::push(l, o);
      check("RECF-14", "a most recent file from another directory than the last one is not the seed",
            recent_files::dialog_seed(l, root) == root); }

    { List l; recent_files::push(l, a);
      check("RECF-15", "with no last directory the most recent existing file is the seed",
            recent_files::dialog_seed(l, "") == a); }

    { List l; recent_files::push(l, a);
      check("RECF-16", "the last directory is compared by identity, not spelling",
            recent_files::dialog_seed(l, (r / "other" / "..").string()) == a); }

    std::error_code ec;
    fs::remove_all(r, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
