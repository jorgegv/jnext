// jnext::remote::gdb — the GDB Remote Serial Protocol adapter (GH #281, epic
// #276 package G). The fake-transport unit suite of gdb-rsp-frontend.md §7.3.
//
// WHAT IS UNDER TEST. `GdbServer` (src/remote/gdb/rsp_server.*), its wire
// codec (rsp_codec.*) and the target description with its register packing
// (target_desc.*), run inside the PRODUCTION `remote::Server` over T's
// in-memory `FakeListener` / `FakePeer`, on a real `Emulator` + `Debugger`,
// driven through `Debugger::pump()` exactly as a loop owner drives it. Only the
// kernel is replaced. Every row asserts BYTES ON THE WIRE and, where a packet
// touches the machine, MACHINE STATE.
//
//   GDB-FRM-*   framing (WP-1): checksum accept/reject, the `+`, `}` escapes,
//               the oversize bound, 0x03 inside and outside a packet, noise,
//               split delivery, one command per service pass.
//   GDB-SUP-*   `qSupported` and `qXfer:features:read` (WP-2): the exact
//               stanzas, the decimal size, the document byte for byte and
//               UNDER THE 1023-BYTE v2.4 CEILING, chunked reads.
//   GDB-REG-*   `g`/`G`/`p`/`P` (WP-2): the order the document names, little
//               endian, the clock pair, the zero-clobber rule, HALT.
//   GDB-MEM-*   `m`/`M`/`X` (WP-3): the live CPU view, ROM refused before a
//               byte lands, the 0x10000 clip, gdb's `X` probe.
//   GDB-BP-*    `Z`/`z` 0..4: the stop reply, idempotence, both edges of a
//               watch range, ownership and detach.
//   GDB-STP-*   `s`, `i<len>` (z88dk's nexti), `c`, step-off, refusals.
//   GDB-STOP-*  the §5.4 stop-reply state machine: exactly once, never
//               spontaneous, `?` and 0x03 while running and while stopped,
//               rule 4 (an inspection packet re-pauses).
//   GDB-MON-*   `qRcmd` — the `monitor` vocabulary of §4.3.
//   GDB-UNS-*   unsupported packets: the empty reply, never a silent accept.
//   GDB-GEN-*   the generic stub minimum (`H`, `qC`, `qAttached`, threads).
//   GDB-SES-*   the session (WP-3/WP-4): attach on connect, `D`/`k`/hang-up
//               detach, one client, DZRP and GDB on one backend at once.
//
// THE ORACLE. The rows derive from the client — z88dk-gdb's source as the
// design cites it (debugger_gdb.c v2.4 and upstream) — and the RSP manual,
// never from the adapter. In particular the wire decoding below is this
// suite's own (checksum, packet split), so a framing bug the adapter and its
// codec share cannot pass here.
//
// EVERY WAIT IS BOUNDED. No row sleeps; every pump or frame loop has a cap.
//
// Run: ./build/test/gdb_rsp_test

#include "remote/dzrp/dzrp_server.h"
#include "remote/fake_transport.h"
#include "remote/gdb/rsp_codec.h"
#include "remote/gdb/rsp_server.h"
#include "remote/gdb/target_desc.h"
#include "remote/transport.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "core/rzx.h"
#include "debug/debugger.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "../row_id.h"

using jnext::dbg::Debugger;
using jnext::dbg::PumpBudget;
using jnext::dbg::Result;
using jnext::remote::FakeListener;
using jnext::remote::FakePeer;
using namespace jnext::remote::gdb;

// ── Tiny test harness (matches dzrp_adapter_test) ──────────────────────────

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static void check(const char* id, const char* desc, bool cond,
                  const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc, detail.empty() ? "" : " — ",
                    detail.c_str());
    }
}

static std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : " | ") + s;
    return out;
}

// ── Log capture ────────────────────────────────────────────────────────────

/// Adds a ring sink to the `debugger` channel, at debug level, for its
/// lifetime.
struct LogTap {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> ring =
        std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(512);
    spdlog::level::level_enum old;
    LogTap() : old(Log::debugger()->level()) {
        Log::debugger()->sinks().push_back(ring);
        Log::debugger()->set_level(spdlog::level::debug);
    }
    ~LogTap() {
        Log::debugger()->set_level(old);
        auto& s = Log::debugger()->sinks();
        s.erase(std::remove(s.begin(), s.end(), ring), s.end());
    }
    int count(const std::string& needle) const {
        int n = 0;
        for (const auto& l : ring->last_formatted())
            if (l.find(needle) != std::string::npos) ++n;
        return n;
    }
};

// ── The wire, decoded independently of the adapter's codec ─────────────────

static std::string hex2(unsigned v) {
    static const char d[] = "0123456789abcdef";
    return std::string{d[(v >> 4) & 0xF], d[v & 0xF]};
}

static unsigned sum(const std::string& s) {
    unsigned t = 0;
    for (char c : s) t += static_cast<unsigned char>(c);
    return t & 0xFF;
}

/// A packet as a client sends it (no escaping: the rows that need it build the
/// body escaped).
static std::string pkt(const std::string& body) { return "$" + body + "#" + hex2(sum(body)); }

/// Text → hex, as `qRcmd` carries it.
static std::string tohex(const std::string& s) {
    std::string out;
    for (char c : s) out += hex2(static_cast<unsigned char>(c));
    return out;
}

static std::string fromhex(const std::string& h) {
    std::string out;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2)
        out.push_back(static_cast<char>(std::stoul(h.substr(i, 2), nullptr, 16)));
    return out;
}

/// What the server wrote, split: acks in `acks`, packet bodies in order.
struct Wire {
    std::string              rx;
    std::string              acks;
    std::vector<std::string> bodies;
    bool                     bad_sum = false;

    void absorb(const std::string& bytes) {
        rx += bytes;
        for (;;) {
            if (rx.empty()) return;
            if (rx[0] == '+' || rx[0] == '-') {
                acks.push_back(rx[0]);
                rx.erase(0, 1);
                continue;
            }
            if (rx[0] != '$') {
                // The server never writes anything else outside a packet.
                acks.push_back('?');
                rx.erase(0, 1);
                continue;
            }
            const std::size_t hash = rx.find('#');
            if (hash == std::string::npos || rx.size() < hash + 3) return;
            const std::string body = rx.substr(1, hash - 1);
            if (hex2(sum(body)) != rx.substr(hash + 1, 2)) bad_sum = true;
            bodies.push_back(body);
            rx.erase(0, hash + 3);
        }
    }
};

// ── The rig: a real machine, the backend, the adapter over T's fake ────────

struct Rig {
    Emulator                   emu;
    std::unique_ptr<Debugger>  dbg;
    std::unique_ptr<GdbServer> gdb;
    FakeListener*              lsn = nullptr;

    explicit Rig(MachineType type = MachineType::ZX48K) {
        EmulatorConfig cfg;
        cfg.type = type;
        emu.init(cfg);
        // Parked on `JR $` at 0x8000, interrupts off — the backend suite's
        // idiom: frames run, nothing moves.
        emu.mmu().write(0x8000, 0x18);
        emu.mmu().write(0x8001, 0xFE);
        Z80Registers r = emu.cpu().get_registers();
        r.PC   = 0x8000;
        r.SP   = 0xFF00;
        r.IFF1 = 0;
        r.IFF2 = 0;
        emu.cpu().set_registers(r);

        dbg = std::make_unique<Debugger>(emu);
        gdb = std::make_unique<GdbServer>(*dbg);
        auto l = std::make_unique<FakeListener>();
        lsn    = l.get();
        gdb->server().open(std::move(l), "127.0.0.1", 0);
        dbg->add_service(gdb->server());
    }

    ~Rig() {
        gdb.reset();  // before the Debugger it unregisters from
        dbg.reset();
    }

    /// One loop-owner tick's worth of service while running: `PumpBudget{}`.
    void pump(int n = 1) {
        for (int i = 0; i < n; ++i) dbg->pump(PumpBudget{});
    }
};

/// A client: the peer end plus what it has read.
struct Client {
    Rig&                      rig;
    std::shared_ptr<FakePeer> p;
    Wire                      w;
    std::size_t               seen = 0;  // bodies already returned

    explicit Client(Rig& r) : rig(r), p(r.lsn->connect()) { rig.pump(); }

    /// One pump, collecting what it wrote.
    void tick() {
        rig.pump();
        w.absorb(p->take());
    }

    /// Bodies that arrived since the last call.
    std::vector<std::string> fresh() {
        std::vector<std::string> out(w.bodies.begin() + static_cast<long>(seen), w.bodies.end());
        seen = w.bodies.size();
        return out;
    }

    /// Send raw bytes, pump until at least one new packet body arrived (or
    /// `max` pumps), and return every new body. What arrived before the send
    /// is set aside first: a row that cares whether anything did checks
    /// `fresh()` itself.
    std::vector<std::string> send_raw(const std::string& bytes, int max = 8) {
        fresh();
        if (!bytes.empty()) p->send(bytes);
        std::vector<std::string> got;
        for (int i = 0; i < max && got.empty(); ++i) {
            tick();
            got = fresh();
        }
        return got;
    }

    /// Send one packet and return its reply body; "<none>" if nothing came.
    std::string cmd(const std::string& body, int max = 8) {
        const auto got = send_raw(pkt(body), max);
        return got.empty() ? std::string("<none>") : got[0];
    }

    /// `monitor <text>`: the decoded `O` lines and the final reply.
    std::vector<std::string> monitor(const std::string& text, std::string& final_reply) {
        p->send(pkt("qRcmd," + tohex(text)));
        std::vector<std::string> lines;
        final_reply = "<none>";
        for (int i = 0; i < 8; ++i) {
            tick();
            for (const auto& b : fresh()) {
                if (b.size() > 1 && b[0] == 'O' && b != "OK") {
                    lines.push_back(fromhex(b.substr(1)));
                } else {
                    final_reply = b;
                    return lines;
                }
            }
        }
        return lines;
    }
};

static void load_prog(Rig& rig, std::uint16_t at, std::initializer_list<int> code) {
    std::uint16_t a = at;
    for (int b : code) rig.emu.mmu().write(a++, static_cast<std::uint8_t>(b));
}

static void set_pc(Rig& rig, std::uint16_t pc) {
    Z80Registers r = rig.emu.cpu().get_registers();
    r.PC     = pc;
    r.halted = false;
    rig.emu.cpu().set_registers(r);
}

/// Run frames, as a loop owner does, until the machine pauses or `max` ran.
static bool run_until_paused(Rig& rig, int max = 10) {
    for (int i = 0; i < max && !rig.dbg->state().paused; ++i) rig.emu.run_frame();
    return rig.dbg->state().paused;
}

static std::uint16_t pc(Rig& rig) { return rig.emu.cpu().get_registers().PC; }
static std::uint8_t reg_a(Rig& rig) {
    return static_cast<std::uint8_t>(rig.emu.cpu().get_registers().AF >> 8);
}

/// The loop at 0x8000: INC A; NOP; NOP; JR 0x8000.
static void load_loop(Rig& rig) {
    load_prog(rig, 0x8000, {0x3C, 0x00, 0x00, 0x18, 0xFB});
    set_pc(rig, 0x8000);
}

/// A connected client whose `?` has stopped the machine — where every
/// z88dk-gdb session starts.
static void open_session(Client& c) {
    c.cmd("qSupported");
    c.cmd("?");
}

