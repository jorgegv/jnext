// jnext::remote::dzrp::DzrpTarget (GH #515, epic #486): a `Debugger` over a REMOTE JNEXT.
//
// WHAT IS UNDER TEST. `DzrpTarget` (src/remote/dzrp/dzrp_target.*), the non-local facade's
// run control, breakpoint and watchpoint paths (src/debug/debugger_events.cpp, _control.cpp,
// _session.cpp), end to end over a memory link against the SHIPPED `DzrpServer` on a real
// machine. There is no VHDL counterpart (debugger architecture, jnext-internal).
//
// ORACLES. (1) The same scenario run on a REFERENCE jnext through its LOCAL facade: two
// identical `ZXN_ISSUE2` machines, one behind the local `Debugger`, one served over DZRP and
// read through a `Debugger(Target&)`. (2) The DZRP 2.2.0 wire contract the shipped server
// implements (src/remote/dzrp/dzrp_server.cpp) and DeZog's `DeZogProtocol.md`, for the rows
// that run against a scripted peer.
//
//   RTG-INS-*  inspection (R2.1): registers, memory, slots, NextREGs, ports, sprites, the
//              cache and the cheap reads, failure and the profiles.
//   RTG-RUN-*  run control and execute breakpoints (R2.2).
//   RTG-WP-*   watchpoints (R2.3).
//
// No socket anywhere; EVERY WAIT IS BOUNDED: the client's clock is a fake one that advances
// on every read, and every loop has an iteration cap.
//
// Run: ./build/test/remote_target_test

#include "remote/client_link.h"
#include "remote/dzrp/dzrp_client.h"
#include "remote/dzrp/dzrp_frame.h"
#include "remote/dzrp/dzrp_server.h"
#include "remote/dzrp/dzrp_target.h"
#include "remote/fake_transport.h"
#include "remote/memory_link.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "debug/debugger.h"
#include "platform/emulator_boot.h"
#include "memory/mmu.h"
#include "port/nextreg.h"
#include "video/sprites.h"
#include "version.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "../row_id.h"

using jnext::dbg::Access;
using jnext::dbg::Capability;
using jnext::dbg::ClientId;
using jnext::dbg::Debugger;
using jnext::dbg::EventKind;
using jnext::dbg::MemSpace;
using jnext::dbg::PauseReason;
using jnext::dbg::PumpBudget;
using jnext::dbg::RegId;
using jnext::dbg::RegisterValidity;
using jnext::dbg::Result;
using jnext::remote::ClientLink;
using jnext::remote::FakeListener;
using jnext::remote::LinkKind;
using jnext::remote::MemoryLink;
using namespace jnext::remote::dzrp;
using SteadyClock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using Bytes = std::vector<std::uint8_t>;

// -- Tiny test harness (matches dzrp_client_test) -----------------------------------------------

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_skip  = 0;

static void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc, detail.empty() ? "" : " - ", detail.c_str());
    }
}

// -- The fake clock the client reads: every read moves it, so a wait nothing answers times out ----

static SteadyClock::time_point g_now = SteadyClock::time_point{} + std::chrono::hours(1);
static SteadyClock::time_point fake_now() {
    g_now += milliseconds(1);
    return g_now;
}
static SteadyClock::time_point server_clock() { return SteadyClock::time_point{} + std::chrono::hours(1); }

static const char* const TOKEN = "0123456789abcdef";   // never this process's token

static ClientConfig cfg() {
    ClientConfig c;
    c.session_token = TOKEN;
    c.clock         = fake_now;
    return c;
}

struct LogTap {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> ring =
        std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(1024);
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

// -- Listener, subscription and byte helpers ------------------------------------------------------

struct StopListener : jnext::dbg::Listener {
    std::vector<jnext::dbg::PausedInfo> paused;
    std::vector<std::string>            logs;
    int                                 resumed = 0;
    void on_paused(const jnext::dbg::PausedInfo& p) override { paused.push_back(p); }
    void on_resumed(ClientId) override { ++resumed; }
    void on_reset(jnext::dbg::ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(jnext::dbg::LogLevel, const std::string& t) override { logs.push_back(t); }
    std::vector<std::string> event_logs() const {
        std::vector<std::string> out;
        for (const auto& l : logs)
            if (l.rfind("EVENT ", 0) == 0) out.push_back(l);
        return out;
    }
    int mutates() const {
        int n = 0;
        for (const auto& l : logs)
            if (l.rfind("MUTATE ", 0) == 0) ++n;
        return n;
    }
};

static jnext::dbg::Subscription exec_sub(uint16_t addr) {
    jnext::dbg::Subscription s;
    s.kind      = EventKind::Execute;
    s.filter.lo = s.filter.hi = addr;
    return s;
}

static jnext::dbg::Subscription mem_sub(uint16_t lo, uint16_t hi, Access a) {
    jnext::dbg::Subscription s;
    s.kind      = EventKind::Mem;
    s.filter.lo = lo;
    s.filter.hi = hi;
    s.access    = a;
    return s;
}

using Cond = std::function<bool(const jnext::dbg::Event&, const Debugger&)>;

static Bytes pattern(std::size_t n, unsigned salt) {
    Bytes s(n);
    for (std::size_t i = 0; i < n; ++i) s[i] = static_cast<std::uint8_t>((i * 7u + salt + (i >> 8)) & 0xFF);
    return s;
}

static unsigned a_reg(const Debugger& d) { return d.registers().AF >> 8; }

static bool regs_equal(const Z80Registers& a, const Z80Registers& b) {
    return a.PC == b.PC && a.SP == b.SP && a.AF == b.AF && a.BC == b.BC && a.DE == b.DE && a.HL == b.HL &&
           a.IX == b.IX && a.IY == b.IY && a.AF2 == b.AF2 && a.BC2 == b.BC2 && a.DE2 == b.DE2 &&
           a.HL2 == b.HL2 && a.R == b.R && a.I == b.I && a.IM == b.IM;
}

// -- The programs ----------------------------------------------------------------------------------
//
// LOOP, at 0x8000, writing and reading `data` on every pass:
//   8000 LD A,0 / 8002 INC A / 8003 LD (data),A / 8006 LD A,(data) / 8009 JR 8002
static Bytes prog_loop(uint16_t data) {
    return {0x3E, 0x00, 0x3C, 0x32, static_cast<std::uint8_t>(data), static_cast<std::uint8_t>(data >> 8),
            0x3A, static_cast<std::uint8_t>(data), static_cast<std::uint8_t>(data >> 8), 0x18, 0xF7};
}
// DJNZ: a body that runs exactly seven times, then parks:
//   8000 LD B,7 / 8002 NOP / 8003 DJNZ 8002 / 8005 JR $
static Bytes prog_djnz() { return {0x06, 0x07, 0x00, 0x10, 0xFD, 0x18, 0xFE}; }

struct Machine {
    Emulator                  emu;
    std::unique_ptr<Debugger> dbg;
    void init(MachineType type, const Bytes& code) {
        EmulatorConfig ec;
        ec.type = type;
        emu.init(ec);
        // Memory the reads can tell apart, around the program and its data cell.
        for (uint32_t a = 0x4000; a < 0x10000; ++a) {
            if ((a >= 0x8000 && a < 0x8040) || (a >= 0x9000 && a < 0x9020)) continue;
            emu.mmu().write(static_cast<uint16_t>(a), static_cast<uint8_t>((a * 31u + (a >> 8)) & 0xFF));
        }
        for (std::size_t i = 0; i < code.size(); ++i) emu.mmu().write(static_cast<uint16_t>(0x8000 + i), code[i]);
        Z80Registers r = emu.cpu().get_registers();
        r.PC   = 0x8000;
        r.SP   = 0xFF00;
        r.IFF1 = 0;
        r.IFF2 = 0;
        r.BC   = 0x1234;
        r.DE   = 0x5678;
        r.HL   = 0x9ABC;
        emu.cpu().set_registers(r);
        dbg = std::make_unique<Debugger>(emu);
    }
};

// -- A link that counts what the client writes ------------------------------------------------------

struct CountingLink final : ClientLink {
    std::unique_ptr<ClientLink>                   in;
    std::vector<std::pair<std::uint8_t, Bytes>>   frames;   // id, payload
    std::size_t                                   mark_at = 0;
    long                                          first_reply_at = -1;
    std::size_t                                   rounds = 0;       // writes with no read since the last write
    bool                                          wrote_since_read = false;
    explicit CountingLink(std::unique_ptr<ClientLink> l) : in(std::move(l)) {}
    bool open() override { return in->open(); }
    void poll(int w) override { in->poll(w); }
    jnext::remote::LinkState state() const override { return in->state(); }
    std::size_t read(std::uint8_t* b, std::size_t cap) override {
        const std::size_t n = in->read(b, cap);
        if (n > 0) wrote_since_read = false;
        if (n > 0 && first_reply_at < 0) first_reply_at = static_cast<long>(frames.size() - mark_at);
        return n;
    }
    bool write(const std::uint8_t* p, std::size_t n) override {
        if (n >= COMMAND_HEADER_BYTES) frames.push_back({p[5], Bytes(p + COMMAND_HEADER_BYTES, p + n)});
        if (!wrote_since_read) ++rounds;   // a burst: the frames written between two reads
        wrote_since_read = true;
        return in->write(p, n);
    }
    void close() override { in->close(); }
    LinkKind kind() const override { return in->kind(); }
    std::string describe() const override { return in->describe(); }
    const std::string& last_error() const override { return in->last_error(); }

    void mark() { mark_at = frames.size(); first_reply_at = -1; rounds = 0; }
    std::size_t since() const { return frames.size() - mark_at; }
    int count(int id) const {
        int n = 0;
        for (const auto& f : frames) n += f.first == id ? 1 : 0;
        return n;
    }
    int count_since(int id) const {
        int n = 0;
        for (std::size_t i = mark_at; i < frames.size(); ++i) n += frames[i].first == id ? 1 : 0;
        return n;
    }
    const Bytes* last_payload(int id) const {
        for (std::size_t i = frames.size(); i-- > 0;)
            if (frames[i].first == id) return &frames[i].second;
        return nullptr;
    }
};

// -- The rig: a reference machine, a served machine, the shipped server, the target, a facade -------

struct Pair {
    // Declared first, so destroyed last: the Debuggers below keep pointers to them.
    StopListener                rl;       // the remote facade's client
    StopListener                refl;     // the reference facade's client
    Machine                     ref;
    Machine                     srv;
    std::unique_ptr<DzrpServer> server;
    FakeListener*               lsn  = nullptr;
    CountingLink*               link = nullptr;
    std::unique_ptr<DzrpTarget> tgt;
    std::unique_ptr<Debugger>   remote;
    ClientId                    ra = 0;   // client of the remote facade
    ClientId                    rc = 0;   // client of the reference facade

    explicit Pair(const Bytes& code = prog_loop(0x9000), bool connect = true,
                  MachineType type = MachineType::ZXN_ISSUE2) {
        ref.init(type, code);
        srv.init(type, code);
        server = std::make_unique<DzrpServer>(*srv.dbg, server_clock);
        auto l = std::make_unique<FakeListener>();
        lsn    = l.get();
        server->server().open(std::move(l), "127.0.0.1", 0);
        srv.dbg->add_service(server->server());
        rc = ref.dbg->attach(client_info("ref")).value;
        ref.dbg->set_listener(rc, &refl);
        if (connect) connect_remote();
    }
    static jnext::dbg::ClientInfo client_info(const char* name) {
        jnext::dbg::ClientInfo ci;
        ci.name = name;
        ci.kind = jnext::dbg::ClientKind::Test;
        return ci;
    }

