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

    // Reset MMU to power-on state (from VHDL zxnext.vhd).
    //
    // The `hard` flag selects the VHDL reset domain:
    //   * hard=true  — the top-level `reset` signal (`reset <= i_RESET`,
    //                  zxnext.vhd:1730). Clears ALL reset-domain state.
    //   * hard=false — soft reset (tbblue RESET_SOFT / NR 0x02 bit 0).
    //                  Preserves state that is only gated on the hard
    //                  `reset='1'` signal in VHDL — specifically:
    //                    - nr_8c_altrom bits 7:4 (zxnext.vhd:2253-2256)
    //                    - nr_08_contention_disable (zxnext.vhd:4935)
    //                    - port_7ffd_reg / paging lock (zxnext.vhd:3646-3648)
    //                  All FF-based state that VHDL clears on both hard
    //                  and soft reset still clears here (slots, nr_04, L2
    //                  write-enable, bootrom_en overlay, etc.).
    //
    // Callers:
    //   * Emulator::init(cfg, preserve_memory=false)  → reset(true)  [hard]
    //   * Emulator::init(cfg, preserve_memory=true)   → reset(false) [soft]
    //   * Emulator::reset()                            → reset(true)  [hard]
    void reset(bool hard);
    // Backwards-compatible overload: defaults to hard reset (power-on
    // semantics). Callers that have not yet been threaded through the
    // hard/soft distinction still behave as before.
    void reset() { reset(true); }

    // Set slot to page number; rebuilds fast-dispatch pointer
    void set_page(int slot, uint8_t page);
    // Returns the NR 0x50–0x57 register-visible value. At reset this matches
    // the VHDL zxnext.vhd:4611-4618 defaults (MMU0/MMU1 = 0xFF ROM sentinel).
    uint8_t get_page(int slot) const { return nr_mmu_[slot]; }
    // Returns the effective physical page backing `slot` — combines the NR
    // 0x50–0x57 explicit override (nr_mmu_[slot] when != 0xFF) with the
    // legacy-paging dynamic resolution (slots_[slot], kept up to date by
    // map_rom_physical / set_page). Use this for tests/debugger observability
    // of ROM-page-after-port-write scenarios where nr_mmu_ holds the VHDL
    // 0xFF ROM sentinel (zxnext.vhd:4611-4612, 4619-4644) but the resolved
    // physical page still matters. Returns 0xFF for out-of-range slot.
    uint8_t get_effective_page(int slot) const {
        if (slot < 0 || slot > 7) return 0xFF;
        return nr_mmu_[slot] != 0xFF ? nr_mmu_[slot] : slots_[slot];
    }
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
        // Layer 2 read-over: redirect reads to L2 RAM banks. VHDL arbiter
        // at zxnext.vhd:3100 puts Layer 2 above config_mode, altrom, MMU,
        // and ROMCS (and below DivMMC ROM/RAM). Mirrors the write-side
        // block below: same segment check, same (l2_bank_+segment)*2
        // page computation, same to_sram_page() shift, same offset
        // arithmetic — both come from the shared layer2_active_page
        // formula at zxnext.vhd:2969.
        if (l2_read_enable_ && addr < 0xC000) {
            int segment = addr / 0x4000;  // 0, 1, or 2
            if (l2_segment_mask_ & (1 << segment)) {
                uint16_t l2_page = static_cast<uint16_t>((l2_bank_ + segment) * 2);
                uint16_t offset = addr % 0x4000;
                uint8_t phys_page = to_sram_page(static_cast<uint8_t>(l2_page + (offset >> 13)));
                const uint8_t* p = ram_.page_ptr(phys_page);
                uint8_t val = p ? p[offset & 0x1FFF] : 0xFF;
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
        // Alt-ROM read override (VHDL zxnext.vhd:3021, 3078, 3116-3123).
        // Fires only on ROM-mapped slots in 0x0000-0x3FFF when config_mode
        // is OFF (VHDL:3050 sets override(0)=0 in config_mode → altrom
        // disabled by VHDL:3078 first clause). Per VHDL:3078 fourth clause,
        // the read path takes altrom ONLY when sram_pre_rdonly=1 — i.e.
        // when altrom_rw=0 (read-only altrom). When altrom_rw=1 the read
        // falls through to the normal ROM/sram_pre path (firmware uses that
        // mode to write-over altrom while keeping reads on the live ROM).
        if (nr_8c_altrom_en() && !nr_8c_altrom_rw() &&
            !config_mode_ && addr < 0x4000 && read_only_[slot]) {
            const uint8_t* p = ram_.page_ptr(altrom_sram_page_(addr));
            uint8_t val = p ? p[addr & 0x1FFF] : 0xFF;
            if (debug_state_ && debug_state_->active() &&
                debug_state_->breakpoints().has_any_watchpoints() &&
                debug_state_->breakpoints().has_watchpoint(addr, WatchType::READ)) {
                debug_state_->set_data_bp_hit(true);
                debug_state_->set_data_bp_addr(addr);
            }
            return val;
        }
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
        // VHDL zxnext.vhd:4498-4509 — p3_floating_bus_dat captures cpu_di
        // on every contended memory read. Approximated here via the
        // per-16K-slot contended flag (slot_contended_[]) pushed by the
        // Emulator alongside ContentionModel::set_contended_slot().
        if (slot_contended_[addr >> 14]) {
            p3_floating_bus_dat_ = val;
        }
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
        // Alt-ROM write override (VHDL zxnext.vhd:3056, 3078, 3116-3123).
        // Fires only on ROM-mapped slots in 0x0000-0x3FFF when config_mode
        // is OFF (config_mode forces override(0)=0 at VHDL:3050 → altrom
        // disabled). Per VHDL:3078, the write path takes altrom ONLY when
        // sram_pre_rdonly=0 — i.e. when altrom_en+altrom_rw=1. The byte
        // lands in the alt-ROM SRAM area selected by altrom_sram_page_().
        // Reads under the same en+rw=1 mode stay on the normal ROM path
        // (VHDL:3078 fourth clause), so this is a true "write-over": the
        // CPU patches alt-ROM without disturbing the visible ROM.
        if (nr_8c_altrom_en() && nr_8c_altrom_rw() &&
            !config_mode_ && addr < 0x4000 && read_only_[slot]) {
            uint8_t* p = ram_.page_ptr(altrom_sram_page_(addr));
            if (p) p[addr & 0x1FFF] = val;
            return;
        }
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
        // VHDL zxnext.vhd:4498-4509 — p3_floating_bus_dat captures cpu_do
        // on every contended memory write. Approximated via per-16K-slot
        // contended flag (see read() for the mirror update on reads).
        if (slot_contended_[addr >> 14]) {
            p3_floating_bus_dat_ = val;
        }
    }

    // Apply 128K banking: port 0x7FFD value maps slots 0/1/6/7
    void map_128k_bank(uint8_t port_7ffd);

    // ---------------------------------------------------------------
    // Port 0xDFFD — Profi / Next extended paging (extra bank bits)
    // ---------------------------------------------------------------
    // VHDL zxnext.vhd:3683-3708 stores cpu_do(4:0) into port_dffd_reg and
    // cpu_do(6) into a separate port_dffd_reg_6 signal. The write is
    // gated by `port_7ffd_locked='0' OR nr_8f_mapping_mode_profi='1'`
    // (VHDL:3691). The register feeds port_7ffd_bank composition at
    // zxnext.vhd:3763-3766:
    //   bank(2:0) = port_7ffd_reg(2:0)      [primary 3 bits of bank]
    //   bank(4:3) = port_dffd_reg(1:0)      [non-Pentagon]
    //   bank(5)   = port_dffd_reg(2)        [non-Pentagon]
    //   bank(6)   = port_dffd_reg(3)        [non-Pentagon, non-Profi]
    // Bit 4 is the Profi mapping-mode DFFD override — VHDL:3797 forces
    // nr_8f_mapping_mode_profi='0' unconditionally in the synthesizable
    // build, so bit 4 is stored but has no downstream effect in JNEXT.
    //
    // On each write the composed bank feeds MMU6/MMU7 via the shared
    // port_memory_change_dly path (VHDL:4619-4682). We mirror that by
    // re-running the bank composition at the end of write_port_dffd so
    // the MMU register view reflects the new extra bits immediately.
    //
    // Decode: VHDL:2596 port_dffd <= A15:12="1101" AND port_fd (A1:0="01").
    // Registered in Emulator::init under mask 0xF003 / match 0xD001.
    void write_port_dffd(uint8_t v);
    // Observable on port_dffd_reg (bits 4:0). Used by tests to verify the
    // 5-bit storage + lock-gating contract. Bit 4 stores the Profi override,
    // bits 0..3 feed the bank composition.
    uint8_t port_dffd_reg() const { return port_dffd_reg_; }

    // ---------------------------------------------------------------
    // Port 0xEFF7 — Pentagon-1024 disable / RAM-at-0x0000
    // ---------------------------------------------------------------
    // VHDL zxnext.vhd:3774-3785 stores cpu_do(2) into port_eff7_reg_2 and
    // cpu_do(3) into port_eff7_reg_3 on each port_eff7_wr pulse. Both
    // flags are cleared on `reset='1'` (VHDL:3778-3779).
    //
    // Effects:
    //   * port_eff7_reg_3 (bit 3): on the next paging-register change,
    //     force RAM-at-0x0000 — MMU0=0x00, MMU1=0x01 — instead of the
    //     default MMU0=MMU1=0xFF ROM sentinel (VHDL:4636-4644).
    //   * port_eff7_reg_2 (bit 2): when NR 0x8F mapping mode = Pentagon-1024
    //     (nr_8f_mapping_mode="11"), EFF7(2)=1 disables the 1024K extension
    //     (nr_8f_mapping_mode_pentagon_1024_en = '0', VHDL:3801). The
    //     pentagon_1024_en signal in turn feeds the lock-override at
    //     VHDL:3769 — EFF7(2)=1 prevents the override from firing.
    //     NR 0x8F handling is not on the Mmu surface (Branch B owns it);
    //     storing the bit here is enough for Branch B to compose the gate.
    //
    // Decode: VHDL:2604 port_eff7 <= A15:12="1110" AND port_f7_lsb=(A7:0="F7").
    // A14:13 are don't-care, so the decode spans 0xE0F7..0xEFF7 — the
    // canonical documented address is 0xEFF7.
    // Registered in Emulator::init under mask 0xF0FF / match 0xE0F7.
    void write_port_eff7(uint8_t v);
    // Observable on the stored 2-bit state — used by tests and by Branch B's
    // NR 0x8F handler to gate Pentagon-1024 mode.
    bool port_eff7_ram_at_0000() const { return port_eff7_reg_3_; }  // bit 3
    bool port_eff7_disable_p1024() const { return port_eff7_reg_2_; } // bit 2

    // ---------------------------------------------------------------
    // NR 0x8F — Mapping Mode (Pentagon-128/512/1024)
    // ---------------------------------------------------------------
    // VHDL zxnext.vhd:3787-3794, 3798-3801. Stores the 2-bit
    // nr_8f_mapping_mode.
    //   00 = standard (default)
    //   01 = reserved (profi in source, forced off at line 3797)
    //   10 = Pentagon 512K
    //   11 = Pentagon 1024K
    //
    // In Pentagon modes the port_7ffd_bank composition at VHDL:3764-3766
    // uses 7FFD(7:6) for bank(4:3) (instead of DFFD(1:0)), forces bank(6)
    // to 0, and — in Pentagon-1024 mode when EFF7(2)=0 — promotes 7FFD(5)
    // from the paging-lock bit into bank(5). The Pentagon-1024 gate also
    // overrides port_7ffd_locked via VHDL:3769.
    //
    // Reset semantics (VHDL fidelity): VHDL declares
    //   signal nr_8f_mapping_mode : std_logic_vector(1 downto 0) := (others => '0');
    // with NO reset process — the write at line 3790-3792 only fires on
    // nr_8f_we=1. That means neither hard nor soft reset clears the value;
    // the default-init '00' is the FPGA-configuration-time initial state
    // (equivalent to power-on, modelled here by the default member
    // initializer). reset(bool) leaves nr_8f_mode_ alone.
    void write_nr_8f(uint8_t v);
    // Observable on the 2-bit stored value. Tests and internal composition
    // both consume it.
    uint8_t nr_8f_mode() const { return nr_8f_mode_; }

    // Derived gates matching VHDL:3798-3801 exactly.
    //   pentagon (3798): mode=="10" OR pentagon_1024_en
    //   pentagon_1024   (3799): mode=="11"
    //   pentagon_1024_en(3801): pentagon_1024 AND NOT port_eff7_reg_2
    bool pentagon_1024_en() const {
        return (nr_8f_mode_ == 0x03) && !port_eff7_reg_2_;
    }
    bool pentagon_en() const {
        return (nr_8f_mode_ == 0x02) || pentagon_1024_en();
    }

    // Effective lock state from VHDL:3769. Pentagon-1024-en overrides the
    // 7FFD(5) lock (drops port_7ffd_locked to 0). Profi branch is always
    // false in JNEXT (VHDL:3797). Returned value controls the write gates
    // in map_128k_bank / map_plus3_bank / write_port_dffd.
    bool effective_paging_locked() const {
        return paging_locked_ && !pentagon_1024_en();
    }

    // ---------------------------------------------------------------
    // NR 0x8E — Unified paging (write-only, read-back re-composed)
    // ---------------------------------------------------------------
    // VHDL zxnext.vhd:3662-3671 (port_7ffd_reg updates),
    //      zxnext.vhd:3696-3704 (port_dffd_reg updates),
    //      zxnext.vhd:3726-3735 (port_1ffd_reg updates),
    //      zxnext.vhd:6158-6159 (read-back composition).
    //
    // Write layout (nr_wr_dat):
    //   bit 7 → port_dffd_reg(0)   [when bit 3 = 1; also port_dffd_reg(2:1)="00"]
    //   bit 6 → port_7ffd_reg(2)   [when bit 3 = 1]
    //   bit 5 → port_7ffd_reg(1)   [when bit 3 = 1]
    //   bit 4 → port_7ffd_reg(0)   [when bit 3 = 1]
    //   bit 3 = "bank select" mode enable. When 1, writes the RAM-bank
    //           fields above, clears port_dffd_reg(3) (non-Profi), and
    //           zeroes port_dffd_reg(2:1) via the "00" prefix.
    //   bit 2 → port_1ffd_reg(0) (special mode enable). Also gates
    //           port_7ffd_reg(4) update: when bit 2 = 0, 7FFD(4) ← bit 0.
    //           When bit 2 = 1, 7FFD(4) is preserved.
    //   bit 1 → port_1ffd_reg(2)  (ROM-high bit)
    //   bit 0 → port_1ffd_reg(1)  (+3 config bit). Also → 7FFD(4) when bit 2 = 0.
    //
    // Lock bypass: VHDL:3662, 3696, 3726 — all three register updates on
    // nr_8e_we are in the `elsif` chain AFTER the locked-gated write path,
    // so they always fire regardless of port_7ffd_locked. LCK-07 (plan).
    //
    // Read-back formula from VHDL:6159:
    //   {dffd(0), 7ffd(2), 7ffd(1), 7ffd(0), '1', 1ffd(0), 1ffd(2),
    //    (7ffd(4) AND NOT 1ffd(0)) OR (1ffd(1) AND 1ffd(0))}
    //
    // Bit 3 on read is always '1' — a spec sentinel, NOT the bit written.
    void write_nr_8e(uint8_t v);
    uint8_t read_nr_8e() const;

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
    // The SRAM arbiter consumes these flags via the read()/write() hot
    // path. The altrom address override (zxnext.vhd:2981-3001, 3021,
    // 3056, 3078, 3116-3123) is wired through altrom_sram_page_(): when
    // altrom_en=1+altrom_rw=0 the read path redirects 0x0000-0x3FFF
    // ROM-mapped accesses into the alt-ROM SRAM region (pages 12..15),
    // and when altrom_en+altrom_rw=1 the write path redirects ROM-slot
    // writes there (the en+rw=1 read still falls through to the live
    // ROM/sram_pre path per VHDL:3078 fourth clause — the canonical
    // "write-over" semantics).
    void    set_nr_8c(uint8_t v) { nr_8c_reg_ = v; }
    uint8_t get_nr_8c() const { return nr_8c_reg_; }
    bool    nr_8c_altrom_en()        const { return (nr_8c_reg_ & 0x80) != 0; }
    bool    nr_8c_altrom_rw()        const { return (nr_8c_reg_ & 0x40) != 0; }
    bool    nr_8c_altrom_lock_rom1() const { return (nr_8c_reg_ & 0x20) != 0; }
    bool    nr_8c_altrom_lock_rom0() const { return (nr_8c_reg_ & 0x10) != 0; }

    // ---------------------------------------------------------------
    // p3_floating_bus_dat — last contended-CPU-r/w byte latch.
    // ---------------------------------------------------------------
    // VHDL zxnext.vhd:4498-4509: on every contended memory access
    // (mem_contend='1' AND cpu_mreq_n='0'), the latch captures cpu_di
    // on a read or cpu_do on a write. The latched byte feeds
    // i_p3_floating_bus into the ULA (zxnext.vhd:4478) as the
    // border-fallback arm of o_ula_floating_bus (zxula.vhd:573); on +3
    // it is exposed as the +3 floating-bus surface via port 0x0FFD
    // (zxnext.vhd:4517 + 2589). Only Mmu sees every CPU r/w byte, so
    // the latch lives here and is updated from read()/write() when the
    // current 16K slot is flagged contended.
    //
    // Branch B scope (Phase 2): plumbing only. The set_slot_contended()
    // mirror is pushed by Emulator alongside the existing
    // ContentionModel::set_contended_slot() calls; runtime mem_contend
    // gating (mem_active_page high-bits, cpu_speed, contention_disable)
    // is approximated by the per-slot flag (matches the same simplification
    // used by the existing FUSE Z80 contention path).
    void    set_p3_floating_bus_dat(uint8_t v) { p3_floating_bus_dat_ = v; }
    uint8_t p3_floating_bus_dat() const { return p3_floating_bus_dat_; }

    // Per-16K-slot contention mirror — pushed by Emulator at every site
    // that already updates ContentionModel::set_contended_slot(). Used
    // by read()/write() to decide whether to update p3_floating_bus_dat_.
    // Slot index is addr>>14 (0=0x0000, 1=0x4000, 2=0x8000, 3=0xC000).
    void set_slot_contended(int slot, bool v) {
        if (slot >= 0 && slot < 4) slot_contended_[slot] = v;
    }
    bool slot_contended(int slot) const {
        return (slot >= 0 && slot < 4) ? slot_contended_[slot] : false;
    }

    // Last 128K paging register value (for debugger display)
    uint8_t port_7ffd() const { return port_7ffd_; }

    // port_7ffd_reg(3) — ULA shadow-screen select. VHDL zxnext.vhd:3640
    // latches the full byte into port_7ffd_reg on a port-0x7FFD write;
    // line :4453 routes bit 3 to `i_ula_shadow_en` on the ULA. Emulator's
    // 0x7FFD handler reads this accessor after map_128k_bank() and
    // forwards into Ula::set_shadow_screen_en, giving the MMU a single
    // source of truth for the shadow bit (mirrors the VHDL where the
    // port_7ffd_reg is the sole producer of i_ula_shadow_en).
    bool shadow_screen_en() const { return (port_7ffd_ & 0x08) != 0; }

    // Last +3 paging register value (needed by NR 0x8E/0x8F tests + debugger)
    uint8_t port_1ffd() const { return port_1ffd_; }

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
    // Layer 2 read/write-over control (driven by port 0x123B)
    // ---------------------------------------------------------------

    /// Configure Layer 2 read + write mapping from port 0x123B value.
    ///   bit 0: write-map enable (l2_write_enable_)
    ///   bit 2: read-map  enable (l2_read_enable_)
    ///   bits 7:6: segment select (00=0x0000, 01=0x4000, 10=0x8000, 11=all — SHARED)
    /// VHDL zxnext.vhd:905-912 (signals), 3904-3928 (handler), 3077 (arbiter gate).
    /// Read and write share a single segment register (port_123b_layer2_map_segment
    /// is one 2-bit signal, not two).
    void set_l2_port(uint8_t val, uint8_t active_bank);

    // Observable on the latched enable bits. Both are exposed so tests can
    // verify the latch independently of driving the read or write paths.
    bool l2_read_enable()  const { return l2_read_enable_; }
    bool l2_write_enable() const { return l2_write_enable_; }

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
    // Compute the 8K SRAM page index for an alt-ROM access at `addr`
    // (addr in [0x0000, 0x3FFF]). VHDL zxnext.vhd:3117:
    //   sram_A21_A13 = "0000011" & sram_pre_alt_128_n & sram_pre_A21_A13(0)
    // with sram_pre_A21_A13(0) = cpu_a(13). The 9-bit sram_A21_A13 selects
    // an 8K SRAM page (Ram::page_ptr is 8K-indexed), so the page index is:
    //   page = 0x0C | (alt_128_n << 1) | (addr >> 13)
    // covering pages 12..15 — the Alt-ROM region documented at
    // zxnext.vhd:2924-2925 (Alt ROM0 128K at SRAM 0x018000..0x01BFFF =
    // pages 12-13, Alt ROM1 48K at 0x01C000..0x01FFFF = pages 14-15).
    //
    // sram_pre_alt_128_n is computed per machine type (zxnext.vhd:2981-3008):
    //   48K   : alt_128_n = NOT((NOT lock_rom1) AND lock_rom0)
    //   +3    : with any lock → alt_128_n = lock_rom1; else port_1ffd_rom(0)
    //   ZXN/  : with any lock → alt_128_n = lock_rom1; else port_1ffd_rom(0)
    //   Pent.   (Pentagon follows the 128K legacy ROM path → port_7ffd(4))
    // port_1ffd_rom(0) is bit 0 of the 2-bit ROM bank, derived from
    // port_1ffd(2)<<1 | port_7ffd(4) → equivalently (port_7ffd_ >> 4) & 1.
    inline uint8_t altrom_sram_page_(uint16_t addr) const {
        const bool lk1 = nr_8c_altrom_lock_rom1();
        const bool lk0 = nr_8c_altrom_lock_rom0();
        bool alt_128_n;
        switch (machine_type_) {
            case MachineType::ZX48K:
                // zxnext.vhd:2986
                alt_128_n = !((!lk1) && lk0);
                break;
            case MachineType::ZX_PLUS3:
                // zxnext.vhd:2988-2995
                if (lk1 || lk0) alt_128_n = lk1;
                else            alt_128_n = ((port_7ffd_ >> 4) & 1) != 0;
                break;
            case MachineType::ZX128K:
            case MachineType::PENTAGON:
            case MachineType::ZXN_ISSUE2:
            default:
                // zxnext.vhd:2998-3005 (ZXN branch — Pentagon/128K share
                // the same 1-bit port_1ffd_rom(0) selector via current_rom_bank).
                if (lk1 || lk0) alt_128_n = lk1;
                else            alt_128_n = ((port_7ffd_ >> 4) & 1) != 0;
                break;
        }
        const uint8_t a13 = static_cast<uint8_t>((addr >> 13) & 1);
        return static_cast<uint8_t>(0x0C | (alt_128_n ? 0x02 : 0x00) | a13);
    }

    void rebuild_ptr(int slot);
    // Map a ROM page into a slot without updating nr_mmu_ (callers
    // set nr_mmu_ themselves: reset() seeds 0xFF, legacy paging writes
    // the physical page for test/debugger observability).
    void map_rom_physical(int slot, uint8_t rom_page);
    // Rebuild slots 6/7 (RAM bank) from the current port_7ffd_ /
    // port_dffd_reg_ state. Mirrors VHDL zxnext.vhd:4677-4680, whose
    // update is gated on port_memory_ram_change_dly (:3814). Every
    // trigger fires this path EXCEPT an NR 0x8E write with bit 3 = 0,
    // whose suppression is honoured by the caller in write_nr_8e.
    void apply_legacy_ram_slots_();
    // Rebuild slots 0/1 (ROM area) from the current port_7ffd_ /
    // port_1ffd_ / port_eff7_reg_3_ state. Mirrors VHDL
    // zxnext.vhd:4619-4646, which fires whenever port_memory_change_dly
    // is '1' (every port 7FFD/1FFD/DFFD/EFF7 and every NR 0x8E / NR 0x8F
    // write, per :3813). Honours the port_eff7_reg_3 / RAM-at-0x0000
    // override.
    void apply_legacy_rom_slots_();
    // Convenience: rebuild both halves. Used by port 7FFD/1FFD/DFFD/EFF7
    // writes, the NR 0x8F handler, and the soft-reset path — all
    // triggers where port_memory_ram_change_dly='1' (default case).
    // NR 0x8E has its own path in write_nr_8e that calls the halves
    // independently so the bit-3=0 suppression can be honoured.
    void apply_legacy_paging_();
    // Compose the 7-bit port_7ffd_bank per VHDL zxnext.vhd:3763-3766,
    // branching on pentagon_en() / pentagon_1024_en().
    uint8_t compose_bank_() const;

    Ram& ram_;
    Rom& rom_;
    uint8_t slots_[8];      // physical page used by rebuild_ptr
    uint8_t nr_mmu_[8];     // NR 0x50–0x57 register-visible value (may be 0xFF sentinel)
    const uint8_t* read_ptr_[8];
    uint8_t*       write_ptr_[8];
    bool           read_only_[8];
    bool           paging_locked_ = false;
    // VHDL p3_floating_bus_dat latch (zxnext.vhd:4498-4509). Captures the
    // last contended CPU r/w data byte; surfaced through port 0x0FFD on
    // +3. Power-on default 0x00 (VHDL signal default — no explicit
    // reset clause).
    uint8_t        p3_floating_bus_dat_ = 0x00;
    // Per-16K-slot contention mirror (see set_slot_contended() comment).
    bool           slot_contended_[4] = {false, false, false, false};
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

    // VHDL port_dffd_reg (zxnext.vhd:3688, 5 bits cpu_do(4:0)). Feeds the
    // port_7ffd_bank composition at VHDL:3764-3766. Bit 4 is the Profi
    // DFFD override (VHDL:3769); Profi mode is forced off in the VHDL
    // synthesizable build (VHDL:3797), so bit 4 is stored but has no
    // downstream effect in JNEXT. Hard-reset only per VHDL:3686-3690
    // (gated on `reset='1'`).
    uint8_t        port_dffd_reg_ = 0;

    // VHDL port_eff7_reg_2 / port_eff7_reg_3 (zxnext.vhd:3778-3782). Stored
    // as two separate bits in VHDL; mirrored here as a single uint8_t with
    // only bits 2 and 3 meaningful. Hard-reset only per VHDL:3777-3779.
    // bit 3 → RAM-at-0x0000 mode (MMU0/1 = 0x00/0x01 on next paging change,
    //         VHDL:4636-4644).
    // bit 2 → disables Pentagon-1024 extension (VHDL:3801).
    bool           port_eff7_reg_2_ = false;
    bool           port_eff7_reg_3_ = false;

    // VHDL nr_8f_mapping_mode (zxnext.vhd:888 default "00", written at
    // zxnext.vhd:3790-3792, read back at zxnext.vhd:6162). VHDL has NO
    // reset process — the value persists across both hard and soft reset.
    // The default member init below models the FPGA-configuration-time
    // power-on value.
    uint8_t        nr_8f_mode_ = 0;

    // Layer 2 read/write-over state. VHDL zxnext.vhd:905-912 carries separate
    // wr_en and rd_en signals but a SHARED segment register
    // (port_123b_layer2_map_segment, one 2-bit signal used by both directions).
    // The arbiter at zxnext.vhd:3077 selects between them on cpu_rd_n — the
    // two directions are mutually exclusive per cycle.
    bool    l2_write_enable_  = false;
    bool    l2_read_enable_   = false;  // port 0x123B bit 2 (VHDL zxnext.vhd:3918)
    uint8_t l2_segment_mask_  = 0;     // bitmask: bit 0=seg0, bit 1=seg1, bit 2=seg2 (shared)
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
