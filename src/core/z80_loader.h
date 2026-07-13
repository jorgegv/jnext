#pragma once

#include "memory/mmu.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Emulator;

/// Parsed `.z80` snapshot header (versions 1, 2, 3).
///
/// Field layout follows the canonical `.z80` FAQ
/// (worldofspectrum.org/faq/reference/z80format.htm), the same document
/// FUSE/CSpect/ZEsarUX implementations trace back to. See `Z80Loader` below
/// for the byte-offset citations used while parsing.
struct Z80Header {
    uint8_t  A = 0, F = 0;
    uint16_t BC = 0, HL = 0, DE = 0;
    uint16_t PC = 0, SP = 0;
    uint8_t  I = 0, R = 0;
    uint8_t  border = 0;           // 0-7
    uint16_t BC2 = 0, DE2 = 0, HL2 = 0;
    uint8_t  A2 = 0, F2 = 0;
    uint16_t IY = 0, IX = 0;
    bool     IFF1 = false, IFF2 = false;
    uint8_t  IM = 0;                // 0, 1, or 2

    int      version = 0;           // 1, 2, or 3 (0 = not yet parsed)
    uint8_t  hardware_mode = 0;     // raw byte 34; only meaningful for v2/v3
    uint8_t  port_7ffd = 0;         // raw byte 35 (offset 0x23); only valid when is_128k
    bool     is_128k = false;
};

/// `.z80` snapshot file loader for the ZX Spectrum emulator (G34).
///
/// Supports v1 (30-byte header, PC != 0), v2 (23-byte extended header) and
/// v3 (54/55-byte extended header) files, 48K and 128K hardware, and both
/// the v1 end-marker-terminated RLE stream and the v2/v3 explicit-length
/// per-page RLE blocks.
///
/// Usage:
///   Z80Loader loader;
///   if (loader.load("game.z80")) {
///       loader.apply(emulator);
///   }
///
/// `load_from_buffer()`, `apply_ram_to_mmu()`, `decompress_v1()` and
/// `decompress_page()` are defined inline here (not in z80_loader.cpp) so
/// that unit tests can exercise the parsing/decompression/RAM-routing logic
/// by linking only `jnext_memory` — the same technique
/// `NexLoader::ram_required_fits()` / `zero_bank5_screen_pages()` use in
/// `nex_loader.h`, and for the same reason: `test/mmu/mmu_test.cpp` does not
/// link `jnext_core` (which pulls in `Emulator`/SDL/Qt). Only `load()` (file
/// I/O + logging) and `apply(Emulator&)` (CPU/port access) live in the .cpp.
class Z80Loader {
public:
    /// Read a file from disk and parse it. Returns true on success.
    bool load(const std::string& path);

    /// Parse an in-memory `.z80` buffer (no file I/O). Returns true on
    /// success; on failure no state from this call is retained (safe to
    /// call apply_ram_to_mmu() afterwards, which will simply return false).
    inline bool load_from_buffer(const std::vector<uint8_t>& buf);

    const Z80Header& header() const { return header_; }
    bool is_loaded() const { return loaded_; }

    /// Apply the loaded snapshot to the emulator: RAM, CPU registers,
    /// border colour, and 128K paging (port 0x7FFD).
    bool apply(Emulator& emu) const;

    /// RAM-only application, usable without an Emulator instance (unit
    /// tests link only `Mmu`/`Ram`/`Rom`). Writes the parsed RAM image(s)
    /// into the physical SRAM pages the plain-48K / 128K bank layout maps
    /// to (bank N -> MMU pages N*2, N*2+1 — same convention as
    /// SnaLoader/SzxLoader). Returns false if load()/load_from_buffer()
    /// has not succeeded.
    inline bool apply_ram_to_mmu(Mmu& mmu) const;

    // ---- Pure parsing helpers, exposed for direct unit testing ----------