/// A register file with every field distinct, so a write to the wrong one shows.
static Z80Registers distinct_regs(const Z80Registers& base) {
    Z80Registers r = base;
    r.AF = 0xA1F1; r.BC = 0xB2C2; r.DE = 0xD3E3; r.HL = 0x4454; r.AF2 = 0xA7F7;
    r.BC2 = 0xB8C8; r.DE2 = 0xD9E9; r.HL2 = 0x4A5A; r.IX = 0x6575; r.IY = 0x8696;
    r.SP = 0xFE12; r.PC = 0x8000; r.I = 0x3B; r.R = 0x4C; r.IM = 1; r.IFF1 = 1; r.IFF2 = 1;
    return r;
}

/// The test's own name → value map for the document's registers (NOT the
/// adapter's `reg_value`).
static bool reg_by_name(const std::string& n, const Z80Registers& r, std::uint16_t& v) {
    if (n == "af") v = r.AF;
    else if (n == "bc") v = r.BC;
    else if (n == "de") v = r.DE;
    else if (n == "hl") v = r.HL;
    else if (n == "af'") v = r.AF2;
    else if (n == "bc'") v = r.BC2;
    else if (n == "de'") v = r.DE2;
    else if (n == "hl'") v = r.HL2;
    else if (n == "ix") v = r.IX;
    else if (n == "iy") v = r.IY;
    else if (n == "sp") v = r.SP;
    else if (n == "pc") v = r.PC;
    else return false;
    return true;
}

/// 4 hex digits, little endian, as the test expects a register on the wire.
static std::string le4(std::uint16_t v) { return hex2(v & 0xFF) + hex2(v >> 8); }

/// Every `<reg name="…">` of the document, in order, by plain string search.
static std::vector<std::string> doc_reg_names(const std::string& xml) {
    std::vector<std::string> out;
    const std::string        tag = "<reg name=\"";
    for (std::size_t at = xml.find(tag); at != std::string::npos; at = xml.find(tag, at + 1)) {
        const std::size_t from = at + tag.size();
        out.push_back(xml.substr(from, xml.find('"', from) - from));
    }
    return out;
}

// ── GDB-FRM — the wire (WP-1) ─────────────────────────────────────────────

static void framing_rows() {
    {
        Rig    rig;
        Client c(rig);
        // "qC" sums to 0x71 + 0x43 = 0xB4.
        const auto good = c.send_raw("$qC#b4");
        check("GDB-FRM-01", "a packet with the right checksum is acknowledged '+' and answered, "
                            "and the reply's own checksum is the sum of its body mod 256",
              good.size() == 1 && good[0] == "QC1" && c.w.acks == "+" && !c.w.bad_sum,
              join(good) + " acks=" + c.w.acks);

        const auto bad = c.send_raw("$qC#b5", 3);
        const auto after = c.send_raw(pkt("qC"));
        check("GDB-FRM-02", "a wrong checksum is answered '-' and NOT executed; the next good "
                            "packet is served as usual",
              bad.empty() && after.size() == 1 && after[0] == "QC1" && c.w.acks == "+-+",
              "acks=" + c.w.acks);

        // Upper-case checksum digits are the same number.
        const auto upper = c.send_raw("$qC#B4");
        check("GDB-FRM-03", "the checksum's hex digits are read in either case, and the server "
                            "never waits for the client's ack: three packets, no '+' sent back, "
                            "three replies",
              upper.size() == 1 && upper[0] == "QC1" &&
                  [&] {
                      c.p->send(pkt("qC") + pkt("qAttached") + pkt("qC"));
                      std::vector<std::string> all;
                      for (int i = 0; i < 6; ++i) {
                          c.tick();
                          for (auto& b : c.fresh()) all.push_back(b);
                      }
                      return all == std::vector<std::string>{"QC1", "1", "QC1"};
                  }());
    }
    {
        // `}` escapes inside X: `#`, `$`, `}`, `*` and a raw 0x03 (data, not an
        // interrupt, inside a packet).
        Rig    rig;
        Client c(rig);
        open_session(c);
        const std::string esc = std::string("}\x03}\x04}]}\x0a") + std::string(1, '\x03') + "A";
        const std::string r   = c.cmd("X9000,6:" + esc);
        std::uint8_t      back[6] = {};
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x9000, 6, back);
        const bool ok = back[0] == '#' && back[1] == '$' && back[2] == '}' && back[3] == '*' &&
                        back[4] == 0x03 && back[5] == 'A';
        check("GDB-FRM-04", "`X` data is `}`-unescaped (# $ } * arrive as }\\x03 }\\x04 }] }\\n) "
                            "and a raw 0x03 INSIDE a packet is data, not an interrupt",
              r == "OK" && ok && rig.dbg->state().paused, r);
    }
    {
        Rig    rig;
        LogTap log;
        Client c(rig);
        open_session(c);
        const std::string big = "m" + std::string(MAX_PACKET_BODY, '0');
        const auto        over = c.send_raw(pkt(big), 4);
        const auto        next = c.send_raw(pkt("qC"));
        check("GDB-FRM-05", "a packet longer than 16384 bytes is dropped and answered '-', "
                            "with a warn line; the stream stays in sync and the next packet is "
                            "served",
              over.empty() && next.size() == 1 && next[0] == "QC1" &&
                  c.w.acks.find("-+") != std::string::npos && log.count("longer than 16384") == 1,
              "acks=" + c.w.acks);
    }
    {
        Rig    rig;
        Client c(rig);
        load_loop(rig);
        // 0x03 OUTSIDE a packet, while running: the interrupt. It is not a
        // request, and with no stop reply owed (no `c`) none is sent.
        const auto r = c.send_raw(std::string(1, '\x03'), 3);
        check("GDB-FRM-06", "a 0x03 outside a packet is Ctrl-C: the running machine stops, with "
                            "no ack (it is not a packet) and — nothing being owed — no reply",
              r.empty() && rig.dbg->state().paused && c.w.acks.empty() && c.p->pending() == 0,
              join(r) + " acks=" + c.w.acks);
    }
    {
        Rig    rig;
        Client c(rig);
        // Noise before `$`, and the client's own acks, are ignored.
        const auto r = c.send_raw("xyz\r\n++-" + pkt("qC"));
        const auto r2 = c.send_raw(std::string("$qAtt") + pkt("qC"));
        check("GDB-FRM-07", "bytes before a `$`, and `+`/`-` from the client, are skipped; a "
                            "packet cut off by a new `$` is dropped and the new one served",
              r.size() == 1 && r[0] == "QC1" && r2.size() == 1 && r2[0] == "QC1" &&
                  c.w.acks == "++",
              join(r) + " / " + join(r2) + " acks=" + c.w.acks);
    }
    {
        Rig    rig;
        Client c(rig);
        // Split delivery: one byte per pump.
        const std::string whole = pkt("qAttached");
        std::vector<std::string> got;
        for (char ch : whole) {
            c.p->send(std::string(1, ch));
            c.tick();
            for (auto& b : c.fresh()) got.push_back(b);
        }
        check("GDB-FRM-08", "a packet delivered one byte per pass is assembled and answered "
                            "once, after its last checksum digit",
              got.size() == 1 && got[0] == "1", join(got));
    }
    {
        // ONE COMMAND PER SERVICE PASS (T's contract): two packets in one
        // delivery are two `service_once` results.
        Rig    rig;
        Client c(rig);
        c.p->send(pkt("qC") + pkt("qAttached"));
        const auto s1 = rig.gdb->server().service_once(0);
        Wire       w1;
        w1.absorb(c.p->take());
        const auto s2 = rig.gdb->server().service_once(0);
        Wire       w2;
        w2.absorb(c.p->take());
        const auto s3 = rig.gdb->server().service_once(0);
        check("GDB-FRM-09", "two packets that arrive together are executed one per service "
                            "pass — Serviced, Serviced, then Idle",
              s1 == jnext::dbg::ServiceStep::Serviced && w1.bodies.size() == 1 &&
                  s2 == jnext::dbg::ServiceStep::Serviced && w2.bodies.size() == 1 &&
                  w2.bodies[0] == "1" && s3 == jnext::dbg::ServiceStep::Idle);
    }
    {
        // The codec's pure helpers, against hand-computed bytes.
        std::string       un;
        const std::string framed = frame_packet("a#b");
        const std::string all4 = frame_packet("#$}*");
        check("GDB-FRM-10", "frame_packet escapes each of # $ } * as } + byte^0x20 (so a binary "
                            "reply is well formed) and checksums the escaped body; "
                            "unescape_binary undoes it and refuses a lone trailing `}`",
              framed == "$a}\x03" "b#" + hex2(sum("a}\x03" "b")) &&
                  all4 == "$}\x03}\x04}]}\x0a#" + hex2(sum("}\x03}\x04}]}\x0a")) &&
                  !unescape_binary("ab}", un) && unescape_binary("}]}\x0a", un) && un == "}*");
        std::uint32_t v = 0;
        check("GDB-FRM-11", "parse_hex_u32 takes 1..8 hex digits and nothing else: 9 digits, "
                            "an empty string and a non-hex digit are refused, never wrapped",
              parse_hex_u32("ffffffff", v) && v == 0xFFFFFFFFu && !parse_hex_u32("100000000", v) &&
                  !parse_hex_u32("", v) && !parse_hex_u32("12g", v) && parse_hex_u32("C0DE", v) &&
                  v == 0xC0DE);
    }
}

// ── GDB-SUP — qSupported and the target description (WP-2) ────────────────

