#pragma once
#include <cstdint>
#include "ram.h"
#include "rom.h"
#include "cpu/z80_cpu.h"
#include "debug/debug_state.h"

class DivMmc;  // forward declaration for overlay

class Mmu : public MemoryInterface {
public:
    Mmu(Ram& ram, Rom& rom);

    // Reset MMU to power-on state (from VHDL zxnext.vhd)
    void reset();

    // Set slot to page number; rebuilds fast-dispatch pointer
    void set_page(int slot, uint8_t page);
    // Returns the NR 0x50–0x57 register-visible value. At reset this matches
    // the VHDL zxnext.vhd:4611-4618 defaults (MMU0/MMU1 = 0xFF ROM sentinel).
    uint8_t get_page(int slot) const { return nr_mmu_[slot]; }
    bool is_slot_rom(int slot) const { return read_only_[slot]; }

    // Boot ROM overlay — highest priority at 0x0000-0x1FFF when enabled.
    // Matches VHDL bootrom_en signal: enabled at power-on, disabled by NextREG 0x03.
    void set_boot_rom(const uint8_t* data, size_t size) {
        boot_rom_ = data;
        boot_rom_size_ = size;
        boot_rom_en_ = (data != nullptr);
    }
    void set_boot_rom_enabled(bool en) { boot_rom_en_ = en; }
    bool boot_rom_enabled() const { return boot_rom_en_; }

    // VHDL nr_03_config_mode + nr_04_romram_bank mirror.
    // When config_mode=1, CPU accesses to 0x0000-0x3FFF on ROM-mapped slots
    // route to SRAM at `(nr_04_romram_bank << 1) | slot` (8 KB pages) instead
    // of the normal ROM serving path. See zxnext.vhd:3044-3050. Writes are
    // permitted through this path (sram_pre_rdonly<='0' at line 3049) — this
    // is how tbblue.fw's load_roms() populates the Spectrum/DivMMC/MF ROMs
    // in SRAM before triggering RESET_SOFT.
    //
    // Priority per VHDL SRAM arbiter (zxnext.vhd:3084-3132). The config_mode
    // branch at line 3050 sets sram_pre_override="110" — DivMMC (bit 2) and
    // Layer 2 (bit 1) overrides stay enabled. The arbiter then picks, in
    // order:
    //   Boot ROM overlay (upstream of the SRAM arbiter, wins over all)
    //   > MF overlay / MMU-RAM slot (pre-arbiter at lines 3029-3043)
    //   > DivMMC ROM/RAM    (arbiter line 3084, 3092)
    //   > Layer 2 write-map (arbiter line 3100)
    //   > ROMCS / Altrom    (arbiter line 3108, 3116)
    //   > sram_pre_A21_A13  (arbiter line 3124 — this is the config_mode or
    //                        normal sram_rom fallthrough)
    // So the C++ hot path checks DivMMC and Layer 2 BEFORE falling into the
    // config_mode routing for ROM slots.
    void set_config_mode(bool enabled) { config_mode_ = enabled; }
    void set_nr_04_romram_bank(uint8_t v) { nr_04_romram_bank_ = v; }

    // Enable VHDL-faithful ROM-in-SRAM serving: ROM-mapped slots read from
    // ram_.page_ptr(rom_page) instead of rom_.page_ptr(). Matches zxnext.vhd:
    // 3052 sram_pre_A21_A13 <= "000000" & sram_rom & cpu_a(13). sram_rom is
    // 2 bits, so the address spans SRAM pages 0..7 (4 × 16 KB ROM banks, the
    // same 64 KB that tbblue's Rom object holds). Called by Emulator::init()
    // for the Next machine AFTER copying rom_ content into ram_ pages 0..7.
    // With this enabled, tbblue.fw's load_roms() writes via config_mode
    // routing and subsequent normal-mode ROM reads both hit the same SRAM
    // pages, which is what makes NextZXOS boot after RESET_SOFT.
    // Default false: 48K/128K/+3/Pentagon keep serving ROM from rom_.
    void set_rom_in_sram(bool en);
    bool rom_in_sram() const { return rom_in_sram_; }

    // DivMMC memory overlay — set by Emulator when DivMMC is initialized.
    // Kept as raw pointer for zero-overhead hot path.
    void set_divmmc(DivMmc* d) { divmmc_ = d; }

    // Debugger state — set by Emulator for data breakpoint checking.
    void set_debug_state(DebugState* ds) { debug_state_ = ds; }

