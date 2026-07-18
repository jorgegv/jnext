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
    check("ESXT-01", "$88 is M_DOSVERSION", name_of(0x88) == "M_DOSVERSION", name_of(0x88));
    check("ESXT-02", "$8E is M_GETHANDLE",  name_of(0x8E) == "M_GETHANDLE",  name_of(0x8E));
    check("ESXT-03", "$8F is M_EXECCMD",    name_of(0x8F) == "M_EXECCMD",    name_of(0x8F));
    check("ESXT-04", "$94 is M_P3DOS",      name_of(0x94) == "M_P3DOS",      name_of(0x94));
    check("ESXT-05", "$9A is F_OPEN",       name_of(0x9A) == "F_OPEN",       name_of(0x9A));
    check("ESXT-06", "$9B is F_CLOSE",      name_of(0x9B) == "F_CLOSE",      name_of(0x9B));
    check("ESXT-07", "$9D is F_READ",       name_of(0x9D) == "F_READ",       name_of(0x9D));
    check("ESXT-08", "$9E is F_WRITE",      name_of(0x9E) == "F_WRITE",      name_of(0x9E));
    check("ESXT-09", "$9F is F_SEEK",       name_of(0x9F) == "F_SEEK",       name_of(0x9F));
    check("ESXT-10", "$A3 is F_OPENDIR",    name_of(0xA3) == "F_OPENDIR",    name_of(0xA3));
    check("ESXT-11", "$B1 is F_GETFREE",    name_of(0xB1) == "F_GETFREE",    name_of(0xB1));

    check("ESXT-12", "codes below the esxdos range are unknown",
          esxdos_call_name(0x00) == nullptr && esxdos_call_name(0x7F) == nullptr,
          name_of(0x00) + "/" + name_of(0x7F));
    check("ESXT-13", "codes above the esxdos range are unknown",
          esxdos_call_name(0xB2) == nullptr && esxdos_call_name(0xFF) == nullptr,
          name_of(0xB2) + "/" + name_of(0xFF));
    check("ESXT-14", "unassigned codes inside the esxdos range are unknown",
          esxdos_call_name(0x86) == nullptr && esxdos_call_name(0x87) == nullptr &&
          esxdos_call_name(0x93) == nullptr,
          name_of(0x86) + "/" + name_of(0x87) + "/" + name_of(0x93));

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
        check("ESXT-15", "38 distinct non-empty names, all within $85..$B1",
              named == 38 && names.size() == 38 && !empty_name &&
              lowest == 0x85 && highest == 0xB1,
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
    check("ESXT-16", "no hook installed when neither stub nor tracing is on",
          !quiet.cpu().on_esxdos_call);

    // Tracing on, stub off: the hook exists purely to observe.
    Log::esxdos()->set_level(spdlog::level::trace);
    Emulator traced;
    traced.init(plain);
    check("ESXT-17", "tracing alone installs the hook (stub not required)",
          static_cast<bool>(traced.cpu().on_esxdos_call));

    // ESXT-17 already reported a missing hook; do not also crash on it.
    const auto traced_call = traced.cpu().on_esxdos_call
        ? traced.cpu().on_esxdos_call
        : [](uint8_t, Z80Registers&) { return true; };   // forces 18-20 to fail

    trace_out.str("");
    Z80Registers before{};
    before.AF = 0x1234; before.BC = 0x5678; before.DE = 0x9ABC; before.HL = 0xDEF0;
    Z80Registers after = before;
    const bool serviced = traced_call(0x88, after);
    check("ESXT-18", "tracing-only hook services nothing and touches no register",
          !serviced && after.AF == before.AF && after.BC == before.BC &&
          after.DE == before.DE && after.HL == before.HL,
          "serviced=" + std::to_string(serviced));

    {
        const std::string log = trace_out.str();
        check("ESXT-19", "unserviced call is traced by name as NOT IMPLEMENTED",
              log.find("-> $88 M_DOSVERSION") != std::string::npos &&
              log.find("<- $88 M_DOSVERSION") != std::string::npos &&
              log.find("NOT IMPLEMENTED") != std::string::npos,
              log);
    }

    trace_out.str("");
    Z80Registers unk{};
    traced_call(0x7F, unk);
    {
        const std::string log = trace_out.str();
        check("ESXT-20", "a code with no name is traced as (unknown)",
              log.find("$7F (unknown)") != std::string::npos, log);
    }

    // Stub AND tracing: the wrapper must delegate, not swallow the result.
    trace_out.str("");
    Emulator both;
    both.init(cfg);                 // cfg.esxdos_stub == true
    Z80Registers ver{};
    const bool handled = both.cpu().on_esxdos_call && both.cpu().on_esxdos_call(0x88, ver);
    check("ESXT-21", "with stub+tracing the stub's result survives the trace wrapper",
          handled && (ver.AF & 1) == 0 && ver.BC == 0x4E58,
          "handled=" + std::to_string(handled) +
          " BC=" + std::to_string(ver.BC));
    {
        const std::string log = trace_out.str();
        check("ESXT-22", "a serviced call is traced as ok, not NOT IMPLEMENTED",
              log.find("<- $88 M_DOSVERSION ok") != std::string::npos &&
              log.find("NOT IMPLEMENTED") == std::string::npos, log);
    }

    Log::esxdos()->set_level(spdlog::level::info);
    Log::esxdos()->sinks().pop_back();

    const int total = passed + failed;
    std::printf("Total: %d Passed: %d Failed: %d Skipped: 0\n", total, passed, failed);
    return failed == 0 ? 0 : 1;
}
