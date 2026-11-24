#include "debug/psg_recorder.h"

#include <algorithm>
#include <vector>

#include "core/output_files.h"

namespace jnext {
namespace dbg {

std::string PsgRecorder::chip_path(const std::string& path, int chip)
{
    if (chip <= 0) return path;
    // The last path component starts after the last '/', and after a '\' on Windows only.
    size_t base = path.rfind('/');
#ifdef _WIN32
    const size_t bs = path.rfind('\\');
    if (bs != std::string::npos && (base == std::string::npos || bs > base)) base = bs;
#endif
    const size_t comp = (base == std::string::npos) ? 0 : base + 1;
    // The extension starts at the component's last '.', unless that is its first character.
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || dot <= comp) dot = path.size();
    return path.substr(0, dot) + "-ay" + std::to_string(chip) + path.substr(dot);
}

PsgRecorder::~PsgRecorder()
{
    // Safety net for a Debugger destroyed mid-recording: close up to the last frame seen,
    // touching no Emulator.
    std::string why;
    if (recording_) finish(seen_, why);
}

bool PsgRecorder::start(const std::string& path, uint32_t start_frame, std::string& why)
{
    if (recording_) { why = "already recording"; return false; }
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { why = "cannot create '" + path + "'"; return false; }
    path_ = path;
    start_frame_ = start_frame;
    file_.fill(nullptr);
    file_[0] = f;
    writer_[0].begin(start_frame);
    acc_ = AudioFrameAccumulator();
    off_ = 0;
    seen_ = static_cast<int64_t>(start_frame) - 1;
    last_.reset();
    io_error_ = false;
    chip_failed_.fill(false);
    recording_ = true;
    output_files::begin(path_);   // a page lists the file, incomplete, until stop()
    write_out(0);
    return true;
}

bool PsgRecorder::write_out(int chip)
{
    const std::vector<uint8_t> b = writer_[chip].take();
    if (b.empty() || !file_[chip]) return true;
    if (std::fwrite(b.data(), 1, b.size(), file_[chip]) != b.size()) io_error_ = true;
    return !io_error_;
}

void PsgRecorder::emit(const AudioFrame& f)
{
    const uint32_t file_frame = static_cast<uint32_t>(std::max<int64_t>(static_cast<int64_t>(f.frame) + off_, 0));
    for (int c = 0; c < static_cast<int>(AY_CHIP_COUNT); ++c) {
        const uint16_t mask = f.written_mask[c] & 0x3FFF;
        if (mask == 0) continue;
        if (chip_failed_[c]) continue;   // one open attempt only: this chip's blocks are dropped
        if (!file_[c]) {
            // AY#1 and AY#2 begin at this chip's first block, yet their writer starts at the
            // recording's start frame, so the files stay frame-aligned.
            file_[c] = std::fopen(chip_path(path_, c).c_str(), "wb");
            if (!file_[c]) { io_error_ = true; chip_failed_[c] = true; continue; }
            writer_[c].begin(start_frame_);
            output_files::begin(chip_path(path_, c));
        }
        std::array<uint8_t, 14> v{};
        for (size_t r = 0; r < v.size(); ++r)
            if (f.final_value[c][r] >= 0) v[r] = static_cast<uint8_t>(f.final_value[c][r]);
        writer_[c].frame(file_frame, mask, v);
        write_out(c);
    }
}

void PsgRecorder::feed(const AudioLogEntry& e)
{
    if (!recording_) return;
    // 1. Group. A frame this entry completes belongs to the mapping in force before it.
    if (auto done = acc_.feed(e)) emit(*done);

    // 2. A discontinuity: a marker, or a frame number below the last one seen. The file's
    // frames never go back and never repeat; a state load always continues right after the
    // last frame seen, because a loaded frame number says nothing about time that played.
    const bool marker = e.marker != AudioMarker::None;
    if (marker || (last_ && e.frame < *last_)) {
        const int64_t join = seen_ + 1 - static_cast<int64_t>(e.frame);
        off_ = (e.marker == AudioMarker::StateLoad) ? join : std::max(off_, join);
    }
    last_ = e.frame;
    // 3. Every non-marker entry (DAC, beeper and select entries too) moves time on.
    if (!marker) seen_ = std::max(seen_, static_cast<int64_t>(e.frame) + off_);
}

void PsgRecorder::note_time(uint32_t frame)
{
    if (recording_) seen_ = std::max(seen_, static_cast<int64_t>(frame) + off_);
}

bool PsgRecorder::finish(int64_t stop_file_frame, std::string& why)
{
    if (auto done = acc_.flush()) emit(*done);
    const uint32_t end_frame = static_cast<uint32_t>(std::max<int64_t>(stop_file_frame, 0));
    bool ok = true;
    for (int c = 0; c < static_cast<int>(AY_CHIP_COUNT); ++c) {
        if (!file_[c]) continue;
        writer_[c].end(end_frame);
        write_out(c);
        if (std::fclose(file_[c]) != 0) io_error_ = true;
        file_[c] = nullptr;
        output_files::end(chip_path(path_, c));   // closed, whatever the verdict
    }
    recording_ = false;
    ok = !io_error_;
    if (!ok) why = "write error on '" + path_ + "' or its -ay1/-ay2 files";
    return ok;
}

bool PsgRecorder::stop(uint32_t stop_frame, std::string& why)
{
    if (!recording_) { why = "not recording"; return false; }
    return finish(static_cast<int64_t>(stop_frame) + off_, why);
}

}  // namespace dbg
}  // namespace jnext
