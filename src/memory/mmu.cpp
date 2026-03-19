#include "mmu.h"
#include <cstring>

// Reset MMU state from VHDL zxnext.vhd lines 4611-4618:
// MMU0=0xFF(ROM), MMU1=0xFF(ROM), MMU2=0x0A(bank5 lo), MMU3=0x0B(bank5 hi),
// MMU4=0x04(bank2 lo), MMU5=0x05(bank2 hi), MMU6=0x00(bank0 lo), MMU7=0x01(bank0 hi)
static constexpr uint8_t RESET_PAGES[8] = {0xFF, 0xFF, 0x0A, 0x0B, 0x04, 0x05, 0x00, 0x01};

Mmu::Mmu(Ram& ram, Rom& rom) : ram_(ram), rom_(rom) {
    reset();
}

void Mmu::reset() {
    paging_locked_ = false;
    for (int i = 0; i < 8; ++i) {
        slots_[i] = RESET_PAGES[i];
        read_only_[i] = false;
        rebuild_ptr(i);
    }
    // Slots 0-1 are ROM in reset state
    map_rom(0, 0);
    map_rom(1, 1);
}

void Mmu::rebuild_ptr(int slot) {
    uint8_t page = slots_[slot];
    if (page == 0xFF || read_only_[slot]) {
        // ROM or unmapped
        if (read_only_[slot]) {
            read_ptr_[slot] = rom_.page_ptr(page);
            write_ptr_[slot] = nullptr;
        } else {
            read_ptr_[slot] = nullptr;
            write_ptr_[slot] = nullptr;
        }
    } else {
        uint8_t* p = ram_.page_ptr(page);
        read_ptr_[slot] = p;
        write_ptr_[slot] = p;
    }
}

void Mmu::set_page(int slot, uint8_t page) {
    if (slot < 0 || slot > 7) return;
    slots_[slot] = page;
    read_only_[slot] = false;
    rebuild_ptr(slot);
}

void Mmu::map_rom(int slot, uint8_t rom_page) {
    if (slot < 0 || slot > 7) return;
    slots_[slot] = rom_page;
    read_only_[slot] = true;
    read_ptr_[slot] = rom_.page_ptr(rom_page);
    write_ptr_[slot] = nullptr;
}

void Mmu::map_128k_bank(uint8_t port_7ffd) {
    if (paging_locked_) return;
    uint8_t bank = port_7ffd & 0x07;
    bool rom_select = (port_7ffd >> 4) & 1;
    paging_locked_ = (port_7ffd >> 5) & 1;

    // Slots 6-7: selected RAM bank
    set_page(6, bank * 2);
    set_page(7, bank * 2 + 1);

    // Slots 0-1: ROM or RAM based on bit 4
    if (rom_select) {
        set_page(0, 0x02);  // alt ROM page
        set_page(1, 0x03);
    } else {
        map_rom(0, 0);
        map_rom(1, 1);
    }
}
