#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/esxdos_trace.h"
#include "core/extended_nex_host.h"
#include "core/log.h"
#include "platform/emulator_boot.h"

#include <spdlog/sinks/ostream_sink.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>   // getpid() — per-process fixture paths

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

// ── GH #250 fixture: a direct-load NEX that makes Warhawk's calls ──────
//
// One 16K bank (bank 2, $8000-$BFFF), entered at $8000 with interrupts off.
// Each esxDOS call is followed by `LD (res),A : PUSH AF : POP BC : LD A,C :
// LD (res+1),A`, so the program itself records the A and F it got back.
// It finishes by writing kDoneMark to kDone and spinning on `JR $` at
// spin_pc. With `own_rst08` it first maps RAM page $20 at $0000 (NR $50)
// and copies in its own RST $08 handler, which writes kOwnMark to kOwn and
// returns past the DEFB — i.e. a program that owns the $0008 vector.
//
// `call` picks what it asks for: the Warhawk-shaped sequence (only without
// own_rst08), a single M_GETSETDRV get, a single F_READ of 4 bytes from
// handle `handle` into kBuf, a single M_DOSVERSION, or a single call of hook
// code `handle` with A=0. F_READ and M_DOSVERSION store the returned BC at
// kRes+2.
enum class Call250 { Sequence, GetDrv, FRead, DosVersion, Hook };
constexpr uint16_t kRes      = 0x9000;
constexpr uint16_t kBuf      = 0x9010;
constexpr uint16_t kOwn      = 0x900E;
constexpr uint16_t kDone     = 0x900F;
constexpr uint8_t  kOwnMark  = 0x5A;
constexpr uint8_t  kDoneMark = 0xA5;

struct Probe250 {
    std::vector<uint8_t> code;
    uint16_t spin_pc = 0;
};

Probe250 build_probe_250(bool own_rst08, Call250 call, uint8_t handle = 0) {
    std::vector<uint8_t> c;
    auto b = [&](std::initializer_list<uint8_t> bytes) {
        c.insert(c.end(), bytes.begin(), bytes.end());
    };
    auto lo = [](uint16_t v) { return static_cast<uint8_t>(v); };
    auto hi = [](uint16_t v) { return static_cast<uint8_t>(v >> 8); };
    auto store = [&](uint16_t at) {             // (at)=A, (at+1)=F
        b({0x32, lo(at), hi(at), 0xF5, 0xC1, 0x79,
           0x32, lo(static_cast<uint16_t>(at + 1)), hi(static_cast<uint16_t>(at + 1))});
    };
    constexpr uint16_t kName    = 0x8100;       // "missing.sav"
    constexpr uint16_t kHandler = 0x8200;       // own RST $08 routine

    b({0xF3});                                  // DI
    if (own_rst08) {
        b({0xED, 0x91, 0x50, 0x20});            // NEXTREG $50,$20 — RAM at $0000
        b({0x21, lo(kHandler), hi(kHandler),    // LD HL,handler
           0x11, 0x08, 0x00,                    // LD DE,$0008
           0x01, 0x0A, 0x00,                    // LD BC,10
           0xED, 0xB0});                        // LDIR
    }
    if (call == Call250::GetDrv) {
        b({0xAF, 0xCF, 0x89}); store(kRes);     // XOR A : RST $08 : DEFB $89
    } else if (call == Call250::DosVersion) {
        b({0xAF, 0xCF, 0x88,                    // XOR A : RST $08 : DEFB $88
           0xED, 0x43, lo(static_cast<uint16_t>(kRes + 2)),
                       hi(static_cast<uint16_t>(kRes + 2))});  // LD (res+2),BC
        store(kRes);
    } else if (call == Call250::Hook) {
        b({0xAF, 0xCF, handle}); store(kRes);   // XOR A : RST $08 : DEFB handle
    } else if (call == Call250::FRead) {
        b({0x3E, handle,                        // LD A,handle
           0xDD, 0x21, lo(kBuf), hi(kBuf),      // LD IX,buf
           0x01, 0x04, 0x00,                    // LD BC,4
           0xCF, 0x9D,                          // RST $08 : DEFB $9D (F_READ)
           0xED, 0x43, lo(static_cast<uint16_t>(kRes + 2)),
                       hi(static_cast<uint16_t>(kRes + 2))});  // LD (res+2),BC
        store(kRes);
    } else {
        b({0xAF, 0xCF, 0x89}); store(kRes + 0);          // M_GETSETDRV get
        b({0x3E, 0x19, 0xCF, 0x89}); store(kRes + 2);    // set D:
        b({0x3E, 0x11, 0xCF, 0x89}); store(kRes + 4);    // set C:
        b({0x3E, 0x2A,                                    // LD A,'*'
           0xDD, 0x21, lo(kName), hi(kName),              // LD IX,name
           0x06, 0x01, 0xCF, 0x9A}); store(kRes + 6);     // B=read : F_OPEN
        b({0xAF, 0xCF, 0x96}); store(kRes + 8);          // unassigned $96
    }
    b({0x3E, kDoneMark, 0x32, lo(kDone), hi(kDone)});    // done marker
    Probe250 p;
    p.spin_pc = static_cast<uint16_t>(0x8000 + c.size());
    b({0x18, 0xFE});                                      // JR $

    c.resize(0x4000, 0x00);
    const char name[] = "missing.sav";
    std::copy(name, name + sizeof(name), c.begin() + (kName - 0x8000));
    const uint8_t handler[] = {
        0x3E, kOwnMark, 0x32, lo(kOwn), hi(kOwn),   // LD A,mark : LD (own),A
        0xE1, 0x23, 0xE5,                           // POP HL : INC HL : PUSH HL
        0xC9, 0x00                                  // RET (past the DEFB)
    };
    std::copy(handler, handler + sizeof(handler), c.begin() + (kHandler - 0x8000));
    p.code = std::move(c);
    return p;
}

