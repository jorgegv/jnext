// The startup stop of `--debugger` / `--break-at` (GH #548): src/platform/startup_stop.*.
//
// ORACLES. Nothing is derived by reading the implementation back.
//   - The design the owner agreed (2026-11-21): with no file the machine stops at power-on
//     (PC 0, before the first instruction); a NEX or a snapshot stops right after the load at
//     the PC the FILE gives (NEX header bytes 14-15; SNA: the word pushed at SP, the 48K form;
//     Z80 v1: header bytes 6-7; SZX / JNS: the PC the machine had when it was saved, which
//     this test sets); `--break-at` is a one-shot run-to that replaces that stop, takes
//     the Breakpoints panel's address forms (hex in three spellings, a symbol, `@symbol`, a
//     source line), never shows in the subscription list, and an unresolved one is refused.
//   - The panel's rules are the oracle for the address forms: `4000`, `$4000` and `0x4000` are
//     hexadecimal (symbol_table.h `resolve_key`), a bare name is a symbol unless it reads as
//     hex, `@name` is always the symbol, and a paged symbol or source line keeps the memory it
//     was assembled for (a run-to on another page never fires).
//   - SES-01: a pause belongs to the client that made it, so no other client's attach or detach
//     resumes it; a detach resumes only when no arming client remains.
// Every address and PC a row expects is a value THIS TEST put in a file or a register.
//
// Run: ./build/test/startup_stop_test

#include "platform/startup_stop.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "core/snapshot_file.h"
#include "debug/debugger.h"
#include "platform/emulator_boot.h"

#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../row_id.h"
#include "../test_portable.h"

using jnext::dbg::ClientId;
using jnext::dbg::ClientInfo;
using jnext::dbg::ClientKind;
using jnext::dbg::Debugger;
using jnext::dbg::MemSpace;
using jnext::dbg::PauseReason;
using jnext::dbg::Result;

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string hex(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%04X", v);
    return b;
}

std::string g_dir;

std::string path(const char* name) { return g_dir + "/" + name; }

void write_bytes(const std::string& file, const std::vector<uint8_t>& bytes) {
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write_text(const std::string& file, const std::string& text) {
    std::ofstream f(file, std::ios::trunc);
    f << text;
}

// A V1.2 NEX: one 16 KB bank (bank 2, at 0x8000) holding `code`, entry PC `pc`, SP 0xFF00
// (nex_loader_test's bank fixture, as debugger_backend_test's b5_write_nex).
void write_nex(const std::string& file, const std::vector<uint8_t>& code, uint16_t pc) {
    constexpr size_t BANK = 16384;
    std::vector<uint8_t> f(512 + BANK, 0x00);
    std::memcpy(f.data() + 0, "Next", 4);
    std::memcpy(f.data() + 4, "V1.2", 4);
    f[9]  = 1;                                        // number of banks
    f[11] = 7;                                        // border
    f[12] = 0x00; f[13] = 0xFF;                       // SP 0xFF00
    f[14] = static_cast<uint8_t>(pc & 0xFF);          // PC, bytes 14-15
    f[15] = static_cast<uint8_t>(pc >> 8);
    f[18 + 2] = 1;                                    // bank 2 present
    for (size_t i = 0; i < code.size(); ++i) f[512 + i] = code[i];
    write_bytes(file, f);
}

// NOP x4 then JR $ at 0x8004: a program that never ends, so "ran past" is "still running".
const std::vector<uint8_t> kProgram = { 0x00, 0x00, 0x00, 0x00, 0x18, 0xFE };
constexpr uint16_t ENTRY = 0x8000;
constexpr uint16_t TARGET = 0x8003;

// The machine, its backend, and the stop, declared in the order they are destroyed in reverse.
struct Rig {
    Emulator                  emu;
    EmulatorConfig            cfg;
    std::unique_ptr<Debugger> dbg;
    StartupStop               ss;
    std::string               why;

    explicit Rig(const std::string& break_at = {}) {
        cfg.type           = MachineType::ZX48K;
        cfg.debugger_start = true;
        cfg.break_at       = break_at;
        emu.init(cfg);
        dbg = std::make_unique<Debugger>(emu);
    }
    bool start(int coming) { return ss.start(*dbg, emu, cfg, coming, true, why); }
    // The loop owner's sequence for a `--load`: the load, its sidecars, the landing.
    bool land(const std::string& file) {
        const bool ok = emulator_apply_load(emu, file, false);
        dbg->load_program_sidecars(file);
        return ss.program_landed(file, ok, false, why);
    }
    uint16_t pc() { return emu.cpu().get_registers().PC; }
    bool paused() const { return dbg->state().paused; }
    // Frames, as the loop owner runs them, with the per-tick poll, until the machine stops.
    void run(int frames = 6) {
        for (int i = 0; i < frames && !paused(); ++i) {
            emu.run_frame();
            dbg->pump(jnext::dbg::PumpBudget{});
            ss.poll();
        }
    }
};

ClientInfo other_client(const char* name) {
    ClientInfo ci;
    ci.name = name;
    ci.kind = ClientKind::Test;
    return ci;
}

// Log lines of Log::platform() while `fn` runs.
template <class F>
std::string logged(F fn) {
    std::ostringstream out;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);
    sink->set_pattern("%l %v");
    Log::platform()->sinks().push_back(sink);
    fn();
    Log::platform()->sinks().pop_back();
    return out.str();
}

}  // namespace

