#include "core/nex_loader.h"
#include "core/emulator.h"
#include "core/log.h"
#include "video/palette.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

/// Read a little-endian uint16_t from a byte buffer.
static uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

/// Read a little-endian uint32_t from a byte buffer.
static uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// Two-digit uppercase hex, for diagnostics built as std::string.
static std::string hex2(uint8_t v) {
    static const char* kDigits = "0123456789ABCDEF";
    return std::string{kDigits[v >> 4], kDigits[v & 0x0F]};
}

/// V1.3 big Layer 2 screen block: 81920 bytes = 5 banks (nexload2.asm:623-624
/// loads 10 MMU pages of $2000 each, starting at LAYER2_BANK*2 = page 18).
static constexpr size_t L2_BIG_SIZE = 81920;
/// V1.3 copper code block (nexload2.asm:757 `ld bc,$800`).
static constexpr size_t COPPER_BLOCK_SIZE = 2048;

/// True when the NEX file carries a 512-byte palette block ahead of the
/// screen data. tbblue/src/asm/nexload/nexload.asm:426-427:
///
///   ld a,(IsLoadingScr):and 128:jr nz,.skppal      ; bit 7 NO_PAL  → no palette
///   ld a,(IsLoadingScr):and %11010:jr nz,.skppal   ; ULA|HIRES|HICOL → no palette
///   ld ix,$c200:ld bc,$200:call fread              ; else: read 512 bytes
///
/// So the palette belongs to the two paletted screen kinds — Layer 2 (bit 0)
/// AND **LoRes** (bit 2) — not to Layer 2 alone, which is what jnext used to
/// assume. The file sizes of ped7g's nexload2 conformance pair are the
/// discriminating evidence (issue #156):
///
///   tmLoRes.nex      29696 = 512 hdr + **512 pal** + 12288 LoRes + 16384 bank
///   tmLoResNoPal.nex 29184 = 512 hdr +       0     + 12288 LoRes + 16384 bank
///
/// Without the LoRes palette counted, the 512 bytes were reported as
/// "trailing padding" and every following block was read 512 bytes early.
///
/// V1.3's LOADSCR bit 6 (EXT2) is paletted too — nexload2.asm:72 HASPAL =
/// LAYER2|LORES|EXT2 — and issue #162 now decodes those screen kinds, so bit 6
/// is included below. All three V1.3 screens (Layer 2 320x256, Layer 2
/// 640x256, tilemode) carry the same optional 512-byte block under the same
/// +128 rule (nexload2.asm:130-132).
///
/// One deliberate divergence: nexload2's own test is simply
/// `(sf & HASPAL) != 0 && !(sf & NOPAL)` (:296-300) with NO ULA/HiRes/HiCol
/// exclusion, so for a file mixing bit 6 with those older bits the two loaders
/// would disagree. jnext keeps nexload.asm's exclusion above. The case is
/// unreachable for conforming files: the spec says the old screen bits
/// "should be NOT mixed with new modes" (wiki Alternative_NEX_file_formats,
/// offset 152).
static bool nex_has_palette_block(uint8_t sf) {
    if (sf & NexHeader::SCREEN_NO_PAL) return false;
    if (sf & (NexHeader::SCREEN_ULA | NexHeader::SCREEN_HIRES | NexHeader::SCREEN_HICOLOUR))
        return false;
    return (sf & (NexHeader::SCREEN_LAYER2 | NexHeader::SCREEN_LORES |
                  NexHeader::SCREEN_EXT2)) != 0;
}

/// Bytes of optional screen data (palette + screen blocks) a NEX header
/// declares, in the exact order and sizes NexLoader::apply() consumes them.
/// Lets load() compute the header-described region (header + screens +
/// [copper] + banks). Bytes after that boundary remain host-backed for
/// self-streaming NEX files (issues #29/#84).
///
/// Returns false — with `why` filled in — when the header declares a screen
/// this loader cannot size. That refusal is the point: the previous code
/// silently returned 0 for an unrecognised flag, so the header-described
/// region came out short and EVERY subsequent bank was then read from the
/// wrong file offset, loading garbage without a single diagnostic
/// (issue #156 Scope, issue #162).
static bool nex_screen_bytes(const NexHeader& h, size_t& out, std::string& why) {
    const uint8_t sf = h.screen_flags;
    out = 0;

    if (sf & ~NexHeader::SCREEN_KNOWN_MASK) {
        why = "screen_flags has unassigned bit(s) set (0x" +
              hex2(static_cast<uint8_t>(sf & ~NexHeader::SCREEN_KNOWN_MASK)) +
              "); the size of the screen block they declare is unknown";
        return false;
    }

    // Palette block — shared rule, see nex_has_palette_block() above.
    if (nex_has_palette_block(sf)) out += 512;

    // Screen data blocks, in file order (nexload2.asm:614-625 — "order of
    // block definitions must be same as block order in file").
    if (sf & NexHeader::SCREEN_LAYER2)   out += 49152;
    if (sf & NexHeader::SCREEN_ULA)      out += 6912;
    if (sf & NexHeader::SCREEN_LORES)    out += 12288;
    if (sf & NexHeader::SCREEN_HIRES)    out += 12288;
    if (sf & NexHeader::SCREEN_HICOLOUR) out += 12288;

    if (sf & NexHeader::SCREEN_EXT2) {
        switch (h.screen_flags2) {
            case NexHeader::SCREEN2_L2_320x256:
            case NexHeader::SCREEN2_L2_640x256:
                out += L2_BIG_SIZE;
                break;
            case NexHeader::SCREEN2_TILEMODE:
                // No data block at all: "Tilemap data are stored in regular
                // (!) bank 5 - no specialized data block is used"
                // (nexload2.asm:133; block def :625 loads 0 pages).
                break;
            default:
                why = "screen_flags bit 6 selects a V1.3 screen but screen_flags2 "
                      "(offset 152) is " + std::to_string(h.screen_flags2) +
                      ", which is not one of 1 (L2 320x256x8bpp), "
                      "2 (L2 640x256x4bpp) or 3 (tilemode)";
                return false;
        }
    }

    return true;
}

// ram_required_kb / ram_required_fits — defined inline in nex_loader.h
// (G155). Inline keeps unit tests linkable without jnext_core.

/// Write `len` bytes from `src` into a physical 8K RAM page via the MMU.
/// Temporarily maps the page into slot 7 (0xE000-0xFFFF), writes, then restores.
static void write_to_page(Mmu& mmu, uint8_t page, const uint8_t* src, size_t page_offset, size_t len)
{
    constexpr int TEMP_SLOT = 7;
    constexpr uint16_t SLOT_BASE = 0xE000;

    uint8_t saved = mmu.get_page(TEMP_SLOT);
    mmu.set_page(TEMP_SLOT, page);

    for (size_t i = 0; i < len; ++i) {
        mmu.write(static_cast<uint16_t>(SLOT_BASE + page_offset + i), src[i]);
    }

    mmu.set_page(TEMP_SLOT, saved);
}

