// NEX file format V1.3 tests — issue #162.
//
// Oracle: the V1.3 specification at
// https://wiki.specnext.dev/Alternative_NEX_file_formats and the reference
// loader that implements it, ped7g/ZXSpectrumNextMisc `nexload2/nexload2.asm`.
// This is a FILE-FORMAT convention, not hardware, so the spec and that loader
// — not the VHDL — are the authority throughout.
//
// V1.3 is NOT loaded by the distro's own nexload.asm (it cannot parse the
// version at all), which is why several behaviours here deliberately differ
// from jnext's nexload.asm-derived V1.0-V1.2 handling: the per-bank delay
// model (nexload2.asm:1005,:1038-1059) and the loading bar
// (nexload2.asm:489-509). Rows NEXV13-DLY-* and NEXV13-BAR-* pin exactly
// that difference, including a V1.2 control row proving the change is
// version-gated and not a blanket behaviour change.
//
// The header field table under test (offsets are file offsets):
//   10  screen flags, new bit 6 (+64) = screen described by "flags 2"
//   142 expansion bus     143 checksum flag     144 first-bank file offset
//   148 CLI buffer addr   150 CLI buffer size   152 loading-screen flags 2
//   153 copper block flag 154 tilemode config   158 loading-bar Y
//   508 optional CRC-32C
//
// Note that "loading screen flags 2" at 152 is an ENUMERATED value despite
// its name: 1 / 2 / 3, not bit flags (nexload2.asm:74-76). Rows
// NEXV13-REFUSE-02/03 pin the refusal of every other value.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/nex_loader.h"
#include "memory/mmu.h"
#include "peripheral/copper.h"
#include "video/layer2.h"

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

// ── Fixture construction ─────────────────────────────────────────────

// Distinct byte generators per region, so a byte found in the wrong place
// identifies which block it leaked from. None produce 0x00, so any zero read
// back is unambiguously "never written" rather than "written as zero".
uint8_t pal_byte (size_t i) { return static_cast<uint8_t>(1 + (i % 251)); }
uint8_t scr_byte (size_t i) { return static_cast<uint8_t>(1 + (i % 241)); }
uint8_t cop_byte (size_t i) { return static_cast<uint8_t>(1 + (i % 233)); }
uint8_t gap_byte (size_t i) { return static_cast<uint8_t>(1 + (i % 229)); }
uint8_t bank_byte(int bank, size_t i) {
    return static_cast<uint8_t>(1 + ((static_cast<size_t>(bank) * 7 + i) % 223));
}

struct V13Opts {
    const char* version        = "V1.3";
    uint8_t     screen_flags   = 0;
    uint8_t     screen_flags2  = 0;
    uint8_t     copper_flag    = 0;
    uint8_t     expansion_bus  = 1;   // 1 = leave alone; tests opt in to 0
    uint8_t     checksum_flag  = 0;
    uint32_t    banks_offset   = 0;   // 0 = write the derived value
    bool        omit_banks_offset = false;  // leave field 144 at zero
    uint32_t    forced_banks_offset = 0;    // non-zero = write this instead
    uint16_t    cli_addr       = 0;
    uint16_t    cli_size       = 0;
    uint8_t     tile_cfg[4]    = {0, 0, 0, 0};
    uint8_t     loading_bar_y  = 0;
    uint8_t     loading_bar    = 0;
    uint8_t     loading_bar_colour = 0;
    uint8_t     loading_delay  = 0;
    uint8_t     start_delay    = 0;
    uint8_t     hires_colour   = 0;
    uint8_t     entry_bank     = 0;
    uint8_t     preserve_regs  = 0;
    std::vector<int> banks;            // bank numbers present, in kBankOrder order
    uint32_t    gap_before_banks = 0;  // unknown-block bytes between screens and banks
    bool        write_crc      = false;  // compute and store the real CRC-32C
    uint32_t    forced_crc     = 0;      // stored when write_crc is false
};

// Screen block sizes computed INDEPENDENTLY of the loader — this is the
// oracle side of the sizing rows, derived from the spec table, not from
// nex_screen_bytes().
size_t expected_palette_bytes(const V13Opts& o) {
    const bool has_pal_screen = (o.screen_flags & 0x01) || (o.screen_flags & 0x40);
    return (has_pal_screen && !(o.screen_flags & 0x80)) ? 512 : 0;
}

size_t expected_screen_data_bytes(const V13Opts& o) {
    size_t n = 0;
    if (o.screen_flags & 0x01) n += 49152;
    if (o.screen_flags & 0x02) n += 6912;
    if (o.screen_flags & 0x04) n += 12288;
    if (o.screen_flags & 0x08) n += 12288;
    if (o.screen_flags & 0x10) n += 12288;
    if (o.screen_flags & 0x40) {
        if (o.screen_flags2 == 1 || o.screen_flags2 == 2) n += 81920;
        // screen_flags2 == 3 (tilemode) contributes no data block at all
    }
    return n;
}

size_t expected_copper_bytes(const V13Opts& o) {
    const bool v13 = std::strncmp(o.version, "V1.3", 4) == 0;
    return (v13 && o.copper_flag) ? 2048 : 0;
}

size_t expected_bank_offset(const V13Opts& o) {
    return 512 + expected_palette_bytes(o) + expected_screen_data_bytes(o) +
           expected_copper_bytes(o) + o.gap_before_banks;
}

void put_u16(std::vector<uint8_t>& f, size_t off, uint16_t v) {
    f[off] = static_cast<uint8_t>(v & 0xFF);
    f[off + 1] = static_cast<uint8_t>(v >> 8);
}

