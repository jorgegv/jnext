#include "remote/dzrp/dzrp_server.h"

#include <algorithm>
#include <cctype>

#include "core/log.h"
#include "version.h"

namespace jnext {
namespace remote {
namespace dzrp {

using jnext::dbg::Access;
using jnext::dbg::Action;
using jnext::dbg::CLIENT_NONE;
using jnext::dbg::ClientId;
using jnext::dbg::ClientInfo;
using jnext::dbg::ClientKind;
using jnext::dbg::EVENT_NONE;
using jnext::dbg::EventId;
using jnext::dbg::EventKind;
using jnext::dbg::MemSpace;
using jnext::dbg::PAGE_ANY;
using jnext::dbg::PausedInfo;
using jnext::dbg::PauseReason;
using jnext::dbg::RegId;
using jnext::dbg::Result;
using jnext::dbg::result_name;
using jnext::dbg::ServiceStep;
using jnext::dbg::Subscription;

namespace {

/// How much of the connection's input one read takes. Only the granularity:
/// the parser never asks for more than the rest of one frame.
constexpr std::size_t kReadChunk = 64 * 1024;

/// The longest client program name kept from `CMD_INIT` for the log and the
/// backend's client list.
constexpr std::size_t kMaxClientName = 64;

ServerConfig dzrp_server_config() {
    ServerConfig cfg;
    cfg.name = "dzrp";
    // DZRP's second connection is "accepted and immediately closed with a log
    // line" (design §2 row 1): T logs the refusal at warn; nothing is sent.
    cfg.busy_reply.clear();
    return cfg;
}

std::uint16_t le16(const std::vector<std::uint8_t>& p, std::size_t at) {
    return static_cast<std::uint16_t>(p[at] | (p[at + 1] << 8));
}

void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

/// `CMD_SET_REGISTER`'s register numbers (spec: "0=PC, 1=SP, 2=AF, 3=BC, 4=DE,
/// 5=HL, 6=IX, 7=IY, 8=AF', 9=BC', 10=DE', 11=HL', 13=IM, 14=F, 15=A, 16=C,
/// 17=B, 18=E, 19=D, 20=L, 21=H, 22=IXL, 23=IXH, 24=IYL, 25=IYH, 26=F', 27=A',
/// 28=C', 29=B', 30=E', 31=D', 32=L', 33=H', 34=R, 35=I") → the backend's
/// `RegId`. VERIFIED equal to DeZog's own `Z80_REG` enum (`z80registers.ts:14-25`,
/// 3.7.4 and upstream 3.8.0-rc7 alike), which is what DeZog sends. The one
/// difference: DeZog names 12 `IR` (I<<8 | R) where the spec says 12 is unused;
/// DeZog's register panes never offer IR for editing, so only the raw debug
/// console (`-dbg cmd_set_register 12 …`) can send it, and it is refused as the
/// spec's "unused" (design §2 row 4: unknown index → warn).
constexpr int kNoReg = -1;
constexpr int kRegMap[] = {
    static_cast<int>(RegId::PC),  static_cast<int>(RegId::SP),  static_cast<int>(RegId::AF),
    static_cast<int>(RegId::BC),  static_cast<int>(RegId::DE),  static_cast<int>(RegId::HL),
    static_cast<int>(RegId::IX),  static_cast<int>(RegId::IY),  static_cast<int>(RegId::AF2),
    static_cast<int>(RegId::BC2), static_cast<int>(RegId::DE2), static_cast<int>(RegId::HL2),
    kNoReg,                       static_cast<int>(RegId::IM),  static_cast<int>(RegId::F),
    static_cast<int>(RegId::A),   static_cast<int>(RegId::C),   static_cast<int>(RegId::B),
    static_cast<int>(RegId::E),   static_cast<int>(RegId::D),   static_cast<int>(RegId::L),
    static_cast<int>(RegId::H),   static_cast<int>(RegId::IXL), static_cast<int>(RegId::IXH),
    static_cast<int>(RegId::IYL), static_cast<int>(RegId::IYH), static_cast<int>(RegId::F2),
    static_cast<int>(RegId::A2),  static_cast<int>(RegId::C2),  static_cast<int>(RegId::B2),
    static_cast<int>(RegId::E2),  static_cast<int>(RegId::D2),  static_cast<int>(RegId::L2),
    static_cast<int>(RegId::H2),  static_cast<int>(RegId::R),   static_cast<int>(RegId::I),
};
constexpr std::size_t kRegMapSize = sizeof(kRegMap) / sizeof(kRegMap[0]);
static_assert(kRegMapSize == 36, "DZRP register numbers run 0..35");

/// Why a command the table does not serve is unsupported, where the design
/// says more than "unsupported" (§2 rows 13, 14, 22).
const char* unsupported_hint(std::uint8_t id) {
    switch (id) {
        case CMD_SET_BREAKPOINTS:
            return "CMD_SET_BREAKPOINTS: jnext is a normal-mode remote — use DeZog's "
                   "remoteType \"cspect\" or \"dzrp\", not \"zxnext\"";
        case CMD_RESTORE_MEM:
            return "CMD_RESTORE_MEM: jnext is a normal-mode remote — use DeZog's "
                   "remoteType \"cspect\" or \"dzrp\", not \"zxnext\"";
        case CMD_EXEC_ASM:
            return "CMD_EXEC_ASM: jnext has no scratch execution context to run it in";
        default:
            return "not served";
    }
}

/// A `bank+1` byte from the wire → the backend's page qualifier. 0 = none (a
/// 64K address). A ROM bank (0xFE/0xFF) cannot be a qualifier: the backend
/// names a ROM slot by its ROM page, not by the DZRP bank, and 3.8's ROM bank
/// 0xFF does not even fit the byte (it arrives as 0) — so a ROM breakpoint is
/// armed in every bank (§5.3). False for a bank that does not exist.
bool bank1_to_page(std::uint8_t bank1, std::uint16_t& page) {
    if (bank1 == 0) {
        page = PAGE_ANY;
        return true;
    }
    const std::uint8_t bank = static_cast<std::uint8_t>(bank1 - 1);
    if (bank <= 223) {
        page = bank;
        return true;
    }
    if (bank >= 0xFE) {
        page = PAGE_ANY;
        return true;
    }
    return false;
}

bool contains(const std::vector<EventId>& v, EventId id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

/// The string an `NTF_PAUSE` reason 255 carries for a stop that is not this
/// client's to name (design §3.3 "anything else").
std::string other_stop_text(const PauseReason& r) {
    switch (r.kind) {
        case PauseReason::Kind::User:       return "paused by another debugger client";
        case PauseReason::Kind::Breakpoint: return "breakpoint of another debugger client";
        case PauseReason::Kind::Watch:      return "watchpoint of another debugger client";
        case PauseReason::Kind::Step:       return "stepped by another debugger client";
        case PauseReason::Kind::RunTo:      return "run-to target of another debugger client";
        case PauseReason::Kind::Magic:      return "magic breakpoint";
        case PauseReason::Kind::Corrupt:    return "machine state corrupt";
        case PauseReason::Kind::Script:
            return r.text.empty() ? std::string("stopped by a debugger subscription") : r.text;
        case PauseReason::Kind::None:       break;
    }
    return "paused";
}

/// The client's program name as a log line may show it: printable ASCII only,
/// bounded. It is the client's claim, so it is never trusted to be either.
std::string printable_name(const std::vector<std::uint8_t>& p, std::size_t from) {
    std::string out;
    for (std::size_t i = from; i < p.size() && p[i] != 0 && out.size() < kMaxClientName; ++i)
        out.push_back(std::isprint(p[i]) ? static_cast<char>(p[i]) : '?');
    return out.empty() ? std::string("DZRP client") : out;
}

}  // namespace

// THE TABLE. `GET_SUPPORTED_COMMANDS` is computed from these rows and nothing
// else, so a row here is served AND advertised (unless `legacy`), and a
// command with no row is neither.
//
// `session` false: machine-free — answerable before CMD_INIT.
const DzrpServer::CommandDef DzrpServer::COMMANDS[] = {
    // id                                      name                                        min legacy session handler
    {CMD_INIT,                                "CMD_INIT",                                  0, false, false, &DzrpServer::cmd_init},
    {CMD_CLOSE,                               "CMD_CLOSE",                                 0, false, false, &DzrpServer::cmd_close},
    {CMD_GET_REGISTERS,                       "CMD_GET_REGISTERS",                         0, false, true,  &DzrpServer::cmd_get_registers},
    {CMD_SET_REGISTER,                        "CMD_SET_REGISTER",                          3, false, true,  &DzrpServer::cmd_set_register},
    {CMD_WRITE_BANK,                          "CMD_WRITE_BANK",                            0, true,  true,  &DzrpServer::cmd_write_bank},
    {CMD_CONTINUE,                            "CMD_CONTINUE",                              5, false, true,  &DzrpServer::cmd_continue},
    {CMD_PAUSE,                               "CMD_PAUSE",                                 0, false, true,  &DzrpServer::cmd_pause},
    {CMD_READ_MEM,                            "CMD_READ_MEM",                              5, false, true,  &DzrpServer::cmd_read_mem},
    {CMD_WRITE_MEM,                           "CMD_WRITE_MEM",                             3, false, true,  &DzrpServer::cmd_write_mem},
    {CMD_SET_SLOT,                            "CMD_SET_SLOT",                              0, false, true,  &DzrpServer::cmd_set_slot},
    {CMD_GET_TBBLUE_REG,                      "CMD_GET_TBBLUE_REG",                        1, false, true,  &DzrpServer::cmd_get_tbblue_reg},
    {CMD_SET_BORDER,                          "CMD_SET_BORDER",                            1, true,  true,  &DzrpServer::cmd_set_border},
    {CMD_LOOPBACK,                            "CMD_LOOPBACK",                              0, false, false, &DzrpServer::cmd_loopback},
    {CMD_GET_SPRITES_PALETTE,                 "CMD_GET_SPRITES_PALETTE",                   1, false, true,  &DzrpServer::cmd_get_sprites_palette},
    {CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL, "CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL",   0, false, true,  &DzrpServer::cmd_get_sprites_clip_window_and_control},
    {CMD_GET_SPRITES,                         "CMD_GET_SPRITES",                           2, false, true,  &DzrpServer::cmd_get_sprites},
    {CMD_GET_SPRITE_PATTERNS,                 "CMD_GET_SPRITE_PATTERNS",                   2, false, true,  &DzrpServer::cmd_get_sprite_patterns},
    {CMD_READ_PORT,                           "CMD_READ_PORT",                             2, false, true,  &DzrpServer::cmd_read_port},
    {CMD_WRITE_PORT,                          "CMD_WRITE_PORT",                            3, false, true,  &DzrpServer::cmd_write_port},
    {CMD_INTERRUPT_ON_OFF,                    "CMD_INTERRUPT_ON_OFF",                      1, false, true,  &DzrpServer::cmd_interrupt_on_off},
    {CMD_GET_SUPPORTED_COMMANDS,              "CMD_GET_SUPPORTED_COMMANDS",                0, false, false, &DzrpServer::cmd_get_supported_commands},
    {CMD_READ_BANK_MEM,                       "CMD_READ_BANK_MEM",                         5, false, true,  &DzrpServer::cmd_read_bank_mem},
    {CMD_WRITE_BANK_MEM,                      "CMD_WRITE_BANK_MEM",                        3, false, true,  &DzrpServer::cmd_write_bank_mem},
    {CMD_ENABLE_BREAK_ON_INTERRUPT,           "CMD_ENABLE_BREAK_ON_INTERRUPT",             1, false, true,  &DzrpServer::cmd_enable_break_on_interrupt},
    {CMD_ADD_BREAKPOINT,                      "CMD_ADD_BREAKPOINT",                        3, false, true,  &DzrpServer::cmd_add_breakpoint},
    {CMD_REMOVE_BREAKPOINT,                   "CMD_REMOVE_BREAKPOINT",                     2, false, true,  &DzrpServer::cmd_remove_breakpoint},
    {CMD_ADD_WATCHPOINT,                      "CMD_ADD_WATCHPOINT",                        0, false, true,  &DzrpServer::cmd_add_watchpoint},
    {CMD_REMOVE_WATCHPOINT,                   "CMD_REMOVE_WATCHPOINT",                     6, false, true,  &DzrpServer::cmd_remove_watchpoint},
    {CMD_READ_STATE,                          "CMD_READ_STATE",                            0, false, true,  &DzrpServer::cmd_read_state},
    {CMD_WRITE_STATE,                         "CMD_WRITE_STATE",                           0, false, true,  &DzrpServer::cmd_write_state},
};

const DzrpServer::CommandDef* DzrpServer::find_command(std::uint8_t id) {
    for (const CommandDef& d : COMMANDS)
        if (d.id == id) return &d;
    return nullptr;
}

std::vector<std::uint8_t> DzrpServer::supported_bitfield() {
    std::vector<std::uint8_t> bits;
    for (const CommandDef& d : COMMANDS) {
        if (d.legacy) continue;
        const std::size_t byte = d.id / 8;
        if (bits.size() <= byte) bits.resize(byte + 1, 0);
        bits[byte] = static_cast<std::uint8_t>(bits[byte] | (1u << (d.id % 8)));
    }
    return bits;
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
    // Nothing to do: DZRP attaches on CMD_INIT, not on connect (design §4.1),
    // so a connection that never says CMD_INIT has touched nothing. The parser
    // starts clean because every session's end (`on_disconnect`) resets it.
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

// The post-frame flush (REQ-dzrp-8): `pump()` calls this AFTER its drain and
// after the backend's `Paused` push, so a stop in this tick's frames reaches
// the client in this tick — and after every reply the drain wrote, so a
// CMD_PAUSE's response always precedes its notification (spec).
void DzrpServer::on_notify(Connection& c) {
    if (pending_pause_) {
        const PausedInfo info = *pending_pause_;
        pending_pause_.reset();
        const PauseReason& r = info.reason;

        // THE REASON (design §3.3), in this order. A temporary first (F8): a
        // stop that satisfies both a CMD_CONTINUE temporary and a user
        // breakpoint at the same address is reported 0, or DeZog evaluates the
        // user breakpoint's condition, finds it false and runs the step away.
        bool temp = false, bp = false, boi = false;
        const jnext::dbg::Hit* watch = nullptr;
        for (const jnext::dbg::Hit& h : info.matched) {
            temp = temp || contains(temps_, h.event_id);
            if (h.event_id == int_ack_) boi = true;
            for (const auto& kv : bps_) bp = bp || kv.second == h.event_id;
            for (const Watch& w : wps_)
                if (w.id == h.event_id && !watch) watch = &h;
        }
        if (temp)
            queue_pause_ntf(BREAK_NONE, info.pc, "");
        else if (r.kind == PauseReason::Kind::User && r.by == cid_)
            queue_pause_ntf(BREAK_MANUAL, info.pc, "");
        else if (bp)
            queue_pause_ntf(BREAK_BP, info.pc, "");
        else if (watch)
            queue_pause_ntf(jnext::dbg::has_write(watch->access) ? BREAK_WP_WRITE : BREAK_WP_READ,
                            watch->addr, "");
        else if (boi)
            queue_pause_ntf(BREAK_OTHER, info.pc, "Break on interrupt.");
        else
            queue_pause_ntf(BREAK_OTHER, info.pc, other_stop_text(r));
        continue_outstanding_ = false;
        pause_owed_           = false;
        drop_temporaries();
    }
    for (const auto& f : ntf_queue_) c.write(f.data(), f.size());
    ntf_queue_.clear();
}

// ---------------------------------------------------------------------------
// dbg::Listener — the backend's pushes (SES-02)
// ---------------------------------------------------------------------------

// A stop is recorded here and turned into an NTF_PAUSE in `on_notify()`, the
// Protocol callback, rather than here inside the backend's fan-out: building it
// ends with unsubscribing the leftover temporaries, which is not something to
// do from inside a listener callback.
//
// EXACTLY ONCE: only while this client is owed one — a CMD_CONTINUE outstanding
// (one per CONTINUE) or a CMD_PAUSE that stopped a running machine. A second
// stop edge with nothing owed (a GUI pause after our breakpoint already stopped
// it) is dropped; DeZog would ignore it anyway (buf:228).
void DzrpServer::on_paused(const PausedInfo& info) {
    if (!(continue_outstanding_ || pause_owed_)) return;
    pending_pause_ = info;
}

// DZRP has no "resumed" notification (§4.4), no reset notification (§3.3: a
// hard reset never pauses, so nothing is sent — the outstanding CONTINUE stays
// outstanding and the next stop answers it), and no NTF_LOG in production
// (§2). The other pushes carry nothing this protocol can say.
void DzrpServer::on_resumed(ClientId /*by*/) {}
void DzrpServer::on_reset(jnext::dbg::ResetKind /*kind*/) {}
void DzrpServer::on_frame_ended(std::uint32_t /*frame*/) {}
void DzrpServer::on_subscriptions_changed(jnext::dbg::EventKindMask /*kinds*/) {}
void DzrpServer::on_exit_requested(int /*code*/) {}
void DzrpServer::on_log(jnext::dbg::LogLevel /*level*/, const std::string& /*text*/) {}

void DzrpServer::on_disconnect() {
    // A dropped socket is a CMD_CLOSE (design §2 row 2, §4.1): SES-01's detach
    // releases this client's own pause when no other client remains (else it
    // passes to one, GH #280 N1), so a crashed DeZog cannot leave the machine
    // hung.
    end_session();
    parser_.reset();
    conn_ = nullptr;
}

void DzrpServer::end_session() {
    if (cid_ == CLIENT_NONE) return;
    // The backend's detach removes this client's subscriptions (breakpoints,
    // watchpoints, temporaries, break-on-interrupt) and its bookmarks; the
    // adapter forgets its names for them.
    dbg_.detach(cid_);
    cid_ = CLIENT_NONE;
    bps_.clear();
    next_bp_id_ = 1;
    temps_.clear();
    int_ack_              = EVENT_NONE;
    continue_outstanding_ = false;
    pause_owed_           = false;
    pending_pause_.reset();
    ntf_queue_.clear();
    wps_.clear();
    last_save_refused_mid_frame_ = false;
}

std::uint8_t DzrpServer::bank_byte(std::uint16_t addr) const {
    const int  slot = addr >> 13;
    const auto si   = dbg_.mmu_slots()[static_cast<std::size_t>(slot)];
    if (si.is_rom) return slot == 0 ? 0xFF : 0x00;
    return static_cast<std::uint8_t>(si.nr_page + 1);
}

void DzrpServer::queue_pause_ntf(std::uint8_t reason, std::uint16_t addr,
                                 const std::string& text) {
    // Frame: len, seq 0, NTF_PAUSE, reason, addr u16, bank+1, string\0 — the
    // string at least one byte (spec).
    std::vector<std::uint8_t> p = {NTF_PAUSE, reason, static_cast<std::uint8_t>(addr & 0xFF),
                                   static_cast<std::uint8_t>(addr >> 8), bank_byte(addr)};
    p.insert(p.end(), text.begin(), text.end());
    p.push_back(0);
    ntf_queue_.push_back(encode_response(0, p));
}

void DzrpServer::drop_temporaries() {
    // The backend removes transients at the stop it causes; a stop it did not
    // cause (a legacy breakpoint) leaves them armed, so what is left of this
    // CONTINUE's are removed here — "removed automatically after the command
    // is finished" (spec). `unsubscribe` of one already gone is benign.
    for (EventId id : temps_) dbg_.unsubscribe(cid_, id);
    temps_.clear();
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void DzrpServer::protocol_error(Connection& c, const std::string& why) {
    Log::debugger()->warn("dzrp: protocol error from {}: {} — closing the connection",
                          c.peer(), why);
    // Nothing more is read from a closing connection, and T follows with
    // `on_disconnect()`, which resets the parser.
    c.close();
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
        Log::debugger()->warn("dzrp: unsupported DZRP command {} ({})", cmd.id,
                              unsupported_hint(cmd.id));
        reply(cmd.seq);
        return;
    }
    if (def->session && cid_ == CLIENT_NONE) {
        Log::debugger()->warn("dzrp: {} before CMD_INIT — refused (no session to attribute it to)",
                              def->name);
        reply(cmd.seq);
        return;
    }
    if (cmd.payload.size() < def->min_len) {
        Log::debugger()->warn("dzrp: malformed {}: payload is {} bytes, needs at least {}",
                              def->name, cmd.payload.size(), def->min_len);
        reply(cmd.seq);
        return;
    }
    if (def->legacy && client_version_[0] == 2 && client_version_[1] >= 2) {
        Log::debugger()->debug("dzrp: {} was removed in DZRP 2.2.0 — served as legacy to a "
                               "{}.{}.{} client",
                               def->name, client_version_[0], client_version_[1],
                               client_version_[2]);
    }
    (this->*def->run)(cmd);
}

// ---------------------------------------------------------------------------
// Session — CMD_INIT, CMD_CLOSE
// ---------------------------------------------------------------------------

// CMD_INIT (1). Attach, PAUSE (design F4: DeZog assumes a stopped remote after
// CMD_INIT and sends nothing that would stop it), record the client's version,
// answer 2.2.0 / ZXNEXT / "jnext v<version>".
void DzrpServer::cmd_init(const Command& cmd) {
    std::vector<std::uint8_t> out = {0, DZRP_VERSION_MAJOR, DZRP_VERSION_MINOR,
                                     DZRP_VERSION_PATCH, DZRP_MACHINE_ZXNEXT};
    const std::string name = std::string("jnext v") + JNEXT_VERSION_STRING;
    auto finish = [&](std::uint8_t err) {
        out[0] = err;
        out.insert(out.end(), name.begin(), name.end());
        out.push_back(0);
        reply(cmd.seq, out);
    };

    if (cmd.payload.size() < 3) {
        // The reply carries an error field, so the fault goes there: 1 is the
        // spec's "general (unknown) error". Nothing is attached or paused.
        Log::debugger()->warn("dzrp: malformed CMD_INIT: payload is {} bytes, needs the "
                              "3-byte version",
                              cmd.payload.size());
        finish(1);
        return;
    }

    const std::string client = printable_name(cmd.payload, 3);
    if (cid_ == CLIENT_NONE) {
        // One attach per session: a repeated CMD_INIT (conformance C6 sends
        // five) renegotiates on the client it already has.
        const auto a = dbg_.attach(ClientInfo{client, ClientKind::Dzrp});
        if (!a) {
            Log::debugger()->warn("dzrp: CMD_INIT from {} — attach refused: {}", client,
                                  result_name(a.status));
            finish(1);
            return;
        }
        cid_ = a.value;
        // SES-02: this client's stops arrive as `on_paused()` pushes.
        dbg_.set_listener(cid_, this);
    }
    client_version_[0] = cmd.payload[0];
    client_version_[1] = cmd.payload[1];
    client_version_[2] = cmd.payload[2];

    // PAUSE ONLY A RUNNING MACHINE. `pause()` on a paused machine re-attributes
    // the stop to the caller ("last verb wins"), and then this client's detach
    // would release a pause that was never its own — a GUI pause, or an
    // unowned `Magic` stop that no client's departure may clear. So an already
    // paused machine is left exactly as it is (design §4.1).
    if (!dbg_.state().paused) dbg_.pause(cid_);

    Log::debugger()->info("dzrp: CMD_INIT from \"{}\" (DZRP {}.{}.{}) — client {}; answering "
                          "DZRP {}.{}.{}",
                          client, client_version_[0], client_version_[1], client_version_[2],
                          cid_, DZRP_VERSION_MAJOR, DZRP_VERSION_MINOR, DZRP_VERSION_PATCH);
    finish(0);
}

// CMD_CLOSE (2). Detach — which releases this client's own pause, so DeZog's
// graceful close (it pauses, then closes) leaves the machine running — and
// answer. The CONNECTION stays: DZRP is silent on the transport's fate, and a
// client may say CMD_INIT again on it (conformance C15).
void DzrpServer::cmd_close(const Command& cmd) {
    end_session();
    reply(cmd.seq);
}

// ---------------------------------------------------------------------------
// Registers
// ---------------------------------------------------------------------------

// CMD_GET_REGISTERS (3): PC SP AF BC DE HL IX IY AF' BC' DE' HL' (LE words),
// R, I, IM, reserved, Nslots = 8, then each slot's NR 0x50-0x57 value as
// written — 0xFF for ROM (design §2 row 3).
void DzrpServer::cmd_get_registers(const Command& cmd) {
    const Z80Registers r = dbg_.registers();
    std::vector<std::uint8_t> out;
    out.reserve(37);
    for (std::uint16_t w : {r.PC, r.SP, r.AF, r.BC, r.DE, r.HL, r.IX, r.IY, r.AF2, r.BC2,
                            r.DE2, r.HL2})
        put16(out, w);
    out.push_back(r.R);
    out.push_back(r.I);
    out.push_back(r.IM);
    out.push_back(0);  // reserved
    const auto slots = dbg_.mmu_slots();
    out.push_back(static_cast<std::uint8_t>(slots.size()));
    for (const auto& s : slots) out.push_back(s.nr_page);
    reply(cmd.seq, out);
}

// CMD_SET_REGISTER (4): index + u16; an 8-bit target takes the low byte (the
// backend's `set_register` does that for an 8-bit `RegId`).
void DzrpServer::cmd_set_register(const Command& cmd) {
    const std::uint8_t  index = cmd.payload[0];
    const std::uint16_t value = le16(cmd.payload, 1);
    const int           reg   = index < kRegMapSize ? kRegMap[index] : kNoReg;
    if (reg == kNoReg) {
        Log::debugger()->warn("dzrp: CMD_SET_REGISTER with unknown register number {} — ignored",
                              index);
    } else if (const Result r = dbg_.set_register(cid_, static_cast<RegId>(reg), value);
               r != Result::Ok) {
        Log::debugger()->warn("dzrp: CMD_SET_REGISTER {} refused: {}", index, result_name(r));
    }
    reply(cmd.seq);
}

// CMD_INTERRUPT_ON_OFF (23): IFF1 = IFF2 = flag.
void DzrpServer::cmd_interrupt_on_off(const Command& cmd) {
    const std::uint16_t on = cmd.payload[0] != 0 ? 1 : 0;
    Result r = dbg_.set_register(cid_, RegId::IFF1, on);
    if (r == Result::Ok) r = dbg_.set_register(cid_, RegId::IFF2, on);
    if (r != Result::Ok)
        Log::debugger()->warn("dzrp: CMD_INTERRUPT_ON_OFF refused: {}", result_name(r));
    reply(cmd.seq);
}

// ---------------------------------------------------------------------------
// Memory — the CPU view
// ---------------------------------------------------------------------------

// CMD_READ_MEM (8): reserved(1), addr u16, size u16. The CPU view through the
// live mapping, SIDE-EFFECT FREE: `peek(Cpu)` is `Mmu::peek`, which does not
// latch the +3 floating-bus byte as `Mmu::read` would (backend F1).
void DzrpServer::cmd_read_mem(const Command& cmd) {
    const std::uint16_t addr = le16(cmd.payload, 1);
    const std::uint16_t size = le16(cmd.payload, 3);
    std::vector<std::uint8_t> out(size);
    if (size > 0) dbg_.peek(MemSpace::cpu(), addr, size, out.data());
    reply(cmd.seq, out);
}

// CMD_WRITE_MEM (9): reserved(1), addr u16, data. As the CPU would write it:
// ROM-mapped bytes are dropped, overlays (Layer-2 write-over, DivMMC and
// Multiface RAM) take theirs. DZRP has no error field here, so what did not
// land is said in the log — counted by the backend (`poke(Cpu)` returns the
// bytes that landed, GH #281 F1). This used to read the range back and count
// differences, which also counted a Layer 2 write-over byte (it lands in the
// Layer 2 page, and reads come from the normal map) as not written.
void DzrpServer::cmd_write_mem(const Command& cmd) {
    const std::uint16_t addr = le16(cmd.payload, 1);
    const std::size_t   n    = cmd.payload.size() - 3;
    if (n > 0) {
        const std::uint8_t* data = cmd.payload.data() + 3;
        const auto          w    = dbg_.poke(cid_, MemSpace::cpu(), addr, n, data);
        if (w.status == Result::RefusedReadOnly) {
            Log::debugger()->debug("dzrp: CMD_WRITE_MEM at 0x{:04X}: {} of {} bytes did not "
                                   "land (read-only memory)",
                                   addr, n - w.value, n);
        } else if (w.status != Result::Ok) {
            Log::debugger()->warn("dzrp: CMD_WRITE_MEM of {} bytes at 0x{:04X} refused: {}", n,
                                  addr, result_name(w.status));
        }
    }
    reply(cmd.seq);
}

// ---------------------------------------------------------------------------
// Banks — DZRP bank N = MMU page N as NR 0x50-0x57 spell it (design §5.2)
// ---------------------------------------------------------------------------

// CMD_WRITE_BANK (5), LEGACY — removed in 2.2.0, served for 2.0/2.1 clients
// (DeZog 3.7.4 loads .sna/.z80/.nex with it). bank(1) + 8192 bytes; the reply
// carries an error byte and a string, which DeZog throws — so the refusal
// reaches the user (design §2 row 5).
void DzrpServer::cmd_write_bank(const Command& cmd) {
    auto answer = [&](std::uint8_t err, const std::string& why) {
        std::vector<std::uint8_t> out = {err};
        out.insert(out.end(), why.begin(), why.end());
        out.push_back(0);
        reply(cmd.seq, out);
    };
    if (!cmd.payload.empty() && cmd.payload[0] > 223) {
        Log::debugger()->warn("dzrp: CMD_WRITE_BANK to bank {} refused: bank out of range",
                              cmd.payload[0]);
        answer(1, "bank out of range");
        return;
    }
    if (cmd.payload.size() != 1 + DZRP_BANK_BYTES) {
        Log::debugger()->warn("dzrp: CMD_WRITE_BANK refused: {} data bytes, must be 8192",
                              cmd.payload.empty() ? 0 : cmd.payload.size() - 1);
        answer(1, "length must be 8192");
        return;
    }
    const auto w = dbg_.poke(cid_, MemSpace::page(cmd.payload[0]), 0, DZRP_BANK_BYTES,
                             cmd.payload.data() + 1);
    if (w.status != Result::Ok) {
        Log::debugger()->warn("dzrp: CMD_WRITE_BANK to bank {} refused: {}", cmd.payload[0],
                              result_name(w.status));
        answer(1, result_name(w.status));
        return;
    }
    answer(0, "");
}

// CMD_READ_BANK_MEM (25): bank, offset u16, size u16. Banks 0..223 read the
// PHYSICAL page, regardless of any overlay; only bytes inside the 8 KB page
// are served. Bank 0xFF is DeZog 3.8's 16 KB ROM bank: offset 0x0000-0x1FFF
// is slot 0's ROM half, 0x2000-0x3FFF slot 1's. Each half is read through the
// space the backend names — the adapter composes no ROM index of its own
// (REQ-dzrp-12): `SlotInfo.space` + `space_offset` while the slot IS ROM (so
// the bytes are the ones the CPU sees there), and `rom_select()` — the image
// legacy paging selects — while RAM is paged in (design §5.2).
void DzrpServer::cmd_read_bank_mem(const Command& cmd) {
    const std::uint8_t  bank   = cmd.payload[0];
    const std::uint16_t offset = le16(cmd.payload, 1);
    const std::uint16_t size   = le16(cmd.payload, 3);
    std::vector<std::uint8_t> out(size);
    std::size_t               served = 0;

    if (bank == DZRP_ROM_BANK) {
        const auto slots = dbg_.mmu_slots();
        while (served < size) {
            const std::uint32_t off = static_cast<std::uint32_t>(offset) + served;
            if (off >= 2 * DZRP_BANK_BYTES) break;
            const std::uint32_t half    = off / DZRP_BANK_BYTES;
            const auto&         si      = slots[half];
            const MemSpace      space   = si.is_rom ? si.space : dbg_.rom_select();
            const std::uint32_t base    = si.is_rom ? si.space_offset : half * DZRP_BANK_BYTES;
            const std::uint32_t in_half = off % DZRP_BANK_BYTES;
            const std::size_t   chunk =
                std::min<std::size_t>(size - served, DZRP_BANK_BYTES - in_half);
            const auto got = dbg_.peek(space, base + in_half, chunk, out.data() + served);
            served += got.value;
            if (got.value < chunk) break;
        }
    } else if (bank <= 223) {
        served = dbg_.peek(MemSpace::page(bank), offset, size, out.data()).value;
    }

    if (size > 0 && served == 0) {
        Log::debugger()->warn("dzrp: CMD_READ_BANK_MEM bank {} offset 0x{:04X} size {}: nothing "
                              "to serve there — empty reply",
                              bank, offset, size);
    }
    out.resize(served);
    reply(cmd.seq, out);
}

// CMD_WRITE_BANK_MEM (26): bank, offset u16, data. Banks 0..223, bounded to the
// page. A ROM bank (0xFE/0xFF) writes nothing — the command has no error field,
// so that is said in the log.
void DzrpServer::cmd_write_bank_mem(const Command& cmd) {
    const std::uint8_t  bank   = cmd.payload[0];
    const std::uint16_t offset = le16(cmd.payload, 1);
    const std::size_t   n      = cmd.payload.size() - 3;
    if (n > 0) {
        if (bank > 223) {
            Log::debugger()->warn("dzrp: CMD_WRITE_BANK_MEM to bank {} refused: {} — nothing "
                                  "written",
                                  bank, bank >= 0xFE ? "ROM is read-only" : "no such bank");
        } else {
            const auto w =
                dbg_.poke(cid_, MemSpace::page(bank), offset, n, cmd.payload.data() + 3);
            if (w.value < n)
                Log::debugger()->warn("dzrp: CMD_WRITE_BANK_MEM bank {} offset 0x{:04X}: {} of "
                                      "{} bytes written ({})",
                                      bank, offset, w.value, n,
                                      w.status == Result::RefusedUnavailable
                                          ? "the rest lies past the 8 KB page"
                                          : result_name(w.status));
        }
    }
    reply(cmd.seq);
}

// CMD_SET_SLOT (10): slot, bank. The error byte is 1 for a slot past 7 or a
// bank in 224..0xFD; 0xFE on slot 0 is the pre-2.8 DeZog spelling of ROM and is
// taken as 0xFF (spec). The write is what the guest's own `NEXTREG 0x50+slot`
// would do — `set_mmu_slot()`, which runs the NR 0x50-0x57 write handler.
void DzrpServer::cmd_set_slot(const Command& cmd) {
    std::uint8_t err = 1;
    if (cmd.payload.size() < 2) {
        Log::debugger()->warn("dzrp: malformed CMD_SET_SLOT: payload is {} bytes, needs 2",
                              cmd.payload.size());
    } else {
        const std::uint8_t slot = cmd.payload[0];
        std::uint8_t       bank = cmd.payload[1];
        if (slot > 7 || (bank >= 224 && bank <= 0xFD)) {
            Log::debugger()->warn("dzrp: CMD_SET_SLOT {} to bank {} refused: {}", slot, bank,
                                  slot > 7 ? "no such slot" : "no such bank");
        } else {
            if (slot == 0 && bank == 0xFE) bank = 0xFF;
            const Result r = dbg_.set_mmu_slot(cid_, slot, bank);
            if (r == Result::Ok)
                err = 0;
            else
                Log::debugger()->warn("dzrp: CMD_SET_SLOT {} to bank {} refused: {}", slot, bank,
                                      result_name(r));
        }
    }
    reply(cmd.seq, {err});
}

// ---------------------------------------------------------------------------
// NextREGs, border, ports
// ---------------------------------------------------------------------------

// CMD_GET_TBBLUE_REG (11): through the register's READ path, never a
// destructive one (`nextreg_peek`: no read handler side effects, no phantom
// trace lines, the 0x243B selection untouched).
void DzrpServer::cmd_get_tbblue_reg(const Command& cmd) {
    reply(cmd.seq, {dbg_.nextreg_peek(cmd.payload[0])});
}

// CMD_SET_BORDER (12), LEGACY — removed in 2.2.0 ("use CMD_WRITE_PORT").
// Bits 2:0 (the backend keeps those), straight to the ULA (`set_border`,
// CAP-INS-18): no port traffic, so EAR/MIC are not driven.
void DzrpServer::cmd_set_border(const Command& cmd) {
    if (const Result r = dbg_.set_border(cid_, cmd.payload[0]); r != Result::Ok)
        Log::debugger()->warn("dzrp: CMD_SET_BORDER refused: {}", result_name(r));
    reply(cmd.seq);
}

// CMD_READ_PORT (20): a real IN — PERTURBING BY NATURE (it runs the port's
// read handlers). The reply is the value alone, with no error field, so a
// refusal is a seq-only reply plus a warn line: short, never a made-up value.
void DzrpServer::cmd_read_port(const Command& cmd) {
    const std::uint16_t port = le16(cmd.payload, 0);
    const auto          v    = dbg_.port_in(cid_, port);
    if (!v) {
        Log::debugger()->warn("dzrp: CMD_READ_PORT 0x{:04X} refused: {}", port,
                              result_name(v.status));
        reply(cmd.seq);
        return;
    }
    reply(cmd.seq, {v.value});
}

// CMD_WRITE_PORT (21): a real OUT. DeZog 3.8 sets the border on a load with
// `WRITE_PORT(0xFE, colour)`, which also drives EAR/MIC low — DeZog's choice.
void DzrpServer::cmd_write_port(const Command& cmd) {
    const std::uint16_t port = le16(cmd.payload, 0);
    if (const Result r = dbg_.port_out(cid_, port, cmd.payload[2]); r != Result::Ok)
        Log::debugger()->warn("dzrp: CMD_WRITE_PORT 0x{:04X} refused: {}", port, result_name(r));
    reply(cmd.seq);
}

// ---------------------------------------------------------------------------
// Machine-free commands
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

// CMD_GET_SUPPORTED_COMMANDS (24) — "MUST be supported by any remote that
// supports DZRP >= 2.2.0". DeZog 3.8 sends it right after CMD_INIT and turns
// every clear bit into a client-side thrower.
void DzrpServer::cmd_get_supported_commands(const Command& cmd) {
    reply(cmd.seq, supported_bitfield());
}

// ---------------------------------------------------------------------------
// WP-3 — continue, pause, breakpoints, break on interrupt (design §3)
// ---------------------------------------------------------------------------

// CMD_CONTINUE (6): bp1en, bp1 u16, bp2en, bp2 u16 [, alt, 4 bytes] — DeZog
// sends 11 bytes, the CSpect reference reads 5 (F5). Each enabled address
// becomes one TRANSIENT `Execute` subscription (hidden from every user list,
// exempt from the master switch, removed at the next stop — REQ-dzrp-3); then
// `run()`, whose GH #221 step-off arm is what makes a CONTINUE from a
// breakpoint not re-hit it. The reply goes out BEFORE anything runs: the
// machine only runs in the loop owner's next frames (§4.2).
//
// DeZog's division of labour, binding (§0 item 2): the temporaries ARE its
// Step Into / Over / Out — it computed them — so the adapter never calls a
// backend step verb.
void DzrpServer::cmd_continue(const Command& cmd) {
    const std::vector<std::uint8_t>& p = cmd.payload;
    if (p.size() > 6 && p[6] != 0) {
        // "At the moment there are no plans to implement 'step-over' or
        // 'step-out' alternate commands. Only 0 is implemented." (spec) — and
        // "the remote MIGHT execute the alternate command": treated as 0.
        Log::debugger()->info("dzrp: CMD_CONTINUE alternate command {} is not implemented — "
                              "running with the breakpoints instead",
                              p[6]);
    }
    for (std::size_t at : {std::size_t{0}, std::size_t{3}}) {
        if (p[at] == 0) continue;
        Subscription sub;
        sub.kind      = EventKind::Execute;
        sub.filter.lo = sub.filter.hi = le16(p, at + 1);
        sub.transient = true;
        sub.action    = Action::Stop;
        const auto s = dbg_.subscribe(cid_, sub);
        if (s)
            temps_.push_back(s.value);
        else
            Log::debugger()->warn("dzrp: CMD_CONTINUE temporary breakpoint at 0x{:04X} refused: {}",
                                  sub.filter.lo, result_name(s.status));
    }
    reply(cmd.seq);
    const Result r = dbg_.run(cid_);
    if (r != Result::Ok) {
        // The reply cannot carry a refusal, so the refusal is the stop DeZog
        // is waiting for: NTF_PAUSE 255 with the reason, shown as the break
        // reason, and the machine stays paused — the honest outcome (§3.2).
        std::string why = std::string("resume refused: ") + result_name(r);
        if (const auto inc = dbg_.resume_blocked_by_corruption())
            why += " (" + inc->subsystem + ")";
        Log::debugger()->warn("dzrp: CMD_CONTINUE {}", why);
        drop_temporaries();
        queue_pause_ntf(BREAK_OTHER, dbg_.registers().PC, why);
        return;
    }
    continue_outstanding_ = true;
}

// CMD_PAUSE (7): reply; and ONE NTF_PAUSE reason 1, after the reply, ONLY if
// this stopped a running machine — "if for some reason the program is not
// running when received, nothing happens" (spec). Called from `pump()`, the
// pause lands at a frame boundary.
void DzrpServer::cmd_pause(const Command& cmd) {
    reply(cmd.seq);
    if (dbg_.state().paused) return;
    if (const Result r = dbg_.pause(cid_); r != Result::Ok) {
        Log::debugger()->warn("dzrp: CMD_PAUSE refused: {}", result_name(r));
        return;
    }
    pause_owed_ = true;
}

// CMD_ENABLE_BREAK_ON_INTERRUPT (39): 1 → an `IntAck` subscription that stops
// (the accepted-maskable-interrupt seam; NMI is not an "interrupt" here), 0 →
// removed. The stop is NTF_PAUSE 255 at the handler's entry, "Break on
// interrupt." — DeZog's own text for the zsim-only break (§3.3).
void DzrpServer::cmd_enable_break_on_interrupt(const Command& cmd) {
    if (cmd.payload[0] != 0 && int_ack_ == EVENT_NONE) {
        Subscription sub;
        sub.kind   = EventKind::IntAck;
        sub.action = Action::Stop;
        const auto s = dbg_.subscribe(cid_, sub);
        if (s)
            int_ack_ = s.value;
        else
            Log::debugger()->warn("dzrp: CMD_ENABLE_BREAK_ON_INTERRUPT refused: {}",
                                  result_name(s.status));
    } else if (cmd.payload[0] == 0 && int_ack_ != EVENT_NONE) {
        dbg_.unsubscribe(cid_, int_ack_);
        int_ack_ = EVENT_NONE;
    }
    reply(cmd.seq);
}

// CMD_ADD_BREAKPOINT (40): addr u16, bank+1, condition\0. One `Execute[a,a]`
// subscription that stops, owned by this client (listed read-only in the GUI),
// with NO condition: DeZog evaluates conditions itself and a conditional
// breakpoint is an unconditional pause to the remote (§0 item 2) — the string
// is ignored. Bank byte ≠ 0 → the `page` qualifier (REQ-dzrp-7): it fires only
// with that page at the PC's slot. Reply: the id, 1..65535, never reused in a
// session; 0 = refused, which DeZog shows as an unverified breakpoint.
void DzrpServer::cmd_add_breakpoint(const Command& cmd) {
    const std::uint16_t addr  = le16(cmd.payload, 0);
    const std::uint8_t  bank1 = cmd.payload[2];
    std::uint16_t       id    = 0;
    std::uint16_t       page  = PAGE_ANY;
    if (!bank1_to_page(bank1, page)) {
        Log::debugger()->warn("dzrp: CMD_ADD_BREAKPOINT at 0x{:04X} refused: no bank {}", addr,
                              bank1 - 1);
    } else if (next_bp_id_ > 0xFFFF) {
        Log::debugger()->warn("dzrp: CMD_ADD_BREAKPOINT at 0x{:04X} refused: all 65535 ids of "
                              "this session are used",
                              addr);
    } else {
        Subscription sub;
        sub.kind        = EventKind::Execute;
        sub.filter.lo   = sub.filter.hi = addr;
        sub.filter.page = page;
        sub.action      = Action::Stop;
        const auto s = dbg_.subscribe(cid_, sub);
        if (s) {
            id       = static_cast<std::uint16_t>(next_bp_id_++);
            bps_[id] = s.value;
        } else {
            Log::debugger()->warn("dzrp: CMD_ADD_BREAKPOINT at 0x{:04X} refused: {}", addr,
                                  result_name(s.status));
        }
    }
    reply(cmd.seq, {static_cast<std::uint8_t>(id & 0xFF), static_cast<std::uint8_t>(id >> 8)});
}

// CMD_REMOVE_BREAKPOINT (41): by id. An id this session did not issue (or
// already removed) is a seq-only reply and a warn line.
void DzrpServer::cmd_remove_breakpoint(const Command& cmd) {
    const std::uint16_t id = le16(cmd.payload, 0);
    const auto          it = bps_.find(id);
    if (it == bps_.end()) {
        Log::debugger()->warn("dzrp: CMD_REMOVE_BREAKPOINT: no breakpoint with id {}", id);
    } else {
        dbg_.unsubscribe(cid_, it->second);
        bps_.erase(it);
    }
    reply(cmd.seq);
}

// ---------------------------------------------------------------------------
// WP-4 — sprites (16-19), watchpoints (42/43), state bookmarks (50/51)
// ---------------------------------------------------------------------------

// CMD_GET_SPRITES_PALETTE (16): palette 0/1 → 256 entries, each LE
// `RRRGGGBB, 0000000B` — the 9-bit RGB333 value split as the spec says.
void DzrpServer::cmd_get_sprites_palette(const Command& cmd) {
    const int bank = cmd.payload[0];
    if (bank > 1) {
        Log::debugger()->warn("dzrp: CMD_GET_SPRITES_PALETTE: no sprite palette {}", bank);
        reply(cmd.seq);
        return;
    }
    std::vector<std::uint8_t> out;
    out.reserve(512);
    for (int i = 0; i < 256; ++i) {
        const std::uint16_t c =
            dbg_.sprite_palette_rgb333(bank, static_cast<std::uint8_t>(i)).value;
        out.push_back(static_cast<std::uint8_t>(c >> 1));
        out.push_back(static_cast<std::uint8_t>(c & 1));
    }
    reply(cmd.seq, out);
}

// CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL (17): x1, x2, y1, y2 from the live
// sprite clip window, then NR 0x15 through its read path.
void DzrpServer::cmd_get_sprites_clip_window_and_control(const Command& cmd) {
    const auto w = dbg_.sprite_clip();
    reply(cmd.seq, {w.x1, w.x2, w.y1, w.y2, dbg_.nextreg_peek(0x15)});
}

// CMD_GET_SPRITES (18): index, count → 5 raw attribute bytes per sprite, as
// the engine holds them (the decoded `SpriteInfo` is lossy). A range past
// sprite 127 is clamped, and the reply is the clamped length.
void DzrpServer::cmd_get_sprites(const Command& cmd) {
    const std::size_t index = cmd.payload[0];
    std::size_t       count = cmd.payload[1];
    if (index + count > jnext::dbg::SPRITE_COUNT) {
        const std::size_t clamped = index >= jnext::dbg::SPRITE_COUNT
                                        ? 0
                                        : jnext::dbg::SPRITE_COUNT - index;
        Log::debugger()->warn("dzrp: CMD_GET_SPRITES {} from {} runs past sprite 127 — "
                              "clamped to {}",
                              count, index, clamped);
        count = clamped;
    }
    std::vector<std::uint8_t> out;
    out.reserve(count * jnext::dbg::SPRITE_ATTR_BYTES);
    for (std::size_t i = 0; i < count; ++i) {
        const auto a = dbg_.sprite_attr_raw(static_cast<std::uint8_t>(index + i));
        out.insert(out.end(), a.value.begin(), a.value.end());
    }
    reply(cmd.seq, out);
}

// CMD_GET_SPRITE_PATTERNS (19): index, count of 256-byte patterns — two bytes
// from DeZog, two LE words by the spec (F6): both are accepted. Index 0..63,
// count clamped to what is left of the 16 KB pattern RAM.
void DzrpServer::cmd_get_sprite_patterns(const Command& cmd) {
    const bool        words = cmd.payload.size() >= 4;
    const std::size_t index = words ? le16(cmd.payload, 0) : cmd.payload[0];
    std::size_t       count = words ? le16(cmd.payload, 2) : cmd.payload[1];
    constexpr std::size_t kPatterns = jnext::dbg::PATTERN_RAM_BYTES / 256;
    if (index + count > kPatterns) {
        const std::size_t clamped = index >= kPatterns ? 0 : kPatterns - index;
        Log::debugger()->warn("dzrp: CMD_GET_SPRITE_PATTERNS {} from {} runs past pattern 63 — "
                              "clamped to {}",
                              count, index, clamped);
        count = clamped;
    }
    const auto ram = dbg_.pattern_ram();
    std::vector<std::uint8_t> out;
    if (count > 0)
        out.assign(ram.data + index * 256, ram.data + (index + count) * 256);
    reply(cmd.seq, out);
}

// CMD_ADD_WATCHPOINT (42): addr u16, bank+1, size u16, access (bit 0 read,
// bit 1 write). One `Mem[addr, addr+size-1]` subscription that stops; a bank
// byte ≠ 0 is the `page` qualifier (REQ-dzrp-11). Reported with the bank, never
// filtered, never auto-continued (§5.4). Reply: error byte, 1 = refused (size
// 0, no access bit, a range past 0xFFFF — DeZog ignores wrap-around too — or a
// bank that does not exist).
void DzrpServer::cmd_add_watchpoint(const Command& cmd) {
    std::uint8_t err = 1;
    if (cmd.payload.size() < 6) {
        Log::debugger()->warn("dzrp: malformed CMD_ADD_WATCHPOINT: payload is {} bytes, needs 6",
                              cmd.payload.size());
    } else {
        const std::uint16_t addr   = le16(cmd.payload, 0);
        const std::uint8_t  bank1  = cmd.payload[2];
        const std::uint16_t size   = le16(cmd.payload, 3);
        const std::uint8_t  access = cmd.payload[5] & 0x03;
        std::uint16_t       page   = PAGE_ANY;
        const char*         why    = nullptr;
        if (size == 0)
            why = "size 0";
        else if (access == 0)
            why = "neither read nor write";
        else if (std::uint32_t{addr} + size - 1 > 0xFFFF)
            why = "the range runs past 0xFFFF";
        else if (!bank1_to_page(bank1, page))
            why = "no such bank";
        if (why) {
            Log::debugger()->warn("dzrp: CMD_ADD_WATCHPOINT at 0x{:04X} size {} refused: {}",
                                  addr, size, why);
        } else {
            Subscription sub;
            sub.kind        = EventKind::Mem;
            sub.filter.lo   = addr;
            sub.filter.hi   = static_cast<std::uint16_t>(addr + size - 1);
            sub.filter.page = page;
            sub.access      = static_cast<Access>(access);
            sub.action      = Action::Stop;
            const auto s = dbg_.subscribe(cid_, sub);
            if (s) {
                wps_.push_back(Watch{addr, bank1, size, cmd.payload[5], s.value});
                err = 0;
            } else {
                Log::debugger()->warn("dzrp: CMD_ADD_WATCHPOINT at 0x{:04X} refused: {}", addr,
                                      result_name(s.status));
            }
        }
    }
    reply(cmd.seq, {err});
}

// CMD_REMOVE_WATCHPOINT (43): DZRP watchpoints have no id, so the exact
// (addr, bank+1, size, access) tuple the add carried is matched. One removed
// per command; an unknown tuple is a seq-only reply and a warn line.
void DzrpServer::cmd_remove_watchpoint(const Command& cmd) {
    const std::uint16_t addr   = le16(cmd.payload, 0);
    const std::uint8_t  bank1  = cmd.payload[2];
    const std::uint16_t size   = le16(cmd.payload, 3);
    const std::uint8_t  access = cmd.payload[5];
    const auto it = std::find_if(wps_.begin(), wps_.end(), [&](const Watch& w) {
        return w.addr == addr && w.bank1 == bank1 && w.size == size && w.access == access;
    });
    if (it == wps_.end()) {
        Log::debugger()->warn("dzrp: CMD_REMOVE_WATCHPOINT: none at 0x{:04X} bank+1 {} size {} "
                              "access {}",
                              addr, bank1, size, access);
    } else {
        dbg_.unsubscribe(cid_, it->id);
        wps_.erase(it);
    }
    reply(cmd.seq);
}

// CMD_READ_STATE (50): a BOOKMARK in the backend's named map (CAP-CAP-03), and
// on the wire a token — "JNXB" + its name — not the snapshot: "arbitrary data,
// the format is up to the remote" (spec). Only at a frame boundary: DeZog does
// not re-read registers after a save (F9), so advancing to one would leave its
// cached PC behind the machine. Mid-frame (after a breakpoint) the reply is
// ZERO-LENGTH — "it was not possible to obtain the state" (spec) — and so is a
// save past the bound of 8 (owner decision Q2; never a silent eviction).
void DzrpServer::cmd_read_state(const Command& cmd) {
    const std::string name =
        "dzrp-" + std::to_string(cid_) + "-" + std::to_string(next_token_);
    const Result r =
        dbg_.bookmark_save(cid_, name, jnext::dbg::SaveStateMode::RefuseMidFrame);
    last_save_refused_mid_frame_ = r == Result::NotAtFrameBoundary;
    if (r != Result::Ok) {
        Log::debugger()->warn("dzrp: CMD_READ_STATE refused: {}{}", result_name(r),
                              r == Result::NotAtFrameBoundary
                                  ? " — the machine stopped mid-frame; a state can be saved "
                                    "after a manual pause"
                                  : "");
        reply(cmd.seq);
        return;
    }
    ++next_token_;
    std::vector<std::uint8_t> out(STATE_TOKEN_MAGIC, STATE_TOKEN_MAGIC + 4);
    out.insert(out.end(), name.begin(), name.end());
    reply(cmd.seq, out);
}

// CMD_WRITE_STATE (51): the payload is VALIDATED BEFORE ANY BACKEND CALL
// (review R-1). Empty (what DeZog sends back after a refused save), short,
// without the magic, or naming a bookmark this session did not issue or the
// backend no longer holds → reply, then NTF_PAUSE 255 "no state to restore";
// `bookmark_restore` is not called, nothing is latched, the session goes on.
// Only a valid token reaches the backend; a restore that then fails is reported
// the same way with the reason.
void DzrpServer::cmd_write_state(const Command& cmd) {
    const std::vector<std::uint8_t>& p = cmd.payload;
    std::string name;
    bool        valid = p.size() > 4 && std::equal(p.begin(), p.begin() + 4, STATE_TOKEN_MAGIC);
    if (valid) {
        // Held by the backend FOR THIS CLIENT: bookmarks are per client and die
        // with its detach, and only this adapter saves under this client — so
        // "issued by this session" and "held" are the same question.
        name.assign(p.begin() + 4, p.end());
        const auto held = dbg_.bookmarks(cid_);
        valid = std::find(held.begin(), held.end(), name) != held.end();
    }
    reply(cmd.seq);
    if (!valid) {
        const std::string why =
            std::string("no state to restore") +
            (last_save_refused_mid_frame_ ? " (save was refused mid-frame)" : "");
        Log::debugger()->warn("dzrp: CMD_WRITE_STATE of {} bytes: {}", p.size(), why);
        queue_pause_ntf(BREAK_OTHER, dbg_.registers().PC, why);
        return;
    }
    const Result r = dbg_.bookmark_restore(cid_, name);
    if (r != Result::Ok) {
        std::string why = std::string("restore failed: ") + result_name(r);
        if (const auto inc = dbg_.resume_blocked_by_corruption())
            why += " (" + inc->subsystem + ")";
        Log::debugger()->warn("dzrp: CMD_WRITE_STATE {}", why);
        queue_pause_ntf(BREAK_OTHER, dbg_.registers().PC, why);
    }
}

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