    /// v1-style RLE decompression: `ED ED <count> <byte>` run markers,
    /// terminated by the 4-byte end marker `00 ED ED 00` (the v1 body has
    /// no explicit length field, unlike v2/v3 pages). Returns false if the
    /// end marker is never found within `len` bytes, or if the decompressed
    /// size does not equal `expected_size`.
    static inline bool decompress_v1(const uint8_t* data, size_t len,
                                      std::vector<uint8_t>& out,
                                      size_t expected_size = 49152);

    /// v2/v3-style per-page RLE decompression: consumes exactly
    /// `compressed_len` input bytes (the page header supplies the length
    /// explicitly, so no end marker is used). Returns false if the input is
    /// truncated mid-run or the decompressed size does not equal
    /// `page_size`.
    static inline bool decompress_page(const uint8_t* data, size_t compressed_len,
                                        std::vector<uint8_t>& out,
                                        size_t page_size = 16384);

private:
    Z80Header header_{};
    std::vector<uint8_t> v1_ram_;                    // 49152 bytes; version == 1 only
    std::map<uint8_t, std::vector<uint8_t>> pages_;  // version 2/3: z80 page# -> 16384 bytes
    bool loaded_ = false;

    static inline uint16_t read_u16(const uint8_t* p) {
        return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
    }

    /// Parse the v2/v3 per-page block stream (3-byte block header + data,
    /// repeated until `size` bytes are consumed). Populates `pages_`.
    inline bool parse_pages(const uint8_t* data, size_t size);

    /// Write `len` bytes from `src` into a physical 8K RAM page via the
    /// MMU. Temporarily maps the page into slot 7 (0xE000-0xFFFF), writes,
    /// then restores — same technique as SnaLoader/SzxLoader's private
    /// write_to_page() helpers (each loader keeps its own copy; not shared
    /// to avoid a cross-loader header dependency).
    static inline void write_to_page(Mmu& mmu, uint8_t page, const uint8_t* src, size_t len) {
        constexpr int TEMP_SLOT = 7;
        constexpr uint16_t SLOT_BASE = 0xE000;
        uint8_t saved = mmu.get_page(TEMP_SLOT);
        mmu.set_page(TEMP_SLOT, page);
        for (size_t i = 0; i < len; ++i)
            mmu.write(static_cast<uint16_t>(SLOT_BASE + i), src[i]);
        mmu.set_page(TEMP_SLOT, saved);
    }

    /// Write a 16384-byte bank image into MMU pages `start_page` and
    /// `start_page + 1`.
    static inline void write_bank(Mmu& mmu, uint16_t start_page, const uint8_t* src, size_t len) {
        size_t first = std::min(len, static_cast<size_t>(0x2000));
        write_to_page(mmu, static_cast<uint8_t>(start_page), src, first);
        if (len > first) {
            size_t second = std::min(len - first, static_cast<size_t>(0x2000));
            write_to_page(mmu, static_cast<uint8_t>(start_page + 1), src + first, second);
        }
    }
};

// ---------------------------------------------------------------------------
// Inline definitions (see class-body comment for why these live here).
// ---------------------------------------------------------------------------

inline bool Z80Loader::decompress_v1(const uint8_t* data, size_t len,
                                      std::vector<uint8_t>& out, size_t expected_size)
{
    out.clear();
    out.reserve(expected_size);

    size_t i = 0;
    while (i < len) {
        // End marker: 00 ED ED 00 — the v1 block has no explicit length, so
        // this exact 4-byte sequence is the only way to know it's done.
        if (i + 4 <= len && data[i] == 0x00 && data[i + 1] == 0xED &&
            data[i + 2] == 0xED && data[i + 3] == 0x00) {
            return out.size() == expected_size;
        }
        if (i + 1 < len && data[i] == 0xED && data[i + 1] == 0xED) {
            if (i + 4 > len) return false;  // truncated run marker
            uint8_t count = data[i + 2];
            uint8_t value = data[i + 3];
            for (uint8_t k = 0; k < count; ++k) {
                if (out.size() >= expected_size) return false;  // overrun: corrupt
                out.push_back(value);
            }
            i += 4;
        } else {
            if (out.size() >= expected_size) return false;  // overrun: corrupt
            out.push_back(data[i]);
            ++i;
        }
    }
    // Ran off the end of the buffer without ever finding the end marker.
    return false;
}

