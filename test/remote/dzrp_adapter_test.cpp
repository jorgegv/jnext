// jnext::remote::dzrp — the DeZog Remote Protocol adapter (GH #12, epic #276
// package D). The fake-transport unit suite of dzrp-frontend.md §7.3.
//
// WHAT IS UNDER TEST. `DzrpServer` (src/remote/dzrp/dzrp_server.*) and its
// framing (src/remote/dzrp/dzrp_frame.*), run inside the PRODUCTION
// `remote::Server` over T's in-memory `FakeListener` / `FakePeer`, on a real
// `Emulator` + `Debugger`, driven through `Debugger::pump()` exactly as a loop
// owner drives it. Only the kernel is replaced. Every row asserts BYTES ON THE
// WIRE and, where a command touches the machine, MACHINE STATE — never only
// the adapter's own bookkeeping.
//
//   DZRP-FR-*   framing (WP-1): both length conventions, split delivery, the
//               seq rules, the 16 MiB cap, the chunk timeout, CMD_LOOPBACK,
//               one command per pass, the unknown-command path.
//
// PROVENANCE OF THE TEST DESIGN. Several rows restate, for this adapter and
// over the fake, checks from the owner's dezogif_ng DZRP conformance suite —
// `/home/jorgegv/src/spectrum/dezogif_ng/test/dzrp/conformance.py` @ commit
// 709ae7d77d444e0e14d88d5c6114b2a7c18e2be6 (GPLv3, as jnext). No code is
// copied: those checks drive a live remote over TCP, which the fake cannot
// host. The rows that restate one cite its check id (C2, C5, C9, C18, …).
//
// EVERY WAIT IS BOUNDED. No row sleeps: the chunk timeout runs on a fake clock
// (`g_now`), every pump loop has an iteration cap, and T's fake is finite.
//
// Run: ./build/test/dzrp_adapter_test

#include "remote/dzrp/dzrp_frame.h"
#include "remote/dzrp/dzrp_server.h"
#include "remote/fake_transport.h"
#include "remote/transport.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "debug/debugger.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../row_id.h"

using jnext::dbg::Debugger;
using jnext::dbg::PumpBudget;
using jnext::remote::FakeListener;
using jnext::remote::FakePeer;
using namespace jnext::remote::dzrp;
using SteadyClock = std::chrono::steady_clock;
using std::chrono::milliseconds;

// ── Tiny test harness (matches remote_transport_test) ──────────────────────

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

static std::string hex(const std::string& s, std::size_t max = 48) {
    std::string out;
    char        b[4];
    for (std::size_t i = 0; i < s.size() && i < max; ++i) {
        std::snprintf(b, sizeof(b), "%02X ", static_cast<unsigned char>(s[i]));
        out += b;
    }
    if (s.size() > max) out += "...";
    return out;
}

// ── The fake clock the chunk timeout reads ─────────────────────────────────

static SteadyClock::time_point g_now = SteadyClock::time_point{} + std::chrono::hours(1);

static void advance(long ms) { g_now += milliseconds(ms); }

// ── Log capture ────────────────────────────────────────────────────────────

