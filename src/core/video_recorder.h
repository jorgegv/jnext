#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <functional>

/// Records emulator video (ARGB8888 frames) and audio (stereo s16le) to
/// temporary raw files, then muxes them into an MP4 via FFmpeg on stop.
///
/// Usage:
///   1. Call start(output_path) to begin recording.
///   2. Each frame, call capture_frame(framebuffer, w, h).
///   3. Call capture_audio(samples, count) whenever audio samples are generated.
///   4. Call stop() to finalize — FFmpeg encodes the MP4.
///
/// The recorder writes raw data to temp files during capture (zero CPU
/// overhead from encoding) and invokes FFmpeg only once at the end.
class VideoRecorder {
public:
    VideoRecorder();
    ~VideoRecorder();

    // Non-copyable.
    VideoRecorder(const VideoRecorder&) = delete;
    VideoRecorder& operator=(const VideoRecorder&) = delete;

    /// Check if FFmpeg is available on the system.
    static bool ffmpeg_available();

    /// Start recording to `output_path` (e.g. "/tmp/recording.mp4").
    /// Returns true on success.
    bool start(const std::string& output_path);

    /// Stop recording and encode the final MP4.
    /// Returns true if FFmpeg encoding succeeded.
    bool stop();

    /// Capture one video frame.  `framebuffer` is ARGB8888 (0xAARRGGBB),
    /// dimensions `width` x `height`.  Converts to RGB24 for FFmpeg.
    void capture_frame(const uint32_t* framebuffer, int width, int height);

    /// Capture audio samples.  `samples` is interleaved stereo s16le,
    /// `count` is the number of stereo sample pairs.
    void capture_audio(const int16_t* samples, int count);

    /// Is recording currently active?
    bool is_recording() const { return recording_; }

    /// Get the output file path.
    const std::string& output_path() const { return output_path_; }

    /// Frame rate for encoding (default 50 Hz for PAL).
    static constexpr int FRAME_RATE = 50;

    /// Audio sample rate (must match Mixer::SAMPLE_RATE).
    static constexpr int SAMPLE_RATE = 44100;

private:
    bool recording_ = false;
    std::string output_path_;
    std::string video_tmp_;
    std::string audio_tmp_;
    FILE* video_file_ = nullptr;
    FILE* audio_file_ = nullptr;
    int frame_width_ = 0;
    int frame_height_ = 0;

    /// Reusable buffer for ARGB->RGB24 conversion.
    std::vector<uint8_t> rgb_buffer_;
};
