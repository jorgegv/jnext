// GH #538 -- the .psg recorder: the tap's third consumer (beside the audio log ring and
// the AudioWrite latch). INTERNAL: not in the published header set; `Debugger::Impl`
// owns one while a recording runs, and `Debugger::psg_record_start/stop` are its verbs.
//
// It is fed every tapped entry (markers included) straight from the sink, not from the
// log ring, so nothing is lost to the ring's size or to a user's Log Clear, and it works
// with the log off. The grouping is `AudioFrameAccumulator`'s; the bytes are
// `audio::PsgWriter`'s. This class owns the files and the frame mapping.
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include "audio/psg_writer.h"
#include "debug/audio_frames.h"

namespace jnext {
namespace dbg {

class PsgRecorder {
public:
    PsgRecorder() = default;
    ~PsgRecorder();
    PsgRecorder(const PsgRecorder&) = delete;
    PsgRecorder& operator=(const PsgRecorder&) = delete;

    /// FILE for AY#0; for chips 1 and 2 the name with "-ay1" / "-ay2" before the extension.
    static std::string chip_path(const std::string& path, int chip);

    /// Create FILE and write its header. False, with `why` naming the path, when it
    /// cannot be created. `start_frame` is the stamper frame current at the start.
    bool start(const std::string& path, uint32_t start_frame, std::string& why);

    /// One tapped entry, markers included, oldest first.
    void feed(const AudioLogEntry& e);

    /// The machine is about to be rebuilt (a hard reset): `frame` is the stamper frame current at that
    /// moment. The new machine's frame 0 then maps after it, so the silent frames that played before the
    /// reset stay in the file, as Fuse keeps counting frames through a reset.
    void note_time(uint32_t frame);

    /// Complete the open frame, write every created file's trailing separators and close.
    /// False on any I/O error; the files are closed either way. `stop_frame` is the
    /// stamper frame current at the stop.
    bool stop(uint32_t stop_frame, std::string& why);

    bool recording() const { return recording_; }

private:
    void emit(const AudioFrame& f);
    bool write_out(int chip);
    bool finish(int64_t stop_file_frame, std::string& why);

    bool recording_ = false;
    bool io_error_ = false;
    std::array<bool, AY_CHIP_COUNT> chip_failed_{};   ///< a chip file that could not be created: tried once, its blocks dropped
    std::string path_;
    uint32_t start_frame_ = 0;
    std::array<std::FILE*, AY_CHIP_COUNT> file_{};
    std::array<audio::PsgWriter, AY_CHIP_COUNT> writer_;
    AudioFrameAccumulator acc_;
    int64_t off_ = 0;                  ///< file frame = stamper frame + off_
    int64_t seen_ = 0;                 ///< highest file frame of any non-marker entry
    std::optional<uint32_t> last_;     ///< stamper frame of the last entry since the last discontinuity
};

}  // namespace dbg
}  // namespace jnext
