#include "peripheral/divmmc.h"
#include "core/log.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// P0 boot probe env gate (JNEXT_BOOT_PROBE) — shared by the two probe
// blocks in check_automap(). Function-local static: getenv runs once.
static bool boot_probe_env() {
    static const bool v = std::getenv("JNEXT_BOOT_PROBE") != nullptr;
    return v;
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
    retn_pending_clear_ = false;  // G46(a) — flush any queued delayed clear
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

    uint8_t* target = rom_data();
    std::fill(target, target + kRomSize, 0xFF);
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(target), size);

    divmmc_log()->debug("loaded ROM: {} ({} bytes)", path, static_cast<int>(size));
    return true;
}

bool DivMmc::load_rom_bytes(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        divmmc_log()->error("load_rom_bytes: empty buffer");
        return false;
    }

    // Reset to padded 0xFF before loading. Mirrors the file path's
    // "short file padded with 0xFF" semantics.
    uint8_t* target = rom_data();
    std::fill(target, target + kRomSize, 0xFF);

    size_t to_copy = size;
    if (size < static_cast<size_t>(kRomSize)) {
        divmmc_log()->warn("ROM buffer too small ({} bytes, expected {}), padding with 0xFF",
                           size, kRomSize);
    }
    if (size > static_cast<size_t>(kRomSize)) {
        divmmc_log()->warn("ROM buffer too large ({} bytes, expected {}), truncating",
                           size, kRomSize);
        to_copy = kRomSize;
    }

    std::memcpy(target, data, to_copy);

    divmmc_log()->debug("loaded ROM from byte buffer ({} bytes)", to_copy);
    return true;
}

// ── Port 0xE3 ─────────────────────────────────────────────────────────

void DivMmc::write_control(uint8_t val) {
    conmem_ = (val & 0x80) != 0;
    mapram_ = mapram_ || ((val & 0x40) != 0);  // OR-latch per VHDL zxnext.vhd:4182-4183
    bank_   = val & 0x0F;
    // F19-DIVMMC-NIT-01 (Pass-19 verify-audit fix, 2026-05-10): VHDL
    // `zxnext.vhd:4180-4183` only assigns bits 7, 6, and 3:0 of port_e3_reg
    // from cpu_do during a port-E3 write. Bits 5:4 are reset to 0 at hardware
    // reset (`zxnext.vhd:4177`) and never touched again, so they are
    // INVARIANTLY '00' on real hardware. Pre-fix `control_reg_` stored input
    // bits 5:4 verbatim; the divergence was hidden by `read_control()`'s
    // `& 0xCF` mask (which mirrored the VHDL `port_e3_dat` zero-substitution
    // at `:4190`), but a save-state round-trip — which serialises
    // `control_reg_` raw at `save_state` line 543 — would faithfully
    // round-trip non-zero bits 5:4, diverging from VHDL's "always 0"
    // invariant on snapshot inspection. Mask `val & 0x8F` (drop bits 6 and
    // 5:4 from raw input) before re-folding the OR-latched bit 6, so the
    // stored byte always reflects the VHDL contract. Class-(c) latent.
    control_reg_ = (val & 0x8F) | (mapram_ ? 0x40 : 0x00);  // reflect latched bit 6

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
        // DA-08: mirror VHDL i_automap_reset (divmmc.vhd:108,126,139) on
        // any enabled→disabled edge — hold, held AND button_nmi are all
        // cleared by i_automap_reset in the same shared process, so the
        // combinational automap output goes low immediately and a prior
        // NMI-button press is dropped.
        if (automap_active_ || button_nmi_) {
            divmmc_log()->debug("automap+button_nmi OFF (enable cleared)");
        }
        automap_active_ = false;
        automap_hold_   = false;
        automap_held_   = false;
        button_nmi_     = false;  // divmmc.vhd:108 — i_automap_reset clear
        // G46(a) — drop any queued delayed-clear on automap_reset; the
        // shared VHDL process zeroes the pipeline registers and the
        // delayed-clear shadow has no meaning once held=0.
        retn_pending_clear_ = false;
    }
}

