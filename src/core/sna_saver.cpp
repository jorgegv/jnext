#include "core/sna_saver.h"
#include "core/emulator.h"
#include "core/log.h"

/// Write a little-endian uint16_t to a byte buffer.
static void put_u16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

/// Read `len` bytes from a physical 8K RAM page via the MMU.
/// Temporarily maps the page into slot 7 (0xE000-0xFFFF), reads, then restores.
static void read_from_page(Mmu& mmu, uint8_t page, uint8_t* dst,
                           size_t page_offset, size_t len)
{
    constexpr int TEMP_SLOT = 7;
    constexpr uint16_t SLOT_BASE = 0xE000;

    uint8_t saved = mmu.get_page(TEMP_SLOT);
    mmu.set_page(TEMP_SLOT, page);

    for (size_t i = 0; i < len; ++i) {
        dst[i] = mmu.read(static_cast<uint16_t>(SLOT_BASE + page_offset + i));
    }

    mmu.set_page(TEMP_SLOT, saved);
}

/// Read `len` bytes from RAM starting at the given 8K page.
static void read_from_ram(Mmu& mmu, uint16_t start_page, size_t start_offset,
                          uint8_t* dst, size_t len)
{
    size_t remaining = len;
    size_t dst_pos = 0;
    uint16_t page = start_page;
    size_t page_off = start_offset;

    while (remaining > 0) {
        size_t chunk = std::min(remaining, static_cast<size_t>(0x2000) - page_off);
        read_from_page(mmu, static_cast<uint8_t>(page), dst + dst_pos, page_off, chunk);
        dst_pos += chunk;
        remaining -= chunk;
        ++page;
        page_off = 0;
    }
}

std::vector<uint8_t> SnaSaver::save(Emulator& emu) {
    // 48K SNA: 27-byte header + 49152 bytes RAM
    static constexpr size_t SNA_48K_SIZE = 49179;
    static constexpr size_t HEADER_SIZE = 27;
    static constexpr size_t RAM_SIZE = 49152;

    std::vector<uint8_t> data(SNA_48K_SIZE, 0);

    Mmu& mmu = emu.mmu();
    Z80Cpu& cpu = emu.cpu();
    auto regs = cpu.get_registers();

    // For 48K SNA, PC is pushed onto the stack (destructive to stack).
    // We modify SP to push the return address.
    uint16_t sp = regs.SP;

    // Push PC onto stack
    sp -= 2;
    mmu.write(sp, static_cast<uint8_t>(regs.PC & 0xFF));
    mmu.write(static_cast<uint16_t>(sp + 1), static_cast<uint8_t>(regs.PC >> 8));

    // Build header
    data[0] = regs.I;
    put_u16(data.data() + 1,  regs.HL2);
    put_u16(data.data() + 3,  regs.DE2);
    put_u16(data.data() + 5,  regs.BC2);
    put_u16(data.data() + 7,  regs.AF2);
    put_u16(data.data() + 9,  regs.HL);
    put_u16(data.data() + 11, regs.DE);
    put_u16(data.data() + 13, regs.BC);
    put_u16(data.data() + 15, regs.IY);
    put_u16(data.data() + 17, regs.IX);
    data[19] = regs.IFF2 ? 0x04 : 0x00;
    data[20] = regs.R;
    put_u16(data.data() + 21, regs.AF);
    put_u16(data.data() + 23, sp);
    data[25] = regs.IM;
    data[26] = 0;  // border (we could read it but 0 is fine for RZX)

    // Read RAM: Bank 5 (pages 10,11), Bank 2 (pages 4,5), Bank 0 (pages 0,1)
    read_from_ram(mmu, 10, 0, data.data() + HEADER_SIZE, 16384);
    read_from_ram(mmu, 4, 0, data.data() + HEADER_SIZE + 16384, 16384);
    read_from_ram(mmu, 0, 0, data.data() + HEADER_SIZE + 32768, 16384);

    Log::emulator()->info("SNA saver: saved 48K snapshot ({} bytes)", data.size());
    return data;
}
