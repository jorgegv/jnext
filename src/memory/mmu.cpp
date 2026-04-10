#include "mmu.h"
#include "peripheral/divmmc.h"
#include "core/log.h"
#include "core/saveable.h"
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

    // Slots 0-1: ROM selection
    // 128K: bit 4 selects ROM 0 or 1 (2 ROMs)
    // +3: combines bit 4 with port_1ffd_ bit 2 for 4-ROM selection
    port_7ffd_ = port_7ffd;
    int rom_bank = ((port_1ffd_ >> 2) & 1) << 1 | (rom_select ? 1 : 0);
    map_rom(0, rom_bank * 2);
    map_rom(1, rom_bank * 2 + 1);
}

void Mmu::map_plus3_bank(uint8_t port_1ffd) {
    if (paging_locked_) {
        Log::memory()->trace("+3 bank switch ignored (paging locked)");
        return;
    }
    Log::memory()->debug("+3 bank switch: port_1ffd={:#04x}", port_1ffd);
    port_1ffd_ = port_1ffd;

    bool special_mode = (port_1ffd & 0x01) != 0;

    if (special_mode) {
        // Special paging: 4 fixed configurations based on bits 2:1
        uint8_t config = (port_1ffd >> 1) & 0x03;
        // Config 0: RAM 0,1,2,3  Config 1: RAM 4,5,6,7
        // Config 2: RAM 4,5,6,3  Config 3: RAM 4,7,6,3
        static const uint8_t configs[4][4] = {
            {0, 1, 2, 3}, {4, 5, 6, 7}, {4, 5, 6, 3}, {4, 7, 6, 3}
        };
        for (int seg = 0; seg < 4; ++seg) {
            uint8_t bank = configs[config][seg];
            set_page(seg * 2,     bank * 2);
            set_page(seg * 2 + 1, bank * 2 + 1);
        }
    } else {
        // Normal paging: bit 2 selects ROM high bit (combined with 0x7FFD bit 4)
        // ROM number = (port_1ffd bit 2) << 1 | (port_7ffd bit 4)
        int rom_bank = ((port_1ffd >> 2) & 1) << 1 | ((port_7ffd_ >> 4) & 1);
        map_rom(0, rom_bank * 2);
        map_rom(1, rom_bank * 2 + 1);
    }
}

// ---------------------------------------------------------------------------
// State serialisation
// ---------------------------------------------------------------------------

void Mmu::save_state(StateWriter& w) const
{
    w.write_bytes(slots_, 8);
    for (int i = 0; i < 8; ++i) w.write_bool(read_only_[i]);
    w.write_bool(paging_locked_);
    w.write_u8(port_7ffd_);
    w.write_u8(port_1ffd_);
    w.write_bool(l2_write_enable_);
    w.write_u8(l2_segment_mask_);
    w.write_u8(l2_bank_);
    w.write_bool(boot_rom_en_);
}

void Mmu::load_state(StateReader& r)
{
    r.read_bytes(slots_, 8);
    for (int i = 0; i < 8; ++i) read_only_[i] = r.read_bool();
    paging_locked_   = r.read_bool();
    port_7ffd_       = r.read_u8();
    port_1ffd_       = r.read_u8();
    l2_write_enable_ = r.read_bool();
    l2_segment_mask_ = r.read_u8();
    l2_bank_         = r.read_u8();
    boot_rom_en_     = r.read_bool();
    // Rebuild fast-dispatch pointers from restored page/read_only state.
    for (int i = 0; i < 8; ++i) rebuild_ptr(i);
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
