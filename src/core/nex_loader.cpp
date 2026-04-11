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

bool NexLoader::load(const std::string& path)
{
    loaded_ = false;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        Log::emulator()->error("NEX: cannot open '{}'", path);
        return false;
    }

    auto file_size = static_cast<size_t>(f.tellg());
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

    // Read remaining file data (screen + bank payloads)
    size_t data_size = file_size - 512;
    file_data_.resize(data_size);
    f.read(reinterpret_cast<char*>(file_data_.data()), static_cast<std::streamsize>(data_size));

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

    Mmu& mmu = emu.mmu();
    Z80Cpu& cpu = emu.cpu();

    size_t offset = 0;  // current read position in file_data_

    // ---------------------------------------------------------------
    // 1. Screen data
    // ---------------------------------------------------------------

    const uint8_t sf = header_.screen_flags;

    // Layer 2 palette (512 bytes, only if Layer2 screen AND palette present)
    if ((sf & NexHeader::SCREEN_LAYER2) && !(sf & NexHeader::SCREEN_NO_PAL)) {
        if (offset + 512 > file_data_.size()) {
            Log::emulator()->error("NEX: truncated Layer2 palette data");
            return false;
        }
        // Load 256 palette entries into Layer2 first palette via NextREG API.
        // Each entry is 2 bytes: byte 0 = RRRGGGBB, byte 1 = 0000000B (9th bit).
        Log::emulator()->info("NEX: loading Layer2 palette (512 bytes)");
        auto& pal = emu.palette();
        // Select Layer2 first palette for writing, auto-increment enabled
        pal.write_control(static_cast<uint8_t>(PaletteId::LAYER2_FIRST) << 4);
        pal.set_index(0);
        for (int i = 0; i < 256; ++i) {
            uint8_t lo = file_data_[offset + i * 2];
            uint8_t hi = file_data_[offset + i * 2 + 1];
            // Write as two consecutive 9-bit writes (first = RRRGGGBB, second = LSB)
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
        Log::emulator()->info("NEX: loading Layer2 screen ({} bytes into banks 8,9,10)", L2_SIZE);
        // Banks 8,9,10 → pages 16,17,18,19,20,21
        write_to_ram(mmu, 16, 0, file_data_.data() + offset, L2_SIZE);
        offset += L2_SIZE;
    }

    // ULA screen (6912 bytes → bank 5 offset 0 = page 10, offset 0)
    if (sf & NexHeader::SCREEN_ULA) {
        constexpr size_t ULA_SIZE = 6912;
        if (offset + ULA_SIZE > file_data_.size()) {
            Log::emulator()->error("NEX: truncated ULA screen data");
            return false;
        }
        Log::emulator()->info("NEX: loading ULA screen ({} bytes into bank 5)", ULA_SIZE);
        write_to_ram(mmu, 10, 0, file_data_.data() + offset, ULA_SIZE);
        offset += ULA_SIZE;
    }

    // LoRes screen (12288 bytes)
    if (sf & NexHeader::SCREEN_LORES) {
        constexpr size_t LORES_SIZE = 12288;
        if (offset + LORES_SIZE > file_data_.size()) {
            Log::emulator()->error("NEX: truncated LoRes screen data");
            return false;
        }
        Log::emulator()->info("NEX: loading LoRes screen ({} bytes into bank 5)", LORES_SIZE);
        write_to_ram(mmu, 10, 0, file_data_.data() + offset, LORES_SIZE);
        offset += LORES_SIZE;
    }

    // HiRes screen (12288 bytes)
    if (sf & NexHeader::SCREEN_HIRES) {
        constexpr size_t HIRES_SIZE = 12288;
        if (offset + HIRES_SIZE > file_data_.size()) {
            Log::emulator()->error("NEX: truncated HiRes screen data");
            return false;
        }
        Log::emulator()->info("NEX: loading HiRes screen ({} bytes into bank 5)", HIRES_SIZE);
        write_to_ram(mmu, 10, 0, file_data_.data() + offset, HIRES_SIZE);
        offset += HIRES_SIZE;
    }

    // HiColour screen (12288 bytes)
    if (sf & NexHeader::SCREEN_HICOLOUR) {
        constexpr size_t HICOL_SIZE = 12288;
        if (offset + HICOL_SIZE > file_data_.size()) {
            Log::emulator()->error("NEX: truncated HiColour screen data");
            return false;
        }
        Log::emulator()->info("NEX: loading HiColour screen ({} bytes into bank 5)", HICOL_SIZE);
        write_to_ram(mmu, 10, 0, file_data_.data() + offset, HICOL_SIZE);
        offset += HICOL_SIZE;
    }

    // ---------------------------------------------------------------
    // 2. Bank data (16K each, in kBankOrder)
    // ---------------------------------------------------------------

    int banks_loaded = 0;
    for (int bank : kBankOrder) {
        if (bank >= 112) break;
        if (!header_.banks[bank]) continue;

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

    Log::emulator()->info("NEX: loaded {} banks ({} KB)", banks_loaded, banks_loaded * 16);

    // ---------------------------------------------------------------
    // 3. CPU setup
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
        Log::emulator()->info("NEX: registers reset, PC={:#06x} SP={:#06x}", header_.pc, header_.sp);
    } else {
        auto regs = cpu.get_registers();
        regs.PC = header_.pc;
        regs.SP = header_.sp;
        cpu.set_registers(regs);
        Log::emulator()->info("NEX: registers preserved, PC={:#06x} SP={:#06x}", header_.pc, header_.sp);
    }

    // ---------------------------------------------------------------
    // 4. Sprite/layer system setup (NextREG 0x15)
    //
    // The official NextZXOS NEX loader (nexload.asm) sets NextREG 0x15
    // to 0x01 (sprites visible, SLU priority) before launching every
    // NEX program. Replicate this so programs that rely on sprites
    // being enabled by default work correctly.
    // ---------------------------------------------------------------

    emu.nextreg().write(0x15, 0x01);
    Log::emulator()->info("NEX: NextREG 0x15 set to 0x01 (sprites visible, SLU)");

    // ---------------------------------------------------------------
    // 5. Border colour (via ULA port 0xFE)
    // ---------------------------------------------------------------

    emu.port().out(0x00FE, header_.border_colour & 0x07);
    Log::emulator()->info("NEX: border colour set to {}", header_.border_colour & 0x07);

    // ---------------------------------------------------------------
    // 6. Map entry_bank to MMU slots 6+7 (0xC000-0xFFFF)
    // ---------------------------------------------------------------

    uint8_t page_slot6 = static_cast<uint8_t>(header_.entry_bank * 2);
    uint8_t page_slot7 = static_cast<uint8_t>(header_.entry_bank * 2 + 1);
    mmu.set_page(6, page_slot6);
    mmu.set_page(7, page_slot7);
    Log::emulator()->info("NEX: entry_bank {} mapped to slots 6,7 (pages {}, {})",
                          header_.entry_bank, page_slot6, page_slot7);

    return true;
}