static void supported_rows() {
    Rig    rig;
    Client c(rig);
    const std::string s1 = c.cmd("qSupported");
    const std::string s2 = c.cmd("qSupported:multiprocess+;swbreak+;xmlRegisters=i386");
    check("GDB-SUP-01", "qSupported (bare, as z88dk-gdb sends it, or with gdb's feature list) "
                        "answers exactly PacketSize=4000;qXfer:features:read+;swbreak+;hwbreak+",
          s1 == "PacketSize=4000;qXfer:features:read+;swbreak+;hwbreak+" && s2 == s1, s1);

    // z88dk-gdb `sscanf("PacketSize=%d")` (decimal, :995-1006), gdb `strtol(…,
    // 16)`. Both readings must be within the server's receive bound, and the
    // client's `M` chunk, (size-16)/2 bytes as hex, must fit it.
    const std::size_t at  = s1.find("PacketSize=") + 11;
    const std::string num = s1.substr(at, s1.find(';', at) - at);
    const bool digits     = !num.empty() &&
                        std::all_of(num.begin(), num.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
    const unsigned long dec = digits ? std::stoul(num, nullptr, 10) : 0;
    const unsigned long hex = digits ? std::stoul(num, nullptr, 16) : 0;
    check("GDB-SUP-02", "the PacketSize value is decimal digits only, and both clients' readings "
                        "(decimal and hex) fit the 16384-byte receive bound",
          digits && dec <= MAX_PACKET_BODY && hex <= MAX_PACKET_BODY && dec >= 64, num);

    const std::string x = c.cmd("qXfer:features:read:target.xml:0,3fff");
    check("GDB-SUP-03", "qXfer:features:read:target.xml:0,3fff (the client's one read) answers "
                        "l + the whole document, 600 bytes, and the reply body stays under "
                        "1023 bytes — the v2.4 client strcpy's it into char[1024] and a 1094-byte "
                        "one segfaults it (design §7.1)",
          x.size() >= 1 && x[0] == 'l' && x.substr(1) == std::string(TARGET_XML) &&
              TARGET_XML_BYTES == 600 && x.size() < 1023,
          std::to_string(x.size()) + " bytes");

    // The document as the client parses it (§1.2): root `target`, arch exactly
    // `z80`, ONE feature whose name contains `z80`, 16-bit regs, names in the
    // order the `g` packer uses.
    const std::string        doc   = x.substr(1);
    const std::vector<std::string> names = doc_reg_names(doc);
    std::vector<std::string> packer(REG_NAMES.begin(), REG_NAMES.end());
    std::size_t bits16 = 0;
    for (std::size_t p = doc.find("bitsize=\"16\""); p != std::string::npos;
         p = doc.find("bitsize=\"16\"", p + 1))
        ++bits16;
    const bool feature_ok = doc.find("<feature name=\"org.gnu.gdb.z80.cpu\">") != std::string::npos &&
                            doc.find("<feature", doc.find("<feature") + 1) == std::string::npos;
    check("GDB-SUP-04", "the document has root <target>, <architecture>z80</architecture>, one "
                        "feature named with z80, 14 regs all bitsize 16 — af bc de hl af' bc' "
                        "de' hl' ix iy sp pc clockl clockh — in the SAME order the g packer "
                        "uses, and no i/r/iff/im",
          doc.rfind("<?xml version=\"1.0\"?>\n<target version=\"1.0\">", 0) == 0 &&
              doc.find("<architecture>z80</architecture>") != std::string::npos && feature_ok &&
              names == packer && names.size() == 14 && bits16 == 14 &&
              names == std::vector<std::string>{"af", "bc", "de", "hl", "af'", "bc'", "de'", "hl'",
                                                "ix", "iy", "sp", "pc", "clockl", "clockh"},
          join(names));

    // Chunked reads: gdb may read it in pieces; they must reassemble.
    std::string assembled;
    std::string kinds;
    for (unsigned off = 0; off < 700; off += 256) {
        const std::string part =
            c.cmd("qXfer:features:read:target.xml:" + hex2(off >> 8) + hex2(off & 0xFF) + ",100");
        if (part.empty()) break;
        kinds.push_back(part[0]);
        assembled += part.substr(1);
        if (part[0] == 'l') break;
    }
    const std::string beyond = c.cmd("qXfer:features:read:target.xml:1000,10");
    check("GDB-SUP-05", "read in 256-byte windows the document comes back m, m, l and "
                        "reassembles byte for byte; an offset past its end answers a bare l",
          kinds == "mml" && assembled == std::string(TARGET_XML) && beyond == "l", kinds);

    const std::string other = c.cmd("qXfer:features:read:other.xml:0,100");
    const std::string mmap  = c.cmd("qXfer:memory-map:read::0,100");
    const std::string zero  = c.cmd("qXfer:features:read:target.xml:0,0");
    check("GDB-SUP-06", "another annex of features:read answers E00, a zero-length read E00, "
                        "and another qXfer object is unsupported (empty reply)",
          other == "E00" && zero == "E00" && mmap.empty(), other + "/" + zero + "/" + mmap);
}

// ── GDB-REG — g / G / p / P (WP-2) ─────────────────────────────────────────

static void register_rows() {
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        const Z80Registers r = distinct_regs(rig.emu.cpu().get_registers());
        rig.emu.cpu().set_registers(r);
        const std::string g = c.cmd("g");
        // The test's own expectation: every slot is the register the DOCUMENT
        // names there, two bytes little endian.
        std::string want;
        bool        named = true;
        for (int n = 0; n < 12; ++n) {
            std::uint16_t v = 0;
            named = named && reg_by_name(REG_NAMES[static_cast<std::size_t>(n)], r, v);
            want += le4(v);
        }
        check("GDB-REG-01", "g is 56 hex digits: the document's registers in its order, each "
                            "little endian — AF first as F then A (f1a1), PC at slot 11",
              named && g.size() == 56 && g.substr(0, 48) == want && g.substr(0, 4) == "f1a1" &&
                  g.substr(44, 4) == "0080",
              g);

        const std::uint64_t t = rig.dbg->time().tstates_total;
        const std::string   lo = c.cmd("pc"), hi = c.cmd("pd");
        const bool before_ok = lo == le4(static_cast<std::uint16_t>(t & 0xFFFF)) &&
                               hi == le4(static_cast<std::uint16_t>((t >> 16) & 0xFFFF)) &&
                               g.substr(48, 4) == lo && g.substr(52, 4) == hi;
        c.send_raw(pkt("c"), 1);
        rig.emu.run_frame();
        c.send_raw(std::string(1, '\x03'), 2);
        const std::uint64_t t2 = rig.dbg->time().tstates_total;
        const std::string   lo2 = c.cmd("pc"), hi2 = c.cmd("pd");
        const std::string   g2  = c.cmd("g");
        check("GDB-REG-02", "clockl/clockh (p c / p d, and the SERVED g's slots 12-13) are the "
                            "low and high 16 bits of the monotonic T-state count, and move when "
                            "a frame runs (past 0x10000, so the high half is non-zero)",
              before_ok && t2 > t && (t2 >> 16) != 0 &&
                  lo2 == le4(static_cast<std::uint16_t>(t2 & 0xFFFF)) &&
                  hi2 == le4(static_cast<std::uint16_t>((t2 >> 16) & 0xFFFF)) &&
                  g2.size() == 56 && g2.substr(48, 4) == lo2 && g2.substr(52, 4) == hi2,
              lo + " " + hi + " / " + lo2 + " " + hi2 + " g2=" + g2 + " t2=" + std::to_string(t2));
    }
    {
        // THE ZERO-CLOBBER ROW. z88dk-gdb's `set hl 1234` sends the whole file
        // with the clock pair (and anything it does not know) as 0000.
        Rig    rig;
        Client c(rig);
        open_session(c);
        rig.emu.cpu().set_registers(distinct_regs(rig.emu.cpu().get_registers()));
        std::string body = "G";
        const std::uint16_t vals[12] = {0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666,
                                        0x7777, 0x8888, 0x9999, 0xAAAA, 0xBBBB, 0x9000};
        for (std::uint16_t v : vals) body += le4(v);
        body += "00000000";
        const std::string   r = c.cmd(body);
        const Z80Registers  a = rig.emu.cpu().get_registers();
        check("GDB-REG-03", "G applies the 12 pairs in document order and leaves I, R, IFF1, "
                            "IFF2 and IM exactly as they were, although the client sends the "
                            "clock pair as 0000",
              r == "OK" && a.AF == 0x1111 && a.BC == 0x2222 && a.DE == 0x3333 && a.HL == 0x4444 &&
                  a.AF2 == 0x5555 && a.BC2 == 0x6666 && a.DE2 == 0x7777 && a.HL2 == 0x8888 &&
                  a.IX == 0x9999 && a.IY == 0xAAAA && a.SP == 0xBBBB && a.PC == 0x9000 &&
                  a.I == 0x3B && a.R == 0x4C && a.IFF1 == 1 && a.IFF2 == 1 && a.IM == 1,
              r);
    }
    {
        // A HALTed CPU: `set hl …` re-sends an unchanged PC, which must not
        // un-halt it; a changed PC must (§4.2a).
        Rig    rig;
        Client c(rig);
        open_session(c);
        Z80Registers r = rig.emu.cpu().get_registers();
        r.halted       = true;
        rig.emu.cpu().set_registers(r);
        std::string g  = c.cmd("g");
        std::string g2 = "G" + le4(0x4321) + g.substr(4);  // AF changed, PC as read
        const std::string a = c.cmd(g2);
        const bool still_halted = rig.emu.cpu().get_registers().halted &&
                                  rig.emu.cpu().get_registers().AF == 0x4321;
        std::string g3 = "G" + g2.substr(1, 44) + le4(0x8100) + g.substr(48);
        const std::string b = c.cmd(g3);
        check("GDB-REG-04", "G writes only the registers that change: an unchanged PC leaves a "
                            "HALT in place, a changed PC clears it",
              a == "OK" && still_halted && b == "OK" &&
                  !rig.emu.cpu().get_registers().halted && pc(rig) == 0x8100,
              a + "/" + b);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        const Z80Registers before = rig.emu.cpu().get_registers();
        const std::string  shortg = c.cmd("G" + std::string(55, '1'));
        const std::string  longg  = c.cmd("G" + std::string(57, '1'));
        const std::string  badhex = c.cmd("G" + std::string(55, '1') + "x");
        const Z80Registers after  = rig.emu.cpu().get_registers();
        check("GDB-REG-05", "a G of 55 or 57 hex digits, or with a non-hex digit, is E01 and "
                            "writes nothing",
              shortg == "E01" && longg == "E01" && badhex == "E01" && after.AF == before.AF &&
                  after.HL == before.HL && after.PC == before.PC);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        rig.emu.cpu().set_registers(distinct_regs(rig.emu.cpu().get_registers()));
        const std::string g  = c.cmd("g");
        bool              ok = true;
        for (int n = 0; n < 12; ++n) {
            const std::string one = c.cmd("p" + std::string(1, "0123456789ab"[n]));
            ok = ok && one == g.substr(static_cast<std::size_t>(n) * 4, 4);
        }
        check("GDB-REG-06", "p0..pb each answer the same 4 hex digits as their g slot; p e "
                            "(past clockh) and a bare p are E01",
              ok && c.cmd("pe") == "E01" && c.cmd("p") == "E01");
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        const Z80Registers before = rig.emu.cpu().get_registers();
        const std::string  w      = c.cmd("P3=3412");
        const Z80Registers a      = rig.emu.cpu().get_registers();
        check("GDB-REG-07", "P3=3412 writes HL = 0x1234 (little endian) and nothing else; P c "
                            "(clockl) and P d are read-only: E01; P e, a bad value length and a "
                            "missing '=' are E01",
              w == "OK" && a.HL == 0x1234 && a.AF == before.AF && a.PC == before.PC &&
                  a.SP == before.SP && c.cmd("Pc=0000") == "E01" && c.cmd("Pd=0000") == "E01" &&
                  c.cmd("Pe=0000") == "E01" && c.cmd("P3=12") == "E01" && c.cmd("P3") == "E01",
              w);
    }
    {
        // The backend's RZX wall reaches the wire as E01.
        Rig    rig;
        Client c(rig);
        open_session(c);
        rig.emu.rzx_player().start(RzxRecording{});
        const std::string g = c.cmd("g");
        const std::string r = c.cmd("G" + g.substr(0, 12) + le4(0x7777) + g.substr(16));
        const std::string p = c.cmd("P3=7777");
        rig.emu.rzx_player().stop();
        check("GDB-REG-08", "while an RZX plays the backend refuses register writes, and G and P "
                            "answer E01 (g still reads)",
              g.size() == 56 && r == "E01" && p == "E01" &&
                  rig.emu.cpu().get_registers().HL != 0x7777,
              r + "/" + p);
    }
}

// ── GDB-MEM — m / M / X, the flat 64 K CPU view (WP-3) ─────────────────────

