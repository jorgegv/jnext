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
//   DZRP-SES-*  the session (WP-2): CMD_INIT (2.2.0, attach, pause only a
//               running machine), CMD_CLOSE, detach on a hang-up or reset
//               (SES-01), commands before CMD_INIT, the version record.
//   DZRP-SUP-*  CMD_GET_SUPPORTED_COMMANDS: the bitfield and the dispatcher
//               are one table, checked for every id 0..255.
//   DZRP-REG-*  CMD_GET/SET_REGISTER (the spec's numbers = DeZog's Z80_REG),
//               CMD_INTERRUPT_ON_OFF.
//   DZRP-MEM-*  CMD_READ/WRITE_MEM, side-effect-free reads (+3 latch).
//   DZRP-BANK-* CMD_WRITE_BANK (legacy), CMD_READ/WRITE_BANK_MEM, page bounds,
//               DeZog 3.8's ROM bank 0xFF.
//   DZRP-SLOT-* CMD_SET_SLOT, including 0xFF = ROM as NEXTREG 0x50 does it.
//   DZRP-NR-*, DZRP-PORT-*, DZRP-BRD-*  CMD_GET_TBBLUE_REG, CMD_READ/WRITE_PORT,
//               the legacy CMD_SET_BORDER.
//   DZRP-MAL-*  every fixed-length command, one byte short.
//   DZRP-BRK-*, DZRP-CONT-*, DZRP-NTF-*, DZRP-PAUSE-*, DZRP-TEMP-*, DZRP-BOI-*,
//   DZRP-RST-*  WP-3: CMD_ADD/REMOVE_BREAKPOINT, CMD_CONTINUE (reply before
//               anything runs, GH #221 step-off through run(), a refused
//               resume), CMD_PAUSE (NTF only when it stopped something),
//               NTF_PAUSE (reason, address, bank byte, the post-frame flush,
//               exactly once), temp-beats-user (F8), long addresses (F7),
//               break on interrupt, a hard reset sends nothing.
//   DZRP-EDGE-* every bank / range / length check the other rows did not
//               already pin from both sides (the milestone-3 sweep).
//   DZRP-WP-*, DZRP-ST-*, DZRP-SPR-*  WP-4: watchpoints (both edges of a
//               range, bank, the exact-tuple remove), state tokens validated
//               before any backend call, sprites 16-19.
//
// Every refusal path has a row: before CMD_INIT, malformed, unknown register,
// out-of-range bank/slot, the page bound, and the backend's RZX wall.
//
// PROVENANCE OF THE TEST DESIGN. Several rows restate, for this adapter and
// over the fake, checks from the owner's dezogif_ng DZRP conformance suite —
// `test/dzrp/conformance.py` of the dezogif_ng repository (a sibling checkout
// of this one) @ commit 709ae7d77d444e0e14d88d5c6114b2a7c18e2be6 (GPLv3, as
// jnext). No code is
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
#include "core/rzx.h"
#include "debug/breakpoints.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "version.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "../row_id.h"

using jnext::dbg::Debugger;
using jnext::dbg::PumpBudget;
using jnext::dbg::Result;
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

/// A minimal CMD_INIT payload: version 2.2.0 and an empty name.
static std::string init_payload_fr() { return std::string("\x02\x02\x00\x00", 4); }

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
        // A ZERO-LENGTH command followed at once by the next. The adapter reads
        // only what the current frame still wants, so a header-only frame must
        // end its read at byte 6 — one byte more and the next frame loses its
        // first length byte and the stream desynchronises.
        Rig         rig;
        auto        p = rig.connect();
        std::string rx;
        const auto r = exchange(rig, *p, rx,
                                frame(1, CMD_GET_SUPPORTED_COMMANDS) + frame(2, CMD_LOOPBACK, "z") +
                                    frame(3, CMD_GET_SUPPORTED_COMMANDS) + frame(4, CMD_LOOPBACK),
                                4);
        check("DZRP-FR-21", "zero-length commands pipelined with others lose no byte: four "
                            "frames in one send, four replies, in order and in sync",
              r.size() == 4 && r[0].seq == 1 && r[1].seq == 2 && r[1].payload == "z" &&
                  r[2].seq == 3 && r[2].payload == r[0].payload && r[3].seq == 4 && r[3].len == 1);
    }
    {
        // The parser's own bound, byte for byte: fed two frames in one call it
        // takes exactly the first and leaves the second to its caller.
        FrameParser       fp;
        const std::string two  = frame(7, CMD_LOOPBACK, "abc") + frame(8, CMD_LOOPBACK, "de");
        const std::size_t took = fp.feed(reinterpret_cast<const std::uint8_t*>(two.data()),
                                         two.size(), g_now);
        const bool    ready = fp.ready();
        const Command cmd   = fp.take();
        check("DZRP-FR-22", "FrameParser fed two frames at once takes exactly the first (9 bytes) "
                            "and yields exactly its command",
              took == 9 && ready && cmd.seq == 7 && cmd.id == CMD_LOOPBACK &&
                  cmd.payload == std::vector<std::uint8_t>({'a', 'b', 'c'}));
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
        // ...and while PAUSED the whole chain is drained in ONE pump: the
        // adapter reports each executed command as `Serviced`, which is what
        // lets the backend's SES-03 drain ask for the next (a DeZog step is
        // 4-6 sequential round trips, design §4.2.3).
        Rig         rig;
        auto        p = rig.connect();
        std::string rx;
        p->send(frame(1, CMD_INIT, init_payload_fr()));
        rig.pump();
        p->take();
        std::string five;
        for (int i = 0; i < 5; ++i)
            five += frame(static_cast<std::uint8_t>(10 + i), CMD_LOOPBACK, std::string(1, char('a' + i)));
        p->send(five);
        PumpBudget drain;
        drain.max_wait_ms = 0;
        drain.drain_ms    = 50;
        drain.budget_ms   = 2000;
        rig.dbg->pump(drain);
        rx += p->take();
        const auto r = take_frames(rx);
        check("DZRP-FR-20", "while paused (after CMD_INIT) five pipelined commands are all "
                            "answered in ONE pump with a drain budget",
              rig.dbg->state().paused && r.size() == 5 && r[4].seq == 14 && r[4].payload == "e",
              "answered=" + std::to_string(r.size()));
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

// ── WP-2 helpers ───────────────────────────────────────────────────────────

static std::string bytes(std::initializer_list<int> v) {
    std::string s;
    for (int b : v) s.push_back(static_cast<char>(b & 0xFF));
    return s;
}

static std::string u16s(std::uint16_t v) {
    return bytes({v & 0xFF, v >> 8});
}

static std::string init_payload(int maj, int min, int pat, const std::string& name) {
    return bytes({maj, min, pat}) + name + std::string(1, '\0');
}

static std::string jnext_name() { return std::string("jnext v") + JNEXT_VERSION_STRING; }

/// One DZRP client over the rig: numbered commands, one reply each.
struct Dz {
    Rig&                      rig;
    std::shared_ptr<FakePeer> p;
    std::string               rx;
    std::uint8_t              seq = 0;

    /// Notifications (seq 0) as they came off the wire, oldest first, and
    /// every frame in arrival order (`order`: 'R' reply, 'N' notification).
    std::vector<Resp> ntfs;
    std::string       order;

    explicit Dz(Rig& r) : rig(r), p(r.connect()) {}

    /// Take what the server wrote; notifications go to `ntfs`, replies are
    /// returned.
    std::vector<Resp> collect() {
        rx += p->take();
        std::vector<Resp> replies;
        for (auto& f : take_frames(rx)) {
            order.push_back(f.seq == 0 ? 'N' : 'R');
            (f.seq == 0 ? ntfs : replies).push_back(f);
        }
        return replies;
    }

    /// Send one command and return its reply; `len` 0 if none came. Any
    /// notification that arrives meanwhile is kept in `ntfs`.
    Resp cmd(std::uint8_t id, const std::string& payload = {}) {
        seq = static_cast<std::uint8_t>(seq % 255 + 1);
        p->send(frame(seq, id, payload));
        for (int i = 0; i < 16; ++i) {
            rig.pump();
            for (const Resp& r : collect())
                if (r.seq == seq) return r;
        }
        return Resp{};
    }

    /// One loop-owner tick's pump, collecting what it wrote.
    void tick() {
        rig.pump();
        collect();
    }
    Resp init(int maj = 2, int min = 2, int pat = 0, const std::string& name = "dzrp-test") {
        return cmd(CMD_INIT, init_payload(maj, min, pat, name));
    }
};

static bool regs_equal(const Z80Registers& a, const Z80Registers& b) {
    return a.AF == b.AF && a.BC == b.BC && a.DE == b.DE && a.HL == b.HL && a.AF2 == b.AF2 &&
           a.BC2 == b.BC2 && a.DE2 == b.DE2 && a.HL2 == b.HL2 && a.IX == b.IX && a.IY == b.IY &&
           a.SP == b.SP && a.PC == b.PC && a.I == b.I && a.R == b.R && a.IFF1 == b.IFF1 &&
           a.IFF2 == b.IFF2 && a.IM == b.IM;
}

/// A register file with every field distinct, so a write to the wrong one shows.
static Z80Registers distinct_regs(const Z80Registers& base) {
    Z80Registers r = base;
    r.PC = 0x8000; r.SP = 0xFE12; r.AF = 0xA1F1; r.BC = 0xB2C2; r.DE = 0xD3E3;
    r.HL = 0x4454; r.IX = 0x6575; r.IY = 0x8696; r.AF2 = 0xA7F7; r.BC2 = 0xB8C8;
    r.DE2 = 0xD9E9; r.HL2 = 0x4A5A; r.I = 0x3B; r.R = 0x4C; r.IM = 1; r.IFF1 = 0; r.IFF2 = 0;
    return r;
}

/// The machine's time right now, and whether a frame moves it.
static std::uint64_t cycle(Rig& rig) { return rig.dbg->time().master_cycle; }
static bool frame_advances(Rig& rig) {
    const std::uint64_t t0 = cycle(rig);
    rig.emu.run_frame();
    return cycle(rig) != t0;
}

/// An RZX playback in force, for the refusal rows: the backend refuses every
/// mutation (and `port_in`) while one runs. No frame is run while it is on.
struct RzxOn {
    Emulator& emu;
    explicit RzxOn(Emulator& e) : emu(e) { emu.rzx_player().start(RzxRecording{}); }
    ~RzxOn() { emu.rzx_player().stop(); }
};

/// Marks each of the Next's eight ROM SRAM pages (four 16 KB ROM images) at
/// both ends, so a read that lands on the wrong page or half says which.
static void mark_rom_pages(Rig& rig) {
    for (int p = 0; p < 8; ++p) {
        std::uint8_t* d = rig.emu.ram().page_ptr(static_cast<std::uint16_t>(p));
        for (int i = 0; i < 16; ++i) {
            d[i]          = static_cast<std::uint8_t>(0xC0 + p * 4 + (i & 3));
            d[0x1FF0 + i] = static_cast<std::uint8_t>(0x80 + p * 4 + (i & 3));
        }
    }
}

// ── DZRP-SES — session: CMD_INIT, CMD_CLOSE, attach/detach (WP-2) ──────────

static void session_rows() {
    {
        Rig rig;
        Dz  c(rig);
        const bool attached_before = rig.dbg->attached();
        const bool running_before  = !rig.dbg->state().paused;
        const Resp r = c.init();
        const std::string want = bytes({0, 2, 2, 0, 4}) + jnext_name() + std::string(1, '\0');
        check("DZRP-SES-01", "CMD_INIT answers error 0, DZRP 2.2.0, machine ZXNEXT (4) and "
                             "\"jnext v<version>\\0\", on the command's seq",
              r.len == 1 + want.size() && r.seq == c.seq && r.payload == want, hex(r.payload));
        const auto st = rig.dbg->state();
        check("DZRP-SES-02", "CMD_INIT attaches the client and PAUSES a running machine (design "
                             "F4): it was running and detached, it is now paused by a client, "
                             "and a frame no longer moves it",
              running_before && !attached_before && rig.dbg->attached() && st.paused &&
                  st.pause_reason.kind == jnext::dbg::PauseReason::Kind::User &&
                  st.pause_reason.by != jnext::dbg::CLIENT_NONE && !frame_advances(rig));
    }
    {
        // THE CLIENT'S VERSION IS RECORDED, and does not change the answer: a
        // 2.0.0 client (DeZog 3.7.4) and a 2.2.0 one (3.8) both get 2.2.0,
        // which satisfies both ("major equal, remote minor >= client minor").
        LogTap log;
        Rig    rig_old;
        Dz     old_c(rig_old);
        const Resp a = old_c.init(2, 0, 0, "DeZog v3.7.4");
        Rig    rig_new;
        Dz     new_c(rig_new);
        const Resp b = new_c.init(2, 2, 0, "DeZog v3.8.0");
        check("DZRP-SES-03", "a 2.0.0 and a 2.2.0 client both get the 2.2.0 answer, and the log "
                             "records each client's own version and name",
              a.payload == b.payload && a.payload.substr(0, 5) == bytes({0, 2, 2, 0, 4}) &&
                  log.count("CMD_INIT from \"DeZog v3.7.4\" (DZRP 2.0.0)") == 1 &&
                  log.count("CMD_INIT from \"DeZog v3.8.0\" (DZRP 2.2.0)") == 1);
    }
    {
        // FIVE CMD_INITs (conformance C6 sends five) are ONE attach: after them
        // a single CMD_CLOSE leaves nothing attached and the machine running.
        Rig  rig;
        Dz   c(rig);
        bool all = true;
        for (int i = 0; i < 5; ++i) all = all && c.init().payload.substr(0, 1) == bytes({0});
        const Resp cl = c.cmd(CMD_CLOSE);
        check("DZRP-SES-04", "five CMD_INITs on one session attach ONE client: a single "
                             "CMD_CLOSE then leaves nothing attached and the machine running",
              all && cl.len == 1 && !rig.dbg->attached() && !rig.dbg->state().paused &&
                  frame_advances(rig));
    }
    {
        // CMD_CLOSE: seq-only reply, detach, this client's pause released —
        // and the CONNECTION serves on: a CMD_INIT after it is answered in
        // sync (conformance C15).
        Rig rig;
        Dz  c(rig);
        c.init();
        const bool paused = rig.dbg->state().paused;
        const Resp cl     = c.cmd(CMD_CLOSE);
        const bool after_close = !rig.dbg->attached() && !rig.dbg->state().paused &&
                                 frame_advances(rig);
        const Resp again = c.init();
        check("DZRP-SES-05", "CMD_CLOSE answers with the seq alone, detaches, releases the "
                             "client's own pause, and the same connection then serves a new "
                             "CMD_INIT (conformance C15)",
              paused && cl.len == 1 && cl.seq == c.seq - 1 && after_close &&
                  again.payload.substr(0, 1) == bytes({0}) && rig.dbg->attached() &&
                  !c.p->closed_by_server());
    }
    {
        // SES-01 — A CRASHED DeZog MUST NOT LEAVE THE MACHINE HUNG: the client
        // pauses (CMD_INIT), then its socket goes — a FIN.
        Rig rig;
        Dz  c(rig);
        c.init();
        const bool held = rig.dbg->state().paused && !frame_advances(rig);
        c.p->close();
        rig.pump(3);
        check("DZRP-SES-06", "a client that hangs up while its own pause holds leaves the machine "
                             "RUNNING, and detached (SES-01)",
              held && !rig.dbg->attached() && !rig.dbg->state().paused && frame_advances(rig));
    }
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        c.p->reset();
        rig.pump(3);
        check("DZRP-SES-07", "and so does one whose connection is RESET",
              !rig.dbg->attached() && !rig.dbg->state().paused && frame_advances(rig));
    }
    {
        // A PAUSE THAT IS NOT OURS SURVIVES US. Another client paused the
        // machine; CMD_INIT must not re-attribute that pause to DZRP (the
        // backend's `pause()` would — "last verb wins"), or DZRP's departure
        // would release a pause it never owned.
        Rig  rig;
        auto other = rig.dbg->attach({"other client", jnext::dbg::ClientKind::Test}).value;
        rig.dbg->pause(other);
        Dz c(rig);
        const Resp r = c.init();
        const auto during = rig.dbg->state();
        c.p->close();
        rig.pump(3);
        const auto after = rig.dbg->state();
        check("DZRP-SES-08", "CMD_INIT on a machine another client paused leaves that pause and "
                             "its owner alone, and DZRP's hang-up leaves the machine paused",
              r.payload.substr(0, 1) == bytes({0}) && during.paused &&
                  during.pause_reason.by == other && after.paused &&
                  after.pause_reason.by == other && !frame_advances(rig));
        rig.dbg->detach(other);
    }
    {
        // Destroying the adapter with a live session detaches the client.
        Rig rig;
        Dz  c(rig);
        c.init();
        rig.dzrp.reset();
        check("DZRP-SES-09", "destroying the adapter mid-session detaches its client and releases "
                             "its pause",
              !rig.dbg->attached() && !rig.dbg->state().paused && frame_advances(rig));
    }
    {
        // A malformed CMD_INIT: the reply carries an error field, so the fault
        // goes there — error 1, the rest of the reply intact — and nothing is
        // attached or paused.
        Rig        rig;
        Dz         c(rig);
        const Resp r = c.cmd(CMD_INIT, bytes({2, 2}));
        check("DZRP-SES-10", "a CMD_INIT shorter than its 3-byte version is answered with error 1 "
                             "in the full reply shape, attaching and pausing nothing",
              r.payload == bytes({1, 2, 2, 0, 4}) + jnext_name() + std::string(1, '\0') &&
                  !rig.dbg->attached() && !rig.dbg->state().paused);
    }
    {
        // NO SESSION, NO MACHINE: before CMD_INIT a machine command has no
        // client to be attributed to, so it is refused; the machine-free ones
        // (LOOPBACK, GET_SUPPORTED_COMMANDS, CLOSE) are served.
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        rig.emu.mmu().write(0x9000, 0x11);
        const Resp w  = c.cmd(CMD_WRITE_MEM, bytes({0}) + u16s(0x9000) + bytes({0x77}));
        const Resp g  = c.cmd(CMD_GET_REGISTERS);
        const Resp lb = c.cmd(CMD_LOOPBACK, "pre");
        const Resp sc = c.cmd(CMD_GET_SUPPORTED_COMMANDS);
        const Resp cl = c.cmd(CMD_CLOSE);
        check("DZRP-SES-11", "before CMD_INIT a machine command gets a seq-only reply and a warn "
                             "line, and touches nothing; LOOPBACK, GET_SUPPORTED_COMMANDS and "
                             "CLOSE are answered",
              w.len == 1 && g.len == 1 && rig.emu.mmu().peek(0x9000) == 0x11 &&
                  log.count("CMD_WRITE_MEM before CMD_INIT") == 1 &&
                  log.count("CMD_GET_REGISTERS before CMD_INIT") == 1 && lb.payload == "pre" &&
                  sc.len > 1 && cl.len == 1 && !rig.dbg->attached());
    }
    {
        // The legacy commands tell a 2.2 client, at debug level, that DZRP
        // 2.2.0 removed them; a 2.1 client is not told (it is entitled to them).
        LogTap log;
        const auto level = Log::debugger()->level();
        Log::debugger()->set_level(spdlog::level::debug);
        Rig rig_new;
        Dz  n(rig_new);
        n.init(2, 2, 0);
        n.cmd(CMD_GET_REGISTERS);
        n.cmd(CMD_SET_BORDER, bytes({1}));
        Rig rig_old;
        Dz  o(rig_old);
        o.init(2, 1, 0);
        o.cmd(CMD_SET_BORDER, bytes({1}));
        Log::debugger()->set_level(level);
        check("DZRP-SES-12", "a legacy command from a 2.2 client is logged at debug as removed in "
                             "2.2.0 and still served; from a 2.1 client it is not remarked on",
              log.count("CMD_SET_BORDER was removed in DZRP 2.2.0 — served as legacy to a 2.2.0 "
                        "client") == 1 &&
                  log.count("was removed in DZRP 2.2.0") == 1 &&
                  rig_new.emu.ula().get_border() == 1 && rig_old.emu.ula().get_border() == 1);
    }
    {
        // THE CLIENT'S NAME IS THE CLIENT'S CLAIM: kept to 64 printable bytes
        // before it reaches a log line or the backend's client list (review
        // finding #2). 100 bytes are sent; the 64-byte prefix is what shows,
        // quoted, both in the adapter's line and in the backend's ATTACH line.
        LogTap log;
        Rig    rig;
        Dz     c(rig);
        std::string name;
        for (int i = 0; i < 100; ++i) name.push_back(static_cast<char>('A' + i % 26));
        const Resp r = c.init(2, 2, 0, name);
        const std::string kept = name.substr(0, 64);
        check("DZRP-SES-13", "a 100-byte CMD_INIT program name is kept to its first 64 bytes, in "
                             "the adapter's log line and in the backend's client list",
              r.payload.substr(0, 1) == bytes({0}) &&
                  log.count("CMD_INIT from \"" + kept + "\" (DZRP 2.2.0)") == 1 &&
                  log.count("\"" + kept + "\" kind=") == 1 &&
                  log.count(name.substr(0, 65)) == 0);
    }
}