inline bool Z80Loader::decompress_page(const uint8_t* data, size_t compressed_len,
                                        std::vector<uint8_t>& out, size_t page_size)
{
    out.clear();
    out.reserve(page_size);

    size_t i = 0;
    while (i < compressed_len) {
        if (i + 1 < compressed_len && data[i] == 0xED && data[i + 1] == 0xED) {
            if (i + 4 > compressed_len) return false;  // truncated run marker
            uint8_t count = data[i + 2];
            uint8_t value = data[i + 3];
            for (uint8_t k = 0; k < count; ++k) {
                if (out.size() >= page_size) return false;  // overrun: corrupt
                out.push_back(value);
            }
            i += 4;
        } else {
            if (out.size() >= page_size) return false;  // overrun: corrupt
            out.push_back(data[i]);
            ++i;
        }
    }
    return out.size() == page_size;
}

inline bool Z80Loader::parse_pages(const uint8_t* data, size_t size)
{
    size_t i = 0;
    while (i + 3 <= size) {
        uint16_t block_len = read_u16(data + i);
        uint8_t  page_num  = data[i + 2];
        i += 3;

        if (block_len == 0xFFFF) {
            // Sentinel: 16384 bytes follow uncompressed.
            if (i + 16384 > size) return false;
            pages_[page_num] = std::vector<uint8_t>(data + i, data + i + 16384);
            i += 16384;
        } else {
            if (i + block_len > size) return false;
            std::vector<uint8_t> decoded;
            if (!decompress_page(data + i, block_len, decoded, 16384))
                return false;
            pages_[page_num] = std::move(decoded);
            i += block_len;
        }
    }
    // Trailing bytes that don't form a full 3-byte block header are corrupt.
    if (i != size) return false;
    return !pages_.empty();
}

inline bool Z80Loader::load_from_buffer(const std::vector<uint8_t>& buf)
{
    loaded_ = false;
    header_ = Z80Header{};
    v1_ram_.clear();
    pages_.clear();

    // v1 header is 30 bytes (offsets 0-29). Field offsets below cite the
    // canonical `.z80` FAQ layout.
    if (buf.size() < 30) return false;
    const uint8_t* raw = buf.data();

    header_.A  = raw[0];
    header_.F  = raw[1];
    header_.BC = read_u16(raw + 2);
    header_.HL = read_u16(raw + 4);
    uint16_t pc_v1 = read_u16(raw + 6);      // 0 => version 2/3 sentinel
    header_.SP = read_u16(raw + 8);
    header_.I  = raw[10];

    uint8_t r_low  = raw[11];
    uint8_t flags1 = raw[12];
    // Documented compatibility quirk: some old tools wrote 0xFF here before
    // the R-bit7/border/compressed bits were formalised; readers must treat
    // 0xFF as 0x01 (R bit 7 set, border 0, uncompressed).
    if (flags1 == 0xFF) flags1 = 0x01;
    header_.R      = static_cast<uint8_t>((r_low & 0x7F) | ((flags1 & 0x01) << 7));
    header_.border = static_cast<uint8_t>((flags1 >> 1) & 0x07);
    bool compressed_v1 = (flags1 & 0x20) != 0;

    header_.DE  = read_u16(raw + 13);
    header_.BC2 = read_u16(raw + 15);
    header_.DE2 = read_u16(raw + 17);
    header_.HL2 = read_u16(raw + 19);
    header_.A2  = raw[21];
    header_.F2  = raw[22];
    header_.IY  = read_u16(raw + 23);
    header_.IX  = read_u16(raw + 25);
    header_.IFF1 = raw[27] != 0;
    header_.IFF2 = raw[28] != 0;

    uint8_t flags2 = raw[29];
    header_.IM = flags2 & 0x03;

    if (pc_v1 != 0) {
        // ---- Version 1 ----------------------------------------------
        header_.version = 1;
        header_.PC = pc_v1;
        header_.is_128k = false;

        const size_t body_offset = 30;
        if (buf.size() < body_offset) return false;
        size_t remaining = buf.size() - body_offset;

        if (!compressed_v1) {
            if (remaining < 49152) return false;
            v1_ram_.assign(raw + body_offset, raw + body_offset + 49152);
        } else {
            if (!decompress_v1(raw + body_offset, remaining, v1_ram_, 49152))
                return false;
        }
        loaded_ = true;
        return true;
    }

    // ---- Version 2 / 3: extended header -------------------------------
    // Offset 30-31 (LE): length of the additional header block that
    // follows. 23 = v2; 54 or 55 = v3 (the extra byte in 55 covers R
    // register emulation, not needed here). The length value itself,
    // not a hardcoded 23/54/55 switch, tells us where the memory-page
    // blocks start — see class comment for why we don't need a strict
    // version switch beyond that.
    if (buf.size() < 32) return false;
    uint16_t add_len = read_u16(raw + 30);
    if (add_len < 4) return false;              // need PC(2)+hw(1)+7ffd(1) at minimum
    if (buf.size() < 32u + add_len) return false;

    header_.version        = (add_len == 23) ? 2 : 3;
    header_.PC             = read_u16(raw + 32);
    header_.hardware_mode  = raw[34];
    header_.port_7ffd      = raw[35];
    // Hardware-mode >= 4 is unambiguously 128K-class (128k / 128k+IF1 /
    // 128k+MGT / +3 / Pentagon 128 / Scorpion / ...) in both v2 and v3.
    // Value 3 is the one documented case where sources disagree between
    // v2 ("48k + M.G.T.") and v3 (some references call it SamRam) — jnext
    // implements neither MGT nor SamRam, so both interpretations reduce to
    // "plain 48K RAM layout" here; we deliberately treat hardware_mode==3
    // as 48K-class in both versions. See z80-loader task report for the
    // explicit ambiguity flag.
    header_.is_128k = header_.hardware_mode >= 4;

    const size_t body_offset = 32 + add_len;
    if (!parse_pages(raw + body_offset, buf.size() - body_offset))
        return false;

    loaded_ = true;
    return true;
}

