#include "core/szx_loader.h"
#include "core/emulator.h"
#include "core/log.h"

#include <cstring>
#include <fstream>
#include <zlib.h>

/// Read a little-endian uint16_t from a byte buffer.
static uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

/// Read a little-endian uint32_t from a byte buffer.
static uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

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

// ---------------------------------------------------------------------------
// Chunk parsers
// ---------------------------------------------------------------------------

bool SzxLoader::parse_z80r(const uint8_t* data, uint32_t size)
{
    if (size < 37) {
        Log::emulator()->error("SZX: Z80R chunk too small ({} bytes, need 37)", size);
        return false;
    }

    regs_.AF  = read_u16(data + 0);
    regs_.BC  = read_u16(data + 2);
    regs_.DE  = read_u16(data + 4);
    regs_.HL  = read_u16(data + 6);
    regs_.AF2 = read_u16(data + 8);
    regs_.BC2 = read_u16(data + 10);
    regs_.DE2 = read_u16(data + 12);
    regs_.HL2 = read_u16(data + 14);
    regs_.IX  = read_u16(data + 16);
    regs_.IY  = read_u16(data + 18);
    regs_.SP  = read_u16(data + 20);
    regs_.PC  = read_u16(data + 22);
    regs_.I   = data[24];
    regs_.R   = data[25];
    regs_.IFF1 = data[26];
    regs_.IFF2 = data[27];
    regs_.IM  = data[28];
    regs_.tstates = read_u32(data + 29);
    regs_.halted = (data[33] & 0x01) != 0;

    have_z80r_ = true;
    Log::emulator()->debug("SZX: Z80R — PC={:#06x} SP={:#06x} AF={:#06x} IM={}",
                           regs_.PC, regs_.SP, regs_.AF, regs_.IM);
    return true;
}

bool SzxLoader::parse_spcr(const uint8_t* data, uint32_t size)
{
    if (size < 8) {
        Log::emulator()->error("SZX: SPCR chunk too small ({} bytes, need 8)", size);
        return false;
    }

    spcr_.border    = data[0];
    spcr_.port_7ffd = data[1];
    spcr_.port_1ffd = data[2];
    spcr_.port_fe   = data[3];

    have_spcr_ = true;
    Log::emulator()->debug("SZX: SPCR — border={} 7FFD={:#04x} 1FFD={:#04x} FE={:#04x}",
                           spcr_.border, spcr_.port_7ffd, spcr_.port_1ffd, spcr_.port_fe);
    return true;
}

bool SzxLoader::parse_ramp(const uint8_t* data, uint32_t size)
{
    if (size < 3) {
        Log::emulator()->error("SZX: RAMP chunk too small ({} bytes)", size);
        return false;
    }

    uint16_t flags = read_u16(data + 0);
    uint8_t page_num = data[2];
    bool compressed = (flags & 0x01) != 0;

    const uint8_t* page_data = data + 3;
    uint32_t data_len = size - 3;

    constexpr size_t PAGE_SIZE = 16384;

    if (compressed) {
        std::vector<uint8_t> decompressed(PAGE_SIZE);
        uLongf dest_len = PAGE_SIZE;
        int ret = uncompress(decompressed.data(), &dest_len, page_data, data_len);
        if (ret != Z_OK) {
            Log::emulator()->error("SZX: RAMP page {} zlib decompression failed (error {})", page_num, ret);
            return false;
        }
        if (dest_len != PAGE_SIZE) {
            Log::emulator()->error("SZX: RAMP page {} decompressed to {} bytes (expected {})",
                                   page_num, dest_len, PAGE_SIZE);
            return false;
        }
        ram_pages_[page_num] = std::move(decompressed);
    } else {
        if (data_len < PAGE_SIZE) {
            Log::emulator()->error("SZX: RAMP page {} uncompressed data too small ({} bytes)",
                                   page_num, data_len);
            return false;
        }
        ram_pages_[page_num] = std::vector<uint8_t>(page_data, page_data + PAGE_SIZE);
    }

    Log::emulator()->debug("SZX: RAMP page {} loaded ({})", page_num,
                           compressed ? "compressed" : "uncompressed");
    return true;
}

// ---------------------------------------------------------------------------
// Main load
// ---------------------------------------------------------------------------