// ── DZRP-SUP — CMD_GET_SUPPORTED_COMMANDS and the one table (WP-2) ─────────

static void supported_rows() {
    Rig    rig;
    LogTap log;
    Dz     c(rig);
    const Resp r = c.cmd(CMD_GET_SUPPORTED_COMMANDS);
    // The whole of design §2 row 24: the legacy 5 and 12 are served but never
    // advertised, and 13, 14 and 22 are unsupported.
    check("DZRP-SUP-01", "the bitfield names exactly the commands served, little endian, bit "
                         "n = command n: DE 8F BF 1F 80 0F 0C (design §2 row 24: 1-4, 6-11, 15-21, "
                         "23-28, 39-43, 50, 51)",
          r.payload == bytes({0xDE, 0x8F, 0xBF, 0x1F, 0x80, 0x0F, 0x0C}), hex(r.payload));

    // THE ONE TABLE, BOTH WAYS: every id 0..255 sent before CMD_INIT (so no
    // machine is touched) with an empty payload. "Unsupported" must be said for
    // exactly the ids whose bit is clear, except the two legacy ones.
    auto bit = [&](int id) {
        const std::size_t byte = static_cast<std::size_t>(id) / 8;
        return byte < r.payload.size() &&
               ((static_cast<unsigned char>(r.payload[byte]) >> (id % 8)) & 1);
    };
    bool        agree = true;
    std::string why;
    for (int id = 0; id < 256; ++id) {
        const std::string needle = "unsupported DZRP command " + std::to_string(id) + " (";
        const int         before = log.count(needle);
        const Resp        q      = c.cmd(static_cast<std::uint8_t>(id));
        const bool said_unsupported = log.count(needle) == before + 1;
        const bool legacy           = id == CMD_WRITE_BANK || id == CMD_SET_BORDER;
        if (q.seq != c.seq || said_unsupported == (bit(id) || legacy)) {
            agree = false;
            why   = "id " + std::to_string(id);
            break;
        }
    }
    check("DZRP-SUP-02", "the dispatcher and the bitfield are one table: every id 0..255 is "
                         "reported unsupported iff its bit is clear (legacy 5 and 12 excepted)",
          agree && !rig.dbg->attached(), why);
    check("DZRP-SUP-03", "the two legacy commands DZRP 2.2.0 removed (5 WRITE_BANK, 12 "
                         "SET_BORDER) are served but their bits are clear",
          !bit(CMD_WRITE_BANK) && !bit(CMD_SET_BORDER) &&
              log.count("unsupported DZRP command 5 (") == 0 &&
              log.count("unsupported DZRP command 12 (") == 0);
    check("DZRP-SUP-04", "13, 14 and 22 are reported unsupported with the reason: a "
                         "normal-mode remote (use remoteType cspect/dzrp), no scratch context",
          log.count("command 13 (CMD_SET_BREAKPOINTS: jnext is a normal-mode remote") == 1 &&
              log.count("command 14 (CMD_RESTORE_MEM: jnext is a normal-mode remote") == 1 &&
              log.count("command 22 (CMD_EXEC_ASM: jnext has no scratch execution context") == 1);
}

// ── DZRP-REG — registers (WP-2) ────────────────────────────────────────────

static void register_rows() {
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        const Z80Registers set = distinct_regs(rig.emu.cpu().get_registers());
        rig.emu.cpu().set_registers(set);
        const Resp r = c.cmd(CMD_GET_REGISTERS);
        const std::string want = u16s(0x8000) + u16s(0xFE12) + u16s(0xA1F1) + u16s(0xB2C2) +
                                 u16s(0xD3E3) + u16s(0x4454) + u16s(0x6575) + u16s(0x8696) +
                                 u16s(0xA7F7) + u16s(0xB8C8) + u16s(0xD9E9) + u16s(0x4A5A) +
                                 bytes({0x4C, 0x3B, 1, 0, 8}) +
                                 bytes({0xFF, 0xFF, 0x0A, 0x0B, 0x04, 0x05, 0x00, 0x01});
        check("DZRP-REG-01", "CMD_GET_REGISTERS: PC SP AF BC DE HL IX IY AF' BC' DE' HL' (LE), R, "
                             "I, IM, reserved 0, 8 slots — the NR 0x50-0x57 values, FF FF 0A 0B "
                             "04 05 00 01 on a 48K map; 38 bytes counting the seq",
              r.len == 38 && r.payload == want, hex(r.payload));
    }
    {
        // THE REGISTER NUMBERS, ALL OF THEM: spec 0..35 (12 unused) == DeZog's
        // Z80_REG enum. Each write must change exactly the register it names.
        Rig  rig;
        Dz   c(rig);
        c.init();
        struct Case { int num; std::uint16_t value; void (*apply)(Z80Registers&, std::uint16_t); };
        const Case cases[] = {
            {0,  0x1234, [](Z80Registers& r, std::uint16_t v) { r.PC = v; }},
            {1,  0x2345, [](Z80Registers& r, std::uint16_t v) { r.SP = v; }},
            {2,  0x3456, [](Z80Registers& r, std::uint16_t v) { r.AF = v; }},
            {3,  0x4567, [](Z80Registers& r, std::uint16_t v) { r.BC = v; }},
            {4,  0x5678, [](Z80Registers& r, std::uint16_t v) { r.DE = v; }},
            {5,  0x6789, [](Z80Registers& r, std::uint16_t v) { r.HL = v; }},
            {6,  0x789A, [](Z80Registers& r, std::uint16_t v) { r.IX = v; }},
            {7,  0x89AB, [](Z80Registers& r, std::uint16_t v) { r.IY = v; }},
            {8,  0x9ABC, [](Z80Registers& r, std::uint16_t v) { r.AF2 = v; }},
            {9,  0xABCD, [](Z80Registers& r, std::uint16_t v) { r.BC2 = v; }},
            {10, 0xBCDE, [](Z80Registers& r, std::uint16_t v) { r.DE2 = v; }},
            {11, 0xCDEF, [](Z80Registers& r, std::uint16_t v) { r.HL2 = v; }},
            {13, 0x0002, [](Z80Registers& r, std::uint16_t v) { r.IM = static_cast<std::uint8_t>(v); }},
            {14, 0x0011, [](Z80Registers& r, std::uint16_t v) { r.AF = static_cast<std::uint16_t>((r.AF & 0xFF00) | v); }},
            {15, 0x0022, [](Z80Registers& r, std::uint16_t v) { r.AF = static_cast<std::uint16_t>((r.AF & 0x00FF) | (v << 8)); }},
            {16, 0x0033, [](Z80Registers& r, std::uint16_t v) { r.BC = static_cast<std::uint16_t>((r.BC & 0xFF00) | v); }},
            {17, 0x0044, [](Z80Registers& r, std::uint16_t v) { r.BC = static_cast<std::uint16_t>((r.BC & 0x00FF) | (v << 8)); }},
            {18, 0x0055, [](Z80Registers& r, std::uint16_t v) { r.DE = static_cast<std::uint16_t>((r.DE & 0xFF00) | v); }},
            {19, 0x0066, [](Z80Registers& r, std::uint16_t v) { r.DE = static_cast<std::uint16_t>((r.DE & 0x00FF) | (v << 8)); }},
            {20, 0x0077, [](Z80Registers& r, std::uint16_t v) { r.HL = static_cast<std::uint16_t>((r.HL & 0xFF00) | v); }},
            {21, 0x0088, [](Z80Registers& r, std::uint16_t v) { r.HL = static_cast<std::uint16_t>((r.HL & 0x00FF) | (v << 8)); }},
            {22, 0x0099, [](Z80Registers& r, std::uint16_t v) { r.IX = static_cast<std::uint16_t>((r.IX & 0xFF00) | v); }},
            {23, 0x00AA, [](Z80Registers& r, std::uint16_t v) { r.IX = static_cast<std::uint16_t>((r.IX & 0x00FF) | (v << 8)); }},
            {24, 0x00BB, [](Z80Registers& r, std::uint16_t v) { r.IY = static_cast<std::uint16_t>((r.IY & 0xFF00) | v); }},
            {25, 0x00CC, [](Z80Registers& r, std::uint16_t v) { r.IY = static_cast<std::uint16_t>((r.IY & 0x00FF) | (v << 8)); }},
            {26, 0x00DD, [](Z80Registers& r, std::uint16_t v) { r.AF2 = static_cast<std::uint16_t>((r.AF2 & 0xFF00) | v); }},
            {27, 0x00EE, [](Z80Registers& r, std::uint16_t v) { r.AF2 = static_cast<std::uint16_t>((r.AF2 & 0x00FF) | (v << 8)); }},
            {28, 0x0012, [](Z80Registers& r, std::uint16_t v) { r.BC2 = static_cast<std::uint16_t>((r.BC2 & 0xFF00) | v); }},
            {29, 0x0023, [](Z80Registers& r, std::uint16_t v) { r.BC2 = static_cast<std::uint16_t>((r.BC2 & 0x00FF) | (v << 8)); }},
            {30, 0x0034, [](Z80Registers& r, std::uint16_t v) { r.DE2 = static_cast<std::uint16_t>((r.DE2 & 0xFF00) | v); }},
            {31, 0x0045, [](Z80Registers& r, std::uint16_t v) { r.DE2 = static_cast<std::uint16_t>((r.DE2 & 0x00FF) | (v << 8)); }},
            {32, 0x0056, [](Z80Registers& r, std::uint16_t v) { r.HL2 = static_cast<std::uint16_t>((r.HL2 & 0xFF00) | v); }},
            {33, 0x0067, [](Z80Registers& r, std::uint16_t v) { r.HL2 = static_cast<std::uint16_t>((r.HL2 & 0x00FF) | (v << 8)); }},
            {34, 0x0078, [](Z80Registers& r, std::uint16_t v) { r.R = static_cast<std::uint8_t>(v); }},
            {35, 0x0089, [](Z80Registers& r, std::uint16_t v) { r.I = static_cast<std::uint8_t>(v); }},
        };
        bool        all = true;
        std::string why;
        for (const Case& k : cases) {
            const Z80Registers base = distinct_regs(rig.emu.cpu().get_registers());
            rig.emu.cpu().set_registers(base);
            Z80Registers want = base;
            k.apply(want, k.value);
            const Resp r = c.cmd(CMD_SET_REGISTER, bytes({k.num}) + u16s(k.value));
            if (r.len != 1 || !regs_equal(rig.emu.cpu().get_registers(), want)) {
                all = false;
                why = "register number " + std::to_string(k.num);
                break;
            }
        }
        check("DZRP-REG-02", "CMD_SET_REGISTER numbers 0-11 and 13-35 (the spec table, = DeZog's "
                             "Z80_REG) each write exactly the register they name, seq-only reply",
              all, why);
    }
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        const Z80Registers base = distinct_regs(rig.emu.cpu().get_registers());
        rig.emu.cpu().set_registers(base);
        c.cmd(CMD_SET_REGISTER, bytes({15}) + u16s(0xABCD));
        const Z80Registers got = rig.emu.cpu().get_registers();
        check("DZRP-REG-03", "an 8-bit target takes the LOW byte of the value (A <- 0xABCD is "
                             "0xCD), and F beside it is untouched",
              (got.AF >> 8) == 0xCD && (got.AF & 0xFF) == (base.AF & 0xFF));
    }
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        const Z80Registers base = distinct_regs(rig.emu.cpu().get_registers());
        rig.emu.cpu().set_registers(base);
        const Resp a = c.cmd(CMD_SET_REGISTER, bytes({12}) + u16s(0x1234));
        const Resp b = c.cmd(CMD_SET_REGISTER, bytes({36}) + u16s(0x1234));
        const Resp d = c.cmd(CMD_SET_REGISTER, bytes({255}) + u16s(0x1234));
        check("DZRP-REG-04", "register numbers 12 (the spec's unused; DeZog's IR), 36 and 255 "
                             "change nothing: seq-only reply and a warn line each",
              a.len == 1 && b.len == 1 && d.len == 1 &&
                  regs_equal(rig.emu.cpu().get_registers(), base) &&
                  log.count("unknown register number 12") == 1 &&
                  log.count("unknown register number 36") == 1 &&
                  log.count("unknown register number 255") == 1);
    }
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        Z80Registers h = rig.emu.cpu().get_registers();
        h.halted = true;
        rig.emu.cpu().set_registers(h);
        c.cmd(CMD_SET_REGISTER, bytes({0}) + u16s(0x9000));
        const Z80Registers got = rig.emu.cpu().get_registers();
        check("DZRP-REG-05", "setting PC out of a HALT clears `halted` (the backend's INS-01 "
                             "obligation, reached from the wire)",
              got.PC == 0x9000 && !got.halted);
    }
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        const Z80Registers base = distinct_regs(rig.emu.cpu().get_registers());
        rig.emu.cpu().set_registers(base);
        Resp r, i;
        {
            RzxOn rzx(rig.emu);
            r = c.cmd(CMD_SET_REGISTER, bytes({2}) + u16s(0x0000));
            i = c.cmd(CMD_INTERRUPT_ON_OFF, bytes({1}));
        }
        check("DZRP-REG-06", "under an RZX playback CMD_SET_REGISTER and CMD_INTERRUPT_ON_OFF "
                             "change nothing: seq-only replies and a refused_rzx warn line each",
              r.len == 1 && i.len == 1 && regs_equal(rig.emu.cpu().get_registers(), base) &&
                  log.count("CMD_SET_REGISTER 2 refused: refused_rzx") == 1 &&
                  log.count("CMD_INTERRUPT_ON_OFF refused: refused_rzx") == 1);
    }
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        const Resp on = c.cmd(CMD_INTERRUPT_ON_OFF, bytes({1}));
        const Z80Registers a = rig.emu.cpu().get_registers();
        const Resp off = c.cmd(CMD_INTERRUPT_ON_OFF, bytes({0}));
        const Z80Registers b = rig.emu.cpu().get_registers();
        check("DZRP-REG-07", "CMD_INTERRUPT_ON_OFF 1 sets IFF1 and IFF2, 0 clears both; "
                             "seq-only replies",
              on.len == 1 && off.len == 1 && a.IFF1 && a.IFF2 && !b.IFF1 && !b.IFF2);
    }
}

// ── DZRP-MEM — the CPU view (WP-2) ─────────────────────────────────────────

