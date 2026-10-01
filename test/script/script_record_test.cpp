// jnext::script — the RECORDER (GH #20 as re-scoped into GH #26 WP6;
// dsl-frontend.md §7, "WP6 as built", Appendix L), over a real machine.
//
//   REC-NAMES-*     the key names it writes and `press` reads back (key_names.h)
//   REC-EDGE-*      what an input change becomes: one level edge per changed
//                   bit, stamped K-1 for a change first seen at E_K
//   REC-CAP-*       captures: `.scr` + `compare_scr` when only the ULA is on,
//                   a PNG + `screenshot` otherwise; Alt+8 (host key 8)
//   REC-HDR-*       the header and the precondition asserts
//   REC-WARN-*      what it cannot record exactly, and says so
//   REC-LIFE-*      start / stop / a cold boot
//   REC-RT-*        THE ROUND TRIP: record a session driven the way the GUI
//                   drives it (Keyboard::set_key between frames), emit, replay
//                   the emitted script on a fresh machine — the same input every
//                   frame, the same guest result, every compare_scr equal
//   REC-HOST-*      ScriptHost's recorder: `--record-script`, `--script-key`
//                   with no script, the Script tab's log, written on the way out
//   REC-KEY-*       `press "ext:<name>"` in the engine
//
// Every row builds its own machine; files go to one per-run temp directory.
//
// Run: ./build/test/script_record_test

#include "script/evaluator.h"
#include "script/key_names.h"
#include "script/recorder.h"
#include "script/script_engine.h"
#include "script/script_host.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "platform/emulator_boot.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "../row_id.h"

using namespace jnext::script;
using jnext::dbg::ClientId;
using jnext::dbg::Debugger;
using jnext::dbg::EventKind;
using jnext::dbg::InputState;

// ── Tiny test harness (matches script_events_test) ─────────────────────────

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
        std::printf("  FAIL %s: %s%s%s\n", id, desc, detail.empty() ? "" : " — ", detail.c_str());
    }
}

static std::string g_dir;   // the per-run temp directory, with a trailing '/'

