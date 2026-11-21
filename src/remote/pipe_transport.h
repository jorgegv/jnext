#pragma once

// ---------------------------------------------------------------------------
// The page channel: an `esp::EspListener` the HOST feeds (GH #545).
//
// The browser build cannot listen on a socket, but a page can hand bytes to the
// module and take bytes back. `PipeListener` is the second implementation of
// the seam `remote::Server` runs over (the first is the socket layer, the
// second `FakeListener` for the unit suites): everything between the listener
// and the protocol adapter is the production code. Only the byte source
// differs, and the "kernel" is the host calling `PipeChannel`.
//
// Plain C++: no Emscripten include, built in every configuration, so the native
// and the node suites both run it.
//
// ── THE HOST'S END: `PipeChannel` ───────────────────────────────────────────
//
//   host_open()    a client arrived: a transport is parked for `accept()`.
//   host_send()    bytes from the client, queued for the session.
//   host_close()   the client left (a FIN): what it sent is still delivered.
//   take_output()  everything jnext wrote on the session since the last take.
//   take_ended()   true once after jnext ended the session, and only after
//                  `take_output()` has returned what was written before.
//
// The `PipeChannel` is shared with its listener, so it survives
// `Server::stop()` (which destroys the listener): the host can still collect a
// stopped session's last bytes and its end.
//
// ONE SESSION AT A TIME, each with a state of its own: bytes of one session can
// never reach another, even when a close and a redial come in one host turn.
// EVERYTHING IS BOUNDED: the inbound side holds at most `kInboundCap` bytes
// (a `host_send` that would pass it is refused whole), and the outbound side
// takes everything `Server` offers up to `kOutboundWindow`.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "esp01/esp_socket.h"

namespace jnext {
namespace remote {

/// The result of a host call. The numeric values are the page exports' result
/// codes (0 ok, 1 no channel, 2 wrong state, 3 input full).
enum class PipeResult : int {
    Ok         = 0,
    NoChannel  = 1,  ///< the channel's listener is not open
    WrongState = 2,  ///< e.g. open while a session is open, send with no session
    Full       = 3,  ///< the inbound cap
};

class PipeChannel {
public:
    static constexpr std::size_t kInboundCap     = std::size_t{64} << 20;
    static constexpr std::size_t kOutboundWindow = std::size_t{64} << 20;

    /// `inbound_cap`: the most bytes the host may have queued for jnext (a row
    /// lowers it to prove the refusal without a 64 MiB buffer).
    explicit PipeChannel(std::size_t inbound_cap = kInboundCap);
    ~PipeChannel();
    PipeChannel(const PipeChannel&)            = delete;
    PipeChannel& operator=(const PipeChannel&) = delete;

    /// The listener the `Server` opens: it shares this channel's state.
    std::unique_ptr<esp::EspListener> make_listener();

    /// A client arrived. Refused (WrongState) while a session is open or has
    /// ended without the host having been told (`take_ended`); NoChannel while
    /// no listener is open.
    PipeResult host_open();

    /// Client bytes, appended to THIS session. Refused whole (Full) when they
    /// would pass `kInboundCap`; WrongState with no session. Accepted and
    /// dropped once jnext ended the session and the host has not been told yet.
    PipeResult host_send(const std::uint8_t* data, std::size_t len);

    /// The client left. What it sent is still offered to the adapter; this
    /// session's undelivered output is discarded; a new `host_open` may follow
    /// at once. WrongState with no session.
    PipeResult host_close();

    /// Everything jnext wrote on the current session since the last take.
    std::vector<std::uint8_t> take_output();

    /// True exactly once after jnext closed the session, once `take_output()`
    /// has returned the bytes written before the close.
    bool take_ended();

    struct State;

private:
    std::shared_ptr<State> st_;
};

}  // namespace remote
}  // namespace jnext
