#include "remote/dzrp/dzrp_client.h"

#include <algorithm>
#include <cstdio>

#include "core/log.h"
#include "remote/dzrp/dzrp_session.h"

namespace jnext {
namespace remote {
namespace dzrp {

namespace {

std::chrono::steady_clock::time_point real_now() { return std::chrono::steady_clock::now(); }

constexpr std::size_t NAME_DISPLAY_MAX = 256;
constexpr std::size_t LOG_DISPLAY_MAX  = 1024;

bool is_dezogif(Profile p) {
    return p == Profile::DezogifNg;
}

std::string version_text(const std::uint8_t v[3]) {
    return std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]);
}

// The bytes of the remote's name in a CMD_INIT reply: from index 5 to the first NUL or the end.
std::string raw_name(const std::vector<std::uint8_t>& raw) {
    std::string s;
    for (std::size_t i = 5; i < raw.size() && raw[i] != 0; ++i) s.push_back(static_cast<char>(raw[i]));
    return s;
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

}  // namespace

// ── Free helpers ────────────────────────────────────────────────────────────

const char* profile_name(Profile p) {
    switch (p) {
    case Profile::None:          return "none";
    case Profile::Jnext:         return "jnext";
    case Profile::DezogifNg:     return "dezogif_ng";
    }
    return "?";
}

const char* status_name(Status s) {
    switch (s) {
    case Status::Ok:            return "ok";
    case Status::Refused:       return "refused";
    case Status::NotReady:      return "not ready";
    case Status::Reentrant:     return "reentrant";
    case Status::Timeout:       return "timeout";
    case Status::LinkLost:      return "link lost";
    case Status::ProtocolError: return "protocol error";
    case Status::Aborted:       return "aborted";
    }
    return "?";
}

std::string command_name(std::uint8_t id) {
    switch (id) {
#define C(x) case x: return #x;
    C(CMD_INIT) C(CMD_CLOSE) C(CMD_GET_REGISTERS) C(CMD_SET_REGISTER) C(CMD_WRITE_BANK)
    C(CMD_CONTINUE) C(CMD_PAUSE) C(CMD_READ_MEM) C(CMD_WRITE_MEM) C(CMD_SET_SLOT)
    C(CMD_GET_TBBLUE_REG) C(CMD_SET_BORDER) C(CMD_SET_BREAKPOINTS) C(CMD_RESTORE_MEM)
    C(CMD_LOOPBACK) C(CMD_GET_SPRITES_PALETTE) C(CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL)
    C(CMD_GET_SPRITES) C(CMD_GET_SPRITE_PATTERNS) C(CMD_READ_PORT) C(CMD_WRITE_PORT)
    C(CMD_EXEC_ASM) C(CMD_INTERRUPT_ON_OFF) C(CMD_GET_SUPPORTED_COMMANDS)
    C(CMD_READ_BANK_MEM) C(CMD_WRITE_BANK_MEM) C(CMD_SET_NEXTREGS) C(CMD_READ_MEM_BLOCKS)
    C(CMD_ENABLE_BREAK_ON_INTERRUPT) C(CMD_ADD_BREAKPOINT) C(CMD_REMOVE_BREAKPOINT)
    C(CMD_ADD_WATCHPOINT) C(CMD_REMOVE_WATCHPOINT) C(CMD_READ_STATE) C(CMD_WRITE_STATE)
#undef C
    default: break;
    }
    return "CMD_" + std::to_string(id);
}

std::string sanitise_text(const std::uint8_t* p, std::size_t n, std::size_t max) {
    std::string s;
    for (std::size_t i = 0; i < n && s.size() < max; ++i)
        s.push_back(p[i] >= 0x20 && p[i] < 0x7F ? static_cast<char>(p[i]) : '?');
    return s;
}

std::string format_ntf_log(const std::uint8_t* p, std::size_t n) {
    std::size_t fe = 0;
    while (fe < n && p[fe] != 0) ++fe;               // the format string is [0, fe)
    std::size_t di = fe < n ? fe + 1 : n;            // the data follows the NUL
    std::string out;
    for (std::size_t i = 0; i < fe && out.size() < LOG_DISPLAY_MAX; ++i) {
        const char c = static_cast<char>(p[i]);
        const bool seq = c == '$' && i + 2 < fe &&
                         (p[i + 1] == 's' || p[i + 1] == 'u' || p[i + 1] == 'h') &&
                         (p[i + 2] == '1' || p[i + 2] == '2');
        if (!seq) {
            out.push_back(c >= 0x20 && c < 0x7F ? c : '?');
            continue;
        }
        const char        type = static_cast<char>(p[i + 1]);
        const std::size_t size = p[i + 2] == '2' ? 2 : 1;
        i += 2;
        if (di + size > n) {
            out += "<?>";
            di = n;
            continue;
        }
        unsigned v = p[di];
        if (size == 2) v |= static_cast<unsigned>(p[di + 1]) << 8;
        di += size;
        char buf[16];
        if (type == 's') {
            const int sv = size == 2 ? static_cast<int16_t>(v) : static_cast<int8_t>(v);
            std::snprintf(buf, sizeof(buf), "%d", sv);
        } else if (type == 'h') {
            std::snprintf(buf, sizeof(buf), size == 2 ? "%04X" : "%02X", v);
        } else {
            std::snprintf(buf, sizeof(buf), "%u", v);
        }
        out += buf;
    }
    if (out.size() > LOG_DISPLAY_MAX) out.resize(LOG_DISPLAY_MAX);
    return out;
}

ProfileChoice select_profile(const RemoteInfo& info, LinkKind kind) {
    ProfileChoice c;
    const std::string name = raw_name(info.raw);
    const std::uint8_t major = info.version[0], minor = info.version[1];
    auto fail = [&]() {
        c.ok  = false;
        c.why = "unsupported DZRP remote \"" + info.name + "\" (DZRP " + version_text(info.version) +
                ", machine " + std::to_string(info.machine) + ")";
        return c;
    };
    if (major != 2 || info.machine != DZRP_MACHINE_ZXNEXT) return fail();

    if (starts_with(name, "jnext v")) {
        if (minor < 2) return fail();
        c.ok = true;
        c.profile = Profile::Jnext;
        c.breakpoints = BreakpointMode::Normal;
        c.window = 15;
        c.probe = true;
        return c;
    }
    if (starts_with(name, "dezogif v")) {
        if (minor == 1 && kind == LinkKind::Tcp) {
            // dezogif_ng over WiFi: every id it dispatches, 40 and 41 included.
            c.ok          = true;
            c.profile     = Profile::DezogifNg;
            c.window      = 1;
            c.breakpoints = BreakpointMode::Simple;
            for (unsigned i = 1; i <= 23; ++i) c.set.set(i);
            c.set.set(CMD_ADD_BREAKPOINT);
            c.set.set(CMD_REMOVE_BREAKPOINT);
            return c;
        }
        fail();
        c.why += "; only the WiFi dezogif_ng stub (DZRP 2.1.0 over TCP) is supported";
        return c;
    }
    return fail();
}

// ── DzrpClient ──────────────────────────────────────────────────────────────

DzrpClient::DzrpClient(std::unique_ptr<ClientLink> link, ClientConfig cfg)
    : link_(std::move(link)), cfg_(std::move(cfg)) {
    token_ = cfg_.session_token.empty() ? process_session_token() : cfg_.session_token;
}

DzrpClient::~DzrpClient() {
    if (state_ == SessionState::Ready && link_) {
        // One CMD_CLOSE, only if the gate would let it through; never a wait.
        if (in_set(CMD_CLOSE) && !(is_dezogif(profile_) && running_)) {
            std::uint32_t t = 0;
            if (submit(CMD_CLOSE, {}, t) == Status::Ok) link_->poll(0);
        }
    }
    if (link_) link_->close();
}

std::chrono::steady_clock::time_point DzrpClient::now() const {
    return cfg_.clock ? cfg_.clock() : real_now();
}

bool DzrpClient::start() {
    if (state_ != SessionState::Idle || !link_) return false;
    if (!link_->open()) {
        error_   = link_->last_error();
        failure_ = Status::LinkLost;
        state_   = SessionState::Failed;
        return false;
    }
    state_ = SessionState::Connecting;
    return true;
}

SessionState DzrpClient::step() {
    switch (state_) {
    case SessionState::Connecting: {
        link_->poll(0);
        const LinkState ls = link_->state();
        if (ls == LinkState::Open) {
            std::vector<std::uint8_t> p = {DZRP_VERSION_MAJOR, DZRP_VERSION_MINOR, DZRP_VERSION_PATCH};
            const std::string nm = client_program_name(token_);
            p.insert(p.end(), nm.begin(), nm.end());
            p.push_back(0);
            state_ = SessionState::Initialising;
            enqueue(CMD_INIT, std::move(p), Internal::Init);
            send_more();
        } else if (ls == LinkState::Failed || ls == LinkState::Closed) {
            teardown(SessionState::Failed, Status::LinkLost, Status::LinkLost,
                     link_->last_error().empty() ? "the link closed while connecting"
                                                 : link_->last_error());
        }
        break;
    }
    case SessionState::Initialising:
    case SessionState::Probing:
        poll(0);
        break;
    default:
        break;
    }
    return state_;
}

// ── The gate and the writer ─────────────────────────────────────────────────

Status DzrpClient::gate(std::uint8_t id) {
    const char* why = nullptr;
    Status      st  = Status::Refused;
    if (state_ != SessionState::Ready) {
        why = "the session is not ready";
        st  = Status::NotReady;
    } else if (!in_set(id)) {
        why = "not in the profile's command set";
    } else if (is_dezogif(profile_) && running_ && id != CMD_PAUSE) {
        why = "the program is running";
    }
    if (!why) return Status::Ok;
    Log::debugger()->error("dzrp client: refused to send {} ({}) to {}: {} - a client bug",
                           command_name(id), id, profile_name(profile_), why);
    return st;
}

std::uint32_t DzrpClient::enqueue(std::uint8_t id, std::vector<std::uint8_t> payload, Internal in) {
    Cmd c;
    c.id       = id;
    c.payload  = std::move(payload);
    c.internal = in;
    c.ticket   = in == Internal::None ? next_ticket_++ : 0;
    pending_.push_back(std::move(c));
    return pending_.back().ticket;
}

// The ONLY place a command's bytes are written.
void DzrpClient::send_more() {
    while (!pending_.empty() && static_cast<int>(outstanding_.size()) < window_ &&
           (state_ == SessionState::Initialising || state_ == SessionState::Probing ||
            state_ == SessionState::Ready)) {
        Cmd& c = pending_.front();
        const std::vector<std::uint8_t> frame = encode_command(next_seq_, c.id, c.payload);
        if (frame.empty() || !link_->write(frame.data(), frame.size())) {
            teardown(SessionState::Failed, Status::LinkLost, Status::LinkLost,
                     link_->last_error().empty() ? "the write to the link failed"
                                                 : link_->last_error());
            return;
        }
        c.seq  = next_seq_;
        next_seq_ = next_seq_ == 15 ? 1 : static_cast<std::uint8_t>(next_seq_ + 1);
        c.sent = now();
        outstanding_.push_back(std::move(c));
        pending_.pop_front();
    }
}

Status DzrpClient::submit(std::uint8_t id, std::vector<std::uint8_t> payload, std::uint32_t& ticket) {
    ticket = 0;
    const Status g = gate(id);
    if (g != Status::Ok) return g;
    if (payload.size() > MAX_PAYLOAD_BYTES) {
        Log::debugger()->error("dzrp client: refused to send {} ({}): payload of {} bytes is over "
                               "the {}-byte cap - a client bug",
                               command_name(id), id, payload.size(), MAX_PAYLOAD_BYTES);
        return Status::Refused;
    }
    if (id == CMD_CONTINUE) running_ = true;
    ticket = enqueue(id, std::move(payload), Internal::None);
    send_more();
    return Status::Ok;
}

bool DzrpClient::done(std::uint32_t ticket) const { return done_.count(ticket) != 0; }

Reply DzrpClient::take(std::uint32_t ticket) {
    auto it = done_.find(ticket);
    if (it == done_.end()) return Reply{};
    Reply r = std::move(it->second);
    done_.erase(it);
    return r;
}

// ── Waiting ─────────────────────────────────────────────────────────────────

Reply DzrpClient::wait_for(std::uint32_t ticket) {
    while (!done(ticket)) {
        // A dead session resolves every ticket it had; an unknown one can never complete.
        if (state_ == SessionState::Failed || state_ == SessionState::Closed) {
            Reply r;
            r.status = Status::Aborted;
            return r;
        }
        poll(1);
        if (hook_) {
            struct HookScope {   // cleared even if the hook throws
                bool& f;
                explicit HookScope(bool& flag) : f(flag) { f = true; }
                ~HookScope() { f = false; }
            } scope(in_hook_);
            hook_();
        }
    }
    return take(ticket);
}

Reply DzrpClient::request(std::uint8_t id, std::vector<std::uint8_t> payload) {
    Reply r;
    r.id = id;
    if (in_hook_) {
        Log::debugger()->error("dzrp client: refused {} ({}): a request from inside the wait hook",
                               command_name(id), id);
        r.status = Status::Reentrant;
        return r;
    }
    std::uint32_t t = 0;
    const Status  s = submit(id, std::move(payload), t);
    if (s != Status::Ok) {
        r.status = s;
        return r;
    }
    return wait_for(t);
}

std::vector<Reply> DzrpClient::request_all(
    std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> burst) {
    std::vector<Reply> out(burst.size());
    if (in_hook_) {
        Log::debugger()->error("dzrp client: refused a burst of {}: a request from inside the "
                               "wait hook", burst.size());
        for (std::size_t i = 0; i < burst.size(); ++i) {
            out[i].id     = burst[i].first;
            out[i].status = Status::Reentrant;
        }
        return out;
    }
    std::vector<std::uint32_t> tickets(burst.size(), 0);
    for (std::size_t i = 0; i < burst.size(); ++i) {
        out[i].id     = burst[i].first;
        out[i].status = submit(burst[i].first, std::move(burst[i].second), tickets[i]);
    }
    for (std::size_t i = 0; i < burst.size(); ++i)
        if (tickets[i] != 0) out[i] = wait_for(tickets[i]);
    return out;
}

// ── Moving bytes ────────────────────────────────────────────────────────────

void DzrpClient::poll(int wait_ms) {
    if (state_ == SessionState::Idle || state_ == SessionState::Closed ||
        state_ == SessionState::Failed || state_ == SessionState::Connecting)
        return;
    send_more();
    link_->poll(wait_ms);
    service();
    send_more();
    check_timeouts();
    check_link();
}

void DzrpClient::service() {
    std::uint8_t buf[4096];
    auto live = [&] {
        return state_ == SessionState::Initialising || state_ == SessionState::Probing ||
               state_ == SessionState::Ready;
    };
    while (live()) {
        const std::size_t n = link_->read(buf, sizeof(buf));
        if (n == 0) break;
        std::size_t off = 0;
        while (off < n && live()) {
            const std::size_t taken = parser_.feed(buf + off, n - off, now());
            off += taken;
            if (parser_.failed()) {
                teardown(SessionState::Failed, Status::ProtocolError, Status::ProtocolError,
                         "protocol error: " + parser_.error());
                break;
            }
            if (parser_.ready()) route(parser_.take());
            else if (taken == 0) break;
        }
    }
    if (live() && parser_.stalled(now()))
        teardown(SessionState::Failed, Status::ProtocolError, Status::ProtocolError,
                 "protocol error: a reply stalled mid-frame for " +
                     std::to_string(CHUNK_TIMEOUT.count()) + " ms");
}

void DzrpClient::route(const Response& r) {
    if (r.notification()) on_notification(r);
    else on_reply(r);
}

void DzrpClient::on_notification(const Response& r) {
    const std::vector<std::uint8_t>& p = r.payload;   // p[0] is the notification id
    if (p[0] == NTF_PAUSE) {
        if (p.size() < 5) {
            teardown(SessionState::Failed, Status::ProtocolError, Status::ProtocolError,
                     "protocol error: NTF_PAUSE of " + std::to_string(p.size()) + " bytes");
            return;
        }
        PauseNotification n;
        n.reason = p[1];
        n.addr   = static_cast<std::uint16_t>(p[2] | (p[3] << 8));
        n.bank1  = p[4];
        std::size_t e = 5;
        while (e < p.size() && p[e] != 0) ++e;
        n.text = sanitise_text(p.data() + 5, e - 5, NAME_DISPLAY_MAX);
        pauses_.push_back(std::move(n));
        running_ = false;
    } else if (p[0] == NTF_LOG) {
        Log::debugger()->info("dzrp client: remote log: {}", format_ntf_log(p.data() + 1, p.size() - 1));
    } else {
        Log::debugger()->warn("dzrp client: ignoring notification {}", p[0]);
    }
}

void DzrpClient::on_reply(const Response& r) {
    if (outstanding_.empty()) {
        teardown(SessionState::Failed, Status::ProtocolError, Status::ProtocolError,
                 "protocol error: unsolicited reply (seq " + std::to_string(r.seq) + ")");
        return;
    }
    if (outstanding_.front().seq != r.seq) {
        teardown(SessionState::Failed, Status::ProtocolError, Status::ProtocolError,
                 "protocol error: reply seq " + std::to_string(r.seq) + ", expected " +
                     std::to_string(outstanding_.front().seq));
        return;
    }
    Cmd c = std::move(outstanding_.front());
    outstanding_.pop_front();
    const auto rtt = std::chrono::duration_cast<std::chrono::microseconds>(now() - c.sent);
    switch (c.internal) {
    case Internal::Init:  init_rtt_ = rtt; on_init_reply(r.payload); break;
    case Internal::Probe: probe_rtt_ = rtt; on_probe_reply(r.payload); break;
    case Internal::None: {
        Reply rep;
        rep.status  = Status::Ok;
        rep.seq     = c.seq;
        rep.id      = c.id;
        rep.payload = r.payload;
        rep.rtt     = rtt;
        done_[c.ticket] = std::move(rep);
        break;
    }
    }
}

void DzrpClient::on_init_reply(const std::vector<std::uint8_t>& p) {
    if (p.size() < 5) {
        teardown(SessionState::Failed, Status::ProtocolError, Status::ProtocolError,
                 "protocol error: CMD_INIT reply of " + std::to_string(p.size()) + " bytes");
        return;
    }
    remote_.raw     = p;
    remote_.error   = p[0];
    remote_.version[0] = p[1];
    remote_.version[1] = p[2];
    remote_.version[2] = p[3];
    remote_.machine = p[4];
    std::size_t e = 5;
    while (e < p.size() && p[e] != 0) ++e;
    remote_.name = sanitise_text(p.data() + 5, e - 5, NAME_DISPLAY_MAX);

    if (remote_.error != 0) {
        const bool own = name_carries_token(p.data() + 5, e - 5, token_);
        teardown(SessionState::Failed, Status::Refused, Status::Refused,
                 own ? "this is this jnext's own DZRP server"
                     : "remote refused CMD_INIT (error " + std::to_string(remote_.error) + ")");
        return;
    }
    const ProfileChoice c = select_profile(remote_, link_->kind());
    if (!c.ok) {
        teardown(SessionState::Failed, Status::Refused, Status::Refused, c.why);
        return;
    }
    profile_ = c.profile;
    bp_mode_ = c.breakpoints;
    window_  = cfg_.window == 0 ? c.window : std::clamp(cfg_.window, 1, 15);
    set_     = c.set;
    if (c.probe) {
        state_ = SessionState::Probing;
        enqueue(CMD_GET_SUPPORTED_COMMANDS, {}, Internal::Probe);
        send_more();
        return;
    }
    state_ = SessionState::Ready;
    Log::debugger()->info("dzrp client: connected to \"{}\" (DZRP {}) over {}: profile {}, window {}",
                          remote_.name, version_text(remote_.version), link_->describe(),
                          profile_name(profile_), window_);
}

void DzrpClient::on_probe_reply(const std::vector<std::uint8_t>& p) {
    if (p.empty() || p.size() > 32) {
        teardown(SessionState::Failed, Status::ProtocolError, Status::ProtocolError,
                 "protocol error: CMD_GET_SUPPORTED_COMMANDS reply of " +
                     std::to_string(p.size()) + " bytes (1..32 expected)");
        return;
    }
    set_.reset();
    for (std::size_t i = 0; i < p.size() * 8; ++i)
        if (p[i / 8] & (1u << (i % 8))) set_.set(i);
    set_.reset(0);   // bit 0 is not a command
    state_ = SessionState::Ready;
    Log::debugger()->info("dzrp client: connected to \"{}\" (DZRP {}) over {}: profile {}, window {}",
                          remote_.name, version_text(remote_.version), link_->describe(),
                          profile_name(profile_), window_);
}

void DzrpClient::check_timeouts() {
    if (outstanding_.empty()) return;
    if (state_ != SessionState::Initialising && state_ != SessionState::Probing &&
        state_ != SessionState::Ready)
        return;
    const Cmd& c = outstanding_.front();
    if (now() - c.sent < cfg_.response_timeout) return;
    std::string why = "no reply to " + command_name(c.id) + " (seq " + std::to_string(c.seq) +
                      ") within " + std::to_string(cfg_.response_timeout.count()) + " ms";
    if (is_dezogif(profile_))
        why += " - a dezogif_ng stub resets its session on a command it does not serve";
    teardown(SessionState::Failed, Status::Timeout, Status::Timeout, why);
}

void DzrpClient::check_link() {
    if (state_ != SessionState::Initialising && state_ != SessionState::Probing &&
        state_ != SessionState::Ready)
        return;
    const LinkState ls = link_->state();
    if (ls == LinkState::Failed)
        teardown(SessionState::Failed, Status::LinkLost, Status::LinkLost,
                 link_->last_error().empty() ? "the link failed" : link_->last_error());
    else if (ls == LinkState::Closed)
        teardown(SessionState::Failed, Status::LinkLost, Status::LinkLost,
                 "the remote closed the connection");
}

// End the session: resolve every command (the oldest with `oldest`, the rest Aborted, or all
// LinkLost for a lost link) and close the link.
void DzrpClient::teardown(SessionState to, Status failure, Status oldest, const std::string& why) {
    if (to == SessionState::Failed) {
        error_   = why;
        failure_ = failure;
        Log::debugger()->warn("dzrp client: session failed: {}", why);
    }
    state_ = to;
    bool first = true;
    auto resolve = [&](Cmd& c) {
        if (c.internal != Internal::None) return;
        Reply r;
        r.id     = c.id;
        r.seq    = c.seq;
        r.status = oldest == Status::LinkLost ? Status::LinkLost
                                              : (first ? oldest : Status::Aborted);
        first = false;
        done_[c.ticket] = std::move(r);
    };
    for (Cmd& c : outstanding_) resolve(c);
    for (const Cmd& c : pending_) {
        if (c.internal != Internal::None) continue;
        Reply r;
        r.id     = c.id;
        r.status = oldest == Status::LinkLost ? Status::LinkLost : Status::Aborted;
        done_[c.ticket] = std::move(r);
    }
    outstanding_.clear();
    pending_.clear();
    if (link_) link_->close();
}

std::optional<PauseNotification> DzrpClient::take_pause() {
    if (pauses_.empty()) return std::nullopt;
    PauseNotification n = std::move(pauses_.front());
    pauses_.pop_front();
    return n;
}

void DzrpClient::close() {
    if (state_ == SessionState::Ready && in_set(CMD_CLOSE) &&
        !(is_dezogif(profile_) && running_))
        request(CMD_CLOSE);
    if (state_ != SessionState::Failed) teardown(SessionState::Closed, Status::Ok, Status::Aborted, "");
}

std::unique_ptr<ClientLink> DzrpClient::detach_link() {
    if (!pending_.empty() || !outstanding_.empty()) return nullptr;
    state_ = SessionState::Closed;
    return std::move(link_);
}

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
