#include "core/video_recorder.h"
#include "core/log.h"
#include "core/win_process.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace {

/// Run a command line built for VideoRecorder::NATIVE_STYLE and return its
/// exit status (0 == success). POSIX goes through the shell; Windows spawns
/// the child directly — see src/core/win_process.h for why.
int run_native(const std::string& cmd)
{
#ifdef _WIN32
    return win_run_hidden(cmd);
#else
    return system(cmd.c_str());
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

std::string VideoRecorder::build_probe_command(CmdStyle style)
{
    // `>/dev/null 2>&1` is shell syntax, and on Windows /dev/null is just a
    // path that does not exist — cmd.exe would try to create \dev\null on the
    // current drive. GH #56 reports the consequence as ffmpeg_available()
    // always returning false, leaving File > Record MPEG4 Video permanently
    // greyed out. NOT INDEPENDENTLY CONFIRMED here: the only Windows-ish
    // runtime on the dev host is wine, whose file layer special-cases
    // /dev/null, so it cannot answer the question either way. What IS
    // confirmed is that the POSIX quoting below made the encode itself
    // impossible regardless of the probe.
    //
    // The Windows command carries no redirection at all: there is no shell to
    // interpret one, and win_run_hidden() points the child's stdio at NUL
    // through STARTUPINFO instead.
    if (style == CmdStyle::Posix)
        return "ffmpeg -version >/dev/null 2>&1";
    return "ffmpeg -version";
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

bool VideoRecorder::ffmpeg_available()
{
    return run_native(build_probe_command(NATIVE_STYLE)) == 0;
}

bool VideoRecorder::start(const std::string& output_path)
{
    if (recording_) {
        Log::emulator()->warn("VideoRecorder: already recording");
        return false;
    }

    if (!ffmpeg_available()) {
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
        stop_failed_ = true;
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
            "no output) — encoding video-only");
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
        stop_failed_ = true;
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
        stop_failed_ = true;
        return false;
    }

    const auto output_size = fs::file_size(output_path_, output_ec);
    if (output_ec || output_size == 0) {
        Log::emulator()->error(
            "VideoRecorder: FFmpeg reported success but produced no usable output at {}",
            output_path_);
        remove_empty_output();
        stop_failed_ = true;
        return false;
    }

    Log::emulator()->info("VideoRecorder: recording saved to {}", output_path_);
    return true;
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
