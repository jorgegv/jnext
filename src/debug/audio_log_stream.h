// GH #531 -- `--audio-log FILE`: stream the audio write log to a CSV file.
//
// An OBSERVER client of the Debugger (it arms nothing, so it costs the machine
// no per-instruction work) that requests the log, and a `Service`, so `pump()`
// calls `flush_notifications()` once per loop tick, which is where the ring is
// drained into the file. Every frontend and `--headless` pump, so one object in
// `main.cpp` serves all three. PUBLISHED: it includes `debug/debugger.h` only.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "debug/debugger.h"

namespace jnext {
namespace dbg {

class AudioLogStream final : public Service {
public:
    AudioLogStream() = default;
    ~AudioLogStream() override;
    AudioLogStream(const AudioLogStream&) = delete;
    AudioLogStream& operator=(const AudioLogStream&) = delete;

    /// Create FILE and write the header. False (with `why`) on a write failure; the
    /// stream has attached nothing yet, so a refused FILE leaves the machine alone.
    bool open(const std::string& path, std::string& why);

    /// Attach as an observer, ask for the log and register as a service.
    bool attach(Debugger& dbg);

    /// A final drain, then leave: remove the service, detach (the log turns off when
    /// nobody else asks) and close the file. False on an I/O error or when writes
    /// were overwritten before they were drained; `why` says which.
    bool finish(std::string& why);

    ServiceStep service_once(int) override { return ServiceStep::Idle; }
    void flush_notifications() override { drain(); }
    bool peer_connected() const override { return false; }

private:
    void drain();

    std::FILE* file_ = nullptr;
    std::string path_;
    Debugger* dbg_ = nullptr;
    ClientId cid_ = CLIENT_NONE;
    uint64_t last_seq_ = 0;
    uint64_t lost_ = 0;
    bool seen_ = false;
    bool io_error_ = false;
    bool finished_ = false;
};

}  // namespace dbg
}  // namespace jnext