static void memory_rows() {
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        for (int i = 0; i < 64; ++i)
            rig.emu.mmu().write(static_cast<std::uint16_t>(0x9000 + i),
                                static_cast<std::uint8_t>(0x40 + i));
        const Resp r = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0x9000) + u16s(64));
        std::string want;
        for (int i = 0; i < 64; ++i) want.push_back(static_cast<char>(0x40 + i));
        check("DZRP-MEM-01", "CMD_READ_MEM (reserved, addr, size) answers the CPU view's bytes",
              r.len == 65 && r.payload == want, hex(r.payload));

        rig.emu.mmu().write(0xFFFE, 0xA1);
        rig.emu.mmu().write(0xFFFF, 0xA2);
        rig.emu.mmu().write(0x4000, 0xA3);
        const Resp w  = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0xFFFE) + u16s(3));
        check("DZRP-MEM-02", "a read past 0xFFFF wraps to 0x0000 as the CPU's address space does",
              w.payload.size() == 3 && static_cast<unsigned char>(w.payload[0]) == 0xA1 &&
                  static_cast<unsigned char>(w.payload[1]) == 0xA2 &&
                  static_cast<unsigned char>(w.payload[2]) == rig.emu.mmu().peek(0x0000));
        const Resp z = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0x9000) + u16s(0));
        check("DZRP-MEM-03", "a zero-size read is answered with the seq alone",
              z.len == 1 && z.seq == c.seq);
    }
    {
        // SIDE-EFFECT FREE (design §11 WP-2's named row): a +3 READ_MEM over
        // contended RAM leaves `p3_floating_bus_dat_` alone. MEM-05 is the other
        // half — the guest's own read of the same bytes DOES move it — so MEM-04
        // is a gate and not a tautology.
        Rig rig(MachineType::ZX_PLUS3);
        Dz  c(rig);
        c.init();
        for (std::uint16_t a = 0x4000; a < 0x4010; ++a) rig.emu.mmu().write(a, 0xA5);
        rig.emu.mmu().set_p3_floating_bus_dat(0x3C);
        const Resp r = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0x4000) + u16s(16));
        check("DZRP-MEM-04", "a +3 CMD_READ_MEM of contended RAM leaves the floating-bus latch "
                             "unchanged, and reads the right bytes",
              rig.emu.mmu().p3_floating_bus_dat() == 0x3C && r.payload == std::string(16, '\xA5'));
        for (std::uint16_t a = 0x4000; a < 0x4010; ++a) (void)rig.emu.mmu().read(a);
        check("DZRP-MEM-05", "the guest's read of the same bytes DOES move the latch (so MEM-04 "
                             "is a real gate)",
              rig.emu.mmu().p3_floating_bus_dat() == 0xA5);
    }
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        const std::string data = pattern(300, 5);
        const Resp r = c.cmd(CMD_WRITE_MEM, bytes({0}) + u16s(0x9100) + data);
        bool landed = true;
        for (std::size_t i = 0; i < data.size(); ++i)
            landed = landed && rig.emu.mmu().peek(static_cast<std::uint16_t>(0x9100 + i)) ==
                                   static_cast<std::uint8_t>(data[i]);
        check("DZRP-MEM-06", "CMD_WRITE_MEM writes through the CPU view, seq-only reply, and the "
                             "backend logs ONE mutation line for the block, not one per byte",
              r.len == 1 && landed && log.count("MUTATE mem cpu:0x9100 300 bytes") == 1);

        // Straddling the ROM/RAM line of a 48K: 0x3FFE-0x4001.
        const std::uint8_t rom0 = rig.emu.mmu().peek(0x3FFE);
        const std::uint8_t rom1 = rig.emu.mmu().peek(0x3FFF);
        c.cmd(CMD_WRITE_MEM, bytes({0}) + u16s(0x3FFE) + bytes({~rom0 & 0xFF, ~rom1 & 0xFF, 0x5A, 0x6B}));
        check("DZRP-MEM-07", "a write straddling ROM and RAM lands as the CPU's would: the ROM "
                             "bytes are dropped, the RAM bytes land",
              rig.emu.mmu().peek(0x3FFE) == rom0 && rig.emu.mmu().peek(0x3FFF) == rom1 &&
                  rig.emu.mmu().peek(0x4000) == 0x5A && rig.emu.mmu().peek(0x4001) == 0x6B);

        // DZRP has no error field on CMD_WRITE_MEM, so what did not land is
        // said in the log (debug level, design §2 row 9) — counted, not guessed.
        const auto level = Log::debugger()->level();
        Log::debugger()->set_level(spdlog::level::debug);
        c.cmd(CMD_WRITE_MEM, bytes({0}) + u16s(0x3FFE) + bytes({~rom0 & 0xFF, ~rom1 & 0xFF, 0x5B, 0x6C}));
        c.cmd(CMD_WRITE_MEM, bytes({0}) + u16s(0x9300) + bytes({1, 2, 3}));
        c.cmd(CMD_WRITE_MEM, bytes({0}) + u16s(0x3FFF) + bytes({~rom1 & 0xFF, 0x5C, 0x6D}));
        Log::debugger()->set_level(level);
        check("DZRP-MEM-09", "a write whose ROM bytes were dropped says, at debug level, how many "
                             "did not land (2 of 4, the backend's count); a write that landed "
                             "whole says nothing",
              log.count("CMD_WRITE_MEM at 0x3FFE: 2 of 4 bytes did not land") == 1 &&
                  log.count("CMD_WRITE_MEM at 0x3FFF: 1 of 3 bytes did not land") == 1 &&
                  log.count("CMD_WRITE_MEM at 0x9300") == 0);
    }
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        rig.emu.mmu().write(0x9200, 0x12);
        Resp r;
        {
            RzxOn rzx(rig.emu);
            r = c.cmd(CMD_WRITE_MEM, bytes({0}) + u16s(0x9200) + bytes({0x34, 0x56}));
        }
        check("DZRP-MEM-08", "under an RZX playback CMD_WRITE_MEM writes nothing: seq-only reply "
                             "and a refused_rzx warn line",
              r.len == 1 && rig.emu.mmu().peek(0x9200) == 0x12 &&
                  log.count("CMD_WRITE_MEM of 2 bytes at 0x9200 refused: refused_rzx") == 1);
    }
    {
        // GH #281 F1 — a byte an OVERLAY takes has landed: Layer 2 write-over
        // (port 0x123B bit 0, 0x0000-0x3FFF) puts it in the Layer 2 page while
        // reads there still come from ROM. The read-back this adapter used to
        // do counted it as not written; the backend's count does not.
        Rig    rig(MachineType::ZXN_ISSUE2);
        LogTap log;
        Dz     c(rig);
        c.init();
        rig.emu.port().out(0x123B, 0x01);
        const std::uint8_t  rom  = rig.emu.mmu().peek(0x0010);
        const std::uint16_t page = static_cast<std::uint16_t>(rig.dbg->nextreg_peek(0x12) * 2);
        const auto level = Log::debugger()->level();
        Log::debugger()->set_level(spdlog::level::debug);
        const Resp r = c.cmd(CMD_WRITE_MEM, bytes({0}) + u16s(0x0010) + bytes({rom ^ 0xFF}));
        Log::debugger()->set_level(level);
        std::uint8_t l2 = 0;
        rig.dbg->peek(jnext::dbg::MemSpace::page(page), 0x0010, 1, &l2);
        check("DZRP-MEM-10", "CMD_WRITE_MEM under Layer 2 write-over lands the byte in the Layer 2 "
                             "page (the CPU view still reads ROM) and logs nothing: it landed",
              r.len == 1 && l2 == static_cast<std::uint8_t>(rom ^ 0xFF) &&
                  rig.emu.mmu().peek(0x0010) == rom && log.count("CMD_WRITE_MEM at 0x0010") == 0,
              "l2=" + std::to_string(l2));
    }
    {
        // CMD_READ_MEM_BLOCKS (28), DZRP 2.2.0: DeZog 3.8's only memory read.
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        for (int i = 0; i < 4; ++i)
            rig.emu.mmu().write(static_cast<std::uint16_t>(0x9000 + i),
                                static_cast<std::uint8_t>(0x50 + i));
        rig.emu.mmu().write(0xFFFE, 0xB1);
        rig.emu.mmu().write(0xFFFF, 0xB2);
        const std::string blocks = u16s(0x9000) + u16s(4) + u16s(0xFFFE) + u16s(3) +
                                   u16s(0x4000) + u16s(0) + u16s(0x9001) + u16s(2);
        const Resp r = c.cmd(CMD_READ_MEM_BLOCKS, le32(4 + 3 + 0 + 2 + 1) + blocks);
        const std::string want = bytes({0x50, 0x51, 0x52, 0x53, 0xB1, 0xB2,
                                        rig.emu.mmu().peek(0x0000), 0x51, 0x52});
        check("DZRP-MEM-11", "CMD_READ_MEM_BLOCKS answers the blocks back to back, in the order "
                             "asked: a block past 0xFFFF wraps, a zero-size block adds nothing, "
                             "blocks may overlap",
              r.len == 10 && r.payload == want, hex(r.payload));

        const Resp z = c.cmd(CMD_READ_MEM_BLOCKS, le32(1));
        check("DZRP-MEM-12", "a CMD_READ_MEM_BLOCKS with no blocks is answered with the seq alone "
                             "— as a served command, not as an unsupported one",
              z.len == 1 && z.seq == c.seq && log.count("unsupported DZRP command 28") == 0);

        // DeZog's 64 KB read: two 0x8000 blocks (it cannot say 0x10000 in u16).
        const Resp f =
            c.cmd(CMD_READ_MEM_BLOCKS, le32(0x10001) + u16s(0x0000) + u16s(0x8000) +
                                           u16s(0x8000) + u16s(0x8000));
        check("DZRP-MEM-13", "DeZog's whole-64K read (two 0x8000 blocks) is answered with all "
                             "65536 bytes",
              f.payload.size() == 0x10000 &&
                  static_cast<unsigned char>(f.payload[0x9002]) == 0x52 &&
                  static_cast<unsigned char>(f.payload[0xFFFF]) == 0xB2);

        const auto level = Log::debugger()->level();
        Log::debugger()->set_level(spdlog::level::debug);
        const Resp l = c.cmd(CMD_READ_MEM_BLOCKS, le32(99) + u16s(0x9000) + u16s(2));
        Log::debugger()->set_level(level);
        check("DZRP-MEM-14", "resp_length is advisory (spec: the receiver need not use it): a "
                             "wrong one still gets the blocks, and a debug line says so",
              l.payload == bytes({0x50, 0x51}) &&
                  log.count("CMD_READ_MEM_BLOCKS resp_length 99 but the blocks make 3") == 1);

        const Resp m = c.cmd(CMD_READ_MEM_BLOCKS, le32(3) + u16s(0x9000) + bytes({2}));
        check("DZRP-MEM-15", "a block list that is not a multiple of 4 bytes reads nothing: "
                             "seq-only reply and a warn line",
              m.len == 1 && log.count("malformed CMD_READ_MEM_BLOCKS: 3 bytes of blocks") == 1);

        std::string many;
        for (int i = 0; i < 257; ++i) many += u16s(0x0000) + u16s(0xFFFF);
        const Resp big = c.cmd(CMD_READ_MEM_BLOCKS, le32(257u * 0xFFFF + 1) + many);
        check("DZRP-MEM-16", "a request for more than the 16 MiB cap (257 blocks of 0xFFFF) reads "
                             "nothing: seq-only reply and a warn line",
              big.len == 1 &&
                  log.count("CMD_READ_MEM_BLOCKS asks for 16842495 bytes, over the") == 1);

        const Resp s = c.cmd(CMD_READ_MEM_BLOCKS, bytes({0x05, 0x00, 0x00}));
        check("DZRP-MEM-17", "a CMD_READ_MEM_BLOCKS shorter than its 4-byte resp_length reads "
                             "nothing: seq-only reply and the malformed-command warn line",
              s.len == 1 &&
                  log.count("malformed CMD_READ_MEM_BLOCKS: payload is 3 bytes, needs at "
                            "least 4") == 1);

        // The cap at its edge. The response length field counts FROM the seq
        // byte (dzrp_frame.h), so seq + blocks <= MAX_PAYLOAD_BYTES (16 MiB):
        // the largest legal block total is 16 MiB - 1 = 256 x 0xFFFF + 0xFF.
        std::string edge;
        for (int i = 0; i < 256; ++i) edge += u16s(0x0000) + u16s(0xFFFF);
        std::string low(0xFFFF, '\0');
        for (std::size_t i = 0; i < low.size(); ++i)
            low[i] = static_cast<char>(rig.emu.mmu().peek(static_cast<std::uint16_t>(i)));
        std::string want_edge;
        want_edge.reserve(16777215);
        for (int i = 0; i < 256; ++i) want_edge += low;
        want_edge += low.substr(0x9000, 0xFF);
        const Resp at_cap =
            c.cmd(CMD_READ_MEM_BLOCKS, le32(16777216) + edge + u16s(0x9000) + u16s(0x00FF));
        check("DZRP-MEM-18", "a block total of exactly 16 MiB - 1 (256 blocks of 0xFFFF + one of "
                             "0xFF: seq + blocks = the 16 MiB cap) is served in full",
              at_cap.len == 16777216 && at_cap.payload.size() == 16777215 &&
                  at_cap.payload == want_edge,
              "len=" + std::to_string(at_cap.len));

        const Resp over_cap =
            c.cmd(CMD_READ_MEM_BLOCKS, le32(16777217) + edge + u16s(0x9000) + u16s(0x0100));
        check("DZRP-MEM-19", "one byte more (256 blocks of 0xFFFF + one of 0x100: a 16 MiB block "
                             "total) reads nothing: seq-only reply and a warn line",
              over_cap.len == 1 &&
                  log.count("CMD_READ_MEM_BLOCKS asks for 16777216 bytes, over the") == 1);

        // 65538 x 0xFFFF = 4295032830: in 32 bits that wraps to 65534, under
        // the cap. The sum must not wrap.
        std::string wrap;
        for (int i = 0; i < 65538; ++i) wrap += u16s(0x0000) + u16s(0xFFFF);
        const Resp wrapped = c.cmd(CMD_READ_MEM_BLOCKS, le32(65535) + wrap);
        check("DZRP-MEM-20", "a block list whose size sum wraps 32 bits (65538 blocks of 0xFFFF) "
                             "reads nothing: seq-only reply and a warn line with the full sum",
              wrapped.len == 1 &&
                  log.count("CMD_READ_MEM_BLOCKS asks for 4295032830 bytes, over the") == 1,
              "len=" + std::to_string(wrapped.len));
    }
}

// ── DZRP-BANK — banks, DZRP bank N = MMU page N (WP-2) ─────────────────────

