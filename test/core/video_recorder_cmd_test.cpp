// FFmpeg command-line construction test (GH #56).
//
// jnext shells out to ffmpeg to mux a recording. The shipped code built ONE
// command line — a POSIX one — with no _WIN32 guard anywhere in
// src/core/video_recorder.cpp:
//
//   * the probe was  system("ffmpeg -version >/dev/null 2>&1").  /dev/null is
//     shell syntax for a path that does not exist on Windows. GH #56 reports
//     the consequence as ffmpeg_available() always false and "File > Record
//     MPEG4 Video" permanently greyed out. That specific symptom could NOT be
//     confirmed on this dev host — wine, the only Windows-ish runtime here,
//     special-cases /dev/null — so it is taken from the issue, not verified.
//   * arguments were single-quoted ('%s'). Single quotes are not a quoting
//     character on Windows. THIS half was confirmed: run through cmd.exe, a
//     path like C:\Some Name Dir\v.raw reached ffmpeg as THREE arguments —
//     ['C:\Some] [Name] [Dir\v.raw'] — so the encode could never have worked
//     whatever the probe did.
//
// This suite is the platform-independent half of the fix: it exercises BOTH
// dialects from whichever platform it runs on, because the builders take the
// dialect as a parameter rather than selecting it with #ifdef. What it can
// prove is that the command TEXT is right for each dialect. What it cannot
// prove — and no test on this host can — is that the resulting command runs
// correctly on real Windows; only the POSIX half is executed end-to-end, by
// the video-record / silent-record functional regression rows.
//
// Oracle: not VHDL (this is host tooling, no FPGA counterpart). The Windows
// rules come from the MSVCRT/CommandLineToArgvW argument-parsing contract —
// quote wrapping, backslash doubling before a quote or the closing quote —
// and from cmd.exe having NUL rather than /dev/null. The POSIX rules come
// from sh(1) single-quote semantics.
//
// Run: ./build/test/video_recorder_cmd_test

#include "core/video_recorder.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using Style = VideoRecorder::CmdStyle;

std::string q(const std::string& s, Style style)
{
    return VideoRecorder::quote_arg(s, style);
}

bool contains(const std::string& hay, const std::string& needle)
{
    return hay.find(needle) != std::string::npos;
}

std::string got_want(const std::string& got, const std::string& want)
{
    return "got <" + got + "> want <" + want + ">";
}

/// A representative encode spec with audio.
VideoRecorder::EncodeSpec spec_with_audio(const std::string& dir, char sep)
{
    VideoRecorder::EncodeSpec s;
    s.width       = 640;
    s.height      = 512;
    s.video_input = dir + sep + "jnext_rec_video.raw";
    s.audio_input = dir + sep + "jnext_rec_audio.raw";
    s.output      = dir + sep + "out.mp4";
    s.codec       = "libx264";
    s.codec_opts  = "-preset fast -crf 18 -pix_fmt yuv420p";
    return s;
}

// ---------------------------------------------------------------
// GH #86 — stop-failure latch harness (POSIX-executed).
//
// stop_failed() is the seam that carries a discarded stop_recording()
// result (MainWindow::closeEvent()) into main.cpp's exit status. These
// helpers drive real start()/capture_frame()/stop() cycles against a stub
// `ffmpeg` placed FIRST on PATH, whose behaviour is selected with the
// JNEXT_TEST_FFMPEG_MODE environment variable (same convention as
// test/00regression/scripts/video-record-status-func.sh). Like the
// encode-command rows' redirections, actually EXECUTING the stub is
// POSIX-only; the Windows build never compiles this suite
// (make package-win configures -DENABLE_TESTS=OFF).
// ---------------------------------------------------------------
namespace fs = std::filesystem;

fs::path g_sf_dir;  // scratch dir for the SF rows

