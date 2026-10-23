// TZX container validation tests.
//
// No VHDL oracle: a .tzx file is a host-side container the FPGA core never
// sees. The oracle for what counts as MALFORMED is libspectrum's TZX reader —
// internal_tzx_read() in tzx_read.c, the library FUSE loads tapes with — run
// as a live library (libspectrum 1.5.0, libspectrum_tape_read() with
// LIBSPECTRUM_ID_TAPE_TZX, then libspectrum_tape_present()) over the byte
// patterns these rows build. `tzx_loader_test --keep-fixtures DIR` writes
// every fixture to DIR so they can be fed to it again. What it says:
//
//   - fewer than 10 bytes              -> LIBSPECTRUM_ERROR_CORRUPT
//   - no "ZXTape!\x1A" signature       -> LIBSPECTRUM_ERROR_SIGNATURE (this is
//     what a random file, and a TAP file renamed .tzx, get)
//   - a block that runs past the end   -> LIBSPECTRUM_ERROR_CORRUPT
//   - a block ID it does not implement -> LIBSPECTRUM_ERROR_UNKNOWN, including
//     the TZX spec's own $16-$18, $26, $27, $34 and $40. HERE JNEXT ACCEPTS
//     MORE: it follows the TZX specification for those (TZXC-60..99 below).
//   - the header and nothing else      -> read, but no tape present
//   - a bad checksum in a complete block -> accepted (a ROM "R Tape loading
//     error" when that block is read, not a container error)
//
// Before this suite, ZOT's tzx_load() accepted any file of two bytes or more,
// and one without the signature as TAP data: a random or truncated .tzx
// "loaded".
//
// Run: ./build/test/tzx_loader_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "core/tzx_loader.h"
#include "ld_trap_rig.h"

#include <spdlog/sinks/ostream_sink.h>

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int passed = 0;
int failed = 0;
int total = 0;

void check(const char* id, const char* desc, bool condition,
           const std::string& detail = {}) {
    report_row_id(id);
    ++total;
    if (condition) {
        ++passed;
    } else {
        ++failed;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using Bytes = std::vector<uint8_t>;

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

Bytes le16(uint32_t v) { return {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)}; }
Bytes le24(uint32_t v) {
    return {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16)};
}
Bytes le32(uint32_t v) {
    return {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
            static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
}
Bytes str(const char* s) { return Bytes(s, s + std::strlen(s)); }
Bytes pstr(const char* s) {   // TZX string: 1-byte length + text
    Bytes out = {static_cast<uint8_t>(std::strlen(s))};
    Bytes t = str(s);
    out.insert(out.end(), t.begin(), t.end());
    return out;
}

const Bytes kHeader = concat({str("ZXTape!"), {0x1A, 0x01, 0x14}});   // v1.20

// A TAP-style block body: flag + payload + XOR checksum.
Bytes tap_body(uint8_t flag, const Bytes& payload, bool good = true) {
    uint8_t sum = flag;
    for (uint8_t b : payload) sum ^= b;
    if (!good) sum ^= 0x55;
    Bytes out = {flag};
    out.insert(out.end(), payload.begin(), payload.end());
    out.push_back(sum);
    return out;
}
Bytes hdr_body(bool good = true) {
    return tap_body(0x00, {3, 'T', 'E', 'S', 'T', ' ', ' ', ' ', ' ', ' ', ' ',
                           5, 0, 0x00, 0x80, 0x00, 0x80}, good);
}
Bytes data_body() { return tap_body(0xFF, {1, 2, 3, 4, 5}); }

// Block $10: pause + 2-byte length + data.
Bytes b10(const Bytes& body) {
    return concat({{0x10}, le16(1000), le16(static_cast<uint32_t>(body.size())), body});
}

std::filesystem::path g_root;

std::string write_file(const std::string& name, const Bytes& bytes) {
    const auto path = g_root / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

int count_occurrences(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) ++n;
    return n;
}

struct Loaded { bool ok; bool is_loaded; };
Loaded load_bytes(const std::string& name, const Bytes& bytes) {
    TzxLoader t;
    const bool ok = t.load(write_file(name, bytes));
    return {ok, t.is_loaded()};
}
std::string detail(const Loaded& l) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "load=%d is_loaded=%d", l.ok ? 1 : 0, l.is_loaded ? 1 : 0);
    return buf;
}

// One block type, twice: complete and followed by a $10 data block (so the
// walk must land exactly on that block's ID to accept it), and cut one byte
// short at the end of the file.
struct BlockCase {
    const char* id_ok;
    const char* id_cut;
    const char* name;
    Bytes block;
};

} // namespace

