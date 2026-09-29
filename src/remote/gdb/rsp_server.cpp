#include "remote/gdb/rsp_server.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "remote/gdb/target_desc.h"

namespace jnext {
namespace remote {
namespace gdb {

using jnext::dbg::Access;
using jnext::dbg::Action;
using jnext::dbg::CLIENT_NONE;
using jnext::dbg::ClientId;
using jnext::dbg::ClientInfo;
using jnext::dbg::ClientKind;
using jnext::dbg::EventId;
using jnext::dbg::EventKind;
using jnext::dbg::MemSpace;
using jnext::dbg::PausedInfo;
using jnext::dbg::PauseReason;
using jnext::dbg::RegId;
using jnext::dbg::ResetKind;
using jnext::dbg::Result;
using jnext::dbg::result_name;
using jnext::dbg::ServiceStep;
using jnext::dbg::Subscription;

namespace {

/// How much of the connection's input one read takes — enough for the largest
/// packet the parser accepts, so one read normally completes one packet.
constexpr std::size_t kReadChunk = MAX_PACKET_BODY + 4;

/// The longest `m` read (§2 row 7). The client asks for 32 bytes at a time.
constexpr std::uint32_t kMaxRead = 4096;

/// `monitor page`'s default and largest dump.
constexpr std::uint32_t kPageDumpDefault = 16;
constexpr std::uint32_t kPageDumpMax     = 256;

/// The two stop-reply stems (§5.3). Thread 1: the one thread there is.
const char kStopTrap[] = "T05thread:1;";
const char kStopInt[]  = "T02thread:1;";

/// The `qSupported` reply (§2 row 1): the size in decimal digits only, the
/// target description, and the two stop-reason fields a real gdb needs to be
/// told about. No `QStartNoAckMode`, no `vContSupported`, no conditions.
std::string supported_reply() {
    return std::string("PacketSize=") + PACKET_SIZE_FIELD +
           ";qXfer:features:read+;swbreak+;hwbreak+";
}

ServerConfig gdb_server_config() {
    ServerConfig cfg;
    cfg.name = "gdb";
    // "A second connection is accepted and closed with nothing sent, logged
    // at warn" (§6.1): T logs the refusal; nothing is sent.
    cfg.busy_reply.clear();
    return cfg;
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

std::string strf(const char* fmt, ...) {
    char    buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

/// A packet as a log line may show it: printable ASCII, bounded.
std::string printable(const std::string& s, std::size_t max = 64) {
    std::string out;
    for (std::size_t i = 0; i < s.size() && i < max; ++i)
        out.push_back(std::isprint(static_cast<unsigned char>(s[i])) ? s[i] : '?');
    if (s.size() > max) out += "...";
    return out;
}

/// "<hex>,<hex>" → two values. False unless both are well-formed hex.
bool parse_pair(const std::string& s, std::uint32_t& a, std::uint32_t& b) {
    const std::size_t comma = s.find(',');
    if (comma == std::string::npos) return false;
    return parse_hex_u32(s.substr(0, comma), a) && parse_hex_u32(s.substr(comma + 1), b);
}

/// A `monitor` number: `0x…` or `$…` hex, or decimal (§4.3). False on
/// anything else, including a value past 32 bits.
bool parse_number(const std::string& s, std::uint32_t& out) {
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return parse_hex_u32(s.substr(2), out);
    if (s.size() > 1 && s[0] == '$') return parse_hex_u32(s.substr(1), out);
    if (s.empty() || s.size() > 10) return false;
    std::uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (v > 0xFFFFFFFFull) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}

std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> out;
    std::string              w;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!w.empty()) out.push_back(w);
            w.clear();
        } else {
            w.push_back(c);
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const char* kind_name(EventKind k) {
    switch (k) {
        case EventKind::Execute:      return "execute";
        case EventKind::Mem:          return "mem";
        case EventKind::Port:         return "port";
        case EventKind::NextRegWrite: return "nextreg";
        case EventKind::Frame:        return "frame";
        case EventKind::Scanline:     return "scanline";
        case EventKind::Cycle:        return "cycle";
        case EventKind::Reset:        return "reset";
        case EventKind::IntAck:       return "intack";
        case EventKind::Nmi:          return "nmi";
        case EventKind::Magic:        return "magic";
        case EventKind::Host:         return "host";
        case EventKind::Copper:       return "copper";
        case EventKind::Dma:          return "dma";
        case EventKind::Count:        break;
    }
    return "?";
}

const char* access_name(Access a) {
    switch (a) {
        case Access::Read:      return "read";
        case Access::Write:     return "write";
        case Access::ReadWrite: return "access";
        case Access::None:      break;
    }
    return "none";
}

}  // namespace

GdbServer::GdbServer(jnext::dbg::Debugger& dbg)
    : dbg_(dbg), server_(gdb_server_config(), *this), scratch_(kReadChunk) {}

GdbServer::~GdbServer() {
    // Unregistered FIRST: `pump()` must never reach a Server that is going
    // away. Then `stop()`, T's orderly end, so a live client is detached while
    // this object is still whole (T's destructor makes no protocol call).
    dbg_.remove_service(server_);
    server_.stop();
}

// ---------------------------------------------------------------------------
// remote::Protocol
// ---------------------------------------------------------------------------

// RSP has no session-opening command: the client's first packet already needs
// the machine (`qSupported` does not, but `?` right after it does), so the
// client is attached as soon as it is admitted.
void GdbServer::on_connect(Connection& c) {
    parser_.reset();
    const auto a = dbg_.attach(ClientInfo{"GDB RSP client (" + c.peer() + ")", ClientKind::GdbRsp});
    if (!a) {
        Log::debugger()->warn("gdb: client from {} refused — attach failed: {}", c.peer(),
                              result_name(a.status));
        c.close();
        return;
    }
    cid_ = a.value;
    // SES-02: this client's stops arrive as `on_paused()` pushes.
    dbg_.set_listener(cid_, this);
}

ServiceStep GdbServer::on_service(Connection& c) {
    if (c.closing() || cid_ == CLIENT_NONE) return ServiceStep::Idle;
    RspEvent ev;
    for (;;) {
        if (parser_.next(ev)) {
            switch (ev.kind) {
                case RspEvent::Kind::BadChecksum:
                    Log::debugger()->warn("gdb: packet from {} with a bad checksum dropped "
                                          "(answered '-'): {}",
                                          c.peer(), printable(ev.body));
                    c.write("-");
                    continue;
                case RspEvent::Kind::Oversize:
                    Log::debugger()->warn("gdb: packet from {} longer than {} bytes dropped "
                                          "(answered '-')",
                                          c.peer(), MAX_PACKET_BODY);
                    c.write("-");
                    continue;
                case RspEvent::Kind::Interrupt:
                    conn_ = &c;
                    pkt_interrupt();
                    conn_ = nullptr;
                    return ServiceStep::Serviced;
                case RspEvent::Kind::Packet:
                    // The ack goes first; the client erases it (§1.2), a real
                    // gdb in ack mode needs it.
                    c.write("+");
                    conn_ = &c;
                    dispatch(ev.body);
                    conn_ = nullptr;
                    return ServiceStep::Serviced;
            }
        }
        // Only an empty parser is fed, so what this adapter holds beyond T's
        // own buffer is bounded by one read.
        if (c.available() == 0) return ServiceStep::Idle;
        const std::size_t got = c.read(scratch_.data(), scratch_.size());
        parser_.feed(scratch_.data(), got);
    }
}

// The post-frame flush: `pump()` calls this AFTER its drain and after the
// backend's `Paused` push, so a stop in this tick's frames reaches the client
// in this tick, and after every reply the drain wrote.
void GdbServer::on_notify(Connection& c) {
    if (!pending_) return;
    const PausedInfo info = *pending_;
    pending_.reset();
    std::string body;
    switch (owed_) {
        case Owed::Question:  body = kStopTrap; break;
        case Owed::Interrupt: body = kStopInt; break;
        case Owed::Continue:  body = stop_reply(info); break;
        case Owed::None:      return;
    }
    owed_ = Owed::None;
    c.write(frame_packet(body));
}

void GdbServer::on_disconnect() {
    // A dropped socket is a `D` (§6.3): SES-01's detach removes this client's
    // subscriptions and releases its own pause.
    end_session();
    parser_.reset();
    conn_ = nullptr;
}

void GdbServer::end_session() {
    if (cid_ == CLIENT_NONE) return;
    dbg_.detach(cid_);
    cid_          = CLIENT_NONE;
    last_resumer_ = CLIENT_NONE;
    owed_         = Owed::None;
    pending_.reset();
    bps_.clear();
}

// ---------------------------------------------------------------------------
// dbg::Listener — the backend's pushes (SES-02)
// ---------------------------------------------------------------------------

// Kept only while a stop reply is owed (§5.4 rule 2); dropped otherwise (rule
// 3). Written out by `on_notify()`, the Protocol callback that has the
// connection.
void GdbServer::on_paused(const PausedInfo& info) {
    if (owed_ == Owed::None) return;
    pending_ = info;
}

void GdbServer::on_resumed(ClientId by) { last_resumer_ = by; }

// A hard reset never pauses and is not a stop (§4.3): a client mid-run gets
// nothing until a real stop, a stopped one reads the fresh machine at its next
// `g`. The other pushes carry nothing RSP can say.
void GdbServer::on_reset(ResetKind /*kind*/) {}
void GdbServer::on_frame_ended(std::uint32_t /*frame*/) {}
void GdbServer::on_subscriptions_changed(jnext::dbg::EventKindMask /*kinds*/) {}
void GdbServer::on_exit_requested(int /*code*/) {}
void GdbServer::on_log(jnext::dbg::LogLevel /*level*/, const std::string& /*text*/) {}

// ---------------------------------------------------------------------------
// Dispatch — the §2 packet table
// ---------------------------------------------------------------------------

void GdbServer::reply(const std::string& body) {
    if (conn_) conn_->write(frame_packet(body));
}

// "The empty response is used to indicate that a packet is not supported"
// (RSP). Logged at debug with the raw packet, so a user can see what their
// client wanted (§2 row 33).
void GdbServer::unsupported(const std::string& body) {
    Log::debugger()->debug("gdb: unsupported packet \"{}\" — empty reply", printable(body));
    reply("");
}

void GdbServer::pause_first() {
    if (dbg_.state().paused) return;
    Log::debugger()->info("gdb: client re-paused the machine (resumed by client {})",
                          last_resumer_);
    if (const Result r = dbg_.pause(cid_); r != Result::Ok)
        Log::debugger()->warn("gdb: pause refused: {}", result_name(r));
}

void GdbServer::dispatch(const std::string& body) {
    if (body.empty()) {
        unsupported(body);
        return;
    }
    switch (body[0]) {
        case 'q': pkt_query(body); return;
        case '?':
            if (body.size() != 1) break;
            pkt_question();
            return;
        case 'g':
            if (body.size() != 1) break;
            pause_first();
            pkt_read_registers();
            return;
        case 'G': pause_first(); pkt_write_registers(body); return;
        case 'p': pause_first(); pkt_read_register(body); return;
        case 'P': pause_first(); pkt_write_register(body); return;
        case 'm': pause_first(); pkt_read_memory(body); return;
        case 'M': pause_first(); pkt_write_memory(body, false); return;
        case 'X': pause_first(); pkt_write_memory(body, true); return;
        case 'Z':
        case 'z': pause_first(); pkt_breakpoint(body); return;
        case 'c': pkt_continue(body); return;
        case 's': pkt_step(body); return;
        case 'i': pkt_step_over(body); return;
        case 'D':
            // `D` or the multiprocess `D;pid`: detach either way.
            pkt_detach(true);
            return;
        case 'k':
            if (body.size() != 1) break;
            // Same as `D`, minus the reply (§2 row 20): jnext does not exit on
            // `k` — killing the emulator from a debugger is not a feature.
            pkt_detach(false);
            return;
        case 'H':
            // Single thread: any `Hg`/`Hc` selection is accepted.
            if (body.size() < 2) break;
            reply("OK");
            return;
        case 'T':
            // Thread alive: the one there is.
            reply("OK");
            return;
        default:
            break;
    }
    // `v…` (vCont, vCtrlC, vRun, …), `Q…` (QStartNoAckMode, …), `bc`/`bs`,
    // `R`, `C`/`S`, and anything no row names (§2 rows 26-33).
    unsupported(body);
}

void GdbServer::pkt_query(const std::string& body) {
    if (body == "qSupported" || starts_with(body, "qSupported:")) {
        reply(supported_reply());
    } else if (starts_with(body, "qXfer:")) {
        pkt_xfer(body);
    } else if (starts_with(body, "qRcmd,")) {
        pause_first();
        pkt_monitor(body);
    } else if (body == "qC") {
        reply("QC1");
    } else if (body == "qAttached" || starts_with(body, "qAttached:")) {
        // "Attached to an existing process": a gdb `quit` then detaches
        // instead of killing (§2 row 24).
        reply("1");
    } else if (body == "qfThreadInfo") {
        reply("m1");
    } else if (body == "qsThreadInfo") {
        reply("l");
    } else {
        unsupported(body);
    }
}

// qXfer:features:read:<annex>:<off>,<len> — the target description (§2 row 2,
// §3). One object only; any other `qXfer` object is unsupported (row 31).
void GdbServer::pkt_xfer(const std::string& body) {
    static const char kPrefix[] = "qXfer:features:read:";
    if (!starts_with(body, kPrefix)) {
        unsupported(body);
        return;
    }
    const std::string rest  = body.substr(sizeof(kPrefix) - 1);
    const std::size_t colon = rest.find(':');
    std::uint32_t     off = 0, len = 0;
    if (colon == std::string::npos || rest.substr(0, colon) != "target.xml" ||
        !parse_pair(rest.substr(colon + 1), off, len) || len == 0) {
        reply("E00");
        return;
    }
    if (off >= TARGET_XML_BYTES) {
        reply("l");
        return;
    }
    const std::size_t n    = std::min<std::size_t>(len, TARGET_XML_BYTES - off);
    const bool        last = off + n >= TARGET_XML_BYTES;
    reply(std::string(last ? "l" : "m") + std::string(TARGET_XML + off, n));
}

// ---------------------------------------------------------------------------
// Run control and stop replies — §5
// ---------------------------------------------------------------------------

// `?` — the packet that starts the session: the client enters its prompt only
// when a `T` arrives, and takes the NEXT packet after that as its `g` reply,
// so the `T` must be emitted exactly once (§5.4, N-12).
void GdbServer::pkt_question() {
    const bool paused = dbg_.state().paused;
    if (owed_ != Owed::None) {
        // A stop reply is already owed: the pause edge on its way answers it,
        // once. Pause a running machine so that edge comes.
        if (!paused) dbg_.pause(cid_);
        return;
    }
    if (paused) {
        // Already stopped (by anyone): answer now, and do NOT re-pause —
        // `pause()` would re-attribute a pause that is not this client's, and
        // its detach would then release it.
        reply(kStopTrap);
        return;
    }
    if (const Result r = dbg_.pause(cid_); r != Result::Ok) {
        Log::debugger()->warn("gdb: '?' could not pause the machine: {}", result_name(r));
        reply("E01");
        return;
    }
    owed_ = Owed::Question;
}

// 0x03 — Ctrl-C, or the client's temporary break to edit breakpoints while it
// believes the machine runs (§1.1). Signal 2, SIGINT (§2 row 18).
void GdbServer::pkt_interrupt() {
    const bool paused = dbg_.state().paused;
    if (owed_ != Owed::None) {
        // The Ctrl-C of a `c`: pause, and the edge answers the `c` (§5.3 gives
        // T02 for this client's own pause).
        if (!paused) dbg_.pause(cid_);
        return;
    }
    if (paused) {
        reply(kStopInt);
        return;
    }
    if (const Result r = dbg_.pause(cid_); r != Result::Ok) {
        Log::debugger()->warn("gdb: interrupt could not pause the machine: {}", result_name(r));
        return;
    }
    owed_ = Owed::Interrupt;
}

// The optional `c <addr>` / `s <addr>`: resume at `addr` (a `P pc` first).
static bool resume_address(const std::string& body, std::uint32_t& addr, bool& given) {
    given = body.size() > 1;
    if (!given) return true;
    return parse_hex_u32(body.substr(1), addr) && addr <= 0xFFFF;
}

// `c` — CAP-CTL-02 `run()`; the GH #221 step-off is the backend's. The reply
// is the stop reply, later. A refusal (the CTL-11 corruption gate) is `E01`
// now, and the machine stays paused.
void GdbServer::pkt_continue(const std::string& body) {
    std::uint32_t addr  = 0;
    bool          given = false;
    if (!resume_address(body, addr, given)) {
        reply("E01");
        return;
    }
    if (given) {
        if (const Result r = dbg_.set_register(cid_, RegId::PC, static_cast<std::uint16_t>(addr));
            r != Result::Ok) {
            Log::debugger()->warn("gdb: 'c {:04x}' refused: {}", addr, result_name(r));
            reply("E01");
            return;
        }
    }
    if (const Result r = dbg_.run(cid_); r != Result::Ok) {
        Log::debugger()->warn("gdb: continue refused: {}", result_name(r));
        reply("E01");
        return;
    }
    owed_ = Owed::Continue;
}

// `s` — CAP-CTL-03, synchronous: the reply goes out in the same pump.
void GdbServer::pkt_step(const std::string& body) {
    std::uint32_t addr  = 0;
    bool          given = false;
    if (!resume_address(body, addr, given)) {
        reply("E01");
        return;
    }
    if (given) {
        pause_first();
        if (const Result r = dbg_.set_register(cid_, RegId::PC, static_cast<std::uint16_t>(addr));
            r != Result::Ok) {
            reply("E01");
            return;
        }
    }
    if (const Result r = dbg_.step_into(cid_); r != Result::Ok) {
        Log::debugger()->warn("gdb: step refused: {}", result_name(r));
        reply("E01");
        return;
    }
    // The step's own pause edge will be pushed after the drain; it is this
    // reply, not a second one.
    owed_ = Owed::None;
    pending_.reset();
    reply(kStopTrap);
}

// `i<decimal length>` — z88dk-gdb's `nexti` over a CALL (or an ED/CB-prefixed
// instruction), NOT RSP's cycle step: run to PC + length, the length computed
// by the client's own disassembler (§1.1, §2 row 17). CAP-CTL-06 `run_to`,
// whose target is a transient subscription owned by this client.
void GdbServer::pkt_step_over(const std::string& body) {
    const std::string num = body.substr(1);
    bool              ok  = !num.empty() && num.size() <= 5;
    std::uint32_t     len = 0;
    for (char c : num) {
        if (c < '0' || c > '9') {
            ok = false;
            break;
        }
        len = len * 10 + static_cast<std::uint32_t>(c - '0');
    }
    if (!ok || len == 0 || len > 0xFFFF) {
        // `i` alone, or RSP's own `i<addr>,<n>`: jnext does not cycle-step.
        reply("E01");
        return;
    }
    const std::uint16_t target = static_cast<std::uint16_t>(dbg_.registers().PC + len);
    if (const Result r = dbg_.run_to(cid_, target); r != Result::Ok) {
        Log::debugger()->warn("gdb: 'i{}' refused: {}", len, result_name(r));
        reply("E01");
        return;
    }
    owed_ = Owed::Continue;
}

// `D` (reply OK) and `k` (no reply) — CAP-SES-01 detach, then close. The
// socket closes after the reply is flushed (T's linger).
void GdbServer::pkt_detach(bool reply_ok) {
    Log::debugger()->info("gdb: client {} {}", cid_, reply_ok ? "detached (D)" : "sent 'k' — detached");
    Connection* c = conn_;
    end_session();
    if (reply_ok) reply("OK");
    if (c) c->close();
}

// The §5.3 table, for a stop owed to `c` / `i`.
std::string GdbServer::stop_reply(const PausedInfo& info) const {
    const PauseReason& r = info.reason;
    if (r.kind == PauseReason::Kind::Breakpoint || r.kind == PauseReason::Kind::Watch) {
        // `matched[]` lists EVERY subscription that stopped here: the first one
        // this client owns is the reason it is told (RSP carries one).
        for (const jnext::dbg::Hit& h : info.matched) {
            for (const auto& kv : bps_) {
                if (kv.second != h.event_id) continue;
                const char type = kv.first.type;
                if (type == '0' || type == '1') return std::string(kStopTrap) + "swbreak:;";
                const char* field = type == '2' ? "watch" : type == '3' ? "rwatch" : "awatch";
                return std::string(kStopTrap) + strf("%s:%x;", field, h.addr);
            }
        }
    }
    if ((r.kind == PauseReason::Kind::Step || r.kind == PauseReason::Kind::RunTo) && r.by == cid_)
        return kStopTrap;
    // Another client's pause or breakpoint, a magic breakpoint, a script, a
    // corruption: "something else stopped it" (§5.3).
    return kStopInt;
}

// ---------------------------------------------------------------------------
// Registers — §3.2, CAP-INS-01 / CAP-INS-07
// ---------------------------------------------------------------------------

void GdbServer::pkt_read_registers() {
    reply(pack_registers(dbg_.registers(), dbg_.time().tstates_total));
}

// `G`: the 12 pairs; `clockl`/`clockh` are read-only and ignored (the client
// sends them as 0000, §7.1). Only a register whose value CHANGES is written:
// the client's `set hl …` sends the whole file, and writing an unchanged PC
// would clear a HALT (`set_register(PC)` does, §4.2a) and log eleven MUTATE
// lines for one register.
void GdbServer::pkt_write_registers(const std::string& body) {
    std::array<std::uint16_t, REG_COUNT> v{};
    if (!unpack_registers(body.substr(1), v)) {
        reply("E01");
        return;
    }
    const Z80Registers now = dbg_.registers();
    for (int n = 0; n < REG_WRITABLE; ++n) {
        if (v[static_cast<std::size_t>(n)] == reg_value(n, now, 0)) continue;
        if (const Result r = dbg_.set_register(cid_, REG_IDS[static_cast<std::size_t>(n)],
                                               v[static_cast<std::size_t>(n)]);
            r != Result::Ok) {
            Log::debugger()->warn("gdb: 'G' refused at {}: {}", REG_NAMES[static_cast<std::size_t>(n)],
                                  result_name(r));
            reply("E01");
            return;
        }
    }
    reply("OK");
}

void GdbServer::pkt_read_register(const std::string& body) {
    std::uint32_t n = 0;
    if (!parse_hex_u32(body.substr(1), n) || n >= static_cast<std::uint32_t>(REG_COUNT)) {
        reply("E01");
        return;
    }
    reply(pack_register(reg_value(static_cast<int>(n), dbg_.registers(),
                                   dbg_.time().tstates_total)));
}

void GdbServer::pkt_write_register(const std::string& body) {
    const std::size_t eq = body.find('=');
    std::uint32_t     n  = 0;
    std::uint16_t     v  = 0;
    if (eq == std::string::npos || !parse_hex_u32(body.substr(1, eq - 1), n) ||
        n >= static_cast<std::uint32_t>(REG_WRITABLE) || !unpack_register(body.substr(eq + 1), v)) {
        // Out of range, malformed, or `clockl`/`clockh` (read-only).
        reply("E01");
        return;
    }
    if (const Result r = dbg_.set_register(cid_, REG_IDS[n], v); r != Result::Ok) {
        Log::debugger()->warn("gdb: 'P' refused: {}", result_name(r));
        reply("E01");
        return;
    }
    reply("OK");
}

// ---------------------------------------------------------------------------
// Memory — §4.1, the flat 64 K CPU view (CAP-INS-02 `MemSpace::Cpu`)
// ---------------------------------------------------------------------------

// `m<addr>,<len>`: side-effect free (`peek(Cpu)` does not latch the +3
// floating bus). A read past 0xFFFF is clipped; one that starts past it, or
// reads nothing (length 0), is `E01` — an EMPTY reply would mean "m
// unsupported".
void GdbServer::pkt_read_memory(const std::string& body) {
    std::uint32_t addr = 0, len = 0;
    if (!parse_pair(body.substr(1), addr, len) || addr > 0xFFFF || len > kMaxRead) {
        reply("E01");
        return;
    }
    const std::uint32_t n = std::min<std::uint32_t>(len, 0x10000 - addr);
    std::vector<std::uint8_t> buf(n);
    const auto got = dbg_.peek(MemSpace::cpu(), addr, n, buf.data());
    if (got.value == 0) {
        reply("E01");
        return;
    }
    reply(to_hex(buf.data(), got.value));
}

// `M<addr>,<len>:<hex>` and `X<addr>,<len>:<binary>`: through the live map, as
// the CPU would write it — overlays honoured, so a byte Layer 2 write-over or
// DivMMC / Multiface RAM takes lands. `poke(Cpu)` counts what landed (GH #281
// F1): anything short of all of it is `E01` (§4.1, REQ-gdb-6), and the bytes
// that could land did, exactly as the CPU's own write would have left them.
// A range past 0xFFFF is refused before anything is written (the backend would
// wrap it to 0x0000). `X` with length 0 is gdb's probe for `X` support: `OK`.
void GdbServer::pkt_write_memory(const std::string& body, bool binary) {
    const std::size_t colon = body.find(':');
    std::uint32_t     addr = 0, len = 0;
    if (colon == std::string::npos || !parse_pair(body.substr(1, colon - 1), addr, len) ||
        addr > 0xFFFF || std::uint64_t{addr} + len > 0x10000) {
        reply("E01");
        return;
    }
    std::string data;
    const std::string raw = body.substr(colon + 1);
    const bool decoded = binary ? unescape_binary(raw, data) : from_hex(raw, data);
    if (!decoded || data.size() != len) {
        reply("E01");
        return;
    }
    if (len == 0) {
        reply("OK");
        return;
    }
    const auto w = dbg_.poke(cid_, MemSpace::cpu(), addr, len,
                             reinterpret_cast<const std::uint8_t*>(data.data()));
    if (w.status != Result::Ok) {
        Log::debugger()->warn("gdb: write of {} bytes at 0x{:04X}: {} landed ({})", len, addr,
                              w.value, result_name(w.status));
        reply("E01");
        return;
    }
    reply("OK");
}

// ---------------------------------------------------------------------------
// Breakpoints and watchpoints — §2 rows 10-14, §5.1 (CAP-EVT)
// ---------------------------------------------------------------------------

// `Z<t>,<addr>,<kind>` / `z<t>,…`. Z0/Z1: one `Execute[addr,addr]` stop owned
// by this client (`kind` ignored: a Z80 breakpoint has no size). Z2/Z3/Z4: one
// `Mem[addr, addr+len-1]` stop, write / read / either. A duplicate insert is
// the same subscription; a `z` for something never inserted is `OK` — the
// client sends `z0` for its own client-side breakpoints on quit (§7.1).
void GdbServer::pkt_breakpoint(const std::string& body) {
    const bool insert = body[0] == 'Z';
    if (body.size() < 2 || body[1] < '0' || body[1] > '4' || (body.size() > 2 && body[2] != ',')) {
        unsupported(body);
        return;
    }
    const char        type = body[1];
    const std::string args = body.size() > 3 ? body.substr(3) : std::string();
    std::uint32_t     addr = 0, kind = 0;
    // A `;cond_list` / `;cmds` tail fails the parse of `kind`, so it is E01:
    // conditions are declined (§2 row 29) — never advertised, so a gdb
    // evaluates them itself.
    if (!parse_pair(args, addr, kind) || addr > 0xFFFF) {
        reply("E01");
        return;
    }
    const bool   code = type == '0' || type == '1';
    const BpKey  key{type, addr, code ? 0u : kind};
    const auto   it = bps_.find(key);

    if (!insert) {
        if (it != bps_.end()) {
            dbg_.unsubscribe(cid_, it->second);
            bps_.erase(it);
        }
        reply("OK");
        return;
    }
    if (it != bps_.end()) {
        reply("OK");
        return;
    }
    Subscription sub;
    sub.action = Action::Stop;
    if (code) {
        sub.kind      = EventKind::Execute;
        sub.filter.lo = sub.filter.hi = static_cast<std::uint16_t>(addr);
    } else {
        if (kind == 0 || std::uint64_t{addr} + kind - 1 > 0xFFFF) {
            // A watch of nothing, or one that wraps past 0xFFFF.
            reply("E01");
            return;
        }
        sub.kind      = EventKind::Mem;
        sub.filter.lo = static_cast<std::uint16_t>(addr);
        sub.filter.hi = static_cast<std::uint16_t>(addr + kind - 1);
        sub.access    = type == '2' ? Access::Write : type == '3' ? Access::Read : Access::ReadWrite;
    }
    const auto s = dbg_.subscribe(cid_, sub);
    if (!s) {
        Log::debugger()->warn("gdb: 'Z{}' at 0x{:04X} refused: {}", type, addr, result_name(s.status));
        reply("E01");
        return;
    }
    bps_[key] = s.value;
    reply("OK");
}

// ---------------------------------------------------------------------------
// monitor (qRcmd) — §4.3
// ---------------------------------------------------------------------------

void GdbServer::monitor_line(const std::string& text) {
    reply("O" + to_hex(text + "\n"));
}

void GdbServer::pkt_monitor(const std::string& body) {
    std::string text;
    if (!from_hex(body.substr(6), text)) {
        reply("E01");
        return;
    }
    const std::vector<std::string> w = split_words(text);
    const std::string              verb = w.empty() ? std::string("help") : lower(w[0]);
    const std::vector<std::string> args(w.empty() ? w.end() : w.begin() + 1, w.end());

    bool ok = true;
    if (verb == "help")         monitor_help();
    else if (verb == "regs")    ok = monitor_regs(args);
    else if (verb == "set")     ok = monitor_set(args);
    else if (verb == "mmu")     ok = monitor_mmu(args);
    else if (verb == "nextreg") ok = monitor_nextreg(args);
    else if (verb == "page")    ok = monitor_page(args);
    else if (verb == "in")      ok = monitor_in(args);
    else if (verb == "out")     ok = monitor_out(args);
    else if (verb == "sym")     ok = monitor_sym(args);
    else if (verb == "time")    ok = monitor_time(args);
    else if (verb == "reset")   ok = monitor_reset(args);
    else if (verb == "bp")      ok = monitor_bp(args);
    else {
        // NOT empty: an empty reply would mean "qRcmd unsupported", and the
        // client would print nothing at all (§2 row 21).
        monitor_line("unknown monitor command \"" + printable(verb, 32) + "\"; try help");
    }
    reply(ok ? "OK" : "E01");
}

void GdbServer::monitor_help() {
    static const char* const kLines[] = {
        "jnext monitor commands (numbers: 0x.., $.. or decimal):",
        "  regs                       I R IFF1 IFF2 IM HALT MEMPTR and the pairs",
        "  set i|r|iff1|iff2|im VAL   write one of those",
        "  mmu                        the eight slots and the paging ports",
        "  mmu SLOT PAGE              map NR page PAGE at SLOT (as NEXTREG 0x50+SLOT)",
        "  nextreg [REG [VAL]]        all NextREGs, one, or write one",
        "  page N OFF [LEN]           hex dump of physical 8K page N",
        "  in PORT                    read a port - PERTURBING: runs its read side effects",
        "  out PORT VAL               write a port - PERTURBING, as a guest OUT",
        "  sym NAME|ADDR              a symbol's address, or the symbol at/below ADDR",
        "  time                       frame, cycle, T-states, raster position",
        "  reset [soft|hard]          reset the machine; it stays stopped",
        "  bp                         every breakpoint and watchpoint, with its owner",
    };
    for (const char* l : kLines) monitor_line(l);
}

bool GdbServer::monitor_regs(const std::vector<std::string>& args) {
    if (!args.empty()) return false;
    const Z80Registers r = dbg_.registers();
    monitor_line(strf("PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X", r.PC,
                      r.SP, r.AF, r.BC, r.DE, r.HL, r.IX, r.IY));
    monitor_line(strf("AF'=%04X BC'=%04X DE'=%04X HL'=%04X", r.AF2, r.BC2, r.DE2, r.HL2));
    monitor_line(strf("I=%02X R=%02X IFF1=%u IFF2=%u IM=%u HALT=%u MEMPTR=%04X", r.I, r.R,
                      r.IFF1 ? 1u : 0u, r.IFF2 ? 1u : 0u, static_cast<unsigned>(r.IM),
                      r.halted ? 1u : 0u, r.MEMPTR));
    return true;
}

// `set <i|r|iff1|iff2|im> <val>` — the registers the target description
// leaves out on purpose (§3.1).
bool GdbServer::monitor_set(const std::vector<std::string>& args) {
    std::uint32_t v = 0;
    if (args.size() != 2 || !parse_number(args[1], v)) return false;
    const std::string reg = lower(args[0]);
    RegId             id;
    std::uint32_t     max = 0xFF;
    if (reg == "i")         id = RegId::I;
    else if (reg == "r")    id = RegId::R;
    else if (reg == "iff1") { id = RegId::IFF1; max = 1; }
    else if (reg == "iff2") { id = RegId::IFF2; max = 1; }
    else if (reg == "im")   { id = RegId::IM;   max = 2; }
    else return false;
    if (v > max) return false;
    const Result r = dbg_.set_register(cid_, id, static_cast<std::uint16_t>(v));
    if (r != Result::Ok) monitor_line(std::string("refused: ") + result_name(r));
    return r == Result::Ok;
}

bool GdbServer::monitor_mmu(const std::vector<std::string>& args) {
    if (args.empty()) {
        const auto slots = dbg_.mmu_slots();
        for (std::size_t s = 0; s < slots.size(); ++s)
            monitor_line(strf("slot %u: page %02X (effective %02X)%s", static_cast<unsigned>(s),
                              slots[s].nr_page, slots[s].effective_page,
                              slots[s].is_rom ? " ROM" : ""));
        const auto p = dbg_.paging_ports();
        monitor_line(strf("7FFD=%02X 1FFD=%02X DFFD=%02X", p.port_7ffd, p.port_1ffd, p.port_dffd));
        return true;
    }
    std::uint32_t slot = 0, page = 0;
    if (args.size() != 2 || !parse_number(args[0], slot) || !parse_number(args[1], page) ||
        slot > 7 || page > 0xFF)
        return false;
    const Result r = dbg_.set_mmu_slot(cid_, static_cast<int>(slot), static_cast<std::uint8_t>(page));
    if (r != Result::Ok) monitor_line(std::string("refused: ") + result_name(r));
    return r == Result::Ok;
}

// NextREGs through their READ path (`nextreg_peek`: side-effect free) and
// their WRITE handler.
bool GdbServer::monitor_nextreg(const std::vector<std::string>& args) {
    std::uint32_t reg = 0, val = 0;
    if (args.empty()) {
        for (int row = 0; row < 256; row += 16) {
            std::string l = strf("%02X:", row);
            for (int i = 0; i < 16; ++i)
                l += strf(" %02X", dbg_.nextreg_peek(static_cast<std::uint8_t>(row + i)));
            monitor_line(l);
        }
        return true;
    }
    if (!parse_number(args[0], reg) || reg > 0xFF) return false;
    if (args.size() == 1) {
        monitor_line(strf("NR %02X = %02X", reg, dbg_.nextreg_peek(static_cast<std::uint8_t>(reg))));
        return true;
    }
    if (args.size() != 2 || !parse_number(args[1], val) || val > 0xFF) return false;
    const Result r = dbg_.nextreg_write(cid_, static_cast<std::uint8_t>(reg),
                                        static_cast<std::uint8_t>(val));
    if (r != Result::Ok) monitor_line(std::string("refused: ") + result_name(r));
    return r == Result::Ok;
}

// `page <n> <off> [len]` — CAP-INS-02 `peek(Page{n})`: the physical page,
// regardless of what is mapped anywhere.
bool GdbServer::monitor_page(const std::vector<std::string>& args) {
    std::uint32_t page = 0, off = 0, len = kPageDumpDefault;
    if (args.size() < 2 || args.size() > 3 || !parse_number(args[0], page) ||
        !parse_number(args[1], off) || (args.size() == 3 && !parse_number(args[2], len)))
        return false;
    if (page > 0xFF || off >= 0x2000 || len == 0 || len > kPageDumpMax) return false;
    len = std::min<std::uint32_t>(len, 0x2000 - off);
    std::vector<std::uint8_t> buf(len);
    const auto got = dbg_.peek(MemSpace::page(static_cast<std::uint16_t>(page)), off, len, buf.data());
    if (got.status != Result::Ok) {
        monitor_line(strf("page %02X: %s", page, result_name(got.status)));
        return false;
    }
    for (std::uint32_t i = 0; i < got.value; i += 16) {
        std::string l = strf("page %02X +%04X:", page, off + i);
        for (std::uint32_t j = i; j < got.value && j < i + 16; ++j) l += strf(" %02X", buf[j]);
        monitor_line(l);
    }
    return true;
}

bool GdbServer::monitor_in(const std::vector<std::string>& args) {
    std::uint32_t port = 0;
    if (args.size() != 1 || !parse_number(args[0], port) || port > 0xFFFF) return false;
    const auto v = dbg_.port_in(cid_, static_cast<std::uint16_t>(port));
    if (!v) {
        monitor_line(std::string("refused: ") + result_name(v.status));
        return false;
    }
    monitor_line(strf("IN %04X = %02X", port, v.value));
    return true;
}

bool GdbServer::monitor_out(const std::vector<std::string>& args) {
    std::uint32_t port = 0, val = 0;
    if (args.size() != 2 || !parse_number(args[0], port) || !parse_number(args[1], val) ||
        port > 0xFFFF || val > 0xFF)
        return false;
    const Result r = dbg_.port_out(cid_, static_cast<std::uint16_t>(port),
                                   static_cast<std::uint8_t>(val));
    if (r != Result::Ok) monitor_line(std::string("refused: ") + result_name(r));
    return r == Result::Ok;
}

// CAP-SYM — jnext's own table (the GUI's Map menu), for cross-checking the
// client's `-x` map.
bool GdbServer::monitor_sym(const std::vector<std::string>& args) {
    if (args.size() != 1) return false;
    std::uint32_t addr = 0;
    if (parse_number(args[0], addr)) {
        if (addr > 0xFFFF) return false;
        const auto& table = dbg_.symbols().symbols();
        auto        it    = table.upper_bound(static_cast<std::uint16_t>(addr));
        if (it == table.begin()) {
            monitor_line(strf("no symbol at or below $%04X", addr));
            return true;
        }
        --it;
        const std::uint32_t delta = addr - it->first;
        monitor_line(delta == 0 ? strf("$%04X = ", addr) + it->second
                                : strf("$%04X = ", addr) + it->second + strf("+%u", delta));
        return true;
    }
    const auto a = dbg_.lookup_name(args[0]);
    if (!a) monitor_line("no symbol " + printable(args[0], 64));
    else    monitor_line(args[0] + strf(" = $%04X", *a));
    return true;
}

bool GdbServer::monitor_time(const std::vector<std::string>& args) {
    if (!args.empty()) return false;
    const auto t = dbg_.time();
    // 64-bit values through std::to_string, not `%llu`: one format string that
    // MinGW's printf family reads the same way as glibc's.
    monitor_line("frame=" + std::to_string(t.frame) + " cycle=" + std::to_string(t.master_cycle) +
                 " tstates=" + std::to_string(t.tstates_total) + " vc=" +
                 std::to_string(t.vc_raw) + " hc=" + std::to_string(t.hc_raw));
    return true;
}

// `reset [soft|hard]` — CAP-CTL-12. Neither pauses nor resumes: paused stays
// paused, and no stop reply is owed (§4.3). `hard` runs the loop owner's cold
// boot before `reset()` returns (the reconstruct contract), or is refused with
// no driver registered — never a reset that lands late.
bool GdbServer::monitor_reset(const std::vector<std::string>& args) {
    const std::string kind = args.empty() ? std::string("soft") : lower(args[0]);
    if (args.size() > 1 || (kind != "soft" && kind != "hard")) return false;
    const bool   hard = kind == "hard";
    const Result r    = dbg_.reset(cid_, hard ? ResetKind::Hard : ResetKind::Soft);
    if (r == Result::RefusedUnavailable && hard) {
        monitor_line("hard reset not available over this connection");
        return false;
    }
    if (r != Result::Ok) {
        monitor_line(std::string(kind) + " reset refused: " + result_name(r));
        return false;
    }
    monitor_line(strf("%s reset: machine at PC=%04X, %s", kind.c_str(), dbg_.registers().PC,
                      dbg_.state().paused ? "still stopped" : "running"));
    return true;
}

// CAP-INS-17 — every client's subscriptions, transient ones hidden, with the
// owner (this client's marked; the backend publishes no client names).
bool GdbServer::monitor_bp(const std::vector<std::string>& args) {
    if (!args.empty()) return false;
    const auto subs = dbg_.subscriptions(false);
    if (subs.empty()) monitor_line("no breakpoints or watchpoints");
    for (const auto& s : subs) {
        std::string l = strf("#%u %s", s.id, kind_name(s.kind));
        if (s.kind == EventKind::Execute || s.kind == EventKind::Mem)
            l += s.filter.lo == s.filter.hi ? strf(" %04X", s.filter.lo)
                                            : strf(" %04X-%04X", s.filter.lo, s.filter.hi);
        if (s.kind == EventKind::Mem || s.kind == EventKind::Port)
            l += std::string(" ") + access_name(s.access);
        l += s.live ? "" : " (disabled)";
        l += strf(" owner %u", s.owner);
        if (s.owner == cid_) l += " (this client)";
        monitor_line(l);
    }
    return true;
}

}  // namespace gdb
}  // namespace remote
}  // namespace jnext