void put_u32(std::vector<uint8_t>& f, size_t off, uint32_t v) {
    f[off + 0] = static_cast<uint8_t>(v & 0xFF);
    f[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    f[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    f[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// Build a well-formed NEX to `path` from `o`. Returns the file image too, so
// CRC rows can checksum exactly what was written.
bool write_v13_nex(const std::string& path, const V13Opts& o, std::vector<uint8_t>& out) {
    const size_t pal_len  = expected_palette_bytes(o);
    const size_t scr_len  = expected_screen_data_bytes(o);
    const size_t cop_len  = expected_copper_bytes(o);
    const size_t bank_off = expected_bank_offset(o);
    const size_t total    = bank_off + o.banks.size() * 16384;

    std::vector<uint8_t> f(total, 0x00);

    std::memcpy(f.data() + 0, "Next", 4);
    std::memcpy(f.data() + 4, o.version, 4);
    f[8]  = 0;                                        // ram_required: 768 KB
    f[9]  = static_cast<uint8_t>(o.banks.size());     // num_banks
    f[10] = o.screen_flags;
    f[11] = 0;                                        // border
    put_u16(f, 12, 0xFF00);                           // SP
    put_u16(f, 14, 0x8000);                           // PC
    for (int b : o.banks) f[18 + b] = 1;              // bank presence bitmap
    f[130] = o.loading_bar;
    f[131] = o.loading_bar_colour;
    f[132] = o.loading_delay;
    f[133] = o.start_delay;
    f[134] = o.preserve_regs;
    f[138] = o.hires_colour;
    f[139] = o.entry_bank;

    // V1.3 block
    f[142] = o.expansion_bus;
    f[143] = o.checksum_flag;
    if (o.forced_banks_offset != 0) {
        put_u32(f, 144, o.forced_banks_offset);
    } else if (!o.omit_banks_offset) {
        put_u32(f, 144, static_cast<uint32_t>(bank_off));
    }
    put_u16(f, 148, o.cli_addr);
    put_u16(f, 150, o.cli_size);
    f[152] = o.screen_flags2;
    f[153] = o.copper_flag;
    std::memcpy(f.data() + 154, o.tile_cfg, 4);
    f[158] = o.loading_bar_y;

    // Block payloads, in file order
    size_t p = 512;
    for (size_t i = 0; i < pal_len; ++i) f[p + i] = pal_byte(i);
    p += pal_len;
    for (size_t i = 0; i < scr_len; ++i) f[p + i] = scr_byte(i);
    p += scr_len;
    for (size_t i = 0; i < cop_len; ++i) f[p + i] = cop_byte(i);
    p += cop_len;
    for (size_t i = 0; i < o.gap_before_banks; ++i) f[p + i] = gap_byte(i);
    p += o.gap_before_banks;
    for (int b : o.banks) {
        for (size_t i = 0; i < 16384; ++i) f[p + i] = bank_byte(b, i);
        p += 16384;
    }

    // Checksum last: it covers [512,EOF) then header [0,508).
    if (o.checksum_flag) {
        uint32_t crc = o.forced_crc;
        if (o.write_crc) {
            crc = NexLoader::crc32c_append(0, f.data() + 512, f.size() - 512);
            crc = NexLoader::crc32c_append(crc, f.data(), 508);
        }
        put_u32(f, 508, crc);
    }

    out = f;
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) return false;
    os.write(reinterpret_cast<const char*>(f.data()), static_cast<std::streamsize>(f.size()));
    return static_cast<bool>(os);
}

// Read one byte of physical 8K page `page` at `off` through a temp MMU slot,
// the same route NexLoader writes through.
uint8_t read_page(Mmu& mmu, uint16_t page, uint16_t off) {
    constexpr int      TEMP_SLOT = 7;
    constexpr uint16_t SLOT_BASE = 0xE000;
    const uint8_t saved = mmu.get_page(TEMP_SLOT);
    mmu.set_page(TEMP_SLOT, static_cast<uint8_t>(page));
    const uint8_t v = mmu.read(static_cast<uint16_t>(SLOT_BASE + off));
    mmu.set_page(TEMP_SLOT, saved);
    return v;
}

// One loaded fixture: a real Emulator with a real NexLoader::load()+apply().
struct Fixture {
    Emulator   emu;
    NexLoader  loader;
    std::vector<uint8_t> image;
    bool       load_ok  = false;
    bool       apply_ok = false;

    Fixture(const V13Opts& o, const char* tag, uint8_t nr80_seed = 0x00) {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);
        if (nr80_seed) emu.nextreg().write(0x80, nr80_seed);

        const std::string path =
            (std::filesystem::temp_directory_path() /
             (std::string("jnext_nexv13_") + tag + ".nex")).string();
        if (!write_v13_nex(path, o, image)) return;

        load_ok = loader.load(path);
        if (load_ok) apply_ok = loader.apply(emu);

        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// ── Header field parsing ─────────────────────────────────────────────

void test_header_fields() {
    V13Opts o;
    o.screen_flags  = 0x40;          // EXT2
    o.screen_flags2 = 3;             // tilemode: smallest fixture
    o.copper_flag   = 1;
    o.expansion_bus = 1;
    o.checksum_flag = 1;
    o.forced_crc    = 0xDEADBEEF;
    o.cli_addr      = 0xC123;
    o.cli_size      = 1234;
    o.tile_cfg[0] = 0x83; o.tile_cfg[1] = 0x11; o.tile_cfg[2] = 0x40; o.tile_cfg[3] = 0x4A;
    o.loading_bar_y = 236;
    o.banks = {5};
    o.entry_bank = 5;

    Fixture f(o, "hdr");
    if (!f.load_ok) {
        check("NEXV13-HDR-01", "fixture failed to load", false, "load() returned false");
        return;
    }
    const NexHeader& h = f.loader.header();

    check("NEXV13-HDR-01",
          "V1.3 scalar fields parse from offsets 142/143/152/153/158 "
          "(expansion bus, checksum flag, screen flags 2, copper flag, loading-bar Y)",
          h.expansion_bus == 1 && h.checksum_flag == 1 && h.screen_flags2 == 3 &&
          h.copper_flag == 1 && h.loading_bar_y == 236,
          fmt("exp=%u cks=%u sf2=%u cop=%u barY=%u", h.expansion_bus, h.checksum_flag,
              h.screen_flags2, h.copper_flag, h.loading_bar_y));

    check("NEXV13-HDR-02",
          "CLI buffer address (offset 148) and size (offset 150) parse as little-endian u16",
          h.cli_buffer_addr == 0xC123 && h.cli_buffer_size == 1234,
          fmt("addr=%#06x size=%u", h.cli_buffer_addr, h.cli_buffer_size));

    check("NEXV13-HDR-03",
          "tilemode configuration (offset 154) parses as 4 bytes = NR 0x6B, 0x6C, 0x6E, 0x6F",
          h.tilemode_cfg[0] == 0x83 && h.tilemode_cfg[1] == 0x11 &&
          h.tilemode_cfg[2] == 0x40 && h.tilemode_cfg[3] == 0x4A,
          fmt("%02x %02x %02x %02x", h.tilemode_cfg[0], h.tilemode_cfg[1],
              h.tilemode_cfg[2], h.tilemode_cfg[3]));

    check("NEXV13-HDR-04",
          "CRC-32C field (offset 508) parses as little-endian u32",
          h.crc32c == 0xDEADBEEFu, fmt("crc=%#010x", h.crc32c));

    check("NEXV13-HDR-05",
          "first-bank file offset (offset 144) parses as little-endian u32",
          h.banks_offset == static_cast<uint32_t>(expected_bank_offset(o)),
          fmt("banks_offset=%u want %zu", h.banks_offset, expected_bank_offset(o)));

    check("NEXV13-HDR-06",
          "the version string 'V1.3' is accepted and reported by is_v13()",
          f.loader.is_v13(), "is_v13() false for a V1.3 header");

    // Control: a V1.2 file must NOT be treated as V1.3, and its offset-153
    // byte must NOT be consumed as a copper-block flag.
    V13Opts v12;
    v12.version      = "V1.2";
    v12.screen_flags = 0x02;   // classic ULA screen
    v12.copper_flag  = 1;      // byte is set but the version does not define it
    v12.banks        = {5};
    Fixture f12(v12, "v12ctl");
    check("NEXV13-HDR-07",
          "a V1.2 header is not V1.3, and its offset-153 byte is NOT consumed as a "
          "copper-block flag (no 2048-byte block exists in a V1.2 file)",
          f12.load_ok && !f12.loader.is_v13() &&
          f12.loader.bank_data_offset() == 512 + 6912,
          fmt("load=%d v13=%d bank_off=%llu want %d", f12.load_ok, f12.loader.is_v13(),
              static_cast<unsigned long long>(f12.loader.bank_data_offset()), 512 + 6912));
}

// ── Screen-region sizing ─────────────────────────────────────────────

// A sizing row must be able to FAIL when nex_screen_bytes() gets the size
// wrong. With field 144 present that is impossible: the loader (correctly)
// trusts the header's stated bank offset, which silently repairs any sizing
// error and reduces the row to a tautology. So every sizing row omits field
// 144 — `omit_banks_offset` — leaving the DERIVED offset, and therefore
// nex_screen_bytes() itself, as the only thing under test.
//
// Bank content is asserted too, because that is the damage a mis-size does:
// a 512-byte palette error shifts every bank read by 512 bytes, which is
// small enough that neither the truncation check nor the extended-NEX
// heuristic notices — the file just loads garbage.
void size_row(const char* id, const char* desc, V13Opts o, const char* tag) {
    o.banks = {5, 2};
    o.omit_banks_offset = true;
    Fixture f(o, tag);
    const size_t want = expected_bank_offset(o);
    const bool offset_ok = f.load_ok && f.loader.bank_data_offset() == want;
    const bool content_ok = offset_ok && f.apply_ok &&
                            read_page(f.emu.mmu(), 10, 0) == bank_byte(5, 0) &&
                            read_page(f.emu.mmu(), 4, 0) == bank_byte(2, 0) &&
                            read_page(f.emu.mmu(), 4, 0x1FFF) == bank_byte(2, 0x1FFF);
    check(id, desc, offset_ok && content_ok,
          fmt("load=%d apply=%d bank_data_offset=%llu want %zu; bank5[0]=%02x want %02x, "
              "bank2[0]=%02x want %02x",
              f.load_ok, f.apply_ok,
              static_cast<unsigned long long>(f.loader.bank_data_offset()), want,
              offset_ok && f.apply_ok ? read_page(f.emu.mmu(), 10, 0) : 0, bank_byte(5, 0),
              offset_ok && f.apply_ok ? read_page(f.emu.mmu(), 4, 0) : 0, bank_byte(2, 0)));
}

void test_sizing() {
    V13Opts a; a.screen_flags = 0x40; a.screen_flags2 = 1;
    size_row("NEXV13-SIZE-01",
             "L2 320x256x8bpp with palette occupies 512 + 81920 bytes", a, "sz1");

    V13Opts b; b.screen_flags = 0x40 | 0x80; b.screen_flags2 = 1;
    size_row("NEXV13-SIZE-02",
             "L2 320x256x8bpp with +128 (no palette) occupies 81920 bytes", b, "sz2");

    V13Opts c; c.screen_flags = 0x40; c.screen_flags2 = 2;
    size_row("NEXV13-SIZE-03",
             "L2 640x256x4bpp with palette occupies 512 + 81920 bytes", c, "sz3");

    V13Opts d; d.screen_flags = 0x40; d.screen_flags2 = 3;
    size_row("NEXV13-SIZE-04",
             "tilemode with palette occupies 512 bytes and NO data block — tile data "
             "rides in regular bank 5 (nexload2.asm:133)", d, "sz4");

    V13Opts e; e.screen_flags = 0x40 | 0x80; e.screen_flags2 = 3;
    size_row("NEXV13-SIZE-05",
             "tilemode with +128 occupies no screen bytes at all", e, "sz5");

    V13Opts g; g.screen_flags = 0x40; g.screen_flags2 = 1; g.copper_flag = 1;
    size_row("NEXV13-SIZE-06",
             "the copper block adds 2048 bytes after the last screen data block", g, "sz6");
}

// ── Loud refusal of screens this loader cannot size ──────────────────

void refuse_row(const char* id, const char* desc, V13Opts o, const char* tag) {
    o.banks = {5};
    Fixture f(o, tag);
    check(id, desc, !f.load_ok,
          "load() SUCCEEDED on a header it cannot size — every bank would then be "
          "read from the wrong file offset (issue #156)");
}

void test_refusal() {
    V13Opts a; a.screen_flags = 0x20;   // bit 5: the one unassigned bit
    refuse_row("NEXV13-REFUSE-01",
               "screen_flags bit 5 (0x20) is unassigned, so its block size is unknown "
               "and load() must refuse rather than mis-size the region", a, "rf1");

    V13Opts b; b.screen_flags = 0x40; b.screen_flags2 = 0;
    refuse_row("NEXV13-REFUSE-02",
               "screen_flags bit 6 set with screen_flags2 == 0 declares a V1.3 screen "
               "that does not exist; load() must refuse", b, "rf2");

    V13Opts c; c.screen_flags = 0x40; c.screen_flags2 = 4;
    refuse_row("NEXV13-REFUSE-03",
               "screen_flags2 is enumerated 1..3, not a bit field — value 4 is not "
               "'modes 1 and 3' and load() must refuse", c, "rf3");

    // A V1.2 file that sets bit 6 has no flags-2 byte (offset 152 is reserved
    // and zero there), so it is unsizable in exactly the same way.
    V13Opts d; d.version = "V1.2"; d.screen_flags = 0x40;
    refuse_row("NEXV13-REFUSE-04",
               "a V1.2 header setting bit 6 has no flags-2 byte to describe the screen; "
               "load() must refuse rather than assume a size", d, "rf4");
}

// ── First-bank file offset (field 144) ───────────────────────────────

void test_banks_offset() {
    // Banks land correctly when the header's offset agrees with the blocks.
    {
        V13Opts o; o.screen_flags = 0x40; o.screen_flags2 = 1; o.banks = {5, 2};
        Fixture f(o, "boff1");
        const bool ok = f.load_ok && f.apply_ok &&
                        read_page(f.emu.mmu(), 4, 0) == bank_byte(2, 0) &&
                        read_page(f.emu.mmu(), 4, 0x1FFF) == bank_byte(2, 0x1FFF);
        check("NEXV13-BOFF-01",
              "with banks_offset matching the header-described blocks, bank data loads "
              "from the stated offset (bank 2 content intact at pages 4/5)",
              ok, fmt("load=%d apply=%d page4[0]=%02x want %02x", f.load_ok, f.apply_ok,
                      f.load_ok && f.apply_ok ? read_page(f.emu.mmu(), 4, 0) : 0,
                      bank_byte(2, 0)));
    }

    // An unknown 1024-byte block between screens and banks: the header states
    // where banks really start, so they must still load correctly. This is
    // the forward-compatibility case the spec gives field 144 for.
    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3;
        o.gap_before_banks = 1024;
        o.banks = {5, 2};
        Fixture f(o, "boff2");
        const bool ok = f.load_ok && f.apply_ok &&
                        f.loader.bank_data_offset() == expected_bank_offset(o) &&
                        read_page(f.emu.mmu(), 4, 0) == bank_byte(2, 0) &&
                        read_page(f.emu.mmu(), 10, 0) == bank_byte(5, 0);
        check("NEXV13-BOFF-02",
              "banks_offset past the known blocks makes the loader skip the intervening "
              "unknown block and still read every bank from the right place",
              ok, fmt("load=%d apply=%d off=%llu want %zu", f.load_ok, f.apply_ok,
                      static_cast<unsigned long long>(f.loader.bank_data_offset()),
                      expected_bank_offset(o)));
    }

    // Backwards is the one case that cannot be a future block: it would
    // overlap blocks already parsed, so the header is self-inconsistent.
    //
    // The geometry is chosen so that NO OTHER GUARD can refuse this file,
    // which is what makes the row specific to the inconsistency check. A
    // tilemode screen with a palette derives a bank offset of 1024; stating
    // 1000 puts the declared start only 24 bytes early. That is too small a
    // discrepancy for the truncation check (the file is still longer than
    // the header describes) and far too small for the extended-NEX
    // self-streaming heuristic, which needs a trailing region of more than
    // 16384 bytes. Remove the inconsistency check and this file LOADS —
    // 24 bytes out of step — rather than being caught by something else.
    //
    // The consistent twin is asserted in the same row so the refusal cannot
    // be attributed to the geometry: identical file, field 144 = 1024, loads.
    {
        V13Opts bad;
        bad.screen_flags = 0x40; bad.screen_flags2 = 3;
        bad.forced_banks_offset = 1000;         // derived is 1024
        bad.banks = {5, 2};
        Fixture fb(bad, "boff3bad");

        V13Opts good = bad;
        good.forced_banks_offset = 1024;        // == derived
        Fixture fg(good, "boff3good");

        check("NEXV13-BOFF-03",
              "a banks_offset BEFORE the end of the header's own blocks is "
              "self-inconsistent and must be refused, while the otherwise-identical "
              "file with a consistent offset loads — so the refusal is the "
              "inconsistency check and not some other guard reacting to the geometry",
              !fb.load_ok && fg.load_ok && fg.apply_ok,
              fmt("backwards load=%d (want 0), consistent load=%d apply=%d (want 1,1)",
                  fb.load_ok, fg.load_ok, fg.apply_ok));
    }

    // Field 144 does not exist before V1.3, so a stale value there must be
    // ignored rather than used to relocate the bank data.
    {
        V13Opts o;
        o.version = "V1.2";
        o.screen_flags = 0x02;               // classic ULA screen
        o.forced_banks_offset = 0xFFFF;      // garbage in the V1.2 reserved area
        o.banks = {5};
        Fixture f(o, "boff4");
        check("NEXV13-BOFF-04",
              "field 144 is ignored for V1.0-V1.2 files, where those bytes are reserved "
              "space that may hold anything",
              f.load_ok && f.loader.bank_data_offset() == 512 + 6912,
              fmt("load=%d bank_off=%llu want %d", f.load_ok,
                  static_cast<unsigned long long>(f.loader.bank_data_offset()), 512 + 6912));
    }
}

// ── CRC-32C ──────────────────────────────────────────────────────────

void test_crc() {
    // The canonical CRC-32C check value. This pins the algorithm itself:
    // reflected, polynomial 0x82F63B78, init and final XOR 0xFFFFFFFF. A
    // literal reading of the spec's "initial value is zero" as a zero CRC
    // register fails this row.
    {
        const char* v = "123456789";
        const uint32_t got =
            NexLoader::crc32c_append(0, reinterpret_cast<const uint8_t*>(v), 9);
        check("NEXV13-CRC-01",
              "CRC-32C of the standard check vector \"123456789\" is 0xE3069283",
              got == 0xE3069283u, fmt("got %#010x", got));
    }

    // The append form must chain, because the spec computes the checksum as
    // two separate ranges (file tail, then header head).
    {
        const uint8_t data[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        const uint32_t whole = NexLoader::crc32c_append(0, data, 8);
        const uint32_t split =
            NexLoader::crc32c_append(NexLoader::crc32c_append(0, data, 3), data + 3, 5);
        check("NEXV13-CRC-02",
              "crc32c_append chains: checksumming a buffer in two calls equals one call "
              "over the concatenation (the spec's two-range computation depends on it)",
              whole == split, fmt("whole=%#010x split=%#010x", whole, split));
    }

    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3;
        o.checksum_flag = 1; o.write_crc = true;
        o.banks = {5, 2};
        Fixture f(o, "crc3");
        check("NEXV13-CRC-03",
              "a correct CRC-32C over [512,EOF) then header [0,508) verifies as Valid",
              f.load_ok && f.loader.checksum_status() == NexChecksum::Valid,
              fmt("load=%d status=%d header=%#010x computed=%#010x", f.load_ok,
                  static_cast<int>(f.loader.checksum_status()),
                  f.loader.header().crc32c, f.loader.computed_crc32c()));
    }

    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3;
        o.checksum_flag = 1; o.write_crc = false; o.forced_crc = 0x12345678;
        o.banks = {5, 2};
        Fixture f(o, "crc4");
        check("NEXV13-CRC-04",
              "a wrong CRC-32C reports Invalid but still loads — the reference loader "
              "does not check this field, so refusing would reject files real hardware runs",
              f.load_ok && f.apply_ok &&
              f.loader.checksum_status() == NexChecksum::Invalid,
              fmt("load=%d apply=%d status=%d", f.load_ok, f.apply_ok,
                  static_cast<int>(f.loader.checksum_status())));
    }

    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3; o.checksum_flag = 0;
        o.banks = {5};
        Fixture f(o, "crc5");
        check("NEXV13-CRC-05",
              "with the checksum flag clear no CRC is computed and the status is Absent",
              f.load_ok && f.loader.checksum_status() == NexChecksum::Absent &&
              f.loader.computed_crc32c() == 0,
              fmt("status=%d computed=%#010x",
                  static_cast<int>(f.loader.checksum_status()), f.loader.computed_crc32c()));
    }
}

// ── Screen data ingest ───────────────────────────────────────────────

void test_screen_ingest() {
    // The two big Layer 2 screens go to banks 9,10,11,12,13 = pages 18..27
    // (nexload2.asm:623-624, LAYER2_BANK*2 = 18, 10 pages of $2000) — NOT to
    // banks 8,9,10 where the classic 256x192 Layer 2 screen goes.
    for (int mode = 1; mode <= 2; ++mode) {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80;   // no palette, so the screen starts at 512
        o.screen_flags2 = static_cast<uint8_t>(mode);
        o.banks = {5};
        Fixture f(o, mode == 1 ? "scr320" : "scr640");

        bool all_ok = f.load_ok && f.apply_ok;
        size_t bad_page = 0, bad_off = 0;
        uint8_t got = 0, want = 0;
        if (all_ok) {
            for (uint16_t pg = 18; pg <= 27 && all_ok; ++pg) {
                const size_t page_base = static_cast<size_t>(pg - 18) * 0x2000;
                const uint16_t offs[] = {0, 0x1000, 0x1FFF};
                for (uint16_t off : offs) {
                    const uint8_t g = read_page(f.emu.mmu(), pg, off);
                    const uint8_t w = scr_byte(page_base + off);
                    if (g != w) {
                        all_ok = false; bad_page = pg; bad_off = off; got = g; want = w;
                        break;
                    }
                }
            }
        }
        check(mode == 1 ? "NEXV13-SCR-01" : "NEXV13-SCR-02",
              mode == 1
                  ? "L2 320x256x8bpp screen data lands across pages 18-27 (banks 9-13)"
                  : "L2 640x256x4bpp screen data lands across pages 18-27 (banks 9-13)",
              all_ok,
              fmt("load=%d apply=%d page %zu off %#zx = %02x want %02x", f.load_ok,
                  f.apply_ok, bad_page, bad_off, got, want));
    }

    // Guard against the big screens being routed down the classic Layer 2
    // path: pages 16/17 (bank 8) must be untouched.
    {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80; o.screen_flags2 = 1;
        o.banks = {5};
        Fixture f(o, "scrb8");
        const bool ok = f.load_ok && f.apply_ok &&
                        read_page(f.emu.mmu(), 16, 0) == 0x00 &&
                        read_page(f.emu.mmu(), 17, 0) == 0x00;
        check("NEXV13-SCR-03",
              "the big Layer 2 screens do NOT write bank 8 (pages 16/17), where the "
              "classic 256x192 Layer 2 screen goes",
              ok, fmt("page16[0]=%02x page17[0]=%02x, expected both 0x00",
                      f.load_ok && f.apply_ok ? read_page(f.emu.mmu(), 16, 0) : 0xFF,
                      f.load_ok && f.apply_ok ? read_page(f.emu.mmu(), 17, 0) : 0xFF));
    }

    // Tilemode has no data block: its tiles arrive as ordinary bank 5 data,
    // so bank 5 must hold the BANK payload, not screen payload.
    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3;
        o.banks = {5};
        Fixture f(o, "scrtile");
        const bool ok = f.load_ok && f.apply_ok &&
                        read_page(f.emu.mmu(), 10, 0) == bank_byte(5, 0) &&
                        read_page(f.emu.mmu(), 18, 0) == 0x00;
        check("NEXV13-SCR-04",
              "tilemode consumes no screen data block — bank 5 holds its BANK payload "
              "and no Layer 2 page is written",
              ok, fmt("page10[0]=%02x want %02x, page18[0]=%02x want 00",
                      f.load_ok && f.apply_ok ? read_page(f.emu.mmu(), 10, 0) : 0,
                      bank_byte(5, 0),
                      f.load_ok && f.apply_ok ? read_page(f.emu.mmu(), 18, 0) : 0xFF));
    }
}

