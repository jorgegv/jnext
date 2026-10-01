#include "remote/zrcp/zrcp_server.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>

#include "core/log.h"
#include "debug/disasm.h"
#include "remote/zrcp/zrcp_condition.h"
#include "remote/zrcp/zrcp_format.h"
#include "script/expr_compiler.h"
#include "version.h"

namespace jnext {
namespace remote {
namespace zrcp {

using jnext::dbg::Action;
using jnext::dbg::ClientInfo;
using jnext::dbg::ClientKind;
using jnext::dbg::CLIENT_NONE;
using jnext::dbg::MemSpace;
using jnext::dbg::PauseReason;
using jnext::dbg::RegId;
using jnext::dbg::Result;
using jnext::dbg::result_name;
using jnext::dbg::RunState;
using jnext::dbg::ServiceStep;
using jnext::dbg::Subscription;

namespace {

/// How much of the connection's input one read takes.
constexpr std::size_t kReadChunk = 64 * 1024;

/// The longest command line served. DeZog's biggest is a 64 KB
/// `write-memory-raw`: 131072 hex digits plus the command, so 512 KiB leaves it
/// four times the room. A longer line is answered with an error and discarded
/// up to its newline — never executed piecemeal.
constexpr std::size_t kMaxLine = 512 * 1024;

/// The most bytes `read-memory` / `hexdump` / `get-crc32` serve in one reply
/// (the transport disconnects a peer 32 MiB behind; a hexdump line is ~4.3x
/// its bytes). DeZog never asks for more than 64 KB at a time.
constexpr std::uint32_t kMaxMemLen = 1024 * 1024;

/// `disassemble`'s line cap: one pass over the whole address space.
constexpr std::uint32_t kMaxDisasmLines = 65536;

/// One `run n` time slice (§2.2 "pump-budgeted"): the step loop yields after
/// this long, so the pump's own budget decides how many slices a tick runs —
/// the adapter cannot see `PumpBudget`, only its own clock.
constexpr auto kRunSlice = std::chrono::milliseconds(2);

/// A CPU-step verb refused because the machine is corrupt (CTL-11). The design
/// text also offered "send hard-reset-cpu", but CTL-12 `Hard` is behind the
/// same gate, so that advice would be refused too.
constexpr char kCorrupt[] =
    "Error. Machine state is corrupt after a failed rewind; acknowledge it in the jnext debugger";

ServerConfig zrcp_server_config() {
    ServerConfig cfg;
    cfg.name       = "zrcp";
    cfg.busy_reply = BUSY_REPLY;
    return cfg;
}

bool iequals(const std::string& a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return i == a.size() && b[i] == 0;
}

/// `set-register` names (ZEsarUX's `debug_change_register`, case-insensitive)
/// → the backend's `RegId`. `IM` is jnext's addition (§10).
bool register_id(const std::string& name, RegId& out) {
    static const struct {
        const char* name;
        RegId       id;
    } kRegs[] = {
        {"PC", RegId::PC},     {"SP", RegId::SP},     {"IX", RegId::IX},   {"IY", RegId::IY},
        {"AF", RegId::AF},     {"BC", RegId::BC},     {"DE", RegId::DE},   {"HL", RegId::HL},
        {"AF'", RegId::AF2},   {"BC'", RegId::BC2},   {"DE'", RegId::DE2}, {"HL'", RegId::HL2},
        {"A", RegId::A},       {"B", RegId::B},       {"C", RegId::C},     {"D", RegId::D},
        {"E", RegId::E},       {"F", RegId::F},       {"H", RegId::H},     {"L", RegId::L},
        {"A'", RegId::A2},     {"B'", RegId::B2},     {"C'", RegId::C2},   {"D'", RegId::D2},
        {"E'", RegId::E2},     {"F'", RegId::F2},     {"H'", RegId::H2},   {"L'", RegId::L2},
        {"I", RegId::I},       {"R", RegId::R},       {"IFF1", RegId::IFF1},
        {"IFF2", RegId::IFF2}, {"IM", RegId::IM},
    };
    for (const auto& r : kRegs)
        if (iequals(name, r.name)) {
            out = r.id;
            return true;
        }
    return false;
}

/// `cpu-step-over`'s JP family (ZEsarUX `si_cpu_step_over_jpret`, plus the
/// `JP NZ` it forgets and the index-register `JP (IX)` / `JP (IY)`): a jump
/// has no "next instruction" to run to, so it is a plain step.
bool is_jp_like(std::uint8_t op, std::uint8_t op2) {
    switch (op) {
        case 0xC3: case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        case 0xE2: case 0xEA: case 0xF2: case 0xFA: case 0xE9:
            return true;
        case 0xDD: case 0xFD:
            return op2 == 0xE9;
        default:
            return false;
    }
}

/// A CPU address: a ZEsarUX number that fits 16 bits. ZEsarUX reads
/// `70000` as 1170H; jnext refuses it (§11.3 item 9).
bool parse_addr(const std::string& tok, std::uint32_t& out) {
    return parse_number(tok, out) && out <= 0xFFFF;
}

/// The widest value `set-register` accepts for `id`: 16-bit pairs, IM 0..2,
/// the interrupt flip-flops 0/1, and a byte for everything else.
std::uint32_t register_max(RegId id) {
    switch (id) {
        case RegId::AF: case RegId::BC: case RegId::DE: case RegId::HL:
        case RegId::AF2: case RegId::BC2: case RegId::DE2: case RegId::HL2:
        case RegId::IX: case RegId::IY: case RegId::SP: case RegId::PC:
            return 0xFFFF;
        case RegId::IM:   return 2;
        case RegId::IFF1: case RegId::IFF2: return 1;
        default:          return 0xFF;
    }
}

/// A translated condition (zrcp_condition.h) → the CAP-EVT predicate a slot's
/// `Execute` subscription carries. The DSL part is compiled by the DSL's own
/// library (`compile_expr`, the `Execute` scope, so `PC` is the instruction's);
/// the native `SEGn` / `ROM` / `RAM` terms are evaluated here from the backend's
/// `SlotInfo`. Empty = no condition at all (the slot is a bare `PC=nnnn`).
/// False with `error` set = the DSL refused the translation.
bool compile_condition(const Translation& t, jnext::dbg::Condition& out, std::string& error) {
    out = {};
    jnext::dbg::Condition dsl;
    if (!t.dsl.empty()) {
        jnext::script::CompileOptions opts;
        opts.on_runtime_error = [](const jnext::script::Diagnostic& d) {
            Log::debugger()->warn("zrcp: breakpoint condition: {}", d.to_string());
        };
        auto c = jnext::script::compile_expr(
            t.dsl, jnext::script::PayloadScope(jnext::dbg::EventKind::Execute), opts);
        if (!c) {
            error = c.errors.empty() ? std::string("the DSL refused it") : c.errors[0].to_string();
            return false;
        }
        dsl = c.predicate;
    }
    const std::vector<NativeTerm>    natives = t.natives;
    const std::optional<NativeTerm>  bare    = t.bare_native;
    if (!dsl && natives.empty() && !bare) return true;
    out = [dsl, natives, bare](const jnext::dbg::Event& ev, const jnext::dbg::Debugger& d) {
        if (bare || !natives.empty()) {
            const auto slots = d.mmu_slots();
            const auto type  = d.machine().type;
            if (bare && native_value(bare->var, bare->n, slots, type) == 0) return false;
            for (const auto& n : natives)
                if (!native_holds(n, slots, type)) return false;
        }
        return !dsl || dsl(ev, d);
    };
    return true;
}

/// The first word of a breakpoint action.
std::string action_word(const std::string& action) {
    return action.substr(0, action.find(' '));
}

/// `debug_if_breakpoint_action_menu`: the actions that stop the machine.
bool action_stops(const std::string& action) {
    return action.empty() || action == "menu" || action == "break";
}

/// The actions jnext serves without stopping (§2.4): they print.
bool action_prints(const std::string& action) {
    const std::string w = action_word(action);
    return w == "prints" || w == "printregs" || w == "printe" || w == "printc";
}

bool palette_id(const std::string& name, const std::string& which, jnext::dbg::PaletteId& out) {
    using jnext::dbg::PaletteId;
    bool second;
    if (which == "first")
        second = false;
    else if (which == "second")
        second = true;
    else
        return false;
    if (name == "ula")
        out = second ? PaletteId::UlaSecond : PaletteId::UlaFirst;
    else if (name == "layer2")
        out = second ? PaletteId::Layer2Second : PaletteId::Layer2First;
    else if (name == "sprite")
        out = second ? PaletteId::SpriteSecond : PaletteId::SpriteFirst;
    else
        return false;
    return true;
}

/// `tbblue-*-clipwindow`'s layer names → the backend's window and the NR that
/// programs it (0x18-0x1B) with its NR 0x1C index-reset bit.
bool clip_layer(const std::string& name, jnext::dbg::ClipLayer& layer, std::uint8_t& nr,
                std::uint8_t& reset_bit) {
    using jnext::dbg::ClipLayer;
    if (name == "layer2") {
        layer = ClipLayer::Layer2; nr = 0x18; reset_bit = 0x01;
    } else if (name == "sprite") {
        layer = ClipLayer::Sprites; nr = 0x19; reset_bit = 0x02;
    } else if (name == "ula") {
        layer = ClipLayer::Ula; nr = 0x1A; reset_bit = 0x04;
    } else if (name == "tilemap") {
        layer = ClipLayer::Tilemap; nr = 0x1B; reset_bit = 0x08;
    } else {
        return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// THE TABLE — alphabetical, as `help` and `ls` print it. Every name ZEsarUX
// 12.0's `ls` lists is here: 67 served, 1 declined, 57 unsupported
// (ZRCP-TAB-01).
// ---------------------------------------------------------------------------

#define U(name, aliases) \
    {name, aliases, nullptr, nullptr, CommandClass::Unsupported, nullptr}

const ZrcpServer::CommandDef ZrcpServer::COMMANDS[] = {
    {"about", nullptr, nullptr, "Shows about message", CommandClass::Served,
     &ZrcpServer::cmd_about},
    U("assemble", "|a"),
    U("ayplayer", "|ayp"),
    {"clear-membreakpoints", nullptr, nullptr, "Clear all memory breakpoints",
     CommandClass::Served, &ZrcpServer::cmd_clear_membreakpoints},
    {"close-all-menus", nullptr, nullptr,
     "Close all open menus. jnext has no menu a remote client could have opened, so it does "
     "nothing",
     CommandClass::Served, &ZrcpServer::cmd_empty},
    {"cpu-code-coverage", nullptr, "action [parameter]",
     "Sets cpu code coverage parameters: enabled yes|no, clear, get (the executed addresses "
     "since the last clear, %04X and a space each, ascending). The coverage is the machine's: "
     "it starts with the first client that enables it and this session turns off only what it "
     "turned on",
     CommandClass::Served, &ZrcpServer::cmd_cpu_code_coverage},
    {"cpu-history", nullptr, "action [parameter]",
     "Runs cpu history actions: enabled yes|no, is-enabled, started yes|no, is-started, "
     "set-max-size n (1 to 1000000; ZEsarUX allows 10000000, a jnext entry is 56 bytes), "
     "get-max-size, get-size, clear, get i (0 = the newest), get-pc start n, ignrephalt "
     "yes|no, ignrepldxr yes|no. clear and set-max-size act on this session's view only: "
     "the machine's trace, which jnext's Step Back reads, is never cleared or resized, so the "
     "history holds at most what it holds (10000 entries by default). The history is jnext's "
     "trace log: enabled yes shows what the machine's trace already holds, and clear starts "
     "the view afresh (ZEsarUX's history starts empty and records only after started yes); "
     "started is recorded but "
     "the history records while it is enabled; ignrephalt / ignrepldxr filter the view (a "
     "run of HALTs or LDIR / LDDR shows its first entry), so they apply to entries already "
     "recorded too; MMU is the get-registers projection of the pages and ROM slots the "
     "entry recorded. Declined in jnext: get-extended (jnext records no paging-port "
     "values) and restore (use the jnext debugger's Step Back)",
     CommandClass::Served, &ZrcpServer::cmd_cpu_history},
    U("cpu-panic", nullptr),
    {"cpu-step", "|cs", nullptr,
     "Run single opcode cpu step. Needs cpu-step mode. The reply is the register line with "
     "TSTATES, then the disassembly at the new PC",
     CommandClass::Served, &ZrcpServer::cmd_cpu_step},
    {"cpu-step-over", "|cso", nullptr,
     "Runs until returning from the current opcode. In case the current opcode is RET or JP "
     "(with or without flag conditions) it runs a cpu-step instead. Divergence from ZEsarUX: "
     "it stops early on a breakpoint, and any data sent stops it, so it can never hang (ZEsarUX "
     "never returns from JR $)",
     CommandClass::Served, &ZrcpServer::cmd_cpu_step_over},
    U("cpu-transaction-log", nullptr),
    U("debug-analyze-command", nullptr),
    {"disable-breakpoint", "|db", "index", "Disable specific breakpoint", CommandClass::Served,
     &ZrcpServer::cmd_disable_breakpoint},
    {"disable-breakpoints", nullptr, nullptr,
     "Disable all breakpoints of this session. Another client's breakpoints (the jnext "
     "debugger's included) keep working",
     CommandClass::Served, &ZrcpServer::cmd_disable_breakpoints},
    {"disassemble", "|d", "[address] [lines]",
     "Disassemble at address. If no address specified, disassemble from PC register. If no "
     "lines specified, disassembles one line. Operands are hexadecimal without suffix",
     CommandClass::Served, &ZrcpServer::cmd_disassemble},
    U("dump-nested-functions", nullptr),
    U("dump-scanline-buffer", nullptr),
    {"enable-breakpoint", "|eb", "index", "Enable specific breakpoint", CommandClass::Served,
     &ZrcpServer::cmd_enable_breakpoint},
    {"enable-breakpoints", nullptr, nullptr,
     "Enable breakpoints for this session. They start disabled, as in ZEsarUX",
     CommandClass::Served, &ZrcpServer::cmd_enable_breakpoints},
    {"enter-cpu-step", "|encs", nullptr,
     "Enter cpu step to step mode: the machine pauses (unless it already is) and the prompt "
     "becomes command@cpu-step>",
     CommandClass::Served, &ZrcpServer::cmd_enter_cpu_step},
    U("esxdoshandler-get-open-files", "|esxgof"),
    {"evaluate", "|e", "expression",
     "Evaluate expression. It's the same parser as breakpoint conditions (see help "
     "set-breakpoint); the result is decimal",
     CommandClass::Served, &ZrcpServer::cmd_evaluate},
    {"exit-cpu-step", "|ecs", nullptr, "Exit cpu step to step mode: the machine resumes",
     CommandClass::Served, &ZrcpServer::cmd_exit_cpu_step},
    {"exit-emulator", nullptr, nullptr,
     "Declined in jnext: a remote client must not be able to end the user's emulator session",
     CommandClass::Declined, nullptr},
    {"extended-stack", nullptr, "action [parameter]",
     "Sets extended stack parameters: enabled yes|no, clear, get n [index] (n stack words "
     "from SP, or from index, as %04XH and its type). The type is call, rst, "
     "maskable_interrupt or non_maskable_interrupt for a return address jnext's call "
     "tracking pushed there, default for anything else (jnext does not tell push from "
     "default). clear does nothing in jnext: tracking forgets a frame when its return pops "
     "it",
     CommandClass::Served, &ZrcpServer::cmd_extended_stack},
    U("find-label", nullptr),
    {"generate-nmi", nullptr, nullptr,
     "Generates a NMI: presses the Multiface NMI button (what jnext's F9 does)",
     CommandClass::Served, &ZrcpServer::cmd_generate_nmi},
    U("get-audio-buffer-info", nullptr),
    {"get-breakpoints", "|gb", "[index] [items]",
     "Get breakpoints list. If set index, returns item at index. If set items, returns number "
     "of items list starting from index parameter. Conditions are listed as ZEsarUX re-prints "
     "them",
     CommandClass::Served, &ZrcpServer::cmd_get_breakpoints},
    {"get-breakpointsactions", "|gba", "[index] [items]",
     "Get breakpoints actions list. If set first, returns item at index. If set items, returns "
     "number of items list starting from index parameter",
     CommandClass::Served, &ZrcpServer::cmd_get_breakpointsactions},
    U("get-breakpoints-optimized", nullptr),
    {"get-buildnumber", nullptr, nullptr, "Shows the jnext version (jnext has no build number)",
     CommandClass::Served, &ZrcpServer::cmd_get_buildnumber},
    {"get-cpu-core-name", nullptr, nullptr, "Get emulation cpu core name", CommandClass::Served,
     &ZrcpServer::cmd_get_cpu_core_name},
    {"get-cpu-frequency", nullptr, nullptr,
     "Get cpu frequency in HZ, from the live machine timing and the NR 0x07 speed",
     CommandClass::Served, &ZrcpServer::cmd_get_cpu_frequency},
    U("get-cpu-turbo-speed", nullptr),
    {"get-crc32", nullptr, "start_address length",
     "Calculate crc32 checksum starting at address for defined length, over the CPU view of "
     "memory",
     CommandClass::Served, &ZrcpServer::cmd_get_crc32},
    {"get-current-machine", "|gcm", nullptr,
     "Returns current machine name: ZX Spectrum Next, ZX Spectrum 48k, ZX Spectrum 128k or ZX "
     "Spectrum +3",
     CommandClass::Served, &ZrcpServer::cmd_get_current_machine},
    U("get-current-memory-zone", "|gcmz"),
    {"get-debug-settings", "|gds", nullptr,
     "Get debug settings on remote command protocol. See command set-debug-settings",
     CommandClass::Served, &ZrcpServer::cmd_get_debug_settings},
    U("get-io-ports", nullptr),
    U("get-machines", nullptr),
    {"get-membreakpoints", nullptr, "[address] [items]",
     "Get memory breakpoints list. If set address, returns item at address. If set items, "
     "returns number of enabled items list starting from address parameter",
     CommandClass::Served, &ZrcpServer::cmd_get_membreakpoints},
    {"get-memory-pages", "|gmp", "[verbose]",
     "Returns current state of memory pages: on the Next RO for a ROM slot, A<page> for a RAM "
     "slot (the 8K page); on the 128K and +3 the four 16K segments RO<rom> / RA<bank>; on the "
     "48K ROM and RAM. verbose gives a description of every page",
     CommandClass::Served, &ZrcpServer::cmd_get_memory_pages},
    U("get-memory-zones", "|gmz"),
    U("get-ocr", nullptr),
    {"get-os", nullptr, nullptr, "Shows emulator operating system", CommandClass::Served,
     &ZrcpServer::cmd_get_os},
    U("get-paging-state", nullptr),
    {"get-registers", "|gr", nullptr,
     "Get CPU registers. VPS is always 0 (it is ZEsarUX's video pause state). On the Next, MMU "
     "is the eight slots: a RAM slot is its 8K page; a ROM slot is 8000H+k in DeZog's two-ROM "
     "model (ROM 0 and 2 read as ROM0, ROM 1 and 3 as ROM1: jnext has four ROMs, DeZog's model "
     "two), never ZEsarUX 12.0's 0000. On the 48K, 128K and +3 it is ZEsarUX's own legacy form: "
     "four 16K segments (8000H+ROM, or the RAM bank), then four 0000. Numbers in every command "
     "are decimal or hexadecimal with an H suffix; an address must fit 16 bits",
     CommandClass::Served, &ZrcpServer::cmd_get_registers},
    U("get-snapshot", nullptr),
    {"get-stack-backtrace", nullptr, "[items]",
     "Get last 16-bit values from the stack. If no items parameter, it shows 5 by default",
     CommandClass::Served, &ZrcpServer::cmd_get_stack_backtrace},
    U("get-text-overlay", nullptr),
    {"get-tstates", nullptr, nullptr, "Get the t-states counter within the current frame",
     CommandClass::Served, &ZrcpServer::cmd_get_tstates},
    {"get-tstates-partial", nullptr, nullptr,
     "Get the t-states partial counter since reset-tstates-partial. It is 64-bit and never "
     "shows OVERFLOW. hard-reset-cpu restarts it with the new machine",
     CommandClass::Served, &ZrcpServer::cmd_get_tstates_partial},
    U("get-ui-io-ports", nullptr),
    {"get-version", nullptr, nullptr,
     "Shows the version: 12.0-jnext-<jnext version> (the ZEsarUX protocol level jnext speaks)",
     CommandClass::Served, &ZrcpServer::cmd_get_version},
    U("get-video-driver", nullptr),
    U("get-visualmem-opcode-dump", "|gvmod"),
    U("get-visualmem-read-dump", "|gvmrd"),
    U("get-visualmem-written-dump", "|gvmwd"),
    {"hard-reset-cpu", nullptr, nullptr,
     "Hard resets the machine: in jnext a cold boot, completed before the reply. A paused "
     "machine stays paused (at PC 0000), a running one runs. A run in progress is answered "
     "with the fresh machine's registers, which then run on",
     CommandClass::Served, &ZrcpServer::cmd_hard_reset_cpu},
    {"help", "|?", "[command]", "Shows help screen or command help", CommandClass::Served,
     &ZrcpServer::cmd_help},
    {"hexdump", "|h", "pointer length", "Dumps memory at address, showing hex and ascii",
     CommandClass::Served, &ZrcpServer::cmd_hexdump},
    U("hexdump-internal", nullptr),
    U("ifrom-press-button", nullptr),
    U("kartusho-press-button", nullptr),
    {"load-binary", nullptr, "file address length",
     "Load binary file \"file\" at address with length, into the CPU view (a write to ROM is "
     "ignored, an address past FFFFH wraps). length 0 loads the whole file (4 MB at most). "
     "The file is on the host running jnext",
     CommandClass::Served, &ZrcpServer::cmd_load_binary},
    U("load-source-code", "|lsc"),
    {"ls", nullptr, nullptr, "Minimal command list", CommandClass::Served, &ZrcpServer::cmd_ls},
    U("mmc-reload", nullptr),
    {"noop", nullptr, nullptr, "This command does nothing", CommandClass::Served,
     &ZrcpServer::cmd_empty},
    U("open-menu", nullptr),
    U("print-error", nullptr),
    U("print-footer", nullptr),
    U("put-snapshot", nullptr),
    U("qdos-get-open-files", "|qlgof"),
    {"quit", "|exit|logout", nullptr,
     "Closes connection. The machine resumes if this client paused it",
     CommandClass::Served, &ZrcpServer::cmd_quit},
    {"read-memory", nullptr, "[address] [length]",
     "Dumps memory at address, CPU view. If address not specified, dumps all 64 KB. If "
     "address but not length, 1 byte. A length past FFFFH wraps to 0000H",
     CommandClass::Served, &ZrcpServer::cmd_read_memory},
    U("realtape-open", nullptr),
    {"reset-cpu", nullptr, nullptr, "Resets CPU: a soft reset", CommandClass::Served,
     &ZrcpServer::cmd_reset_cpu},
    {"reset-tstates-partial", nullptr, nullptr, "Resets the t-states partial counter",
     CommandClass::Served, &ZrcpServer::cmd_reset_tstates_partial},
    {"run", "|r", "[limit]",
     "Run cpu when on cpu step mode. Returns when a breakpoint is fired, data is sent or any "
     "other stop. limit is a number of opcodes (1 to 1000000) to run before returning. "
     "Declined in jnext: verbose, no-stop-on-data (it makes the connection unusable) and "
     "update-immediately",
     CommandClass::Served, &ZrcpServer::cmd_run},
    {"save-binary", nullptr, "file address length",
     "Save binary file \"file\" from address with length, from the CPU view. length 0 saves "
     "64 KB. The file is on the host running jnext",
     CommandClass::Served, &ZrcpServer::cmd_save_binary},
    U("save-binary-internal", nullptr),
    U("save-screen", nullptr),
    U("send-keys-ascii", nullptr),
    U("send-keys-event", nullptr),
    U("send-keys-string", nullptr),
    {"set-breakpoint", "|sb", "index [condition]",
     "Sets a breakpoint at desired index entry with condition, and enables it. If no condition"
     " set, breakpoint will be handled as disabled. A condition jnext cannot compile is "
     "refused and the slot keeps what it had. Conditions are ZEsarUX's: registers (A..L, "
     "AF..HL, IX, IY, SP, PC, I, R, the alternates A'..L' and AF'..HL'), the flags FS FZ FP FV"
     " FH FN FC, IFF1, IFF2, OPCODE1..4, PEEK(), PEEKW(), NOT(), = <> < > <= >=, AND OR XOR, +"
     " - * / & | ^, brackets, and numbers in decimal, hexadecimal with an H suffix, binary "
     "with %, or 'c'. Grouping is ZEsarUX's own: an expression splits at its first logical "
     "operator, then its first comparison, then its first + or -, then its first other "
     "operator. Integers are 32-bit signed. SEG0..SEG7 (Next), ROM and RAM (128K, +3) are "
     "honoured as a comparison with a number that is part of the top-level AND chain, the form"
     " DeZog sends. Declined in jnext: MRA MRV MWA MWV PRA PRV PWA PWV TSTATES TSTATESL "
     "TSTATESP SCANLINE OUTFIRED INFIRED INTFIRED ENTERROM EXITROM HILOWMAPPED PD765PCN USP "
     "EPC COPPERPC D0..D7 A0..A7 AC ER SR P1..P3, and FPEEK IN ABS BYTE WORD OPMWA OPMRA OPMWV"
     " OPMRV. A condition whose first term is PC=<number> is checked only at that address; any"
     " other is checked at every instruction and, as ZEsarUX's default, fires only when it "
     "turns from false to true (one that stays true fires once). A PC=<number> condition fires"
     " at every arrival at its address, except at the address a run resumes from",
     CommandClass::Served, &ZrcpServer::cmd_set_breakpoint},
    {"set-breakpointaction", "|sba", "index [action]",
     "Sets a breakpoint action at desired index entry. Empty, menu or break stop the machine; "
     "prints <text>, printregs, printe <expression> and printc <expression> do not stop it "
     "and send their output to this session as a log> line. Declined in jnext: every other "
     "action",
     CommandClass::Served, &ZrcpServer::cmd_set_breakpointaction},
    {"set-cr", nullptr, nullptr,
     "Sends carriage return before every line feed of this session's output, useful on "
     "Windows environments",
     CommandClass::Served, &ZrcpServer::cmd_set_cr},
    {"set-debug-settings", "|sds", "setting",
     "Set debug settings on remote command protocol. The value is stored and echoed; bits 0-4 "
     "change nothing in jnext's replies (cpu-step and run always show all registers). Bit 5 "
     "(step over interrupt) is declined in jnext",
     CommandClass::Served, &ZrcpServer::cmd_set_debug_settings},
    U("set-machine", "|sm"),
    {"set-membreakpoint", nullptr, "address type [items]",
     "Sets a memory breakpoint starting at desired address entry for type. If items parameter "
     "is not set, the default is 1. type can be: 0: Disabled, 1: Fired when reading memory, 2: "
     "Fired when writing memory, 3: Fired when reading or writing memory. The address wraps at "
     "FFFFH",
     CommandClass::Served, &ZrcpServer::cmd_set_membreakpoint},
    U("set-memory-zone", "|smz"),
    {"set-register", "|sr", "register=value",
     "Changes register value. Example: set-register DE=3344H. Divergence from ZEsarUX 12.0: IM "
     "can be set too",
     CommandClass::Served, &ZrcpServer::cmd_set_register},
    U("set-text-brightness", nullptr),
    U("set-ui-io-ports", nullptr),
    U("set-verbose-level", nullptr),
    U("set-window-zoom", nullptr),
    {"smartload", "|sl", "file",
     "Smart-loads a .nex, .sna, .szx, .z80, .jns, .tap, .tzx, .wav or .rzx file into the "
     "machine, as jnext's --load does. A paused machine stays paused, at the program's start. "
     "The file is on the host running jnext",
     CommandClass::Served, &ZrcpServer::cmd_smartload},
    U("snapshot-inram-get-index", nullptr),
    U("snapshot-inram-load", nullptr),
    {"snapshot-load", nullptr, "name",
     "Restores the snapshot this session saved under name. Divergence from ZEsarUX: a "
     "snapshot is an in-memory bookmark of this session (name is only a key, nothing is read "
     "from disk) and it is gone when the session ends",
     CommandClass::Served, &ZrcpServer::cmd_snapshot_load},
    {"snapshot-save", nullptr, "name",
     "Saves the machine as an in-memory snapshot of this session under name (at most 8; a "
     "name saved again is replaced). Divergence from ZEsarUX: nothing is written to disk (the "
     "jnext GUI saves .jns files), and the machine must be stopped at a frame boundary — "
     "pause it while running; a stop at a breakpoint is mid-frame",
     CommandClass::Served, &ZrcpServer::cmd_snapshot_save},
    U("speech-empty-fifo", nullptr),
    U("speech-send", nullptr),
    {"tbblue-get-clipwindow", nullptr, "ula|layer2|sprite|tilemap",
     "Get clip window parameters x1 x2 y1 y2, from the live layer state",
     CommandClass::Served, &ZrcpServer::cmd_tbblue_get_clipwindow},
    {"tbblue-get-palette", nullptr, "ula|layer2|sprite first|second index [items]",
     "Get palette colours at index as 9-bit RGB333 values in hexadecimal. If items not "
     "specified, returns only one",
     CommandClass::Served, &ZrcpServer::cmd_tbblue_get_palette},
    {"tbblue-get-pattern", nullptr, "index 4|8 [items]",
     "Get patterns at index, of type 4 or 8 bpp, in hexadecimal. If items not specified, "
     "returns only one",
     CommandClass::Served, &ZrcpServer::cmd_tbblue_get_pattern},
    {"tbblue-get-register", nullptr, "index",
     "Get Next register at index, through its read path", CommandClass::Served,
     &ZrcpServer::cmd_tbblue_get_register},
    {"tbblue-get-sprite", nullptr, "index [items]",
     "Get sprite attributes at index, 4 or 5 bytes as attribute 3 bit 6 says. If items not "
     "specified, returns only one",
     CommandClass::Served, &ZrcpServer::cmd_tbblue_get_sprite},
    {"tbblue-set-clipwindow", nullptr, "ula|layer2|sprite|tilemap x1 x2 y1 y2",
     "Set clip window parameters, as four writes of the window's Next register",
     CommandClass::Served, &ZrcpServer::cmd_tbblue_set_clipwindow},
    {"tbblue-set-palette", nullptr, "ula|layer2|sprite first|second index value",
     "Sets 9-bit palette values starting at index. Values separated by one space",
     CommandClass::Served, &ZrcpServer::cmd_tbblue_set_palette},
    {"tbblue-set-pattern", nullptr, "index value",
     "Sets 8-bit pattern values starting at pattern index, 256 values maximum",
     CommandClass::Served, &ZrcpServer::cmd_tbblue_set_pattern},
    {"tbblue-set-register", nullptr, "index value",
     "Set Next register with value at index, running its write handler",
     CommandClass::Served, &ZrcpServer::cmd_tbblue_set_register},
    {"tbblue-set-sprite", nullptr, "index value",
     "Sets sprite attribute bytes starting at byte 0 of the sprite, 5 values maximum; the rest "
     "keep their value. Not port traffic",
     CommandClass::Served, &ZrcpServer::cmd_tbblue_set_sprite},
    U("tsconf-get-af-port", nullptr),
    U("tsconf-set-af-port", nullptr),
    U("view-basic", nullptr),
    {"write-memory", "|wm", "address value",
     "Writes a sequence of bytes starting at desired address, CPU view. Bytes separated by one "
     "space. Writes to ROM are ignored, as in ZEsarUX",
     CommandClass::Served, &ZrcpServer::cmd_write_memory},
    {"write-memory-raw", nullptr, "address values",
     "Writes a sequence of bytes starting at desired address, CPU view. Bytes in hexadecimal "
     "and not separated. Writes to ROM are ignored, as in ZEsarUX",
     CommandClass::Served, &ZrcpServer::cmd_write_memory_raw},
    {"write-port", nullptr, "port value",
     "Writes value at port, as a guest OUT would: it perturbs the machine",
     CommandClass::Served, &ZrcpServer::cmd_write_port},
    U("zeng-is-master", nullptr),
    U("zeng-online", "|zo"),
    U("zxevo-get-nvram", nullptr),
};

#undef U

const ZrcpServer::CommandDef* ZrcpServer::find_command(const std::string& name) {
    if (name.empty()) return nullptr;
    for (const CommandDef& d : COMMANDS) {
        if (name == d.name) return &d;
        if (!d.aliases) continue;
        // `|gr` / `|exit|logout` — ZEsarUX's spelling of the alias list.
        const std::string list = d.aliases;
        std::size_t       at   = 0;
        while (at < list.size()) {
            const std::size_t bar  = list.find('|', at + 1);
            const std::string item = list.substr(at + 1, bar == std::string::npos
                                                             ? std::string::npos
                                                             : bar - at - 1);
            if (item == name) return &d;
            if (bar == std::string::npos) break;
            at = bar;
        }
    }
    return nullptr;
}

std::vector<CommandInfo> ZrcpServer::command_table() {
    std::vector<CommandInfo> out;
    for (const CommandDef& d : COMMANDS) out.push_back(CommandInfo{d.name, d.aliases, d.cls});
    return out;
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

ZrcpServer::ZrcpServer(jnext::dbg::Debugger& dbg, Clock clock)
    : dbg_(dbg),
      clock_(clock ? std::move(clock) : Clock([] { return std::chrono::steady_clock::now(); })),
      server_(zrcp_server_config(), *this) {}

ZrcpServer::~ZrcpServer() {
    // As D: unregistered FIRST, so `pump()` never reaches a Server that is
    // going away; then `stop()`, so a live client is detached while this
    // object is still whole.
    dbg_.remove_service(server_);
    server_.stop();
}

// ---------------------------------------------------------------------------
// remote::Protocol
// ---------------------------------------------------------------------------

void ZrcpServer::on_connect(Connection& c) {
    // ZRCP has no handshake: the first command may touch the machine, and every
    // mutation is attributed to a client (SES-01), so the attach is here.
    const auto a = dbg_.attach(ClientInfo{"ZRCP client " + c.peer(), ClientKind::Zrcp});
    if (!a) {
        Log::debugger()->warn("zrcp: attach refused for {}: {} — closing", c.peer(),
                              result_name(a.status));
        c.write(std::string("Error. The jnext debugger refused the session\n"));
        c.close();
        return;
    }
    cid_ = a.value;
    dbg_.set_listener(cid_, this);
    conn_ = &c;
    send(WELCOME);
    conn_ = nullptr;
}

ServiceStep ZrcpServer::on_service(Connection& c) {
    conn_ = &c;
    struct Reset {
        Connection*& p;
        ~Reset() { p = nullptr; }
    } reset{conn_};

    if (cid_ == CLIENT_NONE) return ServiceStep::Idle;  // the attach was refused
    hist_watch_clock();
    flush_logs();
    pull_input(c);

    if (in_run_ != RunKind::None) return service_run();

    if (discard_to_eol_) {
        // The rest of a line that interrupted a run, or of an overlong one.
        const std::size_t nl = rx_.find('\n');
        if (nl == std::string::npos) {
            rx_.clear();
            return ServiceStep::Idle;
        }
        rx_.erase(0, nl + 1);
        discard_to_eol_ = false;
    }

    std::string line;
    if (!take_line(line)) {
        if (rx_.size() > kMaxLine) {
            Log::debugger()->warn("zrcp: a command line from {} passed {} bytes with no newline "
                                  "— refused and discarded",
                                  c.peer(), kMaxLine);
            rx_.clear();
            discard_to_eol_ = true;
            reply("Error. Command line too long");
            return ServiceStep::Serviced;
        }
        return ServiceStep::Idle;
    }
    execute_line(line);
    return ServiceStep::Serviced;
}

// After the pump's drain and its `Paused` push: a stop caused in this tick (a
// breakpoint in the frames just run, another client's pause in this drain) is
// answered in this tick.
void ZrcpServer::on_notify(Connection& c) {
    conn_ = &c;
    hist_watch_clock();
    flush_logs();
    if (in_run_ != RunKind::None &&
        (reset_stop_owed_ ||
         ((in_run_ == RunKind::Run || in_run_ == RunKind::StepOver) && dbg_.state().paused)))
        finish_run(false);
    conn_ = nullptr;
}

void ZrcpServer::on_disconnect() {
    end_session();
}

void ZrcpServer::end_session() {
    // SES-01: the backend removes this client's subscriptions (a cpu-step-over
    // target included); a pause that is this client's passes to a remaining
    // client, or is released when none remains (GH #280 N1).
    if (cid_ != CLIENT_NONE) dbg_.detach(cid_);
    cid_                     = CLIENT_NONE;
    conn_                    = nullptr;
    rx_.clear();
    discard_to_eol_          = false;
    reset_stop_owed_         = false;
    step_mode_               = false;
    cr_mode_                 = false;
    debug_settings_          = 1;
    tstates_base_            = 0;
    in_run_                  = RunKind::None;
    run_limit_               = 0;
    run_remaining_           = 0;
    run_landed_slot_         = -1;
    // WP-5 — the machine-wide switches this session turned on go off with it;
    // one that was already on when it asked stays on. The view state goes too.
    if (hist_.owned) dbg_.set_trace_enabled(false);
    if (xstack_.owned) dbg_.set_call_stack_enabled(false);
    if (cov_.owned) dbg_.coverage_enable(false);
    hist_ = xstack_ = cov_ = Owned{};
    hist_started_ = ign_halt_ = ign_ldxr_ = false;
    hist_max_     = 10000;
    hist_has_base_ = false;
    hist_clock_seen_ = 0;
    ++hist_gen_;
    hist_view_.clear();
    // WP-4 — the detach above removed every subscription; the session's map of
    // them goes too, so a next client starts as ZEsarUX does: breakpoints off,
    // every slot empty, no memory breakpoint.
    bp_master_ = false;
    for (auto& sl : slots_) sl = Slot{};
    std::fill(mem_types_.begin(), mem_types_.end(), std::uint8_t{0});
    mem_ranges_.clear();
    pending_logs_.clear();
}

// ---------------------------------------------------------------------------
// dbg::Listener — the backend's pushes (SES-02)
// ---------------------------------------------------------------------------

// A stop is read from `state()` when the adapter is next asked (on_service /
// on_notify), not recorded here: the reply needs the machine as it is when the
// reply is written, and ZRCP has no unsolicited stop message for a client idle
// at the prompt (§4.4).
void ZrcpServer::on_paused(const jnext::dbg::PausedInfo& /*info*/) {}
void ZrcpServer::on_resumed(jnext::dbg::ClientId /*by*/) {}

// §4.6 rule 4: a hard reset NEVER pauses a running machine, so a client blocked
// in a run would wait for a stop that is not coming. Its reply is completed
// from the event: the plain stop shape with the fresh machine's registers,
// which then run on (`help hard-reset-cpu` says so). The reply is OWED, not
// written here: it goes out at the next callback through finish_run(), like
// any stop — in this pump's on_notify at the latest, with no frame run in
// between, so the registers are the ones the reset left — which also makes a
// line the client sent before it an interrupt of the run it was waiting on,
// stops a parked `run n` from stepping the new machine, and removes a
// cpu-step-over target of the replaced machine outside the backend's fan-out.
void ZrcpServer::on_reset(jnext::dbg::ResetKind kind) {
    if (kind != jnext::dbg::ResetKind::Hard) return;
    // The fresh machine counts T-states from 0 again, and its cycles restart: a
    // history `clear` base (a cycle) is dropped HERE, synchronously. The clock
    // watch alone is not enough for a cold boot: a loop owner's deferred one
    // (GUI Reset, F1, a guest NR 0x02 reset, a NEX `.run`, a menu load, an SD
    // swap) ends its tick before the pump, so the new machine runs frames
    // before the next callback and its clock may already be past the old one.
    // The watch covers what pushes no `Reset{Hard}` (`hist_watch_clock()`).
    tstates_base_  = 0;
    hist_has_base_ = false;
    ++hist_gen_;
    if (in_run_ != RunKind::None) reset_stop_owed_ = true;
}

void ZrcpServer::on_frame_ended(std::uint32_t /*frame*/) {}
void ZrcpServer::on_subscriptions_changed(jnext::dbg::EventKindMask /*kinds*/) {}
void ZrcpServer::on_exit_requested(int /*code*/) {}
void ZrcpServer::on_log(jnext::dbg::LogLevel /*level*/, const std::string& /*text*/) {}

// ---------------------------------------------------------------------------
// Output and input
// ---------------------------------------------------------------------------

void ZrcpServer::send(const std::string& text) {
    if (!conn_) return;
    if (!cr_mode_) {
        conn_->write(text);
        return;
    }
    std::string out;
    out.reserve(text.size() + text.size() / 16);
    for (char ch : text) {
        if (ch == '\n') out.push_back('\r');
        out.push_back(ch);
    }
    conn_->write(out);
}

void ZrcpServer::reply(const std::string& body) {
    send(body + "\n" + prompt());
}

void ZrcpServer::pull_input(Connection& c) {
    // Bounded, so a flood stays in T's buffer (and the kernel's) — backpressure,
    // never a loss — while a line up to kMaxLine can still complete.
    std::uint8_t buf[kReadChunk];
    while (c.available() > 0 && rx_.size() <= kMaxLine) {
        const std::size_t got = c.read(buf, sizeof(buf));
        if (got == 0) break;
        rx_.append(reinterpret_cast<const char*>(buf), got);
    }
}

bool ZrcpServer::take_line(std::string& line) {
    const std::size_t nl = rx_.find('\n');
    if (nl == std::string::npos) return false;
    line.assign(rx_, 0, nl);
    rx_.erase(0, nl + 1);
    // A telnet client's CRLF: the command ends at the CR, as in ZEsarUX.
    while (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

void ZrcpServer::execute_line(const std::string& line) {
    // A blank line is the empty command: `\n` + the prompt ([T5]). It is also
    // how DeZog interrupts a run, which never reaches here (service_run).
    if (line.empty()) {
        reply("");
        return;
    }
    // The name runs to the first space; leading whitespace makes it no name at
    // all, so `   get-registers` is an unknown command, as in ZEsarUX ([T5]).
    const std::size_t sp = line.find(' ');
    Cmd cmd;
    cmd.name   = line.substr(0, sp);
    cmd.params = sp == std::string::npos ? std::string() : line.substr(sp + 1);
    cmd.args   = split_args(cmd.params);

    const CommandDef* def = find_command(cmd.name);
    if (!def) {
        reply("Unknown command");
        return;
    }
    if (def->cls != CommandClass::Served) {
        reply(std::string("Error. Unsupported command in jnext: ") + def->name);
        return;
    }
    (this->*def->run)(cmd);
}

// ---------------------------------------------------------------------------
// The run state machine (§4.3)
// ---------------------------------------------------------------------------

ServiceStep ZrcpServer::service_run() {
    // ANY byte stops it, and the line it belongs to is discarded, not executed
    // (§1.2) — whether the machine was still running or had already stopped by
    // itself before the pump that saw the byte: the client sent it while it was
    // still waiting for this run's reply.
    if (!rx_.empty()) {
        const std::size_t nl = rx_.find('\n');
        if (nl == std::string::npos) {
            rx_.clear();
            discard_to_eol_ = true;
        } else {
            rx_.erase(0, nl + 1);
        }
        // Stop a machine still running — unless a reset already answered this
        // run: that reply is owed as it is, and a reset is never a pause.
        if (!reset_stop_owed_ && !dbg_.state().paused) dbg_.pause(cid_);
        finish_run(false);
        return ServiceStep::Serviced;
    }
    if (reset_stop_owed_) {
        finish_run(false);
        return ServiceStep::Serviced;
    }
    if (in_run_ == RunKind::RunLimit) {
        run_slice();
        return ServiceStep::Serviced;
    }
    if (dbg_.state().paused) {
        finish_run(false);
        return ServiceStep::Serviced;
    }
    return ServiceStep::Idle;
}

// `run n`: a loop of CTL-03 steps, each synchronous, parked after `kRunSlice`
// so the pump's budget — not the size of `n` — bounds one tick (§2.2). It ends
// early when a step stops on an event (a watch, the magic opcode: REQ-zrcp-05,
// `pause_reason` is then not `Step`) or lands on an armed `Execute` — which the
// next resume's GH #221 step-off would otherwise skip (`probe_execute`).
void ZrcpServer::run_slice() {
    const auto start = clock_();
    bool       first = true;
    while (run_remaining_ > 0) {
        if (!first && clock_() - start >= kRunSlice) return;  // parked: next pass
        first = false;
        if (dbg_.step_into(cid_) != Result::Ok) {
            finish_run(false);  // refused (a corrupt machine): its reason is the reply
            return;
        }
        --run_remaining_;
        const RunState st = dbg_.state();
        if (st.pause_reason.kind != PauseReason::Kind::Step) {
            finish_run(run_remaining_ == 0);
            return;
        }
        // The landing check. `run n` steps with the GH #221 step-off, which
        // skips the `Execute` match of each stepped instruction, so no
        // breakpoint is evaluated by the backend inside a `run n`: this is the
        // one evaluation of the boundary the machine has landed on.
        //   * This session's slots: their own predicate (a PC-free one through
        //     its On-Change state, which a stateless probe cannot judge). A stop
        //     slot ends the run; a print slot prints and the run steps on, as
        //     ZEsarUX runs actions inside a run.
        //   * Every other client's breakpoint, and a legacy PC breakpoint, that
        //     would fire here — `probe_execute`, conditions evaluated.
        bool stop = false;
        for (int i = 0; i < BREAKPOINT_SLOTS; ++i) {
            const Slot& sl = slots_[static_cast<std::size_t>(i)];
            if (sl.sub == jnext::dbg::EVENT_NONE) continue;
            if (sl.cond.fast_pc && *sl.cond.fast_pc != st.pc) continue;
            if (!slot_edge_at(i, st.pc)) continue;
            if (!action_stops(sl.action)) {
                queue_action_log(i);
            } else if (!stop) {
                run_landed_slot_ = i;
                stop             = true;
            }
        }
        if (stop || other_breakpoint_at(st.pc)) {
            finish_run(run_remaining_ == 0);
            return;
        }
    }
    finish_run(true);
}

void ZrcpServer::finish_run(bool limit_reached) {
    flush_logs();  // a print slot's line from this run goes before its reply
    std::string out;
    if (reset_stop_owed_) {
        // The reset answered it: the plain shape, never a `fired` line.
        out              = stop_reply("");
        reset_stop_owed_ = false;
    } else {
        if (limit_reached) out += "Returning after " + std::to_string(run_limit_) + " opcodes\n";
        out += stop_reply(run_landed_slot_ >= 0
                              ? slots_[static_cast<std::size_t>(run_landed_slot_)].cond.canonical
                              : fired_text(dbg_.state()));
    }
    run_landed_slot_ = -1;
    in_run_        = RunKind::None;
    run_limit_     = 0;
    run_remaining_ = 0;
    drop_transients();
    reply(out);
}

// The stop reply's body (§2.4): `[Breakpoint fired: …\n]` + the register line
// with ` TSTATES: n` + `\n` + the disassembly at PC.
std::string ZrcpServer::stop_reply(const std::string& fired) const {
    std::string out;
    if (!fired.empty()) out += "Breakpoint fired: " + fired + "\n";
    const Z80Registers r = dbg_.registers();
    out += register_line(r, dbg_.mmu_slots(), dbg_.machine().type) + " TSTATES: " +
           tstates_text() + "\n";
    const auto d = dbg_.disassemble(r.PC, 1, nullptr);
    out += disasm_line(r.PC, d.empty() ? std::string("?") : std::string(d[0].mnemonic));
    return out;
}

// The `fired` line (§2.4): a slot of this session echoes its condition as
// ZEsarUX re-prints it; a memory breakpoint of this session names the access
// and the address, as ZEsarUX's `cpu_core_loop_debug_check_mem_breakpoints`
// does; the magic opcode and a corrupt machine say so. A stop by another
// client's breakpoint, a user pause, a step, a run-to target or data sent has
// no `fired` line.
std::string ZrcpServer::fired_text(const RunState& st) const {
    const auto& r = st.pause_reason;
    switch (r.kind) {
        case PauseReason::Kind::Magic:   return "Magic breakpoint";
        case PauseReason::Kind::Corrupt: return "Machine corrupt after failed rewind";
        case PauseReason::Kind::Breakpoint:
            for (const Slot& sl : slots_)
                if (sl.sub != jnext::dbg::EVENT_NONE && sl.sub == r.id) return sl.cond.canonical;
            // The backend names the FIRST `Stop` of the boundary; when another
            // client's breakpoint came first, a slot of this session that fires
            // on the same instruction is still this session's reason.
            for (int i = 0; i < BREAKPOINT_SLOTS; ++i) {
                const Slot& sl = slots_[static_cast<std::size_t>(i)];
                if (sl.sub == jnext::dbg::EVENT_NONE || !action_stops(sl.action)) continue;
                if (sl.edge ? sl.edge->fired : slot_fires_at(i, st.pc)) return sl.cond.canonical;
            }
            return "";
        case PauseReason::Kind::Watch: {
            // This session's range by id, or — when another client's watch was
            // the first `Stop` — one of its ranges covering the same access.
            const bool write = jnext::dbg::has_write(r.access);
            for (const MemRange& m : mem_ranges_) {
                if (m.sub == jnext::dbg::EVENT_NONE) continue;
                const bool covers = r.addr >= m.lo && r.addr <= m.hi &&
                                    (m.type & (write ? 2 : 1)) != 0;
                if (m.sub != r.id && !covers) continue;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Memory Breakpoint %s Address: %04XH",
                              write ? "Write" : "Read", static_cast<unsigned>(r.addr));
                return buf;
            }
            return "";
        }
        default:
            return "";
    }
}

std::string ZrcpServer::tstates_text() const {
    const auto t  = dbg_.time();
    const auto mi = dbg_.machine();
    const int  div = mi.cpu_divisor > 0 ? mi.cpu_divisor : 8;
    return std::to_string(t.cycle_in_frame / static_cast<std::uint64_t>(div));
}

// What the backend already removes at a stop it causes; a stop it did not
// cause (another client's pause, a reset) can leave a cpu-step-over target
// armed, so this session's transients go when its run ends.
void ZrcpServer::drop_transients() {
    if (cid_ == CLIENT_NONE) return;
    for (const auto& s : dbg_.subscriptions(true))
        if (s.owner == cid_ && s.transient) dbg_.unsubscribe(cid_, s.id);
}

std::string ZrcpServer::refusal_text(Result r, const char* what) {
    if (r == Result::RefusedCorrupt) return kCorrupt;
    return std::string("Error. ") + what + " refused: " + result_name(r);
}

// ---------------------------------------------------------------------------
// WP-1 — session and information (§2.1)
// ---------------------------------------------------------------------------

void ZrcpServer::cmd_about(const Cmd&) { reply("jnext ZRCP remote command protocol"); }

void ZrcpServer::cmd_get_version(const Cmd&) {
    // `semver.coerce("12.0-jnext-…")` = 12.0.0: >= 10.3 (DeZog's floor) and
    // < 12.1, so DeZog never sends the `set-breakpointpasscount` 12.0 lacks.
    reply(std::string("12.0-jnext-") + JNEXT_VERSION_STRING);
}

void ZrcpServer::cmd_get_buildnumber(const Cmd&) { reply(JNEXT_VERSION_STRING); }

void ZrcpServer::cmd_get_cpu_core_name(const Cmd&) { reply("jnext-fuse-z80"); }

void ZrcpServer::cmd_get_os(const Cmd&) {
#if defined(_WIN32)
    reply("Windows");
#elif defined(__APPLE__)
    reply("macOS");
#else
    reply("GNU/Linux");
#endif
}

void ZrcpServer::cmd_empty(const Cmd&) { reply(""); }

void ZrcpServer::cmd_help(const Cmd& c) {
    if (!c.args.empty()) {
        const CommandDef* d = find_command(c.args[0]);
        if (!d || d->cls == CommandClass::Unsupported) {
            reply(d ? std::string("Error. Unsupported command in jnext: ") + d->name
                    : std::string("No help for that command"));
            return;
        }
        std::string out = std::string("Syntax: ") + d->name + (d->aliases ? d->aliases : "");
        if (d->params) out += std::string(" ") + d->params;
        out += "\n\nDescription\n";
        out += d->help;
        reply(out);
        return;
    }
    std::size_t width = 0;
    for (const CommandDef& d : COMMANDS)
        if (d.cls != CommandClass::Unsupported) width = std::max(width, std::strlen(d.name));
    std::string out = "Available commands:\n";
    for (const CommandDef& d : COMMANDS) {
        if (d.cls == CommandClass::Unsupported) continue;
        std::string line = d.name;
        line.resize(width + 2, ' ');
        // The description up to its first full stop, as ZEsarUX's own list.
        const std::string help  = d.help;
        const std::size_t dot   = help.find('.');
        line += dot == std::string::npos ? help : help.substr(0, dot);
        out += line + "\n";
    }
    out += "\nYou can get descriptive help for every command with: help command\n";
    out += "jnext serves one ZRCP client at a time. The other ZEsarUX commands answer: Error. "
           "Unsupported command in jnext: <name>";
    reply(out);
}

void ZrcpServer::cmd_ls(const Cmd&) {
    // ZEsarUX's remote_simple_help(): four columns, each the longest name + 2.
    std::vector<const char*> names;
    std::size_t              width = 0;
    for (const CommandDef& d : COMMANDS) {
        if (d.cls == CommandClass::Unsupported) continue;
        names.push_back(d.name);
        width = std::max(width, std::strlen(d.name));
    }
    width += 2;
    std::string out, row;
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::string cell = names[i];
        cell.resize(width, ' ');
        row += cell;
        if (i % 4 == 3 || i + 1 == names.size()) {
            out += row + "\n";
            row.clear();
        }
    }
    reply(out);
}

void ZrcpServer::cmd_set_cr(const Cmd&) {
    cr_mode_ = true;
    reply("");
}

void ZrcpServer::cmd_quit(const Cmd&) {
    // ZEsarUX's goodbye, no prompt; T delivers it before the socket closes, and
    // on_disconnect() then detaches (SES-01).
    send("Sayonara baby\n");
    if (conn_) conn_->close();
}

void ZrcpServer::cmd_get_debug_settings(const Cmd&) { reply(std::to_string(debug_settings_)); }

void ZrcpServer::cmd_set_debug_settings(const Cmd& c) {
    std::uint32_t v = 0;
    if (c.args.empty()) {
        reply("ERROR. No parameter set");
        return;
    }
    if (!parse_number(c.args[0], v)) {
        reply("Error. Invalid number: " + c.args[0]);
        return;
    }
    if (v & 0x20) {
        // DeZog sends 32 only with `skipInterrupt: true` (§2.5).
        reply("Error. Unsupported in jnext: step-over-interrupt (bit 5)");
        return;
    }
    debug_settings_ = v;
    reply("");
}

// ---------------------------------------------------------------------------
// WP-2 — inspection (§1.4, §2.3)
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> ZrcpServer::read_cpu(std::uint32_t addr, std::size_t n) const {
    // The CPU view through the live mapping, side-effect free. `peek(Cpu)`
    // wraps past FFFFH to 0000H itself, as ZEsarUX does (row ZRCP-MEM-01).
    std::vector<std::uint8_t> out(n);
    if (n > 0) dbg_.peek(MemSpace::cpu(), addr & 0xFFFF, n, out.data());
    return out;
}

void ZrcpServer::cmd_get_registers(const Cmd&) {
    reply(register_line(dbg_.registers(), dbg_.mmu_slots(), dbg_.machine().type));
}

void ZrcpServer::cmd_set_register(const Cmd& c) {
    // `NAME=VALUE`, one register (ZEsarUX `debug_change_register`). Every
    // failure is ZEsarUX's one text; the value is a number (§1.1).
    const std::size_t eq = c.params.find('=');
    RegId             id;
    std::uint32_t     v = 0;
    if (eq == std::string::npos || !register_id(c.params.substr(0, eq), id) ||
        !parse_number(c.params.substr(eq + 1), v) || v > register_max(id)) {
        reply("Error changing register");
        return;
    }
    if (dbg_.set_register(cid_, id, static_cast<std::uint16_t>(v)) != Result::Ok) {
        reply("Error changing register");
        return;
    }
    reply(register_line(dbg_.registers(), dbg_.mmu_slots(), dbg_.machine().type));
}

void ZrcpServer::cmd_read_memory(const Cmd& c) {
    std::uint32_t addr = 0, len = 0x10000;
    if (!c.args.empty()) {
        if (!parse_addr(c.args[0], addr)) {
            reply("Error. Invalid address: " + c.args[0]);
            return;
        }
        len = 1;
        if (c.args.size() > 1 && !parse_number(c.args[1], len)) {
            reply("Error. Invalid number: " + c.args[1]);
            return;
        }
        // ZEsarUX: length 0 is "the whole zone" — 64 KB of mapped memory.
        if (len == 0) len = 0x10000;
    }
    if (len > kMaxMemLen) {
        reply("Error. Length too large (max " + std::to_string(kMaxMemLen) + ")");
        return;
    }
    const auto bytes = read_cpu(addr, len);
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (std::uint8_t b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0xF]);
    }
    reply(out);
}

// Writes go through `poke(Cpu)`: the live map, ROM ignored. Correct under both
// of the backend's poke contracts — today's (a ROM byte is dropped and the call
// still returns Ok) and package G's ("bytes actually landed; RefusedReadOnly
// if none"): a write that lands nowhere because it is all ROM is still the
// silent success ZEsarUX gives ([T4] `write-memory 0 1`). Anything else
// refused (an RZX in progress) is said.
static bool poke_ok(Result r) { return r == Result::Ok || r == Result::RefusedReadOnly; }

void ZrcpServer::cmd_write_memory(const Cmd& c) {
    std::uint32_t addr = 0;
    if (c.args.empty()) {
        reply("ERROR. No parameters set");
        return;
    }
    if (!parse_addr(c.args[0], addr)) {
        reply("Error. Invalid address: " + c.args[0]);
        return;
    }
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 1; i < c.args.size(); ++i) {
        std::uint32_t v = 0;
        if (!parse_number(c.args[i], v) || v > 0xFF) {
            reply("Error. Invalid byte value: " + c.args[i]);
            return;
        }
        bytes.push_back(static_cast<std::uint8_t>(v));
    }
    if (!bytes.empty()) {
        // `poke(Cpu)` wraps past FFFFH itself, as `peek(Cpu)` does.
        const auto w = dbg_.poke(cid_, MemSpace::cpu(), addr & 0xFFFF, bytes.size(), bytes.data());
        if (!poke_ok(w.status)) {
            reply(std::string("Error. write-memory refused: ") + result_name(w.status));
            return;
        }
    }
    reply("");
}

void ZrcpServer::cmd_write_memory_raw(const Cmd& c) {
    std::uint32_t addr = 0;
    if (c.args.empty()) {
        reply("ERROR. No parameters set");
        return;
    }
    if (!parse_addr(c.args[0], addr)) {
        reply("Error. Invalid address: " + c.args[0]);
        return;
    }
    const std::string hex = c.args.size() > 1 ? c.args[1] : std::string();
    auto nibble = [](char ch, unsigned& v) {
        if (ch >= '0' && ch <= '9') v = static_cast<unsigned>(ch - '0');
        else if (ch >= 'a' && ch <= 'f') v = static_cast<unsigned>(ch - 'a' + 10);
        else if (ch >= 'A' && ch <= 'F') v = static_cast<unsigned>(ch - 'A' + 10);
        else return false;
        return true;
    };
    std::vector<std::uint8_t> bytes;
    bool ok = hex.size() % 2 == 0;
    for (std::size_t i = 0; ok && i + 1 < hex.size(); i += 2) {
        unsigned hi = 0, lo = 0;
        ok = nibble(hex[i], hi) && nibble(hex[i + 1], lo);
        bytes.push_back(static_cast<std::uint8_t>(hi << 4 | lo));
    }
    if (!ok) {
        reply("Error. Invalid hexadecimal byte string");
        return;
    }
    if (!bytes.empty()) {
        const auto w = dbg_.poke(cid_, MemSpace::cpu(), addr & 0xFFFF, bytes.size(), bytes.data());
        if (!poke_ok(w.status)) {
            reply(std::string("Error. write-memory-raw refused: ") + result_name(w.status));
            return;
        }
    }
    reply("");
}

void ZrcpServer::cmd_hexdump(const Cmd& c) {
    std::uint32_t addr = 0, len = 0;
    if (c.args.size() != 2) {
        reply("ERROR. Needs two parameters");
        return;
    }
    if (!parse_addr(c.args[0], addr) || !parse_number(c.args[1], len)) {
        reply("Error. Invalid address or length");
        return;
    }
    if (len > kMaxMemLen) {
        reply("Error. Length too large (max " + std::to_string(kMaxMemLen) + ")");
        return;
    }
    const auto bytes = read_cpu(addr, len);
    reply(hexdump(static_cast<std::uint16_t>(addr), bytes.data(), bytes.size()));
}

void ZrcpServer::cmd_get_crc32(const Cmd& c) {
    std::uint32_t addr = 0, len = 0;
    if (c.args.size() < 2) {
        reply("ERROR. Needs two parameters");
        return;
    }
    if (!parse_addr(c.args[0], addr) || !parse_number(c.args[1], len)) {
        reply("Error. Invalid address or length");
        return;
    }
    if (len < 1) {
        reply("ERROR. Length must be >0");
        return;
    }
    if (len > kMaxMemLen) {
        reply("Error. Length too large (max " + std::to_string(kMaxMemLen) + ")");
        return;
    }
    const auto bytes = read_cpu(addr, len);
    char       buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", crc32_ieee(bytes.data(), bytes.size()));
    reply(buf);
}

void ZrcpServer::cmd_disassemble(const Cmd& c) {
    std::uint32_t addr = dbg_.registers().PC, lines = 1;
    if (!c.args.empty() && !parse_addr(c.args[0], addr)) {
        reply("Error. Invalid address: " + c.args[0]);
        return;
    }
    if (c.args.size() > 1 && !parse_number(c.args[1], lines)) {
        reply("Error. Invalid number: " + c.args[1]);
        return;
    }
    if (lines > kMaxDisasmLines) {
        reply("Error. Too many lines (max " + std::to_string(kMaxDisasmLines) + ")");
        return;
    }
    std::string out;
    std::uint16_t a = static_cast<std::uint16_t>(addr);
    for (std::uint32_t i = 0; i < lines; ++i) {
        const auto d = dbg_.disassemble(a, 1, nullptr);
        if (d.empty()) break;
        if (i > 0) out += "\n";
        out += disasm_line(a, d[0].mnemonic);
        a = static_cast<std::uint16_t>(a + (d[0].byte_count > 0 ? d[0].byte_count : 1));
    }
    reply(out);
}

void ZrcpServer::cmd_get_memory_pages(const Cmd& c) {
    reply(memory_pages(dbg_.mmu_slots(), !c.args.empty() && c.args[0] == "verbose",
                       dbg_.machine().type));
}

void ZrcpServer::cmd_get_stack_backtrace(const Cmd& c) {
    std::uint32_t items = 5;
    if (!c.args.empty()) {
        if (!parse_number(c.args[0], items)) {
            reply("Error. Invalid number: " + c.args[0]);
            return;
        }
        if (items < 1) {
            reply("ERROR. Items must be >0");
            return;
        }
        if (items > 0x8000) {
            reply("Error. Too many items (max 32768)");
            return;
        }
    }
    const std::uint16_t sp    = dbg_.registers().SP;
    const auto          words = read_cpu(sp, items * 2u);
    std::string         out;
    char                buf[16];
    for (std::uint32_t i = 0; i < items; ++i) {
        std::snprintf(buf, sizeof(buf), "%04XH ",
                      static_cast<unsigned>(words[2 * i] | (words[2 * i + 1] << 8)));
        out += buf;
    }
    reply(out);
}

void ZrcpServer::cmd_get_tstates(const Cmd&) { reply(tstates_text()); }

void ZrcpServer::cmd_get_tstates_partial(const Cmd&) {
    const std::uint64_t total = dbg_.time().tstates_total;
    // A cold boot restarts the counter under the base; that is a fresh count.
    if (total < tstates_base_) tstates_base_ = 0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%09llu",
                  static_cast<unsigned long long>(total - tstates_base_));
    reply(buf);
}

void ZrcpServer::cmd_reset_tstates_partial(const Cmd&) {
    tstates_base_ = dbg_.time().tstates_total;
    reply("");
}

void ZrcpServer::cmd_get_cpu_frequency(const Cmd&) {
    // Master cycles per second over the CPU divisor: 3500000 at 3.5 MHz on the
    // Next's 28 MHz master clock; NR 0x07 changes the divisor.
    const auto   mi  = dbg_.machine();
    const int    div = mi.cpu_divisor > 0 ? mi.cpu_divisor : 8;
    const double hz  = static_cast<double>(mi.master_cycles_per_frame) * mi.fps / div;
    reply(std::to_string(static_cast<long long>(std::llround(hz))));
}

void ZrcpServer::cmd_get_current_machine(const Cmd&) { reply(machine_name(dbg_.machine().type)); }

void ZrcpServer::cmd_tbblue_get_register(const Cmd& c) {
    std::uint32_t reg = 0;
    if (c.args.empty()) {
        reply("ERROR. No parameter set");
        return;
    }
    if (!parse_number(c.args[0], reg) || reg > 255) {
        reply("ERROR. Out of range");
        return;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02XH", dbg_.nextreg_peek(static_cast<std::uint8_t>(reg)));
    reply(buf);
}

void ZrcpServer::cmd_tbblue_set_register(const Cmd& c) {
    std::uint32_t reg = 0, v = 0;
    if (c.args.size() < 2) {
        reply("ERROR. Needs two parameters");
        return;
    }
    if (!parse_number(c.args[0], reg) || !parse_number(c.args[1], v) || reg > 255 || v > 255) {
        reply("ERROR. Out of range");
        return;
    }
    const Result r = dbg_.nextreg_write(cid_, static_cast<std::uint8_t>(reg),
                                        static_cast<std::uint8_t>(v));
    reply(r == Result::Ok ? std::string() : refusal_text(r, "tbblue-set-register"));
}

void ZrcpServer::cmd_tbblue_get_sprite(const Cmd& c) {
    std::uint32_t idx = 0, items = 1;
    if (c.args.empty()) {
        reply("ERROR. Needs one parameter minimum");
        return;
    }
    if (!parse_number(c.args[0], idx) || idx >= jnext::dbg::SPRITE_COUNT ||
        (c.args.size() > 1 && (!parse_number(c.args[1], items) ||
                               items > jnext::dbg::SPRITE_COUNT))) {
        reply("ERROR. Out of range");
        return;
    }
    std::string out;
    char        buf[8];
    for (std::uint32_t i = 0; i < items; ++i) {
        const auto a = dbg_.sprite_attr_raw(static_cast<std::uint8_t>((idx + i) % jnext::dbg::SPRITE_COUNT));
        // Attribute 3 bit 6 says a fifth byte follows (the extended form).
        const std::size_t n = (a.value[3] & 0x40) ? 5 : 4;
        for (std::size_t k = 0; k < n; ++k) {
            std::snprintf(buf, sizeof(buf), "%02X ", a.value[k]);
            out += buf;
        }
        out += "\n";
    }
    reply(out);
}

void ZrcpServer::cmd_tbblue_set_sprite(const Cmd& c) {
    std::uint32_t idx = 0;
    if (c.args.empty()) {
        reply("ERROR. No parameters set");
        return;
    }
    if (!parse_number(c.args[0], idx) || idx >= jnext::dbg::SPRITE_COUNT) {
        reply("ERROR. Out of range");
        return;
    }
    if (c.args.size() - 1 > jnext::dbg::SPRITE_ATTR_BYTES) {
        reply("Error. At most 5 attribute bytes");
        return;
    }
    auto bytes = dbg_.sprite_attr_raw(static_cast<std::uint8_t>(idx)).value;
    for (std::size_t i = 1; i < c.args.size(); ++i) {
        std::uint32_t v = 0;
        if (!parse_number(c.args[i], v) || v > 0xFF) {
            reply("Error. Invalid byte value: " + c.args[i]);
            return;
        }
        bytes[i - 1] = static_cast<std::uint8_t>(v);
    }
    const Result r = dbg_.set_sprite_attr_raw(cid_, static_cast<std::uint8_t>(idx), bytes.data(),
                                              bytes.size());
    reply(r == Result::Ok ? std::string() : refusal_text(r, "tbblue-set-sprite"));
}

void ZrcpServer::cmd_tbblue_get_pattern(const Cmd& c) {
    std::uint32_t idx = 0, bpp = 0, items = 1;
    if (c.args.size() < 2) {
        reply("ERROR. Needs two parameters minimum");
        return;
    }
    if (!parse_number(c.args[1], bpp) || (bpp != 4 && bpp != 8)) {
        reply("ERROR. Invalid value for bpp: " + c.args[1]);
        return;
    }
    // 8 bpp: 64 patterns of 256 bytes; 4 bpp: 128 half-patterns of 128 bytes,
    // pattern N at N*128 (the VHDL's 7-bit 4-bit-pattern number).
    const std::uint32_t size  = bpp == 8 ? 256 : 128;
    const std::uint32_t count = static_cast<std::uint32_t>(jnext::dbg::PATTERN_RAM_BYTES) / size;
    if (!parse_number(c.args[0], idx) || idx >= count ||
        (c.args.size() > 2 && (!parse_number(c.args[2], items) || items > count))) {
        reply("ERROR. Out of range");
        return;
    }
    const auto  ram = dbg_.pattern_ram();
    std::string out;
    char        buf[8];
    for (std::uint32_t i = 0; i < items; ++i) {
        const std::size_t base = static_cast<std::size_t>((idx + i) % count) * size;
        for (std::uint32_t k = 0; k < size; ++k) {
            std::snprintf(buf, sizeof(buf), "%02X ", base + k < ram.size ? ram.data[base + k] : 0);
            out += buf;
        }
        out += "\n";
    }
    reply(out);
}

void ZrcpServer::cmd_tbblue_set_pattern(const Cmd& c) {
    std::uint32_t idx = 0;
    if (c.args.empty()) {
        reply("ERROR. No parameters set");
        return;
    }
    if (!parse_number(c.args[0], idx) || idx >= jnext::dbg::PATTERN_RAM_BYTES / 256) {
        reply("ERROR. Out of range");
        return;
    }
    if (c.args.size() - 1 > 256) {
        reply("Error. At most 256 values");
        return;
    }
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 1; i < c.args.size(); ++i) {
        std::uint32_t v = 0;
        if (!parse_number(c.args[i], v) || v > 0xFF) {
            reply("Error. Invalid byte value: " + c.args[i]);
            return;
        }
        bytes.push_back(static_cast<std::uint8_t>(v));
    }
    Result r = Result::Ok;
    if (!bytes.empty())
        r = dbg_.write_pattern_ram(cid_, static_cast<std::uint16_t>(idx * 256), bytes.data(),
                                   bytes.size());
    reply(r == Result::Ok ? std::string() : refusal_text(r, "tbblue-set-pattern"));
}

void ZrcpServer::cmd_tbblue_get_palette(const Cmd& c) {
    jnext::dbg::PaletteId id;
    std::uint32_t         idx = 0, items = 1;
    if (c.args.size() < 3) {
        reply("ERROR. Needs three parameter minimum");
        return;
    }
    if (!palette_id(c.args[0], c.args[1], id)) {
        reply("ERROR. Unknown palette");
        return;
    }
    if (!parse_number(c.args[2], idx) || idx > 255 ||
        (c.args.size() > 3 && (!parse_number(c.args[3], items) || items > 256))) {
        reply("ERROR. Out of range");
        return;
    }
    const auto  pal = dbg_.palette(id);
    std::string out;
    char        buf[8];
    for (std::uint32_t i = 0; i < items; ++i) {
        // The index wraps within the 256-entry bank, as ZEsarUX's byte index;
        // 9 bits: RGB333 (a Layer 2 entry's priority bit is not colour).
        const std::size_t k = (idx + i) & 0xFF;
        std::snprintf(buf, sizeof(buf), "%03X ", k < pal.size() ? (pal[k] & 0x1FF) : 0);
        out += buf;
    }
    reply(out);
}

void ZrcpServer::cmd_tbblue_set_palette(const Cmd& c) {
    jnext::dbg::PaletteId id;
    std::uint32_t         idx = 0;
    if (c.args.empty()) {
        reply("ERROR. No parameters set");
        return;
    }
    if (c.args.size() < 3 || !palette_id(c.args[0], c.args[1], id)) {
        reply("ERROR. Unknown palette");
        return;
    }
    if (!parse_number(c.args[2], idx) || idx > 255) {
        reply("ERROR. Out of range");
        return;
    }
    std::vector<std::uint16_t> values;
    for (std::size_t i = 3; i < c.args.size(); ++i) {
        std::uint32_t v = 0;
        if (!parse_number(c.args[i], v) || v > 0x1FF) {
            reply("Error. Invalid 9-bit colour: " + c.args[i]);
            return;
        }
        values.push_back(static_cast<std::uint16_t>(v));
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        const Result r =
            dbg_.set_palette(cid_, id, static_cast<std::uint8_t>((idx + i) & 0xFF), values[i]);
        if (r != Result::Ok) {
            reply(refusal_text(r, "tbblue-set-palette"));
            return;
        }
    }
    reply("");
}

void ZrcpServer::cmd_tbblue_get_clipwindow(const Cmd& c) {
    jnext::dbg::ClipLayer layer;
    std::uint8_t          nr, bit;
    if (c.args.empty()) {
        reply("ERROR. Needs one parameter");
        return;
    }
    if (!clip_layer(c.args[0], layer, nr, bit)) {
        reply("ERROR. Unknown clip window");
        return;
    }
    const auto w = dbg_.clip_window(layer);
    reply(std::to_string(w.x1) + " " + std::to_string(w.x2) + " " + std::to_string(w.y1) + " " +
          std::to_string(w.y2) + " ");
}

void ZrcpServer::cmd_tbblue_set_clipwindow(const Cmd& c) {
    jnext::dbg::ClipLayer layer;
    std::uint8_t          nr, bit;
    if (c.args.size() < 5) {
        reply("ERROR. Needs five parameters");
        return;
    }
    if (!clip_layer(c.args[0], layer, nr, bit)) {
        reply("ERROR. Unknown clip window");
        return;
    }
    std::uint8_t v[4];
    for (int i = 0; i < 4; ++i) {
        std::uint32_t n = 0;
        if (!parse_number(c.args[static_cast<std::size_t>(i) + 1], n) || n > 255) {
            reply("ERROR. Out of range");
            return;
        }
        v[i] = static_cast<std::uint8_t>(n);
    }
    // The hardware's own route (nextreg.txt 0x18-0x1C): reset the window's
    // write index through NR 0x1C, then four writes X1 X2 Y1 Y2 — the live
    // window changes exactly as a guest program would change it.
    Result r = dbg_.nextreg_write(cid_, 0x1C, bit);
    for (int i = 0; r == Result::Ok && i < 4; ++i) r = dbg_.nextreg_write(cid_, nr, v[i]);
    reply(r == Result::Ok ? std::string() : refusal_text(r, "tbblue-set-clipwindow"));
}

void ZrcpServer::cmd_write_port(const Cmd& c) {
    std::uint32_t port = 0, v = 0;
    if (c.args.size() < 2) {
        reply("ERROR. Needs two parameters");
        return;
    }
    if (!parse_number(c.args[0], port) || !parse_number(c.args[1], v) || port > 0xFFFF ||
        v > 0xFF) {
        reply("ERROR. Out of range");
        return;
    }
    const Result r = dbg_.port_out(cid_, static_cast<std::uint16_t>(port),
                                   static_cast<std::uint8_t>(v));
    reply(r == Result::Ok ? std::string() : refusal_text(r, "write-port"));
}

// ---------------------------------------------------------------------------
// WP-3 — control (§2.2, §4.3-4.6)
// ---------------------------------------------------------------------------

void ZrcpServer::cmd_enter_cpu_step(const Cmd&) {
    // PAUSE ONLY A RUNNING MACHINE (D's lesson, dzrp_server.cpp CMD_INIT):
    // `pause()` on a paused machine re-attributes the stop, and this client's
    // detach would then hand on (or release) a pause that was never its own.
    if (!dbg_.state().paused) dbg_.pause(cid_);
    step_mode_ = true;
    reply("");
}

void ZrcpServer::cmd_exit_cpu_step(const Cmd&) {
    if (!step_mode_) {
        reply("Error. You are not in step to step mode");
        return;
    }
    const Result r = dbg_.run(cid_);
    if (r != Result::Ok) {
        // Still paused, so still in step mode: the prompt tells the truth.
        reply(refusal_text(r, "exit-cpu-step"));
        return;
    }
    step_mode_ = false;
    reply("");
}

void ZrcpServer::cpu_step_reply() {
    // A step's reply is the stop shape without a `fired` line: ZEsarUX clears
    // the breakpoint exception after a step and names nothing ([T3]).
    reply(stop_reply(""));
}

void ZrcpServer::cmd_cpu_step(const Cmd&) {
    if (!step_mode_) {
        reply("Error. You must first enter cpu-step mode");
        return;
    }
    // CTL-03 is synchronous and pauses a machine another client resumed first
    // (§4.4), so a GUI Run under a ZRCP session cannot make this step a frame.
    const Result r = dbg_.step_into(cid_);
    if (r != Result::Ok) {
        reply(refusal_text(r, "cpu-step"));
        return;
    }
    cpu_step_reply();
}

void ZrcpServer::cmd_cpu_step_over(const Cmd&) {
    if (!step_mode_) {
        reply("Error. You must first enter cpu-step mode");
        return;
    }
    const std::uint16_t pc = dbg_.registers().PC;
    std::uint8_t        op[2];
    dbg_.peek(MemSpace::cpu(), pc, 2, op);
    // ZEsarUX semantics (§2.2): a RET or JP has no next instruction to run
    // to, so it is a plain step; anything else runs until PC reaches the
    // instruction after this one.
    if (is_ret_like(pc, dbg_.memory_reader()) || is_jp_like(op[0], op[1])) {
        cmd_cpu_step(Cmd{});
        return;
    }
    const std::uint16_t next = static_cast<std::uint16_t>(pc + dbg_.instruction_length(pc));
    const Result        r    = dbg_.run_to(cid_, next);
    if (r != Result::Ok) {
        reply(refusal_text(r, "cpu-step-over"));
        return;
    }
    // Asynchronous: the reply is the stop, whatever ends it — the target, a
    // breakpoint, another client, or data sent (§10: never a hang).
    in_run_ = RunKind::StepOver;
}

void ZrcpServer::cmd_run(const Cmd& c) {
    std::uint32_t limit = 0;
    for (const std::string& a : c.args) {
        // ZEsarUX takes its options in any order; the three that do not fit a
        // pumped, one-client socket are declined by name (§2.2).
        if (iequals(a, "verbose")) {
            reply("Error. Unsupported in jnext: run verbose");
            return;
        }
        if (iequals(a, "no-stop-on-data")) {
            reply("Error. Unsupported in jnext: no-stop-on-data");
            return;
        }
        if (iequals(a, "update-immediately")) {
            reply("Error. Unsupported in jnext: update-immediately");
            return;
        }
        if (!parse_number(a, limit)) {
            reply("Error. Invalid run limit: " + a);
            return;
        }
    }
    if (limit > RUN_LIMIT_MAX) {
        reply("Error. Unsupported in jnext: a run limit above " + std::to_string(RUN_LIMIT_MAX));
        return;
    }
    std::string head = RUNNING_UNTIL;
    if (limit > 0) {
        head = "Running until a breakpoint, key press or data sent, menu opening, " +
               std::to_string(limit) + " opcodes run, or other event";
    }
    head += "\n";
    if (!step_mode_) {
        // ZEsarUX prints the first line before it checks the mode ([T2]); its
        // stray `\r` is not reproduced (§10).
        reply(head + "Error. You must first enter cpu-step mode");
        return;
    }
    // The first line, and NO prompt: DeZog asserts that line is the whole of
    // what it gets first, and the rest arrives when the machine stops (§1.2).
    send(head);
    if (limit > 0) {
        in_run_        = RunKind::RunLimit;
        run_limit_     = limit;
        run_remaining_ = limit;
        return;  // the steps run from the next pass (run_slice)
    }
    in_run_ = RunKind::Run;
    // A refused resume (a corrupt machine) leaves it paused: the stop reply,
    // with its reason, goes out at once.
    if (dbg_.run(cid_) != Result::Ok) finish_run(false);
}

void ZrcpServer::cmd_hard_reset_cpu(const Cmd&) {
    // CTL-12 `Hard` completes inside this call (§4.6): the machine is rebuilt,
    // the pause kept, and the next command in this drain sees the new machine.
    const Result r = dbg_.reset(cid_, jnext::dbg::ResetKind::Hard);
    if (r == Result::Ok)
        reply("");
    else if (r == Result::RefusedUnavailable)
        reply("Error. Unsupported in jnext: hard-reset-cpu");  // rule 6: no driver
    else
        reply(refusal_text(r, "hard-reset-cpu"));
}

void ZrcpServer::cmd_reset_cpu(const Cmd&) {
    const Result r = dbg_.reset(cid_, jnext::dbg::ResetKind::Soft);
    reply(r == Result::Ok ? std::string() : refusal_text(r, "reset-cpu"));
}

void ZrcpServer::cmd_generate_nmi(const Cmd&) {
    const Result r = dbg_.press_nmi(cid_, jnext::dbg::NmiButton::Mf);
    reply(r == Result::Ok ? std::string() : refusal_text(r, "generate-nmi"));
}

// ---------------------------------------------------------------------------
// WP-4 — breakpoints and conditions (§2.4, §3, §4.1-4.2)
// ---------------------------------------------------------------------------

namespace {

constexpr char kEnableFirst[] = "Error. You must enable breakpoints first";

/// ZEsarUX's `atoi` on a slot index, through the adapter's strict number rule:
/// a 1-based slot, or 0 for anything unparsable (which is out of range).
int slot_index(const std::string& params) {
    std::uint32_t v = 0;
    if (!parse_number(params.substr(0, params.find(' ')), v) || v > 0x7FFFFFFFu) return 0;
    return static_cast<int>(v);
}

/// Everything after the first space — a condition or an action is the rest of
/// the line, spaces included (`remote_set_breakpoint`).
std::string after_index(const std::string& params) {
    const std::size_t sp = params.find(' ');
    return sp == std::string::npos ? std::string() : params.substr(sp + 1);
}

}  // namespace

// A slot's subscription exists exactly while breakpoints are on, the slot is
// enabled and it has a condition: every change re-creates it, so the
// subscription always carries the slot's current condition and action, and
// `enable-/disable-breakpoints` never touches the backend's switches — the
// per-client switch would suspend this session's cpu-step-over target too
// (§11.8).
void ZrcpServer::arm_slot(int index) {
    Slot& sl = slots_[static_cast<std::size_t>(index)];
    if (sl.sub != jnext::dbg::EVENT_NONE) {
        dbg_.unsubscribe(cid_, sl.sub);
        sl.sub = jnext::dbg::EVENT_NONE;
    }
    sl.edge.reset();
    if (!bp_master_ || !sl.enabled || !sl.has_cond) return;
    Subscription sub;
    sub.kind      = jnext::dbg::EventKind::Execute;
    sub.filter.lo = sl.cond.fast_pc ? *sl.cond.fast_pc : 0x0000;
    sub.filter.hi = sl.cond.fast_pc ? *sl.cond.fast_pc : 0xFFFF;
    sub.condition = sl.predicate;
    if (!sl.cond.fast_pc) {
        // ZEsarUX's default, "On Change" (`debug.c` cpu_core_loop_debug_check_
        // breakpoints, `debug_breakpoints_cond_behaviour` = 1): a PC-free
        // condition fires on a false→true edge only, so one that stays true
        // stops (or prints) once, not at every instruction. Evaluated at every
        // boundary, so the previous value is known; re-arming starts it false,
        // as `debug_set_breakpoint` does.
        sl.edge = std::make_shared<Edge>();
        const auto inner = sl.predicate;
        const auto edge  = sl.edge;
        sub.condition = [inner, edge](const jnext::dbg::Event& ev, const jnext::dbg::Debugger& d) {
            return edge->step(ev.cycle, !inner || inner(ev, d));
        };
    }
    if (action_stops(sl.action)) {
        sub.action = Action::Stop;
    } else {
        sub.action  = Action::Continue;
        sub.handler = [this, index](const jnext::dbg::Event&, jnext::dbg::Debugger&) {
            queue_action_log(index);
            return Action::Continue;
        };
    }
    const auto r = dbg_.subscribe(cid_, sub);
    if (r) sl.sub = r.value;
    else
        Log::debugger()->warn("zrcp: breakpoint {} not armed: {}", index + 1, result_name(r.status));
}

// Would slot `index` fire at `pc` now? Its own predicate, with the `Execute`
// event the backend would build there.
bool ZrcpServer::slot_fires_at(int index, std::uint16_t pc) const {
    const Slot& sl = slots_[static_cast<std::size_t>(index)];
    if (!sl.has_cond) return false;
    if (sl.cond.fast_pc && *sl.cond.fast_pc != pc) return false;
    if (!sl.predicate) return true;
    jnext::dbg::Event ev;
    ev.kind  = jnext::dbg::EventKind::Execute;
    ev.cycle = dbg_.time().master_cycle;
    ev.pc    = pc;
    ev.id    = sl.sub;
    ev.owner = cid_;
    return sl.predicate(ev, dbg_);
}

// The landing check of `run n` for slot `index` at `pc`: a fast-path slot as
// `slot_fires_at`; a PC-free one through its On-Change state, which this
// evaluation advances — `run n` steps with the GH #221 step-off, so the backend
// never evaluates a slot there and this is its one evaluation per boundary.
bool ZrcpServer::slot_edge_at(int index, std::uint16_t pc) {
    Slot& sl = slots_[static_cast<std::size_t>(index)];
    if (!sl.edge) return slot_fires_at(index, pc);
    return sl.edge->step(dbg_.time().master_cycle, slot_fires_at(index, pc));
}

// Would another client's breakpoint — or a legacy `BreakpointSet` PC
// breakpoint (`EVENT_NONE`) — STOP the machine at `pc`? `probe_execute` lists
// every live `Execute` subscription covering it whose condition holds now; this
// session's own are left to its slots' (On-Change) evaluation. Of the others,
// only a static `Stop` with no handler is a breakpoint: a `Log` / `Continue`
// one would not stop a free `run`, so it must not end a `run n` either; and a
// handler's verdict OVERRIDES the static action (`events.h` `Handler`) and is
// known only by running the handler, which a probe may not do (it may mutate,
// §4.2a) — so a handler subscription is not counted (§11.8). Every jnext
// frontend's breakpoint (DZRP, GDB, the Qt GUI) is a static `Stop`.
bool ZrcpServer::other_breakpoint_at(std::uint16_t pc) const {
    const auto ids = dbg_.probe_execute(pc);
    if (ids.empty()) return false;
    const auto subs = dbg_.subscriptions(true);
    for (const auto id : ids) {
        if (id == jnext::dbg::EVENT_NONE) return true;
        for (const auto& s : subs)
            if (s.id == id && s.owner != cid_ && s.action == Action::Stop && !s.has_handler)
                return true;
    }
    return false;
}

// §4.2 — the maximal runs of equal non-zero type, diffed against the armed
// ones: an unchanged run keeps its subscription, a gone one is removed, a new
// one subscribed. With breakpoints off nothing is armed; the map stays.
void ZrcpServer::sync_mem_ranges() {
    std::vector<MemRange> want;
    if (bp_master_) {
        std::uint32_t a = 0;
        while (a < 0x10000) {
            const std::uint8_t t = mem_types_[a];
            if (t == 0) {
                ++a;
                continue;
            }
            std::uint32_t b = a;
            while (b + 1 < 0x10000 && mem_types_[b + 1] == t) ++b;
            MemRange m;
            m.lo   = static_cast<std::uint16_t>(a);
            m.hi   = static_cast<std::uint16_t>(b);
            m.type = t;
            want.push_back(m);
            a = b + 1;
        }
    }
    for (const MemRange& old : mem_ranges_) {
        bool kept = false;
        for (MemRange& w : want)
            if (w.lo == old.lo && w.hi == old.hi && w.type == old.type) {
                w.sub = old.sub;
                kept  = true;
            }
        if (!kept && old.sub != jnext::dbg::EVENT_NONE) dbg_.unsubscribe(cid_, old.sub);
    }
    for (MemRange& w : want) {
        if (w.sub != jnext::dbg::EVENT_NONE) continue;
        // `cpu_core_loop_debug_check_mem_breakpoints`: bit 0 reads, bit 1
        // writes; a type with neither is listed and never fires.
        jnext::dbg::Access acc = jnext::dbg::Access::None;
        if (w.type & 1) acc = acc | jnext::dbg::Access::Read;
        if (w.type & 2) acc = acc | jnext::dbg::Access::Write;
        if (acc == jnext::dbg::Access::None) continue;
        Subscription sub;
        sub.kind      = jnext::dbg::EventKind::Mem;
        sub.filter.lo = w.lo;
        sub.filter.hi = w.hi;
        sub.access    = acc;
        sub.action    = Action::Stop;
        const auto r  = dbg_.subscribe(cid_, sub);
        if (r) w.sub = r.value;
        else
            Log::debugger()->warn("zrcp: memory breakpoint {:04X}-{:04X} not armed: {}", w.lo,
                                  w.hi, result_name(r.status));
    }
    mem_ranges_ = std::move(want);
}

// `evaluate`'s text, also `printe`'s (`exp_par_evaluate_expression`): the value
// in decimal, `Error parsing` when it does not tokenise, `Error evaluating
// parsed string: <canonical>` when it cannot be evaluated.
std::string ZrcpServer::evaluate_text(const std::string& expr) const {
    const Translation t = translate_condition(expr, /*fast_path=*/false);
    if (t.parse_error) return "Error parsing";
    const std::string bad = "Error evaluating parsed string: " + t.canonical;
    if (!t.ok || t.empty) return bad;
    const auto slots = dbg_.mmu_slots();
    const auto type  = dbg_.machine().type;
    if (t.bare_native) return std::to_string(native_value(t.bare_native->var, t.bare_native->n, slots, type));
    std::int32_t dsl_value = 1;
    if (!t.dsl.empty()) {
        const auto r = jnext::script::eval_expr(t.dsl, dbg_);
        if (!r.ok) return bad;
        dsl_value = r.value;
    }
    if (t.natives.empty()) return std::to_string(dsl_value);
    // An AND chain with native terms: ZEsarUX's AND yields 1 or 0.
    bool all = dsl_value != 0;
    for (const auto& n : t.natives) all = all && native_holds(n, slots, type);
    return all ? "1" : "0";
}

// A print action's output (ZEsarUX `debug_run_action_breakpoint` prints it on
// its own console; jnext sends it to the session that set it).
void ZrcpServer::queue_action_log(int index) {
    const std::string& a = slots_[static_cast<std::size_t>(index)].action;
    const std::string  w = action_word(a);
    const std::string  p = a.size() > w.size() ? a.substr(w.size() + 1) : std::string();
    std::string line;
    if (w == "prints") {
        line = p;
    } else if (w == "printregs") {
        line = register_line(dbg_.registers(), dbg_.mmu_slots(), dbg_.machine().type);
    } else if (w == "printe") {
        line = evaluate_text(p);
    } else if (w == "printc") {
        const std::string v = evaluate_text(p);
        char* end = nullptr;
        const long n = std::strtol(v.c_str(), &end, 10);
        line = (end && *end == 0 && !v.empty()) ? std::string(1, static_cast<char>(n)) : v;
    }
    pending_logs_.push_back(line);
}

void ZrcpServer::flush_logs() {
    if (pending_logs_.empty() || !conn_) return;
    for (const std::string& l : pending_logs_) send("log> " + l + "\n");
    pending_logs_.clear();
}

void ZrcpServer::cmd_enable_breakpoints(const Cmd&) {
    if (bp_master_) {
        reply("Error. Already enabled");
        return;
    }
    bp_master_ = true;
    for (int i = 0; i < BREAKPOINT_SLOTS; ++i) arm_slot(i);
    sync_mem_ranges();
    reply("");
}

void ZrcpServer::cmd_disable_breakpoints(const Cmd&) {
    if (!bp_master_) {
        reply("Error. Already disabled");
        return;
    }
    bp_master_ = false;
    for (int i = 0; i < BREAKPOINT_SLOTS; ++i) arm_slot(i);
    sync_mem_ranges();
    reply("");
}

void ZrcpServer::cmd_enable_breakpoint(const Cmd& c) {
    if (!bp_master_) return reply(kEnableFirst);
    if (c.params.empty()) return reply("Error. No index set");
    const int n = slot_index(c.params);
    if (n < 1 || n > BREAKPOINT_SLOTS) return reply("Error. Index out of range");
    slots_[static_cast<std::size_t>(n - 1)].enabled = true;
    arm_slot(n - 1);
    reply("");
}

void ZrcpServer::cmd_disable_breakpoint(const Cmd& c) {
    if (!bp_master_) return reply(kEnableFirst);
    if (c.params.empty()) return reply("Error. No index set");
    const int n = slot_index(c.params);
    if (n < 1 || n > BREAKPOINT_SLOTS) return reply("Error. Index out of range");
    slots_[static_cast<std::size_t>(n - 1)].enabled = false;
    arm_slot(n - 1);
    reply("");
}

// `remote_set_breakpoint` + `debug_set_breakpoint`: the checks in ZEsarUX's
// order, then the slot takes the condition AND is enabled. Divergence (§2.4):
// a condition that does not compile leaves the slot as it was; ZEsarUX
// empties it.
void ZrcpServer::cmd_set_breakpoint(const Cmd& c) {
    if (!bp_master_) return reply(kEnableFirst);
    if (c.params.empty()) return reply("Error. No parameters set");
    const int n = slot_index(c.params);
    if (n < 1 || n > BREAKPOINT_SLOTS) return reply("Error. Index out of range");
    const std::string text = after_index(c.params);
    if (text.size() > BREAKPOINT_TEXT_MAX) return reply("Error. Condition too long");
    Translation t = translate_condition(text, /*fast_path=*/true);
    jnext::dbg::Condition pred;
    std::string           why = t.error;
    if (!t.ok || !compile_condition(t, pred, why)) {
        Log::debugger()->info("zrcp: breakpoint {} \"{}\" refused: {}", n, text, why);
        return reply("Error. Error setting breakpoint");
    }
    Slot& sl     = slots_[static_cast<std::size_t>(n - 1)];
    sl.has_cond  = !t.empty;
    sl.cond      = std::move(t);
    sl.predicate = std::move(pred);
    sl.enabled   = true;
    arm_slot(n - 1);
    reply("");
}

void ZrcpServer::cmd_set_breakpointaction(const Cmd& c) {
    if (!bp_master_) return reply(kEnableFirst);
    if (c.params.empty()) return reply("Error. No parameters set");
    const int n = slot_index(c.params);
    if (n < 1 || n > BREAKPOINT_SLOTS) return reply("Error. Index out of range");
    const std::string action = after_index(c.params);
    if (action.size() > BREAKPOINT_TEXT_MAX) return reply("Error. Action too long");
    if (!action_stops(action) && !action_prints(action))
        return reply("Error. Unsupported breakpoint action in jnext: " + action_word(action));
    slots_[static_cast<std::size_t>(n - 1)].action = action;
    arm_slot(n - 1);
    reply("");
}

// `get-breakpoints [index] [items]` (`remote_get_breakpoints`).
void ZrcpServer::cmd_get_breakpoints(const Cmd& c) {
    int start = 1, items = BREAKPOINT_SLOTS;
    if (!c.args.empty()) {
        std::uint32_t v = 0;
        if (!parse_number(c.args[0], v) || v < 1 || v > static_cast<std::uint32_t>(BREAKPOINT_SLOTS))
            return reply("ERROR. Index out of range");
        start = static_cast<int>(v);
        items = 1;
    }
    if (c.args.size() > 1) {
        std::uint32_t v = 0;
        items = parse_number(c.args[1], v) && v <= 0x7FFFFFFFu ? static_cast<int>(v) : 0;
    }
    std::string out = std::string("Breakpoints: ") + (bp_master_ ? "On" : "Off") + "\n";
    for (int i = start - 1; i < BREAKPOINT_SLOTS && i < start - 1 + items; ++i) {
        const Slot& sl = slots_[static_cast<std::size_t>(i)];
        out += (sl.enabled && bp_master_ ? "Enabled " : "Disabled ") + std::to_string(i + 1) + ": " +
               (sl.has_cond ? sl.cond.canonical : std::string("None")) + "\n";
    }
    reply(out);
}

void ZrcpServer::cmd_get_breakpointsactions(const Cmd& c) {
    int start = 1, items = BREAKPOINT_SLOTS;
    if (!c.args.empty()) {
        std::uint32_t v = 0;
        if (!parse_number(c.args[0], v) || v < 1 || v > static_cast<std::uint32_t>(BREAKPOINT_SLOTS))
            return reply("ERROR. Index out of range");
        start = static_cast<int>(v);
        items = 1;
    }
    if (c.args.size() > 1) {
        std::uint32_t v = 0;
        items = parse_number(c.args[1], v) && v <= 0x7FFFFFFFu ? static_cast<int>(v) : 0;
    }
    std::string out;
    for (int i = start - 1; i < BREAKPOINT_SLOTS && i < start - 1 + items; ++i) {
        const std::string& a = slots_[static_cast<std::size_t>(i)].action;
        out += std::to_string(i + 1) + ": " + (action_stops(a) ? std::string("menu") : a) + "\n";
    }
    reply(out);
}

void ZrcpServer::cmd_evaluate(const Cmd& c) {
    if (c.params.empty()) return reply("Error. No expression");
    reply(evaluate_text(c.params));
}

// `set-membreakpoint address type [items]`: ZEsarUX's checks and texts; the
// address wraps at FFFFH as `debug_set_mem_breakpoint`'s z80_int does.
void ZrcpServer::cmd_set_membreakpoint(const Cmd& c) {
    if (!bp_master_) return reply(kEnableFirst);
    if (c.args.size() < 2) return reply("ERROR. Needs two parameters minimum");
    std::uint32_t addr = 0, type = 0, items = 1;
    if (!parse_number(c.args[0], addr) || addr > 0x10000) return reply("ERROR. Address out of range");
    if (!parse_number(c.args[1], type) || type > 255) return reply("ERROR. Type out of range");
    if (c.args.size() >= 3 && !parse_number(c.args[2], items)) items = 0;
    for (std::uint32_t k = 0; k < items && k < 0x10000; ++k)
        mem_types_[(addr + k) & 0xFFFF] = static_cast<std::uint8_t>(type);
    sync_mem_ranges();
    reply("");
}

// `get-membreakpoints [address] [items]` (`remote_get_membreakpoints`): with
// an address alone, that address's type even if 0; with items, that many
// non-zero entries from the address.
void ZrcpServer::cmd_get_membreakpoints(const Cmd& c) {
    std::uint32_t start = 0, items = 65536;
    if (!c.args.empty()) {
        if (!parse_number(c.args[0], start) || start > 65535) return reply("ERROR. Address out of range");
        items = 0;
    }
    if (c.args.size() > 1) {
        if (!parse_number(c.args[1], items) || items > 65536) return reply("ERROR. Items out of range");
    }
    std::string out = std::string("Breakpoints: ") + (bp_master_ ? "On" : "Off") + "\n";
    char buf[32];
    if (items == 0) {
        std::snprintf(buf, sizeof(buf), "%04XH : %u\n", start, mem_types_[start]);
        return reply(out + buf);
    }
    std::uint32_t listed = 0;
    for (std::uint32_t a = start; a < 65536 && listed < items; ++a) {
        if (mem_types_[a] == 0) continue;
        std::snprintf(buf, sizeof(buf), "%04XH : %u\n", a, mem_types_[a]);
        out += buf;
        ++listed;
    }
    reply(out);
}

void ZrcpServer::cmd_clear_membreakpoints(const Cmd&) {
    std::fill(mem_types_.begin(), mem_types_.end(), std::uint8_t{0});
    sync_mem_ranges();
    reply("");
}

// ---------------------------------------------------------------------------
// WP-5 — history, extended stack, coverage, load (§2.2-2.3, §4.1)
// ---------------------------------------------------------------------------

namespace {

/// `remote_eval_yes_no`: "yes", case-insensitively, and nothing else.
bool is_yes(const std::string& v) { return iequals(v, "yes"); }

constexpr char kNotEnabledNl[] = "Error. It's not enabled\n";  // ZEsarUX's own text, newline included
constexpr std::uint32_t kHistoryMax = 1000000;
constexpr std::size_t   kLoadMax    = 4u * 1024 * 1024;  // `load_binary_file`'s 4 MB

}  // namespace

void ZrcpServer::hist_watch_clock() {
    const std::uint64_t now = dbg_.time().master_cycle;
    if (now < hist_clock_seen_ && hist_has_base_) {
        hist_has_base_ = false;
        ++hist_gen_;
    }
    hist_clock_seen_ = now;
}

const std::vector<::TraceEntry>& ZrcpServer::history_view() {
    const auto t = dbg_.time();
    const std::array<std::uint64_t, 3> key{{t.master_cycle, t.tstates_total,
                                            hist_gen_ * 4u + (ign_halt_ ? 2u : 0u) +
                                                (ign_ldxr_ ? 1u : 0u)}};
    if (key == hist_key_) return hist_view_;
    hist_key_ = key;
    hist_view_.clear();
    const auto all = dbg_.trace_entries();
    if (!all) return hist_view_;
    // `clear`'s base: only what the machine recorded after it. Not found — it
    // fell out of the ring, or the machine was rebuilt — means every entry is
    // newer than the clear.
    std::size_t from = 0;
    if (hist_has_base_)
        for (std::size_t k = all.value.size(); k-- > 0;)
            if (all.value[k].cycle == hist_base_cycle_) {
                from = k + 1;
                break;
            }
    // `cpu_history_add_element`'s rule, as a view: of a run of consecutive HALTs
    // (or LDIR / LDDR) only the first is kept.
    int halts = 0, ldxrs = 0;
    for (std::size_t k = from; k < all.value.size(); ++k) {
        const auto& e = all.value[k];
        const bool halt = e.opcode_bytes[0] == 0x76;
        const bool ldxr = e.opcode_bytes[0] == 0xED &&
                          (e.opcode_bytes[1] == 0xB0 || e.opcode_bytes[1] == 0xB8);
        halts = halt ? std::min(halts + 1, 2) : 0;
        ldxrs = ldxr ? std::min(ldxrs + 1, 2) : 0;
        if ((ign_halt_ && halts > 1) || (ign_ldxr_ && ldxrs > 1)) continue;
        hist_view_.push_back(e);
    }
    // `set-max-size`'s limit: ZEsarUX's ring of that size keeps the newest.
    if (hist_view_.size() > hist_max_)
        hist_view_.erase(hist_view_.begin(),
                         hist_view_.end() - static_cast<std::ptrdiff_t>(hist_max_));
    return hist_view_;
}

// The entry's eight `MMU=` values in the get-registers projection, from the
// effective pages and the ROM mask the entry recorded (§11.9 item 2).
std::array<std::uint16_t, 8> ZrcpServer::history_mmu(const ::TraceEntry& e) const {
    const auto type = dbg_.machine().type;
    std::array<jnext::dbg::SlotInfo, 8> s{};
    for (std::size_t i = 0; i < 8; ++i) {
        s[i].effective_page = e.mmu[i];
        s[i].is_rom         = (e.rom_slots >> i) & 1u;  // as recorded (GH #280)
    }
    std::array<std::uint16_t, 8> out{};
    for (int i = 0; i < 8; ++i) out[static_cast<std::size_t>(i)] = mapped_page(s, i, type);
    return out;
}

// `cpu-history` (`remote_cpu_history`): ZEsarUX's sub-commands, texts and index
// rule (`get i`: 0 is the newest), over the trace log (CAP-INS-13).
void ZrcpServer::cmd_cpu_history(const Cmd& c) {
    if (c.args.empty()) return reply("ERROR. Needs at least one parameter");
    const std::string& p     = c.args[0];
    const std::string  value = c.args.size() > 1 ? c.args[1] : std::string();
    if (iequals(p, "enabled")) {
        std::string out;
        if (is_yes(value)) {
            if (hist_.on) out = "Error. Already enabled";
            else {
                hist_.on = true;
                if (!dbg_.trace_enabled()) {
                    dbg_.set_trace_enabled(true);
                    hist_.owned = true;
                }
                ++hist_gen_;
            }
        } else {
            if (!hist_.on) out = "Error. Already disabled";
            hist_.on = false;
            if (hist_.owned) dbg_.set_trace_enabled(false);
            hist_.owned = false;
            ++hist_gen_;
        }
        return reply(out);
    }
    if (iequals(p, "ignrephalt")) { ign_halt_ = is_yes(value); return reply(""); }
    if (iequals(p, "ignrepldxr")) { ign_ldxr_ = is_yes(value); return reply(""); }
    if (iequals(p, "is-enabled")) return reply(hist_.on ? "1" : "0");
    if (iequals(p, "is-started")) return reply(hist_started_ ? "1" : "0");
    if (iequals(p, "restore") || iequals(p, "get-extended")) {
        if (!hist_.on) return reply(kNotEnabledNl);
        return reply(iequals(p, "restore")
                         ? "Error. Unsupported in jnext: cpu-history restore - use the jnext "
                           "debugger's Step Back"
                         : "Error. Unsupported in jnext: cpu-history get-extended - jnext "
                           "records no paging-port values");
    }
    const bool known = iequals(p, "started") || iequals(p, "set-max-size") ||
                       iequals(p, "clear") || iequals(p, "get") || iequals(p, "get-size") ||
                       iequals(p, "get-max-size") || iequals(p, "get-pc");
    if (!known) return reply("Error. Unknown parameter");
    if (!hist_.on) return reply(kNotEnabledNl);
    if (iequals(p, "started")) {
        hist_started_ = is_yes(value);
        return reply("");
    }
    // `set-max-size` and `clear` act on this session's VIEW, never on the
    // machine's trace — jnext's Step Back and rewind read that one (§11.9).
    if (iequals(p, "set-max-size")) {
        std::uint32_t n = 0;
        if (!parse_number(value, n) || n < 1 || n > kHistoryMax)
            return reply("ERROR: Value out of range");
        hist_max_ = n;
        ++hist_gen_;
        return reply("");
    }
    if (iequals(p, "clear")) {
        hist_has_base_ = false;
        const auto all = dbg_.trace_entries();
        if (all && !all.value.empty()) {
            hist_has_base_   = true;
            hist_base_cycle_ = all.value.back().cycle;
        }
        ++hist_gen_;
        return reply("");
    }
    if (iequals(p, "get-max-size")) return reply(std::to_string(hist_max_));
    const auto& view  = history_view();
    const long  total = static_cast<long>(view.size());
    if (iequals(p, "get-size")) return reply(std::to_string(total));
    // `cpu_history_get_*_element`'s three answers for an index.
    const auto element = [&](long idx, bool pc_only, std::string& out) {
        if (idx < 0) {
            out = pc_only ? "ERROR: index can't be negative" : "ERROR: index out of range";
            return;
        }
        if (idx >= total) {
            out = "ERROR: index beyond total elements (" + std::to_string(total) + ")";
            return;
        }
        const auto& e = view[static_cast<std::size_t>(idx)];
        if (pc_only) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%04x", e.pc);
            out = buf;
        } else {
            out = history_line(e, history_mmu(e));
        }
    };
    // ZEsarUX reads these with `parse_string_to_number`: a sign is honoured,
    // garbage is 0.
    const auto signed_arg = [](const std::string& tok) -> long {
        const bool neg = !tok.empty() && tok[0] == '-';
        std::uint32_t v = 0;
        if (!parse_number(neg ? tok.substr(1) : tok, v)) return 0;
        return neg ? -static_cast<long>(v) : static_cast<long>(v);
    };
    const long i = signed_arg(value);
    if (iequals(p, "get")) {
        std::string out;
        element(total - i - 1, false, out);
        return reply(out);
    }
    // get-pc start n: n PCs from `start` towards the oldest, each "%04x ".
    const long n = c.args.size() > 2 ? signed_arg(c.args[2]) : 0;
    if (n < 0) return reply("Error. Can't be negative");
    long count = std::min<long>(n, total);
    long idx   = total - i - 1;
    std::string out;
    for (; count > 0; --count, --idx) {
        std::string one;
        element(idx, true, one);
        out += one + " ";
    }
    reply(out);
}

// `extended-stack` (`remote_extended_stack`): `get n [index]` lists n stack
// words from SP (or index), each typed by jnext's call tracking (CAP-INS-12):
// a frame whose return address sits at that word names the push.
void ZrcpServer::cmd_extended_stack(const Cmd& c) {
    if (c.args.empty()) return reply("ERROR. Needs at least one parameter");
    const std::string& p     = c.args[0];
    const std::string  value = c.args.size() > 1 ? c.args[1] : std::string();
    if (iequals(p, "enabled")) {
        std::string out;
        if (is_yes(value)) {
            if (xstack_.on) out = "Error. Already enabled";
            else {
                xstack_.on = true;
                if (!dbg_.call_stack_enabled()) {
                    dbg_.set_call_stack_enabled(true);
                    xstack_.owned = true;
                }
            }
        } else {
            if (!xstack_.on) out = "Error. Already disabled";
            xstack_.on = false;
            if (xstack_.owned) dbg_.set_call_stack_enabled(false);
            xstack_.owned = false;
        }
        return reply(out);
    }
    if (iequals(p, "clear")) return reply("");
    if (!iequals(p, "get")) return reply("Error. Unknown parameter");
    if (!xstack_.on) return reply("Error. It's not enabled");
    std::uint32_t items = 0, from = dbg_.registers().SP;
    if (!parse_number(value, items)) items = 0;
    if (items > 0x8000) return reply("Error. Too many items (max 32768)");
    if (c.args.size() > 2 && !parse_number(c.args[2], from)) from = 0;
    const auto& frames = dbg_.call_stack();
    std::string out;
    char        buf[48];
    for (std::uint32_t k = 0; k < items; ++k) {
        const std::uint16_t at = static_cast<std::uint16_t>(from + 2 * k);
        const auto w = read_cpu(at, 2);
        const char* type = "default";
        for (const auto& f : frames)
            if (f.sp_at_call == at) {
                switch (f.type) {
                    case ::CallType::CALL: type = "call"; break;
                    case ::CallType::RST:  type = "rst"; break;
                    case ::CallType::INT:  type = "maskable_interrupt"; break;
                    case ::CallType::NMI:  type = "non_maskable_interrupt"; break;
                }
                break;
            }
        std::snprintf(buf, sizeof(buf), "%04XH %s\n", static_cast<unsigned>(w[0] | (w[1] << 8)),
                      type);
        out += buf;
    }
    reply(out);
}

// `cpu-code-coverage` (`remote_cpu_code_coverage`) over CAP-INS-20.
void ZrcpServer::cmd_cpu_code_coverage(const Cmd& c) {
    if (c.args.empty()) return reply("ERROR. Needs at least one parameter");
    const std::string& p     = c.args[0];
    const std::string  value = c.args.size() > 1 ? c.args[1] : std::string();
    if (iequals(p, "enabled")) {
        std::string out;
        if (is_yes(value)) {
            if (cov_.on) out = "Error. Already enabled";
            else {
                cov_.on = true;
                if (!dbg_.coverage_enabled()) {
                    dbg_.coverage_enable(true);
                    cov_.owned = true;
                }
            }
        } else {
            if (!cov_.on) out = "Error. Already disabled";
            cov_.on = false;
            if (cov_.owned) dbg_.coverage_enable(false);
            cov_.owned = false;
        }
        return reply(out);
    }
    if (iequals(p, "clear")) {
        dbg_.coverage_clear();
        return reply("");
    }
    if (!iequals(p, "get")) return reply("Error. Unknown parameter");
    if (!cov_.on) return reply("Error. It's not enabled");
    const auto& bits = dbg_.coverage();
    std::string out;
    char        buf[8];
    for (std::size_t a = 0; a < bits.size(); ++a)
        if (bits[a]) {
            std::snprintf(buf, sizeof(buf), "%04X ", static_cast<unsigned>(a));
            out += buf;
        }
    reply(out);
}

// `load-binary "file" address length` (`load_binary_file`).
void ZrcpServer::cmd_load_binary(const Cmd& c) {
    const auto a = split_quoted_args(c.params);
    if (a.size() < 3) return reply("ERROR. Needs three parameters");
    std::uint32_t addr = 0, len = 0;
    if (!parse_addr(a[1], addr)) return reply("Error. Invalid address: " + a[1]);
    if (!parse_number(a[2], len)) return reply("Error. Invalid length: " + a[2]);
    std::FILE* f = std::fopen(a[0].c_str(), "rb");
    if (!f) return reply("ERROR loading file");
    if (len == 0 || len > kLoadMax) len = kLoadMax;
    std::vector<std::uint8_t> bytes(len);
    const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), f);
    const bool        bad = std::ferror(f) != 0;  // a directory opens, and fails here
    std::fclose(f);
    if (bad) return reply("ERROR loading file");
    bytes.resize(got);
    // The CPU view wraps at FFFFH; a write to ROM lands nowhere (as ZEsarUX, and
    // as write-memory: `RefusedReadOnly` is silent). Any other refusal — an RZX
    // recording, a corrupt machine — is said, with what had landed by then.
    std::size_t landed = 0;
    for (std::size_t off = 0; off < bytes.size(); off += 0x10000) {
        const std::size_t n = std::min<std::size_t>(0x10000, bytes.size() - off);
        const auto w = dbg_.poke(cid_, MemSpace::cpu(), static_cast<std::uint16_t>(addr + off), n,
                                 bytes.data() + off);
        if (!poke_ok(w.status))
            return reply(std::string("Error. load-binary refused: ") + result_name(w.status) +
                         " (" + std::to_string(landed) + " bytes loaded)");
        landed += w.value;
    }
    reply("");
}