static void bank_rows() {
    {
        // The legacy CMD_WRITE_BANK (DeZog 3.7.4's loader), into page 14 — on
        // the Next the bank-7 lower-half BRAM, which the BACKEND routes
        // (REQ-dzrp-5); the adapter only names the page.
        Rig rig(MachineType::ZXN_ISSUE2);
        Dz  c(rig);
        c.init();
        const std::string data = pattern(DZRP_BANK_BYTES, 14);
        const Resp w = c.cmd(CMD_WRITE_BANK, bytes({14}) + data);
        const std::uint8_t* bram = rig.emu.mmu().nr_page_ptr(14);
        const bool in_bram = bram && std::string(reinterpret_cast<const char*>(bram), 16) ==
                                         data.substr(0, 16) &&
                             bram[DZRP_BANK_BYTES - 1] ==
                                 static_cast<std::uint8_t>(data[DZRP_BANK_BYTES - 1]);
        const Resp s  = c.cmd(CMD_SET_SLOT, bytes({6, 14}));
        const Resp rd = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0xC000) + u16s(16));
        check("DZRP-BANK-01", "CMD_WRITE_BANK 14 + 8192 bytes answers error 0 with an empty "
                              "string, lands in page 14 (the bank-7 BRAM), and page 14 mapped "
                              "into slot 6 reads it back through the CPU view",
              w.payload == bytes({0, 0}) && in_bram && s.payload == bytes({0}) &&
                  rd.payload == data.substr(0, 16),
              hex(w.payload));
    }
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        std::uint8_t* page3 = rig.emu.mmu().nr_page_ptr(3);
        page3[0] = 0x3C;
        const std::string full = std::string(DZRP_BANK_BYTES, '\x77');
        const Resp a = c.cmd(CMD_WRITE_BANK, bytes({224}) + full);
        const Resp b = c.cmd(CMD_WRITE_BANK, bytes({0xFE}) + full);
        const Resp d = c.cmd(CMD_WRITE_BANK, bytes({0xFF}) + full);
        const Resp e = c.cmd(CMD_WRITE_BANK, bytes({3}) + std::string(100, '\x77'));
        const Resp f = c.cmd(CMD_WRITE_BANK);
        const Resp g = c.cmd(CMD_WRITE_BANK, bytes({3}) + full + "x");
        const std::string range = bytes({1}) + "bank out of range" + std::string(1, '\0');
        const std::string len   = bytes({1}) + "length must be 8192" + std::string(1, '\0');
        check("DZRP-BANK-02", "CMD_WRITE_BANK refuses banks 224, 0xFE and 0xFF with error 1 "
                              "\"bank out of range\", a short, empty or over-long payload with "
                              "\"length must be 8192\", and writes nothing",
              a.payload == range && b.payload == range && d.payload == range &&
                  e.payload == len && f.payload == len && g.payload == len && page3[0] == 0x3C);
        Resp r;
        {
            RzxOn rzx(rig.emu);
            r = c.cmd(CMD_WRITE_BANK, bytes({3}) + full);
        }
        check("DZRP-BANK-03", "under an RZX playback CMD_WRITE_BANK answers error 1 "
                              "\"refused_rzx\" and writes nothing",
              r.payload == bytes({1}) + "refused_rzx" + std::string(1, '\0') && page3[0] == 0x3C);

        // THE VALID SIDE OF THE LEGACY BOUNDARY (review finding #1): bank 223,
        // the last page there is, is written — the refusals above start at 224.
        const std::string top = pattern(DZRP_BANK_BYTES, 223);
        const Resp        ok  = c.cmd(CMD_WRITE_BANK, bytes({223}) + top);
        const std::uint8_t* p223 = rig.emu.mmu().nr_page_ptr(223);
        check("DZRP-BANK-13", "CMD_WRITE_BANK to bank 223 — the top of the range — answers error 0 "
                              "and lands all 8192 bytes in page 223",
              ok.payload == bytes({0, 0}) && p223 &&
                  std::string(reinterpret_cast<const char*>(p223), DZRP_BANK_BYTES) == top);
    }
    {
        Rig    rig(MachineType::ZXN_ISSUE2);
        LogTap log;
        Dz     c(rig);
        c.init();
        const std::string d = pattern(32, 20);
        const Resp w = c.cmd(CMD_WRITE_BANK_MEM, bytes({20}) + u16s(0x0100) + d);
        const std::uint8_t* p20 = rig.emu.mmu().nr_page_ptr(20);
        const Resp r = c.cmd(CMD_READ_BANK_MEM, bytes({20}) + u16s(0x0100) + u16s(32));
        check("DZRP-BANK-04", "CMD_WRITE_BANK_MEM / CMD_READ_BANK_MEM address a page that is "
                              "mapped nowhere: the bytes land at page 20 + 0x100 and read back, "
                              "with one mutation line",
              w.len == 1 && std::string(reinterpret_cast<const char*>(p20) + 0x100, 32) == d &&
                  r.payload == d && log.count("MUTATE mem page20:0x0100 32 bytes") == 1);

        const Resp edge = c.cmd(CMD_READ_BANK_MEM, bytes({20}) + u16s(0x1FF0) + u16s(0x20));
        const Resp past = c.cmd(CMD_READ_BANK_MEM, bytes({20}) + u16s(0x2000) + u16s(0x10));
        check("DZRP-BANK-05", "a bank read is bounded to its 8 KB page: 16 of 32 bytes at 0x1FF0, "
                              "and none at 0x2000 (empty reply and a warn line)",
              edge.payload == std::string(reinterpret_cast<const char*>(p20) + 0x1FF0, 16) &&
                  past.len == 1 &&
                  log.count("CMD_READ_BANK_MEM bank 20 offset 0x2000 size 16: nothing") == 1);

        std::uint8_t* p21 = rig.emu.mmu().nr_page_ptr(21);
        p21[0] = 0x21;
        const Resp over = c.cmd(CMD_WRITE_BANK_MEM, bytes({20}) + u16s(0x1FF8) + pattern(16, 1));
        check("DZRP-BANK-06", "a bank write past the page writes the part inside it, spills "
                              "nothing into the next page, and says so",
              over.len == 1 &&
                  std::string(reinterpret_cast<const char*>(p20) + 0x1FF8, 8) == pattern(16, 1).substr(0, 8) &&
                  p21[0] == 0x21 &&
                  log.count("CMD_WRITE_BANK_MEM bank 20 offset 0x1FF8: 8 of 16 bytes written") == 1);
    }
    {
        // BANK 0xFF — DeZog 3.8's one 16 KB ROM bank, read through the space the
        // backend names for each slot. A fresh Next has ROM in slots 0 and 1.
        Rig    rig(MachineType::ZXN_ISSUE2);
        LogTap log;
        mark_rom_pages(rig);
        Dz c(rig);
        c.init();
        const Resp lo   = c.cmd(CMD_READ_BANK_MEM, bytes({0xFF}) + u16s(0x0000) + u16s(4));
        const Resp hi   = c.cmd(CMD_READ_BANK_MEM, bytes({0xFF}) + u16s(0x2000) + u16s(4));
        const Resp cpu0 = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0x0000) + u16s(4));
        const Resp cpu1 = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0x2000) + u16s(4));
        const std::uint8_t e0 = rig.emu.mmu().get_effective_page(0);
        const std::uint8_t e1 = rig.emu.mmu().get_effective_page(1);
        const std::string m0 = bytes({0xC0 + e0 * 4, 0xC1 + e0 * 4, 0xC2 + e0 * 4, 0xC3 + e0 * 4});
        const std::string m1 = bytes({0xC0 + e1 * 4, 0xC1 + e1 * 4, 0xC2 + e1 * 4, 0xC3 + e1 * 4});
        check("DZRP-BANK-07", "CMD_READ_BANK_MEM bank 0xFF reads slot 0's ROM half at 0x0000 and "
                              "slot 1's at 0x2000 — the same bytes the CPU sees there, from the "
                              "ROM page each slot serves",
              rig.emu.mmu().is_slot_rom(0) && rig.emu.mmu().is_slot_rom(1) && lo.payload == m0 &&
                  hi.payload == m1 && cpu0.payload == m0 && cpu1.payload == m1,
              hex(lo.payload) + "| " + hex(hi.payload) + "| " + hex(cpu0.payload));
        const Resp span = c.cmd(CMD_READ_BANK_MEM, bytes({0xFF}) + u16s(0x1FFC) + u16s(8));
        const Resp top  = c.cmd(CMD_READ_BANK_MEM, bytes({0xFF}) + u16s(0x3FFC) + u16s(8));
        const Resp past = c.cmd(CMD_READ_BANK_MEM, bytes({0xFF}) + u16s(0x4000) + u16s(8));
        check("DZRP-BANK-08", "a bank-0xFF read runs across the two halves (8 bytes at 0x1FFC), "
                              "stops at the 16 KB end (4 of 8 at 0x3FFC), and serves nothing "
                              "from 0x4000",
              span.payload == bytes({0x80 + e0 * 4, 0x81 + e0 * 4, 0x82 + e0 * 4, 0x83 + e0 * 4}) + m1 &&
                  top.payload == bytes({0x80 + e1 * 4, 0x81 + e1 * 4, 0x82 + e1 * 4, 0x83 + e1 * 4}) &&
                  past.len == 1,
              hex(span.payload) + "| " + hex(top.payload));

        // RAM paged into slot 0: the half at 0x0000 is still served — from the
        // ROM image legacy paging selects (`rom_select()`), not from the RAM
        // the CPU now sees there.
        rig.emu.mmu().nr_page_ptr(5)[0] = 0x55;
        c.cmd(CMD_SET_SLOT, bytes({0, 5}));
        const Resp cpu  = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0x0000) + u16s(1));
        const Resp rom  = c.cmd(CMD_READ_BANK_MEM, bytes({0xFF}) + u16s(0x0000) + u16s(4));
        const Resp kept = c.cmd(CMD_READ_BANK_MEM, bytes({0xFF}) + u16s(0x2000) + u16s(4));
        check("DZRP-BANK-09", "with RAM paged into slot 0 the bank-0xFF half at 0x0000 still reads "
                              "the ROM legacy paging selects (image 0 here), not the RAM the CPU "
                              "sees; slot 1's ROM half is unchanged",
              cpu.payload == bytes({0x55}) && rom.payload == m0 && kept.payload == m1 &&
                  log.count("CMD_READ_BANK_MEM bank 255 offset 0x0000") == 0,
              hex(rom.payload));
    }
    {
        Rig    rig(MachineType::ZXN_ISSUE2);
        LogTap log;
        mark_rom_pages(rig);
        Dz c(rig);
        c.init();
        const Resp fe  = c.cmd(CMD_READ_BANK_MEM, bytes({0xFE}) + u16s(0) + u16s(4));
        const Resp e0  = c.cmd(CMD_READ_BANK_MEM, bytes({224}) + u16s(0) + u16s(4));
        std::string rom_before(reinterpret_cast<const char*>(rig.emu.ram().page_ptr(0)), 16);
        const Resp wff = c.cmd(CMD_WRITE_BANK_MEM, bytes({0xFF}) + u16s(0) + std::string(16, '\x11'));
        const Resp wfe = c.cmd(CMD_WRITE_BANK_MEM, bytes({0xFE}) + u16s(0) + std::string(16, '\x11'));
        const Resp w224 = c.cmd(CMD_WRITE_BANK_MEM, bytes({224}) + u16s(0) + std::string(16, '\x11'));
        std::string rom_after(reinterpret_cast<const char*>(rig.emu.ram().page_ptr(0)), 16);
        // The image is the SELECTED one, not a fixed one: with 0x7FFD bit 4
        // selecting ROM 1 before RAM is paged at 0x0000, bank 0xFF reads ROM 1
        // (SRAM pages 2 and 3). The per-machine selection rule itself is
        // pinned in debugger_backend_test (INS-03-10..17).
        c.cmd(CMD_SET_SLOT, bytes({0, 0xFF}));
        c.cmd(CMD_WRITE_PORT, u16s(0x7FFD) + bytes({0x10}));
        c.cmd(CMD_SET_SLOT, bytes({0, 5}));
        const Resp r1lo = c.cmd(CMD_READ_BANK_MEM, bytes({0xFF}) + u16s(0x0000) + u16s(2));
        const Resp r1hi = c.cmd(CMD_READ_BANK_MEM, bytes({0xFF}) + u16s(0x2000) + u16s(2));
        // RAM in slot 1 as well: its half comes from the same image, at 0x2000.
        c.cmd(CMD_SET_SLOT, bytes({1, 6}));
        const Resp r1hr = c.cmd(CMD_READ_BANK_MEM, bytes({0xFF}) + u16s(0x2000) + u16s(2));
        check("DZRP-BANK-14", "with 0x7FFD selecting ROM 1 and RAM paged into slot 0, bank 0xFF "
                              "reads ROM 1: SRAM page 2 at 0x0000 and page 3 at 0x2000 — and "
                              "still page 3 once RAM is paged into slot 1 too",
              !rig.emu.mmu().is_slot_rom(0) && r1lo.payload == bytes({0xC8, 0xC9}) &&
                  r1hi.payload == bytes({0xCC, 0xCD}) && !rig.emu.mmu().is_slot_rom(1) &&
                  r1hr.payload == bytes({0xCC, 0xCD}),
              hex(r1lo.payload) + "| " + hex(r1hi.payload) + "| " + hex(r1hr.payload));
        check("DZRP-BANK-10", "banks 0xFE and 224 read as empty with a warn line; a bank write to "
                              "0xFF, 0xFE or 224 writes nothing and says why",
              fe.len == 1 && e0.len == 1 && wff.len == 1 && wfe.len == 1 && w224.len == 1 &&
                  rom_before == rom_after &&
                  log.count("CMD_WRITE_BANK_MEM to bank 255 refused: ROM is read-only") == 1 &&
                  log.count("CMD_WRITE_BANK_MEM to bank 254 refused: ROM is read-only") == 1 &&
                  log.count("CMD_WRITE_BANK_MEM to bank 224 refused: no such bank") == 1 &&
                  log.count("CMD_READ_BANK_MEM bank 254 ") == 1 &&
                  log.count("CMD_READ_BANK_MEM bank 224 ") == 1);
        const Resp w223 = c.cmd(CMD_WRITE_BANK_MEM, bytes({223}) + u16s(0x10) + bytes({0xDF, 0xFD}));
        const Resp r223 = c.cmd(CMD_READ_BANK_MEM, bytes({223}) + u16s(0x10) + u16s(2));
        const std::uint8_t* p223 = rig.emu.mmu().nr_page_ptr(223);
        check("DZRP-BANK-12", "bank 223, the last page there is, is served both ways — the "
                              "refusals start at 224",
              w223.len == 1 && r223.payload == bytes({0xDF, 0xFD}) && p223 &&
                  p223[0x10] == 0xDF && p223[0x11] == 0xFD);
        Resp r;
        std::uint8_t* p30 = rig.emu.mmu().nr_page_ptr(30);
        p30[0] = 0x30;
        {
            RzxOn rzx(rig.emu);
            r = c.cmd(CMD_WRITE_BANK_MEM, bytes({30}) + u16s(0) + bytes({0x99}));
        }
        check("DZRP-BANK-11", "under an RZX playback CMD_WRITE_BANK_MEM writes nothing and says "
                              "so",
              r.len == 1 && p30[0] == 0x30 &&
                  log.count("CMD_WRITE_BANK_MEM bank 30 offset 0x0000: 0 of 1 bytes written "
                            "(refused_rzx)") == 1);
    }
}

// ── DZRP-SLOT — CMD_SET_SLOT (WP-2) ────────────────────────────────────────

static void slot_rows() {
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        rig.emu.mmu().nr_page_ptr(14)[0] = 0xE7;
        const Resp s = c.cmd(CMD_SET_SLOT, bytes({6, 14}));
        const Resp g = c.cmd(CMD_GET_REGISTERS);
        const Resp m = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0xC000) + u16s(1));
        check("DZRP-SLOT-01", "CMD_SET_SLOT 6,14 answers error 0; GET_REGISTERS then reports "
                              "slot 6 = 0x0E and the CPU view at 0xC000 is page 14",
              s.payload == bytes({0}) && g.payload.size() == 37 &&
                  static_cast<unsigned char>(g.payload[29 + 6]) == 0x0E &&
                  m.payload == bytes({0xE7}));
    }
    {
        // 0xFF on slot 0 is ROM — what `NEXTREG 0x50,0xFF` does: legacy ROM
        // paging re-engaged, so the CPU view at 0x0000 is ROM again. DeZog sends
        // exactly this for slots 0/1 on every .sna/.z80/.nex load.
        Rig rig(MachineType::ZXN_ISSUE2);
        mark_rom_pages(rig);
        Dz c(rig);
        c.init();
        const Resp rom  = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0x0000) + u16s(4));
        rig.emu.mmu().nr_page_ptr(5)[0] = 0x55;
        const Resp to_ram = c.cmd(CMD_SET_SLOT, bytes({0, 5}));
        const Resp ram    = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0x0000) + u16s(1));
        const Resp back   = c.cmd(CMD_SET_SLOT, bytes({0, 0xFF}));
        const Resp again  = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0x0000) + u16s(4));
        const Resp g      = c.cmd(CMD_GET_REGISTERS);
        check("DZRP-SLOT-02", "CMD_SET_SLOT 0,0xFF after RAM was paged in re-engages the ROM, as "
                              "NEXTREG 0x50,0xFF does: 0x0000 reads the ROM again, slot 0 reports "
                              "0xFF and is read-only",
              to_ram.payload == bytes({0}) && ram.payload == bytes({0x55}) &&
                  back.payload == bytes({0}) && again.payload == rom.payload &&
                  rig.emu.mmu().is_slot_rom(0) && g.payload.size() == 37 &&
                  static_cast<unsigned char>(g.payload[29]) == 0xFF,
              hex(rom.payload) + "| " + hex(again.payload));
        c.cmd(CMD_SET_SLOT, bytes({0, 5}));
        const Resp fe  = c.cmd(CMD_SET_SLOT, bytes({0, 0xFE}));
        const Resp fer = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0x0000) + u16s(4));
        const Resp feg = c.cmd(CMD_GET_REGISTERS);
        check("DZRP-SLOT-03", "0xFE on slot 0 (DeZog up to 2.7) is taken as 0xFF: the ROM again, "
                              "and slot 0 reports 0xFF, not 0xFE",
              fe.payload == bytes({0}) && fer.payload == rom.payload &&
                  rig.emu.mmu().is_slot_rom(0) && feg.payload.size() == 37 &&
                  static_cast<unsigned char>(feg.payload[29]) == 0xFF);
    }
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        const Resp before = c.cmd(CMD_GET_REGISTERS);
        const Resp s8   = c.cmd(CMD_SET_SLOT, bytes({8, 3}));
        const Resp be0  = c.cmd(CMD_SET_SLOT, bytes({4, 0xE0}));
        const Resp bfd  = c.cmd(CMD_SET_SLOT, bytes({4, 0xFD}));
        const Resp shrt = c.cmd(CMD_SET_SLOT, bytes({4}));
        Resp rzx_r;
        {
            RzxOn rzx(rig.emu);
            rzx_r = c.cmd(CMD_SET_SLOT, bytes({4, 3}));
        }
        const Resp after = c.cmd(CMD_GET_REGISTERS);
        check("DZRP-SLOT-04", "CMD_SET_SLOT answers error 1 — and maps nothing — for slot 8, "
                              "banks 0xE0 and 0xFD, a 1-byte payload, and under an RZX playback, "
                              "each with a warn line saying which",
              s8.payload == bytes({1}) && be0.payload == bytes({1}) && bfd.payload == bytes({1}) &&
                  shrt.payload == bytes({1}) && rzx_r.payload == bytes({1}) &&
                  before.payload == after.payload &&
                  log.count("CMD_SET_SLOT 8 to bank 3 refused: no such slot") == 1 &&
                  log.count("CMD_SET_SLOT 4 to bank 224 refused: no such bank") == 1 &&
                  log.count("CMD_SET_SLOT 4 to bank 253 refused: no such bank") == 1 &&
                  log.count("malformed CMD_SET_SLOT: payload is 1 bytes, needs 2") == 1 &&
                  log.count("CMD_SET_SLOT 4 to bank 3 refused: refused_rzx") == 1);
    }
}

// ── DZRP-NR, DZRP-PORT, DZRP-BRD — NextREGs, ports, border (WP-2) ──────────

