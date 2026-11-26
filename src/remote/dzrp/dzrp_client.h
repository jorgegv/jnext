#pragma once

// ---------------------------------------------------------------------------
// DzrpClient: the DZRP CLIENT (GH #511, epic #486; R0.3 of
// doc/design/DEBUGGER-REMOTE-REAL-NEXT-PLAN.md).
//
// jnext is also a debugger for a remote DZRP target (another jnext, or the
// dezogif_ng stub on a real Next). This is the protocol half of that: framing
// (dzrp_frame.h), the session (CMD_INIT / CMD_GET_SUPPORTED_COMMANDS / CMD_CLOSE),
// the profile of the remote it found, sequence numbers and timeouts, notifications,
// and the SEND GATE. It is Qt-free, has no `Debugger` dependency and never touches
// a socket: bytes move over a `ClientLink` (client_link.h).
//
// ── THE SEND GATE ───────────────────────────────────────────────────────────
// A dezogif_ng stub RESETS its session on a command it does not serve, and a
// running program must not be sent anything but CMD_PAUSE. So every command goes
// through ONE gate (`gate()`), and the frame writer (`send_more()`) runs only
// for what the gate (or the handshake) queued. A refusal writes ZERO bytes and
// logs at error: it is a client bug. The only way round it is the test probe's
// `detach_link()`, which hands the link over; the client has no bypass.
//
// ── NEVER A BLOCKING WAIT ON THE EMULATION THREAD, EXCEPT `request` ─────────
// `start()` / `step()` / `submit()` / `poll()` never block. `request()` and
// `request_all()` wait in a loop that polls the link and calls the WAIT HOOK each
// turn: a client whose remote is served by the SAME thread (a jnext dialling a jnext
// in this process) installs a hook that pumps that server. A `request` made from
// inside the hook is refused (`Reentrant`).
//
// ── SESSION WITH ONESELF ────────────────────────────────────────────────────
// CMD_INIT's program name ends with this process's session token (dzrp_session.h);
// a jnext server refuses such a client, and this client recognises the refusal.
// ---------------------------------------------------------------------------

#include <bitset>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "remote/client_link.h"
#include "remote/dzrp/dzrp_frame.h"
#include "remote/dzrp/dzrp_protocol.h"

namespace jnext {
namespace remote {
namespace dzrp {

enum class Profile : std::uint8_t { None, Jnext, DezogifNg, DezogifSerial /*ambiguous*/, Dezogif22 };
enum class BreakpointMode : std::uint8_t { None, Normal /*40/41*/, Simple /*13/14*/ };
enum class SessionState : std::uint8_t {
    Idle, Connecting, Initialising, Probing /*CMD 24*/, Ready, Closed, Failed
};
enum class Status : std::uint8_t {
    Ok, Refused /*gate*/, NotReady, Reentrant, Timeout, LinkLost, ProtocolError, Aborted
};

const char* profile_name(Profile p);
const char* status_name(Status s);
/// `CMD_GET_REGISTERS`, or `CMD_<n>` for an id this table does not name.
std::string command_name(std::uint8_t id);
/// Printable ASCII, `?` for anything else, at most `max` characters.
std::string sanitise_text(const std::uint8_t* p, std::size_t n, std::size_t max);
/// `NTF_LOG`'s payload AFTER the notification id: a NUL-terminated format string, then
/// the data its `$s1 $u1 $h1 $s2 $u2 $h2` sequences consume (words little endian).
/// Never reads past `n`; a missing datum prints `<?>`; at most 1024 characters.
std::string format_ntf_log(const std::uint8_t* p, std::size_t n);

struct RemoteInfo {
    std::uint8_t              error = 0;
    std::uint8_t              version[3] = {0, 0, 0};
    std::uint8_t              machine = 0;
    std::string               name;   // sanitised, <= 256
    std::vector<std::uint8_t> raw;    // the CMD_INIT reply payload
};

/// The outcome of the identity rule.
struct ProfileChoice {
    bool                ok = false;
    Profile             profile = Profile::None;
    BreakpointMode      breakpoints = BreakpointMode::None;
    int                 window = 1;
    bool                probe = false;   // send CMD_GET_SUPPORTED_COMMANDS
    std::bitset<256>    set;             // fixed set when !probe
    std::string         why;
};
/// One table-driven function (D4, D14, D19): the remote's CMD_INIT reply and the link it
/// came over say which profile it is, or that it is not a remote this client speaks to.
ProfileChoice select_profile(const RemoteInfo& info, LinkKind kind);

struct PauseNotification {
    std::uint8_t  reason = 0;
    std::uint16_t addr   = 0;
    std::uint8_t  bank1  = 0;
    std::string   text;
};

struct Reply {
    Status                    status = Status::Aborted;
    std::uint8_t              seq    = 0;
    std::uint8_t              id     = 0;
    std::vector<std::uint8_t> payload;
    std::chrono::microseconds rtt{0};
};

struct ClientConfig {
    std::string                  session_token;   // empty = process_session_token()
    std::chrono::milliseconds    response_timeout{2000};
    int                          window = 0;      // 0 = profile default; clamped to 1..15
    std::function<std::chrono::steady_clock::time_point()> clock;  // empty = steady_clock::now
};

class DzrpClient {
public:
    using WaitHook = std::function<void()>;

