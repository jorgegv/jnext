// NEX loader screen-ingest test — issue #68.
//
// Oracle: the reference NextZXOS loader, tbblue/src/asm/nexload/nexload.asm
// (Jim Bagley / Robin Verhagen-Guest, GPLv3). This is a FILE-FORMAT
// convention, not hardware, so the loader source — not the VHDL — is the
// authority for where each screen block lands. The VHDL is consulted only
// to explain WHY the layout has a hole (see below).
//
// The three 12288-byte screen formats (LoRes, HiRes, HiColour) are loaded
// by the shipped `ELSE` path of each block as TWO 6144-byte freads with a
// 2048-byte hole between them:
//
//   LoRes    nexload.asm:472  ld ix,$4000 : ld bc,$1800 : call fread
//                        :474  ld ix,$6000 : ld bc,$1800 : call fread
//   HiRes                :488  / :490   (identical pair)
//   HiColour             :505  / :507   (identical pair)
//
// (The `IFDEF testing` / `IFDEF testing8000` branches inside each block are
// debug-only and are not assembled into the released loader; they are not
// the oracle.)
//
// Bank 5 occupies $4000-$7FFF throughout nexload's screen ingest — it never
// remaps MMU slots 2/3, only MMU_REGISTER_6/7 (nexload.asm:441-443, :514),
// and it loads bank 5 itself with `ld ix,$4000 : ld bc,$4000`
// (nexload.asm:525). Therefore:
//
//   $4000 = bank-5 offset 0x0000   (MMU page 10, offset 0)
//   $6000 = bank-5 offset 0x2000   (MMU page 11, offset 0)   ← NOT 0x1800
//
// leaving bank-5 offset 0x1800-0x1FFF ($5800-$5FFF) never written. That
// hole is the classic ULA attribute area, which none of these three modes
// addresses: `video/lores.vhd:93-94` adds 1 to lores_addr bits 13:11
// (i.e. +0x800) when y >= 96, so the bottom 48 LoRes rows read from
// 0x2000-0x37FF and 0x1800-0x1FFF is skipped entirely.
//
// The SCREEN_ULA path is deliberately NOT split — nexload.asm:457 is a
// single `ld ix,$4000 : ld bc,$1b00 : call fread` — and is covered here as
// a control row so the split is not over-generalised.
//
// These rows drive the REAL NexLoader::load() + apply() against a real
// Emulator, so they pin the actual call sites in nex_loader.cpp rather
// than a helper in isolation.
//
// ─────────────────────────────────────────────────────────────────────
// The NEXPC0-* group (GH #164) uses the same oracle for a different
// question: the LAUNCH decision. A header PC of 0 means "load only, do
// not execute" —
//
//   nexload.asm:580-581   ld hl,(PCReg):ld sp,(SPReg)
//                         ld a,h:or l:jr z,.returnToBasic
//   nexload2.asm:408-413  ld hl,(nexHeader.PC)
//                         ld a,h : or l : jr z,returnToBasic
//                         ld sp,(nexHeader.SP)
//
// — and neither loader ever jumps to address 0. Two consequences the
// rows below pin, both read straight off the oracle:
//
//   * the header SP has no effect on that path (nexload.asm loads it one
//     instruction before the branch and `.returnToBasic` immediately
//     replaces it with `ld sp,(oldStack)`, :597-600; nexload2.asm does
//     not load it at all before its own `ld sp,(oldStack)`, :436-438);
//   * the entry-bank mapping is REVERSED, not kept — `.returnToBasicTidy`
//     (nexload.asm:602-604) remaps MMU6/7 from BANKM, and nexload2.asm's
//     `cleanupBeforeBasic` (:529-535) does the same, commented "map
//     C000..FFFF region back to BASIC bank".
//
// jnext's `--load` has no OS to return to (it resets and applies the NEX
// before one instruction runs), so it honours "keep the state the load
// produced" by running nothing at all — Emulator::set_cpu_parked().
// Every PC=0 row is paired with a PC≠0 control so the guard is proven to
// key on PC==0 and not to have simply disabled the code path.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/nex_loader.h"
#include "core/saveable.h"
#include "memory/mmu.h"
#include "video/layer2.h"
#include "video/palette.h"
#include "video/renderer.h"
#include "video/ula.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>   // getpid() — per-process fixture paths, see fixture_path()

// ── Test infrastructure ───────────────────────────────────────────────

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

std::string g_group;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
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

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

// ── NEX fixture construction ─────────────────────────────────────────

// Screen payload byte at index i. Never 0x00 (so a byte that lands in the
// untouched hole is unambiguously distinguishable from the G16 pre-zero),
// and its 251-byte period does not divide 2048, so a block displaced by
// the buggy 0x800 offset produces different bytes at every position.
uint8_t payload_byte(size_t i) {
    return static_cast<uint8_t>(1 + (i % 251));
}

// Palette bytes. Entry i is two bytes: lo = RRRGGGBB, hi bit 0 = blue LSB.
// Deliberately drawn from a different sequence than payload_byte() so a
// screen block read 512 bytes early (the pre-#156 LoRes bug) is caught by
// content, not just by size.
uint8_t pal_lo(int i) { return static_cast<uint8_t>((i * 7 + 3) ^ 0xA5); }
uint8_t pal_hi(int i) { return static_cast<uint8_t>(i & 1); }

// The 9-bit RGB333 value NR 0x44's two-write sequence composes from one
// palette entry: first write supplies bits 8:1 (RRRGGGBB), second write
// supplies bit 0 (the blue LSB). nexload.asm:436-437.
uint16_t pal_expected_rgb333(int i) {
    return static_cast<uint16_t>((static_cast<uint16_t>(pal_lo(i)) << 1) | (pal_hi(i) & 1));
}