// ── Screen activation (NextREG writes) ───────────────────────────────

void test_screen_activation() {
    // NR 0x70 = resolution bits | palette offset from the HiRes-colour byte's
    // low nibble (nexload2.asm:699-707, :716-718).
    {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80; o.screen_flags2 = 1;
        o.hires_colour = 0x3B;   // low nibble 0x0B is the L2 palette offset
        o.banks = {5};
        Fixture f(o, "nr70a");
        const uint8_t nr70 = f.apply_ok ? f.emu.nextreg().read(0x70) : 0;
        check("NEXV13-NR-01",
              "L2 320x256 sets NR 0x70 to 0x10 | (HiRes-colour low nibble as palette offset)",
              f.apply_ok && nr70 == 0x1B, fmt("NR70=%#04x want 0x1B", nr70));
    }
    {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80; o.screen_flags2 = 2;
        o.hires_colour = 0x05;
        o.banks = {5};
        Fixture f(o, "nr70b");
        const uint8_t nr70 = f.apply_ok ? f.emu.nextreg().read(0x70) : 0;
        check("NEXV13-NR-02",
              "L2 640x256 sets NR 0x70 to 0x20 | palette offset",
              f.apply_ok && nr70 == 0x25, fmt("NR70=%#04x want 0x25", nr70));
    }
    {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80; o.screen_flags2 = 1;
        o.banks = {5};
        Fixture f(o, "nr69a");
        const uint8_t nr69 = f.apply_ok ? f.emu.nextreg().read(0x69) : 0;
        check("NEXV13-NR-03",
              "a big Layer 2 screen makes Layer 2 visible: NR 0x69 = 0x80",
              f.apply_ok && nr69 == 0x80, fmt("NR69=%#04x want 0x80", nr69));
    }
    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3;
        // NR 0x6E / 0x6F read back masked with 0xBF: VHDL zxnext.vhd:6108,:6111
        // force bit 6 (reserved) to '0' but PRESERVE bit 7, which is the
        // bank-select bit (nextreg.txt:711-731; src/video/tilemap.h:72,:80).
        // Bit 7 is set in both bytes here on purpose — ped7g's tilescreen.nex
        // uses 0x40/0x4A, which have bit 7 clear and so read back identically
        // under 0xBF and under a wrong 0x3F mask. These values tell them
        // apart: 0xC0 & 0xBF = 0x80, but 0xC0 & 0x3F would be 0x00.
        o.tile_cfg[0] = 0x83; o.tile_cfg[1] = 0x11; o.tile_cfg[2] = 0xC0; o.tile_cfg[3] = 0xCA;
        o.banks = {5};
        Fixture f(o, "nrtile");
        auto& nr = f.emu.nextreg();
        const bool ok = f.apply_ok && nr.read(0x6B) == 0x83 && nr.read(0x6C) == 0x11 &&
                        nr.read(0x6E) == (0xC0 & 0xBF) && nr.read(0x6F) == (0xCA & 0xBF) &&
                        nr.read(0x69) == 0x00;
        check("NEXV13-NR-04",
              "tilemode writes the four header config bytes to NR 0x6B, 0x6C, 0x6E, 0x6F "
              "(the latter two read back masked 0xBF — reserved bit 6 cleared, bank-select "
              "bit 7 preserved) and leaves Layer 2 off (NR 0x69 = 0)",
              ok, fmt("6B=%02x 6C=%02x 6E=%02x 6F=%02x 69=%02x",
                      f.apply_ok ? nr.read(0x6B) : 0, f.apply_ok ? nr.read(0x6C) : 0,
                      f.apply_ok ? nr.read(0x6E) : 0, f.apply_ok ? nr.read(0x6F) : 0,
                      f.apply_ok ? nr.read(0x69) : 0));
    }
    // The clip window matters because the generic NextREG init block sets
    // 0,255,0,255 — in 320-mode's half-resolution X units that is the wrong
    // window, not a wider one. This row fails if the V1.3 activation runs
    // before that init instead of after it.
    {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80; o.screen_flags2 = 1;
        o.banks = {5};
        Fixture f(o, "nrclip");
        auto& l2 = f.emu.layer2();
        const bool ok = f.apply_ok && l2.clip_x1() == 0 && l2.clip_x2() == 159 &&
                        l2.clip_y1() == 0 && l2.clip_y2() == 255;
        check("NEXV13-NR-05",
              "a big Layer 2 screen sets the Layer 2 clip window to 0,159,0,255 "
              "(nexload2.asm:708-715), surviving the generic NextREG init",
              ok, fmt("clip=%u,%u,%u,%u want 0,159,0,255",
                      f.apply_ok ? l2.clip_x1() : 0, f.apply_ok ? l2.clip_x2() : 0,
                      f.apply_ok ? l2.clip_y1() : 0, f.apply_ok ? l2.clip_y2() : 0));
    }
}

