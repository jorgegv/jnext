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
#include "core/saveable.h"
#include "core/tap_loader.h"
#include "ld_trap_rig.h"
#include "core/tape_poll_detector.h"

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
        {   // TAPC-25
            // Only the first signature (0x056B) present, the second (0x05DF) not:
            // an image of some other ROM that happens to match there. Control: both.
            auto trapped = [&](bool second) {
                auto emu = std::make_unique<Emulator>();
                EmulatorConfig cfg; cfg.type = MachineType::ZX48K; cfg.rewind_buffer_frames = 0;
                const bool ok = emu->init(cfg) && emu->load_tap(write_file("t25.tap", good), true);
                std::vector<uint8_t> boot(0x2000, 0x00);
                const uint8_t a[4] = {0xC0, 0xCD, 0xE7, 0x05}, b[4] = {0x7C, 0xFE, 0x01, 0xC9};
                for (int i = 0; i < 4; ++i) { boot[0x056B + i] = a[i]; if (second) boot[0x05DF + i] = b[i]; }
                emu->mmu().set_boot_rom(boot.data(), boot.size());
                emu->mmu().set_boot_rom_enabled(true);
                emu->mmu().write(0x9000, 0x00); emu->mmu().write(0x9001, 0x80);
                auto r = emu->cpu().get_registers();
                r.PC = 0x056B; r.SP = 0x9000; r.AF = 0x0040; r.AF2 = 0xFF01; r.IX = 0xA000; r.DE = 5;
                r.IFF1 = r.IFF2 = 0;
                emu->cpu().set_registers(r);
                emu->run_frame();
                return ok ? int(emu->tape().current_block()) : -1;
            };
            const int both = trapped(true), first_only = trapped(false);
            check("TAPC-25", "the identity check needs both signatures: only the 0x056B one is not the 48K LD-BYTES",
                  both == 1 && first_only == 0,
                  "both=" + std::to_string(both) + " first_only=" + std::to_string(first_only));
        }
        {   // TAPC-26
            // A pass through LD-BREAK with no tape must not touch memory the
            // debugger watches: a READ watchpoint inside the 0x05DF signature
            // (never executed by this run) stays quiet.
            auto emu = std::make_unique<Emulator>();
            EmulatorConfig cfg; cfg.type = MachineType::ZX48K; cfg.rewind_buffer_frames = 0;
            cfg.sd_card_image = sd_image(); cfg.sd_card_readonly = true;
            const bool ok = emu->init(cfg);
            emu->debug_state().set_clients_attached(true);
            emu->debug_state().set_live_raster(true);
            emu->debug_state().breakpoints().add_watchpoint(0x05E0, WatchType::READ);
            auto r = emu->cpu().get_registers();
            r.PC = 0x056B; r.SP = 0x9000; r.AF = 0x0040; r.IFF1 = r.IFF2 = 0;
            emu->cpu().set_registers(r);
            emu->run_frame();
            check("TAPC-26", "an LD-BREAK pass with no tape attached trips no read watchpoint (no identity read)",
                  ok && !emu->debug_state().paused(),
                  "paused=" + std::to_string(emu->debug_state().paused()));
        }
        {   // TAPC-27
            // With a fast tape attached and the load served by the trap, the identity
            // check runs: it must not trip a READ watchpoint inside its signature
            // (0x05E0 is never executed by this run, so only the check reads it).
            Case c = mk(Case{}, "t27.tap", good);
            auto emu = std::make_unique<Emulator>();
            EmulatorConfig cfg; cfg.type = MachineType::ZX48K; cfg.rewind_buffer_frames = 0;
            cfg.sd_card_image = sd_image(); cfg.sd_card_readonly = true;
            const bool ok = emu->init(cfg) && emu->load_tap(c.path, true);
            Bytes code = caller(c);
            const uint16_t jr = uint16_t(0x8000 + code.size());
            code.push_back(0x18); code.push_back(0xFE);
            for (size_t i2 = 0; i2 < code.size(); ++i2) emu->mmu().write(uint16_t(0x8000 + i2), code[i2]);
            emu->debug_state().set_clients_attached(true);
            emu->debug_state().set_live_raster(true);
            emu->debug_state().breakpoints().add_watchpoint(0x05E0, WatchType::READ);
            auto r = emu->cpu().get_registers();
            r.PC = 0x8000; r.SP = 0x9000; r.IFF1 = r.IFF2 = 0;
            emu->cpu().set_registers(r);
            for (int f = 0; f < 10 && emu->cpu().pc() != jr; ++f) emu->run_frame();
            check("TAPC-27", "a trap-served LD-BREAK pass trips no read watchpoint on the identity bytes (peek, not read)",
                  ok && emu->tape().current_block() == 1 && emu->cpu().pc() == jr && !emu->debug_state().paused(),
                  "blk=" + std::to_string(emu->tape().current_block()) + " paused=" + std::to_string(emu->debug_state().paused()));
        }
    }

    // ---- Automatic tape play/stop (GH #395) ------------------------------
    // No hardware counterpart (no Spectrum has tape motor control): a host
    // deck feature. Oracle: the design in the plan (B2): the deck stops only
    // at a gap boundary when no one polls the ULA port, resumes at the next
    // block's first pilot pulse when polling starts, and a program that keeps
    // polling never sees a difference. Fixtures: our own synthetic bytes and
    // hand-written Z80 loops.
    {
        // TAPA-01..05: the detector itself.
        {
            TapePollDetector d; uint64_t t = 1000; bool early = false;
            for (int i = 0; i < 31; ++i) { d.on_read(t); early = early || d.polling(t); t += 59; }
            const bool at31 = d.polling(t - 59);
            d.on_read(t);
            check("TAPA-01", "31 reads 59 T apart are not polling, the 32nd is",
                  !early && !at31 && d.polling(t));
        }
        {
            TapePollDetector a, b; uint64_t t = 5000;
            for (int i = 0; i < 40; ++i) { a.on_read(t); b.on_read(t); t += 10; }
            a.on_read(t - 10 + 1000);      // exactly G after the last read: the run continues
            b.on_read(t - 10 + 1001);      // G + 1: a new run of 1
            check("TAPA-02", "a gap of exactly 1000 T continues the run, 1001 T restarts it",
                  a.polling(t - 10 + 1000) && !b.polling(t - 10 + 1001));
        }
        {
            TapePollDetector d; bool ever = false; uint64_t t = 0;
            for (int frame = 0; frame < 200; ++frame) {
                for (int i = 0; i < 8; ++i) { d.on_read(t + i * 59); ever = ever || d.polling(t + i * 59); }
                ever = ever || d.polling(t + 8 * 59 + 100);
                t += 69888;
            }
            check("TAPA-03", "the ROM keyboard scan (bursts of 8 reads 59 T apart, once a frame) is never polling",
                  !ever);
        }
        {
            TapePollDetector d; uint64_t t = 100;
            for (int i = 0; i < 40; ++i) { d.on_read(t); t += 20; }
            const uint64_t last = t - 20;
            check("TAPA-04", "a run of 40 reads is not polling once the last read is more than 1000 T old",
                  d.polling(last) && d.polling(last + 1000) && !d.polling(last + 1001));
        }
        {
            TapePollDetector d; uint64_t t = 90000;
            for (int i = 0; i < 40; ++i) { d.on_read(t); t += 20; }
            const bool was = d.polling(t - 20);
            d.on_read(50000);                  // time went backwards (a state load)
            const bool back = d.polling(50000) || d.polling(60000);
            for (int i = 0; i < 40; ++i) { d.on_read(t); t += 20; }
            const bool again = d.polling(t - 20);
            d.reset();
            check("TAPA-05", "time going backwards and reset() both end a run; no wrap to polling",
                  was && !back && again && !d.polling(t - 20));
        }
    }

    {
        using namespace ldrig;
        // Delay without reading the port (M outer rounds of ~1.7 M T), then an
        // edge loop. BC = 0xFFFE keeps the port address fixed.
        auto delay_then_poll = [](uint16_t m) {
            return Bytes{0xF3, 0x11, uint8_t(m & 0xFF), uint8_t(m >> 8),
                         0x21, 0x00, 0x00, 0x2B, 0x7C, 0xB5, 0x20, 0xFB,
                         0x1B, 0x7A, 0xB3, 0x20, 0xF3,
                         0xED, 0x78, 0x18, 0xFC};
        };
        const Bytes poll_only = {0xF3, 0xED, 0x78, 0x18, 0xFC};
        const Bytes blk = tap_block(0xFF, {1, 2, 3, 4, 5});
        const std::string two = write_file("auto2.tap", concat({blk, tap_block(0xFF, {6, 7, 8, 9})}));
        // A header block then a data block: their pilots differ (8063 / 3223 pulses).
        const std::string hdr_two = write_file("autoh.tap",
                                               concat({tap_block(0x00, Bytes(17, 0x20)), tap_block(0xFF, {6, 7, 8, 9})}));

        auto mk = [&](const std::string& tap, bool fast, bool autop, const Bytes& prog) {
            auto emu = std::make_unique<Emulator>();
            EmulatorConfig cfg; cfg.type = MachineType::ZX48K; cfg.rewind_buffer_frames = 0;
            cfg.tape_auto_play = autop;
            bool ok = emu->init(cfg) && emu->load_tap(tap, fast);
            for (size_t i = 0; i < prog.size(); ++i) emu->mmu().write(uint16_t(0x8000 + i), prog[i]);
            auto r = emu->cpu().get_registers();
            r.PC = 0x8000; r.SP = 0x9000; r.BC = 0xFFFE; r.IFF1 = r.IFF2 = 0;
            emu->cpu().set_registers(r);
            return std::pair<std::unique_ptr<Emulator>, bool>(std::move(emu), ok);
        };
        // Steps instruction by instruction to `until` T-states, recording the
        // T-state of every change of the tape EAR level.
        auto step_to = [](Emulator& e, uint64_t until, std::vector<uint64_t>& edges) {
            bool last = e.beeper().tape_ear();
            while (e.monotonic_tstates() < until) {
                e.execute_single_instruction();
                const bool now = e.beeper().tape_ear();
                if (now != last) { edges.push_back(e.monotonic_tstates()); last = now; }
            }
        };

        {   // TAPA-06
            auto [emu, ok] = mk(hdr_two, false, true, delay_then_poll(20));
            int f = 0;
            for (; f < 600 && !emu->tape().is_held(); ++f) emu->run_frame();
            const bool held = ok && emu->tape().is_held() && !emu->tape().is_playing() &&
                              emu->tape().current_block() == 1;
            const uint64_t t_hold = emu->monotonic_tstates();
            std::vector<uint64_t> edges;
            uint64_t t_res = 0;
            bool resumed = false;
            while (emu->monotonic_tstates() < t_hold + 40000000ULL) {
                const bool was = emu->beeper().tape_ear();
                emu->execute_single_instruction();
                const bool now = emu->beeper().tape_ear();
                if (now != was) edges.push_back(emu->monotonic_tstates());
                if (!resumed && emu->tape().is_playing()) { resumed = true; t_res = emu->monotonic_tstates(); }
                if (resumed && emu->monotonic_tstates() > t_res + 7500000ULL) break;
            }
            size_t before = 0;
            while (before < edges.size() && edges[before] <= t_res) ++before;
            const size_t silent_edges = before;    // edges while held (up to the resume)
            // The first edges after the resume: a complete data pilot.
            size_t pilot = 0;
            for (size_t i = silent_edges; i + 1 < edges.size(); ++i) {
                const int64_t d = int64_t(edges[i + 1]) - int64_t(edges[i]);
                if (d >= 2168 - 40 && d <= 2168 + 40) ++pilot; else break;
            }
            const int64_t first = silent_edges < edges.size() ? int64_t(edges[silent_edges]) - int64_t(t_res) : -1;
            const size_t k = silent_edges + pilot;     // index of the last pilot edge
            const int64_t sync = (k + 1 < edges.size()) ? int64_t(edges[k + 1]) - int64_t(edges[k]) : -1;
            char b[160];
            std::snprintf(b, sizeof(b), "held=%d f=%d silent=%.1fM edges_while_held=%zu pilot_intervals=%zu sync=%lld",
                          held, f, (t_res - t_hold) / 1e6, silent_edges, pilot, (long long)sync);
            check("TAPA-06", "real time: the deck holds in the gap while the program does not poll, then resumes with a complete data pilot",
                  held && resumed && t_res - t_hold >= 10000000ULL && silent_edges <= 1 &&
                  pilot == 3222 && sync >= 667 - 40 && sync <= 667 + 40 && first > 0 && first <= 2168 + 60, b);
        }
        {   // TAPA-07
            std::vector<uint64_t> on_edges, off_edges;
            bool held_on = false;
            for (int pass = 0; pass < 2; ++pass) {
                auto [emu, ok] = mk(two, false, pass == 0, poll_only);
                (void)ok;
                step_to(*emu, 25000000ULL, pass == 0 ? on_edges : off_edges);
                if (pass == 0) held_on = emu->tape().is_held();
            }
            check("TAPA-07", "a program that polls across every gap sees the same edge timeline with auto play on and off",
                  !held_on && on_edges.size() > 6000 && on_edges == off_edges,
                  "on=" + std::to_string(on_edges.size()) + " off=" + std::to_string(off_edges.size()));
        }
        {   // TAPA-08
            auto [emu, ok] = mk(hdr_two, false, false, delay_then_poll(20));
            std::vector<uint64_t> edges;
            step_to(*emu, 26000000ULL, edges);
            int64_t gap = 0;
            for (size_t i = 0; i + 1 < edges.size(); ++i)
                gap = std::max(gap, int64_t(edges[i + 1]) - int64_t(edges[i]));
            check("TAPA-08", "auto play off: no hold, the next pilot starts exactly 1 s (3,500,000 T) after the last edge",
                  ok && !emu->tape().is_held() && gap >= 3500000 - 50 && gap <= 3500000 + 50,
                  "max gap=" + std::to_string(gap));
        }
        // A "press any key" wait: a tight loop of IN A,(C) for ~12 M T-states (longer
        // than a data block's pilot), then a ROM load entered at 0x0562.
        const Bytes key_wait = {0xF3, 0x01, 0xFE, 0xFF, 0x11, 0x05, 0x00, 0x21, 0x00, 0x00,
                                0xED, 0x78, 0x2B, 0x7C, 0xB5, 0x20, 0xF9, 0x1B, 0x7A, 0xB3, 0x20, 0xF1};
        {   // TAPA-09
            auto [emu, ok] = mk(two, true, true, poll_only);
            for (int f = 0; f < 300; ++f) emu->run_frame();
            check("TAPA-09", "fast mode with the defaults: an edge loop never starts the tape (the ROM trap serves the loads)",
                  ok && !emu->tape().is_playing() && !emu->tape().is_held() &&
                  emu->tape().current_block() == 0);
        }
        {   // TAPA-10
            Case c; c.path = two;
            auto emu = std::make_unique<Emulator>();
            EmulatorConfig cfg; cfg.type = MachineType::ZX48K; cfg.rewind_buffer_frames = 0;
            cfg.sd_card_image = sd_image(); cfg.sd_card_readonly = true;
            bool ok = emu->init(cfg) && emu->load_tap(two, true);
            Bytes prog = key_wait;
            Bytes call = caller(c);
            prog.insert(prog.end(), call.begin(), call.end());
            prog.push_back(0x18); prog.push_back(0xFE);
            const uint16_t jr = uint16_t(0x8000 + prog.size() - 2);
            for (size_t i = 0; i < prog.size(); ++i) emu->mmu().write(uint16_t(0x8000 + i), prog[i]);
            auto r = emu->cpu().get_registers();
            r.PC = 0x8000; r.SP = 0x9000; r.IFF1 = r.IFF2 = 0;
            emu->cpu().set_registers(r);
            for (int f = 0; f < 400 && emu->cpu().pc() != jr; ++f) emu->run_frame();
            const bool got = emu->cpu().pc() == jr && (emu->cpu().get_registers().AF & 1) &&
                             emu->mmu().peek(0xA000) == 1 && emu->mmu().peek(0xA004) == 5;
            check("TAPA-10", "fast mode: a key-wait longer than the next block's pilot, then a ROM load, still loads that block (GH #395 review F1)",
                  ok && got && !emu->tape().is_playing() && emu->tape().current_block() == 1,
                  "pc=" + std::to_string(emu->cpu().pc()) + " blk=" + std::to_string(emu->tape().current_block()) +
                  " a000=" + std::to_string(emu->mmu().peek(0xA000)));
        }
        {   // TAPA-11
            auto [emu, ok] = mk(two, true, false, poll_only);
            for (int f = 0; f < 100; ++f) emu->run_frame();
            check("TAPA-11", "fast mode, auto play off: an edge loop never starts the tape",
                  ok && !emu->tape().is_playing() && emu->tape().current_block() == 0);
        }
        {   // TAPA-12
            auto [emu, ok] = mk(two, true, true, poll_only);
            for (int i = 0; i < 100; ++i) (void)emu->port().read(0x00FE);
            const bool dbg = emu->tape_poll().polling(emu->monotonic_tstates());
            for (int i = 0; i < 100; ++i) emu->execute_single_instruction();
            const bool cpu = emu->tape_poll().polling(emu->monotonic_tstates());
            check("TAPA-12", "reads made by the debugger do not count as polling, the CPU's IN does",
                  ok && !dbg && cpu);
        }
        {   // TAPA-13
            auto [emu, ok] = mk(two, true, true, poll_only);
            while (emu->tape().next_block()) {}
            for (int f = 0; f < 50; ++f) emu->run_frame();
            check("TAPA-13", "a tape at its end is never started by a polling program",
                  ok && emu->tape().at_end() && !emu->tape().is_playing());
        }
        {   // TAPA-14
            auto [emu, ok] = mk(two, true, true, poll_only);
            for (int f = 0; f < 3; ++f) emu->run_frame();
            StateWriter m1; emu->save_state(m1);
            std::vector<uint8_t> blob(m1.position());
            StateWriter w1(blob.data(), blob.size()); emu->save_state(w1);
            emu->run_frame();
            const bool was = emu->tape_poll().polling(emu->monotonic_tstates());
            StateReader r1(blob.data(), blob.size());
            const bool loaded = emu->load_state(r1);
            const bool after = emu->tape_poll().polling(emu->monotonic_tstates());
            // A held real-time deck stays held across a state load.
            auto [e2, ok2] = mk(hdr_two, false, true, delay_then_poll(20));
            int f = 0;
            for (; f < 600 && !e2->tape().is_held(); ++f) e2->run_frame();
            StateWriter m2; e2->save_state(m2);
            std::vector<uint8_t> blob2(m2.position());
            StateWriter w2(blob2.data(), blob2.size()); e2->save_state(w2);
            StateReader r2(blob2.data(), blob2.size());
            const bool loaded2 = e2->load_state(r2);
            check("TAPA-14", "a state load resets the detector and leaves a held deck held",
                  ok && ok2 && was && loaded && !after && loaded2 && e2->tape().is_held() &&
                  !e2->tape_poll().polling(e2->monotonic_tstates()),
                  "was=" + std::to_string(was) + " after=" + std::to_string(after) +
                  " held=" + std::to_string(e2->tape().is_held()));
        }
        {   // TAPA-16
            // The resume re-bases the instruction to the read's sample T-state (GH #265), so
            // the first resumed pulse ends 2168 T after that sample, not after the instruction
            // start. A program counts IN A,(C) loops until EAR rises; NOP padding moves the
            // loop phase, and the count changes where the sample point matters. The values
            // are PINNED (measured with the sample point; re-basing to 0 gives 69, 61 and 58
            // for the pads 4, 8 and 10), not derived from an outside oracle.
            static const int want[12] = {83, 79, 75, 71, 70, 67, 65, 63, 62, 60, 59, 57};
            std::string got_s; bool all = true;
            for (int pad = 0; pad < 12; ++pad) {
                Bytes prog = delay_then_poll(20);
                prog.resize(prog.size() - 4);                              // drop the plain poll loop
                prog.insert(prog.end(), {0x21, 0x00, 0x00});               // LD HL,0
                const size_t loop = prog.size();
                prog.push_back(0xED); prog.push_back(0x78);                // IN A,(C)
                prog.push_back(0x23);                                      // INC HL
                for (int i = 0; i < pad; ++i) prog.push_back(0x00);        // NOP padding
                prog.push_back(0xE6); prog.push_back(0x40);                // AND 0x40
                prog.push_back(0x28); prog.push_back(uint8_t(int(loop) - int(prog.size() + 2)));   // JR Z,loop
                prog.push_back(0x18); prog.push_back(0xFE);                // JR $
                auto [emu, ok] = mk(hdr_two, false, true, prog);
                for (int f = 0; f < 900; ++f) emu->run_frame();
                const int k = emu->cpu().get_registers().HL;
                got_s += std::to_string(k) + " ";
                all = all && ok && k == want[pad];
            }
            check("TAPA-16", "the resumed deck's first pulse is timed from the read's sample point (pinned loop counts for 12 phases)",
                  all, "got " + got_s);
        }
        {   // TAPA-15
            auto [emu, ok] = mk(hdr_two, false, true, delay_then_poll(20));
            int f = 0;
            for (; f < 600 && !emu->tape().is_held(); ++f) emu->run_frame();
            const bool held = ok && emu->tape().is_held() && !emu->tape().is_playing();
            emu->set_tape_auto_play(false);
            check("TAPA-15", "turning auto play off while the deck is held plays it again at once",
                  held && emu->tape().is_playing() && !emu->tape().is_held() && !emu->tape_auto_play());
        }
    }

    Log::emulator()->sinks().pop_back();
    std::filesystem::remove_all(g_root, ec);

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                total, passed, failed, 0);
    return failed == 0 ? 0 : 1;
}
