#include "peripheral/divmmc.h"
#include "core/log.h"
#include <fstream>

// ─── DivMMC logger ────────────────────────────────────────────────────

static std::shared_ptr<spdlog::logger>& divmmc_log() {
    static auto l = [] {
        auto existing = spdlog::get("divmmc");
        if (existing) return existing;
        auto logger = spdlog::stderr_color_mt("divmmc");
        logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        return logger;
    }();
    return l;
}

// ─── DivMmc implementation ────────────────────────────────────────────

DivMmc::DivMmc()
    : rom_(kRomSize, 0xFF)
    , ram_(kRamSize, 0x00)
{
}

void DivMmc::reset() {
    conmem_ = false;
    mapram_ = false;
    bank_ = 0;
    control_reg_ = 0;
    automap_active_ = false;
    entry_points_0_ = 0x83;  // soft reset default
    entry_valid_0_  = 0x01;
    entry_timing_0_ = 0x00;
    entry_points_1_ = 0xCD;
    divmmc_log()->debug("reset");
}

bool DivMmc::load_rom(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        divmmc_log()->error("failed to open ROM file: {}", path);
        return false;
    }

    auto size = f.tellg();
    if (size < kRomSize) {
        divmmc_log()->warn("ROM file too small ({} bytes, expected {}), padding with 0xFF",
                           static_cast<int>(size), kRomSize);
    }
    if (size > kRomSize) {
        divmmc_log()->warn("ROM file too large ({} bytes, expected {}), truncating",
                           static_cast<int>(size), kRomSize);
        size = kRomSize;
    }

    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(rom_.data()), size);

    divmmc_log()->info("loaded ROM: {} ({} bytes)", path, static_cast<int>(size));
    return true;
}

// ── Port 0xE3 ─────────────────────────────────────────────────────────

void DivMmc::write_control(uint8_t val) {
    control_reg_ = val;
    conmem_ = (val & 0x80) != 0;
    mapram_ = (val & 0x40) != 0;
    bank_   = val & 0x0F;

    divmmc_log()->debug("write control={:#04x} conmem={} mapram={} bank={}",
                        val, conmem_, mapram_, bank_);
}

uint8_t DivMmc::read_control() const {
    return control_reg_;
}

// ── Auto-mapping ──────────────────────────────────────────────────────

void DivMmc::check_automap(uint16_t pc, bool is_m1) {
    if (!is_m1 || !enabled_) return;

    // Auto-map trigger addresses from NextREG 0xB8 (entry_points_0_).
    // Each bit enables automap on the corresponding RST address.
    static constexpr uint16_t rst_addrs[8] = {
        0x0000, 0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030, 0x0038
    };
    for (int i = 0; i < 8; ++i) {
        if ((entry_points_0_ & (1 << i)) && pc == rst_addrs[i]) {
            if (!automap_active_) {
                divmmc_log()->debug("automap ON at PC={:#06x}", pc);
            }
            automap_active_ = true;
            return;
        }
    }

    // Auto-map trigger from NextREG 0xBB (entry_points_1_).
    if ((entry_points_1_ & 0x02) && pc == 0x0066) {  // NMI instant
        if (!automap_active_) {
            divmmc_log()->debug("automap ON at PC=0x0066 (NMI)");
        }
        automap_active_ = true;
        return;
    }
    if ((entry_points_1_ & 0x04) && pc == 0x04C6) {  // tape trap (esxdos)
        if (!automap_active_) {
            divmmc_log()->debug("automap ON at PC=0x04C6 (tape trap)");
        }
        automap_active_ = true;
        return;
    }
    if ((entry_points_1_ & 0x08) && pc == 0x0562) {  // tape trap (esxdos)
        if (!automap_active_) {
            divmmc_log()->debug("automap ON at PC=0x0562 (tape trap)");
        }
        automap_active_ = true;
        return;
    }
    if ((entry_points_1_ & 0x10) && pc == 0x04D7) {  // tape trap (nextzxos)
        if (!automap_active_) {
            divmmc_log()->debug("automap ON at PC=0x04D7 (tape trap)");
        }
        automap_active_ = true;
        return;
    }
    if ((entry_points_1_ & 0x20) && pc == 0x056A) {  // tape trap (nextzxos)
        if (!automap_active_) {
            divmmc_log()->debug("automap ON at PC=0x056A (tape trap)");
        }
        automap_active_ = true;
        return;
    }

    // Auto-unmap: addresses 0x1FF8-0x1FFF unless disabled (bit 6 of 0xBB)
    if (!(entry_points_1_ & 0x40) && pc >= 0x1FF8 && pc <= 0x1FFF) {
        if (automap_active_) {
            divmmc_log()->debug("automap OFF at PC={:#06x}", pc);
        }
        automap_active_ = false;
    }
}

// ── Memory overlay ────────────────────────────────────────────────────

bool DivMmc::is_ram_mapped(uint16_t addr) const {
    if (!is_active()) return false;
    if (addr < 0x4000) {
        bool page0 = (addr < 0x2000);
        bool page1 = !page0;
        // From VHDL: ram_en = (page0 AND (conmem OR automap) AND mapram)
        //            OR      (page1 AND (conmem OR automap))
        return (page0 && mapram_) || page1;
    }
    return false;
}

bool DivMmc::is_read_only(uint16_t addr) const {
    if (addr < 0x2000) {
        // Slot 0 is always read-only (ROM, or RAM page 3 with mapram)
        return true;
    }
    // Slot 1: RAM bank 3 is read-only when mapram is set (from VHDL)
    if (addr < 0x4000 && mapram_ && bank_ == 3) {
        return true;
    }
    return false;
}

int DivMmc::ram_page_for(uint16_t addr) const {
    // From VHDL: ram_bank = 3 when page0, else reg(3:0)
    if (addr < 0x2000) {
        return 3;  // slot 0 with mapram → always page 3
    }
    return bank_;  // slot 1 → selected bank
}

uint8_t DivMmc::read(uint16_t addr) const {
    if (addr >= 0x4000) return 0xFF;  // outside DivMMC range

    bool page0 = (addr < 0x2000);

    if (page0) {
        if (mapram_) {
            // mapram: read from RAM page 3
            return ram_[3 * kRamPageSize + (addr & 0x1FFF)];
        } else {
            // Normal: read from DivMMC ROM
            return rom_[addr & 0x1FFF];
        }
    } else {
        // Slot 1: read from selected RAM bank
        return ram_[bank_ * kRamPageSize + (addr & 0x1FFF)];
    }
}

void DivMmc::write(uint16_t addr, uint8_t val) {
    if (addr >= 0x4000) return;  // outside DivMMC range

    bool page0 = (addr < 0x2000);

    if (page0) {
        // Slot 0: writable only when conmem is set AND mapram is NOT set
        // (i.e. writing to the ROM area — which actually writes to... nothing
        // useful, but the VHDL allows it when conmem is set).
        // When mapram is set, page 3 at slot 0 is read-only.
        if (conmem_ && !mapram_) {
            // In the real hardware with conmem, writes go to... the ROM area.
            // Since we model ROM as writable storage when conmem forces it:
            // Actually from VHDL: rdonly = '1' when page0 — slot 0 is ALWAYS
            // read-only regardless of conmem. Writes are simply discarded.
            return;
        }
        // All other cases: slot 0 is read-only
        return;
    }

    // Slot 1: writable unless mapram is set AND bank is 3
    if (mapram_ && bank_ == 3) {
        return;  // read-only
    }

    ram_[bank_ * kRamPageSize + (addr & 0x1FFF)] = val;
}