static void nextreg_port_rows() {
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        rig.emu.nextreg().write(0x14, 0x5A);
        const Resp r = c.cmd(CMD_GET_TBBLUE_REG, bytes({0x14}));
        check("DZRP-NR-01", "CMD_GET_TBBLUE_REG answers the register's value",
              r.len == 2 && r.payload == bytes({0x5A}));
    }
    {
        // The read goes through `nextreg_peek`, which leaves the 0x243B
        // selection alone: a guest `IN (0x253B)` after it still reads the
        // register it had selected.
        Rig rig;
        Dz  c(rig);
        c.init();
        rig.emu.nextreg().write(0x14, 0x5A);
        rig.emu.nextreg().write(0x4A, 0x21);
        c.cmd(CMD_WRITE_PORT, u16s(0x243B) + bytes({0x14}));
        const Resp g = c.cmd(CMD_GET_TBBLUE_REG, bytes({0x4A}));
        const Resp v = c.cmd(CMD_READ_PORT, u16s(0x253B));
        check("DZRP-NR-02", "CMD_GET_TBBLUE_REG leaves the NextREG selection alone: port 0x253B "
                              "still reads the register 0x243B selected before it",
              g.payload == bytes({0x21}) && v.payload == bytes({0x5A}) &&
                  rig.dbg->nextreg_selected() == 0x14);
    }
    {
        // CMD_SET_NEXTREGS (27), DZRP 2.2.0: the pairs in order, so a repeated
        // register keeps its last value; each through the register's own write
        // handler (NR 0x56 maps page 14 into slot 6).
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        const Resp r = c.cmd(CMD_SET_NEXTREGS,
                             bytes({0x14, 0x11, 0x4A, 0x22, 0x14, 0x33, 0x56, 14}));
        check("DZRP-NR-03", "CMD_SET_NEXTREGS writes the pairs in order (NR 0x14 written twice "
                            "keeps 0x33), seq-only reply",
              r.len == 1 && rig.dbg->nextreg_peek(0x14) == 0x33 &&
                  rig.dbg->nextreg_peek(0x4A) == 0x22);
        check("DZRP-NR-04", "and through the register's write handler: NR 0x56 = 14 maps page 14 "
                            "into slot 6",
              rig.dbg->mmu_slots()[6].nr_page == 14);

        const Resp e = c.cmd(CMD_SET_NEXTREGS);
        check("DZRP-NR-05", "an empty CMD_SET_NEXTREGS writes nothing and is answered with the "
                            "seq alone, without a warning — as a served command, not as an "
                            "unsupported one",
              e.len == 1 && log.count("CMD_SET_NEXTREGS") == 0 &&
                  log.count("unsupported DZRP command 27") == 0);

        const Resp o = c.cmd(CMD_SET_NEXTREGS, bytes({0x14, 0x44, 0x4A}));
        check("DZRP-NR-06", "an odd length writes NOTHING (not even the first whole pair): "
                            "seq-only reply and a warn line",
              o.len == 1 && rig.dbg->nextreg_peek(0x14) == 0x33 &&
                  log.count("malformed CMD_SET_NEXTREGS: payload is 3 bytes") == 1);
    }
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        rig.emu.nextreg().write(0x14, 0x5A);
        Resp r;
        {
            RzxOn rzx(rig.emu);
            r = c.cmd(CMD_SET_NEXTREGS, bytes({0x14, 0x11, 0x4A, 0x22}));
        }
        check("DZRP-NR-07", "under an RZX playback CMD_SET_NEXTREGS writes nothing and says so "
                            "ONCE, with how far it got",
              r.len == 1 && rig.dbg->nextreg_peek(0x14) == 0x5A &&
                  log.count("CMD_SET_NEXTREGS NR 0x14 refused: refused_rzx — 0 of 2 pairs "
                            "written") == 1 &&
                  log.count("CMD_SET_NEXTREGS NR 0x4A") == 0);
    }
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        const Resp w = c.cmd(CMD_WRITE_PORT, u16s(0x00FE) + bytes({0x15}));
        check("DZRP-PORT-01", "CMD_WRITE_PORT is a real OUT: 0xFE with 0x15 sets border 5; "
                              "seq-only reply",
              w.len == 1 && rig.emu.ula().get_border() == 5);
        check("DZRP-PORT-05", "and the backend's mutation line for it spells the value in HEX, "
                              "as its 0x says (it printed 0x15 as \"0x21\")",
              log.count("MUTATE port out 0x00FE = 0x15 by") == 1 &&
                  log.count("MUTATE port out 0x00FE = 0x21") == 0);
        rig.emu.nextreg().write(0x14, 0x6B);
        c.cmd(CMD_WRITE_PORT, u16s(0x243B) + bytes({0x14}));
        const Resp r = c.cmd(CMD_READ_PORT, u16s(0x253B));
        check("DZRP-PORT-02", "CMD_READ_PORT is a real IN: 0x243B selects NR 0x14, 0x253B reads "
                              "its value back",
              r.len == 2 && r.payload == bytes({0x6B}));
    }
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        rig.emu.ula().set_border(2);
        Resp in, out, brd;
        {
            RzxOn rzx(rig.emu);
            in  = c.cmd(CMD_READ_PORT, u16s(0x00FE));
            out = c.cmd(CMD_WRITE_PORT, u16s(0x00FE) + bytes({0x06}));
            brd = c.cmd(CMD_SET_BORDER, bytes({0x04}));
        }
        check("DZRP-PORT-03", "under an RZX playback CMD_READ_PORT answers with the seq alone — "
                              "never a made-up value — and says why",
              in.len == 1 && log.count("CMD_READ_PORT 0x00FE refused: refused_rzx") == 1);
        check("DZRP-PORT-04", "and CMD_WRITE_PORT changes nothing (the border stays 2) and says "
                              "why",
              out.len == 1 && log.count("CMD_WRITE_PORT 0x00FE refused: refused_rzx") == 1);
        check("DZRP-BRD-02", "and the legacy CMD_SET_BORDER is refused the same way — the border "
                             "stays 2 (the backend's set_border now honours the RZX wall of "
                             "§4.2a like every other mutation)",
              brd.len == 1 && rig.emu.ula().get_border() == 2 &&
                  log.count("CMD_SET_BORDER refused: refused_rzx") == 1);
    }
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        const Resp b = c.cmd(CMD_SET_BORDER, bytes({0xFB}));
        check("DZRP-BRD-01", "the legacy CMD_SET_BORDER takes bits 2:0 (0xFB is 3), seq-only reply",
              b.len == 1 && rig.emu.ula().get_border() == 3);
    }
}

// ── DZRP-MAL — every fixed-length command, one byte short (WP-2) ───────────

static void malformed_rows() {
    Rig    rig;
    LogTap log;
    Dz     c(rig);
    c.init();
    rig.emu.ula().set_border(1);
    rig.emu.mmu().write(0x9000, 0x42);
    const Z80Registers before = rig.emu.cpu().get_registers();
    struct Short { std::uint8_t id; const char* name; int min; };
    const Short cases[] = {
        {CMD_CONTINUE, "CMD_CONTINUE", 5},             {CMD_ADD_BREAKPOINT, "CMD_ADD_BREAKPOINT", 3},
        {CMD_REMOVE_BREAKPOINT, "CMD_REMOVE_BREAKPOINT", 2},
        {CMD_REMOVE_WATCHPOINT, "CMD_REMOVE_WATCHPOINT", 6},
        {CMD_ENABLE_BREAK_ON_INTERRUPT, "CMD_ENABLE_BREAK_ON_INTERRUPT", 1},
        {CMD_GET_SPRITES_PALETTE, "CMD_GET_SPRITES_PALETTE", 1},
        {CMD_GET_SPRITES, "CMD_GET_SPRITES", 2},       {CMD_GET_SPRITE_PATTERNS, "CMD_GET_SPRITE_PATTERNS", 2},
        {CMD_SET_REGISTER, "CMD_SET_REGISTER", 3},     {CMD_READ_MEM, "CMD_READ_MEM", 5},
        {CMD_WRITE_MEM, "CMD_WRITE_MEM", 3},           {CMD_GET_TBBLUE_REG, "CMD_GET_TBBLUE_REG", 1},
        {CMD_SET_BORDER, "CMD_SET_BORDER", 1},         {CMD_READ_PORT, "CMD_READ_PORT", 2},
        {CMD_WRITE_PORT, "CMD_WRITE_PORT", 3},         {CMD_INTERRUPT_ON_OFF, "CMD_INTERRUPT_ON_OFF", 1},
        {CMD_READ_BANK_MEM, "CMD_READ_BANK_MEM", 5},   {CMD_WRITE_BANK_MEM, "CMD_WRITE_BANK_MEM", 3},
    };
    bool        all = true;
    std::string why;
    for (const Short& k : cases) {
        // One byte short, with values that would DO something if acted on.
        const std::string p = bytes({0x01, 0x00, 0x90, 0x07, 0x00}).substr(0, static_cast<std::size_t>(k.min - 1));
        const Resp r = c.cmd(k.id, p);
        const std::string needle = std::string("malformed ") + k.name + ": payload is " +
                                   std::to_string(k.min - 1) + " bytes, needs at least " +
                                   std::to_string(k.min);
        if (r.len != 1 || log.count(needle) != 1) {
            all = false;
            why = k.name;
            break;
        }
    }
    check("DZRP-MAL-01", "each fixed-length command one byte short gets a seq-only reply and a "
                         "\"malformed\" warn line naming it, and changes nothing",
          all && regs_equal(rig.emu.cpu().get_registers(), before) &&
              rig.emu.ula().get_border() == 1 && rig.emu.mmu().peek(0x9000) == 0x42 &&
              rig.dbg->state().paused && rig.dbg->subscriptions(true).empty(),
          why);
}

// ── WP-3 / WP-4 helpers ────────────────────────────────────────────────────

/// One `NTF_PAUSE` as it came off the wire.
struct Ntf {
    bool          ok     = false;  // a well-formed NTF_PAUSE
    std::uint32_t len    = 0;
    std::uint8_t  reason = 0;
    std::uint16_t addr   = 0;
    std::uint8_t  bank   = 0;
    std::string   text;
};

static Ntf parse_ntf(const Resp& r) {
    Ntf n;
    const std::string& p = r.payload;
    if (r.seq != 0 || p.size() < 6 || static_cast<unsigned char>(p[0]) != NTF_PAUSE ||
        p.back() != '\0')
        return n;
    n.ok     = true;
    n.len    = r.len;
    n.reason = static_cast<std::uint8_t>(p[1]);
    n.addr   = static_cast<std::uint16_t>(static_cast<unsigned char>(p[2]) |
                                        (static_cast<unsigned char>(p[3]) << 8));
    n.bank   = static_cast<std::uint8_t>(p[4]);
    n.text   = p.substr(5, p.size() - 6);
    return n;
}

static std::string ntf_str(const Ntf& n) {
    char b[80];
    std::snprintf(b, sizeof(b), "ok=%d reason=%u addr=%04X bank=%02X text=\"", n.ok,
                  n.reason, n.addr, n.bank);
    return b + n.text + "\"";
}

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

static std::string cont(int bp1en = 0, std::uint16_t bp1 = 0, int bp2en = 0,
                        std::uint16_t bp2 = 0, int alt = 0) {
    // DeZog's 11-byte form (buf:484-490): bp1en, bp1, bp2en, bp2, alt, 4 unused.
    return bytes({bp1en}) + u16s(bp1) + bytes({bp2en}) + u16s(bp2) + bytes({alt, 0, 0, 0, 0});
}

static std::string add_bp(std::uint16_t addr, int bank1, const std::string& cond = {}) {
    return u16s(addr) + bytes({bank1}) + cond + std::string(1, '\0');
}

static std::uint16_t bp_id(const Resp& r) {
    return r.payload.size() == 2
               ? static_cast<std::uint16_t>(static_cast<unsigned char>(r.payload[0]) |
                                            (static_cast<unsigned char>(r.payload[1]) << 8))
               : 0xFFFF;
}

/// The loop at 0x8000: INC A; NOP; NOP; JR 0x8000.
static void load_loop(Rig& rig) {
    load_prog(rig, 0x8000, {0x3C, 0x00, 0x00, 0x18, 0xFB});
    set_pc(rig, 0x8000);
}

static std::uint8_t reg_a(Rig& rig) {
    return static_cast<std::uint8_t>(rig.emu.cpu().get_registers().AF >> 8);
}

// ── DZRP-BRK / CONT / NTF / PAUSE / TEMP / BOI — WP-3 ──────────────────────

static void breakpoint_rows() {
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        load_loop(rig);
        const Resp a  = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0x8002, 0, "A == 3"));
        const auto subs = rig.dbg->subscriptions(false);
        check("DZRP-BRK-01", "CMD_ADD_BREAKPOINT answers id 1 (LE u16) and installs ONE "
                             "unconditional Execute[0x8002] stop owned by the client, listed to "
                             "every client — the condition string is DeZog's, never the remote's",
              a.payload == bytes({1, 0}) && subs.size() == 1 &&
                  subs[0].kind == jnext::dbg::EventKind::Execute && subs[0].filter.lo == 0x8002 &&
                  subs[0].filter.hi == 0x8002 && subs[0].filter.page == jnext::dbg::PAGE_ANY &&
                  !subs[0].has_condition && !subs[0].transient &&
                  subs[0].action == jnext::dbg::Action::Stop &&
                  subs[0].owner != jnext::dbg::CLIENT_NONE,
              hex(a.payload));

        // CONTINUE: the reply is on the wire BEFORE anything runs.
        const std::uint64_t t0 = cycle(rig);
        const Resp          r  = c.cmd(CMD_CONTINUE, cont());
        const bool          no_run_yet = cycle(rig) == t0 && !rig.dbg->state().paused;
        check("DZRP-CONT-01", "CMD_CONTINUE (DeZog's 11 bytes) is answered with the seq alone "
                              "before the machine has run a cycle, and leaves it running",
              r.len == 1 && r.seq == c.seq && no_run_yet);

        // THE POST-FRAME FLUSH: the stop happens inside the loop owner's
        // frames; the NTF is on the wire after ONE pump, not before it.
        const bool   stopped = run_until_paused(rig);
        const std::size_t before = c.p->pending();
        c.tick();
        const bool   one_tick = c.ntfs.size() == 1;
        const Ntf    n        = one_tick ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-NTF-01", "a breakpoint stop in the frames is notified by the very next "
                             "pump — nothing on the wire before it, the NTF after exactly one",
              stopped && before == 0 && one_tick);
        check("DZRP-NTF-02", "NTF_PAUSE for it: seq 0, id 1, reason 2 (breakpoint), address "
                             "0x8002, bank byte 5 (page 4 at slot 4, +1), an empty string — "
                             "length 7 counting the seq",
              n.ok && n.len == 7 && n.reason == BREAK_BP && n.addr == 0x8002 && n.bank == 5 &&
                  n.text.empty() && rig.emu.cpu().get_registers().PC == 0x8002,
              ntf_str(n));

        // GH #221 STEP-OFF through `run()`: a CONTINUE from the breakpoint does
        // not re-hit it at once — one loop iteration (A + 1) runs first.
        const std::uint8_t a0 = reg_a(rig);
        c.ntfs.clear();
        c.cmd(CMD_CONTINUE, cont());
        run_until_paused(rig);
        c.tick();
        const Ntf again = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-CONT-02", "CMD_CONTINUE from the breakpoint it stopped on steps off it "
                              "(GH #221, through run()): one iteration runs (A+1) and the next "
                              "pass stops there again, reason 2",
              again.ok && again.reason == BREAK_BP && again.addr == 0x8002 &&
                  reg_a(rig) == static_cast<std::uint8_t>(a0 + 1),
              ntf_str(again) + " A=" + std::to_string(reg_a(rig)));

        // Removal: the breakpoint is gone from the backend and does not fire.
        const Resp rm = c.cmd(CMD_REMOVE_BREAKPOINT, u16s(1));
        c.ntfs.clear();
        c.cmd(CMD_CONTINUE, cont());
        const bool still_running = !run_until_paused(rig, 3);
        const Resp pz = c.cmd(CMD_PAUSE);
        const Ntf  pn = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-BRK-02", "CMD_REMOVE_BREAKPOINT answers the seq alone and the breakpoint "
                             "is gone: the machine runs three frames past 0x8002 until a "
                             "CMD_PAUSE stops it (reason 1)",
              rm.len == 1 && rig.dbg->subscriptions(true).empty() && still_running &&
                  pz.len == 1 && pn.ok && pn.reason == BREAK_MANUAL);
    }
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        const Resp a = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0x8000, 0));
        c.cmd(CMD_REMOVE_BREAKPOINT, u16s(bp_id(a)));
        const Resp b    = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0x8000, 0));
        const Resp c3   = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0x8000, 0));
        const Resp none = c.cmd(CMD_REMOVE_BREAKPOINT, u16s(99));
        const Resp twice = c.cmd(CMD_REMOVE_BREAKPOINT, u16s(1));
        check("DZRP-BRK-03", "ids are never reused within a session (1, then 2 and 3 after 1 was "
                             "removed); two breakpoints at one address are two ids and two "
                             "subscriptions; removing an id never issued, or one already "
                             "removed, is a seq-only reply and a warn line",
              bp_id(a) == 1 && bp_id(b) == 2 && bp_id(c3) == 3 &&
                  rig.dbg->subscriptions(false).size() == 2 && none.len == 1 && twice.len == 1 &&
                  log.count("CMD_REMOVE_BREAKPOINT: no breakpoint with id 99") == 1 &&
                  log.count("CMD_REMOVE_BREAKPOINT: no breakpoint with id 1") == 1);
        c.cmd(CMD_REMOVE_BREAKPOINT, u16s(2));
        load_loop(rig);
        c.cmd(CMD_CONTINUE, cont());
        run_until_paused(rig, 3);
        c.tick();
        const Ntf n = c.ntfs.empty() ? Ntf{} : parse_ntf(c.ntfs.back());
        check("DZRP-BRK-04", "removing one of two breakpoints at the same address leaves the "
                             "other firing",
              n.ok && n.reason == BREAK_BP && n.addr == 0x8000 &&
                  rig.dbg->subscriptions(false).size() == 1);
    }
    {
        // F7 — A LONG BREAKPOINT: bank+1 = 15 is page 14's; it fires only with
        // page 14 at the PC's slot, and the NTF reports bank byte 15.
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        rig.emu.mmu().nr_page_ptr(14)[0] = 0x18;  // JR $ at the top of page 14
        rig.emu.mmu().nr_page_ptr(14)[1] = 0xFE;
        rig.emu.mmu().nr_page_ptr(16)[0] = 0x18;  // and of page 16
        rig.emu.mmu().nr_page_ptr(16)[1] = 0xFE;
        c.cmd(CMD_SET_SLOT, bytes({6, 14}));
        c.cmd(CMD_SET_REGISTER, bytes({0}) + u16s(0xC000));
        const Resp a = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0xC000, 15));
        c.cmd(CMD_CONTINUE, cont());
        run_until_paused(rig, 3);
        c.tick();
        const Ntf hit = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-BRK-05", "a breakpoint with bank+1 = 15 is the backend's page qualifier 14, "
                             "and its stop reports bank byte 15 (F7: DeZog keys breakpoints by "
                             "long address)",
              bp_id(a) == 1 && rig.dbg->subscriptions(false).size() == 1 &&
                  rig.dbg->subscriptions(false)[0].filter.page == 14 && hit.ok &&
                  hit.reason == BREAK_BP && hit.addr == 0xC000 && hit.bank == 15,
              ntf_str(hit));
        c.ntfs.clear();
        c.cmd(CMD_SET_SLOT, bytes({6, 16}));
        c.cmd(CMD_CONTINUE, cont());
        const bool ran = !run_until_paused(rig, 3);
        c.cmd(CMD_PAUSE);
        const Ntf other = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-BRK-06", "with page 16 at slot 6 the same address does not stop it: three "
                             "frames run, and the CMD_PAUSE that ends them reports bank byte 17",
              ran && other.ok && other.reason == BREAK_MANUAL && other.addr == 0xC000 &&
                  other.bank == 17,
              ntf_str(other));
        const Resp bad = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0xC000, 225));
        const Resp rom = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0x0038, 0xFF));
        const auto subs = rig.dbg->subscriptions(false);
        const bool rom_any = std::any_of(subs.begin(), subs.end(), [](const auto& s) {
            return s.filter.lo == 0x0038 && s.filter.page == jnext::dbg::PAGE_ANY;
        });
        check("DZRP-BRK-07", "bank+1 = 225 (bank 224, which does not exist) is refused with id 0 "
                             "and a warn line; bank+1 = 0xFF (3.7.4's slot-0 ROM, 0xFE) is armed "
                             "in every bank, since the backend cannot name ROM by DZRP bank",
              bp_id(bad) == 0 && log.count("CMD_ADD_BREAKPOINT at 0xC000 refused: no bank 224") == 1 &&
                  bp_id(rom) == 2 && rom_any);
    }
    {
        // ROM BANK BYTES (§5.3): slot 0's ROM reports 0xFF (DeZog 3.7.4's
        // 0xFE + 1, which it can match), slot 1's reports 0 (0xFF + 1 does not
        // fit the byte).
        Rig rig;
        Dz  c(rig);
        c.init();
        load_prog(rig, 0x8000, {0x00, 0xC3, 0x10, 0x00});  // NOP; JP 0x0010
        set_pc(rig, 0x8000);
        c.cmd(CMD_CONTINUE, cont(1, 0x0010, 1, 0x2010));
        run_until_paused(rig, 2);
        c.tick();
        const Ntf s0 = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        c.ntfs.clear();
        set_pc(rig, 0x8000);
        load_prog(rig, 0x8002, {0x10, 0x20});  // JP 0x2010 now
        c.cmd(CMD_CONTINUE, cont(1, 0x2010));
        run_until_paused(rig, 2);
        c.tick();
        const Ntf s1 = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-NTF-03", "a stop in slot 0's ROM reports bank byte 0xFF, one in slot 1's "
                             "ROM reports 0",
              s0.ok && s0.addr == 0x0010 && s0.bank == 0xFF && s1.ok && s1.addr == 0x2010 &&
                  s1.bank == 0x00,
              ntf_str(s0) + " | " + ntf_str(s1));
    }
}