/// Adds a ring sink to the `debugger` channel for its lifetime.
struct LogTap {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> ring =
        std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(512);
    LogTap() { Log::debugger()->sinks().push_back(ring); }
    ~LogTap() {
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

// ── Wire helpers ───────────────────────────────────────────────────────────

static std::string le32(std::uint32_t v) {
    std::string s(4, '\0');
    for (int i = 0; i < 4; ++i) s[i] = static_cast<char>((v >> (8 * i)) & 0xFF);
    return s;
}

/// A command frame with an EXPLICIT length field (for the malformed rows).
static std::string frame_len(std::uint32_t len, std::uint8_t seq, std::uint8_t id,
                             const std::string& payload) {
    return le32(len) + static_cast<char>(seq) + static_cast<char>(id) + payload;
}

/// A well-formed command frame: the length counts the PAYLOAD ONLY.
static std::string frame(std::uint8_t seq, std::uint8_t id, const std::string& payload = {}) {
    return frame_len(static_cast<std::uint32_t>(payload.size()), seq, id, payload);
}

/// One response as it came off the wire.
struct Resp {
    std::uint32_t len = 0;  // the length FIELD, as sent
    std::uint8_t  seq = 0;
    std::string   payload;  // everything after the seq byte
};

/// Split every COMPLETE response frame off the front of `buf`.
static std::vector<Resp> take_frames(std::string& buf) {
    std::vector<Resp> out;
    for (;;) {
        if (buf.size() < 5) break;
        std::uint32_t len = 0;
        for (int i = 0; i < 4; ++i)
            len |= static_cast<std::uint32_t>(static_cast<unsigned char>(buf[i])) << (8 * i);
        if (len == 0 || buf.size() < 4 + static_cast<std::size_t>(len)) break;
        Resp r;
        r.len     = len;
        r.seq     = static_cast<std::uint8_t>(buf[4]);
        r.payload = buf.substr(5, len - 1);
        out.push_back(std::move(r));
        buf.erase(0, 4 + len);
    }
    return out;
}

static std::string pattern(std::size_t n, unsigned salt) {
    std::string s(n, '\0');
    for (std::size_t i = 0; i < n; ++i)
        s[i] = static_cast<char>((i * 7u + salt + (i >> 8)) & 0xFF);
    return s;
}

// ── The rig: a real machine, the backend, the adapter over T's fake ────────

struct Rig {
    Emulator                    emu;
    std::unique_ptr<Debugger>   dbg;
    std::unique_ptr<DzrpServer> dzrp;
    FakeListener*               lsn = nullptr;

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

        dbg  = std::make_unique<Debugger>(emu);
        dzrp = std::make_unique<DzrpServer>(*dbg, [] { return g_now; });
        auto l = std::make_unique<FakeListener>();
        lsn    = l.get();
        dzrp->server().open(std::move(l), "127.0.0.1", 0);
        dbg->add_service(dzrp->server());
    }

    ~Rig() {
        dzrp.reset();  // before the Debugger it unregisters from
        dbg.reset();
    }

    /// One loop-owner tick's worth of service while running: `PumpBudget{}`.
    void pump(int n = 1) {
        for (int i = 0; i < n; ++i) dbg->pump(PumpBudget{});
    }

    /// Connect a client, and let one pump admit it.
    std::shared_ptr<FakePeer> connect() {
        auto p = lsn->connect();
        pump();
        return p;
    }
};

/// Send `bytes`, then pump until `want` complete responses have arrived (or
/// `max_pumps` ran). Bytes left over (a partial frame) stay in `rx`.
static std::vector<Resp> exchange(Rig& rig, FakePeer& p, std::string& rx,
                                  const std::string& bytes, std::size_t want = 1,
                                  int max_pumps = 16) {
    if (!bytes.empty()) p.send(bytes);
    std::vector<Resp> got;
    for (int i = 0; i < max_pumps && got.size() < want; ++i) {
        rig.pump();
        rx += p.take();
        auto more = take_frames(rx);
        got.insert(got.end(), more.begin(), more.end());
    }
    return got;
}

// ── DZRP-FR — framing over T's fake (WP-1) ─────────────────────────────────

static void framing_rows() {
    {
        Rig         rig;
        auto        p = rig.connect();
        std::string rx;

        // The two conventions in one exchange: the command's length counts the
        // payload (32), the response's counts from the seq byte (33).
        const std::string data = pattern(32, 1);
        const auto r = exchange(rig, *p, rx, frame(7, CMD_LOOPBACK, data));
        check("DZRP-FR-01", "CMD_LOOPBACK echoes its payload; the reply's length field counts "
                            "from the seq byte (33 for 32 bytes) and carries the command's seq "
                            "(conformance C4)",
              r.size() == 1 && r[0].len == 33 && r[0].seq == 7 && r[0].payload == data,
              r.empty() ? "no reply" : "len=" + std::to_string(r[0].len) + " " + hex(r[0].payload));

        const auto e = exchange(rig, *p, rx, frame(8, CMD_LOOPBACK));
        check("DZRP-FR-02", "an empty CMD_LOOPBACK is answered with the seq alone: length 1",
              e.size() == 1 && e[0].len == 1 && e[0].seq == 8 && e[0].payload.empty());

        // Sizes across a byte counter, a 16-bit length byte, and the ceiling
        // (conformance C5's list, minus its ESP-specific +IPD boundaries).
        bool        all = true;
        std::string why;
        std::uint8_t seq = 10;
        for (std::size_t n : {std::size_t{1}, std::size_t{255}, std::size_t{256},
                              std::size_t{300}, std::size_t{4096}, LOOPBACK_MAX_BYTES}) {
            const std::string d = pattern(n, static_cast<unsigned>(n));
            const auto        q = exchange(rig, *p, rx, frame(seq, CMD_LOOPBACK, d));
            if (q.size() != 1 || q[0].seq != seq || q[0].len != n + 1 || q[0].payload != d) {
                all = false;
                why = "n=" + std::to_string(n);
                break;
            }
            ++seq;
        }
        check("DZRP-FR-03", "CMD_LOOPBACK is exact at 1, 255, 256, 300, 4096 and the 8192-byte "
                            "maximum (conformance C5)",
              all, why);

        LogTap log;
        const auto big = exchange(rig, *p, rx,
                                  frame(20, CMD_LOOPBACK, pattern(LOOPBACK_MAX_BYTES + 1, 3)));
        const auto after = exchange(rig, *p, rx, frame(21, CMD_LOOPBACK, "ok"));
        check("DZRP-FR-04", "an 8193-byte CMD_LOOPBACK is declined in-band — a seq-only reply "
                            "and a warn line — and the connection serves on (conformance C18)",
              big.size() == 1 && big[0].seq == 20 && big[0].len == 1 &&
                  log.count("exceeds the 8192-byte maximum") == 1 && after.size() == 1 &&
                  after[0].payload == "ok" && !p->closed_by_server());
    }
    {
        // THE COMMAND CONVENTION, PROVED BY VIOLATING IT (conformance C2): a
        // length that counts seq and id too (the symmetric reading) leaves the
        // adapter waiting for two bytes that never come — no reply.
        Rig         rig;
        auto        p = rig.connect();
        std::string rx;
        const std::string d = "abcd";
        const auto r = exchange(rig, *p, rx, frame_len(6, 3, CMD_LOOPBACK, d), 1, 8);
        check("DZRP-FR-05", "a command whose length counts seq and id as well is NOT answered: "
                            "the adapter waits for the two bytes it was promised",
              r.empty() && rx.empty() && !p->closed_by_server());
        // ...and those two bytes complete it: the frame was framed on the length.
        const auto done = exchange(rig, *p, rx, "ef");
        check("DZRP-FR-06", "and the two bytes it waited for complete that frame, answered as "
                            "a 6-byte payload",
              done.size() == 1 && done[0].seq == 3 && done[0].payload == "abcdef");
    }
    {
        // FRAMED ON THE LENGTH, NOT ON CONTENT (conformance C9): two commands
        // in one send are answered separately, in order, each with its own seq.
        Rig         rig;
        auto        p = rig.connect();
        std::string rx;
        const std::string two = frame(1, CMD_LOOPBACK, std::string("A\0B", 3)) +
                                frame(2, CMD_LOOPBACK, "CD");
        const auto r = exchange(rig, *p, rx, two, 2);
        check("DZRP-FR-07", "two commands in one send are answered separately and in order, "
                            "each exactly its own payload (framed on the length)",
              r.size() == 2 && r[0].seq == 1 && r[0].payload == std::string("A\0B", 3) &&
                  r[1].seq == 2 && r[1].payload == "CD");
    }
    {
        // SPLIT DELIVERY: one byte per pump. Each partial pass is Idle, and the
        // command runs once, on the pass that completes it.
        Rig         rig;
        auto        p = rig.connect();
        std::string rx;
        const std::string f = frame(9, CMD_LOOPBACK, pattern(40, 9));
        int early = 0;
        for (std::size_t i = 0; i + 1 < f.size(); ++i) {
            p->send(f.substr(i, 1));
            rig.pump();
            rx += p->take();
            if (!rx.empty()) ++early;
        }
        const auto r = exchange(rig, *p, rx, f.substr(f.size() - 1));
        check("DZRP-FR-08", "a command delivered one byte per pump is answered once, when its "
                            "last byte arrives, and not before",
              early == 0 && r.size() == 1 && r[0].seq == 9 && r[0].payload == pattern(40, 9),
              "early=" + std::to_string(early));
    }
    {
        // SEQ ECHO — VERBATIM. DZRP 2.2.0 narrowed the client's range to 1..15
        // with bits 4-7 "unused"; 2.0/2.1 clients use 1..255. DeZog compares the
        // echoed byte exactly, so masking it to 4 bits would break every
        // pre-2.2 client from seq 16 on.
        Rig         rig;
        auto        p = rig.connect();
        std::string rx;
        bool        all = true;
        std::string why;
        for (int s : {1, 15, 16, 0x80, 0xF0, 255}) {
            const auto r = exchange(rig, *p, rx, frame(static_cast<std::uint8_t>(s), CMD_LOOPBACK, "x"));
            if (r.size() != 1 || r[0].seq != s) {
                all = false;
                why = "seq " + std::to_string(s);
                break;
            }
        }
        check("DZRP-FR-09", "the seq byte is echoed verbatim for 1, 15, 16, 0x80, 0xF0 and 255 — "
                            "both the 2.2.0 range and the 2.0/2.1 one (conformance C6)",
              all, why);
    }
    {
        // SEQ 0 IS A PROTOCOL ERROR: it is the notifications' number, so a
        // command carrying it cannot be answered. Close, as the CSpect plugin
        // does on a protocol error.
        Rig         rig;
        LogTap      log;
        auto        p = rig.connect();
        std::string rx;
        const auto r = exchange(rig, *p, rx, frame(0, CMD_LOOPBACK, "zz"), 1, 4);
        check("DZRP-FR-10", "a command with seq 0 closes the connection with a warn line and "
                            "no reply",
              r.empty() && p->closed_by_server() &&
                  log.count("sequence number 0 (reserved for notifications)") == 1);
    }
    {
        // THE 16 MiB CAP — refused on the LENGTH FIELD, before any of the
        // claimed payload is read (and so before it could be buffered).
        Rig         rig;
        LogTap      log;
        auto        p = rig.connect();
        std::string rx;
        p->send(le32(MAX_PAYLOAD_BYTES + 1));
        rig.pump(2);
        check("DZRP-FR-11", "a length one byte over the 16 MiB cap closes the connection as "
                            "soon as the 4 length bytes are in",
              p->closed_by_server() && log.count("exceeds the 16777216-byte cap") == 1);
    }
    {
        Rig         rig;
        LogTap      log;
        auto        p = rig.connect();
        p->send(frame_len(MAX_PAYLOAD_BYTES, 1, CMD_LOOPBACK, "partial"));
        rig.pump(2);
        check("DZRP-FR-12", "a length exactly AT the cap is not refused: the frame is read on",
              !p->closed_by_server() && log.count("exceeds") == 0);
    }
    {
        // THE CHUNK TIMEOUT, on the fake clock: a frame that stops mid-way is a
        // truncated stream at 5 s — not a hair before.
        Rig         rig;
        LogTap      log;
        auto        p = rig.connect();
        p->send(frame(4, CMD_LOOPBACK, "abcdef").substr(0, 3));
        rig.pump();
        advance(4999);
        rig.pump();
        const bool alive = !p->closed_by_server();
        advance(1);
        rig.pump();
        check("DZRP-FR-13", "a frame that stops mid-way survives 4999 ms of silence and is "
                            "closed as truncated at 5000 ms",
              alive && p->closed_by_server() && log.count("truncated frame: 3 bytes") == 1);
    }
    {
        // ...measured from the LAST byte that advanced the frame (DeZog's own
        // "timeout between data chunks"), so a slow link that keeps delivering
        // is never cut off: a byte every 4 s for 40 s of fake time.
        Rig         rig;
        auto        p = rig.connect();
        std::string rx;
        const std::string f = frame(5, CMD_LOOPBACK, "slowly");
        for (std::size_t i = 0; i + 1 < f.size(); ++i) {
            p->send(f.substr(i, 1));
            rig.pump();
            advance(4000);
            rig.pump();
        }
        const auto r = exchange(rig, *p, rx, f.substr(f.size() - 1));
        check("DZRP-FR-14", "a frame trickled one byte every 4 s is never timed out and is "
                            "answered — the timeout runs from the last progress, not the first "
                            "byte",
              !p->closed_by_server() && r.size() == 1 && r[0].payload == "slowly");
    }
    {
        // ONE COMMAND PER PASS, AND THE REST KEPT: three commands in one send
        // while the machine RUNS (so each pump services exactly one). The
        // parser holds one frame and never reads past it, so the other two
        // must still be in the transport's buffer — nothing lost.
        Rig         rig;
        auto        p = rig.connect();
        std::string rx;
        p->send(frame(1, CMD_LOOPBACK, "one") + frame(2, CMD_LOOPBACK, "two") +
                frame(3, CMD_LOOPBACK, "three"));
        rig.pump();
        rx += p->take();
        const auto first = take_frames(rx);
        rig.pump(2);
        rx += p->take();
        const auto rest = take_frames(rx);
        check("DZRP-FR-15", "three pipelined commands while running: one pump answers exactly "
                            "one, the next two answer the other two, in order, none lost",
              !rig.dbg->state().paused && first.size() == 1 && first[0].payload == "one" &&
                  rest.size() == 2 && rest[0].payload == "two" && rest[1].payload == "three");
    }
    {
        // THE UNKNOWN-COMMAND PATH: reported, never silent, and the stream
        // stays in sync behind it (dezogif_ng issue #7's lesson).
        Rig         rig;
        LogTap      log;
        auto        p = rig.connect();
        std::string rx;
        const auto u99 = exchange(rig, *p, rx, frame(1, 99, "\x01\x02\x03"));
        const auto u0  = exchange(rig, *p, rx, frame(2, 0));
        const auto ok  = exchange(rig, *p, rx, frame(3, CMD_LOOPBACK, "sync"));
        check("DZRP-FR-16", "an unknown command id (99, and the reserved 0) gets a seq-only reply "
                            "and a warn line naming it; the next command is answered in sync",
              u99.size() == 1 && u99[0].seq == 1 && u99[0].len == 1 && u0.size() == 1 &&
                  u0[0].seq == 2 && u0[0].len == 1 &&
                  log.count("unsupported DZRP command 99") == 1 &&
                  log.count("unsupported DZRP command 0") == 1 && ok.size() == 1 &&
                  ok[0].payload == "sync");
    }
    {
        // ONE CLIENT AT A TIME (design §2 row 1): the second is accepted and
        // closed with nothing sent; the first is untouched.
        Rig         rig;
        auto        a = rig.connect();
        auto        b = rig.connect();
        std::string rx;
        rig.pump(2);
        const std::string b_got = b->take();
        const auto r = exchange(rig, *a, rx, frame(1, CMD_LOOPBACK, "first"));
        check("DZRP-FR-17", "a second concurrent client is closed with nothing sent, and the "
                            "first is still served",
              b->closed_by_server() && b_got.empty() && r.size() == 1 &&
                  r[0].payload == "first");
    }
    {
        // After a protocol error the listener is still open, and a NEW client
        // starts from a clean parser.
        Rig         rig;
        auto        a = rig.connect();
        a->send(frame(0, CMD_LOOPBACK));
        rig.pump(2);
        auto        b = rig.connect();
        std::string rx;
        const auto r = exchange(rig, *b, rx, frame(6, CMD_LOOPBACK, "again"));
        check("DZRP-FR-18", "after a protocol error closed one client, the next one is served",
              a->closed_by_server() && r.size() == 1 && r[0].payload == "again");
    }
    {
        // A client that hangs up mid-frame: the partial frame is dropped with
        // the session, and the next client's first frame is parsed from byte
        // 0 — not glued onto the leftover.
        Rig         rig;
        auto        a = rig.connect();
        a->send(frame(1, CMD_LOOPBACK, "unfinished").substr(0, 9));
        rig.pump();
        a->close();
        rig.pump(3);
        auto        b = rig.connect();
        std::string rx;
        const auto r = exchange(rig, *b, rx, frame(2, CMD_LOOPBACK, "fresh"));
        check("DZRP-FR-19", "a client that hangs up mid-frame leaves no residue: the next "
                            "client's first command is parsed cleanly",
              r.size() == 1 && r[0].seq == 2 && r[0].payload == "fresh");
    }
}

int main() {
    std::printf("dzrp_adapter_test — the DZRP adapter over T's fake transport (GH #12)\n");
    framing_rows();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass,
                g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
