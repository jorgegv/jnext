#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/esxdos_trace.h"
#include "core/log.h"
#include "platform/emulator_boot.h"

#include <spdlog/sinks/ostream_sink.h>

#include <cstdio>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>

namespace {

int passed = 0;
int failed = 0;

void check(const char* id, bool condition) {
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("FAIL %s\n", id);
    }
}

// Task 85 rows carry a description and a failure detail; the older rows above
// predate that idiom and are left as they are.
void check(const char* id, const char* desc, bool condition,
           const std::string& detail = "") {
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("FAIL %s: %s%s%s\n", id, desc,
                    detail.empty() ? "" : " — ", detail.c_str());
    }
}

std::string name_of(uint8_t code) {
    const char* n = esxdos_call_name(code);
    return n ? std::string(n) : std::string("(null)");
}

void write_string(Emulator& emu, uint16_t address, const std::string& value) {
    for (std::size_t i = 0; i < value.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(address + i), value[i]);
    emu.mmu().write(static_cast<uint16_t>(address + value.size()), 0);
}

bool call(Emulator& emu, uint8_t function, Z80Registers& regs) {
    return emu.cpu().on_esxdos_call && emu.cpu().on_esxdos_call(function, regs);
}

bool carry(const Z80Registers& regs) {
    return (regs.AF & 1) != 0;
}

} // namespace