static void temp_rows() {
    {
        // F8 — TEMP BEATS USER: a user breakpoint and a CONTINUE temporary at
        // one address stop as reason 0, or DeZog would evaluate the user
        // breakpoint's condition, find it false and run the step away.
        Rig rig;
        Dz  c(rig);
        c.init();
        load_loop(rig);
        c.cmd(CMD_ADD_BREAKPOINT, add_bp(0x8002, 0));
        c.cmd(CMD_CONTINUE, cont(1, 0x8002));
        run_until_paused(rig, 2);
        c.tick();
        const Ntf n = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-TEMP-01", "a stop that is both a CONTINUE temporary and a user breakpoint "
                              "is reported reason 0, not 2 (F8)",
              n.ok && n.reason == BREAK_NONE && n.addr == 0x8002, ntf_str(n));
        const auto all = rig.dbg->subscriptions(true);
        check("DZRP-TEMP-02", "and the temporary is gone after the stop, the user breakpoint "
                              "stays",
              all.size() == 1 && !all[0].transient);
    }
    {
        // Both temporary slots are honoured, and the temporaries are HIDDEN
        // from every user list while armed (REQ-dzrp-3).
        Rig rig;
        Dz  c(rig);
        c.init();
        load_loop(rig);
        c.cmd(CMD_CONTINUE, cont(1, 0x9000, 1, 0x8003));
        const auto shown = rig.dbg->subscriptions(false);
        const auto all   = rig.dbg->subscriptions(true);
        const bool hidden = shown.empty() && all.size() == 2 && all[0].transient &&
                            all[1].transient && all[0].owner != jnext::dbg::CLIENT_NONE;
        run_until_paused(rig, 2);
        c.tick();
        const Ntf n = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-TEMP-03", "CMD_CONTINUE's two temporaries are transient subscriptions hidden "
                              "from the user list (listed only with include_transient)",
              hidden);
        check("DZRP-TEMP-04", "the SECOND temporary slot stops it too: reason 0 at 0x8003, and "
                              "both temporaries are gone afterwards",
              n.ok && n.reason == BREAK_NONE && n.addr == 0x8003 &&
                  rig.dbg->subscriptions(true).empty(),
              ntf_str(n));
    }
    {
        // A stop that is NOT a temporary (a CMD_PAUSE) removes them too:
        // "removed automatically after the command is finished" (spec).
        Rig rig;
        Dz  c(rig);
        c.init();
        load_loop(rig);
        c.cmd(CMD_CONTINUE, cont(1, 0x9000));
        rig.emu.run_frame();
        c.cmd(CMD_PAUSE);
        const Ntf n = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-TEMP-05", "a CONTINUE ended by CMD_PAUSE instead of its temporary leaves no "
                              "temporary behind",
              n.ok && n.reason == BREAK_MANUAL && rig.dbg->subscriptions(true).empty());
    }
    {
        // The 5-byte CSpect form, and a non-zero alternate command (spec: "only
        // 0 is implemented") — logged and treated as 0 (F5).
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        load_loop(rig);
        c.cmd(CMD_CONTINUE, bytes({1}) + u16s(0x8001) + bytes({0}) + u16s(0));
        run_until_paused(rig, 2);
        c.tick();
        const Ntf five = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        c.ntfs.clear();
        c.cmd(CMD_CONTINUE, cont(1, 0x8003, 0, 0, 2));
        run_until_paused(rig, 2);
        c.tick();
        const Ntf alt = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-CONT-03", "the CSpect reference's 5-byte CMD_CONTINUE is served (stop at "
                              "0x8001), and a non-zero alternate command is logged and run as 0 "
                              "(stop at 0x8003)",
              five.ok && five.addr == 0x8001 && alt.ok && alt.addr == 0x8003 &&
                  log.count("alternate command 2 is not implemented") == 1);
    }
    {
        // A temporary whose ENABLE byte is 0 is not a temporary, whatever
        // address follows it.
        Rig rig;
        Dz  c(rig);
        c.init();
        load_loop(rig);
        c.cmd(CMD_CONTINUE, cont(0, 0x8001, 0, 0x8003));
        const bool none = rig.dbg->subscriptions(true).empty();
        const bool ran  = !run_until_paused(rig, 3);
        c.cmd(CMD_PAUSE);
        const Ntf n = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-CONT-05", "CMD_CONTINUE with both enable bytes 0 installs no temporary at "
                              "the addresses beside them: the loop through 0x8001 and 0x8003 runs "
                              "until a CMD_PAUSE (reason 1)",
              none && ran && n.ok && n.reason == BREAK_MANUAL);
    }
    {
        // A stop the BACKEND did not cause (a legacy BreakpointSet PC
        // breakpoint) leaves its transients armed; the adapter removes what is
        // left of the CONTINUE's temporaries itself, or the next CONTINUE —
        // which asked for none — would stop at a stale one.
        Rig rig;
        Dz  c(rig);
        c.init();
        load_loop(rig);
        rig.emu.debug_state().breakpoints().add_pc(0x8001);
        c.cmd(CMD_CONTINUE, cont(1, 0x8003));
        run_until_paused(rig, 2);
        c.tick();
        const bool legacy_stop = c.ntfs.size() == 1 && rig.emu.cpu().get_registers().PC == 0x8001;
        const bool swept       = rig.dbg->subscriptions(true).empty();
        rig.emu.debug_state().breakpoints().remove_pc(0x8001);
        c.ntfs.clear();
        c.cmd(CMD_CONTINUE, cont());
        const bool ran = !run_until_paused(rig, 3);
        c.cmd(CMD_PAUSE);
        const Ntf n = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-TEMP-06", "after a stop the backend did not cause (a legacy PC breakpoint), "
                              "the CONTINUE's leftover temporary is removed: the next plain "
                              "CONTINUE runs past 0x8003 until a CMD_PAUSE",
              legacy_stop && swept && ran && n.ok && n.reason == BREAK_MANUAL,
              ntf_str(n));
    }
}

static void notify_rows() {
    {
        // CMD_PAUSE on a RUNNING machine: reply, THEN one NTF reason 1 — the
        // spec's order — with the PC and its bank.
        Rig rig;
        Dz  c(rig);
        c.init();
        load_loop(rig);
        c.cmd(CMD_CONTINUE, cont());
        rig.emu.run_frame();
        c.order.clear();
        const Resp r = c.cmd(CMD_PAUSE);
        const Ntf  n = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        const std::uint16_t pc = rig.emu.cpu().get_registers().PC;
        check("DZRP-PAUSE-01", "CMD_PAUSE on a running machine: the reply, then ONE NTF_PAUSE "
                               "reason 1 at the PC with its bank byte — in that order",
              r.len == 1 && rig.dbg->state().paused && c.order == "RN" && n.ok &&
                  n.reason == BREAK_MANUAL && n.addr == pc && n.bank == 5 && n.text.empty(),
              c.order + " " + ntf_str(n));
        c.ntfs.clear();
        c.order.clear();
        const Resp again = c.cmd(CMD_PAUSE);
        for (int i = 0; i < 3; ++i) c.tick();
        check("DZRP-PAUSE-02", "CMD_PAUSE on a machine already paused: the reply and nothing "
                               "else — \"nothing happens\" (spec)",
              again.len == 1 && c.order == "R" && c.ntfs.empty());
    }
    {
        // A CMD_PAUSE that stops a machine ANOTHER client ran — no CONTINUE of
        // ours outstanding — still stopped something, so it is notified.
        Rig  rig;
        auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        Dz   c(rig);
        c.init();
        load_loop(rig);
        rig.dbg->run(other);
        rig.pump();
        const Resp r = c.cmd(CMD_PAUSE);
        const Ntf  n = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-PAUSE-03", "a CMD_PAUSE that stops a machine another client resumed is "
                               "notified too (it stopped something), reason 1",
              r.len == 1 && n.ok && n.reason == BREAK_MANUAL);
        rig.dbg->detach(other);
    }
    {
        // A CMD_PAUSE on a machine ANOTHER client paused: nothing happens — the
        // pause stays that client's (a DZRP detach must not release it), and
        // nothing is owed (a later stop by that client sends no NTF).
        Rig  rig;
        auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        Dz   c(rig);
        c.init();
        load_loop(rig);
        rig.dbg->run(other);
        rig.pump();
        rig.emu.run_frame();
        rig.dbg->pause(other);
        rig.pump();
        c.ntfs.clear();
        c.order.clear();
        const Resp r    = c.cmd(CMD_PAUSE);
        const auto kept = rig.dbg->state().pause_reason.by;
        rig.dbg->run(other);
        c.tick();
        rig.emu.run_frame();
        rig.dbg->pause(other);
        for (int i = 0; i < 3; ++i) c.tick();
        c.p->close();
        rig.pump(3);
        check("DZRP-PAUSE-04", "CMD_PAUSE on a machine another client paused: the reply alone, "
                               "the pause stays that client's, nothing is owed (its next stop "
                               "sends no NTF), and DZRP's hang-up leaves the machine paused",
              r.len == 1 && c.order == "R" && kept == other && c.ntfs.empty() &&
                  rig.dbg->state().paused && rig.dbg->state().pause_reason.by == other);
        rig.dbg->detach(other);
    }
    {
        // EXACTLY ONCE: one NTF per CONTINUE. A later stop with nothing owed —
        // another client pausing, resuming and pausing again — sends nothing.
        Rig  rig;
        auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        Dz   c(rig);
        c.init();
        load_loop(rig);
        c.cmd(CMD_ADD_BREAKPOINT, add_bp(0x8002, 0));
        c.cmd(CMD_CONTINUE, cont());
        run_until_paused(rig);
        c.tick();
        const std::size_t first = c.ntfs.size();
        rig.dbg->run(other);
        c.tick();
        rig.emu.run_frame();
        rig.dbg->pause(other);
        for (int i = 0; i < 3; ++i) c.tick();
        check("DZRP-NTF-04", "exactly one NTF_PAUSE per CONTINUE: after the breakpoint's, a later "
                              "stop by another client, with no CONTINUE outstanding, sends nothing",
              first == 1 && c.ntfs.size() == 1 && rig.dbg->state().paused);
        rig.dbg->detach(other);
    }
    {
        // A stop that is not this client's to name, while its CONTINUE is
        // outstanding: reason 255 and a string (design §3.3, §4.4).
        Rig  rig;
        auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        Dz   c(rig);
        c.init();
        load_loop(rig);
        c.cmd(CMD_CONTINUE, cont());
        rig.emu.run_frame();
        rig.dbg->pause(other);
        c.tick();
        const Ntf n = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-NTF-05", "another client's pause during this client's CONTINUE is reported "
                             "reason 255, \"paused by another debugger client\", at the PC",
              n.ok && n.reason == BREAK_OTHER && n.text == "paused by another debugger client" &&
                  n.addr == rig.emu.cpu().get_registers().PC,
              ntf_str(n));
        rig.dbg->detach(other);
    }
    {
        // A REFUSED RESUME: the CONTINUE reply cannot carry it, so the refusal
        // is the stop DeZog waits for — NTF 255 with the reason — and the
        // machine stays paused (§3.2).
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        load_loop(rig);
        const std::vector<std::uint8_t> junk(64, 0x5A);
        const auto other = rig.dbg->attach({"loader", jnext::dbg::ClientKind::Test}).value;
        const Result latched = rig.dbg->load_state_bytes(other, junk.data(), junk.size());
        c.order.clear();
        const Resp r = c.cmd(CMD_CONTINUE, cont(1, 0x8001));
        const Ntf  n = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-CONT-04", "a CONTINUE the backend refuses (a latched corruption) is answered, "
                              "then NTF_PAUSE 255 \"resume refused: refused_corrupt …\"; the "
                              "machine stays paused and the temporary is removed",
              latched == Result::RefusedCorrupt && r.len == 1 && c.order == "RN" && n.ok &&
                  n.reason == BREAK_OTHER &&
                  n.text.rfind("resume refused: refused_corrupt", 0) == 0 &&
                  rig.dbg->state().paused && rig.dbg->subscriptions(true).empty(),
              c.order + " " + ntf_str(n));
        rig.dbg->detach(other);
    }
    {
        // A HARD RESET never pauses a running machine, and DZRP has no reset
        // notification: nothing is sent; the outstanding CONTINUE is answered
        // by the NEXT stop (§3.3's Reset{Hard} row).
        Rig rig;
        jnext::dbg::LoopDriver drv;
        drv.cold_boot = [&rig]() {
            EmulatorConfig cfg = rig.emu.config();
            rig.emu.init(cfg);
            return true;
        };
        rig.dbg->set_loop_driver(drv);
        const auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        Dz c(rig);
        c.init();
        load_loop(rig);
        c.cmd(CMD_CONTINUE, cont());
        rig.emu.run_frame();
        const Result reset = rig.dbg->reset(other, jnext::dbg::ResetKind::Hard);
        for (int i = 0; i < 3; ++i) c.tick();
        const std::size_t after_reset = c.ntfs.size();
        const bool        running     = !rig.dbg->state().paused;
        c.cmd(CMD_PAUSE);
        const Ntf n = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-RST-01", "a hard reset during this client's CONTINUE sends nothing and leaves "
                             "the machine running; the next stop (a CMD_PAUSE) is the one "
                             "NTF_PAUSE",
              reset == Result::Ok && after_reset == 0 && running && c.ntfs.size() == 1 && n.ok &&
                  n.reason == BREAK_MANUAL,
              "reset=" + std::string(jnext::dbg::result_name(reset)));
        rig.dbg->detach(other);
    }
}

static void interrupt_rows() {
    // IM 2 with a 257-byte vector table of 0x92 at 0x9000 (I = 0x90), so
    // whatever byte the bus carries the vector is 0x9292 — the handler: EI; RETI.
    auto prime = [](Rig& rig) {
        load_loop(rig);
        for (std::uint16_t a = 0x9000; a <= 0x9100; ++a) rig.emu.mmu().write(a, 0x92);
        load_prog(rig, 0x9292, {0xFB, 0xED, 0x4D});
        Z80Registers r = rig.emu.cpu().get_registers();
        r.I    = 0x90;
        r.IM   = 2;
        r.IFF1 = 1;
        r.IFF2 = 1;
        rig.emu.cpu().set_registers(r);
    };
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        prime(rig);
        const Resp on = c.cmd(CMD_ENABLE_BREAK_ON_INTERRUPT, bytes({1}));
        c.cmd(CMD_CONTINUE, cont());
        const bool stopped = run_until_paused(rig, 3);
        c.tick();
        const Ntf n = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-BOI-01", "CMD_ENABLE_BREAK_ON_INTERRUPT 1: the next accepted interrupt stops "
                             "the machine within a frame, NTF_PAUSE 255 \"Break on interrupt.\" "
                             "at the handler's entry (IM 2 → 0x9292)",
              on.len == 1 && stopped && n.ok && n.reason == BREAK_OTHER &&
                  n.text == "Break on interrupt." && n.addr == 0x9292,
              ntf_str(n));
        c.ntfs.clear();
        const Resp off = c.cmd(CMD_ENABLE_BREAK_ON_INTERRUPT, bytes({0}));
        c.cmd(CMD_CONTINUE, cont());
        const bool ran = !run_until_paused(rig, 5);
        c.cmd(CMD_PAUSE);
        const Ntf p = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        check("DZRP-BOI-02", "CMD_ENABLE_BREAK_ON_INTERRUPT 0: five frames of interrupts pass "
                             "without a stop; the CMD_PAUSE that ends them is reason 1",
              off.len == 1 && ran && p.ok && p.reason == BREAK_MANUAL &&
                  rig.dbg->subscriptions(true).empty());
    }
}