static void memory_rows() {
    {
        // The CPU VIEW through a remapped slot (Next): the same address reads
        // two pages as slot 6 is pointed at each.
        Rig    rig(MachineType::ZXN_ISSUE2);
        Client c(rig);
        open_session(c);
        std::uint8_t* pa = rig.emu.mmu().nr_page_ptr(0x20);
        std::uint8_t* pb = rig.emu.mmu().nr_page_ptr(0x21);
        for (int i = 0; i < 4; ++i) {
            pa[i] = static_cast<std::uint8_t>(0xA0 + i);
            pb[i] = static_cast<std::uint8_t>(0xB0 + i);
        }
        rig.dbg->set_mmu_slot(rig.gdb->client(), 6, 0x20);
        const std::string ra = c.cmd("mc000,4");
        rig.dbg->set_mmu_slot(rig.gdb->client(), 6, 0x21);
        const std::string rb = c.cmd("mc000,4");
        check("GDB-MEM-01", "m reads the CPU view through the live mapping: 0xC000 reads page "
                            "0x20 with slot 6 on it, then page 0x21 after a remap",
              ra == "a0a1a2a3" && rb == "b0b1b2b3", ra + "/" + rb);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        const std::string w = c.cmd("M9000,4:deadbeef");
        std::uint8_t      b[4] = {};
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x9000, 4, b);
        check("GDB-MEM-02", "M into RAM answers OK and the bytes land (read back through the "
                            "backend and through m)",
              w == "OK" && b[0] == 0xDE && b[1] == 0xAD && b[2] == 0xBE && b[3] == 0xEF &&
                  c.cmd("m9000,4") == "deadbeef",
              w);
    }
    {
        // ROM (48K: slots 0-1): a write there is E01 and ROM is unchanged. A
        // write straddling 0x3FFF/0x4000 is E01 too — not every byte landed —
        // and its RAM byte landed, as the CPU's own write would leave it
        // (GH #281 F1: the backend reports what landed; nothing is re-derived).
        Rig    rig;
        Client c(rig);
        open_session(c);
        std::uint8_t rom0 = 0, rom3fff = 0, ram0 = 0;
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x0000, 1, &rom0);
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x3FFF, 1, &rom3fff);
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x4000, 1, &ram0);
        const std::uint8_t ram_new = static_cast<std::uint8_t>(ram0 ^ 0xFF);
        const std::string a = c.cmd("M0,1:" + hex2(static_cast<std::uint8_t>(rom0 ^ 0xFF)));
        const std::string b = c.cmd("M3fff,2:" + hex2(static_cast<std::uint8_t>(rom3fff ^ 0xFF)) +
                                    hex2(ram_new));
        const std::string x = c.cmd("X0,1:" + std::string(1, static_cast<char>(rom0 ^ 0x55)));
        std::uint8_t rom_after = 0, rom3fff_after = 0, ram_after = 0;
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x0000, 1, &rom_after);
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x3FFF, 1, &rom3fff_after);
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x4000, 1, &ram_after);
        check("GDB-MEM-03", "M or X onto ROM is E01 and ROM is unchanged; a write straddling "
                            "0x3FFF/0x4000 is E01 (not all of it landed) with its RAM byte "
                            "landed and its ROM byte not",
              a == "E01" && b == "E01" && x == "E01" && rom_after == rom0 &&
                  rom3fff_after == rom3fff && ram_after == ram_new,
              a + "/" + b + "/" + x);
    }
    {
        // An OVERLAY over a ROM slot takes the write: Layer 2 write-over
        // (port 0x123B bit 0) puts it in the Layer 2 page; the CPU view there
        // still reads ROM.
        Rig    rig(MachineType::ZXN_ISSUE2);
        Client c(rig);
        open_session(c);
        rig.emu.port().out(0x123B, 0x01);
        std::uint8_t rom = 0;
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x0010, 1, &rom);
        const std::uint8_t  v    = static_cast<std::uint8_t>(rom ^ 0xFF);
        const std::uint16_t page = static_cast<std::uint16_t>(rig.dbg->nextreg_peek(0x12) * 2);
        const std::string   w    = c.cmd("M10,1:" + hex2(v));
        std::uint8_t l2 = 0, cpu = 0;
        rig.dbg->peek(jnext::dbg::MemSpace::page(page), 0x0010, 1, &l2);
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x0010, 1, &cpu);
        check("GDB-MEM-08", "M at 0x0010 under Layer 2 write-over (a ROM slot) is OK: the byte "
                            "lands in the Layer 2 page, and the CPU view there still reads ROM",
              rig.dbg->mmu_slots()[0].is_rom && w == "OK" && l2 == v && cpu == rom, w);
    }
    {
        // DivMMC paged in (port 0xE3 conmem): 0x2000-0x3FFF is its RAM, over
        // a ROM slot — a write there lands; 0x0000-0x1FFF is its ROM.
        Rig    rig(MachineType::ZXN_ISSUE2);
        Client c(rig);
        open_session(c);
        rig.emu.port().out(0x00E3, 0x80);
        const std::string w  = c.cmd("M2000,2:3cc3");
        const std::string rb = c.cmd("m2000,2");
        check("GDB-MEM-09", "M into DivMMC RAM (conmem, 0x2000, a ROM slot) is OK and reads back",
              rig.dbg->mmu_slots()[1].is_rom && w == "OK" && rb == "3cc3", w + "/" + rb);

        std::uint8_t d[2] = {};
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x1FFE, 2, d);
        const std::string mix = c.cmd("M1ffe,4:" + hex2(d[0] ^ 0xFF) + hex2(d[1] ^ 0xFF) + "1122");
        std::uint8_t after[4] = {};
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x1FFE, 4, after);
        check("GDB-MEM-10", "an M straddling DivMMC ROM and DivMMC RAM is E01 — only part of it "
                            "landed — with the RAM half written and the ROM half unchanged",
              mix == "E01" && after[0] == d[0] && after[1] == d[1] && after[2] == 0x11 &&
                  after[3] == 0x22,
              mix);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        const std::string x0 = c.cmd("X9000,0:");
        const std::string m0 = c.cmd("M9000,0:");
        const std::string xs = c.cmd("X9000,3:ab");
        const std::string ms = c.cmd("M9000,2:abc");
        const std::string mw = c.cmd("Mffff,2:0102");
        const std::string mh = c.cmd("M9000,1:zz");
        check("GDB-MEM-04", "X with length 0 (gdb's probe for X support) and M with length 0 "
                            "answer OK; a length that disagrees with the data, a write past "
                            "0xFFFF and bad hex are E01",
              x0 == "OK" && m0 == "OK" && xs == "E01" && ms == "E01" && mw == "E01" && mh == "E01",
              x0 + " " + m0 + " " + xs + " " + ms + " " + mw + " " + mh);
    }
    {
        // A write past 0xFFFF must not WRAP to 0x0000 — shown where 0x0000 is
        // RAM (Next, slot 0 mapped to a RAM page), so a wrap would land.
        Rig    rig(MachineType::ZXN_ISSUE2);
        Client c(rig);
        open_session(c);
        rig.dbg->set_mmu_slot(rig.gdb->client(), 0, 0x20);
        rig.dbg->set_mmu_slot(rig.gdb->client(), 7, 0x21);
        std::uint8_t before[2] = {};
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0xFFFF, 1, &before[0]);
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x0000, 1, &before[1]);
        const std::string w = c.cmd("Mffff,2:" + hex2(before[0] ^ 0xFF) + hex2(before[1] ^ 0xFF));
        const std::string x = c.cmd("Xffff,2:ab");
        std::uint8_t after[2] = {};
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0xFFFF, 1, &after[0]);
        rig.dbg->peek(jnext::dbg::MemSpace::cpu(), 0x0000, 1, &after[1]);
        check("GDB-MEM-07", "M or X that would run past 0xFFFF is E01 and writes nothing — not "
                            "the last byte, and not a wrapped byte at 0x0000 (RAM here)",
              !rig.dbg->mmu_slots()[0].is_rom && w == "E01" && x == "E01" &&
                  after[0] == before[0] && after[1] == before[1],
              w + "/" + x);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        const std::string clip = c.cmd("mfff0,20");
        check("GDB-MEM-05", "m clips at 0x10000 (fff0,20 answers 16 bytes); one starting at "
                            "0x10000, a length above 4096 and a zero length are E01 — never an "
                            "empty reply, which would mean 'm unsupported'",
              clip.size() == 32 && c.cmd("m10000,1") == "E01" && c.cmd("m8000,1001") == "E01" &&
                  c.cmd("m8000,1000").size() == 8192 && c.cmd("m8000,0") == "E01" &&
                  c.cmd("m8000") == "E01",
              std::to_string(clip.size()));
    }
    {
        // Side-effect free: an `m` sweep over the whole space on a +3 leaves
        // the floating-bus latch untouched (backend F1, through `peek(Cpu)`).
        Rig    rig(MachineType::ZX_PLUS3);
        Client c(rig);
        open_session(c);
        const std::uint8_t before = rig.emu.mmu().p3_floating_bus_dat();
        rig.emu.mmu().write(0x9000, static_cast<std::uint8_t>(before ^ 0x5A));
        const std::uint8_t latched = rig.emu.mmu().p3_floating_bus_dat();
        for (unsigned a = 0; a < 0x10000; a += 0x1000) c.cmd("m" + hex2(a >> 8) + hex2(a & 0xFF) + ",1000");
        check("GDB-MEM-06", "m is side-effect free: sixteen 4 KB reads on a +3 leave the "
                            "floating-bus latch as the last CPU access left it",
              rig.emu.mmu().p3_floating_bus_dat() == latched);
    }
}

// ── GDB-BP — Z / z (WP-3) ──────────────────────────────────────────────────

/// `LD (nn),A` / `LD A,(nn)` in a loop at 0x8000.
static void load_store_loop(Rig& rig, bool store, std::uint16_t target) {
    load_prog(rig, 0x8000, {store ? 0x32 : 0x3A, target & 0xFF, target >> 8, 0x18, 0xFB});
    set_pc(rig, 0x8000);
}

/// `c`, run frames, pump: the stop reply (or "<none>").
static std::string cont_and_stop(Rig& rig, Client& c, int frames = 5) {
    const auto r = c.send_raw(pkt("c"), 1);
    if (!r.empty()) return "early:" + r[0];
    run_until_paused(rig, frames);
    c.tick();
    const auto got = c.fresh();
    return got.empty() ? std::string("<none>") : join(got);
}

