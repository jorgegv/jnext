#include "mmu.h"
#include "peripheral/divmmc.h"
#include "core/log.h"
#include "core/saveable.h"
#include <cstring>

// Reset MMU register view from VHDL zxnext.vhd lines 4611-4618:
// MMU0=0xFF(ROM), MMU1=0xFF(ROM), MMU2=0x0A(bank5 lo), MMU3=0x0B(bank5 hi),
// MMU4=0x04(bank2 lo), MMU5=0x05(bank2 hi), MMU6=0x00(bank0 lo), MMU7=0x01(bank0 hi).
// Slots 0/1 carry the 0xFF ROM sentinel; the physical ROM pages (0 and 1) are
// applied by map_rom_physical() immediately after the loop in reset().
static constexpr uint8_t RESET_PAGES[8] = {0xFF, 0xFF, 0x0A, 0x0B, 0x04, 0x05, 0x00, 0x01};

Mmu::Mmu(Ram& ram, Rom& rom) : ram_(ram), rom_(rom) {
    reset();
}

void Mmu::reset() {
    paging_locked_ = false;
    l2_write_enable_ = false;
    l2_segment_mask_ = 0;
    l2_bank_ = 8;
    // nr_04_romram_bank resets to 0 (VHDL zxnext.vhd:1104). config_mode stays
    // at its current value — it's pushed in by Emulator per machine type so
    // a reset on a 48K/128K/+3 machine doesn't spuriously activate Next-only
    // SRAM routing. Next machines will re-push config_mode=true via the NR
    // 0x03 handler on first write (matches tbblue.fw's boot flow), and via
    // Emulator::init() directly after nextreg_.reset() for power-on parity.
    nr_04_romram_bank_ = 0;
    // Re-enable boot ROM on reset (if loaded) — matches VHDL bootrom_en init.
    if (boot_rom_) boot_rom_en_ = true;
    for (int i = 0; i < 8; ++i) {
        slots_[i] = RESET_PAGES[i];
        nr_mmu_[i] = RESET_PAGES[i];
        read_only_[i] = false;
        rebuild_ptr(i);
    }
    // Slots 0-1 are ROM in reset state; NR 0x50/0x51 stay at the 0xFF sentinel
    // (already seeded above). Use map_rom_physical so nr_mmu_ is untouched.
    map_rom_physical(0, 0);
    map_rom_physical(1, 1);
}

void Mmu::rebuild_ptr(int slot) {
    uint8_t page = slots_[slot];
    if (page == 0xFF || read_only_[slot]) {
        // ROM or unmapped
        if (read_only_[slot]) {
            // VHDL-faithful Next mode serves ROM from SRAM pages 0..7
            // (zxnext.vhd:3052, sram_rom & cpu_a(13)). rom_page is a ROM-
            // area index (0..7), NOT a logical MMU page, so it skips the
            // to_sram_page shift. Other machines use the separate rom_ buffer.
            read_ptr_[slot] = rom_in_sram_ ? ram_.page_ptr(page) : rom_.page_ptr(page);
            write_ptr_[slot] = nullptr;
        } else {
            read_ptr_[slot] = nullptr;
            write_ptr_[slot] = nullptr;
        }
    } else {
        // RAM slot: apply VHDL mmu_A21_A13 shift (Next mode) via to_sram_page.
        uint8_t* p = ram_.page_ptr(to_sram_page(page));
        read_ptr_[slot] = p;
        write_ptr_[slot] = p;
    }
}

void Mmu::set_page(int slot, uint8_t page) {
    if (slot < 0 || slot > 7) return;
    Log::memory()->debug("MMU slot {} → RAM page {:#04x}", slot, page);
    slots_[slot] = page;
    nr_mmu_[slot] = page;
    read_only_[slot] = false;
    rebuild_ptr(slot);
}