/// Write `len` bytes into RAM starting at the given 8K page and offset within that page.
/// Handles crossing page boundaries.
static void write_to_ram(Mmu& mmu, uint16_t start_page, size_t start_offset,
                         const uint8_t* src, size_t len)
{
    size_t remaining = len;
    size_t src_pos = 0;
    uint16_t page = start_page;
    size_t page_off = start_offset;

    while (remaining > 0) {
        size_t chunk = std::min(remaining, static_cast<size_t>(0x2000) - page_off);
        write_to_page(mmu, static_cast<uint8_t>(page), src + src_pos, page_off, chunk);
        src_pos += chunk;
        remaining -= chunk;
        ++page;
        page_off = 0;
    }
}

/// Ingest one 12288-byte NEX LoRes / HiRes / HiColour screen block into
/// bank 5 the way the reference loader does: TWO 6144-byte reads separated
/// by a 2048-byte hole — NOT one contiguous 12288-byte run (issue #68).
///
/// tbblue/src/asm/nexload/nexload.asm, shipped `ELSE` path of each block
/// (the `IFDEF testing` / `IFDEF testing8000` branches are debug-only and
/// are not assembled into the released loader):
///
///   LoRes    :472  ld ix,$4000 : ld bc,$1800 : call fread
///            :474  ld ix,$6000 : ld bc,$1800 : call fread
///   HiRes    :488  ld ix,$4000 : ld bc,$1800 : call fread
///            :490  ld ix,$6000 : ld bc,$1800 : call fread
///   HiColour :505  ld ix,$4000 : ld bc,$1800 : call fread
///            :507  ld ix,$6000 : ld bc,$1800 : call fread
///
/// nexload runs with bank 5 at $4000-$7FFF (it never remaps MMU slots 2/3
/// — only MMU_REGISTER_6/7 are touched, nexload.asm:441-443,:514 — and it
/// loads bank 5 itself with `ld ix,$4000 : ld bc,$4000`, nexload.asm:525).
/// So $4000 is bank-5 offset 0x0000 (page 10 offset 0) and $6000 is
/// bank-5 offset 0x2000 (page 11 offset 0) — the second half does NOT
/// follow the first at offset 0x1800.
///
/// The 2048 untouched bytes at $5800-$5FFF (bank-5 offset 0x1800-0x1FFF)
/// are the classic ULA attribute area, which none of these three modes
/// use: `video/lores.vhd:93-94` adds 1 to lores_addr bits 13:11 (i.e.
/// +0x800) for y >= 96, so the bottom 48 LoRes rows are read from
/// 0x2000-0x37FF and 0x1800-0x1FFF is skipped entirely. HiRes / HiColour
/// (Timex modes) likewise place their second half at 0x2000.
static void write_split_screen_block(Mmu& mmu, const uint8_t* src)
{
    constexpr size_t HALF = 6144;  // $1800 — one fread
    write_to_ram(mmu, 10, 0, src,        HALF);  // $4000 → bank-5 0x0000
    write_to_ram(mmu, 11, 0, src + HALF, HALF);  // $6000 → bank-5 0x2000
}

// render_progress_mark — defined inline in nex_loader.h (G156). Inline
// keeps unit tests linkable without jnext_core, same rationale as
// ram_required_kb / zero_bank5_screen_pages above.

/// Apply the three writes that end a loading-screen block in nexload.asm,
/// making the just-ingested screen actually visible. The values come from
/// nex_loading_screen_display() (nex_loader.h) — see its citation block.
///
/// Port addressing matches what the Z80 puts on the bus in the reference
/// loader: `ld bc,4667 : out (c),a` drives the full 16-bit 0x123B, while
/// `out (255),a` drives 0xFF in the low byte and a copy of A in the high
/// byte (jnext's port 0xFF handler matches on the low byte only, but the
/// full address is passed so the call is bus-accurate).
static void set_loading_screen_display(Emulator& emu, NexScreenKind kind, uint8_t hires_colour)
{
    const NexScreenDisplay d = nex_loading_screen_display(kind, hires_colour);
    emu.port().out(0x123B, d.port_123b);
    emu.nextreg().write(0x15, d.nr_15);
    emu.port().out(static_cast<uint16_t>((static_cast<uint16_t>(d.port_ff) << 8) | 0x00FF),
                   d.port_ff);
    Log::emulator()->debug("NEX: loading screen displayed — $123B={:#04x} NR$15={:#04x} $FF={:#04x}",
                           d.port_123b, d.nr_15, d.port_ff);
}

/// CRC-32C (Castagnoli), reflected, polynomial 0x82F63B78.
///
/// This mirrors sjasmplus's `crc32c_append_sw` (crc32c/crc32c.cpp), which is
/// the code that actually writes the field: it XORs the incoming value with
/// 0xFFFFFFFF on entry and the result with 0xFFFFFFFF on exit, so successive
/// calls chain to the same value a single call over the concatenated buffers
/// would give. Seeding a whole-file checksum with 0 therefore runs the
/// STANDARD CRC-32C (init 0xFFFFFFFF, final XOR 0xFFFFFFFF) — the wiki's
/// "initial value is zero" describes this chaining seed, not the CRC register.
uint32_t NexLoader::crc32c_append(uint32_t crc, const uint8_t* data, size_t len)
{
    static uint32_t table[256];
    static bool table_ready = false;
    if (!table_ready) {
        constexpr uint32_t POLY = 0x82F63B78u;
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t res = i;
            for (int k = 0; k < 8; ++k) {
                res = (res & 1u) ? (POLY ^ (res >> 1)) : (res >> 1);
            }
            table[i] = res;
        }
        table_ready = true;
    }

    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/// Compute the V1.3 CRC-32C of an already-open NEX file: file content from
/// offset 512 to EOF (including any appended custom binary), then the first
/// 508 bytes of the header, stopping ahead of the checksum field itself
/// (wiki "Alternative NEX file formats" offset 508; sjasmplus io_nex.cpp
/// SNexFile::calculateCrc32C).
static bool compute_nex_crc32c(std::ifstream& f, uint64_t file_size, uint32_t& out)
{
    constexpr size_t CHUNK = 64 * 1024;
    std::vector<uint8_t> buf(CHUNK);
    uint32_t crc = 0;

    f.clear();
    f.seekg(512);
    uint64_t remaining = file_size - 512;
    while (remaining > 0) {
        const size_t want = static_cast<size_t>(std::min<uint64_t>(remaining, CHUNK));
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(want));
        if (f.gcount() != static_cast<std::streamsize>(want)) return false;
        crc = NexLoader::crc32c_append(crc, buf.data(), want);
        remaining -= want;
    }

    f.clear();
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), 508);
    if (f.gcount() != 508) return false;
    out = NexLoader::crc32c_append(crc, buf.data(), 508);
    return true;
}

