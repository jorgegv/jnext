// GH #531 -- the seam between the core's audio write tap and the debugger
// backend. INTERNAL: not in the published header set. The core
// (`Emulator`) holds one `AudioTapSink*`; `Debugger::Impl` implements it.
// `src/audio` holds the chips and has no debug dependency, so this header
// lives beside `debug_state.h`, the other core-to-backend seam.
#pragma once

#include "debug/events.h"

namespace jnext {
namespace dbg {

class AudioTapSink {
public:
    virtual ~AudioTapSink() = default;
    /// One decoded write, or a Reset/StateLoad marker. `e.seq` is the sink's to assign.
    virtual void audio_record(const AudioLogEntry& e) = 0;
    /// A rewind landed at `e.cycle` (`e.marker == Rewind`): entries newer than
    /// it are no longer this machine's timeline.
    virtual void audio_rewind_landing(const AudioLogEntry& e) = 0;
};

}  // namespace dbg
}  // namespace jnext
