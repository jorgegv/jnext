#include "core/sna_loader.h"
#include "core/emulator.h"
#include "core/log.h"

#include <cstring>
#include <fstream>

/// Read a little-endian uint16_t from a byte buffer.
static uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
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

bool SnaLoader::load(const std::string& path)
{
    loaded_ = false;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        Log::emulator()->error("SNA: cannot open '{}'", path);
        return false;
    }

    auto file_size = static_cast<size_t>(f.tellg());
    if (file_size < SNA_48K_SIZE) {
        Log::emulator()->error("SNA: file '{}' too small ({} bytes, need >= {})",
                               path, file_size, SNA_48K_SIZE);
        return false;
    }

    // Read the 27-byte header
    uint8_t raw[SNA_HEADER_SIZE];
    f.seekg(0);
    f.read(reinterpret_cast<char*>(raw), SNA_HEADER_SIZE);

    header_.I    = raw[0];
    header_.HL2  = read_u16(raw + 1);
    header_.DE2  = read_u16(raw + 3);
    header_.BC2  = read_u16(raw + 5);
    header_.AF2  = read_u16(raw + 7);
    header_.HL   = read_u16(raw + 9);
    header_.DE   = read_u16(raw + 11);
    header_.BC   = read_u16(raw + 13);
    header_.IY   = read_u16(raw + 15);
    header_.IX   = read_u16(raw + 17);
    header_.IFF2 = raw[19];
    header_.R    = raw[20];
    header_.AF   = read_u16(raw + 21);
    header_.SP   = read_u16(raw + 23);
    header_.IM   = raw[25];
    header_.border = raw[26];

    // Read 48K RAM dump
    ram48_.resize(SNA_RAM48_SIZE);
    f.read(reinterpret_cast<char*>(ram48_.data()), SNA_RAM48_SIZE);

    // Check for 128K extended format
    is_128k_ = (file_size > SNA_48K_SIZE);
    if (is_128k_) {
        if (file_size < SNA_48K_SIZE + SNA_EXT_HEADER_SIZE) {
            Log::emulator()->error("SNA: 128K file '{}' truncated extended header", path);
            return false;
        }

        uint8_t ext[SNA_EXT_HEADER_SIZE];
        f.read(reinterpret_cast<char*>(ext), SNA_EXT_HEADER_SIZE);
        ext_header_.PC        = read_u16(ext);
        ext_header_.port_7ffd = ext[2];
        ext_header_.trdos     = ext[3];

        // Read remaining banks
        size_t extra_size = file_size - SNA_48K_SIZE - SNA_EXT_HEADER_SIZE;
        if (extra_size > 0) {
            extra_banks_.resize(extra_size);
            f.read(reinterpret_cast<char*>(extra_banks_.data()),
                   static_cast<std::streamsize>(extra_size));
        }

        Log::emulator()->info("SNA: loaded 128K '{}' — PC={:#06x} port_7ffd={:#04x} "
                              "extra_banks={} KB",
                              path, ext_header_.PC, ext_header_.port_7ffd,
                              extra_size / 1024);
    } else {
        Log::emulator()->info("SNA: loaded 48K '{}' — SP={:#06x} IM={} border={}",
                              path, header_.SP, header_.IM, header_.border);
    }

    loaded_ = true;
    return true;
}