// ── Screen palette destination and tilemode default ──────────────────

// Read one palette entry's 8-bit RRRGGGBB through the hardware path: select
// the palette with auto-increment DISABLED (NR 0x43 bit 7), point NR 0x40 at
// the index, read NR 0x41 (which returns nr_palette_dat(8:1)).
uint8_t read_pal_8(Emulator& emu, uint8_t nr43_select, uint8_t index) {
    auto& nr = emu.nextreg();
    nr.write(0x43, static_cast<uint8_t>(0x80 | nr43_select));
    nr.write(0x40, index);
    return nr.read(0x41);
}

constexpr uint8_t kUlaClassic[16] = {
    0x00, 0x02, 0xA0, 0xA2, 0x14, 0x16, 0xB4, 0xB6,
    0x00, 0x03, 0xE0, 0xE7, 0x1C, 0x1F, 0xFC, 0xFF,
};

void test_palette() {
    // A +128 tilemode NEX carries no palette, so the loader must supply one —
    // nexload2.asm:834-836 uses the ULA classic table, repeated to fill all
    // 256 entries. Without this the screen renders solid black, because the
    // tilemap palette's power-on state is all-zero.
    {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80; o.screen_flags2 = 3;
        o.banks = {5};
        Fixture f(o, "pal1");
        bool ok = f.apply_ok;
        size_t bad = 0; uint8_t got = 0, want = 0;
        if (ok) {
            // Entries 0..15 are the table; 16..31 are it again (the 16x repeat
            // that fills all 256 entries), which entry 16 and 255 witness.
            for (uint8_t i = 0; i < 16 && ok; ++i) {
                got = read_pal_8(f.emu, 0x30, i);
                want = kUlaClassic[i];
                if (got != want) { ok = false; bad = i; }
            }
            if (ok) {
                got = read_pal_8(f.emu, 0x30, 16); want = kUlaClassic[0];
                if (got != want) { ok = false; bad = 16; }
            }
            if (ok) {
                got = read_pal_8(f.emu, 0x30, 255); want = kUlaClassic[15];
                if (got != want) { ok = false; bad = 255; }
            }
        }
        check("NEXV13-PAL-01",
              "a +128 tilemode NEX gets the ULA classic palette seeded into the TILEMAP "
              "palette, repeated across all 256 entries (nexload2.asm:834-836) — without "
              "it the screen is solid black",
              ok, fmt("apply=%d tilemap_pal[%zu]=%02x want %02x", f.apply_ok, bad, got, want));
    }

    // When the file DOES carry a palette it must win over that seed, and it
    // must land in the TILEMAP palette, not the Layer 2 one
    // (nexload2.asm:731-734 selects NR 0x43 = %0'011'000'0 for tilemode).
    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3;
        o.banks = {5};
        Fixture f(o, "pal2");
        bool ok = f.apply_ok;
        size_t bad = 0; uint8_t got = 0, want = 0;
        if (ok) {
            for (int i = 0; i < 256 && ok; ++i) {
                got = read_pal_8(f.emu, 0x30, static_cast<uint8_t>(i));
                want = pal_byte(static_cast<size_t>(i) * 2);
                if (got != want) { ok = false; bad = static_cast<size_t>(i); }
            }
        }
        check("NEXV13-PAL-02",
              "a tilemode NEX with a palette block loads it into the TILEMAP palette, "
              "overriding the classic-palette seed",
              ok, fmt("apply=%d tilemap_pal[%zu]=%02x want %02x", f.apply_ok, bad, got, want));
    }

    // The two big Layer 2 screens send their palette to the LAYER 2 palette
    // (nexload2.asm:735), while the tilemap palette still receives the
    // classic seed (nexload2 seeds it on every load, not only for tilemode).
    //
    // Index 1 is deliberate: the classic palette's entry 0 is 0x00, which is
    // also what an unwritten palette reads, so index 0 cannot tell "seeded"
    // from "never touched". Entry 1 is 0x02.
    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 1;
        o.banks = {5};
        Fixture f(o, "pal3");
        bool l2_ok = f.apply_ok;
        size_t bad = 0; uint8_t got = 0, want = 0;
        if (l2_ok) {
            for (int i = 0; i < 256 && l2_ok; ++i) {
                got = read_pal_8(f.emu, 0x10, static_cast<uint8_t>(i));
                want = pal_byte(static_cast<size_t>(i) * 2);
                if (got != want) { l2_ok = false; bad = static_cast<size_t>(i); }
            }
        }
        const uint8_t tm1 = f.apply_ok ? read_pal_8(f.emu, 0x30, 1) : 0xFF;
        check("NEXV13-PAL-03",
              "a big Layer 2 screen's palette block loads into the LAYER 2 palette while "
              "the tilemap palette holds the classic seed — index 1 (0x02) distinguishes "
              "seeded from never-written, which index 0 (0x00 either way) cannot",
              l2_ok && tm1 == 0x02,
              fmt("apply=%d l2_pal[%zu]=%02x want %02x, tilemap_pal[1]=%02x want 02",
                  f.apply_ok, bad, got, want, tm1));
    }

    // The seed is unconditional per nexload2.asm:293-294 -> :830-836, so a
    // file with a NON-tilemode screen and no tilemap palette of its own
    // still gets it. This row is what pins "unconditional": it uses a
    // +128 big Layer 2 screen, which carries no palette block at all.
    {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80; o.screen_flags2 = 2;
        o.banks = {5};
        Fixture f(o, "pal4");
        bool ok = f.apply_ok;
        size_t bad = 0; uint8_t got = 0, want = 0;
        if (ok) {
            for (uint8_t i = 0; i < 16 && ok; ++i) {
                got = read_pal_8(f.emu, 0x30, i);
                want = kUlaClassic[i];
                if (got != want) { ok = false; bad = i; }
            }
        }
        check("NEXV13-PAL-04",
              "the tilemap palette seed is unconditional, not tilemode-only: a Layer 2 "
              "640x256 file with no palette block still gets it, so a program that later "
              "switches to the tilemap layer finds a usable palette",
              ok, fmt("apply=%d tilemap_pal[%zu]=%02x want %02x", f.apply_ok, bad, got, want));
    }

    // ...but PRESERVENEXTREG suppresses it: nexload2.asm:781-783 returns from
    // setupBeforeBlockLoading before any palette reset is reached.
    {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80; o.screen_flags2 = 3;
        o.preserve_regs = 1;
        o.banks = {5};
        Fixture f(o, "pal5");
        const uint8_t tm1 = f.apply_ok ? read_pal_8(f.emu, 0x30, 1) : 0xFF;
        check("NEXV13-PAL-05",
              "preserve_regs suppresses the tilemap palette seed — nexload2.asm:781-783 "
              "returns from setupBeforeBlockLoading before reaching any palette reset",
              f.apply_ok && tm1 == 0x00,
              fmt("apply=%d tilemap_pal[1]=%02x want 00 (unseeded)", f.apply_ok, tm1));
    }
}