void DivMmc::set_enabled(bool en) {
    // VHDL-faithful: at hardware reset, port_divmmc_io_en defaults to '1'
    // (internal_port_enable defaults to all-1s, zxnext.vhd:1226-1229) but
    // nr_0a_divmmc_automap_en defaults to '0' (zxnext.vhd:1126). The
    // combined enable
    //   port_divmmc_io_en AND nr_0a_divmmc_automap_en
    // is therefore 0 at reset — automap is OFF until firmware writes
    // NR 0x0A bit 4 explicitly. This setter flips only the port-IO gate
    // (the "DivMMC hardware present" lever); callers that also want the
    // automap gate on (e.g. unit tests, restored save-states) must call
    // set_nr_0a_4_enable(true) explicitly.
    //
    // Pre-2026-05-04 this used to flip both gates, which made cold-boot
    // automap fire at PC=0x0000 (RST 0 entry per NR 0xB8 default 0x83)
    // and overlay enNxtmmc.rom on slot 0 throughout the boot ROM phase
    // — masking only by the now-retired G87 RETN-alias band-aid.
    bool prev = enabled_;
    port_io_enable_ = en;
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
    // VHDL divmmc.vhd:108,126,139 — i_retn_seen is a shared trigger that
    // clears all three DivMMC NMI-pipeline latches in the same clocked
    // process: button_nmi (line 108), automap_hold (126), automap_held
    // (139). JNEXT models all three clears here. The NMI-source plan's
    // Wave B row CLR-03 exercises the button_nmi clear via this hook.
    //
    // G46(a) note: the Emulator's `on_m1_cycle` lambda latches the
    // canonical ED 45 pulse through `on_m1_retn_delay()`, then calls
    // `on_retn_instruction_complete()` after CPU execution. That keeps
    // the overlay active for RETN itself and removes it before the
    // returned-to instruction is inspected.
    if (automap_active_ || automap_hold_ || automap_held_ || button_nmi_) {
        divmmc_log()->debug("RETN: automap+button_nmi cleared");
    }
    automap_active_ = false;
    automap_hold_   = false;
    automap_held_   = false;
    button_nmi_     = false;
    // Direct on_retn() supersedes the deferred path.
    retn_pending_clear_ = false;
}

void DivMmc::on_retn_instruction_complete() {
    if (!retn_pending_clear_) return;
    if (automap_active_ || automap_hold_ || automap_held_ || button_nmi_) {
        divmmc_log()->debug("RETN clear applied after instruction completion");
    }
    on_retn();
}

void DivMmc::on_m1_retn_delay_apply_(bool retn_seen) {
    if (retn_pending_clear_) {
        on_retn();
    }
    if (retn_seen) {
        retn_pending_clear_ = true;
    }
}

// ── Auto-mapping ──────────────────────────────────────────────────────