bool NexLoader::load(const std::string& path)
{
    loaded_ = false;
    file_data_.clear();
    file_size_ = 0;
    payload_offset_ = 0;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        Log::emulator()->error("NEX: cannot open '{}'", path);
        return false;
    }

    const auto end_pos = f.tellg();
    if (end_pos < 0) {
        Log::emulator()->error("NEX: cannot determine size of '{}'", path);
        return false;
    }
    const uint64_t file_size = static_cast<uint64_t>(end_pos);
    file_size_ = file_size;
    if (file_size < 512) {
        Log::emulator()->error("NEX: file '{}' too small ({} bytes, need >= 512)", path, file_size);
        return false;
    }

    // Read raw header bytes
    uint8_t raw[512];
    f.seekg(0);
    f.read(reinterpret_cast<char*>(raw), 512);

    // Validate magic
    if (std::memcmp(raw, "Next", 4) != 0) {
        Log::emulator()->error("NEX: invalid magic in '{}' (expected 'Next')", path);
        return false;
    }

    // Parse header fields
    std::memcpy(header_.magic, raw + 0, 4);
    std::memcpy(header_.version, raw + 4, 4);
    header_.ram_required      = raw[8];
    header_.num_banks         = raw[9];
    header_.screen_flags      = raw[10];
    header_.border_colour     = raw[11];
    header_.sp                = read_u16(raw + 12);
    header_.pc                = read_u16(raw + 14);
    header_.extra_files       = read_u16(raw + 16);
    std::memcpy(header_.banks, raw + 18, 112);
    header_.loading_bar       = raw[130];
    header_.loading_bar_colour = raw[131];
    header_.loading_delay     = raw[132];
    header_.start_delay       = raw[133];
    header_.preserve_regs     = raw[134];
    header_.core_version[0]   = raw[135];
    header_.core_version[1]   = raw[136];
    header_.core_version[2]   = raw[137];
    header_.hires_colour      = raw[138];
    header_.entry_bank        = raw[139];
    header_.file_handle       = read_u16(raw + 140);

    // V1.3 fields (offsets 142-158 + 508). In a V1.0-V1.2 file these bytes
    // are the zeroed reserved area, so parsing them unconditionally is safe;
    // every *use* below is gated on the version being V1.3.
    header_.expansion_bus     = raw[142];
    header_.checksum_flag     = raw[143];
    header_.banks_offset      = read_u32(raw + 144);
    header_.cli_buffer_addr   = read_u16(raw + 148);
    header_.cli_buffer_size   = read_u16(raw + 150);
    header_.screen_flags2     = raw[152];
    header_.copper_flag       = raw[153];
    std::memcpy(header_.tilemode_cfg, raw + 154, 4);
    header_.loading_bar_y     = raw[158];
    header_.crc32c            = read_u32(raw + 508);

    // Loader-version gate (nexload.asm:290-291):
    //
    //   ld a,(V_MAJOR):sub '0':and %1111:SWAPNIB:ld b,a
    //   ld a,(V_MINOR):and %1111:or b:ld b,a       ; B = BCD, "V1.2" -> $12
    //   ld a,(LoaderVersion):cp b:jp c,loaderUpdate
    //
    // `jp c` fires when the LOADER's version is below the FILE's, i.e. only a
    // file from the future is refused; an older file always loads. jnext used
    // to warn and load anyway, which silently mis-parses a newer layout — the
    // exact failure issue #156 records for V1.3, where undecoded screen bits
    // made nex_screen_bytes() undercount and every bank was then read from the
    // wrong offset. Refusing loudly beats loading garbage.
    //
    // The ceiling is 0x13 as of issue #162: the V1.3 screen kinds ARE decoded
    // now (nex_screen_bytes handles all three and refuses anything it cannot
    // size), so V1.3 files load. ped7g's `nexload2/s p a c e.nex` — a V1.3
    // file with screen_flags=0x00 and zero banks, briefly an expected casualty
    // while the ceiling was 0x12 — loads again. Rows NEXVER-05 (V1.3 loads)
    // and NEXVER-06 (V9.9 still refused) pin both sides of the gate.
    //
    // Raising this constant is not a formality: it must move in the SAME
    // change that teaches nex_screen_bytes() the new version's screen kinds,
    // or the loader accepts a layout it cannot size.
    header_.version_bcd = nex_version_bcd(header_.version);
    if (kNexLoaderVersionBcd < header_.version_bcd) {
        Log::emulator()->error(
            "NEX: '{}' is version '{:.4s}' but this loader supports up to V{:x}.{:x}; "
            "refusing to load (a newer layout would be mis-parsed)",
            path, header_.version,
            (kNexLoaderVersionBcd >> 4) & 0x0F, kNexLoaderVersionBcd & 0x0F);
        return false;
    }

    // Core-version requirement (nexload.asm:308-316). Reported, NOT enforced:
    // the reference loader reads NR 0x00 first and SKIPS the whole check when
    // it reads 8 ("emulator") — `cp 8:jp z,.ok` at nexload.asm:311. jnext
    // reports 0x0A (real hardware) because NextZXOS's ROM1 machine-ID branch
    // needs it (src/port/nextreg.cpp:220-225, test MID-01), so the faithful
    // reading is not "enforce the check" but "the check does not apply to us".
    // jnext's own NR 0x01/NR 0x0E core version is a nominal 3.02.03 that does
    // not track feature-for-feature with any real core, so enforcing it would
    // refuse working files for a number that means little here. Warn instead,
    // naming both versions, so an unsupported-core failure is diagnosable.
    if (!nex_core_version_ok(header_.core_version[0], header_.core_version[1],
                             header_.core_version[2])) {
        Log::emulator()->warn(
            "NEX: '{}' asks for core {}.{:02}.{:02}, above the {}.{:02}.{:02} jnext reports; "
            "loading anyway (nexload.asm:311 skips this check on emulators)",
            path, header_.core_version[0], header_.core_version[1], header_.core_version[2],
            kJnextCoreMajor, kJnextCoreMinor, kJnextCoreSubminor);
    }

    // Locate the end of the fixed machine-state image. Count the banks the
    // presence bitmap actually declares — apply() loads
    // exactly these (header_.banks[bank] != 0). The header's num_banks scalar
    // (offset 9) is decorative: the reference nexload.asm never reads it and
    // jnext's apply() never has either, so trusting it here would false-reject
    // a well-formed NEX whose num_banks disagrees with the bitmap. (issue #10)
    size_t declared_banks = 0;
    for (int b = 0; b < 112; ++b) {
        if (header_.banks[b]) ++declared_banks;
    }
    // A well-formed NEX has num_banks (offset 9) equal to the bitmap count. A
    // mismatch means the file is technically malformed; the bitmap is what
    // apply() loads, so warn and proceed with the bitmap rather than reject.
    if (header_.num_banks != declared_banks) {
        Log::emulator()->warn(
            "NEX: '{}' header num_banks={} disagrees with the bank bitmap ({} bank(s) present); "
            "num_banks is decorative — loading the banks the bitmap declares and hoping for the best.",
            path, header_.num_banks, declared_banks);
    }
    size_t screen_bytes = 0;
    std::string why;
    if (!nex_screen_bytes(header_, screen_bytes, why)) {
        // Refuse rather than mis-size. A wrong screen-region size shifts the
        // start of EVERY bank, so the file would "load" into garbage with no
        // diagnostic at all (issue #156 Scope, issue #162).
        Log::emulator()->error("NEX: '{}' declares a screen this loader cannot size: {}",
                               path, why);
        return false;
    }

    const bool v13 = is_v13();

    // V1.3 copper code block: 2048 bytes after the last screen data block
    // (wiki offset 153; nexload2.asm:315-317, :755-758).
    const size_t copper_bytes =
        (v13 && header_.copper_flag) ? COPPER_BLOCK_SIZE : 0;

    // Where the first bank's data starts. V1.3 states it outright at offset
    // 144; V1.0-V1.2 files leave it 0 and it must be derived.
    const uint64_t derived_bank_offset = 512 + screen_bytes + copper_bytes;
    uint64_t bank_offset = derived_bank_offset;

    if (v13 && header_.banks_offset != 0 &&
        header_.banks_offset != derived_bank_offset) {
        // The spec's stated purpose for this field is to let a loader skip
        // blocks it does not understand, so the header — not our derivation
        // — is the authority on where banks begin. Going BACKWARDS is the
        // one case that cannot be a future block: it would overlap data we
        // already parsed, so the header is self-inconsistent and we refuse.
        if (header_.banks_offset < derived_bank_offset) {
            Log::emulator()->error(
                "NEX: '{}' header banks_offset={} is before the end of the blocks its own "
                "header describes ({}); the header is inconsistent",
                path, header_.banks_offset, derived_bank_offset);
            return false;
        }
        Log::emulator()->warn(
            "NEX: '{}' header banks_offset={} disagrees with the {} bytes of blocks its header "
            "describes; trusting the header and skipping the {} intervening byte(s) — they are "
            "presumably a block type this loader does not know",
            path, header_.banks_offset, derived_bank_offset,
            header_.banks_offset - derived_bank_offset);
        bank_offset = header_.banks_offset;
    }
    bank_data_offset_ = bank_offset;

    const uint64_t expected_size = bank_offset + declared_banks * 16384;
    payload_offset_ = expected_size;
    if (file_size < expected_size) {
        Log::emulator()->error(
            "NEX: '{}' is truncated — file is {} bytes, header describes {} bytes",
            path, file_size, expected_size);
        return false;
    }

    // Optional V1.3 checksum. Verified and reported, but NOT load-blocking:
    // the reference loader does not check it at all ("intended for PC tools
    // ... not for regular loading of NEX files", wiki offset 508), so
    // refusing here would reject files real hardware runs.
    if (v13 && header_.checksum_flag) {
        uint32_t computed = 0;
        if (!compute_nex_crc32c(f, file_size, computed)) {
            Log::emulator()->error("NEX: '{}' declares a CRC-32C but could not be re-read to "
                                   "verify it", path);
        } else {
            computed_crc32c_ = computed;
            if (computed == header_.crc32c) {
                checksum_status_ = NexChecksum::Valid;
                Log::emulator()->debug("NEX: '{}' CRC-32C {:#010x} verified", path, computed);
            } else {
                checksum_status_ = NexChecksum::Invalid;
                Log::emulator()->error(
                    "NEX: '{}' CRC-32C MISMATCH — header says {:#010x}, file computes {:#010x}; "
                    "the file is corrupt or was modified. Loading anyway (the reference loader "
                    "does not check this field).",
                    path, header_.crc32c, computed);
            }
        }
    }

    // Large trailing regions are self-streamed payloads. They are useful only
    // when the NEX header asks nexload to keep the file open and deliver its
    // handle. Preserve issue #10's explicit rejection for unusable extended
    // files (file_handle=0), while valid self-streamers are loaded without
    // slurping their potentially-huge payload into host RAM.
    const bool keep_open =
        header_.file_handle == 1 || header_.file_handle >= 0x4000;
    if (file_size > expected_size + 16384 && !keep_open) {
        Log::emulator()->error(
            "NEX: '{}' is an extended NEX file — {} bytes follow the {} bank(s) its header "
            "describes (file {} bytes, expected {}), but file_handle=0 asks the loader to "
            "close the file. The appended payload would be unreachable.",
            path, file_size - expected_size, declared_banks, file_size, expected_size);
        return false;
    }

    // Read only the region apply() consumes. Any appended payload stays in the
    // host file and is served lazily by Emulator's extended-NEX bridge.
    const size_t data_size = static_cast<size_t>(expected_size - 512);
    file_data_.resize(data_size);
    f.seekg(512);
    f.read(reinterpret_cast<char*>(file_data_.data()), static_cast<std::streamsize>(data_size));
    if (!f) {
        Log::emulator()->error("NEX: failed reading {} declared data bytes from '{}'",
                              data_size, path);
        return false;
    }

    if (file_size > expected_size && keep_open) {
        Log::emulator()->info(
            "NEX: '{}' carries {} appended payload bytes; keeping them host-backed",
            path, file_size - expected_size);
    } else if (file_size > expected_size) {
        Log::emulator()->info(
            "NEX: '{}' carries {} trailing padding bytes; ignoring them",
            path, file_size - expected_size);
    }

    Log::emulator()->info("NEX: loaded '{}' — version={:.4s} banks={} screen_flags={:#04x} "
                          "pc={:#06x} sp={:#06x} entry_bank={} border={}",
                          path, header_.version, header_.num_banks, header_.screen_flags,
                          header_.pc, header_.sp, header_.entry_bank, header_.border_colour);

    loaded_ = true;
    return true;
}