    // Hot-path memory access (inline for performance)
    inline uint8_t read(uint16_t addr) override {
        // Boot ROM overlay: highest priority at 0x0000-0x1FFF (VHDL bootrom_en)
        if (boot_rom_en_ && addr < boot_rom_size_) {
            return boot_rom_[addr];
        }
        // DivMMC overlay: intercept reads from 0x0000-0x3FFF when active.
        // VHDL arbiter (zxnext.vhd:3084) puts DivMMC above the config_mode
        // SRAM routing, so it is checked before the config-mode fallthrough.
        if (divmmc_ && addr < 0x4000) {
            uint8_t val;
            if (divmmc_read(addr, val)) {
                // Check data breakpoints (only when debugger is active with watchpoints)
                if (debug_state_ && debug_state_->active() &&
                    debug_state_->breakpoints().has_any_watchpoints() &&
                    debug_state_->breakpoints().has_watchpoint(addr, WatchType::READ)) {
                    debug_state_->set_data_bp_hit(true);
                    debug_state_->set_data_bp_addr(addr);
                }
                return val;
            }
        }
        int slot = addr >> 13;
        // Config-mode routing (VHDL zxnext.vhd:3044-3050, arbiter line 3124):
        // with nr_03_config_mode=1 on a ROM-mapped 0x0000-0x3FFF slot, the
        // SRAM address comes from nr_04_romram_bank instead of sram_rom.
        // RAM-mapped slots win at zxnext.vhd:3037.
        if (config_mode_ && addr < 0x4000 && read_only_[slot]) {
            const uint8_t* p = ram_.page_ptr((static_cast<uint16_t>(nr_04_romram_bank_) << 1) | slot);
            uint8_t val = p ? p[addr & 0x1FFF] : 0xFF;
            if (debug_state_ && debug_state_->active() &&
                debug_state_->breakpoints().has_any_watchpoints() &&
                debug_state_->breakpoints().has_watchpoint(addr, WatchType::READ)) {
                debug_state_->set_data_bp_hit(true);
                debug_state_->set_data_bp_addr(addr);
            }
            return val;
        }
        const uint8_t* ptr = read_ptr_[slot];
        if (!ptr) return 0xFF;
        uint8_t val = ptr[addr & 0x1FFF];
        // Check data breakpoints (only when debugger is active with watchpoints)
        if (debug_state_ && debug_state_->active() &&
            debug_state_->breakpoints().has_any_watchpoints() &&
            debug_state_->breakpoints().has_watchpoint(addr, WatchType::READ)) {
            debug_state_->set_data_bp_hit(true);
            debug_state_->set_data_bp_addr(addr);
        }
        return val;
    }

    inline void write(uint16_t addr, uint8_t val) override {
        // Check data breakpoints (only when debugger is active with watchpoints)
        if (debug_state_ && debug_state_->active() &&
            debug_state_->breakpoints().has_any_watchpoints() &&
            debug_state_->breakpoints().has_watchpoint(addr, WatchType::WRITE)) {
            debug_state_->set_data_bp_hit(true);
            debug_state_->set_data_bp_addr(addr);
        }
        // DivMMC overlay: intercept writes to 0x0000-0x3FFF when active.
        // Arbiter (zxnext.vhd:3084) puts DivMMC above the config_mode SRAM
        // routing, so DivMMC is checked before the config-mode fallthrough.
        if (divmmc_ && addr < 0x4000) {
            if (divmmc_write(addr, val)) return;
        }
        // Layer 2 write-over: redirect writes to L2 RAM banks. Arbiter line
        // 3100 places Layer 2 above the config_mode path too.
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
        // Config-mode routing (VHDL zxnext.vhd:3044-3050, sram_pre_rdonly<='0'):
        // writes to 0x0000-0x3FFF on ROM-mapped slots route to SRAM at bank
        // nr_04_romram_bank instead of being silently dropped. This is how
        // tbblue.fw's load_roms() populates ROM content in SRAM.
        if (config_mode_ && addr < 0x4000 && read_only_[slot]) {
            uint8_t* p = ram_.page_ptr((static_cast<uint16_t>(nr_04_romram_bank_) << 1) | slot);
            if (p) p[addr & 0x1FFF] = val;
            return;
        }
        if (read_only_[slot]) return;
        uint8_t* ptr = write_ptr_[slot];
        if (!ptr) return;
        ptr[addr & 0x1FFF] = val;
    }

    // Apply 128K banking: port 0x7FFD value maps slots 0/1/6/7
    void map_128k_bank(uint8_t port_7ffd);

    // Last 128K paging register value (for debugger display)
    uint8_t port_7ffd() const { return port_7ffd_; }