bool sf_setup()
{
#ifdef _WIN32
    return false;  // never built; see header comment
#else
    g_sf_dir = fs::temp_directory_path() /
               ("jnext_vr_stopfail_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::create_directories(g_sf_dir, ec);
    if (ec) return false;

    const fs::path stub = g_sf_dir / "ffmpeg";
    {
        std::ofstream f(stub);
        f << "#!/bin/sh\n"
             "[ \"$1\" = \"-version\" ] && exit 0\n"
             "for out do :; done\n"
             "case \"$JNEXT_TEST_FFMPEG_MODE\" in\n"
             "  encode-fail)   : > \"$out\"; exit 42 ;;\n"    // 0-byte artifact, then fail
             "  empty-success) : > \"$out\"; exit 0 ;;\n"     // 0-byte artifact, "success"
             "  success)       printf x > \"$out\"; exit 0 ;;\n"
             "esac\n"
             "exit 99\n";
        if (!f) return false;
    }
    if (::chmod(stub.c_str(), 0755) != 0) return false;

    const char* old_path = std::getenv("PATH");
    const std::string new_path =
        g_sf_dir.string() + ":" + (old_path ? old_path : "/usr/bin:/bin");
    return ::setenv("PATH", new_path.c_str(), 1) == 0;
#endif
}

void sf_mode(const char* mode)
{
#ifndef _WIN32
    ::setenv("JNEXT_TEST_FFMPEG_MODE", mode, 1);
#else
    (void)mode;
#endif
}

/// start() + one captured frame, so stop() reaches the encode step.
bool sf_start_with_frame(VideoRecorder& rec, const std::string& name)
{
    if (!rec.start((g_sf_dir / name).string())) return false;
    const uint32_t frame[16] = {};  // 4x4 ARGB
    rec.capture_frame(frame, 4, 4);
    return true;
}

bool sf_output_exists(const std::string& name)
{
    std::error_code ec;
    return fs::exists(g_sf_dir / name, ec);
}

}  // namespace