    void connect_remote() {
        auto cl_link = std::make_unique<CountingLink>(std::make_unique<MemoryLink>(*lsn, LinkKind::Tcp));
        link         = cl_link.get();
        auto cl = std::make_unique<DzrpClient>(std::move(cl_link), cfg());
        cl->set_wait_hook([this] { srv.dbg->pump(PumpBudget{}); });
        cl->start();
        for (int i = 0; i < 200; ++i) {
            cl->step();
            srv.dbg->pump(PumpBudget{});
            if (cl->state() == SessionState::Ready || cl->state() == SessionState::Failed) break;
        }
        tgt    = std::make_unique<DzrpTarget>(std::move(cl));
        remote = std::make_unique<Debugger>(*tgt);
        remote->add_service(*tgt);
        ra = remote->attach(client_info("remote")).value;
        remote->set_listener(ra, &rl);
        remote->pump(PumpBudget{});   // primes the edge detector on the paused target
        // The reference mirrors CMD_INIT's pause of a running machine.
        ref.dbg->pause(rc);
        link->mark();
    }
    ~Pair() { disconnect(); }
    void disconnect() {
        remote.reset();
        tgt.reset();
    }
    /// One loop-owner tick: a frame on the served machine when it runs, then both pumps.
    void tick() {
        if (!srv.dbg->state().paused) srv.emu.run_frame();
        srv.dbg->pump(PumpBudget{});
        if (remote) remote->pump(PumpBudget{});
    }
    /// The reference's tick.
    void ref_tick() {
        if (!ref.dbg->state().paused) ref.emu.run_frame();
        ref.dbg->pump(PumpBudget{});
    }
    bool remote_run_to_stop(int max = 400) {
        remote->run(ra);
        for (int i = 0; i < max; ++i) {
            tick();
            if (remote->state().paused) return true;
        }
        return false;
    }
    bool ref_run_to_stop(int max = 400) {
        ref.dbg->run(rc);
        for (int i = 0; i < max; ++i) {
            ref_tick();
            if (ref.dbg->state().paused) return true;
        }
        return false;
    }
    /// Run both for `n` ticks; true when neither stopped.
    bool both_run_quiet(int n) {
        remote->run(ra);
        ref.dbg->run(rc);
        for (int i = 0; i < n; ++i) {
            tick();
            ref_tick();
        }
        return !remote->state().paused && !ref.dbg->state().paused;
    }
    uint32_t slot_page(int slot) const { return ref.dbg->mmu_slots()[static_cast<std::size_t>(slot)].nr_page; }
};

// -- A scripted DZRP peer (the dzrp_client_test idiom) ----------------------------------------------

static Bytes bitfield_of(const std::vector<int>& ids) {
    Bytes b;
    for (int id : ids) {
        if (b.size() <= static_cast<std::size_t>(id / 8)) b.resize(static_cast<std::size_t>(id / 8) + 1, 0);
        b[static_cast<std::size_t>(id / 8)] = static_cast<std::uint8_t>(b[static_cast<std::size_t>(id / 8)] | (1u << (id % 8)));
    }
    return b;
}

static std::vector<int> jnext_ids() {
    return {1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 15, 16, 17, 18, 19, 20, 21, 23, 24, 25, 26, 27, 28,
            39, 40, 41, 42, 43, 50, 51};
}

static std::vector<int> jnext_ids_without(std::initializer_list<int> drop) {
    std::vector<int> v = jnext_ids();
    for (int d : drop) v.erase(std::remove(v.begin(), v.end(), d), v.end());
    return v;
}

struct Script {
    FakeListener                       lsn;
    std::unique_ptr<esp::EspTransport> t;
    FrameParser                        fp;
    std::vector<int>                   ids;      // every command received, in order
    std::vector<Command>               cmds;
    std::string                        name = "jnext v1.4.1";
    std::uint8_t                       ver[3] = {2, 2, 0};
    Bytes                              bitfield = bitfield_of(jnext_ids());
    unsigned                           next_bp_id = 1;
    int                                bp_id_override = -1;   // >= 0: every CMD 40 answers this id
    std::uint8_t                       regs_len_delta = 0;    // a GET_REGISTERS reply this many bytes short
    std::uint8_t                       regs_nslots = 8;       // the Nslots byte of a GET_REGISTERS reply
    std::uint8_t                       read_mem_extra = 0;    // a READ_MEM reply this many bytes long
    std::function<bool(Script&, const Command&)> custom;

    Script() { lsn.open(0); }
    void send(const Bytes& f) {
        if (t) t->send(f.data(), f.size());
    }
    void reply(std::uint8_t seq, const Bytes& payload = {}) { send(encode_response(seq, payload)); }
    void notify(const Bytes& payload) { send(encode_response(0, payload)); }
    void ntf_pause(std::uint8_t reason, std::uint16_t addr, std::uint8_t bank1, const std::string& text = "") {
        Bytes p = {NTF_PAUSE, reason, static_cast<std::uint8_t>(addr), static_cast<std::uint8_t>(addr >> 8), bank1};
        p.insert(p.end(), text.begin(), text.end());
        p.push_back(0);
        notify(p);
    }
    void close() {
        if (t) t->close();
    }
    int count(int id) const { return static_cast<int>(std::count(ids.begin(), ids.end(), id)); }

    Bytes init_reply() const {
        Bytes p = {0, ver[0], ver[1], ver[2], 4};
        p.insert(p.end(), name.begin(), name.end());
        p.push_back(0);
        return p;
    }
    Bytes realistic(const Command& c) {
        if (c.id == CMD_GET_REGISTERS) {
            Bytes p;
            const std::uint16_t words[12] = {0x8000, 0xFF00, 0x0044, 0, 0, 0, 0, 0x5C3A, 0, 0, 0, 0};
            for (std::uint16_t w : words) {
                p.push_back(static_cast<std::uint8_t>(w));
                p.push_back(static_cast<std::uint8_t>(w >> 8));
            }
            for (std::uint8_t b : {std::uint8_t{0x12}, std::uint8_t{0x3F}, std::uint8_t{1}, std::uint8_t{0}, std::uint8_t{8}}) p.push_back(b);
            for (int i = 0; i < 8; ++i) p.push_back(static_cast<std::uint8_t>(i < 2 ? 0xFF : 0x10 + i));
            p[28] = regs_nslots;
            p.resize(p.size() - regs_len_delta);
            return p;
        }
        if (c.id == CMD_READ_MEM && c.payload.size() >= 5) {
            const std::size_t n = static_cast<std::size_t>(c.payload[3] | (c.payload[4] << 8));
            return pattern(n + read_mem_extra, c.payload[1]);
        }
        if (c.id == CMD_READ_BANK_MEM && c.payload.size() >= 5) {
            const std::size_t n = static_cast<std::size_t>(c.payload[3] | (c.payload[4] << 8));
            return pattern(n, c.payload[0]);
        }
        if (c.id == CMD_READ_MEM_BLOCKS && c.payload.size() >= 4) {
            std::size_t total = 0;
            for (std::size_t at = 4; at + 3 < c.payload.size(); at += 4)
                total += static_cast<std::size_t>(c.payload[at + 2] | (c.payload[at + 3] << 8));
            return pattern(total, 1);
        }
        if (c.id == CMD_ADD_BREAKPOINT) {
            const unsigned id = bp_id_override >= 0 ? static_cast<unsigned>(bp_id_override) : next_bp_id++;
            return {static_cast<std::uint8_t>(id), static_cast<std::uint8_t>(id >> 8)};
        }
        if (c.id == CMD_ADD_WATCHPOINT) return {0};
        if (c.id == CMD_GET_TBBLUE_REG) return {0x0A};
        return {};
    }
    void handle(const Command& c) {
        ids.push_back(c.id);
        cmds.push_back(c);
        if (custom && custom(*this, c)) return;
        if (c.id == CMD_INIT) reply(c.seq, init_reply());
        else if (c.id == CMD_GET_SUPPORTED_COMMANDS) reply(c.seq, bitfield);
        else reply(c.seq, realistic(c));
    }
    void service() {
        for (int round = 0; round < 64; ++round) {
            if (!t || t->state() != esp::TransportState::Connected) {
                auto n = lsn.accept();
                if (n) {
                    t = std::move(n);
                    fp.reset();
                } else if (!t) {
                    return;
                }
            }
            std::uint8_t      buf[4096];
            const std::size_t n = t->recv(buf, sizeof(buf));
            if (n == 0) return;
            std::size_t off = 0;
            while (off < n) {
                const std::size_t k = fp.feed(buf + off, n - off, g_now);
                off += k;
                if (fp.ready()) handle(fp.take());
                else if (k == 0 || fp.failed()) break;
            }
        }
    }
};

/// A facade over a target over a scripted peer.
struct Scripted {
    Script                      s;
    CountingLink*               link = nullptr;
    std::unique_ptr<DzrpTarget> tgt;
    std::unique_ptr<Debugger>   d;
    StopListener                sl;
    ClientId                    a = 0;
    bool                        up = false;

