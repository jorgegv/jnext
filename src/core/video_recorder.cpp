#include "core/video_recorder.h"
#include "core/log.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

VideoRecorder::VideoRecorder() = default;

VideoRecorder::~VideoRecorder()
{
    if (recording_)
        stop();
}

bool VideoRecorder::ffmpeg_available()
{
    return system("ffmpeg -version >/dev/null 2>&1") == 0;
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

    Log::emulator()->info("VideoRecorder: encoding with FFmpeg...");
    int ret = -1;
    for (const auto& enc : encoders) {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
            "ffmpeg -y "
            "-f rawvideo -pixel_format rgb24 -video_size %dx%d -framerate %d -i '%s' "
            "-f s16le -ar %d -ac 2 -i '%s' "
            "-c:v %s %s "
            "-c:a aac -b:a 128k "
            "-shortest -movflags +faststart "
            "'%s' "
            ">/dev/null 2>&1",
            frame_width_, frame_height_, FRAME_RATE, video_tmp_.c_str(),
            SAMPLE_RATE, audio_tmp_.c_str(),
            enc.codec, enc.extra,
            output_path_.c_str());

        Log::emulator()->info("VideoRecorder: trying encoder '{}'", enc.codec);
        Log::emulator()->debug("VideoRecorder: cmd={}", cmd);
        ret = system(cmd);
        if (ret == 0) break;
        Log::emulator()->debug("VideoRecorder: encoder '{}' failed (exit code {})", enc.codec, ret);
    }

    // Clean up temp files.
    std::remove(video_tmp_.c_str());
    std::remove(audio_tmp_.c_str());

    if (ret != 0) {
        Log::emulator()->error("VideoRecorder: FFmpeg encoding failed (exit code {})", ret);
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