// Build a minimal, well-formed NEX carrying exactly one screen block of
// `screen_bytes` bytes selected by `screen_flag`, and write it to `path`.
// When `with_palette` is set a 512-byte palette block precedes the screen
// data, exactly as nexload.asm:428 reads it.
bool write_nex_fixture(const std::string& path, uint8_t screen_flag, size_t screen_bytes,
                       bool with_palette = false) {
    const size_t pal_bytes = with_palette ? 512u : 0u;
    std::vector<uint8_t> file(512 + pal_bytes + screen_bytes, 0x00);

    std::memcpy(file.data() + 0, "Next", 4);
    std::memcpy(file.data() + 4, "V1.2", 4);
    file[8]  = 0;            // ram_required: 768 KB
    file[9]  = 0;            // num_banks (bitmap at +18 is all-zero)
    file[10] = screen_flag;  // screen_flags
    file[11] = 0;            // border colour
    file[12] = 0x00; file[13] = 0xFF;  // SP = 0xFF00
    file[14] = 0x00; file[15] = 0x80;  // PC = 0x8000
    // banks[112] at +18 left all-zero: no bank data follows the screen.
    file[130] = 0;  // loading_bar off
    file[132] = 0;  // loading_delay
    file[133] = 0;  // start_delay
    file[134] = 0;  // preserve_regs = 0

    if (with_palette) {
        for (int i = 0; i < 256; ++i) {
            file[512 + i * 2]     = pal_lo(i);
            file[512 + i * 2 + 1] = pal_hi(i);
        }
    }
    for (size_t i = 0; i < screen_bytes; ++i) {
        file[512 + pal_bytes + i] = payload_byte(i);
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(f);
}

// Per-process temp path for a fixture. The PID is load-bearing, not
// cosmetic: two copies of this suite running at once — which is the
// normal state of this project (parallel agent worktrees, and the
// reviewer guidance to run mutation cycles concurrently) — otherwise
// pick the SAME /tmp name, and one deletes or truncates the other's
// fixture between write and load. Measured before the PID went in:
// 8 spurious `NEX fixture failed to load/apply` failures in 72 runs of
// six concurrent copies, sometimes a single row, which is exactly the
// shape of an unreproducible one-row triplet failure.
std::string fixture_path(const char* tag) {
    return (std::filesystem::temp_directory_path() /
            ("jnext_nexload_test_" + std::string(tag) + "_" +
             std::to_string(::getpid()) + ".nex")).string();
}

// Read one byte of bank 5 (16 KB, offsets 0x0000-0x3FFF) back through the
// same temp-slot-7 MMU mapping the loader writes through, so the readback
// traverses identical rebuild_ptr() / bank-5 dual-port VRAM machinery
// rather than assuming a physical RAM layout.
uint8_t read_bank5(Mmu& mmu, size_t off) {
    constexpr int      TEMP_SLOT = 7;
    constexpr uint16_t SLOT_BASE = 0xE000;
    const uint8_t saved = mmu.get_page(TEMP_SLOT);
    mmu.set_page(TEMP_SLOT, static_cast<uint8_t>(10 + off / 0x2000));
    const uint8_t v = mmu.read(static_cast<uint16_t>(SLOT_BASE + (off % 0x2000)));
    mmu.set_page(TEMP_SLOT, saved);
    return v;
}

// Compare bank-5 [dst_off, dst_off+len) against payload [src_off, ...).
// Returns true on full match; otherwise reports the first mismatch.
bool bank5_matches_payload(Mmu& mmu, size_t dst_off, size_t src_off, size_t len,
                           size_t& bad_at, uint8_t& got, uint8_t& want) {
    for (size_t i = 0; i < len; ++i) {
        const uint8_t g = read_bank5(mmu, dst_off + i);
        const uint8_t w = payload_byte(src_off + i);
        if (g != w) {
            bad_at = dst_off + i;
            got = g;
            want = w;
            return false;
        }
    }
    bad_at = 0; got = 0; want = 0;
    return true;
}

// The 2048-byte hole at bank-5 0x1800-0x1FFF must contain no payload byte.
// After a load it reads 0x00 because NexLoader::apply() pre-zeroes bank 5
// (G16); the point of the row is that no screen byte was written there.
bool bank5_hole_untouched(Mmu& mmu, size_t& bad_at, uint8_t& got) {
    for (size_t off = 0x1800; off < 0x2000; ++off) {
        const uint8_t g = read_bank5(mmu, off);
        if (g != 0x00) {
            bad_at = off;
            got = g;
            return false;
        }
    }
    bad_at = 0; got = 0;
    return true;
}

struct LoadedFixture {
    Emulator emu;
    bool     ok = false;
    uint64_t payload_offset = 0;   ///< NexLoader::payload_offset() after load()

    LoadedFixture(uint8_t screen_flag, size_t screen_bytes, const char* tag,
                  bool with_palette = false) {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        const std::string path = fixture_path(tag);

        if (!write_nex_fixture(path, screen_flag, screen_bytes, with_palette)) return;

        NexLoader loader;
        if (loader.load(path)) {
            payload_offset = loader.payload_offset();
            ok = loader.apply(emu);
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// ── GH #164 launch-decision fixtures ─────────────────────────────────

// Build a minimal, well-formed NEX carrying exactly one 16 KB bank plus
// the header fields the launch decision reads. The bank is filled with
// payload_byte() (never 0x00, 251-byte period) and `lead` overwrites its
// first bytes, which lets a control fixture plant executable code at the
// bank's start.
bool write_nex_bank_fixture(const std::string& path, uint16_t pc, uint16_t sp,
                            uint8_t preserve_regs, uint8_t entry_bank,
                            uint8_t bank_no, const std::vector<uint8_t>& lead,
                            uint8_t start_delay = 0) {
    constexpr size_t BANK = 16384;
    std::vector<uint8_t> file(512 + BANK, 0x00);

    std::memcpy(file.data() + 0, "Next", 4);
    std::memcpy(file.data() + 4, "V1.2", 4);
    file[8]  = 0;  // ram_required: 768 KB
    file[9]  = 1;  // num_banks — matches the single bitmap entry below
    file[10] = 0;  // screen_flags: no screen block
    file[11] = 7;  // border colour
    file[12] = static_cast<uint8_t>(sp & 0xFF);
    file[13] = static_cast<uint8_t>(sp >> 8);
    file[14] = static_cast<uint8_t>(pc & 0xFF);
    file[15] = static_cast<uint8_t>(pc >> 8);
    file[18 + bank_no] = 1;   // bank presence bitmap
    file[130] = 0;            // loading_bar off
    file[132] = 0;            // loading_delay
    file[133] = start_delay;
    file[134] = preserve_regs;
    file[139] = entry_bank;

    for (size_t i = 0; i < BANK; ++i) file[512 + i] = payload_byte(i);
    for (size_t i = 0; i < lead.size() && i < BANK; ++i) file[512 + i] = lead[i];

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(f);
}

// NOTE: fixture_path() above is shared — it carries the PID, which the
// NEXPC0 rows need for exactly the reason its comment gives (concurrent
// mutation runs would otherwise fight over one /tmp name).

std::string tape_fixture_path(const char* tag, const char* ext) {
    return (std::filesystem::temp_directory_path() /
            ("jnext_nexpc0_" + std::string(tag) + "_" +
             std::to_string(::getpid()) + "." + ext)).string();
}

// One 19-byte header block + one data block — enough for TapLoader::load()
// to accept the file; the rows only care that attaching it un-parks.
bool write_tap_fixture(const std::string& path) {
    std::vector<uint8_t> t;
    auto push_block = [&](const std::vector<uint8_t>& body) {
        const uint16_t len = static_cast<uint16_t>(body.size());
        t.push_back(len & 0xFF);
        t.push_back(static_cast<uint8_t>(len >> 8));
        t.insert(t.end(), body.begin(), body.end());
    };
    std::vector<uint8_t> hdr(19, 0);
    hdr[0] = 0x00;                       // flag: header
    hdr[1] = 3;                          // type: CODE
    std::memcpy(&hdr[2], "probe     ", 10);
    hdr[12] = 0x10;                      // length 16
    hdr[14] = 0x00; hdr[15] = 0x90;      // start 0x9000
    uint8_t p = 0; for (int i = 0; i < 18; ++i) p ^= hdr[i]; hdr[18] = p;
    push_block(hdr);
    std::vector<uint8_t> dat(18, 0);
    dat[0] = 0xFF;                       // flag: data
    for (int i = 1; i <= 16; ++i) dat[i] = static_cast<uint8_t>(0xA0 + i);
    p = 0; for (int i = 0; i < 17; ++i) p ^= dat[i]; dat[17] = p;
    push_block(dat);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(t.data()), static_cast<std::streamsize>(t.size()));
    return static_cast<bool>(f);
}

// "ZXTape!" 0x1A + v1.20 + one 0x10 standard-speed data block.
bool write_tzx_fixture(const std::string& path) {
    std::vector<uint8_t> t = {'Z','X','T','a','p','e','!',0x1A, 1, 20};
    t.push_back(0x10);                       // standard speed data block
    t.push_back(0xE8); t.push_back(0x03);    // pause 1000 ms
    std::vector<uint8_t> dat(18, 0);
    dat[0] = 0xFF;
    for (int i = 1; i <= 16; ++i) dat[i] = static_cast<uint8_t>(0xA0 + i);
    uint8_t p = 0; for (int i = 0; i < 17; ++i) p ^= dat[i]; dat[17] = p;
    t.push_back(static_cast<uint8_t>(dat.size() & 0xFF));
    t.push_back(static_cast<uint8_t>(dat.size() >> 8));
    t.insert(t.end(), dat.begin(), dat.end());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(t.data()), static_cast<std::streamsize>(t.size()));
    return static_cast<bool>(f);
}

// A minimal valid RZX carrying NO embedded snapshot: "RZX!" header, a
// creator block, and an uncompressed input-recording block. The absent
// snapshot is the point — with one, load_rzx() routes through
// load_sna/load_szx, which reset and therefore un-park by themselves, so
// only this shape reaches the resume call in load_rzx().
bool write_rzx_fixture(const std::string& path, uint32_t frames) {
    std::vector<uint8_t> r;
    auto u16 = [&](uint16_t v) { r.push_back(v & 0xFF); r.push_back(static_cast<uint8_t>(v >> 8)); };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) r.push_back((v >> (8 * i)) & 0xFF); };

    // Header: "RZX!" + major + minor + flags   (rzx.h:125-132)
    for (char c : std::string("RZX!")) r.push_back(static_cast<uint8_t>(c));
    r.push_back(0); r.push_back(13);
    u32(0);

    // Creator block: id + len(29) + 20-byte name + major + minor (rzx.h:139-147)
    r.push_back(0x10);
    u32(29);
    { const std::string name = "jnext-test";
      for (size_t i = 0; i < 20; ++i)
          r.push_back(i < name.size() ? static_cast<uint8_t>(name[i]) : 0); }
    u16(0); u16(1);

    // Input recording: id + len + num_frames + reserved + initial_tstates
    // + flags(uncompressed), then one 4-byte frame each with no IN values
    // (rzx.h:177-186, :205-212).
    const uint32_t frame_bytes = frames * 4;
    r.push_back(0x80);
    u32(18 + frame_bytes);
    u32(frames);
    r.push_back(0);
    u32(0);
    u32(0);                       // flags: not compressed
    for (uint32_t i = 0; i < frames; ++i) { u16(1000); u16(0); }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(r.data()), static_cast<std::streamsize>(r.size()));
    return static_cast<bool>(f);
}

// RIFF/WAVE, 8-bit unsigned mono, 8000 Hz, 800 samples of square wave.
bool write_wav_fixture(const std::string& path) {
    constexpr uint32_t RATE = 8000, NSAMP = 800;
    std::vector<uint8_t> w;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back((v >> (8 * i)) & 0xFF); };
    auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; ++i) w.push_back((v >> (8 * i)) & 0xFF); };
    for (char c : std::string("RIFF")) w.push_back(static_cast<uint8_t>(c));
    u32(36 + NSAMP);
    for (char c : std::string("WAVEfmt ")) w.push_back(static_cast<uint8_t>(c));
    u32(16); u16(1); u16(1); u32(RATE); u32(RATE); u16(1); u16(8);
    for (char c : std::string("data")) w.push_back(static_cast<uint8_t>(c));
    u32(NSAMP);
    for (uint32_t i = 0; i < NSAMP; ++i) w.push_back((i / 20) % 2 ? 0xC0 : 0x40);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size()));
    return static_cast<bool>(f);
}

// Read one byte of an arbitrary physical 8K page through the same
// temp-slot-7 mapping the loader writes through.
uint8_t read_page(Mmu& mmu, uint8_t page, size_t off) {
    const uint8_t saved = mmu.get_page(7);
    mmu.set_page(7, page);
    const uint8_t v = mmu.read(static_cast<uint16_t>(0xE000 + off));
    mmu.set_page(7, saved);
    return v;
}

// Registers seeded before apply() so "the launch decision left them
// alone" is distinguishable from "the launch decision wrote them". Every
// value differs from both the header's and the reset branch's.
constexpr uint16_t kSeedPC = 0x1234;
constexpr uint16_t kSeedSP = 0x4321;
constexpr uint16_t kSeedBC = 0xBEEF;
constexpr uint16_t kHdrSP  = 0x58FF;   // same value the ped7g fixtures carry
constexpr uint16_t kHdrPC  = 0x8000;   // control entry point (bank 2 @ $8000)
constexpr uint8_t  kSeedP6 = 0x2A;     // pre-apply MMU slot 6/7 pages
constexpr uint8_t  kSeedP7 = 0x2B;

// A loaded emulator on which apply() was called DIRECTLY (no reset), with
// the registers and MMU slots 6/7 seeded first.
struct AppliedFixture {
    Emulator emu;
    bool     ok = false;