void Mmu::map_rom_physical(int slot, uint8_t rom_page) {
    if (slot < 0 || slot > 7) return;
    Log::memory()->debug("MMU slot {} → ROM page {} (physical)", slot, rom_page);
    slots_[slot] = rom_page;
    read_only_[slot] = true;
    // Next mode: read from SRAM pages 0..7 (ROM-in-SRAM). Non-Next: rom_.
    read_ptr_[slot] = rom_in_sram_ ? ram_.page_ptr(rom_page) : rom_.page_ptr(rom_page);
    write_ptr_[slot] = nullptr;
    // Leaves nr_mmu_[slot] unchanged; callers update it as needed.
    // reset() seeds 0xFF (VHDL ROM sentinel); legacy paging callers
    // (map_128k_bank / map_plus3_bank) overwrite with physical page.
}

void Mmu::set_rom_in_sram(bool en) {
    rom_in_sram_ = en;
    // Re-point every slot through rebuild_ptr so the unmapped-sentinel
    // (page==0xFF) and RAM/ROM branches stay consistent with the flag.
    for (int i = 0; i < 8; ++i) rebuild_ptr(i);
}

void Mmu::map_rom(int slot, uint8_t rom_page) {
    map_rom_physical(slot, rom_page);
    // NR 0x50–0x57 register-visible value: an explicit ROM map from an NR
    // write shows the 0xFF sentinel (VHDL zxnext.vhd:4611-4612).
    if (slot >= 0 && slot < 8) nr_mmu_[slot] = 0xFF;
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

    // Slots 6-7: selected RAM bank. Store the LOGICAL page (VHDL
    // mem_active_page semantics); to_sram_page() inside rebuild_ptr()
    // applies the VHDL zxnext.vhd:2964 +0x20 shift for Next mode. Non-
    // Next machines stay at the legacy bank*2 mapping via the pass-through
    // branch in to_sram_page.
    set_page(6, static_cast<uint8_t>(bank * 2));
    set_page(7, static_cast<uint8_t>(bank * 2 + 1));

    // Slots 0-1: ROM selection
    // 128K: bit 4 selects ROM 0 or 1 (2 ROMs)
    // +3: combines bit 4 with port_1ffd_ bit 2 for 4-ROM selection
    port_7ffd_ = port_7ffd;
    int rom_bank = ((port_1ffd_ >> 2) & 1) << 1 | (rom_select ? 1 : 0);
    map_rom_physical(0, rom_bank * 2);
    map_rom_physical(1, rom_bank * 2 + 1);
    // NOTE: VHDL sets MMU0/MMU1 = 0xFF here (normal ROM paging).
    // We store the physical page so ROM bank changes are observable via
    // get_page() in tests/debugger. NR 0x50/0x51 read-back through the
    // nextreg cache is unaffected.
    nr_mmu_[0] = rom_bank * 2;
    nr_mmu_[1] = rom_bank * 2 + 1;
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
        map_rom_physical(0, rom_bank * 2);
        map_rom_physical(1, rom_bank * 2 + 1);
        nr_mmu_[0] = rom_bank * 2;
        nr_mmu_[1] = rom_bank * 2 + 1;
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
    w.write_bool(config_mode_);
    w.write_u8(nr_04_romram_bank_);
    w.write_bool(rom_in_sram_);
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
    config_mode_       = r.read_bool();
    nr_04_romram_bank_ = r.read_u8();
    rom_in_sram_       = r.read_bool();
    // Rebuild fast-dispatch pointers from restored page/read_only state.
    for (int i = 0; i < 8; ++i) rebuild_ptr(i);
    // Re-derive the NR 0x50–0x57 register view from the loaded mapping:
    // ROM-mapped slots show the 0xFF sentinel, RAM-mapped slots show the page.
    // Lossy by design — older save streams did not persist nr_mmu_ separately,
    // so a prior explicit NR 0x50 RAM write followed by a 0x7FFD ROM remap
    // cannot be distinguished from a fresh power-on sentinel.
    for (int i = 0; i < 8; ++i) nr_mmu_[i] = read_only_[i] ? 0xFF : slots_[i];
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
