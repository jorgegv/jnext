#include "core/video_recorder.h"
#include "core/log.h"
#include "core/win_process.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

/// Run a command line built for VideoRecorder::NATIVE_STYLE and return its
/// exit status (0 == success). POSIX goes through the shell; Windows spawns
/// the child directly — see src/core/win_process.h for why.
int run_native(const std::string& cmd)
{
#ifdef _WIN32
    // WINDOWS IS NOT BROUGHT UNDER THE `LANG=C` RULE HERE, and that is
    // reported rather than hidden. `win_run_hidden` passes a null
    // `lpEnvironment` to `CreateProcess`, so the child inherits; setting a
    // variable would mean building an environment block, which is a change to
    // that function rather than to this call. It would also buy nothing
    // measurable: Windows localises a console tool by UI language, not by
    // `LANG`. And nothing reads ffmpeg's output on EITHER platform — this one
    // redirects to NUL through `STARTUPINFOA` handles (win_process.cpp) where
    // the POSIX branch below puts `>/dev/null 2>&1` in the command string —
    // so the rule has no functional effect here whichever way it is spelled.
    return win_run_hidden(cmd);
#else
    // EXTERNAL PROCESSES RUN UNDER `LANG=C` (CLAUDE.md). A shell assignment
    // prefix applies it to this command only, without touching jnext's own
    // environment.
    //
    // NOTHING HERE PARSES FFMPEG'S OUTPUT — every command built by this class
    // ends in `>/dev/null 2>&1` and only the exit status is read — so this is
    // conformance with a general rule rather than a fix for a live defect. It
    // is applied anyway, because the next person to add a command here should
    // inherit a locale-clean call site rather than have to think of it.
    //
    // RESIDUAL, stated because it is invisible otherwise: this child INHERITS
    // jnext's environment, and POSIX ranks `LC_ALL` above `LANG`. On a host
    // that exports `LC_ALL`, this prefix does not win. The ESP-01 pinger does
    // not have that problem because it passes an explicit, minimal envp
    // (esp_ping.cpp) — which is the more robust shape where output is parsed.
    return system(("LANG=C " + cmd).c_str());
#endif
}

}  // namespace

VideoRecorder::VideoRecorder() = default;

VideoRecorder::~VideoRecorder()
{
    if (recording_)
        stop();
}

std::string VideoRecorder::quote_arg(const std::string& arg, CmdStyle style)
{
    if (style == CmdStyle::Posix) {
        // Single quotes make every shell metacharacter inert; the only
        // character that cannot appear inside them is the single quote
        // itself, which is spliced in as  '\''  (close, escaped quote, open).
        std::string out = "'";
        for (char c : arg) {
            if (c == '\'') out += "'\\''";
            else           out += c;
        }
        out += '\'';
        return out;
    }

    // Windows: the MSVCRT argv parser the child uses, not cmd.exe. An
    // argument with no whitespace and no quote needs no quoting at all; one
    // that does gets wrapped in double quotes, with every run of backslashes
    // that precedes a quote (or the closing quote) doubled, and every embedded
    // quote escaped. Single quotes mean NOTHING here — the shipped code quoted
    // POSIX-style, so a Windows path was handed to ffmpeg with literal
    // apostrophes around it (GH #56).
    if (!arg.empty() &&
        arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg;
    }

    std::string out = "\"";
    for (size_t i = 0; i < arg.size(); ) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\') { ++backslashes; ++i; }

        if (i == arg.size()) {
            // Trailing backslashes would escape the closing quote: double them.
            out.append(backslashes * 2, '\\');
        } else if (arg[i] == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out += '"';
            ++i;
        } else {
            out.append(backslashes, '\\');
            out += arg[i];
            ++i;
        }
    }
    out += '"';
    return out;
}

std::string VideoRecorder::build_encode_command(const EncodeSpec& spec, CmdStyle style)
{
    std::ostringstream cmd;
    cmd << "ffmpeg -y"
        << " -f rawvideo -pixel_format rgb24"
        << " -video_size " << spec.width << "x" << spec.height
        << " -framerate " << FRAME_RATE
        << " -i " << quote_arg(spec.video_input, style);

    const bool have_audio = !spec.audio_input.empty();
    if (have_audio) {
        cmd << " -f s16le -ar " << SAMPLE_RATE << " -ac 2"
            << " -i " << quote_arg(spec.audio_input, style);
    }

    cmd << " -c:v " << spec.codec << ' ' << spec.codec_opts;

    if (have_audio)
        cmd << " -c:a aac -b:a 128k -shortest";

    cmd << " -movflags +faststart " << quote_arg(spec.output, style);

    if (style == CmdStyle::Posix)
        cmd << " >/dev/null 2>&1";

    return cmd.str();
}