    AppliedFixture(uint16_t pc, uint8_t preserve_regs, uint8_t entry_bank, const char* tag) {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        Z80Registers regs = emu.cpu().get_registers();
        regs.PC = kSeedPC;
        regs.SP = kSeedSP;
        regs.BC = kSeedBC;
        emu.cpu().set_registers(regs);
        emu.mmu().set_page(6, kSeedP6);
        emu.mmu().set_page(7, kSeedP7);

        const std::string path = fixture_path(tag);
        if (!write_nex_bank_fixture(path, pc, kHdrSP, preserve_regs, entry_bank, 2, {}))
            return;

        NexLoader loader;
        if (loader.load(path)) ok = loader.apply(emu);
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// Three rows per 12288-byte split screen format.
//
// `with_palette` must be set for SCREEN_LORES: with NO_PAL clear, a LoRes
// NEX is REQUIRED to carry a 512-byte palette block ahead of the screen
// (nexload.asm:426-427), so a fixture without one is not a well-formed
// file and the loader now rejects it as truncated. The screen assertions
// below are unaffected — they index the screen payload, not the file.
void test_split_screen(const char* tag, uint8_t screen_flag,
                       const char* id_first, const char* id_second, const char* id_hole,
                       const char* asm_first, const char* asm_second,
                       bool with_palette = false) {
    constexpr size_t HALF = 6144;
    LoadedFixture f(screen_flag, 2 * HALF, tag, with_palette);
    if (!f.ok) {
        check(id_first,  "NEX fixture failed to load/apply", false, "load+apply returned false");
        check(id_second, "NEX fixture failed to load/apply", false, "load+apply returned false");
        check(id_hole,   "NEX fixture failed to load/apply", false, "load+apply returned false");
        return;
    }
    Mmu& mmu = f.emu.mmu();

    size_t bad = 0; uint8_t got = 0, want = 0;

    bool first_ok = bank5_matches_payload(mmu, 0x0000, 0, HALF, bad, got, want);
    check(id_first,
          "first 6144 bytes of the screen block land at bank-5 0x0000-0x17FF "
          "(nexload.asm ld ix,$4000 : ld bc,$1800 : call fread)",
          first_ok,
          fmt("%s: bank5[0x%04zX]=0x%02X want 0x%02X", asm_first, bad, got, want));

    bool second_ok = bank5_matches_payload(mmu, 0x2000, HALF, HALF, bad, got, want);
    check(id_second,
          "second 6144 bytes land at bank-5 0x2000-0x37FF, NOT at 0x1800 "
          "(nexload.asm ld ix,$6000 : ld bc,$1800 : call fread) — issue #68",
          second_ok,
          fmt("%s: bank5[0x%04zX]=0x%02X want 0x%02X", asm_second, bad, got, want));

    bool hole_ok = bank5_hole_untouched(mmu, bad, got);
    check(id_hole,
          "the 2048-byte hole at bank-5 0x1800-0x1FFF ($5800-$5FFF) receives no "
          "screen byte — the attribute area lores.vhd:93-94 skips (issue #68)",
          hole_ok,
          fmt("bank5[0x%04zX]=0x%02X want 0x00 (screen data leaked into the hole)", bad, got));
}

// ── GH #166: HEADER_DONTRESETNEXTREGS gating ─────────────────────────
//
// Oracle A, <= V1.2 — nexload.asm:323
//
//     ld a,($c000+HEADER_DONTRESETNEXTREGS):or a:jp nz,.dontresetregs
//
// jumps to the `.dontresetregs` label at :422, so the block the flag
// skips is EXACTLY :324-:408 — ";Reset All registers" through
// `NEXTREG_nn MMU_REGISTER_1,255` (:409 is the `jr .dontresetregs`
// that closes it).
//
// The boundary is the whole point of these rows. Four NR writes that
// `NexLoader::apply()` makes have their oracle in nexload's
// UNCONDITIONAL prologue at :266-:274, ABOVE the gate, and so must
// still happen when the flag is set:
//
//     :266  NEXTREG_nn 66,15                   → NR 0x42 = 0x0F
//     :267  NEXTREG_nn PALETTE_CONTROL,0       → NR 0x43 = 0x00
//     :269  NEXTREG_nn PALETTE_INDEX,$18       → NR 0x40 = 0x18
//     :270  NEXTREG_nn PALETTE_VALUE,$e3       → NR 0x41 = 0xE3
//     :274  NEXTREG_nn SPRITE_CONTROL,SLU+VIS  → NR 0x15 = 0x01
//
// (NR 0x42 / 0x43 / 0x15 are ALSO written inside the gated block at
// :395 / :394 / :370, but the prologue copy is unconditional, so the
// net effect of the pair is unconditional. The ULA palette entry 0x18
// pair at :269-:270 is written NOWHERE else — which is why it is the
// wrong register to probe the gate with, despite being the obvious
// candidate, and why NEXORD-06 above depends on it running.)
//
// Oracle B, V1.3 — nexload2.asm:781-783, inside
// `setupBeforeBlockLoading`:
//
//     ld a,(nexHeader.PRESERVENEXTREG)
//     or a
//     ret nz    ; "next regs should be preserved, only 28MHz is set, keep others"
//
// The two loaders disagree on exactly one register jnext writes:
// NR 0x07 (28 MHz turbo) is INSIDE nexload.asm's gate (:365) but ABOVE
// nexload2's (:777). A V1.3 file can only be loaded by nexload2 — the
// distro loader cannot parse it — so jnext follows whichever loader
// would really handle the file. NEXPR-V13-01/02 pin that.
//
// Each register is set to a SENTINEL that differs both from jnext's
// power-on value and from the value the reset block writes, so the two
// branches are distinguishable in every row.

// Registers probed, with their sentinel and the value the gated reset
// block installs. EVERY sentinel differs from BOTH the emulator's
// power-on value and the value the reset block writes, so no row can
// pass because a sentinel write silently did nothing.
//
// NR 0x07 reads back as (actual << 4) | requested (emulator.cpp:1449),
// so the sentinel 0x02 (14 MHz) reads 0x22 and the reset 0x03 reads
// 0x33 — and neither is the power-on 0x00.
constexpr uint8_t kSentinel07 = 0x02, kKeep07 = 0x22, kReset07 = 0x33;
constexpr uint8_t kSentinel12 = 0x0A, kReset12 = 0x09;  // nexload.asm:367
constexpr uint8_t kSentinel13 = 0x0E, kReset13 = 0x0C;  // nexload.asm:368
constexpr uint8_t kSentinel14 = 0x5A, kReset14 = 0xE3;  // nexload.asm:369
constexpr uint8_t kSentinel16 = 0x77, kReset16 = 0x00;  // nexload.asm:371
constexpr uint8_t kSentinel4B = 0x2C, kReset4B = 0xE3;  // nexload.asm:405
// Always-run prologue registers (:266-:274).
constexpr uint8_t kSentinel42 = 0x3F, kAlways42 = 0x0F;  // nexload.asm:266
constexpr uint8_t kSentinel43 = 0x10, kAlways43 = 0x00;  // nexload.asm:267-268
constexpr uint8_t kSentinel15 = 0x84, kAlways15 = 0x01;  // nexload.asm:274
constexpr uint8_t kSentinelPal18 = 0x99, kAlwaysPal18 = 0xE3;  // nexload.asm:269-270
// NR 0x40 (palette index) after the load.
//
// The sentinel is deliberately NOT 0x19. Writing the pal[0x18] sentinel
// through NR 0x41 auto-increments the index to 0x19 all by itself, so a
// fixture that stopped there would leave the index at exactly the value
// the flag-set branch is supposed to prove the PRODUCTION :269-:270
// pair put it at — and NEXPR-KEEP-07 would pass with that pair deleted.
// It did, and a reviewer caught it. Stamping a third, unrelated value
// over the index breaks that shared side effect, so KEEP-07 now fails
// both when the :269-:270 pair is removed and when the trailing
// NR 0x40 = 0 is un-gated.
constexpr uint8_t kSentinel40 = 0x7E;
constexpr uint8_t kReset40 = 0x00, kKeep40 = 0x19;

// A minimal, screen-less, bank-less NEX carrying `preserve_regs` at
// header offset 134. No screen block means nothing downstream of the
// NextREG init can write a NextREG and mask the probe — in particular
// none of the set_loading_screen_display() paths run.
bool write_preserve_nex(const std::string& path, uint8_t preserve_regs, const char* version) {
    std::vector<uint8_t> file(512, 0x00);

    std::memcpy(file.data() + 0, "Next", 4);
    std::memcpy(file.data() + 4, version, 4);
    file[8]  = 0;     // ram_required: 768 KB
    file[9]  = 0;     // num_banks
    file[10] = 0;     // screen_flags: none (so no SCREEN_EXT2 / screen_flags2)
    file[11] = 0;     // border colour
    file[12] = 0x00; file[13] = 0xFF;  // SP = 0xFF00
    file[14] = 0x00; file[15] = 0x80;  // PC = 0x8000
    file[130] = 0;    // loading_bar off
    file[132] = 0;    // loading_delay
    file[133] = 0;    // start_delay
    file[134] = preserve_regs;
    // V1.3 fields (142-158, 508) stay zero: no copper block, no checksum,
    // banks_offset 0 (derived), screen_flags2 unused because bit 6 is clear.

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(f);
}

// Boot an emulator, stamp every probe register with its sentinel (this
// stands in for the machine state a launcher left behind), then run the
// real load() + apply().
struct PreserveFixture {
    Emulator emu;
    bool     ok = false;

    PreserveFixture(uint8_t preserve_regs, const char* tag, const char* version = "V1.2") {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        NextReg& nr = emu.nextreg();
        // ULA palette entry 0x18 first: it needs NR 0x43 pointing at the
        // ULA first palette, and NR 0x43's own sentinel is set last.
        nr.write(0x43, 0x00);
        nr.write(0x40, 0x18);
        nr.write(0x41, kSentinelPal18);
        // Then stamp the index with a value the pal[0x18] write cannot
        // have produced — see kSentinel40.
        nr.write(0x40, kSentinel40);

        nr.write(0x07, kSentinel07);
        nr.write(0x12, kSentinel12);
        nr.write(0x13, kSentinel13);
        nr.write(0x14, kSentinel14);
        nr.write(0x15, kSentinel15);
        nr.write(0x16, kSentinel16);
        nr.write(0x42, kSentinel42);
        nr.write(0x4B, kSentinel4B);
        nr.write(0x43, kSentinel43);

        const std::string path = fixture_path((std::string("preserve_") + tag).c_str());

        if (!write_preserve_nex(path, preserve_regs, version)) return;

        NexLoader loader;
        if (loader.load(path)) {
            ok = loader.apply(emu);
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    // ULA first-palette entry 0x18, as the 8-bit RRRGGGBB the loader
    // wrote. Reading it through NR 0x41 needs NR 0x43 / NR 0x40 pointed
    // at it, so this MUTATES both — every caller reads those two
    // registers' own rows first.
    uint8_t ula_palette_18() {
        NextReg& nr = emu.nextreg();
        nr.write(0x43, 0x00);
        nr.write(0x40, 0x18);
        return nr.read(0x41);   // pure read: does not advance the index
    }
};

void check_nr(const char* id, PreserveFixture& f, uint8_t reg, uint8_t want,
              const char* desc, const char* asm_cite) {
    const uint8_t got = f.emu.nextreg().read(reg);
    check(id, desc, got == want,
          fmt("%s: NR 0x%02X = 0x%02X, want 0x%02X", asm_cite, reg, got, want));
}

void fail_all(std::initializer_list<const char*> ids) {
    for (const char* id : ids) {
        check(id, "NEX fixture failed to load/apply", false, "load+apply returned false");
    }
}

void test_preserve_nextregs() {
    // ── preserve_regs = 0 → nexload.asm:324-408 RUNS ─────────────────
    {
        PreserveFixture f(0, "reset");
        if (!f.ok) {
            fail_all({"NEXPR-RESET-01", "NEXPR-RESET-02", "NEXPR-RESET-03", "NEXPR-RESET-04",
                      "NEXPR-RESET-05", "NEXPR-RESET-06", "NEXPR-RESET-07", "NEXPR-ALWAYS-05",
                      "NEXPR-ALWAYS-06", "NEXPR-ALWAYS-07", "NEXPR-ALWAYS-08"});
        } else {
            check_nr("NEXPR-RESET-01", f, 0x07, kReset07,
                     "DONTRESETNEXTREGS=0: NR 0x07 is reset to turbo 28 MHz",
                     "nexload.asm:365");
            check_nr("NEXPR-RESET-02", f, 0x12, kReset12,
                     "DONTRESETNEXTREGS=0: NR 0x12 is reset to Layer 2 bank 9",
                     "nexload.asm:367");
            check_nr("NEXPR-RESET-03", f, 0x13, kReset13,
                     "DONTRESETNEXTREGS=0: NR 0x13 is reset to Layer 2 shadow bank 12",
                     "nexload.asm:368");
            check_nr("NEXPR-RESET-04", f, 0x14, kReset14,
                     "DONTRESETNEXTREGS=0: NR 0x14 is reset to transparent index 0xE3",
                     "nexload.asm:369");
            check_nr("NEXPR-RESET-05", f, 0x16, kReset16,
                     "DONTRESETNEXTREGS=0: NR 0x16 Layer 2 X-scroll is reset to 0",
                     "nexload.asm:371");
            check_nr("NEXPR-RESET-06", f, 0x4B, kReset4B,
                     "DONTRESETNEXTREGS=0: NR 0x4B sprite transparency index is reset to 0xE3",
                     "nexload.asm:405");
            check_nr("NEXPR-RESET-07", f, 0x40, kReset40,
                     "DONTRESETNEXTREGS=0: the palette index ends at 0 (the :396 / :411 "
                     "`.pa` sweeps inside the gated block leave it there)",
                     "nexload.asm:396");

            // The :266-:274 prologue runs in this branch too — control
            // rows for the ALWAYS group below, so a future over-eager
            // gate cannot pass by breaking only one branch.
            check_nr("NEXPR-ALWAYS-05", f, 0x42, kAlways42,
                     "DONTRESETNEXTREGS=0: NR 0x42 ULANext format = 0x0F",
                     "nexload.asm:266");
            check_nr("NEXPR-ALWAYS-06", f, 0x43, kAlways43,
                     "DONTRESETNEXTREGS=0: NR 0x43 selects the ULA first palette",
                     "nexload.asm:267");
            check_nr("NEXPR-ALWAYS-07", f, 0x15, kAlways15,
                     "DONTRESETNEXTREGS=0: NR 0x15 = SLU priority + sprites visible",
                     "nexload.asm:274");
            const uint8_t pal = f.ula_palette_18();   // last: mutates NR 0x43 / 0x40
            check("NEXPR-ALWAYS-08",
                  "DONTRESETNEXTREGS=0: ULA palette entry 0x18 = 0xE3",
                  pal == kAlwaysPal18,
                  fmt("nexload.asm:269-270: ULA pal[0x18] = 0x%02X, want 0x%02X",
                      pal, kAlwaysPal18));
        }
    }

    // ── preserve_regs = 1 → nexload.asm:324-408 is SKIPPED ───────────
    {
        PreserveFixture f(1, "keep");
        if (!f.ok) {
            fail_all({"NEXPR-KEEP-01", "NEXPR-KEEP-02", "NEXPR-KEEP-03", "NEXPR-KEEP-04",
                      "NEXPR-KEEP-05", "NEXPR-KEEP-06", "NEXPR-KEEP-07", "NEXPR-ALWAYS-01",
                      "NEXPR-ALWAYS-02", "NEXPR-ALWAYS-03", "NEXPR-ALWAYS-04"});
        } else {
            check_nr("NEXPR-KEEP-01", f, 0x07, kKeep07,
                     "DONTRESETNEXTREGS=1 on a <= V1.2 file: NR 0x07 CPU speed survives the "
                     "load — nexload.asm writes it INSIDE the gated block (GH #166)",
                     "nexload.asm:365");
            check_nr("NEXPR-KEEP-02", f, 0x12, kSentinel12,
                     "DONTRESETNEXTREGS=1: NR 0x12 Layer 2 bank survives the load (GH #166)",
                     "nexload.asm:323");
            check_nr("NEXPR-KEEP-03", f, 0x13, kSentinel13,
                     "DONTRESETNEXTREGS=1: NR 0x13 Layer 2 shadow bank survives (GH #166)",
                     "nexload.asm:323");
            check_nr("NEXPR-KEEP-04", f, 0x14, kSentinel14,
                     "DONTRESETNEXTREGS=1: NR 0x14 transparency colour survives (GH #166)",
                     "nexload.asm:323");
            check_nr("NEXPR-KEEP-05", f, 0x16, kSentinel16,
                     "DONTRESETNEXTREGS=1: NR 0x16 Layer 2 X-scroll survives (GH #166)",
                     "nexload.asm:323");
            check_nr("NEXPR-KEEP-06", f, 0x4B, kSentinel4B,
                     "DONTRESETNEXTREGS=1: NR 0x4B sprite transparency survives (GH #166)",
                     "nexload.asm:323");
            check_nr("NEXPR-KEEP-07", f, 0x40, kKeep40,
                     "DONTRESETNEXTREGS=1: the palette index is 0x18+1 — the auto-increment "
                     "of the UNCONDITIONAL :270 NR 0x41 write, over the fixture's own 0x7E "
                     "index sentinel, with nothing past `.dontresetregs` resetting it",
                     "nexload.asm:422");

            // The gate must NOT swallow nexload's unconditional
            // :266-:274 prologue. These four fail if the whole init
            // block is gated — the naive reading of the fix.
            check_nr("NEXPR-ALWAYS-01", f, 0x42, kAlways42,
                     "DONTRESETNEXTREGS=1: NR 0x42 is STILL written — its oracle is above "
                     "the gate",
                     "nexload.asm:266");
            check_nr("NEXPR-ALWAYS-02", f, 0x43, kAlways43,
                     "DONTRESETNEXTREGS=1: NR 0x43 is STILL written — its oracle is above "
                     "the gate",
                     "nexload.asm:267");
            check_nr("NEXPR-ALWAYS-03", f, 0x15, kAlways15,
                     "DONTRESETNEXTREGS=1: NR 0x15 is STILL written — its oracle is above "
                     "the gate",
                     "nexload.asm:274");
            const uint8_t pal = f.ula_palette_18();   // last: mutates NR 0x43 / 0x40
            check("NEXPR-ALWAYS-04",
                  "DONTRESETNEXTREGS=1: ULA palette entry 0x18 is STILL set to 0xE3 — "
                  "its oracle is above the gate and appears nowhere else",
                  pal == kAlwaysPal18,
                  fmt("nexload.asm:269-270: ULA pal[0x18] = 0x%02X, want 0x%02X",
                      pal, kAlwaysPal18));
        }
    }

    // ── V1.3 + PRESERVENEXTREG=1: the one register the two loaders
    //    disagree about (nexload2.asm:777 vs nexload.asm:365) ─────────
    {
        PreserveFixture f(1, "keep_v13", "V1.3");
        if (!f.ok) {
            fail_all({"NEXPR-V13-01", "NEXPR-V13-02"});
        } else {
            check_nr("NEXPR-V13-01", f, 0x07, kReset07,
                     "PRESERVENEXTREG=1 on a V1.3 file STILL gets 28 MHz — nexload2 sets "
                     "NR 0x07 before its early return, and only a V1.3-capable loader can "
                     "load the file (GH #166)",
                     "nexload2.asm:777");
            check_nr("NEXPR-V13-02", f, 0x14, kSentinel14,
                     "PRESERVENEXTREG=1 on a V1.3 file still preserves everything else — "
                     "only NR 0x07 is exempt, not the whole block",
                     "nexload2.asm:781-783");
        }
    }
}

} // namespace

int main() {
    std::printf("NEX loader screen-ingest tests (issue #68)\n\n");

    set_group("NEXSCR");

    // NEXSCR-LORES-01/02/03 — nexload.asm:472,:474
    test_split_screen("lores", NexHeader::SCREEN_LORES,
                      "NEXSCR-LORES-01", "NEXSCR-LORES-02", "NEXSCR-LORES-03",
                      "nexload.asm:472", "nexload.asm:474",
                      /*with_palette=*/true);   // LoRes without NO_PAL: palette is mandatory

    // NEXSCR-HIRES-01/02/03 — nexload.asm:488,:490
    test_split_screen("hires", NexHeader::SCREEN_HIRES,
                      "NEXSCR-HIRES-01", "NEXSCR-HIRES-02", "NEXSCR-HIRES-03",
                      "nexload.asm:488", "nexload.asm:490");

    // NEXSCR-HICOL-01/02/03 — nexload.asm:505,:507
    test_split_screen("hicol", NexHeader::SCREEN_HICOLOUR,
                      "NEXSCR-HICOL-01", "NEXSCR-HICOL-02", "NEXSCR-HICOL-03",
                      "nexload.asm:505", "nexload.asm:507");

    // NEXSCR-ULA-01 — control row. The classic 6912-byte ULA screen is a
    // SINGLE contiguous fread (nexload.asm:457 `ld ix,$4000 : ld bc,$1b00`),
    // so it must NOT be split: bytes 0x1800-0x1AFF are the ULA attribute
    // block and belong at bank-5 0x1800, exactly where the split formats
    // leave a hole. This row fails if the split is over-generalised.
    {
        constexpr size_t ULA_SIZE = 6912;
        LoadedFixture f(NexHeader::SCREEN_ULA, ULA_SIZE, "ula");
        if (!f.ok) {
            check("NEXSCR-ULA-01", "NEX fixture failed to load/apply", false,
                  "load+apply returned false");
        } else {
            size_t bad = 0; uint8_t got = 0, want = 0;
            bool ok = bank5_matches_payload(f.emu.mmu(), 0x0000, 0, ULA_SIZE, bad, got, want);
            check("NEXSCR-ULA-01",
                  "SCREEN_ULA stays one contiguous 6912-byte run at bank-5 "
                  "0x0000-0x1AFF, attributes included (nexload.asm:457)",
                  ok,
                  fmt("nexload.asm:457: bank5[0x%04zX]=0x%02X want 0x%02X", bad, got, want));
        }
    }

    // =================================================================
    // NEXPAL — the 512-byte loading-screen palette block (issue #156)
    //
    // nexload.asm:426-427 gates the palette read on the screen KIND, not
    // on Layer 2 alone:
    //
    //   ld a,(IsLoadingScr):and 128:jr nz,.skppal      ; NO_PAL   → skip
    //   ld a,(IsLoadingScr):and %11010:jr nz,.skppal   ; ULA|HIRES|HICOL → skip
    //   ld ix,$c200:ld bc,$200:call fread              ; else 512 bytes
    //
    // so a LoRes screen carries one too. jnext counted it for Layer 2
    // only, so every LoRes-with-palette NEX had its screen AND its banks
    // read 512 bytes early. The discriminating real-world evidence is the
    // size of ped7g's own conformance pair (github.com/ped7g/
    // ZXSpectrumNextMisc, nexload2/):
    //
    //   tmLoRes.nex      29696 = 512 hdr + 512 pal + 12288 LoRes + 16384 bank
    //   tmLoResNoPal.nex 29184 = 512 hdr +   0     + 12288 LoRes + 16384 bank
    //
    // The 512-byte delta between two otherwise identical files IS the
    // palette block, and it is present exactly when NO_PAL is clear.
    // =================================================================

    set_group("NEXPAL");

    struct RegionCase {
        const char* id;
        const char* tag;
        uint8_t     flags;
        size_t      screen_bytes;
        bool        with_palette;
        uint64_t    expect_offset;
        const char* desc;
    };

    // payload_offset() is the first byte after the header-described
    // region, i.e. 512 + palette + screen (these fixtures declare no
    // banks), so it reads out the loader's palette accounting directly.
    static const RegionCase kRegionCases[] = {
        {"NEXPAL-01", "palregion_lores", NexHeader::SCREEN_LORES, 12288, true, 512 + 512 + 12288,
         "LoRes screen WITHOUT NO_PAL carries a 512-byte palette block "
         "(nexload.asm:426-427; tmLoRes.nex 29696 vs tmLoResNoPal.nex 29184)"},
        {"NEXPAL-02", "palregion_loresnp",
         static_cast<uint8_t>(NexHeader::SCREEN_LORES | NexHeader::SCREEN_NO_PAL), 12288, false,
         512 + 12288,
         "LoRes screen WITH NO_PAL (bit 7) carries no palette block "
         "(nexload.asm:426 `and 128:jr nz,.skppal`)"},
        {"NEXPAL-03", "palregion_l2", NexHeader::SCREEN_LAYER2, 49152, true, 512 + 512 + 49152,
         "Layer 2 screen without NO_PAL still carries its palette block "
         "(control row — behaviour predates issue #156)"},
        {"NEXPAL-04", "palregion_ula", NexHeader::SCREEN_ULA, 6912, false, 512 + 6912,
         "a ULA screen NEVER carries a palette, NO_PAL clear or not "
         "(nexload.asm:427 `and %11010` — bit 1 is in the mask)"},
        {"NEXPAL-05", "palregion_hires", NexHeader::SCREEN_HIRES, 12288, false, 512 + 12288,
         "a Timex HiRes screen NEVER carries a palette (nexload.asm:427, bit 3 in %11010)"},
        {"NEXPAL-06", "palregion_hicol", NexHeader::SCREEN_HICOLOUR, 12288, false, 512 + 12288,
         "a Timex HiColour screen NEVER carries a palette (nexload.asm:427, bit 4 in %11010)"},
    };

    for (const RegionCase& c : kRegionCases) {
        LoadedFixture f(c.flags, c.screen_bytes, c.tag, c.with_palette);
        if (!f.ok) {
            check(c.id, c.desc, false, "load+apply returned false");
            continue;
        }
        check(c.id, c.desc, f.payload_offset == c.expect_offset,
              fmt("payload_offset=%llu want %llu",
                  static_cast<unsigned long long>(f.payload_offset),
                  static_cast<unsigned long long>(c.expect_offset)));
    }

    // NEXPAL-07 — the content check that a size-only test cannot make: with
    // the palette counted, the LoRes screen data starts AFTER it, so bank-5
    // 0x0000 holds payload_byte(0), not pal_lo(0). Pre-fix this row read the
    // palette bytes as screen pixels.
    {
        LoadedFixture f(NexHeader::SCREEN_LORES, 12288, "palskip_lores", /*with_palette=*/true);
        if (!f.ok) {
            check("NEXPAL-07", "NEX fixture failed to load/apply", false,
                  "load+apply returned false");
        } else {
            size_t bad = 0; uint8_t got = 0, want = 0;
            const bool ok = bank5_matches_payload(f.emu.mmu(), 0x0000, 0, 6144, bad, got, want);
            check("NEXPAL-07",
                  "LoRes screen data is read AFTER the 512-byte palette block, so bank-5 "
                  "0x0000 holds screen byte 0 and not palette byte 0 (nexload.asm:428,:472)",
                  ok,
                  fmt("bank5[0x%04zX]=0x%02X want 0x%02X (pal_lo(0)=0x%02X — a match here means "
                      "the palette was ingested as pixels)", bad, got, want, pal_lo(0)));
        }
    }

    // NEXPAL-08/09 — WHICH palette bank the block lands in. nexload.asm:429-433:
    //   ld a,(IsLoadingScr):and 4:jr z,.nlores
    //   NEXTREG_nn PALETTE_CONTROL_REGISTER,%00000001 : jr .nl2   ; LoRes → ULA palette
    // .nlores
    //   NEXTREG_nn PALETTE_CONTROL_REGISTER,%00010000             ; else  → Layer 2 palette
    // LoRes reads the ULA palette, so its loading-screen palette must go there.
    {
        LoadedFixture f(NexHeader::SCREEN_LORES, 12288, "paltarget_lores", /*with_palette=*/true);
        if (!f.ok) {
            check("NEXPAL-08", "NEX fixture failed to load/apply", false, "load+apply returned false");
            check("NEXPAL-09", "NEX fixture failed to load/apply", false, "load+apply returned false");
        } else {
            auto& pal = f.emu.palette();
            int bad_idx = -1;
            for (int i = 0; i < 256; ++i) {
                if (pal.ula_rgb333(false, static_cast<uint8_t>(i)) != pal_expected_rgb333(i)) {
                    bad_idx = i;
                    break;
                }
            }
            check("NEXPAL-08",
                  "a LoRes loading-screen palette lands in the ULA-first palette, all 256 "
                  "entries (nexload.asm:430 NR 0x43 = %00000001)",
                  bad_idx < 0,
                  bad_idx < 0 ? std::string{}
                              : fmt("ULA pal[%d]=0x%03X want 0x%03X", bad_idx,
                                    pal.ula_rgb333(false, static_cast<uint8_t>(bad_idx)),
                                    pal_expected_rgb333(bad_idx)));

            // Discriminator: if the target selection is wrong the same bytes
            // land in the Layer 2 palette instead. Entry 1 is chosen because
            // pal_lo(1)=0xAF is not a Layer 2 default value.
            const uint8_t l2_entry1 = pal.layer2_rgb8(1);
            const uint8_t leaked    = pal_lo(1);
            check("NEXPAL-09",
                  "a LoRes loading-screen palette does NOT touch the Layer 2 palette "
                  "(nexload.asm:430 selects NR 0x43 = %00000001, not %00010000)",
                  l2_entry1 != leaked,
                  fmt("Layer2 pal[1]=0x%02X == pal_lo(1)=0x%02X — the LoRes palette leaked "
                      "into the Layer 2 bank", l2_entry1, leaked));
        }
    }

    // NEXPAL-10 — the Layer 2 side of the same selector (control row).
    {
        LoadedFixture f(NexHeader::SCREEN_LAYER2, 49152, "paltarget_l2", /*with_palette=*/true);
        if (!f.ok) {
            check("NEXPAL-10", "NEX fixture failed to load/apply", false, "load+apply returned false");
        } else {
            auto& pal = f.emu.palette();
            int bad_idx = -1;
            for (int i = 0; i < 256; ++i) {
                // layer2_rgb8() returns the 8-bit RRRGGGBB form of the stored
                // 9-bit entry, which is exactly the first of the two NR 0x44
                // bytes the file supplies.
                if (pal.layer2_rgb8(static_cast<uint8_t>(i)) != pal_lo(i)) {
                    bad_idx = i;
                    break;
                }
            }
            check("NEXPAL-10",
                  "a Layer 2 loading-screen palette lands in the Layer2-first palette "
                  "(nexload.asm:432 NR 0x43 = %00010000)",
                  bad_idx < 0,
                  bad_idx < 0 ? std::string{}
                              : fmt("Layer2 pal[%d]=0x%02X want 0x%02X", bad_idx,
                                    pal.layer2_rgb8(static_cast<uint8_t>(bad_idx)),
                                    pal_lo(bad_idx)));
        }
    }

    // =================================================================
    // NEXDISP — the display-mode writes that end every loading-screen
    // block. Loading the pixels is only half of what nexload.asm does;
    // without these three writes the screen sits in RAM invisible, which
    // is what jnext did for every non-ULA kind (issue #156: tmHiCol,
    // tmHiRes, tmLoRes and tmLoResNoPal all rendered a black screen).
    // =================================================================

    set_group("NEXDISP");

    struct DisplayCase {
        const char*   id;
        NexScreenKind kind;
        uint8_t       hires_colour;
        uint8_t       want_123b;
        uint8_t       want_nr15;
        uint8_t       want_ff;
        const char*   desc;
    };

    static const DisplayCase kDisplayCases[] = {
        {"NEXDISP-L2-01", NexScreenKind::LAYER2, 0x00, 0x02, 0x01, 0x00,
         "Layer 2 screen: port $123B=2 (L2 visible), NR $15=SLU|SPRITES, Timex mode 0 "
         "(nexload.asm:444-446)"},
        {"NEXDISP-ULA-01", NexScreenKind::ULA, 0x00, 0x00, 0x01, 0x00,
         "ULA screen: port $123B=0 (L2 off), NR $15=SLU|SPRITES, Timex mode 0 "
         "(nexload.asm:459-461)"},
        {"NEXDISP-LORES-01", NexScreenKind::LORES, 0x00, 0x00, 0x81, 0x03,
         "LoRes screen: NR $15 bit 7 LORES_ENABLE set (nexload.asm:145,:476) and "
         "Timex mode 3 (nexload.asm:477)"},
        {"NEXDISP-HIRES-01", NexScreenKind::HIRES, 0x00, 0x00, 0x01, 0x06,
         "Timex HiRes screen: port $FF = 6 when HIRESCOL is 0 (nexload.asm:493)"},
        {"NEXDISP-HIRES-02", NexScreenKind::HIRES, 0xFF, 0x00, 0x01, 0x3E,
         "Timex HiRes ink colour comes from HIRESCOL bits 5:3 only: "
         "0xFF -> (0xFF & %111000) | 6 = 0x3E (nexload.asm:493 `and %111000:or 6`)"},
        {"NEXDISP-HICOL-01", NexScreenKind::HICOLOUR, 0x00, 0x00, 0x01, 0x02,
         "Timex HiColour screen: port $FF = 2 (nexload.asm:510)"},
    };

    for (const DisplayCase& c : kDisplayCases) {
        const NexScreenDisplay d = nex_loading_screen_display(c.kind, c.hires_colour);
        check(c.id, c.desc,
              d.port_123b == c.want_123b && d.nr_15 == c.want_nr15 && d.port_ff == c.want_ff,
              fmt("got {$123B=0x%02X NR$15=0x%02X $FF=0x%02X} want {0x%02X 0x%02X 0x%02X}",
                  d.port_123b, d.nr_15, d.port_ff, c.want_123b, c.want_nr15, c.want_ff));
    }

    // =================================================================
    // NEXORD — end-to-end: the writes above must actually reach the
    // machine, and must SURVIVE the NextREG-init block.
    //
    // nexload.asm resets the registers FIRST (:321-410, the
    // `.dontresetregs` fallthrough, which includes `NEXTREG_nn 21,1`) and
    // runs the loading-screen block AFTER (:424-511), so the screen block
    // has the last word. jnext used to run them the other way round: the
    // init block's NR $15 = 0x01 wiped the LoRes enable bit, and its
    // NR $40/$41 pair (ULA palette entry 0x18 = 0xE3) overwrote one entry
    // of a LoRes screen's palette.
    // =================================================================

    set_group("NEXORD");

    // Port 0xFF readback: Ula::get_screen_mode_reg() is the raw byte last
    // written to port 0xFF (zxula.vhd:191 `screen_mode_s <= i_port_ff_reg(2:0)`).
    struct EndToEndCase {
        const char* id;
        const char* tag;
        uint8_t     flags;
        size_t      screen_bytes;
        bool        with_palette;
        uint8_t     want_nr15;
        uint8_t     want_ff;
        bool        want_l2_enabled;
        const char* desc;
    };

    static const EndToEndCase kEndToEnd[] = {
        {"NEXORD-01", "e2e_ula", NexHeader::SCREEN_ULA, 6912, false, 0x01, 0x00, false,
         "after a ULA loading screen the machine is in Timex mode 0 with NR $15 = 0x01 "
         "(nexload.asm:459-461)"},
        {"NEXORD-02", "e2e_lores", NexHeader::SCREEN_LORES, 12288, true, 0x81, 0x03, false,
         "after a LoRes loading screen NR $15 KEEPS bit 7 and Timex mode is 3 — the "
         "NextREG-init block must not run after the screen block (nexload.asm:476-477)"},
        {"NEXORD-03", "e2e_hires", NexHeader::SCREEN_HIRES, 12288, false, 0x01, 0x06, false,
         "after a HiRes loading screen the machine is in Timex mode 6 (nexload.asm:493)"},
        {"NEXORD-04", "e2e_hicol", NexHeader::SCREEN_HICOLOUR, 12288, false, 0x01, 0x02, false,
         "after a HiColour loading screen the machine is in Timex mode 2 (nexload.asm:510)"},
        {"NEXORD-05", "e2e_l2", NexHeader::SCREEN_LAYER2, 49152, true, 0x01, 0x00, true,
         "after a Layer 2 loading screen Layer 2 is VISIBLE — port $123B bit 1 "
         "(nexload.asm:444)"},
    };

    for (const EndToEndCase& c : kEndToEnd) {
        LoadedFixture f(c.flags, c.screen_bytes, c.tag, c.with_palette);
        if (!f.ok) {
            check(c.id, c.desc, false, "load+apply returned false");
            continue;
        }
        const uint8_t nr15 = f.emu.nextreg().read(0x15);
        const uint8_t ff   = f.emu.renderer().ula().get_screen_mode_reg();
        const bool    l2   = f.emu.layer2().enabled();
        check(c.id, c.desc,
              nr15 == c.want_nr15 && ff == c.want_ff && l2 == c.want_l2_enabled,
              fmt("got {NR$15=0x%02X $FF=0x%02X L2en=%d} want {0x%02X 0x%02X %d}",
                  nr15, ff, l2 ? 1 : 0, c.want_nr15, c.want_ff, c.want_l2_enabled ? 1 : 0));
    }

    // NEXORD-06 — the palette-clobber half of the same ordering bug. The
    // NextREG-init block writes ULA palette entry 0x18 = 0xE3; if it runs
    // AFTER the screen block it destroys entry 24 of a LoRes screen's
    // palette. nexload.asm writes that pair unconditionally at :269-270,
    // long before the screen block, and loads the screen
    // palette second (:428-438), so the FILE wins.
    {
        LoadedFixture f(NexHeader::SCREEN_LORES, 12288, "ord_palclobber", /*with_palette=*/true);
        if (!f.ok) {
            check("NEXORD-06", "NEX fixture failed to load/apply", false, "load+apply returned false");
        } else {
            const uint16_t got  = f.emu.palette().ula_rgb333(false, 0x18);
            const uint16_t want = pal_expected_rgb333(0x18);
            // 0xE3 is what the NextREG-init block writes to this entry.
            const uint16_t clobber = static_cast<uint16_t>(static_cast<uint16_t>(0xE3) << 1);
            check("NEXORD-06",
                  "ULA palette entry 0x18 holds the LoRes screen's own colour, not the 0xE3 "
                  "the NextREG-init block writes — init runs BEFORE the screen block "
                  "(nexload.asm:269-270 then :428-438)",
                  got == want,
                  fmt("ULA pal[0x18]=0x%03X want 0x%03X%s", got, want,
                      got == clobber ? " (== the NR-init 0xE3 write: ordering regressed)" : ""));
        }
    }

    // NEXORD-07 — control row: a NEX with NO loading screen must not touch
    // the display at all. nexload.asm:424 `ld a,(...LOADSCR):or a:jp z,.skpbmp`
    // jumps clean over the whole block, border write included. This row
    // fails if the display setup is hoisted out of the per-kind blocks.
    {
        LoadedFixture f(0x00, 0, "e2e_noscreen", /*with_palette=*/false);
        if (!f.ok) {
            check("NEXORD-07", "NEX fixture failed to load/apply", false, "load+apply returned false");
        } else {
            const uint8_t ff = f.emu.renderer().ula().get_screen_mode_reg();
            check("NEXORD-07",
                  "a NEX with screen_flags == 0 leaves Timex mode untouched — the whole "
                  "loading-screen block is skipped (nexload.asm:424)",
                  ff == 0x00 && !f.emu.layer2().enabled(),
                  fmt("$FF=0x%02X L2en=%d want 0x00 / 0", ff, f.emu.layer2().enabled() ? 1 : 0));
        }
    }

    // =================================================================
    // NEXVER — the loader-version gate (issue #156).
    //
    // nexload.asm:290-291 packs the header's version string to BCD and
    // refuses only a file from the FUTURE:
    //
    //   ld a,(V_MAJOR):sub '0':and %1111:SWAPNIB:ld b,a
    //   ld a,(V_MINOR):and %1111:or b:ld b,a
    //   ld a,(LoaderVersion):cp b:jp c,loaderUpdate
    //
    // `jp c` fires when LoaderVersion < file version. jnext used to warn
    // and load anyway, which silently mis-parses a newer layout — the
    // exact V1.3 failure mode #156 documents. ped7g ships
    // nexload2/loaderVersion.nex ("V9.9") to test precisely this gate.
    // =================================================================

    set_group("NEXVER");

    check("NEXVER-01",
          "\"V1.2\" packs to BCD 0x12 (nexload.asm:290 major<<4 | minor)",
          nex_version_bcd("V1.2") == 0x12,
          fmt("got 0x%02X want 0x12", nex_version_bcd("V1.2")));

    check("NEXVER-02",
          "\"V9.9\" packs to BCD 0x99 — the value nexload2/loaderVersion.nex carries",
          nex_version_bcd("V9.9") == 0x99,
          fmt("got 0x%02X want 0x99", nex_version_bcd("V9.9")));

    struct VersionCase {
        const char* id;
        const char* tag;
        const char  version[5];
        bool        want_load;
        const char* desc;
    };

    static const VersionCase kVersionCases[] = {
        {"NEXVER-03", "ver_v10", "V1.0", true,
         "V1.0 (older than the loader) loads — nexload.asm refuses only on carry, "
         "i.e. only when the loader is BEHIND the file"},
        {"NEXVER-04", "ver_v12", "V1.2", true,
         "V1.2 (exactly the supported version) loads — `cp` leaves NC on equality"},
        {"NEXVER-05", "ver_v13", "V1.3", true,
         "V1.3 LOADS: issue #162 decodes its three screen kinds and raised "
         "kNexLoaderVersionBcd to 0x13, so the gate no longer refuses it. ped7g's "
         "nexload2/`s p a c e.nex` — a V1.3 file with screen_flags=0x00 and no banks, "
         "briefly refused while the ceiling was 0x12 — loads again. The gate itself is "
         "still pinned by NEXVER-06/07 below; the V1.3 screen kinds have their own "
         "suite in nex_v13_test"},
        {"NEXVER-06", "ver_v99", "V9.9", false,
         "V9.9 is REFUSED — nexload2/loaderVersion.nex, the conformance file for this gate"},
        {"NEXVER-07", "ver_junk", "XXXX", false,
         "a malformed version string packs to a large BCD and is refused, which is the "
         "safe direction (it used to warn and load anyway)"},
    };

    for (const VersionCase& c : kVersionCases) {
        const std::string path =
            (std::filesystem::temp_directory_path() /
             (std::string("jnext_nexver_") + c.tag + ".nex")).string();
        // Header-only NEX: no screen, no banks, so a well-formed file is
        // exactly 512 bytes and nothing but the version gate can reject it.
        if (!write_nex_fixture(path, 0x00, 0)) {
            check(c.id, c.desc, false, "fixture write failed");
            continue;
        }
        {
            std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(4);
            f.write(c.version, 4);
        }
        NexLoader loader;
        const bool loaded = loader.load(path);
        std::error_code ec;
        std::filesystem::remove(path, ec);
        check(c.id, c.desc, loaded == c.want_load,
              fmt("version \"%s\" (BCD 0x%02X): load()=%d want %d",
                  c.version, nex_version_bcd(c.version), loaded ? 1 : 0, c.want_load ? 1 : 0));
    }

    // =================================================================
    // NEXCORE — the core-version requirement (nexload.asm:308-316).
    //
    // Field-ordered compare: major, then minor, then subminor, refusing
    // only when a field is strictly greater. NexLoader WARNS rather than
    // refuses (nexload.asm:311 `cp 8:jp z,.ok` skips the whole check on
    // an emulator, and jnext's advertised core version is nominal), so
    // these rows pin the comparison itself.
    // =================================================================

    set_group("NEXCORE");

    struct CoreCase {
        const char* id;
        uint8_t     maj, min, sub;
        bool        want_ok;
        const char* desc;
    };

    static const CoreCase kCoreCases[] = {
        {"NEXCORE-01", 3, 2, 3, true,
         "a requirement equal to the advertised core is met (nexload.asm:316 `jr z,.ok`)"},
        {"NEXCORE-02", 15, 15, 255, false,
         "15.15.255 is NOT met — the requirement nexload2/coreVersion.nex carries"},
        {"NEXCORE-03", 3, 3, 0, false,
         "equal major, greater minor is not met (nexload.asm:315, the `.o1` field)"},
        {"NEXCORE-04", 3, 2, 4, false,
         "equal major and minor, greater subminor is not met (nexload.asm:316)"},
        {"NEXCORE-05", 1, 15, 255, true,
         "a LOWER major wins outright even with higher minor/subminor — the compare is "
         "field-ordered, not a tuple sum (nexload.asm:314 `jr .ok`). This is the value "
         "nexload2/empty.nex and preserveNextRegs.nex actually carry"},
        {"NEXCORE-06", 3, 1, 5, true,
         "3.01.05 is met — the value ped7g's bigpic/A_drvHID/test8xN NEX files carry, so "
         "a stricter compare would refuse working corpus files"},
    };

    for (const CoreCase& c : kCoreCases) {
        const bool got = nex_core_version_ok(c.maj, c.min, c.sub);
        check(c.id, c.desc, got == c.want_ok,
              fmt("core %u.%02u.%02u vs advertised %u.%02u.%02u: ok=%d want %d",
                  c.maj, c.min, c.sub, kJnextCoreMajor, kJnextCoreMinor, kJnextCoreSubminor,
                  got ? 1 : 0, c.want_ok ? 1 : 0));
    }

    // NEXCORE-07 — the mirror must not drift. kJnextCore{Major,Minor,
    // Subminor} in nex_loader.h duplicate NR 0x01 / NR 0x0E, whose real
    // home is NextReg::reset() (src/port/nextreg.cpp:226,:293). Read them
    // off a live Emulator so a change in one place fails here rather than
    // silently making the warning lie.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);
        const uint8_t nr01 = emu.nextreg().read(0x01);
        const uint8_t nr0e = emu.nextreg().read(0x0E);
        const uint8_t want01 =
            static_cast<uint8_t>((kJnextCoreMajor << 4) | (kJnextCoreMinor & 0x0F));
        check("NEXCORE-07",
              "kJnextCore{Major,Minor,Subminor} still match the live NR 0x01 / NR 0x0E "
              "(nex_loader.h mirrors NextReg::reset(); this row is the anti-drift pin)",
              nr01 == want01 && nr0e == kJnextCoreSubminor,
              fmt("NR$01=0x%02X (want 0x%02X) NR$0E=0x%02X (want 0x%02X)",
                  nr01, want01, nr0e, kJnextCoreSubminor));
    }

    // =================================================================
    // NEXPR — HEADER_DONTRESETNEXTREGS gating (GH #166).
    //
    // nexload.asm:323 -> :422 for <= V1.2; nexload2.asm:781-783 for
    // V1.3. See the block comment above test_preserve_nextregs() for
    // the boundary derivation and why the two loaders disagree about
    // NR 0x07.
    // =================================================================

    set_group("NEXPR");
    test_preserve_nextregs();

    // ── GH #164 — header PC = 0 is "load only, do not execute" ────────

    set_group("NEXPC0");

    // NEXPC0-01/02 — reset branch (preserve_regs = 0).
    {
        AppliedFixture f(0x0000, /*preserve_regs=*/0, /*entry_bank=*/3, "reset_pc0");
        const Z80Registers r = f.emu.cpu().get_registers();
        check("NEXPC0-01",
              "PC=0, preserve_regs=0: the entry-state block is skipped — PC and SP "
              "keep their pre-load values and the header SP is NOT applied "
              "(nexload.asm:581 .returnToBasic, :597-600 ld sp,(oldStack))",
              f.ok && r.PC == kSeedPC && r.SP == kSeedSP,
              fmt("apply=%d PC=0x%04X want 0x%04X, SP=0x%04X want 0x%04X",
                  f.ok ? 1 : 0, r.PC, kSeedPC, r.SP, kSeedSP));
    }
    {
        AppliedFixture f(kHdrPC, /*preserve_regs=*/0, /*entry_bank=*/3, "reset_pcnz");
        const Z80Registers r = f.emu.cpu().get_registers();
        check("NEXPC0-02",
              "control: PC!=0, preserve_regs=0 still resets the entry state and takes "
              "PC/SP from the header (nexload.asm:580 — the non-zero branch)",
              f.ok && r.PC == kHdrPC && r.SP == kHdrSP && r.AF == 0xFFFF && r.IM == 1,
              fmt("apply=%d PC=0x%04X want 0x%04X, SP=0x%04X want 0x%04X, AF=0x%04X IM=%u",
                  f.ok ? 1 : 0, r.PC, kHdrPC, r.SP, kHdrSP, r.AF, r.IM));
    }

    // NEXPC0-03/04 — preserve branch (preserve_regs != 0).
    {
        AppliedFixture f(0x0000, /*preserve_regs=*/1, /*entry_bank=*/3, "pres_pc0");
        const Z80Registers r = f.emu.cpu().get_registers();
        check("NEXPC0-03",
              "PC=0, preserve_regs=1: PC and SP keep their pre-load values — the "
              "load-only path is taken in the register-preserving branch too "
              "(the oracle's launch decision is not version- or flag-specific)",
              f.ok && r.PC == kSeedPC && r.SP == kSeedSP,
              fmt("apply=%d PC=0x%04X want 0x%04X, SP=0x%04X want 0x%04X",
                  f.ok ? 1 : 0, r.PC, kSeedPC, r.SP, kSeedSP));
    }
    {
        AppliedFixture f(kHdrPC, /*preserve_regs=*/1, /*entry_bank=*/3, "pres_pcnz");
        const Z80Registers r = f.emu.cpu().get_registers();
        check("NEXPC0-04",
              "control: PC!=0, preserve_regs=1 takes PC/SP from the header while "
              "leaving the other registers alone",
              f.ok && r.PC == kHdrPC && r.SP == kHdrSP && r.BC == kSeedBC,
              fmt("apply=%d PC=0x%04X want 0x%04X, SP=0x%04X want 0x%04X, BC=0x%04X want 0x%04X",
                  f.ok ? 1 : 0, r.PC, kHdrPC, r.SP, kHdrSP, r.BC, kSeedBC));
    }

    // NEXPC0-05/06 — entry_bank mapping, reversed by .returnToBasicTidy.
    {
        AppliedFixture f(0x0000, /*preserve_regs=*/0, /*entry_bank=*/3, "eb_pc0");
        const uint8_t p6 = f.emu.mmu().get_page(6);
        const uint8_t p7 = f.emu.mmu().get_page(7);
        check("NEXPC0-05",
              "PC=0: entry_bank is NOT mapped to MMU slots 6/7 — $C000 keeps the "
              "mapping the load found, because .returnToBasicTidy remaps it from "
              "BANKM (nexload.asm:602-604; nexload2.asm:529-535)",
              f.ok && p6 == kSeedP6 && p7 == kSeedP7,
              fmt("apply=%d slot6=%u want %u, slot7=%u want %u",
                  f.ok ? 1 : 0, p6, kSeedP6, p7, kSeedP7));
    }
    {
        AppliedFixture f(kHdrPC, /*preserve_regs=*/0, /*entry_bank=*/3, "eb_pcnz");
        const uint8_t p6 = f.emu.mmu().get_page(6);
        const uint8_t p7 = f.emu.mmu().get_page(7);
        check("NEXPC0-06",
              "control: PC!=0 maps entry_bank 3 to MMU slots 6/7 (pages 6,7) — "
              "nexload.asm:555-558 .entryBankSMC",
              f.ok && p6 == 6 && p7 == 7,
              fmt("apply=%d slot6=%u want 6, slot7=%u want 7", f.ok ? 1 : 0, p6, p7));
    }

    // NEXPC0-07/08/09 — end-to-end through Emulator::load_nex(), which is
    // the real `--load` path (reset, then apply). These are the rows that
    // matter: the ped7g fixture tmNoStart.nex is byte-identical to
    // tmNoPic.nex except for this header field, and it loads bank 2
    // expecting it to survive. Before the fix, entering at $0000 ran the
    // ROM reset sequence and 16330 of those 16384 bytes were gone within
    // five frames (measured on the real binary via --delayed-snapshot).
    {
        constexpr int    FRAMES = 10;
        constexpr size_t BANK   = 16384;
        const std::string path = fixture_path("e2e_pc0");

        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        Emulator emu;
        const bool built = write_nex_bank_fixture(path, 0x0000, kHdrSP, 0, 0, 2, {});
        const bool ok    = built && emu.init(cfg) && emu.load_nex(path);

        const Z80Registers before = emu.cpu().get_registers();
        for (int i = 0; i < FRAMES; ++i) emu.run_frame();
        const Z80Registers after = emu.cpu().get_registers();

        size_t bad = BANK; uint8_t got = 0, want = 0;
        bool survived = ok;
        for (size_t i = 0; i < BANK && survived; ++i) {
            const uint8_t g = read_page(emu.mmu(), static_cast<uint8_t>(4 + i / 0x2000),
                                        i % 0x2000);
            const uint8_t w = payload_byte(i);
            if (g != w) { survived = false; bad = i; got = g; want = w; }
        }
        check("NEXPC0-07",
              "end-to-end: a PC=0 NEX's bank 2 is byte-identical after 10 frames — "
              "nothing the file did not ask for ran over it (GH #164, tmNoStart.nex)",
              survived,
              fmt("load=%d bank2[0x%04zX]=0x%02X want 0x%02X", ok ? 1 : 0, bad, got, want));

        check("NEXPC0-08",
              "end-to-end: no instruction is fetched over those 10 frames — R and PC "
              "are unchanged, so the CPU is parked and not merely pointed elsewhere",
              ok && after.R == before.R && after.PC == before.PC,
              fmt("load=%d R %02X->%02X, PC %04X->%04X",
                  ok ? 1 : 0, before.R, after.R, before.PC, after.PC));

        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    {
        // Control for NEXPC0-08: the identical harness with a non-zero PC
        // MUST see the CPU run, otherwise "R unchanged" would prove
        // nothing. Bank 2 sits at $8000-$BFFF in the reset mapping, so a
        // `JR $` planted at its start executes forever at PC=$8000: R
        // advances, PC does not, and nothing is written.
        constexpr int FRAMES = 10;
        const std::string path = fixture_path("e2e_pcnz");

        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        Emulator emu;
        const bool built = write_nex_bank_fixture(path, kHdrPC, kHdrSP, 0, 0, 2,
                                                  {0x18, 0xFE});  // JR $
        const bool ok    = built && emu.init(cfg) && emu.load_nex(path);

        const Z80Registers before = emu.cpu().get_registers();
        for (int i = 0; i < FRAMES; ++i) emu.run_frame();
        const Z80Registers after = emu.cpu().get_registers();

        check("NEXPC0-09",
              "control: the same harness with PC=$8000 DOES execute — R advances "
              "while PC stays on the planted JR $, proving NEXPC0-08's probe is live",
              ok && after.R != before.R && before.PC == kHdrPC && after.PC == kHdrPC,
              fmt("load=%d R %02X->%02X, PC %04X->%04X (want PC pinned at %04X)",
                  ok ? 1 : 0, before.R, after.R, before.PC, after.PC, kHdrPC));

        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    // NEXPC0-10 — the parked state is machine state, so it must travel with
    // a snapshot. Without it a rewind silently un-parks a load-only NEX and
    // the CPU starts running at the reset vector mid-session — the very
    // thing GH #164 is about, reintroduced through the back door.
    {
        constexpr int    FRAMES = 10;
        constexpr size_t BANK   = 16384;
        const std::string path = fixture_path("state_pc0");

        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        Emulator emu;
        const bool built = write_nex_bank_fixture(path, 0x0000, kHdrSP, 0, 0, 2, {});
        const bool ok    = built && emu.init(cfg) && emu.load_nex(path);

        StateWriter measure;
        emu.save_state(measure);
        std::vector<uint8_t> buf(measure.position());
        StateWriter w(buf.data(), buf.size());
        emu.save_state(w);

        // Un-park by hand, then restore: only a serialised flag can put it back.
        emu.set_cpu_parked(false);
        StateReader r(buf.data(), buf.size());
        const bool restored = emu.load_state(r);

        const Z80Registers before = emu.cpu().get_registers();
        for (int i = 0; i < FRAMES; ++i) emu.run_frame();
        const Z80Registers after = emu.cpu().get_registers();

        size_t bad = BANK; uint8_t got = 0, want = 0;
        bool survived = ok && restored;
        for (size_t i = 0; i < BANK && survived; ++i) {
            const uint8_t g = read_page(emu.mmu(), static_cast<uint8_t>(4 + i / 0x2000),
                                        i % 0x2000);
            const uint8_t w2 = payload_byte(i);
            if (g != w2) { survived = false; bad = i; got = g; want = w2; }
        }
        check("NEXPC0-10",
              "the parked state survives a save_state/load_state round trip — after "
              "restore the CPU still executes nothing and bank 2 is still intact, so "
              "a rewind cannot un-park a load-only NEX",
              survived && after.R == before.R && after.PC == before.PC,
              fmt("load=%d restore=%d R %02X->%02X PC %04X->%04X bank2[0x%04zX]=0x%02X want 0x%02X",
                  ok ? 1 : 0, restored ? 1 : 0, before.R, after.R, before.PC, after.PC,
                  bad, got, want));

        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    // NEXPC0-11 — a park must not be permanent from the user's side. The
    // machine is deliberately left with nothing running, so the ONLY way
    // out is a reset; if that did not clear the flag, loading a PC=0 NEX
    // would wedge jnext until the user quit it.
    {
        constexpr int FRAMES = 10;
        const std::string path = fixture_path("reset_pc0");

        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        Emulator emu;
        const bool built = write_nex_bank_fixture(path, 0x0000, kHdrSP, 0, 0, 2, {});
        const bool ok    = built && emu.init(cfg) && emu.load_nex(path);
        const bool parked_after_load = emu.cpu_parked();

        emu.reset();
        const Z80Registers before = emu.cpu().get_registers();
        for (int i = 0; i < FRAMES; ++i) emu.run_frame();
        const Z80Registers after = emu.cpu().get_registers();

        check("NEXPC0-11",
              "a reset un-parks the machine — the CPU runs again afterwards, so a "
              "load-only NEX cannot wedge jnext (reset() re-runs init(), which "
              "clears the flag; the frontends' Reset takes the same path)",
              ok && parked_after_load && !emu.cpu_parked() && after.R != before.R,
              fmt("load=%d parked_after_load=%d parked_after_reset=%d R %02X->%02X",
                  ok ? 1 : 0, parked_after_load ? 1 : 0, emu.cpu_parked() ? 1 : 0,
                  before.R, after.R));

        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    // ── NEXPC0-12..16 — a park must never become a silent hang ───────
    //
    // The park stops instruction fetch indefinitely, so every entry point
    // that hands the machine content it is expected to EXECUTE has to
    // resume it (Emulator::resume_from_park), or it would succeed, report
    // success, and then do nothing forever with no error and no
    // indication. That is strictly worse than the wrong-but-visible cold
    // boot this whole change replaces, so it is a hard requirement, not a
    // nicety. It was a REGRESSION: before GH #164 a PC=0 NEX fell through
    // to a running (if corrupting) ROM boot, so a tape did eventually
    // load.
    //
    // Each row is its own call site and each can be deleted
    // independently, so each gets its own row. Every one parks via a REAL
    // PC=0 NEX through the real `--load` path, then asserts the CPU
    // actually advanced afterwards — R (incremented on every M1) is the
    // probe, and NEXPC0-09 above already proved that probe is live.
    {
        struct ResumeCase {
            const char* id;
            const char* what;
            const char* desc;
        };

        // Park a real machine on a load-only NEX; returns false if setup failed.
        auto park = [&](Emulator& emu, const std::string& nex_path) -> bool {
            EmulatorConfig cfg;
            cfg.type = MachineType::ZXN_ISSUE2;
            cfg.rewind_buffer_frames = 0;
            return write_nex_bank_fixture(nex_path, 0x0000, kHdrSP, 0, 0, 2, {}) &&
                   emu.init(cfg) && emu.load_nex(nex_path) && emu.cpu_parked();
        };

        auto run_and_check = [&](const ResumeCase& c, bool setup_ok, Emulator& emu,
                                 const Z80Registers& before, int frames) {
            for (int i = 0; i < frames; ++i) emu.run_frame();
            const Z80Registers after = emu.cpu().get_registers();
            check(c.id, c.desc,
                  setup_ok && !emu.cpu_parked() && after.R != before.R,
                  fmt("setup=%d still_parked=%d R %02X->%02X PC %04X->%04X",
                      setup_ok ? 1 : 0, emu.cpu_parked() ? 1 : 0,
                      before.R, after.R, before.PC, after.PC));
        };

        {   // NEXPC0-12 — TAP. The one the reviewer proved: reachable from
            // Tape > Open with nothing in between (gui/main_window.cpp).
            const std::string nex = fixture_path("resume_tap");
            const std::string tap = tape_fixture_path("resume", "tap");
            Emulator emu;
            bool ok = park(emu, nex) && write_tap_fixture(tap) && emu.load_tap(tap, true);
            const Z80Registers before = emu.cpu().get_registers();
            run_and_check({"NEXPC0-12", "TAP",
                           "attaching a TAP to a parked machine resumes it — neither the ROM "
                           "LD-BYTES trap nor the phantom typist can fire while the CPU is "
                           "parked, so the tape would otherwise never load (GH #164)"},
                          ok, emu, before, 500);
            std::error_code ec;
            std::filesystem::remove(nex, ec);
            std::filesystem::remove(tap, ec);
        }
        {   // NEXPC0-13 — TZX, same GUI menu, same mechanisms.
            const std::string nex = fixture_path("resume_tzx");
            const std::string tzx = tape_fixture_path("resume", "tzx");
            Emulator emu;
            bool ok = park(emu, nex) && write_tzx_fixture(tzx) && emu.load_tzx(tzx, true);
            const Z80Registers before = emu.cpu().get_registers();
            run_and_check({"NEXPC0-13", "TZX",
                           "attaching a TZX to a parked machine resumes it (GH #164)"},
                          ok, emu, before, 300);
            std::error_code ec;
            std::filesystem::remove(nex, ec);
            std::filesystem::remove(tzx, ec);
        }
        {   // NEXPC0-14 — WAV: always real-time, so it depends on the guest
            // running even more directly than the fast-load formats.
            const std::string nex = fixture_path("resume_wav");
            const std::string wav = tape_fixture_path("resume", "wav");
            Emulator emu;
            bool ok = park(emu, nex) && write_wav_fixture(wav) && emu.load_wav(wav);
            const Z80Registers before = emu.cpu().get_registers();
            run_and_check({"NEXPC0-14", "WAV",
                           "attaching a WAV to a parked machine resumes it (GH #164)"},
                          ok, emu, before, 300);
            std::error_code ec;
            std::filesystem::remove(nex, ec);
            std::filesystem::remove(wav, ec);
        }
        {   // NEXPC0-15 — inject_binary. NOT reported by review; found by
            // asking the same "what assumes the CPU advances" question of
            // every entry point. `--inject` names an entry address and sets
            // PC to it, so a parked CPU would sit at that address forever.
            const std::string nex = fixture_path("resume_inject");
            const std::string bin =
                (std::filesystem::temp_directory_path() /
                 ("jnext_nexpc0_resume_" + std::to_string(::getpid()) + ".bin")).string();
            Emulator emu;
            bool ok = park(emu, nex);
            if (ok) {
                std::ofstream o(bin, std::ios::binary | std::ios::trunc);
                const char code[2] = {0x18, static_cast<char>(0xFE)};   // JR $
                o.write(code, 2);
                ok = static_cast<bool>(o);
            }
            if (ok) ok = emu.inject_binary(bin, 0x9000, 0x9000);
            const Z80Registers before = emu.cpu().get_registers();
            run_and_check({"NEXPC0-15", "inject",
                           "--inject on a parked machine resumes it — it names an entry point "
                           "and sets PC to it, which a parked CPU would never reach (GH #164)"},
                          ok, emu, before, 100);
            std::error_code ec;
            std::filesystem::remove(nex, ec);
            std::filesystem::remove(bin, ec);
        }
        {   // NEXPC0-16 — the debugger's Step. Also not reported by review.
            // execute_single_instruction() delegates to the SAME shared body
            // run_frame() uses, which honours the park, so every Step was a
            // silent no-op and the debugger looked broken rather than the
            // machine looking parked. No "Parked" affordance exists yet.
            const std::string nex = fixture_path("resume_step");
            Emulator emu;
            const bool ok = park(emu, nex);
            const Z80Registers before = emu.cpu().get_registers();
            for (int i = 0; i < 5; ++i) emu.execute_single_instruction();
            const Z80Registers after = emu.cpu().get_registers();
            check("NEXPC0-16",
                  "a debugger single-step on a parked machine resumes it and actually "
                  "executes — the most explicit possible request to advance the CPU must "
                  "not be a silent no-op (GH #164)",
                  ok && !emu.cpu_parked() && after.R != before.R,
                  fmt("setup=%d still_parked=%d R %02X->%02X PC %04X->%04X",
                      ok ? 1 : 0, emu.cpu_parked() ? 1 : 0,
                      before.R, after.R, before.PC, after.PC));
            std::error_code ec;
            std::filesystem::remove(nex, ec);
        }
        {   // NEXPC0-18 — RZX playback with NO embedded snapshot. The sixth
            // and last resume call site, and the one this suite missed on
            // the first pass: five rows for six sites, caught by counting
            // rather than by any test failing. Nothing else covers it —
            // none of rzx-playback-func / rzx-record-func /
            // cold-boot-load-rzx-func combines RZX with a parked machine,
            // so disabling this call site alone left the suite fully green.
            //
            // The snapshot-bearing path needs no resume: it routes through
            // load_sna/load_szx, which reset and un-park by themselves.
            // Only a snapshot-less recording drives the CURRENT machine,
            // which is exactly what a parked one would refuse to be.
            const std::string nex = fixture_path("resume_rzx");
            const std::string rzx = tape_fixture_path("resume", "rzx");
            Emulator emu;
            bool ok = park(emu, nex) && write_rzx_fixture(rzx, 400) && emu.load_rzx(rzx);
            const Z80Registers before = emu.cpu().get_registers();
            run_and_check({"NEXPC0-18", "RZX",
                           "starting RZX playback with no embedded snapshot on a parked "
                           "machine resumes it — playback drives the CURRENT machine, so a "
                           "parked one would replay nothing forever (GH #164)"},
                          ok, emu, before, 50);
            std::error_code ec;
            std::filesystem::remove(nex, ec);
            std::filesystem::remove(rzx, ec);
        }
    }

    // NEXPC0-17 — the G156 boot hold is not armed for a load-only file.
    // The park pre-empts it permanently, so arming it would leave a
    // countdown ticking towards an entry that never happens.
    {
        const std::string parked_path  = fixture_path("hold_pc0");
        const std::string running_path = fixture_path("hold_pcnz");
        constexpr uint8_t START_DELAY = 40;

        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;

        Emulator parked;
        const bool p_ok =
            write_nex_bank_fixture(parked_path, 0x0000, kHdrSP, 0, 0, 2, {}, START_DELAY) &&
            parked.init(cfg) && parked.load_nex(parked_path);
        const uint32_t parked_hold = parked.boot_hold_frames_remaining();

        Emulator running;
        const bool r_ok =
            write_nex_bank_fixture(running_path, kHdrPC, kHdrSP, 0, 0, 2,
                                   {0x18, 0xFE}, START_DELAY) &&
            running.init(cfg) && running.load_nex(running_path);
        const uint32_t running_hold = running.boot_hold_frames_remaining();

        check("NEXPC0-17",
              "start_delay does NOT arm the G156 boot hold for a load-only file (the park "
              "pre-empts it permanently), while the identical file with a non-zero PC "
              "still arms it — the control proves the fixture really carries a delay",
              p_ok && r_ok && parked_hold == 0 && running_hold == START_DELAY,
              fmt("load pc0=%d pcnz=%d; hold parked=%u want 0, running=%u want %u",
                  p_ok ? 1 : 0, r_ok ? 1 : 0, parked_hold, running_hold,
                  static_cast<unsigned>(START_DELAY)));

        std::error_code ec;
        std::filesystem::remove(parked_path, ec);
        std::filesystem::remove(running_path, ec);
    }

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