static std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::vector<uint8_t> read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static bool has(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

static size_t count(const std::string& text, const std::string& needle) {
    size_t n = 0;
    for (size_t p = text.find(needle); p != std::string::npos; p = text.find(needle, p + 1)) ++n;
    return n;
}

/// The script's rule lines (no comments), joined with '|'.
static std::string rules_of(const std::string& text) {
    std::string out;
    size_t a = 0;
    while (a < text.size()) {
        size_t b = text.find('\n', a);
        if (b == std::string::npos) b = text.size();
        const std::string line = text.substr(a, b - a);
        if (!line.empty() && line[0] != '#') out += line + "|";
        a = b + 1;
    }
    return out;
}

// ── The rig ────────────────────────────────────────────────────────────────

// The program the round trip replays. Two 16-bit counters in SCREEN memory,
// so a `.scr` capture sees them:
//   0x4000  loop passes before Q is first seen down — moves with the FRAME
//           the press lands on;
//   0x4002  loop passes while Q is down — moves with how long it was held.
//
//   8000 01 FE FB    LD BC,0xFBFE      ; row Q W E R T
//   8003 ED 78       IN A,(C)
//   8005 1F          RRA               ; Q -> carry (0 = down)
//   8006 38 0E       JR C,up
//   8008 2A 02 40    LD HL,(0x4002)
//   800B 23          INC HL
//   800C 22 02 40    LD (0x4002),HL
//   800F 3E 01       LD A,1
//   8011 32 04 40    LD (0x4004),A     ; Q has been seen
//   8014 18 EA       JR 0x8000
//   8016 3A 04 40 up:LD A,(0x4004)
//   8019 B7          OR A
//   801A 20 E4       JR NZ,0x8000
//   801C 2A 00 40    LD HL,(0x4000)
//   801F 23          INC HL
//   8020 22 00 40    LD (0x4000),HL
//   8023 18 DB       JR 0x8000
static const std::vector<uint8_t> kCounter = {
    0x01, 0xFE, 0xFB, 0xED, 0x78, 0x1F, 0x38, 0x0E, 0x2A, 0x02, 0x40, 0x23, 0x22, 0x02, 0x40, 0x3E, 0x01, 0x32,
    0x04, 0x40, 0x18, 0xEA, 0x3A, 0x04, 0x40, 0xB7, 0x20, 0xE4, 0x2A, 0x00, 0x40, 0x23, 0x22, 0x00, 0x40, 0x18, 0xDB};

static const std::vector<uint8_t> kPark = {0x18, 0xFE};   // JR $

static constexpr uint16_t PROG = 0x8000;

struct Sink : jnext::dbg::Listener {
    std::vector<std::string> lines;
    void on_paused(const jnext::dbg::PausedInfo&) override {}
    void on_resumed(ClientId) override {}
    void on_reset(jnext::dbg::ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(jnext::dbg::LogLevel, const std::string& t) override { lines.push_back(t); }
    size_t count(const std::string& needle) const {
        size_t n = 0;
        for (const auto& l : lines)
            if (l.find(needle) != std::string::npos) ++n;
        return n;
    }
};

struct Rig {
    Emulator                  emu;
    std::unique_ptr<Debugger> dbg;
    Sink                      sink;
    ClientId                  tc = jnext::dbg::CLIENT_NONE;
    std::vector<uint8_t>      prog;
    /// input_state() at every frame edge, by frame — what the guest sees from
    /// the NEXT frame on.
    std::vector<std::pair<uint32_t, InputState>> trace;

    explicit Rig(const std::vector<uint8_t>& p = kPark, MachineType type = MachineType::ZX48K) : prog(p) {
        EmulatorConfig cfg;
        cfg.type = type;
        emu.init(cfg);
        install();
        dbg = std::make_unique<Debugger>(emu);
        jnext::dbg::ClientInfo ci;
        ci.name = "script_record_test";
        ci.kind = jnext::dbg::ClientKind::Test;
        tc = dbg->attach(ci).value;
        dbg->set_listener(tc, &sink);
        jnext::dbg::Subscription s;
        s.kind    = EventKind::Frame;
        s.action  = jnext::dbg::Action::Continue;
        s.handler = [this](const jnext::dbg::Event& e, Debugger& d) {
            trace.emplace_back(e.frame, d.input_state());
            return jnext::dbg::Action::Continue;
        };
        dbg->subscribe(tc, s);
        dbg->pump(jnext::dbg::PumpBudget{});
    }
    ~Rig() {
        dbg->set_listener(tc, nullptr);
        dbg->detach(tc);
        dbg.reset();
    }
    void install() {
        for (size_t i = 0; i < prog.size(); ++i) emu.mmu().write(static_cast<uint16_t>(PROG + i), prog[i]);
        Z80Registers r = emu.cpu().get_registers();
        r.PC   = PROG;
        r.SP   = 0xFF00;
        r.IFF1 = 0;
        r.IFF2 = 0;
        emu.cpu().set_registers(r);
    }
    void frames(int n) {
        for (int i = 0; i < n && !dbg->state().paused; ++i) {
            emu.run_frame();
            dbg->pump(jnext::dbg::PumpBudget{});
        }
    }
    /// What the GUI does: a host key between frames (Keyboard::set_key, which
    /// the key Router forwards to).
    void key(SDL_Scancode sc, bool down) { emu.keyboard().set_key(sc, down); }
    /// The frame the NEXT run_frame() runs. Between frames `time().frame` is
    /// the tag of the one that just ran (it is the pre-increment tag, §2.4).
    uint32_t next() const { return dbg->time().frame + 1; }
    uint16_t word(uint16_t a) {
        uint8_t b[2] = {};
        dbg->peek(jnext::dbg::MemSpace::cpu(), a, 2, b);
        return static_cast<uint16_t>(b[0] | (b[1] << 8));
    }
};

static RecordingInfo test_info() {
    RecordingInfo i;
    i.load_file = "game.nex";
    i.rtc       = "2026-01-01 00:00:00";
    i.sd_image  = "/some/where/card.img";
    i.sd_id     = "size=1 vol-id=abcd";
    return i;
}

// =========================================================================
// NAMES
// =========================================================================

static void names_rows() {
    {
        // Every matrix bit: the name written reads back as exactly that bit.
        int bad = 0;
        std::string detail;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 5; ++c) {
                const std::string n = matrix_bit_name(r, c);
                jnext::dbg::MatrixKey k;
                bool ok;
                if (n.size() == 3 && n[1] == ',') {
                    ok = n[0] - '0' == r && n[2] - '0' == c;
                } else {
                    ok = jnext::dbg::key_name_to_matrix(n, k) && !k.compound() && k.row1 == r && k.col1 == c;
                }
                if (!ok) {
                    ++bad;
                    detail += "(" + std::to_string(r) + "," + std::to_string(c) + ")=" + n + " ";
                }
            }
        check("REC-NAMES-MATRIX", "each of the 40 matrix bits is written as a name `press` reads back as that "
                                  "one bit; CAPS SHIFT and SYMBOL SHIFT as `0,0` / `7,1`",
              bad == 0 && matrix_bit_name(0, 0) == "0,0" && matrix_bit_name(7, 1) == "7,1" &&
                  matrix_bit_name(8, 0).empty() && matrix_bit_name(0, 5).empty(),
              detail);
    }
    {
        int bad = 0;
        for (int id = 0; id < 16; ++id)
            if (ext_key_id(ext_key_name(id)) != id) ++bad;
        check("REC-NAMES-EXT", "the 16 extended keys: `ext:<name>` round-trips to its id, any case; "
                               "an unknown name and a non-ext name are -1",
              bad == 0 && ext_key_name(0) == "ext:right" && ext_key_name(13) == "ext:break" &&
                  ext_key_id("EXT:Delete") == 15 && ext_key_id("ext:nope") == -1 && ext_key_id("q") == -1 &&
                  ext_key_name(16).empty(),
              std::to_string(bad) + " bad");
    }
}

// =========================================================================
// EDGES
// =========================================================================

static void edge_rows() {
    {
        Rig g;
        Recorder rec(*g.dbg);
        std::string why;
        const bool started = rec.start(g_dir + "edge.jds", nullptr, why);
        g.frames(10);
        const uint32_t k = g.next();   // the frame about to run
        g.key(SDL_SCANCODE_Q, true);
        g.frames(5);
        const uint32_t r = g.next();
        g.key(SDL_SCANCODE_Q, false);
        g.frames(5);
        const std::string s = rules_of(rec.script());
        // First seen at E_k, applied between E_k-1 and frame k: stamped k-1.
        const std::string want = "on frame " + std::to_string(k - 1) + " do press \"q\" end|on frame " +
                                 std::to_string(r - 1) + " do release \"q\" end|";
        check("REC-EDGE-PRESS", "a host key down between frames is ONE `press` stamped at the frame before "
                                "the first one that saw it, its release ONE `release` the same way, and an "
                                "unchanged frame writes nothing",
              started && has(s, want) && count(s, "press") == 1 && count(s, "release") == 1 && rec.edges() == 2,
              s);
    }
    {
        // Overlapping holds stay overlapping: level edges, not pulses.
        Rig g;
        Recorder rec(*g.dbg);
        std::string why;
        rec.start(g_dir + "overlap.jds", nullptr, why);
        g.frames(3);
        const uint32_t a = g.next();
        g.key(SDL_SCANCODE_W, true);
        g.frames(2);
        const uint32_t b = g.next();
        g.key(SDL_SCANCODE_LSHIFT, true);   // CAPS SHIFT
        g.frames(2);
        const uint32_t c = g.next();
        g.key(SDL_SCANCODE_W, false);
        g.frames(2);
        const uint32_t d = g.next();
        g.key(SDL_SCANCODE_LSHIFT, false);
        g.frames(2);
        const std::string s = rules_of(rec.script());
        auto f = [](uint32_t n) { return "on frame " + std::to_string(n - 1) + " do "; };
        check("REC-EDGE-OVERLAP", "W held across CAPS SHIFT: press w, press 0,0, release w, release 0,0 — "
                                  "each at its own frame, never a `for` pulse",
              has(s, f(a) + "press \"w\" end|" + f(b) + "press \"0,0\" end|" + f(c) + "release \"w\" end|" + f(d) +
                         "release \"0,0\" end|") &&
                  !has(s, " for "),
              s);
    }
    {
        // A compound host key (the PC arrow LEFT in 48K is CAPS + 5 — or an
        // extended key: whichever the keyboard sets) is recorded bit by bit.
        Rig g;
        Recorder rec(*g.dbg);
        std::string why;
        rec.start(g_dir + "ext.jds", nullptr, why);
        g.frames(3);
        const uint32_t a = g.next();
        g.key(SDL_SCANCODE_UP, true);
        g.frames(3);
        const uint32_t b = g.next();
        g.key(SDL_SCANCODE_UP, false);
        g.frames(3);
        const std::string s = rules_of(rec.script());
        check("REC-EDGE-EXT", "the host's Up arrow drives the extended key UP (NR 0xB0 bit 3): recorded as "
                              "`press \"ext:up\"` / `release \"ext:up\"` at its frames, no matrix bit",
              has(s, "on frame " + std::to_string(a - 1) + " do press \"ext:up\" end|") &&
                  has(s, "on frame " + std::to_string(b - 1) + " do release \"ext:up\" end|") &&
                  count(s, "press") == 1,
              s);
    }
    {
        Rig g;
        Recorder rec(*g.dbg);
        std::string why;
        rec.start(g_dir + "joy.jds", nullptr, why);
        g.frames(3);
        const uint32_t a = g.next();
        g.emu.joystick().set_joy_left(0x011);
        g.frames(2);
        const uint32_t b = g.next();
        g.emu.joystick().set_joy_right(0x800);
        g.emu.joystick().set_joy_left(0x000);
        g.frames(2);
        const std::string s = rules_of(rec.script());
        check("REC-EDGE-JOY", "a connector's 12-bit state is ONE `joystick 1` / `joystick 2` line per change, "
                              "with the whole new state; both in one frame are two lines",
              has(s, "on frame " + std::to_string(a - 1) + " do joystick 1 0x011 end|") &&
                  has(s, "on frame " + std::to_string(b - 1) + " do joystick 1 0x000 end|") &&
                  has(s, "on frame " + std::to_string(b - 1) + " do joystick 2 0x800 end|") &&
                  count(s, "do joystick ") == 3,
              s);
    }
}

// =========================================================================
// CAPTURES
// =========================================================================

static void capture_rows() {
    {
        Rig g(kCounter);
        Recorder rec(*g.dbg);
        std::string why;
        rec.start(g_dir + "cap.jds", nullptr, why);
        g.frames(3);
        const uint32_t k = g.next();    // the capture is taken at E_k
        const bool asked = rec.capture();
        g.frames(1);
        const std::vector<uint8_t> at_edge = g.dbg->ula_screen_dump();   // nothing runs after E_k
        const std::vector<uint8_t> file    = read_bytes(g_dir + "cap-0001.scr");
        g.frames(3);
        const std::vector<uint8_t> later = g.dbg->ula_screen_dump();
        const std::string s = rules_of(rec.script());
        check("REC-CAP-SCR", "with only the ULA on, a capture writes `<base>-0001.scr` = the screen memory "
                             "at the NEXT frame edge, and `on frame K do compare_scr` at that edge",
              asked && file == at_edge && file.size() == 6912 && later != at_edge &&
                  has(s, "on frame " + std::to_string(k) + " do compare_scr \"cap-0001.scr\" \"capture 1 at FRAME " +
                             std::to_string(k) + "\" end|") &&
                  rec.captures() == 1,
              s);
    }
    {
        // Each of the five non-ULA conditions on its own makes it a PNG.
        struct Case { uint8_t reg, val; const char* what; };
        const Case cases[] = {{0x68, 0x80, "ULA off"},    {0x15, 0x01, "sprites"}, {0x15, 0x80, "LoRes"},
                              {0x69, 0x80, "Layer 2"},    {0x6B, 0x80, "tilemap"}};
        std::string detail;
        int ok = 0;
        for (const Case& c : cases) {
            Rig g(kPark, MachineType::ZXN_ISSUE2);
            Recorder rec(*g.dbg);
            std::string why;
            const std::string name = std::string("png") + std::to_string(c.reg) + "_" + std::to_string(c.val);
            rec.start(g_dir + name + ".jds", nullptr, why);
            g.dbg->nextreg_write(g.tc, c.reg, c.val);
            g.frames(2);
            const uint32_t k = g.next();
            rec.capture();
            g.frames(1);
            const bool pending_file = !std::filesystem::exists(g_dir + name + "-0001.png");
            g.frames(2);   // the PNG is the NEXT rendered frame (CAP-01)
            const std::string s = rules_of(rec.script());
            const bool good =
                std::filesystem::exists(g_dir + name + "-0001.png") && !std::filesystem::exists(g_dir + name + "-0001.scr") &&
                has(s, "on frame " + std::to_string(k) + " do screenshot \"" + name + "-0001-replay.png\" end") &&
                pending_file;
            if (good) ++ok;
            else detail += std::string(c.what) + ": " + s + " ";
        }
        check("REC-CAP-PNG", "with any non-ULA layer on — ULA disabled (NR 0x68 b7), sprites (NR 0x15 b0), "
                             "LoRes (NR 0x15 b7), Layer 2 (NR 0x69 b7) or the tilemap (NR 0x6B b7) — a capture "
                             "is a PNG one rendered frame later and a `screenshot` of a `-replay.png`",
              ok == 5, detail);
    }
    {
        Rig g(kCounter);
        Recorder rec(*g.dbg);
        std::string why;
        rec.start(g_dir + "hk.jds", nullptr, why);
        g.frames(2);
        g.dbg->raise_host_event(g.tc, "script7");
        g.frames(2);
        const unsigned after7 = rec.captures();
        g.dbg->raise_host_event(g.tc, "script8");
        g.frames(2);
        check("REC-CAP-HOSTKEY", "host key 8 (Alt+8, `--script-key F 8`) captures while recording; host key 7 "
                                 "does not",
              after7 == 0 && rec.captures() == 1 && std::filesystem::exists(g_dir + "hk-0001.scr"),
              "after7=" + std::to_string(after7) + " captures=" + std::to_string(rec.captures()));
    }
}

// =========================================================================
// HEADER, WARNINGS, LIFE
// =========================================================================

static void header_rows() {
    {
        Rig g(kPark, MachineType::ZXN_ISSUE2);
        Recorder rec(*g.dbg);
        std::string why;
        rec.start(g_dir + "hdr.jds", test_info, why);
        g.frames(1);
        const uint32_t first = g.trace.back().first;
        const uint8_t  nr05  = g.dbg->nextreg_peek(0x05);
        g.frames(4);
        const uint32_t last = g.trace.back().first;
        char nr[8];
        std::snprintf(nr, sizeof nr, "0x%02X", nr05);
        const std::string t = rec.script();
        const std::string at = "on frame " + std::to_string(first) + " once do ";
        check("REC-HDR-FIELDS", "the header: `jds-recorder: 1`, the machine, the program, the RTC, the SD card "
                                "(base name and identity), NR 0x05, the range, and the replay command line",
              has(t, "# jds-recorder: 1\n") &&
                  has(t, "# machine=next load=game.nex rtc=2026-01-01 00:00:00 sd=card.img (size=1 vol-id=abcd)\n") &&
                  has(t, std::string("# joystick: nr05=") + nr + "\n") &&
                  has(t, "# recorded: FRAME " + std::to_string(first) + ".." + std::to_string(last)) &&
                  has(t, "jnext --headless --machine next --rtc \"2026-01-01 00:00:00\" --load game.nex --script "
                         "hdr.jds --delayed-automatic-exit-frames " + std::to_string(last + 2 + 50)) &&
                  !has(t, "/some/where"),
              t);
        check("REC-HDR-ASSERTS", "the preconditions are asserts at the first recorded frame — MACHINE (4 on a "
                                 "Next) and NR 0x05 as read — and the script ends with `exit 0` two frames "
                                 "after the last recorded one",
              has(t, at + "assert MACHINE == 4 \"recorded on MACHINE 4\" end\n") &&
                  has(t, at + "assert nextreg[0x05] == " + nr + " \"joystick mode NR 0x05 = " + nr +
                             ", as recorded\" end\n") &&
                  has(t, "on frame " + std::to_string(last + 2) + " do exit 0 end\n"),
              t);
    }
    {
        Rig g;   // 48K, no info provider
        Recorder rec(*g.dbg);
        std::string why;
        rec.start(g_dir + "hdr48.jds", nullptr, why);
        g.frames(2);
        const std::string t = rec.script();
        check("REC-HDR-48K", "on a 48K with nothing loaded: `machine=48k load=none rtc=live sd=none`, MACHINE "
                             "== 0, and no --load / --rtc in the replay line",
              has(t, "# machine=48k load=none rtc=live sd=none\n") && has(t, "assert MACHINE == 0 ") &&
                  has(t, "jnext --headless --machine 48k --script hdr48.jds ") && !has(t, "--load") && !has(t, "--rtc"),
              t);
    }
}

static void warn_rows() {
    {
        // A key that is down when the recording starts.
        Rig g;
        g.key(SDL_SCANCODE_A, true);
        g.frames(2);
        Recorder rec(*g.dbg);
        std::string why;
        rec.start(g_dir + "held.jds", nullptr, why);
        g.frames(2);
        const std::string t = rec.script();
        check("REC-WARN-HELD", "input already held when the recording began is replayed from the frame before "
                               "the first recorded one, and a WARNING says so (comment + `log` line)",
              has(t, "do press \"a\" end") && has(t, "# WARNING: input already held when the recording began") &&
                  has(t, "log \"WARNING: input already held when the recording began") &&
                  g.sink.count("RECORD: WARNING: input already held") == 1,
              t);
    }
    {
        // §7.2 item 4: a key changed while paused MID-FRAME lands inside the
        // frame; no `on frame` edge can put it there.
        Rig g(kCounter);
        Recorder rec(*g.dbg);
        std::string why;
        rec.start(g_dir + "mid.jds", nullptr, why);
        g.frames(2);
        const uint32_t k = g.next();
        jnext::dbg::Subscription bp;
        bp.kind      = EventKind::Execute;
        bp.filter.lo = bp.filter.hi = 0x8003;   // on every pass of the loop
        bp.once      = true;
        bp.action    = jnext::dbg::Action::Stop;
        g.dbg->subscribe(g.tc, bp);
        g.emu.run_frame();                       // stops mid-frame k
        g.dbg->pump(jnext::dbg::PumpBudget{});   // the Paused push reaches the recorder
        const bool mid = g.dbg->state().paused && !g.dbg->at_frame_boundary();
        g.key(SDL_SCANCODE_Q, true);
        g.dbg->run(g.tc);
        g.dbg->pump(jnext::dbg::PumpBudget{});
        g.frames(3);
        const uint32_t clean = g.next();
        g.key(SDL_SCANCODE_Q, false);   // between frames: exact, no warning
        g.frames(2);
        const std::string t = rec.script();
        const std::string w = "input change recorded while paused mid-frame at FRAME " + std::to_string(k);
        check("REC-WARN-MIDFRAME", "a key changed while the machine was paused mid-frame K is recorded with a "
                                   "WARNING naming FRAME K (comment + `log`); a change between frames is not "
                                   "warned about",
              mid && has(t, "# WARNING: " + w) && has(t, "log \"WARNING: " + w) &&
                  !has(t, "paused mid-frame at FRAME " + std::to_string(clean)) &&
                  count(t, "# WARNING:") == 1,
              t);
    }
}

static void life_rows() {
    {
        Rig g;
        Recorder rec(*g.dbg);
        std::string why1, why2, why3, why4;
        const bool stop_idle = rec.stop(why1);
        const bool cap_idle  = rec.capture();
        const bool a         = rec.start(g_dir + "life.jds", nullptr, why2);
        const bool again     = rec.start(g_dir + "life2.jds", nullptr, why3);
        g.frames(3);
        const std::string expect = rec.script();
        const bool stopped = rec.stop(why4);
        const std::string written = read_text(g_dir + "life.jds");
        check("REC-LIFE-STARTSTOP", "stop() and capture() refuse when not recording; a second start() refuses "
                                    "while one runs; stop() writes exactly script() and ends the recording",
              !stop_idle && why1 == "not recording" && !cap_idle && a && !again &&
                  why3 == "already recording to " + g_dir + "life.jds" && stopped && written == expect &&
                  !rec.recording() && !std::filesystem::exists(g_dir + "life2.jds"),
              why1 + " | " + why3 + " | " + why4);
    }
    {
        Rig g;
        Recorder rec(*g.dbg);
        std::string why;
        rec.start(g_dir + "no/such/dir/x.jds", nullptr, why);
        g.frames(2);
        const bool ok = rec.stop(why);
        check("REC-LIFE-UNWRITABLE", "a script that cannot be written: stop() is false, says which file, and "
                                     "the recording has ended anyway",
              !ok && why == "cannot write " + g_dir + "no/such/dir/x.jds" && !rec.recording(), why);
    }
    {
        // A cold boot restarts the frame numbering and the recording with it;
        // the header's program is read again.
        Rig g;
        int asked = 0;
        Recorder rec(*g.dbg);
        std::string why;
        rec.start(g_dir + "boot.jds",
                  [&asked]() {
                      RecordingInfo i;
                      i.load_file = ++asked == 1 ? "before.tap" : "after.nex";
                      return i;
                  },
                  why);
        g.frames(3);
        g.key(SDL_SCANCODE_Q, true);
        g.frames(2);
        g.key(SDL_SCANCODE_Q, false);
        g.frames(2);
        const bool had = has(rec.script(), "press \"q\"");
        g.dbg->on_cold_boot_begin();
        emulator_frontend_cold_boot(g.emu, g.emu.config(), std::string(), ColdBootHooks{});
        g.dbg->on_cold_boot_done();
        g.install();
        g.frames(2);
        const std::string t = rec.script();
        check("REC-LIFE-COLDBOOT", "a cold boot during a recording restarts it at FRAME 0: what came before is "
                                   "dropped and the program is asked for again",
              had && !has(t, "press \"q\"") && has(t, "load=after.nex") && has(t, "# recorded: FRAME 0..") &&
                  asked == 2 && g.sink.count("RECORD: cold boot") == 1,
              t);
    }
}

// =========================================================================
// THE ROUND TRIP
// =========================================================================

struct Session {
    std::string                                   script;
    std::vector<std::pair<uint32_t, InputState>>  trace;
    uint16_t                                      before = 0, held = 0;
};

static bool same(const InputState& a, const InputState& b) {
    for (int r = 0; r < 8; ++r)
        if ((a.matrix[r] & 0x1F) != (b.matrix[r] & 0x1F)) return false;
    return a.ext_keys == b.ext_keys && a.joy_left12 == b.joy_left12 && a.joy_right12 == b.joy_right12;
}

// Record a session: Q pressed and held, an extended key and the joystick, three
// captures, driven from the HOST side between frames.
static Session record_session(const std::string& name) {
    Session out;
    Rig g(kCounter);
    Recorder rec(*g.dbg);
    std::string why;
    rec.start(g_dir + name + ".jds", nullptr, why);
    g.frames(7);
    g.key(SDL_SCANCODE_Q, true);
    g.frames(4);
    rec.capture();
    g.frames(3);
    g.key(SDL_SCANCODE_DOWN, true);
    g.emu.joystick().set_joy_left(0x003);
    g.frames(2);
    g.key(SDL_SCANCODE_Q, false);
    g.frames(1);
    g.key(SDL_SCANCODE_DOWN, false);
    g.emu.joystick().set_joy_left(0x000);
    g.frames(2);
    rec.capture();
    g.frames(4);
    rec.capture();
    g.frames(2);
    rec.stop(why);
    out.script = read_text(g_dir + name + ".jds");
    out.trace  = g.trace;
    out.before = g.word(0x4000);
    out.held   = g.word(0x4002);
    return out;
}

static void roundtrip_rows() {
    const Session rec = record_session("rt");

    // Replay on a FRESH machine, from the recording's directory (the captures
    // are named relative to it), as `jnext --headless --script` would.
    Rig g(kCounter);
    EngineHost h;
    int         exit_code = -1;
    h.exit = [&exit_code](int c) {
        if (exit_code < 0) exit_code = c;
    };
    ScriptEngine eng(*g.dbg, h);
    char cwd[4096] = {};
    const bool have_cwd = getcwd(cwd, sizeof cwd) != nullptr;
    const bool in_dir   = chdir(g_dir.c_str()) == 0;
    const LoadResult lr = eng.load(rec.script, "rt.jds");
    g.dbg->pump(jnext::dbg::PumpBudget{});
    g.frames(60);
    if (have_cwd && chdir(cwd) != 0) std::printf("  (could not return to %s)\n", cwd);
    const ScriptEngine::Status st = eng.status();

    check("REC-RT-LOADS", "the emitted script loads with no error",
          in_dir && lr.ok() && count(rec.script, "compare_scr") == 3, lr.errors.empty() ? rec.script : lr.errors[0].to_string());
    // Input, frame by frame, over the recorded range.
    size_t compared = 0, differ = 0;
    std::string where;
    for (const auto& [f, s] : rec.trace)
        for (const auto& [f2, s2] : g.trace)
            if (f2 == f) {
                ++compared;
                if (!same(s, s2)) {
                    ++differ;
                    if (where.empty()) where = "first at FRAME " + std::to_string(f);
                }
            }
    check("REC-RT-INPUT", "the replay presents the guest the SAME input state at every frame edge of the "
                          "recording — matrix, extended keys, joystick — level for level, frame for frame",
          compared == rec.trace.size() && compared > 20 && differ == 0,
          std::to_string(compared) + " compared, " + std::to_string(differ) + " differ " + where);
    check("REC-RT-GUEST", "and the guest computed the same: the passes before Q was first seen and while it "
                          "was held — numbers that move with the frame a key lands on and how long it stays",
          g.word(0x4000) == rec.before && g.word(0x4002) == rec.held && rec.before > 0 && rec.held > 0,
          "recorded " + std::to_string(rec.before) + "/" + std::to_string(rec.held) + " replayed " +
              std::to_string(g.word(0x4000)) + "/" + std::to_string(g.word(0x4002)));
    check("REC-RT-VERDICT", "every compare_scr matched and the script reached its `exit 0`: no stop, no "
                            "run-time error",
          exit_code == 0 && st.exit_code && *st.exit_code == 0 && st.stops == 0 && st.runtime_errors == 0,
          "exit=" + std::to_string(exit_code) + " stops=" + std::to_string(st.stops) + " " + st.last_stop);

    {
        // The control: one byte of one capture changed, and the replay fails
        // on it — the compare_scr lines are live.
        std::vector<uint8_t> b = read_bytes(g_dir + "rt-0002.scr");
        b[2] ^= 0xFF;
        {
            std::ofstream out(g_dir + "rt-0002.scr", std::ios::binary);
            out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        }
        Rig r(kCounter);
        EngineHost h2;
        ScriptEngine e2(*r.dbg, h2);
        const bool in = chdir(g_dir.c_str()) == 0;
        e2.load(rec.script, "rt.jds");
        r.dbg->pump(jnext::dbg::PumpBudget{});
        r.frames(60);
        if (have_cwd && chdir(cwd) != 0) std::printf("  (could not return to %s)\n", cwd);
        const ScriptEngine::Status s2 = e2.status();
        check("REC-RT-CONTROL", "the control: the same replay against a capture with one byte changed stops "
                                "on that capture's compare_scr",
              in && s2.stops == 1 && s2.last_stop.find("capture 2 at FRAME") == 0,
              "stops=" + std::to_string(s2.stops) + " " + s2.last_stop);
    }
}

// =========================================================================
// THE HOST, AND `press "ext:…"`
// =========================================================================

static void host_rows() {
    {
        Rig g;
        std::string text;
        {
            ScriptHost host;
            host.set_recording_info(test_info);
            ScriptHostOptions o;
            o.record_file = g_dir + "host.jds";
            o.keys        = {{3, 8}};   // a capture key with no --script
            o.exits       = true;
            const bool started = host.start(*g.dbg, o);
            g.frames(8);
            const bool rec = host.recording();
            std::vector<std::string> log = host.log_since(0);
            std::string all;
            for (const auto& l : log) all += l + "|";
            check("REC-HOST-START", "`--record-script` records from start(); `--script-key F 8` needs no "
                                    "--script then and captures; the recorder's lines reach the Script tab's log",
                  started && rec && host.recorder() && host.recorder()->captures() == 1 && has(all, "RECORD: capture 1"),
                  all);
        }   // the host goes: the script is written
        text = read_text(g_dir + "host.jds");
        check("REC-HOST-EXIT", "the host going (jnext exiting) stops the recording and writes the script",
              has(text, "# jds-recorder: 1") && has(text, "load=game.nex") && has(text, "compare_scr \"host-0001.scr\""),
              text.substr(0, 200));
    }
    {
        Rig g;
        ScriptHost host;
        ScriptHostOptions o;
        o.keys = {{3, 8}};
        const bool started = host.start(*g.dbg, o);
        const bool cap     = host.capture_screen();
        const bool stop    = host.stop_recording();
        check("REC-HOST-NOREC", "without --record-script a --script-key still needs a --script, and capture / "
                                "stop do nothing",
              !started && !cap && !stop && !host.recording(), "");
    }
    {
        Rig g;
        ScriptHost host;
        ScriptHostOptions o;
        host.start(*g.dbg, o);
        const bool a  = host.start_recording(g_dir + "gui.jds");
        const bool a2 = host.start_recording(g_dir + "gui2.jds");
        g.frames(2);
        const bool c  = host.capture_screen();
        g.frames(2);
        const bool s  = host.stop_recording();
        std::string all;
        for (const auto& l : host.log_since(0)) all += l + "|";
        check("REC-HOST-GUI", "the GUI's verbs: start_recording, a second start refused and logged, "
                              "capture_screen, stop_recording logs what it wrote",
              a && !a2 && c && s && !host.recording() && std::filesystem::exists(g_dir + "gui.jds") &&
                  has(all, "RECORD " + g_dir + "gui2.jds: already recording to " + g_dir + "gui.jds") &&
                  has(all, "RECORD: wrote " + g_dir + "gui.jds — 0 input edges, 1 captures"),
              all);
    }
}

static void key_rows() {
    {
        Rig g;
        EngineHost h;
        ScriptEngine eng(*g.dbg, h);
        const bool ok = eng.load("on frame 2 do press \"ext:up\" end\non frame 5 do release \"EXT:UP\" end\n", "e.jds").ok();
        g.frames(4);
        const uint16_t down = g.dbg->input_state().ext_keys;
        g.frames(3);
        const uint16_t up = g.dbg->input_state().ext_keys;
        check("REC-KEY-EXT", "`press \"ext:up\"` / `release \"ext:up\"` drive the extended key UP (NR 0xB0 bit 3) "
                             "at the frame edge, any case",
              ok && down == 0x0008 && up == 0x0000,
              "down=" + std::to_string(down) + " up=" + std::to_string(up));
    }
    {
        Rig g;
        EngineHost h;
        ScriptEngine eng(*g.dbg, h);
        eng.load("on frame 1 do press \"ext:up\" for 3 end\non frame 1 do press \"ext:nope\" end\n", "e2.jds");
        g.frames(3);
        const ScriptEngine::Status st = eng.status();
        check("REC-KEY-EXT-ERRORS", "`press \"ext:…\" for n` (no pulse form for an extended key) and an unknown "
                                    "`ext:` name are run-time errors, and press nothing",
              st.runtime_errors == 2 && g.dbg->input_state().ext_keys == 0, std::to_string(st.runtime_errors));
    }
}

int main() {
    std::printf("script_record_test — the recorder, and the round trip it makes (GH #26 WP6 / #20)\n");
    char tmpl[] = "/tmp/jnext_srec_XXXXXX";
    if (!mkdtemp(tmpl)) {
        std::printf("cannot make a temp directory\n");
        return 1;
    }
    g_dir = std::string(tmpl) + "/";

    auto run_group = [](const char* name, void (*fn)()) {
        try {
            fn();
        } catch (const EvalError& e) {
            ++g_fail;
            std::printf("  FAIL (group %s aborted): unexpected EvalError %s\n", name, e.d.to_string().c_str());
        }
    };
    run_group("names", names_rows);
    run_group("edges", edge_rows);
    run_group("captures", capture_rows);
    run_group("header", header_rows);
    run_group("warn", warn_rows);
    run_group("life", life_rows);
    run_group("roundtrip", roundtrip_rows);
    run_group("host", host_rows);
    run_group("keys", key_rows);

    std::error_code ec;
    std::filesystem::remove_all(tmpl, ec);

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