    // Apply +3 special paging: port 0x1FFD
    void map_plus3_bank(uint8_t port_1ffd);

    // Currently selected ROM bank 0..3 (VHDL sram_rom signal, derived from
    // port_1ffd bit 2 << 1 | port_7ffd bit 4). Used by Task 7 ROM3-conditional
    // automap gating (zxnext.vhd:3052,3138 — sram_pre_rom3 feeder).
    //
    // Known gaps (Task 7 Branch B scope does not cover — revisit if needed):
    //   * 48K-mode: VHDL zxnext.vhd:2985 hardwires sram_rom3='1' when
    //     machine_type_48='1'. Our implementation reports bank 0 regardless
    //     of machine type. Impact is nil in practice — DivMMC tests target
    //     Next mode, and 48K-mode automap is not a tested path.
    //   * altrom (NR 0x8C): VHDL zxnext.vhd:3138 factors altrom enable into
    //     sram_divmmc_automap_rom3_en. We ignore it — an altrom-masked ROM
    //     bank would still report by its underlying sram_rom bits.
    //   * Next-mode port_1ffd bit 2 is normally gated by NR 0x82 bit 3; a
    //     direct write to port_1ffd on Next mode could make this function
    //     return a ROM3 claim when VHDL would not. Safe in the configured
    //     boot path; fragile if firmware goes off-script.
    uint8_t current_rom_bank() const {
        return static_cast<uint8_t>(((port_1ffd_ >> 2) & 1) << 1 |
                                    ((port_7ffd_ >> 4) & 1));
    }
    bool rom3_selected() const { return current_rom_bank() == 3; }

    // Map ROM page into slot (read-only)
    void map_rom(int slot, uint8_t rom_page);

    // ---------------------------------------------------------------
    // Layer 2 write-over control (driven by port 0x123B)
    // ---------------------------------------------------------------

    /// Configure Layer 2 write-mapping from port 0x123B value.
    ///   bit 0: write-map enable
    ///   bits 7:6: segment select (00=0x0000, 01=0x4000, 10=0x8000, 11=all)
    void set_l2_write_port(uint8_t val, uint8_t active_bank);

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    void rebuild_ptr(int slot);
    // Map a ROM page into a slot without updating nr_mmu_ (callers
    // set nr_mmu_ themselves: reset() seeds 0xFF, legacy paging writes
    // the physical page for test/debugger observability).
    void map_rom_physical(int slot, uint8_t rom_page);

    Ram& ram_;
    Rom& rom_;
    uint8_t slots_[8];      // physical page used by rebuild_ptr
    uint8_t nr_mmu_[8];     // NR 0x50–0x57 register-visible value (may be 0xFF sentinel)
    const uint8_t* read_ptr_[8];
    uint8_t*       write_ptr_[8];
    bool           read_only_[8];
    bool           paging_locked_ = false;
    uint8_t        port_7ffd_ = 0;         // last 128K paging register value
    uint8_t        port_1ffd_ = 0;         // last +3 paging register value

    // Layer 2 write-over state
    bool    l2_write_enable_  = false;
    uint8_t l2_segment_mask_  = 0;     // bitmask: bit 0=seg0, bit 1=seg1, bit 2=seg2
    uint8_t l2_bank_          = 8;     // 16K bank base (from NextREG 0x12)

    // NR 0x03 config_mode + NR 0x04 romram_bank mirror (pushed by Emulator
    // from NextReg handlers). Default false because these signals only exist
    // on the Next; Emulator::init() activates config_mode for ZXN machines
    // after nextreg_.reset() (which sets nr_03_config_mode_=true per VHDL
    // zxnext.vhd:1102). For non-Next machines we must not route through
    // SRAM bank 0 at boot — they have no NextREG.
    bool    config_mode_        = false;
    uint8_t nr_04_romram_bank_  = 0;
    bool    rom_in_sram_        = false;  // serve ROM slots from ram_ pages 0..7 (Next only)

    // Boot ROM overlay (non-owning pointer into Emulator-owned storage)
    const uint8_t* boot_rom_ = nullptr;
    size_t boot_rom_size_ = 0;
    bool boot_rom_en_ = false;

    // DivMMC overlay (non-owning pointer, set by Emulator)
    DivMmc* divmmc_ = nullptr;

    // Debugger state (non-owning pointer, set by Emulator)
    DebugState* debug_state_ = nullptr;

    // Out-of-line DivMMC helpers (defined in mmu.cpp to avoid circular include)
    bool divmmc_read(uint16_t addr, uint8_t& val) const;
    bool divmmc_write(uint16_t addr, uint8_t val);
};