// ── Copper block ─────────────────────────────────────────────────────

void test_copper() {
    {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80; o.screen_flags2 = 3;
        o.copper_flag = 1;
        o.banks = {5};
        Fixture f(o, "cop1");

        bool words_ok = f.apply_ok;
        size_t bad = 0; uint16_t got = 0, want = 0;
        if (words_ok) {
            for (uint16_t w = 0; w < 1024; ++w) {
                const uint16_t expect =
                    static_cast<uint16_t>((cop_byte(w * 2) << 8) | cop_byte(w * 2 + 1));
                const uint16_t g = f.emu.copper().instruction(w);
                if (g != expect) { words_ok = false; bad = w; got = g; want = expect; break; }
            }
        }
        check("NEXV13-COP-01",
              "all 2048 copper bytes are streamed into instruction RAM as 1024 big-endian "
              "words (nexload2.asm:755-766 writes each byte to NR 0x63)",
              words_ok,
              fmt("apply=%d word[%zu]=%#06x want %#06x", f.apply_ok, bad, got, want));

        check("NEXV13-COP-02",
              "the copper is started with control code %01 (reset CPC and run) — "
              "nexload2.asm:767 NR 0x62 = 0x40",
              f.apply_ok && f.emu.copper().mode() == 1,
              fmt("copper mode=%u want 1", f.apply_ok ? f.emu.copper().mode() : 99));
    }

    // Control: without a copper block the copper stays stopped, proving the
    // start is driven by the header flag and not unconditional.
    {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80; o.screen_flags2 = 3;
        o.copper_flag = 0;
        o.banks = {5};
        Fixture f(o, "cop2");
        check("NEXV13-COP-03",
              "with no copper block the copper is left stopped",
              f.apply_ok && f.emu.copper().mode() == 0,
              fmt("copper mode=%u want 0", f.apply_ok ? f.emu.copper().mode() : 99));
    }
}

