#include "mmu.h"
#include "peripheral/divmmc.h"
#include "peripheral/multiface.h"
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
    // Constructor — power-on reset, equivalent to the top-level `reset`
    // signal in VHDL (zxnext.vhd:1730 `reset <= i_RESET`).
    reset(true);
}

void Mmu::set_boot_rom(const uint8_t* data, size_t size) {
    // VHDL zxnext.vhd:3199-3204 hardwires the bootrom entity to
    // cpu_a(12:0) (13 bits = exactly 8 KB). Wrong-sized blobs are not
    // real-hardware-faithful: we accept any pointer but always
    // materialise an 8 KB internal buffer (zero-padded / truncated)
    // and serve from that, so the read path can safely mask with
    // 0x1FFF (mirror per VHDL bootrom_mod) regardless of source size.
    // (G140, G157)
    constexpr size_t kBootRomSize = 0x2000;  // 8 KB
    if (data == nullptr || size == 0) {
        boot_rom_buf_.clear();
        boot_rom_ = nullptr;
        boot_rom_size_ = 0;
        boot_rom_en_ = false;
        return;
    }
    if (size != kBootRomSize) {
        Log::memory()->warn(
            "boot ROM size mismatch: got {} bytes, expected {} (8 KB); "
            "{} per VHDL zxnext.vhd:3199-3204 bootrom is hardwired to 8 KB",
            size, kBootRomSize,
            size < kBootRomSize ? "zero-padding" : "truncating");
    }
    boot_rom_buf_.assign(kBootRomSize, 0);
    const size_t copy = std::min(size, kBootRomSize);
    std::memcpy(boot_rom_buf_.data(), data, copy);
    boot_rom_ = boot_rom_buf_.data();
    boot_rom_size_ = kBootRomSize;
    boot_rom_en_ = true;
}

