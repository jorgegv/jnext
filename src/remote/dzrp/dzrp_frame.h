#pragma once

// ---------------------------------------------------------------------------
// DZRP framing (GH #12, epic #276 package D, WP-1).
//
// The DeZog Remote Protocol's byte-level frames, and nothing else: no command
// is interpreted here. `DzrpServer` (dzrp_server.h) feeds this parser from its
// `remote::Connection` and writes what `encode_response()` builds.
//
// Design: doc/design/debug-subsystem/dzrp-frontend.md §4.2 item 5. Spec:
// DeZog `design/DeZogProtocol.md` (DZRP 2.2.0), "Data Format".
//
// ── THE TWO LENGTH CONVENTIONS ─────────────────────────────────────────────
//
//   command   length(4, LE) seq(1) id(1) payload(length)
//             — the length counts the PAYLOAD ONLY, not seq, not id.
//   response  length(4, LE) seq(1) payload(length - 1)
//             — the length counts FROM THE SEQ BYTE. A notification is a
//             response with seq 0 whose first payload byte is its id.
//
// Assuming the two are symmetric costs a silent hang, not an error: the peer
// waits for bytes that never come (dezogif_ng `test/dzrp/dzrp.py`, header).
//
// ── THE RULES THIS PARSER ENFORCES ─────────────────────────────────────────
//
//   * A payload longer than `MAX_PAYLOAD_BYTES` (16 MiB) is refused as soon as
//     the length field is complete — nothing of it is read.
//   * Sequence number 0 is refused: it is reserved for notifications, so a
//     command carrying it cannot be answered.
//   * Any other sequence number is accepted — 1..15 (DZRP 2.2.0) and 1..255
//     (2.0/2.1 clients) alike — and the adapter echoes it VERBATIM.
//   * A frame that makes no progress for `CHUNK_TIMEOUT` (5 s, DeZog's own
//     timeout between data chunks) is a truncated stream.
//   * It never reads past the end of the current frame: `wanted()` says how
//     many bytes it still needs, so the bytes of the NEXT command stay in the
//     transport's buffer — where its input bound backpressures a peer that
//     pipelines faster than the adapter executes.
//
// Each refusal is a PROTOCOL ERROR: the adapter logs it and closes the
// connection, as the CSpect plugin does ("Shutdown on protocol errors").
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jnext {
namespace remote {
namespace dzrp {

/// Bytes of a command frame before its payload: length(4) + seq(1) + id(1).
constexpr std::size_t COMMAND_HEADER_BYTES = 6;

/// The largest command payload accepted (design §4.2.5): 16 MiB. A
/// `CMD_WRITE_STATE` from a foreign client may be large; nothing a released
/// DeZog sends comes near it.
constexpr std::uint32_t MAX_PAYLOAD_BYTES = std::uint32_t{16} << 20;

/// How long a partial frame may go without a byte before it is a truncated
/// stream: DeZog's own socket timeout (`settings.ts:606`, 5 s). Measured from
/// the LAST byte that advanced the frame, as DeZog's own chunk timeout is, so a
/// slow link that keeps delivering is never cut off.
constexpr std::chrono::milliseconds CHUNK_TIMEOUT{5000};

/// One complete command.
struct Command {
    std::uint8_t              seq = 0;
    std::uint8_t              id  = 0;
    std::vector<std::uint8_t> payload;
};

/// The incremental command parser. Feed it at most `wanted()` bytes at a time;
/// it holds one frame, never more.
class FrameParser {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    /// Bytes the current frame still needs: the rest of the header, then the
    /// rest of the payload. 0 once a command is ready, or after an error.
    std::size_t wanted() const;

    /// Take up to `wanted()` of `n` bytes. Returns how many were taken. `now`
    /// stamps the progress the chunk timeout is measured from.
    std::size_t feed(const std::uint8_t* data, std::size_t n, TimePoint now);

    /// A complete command is waiting in `take()`.
    bool ready() const { return ready_; }

    /// Hand the ready command over and start the next frame.
    Command take();

    /// The stream broke a framing rule; `error()` says which. Sticky until
    /// `reset()`.
    bool               failed() const { return !error_.empty(); }
    const std::string& error() const { return error_; }

    /// Part of a frame has arrived and the rest has not.
    bool partial() const { return !ready_ && !failed() && (have_ > 0); }

    /// A partial frame has made no progress for `CHUNK_TIMEOUT`.
    bool stalled(TimePoint now) const;

    /// Bytes of the current frame received so far (header included).
    std::size_t received() const { return have_; }

    /// Forget everything: a new connection starts here.
    void reset();

private:
    std::uint8_t  header_[COMMAND_HEADER_BYTES] = {};
    std::size_t   have_   = 0;  // bytes of this frame received, header included
    std::uint32_t length_ = 0;  // payload length, once the header has it
    bool          ready_  = false;
    std::string   error_;
    Command       cmd_;
    TimePoint     last_progress_{};
};

/// A response frame: length = 1 + `n` (it counts from the seq byte), then `seq`,
/// then the payload. With `seq` 0 and the notification id as the first payload
/// byte it is a notification.
std::vector<std::uint8_t> encode_response(std::uint8_t seq, const std::uint8_t* payload,
                                          std::size_t n);
std::vector<std::uint8_t> encode_response(std::uint8_t                     seq,
                                          const std::vector<std::uint8_t>& payload);

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
