#pragma once
#include <cstdint>
#include "ram.h"
#include "rom.h"
#include "cpu/z80_cpu.h"

class Mmu : public MemoryInterface {
public:
    Mmu(Ram& ram, Rom& rom);

    // Reset MMU to power-on state (from VHDL zxnext.vhd)
    void reset();

    // Set slot to page number; rebuilds fast-dispatch pointer
    void set_page(int slot, uint8_t page);
    uint8_t get_page(int slot) const { return slots_[slot]; }

    // Hot-path memory access (inline for performance)
    inline uint8_t read(uint16_t addr) override {
        int slot = addr >> 13;
        const uint8_t* ptr = read_ptr_[slot];
        if (!ptr) return 0xFF;
        return ptr[addr & 0x1FFF];
    }

    inline void write(uint16_t addr, uint8_t val) override {
        // Layer 2 write-over: redirect writes to L2 RAM banks.
        if (l2_write_enable_ && addr < 0xC000) {
            int segment = addr / 0x4000;  // 0, 1, or 2
            if (l2_segment_mask_ & (1 << segment)) {
                // Write to L2 RAM: each segment is 16K = 2 pages.
                // L2 bank N → pages N*2, N*2+1; three consecutive banks.
                uint16_t l2_page = static_cast<uint16_t>((l2_bank_ + segment) * 2);
                uint16_t offset = addr % 0x4000;
                uint8_t* p = ram_.page_ptr(l2_page + (offset >> 13));
                if (p) p[offset & 0x1FFF] = val;
                return;
            }
        }
        int slot = addr >> 13;
        if (read_only_[slot]) return;
        uint8_t* ptr = write_ptr_[slot];
        if (!ptr) return;
        ptr[addr & 0x1FFF] = val;
    }

    // Apply 128K banking: port 0x7FFD value maps slots 0/1/6/7
    void map_128k_bank(uint8_t port_7ffd);

    // Map ROM page into slot (read-only)
    void map_rom(int slot, uint8_t rom_page);

    // ---------------------------------------------------------------
    // Layer 2 write-over control (driven by port 0x123B)
    // ---------------------------------------------------------------

    /// Configure Layer 2 write-mapping from port 0x123B value.
    ///   bit 0: write-map enable
    ///   bits 7:6: segment select (00=0x0000, 01=0x4000, 10=0x8000, 11=all)
    void set_l2_write_port(uint8_t val, uint8_t active_bank);

private:
    void rebuild_ptr(int slot);

    Ram& ram_;
    Rom& rom_;
    uint8_t slots_[8];
    const uint8_t* read_ptr_[8];
    uint8_t*       write_ptr_[8];
    bool           read_only_[8];
    bool           paging_locked_ = false;

    // Layer 2 write-over state
    bool    l2_write_enable_  = false;
    uint8_t l2_segment_mask_  = 0;     // bitmask: bit 0=seg0, bit 1=seg1, bit 2=seg2
    uint8_t l2_bank_          = 8;     // 16K bank base (from NextREG 0x12)
};
