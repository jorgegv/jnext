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

    /// Command-line dialect the ffmpeg invocation is built for (GH #56).
    ///
    /// Posix   — a `/bin/sh` command line handed to system(): arguments are
    ///           single-quoted, and the child's output is discarded with the
    ///           shell redirection `>/dev/null 2>&1`.
    /// Windows — a CreateProcess() command line: arguments follow the MSVCRT
    ///           argv quoting rules (`"` quoting, backslash doubling), and
    ///           there is NO redirection suffix — there is no shell to
    ///           interpret one. win_run_hidden() points the child's stdio at
    ///           `NUL` through STARTUPINFO instead.
    ///
    /// Both dialects are built on every platform so both can be unit-tested
    /// from either. Only NATIVE_STYLE is ever executed.
    enum class CmdStyle { Posix, Windows };

    /// The dialect this build actually runs.
    static constexpr CmdStyle NATIVE_STYLE =
#ifdef _WIN32
        CmdStyle::Windows;
#else
        CmdStyle::Posix;
#endif

    /// Quote one command-line argument for `style`, so a path containing
    /// spaces (`C:\Users\Some Name\...`), quotes or shell metacharacters
    /// survives intact.
    static std::string quote_arg(const std::string& arg, CmdStyle style);

    /// Everything build_encode_command() needs. An empty `audio_input` means
    /// video-only (see the --silent note in stop()).
    struct EncodeSpec {
        int         width  = 0;
        int         height = 0;
        std::string video_input;
        std::string audio_input;
        std::string output;
        std::string codec;
        std::string codec_opts;
    };

    /// The ffmpeg command line stop() runs to mux the recording.
    static std::string build_encode_command(const EncodeSpec& spec, CmdStyle style);

    /// The ffmpeg command line ffmpeg_available() runs to probe for ffmpeg.
    static std::string build_probe_command(CmdStyle style);

    /// Start recording to `output_path` (e.g. "/tmp/recording.mp4").
    /// Returns true on success.
    bool start(const std::string& output_path);

    /// Stop recording and encode the final MP4.
    /// Returns true if FFmpeg encoding succeeded.
    bool stop();

    /// Capture one video frame.  `framebuffer` is ARGB8888 (0xAARRGGBB),
    /// dimensions `width × height` describe the IN-MEMORY framebuffer
    /// (canonical 640×256 post-G104). Each input row is emitted twice to
    /// the raw RGB24 stream, so FFmpeg sees `width × (height * 2)` frames
    /// (640×512 — square-pixel CRT-faithful geometry, G104 Phase 7).
    void capture_frame(const uint32_t* framebuffer, int width, int height);

    /// Capture audio samples.  `samples` is interleaved stereo s16le,
    /// `count` is the number of stereo sample pairs.
    void capture_audio(const int16_t* samples, int count);

    /// Is recording currently active?
    bool is_recording() const { return recording_; }

    /// Has any stop() of an active recording failed in this recorder's
    /// lifetime? (GH #86) This is the seam that lets a stop() whose return
    /// value was discarded — MainWindow::closeEvent() stopping a CLI-started
    /// recording when the user closes the window — still fail the process
    /// exit status: main.cpp reads it after run(). Deliberately STICKY: a
    /// later successful recording does not clear it, because the recording
    /// that failed still never materialized.
    bool stop_failed() const { return stop_failed_; }

    /// Get the output file path.
    const std::string& output_path() const { return output_path_; }

    /// Frame rate for encoding (default 50 Hz for PAL).
    static constexpr int FRAME_RATE = 50;

    /// Audio sample rate (must match Mixer::SAMPLE_RATE).
    static constexpr int SAMPLE_RATE = 44100;

private:
    bool recording_ = false;
    bool stop_failed_ = false;  ///< GH #86 — see stop_failed().
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