int main(int argc, char** argv) {
    std::printf("TZX container validation tests\n");
    std::printf("===============================================\n\n");

    bool keep = false;
    if (argc == 3 && std::strcmp(argv[1], "--keep-fixtures") == 0) {
        g_root = argv[2];
        keep = true;
    } else {
        g_root = std::filesystem::temp_directory_path() /
                 ("jnext-tzx-loader-test-" + std::to_string(static_cast<long>(::getpid())));
    }
    std::error_code ec;
    if (!keep) std::filesystem::remove_all(g_root, ec);
    std::filesystem::create_directories(g_root, ec);

    std::ostringstream log_out;
    auto log_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(log_out);
    log_sink->set_pattern("%l %v");
    Log::emulator()->sinks().push_back(log_sink);

    const Bytes valid = concat({kHeader, b10(hdr_body()), b10(data_body())});

    // TZXC-01 — the positive control every rejection is measured against.
    {
        const Loaded l = load_bytes("valid.tzx", valid);
        check("TZXC-01", "a well-formed two-block TZX loads",
              l.ok && l.is_loaded, detail(l));
    }

    // TZXC-02 — the reported defect: a random file named .tzx.
    {
        Bytes bytes(300);
        uint32_t x = 12345;
        for (auto& b : bytes) { x = x * 1103515245u + 12345u; b = static_cast<uint8_t>(x >> 16); }
        const Loaded l = load_bytes("random.tzx", bytes);
        check("TZXC-02", "300 random bytes are refused (libspectrum SIGNATURE)",
              !l.ok && !l.is_loaded, detail(l));
    }

    // TZXC-03 — a TAP file renamed .tzx. ZOT used to play it as TAP data;
    // libspectrum reads a .tzx as TZX and refuses it.
    {
        const Bytes hdr = hdr_body();
        const Bytes dat = data_body();
        const Bytes tap = concat({le16(static_cast<uint32_t>(hdr.size())), hdr,
                                  le16(static_cast<uint32_t>(dat.size())), dat});
        const Loaded l = load_bytes("tapcontent.tzx", tap);
        check("TZXC-03", "TAP content without the TZX signature is refused (libspectrum SIGNATURE)",
              !l.ok && !l.is_loaded, detail(l));
    }

    // TZXC-04 — a truncated tape: the second block cut short.
    {
        Bytes bytes = valid;
        bytes.resize(bytes.size() - 3);
        const Loaded l = load_bytes("truncated.tzx", bytes);
        check("TZXC-04", "a block running past end of file rejects the whole tape "
              "(libspectrum CORRUPT)", !l.ok && !l.is_loaded, detail(l));
    }

    // TZXC-05 — the 10-byte header and nothing else: libspectrum reads it but
    // finds no tape in it, the verdict TapLoader gives an empty .tap (TAPC-05).
    {
        const Loaded l = load_bytes("hdronly.tzx", kHeader);
        check("TZXC-05", "a header with no blocks is refused (libspectrum: no tape present)",
              !l.ok && !l.is_loaded, detail(l));
    }

    // TZXC-06/07 — too short for the header.
    {
        const Loaded l = load_bytes("nine.tzx", Bytes(kHeader.begin(), kHeader.begin() + 9));
        check("TZXC-06", "9 bytes (signature + one version byte) are refused (libspectrum CORRUPT)",
              !l.ok && !l.is_loaded, detail(l));
    }
    {
        const Loaded l = load_bytes("empty.tzx", {});
        check("TZXC-07", "an empty file is refused (libspectrum CORRUPT)",
              !l.ok && !l.is_loaded, detail(l));
    }

    // TZXC-08 — complete blocks, then a lone block ID with no body.
    {
        const Loaded l = load_bytes("trailing.tzx", concat({valid, {0x10}}));
        check("TZXC-08", "a stray block ID after the last block is refused (libspectrum CORRUPT)",
              !l.ok && !l.is_loaded, detail(l));
    }

    // TZXC-09 — a bad checksum inside a complete block is not a container error.
    {
        const Loaded l = load_bytes("badsum.tzx",
                                    concat({kHeader, b10(hdr_body(false)), b10(data_body())}));
        check("TZXC-09", "a bad checksum in a complete block still loads", l.ok && l.is_loaded,
              detail(l));
    }

    // TZXC-10 — the rejection is one error line naming the file and the reason.
    {
        log_out.str("");
        Bytes bytes = valid;
        bytes.resize(bytes.size() - 3);
        TzxLoader t;
        const bool ok = t.load(write_file("reason.tzx", bytes));
        const std::string log = log_out.str();
        check("TZXC-10", "a rejected TZX logs an error naming the file and the overrun block",
              !ok && log.find("error TZX: '") != std::string::npos &&
              log.find("reason.tzx' is not a valid TZX file: block 1 (ID $10) at offset 34 "
                       "runs past the end of the file") != std::string::npos,
              log);
    }

    // TZXC-11 — a refused load leaves a loader that already holds a tape
    // holding it.
    {
        TzxLoader t;
        const bool first = t.load(write_file("keep-good.tzx", valid));
        const bool second = t.load(write_file("keep-bad.tzx", Bytes(40, 0xAA)));
        check("TZXC-11", "a refused load() keeps the tape the loader already had",
              first && !second && t.is_loaded() && t.filename() == "keep-good.tzx",
              "filename=" + t.filename());
    }

    // TZXC-12 — Emulator::load_tzx(), the path --load and the GUI Tape menu
    // take: refused, and the tape already in the player stays.
    {
        auto emu = std::make_unique<Emulator>();
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        const bool init_ok = emu->init(cfg);
        const bool good_ok = init_ok && emu->load_tzx(write_file("player-good.tzx", valid));
        const bool bad_ok = emu->load_tzx(write_file("player-bad.tzx", Bytes(64, 0x11)));
        char buf[96];
        std::snprintf(buf, sizeof(buf), "init=%d good=%d bad=%d loaded=%d name=%s",
                      init_ok ? 1 : 0, good_ok ? 1 : 0, bad_ok ? 1 : 0,
                      emu->tzx_tape().is_loaded() ? 1 : 0,
                      emu->tzx_tape().filename().c_str());
        check("TZXC-12", "Emulator::load_tzx refuses a malformed tape; the loaded one stays",
              good_ok && !bad_ok && emu->tzx_tape().is_loaded() &&
              emu->tzx_tape().filename() == "player-good.tzx", buf);
    }

    // TZXC-13 — a $19 generalised-data block whose parts do not add up to its
    // declared length (one extra byte inside it): libspectrum's "sanity check
    // failed".
    {
        // pause 0, TOTP 0, NPP 0, ASP 0, TOTD 8, NPD 1, ASD 2, table 2x(1+2),
        // one data byte: 14 + 6 + 1 = 21; declare 22 and carry 22 bytes.
        const Bytes body = concat({le16(0), le32(0), {0, 0}, le32(8), {1, 2},
                                   {0}, le16(855), {0}, le16(1710), {0xA5}, {0x00}});
        const Loaded l = load_bytes("gdb-inconsistent.tzx",
                                    concat({kHeader, {0x19}, le32(22), body}));
        check("TZXC-13", "a $19 block whose parts fall short of its declared length is "
              "refused (libspectrum CORRUPT, sanity check)", !l.ok && !l.is_loaded, detail(l));
    }

    // TZXC-14 — the two blocks with no body at all, group end ($22) and loop
    // end ($25), closing the group and loop they belong to.
    {
        const Loaded l = load_bytes("group-loop.tzx",
            concat({kHeader, {0x21}, pstr("grp"), b10(hdr_body()), {0x22},
                    {0x24}, le16(2), b10(data_body()), {0x25}}));
        check("TZXC-14", "group end ($22) and loop end ($25), which have no body, load",
              l.ok && l.is_loaded, detail(l));
    }

    // TZXC-15 — a header followed only by a $5A glue block: libspectrum skips
    // the glue without appending a block, so it too finds no tape.
    {
        const Loaded l = load_bytes("glue-only.tzx",
            concat({kHeader, {0x5A}, str("XTape!"), {0x1A, 0x01, 0x14}}));
        check("TZXC-15", "a header plus only a $5A glue block is refused "
              "(libspectrum: no tape present)", !l.ok && !l.is_loaded, detail(l));
    }

    // TZXC-20..59 — every block type libspectrum implements, complete (loads)
    // and cut one byte short (refused).
    const Bytes gdb_body = concat({le16(0), le32(0), {0, 0}, le32(8), {1, 2},
                                   {0}, le16(855), {0}, le16(1710), {0xA5}});
    const BlockCase cases[] = {
        {"TZXC-20", "TZXC-21", "10-standard", b10(hdr_body())},
        {"TZXC-22", "TZXC-23", "11-turbo",
         concat({{0x11}, le16(2168), le16(667), le16(735), le16(855), le16(1710),
                 le16(3223), {8}, le16(1000), le24(7), data_body()})},
        {"TZXC-24", "TZXC-25", "12-tone", concat({{0x12}, le16(2168), le16(10)})},
        {"TZXC-26", "TZXC-27", "13-pulses", concat({{0x13, 2}, le16(667), le16(735)})},
        {"TZXC-28", "TZXC-29", "14-puredata",
         concat({{0x14}, le16(855), le16(1710), {8}, le16(1000), le24(7), data_body()})},
        {"TZXC-30", "TZXC-31", "15-direct",
         concat({{0x15}, le16(79), le16(0), {8}, le24(4), {0xAA, 0x55, 0xAA, 0x55}})},
        {"TZXC-32", "TZXC-33", "19-generalised",
         concat({{0x19}, le32(static_cast<uint32_t>(gdb_body.size())), gdb_body})},
        {"TZXC-34", "TZXC-35", "20-pause", concat({{0x20}, le16(1000)})},
        {"TZXC-36", "TZXC-37", "21-group", concat({{0x21}, pstr("group")})},
        {"TZXC-38", "TZXC-39", "23-jump", concat({{0x23}, le16(1)})},
        {"TZXC-40", "TZXC-41", "24-loop", concat({{0x24}, le16(2)})},
        {"TZXC-42", "TZXC-43", "28-select",
         concat({{0x28}, le16(7), {1}, le16(1), pstr("abc")})},
        {"TZXC-44", "TZXC-45", "2a-stop48", concat({{0x2A}, le32(0)})},
        {"TZXC-46", "TZXC-47", "2b-level", concat({{0x2B}, le32(1), {1}})},
        {"TZXC-48", "TZXC-49", "30-comment", concat({{0x30}, pstr("comment")})},
        {"TZXC-50", "TZXC-51", "31-message", concat({{0x31, 5}, pstr("msg")})},
        {"TZXC-52", "TZXC-53", "32-archive",
         concat({{0x32}, le16(12), {2, 0x00}, pstr("Game"), {0x02}, pstr("Me")})},
        {"TZXC-54", "TZXC-55", "33-hardware", concat({{0x33, 1, 0x00, 0x03, 0x01}})},
        {"TZXC-56", "TZXC-57", "35-custom",
         concat({{0x35}, str("CUSTOMINFO      "), le32(2), {'x', 'y'}})},
        {"TZXC-58", "TZXC-59", "5a-glue", concat({{0x5A}, str("XTape!"), {0x1A, 0x01, 0x14}})},
    };
    for (const auto& c : cases) {
        const std::string n = c.name;
        const Loaded ok = load_bytes(n + "-ok.tzx", concat({kHeader, c.block, b10(data_body())}));
        check(c.id_ok, (n + ": complete block, then a data block, loads").c_str(),
              ok.ok && ok.is_loaded, detail(ok));
        Bytes cut = concat({kHeader, c.block});
        cut.pop_back();
        const Loaded bad = load_bytes(n + "-cut.tzx", cut);
        check(c.id_cut, (n + ": one byte short at end of file is refused").c_str(),
              !bad.ok && !bad.is_loaded, detail(bad));
    }

    // TZXC-60..99 — the block IDs libspectrum does NOT implement. Here jnext
    // deliberately accepts more than that library (which refuses them all,
    // LIBSPECTRUM_ERROR_UNKNOWN): the TZX specification defines each with a
    // length formula, and gives every later ID one by its General Extension
    // Rule, so such a block is valid when its length fields fit the file —
    // and refused, like any block, when they do not. The player cannot play
    // any of them and skips each, so the blocks after it still load, fast or
    // in real time. Oracle: the TZX specification
    // (worldofspectrum.net/TZXformat.html), the "length:" of each block.
    // (The $19 rows: libspectrum does implement $19 — TZXC-32/33 — but the
    // player does not; it used to stop playback there.)
    struct Rare {
        const char* id_ok; const char* id_cut; const char* id_fast; const char* id_real;
        const char* name; Bytes block;
    };
    const Rare rare[] = {
        // $16/$17: the DWORD is the WHOLE block's length, itself included.
        {"TZXC-60", "TZXC-70", "TZXC-80", "TZXC-90", "16-c64rom",
         concat({{0x16}, le32(8), {1, 2, 3, 4}})},
        {"TZXC-61", "TZXC-71", "TZXC-81", "TZXC-91", "17-c64turbo",
         concat({{0x17}, le32(6), {1, 2}})},
        {"TZXC-62", "TZXC-72", "TZXC-82", "TZXC-92", "18-csw",          // [00..03]+04
         concat({{0x18}, le32(4), {1, 2, 3, 4}})},
        {"TZXC-63", "TZXC-73", "TZXC-83", "TZXC-93", "26-call",         // [00,01]*02+02
         concat({{0x26}, le16(2), le16(1), le16(1)})},
        {"TZXC-64", nullptr,   "TZXC-84", "TZXC-94", "27-return",       // 00
         Bytes{0x27}},
        {"TZXC-65", "TZXC-75", "TZXC-85", "TZXC-95", "34-emuinfo",      // 08
         concat({{0x34}, Bytes(8, 0x11)})},
        {"TZXC-66", "TZXC-76", "TZXC-86", "TZXC-96", "40-snapshot",     // [01,02,03]+04
         concat({{0x40, 0x00}, le24(3), {1, 2, 3}})},
        {"TZXC-67", "TZXC-77", "TZXC-87", "TZXC-97", "5b-unknown",      // extension rule
         concat({{0x5B}, le32(3), {1, 2, 3}})},
        {nullptr,   nullptr,   "TZXC-88", "TZXC-98", "19-generalised",
         concat({{0x19}, le32(static_cast<uint32_t>(gdb_body.size())), gdb_body})},
    };
    // The fast-load path: the ROM LD-BYTES trap, run for the header block and
    // then the data block; true when the second one delivers the data's bytes.
    auto fast_load_reaches_data = [&](const std::string& path) {
        auto emu = std::make_unique<Emulator>();
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 0;
        TzxLoader t;
        if (!emu->init(cfg) || !t.load(path)) return false;
        auto trap = [&](uint16_t af, uint16_t ix, uint16_t de) {
            Z80Registers r = emu->cpu().get_registers();
            r.AF2 = af; r.IX = ix; r.DE = de; r.SP = 0x9000;   // LD-BREAK: flag/mode in AF'
            emu->mmu().write(0x9000, 0x34);
            emu->mmu().write(0x9001, 0x12);
            emu->cpu().set_registers(r);
            return t.handle_ld_bytes_trap(*emu) && (emu->cpu().get_registers().AF & 1) != 0;
        };
        const bool hdr = trap(0x0001, 0x8000, 17);   // flag $00, LOAD
        const bool dat = trap(0xFF01, 0x8100, 5);    // flag $FF, LOAD
        bool bytes = true;
        for (int i = 0; i < 5; ++i)
            bytes = bytes && emu->mmu().read(static_cast<uint16_t>(0x8100 + i)) == i + 1;
        return hdr && dat && bytes;
    };
    // The real-time path: EAR edges the player produces in the first 20000
    // T-states. The $12 tone after the block under test gives ten.
    auto realtime_edges = [&](const std::string& path) {
        TzxLoader t;
        if (!t.load(path)) return -1;
        t.start_playback(0);
        uint8_t last = t.update(0);
        int edges = 0;
        for (uint64_t c = 100; c <= 20000; c += 100) {
            const uint8_t l = t.update(c);
            edges += (l != last) ? 1 : 0;
            last = l;
        }
        return edges;
    };
    const Bytes tone = concat({{0x12}, le16(1000), le16(10)});
    for (const auto& r : rare) {
        const std::string n = r.name;
        if (r.id_ok) {
            const Loaded ok = load_bytes(n + "-ok.tzx", concat({kHeader, r.block, b10(data_body())}));
            check(r.id_ok, (n + ": accepted when its TZX-spec length fits the file (libspectrum "
                                "refuses it: UNKNOWN), and the walk lands on the next block").c_str(),
                  ok.ok && ok.is_loaded, detail(ok));
        }
        if (r.id_cut) {
            Bytes cut = concat({kHeader, r.block});
            cut.pop_back();
            const Loaded bad = load_bytes(n + "-cut.tzx", cut);
            check(r.id_cut, (n + ": one byte short at end of file is refused").c_str(),
                  !bad.ok && !bad.is_loaded, detail(bad));
        }
        const std::string fast = write_file(n + "-fast.tzx",
            concat({kHeader, b10(hdr_body()), r.block, b10(data_body())}));
        check(r.id_fast, (n + ": fast load skips it — the data block after it still loads "
                              "through the LD-BYTES trap").c_str(),
              fast_load_reaches_data(fast));
        const int edges = realtime_edges(write_file(n + "-real.tzx", concat({kHeader, r.block, tone})));
        check(r.id_real, (n + ": real-time playback skips it — the tone after it still plays").c_str(),
              edges >= 9, "edges=" + std::to_string(edges));
    }
    // TZXC-78 — a $16 whose "whole block" length is below the 4 bytes of the
    // length field itself is inconsistent, and refused. The fixture declares
    // 0 and is followed by exactly the bytes that WOULD walk cleanly if 0
    // were taken at face value (an empty $16, then an extension-rule block
    // of ID $00 made of the length field's own zeros), so only the "counts
    // itself" rule refuses it.
    {
        const Loaded l = load_bytes("16-short.tzx",
                                    concat({kHeader, b10(hdr_body()), {0x16}, le32(0), {0x00}}));
        check("TZXC-78", "a $16 block declaring a whole-block length of 0 (below its own 4 "
              "length bytes) is refused", !l.ok && !l.is_loaded, detail(l));
    }
    // TZXC-99 — a block the player skips without playing is reported when the
    // tape loads (a CSW recording's data is lost); a call sequence, whose
    // skipping loses nothing the player ever honoured, is not.
    {
        log_out.str("");
        TzxLoader t;
        const bool ok = t.load(write_file("warn.tzx",
            concat({kHeader, rare[2].block, rare[3].block, b10(data_body())})));
        const std::string log = log_out.str();
        check("TZXC-99", "loading a tape with a CSW block logs one warning naming it as "
              "skipped, and none for the call-sequence block",
              ok && count_occurrences(log, "does not play; it is skipped") == 1 &&
              log.find("block 0 (ID $18)") != std::string::npos, log);
    }

    // TZXC-68 — a new TZX replaces a WAV that was in (it already replaced a
    // TAP): a WAV left attached kept feeding the tape signal and the Tape
    // menu's Eject.
    {
        Bytes wav = {'R', 'I', 'F', 'F', 100, 0, 0, 0, 'W', 'A', 'V', 'E',
                     'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                     0x44, 0xAC, 0, 0, 0x44, 0xAC, 0, 0, 1, 0, 8, 0,
                     'd', 'a', 't', 'a', 64, 0, 0, 0};
        wav.resize(wav.size() + 64, 0x80);
        auto emu = std::make_unique<Emulator>();
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        const bool init_ok = emu->init(cfg);
        const bool wav_ok = init_ok && emu->load_wav(write_file("replace.wav", wav));
        const bool tzx_ok = emu->load_tzx(write_file("replace.tzx", valid));
        char buf[96];
        std::snprintf(buf, sizeof(buf), "init=%d wav=%d tzx=%d wav_loaded_after=%d",
                      init_ok ? 1 : 0, wav_ok ? 1 : 0, tzx_ok ? 1 : 0,
                      emu->wav_tape().is_loaded() ? 1 : 0);
        check("TZXC-68", "Emulator::load_tzx after a WAV ejects the WAV",
              wav_ok && tzx_ok && !emu->wav_tape().is_loaded() && emu->tzx_tape().is_loaded(),
              buf);
    }


    // ---- LD-BYTES fast-load trap (GH #395), TZX side ----------------------
    // Siblings of TAPC-13/17/16: the same shared helper (tape_trap.h) serves
    // both loaders, entry at 0x0562 included. Fixtures are our own bytes.
    {
        using namespace ldrig;
        auto body_of = [](bool good) {
            Bytes b = {0xFF, 1, 2, 3, 4, 5};
            uint8_t sum = 0;
            for (uint8_t v : b) sum ^= v;
            b.push_back(good ? sum : static_cast<uint8_t>(sum ^ 0x55));
            return b;
        };
        auto tzx_case = [&](const char* name, bool good) {
            Case c; c.tzx = true;
            c.path = write_file(name, concat({kHeader, b10(body_of(good))}));
            return c;
        };
        {
            Result f = run(tzx_case("t100.tzx", true), true, 20);
            check("TZXC-100", "fast load serves a caller entering LD-BYTES at 0x0562 from a TZX 0x10 block",
                  f.built && f.reached && carry(f) && f.mem[0] == 1 && f.mem[4] == 5,
                  "reached=" + std::to_string(f.reached));
        }
        {
            Result f = run(tzx_case("t101.tzx", false), true, 20);
            check("TZXC-101", "a TZX 0x10 block with a bad checksum: carry clear, payload written",
                  f.built && f.reached && !carry(f) && f.mem[4] == 5,
                  "reached=" + std::to_string(f.reached));
        }
        {
            const Bytes b = body_of(true);
            Bytes tap = le16(static_cast<uint32_t>(b.size()));
            tap.insert(tap.end(), b.begin(), b.end());
            Case tc; tc.path = write_file("t102.tap", tap);
            Result z = run(tzx_case("t102.tzx", true), true, 20);
            Result t = run(tc, true, 20);
            std::string why;
            check("TZXC-102", "the TZX trap leaves the same state as the TAP trap for the same bytes",
                  z.reached && t.reached && same_state(z, t, &why) && z.r.AF2 == t.r.AF2 &&
                  z.r.BC == t.r.BC && carry(z), why);
        }
    }

    // ---- Automatic tape play/stop (GH #395), TZX side ---------------------
    // ZOT holds at a gap boundary (after a block's non-zero pause) when the
    // host asks, never at a zero-pause join; stop blocks hold instead of
    // ending the tape; 0x2A stops only on a 48K machine (TZX spec, 0x2A).
    {
        using namespace ldrig;
        auto b10p = [](const Bytes& body, uint32_t pause) {
            return concat({{0x10}, le16(pause), le16(static_cast<uint32_t>(body.size())), body});
        };
        const Bytes dataA = tap_body(0xFF, {1, 2, 3, 4, 5});
        const Bytes dataB = tap_body(0xFF, {6, 7, 8, 9});
        auto delay_then_poll = [](uint16_t m) {
            return Bytes{0xF3, 0x11, uint8_t(m & 0xFF), uint8_t(m >> 8),
                         0x21, 0x00, 0x00, 0x2B, 0x7C, 0xB5, 0x20, 0xFB,
                         0x1B, 0x7A, 0xB3, 0x20, 0xF3,
                         0xED, 0x78, 0x18, 0xFC};
        };
        auto mk = [&](const std::string& path, bool fast, bool autop, MachineType mt, const Bytes& prog) {
            auto emu = std::make_unique<Emulator>();
            EmulatorConfig cfg; cfg.type = mt; cfg.rewind_buffer_frames = 0;
            cfg.tape_auto_play = autop;
            const bool ok = emu->init(cfg) && emu->load_tzx(path, fast);
            for (size_t i = 0; i < prog.size(); ++i) emu->mmu().write(uint16_t(0x8000 + i), prog[i]);
            auto r = emu->cpu().get_registers();
            r.PC = 0x8000; r.SP = 0x9000; r.BC = 0xFFFE; r.IFF1 = r.IFF2 = 0;
            emu->cpu().set_registers(r);
            return std::pair<std::unique_ptr<Emulator>, bool>(std::move(emu), ok);
        };

        {   // TZXC-103
            const std::string path = write_file("t103.tzx",
                concat({kHeader, b10p(dataA, 0), Bytes{0x20, 0, 0}, b10p(dataB, 1000)}));
            auto [on, ok1] = mk(path, false, true, MachineType::ZX48K, delay_then_poll(13));
            log_out.str("");
            int f = 0;
            for (; f < 300 && !on->tzx_tape().is_held(); ++f) on->run_frame();
            // The known answer the 128K rows of TZXC-104 measure their "no hold" against.
            const bool hold_logged = log_out.str().find("TZX: deck held before offset") != std::string::npos;
            const bool held = on->tzx_tape().is_held() && !on->tzx_tape().is_playing();
            for (int i = 0; i < 100; ++i) on->run_frame();
            const bool still = on->tzx_tape().is_held();
            for (int i = 0; i < 160; ++i) on->run_frame();            // the program polls from ~frame 316
            const bool resumed = on->tzx_tape().is_playing() && !on->tzx_tape().is_held();
            auto [off, ok2] = mk(path, false, false, MachineType::ZX48K, delay_then_poll(13));
            for (int i = 0; i < 520; ++i) off->run_frame();
            check("TZXC-103", "a stop block (0x20, pause 0): the deck stops there; auto play resumes it when the program polls, off leaves it stopped",
                  ok1 && ok2 && held && hold_logged && still && resumed && !off->tzx_tape().is_playing() && !off->tzx_tape().is_held(),
                  "f=" + std::to_string(f) + " held=" + std::to_string(held) + " still=" + std::to_string(still) +
                  " resumed=" + std::to_string(resumed));
        }
        {   // TZXC-104
            const std::string path = write_file("t104.tzx",
                concat({kHeader, b10p(dataA, 0), Bytes{0x2A, 0, 0, 0, 0}, b10p(dataB, 1000)}));
            auto [m48, ok1] = mk(path, false, true, MachineType::ZX48K, delay_then_poll(13));
            int f = 0;
            for (; f < 300 && !m48->tzx_tape().is_held(); ++f) m48->run_frame();
            const bool held48 = m48->tzx_tape().is_held();
            for (int i = 0; i < 160 + 160; ++i) m48->run_frame();
            const bool resumed48 = m48->tzx_tape().is_playing();
            auto [m128, ok2] = mk(path, false, true, MachineType::ZX128K, delay_then_poll(13));
            for (int i = 0; i < 150; ++i) m128->run_frame();           // past block A's end (~frame 100)
            // The same on a 128K whose program polls: the block is then parsed on a port read.
            // The block's parse lands in a port-read update or in the instruction-end one
            // depending on the loop's phase, so several phases (0..7 NOPs, 4 T each) run.
            bool p128_ok = true; bool ok3 = true;
            log_out.str("");
            for (int pad = 0; pad < 8; ++pad) {
                Bytes prog = {0xF3}; prog.insert(prog.end(), pad, 0x00);
                prog.insert(prog.end(), {0xED, 0x78, 0x18, 0xFC});
                auto [e, okk] = mk(path, false, true, MachineType::ZX128K, prog);
                for (int i = 0; i < 150; ++i) e->run_frame();
                ok3 = ok3 && okk;
                p128_ok = p128_ok && !e->tzx_tape().is_held() && e->tzx_tape().is_playing();
            }
            // A polling program resumes a hold at once, so the log is what shows one happened.
            p128_ok = p128_ok && log_out.str().find("TZX: deck held") == std::string::npos;
            check("TZXC-104", "0x2A stops the deck on a 48K machine (and auto play resumes it), not on a 128K (polling or not)",
                  ok1 && ok2 && ok3 && held48 && resumed48 && !m128->tzx_tape().is_held() && m128->tzx_tape().is_playing() &&
                  p128_ok,
                  "held48=" + std::to_string(held48) + " resumed48=" + std::to_string(resumed48));
        }
        {   // TZXC-105
            // 0x11 turbo (pause 0) joined to a 0x14 pure-data block: no gap, no hold.
            const Bytes turbo = concat({{0x11}, le16(2168), le16(667), le16(735), le16(855), le16(1710),
                                        le16(500), {8}, le16(0), le24(2), {0xAA, 0x55}});
            const Bytes pure = concat({{0x14}, le16(855), le16(1710), {8}, le16(1000), le24(2000), Bytes(2000, 0xAA)});
            const std::string path = write_file("t105.tzx", concat({kHeader, turbo, pure}));
            auto [emu, ok] = mk(path, false, true, MachineType::ZX48K, delay_then_poll(13));
            for (int i = 0; i < 100; ++i) emu->run_frame();           // well past the join (~frame 17)
            check("TZXC-105", "a zero-pause join between two blocks is not a gap: a non-polling program does not hold the deck there",
                  ok && emu->tzx_tape().is_playing() && !emu->tzx_tape().is_held());
        }
        {   // TZXC-106
            const Bytes hdrA = tap_body(0x00, Bytes(17, 0x20));
            const std::string path = write_file("t106.tzx", concat({kHeader, b10p(hdrA, 1000), b10p(dataB, 1000)}));
            auto [emu, ok] = mk(path, false, true, MachineType::ZX48K, delay_then_poll(20));
            int f = 0;
            for (; f < 600 && !emu->tzx_tape().is_held(); ++f) emu->run_frame();
            const bool held = ok && emu->tzx_tape().is_held();
            const uint64_t t_hold = emu->monotonic_tstates();
            std::vector<uint64_t> edges; uint64_t t_res = 0; bool resumed = false;
            while (emu->monotonic_tstates() < t_hold + 60000000ULL) {
                const bool was = emu->beeper().tape_ear();
                emu->execute_single_instruction();
                if (emu->beeper().tape_ear() != was) edges.push_back(emu->monotonic_tstates());
                if (!resumed && emu->tzx_tape().is_playing()) { resumed = true; t_res = emu->monotonic_tstates(); }
                if (resumed && emu->monotonic_tstates() > t_res + 7500000ULL) break;
            }
            size_t i0 = 0;
            while (i0 < edges.size() && edges[i0] <= t_res) ++i0;
            size_t pilot = 0;
            for (size_t i = i0; i + 1 < edges.size(); ++i) {
                const int64_t d = int64_t(edges[i + 1]) - int64_t(edges[i]);
                if (d >= 2168 - 40 && d <= 2168 + 40) ++pilot; else break;
            }
            const int64_t first = i0 < edges.size() ? int64_t(edges[i0]) - int64_t(t_res) : -1;
            check("TZXC-106", "after a hold the player continues at the NEXT block, from its first pulse (3223 pilot pulses of the data block, not block 0's 8063)",
                  held && resumed && pilot == 3222 && first > 0 && first <= 2168 + 60,
                  "pilot_intervals=" + std::to_string(pilot) + " first=" + std::to_string(first));
        }
        {   // TZXC-107
            // Fast mode with the defaults: a key-wait longer than the next block's
            // pilot (tight IN A,(C) loop), then a ROM load, still loads that block.
            const Bytes t1 = tap_body(0xFF, {1, 2, 3, 4, 5});
            const Bytes t2 = tap_body(0xFF, {6, 7, 8, 9});
            const std::string path = write_file("t107.tzx", concat({kHeader, b10p(t1, 1000), b10p(t2, 1000)}));
            Case c; c.tzx = true; c.path = path;
            Bytes prog = {0xF3, 0x01, 0xFE, 0xFF, 0x11, 0x05, 0x00, 0x21, 0x00, 0x00,
                          0xED, 0x78, 0x2B, 0x7C, 0xB5, 0x20, 0xF9, 0x1B, 0x7A, 0xB3, 0x20, 0xF1};
            const Bytes call = caller(c);
            prog.insert(prog.end(), call.begin(), call.end());
            prog.push_back(0x18); prog.push_back(0xFE);
            const uint16_t jr = uint16_t(0x8000 + prog.size() - 2);
            auto emu = std::make_unique<Emulator>();
            EmulatorConfig cfg; cfg.type = MachineType::ZX48K; cfg.rewind_buffer_frames = 0;
            cfg.sd_card_image = sd_image(); cfg.sd_card_readonly = true;
            const bool ok = emu->init(cfg) && emu->load_tzx(path, true);
            for (size_t i = 0; i < prog.size(); ++i) emu->mmu().write(uint16_t(0x8000 + i), prog[i]);
            auto r = emu->cpu().get_registers();
            r.PC = 0x8000; r.SP = 0x9000; r.IFF1 = r.IFF2 = 0;
            emu->cpu().set_registers(r);
            for (int f = 0; f < 400 && emu->cpu().pc() != jr; ++f) emu->run_frame();
            check("TZXC-107", "fast mode: a key-wait longer than the next block's pilot, then a ROM load, still loads that block (GH #395 review F1)",
                  ok && emu->cpu().pc() == jr && emu->mmu().peek(0xA000) == 1 &&
                  emu->mmu().peek(0xA004) == 5 && !emu->tzx_tape().is_playing(),
                  "pc=" + std::to_string(emu->cpu().pc()) + " a000=" + std::to_string(emu->mmu().peek(0xA000)));
        }
        {   // TZXC-108
            const std::string path = write_file("t108.tzx",
                concat({kHeader, b10p(dataA, 0), Bytes{0x20, 0, 0}, b10p(dataB, 1000)}));
            auto [emu, ok] = mk(path, false, true, MachineType::ZX48K, delay_then_poll(13));
            int f = 0;
            for (; f < 300 && !emu->tzx_tape().is_held(); ++f) emu->run_frame();
            const bool held = ok && emu->tzx_tape().is_held() && !emu->tzx_tape().is_playing();
            emu->set_tape_auto_play(false);
            check("TZXC-108", "turning auto play off while a TZX deck is held plays it again at once",
                  held && emu->tzx_tape().is_playing() && !emu->tzx_tape().is_held());
        }
        {   // TZXC-109
            const Bytes hdrA = tap_body(0x00, Bytes(17, 0x20));
            const std::string path = write_file("t109.tzx", concat({kHeader, b10p(hdrA, 1000), b10p(dataB, 1000)}));
            auto [emu, ok] = mk(path, false, true, MachineType::ZX48K, delay_then_poll(20));
            int f = 0;
            for (; f < 600 && !emu->tzx_tape().is_held(); ++f) emu->run_frame();
            const bool held = ok && emu->tzx_tape().is_held();
            emu->tzx_tape().rewind();                       // block B is next; rewound it is block A again
            const bool still = emu->tzx_tape().is_held();
            const uint64_t t0 = emu->monotonic_tstates();
            std::vector<uint64_t> edges; uint64_t t_res = 0; bool resumed = false;
            while (emu->monotonic_tstates() < t0 + 60000000ULL) {
                const bool was = emu->beeper().tape_ear();
                emu->execute_single_instruction();
                if (emu->beeper().tape_ear() != was) edges.push_back(emu->monotonic_tstates());
                if (!resumed && emu->tzx_tape().is_playing()) { resumed = true; t_res = emu->monotonic_tstates(); }
                if (resumed && emu->monotonic_tstates() > t_res + 18500000ULL) break;
            }
            size_t i0 = 0;
            while (i0 < edges.size() && edges[i0] <= t_res) ++i0;
            size_t pilot = 0;
            for (size_t i = i0; i + 1 < edges.size(); ++i) {
                const int64_t d = int64_t(edges[i + 1]) - int64_t(edges[i]);
                if (d >= 2168 - 40 && d <= 2168 + 40) ++pilot; else break;
            }
            check("TZXC-109", "a rewind while the TZX deck is held keeps it held, at block 0: it resumes with the header's 8063-pulse pilot",
                  held && still && resumed && pilot == 8062,
                  "pilot_intervals=" + std::to_string(pilot));
        }
        {   // TZXC-110 (GH #401)
            namespace fs = std::filesystem;
            write_file("t110.tzx", concat({kHeader, b10(hdr_body()), b10(data_body())}));
            const fs::path cwd0 = fs::current_path();
            fs::current_path(g_root);
            TzxLoader a;
            const bool ok = a.load("t110.tzx");            // a RELATIVE name
            fs::current_path(cwd0);
            const fs::path p = fs::u8path(a.path());
            const bool abs_ok = ok && p.is_absolute() &&
                                fs::equivalent(p, g_root / "t110.tzx") &&
                                a.filename() == "t110.tzx";
            const std::string want = a.path();
            TzxLoader b(std::move(a));
            const bool ctor_ok = b.path() == want;
            TzxLoader c;
            c = std::move(b);
            const bool assign_ok = c.path() == want;
            c.eject();
            check("TZXC-110", "path() is the absolute path of a relatively-loaded file (filename() stays the bare name), "
                  "survives a move construction and a move assignment, and eject() clears it",
                  abs_ok && ctor_ok && assign_ok && c.path().empty(),
                  "path=" + want);
        }
    }

    Log::emulator()->sinks().pop_back();
    if (!keep) std::filesystem::remove_all(g_root, ec);

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
