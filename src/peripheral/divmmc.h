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

    /// RETN hook — VHDL divmmc.vhd:126,139 clear automap_hold/automap_held
    /// on i_retn_seen. Must be invoked from the CPU M1 callback when a RETN
    /// (ED 45 or undocumented ED 55/5D/65/6D/75/7D aliases) completes.
    void on_retn();

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

    /// Clear the mapram OR-latch (bit 6 of port 0xE3).
    /// VHDL zxnext.vhd:4184-4185 — writing NR 0x09 with bit 3 set
    /// forces port_e3_reg(6) := '0'. JNEXT exposes this as a dedicated
    /// entry point called by the emulator's NR 0x09 handler.
    void clear_mapram();

    // ── Enable/disable ────────────────────────────────────────────

    /// Enable or disable the entire DivMMC subsystem (legacy, single lever).
    /// VHDL models enable as (port_divmmc_io_en AND nr_0a_divmmc_automap_en);
    /// this convenience setter collapses both independent flags onto a single
    /// boolean, preserving legacy callers.  Any enabled→disabled transition
    /// also clears automap_active_ (mirrors VHDL i_automap_reset path,
    /// divmmc.vhd:126).
    void set_enabled(bool en);
    bool is_enabled() const { return enabled_; }

    /// NA-03 split: individual levers for the two VHDL enable sources.
    /// VHDL zxnext.vhd:4112 — divmmc_automap_reset = (port_divmmc_io_en=0) OR
    /// (nr_0a_divmmc_automap_en=0).  JNEXT models each independently so tests
    /// (and future emulator wiring) can toggle them separately.
    void set_port_io_enable(bool v);
    void set_nr_0a_4_enable(bool v);
    bool port_io_enable() const { return port_io_enable_; }
    bool nr_0a_4_enable() const { return nr_0a_4_enable_; }

    // ── Accessors for debug / testing ─────────────────────────────

    bool conmem() const { return conmem_; }
    bool mapram() const { return mapram_; }
    uint8_t bank() const { return bank_; }
    bool automap_active() const { return automap_active_; }

    // Two-stage automap latch accessors (VHDL divmmc.vhd:123-148).
    // automap_hold_ is set on M1+MREQ-low at an entry-point PC (line 128);
    // automap_held_ latches from hold on MREQ rising edge (line 141). The
    // combinational automap output is (held OR instant_on-matches-this-cycle).
    // Task 7 exposes them for the TM-01..05 tests in divmmc_test.cpp.
    bool automap_hold() const { return automap_hold_; }
    bool automap_held() const { return automap_held_; }

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
    // NA-03: the effective enable is (port_io_enable_ AND nr_0a_4_enable_).
    // set_enabled() writes both for back-compat; the split setters write
    // only one flag each.  apply_enabled_transition_() recomputes enabled_
    // and clears automap_active_ on any true→false edge (DA-08).
    bool port_io_enable_ = false;       // NR 0x83 bit 0 gated port enable
    bool nr_0a_4_enable_ = false;       // NR 0x0A bit 4 automap enable
    bool enabled_ = false;              // = port_io_enable_ AND nr_0a_4_enable_
    void apply_enabled_transition_(bool prev_enabled);
    bool conmem_ = false;               // bit 7 of control register
    bool mapram_ = false;               // bit 6 of control register
    uint8_t bank_ = 0;                  // bits 3:0 of control register
    uint8_t control_reg_ = 0;           // raw control register value

    bool automap_active_ = false;       // combinational: held OR instant_on this-cycle
    // VHDL automap_hold/automap_held two-stage latch (divmmc.vhd:123-148).
    // automap_hold_ is set during the M1 fetch when an entry point matches or
    // when held was on and the current fetch is not an off-trigger; it holds
    // across subsequent non-M1 accesses. automap_held_ promotes from hold on
    // the MREQ rising edge (at the end of the M1's memory cycle) and persists
    // as the base for the next M1's `automap` output.
    bool automap_hold_   = false;
    bool automap_held_   = false;
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
