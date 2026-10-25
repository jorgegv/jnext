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
#include <iterator>
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
std::string slurp(const std::string& p)
{
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f), {});
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

    // RECF-20: a self-looping symlink cannot be resolved at all, so key() must
    // fall back to the lexical form rather than an empty key shared by every
    // path beneath it (two files would collapse into one entry).
    {
        const fs::path loop = r / "loop";
        std::error_code lpe;
        fs::create_symlink(loop, loop, lpe);
        recent_files::List m;
        recent_files::push(m, (loop / "a.nex").string());
        recent_files::push(m, (loop / "b.nex").string());
        const bool two = m.size() == 2;
        recent_files::remove(m, (loop / "a.nex").string());
        std::string d = lpe ? "loop not created: " + lpe.message() : "";
        for (const auto& e : m) d += " | " + e;
        check("RECF-20", "paths under an unresolvable symlink loop stay distinct entries",
              !lpe && two && m.size() == 1 && m[0] == (loop / "b.nex").string(), d);
    }

    // RECF-21: "link/../game.nex" crosses the symlink: the real file is the
    // sibling of the link's TARGET, which a lexical fold of ".." gets wrong.
    // The path is stored as given and the OS resolves it at open time, so what
    // the stored path OPENS must be the real file.
    {
        const fs::path t = r / "tdir" / "sub", other = r / "ldir";
        std::error_code e1, e2;
        fs::create_directories(t, e1);
        fs::create_directories(other, e1);
        std::ofstream(r / "tdir" / "game.nex") << "real";   // the real file
        std::ofstream(other / "game.nex") << "decoy";       // the lexical decoy
        fs::create_directory_symlink(t, other / "lnk", e2);
        recent_files::List m;
        recent_files::push(m, (other / "lnk" / ".." / "game.nex").string());
        std::string d = (e1 || e2) ? "fixture not created" : "";
        for (const auto& e : m) d += " | " + e;
        check("RECF-21", "push through link/.. records a path that reopens the real file, not the lexical sibling",
              !e1 && !e2 && m.size() == 1 && slurp(m[0]) == "real", d);
    }

    // RECF-22: a symlink a build script re-points (latest.nex -> new build) is
    // stored under its OWN name, so a reload opens the current target and the
    // menu label (the stored path's file name) is the name the user picked.
    {
        std::ofstream(r / "v1.nex") << "v1";
        std::ofstream(r / "v2.nex") << "v2";
        const fs::path lat = r / "latest.nex";
        std::error_code e1;
        fs::create_symlink(r / "v1.nex", lat, e1);
        recent_files::List m;
        recent_files::push(m, lat.string());
        const bool first = m.size() == 1 && slurp(m[0]) == "v1";
        std::error_code e2, e3;
        fs::remove(lat, e2);
        fs::create_symlink(r / "v2.nex", lat, e3);
        std::string d = (e1 || e2 || e3) ? "fixture not created" : "";
        for (const auto& e : m) d += " | " + e;
        check("RECF-22", "a re-pointed file symlink is stored under its own name and reloads the new target",
              !e1 && !e2 && !e3 && first && m.size() == 1 && m[0] == lat.string() &&
              fs::path(m[0]).filename() == "latest.nex" && slurp(m[0]) == "v2", d);
    }

    // RECF-23: re-pushing another spelling of an entry replaces it at the top
    // with the NEW spelling (the symlink name, not the earlier target name).
    {
        recent_files::List m;
        recent_files::push(m, (r / "v2.nex").string());
        recent_files::push(m, (r / "latest.nex").string());
        check("RECF-23", "pushing a symlink spelling of a listed file replaces the entry with the new spelling",
              m.size() == 1 && m[0] == (r / "latest.nex").string(), m.empty() ? "" : m[0]);
    }

    // RECF-25: a hand-typed "link/../x" entry records the folder it is really
    // in as the last load directory, and the dialog then preselects it (the
    // lexical folder, "ldir", is a different directory).
    {
        recent_files::List m;
        recent_files::push(m, (r / "ldir" / "lnk" / ".." / "game.nex").string());
        const std::string dir = recent_files::folder(m.empty() ? "" : m[0]);
        check("RECF-25", "a link/.. entry's recorded folder makes the dialog seed that entry",
              m.size() == 1 && recent_files::dialog_seed(m, dir) == m[0], dir);
    }

    std::error_code ec;
    fs::remove_all(r, ec);
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
