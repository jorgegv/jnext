#include "peripheral/divmmc.h"
#include "core/log.h"
#include <fstream>
#include "core/saveable.h"

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
    // NB: enable-flag state (port_io_enable_, nr_0a_4_enable_, enabled_) is
    // preserved across soft reset — VHDL zxnext.vhd:4112 derives these from
    // NR/port signals that are not cleared by the divmmc automap_reset path.
    conmem_ = false;
    mapram_ = false;
    bank_ = 0;
    control_reg_ = 0;
    automap_active_ = false;
    automap_hold_   = false;
    automap_held_   = false;
    button_nmi_     = false;  // VHDL divmmc.vhd:108 — reset clears latch
    layer2_map_read_ = false; // VHDL zxnext.vhd:3910 — reset clears L2 rd-map
    entry_points_0_ = 0x83;  // soft reset default
    entry_valid_0_  = 0x01;
    entry_timing_0_ = 0x00;
    entry_points_1_ = 0xCD;
    divmmc_log()->debug("reset (entry_points_0={:#04x} entry_points_1={:#04x})",
                        entry_points_0_, entry_points_1_);
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
    conmem_ = (val & 0x80) != 0;
    mapram_ = mapram_ || ((val & 0x40) != 0);  // OR-latch per VHDL zxnext.vhd:4182-4183
    bank_   = val & 0x0F;
    control_reg_ = (val & ~0x40) | (mapram_ ? 0x40 : 0x00);  // reflect latched bit 6

    divmmc_log()->debug("write control={:#04x} conmem={} mapram={} bank={}",
                        val, conmem_, mapram_, bank_);
}

uint8_t DivMmc::read_control() const {
    return control_reg_ & 0xCF;  // bits 5:4 masked to zero per VHDL zxnext.vhd:4190
}

// E3-05: NR 0x09 bit 3 clears the port_e3_reg(6) OR-latch (mapram).
// VHDL zxnext.vhd:4184-4185.
void DivMmc::clear_mapram() {
    mapram_ = false;
    control_reg_ &= ~0x40;
    divmmc_log()->debug("clear_mapram (NR 0x09 bit 3)");
}

// ── Enable-flag levers (NA-03, DA-08) ─────────────────────────────────

void DivMmc::apply_enabled_transition_(bool prev_enabled) {
    enabled_ = port_io_enable_ && nr_0a_4_enable_;
    if (prev_enabled && !enabled_) {
        // DA-08: mirror VHDL i_automap_reset (divmmc.vhd:126,139) on any
        // enabled→disabled edge — both hold and held latches are cleared,
        // so the combinational automap output goes low immediately.
        if (automap_active_) {
            divmmc_log()->debug("automap OFF (enable cleared)");
        }
        automap_active_ = false;
        automap_hold_   = false;
        automap_held_   = false;
    }
}

void DivMmc::set_enabled(bool en) {
    bool prev = enabled_;
    port_io_enable_ = en;
    nr_0a_4_enable_ = en;
    apply_enabled_transition_(prev);
}

void DivMmc::set_port_io_enable(bool v) {
    bool prev = enabled_;
    port_io_enable_ = v;
    apply_enabled_transition_(prev);
}

void DivMmc::set_nr_0a_4_enable(bool v) {
    bool prev = enabled_;
    nr_0a_4_enable_ = v;
    apply_enabled_transition_(prev);
}

// ── RETN hook ─────────────────────────────────────────────────────────

void DivMmc::on_retn() {
    // VHDL divmmc.vhd:126,139 — i_retn_seen clears automap hold AND held.
    // button_nmi clear (divmmc.vhd:108) belongs to Task 8 (Multiface);
    // intentionally not handled here.
    if (automap_active_ || automap_hold_ || automap_held_) {
        divmmc_log()->debug("RETN: automap cleared");
    }
    automap_active_ = false;
    automap_hold_   = false;
    automap_held_   = false;
}

// ── Auto-mapping ──────────────────────────────────────────────────────