    explicit Scripted(const std::function<void(Script&)>& tweak = nullptr, bool make_target = true) {
        if (tweak) tweak(s);
        auto cl_link = std::make_unique<CountingLink>(std::make_unique<MemoryLink>(s.lsn, LinkKind::Tcp));
        link         = cl_link.get();
        auto cl = std::make_unique<DzrpClient>(std::move(cl_link), cfg());
        cl->set_wait_hook([this] { s.service(); });
        cl->start();
        for (int i = 0; i < 200; ++i) {
            cl->step();
            s.service();
            if (cl->state() == SessionState::Ready || cl->state() == SessionState::Failed) break;
        }
        up = cl->state() == SessionState::Ready;
        if (!make_target) return;
        tgt = std::make_unique<DzrpTarget>(std::move(cl));
        d   = std::make_unique<Debugger>(*tgt);
        d->add_service(*tgt);
        jnext::dbg::ClientInfo ci;
        ci.name = "script";
        ci.kind = jnext::dbg::ClientKind::Test;
        a       = d->attach(ci).value;
        d->set_listener(a, &sl);
        d->pump(PumpBudget{});   // primes
        link->mark();
    }
    ~Scripted() {
        d.reset();
        tgt.reset();
    }
    void pump() {
        s.service();
        d->pump(PumpBudget{});
    }
};

// ==============================================================================================
// RTG-INS - inspection (R2.1)
// ==============================================================================================

static void ins_rows_a() {
    {
        Pair p;
        p.remote->registers();   // primes the burst
        const bool at_connect = regs_equal(p.remote->registers(), p.ref.dbg->registers());
        const auto v          = p.remote->register_validity();
        const bool validity   = v.valid(RegisterValidity::Field::Core) && v.valid(RegisterValidity::Field::IM) &&
                              !v.valid(RegisterValidity::Field::IFF1) && !v.valid(RegisterValidity::Field::IFF2) &&
                              !v.valid(RegisterValidity::Field::Halted) && !v.valid(RegisterValidity::Field::Memptr) &&
                              !v.valid(RegisterValidity::Field::Q) && !v.valid(RegisterValidity::Field::IncDecZ);
        p.remote->subscribe(p.ra, exec_sub(0x8003));
        p.ref.dbg->subscribe(p.rc, exec_sub(0x8003));
        const bool stopped = p.remote_run_to_stop() && p.ref_run_to_stop();
        check("RTG-INS-01", "registers: Core fields and IM equal the reference at connect and at a breakpoint stop; "
                            "validity is Core and IM only",
              at_connect && validity && stopped && regs_equal(p.remote->registers(), p.ref.dbg->registers()) &&
                  p.remote->registers().PC == 0x8003);
    }
    {
        Pair p;
        bool all = true;
        const struct { RegId id; uint16_t v; } sets[] = {
            {RegId::BC, 0xBEEF}, {RegId::A, 0x5A}, {RegId::HL2, 0x1357}, {RegId::I, 0x3F}, {RegId::R, 0x42},
            {RegId::IM, 2}, {RegId::SP, 0xFE00}, {RegId::IXL, 0x77}};
        for (const auto& s : sets) {
            all = all && p.remote->set_register(p.ra, s.id, s.v) == Result::Ok &&
                  p.ref.dbg->set_register(p.rc, s.id, s.v) == Result::Ok &&
                  regs_equal(p.remote->registers(), p.ref.dbg->registers());
        }
        p.link->mark();
        const Result i1 = p.remote->set_register(p.ra, RegId::IFF1, 1);
        const Result i2 = p.remote->set_register(p.ra, RegId::IFF2, 1);
        check("RTG-INS-02", "set_register for a pair, an 8-bit half, a shadow, I, R, IM, SP and IXL leaves both machines "
                            "equal; IFF1 and IFF2 are Unsupported with no frame (CMD_INTERRUPT_ON_OFF sets both)",
              all && i1 == Result::Unsupported && i2 == Result::Unsupported && p.link->since() == 0);
    }
    {
        Pair p;
        std::vector<uint8_t> a(65536), b(65536);
        const auto ra = p.remote->peek(MemSpace::cpu(), 0, 65536, a.data());
        const auto rb = p.ref.dbg->peek(MemSpace::cpu(), 0, 65536, b.data());
        const bool full = ra.status == Result::Ok && ra.value == 65536 && a == b;
        uint8_t w1[32], w2[32];
        const auto wa = p.remote->peek(MemSpace::cpu(), 0xFFF0, 32, w1);
        const auto wb = p.ref.dbg->peek(MemSpace::cpu(), 0xFFF0, 32, w2);
        const bool wrap = wa.status == Result::Ok && wa.value == 32 && std::memcmp(w1, w2, 32) == 0 &&
                          std::memcmp(w1 + 16, a.data(), 16) == 0;
        std::vector<uint8_t> big1(0x10010), big2(0x10010);
        const auto ba = p.remote->peek(MemSpace::cpu(), 0, 0x10010, big1.data());
        const auto bb = p.ref.dbg->peek(MemSpace::cpu(), 0, 0x10010, big2.data());
        check("RTG-INS-03", "Cpu reads equal the reference: the whole 64 KB, 32 bytes wrapping at 0xFFFF, and 0x10010 "
                            "bytes (a second wrap)",
              rb.status == Result::Ok && full && wb.status == Result::Ok && wrap && ba.status == bb.status &&
                  ba.value == bb.value && big1 == big2);
    }
    {
        Pair p;
        uint8_t data[32];
        for (int i = 0; i < 32; ++i) data[i] = static_cast<uint8_t>(0xA0 + i);
        const auto wa = p.remote->poke(p.ra, MemSpace::cpu(), 0x3FF0, 32, data);
        const auto wb = p.ref.dbg->poke(p.rc, MemSpace::cpu(), 0x3FF0, 32, data);
        p.link->mark();
        uint8_t r1[48], r2[48];
        const auto pa = p.remote->peek(MemSpace::cpu(), 0x3FE8, 48, r1);
        const auto pb = p.ref.dbg->peek(MemSpace::cpu(), 0x3FE8, 48, r2);
        check("RTG-INS-04", "a Cpu write of 32 bytes across ROM and RAM lands 16 and says RefusedReadOnly, as the "
                            "reference; the read-back equals the reference and costs no frame (the write pipelined its "
                            "re-read)",
              wa.status == Result::RefusedReadOnly && wa.value == 16 && wb.status == wa.status && wb.value == wa.value &&
                  pa.status == Result::Ok && pb.status == Result::Ok && std::memcmp(r1, r2, 48) == 0 &&
                  r1[0x18] == 0xB0 && p.link->since() == 0);
    }
    {
        Pair p;
        bool same = true;
        for (uint16_t pg : {0, 10, 223}) {
            std::vector<uint8_t> a(8192), b(8192);
            const auto xa = p.remote->peek(MemSpace::page(pg), 0, 8192, a.data());
            const auto xb = p.ref.dbg->peek(MemSpace::page(pg), 0, 8192, b.data());
            same = same && xa.status == Result::Ok && xb.status == Result::Ok && a == b;
        }
        uint8_t r1[32], r2[32];
        const auto sa = p.remote->peek(MemSpace::page(10), 0x1FF0, 32, r1);
        const auto sb = p.ref.dbg->peek(MemSpace::page(10), 0x1FF0, 32, r2);
        uint8_t data[32];
        std::memset(data, 0x77, sizeof(data));
        const auto wa = p.remote->poke(p.ra, MemSpace::page(10), 0x1FF0, 32, data);
        const auto wb = p.ref.dbg->poke(p.rc, MemSpace::page(10), 0x1FF0, 32, data);
        p.link->mark();
        uint8_t z[8];
        const Result i1 = p.remote->peek(MemSpace::page(0xE0), 0, 8, z).status;
        const Result i2 = p.remote->peek(MemSpace::page(0xFE), 0, 8, z).status;
        const Result i3 = p.remote->peek(MemSpace::page(0x100), 0, 8, z).status;
        check("RTG-INS-05", "Page reads equal the reference (pages 0, 10, 223); a read and a write across the page end are "
                            "short by the same 16 bytes with RefusedUnavailable; pages 0xE0, 0xFE and 0x100 are "
                            "InvalidPage with no frame",
              same && sa.status == Result::RefusedUnavailable && sa.value == 16 && sb.status == sa.status &&
                  sb.value == sa.value && std::memcmp(r1, r2, 16) == 0 && wa.status == Result::RefusedUnavailable &&
                  wa.value == 16 && wb.status == wa.status && wb.value == wa.value && i1 == Result::InvalidPage &&
                  i2 == Result::InvalidPage && i3 == Result::InvalidPage && p.link->since() == 0);
    }
    {
        Pair p;
        p.remote->registers();
        p.link->mark();
        uint8_t z[8];
        const Result r1 = p.remote->peek(MemSpace::rom(0), 0, 8, z).status;
        const Result r2 = p.remote->peek(MemSpace::divmmc_ram(0), 0, 8, z).status;
        check("RTG-INS-06", "Rom{0} and DivMmcRam{0} are Unsupported with no frame: no DZRP command names a ROM image by "
                            "index or a DivMMC bank",
              r1 == Result::Unsupported && r2 == Result::Unsupported && p.link->since() == 0);
    }
    {
        Pair p;
        const auto rs = p.remote->mmu_slots();
        const auto fs = p.ref.dbg->mmu_slots();
        bool ram = true, rom = true;
        for (std::size_t i = 0; i < 8; ++i) {
            if (fs[i].is_rom) {
                std::vector<uint8_t> a(0x2000), b(0x2000);
                const auto xa = p.remote->peek(rs[i].space, rs[i].space_offset, 0x2000, a.data());
                const auto xb = p.ref.dbg->peek(fs[i].space, fs[i].space_offset, 0x2000, b.data());
                rom = rom && rs[i].is_rom && rs[i].nr_page == 0xFF && rs[i].space == MemSpace::cpu() &&
                      xa.status == Result::Ok && xb.status == Result::Ok && a == b;
            } else {
                ram = ram && !rs[i].is_rom && rs[i].nr_page == fs[i].nr_page &&
                      rs[i].effective_page == fs[i].effective_page && rs[i].space == fs[i].space &&
                      rs[i].space_offset == fs[i].space_offset;
            }
        }
        check("RTG-INS-07", "mmu_slots(): RAM slots are field-equal to the reference; ROM slots say is_rom and 0xFF, and "
                            "the 8 KB read through each one's (space, offset) equals the reference's through its own",
              ram && rom && rs[0].is_rom && rs[1].is_rom && !rs[2].is_rom);
    }
    {
        Pair p;
        uint8_t before[64];
        p.remote->peek(MemSpace::cpu(), 0xC000, 64, before);   // fills the cache with the old mapping
        uint8_t pat[64];
        for (int i = 0; i < 64; ++i) pat[i] = static_cast<uint8_t>(0xC0 + i);
        p.remote->poke(p.ra, MemSpace::page(20), 0, 64, pat);
        p.ref.dbg->poke(p.rc, MemSpace::page(20), 0, 64, pat);
        const Result a = p.remote->set_mmu_slot(p.ra, 6, 20);
        const Result b = p.ref.dbg->set_mmu_slot(p.rc, 6, 20);
        const auto rs = p.remote->mmu_slots();
        const auto fs = p.ref.dbg->mmu_slots();
        uint8_t r1[64], r2[64];
        p.remote->peek(MemSpace::cpu(), 0xC000, 64, r1);
        p.ref.dbg->peek(MemSpace::cpu(), 0xC000, 64, r2);
        check("RTG-INS-08", "set_mmu_slot(6, 20) on both: the slots are equal and the read after it shows the new page, "
                            "not the cache's old bytes",
              a == Result::Ok && b == Result::Ok && rs[6].nr_page == 20 && fs[6].nr_page == 20 &&
                  std::memcmp(r1, r2, 64) == 0 && std::memcmp(r1, pat, 64) == 0 && std::memcmp(before, pat, 64) != 0);
    }
}

static void ins_rows_b() {
    {
        Pair p;
        p.remote->registers();   // the burst is spent
        bool all = true;
        for (int r = 0; r < 256; ++r)
            all = all && p.remote->nextreg_peek(static_cast<uint8_t>(r)) == p.ref.dbg->nextreg_peek(static_cast<uint8_t>(r));
        // A cold line: drop the cache by writing, then read one register of it.
        p.remote->nextreg_write(p.ra, 0x15, 0x03);
        p.ref.dbg->nextreg_write(p.rc, 0x15, 0x03);
        p.link->mark();
        const uint8_t v1 = p.remote->nextreg_peek(0x15);
        const std::size_t first = p.link->since();
        const long before_reply = p.link->first_reply_at;
        const uint8_t v2 = p.remote->nextreg_peek(0x14);   // same line of 16
        check("RTG-INS-09", "all 256 NextREGs equal the reference; the first read of a register fetches its line of 16 "
                            "(16 frames, the window's 15 written before the first reply); the neighbours cost nothing; a "
                            "write is seen on both",
              all && v1 == p.ref.dbg->nextreg_peek(0x15) && v1 == 0x03 && first == 16 && before_reply == 15 &&
                  p.link->since() == 16 && v2 == p.ref.dbg->nextreg_peek(0x14));
    }
    {
        Pair p;
        const auto ia = p.remote->port_in(p.ra, 0x7FFE);
        const auto ib = p.ref.dbg->port_in(p.rc, 0x7FFE);
        const Result oa = p.remote->port_out(p.ra, 0x7FFD, 0x03);
        const Result ob = p.ref.dbg->port_out(p.rc, 0x7FFD, 0x03);
        const auto rs = p.remote->mmu_slots();
        const auto fs = p.ref.dbg->mmu_slots();
        bool slots = true;
        for (std::size_t i = 0; i < 8; ++i)
            if (!fs[i].is_rom) slots = slots && rs[i].nr_page == fs[i].nr_page;
        uint8_t r1[64], r2[64];
        p.remote->peek(MemSpace::cpu(), 0xC000, 64, r1);
        p.ref.dbg->peek(MemSpace::cpu(), 0xC000, 64, r2);
        check("RTG-INS-10", "port_in and port_out(0x7FFD) on both: the value read, the slots and the paged-in bytes are equal",
              ia.status == Result::Ok && ib.status == Result::Ok && ia.value == ib.value && oa == Result::Ok &&
                  ob == Result::Ok && slots && std::memcmp(r1, r2, 64) == 0);
    }
    {
        Pair p;
        for (Machine* m : {&p.ref, &p.srv}) {
            auto& sp = m->emu.sprites();
            const uint8_t attrs[3][5] = {{0x10, 0x20, 0x35, 0xC5, 0x00},        // visible, extended, pattern 5
                                         {0x40, 0x50, 0xA2, 0xC7, 0xC8 | 0x01},   // 4-bit, N6 set, scaled
                                         {0xFF, 0x7F, 0x01, 0x80, 0x00}};
            for (int s = 0; s < 3; ++s)
                for (int b = 0; b < 5; ++b) sp.write_attr_byte_at(static_cast<uint8_t>(s * 40), static_cast<uint8_t>(b), attrs[s][b]);
            for (int i = 0; i < 600; ++i) sp.write_pattern_byte_at(static_cast<uint16_t>(i * 13), static_cast<uint8_t>(i * 7 + 3));
            m->emu.nextreg().write(0x43, 0x20);   // sprite palette 0 for the next palette writes
            m->emu.nextreg().write(0x40, 3);
            m->emu.nextreg().write(0x41, 0xAB);
        }
        bool eq = true;
        const auto sa = p.remote->sprites();
        const auto sb = p.ref.dbg->sprites();
        eq = sa.size() == 128 && sb.size() == 128;
        for (std::size_t i = 0; eq && i < 128; ++i)
            eq = sa[i].x == sb[i].x && sa[i].y == sb[i].y && sa[i].pattern == sb[i].pattern &&
                 sa[i].palette_offset == sb[i].palette_offset && sa[i].visible == sb[i].visible &&
                 sa[i].x_mirror == sb[i].x_mirror && sa[i].y_mirror == sb[i].y_mirror && sa[i].rotate == sb[i].rotate &&
                 sa[i].is_4bit == sb[i].is_4bit && sa[i].x_scale == sb[i].x_scale && sa[i].y_scale == sb[i].y_scale;
        bool raw = true;
        for (uint8_t i : {0, 40, 80, 127}) raw = raw && p.remote->sprite_attr_raw(i).value == p.ref.dbg->sprite_attr_raw(i).value;
        const auto pa = p.remote->pattern_ram();
        const auto pb = p.ref.dbg->pattern_ram();
        const bool pat = pa.size == pb.size && pa.size == 16384 && std::memcmp(pa.data, pb.data, pa.size) == 0;
        bool pal = true;
        for (int bank = 0; bank < 2; ++bank)
            for (int i = 0; i < 256; ++i)
                pal = pal && p.remote->sprite_palette_rgb333(bank, static_cast<uint8_t>(i)).value ==
                                 p.ref.dbg->sprite_palette_rgb333(bank, static_cast<uint8_t>(i)).value;
        const auto ca = p.remote->sprite_clip();
        const auto cb = p.ref.dbg->sprite_clip();
        p.link->mark();
        const uint8_t one[5] = {1, 2, 3, 4, 5};
        const Result w1 = p.remote->set_sprite_attr_raw(p.ra, 0, one, 5);
        const Result w2 = p.remote->write_pattern_ram(p.ra, 0, one, 5);
        check("RTG-INS-11", "sprites after identical writes on both machines (a 4-bit sprite with N6 and a scale): the 128 "
                            "decoded entries, the raw attributes, the 16 KB pattern RAM, both palettes and the clip "
                            "window equal the reference; writing sprites is Unsupported with no frame",
              eq && raw && pat && pal && ca.x1 == cb.x1 && ca.x2 == cb.x2 && ca.y1 == cb.y1 && ca.y2 == cb.y2 &&
                  sa[40].is_4bit && w1 == Result::Unsupported && w2 == Result::Unsupported && p.link->since() == 0);
    }
    {
        Pair p;
        p.remote->registers();   // the connect-time burst: the SP is now known
        p.remote->subscribe(p.ra, exec_sub(0x8003));
        const bool stopped = p.remote_run_to_stop();
        p.link->mark();
        const Z80Registers r = p.remote->registers();
        const std::size_t burst = p.link->since();
        const long before_reply = p.link->first_reply_at;
        p.link->mark();
        uint8_t b[16], sp[16];
        p.remote->peek(MemSpace::cpu(), 0x8000, 16, b);
        p.remote->peek(MemSpace::cpu(), r.SP, 16, sp);
        check("RTG-INS-12", "the first registers() after a breakpoint stop is one round trip of two frames (registers and "
                            "the memory around PC and SP); a read of PC's line and of SP's line then costs nothing",
              stopped && r.PC == 0x8003 && burst == 2 && before_reply == 2 && p.link->since() == 0);
    }
    {
        Pair p;
        uint8_t a[4], b[4];
        p.remote->peek(MemSpace::cpu(), 0x9000, 4, a);
        p.link->mark();
        p.remote->peek(MemSpace::cpu(), 0x9000, 4, b);
        const bool cached = p.link->since() == 0;
        // after run() and a stop the line is fetched again
        p.remote->subscribe(p.ra, exec_sub(0x8002));
        p.remote_run_to_stop();
        p.remote->peek(MemSpace::cpu(), 0x9000, 4, a);   // burst + fetch
        p.link->mark();
        p.remote->peek(MemSpace::cpu(), 0x9000, 4, b);
        const bool cached2 = p.link->since() == 0;
        // while running: one fetch per tick
        const auto subs = p.remote->subscriptions(true);
        if (!subs.empty()) p.remote->unsubscribe(p.ra, subs[0].id);
        p.remote->run(p.ra);
        // A frame runs on the served machine and the remote is not pumped: the line cached at the stop describes a
        // machine that has since moved, so the read must fetch (the cache goes at the resume, not at the next pump).
        p.srv.emu.run_frame();
        p.srv.dbg->pump(PumpBudget{});
        uint8_t fresh[1];
        p.remote->peek(MemSpace::cpu(), 0x9000, 1, fresh);
        const bool dropped_at_resume = fresh[0] != b[0];
        p.tick();
        uint8_t v1[1], v2[1], v3[1];
        p.link->mark();
        p.remote->peek(MemSpace::cpu(), 0x9000, 1, v1);
        p.remote->peek(MemSpace::cpu(), 0x9000, 1, v2);
        const std::size_t one_tick = p.link->since();
        p.tick();
        p.link->mark();
        p.remote->peek(MemSpace::cpu(), 0x9000, 1, v3);
        const std::size_t next_tick = p.link->since();
        check("RTG-INS-13", "cache lifetime: a re-read while paused costs nothing; after run() and a stop it is fetched again; "
                            "while running two reads in one tick cost one fetch and the next tick fetches again and sees "
                            "what the running loop wrote",
              cached && cached2 && dropped_at_resume && one_tick == 1 && v1[0] == v2[0] && next_tick == 1 &&
                  v3[0] != v1[0]);
    }
    {
        Pair p;
        const auto ti = p.remote->target_info();
        check("RTG-INS-14", "target_info(): a remote, named 'jnext v<version>', DZRP 2.2.0, over the memory link",
              ti.kind == jnext::dbg::TargetKind::Remote && ti.name == std::string("jnext v") + JNEXT_VERSION_STRING &&
                  ti.version == "2.2.0" && ti.link.rfind("mem:", 0) == 0);
    }
    {
        Pair p;
        jnext::dbg::CapabilitySet want;
        for (auto c : {Capability::Run, Capability::Pause, Capability::ExecuteBreakpoints, Capability::Watchpoints,
                       Capability::CommandsWhileRunning, Capability::RegisterWrite, Capability::Memory,
                       Capability::MemoryWhileRunning, Capability::PhysicalMemory, Capability::MmuSlots,
                       Capability::NextRegs, Capability::Ports, Capability::SpriteData, Capability::SpritePaletteClip})
            want.set(c);
        check("RTG-INS-15", "capabilities() are exactly the fourteen; InstructionStep and RunToCursor are absent with a "
                            "reason; right after connect Core is valid and Run is available (a jnext peer is never 'no "
                            "debuggee')",
              p.remote->capabilities() == want && !p.remote->capabilities().has(Capability::InstructionStep) &&
                  !p.remote->why_unavailable(Capability::InstructionStep).empty() &&
                  !p.remote->why_unavailable(Capability::RunToCursor).empty() &&
                  p.remote->register_validity().valid(RegisterValidity::Field::Core) &&
                  p.remote->why_unavailable(Capability::Run).empty());
    }
    {
        Scripted s([](Script& sc) { sc.bitfield = bitfield_of(jnext_ids_without({42, 43, 28, 27})); });
        const bool no_wp = !s.d->capabilities().has(Capability::Watchpoints) &&
                           s.d->why_unavailable(Capability::Watchpoints) == "the remote does not serve CMD_ADD_WATCHPOINT" &&
                           s.d->capabilities().has(Capability::NextRegs);
        const Result sub = s.d->subscribe(s.a, mem_sub(0x4000, 0x40FF, Access::Write)).status;
        uint8_t mem[16];
        s.d->peek(MemSpace::cpu(), 0x8000, 16, mem);   // a miss, without CMD_READ_MEM_BLOCKS
        const Result nrw = s.d->nextreg_write(s.a, 0x15, 1);
        check("RTG-INS-16", "a remote without CMD 42/43, 28 and 27: Watchpoints absent with the command named and never sent; "
                            "a memory miss reads with CMD_READ_MEM; NextRegs present but writing is Unsupported",
              s.up && no_wp && sub == Result::Unsupported && s.s.count(42) == 0 && s.s.count(28) == 0 &&
                  s.s.count(CMD_READ_MEM) >= 1 && s.s.count(27) == 0 && nrw == Result::Unsupported);
    }
    {
        Scripted s;
        s.d->registers();
        s.s.close();
        LogTap log;
        // The target has not yet noticed the loss (no pump): the write finds it out itself, in the middle of the verb.
        uint8_t data[4] = {1, 2, 3, 4};
        const auto pk = s.d->poke(s.a, MemSpace::page(3), 0, 4, data);
        uint8_t buf[4];
        const bool verbs = s.d->run(s.a) == Result::Unsupported && s.d->pause(s.a) == Result::Unsupported &&
                           s.d->peek(MemSpace::cpu(), 0, 4, buf).status == Result::Unsupported &&
                           s.d->set_register(s.a, RegId::BC, 1) == Result::Unsupported &&
                           s.d->subscribe(s.a, exec_sub(0x8000)).status == Result::Unsupported &&
                           s.d->registers().PC == 0;
        s.d->pump(PumpBudget{});
        // The write's OWN round trip failing: the page's line is already cached, so the facade's old-byte read costs
        // nothing and the loss is met by the write itself.
        Scripted t;
        uint8_t  seen[4];
        t.d->peek(MemSpace::page(3), 0, 4, seen);
        t.s.close();
        const auto pk2 = t.d->poke(t.a, MemSpace::page(3), 0, 4, data);
        check("RTG-INS-17", "link loss: poke(Page) is Unsupported and logs no MUTATE, whether the loss is met by the "
                            "facade's old-byte read or by the write's own round trip; every verb is Unsupported; "
                            "capabilities are empty with 'link lost: ...'; the validity is empty; peer_connected() is "
                            "false; no Paused",
              pk2.status == Result::Unsupported && t.sl.mutates() == 0 && !t.tgt->connected() &&
                  pk.status == Result::Unsupported && s.sl.mutates() == 0 && verbs && s.d->capabilities().bits() == 0 &&
                  s.d->why_unavailable(Capability::Run).rfind("link lost: ", 0) == 0 &&
                  s.d->register_validity().bits == 0 && !s.tgt->peer_connected() && !s.tgt->connected() &&
                  s.sl.paused.empty());
    }
    {
        Pair p;
        auto idle = [&] {
            const std::size_t before = p.link->frames.size();
            (void)p.tgt->info();
            (void)p.tgt->capabilities();
            (void)p.tgt->why_unavailable(Capability::Run);
            (void)p.tgt->register_validity();
            (void)p.tgt->run_state();
            jnext::dbg::TargetStop ts;
            (void)p.tgt->take_stop(ts);
            (void)p.remote->capabilities();
            (void)p.remote->state();
            return p.link->frames.size() == before;
        };
        const bool paused = idle();
        p.remote->run(p.ra);
        const bool running = idle();
        p.server.reset();   // the server's end goes away
        p.tgt->service_once(0);
        const bool lost = !p.tgt->connected() && idle();
        check("RTG-INS-18", "info, capabilities, why_unavailable, register_validity, run_state and take_stop write no frame: "
                            "paused, running and lost",
              paused && running && lost);
    }
    {
        Scripted s([](Script& sc) {
            sc.name   = "dezogif v2.1.0";
            sc.ver[1] = 1;
            sc.bitfield.clear();
        });
        const std::size_t before = s.s.ids.size();
        s.d->registers();
        s.d->nextreg_peek(5);
        s.d->state();
        const Result r = s.d->run(s.a);
        s.d->pump(PumpBudget{});
        check("RTG-INS-19", "a dezogif_ng profile: an empty capability set with its reason and no frame sent after the "
                            "handshake, whatever is asked",
              s.up && s.tgt->client().profile() == Profile::DezogifNg && s.d->capabilities().bits() == 0 &&
                  s.d->why_unavailable(Capability::Run) == "this remote's profile is not served yet" &&
                  r == Result::Unsupported && s.s.ids.size() == before);
    }
    {
        // a hostile peer: a GET_REGISTERS reply one byte short ends the session
        Scripted s([](Script& sc) { sc.regs_len_delta = 1; }, true);
        const Z80Registers z = s.d->registers();
        // and the verb stops talking to the peer it has just judged hostile
        Scripted u([](Script& sc) { sc.regs_len_delta = 1; }, true);
        uint8_t  mem[8];
        const auto pk = u.d->peek(MemSpace::cpu(), 0x8000, 8, mem);
        // and a single-request getter (the sprite clip window) that meets it in the same verb
        Scripted v([](Script& sc) { sc.regs_len_delta = 1; }, true);
        v.d->sprite_clip();
        check("RTG-INS-20", "a CMD_GET_REGISTERS reply of the wrong length is a protocol error: the target is lost, registers "
                            "are zero, every verb Unsupported and exactly one CMD_GET_REGISTERS was sent; a Cpu read that "
                            "meets the same reply sends no memory read after it, and neither does a sprite getter",
              z.PC == 0 && !s.tgt->connected() && s.d->capabilities().bits() == 0 &&
                  s.tgt->lost_reason().find("CMD_GET_REGISTERS") != std::string::npos &&
                  s.s.count(CMD_GET_REGISTERS) == 1 && pk.status == Result::Unsupported && !u.tgt->connected() &&
                  u.s.count(CMD_GET_REGISTERS) == 1 && u.s.count(CMD_READ_MEM) == 0 &&
                  u.s.count(CMD_READ_MEM_BLOCKS) == 0 && v.s.count(CMD_GET_REGISTERS) == 1 &&
                  v.s.count(CMD_GET_SPRITES_CLIP_WINDOW_AND_CONTROL) == 0);
    }
    {
        Scripted s([](Script& sc) {
            sc.read_mem_extra = 3;
            sc.bitfield       = bitfield_of(jnext_ids_without({28}));
        });
        uint8_t buf[16];
        const auto r = s.d->peek(MemSpace::cpu(), 0x8000, 16, buf);
        check("RTG-INS-21", "a read reply longer than asked is a protocol error: the target is lost and nothing is returned",
              r.status == Result::Unsupported && !s.tgt->connected());
    }
    {
        // 37 bytes, but a slot count that is not 8
        Scripted s([](Script& sc) { sc.regs_nslots = 7; });
        const Z80Registers z = s.d->registers();
        check("RTG-INS-22", "a CMD_GET_REGISTERS reply of 37 bytes whose slot count is not 8 is a protocol error: the "
                            "target is lost and registers are zero",
              z.PC == 0 && !s.tgt->connected() && s.tgt->lost_reason().find("CMD_GET_REGISTERS") != std::string::npos);
    }
    {
        // Two slots map one page: a write through one is seen through the other, as on the reference.
        Pair p;
        p.remote->set_mmu_slot(p.ra, 6, 20);
        p.remote->set_mmu_slot(p.ra, 7, 20);
        p.ref.dbg->set_mmu_slot(p.rc, 6, 20);
        p.ref.dbg->set_mmu_slot(p.rc, 7, 20);
        uint8_t old[4], a[4], b[4];
        p.remote->peek(MemSpace::cpu(), 0xE000, 4, old);   // caches the line through slot 7
        const uint8_t data[4] = {0x11, 0x22, 0x33, 0x44};
        p.remote->poke(p.ra, MemSpace::cpu(), 0xC000, 4, data);
        p.ref.dbg->poke(p.rc, MemSpace::cpu(), 0xC000, 4, data);
        p.remote->peek(MemSpace::cpu(), 0xE000, 4, a);
        p.ref.dbg->peek(MemSpace::cpu(), 0xE000, 4, b);
        check("RTG-INS-23", "two slots mapping one page: a Cpu write through one is seen through the other (the cached "
                            "line of the other slot is not left stale), as on the reference",
              std::memcmp(a, b, 4) == 0 && std::memcmp(a, data, 4) == 0 && std::memcmp(old, data, 4) != 0);
    }
    {
        // The other views: a Page write is seen through the Cpu view of a slot that maps it, and a Cpu write through the Page view.
        Pair p;
        p.remote->set_mmu_slot(p.ra, 6, 20);
        p.ref.dbg->set_mmu_slot(p.rc, 6, 20);
        uint8_t c0[4], c1[4], c2[4], g0[4], g1[4], g2[4];
        p.remote->peek(MemSpace::cpu(), 0xC000, 4, c0);            // cached through the slot
        const uint8_t via_page[4] = {0xA1, 0xA2, 0xA3, 0xA4};
        p.remote->poke(p.ra, MemSpace::page(20), 0, 4, via_page);
        p.ref.dbg->poke(p.rc, MemSpace::page(20), 0, 4, via_page);
        p.remote->peek(MemSpace::cpu(), 0xC000, 4, c1);
        p.ref.dbg->peek(MemSpace::cpu(), 0xC000, 4, c2);
        const bool page_to_cpu = std::memcmp(c1, c2, 4) == 0 && std::memcmp(c1, via_page, 4) == 0 && std::memcmp(c0, via_page, 4) != 0;
        p.remote->peek(MemSpace::page(20), 0, 4, g0);               // cached as a page line
        const uint8_t via_cpu[4] = {0xB1, 0xB2, 0xB3, 0xB4};
        p.remote->poke(p.ra, MemSpace::cpu(), 0xC000, 4, via_cpu);
        p.ref.dbg->poke(p.rc, MemSpace::cpu(), 0xC000, 4, via_cpu);
        p.remote->peek(MemSpace::page(20), 0, 4, g1);
        p.ref.dbg->peek(MemSpace::page(20), 0, 4, g2);
        check("RTG-INS-24", "a Page write is seen through the Cpu view of a slot that maps the page, and a Cpu write "
                            "through the Page view, both as on the reference",
              page_to_cpu && std::memcmp(g1, g2, 4) == 0 && std::memcmp(g1, via_cpu, 4) == 0 && std::memcmp(g0, via_cpu, 4) != 0);
    }
}

// ==============================================================================================
// RTG-RUN - run control and execute breakpoints (R2.2)
// ==============================================================================================

static void run_rows_a() {
    {
        Pair p;
        p.remote->run(p.ra);
        for (int i = 0; i < 3; ++i) p.tick();
        const bool running = !p.remote->state().paused && !p.srv.dbg->state().paused;
        p.remote->pause(p.ra);
        for (int i = 0; i < 6; ++i) p.tick();
        const auto st = p.remote->state();
        p.ref.dbg->run(p.rc);
        p.ref_tick();
        p.ref.dbg->pause(p.rc);
        p.ref_tick();
        const auto rs = p.ref.dbg->state();
        check("RTG-RUN-01", "run() runs the served machine and pause() stops it: one Resumed and one Paused (User, by the "
                            "caller) reach the listener, as on the reference",
              running && st.paused && st.pause_reason.kind == PauseReason::Kind::User && st.pause_reason.by == p.ra &&
                  p.rl.resumed == 1 && p.rl.paused.size() == 1 && p.rl.paused[0].reason.by == p.ra &&
                  rs.pause_reason.kind == PauseReason::Kind::User && rs.pause_reason.by == p.rc);
    }
    {
        Pair p;
        p.remote->run(p.ra);
        p.tick();
        p.link->mark();
        const Result r = p.remote->run(p.ra);
        check("RTG-RUN-02", "run() on a running remote is Ok and writes no frame",
              r == Result::Ok && p.link->since() == 0 && p.remote->state().paused == false);
    }
    {
        Pair p;
        const auto a = p.remote->subscribe(p.ra, exec_sub(0x8003));
        const auto b = p.ref.dbg->subscribe(p.rc, exec_sub(0x8003));
        const bool stopped = p.remote_run_to_stop() && p.ref_run_to_stop();
        const auto st = p.remote->state();
        const auto rs = p.ref.dbg->state();
        check("RTG-RUN-03", "a RAM breakpoint stops both: Breakpoint{own id, 0x8003}, matched == [id], equal registers",
              stopped && st.pause_reason.kind == PauseReason::Kind::Breakpoint && st.pause_reason.id == a.value &&
                  st.pause_reason.addr == 0x8003 && rs.pause_reason.kind == PauseReason::Kind::Breakpoint &&
                  rs.pause_reason.id == b.value && p.rl.paused.size() == 1 && p.rl.paused[0].matched.size() == 1 &&
                  p.rl.paused[0].matched[0].event_id == a.value && regs_equal(p.remote->registers(), p.ref.dbg->registers()));
    }
    {
        Pair p;
        const uint16_t pg = static_cast<uint16_t>(p.slot_page(4));
        jnext::dbg::Subscription wrong = exec_sub(0x8003);
        wrong.filter.page = static_cast<uint16_t>(pg + 1);
        p.remote->subscribe(p.ra, wrong);
        p.ref.dbg->subscribe(p.rc, wrong);
        // quiet AND no hit at the remote at all: a hit the client then ignores would cost a CONTINUE
        const bool silent = p.both_run_quiet(6) && p.link->count(CMD_CONTINUE) == 1;
        jnext::dbg::Subscription right = exec_sub(0x8003);
        right.filter.page = pg;
        const auto rid = p.remote->subscribe(p.ra, right);
        const auto fid = p.ref.dbg->subscribe(p.rc, right);
        bool stopped = false;
        for (int i = 0; i < 400 && !stopped; ++i) {
            p.tick();
            p.ref_tick();
            stopped = p.remote->state().paused && p.ref.dbg->state().paused;
        }
        check("RTG-RUN-04", "a page-qualified breakpoint: the wrong page stops neither facade, the right page stops both "
                            "and it is the right subscription that the stop names",
              silent && stopped && p.remote->state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
                  p.ref.dbg->state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
                  p.remote->state().pause_reason.id == rid.value && p.ref.dbg->state().pause_reason.id == fid.value &&
                  p.remote->registers().PC == 0x8003);
    }
    {
        Pair p;
        // a program in ROM: patch the ROM image both machines read at 0x0000
        for (Machine* m : {&p.ref, &p.srv}) {
            const auto sel = p.ref.dbg->rom_select();
            uint8_t* rom = const_cast<uint8_t*>(m->emu.mmu().rom_image_ptr(static_cast<uint8_t>(sel.index)));
            if (rom) {
                rom[0x100] = 0x00;
                rom[0x101] = 0x00;
                rom[0x102] = 0x18;
                rom[0x103] = 0xFE;
            }
            Z80Registers r = m->emu.cpu().get_registers();
            r.PC           = 0x0100;
            m->emu.cpu().set_registers(r);
        }
        p.remote->subscribe(p.ra, exec_sub(0x0101));
        p.ref.dbg->subscribe(p.rc, exec_sub(0x0101));
        const bool stopped = p.remote_run_to_stop() && p.ref_run_to_stop();
        check("RTG-RUN-05", "a breakpoint in ROM (any page) stops both",
              stopped && p.remote->registers().PC == 0x0101 && p.ref.dbg->registers().PC == 0x0101 &&
                  p.remote->state().pause_reason.kind == PauseReason::Kind::Breakpoint);
    }
    {
        Pair p;
        jnext::dbg::Subscription s = exec_sub(0x8003);
        s.condition = [](const jnext::dbg::Event&, const Debugger& d) { return a_reg(d) == 5; };
        p.remote->subscribe(p.ra, s);
        p.ref.dbg->subscribe(p.rc, s);
        const bool stopped = p.remote_run_to_stop() && p.ref_run_to_stop();
        check("RTG-RUN-06", "a breakpoint with a register condition: every false hit is one CONTINUE frame, and both stop at "
                            "the same true hit with equal registers",
              stopped && a_reg(*p.remote) == 5 && a_reg(*p.ref.dbg) == 5 && p.link->count(CMD_CONTINUE) == 5 &&
                  regs_equal(p.remote->registers(), p.ref.dbg->registers()));
    }
    {
        Pair p(prog_djnz());
        jnext::dbg::Subscription s = exec_sub(0x8002);
        s.once = true;
        p.remote->subscribe(p.ra, s);
        p.ref.dbg->subscribe(p.rc, s);
        const bool stopped = p.remote_run_to_stop() && p.ref_run_to_stop();
        const int removes = p.link->count(CMD_REMOVE_BREAKPOINT);
        const bool quiet = p.both_run_quiet(30);
        check("RTG-RUN-07", "a once breakpoint stops once on both; the remote's CMD_REMOVE_BREAKPOINT follows the hit; the "
                            "next run does not stop there",
              stopped && removes == 1 && quiet && p.remote->registers().PC == p.ref.dbg->registers().PC);
    }
    {
        Pair p(prog_djnz());
        int n_remote = 0, n_ref = 0;
        jnext::dbg::Subscription c1 = exec_sub(0x8002);
        c1.handler = [&](const jnext::dbg::Event&, Debugger&) { ++n_remote; return jnext::dbg::Action::Continue; };
        jnext::dbg::Subscription c2 = c1;
        c2.handler = [&](const jnext::dbg::Event&, Debugger&) { ++n_ref; return jnext::dbg::Action::Continue; };
        p.remote->subscribe(p.ra, c1);
        p.ref.dbg->subscribe(p.rc, c2);
        p.both_run_quiet(60);
        // a Log action logs the same lines on both
        Pair q(prog_djnz());
        jnext::dbg::Subscription lg = exec_sub(0x8002);
        lg.action = jnext::dbg::Action::Log;
        q.remote->subscribe(q.ra, lg);
        q.ref.dbg->subscribe(q.rc, lg);
        q.both_run_quiet(60);
        check("RTG-RUN-08", "a Continue handler counts the same seven hits on both; a Log action logs the same lines",
              n_remote == 7 && n_ref == 7 && q.rl.event_logs().size() == 7 &&
                  q.rl.event_logs() == q.refl.event_logs());
    }
    {
        Pair p;
        const auto a = p.remote->subscribe(p.ra, exec_sub(0x8003));
        p.link->mark();
        p.remote->set_enabled(p.ra, a.value, false);
        const bool removed = p.link->count_since(CMD_REMOVE_BREAKPOINT) == 1;
        p.remote->run(p.ra);
        for (int i = 0; i < 5; ++i) p.tick();
        const bool no_stop = !p.remote->state().paused;
        p.remote->pause(p.ra);
        for (int i = 0; i < 4; ++i) p.tick();
        p.link->mark();
        p.remote->set_enabled(p.ra, a.value, true);
        const bool readded = p.link->count_since(CMD_ADD_BREAKPOINT) == 1;
        p.link->mark();
        p.remote->set_master_enabled(false);
        const bool master = p.link->count_since(CMD_REMOVE_BREAKPOINT) == 1;
        p.link->mark();
        p.remote->set_master_enabled(true);
        const bool master_on = p.link->count_since(CMD_ADD_BREAKPOINT) == 1;
        p.link->mark();
        p.remote->set_client_enabled(p.ra, false);
        const bool client = p.link->count_since(CMD_REMOVE_BREAKPOINT) == 1;
        check("RTG-RUN-09", "enable and disable mirror to the remote: a disabled breakpoint is removed (CMD 41) and does not "
                            "stop; enabling adds it (CMD 40); master off and a client switch remove it too",
              removed && no_stop && readded && master && master_on && client);
    }
    {
        Pair p;
        const auto a = p.remote->subscribe(p.ra, exec_sub(0x8003));
        const auto b = p.remote->subscribe(p.ra, exec_sub(0x8003));
        const int adds = p.link->count(CMD_ADD_BREAKPOINT);
        p.remote->unsubscribe(p.ra, a.value);
        const int mid = p.link->count(CMD_REMOVE_BREAKPOINT);
        p.remote->unsubscribe(p.ra, b.value);
        check("RTG-RUN-10", "two subscriptions on one address make one CMD 40; removing both makes one CMD 41, after the "
                            "second",
              adds == 1 && mid == 0 && p.link->count(CMD_REMOVE_BREAKPOINT) == 1);
    }
}

static void run_rows_b() {
    {
        Pair p;
        StopListener own;
        jnext::dbg::ClientInfo ci;
        ci.name = "own";
        ci.kind = jnext::dbg::ClientKind::Test;
        const ClientId oc = p.srv.dbg->attach(ci).value;
        p.srv.dbg->set_listener(oc, &own);
        const auto y = p.srv.dbg->subscribe(oc, exec_sub(0x8002));
        const auto x = p.remote->subscribe(p.ra, exec_sub(0x8006));
        const bool first = p.remote_run_to_stop();
        const auto st1 = p.remote->state();
        const bool ymatch = own.paused.size() == 1 && own.paused[0].matched.size() == 1 &&
                            own.paused[0].matched[0].event_id == y.value;
        const bool other = st1.pause_reason.kind == PauseReason::Kind::User && st1.pause_reason.by == jnext::dbg::CLIENT_NONE &&
                           st1.pause_reason.text == "breakpoint of another debugger client" &&
                           p.rl.paused.size() == 1 && p.rl.paused[0].matched.empty();
        const bool second = p.remote_run_to_stop();
        const auto st2 = p.remote->state();
        check("RTG-RUN-11", "another client of the served jnext: its breakpoint's stop is its own (it sees Breakpoint) and "
                            "the remote sees User with no owner, the remote's words and no match; after run() the remote's "
                            "own breakpoint is a Breakpoint",
              first && ymatch && other && second && st2.pause_reason.kind == PauseReason::Kind::Breakpoint &&
                  st2.pause_reason.id == x.value && st2.pause_reason.addr == 0x8006);
    }
    {
        // A: the session pause (CMD_INIT stopped a running machine) is released at the end of the session
        Pair a(prog_loop(0x9000), false);
        a.connect_remote();
        const bool paused_in_session = a.srv.dbg->state().paused;
        a.disconnect();
        for (int i = 0; i < 4; ++i) a.srv.dbg->pump(PumpBudget{});
        const bool released = !a.srv.dbg->state().paused;
        // B: a pause the served jnext's own client made before the connect survives it
        Pair b(prog_loop(0x9000), false);
        StopListener own;
        jnext::dbg::ClientInfo ci;
        ci.name = "own";
        ci.kind = jnext::dbg::ClientKind::Test;
        const ClientId oc = b.srv.dbg->attach(ci).value;
        b.srv.dbg->pause(oc);
        b.connect_remote();
        b.disconnect();
        for (int i = 0; i < 4; ++i) b.srv.dbg->pump(PumpBudget{});
        check("RTG-RUN-12", "ending the session releases the pause CMD_INIT made on a running machine, and leaves a pause the "
                            "served jnext's own client made before it",
              paused_in_session && released && b.srv.dbg->state().paused);
    }
    {
        Pair p;
        p.remote->pause(p.ra);
        p.link->mark();
        p.remote->detach(p.ra);
        for (int i = 0; i < 3; ++i) p.tick();
        const bool remote_ran = p.link->count_since(CMD_CONTINUE) == 1 && !p.srv.dbg->state().paused;
        p.ref.dbg->detach(p.rc);
        p.ref_tick();
        check("RTG-RUN-13", "SES-01 over DZRP: the pausing client's detach, the last arming client, sends CMD_CONTINUE and "
                            "the served machine runs, as the reference's detach resumes the reference",
              remote_ran && !p.ref.dbg->state().paused);
    }
    {
        Scripted s;
        const auto sub = [&] {
            jnext::dbg::Subscription j = exec_sub(0x8000);
            j.condition = [](const jnext::dbg::Event&, const Debugger&) { return false; };
            return s.d->subscribe(s.a, j);
        }();
        s.d->run(s.a);
        s.d->pause(s.a);
        s.s.ntf_pause(2, 0x8000, 0xFF);
        s.pump();
        const auto st = s.d->state();
        check("RTG-RUN-14", "a pause() then a breakpoint stop whose condition is false: no second CONTINUE is sent and state() "
                            "is User by the caller",
              sub.status == Result::Ok && s.s.count(CMD_CONTINUE) == 1 && st.paused &&
                  st.pause_reason.kind == PauseReason::Kind::User && st.pause_reason.by == s.a);
    }
    {
        Pair p;
        p.remote->subscribe(p.ra, exec_sub(0x8002));
        p.ref.dbg->subscribe(p.rc, exec_sub(0x8002));
        jnext::dbg::Subscription tr = exec_sub(0x8006);
        tr.transient = true;
        p.remote->subscribe(p.ra, tr);
        p.ref.dbg->subscribe(p.rc, tr);
        const bool stopped = p.remote_run_to_stop() && p.ref_run_to_stop();
        auto transient_left = [](const Debugger& d) {
            for (const auto& si : d.subscriptions(true))
                if (si.transient) return true;
            return false;
        };
        check("RTG-RUN-15", "a transient Execute subscription is dropped at the stop on both; the remote sends its CMD 41 "
                            "then",
              stopped && !transient_left(*p.remote) && !transient_left(*p.ref.dbg) &&
                  p.link->count(CMD_REMOVE_BREAKPOINT) == 1);
    }
    {
        Pair p;
        jnext::dbg::Subscription range = exec_sub(0x8000);
        range.filter.hi = 0x8010;
        jnext::dbg::Subscription space = exec_sub(0x8000);
        space.filter.space = MemSpace::rom(0);
        jnext::dbg::Subscription pages = mem_sub(0x4000, 0x40FF, Access::ReadWrite);
        pages.filter.pages = {1};
        jnext::dbg::Subscription page = exec_sub(0x8000);
        page.filter.page = 0xE0;
        jnext::dbg::Subscription port;
        port.kind = EventKind::Port;
        port.filter.port_mask = 0x00FF;
        p.link->mark();
        const Result got[] = {p.remote->subscribe(p.ra, range).status, p.remote->subscribe(p.ra, space).status,
                              p.remote->subscribe(p.ra, pages).status, p.remote->subscribe(p.ra, page).status,
                              p.remote->subscribe(p.ra, port).status};
        check("RTG-RUN-16", "subscriptions a remote cannot hold are refused with no frame: a range, a ROM space, pages, page "
                            "0xE0 (RefusedUnavailable) and a Port event (Unsupported)",
              got[0] == Result::RefusedUnavailable && got[1] == Result::RefusedUnavailable &&
                  got[2] == Result::RefusedUnavailable && got[3] == Result::RefusedUnavailable &&
                  got[4] == Result::Unsupported && p.link->since() == 0 && p.remote->subscriptions(true).empty());
    }
    {
        // (a) an NTF while paused is ignored
        Scripted a;
        a.s.ntf_pause(2, 0x8000, 0);
        a.pump();
        const bool ignored = a.sl.paused.empty() && a.d->state().pause_reason.kind == PauseReason::Kind::User;
        // (b) a second NTF for one CONTINUE is dropped; (c) reason 9 is an unknown stop
        Scripted b;
        b.d->run(b.a);
        b.pump();
        b.s.ntf_pause(255, 0x1111, 0, "first");
        b.s.ntf_pause(255, 0x2222, 0, "second");
        b.pump();
        const auto sb = b.d->state();
        Scripted c;
        c.d->run(c.a);
        c.s.ntf_pause(9, 0x3333, 0, "odd");
        c.pump();
        const auto sc = c.d->state();
        // (d) bank 0xFF is "page unknown": it matches a page-any breakpoint and not a page-qualified one
        Scripted d;
        const auto any = d.d->subscribe(d.a, exec_sub(0x8000));
        jnext::dbg::Subscription q = exec_sub(0x8000);
        q.filter.page = 3;
        d.d->subscribe(d.a, q);
        d.d->run(d.a);
        d.pump();
        d.s.ntf_pause(2, 0x8000, 0xFF);
        d.pump();
        check("RTG-RUN-17", "hostile notifications: one while paused is ignored; the second of two for one CONTINUE is "
                            "dropped; reason 9 is User with no owner and 'stop reason 9'; bank 0xFF matches a page-any "
                            "breakpoint and not a page-qualified one",
              ignored && sb.pause_reason.text == "first" && b.sl.paused.size() == 1 &&
                  sc.pause_reason.kind == PauseReason::Kind::User &&
                  sc.pause_reason.text.find("stop reason 9") != std::string::npos && d.sl.paused.size() == 1 &&
                  d.sl.paused[0].matched.size() == 1 && d.sl.paused[0].matched[0].event_id == any.value);
    }
    {
        Scripted s([](Script& sc) { sc.bp_id_override = 0; });
        const auto zero = s.d->subscribe(s.a, exec_sub(0x8000));
        Scripted t([](Script& sc) { sc.bp_id_override = 7; });
        const auto one = t.d->subscribe(t.a, exec_sub(0x8000));
        const auto two = t.d->subscribe(t.a, exec_sub(0x8001));
        check("RTG-RUN-18", "a breakpoint id of 0 refuses the subscribe and leaves the table; the same id for two addresses "
                            "is a protocol error that loses the target",
              zero.status == Result::RefusedUnavailable && s.d->subscriptions(true).empty() && s.tgt->connected() &&
                  one.status == Result::Ok && two.status == Result::Unsupported && !t.tgt->connected());
    }
    {
        Scripted s;
        const Result r = s.d->pause(s.a);
        check("RTG-RUN-19", "pause() while the client believes the target paused still sends CMD_PAUSE",
              r == Result::Ok && s.s.count(CMD_PAUSE) == 1);
    }
    {
        Scripted s;
        s.d->run(s.a);
        const Command* c = nullptr;
        for (const auto& k : s.s.cmds)
            if (k.id == CMD_CONTINUE) c = &k;
        check("RTG-RUN-20", "CMD_CONTINUE is sent with eleven payload bytes (DeZog's and dezogif_ng's length), all zero: no "
                            "temporary breakpoint and no alternate command",
              c && c->payload == Bytes(11, 0));
    }
}

static void run_rows_d() {
    {
        // A line read while the target runs, between two pumps, must not survive into the stop that follows.
        Pair p;
        p.remote->registers();
        p.remote->subscribe(p.ra, exec_sub(0x8006));
        p.remote->run(p.ra);
        uint8_t before[1], after[1];
        p.remote->peek(MemSpace::cpu(), 0x9000, 1, before);   // cached while running
        p.srv.emu.run_frame();                                 // the loop writes 0x9000 and stops at 0x8006
        p.srv.dbg->pump(PumpBudget{});
        p.remote->pump(PumpBudget{});
        const bool stopped = p.remote->state().paused;
        p.remote->peek(MemSpace::cpu(), 0x9000, 1, after);
        check("RTG-RUN-22", "a line cached while running is not served after the stop: the read after a breakpoint stop "
                            "shows what the served machine holds",
              stopped && after[0] == p.srv.emu.mmu().peek(0x9000) && after[0] != before[0]);
    }
    {
        // The served jnext's own client resumed the machine behind the remote's back; the remote's pause() stops it and the
        // NTF_PAUSE it owes must refresh the remote's picture.
        Pair p;
        jnext::dbg::ClientInfo ci;
        ci.name = "own";
        ci.kind = jnext::dbg::ClientKind::Test;
        const ClientId oc = p.srv.dbg->attach(ci).value;
        p.remote->registers();
        uint8_t stale[1];
        p.remote->peek(MemSpace::cpu(), 0x9000, 1, stale);   // cached before the machine moves
        p.srv.dbg->run(oc);
        for (int i = 0; i < 3; ++i) p.srv.emu.run_frame();
        p.remote->pause(p.ra);
        for (int i = 0; i < 4; ++i) p.tick();
        uint8_t now[1];
        p.remote->peek(MemSpace::cpu(), 0x9000, 1, now);
        check("RTG-RUN-23", "pause() on a target believed paused that the served side resumed: the NTF_PAUSE it owes is taken "
                            "and the registers and memory then equal the served machine's",
              p.srv.dbg->state().paused && regs_equal(p.remote->registers(), p.srv.dbg->registers()) &&
                  now[0] == p.srv.emu.mmu().peek(0x9000) && now[0] != stale[0]);
    }
}

static void run_rows_c() {
    // The target's own stop mapping, below the facade: what each NTF_PAUSE becomes, and which page it names.
    Scripted s;
    jnext::dbg::TargetStop t;
    bool all = true;
    std::string why;
    auto stop = [&](std::uint8_t reason, std::uint16_t addr, std::uint8_t bank1, const char* text) {
        s.tgt->client().poll(0);   // nothing owed yet: a CONTINUE makes the stop expected
        s.tgt->resume();
        s.s.ntf_pause(reason, addr, bank1, text);
        s.s.service();
        s.tgt->service_once(0);
        return s.tgt->take_stop(t);
    };
    struct Case {
        std::uint8_t reason;
        std::uint16_t addr;
        std::uint8_t bank1;
        const char* text;
        jnext::dbg::TargetStop::Why want;
        std::uint16_t page;
        const char* want_text;
    };
    using W = jnext::dbg::TargetStop::Why;
    const Case cases[] = {
        {1, 0x8000, 0, "", W::Manual, jnext::dbg::PAGE_ANY, ""},
        {2, 0x8001, 5, "", W::Breakpoint, 4, ""},            // bank+1 5 is page 4
        {2, 0x8002, 0xFF, "", W::Breakpoint, jnext::dbg::PAGE_ANY, ""},   // ROM in slot 0: no page
        {2, 0x8003, 0, "", W::Breakpoint, jnext::dbg::PAGE_ANY, ""},      // ROM in the other slots
        {2, 0x8004, 225, "", W::Breakpoint, jnext::dbg::PAGE_ANY, ""},    // no such page: not page 224
        {3, 0x9000, 225 - 1, "", W::WatchRead, 223, ""},
        {4, 0x9001, 1, "", W::WatchWrite, 0, ""},
        {255, 0x8005, 0, "other", W::Other, jnext::dbg::PAGE_ANY, "other"},
        {9, 0x8006, 0, "odd", W::Other, jnext::dbg::PAGE_ANY, "stop reason 9: odd"},
        {0, 0x8007, 0, "", W::Other, jnext::dbg::PAGE_ANY, "stop reason 0"},
    };
    int n = 0;
    for (const Case& c : cases) {
        ++n;
        const bool got = stop(c.reason, c.addr, c.bank1, c.text);
        if (!got || t.why != c.want || t.addr != c.addr || t.page != c.page || t.text != c.want_text) {
            all = false;
            why = "case " + std::to_string(n);
            break;
        }
    }
    check("RTG-RUN-21", "an NTF_PAUSE becomes a TargetStop: reasons 1 to 4 and 255 by name, any other reason (0 included) "
                        "an Other stop saying so, and bank+1 names a RAM page only in 1..224 (0 and 0xFF are ROM, "
                        "225 and up no page)",
          all, why);
}

// ==============================================================================================
// RTG-WP - watchpoints (R2.3)
// ==============================================================================================

static bool watch_stop(Pair& p, const jnext::dbg::Subscription& s, jnext::dbg::EventId* rid, jnext::dbg::EventId* fid) {
    const auto a = p.remote->subscribe(p.ra, s);
    const auto b = p.ref.dbg->subscribe(p.rc, s);
    *rid = a.value;
    *fid = b.value;
    return a.status == Result::Ok && b.status == Result::Ok && p.remote_run_to_stop() && p.ref_run_to_stop();
}

static void wp_rows() {
    {
        Pair p;
        jnext::dbg::EventId a = 0, b = 0;
        const bool stopped = watch_stop(p, mem_sub(0x9000, 0x9000, Access::Write), &a, &b);
        const auto st = p.remote->state();
        const auto rs = p.ref.dbg->state();
        check("RTG-WP-01", "a write watch stops both: Watch{id, 0x9000, Write} and equal registers",
              stopped && st.pause_reason.kind == PauseReason::Kind::Watch && st.pause_reason.id == a &&
                  st.pause_reason.addr == 0x9000 && st.pause_reason.access == Access::Write &&
                  rs.pause_reason.kind == PauseReason::Kind::Watch && rs.pause_reason.access == Access::Write &&
                  regs_equal(p.remote->registers(), p.ref.dbg->registers()));
    }
    {
        Pair p;
        jnext::dbg::EventId a = 0, b = 0;
        const bool stopped = watch_stop(p, mem_sub(0x9000, 0x9000, Access::Read), &a, &b);
        const auto st = p.remote->state();
        const auto rs = p.ref.dbg->state();
        check("RTG-WP-02", "a read watch stops both: Watch{id, 0x9000, Read} and equal registers",
              stopped && st.pause_reason.kind == PauseReason::Kind::Watch && st.pause_reason.access == Access::Read &&
                  st.pause_reason.addr == 0x9000 && rs.pause_reason.access == Access::Read &&
                  regs_equal(p.remote->registers(), p.ref.dbg->registers()));
    }
    {
        Pair p;
        jnext::dbg::EventId a = 0, b = 0;
        const bool first = watch_stop(p, mem_sub(0x9000, 0x9000, Access::ReadWrite), &a, &b);
        const bool w1 = p.remote->state().pause_reason.access == Access::Write &&
                        p.ref.dbg->state().pause_reason.access == Access::Write;
        const bool second = p.remote_run_to_stop() && p.ref_run_to_stop();
        const bool r2 = p.remote->state().pause_reason.access == Access::Read &&
                        p.ref.dbg->state().pause_reason.access == Access::Read;
        check("RTG-WP-03", "a read-write watch catches the write and then the read of one pass on both",
              first && w1 && second && r2 && regs_equal(p.remote->registers(), p.ref.dbg->registers()));
    }
    {
        Pair hi(prog_loop(0x9010));
        jnext::dbg::EventId a = 0, b = 0;
        const bool at_hi = watch_stop(hi, mem_sub(0x9000, 0x9010, Access::Write), &a, &b) &&
                           hi.remote->state().pause_reason.addr == 0x9010 && hi.ref.dbg->state().pause_reason.addr == 0x9010;
        Pair past(prog_loop(0x9010));
        past.remote->subscribe(past.ra, mem_sub(0x9000, 0x900F, Access::Write));
        past.ref.dbg->subscribe(past.rc, mem_sub(0x9000, 0x900F, Access::Write));
        check("RTG-WP-04", "a watch range is inclusive: an access at hi stops both, one at hi + 1 stops neither",
              at_hi && past.both_run_quiet(8));
    }
    {
        Pair p;
        const uint16_t pg = static_cast<uint16_t>(p.slot_page(4));   // 0x9000 is in slot 4
        jnext::dbg::Subscription wrong = mem_sub(0x9000, 0x9000, Access::Write);
        wrong.filter.page = static_cast<uint16_t>(pg + 1);
        p.remote->subscribe(p.ra, wrong);
        p.ref.dbg->subscribe(p.rc, wrong);
        const bool silent = p.both_run_quiet(6) && p.link->count(CMD_CONTINUE) == 1;
        jnext::dbg::Subscription right = mem_sub(0x9000, 0x9000, Access::Write);
        right.filter.page = pg;
        const auto rid = p.remote->subscribe(p.ra, right);
        const auto fid = p.ref.dbg->subscribe(p.rc, right);
        bool stopped = false;
        for (int i = 0; i < 400 && !stopped; ++i) {
            p.tick();
            p.ref_tick();
            stopped = p.remote->state().paused && p.ref.dbg->state().paused;
        }
        check("RTG-WP-05", "a page-qualified watch: the wrong page is silent on both, the right page stops both and it is "
                           "the right subscription that the stop names",
              silent && stopped && p.remote->state().pause_reason.kind == PauseReason::Kind::Watch &&
                  p.ref.dbg->state().pause_reason.kind == PauseReason::Kind::Watch &&
                  p.remote->state().pause_reason.id == rid.value && p.ref.dbg->state().pause_reason.id == fid.value);
    }
    {
        Pair p;
        const auto a = p.remote->subscribe(p.ra, mem_sub(0x9000, 0x900F, Access::ReadWrite));
        const auto b = p.remote->subscribe(p.ra, mem_sub(0x9000, 0x900F, Access::ReadWrite));
        const int adds = p.link->count(CMD_ADD_WATCHPOINT);
        const Bytes* add_frame  = p.link->last_payload(CMD_ADD_WATCHPOINT);
        const Bytes  add_payload = add_frame ? *add_frame : Bytes();
        p.remote->unsubscribe(p.ra, a.value);
        const int mid = p.link->count(CMD_REMOVE_WATCHPOINT);
        p.remote->unsubscribe(p.ra, b.value);
        const Bytes* rm = p.link->last_payload(CMD_REMOVE_WATCHPOINT);
        const bool quiet = [&] {
            p.remote->run(p.ra);
            for (int i = 0; i < 8; ++i) p.tick();
            return !p.remote->state().paused;
        }();
        check("RTG-WP-06", "removal: two identical subscriptions share one CMD 42; the last removal sends one CMD 43 carrying "
                           "the add's exact tuple; no stop follows",
              adds == 1 && mid == 0 && p.link->count(CMD_REMOVE_WATCHPOINT) == 1 && rm && *rm == add_payload &&
                  add_payload == Bytes({0x00, 0x90, 0x00, 0x10, 0x00, 0x03}) && quiet);
    }
    {
        Pair p;
        const auto w = p.remote->subscribe(p.ra, mem_sub(0x0000, 0xFFFF, Access::Write));
        std::vector<Bytes> adds;
        for (const auto& f : p.link->frames)
            if (f.first == CMD_ADD_WATCHPOINT) adds.push_back(f.second);
        const bool two = adds.size() == 2 && adds[0] == Bytes({0x00, 0x00, 0x00, 0x00, 0x80, 0x02}) &&
                         adds[1] == Bytes({0x00, 0x80, 0x00, 0x00, 0x80, 0x02});
        const bool stopped = p.remote_run_to_stop() && p.remote->state().pause_reason.kind == PauseReason::Kind::Watch;
        p.remote->unsubscribe(p.ra, w.value);
        check("RTG-WP-07", "a full-range watch is two CMD 42 (0-0x7FFF and 0x8000-0xFFFF), a write stops the remote, and "
                           "removing it sends two CMD 43",
              w.status == Result::Ok && two && stopped && p.link->count(CMD_REMOVE_WATCHPOINT) == 2);
    }
    {
        Pair p;
        jnext::dbg::Subscription s = mem_sub(0x9000, 0x9000, Access::Write);
        s.once      = true;
        s.condition = [](const jnext::dbg::Event&, const Debugger& d) { return a_reg(d) == 5; };
        p.remote->subscribe(p.ra, s);
        p.ref.dbg->subscribe(p.rc, s);
        const bool stopped = p.remote_run_to_stop() && p.ref_run_to_stop();
        check("RTG-WP-08", "a watch whose condition is false continues; the true hit stops both with equal registers and, "
                           "being once, the remote's watch is removed (CMD 43)",
              stopped && a_reg(*p.remote) == 5 && a_reg(*p.ref.dbg) == 5 && p.link->count(CMD_CONTINUE) == 5 &&
                  p.link->count(CMD_REMOVE_WATCHPOINT) == 1 &&
                  regs_equal(p.remote->registers(), p.ref.dbg->registers()));
    }
}

// ==============================================================================================
// RSRC - a program's sources over the wire (GH #519)
//
// Oracle: the reference machine's LOCAL facade on the same z88dk fixture, and the fixture values
// source_debug_test pins (SRCDBG-Z88-01: master/sccz80-debug has 19 symbols, 46 lines, main.c:19
// at $8CB8, main at $8C92; SRCSTEP-02: master/sdcc-banked-run has banked.c:5 at $C000 in page 20).
// ==============================================================================================

namespace {

namespace fs = std::filesystem;

std::string fixture_dir() { return std::string(JNEXT_Z88DK_FIXTURE_DIR) + "/master/"; }

/// A copy of a fixture directory (without its section binaries when `bins` is false), the program
/// loaded in a reference and a served machine, `pre_both` applied to both and `pre_srv` to the
/// served one, then the remote connected.
struct Fx {
    std::string dir;
    std::string nex;
    Pair        p;
    static std::string copy(const char* name, bool bins) {
        std::string d = (fs::temp_directory_path() / ("jnext_rsrc_" + std::string(name) + "_" +
                                                       std::to_string(::getpid()) + "_" + std::to_string(counter()++)))
                            .string();
        fs::remove_all(d);
        fs::create_directories(d);
        for (const auto& e : fs::directory_iterator(fixture_dir() + name)) {
            if (!bins && e.path().extension() == ".bin") continue;
            fs::copy_file(e.path(), fs::path(d) / e.path().filename());
        }
        return d;
    }
    static int& counter() { static int c = 0; return c; }
    Fx(const char* name, bool bins = true, const std::function<void(Emulator&)>& pre_both = {},
       const std::function<void(Emulator&)>& pre_srv = {})
        : dir(copy(name, bins)), nex(dir + "/prog.nex"), p(prog_loop(0x9000), false) {
        emulator_apply_load(p.ref.emu, nex, false);
        emulator_apply_load(p.srv.emu, nex, false);
        if (pre_both) { pre_both(p.ref.emu); pre_both(p.srv.emu); }
        if (pre_srv) pre_srv(p.srv.emu);
        p.connect_remote();
    }
    ~Fx() { std::error_code ec; fs::remove_all(dir, ec); }
};

void map_page20(Emulator& e) {
    e.nextreg().write(0x56, 20);
    e.nextreg().write(0x57, 21);
}

std::string where(const Debugger& d, uint16_t addr) {
    const auto* l = d.source_location(addr);
    return l ? l->file + ":" + std::to_string(l->line) : std::string("-");
}

std::string where_in(const Debugger& d, MemSpace s, uint16_t addr) {
    const auto* l = d.source_location(s, addr);
    return l ? l->file + ":" + std::to_string(l->line) : std::string("-");
}

size_t page_count(const std::vector<SourceMap::CodeSpan>& spans) {
    std::vector<uint16_t> pages;
    for (const auto& c : spans)
        if (c.space.kind == MemSpace::Kind::Page &&
            std::find(pages.begin(), pages.end(), c.space.index) == pages.end())
            pages.push_back(c.space.index);
    return pages.size();
}

}  // namespace

static void rsrc_rows_a() {
    {
        Fx f("sccz80-debug");
        const auto rr = f.p.ref.dbg->load_program_sidecars(f.nex);
        const auto r  = f.p.remote->load_program_sidecars(f.nex);
        bool same = r.sources == rr.sources && r.symbols == rr.symbols;
        const SourceMap& rm = f.p.ref.dbg->source_map();
        size_t ranges = 0;
        for (const std::string& file : rm.files())
            for (const auto& fr : rm.file_ranges(file)) {
                ++ranges;
                for (const uint16_t a : {static_cast<uint16_t>(fr.lo), static_cast<uint16_t>(fr.hi_excl - 1)}) {
                    same = same && where_in(*f.p.remote, fr.space, a) == where_in(*f.p.ref.dbg, fr.space, a);
                    same = same && where(*f.p.remote, a) == where(*f.p.ref.dbg, a);
                }
            }
        bool sym = f.p.remote->symbols().size() == f.p.ref.dbg->symbols().size();
        sym = sym && f.p.remote->lookup(0x8C92) == f.p.ref.dbg->lookup(0x8C92);
        check("RSRC-01", "master/sccz80-debug: the sidecars on the remote are the reference's (19 symbols, 46 lines, attached) "
                         "and every line range resolves, by space and by address, as in the local session",
              r.symbols == 19 && r.sources == rr.sources && r.sources > 0 && same && ranges > 0 && sym &&
                  f.p.remote->source_map_status().state == Debugger::SourceMapStatus::State::Attached &&
                  where(*f.p.remote, 0x8CB8) == "main.c:19",
              "sym=" + std::to_string(r.symbols) + " src=" + std::to_string(r.sources) + " ref " +
                  std::to_string(rr.symbols) + "/" + std::to_string(rr.sources) + " same=" + std::to_string(same) +
                  " ranges=" + std::to_string(ranges) + " symeq=" + std::to_string(sym) + " (" + std::to_string(f.p.remote->symbols().size()) + "/" +
                  std::to_string(f.p.ref.dbg->symbols().size()) + ") at=" +
                  where(*f.p.remote, 0x8CB8) + " | " + r.problem);
    }
    {
        Fx f("sdcc-banked-run", true, map_page20);
        const auto rr = f.p.ref.dbg->load_program_sidecars(f.nex);
        const auto r  = f.p.remote->load_program_sidecars(f.nex);
        const bool at = where(*f.p.remote, 0xC000) == "banked.c:5" && where(*f.p.ref.dbg, 0xC000) == "banked.c:5" &&
                        f.p.remote->source_page(0xC000) == MemSpace::page(20) &&
                        f.p.ref.dbg->source_page(0xC000) == MemSpace::page(20);
        Fx g("sdcc-banked-run", true, [](Emulator& e) { e.nextreg().write(0x56, 0); });
        (void)g.p.ref.dbg->load_program_sidecars(g.nex);
        (void)g.p.remote->load_program_sidecars(g.nex);
        const bool off = where(*g.p.remote, 0xC000) == where(*g.p.ref.dbg, 0xC000) &&
                         where(*g.p.remote, 0xC000) != "banked.c:5";
        check("RSRC-02", "master/sdcc-banked-run: with page 20 in slot 6 both resolve $C000 to banked.c:5 in Page{20}; with "
                         "slot 6 on page 0 both give the same (other) answer",
              rr.sources > 0 && r.sources == rr.sources && at && off, where(*f.p.remote, 0xC000));
    }
    {
        Fx f("sccz80-debug");
        bool ram_eq = true, saw_rom = false, saw_ram = false, rom_ok = true;
        const auto rs = f.p.remote->mmu_slots();
        for (int s = 0; s < 8; ++s) {
            const uint16_t a = static_cast<uint16_t>(s << 13);
            if (rs[static_cast<size_t>(s)].is_rom) {
                saw_rom = true;
                rom_ok = rom_ok && f.p.remote->source_page(a) == MemSpace::cpu() &&
                         f.p.ref.dbg->source_page(a).kind == MemSpace::Kind::Rom;
            } else {
                saw_ram = true;
                ram_eq = ram_eq && f.p.remote->source_page(a) == f.p.ref.dbg->source_page(a) &&
                         f.p.remote->effective_page(a) == f.p.ref.dbg->effective_page(a);
            }
        }
        check("RSRC-03", "every slot: a RAM slot's source_page and effective_page equal the reference's; a ROM slot is cpu() on the "
                         "remote and Rom{k} locally; FetchOverlays is unavailable on the remote and served locally",
              saw_ram && saw_rom && ram_eq && rom_ok &&
                  !f.p.remote->why_unavailable(Capability::FetchOverlays).empty() &&
                  f.p.ref.dbg->why_unavailable(Capability::FetchOverlays).empty());
    }
    {
        Fx f("sccz80-debug", true, [](Emulator& e) { e.port().out(0x00E3, 0x80); });
        const MemSpace local = f.p.ref.dbg->source_page(0x0000);
        check("RSRC-04", "DivMMC mapped (port 0xE3 bit 7): the local source_page($0000) is the DivMMC identity, the remote's is "
                         "cpu() (the documented divergence: the target sends the slot's page only)",
              local.kind != MemSpace::Kind::Rom && local.kind != MemSpace::Kind::Cpu &&
                  f.p.remote->source_page(0x0000) == MemSpace::cpu(),
              std::to_string(static_cast<int>(local.kind)));
    }
}

static void rsrc_rows_b() {
    const auto line_stop = [](Fx& f, const char* expr, const uint8_t want_bank1, bool& same_pc, uint16_t& pc,
                              bool& cmd_ok) {
        (void)f.p.ref.dbg->load_program_sidecars(f.nex);
        (void)f.p.remote->load_program_sidecars(f.nex);
        jnext::dbg::Subscription like;
        like.kind = EventKind::Execute;
        f.p.link->mark();
        const auto g  = f.p.remote->subscribe_source_line(f.p.ra, expr, like);
        const auto gr = f.p.ref.dbg->subscribe_source_line(f.p.rc, expr, like);
        const Bytes* pl = f.p.link->last_payload(CMD_ADD_BREAKPOINT);
        cmd_ok = g.status == Result::Ok && gr.status == Result::Ok && pl && pl->size() > 2 &&
                 (*pl)[2] == want_bank1 && f.p.link->count_since(CMD_ADD_BREAKPOINT) == 1;
        const bool a = f.p.remote_run_to_stop();
        const bool b = f.p.ref_run_to_stop();
        pc      = f.p.remote->registers().PC;
        same_pc = a && b && pc == f.p.ref.dbg->registers().PC &&
                  f.p.remote->state().pause_reason.kind == PauseReason::Kind::Breakpoint &&
                  f.p.ref.dbg->state().pause_reason.kind == PauseReason::Kind::Breakpoint;
    };
    {
        Fx f("sccz80-debug");
        bool same = false, cmd = false;
        uint16_t pc = 0;
        line_stop(f, "main.c:19", 5, same, pc, cmd);   // page 4: DZRP's bank+1 is 5
        check("RSRC-05", "a line breakpoint main.c:19 goes out page-qualified (CMD_ADD_BREAKPOINT bank+1 = the start's page + 1) and "
                         "both machines stop at the same address with a Breakpoint",
              cmd && same && where(*f.p.remote, pc) == "main.c:19", "pc=" + std::to_string(pc));
    }
    {
        Fx f("sdcc-banked-run", true, map_page20);
        bool same = false, cmd = false;
        uint16_t pc = 0;
        line_stop(f, "banked.c:5", 21, same, pc, cmd);
        check("RSRC-06", "a line breakpoint banked.c:5 (page 20) goes out as bank+1 21 and both machines stop at $C000 with page "
                         "20 mapped",
              cmd && same && pc == 0xC000 && f.p.remote->effective_page(0xC000) == 20 &&
                  f.p.ref.dbg->effective_page(0xC000) == 20,
              "pc=" + std::to_string(pc));
    }
    {
        Fx f("sdcc-banked-run", true, map_page20);
        const size_t want_pages = [&] {
            (void)f.p.ref.dbg->load_program_sidecars(f.nex);
            return page_count(f.p.ref.dbg->source_map().code_spans());
        }();
        LogTap tap;
        f.p.link->mark();
        const auto r = f.p.remote->load_program_sidecars(f.nex);
        check("RSRC-10", "master/sdcc-banked-run with its section binaries: attached, the log says the target's code matches, and "
                         "it costs at most one round trip per page holding code on top of the connect burst",
              r.sources > 0 && r.problem.empty() && tap.count("the target's code matches") == 1 && want_pages >= 2 &&
                  f.p.link->rounds <= 1 + want_pages,
              std::to_string(f.p.link->rounds) + " rounds, " + std::to_string(want_pages) + " pages | " + r.problem);
    }
    {
        Fx f("sdcc-banked-run", false, map_page20, [](Emulator& e) {
            e.mmu().write(0xC000, static_cast<uint8_t>(e.mmu().read(0xC000) ^ 0xFF));
        });
        const auto r = f.p.remote->load_program_sidecars(f.nex);
        check("RSRC-11", "without section binaries the NEX alone judges: the first byte of banked.c:5 changed on the target refuses "
                         "the sources and symbols, names P14+$0000 with both bytes, and no line shows at $C000",
              r.sources == -1 && r.symbols == -1 && r.problem.find("P14+$0000") != std::string::npos &&
                  r.problem.find("target $") != std::string::npos && where(*f.p.remote, 0xC000) == "-",
              r.problem);
    }
}

static void rsrc_rows_c() {
    {
        // Every code range of sccz80-debug: its last byte flipped on the served machine is a mismatch.
        std::vector<SourceMap::CodeSpan> spans;
        {
            Fx f("sccz80-debug", false);
            (void)f.p.ref.dbg->load_program_sidecars(f.nex);
            spans = f.p.ref.dbg->source_map().code_spans();
        }
        size_t refused = 0;
        for (const auto& c : spans) {
            const uint16_t last = static_cast<uint16_t>(std::min<uint32_t>(c.hi_excl, (c.lo & 0xE000u) + 0x2000u) - 1);
            Fx f("sccz80-debug", false, {}, [last](Emulator& e) {
                e.mmu().write(last, static_cast<uint8_t>(e.mmu().read(last) ^ 0xFF));
            });
            const auto r = f.p.remote->load_program_sidecars(f.nex);
            refused += r.sources == -1 && r.problem.find("is not the build these sources describe") != std::string::npos;
        }
        check("RSRC-12", "every code range of master/sccz80-debug: its last byte flipped on the target is a mismatch each time",
              !spans.empty() && refused == spans.size(), std::to_string(refused) + "/" + std::to_string(spans.size()));
    }
    {
        // A byte in a data or bss line range is not a mismatch. $8F0A is `data_stdio`'s line (sccz80-debug's
        // prog.map: `__ASM_LINE_8832_..._zxn_crt_2easm_2em4 = $8F0A ; addr, local, , zxn_crt_asm_m4, data_stdio`).
        const uint16_t data_at = 0x8F0A;
        bool mapped = false;   // the byte is on a mapped line
        {
            Fx f("sccz80-debug", false);
            (void)f.p.ref.dbg->load_program_sidecars(f.nex);
            mapped = f.p.ref.dbg->source_location(MemSpace::page(4), data_at) != nullptr;
        }
        Fx f("sccz80-debug", false, {}, [](Emulator& e) {
            e.mmu().write(data_at, static_cast<uint8_t>(e.mmu().read(data_at) ^ 0xFF));
        });
        const auto r = f.p.remote->load_program_sidecars(f.nex);
        check("RSRC-13", "a byte of a data or bss line range changed on the target is not a mismatch (a running program writes "
                         "it): the map attaches",
              mapped && r.sources > 0 && r.sources == f.p.ref.dbg->load_program_sidecars(f.nex).sources,
              "mapped=" + std::to_string(mapped) + " src=" + std::to_string(r.sources) + " | " + r.problem);
    }
    {
        Fx f("sdcc-banked-run", false, map_page20, [](Emulator& e) {
            uint8_t* from = e.mmu().nr_page_ptr(20);
            std::memcpy(e.mmu().nr_page_ptr(22), from, 8192);
            std::memset(from, 0, 8192);
        });
        const auto r = f.p.remote->load_program_sidecars(f.nex);
        check("RSRC-14", "page 20's bytes moved to page 22 and page 20 cleared on the target: a mismatch naming P14",
              r.sources == -1 && r.problem.find("P14+") != std::string::npos,
              "src=" + std::to_string(r.sources) + " | " + r.problem);
    }
    {
        Fx f("sdcc-banked-run", true, [](Emulator& e) { e.nextreg().write(0x56, 0); });
        const auto rr = f.p.ref.dbg->load_program_sidecars(f.nex);
        const auto r  = f.p.remote->load_program_sidecars(f.nex);
        check("RSRC-15", "slot 6 remapped to page 0 with page 20's bytes intact is not a mismatch (the match reads the page, not "
                         "the slot) and no line shows at $C000 on either machine",
              rr.sources > 0 && r.sources == rr.sources && where(*f.p.remote, 0xC000) == "-" &&
                  where(*f.p.ref.dbg, 0xC000) == "-",
              r.problem);
    }
    {
        Fx f("sdcc-banked-run", true, map_page20, [](Emulator& e) {
            e.mmu().write(0xC000, static_cast<uint8_t>(e.mmu().read(0xC000) ^ 0xFF));
        });
        const auto side = f.p.remote->load_program_sidecars(f.nex);
        const auto no   = f.p.remote->load_source_map(f.dir + "/prog.map", false, false, false);
        const auto yes  = f.p.remote->load_source_map(f.dir + "/prog.map", true, false, false);
        check("RSRC-16", "the confirm path over the wire: the sidecar load is refused, the hand load is refused with identity false, "
                         "and accepting it attaches and banked.c:5 resolves",
              side.sources == -1 && no.count < 0 && no.identity.has_value() && !*no.identity && yes.count > 0 &&
                  where(*f.p.remote, 0xC000) == "banked.c:5",
              no.error + " | " + yes.error);
    }
}

int main() {
    std::printf("remote_target_test - a Debugger over a remote jnext (GH #515)\n");
    ins_rows_a();
    ins_rows_b();
    run_rows_a();
    run_rows_b();
    run_rows_c();
    run_rows_d();
    wp_rows();
    rsrc_rows_a();
    rsrc_rows_b();
    rsrc_rows_c();

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
