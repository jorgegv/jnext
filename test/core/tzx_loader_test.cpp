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
//     the TZX spec's own $16-$18, $26, $27, $34 and $40
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

namespace {

int passed = 0;
int failed = 0;
int total = 0;

void check(const char* id, const char* desc, bool condition,
           const std::string& detail = {}) {
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

    // TZXC-60..66 — block IDs libspectrum does not implement are refused,
    // even the TZX spec's own (each carries a well-formed body here).
    struct Unsupported { const char* id; const char* name; Bytes block; };
    const Unsupported unsupported[] = {
        {"TZXC-60", "16-c64rom", concat({{0x16}, le32(4), {0, 0, 0, 0}})},
        {"TZXC-61", "17-c64turbo", concat({{0x17}, le32(4), {0, 0, 0, 0}})},
        {"TZXC-62", "18-csw", concat({{0x18}, le32(4), {0, 0, 0, 0}})},
        {"TZXC-63", "26-call", concat({{0x26}, le16(1), le16(1)})},
        {"TZXC-64", "27-return", {0x27}},
        {"TZXC-65", "34-emuinfo", concat({{0x34}, Bytes(8, 0)})},
        {"TZXC-66", "40-snapshot", concat({{0x40, 0x00}, le24(3), {1, 2, 3}})},
        {"TZXC-67", "5b-unknown", concat({{0x5B}, le32(3), {1, 2, 3}})},
    };
    for (const auto& u : unsupported) {
        const std::string n = u.name;
        const Loaded l = load_bytes(n + ".tzx", concat({kHeader, b10(hdr_body()), u.block}));
        check(u.id, (n + ": a block ID libspectrum does not implement is refused "
                         "(libspectrum UNKNOWN)").c_str(),
              !l.ok && !l.is_loaded, detail(l));
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

    Log::emulator()->sinks().pop_back();
    if (!keep) std::filesystem::remove_all(g_root, ec);

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
