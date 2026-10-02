// Tape SAVE to TZX and WAV (GH #89; doc/design/TAPE-SAVE-PLAN.md).
//
// Oracles:
//   - the tape-out signal is the VHDL: o_AUDIO_MIC <= beep_mic_final
//     (zxnext.vhd:1638), beep_mic_final = i_AUDIO_EAR xor (port_fe_mic and
//     nr_08_keyboard_issue2) xor port_fe_mic (zxnext.vhd:6503), port_fe_mic =
//     port_fe_reg(3) (zxnext.vhd:3599), driven to the jack by
//     zxnext_top_issue2.vhd:1402-1408;
//   - the TZX bytes are the TZX 1.20 specification's block layouts, built by
//     hand in each row (never with the encoder under test);
//   - the WAV bytes are the RIFF/WAVE PCM header layout, by hand;
//   - pulse lengths are counted from the Z80 instruction T-states of the small
//     programs the integration rows run.
// Separately, the TZX and WAV files jnext writes were loaded in FUSE 1.6.0
// (libspectrum 1.5.0, tape traps off): that foreign check is in the GH #89
// report and the tape-save-mic-func regression row, not here.
//
// Run: ./build/test/tape_save_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/tape_recorder.h"
#include "platform/emulator_boot.h"

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int passed = 0;
int failed = 0;
int total = 0;

void check(const char* id, const char* desc, bool condition, const std::string& detail = {}) {
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
using TR = TapeRecorder;

std::filesystem::path g_root;

std::string tmp(const char* name) { return (g_root / name).string(); }

Bytes read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void write_file(const std::string& path, const Bytes& b) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

std::string hex(const Bytes& b, size_t max = 48) {
    std::string s;
    char buf[4];
    for (size_t i = 0; i < b.size() && i < max; ++i) {
        std::snprintf(buf, sizeof(buf), "%02x", b[i]);
        s += buf;
    }
    if (b.size() > max) s += "...";
    return s + " (" + std::to_string(b.size()) + " bytes)";
}

void cat(Bytes& out, const Bytes& b) { out.insert(out.end(), b.begin(), b.end()); }
void le16(Bytes& out, uint32_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); }

// Edge times (master cycles) for a pulse list in T-states, from `start`.
std::vector<uint64_t> edges_of(const std::vector<uint32_t>& pulses, uint64_t start = 1000) {
    std::vector<uint64_t> e{start};
    for (uint32_t p : pulses) e.push_back(e.back() + uint64_t(p) * 8);
    return e;
}

// pilot x n, sync1, sync2, then two pulses per bit (MSB first) of `bits` bits of `data`.
std::vector<uint32_t> block_pulses(uint32_t pilot, uint32_t n, uint32_t s1, uint32_t s2,
                                   uint32_t zero, uint32_t one, const Bytes& data, size_t bits) {
    std::vector<uint32_t> p(n, pilot);
    p.push_back(s1);
    p.push_back(s2);
    for (size_t b = 0; b < bits; ++b) {
        const uint32_t l = (data[b / 8] >> (7 - b % 8)) & 1 ? one : zero;
        p.push_back(l);
        p.push_back(l);
    }
    return p;
}

// Hand-built block 0x13 (pulses) / 0x20 (pause).
Bytes b13(const std::vector<uint32_t>& pulses) {
    Bytes v{0x13, uint8_t(pulses.size())};
    for (uint32_t p : pulses) le16(v, p);
    return v;
}
Bytes b20(uint32_t ms) { Bytes v{0x20}; le16(v, ms); return v; }