// `save-binary "file" address length` (`save_binary_file`). ZEsarUX answers a
// failure "ERROR loading file" here too; so does jnext.
void ZrcpServer::cmd_save_binary(const Cmd& c) {
    const auto a = split_quoted_args(c.params);
    if (a.size() < 3) return reply("ERROR. Needs three parameters");
    std::uint32_t addr = 0, len = 0;
    if (!parse_addr(a[1], addr)) return reply("Error. Invalid address: " + a[1]);
    if (!parse_number(a[2], len)) return reply("Error. Invalid length: " + a[2]);
    if (len == 0) len = 0x10000;
    if (len > kMaxMemLen) return reply("Error. Invalid length: " + a[2]);
    const auto bytes = read_cpu(addr, len);
    std::FILE* f     = std::fopen(a[0].c_str(), "wb");
    if (!f) return reply("ERROR loading file");
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    const bool closed = std::fclose(f) == 0;
    reply(ok && closed ? "" : "ERROR loading file");
}

// `smartload "file"` — CAP-CTL-15, the --load path (REQ-zrcp-12).
void ZrcpServer::cmd_smartload(const Cmd& c) {
    const auto a = split_quoted_args(c.params);
    if (a.empty() || a[0].empty()) return reply("ERROR. No parameter set");
    const Result r = dbg_.load(cid_, a[0]);
    if (r == Result::Ok) return reply("");
    if (r == Result::RefusedCorrupt) return reply(kCorrupt);
    Log::debugger()->warn("zrcp: smartload \"{}\": {}", a[0], result_name(r));
    reply("Error. Unknown file format");
}

