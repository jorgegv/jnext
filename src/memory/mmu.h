#pragma once
#include <cstdint>
#include "ram.h"
#include "rom.h"

class Mmu {
public:
    Mmu(Ram& ram, Rom& rom);

    // Reset MMU to power-on state (from VHDL zxnext.vhd)
    void reset();

    // Set slot to page number; rebuilds fast-dispatch pointer
    void set_page(int slot, uint8_t page);
    uint8_t get_page(int slot) const { return slots_[slot]; }

    // Hot-path memory access (inline for performance)
    inline uint8_t read(uint16_t addr) const {
        int slot = addr >> 13;
        const uint8_t* ptr = read_ptr_[slot];
        if (!ptr) return 0xFF;
        return ptr[addr & 0x1FFF];
    }

    inline void write(uint16_t addr, uint8_t val) {
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

private:
    void rebuild_ptr(int slot);

    Ram& ram_;
    Rom& rom_;
    uint8_t slots_[8];
    const uint8_t* read_ptr_[8];
    uint8_t*       write_ptr_[8];
    bool           read_only_[8];
    bool           paging_locked_ = false;
};
