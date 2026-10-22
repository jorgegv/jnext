// TAP container validation tests.
//
// No VHDL oracle: a .tap file is a host-side container the FPGA core never
// sees. Its format is a sequence of blocks, each a 2-byte little-endian length
// followed by that many bytes (flag, payload, XOR checksum). The oracle for
// what counts as MALFORMED is libspectrum's TAP reader — the library FUSE loads
// tapes with — run as a live library (libspectrum 1.5.0) over the same byte
// patterns these rows build:
//
//   - a block whose declared length runs past the end of the file
//                                    -> LIBSPECTRUM_ERROR_CORRUPT, tape discarded
//   - a 1-byte fragment where a length field should start
//                                    -> LIBSPECTRUM_ERROR_CORRUPT, tape discarded
//   - an empty file                  -> no tape (libspectrum_tape_present() == 0)
//   - a zero-length block ("00 00")  -> accepted
//   - a bad checksum in a complete block -> accepted: that is a tape-loading
//     error the ROM reports ("R Tape loading error") when it reads the block,
//     not a container error.
//
// Run: ./build/test/tap_loader_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "core/tap_loader.h"
#include "ld_trap_rig.h"

#include <spdlog/sinks/ostream_sink.h>

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

// One TAP block: [len lo][len hi][flag][payload...][checksum]. `good=false`
// corrupts the checksum while keeping the block structurally complete.
Bytes tap_block(uint8_t flag, const Bytes& payload, bool good = true) {
    uint8_t sum = flag;
    for (uint8_t b : payload) sum ^= b;
    if (!good) sum ^= 0x55;
    const uint16_t len = static_cast<uint16_t>(payload.size() + 2);
    Bytes out = {static_cast<uint8_t>(len & 0xFF), static_cast<uint8_t>(len >> 8), flag};
    out.insert(out.end(), payload.begin(), payload.end());
    out.push_back(sum);
    return out;
}

// A standard 17-byte CODE header ("TEST", 5 bytes at 0x8000) + its data block.
Bytes header_block(bool good = true) {
    Bytes h = {3, 'T', 'E', 'S', 'T', ' ', ' ', ' ', ' ', ' ', ' ',
               5, 0, 0x00, 0x80, 0x00, 0x80};
    return tap_block(0x00, h, good);
}
Bytes data_block() { return tap_block(0xFF, {1, 2, 3, 4, 5}); }

Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

std::filesystem::path g_root;

std::string write_file(const char* name, const Bytes& bytes) {
    const auto path = g_root / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

// Loads `bytes` through the real TapLoader::load(); returns its verdict.
struct Loaded { bool ok; size_t blocks; bool is_loaded; };
Loaded load_bytes(const char* name, const Bytes& bytes, TapLoader& loader) {
    const bool ok = loader.load(write_file(name, bytes));
    return {ok, loader.block_count(), loader.is_loaded()};
}

std::string detail(const Loaded& l) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "load=%d blocks=%zu is_loaded=%d",
                  l.ok ? 1 : 0, l.blocks, l.is_loaded ? 1 : 0);
    return buf;
}

} // namespace