bool VideoRecorder::ffmpeg_in_path_list(const std::string& path_list, CmdStyle style)
{
    const char sep = (style == CmdStyle::Windows) ? ';' : ':';
    const char* const name = (style == CmdStyle::Windows) ? "ffmpeg.exe" : "ffmpeg";
    std::size_t start = 0;
    while (start <= path_list.size()) {
        std::size_t end = path_list.find(sep, start);
        if (end == std::string::npos) end = path_list.size();
        std::string dir = path_list.substr(start, end - start);
        start = end + 1;
        if (dir.empty()) {
            if (style == CmdStyle::Windows) continue;
            dir = ".";  // sh: an empty PATH entry is the current directory
        }
        const fs::path candidate = fs::path(dir) / name;
        std::error_code ec;
        if (!fs::is_regular_file(candidate, ec) || ec) continue;  // follows symlinks
#ifndef _WIN32
        if (style == CmdStyle::Posix && ::access(candidate.c_str(), X_OK) != 0) continue;
#endif
        return true;
    }
    return false;
}

std::string VideoRecorder::ffmpeg_search_list(CmdStyle style, const char* path_env,
                                              const std::string& exe_dir,
                                              const std::string& system_dir,
                                              const std::string& windows_dir,
                                              const std::string& default_path)
{
    if (style == CmdStyle::Posix)
        return path_env ? std::string(path_env) : default_path;

    std::string list;
    const auto add = [&list](const std::string& part) {
        if (part.empty()) return;
        if (!list.empty()) list += ';';
        list += part;
    };
    add(exe_dir);
    add(".");
    add(system_dir);
    if (!windows_dir.empty()) add(windows_dir + "\\System");
    add(windows_dir);
    add(path_env ? path_env : "");
    return list;
}

namespace {

std::atomic<int> g_probe_count{0};

}  // namespace

/// The search list for this platform, from the real environment.
std::string VideoRecorder::native_search_list()
{
    const char* env = std::getenv("PATH");
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    std::string exe_dir, system_dir, windows_dir;
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) exe_dir = fs::path(buf).parent_path().string();
    n = GetSystemDirectoryA(buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) system_dir = buf;
    n = GetWindowsDirectoryA(buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) windows_dir = buf;
    return ffmpeg_search_list(NATIVE_STYLE, env, exe_dir,
                                             system_dir, windows_dir, "");
#else
    // PATH unset: sh uses its own default; confstr(_CS_PATH) is the standard
    // one (the shell's built-in may differ slightly).
    std::string def;
    const size_t need = confstr(_CS_PATH, nullptr, 0);
    if (need > 0) {
        std::string tmp(need, '\0');
        confstr(_CS_PATH, tmp.data(), need);
        def = tmp.c_str();
    }
    return ffmpeg_search_list(NATIVE_STYLE, env, "", "", "", def);
#endif
}


bool VideoRecorder::ffmpeg_available_fresh()
{
    ++g_probe_count;
    return ffmpeg_in_path_list(native_search_list(), NATIVE_STYLE);
}

int VideoRecorder::ffmpeg_probe_count()
{
    return g_probe_count;
}

bool VideoRecorder::ffmpeg_available()
{
    static const bool available = ffmpeg_available_fresh();
    return available;
}

bool VideoRecorder::start(const std::string& output_path)
{
    if (recording_) {
        Log::emulator()->warn("VideoRecorder: already recording");
        return false;
    }

    if (!ffmpeg_available_fresh()) {
        Log::emulator()->error("VideoRecorder: ffmpeg not found in PATH");
        return false;
    }

    output_path_ = output_path;

    // Create temp files in the same directory as output to avoid cross-device issues.
    fs::path out_dir = fs::path(output_path).parent_path();
    if (out_dir.empty()) out_dir = ".";

    video_tmp_ = (out_dir / "jnext_rec_video.raw").string();
    audio_tmp_ = (out_dir / "jnext_rec_audio.raw").string();

    video_file_ = fopen(video_tmp_.c_str(), "wb");
    if (!video_file_) {
        Log::emulator()->error("VideoRecorder: cannot create temp video file: {}", video_tmp_);
        return false;
    }

    audio_file_ = fopen(audio_tmp_.c_str(), "wb");
    if (!audio_file_) {
        Log::emulator()->error("VideoRecorder: cannot create temp audio file: {}", audio_tmp_);
        fclose(video_file_);
        video_file_ = nullptr;
        return false;
    }

    frame_width_ = 0;
    frame_height_ = 0;
    recording_ = true;

    Log::emulator()->info("VideoRecorder: started recording to {}", output_path_);
    return true;
}