// ── Expansion bus ────────────────────────────────────────────────────

void test_expansion_bus() {
    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3; o.expansion_bus = 0;
        o.banks = {5};
        Fixture f(o, "exp1", 0xF5);
        const uint8_t nr80 = f.apply_ok ? f.emu.nextreg().read(0x80) : 0xFF;
        check("NEXV13-EXP-01",
              "expansion bus byte 0 clears the top four bits of NR 0x80",
              f.apply_ok && nr80 == 0x05, fmt("NR80=%#04x want 0x05", nr80));
    }
    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3; o.expansion_bus = 1;
        o.banks = {5};
        Fixture f(o, "exp2", 0xF5);
        const uint8_t nr80 = f.apply_ok ? f.emu.nextreg().read(0x80) : 0x00;
        check("NEXV13-EXP-02",
              "expansion bus byte 1 leaves NR 0x80 alone",
              f.apply_ok && nr80 == 0xF5, fmt("NR80=%#04x want 0xF5", nr80));
    }
    // In a V1.0-V1.2 file offset 142 is reserved space holding zero, which
    // must NOT be read as "disable the expansion bus".
    {
        V13Opts o;
        o.version = "V1.2"; o.screen_flags = 0x02; o.expansion_bus = 0;
        o.banks = {5};
        Fixture f(o, "exp3", 0xF5);
        const uint8_t nr80 = f.apply_ok ? f.emu.nextreg().read(0x80) : 0x00;
        check("NEXV13-EXP-03",
              "the expansion bus byte is ignored for V1.0-V1.2 files, whose offset 142 is "
              "reserved space that is zero for reasons unrelated to the expansion bus",
              f.apply_ok && nr80 == 0xF5, fmt("NR80=%#04x want 0xF5", nr80));
    }
}

