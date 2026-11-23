#include "debug/audio_log_stream.h"

#include <cerrno>
#include <cstring>

#include "core/output_files.h"
#include "debug/audio_log_csv.h"

namespace jnext {
namespace dbg {

AudioLogStream::~AudioLogStream()
{
    std::string why;
    if (!finished_) finish(why);
}

bool AudioLogStream::open(const std::string& path, std::string& why)
{
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) {
        why = "cannot write '" + path + "': " + std::strerror(errno);
        return false;
    }
    path_ = path;
    output_files::begin(path_);   // GH #553: a page lists the file, incomplete, until finish()
    const std::string h = audio_log_csv_header();
    if (std::fwrite(h.data(), 1, h.size(), file_) != h.size()) {
        why = "cannot write '" + path + "': " + std::strerror(errno);
        std::fclose(file_);
        file_ = nullptr;
        output_files::end(path_);   // what is on disk is final: never left listed as incomplete
        return false;
    }
    return true;
}

bool AudioLogStream::attach(Debugger& dbg)
{
    if (!file_ || dbg_) return false;
    ClientInfo info;
    info.name = "--audio-log";
    info.kind = ClientKind::Script;     // the CLI automation kind, like the #20 recorder
    info.observer = true;               // arms nothing
    auto r = dbg.attach(info);
    if (!r) return false;
    cid_ = r.value;
    dbg_ = &dbg;
    dbg.set_audio_log_enabled(cid_, true);
    dbg.add_service(*this);
    return true;
}

void AudioLogStream::drain()
{
    if (!dbg_ || !file_) return;
    auto r = dbg_->audio_log_entries(last_seq_);
    if (!r || r.value.empty()) return;
    std::string out;
    for (const AudioLogEntry& e : r.value) {
        // A seq gap is either a rewind landing that removed entries never drained (a
        // truncation: the landing's marker is the first entry after it) or entries the
        // ring overwrote before this drain (lost).
        if (seen_ && e.seq != last_seq_ + 1 && e.marker != AudioMarker::Rewind)
            lost_ += e.seq - last_seq_ - 1;
        seen_ = true;
        last_seq_ = e.seq;
        audio_log_csv_append(out, e);
    }
    if (std::fwrite(out.data(), 1, out.size(), file_) != out.size()) io_error_ = true;
}

bool AudioLogStream::finish(std::string& why)
{
    if (finished_) return true;
    finished_ = true;
    bool ok = true;
    if (dbg_) {
        drain();
        dbg_->remove_service(*this);
        dbg_->detach(cid_);
        dbg_ = nullptr;
    }
    if (file_) {
        if (std::fclose(file_) != 0) io_error_ = true;
        file_ = nullptr;
        output_files::end(path_);   // flushed and closed, whatever the verdict below
    }
    if (io_error_) {
        why = "write error on '" + path_ + "'";
        ok = false;
    }
    if (lost_ > 0) {
        if (!why.empty()) why += "; ";
        why += std::to_string(lost_) + " audio writes were overwritten before they were written "
               "(the log holds only what arrives between two services of the loop)";
        ok = false;
    }
    return ok;
}

}  // namespace dbg
}  // namespace jnext