int main()
{
    std::printf("FFmpeg command construction (GH #56) — POSIX and Windows dialects\n");
    std::printf("====================================================\n\n");

    // ---------------------------------------------------------------
    // POSIX argument quoting — sh(1) single-quote semantics.
    // ---------------------------------------------------------------
    {
        const std::string got = q("/tmp/jnext_rec_video.raw", Style::Posix);
        check("QP-01", "a plain POSIX path is wrapped in single quotes",
              got == "'/tmp/jnext_rec_video.raw'",
              got_want(got, "'/tmp/jnext_rec_video.raw'"));
    }
    {
        const std::string got = q("/home/Some Name/rec.mp4", Style::Posix);
        check("QP-02", "spaces survive inside the single quotes",
              got == "'/home/Some Name/rec.mp4'",
              got_want(got, "'/home/Some Name/rec.mp4'"));
    }
    {
        // The shipped code interpolated the path raw into '%s': one apostrophe
        // in a filename closed the quote and handed the rest to the shell.
        const std::string got = q("/tmp/it's here.mp4", Style::Posix);
        check("QP-03", "an embedded apostrophe is spliced as '\\'' , not left to close the quote",
              got == "'/tmp/it'\\''s here.mp4'",
              got_want(got, "'/tmp/it'\\''s here.mp4'"));
    }
    {
        const std::string got = q("/tmp/a; rm -rf b.mp4", Style::Posix);
        check("QP-04", "shell metacharacters stay inert (no unquoted ; reaches sh)",
              got == "'/tmp/a; rm -rf b.mp4'",
              got_want(got, "'/tmp/a; rm -rf b.mp4'"));
    }

    // ---------------------------------------------------------------
    // Windows argument quoting — MSVCRT argv rules.
    // ---------------------------------------------------------------
    {
        const std::string got = q("C:\\tmp\\jnext_rec_video.raw", Style::Windows);
        check("QW-01", "a Windows path with no whitespace needs no quoting",
              got == "C:\\tmp\\jnext_rec_video.raw",
              got_want(got, "C:\\tmp\\jnext_rec_video.raw"));
    }
    {
        // The defect, directly: the shipped builder produced
        //   'C:\Users\Some Name\rec.mp4'
        // and ffmpeg would have looked for a file whose name starts with an
        // apostrophe.
        const std::string got = q("C:\\Users\\Some Name\\rec.mp4", Style::Windows);
        check("QW-02", "a Windows path with spaces is DOUBLE-quoted, never single-quoted",
              got == "\"C:\\Users\\Some Name\\rec.mp4\"",
              got_want(got, "\"C:\\Users\\Some Name\\rec.mp4\""));
    }
    {
        const std::string got = q("C:\\Some Dir\\", Style::Windows);
        check("QW-03", "trailing backslashes are doubled so they do not escape the closing quote",
              got == "\"C:\\Some Dir\\\\\"",
              got_want(got, "\"C:\\Some Dir\\\\\""));
    }
    {
        const std::string got = q("a\"b", Style::Windows);
        check("QW-04", "an embedded double quote is backslash-escaped",
              got == "\"a\\\"b\"", got_want(got, "\"a\\\"b\""));
    }
    {
        const std::string got = q("a\\\\\"b", Style::Windows);
        check("QW-05", "backslashes preceding an embedded quote are doubled, then the quote escaped",
              got == "\"a\\\\\\\\\\\"b\"", got_want(got, "\"a\\\\\\\\\\\"b\""));
    }
    {
        const std::string got = q("", Style::Windows);
        check("QW-06", "an empty argument becomes \"\" rather than vanishing",
              got == "\"\"", got_want(got, "\"\""));
    }

    // ---------------------------------------------------------------
    // The probe — the command that decided the menu item was greyed out.
    // ---------------------------------------------------------------
    {
        const std::string got = VideoRecorder::build_probe_command(Style::Posix);
        check("PR-01", "the POSIX probe discards output with >/dev/null 2>&1",
              got == "ffmpeg -version >/dev/null 2>&1",
              got_want(got, "ffmpeg -version >/dev/null 2>&1"));
    }
    {
        // THE bug. /dev/null in a cmd.exe command line is a nonexistent path,
        // so the probe could never return 0 on Windows.
        const std::string got = VideoRecorder::build_probe_command(Style::Windows);
        check("PR-02", "the Windows probe carries no /dev/null redirection",
              !contains(got, "/dev/null"), got_want(got, "no /dev/null"));
        check("PR-03", "the Windows probe is a bare argv line (stdio is redirected by the spawner)",
              got == "ffmpeg -version", got_want(got, "ffmpeg -version"));
    }

    // ---------------------------------------------------------------
    // The encode command.
    // ---------------------------------------------------------------
    {
        const auto s = spec_with_audio("/tmp", '/');
        const std::string got  = VideoRecorder::build_encode_command(s, Style::Posix);
        const std::string want =
            "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size 640x512 -framerate 50 "
            "-i '/tmp/jnext_rec_video.raw' "
            "-f s16le -ar 44100 -ac 2 -i '/tmp/jnext_rec_audio.raw' "
            "-c:v libx264 -preset fast -crf 18 -pix_fmt yuv420p "
            "-c:a aac -b:a 128k -shortest "
            "-movflags +faststart '/tmp/out.mp4' >/dev/null 2>&1";
        check("EN-01", "POSIX audio+video command is unchanged from the shipped one",
              got == want, got_want(got, want));
    }
    {
        auto s = spec_with_audio("/tmp", '/');
        s.audio_input.clear();                       // --silent: video-only
        const std::string got  = VideoRecorder::build_encode_command(s, Style::Posix);
        const std::string want =
            "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size 640x512 -framerate 50 "
            "-i '/tmp/jnext_rec_video.raw' "
            "-c:v libx264 -preset fast -crf 18 -pix_fmt yuv420p "
            "-movflags +faststart '/tmp/out.mp4' >/dev/null 2>&1";
        check("EN-02", "POSIX video-only command drops -i audio, -c:a and -shortest (Task 47)",
              got == want, got_want(got, want));
    }
    {
        const auto s = spec_with_audio("C:\\Users\\Some Name", '\\');
        const std::string got  = VideoRecorder::build_encode_command(s, Style::Windows);
        const std::string want =
            "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size 640x512 -framerate 50 "
            "-i \"C:\\Users\\Some Name\\jnext_rec_video.raw\" "
            "-f s16le -ar 44100 -ac 2 -i \"C:\\Users\\Some Name\\jnext_rec_audio.raw\" "
            "-c:v libx264 -preset fast -crf 18 -pix_fmt yuv420p "
            "-c:a aac -b:a 128k -shortest "
            "-movflags +faststart \"C:\\Users\\Some Name\\out.mp4\"";
        check("EN-03", "Windows audio+video command: double-quoted paths, no shell redirection",
              got == want, got_want(got, want));
    }
    {
        auto s = spec_with_audio("C:\\Users\\Some Name", '\\');
        s.audio_input.clear();
        const std::string got = VideoRecorder::build_encode_command(s, Style::Windows);
        check("EN-04", "Windows video-only command drops the audio input and -shortest",
              !contains(got, "s16le") && !contains(got, "-shortest") &&
                  !contains(got, "-c:a"),
              got_want(got, "no s16le / -shortest / -c:a"));
        check("EN-05", "no POSIX artefact leaks into the Windows command",
              !contains(got, "/dev/null") && !contains(got, "'"),
              got_want(got, "no /dev/null and no apostrophe"));
    }
    {
        // The shipped builder used snprintf into a fixed char[2048]: a long
        // path truncated the command SILENTLY, producing a malformed line and
        // an unexplained encode failure. Windows paths plus quote expansion
        // make that reachable.
        const std::string long_dir(1500, 'd');
        auto s = spec_with_audio("/tmp/" + long_dir, '/');
        const std::string got = VideoRecorder::build_encode_command(s, Style::Posix);
        check("EN-06", "a >2 KB command is not truncated (no fixed-size buffer)",
              contains(got, "'/tmp/" + long_dir + "/out.mp4'") &&
                  contains(got, ">/dev/null 2>&1"),
              "command length " + std::to_string(got.size()));
    }

    // ---------------------------------------------------------------
    // The dialect this build actually executes.
    // ---------------------------------------------------------------
    {
#ifdef _WIN32
        const bool ok = (VideoRecorder::NATIVE_STYLE == Style::Windows);
        const char* what = "this is a Windows build, so NATIVE_STYLE is Windows";
#else
        const bool ok = (VideoRecorder::NATIVE_STYLE == Style::Posix);
        const char* what = "this is a POSIX build, so NATIVE_STYLE is Posix";
#endif
        check("NS-01", what, ok);
    }

    // ---------------------------------------------------------------
    // GH #86 — the stop-failure latch (stop_failed()).
    //
    // main.cpp reads this after run() so that a stop whose return value was
    // discarded (MainWindow::closeEvent() on window close) still fails the
    // exit status of a --record run. The closeEvent() call itself is a Qt
    // GUI path that cannot be driven from this suite — these rows pin the
    // seam it feeds. Executed against the PATH-stubbed ffmpeg (sf_setup).
    // ---------------------------------------------------------------
    const bool sf_ready = sf_setup();
    check("SF-01", "harness: stub ffmpeg installed first on PATH", sf_ready,
          g_sf_dir.string());
    {
        VideoRecorder rec;
        check("SF-02", "a fresh recorder reports stop_failed() == false",
              !rec.stop_failed());
        const bool r = rec.stop();
        check("SF-03", "stop() with no active recording returns false and does NOT latch "
                       "(misuse guard, not a failed recording)",
              !r && !rec.stop_failed());
    }
    {
        VideoRecorder rec;
        sf_mode("encode-fail");
        const bool started = sf_ready && sf_start_with_frame(rec, "encfail.mp4");
        const bool stopped = started && rec.stop();
        check("SF-04", "a failed encode makes stop() return false AND latch stop_failed()",
              started && !stopped && rec.stop_failed());
        check("SF-05", "a failed encode leaves no 0-byte artifact behind (GH #86 cosmetic)",
              started && !sf_output_exists("encfail.mp4"));
    }
    {
        VideoRecorder rec;
        sf_mode("empty-success");
        const bool started = sf_ready && sf_start_with_frame(rec, "empty.mp4");
        const bool stopped = started && rec.stop();
        check("SF-06", "encoder success with a 0-byte output makes stop() false and latches",
              started && !stopped && rec.stop_failed());
        check("SF-07", "the 0-byte 'successful' output is removed (GH #86 cosmetic)",
              started && !sf_output_exists("empty.mp4"));
    }
    {
        VideoRecorder rec;
        sf_mode("success");
        const bool started = sf_ready && sf_start_with_frame(rec, "good.mp4");
        const bool stopped = started && rec.stop();
        check("SF-08", "a successful recording: stop() true, stop_failed() stays false",
              started && stopped && !rec.stop_failed());
    }
    {
        // Sticky by design: the CLI-requested recording that failed still
        // never materialized, so a LATER successful one must not clear it.
        VideoRecorder rec;
        sf_mode("encode-fail");
        bool ok = sf_ready && sf_start_with_frame(rec, "sticky1.mp4");
        ok = ok && !rec.stop() && rec.stop_failed();
        sf_mode("success");
        ok = ok && sf_start_with_frame(rec, "sticky2.mp4");
        const bool second_ok = ok && rec.stop();
        check("SF-09", "the latch is STICKY: a later successful recording does not clear it",
              ok && second_ok && rec.stop_failed());
    }
    {
        VideoRecorder rec;
        sf_mode("success");
        const bool started = sf_ready && rec.start((g_sf_dir / "noframes.mp4").string());
        const bool stopped = started && rec.stop();  // no capture_frame(): nothing to encode
        check("SF-10", "stop() with no frames captured returns false and latches",
              started && !stopped && rec.stop_failed());
    }
    if (sf_ready) {
        std::error_code ec;
        fs::remove_all(g_sf_dir, ec);
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
