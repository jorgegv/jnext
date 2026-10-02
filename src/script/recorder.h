#pragma once

// ---------------------------------------------------------------------------
// jnext::script — the RECORDER: an interactive session written out as a
// replay script (GH #20 as re-scoped into GH #26 WP6; dsl-frontend.md §7;
// "WP6 as built", Appendix L).
//
// ONE RECORDER = ONE BACKEND CLIENT (`ClientKind::Script`, arming: its `Frame`
// subscription must be delivered). It observes, never drives:
//
//   * at every frame edge E_K it samples INS-16 `input_state()` and writes an
//     edge for every input that CHANGED since E_K-1 — a matrix bit, an
//     extended key, a joystick connector — stamped `on frame K-1`. The change
//     was made between E_K-1 and frame K (the host keys land between frames),
//     the guest first saw it in frame K, and a replay that applies it at
//     E_K-1 shows it from frame K too (§7.2 item 1). Level form only:
//     `press` / `release` / `joystick`, never `press … for`;
//   * a CAPTURE (host key 8 — Alt+8 — while recording, or `capture()` from a
//     menu) is taken at the NEXT frame edge E_K, where a replay's `on frame K`
//     rule runs: `.scr` (ULA screen memory) and a `compare_scr` line when only
//     the ULA layer is on, else a PNG through the backend's CAP-01 and a
//     `screenshot` line naming a `-replay.png` beside it (§7.4);
//   * `stop()` writes the script: header (machine, program, RTC, SD card,
//     joystick mode NR 0x05, the replay command line), the precondition
//     asserts, any warnings both as comments and as `log` lines, the edges and
//     captures, and `exit 0`.
//
// WHAT IT CANNOT RECORD EXACTLY, and says so in the script (WARNING lines):
// an input change in a frame the machine was paused in mid-frame (§7.2 item
// 4), input already held when the recording began, input in frame 0, and a
// frame tag that went backwards (a rewind). A COLD BOOT (a hard reset, or a
// menu load, which is one) restarts the recording: a replay starts at power-on
// too, so what was recorded before it cannot be replayed (the header's
// program is re-read).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "debug/debugger.h"

namespace jnext {
namespace script {

/// The session facts only the frontend knows — the recorder's header, and
/// the replay command line it prints.
struct RecordingInfo {
    std::string load_file;  ///< the program as loaded (`--load` / File > Load), "" = none
    std::string rtc;        ///< the `--rtc` value, "" = the live clock
    std::string sd_image;   ///< the mounted SD image, "" = none
    std::string sd_id;      ///< a one-line identity of it, "" = unknown
};

class Recorder {
public:
    explicit Recorder(dbg::Debugger& dbg);
    ~Recorder();

    Recorder(const Recorder&)            = delete;
    Recorder& operator=(const Recorder&) = delete;

    /// Start recording into `path` (a `.jds`; captures go beside it as
    /// `<base>-NNNN.scr` / `.png`). `info` is re-read through `refresh` at a
    /// cold boot. False, with `why`, if already recording or the backend
    /// refuses the client.
    bool start(const std::string& path, std::function<RecordingInfo()> refresh, std::string& why);

    /// Ask for a capture at the next frame edge. False when not recording.
    bool capture();

    /// Stop and write the script. False, with `why`, when not recording or
    /// the file cannot be written (the recording is still stopped).
    bool stop(std::string& why);

    bool recording() const { return cid_ != dbg::CLIENT_NONE; }
    /// The recorder's backend client while recording (its log lines carry it).
    dbg::ClientId client() const { return cid_; }
    const std::string& path() const { return path_; }
    /// Captures taken so far in this recording.
    unsigned captures() const { return captures_; }
    /// Input edges recorded so far.
    std::size_t edges() const { return edges_; }
    /// The script `stop()` would write now.
    std::string script() const;

private:
    struct Line {
        uint32_t    frame;
        std::string text;
    };
    struct Listen;

    void on_frame(uint32_t frame);
    void on_paused();
    void on_cold_boot();
    void take_capture(uint32_t frame);
    void add(uint32_t frame, std::string text);
    void warn(std::string text);
    void reset_timeline();

    dbg::Debugger&                 dbg_;
    dbg::ClientId                  cid_ = dbg::CLIENT_NONE;
    std::unique_ptr<Listen>        listen_;
    std::function<RecordingInfo()> refresh_;
    RecordingInfo                  info_;
    std::string                    path_;
    std::string                    dir_;   ///< with a trailing '/', or empty
    std::string                    base_;  ///< file name without `.jds`

    int32_t                  machine_ = 0;
    uint8_t                  nr05_    = 0;
    bool                     have_frame_ = false;
    uint32_t                 first_frame_ = 0;
    uint32_t                 last_frame_  = 0;
    dbg::InputState          last_{};
    std::set<uint32_t>       paused_mid_frame_;
    bool                     capture_pending_ = false;
    unsigned                 captures_ = 0;
    std::size_t              edges_ = 0;
    std::vector<std::string> pngs_;   ///< PNG captures, by number, for `stop()`'s flush
    std::vector<Line>        lines_;
    std::vector<std::string> warnings_;
};

}  // namespace script
}  // namespace jnext
