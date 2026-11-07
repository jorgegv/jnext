// The stop-failure latch harness (SF rows), shared by video_recorder_cmd_test and
// video_recorder_cmd_native_test (single source): a stub `ffmpeg` placed first on PATH,
// its behaviour chosen by JNEXT_TEST_FFMPEG_MODE. Needs JNEXT_FFMPEG_STUB.
#pragma once

#include "core/video_recorder.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "../test_portable.h"

namespace vr_sf {

namespace fs = std::filesystem;

inline fs::path g_sf_dir;  // scratch dir for the SF rows

inline bool sf_setup()
{
    g_sf_dir = fs::temp_directory_path() /
               ("jnext_vr_stopfail_" + jtp::process_id_string());
    std::error_code ec;
    fs::create_directories(g_sf_dir, ec);
    if (ec) return false;

    // The stand-in is a real program (core/ffmpeg_stub.cpp), copied under the
    // name the recorder runs: `ffmpeg` on POSIX, `ffmpeg.exe` on Windows (where
    // CreateProcess searches PATH for the name plus ".exe").
#ifdef _WIN32
    const fs::path stub = g_sf_dir / "ffmpeg.exe";
#else
    const fs::path stub = g_sf_dir / "ffmpeg";
#endif
    fs::copy_file(JNEXT_FFMPEG_STUB, stub, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;

#ifdef _WIN32
    const char path_sep = ';';
#else
    const char path_sep = ':';
#endif
    const char* old_path = std::getenv("PATH");
    const std::string new_path =
        g_sf_dir.string() + path_sep + (old_path ? old_path : "/usr/bin:/bin");
    return jtp::set_env("PATH", new_path.c_str()) == 0;
}

inline void sf_mode(const char* mode)
{
    jtp::set_env("JNEXT_TEST_FFMPEG_MODE", mode);
}

/// start() + one captured frame, so stop() reaches the encode step.
inline bool sf_start_with_frame(VideoRecorder& rec, const std::string& name)
{
    if (!rec.start((g_sf_dir / name).string())) return false;
    const uint32_t frame[16] = {};  // 4x4 ARGB
    rec.capture_frame(frame, 4, 4);
    return true;
}

inline std::string sf_path(const std::string& name)
{
    return (g_sf_dir / name).string();
}

inline bool sf_output_exists(const std::string& name)
{
    std::error_code ec;
    return fs::exists(g_sf_dir / name, ec);
}


}  // namespace vr_sf
