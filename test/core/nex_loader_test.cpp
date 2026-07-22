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

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/nex_loader.h"
#include "memory/mmu.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

// Build a minimal, well-formed NEX carrying exactly one screen block of
// `screen_bytes` bytes selected by `screen_flag`, and write it to `path`.
bool write_nex_fixture(const std::string& path, uint8_t screen_flag, size_t screen_bytes) {
    std::vector<uint8_t> file(512 + screen_bytes, 0x00);

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

    for (size_t i = 0; i < screen_bytes; ++i) {
        file[512 + i] = payload_byte(i);
    }

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(f);
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

    LoadedFixture(uint8_t screen_flag, size_t screen_bytes, const char* tag) {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        const std::string path =
            (std::filesystem::temp_directory_path() /
             (std::string("jnext_nexload_test_") + tag + ".nex")).string();

        if (!write_nex_fixture(path, screen_flag, screen_bytes)) return;

        NexLoader loader;
        if (loader.load(path)) {
            ok = loader.apply(emu);
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// Three rows per 12288-byte split screen format.
void test_split_screen(const char* tag, uint8_t screen_flag,
                       const char* id_first, const char* id_second, const char* id_hole,
                       const char* asm_first, const char* asm_second) {
    constexpr size_t HALF = 6144;
    LoadedFixture f(screen_flag, 2 * HALF, tag);
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

} // namespace

int main() {
    std::printf("NEX loader screen-ingest tests (issue #68)\n\n");

    set_group("NEXSCR");

    // NEXSCR-LORES-01/02/03 — nexload.asm:472,:474
    test_split_screen("lores", NexHeader::SCREEN_LORES,
                      "NEXSCR-LORES-01", "NEXSCR-LORES-02", "NEXSCR-LORES-03",
                      "nexload.asm:472", "nexload.asm:474");

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

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