int main() {
    std::printf("TAP container validation tests\n");
    std::printf("===============================================\n\n");

    g_root = std::filesystem::temp_directory_path() /
             ("jnext-tap-loader-test-" + std::to_string(static_cast<long>(::getpid())));
    std::error_code ec;
    std::filesystem::remove_all(g_root, ec);
    std::filesystem::create_directories(g_root, ec);

    std::ostringstream log_out;
    auto log_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(log_out);
    log_sink->set_pattern("%l %v");
    Log::emulator()->sinks().push_back(log_sink);

    // TAPC-01 — the positive control every rejection is measured against.
    {
        TapLoader t;
        const Loaded l = load_bytes("valid.tap", concat({header_block(), data_block()}), t);
        const TapBlock* b0 = t.peek_block();
        check("TAPC-01", "a well-formed two-block TAP loads, both blocks intact",
              l.ok && l.blocks == 2 && l.is_loaded && b0 && b0->flag == 0x00 &&
              b0->data.size() == 17 && b0->verify_checksum(),
              detail(l));
    }

    // TAPC-02 — a truncated tape: a complete header, then a data block cut
    // short. libspectrum rejects it and keeps none of the earlier blocks.
    {
        Bytes bytes = concat({header_block(), data_block()});
        bytes.resize(bytes.size() - 3);
        TapLoader t;
        const Loaded l = load_bytes("truncated.tap", bytes, t);
        check("TAPC-02", "a block running past end of file rejects the whole tape "
              "(libspectrum CORRUPT)",
              !l.ok && l.blocks == 0 && !l.is_loaded, detail(l));
    }

    // TAPC-03 — the first length field already overruns (the shape a random
    // file almost always has).
    {
        Bytes bytes(12, 0xAA);
        bytes[0] = 100;
        bytes[1] = 0;
        TapLoader t;
        const Loaded l = load_bytes("overrun.tap", bytes, t);
        check("TAPC-03", "first block declares 100 bytes, 10 follow: rejected",
              !l.ok && l.blocks == 0 && !l.is_loaded, detail(l));
    }

    // TAPC-04 — complete blocks followed by one stray byte.
    {
        TapLoader t;
        const Loaded l = load_bytes("dangling.tap",
                                    concat({header_block(), data_block(), {0x07}}), t);
        check("TAPC-04", "a 1-byte fragment after the last block is rejected "
              "(libspectrum CORRUPT)",
              !l.ok && l.blocks == 0 && !l.is_loaded, detail(l));
    }

    // TAPC-05 — no bytes at all: libspectrum reports no tape present.
    {
        TapLoader t;
        const Loaded l = load_bytes("empty.tap", {}, t);
        check("TAPC-05", "an empty file is rejected (libspectrum: no tape present)",
              !l.ok && !l.is_loaded, detail(l));
    }

    // TAPC-06 — structurally complete, but the header's checksum is wrong.
    {
        log_out.str("");
        TapLoader t;
        const Loaded l = load_bytes("badsum.tap", concat({header_block(false), data_block()}), t);
        const TapBlock* b0 = t.peek_block();
        const std::string log = log_out.str();
        check("TAPC-06", "a bad checksum in a complete block still loads, with a "
              "warning (a ROM tape-loading error, not a container error)",
              l.ok && l.blocks == 2 && b0 && !b0->verify_checksum() &&
              log.find("warning TAP: checksum mismatch") != std::string::npos &&
              log.find("error ") == std::string::npos,
              detail(l) + " log=" + log);
    }

    // TAPC-07 — a zero-length block is accepted by libspectrum, so it is not a
    // container error here either. (libspectrum keeps it as an empty third
    // block; jnext drops it with a warning, so only ">= 2" is the oracle's.)
    {
        TapLoader t;
        const Loaded l = load_bytes("zero-block.tap",
                                    concat({Bytes{0, 0}, header_block(), data_block()}), t);
        check("TAPC-07", "a zero-length block is not a container error",
              l.ok && l.is_loaded && l.blocks >= 2, detail(l));
    }

    // TAPC-08 — the rejection is an error log line naming the file and the
    // reason, so a user (and the frontends' "failed to load" exit) has
    // something to go on.
    {
        log_out.str("");
        Bytes bytes = concat({header_block(), data_block()});
        bytes.resize(bytes.size() - 3);
        TapLoader t;
        const bool ok = t.load(write_file("reason.tap", bytes));
        const std::string log = log_out.str();
        check("TAPC-08", "a rejected TAP logs one error naming the file and the overrun",
              !ok && log.find("error TAP: '") != std::string::npos &&
              log.find("reason.tap' is not a valid TAP file: block 1 at offset 21 "
                       "declares 7 bytes but only 4 remain") != std::string::npos,
              log);
    }

    // TAPC-09 — Emulator::load_tap(), the path both --load and the GUI Tape
    // menu take: a malformed tape fails and leaves the tape already in the
    // player untouched.
    {
        auto emu = std::make_unique<Emulator>();
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        const bool init_ok = emu->init(cfg);
        const bool good_ok = init_ok &&
            emu->load_tap(write_file("player-good.tap", concat({header_block(), data_block()})));
        const bool bad_ok =
            emu->load_tap(write_file("player-bad.tap", concat({header_block(), {0x07}})));
        char buf[96];
        std::snprintf(buf, sizeof(buf), "init=%d good=%d bad=%d blocks=%zu",
                      init_ok ? 1 : 0, good_ok ? 1 : 0, bad_ok ? 1 : 0,
                      emu->tape().block_count());
        check("TAPC-09", "Emulator::load_tap refuses a malformed tape; the loaded one stays",
              good_ok && !bad_ok && emu->tape().is_loaded() &&
              emu->tape().block_count() == 2, buf);
    }

    // TAPC-10/11 — a new tape replaces the tape that was in, whatever its
    // format: a TZX or WAV left attached kept the status bar, Rewind and
    // Eject on the old tape (the GUI shows TZX first).
    {
        // A TZX: header, then a $10 block — its ID, a 2-byte pause, and then
        // exactly a TAP block (2-byte length + body).
        const Bytes tzx_fixed = concat({{'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 0x01, 0x14,
                                         0x10, 0xE8, 0x03}, data_block()});
        auto emu = std::make_unique<Emulator>();
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        const bool init_ok = emu->init(cfg);
        const bool tzx_ok = init_ok && emu->load_tzx(write_file("replace.tzx", tzx_fixed));
        const bool tap_ok = emu->load_tap(write_file("replace.tap",
                                                     concat({header_block(), data_block()})));
        char buf[96];
        std::snprintf(buf, sizeof(buf), "init=%d tzx=%d tap=%d tzx_loaded_after=%d",
                      init_ok ? 1 : 0, tzx_ok ? 1 : 0, tap_ok ? 1 : 0,
                      emu->tzx_tape().is_loaded() ? 1 : 0);
        check("TAPC-10", "Emulator::load_tap after a TZX ejects the TZX",
              tzx_ok && tap_ok && !emu->tzx_tape().is_loaded() && emu->tape().is_loaded(), buf);
    }
    {
        // A minimal 8-bit mono PCM WAV (44-byte header + 64 samples).
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
        const bool tap_ok = emu->load_tap(write_file("replace2.tap",
                                                     concat({header_block(), data_block()})));
        char buf[96];
        std::snprintf(buf, sizeof(buf), "init=%d wav=%d tap=%d wav_loaded_after=%d",
                      init_ok ? 1 : 0, wav_ok ? 1 : 0, tap_ok ? 1 : 0,
                      emu->wav_tape().is_loaded() ? 1 : 0);
        check("TAPC-11", "Emulator::load_tap after a WAV ejects the WAV",
              wav_ok && tap_ok && !emu->wav_tape().is_loaded() && emu->tape().is_loaded(), buf);
    }

    // TAPC-12 — the display name is the bare file name whatever separator the
    // host's own path syntax uses. g_root / name is built by std::filesystem, so
    // on Windows it holds backslashes (the form --load C:\... and a native file
    // dialog hand over); a name taken with rfind('/') came back as the whole
    // path. Oracle: the contract in tap_loader.h ("the tape filename (for UI
    // display)"), as std::filesystem::path::filename() defines it per host.
    {
        TapLoader t;
        const std::string full = write_file("native-separators.tap",
                                            concat({header_block(), data_block()}));
        const bool ok = t.load(full);
        check("TAPC-12", "a TAP loaded through a native-separator path reports its bare file name",
              ok && t.filename() == "native-separators.tap",
              "path=" + full + " filename=" + t.filename());
    }


    // ---- LD-BYTES fast-load trap (GH #395) -------------------------------
    // The trap sits at LD-BREAK 0x056B, where every LD-BYTES entry converges
    // (FUSE traps the same spot: opcodes_base.c RET NZ -> tape_load_trap()).
    // Oracles: the 48K ROM listing, and the REAL ROM executed by the CPU on
    // the same block fed by jnext's own real-time player (the CPU is pinned by
    // FUSE's 1356 opcode tests). Fixtures are synthetic bytes of our own.
    {
        using namespace ldrig;
        const Bytes pl = {1, 2, 3, 4, 5};
        const uint8_t chk = static_cast<uint8_t>(0xFF ^ 1 ^ 2 ^ 3 ^ 4 ^ 5);
        auto mk = [&](Case c, const char* name, const Bytes& tap) { c.path = write_file(name, tap); return c; };
        auto both = [&](const Case& c, Result& rt, Result& fa) {
            rt = run(c, false, 500);
            fa = run(c, true, 20);
        };
        auto d = [&](const Result& rt, const Result& fa) {
            std::string why;
            const bool same = same_state(rt, fa, &why);
            char b[160];
            std::snprintf(b, sizeof(b), "rt:reached=%d f=%d AF=%04x AF'=%04x BC=%04x DE=%04x HL=%04x IX=%04x | "
                          "fast:reached=%d AF=%04x AF'=%04x BC=%04x DE=%04x HL=%04x IX=%04x same=%d(%s)",
                          rt.reached, rt.frames, rt.r.AF, rt.r.AF2, rt.r.BC, rt.r.DE, rt.r.HL, rt.r.IX,
                          fa.reached, fa.r.AF, fa.r.AF2, fa.r.BC, fa.r.DE, fa.r.HL, fa.r.IX, same, why.c_str());
            return std::string(b);
        };
        const Bytes good = tap_block(0xFF, pl);

        {   // TAPC-13
            Case c = mk(Case{}, "t13.tap", good);
            Result f = run(c, true, 20);
            check("TAPC-13", "fast load serves a caller that enters LD-BYTES at 0x0562 (past 0x0556)",
                  f.built && f.reached && carry(f) && f.mem[0] == 1 && f.mem[4] == 5 && f.at_end,
                  "reached=" + std::to_string(f.reached));
        }
        {   // TAPC-14
            Case c; c.mt = MachineType::ZX128K; c.rom1_128 = true;
            c = mk(c, "t14.tap", good);
            Result f = run(c, true, 20);
            check("TAPC-14", "same on ZX128K with ROM 1 (the 48K BASIC ROM) paged",
                  f.built && f.reached && carry(f) && f.mem[0] == 1 && f.mem[4] == 5 && f.at_end,
                  "reached=" + std::to_string(f.reached));
        }
        {   // TAPC-15
            Case c; c.entry = 0x0556; c.bordcr = 0x10;
            c = mk(c, "t15.tap", good);
            Result f = run(c, true, 20);
            check("TAPC-15", "an entry at 0x0556 returns through SA/LD-RET: EI executed, border restored from BORDCR",
                  f.built && f.reached && carry(f) && f.mem[4] == 5 && f.r.IFF1 == 1 && f.border == 2,
                  "iff1=" + std::to_string(f.r.IFF1) + " border=" + std::to_string(f.border));
        }
        {   // TAPC-16
            Case c = mk(Case{}, "t16.tap", good);
            Result rt, fa; both(c, rt, fa);
            check("TAPC-16", "success: fast load leaves the registers the ROM leaves (AF 0093, AF' 2165, BC B021, L=checksum, DE 0)",
                  same_state(rt, fa) && fa.r.AF == 0x0093 && fa.r.AF2 == 0x2165 && fa.r.BC == 0xB021 &&
                  fa.r.HL == chk && fa.r.DE == 0 && fa.r.IX == 0xA005, d(rt, fa));
        }
        {   // TAPC-17
            Case c = mk(Case{}, "t17.tap", tap_block(0xFF, pl, false));
            Result rt, fa; both(c, rt, fa);
            check("TAPC-17", "bad checksum: carry clear, payload written, same state as the ROM",
                  same_state(rt, fa) && !carry(fa) && fa.mem[4] == 5, d(rt, fa));
        }
        {   // TAPC-18
            Case c; c.flag = 0x00; c.de = 17; c.prefill = Bytes(8, 0xEE);
            c = mk(c, "t18.tap", concat({good, tap_block(0xFF, {9})}));
            Result rt, fa; both(c, rt, fa);
            Result f2 = fa;
            check("TAPC-18", "flag mismatch: carry clear, nothing written, block consumed, same state as the ROM",
                  same_state(rt, fa) && !carry(fa) && fa.mem[0] == 0xEE && fa.block == 1, d(rt, fa));
            (void)f2;
        }
        {   // TAPC-19
            Case c; c.de = 3; c.prefill = Bytes(8, 0xEE);
            c = mk(c, "t19.tap", good);
            Result rt, fa; both(c, rt, fa);
            check("TAPC-19", "DE shorter than the payload: DE bytes written, carry clear (parity takes the next byte)",
                  same_state(rt, fa) && !carry(fa) && fa.mem[2] == 3 && fa.mem[3] == 0xEE, d(rt, fa));
        }
        {   // TAPC-20
            Case c; c.de = 8;
            c = mk(c, "t20.tap", good);
            Result rt, fa; both(c, rt, fa);
            check("TAPC-20", "DE longer than the payload: payload+checksum written, DE reduced, carry clear, B=0, L=1",
                  same_state(rt, fa) && !carry(fa) && fa.mem[5] == chk && fa.r.DE == 2 &&
                  (fa.r.BC >> 8) == 0 && (fa.r.HL & 0xFF) == 1, d(rt, fa));
        }
        {   // TAPC-21
            Case c; c.load = false; c.prefill = Bytes(pl);
            c = mk(c, "t21.tap", good);
            Result rt, fa; both(c, rt, fa);
            check("TAPC-21", "VERIFY against matching memory: carry set, memory untouched",
                  same_state(rt, fa) && carry(fa) && fa.mem[0] == 1 && fa.mem[4] == 5, d(rt, fa));
        }
        {   // TAPC-22
            Case c; c.load = false; c.prefill = Bytes{1, 2, 9, 4, 5};
            c = mk(c, "t22.tap", good);
            Result rt, fa; both(c, rt, fa);
            check("TAPC-22", "VERIFY with one byte differing: carry clear, memory untouched",
                  same_state(rt, fa) && !carry(fa) && fa.mem[2] == 9, d(rt, fa));
        }
        {   // TAPC-23
            Case cc; cc.mt = MachineType::ZX128K; cc = mk(cc, "t23.tap", good);
            auto emu = std::make_unique<Emulator>();
            EmulatorConfig cfg; cfg.type = cc.mt; cfg.rewind_buffer_frames = 0;
            cfg.sd_card_image = sd_image(); cfg.sd_card_readonly = true;
            bool ok = emu->init(cfg) && emu->load_tap(cc.path, true);
            emu->mmu().write(0x9000, 0x00); emu->mmu().write(0x9001, 0x80);
            auto r = emu->cpu().get_registers();
            r.PC = 0x056B; r.SP = 0x9000; r.AF = 0x0040; r.IFF1 = r.IFF2 = 0;
            emu->cpu().set_registers(r);
            emu->run_frame();
            check("TAPC-23", "ROM 0 (128 editor) paged at 0x056B: no trap, the tape is not touched",
                  ok && emu->tape().current_block() == 0, "block=" + std::to_string(emu->tape().current_block()));
        }
        {   // TAPC-24
            Case cc = mk(Case{}, "t24.tap", good);
            auto emu = std::make_unique<Emulator>();
            EmulatorConfig cfg; cfg.type = cc.mt; cfg.rewind_buffer_frames = 0;
            cfg.sd_card_image = sd_image(); cfg.sd_card_readonly = true;
            bool ok = emu->init(cfg) && emu->load_tap(cc.path, true);
            emu->mmu().write(0x8000, 0x18); emu->mmu().write(0x8001, 0xFE);
            emu->mmu().write(0x9000, 0x00); emu->mmu().write(0x9001, 0x80);
            auto r = emu->cpu().get_registers();
            r.PC = 0x056B; r.SP = 0x9000; r.AF = 0x0000; r.IFF1 = r.IFF2 = 0;
            emu->cpu().set_registers(r);
            emu->run_frame();
            check("TAPC-24", "LD-BREAK with Z clear: the ROM's RET NZ is taken, no trap",
                  ok && emu->tape().current_block() == 0 && emu->cpu().pc() == 0x8000,
                  "block=" + std::to_string(emu->tape().current_block()) + " pc=" + std::to_string(emu->cpu().pc()));
        }
    }

    Log::emulator()->sinks().pop_back();
    std::filesystem::remove_all(g_root, ec);

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
