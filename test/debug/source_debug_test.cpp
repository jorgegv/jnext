// Source-level debugging through the debugger backend — CAP-SRC
// (doc/design/SOURCE-LEVEL-DEBUGGING.md):
//
//   SRCDBG-LOAD-*   load_source_map(): identity refused / accepted, the store
//   SRCDBG-LOC-*    source_location() follows the physical page mapped now
//   SRCDBG-SIDE-*   load_program_sidecars(): what is attached, kept, cleared
//   SRCDBG-STEP-*   source_step() forwards: Into / Over / Out, the stops
//   SRCDBG-BACK-*   source_step() backwards: Back / ReverseContinue
//   SRCDBG-CS-*     the call-stack tracker records each frame's pages
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
//   SRCDBG-TAPE-*    a tape program's SLD is not attached automatically
//   SRCDBG-HIST-*    sidecars are dropped when the machine goes back before
//                    the load they came with
//   SRCDBG-PATH-*    a relative map path is made absolute
//   SRCDBG-FB-*      Back after a Frame Back goes back in time
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
#include "core/saveable.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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
        dbg->set_call_stack_enabled(true);
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
        // one marked page_ram_only (a source breakpoint) refuses the ROM.
        Subscription by_number;
        by_number.kind = EventKind::Execute;
        by_number.filter.lo = by_number.filter.hi = 0x0001;
        by_number.filter.page = static_cast<uint16_t>(rom_page);
        Subscription ram_only = by_number;
        ram_only.filter.page_ram_only = true;
        const auto id1 = m.dbg->subscribe(m.a, by_number).value;
        const auto id2 = m.dbg->subscribe(m.a, ram_only).value;
        const auto hits = m.dbg->probe_execute(0x0001);
        const bool has1 = std::find(hits.begin(), hits.end(), id1) != hits.end();
        const bool has2 = std::find(hits.begin(), hits.end(), id2) != hits.end();
        check("SRCDBG-ID-02", "a page_ram_only Execute does not match a ROM fetch under "
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
        bp.filter.page_ram_only = true;
        const auto id = m.dbg->subscribe(m.a, bp).value;
        const auto hits = m.dbg->probe_execute(0x0000);
        const bool bp_refused = std::find(hits.begin(), hits.end(), id) == hits.end();
        check("SRCDBG-ID-06", "an instant DivMMC entry point is foreseen: before the M1 "
                              "maps it, $0000 is already not RAM page 10, and a RAM-only "
                              "breakpoint there does not match",
              !mapped_now && predicted && bp_refused);

        m.dbg->unsubscribe(m.a, id);
        m.dbg->set_trace_enabled(true);
        m.dbg->set_call_stack_enabled(true);
        m.dbg->step_into(m.a);   // CALL
        m.dbg->step_into(m.a);   // DivMMC's INC A
        const auto& t = m.emu.trace_log();
        const bool ran_divmmc = (m.emu.cpu().get_registers().AF >> 8) == 1;
        const bool flagged = t.size() >= 2 && t.at(t.size() - 1).pc == 0x0000 &&
                             t.fetch_not_mmu_ram(t.size() - 1);
        const auto& f = m.dbg->call_stack();
        check("SRCDBG-ID-07", "and once it runs: the trace marks DivMMC's instruction "
                              "not-RAM, and the CALL's frame names no RAM page as target",
              ran_divmmc && flagged && f.size() == 1 && f[0].target_page == NOT_RAM_PAGE,
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
              f.size() == 2 && f[0].caller_pc == 0x8000 && f[0].caller_page == 4 &&
                  f[0].target_pc == 0x0008 && f[0].target_page == NOT_RAM_PAGE &&
                  f[1].caller_pc == 0x0009 && f[1].caller_page == NOT_RAM_PAGE &&
                  f[1].target_pc == 0x0020 && f[1].target_page == NOT_RAM_PAGE,
              [&] {
                  std::string d = "frames=" + std::to_string(f.size());
                  for (const auto& fr : f)
                      d += " [" + hex(fr.caller_pc) + "->" + hex(fr.target_pc) + " cp=" +
                           std::to_string(fr.caller_page) + " tp=" + std::to_string(fr.target_page) + "]";
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
            m.dbg->set_call_stack_enabled(true);
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
                      t.fetch_not_mmu_ram(t.size() - 1) && f.size() == 2 &&
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
                  e.opcode_len == 2 && !t.fetch_not_mmu_ram(t.size() - 1),
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
                  e.opcode_len == 2 && t.fetch_not_mmu_ram(t.size() - 1),
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
                  e.opcode_bytes[2] == 0xA0 && e.opcode_len == 3 && !t.fetch_not_mmu_ram(t.size() - 1),
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
                  e.opcode_bytes[2] == 0x90 && e.opcode_len == 3 && t.fetch_not_mmu_ram(t.size() - 1),
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
            m.dbg->set_call_stack_enabled(true);
            m.set_pc(0x8000);
            (void)step_pair(m); (void)step_pair(m);
            const auto& f = m.dbg->call_stack();
            check("SRCDBG-ID-37", "a CALL in RAM at a delayed DivMMC entry: its frame has the caller page "
                                  "RAM page 10 and the target the DivMMC ROM's operands ($0030)",
                  m.pc() == 0x0030 && f.size() == 2 && f[1].caller_pc == 0x0008 &&
                      f[1].caller_page == 10 && f[1].target_pc == 0x0030,
                  [&] {
                      std::string d = "pc=" + hex(m.pc()) + " frames=" + std::to_string(f.size());
                      if (f.size() > 1)
                          d += " [" + hex(f[1].caller_pc) + "->" + hex(f[1].target_pc) +
                               " cp=" + std::to_string(f[1].caller_page) + "]";
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
        // at $0000 is the DivMMC ROM's, so a page_ram_only subscription on RAM page 10 there is
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
        ram_only.filter.page_ram_only = true;
        ram_only.action = Action::Continue;
        ram_only.handler = [&ram_only_hits](const jnext::dbg::Event&, Debugger&) {
            ++ram_only_hits;
            return Action::Continue;
        };
        Subscription plain = ram_only;
        plain.filter.page_ram_only = false;
        plain.handler = [&plain_hits](const jnext::dbg::Event&, Debugger&) {
            ++plain_hits;
            return Action::Continue;
        };
        m.dbg->subscribe(m.a, ram_only);
        m.dbg->subscribe(m.a, plain);
        m.dbg->run(m.a);
        m.emu.run_frame();
        check("SRCDBG-ID-21", "while running, an Execute gate at an instant DivMMC entry refuses a "
                              "page_ram_only subscription on the RAM page there and delivers the plain "
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
    // ── a tape program gets no automatic map ────────────────────────────
    {
        Machine m;
        write_text(own / "tape.tap", "");
        write_text(own / "tape.sld", program_sld(p80, p90));
        const auto r = m.dbg->load_program_sidecars((own / "tape.tap").string());
        check("SRCDBG-TAPE-01", "an SLD beside a tape program is not attached "
                                "automatically (the tape has not loaded yet)",
              r.sources == -1 && m.dbg->source_map().empty());
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
        const auto& f = m.dbg->call_stack();
        check("SRCDBG-CS-01", "a CALL's frame carries the caller's and the target's "
                              "physical pages",
              f.size() == 1 && f[0].caller_pc == 0x8002 && f[0].caller_page == p80 &&
                  f[0].target_pc == 0x9000 && f[0].target_page == p90);
    }
    {
        // An interrupt taken in the slot right after the tracker was enabled:
        // no opcode was captured before it, so the caller's page is not known.
        // That is NOT_RAM_PAGE, never RAM page 0 (which would show page 0's
        // line as the caller). With a capture, the frame has its page.
        CallStack cs;
        cs.set_enabled(true);
        cs.on_interrupt(0x8000, 0x0038, 0xFF00, CallType::INT, p90);
        cs.on_instruction_pre(0x8002, 0xFF00, 0x00, 0x00, 0x00, p80);
        cs.on_interrupt(0x8002, 0x0038, 0xFEFE, CallType::INT, p90);
        const auto& f = cs.frames();
        check("SRCDBG-CS-02", "an interrupt in the slot right after the tracker is enabled "
                              "records no RAM page for its caller; one after a capture "
                              "records the captured page",
              f.size() == 2 && f[0].caller_pc == 0x8000 && f[0].caller_page == NOT_RAM_PAGE &&
                  f[0].target_page == p90 && f[1].caller_pc == 0x8002 &&
                  f[1].caller_page == p80);
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
        const auto foreign_state = [](uint8_t first, bool program, int frames) {
            Emulator o;
            EmulatorConfig c;
            c.type = MachineType::ZX48K;
            o.init(c);
            if (program) {
                const uint8_t main_prog[] = {0x00, 0x00, 0xCD, 0x00, 0x90, 0x00, 0xC3, 0x00, 0x80};
                for (size_t i = 0; i < sizeof(main_prog); ++i)
                    o.mmu().write(static_cast<uint16_t>(0x8000 + i), main_prog[i]);
            }
            o.mmu().write(0x8000, first);
            Z80Registers r = o.cpu().get_registers();
            r.PC = 0x8000;
            r.SP = 0xFF00;
            r.IFF1 = r.IFF2 = 0;
            o.cpu().set_registers(r);
            for (int i = 0; i < frames; ++i) o.run_frame();
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
                      st.state == State::None && !st.reason.empty(),
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
                  ld == Result::Ok && m.dbg->source_map().empty(),
                  "ld=" + std::to_string(int(ld)));
        }

        // SRCLIFE-08: Back does not cross a state load.
        {
            Machine m(/*rewind=*/true);
            LogCatcher lc;
            m.dbg->set_listener(m.a, &lc);
            m.dbg->load_source_map(good.string(), false);
            m.dbg->set_trace_enabled(true);
            run_frames(m, 2);
            const std::vector<uint8_t> s = own_state(m);
            run_frames(m, 2);
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
                               lc.has("Kept the loaded source map") == false &&
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
        // not run on into the history before a state load.
        {
            Machine m(/*rewind=*/true);
            m.dbg->load_source_map(good.string(), false);
            m.dbg->set_trace_enabled(true);
            run_frames(m, 2);
            m.dbg->step_into(m.a);                 // $8000 NOP -> PC=$8001
            const std::vector<uint8_t> s = own_state(m);
            const Result ld = m.dbg->load_state_bytes(m.a, s.data(), s.size());
            const uint64_t load_clock = m.emu.clock().get();
            m.dbg->source_step(m.a, Step::Into);    // $8001 -> $8002 (main.bas:11)
            const auto& t = m.emu.trace_log();
            const bool pre = t.size() >= 2 && t.at(t.size() - 1).pc == 0x8001 &&
                             t.at(t.size() - 2).pc == 0x8000;
            const Result r = m.dbg->source_step(m.a, Step::Back);
            check("SRCLIFE-21", "Back after Into past a state load does not land before the load",
                  ld == Result::Ok && pre && (r != Result::Ok || m.emu.clock().get() >= load_clock),
                  "r=" + std::to_string(int(r)) + " clock=" + std::to_string(m.emu.clock().get()) +
                      " load=" + std::to_string(load_clock));
        }

        // SRCLIFE-22: verification is pure (no watch, no latch).
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
            const std::vector<uint8_t> s = own_state(m);
            m.dbg->load_state_bytes(m.a, s.data(), s.size());
            const std::vector<uint8_t> before = own_state(m);
            (void)m.dbg->source_map_status();
            m.dbg->pump(jnext::dbg::PumpBudget{});
            const std::vector<uint8_t> after = own_state(m);
            check("SRCLIFE-22", "verifying a map's identity runs no Mem Read handler and changes "
                                "no byte of the state",
                  hits == 0 && before == after && m.dbg->source_map().size() == 6);
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
            check("SRCLIFE-23", "A's history is 'back' within A's generation after a rewind into "
                                "it across B's load",
                  rw == Result::Ok && r == Result::Ok && m.emu.clock().get() < at,
                  "rw=" + std::to_string(int(rw)) + " r=" + std::to_string(int(r)));
        }
    }

    fs::remove_all(dir);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
