// ---------------------------------------------------------------------------
// jnext::dbg::result_name — the one stable spelling of a `Result`.
//
// §4 preamble of doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md, work package B1.
//
// Every name is lowercase with underscores, and every name is what a protocol
// error string, a log line and a test failure message all print. The switch has
// NO default arm: a `Result` added to the closed set without a name here is a
// compiler warning (-Wswitch), which is the only mechanism that catches it.
// ---------------------------------------------------------------------------

#include "debug/result.h"

namespace jnext {
namespace dbg {

const char* result_name(Result r) {
    switch (r) {
        case Result::Ok:                 return "ok";
        case Result::RefusedRunning:     return "refused_running";
        case Result::RefusedPaused:      return "refused_paused";
        case Result::RefusedCorrupt:     return "refused_corrupt";
        case Result::RefusedRzx:         return "refused_rzx";
        case Result::RefusedUnavailable: return "refused_unavailable";
        case Result::RefusedReadOnly:    return "refused_read_only";
        case Result::InvalidPage:        return "invalid_page";
        case Result::NotAtFrameBoundary: return "not_at_frame_boundary";
        case Result::NoFrame:            return "no_frame";
        case Result::Unsupported:        return "unsupported";
    }
    // Unreachable for any value of the closed set; a cast-in integer lands
    // here rather than off the end of the function (which is UB).
    return "unknown";
}

}  // namespace dbg
}  // namespace jnext
