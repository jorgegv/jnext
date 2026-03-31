#include "mmu.h"
#include "peripheral/divmmc.h"
#include "core/log.h"
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
    l2_write_enable_ = false;
    l2_segment_mask_ = 0;
    l2_bank_ = 8;
    // Re-enable boot ROM on reset (if loaded) — matches VHDL bootrom_en init.
    if (boot_rom_) boot_rom_en_ = true;
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
    Log::memory()->debug("MMU slot {} → RAM page {:#04x}", slot, page);
    slots_[slot] = page;
    read_only_[slot] = false;
    rebuild_ptr(slot);
}

void Mmu::map_rom(int slot, uint8_t rom_page) {
    if (slot < 0 || slot > 7) return;
    Log::memory()->debug("MMU slot {} → ROM page {}", slot, rom_page);
    slots_[slot] = rom_page;
    read_only_[slot] = true;
    read_ptr_[slot] = rom_.page_ptr(rom_page);
    write_ptr_[slot] = nullptr;
}

void Mmu::set_config_page(uint8_t bank) {
    // NR 0x04: In config mode, maps a 16K RAM bank into slots 0-1 (writable).
    // The VHDL uses: sram_pre_A21_A13 <= nr_04_romram_bank & cpu_a(13)
    // This concatenates the bank number with address bit 13, effectively:
    //   slot 0 (addr 0x0000-0x1FFF, bit13=0) → 8K page = bank * 2
    //   slot 1 (addr 0x2000-0x3FFF, bit13=1) → 8K page = bank * 2 + 1
    // The firmware uses this to write ROM images into SRAM during boot.
    if (!config_mode_) return;
    uint8_t page0 = bank * 2;
    uint8_t page1 = bank * 2 + 1;
    Log::memory()->debug("Config page: bank {} → slot 0-1 = RAM pages {:#04x},{:#04x} (writable)",
                          bank, page0, page1);
    slots_[0] = page0;
    slots_[1] = page1;
    read_only_[0] = false;
    read_only_[1] = false;
    rebuild_ptr(0);
    rebuild_ptr(1);
}

void Mmu::set_l2_write_port(uint8_t val, uint8_t active_bank) {
    l2_write_enable_ = (val & 0x01) != 0;
    l2_bank_ = active_bank;
    uint8_t seg = (val >> 6) & 0x03;
    switch (seg) {
        case 0: l2_segment_mask_ = 0x01; break;  // 0x0000-0x3FFF
        case 1: l2_segment_mask_ = 0x02; break;  // 0x4000-0x7FFF
        case 2: l2_segment_mask_ = 0x04; break;  // 0x8000-0xBFFF
        case 3: l2_segment_mask_ = 0x07; break;  // all three
    }
    Log::memory()->debug("L2 write-over: enable={} segment_mask={:#04x} bank={}",
                          l2_write_enable_, l2_segment_mask_, l2_bank_);
}

void Mmu::map_128k_bank(uint8_t port_7ffd) {
    if (paging_locked_) {
        Log::memory()->trace("128K bank switch ignored (paging locked)");
        return;
    }
    Log::memory()->debug("128K bank switch: port_7ffd={:#04x}", port_7ffd);
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

// ---------------------------------------------------------------------------
// DivMMC overlay helpers (out-of-line to avoid circular include)
// ---------------------------------------------------------------------------

bool Mmu::divmmc_read(uint16_t addr, uint8_t& val) const {
    if (!divmmc_->is_active()) return false;
    val = divmmc_->read(addr);
    return true;
}

bool Mmu::divmmc_write(uint16_t addr, uint8_t val) {
    if (!divmmc_->is_active()) return false;
    divmmc_->write(addr, val);
    return true;
}