inline bool Z80Loader::apply_ram_to_mmu(Mmu& mmu) const
{
    if (!loaded_) return false;

    if (header_.version == 1) {
        // v1 body layout: 0-16383 -> 0x4000-0x7FFF (bank5, pages 10/11);
        // 16384-32767 -> 0x8000-0xBFFF (bank2, pages 4/5);
        // 32768-49151 -> 0xC000-0xFFFF (bank0, pages 0/1 — 48K default,
        // no real paging in effect). Same bank convention SnaLoader uses.
        write_bank(mmu, 10, v1_ram_.data(), 16384);
        write_bank(mmu, 4,  v1_ram_.data() + 16384, 16384);
        write_bank(mmu, 0,  v1_ram_.data() + 32768, 16384);
        return true;
    }

    if (header_.is_128k) {
        // v2/v3 page numbers 3..10 -> RAM banks 0..7 (canonical `.z80`
        // page table). Pages 0-2 (ROM) and 11 (Multiface ROM) are skipped.
        for (const auto& [page_num, data] : pages_) {
            if (page_num < 3 || page_num > 10) continue;
            uint8_t bank = static_cast<uint8_t>(page_num - 3);
            write_bank(mmu, static_cast<uint16_t>(bank * 2), data.data(), data.size());
        }
    } else {
        // 48K page table: page4 -> 0x8000-0xBFFF (bank2), page5 ->
        // 0xC000-0xFFFF (bank0), page8 -> 0x4000-0x7FFF (bank5).
        auto it4 = pages_.find(4);
        if (it4 != pages_.end()) write_bank(mmu, 4, it4->second.data(), it4->second.size());
        auto it5 = pages_.find(5);
        if (it5 != pages_.end()) write_bank(mmu, 0, it5->second.data(), it5->second.size());
        auto it8 = pages_.find(8);
        if (it8 != pages_.end()) write_bank(mmu, 10, it8->second.data(), it8->second.size());
    }
    return true;
}
