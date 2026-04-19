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
    // Constructor — power-on reset, equivalent to the top-level `reset`
    // signal in VHDL (zxnext.vhd:1730 `reset <= i_RESET`).
    reset(true);
}

void Mmu::reset(bool hard) {
    // paging_locked_ mirrors port_7ffd_reg(5) → port_7ffd_locked
    // (zxnext.vhd:3769 derives the latter from the former). The VHDL
    // reset at zxnext.vhd:3646-3648 gates the `port_7ffd_reg <= (others
    // => '0')` assignment on `reset='1'` (hard only). Soft resets leave
    // the lock state — and thus bit 5 — untouched.
    if (hard) {
        paging_locked_ = false;
    }
    // VHDL zxnext.vhd:4930-4935 — nr_08_contention_disable <= '0' is
    // gated on `reset='1'` (hard only). Soft reset preserves the flag.
    if (hard) {
        contention_disabled_ = false;
    }
    // VHDL zxnext.vhd:3686-3690 — port_dffd_reg <= (others => '0') is
    // gated on `reset='1'`. The existing Branch C architectural decision
    // treats the whole `reset='1'` family as hard-only (port_7ffd_reg,
    // nr_08_contention_disable, nr_8c_altrom) so port_dffd_reg + the
    // port_eff7_reg pair follow the same pattern. Soft reset preserves
    // them; this matches the firmware invariant that a RESET_SOFT does
    // not re-bind the extended-paging configuration the bootloader set.
    if (hard) {
        port_dffd_reg_ = 0;
        port_eff7_reg_2_ = false;
        port_eff7_reg_3_ = false;
    }
    // VHDL zxnext.vhd:3787-3794 — nr_8f_mapping_mode has NO reset process;
    // the value persists across hard and soft reset alike. The signal
    // declaration default `:= (others => '0')` at VHDL:888 is only the
    // FPGA-configuration-time power-on value, already mirrored by the
    // default member initializer on nr_8f_mode_. Do NOT reset it here.
    // VHDL zxnext.vhd:2253-2256 — the lo→hi nibble copy on nr_8c_altrom
    // is gated on `reset='1'` (hard only). Soft reset preserves the full
    // 8-bit register. Bits 3:0 themselves are not cleared by the VHDL
    // process; they persist as "altrom bits to reload on reset" defaults,
    // and the copy re-derives bits 7:4 on each hard reset.
    if (hard) {
        const uint8_t lo = nr_8c_reg_ & 0x0F;
        nr_8c_reg_ = static_cast<uint8_t>((lo << 4) | lo);
    }
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

    // Soft reset only: the VHDL paging registers (port_7ffd_reg / port_dffd_reg
    // / port_eff7_reg_*) are preserved across a soft reset (the reset tree at
    // zxnext.vhd:1730 gates the register clears on `reset='1'`, hard-only).
    // On real hardware MMU0..MMU7 are composed combinationally every cycle
    // from those registers (zxnext.vhd:4619-4682), so the preserved register
    // state immediately re-asserts its page mapping. Our imperative model
    // must re-apply that composition manually — otherwise the register
    // accessors still report the preserved values but the seeded
    // RESET_PAGES + ROM mapping above silently overrides the effect.
    //
    // apply_legacy_paging_() covers both halves: MMU6/7 from
    // port_7ffd_ + port_dffd_reg_ (VHDL:3763-3766, 4677-4680), and the
    // MMU0/1 RAM-at-0x0000 override when port_eff7_reg_3_=1
    // (VHDL:4636-4644). On hard reset all three source registers are
    // already 0 above, so re-applying would be a no-op — but skipping it
    // keeps the hard-reset path byte-for-byte identical to the prior
    // RESET_PAGES + map_rom_physical(0/1) sequence.
    if (!hard) {
        apply_legacy_paging_();
    }
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

// Apply slots 6/7 (RAM bank composed from 7FFD+DFFD) and slots 0/1
// (ROM or RAM-at-0x0000) from the current register state. Called by
// map_128k_bank (after the lock gate), write_port_dffd, and
// write_port_eff7 — all three correspond to VHDL's port_memory_change_dly
// rebuild at zxnext.vhd:4619-4682 which fires on 7ffd/1ffd/dffd/eff7 writes.
void Mmu::apply_legacy_paging_() {
    // Slots 6-7: selected RAM bank composed with port_dffd_reg extra bits
    // (VHDL zxnext.vhd:3763-3766). Store the LOGICAL page (VHDL
    // mem_active_page semantics); to_sram_page() inside rebuild_ptr()
    // applies the VHDL zxnext.vhd:2964 +0x20 shift for Next mode.
    uint8_t bank = compose_bank_();
    set_page(6, static_cast<uint8_t>(bank * 2));
    set_page(7, static_cast<uint8_t>(bank * 2 + 1));

    // Slots 0-1: ROM selection (VHDL zxnext.vhd:4619-4644).
    // Default: MMU0=MMU1=0xFF (ROM sentinel). If port_eff7_reg_3=1 → force
    // MMU0=0x00, MMU1=0x01 (RAM-at-0x0000 mode, VHDL:4636-4644).
    // 128K: bit 4 selects ROM 0 or 1 (2 ROMs)
    // +3: combines bit 4 with port_1ffd_ bit 2 for 4-ROM selection
    if (port_eff7_reg_3_) {
        set_page(0, 0x00);
        set_page(1, 0x01);
    } else {
        int rom_bank = ((port_1ffd_ >> 2) & 1) << 1 | ((port_7ffd_ >> 4) & 1);
        map_rom_physical(0, rom_bank * 2);
        map_rom_physical(1, rom_bank * 2 + 1);
        // NOTE: VHDL sets MMU0/MMU1 = 0xFF here (normal ROM paging).
        // We store the physical page so ROM bank changes are observable via
        // get_page() in tests/debugger. NR 0x50/0x51 read-back through the
        // nextreg cache is unaffected.
        nr_mmu_[0] = rom_bank * 2;
        nr_mmu_[1] = rom_bank * 2 + 1;
    }
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
    apply_legacy_paging_();
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
    // VHDL zxnext.vhd:3693 stores cpu_do(4:0). Bits 5-7 are NOT stored.
    // Bit 6 (port_dffd_reg_6) is consumed by Profi MMU4/5 composition
    // (VHDL:4660) and Multiface readback (VHDL:4314), neither of which
    // is on the Mmu surface. Drop silently.
    port_dffd_reg_ = static_cast<uint8_t>(v & 0x1F);
    // VHDL zxnext.vhd:4619 — port_memory_change_dly rebuilds MMU0..7 on
    // any paging-port write. Bypass the paging_locked gate (we verified
    // above it was unlocked when this write arrived).
    apply_legacy_paging_();
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
    // EFF7 write flips 0x0000 even when 7FFD is locked).
    apply_legacy_paging_();
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
    apply_legacy_paging_();
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