bool SzxLoader::load(const std::string& path)
{
    loaded_ = false;
    have_z80r_ = false;
    have_spcr_ = false;
    ram_pages_.clear();

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        Log::emulator()->error("SZX: cannot open '{}'", path);
        return false;
    }

    auto file_size = static_cast<size_t>(f.tellg());
    if (file_size < 8) {
        Log::emulator()->error("SZX: file '{}' too small ({} bytes, need >= 8)", path, file_size);
        return false;
    }

    // Read entire file
    std::vector<uint8_t> buf(file_size);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(file_size));

    // Validate magic "ZXST"
    if (std::memcmp(buf.data(), "ZXST", 4) != 0) {
        Log::emulator()->error("SZX: invalid magic in '{}' (expected 'ZXST')", path);
        return false;
    }

    major_version_ = buf[4];
    minor_version_ = buf[5];
    machine_id_    = buf[6];
    flags_         = buf[7];

    Log::emulator()->info("SZX: loading '{}' — version={}.{} machine_id={} flags={:#04x}",
                          path, major_version_, minor_version_, machine_id_, flags_);

    // Iterate chunks
    size_t pos = 8;
    while (pos + 8 <= file_size) {
        char chunk_id[5] = {};
        std::memcpy(chunk_id, buf.data() + pos, 4);
        uint32_t chunk_size = read_u32(buf.data() + pos + 4);

        if (pos + 8 + chunk_size > file_size) {
            Log::emulator()->error("SZX: chunk '{}' at offset {} extends past end of file "
                                   "(size {} + 8 > {} remaining)",
                                   chunk_id, pos, chunk_size, file_size - pos);
            return false;
        }

        const uint8_t* chunk_data = buf.data() + pos + 8;

        if (std::memcmp(chunk_id, "Z80R", 4) == 0) {
            if (!parse_z80r(chunk_data, chunk_size)) return false;
        } else if (std::memcmp(chunk_id, "SPCR", 4) == 0) {
            if (!parse_spcr(chunk_data, chunk_size)) return false;
        } else if (std::memcmp(chunk_id, "RAMP", 4) == 0) {
            if (!parse_ramp(chunk_data, chunk_size)) return false;
        } else {
            Log::emulator()->debug("SZX: skipping unknown chunk '{}' ({} bytes)", chunk_id, chunk_size);
        }

        pos += 8 + chunk_size;
    }

    if (!have_z80r_) {
        Log::emulator()->error("SZX: missing required Z80R chunk");
        return false;
    }

    Log::emulator()->info("SZX: parsed {} RAM pages, Z80R={}, SPCR={}",
                          ram_pages_.size(), have_z80r_ ? "yes" : "no",
                          have_spcr_ ? "yes" : "no");

    loaded_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// Apply snapshot to emulator
// ---------------------------------------------------------------------------

bool SzxLoader::apply(Emulator& emu) const
{
    if (!loaded_) {
        Log::emulator()->error("SZX: apply() called without successful load()");
        return false;
    }

    Mmu& mmu = emu.mmu();
    Z80Cpu& cpu = emu.cpu();

    // ---------------------------------------------------------------
    // 1. RAM pages
    // ---------------------------------------------------------------
    for (const auto& [page_num, page_data] : ram_pages_) {
        // 16K bank N → 8K pages N*2 and N*2+1
        uint16_t page_lo = static_cast<uint16_t>(page_num * 2);
        write_to_ram(mmu, page_lo, 0, page_data.data(), page_data.size());
        Log::emulator()->debug("SZX: loaded RAM page {} (8K pages {}, {})",
                               page_num, page_lo, page_lo + 1);
    }
    Log::emulator()->info("SZX: loaded {} RAM pages ({} KB)",
                          ram_pages_.size(), ram_pages_.size() * 16);

    // ---------------------------------------------------------------
    // 2. Paging configuration (before setting registers)
    // ---------------------------------------------------------------
    if (have_spcr_) {
        // Set border via port 0xFE
        emu.port().out(0x00FE, spcr_.port_fe);
        Log::emulator()->info("SZX: border colour set to {}", spcr_.border & 0x07);

        // 128K paging
        if (machine_id_ >= 2) {
            emu.port().out(0x7FFD, spcr_.port_7ffd);
            Log::emulator()->debug("SZX: port 7FFD = {:#04x}", spcr_.port_7ffd);
        }

        // +2A/+3 paging
        if (machine_id_ >= 4 && machine_id_ <= 6) {
            emu.port().out(0x1FFD, spcr_.port_1ffd);
            Log::emulator()->debug("SZX: port 1FFD = {:#04x}", spcr_.port_1ffd);
        }
    }

    // ---------------------------------------------------------------
    // 3. CPU registers
    // ---------------------------------------------------------------
    Z80Registers regs{};
    regs.AF   = regs_.AF;
    regs.BC   = regs_.BC;
    regs.DE   = regs_.DE;
    regs.HL   = regs_.HL;
    regs.AF2  = regs_.AF2;
    regs.BC2  = regs_.BC2;
    regs.DE2  = regs_.DE2;
    regs.HL2  = regs_.HL2;
    regs.IX   = regs_.IX;
    regs.IY   = regs_.IY;
    regs.SP   = regs_.SP;
    regs.PC   = regs_.PC;
    regs.I    = regs_.I;
    regs.R    = regs_.R;
    regs.IFF1 = regs_.IFF1;
    regs.IFF2 = regs_.IFF2;
    regs.IM   = regs_.IM;
    regs.halted = regs_.halted;
    cpu.set_registers(regs);

    Log::emulator()->info("SZX: registers set — PC={:#06x} SP={:#06x} AF={:#06x} IM={}",
                          regs_.PC, regs_.SP, regs_.AF, regs_.IM);

    return true;
}