bool VideoRecorder::stop()
{
    if (!recording_) return false;

    recording_ = false;

    // Close temp files.
    if (video_file_) { fclose(video_file_); video_file_ = nullptr; }
    if (audio_file_) { fclose(audio_file_); audio_file_ = nullptr; }

    if (frame_width_ == 0 || frame_height_ == 0) {
        Log::emulator()->warn("VideoRecorder: no frames captured, skipping encode");
        std::remove(video_tmp_.c_str());
        std::remove(audio_tmp_.c_str());
        latch_stop_failure();
        return false;
    }

    // Build FFmpeg command.
    // Video: raw RGB24, known dimensions and frame rate.
    // Audio: raw signed 16-bit little-endian stereo.
    // Output: H.264 + AAC in MP4 container.
    // Try encoders in preference order: libx264, libopenh264, mpeg4 (fallback).
    // Encoder preference order: libx264 (best quality/compat), mpeg4 (universal
    // fallback, VLC-safe), libopenh264 (last resort — known VLC issues).
    struct Encoder { const char* codec; const char* extra; };
    Encoder encoders[] = {
        {"libx264",      "-preset fast -crf 18 -pix_fmt yuv420p"},
        {"mpeg4",        "-q:v 3 -pix_fmt yuv420p"},
        {"libopenh264",  "-pix_fmt yuv420p"},
    };

    // Task 47 (--silent): with audio synthesis skipped, audio_tmp_ is a
    // 0-byte file. Feeding ffmpeg a zero-duration raw-PCM input alongside
    // "-shortest" makes it clamp the WHOLE output to that zero duration —
    // ffmpeg exits 0 having written a structurally-valid but empty/corrupt
    // MP4 ("Output file is empty, nothing was encoded"), and the caller
    // reported success. Detected by review (Task 47 fix-round). When there
    // is no audio to mux, build a video-only command instead: no audio
    // input, no "-c:a", no "-shortest" (nothing to be the shortest OF).
    std::error_code fs_ec;
    const bool have_audio = fs::file_size(audio_tmp_, fs_ec) > 0 && !fs_ec;
    if (!have_audio) {
        Log::emulator()->info(
            "VideoRecorder: no audio was captured (--silent, or a source with "
            "no output) - encoding video-only");
    }

    // FFmpeg is invoked with -y, so this request explicitly replaces its
    // target. Remove it ourselves first: otherwise a broken/no-op encoder
    // could exit 0 and leave a stale non-empty MP4 that looks like this run's
    // result to the validation below.
    std::error_code output_ec;
    fs::remove(output_path_, output_ec);
    if (output_ec) {
        std::remove(video_tmp_.c_str());
        std::remove(audio_tmp_.c_str());
        Log::emulator()->error("VideoRecorder: cannot replace output file {}: {}",
                               output_path_, output_ec.message());
        latch_stop_failure();
        return false;
    }

    Log::emulator()->debug("VideoRecorder: encoding with FFmpeg...");
    int ret = -1;
    for (const auto& enc : encoders) {
        EncodeSpec spec;
        spec.width       = frame_width_;
        spec.height      = frame_height_;
        spec.video_input = video_tmp_;
        spec.audio_input = have_audio ? audio_tmp_ : std::string();
        spec.output      = output_path_;
        spec.codec       = enc.codec;
        spec.codec_opts  = enc.extra;

        // std::string, not a fixed char[2048]: snprintf truncates silently,
        // and a truncated command is a malformed command. Windows paths plus
        // quote expansion make that far easier to hit.
        const std::string cmd = build_encode_command(spec, NATIVE_STYLE);

        Log::emulator()->debug("VideoRecorder: trying encoder '{}'", enc.codec);
        Log::emulator()->debug("VideoRecorder: cmd={}", cmd);
        ret = run_native(cmd);
        if (ret == 0) break;
        Log::emulator()->debug("VideoRecorder: encoder '{}' failed (exit code {})", enc.codec, ret);
    }

    // Clean up temp files.
    std::remove(video_tmp_.c_str());
    std::remove(audio_tmp_.c_str());

    // GH #86 (cosmetic): an encoder that creates its output file and then
    // fails (or "succeeds" writing nothing) leaves a 0-byte artifact a later
    // glance could mistake for a recording. Remove it on the failure paths
    // below. Only a ZERO-length leftover is removed — a partial non-empty
    // file may still hold recoverable data and stays for inspection.
    auto remove_empty_output = [this]() {
        std::error_code ec;
        if (fs::exists(output_path_, ec) && !ec &&
            fs::file_size(output_path_, ec) == 0 && !ec) {
            fs::remove(output_path_, ec);
        }
    };

    if (ret != 0) {
        Log::emulator()->error("VideoRecorder: FFmpeg encoding failed (exit code {})", ret);
        remove_empty_output();
        latch_stop_failure();
        return false;
    }

    const auto output_size = fs::file_size(output_path_, output_ec);
    if (output_ec || output_size == 0) {
        Log::emulator()->error(
            "VideoRecorder: FFmpeg reported success but produced no usable output at {}",
            output_path_);
        remove_empty_output();
        latch_stop_failure();
        return false;
    }

    Log::emulator()->info("VideoRecorder: recording saved to {}", output_path_);
    // GH #86: a successful recording to this path supersedes an earlier
    // failed one — the requested artifact now exists and is valid.
    failed_outputs_.erase(
        std::remove(failed_outputs_.begin(), failed_outputs_.end(), output_path_),
        failed_outputs_.end());
    return true;
}