static void breakpoint_rows() {
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        const std::string z = c.cmd("Z0,8002,1");
        const auto subs = rig.dbg->subscriptions(false);
        check("GDB-BP-01", "Z0,8002,1 answers OK and installs ONE unconditional Execute[0x8002] "
                           "stop, owned by this client, listed to every client (not transient)",
              z == "OK" && subs.size() == 1 && subs[0].kind == jnext::dbg::EventKind::Execute &&
                  subs[0].filter.lo == 0x8002 && subs[0].filter.hi == 0x8002 &&
                  !subs[0].transient && !subs[0].has_condition &&
                  subs[0].action == jnext::dbg::Action::Stop &&
                  subs[0].owner == rig.gdb->client(),
              z);

        const std::string stop = cont_and_stop(rig, c);
        check("GDB-BP-02", "c answers nothing until the machine stops; the stop reply is "
                           "T05thread:1;swbreak:; and the machine is REALLY paused at 0x8002",
              stop == "T05thread:1;swbreak:;" && rig.dbg->state().paused && pc(rig) == 0x8002,
              stop);

        const std::string dup = c.cmd("Z0,8002,1");
        check("GDB-BP-03", "a duplicate Z0 at the same address answers OK and is the SAME "
                           "subscription (still one)",
              dup == "OK" && rig.dbg->subscriptions(false).size() == 1);

        const std::string unk = c.cmd("z0,a,1");
        check("GDB-BP-04", "z0 of an address never inserted answers OK (the client sends z0 on "
                           "quit for its client-side breakpoints) and removes nothing",
              unk == "OK" && rig.dbg->subscriptions(false).size() == 1, unk);

        const std::string rm = c.cmd("z0,8002,1");
        const std::string after = cont_and_stop(rig, c, 3);
        check("GDB-BP-05", "z0 removes it: the next c runs past 0x8002 and nothing is sent",
              rm == "OK" && rig.dbg->subscriptions(false).empty() && after == "<none>" &&
                  !rig.dbg->state().paused,
              after);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        const std::string z = c.cmd("Z1,8001,0");
        const std::string stop = cont_and_stop(rig, c);
        check("GDB-BP-06", "Z1 is served as Z0 (jnext has no hardware/software distinction): "
                           "the stop is swbreak, never hwbreak",
              z == "OK" && stop == "T05thread:1;swbreak:;" && pc(rig) == 0x8001, stop);
    }
    {
        // BOTH EDGES of a write watch [0x9000, 0x9003].
        struct Case {
            std::uint16_t target;
            bool          store;
            bool          stops;
        };
        const Case cases[] = {{0x9003, true, true},  {0x9004, true, false},
                              {0x9000, true, true},  {0x8FFF, true, false},
                              {0x9001, false, false}};
        std::string got;
        bool        ok = true;
        for (const Case& k : cases) {
            Rig    rig;
            Client c(rig);
            open_session(c);
            load_store_loop(rig, k.store, k.target);
            const std::string z = c.cmd("Z2,9000,4");
            const std::string s = cont_and_stop(rig, c, 3);
            const std::string want = "T05thread:1;watch:" + std::string(k.target == 0x9003 ? "9003" : "9000") + ";";
            ok = ok && z == "OK" && (k.stops ? s == want : s == "<none>");
            got += s + " ";
        }
        check("GDB-BP-07", "Z2,9000,4 watches writes to 0x9000..0x9003: a write to the last byte "
                           "and to the first stops with watch:<that address>; one past the end, "
                           "one below the start and a READ inside the range run on",
              ok, got);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_store_loop(rig, false, 0x9001);
        const std::string z = c.cmd("Z3,9000,2");
        const std::string s = cont_and_stop(rig, c);
        Rig    rig2;
        Client c2(rig2);
        open_session(c2);
        load_store_loop(rig2, true, 0x9001);
        c2.cmd("Z3,9000,2");
        const std::string s2 = cont_and_stop(rig2, c2, 3);
        check("GDB-BP-08", "Z3 is a read watch: a read of 0x9001 stops with rwatch:9001; a WRITE "
                           "there does not stop",
              z == "OK" && s == "T05thread:1;rwatch:9001;" && s2 == "<none>", s + " / " + s2);
    }
    {
        std::string got;
        bool        ok = true;
        for (bool store : {false, true}) {
            Rig    rig;
            Client c(rig);
            open_session(c);
            load_store_loop(rig, store, 0x9002);
            const std::string z = c.cmd("Z4,9002,1");
            const auto        subs = rig.dbg->subscriptions(false);
            const std::string s = cont_and_stop(rig, c);
            ok = ok && z == "OK" && s == "T05thread:1;awatch:9002;" && subs.size() == 1 &&
                 subs[0].access == jnext::dbg::Access::ReadWrite;
            got += s + " ";
        }
        check("GDB-BP-09", "Z4 is an access watch — ONE ReadWrite subscription — and both a read "
                           "and a write stop with awatch:9002",
              ok, got);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        const std::string cond = c.cmd("Z0,8002,1;X1,0f");
        const std::string wz   = c.cmd("Z2,9000,0");
        const std::string wrap = c.cmd("Z2,fffe,4");
        const std::string big  = c.cmd("Z0,10000,1");
        const std::string z5   = c.cmd("Z5,8000,1");
        check("GDB-BP-10", "a Z0 with a condition list is E01 (conditions declined, never "
                           "advertised); a watch of length 0, one wrapping past 0xFFFF and an "
                           "address past 0xFFFF are E01; Z5 is unsupported (empty); nothing is "
                           "installed",
              cond == "E01" && wz == "E01" && wrap == "E01" && big == "E01" && z5.empty() &&
                  rig.dbg->subscriptions(true).empty(),
              cond + " " + wz + " " + wrap + " " + big + " [" + z5 + "]");
    }
    {
        // D removes only THIS client's subscriptions.
        Rig    rig;
        Client c(rig);
        open_session(c);
        c.cmd("Z0,8002,1");
        c.cmd("Z2,9000,1");
        const auto other = rig.dbg->attach({"other", jnext::dbg::ClientKind::Test}).value;
        jnext::dbg::Subscription s;
        s.filter.lo = s.filter.hi = 0x8004;
        const auto theirs = rig.dbg->subscribe(other, s);
        const std::string d = c.cmd("D");
        const auto        left = rig.dbg->subscriptions(true);
        check("GDB-BP-11", "D answers OK and removes this client's Z0 and Z2 — another client's "
                           "breakpoint survives",
              d == "OK" && left.size() == 1 && left[0].owner == other &&
                  left[0].id == theirs.value,
              d + " left=" + std::to_string(left.size()));
        rig.dbg->detach(other);
    }
}

// Reached from breakpoint_rows(): insert, remove, insert again.
static void reinsert_rows() {
    {
        // `break X`, `delete 1`, `break X` — an ordinary z88dk-gdb sequence.
        // The remove must forget the insert (and ignore `kind`, a Z80
        // breakpoint has none), or the second insert answers OK and never fires.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        const std::string a = c.cmd("Z0,8002,1"), b = c.cmd("z0,8002,2");
        const bool removed = rig.dbg->subscriptions(false).empty();
        const std::string d = c.cmd("Z0,8002,1");
        const std::string s = cont_and_stop(rig, c);
        check("GDB-BP-12", "Z0, z0 with another kind (removes it: kind is ignored), Z0 again at "
                           "one address: the re-inserted breakpoint is live — one subscription, "
                           "and c stops on it with swbreak",
              a == "OK" && b == "OK" && removed && d == "OK" &&
                  rig.dbg->subscriptions(false).size() == 1 &&
                  s == "T05thread:1;swbreak:;" && pc(rig) == 0x8002,
              s);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_store_loop(rig, true, 0x9001);
        const std::string a = c.cmd("Z2,9000,2"), b = c.cmd("z2,9000,2"), d = c.cmd("Z2,9000,2");
        const std::string s = cont_and_stop(rig, c);
        check("GDB-BP-13", "Z2, z2, Z2 on one range: the re-inserted watch is live and stops "
                           "with watch:9001",
              a == "OK" && b == "OK" && d == "OK" && s == "T05thread:1;watch:9001;", s);
    }
}

// ── GDB-STP — s, i<len>, c (WP-3) ──────────────────────────────────────────

/// 0x8000 CALL 0x9000; 0x8003 JR $.  0x9000 INC A; RET.
static void load_call(Rig& rig) {
    load_prog(rig, 0x8000, {0xCD, 0x00, 0x90, 0x18, 0xFE});
    load_prog(rig, 0x9000, {0x3C, 0xC9});
    set_pc(rig, 0x8000);
}

static void step_rows() {
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_prog(rig, 0x8000, {0x21, 0x34, 0x12, 0x18, 0xFE});  // LD HL,0x1234
        set_pc(rig, 0x8000);
        const auto r = c.send_raw(pkt("s"), 1);
        check("GDB-STP-01", "s is answered T05thread:1; in the SAME pump, after one instruction "
                            "(LD HL,nn: PC 0x8000 -> 0x8003, HL = 0x1234)",
              r.size() == 1 && r[0] == "T05thread:1;" && pc(rig) == 0x8003 &&
                  rig.emu.cpu().get_registers().HL == 0x1234 && rig.dbg->state().paused,
              join(r));
        c.tick();
        c.tick();
        check("GDB-STP-02", "the step's own pause edge sends nothing more (exactly one reply)",
              c.fresh().empty());
    }
    {
        // i3 on a CALL runs the callee and stops at PC+3; `s` on the same CALL
        // stops INSIDE it — the control that tells the two apart.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_call(rig);
        const std::uint8_t a0 = reg_a(rig);
        const auto early = c.send_raw(pkt("i3"), 1);
        const bool running = !rig.dbg->state().paused;
        run_until_paused(rig, 3);
        c.tick();
        const auto stop = c.fresh();
        const bool over = stop.size() == 1 && stop[0] == "T05thread:1;" && pc(rig) == 0x8003 &&
                          reg_a(rig) == static_cast<std::uint8_t>(a0 + 1);
        Rig    rig2;
        Client c2(rig2);
        open_session(c2);
        load_call(rig2);
        const std::string into = c2.cmd("s");
        check("GDB-STP-03", "i3 (z88dk-gdb's nexti over a CALL) runs to PC+3 — the callee ran "
                            "(A+1) — and stops with T05thread:1; sent only then; s on the same "
                            "CALL stops inside the callee at 0x9000",
              early.empty() && running && over && into == "T05thread:1;" && pc(rig2) == 0x9000,
              join(stop) + " pc=" + std::to_string(pc(rig)));
    }
    {
        // nexti with a breakpoint inside the callee stops there first.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_call(rig);
        c.cmd("Z0,9001,1");
        c.send_raw(pkt("i3"), 1);
        run_until_paused(rig, 3);
        c.tick();
        const auto stop = c.fresh();
        check("GDB-STP-04", "i3 over a CALL whose callee holds this client's Z0 stops at the "
                            "breakpoint first, reported swbreak",
              stop.size() == 1 && stop[0] == "T05thread:1;swbreak:;" && pc(rig) == 0x9001,
              join(stop));
    }
    {
        // The run-to target is the backend's TRANSIENT, owned by this client
        // and hidden from user lists; a hang-up with it armed removes it.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_call(rig);
        c.send_raw(pkt("i3"), 1);
        const auto all  = rig.dbg->subscriptions(true);
        const auto user = rig.dbg->subscriptions(false);
        const auto cid  = rig.gdb->client();
        const bool owned_hidden = all.size() == 1 && all[0].transient && all[0].owner == cid &&
                                  all[0].filter.lo == 0x8003 && user.empty();
        c.p->close();
        c.tick();
        c.tick();
        check("GDB-STP-05", "i3's target is a transient Execute[0x8003] OWNED by this client and "
                            "hidden from the user list; the client hanging up with it armed "
                            "leaves no subscription at all (a crashed client cannot leave a stop "
                            "behind)",
              owned_hidden && rig.dbg->subscriptions(true).empty() &&
                  rig.gdb->client() == jnext::dbg::CLIENT_NONE);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        check("GDB-STP-06", "i with no length, RSP's own i<addr>,<n> (a cycle step jnext does "
                            "not do), i0 and a non-decimal length are E01, and nothing runs",
              c.cmd("i") == "E01" && c.cmd("i8000,1") == "E01" && c.cmd("i0") == "E01" &&
                  c.cmd("i3a") == "E01" && rig.dbg->state().paused &&
                  rig.dbg->subscriptions(true).empty());
    }
    {
        // GH #221 step-off, through `run()`: `c` from a PC holding a Z0 does
        // not stop there again at once.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        c.cmd("Z0,8002,1");
        cont_and_stop(rig, c);
        const std::uint8_t a0 = reg_a(rig);
        const std::string  again = cont_and_stop(rig, c);
        check("GDB-STP-07", "c from the breakpoint it stopped on steps off it (the backend's "
                            "GH #221 arm): one loop iteration runs (A+1), then it stops there "
                            "again",
              again == "T05thread:1;swbreak:;" && pc(rig) == 0x8002 &&
                  reg_a(rig) == static_cast<std::uint8_t>(a0 + 1),
              again);
    }
    {
        // A refused resume: the corruption gate (CTL-11).
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        const std::vector<std::uint8_t> junk(64, 0x5A);
        const auto   other   = rig.dbg->attach({"loader", jnext::dbg::ClientKind::Test}).value;
        const Result latched = rig.dbg->load_state_bytes(other, junk.data(), junk.size());
        const std::string r  = c.cmd("c");
        const std::string i  = c.cmd("i3");
        const std::string st = c.cmd("s");
        c.tick();
        const bool quiet = c.fresh().empty();
        // Nothing is owed after a refusal: a `?` is answered at once.
        const std::string q = c.cmd("?");
        check("GDB-STP-08", "a c, i or s the backend refuses (a latched corruption) is E01 at "
                            "once, the machine stays paused, no stop reply follows, and none is "
                            "left owed (a later ? is answered at once)",
              latched == Result::RefusedCorrupt && r == "E01" && i == "E01" && st == "E01" &&
                  rig.dbg->state().paused && quiet && q == "T05thread:1;",
              r + "/" + i + "/" + st + "/" + q);
        rig.dbg->detach(other);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        c.cmd("Z0,8002,1");
        load_prog(rig, 0x9000, {0x18, 0xFE});
        const auto r = c.send_raw(pkt("c9000"), 1);
        const bool moved = pc(rig) == 0x9000 && !rig.dbg->state().paused;
        const std::string s  = c.cmd("s8000");
        check("GDB-STP-09", "c <addr> resumes AT addr (PC set first, then run), and s <addr> "
                            "steps the instruction at addr",
              r.empty() && moved && s == "T05thread:1;" && pc(rig) == 0x8001, s);
    }
}

