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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
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
    // On the heap: an Emulator is large, and main() holds dozens of Rigs. Their stack slots add up under the
    // MinGW tests' 8 MB reserve (a stack overflow at startup on Windows).
    std::unique_ptr<Emulator> emu_owner = std::make_unique<Emulator>();
    Emulator&                 emu = *emu_owner;
    EmulatorConfig            cfg;
    std::unique_ptr<Debugger> dbg;
    StartupStop               ss;
    std::string               why;

    bool realtime = false;   // tape mode of land(): the deck plays in real time

    // `with_rom`: the real ROM from the SD image (like every suite that executes ROM code): the
    // tape rows run the ROM's LD-BYTES. The other rows use the bare machine.
    explicit Rig(const std::string& break_at = {}, MachineType type = MachineType::ZX48K,
                 bool with_rom = false) {
        cfg.type           = type;
        if (with_rom) {
            const char* env = std::getenv("JNEXT_TEST_SD_IMAGE");
            const char* home = std::getenv("HOME");
            cfg.sd_card_image = env ? env : std::string(home ? home : ".") + "/.jnext/sdcard/cspect-next-1gb-fixed.img";
            cfg.sd_card_readonly = true;
            cfg.rewind_buffer_frames = 0;
        }
        cfg.debugger_start = true;
        cfg.break_at       = break_at;
        emu.init(cfg);
        dbg = std::make_unique<Debugger>(emu);
    }
    bool start(int coming) { return ss.start(*dbg, emu, cfg, coming, true, why); }
    // The loop owner's sequence for a `--load`: the load, its sidecars, the landing.
    bool land(const std::string& file) {
        const bool ok = emulator_apply_load(emu, file, realtime);
        dbg->load_program_sidecars(file);
        return ss.program_landed(file, ok, false, why);
    }
    uint16_t pc() { return emu.cpu().get_registers().PC; }
    bool paused() const { return dbg->state().paused; }
    // Frames, as the loop owner runs them, with the per-tick poll, until the machine stops.
    void run(int frames = 6) {
        for (int i = 0; i < frames && !paused(); ++i) {
            ss.poll();   // the loop owners poll before a tick's frames, too
            emu.run_frame();
            dbg->pump(jnext::dbg::PumpBudget{});
            ss.poll();
        }
    }
};

// ---- tape fixtures (GH #548, DBGST-30..): built here, from the TAP and TZX formats ----

using Bytes = std::vector<uint8_t>;

// One TAP block: length word, flag, payload, XOR checksum.
Bytes tap_block(uint8_t flag, const Bytes& payload) {
    Bytes b;
    const unsigned n = static_cast<unsigned>(payload.size()) + 2;
    b.push_back(static_cast<uint8_t>(n & 0xFF));
    b.push_back(static_cast<uint8_t>(n >> 8));
    b.push_back(flag);
    uint8_t x = flag;
    for (uint8_t v : payload) { b.push_back(v); x ^= v; }
    b.push_back(x);
    return b;
}

// A header block (flag 0): type, 10-char name, length, param1, param2.
Bytes tap_header(uint8_t type, const char* name, unsigned len, unsigned p1, unsigned p2) {
    Bytes p{type};
    std::string nm = name;
    nm.resize(10, ' ');
    for (char c : nm) p.push_back(static_cast<uint8_t>(c));
    for (unsigned v : {len, p1, p2}) { p.push_back(static_cast<uint8_t>(v & 0xFF)); p.push_back(static_cast<uint8_t>(v >> 8)); }
    return tap_block(0x00, p);
}

Bytes cat(std::initializer_list<Bytes> parts) {
    Bytes r;
    for (const Bytes& p : parts) r.insert(r.end(), p.begin(), p.end());
    return r;
}

// A BASIC line: number, tokens (ending 0x0D added here), as the ROM's program area holds it.
Bytes basic_line(unsigned number, const Bytes& body) {
    Bytes l{static_cast<uint8_t>(number >> 8), static_cast<uint8_t>(number & 0xFF)};
    const unsigned n = static_cast<unsigned>(body.size()) + 1;
    l.push_back(static_cast<uint8_t>(n & 0xFF));
    l.push_back(static_cast<uint8_t>(n >> 8));
    l.insert(l.end(), body.begin(), body.end());
    l.push_back(0x0D);
    return l;
}

// Program block pair that autostarts at line 10.
Bytes program_pair(const Bytes& program) {
    return cat({tap_header(0, "prog", static_cast<unsigned>(program.size()), 10,
                           static_cast<unsigned>(program.size())),
                tap_block(0xFF, program)});
}

