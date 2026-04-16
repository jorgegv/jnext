#pragma once
#include <cstdint>
#include <string>
#include <vector>

/// DivMMC peripheral — ROM/RAM overlay with auto-mapping.
///
/// Provides an 8K ROM and 16 × 8K RAM pages (128K total) that can be
/// mapped over the normal MMU address space in slots 0 and 1:
///   Slot 0 (0x0000-0x1FFF): DivMMC ROM, or RAM page 3 if mapram is set
///   Slot 1 (0x2000-0x3FFF): DivMMC RAM bank (selected by port 0xE3 bits 3:0)
///
/// Mapping is activated either manually (conmem bit) or automatically when
/// the CPU fetches from certain trigger addresses (auto-map).
///
/// Port 0xE3 — DivMMC control register:
///   bit 7:   conmem — force DivMMC memory mapping on
///   bit 6:   mapram — map RAM page 3 into slot 0 instead of ROM (write-protected)
///   bits 3:0: RAM bank select for slot 1
///
/// Auto-map trigger addresses (M1 cycle only):
///   Map on:   0x0000, 0x0008, 0x0038, 0x0066, 0x04C6, 0x0562
///   Unmap on: 0x1FF8–0x1FFF
///
/// VHDL reference: device/divmmc.vhd
class DivMmc {
public:
    static constexpr int kRomSize     = 0x2000;          // 8K
    static constexpr int kRamPageSize = 0x2000;          // 8K per page
    static constexpr int kRamPages    = 16;
    static constexpr int kRamSize     = kRamPageSize * kRamPages;  // 128K

    DivMmc();

    void reset();

    /// Load DivMMC ROM from file (8K).  Returns true on success.
    bool load_rom(const std::string& path);

    // ── Port 0xE3 handlers ────────────────────────────────────────

    /// Write to port 0xE3 — DivMMC control register.
    void write_control(uint8_t val);

    /// Read from port 0xE3 — returns last written value.
    uint8_t read_control() const;

    // ── Auto-mapping ──────────────────────────────────────────────

    /// Check whether the current PC triggers auto-map or auto-unmap.
    /// Must be called on every M1 (instruction fetch) cycle.
    void check_automap(uint16_t pc, bool is_m1);

    // ── Memory overlay interface ──────────────────────────────────

    /// Returns true if DivMMC memory overlay is active (conmem OR automap).
    bool is_active() const { return enabled_ && (conmem_ || automap_active_); }

    /// Returns true if DivMMC ROM should be mapped at slot 0.
    /// (active AND page0 AND NOT mapram)
    bool is_rom_mapped() const { return is_active() && !mapram_; }

    /// Returns true if DivMMC RAM should be mapped at the given address.
    /// Slot 0 (mapram): page 3;  Slot 1: selected bank.
    bool is_ram_mapped(uint16_t addr) const;

    /// Returns true if the address is read-only in the DivMMC overlay.
    /// Slot 0 is always read-only.  RAM bank 3 is read-only when mapram is set.
    bool is_read_only(uint16_t addr) const;

    /// Read a byte from the DivMMC overlay (addr 0x0000-0x3FFF).
    uint8_t read(uint16_t addr) const;

    /// Write a byte to the DivMMC overlay (addr 0x0000-0x3FFF).
    /// Writes to read-only regions are silently ignored.
    void write(uint16_t addr, uint8_t val);

    // ── Enable/disable ────────────────────────────────────────────

    /// Enable or disable the entire DivMMC subsystem.
    void set_enabled(bool en) { enabled_ = en; }
    bool is_enabled() const { return enabled_; }

    // ── Accessors for debug / testing ─────────────────────────────

    bool conmem() const { return conmem_; }
    bool mapram() const { return mapram_; }
    uint8_t bank() const { return bank_; }
    bool automap_active() const { return automap_active_; }

    const uint8_t* rom_data() const { return rom_.data(); }
    const uint8_t* ram_page(int page) const {
        return ram_.data() + page * kRamPageSize;
    }

    // ── ROM3 active signal (from MMU / SRAM address generator) ───────
    /// Set whether ROM3 is the currently selected ROM.  VHDL
    /// sram_divmmc_automap_rom3_en (zxnext.vhd:3138) gates the ROM3
    /// automap path in divmmc.vhd:130,148.
    void set_rom3_active(bool v) { rom3_active_ = v; }
    bool rom3_active() const { return rom3_active_; }

    // ── NextREG automap configuration (0xB8–0xBB) ──────────────────
    void set_entry_points_0(uint8_t val) { entry_points_0_ = val; }
    void set_entry_valid_0(uint8_t val)  { entry_valid_0_ = val; }
    void set_entry_timing_0(uint8_t val) { entry_timing_0_ = val; }
    void set_entry_points_1(uint8_t val) { entry_points_1_ = val; }

    uint8_t entry_points_0() const { return entry_points_0_; }
    uint8_t entry_valid_0() const  { return entry_valid_0_; }
    uint8_t entry_timing_0() const { return entry_timing_0_; }
    uint8_t entry_points_1() const { return entry_points_1_; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    bool enabled_ = false;              // DivMMC subsystem enabled
    bool conmem_ = false;               // bit 7 of control register
    bool mapram_ = false;               // bit 6 of control register
    uint8_t bank_ = 0;                  // bits 3:0 of control register
    uint8_t control_reg_ = 0;           // raw control register value

    bool automap_active_ = false;       // auto-map currently engaged
    bool rom3_active_    = false;       // ROM3 is current ROM (from MMU/SRAM)

    // NextREG automap configuration (0xB8–0xBB)
    uint8_t entry_points_0_ = 0x83;    // soft reset default
    uint8_t entry_valid_0_  = 0x01;    // soft reset default
    uint8_t entry_timing_0_ = 0x00;    // soft reset default
    uint8_t entry_points_1_ = 0xCD;    // soft reset default

    std::vector<uint8_t> rom_;          // 8K DivMMC ROM
    std::vector<uint8_t> ram_;          // 128K DivMMC RAM (16 × 8K)

    /// Resolve which RAM page is active for the given address.
    int ram_page_for(uint16_t addr) const;
};