// ── CLI buffer ───────────────────────────────────────────────────────

void test_cli_buffer() {
    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3;
        o.cli_addr = 0xC000; o.cli_size = 256;
        o.entry_bank = 5;
        o.banks = {5};
        Fixture f(o, "cli1");
        const auto regs = f.emu.cpu().get_registers();
        check("NEXV13-CLI-01",
              "a CLI buffer address and size set DE to the buffer address "
              "(nexload2.asm:376-389)",
              f.apply_ok && regs.DE == 0xC000, fmt("DE=%#06x want 0xC000", regs.DE));

        check("NEXV13-CLI-02",
              "the CLI buffer receives a zero-terminated (here empty) argument line, so a "
              "program reading it finds a well-formed line rather than stale RAM",
              f.apply_ok && f.emu.mmu().read(0xC000) == 0x00,
              fmt("[0xC000]=%02x want 0x00",
                  f.apply_ok ? f.emu.mmu().read(0xC000) : 0xFF));
    }
    // Address 0 means "no buffer"; size 0 likewise. Neither may write memory.
    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3;
        o.cli_addr = 0xC000; o.cli_size = 0;   // size 0 = no buffer
        o.entry_bank = 5;
        o.banks = {5};
        Fixture f(o, "cli3");
        // Bank 5 is mapped at 0xC000 via entry_bank, so the byte there is the
        // bank payload; it must be untouched.
        const uint8_t at = f.apply_ok ? f.emu.mmu().read(0xC000) : 0x00;
        check("NEXV13-CLI-03",
              "a zero CLI buffer size means no buffer: no terminator is written and the "
              "memory there keeps its loaded content",
              f.apply_ok && at == bank_byte(5, 0),
              fmt("[0xC000]=%02x want %02x (bank 5 payload)", at, bank_byte(5, 0)));
    }
}