bool VideoRecorder::output_failed(const std::string& path) const
{
    return std::find(failed_outputs_.begin(), failed_outputs_.end(), path) !=
           failed_outputs_.end();
}

void VideoRecorder::latch_stop_failure()
{
    if (!output_failed(output_path_))
        failed_outputs_.push_back(output_path_);
}

void VideoRecorder::capture_frame(const uint32_t* framebuffer, int width, int height)
{
    if (!recording_ || !video_file_) return;

    // The IN-MEMORY framebuffer is 640×256; FFmpeg gets 640×512 frames so
    // the recorded MP4 has square pixels (G104 Phase 7). Each input row is
    // emitted twice into the raw RGB24 stream.
    const int out_height = height * 2;

    // Store dimensions from first frame. frame_height_ reflects the OUTPUT
    // height (post-doubling), so FFmpeg's -video_size is correct.
    if (frame_width_ == 0) {
        frame_width_ = width;
        frame_height_ = out_height;
        rgb_buffer_.resize(static_cast<size_t>(width) * out_height * 3);
    }

    // Convert ARGB8888 (0xAARRGGBB) to RGB24, writing each input row twice.
    uint8_t* dst = rgb_buffer_.data();
    const size_t row_bytes = static_cast<size_t>(width) * 3;
    for (int y = 0; y < height; ++y) {
        const uint32_t* src = framebuffer + static_cast<size_t>(y) * width;
        uint8_t* dst_row = dst + static_cast<size_t>(y) * 2 * row_bytes;
        for (int x = 0; x < width; ++x) {
            uint32_t argb = src[x];
            dst_row[x * 3 + 0] = static_cast<uint8_t>((argb >> 16) & 0xFF); // R
            dst_row[x * 3 + 1] = static_cast<uint8_t>((argb >> 8) & 0xFF);  // G
            dst_row[x * 3 + 2] = static_cast<uint8_t>(argb & 0xFF);         // B
        }
        // Replicate row N as the second of two output rows (vertical 2×).
        std::memcpy(dst_row + row_bytes, dst_row, row_bytes);
    }

    fwrite(rgb_buffer_.data(), 1, rgb_buffer_.size(), video_file_);
}

void VideoRecorder::capture_audio(const int16_t* samples, int count)
{
    if (!recording_ || !audio_file_) return;

    // Write interleaved stereo s16le samples.
    // count = number of stereo pairs, so total int16_t values = count * 2.
    fwrite(samples, sizeof(int16_t), static_cast<size_t>(count) * 2, audio_file_);
}