bool SnaLoader::apply(Emulator& emu) const
{
    if (!loaded_) {
        Log::emulator()->error("SNA: apply() called without successful load()");
        return false;
    }

    Mmu& mmu = emu.mmu();
    Z80Cpu& cpu = emu.cpu();

    // ---------------------------------------------------------------
    // 1. Load 48K RAM into banks
    // ---------------------------------------------------------------
    // The 49152 bytes contain:
    //   bytes 0-16383:     Bank 5 (0x4000-0x7FFF) -> pages 10,11
    //   bytes 16384-32767: Bank 2 (0x8000-0xBFFF) -> pages 4,5
    //   bytes 32768-49151: Bank at 0xC000-0xFFFF  -> determined by port 0x7FFD

    // Bank 5 (pages 10, 11)
    write_to_ram(mmu, 10, 0, ram48_.data(), 16384);
    Log::emulator()->debug("SNA: loaded bank 5 (pages 10, 11)");

    // Bank 2 (pages 4, 5)
    write_to_ram(mmu, 4, 0, ram48_.data() + 16384, 16384);
    Log::emulator()->debug("SNA: loaded bank 2 (pages 4, 5)");

    if (is_128k_) {
        // 128K: the third 16K block is the bank paged at 0xC000
        uint8_t paged_bank = ext_header_.port_7ffd & 0x07;
        uint16_t paged_page = static_cast<uint16_t>(paged_bank * 2);
        write_to_ram(mmu, paged_page, 0, ram48_.data() + 32768, 16384);
        Log::emulator()->debug("SNA: loaded paged bank {} (pages {}, {})",
                               paged_bank, paged_page, paged_page + 1);

        // Load remaining banks in ascending order (0,1,3,4,6,7 minus paged_bank)
        size_t extra_offset = 0;
        for (int bank = 0; bank < 8; ++bank) {
            if (bank == 2 || bank == 5 || bank == paged_bank) continue;

            if (extra_offset + 16384 > extra_banks_.size()) {
                Log::emulator()->error("SNA: truncated extra bank {} data", bank);
                return false;
            }

            uint16_t page = static_cast<uint16_t>(bank * 2);
            write_to_ram(mmu, page, 0, extra_banks_.data() + extra_offset, 16384);
            Log::emulator()->debug("SNA: loaded bank {} (pages {}, {})",
                                   bank, page, page + 1);
            extra_offset += 16384;
        }

        // Set 128K memory paging
        emu.port().out(0x7FFD, ext_header_.port_7ffd);
        Log::emulator()->info("SNA: 128K paging set to {:#04x}", ext_header_.port_7ffd);
    } else {
        // 48K: third block goes to bank 0 (default at 0xC000) -> pages 0,1
        write_to_ram(mmu, 0, 0, ram48_.data() + 32768, 16384);
        Log::emulator()->debug("SNA: loaded bank 0 (pages 0, 1)");
    }

    // ---------------------------------------------------------------
    // 2. CPU register setup
    // ---------------------------------------------------------------
    Z80Registers regs{};
    regs.AF   = header_.AF;
    regs.BC   = header_.BC;
    regs.DE   = header_.DE;
    regs.HL   = header_.HL;
    regs.AF2  = header_.AF2;
    regs.BC2  = header_.BC2;
    regs.DE2  = header_.DE2;
    regs.HL2  = header_.HL2;
    regs.IX   = header_.IX;
    regs.IY   = header_.IY;
    regs.SP   = header_.SP;
    regs.I    = header_.I;
    regs.R    = header_.R;
    regs.IM   = header_.IM;
    regs.halted = false;

    // IFF1 = IFF2 = bit 2 of the IFF2 field
    uint8_t iff = (header_.IFF2 & 0x04) ? 1 : 0;
    regs.IFF1 = iff;
    regs.IFF2 = iff;

    if (is_128k_) {
        // 128K: PC comes from extended header
        regs.PC = ext_header_.PC;
    } else {
        // 48K: PC is on the stack — pop return address
        uint8_t lo = mmu.read(header_.SP);
        uint8_t hi = mmu.read(static_cast<uint16_t>(header_.SP + 1));
        regs.PC = static_cast<uint16_t>(lo | (hi << 8));
        regs.SP = static_cast<uint16_t>(header_.SP + 2);
        Log::emulator()->info("SNA: 48K popped PC={:#06x} from stack, SP now {:#06x}",
                              regs.PC, regs.SP);
    }

    cpu.set_registers(regs);
    Log::emulator()->info("SNA: CPU set — PC={:#06x} SP={:#06x} AF={:#06x} IM={}",
                          regs.PC, regs.SP, regs.AF, regs.IM);

    // ---------------------------------------------------------------
    // 3. Border colour
    // ---------------------------------------------------------------
    emu.port().out(0x00FE, header_.border & 0x07);
    Log::emulator()->info("SNA: border colour set to {}", header_.border & 0x07);

    return true;
}