// ── GDB-STOP — the §5.4 state machine ──────────────────────────────────────

static void stop_rows() {
    {
        // `?` while RUNNING pauses and answers T05 in the same pump, once.
        Rig    rig;
        Client c(rig);
        load_loop(rig);
        const auto r = c.send_raw(pkt("?"), 1);
        const auto st = rig.dbg->state();
        c.tick();
        c.tick();
        check("GDB-STOP-01", "? on a running machine pauses it (attributed to this client) and "
                             "answers T05thread:1; in the same pump, exactly once",
              r.size() == 1 && r[0] == "T05thread:1;" && st.paused &&
                  st.pause_reason.by == rig.gdb->client() && c.fresh().empty(),
              join(r));
    }
    {
        // `?` while PAUSED by someone else answers at once and does NOT take
        // the pause over: this client's detach then leaves it paused.
        Rig  rig;
        auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        rig.dbg->pause(other);
        Client c(rig);
        const auto r = c.send_raw(pkt("?"), 1);
        const auto by = rig.dbg->state().pause_reason.by;
        c.cmd("D");
        check("GDB-STOP-02", "? on a machine another client paused answers T05thread:1; at once "
                             "without re-attributing the pause — so this client's D leaves the "
                             "machine paused",
              r.size() == 1 && r[0] == "T05thread:1;" && by == other && rig.dbg->state().paused,
              join(r));
        rig.dbg->detach(other);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        const auto r = c.send_raw(std::string(1, '\x03'), 3);
        const std::string g = c.cmd("g");
        c.tick();
        check("GDB-STOP-03", "0x03 on a stopped machine with nothing owed sends NOTHING (it is "
                             "not a request; the client is not waiting), so the next request's "
                             "reply is its own — review round 1",
              r.empty() && g.size() == 56 && c.fresh().empty() && rig.dbg->state().paused,
              join(r) + " / " + g);
    }
    {
        // THE REVIEWER'S REAL-CLIENT SEQUENCE (z88dk-gdb 2.4): Ctrl-C at the
        // prompt at a breakpoint stop, then `cont` — the client puts the 0x03 on
        // the wire before the `c`. Exactly ONE stop packet, the `c`'s, and the
        // client's following `g`/`m` get their own replies with nothing glued on.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        c.cmd("Z0,8002,1");
        c.send_raw(pkt("c"), 1);
        run_until_paused(rig);
        const std::string first = [&] { c.tick(); auto f = c.fresh(); return f.empty() ? std::string() : f[0]; }();
        c.p->send(std::string(1, '\x03') + pkt("c"));
        std::vector<std::string> after;
        for (int i = 0; i < 4; ++i) {
            c.tick();
            for (auto& b : c.fresh()) after.push_back(b);
        }
        const bool running = !rig.dbg->state().paused;
        run_until_paused(rig);
        for (int i = 0; i < 3; ++i) {
            c.tick();
            for (auto& b : c.fresh()) after.push_back(b);
        }
        const std::string g = c.cmd("g");
        const std::string m = c.cmd("m8000,4");
        c.tick();
        check("GDB-STOP-13", "0x03 then c at a breakpoint stop: the 0x03 sends nothing, the c "
                             "runs, and its ONE stop reply (swbreak) is the only packet; g and m "
                             "then get exactly their own replies",
              first == "T05thread:1;swbreak:;" && running &&
                  after == std::vector<std::string>{"T05thread:1;swbreak:;"} && g.size() == 56 &&
                  m.size() == 8 && c.fresh().empty(),
              join(after) + " / " + g + " / " + m);
    }
    {
        // Ctrl-C during a `c`.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        c.send_raw(pkt("c"), 1);
        rig.emu.run_frame();
        const auto r = c.send_raw(std::string(1, '\x03'), 1);
        c.tick();
        check("GDB-STOP-04", "0x03 during a c stops the machine and is answered ONE T02thread:1; "
                             "(the c's reply and the interrupt's are the same packet)",
              r.size() == 1 && r[0] == "T02thread:1;" && rig.dbg->state().paused &&
                  c.fresh().empty(),
              join(r));
    }
    {
        // A foreign Paused with no reply owed sends nothing (rule 3).
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        rig.dbg->run(other);
        rig.emu.run_frame();
        rig.dbg->pause(other);
        c.tick();
        c.tick();
        check("GDB-STOP-05", "another client runs and re-pauses the machine while this client "
                             "believes it stopped: a Paused edge with no reply owed puts NOTHING "
                             "on the wire",
              c.fresh().empty() && c.p->pending() == 0);
        rig.dbg->detach(other);
    }
    {
        // Rule 4: an inspection packet on a machine resumed behind the client.
        Rig    rig;
        LogTap log;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        rig.dbg->run(other);
        rig.emu.run_frame();
        const auto r = c.send_raw(pkt("g"), 1);
        c.tick();
        c.tick();
        check("GDB-STOP-06", "g on a machine another client resumed PAUSES it first (the reply is "
                             "the registers at a boundary), logs 're-paused', and sends no stop "
                             "reply — none is owed",
              r.size() == 1 && r[0].size() == 56 && rig.dbg->state().paused &&
                  rig.dbg->state().pause_reason.by == rig.gdb->client() && c.fresh().empty() &&
                  log.count("re-paused the machine") == 1,
              join(r));
        rig.dbg->detach(other);
    }
    {
        // A `c` another client's pause answers.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        c.send_raw(pkt("c"), 1);
        rig.emu.run_frame();
        auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        rig.dbg->pause(other);
        c.tick();
        const auto r = c.fresh();
        rig.dbg->run(other);
        rig.emu.run_frame();
        rig.dbg->pause(other);
        c.tick();
        check("GDB-STOP-07", "a c answered by another client's pause is T02thread:1; (something "
                             "else stopped it) — and the next foreign stop, with nothing owed, "
                             "sends nothing (exactly once)",
              r.size() == 1 && r[0] == "T02thread:1;" && c.fresh().empty(), join(r));
        rig.dbg->detach(other);
    }
    {
        // A breakpoint stop and a Ctrl-C in the same tick: ONE reply, the
        // real reason.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        c.cmd("Z0,8002,1");
        c.send_raw(pkt("c"), 1);
        run_until_paused(rig);
        c.p->send(std::string(1, '\x03'));
        c.tick();
        c.tick();
        const auto r = c.fresh();
        check("GDB-STOP-08", "a Ctrl-C that arrives after the breakpoint stopped the machine but "
                             "before it was reported gets the breakpoint's stop reply, once",
              r.size() == 1 && r[0] == "T05thread:1;swbreak:;", join(r));
    }
    {
        // `?` in the same tick as a breakpoint stop that answers a `c`: ONE
        // reply, the breakpoint's.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        c.cmd("Z0,8002,1");
        c.send_raw(pkt("c"), 1);
        run_until_paused(rig);
        c.p->send(pkt("?"));
        c.tick();
        c.tick();
        const auto r = c.fresh();
        check("GDB-STOP-11", "a ? that arrives after a breakpoint stopped a c but before it was "
                             "reported is answered by that stop reply, once — never two T packets",
              r.size() == 1 && r[0] == "T05thread:1;swbreak:;", join(r));
    }
    {
        // A magic breakpoint (unowned) during a `c`.
        Rig    rig;
        Client c(rig);
        open_session(c);
        rig.dbg->set_magic_breakpoint(true);
        load_prog(rig, 0x8000, {0x00, 0xED, 0xFF, 0x18, 0xFE});
        set_pc(rig, 0x8000);
        const std::string s = cont_and_stop(rig, c);
        check("GDB-STOP-09", "a stop the client did not cause — the magic breakpoint — answers "
                             "its c with T02thread:1;",
              s == "T02thread:1;" && rig.dbg->state().paused, s);
    }
    {
        // Another client's STEP answers this client's `c`: not a step of its
        // own, so T02.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        c.send_raw(pkt("c"), 1);
        rig.emu.run_frame();
        auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        rig.dbg->step_into(other);
        c.tick();
        const auto r = c.fresh();
        check("GDB-STOP-12", "a c answered by another client's step (reason Step, by that "
                             "client) is T02thread:1; — only this client's own step or run-to "
                             "is T05",
              r.size() == 1 && r[0] == "T02thread:1;" &&
                  rig.dbg->state().pause_reason.kind == jnext::dbg::PauseReason::Kind::Step,
              join(r));
        rig.dbg->detach(other);
    }
    {
        // `?` during an outstanding `c` (running): it stops the machine, and
        // the one reply is the `c`'s — never a second T for the `?`.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        c.send_raw(pkt("c"), 1);
        rig.emu.run_frame();
        c.p->send(pkt("?"));
        std::vector<std::string> got;
        for (int i = 0; i < 4; ++i) {
            c.tick();
            for (auto& b : c.fresh()) got.push_back(b);
        }
        check("GDB-STOP-14", "? during an outstanding c pauses the running machine (as this "
                             "client) and ONE stop reply answers both",
              got.size() == 1 && got[0] == "T02thread:1;" && rig.dbg->state().paused &&
                  rig.dbg->state().pause_reason.by == rig.gdb->client(),
              join(got));
    }
    {
        // An inspection packet while a `c` is outstanding means the client
        // already counts the machine stopped: the owed reply is ABANDONED —
        // whether the machine still runs, or has stopped with its edge not yet
        // sent (the two orders a real client can produce).
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        c.send_raw(pkt("c"), 1);
        rig.emu.run_frame();
        // EVERY packet the `g`'s pump wrote, not just the first: a `T` glued
        // behind the `g` reply is exactly the defect (review round 2).
        std::vector<std::string> g_all;
        c.p->send(pkt("g"));
        for (int i = 0; i < 4; ++i) {
            c.tick();
            for (auto& b : c.fresh()) g_all.push_back(b);
        }
        const std::string g1     = g_all.empty() ? std::string() : g_all[0];
        const bool        quiet1 = g_all.size() == 1;

        c.cmd("Z0,8002,1");
        c.send_raw(pkt("c"), 1);
        run_until_paused(rig);
        c.p->send(pkt("m8000,2"));
        std::vector<std::string> got;
        for (int i = 0; i < 4; ++i) {
            c.tick();
            for (auto& b : c.fresh()) got.push_back(b);
        }
        check("GDB-STOP-15", "g on a running machine with a c outstanding is answered and NO T "
                             "follows; m after the c's stop but before its reply went out is "
                             "answered alone — the owed reply is abandoned, never glued on",
              g1.size() == 56 && quiet1 && got.size() == 1 && got[0].size() == 4 &&
                  rig.dbg->state().paused,
              g1 + " / " + join(got));
    }
    {
        // The same rule for EACH inspection verb (sibling-verbs rule): on a
        // machine RUNNING a `c`, the verb pauses it first, is answered, and the
        // `c`'s owed reply is abandoned — its pump writes exactly one packet,
        // and nothing follows. A verb that skipped the pause would leave the
        // machine running; one that kept the owed reply would glue a `T` on.
        struct Verb {
            const char* body;
            const char* reply;  // exact reply; "" = any non-empty, non-T
        };
        const Verb verbs[] = {
            {"G", ""},  // filled in with the paused machine's own registers
            {"p0", ""},
            {"Z0,9000,1", "OK"},
            {"z0,9005,1", "OK"},
            {"m8000,2", ""},
            {"M9000,1:aa", "OK"},
            {"X9000,1:b", "OK"},
            {"qRcmd,74696d65", "OK"},  // "time": O lines, then OK
        };
        bool        ok = true;
        std::string why;
        for (const Verb& v : verbs) {
            Rig    rig;
            Client c(rig);
            open_session(c);
            load_loop(rig);
            const std::string regs = c.cmd("g");
            if (std::string(v.body) == "z0,9005,1") c.cmd("Z0,9005,1");  // never executed
            c.send_raw(pkt("c"), 1);
            rig.emu.run_frame();
            const bool running = !rig.dbg->state().paused;
            const std::string body = std::string(v.body) == "G" ? "G" + regs : std::string(v.body);
            c.p->send(pkt(body));
            std::vector<std::string> all;
            for (int i = 0; i < 4; ++i) {
                c.tick();
                for (auto& b : c.fresh()) all.push_back(b);
            }
            // A `monitor` reply is O lines then its final `OK`.
            std::vector<std::string> finals;
            for (const auto& b : all)
                if (!(b.size() > 1 && b[0] == 'O' && b != "OK")) finals.push_back(b);
            const bool one = finals.size() == 1 && !finals[0].empty() && finals[0][0] != 'T' &&
                             (v.reply[0] == '\0' || finals[0] == v.reply);
            if (!(running && one && rig.dbg->state().paused &&
                  rig.dbg->state().pause_reason.by == rig.gdb->client())) {
                ok = false;
                why += std::string(v.body) + ": [" + join(all) + "] running=" +
                       (running ? "1" : "0") + " paused=" +
                       (rig.dbg->state().paused ? "1" : "0") + "; ";
            }
        }
        check("GDB-STOP-16", "each of G, p, Z, z, m, M, X and qRcmd sent while a c runs pauses the "
                             "machine first (as this client), gets exactly its own reply, and no T "
                             "follows — the c's owed reply is abandoned",
              ok, why);
    }
    {
        // `D` with a `c` outstanding: OK, and no late stop reply.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        c.cmd("Z0,8002,1");
        c.send_raw(pkt("c"), 1);
        const std::string d = c.cmd("D");
        run_until_paused(rig, 3);
        c.tick();
        check("GDB-STOP-10", "D while a c is outstanding answers OK and the session ends: the "
                             "breakpoint is gone, the machine runs on, nothing more is sent",
              d == "OK" && c.fresh().empty() && !rig.dbg->state().paused &&
                  rig.dbg->subscriptions(true).empty(),
              d);
    }
}

