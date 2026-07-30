#include "core/nex_loader.h"
#include "core/emulator.h"
#include "core/log.h"
#include "video/palette.h"

#include <cstring>
#include <fstream>

/// Read a little-endian uint16_t from a byte buffer.
static uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

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
/// V1.3's LOADSCR bit 6 (EXT2) is also paletted (nexload2.asm:70 HASPAL =
/// LAYER2|LORES|EXT2), but the V1.3 screen kinds are not decoded here at all
/// yet — that is issue #162's scope, so bit 6 is deliberately left out.
static bool nex_has_palette_block(uint8_t sf) {
    if (sf & NexHeader::SCREEN_NO_PAL) return false;
    if (sf & (NexHeader::SCREEN_ULA | NexHeader::SCREEN_HIRES | NexHeader::SCREEN_HICOLOUR))
        return false;
    return (sf & (NexHeader::SCREEN_LAYER2 | NexHeader::SCREEN_LORES)) != 0;
}

/// Bytes of optional screen data a NEX header's screen_flags declares, in the
/// exact order and sizes NexLoader::apply() consumes them. Lets load() compute
/// the header-described region (header + screens + banks). Bytes after that
/// boundary remain host-backed for self-streaming NEX files (issues #29/#84).
static size_t nex_screen_bytes(uint8_t sf) {
    size_t n = 0;
    if (nex_has_palette_block(sf))       n += 512;
    if (sf & NexHeader::SCREEN_LAYER2)   n += 49152;
    if (sf & NexHeader::SCREEN_ULA)      n += 6912;
    if (sf & NexHeader::SCREEN_LORES)    n += 12288;
    if (sf & NexHeader::SCREEN_HIRES)    n += 12288;
    if (sf & NexHeader::SCREEN_HICOLOUR) n += 12288;
    return n;
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

    // Validate version string
    if (std::memcmp(header_.version, "V1.0", 4) != 0 &&
        std::memcmp(header_.version, "V1.1", 4) != 0 &&
        std::memcmp(header_.version, "V1.2", 4) != 0) {
        Log::emulator()->warn("NEX: unrecognised version '{:.4s}' in '{}', attempting load anyway",
                              header_.version, path);
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
    const uint64_t expected_size =
        512 + nex_screen_bytes(header_.screen_flags) + declared_banks * 16384;
    payload_offset_ = expected_size;
    if (file_size < expected_size) {
        Log::emulator()->error(
            "NEX: '{}' is truncated — file is {} bytes, header describes {} bytes",
            path, file_size, expected_size);
        return false;
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
    // runs FIRST (nexload.asm:321-410, the `.dontresetregs` fallthrough),
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
        Log::emulator()->debug("NEX: loading {} palette (512 bytes)",
                               lores_pal ? "LoRes/ULA" : "Layer2");
        // nexload.asm:430 / :432 — NR 0x43 selects the write target.
        nr.write(0x43, lores_pal
                           ? 0x01
                           : static_cast<uint8_t>(static_cast<uint8_t>(PaletteId::LAYER2_FIRST) << 4));
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

    // ---------------------------------------------------------------
    // 3. Bank data (16K each, in kBankOrder) + loading bar (G156)
    //
    // `d` is the bank-slot index nexload.asm calls `progress` with
    // (nexload.asm:534-545) — kBankOrder[d] is present-or-not, but the
    // mark is drawn on EVERY slot when loading_bar != 0, matching the
    // reference loader exactly (see nex_loader.h citations).
    // ---------------------------------------------------------------

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
        if (header_.loading_bar) {
            render_progress_mark(mmu, static_cast<uint8_t>(d), header_.loading_bar_colour);
        }
    }

    Log::emulator()->debug("NEX: loaded {} banks ({} KB)", banks_loaded, banks_loaded * 16);

    // G156 — total frames to hold the CPU idle before the loaded program
    // starts (inter-bank loading_delay total, gated on screen presence,
    // plus the unconditional start_delay). See nex_loader.h citations.
    {
        const bool screen_present = (sf != 0);
        const uint32_t hold_frames =
            boot_hold_frames(header_.start_delay, screen_present, header_.loading_delay);
        if (hold_frames > 0) {
            emu.set_boot_hold_frames(hold_frames);
            Log::emulator()->info(
                "NEX: holding CPU for {} frames before entry (loading_delay={} "
                "screen_present={} start_delay={})",
                hold_frames, header_.loading_delay, screen_present, header_.start_delay);
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

    return true;
}
