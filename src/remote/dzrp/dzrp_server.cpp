#include "remote/dzrp/dzrp_server.h"

#include <algorithm>

#include "core/log.h"

namespace jnext {
namespace remote {
namespace dzrp {

using jnext::dbg::ServiceStep;

namespace {

/// How much of the connection's input one read takes. Only the granularity:
/// the parser never asks for more than the rest of one frame.
constexpr std::size_t kReadChunk = 64 * 1024;

ServerConfig dzrp_server_config() {
    ServerConfig cfg;
    cfg.name = "dzrp";
    // DZRP's second connection is "accepted and immediately closed with a log
    // line" (design §2 row 1): T logs the refusal at warn; nothing is sent.
    cfg.busy_reply.clear();
    return cfg;
}

}  // namespace

// THE TABLE. `GET_SUPPORTED_COMMANDS` is computed from these rows and nothing
// else, so a row here is served AND advertised (unless `legacy`), and a
// command with no row is neither.
const DzrpServer::CommandDef DzrpServer::COMMANDS[] = {
    // id             name             min_len  legacy  handler
    {CMD_LOOPBACK, "CMD_LOOPBACK", 0, false, &DzrpServer::cmd_loopback},
};

const DzrpServer::CommandDef* DzrpServer::find_command(std::uint8_t id) {
    for (const CommandDef& d : COMMANDS)
        if (d.id == id) return &d;
    return nullptr;
}

DzrpServer::DzrpServer(jnext::dbg::Debugger& dbg, Clock clock)
    : dbg_(dbg),
      clock_(clock ? std::move(clock) : Clock([] { return std::chrono::steady_clock::now(); })),
      server_(dzrp_server_config(), *this),
      scratch_(kReadChunk) {}

DzrpServer::~DzrpServer() {
    // Unregistered FIRST: `pump()` must never reach a Server that is going
    // away. Then `stop()`, the orderly end T asks for, so a live client is
    // detached while this object is still whole (T's destructor makes no
    // protocol call).
    dbg_.remove_service(server_);
    server_.stop();
}

// ---------------------------------------------------------------------------
// remote::Protocol
// ---------------------------------------------------------------------------

void DzrpServer::on_connect(Connection& /*c*/) {
    // DZRP attaches on CMD_INIT, not on connect (design §4.1): a connection
    // that never says CMD_INIT has touched nothing.
    parser_.reset();
}

ServiceStep DzrpServer::on_service(Connection& c) {
    const auto now = clock_();

    // Pull at most the rest of ONE frame; the next command stays in T's buffer.
    while (!parser_.ready() && !parser_.failed()) {
        const std::size_t want = std::min({parser_.wanted(), c.available(), scratch_.size()});
        if (want == 0) break;
        const std::size_t got = c.read(scratch_.data(), want);
        parser_.feed(scratch_.data(), got, now);
    }

    if (parser_.failed()) {
        protocol_error(c, parser_.error());
        return ServiceStep::Idle;
    }
    if (parser_.ready()) {
        const Command cmd = parser_.take();
        conn_ = &c;
        execute(cmd);
        conn_ = nullptr;
        return ServiceStep::Serviced;
    }
    if (parser_.stalled(now)) {
        protocol_error(c, "truncated frame: " + std::to_string(parser_.received()) +
                              " bytes and nothing more for " +
                              std::to_string(CHUNK_TIMEOUT.count()) + " ms");
    }
    return ServiceStep::Idle;
}

void DzrpServer::on_notify(Connection& /*c*/) {
    // Nothing is queued yet: `NTF_PAUSE` is WP-3's (design §3.3).
}

void DzrpServer::on_disconnect() {
    parser_.reset();
    conn_ = nullptr;
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void DzrpServer::protocol_error(Connection& c, const std::string& why) {
    Log::debugger()->warn("dzrp: protocol error from {}: {} — closing the connection",
                          c.peer(), why);
    c.close();
    parser_.reset();
}

void DzrpServer::reply(std::uint8_t seq, const std::vector<std::uint8_t>& payload) {
    // The seq byte is echoed VERBATIM — never masked to 4 bits: DeZog 3.8
    // compares it exactly (design §4.2.5), and 2.0/2.1 clients use 1..255.
    if (!conn_) return;
    const std::vector<std::uint8_t> f = encode_response(seq, payload);
    conn_->write(f.data(), f.size());
}

void DzrpServer::execute(const Command& cmd) {
    const CommandDef* def = find_command(cmd.id);
    if (!def) {
        // "Reported unsupported, never silently accepted" (design §0 item 4):
        // a seq-only reply, so the client is not left waiting, and a warn line
        // naming the id. The frame is length-delimited, so it was consumed
        // exactly and the stream stays in sync.
        Log::debugger()->warn("dzrp: unsupported DZRP command {}", cmd.id);
        reply(cmd.seq);
        return;
    }
    if (cmd.payload.size() < def->min_len) {
        Log::debugger()->warn("dzrp: malformed {}: payload is {} bytes, needs at least {}",
                              def->name, cmd.payload.size(), def->min_len);
        reply(cmd.seq);
        return;
    }
    (this->*def->run)(cmd);
}

// ---------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------

// CMD_LOOPBACK (15) — echo, "N is max. 8192". Machine-free: the first thing to
// bring up, it proves the framing without touching the machine.
void DzrpServer::cmd_loopback(const Command& cmd) {
    if (cmd.payload.size() > LOOPBACK_MAX_BYTES) {
        // DZRP has no error field here — the reply IS the data — so an
        // oversize payload is declined with an empty echo and a warn line
        // (dezogif_ng conformance C18: decline in-band, keep serving).
        Log::debugger()->warn("dzrp: CMD_LOOPBACK of {} bytes exceeds the {}-byte maximum — "
                              "not echoed",
                              cmd.payload.size(), LOOPBACK_MAX_BYTES);
        reply(cmd.seq);
        return;
    }
    reply(cmd.seq, cmd.payload);
}

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