    explicit DzrpClient(std::unique_ptr<ClientLink> link, ClientConfig cfg = {});
    /// Ready: writes CMD_CLOSE once (if the gate lets it), flushes one pass, closes the link.
    ~DzrpClient();
    DzrpClient(const DzrpClient&)            = delete;
    DzrpClient& operator=(const DzrpClient&) = delete;

    /// Open the link; -> Connecting. False = the link refused (`error()`).
    bool         start();
    /// ONE non-blocking step of connect / CMD_INIT / CMD_GET_SUPPORTED_COMMANDS.
    SessionState step();
    SessionState state() const { return state_; }
    const std::string& error() const { return error_; }
    /// The kind of the failure that put the session in `Failed` (`Ok` otherwise).
    Status       failure() const { return failure_; }

    const RemoteInfo&       remote() const { return remote_; }
    Profile                 profile() const { return profile_; }
    BreakpointMode          breakpoint_mode() const { return bp_mode_; }
    bool                    in_set(std::uint8_t id) const { return set_.test(id); }
    const std::bitset<256>& command_set() const { return set_; }
    int                     window() const { return window_; }
    /// The program under a dezogif stub is running (a CMD_CONTINUE was sent, no NTF_PAUSE yet).
    bool                    running() const { return running_; }
    const ClientLink&       link() const { return *link_; }
    /// Round trip of the handshake commands (CMD_INIT; CMD_GET_SUPPORTED_COMMANDS when sent).
    std::chrono::microseconds init_rtt() const { return init_rtt_; }
    std::chrono::microseconds probe_rtt() const { return probe_rtt_; }

    /// Queue a command through the gate. Ok sets `ticket`; anything else writes nothing.
    Status submit(std::uint8_t id, std::vector<std::uint8_t> payload, std::uint32_t& ticket);
    bool   done(std::uint32_t ticket) const;
    /// The result of a finished ticket (Aborted for an unknown or already taken one).
    Reply  take(std::uint32_t ticket);
    /// submit + wait (bounded by the per-command timeout), calling the wait hook each turn.
    Reply  request(std::uint8_t id, std::vector<std::uint8_t> payload = {});
    /// Submit all, wait for all; the replies are in the order given.
    std::vector<Reply> request_all(std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> burst);

    /// Send what the window allows, move bytes, route replies and notifications, check timeouts.
    void poll(int wait_ms = 0);
    std::optional<PauseNotification> take_pause();
    void set_wait_hook(WaitHook hook) { hook_ = std::move(hook); }

    /// CMD_CLOSE (a bounded request) if the gate lets it, then close the link.
    void close();
    /// ONLY for the test probe: hand the link over, Closed and without a CMD_CLOSE. Null while
    /// a command is in flight or queued.
    std::unique_ptr<ClientLink> detach_link();

private:
    enum class Internal : std::uint8_t { None, Init, Probe };
    struct Cmd {
        std::uint32_t             ticket = 0;
        std::uint8_t              id = 0;
        std::vector<std::uint8_t> payload;
        Internal                  internal = Internal::None;
        std::uint8_t              seq = 0;
        std::chrono::steady_clock::time_point sent{};
    };

    std::chrono::steady_clock::time_point now() const;
    Status gate(std::uint8_t id);
    std::uint32_t enqueue(std::uint8_t id, std::vector<std::uint8_t> payload, Internal in);
    void send_more();
    void service();                       // read, route
    void route(const Response& r);
    void on_notification(const Response& r);
    void on_reply(const Response& r);
    void on_init_reply(const std::vector<std::uint8_t>& p);
    void on_probe_reply(const std::vector<std::uint8_t>& p);
    void check_timeouts();
    void check_link();
    void finish(Cmd& c, Status s, std::vector<std::uint8_t> payload, std::uint8_t seq);
    void teardown(SessionState to, Status failure, Status oldest, const std::string& why);
    Reply wait_for(std::uint32_t ticket);

    std::unique_ptr<ClientLink> link_;
    ClientConfig                cfg_;
    std::string                 token_;
    ResponseParser              parser_;
    SessionState                state_ = SessionState::Idle;
    std::string                 error_;
    Status                      failure_ = Status::Ok;
    RemoteInfo                  remote_;
    Profile                     profile_ = Profile::None;
    BreakpointMode              bp_mode_ = BreakpointMode::None;
    std::bitset<256>            set_;
    int                         window_ = 1;
    bool                        running_ = false;
    std::uint8_t                next_seq_ = 1;
    std::uint32_t               next_ticket_ = 1;
    std::deque<Cmd>             pending_;
    std::deque<Cmd>             outstanding_;
    std::map<std::uint32_t, Reply> done_;
    std::deque<PauseNotification>  pauses_;
    WaitHook                    hook_;
    bool                        in_hook_ = false;
    std::chrono::microseconds   init_rtt_{0};
    std::chrono::microseconds   probe_rtt_{0};
};

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