const Bytes TZX_HDR = {'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 0x01, 0x14};

// ── Emulator fixtures ────────────────────────────────────────────────────

std::unique_ptr<Emulator> next_machine() {
    auto emu = std::make_unique<Emulator>();
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu->init(cfg);
    return emu;
}

void poke(Emulator& emu, uint16_t at, const Bytes& code) {
    for (size_t i = 0; i < code.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(at + i), code[i]);
}

void start_at(Emulator& emu, uint16_t pc) {
    auto r = emu.cpu().get_registers();
    r.PC = pc;
    r.SP = 0x7FF0;
    r.IFF1 = r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

// The pulses (T-states) of every 0x13 block in a TZX image, and the block IDs.
struct TzxView {
    std::vector<uint8_t>  ids;
    std::vector<uint32_t> pulses;
    std::vector<Bytes>    rom_data;   // data of each 0x10
    std::vector<uint32_t> pauses;     // pause of each 0x10 / 0x20
};
TzxView view(const Bytes& f) {
    TzxView v;
    size_t i = 10;
    while (i < f.size()) {
        const uint8_t id = f[i];
        v.ids.push_back(id);
        if (id == 0x13) {
            const size_t n = f[i + 1];
            for (size_t k = 0; k < n; ++k) v.pulses.push_back(f[i + 2 + 2 * k] | f[i + 3 + 2 * k] << 8);
            i += 2 + 2 * n;
        } else if (id == 0x20) {
            v.pauses.push_back(f[i + 1] | f[i + 2] << 8);
            i += 3;
        } else if (id == 0x10) {
            v.pauses.push_back(f[i + 1] | f[i + 2] << 8);
            const size_t len = f[i + 3] | f[i + 4] << 8;
            v.rom_data.emplace_back(f.begin() + long(i + 5), f.begin() + long(i + 5 + len));
            i += 5 + len;
        } else if (id == 0x11) {
            const size_t len = f[i + 16] | f[i + 17] << 8 | f[i + 18] << 16;
            i += 19 + len;
        } else {
            break;
        }
    }
    return v;
}

// ── Rows ─────────────────────────────────────────────────────────────────

void encoder_rows() {
    {
        const bool ok = TR::format_for_path("a.tzx") == TR::Format::Tzx &&
                        TR::format_for_path("dir/A.TZX") == TR::Format::Tzx &&
                        TR::format_for_path("b.wav") == TR::Format::Wav &&
                        TR::format_for_path("B.Wav") == TR::Format::Wav &&
                        TR::format_for_path("c.tap") == TR::Format::None &&
                        TR::format_for_path("noext") == TR::Format::None &&
                        TR::format_for_path("d.tzx/file") == TR::Format::None &&
                        TR::format_for_path("e.tzxx") == TR::Format::None;
        check("TSAVE-01", "format by extension, case-insensitive: .tzx -> TZX, .wav -> WAV, "
              "anything else (.tap, none, a .tzx DIRECTORY) -> the TAP saver", ok);
    }
    check("TSAVE-02", "TZX header: \"ZXTape!\" 0x1A, version 1.20", TR::tzx_header() == TZX_HDR,
          hex(TR::tzx_header()));
    {
        const Bytes want = {0x10, 0xE8, 0x03, 0x03, 0x00, 0x00, 0x01, 0xFF};
        const Bytes got = TR::tzx_block_10(1000, {0x00, 0x01, 0xFF});
        check("TSAVE-03", "block 0x10 (TZX 1.20): ID, pause WORD, length WORD, data", got == want, hex(got));
    }
    {
        TR::TurboParams p;
        p.pilot = 0x0102; p.sync1 = 0x0304; p.sync2 = 0x0506; p.zero = 0x0708; p.one = 0x090A;
        p.pilot_count = 0x0B0C; p.used_bits = 5;
        const Bytes want = {0x11, 0x02, 0x01, 0x04, 0x03, 0x06, 0x05, 0x08, 0x07, 0x0A, 0x09,
                            0x0C, 0x0B, 0x05, 0x0E, 0x0D, 0x02, 0x00, 0x00, 0xAA, 0x55};
        const Bytes got = TR::tzx_block_11(p, 0x0D0E, {0xAA, 0x55});
        check("TSAVE-04", "block 0x11 (TZX 1.20): pilot, sync1, sync2, zero, one, pilot count "
              "WORDs, used bits BYTE, pause WORD, length 3 BYTEs, data", got == want, hex(got));
    }
    {
        const Bytes want13 = {0x13, 0x02, 0x2C, 0x01, 0xFF, 0xFF};
        const Bytes want20 = {0x20, 0x34, 0x12};
        const Bytes g13 = TR::tzx_block_13({300, 65535});
        const Bytes g20 = TR::tzx_block_20(0x1234);
        check("TSAVE-05", "block 0x13: ID, count BYTE, WORD per pulse; block 0x20: ID, pause WORD",
              g13 == want13 && g20 == want20, hex(g13) + " / " + hex(g20));
    }
    {
        const Bytes want = {'R', 'I', 'F', 'F', 0x2C, 0x01, 0x00, 0x00, 'W', 'A', 'V', 'E',
                            'f', 'm', 't', ' ', 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
                            0x44, 0xAC, 0x00, 0x00, 0x44, 0xAC, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00,
                            'd', 'a', 't', 'a', 0x08, 0x01, 0x00, 0x00};
        const Bytes got = TR::wav_header(264);
        check("TSAVE-06", "WAV header: RIFF size 36+data, fmt PCM mono 44100 Hz 8-bit "
              "(byte rate 44100, align 1), data size", got == want, hex(got, 44));
    }
    {
        const auto h = TR::standard_pulses({0x00, 0x80});
        const auto d = TR::standard_pulses({0xFF});
        bool ok = h.size() == 8063 + 2 + 32 && d.size() == 3223 + 2 + 16 &&
                  h[0] == 2168 && h[8062] == 2168 && h[8063] == 667 && h[8064] == 735 &&
                  h[8065] == 855 && h[8066] == 855 &&                       // 0x00: eight 0s
                  h[8065 + 16] == 1710 && h[8065 + 17] == 1710 &&           // 0x80: MSB 1
                  h[8065 + 18] == 855 && d[3225] == 1710 && d.back() == 1710;
        check("TSAVE-07", "standard pulse train (the ROM's): 8063 pilot pulses for a flag below "
              "0x80, 3223 otherwise, 2168 T; sync 667 + 735; two pulses per bit, 855 (0) or "
              "1710 (1), MSB first", ok,
              std::to_string(h.size()) + "/" + std::to_string(d.size()));
    }
}

void decoder_rows() {
    const uint64_t SEC = 28000000;
    {
        // The ROM's own stream: standard timings, then the extra edge SA/LD-RET
        // adds ~855 T after the last bit.
        const Bytes data = {0x00, 0x03, 'A', 'B', 0x7E};
        auto p = block_pulses(2168, 8063, 667, 735, 855, 1710, data, 40);
        p.push_back(860);
        const auto e = edges_of(p);
        Bytes want = {0x10, 0xE8, 0x03, 0x05, 0x00};
        cat(want, data);
        const Bytes got = TR::decode_segment(e, e.back() + SEC);
        check("TSAVE-10", "ROM-timed stream (8063 x 2168, 667/735, 855/1710, closing edge) -> "
              "exactly one block 0x10, data and 1000 ms pause byte-exact; the closing edge "
              "is not read as a bit", got == want, hex(got));
    }
    {
        const Bytes data = {0xA5, 0x3C};
        auto p = block_pulses(1900, 500, 500, 600, 700, 1600, data, 16);
        p.push_back(700);
        const auto e = edges_of(p);
        const Bytes want = {0x11, 0x6C, 0x07, 0xF4, 0x01, 0x58, 0x02, 0xBC, 0x02, 0x40, 0x06,
                            0xF4, 0x01, 0x08, 0xF4, 0x01, 0x02, 0x00, 0x00, 0xA5, 0x3C};
        const Bytes got = TR::decode_segment(e, e.back() + SEC / 2);
        check("TSAVE-11", "turbo stream (500 x 1900, 500/600, 700/1600) -> block 0x11 with the "
              "measured timings, exact pilot count, 8 used bits, 500 ms pause", got == want, hex(got));
    }
    {
        const Bytes data = {0xA5, 0x3C};
        const auto p = block_pulses(2168, 200, 667, 735, 855, 1710, data, 16);
        const auto e = edges_of(p);
        auto want_p = p;
        want_p.push_back(3500);
        Bytes want = b13(want_p);
        cat(want, b20(99));
        const Bytes got = TR::decode_segment(e, e.back() + SEC / 10);
        check("TSAVE-12", "a pilot of 200 pulses (under 256) is not a data block: every pulse "
              "goes to block 0x13 exactly, the open last edge as a 1 ms pulse, then 0x20 with "
              "the rest of the 100 ms gap", got == want, hex(got));
    }
    {
        const std::vector<uint32_t> p = {300, 1212, 497, 65535, 1, 2000};
        const auto e = edges_of(p);
        auto want_p = p;
        want_p.push_back(3500);
        Bytes want = b13(want_p);
        cat(want, b20(65535));
        const Bytes got = TR::decode_segment(e, e.back() + 100 * SEC);
        check("TSAVE-13", "pulses with no pilot -> block 0x13 with every duration exact "
              "(1 and 65535 T included); a 100 s gap caps the 0x20 pause at 65535 ms",
              got == want, hex(got));
    }
    {
        const std::vector<uint32_t> noise = {900, 450, 1300};
        const Bytes data = {0x5A};
        auto p = noise;
        const auto blk = block_pulses(1900, 300, 500, 600, 700, 1600, data, 8);
        p.insert(p.end(), blk.begin(), blk.end());
        const auto e = edges_of(p);
        Bytes want = b13(noise);
        cat(want, {0x11, 0x6C, 0x07, 0xF4, 0x01, 0x58, 0x02, 0xBC, 0x02, 0x40, 0x06,
                   0x2C, 0x01, 0x08, 0x14, 0x00, 0x01, 0x00, 0x00, 0x5A});
        const Bytes got = TR::decode_segment(e, e.back() + SEC / 50);
        check("TSAVE-14", "unrecognised pulses then a turbo block in one segment -> 0x13 "
              "(three closed pulses, the third ending at the pilot) then 0x11, 20 ms pause",
              got == want, hex(got));
    }
    {
        const Bytes data = {0xC3, 0x90};
        auto p = block_pulses(1900, 300, 500, 600, 700, 1600, data, 12);
        p.push_back(700);
        const auto e = edges_of(p);
        const Bytes want = {0x11, 0x6C, 0x07, 0xF4, 0x01, 0x58, 0x02, 0xBC, 0x02, 0x40, 0x06,
                            0x2C, 0x01, 0x04, 0x0A, 0x00, 0x02, 0x00, 0x00, 0xC3, 0x90};
        const Bytes got = TR::decode_segment(e, e.back() + SEC / 100);
        check("TSAVE-15", "12 bits -> 0x11 with 4 used bits in the last byte, the bits high "
              "(TZX: xxxx0000)", got == want, hex(got));
    }
    {
        // 16 bits, the last bit's second half never closed: the stream ends at
        // the edge that starts it, so the bit is read from its first half.
        const Bytes data = {0x0F, 0x81};
        auto p = block_pulses(1900, 300, 500, 600, 700, 1600, data, 16);
        p.pop_back();
        const auto e = edges_of(p);
        const Bytes want = {0x11, 0x6C, 0x07, 0xF4, 0x01, 0x58, 0x02, 0xBC, 0x02, 0x40, 0x06,
                            0x2C, 0x01, 0x08, 0x0A, 0x00, 0x02, 0x00, 0x00, 0x0F, 0x81};
        const Bytes got = TR::decode_segment(e, e.back() + SEC / 100);
        check("TSAVE-16", "a last bit with no second half is read from its first half when it "
              "completes a byte", got == want, hex(got));
    }
    {
        const Bytes data = {0x00, 0x11};
        auto p = block_pulses(2168, 6000, 667, 735, 855, 1710, data, 16);
        p.push_back(855);
        const auto e = edges_of(p);
        const Bytes want = {0x11, 0x78, 0x08, 0x9B, 0x02, 0xDF, 0x02, 0x57, 0x03, 0xAE, 0x06,
                            0x70, 0x17, 0x08, 0xE8, 0x03, 0x02, 0x00, 0x00, 0x00, 0x11};
        const Bytes got = TR::decode_segment(e, e.back() + SEC);
        check("TSAVE-17", "the ROM's timings but 6000 pilot pulses for a header (the ROM sends "
              "8063) -> 0x11, not 0x10: block 0x10 would replay a different pilot", got == want,
              hex(got));
    }
    {
        // Contention-like jitter of a few T-states is still the ROM's block.
        const Bytes data = {0xFF, 0x42};
        auto p = block_pulses(2168, 3223, 667, 735, 855, 1710, data, 16);
        for (size_t i = 0; i < p.size(); ++i) p[i] += (i % 3) * 3;
        p.push_back(855);
        const auto e = edges_of(p);
        const Bytes got = TR::decode_segment(e, e.back() + SEC);
        Bytes want = {0x10, 0xE8, 0x03, 0x02, 0x00, 0xFF, 0x42};
        check("TSAVE-18", "pulses jittered by 0-6 T-states (port 0xFE contention) still decode "
              "to the ROM's 0x10", got == want, hex(got));
    }
    {
        // The ROM's timings, but 12 bits: block 0x10 has no used-bits field.
        const Bytes data = {0xFF, 0xA0};
        auto p = block_pulses(2168, 3223, 667, 735, 855, 1710, data, 12);
        p.push_back(855);
        const auto e = edges_of(p);
        const Bytes want = {0x11, 0x78, 0x08, 0x9B, 0x02, 0xDF, 0x02, 0x57, 0x03, 0xAE, 0x06,
                            0x97, 0x0C, 0x04, 0xE8, 0x03, 0x02, 0x00, 0x00, 0xFF, 0xA0};
        const Bytes got = TR::decode_segment(e, e.back() + SEC);
        check("TSAVE-19", "the ROM's timings with 12 bits -> 0x11 (4 used bits): block 0x10 can "
              "only replay whole bytes", got == want, hex(got));
    }
    {
        // 11% slow: pilot 2400, sync 740/815, bits 950/1900 — a saver a little
        // off the ROM's timings, beyond the 1/16 that counts as the ROM's.
        const Bytes data = {0x00, 0x5A};
        auto p = block_pulses(2400, 8063, 740, 815, 950, 1900, data, 16);
        p.push_back(950);
        const auto e = edges_of(p);
        const Bytes want = {0x11, 0x60, 0x09, 0xE4, 0x02, 0x2F, 0x03, 0xB6, 0x03, 0x6C, 0x07,
                            0x7F, 0x1F, 0x08, 0xE8, 0x03, 0x02, 0x00, 0x00, 0x00, 0x5A};
        const Bytes got = TR::decode_segment(e, e.back() + SEC);
        check("TSAVE-29", "timings 11% off the ROM's (pilot 2400, sync 740/815, bits 950/1900) "
              "are not the ROM's: 0x11 with the measured values, not 0x10", got == want, hex(got));
    }
}

void recorder_rows() {
    const uint64_t MS = 28000;
    {
        const std::string path = tmp("seg.tzx");
        std::filesystem::remove(path);
        TapeRecorder r;
        std::string why;
        const bool opened = r.open(path, why);
        r.sample(true, 0);                        // baseline: not an edge
        r.sample(true, 500);                      // same level: not an edge
        r.sample(false, 1000);
        r.sample(true, 1000 + 100 * 8);           // pulse 100 T
        const uint64_t t2 = 1000 + 800 + 20 * MS; // 20 ms > 65535 T: new segment
        r.sample(false, t2);
        r.sample(true, t2 + 400 * 8);
        const uint64_t edges = r.edges_recorded();
        r.close(t2 + 3200 + 2000 * MS);
        Bytes want = TZX_HDR;
        cat(want, b13({100, 3500}));
        cat(want, b20(19));
        cat(want, b13({400, 3500}));
        cat(want, b20(1999));
        const Bytes got = read_file(path);
        check("TSAVE-20", "recorder: the first sample is a baseline, an unchanged level is no "
              "edge, a gap over 65535 T starts a new segment; file = header + 0x13/0x20 per "
              "segment with the measured gaps", opened && edges == 4 && got == want,
              hex(got) + " edges=" + std::to_string(edges));
    }
    {
        const std::string path = tmp("order.tzx");
        std::filesystem::remove(path);
        TapeRecorder r;
        std::string why;
        r.open(path, why);
        r.sample(false, 0);
        r.sample(true, 1000);
        r.sample(false, 1000 + 200 * 8);
        const uint64_t trap = 1000 + 1600 + 5 * MS;
        const bool rom_ok = r.rom_block({0xFF, 0x01, 0xFE}, trap);
        r.sample(true, trap + 1000 * MS);                 // 1 s after the trap
        r.close(trap + 1000 * MS + 50 * MS);
        Bytes want = TZX_HDR;
        cat(want, b13({200, 3500}));
        cat(want, b20(4));
        cat(want, {0x10, 0xE8, 0x03, 0x03, 0x00, 0xFF, 0x01, 0xFE});
        cat(want, b13({3500}));
        cat(want, b20(49));
        const Bytes got = read_file(path);
        check("TSAVE-21", "a trapped ROM block ends the open segment first (time order kept), "
              "becomes 0x10 with the exact bytes, and its pause is the gap to the next event",
              rom_ok && got == want && r.rom_blocks() == 1, hex(got));
    }
    {
        const std::string path = tmp("back.tzx");
        std::filesystem::remove(path);
        TapeRecorder r;
        std::string why;
        r.open(path, why);
        r.sample(false, 1000000000);
        r.sample(true, 1000000000 + 800);
        r.sample(false, 5);                               // the clock went back
        r.close(5 + 10 * MS);
        Bytes want = TZX_HDR;
        cat(want, b13({3500}));
        cat(want, b20(999));
        cat(want, b13({3500}));
        cat(want, b20(9));
        const Bytes got = read_file(path);
        check("TSAVE-22", "a clock that goes backwards (rewind, reset) counts as a 1 s gap: "
              "no negative or zero pulse", got == want, hex(got));
    }
    {
        const std::string path = tmp("app.tzx");
        std::filesystem::remove(path);
        std::string why;
        {
            TapeRecorder r;
            r.open(path, why);
            r.rom_block({0x00, 0x00}, 0);
            r.close(0);
        }
        bool reopened = false;
        {
            TapeRecorder r;
            reopened = r.open(path, why);
            r.rom_block({0xFF, 0xFF}, 0);
            r.close(0);
        }
        Bytes want = TZX_HDR;
        cat(want, {0x10, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00});
        cat(want, {0x10, 0x00, 0x00, 0x02, 0x00, 0xFF, 0xFF});
        const Bytes got = read_file(path);
        const std::string junk = tmp("junk.tzx");
        write_file(junk, {'h', 'e', 'l', 'l', 'o', '!', '!', '!', '!'});
        TapeRecorder r2;
        std::string why2;
        const bool junk_open = r2.open(junk, why2);
        const Bytes junk_after = read_file(junk);
        check("TSAVE-23", "an existing TZX is appended to (one header, both sessions' blocks); "
              "an existing non-TZX file is refused with a reason and left untouched",
              reopened && got == want && !junk_open && !why2.empty() && !r2.active() &&
                  junk_after.size() == 9,
              hex(got) + " junk_open=" + std::to_string(junk_open) + " why=" + why2);
    }
    {
        const std::string path = tmp("edges.wav");
        std::filesystem::remove(path);
        TapeRecorder r;
        std::string why;
        r.open(path, why);
        r.sample(false, 0);
        r.sample(true, 10 * MS);                 // first event: nothing before it
        r.sample(false, 15 * MS);                // 5 ms high
        r.close(25 * MS);                        // 10 ms low
        const Bytes got = read_file(path);
        // 5 ms at 44100 Hz = 220.5 -> sample 220; 15 ms total = 661.5 -> 661.
        bool ok = got.size() == 44 + 661 && got[4] == uint8_t(36 + 661) && got[5] == 0x02 &&
                  got[40] == uint8_t(661 & 0xFF) && got[41] == 0x02;
        for (size_t i = 0; ok && i < 661; ++i)
            ok = got[44 + i] == (i < 220 ? 0xC0 : 0x40);
        check("TSAVE-24", "WAV: nothing before the first edge; each edge toggles the level at "
              "sample floor(t * 44100 / 28 MHz) (high 0xC0, low 0x40); stopping holds the "
              "level to the stop and patches the RIFF and data sizes", ok,
              "size=" + std::to_string(got.size()));
    }
    {
        const std::string path = tmp("rom.wav");
        std::filesystem::remove(path);
        TapeRecorder r;
        std::string why;
        r.open(path, why);
        r.rom_block({0x00}, 0);
        r.close(0);
        const Bytes got = read_file(path);
        // 8063*2168 + 667 + 735 + 16*855 = 17495666 T = 139965328 master
        // -> 139965328 * 44100 / 28e6 = 220445.39 samples.
        std::vector<size_t> runs;
        size_t transitions = 0;
        if (got.size() > 44) {
            size_t run = 1;
            for (size_t i = 45; i < got.size(); ++i) {
                if (got[i] != got[i - 1]) { runs.push_back(run); run = 1; ++transitions; }
                else ++run;
            }
        }
        const bool ok = got.size() == 44 + 220445 && got[44] == 0xC0 && runs.size() >= 5 &&
                        runs[0] == 27 && runs[1] == 27 && runs[2] == 27 && runs[3] == 28 &&
                        runs[4] == 27 && transitions == 8063 + 2 + 16 - 1;
        check("TSAVE-25", "WAV: a trapped ROM block is synthesised as the ROM's pulse train "
              "(2168 T = 27.3 samples per pilot pulse: runs 27,27,27,28,27; 8081 pulses; "
              "220445 samples)", ok,
              "size=" + std::to_string(got.size()) + " transitions=" + std::to_string(transitions));
    }
    {
        const std::string path = tmp("app.wav");
        std::filesystem::remove(path);
        std::string why;
        {
            TapeRecorder r;
            r.open(path, why);
            r.sample(false, 0);
            r.sample(true, 0);                    // high from sample 0
            r.close(MS);                          // 44.1 -> 44 samples high
        }
        bool reopened = false;
        {
            TapeRecorder r;
            reopened = r.open(path, why);         // continues HIGH
            r.sample(false, 0);
            r.sample(true, 0);                    // high -> low edge here
            r.close(MS);
        }
        const Bytes got = read_file(path);
        bool ok = reopened && got.size() == 44 + 88 && got[40] == 88 && got[4] == 36 + 88;
        for (size_t i = 0; ok && i < 88; ++i) ok = got[44 + i] == (i < 44 ? 0xC0 : 0x40);
        const std::string foreign = tmp("foreign.wav");
        Bytes h16 = TR::wav_header(4);
        h16[34] = 16;                             // 16-bit samples
        h16.insert(h16.end(), {0, 0, 0, 0});
        write_file(foreign, h16);
        TapeRecorder r3;
        std::string why3;
        const bool f_open = r3.open(foreign, why3);
        check("TSAVE-26", "an existing jnext WAV is appended to, its level carried on from the "
              "last sample (the first edge after it toggles it); a 16-bit WAV is refused",
              ok && !f_open && !why3.empty(), "size=" + std::to_string(got.size()));
    }
    {
        const std::string path = tmp("cap.wav");
        std::filesystem::remove(path);
        TapeRecorder r;
        std::string why;
        r.open(path, why);
        r.sample(false, 0);
        r.sample(true, 0);
        r.sample(false, 100 * 28000000ULL);      // 100 s later
        r.close(100 * 28000000ULL);
        const Bytes got = read_file(path);
        // 65.535 s -> 1834980000 master -> 2890093.5 samples
        check("TSAVE-27", "WAV: a gap longer than 65.535 s is written as 65.535 s (the TZX pause "
              "cap)", got.size() == 44 + 2890093, "size=" + std::to_string(got.size()));
    }
}

void emulator_rows() {
    {
        // beep_mic_final = i_AUDIO_EAR xor (port_fe_mic and issue2) xor port_fe_mic.
        auto emu = next_machine();
        const std::string tap = tmp("echo.tap");
        write_file(tap, {0x04, 0x00, 0xFF, 0x01, 0x02, 0xFC});
        const bool loaded = emu->load_tap(tap, false);
        int bad = 0;
        std::string detail;
        for (int playing = 0; playing < 2; ++playing)
            for (int issue2 = 0; issue2 < 2; ++issue2)
                for (int mic = 0; mic < 2; ++mic)
                    for (int ear = 0; ear < 2; ++ear) {
                        if (playing) emu->tape().start_realtime_playback();
                        else emu->tape().eject();
                        emu->nextreg().write(0x08, static_cast<uint8_t>(0x10 | issue2));
                        emu->port().write(0x00FE, static_cast<uint8_t>(mic ? 0x08 : 0x00));
                        emu->beeper().set_tape_ear(ear != 0);
                        const int ear_in = playing ? ear : (issue2 & mic);
                        const bool want = (ear_in ^ (issue2 & mic) ^ mic) != 0;
                        if (emu->tape_out_level() != want || emu->tape().is_playing() != (playing != 0)) {
                            ++bad;
                            detail += " p" + std::to_string(playing) + "i" + std::to_string(issue2) +
                                      "m" + std::to_string(mic) + "e" + std::to_string(ear);
                        }
                        if (!playing) emu->load_tap(tap, false);
                    }
        check("TSAVE-30", "tape-out = i_AUDIO_EAR xor (port_fe_mic and nr_08 issue2) xor "
              "port_fe_mic (zxnext.vhd:6503, :3599, :1638): with no tape it is the MIC bit in both "
              "keyboard modes; a tape playing in real time is echoed to it",
              loaded && bad == 0, detail);
    }
    // A program toggling MIC every 162 T-states:
    //   loop: OUT ($FE),A (11) / LD B,10 (7) / DJNZ $ (9*13+8) / XOR 8 (7) / JR loop (12)
    const Bytes toggler = {0x3E, 0x08, 0xD3, 0xFE, 0x06, 0x0A, 0x10, 0xFE, 0xEE, 0x08, 0x18, 0xF6};
    {
        auto emu = next_machine();
        const std::string path = tmp("toggle.tzx");
        std::filesystem::remove(path);
        const bool armed = emu->start_tape_save(path);
        poke(*emu, 0x8000, toggler);
        start_at(*emu, 0x8000);
        emu->run_frame();
        emu->run_frame();
        const uint64_t edges = emu->tape_recorder().edges_recorded();
        emu->stop_tape_save();
        const TzxView v = view(read_file(path));
        size_t odd = 0;
        for (size_t i = 0; i + 1 < v.pulses.size(); ++i) if (v.pulses[i] != 162) ++odd;
        check("TSAVE-31", "run_frame: OUT ($FE) MIC toggles every 162 T at 3.5 MHz are captured "
              "as edges and written as 162 T pulses (0x13: equal pulses but no sync, so no "
              "data block)", armed && edges > 400 && v.pulses.size() + 1 >= edges && odd == 0 &&
              !v.ids.empty() && v.ids[0] == 0x13,
              "edges=" + std::to_string(edges) + " pulses=" + std::to_string(v.pulses.size()) +
                  " odd=" + std::to_string(odd));
    }
    {
        auto emu = next_machine();
        const std::string path = tmp("toggle7.tzx");
        std::filesystem::remove(path);
        emu->start_tape_save(path);
        emu->nextreg().write(0x07, 0x01);           // 7 MHz
        poke(*emu, 0x8000, toggler);
        start_at(*emu, 0x8000);
        emu->run_frame();
        emu->run_frame();
        emu->stop_tape_save();
        const TzxView v = view(read_file(path));
        size_t n81 = 0;
        for (uint32_t p : v.pulses) if (p == 81) ++n81;
        check("TSAVE-32", "the time base is the master clock, not CPU T-states: the same loop "
              "at 7 MHz (NR 0x07 = 1) writes 81 T pulses, half the 3.5 MHz length",
              n81 > 400 && n81 + 2 >= v.pulses.size(),
              "n81=" + std::to_string(n81) + " pulses=" + std::to_string(v.pulses.size()));
    }
    // A program: MIC up, 3345 T later MIC down, 3378 T later CALL $04C2 (the trap).
    //   DI / LD A,8 / OUT ($FE),A (11) / LD B,0 (7) / DJNZ $ (255*13+8) / XOR A (4) /
    //   OUT ($FE),A (11) / LD B,0 (7) / DJNZ $ (3323) / LD A,$FF (7) / LD IX,$9000 (14) /
    //   LD DE,5 (10) / CALL $04C2 (17) / JR $ at $801C
    const Bytes saver = {0xF3, 0x3E, 0x08, 0xD3, 0xFE, 0x06, 0x00, 0x10, 0xFE, 0xAF, 0xD3, 0xFE,
                         0x06, 0x00, 0x10, 0xFE,
                         0x3E, 0xFF, 0xDD, 0x21, 0x00, 0x90, 0x11, 0x05, 0x00, 0xCD, 0xC2, 0x04,
                         0x18, 0xFE};
    const Bytes payload = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};
    auto with_rom = [&](Emulator& emu) {
        std::vector<uint8_t> rom(0x2000, 0x00);
        rom[0x04C2] = 0x21; rom[0x04C3] = 0x3F; rom[0x04C4] = 0x05; rom[0x04C5] = 0xE5;
        emu.mmu().set_boot_rom(rom.data(), rom.size());
        emu.mmu().set_boot_rom_enabled(true);
        poke(emu, 0x8000, saver);
        poke(emu, 0x9000, payload);
        start_at(emu, 0x8000);
    };
    {
        auto emu = next_machine();
        const std::string path = tmp("coexist.tzx");
        std::filesystem::remove(path);
        emu->start_tape_save(path);
        with_rom(*emu);
        emu->run_frame();
        const uint64_t edges = emu->tape_recorder().edges_recorded();
        const size_t roms = emu->tape_recorder().rom_blocks();
        const uint16_t pc = emu->cpu().get_registers().PC;
        emu->stop_tape_save();
        const Bytes f = read_file(path);
        const TzxView v = view(f);
        // checksum FF^DE^AD^BE^EF^01 = DC
        const Bytes want_data = {0xFF, 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0xDC};
        const bool ok = edges == 2 && roms == 1 && pc == 0x801C &&
                        v.ids == std::vector<uint8_t>{0x13, 0x20, 0x10} &&
                        v.pulses == std::vector<uint32_t>{3345, 3500} &&
                        v.rom_data.size() == 1 && v.rom_data[0] == want_data &&
                        v.pauses.size() == 2 && v.pauses[0] == 1 && v.pauses[1] <= 21;
        check("TSAVE-33", "trap and capture together: the MIC pulse before the CALL is written "
              "(3345 T, exact) and then the trapped block as 0x10 with its bytes; the trap "
              "skips SA-BYTES, so no edge of the save itself is captured (2 edges, 1 block)",
              ok, hex(f, 64) + " edges=" + std::to_string(edges) + " pc=" + std::to_string(pc));
    }
    {
        auto emu = next_machine();
        const std::string path = tmp("coexist.wav");
        std::filesystem::remove(path);
        emu->start_tape_save(path);
        with_rom(*emu);
        emu->run_frame();
        emu->stop_tape_save();
        const Bytes f = read_file(path);
        size_t transitions = 0;
        for (size_t i = 45; i < f.size(); ++i) if (f[i] != f[i - 1]) ++transitions;
        // The MIC pulse ends (1), each of the ROM block's 3223 + 2 + 7*16
        // pulses starts with a toggle, and the closing toggle is held to the stop.
        check("TSAVE-34", "WAV: the same program writes the MIC pulse then the trapped block's "
              "pulse train (1 + 3223 + 2 + 112 + 1 transitions)",
              transitions == 1 + 3223 + 2 + 112 + 1,
              "transitions=" + std::to_string(transitions) + " size=" + std::to_string(f.size()));
    }
    {
        auto emu = next_machine();
        const std::string path = tmp("replay.tzx");
        std::filesystem::remove(path);
        emu->start_tape_save(path);
        with_rom(*emu);
        emu->set_replay_mode(true);
        emu->run_frame();
        emu->set_replay_mode(false);
        const uint64_t edges = emu->tape_recorder().edges_recorded();
        const size_t roms = emu->tape_recorder().rom_blocks();
        const auto regs = emu->cpu().get_registers();
        emu->stop_tape_save();
        const Bytes f = read_file(path);
        check("TSAVE-35", "while a rewind replays frames nothing reaches the tape (no edge, no "
              "block) but the trap still takes the ROM's exit, so the replay runs the same path",
              edges == 0 && roms == 0 && f == TZX_HDR && regs.PC == 0x801C && regs.DE == 0 &&
                  (regs.AF & 1) != 0,
              "edges=" + std::to_string(edges) + " roms=" + std::to_string(roms) + " " + hex(f));
    }
    {
        auto emu = next_machine();
        const std::string path = tmp("rzx.wav");
        emu->start_tape_save(path);
        const bool refused = emu->rzx_refused_by_tape_save("record");
        emu->stop_tape_save();
        const bool after = emu->rzx_refused_by_tape_save("record");
        check("TSAVE-36", "a TZX/WAV save arms the SA-BYTES trap too, so RZX stays refused while "
              "it runs, and is allowed again once it stops", refused && !after);
    }
    {
        auto emu = next_machine();
        const std::string a = tmp("reinit_a.tzx"), b = tmp("reinit_b.tzx");
        std::filesystem::remove(a);
        std::filesystem::remove(b);
        emu->start_tape_save(a);
        emu->port().write(0x00FE, 0x00);
        emu->tape_recorder().sample(emu->tape_out_level(), 100);
        emu->port().write(0x00FE, 0x08);
        emu->tape_recorder().sample(emu->tape_out_level(), 200);
        EmulatorConfig cfg = emu->config();
        emu->init(cfg);                                   // same file: kept open
        const uint64_t kept = emu->tape_recorder().edges_recorded();
        cfg = emu->config();
        cfg.tape_save_file = b;
        emu->init(cfg);                                   // another file: a finished
        const bool a_done = read_file(a).size() > TZX_HDR.size();
        const bool on_b = emu->tape_recorder().active() && emu->tape_recorder().path() == b;
        cfg = emu->config();
        cfg.tape_save_file.clear();
        emu->init(cfg);                                   // none: stopped
        const bool off = !emu->tape_save_active();
        emu->start_tape_save(a);
        emu->stop_tape_save();
        const bool cleared = emu->config().tape_save_file.empty() && !emu->tape_save_active();
        check("TSAVE-37", "re-init() with the same tape_save_file keeps the save open (edges "
              "kept); another file finishes the first and opens it; none stops; "
              "stop_tape_save() clears config().tape_save_file",
              kept == 1 && a_done && on_b && off && cleared,
              "kept=" + std::to_string(kept));
    }
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);                                    // started without --tape-save
        const std::string path = tmp("coldboot.tzx");
        std::filesystem::remove(path);
        emu.start_tape_save(path);                        // as the Tape menu does
        emulator_cold_boot(emu, cfg);
        const bool kept = emu.tape_recorder().active() && emu.tape_recorder().path() == path;
        emu.stop_tape_save();
        emulator_cold_boot(emu, emu.config());
        const bool still_off = !emu.tape_save_active();
        check("TSAVE-38", "a power-on reset (emulator_cold_boot) keeps a save started from the "
              "menu, and does not restart one that was stopped", kept && still_off);
    }
    {
        auto emu = next_machine();
        const std::string path = tmp("refuse.wav");
        write_file(path, {'n', 'o', 't', ' ', 'a', ' ', 'w', 'a', 'v'});
        const bool ok = emu->start_tape_save(path);
        const Bytes after = read_file(path);
        check("TSAVE-39", "start_tape_save on a file it cannot use refuses, saves nothing, and "
              "leaves the file and config().tape_save_file untouched",
              !ok && !emu->tape_save_active() && after.size() == 9 &&
                  emu->config().tape_save_file.empty());
    }
    {
        // A .tap save stopped: the trap no longer appends, RZX is allowed again.
        auto emu = next_machine();
        const std::string path = tmp("stopped.tap");
        std::filesystem::remove(path);
        const bool armed = emu->start_tape_save(path) && emu->tap_saver().active();
        emu->stop_tape_save();
        with_rom(*emu);
        emu->run_frame();
        const Bytes f = read_file(path);
        check("TSAVE-40", "stopping a .tap save disarms the TAP saver: a SAVE after it appends "
              "nothing, and RZX is no longer refused",
              armed && !emu->tape_save_active() && f.empty() &&
                  !emu->rzx_refused_by_tape_save("record"),
              "size=" + std::to_string(f.size()));
    }
    // ── GH #89 review round 1 ──────────────────────────────────────────────
    {
        // B2/R7: the TAP half of the rewind fix — a replayed frame re-runs the
        // trap, and the block already reached the file when the frame first ran.
        auto emu = next_machine();
        const std::string path = tmp("replay.tap");
        std::filesystem::remove(path);
        emu->start_tape_save(path);
        with_rom(*emu);
        emu->set_replay_mode(true);
        emu->run_frame();
        emu->set_replay_mode(false);
        const auto regs = emu->cpu().get_registers();
        const Bytes f = read_file(path);
        check("TSAVE-41", "a .tap save: a rewind replaying the frame of a SAVE does not append the "
              "block again, and the trap still takes the ROM's exit",
              f.empty() && emu->tap_saver().blocks_written() == 0 && regs.PC == 0x801C &&
                  regs.DE == 0 && (regs.AF & 1) != 0,
              "size=" + std::to_string(f.size()));
    }
    {
        // B2/R8: the refusal the other way round — saving cannot start while an
        // RZX records. (RZX recording is refused on a Next, so a 48K.)
        auto emu = std::make_unique<Emulator>();
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        cfg.rewind_buffer_frames = 0;
        emu->init(cfg);
        const std::string rzx = tmp("refuse.rzx");
        const std::string path = tmp("refuse-rzx.tzx");
        std::filesystem::remove(path);
        const bool recording = emu->start_rzx_recording(rzx);
        const bool started = emu->start_tape_save(path);
        const bool active = emu->tape_save_active();
        const bool created = std::filesystem::exists(path);
        emu->stop_rzx_recording();
        check("TSAVE-42", "start_tape_save refuses while an RZX records: nothing armed, no file "
              "created, config().tape_save_file empty",
              recording && !started && !active && !created &&
                  emu->config().tape_save_file.empty(),
              "recording=" + std::to_string(recording) + " started=" + std::to_string(started));
    }
    {
        // B2/R25: the echo for a TZX and a WAV playing in real time, not only a TAP.
        auto emu = next_machine();
        Bytes tzx = TZX_HDR;
        Bytes blk = {0x10, 0xE8, 0x03, 0x04, 0x00, 0xFF, 0x01, 0x02, 0xFC};
        cat(tzx, blk);
        const std::string tzx_path = tmp("echo.tzx");
        write_file(tzx_path, tzx);
        Bytes wav = TR::wav_header(44100);
        for (uint32_t i = 0; i < 44100; ++i) wav.push_back((i / 20) % 2 ? 0xC0 : 0x40);
        const std::string wav_path = tmp("echo.wav");
        write_file(wav_path, wav);
        int bad = 0;
        std::string detail;
        for (int which = 0; which < 2; ++which) {
            const bool loaded = which == 0 ? emu->load_tzx(tzx_path, false) : emu->load_wav(wav_path);
            if (which == 0) emu->tzx_tape().start_playback(emu->monotonic_tstates());
            const bool playing = which == 0 ? emu->tzx_tape().is_playing() : emu->wav_tape().is_playing();
            if (!loaded || !playing) { ++bad; detail += which ? " wav-not-playing" : " tzx-not-playing"; }
            emu->nextreg().write(0x08, 0x10);                       // issue 2 off
            for (int mic = 0; mic < 2; ++mic)
                for (int ear = 0; ear < 2; ++ear) {
                    emu->port().write(0x00FE, static_cast<uint8_t>(mic ? 0x08 : 0x00));
                    emu->beeper().set_tape_ear(ear != 0);
                    if (emu->tape_out_level() != ((ear ^ mic) != 0)) {
                        ++bad;
                        detail += std::string(which ? " wav" : " tzx") + "m" + std::to_string(mic) +
                                  "e" + std::to_string(ear);
                    }
                }
        }
        check("TSAVE-43", "a TZX or WAV playing in real time is echoed to tape-out too "
              "(i_AUDIO_EAR xor port_fe_mic, zxnext.vhd:6503)", bad == 0, detail);
    }
    {
        // The echo reaches the FILE: with MIC still, a tape playing in real time
        // gives edges, for each of the three players (each samples in its own
        // branch of the per-instruction tape tick).
        const Bytes idle = {0xF3, 0x18, 0xFE};                 // DI / JR $
        Bytes tap = {0x13, 0x00, 0x00, 3, 'T', 'E', 'S', 'T', ' ', ' ', ' ', ' ', ' ', ' ',
                     5, 0, 0x00, 0x80, 0x00, 0x80, 0x00};
        uint8_t sum = 0;
        for (size_t i = 2; i + 1 < tap.size(); ++i) sum ^= tap[i];
        tap.back() = sum;
        const std::string tap_path = tmp("play.tap");
        write_file(tap_path, tap);
        uint64_t got[3] = {0, 0, 0};
        for (int which = 0; which < 3; ++which) {
            auto emu = next_machine();
            const std::string out = tmp(which == 0 ? "echo0.tzx" : which == 1 ? "echo1.tzx" : "echo2.tzx");
            std::filesystem::remove(out);
            emu->start_tape_save(out);
            poke(*emu, 0x8000, idle);
            start_at(*emu, 0x8000);
            if (which == 0) { emu->load_tap(tap_path, false); emu->tape().start_realtime_playback(); }
            if (which == 1) { emu->load_tzx(tmp("echo.tzx"), false);
                              emu->tzx_tape().start_playback(emu->monotonic_tstates()); }
            if (which == 2) emu->load_wav(tmp("echo.wav"));
            for (int f = 0; f < 3; ++f) emu->run_frame();
            got[which] = emu->tape_recorder().edges_recorded();
            emu->stop_tape_save();
        }
        check("TSAVE-44", "with MIC still, a TAP, a TZX and a WAV playing in real time each put "
              "their edges on the saved tape (3 frames: more than 20 each)",
              got[0] > 20 && got[1] > 20 && got[2] > 20,
              "tap=" + std::to_string(got[0]) + " tzx=" + std::to_string(got[1]) +
                  " wav=" + std::to_string(got[2]));
    }
    {
        // B3: what a killed run leaves. The machine runs one frame with the
        // trapped SAVE in it and is never stopped; the file is read as it is.
        auto emu = next_machine();
        const std::string path = tmp("killed.tzx");
        std::filesystem::remove(path);
        emu->start_tape_save(path);
        with_rom(*emu);
        emu->run_frame();
        const TzxView v = view(read_file(path));
        const Bytes want_data = {0xFF, 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0xDC};
        const bool on_disk = v.rom_data.size() == 1 && v.rom_data[0] == want_data;
        emu->stop_tape_save();
        check("TSAVE-48", "a run killed after the frame of a trapped SAVE (never stopped) leaves "
              "the block on disk: written at once, its pause patched later",
              on_disk, hex(read_file(path), 64));
    }
    {
        // B4: the warm-start recording machine owns no host output.
        EmulatorConfig live;
        live.type = MachineType::ZXN_ISSUE2;
        live.sd_card_image = "card.img";
        live.load_file = "x.nex";
        live.tape_save_file = "save.wav";
        live.compositor_trace_path = "trace.txt";
        live.magic_port_enabled = true;
        live.joy_uart_rx_file = "rx.bin";
        live.joy_uart_fifo = "fifo";
        live.joy_uart_pty = true;
        const EmulatorConfig b = Emulator::warm_start_boot_config(live);
        check("TSAVE-49", "the warm-start recording boot gets no tape save (nor compositor trace, "
              "magic port, joystick-port cable): one writer per file; the machine itself is kept",
              b.tape_save_file.empty() && b.compositor_trace_path.empty() && !b.magic_port_enabled &&
                  b.joy_uart_rx_file.empty() && b.joy_uart_fifo.empty() && !b.joy_uart_pty &&
                  b.load_file.empty() && b.sd_card_image == "card.img" &&
                  b.type == MachineType::ZXN_ISSUE2);
    }
    {
        // B3 through the machine: a program's own MIC pulses, then silence. Two
        // frames later (40 ms > 65535 T) the frame-end poll has written them,
        // though the save was never stopped.
        //   DI / LD A,8 / OUT ($FE),A / XOR A / OUT ($FE),A / JR $
        auto emu = next_machine();
        const std::string path = tmp("killed-mic.tzx");
        std::filesystem::remove(path);
        emu->start_tape_save(path);
        poke(*emu, 0x8000, {0xF3, 0x3E, 0x08, 0xD3, 0xFE, 0xAF, 0xD3, 0xFE, 0x18, 0xFE});
        start_at(*emu, 0x8000);
        emu->run_frame();
        emu->run_frame();
        const TzxView v = view(read_file(path));
        // OUT ($FE),A to OUT ($FE),A: XOR A (4) + OUT (11) = 15 T.
        const bool on_disk = v.ids == std::vector<uint8_t>{0x13, 0x20} &&
                             v.pulses == std::vector<uint32_t>{15, 3500};
        emu->stop_tape_save();
        check("TSAVE-54", "a program's MIC pulses followed by silence reach the disk at the end of "
              "the frame that sees the silence, with the save still running (a killed run keeps "
              "them)", on_disk, hex(read_file(path), 64));
    }
    {
        // Round 2 / R2-B1: the DMA drives the bus (zxnext.vhd:1829, :1834), so its
        // writes to port 0xFE latch port_fe_reg one by one. A memory -> port 0xFE
        // transfer of 64 bytes alternating MIC: each write is timed at its place
        // in the burst (2 T per byte, what a burst is charged), so 63 pulses of
        // exactly 2 T and no two edges at one time.
        auto emu = next_machine();
        const std::string path = tmp("dma.tzx");
        std::filesystem::remove(path);
        emu->start_tape_save(path);
        Bytes src(64);
        for (size_t i = 0; i < src.size(); ++i) src[i] = i % 2 ? 0x00 : 0x08;
        poke(*emu, 0x9000, src);
        poke(*emu, 0x8000, {0xF3, 0x18, 0xFE});                  // DI / JR $
        start_at(*emu, 0x8000);
        // R0 A->B, port A 0x9000, length 64; R1 memory, increment; R2 I/O, fixed;
        // R4 continuous, port B 0x00FE; LOAD; ENABLE (dma_test.cpp's sequence).
        for (uint8_t b : {0x7D, 0x00, 0x90, 0x40, 0x00, 0x14, 0x28, 0xAD, 0xFE, 0x00, 0xCF, 0x87})
            emu->port().write(0x006B, b);
        emu->run_frame();
        const uint64_t edges = emu->tape_recorder().edges_recorded();
        emu->stop_tape_save();
        const TzxView v = view(read_file(path));
        size_t two = 0;
        for (size_t i = 0; i + 1 < v.pulses.size(); ++i) if (v.pulses[i] == 2) ++two;
        check("TSAVE-55", "a DMA burst writing port 0xFE times each transfer at its place in the "
              "burst: 64 alternating MIC bytes give 64 edges and 63 pulses of exactly 2 T",
              edges == 64 && v.pulses.size() == 64 && two == 63 && v.pulses.back() == 3500,
              "edges=" + std::to_string(edges) + " pulses=" + std::to_string(v.pulses.size()) +
                  " two=" + std::to_string(two));
    }
    {
        // The same transfer at 28 MHz: 2 master cycles per byte, under half a
        // T-state. Every pulse would round to 0 T; the glitch pairs cancel, and
        // no 0 T pulse reaches the file.
        auto emu = next_machine();
        const std::string path = tmp("dma28.tzx");
        std::filesystem::remove(path);
        emu->start_tape_save(path);
        emu->nextreg().write(0x07, 0x03);
        Bytes src(64);
        for (size_t i = 0; i < src.size(); ++i) src[i] = i % 2 ? 0x00 : 0x08;
        poke(*emu, 0x9000, src);
        poke(*emu, 0x8000, {0xF3, 0x18, 0xFE});
        start_at(*emu, 0x8000);
        emu->run_frame();                                     // settle the speed change
        for (uint8_t b : {0x7D, 0x00, 0x90, 0x40, 0x00, 0x14, 0x28, 0xAD, 0xFE, 0x00, 0xCF, 0x87})
            emu->port().write(0x006B, b);
        emu->run_frame();
        const uint64_t edges = emu->tape_recorder().edges_recorded();
        emu->stop_tape_save();
        const TzxView v = view(read_file(path));
        size_t zero = 0;
        for (uint32_t p : v.pulses) if (p == 0) ++zero;
        check("TSAVE-56", "at 28 MHz the DMA's 64 MIC writes (2 master cycles apart) are all "
              "captured, and the file holds no 0 T pulse: sub-half-T glitch pairs cancel",
              edges == 64 && zero == 0 && v.pulses.empty(),
              "edges=" + std::to_string(edges) + " pulses=" + std::to_string(v.pulses.size()) +
                  " zero=" + std::to_string(zero));
    }
}