int main() {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.esxdos_stub = true;
    cfg.rewind_buffer_frames = 0;
    cfg.load_file = "/tmp/jnext-esxdos/selector.nex";

    Emulator emu;
    emu.init(cfg);

    Z80Registers regs{};
    check("ESX-01-version",
          call(emu, 0x88, regs) && !carry(regs) && regs.BC == 0x4E58);

    constexpr uint16_t name_addr = 0x8000;
    constexpr uint16_t data_addr = 0x8100;
    write_string(emu, name_addr, "empty.cfg");
    regs = {};
    regs.IX = name_addr;
    regs.BC = 0x0E00;
    const bool created_empty = call(emu, 0x9A, regs) && !carry(regs);
    regs.AF = 0x0100;
    const bool closed_empty = call(emu, 0x9B, regs) && !carry(regs);
    regs = {};
    regs.IX = name_addr;
    regs.BC = 0x0100;
    check("ESX-02-empty-file",
          created_empty && closed_empty && call(emu, 0x9A, regs) && !carry(regs));

    write_string(emu, name_addr, "options.cfg");
    regs = {};
    regs.IX = name_addr;
    regs.BC = 0x0E00;
    const bool opened_write = call(emu, 0x9A, regs) && !carry(regs);
    const uint8_t handle = static_cast<uint8_t>(regs.AF >> 8);
    emu.mmu().write(data_addr + 0, 0x43);
    emu.mmu().write(data_addr + 1, 0x42);
    emu.mmu().write(data_addr + 2, 0x01);
    regs = {};
    regs.AF = static_cast<uint16_t>(handle << 8);
    regs.IX = data_addr;
    regs.BC = 3;
    const bool wrote = call(emu, 0x9E, regs) && !carry(regs) && regs.BC == 3;
    regs.AF = static_cast<uint16_t>(handle << 8);
    const bool closed = call(emu, 0x9B, regs) && !carry(regs);
    check("ESX-03-write", opened_write && wrote && closed);

    emulator_cold_boot(emu, cfg);
    write_string(emu, name_addr, "options.cfg");
    regs = {};
    regs.IX = name_addr;
    regs.BC = 0x0100;
    const bool opened_read = call(emu, 0x9A, regs) && !carry(regs);
    regs = {};
    regs.AF = static_cast<uint16_t>(handle << 8);
    regs.IX = data_addr;
    regs.BC = 3;
    const bool read = call(emu, 0x9D, regs) && !carry(regs) && regs.BC == 3;
    check("ESX-04-cold-boot-read",
          opened_read && read && emu.mmu().read(data_addr) == 0x43 &&
          emu.mmu().read(data_addr + 1) == 0x42 &&
          emu.mmu().read(data_addr + 2) == 0x01);

    write_string(emu, name_addr, "run game.nex");
    regs = {};
    regs.IX = name_addr;
    const bool run_ok = call(emu, 0x8F, regs) && !carry(regs);
    const auto expected = std::filesystem::path("/tmp/jnext-esxdos/game.nex");
    check("ESX-05-run-sibling",
          run_ok && std::filesystem::path(emu.take_nex_load_request()) == expected);
    check("ESX-06-request-consumed", emu.take_nex_load_request().empty());

    write_string(emu, name_addr, "RUN \"Game Two.NEX\"");
    regs = {};
    regs.IX = name_addr;
    const auto quoted = std::filesystem::path("/tmp/jnext-esxdos/Game Two.NEX");
    check("ESX-07-quoted-name",
          call(emu, 0x8F, regs) && !carry(regs) &&
          std::filesystem::path(emu.take_nex_load_request()) == quoted);

    write_string(emu, name_addr, "run ../escape.nex");
    regs = {};
    regs.IX = name_addr;
    check("ESX-08-reject-parent",
          call(emu, 0x8F, regs) && carry(regs) && emu.take_nex_load_request().empty());

    write_string(emu, name_addr, "run sibling.tap");
    regs = {};
    regs.IX = name_addr;
    check("ESX-09-reject-non-nex",
          call(emu, 0x8F, regs) && carry(regs) && emu.take_nex_load_request().empty());

    write_string(emu, name_addr, "delete sibling.nex");
    regs = {};
    regs.IX = name_addr;
    check("ESX-10-reject-command",
          call(emu, 0x8F, regs) && carry(regs) && emu.take_nex_load_request().empty());

    // ── Task 85: esxdos / NextZXOS syscall tracing ──────────────────────
    //
    // Two things are under test: the pure name table in core/esxdos_trace.h,
    // and the hook-installation / delegation behaviour added to
    // Emulator::init() (tracing must work with the stub OFF, must service
    // nothing in that mode, and must not swallow the stub's results when both
    // are on). The emitted trace text is captured through an extra spdlog sink
    // attached to the esxdos logger.

    // --- name table ---------------------------------------------------
    //
    // EVERY expectation below is transcribed from the two oracle files cited
    // in core/esxdos_trace.h, NOT from that header's implementation:
    //   [1] tbblue   src/asm/dot_commands/esxapi.def
    //   [2] z88dk-2.3 lib/target/zx/def/esxdos.def
    // An earlier revision of these rows was traced from the implementation and
    // therefore certified a table in which nine codes were wrong and two names
    // did not exist in any source. Do not "check what the code returns".
    check("ESXT-01", "$81 is DISK_READ",     name_of(0x81) == "DISK_READ",     name_of(0x81));   // [2] =129
    check("ESXT-02", "$85 is DISK_FILEMAP",  name_of(0x85) == "DISK_FILEMAP",  name_of(0x85));   // [1] disk_filemap
    check("ESXT-03", "$88 is M_DOSVERSION",  name_of(0x88) == "M_DOSVERSION",  name_of(0x88));   // [1] m_dosversion
    check("ESXT-04", "$89 is M_GETSETDRV",   name_of(0x89) == "M_GETSETDRV",   name_of(0x89));   // [1] m_getsetdrv
    check("ESXT-05", "$8A is M_DRIVEINFO",   name_of(0x8A) == "M_DRIVEINFO",   name_of(0x8A));   // [2] =138
    check("ESXT-06", "$8D is M_GETHANDLE",   name_of(0x8D) == "M_GETHANDLE",   name_of(0x8D));   // [1] m_gethandle
    check("ESXT-07", "$8E is M_GETDATE",     name_of(0x8E) == "M_GETDATE",     name_of(0x8E));   // [1] m_getdate
    check("ESXT-08", "$8F is M_EXECCMD",     name_of(0x8F) == "M_EXECCMD",     name_of(0x8F));   // [1] m_execcmd
    check("ESXT-09", "$92 is M_DRVAPI",      name_of(0x92) == "M_DRVAPI",      name_of(0x92));   // [1] m_drvapi
    check("ESXT-10", "$93 is M_GETERR",      name_of(0x93) == "M_GETERR",      name_of(0x93));   // [1] m_geterr
    check("ESXT-11", "$94 is M_P3DOS",       name_of(0x94) == "M_P3DOS",       name_of(0x94));   // [1] m_p3dos
    check("ESXT-12", "$98 is F_MOUNT",       name_of(0x98) == "F_MOUNT",       name_of(0x98));   // [2] =152
    check("ESXT-13", "$9A is F_OPEN",        name_of(0x9A) == "F_OPEN",        name_of(0x9A));   // [1] f_open
    check("ESXT-14", "$9D is F_READ",        name_of(0x9D) == "F_READ",        name_of(0x9D));   // [1] f_read
    check("ESXT-15", "$9F is F_SEEK",        name_of(0x9F) == "F_SEEK",        name_of(0x9F));   // [1] f_seek
    check("ESXT-16", "$B1 is F_GETFREE",     name_of(0xB1) == "F_GETFREE",     name_of(0xB1));   // [1] f_getfree

    check("ESXT-17", "codes below the esxdos range are unknown",
          esxdos_call_name(0x00) == nullptr && esxdos_call_name(0x7F) == nullptr,
          name_of(0x00) + "/" + name_of(0x7F));
    check("ESXT-18", "codes above the esxdos range are unknown",
          esxdos_call_name(0xB2) == nullptr && esxdos_call_name(0xFF) == nullptr,
          name_of(0xB2) + "/" + name_of(0xFF));
    // $96/$97 are the ONLY gap inside $80..$B1: assigned by neither oracle.
    check("ESXT-19", "the $96/$97 gap inside the esxdos range is unknown",
          esxdos_call_name(0x96) == nullptr && esxdos_call_name(0x97) == nullptr,
          name_of(0x96) + "/" + name_of(0x97));

    // Every named code maps to a distinct, non-empty name, and the table has
    // exactly the population the two checks above bracket. A copy-paste slip
    // that duplicates a name or widens the table is caught here.
    {
        std::set<std::string> names;
        int named = 0, lowest = 0x100, highest = -1;
        bool empty_name = false;
        for (int c = 0; c <= 0xFF; ++c) {
            const char* n = esxdos_call_name(static_cast<uint8_t>(c));
            if (!n) continue;
            ++named;
            if (*n == '\0') empty_name = true;
            names.insert(n);
            if (c < lowest)  lowest = c;
            if (c > highest) highest = c;
        }
        // 48 = 8 ($80..$87) + 14 ($88..$95) + 2 ($98/$99) + 24 ($9A..$B1),
        // counted off the two oracle files, not off the switch statement.
        check("ESXT-20", "48 distinct non-empty names, all within $80..$B1",
              named == 48 && names.size() == 48 && !empty_name &&
              lowest == 0x80 && highest == 0xB1,
              "named=" + std::to_string(named) +
              " distinct=" + std::to_string(names.size()) +
              " range=" + std::to_string(lowest) + ".." + std::to_string(highest));
    }

    // --- hook installation / delegation --------------------------------
    std::ostringstream trace_out;
    auto trace_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(trace_out);
    Log::esxdos()->sinks().push_back(trace_sink);

    EmulatorConfig plain = cfg;
    plain.esxdos_stub = false;

    // Both off: no hook at all — RST $08 must run the real $0008 code.
    Log::esxdos()->set_level(spdlog::level::info);
    Emulator quiet;
    quiet.init(plain);
    check("ESXT-21", "no hook installed when neither stub nor tracing is on",
          !quiet.cpu().on_esxdos_call);

    // Tracing on, stub off: the hook exists purely to observe.
    Log::esxdos()->set_level(spdlog::level::trace);
    Emulator traced;
    traced.init(plain);
    check("ESXT-22", "tracing alone installs the hook (stub not required)",
          static_cast<bool>(traced.cpu().on_esxdos_call));

    // ESXT-22 already reported a missing hook; do not also crash on it.
    const auto traced_call = traced.cpu().on_esxdos_call
        ? traced.cpu().on_esxdos_call
        : [](uint8_t, Z80Registers&) { return true; };   // forces 23-25 to fail

    trace_out.str("");
    Z80Registers before{};
    before.AF = 0x1234; before.BC = 0x5678; before.DE = 0x9ABC; before.HL = 0xDEF0;
    Z80Registers after = before;
    const bool serviced = traced_call(0x88, after);
    check("ESXT-23", "tracing-only hook services nothing and touches no register",
          !serviced && after.AF == before.AF && after.BC == before.BC &&
          after.DE == before.DE && after.HL == before.HL,
          "serviced=" + std::to_string(serviced));

    {
        const std::string log = trace_out.str();
        check("ESXT-24", "unserviced call is traced by name as not serviced",
              log.find("-> $88 M_DOSVERSION") != std::string::npos &&
              log.find("<- $88 M_DOSVERSION") != std::string::npos &&
              log.find("not serviced by jnext") != std::string::npos,
              log);
    }

    // $96 is IN the esxdos code space (so it can actually reach the hook —
    // see ESXT-29..36) but is assigned by neither oracle, so it has no name.
    trace_out.str("");
    Z80Registers unk{};
    traced_call(0x96, unk);
    {
        const std::string log = trace_out.str();
        check("ESXT-25", "an in-range but unassigned code is traced as (unknown)",
              log.find("$96 (unknown)") != std::string::npos, log);
    }

    // Stub AND tracing: the wrapper must delegate, not swallow the result.
    trace_out.str("");
    Emulator both;
    both.init(cfg);                 // cfg.esxdos_stub == true
    Z80Registers ver{};
    const bool handled = both.cpu().on_esxdos_call && both.cpu().on_esxdos_call(0x88, ver);
    check("ESXT-26", "with stub+tracing the stub's result survives the trace wrapper",
          handled && (ver.AF & 1) == 0 && ver.BC == 0x4E58,
          "handled=" + std::to_string(handled) +
          " BC=" + std::to_string(ver.BC));
    {
        const std::string log = trace_out.str();
        check("ESXT-27", "a serviced call is traced as ok, not NOT IMPLEMENTED",
              log.find("<- $88 M_DOSVERSION ok") != std::string::npos &&
              log.find("not serviced by jnext") == std::string::npos, log);
    }

    // The "->" line must report the registers as they were ON ENTRY, and the
    // "<-" line as they were on return. The stub mutates the Z80Registers& in
    // place, so if the entry trace were emitted after the handler ran, the
    // arguments would be silently reported as the results — a trace that lies
    // about what the program asked for is worse than no trace.
    //
    // Discriminative: $88 M_DOSVERSION overwrites BC with $4E58, so a sentinel
    // BC of $1111 appears on the "->" line ONLY while the log call precedes the
    // handler. Moving that call after handle_esxdos turns both lines into
    // BC=4E58 and fails this row. (Before this row existed, that exact mutation
    // passed 27/27.)
    trace_out.str("");
    Z80Registers sentinel{};
    sentinel.BC = 0x1111;
    both.cpu().on_esxdos_call(0x88, sentinel);
    {
        const std::string log = trace_out.str();
        const auto in  = log.find("-> $88");
        const auto out = log.find("<- $88");
        const bool ordered = in != std::string::npos && out != std::string::npos && in < out;
        const std::string in_line  = ordered ? log.substr(in, out - in) : std::string();
        const std::string out_line = ordered ? log.substr(out)          : std::string();
        check("ESXT-28", "entry trace reports the arguments, not the results",
              ordered &&
              in_line.find("BC=1111")  != std::string::npos &&
              in_line.find("BC=4E58")  == std::string::npos &&
              out_line.find("BC=4E58") != std::string::npos,
              log);
    }

    // ── CPU-driven trigger rows (ESXT-29..36) ──────────────────────────
    //
    // Everything above calls on_esxdos_call() directly with a hand-picked
    // code. That leaves the TRIGGER — the decision, taken in Z80Cpu, of
    // whether an arrival at $0008 is an esxdos call at all — completely
    // untested. These rows assemble a real `RST $08 : DEFB n` in RAM and
    // execute it.
    //
    // Spec (NOT read off the implementation):
    //   $0008 is the ZX Spectrum ROM's ERROR restart. The published calling
    //   convention there is `RST $08 : DEFB errcode` with the error report
    //   code inline (e.g. $00 "NEXT without FOR", $0D "BREAK - CONT
    //   repeats", $22 an in-range report code). esxdos SHARES that vector
    //   and is distinguished solely by an inline code >= $80:
    //     tbblue   src/asm/dot_commands/esxapi.def:11-14 — callesx macro
    //              is `rst $8` / `defb hook_code`; hooks span $85..$b1
    //     z88dk-2.3 lib/target/zx/def/esxdos.def:104-162 — same span,
    //              __ESX_DISK_FILEMAP=0x85 .. __ESX_F_GETFREE=0xb1
    //   Neither oracle assigns any hook below $80, so a sub-$80 inline byte
    //   is a ROM error report and MUST NOT be reported as an esxdos call.
    constexpr uint16_t code_addr  = 0x8000;   // RST $08 lives here
    constexpr uint16_t stack_top  = 0x9000;

    struct Probe { int fires = 0; uint8_t code = 0; };

    // Executes `RST $08 : DEFB inline` at code_addr and reports whether the
    // esxdos trigger fired. `hijack` selects what the hook claims: false =
    // observe only (tracing), true = service it (stub).
    auto run_rst08 = [&](Emulator& e, uint8_t inline_byte, bool hijack,
                         Probe& p, Z80Registers& out) {
        e.mmu().write(code_addr, 0xCF);            // RST $08
        e.mmu().write(code_addr + 1, inline_byte); // DEFB n
        e.cpu().on_esxdos_call = [&p, hijack](uint8_t c, Z80Registers&) {
            ++p.fires; p.code = c; return hijack;
        };
        Z80Registers r{};
        r.PC = code_addr;
        r.SP = stack_top;
        e.cpu().set_registers(r);
        e.cpu().execute();      // RST $08 → pushes $8001, PC = $0008
        e.cpu().execute();      // the arrival at $0008 the trigger inspects
        out = e.cpu().get_registers();
    };

    Emulator cpu_emu;
    cpu_emu.init(plain);        // stub off — trigger behaviour only

    // ROM error reports: three real inline error codes. None is an esxdos
    // call, so none may be traced as one.
    {
        Probe p; Z80Registers out{};
        run_rst08(cpu_emu, 0x0D, false, p, out);
        check("ESXT-29", "RST $08 : DEFB $0D (ROM error report) is not an esxdos call",
              p.fires == 0, "fires=" + std::to_string(p.fires));
    }
    {
        Probe p; Z80Registers out{};
        run_rst08(cpu_emu, 0x00, false, p, out);
        check("ESXT-30", "RST $08 : DEFB $00 (ROM error report) is not an esxdos call",
              p.fires == 0, "fires=" + std::to_string(p.fires));
    }
    {
        Probe p; Z80Registers out{};
        run_rst08(cpu_emu, 0x22, false, p, out);
        // Not hijacked: execution continued into the $0008 handler rather
        // than resuming after the DEFB.
        check("ESXT-31", "RST $08 : DEFB $22 runs the ROM $0008 handler, not the shim",
              p.fires == 0 && out.PC != code_addr + 2,
              "fires=" + std::to_string(p.fires) +
              " PC=" + std::to_string(out.PC));
    }

    // A genuine esxdos call on the very same vector must still be seen.
    {
        Probe p; Z80Registers out{};
        run_rst08(cpu_emu, 0x9D, false, p, out);   // [1][2] f_read
        check("ESXT-32", "RST $08 : DEFB $9D (F_READ) is an esxdos call",
              p.fires == 1 && p.code == 0x9D,
              "fires=" + std::to_string(p.fires) +
              " code=" + std::to_string(p.code));
    }

    // The $80 boundary itself, from both sides.
    {
        Probe p; Z80Registers out{};
        run_rst08(cpu_emu, 0x7F, false, p, out);
        check("ESXT-33", "$7F is below the esxdos code space and does not trigger",
              p.fires == 0, "fires=" + std::to_string(p.fires));
    }
    {
        Probe p; Z80Registers out{};
        run_rst08(cpu_emu, 0x80, false, p, out);
        check("ESXT-34", "$80 is the first esxdos code and does trigger",
              p.fires == 1 && p.code == 0x80,
              "fires=" + std::to_string(p.fires) +
              " code=" + std::to_string(p.code));
    }

    // Arrival sanity: reaching $0008 other than by RST $08 (here: the top of
    // stack does not point one past a $CF opcode) is not an API call, even
    // when the byte at the "return address" happens to be a valid hook code.
    {
        Probe p;
        cpu_emu.mmu().write(code_addr, 0x00);          // NOP, not RST $08
        cpu_emu.mmu().write(code_addr + 1, 0x9D);      // looks like F_READ
        cpu_emu.cpu().on_esxdos_call = [&p](uint8_t c, Z80Registers&) {
            ++p.fires; p.code = c; return false;
        };
        Z80Registers r{};
        r.PC = 0x0008;
        r.SP = stack_top - 2;
        cpu_emu.mmu().write(stack_top - 2, 0x01);      // pushed word = $8001
        cpu_emu.mmu().write(stack_top - 1, 0x80);
        cpu_emu.cpu().set_registers(r);
        cpu_emu.cpu().execute();
        check("ESXT-35", "arrival at $0008 not preceded by an RST $08 opcode does not trigger",
              p.fires == 0, "fires=" + std::to_string(p.fires));
    }

    // With the stub live, a sub-$80 inline byte must never be hijacked: the
    // shim must not rewrite PC/SP past the DEFB and swallow a ROM error.
    {
        Emulator stub_emu;
        stub_emu.init(cfg);          // cfg.esxdos_stub == true
        Probe p; Z80Registers out{};
        run_rst08(stub_emu, 0x0D, true, p, out);
        check("ESXT-36", "a servicing hook is unreachable for a sub-$80 ROM error code",
              p.fires == 0 && out.PC != code_addr + 2 && out.SP != stack_top,
              "fires=" + std::to_string(p.fires) +
              " PC=" + std::to_string(out.PC) +
              " SP=" + std::to_string(out.SP));
    }

    Log::esxdos()->set_level(spdlog::level::info);
    Log::esxdos()->sinks().pop_back();

    const int total = passed + failed;
    std::printf("Total: %d Passed: %d Failed: %d Skipped: 0\n", total, passed, failed);
    return failed == 0 ? 0 : 1;
}