// ── DZRP-WP — watchpoints, 42/43 (WP-4) ─────────────────────────────────────

static std::string wp(std::uint16_t addr, int bank1, std::uint16_t size, int access) {
    return u16s(addr) + bytes({bank1}) + u16s(size) + bytes({access});
}

static void watch_rows() {
    // Each program: LD (nn),A or LD A,(nn), then JR $.
    auto store = [](Rig& rig, std::uint16_t target) {
        load_prog(rig, 0x8000, {0x32, target & 0xFF, target >> 8, 0x18, 0xFE});
        set_pc(rig, 0x8000);
    };
    auto fetch = [](Rig& rig, std::uint16_t target) {
        load_prog(rig, 0x8000, {0x3A, target & 0xFF, target >> 8, 0x18, 0xFE});
        set_pc(rig, 0x8000);
    };
    // CONTINUE, run up to `frames`; return the NTF that ends it (a CMD_PAUSE
    // ends it when nothing stopped it).
    auto go = [](Rig& rig, Dz& c, int frames) {
        c.ntfs.clear();
        c.cmd(CMD_CONTINUE, cont());
        const bool stopped = run_until_paused(rig, frames);
        if (stopped)
            c.tick();
        else
            c.cmd(CMD_PAUSE);
        return c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
    };
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        const Resp a = c.cmd(CMD_ADD_WATCHPOINT, wp(0x9000, 0, 0x100, 2));
        const auto subs = rig.dbg->subscriptions(false);
        check("DZRP-WP-01", "CMD_ADD_WATCHPOINT 0x9000 size 0x100 write answers error 0 and is "
                            "ONE Mem[0x9000, 0x90FF] write subscription that stops",
              a.payload == bytes({0}) && subs.size() == 1 &&
                  subs[0].kind == jnext::dbg::EventKind::Mem && subs[0].filter.lo == 0x9000 &&
                  subs[0].filter.hi == 0x90FF && subs[0].access == jnext::dbg::Access::Write &&
                  subs[0].action == jnext::dbg::Action::Stop);
        // BOTH EDGES, BOTH SIDES.
        store(rig, 0x8FFF);
        const Ntf below = go(rig, c, 2);
        store(rig, 0x9000);
        const Ntf lo = go(rig, c, 2);
        store(rig, 0x90FF);
        const Ntf hi = go(rig, c, 2);
        store(rig, 0x9100);
        const Ntf above = go(rig, c, 2);
        check("DZRP-WP-02", "a write to 0x8FFF and to 0x9100 — one below and one above — does not "
                            "stop; a write to 0x9000 and to 0x90FF — the two ends — does",
              below.ok && below.reason == BREAK_MANUAL && above.ok &&
                  above.reason == BREAK_MANUAL && lo.ok && lo.reason == BREAK_WP_WRITE &&
                  hi.ok && hi.reason == BREAK_WP_WRITE,
              ntf_str(below) + " | " + ntf_str(lo) + " | " + ntf_str(hi) + " | " + ntf_str(above));
        check("DZRP-WP-03", "the stop reports reason 4 (write), the ACCESSED address (0x9000, "
                            "0x90FF) and its slot's bank byte (page 4 at slot 4 → 5)",
              lo.addr == 0x9000 && hi.addr == 0x90FF && lo.bank == 5 && hi.bank == 5,
              ntf_str(lo) + " | " + ntf_str(hi));
        fetch(rig, 0x9000);
        const Ntf read = go(rig, c, 2);
        check("DZRP-WP-04", "a write-only watchpoint is not stopped by a READ of its range",
              read.ok && read.reason == BREAK_MANUAL, ntf_str(read));
    }
    {
        Rig rig;
        Dz  c(rig);
        c.init();
        c.cmd(CMD_ADD_WATCHPOINT, wp(0x9000, 0, 1, 1));
        fetch(rig, 0x9000);
        const Ntf r = go(rig, c, 2);
        store(rig, 0x9000);
        const Ntf w = go(rig, c, 2);
        check("DZRP-WP-05", "a read watchpoint stops a read with reason 3 at the address, and "
                            "is not stopped by a write",
              r.ok && r.reason == BREAK_WP_READ && r.addr == 0x9000 && w.ok &&
                  w.reason == BREAK_MANUAL,
              ntf_str(r) + " | " + ntf_str(w));
    }
    {
        // BANK: bank+1 = 15 fires only with page 14 behind the address, and the
        // NTF carries the accessed address's bank byte — no filtering, no
        // auto-continue (§5.4).
        Rig rig;
        Dz  c(rig);
        c.init();
        c.cmd(CMD_ADD_WATCHPOINT, wp(0xC010, 15, 1, 2));
        c.cmd(CMD_SET_SLOT, bytes({6, 14}));
        store(rig, 0xC010);
        const Ntf in14 = go(rig, c, 2);
        c.cmd(CMD_SET_SLOT, bytes({6, 16}));
        store(rig, 0xC010);
        const Ntf in16 = go(rig, c, 2);
        check("DZRP-WP-06", "a watchpoint with bank+1 = 15 stops a write with page 14 at slot 6 "
                            "(reason 4, bank byte 15) and not with page 16 there",
              in14.ok && in14.reason == BREAK_WP_WRITE && in14.addr == 0xC010 &&
                  in14.bank == 15 && in16.ok && in16.reason == BREAK_MANUAL,
              ntf_str(in14) + " | " + ntf_str(in16));
    }
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        const Resp z  = c.cmd(CMD_ADD_WATCHPOINT, wp(0x9000, 0, 0, 2));
        const Resp na = c.cmd(CMD_ADD_WATCHPOINT, wp(0x9000, 0, 1, 0));
        const Resp wr = c.cmd(CMD_ADD_WATCHPOINT, wp(0xFFF0, 0, 0x20, 2));
        const Resp nb = c.cmd(CMD_ADD_WATCHPOINT, wp(0x9000, 225, 1, 2));
        const Resp sh = c.cmd(CMD_ADD_WATCHPOINT, wp(0x9000, 0, 1, 2).substr(0, 5));
        const Resp ok = c.cmd(CMD_ADD_WATCHPOINT, wp(0xFFF0, 0, 0x10, 2));
        check("DZRP-WP-07", "CMD_ADD_WATCHPOINT answers error 1 and subscribes nothing for size "
                            "0, no access bit, a range past 0xFFFF, bank 224 and a 5-byte "
                            "payload — each with a warn line; 0xFFF0 size 0x10 (ending at 0xFFFF) "
                            "is accepted",
              z.payload == bytes({1}) && na.payload == bytes({1}) && wr.payload == bytes({1}) &&
                  nb.payload == bytes({1}) && sh.payload == bytes({1}) &&
                  ok.payload == bytes({0}) && rig.dbg->subscriptions(false).size() == 1 &&
                  log.count("size 0 refused: size 0") == 1 &&
                  log.count("refused: neither read nor write") == 1 &&
                  log.count("refused: the range runs past 0xFFFF") == 1 &&
                  log.count("refused: no such bank") == 1 &&
                  log.count("malformed CMD_ADD_WATCHPOINT: payload is 5 bytes") == 1);
        // REMOVE by the exact tuple: a different access leaves it.
        const Resp miss = c.cmd(CMD_REMOVE_WATCHPOINT, wp(0xFFF0, 0, 0x10, 3));
        const bool kept = rig.dbg->subscriptions(false).size() == 1;
        const Resp hit  = c.cmd(CMD_REMOVE_WATCHPOINT, wp(0xFFF0, 0, 0x10, 2));
        check("DZRP-WP-08", "CMD_REMOVE_WATCHPOINT matches the exact (addr, bank, size, access) "
                            "tuple: a different access removes nothing and says so; the right "
                            "one removes it; both replies are the seq alone",
              miss.len == 1 && kept && hit.len == 1 && rig.dbg->subscriptions(false).empty() &&
                  log.count("CMD_REMOVE_WATCHPOINT: none at 0xFFF0") == 1);
    }
}

// ── DZRP-ST — state bookmarks, 50/51 (WP-4) ─────────────────────────────────

static void state_rows() {
    Rig    rig;
    LogTap log;
    Dz     c(rig);
    c.init();
    load_loop(rig);
    const Resp        s     = c.cmd(CMD_READ_STATE);
    const std::string token = s.payload;
    // The name is "dzrp-<client id>-<n>"; the backend must hold it for that client.
    const std::string name  = token.size() > 4 ? token.substr(4) : std::string();
    const auto        dash  = name.rfind('-');
    const jnext::dbg::ClientId owner =
        name.rfind("dzrp-", 0) == 0 && dash > 5
            ? static_cast<jnext::dbg::ClientId>(std::stoul(name.substr(5, dash - 5)))
            : 0;
    const auto held = rig.dbg->bookmarks(owner);
    check("DZRP-ST-01", "CMD_READ_STATE after CMD_INIT's pause (a frame boundary) answers a "
                        "TOKEN — \"JNXB\" + the name of a bookmark the backend now holds for "
                        "this client — not the snapshot",
          token.rfind("JNXB", 0) == 0 && name == "dzrp-" + std::to_string(owner) + "-1" &&
              held == std::vector<std::string>({name}),
          hex(token));

    // Change the machine, then restore the token.
    const Z80Registers saved = rig.emu.cpu().get_registers();
    const std::uint8_t mem0  = rig.emu.mmu().peek(0x9000);
    c.cmd(CMD_SET_REGISTER, bytes({3}) + u16s(0x1234));
    c.cmd(CMD_WRITE_MEM, bytes({0}) + u16s(0x9000) + bytes({static_cast<std::uint8_t>(~mem0)}));
    c.ntfs.clear();
    const Resp w = c.cmd(CMD_WRITE_STATE, token);
    for (int i = 0; i < 2; ++i) c.tick();
    check("DZRP-ST-02", "CMD_WRITE_STATE with that token restores it — registers and memory are "
                        "back — with a seq-only reply and no notification",
          w.len == 1 && c.ntfs.empty() && regs_equal(rig.emu.cpu().get_registers(), saved) &&
              rig.emu.mmu().peek(0x9000) == mem0);

    // MID-FRAME: after a breakpoint the machine is inside a frame, and the save
    // is REFUSED with a zero-length reply (F9, owner decision Q2).
    c.cmd(CMD_ADD_BREAKPOINT, add_bp(0x8002, 0));
    c.cmd(CMD_CONTINUE, cont());
    run_until_paused(rig);
    c.tick();
    const Resp mid = c.cmd(CMD_READ_STATE);
    check("DZRP-ST-03", "CMD_READ_STATE after a breakpoint (mid-frame) answers ZERO-length "
                        "and says why — it never advances the machine",
          mid.len == 1 && !rig.dbg->at_frame_boundary() &&
              log.count("CMD_READ_STATE refused: not_at_frame_boundary") == 1 &&
              rig.emu.cpu().get_registers().PC == 0x8002);

    // WHAT DeZog SENDS BACK after that refusal: a 0-byte CMD_WRITE_STATE. It
    // must reach no backend restore, latch nothing, and leave CONTINUE working.
    const Z80Registers at_bp = rig.emu.cpu().get_registers();
    c.ntfs.clear();
    c.order.clear();
    const Resp e = c.cmd(CMD_WRITE_STATE);
    const Ntf  en = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
    check("DZRP-ST-04", "an EMPTY CMD_WRITE_STATE is answered, then NTF_PAUSE 255 \"no state to "
                        "restore (save was refused mid-frame)\"; registers unchanged, nothing "
                        "latched",
          e.len == 1 && c.order == "RN" && en.ok && en.reason == BREAK_OTHER &&
              en.text == "no state to restore (save was refused mid-frame)" &&
              regs_equal(rig.emu.cpu().get_registers(), at_bp) &&
              !rig.dbg->resume_blocked_by_corruption(),
          ntf_str(en));
    c.cmd(CMD_REMOVE_BREAKPOINT, u16s(1));
    c.ntfs.clear();
    const Resp gar  = c.cmd(CMD_WRITE_STATE, std::string(200, '\x5A'));
    const Resp mag  = c.cmd(CMD_WRITE_STATE, "XXXB" + token.substr(4));
    const Resp un   = c.cmd(CMD_WRITE_STATE, "JNXBdzrp-99-1");
    const Resp shrt = c.cmd(CMD_WRITE_STATE, "JNXB");
    bool all255 = c.ntfs.size() == 4;
    for (const Resp& r : c.ntfs) {
        const Ntf n = parse_ntf(r);
        all255 = all255 && n.ok && n.reason == BREAK_OTHER &&
                 n.text.rfind("no state to restore", 0) == 0;
    }
    c.ntfs.clear();
    const std::uint8_t a0 = reg_a(rig);
    c.cmd(CMD_CONTINUE, cont(1, 0x8002));
    run_until_paused(rig);
    c.tick();
    check("DZRP-ST-05", "garbage, a wrong magic, an unissued name and a bare magic are each "
                        "answered and refused as \"no state to restore\" — no restore, no latch: "
                        "a CONTINUE afterwards still runs",
          gar.len == 1 && mag.len == 1 && un.len == 1 && shrt.len == 1 && all255 &&
              !rig.dbg->resume_blocked_by_corruption() && c.ntfs.size() == 1 &&
              reg_a(rig) == static_cast<std::uint8_t>(a0 + 1));

    // THE BOUND: 8 bookmarks per client; a 9th save is zero-length, never an
    // eviction. One is already held, so seven more succeed.
    // Back to a frame boundary: run the rest of the frame, then pause from
    // pump() — which lands on one (§4.2).
    c.cmd(CMD_CONTINUE, cont());
    rig.emu.run_frame();
    c.cmd(CMD_PAUSE);
    const bool boundary = rig.dbg->at_frame_boundary();
    int good = 0;
    for (int i = 0; i < 7; ++i) {
        const Resp r = c.cmd(CMD_READ_STATE);
        if (r.len > 1) ++good;
    }
    const Resp ninth = c.cmd(CMD_READ_STATE);
    c.ntfs.clear();
    const Resp first = c.cmd(CMD_WRITE_STATE, token);
    check("DZRP-ST-06", "a client holds at most 8 bookmarks: seven more saves succeed, the ninth "
                        "is zero-length with a warn line (never an eviction), and the first "
                        "token still restores",
          boundary && good == 7 && ninth.len == 1 &&
              log.count("CMD_READ_STATE refused: refused_unavailable") == 1 &&
              first.len == 1 && c.ntfs.empty() &&
              regs_equal(rig.emu.cpu().get_registers(), saved),
          "boundary=" + std::to_string(boundary) + " good=" + std::to_string(good) +
              " ninth=" + std::to_string(ninth.len) + " warns=" +
              std::to_string(log.count("CMD_READ_STATE refused: refused_unavailable")) +
              " first=" + std::to_string(first.len) + " ntfs=" + std::to_string(c.ntfs.size()) +
              " regs=" + std::to_string(regs_equal(rig.emu.cpu().get_registers(), saved)));

    // A TOKEN DIES WITH ITS SESSION: bookmarks are per client and go with the
    // detach, so a new session cannot restore the old token.
    c.p->close();
    rig.pump(3);
    Dz d(rig);
    d.init();
    const Resp old = d.cmd(CMD_WRITE_STATE, token);
    const Ntf  on  = d.ntfs.size() == 1 ? parse_ntf(d.ntfs[0]) : Ntf{};
    check("DZRP-ST-07", "a token from an earlier session is refused as \"no state to restore\" "
                        "— its bookmark died with the detach",
          old.len == 1 && on.ok && on.text == "no state to restore");
}

// ── DZRP-SES-14 — WP-3/WP-4 state is per session ───────────────────────────

static void session_state_rows() {
    // A session that ends mid-CONTINUE, holding a breakpoint and a watchpoint,
    // leaves nothing to the next one: ids restart at 1, nothing is owed (a stop
    // with no CONTINUE of the new session's own sends nothing), and the old
    // subscriptions went with the detach.
    Rig  rig;
    auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
    Dz   a(rig);
    a.init();
    load_loop(rig);
    a.cmd(CMD_ADD_BREAKPOINT, add_bp(0x9000, 0));
    a.cmd(CMD_ADD_WATCHPOINT, wp(0x9100, 0, 4, 2));
    a.cmd(CMD_CONTINUE, cont(1, 0x9200));
    a.p->close();
    rig.pump(3);
    const bool gone = rig.dbg->subscriptions(true).empty();
    Dz b(rig);
    b.init();
    rig.dbg->run(other);
    b.tick();
    rig.emu.run_frame();
    rig.dbg->pause(other);
    for (int i = 0; i < 3; ++i) b.tick();
    const Resp id = b.cmd(CMD_ADD_BREAKPOINT, add_bp(0x8000, 0));
    check("DZRP-SES-14", "a new session starts clean: the old session's breakpoint, watchpoint "
                         "and temporary went with its detach, nothing is owed (another client's "
                         "stop sends it nothing), and breakpoint ids restart at 1",
          gone && b.ntfs.empty() && bp_id(id) == 1);
    rig.dbg->detach(other);
}

// ── DZRP-SPR — sprites, 16-19 (WP-4) ────────────────────────────────────────

