// Source-level debugging through the debugger backend — CAP-SRC
// (doc/design/SOURCE-LEVEL-DEBUGGING.md):
//
//   SRCDBG-LOAD-*   load_source_map(): identity refused / accepted, the store
//   SRCDBG-LOC-*    source_location() follows the physical page mapped now
//   SRCDBG-SIDE-*   load_program_sidecars(): what is attached, kept, cleared
//   SRCDBG-STEP-*   source_step() forwards: Into / Over / Out, the stops
//   SRCDBG-BACK-*   source_step() backwards: Back / ReverseContinue
//   SRCDBG-CS-*     the call-stack tracker records each frame's pages and ends its frames
//                    (the return-address rule, the esxDOS shim's return: GH #345)
//
// A real 48K machine and a real `Debugger`, as debugger_backend_test drives
// them; no ROM. The program (interrupts off), with the source map's lines:
//
//   8000  00           NOP          main.bas:10
//   8001  00           NOP          (inside main.bas:10's range: same statement)
//   8002  CD 00 90     CALL SUB     main.bas:11
//   8005  00           NOP          main.bas:12
//   8006  C3 00 80     JP $8000     main.bas:13
//   9000  32 00 A0     LD ($A000),A sub.bas:1
//   9003  00           NOP          sub.bas:2
//   9004  C9           RET          (inside sub.bas:2's range)
//
// Every row fails on a tree without the feature: there is no source map,
// source_step() or load_program_sidecars() to call.
//
// Rows added for the review of the first port (each fails on that tree):
//   SRCDBG-ID-*      a page names RAM only when RAM supplies the fetch: ROM
//                    slots and overlays are not RAM pages, now and in history,
//                    and a source breakpoint's page qualifier says so
//   SRCDBG-HALT-*    a step ends at DI:HALT and within its frame budget
//   SRCDBG-GATE-*    between instructions a step delivers Execute events
//   SRCDBG-OWN-*     store ownership is explicit and changes only on success
//   SRCDBG-TAPE-*    a tape program's maps attach at once; one with an identity is judged
//                    where the CPU first runs its code, space by space (GH #344 stage 3)
//                    (GH #360: the verdict lines name a page as the views do, `P04`, not `page 4`;
//                    TAPE-15 and TAPE-18 assert the new spelling)
//   SRCDBG-HIST-*    sidecars are dropped when the machine goes back before
//                    the load they came with
//   SRCDBG-PATH-*    a relative map path is made absolute
//   SRCDBG-FB-*      Back after a Frame Back goes back in time
//   SRCDBG-SYM-*     the labels of an SLD reach the backend's symbol table, by page
//                    (GH #375 stage 2), through the sidecar and the hand load
//   SRCDBG-Z88-*     a z88dk -debug map: the <stem>.map sidecar (symbols and source lines),
//                    the hand load, what the sidecar precedence keeps, identity from the
//                    section binaries, the lifetime of a z88dk map, real z88dk code (GH #344)
//   SRCSTEP-*        C steps on real z88dk code: Into/Over/Out across banked_call and the
//                    -debug helpers, a tail JP, the esxDOS shim (GH #345)
//   SRCDOT-*         a z88dk dot command's map, which NextZXOS runs from DivMMC RAM bank 2: a source
//                    breakpoint and the steps inside it, the same bytes in RAM or ROM, the judgement
//                    at the entry, the hand load, landings, dotx and a rewind (GH #351)
//   SRCDBG-UANN-*    the user's labels and comments: the two layers and the one reader, the name rule, the
//                    project file through the backend, no lifetime, no side effect (GH #387)
//   SRCLIFE-*        the source map's lifetime: load generations, the attachment
//                    cache, stale user maps, Back stops at a landing (GH #342)
//
// Rows that need POSIX file types as sidecars (a symlink, a FIFO) are in
// source_debug_posix_test.
//
// Run: ./build/test/source_debug_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/call_stack.h"
#include "debug/debugger.h"
#include "debug/event_table.h"
#include "debug/ram_page.h"
#include "peripheral/divmmc.h"
#include "platform/emulator_boot.h"
#include "debug/sld_loader.h"
#include "debug/source_file.h"
#include "debug/variable_store.h"
#include "core/saveable.h"
#include "core/log.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <memory>
#include <string>
#include <initializer_list>
#include <unistd.h>
#include <utility>
#include "../row_id.h"

namespace fs = std::filesystem;
using jnext::dbg::Access;
using jnext::dbg::Action;
using jnext::dbg::ClientId;
using jnext::dbg::Debugger;
using jnext::dbg::EventKind;
using jnext::dbg::MemSpace;
using jnext::dbg::PauseReason;
using jnext::dbg::ResetKind;
using jnext::dbg::Result;
using jnext::dbg::Subscription;
using Step = Debugger::SourceStep;

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

static void check(const char* id, const char* desc, bool cond,
                  const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s%s%s\n", id, desc, detail.empty() ? "" : " - ",
                    detail.c_str());
    }
}

static std::string hex(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof(b), "$%04X", v);
    return b;
}

static void write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

static const char* const kNextDevice =
    "|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
    "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n";

// The SLD for the program above, on the pages the 48K machine maps at $8000
// and $9000. `extra` is appended verbatim.
static std::string program_sld(unsigned page80, unsigned page90, const std::string& extra = {}) {
    const std::string p80 = std::to_string(page80);
    const std::string p90 = std::to_string(page90);
    return std::string("|SLD.data.version|1\n") + "main.bas|10||0" + kNextDevice +
           "main.bas|10||0|" + p80 + "|32768|T|\n" +
           "main.bas|11||0|" + p80 + "|32770|T|\n" +
           "main.bas|12||0|" + p80 + "|32773|T|\n" +
           "main.bas|13||0|" + p80 + "|32774|T|\n" +
           "sub.bas|1||0|" + p90 + "|36864|T|\n" +
           "sub.bas|2||0|" + p90 + "|36867|T|\n" + extra;
}

// program_sld() without the given statement's record (`lines` are main.bas
// line numbers): under ranges the previous record would cover its address.
static std::string program_sld_without(unsigned page80, unsigned page90,
                                       std::initializer_list<int> lines) {
    std::string text = program_sld(page80, page90);
    for (const int line : lines) {
        const size_t at = text.find("main.bas|" + std::to_string(line) + "||0|" +
                                    std::to_string(page80) + "|");
        text.erase(at, text.find('\n', at) + 1 - at);
    }
    return text;
}

// A one-file SLD of `x.asm` records (position, address) on page `page`.
static std::string x_sld(unsigned page,
                         std::initializer_list<std::pair<const char*, unsigned>> records) {
    std::string text = std::string("|SLD.data.version|1\n") + "x.asm|1||0" + kNextDevice;
    for (const auto& [pos, addr] : records)
        text += std::string("x.asm|") + pos + "||0|" + std::to_string(page) + "|" +
                std::to_string(addr) + "|T|\n";
    return text;
}

// The debugger log lines `fn` writes, joined.
static std::string debugger_log_of(const std::function<void()>& fn) {
    auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(64);
    Log::debugger()->sinks().push_back(ring);
    fn();
    Log::debugger()->sinks().pop_back();
    std::string all;
    for (const auto& l : ring->last_formatted()) all += l;
    return all;
}

static bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}


struct Machine {
    Emulator emu;
    std::unique_ptr<Debugger> dbg;
    ClientId a = jnext::dbg::CLIENT_NONE;

    explicit Machine(bool rewind = false, MachineType type = MachineType::ZX48K) {
        EmulatorConfig cfg;
        cfg.type = type;
        emu.init(cfg);
        const uint8_t main_prog[] = {0x00, 0x00, 0xCD, 0x00, 0x90, 0x00, 0xC3, 0x00, 0x80};
        for (size_t i = 0; i < sizeof(main_prog); ++i)
            emu.mmu().write(static_cast<uint16_t>(0x8000 + i), main_prog[i]);
        const uint8_t sub[] = {0x32, 0x00, 0xA0, 0x00, 0xC9};
        for (size_t i = 0; i < sizeof(sub); ++i)
            emu.mmu().write(static_cast<uint16_t>(0x9000 + i), sub[i]);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0x8000;
        r.SP = 0xFF00;
        r.IFF1 = r.IFF2 = 0;
        emu.cpu().set_registers(r);
        if (rewind) {
            emu.set_rewind_enabled(true);
            emu.resize_rewind_buffer(8);
        }
        dbg = std::make_unique<Debugger>(emu);
        jnext::dbg::ClientInfo ci;
        ci.name = "source";
        ci.kind = jnext::dbg::ClientKind::Test;
        a = dbg->attach(ci).value;
        dbg->set_call_stack_enabled(a, true);
        dbg->pause(a);
    }
    uint16_t pc() const { return dbg->registers().PC; }
    void set_pc(uint16_t pc) {
        Z80Registers r = emu.cpu().get_registers();
        r.PC = pc;
        emu.cpu().set_registers(r);
    }
    std::string where() const {
        const auto l = dbg->source_location();
        return "pc=" + hex(pc()) + (l ? " " + l->file + ":" + std::to_string(l->line) : "");
    }
};

static Subscription execute_stop(uint16_t addr) {
    Subscription s;
    s.kind = EventKind::Execute;
    s.filter.lo = s.filter.hi = addr;
    s.action = Action::Stop;
    return s;
}

// GH #339: one instruction's fetch, as predicted BEFORE it (the debugger's
// `source_page(pc)`) and as recorded BY it (`Mmu::last_m1()`).
struct FetchPair {
    uint16_t pc = 0;
    MemSpace predicted;
    MemSpace recorded;
    bool     fetched = false;   // false for an accepted NMI/INT: no opcode was fetched
};

static FetchPair step_pair(Machine& m) {
    FetchPair f;
    f.pc        = m.pc();
    f.predicted = m.dbg->source_page(f.pc);
    m.dbg->step_into(m.a);
    f.recorded  = m.emu.mmu().last_m1();
    f.fetched   = m.emu.cpu().fetched_opcode_last_execute();
    return f;
}

static std::string pair_str(const FetchPair& f) {
    return "pc=" + hex(f.pc) + " predicted=" + std::to_string(unsigned(f.predicted.kind)) + "/" +
           std::to_string(unsigned(f.predicted.index)) + " recorded=" +
           std::to_string(unsigned(f.recorded.kind)) + "/" + std::to_string(unsigned(f.recorded.index)) +
           (f.fetched ? "" : " (no opcode fetched)");
}

// ── GH #344: the z88dk fixtures ───────────────────────────────────────────────
static const fs::path kZ88Fixtures = JNEXT_Z88DK_FIXTURE_DIR;
static const fs::path kZ88NoDebug  = fs::path(JNEXT_ZDEBUGMAP_DIR) / "nodebug.map";

// Copy a z88dk fixture's `prog.nex` and `prog.map` (and, with `bins`, its section
// binaries `prog_<GROUP>.bin`) into `to`, which is created.
static void z88_copy(const char* fixture, const fs::path& to, bool bins = true) {
    fs::create_directories(to);
    for (const auto& e : fs::directory_iterator(kZ88Fixtures / fixture)) {
        const std::string n = e.path().filename().string();
        const bool is_bin = n.size() > 4 && n.compare(n.size() - 4, 4, ".bin") == 0;
        if (n == "prog.nex" || n == "prog.map" || (bins && is_bin))
            fs::copy_file(e.path(), to / n, fs::copy_options::overwrite_existing);
    }
}

// Also the fixture's `--list` listings (GH #348): every `*.lis` beside the map.
static void z88_copy_lis(const char* fixture, const fs::path& to) {
    z88_copy(fixture, to);
    for (const auto& e : fs::directory_iterator(kZ88Fixtures / fixture)) {
        const std::string n = e.path().filename().string();
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".lis") == 0)
            fs::copy_file(e.path(), to / n, fs::copy_options::overwrite_existing);
    }
}

// What a Listener hears: the backend's own log lines.
struct Z88Log : jnext::dbg::Listener {
    std::vector<std::string> lines;
    void on_paused(const jnext::dbg::PausedInfo&) override {}
    void on_resumed(ClientId) override {}
    void on_reset(ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(jnext::dbg::LogLevel, const std::string& t) override { lines.push_back(t); }
    bool has(const std::string& s) const {
        for (const auto& l : lines)
            if (l.find(s) != std::string::npos) return true;
        return false;
    }
};

// A Next machine with the fixture's program loaded the way the frontends load it.
static bool z88_load(Machine& m, const fs::path& nex) {
    return emulator_apply_load(m.emu, nex.string(), false);
}

static std::string line_of(const Debugger& d, uint16_t addr) {
    const auto* l = d.source_location(addr);
    return l ? l->file + ":" + std::to_string(l->line) : std::string("-");
}


// ── GH #344 stage 3: tape programs ───────────────────────────────────────────
// What the backend sees of a tape is a notification (`load_program_sidecars`) made
// while the program is NOT in memory, and bytes that arrive later. These rows do
// exactly that: attach, then `poke` the section binaries in, then run.
static std::vector<uint8_t> read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

// A fixture's prog.map and section binaries beside an empty `prog.tap` in `to`.
// `bins` false leaves the binaries out (a map that carries no identity).
static std::string tape_copy(const char* fixture, const fs::path& to, bool bins = true) {
    fs::create_directories(to);
    for (const auto& e : fs::directory_iterator(kZ88Fixtures / fixture)) {
        const std::string n = e.path().filename().string();
        const bool is_bin = n.size() > 4 && n.compare(n.size() - 4, 4, ".bin") == 0;
        if (n == "prog.map" || (bins && is_bin))
            fs::copy_file(e.path(), to / n, fs::copy_options::overwrite_existing);
    }
    write_text(to / "prog.tap", "");
    return (to / "prog.tap").string();
}

// The program parks at $6000 (`JR $`), outside every code range, until a row starts it.
static void park(Machine& m) {
    m.emu.mmu().write(0x6000, 0x18);
    m.emu.mmu().write(0x6001, 0xFE);
    m.set_pc(0x6000);
}

static void run_frames(Machine& m, int n) {
    m.dbg->run(m.a);
    for (int i = 0; i < n; ++i) m.emu.run_frame();
    m.dbg->pause(m.a);
}

// The tape "loads" a section binary into `space` (offset 0), one byte flipped when asked.
static void tape_bytes(Machine& m, MemSpace space, const fs::path& bin, long flip = -1,
                       uint16_t addr = 0) {
    std::vector<uint8_t> b = read_all(bin);
    if (flip >= 0) b[static_cast<size_t>(flip)] ^= 0xFF;
    m.dbg->poke(m.a, space, addr, b.size(), b.data());
}

// A machine with no client attached and nothing paused: what a headless run is.
struct Bare {
    Emulator emu;
    std::unique_ptr<Debugger> dbg;
    explicit Bare(MachineType type = MachineType::ZX48K) {
        EmulatorConfig cfg;
        cfg.type = type;
        emu.init(cfg);
        emu.mmu().write(0x6000, 0x18);
        emu.mmu().write(0x6001, 0xFE);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0x6000;
        r.SP = 0xFF00;
        r.IFF1 = r.IFF2 = 0;
        emu.cpu().set_registers(r);
        dbg = std::make_unique<Debugger>(emu);
    }
    void start(uint16_t pc) {
        Z80Registers r = emu.cpu().get_registers();
        r.PC = pc;
        emu.cpu().set_registers(r);
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) emu.run_frame();
    }
};

int main() {
    std::printf("Source-level debugging (backend)\n");
    const fs::path dir = fs::temp_directory_path() /
                         ("jnext-source-debug-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    // The pages behind $8000 and $9000 on this machine, read rather than
    // assumed: the map must name exactly what the MMU maps.
    unsigned p80 = 0, p90 = 0;
    {
        Machine m;
        p80 = m.dbg->effective_page(0x8000);
        p90 = m.dbg->effective_page(0x9000);
    }
    const fs::path sld = dir / "prog.sld";
    write_text(sld, program_sld(p80, p90));

    // ── load_source_map() ───────────────────────────────────────────────
    {
        Machine m;
        check("SRCDBG-LOAD-01", "a fresh backend has no source map and no location",
              m.dbg->source_map().empty() && !m.dbg->source_location());
        const auto r = m.dbg->load_source_map(sld.string(), false);
        check("SRCDBG-LOAD-02", "an SLD with no identity loads all six records",
              r.count == 6 && r.error.empty() && !r.identity.has_value());
        const auto bad = m.dbg->load_source_map((dir / "missing.sld").string(), false);
        check("SRCDBG-LOAD-03", "an unreadable file is refused with a reason and the "
                                "map is kept",
              bad.count == -1 && !bad.error.empty() && m.dbg->source_map().size() == 6);
        check("SRCDBG-LOAD-04", "clear_source_map() empties it",
              m.dbg->clear_source_map() == Result::Ok && m.dbg->source_map().empty());

        // Identity of the four bytes at $8000 (00 00 CD 00) — and of four
        // other bytes, which the program in memory does not match.
        const fs::path good = dir / "ident-good.sld";
        const fs::path wrong = dir / "ident-wrong.sld";
        const std::string head = "|SLD.data.version|1\n||program.name:prog\n";
        const std::string tail = "||program.org:32768\n||program.size:4\n";
        // SHA-256(00 00 CD 00) and SHA-256(01 02 03 04).
        write_text(good, head + "||program.sha256:"
                         "04bb9c5fbc1b0517a33504a5fbb7b9de7dd72a1de84987241364d2674495a823\n" +
                         tail + program_sld(p80, p90).substr(20));
        write_text(wrong, head + "||program.sha256:"
                          "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a\n" +
                          tail + program_sld(p80, p90).substr(20));
        const auto refused = m.dbg->load_source_map(wrong.string(), false);
        check("SRCDBG-LOAD-05", "an identity that does not match memory is refused, "
                                "and says so",
              refused.count == -1 && refused.identity && !*refused.identity &&
                  m.dbg->source_map().empty());
        const auto forced = m.dbg->load_source_map(wrong.string(), true);
        check("SRCDBG-LOAD-06", "... unless the caller accepts the mismatch",
              forced.count == 6 && forced.identity && !*forced.identity &&
                  m.dbg->source_map().size() == 6);
        m.dbg->clear_source_map();
        const auto verified = m.dbg->load_source_map(good.string(), false);
        check("SRCDBG-LOAD-07", "an identity that matches memory loads, verified",
              verified.count == 6 && verified.identity && *verified.identity);
    }

    // ── source_location() ───────────────────────────────────────────────
    {
        Machine m;
        m.dbg->load_source_map(sld.string(), false);
        const auto here = m.dbg->source_location();
        check("SRCDBG-LOC-01", "the PC's statement", here && here->file == "main.bas" &&
                                                         here->line == 10,
              m.where());
        const auto sub = m.dbg->source_location(0x9003);
        check("SRCDBG-LOC-02", "any address's statement, on the page mapped there now",
              sub && sub->file == "sub.bas" && sub->line == 2);
        // $8000-$8001 sit below the first record of the page: no statement.
        const fs::path no10 = dir / "no-line10.sld";
        write_text(no10, program_sld_without(p80, p90, {10}));
        m.dbg->load_source_map(no10.string(), false);
        check("SRCDBG-LOC-03", "an unmapped instruction has no location",
              !m.dbg->source_location(0x8001));
        m.dbg->load_source_map(sld.string(), false);
        // The same addresses recorded on other pages do not describe this
        // machine's memory.
        const fs::path other = dir / "other-pages.sld";
        write_text(other, program_sld((p80 + 1) % 224, (p90 + 1) % 224));
        m.dbg->load_source_map(other.string(), false);
        check("SRCDBG-LOC-04", "records for another physical page do not match",
              !m.dbg->source_location() && !m.dbg->source_location(0x9003));
    }

    // ── load_program_sidecars() ─────────────────────────────────────────
    {
        Machine m;
        const fs::path prog = dir / "game.nex";
        write_text(dir / "game.Memory.txt", "8000: ._Main\n9000: ._Sub\n");
        write_text(dir / "game.sld", program_sld(p80, p90));
        const auto r = m.dbg->load_program_sidecars(prog.string());
        check("SRCDBG-SIDE-01", "<stem>.Memory.txt and <stem>.sld beside a .nex attach",
              r.symbols == 2 && r.sources == 6 && m.dbg->lookup_name("Sub") == 0x9000 &&
                  m.dbg->source_map().size() == 6);
        check("SRCDBG-SIDE-02", "the symbols are the backend's one table",
              m.dbg->lookup(0x8000) == std::optional<std::string>("Main"));

        const auto none = m.dbg->load_program_sidecars((dir / "plain.nex").string());
        check("SRCDBG-SIDE-03", "a program with no sidecars clears what the previous "
                                "program's sidecars brought",
              none.symbols == -1 && none.sources == -1 && m.dbg->symbols().empty() &&
                  m.dbg->source_map().empty());

        m.dbg->load_source_map(sld.string(), false);
        const fs::path user_map = dir / "user.map";
        write_text(user_map, "UserSym = $8005 ; addr, local\n");
        m.dbg->load_map(user_map.string(), jnext::dbg::MapFormat::Z88dk);
        const auto kept = m.dbg->load_program_sidecars(prog.string());
        check("SRCDBG-SIDE-04", "a map and a source map the user loaded are kept over "
                                "a program's sidecars",
              kept.symbols == -1 && kept.sources == -1 &&
                  m.dbg->lookup_name("UserSym") == 0x8005 &&
                  m.dbg->source_map().loaded_file() == sld.string());
        m.dbg->load_program_sidecars((dir / "plain.nex").string());
        check("SRCDBG-SIDE-05", "... and not cleared by a program without sidecars",
              m.dbg->lookup_name("UserSym") == 0x8005 && !m.dbg->source_map().empty());

        Machine n;
        write_text(dir / "Memory.txt", "8005: ._Plain\n");
        const auto plain = n.dbg->load_program_sidecars((dir / "plain.nex").string());
        check("SRCDBG-SIDE-06", "a plain Memory.txt is the fallback for a .nex",
              plain.symbols == 1 && n.dbg->lookup_name("Plain") == 0x8005);
        const auto tap = n.dbg->load_program_sidecars((dir / "game.sna").string());
        check("SRCDBG-SIDE-07", "Memory.txt is a .nex sidecar only; an SLD beside any "
                                "program attaches",
              tap.symbols == -1 && tap.sources == 6);

        Machine w;
        write_text(dir / "bad.nex", "");
        write_text(dir / "bad.sld", "|SLD.data.version|1\n||program.name:x\n"
                   "||program.sha256:9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a\n"
                   "||program.org:32768\n||program.size:4\n" + program_sld(p80, p90).substr(20));
        const auto rejected = w.dbg->load_program_sidecars((dir / "bad.nex").string());
        check("SRCDBG-SIDE-08", "a sidecar SLD whose identity does not match the loaded "
                                "program is rejected",
              rejected.sources == -1 && w.dbg->source_map().empty());
    }
    {
        // A refused sidecar is named by its own file: the .sld, or the .sld.txt
        // when that is the one found, never the program it sits beside.
        Machine m;
        write_text(dir / "bad-a.sld", "this is not an SLD\n");
        write_text(dir / "bad-b.sld.txt", "this is not an SLD either\n");
        const auto a = m.dbg->load_program_sidecars((dir / "bad-a.sna").string());
        const auto b = m.dbg->load_program_sidecars((dir / "bad-b.sna").string());
        check("SRCDBG-SIDE-13", "a refused SLD sidecar is named by its own file, .sld or "
                                ".sld.txt, and not by the program beside it",
              a.problem.rfind("source map '", 0) == 0 &&
                  a.problem.find("bad-a.sld' rejected: missing SLD.data.version") !=
                      std::string::npos &&
                  a.problem.find("bad-a.sna") == std::string::npos &&
                  b.problem.rfind("source map '", 0) == 0 &&
                  b.problem.find("bad-b.sld.txt' rejected: missing SLD.data.version") !=
                      std::string::npos &&
                  b.problem.find("bad-b.sna") == std::string::npos,
              a.problem + " | " + b.problem);
    }

    // ── source_step() forwards ──────────────────────────────────────────
    {
        Machine m;
        check("SRCDBG-STEP-01", "with no source map a source step is refused",
              m.dbg->source_step(m.a, Step::Into) == Result::RefusedUnavailable &&
                  m.pc() == 0x8000);
        m.dbg->load_source_map(sld.string(), false);

        const Result r = m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-02", "Into runs through the rest of the statement (the second "
                                "instruction of main.bas:10): main.bas:10 -> :11",
              r == Result::Ok && m.pc() == 0x8002, m.where());
        check("SRCDBG-STEP-03", "the machine is left paused, reason Step",
              m.dbg->state().paused && m.dbg->state().pause_reason.kind == PauseReason::Kind::Step);
        m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-04", "Into enters the call: sub.bas:1", m.pc() == 0x9000, m.where());
        m.dbg->source_step(m.a, Step::Out);
        check("SRCDBG-STEP-05", "Out runs to the caller's next statement: main.bas:12",
              m.pc() == 0x8005, m.where());
        m.dbg->source_step(m.a, Step::Into);
        m.dbg->source_step(m.a, Step::Into);
        m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-06", "Into from main.bas:12 -> :13 -> :10 (round the loop) -> :11",
              m.pc() == 0x8002, m.where());
        m.dbg->source_step(m.a, Step::Over);
        check("SRCDBG-STEP-07", "Over runs the call through: main.bas:11 -> :12",
              m.pc() == 0x8005 && m.emu.mmu().read(0xA000) == m.emu.cpu().get_registers().AF >> 8,
              m.where());
    }
    {
        Machine m;
        m.dbg->load_source_map(sld.string(), false);
        m.dbg->subscribe(m.a, execute_stop(0x8001));
        m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-08", "an Execute breakpoint inside the statement (on its second "
                                "instruction) ends the step there, before it runs",
              m.pc() == 0x8001, m.where());
        m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-09", "a step from the breakpoint's own address steps off it",
              m.pc() == 0x8002, m.where());
    }
    {
        Machine m;
        m.dbg->load_source_map(sld.string(), false);
        m.set_pc(0x8002);
        Subscription w;
        w.kind = EventKind::Mem;
        w.filter.lo = w.filter.hi = 0xA000;
        w.access = Access::Write;
        w.action = Action::Stop;
        m.dbg->subscribe(m.a, w);
        m.dbg->source_step(m.a, Step::Over);
        check("SRCDBG-STEP-10", "a watchpoint inside the stepped-over call ends the step "
                                "after the write, not at the next statement",
              m.pc() == 0x9003 && m.dbg->state().pause_reason.kind != PauseReason::Kind::Step,
              m.where());
    }
    {
        Machine m;
        const fs::path no10 = dir / "no-line10-step.sld";
        write_text(no10, program_sld_without(p80, p90, {10}));
        m.dbg->load_source_map(no10.string(), false);
        m.set_pc(0x8001);   // below the page's first record: unmapped
        m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-11", "from an unmapped instruction a step is one instruction",
              m.pc() == 0x8002, m.where());
        m.set_pc(0x8002);
        m.dbg->source_step(m.a, Step::Out);
        check("SRCDBG-STEP-12", "Out with no tracked call is one instruction",
              m.pc() == 0x9000, m.where());
    }
    {
        // A statement that never ends: the step gives up at the limit, paused,
        // where it got to.
        Machine m;
        m.emu.mmu().write(0x8006, 0x18);   // JR $ in place of JP $8000
        m.emu.mmu().write(0x8007, 0xFE);
        m.dbg->load_source_map(sld.string(), false);
        m.set_pc(0x8006);
        const Result r = m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-13", "a step that never reaches another statement stops at "
                                "the limit, paused, Ok",
              r == Result::Ok && m.pc() == 0x8006 && m.dbg->state().paused, m.where());
    }
    {
        // The caller's return address has no record: Out keeps going past it
        // to the first mapped position below the call.
        // Lines 10-12 have no record: the return address $8005 lies below the
        // page's first record ($8006).
        const fs::path no12 = dir / "no-line12.sld";
        write_text(no12, program_sld_without(p80, p90, {10, 11, 12}));
        Machine m;
        m.dbg->load_source_map(no12.string(), false);
        for (int i = 0; i < 3; ++i) m.dbg->step_into(m.a);   // NOP, NOP, CALL -> sub.bas:1
        const bool in_sub = m.pc() == 0x9000;
        m.dbg->source_step(m.a, Step::Out);
        check("SRCDBG-STEP-14", "Out whose caller returns into unmapped code stops at the "
                                "first mapped position after it: main.bas:13, not $8005",
              in_sub && m.pc() == 0x8006, m.where());
    }
    {
        Machine m;
        m.dbg->load_source_map(sld.string(), false);
        m.emu.debug_state().breakpoints().add_pc(0x8001);
        m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-15", "a legacy PC breakpoint on the statement's second instruction "
                                "stops the step there, before it runs",
              m.pc() == 0x8001, m.where());
    }
    {
        // sub.bas:1 is one instruction, the write: the watchpoint fires on the
        // step's last instruction, which also reaches the next statement.
        Machine m;
        m.dbg->load_source_map(sld.string(), false);
        m.set_pc(0x9000);
        Subscription w;
        w.kind = EventKind::Mem;
        w.filter.lo = w.filter.hi = 0xA000;
        w.access = Access::Write;
        w.action = Action::Stop;
        m.dbg->subscribe(m.a, w);
        m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-16", "a watchpoint hit on the step's last instruction ends it "
                                "with the Watch reason",
              m.pc() == 0x9003 &&
                  m.dbg->state().pause_reason.kind == PauseReason::Kind::Watch,
              m.where());
    }
    {
        // A watchpoint stop inside the step ends it at once: the next
        // instruction's Execute handlers do not run on the way out.
        Machine m;
        m.dbg->load_source_map(sld.string(), false);
        m.set_pc(0x8002);
        Subscription w;
        w.kind = EventKind::Mem;
        w.filter.lo = w.filter.hi = 0xA000;
        w.access = Access::Write;
        w.action = Action::Stop;
        m.dbg->subscribe(m.a, w);
        int ran = 0;
        Subscription logger;
        logger.kind = EventKind::Execute;
        logger.filter.lo = logger.filter.hi = 0x9003;
        logger.action = Action::Continue;
        logger.handler = [&ran](const jnext::dbg::Event&, Debugger&) {
            ++ran;
            return Action::Continue;
        };
        m.dbg->subscribe(m.a, logger);
        m.dbg->source_step(m.a, Step::Over);
        check("SRCDBG-STEP-17", "a watchpoint stop inside the step ends it before the next "
                                "instruction's Execute handlers run",
              m.pc() == 0x9003 && ran == 0 &&
                  m.dbg->state().pause_reason.kind == PauseReason::Kind::Watch,
              m.where() + " ran " + std::to_string(ran));
    }

    // A record covers the bytes up to the next one: the step goes by statement,
    // not by exact record address (GH #338).
    {
        const fs::path nops = dir / "nops.sld";
        write_text(nops, x_sld(p80, {{"1", 0x8000}, {"2", 0x8003}}));
        Machine m;
        for (uint16_t a = 0x8000; a < 0x8004; ++a) m.emu.mmu().write(a, 0x00);   // NOP x4
        m.dbg->load_source_map(nops.string(), false);
        m.set_pc(0x8001);   // inside x.asm:1, not at its record
        m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-18", "a step from inside a statement's range, not at its record "
                                "address, runs to the next statement",
              m.pc() == 0x8003, m.where());

        const fs::path cols = dir / "cols.sld";
        write_text(cols, x_sld(p80, {{"5:1", 0x8000}, {"5:7", 0x8001}, {"6", 0x8002}}));
        Machine n;
        for (uint16_t a = 0x8000; a < 0x8004; ++a) n.emu.mmu().write(a, 0x00);
        n.dbg->load_source_map(cols.string(), false);
        n.dbg->source_step(n.a, Step::Into);
        check("SRCDBG-STEP-19", "a step stops between two columns of one line: the column is "
                                "part of the position",
              n.pc() == 0x8001, n.where());
    }

    // ── source_step() backwards ─────────────────────────────────────────
    {
        Machine m(/*rewind=*/true);
        m.dbg->load_source_map(sld.string(), false);
        check("SRCDBG-BACK-01", "Back with the trace off is refused",
              m.dbg->source_step(m.a, Step::Back) == Result::RefusedUnavailable);
        m.dbg->set_trace_enabled(true);
        for (int i = 0; i < 5; ++i) m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-BACK-02", "precondition: five steps in, at main.bas:13",
              m.pc() == 0x8006, m.where());
        const Result r = m.dbg->source_step(m.a, Step::Back);
        check("SRCDBG-BACK-03", "Back rewinds to the previous statement: main.bas:12",
              r == Result::Ok && m.pc() == 0x8005, m.where());
        m.dbg->source_step(m.a, Step::Back);
        check("SRCDBG-BACK-04", "Back again lands on the start of sub.bas:2, not on its RET: sub.bas:2",
              m.pc() == 0x9003, m.where());
        check("SRCDBG-BACK-05", "the call stack is the one at that point (inside SUB)",
              m.dbg->call_stack().size() == 1);

        m.dbg->subscribe(m.a, execute_stop(0x8002));
        m.dbg->source_step(m.a, Step::Into);
        m.dbg->source_step(m.a, Step::Into);   // main.bas:12, :13
        const Result rc = m.dbg->source_step(m.a, Step::ReverseContinue);
        check("SRCDBG-BACK-06", "Reverse Continue rewinds to the newest source breakpoint "
                                "in the trace: main.bas:11",
              rc == Result::Ok && m.pc() == 0x8002, m.where());
        const Result none = m.dbg->source_step(m.a, Step::ReverseContinue);
        check("SRCDBG-BACK-07", "with none earlier, Reverse Continue is refused and the "
                                "machine stays",
              none == Result::RefusedUnavailable && m.pc() == 0x8002, m.where());
    }

    {
        // Two consecutive instructions of one statement: $8005 and $8006 are
        // both main.bas:13 here. Back from $8006 is one statement back, so it
        // passes $8005 (the same position) and lands on sub.bas:2.
        const fs::path two = dir / "two-records.sld";
        std::string text = program_sld(p80, p90);
        const std::string line12 = "main.bas|12||0|" + std::to_string(p80) + "|32773|T|";
        text.replace(text.find(line12), line12.size(),
                     "main.bas|13||0|" + std::to_string(p80) + "|32773|T|");
        write_text(two, text);
        Machine m(/*rewind=*/true);
        m.dbg->load_source_map(two.string(), false);
        m.dbg->set_trace_enabled(true);
        for (int i = 0; i < 4; ++i) m.dbg->source_step(m.a, Step::Into);   // :11, sub:1, sub:2, :13
        m.dbg->step_into(m.a);   // $8005 -> $8006, still main.bas:13
        const bool at = m.pc() == 0x8006;
        m.dbg->source_step(m.a, Step::Back);
        check("SRCDBG-BACK-08", "Back counts a statement's consecutive instructions as one "
                                "position: from the second, it lands on the statement before",
              at && m.pc() == 0x9003, m.where());
    }

    {
        // Back lands on the FIRST instruction of the previous statement (the
        // inverse of Into), not on the last one it ran (GH #338).
        const fs::path nops = dir / "back-nops.sld";
        write_text(nops, x_sld(p80, {{"1", 0x8000}, {"2", 0x8003}}));
        Machine m(/*rewind=*/true);
        for (uint16_t a = 0x8000; a < 0x8004; ++a) m.emu.mmu().write(a, 0x00);   // NOP x4
        m.dbg->load_source_map(nops.string(), false);
        m.dbg->set_trace_enabled(true);
        m.dbg->source_step(m.a, Step::Into);   // $8000 -> $8003
        const bool at = m.pc() == 0x8003;
        m.dbg->source_step(m.a, Step::Back);
        check("SRCDBG-BACK-10", "Back lands on the first instruction of the previous "
                                "multi-instruction statement",
              at && m.pc() == 0x8000, m.where());

        // The statement's run is not broken by code that has no location: a
        // call into code on another page is part of the statement.
        const fs::path call = dir / "back-call.sld";
        write_text(call, x_sld(p80, {{"1", 0x8000}, {"2", 0x8004}}));
        Machine n(/*rewind=*/true);
        const uint8_t prog[] = {0xCD, 0x00, 0xC0, 0x00, 0x00};   // CALL $C000; NOP; NOP
        for (size_t i = 0; i < sizeof(prog); ++i)
            n.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
        n.emu.mmu().write(0xC000, 0xC9);   // RET
        const bool other_page = n.dbg->effective_page(0xC000) != p80;   // read, not assumed
        n.dbg->load_source_map(call.string(), false);
        n.dbg->set_trace_enabled(true);
        n.dbg->source_step(n.a, Step::Into);   // CALL, RET, NOP -> $8004
        const bool there = n.pc() == 0x8004;
        n.dbg->source_step(n.a, Step::Back);
        check("SRCDBG-BACK-11", "Back passes over unmapped code inside the previous "
                                "statement and lands on its first instruction",
              other_page && there && n.pc() == 0x8000, n.where());
    }

    {
        // A statement that ran twice: Back lands on its LAST pass.
        //   8000 LD B,2   1      8002 NOP   2      8003 DJNZ 8002   3      8005 NOP   4
        const fs::path loop = dir / "back-loop.sld";
        write_text(loop, x_sld(p80, {{"1", 0x8000}, {"2", 0x8002}, {"3", 0x8003}, {"4", 0x8005}}));
        Machine m(/*rewind=*/true);
        const uint8_t prog[] = {0x06, 0x02, 0x00, 0x10, 0xFD, 0x00};
        for (size_t i = 0; i < sizeof(prog); ++i)
            m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
        m.dbg->load_source_map(loop.string(), false);
        m.dbg->set_trace_enabled(true);
        for (int i = 0; i < 5; ++i) m.dbg->source_step(m.a, Step::Into);   // 1,2,3,2,3 -> 4
        const bool at4 = m.pc() == 0x8005;
        m.dbg->source_step(m.a, Step::Back);
        const unsigned b = m.emu.cpu().get_registers().BC >> 8;
        check("SRCDBG-BACK-12", "Back after a loop lands on the last pass of the previous "
                                "statement, not the first",
              at4 && m.pc() == 0x8003 && b == 1,
              m.where() + " B=" + std::to_string(b));
    }

    // ── fetch identity: a page names RAM only when RAM supplies the fetch ─
    {
        // 48K: slots 0/1 hold ROM. Whatever page number the MMU reports for
        // them, a record for that page describes RAM code, not the ROM.
        Machine m;
        const unsigned rom_page = m.dbg->effective_page(0x0000);
        const fs::path rom_sld = dir / "rom-page.sld";
        write_text(rom_sld, std::string("|SLD.data.version|1\n") + "x.asm|1||0" + kNextDevice +
                                "x.asm|7||0|" + std::to_string(rom_page) + "|1|T|\n");
        m.dbg->load_source_map(rom_sld.string(), false);
        check("SRCDBG-ID-01", "code in a ROM slot is not the RAM page with its page number",
              m.dbg->source_page(0x0001) == MemSpace::rom(0) && !m.dbg->source_location(0x0001));

        // A page-qualified Execute matches page numbers alone (DZRP, the DSL);
        // one qualified by space = Page{n} (a source breakpoint) refuses the ROM.
        Subscription by_number;
        by_number.kind = EventKind::Execute;
        by_number.filter.lo = by_number.filter.hi = 0x0001;
        by_number.filter.page = static_cast<uint16_t>(rom_page);
        Subscription ram_only = by_number;
        ram_only.filter.space = MemSpace::page(static_cast<uint16_t>(rom_page));
        const auto id1 = m.dbg->subscribe(m.a, by_number).value;
        const auto id2 = m.dbg->subscribe(m.a, ram_only).value;
        const auto hits = m.dbg->probe_execute(0x0001);
        const bool has1 = std::find(hits.begin(), hits.end(), id1) != hits.end();
        const bool has2 = std::find(hits.begin(), hits.end(), id2) != hits.end();
        check("SRCDBG-ID-02", "an Execute qualified by space Page{n} does not match a ROM fetch under "
                              "the same page number; a plain page qualifier still does",
              has1 && !has2);
    }
    {
        // Next: slot 0 mapped to RAM page 10 is RAM — until the Layer 2 read
        // mapping overlays it, when the fetch comes from Layer 2 instead.
        Machine m(false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        const bool ram = m.dbg->source_page(0x0010) == MemSpace::page(10);
        m.dbg->port_out(m.a, 0x123B, 0x04);   // Layer 2 read mapping, low 16K
        // NR 0x12 resets to bank 8 and the segment is 00, so the page is
        // layer2_active_page = (8 + 0) << 1 | cpu_a(13) = 0x10 (zxnext.vhd:2966-2969).
        const bool overlaid = m.dbg->source_page(0x0010) == MemSpace::layer2(0x10);
        m.dbg->port_out(m.a, 0x123B, 0x00);
        check("SRCDBG-ID-03", "a RAM page under the Layer 2 read mapping is not that page",
              ram && overlaid && m.dbg->source_page(0x0010) == MemSpace::page(10));
    }
    {
        // History: a ROM entry of the trace is not the RAM page its number
        // names either. 8000: JP $0000, then the ROM (with no ROM image loaded
        // it reads $FF: RST $38); the map has a record on the ROM's page
        // number at $0000. Back from the ROM must not land on it.
        Machine m(/*rewind=*/true);
        const unsigned rom_page = m.dbg->effective_page(0x0000);
        m.emu.mmu().write(0x8000, 0xC3);
        m.emu.mmu().write(0x8001, 0x00);
        m.emu.mmu().write(0x8002, 0x00);
        const fs::path h = dir / "rom-history.sld";
        write_text(h, std::string("|SLD.data.version|1\n") + "x.asm|1||0" + kNextDevice +
                          "x.asm|1||0|" + std::to_string(p80) + "|32768|T|\n" +
                          "x.asm|7||0|" + std::to_string(rom_page) + "|0|T|\n");
        m.dbg->load_source_map(h.string(), false);
        m.dbg->set_trace_enabled(true);
        for (int i = 0; i < 4; ++i) m.dbg->step_into(m.a);
        const bool in_rom = m.pc() < 0x4000;
        std::string seen = "rom page " + std::to_string(rom_page) + " from " + hex(m.pc()) + " trace";
        for (size_t i = 0; i < m.emu.trace_log().size(); ++i)
            seen += " " + hex(m.emu.trace_log().at(i).pc) + "/" + std::to_string(m.emu.trace_log().at(i).mmu[0]);
        m.dbg->source_step(m.a, Step::Back);
        check("SRCDBG-ID-04", "Back does not take a ROM entry of the trace for the RAM "
                              "page with its number",
              in_rom && m.pc() == 0x8000, m.where() + " " + seen);
    }

    {
        // History again, the overlay case: the trace's MMU pages say RAM page
        // 10 at $0000, but the Layer 2 read mapping supplied the fetches. The
        // record on page 10 at $0001 is not what ran there.
        Machine m(/*rewind=*/true, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        const unsigned p8 = ram_page_of(m.dbg->source_page(0x8000));
        m.emu.mmu().write(0x8000, 0xC3);
        m.emu.mmu().write(0x8001, 0x00);
        m.emu.mmu().write(0x8002, 0x00);
        Z80Registers r = m.emu.cpu().get_registers();
        r.PC = 0x8000;
        m.emu.cpu().set_registers(r);
        const fs::path h = dir / "overlay-history.sld";
        write_text(h, std::string("|SLD.data.version|1\n") + "x.asm|1||0" + kNextDevice +
                          "x.asm|1||0|" + std::to_string(p8) + "|32768|T|\n" +
                          "x.asm|7||0|10|1|T|\n");
        m.dbg->load_source_map(h.string(), false);
        m.dbg->set_trace_enabled(true);
        m.dbg->port_out(m.a, 0x123B, 0x04);   // Layer 2 read mapping, low 16K
        for (int i = 0; i < 4; ++i) m.dbg->step_into(m.a);
        const bool where = m.pc() == 0x0003;
        m.dbg->source_step(m.a, Step::Back);
        check("SRCDBG-ID-05", "Back does not take an overlaid entry of the trace for the "
                              "RAM page the MMU named",
              where && m.pc() == 0x8000, m.where());
    }

    {
        // What the M1 itself switches. RAM page 10 at $0000, DivMMC automap
        // armed with an INSTANT entry point at $0000 (NR 0xB8/B9/BA bit 0):
        // nothing is mapped yet, but the fetch at $0000 will come from
        // DivMMC. 8000: CALL $0000; DivMMC ROM $0000: INC A, RET.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);   // automap enable
        m.dbg->nextreg_write(m.a, 0xB8, 0x01);
        m.dbg->nextreg_write(m.a, 0xB9, 0x01);
        m.dbg->nextreg_write(m.a, 0xBA, 0x01);   // instant
        m.emu.divmmc().rom_data()[0] = 0x3C;      // INC A
        m.emu.divmmc().rom_data()[1] = 0xC9;      // RET
        m.emu.mmu().write(0x0000, 0x00);          // RAM page 10: NOP
        m.emu.mmu().write(0x8000, 0xCD);
        m.emu.mmu().write(0x8001, 0x00);
        m.emu.mmu().write(0x8002, 0x00);
        Z80Registers r = m.emu.cpu().get_registers();
        r.PC = 0x8000;
        r.AF = 0x0000;
        m.emu.cpu().set_registers(r);
        const bool mapped_now = m.emu.divmmc().is_active();
        const bool predicted = m.dbg->source_page(0x0000) == MemSpace::divmmc_rom();
        Subscription bp = execute_stop(0x0000);
        bp.filter.page = 10;
        bp.filter.space = MemSpace::page(10);
        const auto id = m.dbg->subscribe(m.a, bp).value;
        const auto hits = m.dbg->probe_execute(0x0000);
        const bool bp_refused = std::find(hits.begin(), hits.end(), id) == hits.end();
        check("SRCDBG-ID-06", "an instant DivMMC entry point is foreseen: before the M1 "
                              "maps it, $0000 is already not RAM page 10, and a RAM-only "
                              "breakpoint there does not match",
              !mapped_now && predicted && bp_refused);

        m.dbg->unsubscribe(m.a, id);
        m.dbg->set_trace_enabled(true);
        m.dbg->set_call_stack_enabled(m.a, true);
        m.dbg->step_into(m.a);   // CALL
        m.dbg->step_into(m.a);   // DivMMC's INC A
        const auto& t = m.emu.trace_log();
        const bool ran_divmmc = (m.emu.cpu().get_registers().AF >> 8) == 1;
        const bool flagged = t.size() >= 2 && t.at(t.size() - 1).pc == 0x0000 &&
                             t.at(t.size() - 1).fetch_space().kind != MemSpace::Kind::Page;
        const auto& f = m.dbg->call_stack();
        check("SRCDBG-ID-07", "and once it runs: the trace marks DivMMC's instruction "
                              "not-RAM, and the CALL's frame names no RAM page as target",
              ran_divmmc && flagged && f.size() == 1 && ram_page_of(f[0].target_space) == NOT_RAM_PAGE,
              "A=" + std::to_string(m.emu.cpu().get_registers().AF >> 8));
    }
    {
        // The other direction: DivMMC mapped and held, the auto-unmap range
        // (NR 0xBB bit 6) at $1FF8. The fetch at $1FF8 is still DivMMC's; the
        // fetch after it is RAM page 10 again — while is_active() still says
        // DivMMC.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);
        m.dbg->nextreg_write(m.a, 0xB8, 0x01);
        m.dbg->nextreg_write(m.a, 0xB9, 0x01);
        m.dbg->nextreg_write(m.a, 0xBA, 0x01);
        m.dbg->nextreg_write(m.a, 0xBB, 0x40);   // unmap at 0x1FF8-0x1FFF
        m.emu.divmmc().rom_data()[0] = 0xC3;      // JP $1FF8
        m.emu.divmmc().rom_data()[1] = 0xF8;
        m.emu.divmmc().rom_data()[2] = 0x1F;
        m.emu.divmmc().rom_data()[0x1FF8] = 0x00; // NOP
        Z80Registers r = m.emu.cpu().get_registers();
        r.PC = 0x0000;
        m.emu.cpu().set_registers(r);
        m.dbg->step_into(m.a);                    // the JP, from DivMMC
        const bool at = m.pc() == 0x1FF8 && m.dbg->source_page(0x1FF8) == MemSpace::divmmc_rom();
        m.dbg->step_into(m.a);                    // the NOP at $1FF8 arms the unmap
        const bool still_mapped = m.emu.divmmc().is_active();
        check("SRCDBG-ID-08", "after a fetch in the unmap range the NEXT fetch is RAM "
                              "again, foreseen while DivMMC still reads as mapped",
              at && m.pc() == 0x1FF9 && still_mapped &&
                  m.dbg->source_page(0x1FF9) == MemSpace::page(10),
              m.where());
    }

    // ── GH #339: the prediction IS the recorded fetch ───────────────────
    //
    // `source_page(pc)` is asked BEFORE an instruction; `Mmu::last_m1()` is what its
    // fetch recorded AFTER. They are one function (Mmu::route_with / route) evaluated
    // at two moments, and the rows below make the DivMMC and Multiface paths between
    // the moments agree -- the delayed-on hold, conmem, automap disabled, the port
    // enable, the NMI entry, the boot ROM, DivMMC RAM, the Multiface -- with the
    // expected values derived from the VHDL, not from either side.
    {
        // D2, the delayed-on entry: RST $08 with NR 0xBA bit 1 clear is HELD for the
        // next M1 (divmmc.vhd:128-131 hold, :141 held, :148 automap; zxnext.vhd:2894
        // delayed_on), so the fetch AT $0008 is still RAM page 10 and the one after
        // it, at $0009, comes from the DivMMC ROM.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);
        m.dbg->nextreg_write(m.a, 0xB8, 0x02);
        m.dbg->nextreg_write(m.a, 0xB9, 0x02);
        m.dbg->nextreg_write(m.a, 0xBA, 0x00);    // delayed
        m.emu.mmu().write(0x0008, 0x00);           // RAM page 10: NOP
        m.emu.divmmc().rom_data()[9] = 0x00;       // DivMMC ROM $0009: NOP
        m.emu.mmu().write(0x8000, 0xCF);           // RST $08
        m.emu.mmu().write(0x8001, 0x00);
        m.set_pc(0x8000);
        (void)step_pair(m);                        // the RST
        const FetchPair a = step_pair(m);          // $0008
        const FetchPair b = step_pair(m);          // $0009
        check("SRCDBG-ID-09", "a delayed-on DivMMC entry maps at the NEXT M1: the fetch at $0008 "
                              "is RAM page 10, the one at $0009 the DivMMC ROM, each predicted "
                              "exactly as the fetch records it",
              a.pc == 0x0008 && a.predicted == MemSpace::page(10) && a.recorded == MemSpace::page(10) &&
                  b.pc == 0x0009 && b.predicted == MemSpace::divmmc_rom() &&
                  b.recorded == MemSpace::divmmc_rom(),
              pair_str(a) + " " + pair_str(b));
    }
    {
        // D4, conmem: port 0xE3 bit 7 maps the DivMMC with the automap DISABLED
        // (NR 0x0A bit 4 clear): conmem is not gated by automap_reset
        // (zxnext.vhd:4112 gates `automap`, divmmc.vhd:94 takes conmem OR automap).
        // At $0010 (an RST candidate) and at $0100 (no entry point at all).
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x00);
        m.dbg->port_out(m.a, 0xE3, 0x80);
        m.emu.divmmc().rom_data()[0x10] = 0x00;
        m.emu.divmmc().rom_data()[0x100] = 0x00;
        const bool armed = m.emu.divmmc().conmem() && !m.emu.divmmc().nr_0a_4_enable();
        m.set_pc(0x0010);
        const FetchPair a = step_pair(m);
        m.set_pc(0x0100);
        const FetchPair b = step_pair(m);
        check("SRCDBG-ID-10", "conmem maps the DivMMC ROM with the automap disabled, at an RST "
                              "candidate and at a pc that is no entry point, predicted as recorded",
              armed && a.predicted == MemSpace::divmmc_rom() && a.recorded == MemSpace::divmmc_rom() &&
                  b.predicted == MemSpace::divmmc_rom() && b.recorded == MemSpace::divmmc_rom(),
              pair_str(a) + " " + pair_str(b));
    }
    {
        // D5, automap disabled: RST $00 is an INSTANT entry, but NR 0x0A bit 4 is
        // clear, so `divmmc_automap_reset` holds `automap` at 0 (zxnext.vhd:4112;
        // divmmc.vhd:126,148). The control is the same machine with the bit set.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x00);
        m.dbg->nextreg_write(m.a, 0xB8, 0x01);
        m.dbg->nextreg_write(m.a, 0xB9, 0x01);
        m.dbg->nextreg_write(m.a, 0xBA, 0x01);
        m.set_pc(0x0000);
        const FetchPair off = step_pair(m);
        Machine c(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        c.dbg->nextreg_write(c.a, 0x50, 10);
        c.dbg->nextreg_write(c.a, 0x0A, 0x10);
        c.dbg->nextreg_write(c.a, 0xB8, 0x01);
        c.dbg->nextreg_write(c.a, 0xB9, 0x01);
        c.dbg->nextreg_write(c.a, 0xBA, 0x01);
        c.set_pc(0x0000);
        const FetchPair on = step_pair(c);
        check("SRCDBG-ID-11", "an instant DivMMC entry does not map while NR 0x0A bit 4 is clear "
                              "(and does when it is set), predicted as recorded",
              off.predicted == MemSpace::page(10) && off.recorded == MemSpace::page(10) &&
                  on.predicted == MemSpace::divmmc_rom() && on.recorded == MemSpace::divmmc_rom(),
              pair_str(off) + " " + pair_str(on));
    }
    {
        // D6, port_divmmc_io_en: conmem is set, but NR 0x83 bit 0 clear makes
        // `o_divmmc_rom_en <= rom_en and i_en` low (divmmc.vhd:98; the bit is
        // internal_port_enable(8), zxnext.vhd:2412). The control keeps the port on.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->port_out(m.a, 0xE3, 0x80);
        m.dbg->nextreg_write(m.a, 0x83, 0xFE);
        m.set_pc(0x0010);
        const bool io_off = !m.emu.divmmc().port_io_enable() && m.emu.divmmc().conmem();
        const FetchPair off = step_pair(m);
        Machine c(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        c.dbg->nextreg_write(c.a, 0x50, 10);
        c.dbg->port_out(c.a, 0xE3, 0x80);
        c.set_pc(0x0010);
        const FetchPair on = step_pair(c);
        check("SRCDBG-ID-12", "conmem maps nothing while the DivMMC port enable (NR 0x83 bit 0) is "
                              "clear, and the DivMMC ROM when it is set, predicted as recorded",
              io_off && off.predicted == MemSpace::page(10) && off.recorded == MemSpace::page(10) &&
                  on.predicted == MemSpace::divmmc_rom() && on.recorded == MemSpace::divmmc_rom(),
              pair_str(off) + " " + pair_str(on));
    }
    {
        // U1, the boot ROM answers above the whole arbiter, DivMMC conmem included
        // (zxnext.vhd:1856-1857, the cpu_di mux).
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        const std::vector<uint8_t> nops(0x2000, 0x00);
        m.emu.mmu().set_boot_rom(nops.data(), nops.size());
        m.dbg->port_out(m.a, 0xE3, 0x80);
        m.set_pc(0x0010);
        const FetchPair p = step_pair(m);
        check("SRCDBG-ID-13", "the boot ROM supplies a fetch over DivMMC conmem, predicted as recorded",
              m.emu.divmmc().conmem() && p.predicted == MemSpace::boot_rom() &&
                  p.recorded == MemSpace::boot_rom(),
              pair_str(p));
    }
    {
        // U6, DivMMC RAM: bank 5 at $2000-$3FFF while conmem is set (divmmc.vhd:95-96,99;
        // zxnext.vhd:3092-3093), with an instruction in it at $3000.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->port_out(m.a, 0xE3, 0x85);
        m.emu.divmmc().ram_data()[5 * 0x2000 + 0x1000] = 0x00;   // NOP
        m.set_pc(0x3000);
        const FetchPair p = step_pair(m);
        check("SRCDBG-ID-14", "a fetch from DivMMC RAM names the bank, predicted as recorded",
              p.predicted == MemSpace::divmmc_ram(5) && p.recorded == MemSpace::divmmc_ram(5),
              pair_str(p));
    }
    {
        // The Multiface's 0x0066 fetch: unarmed, $0066 is whatever the MMU maps; once the
        // button has armed the NMI (nmi_active), the fetch at $0066 itself latches
        // mf_enable and comes from the Multiface ROM, which `predict` must say BEFORE the
        // instruction runs (multiface.vhd:169,176,186; zxnext.vhd:3030).
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.emu.multiface().set_enabled(true);
        m.emu.ram().page_ptr(0x0A)[0x66] = 0x00;   // Multiface ROM $0066: NOP (SRAM page 0x0A)
        m.set_pc(0x0066);
        const FetchPair unarmed = step_pair(m);
        m.set_pc(0x0066);
        m.emu.multiface().button_press();
        const FetchPair armed = step_pair(m);
        check("SRCDBG-ID-15", "the Multiface ROM is foreseen at $0066 only once the NMI is armed, "
                              "and the fetch records it",
              unarmed.predicted == unarmed.recorded && unarmed.predicted != MemSpace::multiface_rom() &&
                  armed.predicted == MemSpace::multiface_rom() &&
                  armed.recorded == MemSpace::multiface_rom(),
              pair_str(unarmed) + " " + pair_str(armed));
    }
    {
        // The DivMMC's NMI entry: NR 0xBB bit 1 (instant) with the NMI button latched
        // in the DivMMC (divmmc.vhd:120,148; zxnext.vhd:2907). Without the latch the
        // fetch at $0066 is RAM page 10.
        auto machine = [](bool button) {
            auto m = std::make_unique<Machine>(/*rewind=*/false, MachineType::ZXN_ISSUE2);
            m->dbg->nextreg_write(m->a, 0x50, 10);
            m->dbg->nextreg_write(m->a, 0x0A, 0x10);
            m->dbg->nextreg_write(m->a, 0xBB, 0x02);
            m->emu.divmmc().rom_data()[0x66] = 0x00;
            m->emu.divmmc().set_button_nmi(button);
            m->set_pc(0x0066);
            return m;
        };
        auto armed = machine(true);
        const FetchPair a = step_pair(*armed);
        auto control = machine(false);
        const FetchPair c = step_pair(*control);
        check("SRCDBG-ID-18", "the DivMMC NMI entry maps at $0066 once the button is latched "
                              "(instant), and not before, predicted as recorded",
              a.predicted == MemSpace::divmmc_rom() && a.recorded == MemSpace::divmmc_rom() &&
                  c.predicted == MemSpace::page(10) && c.recorded == MemSpace::page(10),
              pair_str(a) + " " + pair_str(c));
    }
    {
        // A guest tour: every phase is real Z80 code the machine runs, between host actions
        // only where the hardware has no guest-visible way (the boot ROM, config mode, the
        // NMI request). Every step that fetched an opcode must have been predicted as
        // the fetch recorded it, and the tour must have VISITED each source and crossed each
        // transition: an instant and a delayed (held) DivMMC entry, the unmap range, a
        // guest port write (E3, 123B), a guest NEXTREG, a RETN clearing the Multiface.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        auto& emu = m.emu;
        auto put = [&](uint16_t at, std::initializer_list<uint8_t> bytes) {
            uint16_t a = at;
            for (uint8_t b : bytes) emu.mmu().write(a++, b);
        };
        // Stores the phases use. MMU0 = RAM page 10 (BRAM5) for the first phases.
        m.dbg->nextreg_write(m.a, 0x50, 10);
        emu.mmu().write(0x0000, 0xC9);                          // RET   RAM page 10 $0000 (phase 3)
        emu.mmu().write(0x0010, 0x00);                          // NOP   RAM page 10 $0010 (phase 1b)
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);                  // DivMMC automap on
        m.dbg->nextreg_write(m.a, 0xB8, 0x06);                  // RST $08 and RST $10 are entries
        m.dbg->nextreg_write(m.a, 0xB9, 0x06);                  //   on the main path
        m.dbg->nextreg_write(m.a, 0xBA, 0x02);                  //   $08 instant, $10 delayed
        m.dbg->nextreg_write(m.a, 0xBB, 0x40);                  // the unmap range
        uint8_t* drom = emu.divmmc().rom_data();
        drom[0x0008] = 0xC3; drom[0x0009] = 0xF8; drom[0x000A] = 0x1F;   // JP $1FF8
        drom[0x0011] = 0xC3; drom[0x0012] = 0xF8; drom[0x0013] = 0x1F;   // JP $1FF8
        drom[0x1FF8] = 0xC9;                                    // RET
        emu.divmmc().ram_data()[5 * 0x2000 + 0x1000] = 0xC9;    // RET   DivMMC RAM bank 5 $3000
        emu.ram().page_ptr(0x30)[0] = 0xC9;                     // RET   Layer 2 page 0x10 ($0000)
        emu.ram().page_ptr(0x0C)[0] = 0xC9;                     // RET   Alt ROM0 $0000
        emu.ram().page_ptr(0x00)[0x38] = 0xC9;                  // RET   ROM0 $0038
        emu.ram().page_ptr(0x00)[0x00] = 0xC9;                  // RET   ROM0/Config $0000
        emu.ram().page_ptr(0x0A)[0x66] = 0xED;                  // RETN  Multiface ROM $0066
        emu.ram().page_ptr(0x0A)[0x67] = 0x45;
        emu.multiface().set_enabled(true);

        put(0x8000, {0xCF, 0x00});                                              // RST $08 / NOP
        put(0x8040, {0xD7, 0x00});                                              // RST $10 / NOP
        put(0x8080, {0x3E, 0x85, 0xD3, 0xE3, 0xCD, 0x00, 0x30, 0xAF, 0xD3, 0xE3, 0x00});
        put(0x80C0, {0xED, 0x91, 0x50, 0x0A, 0xCD, 0x00, 0x00, 0xED, 0x91, 0x50, 0xFF, 0x00});
        put(0x8100, {0x01, 0x3B, 0x12, 0x3E, 0x04, 0xED, 0x79, 0xCD, 0x00, 0x00, 0xAF, 0xED, 0x79, 0x00});
        put(0x8140, {0xED, 0x91, 0x8C, 0x80, 0xCD, 0x00, 0x00, 0xED, 0x91, 0x8C, 0x00, 0x00});
        put(0x8180, {0xCD, 0x38, 0x00, 0x00});
        put(0x81C0, {0x00, 0x00});
        put(0x8200, {0xCD, 0x00, 0x00, 0x00});
        put(0x8240, {0xCD, 0x00, 0x00, 0x00});

        unsigned visited = 0;
        long fetches = 0, mismatches = 0;
        std::string first_bad;
        auto run = [&](uint16_t pc, int steps) {
            m.set_pc(pc);
            for (int i = 0; i < steps; ++i) {
                const FetchPair f = step_pair(m);
                if (!f.fetched) continue;               // an accepted NMI/INT fetches no opcode
                ++fetches;
                visited |= 1u << static_cast<unsigned>(f.recorded.kind);
                if (f.predicted != f.recorded) {
                    ++mismatches;
                    if (first_bad.empty()) first_bad = "pc=" + hex(f.pc) + " " + pair_str(f);
                }
            }
        };
        run(0x8000, 4);                                 // RST $08 (instant) / JP / RET (unmap) / NOP
        run(0x8040, 5);                                 // RST $10 (delayed) / NOP / JP (held) / RET / NOP
        run(0x8080, 6);                                 // OUT E3 conmem, CALL DivMMC RAM, RET, OUT E3
        m.dbg->nextreg_write(m.a, 0x50, 0xFF);          // MMU0 back to the legacy ROM
        run(0x80C0, 4);                                 // NEXTREG $50 -> RAM, CALL, RET, NEXTREG $50,$FF
        run(0x8100, 7);                                 // OUT 123B Layer 2, CALL, RET, OUT 123B
        run(0x8140, 4);                                 // NEXTREG $8C alt ROM, CALL, RET, NEXTREG $8C
        run(0x8180, 2);                                 // CALL $0038 / RET (legacy ROM)
        emu.multiface().button_press();                 // the NMI button
        emu.cpu().request_nmi();
        run(0x81C0, 3);                                 // NMI slot, RETN at $0066, NOP back in RAM
        std::vector<uint8_t> boot(0x2000, 0x00);
        boot[0] = 0xC9;
        emu.mmu().set_boot_rom(boot.data(), boot.size());
        run(0x8200, 3);                                 // CALL $0000 / RET from the boot ROM
        emu.mmu().set_boot_rom_enabled(false);
        emu.mmu().set_config_mode(true);
        run(0x8240, 3);                                 // CALL $0000 / RET from the config window
        emu.mmu().set_config_mode(false);

        const unsigned wanted =
            (1u << unsigned(MemSpace::Kind::Page)) | (1u << unsigned(MemSpace::Kind::Rom)) |
            (1u << unsigned(MemSpace::Kind::BootRom)) | (1u << unsigned(MemSpace::Kind::MultifaceRom)) |
            (1u << unsigned(MemSpace::Kind::DivMmcRom)) | (1u << unsigned(MemSpace::Kind::DivMmcRam)) |
            (1u << unsigned(MemSpace::Kind::Layer2)) | (1u << unsigned(MemSpace::Kind::AltRom)) |
            (1u << unsigned(MemSpace::Kind::Config));
        check("SRCDBG-ID-19", "a guest tour of every fetch source (RAM, ROM, boot ROM, Multiface, "
                              "DivMMC ROM and RAM, Layer 2, alternate ROM, config) across the "
                              "transitions that switch them: every fetch predicted as recorded",
              mismatches == 0 && fetches == 40 && (visited & wanted) == wanted,
              "fetches=" + std::to_string(fetches) + " mismatches=" + std::to_string(mismatches) +
                  " visited=" + std::to_string(visited) + " wanted=" + std::to_string(wanted) +
                  " first=[" + first_bad + "]");
    }
    {
        // The call-stack tracker captures a CALL's source BEFORE the instruction runs. A CALL
        // fetched from the DivMMC ROM (RST $08 maps it in, instant-on; the NOP at $0008 lets the
        // overlay settle, since the tracker reads the opcode before the M1 that would map it)
        // opens a frame whose caller is not a RAM page, although RAM page 10 is what the MMU
        // reports for $0009.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);
        m.dbg->nextreg_write(m.a, 0xB8, 0x02);
        m.dbg->nextreg_write(m.a, 0xB9, 0x02);
        m.dbg->nextreg_write(m.a, 0xBA, 0x02);
        uint8_t* drom = m.emu.divmmc().rom_data();
        drom[0x0008] = 0x00;                                             // NOP
        drom[0x0009] = 0xCD; drom[0x000A] = 0x20; drom[0x000B] = 0x00;   // CALL $0020
        drom[0x0020] = 0xC9;                                             // RET
        m.emu.mmu().write(0x8000, 0xCF);                                 // RST $08
        m.emu.mmu().write(0x8001, 0x00);
        m.set_pc(0x8000);
        (void)step_pair(m);   // RST $08: a frame from RAM into the DivMMC ROM
        (void)step_pair(m);   // NOP at $0008
        (void)step_pair(m);   // CALL $0020 at $0009, fetched from the DivMMC ROM
        const auto& f = m.dbg->call_stack();
        check("SRCDBG-ID-20", "a CALL fetched from the DivMMC ROM records a caller that is no RAM "
                              "page, and the RST that entered it a RAM caller (MMU4 = page 4)",
              f.size() == 2 && f[0].caller_pc == 0x8000 && ram_page_of(f[0].caller_space) == 4 &&
                  f[0].target_pc == 0x0008 && ram_page_of(f[0].target_space) == NOT_RAM_PAGE &&
                  f[1].caller_pc == 0x0009 && ram_page_of(f[1].caller_space) == NOT_RAM_PAGE &&
                  f[1].target_pc == 0x0020 && ram_page_of(f[1].target_space) == NOT_RAM_PAGE,
              [&] {
                  std::string d = "frames=" + std::to_string(f.size());
                  for (const auto& fr : f)
                      d += " [" + hex(fr.caller_pc) + "->" + hex(fr.target_pc) + " cp=" +
                           std::to_string(ram_page_of(fr.caller_space)) + " tp=" + std::to_string(ram_page_of(fr.target_space)) + "]";
                  return d;
              }());
    }
    {
        // The debugger's reads of the instruction about to run happen BEFORE the M1 that maps the
        // DivMMC in, so they must go through the overlay that M1 selects (divmmc.vhd:124-148;
        // zxnext.vhd:3084): a CALL fetched straight away at the entry point is a CALL from the
        // DivMMC ROM, its bytes are the ROM's (not RAM page 10's zeros), and the tracker opens a
        // second frame. ID-22 is the instant entry (the CALL at $0008), ID-23 the delayed one
        // (RAM at $0008, the CALL at $0009 - the first fetch the DivMMC supplies).
        struct Case { const char* id; uint8_t ba; uint16_t call_at; };
        for (const Case& c : {Case{"SRCDBG-ID-22", 0x02, 0x0008}, Case{"SRCDBG-ID-23", 0x00, 0x0009}}) {
            Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
            m.dbg->nextreg_write(m.a, 0x50, 10);
            m.dbg->nextreg_write(m.a, 0x0A, 0x10);
            m.dbg->nextreg_write(m.a, 0xB8, 0x02);
            m.dbg->nextreg_write(m.a, 0xB9, 0x02);
            m.dbg->nextreg_write(m.a, 0xBA, c.ba);
            uint8_t* drom = m.emu.divmmc().rom_data();
            drom[c.call_at] = 0xCD; drom[c.call_at + 1] = 0x20; drom[c.call_at + 2] = 0x00;  // CALL $0020
            drom[0x0020] = 0xC9;                                                            // RET
            m.emu.mmu().write(0x0008, 0x00);          // RAM page 10: NOP (the delayed case runs it)
            m.emu.mmu().write(0x8000, 0xCF);          // RST $08
            m.emu.mmu().write(0x8001, 0x00);
            m.dbg->set_trace_enabled(true);
            m.dbg->set_call_stack_enabled(m.a, true);
            m.set_pc(0x8000);
            (void)step_pair(m);                                   // RST $08
            if (c.call_at != 0x0008) (void)step_pair(m);          // the NOP at $0008 (held entry)
            (void)step_pair(m);                                   // the CALL, fetched from the DivMMC ROM
            const auto& f = m.dbg->call_stack();
            const auto& t = m.emu.trace_log();
            const TraceEntry& e = t.at(t.size() - 1);
            check(c.id, c.ba == 0x02
                      ? "an instant DivMMC entry: a CALL fetched directly at $0008 is traced with the "
                        "DivMMC ROM's bytes (CD 20 00) and opens its own frame"
                      : "a delayed DivMMC entry: a CALL fetched directly at $0009 is traced with the "
                        "DivMMC ROM's bytes (CD 20 00) and opens its own frame",
                  e.pc == c.call_at && e.opcode_bytes[0] == 0xCD && e.opcode_bytes[1] == 0x20 &&
                      e.opcode_bytes[2] == 0x00 && e.opcode_len == 3 &&
                      t.at(t.size() - 1).fetch_space().kind != MemSpace::Kind::Page && f.size() == 2 &&
                      f[1].caller_pc == c.call_at && f[1].target_pc == 0x0020,
                  [&] {
                      char b[96];
                      std::snprintf(b, sizeof b, "pc=%04X bytes=%02X %02X %02X len=%d frames=%zu",
                                    e.pc, e.opcode_bytes[0], e.opcode_bytes[1], e.opcode_bytes[2],
                                    e.opcode_len, f.size());
                      return std::string(b);
                  }());
        }
    }
    {
        // The first instruction after a fetch in the DivMMC unmap range comes from RAM again
        // (divmmc.vhd:128-148, the unmap releases `automap` for the NEXT M1), while DivMMC still
        // reads as mapped before it: the trace's bytes are RAM page 10's, not the ROM's.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);
        m.dbg->nextreg_write(m.a, 0xB8, 0x01);
        m.dbg->nextreg_write(m.a, 0xB9, 0x01);
        m.dbg->nextreg_write(m.a, 0xBA, 0x01);
        m.dbg->nextreg_write(m.a, 0xBB, 0x40);   // unmap at 0x1FF8-0x1FFF
        m.emu.mmu().write(0x1FF9, 0x3E);          // RAM page 10: LD A,$77
        m.emu.mmu().write(0x1FFA, 0x77);
        uint8_t* drom = m.emu.divmmc().rom_data();
        drom[0] = 0xC3; drom[1] = 0xF8; drom[2] = 0x1F;   // JP $1FF8
        drom[0x1FF8] = 0x00;                               // NOP
        drom[0x1FF9] = 0x06; drom[0x1FFA] = 0x55;          // LD B,$55: the ROM's, not run
        m.dbg->set_trace_enabled(true);
        m.set_pc(0x0000);
        (void)step_pair(m);                       // the JP, from DivMMC
        (void)step_pair(m);                       // the NOP at $1FF8 arms the unmap
        (void)step_pair(m);                       // $1FF9: RAM again
        const auto& t = m.emu.trace_log();
        const TraceEntry& e = t.at(t.size() - 1);
        check("SRCDBG-ID-24", "the first instruction after the DivMMC unmap range is traced with "
                              "RAM's bytes (3E 77), not the ROM it was mapped over",
              e.pc == 0x1FF9 && e.opcode_bytes[0] == 0x3E && e.opcode_bytes[1] == 0x77 &&
                  e.opcode_len == 2 && t.at(t.size() - 1).fetch_space().kind == MemSpace::Kind::Page,
              [&] {
                  char b[96];
                  std::snprintf(b, sizeof b, "pc=%04X bytes=%02X %02X len=%d", e.pc,
                                e.opcode_bytes[0], e.opcode_bytes[1], e.opcode_len);
                  return std::string(b);
              }());
    }
    {
        // The Multiface's 0x0066 fetch, armed: the instruction is the Multiface ROM's
        // (multiface.vhd:169,176,186), and so are the bytes the trace shows for it.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.emu.multiface().set_enabled(true);
        m.emu.mmu().write(0x0066, 0x00);           // RAM page 10: NOP
        m.emu.ram().page_ptr(0x0A)[0x66] = 0x3E;   // Multiface ROM $0066: LD A,$42
        m.emu.ram().page_ptr(0x0A)[0x67] = 0x42;
        m.dbg->set_trace_enabled(true);
        m.set_pc(0x0066);
        m.emu.multiface().button_press();
        (void)step_pair(m);
        const auto& t = m.emu.trace_log();
        const TraceEntry& e = t.at(t.size() - 1);
        check("SRCDBG-ID-25", "an armed Multiface fetch at $0066 is traced with the Multiface "
                              "ROM's bytes (3E 42), not RAM's",
              e.pc == 0x0066 && e.opcode_bytes[0] == 0x3E && e.opcode_bytes[1] == 0x42 &&
                  e.opcode_len == 2 && t.at(t.size() - 1).fetch_space().kind != MemSpace::Kind::Page,
              [&] {
                  char b[96];
                  std::snprintf(b, sizeof b, "pc=%04X bytes=%02X %02X len=%d", e.pc,
                                e.opcode_bytes[0], e.opcode_bytes[1], e.opcode_len);
                  return std::string(b);
              }());
    }
    {
        // A delayed-on entry maps the DivMMC for the rest of the instruction at the entry point:
        // `automap_held` takes `automap_hold` on the first MREQ-high after the opcode fetch
        // (divmmc.vhd:128-131,139-143,148), so the OPERANDS of the instruction at $0008 are
        // already the DivMMC ROM's while its opcode byte was RAM's (ID-09).
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);
        m.dbg->nextreg_write(m.a, 0xB8, 0x02);
        m.dbg->nextreg_write(m.a, 0xB9, 0x02);
        m.dbg->nextreg_write(m.a, 0xBA, 0x00);    // delayed
        m.emu.mmu().write(0x0008, 0x2A);           // RAM page 10: LD HL,($9000)
        m.emu.mmu().write(0x0009, 0x00);
        m.emu.mmu().write(0x000A, 0x90);
        uint8_t* drom = m.emu.divmmc().rom_data();
        drom[0x0009] = 0x00; drom[0x000A] = 0xA0;  // the ROM's operands: ($A000)
        m.emu.mmu().write(0x9000, 0xAA); m.emu.mmu().write(0x9001, 0xBB);
        m.emu.mmu().write(0xA000, 0xCC); m.emu.mmu().write(0xA001, 0xDD);
        m.emu.mmu().write(0x8000, 0xCF);           // RST $08
        m.dbg->set_trace_enabled(true);
        m.set_pc(0x8000);
        (void)step_pair(m);                        // the RST
        (void)step_pair(m);                        // LD HL,(nn) at $0008
        const auto& t = m.emu.trace_log();
        const TraceEntry& e = t.at(t.size() - 1);
        check("SRCDBG-ID-26", "a delayed DivMMC entry: the instruction at $0008 takes its opcode "
                              "from RAM and its operands from the DivMMC ROM (HL = $DDCC, not $BBAA), "
                              "and the trace shows those bytes (2A 00 A0)",
              m.dbg->registers().HL == 0xDDCC && m.pc() == 0x000B && e.pc == 0x0008 &&
                  e.opcode_bytes[0] == 0x2A && e.opcode_bytes[1] == 0x00 &&
                  e.opcode_bytes[2] == 0xA0 && e.opcode_len == 3 && t.at(t.size() - 1).fetch_space().kind == MemSpace::Kind::Page,
              "HL=" + hex(m.dbg->registers().HL) + " " + m.where());
    }
    {
        // The unmap is the mirror image: the fetch in 0x1FF8-0x1FFF still comes from the DivMMC
        // ROM (held is 1 at that M1), and `hold` drops during it, so its operands are RAM's.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);
        m.dbg->nextreg_write(m.a, 0xB8, 0x01);
        m.dbg->nextreg_write(m.a, 0xB9, 0x01);
        m.dbg->nextreg_write(m.a, 0xBA, 0x01);
        m.dbg->nextreg_write(m.a, 0xBB, 0x40);    // unmap at 0x1FF8-0x1FFF
        m.emu.mmu().write(0x1FF9, 0x00);           // RAM page 10: operands ($9000)
        m.emu.mmu().write(0x1FFA, 0x90);
        uint8_t* drom = m.emu.divmmc().rom_data();
        drom[0] = 0xC3; drom[1] = 0xF8; drom[2] = 0x1F;   // JP $1FF8
        drom[0x1FF8] = 0x3A;                               // LD A,(nn)
        drom[0x1FF9] = 0x00; drom[0x1FFA] = 0xA0;          // the ROM's operands: ($A000)
        m.emu.mmu().write(0x9000, 0xAA);
        m.emu.mmu().write(0xA000, 0xCC);
        m.dbg->set_trace_enabled(true);
        m.set_pc(0x0000);
        (void)step_pair(m);                        // the JP, from DivMMC
        (void)step_pair(m);                        // LD A,(nn) at $1FF8
        const auto& t = m.emu.trace_log();
        const TraceEntry& e = t.at(t.size() - 1);
        check("SRCDBG-ID-27", "the instruction at $1FF8 takes its opcode from the DivMMC ROM and "
                              "its operands from RAM (A = $AA, not $CC), and the trace shows "
                              "those bytes (3A 00 90)",
              (m.dbg->registers().AF >> 8) == 0xAA && m.pc() == 0x1FFB && e.pc == 0x1FF8 &&
                  e.opcode_bytes[0] == 0x3A && e.opcode_bytes[1] == 0x00 &&
                  e.opcode_bytes[2] == 0x90 && e.opcode_len == 3 && t.at(t.size() - 1).fetch_space().kind != MemSpace::Kind::Page,
              "AF=" + hex(m.dbg->registers().AF) + " " + m.where());
    }
    {
        // Writes follow the same view: the store of a delayed-entry instruction at $0008 lands
        // after the fetch, so it meets the DivMMC ROM (read-only, divmmc.vhd:88-101) and is lost;
        // RAM page 10 keeps its zero.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);
        m.dbg->nextreg_write(m.a, 0xB8, 0x02);
        m.dbg->nextreg_write(m.a, 0xB9, 0x02);
        m.dbg->nextreg_write(m.a, 0xBA, 0x00);    // delayed
        m.emu.mmu().write(0x0008, 0x32);           // RAM page 10: LD ($0100),A
        m.emu.mmu().write(0x0009, 0x00);
        m.emu.mmu().write(0x000A, 0x01);
        uint8_t* drom = m.emu.divmmc().rom_data();
        drom[0x0009] = 0x00; drom[0x000A] = 0x01;  // the ROM's operands are the same
        m.emu.mmu().write(0x8000, 0xCF);           // RST $08
        m.set_pc(0x8000);
        (void)step_pair(m);                        // the RST
        (void)step_pair(m);                        // LD ($0100),A at $0008, A = $FF
        const Mmu::Route under = m.emu.mmu().route_with(0x0100, false, false);   // RAM page 10, DivMMC off
        const uint8_t ram_byte = under.ptr ? *under.ptr : 0xEE;
        check("SRCDBG-ID-28", "a delayed DivMMC entry: the store of the instruction at $0008 meets "
                              "the DivMMC ROM (read-only), RAM page 10 keeps its zero",
              m.pc() == 0x000B && ram_byte == 0x00 && drom[0x0100] == 0x00,
              "pc=" + hex(m.pc()) + " ram=" + hex(ram_byte));
    }
    {
        // Every M1 loads the DivMMC `hold` (divmmc.vhd:127-131) and `automap_held` takes it on
        // the first MREQ-high after the opcode byte (:139-143), so the INNER byte of a prefixed
        // instruction, itself an M1, already sees the hold the opcode byte loaded; the d and op of
        // DD CB d op are no M1 (t80n.vhd:414-417, 1755-1761) and see it too. A delayed-on entry
        // therefore runs a prefixed instruction whose prefix is RAM's and whose inner byte is the
        // DivMMC ROM's, and the unmap window the other way round (ID-31..33); the trace and the
        // call stack read what the CPU executes.
        auto delayed = [](uint8_t b8, uint8_t ba) {
            auto m = std::make_unique<Machine>(/*rewind=*/false, MachineType::ZXN_ISSUE2);
            m->dbg->nextreg_write(m->a, 0x50, 10);
            m->dbg->nextreg_write(m->a, 0x0A, 0x10);
            m->dbg->nextreg_write(m->a, 0xB8, b8);
            m->dbg->nextreg_write(m->a, 0xB9, b8);
            m->dbg->nextreg_write(m->a, 0xBA, ba);
            return m;
        };
        auto set_regs = [](Machine& m, uint8_t a, uint16_t ix) {
            Z80Registers r = m.emu.cpu().get_registers();
            r.AF = static_cast<uint16_t>((a << 8) | (r.AF & 0xFF));
            r.IX = ix;
            m.emu.cpu().set_registers(r);
        };
        auto dump = [](const TraceEntry& e) {
            char b[96];
            std::snprintf(b, sizeof b, "pc=%04X bytes=%02X %02X %02X %02X len=%d", e.pc, e.opcode_bytes[0],
                          e.opcode_bytes[1], e.opcode_bytes[2], e.opcode_bytes[3], e.opcode_len);
            return std::string(b);
        };
        {
            // P1: RST $08 delayed; RAM CB 07 (RLC A) at $0008, DivMMC ROM 3F (SRL A) at $0009.
            auto mp = delayed(0x02, 0x00);
            Machine& m = *mp;
            m.emu.mmu().write(0x0008, 0xCB); m.emu.mmu().write(0x0009, 0x07);
            m.emu.divmmc().rom_data()[0x0009] = 0x3F;
            m.emu.mmu().write(0x8000, 0xCF);
            set_regs(m, 0x81, 0);
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x8000);
            (void)step_pair(m); (void)step_pair(m);
            const auto& t = m.emu.trace_log();
            const TraceEntry& e = t.at(t.size() - 1);
            check("SRCDBG-ID-31", "a delayed DivMMC entry: CB at $0008 is RAM's and its inner byte at "
                                  "$0009 the DivMMC ROM's - the CPU runs SRL A (A = $40) and the trace "
                                  "shows CB 3F",
                  (m.dbg->registers().AF >> 8) == 0x40 && m.pc() == 0x000A && e.pc == 0x0008 &&
                      e.opcode_bytes[0] == 0xCB && e.opcode_bytes[1] == 0x3F && e.opcode_len == 2,
                  "A=" + hex(m.dbg->registers().AF >> 8) + " " + dump(e));
        }
        {
            // P2: DD CB d op at the delayed entry: DD is RAM's, CB (inner M1), d and op the ROM's.
            auto mp = delayed(0x02, 0x00);
            Machine& m = *mp;
            const uint8_t ram[] = {0xDD, 0xCB, 0x05, 0x06};          // RLC (IX+5)
            for (int i = 0; i < 4; ++i) m.emu.mmu().write(static_cast<uint16_t>(0x0008 + i), ram[i]);
            uint8_t* drom = m.emu.divmmc().rom_data();
            drom[0x0009] = 0xCB; drom[0x000A] = 0x07; drom[0x000B] = 0x0E;   // RRC (IX+7)
            m.emu.mmu().write(0x8000, 0xCF);
            m.emu.mmu().write(0xA005, 0x01); m.emu.mmu().write(0xA007, 0x01);
            set_regs(m, 0, 0xA000);
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x8000);
            (void)step_pair(m); (void)step_pair(m);
            const auto& t = m.emu.trace_log();
            const TraceEntry& e = t.at(t.size() - 1);
            check("SRCDBG-ID-32", "a delayed DivMMC entry: DD at $0008 is RAM's, CB, d and op are the "
                                  "DivMMC ROM's - the CPU runs RRC (IX+7) and the trace shows DD CB 07 0E",
                  m.emu.mmu().read(0xA007) == 0x80 && m.emu.mmu().read(0xA005) == 0x01 &&
                      m.pc() == 0x000C && e.opcode_bytes[0] == 0xDD && e.opcode_bytes[1] == 0xCB &&
                      e.opcode_bytes[2] == 0x07 && e.opcode_bytes[3] == 0x0E && e.opcode_len == 4,
                  "(IX+7)=" + hex(m.emu.mmu().read(0xA007)) + " " + dump(e));
        }
        {
            // P3: the unmap window: CB at $1FF8 is the DivMMC ROM's, the inner byte at $1FF9 RAM's.
            auto mp = delayed(0x01, 0x01);
            Machine& m = *mp;
            m.dbg->nextreg_write(m.a, 0xBB, 0x40);
            m.emu.mmu().write(0x1FF9, 0x3F);          // RAM: SRL A
            uint8_t* drom = m.emu.divmmc().rom_data();
            drom[0] = 0xC3; drom[1] = 0xF8; drom[2] = 0x1F;   // JP $1FF8
            drom[0x1FF8] = 0xCB; drom[0x1FF9] = 0x07;          // RLC A (not run)
            set_regs(m, 0x81, 0);
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x0000);
            (void)step_pair(m); (void)step_pair(m);
            const auto& t = m.emu.trace_log();
            const TraceEntry& e = t.at(t.size() - 1);
            check("SRCDBG-ID-33", "the unmap window: CB at $1FF8 is the DivMMC ROM's and its inner byte "
                                  "at $1FF9 RAM's - the CPU runs SRL A (A = $40), the trace shows CB 3F",
                  (m.dbg->registers().AF >> 8) == 0x40 && m.pc() == 0x1FFA && e.pc == 0x1FF8 &&
                      e.opcode_bytes[0] == 0xCB && e.opcode_bytes[1] == 0x3F && e.opcode_len == 2,
                  "A=" + hex(m.dbg->registers().AF >> 8) + " " + dump(e));
        }
        {
            // The DivMMC NMI entry (NR 0xBB = $CD: bit 0, DELAYED, is the reset value): enENNextZX's
            // ROM 0-2 hold ED 45 at $0066 and the DivMMC ROM has F1 ED 45 at $0067, so the hardware
            // runs ED F1 (an 8 T NOP) from the ROM and the DivMMC's own RETN at $0068; not the ROM's.
            Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
            m.dbg->nextreg_write(m.a, 0x50, 10);
            m.dbg->nextreg_write(m.a, 0x0A, 0x10);
            m.emu.mmu().write(0x0066, 0xED); m.emu.mmu().write(0x0067, 0x45);
            uint8_t* drom = m.emu.divmmc().rom_data();
            drom[0x0067] = 0xF1; drom[0x0068] = 0xED; drom[0x0069] = 0x45;
            m.emu.mmu().write(0xFF00, 0x34); m.emu.mmu().write(0xFF01, 0x12);
            Z80Registers r = m.emu.cpu().get_registers();
            r.SP = 0xFF00;
            m.emu.cpu().set_registers(r);
            m.emu.divmmc().set_button_nmi(true);
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x0066);
            (void)step_pair(m);
            const bool nop_at_68 = m.pc() == 0x0068 && m.dbg->registers().SP == 0xFF00;
            const auto& t = m.emu.trace_log();
            const TraceEntry e = t.at(t.size() - 1);
            (void)step_pair(m);                       // the DivMMC's RETN
            check("SRCDBG-ID-34", "the DivMMC NMI entry is delayed: ED at $0066 is the ROM's, F1 at $0067 "
                                  "the DivMMC's (an 8 T NOP, the trace shows ED F1), then the DivMMC's "
                                  "RETN at $0068 returns",
                  nop_at_68 && e.opcode_bytes[0] == 0xED && e.opcode_bytes[1] == 0xF1 &&
                      m.pc() == 0x1234 && m.dbg->registers().SP == 0xFF02,
                  "after ED F1: " + std::string(nop_at_68 ? "pc=0068" : "pc/sp off") + " " + dump(e) +
                      " final pc=" + hex(m.pc()));
        }
        {
            // A prefix whose inner byte lands on an INSTANT entry point: the inner M1 runs its own
            // decode (zxnext.vhd:4116-4125 latches the instant terms while M1_n is low), so the byte
            // at $0008 is the DivMMC ROM's (3F, SRL A) although the CB before it is RAM's.
            auto mp = delayed(0x02, 0x02);
            Machine& m = *mp;
            m.emu.mmu().write(0x0007, 0xCB); m.emu.mmu().write(0x0008, 0x07);
            m.emu.divmmc().rom_data()[0x0008] = 0x3F;
            set_regs(m, 0x81, 0);
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x0007);
            (void)step_pair(m);
            const auto& t = m.emu.trace_log();
            const TraceEntry& e = t.at(t.size() - 1);
            check("SRCDBG-ID-35", "a prefix whose inner byte is an instant DivMMC entry: the inner M1 "
                                  "maps the DivMMC itself - SRL A (A = $40), the trace shows CB 3F",
                  (m.dbg->registers().AF >> 8) == 0x40 && m.pc() == 0x0009 && e.pc == 0x0007 &&
                      e.opcode_bytes[0] == 0xCB && e.opcode_bytes[1] == 0x3F && e.opcode_len == 2,
                  "A=" + hex(m.dbg->registers().AF >> 8) + " " + dump(e));
        }
        {
            // conmem: the DivMMC ROM is mapped for every cycle, the operand bytes included.
            Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
            m.dbg->nextreg_write(m.a, 0x50, 10);
            m.dbg->nextreg_write(m.a, 0x0A, 0x00);
            m.dbg->port_out(m.a, 0xE3, 0x80);
            uint8_t* drom = m.emu.divmmc().rom_data();
            drom[0x0100] = 0x3A; drom[0x0101] = 0x00; drom[0x0102] = 0x90;   // LD A,($9000)
            m.emu.mmu().write(0x9000, 0x5A);
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x0100);
            (void)step_pair(m);
            const auto& t = m.emu.trace_log();
            const TraceEntry& e = t.at(t.size() - 1);
            check("SRCDBG-ID-36", "conmem: the trace shows the DivMMC ROM's operand bytes (3A 00 90)",
                  m.emu.divmmc().conmem() && (m.dbg->registers().AF >> 8) == 0x5A &&
                      e.opcode_bytes[0] == 0x3A && e.opcode_bytes[1] == 0x00 &&
                      e.opcode_bytes[2] == 0x90 && e.opcode_len == 3,
                  dump(e));
        }
        {
            // The call-stack tracker reads the same bytes the CPU runs: a CALL in RAM at a delayed
            // $0008 takes its operands from the DivMMC ROM (target $0030, not the RAM's $0020), and
            // its caller is RAM page 10.
            auto mp = delayed(0x02, 0x00);
            Machine& m = *mp;
            m.emu.mmu().write(0x0008, 0xCD); m.emu.mmu().write(0x0009, 0x20); m.emu.mmu().write(0x000A, 0x00);
            uint8_t* drom = m.emu.divmmc().rom_data();
            drom[0x0008] = 0x00; drom[0x0009] = 0x30; drom[0x000A] = 0x00;
            m.emu.mmu().write(0x8000, 0xCF);
            m.dbg->set_call_stack_enabled(m.a, true);
            m.set_pc(0x8000);
            (void)step_pair(m); (void)step_pair(m);
            const auto& f = m.dbg->call_stack();
            check("SRCDBG-ID-37", "a CALL in RAM at a delayed DivMMC entry: its frame has the caller page "
                                  "RAM page 10 and the target the DivMMC ROM's operands ($0030)",
                  m.pc() == 0x0030 && f.size() == 2 && f[1].caller_pc == 0x0008 &&
                      ram_page_of(f[1].caller_space) == 10 && f[1].target_pc == 0x0030,
                  [&] {
                      std::string d = "pc=" + hex(m.pc()) + " frames=" + std::to_string(f.size());
                      if (f.size() > 1)
                          d += " [" + hex(f[1].caller_pc) + "->" + hex(f[1].target_pc) +
                               " cp=" + std::to_string(ram_page_of(f[1].caller_space)) + "]";
                      return d;
                  }());
        }
        {
            // The NMI feedback is sampled after the instruction: the one at a delayed entry already
            // disables the NMI (the hold is loaded), the one that unmaps already releases it
            // (divmmc.vhd:139-143,150).
            auto mp = delayed(0x02, 0x00);
            Machine& m = *mp;
            m.emu.mmu().write(0x8000, 0xCF);
            m.set_pc(0x8000);
            (void)step_pair(m);
            const bool before = m.emu.divmmc().is_nmi_hold();
            (void)step_pair(m);                       // the instruction at $0008
            check("SRCDBG-ID-29", "the instruction at a delayed DivMMC entry disables the NMI "
                                  "(is_nmi_hold) once it has been fetched, not before",
                  !before && m.emu.divmmc().is_nmi_hold(),
                  std::string(before ? "held already" : "not held yet") +
                      (m.emu.divmmc().is_nmi_hold() ? ", held after" : ", not held after"));
            auto up = delayed(0x01, 0x01);
            Machine& u = *up;
            u.dbg->nextreg_write(u.a, 0xBB, 0x40);
            uint8_t* drom = u.emu.divmmc().rom_data();
            drom[0] = 0xC3; drom[1] = 0xF8; drom[2] = 0x1F;   // JP $1FF8
            drom[0x1FF8] = 0x00;
            u.set_pc(0x0000);
            (void)step_pair(u);
            const bool mapped = u.emu.divmmc().is_nmi_hold();
            (void)step_pair(u);                       // the NOP at $1FF8 unmaps
            check("SRCDBG-ID-30", "the instruction that unmaps the DivMMC releases the NMI hold "
                                  "(is_nmi_hold) once it has been fetched",
                  mapped && !u.emu.divmmc().is_nmi_hold(),
                  std::string(mapped ? "held before" : "not held before") +
                      (u.emu.divmmc().is_nmi_hold() ? ", held after" : ", released after"));
        }
    }
    {
        // Each inner byte of a prefix chain is an M1 with its own decode: a prefix at $0007 and an
        // inner byte at $0008, a DELAYED entry, is still RAM's (the hold it loads reaches the cycles
        // AFTER that fetch, divmmc.vhd:127-143), while the DivMMC ROM byte at $0008 differs. What the
        // CPU's M1 callback, the Z80N dispatch, the magic-breakpoint test and the IncDecZ latch see
        // for that byte is the RAM's (ID-38..42).
        auto setup = [](uint8_t b8) {
            auto m = std::make_unique<Machine>(/*rewind=*/false, MachineType::ZXN_ISSUE2);
            m->dbg->nextreg_write(m->a, 0x50, 10);
            m->dbg->nextreg_write(m->a, 0x0A, 0x10);
            m->dbg->nextreg_write(m->a, 0xB8, b8);
            m->dbg->nextreg_write(m->a, 0xB9, b8);
            m->dbg->nextreg_write(m->a, 0xBA, 0x00);   // delayed
            m->emu.divmmc().rom_data()[0x0008] = 0x00;  // the DivMMC ROM's byte, never the one wanted
            return m;
        };
        struct Seen { uint16_t pc; uint8_t b; };
        auto spy = [](Machine& m, std::vector<Seen>& log) {
            auto orig = m.emu.cpu().on_m1_cycle;
            m.emu.cpu().on_m1_cycle = [&log, orig](uint16_t pc, uint8_t b) {
                log.push_back({pc, b});
                if (orig) orig(pc, b);
            };
        };
        auto saw = [](const std::vector<Seen>& log, uint16_t pc, uint8_t b) {
            for (const Seen& s : log) if (s.pc == pc && s.b == b) return true;
            return false;
        };
        auto set_regs = [](Machine& m, uint16_t af, uint16_t bc, bool incdecz) {
            Z80Registers r = m.emu.cpu().get_registers();
            r.AF = af; r.BC = bc; r.IncDecZ = incdecz ? 1 : 0;
            m.emu.cpu().set_registers(r);
        };
        {
            auto mp = setup(0x02);
            Machine& m = *mp;
            m.emu.mmu().write(0x0007, 0xED); m.emu.mmu().write(0x0008, 0x23);   // SWAPNIB
            set_regs(m, 0x1200, 0, false);
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x0007);
            (void)step_pair(m);
            const auto& t = m.emu.trace_log();
            const TraceEntry& e = t.at(t.size() - 1);
            check("SRCDBG-ID-38", "ED at $0007 with its Z80N opcode at a delayed DivMMC entry ($0008): "
                                  "the byte is RAM's, SWAPNIB runs (A = $21) and the trace shows ED 23",
                  (m.dbg->registers().AF >> 8) == 0x21 && m.pc() == 0x0009 &&
                      e.opcode_bytes[0] == 0xED && e.opcode_bytes[1] == 0x23 && e.opcode_len == 2,
                  "A=" + hex(m.dbg->registers().AF >> 8) + " " + m.where());
        }
        {
            // The mirror: the inner byte INSIDE the unmap window. The instruction at $1FF7 is the
            // DivMMC ROM's (held), its inner byte at $1FF8 still is (the hold the first M1 loaded
            // reaches an M1 as `held`), and only the cycles after that fetch are RAM's.
            auto mp = setup(0x01);
            Machine& m = *mp;
            m.dbg->nextreg_write(m.a, 0xBA, 0x01);
            m.dbg->nextreg_write(m.a, 0xBB, 0x40);
            m.emu.mmu().write(0x1FF8, 0x3F);          // RAM: SRL A, never run
            uint8_t* drom = m.emu.divmmc().rom_data();
            drom[0] = 0xC3; drom[1] = 0xF7; drom[2] = 0x1F;   // JP $1FF7
            drom[0x1FF7] = 0xCB; drom[0x1FF8] = 0x07;          // RLC A
            set_regs(m, 0x8100, 0, false);
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x0000);
            (void)step_pair(m); (void)step_pair(m);
            const auto& t = m.emu.trace_log();
            const TraceEntry& e = t.at(t.size() - 1);
            check("SRCDBG-ID-43", "CB at $1FF7 with its inner byte inside the unmap window: the byte is "
                                  "the DivMMC ROM's (RLC A, A = $03), and so is the trace's (CB 07)",
                  (m.dbg->registers().AF >> 8) == 0x03 && m.pc() == 0x1FF9 && e.pc == 0x1FF7 &&
                      e.opcode_bytes[0] == 0xCB && e.opcode_bytes[1] == 0x07 && e.opcode_len == 2,
                  "A=" + hex(m.dbg->registers().AF >> 8) + " " + m.where());
        }
        {
            // DD CB d op across the unmap: DD at $1FF7 and CB at $1FF8 are the DivMMC ROM's (the CB
            // M1 loads the released hold), d and op at $1FF9/$1FFA are plain data reads of RAM.
            auto mp = setup(0x01);
            Machine& m = *mp;
            m.dbg->nextreg_write(m.a, 0xBA, 0x01);
            m.dbg->nextreg_write(m.a, 0xBB, 0x40);
            m.emu.mmu().write(0x1FF9, 0x05); m.emu.mmu().write(0x1FFA, 0x06);   // RAM: RLC (IX+5)
            uint8_t* drom = m.emu.divmmc().rom_data();
            drom[0] = 0xC3; drom[1] = 0xF7; drom[2] = 0x1F;   // JP $1FF7
            drom[0x1FF7] = 0xDD; drom[0x1FF8] = 0xCB;
            drom[0x1FF9] = 0x07; drom[0x1FFA] = 0x0E;          // RRC (IX+7), not run
            m.emu.mmu().write(0xA005, 0x01); m.emu.mmu().write(0xA007, 0x01);
            Z80Registers r = m.emu.cpu().get_registers();
            r.IX = 0xA000;
            m.emu.cpu().set_registers(r);
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x0000);
            (void)step_pair(m); (void)step_pair(m);
            const auto& t = m.emu.trace_log();
            const TraceEntry& e = t.at(t.size() - 1);
            check("SRCDBG-ID-44", "DD CB d op across the unmap: d and op are no M1, they come from RAM "
                                  "(RLC (IX+5)) and the trace shows DD CB 05 06",
                  m.emu.mmu().read(0xA005) == 0x02 && m.emu.mmu().read(0xA007) == 0x01 &&
                      m.pc() == 0x1FFB && e.opcode_bytes[0] == 0xDD && e.opcode_bytes[1] == 0xCB &&
                      e.opcode_bytes[2] == 0x05 && e.opcode_bytes[3] == 0x06 && e.opcode_len == 4,
                  "(IX+5)=" + hex(m.emu.mmu().read(0xA005)) + " " + m.where());
        }
        {
            auto mp = setup(0x02);
            Machine& m = *mp;
            std::vector<Seen> log;
            spy(m, log);
            m.emu.mmu().write(0x0007, 0xCB); m.emu.mmu().write(0x0008, 0x07);   // RLC A
            set_regs(m, 0x8100, 0, false);
            m.set_pc(0x0007);
            (void)step_pair(m);
            check("SRCDBG-ID-39", "CB at $0007, its inner byte at a delayed DivMMC entry: the M1 callback "
                                  "sees RAM's 07 at $0008, and RLC A runs",
                  saw(log, 0x0008, 0x07) && !saw(log, 0x0008, 0x00) && (m.dbg->registers().AF >> 8) == 0x03,
                  "A=" + hex(m.dbg->registers().AF >> 8));
        }
        {
            auto mp = setup(0x02);
            Machine& m = *mp;
            std::vector<Seen> log;
            spy(m, log);
            m.emu.mmu().write(0x0007, 0xDD); m.emu.mmu().write(0x0008, 0x0B);   // DD DEC BC
            set_regs(m, 0, 0x0001, true);
            m.set_pc(0x0007);
            (void)step_pair(m);
            check("SRCDBG-ID-40", "DD at $0007, DEC BC at a delayed DivMMC entry: the M1 callback sees "
                                  "RAM's 0B and IncDecZ follows BC (0)",
                  saw(log, 0x0008, 0x0B) && !saw(log, 0x0008, 0x00) &&
                      m.emu.cpu().get_registers().BC == 0 && m.emu.cpu().get_registers().IncDecZ == 0,
                  "BC=" + hex(m.emu.cpu().get_registers().BC) +
                      " IncDecZ=" + std::to_string(m.emu.cpu().get_registers().IncDecZ));
        }
        {
            auto mp = setup(0x02);
            Machine& m = *mp;
            std::vector<Seen> log;
            spy(m, log);
            m.emu.mmu().write(0x0006, 0xDD); m.emu.mmu().write(0x0007, 0xED);
            m.emu.mmu().write(0x0008, 0xA0);                                   // DD ED LDI
            set_regs(m, 0, 0x0001, true);
            Z80Registers r = m.emu.cpu().get_registers();
            r.HL = 0x9000; r.DE = 0x9100;
            m.emu.cpu().set_registers(r);
            m.set_pc(0x0006);
            (void)step_pair(m);
            check("SRCDBG-ID-41", "DD ED at $0006/$0007, LDI at a delayed DivMMC entry: the M1 callback "
                                  "sees RAM's A0 at $0008 and IncDecZ follows BC (0)",
                  saw(log, 0x0008, 0xA0) && !saw(log, 0x0008, 0x00) &&
                      m.emu.cpu().get_registers().BC == 0 && m.emu.cpu().get_registers().IncDecZ == 0,
                  "BC=" + hex(m.emu.cpu().get_registers().BC) +
                      " IncDecZ=" + std::to_string(m.emu.cpu().get_registers().IncDecZ));
        }
        {
            // The M1 of the byte after an ED inside a DD/FD chain loads the hold like any other
            // (divmmc.vhd:127-131): `DD ED A0` at $0006 with the A0 at a delayed entry ($0008) is
            // RAM's, and the NEXT instruction, at $0009, is the DivMMC ROM's (LD A,$77), not RAM's NOP.
            auto mp = setup(0x02);
            Machine& m = *mp;
            m.emu.mmu().write(0x0006, 0xDD); m.emu.mmu().write(0x0007, 0xED);
            m.emu.mmu().write(0x0008, 0xA0);                                   // DD ED LDI
            uint8_t* drom = m.emu.divmmc().rom_data();
            drom[0x0009] = 0x3E; drom[0x000A] = 0x77;                          // LD A,$77
            set_regs(m, 0, 0x0001, true);
            Z80Registers r = m.emu.cpu().get_registers();
            r.HL = 0x9000; r.DE = 0x9100;
            m.emu.cpu().set_registers(r);
            m.set_pc(0x0006);
            (void)step_pair(m);                       // DD ED LDI
            (void)step_pair(m);                       // the instruction at $0009
            check("SRCDBG-ID-45", "DD ED xx with xx at a delayed DivMMC entry: that M1 loads the hold, "
                                  "so the next instruction is the DivMMC ROM's (LD A,$77, PC = $000B)",
                  (m.dbg->registers().AF >> 8) == 0x77 && m.pc() == 0x000B,
                  "A=" + hex(m.dbg->registers().AF >> 8) + " " + m.where());
        }
        {
            // The predictor follows the chain past the first inner byte: `DD ED xx` with xx at an
            // INSTANT entry ($0008): the third M1 maps the DivMMC itself (zxnext.vhd:4116-4125), so xx
            // is the ROM's A0 (LDI) and the trace shows DD ED A0; RAM's 00 at $0008 is not executed.
            auto mp = setup(0x02);
            Machine& m = *mp;
            m.dbg->nextreg_write(m.a, 0xBA, 0x02);   // instant
            m.emu.mmu().write(0x0006, 0xDD); m.emu.mmu().write(0x0007, 0xED);
            m.emu.divmmc().rom_data()[0x0008] = 0xA0;
            set_regs(m, 0, 0x0001, true);
            Z80Registers r = m.emu.cpu().get_registers();
            r.HL = 0x9000; r.DE = 0x9100;
            m.emu.cpu().set_registers(r);
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x0006);
            (void)step_pair(m);
            const auto& t = m.emu.trace_log();
            const TraceEntry& e = t.at(t.size() - 1);
            check("SRCDBG-ID-46", "DD ED xx with xx at an instant DivMMC entry: xx is the ROM's A0 - LDI "
                                  "runs (BC = 0) and the trace shows DD ED A0, three bytes long",
                  m.emu.cpu().get_registers().BC == 0 && m.pc() == 0x0009 && e.opcode_bytes[0] == 0xDD &&
                      e.opcode_bytes[1] == 0xED && e.opcode_bytes[2] == 0xA0 && e.opcode_len == 3,
                  "BC=" + hex(m.emu.cpu().get_registers().BC) + " " + m.where() + " bytes=" + hex(e.opcode_bytes[0]) + hex(e.opcode_bytes[1]) + hex(e.opcode_bytes[2]));
        }
        {
            // A redundant DD/FD is a one-byte prefix in front of an instruction of its own (FUSE
            // z80_ddfd.c default: back up and re-dispatch), so the trace's length is the chain's:
            // DD DD LD A,n and DD FD LD IY,nn, plain RAM at $8000.
            Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
            const uint8_t prog[] = {0xDD, 0xDD, 0x3E, 0x55,         // DD DD LD A,$55
                                    0xDD, 0xFD, 0x21, 0x34, 0x12};   // DD FD LD IY,$1234
            for (size_t i = 0; i < sizeof prog; ++i)
                m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x8000);
            (void)step_pair(m);
            const auto& t = m.emu.trace_log();
            const TraceEntry first = t.at(t.size() - 1);
            const bool a_ok = (m.dbg->registers().AF >> 8) == 0x55 && m.pc() == 0x8004;
            (void)step_pair(m);
            const TraceEntry& second = t.at(t.size() - 1);
            check("SRCDBG-ID-48", "redundant DD/FD prefixes: the trace's length is what the CPU runs "
                                  "(DD DD LD A,n = 4, DD FD LD IY,nn = 5)",
                  a_ok && first.opcode_len == 4 && second.opcode_len == 5 && m.pc() == 0x8009 &&
                      m.dbg->registers().IY == 0x1234,
                  "len " + std::to_string(first.opcode_len) + "/" + std::to_string(second.opcode_len) +
                      " " + m.where());
        }
        {
            // The disassembler's length is the true one for a redundant-prefix chain: ZRCP's
            // cpu-step-over runs to pc + instruction_length(), the panel and the ZRCP disassembler
            // advance by byte_count; a clamp at 4 lands in the middle of `DD FD LD IY,nn`.
            Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
            const uint8_t prog[] = {0xDD, 0xFD, 0x21, 0x34, 0x12, 0x00};   // DD FD LD IY,$1234 ; NOP
            for (size_t i = 0; i < sizeof prog; ++i)
                m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
            const auto lines = m.dbg->disassemble(0x8000, 2, nullptr);
            check("SRCDBG-ID-49", "DD FD LD IY,nn: the disassembler's byte_count and the debugger's "
                                  "instruction_length are 5, and the next line starts at $8005",
                  lines.size() == 2 && lines[0].byte_count == 5 && lines[0].bytes[3] == 0x34 &&
                      m.dbg->instruction_length(0x8000) == 5 && lines[1].addr == 0x8005,
                  lines.empty() ? std::string("no lines")
                                : "count=" + std::to_string(lines[0].byte_count) + " len=" +
                                      std::to_string(m.dbg->instruction_length(0x8000)));
        }
        {
            // GH #375: the disassembly names an address in the memory that supplies it NOW. Two
            // labels at $C000, assembled for pages 20 and 30; MMU slot 6 (NR 0x56) picks one.
            Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
            SymbolTable syms;
            syms.add_label("banked", 0xC000, MemSpace::page(20));
            syms.add_label("other_bank", 0xC000, MemSpace::page(30));
            const uint8_t prog[] = {0xCD, 0x00, 0xC0};   // CALL $C000
            for (size_t i = 0; i < sizeof prog; ++i)
                m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
            const auto call_at = [&](uint8_t page) {
                m.dbg->nextreg_write(m.a, 0x56, page);
                return std::string(m.dbg->disassemble(0x8000, 1, &syms)[0].mnemonic);
            };
            const std::string a = call_at(20), b = call_at(30), c = call_at(21);
            check("SRCDBG-SYM-11", "disassemble() reads CALL $C000 as banked, other_bank or neither, by the page "
                                   "mapped at slot 6",
                  a.find("banked") != std::string::npos && a.find("other") == std::string::npos &&
                      b.find("other_bank") != std::string::npos &&
                      c.find("banked") == std::string::npos && c.find("other_bank") == std::string::npos &&
                      c.find("C000") != std::string::npos,
                  a + " / " + b + " / " + c);
        }
        {
            // Memory full of DD / FD bytes (or the address space wrapping onto one) must not recurse
            // once per byte: the chain is bounded at 64 redundant prefixes, the next prefix byte is a
            // line of its own (DB $DD), so one line is 65 bytes long.
            auto all_dd = [](uint16_t) -> uint8_t { return 0xDD; };
            auto all_fd = [](uint16_t) -> uint8_t { return 0xFD; };
            const DisasmLine a = disasm_one(0x1234, all_dd);
            const DisasmLine b = disasm_one(0x0000, all_fd);
            check("SRCDBG-ID-50", "a run of DD or FD bytes disassembles to one bounded line (65 bytes, "
                                  "ending in DB) and instruction_length agrees",
                  a.byte_count == 65 && b.byte_count == 65 && instruction_length(0x1234, all_dd) == 65 &&
                      std::string(a.mnemonic) == "DB $DD" && std::string(b.mnemonic) == "DB $FD",
                  "counts " + std::to_string(a.byte_count) + "/" + std::to_string(b.byte_count) + " '" +
                      a.mnemonic + "'");
        }
        {
            // The Multiface's 0x0066 latch across a prefix chain: CB at $0065 (RAM), the M1 of the inner
            // byte at $0066 latches the armed Multiface (multiface.vhd:169-186), so that byte is the
            // Multiface ROM's 3F (SRL A) and the trace shows CB 3F; RAM's 07 (RLC A) is not run.
            Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
            m.dbg->nextreg_write(m.a, 0x50, 10);
            m.emu.multiface().set_enabled(true);
            m.emu.mmu().write(0x0065, 0xCB); m.emu.mmu().write(0x0066, 0x07);
            m.emu.ram().page_ptr(0x0A)[0x66] = 0x3F;   // Multiface ROM $0066: SRL A
            Z80Registers r = m.emu.cpu().get_registers();
            r.AF = 0x8100;
            m.emu.cpu().set_registers(r);
            m.emu.multiface().button_press();
            m.dbg->set_trace_enabled(true);
            m.set_pc(0x0065);
            (void)step_pair(m);
            const auto& t = m.emu.trace_log();
            const TraceEntry& e = t.at(t.size() - 1);
            check("SRCDBG-ID-47", "CB at $0065, its inner byte at $0066 latching an armed Multiface: the "
                                  "byte is the Multiface ROM's (SRL A, A = $40) and the trace shows CB 3F",
                  (m.dbg->registers().AF >> 8) == 0x40 && e.pc == 0x0065 && e.opcode_bytes[0] == 0xCB &&
                      e.opcode_bytes[1] == 0x3F && e.opcode_len == 2,
                  "A=" + hex(m.dbg->registers().AF >> 8) + " " + m.where());
        }
        {
            auto mp = setup(0x02);
            Machine& m = *mp;
            int hits = 0;
            m.emu.cpu().on_magic_breakpoint = [&hits](uint16_t) { ++hits; return true; };
            m.emu.mmu().write(0x0007, 0xDD); m.emu.mmu().write(0x0008, 0x01);   // DD 01: magic
            m.set_pc(0x0007);
            (void)step_pair(m);
            check("SRCDBG-ID-42", "DD 01 with the 01 at a delayed DivMMC entry: RAM's byte, so the magic "
                                  "breakpoint fires",
                  hits == 1 && m.pc() == 0x0009, "hits=" + std::to_string(hits) + " " + m.where());
        }
    }
    {
        // The live gate (execute_gate, run while a machine RUNS with an Execute subscription) takes
        // the same prediction as probe_execute (ID-02, ID-06): at an instant DivMMC entry the fetch
        // at $0000 is the DivMMC ROM's, so a space-Page{10} subscription on RAM page 10 there is
        // refused and a plain page qualifier, which compares numbers alone, still matches.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);
        m.dbg->nextreg_write(m.a, 0xB8, 0x01);
        m.dbg->nextreg_write(m.a, 0xB9, 0x01);
        m.dbg->nextreg_write(m.a, 0xBA, 0x01);
        m.emu.divmmc().rom_data()[0] = 0x3C;      // INC A
        m.emu.divmmc().rom_data()[1] = 0xC9;      // RET
        m.emu.mmu().write(0x0000, 0x00);          // RAM page 10: NOP
        m.emu.mmu().write(0x8000, 0xCD);          // CALL $0000
        m.emu.mmu().write(0x8001, 0x00);
        m.emu.mmu().write(0x8002, 0x00);
        m.set_pc(0x8000);
        int ram_only_hits = 0, plain_hits = 0;
        Subscription ram_only;
        ram_only.kind = EventKind::Execute;
        ram_only.filter.lo = ram_only.filter.hi = 0x0000;
        ram_only.filter.page = 10;
        ram_only.filter.space = MemSpace::page(10);
        ram_only.action = Action::Continue;
        ram_only.handler = [&ram_only_hits](const jnext::dbg::Event&, Debugger&) {
            ++ram_only_hits;
            return Action::Continue;
        };
        Subscription plain = ram_only;
        plain.filter.space = MemSpace::cpu();
        plain.handler = [&plain_hits](const jnext::dbg::Event&, Debugger&) {
            ++plain_hits;
            return Action::Continue;
        };
        m.dbg->subscribe(m.a, ram_only);
        m.dbg->subscribe(m.a, plain);
        m.dbg->run(m.a);
        m.emu.run_frame();
        check("SRCDBG-ID-21", "while running, an Execute gate at an instant DivMMC entry refuses a "
                              "space-qualified subscription on the RAM page there and delivers the plain "
                              "page-qualified one",
              ram_only_hits == 0 && plain_hits >= 1,
              "ram_only=" + std::to_string(ram_only_hits) + " plain=" + std::to_string(plain_hits));
    }
    {
        // PURE-01/02 -- asking is not doing. The prediction functions run inside the
        // guest's own execution scope, where a READ watch, a Mem subscription and the +3
        // floating-bus latch are all live, over probes that hit every overlay, and the
        // machine must not notice: the whole save-state stream (memory, Mmu incl. the
        // latch, the DivMMC and Multiface state machines, NextREGs) is byte-identical,
        // no watchpoint fired, nothing entered the CAP-EVT ring. PURE-02 is the control:
        // ONE guest read of an address in the same fixture moves all three, so PURE-01
        // cannot be passing because nothing in the fixture is live.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        auto& emu = m.emu;
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);
        m.dbg->nextreg_write(m.a, 0xB8, 0xFF);
        m.dbg->nextreg_write(m.a, 0xB9, 0x0F);
        m.dbg->nextreg_write(m.a, 0xBA, 0x03);
        m.dbg->nextreg_write(m.a, 0xBB, 0xFF);
        emu.divmmc().set_button_nmi(true);
        emu.multiface().set_enabled(true);
        emu.multiface().button_press();
        m.dbg->port_out(m.a, 0x123B, 0xC4);
        emu.mmu().set_machine_timing(MachineTimingMode::TimingPlus3);
        m.dbg->nextreg_write(m.a, 0x52, 0x08);                  // MMU2 = 0x08: contended
        emu.ram().page_ptr(0x32)[0] = 0xC7;                     // Layer 2 at $4000: bank 8 + segment 1 = page 0x12, SRAM 0x32
        emu.mmu().set_p3_floating_bus_dat(0x3C);
        const uint16_t probes[] = {0x0000, 0x0008, 0x0066, 0x1FF8, 0x3D00, 0x4000, 0x8000};
        for (uint16_t p : probes) emu.debug_state().breakpoints().add_watchpoint(p, WatchType::READ);
        Subscription sub;
        sub.kind = EventKind::Mem;
        sub.access = Access::Read;
        sub.filter.lo = 0x0000;
        sub.filter.hi = 0xFFFF;
        sub.action = Action::Continue;
        m.dbg->subscribe(m.a, sub);

        auto stream = [&] {
            StateWriter measure;
            emu.save_state(measure);
            std::vector<uint8_t> bytes(measure.position());
            StateWriter w(bytes.data(), bytes.size());
            emu.save_state(w);
            return bytes;
        };
        auto ring = [&] { return emu.debug_state().event_table()->size(); };
        const std::vector<uint8_t> before = stream();
        const size_t ring_before = ring();
        uint32_t sink = 0;
        {
            DebugState::GuestExecutionScope guest(emu.debug_state());
            for (uint16_t p : probes) {
                sink += m.dbg->source_page(p).index;
                sink += emu.predict_fetch(p).index;
                sink += emu.mmu().route(p).offset;
                for (int t = 0; t < 4; ++t)
                    sink += emu.mmu().route_with(p, (t & 1) != 0, (t & 2) != 0).offset;
                sink += emu.multiface().would_latch_on_m1(p) ? 1u : 0u;
                for (int g = 0; g < 4; ++g)
                    sink += emu.divmmc().active_on_m1(p, (g & 1) != 0, (g & 2) != 0, false, false) ? 1u : 0u;
            }
        }
        const std::vector<uint8_t> after = stream();
        const bool untouched = before == after && !emu.debug_state().data_bp_hit() &&
                               ring() == ring_before && emu.mmu().p3_floating_bus_dat() == 0x3C;
        check("SRCDBG-PURE-01", "source_page, predict_fetch, route, route_with, would_latch_on_m1 and "
                                "active_on_m1 over every overlay, inside the guest's scope with watches "
                                "armed, change nothing: the save stream, the latch, the watch flag and "
                                "the event ring are as they were",
              untouched,
              "sink=" + std::to_string(sink) + " stream_equal=" + std::to_string(before == after) +
                  " hit=" + std::to_string(emu.debug_state().data_bp_hit()) + " ring=" +
                  std::to_string(ring_before) + "->" + std::to_string(ring()) + " latch=" +
                  hex(emu.mmu().p3_floating_bus_dat()));

        uint8_t byte = 0;
        {
            DebugState::GuestExecutionScope guest(emu.debug_state());
            byte = emu.mmu().read(0x4000);
        }
        const std::vector<uint8_t> moved = stream();
        const bool latch_moved = emu.mmu().p3_floating_bus_dat() == 0xC7;
        check("SRCDBG-PURE-02", "control for PURE-01: ONE guest read of $4000 in the same fixture moves "
                                "the floating-bus latch, raises the watchpoint and enters the event "
                                "ring, and the save stream sees it",
              byte == 0xC7 && latch_moved && emu.debug_state().data_bp_hit() && ring() > ring_before &&
                  moved != before,
              "byte=" + hex(byte) + " latch=" + hex(emu.mmu().p3_floating_bus_dat()) + " hit=" +
                  std::to_string(emu.debug_state().data_bp_hit()) + " ring=" + std::to_string(ring()) +
                  " stream_equal=" + std::to_string(moved == before));
    }

    // ── a step ends at DI:HALT, and within its frame budget ─────────────
    {
        Machine m;
        m.emu.mmu().write(0x8000, 0xF3);   // DI      main.bas:10
        m.emu.mmu().write(0x8001, 0x76);   // HALT    (same statement, inside h.bas:10)
        const fs::path hs = dir / "halt.sld";
        write_text(hs, std::string("|SLD.data.version|1\n") + "h.bas|1||0" + kNextDevice +
                           "h.bas|10||0|" + std::to_string(p80) + "|32768|T|\n");
        m.dbg->load_source_map(hs.string(), false);
        const uint64_t c0 = m.emu.clock().get();
        m.dbg->source_step(m.a, Step::Into);
        const uint64_t spent = m.emu.clock().get() - c0;
        check("SRCDBG-HALT-01", "a step that reaches DI:HALT stops there, paused, at "
                                "once (within 3 frames of machine time)",
              m.pc() == 0x8001 && m.dbg->state().paused &&
                  spent < 3 * m.emu.timing().master_cycles_per_frame,
              "spent " + std::to_string(spent));
    }
    {
        Machine m;
        m.emu.mmu().write(0x8006, 0x18);   // JR $  main.bas:13
        m.emu.mmu().write(0x8007, 0xFE);
        m.dbg->load_source_map(sld.string(), false);
        m.set_pc(0x8006);
        const uint64_t c0 = m.emu.clock().get();
        m.dbg->source_step(m.a, Step::Into);
        const uint64_t frames =
            (m.emu.clock().get() - c0) / m.emu.timing().master_cycles_per_frame;
        check("SRCDBG-HALT-02", "a statement that never ends stops at the frame budget",
              frames <= static_cast<uint64_t>(Debugger::SOURCE_STEP_FRAME_LIMIT) &&
                  frames + 2 >= static_cast<uint64_t>(Debugger::SOURCE_STEP_FRAME_LIMIT),
              "frames " + std::to_string(frames));
    }

    // ── Execute delivery between the instructions of a step ─────────────
    {
        Machine m;
        m.dbg->load_source_map(sld.string(), false);
        int ran = 0;
        Subscription logger;
        logger.kind = EventKind::Execute;
        logger.filter.lo = logger.filter.hi = 0x8001;
        logger.action = Action::Continue;
        logger.handler = [&ran](const jnext::dbg::Event&, Debugger&) {
            ++ran;
            return Action::Continue;
        };
        m.dbg->subscribe(m.a, logger);
        m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-GATE-01", "a Continue handler on an instruction inside the step "
                                "runs, and the step carries on",
              ran == 1 && m.pc() == 0x8002, m.where() + " ran " + std::to_string(ran));
    }
    {
        Machine m;
        m.dbg->load_source_map(sld.string(), false);
        Subscription once = execute_stop(0x8001);
        once.once = true;
        const auto id = m.dbg->subscribe(m.a, once).value;
        m.dbg->source_step(m.a, Step::Into);
        const auto st = m.dbg->state();
        bool spent = false;
        for (const auto& si : m.dbg->subscriptions(true))
            if (si.id == id) spent = !si.live;
        check("SRCDBG-GATE-02", "a Stop inside the step is delivered: reason Breakpoint, "
                                "its `once` spent",
              m.pc() == 0x8001 && st.pause_reason.kind == PauseReason::Kind::Breakpoint &&
                  spent,
              m.where() + " reason " + std::to_string(static_cast<int>(st.pause_reason.kind)));
    }

    {
        // A handler that redirects: at $8001 (inside main.bas:10) it sends the PC to
        // $8005 (main.bas:12). The step must stop there, not run line 12.
        Machine m;
        m.dbg->load_source_map(sld.string(), false);
        Subscription jump;
        jump.kind = EventKind::Execute;
        jump.filter.lo = jump.filter.hi = 0x8001;
        jump.action = Action::Continue;
        const ClientId a = m.a;
        jump.handler = [a](const jnext::dbg::Event&, Debugger& d) {
            d.set_register(a, jnext::dbg::RegId::PC, 0x8005);
            return Action::Continue;
        };
        m.dbg->subscribe(m.a, jump);
        m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-GATE-03", "a handler that moves the PC onto the next statement ends "
                                "the step there",
              m.pc() == 0x8005, m.where());
    }

    // ── store ownership ─────────────────────────────────────────────────
    // In a directory of their own: the rows above leave a plain Memory.txt
    // in `dir`, which is every .nex's fallback there.
    const fs::path own = dir / "own";
    fs::create_directories(own);
    {
        Machine m;
        write_text(own / "own.nex", "");
        write_text(own / "own.Memory.txt", "8000: ._Main\n");
        m.dbg->load_program_sidecars((own / "own.nex").string());
        const auto failed = m.dbg->load_map((own / "no-such.map").string(),
                                            jnext::dbg::MapFormat::Z88dk);
        m.dbg->load_program_sidecars((own / "plain2.nex").string());
        check("SRCDBG-OWN-01", "a MAP load that fails changes nothing: the sidecar's "
                               "symbols are still the program's, cleared with it",
              !failed && m.dbg->symbols().empty());
    }
    {
        Machine m;
        const fs::path consts = own / "consts.map";
        write_text(consts, "__data_crt_head = $1234 ; const, local, , , , a.asm:1\n");
        m.dbg->load_map(consts.string(), jnext::dbg::MapFormat::Z88dk);
        write_text(own / "own2.nex", "");
        write_text(own / "own2.Memory.txt", "8000: ._Main\n");
        const auto r = m.dbg->load_program_sidecars((own / "own2.nex").string());
        check("SRCDBG-OWN-02", "a user MAP of constants only is still the user's: a "
                               "sidecar does not replace it",
              r.symbols == -1 && m.dbg->lookup_name("__data_crt_head") == 0x1234 &&
                  !m.dbg->lookup_name("Main"));
    }
    {
        // A Memory.txt that is there and cannot be loaded: a regular file
        // over the size cap, refused for every user, root included. Sparse,
        // so it costs no disk.
        Machine m;
        write_text(own / "own3.nex", "");
        write_text(own / "own3.Memory.txt", "8000: ._Old\n");
        m.dbg->load_program_sidecars((own / "own3.nex").string());
        const bool had_old = m.dbg->lookup_name("Old").has_value();
        write_text(own / "own4.nex", "");
        const fs::path big = own / "own4.Memory.txt";
        write_text(big, "8000: ._New\n");
        std::error_code ec;
        fs::resize_file(big, SOURCE_FILE_MAX_BYTES + 1, ec);
        const auto r = m.dbg->load_program_sidecars((own / "own4.nex").string());
        fs::remove(big);
        check("SRCDBG-OWN-03", "a sidecar Memory.txt that cannot be loaded (over the size "
                               "cap) is reported unreadable, and leaves none of the "
                               "previous program's symbols",
              had_old && !ec && r.symbols_unreadable && !m.dbg->lookup_name("Old"));
        const std::string why = "own4.Memory.txt' rejected: file is " +
                                std::to_string(SOURCE_FILE_MAX_BYTES + 1) + " bytes";
        check("SRCDBG-OWN-04", "the problem a frontend shows for it names the file and why: "
                               "its size over the cap",
              r.problem.find(why) != std::string::npos, r.problem);
    }
    {
        // Both sidecars refused in one load: the second reason is appended to
        // the first with "; " (symbols first, then the source map), not
        // written over it.
        Machine m;
        write_text(own / "own5.nex", "");
        const fs::path big = own / "own5.Memory.txt";
        write_text(big, "8000: ._Big\n");
        std::error_code ec;
        fs::resize_file(big, SOURCE_FILE_MAX_BYTES + 1, ec);
        write_text(own / "own5.sld", "this is not an SLD\n");
        const auto r = m.dbg->load_program_sidecars((own / "own5.nex").string());
        fs::remove(big);
        const size_t sep = r.problem.find("; ");
        const std::string head = r.problem.substr(0, sep);
        const std::string tail = sep == std::string::npos ? std::string() : r.problem.substr(sep + 2);
        check("SRCDBG-OWN-05", "a program whose Memory.txt and SLD are both refused reports "
                               "both: the symbols' reason, then \"; \", then the source map's",
              !ec && sep != std::string::npos && head.rfind("NextBuild symbols '", 0) == 0 &&
                  head.find("own5.Memory.txt' rejected: file is " +
                            std::to_string(SOURCE_FILE_MAX_BYTES + 1) + " bytes") !=
                      std::string::npos &&
                  tail.rfind("source map '", 0) == 0 &&
                  tail.find("own5.sld' rejected: missing SLD.data.version") != std::string::npos,
              r.problem);
    }
    // ── a tape program's map attaches at once (owner decision 2026-10-29) ──
    {
        Machine m;
        write_text(own / "tape.tap", "");
        write_text(own / "tape.sld", program_sld(p80, p90));
        const auto r = m.dbg->load_program_sidecars((own / "tape.tap").string());
        check("SRCDBG-TAPE-01", "an SLD with no identity beside a tape program is attached at "
                                "once, unverified (nothing to judge it by, so nothing pending)",
              r.sources == 6 && m.dbg->source_map().size() == 6 && !r.sources_pending &&
                  !m.dbg->source_map_status().pending);
        const auto byhand = m.dbg->load_source_map((own / "tape.sld").string(), false);
        check("SRCDBG-TAPE-02", "it can still be loaded by hand",
              byhand.count == 6 && m.dbg->source_map().size() == 6);
    }

    // ── sidecars belong to the load they came with ──────────────────────
    {
        // Frames of history before the "load", then the program's sidecars
        // attached, then more frames. Rewinding to a frame before the attach
        // point drops them; one after it keeps them.
        Machine m(/*rewind=*/true);
        m.dbg->run(m.a);
        for (int i = 0; i < 3; ++i) m.emu.run_frame();
        m.dbg->pause(m.a);
        write_text(own / "hist.nex", "");
        write_text(own / "hist.Memory.txt", "8000: ._Main\n");
        write_text(own / "hist.sld", program_sld(p80, p90));
        const auto r = m.dbg->load_program_sidecars((own / "hist.nex").string());
        const uint32_t attach_frame = m.dbg->time().frame;
        m.dbg->run(m.a);
        for (int i = 0; i < 3; ++i) m.emu.run_frame();
        m.dbg->pause(m.a);
        const bool attached = r.symbols == 1 && r.sources == 6;
        m.dbg->rewind_to_frame(m.a, attach_frame + 1);
        const bool kept = m.dbg->source_map().size() == 6 && m.dbg->lookup_name("Main");
        m.dbg->rewind_to_frame(m.a, attach_frame - 1);
        const bool dropped = m.dbg->source_map().empty() && !m.dbg->lookup_name("Main") &&
                             !m.dbg->source_location(0x8000);
        check("SRCDBG-HIST-01", "a rewind to after the program's load keeps its sidecars",
              attached && kept);
        check("SRCDBG-HIST-02", "a rewind to before it drops them: no source or symbol "
                                "of a program not yet loaded",
              attached && dropped);
    }
    {
        Machine m;
        m.dbg->run(m.a);
        m.emu.run_frame();
        m.dbg->pause(m.a);
        write_text(own / "hist2.nex", "");
        write_text(own / "hist2.sld", program_sld(p80, p90));
        StateWriter measure;
        m.emu.save_state(measure);
        std::vector<uint8_t> before_load(measure.position(), 0);
        // A state from BEFORE the load: the clock of an empty machine.
        Emulator other;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        other.init(cfg);
        StateWriter w(before_load.data(), before_load.size());
        other.save_state(w);
        m.dbg->load_program_sidecars((own / "hist2.nex").string());
        const bool attached = m.dbg->source_map().size() == 6;
        m.dbg->load_state_bytes(m.a, before_load.data(), before_load.size());
        check("SRCDBG-HIST-03", "a state restored from before the load drops the sidecars",
              attached && m.dbg->source_map().empty());
    }
    {
        Machine m;
        m.dbg->run(m.a);
        m.emu.run_frame();
        m.dbg->pause(m.a);
        write_text(own / "hist3.nex", "");
        write_text(own / "hist3.sld", program_sld(p80, p90));
        m.dbg->load_program_sidecars((own / "hist3.nex").string());
        const bool attached = m.dbg->source_map().size() == 6;
        m.dbg->pump(jnext::dbg::PumpBudget{});
        const bool kept = m.dbg->source_map().size() == 6;
        check("SRCDBG-HIST-04", "nothing happening to the machine keeps them",
              attached && kept);
    }

    // ── a reset or a restored state drops the sidecars ──────────────────
    // A program's sidecars describe the program loaded with them. A rewind-ring
    // restore goes back into that program's history (HIST-01/02, and HIST-08
    // below); anything else puts a machine in front of them that they may not
    // describe, whatever its clock says.
    const auto attach = [&](Machine& m, const std::string& stem) {
        write_text(own / (stem + ".nex"), "");
        write_text(own / (stem + ".Memory.txt"), "8000: ._Main\n");
        write_text(own / (stem + ".sld"), program_sld(p80, p90));
        return m.dbg->load_program_sidecars((own / (stem + ".nex")).string());
    };
    const auto with_cold_boot = [](Machine& m) {
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&m]() {
            emulator_frontend_cold_boot(m.emu, m.emu.config(), std::string(), ColdBootHooks{});
            return true;
        };
        m.dbg->set_loop_driver(d);
    };
    const auto sidecars_gone = [](Machine& m) {
        return m.dbg->source_map().empty() && m.dbg->symbols().empty() &&
               !m.dbg->source_location(0x8000);
    };
    {
        Machine m;
        const auto r = attach(m, "rst-soft");
        const bool attached = r.symbols == 1 && r.sources == 6;
        const Result rr = m.dbg->reset(m.a, ResetKind::Soft);
        check("SRCDBG-HIST-05", "a soft reset drops the program's sidecars: no source map, "
                                "no symbols",
              attached && rr == Result::Ok && sidecars_gone(m));
    }
    {
        Machine m;
        with_cold_boot(m);
        const auto r = attach(m, "rst-hard");
        const bool attached = r.symbols == 1 && r.sources == 6;
        const Result rr = m.dbg->reset(m.a, ResetKind::Hard);
        check("SRCDBG-HIST-06", "a hard reset (a cold boot of a rebuilt machine) drops the "
                                "program's sidecars too",
              attached && rr == Result::Ok && sidecars_gone(m));
    }
    {
        // The rebuilt machine's hook is installed by the re-application after a
        // cold boot, not by the constructor: sidecars attached to the rebuilt
        // machine are dropped by its next reset all the same.
        Machine m;
        with_cold_boot(m);
        m.dbg->reset(m.a, ResetKind::Hard);
        const auto r = attach(m, "rst-after-boot");
        const bool attached = r.symbols == 1 && r.sources == 6;
        const Result rr = m.dbg->reset(m.a, ResetKind::Soft);
        check("SRCDBG-HIST-10", "after a cold boot the rebuilt machine's reset still drops "
                                "the sidecars attached to it",
              attached && rr == Result::Ok && sidecars_gone(m));
    }
    {
        // A state from another machine, taken well after the sidecars were
        // attached: its clock is not before the attach, so only the kind of
        // landing can tell it is not this program's history.
        Machine m;
        const auto r = attach(m, "rst-load");
        const bool attached = r.symbols == 1 && r.sources == 6;
        const uint64_t attach_clock = m.emu.clock().get();
        Emulator other;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        other.init(cfg);
        for (int i = 0; i < 3; ++i) other.run_frame();
        StateWriter measure;
        other.save_state(measure);
        std::vector<uint8_t> foreign(measure.position(), 0);
        StateWriter w(foreign.data(), foreign.size());
        other.save_state(w);
        const Result lr = m.dbg->load_state_bytes(m.a, foreign.data(), foreign.size());
        check("SRCDBG-HIST-07", "a state restored from another machine drops the sidecars "
                                "even when its clock is past their load",
              attached && lr == Result::Ok && m.emu.clock().get() > attach_clock &&
                  sidecars_gone(m));
    }
    {
        // The same kind of landing (after the attach) twice: through the rewind
        // ring it stays this program's history and keeps the sidecars; as a
        // restored state it may be anything, and drops them.
        Machine m(/*rewind=*/true);
        m.dbg->run(m.a);
        for (int i = 0; i < 3; ++i) m.emu.run_frame();
        m.dbg->pause(m.a);
        const auto r = attach(m, "rst-ring");
        const uint32_t attach_frame = m.dbg->time().frame;
        m.dbg->run(m.a);
        for (int i = 0; i < 3; ++i) m.emu.run_frame();
        m.dbg->pause(m.a);
        StateWriter measure;
        m.emu.save_state(measure);
        std::vector<uint8_t> later(measure.position(), 0);
        StateWriter w(later.data(), later.size());
        m.emu.save_state(w);
        const bool attached = r.symbols == 1 && r.sources == 6;
        m.dbg->rewind_to_frame(m.a, attach_frame + 1);
        const bool ring_kept = m.dbg->source_map().size() == 6 && m.dbg->lookup_name("Main");
        const Result lr = m.dbg->load_state_bytes(m.a, later.data(), later.size());
        check("SRCDBG-HIST-08", "a landing after the attach keeps the sidecars when it comes "
                                "from the rewind ring and drops them when it is restored as "
                                "a state",
              attached && ring_kept && lr == Result::Ok && sidecars_gone(m));
    }
    {
        // What the user loaded is not the program's: a reset leaves the
        // symbols. The source map stays the user's too, but with no binary
        // identity it cannot be told to describe the program after the reset
        // (design §6.5): it goes stale, and loading it again makes it live.
        Machine m;
        with_cold_boot(m);
        const fs::path user_map = dir / "keep.map";
        write_text(user_map, "UserSym = $8005 ; addr, local\n");
        m.dbg->load_map(user_map.string(), jnext::dbg::MapFormat::Z88dk);
        m.dbg->load_source_map(sld.string(), false);
        using State = Debugger::SourceMapStatus::State;
        const Result soft = m.dbg->reset(m.a, ResetKind::Soft);
        const auto st_soft = m.dbg->source_map_status();
        const bool soft_kept = m.dbg->lookup_name("UserSym") == 0x8005 &&
                               st_soft.state == State::Stale && st_soft.by_user &&
                               st_soft.file == sld.string() && m.dbg->source_map().empty();
        m.dbg->load_source_map(sld.string(), false);
        const Result hard = m.dbg->reset(m.a, ResetKind::Hard);
        const auto st_hard = m.dbg->source_map_status();
        check("SRCDBG-HIST-09", "symbols the user loaded survive a soft and a hard reset; a "
                                "source map loaded by hand stays the user's but goes stale "
                                "(it has no binary identity to check)",
              soft == Result::Ok && hard == Result::Ok && soft_kept &&
                  m.dbg->lookup_name("UserSym") == 0x8005 && st_hard.state == State::Stale &&
                  st_hard.by_user && st_hard.file == sld.string() &&
                  m.dbg->source_map().empty());
    }

    // ── loops, links and relative paths ─────────────────────────────────
    {
        Machine m;
        const fs::path before = fs::current_path();
        fs::current_path(dir);
        const auto r = m.dbg->load_source_map("prog.sld", false);
        fs::current_path(before);
        const std::string f = m.dbg->source_map().loaded_file();
        // The same file, not the same spelling: macOS's temp dir is under /var, a
        // link to /private/var, and the working directory reads back resolved.
        std::error_code ec;
        check("SRCDBG-PATH-01", "a map loaded by a relative name records its absolute path",
              r.count == 6 && fs::path(f).is_absolute() && fs::equivalent(f, dir / "prog.sld", ec),
              f);
    }

    // ── Back after a Frame Back goes back in time ───────────────────────
    {
        Machine m(/*rewind=*/true);
        m.dbg->load_source_map(sld.string(), false);
        m.dbg->set_trace_enabled(true);
        m.dbg->run(m.a);
        for (int i = 0; i < 4; ++i) m.emu.run_frame();
        m.dbg->pause(m.a);
        const auto range = m.dbg->rewind_range();
        // The newest snapshot: the trace (10000 instructions) still covers
        // some of the frame before it, and all of the history after it.
        const uint32_t target = range.newest_frame;
        const bool rewound = m.dbg->rewind_to_frame(m.a, target) == Result::Ok;
        const uint64_t at = m.emu.clock().get();
        const Result r = m.dbg->source_step(m.a, Step::Back);
        const uint64_t after = m.emu.clock().get();
        check("SRCDBG-FB-01", "Back after a Frame Back lands earlier than the restored "
                              "point, never in the history it left",
              rewound && r == Result::Ok && after < at,
              "at " + std::to_string(at) + " after " + std::to_string(after) + " rc " +
                  std::to_string(static_cast<int>(r)));
    }
    {
        // After a Frame Back the trace still holds the instruction at the PC,
        // recorded at exactly this cycle in the history that was left. It has
        // not run yet: a breakpoint on it is not "back". Every address is
        // mapped, so wherever the frame boundary falls the PC has a position.
        const fs::path all = dir / "all-mapped.sld";
        write_text(all, program_sld(p80, p90,
                                    "main.bas|20||0|" + std::to_string(p80) + "|32769|T|\n" +
                                    "sub.bas|3||0|" + std::to_string(p90) + "|36868|T|\n"));
        Machine m(/*rewind=*/true);
        m.dbg->load_source_map(all.string(), false);
        m.dbg->set_trace_enabled(true);
        m.dbg->run(m.a);
        for (int i = 0; i < 4; ++i) m.emu.run_frame();
        m.dbg->pause(m.a);
        const bool rewound =
            m.dbg->rewind_to_frame(m.a, m.dbg->rewind_range().newest_frame) == Result::Ok;
        const uint64_t at = m.emu.clock().get();
        m.dbg->subscribe(m.a, execute_stop(m.pc()));
        const Result r = m.dbg->source_step(m.a, Step::ReverseContinue);
        const uint64_t after = m.emu.clock().get();
        check("SRCDBG-BACK-09", "after a Frame Back the trace entry at this very cycle is "
                                "not back: Reverse Continue to a breakpoint on the PC lands "
                                "on an earlier hit",
              rewound && r == Result::Ok && after < at,
              "at " + std::to_string(at) + " after " + std::to_string(after) + " rc " +
                  std::to_string(static_cast<int>(r)));
    }

    // ── the call-stack tracker records pages ────────────────────────────
    {
        Machine m;
        m.dbg->step_into(m.a);
        m.dbg->step_into(m.a);
        m.dbg->step_into(m.a);   // CALL taken
        // GH #340: the target is recorded at ITS first fetch, so one more step
        // runs the instruction at the target (the frame stays).
        m.dbg->step_into(m.a);
        const auto& f = m.dbg->call_stack();
        check("SRCDBG-CS-01", "a CALL's frame carries the caller's and the target's "
                              "physical pages",
              f.size() == 1 && f[0].caller_pc == 0x8002 &&
                  ram_page_of(f[0].caller_space) == p80 &&
                  f[0].target_pc == 0x9000 && ram_page_of(f[0].target_space) == p90);
    }
    {
        // An interrupt taken in the slot right after the tracker was enabled:
        // no opcode was captured before it, so the caller's page is not known.
        // That is NOT_RAM_PAGE, never RAM page 0 (which would show page 0's
        // line as the caller). With a capture, the frame has its page.
        CallStack cs;
        cs.set_enabled(true);
        cs.on_interrupt(0x8000, 0x0038, 0xFF00, CallType::INT);
        // GH #340: the target is recorded at the ISR's first fetch, fed here.
        cs.on_instruction_pre(0x0038, 0xFF00, 0x00, 0x00, 0x00);
        cs.on_instruction_post(0xFF00, 0x0039, MemSpace::page(p90));
        cs.on_instruction_pre(0x8002, 0xFF00, 0x00, 0x00, 0x00, MemSpace::page(p80));
        cs.on_interrupt(0x8002, 0x0038, 0xFEFE, CallType::INT);
        const auto& f = cs.frames();
        check("SRCDBG-CS-02", "an interrupt in the slot right after the tracker is enabled "
                              "records no RAM page for its caller; one after a capture "
                              "records the captured page",
              f.size() == 2 && f[0].caller_pc == 0x8000 &&
                  ram_page_of(f[0].caller_space) == NOT_RAM_PAGE &&
                  ram_page_of(f[0].target_space) == p90 && f[1].caller_pc == 0x8002 &&
                  ram_page_of(f[1].caller_space) == p80);
    }

    // ── GH #340 (E23-08): the identity recorded in history and call frames ──
    // The ZXN fixture: `page` mapped into MMU slot `nr - 0x50`, a byte string
    // written there, then the real mapping restored by the caller.
    auto poke_page = [](Machine& m, unsigned nr, unsigned page, uint16_t at,
                        std::initializer_list<uint8_t> bytes) {
        m.dbg->nextreg_write(m.a, static_cast<uint8_t>(nr), static_cast<uint8_t>(page));
        for (const uint8_t b : bytes) m.emu.mmu().write(at++, b);
    };
    auto divmmc_instant_machine = [&](Machine& m) {
        m.dbg->nextreg_write(m.a, 0x50, 10);
        m.dbg->nextreg_write(m.a, 0x0A, 0x10);   // automap enable
        m.dbg->nextreg_write(m.a, 0xB8, 0x01);
        m.dbg->nextreg_write(m.a, 0xB9, 0x01);
        m.dbg->nextreg_write(m.a, 0xBA, 0x01);   // instant entry at $0000
        m.emu.divmmc().rom_data()[0] = 0x3C;      // INC A
        m.emu.divmmc().rom_data()[1] = 0xC9;      // RET
        m.emu.mmu().write(0x8000, 0xCD);
        m.emu.mmu().write(0x8001, 0x00);
        m.emu.mmu().write(0x8002, 0x00);
        Z80Registers r = m.emu.cpu().get_registers();
        r.PC = 0x8000;
        r.AF = 0x0000;
        m.emu.cpu().set_registers(r);
        m.dbg->set_trace_enabled(true);
    };
    {
        // zxnext.vhd:3084-3092 / divmmc.vhd:147 (instant entry: the M1 at $0000 is DivMMC's)
        // and the MMU4 reset value (:4615): the entries name what the M1 recorded.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        divmmc_instant_machine(m);
        m.dbg->step_into(m.a);   // CALL $0000
        m.dbg->step_into(m.a);   // INC A, fetched from the DivMMC ROM
        const auto& t = m.emu.trace_log();
        const bool two = t.size() == 2;
        check("SRCDBG-ID-16", "a trace entry records the memory its instruction was fetched "
                              "from: the CALL in RAM page 4, DivMMC's instruction at the "
                              "instant entry point in the DivMMC ROM",
              two && t.at(0).pc == 0x8000 && t.at(0).fetch_space() == MemSpace::page(4) &&
                  t.at(1).pc == 0x0000 && t.at(1).fetch_space() == MemSpace::divmmc_rom(),
              "entries=" + std::to_string(t.size()));
        const fs::path out = dir / "trace-id-04.txt";
        const bool exported = m.dbg->trace_export(out.string()) == Result::Ok;
        std::string l0, l1;
        {
            std::ifstream f(out);
            std::getline(f, l0);
            std::getline(f, l1);
        }
        check("TRACE-ID-04", "the trace export names the fetch identity after ROM= as the space "
                             "tag, FETCH=P04 for RAM page 4, FETCH=DIVROM for the "
                             "DivMMC ROM",
              exported && l0.find("  $8000  ") != std::string::npos &&
                  l0.find(" FETCH=P04  [") != std::string::npos &&
                  l1.find("  $0000  ") != std::string::npos &&
                  l1.find(" FETCH=DIVROM  [") != std::string::npos,
              "'" + l0 + "' / '" + l1 + "'");
    }
    // ── GH #360 (E23-22) F1: the code at the PC is read as the CPU fetches it ──
    {
        // divmmc.vhd:147 (instant entry: the M1 at $0000 is DivMMC's), zxnext.vhd:3084:
        // the data view of slot 0 is still RAM page 10, the instruction is DivMMC's.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        divmmc_instant_machine(m);
        m.dbg->step_into(m.a);   // CALL $0000
        uint8_t pk = 0xEE;
        m.dbg->peek(MemSpace::cpu(), 0x0000, 1, &pk);
        const auto line = m.dbg->disassemble(0x0000, 1, nullptr);
        const std::string mn = line.empty() ? std::string() : std::string(line[0].mnemonic);
        check("LADDR-17", "at a DivMMC instant entry the memory reader returns the byte the CPU "
                          "fetches (DivMMC ROM 0x3C), while peek shows the data view (page 10, "
                          "0x00): the PC line disassembles as INC A",
              m.pc() == 0x0000 && m.dbg->memory_reader()(0x0000) == 0x3C && pk == 0x00 &&
                  mn == "INC A" && m.dbg->source_page(0x0000) == MemSpace::divmmc_rom() &&
                  m.dbg->mmu_slots()[0].now == MemSpace::page(10),
              "reader=" + hex(m.dbg->memory_reader()(0x0000)) + " peek=" + hex(pk) + " '" + mn + "'");
    }
    {
        // As LADDR-17: step_over() asks is_call_like() / instruction_length() of the reader
        // (debugger_control.cpp). The DivMMC ROM holds CALL $0010 at 0 and RET at $10.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        divmmc_instant_machine(m);
        m.emu.divmmc().rom_data()[0] = 0xCD;
        m.emu.divmmc().rom_data()[1] = 0x10;
        m.emu.divmmc().rom_data()[2] = 0x00;
        m.emu.divmmc().rom_data()[0x10] = 0xC9;
        // RAM page 10 (the data view) holds other bytes at $0001/$0002: the CALL's operands
        // are DivMMC's, and the length of the PC instruction is the predicted one (3), not
        // that of the RAM's byte (1).
        m.emu.mmu().write(0x0001, 0xAA);
        m.emu.mmu().write(0x0002, 0xBB);
        m.set_pc(0x0000);
        const auto rd = m.dbg->memory_reader();
        const auto dl = m.dbg->disassemble(0x0000, 1, nullptr);
        const std::string dm = dl.empty() ? std::string() : std::string(dl[0].mnemonic);
        const bool operands = rd(1) == 0x10 && rd(2) == 0x00 && dl.size() == 1 &&
                              dl[0].byte_count == 3 && dm.find("CALL") == 0 &&
                              dm.find("0010") != std::string::npos;
        const bool call = m.dbg->is_call_like(0x0000);
        const Result rc = m.dbg->step_over(m.a);
        for (int i = 0; i < 3 && !m.dbg->state().paused; ++i) m.emu.run_frame();
        const uint16_t after = m.pc();
        // Bytes past the PC instruction are the data view's: INC A is one byte long,
        // so address 1 reads page 10's byte, not the ROM's RET.
        Machine n(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        divmmc_instant_machine(n);
        n.emu.mmu().write(0x0001, 0xAA);
        n.set_pc(0x0000);
        check("LADDR-18", "Step Over at a DivMMC instant entry recognises the CALL the CPU "
                          "will run (stops at $0003); past a one-byte instruction the reader "
                          "returns the data view",
              call && operands && rc == Result::Ok && after == 0x0003 &&
                  n.dbg->memory_reader()(0x0000) == 0x3C && n.dbg->memory_reader()(0x0001) == 0xAA,
              "call=" + std::to_string(call) + " operands=" + std::to_string(operands) + " '" + dm + "' pc=" + hex(after) + " b1=" +
                  hex(n.dbg->memory_reader()(0x0001)));
    }
    {
        // divmmc.vhd:124-148 / design 4.4: with a DELAYED entry the opcode is fetched from
        // RAM (the M1 itself is not yet DivMMC's) and the operand bytes after it from the
        // DivMMC ROM. The executed result (HL) is the independent witness of the prediction.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        divmmc_instant_machine(m);
        m.dbg->nextreg_write(m.a, 0xBA, 0x00);   // delayed entry at $0000
        m.emu.mmu().write(0x0000, 0x21);          // LD HL,nn in RAM page 10
        m.emu.mmu().write(0x0001, 0x34);
        m.emu.mmu().write(0x0002, 0x12);
        m.emu.divmmc().rom_data()[0] = 0x00;
        m.emu.divmmc().rom_data()[1] = 0x78;
        m.emu.divmmc().rom_data()[2] = 0x56;
        m.set_pc(0x0000);
        const auto rd = m.dbg->memory_reader();
        const uint8_t r0 = rd(0), r1 = rd(1), r2 = rd(2);
        uint8_t pk[3] = {};
        m.dbg->peek(MemSpace::cpu(), 0x0000, 3, pk);
        m.dbg->step_into(m.a);
        check("LADDR-19", "with a delayed DivMMC entry the reader gives the RAM opcode (0x21) and "
                          "the DivMMC ROM operand bytes (0x78 0x56), which is what the CPU "
                          "executed (HL), while peek shows the RAM's",
              r0 == 0x21 && r1 == 0x78 && r2 == 0x56 && pk[1] == 0x34 && pk[2] == 0x12 &&
                  m.dbg->registers().HL == 0x5678,
              hex(r0) + " " + hex(r1) + " " + hex(r2) + " HL=" + hex(m.dbg->registers().HL));
    }
    {
        // zxnext.vhd:3084-3098 (DivMMC RAM), :2966-2971 (Layer 2), :1856-1857 (boot ROM):
        // each entry's FETCH= names the memory the instruction came from.
        auto fetch_of = [&](Machine& m, const char* name) {
            const fs::path out = dir / name;
            m.dbg->trace_clear();
            m.dbg->step_into(m.a);
            m.dbg->trace_export(out.string());
            std::string l;
            std::ifstream f(out);
            std::getline(f, l);
            return l;
        };
        Machine a(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        a.emu.mmu().set_boot_rom_enabled(false);
        a.emu.port().out(0x00E3, 0x83);    // DivMMC conmem, bank 3: $2000-$3FFF is DivMMC RAM 3
        a.set_pc(0x2000);
        a.dbg->set_trace_enabled(true);
        const std::string la = fetch_of(a, "ladd-20a.txt");
        Machine b(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        b.emu.mmu().set_boot_rom_enabled(false);
        b.dbg->nextreg_write(b.a, 0x12, 9);
        b.emu.port().out(0x123B, 0x04);    // Layer 2 read mapping, segment 0
        b.set_pc(0x0000);
        b.dbg->set_trace_enabled(true);
        const std::string lb = fetch_of(b, "ladd-20b.txt");
        Machine c(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        static const uint8_t boot[0x2000] = {};
        c.emu.mmu().set_boot_rom(boot, sizeof(boot));
        c.emu.mmu().set_boot_rom_enabled(true);
        c.set_pc(0x0000);
        c.dbg->set_trace_enabled(true);
        const std::string lc = fetch_of(c, "ladd-20c.txt");
        check("LADDR-20", "the trace export spells DivMMC RAM 3, the Layer 2 mapping and the "
                          "boot ROM as FETCH=DIVRAM3, FETCH=L2P12 and FETCH=BOOT",
              has(la, " FETCH=DIVRAM3  [") && has(lb, " FETCH=L2P12  [") &&
                  has(lc, " FETCH=BOOT  ["),
              "'" + la + "' / '" + lb + "' / '" + lc + "'");
    }
    {
        // zxnext.vhd:4688 (the NextREG write), :2952/:2964/:3060 (slot -> page):
        // JP $0000 into page 0x10, whose NEXTREG $50,$11 remaps slot 0 AS IT RUNS.
        // The entry for the NEXTREG instruction is the page it was fetched from
        // (0x10), not the page the slot holds once it has run (0x11).
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        poke_page(m, 0x50, 0x11, 0x0004, {0x00});
        poke_page(m, 0x50, 0x10, 0x0000, {0xED, 0x91, 0x50, 0x11});
        m.emu.mmu().write(0x8000, 0xC3);
        m.emu.mmu().write(0x8001, 0x00);
        m.emu.mmu().write(0x8002, 0x00);
        m.set_pc(0x8000);
        m.dbg->set_trace_enabled(true);
        m.dbg->step_into(m.a);   // JP $0000
        m.dbg->step_into(m.a);   // NEXTREG $50,$11
        const bool remapped = m.dbg->source_page(0x0000) == MemSpace::page(0x11);
        m.dbg->step_into(m.a);   // NOP at $0004, now from page 0x11
        const auto& t = m.emu.trace_log();
        const bool three = t.size() == 3;

        // The same through a port write, which takes effect INSIDE the instruction (a NEXTREG
        // is applied after the slot): OUT (C),A to $7FFD from page 0x12 at $C000 puts bank 3
        // (pages 6 and 7) in slot 6 before the slot ends. A prediction made after the
        // instruction would name page 6.
        Machine n(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        poke_page(n, 0x56, 0x12, 0xC000, {0xED, 0x79});
        Z80Registers r = n.emu.cpu().get_registers();
        r.PC = 0xC000; r.BC = 0x7FFD; r.AF = 0x0300;
        n.emu.cpu().set_registers(r);
        n.dbg->set_trace_enabled(true);
        n.dbg->step_into(n.a);   // OUT (C),A
        const auto& u = n.emu.trace_log();
        const bool port_ok = u.size() == 1 && n.dbg->source_page(0xC000) == MemSpace::page(6) &&
                             u.at(0).fetch_space() == MemSpace::page(0x12);
        check("SRCDBG-ID-17", "an instruction that remaps its own slot is recorded with the "
                              "page it was fetched from, not the page it left behind: NEXTREG "
                              "$50,$11 run from page 0x10, then the next fetch from page 0x11; "
                              "and OUT to $7FFD from page 0x12 that puts pages 6 and 7 in its slot",
              port_ok && three && remapped && t.at(0).fetch_space() == MemSpace::page(4) &&
                  t.at(1).pc == 0x0000 && t.at(1).fetch_space() == MemSpace::page(0x10) &&
                  t.at(1).mmu[0] == 0x10 && t.at(2).pc == 0x0004 &&
                  t.at(2).fetch_space() == MemSpace::page(0x11),
              "entries=" + std::to_string(t.size()));
    }
    {
        // zxnext.vhd:2983-2984, :3052: a 48K's RST $38 lands in ROM 0. And :4688: on a Next
        // with MMU0 = page 0x0C the same RST lands in that RAM page.
        Machine m;
        m.emu.mmu().write(0x8000, 0xFF);   // RST $38 (the bare test ROM is 0xFF too: more RSTs follow)
        m.set_pc(0x8000);
        m.dbg->step_into(m.a);
        m.dbg->step_into(m.a);             // the first instruction at $0038
        const auto& fa = m.dbg->call_stack();
        const bool a_ok = !fa.empty() && fa[0].type == CallType::RST &&
                          fa[0].target_pc == 0x0038 && fa[0].target_space == MemSpace::rom(0) &&
                          fa[0].caller_space == MemSpace::page(4);

        Machine n(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        poke_page(n, 0x50, 0x0C, 0x0038, {0x00});
        n.emu.mmu().write(0x8000, 0xFF);
        n.set_pc(0x8000);
        n.dbg->step_into(n.a);
        n.dbg->step_into(n.a);
        const auto& fb = n.dbg->call_stack();
        const bool b_ok = fb.size() == 1 && fb[0].target_pc == 0x0038 &&
                          fb[0].target_space == MemSpace::page(0x0C) &&
                          fb[0].caller_space == MemSpace::page(4);
        check("SRCDBG-CS-03", "a frame's target is the memory its first instruction was "
                              "fetched from: ROM 0 on a 48K, the RAM page mapped at $0000 on "
                              "a Next",
              a_ok && b_ok,
              std::string("48K ") + (a_ok ? "ok" : "BAD") + " frames=" + std::to_string(fa.size()) +
                  (fa.empty() ? std::string() :
                       " target=" + std::to_string(unsigned(fa[0].target_space.kind)) + "/" +
                       std::to_string(unsigned(fa[0].target_space.index)) + " caller=" +
                       std::to_string(unsigned(fa[0].caller_space.kind)) + "/" +
                       std::to_string(unsigned(fa[0].caller_space.index)) + " type " +
                       std::to_string(int(fa[0].type)) + " tpc " + hex(fa[0].target_pc) +
                       (fa.size() > 1 ? " f1 type " + std::to_string(int(fa[1].type)) + " cpc " +
                                            hex(fa[1].caller_pc) + " tpc " + hex(fa[1].target_pc)
                                      : std::string())) +
                  " Next " + (b_ok ? "ok" : "BAD"));
    }
    {
        // zxnext.vhd:4688: CALL $C000 into page 0x12, whose first instruction remaps slot 6
        // to page 0x13. The target stays page 0x12 (recorded once, at the first fetch there):
        // page 0x13 jumps back to $C000, and the fetch AT the target from page 0x13 must not
        // overwrite the recorded one.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        poke_page(m, 0x56, 0x13, 0xC000, {0x00});
        m.emu.mmu().write(0xC004, 0xC3);   // JP $C000
        m.emu.mmu().write(0xC005, 0x00);
        m.emu.mmu().write(0xC006, 0xC0);
        poke_page(m, 0x56, 0x12, 0xC000, {0xED, 0x91, 0x56, 0x13});
        m.emu.mmu().write(0x8000, 0xCD);
        m.emu.mmu().write(0x8001, 0x00);
        m.emu.mmu().write(0x8002, 0xC0);
        m.set_pc(0x8000);
        m.dbg->step_into(m.a);   // CALL $C000
        const bool pending = m.dbg->call_stack().size() == 1 &&
                             m.dbg->call_stack()[0].target_space == MemSpace::cpu();
        m.dbg->step_into(m.a);   // NEXTREG $56,$13, fetched from page 0x12
        const bool remapped = m.dbg->source_page(0xC000) == MemSpace::page(0x13);
        const bool after_one = m.dbg->call_stack().size() == 1 &&
                               m.dbg->call_stack()[0].target_space == MemSpace::page(0x12);
        m.dbg->step_into(m.a);   // JP $C000 at $C004, fetched from page 0x13
        m.dbg->step_into(m.a);   // NOP at $C000 again, now fetched from page 0x13
        const auto& f = m.dbg->call_stack();
        check("SRCDBG-CS-04", "the target is recorded at the first fetch there and kept: "
                              "unrecorded right after the CALL, page 0x12 after the NEXTREG "
                              "that remaps its own slot, still page 0x12 after a later fetch "
                              "at the target from page 0x13",
              pending && remapped && after_one && f.size() == 1 &&
                  f[0].target_space == MemSpace::page(0x12) &&
                  f[0].caller_space == MemSpace::page(4),
              std::string("pending=") + (pending ? "1" : "0") + " remapped=" +
                  (remapped ? "1" : "0") + " after_one=" + (after_one ? "1" : "0"));
    }
    // The interrupt fixture: CALL $C000 (page 0x12) from page 4, an INT taken at $C000 before
    // that NOP runs; the IM 1 routine at $0038 (page 0x10) is EI, RET.
    auto interrupt_machine = [&](Machine& m) {
        poke_page(m, 0x56, 0x12, 0xC000, {0x00});
        poke_page(m, 0x50, 0x10, 0x0038, {0xFB, 0xC9});
        m.emu.mmu().write(0x8000, 0xCD);
        m.emu.mmu().write(0x8001, 0x00);
        m.emu.mmu().write(0x8002, 0xC0);
        Z80Registers r = m.emu.cpu().get_registers();
        r.PC = 0x8000; r.SP = 0xFF00; r.IM = 1; r.IFF1 = 1; r.IFF2 = 1;
        m.emu.cpu().set_registers(r);
        m.dbg->set_trace_enabled(true);
    };
    {
        // t80na.vhd:278-279: the INT acknowledge cycle fetches nothing, so the interrupted
        // instruction's source is the prediction at its PC (page 0x12, :4688), neither the
        // last recorded M1 (the CALL's page 4) nor unknown.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        interrupt_machine(m);
        m.dbg->step_into(m.a);                   // CALL $C000
        m.emu.cpu().request_interrupt(0xFF);
        m.dbg->step_into(m.a);                   // the INT, taken at $C000
        const auto& f = m.dbg->call_stack();
        // frames() is oldest first: the CALL, then the INT on top of it.
        const bool shape = f.size() == 2 && f[1].type == CallType::INT && f[0].type == CallType::CALL &&
                           f[1].caller_pc == 0xC000 && m.pc() == 0x0038;
        check("SRCDBG-CS-05", "an INT frame's caller is the memory the interrupted "
                              "instruction will be fetched from (page 0x12), not the last "
                              "recorded fetch (the CALL's page 4) and not unknown",
              shape && f[1].caller_space == MemSpace::page(0x12),
              shape ? "caller kind " + std::to_string(unsigned(f[1].caller_space.kind)) + "/" +
                          std::to_string(unsigned(f[1].caller_space.index))
                    : "fixture: " + std::to_string(f.size()) + " frames, pc " + hex(m.pc()));
        const auto& t = m.emu.trace_log();
        check("TRACE-ID-05", "an INT's slot has no fetch to record: its trace entry keeps "
                             "the prediction for its PC (page 0x12), not the previous "
                             "instruction's recorded fetch (page 4)",
              shape && t.size() == 2 && t.at(1).pc == 0xC000 &&
                  t.at(1).fetch_space() == MemSpace::page(0x12),
              "entries=" + std::to_string(t.size()));

        // The targets are recorded at their first fetches: both unrecorded right after the INT.
        const bool both_pending = shape && f[0].target_space == MemSpace::cpu() &&
                                  f[1].target_space == MemSpace::cpu();
        m.dbg->step_into(m.a);                   // EI at $0038
        const bool isr_filled = f.size() == 2 && f[1].target_space == MemSpace::page(0x10) &&
                                f[0].target_space == MemSpace::cpu();
        m.dbg->step_into(m.a);                   // RET: the INT frame is popped
        m.dbg->step_into(m.a);                   // NOP at $C000
        check("SRCDBG-CS-06", "an INT frame's target is recorded at the ISR's first fetch "
                              "(page 0x10) while the CALL it interrupted stays unrecorded "
                              "until its own target runs (page 0x12, after the ISR returns)",
              both_pending && isr_filled && f.size() == 1 && f[0].type == CallType::CALL &&
                  f[0].target_space == MemSpace::page(0x12),
              std::string("both_pending=") + (both_pending ? "1" : "0") + " isr_filled=" +
                  (isr_filled ? "1" : "0") + " frames=" + std::to_string(f.size()));
    }
    {
        // A fetch elsewhere than at the target records nothing: the debugger moved PC after
        // the CALL, so no instruction ran at $C000.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        poke_page(m, 0x56, 0x12, 0xC000, {0x00});
        m.emu.mmu().write(0x8000, 0xCD);
        m.emu.mmu().write(0x8001, 0x00);
        m.emu.mmu().write(0x8002, 0xC0);
        m.emu.mmu().write(0x8003, 0x00);
        m.set_pc(0x8000);
        m.dbg->step_into(m.a);   // CALL $C000
        m.set_pc(0x8003);
        m.dbg->step_into(m.a);   // NOP at $8003
        const auto& f = m.dbg->call_stack();
        check("SRCDBG-CS-07", "a frame's target stays unrecorded when the instruction that "
                              "ran was not at the target",
              f.size() == 1 && f[0].target_pc == 0xC000 && f[0].target_space == MemSpace::cpu());
    }
    {
        // The esxdos shim services RST $08 without running anything at $0008: the frame it
        // opened has no fetch at its target to record, and a record of the PREVIOUS fetch
        // (the RST's own, RAM page 4) would name a memory that never supplied $0008.
        Machine m;
        m.emu.cpu().on_esxdos_call = [](uint8_t, Z80Registers&) { return true; };
        m.emu.mmu().write(0x8000, 0xCF);   // RST $08
        m.emu.mmu().write(0x8001, 0x85);   // DEFB $85 (esxdos call code)
        m.emu.mmu().write(0x8002, 0x00);
        m.set_pc(0x8000);
        m.dbg->step_into(m.a);   // the RST itself
        const auto& f = m.dbg->call_stack();
        const bool opened = f.size() == 1 && f[0].type == CallType::RST && f[0].target_pc == 0x0008 &&
                            f[0].caller_space == MemSpace::page(4) &&
                            f[0].target_space == MemSpace::cpu();
        m.dbg->step_into(m.a);   // the shim's slot: it returns to $8002 without a fetch
        // GH #345 (owner-approved 2026-10-30, D5): the shim's return ends the RST frame.
        // This row pinned the stale frame (1 after the shim) as an interim; the
        // property it guards (the frame's target is never recorded from the RST's own
        // fetch) is now asserted between the two slots.
        check("SRCDBG-CS-08", "an RST $08 serviced by the esxdos shim leaves its frame's target "
                              "unrecorded (nothing was fetched at $0008), and the shim's return "
                              "ends the frame",
              opened && f.empty() && m.pc() == 0x8002,
              f.empty() ? std::string("no frame, ") + m.where()
                        : "frames " + std::to_string(f.size()) + " " + m.where());
    }

    {
        // GH #345 (E23-14): a RET that is a computed jump (PUSH; RET) lands on no CALL's
        // return address, so it keeps the SP rule and leaves the CALL's frame; the real
        // RET after it lands on the CALL's return address and ends it.
        Machine m;
        const uint8_t main_p[] = {0xCD, 0x00, 0x90, 0x00};                   // CALL $9000; NOP
        const uint8_t sub_p[]  = {0x21, 0x05, 0x90, 0xE5, 0xC9, 0x00, 0xC9}; // LD HL,$9005; PUSH HL; RET; NOP; RET
        for (size_t i = 0; i < sizeof(main_p); ++i) m.emu.mmu().write(uint16_t(0x8000 + i), main_p[i]);
        for (size_t i = 0; i < sizeof(sub_p); ++i) m.emu.mmu().write(uint16_t(0x9000 + i), sub_p[i]);
        m.set_pc(0x8000);
        for (int i = 0; i < 4; ++i) m.dbg->step_into(m.a);   // CALL, LD, PUSH, RET (the jump)
        const bool jumped = m.pc() == 0x9005 && m.dbg->call_stack().size() == 1;
        m.dbg->step_into(m.a);   // NOP
        m.dbg->step_into(m.a);   // RET (real)
        check("SRCDBG-CS-09", "a RET used as a computed jump keeps the CALL's frame; the real RET "
                              "to the CALL's return address ends it",
              jumped && m.pc() == 0x8003 && m.dbg->call_stack().empty(),
              m.where() + " frames " + std::to_string(m.dbg->call_stack().size()));
    }
    {
        // GH #345 round 1: the tracker driven with the exact pre/post sequence the emulator
        // delivers, for shapes where a routine moves its own return slot.
        const uint16_t T = 0xFF00;
        CallStack cs;
        const auto ins = [&](uint16_t pc, uint16_t sp, uint8_t op, uint16_t npc, uint16_t nsp,
                             uint8_t o2 = 0, uint8_t o3 = 0) {
            cs.on_instruction_pre(pc, sp, op, o2, o3);
            cs.on_instruction_post(nsp, npc);
        };
        const auto fresh = [&] { cs.set_enabled(false); cs.set_enabled(true); };
        // The __z88dk_callee shape (SRCDBG-CS-10's): PUSH HL; CALL f; f: POP DE; POP BC; PUSH DE
        // (f's return slot is now 2 bytes ABOVE its CALL frame's sp_at_call); f: CALL g.
        const auto callee_to_g = [&] {
            fresh();
            ins(0x8000, T, 0xE5, 0x8001, T - 2);
            ins(0x8001, T - 2, 0xCD, 0x9000, T - 4, 0x00, 0x90);
            ins(0x9000, T - 4, 0xD1, 0x9001, T - 2);
            ins(0x9001, T - 2, 0xC1, 0x9002, T);
            ins(0x9002, T, 0xD5, 0x9003, T - 2);
            ins(0x9003, T - 2, 0xCD, 0xA000, T - 4, 0x00, 0xA0);
        };
        {   // CS-11
            callee_to_g();
            const bool two = cs.frames().size() == 2;
            ins(0xA000, T - 4, 0xED, 0x9006, T - 2, 0x4D);   // g: RETI
            const bool reti = cs.frames().size() == 1 && cs.frames()[0].caller_pc == 0x8001;
            callee_to_g();
            ins(0xA000, T - 4, 0xED, 0x9006, T - 2, 0x45);   // g: RETN
            const bool retn = cs.frames().size() == 1 && cs.frames()[0].caller_pc == 0x8001;
            check("SRCDBG-CS-11", "a RETI or RETN that lands on the newest CALL's return address ends "
                                  "that frame only: a __z88dk_callee routine's frame survives the "
                                  "RETI/RETN of a routine it called (the SP rule would end both)",
                  two && reti && retn);
        }
        {   // CS-12
            // A stale frame (CALL s; s: POP HL; JP $8003: it dropped its return address and
            // left by a direct jump to caller_pc + 3), then the ROM idiom RST $10 whose
            // routine returns past an inline DEFW: the return point is not caller_pc + 3 for a
            // reason the frame cannot know, so an RST frame ends by SP, and the SP rule ends
            // the stale frame below it too.
            fresh();
            ins(0x8000, T, 0xCD, 0x9000, T - 2, 0x00, 0x90);
            ins(0x9000, T - 2, 0xE1, 0x9001, T);
            ins(0x9001, T, 0xC3, 0x8003, T, 0x03, 0x80);
            const bool stale = cs.frames().size() == 1;
            ins(0x8003, T, 0xD7, 0x0010, T - 2);             // RST $10
            ins(0x0010, T - 2, 0xE1, 0x0011, T);             // POP HL
            ins(0x0011, T, 0xE5, 0x0012, T - 2);             // PUSH HL (HL = $8006, past a DEFW)
            ins(0x0012, T - 2, 0xC9, 0x8006, T);             // RET to caller_pc + 3
            check("SRCDBG-CS-12", "an RST frame ends by SP alone even when its RET lands on "
                                  "caller_pc + 3 (an inline DEFW): the stale frame below it ends too",
                  stale && cs.frames().empty(), std::to_string(cs.frames().size()) + " frames");
        }
        {   // CS-13
            // newlib sccz80 esx_f_read_callee: three args, CALL, pop af/bc/hl/de, push af (slot up
            // 6), jp asm_esx_f_read; RST $08 (the shim); RET. The shim ends the RST frame only.
            fresh();
            ins(0x8000, T, 0xE5, 0x8001, T - 2);
            ins(0x8001, T - 2, 0xE5, 0x8002, T - 4);
            ins(0x8002, T - 4, 0xE5, 0x8003, T - 6);
            ins(0x8003, T - 6, 0xCD, 0x9000, T - 8, 0x00, 0x90);
            ins(0x9000, T - 8, 0xF1, 0x9001, T - 6);
            ins(0x9001, T - 6, 0xC1, 0x9002, T - 4);
            ins(0x9002, T - 4, 0xE1, 0x9003, T - 2);
            ins(0x9003, T - 2, 0xD1, 0x9004, T);
            ins(0x9004, T, 0xF5, 0x9005, T - 2);
            ins(0x9005, T - 2, 0xC3, 0xA000, T - 2, 0x00, 0xA0);   // jp asm_esx_f_read
            ins(0xA000, T - 2, 0xCF, 0x0008, T - 4);               // RST $08
            const bool rst = cs.frames().size() == 2;
            cs.on_instruction_pre(0x0008, T - 4, 0xF5, 0, 0);
            cs.on_slot_without_fetch(T - 2);                        // the shim returns to $A002
            const bool live = cs.frames().size() == 1 && cs.frames()[0].type == CallType::CALL;
            ins(0xA002, T - 2, 0xC9, 0x8006, T);                    // asm_esx_f_read: RET
            const bool sccz80 = rst && live && cs.frames().empty();
            // sdcc_ix _esx_f_read_callee: slot up 5, push ix, call asm; RST $08; asm RET; pop ix; RET.
            fresh();
            ins(0x8000, T, 0xE5, 0x8001, T - 2);
            ins(0x8001, T - 2, 0xE5, 0x8002, T - 4);
            ins(0x8002, T - 4, 0x3B, 0x8003, T - 5);
            ins(0x8003, T - 5, 0xCD, 0x9000, T - 7, 0x00, 0x90);
            ins(0x9000, T - 7, 0xD1, 0x9001, T - 5);
            ins(0x9001, T - 5, 0x3B, 0x9002, T - 6);
            ins(0x9002, T - 6, 0xF1, 0x9003, T - 4);
            ins(0x9003, T - 4, 0xE1, 0x9004, T - 2);
            ins(0x9004, T - 2, 0xC1, 0x9005, T);
            ins(0x9005, T, 0xD5, 0x9006, T - 2);
            ins(0x9006, T - 2, 0xDD, 0x9008, T - 4);                // push ix
            ins(0x9008, T - 4, 0xCD, 0xA000, T - 6, 0x00, 0xA0);    // call asm_esx_f_read
            ins(0xA000, T - 6, 0xCF, 0x0008, T - 8);                // RST $08
            cs.on_instruction_pre(0x0008, T - 8, 0xF5, 0, 0);
            cs.on_slot_without_fetch(T - 6);
            const bool d1 = cs.frames().size() == 2;
            ins(0xA002, T - 6, 0xC9, 0x900B, T - 4);
            const bool d2 = cs.frames().size() == 1;
            ins(0x900B, T - 4, 0xDD, 0x900D, T - 2);                // pop ix
            ins(0x900D, T - 2, 0xC9, 0x8006, T);
            check("SRCDBG-CS-13", "the esxDOS shim's return ends the RST frame and only it: newlib's "
                                  "sccz80 esx callee (slot moved up, jp to the RST) keeps its CALL "
                                  "frame, and so does the sdcc_ix bridge's nest",
                  sccz80 && d1 && d2 && cs.frames().empty());
        }
        {   // CS-14
            // An interrupt taken inside a routine that moved its return slot up (f after PUSH DE):
            // the ISR (EI; RET) returns to the interrupted PC, which ends the INT frame only.
            fresh();
            ins(0x8000, T, 0xE5, 0x8001, T - 2);
            ins(0x8001, T - 2, 0xCD, 0x9000, T - 4, 0x00, 0x90);
            ins(0x9000, T - 4, 0xD1, 0x9001, T - 2);
            ins(0x9001, T - 2, 0xC1, 0x9002, T);
            ins(0x9002, T, 0xD5, 0x9003, T - 2);
            cs.on_instruction_pre(0x9003, T - 2, 0x00, 0, 0);
            cs.on_interrupt(0x9003, 0x0038, T - 4);
            ins(0x0038, T - 4, 0xFB, 0x0039, T - 4);
            ins(0x0039, T - 4, 0xC9, 0x9003, T - 2);
            const bool callee = cs.frames().size() == 1 && cs.frames()[0].type == CallType::CALL;
            // The same, the INT ending a HALT at $9003: the pushed address is $9004.
            fresh();
            ins(0x8000, T, 0xE5, 0x8001, T - 2);
            ins(0x8001, T - 2, 0xCD, 0x9000, T - 4, 0x00, 0x90);
            ins(0x9000, T - 4, 0xD1, 0x9001, T - 2);
            ins(0x9001, T - 2, 0xC1, 0x9002, T);
            ins(0x9002, T, 0xD5, 0x9003, T - 2);
            cs.on_instruction_pre(0x9003, T - 2, 0x76, 0, 0);
            cs.on_interrupt(0x9003, 0x0038, T - 4);
            ins(0x0038, T - 4, 0xC9, 0x9004, T - 2);
            const bool halted = cs.frames().size() == 1 && cs.frames()[0].type == CallType::CALL;
            // The NMI half: the same callee shape, an NMI at $9003, RETN back to $9003.
            fresh();
            ins(0x8000, T, 0xE5, 0x8001, T - 2);
            ins(0x8001, T - 2, 0xCD, 0x9000, T - 4, 0x00, 0x90);
            ins(0x9000, T - 4, 0xD1, 0x9001, T - 2);
            ins(0x9001, T - 2, 0xC1, 0x9002, T);
            ins(0x9002, T, 0xD5, 0x9003, T - 2);
            cs.on_instruction_pre(0x9003, T - 2, 0x00, 0, 0);
            cs.on_interrupt(0x9003, 0x0066, T - 4, CallType::NMI);
            const bool nmi_open = cs.frames().size() == 2 && cs.frames()[1].type == CallType::NMI;
            ins(0x0066, T - 4, 0xED, 0x9003, T - 2, 0x45);   // RETN
            const bool nmi = nmi_open && cs.frames().size() == 1 && cs.frames()[0].type == CallType::CALL;
            // #278: an ISR that returns ELSEWHERE (a context switch) falls back to the SP rule:
            // the INT frame ends (its slot is below SP) and the CALL frame at SP stays.
            fresh();
            ins(0x8000, T, 0xCD, 0x9000, T - 2, 0x00, 0x90);
            cs.on_instruction_pre(0x9000, T - 2, 0x00, 0, 0);
            cs.on_interrupt(0x9000, 0x0038, T - 4);
            ins(0x0038, T - 4, 0xC9, 0x9050, T - 2);
            const bool elsewhere = cs.frames().size() == 1 && cs.frames()[0].type == CallType::CALL;
            check("SRCDBG-CS-14", "an INT frame ends when the ISR returns to the interrupted PC, so a "
                                  "routine that moved its return slot keeps its frame; an ISR that "
                                  "returns elsewhere ends it by SP (GH #278: strictly below); after a HALT the return point is the HALT + 1; an NMI and its RETN follow the same rule",
                  callee && halted && nmi && elsewhere);
        }
        {   // CS-16
            // GH #349 (R1): sccz80's helpers that return by EX (SP),HL; JP (HL). Oracles: the
            // z88dk library sources (l_gintspsp, l_gcharspsp: slot reused by the value, SP stays
            // at sp_at_call; l_g2intspsp, l_glong2sp: one more PUSH, SP = sp_at_call - 2).
            // CALL at $8000 -> frame (sp_at_call T-2); the helper leaves for $8003 by a JP.
            const auto helper = [&](uint16_t nsp, uint8_t op, uint8_t o2 = 0) {
                fresh();
                ins(0x8000, T, 0xCD, 0x9000, T - 2, 0x00, 0x90);
                ins(0x9000, nsp, op, 0x8003, nsp, o2);   // the jump itself moves no SP
                return cs.frames().empty();
            };
            const bool gint  = helper(T - 2, 0xE9);          // l_gintspsp, l_gcharspsp: JP (HL)
            const bool g2int = helper(T - 4, 0xE9);          // l_g2intspsp, l_glong2sp: after a PUSH
            const bool ix    = helper(T - 2, 0xDD, 0xE9);    // JP (IX)
            const bool iy    = helper(T - 2, 0xFD, 0xE9);    // JP (IY)
            // The frame closes at the jump, not before: the helper's body leaves it open.
            fresh();
            ins(0x8000, T, 0xCD, 0x9000, T - 2, 0x00, 0x90);
            ins(0x9000, T - 2, 0x09, 0x9001, T - 2);          // ADD HL,BC
            ins(0x9001, T - 2, 0xE3, 0x9002, T - 2);          // EX (SP),HL
            const bool open = cs.frames().size() == 1;
            check("SRCDBG-CS-16", "a JP (HL), JP (IX) or JP (IY) onto the newest CALL's return point "
                                  "with SP at or below its slot ends that frame (sccz80's EX (SP),HL; "
                                  "JP (HL) helpers, with and without an extra PUSH); the helper's body "
                                  "before the jump keeps it",
                  gint && g2int && ix && iy && open);
        }
        {   // CS-17
            // The negatives of CS-16 and CS-19/20: each keeps the frame it must not end (a JP (HL) with SP above
            // the slot is CS-19/20's, GH #349 R1').
            // (b) a JP (HL) elsewhere than the return point.
            fresh();
            ins(0x8000, T, 0xCD, 0x9000, T - 2, 0x00, 0x90);
            ins(0x9000, T - 2, 0xE9, 0x9100, T - 2);
            const bool elsewhere = cs.frames().size() == 1;
            // (c) a jump that is not indirect (JP nn) onto the return point.
            fresh();
            ins(0x8000, T, 0xCD, 0x9000, T - 2, 0x00, 0x90);
            ins(0x9000, T - 2, 0xC3, 0x8003, T - 2, 0x03, 0x80);
            const bool direct = cs.frames().size() == 1;
            // (d) recursion: the inner call is the newest frame; a JP (HL) to the OUTER call's
            // return point (SP below both slots) ends neither.
            fresh();
            ins(0x8000, T, 0xCD, 0x9000, T - 2, 0x00, 0x90);
            ins(0x9010, T - 2, 0xCD, 0x9000, T - 4, 0x00, 0x90);
            ins(0x9000, T - 4, 0xE9, 0x8003, T - 4);
            const bool nested = cs.frames().size() == 2;
            // ... and the inner frame's own jump ends the inner frame only.
            ins(0x9000, T - 4, 0xE9, 0x9013, T - 4);
            const bool inner = cs.frames().size() == 1 && cs.frames()[0].caller_pc == 0x8000;
            // (e) an interrupt frame is newest while its ISR runs: a JP (HL) there ends nothing.
            fresh();
            ins(0x8000, T, 0xCD, 0x9000, T - 2, 0x00, 0x90);
            cs.on_instruction_pre(0x9000, T - 2, 0x00, 0, 0);
            cs.on_interrupt(0x9000, 0x0038, T - 4);
            ins(0x0038, T - 4, 0xE9, 0x9003, T - 4);   // onto the INT frame's own caller_pc + 3
            const bool isr_jump = cs.frames().size() == 2;
            // (f) an RST frame is not a CALL frame: its return point varies (inline DEFW).
            fresh();
            ins(0x8000, T, 0xD7, 0x0010, T - 2);
            ins(0x0010, T - 2, 0xE9, 0x8003, T - 2);   // caller_pc + 3
            const bool rst = cs.frames().size() == 1;
            check("SRCDBG-CS-17", "a JP (HL) does NOT end a frame when its target is not the return point and "
                                  "SP is at the slot, the jump is a direct "
                                  "JP nn, the newest frame is another call (recursion) or an INT, or "
                                  "the frame is an RST; the inner frame's own jump ends the inner only",
                  elsewhere && direct && nested && inner && isr_jump && rst);
        }
        {   // CS-19
            // GH #349 R1': sccz80's l_long_sub / and / or / xor / eq / ne: POP IX (the return
            // address), POP the arguments, compute, JP (IX) to caller_pc + 3 with SP ABOVE the slot.
            // Oracle: libsrc/l/sccz80/5-z80/i32/l_long_sub.asm (both installs). An outer CALL frame
            // (slot $FF00-4, sp_at_call) below stays.
            fresh();
            ins(0x7000, T, 0xCD, 0x8000, T - 2, 0x00, 0x80);    // outer: CALL $8000
            ins(0x8000, T - 2, 0xE5, 0x8001, T - 4);            // PUSH HL (two arguments)
            ins(0x8001, T - 4, 0xE5, 0x8002, T - 6);
            ins(0x8002, T - 6, 0xCD, 0x9000, T - 8, 0x00, 0x90); // CALL l_long_sub
            ins(0x9000, T - 8, 0xDD, 0x9002, T - 6, 0xE1);      // POP IX (the return address)
            ins(0x9002, T - 6, 0xE1, 0x9003, T - 4);            // POP HL (argument)
            ins(0x9003, T - 4, 0xE1, 0x9004, T - 2);            // POP HL (argument)
            const bool open = cs.frames().size() == 2;
            ins(0x9004, T - 2, 0xDD, 0x8005, T - 2, 0xE9);      // JP (IX) to caller_pc + 3, SP above the slot
            const bool closed = cs.frames().size() == 1 && cs.frames()[0].caller_pc == 0x7000;
            check("SRCDBG-CS-19", "a JP (IX) onto the newest CALL's return point ends that frame with SP "
                                  "ABOVE its slot (l_long_sub: POP IX, arguments popped), and the outer "
                                  "frame stays",
                  open && closed);
        }
        {   // CS-20
            // GH #349 R1': l_case / l_long_case / l_i64_case: POP HL takes the inline case table,
            // the table is walked, JP (HL) goes to a case label in the caller (not caller_pc + 3).
            // Oracle: libsrc/l/sccz80/9-common/l_case.asm (both installs).
            fresh();
            ins(0x7000, T, 0xCD, 0x8000, T - 2, 0x00, 0x80);    // outer: CALL $8000
            ins(0x8010, T - 2, 0xCD, 0x9000, T - 4, 0x00, 0x90); // CALL l_case, in the middle of $8000's function
            ins(0x9000, T - 4, 0xEB, 0x9001, T - 4);            // EX DE,HL
            ins(0x9001, T - 4, 0xE1, 0x9002, T - 2);            // POP HL (the case table)
            const bool open = cs.frames().size() == 2;
            ins(0x9002, T - 2, 0xE9, 0x8040, T - 2);            // JP (HL) to the case label $8040
            const bool closed = cs.frames().size() == 1 && cs.frames()[0].caller_pc == 0x7000;
            // The default case: the continuation after the table, also in the caller.
            fresh();
            ins(0x7000, T, 0xCD, 0x8000, T - 2, 0x00, 0x80);
            ins(0x8010, T - 2, 0xCD, 0x9000, T - 4, 0x00, 0x90);
            ins(0x9001, T - 4, 0xE1, 0x9002, T - 2);
            ins(0x9002, T - 2, 0xE9, 0x8020, T - 2);
            const bool dflt = cs.frames().size() == 1 && cs.frames()[0].caller_pc == 0x7000;
            check("SRCDBG-CS-20", "a JP (HL) taken after the newest CALL frame's return slot was consumed "
                                  "(l_case: POP HL as the case table) ends that frame wherever it lands, "
                                  "and the outer frame stays",
                  open && closed && dflt);
        }
        {   // CS-21
            // The explicit negative: banked_call POPs its return address, then CALLs l_jphl; the
            // JP (HL) is inside l_jphl (its own frame is newest, SP at its slot, the target is a
            // function), so nothing ends. Oracle: libsrc/_DEVELOPMENT/target/zxn/.../banked_call.asm
            // and SRCSTEP-01..03, -08 (running code).
            fresh();
            ins(0x8000, T, 0xCD, 0x9000, T - 2, 0x00, 0x90);    // CALL banked_call
            ins(0x9000, T - 2, 0xE1, 0x9001, T);                // POP HL (the return address)
            ins(0x9001, T, 0xCD, 0x9100, T - 2, 0x00, 0x91);    // CALL l_jphl
            ins(0x9100, T - 2, 0xE9, 0xC000, T - 2);            // JP (HL) into the banked function
            const bool both = cs.frames().size() == 2;
            ins(0xC000, T - 2, 0xC9, 0x9004, T);                // the function's RET ends l_jphl's frame
            check("SRCDBG-CS-21", "banked_call's POP HL; CALL l_jphl; JP (HL) ends no frame (the jump is "
                                  "inside l_jphl at its slot, to a function), and the function's RET "
                                  "ends l_jphl's frame only",
                  both && cs.frames().size() == 1 && cs.frames()[0].caller_pc == 0x8000);
        }
        {   // CS-22
            // After the newest frame ends by a jump, the older frames whose slot is below the new SP end
            // by the SP rule: A's routine dropped its own return address (POP HL), then called a helper
            // that POPs and JP (HL)s; SP is now above A's slot too.
            fresh();
            ins(0x7000, T, 0xCD, 0x8000, T - 2, 0x00, 0x80);    // A: CALL $8000 (slot T-2)
            ins(0x8000, T - 2, 0xE1, 0x8001, T);                // A's routine: POP HL (its slot is gone)
            ins(0x8001, T, 0xCD, 0x9000, T - 2, 0x00, 0x90);    // B: CALL helper (slot T-2)
            ins(0x9000, T - 2, 0xE1, 0x9001, T);                // helper: POP HL
            const bool both = cs.frames().size() == 2;
            ins(0x9001, T, 0xE9, 0x8004, T);                    // JP (HL) to B's caller_pc + 3
            check("SRCDBG-CS-22", "when a jump ends the newest frame, the older frames whose slot is below "
                                  "the new SP end with it (the SP rule)",
                  both && cs.frames().empty(), std::to_string(cs.frames().size()) + " frames");
        }
        // GH #349 R1'': a __z88dk_callee body moves its return address UP (POP r; POP args; PUSH r), so SP
        // is above its slot for the whole body, and an internal JP (r) must not end the frame. Clause B
        // asks whether the routine still holds a stored copy of its return address (a PUSH or EX (SP) that
        // put caller_pc + 3 above the slot, still at or above SP). These rows install a stack reader over a
        // test-local memory; each step writes what the instruction stores before it is delivered.
        std::array<uint8_t, 0x10000> mem{};
        const auto w16 = [&](uint16_t a, uint16_t v) {
            mem[a] = static_cast<uint8_t>(v & 0xFF);
            mem[static_cast<uint16_t>(a + 1)] = static_cast<uint8_t>(v >> 8);
        };
        uint16_t B = T;   // the stack top of the routine under test (T, or T - 2 under an outer CALL)
        // A fresh tracker with the reader, `n` argument words PUSHed and a CALL to $9000: the return
        // address is $8003 + n, the CALL's slot B - 2n - 2. `outer`: the routine itself is called from $7000.
        const auto open_call = [&](int n, bool outer) {
            mem.fill(0);
            cs.set_stack_reader([&](uint16_t a) { return mem[a]; });
            fresh();
            B = T;
            if (outer) {
                ins(0x7000, T, 0xCD, 0x8000, T - 2, 0x00, 0x80);
                B = T - 2;
            }
            uint16_t sp = B;
            for (int k = 0; k < n; ++k, sp -= 2) ins(0x8000 + k, sp, 0xE5, 0x8001 + k, sp - 2);
            const uint16_t R = static_cast<uint16_t>(0x8003 + n);
            w16(sp - 2, R);
            ins(0x8000 + n, sp, 0xCD, 0x9000, sp - 2, 0x00, 0x90);
            return R;
        };
        {   // CS-23
            // Oracles: libsrc/sound/bit/c/sccz80/bit_beep_raw_callee.asm (POP AF; POP HL; POP DE; PUSH AF;
            // JP asm_bit_beep_raw) and sound/bit/z80/asm_bit_beep_raw.asm (loops by JP (IX)), both installs.
            // (a) the addone shape: PUSH arg; CALL f; POP HL; POP DE; PUSH HL; JP (HL) inside; RET.
            uint16_t R = open_call(1, false);
            ins(0x9000, T - 4, 0xE1, 0x9001, T - 2);
            ins(0x9001, T - 2, 0xD1, 0x9002, T);
            w16(T - 2, R);
            ins(0x9002, T, 0xE5, 0x9003, T - 2);
            ins(0x9006, T - 2, 0xE9, 0x9007, T - 2);
            const bool a_kept = cs.frames().size() == 1;
            ins(0x9007, T - 2, 0xC9, R, T);
            const bool a = a_kept && cs.frames().empty();
            // (b) the bit_beep_raw_callee shape: two args; POP AF; POP HL; POP DE; PUSH AF; JP nn;
            // CALL asm_bit_open (a frame of its own) and RET; three JP (IX) at SP = slot + 4; JP nn; RET.
            R = open_call(2, false);
            ins(0x9000, T - 6, 0xF1, 0x9001, T - 4);
            ins(0x9001, T - 4, 0xE1, 0x9002, T - 2);
            ins(0x9002, T - 2, 0xD1, 0x9003, T);
            w16(T - 2, R);
            ins(0x9003, T, 0xF5, 0x9004, T - 2);
            ins(0x9004, T - 2, 0xC3, 0x9100, T - 2, 0x00, 0x91);
            w16(T - 4, 0x9103);
            ins(0x9100, T - 2, 0xCD, 0x9200, T - 4, 0x00, 0x92);
            ins(0x9200, T - 4, 0xC9, 0x9103, T - 2);
            bool loop = cs.frames().size() == 1;
            for (const uint16_t to : {0x9110, 0x9111, 0x9112}) {
                ins(0x9103, T - 2, 0xDD, to, T - 2, 0xE9);
                loop = loop && cs.frames().size() == 1;
            }
            ins(0x9112, T - 2, 0xC3, 0x9300, T - 2, 0x00, 0x93);
            ins(0x9300, T - 2, 0xC9, R, T);
            const bool b = loop && cs.frames().empty();
            cs.set_stack_reader({});
            check("SRCDBG-CS-23", "a callee body whose return address was PUSHed back above its slot keeps its frame "
                                  "across internal JP (HL) / JP (IX) (the addone shape; the bit_beep_raw_callee "
                                  "shape with an inner CALL/RET and three JP (IX) at SP = slot + 4); its RET ends it",
                  a && b, std::string(a ? "a ok " : "a FAIL ") + (b ? "b ok" : "b FAIL"));
        }
        {   // CS-24
            // Which stores are recorded, and that the latest one counts.
            // (a) POP HL; POP DE; EX (SP),HL (the return address at slot + 4); PUSH BC; JP (IX).
            uint16_t R = open_call(2, false);
            ins(0x9000, T - 6, 0xE1, 0x9001, T - 4);
            ins(0x9001, T - 4, 0xD1, 0x9002, T - 2);
            w16(T - 2, R);
            ins(0x9002, T - 2, 0xE3, 0x9003, T - 2);
            w16(T - 4, 0x1234);
            ins(0x9003, T - 2, 0xC5, 0x9004, T - 4);
            ins(0x9004, T - 4, 0xDD, 0x9010, T - 4, 0xE9);
            const bool ex = cs.frames().size() == 1;
            // (b) the same move by PUSH IX, PUSH IY and Z80N PUSH nn.
            bool pushes = true;
            for (const uint8_t pfx : {0xDD, 0xFD, 0xED}) {
                R = open_call(1, false);
                ins(0x9000, T - 4, 0xE1, 0x9001, T - 2);
                ins(0x9001, T - 2, 0xD1, 0x9002, T);
                w16(T - 2, R);
                ins(0x9002, T, pfx, 0x9004, T - 2, pfx == 0xED ? 0x8A : 0xE5);
                ins(0x9004, T - 2, 0xE9, 0x9010, T - 2);
                pushes = pushes && cs.frames().size() == 1;
            }
            // (c) moved twice: pushed at slot + 2, popped, one more argument popped, pushed at slot + 4.
            R = open_call(2, false);
            ins(0x9000, T - 6, 0xE1, 0x9001, T - 4);
            ins(0x9001, T - 4, 0xD1, 0x9002, T - 2);
            w16(T - 4, R);
            ins(0x9002, T - 2, 0xE5, 0x9003, T - 4);
            ins(0x9003, T - 4, 0xE1, 0x9004, T - 2);
            ins(0x9004, T - 2, 0xC1, 0x9005, T);
            w16(T - 2, R);
            ins(0x9005, T, 0xE5, 0x9006, T - 2);
            ins(0x9006, T - 2, 0xE9, 0x9010, T - 2);
            const bool twice = cs.frames().size() == 1;
            // (d) a word that is not the return address, pushed below the real copy, does not replace it.
            R = open_call(2, false);
            ins(0x9000, T - 6, 0xE1, 0x9001, T - 4);
            ins(0x9001, T - 4, 0xD1, 0x9002, T - 2);
            ins(0x9002, T - 2, 0xC1, 0x9003, T);
            w16(T - 2, R);
            ins(0x9003, T, 0xE5, 0x9004, T - 2);
            w16(T - 4, 0x1234);
            ins(0x9004, T - 2, 0xC5, 0x9005, T - 4);
            ins(0x9005, T - 4, 0xE9, 0x9010, T - 4);
            const bool other = cs.frames().size() == 1;
            cs.set_stack_reader({});
            check("SRCDBG-CS-24", "the store that moves the return address up is recorded for EX (SP),HL, PUSH IX, "
                                  "PUSH IY and PUSH nn; the latest store counts (moved twice); a non-return word "
                                  "pushed below the real copy replaces nothing",
                  ex && pushes && twice && other,
                  std::string(ex ? "ex ok " : "ex FAIL ") + (pushes ? "pushes ok " : "pushes FAIL ") +
                      (twice ? "twice ok " : "twice FAIL ") + (other ? "other ok" : "other FAIL"));
        }
        {   // CS-25
            // Clause B still ends the frame when the routine has left; the outer frame ($7000) stays.
            const auto outer_only = [&] { return cs.frames().size() == 1 && cs.frames()[0].caller_pc == 0x7000; };
            // (a) the stored copy popped again, then JP (HL) elsewhere.
            uint16_t R = open_call(2, true);
            ins(0x9000, B - 6, 0xE1, 0x9001, B - 4);
            ins(0x9001, B - 4, 0xD1, 0x9002, B - 2);
            ins(0x9002, B - 2, 0xC1, 0x9003, B);
            w16(B - 2, R);
            ins(0x9003, B, 0xE5, 0x9004, B - 2);
            ins(0x9004, B - 2, 0xE1, 0x9005, B);
            const bool open_a = cs.frames().size() == 2;
            ins(0x9005, B, 0xE9, 0x9040, B);
            const bool popped = open_a && outer_only();
            // (b) the stored copy overwritten by EX (SP),HL with another value, then JP (IX) elsewhere.
            R = open_call(2, true);
            ins(0x9000, B - 6, 0xE1, 0x9001, B - 4);
            ins(0x9001, B - 4, 0xD1, 0x9002, B - 2);
            ins(0x9002, B - 2, 0xC1, 0x9003, B);
            w16(B - 2, R);
            ins(0x9003, B, 0xE5, 0x9004, B - 2);
            w16(B - 2, 0x1111);
            ins(0x9004, B - 2, 0xE3, 0x9005, B - 2);
            const bool still = cs.frames().size() == 2;
            ins(0x9005, B - 2, 0xDD, 0x9040, B - 2, 0xE9);
            const bool overwritten = still && outer_only();
            // (c) a word that is not the return address pushed above the slot (l_case: POP HL = table).
            R = open_call(2, true);
            ins(0x9000, B - 6, 0xE1, 0x9001, B - 4);
            ins(0x9001, B - 4, 0xD1, 0x9002, B - 2);
            w16(B - 4, 0x2222);
            ins(0x9002, B - 2, 0xD5, 0x9003, B - 4);
            const bool open_c = cs.frames().size() == 2;
            ins(0x9003, B - 4, 0xE9, 0x8040, B - 4);
            const bool other = open_c && outer_only();
            cs.set_stack_reader({});
            (void)R;
            check("SRCDBG-CS-25", "clause B still ends the frame: the stored copy popped again, the stored copy "
                                  "overwritten by EX (SP),HL, a non-return word pushed above the slot (l_case); "
                                  "the outer frame stays in each",
                  popped && overwritten && other,
                  std::string(popped ? "popped ok " : "popped FAIL ") + (overwritten ? "overwritten ok " : "overwritten FAIL ") +
                      (other ? "other ok" : "other FAIL"));
        }
        {   // CS-26
            // The classic retrofit shim _clga_callee (libsrc/classic/gfx/retrofit/clga_callee.asm):
            // LD HL,retaddr; EX (SP),HL; LD (retaddr0+1),HL; LD HL,clga; JP (HL). SP stays AT the slot,
            // which holds a local label while the return address sits in memory: nothing is stored
            // above the slot, and the frame stays.
            const uint16_t R = open_call(1, false);
            (void)R;
            w16(T - 4, 0x9007);
            ins(0x9003, T - 4, 0xE3, 0x9004, T - 4);
            ins(0x9008, T - 4, 0xE9, 0xA000, T - 4);
            const bool kept = cs.frames().size() == 1;
            cs.set_stack_reader({});
            check("SRCDBG-CS-26", "a JP (HL) with SP AT the slot keeps the frame whatever the slot holds (the "
                                  "classic retrofit callee shim: the return address is in memory)",
                  kept);
        }
        {   // CS-27
            // Interrupts around the sequence. Memory: the INT pushes the interrupted PC.
            // (a) an INT between the prologue's POP DE and PUSH HL; the ISR runs PUSH AF; POP AF; RETI.
            uint16_t R = open_call(1, false);
            ins(0x9000, T - 4, 0xE1, 0x9001, T - 2);
            ins(0x9001, T - 2, 0xD1, 0x9002, T);
            cs.on_instruction_pre(0x9002, T, 0x00, 0, 0);
            w16(T - 2, 0x9002);
            cs.on_interrupt(0x9002, 0x0038, T - 2);
            w16(T - 4, 0xAAAA);
            ins(0x0038, T - 2, 0xF5, 0x0039, T - 4);
            ins(0x0039, T - 4, 0xF1, 0x003A, T - 2);
            ins(0x003A, T - 2, 0xED, 0x9002, T, 0x4D);
            const bool int_ended = cs.frames().size() == 1 && cs.frames()[0].type == CallType::CALL;
            w16(T - 2, R);
            ins(0x9002, T, 0xE5, 0x9003, T - 2);
            ins(0x9006, T - 2, 0xE9, 0x9007, T - 2);
            const bool a = int_ended && cs.frames().size() == 1;
            // (b) an INT after the move: the ISR's pushes land below SP; the internal jump keeps the frame.
            R = open_call(1, false);
            ins(0x9000, T - 4, 0xE1, 0x9001, T - 2);
            ins(0x9001, T - 2, 0xD1, 0x9002, T);
            w16(T - 2, R);
            ins(0x9002, T, 0xE5, 0x9003, T - 2);
            cs.on_instruction_pre(0x9006, T - 2, 0x00, 0, 0);
            w16(T - 4, 0x9006);
            cs.on_interrupt(0x9006, 0x0038, T - 4);
            ins(0x0038, T - 4, 0xED, 0x9006, T - 2, 0x4D);
            ins(0x9006, T - 2, 0xE9, 0x9007, T - 2);
            const bool b = cs.frames().size() == 1 && cs.frames()[0].type == CallType::CALL;
            // (c) an INT between l_case's POP HL and JP (HL): its push lands on the consumed slot; after RETI
            // the jump ends the frame.
            open_call(0, false);
            ins(0x9000, T - 2, 0xE1, 0x9001, T);
            cs.on_instruction_pre(0x9001, T, 0x00, 0, 0);
            w16(T - 2, 0x9001);
            cs.on_interrupt(0x9001, 0x0038, T - 2);
            ins(0x0038, T - 2, 0xED, 0x9001, T, 0x4D);
            const bool before = cs.frames().size() == 1;
            ins(0x9001, T, 0xE9, 0x8040, T);
            const bool c = before && cs.frames().empty();
            cs.set_stack_reader({});
            check("SRCDBG-CS-27", "interrupts: one between the prologue's POP and PUSH ends only its own frame and "
                                  "the PUSH is still recorded; one after the move leaves the frame; one between "
                                  "l_case's POP HL and JP (HL) does not stop the jump from ending the frame",
                  a && b && c, std::string(a ? "a ok " : "a FAIL ") + (b ? "b ok " : "b FAIL ") + (c ? "c ok" : "c FAIL"));
        }
        {   // CS-18
            // The helper, then RETI/RETN and an interrupt around it, keep the other rules:
            // an INT taken between EX (SP),HL and JP (HL), returned by the ISR's RET to the
            // interrupted JP (HL), then the JP ends the CALL frame.
            fresh();
            ins(0x8000, T, 0xCD, 0x9000, T - 2, 0x00, 0x90);
            ins(0x9000, T - 2, 0xE3, 0x9001, T - 2);          // EX (SP),HL
            cs.on_instruction_pre(0x9001, T - 2, 0xE9, 0, 0);
            cs.on_interrupt(0x9001, 0x0038, T - 4);
            ins(0x0038, T - 4, 0xED, 0x9001, T - 2, 0x4D);    // RETI to the interrupted JP (HL)
            const bool back = cs.frames().size() == 1 && cs.frames()[0].type == CallType::CALL;
            ins(0x9001, T - 2, 0xE9, 0x8003, T - 2);
            check("SRCDBG-CS-18", "an interrupt taken between EX (SP),HL and JP (HL) neither ends nor "
                                  "keeps the wrong frame: its RETI ends the INT frame, the JP (HL) "
                                  "then ends the CALL frame",
                  back && cs.frames().empty());
        }
    }
    {
        // An INT that ends a HALT: the CPU has stepped past the HALT, so the pushed return
        // address is the HALT's + 1 while the frame's caller is the HALT. The ISR's RET lands
        // there and ends the INT frame, whatever the SP rule would say.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        interrupt_machine(m);
        // The callee shape, so the SP rule would give the wrong answer: POP DE; POP BC; PUSH DE
        // moves the return slot up 2, then HALT; NOP.
        poke_page(m, 0x56, 0x12, 0xC000, {0xD1, 0xC1, 0xD5, 0x76, 0x00});
        {
            Z80Registers r = m.emu.cpu().get_registers();
            r.SP = 0xFEFE;                       // one argument word above the CALL's slot
            m.emu.cpu().set_registers(r);
        }
        m.dbg->step_into(m.a);                   // CALL $C000
        for (int i = 0; i < 4; ++i) m.dbg->step_into(m.a);   // POP DE, POP BC, PUSH DE, HALT
        m.emu.cpu().request_interrupt(0xFF);
        m.dbg->step_into(m.a);                   // the INT
        const auto& f = m.dbg->call_stack();
        const bool taken = f.size() == 2 && f[1].type == CallType::INT && m.pc() == 0x0038;
        const uint16_t caller = taken ? f[1].caller_pc : 0;
        m.dbg->step_into(m.a);                   // EI
        m.dbg->step_into(m.a);                   // RET
        check("SRCDBG-CS-15", "an INT that ended a HALT has the HALT as its caller, and the ISR's RET "
                              "to the HALT's successor ends it, leaving the interrupted CALL's frame",
              taken && caller == 0xC003 && m.pc() == 0xC004 && f.size() == 1 &&
                  f[0].type == CallType::CALL,
              "caller " + hex(caller) + " pc " + hex(m.pc()) + " frames " + std::to_string(f.size()));
    }

    // ── GH #342: the source map's lifetime (design §6.5-6.7) ────────────
    // A LOAD GENERATION starts at every landing that is not a rewind-ring
    // restore and at every program-load notification; a rewind brings back
    // the sidecars of the point it lands on (the last 4 attachments are
    // kept); any other landing keeps a carried map only if its binary
    // identity verifies; Back never crosses a landing.
    {
        const fs::path life = dir / "life";
        fs::create_directories(life);
        const fs::path good = dir / "ident-good.sld";
        // A state of another machine whose $8000 holds `first` (and, when
        // `program`, the fixture program), run for `frames` frames.
        const auto foreign_state = [](uint8_t first, bool program, int frames, uint16_t pc = 0x8000) {
            Emulator o;
            EmulatorConfig c;
            c.type = MachineType::ZX48K;
            o.init(c);
            if (program) {
                const uint8_t main_prog[] = {0x00, 0x00, 0xCD, 0x00, 0x90, 0x00, 0xC3, 0x00, 0x80};
                for (size_t i = 0; i < sizeof(main_prog); ++i)
                    o.mmu().write(static_cast<uint16_t>(0x8000 + i), main_prog[i]);
                const uint8_t sub[] = {0x32, 0x00, 0xA0, 0x00, 0xC9};
                for (size_t i = 0; i < sizeof(sub); ++i)
                    o.mmu().write(static_cast<uint16_t>(0x9000 + i), sub[i]);
            }
            o.mmu().write(0x8000, first);
            Z80Registers r = o.cpu().get_registers();
            r.PC = pc;
            r.SP = 0xFF00;
            r.IFF1 = r.IFF2 = 0;
            o.cpu().set_registers(r);
            for (int i = 0; i < frames; ++i) o.run_frame();
            // The frames moved the PC along the program; the state is taken at `pc`.
            r = o.cpu().get_registers();
            r.PC = pc;
            o.cpu().set_registers(r);
            StateWriter measure;
            o.save_state(measure);
            std::vector<uint8_t> bytes(measure.position(), 0);
            StateWriter w(bytes.data(), bytes.size());
            o.save_state(w);
            return bytes;
        };
        const auto own_state = [](Machine& m) {
            StateWriter measure;
            m.emu.save_state(measure);
            std::vector<uint8_t> bytes(measure.position(), 0);
            StateWriter w(bytes.data(), bytes.size());
            m.emu.save_state(w);
            return bytes;
        };
        const auto run_frames = [](Machine& m, int n) {
            m.dbg->run(m.a);
            for (int i = 0; i < n; ++i) m.emu.run_frame();
            m.dbg->pause(m.a);
        };
        // The driver's `load` lands `*bytes` as a state (the ring is kept).
        const auto state_driver = [](Machine& m, const std::vector<uint8_t>* bytes) {
            jnext::dbg::LoopDriver d;
            d.load = [&m, bytes](const std::string&) {
                StateReader r(bytes->data(), bytes->size());
                return m.emu.load_state(r);
            };
            m.dbg->set_loop_driver(d);
        };
        const auto attach_text = [&](Machine& m, const std::string& stem, const std::string& sld,
                                     const std::string& memory = {}) {
            write_text(life / (stem + ".nex"), "");
            write_text(life / (stem + ".sld"), sld);
            if (!memory.empty()) write_text(life / (stem + ".Memory.txt"), memory);
            return m.dbg->load_program_sidecars((life / (stem + ".nex")).string());
        };
        struct LogCatcher : jnext::dbg::Listener {
            std::vector<std::string> lines;
            std::vector<jnext::dbg::LogLevel> levels;
            void on_paused(const jnext::dbg::PausedInfo&) override {}
            void on_resumed(ClientId) override {}
            void on_reset(ResetKind) override {}
            void on_frame_ended(uint32_t) override {}
            void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
            void on_exit_requested(int) override {}
            void on_log(jnext::dbg::LogLevel l, const std::string& t) override {
                levels.push_back(l);
                lines.push_back(t);
            }
            bool has(const std::string& s) const {
                for (const auto& l : lines)
                    if (l.find(s) != std::string::npos) return true;
                return false;
            }
        };
        using State = Debugger::SourceMapStatus::State;
        const std::string a_sld = program_sld(p80, p90);
        const std::string b_sld = x_sld(p80, {{"5", 0x8000}});
        const std::string good_text = [&] {
            std::ifstream in(good, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        }();
        const auto line_at = [](Machine& m, uint16_t addr) -> std::string {
            const auto* l = m.dbg->source_location(addr);
            return l ? l->file + ":" + std::to_string(l->line) : std::string("-");
        };

        // SRCLIFE-01: A, then B's state landing that keeps A's slots, then a
        // rewind into A's history and into B's: each point's own map comes back.
        {
            Machine m(/*rewind=*/true);
            run_frames(m, 1);
            attach_text(m, "l1a", a_sld);
            run_frames(m, 2);
            const uint32_t frame_a = m.dbg->time().frame;
            const std::vector<uint8_t> b_state = foreign_state(0x00, true, 6);
            state_driver(m, &b_state);
            write_text(life / "l1b.sld", b_sld);
            const Result ld = m.dbg->load(m.a, (life / "l1b.nex").string());
            run_frames(m, 2);
            const uint32_t frame_b = m.dbg->time().frame;
            const Result ra = m.dbg->rewind_to_frame(m.a, frame_a);
            const std::string at_a = line_at(m, 0x8000);
            const Result rb = m.dbg->rewind_to_frame(m.a, frame_b);
            const std::string at_b = line_at(m, 0x8000);
            check("SRCLIFE-01", "load A, load B (a state landing that keeps A's slots), rewind "
                                "into A: A's map is back; rewind into B: B's",
                  ld == Result::Ok && ra == Result::Ok && rb == Result::Ok &&
                      at_a == "main.bas:10" && at_b == "x.asm:5",
                  "A=" + at_a + " B=" + at_b);
        }

        // SRCLIFE-02: a real .sna of other bytes: the identity fails.
        {
            Machine m;
            LogCatcher lc;
            m.dbg->set_listener(m.a, &lc);
            const auto r = attach_text(m, "l2", good_text, "8000: ._Main\n");
            Machine other;
            other.emu.mmu().write(0x8000, 0xFF);
            const std::string sna = (life / "l2b.sna").string();
            const Result sv = other.dbg->save_snapshot(other.a, sna);
            jnext::dbg::LoopDriver d;
            d.load = [&m](const std::string& p) { return emulator_apply_load(m.emu, p, false); };
            m.dbg->set_loop_driver(d);
            const bool attached = r.sources == 6 && r.symbols == 1;
            const Result ld = m.dbg->load(m.a, sna);
            const auto st = m.dbg->source_map_status();
            check("SRCLIFE-02", "a sidecar with identity and a real .sna of other bytes: map and "
                                "symbols detached, status None with a reason",
                  attached && sv == Result::Ok && ld == Result::Ok &&
                      m.dbg->source_map().empty() && m.dbg->symbols().empty() &&
                      st.state == State::None && !st.reason.empty() &&
                      lc.has("Dropped the program's SLD source map: its binary identity"),
                  "ld=" + std::to_string(int(ld)) + " reason=" + st.reason);
        }

        // SRCLIFE-03: a state whose $8000.. differ (B3c).
        {
            Machine m;
            const auto r = attach_text(m, "l3", good_text, "8000: ._Main\n");
            const std::vector<uint8_t> other = foreign_state(0xFF, true, 1);
            const Result ld = m.dbg->load_state_bytes(m.a, other.data(), other.size());
            check("SRCLIFE-03", "a state restored with other bytes at $8000: the identity fails, "
                                "map and symbols detached",
                  r.sources == 6 && ld == Result::Ok && m.dbg->source_map().empty() &&
                      m.dbg->symbols().empty());
        }

        // SRCLIFE-04: identical code: kept, re-keyed, and a rewind into the
        // post-load slots keeps it too.
        {
            Machine m(/*rewind=*/true);
            run_frames(m, 1);
            const auto r = attach_text(m, "l4", good_text, "8000: ._Main\n");
            const std::vector<uint8_t> same = foreign_state(0x00, true, 4);
            const Result ld = m.dbg->load_state_bytes(m.a, same.data(), same.size());
            const bool kept_load = m.dbg->source_map().size() == 6;
            const uint32_t f0 = m.dbg->time().frame;
            run_frames(m, 2);
            const Result rr = m.dbg->rewind_to_frame(m.a, f0 + 1);
            check("SRCLIFE-04", "a state of the same code keeps the sidecars after the load and "
                                "after a rewind into the post-load slots",
                  r.sources == 6 && ld == Result::Ok && kept_load && rr == Result::Ok &&
                      m.dbg->source_map().size() == 6 && m.dbg->lookup_name("Main") == 0x8000);
        }

        // SRCLIFE-05: the user's map, a foreign state: stale, not replaced.
        {
            Machine m;
            m.dbg->load_source_map(good.string(), false);
            const std::vector<uint8_t> other = foreign_state(0xFF, true, 1);
            const Result ld = m.dbg->load_state_bytes(m.a, other.data(), other.size());
            const auto st = m.dbg->source_map_status();
            const bool stale = st.state == State::Stale && st.by_user && st.file == good.string() &&
                               !st.reason.empty() && m.dbg->source_map().empty() &&
                               !m.dbg->source_location(0x8000);
            attach_text(m, "l5", a_sld);
            const auto st2 = m.dbg->source_map_status();
            const bool kept = st2.state == State::Stale && st2.file == good.string();
            const auto again = m.dbg->load_source_map(good.string(), true);
            const auto st3 = m.dbg->source_map_status();
            check("SRCLIFE-05", "the user's map after another program's state: stale (no lookup), "
                                "a later sidecar does not replace it, loading it again makes it live",
                  ld == Result::Ok && stale && kept && again.count == 6 &&
                      st3.state == State::Attached && m.dbg->source_map().size() == 6,
                  st.reason);
        }

        // SRCLIFE-06 / -24: a reset whose ROM clears RAM afterwards (poked
        // here: the fixture has no ROM). Intact at the reset, gone later.
        for (int by_user = 0; by_user < 2; ++by_user) {
            Machine m;
            if (by_user) {
                m.dbg->load_source_map(good.string(), false);
            } else {
                attach_text(m, "l6", good_text);
            }
            const Result rs = m.dbg->reset(m.a, ResetKind::Soft);
            const auto s0 = m.dbg->source_map_status();
            m.dbg->run(m.a);
            m.emu.mmu().write(0x8001, 0x55);
            for (int i = 0; i < 5; ++i) m.emu.run_frame();
            const auto s1 = m.dbg->source_map_status();
            m.dbg->pause(m.a);
            const auto s2 = m.dbg->source_map_status();
            const bool gone = m.emu.mmu().peek(0x8001) != 0x00;
            const bool ok = rs == Result::Ok && gone && s0.state == State::Attached &&
                            s1.state == State::Attached;
            if (by_user)
                check("SRCLIFE-24", "the same with a map loaded by hand: Stale (by_user, file kept) "
                                    "after the pause",
                      ok && s2.state == State::Stale && s2.by_user && s2.file == good.string() &&
                          m.dbg->source_map().empty());
            else
                check("SRCLIFE-06", "a soft reset keeps a verified sidecar map while it is still "
                                    "intact and while running; the next pause finds RAM changed "
                                    "and detaches it with a reason",
                      ok && s2.state == State::None && !s2.reason.empty());
        }

        // SRCLIFE-07: an RZX started from other bytes.
        {
            Machine m;
            LogCatcher lc;
            m.dbg->set_listener(m.a, &lc);
            attach_text(m, "l7", good_text);
            const std::string rzx = (life / "l7b.rzx").string();
            {
                Machine rec;
                rec.emu.mmu().write(0x8000, 0xFF);
                rec.emu.start_rzx_recording(rzx);
                rec.emu.run_frame();
                rec.emu.stop_rzx_recording();
            }
            jnext::dbg::LoopDriver d;
            d.load = [&m](const std::string& p) { return m.emu.load_rzx(p); };
            m.dbg->set_loop_driver(d);
            const Result ld = m.dbg->load(m.a, rzx);
            check("SRCLIFE-07", "an RZX playback start is a landing: the sidecar with an identity "
                                "that does not match is detached",
                  ld == Result::Ok && m.emu.mmu().peek(0x8000) == 0xFF &&
                      m.dbg->source_map().empty() &&
                      lc.has("Dropped the program's SLD source map: its binary identity"),
                  "ld=" + std::to_string(int(ld)));
        }

        // SRCLIFE-08: Back does not cross a state load.
        {
            Machine m(/*rewind=*/true);
            LogCatcher lc;
            m.dbg->set_listener(m.a, &lc);
            m.dbg->load_source_map(good.string(), false);
            m.dbg->set_trace_enabled(true);
            // The trace (10000 entries) holds the last frame and a bit: after
            // the state of frame 5 is taken and one more frame run, the entries
            // from before it have clocks below the load's, as a Frame Back leaves.
            run_frames(m, 5);
            const std::vector<uint8_t> s = own_state(m);
            run_frames(m, 1);
            const Result ld = m.dbg->load_state_bytes(m.a, s.data(), s.size());
            const uint64_t load_clock = m.emu.clock().get();
            const Result first = m.dbg->source_step(m.a, Step::Back);
            run_frames(m, 2);
            m.dbg->source_step(m.a, Step::Into);
            m.dbg->source_step(m.a, Step::Into);
            const Result second = m.dbg->source_step(m.a, Step::Back);
            check("SRCLIFE-08", "Back right after a state load refuses (the history before it is "
                                "another machine's); after more frames it lands on a post-load "
                                "entry",
                  ld == Result::Ok && first == Result::RefusedUnavailable &&
                      lc.has("no earlier mapped source position") && second == Result::Ok &&
                      m.emu.clock().get() >= load_clock,
                  "first=" + std::to_string(int(first)) + " second=" +
                      std::to_string(int(second)));
        }

        // SRCLIFE-09: load() attaches the program's sidecars.
        {
            Machine m;
            write_text(life / "l9.nex", "");
            write_text(life / "l9.sld", a_sld);
            write_text(life / "l9.Memory.txt", "8000: ._Main\n");
            jnext::dbg::LoopDriver d;
            d.load = [&m](const std::string&) { m.emu.mmu().write(0xC000, 0x99); return true; };
            m.dbg->set_loop_driver(d);
            const Result ld = m.dbg->load(m.a, (life / "l9.nex").string());
            check("SRCLIFE-09", "load() attaches the program's source map and symbols",
                  ld == Result::Ok && m.dbg->source_map().size() == 6 &&
                      m.dbg->lookup_name("Main") == 0x8000);
        }

        // SRCLIFE-10..14: a ring landing before the attach; the FIRST reader
        // (or pump) must see the processed state, and pump announces it.
        {
            const auto setup = [&](LogCatcher& lc) {
                auto m = std::make_unique<Machine>(/*rewind=*/true);
                m->dbg->set_listener(m->a, &lc);
                run_frames(*m, 3);
                attach_text(*m, "l10", a_sld, "8000: ._Main\n");
                const uint32_t f = m->dbg->time().frame;
                run_frames(*m, 3);
                m->dbg->rewind_to_frame(m->a, f - 1);
                return m;
            };
            LogCatcher l10, l11, l12, l13, l14;
            check("SRCLIFE-10", "after a rewind to before the attach the first reader "
                                "symbols() is already empty",
                  setup(l10)->dbg->symbols().empty());
            check("SRCLIFE-11", "... the first reader lookup() finds nothing",
                  !setup(l11)->dbg->lookup(0x8000).has_value());
            check("SRCLIFE-12", "... the first reader lookup_name() finds nothing",
                  !setup(l12)->dbg->lookup_name("Main").has_value());
            check("SRCLIFE-13", "... the first reader source_map() is empty",
                  setup(l13)->dbg->source_map().empty());
            {
                auto m = setup(l14);
                const bool before = l14.has("Dropped the program's");
                m->dbg->pump(jnext::dbg::PumpBudget{});
                                check("SRCLIFE-14", "... and pump() alone announces the drop to the listeners",
                      !before && l14.has("Dropped the program's SLD source map"));
            }
        }

        // SRCLIFE-15: a writer rechecks first, so the pending landing cannot
        // undo it afterwards.
        {
            Machine m(/*rewind=*/true);
            run_frames(m, 1);
            attach_text(m, "l15a", a_sld, "8000: ._S1sym\n");
            const uint32_t f1 = m.dbg->time().frame;
            run_frames(m, 3);
            attach_text(m, "l15b", a_sld, "8000: ._S2sym\n");
            run_frames(m, 3);
            m.dbg->rewind_to_frame(m.a, f1 + 1);
            m.dbg->clear_symbols();
            const bool gone = !m.dbg->lookup_name("S1sym").has_value();
            attach_text(m, "l15c", a_sld, "8000: ._S3sym\n");
            check("SRCLIFE-15", "clear_symbols() right after a rewind: the old symbol does not "
                                "come back, and the next program's fill the store",
                  gone && !m.dbg->lookup_name("S1sym").has_value() &&
                      m.dbg->lookup_name("S3sym") == 0x8000);
        }

        // SRCLIFE-16: load() reports a refused sidecar.
        {
            Machine m;
            LogCatcher lc;
            m.dbg->set_listener(m.a, &lc);
            write_text(life / "bad.nex", "");
            write_text(life / "bad.sld", "this is not an SLD\n");
            jnext::dbg::LoopDriver d;
            d.load = [](const std::string&) { return true; };
            m.dbg->set_loop_driver(d);
            const Result ld = m.dbg->load(m.a, (life / "bad.nex").string());
            const auto st = m.dbg->source_map_status();
            bool warn = false;
            for (size_t i = 0; i < lc.lines.size(); ++i)
                if (lc.levels[i] == jnext::dbg::LogLevel::Warn &&
                    lc.lines[i].find("rejected") != std::string::npos)
                    warn = true;
            check("SRCLIFE-16", "load() of a program with a refused sidecar tells every listener "
                                "(Warn) and the status names the file",
                  ld == Result::Ok && warn && st.state == State::None &&
                      st.reason.find("bad.sld") != std::string::npos,
                  st.reason);
        }

        // SRCLIFE-17: a rebuilt program's Reload attaches the new map.
        {
            Machine m;
            write_text(life / "l17.nex", "");
            jnext::dbg::LoopDriver d;
            d.load = [](const std::string&) { return true; };
            m.dbg->set_loop_driver(d);
            write_text(life / "l17.sld", a_sld);
            m.dbg->load(m.a, (life / "l17.nex").string());
            const std::string first = line_at(m, 0x8000);
            write_text(life / "l17.sld", x_sld(p80, {{"77", 0x8000}}));
            m.dbg->load(m.a, (life / "l17.nex").string());
            check("SRCLIFE-17", "a Reload of a rebuilt program attaches the new build's map",
                  first == "main.bas:10" && line_at(m, 0x8000) == "x.asm:77", line_at(m, 0x8000));
        }

        // SRCLIFE-18: a map loaded by hand across a Reload (decision D7).
        {
            Machine m;
            LogCatcher lc;
            m.dbg->set_listener(m.a, &lc);
            const fs::path user = life / "user.sld";
            write_text(user, a_sld);
            m.dbg->load_source_map(user.string(), false);
            write_text(life / "l18.nex", "");
            write_text(life / "l18.sld", b_sld);
            jnext::dbg::LoopDriver d;
            d.load = [](const std::string&) { return true; };
            m.dbg->set_loop_driver(d);
            m.dbg->load(m.a, (life / "l18.nex").string());
            const auto st = m.dbg->source_map_status();
            const bool first = st.state == State::Stale && st.by_user && st.file == user.string() &&
                               m.dbg->source_map().empty();
            m.dbg->load_source_map(good.string(), false);
            m.dbg->load(m.a, (life / "l18.nex").string());
            const auto st2 = m.dbg->source_map_status();
            check("SRCLIFE-18", "a map loaded by hand across a Reload stays the user's: stale "
                                "without an identity, live when the new build matches it",
                  first && st2.state == State::Attached && st2.by_user &&
                      m.dbg->source_map().size() == 6,
                  st.reason);
        }

        // SRCLIFE-19: the cache keeps the last 4 attachments.
        {
            Machine m(/*rewind=*/true);
            run_frames(m, 1);
            std::vector<uint32_t> frames;
            for (int k = 1; k <= 5; ++k) {
                attach_text(m, "l19_" + std::to_string(k),
                            x_sld(p80, {{std::to_string(k).c_str(), 0x8000}}));
                frames.push_back(m.dbg->time().frame);
                run_frames(m, 1);
            }
            const Result r1 = m.dbg->rewind_to_frame(m.a, frames[0] + 1);
            const bool g1_none = m.dbg->source_map().empty();
            const Result r2 = m.dbg->rewind_to_frame(m.a, frames[1] + 1);
            check("SRCLIFE-19", "five attachments: a rewind into the first finds none (evicted), "
                                "into the second finds the second's",
                  r1 == Result::Ok && r2 == Result::Ok && g1_none && line_at(m, 0x8000) == "x.asm:2",
                  line_at(m, 0x8000));
        }

        // SRCLIFE-20: a guest write is no landing: nothing is verified.
        {
            Machine m;
            attach_text(m, "l20", good_text);
            m.emu.mmu().write(0x8001, 0x01);
            run_frames(m, 1);
            check("SRCLIFE-20", "a guest write with no landing in between leaves the attached "
                                "map alone (design §6.6)",
                  m.dbg->source_map().size() == 6);
        }

        // SRCLIFE-21: Back's "first instruction of the statement" search does
        // not run on into the history before a state load. The state is another
        // machine's, at a later clock, at PC $8001: the post-load $8001 is the
        // second instruction of main.bas:10, and the instruction before it in
        // the trace ($8000) belongs to the machine before the load.
        {
            Machine m(/*rewind=*/true);
            m.dbg->load_source_map(good.string(), false);
            m.dbg->set_trace_enabled(true);
            run_frames(m, 2);
            m.set_pc(0x8000);
            m.dbg->step_into(m.a);                 // $8000 NOP -> PC=$8001
            const std::vector<uint8_t> s = foreign_state(0x00, true, 6, 0x8001);
            const Result ld = m.dbg->load_state_bytes(m.a, s.data(), s.size());
            const uint64_t load_clock = m.emu.clock().get();
            const Result into = m.dbg->source_step(m.a, Step::Into);    // $8001 -> $8002 (main.bas:11)
            const auto& t = m.emu.trace_log();
            const bool pre = t.size() >= 2 && t.at(t.size() - 1).pc == 0x8001 &&
                             t.at(t.size() - 2).pc == 0x8000 &&
                             t.at(t.size() - 2).cycle < load_clock;
            const Result r = m.dbg->source_step(m.a, Step::Back);
            check("SRCLIFE-21", "Back after Into past a state load does not land before the load",
                  ld == Result::Ok && pre && (r != Result::Ok || m.emu.clock().get() >= load_clock),
                  "r=" + std::to_string(int(r)) + " clock=" + std::to_string(m.emu.clock().get()) +
                      " load=" + std::to_string(load_clock) + " ld=" + std::to_string(int(ld)) +
                      " pre=" + std::to_string(int(pre)) + " n=" + std::to_string(t.size()));
        }

        // SRCLIFE-22: verification is pure (no watch, no latch). The check runs
        // as a guest read would be seen: inside the guest-execution scope.
        {
            Machine m;
            m.dbg->load_source_map(good.string(), false);
            int hits = 0;
            Subscription sub;
            sub.kind = EventKind::Mem;
            sub.access = Access::Read;
            sub.filter.lo = 0x8000;
            sub.filter.hi = 0x8003;
            sub.action = Action::Continue;
            sub.handler = [&hits](const jnext::dbg::Event&, Debugger&) {
                ++hits;
                return Action::Continue;
            };
            m.dbg->subscribe(m.a, sub);
            for (uint16_t p = 0x8000; p <= 0x8003; ++p)
                m.emu.debug_state().breakpoints().add_watchpoint(p, WatchType::READ);
            const std::vector<uint8_t> s = own_state(m);
            m.dbg->load_state_bytes(m.a, s.data(), s.size());
            const std::vector<uint8_t> before = own_state(m);
            const size_t ring_before = m.emu.debug_state().event_table()->size();
            Debugger::SourceMapStatus st;
            {
                DebugState::GuestExecutionScope guest(m.emu.debug_state());
                st = m.dbg->source_map_status();
            }
            const std::vector<uint8_t> after = own_state(m);
            check("SRCLIFE-22", "verifying a map's identity after a landing runs no Mem Read "
                                "handler, raises no watchpoint and changes no byte of the state",
                  st.state == State::Attached && hits == 0 && before == after &&
                      !m.emu.debug_state().watch_stop() &&
                      m.emu.debug_state().event_table()->size() == ring_before);
        }

        // SRCLIFE-23: after a Frame Back into A's history across B's load,
        // Back still goes back within A's generation.
        {
            Machine m(/*rewind=*/true);
            m.dbg->set_trace_enabled(true);
            attach_text(m, "l23a", a_sld);
            run_frames(m, 4);
            const uint32_t newest = m.dbg->rewind_range().newest_frame;
            const std::vector<uint8_t> b = foreign_state(0x00, true, 8);
            state_driver(m, &b);
            write_text(life / "l23b.sld", b_sld);
            m.dbg->load(m.a, (life / "l23b.nex").string());
            const Result rw = m.dbg->rewind_to_frame(m.a, newest);
            const uint64_t at = m.emu.clock().get();
            const Result r = m.dbg->source_step(m.a, Step::Back);
            const uint64_t first = m.emu.clock().get();
            // The Back replayed from an earlier slot: those instructions were
            // recorded in A's generation, so the next Back finds them (a
            // statement or two away, never a frame).
            const Result r2 = m.dbg->source_step(m.a, Step::Back);
            const uint64_t second = m.emu.clock().get();
            check("SRCLIFE-23", "A's history is 'back' within A's generation after a rewind into "
                                "it across B's load, and the replay of a Back is A's too",
                  rw == Result::Ok && r == Result::Ok && first < at && r2 == Result::Ok &&
                      second < first && first - second < m.emu.timing().master_cycles_per_frame,
                  "rw=" + std::to_string(int(rw)) + " r=" + std::to_string(int(r)) + " r2=" +
                      std::to_string(int(r2)) + " d=" + std::to_string(first - second));
        }

        // SRCLIFE-25: the instructions run right after a rewind from B's
        // generation into A's are A's (the generation is set before the load).
        {
            Machine m(/*rewind=*/true);
            m.dbg->set_trace_enabled(true);
            attach_text(m, "l25a", a_sld);
            run_frames(m, 4);
            const uint32_t newest = m.dbg->rewind_range().newest_frame;
            const std::vector<uint8_t> b = foreign_state(0x00, true, 8);
            state_driver(m, &b);
            write_text(life / "l25b.sld", b_sld);
            m.dbg->load(m.a, (life / "l25b.nex").string());
            const Result rw = m.dbg->rewind_to_frame(m.a, newest);
            const uint64_t at = m.emu.clock().get();
            m.emu.trace_log().clear();   // only what runs from here on
            const Result i1 = m.dbg->source_step(m.a, Step::Into);
            const Result i2 = m.dbg->source_step(m.a, Step::Into);
            const Result r = m.dbg->source_step(m.a, Step::Back);
            check("SRCLIFE-25", "Back finds the statements run since a rewind from another "
                                "generation: they belong to the generation rewound into",
                  rw == Result::Ok && i1 == Result::Ok && i2 == Result::Ok && r == Result::Ok &&
                      m.emu.clock().get() >= at,
                  "r=" + std::to_string(int(r)));
        }

        // SRCLIFE-26: SRCLIFE-01 with a sidecar that carries an identity. B's
        // state fails A's identity, which detaches A's map there - and a rewind
        // into A's history still brings it back (the cached record is A's).
        {
            Machine m(/*rewind=*/true);
            run_frames(m, 1);
            attach_text(m, "l26a", good_text);
            run_frames(m, 2);
            const uint32_t frame_a = m.dbg->time().frame;
            const std::vector<uint8_t> b_state = foreign_state(0xFF, true, 6);
            state_driver(m, &b_state);
            write_text(life / "l26b.sld", b_sld);
            const Result ld = m.dbg->load(m.a, (life / "l26b.nex").string());
            const bool gone = m.dbg->source_map().size() == 6 ? false : true;
            run_frames(m, 2);
            const uint32_t frame_b = m.dbg->time().frame;
            const Result ra = m.dbg->rewind_to_frame(m.a, frame_a);
            const std::string at_a = line_at(m, 0x8000);
            const Result rb = m.dbg->rewind_to_frame(m.a, frame_b);
            const std::string at_b = line_at(m, 0x8000);
            check("SRCLIFE-26", "load A (with an identity), load B whose bytes fail it, rewind into "
                                "A: A's map is back; into B: B's",
                  ld == Result::Ok && gone && ra == Result::Ok && rb == Result::Ok &&
                      at_a == "main.bas:10" && at_b == "x.asm:5",
                  "A=" + at_a + " B=" + at_b);
        }

        // SRCLIFE-27: a map detached at the next pause (the ROM cleared RAM
        // after a reset) does not come back by a rewind into that generation.
        {
            Machine m(/*rewind=*/true);
            run_frames(m, 1);
            attach_text(m, "l27", good_text);
            const uint32_t f1 = m.dbg->time().frame;
            run_frames(m, 1);
            m.dbg->reset(m.a, ResetKind::Soft);
            const bool kept = m.dbg->source_map_status().state == State::Attached;
            m.dbg->run(m.a);
            m.emu.mmu().write(0x8001, 0x55);
            for (int i = 0; i < 3; ++i) m.emu.run_frame();
            m.dbg->pause(m.a);
            const bool detached = m.dbg->source_map_status().state == State::None;
            const uint32_t f2 = m.dbg->rewind_range().newest_frame;
            const Result r1 = m.dbg->rewind_to_frame(m.a, f1 + 1);
            const bool back_in_g1 = m.dbg->source_map().size() == 6;
            const Result r2 = m.dbg->rewind_to_frame(m.a, f2);
            check("SRCLIFE-27", "a reset keeps the map, the next pause detaches it, and a rewind "
                                "out and back into that generation does not bring it back",
                  kept && detached && r1 == Result::Ok && back_in_g1 && r2 == Result::Ok &&
                      m.dbg->source_map().empty(),
                  "size=" + std::to_string(m.dbg->source_map().size()));
        }

        // SRCLIFE-28: a map that passed the identity check at a landing is
        // re-keyed AND cached under the new generation.
        {
            Machine m(/*rewind=*/true);
            run_frames(m, 1);
            attach_text(m, "l28", good_text);
            const uint32_t f1 = m.dbg->time().frame;
            run_frames(m, 1);
            const std::vector<uint8_t> same = foreign_state(0x00, true, 12);
            m.dbg->load_state_bytes(m.a, same.data(), same.size());
            const bool kept = m.dbg->source_map().size() == 6;
            const uint32_t f2 = m.dbg->time().frame;
            run_frames(m, 2);
            const Result r1 = m.dbg->rewind_to_frame(m.a, f1 + 1);
            const bool in_g1 = m.dbg->source_map().size() == 6;
            const Result r2 = m.dbg->rewind_to_frame(m.a, f2 + 1);
            check("SRCLIFE-28", "a same-code state load keeps the map; rewinding out to before it "
                                "and forward into the post-load slots brings it back",
                  kept && r1 == Result::Ok && in_g1 && r2 == Result::Ok &&
                      m.dbg->source_map().size() == 6);
        }

        // SRCLIFE-29: a hand-loaded map (no identity) across a tape load (a new
        // generation with no landing) is carried: a rewind within it keeps it.
        {
            Machine m(/*rewind=*/true);
            write_text(life / "l29.sld", a_sld);
            m.dbg->load_source_map((life / "l29.sld").string(), false);
            run_frames(m, 1);
            write_text(life / "l29t.tap", "");
            m.dbg->load_program_sidecars((life / "l29t.tap").string());
            const uint32_t f = m.dbg->time().frame;
            run_frames(m, 3);
            const Result rr = m.dbg->rewind_to_frame(m.a, f + 1);
            check("SRCLIFE-29", "a hand-loaded map is carried into the generation a tape load "
                                "opens: a rewind within it leaves it live",
                  rr == Result::Ok && m.dbg->source_map_status().state == State::Attached);
        }

        // SRCLIFE-30: the reason of a refused attach comes back with a rewind
        // into its generation.
        {
            Machine m(/*rewind=*/true);
            run_frames(m, 1);
            write_text(life / "l30a.nex", "");
            write_text(life / "l30a.sld", "this is not an SLD\n");
            m.dbg->load_program_sidecars((life / "l30a.nex").string());
            const std::string reason_a = m.dbg->source_map_status().reason;
            const uint32_t f = m.dbg->time().frame;
            run_frames(m, 2);
            attach_text(m, "l30b", a_sld);
            run_frames(m, 1);
            const Result rr = m.dbg->rewind_to_frame(m.a, f + 1);
            const auto st = m.dbg->source_map_status();
            check("SRCLIFE-30", "a rewind into the generation of a refused sidecar shows that "
                                "refusal again",
                  rr == Result::Ok && st.state == State::None && !reason_a.empty() &&
                      st.reason == reason_a,
                  st.reason);
        }

        // SRCLIFE-31: a hand-loaded map with an identity is stale after a
        // foreign state and live again after a rewind back (a cross-generation
        // rewind re-verifies it).
        {
            Machine m(/*rewind=*/true);
            m.dbg->load_source_map(good.string(), false);
            run_frames(m, 2);
            const uint32_t f = m.dbg->time().frame;
            run_frames(m, 1);
            const std::vector<uint8_t> other = foreign_state(0xFF, true, 1);
            m.dbg->load_state_bytes(m.a, other.data(), other.size());
            const auto st0 = m.dbg->source_map_status();
            const Result rr = m.dbg->rewind_to_frame(m.a, f);
            const auto st1 = m.dbg->source_map_status();
            check("SRCLIFE-31", "a hand-loaded map with an identity: stale after a state that "
                                "fails it, live again after a rewind to where it matched",
                  st0.state == State::Stale && rr == Result::Ok && st1.state == State::Attached);
        }

        // SRCLIFE-32: Back works after a hard reset that follows two soft
        // ones inside the trace window (the cold boot made a new TraceLog).
        {
            Machine m(/*rewind=*/true);
            with_cold_boot(m);
            m.dbg->set_trace_enabled(true);
            run_frames(m, 2);
            m.dbg->reset(m.a, ResetKind::Soft);
            run_frames(m, 1);
            m.dbg->reset(m.a, ResetKind::Soft);
            run_frames(m, 1);
            const Result hr = m.dbg->reset(m.a, ResetKind::Hard);
            const uint8_t main_prog[] = {0x00, 0x00, 0xCD, 0x00, 0x90, 0x00, 0xC3, 0x00, 0x80};
            for (size_t i = 0; i < sizeof(main_prog); ++i)
                m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), main_prog[i]);
            const uint8_t sub[] = {0x32, 0x00, 0xA0, 0x00, 0xC9};
            for (size_t i = 0; i < sizeof(sub); ++i)
                m.emu.mmu().write(static_cast<uint16_t>(0x9000 + i), sub[i]);
            m.set_pc(0x8000);
            // The cold boot rebuilt the ring too (rewind enabled from the debugger
            // is not in the config); turn it on again.
            m.emu.set_rewind_enabled(true);
            m.emu.resize_rewind_buffer(8);
            const auto lr = m.dbg->load_source_map(good.string(), false);
            m.dbg->set_trace_enabled(true);
            run_frames(m, 1);
            m.set_pc(0x8000);
            const Result i1 = m.dbg->source_step(m.a, Step::Into);
            const Result i2 = m.dbg->source_step(m.a, Step::Into);
            const Result b = m.dbg->source_step(m.a, Step::Back);
            check("SRCLIFE-32", "Back works after a cold boot that followed two soft resets in "
                                "the retained trace",
                  hr == Result::Ok && lr.count == 6 && i1 == Result::Ok && i2 == Result::Ok &&
                      b == Result::Ok,
                  "hr=" + std::to_string(int(hr)) + " lr=" + std::to_string(lr.count) + " i1=" + std::to_string(int(i1)) + " i2=" + std::to_string(int(i2)) + " back=" + std::to_string(int(b)));
        }

        // SRCLIFE-33: an attached sidecar's status names its file.
        {
            Machine m;
            attach_text(m, "l33", a_sld);
            const auto st = m.dbg->source_map_status();
            check("SRCLIFE-33", "the status of an attached sidecar map names its file and is "
                                "not by_user",
                  st.state == State::Attached && !st.by_user &&
                      st.file == (life / "l33.sld").string(),
                  st.file);
        }

        // SRCLIFE-34: a hand-loaded map without an identity, loaded after a
        // reset, stays live across a rewind within its own generation.
        {
            Machine m(/*rewind=*/true);
            m.dbg->reset(m.a, ResetKind::Soft);
            m.set_pc(0x8000);
            write_text(life / "l34.sld", a_sld);
            m.dbg->load_source_map((life / "l34.sld").string(), false);
            const uint32_t f = m.dbg->time().frame;
            run_frames(m, 3);
            const Result rr = m.dbg->rewind_to_frame(m.a, f + 1);
            check("SRCLIFE-34", "a hand-loaded map without an identity stays live across a rewind "
                                "within the generation it was loaded in",
                  rr == Result::Ok && m.dbg->source_map_status().state == State::Attached);
        }

        // SRCLIFE-35: loading a map again by hand cancels a check still owed
        // from the reset (D6: a fresh load was verified a moment ago).
        {
            Machine m;
            m.dbg->load_source_map(good.string(), false);
            m.dbg->reset(m.a, ResetKind::Soft);
            (void)m.dbg->source_map_status();
            m.dbg->load_source_map(good.string(), false);
            m.dbg->run(m.a);
            m.emu.mmu().write(0x8001, 0x55);
            m.emu.run_frame();
            m.dbg->pause(m.a);
            check("SRCLIFE-35", "a map loaded again by hand after a reset is not checked at the "
                                "next pause",
                  m.dbg->source_map_status().state == State::Attached);
        }

        // SRCLIFE-36: clearing a stale hand-loaded map leaves no stale flag
        // behind for the next sidecar.
        {
            Machine m;
            m.dbg->load_source_map(good.string(), false);
            const std::vector<uint8_t> other = foreign_state(0xFF, true, 1);
            m.dbg->load_state_bytes(m.a, other.data(), other.size());
            const bool stale = m.dbg->source_map_status().state == State::Stale;
            m.dbg->clear_source_map();
            attach_text(m, "l36", a_sld);
            check("SRCLIFE-36", "a stale hand-loaded map that is cleared does not hide the next "
                                "program's sidecar map",
                  stale && m.dbg->source_map().size() == 6);
        }

        // SRCLIFE-37: a source step on a stale hand-loaded map is refused.
        {
            Machine m;
            m.dbg->load_source_map(good.string(), false);
            const std::vector<uint8_t> other = foreign_state(0xFF, true, 1);
            m.dbg->load_state_bytes(m.a, other.data(), other.size());
            const bool stale = m.dbg->source_map_status().state == State::Stale;
            const Result r = m.dbg->source_step(m.a, Step::Into);
            check("SRCLIFE-37", "a source step on a stale hand-loaded map is refused",
                  stale && r == Result::RefusedUnavailable);
        }

        // SRCLIFE-38: a pause check owed by a reset does not judge the generation
        // a rewind lands in (it described the generation left).
        {
            Machine m(/*rewind=*/true);
            run_frames(m, 1);
            attach_text(m, "l38", good_text);
            run_frames(m, 1);
            m.emu.mmu().write(0x8001, 0x55);
            run_frames(m, 2);
            const uint32_t fmod = m.dbg->time().frame;
            m.emu.mmu().write(0x8001, 0x00);
            m.dbg->reset(m.a, ResetKind::Soft);
            const auto s0 = m.dbg->source_map_status();
            const Result rr = m.dbg->rewind_to_frame(m.a, fmod - 1);
            const auto s1 = m.dbg->source_map_status();
            check("SRCLIFE-38", "a reset leaves a pause check owed; a rewind into the earlier "
                                "generation (its bytes changed there) restores that generation's "
                                "map and the owed check does not detach it",
                  s0.state == State::Attached && rr == Result::Ok &&
                      m.emu.mmu().peek(0x8001) == 0x55 && s1.state == State::Attached &&
                      m.dbg->source_map().size() == 6);
        }

        // SRCLIFE-39: Back's statement-start
        // search STOPS at another generation's entries; it does not skip them.
        // A loops on x.asm:1, B (a state load) runs 3 instructions, a rewind
        // returns to A: A's replay is preceded in the trace by B's entries and,
        // before them, A's own older x.asm:1 entries that are not this pass.
        {
            Machine m(/*rewind=*/true);
            m.dbg->set_trace_enabled(true);
            const uint8_t prog[] = {0x00, 0x18, 0xFD, 0x00, 0x00, 0x00};  // NOP; JR $8000; NOP...
            for (size_t i = 0; i < sizeof(prog); ++i) m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
            m.set_pc(0x8000);
            attach_text(m, "pd3a", x_sld(p80, {{"1", 0x8000}, {"2", 0x8003}}));
            run_frames(m, 4);
            const uint32_t newest = m.dbg->rewind_range().newest_frame;
            const std::vector<uint8_t> b = foreign_state(0x00, true, 8);
            state_driver(m, &b);
            write_text(life / "pd3b.sld", b_sld);
            const Result ld = m.dbg->load(m.a, (life / "pd3b.nex").string());
            for (int i = 0; i < 3; ++i) m.dbg->step_into(m.a);
            const Result rw = m.dbg->rewind_to_frame(m.a, newest);
            const uint64_t at = m.emu.clock().get();
            for (int i = 0; i < 4; ++i) m.dbg->step_into(m.a);
            m.set_pc(0x8003);
            m.dbg->step_into(m.a);
            const Result r = m.dbg->source_step(m.a, Step::Back);
            const uint64_t after = m.emu.clock().get();
            check("SRCLIFE-39", "Back's statement-start search stops at another generation's entries",
                  ld == Result::Ok && rw == Result::Ok && r == Result::Ok && after >= at,
                  "r=" + std::to_string(int(r)) + " at=" + std::to_string(at) + " after=" +
                      std::to_string(after) + " now=" + m.where());
        }
    }

    // ── GH #341 / E23-09: source-line breakpoints on the canonical identity ──
    bool srcbp07_b = false;
    std::string srcbp07_detail;
    // The stores have no VHDL counterpart (design 13); the identity rows cite
    // the arbiter they rely on. Every row asserts the machine it stands on (the
    // fetch source and the effective page) so a wrong setup fails instead of
    // passing vacuously.
    {
        // Two RAM pages with the same code at $C000, a program that calls each in turn.
        auto bank_machine = [&](Machine& m) {
            for (uint8_t pg : {uint8_t(20), uint8_t(21)}) {
                m.dbg->nextreg_write(m.a, 0x56, pg);
                m.emu.mmu().write(0xC000, 0x04);   // INC B
                m.emu.mmu().write(0xC001, 0xC9);   // RET
            }
            const uint8_t prog[] = {0xED, 0x91, 0x56, 21,       // NEXTREG $56,21
                                    0xCD, 0x00, 0xC0,           // CALL $C000
                                    0xED, 0x91, 0x56, 20,       // NEXTREG $56,20
                                    0xCD, 0x00, 0xC0,           // CALL $C000
                                    0x18, 0xFE};                // JR $
            for (size_t i = 0; i < sizeof(prog); ++i)
                m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
            Z80Registers r = m.emu.cpu().get_registers();
            r.BC = 0;
            m.emu.cpu().set_registers(r);
            m.set_pc(0x8000);
        };
        const fs::path c_sld = dir / "bank.sld";
        write_text(c_sld, std::string("|SLD.data.version|1\n") + "main.c|1||0" + kNextDevice +
                              "main.c|19||0|20|49152|T|\n");

        // SRCBP-01 (zxnext.vhd:2952-2959, :3058-3065: MMU6 alone decides $C000).
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        bank_machine(m);
        m.dbg->load_source_map(c_sld.string(), false);
        const auto g = m.dbg->subscribe_source_line(m.a, "main.c:19", execute_stop(0));
        m.dbg->run(m.a);
        for (int i = 0; i < 4 && !m.dbg->state().paused; ++i) m.emu.run_frame();
        const auto subs = m.dbg->subscriptions(false);
        check("SRCBP-01", "a source breakpoint on page 20 fires at $C000 only while page 20 is "
                          "mapped there: the page-21 call runs through (B==1), the page-20 call "
                          "stops",
              static_cast<bool>(g) && subs.size() == 1 && subs[0].group == g.value &&
                  m.dbg->state().paused && m.pc() == 0xC000 &&
                  m.dbg->effective_page(0xC000) == 20 && (m.emu.cpu().get_registers().BC >> 8) == 1,
              m.where() + " page=" + std::to_string(m.dbg->effective_page(0xC000)) +
                  " B=" + std::to_string(m.emu.cpu().get_registers().BC >> 8));

        // SRCBP-06: the probe asks the same predicate.
        Machine q(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        bank_machine(q);
        q.dbg->load_source_map(c_sld.string(), false);
        const auto gq = q.dbg->subscribe_source_line(q.a, "main.c:19", execute_stop(0));
        const jnext::dbg::EventId member = q.dbg->subscriptions(false).at(0).id;
        auto probed = [&](uint8_t pg) {
            q.dbg->nextreg_write(q.a, 0x56, pg);
            const auto h = q.dbg->probe_execute(0xC000);
            return std::find(h.begin(), h.end(), member) != h.end();
        };
        const bool on21 = probed(21);
        const bool on20 = probed(20);
        check("SRCBP-06", "probe_execute lists the source breakpoint's member with page 20 "
                          "at slot 6, and not with page 21",
              static_cast<bool>(gq) && q.dbg->source_page(0xC000) == MemSpace::page(20) && on20 &&
                  !on21,
              "on21=" + std::to_string(on21) + " on20=" + std::to_string(on20));
    }
    {
        // SRCBP-02 (divmmc.vhd:94-96; zxnext.vhd:3037-3043 override "110", :3092-3098, :4180-4182):
        // NR 0x50 = 20 names page 20 at $0100, but under conmem + mapram DivMMC RAM bank 3 supplies it.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        m.dbg->nextreg_write(m.a, 0x50, 20);
        m.emu.mmu().write(0x0100, 0x04);   // page 20: INC B
        m.emu.mmu().write(0x0101, 0xC9);   // RET
        m.emu.divmmc().ram_data()[3 * 0x2000 + 0x0100] = 0x04;
        m.emu.divmmc().ram_data()[3 * 0x2000 + 0x0101] = 0xC9;
        const uint8_t prog[] = {0xCD, 0x00, 0x01,   // CALL $0100 (DivMMC RAM)
                                0x3E, 0x00,         // LD A,0
                                0xD3, 0xE3,         // OUT ($E3),A: conmem off
                                0xCD, 0x00, 0x01,   // CALL $0100 (page 20)
                                0x18, 0xFE};
        for (size_t i = 0; i < sizeof(prog); ++i)
            m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
        m.dbg->port_out(m.a, 0xE3, 0xC0);   // conmem + mapram
        Z80Registers r = m.emu.cpu().get_registers();
        r.BC = 0;
        m.emu.cpu().set_registers(r);
        m.set_pc(0x8000);
        const fs::path sld0 = dir / "divmmc-ram.sld";
        write_text(sld0, std::string("|SLD.data.version|1\n") + "main.c|1||0" + kNextDevice +
                             "main.c|19||0|20|256|T|\n");
        m.dbg->load_source_map(sld0.string(), false);
        const bool premise = m.dbg->source_page(0x0100) == MemSpace::divmmc_ram(3) &&
                             m.dbg->effective_page(0x0100) == 20;
        const auto g = m.dbg->subscribe_source_line(m.a, "main.c:19", execute_stop(0));
        m.dbg->run(m.a);
        for (int i = 0; i < 4 && !m.dbg->state().paused; ++i) m.emu.run_frame();
        check("SRCBP-02", "NR 0x50 naming page 20 is not the identity: a source breakpoint on "
                          "page 20 at $0100 does not stop in DivMMC RAM bank 3 (conmem+mapram), "
                          "and stops once page 20 itself supplies it",
              premise && static_cast<bool>(g) && m.dbg->state().paused && m.pc() == 0x0100 &&
                  !m.emu.divmmc().conmem() && m.dbg->source_page(0x0100) == MemSpace::page(20) &&
                  (m.emu.cpu().get_registers().BC >> 8) == 1,
              "premise=" + std::to_string(premise) + " " + m.where() + " B=" +
                  std::to_string(m.emu.cpu().get_registers().BC >> 8));

        // SRCBP-07(b): the DivMMC RAM space itself is an exact, matchable identity.
        Machine n(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        n.dbg->nextreg_write(n.a, 0x50, 20);
        n.emu.divmmc().ram_data()[3 * 0x2000 + 0x0100] = 0x00;   // NOP
        n.emu.mmu().write(0x8000, 0xC3);                           // JP $0100
        n.emu.mmu().write(0x8001, 0x00);
        n.emu.mmu().write(0x8002, 0x01);
        n.dbg->port_out(n.a, 0xE3, 0xC0);
        n.set_pc(0x8000);
        int ram_hits = 0, page_hits = 0;
        auto counting = [](int& n_hits, MemSpace sp) {
            Subscription s = execute_stop(0x0100);
            s.filter.space = sp;
            s.action = Action::Continue;
            s.handler = [&n_hits](const jnext::dbg::Event&, Debugger&) {
                ++n_hits;
                return Action::Continue;
            };
            return s;
        };
        n.dbg->subscribe(n.a, counting(ram_hits, MemSpace::divmmc_ram(3)));
        n.dbg->subscribe(n.a, counting(page_hits, MemSpace::page(20)));
        const bool premise_n = n.dbg->source_page(0x0100) == MemSpace::divmmc_ram(3);
        n.dbg->run(n.a);
        n.emu.run_frame();
        srcbp07_b = premise_n && ram_hits >= 1 && page_hits == 0;
        srcbp07_detail = "(b) ram=" + std::to_string(ram_hits) + " page=" + std::to_string(page_hits);
    }
    {
        // SRCBP-03: ReverseContinue asks the same predicate of the RECORDED fetch identity. A second
        // record (main.c:30 on page 21) is required: an entry the map does not cover is skipped
        // before the predicate is asked, so without it a space-blind predicate would never be tried.
        const fs::path rc_sld = dir / "reverse-bank.sld";
        write_text(rc_sld, std::string("|SLD.data.version|1\n") + "main.c|1||0" + kNextDevice +
                               "main.c|19||0|20|49152|T|\n" + "main.c|30||0|21|49152|T|\n");
        // A machine that has run both calls and both RETs, trace on.
        auto ran_machine = [&]() {
            auto m = std::make_unique<Machine>(/*rewind=*/true, MachineType::ZXN_ISSUE2);
            for (uint8_t pg : {uint8_t(20), uint8_t(21)}) {
                m->dbg->nextreg_write(m->a, 0x56, pg);
                m->emu.mmu().write(0xC000, 0x04);   // INC B
                m->emu.mmu().write(0xC001, 0xC9);   // RET
            }
            const uint8_t prog[] = {0xED, 0x91, 0x56, 20, 0xCD, 0x00, 0xC0,    // page 20 first
                                    0xED, 0x91, 0x56, 21, 0xCD, 0x00, 0xC0,    // page 21 last
                                    0x00, 0x00};
            for (size_t i = 0; i < sizeof(prog); ++i)
                m->emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
            m->set_pc(0x8000);
            m->dbg->load_source_map(rc_sld.string(), false);
            m->dbg->set_trace_enabled(true);
            for (int i = 0; i < 8; ++i) m->dbg->step_into(m->a);   // both calls and both RETs
            return m;
        };
        auto m = ran_machine();
        const bool ran = m->pc() == 0x800E && m->dbg->effective_page(0xC000) == 21;
        const auto g = m->dbg->subscribe_source_line(m->a, "main.c:19", execute_stop(0));
        const Result rc = m->dbg->source_step(m->a, Step::ReverseContinue);
        const bool by_space = static_cast<bool>(g) && rc == Result::Ok && m->pc() == 0xC000 &&
                              m->dbg->effective_page(0xC000) == 20;
        // The legacy page-number qualifier (DZRP, the DSL) is still asked of the recorded MMU page.
        auto n = ran_machine();
        Subscription legacy = execute_stop(0xC000);
        legacy.filter.page = 20;
        n->dbg->subscribe(n->a, legacy);
        const Result rl = n->dbg->source_step(n->a, Step::ReverseContinue);
        const bool by_page = rl == Result::Ok && n->pc() == 0xC000 && n->dbg->effective_page(0xC000) == 20;
        check("SRCBP-03", "Reverse Continue lands on the page-20 execution of $C000, not the "
                          "newer page-21 one, whose recorded fetch source is not the breakpoint's; "
                          "a legacy page-20 qualifier lands there too",
              ran && by_space && by_page,
              "ran=" + std::to_string(ran) + " rc=" + std::to_string(int(rc)) + " " + m->where() +
                  " page=" + std::to_string(m->dbg->effective_page(0xC000)) +
                  " legacy=" + std::to_string(by_page) + " " + n->where());
    }
    {
        // SRCBP-04: a line compiled to two places is one group of two subscriptions.
        const fs::path two = dir / "two-places.sld";
        write_text(two, program_sld(p80, p90, "main.bas|11||0|" + std::to_string(p90) + "|36864|T|\n"));
        Machine m;
        m.dbg->load_source_map(two.string(), false);
        jnext::dbg::ClientInfo ci;
        ci.name = "other";
        ci.kind = jnext::dbg::ClientKind::Test;
        const ClientId other = m.dbg->attach(ci).value;
        const auto g = m.dbg->subscribe_source_line(m.a, "main.bas:11", execute_stop(0));
        const auto subs = m.dbg->subscriptions(false);
        bool at8002 = false, at9000 = false;
        for (const auto& si : subs) {
            if (si.filter.lo == 0x8002 && si.filter.space == MemSpace::page(p80)) at8002 = true;
            if (si.filter.lo == 0x9000 && si.filter.space == MemSpace::page(p90)) at9000 = true;
        }
        const bool listed = static_cast<bool>(g) && g.value != jnext::dbg::GROUP_NONE &&
                            subs.size() == 2 && subs[0].group == g.value &&
                            subs[1].group == g.value && at8002 && at9000;
        const Result foreign = m.dbg->unsubscribe_group(other, g.value);
        const bool kept = foreign == Result::RefusedUnavailable && m.dbg->subscriptions(false).size() == 2;
        const Result gone = m.dbg->unsubscribe_group(m.a, g.value);
        const Result again = m.dbg->unsubscribe_group(m.a, g.value);
        check("SRCBP-04", "subscribe_source_line on a two-place line makes one group of two "
                          "Execute subscriptions, one per start with its space; only the owner "
                          "removes it, and unsubscribe_group removes both",
              listed && kept && gone == Result::Ok && again == Result::RefusedUnavailable &&
                  m.dbg->subscriptions(false).empty(),
              "listed=" + std::to_string(listed) + " kept=" + std::to_string(kept));

        // SRCBP-09: enabling acts on the group, and the run honours it.
        m.dbg->subscribe_source_line(m.a, "main.bas:11", execute_stop(0));
        const auto grp = m.dbg->subscriptions(false).at(0).group;
        m.dbg->set_group_enabled(m.a, grp, false);
        const auto off = m.dbg->subscriptions(false);
        const bool both_off = off.size() == 2 && !off[0].enabled && !off[1].enabled;
        m.dbg->run(m.a);
        m.emu.run_frame();
        const bool passed = !m.dbg->state().paused;
        m.dbg->pause(m.a);
        const Result foreign_en = m.dbg->set_group_enabled(other, grp, true);
        m.dbg->set_group_enabled(m.a, grp, true);
        const auto on = m.dbg->subscriptions(false);
        check("SRCBP-09", "set_group_enabled(false) disables every member and a run passes both "
                          "places with no stop; (true) enables them again; another client may not",
              both_off && passed && foreign_en == Result::RefusedUnavailable && on.size() == 2 &&
                  on[0].enabled && on[1].enabled,
              "both_off=" + std::to_string(both_off) + " passed=" + std::to_string(passed));
    }
    // ── run_to_source_line (GH #423; CTL-06 for a source line) ──────────
    {
        // The jnext debugger's own verb: no VHDL counterpart. Expected values are the
        // issue's semantics (the first place reached stops it, nothing is left behind).
        const auto run_until_stop = [](Machine& m, int frames = 4) {
            for (int i = 0; i < frames && !m.emu.debug_state().paused(); ++i) m.emu.run_frame();
        };
        const auto stop_is = [](Machine& m, unsigned addr) {
            const auto st = m.dbg->state();
            return st.paused && m.pc() == addr && st.pause_reason.kind == PauseReason::Kind::RunTo &&
                   st.pause_reason.addr == addr && st.pause_reason.by == m.a;
        };
        const fs::path both = dir / "run-both.sld";
        write_text(both, program_sld(p80, p90, "main.bas|12||0|" + std::to_string(p90) + "|36864|T|\n"));
        const fs::path banked = dir / "run-banked.sld";
        write_text(banked, program_sld_without(p80, p90, {12}) + "main.bas|12||0|" +
                               std::to_string(p80 + 2) + "|32773|T|\n");
        {
            Machine m;
            m.dbg->load_source_map(sld.string(), false);
            const Result r = m.dbg->run_to_source_line(m.a, "main.bas:12");
            run_until_stop(m);
            check("SRCRUN-01", "run_to_source_line(main.bas:12) from $8000 is accepted and stops at "
                               "$8005 with reason RunTo, addr $8005, by the caller; the transient "
                               "group is gone",
                  r == Result::Ok && stop_is(m, 0x8005) && m.dbg->subscriptions(true).empty(),
                  m.where());
        }
        {
            Machine m;
            m.dbg->load_source_map(both.string(), false);
            m.set_pc(0x8002);
            const Result r = m.dbg->run_to_source_line(m.a, "main.bas:12");
            const auto armed = m.dbg->subscriptions(true);
            const bool two = armed.size() == 2 && armed[0].transient && armed[1].transient &&
                             armed[0].group == armed[1].group && armed[0].group != jnext::dbg::GROUP_NONE &&
                             armed[0].filter.lo == 0x8005 && armed[0].filter.space == MemSpace::page(p80) &&
                             armed[1].filter.lo == 0x9000 && armed[1].filter.space == MemSpace::page(p90);
            run_until_stop(m);
            check("SRCRUN-02", "a line at two places arms one transient group of two members; from the "
                               "CALL the stop is at $9000 (reached first, RunTo addr $9000), and both "
                               "members are gone",
                  r == Result::Ok && two && stop_is(m, 0x9000) && m.dbg->subscriptions(true).empty(),
                  "two=" + std::to_string(two) + " " + m.where());
        }
        {
            Machine m;
            m.dbg->load_source_map(sld.string(), false);
            const Result nocode = m.dbg->run_to_source_line(m.a, "main.bas:3");
            const bool untouched = m.dbg->state().paused && m.pc() == 0x8000 &&
                                   m.dbg->subscriptions(true).empty();
            m.dbg->run(m.a);
            const Result running = m.dbg->run_to_source_line(m.a, "main.bas:3");
            const bool still_running = !m.dbg->state().paused && m.dbg->subscriptions(true).empty();
            m.dbg->pause(m.a);
            check("SRCRUN-03", "a line with no code is RefusedUnavailable with the machine untouched: "
                               "paused stays paused at the same PC, and a RUNNING machine keeps running",
                  nocode == Result::RefusedUnavailable && untouched &&
                      running == Result::RefusedUnavailable && still_running,
                  "nocode=" + std::to_string(int(nocode)) + " untouched=" + std::to_string(untouched) +
                      " running=" + std::to_string(int(running)) + " still=" + std::to_string(still_running));
        }
        {
            Machine m;
            m.dbg->load_source_map(banked.string(), false);
            const Result r = m.dbg->run_to_source_line(m.a, "main.bas:12");
            const auto armed = m.dbg->subscriptions(true);
            const bool one = armed.size() == 1 && armed[0].transient && armed[0].filter.lo == 0x8005 &&
                             armed[0].filter.space == MemSpace::page(p80 + 2);
            for (int i = 0; i < 20; ++i) m.emu.run_frame();
            const bool running = !m.dbg->state().paused;
            m.dbg->pause(m.a);
            check("SRCRUN-04", "a place in a page that is not mapped is one transient member tied to "
                               "that page: 20 frames passing $8005 in the mapped page do not stop it, "
                               "and a Pause leaves nothing armed",
                  r == Result::Ok && one && running && m.dbg->subscriptions(true).empty(),
                  "one=" + std::to_string(one) + " running=" + std::to_string(running));
        }
        {
            Machine m;
            m.dbg->load_source_map(sld.string(), false);
            const Result a = m.dbg->run_to_source_line(m.a, "main.bas:12");
            const bool running = !m.emu.debug_state().paused();
            const Result b = m.dbg->run_to_source_line(m.a, "main.bas:13");   // before a frame ran
            run_until_stop(m);
            check("SRCRUN-05", "GH #423: a second run_to_source_line on the RUNNING machine re-targets: "
                               "the first line's group is dropped and the stop is at the second ($8006, "
                               "RunTo addr $8006), passing $8005",
                  a == Result::Ok && running && b == Result::Ok && stop_is(m, 0x8006), m.where());
        }
        {
            Machine m;
            m.dbg->load_source_map(sld.string(), false);
            m.dbg->subscribe(m.a, execute_stop(0x8002));
            const Result r = m.dbg->run_to_source_line(m.a, "main.bas:12");
            run_until_stop(m);
            const auto left = m.dbg->subscriptions(true);
            check("SRCRUN-06", "a breakpoint crossed on the way to the line stops there with a reason "
                               "that is not RunTo, and no transient is left (the breakpoint stays)",
                  r == Result::Ok && m.dbg->state().paused && m.pc() == 0x8002 &&
                      m.dbg->state().pause_reason.kind != PauseReason::Kind::RunTo &&
                      left.size() == 1 && !left[0].transient,
                  m.where() + " kind=" + std::to_string(int(m.dbg->state().pause_reason.kind)));
        }
        {
            Machine m;
            m.dbg->load_source_map(sld.string(), false);
            const auto good = m.dbg->save_state_bytes(m.a, jnext::dbg::SaveStateMode::RefuseMidFrame);
            std::vector<uint8_t> bad = good.value;
            for (size_t i = bad.size() / 2; i < bad.size(); ++i) bad[i] ^= 0xFF;
            (void)m.dbg->load_state_bytes(m.a, bad.data(), bad.size());   // a torn machine (CTL-11)
            const Result as_run_to = m.dbg->run_to(m.a, 0x8005);
            const Result r = m.dbg->run_to_source_line(m.a, "main.bas:12");
            check("SRCRUN-07", "with the corruption gate closed run_to_source_line is refused as "
                               "run_to is (RefusedCorrupt) and arms nothing",
                  as_run_to == Result::RefusedCorrupt && r == Result::RefusedCorrupt &&
                      m.dbg->state().paused && m.dbg->subscriptions(true).empty());
        }
        {
            // A later verb does not inherit the previous source run-to's targets (arm() clears them).
            Machine m;
            m.dbg->load_source_map(sld.string(), false);
            m.dbg->run_to_source_line(m.a, "main.bas:12");
            run_until_stop(m);
            const bool first = stop_is(m, 0x8005);
            m.set_pc(0x8000);
            m.dbg->subscribe(m.a, execute_stop(0x8005));
            m.dbg->run_to(m.a, 0x8006);
            run_until_stop(m);
            const auto st = m.dbg->state();
            check("SRCRUN-08", "after a run to line 12 stopped at $8005, a later run_to($8006) that a "
                               "breakpoint at $8005 interrupts reads as the breakpoint's stop, not as RunTo "
                               "(the previous verb's targets are not inherited)",
                  first && st.paused && m.pc() == 0x8005 && st.pause_reason.kind != PauseReason::Kind::RunTo,
                  m.where() + " kind=" + std::to_string(int(st.pause_reason.kind)));
        }
    }
    {
        // SRCBP-07(a): a ROM fetch at $0000 is Rom{0}, whose effective page number is also a RAM page
        // number; only the space tells them apart (zxnext.vhd ROM branch, slot 0; mmu.cpp:374).
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        const uint8_t prog[] = {0xC3, 0x00, 0x00};   // JP $0000
        for (size_t i = 0; i < sizeof(prog); ++i)
            m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
        m.set_pc(0x8000);
        const unsigned eff = m.dbg->effective_page(0x0000);
        const bool premise = m.dbg->source_page(0x0000) == MemSpace::rom(0) && eff == 0;
        int page_hits = 0, rom_hits = 0;
        auto counting = [](int& n_hits, MemSpace sp) {
            Subscription s = execute_stop(0x0000);
            s.filter.space = sp;
            s.action = Action::Continue;
            s.handler = [&n_hits](const jnext::dbg::Event&, Debugger&) {
                ++n_hits;
                return Action::Continue;
            };
            return s;
        };
        m.dbg->subscribe(m.a, counting(page_hits, MemSpace::page(static_cast<uint16_t>(eff))));
        m.dbg->subscribe(m.a, counting(rom_hits, MemSpace::rom(0)));
        m.dbg->run(m.a);
        m.emu.run_frame();
        check("SRCBP-07", "the space comparison is exact, kind and index: a fetch from Rom{0} at $0000 "
                          "is not Page{effective page}, and DivMMC RAM bank 3 is not Page{20}",
              premise && page_hits == 0 && rom_hits >= 1 && srcbp07_b,
              "(a) premise=" + std::to_string(premise) + " eff=" + std::to_string(eff) +
                  " page=" + std::to_string(page_hits) + " rom=" + std::to_string(rom_hits) + " " +
                  srcbp07_detail);
    }
    {
        // SRCBP-08: a fetch-source qualifier on a kind that has no fetch is refused, not stored inert.
        Machine m(/*rewind=*/false, MachineType::ZXN_ISSUE2);
        Subscription mem;
        mem.kind = EventKind::Mem;
        mem.access = Access::Read;
        mem.filter.space = MemSpace::page(20);
        const auto r = m.dbg->subscribe(m.a, mem);
        check("SRCBP-08", "a Mem subscription carrying a space is refused and nothing is listed",
              !r && r.status == Result::RefusedUnavailable && m.dbg->subscriptions(true).empty());
    }
    {
        // SRCBP-10: subscribe_source_line arms nothing when it refuses.
        Machine m;
        const auto no_map = m.dbg->subscribe_source_line(m.a, "main.bas:11", execute_stop(0));
        m.dbg->load_source_map(sld.string(), false);
        const auto no_code = m.dbg->subscribe_source_line(m.a, "main.bas:5", execute_stop(0));
        Subscription mem = execute_stop(0);
        mem.kind = EventKind::Mem;
        mem.access = Access::Read;
        const auto bad_kind = m.dbg->subscribe_source_line(m.a, "main.bas:11", mem);
        check("SRCBP-10", "subscribe_source_line refuses with no map, a line with no code and a non-"
                          "Execute subscription, and arms nothing",
              !no_map && !no_code && !bad_kind && no_map.status == Result::RefusedUnavailable &&
                  no_code.status == Result::RefusedUnavailable &&
                  bad_kind.status == Result::RefusedUnavailable && m.dbg->subscriptions(true).empty() &&
                  m.dbg->source_map().resolve("main.bas:5").error.find("no code at") != std::string::npos);
    }
    {
        // SRCBP-11: GROUP_NONE is "not grouped", so it names no group: both verbs refuse it and the
        // client's plain subscriptions (group == GROUP_NONE too) are untouched; a removed group is
        // refused like an unknown one (debugger.h: owner only, RefusedUnavailable otherwise).
        const fs::path two = dir / "two-places-b.sld";
        write_text(two, program_sld(p80, p90, "main.bas|11||0|" + std::to_string(p90) + "|36864|T|\n"));
        Machine m;
        m.dbg->load_source_map(two.string(), false);
        m.dbg->subscribe(m.a, execute_stop(0x8000));
        const Result un_none = m.dbg->unsubscribe_group(m.a, jnext::dbg::GROUP_NONE);
        const Result en_none = m.dbg->set_group_enabled(m.a, jnext::dbg::GROUP_NONE, false);
        const auto plain = m.dbg->subscriptions(false);
        const bool intact = plain.size() == 1 && plain[0].enabled && plain[0].group == jnext::dbg::GROUP_NONE;
        const auto g = m.dbg->subscribe_source_line(m.a, "main.bas:11", execute_stop(0));
        m.dbg->unsubscribe_group(m.a, g.value);
        const Result en_gone = m.dbg->set_group_enabled(m.a, g.value, true);
        check("SRCBP-11", "unsubscribe_group and set_group_enabled refuse GROUP_NONE and leave a plain "
                          "subscription intact; set_group_enabled refuses a removed group",
              un_none == Result::RefusedUnavailable && en_none == Result::RefusedUnavailable && intact &&
                  static_cast<bool>(g) && en_gone == Result::RefusedUnavailable &&
                  m.dbg->subscriptions(false).size() == 1,
              "un=" + std::to_string(int(un_none)) + " en=" + std::to_string(int(en_none)) +
                  " intact=" + std::to_string(intact) + " gone=" + std::to_string(int(en_gone)));
    }
    {
        // SRCBP-12: a detached client cannot own a source breakpoint (the GH #276 B5 rule subscribe() keeps).
        Machine m;
        m.dbg->load_source_map(sld.string(), false);
        jnext::dbg::ClientInfo ci;
        ci.name = "gone";
        ci.kind = jnext::dbg::ClientKind::Test;
        const ClientId c = m.dbg->attach(ci).value;
        m.dbg->detach(c);
        const auto r = m.dbg->subscribe_source_line(c, "main.bas:11", execute_stop(0));
        check("SRCBP-12", "subscribe_source_line for a detached client is refused and arms nothing",
              !r && r.status == Result::RefusedUnavailable && m.dbg->subscriptions(true).empty());
    }

    // ── GH #375 stage 2: an SLD's labels reach the backend's symbol table ──
    // The fixtures are sjasmplus's own output (test/fixtures/sjasmequ/README.md):
    // equlabels.sld places `banked` at $C000 on page 20 and `other_bank` at $C000
    // on page 30, `SCREEN` unpaged at $4000, `main` at $8000 and `gfx.draw.loop`
    // at $8018 on page 4; its 12 accepted labels and 18 `T` records.
    {
        const fs::path fixtures = JNEXT_SJASMEQU_DIR;
        const auto sub = [&](const char* name) {
            fs::create_directories(dir / name);
            return dir / name;
        };
        const auto equlabels = [&]() {
            std::ifstream in(fixtures / "equlabels.sld", std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
        const std::string equ = equlabels();
        const auto at_c000 = [](Machine& m, unsigned page) {
            m.dbg->nextreg_write(m.a, 0x56, page);
            return m.dbg->source_page(0xC000) == MemSpace::page(page);
        };
        const fs::path user_map_sym = dir / "sym-user.map";
        write_text(user_map_sym, "UserSym = $8005 ; addr, local\n");

        {   // SYM-01 (R10) and SYM-02
            const fs::path d = sub("sym01");
            write_text(d / "lab1.sld", equ);
            Machine m(false, MachineType::ZXN_ISSUE2);
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "lab1.nex").string());
            });
            const bool p20 = at_c000(m, 20);
            const auto n20 = m.dbg->lookup(0xC000);
            const bool p30 = at_c000(m, 30);
            const auto n30 = m.dbg->lookup(0xC000);
            const bool p21 = at_c000(m, 21);
            const auto n21 = m.dbg->lookup(0xC000);
            check("SRCDBG-SYM-01", "a sidecar SLD's labels name an address by the page mapped there now",
                  r.symbols == 12 && r.sources == 18 &&
                      has(log, "Loaded 12 SLD labels from '" + (d / "lab1.sld").string() + "'") &&
                      p20 && p30 && p21 && n20 == std::optional<std::string>("banked") &&
                      n30 == std::optional<std::string>("other_bank") && !n21,
                  "symbols=" + std::to_string(r.symbols) + " sources=" + std::to_string(r.sources) +
                      " n20=" + n20.value_or("-") + " n30=" + n30.value_or("-") +
                      " n21=" + n21.value_or("-"));
            m.dbg->nextreg_write(m.a, 0x52, 10);
            const auto s10 = m.dbg->lookup(0x4000);
            m.dbg->nextreg_write(m.a, 0x52, 40);
            const auto s40 = m.dbg->lookup(0x4000);
            check("SRCDBG-SYM-02", "an unpaged label (page -1) names its address whatever is mapped",
                  s10 == std::optional<std::string>("SCREEN") &&
                      s40 == std::optional<std::string>("SCREEN"),
                  s10.value_or("-") + " " + s40.value_or("-"));

            // SYM-07: the next program's sidecars (none) take them away.
            fs::create_directories(d / "next");
            m.dbg->load_program_sidecars((d / "next" / "plain7.nex").string());
            check("SRCDBG-SYM-07", "the SLD's labels belong to the program: the next program without "
                                   "sidecars clears them",
                  m.dbg->symbols().empty() && !m.dbg->lookup_name("banked"));
        }
        {   // SYM-03
            const fs::path d = sub("sym03");
            write_text(d / "lab3.Memory.txt", "8000: ._Main\nC000: .banked\n");
            write_text(d / "lab3.sld", equ);
            Machine m(false, MachineType::ZXN_ISSUE2);
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "lab3.nex").string());
            });
            const auto loop = m.dbg->symbols().find_name("gfx.draw.loop");
            const auto banked = m.dbg->symbols().find_name("banked");
            const bool p30 = at_c000(m, 30);
            const auto n30 = m.dbg->lookup(0xC000);
            const bool p21 = at_c000(m, 21);
            const auto n21 = m.dbg->lookup(0xC000);
            check("SRCDBG-SYM-03", "Memory.txt and the SLD's labels make one table, Memory.txt first",
                  r.symbols == 14 && m.dbg->lookup_name("Main") == 0x8000 && loop &&
                      loop->addr == 0x8018 && loop->space && *loop->space == MemSpace::page(4) &&
                      banked && !banked->space && p30 && p21 &&
                      n30 == std::optional<std::string>("other_bank") &&
                      n21 == std::optional<std::string>("banked") &&
                      has(log, "Loaded 2 NextBuild symbols from") && has(log, "Loaded 12 SLD labels from"),
                  "symbols=" + std::to_string(r.symbols) + " n30=" + n30.value_or("-") +
                      " n21=" + n21.value_or("-"));
        }
        {   // SYM-04
            const fs::path d = sub("sym04");
            write_text(d / "lab4.tap", "");
            write_text(d / "lab4.sld", equ);
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto r = m.dbg->load_program_sidecars((d / "lab4.tap").string());
            const bool p20 = at_c000(m, 20);
            check("SRCDBG-SYM-04", "beside a tape program the SLD's labels attach",
                  r.symbols == 12 && p20 && m.dbg->lookup(0xC000) == std::optional<std::string>("banked"),
                  "symbols=" + std::to_string(r.symbols));
        }
        {   // SYM-05: an identity that does not match: neither half, either path
            const fs::path d = sub("sym05");
            Machine m;
            const std::string body =
                program_sld(p80, p90, "main.bas|10||0|" + std::to_string(p80) + "|32768|L|,Start,\n");
            const fs::path bad = d / "bad5.sld";
            write_text(bad, "|SLD.data.version|1\n||program.name:x\n"
                       "||program.sha256:9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a\n"
                       "||program.org:32768\n||program.size:4\n" + body.substr(20));
            const auto r = m.dbg->load_program_sidecars((d / "bad5.nex").string());
            const bool sidecar_none = r.sources == -1 && r.symbols == -1 && !m.dbg->lookup_name("Start") &&
                                      r.problem.find("bad5.sld' rejected") != std::string::npos;
            const auto refused = m.dbg->load_source_map(bad.string(), false);
            const bool hand_none = refused.count == -1 && refused.labels == -1 &&
                                   !m.dbg->lookup_name("Start");
            const auto forced = m.dbg->load_source_map(bad.string(), true);
            check("SRCDBG-SYM-05", "an SLD whose identity does not match the program attaches no labels; "
                                   "accepted by hand, it does",
                  sidecar_none && hand_none && forced.count == 6 && forced.labels == 1 &&
                      m.dbg->lookup_name("Start") == 0x8000,
                  r.problem);
        }
        {   // SYM-06
            const fs::path d = sub("sym06");
            write_text(d / "lab6.sld", equ);
            Machine m(false, MachineType::ZXN_ISSUE2);
            m.dbg->load_map(user_map_sym.string(), jnext::dbg::MapFormat::Z88dk);
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "lab6.nex").string());
            });
            check("SRCDBG-SYM-06", "a MAP the user loaded is kept over the SLD sidecar's labels",
                  r.symbols == -1 && r.sources == 18 && m.dbg->lookup_name("UserSym") == 0x8005 &&
                      !m.dbg->lookup_name("banked") && has(log, "Kept the loaded symbols"));
        }
        {   // SYM-08
            const fs::path d = sub("sym08");
            write_text(d / "m8.Memory.txt", "8000: ._Main\n");
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto h = m.dbg->load_source_map((fixtures / "equlabels.sld").string(), false);
            const bool p20 = at_c000(m, 20);
            const bool named = m.dbg->lookup(0xC000) == std::optional<std::string>("banked");
            const auto r = m.dbg->load_program_sidecars((d / "m8.nex").string());
            check("SRCDBG-SYM-08", "a hand-loaded SLD's labels are the user's symbols: a program's "
                                   "sidecars do not replace them",
                  h.count == 18 && h.labels == 12 && p20 && named && r.symbols == -1 &&
                      m.dbg->lookup_name("banked") == 0xC000 && !m.dbg->lookup_name("Main"),
                  "count=" + std::to_string(h.count) + " labels=" + std::to_string(h.labels));
        }
        {   // SYM-09
            Machine a(false, MachineType::ZXN_ISSUE2);
            a.dbg->load_map(user_map_sym.string(), jnext::dbg::MapFormat::Z88dk);
            const auto ra = a.dbg->load_source_map((fixtures / "equlabels.sld").string(), false);
            const bool part_a = ra.count == 18 && ra.labels == -1 && a.dbg->lookup_name("UserSym") &&
                                !a.dbg->lookup_name("banked");
            Machine b(false, MachineType::ZXN_ISSUE2);
            b.dbg->load_source_map((fixtures / "equlabels.sld").string(), false);
            b.dbg->load_map(user_map_sym.string(), jnext::dbg::MapFormat::Z88dk);
            const auto rb = b.dbg->load_source_map((fixtures / "sizeof.sld").string(), false);
            const bool part_b = rb.labels == -1 && b.dbg->lookup_name("UserSym") &&
                                !b.dbg->lookup_name("msg");
            check("SRCDBG-SYM-09", "a hand SLD never replaces a MAP the user loaded, whichever came first",
                  part_a && part_b,
                  "a=" + std::to_string(part_a) + " b=" + std::to_string(part_b));
        }
        {   // SYM-10
            const fs::path d = sub("sym10");
            write_text(d / "lab10.sld", equ + "x.asm|99||0|999|32768|T|\n");
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto r = m.dbg->load_program_sidecars((d / "lab10.nex").string());
            check("SRCDBG-SYM-10", "an SLD whose trace is malformed refuses the source half only: "
                                   "its labels still load",
                  r.sources == -1 && r.problem.find("lab10.sld' rejected") != std::string::npos &&
                      r.symbols == 12 && m.dbg->lookup_name("banked") == 0xC000,
                  "symbols=" + std::to_string(r.symbols) + " " + r.problem);
        }
        {   // SYM-12, SYM-13
            Machine m(false, MachineType::ZXN_ISSUE2);
            m.dbg->load_source_map((fixtures / "equlabels.sld").string(), false);
            const auto two = m.dbg->load_source_map((fixtures / "sizeof.sld").string(), false);
            const bool replaced = two.labels == 2 && m.dbg->lookup_name("msg") == 0x8004 &&
                                  !m.dbg->lookup_name("banked");
            const fs::path plain = dir / "sym12-plain.sld";
            write_text(plain, program_sld(p80, p90));
            const auto none = m.dbg->load_source_map(plain.string(), false);
            // Cleared, they are nobody's: the next program's sidecar symbols attach again.
            const fs::path d12 = sub("sym12");
            write_text(d12 / "m12.Memory.txt", "8000: ._Main\n");
            const auto next = m.dbg->load_program_sidecars((d12 / "m12.nex").string());
            check("SRCDBG-SYM-12", "a later hand SLD replaces an earlier hand SLD's labels; one with none "
                                   "clears them, and leaves the store free for a program's sidecar",
                  replaced && none.count == 6 && none.labels == 0 && next.symbols == 1 &&
                      m.dbg->lookup_name("Main") == 0x8000,
                  "two.labels=" + std::to_string(two.labels) + " none.labels=" + std::to_string(none.labels) +
                      " next=" + std::to_string(next.symbols));

            const fs::path d = sub("sym13");
            write_text(d / "m13.Memory.txt", "8000: ._Main\n");
            Machine n(false, MachineType::ZXN_ISSUE2);
            const auto r = n.dbg->load_program_sidecars((d / "m13.nex").string());
            const auto h = n.dbg->load_source_map(plain.string(), false);
            check("SRCDBG-SYM-13", "a hand SLD with no labels leaves a program's sidecar symbols alone",
                  r.symbols == 1 && h.labels == 0 && n.dbg->lookup_name("Main") == 0x8000,
                  "labels=" + std::to_string(h.labels));
        }
        {   // SYM-15: an unreadable Memory.txt beside an SLD with labels
            const fs::path d = sub("sym15");
            write_text(d / "lab15.sld", equ);
            write_text(d / "lab15.Memory.txt", "8000: ._Main\n");
            std::error_code ec;
            fs::resize_file(d / "lab15.Memory.txt", SOURCE_FILE_MAX_BYTES + 1, ec);
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto r = m.dbg->load_program_sidecars((d / "lab15.nex").string());
            const bool attached = m.dbg->lookup_name("banked") == 0xC000;
            fs::create_directories(d / "next");
            m.dbg->load_program_sidecars((d / "next" / "plain15.nex").string());
            check("SRCDBG-SYM-15", "an unreadable Memory.txt is reported and the SLD's labels still attach, "
                                   "counted alone, as the program's symbols",
                  !ec && r.symbols_unreadable && r.symbols == 12 && attached &&
                      r.problem.find("NextBuild symbols '") != std::string::npos &&
                      r.problem.find("rejected") != std::string::npos && m.dbg->symbols().empty(),
                  "symbols=" + std::to_string(r.symbols) + " " + r.problem);
        }
        {   // SYM-16: beside a tape, an SLD with an identity is not checked against memory
            const fs::path d = sub("sym16");
            write_text(d / "lab16.tap", "");
            write_text(d / "lab16.sld", "|SLD.data.version|1\n||program.name:x\n"
                       "||program.sha256:9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a\n"
                       "||program.org:32768\n||program.size:4\n" + equ.substr(20));
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto r = m.dbg->load_program_sidecars((d / "lab16.tap").string());
            check("SRCDBG-SYM-16", "beside a tape program the labels of an SLD that carries a binary identity "
                                   "attach too: the tape is not in memory to check it against",
                  r.symbols == 12 && m.dbg->lookup_name("banked") == 0xC000 && r.problem.empty(),
                  "symbols=" + std::to_string(r.symbols) + " " + r.problem);
        }
        {   // SYM-17: a hand SLD's labels replace a program's sidecar symbols
            const fs::path d = sub("sym17");
            write_text(d / "m17.Memory.txt", "8000: ._Main\n");
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto first = m.dbg->load_program_sidecars((d / "m17.nex").string());
            const auto h = m.dbg->load_source_map((fixtures / "equlabels.sld").string(), false);
            const bool replaced = h.labels == 12 && m.dbg->lookup_name("banked") == 0xC000 &&
                                  !m.dbg->lookup_name("Main");
            write_text(d / "m17b.Memory.txt", "8000: ._Other\n");
            const auto next = m.dbg->load_program_sidecars((d / "m17b.nex").string());
            check("SRCDBG-SYM-17", "a hand SLD with labels replaces a program's sidecar symbols, and they "
                                   "are then the user's: the next program's Memory.txt is kept out",
                  first.symbols == 1 && replaced && next.symbols == -1 && !m.dbg->lookup_name("Other"),
                  "labels=" + std::to_string(h.labels) + " next=" + std::to_string(next.symbols));
        }
        {   // SYM-14
            const fs::path d = sub("sym14");
            write_text(d / "lab14.sld", equ);
            Machine m(false, MachineType::ZXN_ISSUE2);
            const fs::path plain = dir / "sym14-plain.sld";
            write_text(plain, program_sld(p80, p90));
            m.dbg->load_source_map(plain.string(), false);
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "lab14.nex").string());
            });
            check("SRCDBG-SYM-14", "with the user's source map kept, the sidecar SLD is not loaded, "
                                   "labels included",
                  r.sources == -1 && r.symbols == -1 && !m.dbg->lookup_name("banked") &&
                      has(log, "Kept the loaded source map"));
        }
    }

    // ── GH #344: the z88dk -debug map ─────────────────────────────────────────
    // Every row loads a committed fixture (test/fixtures/z88dk): the program with
    // `load_nex()`, as a frontend does, then the sidecars. The oracle is the map
    // (main.c:19 starts at $8CB5, `_main` is $8C92), the NEX placement of its
    // bytes (prog_CODE.bin is bank 2 offset 0, i.e. page 4) and the compiler's
    // listing; design 6.1 / 9.4 / 9.7.
    {
        const fs::path z = dir / "z88";
        const fs::path d1 = z / "d1";
        z88_copy("v2.4/sccz80-debug", d1);
        const std::string map1 = (d1 / "prog.map").string();
        {   // Z88-01
            Machine m(false, MachineType::ZXN_ISSUE2);
            const bool ok = z88_load(m, d1 / "prog.nex");
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d1 / "prog.nex").string());
            });
            const auto st = m.dbg->source_map_status();
            check("SRCDBG-Z88-01", "prog.map beside a .nex attaches its symbols and its 46 source lines, "
                                   "identity verified from prog_CODE.bin",
                  ok && r.symbols == 19 && r.sources == 46 && m.dbg->source_map().size() == 46 &&
                      m.dbg->source_map().format() == "z88dk" &&
                      st.state == Debugger::SourceMapStatus::State::Attached && !st.by_user &&
                      line_of(*m.dbg, 0x8CB8) == "main.c:19" &&
                      m.dbg->lookup(0x8C92) == std::string("main") &&
                      has(log, "Loaded 19 z88dk symbols from '" + map1 + "'") &&
                      has(log, "Loaded 46 z88dk source lines from '" + map1 +
                                   "' (program identity verified)"),
                  "symbols=" + std::to_string(r.symbols) + " sources=" + std::to_string(r.sources) +
                      " " + line_of(*m.dbg, 0x8CB8) + "\n" + log);
        }
        {   // Z88-02
            const fs::path d = z / "d2";
            z88_copy("v2.4/sccz80-debug", d);
            write_text(d / "user.map", "_mine = $9000 ; addr, public, , m, code_compiler, x.c:1\n");
            Machine m(false, MachineType::ZXN_ISSUE2);
            m.dbg->load_map((d / "user.map").string(), jnext::dbg::MapFormat::Z88dk);
            z88_load(m, d / "prog.nex");
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            });
            check("SRCDBG-Z88-02", "the user's symbols are kept over prog.map's, whose source lines still attach",
                  r.symbols == -1 && r.sources == 46 && m.dbg->lookup_name("_mine") == 0x9000 &&
                      !m.dbg->lookup_name("_main") && has(log, "Kept the loaded symbols"),
                  log);
        }
        {   // Z88-03
            const fs::path d = z / "d3";
            fs::create_directories(d);
            fs::copy_file(d1 / "prog.map", d / "game.map");
            // The section binary is beside the tape: the program is not in memory yet, so its
            // identity is judged where the CPU first runs it, not now (owner decision 2026-10-29).
            fs::copy_file(d1 / "prog_CODE.bin", d / "game_CODE.bin");
            write_text(d / "game.tap", "");
            Machine m(false, MachineType::ZXN_ISSUE2);
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "game.tap").string());
            });
            check("SRCDBG-Z88-03", "game.map beside a tape attaches its symbols and, at once, its source "
                                   "lines, pending: the program is not in memory, so its identity is not "
                                   "judged now and the symbols do not go with a \"mismatch\"",
                  r.symbols == 19 && m.dbg->lookup_name("_main") == 0x8C92 && r.sources == 46 &&
                      r.sources_pending && m.dbg->source_map().size() == 46 &&
                      m.dbg->source_map_status().pending &&
                      has(log, "(identity check pending: the program loads from tape)"),
                  log);
        }
        {   // Z88-04
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d1 / "prog.nex");
            m.dbg->load_program_sidecars((d1 / "prog.nex").string());
            m.dbg->nextreg_write(m.a, 0x54, 40);
            const auto out = m.dbg->load_source_map(map1, true);
            const std::string at40 = line_of(*m.dbg, 0x8CB8);
            m.dbg->nextreg_write(m.a, 0x54, 4);
            const std::string at4 = line_of(*m.dbg, 0x8CB8);
            check("SRCDBG-Z88-04", "a map loaded by hand takes its rule-4 page from the MMU now: MMU4 = 40 "
                                   "puts main.c:19 in page 40 and nowhere in page 4",
                  out.count == 46 && out.identity && !*out.identity && at40 == "main.c:19" &&
                      at4 == "-",
                  at40 + " / " + at4);
        }
        {   // Z88-05
            const fs::path a = z / "d5a", b = z / "d5b";
            fs::create_directories(a);
            fs::copy_file(d1 / "prog.nex", a / "game.nex");
            z88_copy("v2.4/sccz80-debug", b);
            fs::remove(b / "prog.nex");
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, a / "game.nex");
            const auto out = m.dbg->load_source_map((b / "prog.map").string(), false);
            check("SRCDBG-Z88-05", "Load Source Map with a z88dk map: 46 lines, identity verified, user "
                                   "owned, and no symbols (they stay with Load MAP)",
                  out.count == 46 && out.identity && *out.identity &&
                      m.dbg->source_map().format() == "z88dk" && !m.dbg->lookup_name("_main") &&
                      out.identity_note == " (program identity verified)" &&
                      m.dbg->source_map_status().by_user,
                  out.error);
        }
        {   // Z88-06
            const fs::path d = z / "d6";
            z88_copy("v2.4/sccz80-page20", d);
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const std::string log = debugger_log_of([&] {
                m.dbg->load_program_sidecars((d / "prog.nex").string());
            });
            m.dbg->nextreg_write(m.a, 0x56, 20);
            const std::string at20 = line_of(*m.dbg, 0xC003);
            m.dbg->nextreg_write(m.a, 0x56, 21);
            const std::string at21 = line_of(*m.dbg, 0xC003);
            check("SRCDBG-Z88-06", "banked code (PAGE_20) is found while page 20 is mapped at $C000 and not "
                                   "under page 21; identity verified over both binaries",
                  at20 == "banked.c:8" && at21 == "-" && has(log, "(program identity verified)"),
                  at20 + " / " + at21 + "\n" + log);
        }
        {   // Z88-07
            const fs::path d = z / "d7";
            z88_copy("v2.4/sccz80-debug", d);
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            m.dbg->load_program_sidecars((d / "prog.nex").string());
            const Result soft = m.dbg->reset(m.a, ResetKind::Soft);
            const size_t kept = m.dbg->source_map().size();

            Machine n(false, MachineType::ZXN_ISSUE2);
            Z88Log lc;
            n.dbg->set_listener(n.a, &lc);
            z88_load(n, d / "prog.nex");
            n.dbg->load_program_sidecars((d / "prog.nex").string());
            uint8_t b = 0;
            n.dbg->peek(MemSpace::page(4), 0x0CB5, 1, &b);
            const uint8_t other = static_cast<uint8_t>(b ^ 0xFF);
            n.dbg->poke(n.a, MemSpace::page(4), 0x0CB5, 1, &other);
            const Result soft2 = n.dbg->reset(n.a, ResetKind::Soft);
            const size_t gone = n.dbg->source_map().size();
            check("SRCDBG-Z88-07", "a soft reset keeps a z88dk map while its section bytes are in the page "
                                   "and drops it when one differs (read through the peek path)",
                  soft == Result::Ok && kept == 46 && soft2 == Result::Ok && gone == 0 &&
                      lc.has("Dropped the program's z88dk source map: its binary identity does not "
                             "match the program now in memory"),
                  "kept=" + std::to_string(kept) + " gone=" + std::to_string(gone));
        }
        {   // Z88-08
            const fs::path d = z / "d8";
            z88_copy("v2.4/sccz80-debug", d);
            write_text(d / "prog.sld", program_sld(p80, p90));
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const auto r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            check("SRCDBG-Z88-08", "an SLD sidecar wins the source half over prog.map, whose symbols still attach",
                  r.sources == 6 && m.dbg->source_map().format() == "SLD" && r.symbols == 19);
        }
        {   // Z88-09
            const fs::path d = z / "d9";
            z88_copy("v2.4/sccz80-debug", d);
            write_text(d / "prog.Memory.txt", "8000: ._Main\n");
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const auto r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            check("SRCDBG-Z88-09", "a Memory.txt sidecar keeps prog.map's symbols out; its source lines attach",
                  r.symbols == 1 && m.dbg->lookup_name("Main") && !m.dbg->lookup_name("_main") &&
                      r.sources == 46 && m.dbg->source_map().format() == "z88dk");
        }
        {   // Z88-10
            const fs::path d = z / "d10";
            fs::create_directories(d);
            write_text(d / "a.nex", "");
            write_text(d / "a.sld", program_sld(p80, p90));
            write_text(d / "a.Memory.txt", "8000: ._Main\n");
            write_text(d / "b.nex", "");
            write_text(d / "b.map", "X = $1234\n");
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto first = m.dbg->load_program_sidecars((d / "a.nex").string());
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "b.nex").string());
            });
            check("SRCDBG-Z88-10", "a .map with no symbol and no line record is not a z88dk map: nothing "
                                   "attached, nothing reported, the previous program's sidecars cleared",
                  first.sources == 6 && first.symbols == 1 && r.symbols == -1 && r.sources == -1 &&
                      r.problem.empty() && m.dbg->symbols().empty() && m.dbg->source_map().empty() &&
                      !has(log, "z88dk"),
                  log);
        }
        {   // Z88-11
            const fs::path d = z / "d11";
            z88_copy("v2.4/sccz80-debug", d);
            {
                std::ofstream o(d / "prog.map", std::ios::app);
                o << "__C_LINE_99_x = $8CC0 ; addr, local, , main_c, code_compiler, main.c::main::0::2:abc\n";
            }
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const auto r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            check("SRCDBG-Z88-11", "one malformed line record refuses the source half, naming its line; the "
                                   "symbol half still attaches",
                  r.sources == -1 && r.symbols == 19 && m.dbg->source_map().empty() &&
                      has(r.problem, "source map '" + (d / "prog.map").string() + "' rejected: line "),
                  r.problem);
        }
        {   // Z88-12
            const fs::path d = z / "d12";
            z88_copy("v2.4/sccz80-debug", d, /*bins=*/false);
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            });
            check("SRCDBG-Z88-12", "without the section binaries the map attaches, and the log says the "
                                   "identity was not verified",
                  r.sources == 46 &&
                      has(log, "(program identity not verified: no section binaries beside it)"),
                  log);
        }
        {   // Z88-13
            const fs::path d = z / "d13";
            fs::create_directories(d);
            fs::copy_file(d1 / "prog.map", d / "game.map");
            write_text(d / "game.z80", "");
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto r = m.dbg->load_program_sidecars((d / "game.z80").string());
            check("SRCDBG-Z88-13", "<stem>.map is a sidecar of a .nex, .tap or .sna only: beside a .z80 "
                                   "nothing attaches",
                  r.symbols == -1 && r.sources == -1 && m.dbg->symbols().empty());
        }
        {   // Z88-14
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d1 / "prog.nex");
            m.dbg->load_program_sidecars((d1 / "prog.nex").string());
            m.dbg->subscribe(m.a, execute_stop(0x8CB5));
            m.dbg->run(m.a);
            for (int i = 0; i < 100 && !m.dbg->state().paused; ++i) m.emu.run_frame();
            const bool at_line = m.dbg->state().paused && m.pc() == 0x8CB5 &&
                                 line_of(*m.dbg, m.pc()) == "main.c:19";
            m.dbg->source_step(m.a, Step::Into);
            check("SRCDBG-Z88-14", "real z88dk code: a breakpoint at main.c:19's start stops there, and a "
                                   "source step ends at main.c:20's start ($8CBE)",
                  at_line && m.pc() == 0x8CBE && line_of(*m.dbg, m.pc()) == "main.c:20", m.where());
        }
        {   // COV-01 (GH #388): executed_source_lines on real z88dk code
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d1 / "prog.nex");
            m.dbg->load_program_sidecars((d1 / "prog.nex").string());
            m.dbg->mem_coverage_enable(true);
            m.dbg->subscribe(m.a, execute_stop(0x8CB5));
            m.dbg->run(m.a);
            for (int i = 0; i < 100 && !m.dbg->state().paused; ++i) m.emu.run_frame();
            const bool at19 = m.dbg->state().paused && m.pc() == 0x8CB5;
            const auto before = m.dbg->executed_source_lines("main.c");
            m.dbg->source_step(m.a, Step::Into);                     // runs line 19 (and square()) to 0x8CBE
            auto listed = [](const std::vector<uint32_t>& v, uint32_t l) {
                return std::find(v.begin(), v.end(), l) != v.end();
            };
            {   // line 20's first byte READ by the guest: an R mark, not an X
                DebugState::GuestExecutionScope g(m.emu.debug_state());
                (void)m.emu.mmu().read(0x8CBE);
            }
            const auto lines = m.dbg->executed_source_lines("main.c");
            const auto util  = m.dbg->executed_source_lines("util.c");
            const bool util_in_map = !m.dbg->source_map().file_ranges("util.c").empty();
            std::string got;
            for (uint32_t l : lines) got += std::to_string(l) + " ";
            check("SRCDBG-COV-01", "executed_source_lines: the lines a run executed are listed (main.c "
                                   "15, 17, 18 before the stop at 19's start; 19 after the step); a line not "
                                   "yet executed (20), a function never called (util.c is in the map) and a "
                                   "line with only R-marked bytes (20 was read) are not",
                  at19 && m.pc() == 0x8CBE && before == std::vector<uint32_t>({15, 17, 18}) &&
                      lines == std::vector<uint32_t>({15, 17, 18, 19}) && !listed(lines, 20) &&
                      util_in_map && util.empty() && m.dbg->executed_source_lines("nosuch.c").empty(),
                  "lines=" + got + " pc=" + hex(m.pc()));
        }
        {   // COV-02 (GH #388): a banked line, ranges in two pages
            const unsigned pa = 0x30, pb = 0x31;
            const fs::path two = dir / "cov2.sld";
            write_text(two, std::string("|SLD.data.version|1\n") + "x.asm|1||0" + kNextDevice +
                                "x.asm|7||0|" + std::to_string(pa) + "|32768|T|\n" +
                                "x.asm|7||0|" + std::to_string(pb) + "|32768|T|\n" +
                                "x.asm|8||0|" + std::to_string(pa) + "|32769|T|\n");
            Machine m(false, MachineType::ZXN_ISSUE2);
            m.emu.mmu().set_boot_rom_enabled(false);
            m.emu.mmu().set_config_mode(false);
            for (uint8_t pg : { pb, pa }) {
                m.emu.mmu().set_page(4, pg);
                m.emu.mmu().write(0x8000, 0x00);      // NOP
                m.emu.mmu().write(0x8001, 0x00);      // NOP
                m.emu.mmu().write(0x8002, 0x18);      // JR $
                m.emu.mmu().write(0x8003, 0xFE);
            }
            m.dbg->load_source_map(two.string(), false);
            m.dbg->mem_coverage_enable(true);
            m.set_pc(0x8000);
            m.dbg->step_into(m.a);                    // one NOP of Page{pa}'s copy
            auto has_line = [&](uint32_t l) {
                const auto v = m.dbg->executed_source_lines("x.asm");
                return std::find(v.begin(), v.end(), l) != v.end();
            };
            const bool only_a = has_line(7) && !has_line(8);   // line 7 via Page{pa}; Page{pb} never ran
            m.dbg->mem_coverage_clear();
            const bool cleared = !has_line(7);
            // run neither: switch the mapping to a THIRD page holding the same bytes
            m.emu.mmu().set_page(4, 0x32);
            for (uint16_t a = 0x8000; a < 0x8004; ++a) m.emu.mmu().write(a, a == 0x8002 ? 0x18 : (a == 0x8003 ? 0xFE : 0x00));
            m.set_pc(0x8000);
            m.dbg->step_into(m.a);
            const bool third_page = !has_line(7) && !has_line(8);
            // And only Page{pb}'s copy (the SECOND range of line 7 in the map): still listed, and
            // line 8, which has a range in Page{pa} alone, is not.
            m.dbg->mem_coverage_clear();
            m.emu.mmu().set_page(4, pb);
            m.set_pc(0x8000);
            m.dbg->step_into(m.a);
            const bool second_copy = has_line(7) && !has_line(8);
            check("SRCDBG-COV-02", "a line with ranges in two pages is listed once a copy in either page "
                                   "ran (only Page{p1} ran: listed; only Page{p2} ran: listed); after Clear it is not, and the same "
                                   "bytes executed in a third page do not list it",
                  only_a && cleared && third_page && second_copy,
                  std::string("only_a=") + std::to_string(only_a) + " cleared=" + std::to_string(cleared) +
                      " third=" + std::to_string(third_page) + " second=" + std::to_string(second_copy));
        }
        {   // Z88-18: some code ranges have no binary
            const fs::path d = z / "d18";
            z88_copy("v2.4/sccz80-page20", d);
            fs::remove(d / "prog_PAGE_20.bin");
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const std::string log = debugger_log_of([&] {
                m.dbg->load_program_sidecars((d / "prog.nex").string());
            });
            check("SRCDBG-Z88-18", "with only some of the section binaries the identity is verified for "
                                   "the code they cover, and the log counts the ranges it did not check",
                  has(log, "(program identity verified; ") && has(log, " code ranges have no section "
                                                                       "binary and were not checked)"),
                  log);
        }
        {   // Z88-19: the --source-map helper
            const fs::path d = z / "d19";
            z88_copy("v2.4/sccz80-debug", d);
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const auto good = emulator_attach_cli_source_map(*m.dbg, (d / "prog.map").string());
            // The same program, one of its code bytes changed: refused.
            Machine n(false, MachineType::ZXN_ISSUE2);
            z88_load(n, d / "prog.nex");
            uint8_t b = 0;
            n.dbg->peek(MemSpace::page(4), 0x0CB5, 1, &b);
            const uint8_t other = static_cast<uint8_t>(b ^ 0xFF);
            n.dbg->poke(n.a, MemSpace::page(4), 0x0CB5, 1, &other);
            const auto bad = emulator_attach_cli_source_map(*n.dbg, (d / "prog.map").string());
            check("SRCDBG-Z88-19", "--source-map's helper attaches a matching map as the user's and "
                                   "refuses a mismatching one, with a line to log each",
                  good.ok && good.line == "--source-map '" + (d / "prog.map").string() +
                                              "': 46 source lines (program identity verified)" &&
                      m.dbg->source_map_status().by_user && !bad.ok && n.dbg->source_map().empty() &&
                      has(bad.line, "does not match the source map's binary identity"),
                  good.line + " | " + bad.line);
        }
        {   // Z88-20: a hand load of a map with no line records
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d1 / "prog.nex");
            m.dbg->load_source_map(map1, true);
            const auto out = m.dbg->load_source_map(kZ88NoDebug.string(), true);
            check("SRCDBG-Z88-20", "a z88dk map built without -debug is refused by hand with a reason, and "
                                   "the map already loaded is kept",
                  out.count == -1 && has(out.error, "no source lines in") &&
                      has(out.error, "built without -debug") && m.dbg->source_map().size() == 46);
        }
        {   // SRCDBG-LIS-01..04 (GH #348): the --list listings beside a z88dk map
            const fs::path d = z / "lis";
            z88_copy_lis("v2.4/sdcc-SO2", d);
            const std::string map = (d / "prog.map").string();
            {   // LIS-01
                Machine m(false, MachineType::ZXN_ISSUE2);
                const bool ok = z88_load(m, d / "prog.nex");
                Debugger::SidecarLoad r;
                const std::string log = debugger_log_of([&] {
                    r = m.dbg->load_program_sidecars((d / "prog.nex").string());
                });
                check("SRCDBG-LIS-01", "the sidecar attaches the listings' 230 source lines, the log says "
                                       "'and 4 listings', the identity is verified, and $8C6F is main.c:18",
                      ok && r.sources == 230 && line_of(*m.dbg, 0x8C6F) == "main.c:18" &&
                          has(log, "Loaded 230 z88dk source lines from '" + map + "' and 4 listings") &&
                          has(log, "(program identity verified") && !has(log, "not verified"),
                      std::to_string(r.sources) + "\n" + log);
            }
            {   // LIS-02
                Machine m(false, MachineType::ZXN_ISSUE2);
                z88_load(m, d / "prog.nex");
                const auto out = m.dbg->load_source_map(map, true);
                const fs::path bare = z / "lis-bare";
                z88_copy("v2.4/sdcc-SO2", bare);
                Machine n(false, MachineType::ZXN_ISSUE2);
                z88_load(n, bare / "prog.nex");
                const auto none = n.dbg->load_source_map((bare / "prog.map").string(), true);
                check("SRCDBG-LIS-02", "a hand load gives 230 lines from 4 listings and no note; the same map "
                                       "with no listings is refused, the reason pointing at --list",
                      out.count == 230 && out.listings == 4 && out.listing_note.empty() &&
                          none.count == -1 && has(none.error, "no source lines in") &&
                          has(none.error, "--list"),
                      none.error);
            }
            {   // LIS-03
                Machine m(false, MachineType::ZXN_ISSUE2);
                z88_load(m, d / "prog.nex");
                const auto line = emulator_attach_cli_source_map(*m.dbg, map);
                check("SRCDBG-LIS-03", "--source-map's line carries the listing count",
                      line.ok && has(line.line, "230 source lines") && has(line.line, "(4 z88dk listings)"),
                      line.line);
            }
            {   // LIS-04
                const fs::path st = z / "lis-stale";
                z88_copy_lis("v2.4/sdcc-SO2", st);
                {
                    std::string text;
                    {
                        std::ifstream in(st / "main.c.lis", std::ios::binary);
                        text.assign(std::istreambuf_iterator<char>(in), {});
                    }
                    const size_t at = text.find("0023  af");
                    if (at != std::string::npos) text.replace(at, 8, "0023  b7");
                    write_text(st / "main.c.lis", text);
                }
                Machine m(false, MachineType::ZXN_ISSUE2);
                z88_load(m, st / "prog.nex");
                Debugger::SidecarLoad r;
                const std::string log = debugger_log_of([&] {
                    r = m.dbg->load_program_sidecars((st / "prog.nex").string());
                });
                Machine n(false, MachineType::ZXN_ISSUE2);
                z88_load(n, st / "prog.nex");
                const auto hand = n.dbg->load_source_map((st / "prog.map").string(), true);
                check("SRCDBG-LIS-04", "a stale main.c.lis is dropped alone: the sidecar attaches the rest and "
                                       "warns naming it; a hand load carries the note",
                      r.sources > 0 && r.sources < 230 && has(log, "main.c.lis") &&
                          has(log, "and 3 listings") && hand.count > 0 && hand.listings == 3 &&
                          has(hand.listing_note, "main.c.lis"),
                      log);
            }
        }
        {   // SRCDBG-LIS-05..07 (GH #348 stage 2 review): what the user is told when listings are dropped
            auto edit = [](const fs::path& file, const std::string& from, const std::string& to) {
                std::string text;
                {
                    std::ifstream in(file, std::ios::binary);
                    text.assign(std::istreambuf_iterator<char>(in), {});
                }
                const size_t at = text.find(from);
                if (at != std::string::npos) text.replace(at, from.size(), to);
                write_text(file, text);
            };
            {   // LIS-05: the only listing is stale
                const fs::path d = z / "lis-only-stale";
                z88_copy("v2.4/sdcc-SO2", d);
                fs::copy_file(kZ88Fixtures / "v2.4/sdcc-SO2/main.c.lis", d / "main.c.lis");
                edit(d / "main.c.lis", "0023  af", "0023  b7");
                Machine m(false, MachineType::ZXN_ISSUE2);
                z88_load(m, d / "prog.nex");
                Debugger::SidecarLoad r;
                const std::string log = debugger_log_of([&] {
                    r = m.dbg->load_program_sidecars((d / "prog.nex").string());
                });
                Machine n(false, MachineType::ZXN_ISSUE2);
                z88_load(n, d / "prog.nex");
                const auto hand = n.dbg->load_source_map((d / "prog.map").string(), true);
                check("SRCDBG-LIS-05", "with no usable listing left the sidecar still warns naming the dropped "
                                       "one and why, and a refused hand load carries the same note",
                      m.dbg->source_map().empty() && has(log, "z88dk listings for '" + (d / "prog.map").string()) &&
                          has(log, "main.c.lis") && has(log, "$8C6B") &&
                          hand.count == -1 && has(hand.error, "main.c.lis") && has(hand.error, "--list"),
                      log + "\n" + hand.error);
            }
            {   // LIS-06: two dropped listings, the --source-map line and the joined note
                const fs::path d = z / "lis-two-stale";
                z88_copy_lis("v2.4/sdcc-SO2", d);
                edit(d / "main.c.lis", "0023  af", "0023  b7");
                edit(d / "asmfn.asm.lis", "0000  210000", "0000  310000");
                Machine m(false, MachineType::ZXN_ISSUE2);
                z88_load(m, d / "prog.nex");
                const auto line = emulator_attach_cli_source_map(*m.dbg, (d / "prog.map").string());
                const size_t a = line.line.find("asmfn.asm.lis");
                const size_t b = line.line.find("main.c.lis");
                check("SRCDBG-LIS-06", "--source-map's line carries the notes of two dropped listings, both named "
                                       "and joined with '; ', beside the count of the two that were used",
                      line.ok && has(line.line, "(2 z88dk listings)") && a != std::string::npos &&
                          b != std::string::npos && a < b && line.line.find("; ", a) != std::string::npos,
                      line.line);
            }
            {   // LIS-07: one listing is singular everywhere
                const fs::path d = z / "lis-one";
                z88_copy_lis("v2.4/sccz80-incmid", d);
                Machine m(false, MachineType::ZXN_ISSUE2);
                z88_load(m, d / "prog.nex");
                Debugger::SidecarLoad r;
                const std::string log = debugger_log_of([&] {
                    r = m.dbg->load_program_sidecars((d / "prog.nex").string());
                });
                Machine n(false, MachineType::ZXN_ISSUE2);
                z88_load(n, d / "prog.nex");
                const auto line = emulator_attach_cli_source_map(*n.dbg, (d / "prog.map").string());
                check("SRCDBG-LIS-07", "a single listing reads 'and 1 listing' in the log and '(1 z88dk listing)' "
                                       "in --source-map's line",
                      r.sources > 0 && has(log, " and 1 listing") && !has(log, "1 listings") &&
                          has(line.line, "(1 z88dk listing)") && !has(line.line, "1 z88dk listings"),
                      log + "\n" + line.line);
            }
        }
        {   // Z88-21: sidecar identity mismatch
            const fs::path d = z / "d21";
            z88_copy("v2.4/sccz80-debug", d);
            // The binary beside the map is another build's: one code byte differs.
            std::string bin;
            {
                std::ifstream in(d / "prog_CODE.bin", std::ios::binary);
                bin.assign(std::istreambuf_iterator<char>(in), {});
            }
            bin[0x0CB5] = static_cast<char>(bin[0x0CB5] ^ 0xFF);
            write_text(d / "prog_CODE.bin", bin);
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            });
            check("SRCDBG-Z88-21", "a z88dk map whose binaries are not the loaded program's is rejected, "
                                   "symbols included, with the reason",
                  r.sources == -1 && r.symbols == -1 && m.dbg->source_map().empty() &&
                      m.dbg->symbols().empty() &&
                      has(log, "Rejected z88dk source map") && has(r.problem, "rejected"),
                  log);
        }
        {   // Z88-22: a map built without -debug beside a .nex
            const fs::path d = z / "d22";
            fs::create_directories(d);
            fs::copy_file(kZ88NoDebug, d / "game.map");
            write_text(d / "game.nex", "");
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto r = m.dbg->load_program_sidecars((d / "game.nex").string());
            check("SRCDBG-Z88-22", "a map built without -debug beside a .nex attaches its symbols and "
                                   "reports no source half and no problem",
                  r.symbols > 0 && r.sources == -1 && r.problem.empty() &&
                      m.dbg->lookup_name("_main") && m.dbg->source_map().empty());
        }
        {   // Z88-23: the identity check changes nothing in the machine, live (PURE-01's fixture)
            const fs::path d = z / "d23";
            z88_copy("v2.4/sccz80-page20", d);
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            auto& emu = m.emu;
            // Live: +3 timing, a contended page behind $C000 (where page 20's code is
            // checked by its logical address in a careless reader), the floating-bus
            // latch set, READ watches over checked ranges, a Mem subscription.
            emu.mmu().set_machine_timing(MachineTimingMode::TimingPlus3);
            m.dbg->nextreg_write(m.a, 0x56, 0x08);
            emu.mmu().set_p3_floating_bus_dat(0x3C);
            for (uint16_t at : {0xC003, 0xC010, 0x8CB5})
                emu.debug_state().breakpoints().add_watchpoint(at, WatchType::READ);
            Subscription sub;
            sub.kind = EventKind::Mem;
            sub.access = Access::Read;
            sub.filter.lo = 0x0000;
            sub.filter.hi = 0xFFFF;
            sub.action = Action::Continue;
            m.dbg->subscribe(m.a, sub);
            auto stream = [&] {
                StateWriter measure;
                emu.save_state(measure);
                std::vector<uint8_t> bytes(measure.position());
                StateWriter w(bytes.data(), bytes.size());
                emu.save_state(w);
                return bytes;
            };
            auto ring = [&] { return emu.debug_state().event_table()->size(); };
            const std::vector<uint8_t> before = stream();
            const size_t ring_before = ring();
            Debugger::SourceMapLoad out;
            {
                DebugState::GuestExecutionScope guest(emu.debug_state());
                out = m.dbg->load_source_map((d / "prog.map").string(), false);
            }
            const bool untouched = before == stream() && !emu.debug_state().data_bp_hit() &&
                                   ring() == ring_before && emu.mmu().p3_floating_bus_dat() == 0x3C;
            check("SRCDBG-Z88-23", "verifying a z88dk map's identity over a mapped page and an unmapped "
                                   "one, inside the guest's scope with watches armed, changes nothing: "
                                   "the save stream, the latch, the watch flag and the event ring",
                  out.count > 0 && out.identity && *out.identity && untouched,
                  "stream_equal=" + std::to_string(before == stream()) + " hit=" +
                      std::to_string(emu.debug_state().data_bp_hit()) + " ring=" +
                      std::to_string(ring_before) + "->" + std::to_string(ring()) + " latch=" +
                      hex(emu.mmu().p3_floating_bus_dat()));
            // Control: ONE guest read of $C003 in the same fixture moves all of them, so the
            // row above cannot pass because nothing here is live.
            {
                DebugState::GuestExecutionScope guest(emu.debug_state());
                (void)emu.mmu().read(0xC003);
            }
            check("SRCDBG-Z88-28", "control for Z88-23: ONE guest read of $C003 in the same fixture moves "
                                   "the save stream, raises the watchpoint and enters the event ring",
                  before != stream() && emu.debug_state().data_bp_hit() && ring() > ring_before,
                  "hit=" + std::to_string(emu.debug_state().data_bp_hit()) + " ring=" +
                      std::to_string(ring()));
        }
        {   // Z88-24: a refused SLD sidecar still speaks for the source half
            const fs::path d = z / "d24";
            z88_copy("v2.4/sccz80-debug", d);
            write_text(d / "prog.sld", "this is not an SLD\n");
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const auto r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            check("SRCDBG-Z88-24", "an SLD sidecar that is refused is not replaced by prog.map's source "
                                   "lines (it exists, and says why); the map's symbols still attach",
                  r.sources == -1 && m.dbg->source_map().empty() && r.symbols == 19 &&
                      has(r.problem, "prog.sld' rejected"),
                  r.problem);
        }
        {   // Z88-25: the user's source map is kept; prog.map's source lines are not loaded, its symbols are
            const fs::path d = z / "d25";
            z88_copy("v2.4/sccz80-debug", d);
            const fs::path plain = d / "mine.sld";
            write_text(plain, program_sld(p80, p90));
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            m.dbg->load_source_map(plain.string(), false);
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            });
            check("SRCDBG-Z88-25", "with the user's source map kept, prog.map's source lines are not loaded "
                                   "(and the log says so) but its symbols still attach (D10)",
                  r.sources == -1 && r.symbols == 19 && m.dbg->lookup_name("_main") == 0x8C92 &&
                      m.dbg->source_map().loaded_file() == plain.string() &&
                      has(log, "Kept the loaded source map '" + plain.string() +
                                   "'; not loading the source lines of '" + (d / "prog.map").string() + "'"),
                  "sources=" + std::to_string(r.sources) + " symbols=" + std::to_string(r.symbols) +
                      " file=" + m.dbg->source_map().loaded_file() + "\n" + log);
        }
        {   // Z88-29: a hand z88dk map leaves an earlier hand SLD's labels alone
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d1 / "prog.nex");
            const auto first = m.dbg->load_source_map(JNEXT_SJASMEQU_DIR "/equlabels.sld", true);
            const auto second = m.dbg->load_source_map(map1, true);
            check("SRCDBG-Z88-29", "a z88dk map loaded by hand fills the source half only: the labels of "
                                   "the hand SLD loaded before it stay, and it reports none of its own",
                  first.labels == 12 && second.count == 46 && second.labels == 0 &&
                      m.dbg->lookup_name("banked") == 0xC000 && !m.dbg->lookup_name("_main"),
                  "first=" + std::to_string(first.labels) + " second=" + std::to_string(second.labels));
        }
        {   // Z88-30: a BOM'd SLD is an SLD, and it loads
            // History: GH #344 review F5 made the BOM route to the SLD loader, which then refused
            // it ("missing SLD.data.version 1 header"); owner decision 2026-11-01 (GH #427): the
            // BOM is skipped and the file loads exactly as without it.
            const fs::path d = z / "d30";
            fs::create_directories(d);
            write_text(d / "bom.sld", std::string("\xEF\xBB\xBF") + program_sld(p80, p90));
            write_text(d / "plain.sld", program_sld(p80, p90));
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto out = m.dbg->load_source_map((d / "bom.sld").string(), false);
            Machine mp(false, MachineType::ZXN_ISSUE2);
            const auto plain = mp.dbg->load_source_map((d / "plain.sld").string(), false);
            check("SRCDBG-Z88-30", "a UTF-8 BOM before an SLD's header is skipped: the SLD loads exactly "
                                   "as the same file without it (count, error, format)",
                  out.error.empty() && out.count > 0 && out.count == plain.count &&
                      m.dbg->source_map().size() == mp.dbg->source_map().size() &&
                      m.dbg->source_map().format() == mp.dbg->source_map().format(),
                  out.error + " count=" + std::to_string(out.count) + "/" + std::to_string(plain.count));
        }
        {   // Z88-33: a BOM'd SLD carries its labels too
            const fs::path d = z / "d33";
            fs::create_directories(d);
            std::ifstream in(JNEXT_SJASMEQU_DIR "/equlabels.sld", std::ios::binary);
            const std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            write_text(d / "bom.sld", std::string("\xEF\xBB\xBF") + body);
            Machine m(false, MachineType::ZXN_ISSUE2);
            Machine mp(false, MachineType::ZXN_ISSUE2);
            const auto out = m.dbg->load_source_map((d / "bom.sld").string(), true);
            const auto plain = mp.dbg->load_source_map(JNEXT_SJASMEQU_DIR "/equlabels.sld", true);
            check("SRCDBG-Z88-33", "a BOM'd SLD loads the same labels and source lines as the plain one",
                  out.error.empty() && plain.labels == 12 && out.labels == plain.labels &&
                      out.count == plain.count && m.dbg->lookup_name("banked") == 0xC000 &&
                      m.dbg->lookup_name("banked") == mp.dbg->lookup_name("banked"),
                  out.error + " labels=" + std::to_string(out.labels));
        }
        {   // Z88-34: only a BOM at the very start is skipped
            const fs::path d = z / "d34";
            fs::create_directories(d);
            const std::string bom = "\xEF\xBB\xBF";
            write_text(d / "twice.sld", bom + bom + program_sld(p80, p90));
            write_text(d / "line2.sld", "\n" + bom + program_sld(p80, p90));
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto twice = m.dbg->load_source_map((d / "twice.sld").string(), false);
            const auto line2 = m.dbg->load_source_map((d / "line2.sld").string(), false);
            check("SRCDBG-Z88-34", "a second BOM, or a BOM after a leading blank line, is not skipped: the "
                                   "file is not an SLD (its first line does not start the header), is read "
                                   "as a z88dk map and refused",
                  twice.count == -1 && has(twice.error, "no source lines") && line2.count == -1 &&
                      has(line2.error, "no source lines") &&
                      m.dbg->source_map().size() == 0,
                  twice.error + " | " + line2.error);
        }
        {   // Z88-35: a BOM'd z88dk map still goes to the z88dk adapter and loads as without it
            const fs::path d = z / "d35";
            z88_copy("v2.4/sccz80-debug", d);
            std::ifstream in(d / "prog.map", std::ios::binary);
            const std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            write_text(d / "bom.map", std::string("\xEF\xBB\xBF") + body);
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const auto out = m.dbg->load_source_map((d / "bom.map").string(), true);
            check("SRCDBG-Z88-35", "a BOM'd z88dk map is routed to the z88dk adapter and loads its 46 source "
                                   "lines as without the BOM",
                  out.error.empty() && out.count == 46 && m.dbg->source_map().format() == "z88dk" &&
                      line_of(*m.dbg, 0x8CB8) == "main.c:19",
                  out.error + " count=" + std::to_string(out.count));
        }
        {   // Z88-31: the --source-map helper's line for an SLD
            const std::string sha = "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a";
            const fs::path d = z / "d31";
            fs::create_directories(d);
            write_text(d / "hash.sld", std::string("|SLD.data.version|1\n||program.name:x\n||program.sha256:") +
                                           sha + "\n||program.org:32768\n||program.size:4\n" +
                                           "main.bas|10||0" + kNextDevice + "main.bas|10||0|" +
                                           std::to_string(p80) + "|32768|T|\n");
            write_text(d / "plain.sld", program_sld(p80, p90));
            Machine m;
            for (unsigned i = 0; i < 4; ++i) m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), uint8_t(i + 1));
            const auto with = emulator_attach_cli_source_map(*m.dbg, (d / "hash.sld").string());
            const auto without = emulator_attach_cli_source_map(*m.dbg, (d / "plain.sld").string());
            check("SRCDBG-Z88-31", "the helper's line for an SLD ends \" (program identity verified)\" when "
                                   "its hash matched and has no suffix when it carries none",
                  with.ok && with.line == "--source-map '" + (d / "hash.sld").string() +
                                              "': 1 source lines (program identity verified)" &&
                      without.ok && without.line == "--source-map '" + (d / "plain.sld").string() +
                                                        "': 6 source lines",
                  with.line + " | " + without.line);
        }
        {   // Z88-27: the one table of program formats
            bool later = true, not_later = true, map_yes = true, map_no = true;
            for (const char* p : {"a.tap", "a.TZX", "a.Wav", "dir/a.tap"})
                later = later && program_loads_later(p);
            for (const char* p : {"a.nex", "a.sna", "a.z80", "a.szx", "a.rzx", "a.jns", "a", "a.tap.bak"})
                not_later = not_later && !program_loads_later(p);
            for (const char* p : {"a.nex", "a.NEX", "a.sna", "a.tap", "a.TAP"})
                map_yes = map_yes && program_has_z88dk_map(p);
            for (const char* p : {"a.tzx", "a.wav", "a.z80", "a.szx", "a.rzx", "a", "a.map"})
                map_no = map_no && !program_has_z88dk_map(p);
            check("SRCDBG-Z88-27", "program_loads_later() is the tape formats (.tap, .tzx, .wav, any case) "
                                   "and program_has_z88dk_map() is .nex, .sna and .tap",
                  later && not_later && map_yes && map_no,
                  std::string(later ? "" : "later ") + (not_later ? "" : "not_later ") +
                      (map_yes ? "" : "map_yes ") + (map_no ? "" : "map_no"));
        }
        {   // Z88-32: Z88-25's setup with prog.map's binary another build's
            const fs::path d = z / "d32";
            z88_copy("v2.4/sccz80-debug", d);
            std::string bin;
            {
                std::ifstream in(d / "prog_CODE.bin", std::ios::binary);
                bin.assign(std::istreambuf_iterator<char>(in), {});
            }
            bin[0x0CB5] = static_cast<char>(bin[0x0CB5] ^ 0xFF);
            write_text(d / "prog_CODE.bin", bin);
            const fs::path plain = d / "mine.sld";
            write_text(plain, program_sld(p80, p90));
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            m.dbg->load_source_map(plain.string(), false);
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            });
            check("SRCDBG-Z88-32", "with the user's source map kept, a prog.map whose binaries are another "
                                   "build's is still rejected: no symbols, the user's map kept, the "
                                   "rejection logged and reported",
                  r.symbols == -1 && r.sources == -1 && !m.dbg->lookup_name("_main") &&
                      m.dbg->source_map().loaded_file() == plain.string() &&
                      has(log, "Rejected z88dk source map") && has(r.problem, "prog.map' rejected"),
                  "symbols=" + std::to_string(r.symbols) + " problem=" + r.problem);
        }
        {   // Z88-26: a rewind across the load drops the z88dk map and brings it back
            const fs::path d = z / "d26";
            z88_copy("v2.4/sccz80-debug", d);
            // The program's bytes are written into page 4 (where the NEX loader puts them), so the
            // identity checks without a load that would rebuild the machine under the rewind ring.
            std::vector<uint8_t> code;
            {
                std::ifstream in(d / "prog_CODE.bin", std::ios::binary);
                code.assign(std::istreambuf_iterator<char>(in), {});
            }
            Machine m(/*rewind=*/true, MachineType::ZXN_ISSUE2);
            Z88Log lc;
            m.dbg->set_listener(m.a, &lc);
            const auto frames = [&](int n) {
                m.dbg->run(m.a);
                for (int i = 0; i < n; ++i) m.emu.run_frame();
                m.dbg->pause(m.a);
            };
            frames(1);
            const uint32_t before_load = m.dbg->time().frame;
            m.dbg->poke(m.a, MemSpace::page(4), 0, code.size(), code.data());
            write_text(d / "game.nex", "");
            fs::copy_file(d / "prog.map", d / "game.map");
            fs::copy_file(d / "prog_CODE.bin", d / "game_CODE.bin");
            const auto r = m.dbg->load_program_sidecars((d / "game.nex").string());
            frames(2);
            const uint32_t after_load = m.dbg->time().frame;
            const Result back = m.dbg->rewind_to_frame(m.a, before_load);
            const size_t at_before = m.dbg->source_map().size();
            const Result fwd = m.dbg->rewind_to_frame(m.a, after_load);
            check("SRCDBG-Z88-26", "rewinding to before the program was loaded drops its z88dk map and "
                                   "rewinding back restores it, each named as a z88dk map",
                  r.sources == 46 && back == Result::Ok && fwd == Result::Ok && at_before == 0 &&
                      m.dbg->source_map().size() == 46 &&
                      lc.has("Dropped the program's z88dk source map: this point of the history is "
                             "before the program was loaded") &&
                      lc.has("Restored the program's z88dk source map of this point in the history"),
                  "sources=" + std::to_string(r.sources) + " back=" + std::to_string(int(back)) +
                      " fwd=" + std::to_string(int(fwd)) + " before=" + std::to_string(at_before) +
                      " after=" + std::to_string(m.dbg->source_map().size()));
        }
        {   // Z88-36 (GH #428 x epic): the drop a rewind causes is a change the panels must see
            const fs::path d = z / "d36";
            z88_copy("v2.4/sccz80-debug", d);
            std::vector<uint8_t> code;
            {
                std::ifstream in(d / "prog_CODE.bin", std::ios::binary);
                code.assign(std::istreambuf_iterator<char>(in), {});
            }
            Machine m(/*rewind=*/true, MachineType::ZXN_ISSUE2);
            const auto frames = [&](int n) {
                m.dbg->run(m.a);
                for (int i = 0; i < n; ++i) m.emu.run_frame();
                m.dbg->pause(m.a);
            };
            frames(1);
            const uint32_t before_load = m.dbg->time().frame;
            m.dbg->poke(m.a, MemSpace::page(4), 0, code.size(), code.data());
            write_text(d / "game.nex", "");
            fs::copy_file(d / "prog.map", d / "game.map");
            fs::copy_file(d / "prog_CODE.bin", d / "game_CODE.bin");
            (void)m.dbg->load_program_sidecars((d / "game.nex").string());
            frames(2);
            const Result back = m.dbg->rewind_to_frame(m.a, before_load);
            // No reader has run since the rewind: the map is dropped by the first one (recheck).
            const uint64_t rev0 = m.dbg->state_revision();
            const size_t at_before = m.dbg->source_map().size();
            const uint64_t rev1 = m.dbg->state_revision();
            check("SRCDBG-Z88-36", "the state revision advances when the first reader after a rewind drops "
                                   "the program's z88dk map (a paused panel redraws on the revision, not the clock)",
                  back == Result::Ok && at_before == 0 && rev1 > rev0,
                  "back=" + std::to_string(int(back)) + " size=" + std::to_string(at_before) +
                      " rev " + std::to_string(rev0) + " -> " + std::to_string(rev1));
        }
        // ── GH #433: the C variable store has the lifetime of the source map it came from ──
        // Oracle: the store is part of SourceMap, so every rule of 7.6 (design 6.8) applies to it; the
        // program is master/sccz80-cvars (functions sum, main, other; s_dup of vars_c at P4:$909F).
        const fs::path vz = z / "var";
        const auto var_dir = [&](const char* name, const char* fixture = "master/sccz80-cvars") {
            const fs::path d = vz / name;
            z88_copy(fixture, d);
            return d;
        };
        const auto has_fn = [](const jnext::dbg::VariableStore* s, const char* name) {
            for (const auto& f : s->functions())
                if (f.name == name) return true;
            return false;
        };
        const auto s_dup_at = [](const jnext::dbg::VariableStore* s, uint16_t addr) {
            for (const auto& v : s->variables())
                if (v.record.var.name == "s_dup" && v.module == "vars_c")
                    return v.storage.resolved && v.storage.at.space == MemSpace::page(4) &&
                           v.storage.at.address == addr;
            return false;
        };
        {   // SRCDBG-VAR-01
            const fs::path d = var_dir("v1");
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            });
            const auto vs = m.dbg->variables();
            const std::string map = (d / "prog.map").string();
            check("SRCDBG-VAR-01", "prog.map beside a .nex: variables() is the source map's own store, with the "
                                   "functions sum, main and other; the sidecar log says what it said before",
                  vs && vs == m.dbg->source_map().variables() && has_fn(vs.get(), "sum") &&
                      has_fn(vs.get(), "main") && has_fn(vs.get(), "other") && s_dup_at(vs.get(), 0x909F) &&
                      has(log, "Loaded " + std::to_string(r.symbols) + " z88dk symbols from '" + map + "'") &&
                      has(log, "Loaded " + std::to_string(r.sources) + " z88dk source lines from '" + map +
                                   "' (program identity verified)") &&
                      log.find("ariable") == std::string::npos,
                  log);
        }
        {   // SRCDBG-VAR-02
            const fs::path d = var_dir("v2");
            const fs::path other = vz / "v2other";
            fs::create_directories(other);
            write_text(other / "next.nex", "");
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            m.dbg->load_program_sidecars((d / "prog.nex").string());
            const bool had = m.dbg->variables() != nullptr;
            m.dbg->load_program_sidecars((other / "next.nex").string());
            check("SRCDBG-VAR-02", "the next program's load, with no sidecar of its own, drops the source map and "
                                   "its store: variables() is null",
                  had && m.dbg->source_map().empty() && !m.dbg->variables());
        }
        {   // SRCDBG-VAR-03
            const fs::path d = var_dir("v3");
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const auto out = m.dbg->load_source_map((d / "prog.map").string(), false);
            const auto vs = m.dbg->variables();
            const bool by_user = m.dbg->source_map_status().by_user;
            const Result cleared = m.dbg->clear_source_map();
            check("SRCDBG-VAR-03", "Load Source Map by hand carries the store (the map is the user's); "
                                   "clear_source_map() drops both",
                  out.count > 0 && vs && by_user && has_fn(vs.get(), "main") && cleared == Result::Ok &&
                      !m.dbg->variables());
        }
        {   // SRCDBG-VAR-04
            const fs::path d = var_dir("v4");
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            m.dbg->load_source_map((d / "prog.map").string(), false);
            const bool had = m.dbg->variables() != nullptr;
            // As SRCLIFE-24: a soft reset keeps the user's map while its bytes are intact; a byte
            // that changes while the machine runs is found at the next pause, and the map is Stale.
            const Result rs = m.dbg->reset(m.a, ResetKind::Soft);
            m.dbg->run(m.a);
            const uint8_t was = m.emu.mmu().peek(0x8CB5);
            m.emu.mmu().write(0x8CB5, static_cast<uint8_t>(was ^ 0xFF));
            for (int i = 0; i < 5; ++i) m.emu.run_frame();
            m.dbg->pause(m.a);
            const auto st = m.dbg->source_map_status();
            const bool stale_null = rs == Result::Ok && m.emu.mmu().peek(0x8CB5) != was &&
                                    st.state == Debugger::SourceMapStatus::State::Stale && st.by_user &&
                                    !m.dbg->variables();
            const auto again = m.dbg->load_source_map((d / "prog.map").string(), true);
            check("SRCDBG-VAR-04", "the user's map made Stale when its program's bytes change under a soft reset (found at the "
                                   "next pause): variables() is null while it is Stale, and loading it again by hand brings it back",
                  had && stale_null && again.count > 0 && m.dbg->variables() != nullptr &&
                      m.dbg->source_map_status().state == Debugger::SourceMapStatus::State::Attached);
        }
        // SRCDBG-VAR-05 / -06: a rewind across the load, as SRCDBG-Z88-26 / -36
        const auto rewound = [&](const char* name, bool read_first, std::shared_ptr<const jnext::dbg::VariableStore>& before,
                                 std::shared_ptr<const jnext::dbg::VariableStore>& after_back,
                                 std::shared_ptr<const jnext::dbg::VariableStore>& after_fwd,
                                 uint64_t& rev0, uint64_t& rev1) {
            const fs::path d = var_dir(name);
            const std::vector<uint8_t> code = read_all(d / "prog_CODE.bin");
            Machine m(/*rewind=*/true, MachineType::ZXN_ISSUE2);
            const auto frames = [&](int n) {
                m.dbg->run(m.a);
                for (int i = 0; i < n; ++i) m.emu.run_frame();
                m.dbg->pause(m.a);
            };
            frames(1);
            const uint32_t before_load = m.dbg->time().frame;
            m.dbg->poke(m.a, MemSpace::page(4), 0, code.size(), code.data());
            write_text(d / "game.nex", "");
            fs::copy_file(d / "prog.map", d / "game.map");
            fs::copy_file(d / "prog_CODE.bin", d / "game_CODE.bin");
            m.dbg->load_program_sidecars((d / "game.nex").string());
            before = m.dbg->variables();
            frames(2);
            const uint32_t after_load = m.dbg->time().frame;
            const Result back = m.dbg->rewind_to_frame(m.a, before_load);
            rev0 = m.dbg->state_revision();
            if (read_first) after_back = m.dbg->variables();
            rev1 = m.dbg->state_revision();
            if (!read_first) after_back = m.dbg->variables();
            const Result fwd = m.dbg->rewind_to_frame(m.a, after_load);
            after_fwd = m.dbg->variables();
            return back == Result::Ok && fwd == Result::Ok;
        };
        {   // SRCDBG-VAR-05
            std::shared_ptr<const jnext::dbg::VariableStore> before, back, fwd;
            uint64_t r0 = 0, r1 = 0;
            const bool ok = rewound("v5", false, before, back, fwd, r0, r1);
            check("SRCDBG-VAR-05", "a rewind to before the load drops the store (null), and rewinding back restores "
                                   "the very same store object (pointer equal), shared with the cache, not copied",
                  ok && before && !back && fwd == before);
        }
        {   // SRCDBG-VAR-06
            std::shared_ptr<const jnext::dbg::VariableStore> before, back, fwd;
            uint64_t r0 = 0, r1 = 0;
            const bool ok = rewound("v6", true, before, back, fwd, r0, r1);
            check("SRCDBG-VAR-06", "after a rewind to before the load, variables() is the FIRST reader: it runs "
                                   "recheck(), returns null, and the state revision advances",
                  ok && before && !back && r1 > r0);
        }
        {   // SRCDBG-VAR-07
            const fs::path d = vz / "v7";
            const std::string tap = tape_copy("v2.4/sccz80-tape", d);
            Machine m;
            park(m);
            const auto r = m.dbg->load_program_sidecars(tap);
            const auto vs = m.dbg->variables();
            bool counter = false;
            for (const auto& v : vs ? vs->variables() : std::vector<jnext::dbg::CVariable>{})
                counter = counter || (v.record.var.name == "counter" && v.storage.symbol == "_counter");
            const bool pending = m.dbg->source_map_status().pending;
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", 0xCC4, 0x8000);
            m.set_pc(0x8000);
            run_frames(m, 1);
            check("SRCDBG-VAR-07", "a tape program's map is attached at once, pending, and its store with it "
                                   "(counter is joined); a wrong byte judged at the first execution drops the map "
                                   "and the store",
                  r.sources == 46 && r.sources_pending && pending && vs && counter && m.dbg->source_map().empty() &&
                      !m.dbg->variables());
        }
        {   // SRCDBG-VAR-08
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto r = m.dbg->load_source_map((kZ88Fixtures / "master/sccz80-dot/prog.map").string(), false, false,
                                                  /*dot_only=*/true);
            const auto vs = m.dbg->variables();
            bool total = false;
            for (const auto& v : vs ? vs->variables() : std::vector<jnext::dbg::CVariable>{})
                total = total || (v.record.var.name == "total" && v.storage.resolved &&
                                  v.storage.at.space == MemSpace::divmmc_ram(2) && v.storage.at.address == 0x3253);
            check("SRCDBG-VAR-08", "a dot command's map loaded by hand (pending, dot): its store has total at "
                                   "DivMmcRam{2}:$3253 (GH #445: z88dk v2.4-2883 moved it)",
                  r.count > 0 && r.pending && vs && total);
        }
        {   // SRCDBG-VAR-09
            const fs::path d = var_dir("v9");
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const auto out = m.dbg->load_map((d / "prog.map").string(), jnext::dbg::MapFormat::Z88dk);
            check("SRCDBG-VAR-09", "Load MAP of a z88dk map loads the symbols and no source half, so no store (OD-8)",
                  out && out.value > 0 && m.dbg->lookup_name("_main").has_value() && m.dbg->source_map().empty() &&
                      !m.dbg->variables());
        }
        {   // SRCDBG-VAR-10
            const fs::path d = var_dir("v10");
            write_text(d / "prog.sld", program_sld(p80, p90));
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const auto r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            check("SRCDBG-VAR-10", "an SLD sidecar beside prog.map wins the source half, and an SLD carries no store",
                  r.sources == 6 && m.dbg->source_map().format() == "SLD" && !m.dbg->variables());
        }
        {   // SRCDBG-VAR-11
            const fs::path d = var_dir("v11");
            const fs::path plain = d / "mine.sld";
            write_text(plain, program_sld(p80, p90));
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            m.dbg->load_program_sidecars((d / "prog.nex").string());
            const bool had = m.dbg->variables() != nullptr;
            m.dbg->load_source_map(plain.string(), false);
            check("SRCDBG-VAR-11", "a hand SLD loaded over a z88dk map replaces it, and the store goes with the map",
                  had && m.dbg->source_map().format() == "SLD" && !m.dbg->variables());
        }
        {   // SRCDBG-VAR-12
            const fs::path d = var_dir("v12");
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            m.dbg->load_program_sidecars((d / "prog.nex").string());
            const auto before = m.dbg->variables();
            const Result lab = m.dbg->set_user_label(MemSpace::page(4), 0x909F, "mine");
            const auto k = m.dbg->symbols().find_name("mine");
            const auto after = m.dbg->variables();
            check("SRCDBG-VAR-12", "a user label at s_dup's address under another name shows in the symbols, and the "
                                   "store is the same object, still joining vars.c's s_dup to _s_dup at P4:$909F",
                  lab == Result::Ok && k && k->addr == 0x909F && m.dbg->lookup(0x909F) == std::string("mine") &&
                      before && after == before && s_dup_at(after.get(), 0x909F));
        }

        // ── GH #434 (E353-04) stage 1: the call-stack tracker policy and the C frames ──
        // Oracle: the C source's call structure (vars.c, other.c, bk.c, bkmain.c, cross.c), the fixtures'
        // maps and listings (addresses, prologues: `call l_debug_push_frame` / `push ix; ld ix,0; add ix,sp`),
        // z88dk's library sources (l_debug_push_frame stores E-2 in __debug_framepointer), and measurements of
        // the running machine that do not use the code under test (SP at the entry instruction, IX, the variable).
        using FrameBase = Debugger::CFrame::Base;
        const auto peek16 = [](Machine& m, uint16_t a) {
            return static_cast<unsigned>(m.emu.mmu().peek(a) | (m.emu.mmu().peek(static_cast<uint16_t>(a + 1)) << 8));
        };
        const auto cv_load = [&](Machine& m, const char* fixture, const char* name) {
            const fs::path d = var_dir(name, fixture);
            z88_load(m, d / "prog.nex");
            m.dbg->load_program_sidecars((d / "prog.nex").string());
        };
        const auto cv_run_to = [&](Machine& m, uint16_t addr) {
            const auto id = m.dbg->subscribe(m.a, execute_stop(addr)).value;
            m.dbg->run(m.a);
            for (int i = 0; i < 100 && !m.dbg->state().paused; ++i) m.emu.run_frame();
            m.dbg->unsubscribe(m.a, id);
            return m.dbg->state().paused && m.pc() == addr;
        };
        const auto fn_name = [&](const Machine& m, const Debugger::CFrame& f) {
            const auto vs = m.dbg->variables();
            return vs && f.function >= 0 ? vs->functions()[static_cast<size_t>(f.function)].name : std::string("-");
        };
        const auto line_no = [](const Debugger::CFrame& f) { return f.line ? f.line->line : 0; };
        {   // SRCDBG-CVAL-01
            Bare m(MachineType::ZXN_ISSUE2);
            const fs::path d = var_dir("c01");
            emulator_apply_load(m.emu, (d / "prog.nex").string(), false);
            m.dbg->load_program_sidecars((d / "prog.nex").string());
            const bool no_client = !m.dbg->call_stack_enabled();
            jnext::dbg::ClientInfo ci;
            ci.name = "t";
            ci.kind = jnext::dbg::ClientKind::Test;
            const ClientId c = m.dbg->attach(ci).value;
            const bool attached = m.dbg->call_stack_enabled();
            m.dbg->detach(c);
            check("SRCDBG-CVAL-01", "no client and a C variable store: call-stack tracking is off; a (non-observer) client attached: on; "
                                    "detached: off again",
                  m.dbg->variables() && no_client && attached && !m.dbg->call_stack_enabled());
        }
        {   // SRCDBG-CVAL-02
            Bare m(MachineType::ZXN_ISSUE2);
            jnext::dbg::ClientInfo ci;
            ci.name = "t";
            ci.kind = jnext::dbg::ClientKind::Test;
            m.dbg->attach(ci);
            const std::string sld = (z / "c02.sld").string();
            write_text(sld, "|SLD.data.version|1\nmain.bas|10||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
                            "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\nmain.bas|10||0|0|32768|T|\n");
            m.dbg->load_source_map(sld, true);
            const bool sld_off = !m.dbg->call_stack_enabled();
            const fs::path d = z / "c02so2";
            z88_copy_lis("v2.4/sdcc-SO2", d);
            Debugger::SidecarLoad r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            const auto vs = m.dbg->variables();
            check("SRCDBG-CVAL-02", "a client is attached: an SLD map (no store) leaves tracking off, and so does v2.4/sdcc-SO2's map "
                                    "(lines from listings, an empty store)",
                  sld_off && (!vs || (vs->functions().empty() && vs->variables().empty())) && !m.dbg->call_stack_enabled());
            (void)r;
        }
        {   // SRCDBG-CVAL-03
            Bare m(MachineType::ZXN_ISSUE2);
            const fs::path d = var_dir("c03");
            emulator_apply_load(m.emu, (d / "prog.nex").string(), false);
            m.dbg->load_program_sidecars((d / "prog.nex").string());
            jnext::dbg::ClientInfo ci;
            ci.name = "emulator window";
            ci.kind = jnext::dbg::ClientKind::Gui;
            ci.observer = true;
            m.dbg->attach(ci);
            check("SRCDBG-CVAL-03", "only an observer client is attached (the Qt emulator window), with a C variable store: tracking is off",
                  m.dbg->variables() && !m.dbg->call_stack_enabled());
        }
        {   // SRCDBG-CVAL-04
            Bare m;
            jnext::dbg::ClientInfo ci;
            ci.name = "A";
            ci.kind = jnext::dbg::ClientKind::Test;
            const ClientId a = m.dbg->attach(ci).value;
            ci.name = "B";
            const ClientId b = m.dbg->attach(ci).value;
            m.dbg->set_call_stack_enabled(a, true);
            m.dbg->set_call_stack_enabled(b, true);
            m.dbg->set_call_stack_enabled(a, false);
            const bool b_keeps = m.dbg->call_stack_enabled();
            // a tracked frame survives A's switching off (nothing cleared the tracker)
            m.emu.mmu().write(0x6100, 0xCD);
            m.emu.mmu().write(0x6101, 0x00);
            m.emu.mmu().write(0x6102, 0x62);
            m.emu.mmu().write(0x6200, 0x18);
            m.emu.mmu().write(0x6201, 0xFE);
            m.start(0x6100);
            m.dbg->pause(a);
            m.emu.execute_single_instruction();
            const size_t frames_before = m.dbg->call_stack().size();
            m.dbg->set_call_stack_enabled(a, true);
            m.dbg->set_call_stack_enabled(a, false);
            const bool kept = m.dbg->call_stack().size() == frames_before && frames_before == 1;
            m.dbg->set_call_stack_enabled(b, false);
            const bool off = !m.dbg->call_stack_enabled();
            const ClientId c = m.dbg->attach(ci).value;
            m.dbg->set_call_stack_enabled(c, true);
            const bool on = m.dbg->call_stack_enabled();
            m.dbg->detach(c);
            check("SRCDBG-CVAL-04", "per-client requests (GH #434 F-B): A on, B on, A off: still on and the frames are kept; B off: off; "
                                    "a client that asked and is detached takes its request with it",
                  b_keeps && kept && off && on && !m.dbg->call_stack_enabled());
        }
        {   // SRCDBG-CVAL-05
            const fs::path d = var_dir("c05");
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            m.dbg->set_call_stack_enabled(m.a, false);   // the auto rule alone
            m.dbg->load_source_map((d / "prog.map").string(), false);
            const bool on1 = m.dbg->call_stack_enabled();
            // As SRCLIFE-24: the user's map goes Stale when its program's bytes change while the machine runs
            m.dbg->reset(m.a, ResetKind::Soft);
            m.dbg->run(m.a);
            const uint8_t was = m.emu.mmu().peek(0x8CB5);
            m.emu.mmu().write(0x8CB5, static_cast<uint8_t>(was ^ 0xFF));
            for (int i = 0; i < 5; ++i) m.emu.run_frame();
            m.dbg->pause(m.a);
            const bool stale = m.dbg->source_map_status().state == Debugger::SourceMapStatus::State::Stale;
            const bool off_stale = !m.dbg->call_stack_enabled();
            m.dbg->load_source_map((d / "prog.map").string(), true);
            const bool on2 = m.dbg->call_stack_enabled();
            m.dbg->clear_source_map();
            const bool off_clear = !m.dbg->call_stack_enabled();
            const fs::path d2 = var_dir("c05b");
            z88_load(m, d2 / "prog.nex");   // the next program, loaded over this one
            m.dbg->load_program_sidecars((d2 / "prog.nex").string());
            const bool on3 = m.dbg->call_stack_enabled();
            check("SRCDBG-CVAL-05", "the auto rule follows the store: on with a user map, off while it is Stale, on when loaded again, off after "
                                    "clear_source_map(), on with the next program's sidecar",
                  on1 && stale && off_stale && on2 && off_clear && on3);
        }
        {   // SRCDBG-CVAL-06
            Bare m;
            jnext::dbg::ClientInfo ci;
            ci.name = "A";
            ci.kind = jnext::dbg::ClientKind::Test;
            const ClientId a = m.dbg->attach(ci).value;
            ci.name = "B";
            const ClientId b = m.dbg->attach(ci).value;
            jnext::dbg::LoopDriver d;   // the frontend's real cold boot: ~Emulator() and placement-new
            d.cold_boot = [&]() {
                emulator_frontend_cold_boot(m.emu, m.emu.config(), std::string(), ColdBootHooks{});
                return true;
            };
            m.dbg->set_loop_driver(d);
            m.dbg->set_call_stack_enabled(a, true);
            m.dbg->set_call_stack_enabled(b, true);
            m.emu.trace_log().set_enabled(true);   // direct, no request: only a rebuilt machine has it off
            const Result r1 = m.dbg->reset(a, ResetKind::Hard);
            const bool rebuilt1 = !m.emu.trace_log().enabled();
            const bool both = m.emu.call_stack().enabled();
            m.dbg->set_call_stack_enabled(a, false);
            m.emu.trace_log().set_enabled(true);
            const Result r2 = m.dbg->reset(a, ResetKind::Hard);
            const bool rebuilt2 = !m.emu.trace_log().enabled();
            const bool one = m.emu.call_stack().enabled();
            m.dbg->set_call_stack_enabled(b, false);
            m.emu.trace_log().set_enabled(true);
            const Result r3 = m.dbg->reset(a, ResetKind::Hard);
            check("SRCDBG-CVAL-06", "a real cold boot (the machine is rebuilt) keeps every client's request: both on, A off (B still on), both off",
                  r1 == Result::Ok && r2 == Result::Ok && r3 == Result::Ok && rebuilt1 && rebuilt2 && !m.emu.trace_log().enabled() &&
                      both && one && !m.emu.call_stack().enabled());
        }
        // Frame bases, tracked: the CALL that entered the frame's code recorded SP = E, and fp = E - 2
        // for every compiler (z88dk l_debug_push_frame.asm stores SP-2; `push ix; ld ix,0; add ix,sp` is the same).
        {   // SRCDBG-CVAL-07
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sccz80-cvars", "c07");
            const bool s1 = cv_run_to(m, 0x8CB9);
            const uint16_t E = m.dbg->registers().SP;
            const bool s2 = cv_run_to(m, 0x8D3E);
            const auto fr = m.dbg->c_frames();
            check("SRCDBG-CVAL-07", "master sccz80-cvars at vars.c:41 of the first call: frame 0 is sum (Tracked), fp == E - 2 == "
                                    "__debug_framepointer ($906A); frame 1 is main at vars.c:56 (Tracked)",
                  s1 && s2 && fr.size() >= 2 && fn_name(m, fr[0]) == "sum" && fr[0].how == FrameBase::Tracked &&
                      line_no(fr[0]) == 41 && fr[0].fp == static_cast<uint16_t>(E - 2) && fr[0].fp == peek16(m, 0x906A) &&
                      fn_name(m, fr[1]) == "main" && fr[1].how == FrameBase::Tracked && line_no(fr[1]) == 56);
        }
        {   // SRCDBG-CVAL-08
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sdcc-cvars", "c08");
            const bool s1 = cv_run_to(m, 0x8C48);
            const uint16_t E = m.dbg->registers().SP;
            const bool s2 = cv_run_to(m, 0x8C97);
            const auto fr = m.dbg->c_frames();
            check("SRCDBG-CVAL-08", "master sdcc-cvars at vars.c:41: frame 0 is sum (Tracked), fp == E - 2 == IX; frame 1 is main",
                  s1 && s2 && fr.size() >= 2 && fn_name(m, fr[0]) == "sum" && fr[0].how == FrameBase::Tracked &&
                      fr[0].fp == static_cast<uint16_t>(E - 2) && fr[0].fp == m.dbg->registers().IX &&
                      fn_name(m, fr[1]) == "main" && fr[1].how == FrameBase::Tracked);
        }
        {   // SRCDBG-CVAL-09
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/80cc-cvars", "c09");
            const bool s = cv_run_to(m, 0x8DC6);   // other.c:9
            const auto fr = m.dbg->c_frames();
            check("SRCDBG-CVAL-09", "master 80cc-cvars at other.c:9 called from main: fp_0 == __debug_framepointer ($8FE5, other keeps a "
                                    "push-frame frame), fp_1 == IX (main keeps an IX frame); both Tracked",
                  s && fr.size() >= 2 && fn_name(m, fr[0]) == "other" && fr[0].how == FrameBase::Tracked &&
                      fr[0].fp == peek16(m, 0x8FE5) && fn_name(m, fr[1]) == "main" && fr[1].how == FrameBase::Tracked &&
                      fr[1].fp == m.dbg->registers().IX);
        }
        {   // SRCDBG-CVAL-10
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sccz80-cvars-banked", "c10");
            // This machine has no ROM (RST 38 at $0038 reads $FF), so an interrupt runs away: the program's
            // `ei` (banked_call re-enables them) is undone at bk_sum's entry; the call chain under test is the program's own.
            cv_run_to(m, 0xC005);
            {
                Z80Registers r = m.emu.cpu().get_registers();
                r.IFF1 = r.IFF2 = 0;
                m.emu.cpu().set_registers(r);
            }
            const bool s = cv_run_to(m, 0xC04C);   // bk.c:10, Page{20}
            const auto fr = m.dbg->c_frames();
            const auto& cs = m.dbg->call_stack();
            check("SRCDBG-CVAL-10", "master sccz80-cvars-banked in bk_sum (bk.c:10, Page{20}): frame 0 bk_sum, frame 1 asm (banked_call, "
                                    "no C function), frame 2 main at bkmain.c:7; frame 0's function is not the one at the newest "
                                    "frame's target (l_jphl)",
                  s && fr.size() >= 3 && fn_name(m, fr[0]) == "bk_sum" && fr[0].pc.space == MemSpace::page(20) &&
                      fr[1].function == -1 && fn_name(m, fr[2]) == "main" && line_no(fr[2]) == 7 && !cs.empty() &&
                      cs.back().target_pc != 0xC04C && fr[0].how == FrameBase::Tracked);
        }
        // The fallback: no tracker frames (a mid-run attach), so frame 0 comes from the entry bytes and older
        // frames from the saved frame pointers.
        {   // SRCDBG-CVAL-11
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sccz80-cvars", "c11");
            cv_run_to(m, 0x8D3E);
            const auto tracked = m.dbg->c_frames();
            m.emu.call_stack().clear();
            const auto fr = m.dbg->c_frames();
            check("SRCDBG-CVAL-11", "sccz80, vars.c:41 with the tracker emptied: sum is Prologue and main Chain, with the same fp as "
                                    "the tracked run",
                  tracked.size() >= 2 && fr.size() >= 2 && fn_name(m, fr[0]) == "sum" && fr[0].how == FrameBase::Prologue &&
                      fr[0].fp == tracked[0].fp && fn_name(m, fr[1]) == "main" && fr[1].how == FrameBase::Chain &&
                      fr[1].fp == tracked[1].fp && fr[1].floor == tracked[1].floor && line_no(fr[1]) == line_no(tracked[1]) &&
                      line_no(fr[1]) == 56);
        }
        {   // SRCDBG-CVAL-12
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sdcc-cvars", "c12");
            cv_run_to(m, 0x8C97);
            const auto tracked = m.dbg->c_frames();
            m.emu.call_stack().clear();
            const auto fr = m.dbg->c_frames();
            check("SRCDBG-CVAL-12", "zsdcc, vars.c:41 with the tracker emptied: sum is Prologue (IX) and main Chain, with the same fp as "
                                    "the tracked run",
                  tracked.size() >= 2 && fr.size() >= 2 && fn_name(m, fr[0]) == "sum" && fr[0].how == FrameBase::Prologue &&
                      fr[0].fp == tracked[0].fp && fn_name(m, fr[1]) == "main" && fr[1].how == FrameBase::Chain &&
                      fr[1].fp == tracked[1].fp && fr[1].floor == tracked[1].floor && line_no(fr[1]) == line_no(tracked[1]) &&
                      line_no(fr[1]) == 56);
        }
        {   // SRCDBG-CVAL-13
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/80cc-cvars", "c13");
            cv_run_to(m, 0x8DC6);
            m.emu.call_stack().clear();
            const auto fr = m.dbg->c_frames();
            check("SRCDBG-CVAL-13", "80cc in other with the tracker emptied: frame 0 Prologue (push-frame); frame 1 main is Unknown with the "
                                    "style-change note, and nothing further is listed",
                  fr.size() == 2 && fn_name(m, fr[0]) == "other" && fr[0].how == FrameBase::Prologue &&
                      fn_name(m, fr[1]) == "main" && fr[1].how == FrameBase::Unknown &&
                      fr[1].note.find("keeps its frame differently") != std::string::npos);
        }
        {   // SRCDBG-CVAL-14
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sccz80-cvars", "c14a");
            cv_run_to(m, 0x8CB9);
            m.emu.call_stack().clear();
            const auto at_entry = m.dbg->c_frames();
            const uint16_t sp_entry = m.dbg->registers().SP;
            const bool s = cv_run_to(m, 0x8D53);   // the ret after `call l_debug_pop_frame`
            m.emu.call_stack().clear();
            const auto at_ret = m.dbg->c_frames();
            const uint16_t sp_ret = m.dbg->registers().SP;
            Machine z(false, MachineType::ZXN_ISSUE2);
            cv_load(z, "master/sdcc-cvars", "c14b");
            cv_run_to(z, 0x8C48);
            z.emu.call_stack().clear();
            const auto zentry = z.dbg->c_frames();
            const uint16_t zsp = z.dbg->registers().SP;
            const bool zs = cv_run_to(z, 0x8C9E);   // the ret after `pop ix`
            z.emu.call_stack().clear();
            const auto zret = z.dbg->c_frames();
            const uint16_t zsp2 = z.dbg->registers().SP;
            check("SRCDBG-CVAL-14", "prologue and epilogue windows with no frames: at sum's entry instruction fp == SP - 2 (sccz80, zsdcc); "
                                    "at the ret after call l_debug_pop_frame fp == SP - 2, not the variable (already the caller's); "
                                    "zsdcc at the ret after pop ix fp == SP - 2, not IX",
                  s && zs && !at_entry.empty() && !at_ret.empty() && !zentry.empty() && !zret.empty() &&
                      at_entry[0].fp == static_cast<uint16_t>(sp_entry - 2) && at_ret[0].fp == static_cast<uint16_t>(sp_ret - 2) &&
                      at_ret[0].fp != peek16(m, 0x906A) && zentry[0].fp == static_cast<uint16_t>(zsp - 2) &&
                      zret[0].fp == static_cast<uint16_t>(zsp2 - 2) && zret[0].fp != z.dbg->registers().IX);
        }
        {   // SRCDBG-CVAL-15
            auto attach_midrun = [&](const char* fixture, const char* name, bool ix, bool& ok) {
                Bare m(MachineType::ZXN_ISSUE2);
                const fs::path d = var_dir(name, fixture);
                emulator_apply_load(m.emu, (d / "prog.nex").string(), false);
                m.dbg->load_program_sidecars((d / "prog.nex").string());
                m.frames(3);   // the program is in main's while (1) loop; no client, so nothing was tracked
                jnext::dbg::ClientInfo ci;
                ci.name = "late";
                ci.kind = jnext::dbg::ClientKind::Test;
                const ClientId c = m.dbg->attach(ci).value;
                m.dbg->pause(c);
                const auto fr = m.dbg->c_frames();
                const auto vs = m.dbg->variables();
                const unsigned var = static_cast<unsigned>(m.emu.mmu().peek(0x906A) | (m.emu.mmu().peek(0x906B) << 8));
                ok = ok && !fr.empty() && vs && fr[0].function >= 0 &&
                     vs->functions()[static_cast<size_t>(fr[0].function)].name == "main" && fr[0].how == FrameBase::Prologue &&
                     fr[0].fp == (ix ? m.dbg->registers().IX : var);
            };
            bool ok = true;
            attach_midrun("master/sccz80-cvars", "c15a", false, ok);
            attach_midrun("master/sdcc-cvars", "c15b", true, ok);
            check("SRCDBG-CVAL-15", "a real mid-run attach (no tracked frame): frame 0 main is Prologue, fp == __debug_framepointer "
                                    "(sccz80, $906A) / IX (zsdcc)",
                  ok);
        }
        {   // SRCDBG-CVAL-16
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sccz80-cvars", "c16");
            cv_run_to(m, 0x8CB9);
            const uint16_t E = m.dbg->registers().SP;
            const bool s = cv_run_to(m, 0x8B64);   // l_debug_push_frame's first instruction, in sum's first call
            const auto fr = m.dbg->c_frames();
            const auto& cs = m.dbg->call_stack();
            check("SRCDBG-CVAL-16", "stopped at l_debug_push_frame's first instruction: frame 0 is asm, frame 1 is sum with floor == the "
                                    "inner call's sp_at_call + 2 (== E), above fp, so its locals are not allocated",
                  s && fr.size() >= 2 && fr[0].function == -1 && fn_name(m, fr[1]) == "sum" && !cs.empty() &&
                      fr[1].floor == static_cast<uint16_t>(cs.back().sp_at_call + 2) && fr[1].floor == E &&
                      fr[1].floor > fr[1].fp);
        }
        {   // SRCDBG-CVAL-17
            struct Cross { const char* fx; uint16_t pc; int line; const char* tag; };
            // the first line after the padding: cross.c:20 on each compiler (derive-scopes.py: $AC75, $AC87, $AC57)
            bool ok = true;
            for (const Cross& c : {Cross{"master/sccz80-cross", 0xAC75, 20, "c17a"}, Cross{"master/80cc-cross", 0xAC87, 20, "c17b"},
                                   Cross{"master/sdcc-cross", 0xAC57, 20, "c17c"}}) {
                Machine m(false, MachineType::ZXN_ISSUE2);
                cv_load(m, c.fx, c.tag);
                const bool s = cv_run_to(m, c.pc);
                const auto fr = m.dbg->c_frames();
                const auto vs = m.dbg->variables();
                const auto loc = vs && !fr.empty() ? vs->locate(fr[0].pc) : std::nullopt;
                const jnext::dbg::CScope* sc = loc ? vs->scope_at(loc->function, loc->offset) : nullptr;
                ok = ok && s && !fr.empty() && fn_name(m, fr[0]) == "cross" && m.pc() >= 0xA000 && fr[0].pc.space == MemSpace::page(5) &&
                     loc && loc->offset > 0x1000 && sc && sc->at.space == MemSpace::page(5) && sc->at.address == c.pc &&
                     sc->line == c.line && sc->known && fr[0].line && fr[0].line->line == c.line &&
                     fr.size() >= 2 && fn_name(m, fr[1]) == "main";
            }
            check("SRCDBG-CVAL-17", "a function that crosses an 8 KB slot (cross.c): stopped past $A000 in cross, frame 0 is cross (piece 2, page 5) with "
                                    "an offset across the pieces, its scope is the page-5 one of line 20, frame 1 is main; sccz80, 80cc and zsdcc",
                  ok);
        }
        {   // SRCDBG-CVAL-18
            // GH #434 review F1: a __z88dk_callee epilogue pops its arguments after the frame is restored. Oracle: the
            // fc listings (sccz80: call l_debug_pop_frame; pop bc; pop af; pop af; push bc; ret; zsdcc: pop ix; pop bc; pop af;
            // inc sp; push bc; ret; 80cc: call l_debug_pop_frame; pop bc; exx; ...; ret) and E measured at fcallee's entry.
            struct Fc { const char* fx; uint16_t entry, start, later; const char* tag; };
            bool ok = true;
            std::string seen;
            for (const Fc& c : {Fc{"master/sccz80-fc", 0x8C74, 0x8C8D, 0x8C8E, "c18a"}, Fc{"master/sdcc-fc", 0x8C4A, 0x8C60, 0x8C61, "c18b"},
                                Fc{"master/80cc-fc", 0x8C74, 0x8C8C, 0x8C8D, "c18c"}}) {
                Machine m(false, MachineType::ZXN_ISSUE2);
                cv_load(m, c.fx, c.tag);
                cv_run_to(m, c.entry);
                const uint16_t E = m.dbg->registers().SP;
                const bool s1 = cv_run_to(m, c.start);
                m.emu.call_stack().clear();
                const auto at_start = m.dbg->c_frames();
                const bool s2 = cv_run_to(m, c.later);
                m.emu.call_stack().clear();
                const auto later = m.dbg->c_frames();
                // right after the restore SP is E again: the base is E - 2; one instruction later SP has moved and the variable
                // or IX hold the caller's base, which must never be reported
                const bool good = s1 && s2 && !at_start.empty() && !later.empty() && at_start[0].how == FrameBase::Prologue &&
                                  at_start[0].fp == static_cast<uint16_t>(E - 2) &&
                                  (later[0].how == FrameBase::Unknown || later[0].fp == static_cast<uint16_t>(E - 2)) &&
                                  !(later[0].how == FrameBase::Prologue && later[0].fp != static_cast<uint16_t>(E - 2));
                ok = ok && good;
                if (!good) seen += std::string(" ") + c.fx;
            }
            // GH #434 review R2-1: a long function (cross(): 8192 bytes of padding) at the ret right after the restore
            struct Cross { const char* fx; uint16_t entry, ret; const char* tag; };
            for (const Cross& c : {Cross{"master/sccz80-cross", 0x8C6B, 0xAC80, "c18d"}, Cross{"master/sdcc-cross", 0x8C48, 0xAC5A, "c18e"},
                                   Cross{"master/80cc-cross", 0x8C78, 0xAC96, "c18f"}}) {
                Machine m(false, MachineType::ZXN_ISSUE2);
                cv_load(m, c.fx, c.tag);
                cv_run_to(m, c.entry);
                const uint16_t E = m.dbg->registers().SP;
                const bool s1 = cv_run_to(m, c.ret);
                m.emu.call_stack().clear();
                const auto fr = m.dbg->c_frames();
                const bool good = s1 && !fr.empty() && fr[0].how == FrameBase::Prologue && fr[0].fp == static_cast<uint16_t>(E - 2);
                ok = ok && good;
                if (!good) seen += std::string(" ") + c.fx;
            }
            check("SRCDBG-CVAL-18", "a __z88dk_callee epilogue with no frames: right after the frame pointer is restored fp == E - 2; one "
                                    "instruction later (arguments being popped) the base is Unknown, never the caller's, on sccz80, zsdcc and 80cc; "
                                    "and at the ret of cross() (8192 bytes of code before it) fp == E - 2",
                  ok, seen);
        }
        {   // SRCDBG-CVAL-19
            // GH #434 review F2: two frames that keep no saved frame pointer are never chained. Synthetic program: fa (nop; call fb)
            // and fb (nop; jr $), neither a push-frame nor an IX prologue, in a map of two functions.
            Bare m(MachineType::ZXN_ISSUE2);
            jnext::dbg::ClientInfo ci;
            ci.name = "t";
            ci.kind = jnext::dbg::ClientKind::Test;
            m.dbg->attach(ci);
            const auto esc = [](const std::string& t) {
                std::string o;
                for (const unsigned char c : t) {
                    if (std::isalpha(c)) {
                        o += static_cast<char>(c);
                    } else {
                        char b[8];
                        std::snprintf(b, sizeof b, "_%02x", c);
                        o += b;
                    }
                }
                return o;
            };
            const std::string map = (z / "c19.map").string();
            write_text(map,
                       "_fa = $8000 ; addr, public, , t_c, code_compiler, t.c::fa::0::0:1\n"
                       "_fb = $8010 ; addr, public, , t_c, code_compiler, t.c::fb::0::0:5\n"
                       "__C_LINE_1_t_2ec = $8000 ; addr, local, , t_c, code_compiler, t.c::fa::0::0:1\n"
                       "__C_LINE_5_t_2ec = $8010 ; addr, local, , t_c, code_compiler, t.c::fb::0::0:5\n"
                       "__code_compiler_tail = $8020 ; const, public, def, , ,\n"
                       "__CDBINFO__" + esc("F:G$fa$0_0$0({0}DF,SI:S),C,0,0,0,0,0") + " = $0001 ; const, public, , t_c, code_compiler, t.c:1\n"
                       "__CDBINFO__" + esc("F:G$fb$0_0$0({0}DF,SI:S),C,0,0,0,0,0") + " = $0001 ; const, public, , t_c, code_compiler, t.c:5\n");
            const auto lr = m.dbg->load_source_map(map, true);
            const uint8_t code[] = {0x00, 0xCD, 0x10, 0x80, 0x18, 0xFE};
            for (size_t i = 0; i < sizeof code; ++i) m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), code[i]);
            m.emu.mmu().write(0x8010, 0x00);
            m.emu.mmu().write(0x8011, 0x18);
            m.emu.mmu().write(0x8012, 0xFE);
            m.start(0x8000);
            m.emu.execute_single_instruction();   // nop
            m.emu.execute_single_instruction();   // call fb
            const uint16_t sp = m.dbg->registers().SP;
            m.emu.mmu().write(static_cast<uint16_t>(sp - 2), 0x80);   // fb's "saved frame pointer": a plausible $FF80
            m.emu.mmu().write(static_cast<uint16_t>(sp - 1), 0xFF);
            const auto fr = m.dbg->c_frames();
            const auto vs = m.dbg->variables();
            const auto name = [&](const Debugger::CFrame& f) {
                return vs && f.function >= 0 ? vs->functions()[static_cast<size_t>(f.function)].name : std::string("-");
            };
            check("SRCDBG-CVAL-19", "frame 0 fb is Tracked and its caller fa, both with no saved frame pointer, is Unknown (never Chain, "
                                    "though the word at fb's fp looks like a stack address)",
                  lr.count > 0 && fr.size() == 2 && name(fr[0]) == "fb" && fr[0].how == FrameBase::Tracked && name(fr[1]) == "fa" &&
                      fr[1].how == FrameBase::Unknown && !fr[1].note.empty());
        }
        {   // SRCDBG-CVAL-24
            // GH #434 review R20: an older frame found through the chain is looked up INSIDE its call (return address - 1), so a
            // call that is the last instruction of a line stays on that line. Synthetic IX-frame program fo -> fa -> fb whose return
            // addresses ($800B, $804B) start a new line; the tracker is emptied after fo calls fa, so fo is found by the chain.
            Bare m(MachineType::ZXN_ISSUE2);
            jnext::dbg::ClientInfo ci;
            ci.name = "t";
            ci.kind = jnext::dbg::ClientKind::Test;
            m.dbg->attach(ci);
            const auto esc = [](const std::string& t) {
                std::string o;
                for (const unsigned char c : t) {
                    if (std::isalpha(c)) {
                        o += static_cast<char>(c);
                    } else {
                        char b[8];
                        std::snprintf(b, sizeof b, "_%02x", c);
                        o += b;
                    }
                }
                return o;
            };
            const auto fn_rec = [&](const char* n) {
                return "__CDBINFO__" + esc(std::string("F:G$") + n + "$0_0$0({0}DF,SI:S),C,0,0,0,0,0") +
                       " = $0001 ; const, public, , t_c, code_compiler, t.c:1\n";
            };
            const auto line_rec = [](int line, unsigned at, const char* fn) {
                char b[200];
                std::snprintf(b, sizeof b, "__C_LINE_%d_t_2ec = $%04X ; addr, local, , t_c, code_compiler, t.c::%s::0::0:%d\n", line, at, fn, line);
                return std::string(b);
            };
            const std::string map = (z / "c24.map").string();
            write_text(map, "_fa = $8000 ; addr, public, , t_c, code_compiler, t.c::fa::0::0:1\n"
                            "_fb = $8020 ; addr, public, , t_c, code_compiler, t.c::fb::0::0:10\n"
                            "_fo = $8040 ; addr, public, , t_c, code_compiler, t.c::fo::0::0:20\n" +
                                line_rec(1, 0x8000, "fa") + line_rec(2, 0x8008, "fa") + line_rec(3, 0x800B, "fa") +
                                line_rec(10, 0x8020, "fb") + line_rec(20, 0x8040, "fo") + line_rec(21, 0x8048, "fo") +
                                line_rec(22, 0x804B, "fo") + "__code_compiler_tail = $8060 ; const, public, def, , ,\n" +
                                fn_rec("fa") + fn_rec("fb") + fn_rec("fo"));
            const auto lr = m.dbg->load_source_map(map, true);
            const uint8_t pro[] = {0xDD, 0xE5, 0xDD, 0x21, 0x00, 0x00, 0xDD, 0x39};
            const auto put = [&](uint16_t at, std::initializer_list<uint8_t> tail) {
                for (size_t i = 0; i < sizeof pro; ++i) m.emu.mmu().write(static_cast<uint16_t>(at + i), pro[i]);
                uint16_t a = static_cast<uint16_t>(at + sizeof pro);
                for (const uint8_t b : tail) m.emu.mmu().write(a++, b);
            };
            put(0x8040, {0xCD, 0x00, 0x80, 0x18, 0xFE});
            put(0x8000, {0xCD, 0x20, 0x80, 0x18, 0xFE});
            put(0x8020, {0x18, 0xFE});
            m.start(0x8040);
            for (int i = 0; i < 4; ++i) m.emu.execute_single_instruction();   // fo: prologue, call fa
            m.emu.call_stack().clear();
            for (int i = 0; i < 4; ++i) m.emu.execute_single_instruction();   // fa: prologue, call fb
            for (int i = 0; i < 3; ++i) m.emu.execute_single_instruction();   // fb: prologue
            const auto fr = m.dbg->c_frames();
            const auto vs = m.dbg->variables();
            const auto name = [&](const Debugger::CFrame& f) {
                return vs && f.function >= 0 ? vs->functions()[static_cast<size_t>(f.function)].name : std::string("-");
            };
            check("SRCDBG-CVAL-24", "a frame found through the chain is looked up at return address - 1: fo's call of fa is the last "
                                    "instruction of line 21 ($804B starts line 22), fa's call of fb of line 2",
                  lr.count > 0 && fr.size() == 4 && fr[3].function == -1 && name(fr[0]) == "fb" && fr[0].how == FrameBase::Tracked && name(fr[1]) == "fa" &&
                      line_no(fr[1]) == 2 && name(fr[2]) == "fo" && fr[2].how == FrameBase::Chain && line_no(fr[2]) == 21);
        }
        {   // SRCDBG-CVAL-25
            // GH #434 review R2-2: a `ret` closes the epilogue window, so code after it in the same function is not in it.
            // Synthetic IX-frame function fq: prologue, `pop ix; ret`, then more body; stopped in the body, tracker empty.
            Bare m(MachineType::ZXN_ISSUE2);
            jnext::dbg::ClientInfo ci;
            ci.name = "t";
            ci.kind = jnext::dbg::ClientKind::Test;
            m.dbg->attach(ci);
            const auto esc = [](const std::string& t) {
                std::string o;
                for (const unsigned char c : t) {
                    if (std::isalpha(c)) {
                        o += static_cast<char>(c);
                    } else {
                        char b[8];
                        std::snprintf(b, sizeof b, "_%02x", c);
                        o += b;
                    }
                }
                return o;
            };
            const std::string map = (z / "c25.map").string();
            write_text(map, "_fq = $8000 ; addr, public, , t_c, code_compiler, t.c::fq::0::0:1\n"
                            "__C_LINE_1_t_2ec = $8000 ; addr, local, , t_c, code_compiler, t.c::fq::0::0:1\n"
                            "__code_compiler_tail = $8020 ; const, public, def, , ,\n"
                            "__CDBINFO__" + esc("F:G$fq$0_0$0({0}DF,SI:S),C,0,0,0,0,0") + " = $0001 ; const, public, , t_c, code_compiler, t.c:1\n");
            const auto lr = m.dbg->load_source_map(map, true);
            const uint8_t code[] = {0xDD, 0xE5, 0xDD, 0x21, 0x00, 0x00, 0xDD, 0x39, 0xDD, 0xE1, 0xC9, 0x00, 0x00, 0x18, 0xFE};
            for (size_t i = 0; i < sizeof code; ++i) m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), code[i]);
            m.start(0x8000);
            for (int i = 0; i < 3; ++i) m.emu.execute_single_instruction();   // push ix; ld ix,0; add ix,sp
            m.start(0x800B);                                                  // the body after the ret
            const auto fr = m.dbg->c_frames();
            check("SRCDBG-CVAL-25", "code after an epilogue ret in the same function is not in the epilogue window: Prologue, fp == IX",
                  lr.count > 0 && !fr.empty() && fr[0].how == FrameBase::Prologue && fr[0].fp == m.dbg->registers().IX &&
                      fr[0].fp == static_cast<uint16_t>(m.dbg->registers().SP));
        }
        {   // SRCDBG-CVAL-26
            // GH #434 review 3 (R3-1): "right after the restore" needs ALL its bytes. Synthetic map: l_debug_pop_frame $8100,
            // l_debug_push_frame $8110, __debug_framepointer $9000 (holding $9ABC); a push-frame function fp1 at $8000 stopped
            // right after `call $8200` (the low byte of the pop helper's address, not the high one); an IX function fx at $8300
            // stopped right after a `pop hl` ($E1 not preceded by $DD). Neither is a restore: the base is the variable's / IX.
            Bare m(MachineType::ZXN_ISSUE2);
            jnext::dbg::ClientInfo ci;
            ci.name = "t";
            ci.kind = jnext::dbg::ClientKind::Test;
            m.dbg->attach(ci);
            const auto esc = [](const std::string& t) {
                std::string o;
                for (const unsigned char c : t) {
                    if (std::isalpha(c)) {
                        o += static_cast<char>(c);
                    } else {
                        char b[8];
                        std::snprintf(b, sizeof b, "_%02x", c);
                        o += b;
                    }
                }
                return o;
            };
            const auto fn_rec = [&](const char* n) {
                return "__CDBINFO__" + esc(std::string("F:G$") + n + "$0_0$0({0}DF,SI:S),C,0,0,0,0,0") +
                       " = $0001 ; const, public, , t_c, code_compiler, t.c:1\n";
            };
            const std::string map = (z / "c26.map").string();
            write_text(map, "_fp1 = $8000 ; addr, public, , t_c, code_compiler, t.c::fp1::0::0:1\n"
                            "_fx = $8300 ; addr, public, , t_c, code_compiler, t.c::fx::0::0:5\n"
                            "l_debug_pop_frame = $8100 ; addr, public, , lib, code_l_sccz80, l.asm:1\n"
                            "l_debug_push_frame = $8110 ; addr, public, , lib, code_l_sccz80, l.asm:2\n"
                            "__debug_framepointer = $9000 ; addr, public, , lib, bss_clib, v.asm:1\n"
                            "__C_LINE_1_t_2ec = $8000 ; addr, local, , t_c, code_compiler, t.c::fp1::0::0:1\n"
                            "__C_LINE_5_t_2ec = $8300 ; addr, local, , t_c, code_compiler, t.c::fx::0::0:5\n"
                            "__code_compiler_tail = $8320 ; const, public, def, , ,\n" + fn_rec("fp1") + fn_rec("fx"));
            const auto lr = m.dbg->load_source_map(map, true);
            const uint8_t push_fn[] = {0xCD, 0x10, 0x81, 0xCD, 0x00, 0x82, 0xCD, 0x01, 0x81, 0x00, 0x18, 0xFE};
            const uint8_t ix_fn[] = {0xDD, 0xE5, 0xDD, 0x21, 0x00, 0x00, 0xDD, 0x39, 0xE1, 0x00, 0x18, 0xFE};
            for (size_t i = 0; i < sizeof push_fn; ++i) m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), push_fn[i]);
            for (size_t i = 0; i < sizeof ix_fn; ++i) m.emu.mmu().write(static_cast<uint16_t>(0x8300 + i), ix_fn[i]);
            m.emu.mmu().write(0x9000, 0xBC);
            m.emu.mmu().write(0x9001, 0x9A);
            m.start(0x8006);   // right after `call $8200`
            const auto a = m.dbg->c_frames();
            m.start(0x8009);   // right after `call $8101` (the pop helper's high byte, another low byte)
            const auto a2 = m.dbg->c_frames();
            m.start(0x8300);
            for (int i = 0; i < 3; ++i) m.emu.execute_single_instruction();   // push ix; ld ix,0; add ix,sp
            m.start(0x8309);   // right after the `pop hl`
            const auto b = m.dbg->c_frames();
            check("SRCDBG-CVAL-26", "right after `call` of an address that only shares the pop helper's low byte or only its high byte, or after a `pop hl`, is not "
                                    "right after the restore: the push-frame function's base is the variable ($9ABC), the IX function's is IX",
                  lr.count > 0 && !a.empty() && !a2.empty() && !b.empty() && a[0].how == FrameBase::Prologue && a[0].fp == 0x9ABC &&
                      a2[0].how == FrameBase::Prologue && a2[0].fp == 0x9ABC &&
                      b[0].how == FrameBase::Prologue && b[0].fp == m.dbg->registers().IX &&
                      b[0].fp != static_cast<uint16_t>(m.dbg->registers().SP - 2));
        }
        {   // SRCDBG-CVAL-27
            // GH #434 review 3: the real-fixture case: main right after `call _sum` returned, tracker emptied: its base is the
            // tracked one (main keeps locals, so it is not SP - 2).
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sccz80-cvars", "c27");
            cv_run_to(m, 0x8CB9);
            const unsigned ret = peek16(m, m.dbg->registers().SP);   // main's return address after the call
            const bool s = cv_run_to(m, static_cast<uint16_t>(ret));
            const auto tracked = m.dbg->c_frames();
            m.emu.call_stack().clear();
            const auto fr = m.dbg->c_frames();
            check("SRCDBG-CVAL-27", "master sccz80-cvars main right after `call _sum` returned, tracker emptied: Prologue, fp == the tracked fp "
                                    "(not SP - 2, which only a restore leaves)",
                  s && !tracked.empty() && !fr.empty() && fn_name(m, fr[0]) == "main" && fr[0].how == FrameBase::Prologue &&
                      fr[0].fp == tracked[0].fp && fr[0].fp != static_cast<uint16_t>(m.dbg->registers().SP - 2));
        }

        // ── GH #434 stage 3: the C variables of a frame, read on a running machine ──
        // Oracle: vars.c / other.c / bk.c / fc.c / cross.c semantics (sum(i, -5, &lp, 'x') at line 41 on its first call: t = i + 115 + 7,
        // acc = 3 (i + 115)); the maps' addresses (each stop is asserted against the map's own line record).
        using Dbg = Debugger;
        const auto cvv = [&](const Machine& m, size_t k, const char* path) {
            const Dbg::CValueResult r = m.dbg->read_c_value(k, path);
            return r.ok ? r.value : "!" + r.why;
        };
        const auto cv_starts_at = [&](const Machine& m, const char* line, uint16_t addr) {
            const auto r = m.dbg->source_map().resolve(line);
            for (const auto& a : r.starts)
                if (a.address == addr) return true;
            return false;
        };
        const auto row_of = [](const std::vector<Dbg::CValueRow>& rows, const char* name) -> const Dbg::CValueRow* {
            for (const auto& r : rows)
                if (r.name == name) return &r;
            return nullptr;
        };
        const auto has_note = [](const Dbg::CValueResult& r, const char* part) {
            for (const auto& n : r.notes)
                if (n.find(part) != std::string::npos) return true;
            return false;
        };
        // sum's first call at vars.c:41 on the three compilers
        struct SumAt { const char* fixture; const char* tag; uint16_t pc; };
        const auto at41 = [&](const SumAt& c, Machine& m) {
            cv_load(m, c.fixture, c.tag);
            return cv_starts_at(m, "vars.c:41", c.pc) && cv_run_to(m, c.pc);
        };
        const auto sccz80_41 = [&](const SumAt& c) {
            Machine m(false, MachineType::ZXN_ISSUE2);
            const bool at = at41(c, m);
            const auto loc = m.dbg->c_locals(0);
            const auto* pp = row_of(loc.rows, "p");
            const Dbg::CValueResult lp = m.dbg->read_c_value(1, "lp");
            const Dbg::CValueResult p = m.dbg->read_c_value(0, "p");
            const Dbg::CValueResult inner = m.dbg->read_c_value(0, "inner");
            return at && loc.ok && pp && cvv(m, 0, "a") == "0" && cvv(m, 0, "b") == "-5" && cvv(m, 0, "k") == "120 'x'" && cvv(m, 0, "t") == "122" &&
                   cvv(m, 0, "acc") == "345" && cvv(m, 0, "calls") == "1" && cvv(m, 0, "*p") == "{x = 3, y = 4, tag = 5}" && lp.ok &&
                   lp.where.kind == Dbg::CWhere::Kind::Cpu && p.ok && p.integer && *p.integer == lp.where.cpu && !inner.ok &&
                   inner.why == "out of scope at vars.c:41" && !row_of(loc.rows, "inner") && cvv(m, 0, "p->y") == "4";
        };
        check("SRCDBG-CVAL-28", "v2.4 sccz80-cvars at vars.c:41 (first call of sum): a=0 b=-5 k=120 'x' t=122 acc=345 calls=1, *p = {x = 3, y = 4, tag = 5}, p is "
                                "frame 1's &lp, and `inner` (block 2) is not listed and read_c_value says out of scope at vars.c:41",
              sccz80_41({"v2.4/sccz80-cvars", "c28", 0x8D3E}));
        check("SRCDBG-CVAL-29", "the same on master sccz80-cvars", sccz80_41({"master/sccz80-cvars", "c29", 0x8D3E}));
        {   // SRCDBG-CVAL-30
            Machine m(false, MachineType::ZXN_ISSUE2);
            const bool at = at41({"master/sdcc-cvars", "c30", 0x8C97}, m);
            const Dbg::CValueResult t = m.dbg->read_c_value(0, "t");
            const Dbg::CValueResult acc = m.dbg->read_c_value(0, "acc");
            const Dbg::CValueResult inner = m.dbg->read_c_value(0, "inner");
            const auto loc = m.dbg->c_locals(0);
            check("SRCDBG-CVAL-30", "master sdcc-cvars at vars.c:41: a=0 b=-5 k=120 'x' calls=1 *p as on sccz80; t is 122 read from BC with the stale note; acc is "
                                    "optimised out; `inner` is hidden (out of scope); the parameters (block 9) are visible",
                  at && cvv(m, 0, "a") == "0" && cvv(m, 0, "b") == "-5" && cvv(m, 0, "k") == "120 'x'" && cvv(m, 0, "calls") == "1" &&
                      cvv(m, 0, "*p") == "{x = 3, y = 4, tag = 5}" && t.ok && t.value == "122" && t.where.kind == Dbg::CWhere::Kind::Register &&
                      has_note(t, "in BC; may be stale") && !acc.ok && acc.why == "optimised out" && !inner.ok &&
                      inner.why == "out of scope at vars.c:41" && loc.ok && row_of(loc.rows, "a") && row_of(loc.rows, "k") && !row_of(loc.rows, "inner"));
        }
        {   // SRCDBG-CVAL-31
            Machine m(false, MachineType::ZXN_ISSUE2);
            const bool at = at41({"master/80cc-cvars", "c31", 0x8CCA}, m);
            const auto loc = m.dbg->c_locals(0);
            const auto* inner = row_of(loc.rows, "inner");
            bool marked = false;
            if (inner)
                for (const auto& n : inner->notes) marked = marked || n.find("scope unknown") != std::string::npos;
            check("SRCDBG-CVAL-31", "master 80cc-cvars at vars.c:41: a=0 b=-5 k=120 'x' t=122 acc=345 calls=1 as on sccz80; 80cc writes no block structure, so `inner` is "
                                    "shown, marked scope unknown",
                  at && cvv(m, 0, "a") == "0" && cvv(m, 0, "b") == "-5" && cvv(m, 0, "k") == "120 'x'" && cvv(m, 0, "t") == "122" && cvv(m, 0, "acc") == "345" &&
                      cvv(m, 0, "calls") == "1" && inner && marked);
        }
        {   // SRCDBG-CVAL-32
            bool ok = true;
            for (const SumAt& c : {SumAt{"master/sccz80-cvars", "c32a", 0x8D2B}, SumAt{"master/sdcc-cvars", "c32b", 0x8C94}}) {
                Machine m(false, MachineType::ZXN_ISSUE2);
                cv_load(m, c.fixture, c.tag);
                ok = ok && cv_starts_at(m, "vars.c:39", c.pc) && cv_run_to(m, c.pc) && cvv(m, 0, "inner") == "7";
            }
            check("SRCDBG-CVAL-32", "at vars.c:39 (block 2 open) `inner` is 7 (p->x + p->y) on sccz80 and zsdcc", ok);
        }
        {   // SRCDBG-CVAL-33
            bool ok = true;
            for (const SumAt& c : {SumAt{"master/sccz80-cvars", "c33a", 0x8D3E}, SumAt{"master/80cc-cvars", "c33b", 0x8CCA}}) {
                Machine m(false, MachineType::ZXN_ISSUE2);
                const bool at = at41(c, m);
                const Dbg::CValueResult lp = m.dbg->read_c_value(1, "lp");
                const Dbg::CValueResult pp = m.dbg->read_c_value(1, "pp");
                ok = ok && at && cvv(m, 1, "lp") == "{x = 3, y = 4, tag = 5}" && cvv(m, 1, "i") == "0" && cvv(m, 1, "c") == "120 'x'" &&
                     cvv(m, 1, "big") == "100000" && lp.ok && pp.ok && pp.integer && *pp.integer == lp.where.cpu;
            }
            Machine z(false, MachineType::ZXN_ISSUE2);
            const bool zat = at41({"master/sdcc-cvars", "c33c", 0x8C97}, z);
            const Dbg::CValueResult big = z.dbg->read_c_value(1, "big");
            const Dbg::CValueResult cc = z.dbg->read_c_value(1, "c");
            const Dbg::CValueResult pp = z.dbg->read_c_value(1, "pp");
            check("SRCDBG-CVAL-33", "frame 1 (main) at the vars.c:41 stop: lp = {3, 4, 5}, i = 0, c = 120 'x', big = 100000 and pp == &lp on sccz80 and 80cc; on zsdcc big, c and pp "
                                    "are registers of an outer frame (optimised out) while lp and i are read from the stack",
                  ok && zat && cvv(z, 1, "lp") == "{x = 3, y = 4, tag = 5}" && cvv(z, 1, "i") == "0" && !big.ok && big.why == "optimised out" && !cc.ok && !pp.ok);
        }
        {   // SRCDBG-CVAL-34
            // vars.c's globals at the stop of CVAL-28 (g_float is `{6}SF` on sccz80/80cc and `{4}SF` on zsdcc; the enum colour BLUE is 6)
            bool ok = true;
            std::string seen;
            for (const SumAt& c : {SumAt{"master/sccz80-cvars", "c34a", 0x8D3E}, SumAt{"master/sdcc-cvars", "c34b", 0x8C97}, SumAt{"master/80cc-cvars", "c34c", 0x8CCA}}) {
                Machine m(false, MachineType::ZXN_ISSUE2);
                const bool at = at41(c, m);
                const std::string want_float = "1.5";
                const std::string got =
                    "g_int=" + cvv(m, 0, "g_int") + " g_uint=" + cvv(m, 0, "g_uint") + " g_schar=" + cvv(m, 0, "g_schar") + " g_char=" + cvv(m, 0, "g_char") +
                    " g_long=" + cvv(m, 0, "g_long") + " g_ulong=" + cvv(m, 0, "g_ulong") + " g_float=" + cvv(m, 0, "g_float") + " g_str=" + cvv(m, 0, "g_str") +
                    " g_cptr=" + cvv(m, 0, "g_cptr").substr(cvv(m, 0, "g_cptr").find(' ') + 1) + " g_matrix=" + cvv(m, 0, "g_matrix") +
                    " g_pt=" + cvv(m, 0, "g_pt") + " g_colour=" + cvv(m, 0, "g_colour") + " g_byte=" + cvv(m, 0, "g_byte") + " s_file=" + cvv(m, 0, "s_file") +
                    " vs=" + cvv(m, 0, "vars.c::s_dup") + " os=" + cvv(m, 0, "other.c::s_dup");
                const std::string want = "g_int=-5 g_uint=40000 g_schar=-3 g_char=65 'A' g_long=305419896 g_ulong=3405691582 g_float=" + want_float +
                                         " g_str=\"hi\" g_cptr=\"abc\" g_matrix={{1, 2, 3}, {4, 5, 6}} g_pt={x = 1, y = 2, tag = 3} g_colour=6 g_byte=7 s_file=9 vs=0 os=4";
                if (!at || got != want) {
                    ok = false;
                    seen += std::string(" [") + c.fixture + "] " + got;
                }
            }
            check("SRCDBG-CVAL-34", "the globals of vars.c at the vars.c:41 stop, on sccz80, zsdcc and 80cc: g_int=-5 g_uint=40000 g_schar=-3 g_char=65 'A' g_long=305419896 "
                                    "g_ulong=3405691582 g_float 1.5 g_str=\"hi\" g_cptr=\"abc\" g_matrix g_pt g_colour=6 g_byte=7 s_file=9, and the two file statics s_dup "
                                    "told apart by file: vars.c::s_dup=0 and other.c::s_dup=4",
                  ok, seen);
        }
        {   // SRCDBG-CVAL-35
            // in main's while (1): everything main stored has happened (arr = {467, 471, 475, 479}; s_file = 479 + 120; other(599) = 1202, as uint8_t 178)
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sccz80-cvars", "c35");
            const bool at = cv_starts_at(m, "vars.c:64", 0x8E1F) && cv_run_to(m, 0x8E1F);
            const Dbg::CValueResult head = m.dbg->read_c_value(0, "g_head");
            const Dbg::CValueResult node = m.dbg->read_c_value(0, "g_node");
            const Dbg::CValueResult fn = m.dbg->read_c_value(0, "g_fnptr2");
            check("SRCDBG-CVAL-35", "in main's loop: g_head shows ` <g_node>` (the label of what it points at), g_head->val is 100000, g_node.next is NULL, s_file is 599, "
                                    "vars.c::s_dup is 178 and g_fnptr2 shows ` <other>`",
                  at && head.ok && head.value.find(" <g_node>") != std::string::npos && cvv(m, 0, "g_head->val") == "100000" &&
                      cvv(m, 0, "g_node.next") == "$0000 (NULL)" && node.ok && cvv(m, 0, "s_file") == "599" && cvv(m, 0, "vars.c::s_dup") == "178" && fn.ok &&
                      fn.value.find(" <other>") != std::string::npos && cvv(m, 0, "g_head->next->val").find("null pointer") != std::string::npos);
        }
        {   // SRCDBG-CVAL-36
            // a real mid-run attach (the program ran with no client, so nothing was tracked): the fallback bases give main's locals
            bool ok = true;
            for (const char* fx : {"master/sccz80-cvars", "master/sdcc-cvars"}) {
                Bare m(MachineType::ZXN_ISSUE2);
                const fs::path d = var_dir(fx[7] == 's' && fx[8] == 'c' ? "c36a" : "c36b", fx);
                emulator_apply_load(m.emu, (d / "prog.nex").string(), false);
                m.dbg->load_program_sidecars((d / "prog.nex").string());
                m.frames(3);
                jnext::dbg::ClientInfo ci;
                ci.name = "late";
                ci.kind = jnext::dbg::ClientKind::Test;
                const ClientId c = m.dbg->attach(ci).value;
                m.dbg->pause(c);
                const auto str = [&](const char* path) {
                    const Dbg::CValueResult r = m.dbg->read_c_value(0, path);
                    return r.ok ? r.value : "!" + r.why;
                };
                const Dbg::CValueResult lp = m.dbg->read_c_value(0, "lp");
                bool good = str("lp") == "{x = 3, y = 4, tag = 5}" && str("i") == "4" && str("arr") == "{467, 471, 475, 479}";
                if (fx[7] == 's' && fx[8] == 'c') {   // sccz80 keeps c, big and pp on the stack
                    const Dbg::CValueResult pp = m.dbg->read_c_value(0, "pp");
                    good = good && str("c") == "120 'x'" && str("big") == "100000" && lp.ok && pp.ok && pp.integer && *pp.integer == lp.where.cpu;
                }
                ok = ok && good;
            }
            check("SRCDBG-CVAL-36", "a real mid-run attach (main in its while (1), no tracked frame): lp = {3, 4, 5}, i = 4, arr = {467, 471, 475, 479} on sccz80 and zsdcc, and "
                                    "on sccz80 also c = 120 'x', big = 100000 and pp == &lp",
                  ok);
        }
        {   // SRCDBG-CVAL-20
            // GH #434 review F7 (R12): zsdcc `push ix; ld ix,0; add ix,sp`: after the push (entry+2) and after the ld (entry+6)
            // SP = E - 2 already, so the base is SP.
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sdcc-cvars", "c20");
            cv_run_to(m, 0x8C48);
            const uint16_t E = m.dbg->registers().SP;
            const bool s1 = cv_run_to(m, 0x8C4A);
            m.emu.call_stack().clear();
            const auto a = m.dbg->c_frames();
            const bool s2 = cv_run_to(m, 0x8C4E);
            m.emu.call_stack().clear();
            const auto b = m.dbg->c_frames();
            check("SRCDBG-CVAL-20", "zsdcc sum at entry + 2 and entry + 6 with no frames: Prologue, fp == E - 2 (SP), not the caller's IX",
                  s1 && s2 && !a.empty() && !b.empty() && a[0].how == FrameBase::Prologue && b[0].how == FrameBase::Prologue &&
                      a[0].fp == static_cast<uint16_t>(E - 2) && b[0].fp == static_cast<uint16_t>(E - 2));
        }
        {   // SRCDBG-CVAL-21
            // GH #434 review F7 (R17): a saved frame pointer that is no stack address above this frame ends the chain.
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sccz80-cvars", "c21");
            cv_run_to(m, 0x8D3E);
            m.emu.call_stack().clear();
            const uint16_t fp = m.dbg->c_frames()[0].fp;
            bool ok = true;
            for (const uint16_t bad : {uint16_t(0x1234), uint16_t(0xFFFE), static_cast<uint16_t>(fp + 2)}) {
                m.emu.mmu().write(fp, static_cast<uint8_t>(bad & 0xFF));
                m.emu.mmu().write(static_cast<uint16_t>(fp + 1), static_cast<uint8_t>(bad >> 8));
                const auto fr = m.dbg->c_frames();
                ok = ok && fr.size() == 2 && fr[1].how == FrameBase::Unknown &&
                     fr[1].note.find("saved frame pointer") != std::string::npos;
            }
            check("SRCDBG-CVAL-21", "a saved frame pointer below the frame, at the top of memory or inside the frame itself gives an Unknown "
                                    "caller with a note, not a Chain",
                  ok);
        }
        {   // SRCDBG-CVAL-22
            // GH #434 review F7 (R19): a tracked frame whose caller_space was not recorded uses the page mapped now, with a note.
            Machine m(false, MachineType::ZXN_ISSUE2);
            cv_load(m, "master/sccz80-cvars", "c22");
            cv_run_to(m, 0x8D3E);
            std::vector<CallFrame> frames = m.emu.call_stack().frames();
            for (CallFrame& f : frames) f.caller_space = MemSpace::cpu();
            m.emu.call_stack().restore_frames(frames);
            const auto fr = m.dbg->c_frames();
            check("SRCDBG-CVAL-22", "a tracked frame with no recorded caller_space: its pc is in the page mapped now, with a note",
                  fr.size() >= 2 && fr[1].pc.space == m.dbg->source_page(fr[1].pc.address) && !(fr[1].pc.space == MemSpace::cpu()) &&
                      !fr[1].note.empty() && fn_name(m, fr[1]) == "main");
        }
        {   // SRCDBG-CVAL-23
            // GH #434 review F8: a request needs an attached client.
            Bare m;
            jnext::dbg::ClientInfo ci;
            ci.name = "t";
            ci.kind = jnext::dbg::ClientKind::Test;
            const ClientId c = m.dbg->attach(ci).value;
            m.dbg->detach(c);
            const Result none = m.dbg->set_call_stack_enabled(jnext::dbg::CLIENT_NONE, true);
            const Result gone = m.dbg->set_call_stack_enabled(c, true);
            check("SRCDBG-CVAL-23", "set_call_stack_enabled is RefusedUnavailable for CLIENT_NONE and for a detached client, and changes nothing",
                  none == Result::RefusedUnavailable && gone == Result::RefusedUnavailable && !m.dbg->call_stack_enabled());
        }
    }

    // ── GH #344 stage 3: a tape program's maps ───────────────────────────────
    // The oracle is the committed v2.4/sccz80-tape fixture (zcc +zx; its prog.tap carries
    // prog_CODE.bin as one CODE block at $8000) and the owner's decisions of 2026-10-29:
    // a map attaches at once; its identity is judged per memory space, the first time the
    // CPU is about to run inside that space; a mismatch in any space drops the map.
    // main.c:19 starts at $8CC4 here, `_main` is $8CA1, the map has 46 source lines and
    // 19 symbols.
    {
        const fs::path tape = dir / "tape";
        const char* kFx = "v2.4/sccz80-tape";
        const long kFlip = 0xCC4;   // main.c:19's first byte, as an offset into prog_CODE.bin
        auto rejected = [](const std::string& map) {
            return "Rejected z88dk source map '" + map +
                   "': the program loaded from tape does not match its binary identity";
        };
        {   // TAPE-03
            const fs::path d = tape / "t3";
            const std::string tap = tape_copy(kFx, d);
            Machine m;
            park(m);
            Debugger::SidecarLoad r;
            const std::string log1 = debugger_log_of([&] { r = m.dbg->load_program_sidecars(tap); });
            const auto st1 = m.dbg->source_map_status();
            const std::string at19 = line_of(*m.dbg, 0x8CC4);
            run_frames(m, 2);
            const bool parked_pending = m.dbg->source_map_status().pending;
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", -1, 0x8000);
            m.set_pc(0x8000);
            Debugger::SourceMapStatus st2;
            bool stayed_running = false;
            uint16_t pc_after = 0x8000;
            const std::string log2 = debugger_log_of([&] {
                m.dbg->run(m.a);
                m.emu.run_frame();
                stayed_running = !m.emu.debug_state().paused();   // the check does not stop the machine
                pc_after = m.pc();
                m.dbg->pause(m.a);
                st2 = m.dbg->source_map_status();
            });
            check("SRCDBG-TAPE-03", "prog.map beside a tape attaches at once and works (main.c:19 resolves), "
                                    "pending; once the bytes are in and the CPU runs them it is verified "
                                    "and kept",
                  r.sources == 46 && r.sources_pending && r.symbols == 19 &&
                      st1.state == Debugger::SourceMapStatus::State::Attached && st1.pending &&
                      at19 == "main.c:19" && parked_pending &&
                      has(log1, "Loaded 46 z88dk source lines from '" + (d / "prog.map").string() +
                                    "' (identity check pending: the program loads from tape)") &&
                      st2.state == Debugger::SourceMapStatus::State::Attached && !st2.pending &&
                      m.dbg->source_map().size() == 46 && stayed_running && pc_after != 0x8000 &&
                      has(log2, "Verified z88dk source map '" + (d / "prog.map").string() +
                                    "': the program loaded from tape matches its binary identity"),
                  log1 + log2);
        }
        {   // TAPE-04
            const fs::path d = tape / "t4";
            const std::string tap = tape_copy(kFx, d);
            Machine m;
            park(m);
            const auto r = m.dbg->load_program_sidecars(tap);
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", kFlip, 0x8000);
            m.set_pc(0x8000);
            Debugger::SourceMapStatus st;
            const std::string log = debugger_log_of([&] {
                run_frames(m, 1);
                st = m.dbg->source_map_status();
            });
            check("SRCDBG-TAPE-04", "one byte of main.c:19 wrong: at the first execution the map is dropped "
                                    "with the symbols that came with it, the error is logged and the Source "
                                    "tab's reason says why",
                  r.sources == 46 && r.symbols == 19 && m.dbg->source_map().empty() &&
                      st.state == Debugger::SourceMapStatus::State::None && !st.pending &&
                      has(st.reason, "does not match") && !m.dbg->lookup_name("_main") &&
                      has(log, rejected((d / "prog.map").string())) &&
                      has(log, "Dropped the program's symbols"),
                  st.reason + "\n" + log);
        }
        {   // TAPE-05
            const fs::path d = tape / "t5";
            const std::string tap = tape_copy(kFx, d);
            Machine m;
            park(m);
            m.dbg->load_program_sidecars(tap);
            // The bytes arrive (writes into the range) while the CPU runs elsewhere: neither judges.
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", kFlip, 0x8000);
            run_frames(m, 2);
            const auto parked = m.dbg->source_map_status();
            check("SRCDBG-TAPE-05", "writes into the range and execution outside it do not judge the map: "
                                    "pending after two frames at $6000, even with a wrong byte loaded",
                  parked.pending && parked.state == Debugger::SourceMapStatus::State::Attached &&
                      m.dbg->source_map().size() == 46,
                  parked.reason);
        }
        {   // TAPE-06: the space, not the logical address
            const fs::path d = tape / "t6";
            const std::string tap = tape_copy(kFx, d);
            Machine m(false, MachineType::ZXN_ISSUE2);
            park(m);
            m.dbg->nextreg_write(m.a, 0x54, 4);
            m.dbg->load_program_sidecars(tap);
            const uint8_t jr[2] = {0x18, 0xFE};
            m.dbg->poke(m.a, MemSpace::page(40), 0, 2, jr);
            m.dbg->nextreg_write(m.a, 0x54, 40);
            m.set_pc(0x8000);
            run_frames(m, 1);
            const bool other_page = m.dbg->source_map_status().pending;
            tape_bytes(m, MemSpace::page(4), d / "prog_CODE.bin");
            m.dbg->nextreg_write(m.a, 0x54, 4);
            m.set_pc(0x8000);
            run_frames(m, 1);
            const auto st = m.dbg->source_map_status();
            check("SRCDBG-TAPE-06", "$8000 is in the code range but page 40 is mapped there, not the page "
                                    "the map was bound to (4): still pending; with page 4 mapped and the "
                                    "program in it, verified",
                  other_page && st.state == Debugger::SourceMapStatus::State::Attached && !st.pending &&
                      m.dbg->source_map().size() == 46);
        }
        {   // TAPE-07: the pending window changes nothing in the machine
            const fs::path d = tape / "t7";
            const std::string tap = tape_copy(kFx, d);
            Bare with, without;
            with.dbg->load_program_sidecars(tap);
            const bool armed_with = with.emu.debug_state().armed();
            const bool armed_without = without.emu.debug_state().armed();
            with.frames(20);
            without.frames(20);
            const auto ra = with.emu.cpu().get_registers();
            const auto rb = without.emu.cpu().get_registers();
            uint64_t ha = 1469598103934665603ULL, hb = ha;
            for (uint32_t a = 0x4000; a < 0x10000; ++a) {
                ha = (ha ^ with.emu.mmu().peek(static_cast<uint16_t>(a))) * 1099511628211ULL;
                hb = (hb ^ without.emu.mmu().peek(static_cast<uint16_t>(a))) * 1099511628211ULL;
            }
            check("SRCDBG-TAPE-07", "a pending window that never triggers leaves the machine exactly as "
                                    "one without it: equal registers, clock and RAM after 20 frames "
                                    "(the first armed, the second not)",
                  armed_with && !armed_without && with.dbg->source_map_status().pending &&
                      ra.PC == rb.PC && ra.SP == rb.SP && ra.AF == rb.AF && ra.BC == rb.BC &&
                      ra.DE == rb.DE && ra.HL == rb.HL &&
                      with.emu.clock().get() == without.emu.clock().get() && ha == hb);
        }
        {   // TAPE-08: with no client, the hold arms the machine only while a check is pending
            const fs::path d = tape / "t8";
            const std::string tap = tape_copy(kFx, d);
            Bare m;
            const bool before = m.emu.debug_state().armed();
            m.dbg->load_program_sidecars(tap);
            const bool pending = m.emu.debug_state().armed() && m.dbg->source_map_status().pending;
            const std::vector<uint8_t> code = read_all(d / "prog_CODE.bin");
            for (size_t i = 0; i < code.size(); ++i)
                m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), code[i]);
            m.start(0x8000);
            m.frames(1);
            const bool ran_on = !m.emu.debug_state().paused();
            const auto st = m.dbg->source_map_status();
            const bool after = m.emu.debug_state().armed();
            const fs::path d2 = tape / "t8b";
            const std::string tap2 = tape_copy(kFx, d2, /*bins=*/false);
            Bare n;
            n.dbg->load_program_sidecars(tap2);
            check("SRCDBG-TAPE-08", "with no client attached the machine is unarmed, armed while a check "
                                    "is pending, unarmed again once it is judged; a map with no identity "
                                    "never arms it",
                  !before && pending && !after && ran_on && st.state == Debugger::SourceMapStatus::State::Attached &&
                      !st.pending && !n.emu.debug_state().armed() && !n.dbg->source_map_status().pending &&
                      n.dbg->source_map().size() == 46);
        }
        {   // TAPE-09: a rewind within and before the generation
            const fs::path d = tape / "t9";
            const std::string tap = tape_copy(kFx, d);
            Machine m(/*rewind=*/true);
            park(m);
            run_frames(m, 1);
            const uint32_t before_load = m.dbg->time().frame;
            m.dbg->load_program_sidecars(tap);
            run_frames(m, 1);
            const uint32_t after_load = m.dbg->time().frame;
            run_frames(m, 3);
            const Result back = m.dbg->rewind_to_frame(m.a, after_load);
            const bool a_pending = m.dbg->source_map_status().pending && m.emu.debug_state().verify_hold();
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", -1, 0x8000);
            m.set_pc(0x8000);
            run_frames(m, 1);
            const auto after = m.dbg->source_map_status();
            const Result back2 = m.dbg->rewind_to_frame(m.a, before_load);
            const bool b_gone = m.dbg->source_map().empty() && !m.emu.debug_state().verify_hold();
            check("SRCDBG-TAPE-09", "a rewind inside the load's generation keeps the check pending and the "
                                    "window armed (and the program can still be verified); a rewind to "
                                    "before the load drops the map and the window",
                  back == Result::Ok && a_pending && !after.pending &&
                      after.state == Debugger::SourceMapStatus::State::Attached &&
                      back2 == Result::Ok && b_gone,
                  "back=" + std::to_string(int(back)) + " back2=" + std::to_string(int(back2)));
        }
        {   // TAPE-10: a reset in the middle of the load
            const fs::path d = tape / "t10";
            const std::string tap = tape_copy(kFx, d);
            Machine m;
            park(m);
            m.dbg->load_program_sidecars(tap);
            const Result soft = m.dbg->reset(m.a, ResetKind::Soft);
            Debugger::SourceMapStatus st;
            const std::string log = debugger_log_of([&] { st = m.dbg->source_map_status(); });
            check("SRCDBG-TAPE-10", "a reset before the program arrived judges the pending map at the "
                                    "landing: dropped as not matching the program now in memory, window off",
                  soft == Result::Ok && st.state == Debugger::SourceMapStatus::State::None &&
                      m.dbg->source_map().empty() && !m.emu.debug_state().verify_hold() &&
                      has(log, "Dropped the program's z88dk source map: its binary identity does not "
                               "match the program now in memory"),
                  log);
        }
        {   // TAPE-11: an SLD with a hash
            const fs::path d = tape / "t11";
            fs::create_directories(d);
            const std::string head = "|SLD.data.version|1\n||program.name:prog\n";
            const std::string tail = "||program.org:32768\n||program.size:4\n";
            write_text(d / "game.sld",
                       head + "||program.sha256:04bb9c5fbc1b0517a33504a5fbb7b9de7dd72a1de84987241364d2674495a823\n" +
                           tail + program_sld(p80, p90).substr(20));
            write_text(d / "game.tap", "");
            auto fresh = [&](uint8_t third, Debugger::SidecarLoad& r, std::string& log,
                             Debugger::SourceMapStatus& st) {
                auto m = std::make_unique<Machine>();
                park(*m);
                for (uint16_t i = 0; i < 4; ++i) m->emu.mmu().write(0x8000 + i, 0);
                r = m->dbg->load_program_sidecars((d / "game.tap").string());
                const uint8_t program[4] = {0x00, 0x00, third, 0x00};
                m->dbg->poke(m->a, MemSpace::cpu(), 0x8000, 4, program);
                m->set_pc(0x8000);
                log = debugger_log_of([&] {
                    run_frames(*m, 1);
                    st = m->dbg->source_map_status();
                });
                return m;
            };
            Debugger::SidecarLoad r1, r2;
            std::string log1, log2;
            Debugger::SourceMapStatus st1, st2;
            auto good = fresh(0xCD, r1, log1, st1);
            auto bad = fresh(0xCE, r2, log2, st2);
            check("SRCDBG-TAPE-11", "an SLD whose hash is of the program attaches pending beside a tape, "
                                    "with $8000 still zero (not judged at the attach); verified when the "
                                    "bytes match at the first execution and dropped when one differs",
                  r1.sources == 6 && r1.sources_pending &&
                      st1.state == Debugger::SourceMapStatus::State::Attached && !st1.pending &&
                      has(log1, "Verified SLD source map") &&
                      st2.state == Debugger::SourceMapStatus::State::None && bad->dbg->source_map().empty() &&
                      has(log2, "Rejected SLD source map") &&
                      has(log2, "the program loaded from tape does not match its binary identity"),
                  log1 + log2);
        }
        {   // TAPE-12
            const fs::path d = tape / "t12";
            const std::string tap = tape_copy(kFx, d, /*bins=*/false);
            Bare m;
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] { r = m.dbg->load_program_sidecars(tap); });
            check("SRCDBG-TAPE-12", "prog.map without its section binaries beside a tape attaches "
                                    "unverified at once: no pending state, no window, the unverified "
                                    "wording in the log",
                  r.sources == 46 && !r.sources_pending && !m.dbg->source_map_status().pending &&
                      !m.emu.debug_state().armed() &&
                      has(log, "(program identity not verified: no section binaries beside it)"),
                  log);
        }
        {   // TAPE-13: --source-map with a tape
            const fs::path d = tape / "t13";
            tape_copy(kFx, d);
            const std::string map = (d / "prog.map").string();
            Machine m;
            park(m);
            const auto out = m.dbg->load_source_map(map, false, /*verify_at_first_execution=*/true);
            const auto st1 = m.dbg->source_map_status();
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", kFlip, 0x8000);
            m.set_pc(0x8000);
            run_frames(m, 1);
            const auto st2 = m.dbg->source_map_status();
            check("SRCDBG-TAPE-13", "a map loaded for a tape program is the user's and pending; a wrong "
                                    "byte at the first execution drops it: None, not Stale, with the reason",
                  out.count == 46 && out.pending && st1.by_user && st1.pending &&
                      has(out.identity_note, "identity check pending") &&
                      st2.state == Debugger::SourceMapStatus::State::None && !st2.by_user &&
                      has(st2.reason, "does not match") && m.dbg->source_map().empty());
        }
        {   // TAPE-14: the verdict is in before a stop on the first instruction
            const fs::path d = tape / "t14";
            const std::string tap = tape_copy(kFx, d);
            Machine m;
            park(m);
            m.dbg->load_program_sidecars(tap);
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", kFlip, 0x8000);
            Subscription stop = execute_stop(0x8000);
            Debugger::SourceMapStatus::State seen = Debugger::SourceMapStatus::State::Attached;
            bool pending_seen = true;
            stop.handler = [&](const jnext::dbg::Event&, Debugger& dd) {
                const auto st = dd.source_map_status();
                seen = st.state;
                pending_seen = st.pending;
                return Action::Stop;
            };
            m.dbg->subscribe(m.a, stop);
            // A JP from $6000, not a resume at $8000: the first instruction after a resume is
            // exempt from every breakpoint (GH #221), the window's check is not.
            const uint8_t jp[3] = {0xC3, 0x00, 0x80};
            m.dbg->poke(m.a, MemSpace::cpu(), 0x6000, 3, jp);
            m.set_pc(0x6000);
            run_frames(m, 1);
            check("SRCDBG-TAPE-14", "a stop on the very instruction that triggers the check already sees "
                                    "the verdict (the window runs before the breakpoint gate)",
                  seen == Debugger::SourceMapStatus::State::None && !pending_seen &&
                      m.pc() == 0x8000,
                  "seen=" + std::to_string(int(seen)) + " pending=" + std::to_string(pending_seen) +
                      " pc=" + hex(m.pc()));
        }
    }

    // ── GH #344 stage 3: per memory space (owner decision 2026-10-29) ─────────
    // v2.4/sccz80-page20: prog_CODE.bin is page 4 ($8000..), prog_PAGE_20.bin is page 20
    // ($C000..$C023; banked.c:8 is $C003). Two spaces, two checks.
    {
        const fs::path tape = dir / "tape";
        const char* kFx = "v2.4/sccz80-page20";
        auto verified_lines = [](const Z88Log& l) {
            std::vector<std::string> out;
            for (const auto& x : l.lines)
                if (x.rfind("Verified ", 0) == 0) out.push_back(x);
            return out;
        };
        {   // TAPE-15, -16
            const fs::path d = tape / "t15";
            const std::string tap = tape_copy(kFx, d);
            const std::string map = (d / "prog.map").string();
            Machine m(false, MachineType::ZXN_ISSUE2);
            Z88Log lc;
            m.dbg->set_listener(m.a, &lc);
            park(m);
            m.dbg->nextreg_write(m.a, 0x54, 4);
            m.dbg->nextreg_write(m.a, 0x56, 8);
            const auto r = m.dbg->load_program_sidecars(tap);
            // Page 4 has its program; page 20 holds nothing yet (a bank the tape loads later).
            tape_bytes(m, MemSpace::page(4), d / "prog_CODE.bin");
            m.set_pc(0x8000);
            m.dbg->step_into(m.a);
            const auto mid = m.dbg->source_map_status();
            const bool mid_hold = m.emu.debug_state().verify_hold();
            const auto lines1 = verified_lines(lc);
            m.set_pc(0x8000);
            m.dbg->step_into(m.a);   // the same space again: not judged twice
            const size_t lines_again = verified_lines(lc).size();
            check("SRCDBG-TAPE-15", "the first space run (page 4) is judged and the map kept, still pending "
                                    "for page 20, which holds nothing yet and is not judged; the window "
                                    "stays armed, and page 4 is not judged twice",
                  r.sources_pending && mid.state == Debugger::SourceMapStatus::State::Attached &&
                      mid.pending && mid_hold && !m.dbg->source_map().empty() &&
                      lines1.size() == 1 &&
                      has(lines1[0], "Verified z88dk source map '" + map +
                                         "' in P04; 1 memory space still to check") &&
                      lines_again == 1,
                  "state=" + std::to_string(int(mid.state)) + " empty=" + std::to_string(m.dbg->source_map().empty()) + " src_pending=" + std::to_string(r.sources_pending) + " mid.pending=" +
                      std::to_string(mid.pending) + " hold=" + std::to_string(mid_hold) + " lines1=" +
                      std::to_string(lines1.size()) + " again=" + std::to_string(lines_again) + " " +
                      (lines1.empty() ? "no Verified line" : lines1[0]));
            tape_bytes(m, MemSpace::page(20), d / "prog_PAGE_20.bin");
            m.dbg->nextreg_write(m.a, 0x56, 20);
            m.set_pc(0xC003);
            m.dbg->step_into(m.a);
            const auto end = m.dbg->source_map_status();
            const auto lines2 = verified_lines(lc);
            check("SRCDBG-TAPE-16", "the second space (page 20), loaded after the first was judged and run "
                                    "later, is judged then: the map is verified whole, the window off",
                  end.state == Debugger::SourceMapStatus::State::Attached && !end.pending &&
                      !m.emu.debug_state().verify_hold() && lines2.size() == 2 &&
                      has(lines2[1], "Verified z88dk source map '" + map +
                                         "': the program loaded from tape matches its binary identity"),
                  std::to_string(lines2.size()) + " lines; pending=" + std::to_string(end.pending) +
                      " hold=" + std::to_string(m.emu.debug_state().verify_hold()) + " state=" +
                      std::to_string(int(end.state)) + " reason=" + end.reason);
        }
        {   // TAPE-17: the later space does not match
            const fs::path d = tape / "t17";
            const std::string tap = tape_copy(kFx, d);
            Machine m(false, MachineType::ZXN_ISSUE2);
            park(m);
            m.dbg->nextreg_write(m.a, 0x54, 4);
            m.dbg->nextreg_write(m.a, 0x56, 8);
            const auto r = m.dbg->load_program_sidecars(tap);
            tape_bytes(m, MemSpace::page(4), d / "prog_CODE.bin");
            m.set_pc(0x8000);
            m.dbg->step_into(m.a);
            const bool kept_after_first = m.dbg->source_map_status().pending && !m.dbg->source_map().empty();
            tape_bytes(m, MemSpace::page(20), d / "prog_PAGE_20.bin", /*flip=*/5);
            m.dbg->nextreg_write(m.a, 0x56, 20);
            m.set_pc(0xC003);
            Debugger::SourceMapStatus st;
            const std::string log = debugger_log_of([&] {
                run_frames(m, 1);
                st = m.dbg->source_map_status();
            });
            check("SRCDBG-TAPE-17", "page 4 matched and was judged; a wrong byte in page 20, judged when it "
                                    "first runs, still drops the whole map and its symbols",
                  r.sources > 0 && r.symbols > 0 && kept_after_first &&
                      st.state == Debugger::SourceMapStatus::State::None && m.dbg->source_map().empty() &&
                      !m.emu.debug_state().verify_hold() &&
                      has(log, "Rejected z88dk source map '" + (d / "prog.map").string() +
                                   "': the program loaded from tape does not match its binary identity") &&
                      has(log, "Dropped the program's symbols"),
                  log);
        }
        {   // TAPE-18: the other order
            const fs::path d = tape / "t18";
            const std::string tap = tape_copy(kFx, d);
            Machine m(false, MachineType::ZXN_ISSUE2);
            Z88Log lc;
            m.dbg->set_listener(m.a, &lc);
            park(m);
            m.dbg->nextreg_write(m.a, 0x54, 4);
            m.dbg->nextreg_write(m.a, 0x56, 8);
            m.dbg->load_program_sidecars(tap);
            // Page 20 first; page 4 holds nothing yet.
            tape_bytes(m, MemSpace::page(20), d / "prog_PAGE_20.bin");
            m.dbg->nextreg_write(m.a, 0x56, 20);
            m.set_pc(0xC003);
            m.dbg->step_into(m.a);
            const auto mid = m.dbg->source_map_status();
            const auto lines1 = verified_lines(lc);
            tape_bytes(m, MemSpace::page(4), d / "prog_CODE.bin");
            m.set_pc(0x8000);
            m.dbg->step_into(m.a);
            const auto end = m.dbg->source_map_status();
            check("SRCDBG-TAPE-18", "the spaces are judged in the order they first run: page 20 first (page "
                                    "4 still empty and not judged), then page 4",
                  mid.state == Debugger::SourceMapStatus::State::Attached && mid.pending &&
                      lines1.size() == 1 && has(lines1[0], "in P14; 1 memory space still to check") &&
                      end.state == Debugger::SourceMapStatus::State::Attached && !end.pending &&
                      verified_lines(lc).size() == 2);
        }
        {   // TAPE-19: a rewind with a space judged and the other not, compared over the replayed run
            struct Run {
                std::vector<std::string> verified;
                bool mid_pending = false, mid_hold = false, end_pending = true, end_hold = true;
                bool end_attached = false;
                std::string rewind_note;
            };
            auto scenario = [&](bool with_rewind, const fs::path& d) {
                const std::string tap = tape_copy(kFx, d);
                Run out;
                Machine m(/*rewind=*/true, MachineType::ZXN_ISSUE2);
                Z88Log lc;
                m.dbg->set_listener(m.a, &lc);
                park(m);
                m.dbg->nextreg_write(m.a, 0x54, 4);
                m.dbg->nextreg_write(m.a, 0x56, 8);
                run_frames(m, 1);
                const uint32_t f_pre = m.dbg->time().frame;
                m.dbg->load_program_sidecars(tap);
                run_frames(m, 1);
                const uint32_t f_attach = m.dbg->time().frame;
                tape_bytes(m, MemSpace::page(4), d / "prog_CODE.bin");
                m.set_pc(0x8000);
                m.dbg->step_into(m.a);   // page 4 judged here
                park(m);
                run_frames(m, 2);
                const uint32_t f_late = m.dbg->time().frame;
                if (with_rewind) {
                    const Result back = m.dbg->rewind_to_frame(m.a, f_attach);
                    const auto st = m.dbg->source_map_status();   // before page 4 was judged: still judged
                    const bool st_hold = m.emu.debug_state().verify_hold();
                    // Before the load there is no map and no window; coming forward again, the
                    // generation's record brings the map back with one space judged and one to come.
                    const Result before = m.dbg->rewind_to_frame(m.a, f_pre);
                    const bool none_then = m.dbg->source_map().empty() && !m.emu.debug_state().verify_hold();
                    const Result fwd = m.dbg->rewind_to_frame(m.a, f_late);
                    out.rewind_note = "back=" + std::to_string(int(back)) + " before=" + std::to_string(int(before)) +
                                      " fwd=" + std::to_string(int(fwd)) + " pend=" + std::to_string(st.pending) +
                                      " hold=" + std::to_string(st_hold) + " none=" + std::to_string(none_then);
                    if (back != Result::Ok || before != Result::Ok || fwd != Result::Ok || !st.pending ||
                        !st_hold || !none_then)
                        out.rewind_note += " BAD";
                }
                const auto mid = m.dbg->source_map_status();
                out.mid_pending = mid.pending;
                out.mid_hold = m.emu.debug_state().verify_hold();
                tape_bytes(m, MemSpace::page(20), d / "prog_PAGE_20.bin");
                m.dbg->nextreg_write(m.a, 0x56, 20);
                m.set_pc(0xC003);
                m.dbg->step_into(m.a);
                const auto end = m.dbg->source_map_status();
                out.end_pending = end.pending;
                out.end_hold = m.emu.debug_state().verify_hold();
                out.end_attached = end.state == Debugger::SourceMapStatus::State::Attached;
                out.verified = verified_lines(lc);
                return out;
            };
            const Run control = scenario(false, tape / "t19a");
            const Run rewound = scenario(true, tape / "t19b");
            auto strip_map = [](std::vector<std::string> v) {
                for (auto& s : v) {   // the two runs use two directories
                    const size_t a = s.find('\''), b = s.find('\'', a + 1);
                    if (a != std::string::npos && b != std::string::npos) s.erase(a, b - a + 1);
                }
                return v;
            };
            check("SRCDBG-TAPE-19", "with one space judged and one to come, a rewind back past the judged "
                                    "space's first run and forward again leaves the check as it was: the "
                                    "replayed run produces the same Verified lines, the same pending state "
                                    "and window as the run without a rewind",
                  rewound.rewind_note.find("BAD") == std::string::npos && control.mid_pending &&
                      control.mid_hold && rewound.mid_pending == control.mid_pending &&
                      rewound.mid_hold == control.mid_hold && !control.end_pending && !control.end_hold &&
                      rewound.end_pending == control.end_pending && rewound.end_hold == control.end_hold &&
                      control.end_attached && rewound.end_attached &&
                      control.verified.size() == 2 &&
                      strip_map(rewound.verified) == strip_map(control.verified),
                  rewound.rewind_note + " control=" + std::to_string(control.verified.size()) +
                      " rewound=" + std::to_string(rewound.verified.size()));
        }
        {   // TAPE-20, -21: the check, run by the guest, touches nothing (PURE-01's live fixture)
            const fs::path d = tape / "t20";
            const std::string tap = tape_copy(kFx, d);
            struct Side {
                std::unique_ptr<Machine> m;
                std::vector<uint8_t> mem;
                size_t ring = 0;
                bool hit = false;
                uint8_t latch = 0;
                uint64_t clock = 0;
                Z80Registers regs;
                bool pending = false;
            };
            auto build = [&](bool with_map) {
                Side s;
                s.m = std::make_unique<Machine>(false, MachineType::ZXN_ISSUE2);
                Machine& m = *s.m;
                park(m);
                m.dbg->nextreg_write(m.a, 0x54, 4);
                m.dbg->nextreg_write(m.a, 0x56, 8);
                if (with_map) m.dbg->load_program_sidecars(tap);
                tape_bytes(m, MemSpace::page(4), d / "prog_CODE.bin");
                tape_bytes(m, MemSpace::page(20), d / "prog_PAGE_20.bin");
                m.emu.mmu().set_machine_timing(MachineTimingMode::TimingPlus3);
                m.emu.mmu().set_p3_floating_bus_dat(0x3C);
                for (uint16_t at : {0x8CB5, 0xC010, 0xC020, 0x8E0E})
                    m.emu.debug_state().breakpoints().add_watchpoint(at, WatchType::READ);
                Subscription sub;
                sub.kind = EventKind::Mem;
                sub.access = Access::Read;
                sub.filter.lo = 0x0000;
                sub.filter.hi = 0xFFFF;
                sub.action = Action::Continue;
                m.dbg->subscribe(m.a, sub);
                return s;
            };
            auto settle = [&](Side& s) {
                Machine& m = *s.m;
                s.mem.clear();
                for (uint32_t a = 0x4000; a < 0x10000; ++a)
                    s.mem.push_back(m.emu.mmu().peek(static_cast<uint16_t>(a)));
                s.ring = m.emu.debug_state().event_table()->size();
                s.hit = m.emu.debug_state().data_bp_hit();
                s.latch = m.emu.mmu().p3_floating_bus_dat();
                s.clock = m.emu.clock().get();
                s.regs = m.emu.cpu().get_registers();
                s.pending = m.dbg->source_map_status().pending;
            };
            Side with = build(true), without = build(false);
            for (Side* s : {&with, &without}) {
                Machine& m = *s->m;
                m.set_pc(0x8000);
                m.dbg->step_into(m.a);                 // page 4 judged inside the guest's scope
                m.dbg->nextreg_write(m.a, 0x56, 20);
                m.set_pc(0xC003);
                m.dbg->step_into(m.a);                 // page 20 judged
                settle(*s);
            }
            const bool same = with.mem == without.mem && with.ring == without.ring &&
                              with.hit == without.hit && with.latch == without.latch &&
                              with.clock == without.clock && with.regs.PC == without.regs.PC &&
                              with.regs.SP == without.regs.SP && with.regs.AF == without.regs.AF;
            check("SRCDBG-TAPE-20", "a check made by the guest's own execution (two steps, one in each "
                                    "space) leaves the machine as the same steps without a map: same "
                                    "memory, event ring, watch flag, floating-bus latch, clock and "
                                    "registers; and both spaces were judged",
                  same && !with.pending && !with.hit && with.latch == 0x3C,
                  "mem=" + std::to_string(with.mem == without.mem) + " ring=" + std::to_string(with.ring) +
                      "/" + std::to_string(without.ring) + " hit=" + std::to_string(with.hit) + "/" +
                      std::to_string(without.hit) + " latch=" + hex(with.latch) + "/" + hex(without.latch) +
                      " clock=" + std::to_string(with.clock) + "/" + std::to_string(without.clock) +
                      " pending=" + std::to_string(with.pending));
            // Control: ONE guest read of a checked, watched byte in the same fixture moves all of it.
            Side ctl = build(true);
            settle(ctl);
            const size_t ring_before = ctl.ring;
            {
                DebugState::GuestExecutionScope guest(ctl.m->emu.debug_state());
                (void)ctl.m->emu.mmu().read(0xC010);
            }
            settle(ctl);
            check("SRCDBG-TAPE-21", "control for TAPE-20: ONE guest read of $C010 in the same fixture "
                                    "raises the watchpoint, enters the event ring and moves the latch, "
                                    "so the row above cannot pass because nothing here is live",
                  ctl.hit && ctl.ring > ring_before && ctl.latch != 0x3C,
                  "hit=" + std::to_string(ctl.hit) + " ring=" + std::to_string(ctl.ring) + " latch=" +
                      hex(ctl.latch));
        }
    }

    // ── GH #344 stage 3: who answers for the verdict ─────────────────────────
    {
        const fs::path tape = dir / "tape";
        const char* kFx = "v2.4/sccz80-tape";
        const long kFlip = 0xCC4;
        {   // TAPE-22: the user's source map is kept, the sidecar's symbols still wait for their program
            const fs::path d = tape / "t22";
            const std::string tap = tape_copy(kFx, d);
            write_text(d / "mine.sld", program_sld(p80, p90));
            auto side = [&](long flip, Debugger::SidecarLoad& r, std::string& log) {
                auto m = std::make_unique<Machine>();
                park(*m);
                m->dbg->load_source_map((d / "mine.sld").string(), false);
                r = m->dbg->load_program_sidecars(tap);
                tape_bytes(*m, MemSpace::cpu(), d / "prog_CODE.bin", flip, 0x8000);
                m->set_pc(0x8000);
                log = debugger_log_of([&] {
                    run_frames(*m, 1);
                    (void)m->dbg->source_map_status();
                });
                return m;
            };
            Debugger::SidecarLoad rg, rb;
            std::string logg, logb;
            auto good = side(-1, rg, logg);
            auto bad = side(kFlip, rb, logb);
            check("SRCDBG-TAPE-22", "with the user's source map kept, prog.map's symbols attach beside a "
                                    "tape and wait for the program: kept when it matches, dropped (the "
                                    "user's map untouched) when it does not",
                  rg.symbols == 19 && rg.sources == -1 && good->dbg->lookup_name("_main") == 0x8CA1 &&
                      rb.symbols == 19 && !bad->dbg->lookup_name("_main") &&
                      bad->dbg->source_map().loaded_file() == (d / "mine.sld").string() &&
                      bad->dbg->source_map().size() == 6 &&
                      has(logb, "Rejected the symbols of z88dk map '" + (d / "prog.map").string() + "'") &&
                      has(logb, "Dropped the program's symbols") && !has(logg, "Rejected"),
                  logg + logb);
        }
        {   // TAPE-23: --source-map on top of a pending sidecar map
            const fs::path d = tape / "t23";
            const std::string tap = tape_copy(kFx, d);
            const std::string map = (d / "prog.map").string();
            auto both = [&](long flip, std::string& log, Debugger::SourceMapLoad& out) {
                auto m = std::make_unique<Machine>();
                park(*m);
                m->dbg->load_program_sidecars(tap);
                out = m->dbg->load_source_map(map, false, true);
                tape_bytes(*m, MemSpace::cpu(), d / "prog_CODE.bin", flip, 0x8000);
                m->set_pc(0x8000);
                log = debugger_log_of([&] {
                    run_frames(*m, 1);
                    (void)m->dbg->source_map_status();
                });
                return m;
            };
            Debugger::SourceMapLoad og, ob;
            std::string logg, logb;
            auto good = both(-1, logg, og);
            auto bad = both(kFlip, logb, ob);
            const auto sg = good->dbg->source_map_status();
            const auto sb = bad->dbg->source_map_status();
            check("SRCDBG-TAPE-23", "the CLI order (the sidecar first, then --source-map of the same map): "
                                    "matching bytes keep the user's map and the symbols; a wrong byte "
                                    "drops BOTH, the user's map to None and the sidecar's symbols with it",
                  og.pending && ob.pending && sg.state == Debugger::SourceMapStatus::State::Attached &&
                      sg.by_user && !sg.pending && good->dbg->lookup_name("_main") == 0x8CA1 &&
                      sb.state == Debugger::SourceMapStatus::State::None && !sb.by_user &&
                      bad->dbg->source_map().empty() && !bad->dbg->lookup_name("_main") &&
                      !bad->emu.debug_state().verify_hold() &&
                      has(logb, "Rejected the symbols of z88dk map '" + map + "'") &&
                      [&] {   // exactly one source-map rejection: the sidecar's check only answers for the symbols
                          size_t n = 0;
                          for (size_t at = 0; (at = logb.find("Rejected z88dk source map", at)) != std::string::npos; ++at) ++n;
                          return n == 1;
                      }(),
                  logg + logb);
        }
        {   // TAPE-24: a user's pending map is carried into the next tape load
            const fs::path d = tape / "t24";
            tape_copy(kFx, d);
            write_text(d / "other.tap", "");
            Machine m;
            park(m);
            m.dbg->load_source_map((d / "prog.map").string(), false, true);
            m.dbg->load_program_sidecars((d / "other.tap").string());
            const auto st = m.dbg->source_map_status();
            const bool held = m.emu.debug_state().verify_hold();
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", -1, 0x8000);
            m.set_pc(0x8000);
            m.dbg->step_into(m.a);
            const auto end = m.dbg->source_map_status();
            check("SRCDBG-TAPE-24", "a pending map the user loaded survives the next program's load, still "
                                    "pending with its window armed, and is verified when its code runs",
                  st.by_user && st.pending && held && end.state == Debugger::SourceMapStatus::State::Attached &&
                      end.by_user && !end.pending && !m.emu.debug_state().verify_hold());
        }
        {   // TAPE-25: a debugger Step is an execution
            const fs::path d = tape / "t25";
            const std::string tap = tape_copy(kFx, d);
            Machine m;
            park(m);
            m.dbg->load_program_sidecars(tap);
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", -1, 0x8000);
            m.set_pc(0x8000);
            const bool before = m.dbg->source_map_status().pending;
            m.dbg->step_into(m.a);
            check("SRCDBG-TAPE-25", "one debugger Step onto the first instruction of the program judges "
                                    "the map, as a run would",
                  before && !m.dbg->source_map_status().pending && m.pc() != 0x8000 &&
                      m.dbg->source_map().size() > 0);
        }
        {   // TAPE-26: a reset after the program arrived judges it whole
            const fs::path d = tape / "t26";
            const std::string tap = tape_copy(kFx, d);
            Machine m;
            park(m);
            m.dbg->load_program_sidecars(tap);
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", -1, 0x8000);
            const Result soft = m.dbg->reset(m.a, ResetKind::Soft);
            const auto st = m.dbg->source_map_status();
            Machine u;
            park(u);
            u.dbg->load_source_map((d / "prog.map").string(), false, true);
            tape_bytes(u, MemSpace::cpu(), d / "prog_CODE.bin", -1, 0x8000);
            const Result soft2 = u.dbg->reset(u.a, ResetKind::Soft);
            const auto su = u.dbg->source_map_status();
            check("SRCDBG-TAPE-26", "a soft reset after the whole program arrived verifies the pending map "
                                    "at the landing (it matches), a sidecar's and the user's alike: kept, "
                                    "no longer pending, window off",
                  soft == Result::Ok && st.state == Debugger::SourceMapStatus::State::Attached &&
                      !st.pending && !m.emu.debug_state().verify_hold() &&
                      m.dbg->source_map().size() > 0 && soft2 == Result::Ok && su.by_user &&
                      su.state == Debugger::SourceMapStatus::State::Attached && !su.pending &&
                      !u.emu.debug_state().verify_hold() && u.dbg->source_map().size() > 0);
        }
        {   // TAPE-27: replacing or clearing the map takes the window with it
            const fs::path d = tape / "t27";
            const std::string tap = tape_copy(kFx, d);
            write_text(d / "other.tap", "");
            Machine m;
            park(m);
            m.dbg->load_program_sidecars(tap);
            const bool armed1 = m.emu.debug_state().verify_hold();
            m.dbg->load_program_sidecars((d / "other.tap").string());   // a program with no sidecars
            const bool off1 = !m.emu.debug_state().verify_hold() && m.dbg->source_map().empty() &&
                              !m.dbg->source_map_status().pending;
            Machine n;
            park(n);
            n.dbg->load_source_map((d / "prog.map").string(), false, true);
            const bool armed2 = n.emu.debug_state().verify_hold();
            n.dbg->clear_source_map();
            const bool off2 = !n.emu.debug_state().verify_hold() && !n.dbg->source_map_status().pending;
            check("SRCDBG-TAPE-27", "a pending map replaced by the next program's load, or cleared by hand, "
                                    "leaves no window armed",
                  armed1 && off1 && armed2 && off2,
                  std::to_string(armed1) + std::to_string(off1) + std::to_string(armed2) + std::to_string(off2));
        }
        {   // TAPE-28: the user's pending map across a rewind into an earlier generation
            const fs::path d = tape / "t28";
            tape_copy(kFx, d);
            write_text(d / "other.tap", "");
            write_text(d / "other2.tap", "");
            Machine m(/*rewind=*/true);
            park(m);
            run_frames(m, 1);
            const uint32_t f_pre = m.dbg->time().frame;
            m.dbg->load_program_sidecars((d / "other.tap").string());
            m.dbg->load_source_map((d / "prog.map").string(), false, true);
            run_frames(m, 1);
            m.dbg->load_program_sidecars((d / "other2.tap").string());   // another generation, the map carried
            run_frames(m, 2);
            const Result back = m.dbg->rewind_to_frame(m.a, f_pre);
            const auto st = m.dbg->source_map_status();
            const bool held = m.emu.debug_state().verify_hold();
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", -1, 0x8000);
            m.set_pc(0x8000);
            m.dbg->step_into(m.a);
            const auto end = m.dbg->source_map_status();
            check("SRCDBG-TAPE-28", "a rewind into an earlier generation does not judge the user's pending map "
                                    "against the memory of that moment (the program has not run its code "
                                    "yet): still attached, pending, window armed, and verified when it runs",
                  back == Result::Ok && st.by_user && st.pending &&
                      st.state == Debugger::SourceMapStatus::State::Attached && held &&
                      end.state == Debugger::SourceMapStatus::State::Attached && !end.pending,
                  "back=" + std::to_string(int(back)) + " state=" + std::to_string(int(st.state)) +
                      " pending=" + std::to_string(st.pending));
        }
        {   // TAPE-29: a SuspendScope disarms the verify hold and puts it back
            const fs::path d = tape / "t29";
            const std::string tap = tape_copy(kFx, d);
            Bare m;
            m.dbg->load_program_sidecars(tap);
            DebugState& ds = m.emu.debug_state();
            const bool before = ds.armed() && ds.verify_hold();
            bool during = true;
            {
                DebugState::SuspendScope quiet(ds);
                during = ds.armed() || ds.verify_hold();
            }
            check("SRCDBG-TAPE-29", "SuspendScope (a save, a load) disarms a machine held only by a pending "
                                    "check and puts the hold back when it ends",
                  before && !during && ds.armed() && ds.verify_hold());
        }
        // ── TAPE-30..32: the deferred check on the RUN path, and landings ──────────
        {   // TAPE-30, -31: the hook's reads, run by run_frame() with a READ watch over a checked byte
            const fs::path d = tape / "t30";
            const std::string tap = tape_copy(kFx, d);
            struct Side {
                std::unique_ptr<Machine> m;
                uint16_t pc = 0;
                bool hit = false;
                size_t ring = 0;
                bool pending = true;
            };
            // `with_map`: the sidecar map is pending and judges at $8000; `pre_read`: one guest read of the
            // watched byte before the run (the control).
            auto run = [&](bool with_map, bool pre_read) {
                Side sd;
                sd.m = std::make_unique<Machine>();
                Machine& m = *sd.m;
                park(m);
                if (with_map) m.dbg->load_program_sidecars(tap);
                tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", -1, 0x8000);
                m.emu.debug_state().breakpoints().add_watchpoint(0x8CC5, WatchType::READ);   // inside main.c:19
                Subscription sub;
                sub.kind = EventKind::Mem;
                sub.access = Access::Read;
                sub.filter.lo = 0x0000;
                sub.filter.hi = 0xFFFF;
                sub.action = Action::Continue;
                m.dbg->subscribe(m.a, sub);
                if (pre_read) {   // one read of the watched byte at the first instruction, as the check's reader would make
                    Subscription at = execute_stop(0x8000);
                    Emulator* emu = &m.emu;
                    at.handler = [emu](const jnext::dbg::Event&, Debugger&) {
                        DebugState::GuestExecutionScope guest(emu->debug_state());
                        (void)emu->mmu().read(0x8CC5);
                        return Action::Continue;
                    };
                    m.dbg->subscribe(m.a, at);
                    park(m);
                    const uint8_t jp[3] = {0xC3, 0x00, 0x80};
                    m.dbg->poke(m.a, MemSpace::cpu(), 0x6000, 3, jp);
                }
                m.set_pc(pre_read ? 0x6000 : 0x8000);
                m.dbg->run(m.a);
                m.emu.run_frame();
                sd.pc  = m.pc();
                sd.hit = m.emu.debug_state().data_bp_hit();   // before anything consumes it
                sd.ring = m.emu.debug_state().event_table()->size();
                m.dbg->pause(m.a);
                sd.pending = m.dbg->source_map_status().pending;
                return sd;
            };
            const Side with = run(true, false), without = run(false, false), ctl = run(false, true);
            check("SRCDBG-TAPE-30", "the check made inside run_frame() with a READ watch over a checked byte "
                                    "leaves the run as the same run without a map: it stops at the same "
                                    "instruction (the program's own read of $8CC5, not one instruction in "
                                    "at the hook's), with the same watch flag and event ring; and the "
                                    "map was judged",
                  with.pc == without.pc && with.pc != 0x8001 && with.hit == without.hit &&
                      with.ring == without.ring && !with.pending,
                  "pc=" + hex(with.pc) + "/" + hex(without.pc) + " hit=" + std::to_string(with.hit) + "/" +
                      std::to_string(without.hit) + " ring=" + std::to_string(with.ring) + "/" +
                      std::to_string(without.ring) + " pending=" + std::to_string(with.pending));
            check("SRCDBG-TAPE-31", "control for TAPE-30: ONE guest read of $8CC5 made at the first instruction "
                                    "(as the check's reader would make it) moves the observation: the run "
                                    "stops one instruction in, not where the program reads the byte, so "
                                    "TAPE-30's stop PC can see a read made by the check",
                  ctl.pc == 0x8001 && ctl.pc != without.pc,
                  "ctl pc=" + hex(ctl.pc) + " hit=" + std::to_string(ctl.hit) + " vs " + hex(without.pc));
        }
        {   // TAPE-32: a verdict is applied before a landing nobody read in between
            const fs::path d = tape / "t32";
            const std::string tap = tape_copy(kFx, d);
            Machine m;
            park(m);
            m.dbg->load_program_sidecars(tap);
            tape_bytes(m, MemSpace::cpu(), d / "prog_CODE.bin", kFlip, 0x8000);
            m.set_pc(0x8000);
            m.dbg->run(m.a);
            m.emu.run_frame();      // the hook judges the wrong byte; no reader runs recheck()
            m.dbg->pause(m.a);
            Debugger::SourceMapStatus st;
            const std::string log = debugger_log_of([&] {
                (void)m.dbg->reset(m.a, ResetKind::Soft);
                st = m.dbg->source_map_status();
            });
            check("SRCDBG-TAPE-32", "a wrong byte judged by the hook and a reset before any reader: the map "
                                    "is dropped for the tape verdict (reason and Error line), not for the "
                                    "landing's own \"does not match the program now in memory\"",
                  st.state == Debugger::SourceMapStatus::State::None && has(st.reason, "from tape") &&
                      has(log, "Rejected z88dk source map '" + (d / "prog.map").string() +
                               "': the program loaded from tape does not match its binary identity"),
                  st.reason + "\n" + log);
        }
    }

    // ── GH #345 (E23-14): C stepping semantics on real z88dk code ───────────────
    // Oracles: the -run fixtures' maps and listings (test/fixtures/z88dk), design 7
    // (Over/Out by call depth) and the recorded z88dk-gdb sessions A-E (plan
    // gh345-plan.md section 6) for v2.4/sccz80-debug. Each row reaches its start by
    // RUNNING to a breakpoint, as SRCDBG-Z88-14 does.
    {
        const fs::path sdir = dir / "srcstep";
        // Run to `addr` on a fixture's machine; false when it never stops there.
        const auto reach = [](Machine& m, uint16_t addr) {
            const auto id = m.dbg->subscribe(m.a, execute_stop(addr)).value;
            m.dbg->run(m.a);
            for (int i = 0; i < 100 && !m.dbg->state().paused; ++i) m.emu.run_frame();
            m.dbg->unsubscribe(m.a, id);
            return m.dbg->state().paused && m.pc() == addr;
        };
        // Depth over CALL / RST frames (an interrupt's frame is not the program's).
        const auto depth = [](const Machine& m) {
            int n = 0;
            for (const auto& f : m.dbg->call_stack())
                if (f.type == CallType::CALL || f.type == CallType::RST) ++n;
            return n;
        };
        const auto at = [&](Machine& m, uint16_t pc, const char* line) {
            return m.pc() == pc && line_of(*m.dbg, m.pc()) == line;
        };
        // A machine with the fixture loaded, its sidecars attached, stopped at `addr`.
        const auto start = [&](Machine& m, const char* fixture, const char* sub, uint16_t addr,
                               std::string* log) {
            const fs::path d = sdir / sub;
            z88_copy(fixture, d);
            z88_load(m, d / "prog.nex");
            *log = debugger_log_of([&] { m.dbg->load_program_sidecars((d / "prog.nex").string()); });
            return reach(m, addr) && has(*log, "(program identity verified)");
        };

        // master / sdcc `__banked`: bmain.c:13 `result = banked_add(2, 3);` at $8CAE.
        {   // SRCSTEP-01
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sdcc-banked-run", "bs1", 0x8CAE, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-01", "sdcc __banked: Over bmain.c:13's call to banked_add (through "
                                "banked_call) lands on bmain.c:15 ($8CBE; line 14 is its alias) at the "
                                "same depth",
                  ok && at(m, 0x8CBE, "bmain.c:15") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-02
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sdcc-banked-run", "bs2", 0x8CAE, &log);
            m.dbg->source_step(m.a, Step::Into);
            check("SRCSTEP-02", "sdcc __banked: Into bmain.c:13 runs through banked_call and stops "
                                "at banked.c:5 ($C000), fetched from page 20",
                  ok && at(m, 0xC000, "banked.c:5") && m.dbg->effective_page(0xC000) == 20, m.where());
        }
        {   // SRCSTEP-03
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sdcc-banked-run", "bs3", 0x8CAE, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Into);
            m.dbg->source_step(m.a, Step::Into);
            const bool inside = at(m, 0xC008, "banked.c:8");
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-03", "sdcc __banked: Out of banked_add returns to the caller's line "
                                "bmain.c:13 at $8CB9 (after the defq), at the caller's depth",
                  ok && inside && at(m, 0x8CB9, "bmain.c:13") && depth(m) == d0, m.where());
        }
        {   // SRCSTEP-08
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sdcc-banked-run", "bs8", 0x8CAE, &log);
            const int d0 = depth(m);
            int d805A = -1, d809F = -1, d807E = -1, dC000 = -1, d8083 = -1, d809D = -1, d8CB9 = -1;
            int mindepth = 1 << 20;
            bool in_call = false, arrived = false;
            const auto first = [](int& slot, int v) { if (slot < 0) slot = v; };
            for (int i = 0; i < 200 && !arrived; ++i) {
                const uint16_t pc = m.pc();
                const int d = depth(m);
                if (pc == 0x805A) in_call = true;
                if (pc == 0x8CB9) { first(d8CB9, d); arrived = true; break; }
                if (in_call && d < mindepth) mindepth = d;
                if (pc == 0x805A) first(d805A, d);
                if (pc == 0x809F) first(d809F, d);
                if (pc == 0x807E) first(d807E, d);
                if (pc == 0xC000) first(dC000, d);
                if (pc == 0x8083) first(d8083, d);
                if (pc == 0x809D) first(d809D, d);
                m.dbg->step_into(m.a);
            }
            check("SRCSTEP-08", "banked_call keeps its frame for the whole trampoline: depth d0+1 at "
                                "$805A, d0+2 in set_mmu ($809F), d0+1 after its RET ($807E), d0+2 in "
                                "banked_add ($C000), d0+1 after the target's RET ($8083, $809D), d0 at "
                                "$8CB9, and never below d0+1 inside the call",
                  ok && arrived && d805A == d0 + 1 && d809F == d0 + 2 && d807E == d0 + 1 &&
                      dC000 == d0 + 2 && d8083 == d0 + 1 && d809D == d0 + 1 && d8CB9 == d0 &&
                      mindepth == d0 + 1,
                  "d0=" + std::to_string(d0) + " 805A:" + std::to_string(d805A) + " 809F:" +
                      std::to_string(d809F) + " 807E:" + std::to_string(d807E) + " C000:" +
                      std::to_string(dC000) + " 8083:" + std::to_string(d8083) + " 809D:" +
                      std::to_string(d809D) + " 8CB9:" + std::to_string(d8CB9) + " min:" +
                      std::to_string(mindepth));
        }

        // v2.4 / sccz80 -debug: z88dk-gdb sessions A, B, E (l_debug_push_frame in every function).
        {   // SRCSTEP-04
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-debug", "s4", 0x8CBE, &log);
            m.dbg->source_step(m.a, Step::Into);
            const bool first = at(m, 0x8C78, "main.c:10");
            m.dbg->source_step(m.a, Step::Into);
            check("SRCSTEP-04", "sccz80 -debug: Into main.c:20 stops at square's first line "
                                "($8C78, main.c:10) and the next Into runs l_debug_push_frame through "
                                "to main.c:11 ($8C7B) [z88dk-gdb A, B]",
                  ok && first && at(m, 0x8C7B, "main.c:11"), m.where());
        }
        {   // SRCSTEP-05
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-debug", "s5", 0x8CB5, &log);
            m.dbg->source_step(m.a, Step::Over);
            const bool first = at(m, 0x8CBE, "main.c:20");
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-05", "sccz80 -debug: Over main.c:19 -> :20 ($8CBE), Over the call to "
                                "square -> main.c:21 ($8CE9) [z88dk-gdb A, E]",
                  ok && first && at(m, 0x8CE9, "main.c:21"), m.where());
        }
        {   // SRCSTEP-06
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-debug", "s6", 0x8CBE, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Into);
            m.dbg->source_step(m.a, Step::Into);
            const bool in = at(m, 0x8C7B, "main.c:11");
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-06", "sccz80 -debug: Out of square's last line returns to main.c:20 at "
                                "$8CC9, at the caller's depth [z88dk-gdb A: finish]",
                  ok && in && at(m, 0x8CC9, "main.c:20") && depth(m) == d0, m.where());
        }
        {   // SRCSTEP-09
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-debug", "s9", 0x8CBE, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Into);
            const bool in = at(m, 0x8C78, "main.c:10");
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-09", "sccz80 -debug: Over from square's first line runs the helper "
                                "through and stops at main.c:11 ($8C7B), inside square [z88dk-gdb A: next]",
                  ok && in && at(m, 0x8C7B, "main.c:11") && depth(m) == d0 + 1, m.where());
        }
        {   // SRCSTEP-10
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-debug", "s10", 0x8CBE, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Into);
            m.dbg->source_step(m.a, Step::Into);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-10", "sccz80 -debug: Over from square's last line returns to main.c:20 "
                                "($8CC9) [z88dk-gdb B: next]",
                  ok && at(m, 0x8CC9, "main.c:20") && depth(m) == d0, m.where());
        }

        // GH #349 (R1): an int parameter makes sccz80 call l_gintspsp (EX (SP),HL; JP (HL): a
        // return with no RET). Oracles: test/fixtures/z88dk/{v2.4,master}/{sccz80,80cc}-intarg
        // (map, listing, binary) and the helper's source in the z88dk library.
        {   // SRCSTEP-14
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-intarg", "i14", 0x8C6E, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-14", "sccz80 2.4: Over accum's line 4 (the int parameter, CALL l_gintspsp at $8C72) stays in accum and stops at line 5 ($8C84), at the same depth",
                  ok && at(m, 0x8C84, "intarg.c:5") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-15
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-intarg", "i15", 0x8C6E, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-15", "sccz80 master: Over accum's line 4 (CALL l_gintspsp at $8C72) stays in accum and stops at line 5 ($8C84), at the same depth",
                  ok && at(m, 0x8C84, "intarg.c:5") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-16
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/80cc-intarg", "i16", 0x8C66, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-16", "80cc master: Over accum's line 4 stops at line 5 ($8C7D; GH #445: was $8C7E before z88dk v2.4-2883) at the same depth (80cc emits no l_gintspsp call for this program: the unchanged control)",
                  ok && at(m, 0x8C7D, "intarg.c:5") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-17
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-intarg", "i17", 0x8C6E, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-17", "sccz80 2.4: Out of accum from line 4 returns to main at $8C9A (intarg.c:12) one level up, the helper's frame already closed",
                  ok && at(m, 0x8C9A, "intarg.c:12") && depth(m) == d0 - 1, log + m.where());
        }
        {   // SRCSTEP-18
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-intarg", "i18", 0x8C6E, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-18", "sccz80 master: Out of accum from line 4 returns to main at $8C99 (intarg.c:11) one level up",
                  ok && at(m, 0x8C99, "intarg.c:11") && depth(m) == d0 - 1, log + m.where());
        }
        {   // SRCSTEP-19
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-intarg", "i19", 0x8C6E, &log);
            const size_t n0 = m.dbg->call_stack().size();
            const bool moved = reach(m, 0x8C75);
            const auto& f = m.dbg->call_stack();
            check("SRCSTEP-19", "sccz80 2.4: after l_gintspsp returns by JP (HL) ($8C75, inside accum) the Call Stack holds the frames it held before the call and no frame for the helper (the newest is the CALL _accum at $8C97)",
                  ok && moved && n0 > 0 && f.size() == n0 && f.back().caller_pc == 0x8C97,
                  log + "frames " + std::to_string(f.size()) + "/" + std::to_string(n0) + " " + m.where());
        }
        {   // SRCSTEP-20
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-intarg", "i20", 0x8C6E, &log);
            const size_t n0 = m.dbg->call_stack().size();
            const bool moved = reach(m, 0x8C75);
            const auto& f = m.dbg->call_stack();
            check("SRCSTEP-20", "sccz80 master: after l_gintspsp returns by JP (HL) ($8C75, inside accum) the Call Stack holds no frame for the helper (the newest is the CALL _accum at $8C96)",
                  ok && moved && n0 > 0 && f.size() == n0 && f.back().caller_pc == 0x8C96,
                  log + "frames " + std::to_string(f.size()) + "/" + std::to_string(n0) + " " + m.where());
        }
        {   // SRCSTEP-21
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-intarg", "i21", 0x8C9D, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-21", "sccz80 master: Over main's line 12, which calls l_gintspsp and then accum (a call that itself calls the helper), stops at line 13 ($8CAE) at the same depth",
                  ok && at(m, 0x8CAE, "intarg.c:13") && depth(m) == d0, log + m.where());
        }

        // GH #349 (R1'): long arithmetic (l_long_sub: POP IX ... JP (IX), SP above the slot) and switch
        // (l_case: POP HL as the case table, JP (HL) to a case label). Oracles: test/fixtures/z88dk/
        // {v2.4,master}/{sccz80,80cc}-longsw (map, listing, binary) and the helpers' library sources.
        {   // SRCSTEP-22
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-longsw", "l22", 0x8CC8, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-22", "sccz80 2.4: Over lsub's line 4 (CALL l_long_sub at $8CDD: POP IX ... JP (IX)) stays in lsub and stops at line 5 ($8CE4), at the same depth",
                  ok && at(m, 0x8CE4, "longsw.c:5") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-23
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-longsw", "l23", 0x8CC8, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-23", "sccz80 master: Over lsub's line 4 (CALL l_long_sub) stays in lsub and stops at line 5 ($8CE4), at the same depth",
                  ok && at(m, 0x8CE4, "longsw.c:5") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-24
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/80cc-longsw", "l24", 0x8C83, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-24", "80cc master: Over lsub's line 4 stops at line 5 ($8CA7) at the same depth (80cc inlines the long subtraction: the unchanged control)",
                  ok && at(m, 0x8CA7, "longsw.c:5") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-25
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-longsw", "l25", 0x8CC8, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-25", "sccz80 2.4: Out of lsub from line 4 returns to main at $8D75 (longsw.c:28) one level up",
                  ok && at(m, 0x8D75, "longsw.c:28") && depth(m) == d0 - 1, log + m.where());
        }
        {   // SRCSTEP-26
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-longsw", "l26", 0x8CC8, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-26", "sccz80 master: Out of lsub from line 4 returns to main at $8D75 (longsw.c:27) one level up",
                  ok && at(m, 0x8D75, "longsw.c:27") && depth(m) == d0 - 1, log + m.where());
        }
        {   // SRCSTEP-27
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-longsw", "l27", 0x8CC8, &log);
            const size_t n0 = m.dbg->call_stack().size();
            const bool moved = reach(m, 0x8CE0);
            const auto& f = m.dbg->call_stack();
            check("SRCSTEP-27", "sccz80 2.4: after l_long_sub returns by JP (IX) ($8CE0, inside lsub) the Call Stack holds the frames it held before and none for the helper (the newest is the CALL _lsub at $8D72)",
                  ok && moved && n0 > 0 && f.size() == n0 && f.back().caller_pc == 0x8D72,
                  log + "frames " + std::to_string(f.size()) + "/" + std::to_string(n0) + " " + m.where());
        }
        {   // SRCSTEP-28
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-longsw", "l28", 0x8CC8, &log);
            const size_t n0 = m.dbg->call_stack().size();
            const bool moved = reach(m, 0x8CE0);
            const auto& f = m.dbg->call_stack();
            check("SRCSTEP-28", "sccz80 master: after l_long_sub returns by JP (IX) ($8CE0) the Call Stack holds no frame for the helper (the newest is the CALL _lsub at $8D72)",
                  ok && moved && n0 > 0 && f.size() == n0 && f.back().caller_pc == 0x8D72,
                  log + "frames " + std::to_string(f.size()) + "/" + std::to_string(n0) + " " + m.where());
        }
        {   // SRCSTEP-29
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-longsw", "l29", 0x8CF4, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-29", "sccz80 2.4: Over sw's switch (CALL l_case at $8CF8 matching case 200) stays in sw and stops at the case body longsw.c:13 ($8D20), at the same depth",
                  ok && at(m, 0x8D20, "longsw.c:13") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-30
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-longsw", "l30", 0x8CF4, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-30", "sccz80 master: Over sw's switch (CALL l_case, case 200) stops at the case body longsw.c:13 ($8D20), at the same depth",
                  ok && at(m, 0x8D20, "longsw.c:13") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-31
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/80cc-longsw", "l31", 0x8CB4, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-31", "80cc master: Over sw's switch (CALL l_case at $8CBC, case 200) stops at the case body longsw.c:13 ($8CE8), at the same depth",
                  ok && at(m, 0x8CE8, "longsw.c:13") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-32
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-longsw", "l32", 0x8CF4, &log);
            const size_t n0 = m.dbg->call_stack().size();
            const bool moved = reach(m, 0x8D20);
            const auto& f = m.dbg->call_stack();
            check("SRCSTEP-32", "sccz80 2.4: after l_case jumps to its case label ($8D20) the Call Stack holds no frame for the helper (the newest is the CALL _sw at $8D81)",
                  ok && moved && n0 > 0 && f.size() == n0 && f.back().caller_pc == 0x8D81,
                  log + "frames " + std::to_string(f.size()) + "/" + std::to_string(n0) + " " + m.where());
        }
        {   // SRCSTEP-33
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-longsw", "l33", 0x8CF4, &log);
            const size_t n0 = m.dbg->call_stack().size();
            const bool moved = reach(m, 0x8D20);
            const auto& f = m.dbg->call_stack();
            check("SRCSTEP-33", "sccz80 master: after l_case jumps to its case label ($8D20) the Call Stack holds no frame for the helper (the newest is the CALL _sw at $8D81)",
                  ok && moved && n0 > 0 && f.size() == n0 && f.back().caller_pc == 0x8D81,
                  log + "frames " + std::to_string(f.size()) + "/" + std::to_string(n0) + " " + m.where());
        }
        {   // SRCSTEP-34
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-longsw", "l34", 0x8D20, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-34", "sccz80 2.4: Out of sw from its case 200 body returns to main at $8D84 (longsw.c:29) one level up",
                  ok && at(m, 0x8D84, "longsw.c:29") && depth(m) == d0 - 1, log + m.where());
        }
        {   // SRCSTEP-35
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-longsw", "l35", 0x8D20, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-35", "sccz80 master: Out of sw from its case 200 body returns to main at $8D84 (longsw.c:28) one level up",
                  ok && at(m, 0x8D84, "longsw.c:28") && depth(m) == d0 - 1, log + m.where());
        }

        // GH #349 (R1''): a __z88dk_callee body keeps its return address ABOVE its CALL's slot (POP r; POP
        // args; PUSH r), so SP is above the slot for the whole body, and its internal JP (r) is not a return.
        // The shipped rule (SP above the slot = consumed) ended those frames: Over became Into and the Call
        // Stack lost the frame. Oracles: test/fixtures/sjasmcallee (callee.asm + the SLD) and test/fixtures/
        // z88dk/{v2.4,master}/sccz80-calleejp (map, listing, binary); libsrc/sound/bit/c/sccz80/
        // bit_beep_raw_callee.asm. The text in [] is what 837a4ee71 did.
        const auto sld_start = [&](Machine& m, uint16_t addr, std::string* log) {
            std::ifstream in(JNEXT_SJASMCALLEE_DIR "/callee.bin", std::ios::binary);
            const std::string bin((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            for (size_t k = 0; k < bin.size(); ++k)
                m.emu.mmu().write(static_cast<uint16_t>(0x8000 + k), static_cast<uint8_t>(bin[k]));
            const auto r = m.dbg->load_source_map(JNEXT_SJASMCALLEE_DIR "/callee.sld", true);
            *log = "labels " + std::to_string(r.count);
            m.set_pc(0x8000);
            return !bin.empty() && r.count > 0 && reach(m, addr);
        };
        {   // SRCSTEP-36
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = sld_start(m, 0x8008, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-36", "hand asm: Over callee.asm:9 (call addone: POP HL; POP DE; PUSH HL; JP (HL) inside) "
                                "stops at callee.asm:10 ($800B) at the same depth [837a4ee71: callee.asm:28 $8029]",
                  ok && at(m, 0x800B, "callee.asm:10") && depth(m) == d0, log + " " + m.where());
        }
        {   // SRCSTEP-37
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = sld_start(m, 0x8011, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-37", "hand asm: Over callee.asm:14 (call addtwo: JP (IX) with the return address at SP+2) "
                                "stops at callee.asm:15 ($8014) at the same depth [837a4ee71: callee.asm:39 $8037]",
                  ok && at(m, 0x8014, "callee.asm:15") && depth(m) == d0, log + " " + m.where());
        }
        {   // SRCSTEP-38
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = sld_start(m, 0x8015, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-38", "hand asm: Over callee.asm:16 (call dispatch: POP HL as an inline table, JP (HL) into "
                                "the caller) stops at case_a, callee.asm:20 ($801E), at the same depth: clause B's "
                                "positive on code with no symbols",
                  ok && at(m, 0x801E, "callee.asm:20") && depth(m) == d0, log + " " + m.where());
        }
        {   // SRCSTEP-39
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = sld_start(m, 0x8008, &log);
            const int d0 = depth(m);
            const bool moved = reach(m, 0x8029);
            const auto& f = m.dbg->call_stack();
            const bool kept = moved && depth(m) == d0 + 1 && !f.empty() && f.back().caller_pc == 0x8008 &&
                              f.back().target_pc == 0x8022;
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-39", "hand asm: at stage2 ($8029, after addone's JP (HL)) the frame CALL $8008 -> $8022 is "
                                "still open (depth d0 + 1); Out returns to callee.asm:10 ($800B) at depth d0 [837a4ee71: "
                                "depth d0 at $8029, and Out ran one instruction]",
                  ok && kept && at(m, 0x800B, "callee.asm:10") && depth(m) == d0, log + " " + m.where());
        }
        {   // SRCSTEP-40
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = sld_start(m, 0x8011, &log);
            const int d0 = depth(m);
            const bool moved = reach(m, 0x8037);
            const auto& f = m.dbg->call_stack();
            const bool kept = moved && depth(m) == d0 + 1 && !f.empty() && f.back().caller_pc == 0x8011 &&
                              f.back().target_pc == 0x802C;
            const bool back = reach(m, 0x8014) && depth(m) == d0;
            check("SRCSTEP-40", "hand asm: at stage3 ($8037, after addtwo's JP (IX)) the frame CALL $8011 -> $802C is "
                                "open; after its RET ($8014) the depth is d0 [837a4ee71: d0 at $8037]",
                  ok && kept && back, log + " " + m.where());
        }
        {   // SRCSTEP-41
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-calleejp", "c41", 0x8C7F, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-41", "sccz80 2.4: Over calleejp.c:9 (addtwo, a __z88dk_callee routine with an internal JP (HL)) stops at calleejp.c:10 ($8C91) at the same depth [837a4ee71: calleejpfn.asm:14 $8CD6]",
                  ok && at(m, 0x8C91, "calleejp.c:10") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-42
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-calleejp", "c42", 0x8C75, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-42", "sccz80 master: Over calleejp.c:8 (addtwo) stops at calleejp.c:9 ($8C80) at the same depth [837a4ee71: calleejpfn.asm:14 $8CC8]",
                  ok && at(m, 0x8C80, "calleejp.c:9") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-43
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-calleejp", "c43", 0x8C7F, &log);
            const size_t n0 = m.dbg->call_stack().size();
            const bool r0 = reach(m, 0x8CD6) && m.dbg->call_stack().size() == n0 + 1 &&
                              m.dbg->call_stack().back().caller_pc == 0x8C87 && m.dbg->call_stack().back().target_pc == 0x8CCD;
            const bool r1 = reach(m, 0x8CDB) && m.dbg->call_stack().size() == n0 + 1 &&
                              m.dbg->call_stack().back().caller_pc == 0x8C87 && m.dbg->call_stack().back().target_pc == 0x8CCD;
            const bool end = reach(m, 0x8C8A) && m.dbg->call_stack().size() == n0;
            check("SRCSTEP-43", "sccz80 2.4: inside addtwo after each internal JP (HL) ($8CD6 stage2, $8CDB stage3) the Call Stack holds the frame CALL $8C87 -> $8CCD above the caller's frames; after its RET ($8C8A) it is gone",
                  ok && r0 && r1 && end, log + "frames n0=" + std::to_string(n0) + " " + m.where());
        }
        {   // SRCSTEP-44
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-calleejp", "c44", 0x8C75, &log);
            const size_t n0 = m.dbg->call_stack().size();
            const bool r0 = reach(m, 0x8CC8) && m.dbg->call_stack().size() == n0 + 1 &&
                              m.dbg->call_stack().back().caller_pc == 0x8C7B && m.dbg->call_stack().back().target_pc == 0x8CBF;
            const bool r1 = reach(m, 0x8CCD) && m.dbg->call_stack().size() == n0 + 1 &&
                              m.dbg->call_stack().back().caller_pc == 0x8C7B && m.dbg->call_stack().back().target_pc == 0x8CBF;
            const bool end = reach(m, 0x8C7E) && m.dbg->call_stack().size() == n0;
            check("SRCSTEP-44", "sccz80 master: inside addtwo after each internal JP (HL) ($8CC8, $8CCD) the Call Stack holds the frame CALL $8C7B -> $8CBF; after its RET ($8C7E) it is gone",
                  ok && r0 && r1 && end, log + "frames n0=" + std::to_string(n0) + " " + m.where());
        }
        {   // SRCSTEP-45
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-calleejp", "c45", 0x8C7F, &log);
            const int d0 = depth(m);
            const bool in = reach(m, 0x8CD6) && depth(m) == d0 + 1;
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-45", "sccz80 2.4: Out from stage2 (calleejpfn.asm:14, after addtwo's JP (HL)) returns to calleejp.c:9 ($8C8A), one level up [837a4ee71: main $8CC0]",
                  ok && in && at(m, 0x8C8A, "calleejp.c:9") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-46
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-calleejp", "c46", 0x8C75, &log);
            const int d0 = depth(m);
            const bool in = reach(m, 0x8CC8) && depth(m) == d0 + 1;
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-46", "sccz80 master: Out from stage2 ($8CC8) returns to calleejp.c:8 ($8C7E), one level up [837a4ee71: main $8CB2]",
                  ok && in && at(m, 0x8C7E, "calleejp.c:8") && depth(m) == d0, log + m.where());
        }
        {   // SRCSTEP-47
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-calleejp", "c47", 0x8C9B, &log);
            const size_t n0 = m.dbg->call_stack().size();
            const bool moved = reach(m, 0x8BD3);
            const auto& f = m.dbg->call_stack();
            check("SRCSTEP-47", "sccz80 2.4: after bit_beep_raw_callee's asm_bit_beep_raw has looped by JP (IX) and reached asm_bit_close ($8BD3), the Call Stack still holds CALL $8CA3 -> $8B8C [837a4ee71: no frame]",
                  ok && moved && f.size() == n0 + 1 && f.back().caller_pc == 0x8CA3 && f.back().target_pc == 0x8B8C,
                  log + "frames " + std::to_string(f.size()) + "/" + std::to_string(n0) + " " + m.where());
        }
        {   // SRCSTEP-48
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "master/sccz80-calleejp", "c48", 0x8C8F, &log);
            const size_t n0 = m.dbg->call_stack().size();
            const bool moved = reach(m, 0x8BCA);
            const auto& f = m.dbg->call_stack();
            check("SRCSTEP-48", "sccz80 master: after the JP (IX) loop, at asm_bit_close ($8BCA), the Call Stack still holds CALL $8C96 -> $8B83 [837a4ee71: no frame]",
                  ok && moved && f.size() == n0 + 1 && f.back().caller_pc == 0x8C96 && f.back().target_pc == 0x8B83,
                  log + "frames " + std::to_string(f.size()) + "/" + std::to_string(n0) + " " + m.where());
        }

        // v2.4 / sccz80 `__banked`: G5 and G9 in one program.
        {   // SRCSTEP-11
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-banked-run", "k11", 0x8CCB, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Into);
            const bool first = at(m, 0xC000, "banked.c:6") && m.dbg->effective_page(0xC000) == 20;
            m.dbg->source_step(m.a, Step::Into);
            const bool second = at(m, 0xC003, "banked.c:8");
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-11", "sccz80 __banked: Into banked_call stops at banked.c:6 (page 20), then "
                                ":8; Out returns to bmain.c:14 at $8CDA (after the defq) at the "
                                "caller's depth [z88dk-gdb C: step]",
                  ok && first && second && at(m, 0x8CDA, "bmain.c:14") && depth(m) == d0, m.where());
        }
        {   // SRCSTEP-12
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string log;
            const bool ok = start(m, "v2.4/sccz80-banked-run", "k12", 0x8CCB, &log);
            const int d0 = depth(m);
            m.dbg->source_step(m.a, Step::Over);
            check("SRCSTEP-12", "sccz80 __banked: Over the banked call lands on bmain.c:15 ($8CE2) at "
                                "the caller's depth",
                  ok && at(m, 0x8CE2, "bmain.c:15") && depth(m) == d0, m.where());
        }

        // Synthetic programs: a source map of `file|line` at addresses, on the page now mapped.
        const auto syn_sld = [&](const char* name, Machine& m,
                                 std::initializer_list<std::pair<const char*, unsigned>> recs) {
            std::string text = std::string("|SLD.data.version|1\n") + "main.asm|1||0" + kNextDevice;
            for (const auto& [pos, addr] : recs) {
                const std::string r = pos;
                const size_t c = r.find(':');
                text += r.substr(0, c) + "|" + r.substr(c + 1) + "||0|" +
                        std::to_string(m.dbg->effective_page(uint16_t(addr))) + "|" +
                        std::to_string(addr) + "|T|\n";
            }
            fs::create_directories(sdir);
            write_text(sdir / name, text);
            return (sdir / name).string();
        };
        const auto poke = [](Machine& m, uint16_t addr, std::initializer_list<uint8_t> b) {
            for (const uint8_t v : b) m.emu.mmu().write(addr++, v);
        };
        {   // SRCSTEP-07
            Machine m;
            poke(m, 0x8000, {0xCD, 0x00, 0x90, 0x00, 0x18, 0xFE});   // CALL f; NOP; JR $
            poke(m, 0x9000, {0x00, 0xC3, 0x00, 0xA0});               // NOP; JP g
            poke(m, 0xA000, {0x00, 0x00, 0xC9});                     // NOP; NOP; RET
            m.dbg->load_source_map(syn_sld("s7.sld", m,
                {{"main.asm:1", 0x8000}, {"main.asm:2", 0x8003}, {"f.asm:1", 0x9000},
                 {"f.asm:2", 0x9001}, {"g.asm:1", 0xA000}, {"g.asm:2", 0xA001}, {"g.asm:3", 0xA002}}),
                false);
            m.set_pc(0x8000);
            m.dbg->source_step(m.a, Step::Into);
            m.dbg->source_step(m.a, Step::Into);
            m.dbg->source_step(m.a, Step::Into);
            const bool in_g = at(m, 0xA000, "g.asm:1");
            m.dbg->source_step(m.a, Step::Out);
            check("SRCSTEP-07", "a tail JP pushes no frame: Out from the jumped-to routine returns to "
                                "f's caller (main.asm:2, $8003) at depth 0",
                  in_g && at(m, 0x8003, "main.asm:2") && depth(m) == 0, m.where());
        }
        {   // SRCSTEP-13
            Machine m;
            m.emu.cpu().on_esxdos_call = [](uint8_t, Z80Registers&) { return true; };
            poke(m, 0x8000, {0xCF, 0x85, 0x00, 0x18, 0xFD});   // RST $08; DEFB $85; NOP; JR $8002
            m.dbg->load_source_map(syn_sld("s13.sld", m,
                {{"main.asm:1", 0x8000}, {"main.asm:2", 0x8002}, {"main.asm:3", 0x8003}}), false);
            m.set_pc(0x8000);
            const std::string log = debugger_log_of([&] { m.dbg->source_step(m.a, Step::Over); });
            check("SRCSTEP-13", "Over a line holding an esxdos RST $08 serviced by the shim stops on "
                                "the next line ($8002) at depth 0, with no runaway warning",
                  at(m, 0x8002, "main.asm:2") && depth(m) == 0 && !has(log, "Source step stopped after"),
                  m.where() + " " + log);
        }
        {   // SRCDBG-CS-10
            const auto build = [&](Machine& m) {
                poke(m, 0x8000, {0x21, 0x01, 0x00, 0xE5, 0xCD, 0x00, 0x90, 0x00, 0x18, 0xFE});
                poke(m, 0x9000, {0xD1, 0xC1, 0xD5, 0xCD, 0x00, 0xA0, 0x00, 0xC9});   // callee convention
                poke(m, 0xA000, {0x00, 0xC9});
                m.dbg->load_source_map(syn_sld("cs10.sld", m,
                    {{"main.asm:1", 0x8000}, {"main.asm:2", 0x8007}, {"f.asm:1", 0x9000},
                     {"f.asm:2", 0x9003}, {"f.asm:3", 0x9006}, {"g.asm:1", 0xA000}}), false);
                m.set_pc(0x8000);
            };
            Machine a;
            build(a);
            a.dbg->source_step(a.a, Step::Over);
            const bool over = at(a, 0x8007, "main.asm:2") && depth(a) == 0;
            Machine b;
            build(b);
            b.dbg->source_step(b.a, Step::Into);
            b.dbg->source_step(b.a, Step::Into);
            const bool in_f2 = at(b, 0x9003, "f.asm:2");
            b.dbg->source_step(b.a, Step::Over);
            check("SRCDBG-CS-10", "a __z88dk_callee routine (its return slot moved UP) keeps its frame "
                                  "while the call it makes returns: Over main.asm:1 lands on :2 at depth "
                                  "0, Over inside f lands on f.asm:3 at depth 1",
                  over && in_f2 && at(b, 0x9006, "f.asm:3") && depth(b) == 1,
                  a.where() + " | " + b.where() + " depth " + std::to_string(depth(b)));
        }
    }

    // ── GH #375 stage 3: z88dk map symbols take their pages ─────────────────────
    // Oracle: the fixtures' NEX placement (prog_PAGE_20.bin and prog_BANK_10.bin at NEX
    // bank 10 +0 = page 20, prog_CODE.bin at bank 2 +0 = page 4), design 9.4 rules 1-4
    // and 10.4 (one derivation, one binding function), #344 D17 for tapes.
    {
        const fs::path sdir = dir / "sym375";
        const auto nr = [](Machine& m, uint8_t reg, uint8_t v) { m.dbg->nextreg_write(m.a, reg, v); };
        const auto lookup_at = [](Machine& m, uint16_t a) {
            return m.dbg->lookup(a).value_or("-");
        };
        const auto key_of = [](Machine& m, const char* n) { return m.dbg->symbols().find_name(n); };
        const auto in_space = [&](Machine& m, const char* n, uint16_t addr, const MemSpace& sp) {
            const auto k = key_of(m, n);
            return k && k->addr == addr && k->space && *k->space == sp;
        };
        {   // SRCDBG-SYM-18: the issue row
            const fs::path d = sdir / "s18";
            z88_copy("v2.4/sccz80-page20", d);
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const auto r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            nr(m, 0x56, 20);
            const bool p20 = m.dbg->source_page(0xC000) == MemSpace::page(20);
            const std::string at20 = lookup_at(m, 0xC000);
            nr(m, 0x56, 21);
            const std::string at21 = lookup_at(m, 0xC000);
            nr(m, 0x56, 0);
            const std::string at0 = lookup_at(m, 0xC000);
            const bool p4 = m.dbg->source_page(0x8C62) == MemSpace::page(4);
            const std::string main4 = lookup_at(m, 0x8C62);
            nr(m, 0x54, 40);
            const std::string main40 = lookup_at(m, 0x8C62);
            check("SRCDBG-SYM-18", "a banked z88dk label names its address only while its page is mapped, and a "
                                   "code symbol only in the RAM page the program loaded it in",
                  r.symbols == 10 && p20 && at20 == "banked_add" && at21 == "-" && at0 == "-" && p4 &&
                      main4 == "main" && main40 == "-",
                  at20 + "/" + at21 + "/" + at0 + " " + main4 + "/" + main40);
        }
        {   // SRCDBG-SYM-19
            const fs::path d = sdir / "s19";
            z88_copy("v2.4/sccz80-page20", d);
            const fs::path alone = sdir / "s19alone";
            fs::create_directories(alone);
            fs::copy_file(d / "prog.nex", alone / "game.nex", fs::copy_options::overwrite_existing);
            Machine m(false, MachineType::ZXN_ISSUE2);
            nr(m, 0x54, 40);
            m.dbg->load_map((d / "prog.map").string(), jnext::dbg::MapFormat::Z88dk);
            const bool in40 = lookup_at(m, 0x8C62) == "main";
            nr(m, 0x54, 4);
            const bool gone4 = lookup_at(m, 0x8C62) == "-";
            z88_load(m, alone / "game.nex");
            const bool premise = m.dbg->source_page(0x8C62) == MemSpace::page(4);
            const auto r = m.dbg->load_program_sidecars((alone / "game.nex").string());
            const bool rebound = lookup_at(m, 0x8C62) == "main" && in_space(m, "_main", 0x8C62, MemSpace::page(4)) &&
                                 key_of(m, "_main")->bind_at_load;
            nr(m, 0x54, 40);
            const bool gone40 = lookup_at(m, 0x8C62) == "-";
            nr(m, 0x56, 20);
            const bool b20 = lookup_at(m, 0xC000) == "banked_add";
            nr(m, 0x56, 0);
            const bool b0 = lookup_at(m, 0xC000) == "-";
            check("SRCDBG-SYM-19", "a map loaded before its program is bound when loaded and again at the program load "
                                   "(the user's store is kept)",
                  in40 && gone4 && premise && r.symbols == -1 && rebound && gone40 && b20 && b0);
        }
        {   // SRCDBG-SYM-20
            const fs::path d = sdir / "s20";
            const std::string tap = tape_copy("v2.4/sccz80-tape", d);
            Machine m(false, MachineType::ZXN_ISSUE2);
            const bool premise = m.dbg->source_page(0x8CA1) == MemSpace::page(4);
            const auto r = m.dbg->load_program_sidecars(tap);
            const bool live = lookup_at(m, 0x8CA1) == "main" && in_space(m, "_main", 0x8CA1, MemSpace::page(4));
            nr(m, 0x54, 40);
            const bool gone40 = lookup_at(m, 0x8CA1) == "-";
            const fs::path d2 = sdir / "s20b";
            const std::string tap2 = tape_copy("v2.4/sccz80-tape", d2, /*bins=*/false);
            Machine n(false, MachineType::ZXN_ISSUE2);
            const auto r2 = n.dbg->load_program_sidecars(tap2);
            check("SRCDBG-SYM-20", "beside a tape the map's symbols are bound at the attach and live while the check "
                                   "is pending; a map with no identity binds the same way",
                  premise && r.sources_pending && r.symbols == 19 && m.dbg->source_map_status().pending && live &&
                      gone40 && !r2.sources_pending && in_space(n, "_main", 0x8CA1, MemSpace::page(4)));
        }
        {   // SRCDBG-SYM-21
            const fs::path d = sdir / "s21";
            tape_copy("v2.4/sccz80-tape", d);
            const fs::path alone = sdir / "s21alone";
            fs::create_directories(alone);
            write_text(alone / "game.tap", "");
            Machine m(false, MachineType::ZXN_ISSUE2);
            nr(m, 0x54, 40);
            m.dbg->load_map((d / "prog.map").string(), jnext::dbg::MapFormat::Z88dk);
            const bool in40 = in_space(m, "_main", 0x8CA1, MemSpace::page(40));
            nr(m, 0x54, 4);
            m.dbg->load_program_sidecars((alone / "game.tap").string());
            const bool in4 = in_space(m, "_main", 0x8CA1, MemSpace::page(4)) && lookup_at(m, 0x8CA1) == "main";
            nr(m, 0x54, 40);
            check("SRCDBG-SYM-21", "the user's map is rebound at a tape load",
                  in40 && in4 && lookup_at(m, 0x8CA1) == "-");
        }
        {   // SRCDBG-SYM-22
            const fs::path d = sdir / "s22";
            z88_copy("master/sdcc-banked-run", d);
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            Debugger::SidecarLoad r;
            const std::string log = debugger_log_of([&] {
                r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            });
            const auto run_to = [&](uint16_t addr) {
                const auto id = m.dbg->subscribe(m.a, execute_stop(addr)).value;
                m.dbg->run(m.a);
                for (int i = 0; i < 100 && !m.dbg->state().paused; ++i) m.emu.run_frame();
                m.dbg->unsubscribe(m.a, id);
                return m.dbg->state().paused && m.pc() == addr;
            };
            const bool s1 = run_to(0x8CAE);
            const MemSpace before = m.dbg->source_page(0xC000);
            const bool premise1 = !(before == MemSpace::page(20)) && m.dbg->source_page(0x8CAE) == MemSpace::page(4);
            const bool main_named = lookup_at(m, 0x8CAE) == "main" && lookup_at(m, 0xC000) == "-";
            const bool s2 = run_to(0xC000);
            const bool in20 = m.dbg->source_page(0xC000) == MemSpace::page(20) &&
                              lookup_at(m, 0xC000) == "banked_add" && m.dbg->lookup_name("_banked_add") == 0xC000;
            const bool s3 = run_to(0x8CBE);
            const bool out = m.dbg->source_page(0xC000) == before && lookup_at(m, 0xC000) == "-";
            check("SRCDBG-SYM-22", "the program pages its own banked symbol in and out: banked_add (_banked_add, a 24-bit "
                                   "$14C000 in the map) names $C000 only while banked_call has page 20 mapped",
                  r.symbols == 9 && has(log, "(program identity verified)") && s1 && premise1 && main_named && s2 &&
                      in20 && s3 && out,
                  log);
        }
        {   // SRCDBG-CN-01: GH #424, a C symbol under its C name through the real attach
            // Oracle: the fixture's own bytes (`32 59 8F` at $8C9A is ld (_counter),a, main.c.lis:624;
            // `21 59 8F` at $8D65 is ld hl,_counter) and zcc's MODULE naming (main_c).
            const fs::path d = sdir / "cn1";
            z88_copy("v2.4/sccz80-debug", d);
            Machine m(false, MachineType::ZXN_ISSUE2);
            z88_load(m, d / "prog.nex");
            const auto r = m.dbg->load_program_sidecars((d / "prog.nex").string());
            const auto mnemonic = [&](uint16_t addr) {
                return std::string(m.dbg->disassemble(addr, 1, &m.dbg->symbols())[0].mnemonic);
            };
            const auto k = m.dbg->symbols().resolve_key("counter");
            check("SRCDBG-CN-01", "after the attach of a z88dk program, `counter` names `_counter`'s address and page, "
                                  "lookup() shows counter, and disassemble() prints the C name",
                  r.symbols == 19 && m.dbg->lookup_name("counter") == std::optional<uint16_t>(0x8F59) &&
                      k && k->space && *k->space == MemSpace::page(4) && lookup_at(m, 0x8F59) == "counter" &&
                      mnemonic(0x8C9A) == "LD (counter),A" && mnemonic(0x8D65) == "LD HL,counter",
                  mnemonic(0x8C9A) + " | " + mnemonic(0x8D65));
        }
    }

    // ── GH #351 (E23-21): z88dk dot commands, which NextZXOS runs from DivMMC RAM bank 2 ──
    // Oracle: the committed dot / dotx fixtures (zcc +zxn -subtype=dot|dotx, v2.4 sccz80:
    // `_main` $2D5F, dot.c:8 $2D4F four times, dot.c:14 $2D62, dot.c:15 $2D6A, dot.c:17 $2D82,
    // add3's first line dot.c:7 $2D4C, dot.c:18 $2D95) and the measured NextZXOS behaviour
    // (fixtures README): the file is copied into DivMMC bank 2, E3 <- $82, the CPU runs $2000.
    // The machine is a Next with the fixture's CODE group placed in bank 2 the way NextZXOS
    // does it, the stack holding a return address to a parked loop.
    {
        const MemSpace D2 = MemSpace::divmmc_ram(2);
        const fs::path root = dir / "dot";
        constexpr uint16_t kMain = 0x2D5F;
        constexpr uint16_t kEntry = 0x2000;
        z88_copy("v2.4/sccz80-dot", root / "a");
        z88_copy("v2.4/sccz80-dotx", root / "x");
        z88_copy("v2.4/sccz80-debug", root / "nex");
        const std::string dot_map = (root / "a" / "prog.map").string();
        const std::string dotx_map = (root / "x" / "prog.map").string();
        const std::vector<uint8_t> dot_code = read_all(root / "a" / "prog_CODE.bin");
        const std::vector<uint8_t> x_code = read_all(root / "x" / "prog_CODE.bin");
        const std::vector<uint8_t> x_main = read_all(root / "x" / "prog_MAIN.bin");
        const auto put_dot = [](Machine& m, const std::vector<uint8_t>& code, long flip = -1) {
            uint8_t* bank2 = m.emu.divmmc().ram_data() + 2 * 0x2000;
            std::copy(code.begin(), code.end(), bank2);
            if (flip >= 0) bank2[flip] ^= 0xFF;
        };
        const auto enter_dot_bank = [](Machine& m) { m.dbg->port_out(m.a, 0xE3, 0x82); };
        // PC at `pc`, with a return address to the parked loop at $6000 on the stack.
        const auto start_at = [](Machine& m, uint16_t pc) {
            park(m);
            m.emu.mmu().write(0xFEFE, 0x00);
            m.emu.mmu().write(0xFEFF, 0x60);
            Z80Registers r = m.emu.cpu().get_registers();
            r.SP = 0xFEFE;
            m.emu.cpu().set_registers(r);
            m.set_pc(pc);
        };
        // A Next machine running the dot command `code` from bank 2 with `map` attached.
        const auto dot_machine = [&](const std::vector<uint8_t>& code, const std::string& map, Machine& m,
                                     long flip = -1) {
            put_dot(m, code, flip);
            enter_dot_bank(m);
            start_at(m, 0x6000);   // parked while the map is loaded: a hand load made inside the command judges like a landing there
            const auto r = m.dbg->load_source_map(map, false);
            m.set_pc(kMain);
            return r;
        };
        // Run until the machine stops, at most `limit` times; the PCs it stopped at.
        const auto stops_of = [](Machine& m, int limit = 10) {
            std::vector<uint16_t> pcs;
            for (int i = 0; i < limit; ++i) {
                m.dbg->run(m.a);
                for (int f = 0; f < 4 && !m.dbg->state().paused; ++f) m.emu.run_frame();
                if (!m.dbg->state().paused) break;
                pcs.push_back(m.pc());
            }
            return pcs;
        };
        const auto name_of = [&](Machine& m) {
            const auto* l = m.dbg->source_location();
            return l ? l->file + ":" + std::to_string(l->line) : std::string("-");
        };
        const auto verified_line = [&](const std::string& map, const char* tail) {
            return "Verified z88dk source map '" + map + "': " + tail;
        };
        const std::string not_the_one = "The dot command started in DivMMC RAM bank 2 is not the one source map '" +
                                        dot_map + "' describes; it stays pending";
        const std::string verified_all = verified_line(dot_map, "the dot command in DivMMC RAM bank 2 matches its binary identity");

        {   // SRCDOT-01
            Machine m(false, MachineType::ZXN_ISSUE2);
            const auto r = dot_machine(dot_code, dot_map, m);
            const bool premise = m.dbg->source_page(0x2D4F) == D2 && m.dbg->effective_page(0x2D4F) != 0xFF;
            const auto g = m.dbg->subscribe_source_line(m.a, "dot.c:8", execute_stop(0));
            const auto stops = stops_of(m);
            bool at_line = true;
            Machine n(false, MachineType::ZXN_ISSUE2);
            dot_machine(dot_code, dot_map, n);
            n.dbg->subscribe_source_line(n.a, "dot.c:8", execute_stop(0));
            n.dbg->run(n.a);
            for (int f = 0; f < 4 && !n.dbg->state().paused; ++f) n.emu.run_frame();
            at_line = n.dbg->state().paused && n.pc() == 0x2D4F && name_of(n) == "dot.c:8" &&
                      n.dbg->source_page(n.pc()) == D2;
            check("SRCDOT-01", "a dot command's map attaches (pending) with the ROM at $2000 and a source breakpoint on dot.c:8 stops "
                               "the command running from DivMMC bank 2 at $2D4F, four times in all, reporting dot.c:8",
                  r.count > 0 && r.pending && premise && static_cast<bool>(g) && at_line &&
                      stops == std::vector<uint16_t>(4, 0x2D4F),
                  "count=" + std::to_string(r.count) + " stops=" + std::to_string(stops.size()) + " " + n.where());
        }
        {   // SRCDOT-02
            Machine m(false, MachineType::ZXN_ISSUE2);
            dot_machine(dot_code, dot_map, m);
            m.dbg->subscribe_source_line(m.a, "dot.c:14", execute_stop(0));
            stops_of(m, 1);
            const uint16_t at14 = m.pc();
            const std::string l14 = name_of(m);
            m.dbg->source_step(m.a, Step::Into);
            const uint16_t at15 = m.pc();
            const std::string l15 = name_of(m);
            const bool in15 = m.dbg->source_page(at15) == D2;
            m.dbg->subscribe_source_line(m.a, "dot.c:17", execute_stop(0));
            stops_of(m, 1);
            const uint16_t at17 = m.pc();
            const std::string l17 = name_of(m);
            m.dbg->source_step(m.a, Step::Into);
            const uint16_t in_add3 = m.pc();
            const std::string l7 = name_of(m);
            m.dbg->source_step(m.a, Step::Out);
            const std::string back = name_of(m);
            m.dbg->source_step(m.a, Step::Over);
            const uint16_t next = m.pc();
            const std::string l18 = name_of(m);
            check("SRCDOT-02", "source steps follow inside the dot command: Into dot.c:14 -> dot.c:15 ($2D6A); Into the add3() call line "
                               "dot.c:17 ($2D82) -> add3's dot.c:7 ($2D4C); Out -> back on dot.c:17; Over (the loop returns to its test) -> dot.c:15 ($2D6A-$2D81); every landing in DivMMC RAM bank 2",
                  at14 == 0x2D62 && l14 == "dot.c:14" && at15 == 0x2D6A && l15 == "dot.c:15" && in15 && at17 == 0x2D82 &&
                      l17 == "dot.c:17" && in_add3 == 0x2D4C && l7 == "dot.c:7" && back == "dot.c:17" && l18 == "dot.c:15" &&
                      next >= 0x2D6A && next < 0x2D82 && m.dbg->source_page(next) == D2,
                  l14 + " " + l15 + " " + l17 + " " + l7 + " " + back + " " + l18);
        }
        {   // SRCDOT-03
            // The same 8K in ordinary RAM (page 40 in MMU1, E3 off) and the control with E3 = $82.
            const auto ram_machine = [&](Machine& m) {
                m.dbg->poke(m.a, MemSpace::page(40), 0, dot_code.size(), dot_code.data());
                m.dbg->nextreg_write(m.a, 0x51, 40);
                m.dbg->port_out(m.a, 0xE3, 0x00);
                start_at(m, kMain);
                m.dbg->load_source_map(dot_map, false);
            };
            Machine r(false, MachineType::ZXN_ISSUE2);
            ram_machine(r);
            const bool premise = r.dbg->source_page(0x2D62) == MemSpace::page(40);
            r.dbg->subscribe_source_line(r.a, "dot.c:8", execute_stop(0));
            const auto stops = stops_of(r, 3);
            r.dbg->pause(r.a);
            const bool ran_through = stops.empty() && r.pc() == 0x6000;
            // How often the code ran $2D4F: single steps over the same RAM.
            Machine c(false, MachineType::ZXN_ISSUE2);
            ram_machine(c);
            int passes = 0;
            for (int i = 0; i < 4000 && c.pc() != 0x6000; ++i) {
                if (c.pc() == 0x2D4F) ++passes;
                c.dbg->step_into(c.a);
            }
            const bool no_loc = !c.dbg->source_location(0x2D62) && !r.dbg->source_location(0x2D4F);
            // Under the ROM.
            Machine o(false, MachineType::ZXN_ISSUE2);
            o.dbg->load_source_map(dot_map, false);
            const bool rom_none = !o.dbg->source_location(0x2D62) && o.dbg->source_page(0x2D62).kind == MemSpace::Kind::Rom;
            // The control: E3 = $82 stops.
            Machine d(false, MachineType::ZXN_ISSUE2);
            dot_machine(dot_code, dot_map, d);
            d.dbg->subscribe_source_line(d.a, "dot.c:8", execute_stop(0));
            const auto d_stops = stops_of(d, 2);
            check("SRCDOT-03", "the same bytes in ordinary RAM (page 40 at $2000, E3 off) or ROM at $2D62 match nothing: a dot.c:8 breakpoint never stops "
                               "while the code runs $2D4F four times; the control with DivMMC bank 2 stops",
                  premise && ran_through && passes == 4 && no_loc && rom_none && !d_stops.empty() && d_stops[0] == 0x2D4F,
                  "premise=" + std::to_string(premise) + " stops=" + std::to_string(stops.size()) + " passes=" + std::to_string(passes));
        }
        {   // SRCDOT-04
            const auto at_entry = [&](Machine& m, std::string& log, Debugger::SourceMapStatus& st) {
                enter_dot_bank(m);
                m.set_pc(kEntry);
                log = debugger_log_of([&] {
                    m.dbg->step_into(m.a);
                    st = m.dbg->source_map_status();
                });
            };
            Machine good(false, MachineType::ZXN_ISSUE2);
            dot_machine(dot_code, dot_map, good);
            const bool was_pending = good.dbg->source_map_status().pending;
            std::string log_good;
            Debugger::SourceMapStatus st_good;
            at_entry(good, log_good, st_good);
            // One byte of dot.c:14's code changed: another dot command is in the bank.
            Machine bad(false, MachineType::ZXN_ISSUE2);
            dot_machine(dot_code, dot_map, bad, 0xD62);
            std::string log1, log2, log3;
            Debugger::SourceMapStatus st1, st2, st3;
            at_entry(bad, log1, st1);
            at_entry(bad, log2, st2);
            bad.emu.divmmc().ram_data()[2 * 0x2000 + 0xD62] = dot_code[0xD62];   // the real command arrives
            at_entry(bad, log3, st3);
            check("SRCDOT-04", "judged at the entry: $2000 fetched from bank 2 with matching bytes verifies the map (and tells so); with one changed byte "
                               "the map stays attached and pending, the 'not the one' line is told once, nothing is dropped; the right bytes at the next entry verify it",
                  was_pending && !st_good.pending && has(log_good, verified_all) &&
                      st1.state == Debugger::SourceMapStatus::State::Attached && st1.pending && has(log1, not_the_one) &&
                      st2.pending && !has(log2, not_the_one) && !has(log1 + log2, "Rejected") && !has(log1 + log2, "Dropped") &&
                      bad.dbg->source_map().size() > 0 && !st3.pending && has(log3, verified_all),
                  log_good + "|" + log1 + "|" + log2 + "|" + log3);
        }
        {   // SRCDOT-05
            Machine m(false, MachineType::ZXN_ISSUE2);
            dot_machine(dot_code, dot_map, m);
            std::string log;
            Debugger::SourceMapStatus st;
            log = debugger_log_of([&] {
                const auto stops = stops_of(m, 1);   // runs _main to the parked loop, never fetching $2000
                (void)stops;
                m.dbg->pause(m.a);
                st = m.dbg->source_map_status();
            });
            check("SRCDOT-05", "the code runs from _main without ever fetching $2000: the map is still pending (the entry is the judgement point, not the first run of a span)",
                  m.pc() == 0x6000 && st.state == Debugger::SourceMapStatus::State::Attached && st.pending &&
                      !has(log, "Verified"),
                  log);
        }
        {   // SRCDOT-06
            Machine m(false, MachineType::ZXN_ISSUE2);
            put_dot(m, dot_code, 0xD62);   // not the command: hand loading does not compare
            enter_dot_bank(m);
            start_at(m, kMain);
            const auto r = m.dbg->load_source_map(dot_map, /*accept_identity_mismatch=*/false, /*verify_at_first_execution=*/false);
            const size_t size = m.dbg->source_map().size();
            const auto nex = m.dbg->load_source_map((root / "nex" / "prog.map").string(), false, false, /*dot_only=*/true);
            const auto dot_again = m.dbg->load_source_map(dot_map, false, false, /*dot_only=*/true);
            check("SRCDOT-06", "a hand load of a dot command's map attaches pending even with different bytes in bank 2 (no mismatch, no refusal); with no program "
                               "(dot_only) a NEX map is refused as not a dot command and the attached map is kept, while a dot map is accepted",
                  r.count > 0 && r.pending && r.error.empty() && !r.identity.has_value() &&
                      has(r.identity_note, "the dot command loads when it runs") && nex.count == -1 && nex.not_dot &&
                      has(nex.error, "dot command") && m.dbg->source_map().size() == size && dot_again.count > 0 &&
                      dot_again.pending && !dot_again.not_dot,
                  r.error + " | " + nex.error);
        }
        {   // SRCDOT-07
            Machine m(false, MachineType::ZXN_ISSUE2);
            dot_machine(dot_code, dot_map, m);
            const auto verify_entry = [&] {
                put_dot(m, dot_code);
                enter_dot_bank(m);
                m.set_pc(kEntry);
                m.dbg->step_into(m.a);
                return m.dbg->source_map_status();
            };
            m.dbg->reset(m.a, ResetKind::Soft);
            const std::string log_a = debugger_log_of([&] { (void)m.dbg->source_map_status(); });
            const auto after_first = m.dbg->source_map_status();
            const auto v1 = verify_entry();
            m.dbg->reset(m.a, ResetKind::Soft);
            std::string log_b;
            Debugger::SourceMapStatus after_second;
            log_b = debugger_log_of([&] { after_second = m.dbg->source_map_status(); });
            const auto v2 = verify_entry();
            check("SRCDOT-07", "a soft reset neither drops nor stales a dot map: a pending one stays attached and pending (a tape's would be judged and dropped), "
                               "a verified one goes back to pending, and the next matching entry verifies it again",
                  after_first.state == Debugger::SourceMapStatus::State::Attached && after_first.pending && !has(log_a, "Dropped") &&
                      !v1.pending && after_second.state == Debugger::SourceMapStatus::State::Attached && after_second.pending &&
                      !has(log_b, "stale") && !has(log_b, "Dropped") && !v2.pending && m.dbg->source_map().size() > 0,
                  log_a + "|" + log_b);
        }
        {   // SRCDOT-08
            const auto x_machine = [&](Machine& m, long flip_main) {
                dot_machine(x_code, dotx_map, m);
                tape_bytes(m, MemSpace::page(4), root / "x" / "prog_MAIN.bin", flip_main, 0);
            };
            const std::string x_dot_line = "Verified z88dk source map '" + dotx_map + "' in DivMMC RAM bank 2; 1 memory space still to check";
            const std::string x_all = verified_line(dotx_map, "the dot command in DivMMC RAM bank 2 matches its binary identity");
            // a) the entry matches, then the main part runs
            Machine a(false, MachineType::ZXN_ISSUE2);
            x_machine(a, -1);
            const bool premise = a.dbg->source_page(0x87EB) == MemSpace::page(4);
            Debugger::SourceMapStatus s1, s2;
            const std::string la = debugger_log_of([&] {
                enter_dot_bank(a);
                a.set_pc(kEntry);
                a.dbg->step_into(a.a);
                s1 = a.dbg->source_map_status();
                a.set_pc(0x87EB);
                a.dbg->step_into(a.a);
                s2 = a.dbg->source_map_status();
            });
            // b) a changed byte of the main part, after the entry matched: dropped, as a tape
            Machine b(false, MachineType::ZXN_ISSUE2);
            x_machine(b, 0x7EB);
            Debugger::SourceMapStatus s3;
            const std::string lb = debugger_log_of([&] {
                enter_dot_bank(b);
                b.set_pc(kEntry);
                b.dbg->step_into(b.a);
                b.set_pc(0x87EB);
                b.dbg->step_into(b.a);
                s3 = b.dbg->source_map_status();
            });
            // c) the main part runs before any entry: nothing is judged
            Machine c(false, MachineType::ZXN_ISSUE2);
            x_machine(c, 0x7EB);
            Debugger::SourceMapStatus s4;
            const std::string lc = debugger_log_of([&] {
                c.set_pc(0x87EB);
                c.dbg->step_into(c.a);
                s4 = c.dbg->source_map_status();
            });
            check("SRCDOT-08", "dotx: a matching entry verifies bank 2 and arms page 4 (still pending, one space to check); the main part's first run with matching "
                               "bytes verifies the map; with one changed byte it is dropped as a tape's; before any entry the main part's run judges nothing",
                  premise && s1.pending && has(la, x_dot_line) && !s2.pending && has(la, x_all) &&
                      s3.state == Debugger::SourceMapStatus::State::None && has(lb, "Rejected z88dk source map") &&
                      s4.state == Debugger::SourceMapStatus::State::Attached && s4.pending && !has(lc, "Rejected") && !has(lc, "Verified"),
                  la + "|" + lb + "|" + lc);
        }
        {   // SRCDOT-09
            Machine m(true, MachineType::ZXN_ISSUE2);
            dot_machine(dot_code, dot_map, m);
            m.emu.divmmc().ram_data()[2 * 0x2000 + 0x1F00] = 0x18;   // a loop inside the dot's bank, past its code
            m.emu.divmmc().ram_data()[2 * 0x2000 + 0x1F01] = 0xFE;
            run_frames(m, 2);   // _main returns to the loop at $6000 in the first
            const uint32_t before_entry = m.dbg->time().frame;
            m.set_pc(kEntry);
            m.dbg->step_into(m.a);
            m.set_pc(0x3F00);
            run_frames(m, 2);
            const uint32_t after_entry = m.dbg->time().frame;
            const auto in_dot_now = m.dbg->source_map_status();
            const Result back = m.dbg->rewind_to_frame(m.a, before_entry);
            const auto s_back = m.dbg->source_map_status();
            const Result fwd = m.dbg->rewind_to_frame(m.a, after_entry);
            const auto s_fwd = m.dbg->source_map_status();
            check("SRCDOT-09", "a rewind to before the entry puts the dot map back to pending, still attached; coming forward to a frame where the CPU is inside the "
                               "command (bytes matching) verifies it again",
                  !in_dot_now.pending && back == Result::Ok && s_back.state == Debugger::SourceMapStatus::State::Attached &&
                      s_back.pending && fwd == Result::Ok && !s_fwd.pending && m.dbg->source_map().size() > 0,
                  "back=" + std::to_string(int(back)) + " fwd=" + std::to_string(int(fwd)) + " pend=" +
                      std::to_string(s_back.pending) + std::to_string(s_fwd.pending));
        }
        {   // SRCDOT-10 (review R1): verified, then another dot command, then the right one again
            Machine m(false, MachineType::ZXN_ISSUE2);
            dot_machine(dot_code, dot_map, m);
            const auto entry = [&](std::string& log) {
                enter_dot_bank(m);
                m.set_pc(kEntry);
                Debugger::SourceMapStatus st;
                log = debugger_log_of([&] { m.dbg->step_into(m.a); st = m.dbg->source_map_status(); });
                return st;
            };
            std::string l1, l2, l3, l4;
            const auto s1 = entry(l1);                                   // verified
            m.emu.divmmc().ram_data()[2 * 0x2000 + 0xD62] ^= 0xFF;       // another command
            const auto s2 = entry(l2);
            const auto s3 = entry(l3);                                   // told once
            m.emu.divmmc().ram_data()[2 * 0x2000 + 0xD62] ^= 0xFF;       // ours again
            const auto s4 = entry(l4);
            // dotx: after the foreign entry the main part is disarmed again.
            Machine x(false, MachineType::ZXN_ISSUE2);
            dot_machine(x_code, dotx_map, x);
            tape_bytes(x, MemSpace::page(4), root / "x" / "prog_MAIN.bin", 0x7EB, 0);   // a wrong main part
            const auto first_span = x.dbg->source_map().verify_spans();
            size_t code_lo = 0x2000;
            for (const auto& sp : first_span) if (sp.unit == D2) { code_lo = sp.lo; break; }
            const auto x_entry = [&](std::string& log) {
                enter_dot_bank(x);
                x.set_pc(kEntry);
                log = debugger_log_of([&] { x.dbg->step_into(x.a); (void)x.dbg->source_map_status(); });
            };
            const auto x_main = [&](std::string& log) {
                x.set_pc(0x87EB);
                Debugger::SourceMapStatus st;
                log = debugger_log_of([&] { x.dbg->step_into(x.a); st = x.dbg->source_map_status(); });
                return st;
            };
            std::string xa, xb, xc, xd;
            x_entry(xa);                                                 // armed
            x.emu.divmmc().ram_data()[2 * 0x2000 + (code_lo - 0x2000)] ^= 0xFF;
            x_entry(xb);                                                 // foreign: disarmed
            const auto xs = x_main(xc);                                  // wrong main part: judges nothing
            x.emu.divmmc().ram_data()[2 * 0x2000 + (code_lo - 0x2000)] ^= 0xFF;
            x_entry(xd);                                                 // ours: armed again
            const auto xe = [&] { std::string l; const auto st = x_main(l); return std::make_pair(st, l); }();
            check("SRCDOT-10", "a verified dot map meets another dot command's entry: it goes back to pending, the 'not the one' line is told once, nothing is "
                               "dropped, and the right bytes at the next entry verify it again; a dotx's main part is disarmed by the foreign entry (a wrong "
                               "main part run then drops nothing) and armed again by the right entry (it is then dropped)",
                  !s1.pending && has(l1, verified_all) && s2.pending && s2.state == Debugger::SourceMapStatus::State::Attached &&
                      has(l2, not_the_one) && s3.pending && !has(l3, not_the_one) && !has(l2 + l3, "Dropped") && !has(l2 + l3, "Rejected") &&
                      m.dbg->source_map().size() > 0 && !s4.pending && has(l4, verified_all) &&
                      xs.state == Debugger::SourceMapStatus::State::Attached && xs.pending && !has(xc, "Rejected") &&
                      xe.first.state == Debugger::SourceMapStatus::State::None && has(xe.second, "Rejected z88dk source map"),
                  l1 + "|" + l2 + "|" + l3 + "|" + l4);
        }
        {   // SRCDOT-11 (review R2): $2000 run from somewhere other than bank 2 judges nothing
            const auto at_2000 = [&](uint8_t e3, long flip, std::string& log) {
                Machine m(false, MachineType::ZXN_ISSUE2);
                dot_machine(dot_code, dot_map, m, flip);
                m.dbg->port_out(m.a, 0xE3, e3);
                m.set_pc(kEntry);
                log = debugger_log_of([&] { m.dbg->step_into(m.a); });
                const bool premise = e3 == 0 ? m.dbg->source_page(kEntry).kind == MemSpace::Kind::Rom
                                             : m.dbg->source_page(kEntry) == MemSpace::divmmc_ram(0);
                return std::make_pair(m.dbg->source_map_status(), premise);
            };
            std::string la, lb, lc, ld;
            const auto rom   = at_2000(0x00, -1, la);    // ROM at $2000, bank 2 holds the command
            const auto bank0 = at_2000(0x80, -1, lb);    // DivMMC bank 0 (NextZXOS's hook), same
            const auto rom_x = at_2000(0x00, 0xD62, lc); // ROM, bank 2 holds another command
            const auto bk0_x = at_2000(0x80, 0xD62, ld);
            const std::string all = la + lb + lc + ld;
            check("SRCDOT-11", "$2000 run from ROM (E3 = 0) or from DivMMC bank 0 (E3 = $80) with bank 2 holding the command judges nothing: the map stays pending, "
                               "no Verified line, and no 'not the one' line even when bank 2 holds another command",
                  rom.second && bank0.second && rom_x.second && bk0_x.second && rom.first.pending && bank0.first.pending &&
                      rom_x.first.pending && bk0_x.first.pending && !has(all, "Verified") && !has(all, not_the_one) &&
                      rom.first.state == Debugger::SourceMapStatus::State::Attached,
                  all);
        }
        {   // SRCDOT-12 (review R5): after a ring landing the status is what the original run showed at that instant
            struct Sample { uint64_t clock; bool pending; bool attached; };
            const auto trial = [&](const std::vector<uint8_t>& code, const std::string& map, bool main_part,
                                   int& compared, bool& saw_true, bool& saw_false, std::string& note) {
                Machine m(true, MachineType::ZXN_ISSUE2);
                dot_machine(code, map, m);
                if (main_part) tape_bytes(m, MemSpace::page(4), root / "x" / "prog_MAIN.bin", -1, 0);
                const uint8_t loop[2] = {0x18, 0xFE};
                m.dbg->poke(m.a, MemSpace::page(4), 0x1F00, 2, loop);          // a loop at $9F00 in page 4
                m.emu.divmmc().ram_data()[2 * 0x2000 + 0x1F00] = 0x18;        // and one at $3F00 in bank 2
                m.emu.divmmc().ram_data()[2 * 0x2000 + 0x1F01] = 0xFE;
                std::vector<Sample> log;
                std::vector<uint32_t> frames;
                const auto sample = [&] {   // the status of the original run now
                    const auto st = m.dbg->source_map_status();
                    log.push_back({m.emu.clock().get(), st.pending, st.state == Debugger::SourceMapStatus::State::Attached});
                };
                const auto frame = [&] {
                    sample();
                    frames.push_back(m.dbg->time().frame);
                    run_frames(m, 1);
                };
                m.set_pc(0x6000);
                frame(); frame();                                              // parked: pending
                m.set_pc(kEntry);
                m.dbg->step_into(m.a);                                         // the entry
                sample();
                if (main_part) {
                    m.set_pc(0x87EB);
                    m.dbg->step_into(m.a);                                     // the main part's first run
                    sample();
                    m.set_pc(0x9F00);                                          // running in page 4
                } else {
                    m.set_pc(0x9F00);                                          // a plain dot calls out of its bank
                }
                frame(); frame();
                if (!main_part) { m.set_pc(0x3F00); frame(); frame(); }       // and is back inside it
                frame();
                for (size_t i = 0; i < frames.size(); ++i) {
                    if (m.dbg->rewind_to_frame(m.a, frames[i]) != Result::Ok) { note += " rewind-failed"; continue; }
                    const auto st = m.dbg->source_map_status();
                    const uint64_t t = m.emu.clock().get();
                    const Sample* orig = nullptr;                              // the original run at cycle t
                    for (const Sample& s2 : log) if (s2.clock <= t) orig = &s2;
                    if (!orig) continue;
                    ++compared;
                    if (orig->pending != st.pending || orig->attached != (st.state == Debugger::SourceMapStatus::State::Attached))
                        note += " frame " + std::to_string(frames[i]) + " orig-pending=" + std::to_string(orig->pending) +
                                " landed-pending=" + std::to_string(st.pending);
                    (orig->pending ? saw_true : saw_false) = true;
                }
            };
            int c1 = 0, c2 = 0;
            bool t1 = false, f1 = false, t2 = false, f2 = false;
            std::string n1, n2;
            trial(x_code, dotx_map, true, c1, t1, f1, n1);
            trial(dot_code, dot_map, false, c2, t2, f2, n2);
            check("SRCDOT-12", "a rewind into a dotx's main part (page 4) and into a plain dot's out-of-bank code reads the same status the original run showed at that "
                               "instant (verified while running there, pending before the entry), for every frame of the history",
                  c1 >= 5 && c2 >= 7 && t1 && f1 && t2 && f2 && n1.empty() && n2.empty(),
                  "dotx compared=" + std::to_string(c1) + n1 + " dot compared=" + std::to_string(c2) + n2);
        }
        {   // SRCDOT-13 (review R5): a hand load paused inside the running command judges like a landing there
            const auto loaded_at = [&](uint16_t pc, long flip) {
                Machine m(false, MachineType::ZXN_ISSUE2);
                put_dot(m, dot_code, flip);
                enter_dot_bank(m);
                start_at(m, pc);
                const auto r = m.dbg->load_source_map(dot_map, false);
                return std::make_pair(r, m.dbg->source_map_status());
            };
            const auto inside  = loaded_at(kMain, -1);     // in bank 2, the bytes are the command's
            const auto foreign = loaded_at(kMain, 0xD62);  // in bank 2, another command
            const auto outside = loaded_at(0x6000, -1);    // not running it
            check("SRCDOT-13", "a hand load made while the CPU runs inside the command (bank 2, matching bytes) is verified at once, as a landing there would be; "
                               "with other bytes in the bank, or the CPU elsewhere, it attaches pending",
                  inside.first.count > 0 && !inside.second.pending && inside.second.state == Debugger::SourceMapStatus::State::Attached &&
                      foreign.second.pending && outside.second.pending && !inside.first.pending &&
                      has(inside.first.identity_note, "program identity verified") && !has(inside.first.identity_note, "pending") &&
                      has(foreign.first.identity_note, "the dot command loads when it runs") && foreign.first.pending &&
                      has(outside.first.identity_note, "the dot command loads when it runs"));
        }
        // Shared by SRCDOT-14..18: a Machine with a rewind ring, the command in bank 2 and a loop at $3F00 in it.
        struct Sample { uint64_t clock; bool pending; bool attached; };
        const auto ring_machine = [&](Machine& m, const std::vector<uint8_t>& code, const std::string& map) {
            dot_machine(code, map, m);
            m.emu.divmmc().ram_data()[2 * 0x2000 + 0x1F00] = 0x18;
            m.emu.divmmc().ram_data()[2 * 0x2000 + 0x1F01] = 0xFE;
            m.set_pc(0x6000);
        };
        // The status of the original run now, kept; `frames` the frame numbers a rewind can name.
        const auto sample_of = [&](Machine& m, std::vector<Sample>& log) {
            const auto st = m.dbg->source_map_status();
            log.push_back({m.emu.clock().get(), st.pending, st.state == Debugger::SourceMapStatus::State::Attached});
        };
        const auto frame_of = [&](Machine& m, std::vector<Sample>& log, std::vector<uint32_t>& frames) {
            sample_of(m, log);
            run_frames(m, 1);
            frames.push_back(m.dbg->time().frame);   // rewinding to it lands at the start of the frame just run
        };
        // Rewind to each of `frames`; compare the landed status with the last sample at or before the landed cycle.
        const auto compare_rewinds = [&](Machine& m, const std::vector<Sample>& log, const std::vector<uint32_t>& frames,
                                         int& compared, bool& saw_pending, bool& saw_verified, std::string& note) {
            for (const uint32_t fr : frames) {
                if (m.dbg->rewind_to_frame(m.a, fr) != Result::Ok) { note += " rewind-failed"; continue; }
                const auto st = m.dbg->source_map_status();
                const uint64_t t = m.emu.clock().get();
                const Sample* orig = nullptr;
                for (const Sample& q : log) if (q.clock <= t) orig = &q;
                if (!orig) continue;
                ++compared;
                if (orig->pending != st.pending || orig->attached != (st.state == Debugger::SourceMapStatus::State::Attached))
                    note += " frame " + std::to_string(fr) + " orig-pending=" + std::to_string(orig->pending) +
                            " landed-pending=" + std::to_string(st.pending);
                (orig->pending ? saw_pending : saw_verified) = true;
            }
        };
        {   // SRCDOT-14 (review Q1): a rewind back across a reset into the running command
            Machine m(true, MachineType::ZXN_ISSUE2);
            ring_machine(m, dot_code, dot_map);
            std::vector<Sample> log;
            std::vector<uint32_t> frames;
            frame_of(m, log, frames); frame_of(m, log, frames);          // parked: pending
            m.set_pc(kEntry);
            m.dbg->step_into(m.a);
            m.set_pc(0x3F00);                                           // running inside bank 2
            sample_of(m, log);
            frame_of(m, log, frames); frame_of(m, log, frames);
            const bool ran_verified = !m.dbg->source_map_status().pending;
            const Result rst = m.dbg->reset(m.a, ResetKind::Soft);
            const bool after_reset_pending = m.dbg->source_map_status().pending;
            run_frames(m, 2);
            sample_of(m, log);
            int compared = 0;
            bool sp = false, sv = false;
            std::string note;
            compare_rewinds(m, log, std::vector<uint32_t>(frames.begin(), frames.end()), compared, sp, sv, note);
            check("SRCDOT-14", "a rewind back across a soft reset into the frames where the command was running reads what the original run showed (verified there, "
                               "pending before the entry), although the reset itself leaves the map pending: the records of the earlier generation are kept",
                  rst == Result::Ok && ran_verified && after_reset_pending && compared >= 3 && sp && sv && note.empty(),
                  "compared=" + std::to_string(compared) + note);
        }
        {   // SRCDOT-15 (review Q2): a live re-run after a rewind replaces what the first run recorded
            const auto diverge = [&](bool foreign, std::string& note, bool& ok) {
                Machine m(true, MachineType::ZXN_ISSUE2);
                ring_machine(m, dot_code, dot_map);
                std::vector<Sample> log;
                std::vector<uint32_t> frames;
                frame_of(m, log, frames);
                const uint32_t f_pre = frames.back();
                frame_of(m, log, frames);                                // the first run enters one frame later
                m.set_pc(kEntry);
                m.dbg->step_into(m.a);
                m.set_pc(0x3F00);
                sample_of(m, log);
                frame_of(m, log, frames); frame_of(m, log, frames);
                const bool first_verified = !m.dbg->source_map_status().pending;
                if (m.dbg->rewind_to_frame(m.a, f_pre) != Result::Ok) { ok = false; return; }
                const bool back_pending = m.dbg->source_map_status().pending;
                // the new run: its entry comes at an earlier cycle, with another command or the same one
                if (foreign) m.emu.divmmc().ram_data()[2 * 0x2000 + 0xD62] ^= 0xFF;
                std::vector<Sample> log2;
                std::vector<uint32_t> frames2;
                m.set_pc(kEntry);
                m.dbg->step_into(m.a);
                m.set_pc(0x3F00);
                sample_of(m, log2);
                frame_of(m, log2, frames2); frame_of(m, log2, frames2); frame_of(m, log2, frames2); frame_of(m, log2, frames2);
                const bool new_pending = m.dbg->source_map_status().pending;
                int compared = 0;
                bool sp = false, sv = false;
                compare_rewinds(m, log2, frames2, compared, sp, sv, note);
                ok = first_verified && back_pending && new_pending == foreign && compared >= 3 && (foreign ? sp : sv) && note.empty();
            };
            std::string n1, n2;
            bool ok1 = false, ok2 = false;
            diverge(true, n1, ok1);
            diverge(false, n2, ok2);
            check("SRCDOT-15", "verified run, rewind to before the entry, a live re-run entering earlier with another command (and with the same command): a rewind into the "
                               "new run reads the new run's status, the abandoned run's later verdict is gone",
                  ok1 && ok2, n1 + "|" + n2);
        }
        {   // SRCDOT-16 (review Q3, N2): a landing between the matching entry and the main part's first run keeps the main part armed
            Machine m(true, MachineType::ZXN_ISSUE2);
            ring_machine(m, x_code, dotx_map);
            tape_bytes(m, MemSpace::page(4), root / "x" / "prog_MAIN.bin", 0x7EB, 0);   // a wrong main part
            std::vector<Sample> log;
            std::vector<uint32_t> frames;
            frame_of(m, log, frames);
            const uint32_t f_pre = frames.back();
            m.set_pc(kEntry);
            m.dbg->step_into(m.a);                                       // armed
            m.set_pc(0x3F00);
            frame_of(m, log, frames); frame_of(m, log, frames); frame_of(m, log, frames);
            const uint32_t f_mid = frames[2];                            // a frame boundary after the entry, before the main part runs
            const Result r1 = m.dbg->rewind_to_frame(m.a, f_pre);
            const auto s_pre = m.dbg->source_map_status();
            const Result r2 = m.dbg->rewind_to_frame(m.a, f_mid);
            const auto s_mid = m.dbg->source_map_status();
            Debugger::SourceMapStatus s_end;
            m.set_pc(0x87EB);
            const std::string lg = debugger_log_of([&] { m.dbg->step_into(m.a); s_end = m.dbg->source_map_status(); });
            check("SRCDOT-16", "dotx: after a rewind to before the entry and forward to a frame between the matching entry and the main part's first run, the main part is "
                               "still armed: running it with a wrong byte drops the map, as in the original run",
                  r1 == Result::Ok && r2 == Result::Ok && s_pre.pending && s_mid.pending &&
                      s_end.state == Debugger::SourceMapStatus::State::None && has(lg, "Rejected z88dk source map"),
                  lg);
        }
        {   // SRCDOT-17 (review Q3, N7): a history of several transitions is read back in every period
            Machine m(true, MachineType::ZXN_ISSUE2);
            ring_machine(m, dot_code, dot_map);
            std::vector<Sample> log;
            std::vector<uint32_t> frames;
            const auto enter = [&] { enter_dot_bank(m); m.set_pc(kEntry); m.dbg->step_into(m.a); m.set_pc(0x3F00); };
            frame_of(m, log, frames);                                    // pending
            enter(); sample_of(m, log);                                  // verified
            frame_of(m, log, frames);
            m.emu.divmmc().ram_data()[2 * 0x2000 + 0xD62] ^= 0xFF;
            enter(); sample_of(m, log);                                  // another command: pending
            frame_of(m, log, frames);
            m.emu.divmmc().ram_data()[2 * 0x2000 + 0xD62] ^= 0xFF;
            enter(); sample_of(m, log);                                  // ours again: verified
            frame_of(m, log, frames);
            frame_of(m, log, frames);
            int compared = 0;
            bool sp = false, sv = false;
            std::string note;
            compare_rewinds(m, log, frames, compared, sp, sv, note);
            check("SRCDOT-17", "a history of verified, foreign entry, verified again, with frames in each period: a rewind into every frame reads the status the original run "
                               "showed there",
                  compared >= 4 && sp && sv && note.empty(), "compared=" + std::to_string(compared) + note);
        }
        {   // SRCDOT-18 (review Q3, N9): a hand load inside the command is part of the history
            Machine m(true, MachineType::ZXN_ISSUE2);
            put_dot(m, dot_code);
            enter_dot_bank(m);
            m.emu.divmmc().ram_data()[2 * 0x2000 + 0x1F00] = 0x18;
            m.emu.divmmc().ram_data()[2 * 0x2000 + 0x1F01] = 0xFE;
            start_at(m, 0x3F00);                                         // running the command's loop
            const auto r = m.dbg->load_source_map(dot_map, false);
            run_frames(m, 2);
            const uint32_t f_later = m.dbg->time().frame;   // the start of the second frame, after the load
            run_frames(m, 2);
            const bool before = !m.dbg->source_map_status().pending;
            const Result rr = m.dbg->rewind_to_frame(m.a, f_later);
            const auto st = m.dbg->source_map_status();
            check("SRCDOT-18", "a map hand-loaded while the CPU runs the command is verified, and a rewind to a later frame of that run still reads verified",
                  r.count > 0 && before && rr == Result::Ok && !st.pending && st.state == Debugger::SourceMapStatus::State::Attached);
        }
        {   // SRCDOT-19 (review S1): a state load restores an earlier clock; the old run's verdict is not the new run's
            Machine m(true, MachineType::ZXN_ISSUE2);
            ring_machine(m, dot_code, dot_map);
            run_frames(m, 1);                                            // parked
            const auto saved = m.dbg->save_state_bytes(m.a, jnext::dbg::SaveStateMode::RefuseMidFrame);
            m.set_pc(kEntry);
            m.dbg->step_into(m.a);                                       // verified, in the old generation
            m.set_pc(0x3F00);
            run_frames(m, 3);
            const bool old_verified = !m.dbg->source_map_status().pending;
            Result ld = Result::Unsupported;
            if (saved) ld = m.dbg->load_state_bytes(m.a, saved.value.data(), saved.value.size());
            m.set_pc(0x6000);
            run_frames(m, 1);
            run_frames(m, 1);                                            // never entered: pending
            const bool new_pending = m.dbg->source_map_status().pending;
            const uint32_t f2 = m.dbg->time().frame;
            const Result rr = m.dbg->rewind_to_frame(m.a, f2);
            const auto st = m.dbg->source_map_status();
            check("SRCDOT-19", "after a state load back to an earlier clock, a rewind into the new run (never entered) reads pending, not the old generation's verdict at an overlapping cycle",
                  saved && ld == Result::Ok && old_verified && new_pending && rr == Result::Ok && st.pending &&
                      st.state == Debugger::SourceMapStatus::State::Attached);
        }
    }

    // ── GH #387 (E23-24): the user's labels and comments through the backend ────────
    // Oracle: design 10.2/10.3 (two layers, one reader: user label, else toolchain paged, else toolchain
    // unpaged; by name the user layer first), 6.8 (kept across loads, resets, state loads, rewinds; not in
    // `.jns`), 10.5 (the file), and Mmu::route() / NR 0x56 for which page supplies $C000 (design 4.1, 4.2).
    {
        const fs::path udir = dir / "uann";
        fs::create_directories(udir);
        const MemSpace P10 = MemSpace::page(0x10), P11 = MemSpace::page(0x11);
        const auto nr = [](Machine& m, uint8_t reg, uint8_t v) { m.dbg->nextreg_write(m.a, reg, v); };
        const auto name_at = [&](Machine& m, unsigned page, uint16_t addr = 0xC000) {
            nr(m, 0x56, static_cast<uint8_t>(page));
            return m.dbg->lookup(addr).value_or("-");
        };
        const std::string equ = [] {
            std::ifstream in(fs::path(JNEXT_SJASMEQU_DIR) / "equlabels.sld", std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        }();
        // equlabels.sld (sjasmplus's own output): banked P20 $C000, other_bank P30 $C000, SCREEN unpaged $4000,
        // main P4 $8000, 12 labels.
        const auto with_equ = [&](Machine& m, const char* stem) {
            write_text(udir / (std::string(stem) + ".sld"), equ);
            return m.dbg->load_program_sidecars((udir / (std::string(stem) + ".nex")).string());
        };
        const fs::path tcmap = udir / "tc.map";
        write_text(tcmap, "tc = $C000\n");
        {   // SRCDBG-UANN-01 (issue row 1)
            Machine m(false, MachineType::ZXN_ISSUE2);
            const Result s = m.dbg->set_user_label(P10, 0xC000, "banked");
            const bool premise = (nr(m, 0x56, 0x10), m.dbg->source_page(0xC000) == P10);
            const std::string at10 = name_at(m, 0x10), at11 = name_at(m, 0x11);
            m.dbg->load_map(tcmap.string(), jnext::dbg::MapFormat::Simple);
            const std::string tc11 = name_at(m, 0x11), tc10 = name_at(m, 0x10);
            check("SRCDBG-UANN-01", "a user label at (P10, $C000) names $C000 only while page 0x10 is mapped in slot 6, "
                                    "and over an unpaged toolchain label of the same address (which names it in the other pages)",
                  s == Result::Ok && premise && at10 == "banked" && at11 == "-" && tc11 == "tc" && tc10 == "banked",
                  at10 + "/" + at11 + "/" + tc11 + "/" + tc10);
        }
        {   // SRCDBG-UANN-02
            Machine m(false, MachineType::ZXN_ISSUE2);
            m.dbg->set_user_label(P10, 0xC000, "banked");
            nr(m, 0x56, 0x10);
            const auto k = m.dbg->symbols().find_name("banked");
            const auto near = m.dbg->symbols().nearest(0xC005, [&](uint16_t a) { return m.dbg->source_page(a); });
            const uint8_t prog[] = {0xCD, 0x00, 0xC0};   // CALL $C000
            for (size_t i = 0; i < sizeof prog; ++i) m.emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
            const std::string in10 = m.dbg->disassemble(0x8000, 1, &m.dbg->symbols())[0].mnemonic;
            nr(m, 0x56, 0x11);
            const std::string in11 = m.dbg->disassemble(0x8000, 1, &m.dbg->symbols())[0].mnemonic;
            check("SRCDBG-UANN-02", "every backend reader sees the user label through the one table: lookup_name(), "
                                    "symbols().find_name() (with its page), nearest(), and the disassembler's operand "
                                    "(CALL banked in page 0x10, CALL $C000 in 0x11)",
                  m.dbg->lookup_name("banked") == std::optional<uint16_t>(0xC000) && k && k->addr == 0xC000 &&
                      k->space && *k->space == P10 && near && near->first == 0xC000 && near->second == "banked" &&
                      in10.find("banked") != std::string::npos &&
                      in11.find("banked") == std::string::npos && in11.find("C000") != std::string::npos,
                  in10 + " / " + in11);
        }
        {   // SRCDBG-UANN-03
            Machine m(false, MachineType::ZXN_ISSUE2);
            with_equ(m, "u3");
            const std::string tc_shown = name_at(m, 20);
            m.dbg->set_user_label(MemSpace::page(20), 0xC000, "mine");
            const std::string user_shown = name_at(m, 20);
            const bool by_name = m.dbg->lookup_name("banked") == std::optional<uint16_t>(0xC000) &&
                                 m.dbg->lookup_name("mine") == std::optional<uint16_t>(0xC000);
            m.dbg->remove_user_label(MemSpace::page(20), 0xC000);
            check("SRCDBG-UANN-03", "a user label shadows the toolchain's paged label at its key (both names still "
                                    "resolve), and removing it shows the toolchain's again",
                  tc_shown == "banked" && user_shown == "mine" && by_name && name_at(m, 20) == "banked" &&
                      !m.dbg->lookup_name("mine"));
        }
        {   // SRCDBG-UANN-04
            Machine m(false, MachineType::ZXN_ISSUE2);
            with_equ(m, "u4");
            std::string w1, w3;
            const Result other_addr = m.dbg->set_user_label(MemSpace::page(5), 0x8000, "SCREEN", &w1);
            const Result pin = m.dbg->set_user_label(MemSpace::page(5), 0x4000, "SCREEN");
            const Result other_page = m.dbg->set_user_label(MemSpace::page(21), 0xC000, "banked", &w3);
            write_text(udir / "u4.jdp", "jnext-annotations 1\nlabel P05 $8000 SCREEN\n");
            const Result file = m.dbg->load_annotations((udir / "u4.jdp").string());
            check("SRCDBG-UANN-04", "the name rule: a name the toolchain gives another address, or another paged "
                                    "memory, is refused naming it; the same address (pinning an unpaged name to a page) "
                                    "is accepted; a project file is not refused and then wins by name",
                  other_addr == Result::Unsupported && w1.find("$4000") != std::string::npos &&
                      pin == Result::Ok && other_page == Result::Unsupported && w3.find("$C000") != std::string::npos &&
                      file == Result::Ok && m.dbg->lookup_name("SCREEN") == std::optional<uint16_t>(0x8000));
        }
        {   // SRCDBG-UANN-12 (issue row 3), -13, -14
            Machine m(false, MachineType::ZXN_ISSUE2);
            const uint64_t r0 = m.dbg->annotations_revision();
            m.dbg->set_user_label(P10, 0xC000, "banked");
            m.dbg->set_user_comment(P10, 0xC000, "hi\nthere");
            const uint64_t r2 = m.dbg->annotations_revision();
            const fs::path f = udir / "u12.jdp";
            std::string why;
            const Result sv = m.dbg->save_annotations(f.string(), &why);
            const jnext::dbg::UserAnnotations original = m.dbg->annotations();
            std::ifstream in(f, std::ios::binary);
            const std::string text(std::istreambuf_iterator<char>(in), {});
            in.close();   // Windows cannot replace a file that is open
            m.dbg->replace_annotations(jnext::dbg::UserAnnotations{});
            const bool emptied = m.dbg->annotations().empty();
            const Result ld = m.dbg->load_annotations(f.string(), &why);
            const uint64_t r4 = m.dbg->annotations_revision();
            const Result refused = m.dbg->set_user_label(P11, 0xC000, "banked");
            const Result refused2 = m.dbg->remove_user_label(P11, 0xC000);
            check("SRCDBG-UANN-12", "save, replace by an empty store, load: the store is as it was and the file is "
                                    "design 10.5's canonical text; annotations_revision() rises by one per successful verb "
                                    "and is untouched by a refused one",
                  sv == Result::Ok && ld == Result::Ok && emptied && m.dbg->annotations() == original &&
                      text == "jnext-annotations 1\nlabel P10 $C000 banked\ncomment P10 $C000 hi\\nthere\n" &&
                      r2 == r0 + 2 && r4 == r2 + 2 && refused == Result::Unsupported &&
                      refused2 == Result::RefusedUnavailable && m.dbg->annotations_revision() == r4);

            // -13: a refused load keeps the store and the revision
            const jnext::dbg::UserAnnotations keep = m.dbg->annotations();
            const uint64_t rk = m.dbg->annotations_revision();
            write_text(udir / "u13.jdp", "jnext-annotations 1\nlabel P10 $C000 a\nlabel P10 $C001 !\n");
            std::string w_bad, w_dir, w_big, w_none;
            const Result bad = m.dbg->load_annotations((udir / "u13.jdp").string(), &w_bad);
            fs::create_directories(udir / "u13dir");
            const Result dr = m.dbg->load_annotations((udir / "u13dir").string(), &w_dir);
            {
                std::ofstream big(udir / "u13big.jdp", std::ios::binary);
                big << "jnext-annotations 1\n";
            }
            fs::resize_file(udir / "u13big.jdp", SOURCE_FILE_MAX_BYTES + 1);
            const Result bg = m.dbg->load_annotations((udir / "u13big.jdp").string(), &w_big);
            fs::remove(udir / "u13big.jdp");
            const Result none = m.dbg->load_annotations((udir / "nonexistent.jdp").string(), &w_none);
            check("SRCDBG-UANN-13", "a load that is refused keeps the store and the revision: a bad line 3 "
                                    "(Unsupported, 'line 3: ...'), a directory, a file over the size cap, a missing file "
                                    "(RefusedUnavailable, each with a reason)",
                  bad == Result::Unsupported && w_bad == "line 3: '!' is not a label name" &&
                      dr == Result::RefusedUnavailable && w_dir == source_file_problem((udir / "u13dir").string()) &&
                      !w_dir.empty() && bg == Result::RefusedUnavailable &&
                      w_big.find("-byte limit") != std::string::npos &&
                      none == Result::RefusedUnavailable && !w_none.empty() && m.dbg->annotations() == keep &&
                      m.dbg->annotations_revision() == rk,
                  w_bad + " | " + w_dir + " | " + w_big + " | " + w_none);

            // -14: save
            m.dbg->set_user_comment(P11, 0x0000, "second");
            std::string w_save;
            const Result again = m.dbg->save_annotations(f.string(), &w_save);   // over the existing file
            std::ifstream in2(f, std::ios::binary);
            const std::string text2(std::istreambuf_iterator<char>(in2), {});
            const Result nodir = m.dbg->save_annotations((udir / "no" / "such" / "x.jdp").string(), &w_save);
            check("SRCDBG-UANN-14", "save over an existing file replaces it, leaves no .part behind, and a path "
                                    "that cannot be written is refused with a reason",
                  again == Result::Ok && text2.find("comment P11 $0000 second\n") != std::string::npos &&
                      !fs::exists(f.string() + ".part") && nodir == Result::RefusedUnavailable && !w_save.empty());
        }
        {   // SRCDBG-UANN-15 and -16
            Machine m(false, MachineType::ZXN_ISSUE2);
            with_equ(m, "u15");
            const size_t tool = m.dbg->toolchain_symbols().size();
            m.dbg->set_user_label(P10, 0xC000, "banked10");
            m.dbg->set_user_comment(P10, 0xC000, "note");
            nr(m, 0x56, 0x10);
            const auto* c10 = m.dbg->annotations().find(m.dbg->source_page(0xC000), 0xC000);
            nr(m, 0x56, 0x11);
            const auto* c11 = m.dbg->annotations().find(m.dbg->source_page(0xC000), 0xC000);
            check("SRCDBG-UANN-15", "a comment is page-aware: the entry keyed by the memory that supplies $C000 is "
                                    "found with page 0x10 mapped and not with 0x11",
                  c10 && c10->comment == "note" && !c11);
            check("SRCDBG-UANN-16", "toolchain_symbols() holds the loaded labels only (the Load MAP message's "
                                    "count), symbols() the composition including the user's",
                  tool == m.dbg->toolchain_symbols().size() && tool > 0 &&
                      m.dbg->symbols().size() == tool + 1,
                  std::to_string(tool) + "/" + std::to_string(m.dbg->symbols().size()));
        }
        {   // SRCDBG-UANN-17: the state revision (GH #428) and -18: allowed while running
            Machine m(false, MachineType::ZXN_ISSUE2);
            const uint64_t v0 = m.dbg->state_revision();
            m.dbg->set_user_label(P10, 0xC000, "banked");
            const uint64_t v1 = m.dbg->state_revision();
            m.dbg->set_user_comment(P10, 0xC000, "c");
            const uint64_t v2 = m.dbg->state_revision();
            m.dbg->set_user_label(P10, 0xC000, "bank2");
            const uint64_t v3 = m.dbg->state_revision();
            m.dbg->remove_user_label(P10, 0xC000);
            const uint64_t v4 = m.dbg->state_revision();
            write_text(udir / "u17.jdp", "jnext-annotations 1\nlabel P10 $C000 z\n");
            m.dbg->load_annotations((udir / "u17.jdp").string());
            const uint64_t v5 = m.dbg->state_revision();
            m.dbg->replace_annotations(jnext::dbg::UserAnnotations{});
            const uint64_t v6 = m.dbg->state_revision();
            m.dbg->set_user_label(P10, 0xC000, "1bad");                          // refused
            m.dbg->remove_user_label(P10, 0xC000);                               // refused
            m.dbg->load_annotations((udir / "nonexistent.jdp").string());        // refused
            m.dbg->save_annotations((udir / "u17save.jdp").string());            // reads only
            const uint64_t v7 = m.dbg->state_revision();
            check("SRCDBG-UANN-17", "every verb that changes what a panel shows bumps state_revision() (set, rename, "
                                    "comment, remove, load, replace); a refused verb and a save do not (GH #428)",
                  v1 > v0 && v2 > v1 && v3 > v2 && v4 > v3 && v5 > v4 && v6 > v5 && v7 == v6);
            Machine r(false, MachineType::ZXN_ISSUE2);
            r.dbg->run(r.a);
            const Result run_set = r.dbg->set_user_label(P10, 0xC000, "banked");
            const bool running = !r.dbg->state().paused;
            r.dbg->pause(r.a);
            check("SRCDBG-UANN-18", "the verbs touch no machine state, so they are allowed while the machine runs",
                  run_set == Result::Ok && running && r.dbg->lookup_name("banked") == std::optional<uint16_t>(0xC000));
        }
    }

    // ── GH #387: the store has no lifetime (design 6.8) ──────────────────────────────
    {
        const MemSpace P = MemSpace::page(p80);   // what supplies $8000 on the 48K fixture machine
        const fs::path tdir = dir / "uann-life";
        fs::create_directories(tdir);
        const fs::path uann_map = tdir / "user.map";
        write_text(uann_map, "UserSym = $8005 ; addr, local\n");
        const auto add_users = [&](Machine& m) {
            m.dbg->set_user_label(m.dbg->source_page(0x8000), 0x8000, "mine");
            m.dbg->set_user_label(m.dbg->source_page(0x8010), 0x8010, "other");
        };
        {   // SRCDBG-UANN-05 (issue row 2): sidecar reloads
            Machine m;
            const auto r = attach(m, "uann5");
            add_users(m);
            const jnext::dbg::UserAnnotations before = m.dbg->annotations();
            const bool side_below = r.symbols == 1 && m.dbg->lookup_name("Main") == std::optional<uint16_t>(0x8000);
            attach(m, "uann5");   // the same program again
            const bool again = m.dbg->lookup(0x8000) == std::optional<std::string>("mine") &&
                               m.dbg->lookup(0x8010) == std::optional<std::string>("other") &&
                               m.dbg->lookup_name("Main");
            m.dbg->load_program_sidecars((tdir / "plain5.nex").string());   // a program with no sidecars
            check("SRCDBG-UANN-05", "a sidecar reload, and then a program with no sidecars, leave the user labels "
                                    "shown (the sidecar's Main is gone after the second)",
                  side_below && again && !m.dbg->lookup_name("Main") &&
                      m.dbg->lookup(0x8000) == std::optional<std::string>("mine") &&
                      m.dbg->lookup(0x8010) == std::optional<std::string>("other") && m.dbg->annotations() == before);
        }
        {   // SRCDBG-UANN-06: the hand loads
            Machine m;
            add_users(m);
            const jnext::dbg::UserAnnotations before = m.dbg->annotations();
            m.dbg->load_map(uann_map.string(), jnext::dbg::MapFormat::Z88dk);
            const bool a = m.dbg->lookup(0x8000) == std::optional<std::string>("mine") && m.dbg->lookup_name("UserSym");
            m.dbg->clear_symbols();
            const bool b = m.dbg->lookup(0x8010) == std::optional<std::string>("other") && !m.dbg->lookup_name("UserSym");
            m.dbg->load_source_map((fs::path(JNEXT_SJASMEQU_DIR) / "equlabels.sld").string(), false);
            const bool c = m.dbg->lookup(0x8000) == std::optional<std::string>("mine") && m.dbg->lookup_name("SCREEN");
            check("SRCDBG-UANN-06", "after Load MAP, clear_symbols() and a hand-loaded SLD with labels the user "
                                    "labels are shown and the store is as it was",
                  a && b && c && m.dbg->annotations() == before);
        }
        {   // SRCDBG-UANN-19: a hand SLD with no labels clears an earlier hand SLD's labels, not the user's
            Machine m(false, MachineType::ZXN_ISSUE2);
            m.dbg->set_user_label(m.dbg->source_page(0x8000), 0x8000, "mine");
            m.dbg->load_source_map((fs::path(JNEXT_SJASMEQU_DIR) / "equlabels.sld").string(), false);
            const bool loaded = m.dbg->lookup_name("SCREEN") && m.dbg->lookup_name("mine");
            const fs::path plain = tdir / "u19-plain.sld";
            write_text(plain, program_sld(p80, p90));
            const auto none = m.dbg->load_source_map(plain.string(), false);
            check("SRCDBG-UANN-19", "loading a hand SLD with no labels removes the earlier hand SLD's labels from "
                                    "every reader (symbols(), lookup_name()) and leaves the user's",
                  loaded && none.labels == 0 && !m.dbg->lookup_name("SCREEN") && !m.dbg->lookup_name("banked") &&
                      m.dbg->toolchain_symbols().empty() && m.dbg->lookup_name("mine") == std::optional<uint16_t>(0x8000) &&
                      m.dbg->symbols().size() == 1);
        }
        {   // SRCDBG-UANN-20: a memory the project file cannot name is refused through the verbs
            Machine m(false, MachineType::ZXN_ISSUE2);
            std::string w1, w2;
            const uint64_t r0 = m.dbg->annotations_revision(), v0 = m.dbg->state_revision();
            const Result a = m.dbg->set_user_label(MemSpace::page(0xE0), 0x8000, "x", &w1);
            const Result b = m.dbg->set_user_comment(MemSpace::rom(4), 0x0000, "c", &w2);
            const bool refused = a == Result::Unsupported && b == Result::Unsupported &&
                                 w1.find("PE0") != std::string::npos && w2.find("ROM4") != std::string::npos &&
                                 m.dbg->annotations().empty() && m.dbg->annotations_revision() == r0 &&
                                 m.dbg->state_revision() == v0;
            const Result c = m.dbg->set_user_label(MemSpace::page(0xDF), 0x8000, "ok");
            const fs::path f = tdir / "u20.jdp";
            const Result sv = m.dbg->save_annotations(f.string());
            m.dbg->replace_annotations(jnext::dbg::UserAnnotations{});
            const Result ld = m.dbg->load_annotations(f.string());
            check("SRCDBG-UANN-20", "set_user_label / set_user_comment refuse a memory the file cannot name (PE0, ROM4) "
                                    "with Unsupported, a reason, and no change of the store or the revisions; the last "
                                    "in-range page is accepted and survives a save and a load",
                  refused && c == Result::Ok && sv == Result::Ok && ld == Result::Ok &&
                      m.dbg->lookup_name("ok") == std::optional<uint16_t>(0x8000));
        }
        {   // SRCDBG-UANN-21: a verb that changes nothing bumps neither revision (GH #387 N1, decided by the coordinator)
            Machine m(false, MachineType::ZXN_ISSUE2);
            const MemSpace sp = MemSpace::page(0x10);
            const auto rev = [&] { return std::make_pair(m.dbg->annotations_revision(), m.dbg->state_revision()); };
            m.dbg->set_user_label(sp, 0xC000, "banked");
            m.dbg->set_user_comment(sp, 0xC000, "note");
            const auto r0 = rev();
            const Result a1 = m.dbg->set_user_label(sp, 0xC000, "banked");           // rename to its own name
            const bool k1 = rev() == r0;
            const Result a2 = m.dbg->set_user_comment(sp, 0xC000, "note");           // the identical comment
            const bool k2 = rev() == r0;
            const Result a3 = m.dbg->set_user_comment(sp, 0xC100, "");               // clearing one that is not there
            const bool k3 = rev() == r0;
            const Result a4 = m.dbg->set_user_comment(sp, 0xC000, "no\r\nte");       // CRLF normalised: a change
            const bool c4 = rev().first == r0.first + 1 && rev().second > r0.second;
            m.dbg->set_user_comment(sp, 0xC000, "no\nte");                           // already stored so: nothing
            const auto r5 = rev();
            const Result a5 = m.dbg->set_user_comment(sp, 0xC000, "no\r\nte");      // normalises to the stored text
            const bool k5 = rev() == r5;
            const Result a6 = m.dbg->replace_annotations(m.dbg->annotations());     // the same store
            const bool k6 = rev() == r5;
            const fs::path f = tdir / "u21.jdp";
            m.dbg->save_annotations(f.string());
            const Result a7 = m.dbg->load_annotations(f.string());                  // a file of the same store
            const bool k7 = rev() == r5;
            // control: a real change after each still bumps both
            const Result b1 = m.dbg->set_user_label(sp, 0xC000, "renamed");
            const bool real1 = rev().first == r5.first + 1 && rev().second > r5.second;
            check("SRCDBG-UANN-21", "a verb that changes nothing (a label renamed to its own name, an identical or "
                                    "normalised-identical comment, clearing a comment that is not there, replacing or "
                                    "loading the same store) returns Ok and bumps neither annotations_revision() nor "
                                    "state_revision(); a real change still bumps both",
                  a1 == Result::Ok && a2 == Result::Ok && a3 == Result::Ok && a4 == Result::Ok && a5 == Result::Ok &&
                      a6 == Result::Ok && a7 == Result::Ok && b1 == Result::Ok && k1 && k2 && k3 && c4 && k5 && k6 && k7 &&
                      real1);
        }
        {   // SRCDBG-UANN-07 (issue row 2): resets
            bool ok[2] = {false, false};
            for (int hard = 0; hard < 2; ++hard) {
                Machine m;
                if (hard) with_cold_boot(m);
                attach(m, hard ? "uann7h" : "uann7s");
                add_users(m);
                const jnext::dbg::UserAnnotations before = m.dbg->annotations();
                const Result rr = m.dbg->reset(m.a, hard ? ResetKind::Hard : ResetKind::Soft);
                const bool dropped = !m.dbg->lookup_name("Main");   // the landing dropped the sidecar's symbols
                ok[hard] = rr == Result::Ok && dropped && m.dbg->lookup(0x8000) == std::optional<std::string>("mine") &&
                           m.dbg->lookup(0x8010) == std::optional<std::string>("other") && m.dbg->annotations() == before;
            }
            check("SRCDBG-UANN-07", "after a soft and after a hard reset (whose landing drops the sidecar's symbols) "
                                    "the user labels are shown at the next reader and the store is unchanged",
                  ok[0] && ok[1], std::string(ok[0] ? "soft ok " : "soft BAD ") + (ok[1] ? "hard ok" : "hard BAD"));
        }
        {   // SRCDBG-UANN-08 (issue row 2): state loads, a .sna
            Machine m;
            StateWriter measure;
            m.emu.save_state(measure);
            std::vector<uint8_t> earlier(measure.position(), 0);
            StateWriter w(earlier.data(), earlier.size());
            m.emu.save_state(w);
            attach(m, "uann8");
            add_users(m);   // added AFTER the state was saved
            const Result ld = m.dbg->load_state_bytes(m.a, earlier.data(), earlier.size());
            const bool st = m.dbg->lookup(0x8000) == std::optional<std::string>("mine") && !m.dbg->lookup_name("Main");
            Machine other;
            other.emu.mmu().write(0x8000, 0xFF);
            const std::string sna = (tdir / "u8.sna").string();
            other.dbg->save_snapshot(other.a, sna);
            jnext::dbg::LoopDriver d;
            d.load = [&m](const std::string& p) { return emulator_apply_load(m.emu, p, false); };
            m.dbg->set_loop_driver(d);
            const Result sl = m.dbg->load(m.a, sna);
            check("SRCDBG-UANN-08", "a user label added after a state was saved is still there, and shown, after "
                                    "load_state_bytes() of that state and after a .sna load",
                  ld == Result::Ok && st && sl == Result::Ok &&
                      m.dbg->lookup(0x8000) == std::optional<std::string>("mine") &&
                      m.dbg->lookup(0x8010) == std::optional<std::string>("other"));
        }
        {   // SRCDBG-UANN-09: a ring landing
            Machine m(/*rewind=*/true);
            m.dbg->run(m.a);
            for (int i = 0; i < 3; ++i) m.emu.run_frame();
            m.dbg->pause(m.a);
            m.dbg->set_user_label(m.dbg->source_page(0x8000), 0x8000, "gone");   // removed after A's attach
            attach(m, "uann9a");                                              // generation A: Main (and "gone" shown)
            const uint32_t fa = m.dbg->time().frame;
            m.dbg->run(m.a);
            for (int i = 0; i < 3; ++i) m.emu.run_frame();
            m.dbg->pause(m.a);
            m.dbg->load_program_sidecars((tdir / "plain9.nex").string());     // B: no sidecars
            m.dbg->remove_user_label(m.dbg->source_page(0x8000), 0x8000);
            m.dbg->set_user_label(m.dbg->source_page(0x8010), 0x8010, "kept");
            m.dbg->run(m.a);
            for (int i = 0; i < 3; ++i) m.emu.run_frame();
            m.dbg->pause(m.a);
            const bool b_clean = !m.dbg->lookup_name("Main");
            const Result rr = m.dbg->rewind_to_frame(m.a, fa + 1);
            check("SRCDBG-UANN-09", "a ring landing back into A re-attaches A's cached sidecar symbols (Main) and "
                                    "not a user label: one added after is shown, one removed after A's attach does "
                                    "not come back",
                  b_clean && rr == Result::Ok && m.dbg->lookup_name("Main") == std::optional<uint16_t>(0x8000) &&
                      m.dbg->lookup(0x8000) == std::optional<std::string>("Main") &&
                      m.dbg->lookup(0x8010) == std::optional<std::string>("kept") &&
                      !m.dbg->annotations().find_name("gone"));
        }
        {   // SRCDBG-UANN-10 / -11: purity, and its control
            Machine m(/*rewind=*/true);
            m.dbg->run(m.a);
            for (int i = 0; i < 3; ++i) m.emu.run_frame();
            m.dbg->pause(m.a);
            attach(m, "uann10");
            const auto fingerprint = [&](Machine& x) {
                StateWriter ms;
                x.emu.save_state(ms);
                std::vector<uint8_t> bytes(ms.position(), 0);
                StateWriter w(bytes.data(), bytes.size());
                x.emu.save_state(w);
                std::string f(bytes.begin(), bytes.end());
                f += "|gen=" + std::to_string(x.emu.debug_state().load_generation());
                for (const auto& l : x.dbg->toolchain_symbols().symbols())
                    f += "|" + hex(l.first) + l.second.name + (l.second.space ? std::to_string(unsigned(l.second.space->index)) : "u");
                f += "|size=" + std::to_string(x.dbg->toolchain_symbols().size());
                const auto st = x.dbg->source_map_status();
                f += "|map=" + std::to_string(unsigned(st.state)) + st.file + st.reason + (st.pending ? "p" : "") +
                     std::to_string(x.dbg->source_map().size());
                f += "|subs=" + std::to_string(x.dbg->subscriptions(true).size());
                f += "|events=" + std::to_string(x.dbg->events_fired_since(0).size());
                const auto rg = x.dbg->rewind_range();
                f += "|ring=" + std::to_string(rg.oldest_frame) + "-" + std::to_string(rg.newest_frame) + "@" +
                     std::to_string(rg.newest_cycle);
                return f;
            };
            const std::string before = fingerprint(m);
            const fs::path pf = tdir / "u10.jdp";
            write_text(pf, "jnext-annotations 1\nlabel P04 $8000 viafile\n");
            std::string why;
            m.dbg->set_user_label(MemSpace::page(4), 0x8100, "a1");
            m.dbg->set_user_label(MemSpace::page(4), 0x8100, "a2");
            m.dbg->set_user_comment(MemSpace::page(4), 0x8100, "c");
            m.dbg->remove_user_label(MemSpace::page(4), 0x8100);
            m.dbg->save_annotations((tdir / "u10save.jdp").string());
            m.dbg->load_annotations(pf.string());
            m.dbg->replace_annotations(jnext::dbg::UserAnnotations{});
            const bool same = fingerprint(m) == before;
            check("SRCDBG-UANN-10", "every CAP-ANN verb (set, rename, comment, remove, save, load, replace) leaves "
                                    "the machine's save stream, load_generation(), toolchain_symbols(), "
                                    "source_map_status(), the subscriptions, the events fired and the rewind ring "
                                    "exactly as they were",
                  same);
            // control: the observer sees a poke and a Load MAP
            const uint8_t five_a = 0x5A;
            m.dbg->poke(m.a, MemSpace::cpu(), 0x9000, 1, &five_a);
            const bool poke_seen = fingerprint(m) != before;
            const std::string mid = fingerprint(m);
            m.dbg->load_map(uann_map.string(), jnext::dbg::MapFormat::Z88dk);
            check("SRCDBG-UANN-11", "control for -10: the same fingerprint changes with a poke and with a Load MAP, "
                                    "so -10 is not passing on a dead observer",
                  poke_seen && fingerprint(m) != mid);
        }
    }

    fs::remove_all(dir);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