// ── Loading bar and delay model ──────────────────────────────────────

void test_bar_and_delay() {
    // Pure function first: the V1.3 model charges the delay per bank ACTUALLY
    // LOADED, with no screen gate (nexload2.asm:1005, :1038-1059).
    check("NEXV13-DLY-01",
          "boot_hold_frames_v13 = banks_loaded * loading_delay + start_delay",
          NexLoader::boot_hold_frames_v13(200, 4, 100) == 600 &&
          NexLoader::boot_hold_frames_v13(0, 0, 100) == 0 &&
          NexLoader::boot_hold_frames_v13(7, 2, 3) == 13,
          fmt("got %u/%u/%u want 600/0/13",
              NexLoader::boot_hold_frames_v13(200, 4, 100),
              NexLoader::boot_hold_frames_v13(0, 0, 100),
              NexLoader::boot_hold_frames_v13(7, 2, 3)));

    // Wired through apply(): a 2-bank V1.3 file must hold 2*delay + start,
    // NOT the 109-slot tbblue figure (which would be 109*100+200 = 11100).
    {
        V13Opts o;
        o.screen_flags = 0x40; o.screen_flags2 = 3;
        o.loading_delay = 100; o.start_delay = 200;
        o.banks = {5, 2};
        Fixture f(o, "dly2");
        const uint32_t held = f.apply_ok ? f.emu.boot_hold_frames_remaining() : 0;
        check("NEXV13-DLY-02",
              "a V1.3 load holds the CPU for banks_loaded*loading_delay + start_delay "
              "frames — the nexload2 model, not nexload.asm's 109 bank slots",
              f.apply_ok && held == 400,
              fmt("held=%u want 400 (the tbblue model would give 11100)", held));
    }

    // The tbblue progress bar scribbles physical bank 11, which for a big
    // Layer 2 screen is the middle of the visible picture. V1.3 files are not
    // loaded by that loader, so the bar must not be drawn.
    {
        V13Opts o;
        o.screen_flags = 0x40 | 0x80; o.screen_flags2 = 1;
        o.loading_bar = 1; o.loading_bar_colour = 0xAA; o.loading_bar_y = 236;
        o.banks = {5};
        Fixture f(o, "bar1");
        // render_progress_mark maps pages 22/23 into slots 6/7 and writes
        // 0xFE00|l, 0xFF00|l, 0xFF00|l+1, 0xFE00|l+1 for l = d*2+18. All four
        // land in slot 7 (0xE000-0xFFFF) = page 23, at offsets 0x1E00|l and
        // 0x1F00|l. d=0 gives l=0x12, the first mark the bar would draw.
        // Page 23 is the 6th page of the big Layer 2 screen (pages 18-27).
        constexpr size_t PAGE23_BASE = static_cast<size_t>(23 - 18) * 0x2000;
        const bool intact = f.apply_ok &&
                            read_page(f.emu.mmu(), 23, 0x1E12) ==
                                scr_byte(PAGE23_BASE + 0x1E12) &&
                            read_page(f.emu.mmu(), 23, 0x1F12) ==
                                scr_byte(PAGE23_BASE + 0x1F12);
        check("NEXV13-BAR-01",
              "V1.3 draws no tbblue-style loading bar: physical bank 11 keeps its Layer 2 "
              "screen bytes instead of being scribbled with bar marks",
              intact,
              fmt("page23[0x1E12]=%02x want %02x, [0x1F12]=%02x want %02x",
                  f.apply_ok ? read_page(f.emu.mmu(), 23, 0x1E12) : 0,
                  scr_byte(PAGE23_BASE + 0x1E12),
                  f.apply_ok ? read_page(f.emu.mmu(), 23, 0x1F12) : 0,
                  scr_byte(PAGE23_BASE + 0x1F12)));
    }

    // Control row: the suppression is version-gated, not a blanket removal —
    // a V1.2 file with the bar enabled still gets its marks.
    {
        V13Opts o;
        o.version = "V1.2";
        o.screen_flags = 0x01;   // classic Layer 2 screen (banks 8,9,10)
        o.loading_bar = 1; o.loading_bar_colour = 0xAA;
        o.banks = {5};
        Fixture f(o, "bar2");
        // Same address arithmetic as NEXV13-BAR-01: the d=0 mark lands at
        // page 23 offsets 0x1E12 / 0x1F12. Bank 11 is outside the classic
        // Layer 2 screen's banks 8-10, so these bytes are the bar's alone.
        const bool marked = f.apply_ok &&
                            read_page(f.emu.mmu(), 23, 0x1E12) == 0xAA &&
                            read_page(f.emu.mmu(), 23, 0x1F12) == 0xAA;
        check("NEXV13-BAR-02",
              "a V1.2 file with the loading bar enabled still gets its progress marks — "
              "the V1.3 suppression is version-gated, not a blanket change",
              marked,
              fmt("page23[0x1E12]=%02x [0x1F12]=%02x want 0xAA both",
                  f.apply_ok ? read_page(f.emu.mmu(), 23, 0x1E12) : 0,
                  f.apply_ok ? read_page(f.emu.mmu(), 23, 0x1F12) : 0));
    }
}

} // namespace

int main() {
    std::printf("NEX V1.3 format tests (issue #162)\n\n");

    test_header_fields();
    test_sizing();
    test_refusal();
    test_banks_offset();
    test_crc();
    test_screen_ingest();
    test_screen_activation();
    test_palette();
    test_copper();
    test_expansion_bus();
    test_cli_buffer();
    test_bar_and_delay();

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