void DivMmc::check_automap(uint16_t pc, bool is_m1,
                           bool sram_pre_override_2,
                           bool sram_pre_override_0) {
    if (!is_m1 || !enabled_) {
        // P0 boot probe: log when the enable gate rejects the call, so a
        // "$0000 fetch with automap disabled" is visible (env-gated,
        // capped). Task 27 C-M1 moved this below the early-out check so
        // the rejected path costs two compares when the env var is
        // unset; the probe condition (pc<=0x38 AND is_m1 AND !enabled_)
        // is unchanged, so it fires identically when SET.
        if (pc <= 0x0038 && is_m1 && !enabled_ && boot_probe_env()) {
            static int gate_probe_count = 0;
            if (gate_probe_count < 20) {
                ++gate_probe_count;
                divmmc_log()->info(
                    "BOOT_PROBE check_automap pc={:#06x} REJECTED by enable gate "
                    "(port_io_enable={} nr_0a_4_enable={})",
                    pc, port_io_enable_, nr_0a_4_enable_);
            }
        }
        return;
    }

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
    const bool prev_held = automap_held_;
    automap_held_ = automap_hold_;

    // VHDL divmmc.vhd:112-113 — in the shared `button_nmi` process, the
    // `elsif automap_held = '1' then button_nmi <= '0'` clause re-clears
    // the latch every cycle while `automap_held` is high (subject to the
    // higher-priority i_reset / i_automap_reset / i_retn_seen clears and
    // the i_divmmc_button set above them).
    //
    // Pass-8 verify-audit fix (2026-05-09): pre-fix used a 0→1 rising-edge
    // one-shot clear on automap_held_, which diverged from VHDL whenever a
    // new `i_divmmc_button` strobe arrived AFTER held=1 had been latched
    // (e.g. user double-presses Drive button while overlay is held). VHDL
    // would clear it every subsequent clock; pre-fix kept it true until the
    // next held 0→1 edge or RETN. Practical impact: NmiSource consumes
    // `is_nmi_hold() = held OR button_nmi`, so the held=true masking
    // hid the divergence except on the falling edge of held — at which
    // point jnext kept the NMI hold asserted while VHDL released it.
    // Class-(b) → resolved by clearing button_nmi every check_automap call
    // when held remains 1, matching VHDL's continuous-while-held semantics.
    (void)prev_held;
    if (automap_held_ && button_nmi_) {
        divmmc_log()->debug(
            "button_nmi cleared while automap_held=1 "
            "(VHDL divmmc.vhd:112-113)");
        button_nmi_ = false;
    }

    // Step 2: Decode this PC. For each entry point we need: does it match,
    // is it currently enabled, and is its configured timing instant or
    // delayed. NR 0xB8 (entry_points_0_) enables RST 0x00..0x38; NR 0xBA
    // (entry_timing_0_) sets per-entry instant (1) vs delayed (0) timing;
    // NR 0xB9 (entry_valid_0_) gates each RST entry on "main path" (bit=1)
    // vs "ROM3 only path" (bit=0).
    bool instant_match = false;
    bool delayed_match = false;
    bool off_match     = false;

    // G46(b) — VHDL gate decomposition (zxnext.vhd:3137-3138 + divmmc.vhd:130,148).
    //
    //   i_automap_active      = sram_divmmc_automap_en
    //                         = sram_pre_override(2)
    //   i_automap_rom3_active = sram_divmmc_automap_rom3_en
    //                         = sram_pre_override(2) AND sram_pre_override(0)
    //                           AND (NOT sram_layer2_map_en)
    //                           AND (NOT sram_romcs)                  -- false in jnext
    //                           AND ((sram_altrom_en AND sram_pre_alt_128_n) OR
    //                                (sram_pre_rom3 AND NOT sram_altrom_en))
    //
    // The two gates control which entry-point bucket fires. Without them,
    // jnext was firing the ROM3 path during NextZXOS supervisor's
    // config_mode window (where VHDL would force pre_override(0)=0 →
    // ROM3-path blocked), causing the periodic boot loop tracked as G46(b).
    //
    // Note: jnext currently models the altrom branch as `rom3_active_`
    // alone (the boot path keeps NR 0x8C bit 7 clear, so altrom_en=0 and
    // the second clause `(rom3_sel AND !altrom_en) = rom3_sel` holds —
    // matching the VHDL at zxnext.vhd:3138 for that path). Full altrom
    // modelling can be added when an altrom-locked test case demands it.
    const bool main_path_eligible = sram_pre_override_2;
    const bool rom3_path_eligible =
        sram_pre_override_2 && sram_pre_override_0 &&
        !layer2_map_read_ && rom3_active_;

    // P0 boot probe (doc/issues/nextzxos-boot/ZXGO-COMPARISON-2026-07-09.md):
    // env-gated, capped; logs automap decision inputs at RST vectors to
    // diagnose the post-soft-reset $0000 trap.
    if (pc <= 0x0038 && boot_probe_env()) {
        static int probe_count = 0;
        if (probe_count < 60) {
            ++probe_count;
            divmmc_log()->info(
                "BOOT_PROBE check_automap pc={:#06x} enabled={} ov2={} ov0={} "
                "rom3_active={} B8/B9/BA={:#04x}/{:#04x}/{:#04x} "
                "hold={} held={} active={} conmem={}",
                pc, enabled_, sram_pre_override_2, sram_pre_override_0,
                rom3_active_, entry_points_0_, entry_valid_0_,
                entry_timing_0_, automap_hold_, automap_held_,
                automap_active_, conmem_);
        }
    }

    static constexpr uint16_t rst_addrs[8] = {
        0x0000, 0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030, 0x0038
    };
    for (int i = 0; i < 8; ++i) {
        if ((entry_points_0_ & (1 << i)) && pc == rst_addrs[i]) {
            const bool valid = (entry_valid_0_ & (1 << i)) != 0;
            const bool instant = (entry_timing_0_ & (1 << i)) != 0;
            // VHDL zxnext.vhd:2898-2902 splits each RST trap onto either
            // the main path (`divmmc_automap_*_on`) when its valid bit is
            // set, or the ROM3-conditional path
            // (`divmmc_automap_rom3_*_on`) when valid=0. Each path has its
            // own input gate in the divmmc entity (divmmc.vhd:130,148):
            //   main : i_automap_active      = sram_pre_override(2)
            //   rom3 : i_automap_rom3_active = full rom3_en composite
            const bool path_eligible = valid ? main_path_eligible
                                             : rom3_path_eligible;
            if (path_eligible) {
                if (instant) instant_match = true;
                else         delayed_match = true;
            }
            goto decode_done;
        }
    }

    // Non-RST entry points from NR 0xBB (entry_points_1_). VHDL timing is
    // documented in zxnext.vhd around :2892-2908. NMI@0x0066 uses
    // automap_nmi_instant_on (bit 1) AND automap_nmi_delayed_on (bit 0) —
    // BOTH bits fire on the same M1 in VHDL when both are set (default NR
    // 0xBB=$CD includes bit 0). Tape traps at 0x04C6/0x0562/0x04D7/0x056A
    // use rom3_delayed_on (ROM3-only). $3Dxx wildcard (bit 7) is rom3
    // instant_on. The clauses below are independent (not else-if) so
    // multiple bit-paths can fire on the same fetch — VHDL's `_on` signals
    // are independently OR'd into `automap_hold` (line 129) and `automap`
    // (line 148), so jnext mirrors that by accumulating into instant_match
    // / delayed_match.
    if (pc == 0x0066 && button_nmi_ && main_path_eligible) {
        // NMI@$0066 — VHDL divmmc.vhd:120-121 + zxnext.vhd:2907-2908.
        // Both nmi_instant_on (bit 1) and nmi_delayed_on (bit 0) gate on
        // button_nmi. They feed automap_hold (line 129) independently.
        // `automap` (combinational, line 148) only includes
        // automap_nmi_instant_on (via i_automap_active gate); the delayed
        // bit produces automap=1 only on the NEXT M1 via held promotion.
        if (entry_points_1_ & 0x02) instant_match = true;
        if (entry_points_1_ & 0x01) delayed_match = true;
    }
    if ((entry_points_1_ & 0x04) && pc == 0x04C6 && rom3_path_eligible) {
        // ROM3-only tape trap — VHDL zxnext.vhd:2902-2905 gates on the
        // full sram_divmmc_automap_rom3_en composite (pre_override(2)+(0)
        // + !layer2_map + ROM3 selector).
        delayed_match = true;
    }
    if ((entry_points_1_ & 0x08) && pc == 0x0562 && rom3_path_eligible) {
        delayed_match = true;
    }
    if ((entry_points_1_ & 0x10) && pc == 0x04D7 && rom3_path_eligible) {
        delayed_match = true;
    }
    if ((entry_points_1_ & 0x20) && pc == 0x056A && rom3_path_eligible) {
        delayed_match = true;
    }
    // $3Dxx wildcard (NR 0xBB bit 7) — VHDL zxnext.vhd:2898-2899:
    //   divmmc_automap_rom3_instant_on <= ... or (port_3dxx_msb and
    //                                              nr_bb_divmmc_ep_1(7));
    // port_3dxx_msb decodes cpu_a(15:8) = $3D, so any PC with high byte
    // $3D fires when bit 7 of NR $BB is set AND the ROM3 path is eligible.
    // This is the +3DOS RAM-disk trap entry. Default NR $BB = $CD has
    // bit 7 set, so this is on by default in ROM3 mode. Pre-fix jnext
    // missed this entirely. Fires `instant_match` (NOT delayed) per VHDL
    // line 2898, so it activates `automap` same-cycle when rom3_active=1.
    if ((entry_points_1_ & 0x80) && (pc & 0xFF00) == 0x3D00 && rom3_path_eligible) {
        instant_match = true;
    }
    // JNEXT_G46B_AUTOMAP_3DXX_TRACE=1 — log every $3Dxx PC eval with the
    // gate inputs. EOD-28 candidate: the $3Dxx wildcard automap trap may
    // fire in jnext during the supervisor's post-NEXTREG-$8E,$03 NOP-sled
    // at $3D00, overlaying slot 0/1 with DivMMC RAM and diverting the
    // CPU's execution. CSpect's gate composite at the same PC may NOT
    // fire automap there (per EOD-26 P4 Candidate C). Capture inputs so
    // the divergence is observable without source instrumentation in
    // CSpect.
    {
        static const char* env_3dxx = std::getenv("JNEXT_G46B_AUTOMAP_3DXX_TRACE");
        if (env_3dxx && (pc & 0xFF00) == 0x3D00) {
            static uint64_t hits = 0;
            ++hits;
            if (hits <= 8) {
                std::fprintf(stderr,
                    "G46B-v2 AUTOMAP_3DXX pc=%04x hit=%llu "
                    "ep1=%02x bit7=%d "
                    "pre_ovr2=%d pre_ovr0=%d layer2_map=%d rom3_active=%d "
                    "main_eligible=%d rom3_eligible=%d "
                    "instant_match=%d delayed_match=%d off_match=%d "
                    "automap_active_before=%d automap_hold_before=%d "
                    "conmem=%d port_io_en=%d enabled=%d\n",
                    pc, (unsigned long long)hits,
                    entry_points_1_, (entry_points_1_ >> 7) & 1,
                    sram_pre_override_2 ? 1 : 0,
                    sram_pre_override_0 ? 1 : 0,
                    layer2_map_read_ ? 1 : 0,
                    rom3_active_ ? 1 : 0,
                    main_path_eligible ? 1 : 0,
                    rom3_path_eligible ? 1 : 0,
                    instant_match ? 1 : 0,
                    delayed_match ? 1 : 0,
                    off_match ? 1 : 0,
                    automap_active_ ? 1 : 0,
                    automap_hold_ ? 1 : 0,
                    conmem_ ? 1 : 0,
                    port_io_enable_ ? 1 : 0,
                    enabled_ ? 1 : 0);
            }
        }
    }
    if ((entry_points_1_ & 0x40) && pc >= 0x1FF8 && pc <= 0x1FFF
               && main_path_eligible) {
        // Auto-unmap range (divmmc.vhd:131, automap_delayed_off factor).
        // VHDL line 131:
        //   automap_hold <= ... OR (automap_held AND NOT
        //                           (i_automap_active AND i_automap_delayed_off))
        // — the off-fire term IS gated by `i_automap_active`
        // (= sram_divmmc_automap_en = sram_pre_override(2)). When
        // pre_override(2)=0 (the only realistic case is mf_mem_en=1,
        // since the DivMMC overlay itself does NOT zero pre_override(2)
        // — overlay arbitration happens later at VHDL :3081), the held
        // latch propagates rather than dropping. Gate match accordingly.
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
            return ram_data()[3 * kRamPageSize + (addr & 0x1FFF)];
        } else {
            // Normal: read from DivMMC ROM
            return rom_data()[addr & 0x1FFF];
        }
    } else {
        // Slot 1: read from selected RAM bank
        return ram_data()[bank_ * kRamPageSize + (addr & 0x1FFF)];
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

    ram_data()[bank_ * kRamPageSize + (addr & 0x1FFF)] = val;
}