// `snapshot-save name` / `snapshot-load name` — this session's in-memory
// bookmarks (CAP-CAP-03, REQ-zrcp-13 DECIDED). Only at a frame boundary, for
// DZRP's reason: DeZog does not re-read the registers after a save, so the
// advance to one would leave its cached PC behind the machine.
void ZrcpServer::cmd_snapshot_save(const Cmd& c) {
    if (c.params.empty()) return reply("ERROR. No parameter set");
    const Result r = dbg_.bookmark_save(cid_, c.params, jnext::dbg::SaveStateMode::RefuseMidFrame);
    if (r == Result::Ok) return reply("");
    if (r == Result::NotAtFrameBoundary)
        return reply("Error. The machine is stopped mid-frame: a snapshot can be saved at a frame "
                     "boundary (pause it while it runs)");
    if (r == Result::RefusedUnavailable && dbg_.bookmarks(cid_).size() >= 8)
        return reply("Error. Too many snapshots in this session (8)");
    reply(refusal_text(r, "snapshot-save"));
}

void ZrcpServer::cmd_snapshot_load(const Cmd& c) {
    if (c.params.empty()) return reply("ERROR. No parameter set");
    const auto held = dbg_.bookmarks(cid_);
    if (std::find(held.begin(), held.end(), c.params) == held.end())
        return reply("Error. No snapshot saved under that name in this session");
    const Result r = dbg_.bookmark_restore(cid_, c.params);
    reply(r == Result::Ok ? std::string() : refusal_text(r, "snapshot-load"));
}

}  // namespace zrcp
}  // namespace remote
}  // namespace jnext