void DivMmc::check_automap(uint16_t pc, bool is_m1) {
    if (!is_m1 || !enabled_) return;

    // VHDL divmmc.vhd:123-148 two-stage automap pipeline.
    //
    // Per-M1 model: the VHDL `automap_hold` FF clocks on M1+MREQ-low during
    // the fetch; `automap_held` clocks on the subsequent MREQ rising edge.
    // We collapse a full fetch into one call here: first latch hold → held
    // (simulating the MREQ rising edge that separates the previous M1 from
    // this one), then decode this PC against entry points to update hold,
    // then compute the combinational `automap` output for reads during this
    // M1 (held OR any instant_on match this cycle).
    //
    // VHDL line 148 shows that `automap` output is the OR of:
    //   held
    //   (main_active AND instant_on)            — fires SAME cycle as detection
    //   (rom3_active AND rom3_instant_on)       — ditto for ROM3 path
    // Meanwhile `hold` (line 128-131) loads from the OR of instant+delayed
    // matches or the previous held (minus off), so delayed-on matches
    // activate on the NEXT M1 via the held promotion.

    // Step 1: Latch hold → held. Simulates the MREQ rising edge that
    // separates the previous M1's memory cycle from this one (VHDL line 141).
    automap_held_ = automap_hold_;

    // Step 2: Decode this PC. For each entry point we need: does it match,
    // is it currently enabled, and is its configured timing instant or
    // delayed. NR 0xB8 (entry_points_0_) enables RST 0x00..0x38; NR 0xBA
    // (entry_timing_0_) sets per-entry instant (1) vs delayed (0) timing;
    // NR 0xB9 (entry_valid_0_) gates each RST entry on "main path" (bit=1)
    // vs "ROM3 only path" (bit=0).
    bool instant_match = false;
    bool delayed_match = false;
    bool off_match     = false;

    static constexpr uint16_t rst_addrs[8] = {
        0x0000, 0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030, 0x0038
    };
    for (int i = 0; i < 8; ++i) {
        if ((entry_points_0_ & (1 << i)) && pc == rst_addrs[i]) {
            const bool valid = (entry_valid_0_ & (1 << i)) != 0;
            const bool instant = (entry_timing_0_ & (1 << i)) != 0;
            // Main path fires unconditionally when valid=1; ROM3 path fires
            // only when ROM3 is the selected ROM (VHDL divmmc.vhd:130,148
            // gates i_automap_rom3_* on i_automap_rom3_active) AND when
            // Layer 2 is NOT read-mapped at 0x0000-0x3FFF (VHDL zxnext.vhd:
            // 3138 — sram_divmmc_automap_rom3_en factors in
            // NOT sram_layer2_map_en; the L2 read overlay owns that region
            // when active). The main path is unaffected by L2 read-map.
            if (valid || (rom3_active_ && !layer2_map_read_)) {
                if (instant) instant_match = true;
                else         delayed_match = true;
            }
            goto decode_done;
        }
    }

    // Non-RST entry points from NR 0xBB (entry_points_1_). VHDL timing is
    // documented in zxnext.vhd around :2892-2905. NMI@0x0066 uses
    // automap_nmi_instant_on (Task 8 scope, main path). Tape traps at
    // 0x04C6/0x0562/0x04D7/0x056A use rom3_delayed_on (ROM3 only).
    if ((entry_points_1_ & 0x02) && pc == 0x0066 && button_nmi_) {
        // NMI instant-on. VHDL divmmc.vhd:120 gates automap_nmi_instant_on
        // on the latched `button_nmi` signal — the automap only fires on
        // a PC=0x0066 fetch when the NMI-button has actually been pressed.
        // We track this via button_nmi_, which stays false until a future
        // Multiface / NMI-button source sets it via set_button_nmi(). With
        // no button consumer wired yet, this branch is effectively off —
        // matching VHDL behaviour on a quiescent core and preventing a
        // spurious automap activation on any ordinary control-flow path
        // that happens to reach 0x0066 (e.g. enNextZX.rom subroutines).
        instant_match = true;
    } else if ((entry_points_1_ & 0x04) && pc == 0x04C6 && rom3_active_ && !layer2_map_read_) {
        // ROM3-only tape trap — VHDL zxnext.vhd:3138 gates on NOT sram_layer2_map_en.
        delayed_match = true;
    } else if ((entry_points_1_ & 0x08) && pc == 0x0562 && rom3_active_ && !layer2_map_read_) {
        delayed_match = true;
    } else if ((entry_points_1_ & 0x10) && pc == 0x04D7 && rom3_active_ && !layer2_map_read_) {
        delayed_match = true;
    } else if ((entry_points_1_ & 0x20) && pc == 0x056A && rom3_active_ && !layer2_map_read_) {
        delayed_match = true;
    } else if ((entry_points_1_ & 0x40) && pc >= 0x1FF8 && pc <= 0x1FFF) {
        // Auto-unmap range (divmmc.vhd:131, automap_delayed_off factor).
        off_match = true;
    }

decode_done:
    // Step 3: Update hold for this M1. VHDL divmmc.vhd:128-131.
    // hold = (any instant or delayed match) OR (held AND NOT off).
    const bool prev_hold = automap_hold_;
    automap_hold_ = instant_match || delayed_match ||
                    (automap_held_ && !off_match);

    // Step 4: Combinational `automap` output for reads during this M1.
    // held covers previous-cycle carry; instant_match activates same-cycle.
    // VHDL divmmc.vhd:148.
    const bool prev_active = automap_active_;
    automap_active_ = automap_held_ || instant_match;

    if (automap_active_ != prev_active || automap_hold_ != prev_hold) {
        divmmc_log()->debug(
            "automap @PC={:#06x}: hold={}→{} held={} active={}→{} "
            "(instant={} delayed={} off={})",
            pc, prev_hold, automap_hold_, automap_held_,
            prev_active, automap_active_,
            instant_match, delayed_match, off_match);
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

void DivMmc::save_state(StateWriter& w) const
{
    // Persist the composite enable flag.  The individual port_io_enable_
    // and nr_0a_4_enable_ levers are reconstructed on load from enabled_
    // (see load_state below) since pre-NA-03 snapshots do not carry them.
    w.write_bool(enabled_);
    w.write_bool(conmem_);
    w.write_bool(mapram_);
    w.write_u8(bank_);
    w.write_u8(control_reg_);
    w.write_bool(automap_active_);
    w.write_u8(entry_points_0_);
    w.write_u8(entry_valid_0_);
    w.write_u8(entry_timing_0_);
    w.write_u8(entry_points_1_);
    // Two-stage automap latch state (Task 7 Branch A).
    w.write_bool(automap_hold_);
    w.write_bool(automap_held_);
    // NMI-button latch (VHDL divmmc.vhd:108-111).
    w.write_bool(button_nmi_);
    // Layer 2 read-map feeder (VHDL zxnext.vhd:3138). Appended after
    // button_nmi_ to keep the stream layout append-only.
    w.write_bool(layer2_map_read_);
    w.write_bytes(ram_.data(), ram_.size());
}

void DivMmc::load_state(StateReader& r)
{
    enabled_        = r.read_bool();
    // NA-03: keep the split levers consistent with the composite flag.
    port_io_enable_ = enabled_;
    nr_0a_4_enable_ = enabled_;
    conmem_         = r.read_bool();
    mapram_         = r.read_bool();
    bank_           = r.read_u8();
    control_reg_    = r.read_u8();
    automap_active_ = r.read_bool();
    entry_points_0_ = r.read_u8();
    entry_valid_0_  = r.read_u8();
    entry_timing_0_ = r.read_u8();
    entry_points_1_ = r.read_u8();
    automap_hold_   = r.read_bool();
    automap_held_   = r.read_bool();
    // NMI-button latch (VHDL divmmc.vhd:108-111). Appended to the
    // snapshot stream — consistent with the Task 7 Branch A additions
    // above (hold/held). Pre-existing snapshots from before this commit
    // are not backward-compatible; the project has historically treated
    // save-state format as tied to build version.
    button_nmi_     = r.read_bool();
    // Layer 2 read-map feeder (VHDL zxnext.vhd:3138). Appended after
    // button_nmi_. Like the Task 7 Branch A additions above, this breaks
    // backward compat with pre-feeder snapshots by design.
    layer2_map_read_ = r.read_bool();
    r.read_bytes(ram_.data(), ram_.size());
}