void DivMmc::save_state(StateWriter& w) const
{
    // Note: snapshot schema breaks back-compat with pre-2026-05-04 saves —
    // the two split enable levers (port_io_enable_, nr_0a_4_enable_) are
    // appended at the end of the stream (see below). This matches the
    // existing pattern for prior schema additions (button_nmi_,
    // layer2_map_read_, retn_pending_clear_) — see comments below. The
    // composite enabled_ byte stays at the front so the field order is
    // unchanged for the first half of the stream; the post-ram appendage
    // is the new state.
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
    // G46(a) — RETN delayed-clear pending flag. Appended last to keep
    // the stream layout append-only (matches earlier additions); breaks
    // backward compat with pre-G46(a) snapshots by design.
    w.write_bool(retn_pending_clear_);
    w.write_bytes(ram_data(), kRamSize);
    // VHDL-split enable levers (port_divmmc_io_en, nr_0a_divmmc_automap_en).
    // Saved independently so post-fix snapshots survive a load even when
    // they hold port_io=1 / nr_0a_4=0 (the firmware-reset shape that yields
    // composite enabled_=0). Same hard-versioned compat caveat as the
    // earlier additions above: pre-2026-05-04 snapshots are NOT readable
    // with this schema — `r.eof()` cannot subset the stream because
    // load_state reads from a single shared StateReader (the rest of the
    // Emulator follows immediately, see Emulator::load_state).
    w.write_bool(port_io_enable_);
    w.write_bool(nr_0a_4_enable_);
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
    // G46(a) — RETN delayed-clear pending flag. Same compat caveat.
    retn_pending_clear_ = r.read_bool();
    r.read_bytes(ram_data(), kRamSize);
    // VHDL-split enable levers (post-2026-05-04). Same hard-versioned compat
    // caveat as the additions above — pre-fix snapshots cannot be loaded
    // with this schema. The composite-derived defaults at lines 471-472 are
    // overwritten unconditionally with the persisted values.
    port_io_enable_ = r.read_bool();
    nr_0a_4_enable_ = r.read_bool();
}