void round1_rows() {
    const uint64_t MS = 28000;
    const uint64_t SEC = 28000000;
    {
        // B3: a trapped block is on disk at once, pause 0, and the pause WORD
        // is patched in place when the next event comes.
        const std::string path = tmp("kill-rom.tzx");
        std::filesystem::remove(path);
        TapeRecorder r;
        std::string why;
        r.open(path, why);
        r.rom_block({0xFF, 0x01, 0xFE}, 1000);
        const Bytes now1 = read_file(path);                 // never closed
        r.sample(false, 2000);
        r.sample(true, 1000 + 250 * MS);                    // 250 ms after the trap
        r.poll(1000 + 250 * MS);
        const Bytes now2 = read_file(path);
        Bytes want1 = TZX_HDR;
        cat(want1, {0x10, 0x00, 0x00, 0x03, 0x00, 0xFF, 0x01, 0xFE});
        Bytes want2 = TZX_HDR;
        cat(want2, {0x10, 0xFA, 0x00, 0x03, 0x00, 0xFF, 0x01, 0xFE});
        check("TSAVE-45", "TZX: a trapped block is written at once with pause 0 (a killed run "
              "keeps it), and its pause is patched in place to the real gap (250 ms)",
              now1 == want1 && now2 == want2, hex(now1) + " / " + hex(now2));
        r.close(1000 + 300 * MS);
    }
    {
        // B3: a MIC segment is written by poll() once it has been silent for
        // more than 65535 T, its last pause provisional, then patched.
        const std::string path = tmp("kill-seg.tzx");
        std::filesystem::remove(path);
        TapeRecorder r;
        std::string why;
        r.open(path, why);
        r.sample(false, 0);
        r.sample(true, 1000);
        r.sample(false, 1000 + 300 * 8);
        r.poll(1000 + 2400 + 10 * MS);                      // 10 ms: not yet a gap
        const Bytes early = read_file(path);
        r.poll(1000 + 2400 + 30 * MS);                      // 30 ms: written
        const Bytes written = read_file(path);
        r.sample(true, 1000 + 2400 + 500 * MS);             // the next event, 500 ms on
        r.poll(1000 + 2400 + 500 * MS);
        const Bytes patched = read_file(path);
        Bytes w1 = TZX_HDR;
        cat(w1, b13({300, 3500}));
        cat(w1, b20(29));
        Bytes w2 = TZX_HDR;
        cat(w2, b13({300, 3500}));
        cat(w2, b20(499));
        check("TSAVE-46", "TZX: poll() writes a segment once it is silent past 65535 T (not before), "
              "its 0x20 provisional, and patches it to the real gap when the next edge comes",
              early == TZX_HDR && written == w1 && patched == w2,
              hex(early) + " / " + hex(written) + " / " + hex(patched));
        r.close(1000 + 2400 + 600 * MS);
    }
    {
        // B3: a WAV's RIFF/data sizes are refreshed by poll() and after a trapped
        // block, so a killed run leaves a header that matches its data.
        const std::string path = tmp("kill.wav");
        std::filesystem::remove(path);
        TapeRecorder r;
        std::string why;
        r.open(path, why);
        r.sample(false, 0);
        r.sample(true, 0);
        r.sample(false, 10 * MS);                          // 441 samples high
        r.poll(10 * MS);
        const Bytes a = read_file(path);
        r.rom_block({0x00}, 20 * MS);
        const Bytes b = read_file(path);
        auto data_of = [](const Bytes& f) -> uint32_t {
            return f.size() < 44 ? 0 : f[40] | f[41] << 8 | f[42] << 16 | uint32_t(f[43]) << 24;
        };
        auto riff_of = [](const Bytes& f) -> uint32_t {
            return f.size() < 44 ? 0 : f[4] | f[5] << 8 | f[6] << 16 | uint32_t(f[7]) << 24;
        };
        check("TSAVE-47", "WAV: the header's sizes match the data on disk after poll() and after a "
              "trapped block, with the file still open",
              a.size() == 44 + 441 && data_of(a) == 441 && riff_of(a) == 36 + 441 &&
                  b.size() > a.size() && data_of(b) == b.size() - 44 && riff_of(b) == b.size() - 8,
              "a=" + std::to_string(a.size()) + "/" + std::to_string(data_of(a)) +
                  " b=" + std::to_string(b.size()) + "/" + std::to_string(data_of(b)));
        r.close(30 * MS);
    }
    {
        // N1: inside the ROM-timing tolerance (1/16) is still the ROM's block.
        const Bytes data = {0xFF, 0x5A};
        auto p = block_pulses(2268, 3223, 690, 760, 885, 1770, data, 16);
        p.push_back(885);
        const auto e = edges_of(p);
        Bytes want = {0x10, 0xE8, 0x03, 0x02, 0x00, 0xFF, 0x5A};
        const Bytes got = TR::decode_segment(e, e.back() + SEC);
        check("TSAVE-50", "timings 3.5-4.6% off the ROM's (pilot 2268, sync 690/760, bits "
              "885/1770: inside 1/16) are still the ROM's block, 0x10", got == want, hex(got));
    }
    {
        // N1: the pilot count, both sides of 1%.
        const Bytes data = {0x00, 0x5A};
        auto p8000 = block_pulses(2168, 8000, 667, 735, 855, 1710, data, 16);
        p8000.push_back(855);
        auto p7900 = block_pulses(2168, 7900, 667, 735, 855, 1710, data, 16);
        p7900.push_back(855);
        const auto e1 = edges_of(p8000), e2 = edges_of(p7900);
        const Bytes g1 = TR::decode_segment(e1, e1.back() + SEC);
        const Bytes g2 = TR::decode_segment(e2, e2.back() + SEC);
        Bytes w1 = {0x10, 0xE8, 0x03, 0x02, 0x00, 0x00, 0x5A};
        Bytes w2 = {0x11, 0x78, 0x08, 0x9B, 0x02, 0xDF, 0x02, 0x57, 0x03, 0xAE, 0x06,
                    0xDC, 0x1E, 0x08, 0xE8, 0x03, 0x02, 0x00, 0x00, 0x00, 0x5A};
        check("TSAVE-51", "a header pilot of 8000 pulses (0.8% under 8063) is still 0x10; 7900 "
              "(2% under) is 0x11 with the count kept", g1 == w1 && g2 == w2, hex(g1) + " / " + hex(g2));
    }
    {
        // N1: pulse equality is 1/8 — a pilot whose pulses alternate 5.5% each
        // side of 2170 is one pilot.
        const Bytes data = {0xFF, 0x5A};
        auto p = block_pulses(2168, 3223, 667, 735, 855, 1710, data, 16);
        for (size_t i = 0; i < 3223; ++i) p[i] = i % 2 ? 2290 : 2050;
        p.push_back(855);
        const auto e = edges_of(p);
        Bytes want = {0x10, 0xE8, 0x03, 0x02, 0x00, 0xFF, 0x5A};
        const Bytes got = TR::decode_segment(e, e.back() + SEC);
        check("TSAVE-52", "pilot pulses alternating 2050/2290 T (within 1/8 of each other) are one "
              "pilot: the ROM's 0x10", got == want, hex(got));
    }
    {
        // N1/R17: a WAV appended to continues at the first position whose
        // sample index is the existing size (ceil), not one sample earlier.
        const std::string path = tmp("round.wav");
        std::filesystem::remove(path);
        std::string why;
        {
            TapeRecorder r;
            r.open(path, why);
            r.sample(false, 0);
            r.sample(true, 0);
            r.close(MS);                                    // 44 samples
        }
        {
            TapeRecorder r;
            r.open(path, why);
            r.sample(false, 0);
            r.sample(true, 0);
            r.close(27937);   // 44 * 28e6/44100 = 27936.5 -> 27937; + 27937 = sample 88.0006
        }
        const Bytes got = read_file(path);
        check("TSAVE-53", "WAV append: the tape position resumes at ceil(size * 28 MHz / 44100), so "
              "a 27937-cycle hold after 44 samples ends at sample 88", got.size() == 44 + 88,
              "size=" + std::to_string(got.size()));
    }
    {
        // Round 2 / R2-B2: a block poll() wrote gets its pause patched in its own
        // field — 0x11 at offset 14 (not the used-bits byte), 0x10 at offset 1.
        auto run = [&](const std::vector<uint32_t>& p, const std::string& name,
                       Bytes& provisional, Bytes& patched) {
            const std::string path = tmp(name.c_str());
            std::filesystem::remove(path);
            TapeRecorder r;
            std::string why;
            r.open(path, why);
            const auto e = edges_of(p);
            bool level = false;
            r.sample(level, 0);
            for (uint64_t t : e) { level = !level; r.sample(level, t); }
            r.poll(e.back() + 30 * MS);                    // silent 30 ms: written, pause 30
            provisional = read_file(path);
            level = !level;
            r.sample(level, e.back() + 700 * MS);          // the next event, 700 ms on
            r.poll(e.back() + 700 * MS);
            patched = read_file(path);
            r.close(e.back() + 710 * MS);
        };
        auto turbo = block_pulses(1900, 500, 500, 600, 700, 1600, {0xA5, 0x3C}, 16);
        turbo.push_back(700);
        auto rom = block_pulses(2168, 3223, 667, 735, 855, 1710, {0xFF, 0x42}, 16);
        rom.push_back(855);
        Bytes t_prov, t_pat, r_prov, r_pat;
        run(turbo, "patch11.tzx", t_prov, t_pat);
        run(rom, "patch10.tzx", r_prov, r_pat);
        auto b11 = [](uint8_t lo, uint8_t hi) {
            Bytes v = TZX_HDR;
            cat(v, {0x11, 0x6C, 0x07, 0xF4, 0x01, 0x58, 0x02, 0xBC, 0x02, 0x40, 0x06, 0xF4, 0x01,
                    0x08, lo, hi, 0x02, 0x00, 0x00, 0xA5, 0x3C});
            return v;
        };
        auto b10 = [](uint8_t lo, uint8_t hi) {
            Bytes v = TZX_HDR;
            cat(v, {0x10, lo, hi, 0x02, 0x00, 0xFF, 0x42});
            return v;
        };
        check("TSAVE-57", "a 0x11 and a 0x10 written by poll() carry a provisional pause (30 ms), "
              "patched in their own pause WORD to the real gap (700 ms) when the next edge comes; "
              "the 0x11's used-bits byte stays 8",
              t_prov == b11(0x1E, 0x00) && t_pat == b11(0xBC, 0x02) &&
                  r_prov == b10(0x1E, 0x00) && r_pat == b10(0xBC, 0x02),
              hex(t_pat, 64) + " / " + hex(r_pat, 64));
    }
    {
        // Round 2: no 0 T pulse — two edges under half a T-state apart cancel.
        const uint64_t t0 = 1000, t1 = t0 + 800 * 8, t2 = t1 + 2, t3 = t2 + 800 * 8;
        const Bytes got = TR::decode_segment({t0, t1, t2, t3}, t3 + 100 * MS);
        Bytes want = b13({1600, 3500});
        cat(want, b20(99));
        check("TSAVE-58", "a glitch (two edges 2 master cycles apart) is dropped: the pulses each "
              "side join into one (800 + 800 T), never a 0 T pulse", got == want, hex(got));
    }
    {
        // Round 2: a trapped block's pause, patched after the clock went back
        // (a rewind, a reset), counts the 1 s backwards gap, not 0.
        const std::string path = tmp("patch-back.tzx");
        std::filesystem::remove(path);
        TapeRecorder r;
        std::string why;
        r.open(path, why);
        r.rom_block({0xFF, 0x01, 0xFE}, 1000000000);
        r.sample(false, 5);
        r.sample(true, 10);                                  // after the clock went back
        r.poll(10);
        const Bytes got = read_file(path);
        Bytes want = TZX_HDR;
        cat(want, {0x10, 0xE8, 0x03, 0x03, 0x00, 0xFF, 0x01, 0xFE});
        check("TSAVE-59", "a pause patched after the clock went backwards is the 1 s gap (1000 ms), "
              "not 0", got == want, hex(got));
        r.close(20);
    }
    {
        // Round 2: a change no write makes — the machine reset with MIC high
        // (port_fe_reg cleared, zxnext.vhd:3590-3591) — is recorded at the end
        // of that frame.
        auto emu = next_machine();
        const std::string path = tmp("reset.tzx");
        std::filesystem::remove(path);
        emu->start_tape_save(path);
        poke(*emu, 0x8000, {0xF3, 0x3E, 0x08, 0xD3, 0xFE, 0x18, 0xFE});   // MIC up, JR $
        start_at(*emu, 0x8000);
        emu->run_frame();
        const uint64_t before = emu->tape_recorder().edges_recorded();
        emu->init(emu->config(), true);                     // soft reset: MIC low
        start_at(*emu, 0x8005);                             // JR $: no port 0xFE write
        emu->run_frame();
        const uint64_t after = emu->tape_recorder().edges_recorded();
        emu->stop_tape_save();
        check("TSAVE-60", "a reset that clears MIC is an edge on the saved tape, recorded at the "
              "end of the next frame (1 edge before, 2 after)", before == 1 && after == 2,
              "before=" + std::to_string(before) + " after=" + std::to_string(after));
    }
}

} // namespace

int main() {
    std::error_code ec;
    g_root = std::filesystem::temp_directory_path() /
             ("jnext-tape-save-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(g_root, ec);
    std::printf("Tape SAVE to TZX / WAV (GH #89)\n");
    encoder_rows();
    decoder_rows();
    recorder_rows();
    emulator_rows();
    round1_rows();

    std::filesystem::remove_all(g_root, ec);

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