// V1.2 header, one bank (bank 2), PC $8000, SP $BFF0. file_handle 0 closes
// the file after loading, as for Warhawk.nex; file_handle 1 keeps it open
// (handle in BC), which is what opens the extended-NEX host bridge, with
// `appended` as the payload after the bank.
bool write_probe_nex(const std::string& path, const std::vector<uint8_t>& bank2,
                     uint16_t file_handle = 0,
                     const std::vector<uint8_t>& appended = {}) {
    std::vector<uint8_t> f(512, 0);
    std::copy_n("NextV1.2", 8, f.begin());
    f[9]  = 1;                                // num_banks
    f[12] = 0xF0; f[13] = 0xBF;               // SP
    f[14] = 0x00; f[15] = 0x80;               // PC
    f[18 + 2] = 1;                            // bank 2 present
    f[140] = static_cast<uint8_t>(file_handle);
    f[141] = static_cast<uint8_t>(file_handle >> 8);
    f.insert(f.end(), bank2.begin(), bank2.end());
    f.insert(f.end(), appended.begin(), appended.end());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(f.data()),
              static_cast<std::streamsize>(f.size()));
    return static_cast<bool>(out);
}

std::string hex2s(uint8_t v) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02X", v);
    return buf;
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

    // ── GH #250 — a directly loaded NEX gets esxDOS answers by default ──
    //
    // `jnext --load Warhawk.nex` hung: Warhawk's first act is `XOR A : RST
    // $08 : DEFB $89` (M_GETSETDRV), which without --esxdos-stub ran the 48K
    // ROM's ERROR-1 and parked the CPU on a DI + HALT at $1303. On hardware a
    // NEX is only ever started by NextZXOS's nexload, so the esxDOS API is
    // always behind it. These rows load a fixture through Emulator::load_nex()
    // exactly as --load does, WITHOUT cfg.esxdos_stub, and run it.
    //
    // ORACLE — the replies below were measured on real NextZXOS: the distro
    // SD image booted natively (tbblue.fw + NextZXOS), a probe NEX copied to
    // the card and started with `.nexload`, results read back through a
    // magic port. The API document (NextZXOS_and_esxDOS_APIs.pdf, M_GETSETDRV:
    // "bits 7..3=drive letter (0=A...15=P)") and esxapi.def (error numbers:
    // esx_enonsense=2 :172, esx_enoent=5 :175, esx_enodrv=11 :181) name
    // them; the measurement pins which one each call returns:
    //   M_GETSETDRV get        -> A=$10 (C:), Fc=0
    //   M_GETSETDRV set D: $19 -> A=$0B, Fc=1       (no such drive)
    //   M_GETSETDRV set C: $11 -> A=$10, Fc=0
    //   F_OPEN of a missing file (Warhawk's "Warhawk.sav", same run) -> A=5, Fc=1
    //   unassigned hook $96    -> A=$02, Fc=1       (also $80, $8A, $97)
    {
        const std::string probe_path =
            (std::filesystem::temp_directory_path() /
             ("jnext_esxdos250_" + std::to_string(::getpid()) + ".nex")).string();
        const std::string own_path =
            (std::filesystem::temp_directory_path() /
             ("jnext_esxdos250_own_" + std::to_string(::getpid()) + ".nex")).string();
        const Probe250 probe = build_probe_250(/*own_rst08=*/false, Call250::Sequence);
        const Probe250 own   = build_probe_250(/*own_rst08=*/true, Call250::GetDrv);
        const bool built = write_probe_nex(probe_path, probe.code) &&
                           write_probe_nex(own_path, own.code);

        // CLI shape: --load sets cfg.load_file; no --esxdos-stub.
        EmulatorConfig cli = plain;
        cli.load_file = probe_path;
        // Heap, not stack: an Emulator is several MB and this main() already
        // holds six of them on the stack.
        auto e250_ptr = std::make_unique<Emulator>();
        Emulator& e250 = *e250_ptr;
        const bool loaded = built && e250.init(cli) && e250.load_nex(probe_path);
        for (int i = 0; i < 3; ++i) e250.run_frame();
        auto rd = [&](Emulator& e, uint16_t a) { return e.mmu().read(a); };
        const uint16_t pc = e250.cpu().get_registers().PC;
        auto af = [&](uint16_t at) {
            return "A=" + hex2s(rd(e250, at)) + " F=" + hex2s(rd(e250, at + 1));
        };

        check("ESXN-01",
              "direct-load NEX (no --esxdos-stub) survives its esxDOS calls and "
              "reaches its own end — GH #250 Warhawk hung in the 48K ROM at $1303",
              loaded && rd(e250, kDone) == kDoneMark && pc == probe.spin_pc,
              "loaded=" + std::to_string(loaded) + " done=" + hex2s(rd(e250, kDone)) +
              " PC=" + std::to_string(pc) + " want " + std::to_string(probe.spin_pc));
        check("ESXN-02",
              "M_GETSETDRV get (A=0) returns the default drive C: = $10 with Fc=0 "
              "(NextZXOS measured; API doc M_GETSETDRV encoding)",
              rd(e250, kRes + 0) == 0x10 && (rd(e250, kRes + 1) & 1) == 0, af(kRes + 0));
        check("ESXN-03",
              "M_GETSETDRV set D: ($19) fails Fc=1 A=$0B esx_enodrv "
              "(NextZXOS measured; esxapi.def:181)",
              rd(e250, kRes + 2) == 0x0B && (rd(e250, kRes + 3) & 1) == 1, af(kRes + 2));
        check("ESXN-04",
              "M_GETSETDRV set C: ($11) succeeds Fc=0 A=$10, low bits ignored "
              "(NextZXOS measured; API doc: bits 2..0 ignored)",
              rd(e250, kRes + 4) == 0x10 && (rd(e250, kRes + 5) & 1) == 0, af(kRes + 4));
        check("ESXN-05",
              "F_OPEN of a file that does not exist fails Fc=1 A=5 esx_enoent — "
              "Warhawk's hi-score open (NextZXOS measured; esxapi.def:175)",
              rd(e250, kRes + 6) == 0x05 && (rd(e250, kRes + 7) & 1) == 1, af(kRes + 6));
        check("ESXN-06",
              "a hook code nothing implements ($96) fails Fc=1 A=$02 esx_enonsense "
              "instead of entering the ROM (NextZXOS measured; esxapi.def:172)",
              rd(e250, kRes + 8) == 0x02 && (rd(e250, kRes + 9) & 1) == 1, af(kRes + 8));

        // GUI shape: File > Load after init() of a machine with no --load, so
        // init() attached no hook; load_nex() itself must arm and attach it.
        EmulatorConfig gui = plain;
        gui.load_file.clear();
        auto g250_ptr = std::make_unique<Emulator>();
        Emulator& g250 = *g250_ptr;
        const bool gui_no_hook = g250.init(gui) && !g250.cpu().on_esxdos_call;
        const bool gui_loaded  = built && g250.load_nex(probe_path);
        for (int i = 0; i < 3; ++i) g250.run_frame();
        check("ESXN-07",
              "a NEX loaded after init() (GUI File > Load) gets the same answers: "
              "load_nex() arms and attaches the handler",
              gui_no_hook && gui_loaded && rd(g250, kDone) == kDoneMark &&
              rd(g250, kRes + 0) == 0x10,
              "hookless_before=" + std::to_string(gui_no_hook) +
              " done=" + hex2s(rd(g250, kDone)) + " drv=" + hex2s(rd(g250, kRes)));

        // Catch-all boundary. $B1 (F_GETFREE) is the last hook code either
        // oracle assigns (esxapi.def:73 `f_getfree equ $b1`; z88dk esxdos.def
        // __ESX_F_GETFREE=0xb1), so it is the last code the catch-all claims.
        // jnext does not implement F_GETFREE, so it gets the catch-all error.
        // That is jnext's POLICY, not NextZXOS's reply — NextZXOS implements
        // F_GETFREE. $B2 is past the hook range: real NextZXOS raised a BASIC
        // error report for it (measured: the probe's call never returned), so
        // the catch-all must leave it to the code at $0008.
        {
            Z80Registers b1{};
            b1.AF = 0x0000;
            const bool b1_handled = e250.cpu().on_esxdos_call &&
                                    e250.cpu().on_esxdos_call(0xB1, b1);
            check("ESXN-11",
                  "$B1 F_GETFREE, the last assigned hook code (esxapi.def:73), is "
                  "inside the catch-all: jnext answers Fc=1 A=$02 (jnext policy — "
                  "NextZXOS implements this call)",
                  loaded && b1_handled && carry(b1) && (b1.AF >> 8) == 0x02,
                  "handled=" + std::to_string(b1_handled) +
                  " A=" + hex2s(static_cast<uint8_t>(b1.AF >> 8)) +
                  " F=" + hex2s(static_cast<uint8_t>(b1.AF)));

            Z80Registers b2{};
            b2.AF = 0x1234; b2.BC = 0x5678; b2.DE = 0x9ABC; b2.HL = 0xDEF0;
            const Z80Registers b2_before = b2;
            const bool hook = static_cast<bool>(e250.cpu().on_esxdos_call);
            const bool b2_handled = hook && e250.cpu().on_esxdos_call(0xB2, b2);
            check("ESXN-12",
                  "$B2, past the last hook code, is NOT answered by the catch-all "
                  "and no register is touched — it runs the code at $0008 (real "
                  "NextZXOS: a BASIC error report, the call never returns)",
                  loaded && hook && !b2_handled && b2.AF == b2_before.AF &&
                  b2.BC == b2_before.BC && b2.DE == b2_before.DE &&
                  b2.HL == b2_before.HL,
                  "hook=" + std::to_string(hook) +
                  " handled=" + std::to_string(b2_handled));
        }

        // After a hard reset no directly-loaded program is running any more:
        // the machine is back on its own ROM, so the stand-in must stand down
        // (the same lifetime as the host-file bridge, XNEX-25..27). Driven
        // through the production hard reset (GH #239): the frontend cold boot,
        // which reconstructs the Emulator and clears the load file, so it
        // normally attaches no hook at all; if a hook is there (the esxdos
        // trace level is on), it must decline.
        emulator_frontend_cold_boot(e250, e250.config(), std::string(),
                                    ColdBootHooks{});
        Z80Registers after_reset{};
        const bool hook_present = static_cast<bool>(e250.cpu().on_esxdos_call);
        const bool serviced = hook_present &&
                              e250.cpu().on_esxdos_call(0x89, after_reset);
        check("ESXN-08",
              "a hard reset (frontend cold boot) disarms the direct-NEX esxDOS "
              "stand-in: M_GETSETDRV is no longer answered and $0008 runs the "
              "machine's own code",
              loaded && !serviced,
              "hook=" + std::to_string(hook_present) +
              " serviced=" + std::to_string(serviced));

        // Same for a soft reset (F4), which keeps RAM but restarts the machine
        // on its ROM — the host-file bridge stands down there too (XNEX-25).
        g250.soft_reset();
        Z80Registers after_soft{};
        const bool soft_hook = static_cast<bool>(g250.cpu().on_esxdos_call);
        const bool soft_serviced = soft_hook &&
                                   g250.cpu().on_esxdos_call(0x89, after_soft);
        check("ESXN-10",
              "soft_reset() disarms the direct-NEX esxDOS stand-in as well",
              gui_loaded && soft_hook && !soft_serviced,
              "hook=" + std::to_string(soft_hook) +
              " serviced=" + std::to_string(soft_serviced));

        // A program with RAM, not ROM, at $0000 owns the $0008 vector: the
        // DivMMC automap that reaches NextZXOS needs the ROM branch of the
        // slot-0 decode (zxnext.vhd:3060-3066 sets sram_pre_override(0) only
        // there; :3138 requires it), so its own RST $08 handler must run.
        EmulatorConfig cli_own = plain;
        cli_own.load_file = own_path;
        auto o250_ptr = std::make_unique<Emulator>();
        Emulator& o250 = *o250_ptr;
        const bool own_loaded = built && o250.init(cli_own) && o250.load_nex(own_path);
        for (int i = 0; i < 3; ++i) o250.run_frame();
        check("ESXN-09",
              "with RAM mapped at $0000 (NR $50=$20) the program's own RST $08 "
              "handler runs, not the stand-in (zxnext.vhd:3060-3066, :3138)",
              own_loaded && rd(o250, kOwn) == kOwnMark && rd(o250, kRes) == kOwnMark &&
              rd(o250, kDone) == kDoneMark,
              "own=" + hex2s(rd(o250, kOwn)) + " A=" + hex2s(rd(o250, kRes)) +
              " done=" + hex2s(rd(o250, kDone)));

        // The ROM-at-$0000 gate is not specific to a direct load: it sits in
        // front of every answer the handler gives, so it must hold for an
        // explicit --esxdos-stub (no NEX loaded at all) and for the
        // extended-NEX host bridge too. Each case has a positive control in
        // the same harness, so a pass on the RAM-at-$0000 row cannot come
        // from a hook that was never reached.
        //
        // --esxdos-stub alone: the program is placed in RAM directly (no
        // load_nex(), so the direct-load stand-in is NOT armed) and started.
        auto run_placed = [&](Emulator& e, const Probe250& pr) {
            for (std::size_t i = 0; i < pr.code.size(); ++i)
                e.mmu().write(static_cast<uint16_t>(0x8000 + i), pr.code[i]);
            Z80Registers r{};
            r.PC = 0x8000;
            r.SP = 0xBFF0;
            e.cpu().set_registers(r);
            for (int i = 0; i < 3; ++i) e.run_frame();
        };
        EmulatorConfig stub_only = cfg;       // cfg.esxdos_stub == true
        stub_only.load_file.clear();

        // M_DOSVERSION is used here because it is a call bare --esxdos-stub
        // answers (M_GETSETDRV and the $80-$B1 catch-all are direct-load
        // only, ESXN-17/18), so the gate is the only thing that can stop it.
        const Probe250 s_own_probe = build_probe_250(true, Call250::DosVersion);
        const Probe250 s_ctl_probe = build_probe_250(false, Call250::DosVersion);

        auto s_own_ptr = std::make_unique<Emulator>();
        Emulator& s_own = *s_own_ptr;
        const bool s_own_init = s_own.init(stub_only);
        run_placed(s_own, s_own_probe);
        check("ESXN-13",
              "--esxdos-stub with no NEX loaded: with RAM at $0000 (NR $50=$20) the "
              "program's own RST $08 handler runs for M_DOSVERSION, not the stub "
              "(zxnext.vhd:3060-3066, :3138)",
              s_own_init && rd(s_own, kOwn) == kOwnMark &&
              rd(s_own, kRes) == kOwnMark && rd(s_own, kDone) == kDoneMark,
              "own=" + hex2s(rd(s_own, kOwn)) + " A=" + hex2s(rd(s_own, kRes)) +
              " done=" + hex2s(rd(s_own, kDone)));

        auto s_ctl_ptr = std::make_unique<Emulator>();
        Emulator& s_ctl = *s_ctl_ptr;
        const bool s_ctl_init = s_ctl.init(stub_only);
        run_placed(s_ctl, s_ctl_probe);
        check("ESXN-14",
              "control for ESXN-13: the same harness with ROM at $0000 IS answered "
              "by the stub (M_DOSVERSION: Fc=0, BC='NX'=$4E58) and the program finishes",
              s_ctl_init && rd(s_ctl, kDone) == kDoneMark &&
              (rd(s_ctl, kRes + 1) & 1) == 0 &&
              rd(s_ctl, kRes + 2) == 0x58 && rd(s_ctl, kRes + 3) == 0x4E,
              "done=" + hex2s(rd(s_ctl, kDone)) + " F=" + hex2s(rd(s_ctl, kRes + 1)) +
              " BC=" + hex2s(rd(s_ctl, kRes + 3)) + hex2s(rd(s_ctl, kRes + 2)));

        // Bare --esxdos-stub must not stand in front of NextZXOS for calls it
        // did not handle before GH #250. With NextZXOS booted it did exactly
        // that once M_GETSETDRV and the catch-all applied to it: `.ls` failed
        // M_P3DOS ($94) and M_GETHANDLE ($8D) and left a blank screen. So
        // with ROM at $0000 and NO direct NEX, those codes must reach the
        // code at $0008. The unit machine has no SD image, so a stand-in
        // "OS" handler is planted in the ROM itself (Next ROM-in-SRAM page
        // 0, where $0000-$1FFF reads come from): it marks kOwn and returns
        // past the DEFB, like the program-owned handler above.
        auto plant_rom_handler = [&](Emulator& e) {
            const uint8_t handler[] = {
                0x3E, kOwnMark, 0x32, static_cast<uint8_t>(kOwn),
                static_cast<uint8_t>(kOwn >> 8),        // LD A,mark : LD (own),A
                0xE1, 0x23, 0xE5, 0xC9                  // POP HL : INC HL : PUSH HL : RET
            };
            uint8_t* rom0 = e.ram().page_ptr(0);
            for (std::size_t i = 0; i < sizeof(handler); ++i) rom0[0x0008 + i] = handler[i];
        };
        auto bare_untouched = [&](Emulator& e, uint8_t code) {
            Z80Registers r{};
            r.AF = 0x1234; r.BC = 0x5678; r.DE = 0x9ABC; r.HL = 0xDEF0;
            const Z80Registers before = r;
            const bool handled = e.cpu().on_esxdos_call && e.cpu().on_esxdos_call(code, r);
            return !handled && r.AF == before.AF && r.BC == before.BC &&
                   r.DE == before.DE && r.HL == before.HL;
        };
        {
            auto e_ptr = std::make_unique<Emulator>();
            Emulator& e = *e_ptr;
            const bool ok = e.init(stub_only);
            plant_rom_handler(e);
            const bool rom_ok = e.mmu().read(0x0008) == 0x3E && e.mmu().get_page(0) == 0xFF;
            run_placed(e, build_probe_250(false, Call250::Hook, 0x94));
            const bool calls_untouched = bare_untouched(e, 0x94) && bare_untouched(e, 0x8D);
            check("ESXN-17",
                  "--esxdos-stub with no direct NEX: M_P3DOS ($94) is NOT answered — "
                  "the handler at $0008 runs — and a direct call of $94 or $8D "
                  "M_GETHANDLE is declined with no register touched (GH #250 "
                  "regression: this blanked NextZXOS's `.ls`)",
                  ok && rom_ok && rd(e, kOwn) == kOwnMark && rd(e, kRes) == kOwnMark &&
                  rd(e, kDone) == kDoneMark && calls_untouched,
                  "rom=" + std::to_string(rom_ok) + " own=" + hex2s(rd(e, kOwn)) +
                  " A=" + hex2s(rd(e, kRes)) + " done=" + hex2s(rd(e, kDone)) +
                  " untouched=" + std::to_string(calls_untouched));
        }
        {
            auto e_ptr = std::make_unique<Emulator>();
            Emulator& e = *e_ptr;
            const bool ok = e.init(stub_only);
            plant_rom_handler(e);
            run_placed(e, build_probe_250(false, Call250::GetDrv));
            check("ESXN-18",
                  "--esxdos-stub with no direct NEX: M_GETSETDRV ($89) is NOT "
                  "answered — the handler at $0008 (NextZXOS's own, when booted) runs",
                  ok && rd(e, kOwn) == kOwnMark && rd(e, kRes) == kOwnMark &&
                  rd(e, kDone) == kDoneMark && bare_untouched(e, 0x89),
                  "own=" + hex2s(rd(e, kOwn)) + " A=" + hex2s(rd(e, kRes)) +
                  " done=" + hex2s(rd(e, kDone)));
        }

        // Extended-NEX host bridge: file_handle=1 and an appended payload, so
        // load_nex() opens the host handle. The program asks for F_READ on
        // that handle — a call only the bridge answers.
        const std::string br_own_path =
            (std::filesystem::temp_directory_path() /
             ("jnext_esxdos250_brown_" + std::to_string(::getpid()) + ".nex")).string();
        const std::string br_ctl_path =
            (std::filesystem::temp_directory_path() /
             ("jnext_esxdos250_brctl_" + std::to_string(::getpid()) + ".nex")).string();
        const std::vector<uint8_t> payload = {'P', 'A', 'Y', '!', 1, 2, 3, 4};
        const Probe250 br_own = build_probe_250(true, Call250::FRead,
                                                ExtendedNexHost::kHandle);
        const Probe250 br_ctl = build_probe_250(false, Call250::FRead,
                                                ExtendedNexHost::kHandle);
        const bool br_built = write_probe_nex(br_own_path, br_own.code, 1, payload) &&
                              write_probe_nex(br_ctl_path, br_ctl.code, 1, payload);

        EmulatorConfig br_cfg = plain;
        br_cfg.load_file = br_own_path;
        auto b_own_ptr = std::make_unique<Emulator>();
        Emulator& b_own = *b_own_ptr;
        const bool b_own_loaded = br_built && b_own.init(br_cfg) &&
                                  b_own.load_nex(br_own_path);
        for (int i = 0; i < 3; ++i) b_own.run_frame();
        check("ESXN-15",
              "extended-NEX host bridge open: with RAM at $0000 an F_READ on the "
              "host handle runs the program's own RST $08 handler, not the bridge "
              "(zxnext.vhd:3060-3066, :3138)",
              b_own_loaded && rd(b_own, kOwn) == kOwnMark &&
              rd(b_own, kRes) == kOwnMark && rd(b_own, kDone) == kDoneMark,
              "loaded=" + std::to_string(b_own_loaded) +
              " own=" + hex2s(rd(b_own, kOwn)) + " A=" + hex2s(rd(b_own, kRes)) +
              " done=" + hex2s(rd(b_own, kDone)));

        br_cfg.load_file = br_ctl_path;
        auto b_ctl_ptr = std::make_unique<Emulator>();
        Emulator& b_ctl = *b_ctl_ptr;
        const bool b_ctl_loaded = br_built && b_ctl.init(br_cfg) &&
                                  b_ctl.load_nex(br_ctl_path);
        for (int i = 0; i < 3; ++i) b_ctl.run_frame();
        check("ESXN-16",
              "control for ESXN-15: the same NEX with ROM at $0000 has its F_READ "
              "served by the bridge (Fc=0, A=handle, BC=4 bytes read)",
              b_ctl_loaded && rd(b_ctl, kDone) == kDoneMark &&
              rd(b_ctl, kRes) == ExtendedNexHost::kHandle &&
              (rd(b_ctl, kRes + 1) & 1) == 0 &&
              rd(b_ctl, kRes + 2) == 4 && rd(b_ctl, kRes + 3) == 0,
              "loaded=" + std::to_string(b_ctl_loaded) +
              " A=" + hex2s(rd(b_ctl, kRes)) + " F=" + hex2s(rd(b_ctl, kRes + 1)) +
              " BC=" + hex2s(rd(b_ctl, kRes + 3)) + hex2s(rd(b_ctl, kRes + 2)));

        std::error_code ec;
        std::filesystem::remove(probe_path, ec);
        std::filesystem::remove(own_path, ec);
        std::filesystem::remove(br_own_path, ec);
        std::filesystem::remove(br_ctl_path, ec);
    }

    const int total = passed + failed;
    std::printf("Total: %d Passed: %d Failed: %d Skipped: 0\n", total, passed, failed);
    return failed == 0 ? 0 : 1;
}