// TAP -> TZX 1.20: each TAP block is a standard-speed 0x10 block (pause 1000 ms), then `tail`.
Bytes tzx_of(const Bytes& tap, const Bytes& tail) {
    Bytes z{'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20};
    for (size_t off = 0; off + 2 <= tap.size();) {
        const unsigned n = tap[off] | (tap[off + 1] << 8);
        z.push_back(0x10);
        z.push_back(0xE8); z.push_back(0x03);   // pause 1000 ms
        z.push_back(tap[off]); z.push_back(tap[off + 1]);
        z.insert(z.end(), tap.begin() + static_cast<long>(off) + 2, tap.begin() + static_cast<long>(off) + 2 + n);
        off += 2 + n;
    }
    z.insert(z.end(), tail.begin(), tail.end());
    return z;
}

// TAP -> 8-bit mono 44100 Hz WAV with the standard ROM pulse timings (T-states / 79.365 per sample).
Bytes wav_of(const Bytes& tap) {
    Bytes pcm;
    int level = 0;
    auto pulse = [&](double tstates) {
        const int n = static_cast<int>(tstates / 79.365 + 0.5);
        for (int i = 0; i < n; ++i) pcm.push_back(level ? 0xE0 : 0x20);
        level ^= 1;
    };
    for (size_t off = 0; off + 2 <= tap.size();) {
        const unsigned n = tap[off] | (tap[off + 1] << 8);
        const uint8_t* blk = tap.data() + off + 2;
        const int pilot = blk[0] < 0x80 ? 8063 : 3223;
        for (int i = 0; i < pilot; ++i) pulse(2168);
        pulse(667); pulse(735);
        for (unsigned i = 0; i < n; ++i)
            for (int bit = 7; bit >= 0; --bit) {
                const double t = (blk[i] >> bit) & 1 ? 1710 : 855;
                pulse(t); pulse(t);
            }
        for (int i = 0; i < 44; ++i) pcm.push_back(0x80);   // 1 ms of silence
        level = 0;
        for (int i = 0; i < 44100; ++i) pcm.push_back(0x80);   // and a second between blocks
        off += 2 + n;
    }
    Bytes w;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { w.push_back(static_cast<uint8_t>(v)); w.push_back(static_cast<uint8_t>(v >> 8)); };
    for (char c : std::string("RIFF")) w.push_back(static_cast<uint8_t>(c));
    u32(36 + static_cast<uint32_t>(pcm.size()));
    for (char c : std::string("WAVEfmt ")) w.push_back(static_cast<uint8_t>(c));
    u32(16); u16(1); u16(1); u32(44100); u32(44100); u16(1); u16(8);
    for (char c : std::string("data")) w.push_back(static_cast<uint8_t>(c));
    u32(static_cast<uint32_t>(pcm.size()));
    w.insert(w.end(), pcm.begin(), pcm.end());
    return w;
}

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
                  st.pause_reason.kind == PauseReason::Kind::Breakpoint && st.pause_reason.by == r.ss.client() &&
                  r.dbg->subscriptions(false).empty()))
                bad += std::string(" [") + spelling + " ok=" + std::to_string(ok) + " armed_paused=" +
                       std::to_string(!not_at_entry) + " pc=" + hex(st.pc) + "]";
        }
        check("DBGST-10", "--break-at 8003, $8003 and 0x8003 on a NEX: not paused after the load, stops at 0x8003 "
                          "(a Breakpoint of the command-line client), and the subscription never shows in the listing",
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
              ok && r.paused() && r.pc() == 0x8003 && r.dbg->state().pause_reason.kind == PauseReason::Kind::Breakpoint,
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
              landed && r.paused() && r.pc() == 0x8003 && r.dbg->state().pause_reason.kind == PauseReason::Kind::Breakpoint,
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

    // DBGST-19: a pause by another client before the stop does NOT cancel it (DeZog's CMD_INIT pauses
    // a machine still on its way): its stop removes our transient run-to, and it is put back when the
    // machine runs again, so the machine still stops at the target.
    {
        Rig r("8003");
        r.start(1);
        r.land(path("target.nex"));
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->pause(other);
        r.ss.poll();
        const bool kept = r.ss.active();
        r.dbg->run(other);
        r.run();
        check("DBGST-19", "a pause by another client before the --break-at stop does not cancel it: the machine still stops at the target "
                          "once it runs on",
              kept && r.paused() && r.pc() == 0x8003 && r.dbg->state().pause_reason.by == r.ss.client(),
              "kept=" + std::to_string(kept) + " paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()));
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

    // ---- stage 2: the default stop of a tape program (D2-D4) --------------------------------
    // The tape's CODE header is the oracle for where the program starts: 0x8000 in the z88dk
    // fixture (read from its header block here), and the loader's own addresses in the built tapes.
    const std::string fixture = std::string(JNEXT_TAPE_FIXTURE_DIR) + "/prog.tap";
    unsigned code_start = 0;
    Bytes fixture_bytes;
    {
        std::ifstream f(fixture, std::ios::binary);
        fixture_bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        for (size_t off = 0; off + 2 <= fixture_bytes.size();) {
            const unsigned n = fixture_bytes[off] | (fixture_bytes[off + 1] << 8);
            if (n == 19 && fixture_bytes[off + 2] == 0 && fixture_bytes[off + 3] == 3)
                code_start = fixture_bytes[off + 2 + 14] | (fixture_bytes[off + 2 + 15] << 8);
            off += 2 + n;
        }
    }
    constexpr int kTapeFrames = 1500, kRealFrames = 6000;
    // `delay`: frames the machine runs before the tape attaches, as the loop owner waits 100 frames
    // for a .tzx / .wav so BASIC is at its prompt (a .tap types LOAD "" itself).
    auto tape_stop = [&](Rig& r, const std::string& file, int frames, int delay = 0) {
        r.start(1);
        if (delay) r.run(delay);
        const bool ok = r.land(file);
        const bool armed_running = ok && !r.paused() && r.dbg->subscriptions(false).empty();
        r.run(frames);
        return armed_running;
    };
    auto in_stub = [](uint16_t pc) { return pc >= 0x5B00 && pc <= 0x5BFF; };

    // DBGST-30 / 31: the z88dk tape on a 48K: the stop is the first instruction of the CODE the
    // tape loaded (its header's start), after the last block; fast load and real time alike.
    {
        std::string bad;
        for (int mode = 0; mode < 2; ++mode) {
            Rig r("", MachineType::ZX48K, true);
            r.realtime = mode == 1;
            std::string log;
            bool armed = false;
            log = logged([&] { armed = tape_stop(r, fixture, mode ? kRealFrames : kTapeFrames); });
            if (!(armed && r.paused() && r.pc() == code_start && code_start == 0x8000 &&
                  log.find("(the first instruction from the tape's memory after its last block)") != std::string::npos))
                bad += " [" + std::string(mode ? "realtime" : "fast") + " armed=" + std::to_string(armed) +
                       " paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()) + "]";
        }
        check("DBGST-30", "48K z88dk tape, fast load and real time: paused at the CODE header's start after the last block, "
                          "having run no instruction of it before; the stop is logged with the tape's reason",
              bad.empty(), bad);
    }

    // DBGST-32: the same tape on a 128K and a +3: not in the ROM's RAM paging stubs (0x5B00..).
    {
        std::string bad;
        for (MachineType t : {MachineType::ZX128K, MachineType::ZX_PLUS3}) {
            Rig r("", t, true);
            tape_stop(r, fixture, 2500);
            if (!(r.paused() && r.pc() == code_start && !in_stub(r.pc())))
                bad += " [machine " + std::to_string(static_cast<int>(t)) + " paused=" + std::to_string(r.paused()) +
                       " pc=" + hex(r.pc()) + "]";
        }
        check("DBGST-32", "the same tape on a 128K and on a +3 stops at the program (0x8000), not in the ROM's RAM stubs at 0x5B00",
              bad.empty(), bad);
    }

    // DBGST-33: a TZX the test builds from that tape (standard 0x10 blocks, TZX 1.20) with a trailing
    // 0x30 text block, where at_end() stays false: fast and real time.
    {
        const Bytes tail{0x30, 4, 'e', 'n', 'd', '.'};
        write_bytes(path("prog.tzx"), tzx_of(fixture_bytes, tail));
        std::string bad;
        for (int mode = 0; mode < 2; ++mode) {
            Rig r("", MachineType::ZX48K, true);
            r.realtime = mode == 1;
            tape_stop(r, path("prog.tzx"), mode ? kRealFrames : kTapeFrames, 110);
            if (!(r.paused() && r.pc() == code_start))
                bad += " [" + std::string(mode ? "realtime" : "fast") + " paused=" + std::to_string(r.paused()) +
                       " pc=" + hex(r.pc()) + "]";
        }
        check("DBGST-33", "a TZX with a trailing text block stops at the program, fast and real time",
              bad.empty(), bad);
    }

    // DBGST-34: a WAV the test makes from that tape.
    {
        write_bytes(path("prog.wav"), wav_of(fixture_bytes));
        Rig r("", MachineType::ZX48K, true);
        tape_stop(r, path("prog.wav"), 8000, 110);
        check("DBGST-34", "a WAV made from the tape stops at the program after its last pulse",
              r.paused() && r.pc() == code_start, "paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()));
    }

    // The custom-loader tape: BASIC `LOAD ""CODE : RANDOMIZE USR 32768`; a 16-byte CODE loader at
    // 0x8000 that CALLs LD-BYTES (0x0556) for one more headerless block into 0xA000, then JPs there.
    const Bytes loader_code{0xDD, 0x21, 0x00, 0xA0,   // 8000 LD IX,0xA000
                            0x11, 0x10, 0x00,         // 8004 LD DE,16
                            0x3E, 0xFF,               // 8007 LD A,0xFF
                            0x37,                     // 8009 SCF
                            0xCD, 0x56, 0x05,         // 800A CALL 0x0556
                            0x00,                     // 800D NOP   <- the first instruction after the last block
                            0xC3, 0x00};              // 800E JP 0xA000 (the address byte follows in padding)
    Bytes loader_padded = loader_code;
    loader_padded.push_back(0xA0);
    const Bytes last_block(16, 0x00);   // never run: the stop is the NOP at 0x800D
    const Bytes loader_basic = basic_line(10, {0xEF, 0x22, 0x22, 0xAF, 0x3A, 0xF9, 0xC0, '3', '2', '7', '6', '8',
                                               0x0E, 0x00, 0x00, 0x00, 0x80, 0x00});
    const Bytes loader_tap = cat({program_pair(loader_basic),
                                  tap_header(3, "loader", static_cast<unsigned>(loader_padded.size()), 0x8000, 0x8000),
                                  tap_block(0xFF, loader_padded), tap_block(0xFF, last_block)});
    write_bytes(path("loader.tap"), loader_tap);

    // DBGST-35: the stop is the loader's instruction after the CALL: the first tape-loaded
    // instruction after delivery, not 0xA000 and not before the last block.
    {
        Rig r("", MachineType::ZX48K, true);
        tape_stop(r, path("loader.tap"), kTapeFrames);
        check("DBGST-35", "a custom loader that CALLs LD-BYTES for a last headerless block: paused at the instruction after the "
                          "CALL (0x800D), the last block delivered, not at 0xA000",
              r.paused() && r.pc() == 0x800D && r.emu.tape_delivered(),
              "paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()));
    }

    // DBGST-36 / 37: a tape that cannot carry code (rule 1): BASIC only, and BASIC + SCREEN$. The
    // stop is the first instruction after the last block's delivery: the trap's exit RET, 0x05E2
    // (tape_trap.h), in any memory.
    {
        const Bytes rem = basic_line(10, {0xEA});
        write_bytes(path("rem.tap"), program_pair(rem));
        const Bytes screen = basic_line(10, {0xEF, 0x22, 0x22, 0xAA});
        write_bytes(path("screen.tap"), cat({program_pair(screen),
                                              tap_header(3, "screen", 6912, 16384, 0x8000),
                                              tap_block(0xFF, Bytes(6912, 0x00))}));
        Rig a("", MachineType::ZX48K, true);
        tape_stop(a, path("rem.tap"), kTapeFrames);
        check("DBGST-36", "a BASIC-only tape (nothing can run from RAM): paused at the first instruction after its last block "
                          "was delivered, 0x05E2",
              a.paused() && a.pc() == 0x05E2 && !a.emu.tape_holds_code(), "paused=" + std::to_string(a.paused()) + " pc=" + hex(a.pc()));
        Rig b("", MachineType::ZX48K, true);
        tape_stop(b, path("screen.tap"), kTapeFrames);
        check("DBGST-37", "BASIC + SCREEN$ is still a tape that cannot carry code: the stop is the first instruction after "
                          "the screen's block, 0x05E2",
              b.paused() && b.pc() == 0x05E2 && !b.emu.tape_holds_code(), "paused=" + std::to_string(b.paused()) + " pc=" + hex(b.pc()));
    }

    // DBGST-38: --break-at 8000 on the loader tape stops INSIDE the loader, before the last block:
    // armed when the tape attaches, so it catches code a tape runs while it loads.
    {
        Rig r("8000", MachineType::ZX48K, true);
        tape_stop(r, path("loader.tap"), kTapeFrames);
        check("DBGST-38", "--break-at 8000 on the custom-loader tape stops at the loader's start with the last block not yet delivered",
              r.paused() && r.pc() == 0x8000 && !r.emu.tape_delivered(),
              "paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()));
    }

    // DBGST-39: the tape stop's subscriptions are transient (never listed), and another client's pause while
    // the tape loads (the usual DeZog attach) does not cancel the stop: the machine still stops at the program.
    {
        Rig r("", MachineType::ZX48K, true);
        r.start(1);
        r.land(fixture);
        r.run(100);   // mid-load
        const bool hidden = r.dbg->subscriptions(false).empty();
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->pause(other);
        r.ss.poll();
        const bool kept = r.ss.active();
        r.dbg->run(other);
        r.run(kTapeFrames);
        check("DBGST-39", "the tape stop's subscriptions are never listed, and another client's pause mid-load does not cancel the stop: "
                          "the machine still stops at the program",
              hidden && kept && r.paused() && r.pc() == code_start && r.dbg->state().pause_reason.by == r.ss.client(),
              "hidden=" + std::to_string(hidden) + " kept=" + std::to_string(kept) + " paused=" + std::to_string(r.paused()) +
                  " pc=" + hex(r.pc()));
    }

    // ---- round 1 of stage 2: rows the review asked for ---------------------------------------------
    auto loader_tape = [&](const Bytes& code, std::initializer_list<Bytes> extra) {
        Bytes t = cat({program_pair(loader_basic),
                       tap_header(3, "loader", static_cast<unsigned>(code.size()), 0x8000, 0x8000),
                       tap_block(0xFF, code)});
        for (const Bytes& e : extra) t = cat({t, e});
        return t;
    };
    const Bytes jr_block = [] { Bytes b(16, 0x00); b[0] = 0x18; b[1] = 0xFE; return b; }();

    // DBGST-40: the real-time capture watches each loaded range ONCE. The ROM's edge-wait loop passes
    // LD-BREAK every ~4 ms with IX and DE unchanged; a watch per pass left hundreds of live transients
    // (642 for this tape) and slowed a real-time load about 6x. The four LD-BYTES calls need a handful.
    {
        Rig r("", MachineType::ZX48K, true);
        r.realtime = true;
        r.start(1);
        r.land(fixture);
        size_t most = 0;
        for (int i = 0; i < kRealFrames && !r.paused(); ++i) {
            r.run(1);
            most = std::max(most, r.dbg->subscriptions(true).size());
        }
        check("DBGST-40", "a real-time tape load keeps a handful of live subscriptions (each loaded range watched once), "
                          "not one set per pass of the ROM's edge-wait loop",
              r.paused() && r.pc() == code_start && most <= 10, "most=" + std::to_string(most));
    }

    // DBGST-41: the capture ignores a VERIFY (carry of F' clear). A loader relocates a stub to 0xD000
    // (memory the tape never loaded); the stub VERIFYs a block at 0xC000, LOADs the last block into 0xA000,
    // then JP 0xC000 where a JP 0xA000 waits: the first TAPE-LOADED fetch is 0xA000, not the verified 0xC000.
    {
        const Bytes stub{0x3E, 0xFF, 0xA7, 0xDD, 0x21, 0x00, 0xC0, 0x11, 0x10, 0x00, 0xCD, 0x56, 0x05,   // VERIFY into C000
                         0x3E, 0xFF, 0x37, 0xDD, 0x21, 0x00, 0xA0, 0x11, 0x10, 0x00, 0xCD, 0x56, 0x05,   // LOAD into A000
                         0xC3, 0x00, 0xC0};                                                               // JP C000
        Bytes code{0x21, 0x19, 0x80, 0x11, 0x00, 0xD0, 0x01, static_cast<uint8_t>(stub.size()), 0x00, 0xED, 0xB0,
                   0x21, 0x00, 0xC0, 0x36, 0xC3, 0x23, 0x36, 0x00, 0x23, 0x36, 0xA0, 0xC3, 0x00, 0xD0};
        code.insert(code.end(), stub.begin(), stub.end());
        write_bytes(path("verify.tap"), loader_tape(code, {tap_block(0xFF, Bytes(16, 0x00)), tap_block(0xFF, jr_block)}));
        Rig r("", MachineType::ZX48K, true);
        tape_stop(r, path("verify.tap"), kTapeFrames);
        check("DBGST-41", "a VERIFY'd block is not tape-loaded memory: the stop is the first fetch from the block that was LOADed "
                          "(0xA000), not from the one only verified (0xC000)",
              r.paused() && r.pc() == 0xA000, "paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()));
    }

    // DBGST-42: LD-BREAK of ANOTHER ROM is not LD-BYTES. A +3 at power-on has ROM 0 at 0x056B; with the tape
    // landed, execution there with F' carry and Z set, IX=0x9000 and DE=0x100 must create no watch.
    {
        Rig r("", MachineType::ZX_PLUS3, true);
        r.start(1);
        r.land(fixture);
        Z80Registers rg = r.emu.cpu().get_registers();
        rg.PC = 0x056B;
        rg.AF = static_cast<uint16_t>(rg.AF | 0x40);
        rg.AF2 = static_cast<uint16_t>(rg.AF2 | 0x01);
        rg.IX = 0x9000;
        rg.DE = 0x0100;
        r.emu.cpu().set_registers(rg);
        r.emu.run_frame();
        int watches = 0;
        for (const auto& s : r.dbg->subscriptions(true))
            if (s.filter.lo == 0x9000) ++watches;
        check("DBGST-42", "execution at 0x056B in a +3's ROM 0 (not the 48K LD-BYTES) creates no watch",
              watches == 0, "watches=" + std::to_string(watches));
    }

    // DBGST-43: the watches are tied to the memory that was loaded. A 128K loader relocates a stub to 0x7000;
    // it puts a RET in bank 3 at 0xC000, pages bank 1 and LOADs the last block there, pages bank 3 and CALLs
    // 0xC000 (the RET: not tape-loaded), then pages bank 1 and JPs 0xC000 (tape-loaded). The stop is the JP.
    {
        auto out = [](uint8_t value) { return Bytes{0x01, 0xFD, 0x7F, 0x3E, value, 0xED, 0x79}; };   // LD BC,7FFD; LD A,v; OUT (C),A
        Bytes stub{0x31, 0x00, 0x7F};                                                              // LD SP,7F00
        for (const Bytes& p : {out(0x13), Bytes{0x3E, 0xC9, 0x32, 0x00, 0xC0}, out(0x11),
                               Bytes{0x3E, 0xFF, 0x37, 0xDD, 0x21, 0x00, 0xC0, 0x11, 0x10, 0x00, 0xCD, 0x56, 0x05},
                               out(0x13), Bytes{0xCD, 0x00, 0xC0}, out(0x11), Bytes{0xC3, 0x00, 0xC0}})
            stub.insert(stub.end(), p.begin(), p.end());
        Bytes code{0x21, 0x0E, 0x80, 0x11, 0x00, 0x70, 0x01, static_cast<uint8_t>(stub.size()), 0x00, 0xED, 0xB0, 0xC3, 0x00, 0x70};
        code.insert(code.end(), stub.begin(), stub.end());
        write_bytes(path("bank.tap"), loader_tape(code, {tap_block(0xFF, jr_block)}));
        Rig r("", MachineType::ZX128K, true);
        tape_stop(r, path("bank.tap"), 2500);
        check("DBGST-43", "128K: a stub executing 0xC000 in another bank is not the stop; the JP into the bank the tape loaded is "
                          "(SP 0x7F00, not 0x7EFE inside the CALL)",
              r.paused() && r.pc() == 0xC000 && r.emu.cpu().get_registers().SP == 0x7F00,
              "paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()) +
                  " sp=" + hex(r.emu.cpu().get_registers().SP));
    }

    // DBGST-44: a trailing 0x14 (pure data) block counts as data in real time: with one the stop comes after
    // it has played (two minutes of data later in master cycles), without it right after the CODE block.
    {
        const Bytes pure = [] {
            Bytes b{0x14, 0x57, 0x03, 0xAE, 0x06, 8, 0, 0, 200, 0, 0};   // zero 855, one 1710, 8 bits, pause 0, 200 bytes
            b.insert(b.end(), 200, 0x00);
            return b;
        }();
        const Bytes tap = loader_tape(jr_block, {});
        write_bytes(path("a.tzx"), tzx_of(tap, {}));
        write_bytes(path("b.tzx"), tzx_of(tap, pure));
        uint64_t cyc[2] = {0, 0};
        uint16_t pcs[2] = {0, 0};
        for (int i = 0; i < 2; ++i) {
            Rig r("", MachineType::ZX48K, true);
            r.realtime = true;
            tape_stop(r, path(i ? "b.tzx" : "a.tzx"), 9000, 110);
            cyc[i] = r.dbg->state().cycle;
            pcs[i] = r.pc();
        }
        check("DBGST-44", "a real-time TZX with a trailing pure-data (0x14) block stops after that block has played, not at the "
                          "loader's first instruction",
              pcs[0] == 0x8000 && pcs[1] == 0x8000 && cyc[1] > cyc[0] + 10000000ull,
              "pcs=" + hex(pcs[0]) + "/" + hex(pcs[1]) + " cycles=" + std::to_string(cyc[0]) + "/" + std::to_string(cyc[1]));
    }

    // ---- round 2 of stage 2: the stop survives, and interferes with nothing ---------------------------
    // The startup stop's subscriptions are removed by ANY client's stop and put back by subscribe() alone.
    write_nex(path("loop.nex"), {0x00, 0x00, 0x00, 0x00, 0x18, 0xFA}, ENTRY);   // NOP x4, JR 0x8000: passes 0x8003 every loop
    auto owned = [](Rig& r) {
        size_t n = 0;
        for (const auto& s : r.dbg->subscriptions(true)) if (s.owner == r.ss.client()) ++n;
        return n;
    };
    auto owned_covering = [](Rig& r, uint16_t addr) {
        for (const auto& s : r.dbg->subscriptions(true))
            if (s.owner == r.ss.client() && s.filter.lo <= addr && addr <= s.filter.hi) return true;
        return false;
    };

    // DBGST-45: another client's pause and resume with NO poll between them (one pipelined pump) removes the
    // transient; poll() must notice from the subscriptions, not from a pause it did not see.
    {
        Rig r("8003");
        r.start(1);
        r.land(path("target.nex"));
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->pause(other);
        r.dbg->run(other);
        r.ss.poll();
        r.run();
        const auto st = r.dbg->state();
        check("DBGST-45", "another client's pause + run between two polls does not lose the --break-at stop: it still stops at 0x8003, "
                          "a Breakpoint of the command-line client",
              st.paused && st.pc == TARGET && st.pause_reason.by == r.ss.client() &&
                  st.pause_reason.kind == PauseReason::Kind::Breakpoint,
              "paused=" + std::to_string(st.paused) + " pc=" + hex(st.pc));
    }

    // DBGST-46: the same loss mid-load of the z88dk tape (after the first block's capture): the tape stop survives.
    {
        Rig r("", MachineType::ZX48K, true);
        r.start(1);
        r.land(fixture);
        for (int i = 0; i < kTapeFrames && !r.paused() && owned(r) < 2; ++i) r.run(1);
        const bool mid = !r.paused() && owned(r) >= 2;
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->pause(other);
        r.dbg->run(other);
        r.ss.poll();
        r.run(kTapeFrames);
        check("DBGST-46", "another client's pause + run between two polls mid-load does not lose the tape stop: it stops at the program",
              mid && r.paused() && r.pc() == code_start,
              "mid=" + std::to_string(mid) + " paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()));
    }

    // DBGST-47: re-adding the stop uses subscribe() and no run-control verb: another client's Step Out (armed
    // after our subscription was removed and put back) keeps its step mode, completes at the return address, and
    // our stop is still taken afterwards.
    {
        write_nex(path("call.nex"), {0xCD, 0x10, 0x80, 0x00, 0x00, 0x18, 0xFE, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                     0x00, 0x00, 0xC9}, ENTRY);   // 8000 CALL 8010; 8003 NOP; 8004 NOP; 8005 JR $; 8010 NOP NOP RET
        Rig r("8004");
        r.start(1);
        r.land(path("call.nex"));
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->pause(other);
        r.ss.poll();   // ours put back while the other client holds the machine
        r.dbg->step_into(other);   // into the CALL: PC 0x8010
        r.dbg->step_out(other);
        r.ss.poll();
        const auto before = r.dbg->state();
        const bool mode_kept = before.step_mode == jnext::dbg::StepMode::Out;
        for (int i = 0; i < 6 && !r.paused(); ++i) { r.emu.run_frame(); r.dbg->pump(jnext::dbg::PumpBudget{}); }
        const auto step_stop = r.dbg->state();
        const bool stepped = step_stop.paused && step_stop.pc == 0x8003 && step_stop.pause_reason.by == other &&
                             step_stop.pause_reason.kind == PauseReason::Kind::Step;
        r.dbg->run(other);
        r.ss.poll();
        r.run();
        const auto st = r.dbg->state();
        check("DBGST-47", "re-adding the startup stop leaves another client's Step Out alone (step mode kept, it completes at the "
                          "return address 0x8003), and our stop is still taken at 0x8004 afterwards",
              mode_kept && stepped && st.paused && st.pc == 0x8004 && st.pause_reason.by == r.ss.client(),
              "mode_kept=" + std::to_string(mode_kept) + " stepped=" + std::to_string(stepped) + " pc=" + hex(st.pc) +
                  " by_us=" + std::to_string(st.pause_reason.by == r.ss.client()));
    }

    // DBGST-48: real time, a foreign pause AFTER the last block's capture and BEFORE its delivery: the re-added stop must carry
    // the ranges already captured (the capture does not fire again for that LD-BYTES call).
    {
        Rig r("", MachineType::ZX48K, true);
        r.realtime = true;
        r.start(1);
        r.land(fixture);
        bool mid = false;
        for (int i = 0; i < kRealFrames && !r.paused(); ++i) {
            r.run(1);
            if (owned_covering(r, 0x8000) && !r.emu.tape_delivered()) { mid = true; break; }
        }
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->pause(other);
        r.ss.poll();
        r.dbg->run(other);
        r.run(kRealFrames);
        check("DBGST-48", "a foreign pause between the last block's capture and its delivery keeps the watches already captured: "
                          "the machine stops at the program",
              mid && r.paused() && r.pc() == code_start,
              "mid=" + std::to_string(mid) + " paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()));
    }

    // DBGST-49: our stop, then another client's run in the same pump (no poll between): the stop is recorded at delivery,
    // logged once, and not taken a second time on the next pass over the target.
    {
        Rig r("8003");
        r.start(1);
        r.land(path("loop.nex"));
        for (int i = 0; i < 6 && !r.paused(); ++i) r.emu.run_frame();   // no poll: the loop owner's pump has not come yet
        const bool stopped = r.paused() && r.pc() == TARGET;
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->run(other);
        int lines = 0;
        const std::string log = logged([&] { r.ss.poll(); r.ss.poll(); r.run(4); });
        for (size_t at = log.find("debugger: stopped at"); at != std::string::npos; at = log.find("debugger: stopped at", at + 1)) ++lines;
        check("DBGST-49", "our --break-at stop followed at once by another client's run: logged exactly once, the machine is not "
                          "stopped again at the next pass, and the client detaches",
              stopped && lines == 1 && !r.paused() && !r.ss.active(),
              "stopped=" + std::to_string(stopped) + " lines=" + std::to_string(lines) + " active=" + std::to_string(r.ss.active()));
    }

    // DBGST-50: another client holds the machine paused ON our stop point (it stepped there): the stop is reached, and after
    // its run the next pass over the address (the step-off skipped the gate) is not a second stop.
    {
        Rig r("8003");
        r.start(1);
        r.land(path("loop.nex"));
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->pause(other);
        r.dbg->step_into(other);
        r.dbg->step_into(other);
        r.dbg->step_into(other);   // 8000 -> 8003
        std::string log = logged([&] { r.ss.poll(); });
        const bool reached = r.paused() && r.pc() == TARGET &&
                             log.find("reached while another debugger held the machine") != std::string::npos;
        r.dbg->run(other);
        r.ss.poll();
        r.run(4);
        check("DBGST-50", "a machine another client holds paused on the stop point has reached the stop (logged as such); after its "
                          "run there is no second stop at the next pass",
              reached && !r.paused() && !r.ss.active(),
              "reached=" + std::to_string(reached) + " paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()) + " " + log);
    }

    // DBGST-53: the step-off half of "reached": another client steps onto the stop point and runs at once, with no
    // poll between (one pipelined pump). Nothing holds the machine any more, but the resume's step-off will skip our
    // gate at that instruction: the poll that follows must still count the stop as reached (logged as such), with no
    // second stop at the next pass, and the client detaches.
    {
        Rig r("8003");
        r.start(1);
        r.land(path("loop.nex"));
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->pause(other);
        r.dbg->step_into(other);
        r.dbg->step_into(other);
        r.dbg->step_into(other);   // 8000 -> 8003
        r.dbg->run(other);         // no poll since the pause: ours is gone, the step-off is armed
        const bool armed_off = !r.paused() && r.emu.debug_state().step_off_pending();
        int lines = 0, reached = 0;
        const std::string log = logged([&] { r.ss.poll(); r.run(4); });
        for (size_t at = log.find("debugger: stopped at"); at != std::string::npos; at = log.find("debugger: stopped at", at + 1)) ++lines;
        if (log.find("reached while another debugger held the machine") != std::string::npos) reached = 1;
        check("DBGST-53", "another client stepping onto the stop point and running at once (no poll between): the stop is logged as reached, "
                          "once, with no second stop at the next pass, and the client detaches",
              armed_off && lines == 1 && reached == 1 && !r.paused() && !r.ss.active(),
              "armed_off=" + std::to_string(armed_off) + " lines=" + std::to_string(lines) + " reached=" + std::to_string(reached) +
                  " paused=" + std::to_string(r.paused()) + " active=" + std::to_string(r.ss.active()));
    }

    // DBGST-51: a --break-at landing while another client holds the machine paused leaves that pause alone (run_to would have
    // resumed it), and the stop is taken once it runs; an --inject landing (a pause-type stop) takes the held pause as the stop
    // instead of re-attributing it.
    {
        Rig r("8003");
        r.start(1);
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->pause(other);
        // An --inject landing: a NEX load rebuilds the machine and would clear the other client's pause itself.
        write_bytes(path("inject3.bin"), {0x00, 0x00, 0x00, 0x00, 0x18, 0xFE});
        const bool inj1 = r.emu.inject_binary(path("inject3.bin"), 0x8000, 0x8000);
        r.ss.program_landed(path("inject3.bin"), inj1, true, r.why);
        const auto held = r.dbg->state();
        const bool still_held = held.paused && held.pause_reason.by == other && owned(r) >= 1;
        r.dbg->run(other);
        r.run();
        const bool stopped = r.paused() && r.pc() == TARGET && r.dbg->state().pause_reason.by == r.ss.client();

        write_bytes(path("inject2.bin"), {0x00, 0x00, 0x18, 0xFE});
        Rig q;
        q.start(1);
        const ClientId other2 = q.dbg->attach(other_client("remote")).value;
        q.dbg->pause(other2);
        const bool injected = q.emu.inject_binary(path("inject2.bin"), 0x8000, 0x8000);
        const std::string log = logged([&] { q.ss.program_landed(path("inject2.bin"), injected, true, q.why); });
        const auto qs = q.dbg->state();
        const bool adopted = q.paused() && qs.pause_reason.by == other2 &&
                             log.find("reached while another debugger held the machine") != std::string::npos;
        check("DBGST-51", "a landing while another client holds the machine: --break-at neither resumes it nor re-pauses it and "
                          "still stops once it runs; a pause-type stop takes that pause (the pause stays the other client's)",
              still_held && stopped && adopted,
              "still_held=" + std::to_string(still_held) + " stopped=" + std::to_string(stopped) + " adopted=" + std::to_string(adopted));
    }

    // DBGST-52: the decision "standing on the stop point counts as reaching it", over its truth table.
    check("DBGST-52", "reached_here(): not on the point false; on it and held by another client true; on it, running, step-off pending true; "
                      "on it, running, no step-off false (the gate fires at the next instruction)",
          !StartupStop::reached_here(true, true, false) && !StartupStop::reached_here(false, false, false) &&
              StartupStop::reached_here(true, false, true) && StartupStop::reached_here(false, true, true) &&
              !StartupStop::reached_here(false, false, true));

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

    // The SLD fixture of DBGST-15/16 for the entry PC: a label and a source line at 0x8000, both
    // assembled for `page` (0x8000 is in slot 4).
    auto entry_sld = [&](unsigned page) {
        std::ostringstream o;
        o << "|SLD.data.version|1\n"
          << "x.asm|3||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
             "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
          << "x.asm|5||0|" << page << "|32768|L|,entryname,,\n"
          << "x.asm|20||0|" << page << "|32768|T|\n";
        write_text(path("paged.sld"), o.str());
    };
    unsigned entry_page = 0;
    {
        Rig probe;
        probe.start(1);
        probe.land(path("paged.nex"));
        entry_page = probe.dbg->mmu_slots()[4].space.index;
    }

    // DBGST-22: the at-PC stop is memory-qualified too: a symbol AT the entry address, assembled
    // for another page than the one mapped there, is not the stop (the program runs on).
    {
        entry_sld(entry_page + 1);
        Rig other("entryname");
        other.start(1);
        const bool ok = other.land(path("paged.nex"));
        other.run();
        entry_sld(entry_page);
        Rig same("entryname");
        same.start(1);
        const bool ok2 = same.land(path("paged.nex"));
        check("DBGST-22", "a symbol at the entry PC assembled for the next page is not an at-PC stop (not paused after the "
                          "load); assembled for the mapped page it stops at the entry at once",
              ok && !other.paused() && ok2 && same.paused() && same.pc() == ENTRY,
              "other_paused=" + std::to_string(other.paused()) + " same_paused=" + std::to_string(same.paused()) +
                  " " + other.why);
    }

    // DBGST-23: a source line that starts at the entry PC stops there at once, as an address does.
    {
        entry_sld(entry_page);
        Rig r("x.asm:20");
        r.start(1);
        const bool ok = r.land(path("paged.nex"));
        check("DBGST-23", "--break-at file:line whose line starts at the entry PC stops at the entry right after the load",
              ok && r.paused() && r.pc() == ENTRY && r.dbg->state().pause_reason.by == r.ss.client(),
              "paused=" + std::to_string(r.paused()) + " pc=" + hex(r.pc()) + " " + r.why);
    }

    // DBGST-24: the detach rule's second half: another client STEPS the paused machine (it stays
    // paused, its cycle moved); the startup client is then gone, so when that client detaches
    // nothing holds the pause and the machine is released (a crashed DeZog cannot leave it hung).
    {
        Rig r;
        r.start(0);
        const ClientId other = r.dbg->attach(other_client("remote")).value;
        r.dbg->step_into(other);
        const bool stepped = r.paused();
        r.ss.poll();
        const bool gone = !r.ss.active();
        r.dbg->detach(other);
        check("DBGST-24", "after another client steps the paused machine the startup client detaches on the next poll, so "
                          "that client's own detach leaves the machine running",
              stepped && gone && !r.paused(),
              "stepped=" + std::to_string(stepped) + " gone=" + std::to_string(gone) + " paused=" + std::to_string(r.paused()));
    }

    // DBGST-25: a failed load stops where the machine is even with --break-at (no run-to is armed).
    {
        Rig r("8003");
        r.start(1);
        bool ok = false;
        const std::string log = logged([&] { ok = r.land(path("broken.nex")); });
        check("DBGST-25", "a failed load with --break-at 8003: paused where the machine is, the log says the load failed",
              ok && r.paused() && log.find("(the load failed)") != std::string::npos, log);
    }

    std::filesystem::remove_all(g_dir);
    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