void Mmu::reset(bool hard) {
    // VHDL reset semantics — corrected G46(b) 2026-05-08.
    //
    // Inside zxnext.vhd, the `reset` signal is `i_RESET` (line 1730).
    // i_RESET is wired from the top-level wrapper at
    // zxnext_top_issue5.vhd:2384 to the signal `reset`, which is
    // `reset_hard or reset_soft` (zxnext_top_issue5.vhd:880). So every
    // `if reset='1'` clause inside zxnext.vhd fires on BOTH hard AND
    // soft resets — there is no separation inside the core.
    //
    // The earlier "Branch C" architectural decision in this file (which
    // treated port_7ffd_reg / port_1ffd_reg / port_dffd_reg / paging_locked
    // / nr_08_contention_disable / nr_8c lo→hi copy as hard-only) was a
    // misreading of the VHDL. It caused a NextZXOS-boot regression: the
    // supervisor expects post-NR_02-soft-reset rom_bank to be 0 so that
    // PC=$0000 lands in enNextZX.rom bank 0's `DI; JP $00EF` cold-start.
    // jnext preserved port_7ffd/port_1ffd, leaving the CPU in bank 2's
    // `NOP; JR $0000` infinite-loop sentinel. Three independent VHDL
    // audits + ROM disassembly confirmed the supervisor's design.
    //
    // VHDL line refs for each register cleared on `reset='1'`:
    //   * port_7ffd_reg     — zxnext.vhd:3646-3648
    //   * port_1ffd_reg     — zxnext.vhd:3713-3715
    //   * port_dffd_reg + _6 — zxnext.vhd:3686-3690
    //   * port_eff7 latches  — zxnext.vhd:3777-3779
    //   * paging_locked (port_7ffd_reg(5)) — implicit in 3648
    //   * nr_08_contention_disable — zxnext.vhd:4930-4935
    //   * nr_8c lo→hi copy — zxnext.vhd:2253-2256
    //
    // (`hard` parameter is now unused but kept for ABI parity; if a
    // future register turns out to genuinely be hard-only per VHDL, it
    // would re-introduce the gate.)
    (void)hard;

    paging_locked_ = false;
    contention_disabled_ = false;
    port_dffd_reg_ = 0;
    port_dffd_reg_6_ = false;
    port_eff7_reg_2_ = false;
    port_eff7_reg_3_ = false;
    port_7ffd_ = 0;
    port_1ffd_ = 0;
    // VHDL zxnext.vhd:3716 — port_1ffd_special_old clears on reset.
    port_1ffd_special_old_ = false;

    // VHDL zxnext.vhd:3787-3794 — nr_8f_mapping_mode has NO reset process;
    // the value persists across hard and soft reset alike. The signal
    // declaration default `:= (others => '0')` at VHDL:888 is only the
    // FPGA-configuration-time power-on value, already mirrored by the
    // default member initializer on nr_8f_mode_. Do NOT reset it here.

    // VHDL zxnext.vhd:2253-2256 — nr_8c lo→hi nibble copy on `reset='1'`.
    {
        const uint8_t lo = nr_8c_reg_ & 0x0F;
        nr_8c_reg_ = static_cast<uint8_t>((lo << 4) | lo);
    }
    // VHDL zxnext.vhd:3907-3913 — port 0x123B Layer 2 latches clear on
    // `reset='1'` (top-level reset, i.e. both hard and soft in our model
    // since the signal chain is unconditional from the top-level reset).
    l2_write_enable_ = false;
    l2_read_enable_  = false;
    l2_segment_mask_ = 0;
    l2_segment_raw_  = 0;
    l2_enable_       = false;
    l2_map_shadow_   = false;
    l2_offset_       = 0;
    l2_bank_ = 8;
    // l2_shadow_bank_ is not in the VHDL :3907-3913 reset block — it
    // mirrors NR 0x13 (zxnext.vhd:5934 has its own reset writing 11). The
    // NextReg dispatch will re-push 11 via set_l2_shadow_bank() on
    // reset; default member init also matches.
    l2_shadow_bank_  = 11;
    // nr_04_romram_bank resets to 0 (VHDL zxnext.vhd:1104). config_mode stays
    // at its current value — it's pushed in by Emulator per machine type so
    // a reset on a 48K/128K/+3 machine doesn't spuriously activate Next-only
    // SRAM routing. Next machines will re-push config_mode=true via the NR
    // 0x03 handler on first write (matches tbblue.fw's boot flow), and via
    // Emulator::init() directly after nextreg_.reset() for power-on parity.
    nr_04_romram_bank_ = 0;
    // Re-enable boot ROM on reset only when nr_03_config_mode='1' at reset
    // time — VHDL zxnext.vhd:5109-5111 gates the bootrom_en re-assertion on
    // `if nr_03_config_mode = '1' then bootrom_en <= '1'` inside the
    // `if reset='1'` block (process at :4926). When config_mode='0' the
    // signal is NOT written and retains its pre-reset value (bootrom_en
    // is a flip-flop with FPGA-config-time default '1' per :1101). The
    // pre-pass-5 unconditional re-enable diverged from VHDL whenever
    // firmware had already written NR 0x03 with bits[2:0]∈{001..110} to
    // clear config_mode (so bootrom_en was previously cleared by the
    // VHDL :5122 NR 0x03 handler) — a subsequent NR 0x02 bit 1 / bit 0
    // reset would re-arm the boot ROM in jnext but leave it cleared in
    // VHDL. Verify5-memory class-(a) fix.
    //
    // Note: Mmu::config_mode_ is NOT cleared by reset() (the comment
    // above documents that config_mode is preserved across resets to
    // match VHDL :1102 — nr_03_config_mode has no reset clause). So we
    // read it here BEFORE any reset-time mutation, and the value is the
    // pre-reset config_mode the VHDL gate would consult.
    if (boot_rom_ && config_mode_) boot_rom_en_ = true;
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

    // With port_7ffd_ / port_1ffd_ / port_dffd_reg_ / port_eff7_* all
    // zeroed above, apply_legacy_paging_() would re-assert the same
    // mapping the RESET_PAGES seed + map_rom_physical(0/1) calls already
    // produced (slot 6 = page 0, slot 7 = page 1, MMU0/1 = ROM). Skip
    // it: there's no preserved state for it to re-compose.
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
        // VHDL floating-bus / inactive-slot gate (zxnext.vhd:2964 +
        // 3029-3066). The early-decode process derives
        //   mmu_A21_A13(8) = '1'  iff  mem_active_page(7:5) = "111"
        // i.e. logical page >= 0xE0.
        //
        // For slots 2..7 (cpu_a(15:14) ≠ "00") the SRAM arbiter at
        // line 3060-3061 drives `sram_pre_active <= (not mmu_A21_A13(8))
        // and …`. When inactive, CPU reads return the floating bus
        // (0xFF in practice) and writes are dropped.
        //
        // For slots 0/1 (cpu_a(15:14) = "00") VHDL takes a different
        // branch at lines 3029-3057. With mmu_A21_A13(8) = '1' and no
        // MF override, the access falls through to the legacy ROM path
        // at line 3052 (`sram_pre_A21_A13 <= "000000" & sram_rom &
        // cpu_a(13)`) — i.e. the slot is served from the
        // sram_rom-derived ROM page, NOT from the MMU page. This is
        // the same effect as a NR $50/$51 = $FF write per
        // zxnext.vhd:4611-4612 + :3052.
        //
        // Without this gate, an NR $50/$51 write with v ∈ [$E0..$FE]
        // would store the page verbatim in slots_[slot] (matching VHDL
        // MMU<i>), but rebuild_ptr would then resolve it as a RAM page
        // via `to_sram_page(v) = (v + 0x20) mod 256` — pointing to a
        // wrong wrap-aliased SRAM region (e.g. page $E5 → $05) instead
        // of the legacy ROM. Re-route through the same map_rom_physical
        // path used by engage_legacy_rom_paging_slot() so the cached
        // pointer matches sram_rom — but leave nr_mmu_[slot] alone so
        // the NR 0x50/0x51 read-back returns the verbatim VHDL MMU<i>
        // value, not the 0xFF sentinel.
        if (page >= 0xE0) {
            if (slot >= 2) {
                // Inactive slot — SRAM does not respond.
                read_ptr_[slot]  = nullptr;
                write_ptr_[slot] = nullptr;
            } else {
                // Slot 0/1: legacy ROM (sram_rom-derived). Mirror
                // engage_legacy_rom_paging_slot() but without touching
                // nr_mmu_[slot] (the caller — typically the NR $50/$51
                // dispatcher — has already written the verbatim VHDL
                // page value into nr_mmu_).
                //
                // Verify4-memory class-(a) fix: the EFF7(3) RAM-at-0x0000
                // override at VHDL :4636-4644 fires only on
                // port_memory_change_dly='1' (VHDL :3813), which an NR
                // 0x50/0x51 = high-page write does NOT trigger. With
                // MMU<i>='FF' (sentinel) the SRAM arbiter at :3037-3057
                // falls into the legacy-ROM branch (:3052) — sram_rom
                // selects the ROM page, EFF7(3) has NO effect until the
                // next paging-port write. Pre-fix this branch routed
                // slot 0/1 to RAM page 0/1 immediately on a same-cycle
                // NR $50/$51 write, which diverged from VHDL.
                const uint8_t sram_rom = current_sram_rom();
                const uint8_t rom_page = static_cast<uint8_t>(sram_rom * 2 + slot);
                read_ptr_[slot] = rom_in_sram_ ? ram_.page_ptr(rom_page)
                                               : rom_.page_ptr(rom_page);
                write_ptr_[slot] = nullptr;
                read_only_[slot] = true;
                // Verify11-memory class-(c) fix: keep `slots_[]` semantics
                // consistent with `read_only_[slot]=true` (= "physical SRAM
                // page currently being served"). Without this update, after
                // an NR $50/$51 write of v ∈ [$E0..$FE] the array carries
                // the verbatim NR-write value (e.g. 0xE5) while
                // `read_only_=true` says "interpret slots_[] as physical
                // page". The first-branch (`read_only_=true`) reload path
                // in rebuild_ptr (the load_state and any other rebuild
                // entry) would consume `slots_[slot]` as the physical
                // page and dispatch reads to the wrong SRAM region (e.g.
                // ram.page_ptr(0xE5) instead of the legacy-ROM
                // sram_rom-derived page). VHDL keeps MMU<i> verbatim in
                // the NR register (mirrored by `nr_mmu_[slot]`), but the
                // SRAM arbiter at zxnext.vhd:3037-3057 always falls into
                // the legacy ROM branch (`sram_pre_A21_A13 <= "000000" &
                // sram_rom & cpu_a(13)`) for `mmu_A21_A13(8)='1'` slot
                // 0/1 access. Mirroring that here means `slots_[slot]`
                // holds `sram_rom*2+slot`, the page the SRAM arbiter
                // would actually drive. `nr_mmu_[slot]` keeps the
                // verbatim NR-write value (e.g. 0xE5) for read-back
                // (zxnext.vhd:6075-6082).
                slots_[slot] = rom_page;
            }
            return;
        }
        // Bank-7 lower half (page 0x0E) is a dedicated dual-port BRAM
        // (VHDL bank7_ram dpram2, zxnext.vhd:6670; mem_active_bank7 gate
        // at :2962+3039-3041) — served from bank7_bram_, never from
        // external SRAM. Unconditional across machine personalities,
        // matching the VHDL (the gate has no machine-type term).
        if (page == 0x0E) {
            read_ptr_[slot]  = bank7_bram_.data();
            write_ptr_[slot] = bank7_bram_.data();
            return;
        }
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

void Mmu::set_l2_port(uint8_t val, uint8_t active_bank) {
    // VHDL zxnext.vhd:3914-3923 — port 0x123B write bifurcates on cpu_do(4):
    //   cpu_do(4) = 0:                                      (G92)
    //     bit 0 = wr_en, bit 1 = enable, bit 2 = rd_en,
    //     bit 3 = map_shadow (G144), bits 7:6 = segment.
    //   cpu_do(4) = 1:
    //     bits 2:0 = port_123b_layer2_offset; all other latches preserved.
    // l2_bank_ tracks NR 0x12 (active) and is refreshed every write so the
    // CPU map sees Copper-driven bank changes; the shadow bank is mirrored
    // separately via set_l2_shadow_bank (NR 0x13 path).
    l2_bank_ = active_bank;
    if ((val & 0x10) != 0) {
        // Offset-only update: VHDL zxnext.vhd:3922.
        l2_offset_ = static_cast<uint8_t>(val & 0x07);
        Log::memory()->debug("L2 port (offset-only): offset={}", l2_offset_);
        return;
    }
    l2_write_enable_ = (val & 0x01) != 0;  // bit 0
    l2_enable_       = (val & 0x02) != 0;  // bit 1 (display, mirrored at :3924-3925)
    l2_read_enable_  = (val & 0x04) != 0;  // bit 2
    l2_map_shadow_   = (val & 0x08) != 0;  // bit 3 — VHDL :3919, :2968 (G144)
    uint8_t seg = (val >> 6) & 0x03;
    l2_segment_raw_ = seg;
    // Verify8-memory class-(a) fix: the read/write hot path now consults
    // `l2_overlay_active_for(addr)` (in mmu.h) which models the VHDL
    // sram_pre_override(1) gate exactly: low half always enabled (in
    // non-MF cases), high half only when seg="11", 0xC000+ never. The
    // legacy `l2_segment_mask_` bitmask below is retained ONLY for
    // save_state/load_state schema compatibility — it is no longer
    // consulted on any access path. (Pre-fix, the mask incorrectly
    // restricted L2 to a single 16K segment matching the address range,
    // which diverged from VHDL :3043/:3050/:3057 — those branches all
    // set sram_pre_override(1)='1' for the low half regardless of seg.)
    switch (seg) {
        case 0: l2_segment_mask_ = 0x01; break;  // legacy — see note above
        case 1: l2_segment_mask_ = 0x02; break;
        case 2: l2_segment_mask_ = 0x04; break;
        case 3: l2_segment_mask_ = 0x07; break;
    }
    Log::memory()->debug("L2 port: wr={} rd={} en={} shadow={} seg={:#04x} mask={:#04x} bank={}",
                          l2_write_enable_, l2_read_enable_, l2_enable_,
                          l2_map_shadow_, seg, l2_segment_mask_, l2_bank_);
}

// VHDL zxnext.vhd:3763-3766 bank composition. Builds the 7-bit
// port_7ffd_bank value from port_7ffd_reg + port_dffd_reg, branching on
// the NR 0x8F mapping mode (Pentagon-512 / Pentagon-1024 / standard).
// Profi mode is forced off (VHDL:3797) so we drop that branch.
//
// Standard (non-Pentagon):
//   bank(2:0) = port_7ffd_reg(2:0)            (VHDL:3763)
//   bank(4:3) = port_dffd_reg(1:0)            (VHDL:3764, else branch)
//   bank(5)   = port_dffd_reg(2)              (VHDL:3765, else branch)
//   bank(6)   = port_dffd_reg(3)              (VHDL:3766, else branch)
//
// Pentagon modes (pentagon_en() true):
//   bank(2:0) = port_7ffd_reg(2:0)            (VHDL:3763)
//   bank(4:3) = port_7ffd_reg(7:6)            (VHDL:3764, when branch)
//   bank(5)   = pentagon_1024_en AND 7ffd(5)  (VHDL:3765, when branch)
//   bank(6)   = '0'                           (VHDL:3766, when branch)
// In Pentagon-512 (pentagon_1024_en=0), bank(5) always 0.
// In Pentagon-1024-en (nr_8f=="11" AND EFF7(2)=0), bank(5) = 7FFD(5) —
// which promotes the paging-lock bit into a bank-select bit. The lock
// is also bypassed via effective_paging_locked() to match VHDL:3769.
uint8_t Mmu::compose_bank_() const {
    const uint8_t p7ffd = port_7ffd_;
    const uint8_t pdffd = port_dffd_reg_;
    uint8_t bank = static_cast<uint8_t>(p7ffd & 0x07);           // bits 2:0
    if (pentagon_en()) {
        bank |= static_cast<uint8_t>(((p7ffd >> 6) & 0x03) << 3);     // 4:3
        if (pentagon_1024_en()) {
            bank |= static_cast<uint8_t>(((p7ffd >> 5) & 0x01) << 5); // 5
        }
        // bank(6) forced 0 in Pentagon modes — N8F-05.
    } else {
        bank |= static_cast<uint8_t>(((pdffd >> 0) & 0x03) << 3);     // 4:3
        bank |= static_cast<uint8_t>(((pdffd >> 2) & 0x01) << 5);     // 5
        bank |= static_cast<uint8_t>(((pdffd >> 3) & 0x01) << 6);     // 6
    }
    return bank;
}

// Rebuild slots 6/7 (RAM bank) from port_7ffd_ / port_dffd_reg_.
// VHDL zxnext.vhd:4677-4680 — gated by port_memory_ram_change_dly
// (:3814). Every port/NR paging trigger fires this EXCEPT an NR 0x8E
// write with bit 3 = 0; the NR 0x8E handler honours that by bypassing
// this helper rather than calling it.
void Mmu::apply_legacy_ram_slots_() {
    // Slots 6-7: selected RAM bank composed with port_dffd_reg extra bits
    // (VHDL zxnext.vhd:3763-3766). Store the LOGICAL page (VHDL
    // mem_active_page semantics); to_sram_page() inside rebuild_ptr()
    // applies the VHDL zxnext.vhd:2964 +0x20 shift for Next mode.
    uint8_t bank = compose_bank_();
    set_page(6, static_cast<uint8_t>(bank * 2));
    set_page(7, static_cast<uint8_t>(bank * 2 + 1));
}

// Rebuild slots 0/1 (ROM area) from port_7ffd_ / port_1ffd_ /
// port_eff7_reg_3_. VHDL zxnext.vhd:4619-4646 — fires whenever
// port_memory_change_dly='1' (every port 7FFD/1FFD/DFFD/EFF7 and every
// NR 0x8E / NR 0x8F write, per :3813).
void Mmu::apply_legacy_rom_slots_() {
    // Slots 0-1: ROM selection (VHDL zxnext.vhd:4619-4644).
    // Default: MMU0=MMU1=0xFF (ROM sentinel). If port_eff7_reg_3=1 → force
    // MMU0=0x00, MMU1=0x01 (RAM-at-0x0000 mode, VHDL:4636-4644).
    // 128K: bit 4 selects ROM 0 or 1 (2 ROMs)
    // +3: combines bit 4 with port_1ffd_ bit 2 for 4-ROM selection
    if (port_eff7_reg_3_) {
        set_page(0, 0x00);
        set_page(1, 0x01);
    } else {
        // VHDL-faithful sram_rom selection per machine type
        // (zxnext.vhd:2981-3008): 48K → ROM 0; 128K/Next → 1-bit
        // (port_7ffd(4) only); +3 → 2-bit (port_1ffd(2):port_7ffd(4)).
        // Also handles NR 0x8C altrom-lock overrides. The previous
        // unconditional 2-bit composition was the +3-only formula, and
        // it caused the supervisor's bank-flip wrapper at slot-2 RAM
        // $5B00 to map slot 0 to bank 3 (soft-reset trampoline) on
        // toggles of $7FFD bit 4 / $1FFD bit 2 in Next mode.
        const uint8_t sram_rom = current_sram_rom();
        map_rom_physical(0, sram_rom * 2);
        map_rom_physical(1, sram_rom * 2 + 1);
        // VHDL zxnext.vhd:4611-4612,4619-4644 — MMU0/MMU1 hold 0xFF whenever
        // slots 0/1 are in legacy ROM paging mode; the physical ROM page is
        // derived dynamically from port_7ffd / port_1ffd / NR 0x8C / NR 0x8E.
        // Use get_effective_page(slot) to observe the derived page.
        nr_mmu_[0] = 0xFF;
        nr_mmu_[1] = 0xFF;
    }
}

// Apply slots 6/7 (RAM) and slots 0/1 (ROM) from the current register
// state. Called by map_128k_bank (after the lock gate), write_port_dffd,
// write_port_eff7, write_nr_8f, and the soft-reset path — all triggers
// where VHDL's port_memory_ram_change_dly='1', so the unconditional
// both-halves rebuild is correct. NR 0x8E is handled separately in
// write_nr_8e. Order: RAM first, ROM second — preserved from the
// pre-refactor monolithic helper.
void Mmu::apply_legacy_paging_() {
    apply_legacy_ram_slots_();
    apply_legacy_rom_slots_();
}

// VHDL zxnext.vhd:4623-4632 — +3 special paging table. When
// `port_1ffd_special='1'` (i.e. `port_1ffd_reg(0)=1`), MMU0..MMU7 are
// all rewritten per the four configurations selected by
// port_1ffd_reg(2:1). Each bit pair encodes (B, A) = (bit2, bit1):
//   (0,0): banks 0,1,2,3 → MMU pages 0,1,2,3,4,5,6,7
//   (0,1): banks 4,5,6,7 → MMU pages 8,9,10,11,12,13,14,15
//   (1,0): banks 4,5,6,3 → MMU pages 8,9,10,11,12,13,6,7
//   (1,1): banks 4,7,6,3 → MMU pages 8,9,14,15,12,13,6,7
// (Verified algebraically from the bit-construction at VHDL :4625-4632.)
//
// All eight slots become RAM-mapped (read+write enabled); the SRAM
// arbiter at :3037 routes them via the MMU page (mmu_A21_A13(8)='0' for
// these low pages). set_page() handles read_only_=false + rebuild_ptr.
void Mmu::apply_plus3_special_paging_() {
    static const uint8_t configs[4][4] = {
        {0, 1, 2, 3},   // (B=0, A=0): banks 0..3
        {4, 5, 6, 7},   // (B=0, A=1): banks 4..7
        {4, 5, 6, 3},   // (B=1, A=0): banks 4,5,6,3
        {4, 7, 6, 3},   // (B=1, A=1): banks 4,7,6,3
    };
    const uint8_t cfg = static_cast<uint8_t>((port_1ffd_ >> 1) & 0x03);
    for (int seg = 0; seg < 4; ++seg) {
        const uint8_t bank = configs[cfg][seg];
        set_page(seg * 2,     static_cast<uint8_t>(bank * 2));
        set_page(seg * 2 + 1, static_cast<uint8_t>(bank * 2 + 1));
    }
}

// VHDL zxnext.vhd:4653-4670 — on the cycle that exits +3 special
// paging (port_1ffd_special_old='1' AND new port_1ffd_special='0'),
// MMU2/3 revert to {0x0A, 0x0B} (bank 5) and MMU4/5 to {0x04, 0x05}
// (bank 2). MMU0/1 and MMU6/7 are handled by the standard legacy
// paging path immediately afterwards. We model the per-clock-edge
// "fires once on transition" semantics with a same-write hand-off:
// caller checks port_1ffd_special_old_ before invoking this helper.
void Mmu::revert_slots_2_to_5_post_special_() {
    set_page(2, 0x0A);
    set_page(3, 0x0B);
    set_page(4, 0x04);
    set_page(5, 0x05);
}

// Apply the paging update matching VHDL zxnext.vhd:4623-4684. Three
// branches correspond to the VHDL outer if/else:
//   1. port_1ffd_special=1 → +3 special table fully replaces MMU0..7.
//   2. port_1ffd_special_old=1 AND port_1ffd_special=0 → revert slots
//      2-5 (per :4655-4670) then legacy paging.
//   3. otherwise → legacy paging.
// After branch resolution, we mirror VHDL :3729's port_1ffd_special_old
// update — it captures `port_1ffd_special` whenever a paging trigger
// fires (the VHDL is `if port_memory_change_dly = '0' then ... else
// '0'`, but with our per-write granularity the equivalent end-state is
// to set _old to the current _special after each apply_paging_update_).
void Mmu::apply_paging_update_() {
    const bool special = (port_1ffd_ & 0x01) != 0;
    if (special) {
        apply_plus3_special_paging_();
    } else if (port_1ffd_special_old_) {
        revert_slots_2_to_5_post_special_();
        apply_legacy_paging_();
    } else {
        apply_legacy_paging_();
    }
    port_1ffd_special_old_ = special;
}

// Per-slot legacy-ROM re-engage helper — see header comment for full
// rationale. Mirrors the half of `apply_legacy_rom_slots_` that touches
// the requested slot only, so explicit NR $50,$FF / NR $51,$FF writes
// preserve the other slot's NR-driven mapping. Matches VHDL
// zxnext.vhd:4686-4696 nr_mmu_we semantics for value 0xFF.
void Mmu::engage_legacy_rom_paging_slot(int slot, bool set_nr_sentinel) {
    if (slot != 0 && slot != 1) return;
    // Verify4-memory class-(a) fix: the VHDL EFF7(3) RAM-at-0x0000
    // override at zxnext.vhd:4636-4644 fires ONLY when
    // `port_memory_change_dly='1'`, i.e. on a port_7FFD/1FFD/DFFD/EFF7
    // write or an NR 0x8E/0x8F write. An explicit `NR 0x50/0x51 = 0xFF`
    // write goes through the nr_mmu_we path (zxnext.vhd:4686-4699) and
    // stores `MMU<i> <= 0xFF` verbatim — it does NOT trigger
    // port_memory_change_dly (zxnext.vhd:3813), so the eff7 override
    // does NOT apply on that cycle. The next CPU access to slot 0/1
    // sees MMU0/1=0xFF, mmu_A21_A13(8)='1', and the SRAM arbiter at
    // zxnext.vhd:3037-3057 falls through to the legacy ROM branch
    // (sram_rom-derived) — eff7 has no influence until the next
    // paging-port write reasserts MMU0/1 = 0x00/0x01.
    //
    // Pre-fix this path mirrored the eff7 override immediately on the
    // NR $50/$51 = $FF write, routing slot 0/1 to RAM page 0/1. That
    // diverges from VHDL: an `NR $50,$FF` while EFF7(3)=1 should leave
    // the slot serving legacy ROM, not RAM.
    const uint8_t sram_rom = current_sram_rom();
    map_rom_physical(slot, static_cast<uint8_t>(sram_rom * 2 + slot));
    // Verify12-memory class-(b) fix: gate the nr_mmu_ sentinel update on
    // the caller's path. VHDL only writes MMU<i> on three triggers:
    //   * reset (zxnext.vhd:4610)
    //   * port_memory_change_dly=1 (zxnext.vhd:4619), set on port
    //     7FFD/1FFD/DFFD/EFF7 / NR 8E / NR 8F writes
    //   * nr_mmu_we=1 (zxnext.vhd:4686), set ONLY on NR 0x50..0x57 writes
    // NR 0x8C and machine-type-change touch NEITHER trigger, so MMU<i>
    // must keep its current value. The NR $50/$51 dispatcher passes
    // `set_nr_sentinel=true` to mirror the nr_mmu_we-stores-0xFF path;
    // the NR 0x8C and set_machine_type refresh callers pass `false` so
    // a verbatim 0xE0..0xFE value (or the 0x00/0x01 EFF7(3)=1-derived
    // value) is preserved for the NR-port read-back at :6075-6082.
    if (set_nr_sentinel) {
        nr_mmu_[slot] = 0xFF;
    }
}

// VHDL zxnext.vhd:2256-2265 stores nr_8c_altrom verbatim. The lock bits
// feed `sram_rom` / `sram_rom3` combinationally at zxnext.vhd:2981-3008,
// so any change is immediately visible to the SRAM arbiter without a
// port_memory_change_dly pulse. jnext caches slot 0/1 read pointers
// based on `current_sram_rom()`, so on every NR 0x8C write that flips
// lock_rom1/rom0 we must refresh slots 0/1 ONLY when those slots are
// currently routing through legacy ROM — i.e. when read_only_[i] is
// true. Per-slot rebuild via engage_legacy_rom_paging_slot() preserves
// any explicit RAM mapping installed by NR 0x50/0x51 with v < 0xE0
// (which leaves read_only_[i]=false and routes the slot to RAM via
// mmu_A21_A13(8)='0' per VHDL zxnext.vhd:3037-3043). The previous
// unconditional apply_legacy_rom_slots_() clobbered nr_mmu_[0]/[1] to
// 0xFF and forced both slots back to ROM, contradicting VHDL where
// MMU<i> is unchanged on an NR 0x8C write (no port_memory_change_dly
// pulse — see VHDL :3813).
//
// Verify3-memory class-(a) fix: previously this rebuilt unconditionally,
// silently dropping explicit slot-0/1 RAM mappings when firmware
// touched NR 0x8C.
void Mmu::set_nr_8c(uint8_t v) {
    nr_8c_reg_ = v;
    // Per-slot refresh: only rebuild slots that are currently in legacy
    // ROM mode (read_only_=true). Slots explicitly mapped to RAM via
    // NR 0x50/0x51 stay routed to that RAM page — VHDL leaves MMU<i>
    // alone on an NR 0x8C write (zxnext.vhd:4619-4644 fires only on
    // port_memory_change_dly='1', which excludes NR 0x8C).
    //
    // Verify12-memory class-(b) fix: pass `set_nr_sentinel=false` so the
    // NR-visible MMU<i> register is preserved across the NR 0x8C
    // refresh. VHDL nr_mmu_we does NOT fire on NR 0x8C (only on
    // NR 0x50..0x57), and port_memory_change_dly does NOT fire either
    // (VHDL :3813), so MMU<i> stays at whatever value the last legitimate
    // trigger wrote. Pre-fix the helper unconditionally clobbered
    // nr_mmu_[slot] to 0xFF — wrong whenever a previous NR 0x50/0x51
    // wrote a verbatim value in 0xE0..0xFE (legal high-page mapping
    // that resolves to legacy ROM via the SRAM arbiter at :3037-3057
    // but reads back the verbatim NR-write byte) or the EFF7(3)=1 path
    // applied the verbatim 0x00/0x01 mapping (:4636-4644).
    if (read_only_[0]) engage_legacy_rom_paging_slot(0, /*set_nr_sentinel=*/false);
    if (read_only_[1]) engage_legacy_rom_paging_slot(1, /*set_nr_sentinel=*/false);
}

void Mmu::map_128k_bank(uint8_t port_7ffd) {
    // VHDL zxnext.vhd:3650 gates the port_7ffd_reg write on
    // `port_7ffd_locked = '0'`. port_7ffd_locked itself (VHDL:3769) is
    // dropped to 0 whenever Pentagon-1024 mode is enabled (nr_8f=="11" AND
    // EFF7(2)=0), so the Pentagon-1024 lock-override applies here too.
    // effective_paging_locked() models the full VHDL:3769 gate.
    if (effective_paging_locked()) {
        Log::memory()->trace("128K bank switch ignored (paging locked)");
        return;
    }
    Log::memory()->debug("128K bank switch: port_7ffd={:#04x}", port_7ffd);
    // VHDL:3769 — port_7ffd_reg(5) is the raw lock bit; the lock is active
    // unless pentagon_1024_en overrides. Store the bit verbatim; the
    // effective gate composes with nr_8f_mode_ / port_eff7_reg_2_ at use
    // sites via effective_paging_locked().
    paging_locked_ = (port_7ffd >> 5) & 1;
    port_7ffd_ = port_7ffd;
    // VHDL :4623 — when `port_1ffd_special='1'` AND a paging trigger
    // fires (port 7FFD write satisfies port_memory_change_dly), the
    // MMU is rewritten from the +3 special table, NOT legacy paging.
    // apply_paging_update_() arbitrates between the special / legacy /
    // exit-special cases.
    apply_paging_update_();
}

void Mmu::write_port_dffd(uint8_t v) {
    // VHDL zxnext.vhd:3691 — port_dffd write gated by
    //   port_7ffd_locked='0' OR nr_8f_mapping_mode_profi='1'
    // Profi is forced '0' at VHDL:3797, so we drop that branch. But
    // port_7ffd_locked itself (VHDL:3769) is dropped to '0' by
    // pentagon_1024_en, so a locked 7FFD(5) does NOT block DFFD writes
    // when we are in Pentagon-1024 mode with EFF7(2)=0. Model this with
    // effective_paging_locked().
    if (effective_paging_locked()) {
        Log::memory()->trace("port 0xDFFD write ignored (paging locked)");
        return;
    }
    Log::memory()->debug("port 0xDFFD write: v={:#04x}", v);
    // VHDL zxnext.vhd:3693 stores cpu_do(4:0) into port_dffd_reg, while
    // :3694 stores cpu_do(6) into the separate port_dffd_reg_6 flip-flop
    // (VHDL :877). Bits 5 and 7 are not stored. The DFFD-bit-6 latch
    // feeds the Multiface +3 read-mux at :4314 (`'0' & port_dffd_reg_6
    // & '0' & port_dffd_reg`). (G148)
    port_dffd_reg_   = static_cast<uint8_t>(v & 0x1F);
    port_dffd_reg_6_ = (v & 0x40) != 0;
    // VHDL zxnext.vhd:4619 — port_memory_change_dly rebuilds MMU0..7 on
    // any paging-port write. Bypass the paging_locked gate (we verified
    // above it was unlocked when this write arrived). Honor the
    // port_1ffd_special arbitration in apply_paging_update_().
    apply_paging_update_();
}

void Mmu::write_port_eff7(uint8_t v) {
    // VHDL zxnext.vhd:3780-3782 — always accepted (no lock gate).
    Log::memory()->debug("port 0xEFF7 write: v={:#04x}", v);
    port_eff7_reg_2_ = (v & 0x04) != 0;
    port_eff7_reg_3_ = (v & 0x08) != 0;
    // VHDL zxnext.vhd:4619-4644 — port_memory_change_dly fires on EFF7
    // writes too; MMU0/1 pick up the new RAM-at-0x0000 choice immediately.
    // VHDL line 4619 does NOT gate the rebuild on the paging lock, so we
    // rebuild unconditionally (mirrors the hardware behaviour that an
    // EFF7 write flips 0x0000 even when 7FFD is locked). Honor the
    // port_1ffd_special arbitration so the +3 special-paging table sticks
    // through an EFF7 write.
    apply_paging_update_();
}

// NR 0x8F — Mapping Mode (Pentagon / Pentagon-1024).
// VHDL zxnext.vhd:3787-3794 stores nr_wr_dat(1:0) into nr_8f_mapping_mode.
// VHDL:3813 flags nr_8f_we into port_memory_change_dly so MMU0..7 rebuild
// on the next clock. We re-run apply_legacy_paging_ immediately: bank(4:3)
// / bank(5) / bank(6) composition depends on the new mode (VHDL:3764-3766),
// and port_7ffd_locked can flip via VHDL:3769 — the latter affects future
// writes, not the current slot view, but the rebuild still produces the
// correct MMU6/7 pages for the new composition.
void Mmu::write_nr_8f(uint8_t v) {
    Log::memory()->debug("NR 0x8F write: v={:#04x}", v);
    nr_8f_mode_ = static_cast<uint8_t>(v & 0x03);
    // VHDL :3815 nr_8f_we_dly feeds port_memory_change_dly; honor the
    // port_1ffd_special arbitration so a special-mode-locked MMU image
    // is preserved through the NR 0x8F write.
    apply_paging_update_();
}

// NR 0x8E — Unified paging (write).
// VHDL zxnext.vhd:3662-3670 (port_7ffd_reg),
//      zxnext.vhd:3696-3704 (port_dffd_reg),
//      zxnext.vhd:3726-3734 (port_1ffd_reg).
// The three update processes are in `elsif nr_8e_we='1'` branches, so they
// always fire regardless of port_7ffd_locked — NR 0x8E bypasses the lock
// (LCK-07). After the register updates VHDL:3813 flags nr_8e_we into
// port_memory_change_dly which triggers the MMU0..7 rebuild.
void Mmu::write_nr_8e(uint8_t v) {
    Log::memory()->debug("NR 0x8E write: v={:#04x}", v);
    const bool bank_mode = (v & 0x08) != 0;  // bit 3

    // port_7ffd_reg updates (VHDL:3662-3670):
    //   if bit 3 = 1: 7FFD(2:0) <= nr_wr_dat(6 downto 4)
    //   if bit 2 = 0: 7FFD(4)   <= nr_wr_dat(0)
    if (bank_mode) {
        // 7FFD(2)=bit6, 7FFD(1)=bit5, 7FFD(0)=bit4
        const uint8_t bits = static_cast<uint8_t>((v >> 4) & 0x07);
        port_7ffd_ = static_cast<uint8_t>((port_7ffd_ & ~0x07) | bits);
    }
    if ((v & 0x04) == 0) {
        // 7FFD(4) <- nr_wr_dat(0)
        if (v & 0x01) port_7ffd_ |= 0x10;
        else          port_7ffd_ &= ~0x10;
    }

    // port_dffd_reg updates (VHDL:3696-3704, profi=0):
    //   if bit 3 = 1 (AND profi=0): dffd(3) <= '0'
    //   if bit 3 = 1:              dffd(2:0) <= "00" & nr_wr_dat(7)
    if (bank_mode) {
        // Clear dffd(3) (N8E-06).
        port_dffd_reg_ &= static_cast<uint8_t>(~0x08);
        // dffd(0) <- bit 7; dffd(2:1) <- "00".
        port_dffd_reg_ &= static_cast<uint8_t>(~0x07);
        if (v & 0x80) port_dffd_reg_ |= 0x01;
    }

    // port_1ffd_reg updates (VHDL:3726-3734):
    //   1FFD(2) <= nr_wr_dat(1)
    //   1FFD(1) <= nr_wr_dat(0)
    //   1FFD(0) <= nr_wr_dat(2)
    // Note: port_1ffd_ stores the raw 8-bit port value with bits 2:0
    // meaningful — matches VHDL port_1ffd_reg's 3-bit layout.
    uint8_t new_1ffd = static_cast<uint8_t>(port_1ffd_ & ~0x07);
    if (v & 0x02) new_1ffd |= 0x04;  // 1FFD(2) <- bit 1
    if (v & 0x01) new_1ffd |= 0x02;  // 1FFD(1) <- bit 0
    if (v & 0x04) new_1ffd |= 0x01;  // 1FFD(0) <- bit 2
    port_1ffd_ = new_1ffd;

    // Rebuild MMU0..7 per VHDL:3813 port_memory_change_dly (nr_8e_we
    // path). MMU6/MMU7 rebuild is suppressed when NR 0x8E bit 3 = 0 —
    // VHDL:3814 drives port_memory_ram_change_dly = NOT(nr_8e_we AND NOT
    // nr_wr_dat(3)), and the MMU6/7 update at VHDL:4677 is gated on that
    // signal. MMU0/MMU1 are rebuilt unconditionally (VHDL:4619-4644).
    //
    // VHDL :4623 — when `port_1ffd_special='1'` after this write, the
    // special-paging table fully replaces MMU0..7 (no bit-3 suppression
    // applies — the special table writes ALL eight slots together).
    // Likewise on the 1→0 transition we must revert slots 2-5 to
    // defaults per :4655-4670. Run the special-paging arbiter for the
    // bit-3=1 case (rebuilds MMU6/7 anyway) and for any
    // port_1ffd_special transition; only fall back to the half-rebuild
    // path when bit 3 = 0 AND we are not in / leaving special mode.
    const bool special     = (port_1ffd_ & 0x01) != 0;
    const bool exit_special = port_1ffd_special_old_ && !special;
    if (special || exit_special || (v & 0x08)) {
        apply_paging_update_();
    } else {
        // Bit 3 = 0 and no special-mode transition: VHDL :4677 leaves
        // MMU6/7 untouched; only MMU0/1 update via :4619-4644.
        apply_legacy_rom_slots_();
        // Track special-bit history even on the suppressed path so the
        // next paging trigger sees the up-to-date _old value (matches
        // VHDL :3729's continuous capture semantics).
        port_1ffd_special_old_ = special;
    }
}

// NR 0x8E — read-back. VHDL zxnext.vhd:6158-6159:
//   port_253b_dat <= dffd(0) & 7ffd(2) & 7ffd(1) & 7ffd(0) & '1' &
//                    1ffd(0) & 1ffd(2) &
//                    ((7ffd(4) AND NOT 1ffd(0)) OR (1ffd(1) AND 1ffd(0)));
// Bit 3 on read is a fixed '1', independent of the bit 3 that was written.
uint8_t Mmu::read_nr_8e() const {
    const uint8_t dffd0 = static_cast<uint8_t>(port_dffd_reg_ & 0x01);
    const uint8_t p7ffd = port_7ffd_;
    const uint8_t p1ffd = port_1ffd_;
    const uint8_t bit7 = dffd0;                                           // dffd(0)
    const uint8_t bit6 = static_cast<uint8_t>((p7ffd >> 2) & 0x01);       // 7ffd(2)
    const uint8_t bit5 = static_cast<uint8_t>((p7ffd >> 1) & 0x01);       // 7ffd(1)
    const uint8_t bit4 = static_cast<uint8_t>((p7ffd >> 0) & 0x01);       // 7ffd(0)
    const uint8_t bit3 = 1;                                               // '1'
    const uint8_t bit2 = static_cast<uint8_t>((p1ffd >> 0) & 0x01);       // 1ffd(0) special
    const uint8_t bit1 = static_cast<uint8_t>((p1ffd >> 2) & 0x01);       // 1ffd(2) rom-high
    const uint8_t rom_hi = static_cast<uint8_t>((p7ffd >> 4) & 0x01);     // 7ffd(4)
    const uint8_t sp    = static_cast<uint8_t>((p1ffd >> 0) & 0x01);      // 1ffd(0)
    const uint8_t cfg1  = static_cast<uint8_t>((p1ffd >> 1) & 0x01);      // 1ffd(1)
    const uint8_t bit0 = static_cast<uint8_t>((rom_hi & (1 - sp)) | (cfg1 & sp));
    return static_cast<uint8_t>((bit7 << 7) | (bit6 << 6) | (bit5 << 5) |
                                (bit4 << 4) | (bit3 << 3) | (bit2 << 2) |
                                (bit1 << 1) | (bit0 << 0));
}

void Mmu::map_plus3_bank(uint8_t port_1ffd) {
    // VHDL zxnext.vhd:3718 gates the port_1ffd_reg write on
    // `port_7ffd_locked = '0'`. Same Pentagon-1024 override as 7FFD/DFFD.
    if (effective_paging_locked()) {
        Log::memory()->trace("+3 bank switch ignored (paging locked)");
        return;
    }
    Log::memory()->debug("+3 bank switch: port_1ffd={:#04x}", port_1ffd);
    port_1ffd_ = port_1ffd;
    // VHDL :4623-4684 — single arbiter handles both special-mode entry
    // and the exit-special-mode revert (slots 2-5 → bank 5 / bank 2).
    // Previously the else branch only refreshed slots 0/1 via
    // apply_legacy_rom_slots_(); the revert was missing, leaving stale
    // RAM banks in slots 2-5 after a special→normal transition.
    apply_paging_update_();
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
    // Branch C appended state (post-Task 12c): contention_disabled (NR 0x08 bit 6),
    // the full nr_8c_altrom register byte (VHDL zxnext.vhd:387), and machine_type_
    // for sram_rom selection (zxnext.vhd:2981-3008).
    w.write_bool(contention_disabled_);
    w.write_u8(nr_8c_reg_);
    w.write_u8(static_cast<uint8_t>(machine_type_));
    // Phase 2 A appended state — extended-paging ports (0xDFFD + 0xEFF7).
    // port_dffd_reg_ holds cpu_do(4:0) per VHDL zxnext.vhd:3693; the
    // port_eff7 flags are two single bits per VHDL zxnext.vhd:3781-3782.
    w.write_u8(port_dffd_reg_);
    w.write_bool(port_eff7_reg_2_);
    w.write_bool(port_eff7_reg_3_);
    // Phase 2 B appended state — NR 0x8F mapping mode (2 bits).
    // VHDL zxnext.vhd:3787-3794 has no reset process, so the value persists
    // across reset and must round-trip.
    w.write_u8(nr_8f_mode_);
    // Phase 2 D2 appended state — Layer 2 read-enable latch
    // (port_123b_layer2_map_rd_en, VHDL zxnext.vhd:3918). Write-enable and
    // segment mask/bank are already persisted above.
    w.write_bool(l2_read_enable_);
    // Floating-bus Branch B appended state — p3_floating_bus_dat latch
    // (VHDL zxnext.vhd:4498-4509) and per-slot contention mirror.
    w.write_u8(p3_floating_bus_dat_);
    for (int i = 0; i < 4; ++i) w.write_bool(slot_contended_[i]);
    // Task 8 Tier 1 Wave 2 — port 0x123B G92/G144/G145 latches:
    //   l2_segment_raw_  — 2-bit raw segment (VHDL zxnext.vhd:3920) for the
    //                      :3933 read-back composition (G145).
    //   l2_enable_       — display enable mirror (also driven by NR 0x69).
    //   l2_map_shadow_   — bit 3 (CPU map shadow bank select, G144).
    //   l2_offset_       — 3-bit offset register (G92, VHDL :3922).
    //   l2_shadow_bank_  — NR 0x13 mirror used when map_shadow=1 (:2968).
    w.write_u8(l2_segment_raw_);
    w.write_bool(l2_enable_);
    w.write_bool(l2_map_shadow_);
    w.write_u8(l2_offset_);
    w.write_u8(l2_shadow_bank_);
    // Task 8 Tier 1 Wave 2 — port 0xDFFD bit 6 latch (VHDL zxnext.vhd:877,
    // 3694, 4314). Single-bit flip-flop, separate from the 5-bit
    // port_dffd_reg vector. (G148)
    w.write_bool(port_dffd_reg_6_);
    // Task 2 Memory review — port_1ffd_special_old (VHDL :882/3716/3729).
    // Required so a save state taken inside +3 special paging mode
    // restores the slot-2-to-5 revert behaviour on the next paging
    // trigger after load.
    w.write_bool(port_1ffd_special_old_);
    // Verify4-memory class-(a) fix — persist nr_mmu_[8] verbatim. VHDL
    // zxnext.vhd:4686-4699 stores nr_wr_dat directly into MMU<i> on
    // any NR 0x50..0x57 write, including values in the 0xE0..0xFE
    // range that map to the ROM area via the mmu_A21_A13(8)='1' gate.
    // The NR-port read-back at :6075-6082 returns this verbatim value.
    // Pre-fix load_state() recovered nr_mmu_[i] from
    //   `read_only_[i] ? 0xFF : slots_[i]`
    // which lost any verbatim 0xE0..0xFE NR 0x50/0x51 write value (it
    // collapsed back to the 0xFF sentinel because read_only_=true).
    // Persisting the array round-trips the NR read-back faithfully.
    w.write_bytes(nr_mmu_, 8);
    // V24-MEM-01 / V25-MEM-01 fix — machine_timing axis split. Both
    // shadow (pending) and effective (latched) fields are persisted so
    // a snapshot taken between an NR 0x03 bits-6:4 write and the next
    // video-frame edge round-trips faithfully. Schema bump: appended at
    // tail, matched by the `!r.eof()`-tolerant reader (per V20R-CPU-
    // NIT-01 precedent). Older saves fall through with the constructor
    // default (+3 timing per VHDL :1099/:1377), which Emulator::
    // load_state re-syncs from the canonical NR 0x03 cached byte
    // (matching the same machine_type-from-NextReg pattern at :7283).
    w.write_u8(static_cast<uint8_t>(machine_timing_));
    w.write_u8(static_cast<uint8_t>(pending_machine_timing_));
    // 2026-07-10 schema append: dedicated bank-7 lower-half BRAM content
    // (VHDL bank7_ram dpram2). Appended at end per the established
    // schema-extension pattern.
    w.write_bytes(bank7_bram_.data(), bank7_bram_.size());
}

void Mmu::load_state(StateReader& r)
{
    // V24-MEM-NIT-01: reset the load-time machine_timing schema flag at
    // entry. Will be set true below iff BOTH timing slots are present.
    machine_timing_loaded_from_schema_ = false;
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
    // Branch C appended state — keep load tolerant of older streams that
    // do not carry these fields yet. save_state writes them; if the stream
    // predates Branch C, the reader will short-read and the caller's
    // StateReader bounds-check will flag it. We rely on save_state always
    // matching the same code generation so load_state is safe to read.
    contention_disabled_ = r.read_bool();
    nr_8c_reg_           = r.read_u8();
    machine_type_        = static_cast<MachineType>(r.read_u8());
    // Phase 2 A appended state — extended-paging ports (0xDFFD + 0xEFF7).
    port_dffd_reg_   = r.read_u8();
    port_eff7_reg_2_ = r.read_bool();
    port_eff7_reg_3_ = r.read_bool();
    // Phase 2 B appended state — NR 0x8F mapping mode (2 bits stored in u8).
    nr_8f_mode_      = static_cast<uint8_t>(r.read_u8() & 0x03);
    // Phase 2 D2 appended state — Layer 2 read-enable latch
    // (port_123b_layer2_map_rd_en, VHDL zxnext.vhd:3918).
    l2_read_enable_  = r.read_bool();
    // Floating-bus Branch B appended state — p3_floating_bus_dat latch
    // and per-slot contention mirror.
    p3_floating_bus_dat_ = r.read_u8();
    for (int i = 0; i < 4; ++i) slot_contended_[i] = r.read_bool();
    // Task 8 Tier 1 Wave 2 — port 0x123B G92/G144/G145 latches (must
    // mirror save_state() in order). Older streams that predate this
    // block will short-read; StateReader's bounds check flags it.
    l2_segment_raw_  = r.read_u8();
    l2_enable_       = r.read_bool();
    l2_map_shadow_   = r.read_bool();
    l2_offset_       = r.read_u8();
    l2_shadow_bank_  = r.read_u8();
    // Task 8 Tier 1 Wave 2 — port 0xDFFD bit 6 latch (G148, VHDL :877/3694/4314).
    port_dffd_reg_6_ = r.read_bool();
    // Task 2 Memory review — port_1ffd_special_old (VHDL :882/3716/3729).
    port_1ffd_special_old_ = r.read_bool();
    // Rebuild fast-dispatch pointers from restored page/read_only state.
    for (int i = 0; i < 8; ++i) rebuild_ptr(i);
    // Verify4-memory class-(a) fix — restore nr_mmu_[8] verbatim. See
    // save_state() comment for VHDL line refs and rationale. The array
    // is appended after every other field so older save streams that
    // predate this addition still round-trip via the lossy fallback
    // recovered below (StateReader's bounds check flags short reads).
    r.read_bytes(nr_mmu_, 8);
    // V24-MEM-01 / V25-MEM-01 fix — machine_timing axis split. Older
    // saves fall through with the constructor default (TimingPlus3 per
    // VHDL :1099/:1377). Emulator::load_state subsequently re-syncs
    // both fields from the canonical NR 0x03 cached byte when the
    // schema slot was absent, so the round-trip is robust against
    // schema-version mismatch.
    //
    // V24-MEM-NIT-01 (reviewer follow-up): track whether BOTH slots
    // were successfully read. The Emulator::load_state re-sync uses
    // this signal to decide between trusting the schema-restored pair
    // (preserves a `pending != effective` deferred-commit state taken
    // between an NR 0x03 bits-6:4 write and the next video-frame edge)
    // OR re-deriving from NextReg (old-format fallback). Pre-fix the
    // re-sync clobbered the pair unconditionally — see
    // emulator.cpp:7376-7395.
    bool got_machine_timing = false;
    bool got_pending        = false;
    if (!r.eof()) {
        machine_timing_ = static_cast<MachineTimingMode>(r.read_u8());
        got_machine_timing = true;
    }
    if (!r.eof()) {
        pending_machine_timing_ = static_cast<MachineTimingMode>(r.read_u8());
        got_pending = true;
    }
    machine_timing_loaded_from_schema_ = got_machine_timing && got_pending;
    // 2026-07-10 schema append — bank-7 BRAM content. Older saves without
    // this slot fall through with a zeroed BRAM (StateReader bounds check).
    if (!r.eof()) {
        r.read_bytes(bank7_bram_.data(), bank7_bram_.size());
    }
    // Re-point the slot ptrs so any slot holding page 0x0E picks up the
    // freshly-restored BRAM buffer.
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

// ---------------------------------------------------------------------------
// Multiface overlay helpers (Wave 1 E)
// ---------------------------------------------------------------------------
// VHDL authority:
//   * multiface.vhd:186 — `mf_enabled_o = mf_enable_eff = mf_enable OR fetch_66`.
//     This is the gate the SRAM arbiter consumes as `mf_mem_en` at
//     zxnext.vhd:3028.
//   * zxnext.vhd:2937-2945 — priority cascade
//       0. bootrom 1. multiface 2. divmmc 3. layer 2 4. mmu 5. config 6. romcs 7. rom
//   * zxnext.vhd:3028-3035 — when `mf_mem_en='1'` AND `cpu_a(15:14)="00"`:
//       sram_pre_A21_A13  := "00000101" & cpu_a(13);
//       sram_pre_active   := '1';
//       sram_pre_rdonly   := NOT cpu_a(13);    -- ROM-half write-locked
//       sram_pre_override := "000";            -- divmmc + L2 + romcs all gone

bool Mmu::mf_overlay_active_() const {
    return multiface_->is_mem_active();
}

uint8_t Mmu::mf_rom_byte_(uint16_t addr) const {
    // 8 KB MF ROM at 0x0000-0x1FFF.
    return multiface_->rom_data()[addr];
}

uint8_t Mmu::mf_ram_byte_(uint16_t addr) const {
    // 8 KB MF RAM at 0x2000-0x3FFF.
    return multiface_->ram_data()[addr - 0x2000];
}

void Mmu::mf_ram_write_(uint16_t addr, uint8_t val) {
    multiface_->ram_data()[addr - 0x2000] = val;
}
