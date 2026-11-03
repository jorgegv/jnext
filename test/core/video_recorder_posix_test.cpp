// POSIX-only rows of the ffmpeg PATH lookup (GH #446). They need execute bits,
// symbolic links and a shell script that would leave a marker if it ran, none
// of which Windows (or wine) provides; the all-OS rows FFL-01..14 live in
// video_recorder_cmd_test. Oracle: execvp's search rule (a regular file with
// an execute bit for the caller; symlinks are followed) and "no process is
// started" proven by a decoy ffmpeg that writes a marker file when run.
//
// Run: ./build/test/video_recorder_posix_test

#include "core/video_recorder.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <string>

#include "../row_id.h"
#include "../test_portable.h"

namespace fs = std::filesystem;

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

bool in(const std::string& list)
{
    return VideoRecorder::ffmpeg_in_path_list(list, VideoRecorder::CmdStyle::Posix);
}

}  // namespace

int main()
{
    std::printf("ffmpeg PATH lookup, POSIX-only rows (GH #446)\n");
    std::printf("====================================================\n\n");

    const fs::path root = jtp::make_temp_dir("jnext_ffp_");
    const bool ok = !root.empty();
    const fs::path noexec = root / "noexec", links = root / "links", real = root / "real",
                   decoy = root / "decoy", broken = root / "broken";
    for (const auto& d : {noexec, links, real, decoy, broken}) fs::create_directories(d);
    const std::string marker = (root / "MARKER").string();

    std::ofstream(noexec / "ffmpeg") << "#!/bin/sh\n";
    chmod((noexec / "ffmpeg").c_str(), 0644);
    check("FFP-01", "a regular file named ffmpeg WITHOUT an execute bit is not ffmpeg",
          ok && !in(noexec.string()));

    std::ofstream(real / "ffmpeg") << "#!/bin/sh\n";
    chmod((real / "ffmpeg").c_str(), 0755);
    std::error_code ec;
    fs::create_symlink(real / "ffmpeg", links / "ffmpeg", ec);
    check("FFP-02", "a symlink to an executable ffmpeg is found", ok && !ec && in(links.string()));

    fs::create_symlink(root / "does-not-exist", broken / "ffmpeg", ec);
    check("FFP-03", "a dangling symlink named ffmpeg is not found", ok && !ec && !in(broken.string()));

    {
        std::ofstream(decoy / "ffmpeg") << "#!/bin/sh\ntouch '" << marker << "'\n";
        chmod((decoy / "ffmpeg").c_str(), 0755);
        const char* old = std::getenv("PATH");
        const std::string old_path = old ? old : "";
        jtp::set_env("PATH", (decoy.string() + ":" + old_path).c_str());
        // The fresh lookup and the cached one are the startup consumers; the
        // recorder's start() refusal path is not needed here.
        const bool a = VideoRecorder::ffmpeg_available_fresh();
        const bool b = VideoRecorder::ffmpeg_available();
        jtp::set_env("PATH", old_path.c_str());
        check("FFP-04", "the decoy ffmpeg is found by both lookups", ok && a && b);
        check("FFP-05", "and NO process was started: the decoy never ran (no marker file)",
              ok && !fs::exists(marker));
    }

    {
        // The environment the spawn (sh) would run in. PATH="" is the current
        // directory for sh, so the decoy in cwd is found; it must not run.
        const char* old = std::getenv("PATH");
        const std::string old_path = old ? old : "";
        const fs::path saved = fs::current_path(ec);
        fs::current_path(decoy, ec);
        jtp::set_env("PATH", "");
        const bool empty_found = VideoRecorder::ffmpeg_available_fresh();
        // Unset PATH: the default of confstr(_CS_PATH), the standard one.
        fs::current_path(noexec, ec);  // no runnable ffmpeg here: only the default list can find one
        jtp::unset_env("PATH");
        const bool unset_found = VideoRecorder::ffmpeg_available_fresh();
        std::string def;
        const size_t need = confstr(_CS_PATH, nullptr, 0);
        if (need > 0) { def.assign(need, '\0'); confstr(_CS_PATH, def.data(), need); def = def.c_str(); }
        const bool unset_want = VideoRecorder::ffmpeg_in_path_list(def, VideoRecorder::CmdStyle::Posix);
        fs::current_path(saved, ec);
        jtp::set_env("PATH", old_path.c_str());
        check("FFP-06", "an empty PATH is the current directory: the decoy ffmpeg there is found, and not run",
              ok && empty_found && !fs::exists(marker));
        check("FFP-07", "an UNSET PATH searches the confstr(_CS_PATH) default, not nothing",
              ok && unset_found == unset_want);
    }

    fs::remove_all(root, ec);
    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
