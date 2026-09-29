#include "remote/dzrp/dzrp_server.h"

#include <algorithm>
#include <cctype>

#include "core/log.h"
#include "version.h"

namespace jnext {
namespace remote {
namespace dzrp {

using jnext::dbg::CLIENT_NONE;
using jnext::dbg::ClientInfo;
using jnext::dbg::ClientKind;
using jnext::dbg::MemSpace;
using jnext::dbg::RegId;
using jnext::dbg::Result;
using jnext::dbg::result_name;
using jnext::dbg::ServiceStep;

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
    // id                         name                          min  legacy session handler
    {CMD_INIT,                   "CMD_INIT",                   0, false, false, &DzrpServer::cmd_init},
    {CMD_CLOSE,                  "CMD_CLOSE",                  0, false, false, &DzrpServer::cmd_close},
    {CMD_GET_REGISTERS,          "CMD_GET_REGISTERS",          0, false, true,  &DzrpServer::cmd_get_registers},
    {CMD_SET_REGISTER,           "CMD_SET_REGISTER",           3, false, true,  &DzrpServer::cmd_set_register},
    {CMD_WRITE_BANK,             "CMD_WRITE_BANK",             0, true,  true,  &DzrpServer::cmd_write_bank},
    {CMD_READ_MEM,               "CMD_READ_MEM",               5, false, true,  &DzrpServer::cmd_read_mem},
    {CMD_WRITE_MEM,              "CMD_WRITE_MEM",              3, false, true,  &DzrpServer::cmd_write_mem},
    {CMD_SET_SLOT,               "CMD_SET_SLOT",               0, false, true,  &DzrpServer::cmd_set_slot},
    {CMD_GET_TBBLUE_REG,         "CMD_GET_TBBLUE_REG",         1, false, true,  &DzrpServer::cmd_get_tbblue_reg},
    {CMD_SET_BORDER,             "CMD_SET_BORDER",             1, true,  true,  &DzrpServer::cmd_set_border},
    {CMD_LOOPBACK,               "CMD_LOOPBACK",               0, false, false, &DzrpServer::cmd_loopback},
    {CMD_READ_PORT,              "CMD_READ_PORT",              2, false, true,  &DzrpServer::cmd_read_port},
    {CMD_WRITE_PORT,             "CMD_WRITE_PORT",             3, false, true,  &DzrpServer::cmd_write_port},
    {CMD_INTERRUPT_ON_OFF,       "CMD_INTERRUPT_ON_OFF",       1, false, true,  &DzrpServer::cmd_interrupt_on_off},
    {CMD_GET_SUPPORTED_COMMANDS, "CMD_GET_SUPPORTED_COMMANDS", 0, false, false, &DzrpServer::cmd_get_supported_commands},
    {CMD_READ_BANK_MEM,          "CMD_READ_BANK_MEM",          5, false, true,  &DzrpServer::cmd_read_bank_mem},
    {CMD_WRITE_BANK_MEM,         "CMD_WRITE_BANK_MEM",         3, false, true,  &DzrpServer::cmd_write_bank_mem},
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

void DzrpServer::on_notify(Connection& /*c*/) {
    // Nothing is queued yet: `NTF_PAUSE` is WP-3's (design §3.3).
}

void DzrpServer::on_disconnect() {
    // A dropped socket is a CMD_CLOSE (design §2 row 2, §4.1): SES-01's detach
    // releases this client's own pause, so a crashed DeZog cannot leave the
    // machine hung.
    end_session();
    parser_.reset();
    conn_ = nullptr;
}

void DzrpServer::end_session() {
    if (cid_ == CLIENT_NONE) return;
    dbg_.detach(cid_);
    cid_ = CLIENT_NONE;
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
// ROM-mapped bytes are dropped, Layer-2 write-over applies. DZRP has no error
// field here, so what did not land is said in the log.
void DzrpServer::cmd_write_mem(const Command& cmd) {
    const std::uint16_t addr = le16(cmd.payload, 1);
    const std::size_t   n    = cmd.payload.size() - 3;
    if (n > 0) {
        const std::uint8_t* data = cmd.payload.data() + 3;
        const auto          w    = dbg_.poke(cid_, MemSpace::cpu(), addr, n, data);
        if (w.status != Result::Ok) {
            Log::debugger()->warn("dzrp: CMD_WRITE_MEM of {} bytes at 0x{:04X} refused: {}", n,
                                  addr, result_name(w.status));
        } else {
            // Read back to count what did not land: ROM, or a write-only
            // overlay (Layer 2 write-over) that routes writes elsewhere.
            std::vector<std::uint8_t> back(n);
            dbg_.peek(MemSpace::cpu(), addr, n, back.data());
            std::size_t differ = 0;
            for (std::size_t i = 0; i < n; ++i) differ += back[i] != data[i];
            if (differ > 0)
                Log::debugger()->debug("dzrp: CMD_WRITE_MEM at 0x{:04X}: {} of {} bytes do not "
                                       "read back as written (ROM, or a write-only overlay)",
                                       addr, differ, n);
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
// is slot 0's ROM half, 0x2000-0x3FFF slot 1's, each read through the space
// the backend names for that slot (`SlotInfo.space` + `space_offset`) — the
// adapter composes no ROM index of its own (REQ-dzrp-12). A half is served
// only while its slot IS ROM (design deviation: see dzrp-frontend.md §12).
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
            const auto& si = slots[off / DZRP_BANK_BYTES];
            if (!si.is_rom) break;
            const std::uint32_t in_half = off % DZRP_BANK_BYTES;
            const std::size_t   chunk =
                std::min<std::size_t>(size - served, DZRP_BANK_BYTES - in_half);
            const auto got =
                dbg_.peek(si.space, si.space_offset + in_half, chunk, out.data() + served);
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

}  // namespace dzrp
}  // namespace remote
}  // namespace jnext
