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
//   8001  00           NOP          (unmapped: same statement)
//   8002  CD 00 90     CALL SUB     main.bas:11
//   8005  00           NOP          main.bas:12
//   8006  C3 00 80     JP $8000     main.bas:13
//   9000  32 00 A0     LD ($A000),A sub.bas:1
//   9003  00           NOP          sub.bas:2
//   9004  C9           RET          (unmapped)
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
//
// Run: ./build/test/source_debug_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "debug/ram_page.h"
#include "peripheral/divmmc.h"
#include "debug/sld_loader.h"
#include "core/saveable.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <string>
#include <unistd.h>
#include "../row_id.h"

namespace fs = std::filesystem;
using jnext::dbg::Access;
using jnext::dbg::Action;
using jnext::dbg::ClientId;
using jnext::dbg::Debugger;
using jnext::dbg::EventKind;
using jnext::dbg::PauseReason;
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
        std::printf("  FAIL %s: %s%s%s\n", id, desc, detail.empty() ? "" : " — ",
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
        check("SRCDBG-LOC-03", "an unmapped instruction has no location",
              !m.dbg->source_location(0x8001));
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

    // ── source_step() forwards ──────────────────────────────────────────
    {
        Machine m;
        check("SRCDBG-STEP-01", "with no source map a source step is refused",
              m.dbg->source_step(m.a, Step::Into) == Result::RefusedUnavailable &&
                  m.pc() == 0x8000);
        m.dbg->load_source_map(sld.string(), false);

        const Result r = m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-02", "Into skips the unmapped instruction of the same "
                                "statement: main.bas:10 -> :11",
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
        check("SRCDBG-STEP-08", "an Execute breakpoint on an unmapped instruction ends "
                                "the step there, before it runs",
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
        m.dbg->load_source_map(sld.string(), false);
        m.set_pc(0x8001);   // unmapped
        m.dbg->source_step(m.a, Step::Into);
        check("SRCDBG-STEP-11", "from an unmapped instruction a step is one instruction",
              m.pc() == 0x8002, m.where());
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
        check("SRCDBG-BACK-04", "Back again skips the unmapped RET: sub.bas:2",
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
              m.dbg->source_page(0x0001) == NOT_RAM_PAGE && !m.dbg->source_location(0x0001));

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
        const bool ram = m.dbg->source_page(0x0010) == 10;
        m.dbg->port_out(m.a, 0x123B, 0x04);   // Layer 2 read mapping, low 16K
        const bool overlaid = m.dbg->source_page(0x0010) == NOT_RAM_PAGE;
        m.dbg->port_out(m.a, 0x123B, 0x00);
        check("SRCDBG-ID-03", "a RAM page under the Layer 2 read mapping is not that page",
              ram && overlaid && m.dbg->source_page(0x0010) == 10);
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
        const unsigned p8 = m.dbg->source_page(0x8000);
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
        const bool predicted = m.dbg->source_page(0x0000) == NOT_RAM_PAGE;
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
        const bool at = m.pc() == 0x1FF8 && m.dbg->source_page(0x1FF8) == NOT_RAM_PAGE;
        m.dbg->step_into(m.a);                    // the NOP at $1FF8 arms the unmap
        const bool still_mapped = m.emu.divmmc().is_active();
        check("SRCDBG-ID-08", "after a fetch in the unmap range the NEXT fetch is RAM "
                              "again, foreseen while DivMMC still reads as mapped",
              at && m.pc() == 0x1FF9 && still_mapped && m.dbg->source_page(0x1FF9) == 10,
              m.where());
    }

    // ── a step ends at DI:HALT, and within its frame budget ─────────────
    {
        Machine m;
        m.emu.mmu().write(0x8000, 0xF3);   // DI      main.bas:10
        m.emu.mmu().write(0x8001, 0x76);   // HALT    (unmapped)
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
        // A handler that redirects: at the unmapped $8001 it sends the PC to
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
        // A Memory.txt that is there and cannot be opened — a UNIX socket
        // node, which open() refuses for every user, root included.
        Machine m;
        write_text(own / "own3.nex", "");
        write_text(own / "own3.Memory.txt", "8000: ._Old\n");
        m.dbg->load_program_sidecars((own / "own3.nex").string());
        const bool had_old = m.dbg->lookup_name("Old").has_value();
        write_text(own / "own4.nex", "");
        const fs::path sock = own / "own4.Memory.txt";
        fs::remove(sock);
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un sa{};
        sa.sun_family = AF_UNIX;
        std::snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", sock.c_str());
        const bool made = fd >= 0 &&
                          ::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0;
        const auto r = m.dbg->load_program_sidecars((own / "own4.nex").string());
        if (fd >= 0) ::close(fd);
        fs::remove(sock);
        check("SRCDBG-OWN-03", "a sidecar Memory.txt that cannot be opened is reported "
                               "unreadable, and leaves none of the previous program's "
                               "symbols",
              had_old && made && r.symbols_unreadable && !m.dbg->lookup_name("Old"));
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

    // ── loops, links and relative paths ─────────────────────────────────
    {
        Machine m;
        write_text(own / "loop.nex", "");
        std::error_code ec;
        fs::create_symlink("loop.sld", own / "loop.sld", ec);
        fs::create_symlink("loop.Memory.txt", own / "loop.Memory.txt", ec);
        bool threw = false;
        Debugger::SidecarLoad r;
        try {
            r = m.dbg->load_program_sidecars((own / "loop.nex").string());
        } catch (...) {
            threw = true;
        }
        check("SRCDBG-SIDE-09", "a sidecar that is a looping link is no sidecar, not an "
                                "exception",
              !threw && r.symbols == -1 && r.sources == -1);
    }
    {
        Machine m;
        const fs::path before = fs::current_path();
        fs::current_path(dir);
        const auto r = m.dbg->load_source_map("prog.sld", false);
        fs::current_path(before);
        const std::string f = m.dbg->source_map().loaded_file();
        check("SRCDBG-PATH-01", "a map loaded by a relative name records its absolute path",
              r.count == 6 && fs::path(f).is_absolute() && fs::path(f) == dir / "prog.sld", f);
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

    fs::remove_all(dir);
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