int main() {
    std::printf("=== startup stop (GH #548) ===\n\n");
    g_dir = jtp::make_temp_dir("startup_stop_test");

    // DBGST-01: no file -> the stop is power-on, before the first instruction.
    {
        Rig r;
        const std::string log = logged([&] { r.start(0); });
        const auto st = r.dbg->state();
        const uint16_t before = r.pc();
        r.emu.run_frame();
        check("DBGST-01", "no file: start() leaves the machine paused at power-on, PC 0, cycle 0, a User pause "
                          "by the command-line client; a frame executes nothing; the stop is logged",
              st.paused && st.pc == 0 && st.cycle == 0 && st.pause_reason.kind == PauseReason::Kind::User &&
                  st.pause_reason.by == r.ss.client() && r.ss.client() != jnext::dbg::CLIENT_NONE &&
                  r.pc() == before && r.pc() == 0 &&
                  log.find("debugger: stopped at $0000 (power-on)") != std::string::npos,
              "paused=" + std::to_string(st.paused) + " pc=" + hex(st.pc) + " cycle=" + std::to_string(st.cycle) +
                  " log=" + log);
    }

    // DBGST-02: NEX -> stopped after the load at the header's PC; nothing ran.
    {
        const std::string nex = path("entry.nex");
        write_nex(nex, kProgram, 0x8002);
        std::ifstream f(nex, std::ios::binary);
        char hdr[16] = {};
        f.read(hdr, 16);
        const uint16_t header_pc = static_cast<uint16_t>(static_cast<uint8_t>(hdr[14]) |
                                                         (static_cast<uint8_t>(hdr[15]) << 8));
        Rig r;
        r.start(1);
        const bool before_landing = !r.paused();
        const bool ok = r.land(nex);
        check("DBGST-02", "NEX: paused right after the load at the PC of the header (bytes 14-15), by the "
                          "command-line client, the CPU not having run",
              before_landing && ok && header_pc == 0x8002 && r.paused() && r.pc() == header_pc &&
                  r.dbg->state().pause_reason.by == r.ss.client(),
              "paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()) + " why=" + r.why);
    }

    // DBGST-03: NEX with header PC 0 is load-only: the CPU is parked and the stop says so.
    {
        const std::string nex = path("loadonly.nex");
        write_nex(nex, kProgram, 0x0000);
        Rig r;
        r.start(1);
        const std::string log = logged([&] { r.land(nex); });
        check("DBGST-03", "NEX with header PC 0 (load-only): paused after the load, the CPU parked, the log names "
                          "the load-only file",
              r.paused() && r.emu.cpu_parked() &&
                  log.find("debugger: stopped at") != std::string::npos &&
                  log.find("load-only NEX file") != std::string::npos,
              log);
    }

    // DBGST-04 .. DBGST-07: a snapshot stops at the PC it restores. The PC is the one the test sets
    // in the machine before saving (SNA 48K pushes it at SP: the loader pops it back).
    auto snapshot_row = [&](const char* id, const char* desc, const char* name, uint16_t pc) {
        const std::string file = path(name);
        {
            Rig src;
            Z80Registers regs = src.emu.cpu().get_registers();
            regs.PC = pc;
            regs.SP = 0xFF00;
            src.emu.cpu().set_registers(regs);
            std::string err;
            size_t bytes = 0;
            if (!save_snapshot_file(src.emu, file, err, bytes)) {
                check(id, desc, false, "cannot save " + file + ": " + err);
                return;
            }
        }
        Rig r;
        r.start(1);
        const bool ok = r.land(file);
        check(id, desc, ok && r.paused() && r.pc() == pc && r.dbg->state().pc == pc,
              "paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()) + " want " + hex(pc));
    };
    snapshot_row("DBGST-04", "48K .sna: paused after the load at the PC the machine had when saved",
                 "pc.sna", 0x9ABC);
    {
        // .z80 version 1: 30-byte header (PC at bytes 6-7, byte 12 bit 5 clear = uncompressed) + 48 KB.
        std::vector<uint8_t> z(30 + 49152, 0x00);
        z[6] = 0xCD; z[7] = 0x9B;          // PC 0x9BCD
        z[8] = 0x00; z[9] = 0xFF;          // SP 0xFF00
        z[12] = 0x00;                      // flags: not compressed, border 0
        write_bytes(path("pc.z80"), z);
        Rig r;
        r.start(1);
        const bool ok = r.land(path("pc.z80"));
        check("DBGST-05", "v1 .z80: paused after the load at the PC of header bytes 6-7",
              ok && r.paused() && r.pc() == 0x9BCD, "paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()));
    }
    snapshot_row("DBGST-06", "48K .szx: paused after the load at the PC the machine had when saved",
                 "pc.szx", 0x9CDE);
    snapshot_row("DBGST-07", ".jns: paused after the load at the PC the machine had when saved",
                 "pc.jns", 0x9DEF);

    // DBGST-08: --inject lands as its own program: stopped at the injected PC.
    {
        write_bytes(path("inject.bin"), { 0x00, 0x00, 0x00, 0x18, 0xFE });
        Rig r;
        r.start(1);
        const bool injected = r.emu.inject_binary(path("inject.bin"), 0x8000, 0x8000);
        const bool ok = r.ss.program_landed(path("inject.bin"), injected, true, r.why);
        check("DBGST-08", "--inject: paused right after the injection at the injected PC",
              injected && ok && r.paused() && r.pc() == 0x8000, "pc=" + hex(r.pc()));
    }

    // DBGST-09: a failed load stops where the machine is, and the log says the load failed.
    {
        write_bytes(path("broken.nex"), { 'N', 'e', 'x', 't', 'V', '1', '.', '2', 0, 0 });
        Rig r;
        r.start(1);
        bool ok = false;
        const std::string log = logged([&] { ok = r.land(path("broken.nex")); });
        check("DBGST-09", "a failed load (truncated NEX): paused where the machine is, the log says the load failed",
              ok && r.paused() && log.find("(the load failed)") != std::string::npos, log);
    }

    // DBGST-10: --break-at replaces the default stop; the spellings 8003, $8003 and 0x8003 are
    // the same hexadecimal address; the stop is a one-shot run-to that no listing shows.
    {
        const std::string nex = path("target.nex");
        write_nex(nex, kProgram, ENTRY);
        std::string bad;
        for (const char* spelling : { "8003", "$8003", "0x8003" }) {
            Rig r(spelling);
            r.start(1);
            const bool ok = r.land(nex);
            const bool not_at_entry = ok && !r.paused();
            const bool hidden_while_armed = r.dbg->subscriptions(false).empty();
            r.run();
            const auto st = r.dbg->state();
            if (!(not_at_entry && hidden_while_armed && st.paused && st.pc == TARGET &&
                  st.pause_reason.kind == PauseReason::Kind::RunTo && st.pause_reason.by == r.ss.client() &&
                  r.dbg->subscriptions(false).empty()))
                bad += std::string(" [") + spelling + " ok=" + std::to_string(ok) + " armed_paused=" +
                       std::to_string(!not_at_entry) + " pc=" + hex(st.pc) + "]";
        }
        check("DBGST-10", "--break-at 8003, $8003 and 0x8003 on a NEX: not paused after the load, stops at 0x8003 "
                          "(RunTo, the command-line client's), and the subscription never shows in the listing",
              bad.empty(), bad);
    }

    // DBGST-11: a symbol of the program's own sidecar map (z88dk map line).
    {
        write_nex(path("prog.nex"), kProgram, ENTRY);
        write_text(path("prog.map"), "_main                           = $8003 ; addr, public, , prog_asm, code_user, prog.asm:7\n");
        Rig r("_main");
        r.start(1);
        const bool ok = r.land(path("prog.nex"));
        r.run();
        check("DBGST-11", "--break-at _main resolves from the sidecar map beside the program and stops at its address",
              ok && r.paused() && r.pc() == 0x8003 && r.dbg->state().pause_reason.kind == PauseReason::Kind::RunTo,
              "pc=" + hex(r.pc()) + " why=" + r.why);
    }

    // DBGST-12: a --map symbol, and @name for a name that reads as hex ("bad" = 0x0BAD).
    {
        write_text(path("user.map"),
                   "bad                             = $8003 ; addr, public, , x_asm, code_user, x.asm:1\n"
                   "foo                             = $8003 ; addr, public, , x_asm, code_user, x.asm:2\n");
        std::string bad;
        for (const char* spelling : { "foo", "@bad" }) {
            Rig r(spelling);
            r.dbg->load_map(path("user.map"), jnext::dbg::MapFormat::Z88dk);
            r.start(1);
            r.land(path("target.nex"));
            r.run();
            if (!(r.paused() && r.pc() == 0x8003)) bad += std::string(" [") + spelling + " pc=" + hex(r.pc()) + "]";
        }
        // The same name without @ is the hexadecimal address 0x0BAD, which this program never reaches.
        Rig plain("bad");
        plain.dbg->load_map(path("user.map"), jnext::dbg::MapFormat::Z88dk);
        plain.start(1);
        plain.land(path("target.nex"));
        plain.run();
        check("DBGST-12", "--break-at foo (a --map symbol) and @bad stop at the symbol; plain `bad` is the hex address "
                          "0x0BAD, as in the Breakpoints panel",
              bad.empty() && !plain.paused(), bad + " plain_paused=" + std::to_string(plain.paused()));
    }

    // DBGST-13: an unresolved name is refused before the program runs an instruction.
    {
        Rig with_file("nosuch");
        with_file.start(1);
        const bool landed = with_file.land(path("target.nex"));
        const bool ran_nothing = with_file.pc() == ENTRY;
        Rig none("nosuch");
        const bool started = none.start(0);
        check("DBGST-13", "--break-at nosuch: program_landed() and (with no file) start() are refused, the reason naming "
                          "the text; the program has not run",
              !landed && with_file.why.find("nosuch") != std::string::npos && ran_nothing && !started &&
                  none.why.find("nosuch") != std::string::npos && !with_file.ss.active() && !none.ss.active(),
              with_file.why + " | " + none.why);
    }

    // DBGST-14: decimal is not a form (the panel reads 32768 as a name, not as 0x8000).
    {
        Rig r("32768");
        r.start(1);
        const bool landed = r.land(path("target.nex"));
        check("DBGST-14", "--break-at 32768 is not 0x8000: refused as no address or symbol (the panel's parser has "
                          "no decimal form)",
              !landed && r.why.find("32768") != std::string::npos, r.why);
    }

    // DBGST-15: a paged symbol keeps its memory: it stops only while that page supplies the fetch.
    {
        write_nex(path("paged.nex"), kProgram, ENTRY);
        auto sld_with_label = [&](unsigned page) {
            std::ostringstream s;
            s << "|SLD.data.version|1\n"
              << "x.asm|3||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
                 "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
              << "x.asm|5||0|" << page << "|32771|L|,pagedname,,\n";
            write_text(path("paged.sld"), s.str());
        };
        unsigned real_page = 0;
        {
            Rig probe;
            probe.start(1);
            probe.land(path("paged.nex"));
            real_page = probe.dbg->mmu_slots()[4].space.index;
        }
        sld_with_label(real_page);
        Rig same("pagedname");
        same.start(1);
        same.land(path("paged.nex"));
        same.run();
        sld_with_label(real_page + 1);
        Rig other("pagedname");
        other.start(1);
        const bool other_landed = other.land(path("paged.nex"));
        other.run();
        check("DBGST-15", "a symbol assembled for page P stops only while page P supplies the fetch: the page that "
                          "backs 0x8000 stops at 0x8003, the next page runs through",
              same.paused() && same.pc() == 0x8003 && other_landed && !other.paused(),
              "real_page=" + std::to_string(real_page) + " same_pc=" + hex(same.pc()) +
                  " other_paused=" + std::to_string(other.paused()) + " why=" + other.why);
    }

    // DBGST-16: a source line (an SLD sidecar), tied to the page it was assembled for.
    {
        unsigned real_page = 0;
        {
            Rig probe;
            probe.start(1);
            probe.land(path("paged.nex"));
            real_page = probe.dbg->mmu_slots()[4].space.index;
        }
        std::ostringstream s;
        s << "|SLD.data.version|1\n"
          << "x.asm|3||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
             "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
          << "x.asm|20||0|" << real_page << "|32771|T|\n";
        write_text(path("paged.sld"), s.str());
        Rig r("x.asm:20");
        r.start(1);
        const bool landed = r.land(path("paged.nex"));
        r.run();
        check("DBGST-16", "--break-at file:line with a source map stops at the start of that line",
              landed && r.paused() && r.pc() == 0x8003 && r.dbg->state().pause_reason.kind == PauseReason::Kind::RunTo,
              "pc=" + hex(r.pc()) + " why=" + r.why);
    }

    // DBGST-17: the client detaches only after the machine has run again after its stop.
    {
        Rig r;
        r.start(0);
        r.ss.poll();
        const bool held_attached = r.ss.active() && r.paused();
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->run(other);
        const bool running = !r.paused();
        r.ss.poll();
        const bool detached = !r.ss.active();
        const bool others_arm = r.dbg->attached();
        r.dbg->detach(other);
        check("DBGST-17", "the startup client stays attached while the stop stands, detaches on the poll after another "
                          "client resumed the machine, and armed() then follows the other clients",
              held_attached && running && detached && others_arm && !r.dbg->attached() && !r.dbg->armed(),
              "held=" + std::to_string(held_attached) + " running=" + std::to_string(running) +
                  " detached=" + std::to_string(detached));
    }

    // DBGST-18: another client coming and going while the stop stands does not resume it (SES-01).
    {
        Rig r;
        r.start(0);
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->detach(other);
        r.ss.poll();
        check("DBGST-18", "another client attaching and detaching while the startup pause stands leaves the machine "
                          "paused and the startup client attached (the pause is not that client's)",
              r.paused() && r.ss.active() && r.dbg->state().pause_reason.by == r.ss.client());
    }

    // DBGST-19: a pause by another client before the stop cancels it.
    {
        Rig r("8003");
        r.start(1);
        r.land(path("target.nex"));
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->pause(other);
        r.ss.poll();
        const bool cancelled = !r.ss.active();
        r.dbg->run(other);
        r.run();
        check("DBGST-19", "a pause by another client before the --break-at stop cancels it: the client detaches and "
                          "the machine runs past the target",
              cancelled && !r.paused() && r.pc() >= 0x8003,
              "cancelled=" + std::to_string(cancelled) + " paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()));
    }

    // DBGST-20: a cold boot before the stop cancels it.
    {
        Rig r("8003");
        r.start(1);
        r.land(path("target.nex"));
        r.ss.on_cold_boot();
        const bool cancelled = !r.ss.active();
        r.run();
        check("DBGST-20", "a cold boot before the --break-at stop cancels it: the client is gone and nothing stops",
              cancelled && !r.paused(), "cancelled=" + std::to_string(cancelled) + " paused=" + std::to_string(r.paused()));
    }

    // DBGST-21: a target that IS the PC the program starts at stops there at once (a resume
    // steps off the instruction it is at, so a plain run-to would run straight past it).
    {
        Rig r("8000");
        r.start(1);
        bool ok = false;
        const std::string log = logged([&] { ok = r.land(path("target.nex")); });
        r.run(2);
        check("DBGST-21", "--break-at 8000 on a NEX that starts at 0x8000 stops there right after the load, "
                          "not past it, and the log names the --break-at",
              ok && r.paused() && r.pc() == ENTRY && r.dbg->state().pause_reason.by == r.ss.client() &&
                  log.find("debugger: stopped at $8000 (--break-at 8000)") != std::string::npos,
              "paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()) + " " + log);
    }

    std::filesystem::remove_all(g_dir);
    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
