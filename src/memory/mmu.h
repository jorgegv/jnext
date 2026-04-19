#pragma once
#include <cstdint>
#include "ram.h"
#include "rom.h"
#include "cpu/z80_cpu.h"
#include "debug/debug_state.h"
#include "memory/contention.h"   // for MachineType

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
                // Next mode: apply VHDL mmu_A21_A13 shift via to_sram_page so
                // L2 write-over lands on the same SRAM region Layer 2's
                // compute_ram_addr fetches from (both shift +0x20 in Next).
                uint16_t l2_page = static_cast<uint16_t>((l2_bank_ + segment) * 2);
                uint16_t offset = addr % 0x4000;
                uint8_t phys_page = to_sram_page(static_cast<uint8_t>(l2_page + (offset >> 13)));
                uint8_t* p = ram_.page_ptr(phys_page);
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

    // Clear the 128K paging lock. VHDL zxnext.vhd:3654-3656 — a write to
    // NR 0x08 with bit 7 set clears port_7ffd_reg(5), which in turn drops
    // port_7ffd_locked (derived at zxnext.vhd:3769) to '0'. Our emulator
    // mirrors this by clearing paging_locked_ directly; the gate inside
    // map_128k_bank / map_plus3_bank then allows subsequent port_7FFD /
    // port_1FFD writes to take effect again. Driven by the NR 0x08 write
    // handler in Emulator::install_port_handlers (src/core/emulator.cpp).
    void unlock_paging() { paging_locked_ = false; }
    // Observable on the 7FFD lock state (used by NR 0x08 read to compose
    // bit 7 = NOT port_7ffd_locked per zxnext.vhd:5906).
    bool paging_locked() const { return paging_locked_; }

    // NR 0x08 bit 6 "contention disable" storage. VHDL zxnext.vhd:5176
    // sets nr_08_contention_disable <= nr_wr_dat(6); the value feeds the
    // ula_contention enable at line 4481 and is read back at line 5906.
    // Branch D will rehome this into ContentionModel; for now Mmu owns
    // the flag so the NR 0x08 write/read handlers have somewhere to store
    // and compose the bit.
    void set_contention_disabled(bool v) { contention_disabled_ = v; }
    bool contention_disabled() const { return contention_disabled_; }

    // ---------------------------------------------------------------
    // NR 0x8C — Alternate ROM (altrom) control
    // ---------------------------------------------------------------
    // VHDL zxnext.vhd:2247-2265 stores the full 8-bit register and exposes
    //   nr_8c_altrom_en        = bit 7
    //   nr_8c_altrom_rw        = bit 6
    //   nr_8c_altrom_lock_rom1 = bit 5
    //   nr_8c_altrom_lock_rom0 = bit 4
    // Read-back (zxnext.vhd:6156) returns the full byte. Hard reset
    // (zxnext.vhd:2255) copies the lower nibble into the upper nibble —
    // bits 3:0 are power-on defaults ('0000' here) that are preserved
    // across reset and become the effective control bits on each reset.
    //
    // This branch adds the register storage + accessors. The altrom
    // address override (zxnext.vhd:2981-3001, 3021) in the SRAM arbiter
    // is NOT implemented here — it is deferred to a follow-up that wires
    // sram_alt_en and the +3/48K/Next branch selection into the read path.
    void    set_nr_8c(uint8_t v) { nr_8c_reg_ = v; }
    uint8_t get_nr_8c() const { return nr_8c_reg_; }
    bool    nr_8c_altrom_en()        const { return (nr_8c_reg_ & 0x80) != 0; }
    bool    nr_8c_altrom_rw()        const { return (nr_8c_reg_ & 0x40) != 0; }
    bool    nr_8c_altrom_lock_rom1() const { return (nr_8c_reg_ & 0x20) != 0; }
    bool    nr_8c_altrom_lock_rom0() const { return (nr_8c_reg_ & 0x10) != 0; }

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

    // Machine-type injection for sram_rom selection. VHDL zxnext.vhd:2981-3008
    // branches on machine_type_48 / machine_type_p3 to select sram_rom:
    //   48K : sram_rom = "00" (always ROM 0, unless altrom locks override)
    //   +3  : sram_rom = nr_8c_altrom_lock_rom1 & _lock_rom0 when any altrom
    //          lock bit is set, else port_1ffd_rom (2-bit, bit1=1ffd(2),
    //          bit0=7ffd(4))
    //   Next: sram_rom = '0' & (altrom_lock_rom1 if any lock) else
    //          '0' & port_1ffd_rom(0)  (1-bit effective; high bit always 0)
    // Pentagon follows the 128K legacy ROM selection (same as Next but
    // without Next-specific altrom semantics).
    //
    // Default ZXN_ISSUE2 — call set_machine_type from Emulator::init.
    void set_machine_type(MachineType t) { machine_type_ = t; }
    MachineType machine_type() const { return machine_type_; }

    // Compute the VHDL sram_rom value (0..3) that the SRAM arbiter would
    // feed into the ROM address (zxnext.vhd:3052 sram_pre_A21_A13 =
    // "000000" & sram_rom & cpu_a(13)) for the currently configured
    // machine type + port_7FFD / port_1FFD / NR 0x8C altrom state.
    //
    // Branch C.3 models the cases:
    //   48K  → always 0 (altrom locks do not select a different ROM bank on
    //          the physical 48K machine; they only override sram_alt_128_n
    //          which the C++ model does not track yet).
    //   128K / PENTAGON → port_7ffd(4) select between ROM 0 / 1.
    //   +3   → 2-bit bank = (port_1ffd(2), port_7ffd(4)); altrom locks
    //          override to (lock_rom1, lock_rom0) when either lock bit is
    //          set per zxnext.vhd:2988-2991.
    //   ZXN  → 1-bit bank = port_1ffd_rom(0) (= bit 0 of current_rom_bank);
    //          altrom lock overrides to (0, lock_rom1) per zxnext.vhd:2998-3001.
    uint8_t current_sram_rom() const {
        switch (machine_type_) {
            case MachineType::ZX48K:
                return 0;
            case MachineType::ZX128K:
            case MachineType::PENTAGON:
                return static_cast<uint8_t>((port_7ffd_ >> 4) & 1);
            case MachineType::ZX_PLUS3:
                if (nr_8c_altrom_lock_rom1() || nr_8c_altrom_lock_rom0()) {
                    return static_cast<uint8_t>((nr_8c_altrom_lock_rom1() ? 2 : 0) |
                                                (nr_8c_altrom_lock_rom0() ? 1 : 0));
                }
                return current_rom_bank();
            case MachineType::ZXN_ISSUE2:
            default:
                if (nr_8c_altrom_lock_rom1() || nr_8c_altrom_lock_rom0()) {
                    return static_cast<uint8_t>(nr_8c_altrom_lock_rom1() ? 1 : 0);
                }
                return static_cast<uint8_t>(current_rom_bank() & 1);
        }
    }

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

    // VHDL zxnext.vhd:2964 mmu_A21_A13 formula: logical MMU page →
    // physical SRAM page. In Next mode (rom_in_sram_=true) the formula
    //   sram = ((1 + (page >> 5)) & 0x0F) << 5 | (page & 0x1F)
    // simplifies to `sram = page + 0x20` for pages 0x00..0xDF (wraps for
    // 0xE0..0xFF — those map to SRAM 0x00..0x1F). firmware NR 0x50-0x57
    // writes use logical pages; port_7FFD bank 0 yields logical 0 which
    // maps to SRAM 0x20 (RAMPAGE_RAMSPECCY), not page 0 (ROM-in-SRAM).
    //
    // Exceptions per VHDL zxnext.vhd:2961-2962: bank 5 (pages 0x0A/0x0B)
    // and bank 7 lower (page 0x0E) bypass the shift — they live in
    // dedicated dual-port VRAM. Our emulator and the ULA VRAM fetch use
    // physical pages 0x0A/0x0B/0x0E for the dual-port banks; matching
    // VHDL exactly means keeping those logical values un-shifted.
    //
    // Public so Layer 2 / tilemap / sprite renderers can match their SRAM
    // fetches to the MMU-shifted layout (otherwise firmware MMU writes go
    // to SRAM page +0x20 but the renderer reads from the un-shifted page).
    // Non-Next mode passes the value through unchanged.
    uint8_t to_sram_page(uint8_t logical) const {
        if (!rom_in_sram_) return logical;
        if (logical == 0x0A || logical == 0x0B || logical == 0x0E) return logical;
        return static_cast<uint8_t>(logical + 0x20);
    }

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
    // VHDL nr_08_contention_disable (zxnext.vhd:1114 default '0', written
    // at zxnext.vhd:5176, read back at zxnext.vhd:5906). Stored here so the
    // NR 0x08 read handler can compose bit 6 without reaching into the
    // ContentionModel (Branch D will rehome).
    bool           contention_disabled_ = false;
    // VHDL nr_8c_altrom (zxnext.vhd:387 default X"00", written at
    // zxnext.vhd:2257, read back at zxnext.vhd:6156). Hard reset copies
    // bits 3:0 into bits 7:4 (zxnext.vhd:2255); bits 3:0 themselves are
    // never cleared by reset.
    uint8_t        nr_8c_reg_ = 0;
    // VHDL machine_type_48/p3 in zxnext.vhd:2981-3008 drive sram_rom
    // selection. Stored here so current_sram_rom() can match VHDL per
    // machine type. Default ZXN_ISSUE2 matches Emulator's default Next
    // config; non-Next machines push via set_machine_type().
    MachineType    machine_type_ = MachineType::ZXN_ISSUE2;
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