// ── GDB-MON — qRcmd, the monitor vocabulary (§4.3) ─────────────────────────

static void monitor_rows() {
    {
        Rig    rig(MachineType::ZXN_ISSUE2);
        Client c(rig);
        open_session(c);
        std::string fin;
        const auto  lines = c.monitor("mmu", fin);
        const auto  slots = rig.dbg->mmu_slots();
        bool        ok    = lines.size() == 9;
        for (std::size_t s = 0; ok && s < 8; ++s) {
            char want[64];
            std::snprintf(want, sizeof(want), "slot %u: page %02X (effective %02X)%s\n",
                          static_cast<unsigned>(s), slots[s].nr_page, slots[s].effective_page,
                          slots[s].is_rom ? " ROM" : "");
            ok = lines[s] == want;
        }
        ok = ok && lines.size() == 9 && lines[8].rfind("7FFD=", 0) == 0;
        std::string fin2;
        c.monitor("mmu 6 0x22", fin2);
        check("GDB-MON-01", "monitor mmu prints the eight slots (page, effective page, ROM) and "
                            "the paging ports as O packets, then OK; monitor mmu 6 0x22 maps "
                            "page 0x22 at slot 6",
              ok && fin == "OK" && fin2 == "OK" && rig.dbg->mmu_slots()[6].nr_page == 0x22,
              join(lines));
    }
    {
        Rig    rig(MachineType::ZXN_ISSUE2);
        Client c(rig);
        open_session(c);
        std::string f1, f2, f3, f4, f5;
        char        want[32];
        std::snprintf(want, sizeof(want), "NR 07 = %02X\n", rig.dbg->nextreg_peek(7));
        const auto  one  = c.monitor("nextreg 7", f1);
        const auto  all  = c.monitor("nextreg", f2);
        c.monitor("nextreg $7f 0x5a", f3);
        const auto  back = c.monitor("nextreg 0x7F", f5);
        const auto  bad  = c.monitor("nextreg 256", f4);
        check("GDB-MON-02", "monitor nextreg 7 prints one register through its read path, bare "
                            "nextreg all 256 in 16 lines, nextreg $7f 0x5a writes the user "
                            "register (read back 5A), and a register past 0xFF is E01",
              one.size() == 1 && one[0] == want && f1 == "OK" && all.size() == 16 && f2 == "OK" &&
                  f3 == "OK" && rig.dbg->nextreg_peek(0x7F) == 0x5A && back.size() == 1 &&
                  back[0] == "NR 7F = 5A\n" && f4 == "E01" && bad.empty(),
              join(one));
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        Z80Registers r = rig.emu.cpu().get_registers();
        r.I = 0x3F; r.R = 0x12; r.IM = 2; r.IFF1 = 1; r.IFF2 = 0;
        rig.emu.cpu().set_registers(r);
        std::string f1, f2, f3, f4;
        const auto  regs = c.monitor("regs", f1);
        c.monitor("set i 0x41", f2);
        c.monitor("set iff2 1", f3);
        c.monitor("set im 3", f4);
        const Z80Registers a = rig.emu.cpu().get_registers();
        check("GDB-MON-03", "monitor regs shows I R IFF1 IFF2 IM HALT MEMPTR; set i / set iff2 "
                            "write them (OK); set im 3 is E01 and writes nothing",
              regs.size() == 3 && regs[2].rfind("I=3F R=12 IFF1=1 IFF2=0 IM=2 HALT=0 MEMPTR=", 0) == 0 &&
                  f1 == "OK" && f2 == "OK" && f3 == "OK" && f4 == "E01" && a.I == 0x41 &&
                  a.IFF2 == 1 && a.IM == 2,
              join(regs));
    }
    {
        Rig    rig(MachineType::ZXN_ISSUE2);
        Client c(rig);
        open_session(c);
        std::uint8_t* p = rig.emu.mmu().nr_page_ptr(0x30);
        for (int i = 0; i < 20; ++i) p[0x100 + i] = static_cast<std::uint8_t>(0x40 + i);
        std::string f1, f2;
        const auto  d   = c.monitor("page 0x30 0x100 20", f1);
        c.monitor("page 0xfe 0", f2);
        check("GDB-MON-04", "monitor page 0x30 0x100 20 dumps the PHYSICAL page, 16 bytes a "
                            "line (two lines here), whatever is mapped; a ROM sentinel page is "
                            "E01",
              d.size() == 2 && d[0] == "page 30 +0100: 40 41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F\n" &&
                  d[1] == "page 30 +0110: 50 51 52 53\n" && f1 == "OK" && f2 == "E01",
              join(d));
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        std::string f1, f2;
        const auto  u = c.monitor("frobnicate 1 2", f1);
        const auto  h = c.monitor("help", f2);
        std::string all;
        for (const auto& l : h) all += l;
        bool every = true;
        for (const char* v : {"regs", "set ", "mmu", "nextreg", "page", "in PORT", "out PORT",
                              "sym", "time", "reset", "bp"})
            every = every && all.find(v) != std::string::npos;
        check("GDB-MON-05", "an unknown monitor command answers an O line 'unknown monitor "
                            "command' and OK — never the empty reply that would read as 'qRcmd "
                            "unsupported'; help names every verb and marks in/out perturbing",
              u.size() == 1 && u[0].find("unknown monitor command") != std::string::npos &&
                  f1 == "OK" && f2 == "OK" && every && all.find("PERTURBING") != std::string::npos,
              join(u));
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        std::string f1, f2, f3;
        const auto  soft = c.monitor("reset soft", f1);
        const bool  stays = rig.dbg->state().paused && pc(rig) == 0x0000;
        const auto  hard = c.monitor("reset hard", f2);
        c.monitor("reset sideways", f3);
        c.tick();
        check("GDB-MON-06", "monitor reset soft resets the machine and it STAYS stopped (PC 0, no "
                            "stop reply); reset hard with no loop driver is an O line + E01; a "
                            "bad kind is E01",
              f1 == "OK" && stays && soft.size() == 1 && f2 == "E01" && hard.size() == 1 &&
                  hard[0].find("hard reset not available") != std::string::npos && f3 == "E01" &&
                  c.fresh().empty(),
              join(soft) + " / " + join(hard));
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        std::string f1, f2, f3;
        c.monitor("out 0xfe 3", f1);
        const auto in = c.monitor("in 0x7ffe", f2);
        c.monitor("out 0x10000 1", f3);
        check("GDB-MON-07", "monitor out 0xfe 3 is a real OUT (border 3); in answers IN <port> = "
                            "value; a port past 0xFFFF is E01",
              f1 == "OK" && rig.dbg->ula_screen_regs().border == 3 && f2 == "OK" &&
                  in.size() == 1 && in[0].rfind("IN 7FFE = ", 0) == 0 && f3 == "E01",
              join(in));
    }
    {
        Rig rig;
        char path[] = "/tmp/gdb_rsp_test_map_XXXXXX";
        const int fd = mkstemp(path);
        const std::string map = "_main = $816A\n_helper = $8200\n";
        const bool wrote = fd >= 0 && write(fd, map.data(), map.size()) ==
                                          static_cast<ssize_t>(map.size());
        if (fd >= 0) close(fd);
        const auto loaded = rig.dbg->load_map(path, jnext::dbg::MapFormat::Simple);
        std::remove(path);
        Client c(rig);
        open_session(c);
        std::string f1, f2, f3, f4;
        const auto byname = c.monitor("sym _main", f1);
        const auto near   = c.monitor("sym 0x8170", f2);
        const auto exact  = c.monitor("sym $8200", f3);
        const auto none   = c.monitor("sym _nope", f4);
        check("GDB-MON-08", "monitor sym resolves through jnext's own symbol table: a name to "
                            "its address, an address to the symbol at or below it (+offset), "
                            "an exact address to the name, an unknown name said so",
              wrote && loaded.value == 2 && byname.size() == 1 && byname[0] == "_main = $816A\n" &&
                  near.size() == 1 && near[0] == "$8170 = _main+6\n" && exact.size() == 1 &&
                  exact[0] == "$8200 = _helper\n" && none.size() == 1 &&
                  none[0] == "no symbol _nope\n" && f1 == "OK" && f2 == "OK" && f4 == "OK",
              join(byname) + join(near) + join(exact));
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        std::string f1;
        const auto  t  = c.monitor("time", f1);
        const auto  tm = rig.dbg->time();
        check("GDB-MON-09", "monitor time prints frame, master cycle, T-states and the raster "
                            "position as the backend reports them",
              t.size() == 1 && f1 == "OK" &&
                  t[0] == "frame=" + std::to_string(tm.frame) + " cycle=" +
                              std::to_string(tm.master_cycle) + " tstates=" +
                              std::to_string(tm.tstates_total) + " vc=" +
                              std::to_string(tm.vc_raw) + " hc=" + std::to_string(tm.hc_raw) + "\n",
              join(t));
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        c.cmd("Z0,8002,1");
        const auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        jnext::dbg::Subscription s;
        s.kind      = jnext::dbg::EventKind::Mem;
        s.filter.lo = 0x9000;
        s.filter.hi = 0x9003;
        s.access    = jnext::dbg::Access::Write;
        rig.dbg->subscribe(other, s);
        jnext::dbg::Subscription t;  // a transient: hidden from every user list
        t.filter.lo = t.filter.hi = 0x8123;
        t.transient = true;
        rig.dbg->subscribe(other, t);
        std::string f1;
        const auto  l = c.monitor("bp", f1);
        const bool mine = l.size() == 2 && l[0].find("execute 8002") != std::string::npos &&
                          l[0].find("(this client)") != std::string::npos;
        const bool theirs = l.size() == 2 && l[1].find("mem 9000-9003 write") != std::string::npos &&
                            l[1].find("owner " + std::to_string(other)) != std::string::npos &&
                            l[1].find("(this client)") == std::string::npos;
        check("GDB-MON-10", "monitor bp lists every client's breakpoints and watchpoints with "
                            "the owner, this client's marked, and no transient (8123 absent)",
              f1 == "OK" && mine && theirs, join(l));
        rig.dbg->detach(other);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        const std::string bad = c.cmd("qRcmd,6a6");
        check("GDB-MON-11", "a qRcmd whose argument is not whole hex is E01", bad == "E01", bad);
    }
    {
        // qRcmd is in rule 4's pause-first set.
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        rig.dbg->run(other);
        std::string f1;
        c.monitor("regs", f1);
        check("GDB-MON-12", "a monitor command on a machine resumed behind the client pauses it "
                            "first",
              f1 == "OK" && rig.dbg->state().paused);
        rig.dbg->detach(other);
    }
}