bool NexLoader::apply(Emulator& emu) const
{
    if (!loaded_) {
        Log::emulator()->error("NEX: apply() called without successful load()");
        return false;
    }

    // Validate ram_required (NEX V1.1+ field at header offset 8) against the
    // installed Ram size. tbblue's official nexload aborts when ram_required
    // exceeds installed RAM. jnext's previous behaviour was a silent proceed
    // → corrupted bank loads on under-installed RAM. (G155)
    if (!ram_required_fits(header_.ram_required, emu.ram().size())) {
        Log::emulator()->error(
            "NEX: ram_required={} ({} KB) exceeds installed RAM ({} KB); aborting load",
            header_.ram_required, ram_required_kb(header_.ram_required),
            emu.ram().size() / 1024);
        return false;
    }

    Mmu& mmu = emu.mmu();
    Z80Cpu& cpu = emu.cpu();

    size_t offset = 0;  // current read position in file_data_

    // ---------------------------------------------------------------
    // 1. NextREG initialization (replicates official nexload.asm)
    //
    // The official NextZXOS NEX loader sets up a standard machine
    // state before launching the program. We replicate the key
    // register writes here. Source: nexload.asm from tbblue gitlab.
    //
    // ORDER MATTERS, and it is nexload.asm's: this whole reset block
    // runs FIRST (nexload.asm:323-409, gated by DONTRESETNEXTREGS at :323
    // and falling through to the `.dontresetregs` label at :422),
    // and only then does the loading-screen block run (:424-511). The
    // screen block therefore has the LAST word on NR 0x15, NR 0x43 and
    // the ULA palette. jnext used to run them the other way round, which
    // silently clobbered a LoRes screen's NR 0x15 bit 7 (LoRes enable)
    // and one entry of its ULA palette (issue #156).
    // ---------------------------------------------------------------

    auto& nr = emu.nextreg();

    // tbblue nexload.asm bundle (Task 3) — six NR writes the official
    // nexload.asm performs that we previously omitted. Source:
    // /tbblue/src/asm/nexload/nexload.asm:326-365.
    //
    // Stop copper before reset:
    nr.write(0x62, 0x00);   // copper-control HI byte: stop
    nr.write(0x61, 0x00);   // copper-control LO byte: stop
    // Peripheral 2 — read-modify-write in tbblue (preserves bits 7+5
    // F8/F3-enable, clears bit 4 DivMMC autopage, sets bit 3 Multiface).
    // Reset baseline of NR 0x06 is 0xA0 (zxnext.vhd:1107-1108), so the
    // post-modify constant is 0xA0 & ~0x10 | 0x08 = 0xA8.
    nr.write(0x06, 0xA8);
    // Peripheral 3 — read-modify-write in tbblue (sets bit 7 disable
    // locked paging, bit 6 disable contention, clears bit 5 stereo→ABC,
    // sets bits 3/2/1 specdrum/timex/turbosound, clears bit 0).
    // Reset baseline of NR 0x08 is 0x00, so the post-modify constant is
    // 0xCE.
    nr.write(0x08, 0xCE);
    // Turbo 28 MHz (matches tbblue nexload — assumes loader-issued
    // turbo is honoured by the CPU model; vblank-flush fix in commit
    // 346b53b is the prerequisite that prevents tilemap_demo from
    // black-screening at this speed).
    nr.write(0x07, 0x03);
    // ULANext format: 0x0F (allow flashing). Overrides Emulator init's
    // 0x07 — tbblue nexload.asm:395 writes this every NEX load.
    nr.write(0x42, 0x0F);

    // Sprite/layer system: sprites visible, SLU priority
    nr.write(0x15, 0x01);

    // Transparency: global colour = 0xE3, fallback = 0x00
    nr.write(0x14, 0xE3);
    nr.write(0x4A, 0x00);

    // Sprite transparency index = 0xE3
    nr.write(0x4B, 0xE3);

    // Layer 2 active bank = 9, shadow bank = 12
    nr.write(0x12, 0x09);
    nr.write(0x13, 0x0C);

    // Reset all clip window indices
    nr.write(0x1C, 0x0F);

    // Layer 2 clip: 0, 255, 0, 255 (full)
    nr.write(0x18, 0x00); nr.write(0x18, 0xFF);
    nr.write(0x18, 0x00); nr.write(0x18, 0xFF);
    // Sprite clip: 0, 255, 0, 255
    nr.write(0x19, 0x00); nr.write(0x19, 0xFF);
    nr.write(0x19, 0x00); nr.write(0x19, 0xFF);
    // ULA clip: 0, 255, 0, 191
    nr.write(0x1A, 0x00); nr.write(0x1A, 0xFF);
    nr.write(0x1A, 0x00); nr.write(0x1A, 0xBF);
    // Tilemap clip: 0, 159, 0, 255
    nr.write(0x1B, 0x00); nr.write(0x1B, 0x9F);
    nr.write(0x1B, 0x00); nr.write(0x1B, 0xFF);

    // Layer 2 scroll = 0,0
    nr.write(0x16, 0x00);
    nr.write(0x17, 0x00);

    // Palette control: select ULA first palette, no auto-increment
    nr.write(0x43, 0x00);

    // ULA palette entry 24 (border ink 0) = 0xE3 (standard transparent)
    nr.write(0x40, 0x18);
    nr.write(0x41, 0xE3);

    // Reset palette index to 0 (programs assume this after NEX load)
    nr.write(0x40, 0x00);

    Log::emulator()->debug("NEX: machine state initialized (nexload.asm compatible)");

    // ---------------------------------------------------------------
    // 2. Screen data
    // ---------------------------------------------------------------

    const uint8_t sf = header_.screen_flags;
    const bool    v13 = is_v13();
    // The V1.3 screen kind, or SCREEN2_NONE when bit 6 is clear. load() has
    // already rejected any other value, so this is one of the three known
    // kinds whenever it is non-zero.
    const uint8_t sf2 = (sf & NexHeader::SCREEN_EXT2) ? header_.screen_flags2
                                                      : NexHeader::SCREEN2_NONE;

    // G16: pre-zero bank 5 (pages 10+11, 16 KB) before any of the optional
    // ULA / LoRes / HiRes / HiColour screen-format ingest paths runs. They
    // each write to page 10 offset 0 with 6912 / 12288 / 12288 / 12288
    // bytes respectively, leaving the residual upper portion of bank 5 with
    // stale RAM contents from before the load — observable as attribute /
    // pixel leak in shadow-screen and certain ULA modes. The Layer 2
    // screen ingest writes pages 16-21 (banks 8/9/10), so bank 5 is not
    // in that range and Layer 2 remains untouched. See header doc-comment
    // for full rationale (BEAST-NEX-INVESTIGATION.md § Verdict).
    zero_bank5_screen_pages(mmu);

    // Seed the tilemap palette with the ULA classic palette.
    //
    // nexload2.asm calls setupBeforeBlockLoading ONCE, unconditionally, at
    // :293-294 — before it has even looked at which screen kind the file
    // declares. That routine resets the ULA classic palette and then
    // re-points NR 0x43 at the tilemap palette ($30) and writes the SAME
    // classic 16-colour table again (:830-836, "set ULA classic palette also
    // to tiles palette"), 16 times over to fill all 256 entries (:849-857,
    // `ld b,16` around a 16-entry table). So EVERY V1.3 load gets this, not
    // just the tilemode screens — a program that later switches to the
    // tilemap layer without loading its own palette finds the classic one,
    // whatever screen the file used.
    //
    // The one gate is PRESERVENEXTREG: setupBeforeBlockLoading returns early
    // at :781-783 (`ld a,(nexHeader.PRESERVENEXTREG): or a: ret nz`) before
    // reaching any of the palette resets, so a register-preserving load must
    // leave the tilemap palette alone.
    //
    // Without this a +128 (no-palette) tilemode NEX renders solid black: the
    // tilemap palette's power-on state really is all-zero (the FPGA's palette
    // dpram2 takes the default init_file_g, dpram2.vhd:44,:63-78, and has no
    // reset port), and unlike the ULA/Layer 2/Sprite palettes jnext does not
    // seed the tilemap one at reset. Routed through the real NextREG path so
    // it goes through the same write/auto-increment machinery the loader's
    // own `nextreg` writes would.
    //
    // Done BEFORE the palette-block ingest below, so a file that DOES carry a
    // palette overwrites all 256 entries and is unaffected — matching
    // nexload2, which likewise seeds first and loads the file palette after.
    if (v13 && header_.preserve_regs == 0) {
        // nexload2.asm:929-933 ulaClassicPalette, as NR 0x41 (8-bit
        // RRRGGGBB) values: the 8 normal then 8 bright Spectrum colours.
        static constexpr uint8_t kUlaClassicPalette[16] = {
            0x00, 0x02, 0xA0, 0xA2, 0x14, 0x16, 0xB4, 0xB6,
            0x00, 0x03, 0xE0, 0xE7, 0x1C, 0x1F, 0xFC, 0xFF,
        };
        nr.write(0x43, 0x30);   // tilemap first palette, auto-increment on
        nr.write(0x40, 0x00);   // index 0
        for (int rep = 0; rep < 16; ++rep) {
            for (uint8_t v : kUlaClassicPalette) nr.write(0x41, v);
        }
        Log::emulator()->debug("NEX: V1.3 — seeded tilemap palette with the ULA classic "
                               "palette (nexload2.asm:830-836)");
    }

    // Loading-screen palette (512 bytes) — carried by Layer 2 AND LoRes
    // screens unless NO_PAL is set (nexload.asm:426-427; see
    // nex_has_palette_block above for the file-size evidence, issue #156).
    //
    // The palette goes to a DIFFERENT bank depending on the screen kind
    // (nexload.asm:429-433):
    //
    //   ld a,(IsLoadingScr):and 4:jr z,.nlores
    //   NEXTREG_nn PALETTE_CONTROL_REGISTER,%00000001 : jr .nl2  ; LoRes
    // .nlores
    //   NEXTREG_nn PALETTE_CONTROL_REGISTER,%00010000            ; Layer 2
    //
    // i.e. a LoRes screen's palette is the ULA-first palette (NR 0x43 bits
    // 6:4 = 000, plus bit 0 = ULANext enable), because LoRes reads the ULA
    // palette; only a Layer 2 screen uses the Layer2-first palette
    // (bits 6:4 = 001).
    if (nex_has_palette_block(sf)) {
        if (offset + 512 > file_data_.size()) {
            Log::emulator()->error("NEX: truncated loading-screen palette data");
            return false;
        }
        const bool lores_pal = (sf & NexHeader::SCREEN_LORES) != 0;
        // V1.3 adds a third destination: a tilemode screen's palette is a
        // TILEMAP palette (nexload2.asm:731-734 selects NR 0x43 =
        // %0'011'000'0), while the two big Layer 2 screens use the
        // Layer2-first palette like the classic Layer 2 one (:735).
        const bool tile_pal = (sf2 == NexHeader::SCREEN2_TILEMODE);
        const uint8_t pal_ctrl =
            tile_pal  ? 0x30
                      : (lores_pal
                             ? 0x01
                             : static_cast<uint8_t>(
                                   static_cast<uint8_t>(PaletteId::LAYER2_FIRST) << 4));
        Log::emulator()->debug("NEX: loading {} palette (512 bytes)",
                               tile_pal ? "Tilemap" : (lores_pal ? "LoRes/ULA" : "Layer2"));
        // nexload.asm:430 / :432 — NR 0x43 selects the write target.
        nr.write(0x43, pal_ctrl);
        auto& pal = emu.palette();
        pal.set_index(0);                        // nexload.asm:434 NR 0x40 = 0
        for (int i = 0; i < 256; ++i) {
            uint8_t lo = file_data_[offset + i * 2];
            uint8_t hi = file_data_[offset + i * 2 + 1];
            // nexload.asm:436-437 — two consecutive NR 0x44 writes per entry
            // (first = RRRGGGBB, second = 9th colour bit in bit 0).
            pal.write_9bit(lo);
            pal.write_9bit(hi & 0x01);
        }
        offset += 512;
    }

    // Layer 2 screen (48K = 256x192x8bpp, loaded into banks 8,9,10)
    if (sf & NexHeader::SCREEN_LAYER2) {
        constexpr size_t L2_SIZE = 49152;
        if (offset + L2_SIZE > file_data_.size()) {
            Log::emulator()->error("NEX: truncated Layer2 screen data");
            return false;
        }
        Log::emulator()->debug("NEX: loading Layer2 screen ({} bytes into banks 8,9,10)", L2_SIZE);
        // Banks 8,9,10 → pages 16,17,18,19,20,21
        write_to_ram(mmu, 16, 0, file_data_.data() + offset, L2_SIZE);
        offset += L2_SIZE;
        // nexload.asm:444-446 — make the freshly-loaded screen VISIBLE.
        //   ld bc,4667:ld a,2:out (c),a   ; port $123B bit 1 = Layer 2 on
        //   NEXTREG_nn 21,SLU+SPRITES_VISIBLE
        //   xor a:out (255),a             ; Timex mode 0
        set_loading_screen_display(emu, NexScreenKind::LAYER2, header_.hires_colour);
    }

    // ULA screen (6912 bytes → bank 5 offset 0 = page 10, offset 0)
    if (sf & NexHeader::SCREEN_ULA) {
        constexpr size_t ULA_SIZE = 6912;
        if (offset + ULA_SIZE > file_data_.size()) {
            Log::emulator()->error("NEX: truncated ULA screen data");
            return false;
        }
        Log::emulator()->debug("NEX: loading ULA screen ({} bytes into bank 5)", ULA_SIZE);
        write_to_ram(mmu, 10, 0, file_data_.data() + offset, ULA_SIZE);
        offset += ULA_SIZE;
        // nexload.asm:459-461 — Layer 2 off, SLU priority, Timex mode 0.
        set_loading_screen_display(emu, NexScreenKind::ULA, header_.hires_colour);
    }

    // LoRes screen (12288 bytes)
    if (sf & NexHeader::SCREEN_LORES) {
        constexpr size_t LORES_SIZE = 12288;
        if (offset + LORES_SIZE > file_data_.size()) {
            Log::emulator()->error("NEX: truncated LoRes screen data");
            return false;
        }
        Log::emulator()->debug("NEX: loading LoRes screen ({} bytes into bank 5)", LORES_SIZE);
        // Two 6144-byte halves at bank-5 0x0000 / 0x2000 (nexload.asm:472,:474).
        write_split_screen_block(mmu, file_data_.data() + offset);
        offset += LORES_SIZE;
        // nexload.asm:475-477 — NR 0x15 bit 7 (LoRes enable) + Timex mode 3.
        //   NEXTREG_nn 21,SLU+SPRITES_VISIBLE+LORES_ENABLE
        //   ld a,3:out (255),a
        set_loading_screen_display(emu, NexScreenKind::LORES, header_.hires_colour);
    }

    // HiRes screen (12288 bytes)
    if (sf & NexHeader::SCREEN_HIRES) {
        constexpr size_t HIRES_SIZE = 12288;
        if (offset + HIRES_SIZE > file_data_.size()) {
            Log::emulator()->error("NEX: truncated HiRes screen data");
            return false;
        }
        Log::emulator()->debug("NEX: loading HiRes screen ({} bytes into bank 5)", HIRES_SIZE);
        // Two 6144-byte halves at bank-5 0x0000 / 0x2000 (nexload.asm:488,:490).
        write_split_screen_block(mmu, file_data_.data() + offset);
        offset += HIRES_SIZE;
        // nexload.asm:491-493 — Timex hi-res mode 6, ink colour from the
        // header's HIRESCOL field: `ld a,(HIRESCOL):and %111000:or 6`.
        set_loading_screen_display(emu, NexScreenKind::HIRES, header_.hires_colour);
    }

    // HiColour screen (12288 bytes)
    if (sf & NexHeader::SCREEN_HICOLOUR) {
        constexpr size_t HICOL_SIZE = 12288;
        if (offset + HICOL_SIZE > file_data_.size()) {
            Log::emulator()->error("NEX: truncated HiColour screen data");
            return false;
        }
        Log::emulator()->debug("NEX: loading HiColour screen ({} bytes into bank 5)", HICOL_SIZE);
        // Two 6144-byte halves at bank-5 0x0000 / 0x2000 (nexload.asm:505,:507).
        write_split_screen_block(mmu, file_data_.data() + offset);
        offset += HICOL_SIZE;
        // nexload.asm:508-510 — Timex hi-colour mode 2.
        set_loading_screen_display(emu, NexScreenKind::HICOLOUR, header_.hires_colour);
    }

    // V1.3 big Layer 2 screens (320x256x8bpp / 640x256x4bpp), 81920 bytes.
    // nexload2.asm:623-624 loads 10 consecutive $2000 MMU pages starting at
    // LAYER2_BANK*2 = page 18, i.e. banks 9,10,11,12,13 — NOT banks 8,9,10
    // where the classic 256x192 Layer 2 screen goes.
    if (sf2 == NexHeader::SCREEN2_L2_320x256 || sf2 == NexHeader::SCREEN2_L2_640x256) {
        const bool is320 = (sf2 == NexHeader::SCREEN2_L2_320x256);
        if (offset + L2_BIG_SIZE > file_data_.size()) {
            Log::emulator()->error("NEX: truncated Layer2 {} screen data",
                                   is320 ? "320x256" : "640x256");
            return false;
        }
        Log::emulator()->debug("NEX: loading Layer2 {} screen ({} bytes into banks 9-13)",
                               is320 ? "320x256" : "640x256", L2_BIG_SIZE);
        write_to_ram(mmu, 18, 0, file_data_.data() + offset, L2_BIG_SIZE);
        offset += L2_BIG_SIZE;

        // Make it visible (nexload2.asm:699-718). The Layer 2 clip window is
        // 0,159,0,255 — the X pair is in the 320-mode's own half-resolution
        // units, so 159 IS full width, not a narrower window (:708-715).
        nr.write(0x1C, 0x01);   // reset the Layer 2 clip-window index
        nr.write(0x18, 0x00);
        nr.write(0x18, 0x9F);   // 159
        nr.write(0x18, 0x00);
        nr.write(0x18, 0xFF);
        // NR 0x70 = resolution | palette offset, the offset being the
        // HiRes-colour byte's low nibble ("HiRes color value 0..15 is used as
        // L2 palette offset", :716-718).
        const uint8_t nr70 =
            static_cast<uint8_t>((header_.hires_colour & 0x0F) | (is320 ? 0x10 : 0x20));
        nr.write(0x70, nr70);
        nr.write(0x12, 9);      // LAYER2_BANK (:679)
        nr.write(0x69, 0x80);   // Layer 2 visible; NR69 is SET, not OR'd (:680-686)
        Log::emulator()->debug("NEX: V1.3 Layer2 {} screen shown (NR70={:#04x})",
                               is320 ? "320x256" : "640x256", nr70);
    } else if (sf2 == NexHeader::SCREEN2_TILEMODE) {
        // No data block of its own — the tilemap and tile definitions arrive
        // as ordinary bank 5 content (nexload2.asm:133). Only the four
        // configuration bytes are applied, in header order = NR 0x6B, 0x6C,
        // 0x6E, 0x6F (:688-697).
        nr.write(0x6B, header_.tilemode_cfg[0]);
        nr.write(0x6C, header_.tilemode_cfg[1]);
        nr.write(0x6E, header_.tilemode_cfg[2]);
        nr.write(0x6F, header_.tilemode_cfg[3]);
        nr.write(0x69, 0x00);
        Log::emulator()->debug("NEX: V1.3 tilemode screen shown (NR 6B={:#04x} 6C={:#04x} "
                               "6E={:#04x} 6F={:#04x})",
                               header_.tilemode_cfg[0], header_.tilemode_cfg[1],
                               header_.tilemode_cfg[2], header_.tilemode_cfg[3]);
    }

    // V1.3 copper code block: 2048 bytes immediately after the last screen
    // data block. nexload2.asm:755-768 streams every byte through NR 0x63
    // (which auto-increments the copper write address, left at 0 by the
    // NR 0x62/0x61 zero writes in the NextREG reset above) and then starts
    // the copper with control code %01 = reset CPC to 0 and run.
    if (v13 && header_.copper_flag) {
        if (offset + COPPER_BLOCK_SIZE > file_data_.size()) {
            Log::emulator()->error("NEX: truncated copper code block");
            return false;
        }
        const uint8_t* copper_block = file_data_.data() + offset;
        for (size_t i = 0; i < COPPER_BLOCK_SIZE; ++i) {
            nr.write(0x63, copper_block[i]);
        }
        nr.write(0x62, 0x40);
        offset += COPPER_BLOCK_SIZE;
        Log::emulator()->debug("NEX: V1.3 copper block loaded ({} bytes) and started",
                               COPPER_BLOCK_SIZE);
    }

    // V1.3 expansion bus (header offset 142): 0 = disable it by clearing the
    // top four bits of NR 0x80, 1 = leave it alone (nexload2.asm:778-780).
    // Gated on V1.3 because in a V1.0-V1.2 file offset 142 is reserved space
    // that is zero — which would read as "disable" for every older file.
    if (v13 && header_.expansion_bus == 0) {
        const uint8_t nr80 = nr.read(0x80);
        nr.write(0x80, static_cast<uint8_t>(nr80 & 0x0F));
        Log::emulator()->debug("NEX: V1.3 expansion bus disabled (NR80 {:#04x} -> {:#04x})",
                               nr80, nr80 & 0x0F);
    }

    // ---------------------------------------------------------------
    // 3. Bank data (16K each, in kBankOrder) + loading bar (G156)
    //
    // `d` is the bank-slot index nexload.asm calls `progress` with
    // (nexload.asm:534-545) — kBankOrder[d] is present-or-not, but the
    // mark is drawn on EVERY slot when loading_bar != 0, matching the
    // reference loader exactly (see nex_loader.h citations).
    // ---------------------------------------------------------------

    // load() resolved where bank data actually begins — normally right after
    // the blocks walked above, but a V1.3 header may state an offset past
    // block types this loader does not know (header field 144). Re-seat the
    // read cursor on it so any such gap is skipped rather than mis-read.
    if (bank_data_offset_ < 512 || bank_data_offset_ - 512 > file_data_.size()) {
        Log::emulator()->error("NEX: bank data offset {} is outside the loaded region",
                               bank_data_offset_);
        return false;
    }
    offset = static_cast<size_t>(bank_data_offset_ - 512);

    // The loading bar is drawn by the LOADER, and V1.3 files are not loaded
    // by the firmware nexload.asm that render_progress_mark() models — that
    // loader cannot parse V1.3 at all. ped7g's nexload2.asm draws a different
    // bar in a different place (nexload2.asm:489-509, BIGL2BARPOSY), which
    // jnext does not model. Drawing the tbblue one anyway would be actively
    // wrong: it scribbles into physical bank 11, which for the two big
    // Layer 2 screens is the MIDDLE OF THE VISIBLE PICTURE. So V1.3 gets no
    // bar rather than the wrong bar.
    const bool draw_loading_bar = header_.loading_bar && !v13;

    int banks_loaded = 0;
    for (size_t d = 0; d < static_cast<size_t>(kTotalBankSlots); ++d) {
        const int bank = kBankOrder[d];

        if (bank < 112 && header_.banks[bank]) {
            constexpr size_t BANK_SIZE = 16384;
            if (offset + BANK_SIZE > file_data_.size()) {
                Log::emulator()->error("NEX: truncated bank {} data (offset {} + {} > {})",
                                       bank, offset, BANK_SIZE, file_data_.size());
                return false;
            }

            // Bank N → 8K pages N*2 and N*2+1
            uint16_t page_lo = static_cast<uint16_t>(bank * 2);
            write_to_ram(mmu, page_lo, 0, file_data_.data() + offset, BANK_SIZE);

            Log::emulator()->debug("NEX: loaded bank {} (pages {}, {})", bank, page_lo, page_lo + 1);
            offset += BANK_SIZE;
            ++banks_loaded;
        }

        // nexload.asm draws the progress mark AFTER (any) bank data for
        // this slot has been loaded — including the case where `bank`
        // IS 11 itself (LAYER_2_PAGE_2), whose freshly-loaded tail bytes
        // get overwritten by its own mark. That is real firmware
        // behaviour, not a jnext bug; faithfully reproduced by ordering
        // this call after the bank-data write above.
        if (draw_loading_bar) {
            render_progress_mark(mmu, static_cast<uint8_t>(d), header_.loading_bar_colour);
        }
    }

    Log::emulator()->debug("NEX: loaded {} banks ({} KB)", banks_loaded, banks_loaded * 16);

    // G156 — total frames to hold the CPU idle before the loaded program
    // starts (inter-bank loading_delay total, gated on screen presence,
    // plus the unconditional start_delay). See nex_loader.h citations.
    {
        const bool screen_present = (sf != 0);
        // V1.3 is loaded by nexload2.asm, not nexload.asm, and that loader
        // charges the per-bank delay once per bank ACTUALLY LOADED and
        // without the screen-present gate. See boot_hold_frames_v13().
        const uint32_t hold_frames =
            v13 ? boot_hold_frames_v13(header_.start_delay,
                                       static_cast<size_t>(banks_loaded),
                                       header_.loading_delay)
                : boot_hold_frames(header_.start_delay, screen_present, header_.loading_delay);
        if (hold_frames > 0) {
            emu.set_boot_hold_frames(hold_frames);
            Log::emulator()->info(
                "NEX: holding CPU for {} frames before entry (loading_delay={} "
                "screen_present={} start_delay={} banks_loaded={} v13={})",
                hold_frames, header_.loading_delay, screen_present, header_.start_delay,
                banks_loaded, v13);
        }
    }

    // ---------------------------------------------------------------
    // 4. CPU setup
    // ---------------------------------------------------------------

    if (header_.preserve_regs == 0) {
        // Reset all registers before setting PC/SP
        Z80Registers regs{};
        regs.AF  = 0xFFFF;
        regs.SP  = header_.sp;
        regs.PC  = header_.pc;
        regs.IM  = 1;
        regs.IFF1 = 0;
        regs.IFF2 = 0;
        cpu.set_registers(regs);
        Log::emulator()->debug("NEX: registers reset, PC={:#06x} SP={:#06x}", header_.pc, header_.sp);
    } else {
        auto regs = cpu.get_registers();
        regs.PC = header_.pc;
        regs.SP = header_.sp;
        cpu.set_registers(regs);
        Log::emulator()->debug("NEX: registers preserved, PC={:#06x} SP={:#06x}", header_.pc, header_.sp);
    }

    // ---------------------------------------------------------------
    // 5. Border colour (via ULA port 0xFE)
    // ---------------------------------------------------------------

    emu.port().out(0x00FE, header_.border_colour & 0x07);
    Log::emulator()->debug("NEX: border colour set to {}", header_.border_colour & 0x07);

    // ---------------------------------------------------------------
    // 6. Map entry_bank to MMU slots 6+7 (0xC000-0xFFFF)
    // ---------------------------------------------------------------

    uint8_t page_slot6 = static_cast<uint8_t>(header_.entry_bank * 2);
    uint8_t page_slot7 = static_cast<uint8_t>(header_.entry_bank * 2 + 1);
    mmu.set_page(6, page_slot6);
    mmu.set_page(7, page_slot7);
    Log::emulator()->debug("NEX: entry_bank {} mapped to slots 6,7 (pages {}, {})",
                          header_.entry_bank, page_slot6, page_slot7);

    // ---------------------------------------------------------------
    // 7. V1.3 CLI buffer (header offsets 148/150)
    //
    // "when address and size are provided, the original argument line passed
    // to NEX loader will be copied to defined buffer ... and register DE is
    // set to the buffer address". nexload2.asm:376-389 does this AFTER the
    // entry bank is paged in, which is why it lives here at the end.
    //
    // jnext has no way to pass an argument line to a NEX yet (no CLI surface
    // for it), so the line it delivers is the empty one: a single zero
    // terminator, which is a valid terminated BASIC line per the spec
    // ("shorter string may be zero/colon/enter terminated as any other BASIC
    // line"). DE is still set, so a program that reads its arguments finds a
    // well-formed empty line rather than whatever RAM happened to hold.
    // ---------------------------------------------------------------

    if (v13 && header_.cli_buffer_addr != 0 && header_.cli_buffer_size != 0) {
        // The spec caps the buffer at 2048 bytes; honour that cap rather
        // than trusting an over-large header value.
        constexpr uint16_t CLI_BUFFER_MAX = 2048;
        uint16_t size = header_.cli_buffer_size;
        if (size > CLI_BUFFER_MAX) {
            Log::emulator()->warn("NEX: V1.3 cli_buffer_size={} exceeds the 2048-byte maximum; "
                                  "clamping", size);
            size = CLI_BUFFER_MAX;
        }
        mmu.write(header_.cli_buffer_addr, 0x00);

        auto regs = cpu.get_registers();
        regs.DE = header_.cli_buffer_addr;
        cpu.set_registers(regs);

        Log::emulator()->info("NEX: V1.3 CLI buffer at {:#06x} ({} bytes), DE set; jnext passes "
                              "no argument line, so an empty one was written",
                              header_.cli_buffer_addr, size);
    }

    return true;
}