static void sprite_rows() {
    Rig    rig;
    LogTap log;
    Dz     c(rig);
    c.init();
    auto set_pal = [&](jnext::dbg::PaletteId id, int i, std::uint16_t v) {
        rig.dbg->set_palette(1, id, static_cast<std::uint8_t>(i), v);
    };
    set_pal(jnext::dbg::PaletteId::SpriteFirst, 0, 0x1FF);
    set_pal(jnext::dbg::PaletteId::SpriteFirst, 1, 0x001);
    set_pal(jnext::dbg::PaletteId::SpriteFirst, 255, 0x0AB);
    set_pal(jnext::dbg::PaletteId::SpriteSecond, 0, 0x100);
    const Resp p0 = c.cmd(CMD_GET_SPRITES_PALETTE, bytes({0}));
    const Resp p1 = c.cmd(CMD_GET_SPRITES_PALETTE, bytes({1}));
    const Resp p2 = c.cmd(CMD_GET_SPRITES_PALETTE, bytes({2}));
    check("DZRP-SPR-01", "CMD_GET_SPRITES_PALETTE answers 256 entries (512 bytes), each LE "
                         "RRRGGGBB, 0000000B: 0x1FF → FF 01, 0x001 → 00 01, 0x0AB → 55 01; "
                         "palette 1 is the second sprite bank (0x100 → 80 00)",
          p0.payload.size() == 512 && p0.payload.substr(0, 4) == bytes({0xFF, 0x01, 0x00, 0x01}) &&
              p0.payload.substr(510, 2) == bytes({0x55, 0x01}) && p1.payload.size() == 512 &&
              p1.payload.substr(0, 2) == bytes({0x80, 0x00}),
          hex(p0.payload.substr(0, 4)) + "| " + hex(p1.payload.substr(0, 2)));
    check("DZRP-SPR-02", "palette 2 does not exist: a seq-only reply and a warn line",
          p2.len == 1 && log.count("CMD_GET_SPRITES_PALETTE: no sprite palette 2") == 1);

    // NR 0x19 cycles through the sprite clip window's four bytes.
    for (int v : {10, 200, 20, 150}) rig.dbg->nextreg_write(1, 0x19, static_cast<std::uint8_t>(v));
    rig.dbg->nextreg_write(1, 0x15, 0x0B);
    const Resp clip = c.cmd(CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL);
    const auto w    = rig.dbg->sprite_clip();
    check("DZRP-SPR-03", "CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL answers x1 x2 y1 y2 from the "
                         "live clip window, then NR 0x15",
          clip.payload == bytes({w.x1, w.x2, w.y1, w.y2, 0x0B}) && w.x1 == 10 && w.x2 == 200 &&
              w.y1 == 20 && w.y2 == 150,
          hex(clip.payload));

    const std::uint8_t s0[5]   = {0x11, 0x12, 0x13, 0x14, 0x15};
    const std::uint8_t s1[5]   = {0x21, 0x22, 0x23, 0x24, 0x25};
    const std::uint8_t s127[5] = {0x71, 0x72, 0x73, 0x74, 0x75};
    rig.dbg->set_sprite_attr_raw(1, 0, s0, 5);
    rig.dbg->set_sprite_attr_raw(1, 1, s1, 5);
    rig.dbg->set_sprite_attr_raw(1, 127, s127, 5);
    auto attrs = [&](int i) {
        const auto a = rig.dbg->sprite_attr_raw(static_cast<std::uint8_t>(i)).value;
        return std::string(reinterpret_cast<const char*>(a.data()), 5);
    };
    const Resp g01  = c.cmd(CMD_GET_SPRITES, bytes({0, 2}));
    const Resp g127 = c.cmd(CMD_GET_SPRITES, bytes({127, 1}));
    const Resp gcl  = c.cmd(CMD_GET_SPRITES, bytes({126, 5}));
    const Resp g128 = c.cmd(CMD_GET_SPRITES, bytes({128, 1}));
    check("DZRP-SPR-04", "CMD_GET_SPRITES answers the 5 raw attribute bytes per sprite: sprites "
                         "0-1 are 10 bytes, 127 is 5; 126+5 is clamped to 2 sprites and 128+1 to "
                         "none, each with a warn line",
          g01.payload == attrs(0) + attrs(1) && attrs(0) == bytes({0x11, 0x12, 0x13, 0x14, 0x15}) &&
              g127.payload == attrs(127) && g127.payload.size() == 5 &&
              gcl.payload == attrs(126) + attrs(127) && g128.len == 1 &&
              log.count("CMD_GET_SPRITES 5 from 126 runs past sprite 127 — clamped to 2") == 1 &&
              log.count("CMD_GET_SPRITES 1 from 128 runs past sprite 127 — clamped to 0") == 1,
          hex(g01.payload));

    std::vector<std::uint8_t> pat(768);
    for (std::size_t i = 0; i < pat.size(); ++i) pat[i] = static_cast<std::uint8_t>(i * 3 + 1);
    rig.dbg->write_pattern_ram(1, 256, pat.data(), pat.size());
    const std::string want(reinterpret_cast<const char*>(pat.data()), 512);
    const Resp two  = c.cmd(CMD_GET_SPRITE_PATTERNS, bytes({1, 2}));
    const Resp four = c.cmd(CMD_GET_SPRITE_PATTERNS, u16s(1) + u16s(2));
    const Resp end  = c.cmd(CMD_GET_SPRITE_PATTERNS, bytes({63, 3}));
    const Resp past = c.cmd(CMD_GET_SPRITE_PATTERNS, bytes({64, 1}));
    const auto ram  = rig.dbg->pattern_ram();
    check("DZRP-SPR-05", "CMD_GET_SPRITE_PATTERNS answers count × 256 bytes of pattern RAM from "
                         "index × 256, in DeZog's 2-byte form and the spec's 4-byte form alike; "
                         "63+3 is clamped to one pattern, 64 to none",
          two.payload == want && four.payload == want &&
              end.payload == std::string(reinterpret_cast<const char*>(ram.data) + 63 * 256, 256) &&
              past.len == 1,
          std::to_string(two.payload.size()) + "/" + std::to_string(four.payload.size()));
}

// ── DZRP-EDGE — every bank / range / length check, from BOTH sides ──────────
//
// The milestone-1 and milestone-2 reviews each found a valid/invalid boundary
// tested from its invalid side only. These rows sweep every range check in
// src/remote/dzrp/ that a row did not already pin on both sides; the rest are
// listed in dzrp-frontend.md §14 with the rows that pin them.

static void edge_rows() {
    // bank1_to_page — the shared helper of CMD_ADD_BREAKPOINT and
    // CMD_ADD_WATCHPOINT: 0 → no qualifier; 1..224 → page 0..223; 225..254 →
    // refused (banks 224..253); 255 → a ROM bank, no qualifier.
    {
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        const Resp b1   = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0xC000, 1));
        const Resp b224 = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0xC000, 224));
        const Resp b254 = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0xC000, 254));
        const Resp w224 = c.cmd(CMD_ADD_WATCHPOINT, wp(0xC000, 224, 1, 2));
        const Resp w225 = c.cmd(CMD_ADD_WATCHPOINT, wp(0xC000, 225, 1, 2));
        const Resp w254 = c.cmd(CMD_ADD_WATCHPOINT, wp(0xC000, 254, 1, 2));
        const Resp w255 = c.cmd(CMD_ADD_WATCHPOINT, wp(0xC000, 255, 1, 2));
        const auto subs = rig.dbg->subscriptions(false);
        auto page_of = [&](jnext::dbg::EventKind k, std::size_t nth) {
            std::size_t seen = 0;
            for (const auto& x : subs)
                if (x.kind == k && seen++ == nth) return static_cast<int>(x.filter.page);
            return -1;
        };
        check("DZRP-EDGE-01", "bank+1 = 224 — bank 223, the last page — is a breakpoint and a "
                              "watchpoint on page 223; bank+1 = 1 is page 0, not \"any bank\" "
                              "(review of milestone 2, finding #1)",
              bp_id(b1) == 1 && bp_id(b224) == 2 && w224.payload == bytes({0}) &&
                  page_of(jnext::dbg::EventKind::Execute, 0) == 0 &&
                  page_of(jnext::dbg::EventKind::Execute, 1) == 223 &&
                  page_of(jnext::dbg::EventKind::Mem, 0) == 223);
        check("DZRP-EDGE-02", "bank+1 = 225 and 254 — banks 224 and 253 — are refused, a "
                              "breakpoint and a watchpoint alike; bank+1 = 255 (a ROM bank) "
                              "is armed with no qualifier",
              bp_id(b254) == 0 && w225.payload == bytes({1}) && w254.payload == bytes({1}) &&
                  w255.payload == bytes({0}) &&
                  page_of(jnext::dbg::EventKind::Mem, 1) == jnext::dbg::PAGE_ANY &&
                  log.count("CMD_ADD_BREAKPOINT at 0xC000 refused: no bank 253") == 1);
    }
    {
        // CMD_SET_SLOT: slot 7 is the last slot; bank 223 the last page; 0xFE
        // becomes 0xFF on slot 0 ONLY — on slot 1 it is passed to NEXTREG 0x51
        // as the guest's own write would (legacy ROM, register reads 0xFE).
        Rig rig(MachineType::ZXN_ISSUE2);
        Dz  c(rig);
        c.init();
        rig.emu.mmu().nr_page_ptr(223)[0] = 0xDF;
        const Resp s7   = c.cmd(CMD_SET_SLOT, bytes({7, 223}));
        const Resp rd   = c.cmd(CMD_READ_MEM, bytes({0}) + u16s(0xE000) + u16s(1));
        const Resp fe1  = c.cmd(CMD_SET_SLOT, bytes({1, 0xFE}));
        const Resp regs = c.cmd(CMD_GET_REGISTERS);
        check("DZRP-EDGE-03", "CMD_SET_SLOT slot 7 and bank 223 — the last of each — map (error "
                              "0); 0xFE on slot 1 is NOT rewritten to 0xFF (that is slot 0's rule) "
                              "and slot 1 still serves ROM",
              s7.payload == bytes({0}) && rd.payload == bytes({0xDF}) &&
                  fe1.payload == bytes({0}) && regs.payload.size() == 37 &&
                  static_cast<unsigned char>(regs.payload[29 + 7]) == 223 &&
                  static_cast<unsigned char>(regs.payload[29 + 1]) == 0xFE &&
                  rig.emu.mmu().is_slot_rom(1));
    }
    {
        // CMD_WRITE_BANK_MEM's refusal words: 253 is "no such bank", 254 the
        // first ROM bank.
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        c.cmd(CMD_WRITE_BANK_MEM, bytes({253}) + u16s(0) + bytes({1}));
        c.cmd(CMD_WRITE_BANK_MEM, bytes({254}) + u16s(0) + bytes({1}));
        check("DZRP-EDGE-04", "CMD_WRITE_BANK_MEM bank 253 is refused as \"no such bank\", bank "
                              "254 as \"ROM is read-only\"",
              log.count("CMD_WRITE_BANK_MEM to bank 253 refused: no such bank") == 1 &&
                  log.count("CMD_WRITE_BANK_MEM to bank 254 refused: ROM is read-only") == 1);
    }
    {
        // THE SHORTEST VALID PAYLOAD of each command whose minimum a MAL-01 row
        // pins from below: served, not "malformed".
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        const Resp bp  = c.cmd(CMD_ADD_BREAKPOINT, u16s(0x8000) + bytes({0}));  // no condition byte
        const Resp wm  = c.cmd(CMD_WRITE_MEM, bytes({0}) + u16s(0x9000));       // no data
        const Resp wbm = c.cmd(CMD_WRITE_BANK_MEM, bytes({3}) + u16s(0));       // no data
        const Resp in3 = c.cmd(CMD_INIT, bytes({2, 2, 0}));                     // no name
        check("DZRP-EDGE-05", "the shortest valid payloads are served, not refused: a 3-byte "
                              "CMD_ADD_BREAKPOINT (no condition string), a 3-byte CMD_WRITE_MEM "
                              "and CMD_WRITE_BANK_MEM (no data), a 3-byte CMD_INIT (no name)",
              bp_id(bp) == 1 && wm.len == 1 && wbm.len == 1 &&
                  in3.payload.substr(0, 1) == bytes({0}) && log.count("malformed") == 0);
    }
    {
        // CMD_INTERRUPT_ON_OFF: any non-zero byte is "on".
        Rig rig;
        Dz  c(rig);
        c.init();
        c.cmd(CMD_INTERRUPT_ON_OFF, bytes({0x80}));
        const Z80Registers r = rig.emu.cpu().get_registers();
        check("DZRP-EDGE-06", "CMD_INTERRUPT_ON_OFF 0x80 enables interrupts like 1 does",
              r.IFF1 && r.IFF2);
    }
    {
        // The sprite and pattern clamps: exactly at the end is not clamped (no
        // warn), one past is; the pattern command's 3-byte payload is the
        // 2-byte form with a byte to spare.
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        const Resp s127 = c.cmd(CMD_GET_SPRITES, bytes({127, 1}));
        const Resp p62  = c.cmd(CMD_GET_SPRITE_PATTERNS, bytes({62, 2}));
        const Resp p3   = c.cmd(CMD_GET_SPRITE_PATTERNS, bytes({1, 1, 0x40}));
        const auto ram  = rig.dbg->pattern_ram();
        check("DZRP-EDGE-07", "GET_SPRITES 127+1 and GET_SPRITE_PATTERNS 62+2 end exactly at the "
                              "last sprite / pattern: served whole, with no clamp warning; a 3-byte "
                              "GET_SPRITE_PATTERNS is the byte form (pattern 1, one pattern)",
              s127.payload.size() == 5 && p62.payload.size() == 512 &&
                  log.count("clamped") == 0 &&
                  p3.payload == std::string(reinterpret_cast<const char*>(ram.data) + 256, 256));
    }
    {
        // Breakpoint ids run to 65535, and past it a breakpoint is refused.
        // Added and removed in turn, so the backend's table stays small: ids
        // are never reused within a session, so the counter still reaches the
        // end.
        Rig    rig;
        LogTap log;
        Dz     c(rig);
        c.init();
        bool all = true;
        for (int i = 1; i < 0xFFFF && all; ++i) {
            const Resp a = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0x9000, 0));
            all = bp_id(a) == i;
            c.cmd(CMD_REMOVE_BREAKPOINT, u16s(static_cast<std::uint16_t>(i)));
        }
        const Resp last = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0x9000, 0));
        const Resp past = c.cmd(CMD_ADD_BREAKPOINT, add_bp(0x9000, 0));
        check("DZRP-EDGE-08", "breakpoint ids 1..65535 are issued once each; the 65535th is "
                              "served, the next add is refused with id 0 and a warn line",
              all && bp_id(last) == 0xFFFF && bp_id(past) == 0 &&
                  log.count("all 65535 ids of this session are used") == 1);
    }
}

// ── DZRP review items of milestone 2 ────────────────────────────────────────

static void review_m2_rows() {
    {
        // Finding #2 — BOTH DEBTS AT ONCE: this client's CONTINUE is
        // outstanding when its own CMD_PAUSE stops the machine. One NTF, and
        // both debts settle: a later stop by another client sends nothing.
        Rig  rig;
        auto other = rig.dbg->attach({"gui", jnext::dbg::ClientKind::Test}).value;
        Dz   c(rig);
        c.init();
        load_loop(rig);
        c.cmd(CMD_CONTINUE, cont());
        rig.emu.run_frame();
        c.cmd(CMD_PAUSE);
        const std::size_t first = c.ntfs.size();
        const Ntf n = first == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        rig.dbg->run(other);
        c.tick();
        rig.emu.run_frame();
        rig.dbg->pause(other);
        for (int i = 0; i < 3; ++i) c.tick();
        check("DZRP-PAUSE-05", "this client's own CMD_PAUSE during its own CONTINUE: exactly one "
                               "NTF (reason 1), and BOTH debts are settled — another client's "
                               "later stop sends nothing",
              first == 1 && n.ok && n.reason == BREAK_MANUAL && c.ntfs.size() == 1);
        rig.dbg->detach(other);
    }
    {
        // Finding #4 — the WHOLE 4-byte magic is checked: "JNXA" + a name the
        // backend holds is refused; "JNXB" + the same name restores.
        Rig rig;
        Dz  c(rig);
        c.init();
        const Resp s = c.cmd(CMD_READ_STATE);
        const std::string name = s.payload.substr(4);
        c.ntfs.clear();
        c.cmd(CMD_WRITE_STATE, "JNXA" + name);
        const Ntf  bad = c.ntfs.size() == 1 ? parse_ntf(c.ntfs[0]) : Ntf{};
        c.ntfs.clear();
        c.cmd(CMD_WRITE_STATE, "JNXB" + name);
        check("DZRP-ST-08", "a token whose 4th magic byte is wrong is refused even with a name "
                            "the backend holds; the right magic with that name restores (no NTF)",
              bad.ok && bad.text == "no state to restore" && c.ntfs.empty());
    }
    {
        // Finding #6 — a second CMD_ENABLE_BREAK_ON_INTERRUPT 1 does not stack
        // a second IntAck subscription, so one 0 removes it.
        Rig rig;
        Dz  c(rig);
        c.init();
        c.cmd(CMD_ENABLE_BREAK_ON_INTERRUPT, bytes({1}));
        c.cmd(CMD_ENABLE_BREAK_ON_INTERRUPT, bytes({1}));
        const std::size_t armed = rig.dbg->subscriptions(true).size();
        c.cmd(CMD_ENABLE_BREAK_ON_INTERRUPT, bytes({0}));
        check("DZRP-BOI-03", "enabling break-on-interrupt twice arms ONE subscription, and one "
                             "disable removes it",
              armed == 1 && rig.dbg->subscriptions(true).empty());
    }
}

int main() {
    std::printf("dzrp_adapter_test — the DZRP adapter over T's fake transport (GH #12)\n");
    framing_rows();
    session_rows();
    supported_rows();
    register_rows();
    memory_rows();
    bank_rows();
    slot_rows();
    nextreg_port_rows();
    malformed_rows();
    breakpoint_rows();
    temp_rows();
    notify_rows();
    interrupt_rows();
    watch_rows();
    state_rows();
    sprite_rows();
    session_state_rows();
    edge_rows();
    review_m2_rows();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass,
                g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