// ── GDB-UNS — unsupported, honestly (§2 rows 26-33) ────────────────────────

static void unsupported_rows() {
    Rig    rig;
    LogTap log;
    Client c(rig);
    open_session(c);
    load_loop(rig);
    const std::string vq = c.cmd("vCont?");
    const std::string vc = c.cmd("vCont;c");
    check("GDB-UNS-01", "vCont? and vCont;c answer the empty reply (gdb then falls back to "
                        "c/s), and vCont;c does NOT resume the machine",
          vq.empty() && vc.empty() && rig.dbg->state().paused, "[" + vq + "][" + vc + "]");
    check("GDB-UNS-02", "bc and bs (reverse continue / step) answer the empty reply",
          c.cmd("bc").empty() && c.cmd("bs").empty() && rig.dbg->state().paused);
    const std::string na = c.cmd("QStartNoAckMode");
    c.cmd("qC");
    check("GDB-UNS-03", "QStartNoAckMode answers the empty reply and acks stay on: the next "
                        "packet is still acknowledged '+'",
          na.empty() && !c.w.acks.empty() && c.w.acks.back() == '+', "[" + na + "]");
    bool all_empty = true;
    for (const char* p : {"qOffsets", "qSymbol::", "qTStatus", "R00", "vRun;", "vKill;1",
                          "vCtrlC", "QNonStop:1", "qHostInfo", "Y", "C05", "S05"})
        all_empty = all_empty && c.cmd(p).empty();
    check("GDB-UNS-04", "qOffsets, qSymbol, qTStatus, R, vRun, vKill, vCtrlC, QNonStop, "
                        "qHostInfo, C/S with a signal and an unknown letter all answer the empty "
                        "reply, each logged at debug with the packet",
          all_empty && log.count("gdb: unsupported packet \"qOffsets\"") == 1 &&
              log.count("gdb: unsupported packet \"Y\"") == 1 && rig.dbg->state().paused);
}

// ── GDB-GEN — the generic stub minimum (§2 rows 22-25, 32) ──────────────────

static void generic_rows() {
    Rig    rig;
    Client c(rig);
    open_session(c);
    const std::string hg = c.cmd("Hg0"), hc = c.cmd("Hc-1"), qc = c.cmd("qC"),
                      qa = c.cmd("qAttached"), qa1 = c.cmd("qAttached:1"),
                      qf = c.cmd("qfThreadInfo"), qs = c.cmd("qsThreadInfo"),
                      t1 = c.cmd("T1");
    check("GDB-GEN-01", "the generic minimum a stock gdb sends at connect: Hg0/Hc-1 OK, qC "
                        "QC1, qAttached 1 (a gdb quit detaches, never kills), qfThreadInfo m1 "
                        "then qsThreadInfo l, T1 OK",
          hg == "OK" && hc == "OK" && qc == "QC1" && qa == "1" && qa1 == "1" && qf == "m1" &&
              qs == "l" && t1 == "OK",
          hg + " " + hc + " " + qc + " " + qa + " " + qf + " " + qs + " " + t1);
}

// ── GDB-SES — the session (WP-3 / WP-4) ────────────────────────────────────

static std::string le32(std::uint32_t v) {
    std::string s(4, '\0');
    for (int i = 0; i < 4; ++i) s[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
    return s;
}

static void session_rows() {
    {
        Rig rig;
        const bool before = rig.dbg->attached();
        Client c(rig);
        const auto cid = rig.gdb->client();
        const bool on  = cid != jnext::dbg::CLIENT_NONE && rig.dbg->attached();
        c.p->close();
        c.tick();
        c.tick();
        check("GDB-SES-01", "a client is attached to the backend when it is admitted (RSP has no "
                            "session command) and detached when it hangs up",
              !before && on && rig.gdb->client() == jnext::dbg::CLIENT_NONE &&
                  !rig.dbg->attached());
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        load_loop(rig);
        const bool held = rig.dbg->state().paused;
        const std::string d = c.cmd("D");
        c.tick();
        check("GDB-SES-02", "D answers OK, detaches (the pause its ? made is released — the "
                            "machine runs) and the server closes the connection",
              held && d == "OK" && !rig.dbg->state().paused && c.p->closed_by_server() &&
                  rig.gdb->client() == jnext::dbg::CLIENT_NONE,
              d);
    }
    {
        Rig    rig;
        Client c(rig);
        open_session(c);
        c.cmd("Z0,8002,1");
        const auto r = c.send_raw(pkt("k"), 2);
        c.tick();
        check("GDB-SES-03", "k is a detach without a reply (jnext never exits on k): '+', no "
                            "packet, the connection closed, the breakpoint gone, the machine "
                            "running",
              r.empty() && c.w.acks.back() == '+' && c.p->closed_by_server() &&
                  rig.dbg->subscriptions(true).empty() && !rig.dbg->state().paused);
    }
    {
        Rig    rig;
        LogTap log;
        Client a(rig);
        open_session(a);
        auto   second = rig.lsn->connect();
        second->send(pkt("?"));
        rig.pump();
        rig.pump();
        const std::string second_got = second->take();
        const auto        still = a.cmd("qC");
        check("GDB-SES-04", "a second connection is closed with nothing sent (its bytes never "
                            "executed), logged at warn; the first client is untouched",
              second_got.empty() && second->closed_by_server() && still == "QC1" &&
                  log.count("refused a connection") == 1,
              still);
    }
    {
        // DZRP and GDB RSP on ONE backend at once — what --dzrp-port and
        // --gdb-port together do (WP-4): two listeners, one client each.
        Rig  rig;
        auto dz  = std::make_unique<jnext::remote::dzrp::DzrpServer>(*rig.dbg);
        auto dzl = std::make_unique<FakeListener>();
        FakeListener* dzlp = dzl.get();
        dz->server().open(std::move(dzl), "127.0.0.1", 0);
        rig.dbg->add_service(dz->server());

        Client g(rig);
        auto   d = dzlp->connect();
        rig.pump();
        const std::string init = std::string("\x02\x02\x00", 3) + "dz" + std::string(1, '\0');
        d->send(le32(static_cast<std::uint32_t>(init.size())) + std::string(1, '\x01') +
                std::string(1, '\x01') + init);
        const std::string q = g.cmd("?");
        rig.pump();
        const std::string dzr = d->take();
        const auto        gcid = rig.gdb->client();
        const auto        subs = [&] {
            g.cmd("Z0,8002,1");
            return rig.dbg->subscriptions(false);
        }();
        g.cmd("D");
        const auto after = rig.dbg->subscriptions(false);
        check("GDB-SES-05", "DZRP and GDB served by one backend at once: the DZRP CMD_INIT is "
                            "answered, the gdb ? is answered T05, each its own client; the gdb "
                            "client's D removes only its own breakpoint and leaves the DZRP "
                            "session attached",
              q == "T05thread:1;" && dzr.size() > 5 && static_cast<unsigned char>(dzr[4]) == 1 &&
                  gcid != jnext::dbg::CLIENT_NONE && subs.size() == 1 && subs[0].owner == gcid &&
                  after.empty() && rig.dbg->attached() && !d->closed_by_server(),
              q);
        rig.dbg->remove_service(dz->server());
        dz.reset();
    }
    {
        // A new session after D starts clean: a new client, no inherited
        // breakpoints, a working handshake.
        Rig    rig;
        Client a(rig);
        open_session(a);
        a.cmd("Z0,8002,1");
        const auto first = rig.gdb->client();
        a.cmd("D");
        a.tick();
        Client b(rig);
        const std::string s = b.cmd("qSupported");
        const std::string q = b.cmd("?");
        check("GDB-SES-06", "a client that reconnects after D gets a fresh session: a new "
                            "backend client, no breakpoints from the old one, and the handshake "
                            "answered as the first time",
              rig.gdb->client() != first && rig.gdb->client() != jnext::dbg::CLIENT_NONE &&
                  rig.dbg->subscriptions(true).empty() && s.rfind("PacketSize=", 0) == 0 &&
                  q == "T05thread:1;",
              s + " / " + q);
    }
    {
        // The adapter serves many sessions: a breakpoint the first one set at A
        // must not make the next session's Z0 at A a silent no-op.
        Rig    rig;
        Client a(rig);
        open_session(a);
        load_loop(rig);
        a.cmd("Z0,8002,1");
        a.cmd("D");
        a.tick();
        rig.dbg->pause(rig.dbg->attach({"hold", jnext::dbg::ClientKind::Test}).value);
        set_pc(rig, 0x8000);
        Client b(rig);
        open_session(b);
        const std::string z = b.cmd("Z0,8002,1");
        const std::string s = cont_and_stop(rig, b);
        check("GDB-SES-07", "after D, the next session's Z0 at the same address is a real "
                            "breakpoint: one subscription, owned by the new client, and c stops "
                            "on it",
              z == "OK" && s == "T05thread:1;swbreak:;" && pc(rig) == 0x8002 &&
                  rig.dbg->subscriptions(false).size() == 1 &&
                  rig.dbg->subscriptions(false)[0].owner == rig.gdb->client(),
              z + " / " + s);
    }
}

int main() {
    std::printf("gdb_rsp_test — the GDB RSP adapter over T's fake transport (GH #281)\n");
    framing_rows();
    supported_rows();
    register_rows();
    memory_rows();
    breakpoint_rows();
    reinsert_rows();
    step_rows();
    stop_rows();
    monitor_rows();
    unsupported_rows();
    generic_rows();
    session_rows();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass,
                g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
