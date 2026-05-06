#include "core/emulator.h"

#include "core/embedded_nextboot_rom.h"
#include "core/log.h"
#include "core/nex_loader.h"
#include "core/sd_rom_extractor.h"
#include "core/sna_saver.h"
#include "core/saveable.h"
#include <algorithm>
#include <cstring>
#include <fstream>

// ---------------------------------------------------------------------------
// Constructor — initializer list for members with non-trivial dependencies.
// Declaration order in emulator.h determines construction order:
//   ram_ → rom_ → mmu_(ram_,rom_) → port_ → nextreg_ → cpu_(mmu_,port_)
// ---------------------------------------------------------------------------

Emulator::Emulator() : mmu_(ram_, rom_), cpu_(mmu_, port_) {
    // Task 3 Input runtime wiring: install the MembraneStick back-pointer
    // on Keyboard so read_rows() AND-folds joystick→membrane directions
    // per VHDL `zxnext_top_issue4.vhd:1843`. Done once in the ctor because
    // both members share Emulator lifetime and neither is ever reconstructed
    // at runtime. Invariant: `Keyboard::reset()` MUST NOT null this
    // back-pointer — it is lifetime-bound wiring, not soft-reset state.
    keyboard_.set_membrane_stick(&membrane_stick_);

    // G126 — Task 8 T2 W1 Agent B (Joystick→MembraneStick fold). The
    // Joystick decodes NR 0x05 and the per-connector mode must propagate
    // to MembraneStick so the membrane-fold's per-mode keymap region
    // (VHDL membrane_stick.vhd:117-149 `joy_type` driver) stays in sync.
    // Same lifetime invariant as the keyboard wiring above: lifetime-
    // bound, not soft-reset state. Joystick::reset() does NOT clear this
    // back-pointer.
    joystick_.set_membrane_stick(&membrane_stick_);

    // Audio Phase 1: wire the I2s stub into the Mixer. The Mixer does
    // not own the I2s; lifetime is the Emulator's. See
    // audio_mixer.vhd:89-90,99-100 for the 13-bit sum term.
    mixer_.set_i2s_source(&i2s_);
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

bool Emulator::init(const EmulatorConfig& cfg, bool preserve_memory)
{
    config_ = cfg;
    timing_ = machine_timing(cfg.type);
    Log::emulator()->info("Initializing emulator: machine_type={} cpu_speed={} lines={} tstates/line={}{}",
                          static_cast<int>(cfg.type), cpu_speed_str(cfg.cpu_speed),
                          timing_.lines_per_frame, timing_.tstates_per_line,
                          preserve_memory ? "  [preserve_memory=1 soft-reset]" : "");

    // Apply CPU speed to the clock.
    clock_.reset();
    clock_.set_cpu_speed(cfg.cpu_speed);

    // Allocate the framebuffer and fill with black (ARGB: 0xFF000000).
    framebuffer_.assign(FRAMEBUFFER_PIXELS, 0xFF000000u);

    // Clear any stale scheduler events from a previous session.
    scheduler_.reset();

    frame_cycle_ = 0;
    frame_num_   = 0;
    replay_mode_ = false;

    // Subsystem resets. RAM and the separate Rom buffer are skipped on
    // soft reset so tbblue-loaded content in SRAM (including the ROM-in-SRAM
    // window pages 0..7 for Next) survives RESET_SOFT, matching VHDL: SRAM
    // is not in any reset domain (only the FF-based subsystems are).
    if (!preserve_memory) {
        ram_.reset();
        rom_.reset();
    }
    // preserve_memory=true == soft reset (RESET_SOFT / NR 0x02 bit 0).
    // VHDL distinguishes the two reset domains: some MMU-side flip-flops
    // only clear on the hard `reset` signal (zxnext.vhd:1730
    // `reset <= i_RESET`). Thread the distinction through so soft reset
    // preserves nr_8c_altrom upper nibble, nr_08_contention_disable, and
    // the 7FFD paging lock per VHDL zxnext.vhd:2253-2256 / 4930-4935 /
    // 3646-3648.
    mmu_.reset(/*hard=*/!preserve_memory);
    nextreg_.reset();
    palette_.reset();
    layer2_.reset();
    sprites_.reset();
    tilemap_.reset();
    copper_.reset();
    cpu_.reset();
    im2_.reset();
    keyboard_.reset();
    // Input subsystem Phase 1 scaffold (Task 3). See src/input/*.
    joystick_.reset();
    mouse_.reset();
    md6_.reset();
    membrane_stick_.reset();
    iomode_.reset();
    // F-key FSM (G132) — VHDL emu_fnkeys.vhd:89,106,169,182 reset path:
    //   timer_count → 0, state → S_IDLE, cancel_nmi → 0, local_fnkeys → 0.
    emu_fnkeys_.reset();
    beeper_.reset();
    turbosound_.reset();
    dac_.reset();
    i2s_.reset();
    mixer_.reset();
    ctc_.reset();
    dma_.reset();
    spi_.reset();
    i2c_.reset();
    uart_.reset();
    divmmc_.reset();
    multiface_.reset(/*hard=*/true);
    nmi_source_.reset();
    // VHDL zxnext.vhd:5107 — `nr_d8_io_trap_fdc_en <= '0'` on i_reset.
    nr_d8_io_trap_fdc_en_ = false;
    // VHDL zxnext.vhd:3870, 3891 — both NR 0xDA cause field and NR 0xD9
    // captured-byte field reset to all zeros on i_reset.
    nr_d9_iotrap_write_ = 0;
    nr_da_iotrap_cause_ = 0;
    // Edge-detector level for the /NMI line. VHDL holds nmi_generate_n
    // inactive ('1') after reset; the falling-edge latch must see that
    // baseline so the first real assertion fires. Constructor-init alone
    // would let a stale `false` from a previous run mask the next NMI.
    prev_nmi_generate_n_ = true;
    rtc_.reset();
    sd_card_.reset();

    renderer_.reset();

    psg_accum_ = 0;
    sample_accum_ = 0;
    dac_enabled_ = false;
    // VHDL zxnext.vhd:1115-1120 reset defaults for NR 0x08 stored bits.
    // nr_08_psg_stereo_mode, nr_08_dac_en, nr_08_port_ff_rd_en,
    // nr_08_psg_turbosound_en, nr_08_keyboard_issue2 all default '0';
    // nr_08_internal_speaker_en defaults '1' (bit 4).
    nr_08_stored_low_ = 0x10;

    // Reset clip window write indices.
    clip_l2_idx_ = clip_spr_idx_ = clip_ula_idx_ = clip_tm_idx_ = 0;

    // G56: NR 0x10 coreid reset to VHDL default "00001" (zxnext.vhd:1133).
    nr_10_coreid_ = 0x01;

    // G112: NR 0x2D I2S sample latch (VHDL signal nr_2d_i2s_sample,
    // zxnext.vhd:1176). Stored pre-shifted into bits [7:6]; bits [5:0]
    // always zero. Power-on value is 0 (no I2S reads have occurred yet).
    nr_2d_i2s_sample_ = 0;

    // Reset line interrupt and IM2 hardware mode state.
    // VHDL zxnext.vhd:5092-5096 reset defaults:
    //   nr_c0_im2_vector          <= (others => '0');
    //   nr_c0_stackless_nmi       <= '0';
    //   nr_c0_int_mode_pulse_0_im2_1 <= '0';
    //   nr_c4_int_en_0_expbus     <= '1';
    // G71 (2026-04-28): line-int state lives on video_timing_; only the
    // ULA-int disable gate is local. video_timing_.init() is called in
    // run_emulator() / Emulator construction and resets the line-int
    // enable + target via the standard VideoTiming default-state path.
    ula_int_disabled_ = false;
    video_timing_.set_interrupt_enable(true);
    video_timing_.set_line_interrupt_enable(false);
    video_timing_.set_line_interrupt_target(0);
    // VHDL zxnext.vhd:3613-3614 — port_ff_reg <= (others => '0') on reset.
    port_ff_reg_ = 0;
    im2_hw_mode_ = false;
    im2_vector_base_ = 0;
    im2_int_enable_[0] = 0x81;  // legacy shadow; soft reset: ULA + expbus enabled
    im2_int_enable_[1] = 0;
    im2_int_enable_[2] = 0;
    im2_int_status_[0] = 0;
    im2_int_status_[1] = 0;
    im2_int_status_[2] = 0;
    im2_c4_expbus_     = true;   // NR 0xC4 bit 7 reset default '1'
    nr_c6_uart_int_en_ = 0;
    // G135 — NR 0xA0 Pi peripheral enable byte resets to 0x00 per VHDL
    // zxnext.vhd:5080 (nr_a0_pi_peripheral_en <= (others => '0')).
    nr_a0_pi_peripheral_en_ = 0;

    // VHDL zxnext.vhd:3516 — joy_iomode_pin7 resets to '1'. The pin7
    // state lives inside `iomode_` now; iomode_.reset() above already
    // restored it to '1'. Kept as a comment marker so future readers
    // see the explicit VHDL reference even though no field is set here.

    // Build contention LUT for the selected machine type.
    // MachineType is shared between emulator_config.h and contention.h
    // (emulator_config.h now includes contention.h for this definition).
    // Seed cpu_speed from the boot-time config so i_contention_en matches
    // the initial NR 0x07 state (VHDL zxnext.vhd:1300 cpu_speed power-on
    // "00"). The standalone Pentagon machine type was dropped (Wave 0.3
    // follow-up, 2026-05-04); the VHDL `machine_timing_pentagon` term
    // (NR 0x03 b2) is not currently wired into ContentionModel — see
    // contention.cpp::is_contended_access() comment for the rationale.
    contention_.build(cfg.type);
    contention_.set_cpu_speed(static_cast<uint8_t>(cfg.cpu_speed) & 0x03);

    // VideoTiming — production-wired raster counter used by the
    // contention tick path (Phase-2 wiring 2026-04-26). Mirrors VHDL
    // zxula_timing per-machine c_max_hc / c_max_vc / c_int_h / c_int_v.
    // The 60 Hz override flag is not yet plumbed through EmulatorConfig
    // (jnext defaults to 50 Hz only); pass `false` here.
    video_timing_.init(cfg.type, /*refresh_60hz=*/false);

    // Mirror the post-build per-slot contention state into Mmu so the
    // p3_floating_bus_dat latch (VHDL zxnext.vhd:4498-4509) sees the
    // same initial slot map as ContentionModel. ContentionModel::build()
    // seeds slot 1 contended for 48K/128K/+3 (bank 5 always at 0x4000-0x7FFF).
    for (int slot = 0; slot < 4; ++slot) {
        // Initial address representative of the slot — high 2 bits select 16K slot.
        uint16_t addr = static_cast<uint16_t>(slot) << 14;
        mmu_.set_slot_contended(slot, contention_.is_contended_address(addr));
    }

    // Push machine type into Mmu so Mmu::current_sram_rom() matches the
    // VHDL zxnext.vhd:2981-3008 sram_rom selection (48K always 0, +3 uses
    // 2-bit rom bank, Next/128K/Pentagon use 1-bit). Altrom lock bits
    // override per VHDL for +3 and Next variants.
    mmu_.set_machine_type(cfg.type);

    // Pulse-mode INT width gate per VHDL zxnext.vhd:2033 — 48K/+3 use 32 CPU
    // cycles (bit 5 only); 128K/Pentagon/Next use 36 (bit 5 AND bit 2).
    im2_.set_machine_timing_48_or_p3(cfg.type == MachineType::ZX48K || cfg.type == MachineType::ZX_PLUS3);

    // Wire ContentionModel into the FUSE memory/IO callbacks (Phase-2
    // contention plan, 2026-04-26). After this call, every Z80 memory
    // read/write/IORQ adds VHDL-faithful per-cycle contention via
    // ContentionModel::contention_tick(), gated by the live (hc, vc)
    // computed from the FUSE tstates counter and the machine's
    // tstates_per_line. Mmu provides mem_active_page per cycle.
    //
    // G141 + G53 (2026-05-01): the FUSE in-opcode contention macros
    // (contend_read/_no_mreq/_write_no_mreq) also flow through this
    // ContentionModel via CORETEST function overrides in z80_cpu.cpp,
    // and the legacy z80_build_contention_tables() / z80_set_page_contended()
    // were retired in lockstep.
    z80_set_contention_runtime(&contention_, &mmu_, cfg.type);

    // Clear all port dispatch handlers before re-registering them.
    // Without this, reset() → init() would duplicate every handler, causing
    // double-fired writes (breaking auto-increment ports like sprites/palette).
    port_.clear_handlers();
    port_.clear_io_observers();

    // Floating bus: unmatched port reads return ULA bus value in 48K/128K modes.
    port_.set_default_read([this](uint16_t) -> uint8_t {
        return floating_bus_read();
    });

    // ── Multiface port dispatch (Wave 1 B2 — TASK-8-MULTIFACE-PLAN.md) ──
    //
    // VHDL `multiface` decode runs in PARALLEL with the rest of the port
    // matrix (zxnext.vhd:2610-2616) — the LSBs 0x1F / 0x3F / 0x9F / 0xBF
    // also feed Kempston joystick (0x1F read), Profi/SD1 DAC (0x1F write,
    // 0x3F write), and the floating-bus default. Our most-specific-wins
    // dispatcher only invokes ONE handler per port; we therefore register
    // the MF strobe as a side-effect-only observer that fires before
    // handler dispatch and never affects the returned read value.
    //
    // Per-mode decode (zxnext.vhd:2612-2613, exhaustive table):
    //   nr_0a_mf_type  mode      enable_io   disable_io
    //     "00"         mode_p3    0x3F        0xBF
    //     "01"         mode_128   0xBF        0x3F
    //     "10"         mode_128   0x9F        0x1F
    //     "11"         mode_48    0x9F        0x1F
    //
    // Both bits of `mf_type` must be inspected (mode_128 covers both `01`
    // and `10` but with different LSBs). We use `multiface_.mf_type()`
    // and the per-bit decode straight from VHDL.
    //
    // The whole dispatch is gated on `port_multiface_io_en` (NR 0x83 b1)
    // via `multiface_.is_enabled()` — when disabled, the observer is a
    // no-op (same as the VHDL `port_multiface_io_en = '1'` AND in line 2615).
    port_.add_io_observer([this](uint16_t port, bool is_read) {
        if (!multiface_.is_enabled()) return;
        const uint8_t lsb = static_cast<uint8_t>(port & 0xFF);
        if (lsb != 0x1F && lsb != 0x3F &&
            lsb != 0x9F && lsb != 0xBF) {
            return;  // not an MF-relevant LSB
        }
        const uint8_t t = multiface_.mf_type();
        const bool t1 = (t & 0x02) != 0;
        const bool t0 = (t & 0x01) != 0;
        // Compute enable_io / disable_io for the active mode (VHDL
        // ternary cascade at 2612-2613).
        uint8_t enable_io  = t1 ? 0x9F : (t0 ? 0xBF : 0x3F);
        uint8_t disable_io = t1 ? 0x1F : (t0 ? 0x3F : 0xBF);
        const bool is_enable  = (lsb == enable_io);
        const bool is_disable = (lsb == disable_io);
        if (!is_enable && !is_disable) return;  // LSB not active for this mode
        if (is_enable) {
            if (is_read) multiface_.on_port_enable_rd(true);
            else         multiface_.on_port_enable_wr(true);
        } else {
            if (is_read) multiface_.on_port_disable_rd(true);
            else         multiface_.on_port_disable_wr(true);
        }
    });

    // ── Multiface +3 / MF128 readback mux (Wave 1 B3 — TASK-8-MULTIFACE-PLAN.md) ──
    //
    // VHDL zxnext.vhd:4310-4322 — when `mf_port_en = '1'`, the byte returned
    // from a Multiface readback IO depends on `nr_0a_mf_type`:
    //
    //   if nr_0a_mf_type = "00" (MF+3, mode_p3) — case mux on cpu_a(15:12):
    //     "0001" → "0000" & (NOT port_1ffd_mtr_n) & port_1ffd_reg(2:0)
    //                ≡ port_1ffd_ & 0x0F  (jnext stores cpu_do verbatim)
    //     "0111" → port_7ffd_reg                 (full 8-bit register)
    //     "1101" → '0' & port_dffd_reg_6 & '0' & port_dffd_reg(4:0)
    //                ≡ (reg_6 ? 0x40 : 0) | (port_dffd_reg & 0x1F)
    //     "1110" → "0000" & port_eff7_reg_3 & port_eff7_reg_2 & "00"
    //                ≡ (reg_3 ? 0x08 : 0) | (reg_2 ? 0x04 : 0)
    //     others → "00000" & port_fe_reg(2 downto 0)        (VHDL :4316)
    //                ≡ Ula::get_border() (lower 3 border bits of port 0xFE)
    //
    //   else (mf_type ≠ "00" — MF128 var A "01", var B "10"; MF1 "11"
    //   excluded by mf_port_en gate at multiface.vhd:195):
    //     mf_port_dat <= port_7ffd_reg(3) & "1111111"          (VHDL :4319)
    //                ≡ (port_7ffd_reg & 0x08 ? 0x80 : 0) | 0x7F
    //   The case mux is BYPASSED in MF128 mode — cpu_a(15:12) is irrelevant.
    //
    // The combinational `mf_port_en` (multiface.vhd:195) is asserted iff
    //   port_mf_enable_rd_i = '1'
    //   AND invisible_eff = '0' (= invisible AND NOT mode_48)
    //   AND (mode_128 = '1' OR mode_p3 = '1')
    // `port_mf_enable_rd` only fires for the per-mode enable_io_a LSB
    // (zxnext.vhd:2612):
    //     mf_type=00 (MF+3)         → enable_io_a = 0x3F
    //     mf_type=01 (MF128 var A)  → enable_io_a = 0xBF
    //     mf_type=10 (MF128 var B)  → enable_io_a = 0x9F
    //     mf_type=11 (MF1 / 48K)    → enable_io_a = 0x9F   (gated off by :195)
    // Each LSB therefore needs its own read handler. We register all three
    // active LSBs (0x3F, 0xBF, 0x9F) and gate per-LSB on the matching
    // mf_type plus the cached `mf_port_en` (which the dispatch observer
    // updates BEFORE the read handler runs — see PortDispatch::read).
    //
    // When the gate is closed (MF disabled / wrong mode / invisible_eff=1)
    // the handler falls back to the floating-bus default, mirroring VHDL's
    // `port_mf_rd_dat <= ... else X"00"` OR'd into `port_rd_dat` (the bus
    // is otherwise driven by Profi-DAC-WRITE-only at this LSB, so reads
    // see floating bus on real hardware).

    // ── MF+3 readback at LSB 0x3F (mf_type=00) — case mux on cpu_a(15:12) ──
    port_.register_handler(0x00FF, 0x003F,
        [this](uint16_t cpu_a) -> uint8_t {
            if (!multiface_.is_enabled() ||
                multiface_.mf_type() != 0x00 ||  // MF+3 only (case "00")
                multiface_.invisible_eff()) {
                return floating_bus_read();
            }
            const uint8_t a_high = static_cast<uint8_t>((cpu_a >> 12) & 0x0F);
            switch (a_high) {
                case 0x1:  // 0x1xxx — port_1ffd readback (motor + reg(2:0))
                    return static_cast<uint8_t>(mmu_.port_1ffd() & 0x0F);
                case 0x7:  // 0x7xxx — port_7ffd readback (full 8-bit reg)
                    return mmu_.port_7ffd();
                case 0xD: { // 0xDxxx — port_dffd readback (with bit 6)
                    const uint8_t b6 = mmu_.port_dffd_reg_6() ? 0x40 : 0x00;
                    return static_cast<uint8_t>(b6 | (mmu_.port_dffd_reg() & 0x1F));
                }
                case 0xE: { // 0xExxx — port_eff7 readback (bits 3 + 2)
                    const uint8_t b3 = mmu_.port_eff7_ram_at_0000()  ? 0x08 : 0x00;
                    const uint8_t b2 = mmu_.port_eff7_disable_p1024() ? 0x04 : 0x00;
                    return static_cast<uint8_t>(b3 | b2);
                }
                default:
                    // VHDL zxnext.vhd:4316: "when others => mf_port_dat <=
                    // "00000" & port_fe_reg(2 downto 0)" — return the lower
                    // 3 bits of port_FE (the border colour latch).
                    return static_cast<uint8_t>(renderer_.ula().get_border() & 0x07);
            }
        },
        nullptr);  // No write side: Profi DAC owns the WRITE half at 0x003F
                   // via a more-specific 0xFFFF mask handler below.

    // ── MF128 readback at LSB 0xBF (mf_type=01 — MF128 var A) ─────────────
    // VHDL zxnext.vhd:4319 else-branch:
    //   mf_port_dat <= port_7ffd_reg(3) & "1111111"
    // Gated by mf_port_en (multiface.vhd:195) — which the dispatch observer
    // pulses for this LSB only when mf_type=01.
    port_.register_handler(0x00FF, 0x00BF,
        [this](uint16_t /*cpu_a*/) -> uint8_t {
            if (!multiface_.is_enabled() ||
                multiface_.invisible_eff() ||
                multiface_.mf_type() != 0x01) {  // MF128 var A only
                return floating_bus_read();
            }
            // MF128 readback (VHDL :4319): port_7ffd_reg(3) & 0x7F.
            const uint8_t shadow_bit =
                (mmu_.port_7ffd() & 0x08) ? 0x80 : 0x00;
            return static_cast<uint8_t>(shadow_bit | 0x7F);
        },
        nullptr);  // No write side at LSB 0xBF.

    // ── MF128 readback at LSB 0x9F (mf_type=10 — MF128 var B) ─────────────
    // VHDL :2612 routes mf_type=10 to enable_io_a=0x9F; the mf_port_en gate
    // at multiface.vhd:195 still requires (mode_128 OR mode_p3), and mf_type=10
    // → mode_128=1, so this LSB also fires the else-branch readback.
    // mf_type=11 (MF1 / 48K) ALSO uses enable_io_a=0x9F (VHDL :2612), but
    // mode_48=1 fails the (mode_128 OR mode_p3) gate, so mf_port_en=0 and
    // the handler falls through to floating bus — identical to VHDL.
    port_.register_handler(0x00FF, 0x009F,
        [this](uint16_t /*cpu_a*/) -> uint8_t {
            if (!multiface_.is_enabled() ||
                multiface_.invisible_eff() ||
                multiface_.mf_type() != 0x02) {  // MF128 var B only
                return floating_bus_read();
            }
            // MF128 readback (VHDL :4319): port_7ffd_reg(3) & 0x7F.
            const uint8_t shadow_bit =
                (mmu_.port_7ffd() & 0x08) ? 0x80 : 0x00;
            return static_cast<uint8_t>(shadow_bit | 0x7F);
        },
        nullptr);  // No write side at LSB 0x9F.

    // Memory contention flows through ContentionModel::contention_tick()
    // (Phase-2 wiring 2026-04-26 + G141 in-opcode 2026-05-01) — the FUSE
    // memory/IO callbacks and the contend_read[_no_mreq]/_write_no_mreq
    // overrides in src/cpu/z80_cpu.cpp call it per cycle. No external
    // on_contention callback is needed.
    cpu_.on_contention = nullptr;

    // DivMMC auto-map must fire BEFORE the opcode fetch so the memory
    // overlay is active for the same M1 read that triggered it (matching
    // the VHDL combinatorial address decode).
    //
    // The NMI source pipeline (TASK-NMI-SOURCE-PIPELINE-PLAN.md Phase 1)
    // also observes every M1 prefetch: its FSM advances FETCH -> HOLD on
    // a PC=0x0066 M1 fetch (VHDL:2135-2138 `mf_a_0066`). We feed both
    // subsystems from the same closure so the PC-observation seam stays
    // single-sourced. Phase 1 leaves NmiSource gated off (no producers
    // wired yet), so this call is inert; it's in place so Waves A/B/E
    // don't need to touch Emulator plumbing.
    cpu_.on_m1_prefetch = [this](uint16_t pc) {
        // G46(b) — compute the VHDL `sram_pre_override(2)` and `(0)`
        // priority-arbiter bits for this M1 fetch so DivMmc::check_automap
        // can apply the gates from zxnext.vhd:3137-3138 (i_automap_active
        // = sram_divmmc_automap_en, i_automap_rom3_active =
        // sram_divmmc_automap_rom3_en). Without these gates jnext fires
        // the ROM3-conditional AUTOMAP trap during the NextZXOS supervisor's
        // config_mode window and gets stuck in the boot loop tracked as
        // G46(b). VHDL inputs:
        //   * mf_mem_en       — Multiface::is_mem_active()
        //   * nr_03_config_mode — NextReg::nr_03_config_mode()
        //   * mmu_A21_A13(8)  — Mmu::slot_in_rom_area(slot)
        const bool mf_active = multiface_.is_mem_active();
        const bool config_mode = nextreg_.nr_03_config_mode();
        const bool sram_pre_override_2 =
            mmu_.sram_pre_override_divmmc_eligible(pc, mf_active);
        const bool sram_pre_override_0 =
            mmu_.sram_pre_override_romcs_priority(pc, mf_active, config_mode);
        divmmc_.check_automap(pc, true,
                              sram_pre_override_2,
                              sram_pre_override_0);
        // m1 / mreq are both true during the M1 prefetch M-cycle.
        nmi_source_.observe_m1_fetch(pc, true, true);
        // Wave 1 B1 — feed the Multiface FSM. VHDL `cpu_a_0066_i`
        // (multiface.vhd:58) and `cpu_m1_n_i='0' AND cpu_mreq_n_i='0'`
        // (lines 169, 176) are sampled during the same M1 fetch the
        // DivMmc check_automap above observes. The FSM updates
        // `mf_enable` on this edge and surfaces fetch_66 via
        // mf_enable_eff for the same cycle.
        multiface_.on_m1(pc, /*mreq_low=*/true);
    };

    // Install M1-cycle callback. RETI/RETN decoding splits along two
    // paths because the consumers have *different* requirements:
    //
    //   - Im2Controller (RETI-acknowledge daisy walk): driven by the
    //     VHDL-faithful im2_control.vhd:70-240 FSM inside Im2Controller.
    //     That FSM only fires reti_seen_this_cycle() on canonical ED 4D,
    //     matching VHDL.
    //
    //   - DivMmc (automap hold/held clear, divmmc.vhd:126,139): driven
    //     from im2_'s retn_seen_pulse_ — fires only on canonical ED 45
    //     (VHDL-faithful, im2_control.vhd:236).
    //
    // G87 (2026-05-03): Z80Cpu::execute() now delivers BOTH bytes of an
    //   ED-prefix opcode to on_m1_cycle (Z80N + non-Z80N paths in
    //   z80_cpu.cpp), so Im2Controller::advance_decoder() now correctly
    //   transitions S_0 → S_ED_T4 → {S_ED4D_T4 | S_ED45_T4} and surfaces
    //   reti_seen_pulse_ / retn_seen_pulse_. The old prev_ed band-aid
    //   (which fired divmmc_.on_retn() whenever the byte AFTER any ED
    //   instruction matched 0x45/55/5D/65/6D/75/7D — i.e. false-fired on
    //   any LD r,L instruction following an ED block) has been retired.
    //
    //   Legacy alias bytes 0x55/5D/65/6D/75/7D are intentionally NOT
    //   handled: VHDL im2_control.vhd:236 matches only canonical 0x45 for
    //   o_retn_seen. The pre-G87 alias-firing was undocumented Z80 lore;
    //   z80 hardware does treat these as RETN-equivalent in execution but
    //   the ZX Next VHDL only reacts to the canonical encoding for the
    //   divmmc/mmc/nmi-shadow signals. If a regression surfaces in
    //   tbblue.fw boot or a divmmc test, revisit this decision (the
    //   proper fix would be to widen im2_control.vhd:236 — see
    //   doc/issues/NEXTZXOS-BOOT-INVESTIGATION.md for context).
    cpu_.on_m1_cycle = [this](uint16_t pc, uint8_t opcode) {
        im2_.on_m1_cycle(pc, opcode);
        if (im2_.reti_seen_this_cycle()) {
            im2_.on_reti();
        }
        if (im2_.retn_seen_this_cycle()) {
            im2_.on_retn();
        }
        // G46(a) — proper "delayed-off" automap_held clear.
        // Im2Controller::retn_seen_this_cycle() pulses ONLY on the M1
        // that completes a canonical ED 45 (im2_control.vhd:236), so the
        // pre-G87 RETN-alias band-aid (which fired on any post-ED byte
        // matching 0x45/55/5D/65/6D/75/7D — e.g. a stray LD r,L after
        // ED) is fully retired. DivMmc latches the pulse into a
        // one-M1-cycle delay register: the held/hold/active/button_nmi
        // shadows drop on the NEXT M1 fetch (the first byte of the
        // returned-to instruction), letting the overlay survive RETN's
        // own ED 45 fetch as the VHDL register-shift shape requires.
        // This call fires on EVERY M1 (including ED + ext byte fetches
        // and the next instruction's fetch), so the delay register
        // sees its trigger on the ED 45 ext-byte M1 and applies the
        // clear on the very next M1.
        //
        // Wave 1 F-gate — VHDL zxnext.vhd:4111:
        //   `divmmc_retn_seen <= z80_retn_seen_28 AND NOT mf_is_active;`
        // When MF is active (mf_mem_en OR mf_nmi_hold per :2099), the
        // RETN pulse is suppressed for DivMMC's automap-clear path.
        // DivMMC stays in its current state until MF deactivates; MF's
        // own retn-clear path (multiface.vhd:144,178) is direct and
        // remains wired below.
        divmmc_.on_m1_retn_delay(im2_.retn_seen_this_cycle() && !multiface_.is_active());

        // Wave 1 B1 — Multiface RETN clear. VHDL multiface.vhd:144,178 —
        // cpu_retn_seen_i='1' clears nmi_active and mf_enable in the same
        // clocked process. We feed the same Im2Controller pulse the DivMmc
        // delay-clear path uses; unlike DivMmc we apply the clear
        // immediately (no one-cycle delay register, since multiface.vhd
        // doesn't have a held/hold staging shadow).
        if (im2_.retn_seen_this_cycle()) {
            multiface_.on_retn_seen();
        }
    };

    // G88: latch NR 0xC2 (PC LSB) / NR 0xC3 (PC MSB) from the NMI return
    // address at NMI service time. Mirrors VHDL zxnext.vhd:2050-2085 +
    // 6232-6236 (Z80N_command_s = NMIACK_LSB/MSB & cpu_wr_n='0'). The CPU
    // hands us the post-HALT-fix PC that will be pushed to stack.
    cpu_.on_nmi_servicing = [this](uint16_t saved_pc) {
        nextreg_.set_nmi_return_address(saved_pc);
    };

    // Z80 IntAck vector fetch callback — when the Z80 enters an interrupt
    // acknowledge M1 cycle, pull the vector from the IM2 fabric (Agent B's
    // ack_vector() walks dev_[] priority order and latches the winning
    // device into S_ACK). In pulse mode the fabric's state machine holds
    // all devices at S_0, so ack_vector() returns 0xFF — byte-identical to
    // the legacy cpu_.request_interrupt(0xFF) + int_vector_=0xFF path.
    cpu_.on_int_ack = [this]() -> uint8_t {
        return im2_.ack_vector();
    };

    // Magic breakpoint: ED FF (ZEsarUX) / DD 01 (CSpect) trigger debugger pause.
    if (cfg.magic_breakpoint) {
        cpu_.on_magic_breakpoint = [this](uint16_t pc) -> bool {
            Log::emulator()->info("Magic breakpoint hit at PC={:#06x}", pc);
            debug_state_.set_active(true);
            debug_state_.pause();
            return true;
        };
    }

    // --- NextREG write handlers ---

    // Register 0x00: Machine ID (read-only).
    // VHDL zxnext.vhd:5884-5885 — read dispatch routes nr_register=X"00"
    // unconditionally to g_machine_id; writes have no handler in the VHDL
    // write dispatch, so they are discarded. We install a read_handler that
    // always returns 0x08 (HWID_EMULATORS), making any stale byte written
    // into regs_[0] invisible. JNEXT deliberately reports 0x08 instead of
    // the VHDL g_machine_id=X"0A" so NextZXOS takes its emulator-aware boot
    // paths — see NextReg::reset() comment at src/port/nextreg.cpp:18.
    nextreg_.set_read_handler(0x00, []() -> uint8_t { return 0x08; });

    // Register 0x07: CPU speed selector
    //   0 = 3.5 MHz, 1 = 7 MHz, 2 = 14 MHz, 3 = 28 MHz
    // VHDL zxnext.vhd:1300 nr_07_cpu_speed reset "00"; zxnext.vhd:5789-5791,5817
    // stores the 2-bit field; zxnext.vhd:4481 feeds it into i_contention_en
    // (any non-zero speed disables memory contention).
    nextreg_.set_write_handler(0x07, [this](uint8_t v) -> uint8_t {
        CpuSpeed speed = static_cast<CpuSpeed>(v & 0x03);
        Log::emulator()->info("CPU speed changed to {} (NextREG 0x07={:#04x})", cpu_speed_str(speed), v);
        // VHDL zxnext.vhd:5788-5789 — NR 0x07 write updates the
        // `nr_07_cpu_speed` register immediately (this IS the shadow).
        // The effective `cpu_speed` at zxnext.vhd:5817 only commits on
        // the next CLK_CPU rising edge that satisfies the bus-idle gate
        // (mreq_n='1' AND iorq_n='1' AND m1_n='1' AND dma_holds_bus='0',
        // zxnext.vhd:5809). jnext therefore drives the shadow here and
        // lets Emulator::run_frame()'s post-instruction tick (where the
        // CPU is naturally bus-idle) commit shadow → effective via
        // commit_pending_cpu_speed_on_bus_idle(). G142 closure.
        clock_.set_pending_cpu_speed(speed);
        contention_.set_pending_cpu_speed(v & 0x03);
        return v;
    });
    // NR 0x07 read is a COMPOSED format, not a raw register round-trip.
    // VHDL zxnext.vhd:5902-5903:
    //   port_253b_dat <= "00" & cpu_speed & "00" & nr_07_cpu_speed;
    // i.e. bits[5:4] = actual cpu_speed, bits[1:0] = requested nr_07_cpu_speed.
    // JNEXT does not model the expbus speed override (VHDL zxnext.vhd:5816-5820
    // assigns cpu_speed <= nr_07_cpu_speed when expbus_en = '0'), so the
    // effective actual speed tracks the requested value directly (the
    // write handler above drives both clock_ and contention_ from the
    // low 2 bits of the last write, which live in regs_[0x07]).
    nextreg_.set_read_handler(0x07, [this]() -> uint8_t {
        const uint8_t req = nextreg_.cached(0x07) & 0x03;
        const uint8_t act = req;  // expbus not modelled — actual follows requested
        return static_cast<uint8_t>((act << 4) | req);
    });

    // Register 0x12: Layer 2 active RAM bank
    nextreg_.set_write_handler(0x12, [this](uint8_t v) -> uint8_t {
        layer2_.set_active_bank(v);
        return v;
    });
    // VHDL zxnext.vhd:5930 — port_253b_dat <= '0' & nr_12_layer2_active_bank;
    // read returns the 7-bit authoritative state, bit 7 always 0. Bare
    // NextReg::write stores the raw byte in regs_[0x12], so without this
    // read_handler a write of 0xC5 would read back 0xC5 instead of the
    // VHDL-spec 0x45. Pull directly from Layer2 where the masked value lives.
    nextreg_.set_read_handler(0x12, [this]() -> uint8_t {
        return layer2_.active_bank() & 0x7F;
    });

    // Register 0x13: Layer 2 shadow RAM bank
    nextreg_.set_write_handler(0x13, [this](uint8_t v) -> uint8_t {
        layer2_.set_shadow_bank(v);
        // Mirror to Mmu so that port 0x123B bit-3 (map_shadow) routes the
        // CPU L2 read/write-over through this bank — VHDL zxnext.vhd:2968
        // selects nr_13_layer2_shadow_bank when port_123b_layer2_map_shadow='1'.
        mmu_.set_l2_shadow_bank(v);
        return v;
    });
    // VHDL zxnext.vhd:5931 — port_253b_dat <= '0' & nr_13_layer2_shadow_bank;
    // same pattern as NR 0x12 read above — 7-bit masked read from Layer2.
    nextreg_.set_read_handler(0x13, [this]() -> uint8_t {
        return layer2_.shadow_bank() & 0x7F;
    });

    // Register 0x14: Global transparency colour (Layer2/ULA/LoRes)
    // VHDL zxnext.vhd:7100 — compared against palette RGB[8:1] for
    // ULA and Layer 2 transparency at the compositor stage.
    nextreg_.set_write_handler(0x14, [this](uint8_t v) -> uint8_t {
        palette_.set_global_transparency(v);
        renderer_.set_transparent_rgb(v);
        return v;
    });

    // Register 0x40: Palette index
    nextreg_.set_write_handler(0x40, [this](uint8_t v) -> uint8_t {
        palette_.set_index(v);
        return v;
    });
    // VHDL zxnext.vhd:6035-6036 — port_253b_dat <= nr_palette_idx;
    // Read returns the live 8-bit palette index. The index auto-increments
    // after NR 0x44 9-bit completes and (when not gated by NR 0x43 bit 7)
    // after NR 0x41 8-bit writes — see PaletteManager::write_entry →
    // advance_index. Without this handler, regs_[0x40] holds only the last
    // explicit NR 0x40 write and masks autoinc state (G56-CR-40).
    nextreg_.set_read_handler(0x40, [this]() -> uint8_t {
        return palette_.get_index();
    });

    // Register 0x41: Palette value 8-bit (RRRGGGBB)
    nextreg_.set_write_handler(0x41, [this](uint8_t v) -> uint8_t {
        palette_.write_8bit(v);
        return v;
    });
    // VHDL zxnext.vhd:6038-6039 — NR 0x41 read returns nr_palette_dat(8:1),
    // i.e. the upper 8 bits of the stored RGB333 at the currently selected
    // target palette + index. Without this handler, reads return the stale
    // regs_[0x41] last-write value, masking palette-state bugs (PAL-01/03/06).
    nextreg_.set_read_handler(0x41, [this]() -> uint8_t {
        return palette_.read_8bit();
    });

    // Register 0x43: Palette control
    // Bit 0 drives ULAnext enable (zxnext.vhd:5394). The palette-group bits
    // (7, 6:4, 3, 2, 1) stay owned by PaletteManager. Phase-1 scaffold wires
    // the narrow bit 0 → Ula::set_ulanext_en; the remaining bits are left to
    // palette_.write_control(v) unchanged.
    nextreg_.set_write_handler(0x43, [this](uint8_t v) -> uint8_t {
        palette_.write_control(v);
        renderer_.ula().set_ulanext_en((v & 0x01) != 0);
        // G10: mirror NR 0x43 b1-3 selector bits into Ula's per-scanline
        // selector change-log. PaletteManager owns the live state used by
        // the rasterizer; Ula owns the change-log replayed per scanline so
        // Copper-driven mid-frame selector flips (e.g. split-bank palette
        // bands) take effect on the next scanline rather than collapsing
        // to last-write-wins. VHDL zxnext.vhd:5391-5393, :6825-6828.
        renderer_.ula().set_active_ula_palette((v & 0x02) != 0);
        renderer_.ula().set_active_layer2_palette((v & 0x04) != 0);
        renderer_.ula().set_active_sprite_palette((v & 0x08) != 0);
        return v;
    });
    // VHDL zxnext.vhd:6044-6045 — port_253b_dat <=
    //   nr_43_palette_autoinc_disable & nr_43_palette_write_select &
    //   nr_43_active_sprite_palette  & nr_43_active_layer2_palette &
    //   nr_43_active_ula_palette     & nr_43_ulanext_en;
    // Layout: [7]=autoinc_disable, [6:4]=palette_write_select (3b),
    //         [3]=spr, [2]=l2, [1]=ula, [0]=ulanext_en (8 bits total).
    // PaletteManager owns the authoritative store via write_control() which
    // re-derives the bit fields from the byte. Without this handler the
    // bare regs_[0x43] is also storing the same byte today, but pulling
    // from PaletteManager makes the read source-of-truth aligned with the
    // rest of the palette state and shields against future write-paths
    // that mutate palette state without touching regs_[] (G56-CR-43).
    nextreg_.set_read_handler(0x43, [this]() -> uint8_t {
        return palette_.read_control();
    });

    // Register 0x44: Palette value 9-bit (two consecutive writes)
    nextreg_.set_write_handler(0x44, [this](uint8_t v) -> uint8_t {
        palette_.write_9bit(v);
        return v;
    });

    // Register 0xFF: ULA+ palette poke side-channel — VHDL zxnext.vhd:6957-6958.
    // NR 0xFF writes commit a single palette entry into the ULA palette region
    // at slot (NR 0x43 b6 = bank, port 0xBF3B b5:0 = index), independent of the
    // NR 0x40 / NR 0x41 / NR 0x44 indexing used by the regular palette I/O
    // path.  Value byte expansion follows zxnext.vhd:4919 (RRRGGGBB →
    // RRRGGGBBB with B0 = B1|B0); priority is "00" per zxnext.vhd:4920 since
    // nr_44_we does not fire.
    nextreg_.set_write_handler(0xFF, [this](uint8_t v) -> uint8_t {
        // nr_43_palette_write_select(2) = NR 0x43 bit 6 (zxnext.vhd:5390:
        // `nr_43_palette_write_select <= nr_wr_dat(6 downto 4)`).
        const bool bank_second = (palette_.read_control() & 0x40) != 0;
        const uint8_t bf3b_index = renderer_.ula().get_ulap_index();
        palette_.nr_ff_poke(bank_second, bf3b_index, v);
        return v;
    });

    // Register 0x4B: Sprite transparency index
    nextreg_.set_write_handler(0x4B, [this](uint8_t v) -> uint8_t {
        palette_.set_sprite_transparency(v);
        return v;
    });

    // Register 0x4C: Tilemap transparency index
    // VHDL zxnext.vhd:6056 — port_253b_dat <= "0000" & nr_4c_tm_transparent_index;
    // bits 7:4 are constant '0', bits 3:0 carry the 4-bit tilemap transparent
    // palette index. G56 cluster D, option (b): the canonical byte is the
    // masked write value; PaletteManager has the only writer for
    // tilemap_transparency_ outside reset/save-load, so storing `v & 0x0F` in
    // regs_[0x4C] keeps NextReg::read aligned with the live subsystem state
    // and the read_handler is no longer needed.
    nextreg_.set_write_handler(0x4C, [this](uint8_t v) -> uint8_t {
        palette_.set_tilemap_transparency(v);
        return static_cast<uint8_t>(v & 0x0F);
    });

    // Register 0x16: Layer 2 X scroll LSB
    nextreg_.set_write_handler(0x16, [this](uint8_t v) -> uint8_t {
        layer2_.set_scroll_x_lsb(v);
        return v;
    });

    // Register 0x17: Layer 2 Y scroll
    nextreg_.set_write_handler(0x17, [this](uint8_t v) -> uint8_t {
        layer2_.set_scroll_y(v);
        return v;
    });

    // Register 0x70: Layer 2 control (resolution + palette offset)
    // VHDL zxnext.vhd:6113 — port_253b_dat <= "00" & nr_70_layer2_resolution &
    // nr_70_layer2_palette_offset.  Bits 7:6 are constant "00"; bits 5:4 are
    // the 2-bit resolution; bits 3:0 are the 4-bit palette offset — i.e. the
    // read-back is `v & 0x3F`.  G56 Phase 2 (cluster E): canonicalise on the
    // write side and let NextReg::read() fall through regs_[0x70].
    nextreg_.set_write_handler(0x70, [this](uint8_t v) -> uint8_t {
        layer2_.set_control(v);
        return static_cast<uint8_t>(v & 0x3F);
    });

    // Register 0x71: Layer 2 X scroll MSB
    // VHDL zxnext.vhd:6116 — port_253b_dat <= "0000000" & nr_71_layer2_scrollx_msb.
    // Only bit 0 carries information; bits 7:1 are constant zero.  G56 Phase 2
    // (cluster E): canonicalise on the write side; NR 0x71 is the only writer
    // of bit 8 of Layer2's 9-bit scroll_x, so `v & 0x01` is the read-back.
    nextreg_.set_write_handler(0x71, [this](uint8_t v) -> uint8_t {
        layer2_.set_scroll_x_msb(v);
        return static_cast<uint8_t>(v & 0x01);
    });

    // VHDL zxnext.vhd:5909 — NR 0x09 read composes:
    //   nr_09_psg_mono [7:5] & nr_09_sprite_tie [4] & '0' [3]
    //     & (NOT nr_09_hdmi_audio_en) [2] & eff_nr_09_scanlines [1:0]
    // Bit 3 always reads 0; bit 4 sourced from sprites_.mirror_tie() (the
    // authoritative store wired through Sprites). Bits 7:5 (psg_mono) and
    // bits 1:0 (eff_scanlines) are not separately latched in jnext yet —
    // both echo the last-write byte through the cached regs_[0x09] (the
    // VHDL latches them too, just into named signals). Bit 2 reads back
    // the WRITE bit 2: VHDL stores `not nr_wr_dat(2)` so the read of
    // `not nr_09_hdmi_audio_en` returns nr_wr_dat(2) — same as cached. G56.
    nextreg_.set_read_handler(0x09, [this]() -> uint8_t {
        const uint8_t cached = nextreg_.cached(0x09);
        // Mask: drop bit 4 (sprite_tie comes from sprites_) and bit 3
        // (always 0 per VHDL).
        const uint8_t base = static_cast<uint8_t>(cached & 0xE7);
        const uint8_t tie  = sprites_.mirror_tie() ? 0x10 : 0x00;
        return static_cast<uint8_t>(base | tie);
    });

    // Register 0x0A: Peripheral 2 — SD-card swap + mouse button reverse + DPI.
    //   bits 7:6 = nr_0a_mf_type      (VHDL zxnext.vhd:5191) — config_mode-gated
    //   bit 5    = nr_0a_sd_swap      (VHDL zxnext.vhd:5193) — config_mode-gated
    //   bit 4    = divmmc_automap_en  (VHDL zxnext.vhd:5196) — gates DivMMC automap reset
    //   bit 3    = nr_0a_mouse_button_reverse (VHDL zxnext.vhd:5197)
    //   bits 1:0 = nr_0a_mouse_dpi    (VHDL zxnext.vhd:5198)
    // VHDL zxnext.vhd:3308-3322 (port_e7 decode uses nr_0a_sd_swap).
    // VHDL zxnext.vhd:5191-5198: bits 7:6 (mf_type) and bit 5 (sd_swap) only
    // commit when nr_03_config_mode='1'. Bits 4/3/1:0 commit unconditionally.
    nextreg_.set_write_handler(0x0A, [this](uint8_t v) -> uint8_t {
        // G131: gate bits 7:6 and bit 5 on nr_03_config_mode.
        if (nextreg_.nr_03_config_mode()) {
            spi_.set_sd_swap((v & 0x20) != 0);
            // Wave 1 B1 — bits 7:6 forward as `mf_mode_i` to the Multiface
            // (multiface.vhd:67-68, zxnext.vhd:5191). Set inside the
            // config_mode gate to mirror VHDL: the FF that stores
            // `nr_0a_mf_type` only commits when nr_03_config_mode='1'.
            multiface_.set_mode(static_cast<uint8_t>((v >> 6) & 0x03));
        }
        // G123: bit 4 toggles DivMMC automap-enable (independent lever from
        // NR 0x83 bit 0). VHDL zxnext.vhd:1126,4112,5196.
        divmmc_.set_nr_0a_4_enable((v & 0x10) != 0);
        mouse_.set_button_reverse((v & 0x08) != 0);
        mouse_.set_dpi(v & 0x03);
        return v;
    });
    // VHDL zxnext.vhd:5912 — NR 0x0A read composes:
    //   nr_0a_mf_type [7:6] & nr_0a_sd_swap [5]
    //     & nr_0a_divmmc_automap_en [4] & nr_0a_mouse_button_reverse [3]
    //     & '0' [2] & nr_0a_mouse_dpi [1:0]
    // Bit 2 always reads 0. mf_type [7:6] is unmodelled (G132) — sourced
    // from cached last-write byte; gating divergence is a separate issue
    // tracked there, not G56. Other fields use authoritative subsystem
    // accessors. G56.
    nextreg_.set_read_handler(0x0A, [this]() -> uint8_t {
        uint8_t v = static_cast<uint8_t>(nextreg_.cached(0x0A) & 0xC0); // mf_type
        if (spi_.sd_swap())          v = static_cast<uint8_t>(v | 0x20);
        if (divmmc_.nr_0a_4_enable()) v = static_cast<uint8_t>(v | 0x10);
        if (mouse_.button_reverse()) v = static_cast<uint8_t>(v | 0x08);
        // bit 2 always 0
        v = static_cast<uint8_t>(v | (mouse_.dpi() & 0x03));
        return v;
    });

    // Register 0x05: Joystick mode decoder — VHDL zxnext.vhd:5157-5158.
    // Phase 1 scaffold: Joystick::set_nr_05() is a stub that records the
    // raw byte; Agent A (Phase 2) implements the real bit-extraction.
    nextreg_.set_write_handler(0x05, [this](uint8_t v) -> uint8_t {
        joystick_.set_nr_05(v);
        return v;
    });
    // VHDL zxnext.vhd:5897 — read formula:
    //   port_253b_dat <= nr_05_joy0(1 downto 0)        (bits 7:6)
    //                  & nr_05_joy1(1 downto 0)        (bits 5:4)
    //                  & nr_05_joy0(2)                 (bit  3)
    //                  & eff_nr_05_5060                (bit  2)
    //                  & nr_05_joy1(2)                 (bit  1)
    //                  & eff_nr_05_scandouble_en;      (bit  0)
    // G56 cluster A — joy bits come from Joystick (the authoritative
    // owner of the decoded `nr_05_joy0` / `nr_05_joy1` 3-bit fields,
    // which can also be moved by `set_mode_direct()` in tests). The
    // 5060 / scandouble bits live in the raw shadow store: there is no
    // dedicated subsystem owner today, the F2/F3 hotkey toggles route
    // through `nextreg_.write(0x05, ...)`, and `eff_nr_*` are simply
    // frame-sync-latched copies (irrelevant at unit-test scope, where
    // there is no video-frame edge between writes and reads).
    nextreg_.set_read_handler(0x05, [this]() -> uint8_t {
        const auto m0 = static_cast<uint8_t>(joystick_.mode_left());   // 3 bits
        const auto m1 = static_cast<uint8_t>(joystick_.mode_right());  // 3 bits
        const uint8_t cached = nextreg_.cached(0x05);
        uint8_t v = 0;
        v |= static_cast<uint8_t>(((m0 >> 1) & 1u) << 7);  // joy0[1] → bit 7
        v |= static_cast<uint8_t>(((m0 >> 0) & 1u) << 6);  // joy0[0] → bit 6
        v |= static_cast<uint8_t>(((m1 >> 1) & 1u) << 5);  // joy1[1] → bit 5
        v |= static_cast<uint8_t>(((m1 >> 0) & 1u) << 4);  // joy1[0] → bit 4
        v |= static_cast<uint8_t>(((m0 >> 2) & 1u) << 3);  // joy0[2] → bit 3
        v |= static_cast<uint8_t>(cached & 0x04);          // eff_5060 (bit 2)
        v |= static_cast<uint8_t>(((m1 >> 2) & 1u) << 1);  // joy1[2] → bit 1
        v |= static_cast<uint8_t>(cached & 0x01);          // eff_scandouble (bit 0)
        return v;
    });

    // Register 0x0B: Joystick I/O-mode pin-7 mux — VHDL zxnext.vhd:5200-5203.
    // VHDL zxnext.vhd:5915 — NR 0x0B read composes:
    //   nr_0b_joy_iomode_en [7] & '0' [6] & nr_0b_joy_iomode [5:4]
    //     & "000" [3:1] & nr_0b_joy_iomode_0 [0]
    // Bit 6 always 0; bits 3:1 always "000". G56 Phase 2 (option b): the
    // read mux is a pure static mask of the last-written byte (IoMode's
    // `nr_0b_raw_` is `set_nr_0b(v)` verbatim — no other writers, no
    // cross-subsystem composition). Returning `v & 0xB1` from the
    // write_handler stores the canonical readback in regs_[0x0B], so no
    // separate read_handler is needed.
    nextreg_.set_write_handler(0x0B, [this](uint8_t v) -> uint8_t {
        iomode_.set_nr_0b(v);
        return static_cast<uint8_t>(v & 0xB1);
    });

    // Register 0x10: core/board ID + flashboot. VHDL zxnext.vhd:5677-5687
    // (Issue 2/3 board path — jnext defaults to ZXN_ISSUE2):
    //   nr_10_flashboot <= nr_wr_dat(7);                  -- always
    //   if config_mode = '1' then
    //     nr_10_coreid <= nr_wr_dat(4 downto 0);          -- gated
    //   end if;
    // Read mux at zxnext.vhd:5924:
    //   '0' [7] & nr_10_coreid [6:2] & i_SPKEY_BUTTONS(1:0) [1:0]
    // jnext does not model SPKEY_BUTTONS (the M1+Drive front-panel buttons)
    // — they read as 0 (idle). nr_10_flashboot is not exposed on the read
    // path. coreid is tracked in nr_10_coreid_ (5 bits), config_mode-gated
    // on writes per VHDL Issue 2/3 path. Reset default 0x01 ("00001"),
    // VHDL zxnext.vhd:1133. G56.
    // G56 Phase 2 (option b): write_handler returns the canonical readback
    // byte (bit 7 = 0, bits 6:2 = coreid, bits 1:0 = SPKEY_BUTTONS = 0
    // unmodelled). config_mode-gated coreid update applied first; the
    // returned canonical byte stored in regs_[0x10] always matches what a
    // synthesised read_handler would have returned. nr_10_flashboot
    // (write bit 7) is not exposed on the read path. No separate
    // read_handler is needed.
    nextreg_.set_write_handler(0x10, [this](uint8_t v) -> uint8_t {
        if (nextreg_.nr_03_config_mode()) {
            nr_10_coreid_ = static_cast<uint8_t>(v & 0x1F);
        }
        // Canonical NR 0x10 readback — VHDL zxnext.vhd:5924.
        return static_cast<uint8_t>((nr_10_coreid_ & 0x1F) << 2);
    });

    // Registers 0x28 / 0x29 / 0x2B — PS/2 keymap + joystick keymap (UDK)
    // programming. VHDL zxnext.vhd:6294-6324 + membrane_stick.vhd:172-183
    // (SDP-RAM keyjoy_64_6.coe-loaded defaults). G127 closure (Tier 2 W1
    // Agent C). NR 0x2A is reserved/dead in VHDL (lines 6312-6319 are
    // commented out) and intentionally has no handler here.
    //
    // Routing: NR 0x28 latches keymap_sel + addr bit 8; NR 0x29 sets the
    // low 8 bits of the 9-bit nr_keymap_addr; NR 0x2B writes the data
    // byte (gated by keymap_sel: sel=1 → joystick UDK SDP-RAM; sel=0 →
    // PS/2 keymap, which jnext does not implement, so the data is just
    // dropped). The auto-increment of nr_keymap_addr fires regardless.
    nextreg_.set_write_handler(0x28, [this](uint8_t v) -> uint8_t {
        membrane_stick_.write_nr_28(v);
        return v;
    });
    nextreg_.set_write_handler(0x29, [this](uint8_t v) -> uint8_t {
        membrane_stick_.write_nr_29(v);
        // G149: VHDL zxnext.vhd:5878-6289 read-mux has no entry for NR 0x29
        // (write-only keymap address LSB; auto-incremented internally and
        // not exposed). Fall-through is (others => '0'); return 0 so the
        // canonical regs_[0x29] stores 0 and reads return 0.
        return 0;
    });
    nextreg_.set_write_handler(0x2B, [this](uint8_t v) -> uint8_t {
        membrane_stick_.write_nr_2b(v);
        return v;
    });

    // Registers 0xB0 / 0xB1 / 0xB2 — extended keyboard matrix + MD6 extras.
    // VHDL zxnext.vhd:6206-6215 read compositions. Phase 1 scaffold: the
    // underlying byte getters return 0xFF / 0 until Agents G (extended
    // keys) and D (MD6) fill them in.
    nextreg_.set_read_handler(0xB0, [this]() -> uint8_t { return keyboard_.nr_b0_byte(); });
    nextreg_.set_read_handler(0xB1, [this]() -> uint8_t { return keyboard_.nr_b1_byte(); });
    nextreg_.set_read_handler(0xB2, [this]() -> uint8_t { return md6_.nr_b2_byte(); });

    // Register 0x15: Sprite and layer system setup
    //   bit 7 = LoRes enable (deferred)
    //   bit 6 = sprite priority (0=sprite 0 on top when zero_on_top)
    //   bit 5 = sprite border clip enable
    //   bits 4:2 = layer priority (SLU/LSU/SUL/LUS/USL/ULS)
    //   bit 1 = sprites over border
    //   bit 0 = sprites visible
    nextreg_.set_write_handler(0x15, [this](uint8_t v) -> uint8_t {
        sprites_.set_zero_on_top((v & 0x40) != 0);
        sprites_.set_border_clip_en((v & 0x20) != 0);  // bit 5 — VHDL sprites.vhd 1044
        sprites_.set_over_border((v & 0x02) != 0);
        sprites_.set_sprites_visible((v & 0x01) != 0);
        renderer_.set_sprite_en((v & 0x01) != 0);      // VHDL 6934/7118
        renderer_.set_layer_priority((v >> 2) & 0x07);
        return v;
    });
    // VHDL zxnext.vhd:5939 — NR 0x15 read composes:
    //   nr_15_lores_en [7] & nr_15_sprite_priority [6]
    //     & nr_15_sprite_border_clip_en [5]
    //     & nr_15_layer_priority [4:2]
    //     & nr_15_sprite_over_border_en [1]
    //     & nr_15_sprite_en [0]
    // bit 7 (lores_en): not modeled in jnext yet — sourced from cached
    // last-write byte (deferred LoRes implementation, separate concern
    // from G56). All other bits use authoritative subsystem accessors.
    // G56.
    nextreg_.set_read_handler(0x15, [this]() -> uint8_t {
        uint8_t v = static_cast<uint8_t>(nextreg_.cached(0x15) & 0x80); // lores_en
        if (sprites_.zero_on_top())     v = static_cast<uint8_t>(v | 0x40);
        if (sprites_.border_clip_en())  v = static_cast<uint8_t>(v | 0x20);
        v = static_cast<uint8_t>(v | ((renderer_.layer_priority() & 0x07) << 2));
        if (sprites_.over_border())     v = static_cast<uint8_t>(v | 0x02);
        if (renderer_.sprite_en())      v = static_cast<uint8_t>(v | 0x01);
        return v;
    });

    // Registers 0x18-0x1B: Clip windows (4-write rotating: X1, X2, Y1, Y2)
    // Register 0x18: Layer 2 clip window
    nextreg_.set_write_handler(0x18, [this](uint8_t v) -> uint8_t {
        switch (clip_l2_idx_) {
            case 0: layer2_.set_clip_x1(v); break;
            case 1: layer2_.set_clip_x2(v); break;
            case 2: layer2_.set_clip_y1(v); break;
            case 3: layer2_.set_clip_y2(v); break;
        }
        clip_l2_idx_ = (clip_l2_idx_ + 1) & 0x03;
        return v;
    });
    // NR 0x18 read: pure combinatorial 4-way mux over Layer 2 clip coords
    // selected by clip_l2_idx_ (zxnext.vhd:5947-5953). Reading does NOT
    // advance the idx — only writes advance it (zxnext.vhd:5249).
    nextreg_.set_read_handler(0x18, [this]() -> uint8_t {
        switch (clip_l2_idx_) {
            case 0:  return layer2_.clip_x1();
            case 1:  return layer2_.clip_x2();
            case 2:  return layer2_.clip_y1();
            default: return layer2_.clip_y2();
        }
    });

    // Register 0x19: Sprite clip window
    nextreg_.set_write_handler(0x19, [this](uint8_t v) -> uint8_t {
        switch (clip_spr_idx_) {
            case 0: sprites_.set_clip_x1(v); break;
            case 1: sprites_.set_clip_x2(v); break;
            case 2: sprites_.set_clip_y1(v); break;
            case 3: sprites_.set_clip_y2(v); break;
        }
        clip_spr_idx_ = (clip_spr_idx_ + 1) & 0x03;
        return v;
    });
    // NR 0x19 read: pure combinatorial 4-way mux over sprite clip coords
    // selected by clip_spr_idx_ (zxnext.vhd:5955-5961). Reading does NOT
    // advance the idx — only writes advance it (zxnext.vhd:5256). G97.
    nextreg_.set_read_handler(0x19, [this]() -> uint8_t {
        switch (clip_spr_idx_) {
            case 0:  return sprites_.clip_x1();
            case 1:  return sprites_.clip_x2();
            case 2:  return sprites_.clip_y1();
            default: return sprites_.clip_y2();
        }
    });

    // Register 0x1A: ULA/LoRes clip window
    nextreg_.set_write_handler(0x1A, [this](uint8_t v) -> uint8_t {
        switch (clip_ula_idx_) {
            case 0: renderer_.ula().set_clip_x1(v); break;
            case 1: renderer_.ula().set_clip_x2(v); break;
            case 2: renderer_.ula().set_clip_y1(v); break;
            case 3: renderer_.ula().set_clip_y2(v); break;
        }
        clip_ula_idx_ = (clip_ula_idx_ + 1) & 0x03;
        return v;
    });
    // NR 0x1A read: pure combinatorial 4-way mux over ULA clip coords
    // selected by clip_ula_idx_ (zxnext.vhd:5963-5969). Reading does NOT
    // advance the idx — only writes advance it (zxnext.vhd:5265). G97.
    // y2 returns the raw stored register (`nr_1a_ula_clip_y2`); the
    // consumer-side clamp at 0xC0..0xFF→0xBF is applied at
    // `ula_clip_y2_0` (zxnext.vhd:6779-6783), NOT at the NR 0x1A read,
    // so we use Ula::clip_y2_raw() to mirror the register exactly.
    nextreg_.set_read_handler(0x1A, [this]() -> uint8_t {
        switch (clip_ula_idx_) {
            case 0:  return renderer_.ula().clip_x1();
            case 1:  return renderer_.ula().clip_x2();
            case 2:  return renderer_.ula().clip_y1();
            default: return renderer_.ula().clip_y2_raw();
        }
    });

    // Register 0x1B: Tilemap clip window
    nextreg_.set_write_handler(0x1B, [this](uint8_t v) -> uint8_t {
        switch (clip_tm_idx_) {
            case 0: tilemap_.set_clip_x1(v); break;
            case 1: tilemap_.set_clip_x2(v); break;
            case 2: tilemap_.set_clip_y1(v); break;
            case 3: tilemap_.set_clip_y2(v); break;
        }
        clip_tm_idx_ = (clip_tm_idx_ + 1) & 0x03;
        return v;
    });
    // NR 0x1B read: pure combinatorial 4-way mux over tilemap clip coords
    // selected by clip_tm_idx_ (zxnext.vhd:5971-5977). Reading does NOT
    // advance the idx — only writes advance it (zxnext.vhd:5276).
    nextreg_.set_read_handler(0x1B, [this]() -> uint8_t {
        switch (clip_tm_idx_) {
            case 0:  return tilemap_.clip_x1();
            case 1:  return tilemap_.clip_x2();
            case 2:  return tilemap_.clip_y1();
            default: return tilemap_.clip_y2();
        }
    });

    // Register 0x1C: Clip window control
    //   Read: bits 7:6=tilemap idx, 5:4=ULA idx, 3:2=sprite idx, 1:0=L2 idx
    //   Write: bit 3=reset tilemap idx, bit 2=reset ULA idx,
    //          bit 1=reset sprite idx, bit 0=reset L2 idx
    nextreg_.set_read_handler(0x1C, [this]() -> uint8_t {
        return static_cast<uint8_t>(
            (clip_tm_idx_ << 6) | (clip_ula_idx_ << 4) |
            (clip_spr_idx_ << 2) | clip_l2_idx_);
    });
    nextreg_.set_write_handler(0x1C, [this](uint8_t v) -> uint8_t {
        if (v & 0x01) clip_l2_idx_  = 0;
        if (v & 0x02) clip_spr_idx_ = 0;
        if (v & 0x04) clip_ula_idx_ = 0;
        if (v & 0x08) clip_tm_idx_  = 0;
        return v;
    });

    // Register 0x34: Sprite attribute slot select (alternative to port 0x303B)
    //
    // VHDL zxnext.vhd:4855 — NR 0x34 sets nr_sprite_mirror_we with the
    // default mirror_index = "111" (sprites.vhd:600-602 sprite-number
    // path). Bit 6 of NR address = 0, so nr_sprite_mirror_inc = 0 (no
    // increment after this write).
    //
    // G95: when sprite_tie (NR 0x09 b4) is set, sprites.vhd:653-654
    // syncs attr_index to mirror_sprite_q on the next clock —
    // set_mirror_sprite_num models that synchronously by also updating
    // attr_slot_/attr_byte_/pattern_slot_msb_.
    nextreg_.set_write_handler(0x34,
        [this](uint8_t v) -> uint8_t { sprites_.set_mirror_sprite_num(v); return v; });
    // VHDL zxnext.vhd:6033 — NR 0x34 read: '0' [7] & sprite_mirror_id [6:0].
    // sprite_mirror_id is the 7-bit mirror_sprite_q index (sprites.vhd:594-612).
    // jnext stores this in mirror_sprite_num_ (8 bits — the high bit is the
    // pattern_index MSB which the VHDL formula intentionally drops). G56.
    nextreg_.set_read_handler(0x34, [this]() -> uint8_t {
        return static_cast<uint8_t>(sprites_.mirror_sprite_num() & 0x7F);
    });

    // Registers 0x75-0x79: Direct sprite attribute byte writes WITH
    // per-byte auto-increment (G96).
    //
    // VHDL zxnext.vhd:4857-4875 — same mirror_we/mirror_index decode as
    // NR 0x35-0x39 but bit 6 of nr_wr_reg = 1, so nr_sprite_mirror_inc
    // fires per write (zxnext.vhd:4916). sprites.vhd:603-605 increments
    // mirror_sprite_q's lower 7 bits and reloads bit 7 from
    // pattern_index(7).
    for (int i = 0; i < 5; ++i) {
        nextreg_.set_write_handler(static_cast<uint8_t>(0x75 + i),
            [this, i](uint8_t v) -> uint8_t {
                sprites_.write_attr_byte_nr_per_byte_inc(
                    static_cast<uint8_t>(i), v);
                    return v;
            });
    }

    // Register 0x2F: Tilemap X scroll MSB (bits 1:0)
    nextreg_.set_write_handler(0x2F, [this](uint8_t v) -> uint8_t { tilemap_.set_scroll_x_msb(v); return v; });

    // Register 0x30: Tilemap X scroll LSB
    nextreg_.set_write_handler(0x30, [this](uint8_t v) -> uint8_t { tilemap_.set_scroll_x_lsb(v); return v; });

    // Register 0x31: Tilemap Y scroll
    nextreg_.set_write_handler(0x31, [this](uint8_t v) -> uint8_t { tilemap_.set_scroll_y(v); return v; });

    // --- Phase-1 scaffold: ULA scroll / ULAnext format ---------------------
    // Register 0x26 — ULA X-scroll coarse (VHDL zxnext.vhd:5304, zxula.vhd:199).
    nextreg_.set_write_handler(0x26, [this](uint8_t v) -> uint8_t {
        renderer_.ula().set_ula_scroll_x_coarse(v);
        return v;
    });
    nextreg_.set_read_handler(0x26, [this]() -> uint8_t {
        return renderer_.ula().get_ula_scroll_x_coarse();
    });

    // Register 0x27 — ULA Y-scroll (VHDL zxnext.vhd:5307, zxula.vhd:193-207).
    nextreg_.set_write_handler(0x27, [this](uint8_t v) -> uint8_t {
        renderer_.ula().set_ula_scroll_y(v);
        return v;
    });
    nextreg_.set_read_handler(0x27, [this]() -> uint8_t {
        return renderer_.ula().get_ula_scroll_y();
    });

    // Register 0x42 — ULAnext format byte (VHDL zxnext.vhd:5386, reset X"07").
    nextreg_.set_write_handler(0x42, [this](uint8_t v) -> uint8_t {
        renderer_.ula().set_ulanext_format(v);
        return v;
    });
    nextreg_.set_read_handler(0x42, [this]() -> uint8_t {
        return renderer_.ula().get_ulanext_format();
    });

    // Register 0x6A: LoRes / Radastan control
    // VHDL zxnext.vhd:6098-6099 — port_253b_dat <=
    //   "00" & nr_6a_lores_radastan & nr_6a_lores_radastan_xor &
    //   nr_6a_lores_palette_offset;
    // Bits 7:6 are hard-wired '0', bit 5 = radastan, bit 4 = radastan_xor,
    // bits 3:0 = palette offset. No dedicated LoRes subsystem exists yet;
    // G56 cluster D, option (b): canonicalise the byte at write-time so
    // regs_[0x6A] holds the masked value and NextReg::read returns the
    // VHDL-spec form without a dedicated read_handler.
    nextreg_.set_write_handler(0x6A, [](uint8_t v) -> uint8_t {
        return static_cast<uint8_t>(v & 0x3F);
    });

    // Register 0x6B: Tilemap control
    nextreg_.set_write_handler(0x6B, [this](uint8_t v) -> uint8_t {
        tilemap_.set_control(v);
        renderer_.set_tm_enabled((v & 0x80) != 0);  // VHDL 7130: stencil gate
        // VHDL nr_6b_tm_palette_select (bit 4) — drives the tilemap palette
        // lookup at render time.  Must come from NR 0x6B, NOT from NR 0x43.
        palette_.set_active_tilemap_palette((v & 0x10) != 0);
        // G10: mirror NR 0x6B b4 into Ula's per-scanline selector change-log
        // (independent latch from NR 0x43 — VHDL zxnext.vhd:5462, :6826).
        renderer_.ula().set_active_tilemap_palette((v & 0x10) != 0);
        return v;
    });
    // VHDL zxnext.vhd:6101-6102 — port_253b_dat <=
    //   nr_6b_tm_en & nr_6b_tm_control;  (8 bits total: bit 7 = tm_en,
    //   bits 6:0 = tm_control). Tilemap::set_control stores the raw byte
    //   into control_raw_; bit 7 is also decoded into enabled_. Pull from
    //   Tilemap to keep NR 0x6B read aligned with the live subsystem state
    //   (G56-CR-6B).
    nextreg_.set_read_handler(0x6B, [this]() -> uint8_t {
        return tilemap_.get_control();
    });

    // Register 0x6C: Tilemap default attribute
    // VHDL zxnext.vhd:6104-6105 — port_253b_dat <= nr_6c_tm_default_attr;
    // Full 8 bits, single value. G56 cluster D, option (b): the NR-write
    // handler is the only mutator of Tilemap::default_attr_ (outside reset
    // and save/load), so storing the raw byte in regs_[0x6C] keeps the read
    // source-of-truth aligned with the live attribute and the read_handler
    // is no longer needed.
    nextreg_.set_write_handler(0x6C, [this](uint8_t v) -> uint8_t { tilemap_.set_default_attr(v); return v; });

    // Register 0x6E: Tilemap base address
    nextreg_.set_write_handler(0x6E, [this](uint8_t v) -> uint8_t { tilemap_.set_map_base(v); return v; });
    // VHDL zxnext.vhd:6108 — read mux forces bit 6 to '0'
    // (`nr_6e_tilemap_base_7 & '0' & nr_6e_tilemap_base`).  G56 + G99: the
    // bit-6 reserved-zero mask is applied here (Tilemap::get_map_base_read
    // returns map_base_raw_ & 0xBF), so a raw write of 0xFF reads back 0xBF
    // rather than the unmasked 0xFF that regs_[0x6E] would otherwise return.
    // KEEP (G56 Phase 2 cluster E): VHDL reset default 0x2C lives in Tilemap
    // (zxnext.vhd:5041-5042) — regs_[0x6E] cold-inits to 0, so the cached-
    // fallback would regress the boot-time read.
    nextreg_.set_read_handler(0x6E, [this]() -> uint8_t { return tilemap_.get_map_base_read(); });

    // Register 0x6F: Tile definitions base address
    nextreg_.set_write_handler(0x6F, [this](uint8_t v) -> uint8_t { tilemap_.set_def_base(v); return v; });
    // VHDL zxnext.vhd:6111 — same pattern as NR 0x6E.  G56 + G99: bit 6 is
    // the reserved-zero in the read composition; def_base_raw_ & 0xBF.
    // KEEP (G56 Phase 2 cluster E): VHDL reset default 0x0C lives in Tilemap
    // (zxnext.vhd:5044-5045) — same cold-init regression risk as NR 0x6E.
    nextreg_.set_read_handler(0x6F, [this]() -> uint8_t { return tilemap_.get_def_base_read(); });

    // Registers 0x60-0x63: Copper co-processor
    // G149: VHDL zxnext.vhd:5878-6289 read-mux has no entry for NR 0x60
    // (Copper data is consumed by the Copper subsystem on write; not
    // exposed on read). Fall-through is (others => '0'); return 0 so reads
    // of NR 0x60 return 0 (NRs 0x61/0x62 keep their composed read handlers).
    nextreg_.set_write_handler(0x60, [this](uint8_t v) -> uint8_t { copper_.write_reg_0x60(v); return 0; });
    nextreg_.set_write_handler(0x61, [this](uint8_t v) -> uint8_t { copper_.write_reg_0x61(v); return v; });
    nextreg_.set_write_handler(0x62, [this](uint8_t v) -> uint8_t { copper_.write_reg_0x62(v); return v; });
    nextreg_.set_write_handler(0x63, [this](uint8_t v) -> uint8_t { copper_.write_reg_0x63(v); return v; });

    // NR 0x61 / 0x62 read-back. VHDL zxnext.vhd:6083-6087 returns
    //   0x61 -> nr_copper_addr(7..0)
    //   0x62 -> nr_62_copper_mode & "000" & nr_copper_addr(10..8)
    // Without these, NextReg::read() falls through to regs_[] (last-write
    // semantics) — wrong for the auto-incrementing copper write pointer.
    // (G116 closure.)
    nextreg_.set_read_handler(0x61, [this]() -> uint8_t { return copper_.read_reg_0x61(); });
    nextreg_.set_read_handler(0x62, [this]() -> uint8_t { return copper_.read_reg_0x62(); });

    // Register 0x64: Copper vertical line offset (NR 0x64).
    // VHDL: zxnext.vhd:5442 (write), :6090 (read-back), :6723 (wired
    //       into zxula_timing.vhd i_cu_offset).
    // G109: i_cu_offset feeds cvc reload at zxula_timing.vhd:462; the
    // line-int comparator at :577 uses cvc, so VideoTiming must mirror
    // the offset for line-int scheduling to match VHDL semantics.
    nextreg_.set_write_handler(0x64, [this](uint8_t v) -> uint8_t {
        copper_.write_reg_0x64(v);
        video_timing_.set_cu_offset(v);
        return v;
    });
    nextreg_.set_read_handler (0x64, [this]() -> uint8_t { return copper_.read_reg_0x64(); });

    // Program the Copper c_max_vc wrap to match the active machine timing.
    // Per zxula_timing.vhd the wrap is (lines_per_frame - 1). This is a
    // config-time setter; Copper::reset() intentionally does not clear it.
    copper_.set_c_max_vc(timing_.lines_per_frame - 1);

    // Registers 0x50–0x57: MMU slot→page mapping (one register per slot)
    // Page 0xFF = map ROM into the slot (VHDL: mmu_A21_wr_en = '0' when page = xFF).
    for (int i = 0; i < 8; ++i) {
        nextreg_.set_write_handler(static_cast<uint8_t>(0x50 + i),
            [this, i](uint8_t v) -> uint8_t {
                if (v == 0xFF)
                    mmu_.map_rom(i, static_cast<uint8_t>(i < 2 ? i : 0));
                else
                    mmu_.set_page(i, v);
                return v;
            });
        // G46(b) — VHDL zxnext.vhd:6075-6082 — `port_253b_dat <= MMU<i>` for
        // reads of NR 0x50..0x57. MMU<i> is the LIVE register updated by:
        //   * NR 0x50..0x57 writes (line 4690-4699: nr_mmu_we → MMU<i> <= nr_wr_dat)
        //   * NR 0x8E with bit 3=1 (port_memory_ram_change_dly path,
        //     line 4683-4684: MMU6/7 <= port_7ffd_bank & '0'/'1')
        //   * port 7FFD writes (same path via port_memory_ram_change_dly)
        //   * reset paths (lines 4611-4663)
        // jnext keeps the canonical MMU<i> mirror in `Mmu::nr_mmu_[i]`,
        // updated by ALL of the above via `Mmu::set_page` / the legacy
        // paging recompute. Without this read handler the NR-port read
        // would return `NextReg::regs_[0x50+i]` — stale whenever the live
        // MMU value diverged from the last explicit NR 0x50..0x57 write.
        // Concrete G46(b) symptom: NEXTREG $8E,$08 at firmware $01D7 set
        // `nr_mmu_[7]` to 0x01 (port_7ffd_bank=0 ⇒ bank<<1 | 1) but left
        // `regs_[0x57] = 0xDF` (the last RAM-test pass-2 NR_57 write).
        // The post-RAM-test wrapper at $2730-$274a then read NR_57 via
        // IN A,($253B), saved 0xDF at $5B8A's H byte, and at exit ($279a)
        // restored NR_57 = 0xDF — mapping slot 7 to the wrong physical
        // bank, causing the user-stack RET at $27ab to pop zeros from
        // unused RAM and JP $0000 → $00EF → boot loop.
        nextreg_.set_read_handler(static_cast<uint8_t>(0x50 + i),
            [this, i]() -> uint8_t {
                return mmu_.get_page(i);
            });
    }

    // --- NextREG read handlers (dynamic registers) ---

    // Registers 0x1E/0x1F: Active video line (read-only, computed from cycle count).
    // Returns the current raster line (vc) relative to display start.
    nextreg_.set_read_handler(0x1E, [this]() -> uint8_t {
        uint64_t elapsed = clock_.get() - frame_cycle_;
        int vc = static_cast<int>(elapsed / timing_.master_cycles_per_line);
        return static_cast<uint8_t>((vc >> 8) & 0x01);
    });
    nextreg_.set_read_handler(0x1F, [this]() -> uint8_t {
        uint64_t elapsed = clock_.get() - frame_cycle_;
        int vc = static_cast<int>(elapsed / timing_.master_cycles_per_line);
        return static_cast<uint8_t>(vc & 0xFF);
    });

    // Register 0x22: Line interrupt control
    //   bit 2 = disable ULA interrupt
    //   bit 1 = enable line interrupt
    //   bit 0 = MSB of line interrupt value
    //
    // G71/G106/G107: VideoTiming owns line-int + ULA-int gating state.
    // run_frame() consumes video_timing_.{line_interrupt_enable,
    // int_line_num, frame_int_master_cycle_offset, ...} for scheduling.
    nextreg_.set_write_handler(0x22, [this](uint8_t v) -> uint8_t {
        ula_int_disabled_ = (v & 0x04) != 0;
        video_timing_.set_interrupt_enable(!ula_int_disabled_);
        video_timing_.set_line_interrupt_enable((v & 0x02) != 0);
        const uint16_t cur = video_timing_.line_interrupt_target();
        video_timing_.set_line_interrupt_target(
            static_cast<uint16_t>((cur & 0xFF) | ((v & 0x01) << 8)));
        // VHDL zxnext.vhd:3619-3620 — `nr_22_we` fans bit 2 of the new
        // value into `port_ff_reg(6)` (the ULA-int-disable bit).
        port_ff_reg_ = static_cast<uint8_t>((port_ff_reg_ & 0xBF)
                                          | ((v & 0x04) << 4));
        // G108 — propagate the updated port_ff_reg into Ula::screen_mode_reg_
        // so the renderer sees the change. Bits 2:0 (mode) + 5:3 (paper
        // colour) are preserved from the previous port_ff_reg state; only
        // bit 6 is touched by NR 0x22. Ula does not consume bit 6 directly,
        // so the rendered output is byte-identical when bits 5:0 are stable
        // — but routing through set_screen_mode keeps the per-scanline
        // change-log (G07) symmetric with port-FF and sister NR-side writes.
        renderer_.ula().set_screen_mode(port_ff_reg_);
        // G163 — re-evaluate the line-int schedule. NR 0x22 carries the
        // line-int enable bit (bit 1) and the target MSB (bit 0); both
        // affect the firing line.
        reschedule_line_interrupt();
        return v;
    });

    // VHDL zxnext.vhd:5992 — NR 0x22 read composes:
    //   bit 7   = NOT pulse_int_n        (Im2Controller dynamic pulse)
    //   bits 6:3 = "0000"                (constants)
    //   bit 2   = port_ff_interrupt_disable = port_ff_reg(6) (zxnext.vhd:3635)
    //   bit 1   = nr_22_line_interrupt_en   (VideoTiming::line_interrupt_enable)
    //   bit 0   = nr_23_line_interrupt(8)   (target MSB)
    //
    // Pre-G56-cluster-C: bare regs_[0x22] echoed the last write — fail
    // mode (1) middle nibble bits 6:3 leak the input rather than reading
    // 0; (2) port-0xFF writes mutate port_ff_reg(6) without updating the
    // regs_[] cache, so bit 2 read-back diverges from VHDL contract.
    nextreg_.set_read_handler(0x22, [this]() -> uint8_t {
        const bool pulse_n = im2_.pulse_int_n();
        const uint8_t bit7 = pulse_n ? 0x00 : 0x80;
        const uint8_t bit2 = (port_ff_reg_ >> 4) & 0x04;       // port_ff_reg(6) → bit 2
        const uint8_t bit1 = video_timing_.line_interrupt_enable() ? 0x02 : 0x00;
        const uint8_t bit0 = static_cast<uint8_t>(
            (video_timing_.line_interrupt_target() >> 8) & 0x01);
        return static_cast<uint8_t>(bit7 | bit2 | bit1 | bit0);
    });

    // Register 0x23: Line interrupt value LSB
    nextreg_.set_write_handler(0x23, [this](uint8_t v) -> uint8_t {
        const uint16_t cur = video_timing_.line_interrupt_target();
        video_timing_.set_line_interrupt_target(
            static_cast<uint16_t>((cur & 0x100) | v));
        // G163 — chained line-IRQ rewrites NR 0x23 mid-frame to retarget
        // the next firing line (parallax.nex pattern). VHDL fires every
        // cycle when match (zxula_timing.vhd:577), so we must reschedule.
        reschedule_line_interrupt();
        return v;
    });
    // VHDL zxnext.vhd:5995 — NR 0x23 read returns nr_23_line_interrupt(7:0).
    // G56 cluster C: read_handler dropped. The write_handler above stores
    // `v` into both `regs_[0x23]` (via the new write contract) AND into
    // VideoTiming::line_interrupt_target() LSB; the only path that mutates
    // the LSB is this same write_handler (NR 0x22 only touches the MSB,
    // and load_state reloads both regs_[0x23] and line_interrupt_target
    // from independent slots that were equal at save time). So bare
    // regs_[0x23] echo is byte-identical to the previous handler's
    // VideoTiming-sourced read, with no divergence path.

    // Register 0x02: Reset control + software NMI strobes.
    //   bit 0 = RESET_SOFT (tbblue hardware.h) → preserve SRAM, drop FFs
    //   bit 1 = RESET_HARD → full hard reset (same as power-on)
    //   bit 2 = DivMMC software NMI strobe (VHDL zxnext.vhd:3830-3838,
    //           `nmi_cpu_02_we and cpu_requester_dat(2)` → `nmi_sw_gen_divmmc`)
    //   bit 3 = Multiface software NMI strobe (VHDL zxnext.vhd:3830-3838,
    //           `nmi_cpu_02_we and cpu_requester_dat(3)` → `nmi_sw_gen_mf`)
    //   bit 7 = RESET_ESPBUS → peripheral-bus ESP reset signal, no-op here
    // Hard takes priority over soft if both bits are set (unusual but
    // VHDL-faithful: a hard reset supersedes a soft reset).
    //
    // Bits 2/3 are forwarded to NmiSource on every write; the decode
    // inside NmiSource::nr_02_write is the single source of truth
    // (matches `nmi_gen_nr_*` at VHDL:3832-3833). Copper MOVE-to-NR 0x02
    // also routes through NextReg::write and therefore reaches here, so
    // no separate Copper hook is required (plan §"Copper NR-write path").
    nextreg_.set_write_handler(0x02, [this](uint8_t v) -> uint8_t {
        // Always surface the software-NMI bits to NmiSource — its
        // decode is the VHDL-faithful one. The bits are one-shot
        // strobes so doing this unconditionally on every write is
        // correct (writes with bits 2/3 = 0 are no-ops inside
        // NmiSource::nr_02_write).
        nmi_source_.nr_02_write(v);

        // VHDL zxnext.vhd:3879-3880 — NR 0xDA `nr_da_iotrap_cause`
        // clears when NR 0x02 is written with bit 4 = 0 (the
        // `nr_02_iotrap` ack/clear path). VHDL:3885 also gates the
        // NR 0x02 readback bit 4 from this same field. The clear is
        // unconditional on bit 4 polarity — write bit 4 = 0 to
        // acknowledge / clear; bit 4 = 1 leaves the cause latched.
        // (No trap-event vs nr_02_we race in this single-write path —
        // trap events arrive via separate port-0x?FFD accesses on
        // different cycles.)
        if ((v & 0x10) == 0) {
            nr_da_iotrap_cause_ = 0;
        }

        // VHDL zxnext.vhd:6370 — `nr_02_soft_reset <= ... or
        // (nr_02_we and nr_wr_dat(0))`. The reset_type[2:0] FSM at
        // VHDL:1732-1739 advances on this edge regardless of whether
        // the higher-priority hard-reset bit also fires this cycle.
        // Strobe BEFORE invoking the host-side reset() / soft_reset(),
        // both of which call init() and re-route through NmiSource —
        // strobing first guarantees the FSM-advance is observable in
        // the post-reset readback.
        if (v & 0x01) {
            nmi_source_.strobe_soft_reset();
        }

        if (v & 0x02) {
            const auto regs = cpu_.get_registers();
            Log::emulator()->info("Hard reset triggered via NextREG 0x02 ({:#04x}) PC={:#06x} rom_bank={:#04x} mmu7={:#04x}",
                                  v, regs.PC, mmu_.current_rom_bank(), mmu_.get_page(7));
            reset();
        } else if (v & 0x01) {
            const auto regs = cpu_.get_registers();
            Log::emulator()->info("Soft reset triggered via NextREG 0x02 ({:#04x}) PC={:#06x} rom_bank={:#04x} mmu7={:#04x}",
                                  v, regs.PC, mmu_.current_rom_bank(), mmu_.get_page(7));
            soft_reset();
        }
        // bit 7 alone (RESET_ESPBUS) is intentionally ignored — no ESP.
        return v;
    });

    // NR 0x02 readback — VHDL zxnext.vhd:5891 layout:
    //   bit 7   = nr_02_bus_reset
    //   bits 6:5 = reserved ("00")
    //   bit 4   = nr_02_iotrap           (IO trap status, read-only)
    //   bit 3   = nr_02_generate_mf_nmi  (auto-clears on FSM S_NMI_END)
    //   bit 2   = nr_02_generate_divmmc_nmi (auto-clears on FSM S_NMI_END)
    //   bits 1:0 = nr_02_reset_type(1 downto 0)
    //
    // Only bits 3/2 are owned by NmiSource (the FSM auto-clears them at
    // S_NMI_END per VHDL:5891 + VHDL:2149-2162). The other fields
    // (bus_reset, iotrap, reset_type) are their own VHDL signals and
    // jnext does not yet track them as separate state; per the NMI plan
    // scope we surface them as zero rather than inventing state that
    // wouldn't match the VHDL shift-register / latch semantics.
    nextreg_.set_read_handler(0x02, [this]() -> uint8_t {
        return nmi_source_.nr_02_read();
    });

    // NR 0xD8 — IO trap enable (VHDL zxnext.vhd:1263, 5640, 6266).
    //   bit 0 = nr_d8_io_trap_fdc_en. Gates the +3 floppy port-trap
    //   decode (port 0x2FFD read, 0x3FFD read/write) which OR's into
    //   `nmi_gen_iotrap` (VHDL:3835) and into the `nmi_sw_gen_mf` line
    //   (VHDL:3837), so when enabled a trap-port access fires the
    //   Multiface NMI path.
    // bits 7:1 are unused per VHDL:6266 readback ('0' & nr_d8_io_trap_fdc_en).
    nextreg_.set_write_handler(0xD8, [this](uint8_t v) -> uint8_t {
        nr_d8_io_trap_fdc_en_ = (v & 0x01) != 0;
        return v;
    });
    nextreg_.set_read_handler(0xD8, [this]() -> uint8_t {
        return nr_d8_io_trap_fdc_en_ ? 0x01 : 0x00;
    });

    // NR 0xD9 — IO-trap captured CPU write byte.
    //   VHDL zxnext.vhd:1264, 3887-3898 — process clocks
    //     nr_d9_iotrap_write <= cpu_do  when port_3ffd_wr AND nmi_accept_cause
    //     nr_d9_iotrap_write <= nr_wr_dat  when nr_d9_we ('1' on NR 0xD9 write)
    //   Read mux at VHDL:6269 returns the full 8-bit field.
    //   Reset to 0 at VHDL:3891.
    // Trap-side capture is wired in the port 0x3FFD WRITE handler
    // (see nmi_source_.strobe_iotrap() block below); this handler covers
    // the firmware-side direct-write path.
    nextreg_.set_write_handler(0xD9, [this](uint8_t v) -> uint8_t {
        nr_d9_iotrap_write_ = v;
        return v;  // last-write-wins; full 8 bits retained.
    });
    nextreg_.set_read_handler(0xD9, [this]() -> uint8_t {
        return nr_d9_iotrap_write_;
    });

    // NR 0xDA — IO-trap cause (VHDL zxnext.vhd:1265, 3866-3883, 6271-6272).
    //   Read mux at VHDL:6272: "000000" & nr_da_iotrap_cause(1:0).
    //   Reset to "00" (VHDL:3870).
    //   Set on trap event by the NMI-accept clause (VHDL:3871-3878):
    //     port_2ffd_rd  → "01"
    //     port_3ffd_rd  → "10"
    //     port_3ffd_wr  → "11"
    //   Cleared by NR 0x02 write with bit 4 = 0 (VHDL:3879-3880);
    //   that clause lives in the NR 0x02 write handler above.
    //   NR 0xDA itself has NO write path in VHDL — the
    //   `when X"DA" => nr_da_we <= '1';` arm at VHDL:5645-5646 is
    //   commented out — so direct writes are silently ignored.
    nextreg_.set_write_handler(0xDA, [this](uint8_t /*v*/) -> uint8_t {
        // VHDL: NR 0xDA write is unmapped (commented out in zxnext.vhd:5645).
        // The cause field is unaffected; the canonical regs_[0xDA] mirrors
        // the live cause so that cached reads shadow the live read mux.
        return static_cast<uint8_t>(nr_da_iotrap_cause_ & 0x03);
    });
    nextreg_.set_read_handler(0xDA, [this]() -> uint8_t {
        return static_cast<uint8_t>(nr_da_iotrap_cause_ & 0x03);
    });

    // Register 0x03: Machine type + config_mode transitions.
    // - Writing to this register disables the boot ROM overlay
    //   (VHDL: bootrom_en <= '0' on any write to nr_03).
    // - VHDL zxnext.vhd:5147-5151 state machine on bits[2:0]:
    //     111           → config_mode ← 1 (re-enter)
    //     000           → no change
    //     001..110 else → config_mode ← 0 (exit; machine_type committed only
    //                     if PREVIOUS config_mode was 1, per zxnext.vhd:5137)
    //   The state is owned by NextReg so it persists across the per-register
    //   write path; apply_nr_03_config_mode_transition() encapsulates the rule.
    // - VHDL zxnext.vhd:5124-5135 updates nr_03_machine_timing (bits[6:4],
    //   gated on bit 7 = 1, user_dt_lock = 0, bit 3 = 0) and XOR-toggles
    //   nr_03_user_dt_lock from bit 3 on every write.
    // - VHDL zxnext.vhd:5137-5145 commits nr_03_machine_type from bits[2:0]
    //   (values 001..100) but ONLY when config_mode = 1 at write time — so
    //   this must happen BEFORE apply_nr_03_config_mode_transition() flips
    //   the mode bit.
    nextreg_.set_write_handler(0x03, [this](uint8_t v) -> uint8_t {
        if (mmu_.boot_rom_enabled()) {
            mmu_.set_boot_rom_enabled(false);
            Log::emulator()->info("Boot ROM disabled by NextREG 0x03 write ({:#04x})", v);
        }

        // Machine-timing update (VHDL :5124-5133). The gate checks the
        // PREVIOUS dt_lock — the XOR on :5135 happens after this block in
        // VHDL (same clock edge), so it doesn't matter whether we sequence
        // them strictly; we follow the source order for readability.
        const bool bit7 = (v & 0x80) != 0;
        const bool bit3 = (v & 0x08) != 0;
        if (bit7 && !nextreg_.nr_03_user_dt_lock() && !bit3) {
            const uint8_t tim_sel = static_cast<uint8_t>((v >> 4) & 0x07);
            uint8_t new_timing;
            switch (tim_sel) {
                case 0x00: new_timing = 0x01; break;  // 48K timing
                case 0x01: new_timing = 0x01; break;
                case 0x02: new_timing = 0x02; break;  // 128K
                case 0x03: new_timing = 0x03; break;  // +3
                case 0x04: new_timing = 0x04; break;  // Pentagon
                default:   new_timing = 0x03; break;  // VHDL :5131 others
            }
            nextreg_.set_nr_03_machine_timing(new_timing);

            // G121: VHDL zxnext.vhd:2033 — pulse_count_end gates on
            // `machine_timing_48 OR machine_timing_p3`, both decoded from
            // nr_03_machine_timing. When NR 0x03 changes the timing post-boot,
            // the pulse-mode INT width must follow (32 cycles for 48K/+3, 36
            // for 128K/Pentagon). Without this fan-out the Im2Controller's
            // machine_48_or_p3_ flag stays stuck at the value set at
            // reset_machine() and the pulse width is wrong after any runtime
            // timing change.
            const bool is_48_or_p3 = (new_timing == 0x01) || (new_timing == 0x03);
            im2_.set_machine_timing_48_or_p3(is_48_or_p3);
        }

        // dt_lock XOR-toggle (VHDL :5135) — unconditional XOR with bit 3.
        nextreg_.set_nr_03_user_dt_lock(nextreg_.nr_03_user_dt_lock() ^ bit3);

        // Machine-type commit (VHDL :5137-5145) — gated on CURRENT config_mode.
        if (nextreg_.nr_03_config_mode()) {
            const uint8_t typ_sel = static_cast<uint8_t>(v & 0x07);
            switch (typ_sel) {
                case 0x01: nextreg_.set_nr_03_machine_type(0x01); break;
                case 0x02: nextreg_.set_nr_03_machine_type(0x02); break;
                case 0x03: nextreg_.set_nr_03_machine_type(0x03); break;
                case 0x04: nextreg_.set_nr_03_machine_type(0x04); break;
                default: /* VHDL :5143 others => null (no change) */ break;
            }
        }

        // config_mode FSM — must run AFTER machine-type commit so the commit
        // sees the PREVIOUS config_mode (VHDL :5137 reads the pre-write value).
        nextreg_.apply_nr_03_config_mode_transition(v);
        // Mirror config_mode into Mmu so the fast-path read/write routing
        // (VHDL zxnext.vhd:3044-3050) tracks the NextReg state.
        mmu_.set_config_mode(nextreg_.nr_03_config_mode());
        Log::emulator()->info("NextREG 0x03 ← {:#04x}  (config_mode={})",
                              v, nextreg_.nr_03_config_mode() ? 1 : 0);
        return v;
    });

    // Register 0x03 read: composed per VHDL zxnext.vhd:5894 —
    //   port_253b_dat <= nr_palette_sub_idx & nr_03_machine_timing(2:0) &
    //                    nr_03_user_dt_lock & nr_03_machine_type(2:0)
    // JNEXT does not model nr_palette_sub_idx (palette-aux selector used
    // only by the NR 0x44 / NR 0x41 sub-index toggle), so bit 7 reads 0
    // until that FSM lands. All other bits come from the state fields on
    // NextReg, which track the VHDL signals faithfully.
    nextreg_.set_read_handler(0x03, [this]() -> uint8_t {
        const uint8_t timing = static_cast<uint8_t>(nextreg_.nr_03_machine_timing() & 0x07);
        const uint8_t mtype  = static_cast<uint8_t>(nextreg_.nr_03_machine_type()   & 0x07);
        const uint8_t dtlock = nextreg_.nr_03_user_dt_lock() ? 1 : 0;
        // palette_sub_idx not modeled → 0. bits[7]=0, [6:4]=timing, [3]=dtlock,
        // [2:0]=machine_type.
        return static_cast<uint8_t>((0u << 7) | (timing << 4) | (dtlock << 3) | mtype);
    });

    // Register 0x04: ROM/RAM bank select used by tbblue.fw's load_roms() to
    // populate Spectrum/DivMMC/Multiface ROMs in SRAM while config_mode=1.
    // VHDL zxnext.vhd:1104,5716-5732 — we take all 8 bits (Issue-5 behaviour);
    // out-of-range banks fall back to 0xFF reads via Ram::page_ptr()==nullptr.
    nextreg_.set_write_handler(0x04, [this](uint8_t v) -> uint8_t {
        nextreg_.set_nr_04_romram_bank(v);
        mmu_.set_nr_04_romram_bank(v);
        Log::emulator()->debug("NextREG 0x04 ← {:#04x}  (romram_bank)", v);
        // G149: VHDL zxnext.vhd:5878-6289 read-mux has no entry for NR 0x04
        // (write-only ROM/RAM bank latch; only consumed by the SRAM
        // address-composition path). Fall-through is (others => '0'); return
        // 0 so reads of NR 0x04 return 0 instead of leaking the latched bank.
        return 0;
    });

    // Registers 0x35-0x39: Sprite attribute bytes 0-4, NO auto-increment
    // (G96). VHDL zxnext.vhd:4857-4875 dispatches mirror_we with
    // mirror_index = "000".."100"; zxnext.vhd:4916 — bit 6 of nr_wr_reg
    // = 0 here, so nr_sprite_mirror_inc stays at 0 and mirror_sprite_q
    // is NOT advanced. The write targets the current mirror_sprite_q
    // sprite slot.
    // G149: VHDL zxnext.vhd:5878-6289 read-mux has no entry for NR 0x35-0x39
    // (write-only sprite-attribute mirror commits). Fall-through is
    // (others => '0'); the lambdas return 0 so reads return 0 instead of
    // leaking the last attribute byte through regs_[].
    for (int i = 0; i < 5; ++i) {
        nextreg_.set_write_handler(static_cast<uint8_t>(0x35 + i),
            [this, i](uint8_t v) -> uint8_t {
                sprites_.write_attr_byte_nr_no_inc(
                    static_cast<uint8_t>(i), v);
                return 0;
            });
    }

    // Register 0x4A: Fallback colour (used when all layers are transparent)
    nextreg_.set_write_handler(0x4A, [this](uint8_t v) -> uint8_t {
        renderer_.set_fallback_colour(v);
        return v;
    });

    // Register 0x68: ULA control
    //   bit 7 = ULA disable (0=enable, 1=disable)
    //   bit 6:5 = ula_blend_mode_2 — feeds mix_rgb / mix_top / mix_bot
    //            selection for priority modes 6/7 (VHDL zxnext.vhd:7141-7178).
    //            Encoding: 00 default, 10 ula_final as U, 11 TM as U, 01 transp.
    //   bit 4 = cancel extended keys (not wired here)
    //   bit 3 = ULA+ enable (VHDL zxnext.vhd:4550-4551 — any NR 0x68 write
    //           unconditionally latches nr_wr_dat(3) into port_ff3b_ulap_en,
    //           distinct from the port-0xFF3B path at :4547-4554 which gates
    //           on port_bf3b_ulap_mode = "01"; see backlog item landed
    //           2026-04-23 after ULA Phase 3 critic discovery).
    //   bit 2 = ULA fine X-scroll enable (VHDL zxnext.vhd:5449, zxula.vhd:199)
    //   bit 0 = stencil mode (VHDL 7112)
    nextreg_.set_write_handler(0x68, [this](uint8_t v) -> uint8_t {
        renderer_.ula().set_ula_enabled((v & 0x80) == 0);
        // VHDL 7141-7178: ula_blend_mode (bits 6:5) selects mix_rgb/top/bot.
        renderer_.set_blend_mode((v >> 5) & 0x03);
        // VHDL 7112: stencil mode (bit 0).
        renderer_.set_stencil_mode((v & 0x01) != 0);
        // Phase 1 scaffold extension — bit 2 → Ula::set_ula_fine_scroll_x.
        renderer_.ula().set_ula_fine_scroll_x((v & 0x04) != 0);
        // VHDL :4550-4551 — bit 3 unconditionally latches ulap_en (a second
        // writer to the same register the port-0xFF3B path drives).
        renderer_.ula().set_ulap_en((v & 0x08) != 0);
        return v;
    });
    // VHDL zxnext.vhd:6093 reads NR 0x68 by composing bit 3 from the live
    // `port_ff3b_ulap_en` rather than from a stored copy. Port 0xFF3B writes
    // mutate ulap_en_ without touching regs_[0x68], so the cached snapshot
    // diverges. Compose bit 3 from the live state at read time.
    nextreg_.set_read_handler(0x68, [this]() -> uint8_t {
        uint8_t v = nextreg_.cached(0x68);
        v = (v & 0xF7) | (renderer_.ula().get_ulap_en() ? 0x08 : 0x00);
        return v;
    });

    // Register 0x69: Display Control 1
    //   bit 7 = Layer 2 enable
    //   bit 6 = ULA shadow display (bank 7)
    //   bits 5:4 = Timex modes (deferred)
    //   bits 3:0 = reserved
    nextreg_.set_write_handler(0x69, [this](uint8_t v) -> uint8_t {
        layer2_.set_enabled((v & 0x80) != 0);
        // VHDL zxnext.vhd:3658-3660 — `nr_69_we` drives
        // `port_7ffd_reg(3) <= nr_wr_dat(6)` regardless of port_7ffd_locked
        // (the lock gates only the full-byte branch at :3650). Bit 3 is
        // the sole producer of port_7ffd_shadow (:3768) and i_ula_shadow_en
        // (:4453); push through Mmu's bit-3 alias setter, then forward
        // the live shadow flag into the ULA — same propagation pattern
        // used by the port-0x7FFD handler at emulator.cpp:1922.
        mmu_.set_port_7ffd_bit3((v & 0x40) != 0);
        renderer_.ula().set_shadow_screen_en(mmu_.shadow_screen_en());
        // VHDL zxnext.vhd:3617-3618 — `nr_69_we` fans bits 5:0 of the
        // new value into `port_ff_reg(5:0)` (the Timex screen-mode
        // surface). port_ff_reg_ is the canonical store; the
        // downstream screen-mode mux must also see the change.
        port_ff_reg_ = static_cast<uint8_t>((port_ff_reg_ & 0xC0)
                                          | (v & 0x3F));
        // G108 — propagate the updated port_ff_reg into Ula::screen_mode_reg_
        // so set_screen_mode re-decodes mode_ + alt_file_ (bits 2:0) and
        // re-derives the HI_RES paper colour (bits 5:3). NextZXOS firmware
        // and Timex programs that switch screen mode via NR 0x69 instead
        // of port 0xFF previously left the renderer in its old mode.
        renderer_.ula().set_screen_mode(port_ff_reg_);
        return v;
    });
    // VHDL zxnext.vhd:6095-6096 — port_253b_dat <=
    //   port_123b_layer2_en & port_7ffd_shadow & port_ff_reg(5 downto 0);
    // Three independent live producers (Layer2 enable, MMU shadow flag from
    // 7FFD bit 3, and the Timex screen-mode register). The bare regs_[0x69]
    // would only echo the last NR 0x69 write and miss port 0x123B / 0x7FFD
    // / 0xFF mid-stream changes. G56-CR-69.
    nextreg_.set_read_handler(0x69, [this]() -> uint8_t {
        uint8_t v = 0;
        if (layer2_.enabled())              v |= 0x80;
        if (mmu_.shadow_screen_en())        v |= 0x40;
        v |= static_cast<uint8_t>(port_ff_reg_ & 0x3F);
        return v;
    });

    // --- DivMMC automap config (NextREG 0xB8-0xBB) ---

    nextreg_.set_write_handler(0xB8, [this](uint8_t v) -> uint8_t { divmmc_.set_entry_points_0(v); return v; });
    nextreg_.set_write_handler(0xB9, [this](uint8_t v) -> uint8_t { divmmc_.set_entry_valid_0(v); return v; });
    nextreg_.set_write_handler(0xBA, [this](uint8_t v) -> uint8_t { divmmc_.set_entry_timing_0(v); return v; });
    nextreg_.set_write_handler(0xBB, [this](uint8_t v) -> uint8_t { divmmc_.set_entry_points_1(v); return v; });

    // Register 0x83: Internal port-enable register 2.
    // VHDL zxnext.vhd:1227, 2392, 2412 — bit 0 of NR 0x83 drives
    // port_divmmc_io_en (when expansion bus disabled), which in turn:
    //   • feeds divmmc_mod.i_en (zxnext.vhd:4147) — gates DivMMC ROM/RAM
    //     overlay; clearing bit 0 hides the overlay even with conmem=1.
    //   • feeds divmmc_automap_reset (zxnext.vhd:4112) — clearing bit 0
    //     asserts reset, blocking automap.
    // Other NR 0x83 bits (port_dffd_io_en at bit 2, port_1ffd_io_en at
    // bit 3, port_p3_floating_bus_io_en at bit 4, port_mouse_io_en at
    // bit 5) are consulted at port-decode time via nextreg_.cached(0x83);
    // here we only need to forward bit 0 to DivMmc as a state setter.
    // G124.
    nextreg_.set_write_handler(0x83, [this](uint8_t v) -> uint8_t {
        divmmc_.set_port_io_enable((v & 0x01) != 0);
        // Wave 1 B1 — bit 1 = port_multiface_io_en (VHDL zxnext.vhd:2415-2416,
        // `internal_port_enable(9)`), gating the Multiface (multiface.vhd:64
        // `enable_i`). When low, all four FFs are held in reset.
        multiface_.set_enabled((v & 0x02) != 0);
        return v;
    });

    // Register 0x85: Port-enable register 4 — read packing.
    // VHDL zxnext.vhd:6138: read returns reset_type & "000" & enable(3:0).
    // Bits 6:4 always read back as zero.
    nextreg_.set_read_handler(0x85, [this]() -> uint8_t {
        return nextreg_.cached(0x85) & 0x8F;
    });

    // Register 0x89: Bus port-enable register 4 — read packing. (G154)
    // VHDL zxnext.vhd:5520-5522 (write): bit 7 → nr_89_bus_port_reset_type,
    // bits 3:0 → nr_89_bus_port_enable; bits 6:4 of the write are discarded.
    // VHDL zxnext.vhd:6149-6150 (read): port_253b_dat <=
    //   nr_89_bus_port_reset_type & "000" & nr_89_bus_port_enable.
    // Bits 6:4 always read back as zero. Same pack-mask shape as NR 0x85.
    nextreg_.set_read_handler(0x89, [this]() -> uint8_t {
        return nextreg_.cached(0x89) & 0x8F;
    });

    // Register 0x8C: Alternate ROM control
    //   bit 7 = nr_8c_altrom_en        (VHDL zxnext.vhd:2262)
    //   bit 6 = nr_8c_altrom_rw        (zxnext.vhd:2263)
    //   bit 5 = nr_8c_altrom_lock_rom1 (zxnext.vhd:2264)
    //   bit 4 = nr_8c_altrom_lock_rom0 (zxnext.vhd:2265)
    //   bits 3:0 = reset-defaults for 7:4 (copied on hard reset, zxnext.vhd:2255)
    // Mirror the full byte on Mmu (Branch C: register storage + accessors;
    // SRAM arbiter override is a follow-up). Retain the Rom-side mirror
    // (rom_.set_alt_rom_config) so existing consumers of Rom::alt_rom_*
    // keep working until they migrate to Mmu.
    nextreg_.set_write_handler(0x8C, [this](uint8_t v) -> uint8_t {
        mmu_.set_nr_8c(v);
        rom_.set_alt_rom_config(v);
        return v;
    });
    nextreg_.set_read_handler(0x8C, [this]() -> uint8_t {
        return mmu_.get_nr_8c();
    });

    // Register 0x8E: Unified paging (VHDL zxnext.vhd:3662-3671 / 3696-3704 /
    // 3726-3734 writes; read-back at zxnext.vhd:6158-6159).
    //   Write: decomposes into 7FFD/DFFD/1FFD register updates; bit 3 =
    //          bank-select enable, bit 2 = special mode, bypasses 7FFD lock.
    //   Read:  re-composes from current 7FFD/DFFD/1FFD state; bit 3 = '1'.
    nextreg_.set_write_handler(0x8E, [this](uint8_t v) -> uint8_t {
        mmu_.write_nr_8e(v);
        return v;
    });
    nextreg_.set_read_handler(0x8E, [this]() -> uint8_t {
        return mmu_.read_nr_8e();
    });

    // Register 0x8F: Mapping Mode (VHDL zxnext.vhd:3787-3794 / 6162).
    //   bits 1:0 = mapping mode — 00 standard, 10 Pentagon-512, 11
    //              Pentagon-1024. Bits 7:2 always read as 0.
    nextreg_.set_write_handler(0x8F, [this](uint8_t v) -> uint8_t {
        mmu_.write_nr_8f(v);
        return v;
    });
    nextreg_.set_read_handler(0x8F, [this]() -> uint8_t {
        return static_cast<uint8_t>(mmu_.nr_8f_mode() & 0x03);
    });

    // --- NextREG interrupt control registers (0xC0-0xCF) ---
    //
    // Phase 2 Wave 2 (Agent E): routed through Im2Controller instead of the
    // previous shadow-store pattern. Each NR handler is now a thin adapter
    // over the fabric API; the authoritative state lives inside Im2Controller.

    // Register 0xC0: Interrupt control.
    // VHDL zxnext.vhd:5596-5599 (write) + :6229-6230 (read).
    //   Write format: VVV_0_S_MM_I
    //     bits 7:5 = nr_c0_im2_vector (MSBs of IM2 vector)
    //     bit  4   = 0 (unused)
    //     bit  3   = nr_c0_stackless_nmi
    //     bits 2:1 = (read-only; ignored on write) z80_im_mode
    //     bit  0   = nr_c0_int_mode_pulse_0_im2_1  (0=pulse, 1=HW IM2)
    //   Read format: matches write, with bits 2:1 reflecting the current
    //   z80_im_mode latched by Agent A's RETI/RETN/IM decoder.
    nextreg_.set_write_handler(0xC0, [this](uint8_t v) -> uint8_t {
        im2_.set_vector_base(static_cast<uint8_t>((v >> 5) & 0x07));
        im2_.set_stackless_nmi((v & 0x08) != 0);
        im2_.set_mode((v & 0x01) != 0);
        // bits 2:1 are read-only (im_mode) — VHDL does not write them.
        return v;
    });
    nextreg_.set_read_handler(0xC0, [this]() -> uint8_t {
        uint8_t v = static_cast<uint8_t>((im2_.vector_base() & 0x07) << 5);
        // bit 4 = '0' per VHDL concat.
        if (im2_.stackless_nmi()) v |= 0x08;
        v |= static_cast<uint8_t>((im2_.im_mode() & 0x03) << 1);
        if (im2_.is_im2_mode())  v |= 0x01;
        return v;
    });

    // Registers 0xC4-0xC6: Interrupt enable.
    // VHDL zxnext.vhd:5607-5617 (write) + :6238-6245 (read).
    //
    // NR 0xC4 — ula_int_en + expbus.
    //   bit 7 = nr_c4_int_en_0_expbus (expansion-bus INT enable; non-IM2)
    //   bit 1 = nr_22_line_interrupt_en (mirror of NR 0x22 bit 1)
    //   bit 0 = NR-side fan-out into port_ff_reg(6) with INVERTED
    //           polarity per VHDL :3621-3622:
    //               port_ff_reg(6) <= NOT nr_wr_dat(0)
    //           That is the ULA-int-disable bit. A NR 0xC4 write of
    //           bit 0 = 1 ENABLES the ULA interrupt (clears bit 6); a
    //           write of bit 0 = 0 DISABLES it (sets bit 6).
    // Read format: E_00000_UU where UU = ula_int_en[1:0] = {line,ula}.
    nextreg_.set_write_handler(0xC4, [this](uint8_t v) -> uint8_t {
        im2_.set_int_en_c4(v);
        // Mirror NR 0xC4 bit 1 → nr_22_line_interrupt_en (VHDL:5610).
        video_timing_.set_line_interrupt_enable((v & 0x02) != 0);
        // G163 — NR 0xC4 bit 1 is a hardware mirror of NR 0x22 bit 1
        // (both write the same `nr_22_line_interrupt_en` flip-flop at
        // VHDL zxnext.vhd:5607-5610, which feeds `i_inten_line` of the
        // line-interrupt comparator at :6752). The same dynamic
        // re-evaluation requirement applies here: a mid-frame NR 0xC4
        // write that toggles bit 1 must invalidate / re-arm the
        // pending line-int event, exactly as for NR 0x22.
        reschedule_line_interrupt();
        // VHDL zxnext.vhd:3621-3622 — `nr_c4_we` fans (NOT bit 0) of
        // the new value into `port_ff_reg(6)`. The polarity is
        // inverted: cpu writes '1' to clear the disable bit.
        port_ff_reg_ = static_cast<uint8_t>((port_ff_reg_ & 0xBF)
                                          | (((~v) & 0x01) << 6));
        // G108 — propagate the updated port_ff_reg into Ula::screen_mode_reg_
        // (same rationale as NR 0x22: only bit 6 is touched, but the
        // per-scanline change-log stays symmetric with port-FF / NR 0x22
        // by routing every fan-out through Ula::set_screen_mode).
        renderer_.ula().set_screen_mode(port_ff_reg_);
        // Bit 7 (expbus int enable) is stored for readback via im2_c4_expbus_.
        im2_c4_expbus_ = (v & 0x80) != 0;
        return v;
    });
    nextreg_.set_read_handler(0xC4, [this]() -> uint8_t {
        // VHDL :6239 — nr_c4_int_en_0_expbus & "00000" & ula_int_en
        // ula_int_en = {nr_22_line_interrupt_en, NOT port_ff_interrupt_disable}
        uint8_t v = 0;
        if (im2_c4_expbus_)                       v |= 0x80;
        if (video_timing_.line_interrupt_enable()) v |= 0x02;
        if (!ula_int_disabled_)                   v |= 0x01;
        return v;
    });

    // NR 0xC5 — ctc_int_en (bits 7:0 = CTC7..CTC0).
    // VHDL zxnext.vhd:4078 `nr_c5_we` also gates CTC i_int_en_wr;
    // the CTC peripheral maintains its own int_enable state that drives
    // the on_interrupt callback. We update BOTH sides so the fabric
    // (im2_.set_int_en_c5) and the CTC's internal enable stay in sync.
    nextreg_.set_write_handler(0xC5, [this](uint8_t v) -> uint8_t {
        ctc_.set_int_enable(v);
        im2_.set_int_en_c5(v);
        return v;
    });
    nextreg_.set_read_handler(0xC5, [this]() -> uint8_t {
        // VHDL :6242 — port_253b_dat <= ctc_int_en
        return ctc_.get_int_enable();
    });

    // NR 0xC6 — UART int enable (0_654_0_210 write format).
    // VHDL zxnext.vhd:5615-5617: splits into nr_c6_int_en_2_654 and
    // nr_c6_int_en_2_210 nibble fields; fabric ORs the low two bits of
    // each for the RX int_en (line 1950).
    nextreg_.set_write_handler(0xC6, [this](uint8_t v) -> uint8_t {
        im2_.set_int_en_c6(v);
        nr_c6_uart_int_en_ = v & 0x77;   // mask out bits 7,3 (read as 0)
        return v;
    });
    nextreg_.set_read_handler(0xC6, [this]() -> uint8_t {
        // VHDL :6245 — '0' & nr_c6_int_en_2_654 & '0' & nr_c6_int_en_2_210
        return nr_c6_uart_int_en_;
    });

    // Registers 0xC8-0xCA: Interrupt status (read=status, write=clear).
    // VHDL zxnext.vhd:1952-1955 (status-clear composition) + :6247-6254 (read).
    //
    // NR 0xC8 — LINE (bit 1) + ULA (bit 0).
    nextreg_.set_write_handler(0xC8, [this](uint8_t v) -> uint8_t {
        if (v & 0x02) im2_.clear_status(Im2Controller::DevIdx::LINE);
        if (v & 0x01) im2_.clear_status(Im2Controller::DevIdx::ULA);
        return v;
    });
    nextreg_.set_read_handler(0xC8, [this]() -> uint8_t {
        return im2_.int_status_mask_c8();
    });

    // NR 0xC9 — CTC 7..0 (each bit clears the corresponding CTC status).
    // VHDL :1953: bits 3..10 of im2_status_clear are gated by nr_c9_we
    // AND nr_wr_dat bits 0..7 respectively. CTC4..CTC7 bits are still
    // honoured here (even though those devices are hardwired 0 upstream)
    // to match VHDL literal decode.
    nextreg_.set_write_handler(0xC9, [this](uint8_t v) -> uint8_t {
        if (v & 0x01) im2_.clear_status(Im2Controller::DevIdx::CTC0);
        if (v & 0x02) im2_.clear_status(Im2Controller::DevIdx::CTC1);
        if (v & 0x04) im2_.clear_status(Im2Controller::DevIdx::CTC2);
        if (v & 0x08) im2_.clear_status(Im2Controller::DevIdx::CTC3);
        if (v & 0x10) im2_.clear_status(Im2Controller::DevIdx::CTC4);
        if (v & 0x20) im2_.clear_status(Im2Controller::DevIdx::CTC5);
        if (v & 0x40) im2_.clear_status(Im2Controller::DevIdx::CTC6);
        if (v & 0x80) im2_.clear_status(Im2Controller::DevIdx::CTC7);
        return v;
    });
    nextreg_.set_read_handler(0xC9, [this]() -> uint8_t {
        return im2_.int_status_mask_c9();
    });

    // NR 0xCA — UART (per VHDL :1952,:1954).
    //   bit 6       = clear UART1 TX
    //   bit 5 | b4  = clear UART1 RX (OR, either bit clears)
    //   bit 2       = clear UART0 TX
    //   bit 1 | b0  = clear UART0 RX (OR, either bit clears)
    nextreg_.set_write_handler(0xCA, [this](uint8_t v) -> uint8_t {
        if (v & 0x40)          im2_.clear_status(Im2Controller::DevIdx::UART1_TX);
        if (v & 0x30)          im2_.clear_status(Im2Controller::DevIdx::UART1_RX);
        if (v & 0x04)          im2_.clear_status(Im2Controller::DevIdx::UART0_TX);
        if (v & 0x03)          im2_.clear_status(Im2Controller::DevIdx::UART0_RX);
        return v;
    });
    nextreg_.set_read_handler(0xCA, [this]() -> uint8_t {
        return im2_.int_status_mask_ca();
    });

    // Registers 0xCC/0xCD/0xCE: IM2 DMA delay enables
    // VHDL zxnext.vhd:5629-5637 (write), :6257-6263 (read), :1957-1958 (compose).
    // Wave 3 Agent F: after any write, recompose the 14-bit mask and hand it
    // to Im2Controller so dma_int_pending() observes the new per-device enables.
    nextreg_.set_write_handler(0xCC, [this](uint8_t v) -> uint8_t {
        nr_cc_dma_delay_on_nmi_ = (v & 0x80) != 0;
        nr_cc_dma_delay_en_ula_ = v & 0x03;
        im2_.set_dma_int_en_mask(compose_im2_dma_int_en());
        // Wave E: NR 0xCC bit 7 (nr_cc_dma_int_en_0_7) feeds VHDL:2007's
        // NMI-activated DMA-delay term. Push the decoded value into the
        // Im2Controller so step_dma_delay() sees it live.
        im2_.set_nr_cc_dma_int_en_0_7(nr_cc_dma_delay_on_nmi_);
        return v;
    });
    nextreg_.set_read_handler(0xCC, [this]() -> uint8_t {
        return static_cast<uint8_t>((nr_cc_dma_delay_on_nmi_ ? 0x80 : 0) |
                                    (nr_cc_dma_delay_en_ula_ & 0x03));
    });
    nextreg_.set_write_handler(0xCD, [this](uint8_t v) -> uint8_t {
        nr_cd_dma_delay_en_ctc_ = v;
        im2_.set_dma_int_en_mask(compose_im2_dma_int_en());
        return v;
    });
    nextreg_.set_read_handler(0xCD, [this]() -> uint8_t {
        return nr_cd_dma_delay_en_ctc_;
    });
    nextreg_.set_write_handler(0xCE, [this](uint8_t v) -> uint8_t {
        nr_ce_dma_delay_en_uart1_ = (v >> 4) & 0x07;
        nr_ce_dma_delay_en_uart0_ = v & 0x07;
        im2_.set_dma_int_en_mask(compose_im2_dma_int_en());
        return v;
    });
    nextreg_.set_read_handler(0xCE, [this]() -> uint8_t {
        return static_cast<uint8_t>(((nr_ce_dma_delay_en_uart1_ & 0x07) << 4) |
                                    (nr_ce_dma_delay_en_uart0_ & 0x07));
    });

    // Register 0x20: Generate unqualified maskable interrupt / read pending.
    // VHDL zxnext.vhd:1946-1947 — NR 0x20 write drives im2_int_unq one-shots
    // that bypass int_en (UNQ-04/05). Per VHDL bit layout:
    //   bit 7 = unq LINE
    //   bit 6 = unq ULA
    //   bits 3:0 = unq CTC 3..0  (VHDL "ctc3 & ctc2 & ctc1 & ctc0" AND nr_wr_dat(3..0))
    //
    // Read returns currently-asserted int_status of the same set.
    nextreg_.set_read_handler(0x20, [this]() -> uint8_t {
        uint8_t v = 0;
        if (im2_.int_status(Im2Controller::DevIdx::LINE)) v |= 0x80;  // bit 7 = LINE
        if (im2_.int_status(Im2Controller::DevIdx::ULA))  v |= 0x40;  // bit 6 = ULA
        if (im2_.int_status(Im2Controller::DevIdx::CTC0)) v |= 0x01;
        if (im2_.int_status(Im2Controller::DevIdx::CTC1)) v |= 0x02;
        if (im2_.int_status(Im2Controller::DevIdx::CTC2)) v |= 0x04;
        if (im2_.int_status(Im2Controller::DevIdx::CTC3)) v |= 0x08;
        return v;
    });
    nextreg_.set_write_handler(0x20, [this](uint8_t v) -> uint8_t {
        // Per VHDL: each set bit drives int_unq for the matching device.
        // raise_unq() is a one-shot: sets int_status + im2_int_req bypassing
        // int_en, exactly matching UNQ-04 / UNQ-05 invariants.
        if (v & 0x80) im2_.raise_unq(Im2Controller::DevIdx::LINE);
        if (v & 0x40) im2_.raise_unq(Im2Controller::DevIdx::ULA);
        if (v & 0x01) im2_.raise_unq(Im2Controller::DevIdx::CTC0);
        if (v & 0x02) im2_.raise_unq(Im2Controller::DevIdx::CTC1);
        if (v & 0x04) im2_.raise_unq(Im2Controller::DevIdx::CTC2);
        if (v & 0x08) im2_.raise_unq(Im2Controller::DevIdx::CTC3);
        return v;
    });

    // --- VHDL reset defaults (written through nextreg_.write so both
    //     regs_[] and subsystem handlers see the correct initial values) ---
    //
    // These defaults are spec'd by zxnext.vhd lines 4926-5100.  They must
    // be written AFTER handlers are installed so the write callbacks can
    // propagate the values into the owning subsystems (palette, renderer,
    // MMU, etc.).  Without this, regs_[] stays at the fill(0) value from
    // NextReg::reset() and Z80 reads of these registers return wrong data.

    nextreg_.write(0x12, 0x08);  // L2 active bank (zxnext.vhd:4945)
    nextreg_.write(0x14, 0xE3);  // global transparent colour (RRRGGGBB magenta)
    nextreg_.write(0x42, 0x07);  // ULANext format (default ink mask)
    nextreg_.write(0x4A, 0xE3);  // fallback RGB colour (magenta)
    nextreg_.write(0x4B, 0xE3);  // sprite transparent index (zxnext.vhd:5003)
    nextreg_.write(0x4C, 0x0F);  // tilemap transparent index (zxnext.vhd:5004)

    // MMU page defaults — VHDL zxnext.vhd:4610-4618.
    // mmu_.reset() already set internal slots; this synchronises regs_[].
    {
        static constexpr uint8_t mmu_defaults[8] =
            {0xFF, 0xFF, 0x0A, 0x0B, 0x04, 0x05, 0x00, 0x01};
        for (int i = 0; i < 8; ++i)
            nextreg_.write(static_cast<uint8_t>(0x50 + i), mmu_defaults[i]);
    }

    // --- Port dispatch handlers ---

    // Layer 2 control — port 0x123B (full 16-bit match).
    // VHDL zxnext.vhd:3904-3935. When cpu_do(4)=0:
    //   bit 0 = write-map enable (CPU writes to L2 RAM banks)
    //   bit 1 = Layer 2 visible (also mirrored by NR 0x69 bit 7 at :3924-3925)
    //   bit 2 = read-map  enable (CPU reads from L2 RAM banks)
    //   bit 3 = map_shadow (CPU L2 map uses NR 0x13 bank, VHDL :2968) — G144
    //   bits 7:6 = segment select (shared read+write)
    // When cpu_do(4)=1: bits 2:0 are the Layer 2 offset register (G92, :3922).
    // Read returns port_123b_dat (VHDL :3933): segment | "00" | shadow | rd_en
    // | enable | wr_en (bit 4 always reads 0; offset register not exposed) —
    // G145.
    port_.register_handler(0xFFFF, 0x123B,
        [this](uint16_t) -> uint8_t {
            // VHDL zxnext.vhd:3933 read-back composition (G145).
            return mmu_.l2_port_readback();
        },
        [this](uint16_t, uint8_t val) {
            // The bit-4 / non-bit-4 dispatch lives inside Mmu::set_l2_port;
            // here we just forward the byte plus the current NR 0x12/NR 0x13
            // bank values (the shadow bank is plumbed through the NR 0x13
            // write handler, but we also push it on every port write to
            // cover Copper-driven NR 0x13 changes that bypass NextReg).
            mmu_.set_l2_shadow_bank(layer2_.shadow_bank());
            mmu_.set_l2_port(val, layer2_.active_bank());
            // Display-enable mirror (port 0x123B bit 1, not in offset mode).
            // Mmu's set_l2_port records the new latches; the offset-mode
            // path leaves them untouched. Layer2 has its own enable flag
            // for the renderer — refresh it from the post-write state.
            if ((val & 0x10) == 0) {
                layer2_.set_enabled((val & 0x02) != 0);
            }
            // Feed L2 read-map-enable into DivMmc so the ROM3-conditional
            // automap gate can honour VHDL zxnext.vhd:3138
            // (sram_divmmc_automap_rom3_en AND NOT sram_layer2_map_en).
            // Read-enable is preserved across an offset-mode write by Mmu;
            // pull from the authoritative latch instead of re-decoding val.
            divmmc_.set_layer2_map_read(mmu_.l2_read_enable());
        });

    // 128K bank switch — port 0x7FFD.
    // VHDL zxnext.vhd:2593: port_7ffd <= A15=0 AND (A14=1 OR NOT p3_timing)
    //   AND port_fd AND NOT port_1ffd AND port_7ffd_io_en.
    // port_fd = A1:0="01" (line 2578). Mask: A15=0, A1=0, A0=1 → 0x8003/0x0001.
    // With exclusive dispatch (most-specific-match-wins), the +3 handler
    // (mask 0xF003) naturally wins over this handler for 0x1FFD addresses,
    // implementing the VHDL NOT port_1ffd gate without explicit code.
    port_.register_handler(0x8003, 0x0001,
        nullptr,
        [this](uint16_t port, uint8_t v) {
            // VHDL 2593: on +3 timing, require A14=1
            if (config_.type == MachineType::ZX_PLUS3 && (port & 0x4000) == 0) return;
            // VHDL 2399: port_7ffd_io_en <= internal_port_enable(1) = NR 0x82 bit 1
            if ((nextreg_.cached(0x82) & 0x02) == 0) return;

            mmu_.map_128k_bank(v);
            // VHDL zxnext.vhd:4453 — port_7ffd_reg(3) drives i_ula_shadow_en
            // on the ULA bus. Same pattern as the NR 0x68 bit 3 → set_ulap_en
            // forward (commit a1495ba). Read through the MMU accessor so the
            // bit-3 source of truth stays in the MMU (matches VHDL where
            // port_7ffd_reg is the sole producer of i_ula_shadow_en).
            renderer_.ula().set_shadow_screen_en(mmu_.shadow_screen_en());
            // Push the new ROM3 state into DivMmc. VHDL zxnext.vhd:3138
            // composites sram_divmmc_automap_rom3_en from sram_pre_rom3
            // (derived from sram_rom == "11"); Task 7 Branch B exposes the
            // ROM-selection signal to DivMmc so entry points with NR 0xB9
            // bit=0 gate correctly on ROM3 (EP1..EP7 path).
            divmmc_.set_rom3_active(mmu_.rom3_selected());
            // Update 0xC000 contention based on machine type (VHDL zxnext.vhd:4489-4493):
            //   128K: odd banks (1,3,5,7) are contended
            //   +3:   banks >= 4 (4,5,6,7) are contended
            uint8_t bank = v & 0x07;
            bool slot3_contended;
            if (config_.type == MachineType::ZX_PLUS3)
                slot3_contended = (bank >= 4);
            else
                slot3_contended = (bank & 1) != 0;
            contention_.set_contended_slot(3, slot3_contended);
            // Mirror per-16K-slot contention into Mmu so the
            // p3_floating_bus_dat latch (VHDL zxnext.vhd:4498-4509)
            // captures CPU r/w bytes on contended pages.
            mmu_.set_slot_contended(3, slot3_contended);
            // G53 (2026-05-01): the legacy z80_set_page_contended(6/7,...)
            // calls were retired alongside the FUSE memory_map_*[] tables.
            // Per-cycle contention now flows from `contention_` (above) +
            // `mmu_` (mem_active_page) into `ContentionModel::contention_tick()`
            // via the FUSE callbacks in src/cpu/z80_cpu.cpp.
        });

    // +3 floppy I/O trap (VHDL zxnext.vhd:2598-2602, 3835).
    //   port_xffd  <= A15:14="00" AND A1:0="01"
    //   port_2ffd  <= cpu_a(13:12)="10" AND port_xffd AND nr_d8_io_trap_fdc_en
    //   port_3ffd  <= cpu_a(13:12)="11" AND port_xffd AND nr_d8_io_trap_fdc_en
    //   nmi_gen_iotrap <= port_2ffd_rd OR port_3ffd_rd OR port_3ffd_wr
    // i.e. port 0x2FFD READ, 0x3FFD READ, 0x3FFD WRITE all strobe iotrap;
    //      port 0x2FFD WRITE does NOT.
    //
    // Mask 0xF003: A15:12 + A1:0 — captures the address-line decode
    // exactly. The handlers strobe NmiSource::strobe_iotrap() only
    // when the NR 0xD8 bit 0 enable is on (the upstream VHDL gate).
    // The MF NMI path itself remains gated on NR 0x06 bit 3 (handled
    // inside `NmiSource::nmi_assert_mf()`).
    //
    // (G162 closure — MF-G162-02 in nmi_test plan.)
    port_.register_handler(0xF003, 0x2001,
        [this](uint16_t port) -> uint8_t {
            // 0x2FFD READ — trap (VHDL:3835 port_2ffd_rd term).
            // When NR 0xD8 bit 0 is off, VHDL's `port_2ffd` decode is
            // false and the access falls through to floating-bus /
            // open-bus behaviour. We mirror that by returning 0xFF
            // only when the gate is on (FDC not modelled); otherwise
            // surface the floating-bus value via Mmu::floating_bus_read.
            if (nr_d8_io_trap_fdc_en_) {
                nmi_source_.strobe_iotrap();
                // VHDL zxnext.vhd:3871-3873 — port_2ffd_rd → cause "01".
                nr_da_iotrap_cause_ = 0x01;
                return 0xFF;  // FDC data port — open bus, FDC unmodelled.
            }
            return floating_bus_read();
        },
        nullptr);  // 0x2FFD WRITE: VHDL-only port_2ffd_wr is NOT an iotrap source.
    port_.register_handler(0xF003, 0x3001,
        [this](uint16_t port) -> uint8_t {
            // 0x3FFD READ — trap (VHDL:3835 port_3ffd_rd term).
            if (nr_d8_io_trap_fdc_en_) {
                nmi_source_.strobe_iotrap();
                // VHDL zxnext.vhd:3874-3875 — port_3ffd_rd → cause "10".
                nr_da_iotrap_cause_ = 0x02;
                return 0xFF;
            }
            return floating_bus_read();
        },
        [this](uint16_t, uint8_t v) {
            // 0x3FFD WRITE — trap (VHDL:3835 port_3ffd_wr term).
            if (nr_d8_io_trap_fdc_en_) {
                nmi_source_.strobe_iotrap();
                // VHDL zxnext.vhd:3876-3878 — port_3ffd_wr → cause "11".
                nr_da_iotrap_cause_ = 0x03;
                // VHDL zxnext.vhd:3892-3893 — `nr_d9_iotrap_write <= cpu_do`
                // when port_3ffd_wr AND nmi_accept_cause. The accept gate
                // is the same condition that drives nr_da set, so we
                // capture in the same conditional.
                nr_d9_iotrap_write_ = v;
            }
        });

    // +3 paging — port 0x1FFD.
    // VHDL zxnext.vhd:2599: port_1ffd <= A15:14="00" AND A13:12="01" AND port_fd.
    // Mask: A15:12="0001", A1=0, A0=1 → 0xF003/0x1001.
    port_.register_handler(0xF003, 0x1001,
        nullptr,
        [this](uint16_t, uint8_t v) {
            // VHDL zxnext.vhd:2599: gated by port_1ffd_io_en (NR 0x82 bit 3).
            if ((nextreg_.cached(0x82) & 0x08) == 0) return;
            mmu_.map_plus3_bank(v);
            // Push the new ROM3 state to DivMmc (Task 7 Branch B).
            divmmc_.set_rom3_active(mmu_.rom3_selected());
            // Update per-slot contention for +3 (VHDL: banks >= 4 are contended).
            bool special_mode = (v & 0x01) != 0;
            if (special_mode) {
                // Special paging: 4 configs map specific banks to each 16K slot
                static const uint8_t configs[4][4] = {
                    {0, 1, 2, 3}, {4, 5, 6, 7}, {4, 5, 6, 3}, {4, 7, 6, 3}
                };
                uint8_t config = (v >> 1) & 0x03;
                for (int slot = 0; slot < 4; ++slot) {
                    bool c = (configs[config][slot] >= 4);
                    contention_.set_contended_slot(slot, c);
                    // Mirror into Mmu for p3_floating_bus_dat latch
                    // (VHDL zxnext.vhd:4498-4509).
                    mmu_.set_slot_contended(slot, c);
                }
            } else {
                // Normal paging: slot 0 = ROM (not contended), slot 1 = bank 5 (contended),
                // slot 2 = bank 2 (not contended), slot 3 = per 0x7FFD bank
                contention_.set_contended_slot(0, false);
                contention_.set_contended_slot(1, true);
                contention_.set_contended_slot(2, false);
                // Mirror into Mmu (slot 3 stays as set by 0x7FFD handler).
                mmu_.set_slot_contended(0, false);
                mmu_.set_slot_contended(1, true);
                mmu_.set_slot_contended(2, false);
                // Slot 3 contention stays as set by 0x7FFD handler
            }
        });

    // NextREG select — full 16-bit match on port 0x243B.
    port_.register_handler(0xFFFF, 0x243B,
        nullptr,
        [this](uint16_t, uint8_t v) { nextreg_.select(v); });

    // NextREG data — full 16-bit match on port 0x253B.
    // Writes are deferred during the per-instruction tick window (G65 —
    // see Emulator::enqueue_cpu_nr_write doc). Outside the window
    // (test-setup port writes, ROM init via emulator API, etc.) the
    // commit happens immediately so external callers observe the
    // expected synchronous behaviour.
    port_.register_handler(0xFFFF, 0x253B,
        [this](uint16_t) -> uint8_t { return nextreg_.read_selected(); },
        [this](uint16_t, uint8_t v)  {
            if (defer_cpu_nr_writes_) {
                enqueue_cpu_nr_write(nextreg_.selected(), v);
            } else {
                nextreg_.write_selected(v);
            }
        });

    // ULA — port 0xFE (mask 0x00FF, value 0x00FE).
    // Read (VHDL zxnext.vhd:3459):
    //   port_fe_dat_0 <= '1' & (i_AUDIO_EAR or port_fe_ear) & '1' & i_KBD_COL
    // where `i_AUDIO_EAR` is the relaxed tape-input signal (zxnext_top_issue2.vhd:
    // symmetric_relaxation, i_relax_0/1 = zxn_issue2_fe_mic =
    // port_fe_mic AND nr_08_keyboard_issue2). In the no-tape idle regime the
    // relaxation steady-state collapses to `port_fe_mic AND nr_08_keyboard_issue2`.
    // jnext simplification: idle tape pin is '1' (no signal); we therefore
    // OR a pragmatic `audio_ear_eff` with `port_fe_ear` (OUT 0xFE bit 4 latch):
    //   audio_ear_eff = tape_playing ? tape_bit
    //                 : issue2_on    ? port_fe_mic  (OUT 0xFE bit 3 latch)
    //                                : 1            (idle tape high, matches
    //                                                 i_AUDIO_EAR when issue2=0
    //                                                 and no tape; the full
    //                                                 VHDL relaxation is
    //                                                 approximated at steady
    //                                                 state).
    // Write: bits 2-0 = border colour; bit 4 = EAR; bit 3 = MIC.
    port_.register_handler(0x00FF, 0x00FE,
        [this](uint16_t port) -> uint8_t {
            uint8_t addr_high = static_cast<uint8_t>(port >> 8);
            uint8_t result = 0xE0 | (keyboard_.read_rows(addr_high) & 0x1F);
            // Default `audio_ear_eff = 1` (idle tape pin pull-up). Tape
            // playback overrides. Issue-2 feedback substitutes MIC.
            uint8_t audio_ear_eff = 1;
            if (tape_.is_playing()) {
                audio_ear_eff = tape_.tick_realtime(0);
            } else if (tzx_tape_.is_playing()) {
                // Use FUSE's live T-state counter (advances mid-instruction during I/O).
                uint64_t cpu_clocks = static_cast<uint64_t>(*fuse_z80_tstates_ptr());
                audio_ear_eff = tzx_tape_.update(cpu_clocks);
            } else if (wav_tape_.is_playing()) {
                uint64_t cpu_clocks = static_cast<uint64_t>(*fuse_z80_tstates_ptr());
                audio_ear_eff = wav_tape_.get_ear_bit(cpu_clocks);
            } else if ((nr_08_stored_low_ & 0x01) != 0) {
                // Issue-2 MIC→EAR feedback (VHDL zxnext.vhd:1636, :3459):
                // i_AUDIO_EAR steady-state = port_fe_mic AND nr_08_keyboard_issue2.
                audio_ear_eff = beeper_.mic() ? 1 : 0;
            }
            // Bit 6 = audio_ear_eff OR port_fe_ear (OUT 0xFE bit 4 latch).
            uint8_t bit6 = (audio_ear_eff | (beeper_.ear() ? 1 : 0)) & 1;
            result = (result & ~0x40) | (bit6 << 6);
            return result;
        },
        [this](uint16_t, uint8_t val) {
            renderer_.ula().set_border(val & 0x07);
            beeper_.set_ear((val >> 4) & 1);
            beeper_.set_mic((val >> 3) & 1);
        });

    // Timex screen mode — port 0xFF (full 16-bit match).
    // Write: bits 2:0 = video mode per VHDL zxula.vhd:191
    //                   `screen_mode_s <= i_port_ff_reg(2 downto 0)`
    //          (0=standard, 1=standard+alt, 2=hi-colour, 3=hi-colour+alt,
    //           6=hi-res, 7=hi-res+alt).  Bit 0 selects alt-display-file
    //          (vram_a bit 13, zxula.vhd:218,235).
    //        bits 5:3 = HI_RES paper colour (zxula.vhd:419 — encoded as
    //                   border_clr_tmx <= "01" & ~port_ff(5:3) & port_ff(5:3)).
    // Read is not implemented on real hardware; omit read handler.
    // VHDL zxnext.vhd:2397: port_ff_io_en <= internal_port_enable(0) = NR 0x82 bit 0.
    port_.register_handler(0xFFFF, 0x00FF,
        nullptr,
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x82) & 0x01) == 0) return;
            // VHDL zxnext.vhd:3615-3616 — port_ff_wr branch latches
            // the entire byte into port_ff_reg, beating any NR-side
            // partial fan-out from NR 0x69 / 0x22 / 0xC4 (G108).
            port_ff_reg_ = val;
            renderer_.ula().set_screen_mode(val);
        });

    // +3 floating-bus surface — port 0x0FFD.
    // VHDL zxnext.vhd:2589: port_p3_float decode = A15:12="0000" AND port_fd
    // (A1:0="01") AND p3_timing_hw_en='1' AND port_p3_floating_bus_io_en='1'.
    // Mask: A15..A12=1 + A1=1 + A0=1 → 0xF003. Match: A15:12="0000",A1=0,A0=1
    // → 0x0001. The 0x7FFD handler (mask 0x8003) overlaps but is less
    // specific; most-specific-match-wins dispatch routes 0x0FFD here.
    //
    // Read mux:
    //   - Non-+3 machines: decode blocked (p3_timing_hw_en='0') →
    //     port_p3_float_rd_dat = X"00" (zxnext.vhd:2814).
    //   - port_p3_floating_bus_io_en=0 (NR 0x82 bit 4 cleared,
    //     zxnext.vhd:2403): decode blocked → 0x00.
    //   - +3 + io_en=1 + port_7ffd_locked='1': X"FF" (zxnext.vhd:4517
    //     port_p3_floating_bus_dat <= ... else X"FF").
    //   - +3 + io_en=1 + port_7ffd_locked='0': p3_floating_bus_dat with
    //     bit 0 forced high (zxula.vhd:573 OR i_timing_p3 in the active
    //     arm; the border arm passes the latch through unchanged but
    //     the bit-0 force is also applied combinationally on +3).
    //     Branch B simplification: returns the latched byte | 0x01;
    //     the per-raster-phase distinction (active VRAM byte vs border
    //     latch fallback) is Branch C's fixture work.
    // Write-only on real hardware in this slot — we intentionally drop
    // writes (no decoded write semantic per VHDL).
    port_.register_handler(0xF003, 0x0001,
        [this](uint16_t) -> uint8_t {
            // VHDL zxnext.vhd:2589 — p3_timing_hw_en gate.
            if (config_.type != MachineType::ZX_PLUS3) return 0x00;
            // VHDL zxnext.vhd:2403 — port_p3_floating_bus_io_en = NR 0x82 bit 4.
            if ((nextreg_.cached(0x82) & 0x10) == 0) return 0x00;
            // VHDL zxnext.vhd:4517 — port_7ffd_locked='1' forces 0xFF.
            if (mmu_.paging_locked()) return 0xFF;
            // VHDL zxula.vhd:573 — bit 0 OR i_timing_p3 (always 1 on +3).
            return static_cast<uint8_t>(mmu_.p3_floating_bus_dat() | 0x01);
        },
        nullptr);

    // Sprite slot select and status — port 0x303B (full 16-bit match).
    port_.register_handler(0xFFFF, 0x303B,
        [this](uint16_t) -> uint8_t { return sprites_.read_status(); },
        [this](uint16_t, uint8_t val) { sprites_.write_slot_select(val); });

    // Sprite attributes — port 0x57 (low byte match).
    port_.register_handler(0x00FF, 0x0057,
        nullptr,
        [this](uint16_t, uint8_t val) { sprites_.write_attribute(val); });

    // Sprite pattern data — port 0x5B (low byte match).
    port_.register_handler(0x00FF, 0x005B,
        nullptr,
        [this](uint16_t, uint8_t val) { sprites_.write_pattern(val); });

    // Port 0xDFFD — Profi/Next extended paging. VHDL zxnext.vhd:2596.
    // Decode: A15:12="1101", port_fd (A1:0="01") → mask 0xF003/0xD001.
    // Write-only in VHDL (no port_dffd_rd signal); reads at 0xDFFD fall
    // through to port_fffd_rd (AY select) per VHDL line 2771.
    // VHDL zxnext.vhd:2400 — port_dffd write is gated by
    // internal_port_enable(2) = nr_82_internal_port_enable(2). When NR 0x82
    // bit 2 = 0, the port is silenced and the write is dropped.
    // Phase 2 A: forwards to Mmu::write_port_dffd which stores bits 4:0 and
    // re-composes MMU6/7 with the extra bank bits per VHDL:3763-3766.
    port_.register_handler(0xF003, 0xD001,
        nullptr,
        [this](uint16_t, uint8_t v) {
            if ((nextreg_.cached(0x82) & 0x04) == 0) return;  // NR 0x82 b2 gate
            mmu_.write_port_dffd(v);
        });

    // Port 0xEFF7 — Pentagon-1024 disable / RAM-at-0x0000. VHDL zxnext.vhd:2604.
    // Decode: A15:12="1110", port_f7_lsb (low byte = 0xF7) → A14:13 are
    // don't-care so decode spans 0xE0F7..0xEFF7 (mask 0xF0FF / match 0xE0F7).
    // 0xEFF7 is the canonical documented address. Write-only per VHDL:3780-3785
    // (no port_eff7_rd path). Forwards to Mmu::write_port_eff7 which stores
    // bits 2,3 and re-runs the port_memory_change_dly MMU0/1 rebuild so the
    // RAM-at-0x0000 swap (bit 3) lands immediately per VHDL:4619-4644.
    //
    // Gate (G143 — corrected 2026-05-04): VHDL zxnext.vhd:2604 ANDs
    // port_eff7_lsb with port_eff7_io_en, which per zxnext.vhd:2441 is
    // internal_port_enable(26).  internal_port_enable is the concatenation
    // (nr_85 & nr_84 & nr_83 & nr_82) at zxnext.vhd:2392 — bit 26 sits in
    // the nr_85 range (bits 24..27) and corresponds to NR 0x85 bit 2 (per
    // doc/testing/IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md:191 + port_test.cpp
    // NR85-02 row). The G143 fix originally checked NR 0x84 b2; that was
    // incorrect — corrected here to NR 0x85 b2.
    port_.register_handler(0xF0FF, 0xE0F7,
        nullptr,
        [this](uint16_t, uint8_t v) {
            if ((nextreg_.cached(0x85) & 0x04) == 0) return;  // NR 0x85 b2 gate
            mmu_.write_port_eff7(v);
        });

    // --- Audio port handlers ---

    // AY register select — port 0xFFFD. VHDL zxnext.vhd:2647.
    // Decode: A15:14="11", A2=1, port_fd (A1:0="01") → mask 0xC007/0xC005.
    // VHDL 2772: port_fffd_wr gated by NOT port_dffd — when port_dffd
    // also matches (e.g. 0xDFFD), the AY write is suppressed.
    // VHDL 2771: port_fffd_rd does NOT gate on port_dffd — reads at
    // overlapping addresses still return AY data.
    // VHDL 2647: port_fffd decode includes port_ay_io_en = internal_port_enable(16)
    // = NR 0x84 bit 0. When disabled, the port does not decode and the read
    // falls through to the floating bus default.
    port_.register_handler(0xC007, 0xC005,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x84) & 0x01) == 0) return 0xFF;  // gated off
            return turbosound_.reg_read(false);
        },
        [this](uint16_t port, uint8_t val) {
            if ((nextreg_.cached(0x84) & 0x01) == 0) return;  // gated off
            // VHDL 2772: NOT port_dffd gate is handled by exclusive dispatch —
            // Pentagon handler (mask 0xF003, 6 bits) is more specific than
            // this AY handler (mask 0xC007, 5 bits), so Pentagon wins for
            // 0xDFFD writes and this handler is never called.
            turbosound_.reg_addr(val);
        });

    // AY data write — port 0xBFFD. VHDL zxnext.vhd:2648.
    // Decode: A15:14="10", A2=1, port_fd (A1:0="01") → mask 0xC007/0x8005.
    // VHDL 2648: port_bffd includes port_ay_io_en gating.
    //
    // Read side: VHDL zxnext.vhd:2771 aliases BFFD reads to FFFD reads on
    // +3 timing only: `port_fffd_rd <= iord and (port_fffd or (port_bffd
    // and machine_timing_p3) or port_bff5)`. On non-+3 machines BFFD
    // reads are not decoded and return the floating-bus default.
    port_.register_handler(0xC007, 0x8005,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x84) & 0x01) == 0) return 0xFF;  // gated off
            if (config_.type != MachineType::ZX_PLUS3) return 0xFF;  // +3 alias only
            return turbosound_.reg_read(false);
        },
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x84) & 0x01) == 0) return;  // gated off
            turbosound_.reg_write(val);
        });

    // AY register-query read — port 0xBFF5. VHDL zxnext.vhd:2649.
    // `port_bff5 <= '1' when port_bffd = '1' and cpu_a(3) = '0' else '0'`.
    // When A3=0 the AY chip switches to register-query output
    // (psg_d_o_reg_i => port_bff5, zxnext.vhd:6395) and reg_read(true)
    // returns AY_ID & selected_register per VHDL oracle.
    //
    // This handler is strictly more specific than the BFFD handler above
    // (6 mask bits vs 5), so most-specific-wins dispatch routes 0xBFF5
    // reads here. Writes to 0xBFF5 also decode as port_bffd_wr in VHDL
    // (zxnext.vhd:6393 `psg_reg_wr_i => port_bffd_wr`), so we forward
    // the write to the normal data-write path.
    //
    // Decode: A15:14="10", A3=0, A2=1, A1:0="01" → mask 0xC00F / val 0x8005.
    port_.register_handler(0xC00F, 0x8005,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x84) & 0x01) == 0) return 0xFF;  // gated off
            return turbosound_.reg_read(true);   // reg_mode = true
        },
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x84) & 0x01) == 0) return;  // gated off
            turbosound_.reg_write(val);
        });

    // DAC ports — Soundrive Mode 1 (most common)
    // Channel A (left):  port 0x1F
    // Channel B (left):  port 0x0F
    // Channel C (right): port 0x4F
    // Channel D (right): port 0x5F (handled below with 16-bit match)
    //
    // NR 0x84 gating (G114, VHDL zxnext.vhd:2429-2435):
    //   * bit 1 → port_dac_sd1_ABCD_1f0f4f5f_io_en (Soundrive Mode 1)
    //   * bit 4 → port_dac_stereo_BC_0f4f_io_en    (Covox BC)
    // Ports 0x0F and 0x4F are reachable via SD1 (b1) OR Covox (b4) —
    // their decode in VHDL is the OR of the two enables (port_dac_B /
    // port_dac_C fan-in at :2662-2663). Port 0x1F is SD1-only.
    port_.register_handler(0x00FF, 0x001F, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            if ((nextreg_.cached(0x84) & 0x02) == 0) return;   // SD1 b1 gate
            dac_.write_channel(0, val);
        });
    port_.register_handler(0x00FF, 0x000F, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            // 0x0F: SD1 ch B (b1) OR Covox ch B (b4).
            if ((nextreg_.cached(0x84) & 0x12) == 0) return;   // b1 | b4
            dac_.write_channel(1, val);
        });
    port_.register_handler(0x00FF, 0x004F, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            // 0x4F: SD1 ch C (b1) OR Covox ch C (b4).
            if ((nextreg_.cached(0x84) & 0x12) == 0) return;   // b1 | b4
            dac_.write_channel(2, val);
        });

    // Soundrive Mode 1 channel D (port 0x5F) + Profi Covox channel A
    // (port 0x3F). VHDL zxnext.vhd:2429 (port_dac_sd1_ABCD_1f0f4f5f_io_en
    // = NR 0x84 b1) and :2431 (port_dac_stereo_AD_3f5f_io_en = NR 0x84
    // b3). These use a full 16-bit match so they do not alias with other
    // 0x_F-family peripherals (sprite pattern port 0x5B in particular
    // uses a 0x00FF low-byte mask — the original code deferred 0x5F
    // wiring on that concern).
    //
    // 0x3F: Profi-only (b3). 0x5F: SD1 ch D (b1) OR Profi ch D (b3).
    port_.register_handler(0xFFFF, 0x003F, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            if ((nextreg_.cached(0x84) & 0x08) == 0) return;   // Profi b3 gate
            dac_.write_channel(0, val);
        });
    port_.register_handler(0xFFFF, 0x005F, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            // 0x5F reachable via SD1 (b1) OR Profi (b3).
            if ((nextreg_.cached(0x84) & 0x0A) == 0) return;   // b1 | b3
            dac_.write_channel(3, val);
        });

    // Soundrive Mode 2: 0xF1=A, 0xF3=B, 0xF9=C, 0xFB=D (vhd:2432).
    // NR 0x84 bit 2 = port_dac_sd2_ABCD_f1f3f9fb_io_en (G114).
    // Port 0xFB ALSO mirrors Pentagon mono ch A+D fan-out (vhd:2660)
    // when port_dac_mono_AD_fb_io_en is effective, so the 0xFB handler
    // below honours the VHDL gate composition instead of unconditionally
    // routing to ch D.
    port_.register_handler(0xFFFF, 0x00F1, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            if ((nextreg_.cached(0x84) & 0x04) == 0) return;   // SD2 b2 gate
            dac_.write_channel(0, val);
        });
    port_.register_handler(0xFFFF, 0x00F3, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            if ((nextreg_.cached(0x84) & 0x04) == 0) return;   // SD2 b2 gate
            dac_.write_channel(1, val);
        });
    port_.register_handler(0xFFFF, 0x00F9, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            if ((nextreg_.cached(0x84) & 0x04) == 0) return;   // SD2 b2 gate
            dac_.write_channel(2, val);
        });
    port_.register_handler(0xFFFF, 0x00FB, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            // VHDL zxnext.vhd:2664 — port_dac_D fires on 0xFB when either
            // SD2 mode (NR 0x84 bit 2) OR mono_AD_fb effective is set;
            // :2661 — port_dac_A fires on 0xFB only via mono_AD fan-out.
            // mono_AD_fb_eff = NR 0x84 bit 5 AND NOT bit 2 (vhd:2433).
            const uint8_t nr84 = nextreg_.cached(0x84);
            const bool sd2_en  = (nr84 & 0x04) != 0;         // bit 2
            const bool mono_ad = (nr84 & 0x20) && !sd2_en;   // bit 5 AND NOT bit 2
            if (sd2_en || mono_ad) dac_.write_channel(3, val);
            if (mono_ad)           dac_.write_channel(0, val);
        });

    // GS Covox mono fan-out — port 0xB3 writes both channels B and C.
    // VHDL zxnext.vhd:2659 `port_dac_mono_BC <= '1' when port_b3_lsb='1'
    // and port_dac_mono_BC_b3_io_en='1' else '0'`, routed to channels B
    // and C via the port_dac_B/port_dac_C fan-in at :2662-2663. NR 0x84
    // bit 6 (G114) gates this fan-out.
    port_.register_handler(0xFFFF, 0x00B3, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            if ((nextreg_.cached(0x84) & 0x40) == 0) return;   // GS Covox b6
            dac_.write_channel(1, val);
            dac_.write_channel(2, val);
        });

    // Specdrum: port 0xDF → channels A+D. VHDL zxnext.vhd:2674 routes
    // 0xDF reads through port_1f (joystick 1) when the combo gate fires.
    //
    // Full VHDL gate at zxnext.vhd:2674:
    //   port_1f = (port_1f_lsb OR
    //              (port_df_lsb AND port_dac_mono_AD_df_io_en
    //                          AND NOT port_mouse_io_en))
    //            AND port_1f_io_en AND port_1f_hw_en
    //
    // For 0xDF the alias gate requires (G130 closure):
    //   * port_dac_mono_AD_df_io_en (NR 0x84 bit 7,
    //     internal_port_enable(23), zxnext.vhd:2435)
    //   * NOT port_mouse_io_en      (NR 0x83 bit 5 cleared,
    //     internal_port_enable(13), zxnext.vhd:2422)
    //   * port_1f_io_en             (NR 0x82 bit 6,
    //     internal_port_enable(6), zxnext.vhd:2407)
    //   * port_1f_hw_en             (joyL_1f_en OR joyR_1f_en,
    //     zxnext.vhd:2454; live ⇔ Kempston1 OR MD3-Left on either
    //     connector per zxnext.vhd:3475-3491). Reviewer NIT (Wave-2
    //     NEW-MS-1): both this and the Soundrive gate must hold.
    //
    // When all four gates hold, the read returns the same byte the
    // Kempston-1 0x001F path delivers — i.e. joystick_.read_port_1f().
    // Otherwise the port stays undecoded and the floating-bus default
    // 0x00 (matching the pre-fix behaviour) is returned. (G130 closure.)
    port_.register_handler(0x00FF, 0x00DF,
        [this](uint16_t) -> uint8_t {
            // NR 0x84 bit 7 — Specdrum/DAC enable for 0xDF.
            if ((nextreg_.cached(0x84) & 0x80) == 0) return 0x00;
            // NR 0x83 bit 5 — port_mouse_io_en MUST be cleared.
            if ((nextreg_.cached(0x83) & 0x20) != 0) return 0x00;
            // NR 0x82 bit 6 — port_1f_io_en gate.
            if ((nextreg_.cached(0x82) & 0x40) == 0) return 0x00;
            // port_1f_hw_en: at least one connector in Kempston1 or
            // MD3-Left (joyL_1f_en / joyR_1f_en live). VHDL zxnext.vhd:2454.
            if (!joystick_.port_1f_hw_en()) return 0x00;
            return joystick_.read_port_1f();
        },
        [this](uint16_t, uint8_t val) {
            // VHDL zxnext.vhd:2435 — port_dac_mono_AD_df_io_en =
            // internal_port_enable(23) = NR 0x84 bit 7 (G114). When the
            // gate is clear the SpecDrum DAC port is not decoded and the
            // write is silently dropped.
            if (!dac_enabled_) return;
            if ((nextreg_.cached(0x84) & 0x80) == 0) return;
            dac_.write_channel(0, val);
            dac_.write_channel(3, val);
        });

    // --- Audio NextREG handlers ---

    // Register 0x06: Peripheral 2 — bits 1:0 = PSG mode (00=YM, 01=AY)
    //   bit 7 = nr_06_hotkey_cpu_speed_en   (zxnext.vhd:5162; not stored here yet)
    //   bit 6 = nr_06_internal_speaker_beep (zxnext.vhd:5163; not emulated yet)
    //   bit 5 = nr_06_hotkey_5060_en        (zxnext.vhd:5164; not emulated yet)
    //   bit 4 = nr_06_button_drive_nmi_en   (zxnext.vhd:5165) — DivMMC NMI gate
    //   bit 3 = nr_06_button_m1_nmi_en      (zxnext.vhd:5166) — Multiface NMI gate
    //   bit 2 = nr_06_ps2_mode (config-mode-only; zxnext.vhd:5167-5169)
    //   bits 1:0 = nr_06_psg_mode (zxnext.vhd:5170)
    //
    // Phase 2 Agent I (2026-04-21): bits 3 and 4 are stored on the
    // Emulator and consumed by nmi_assert_mf() / nmi_assert_divmmc()
    // (declared in emulator.h). Single-fanout pattern preserved — this
    // is still the only NR 0x06 write handler.
    //
    // VHDL signal default '0' (zxnext.vhd:1109-1110) — re-clear on every
    // init() so reset() (and soft_reset(), which both reach init()) puts
    // them back to power-on state. Tests rely on this explicit clear.
    nr_06_button_m1_nmi_en_       = false;
    nr_06_button_drive_nmi_en_    = false;
    nr_06_internal_speaker_beep_  = false;
    nr_06_ps2_mode_               = false;  // VHDL :1111 default '0' (G56)
    // G110 — refresh Mixer's exc_i now that the NR 0x06 b6 shadow has been
    // cleared (and `nr_08_stored_low_ = 0x10` earlier in init() set the b4
    // power-on default). beep_spkr_excl is the AND of the two; both bits
    // must be in their reset state before the gate can be evaluated.
    mixer_.set_exc_i(beep_spkr_excl());
    nextreg_.set_write_handler(0x06, [this](uint8_t v) -> uint8_t {
        // VHDL zxnext.vhd:6389 — `aymode_i <= nr_06_psg_mode(0)`. It's bit
        // 0 of the 2-bit psg_mode field, NOT equality with "01". That means
        //   00 (YM)       → bit 0=0 → YM
        //   01 (AY)       → bit 0=1 → AY
        //   10 (alias)    → bit 0=0 → YM (duplicate)
        //   11 (hold/rst) → bit 0=1 → AY (but audio_ay_reset fires too)
        const bool ay_mode_bit = (v & 0x01) != 0;
        turbosound_.set_ay_mode(ay_mode_bit);

        // VHDL zxnext.vhd:6379 — `audio_ay_reset <= '1' when reset='1' or
        // nr_06_psg_mode = "11" else '0'`. The turbosound module receives
        // this as its reset line, so a write of psg_mode="11" clears its
        // internal register file + pan/selector state on the NEXT clock
        // edge. Per turbosound.vhd:118-138 the synchronous reset clause
        // clears ONLY ay_select + per-PSG pan; enabled / stereo_mode /
        // mono_mode are external NR-driven inputs (turbosound_en_i,
        // stereo_mode_i, mono_mode_i fed from NR 0x08 b1/b5 + NR 0x09)
        // and MUST survive the audio_ay_reset pulse. Use the partial
        // reset_ay_only() variant to honour that contract (G115).
        if ((v & 0x03) == 0x03) {
            turbosound_.reset_ay_only();
        }

        // VHDL zxnext.vhd:5163 — `nr_06_internal_speaker_beep <= nr_wr_dat(6)`.
        // Composes into `beep_spkr_excl` with NR 0x08 bit 4 (VHDL:6504).
        nr_06_internal_speaker_beep_ = ((v >> 6) & 1) != 0;

        nr_06_button_m1_nmi_en_    = ((v >> 3) & 1) != 0;
        nr_06_button_drive_nmi_en_ = ((v >> 4) & 1) != 0;

        // VHDL zxnext.vhd:5167-5169 — `if nr_03_config_mode = '1' then
        // nr_06_ps2_mode <= nr_wr_dat(2); end if;`. Bit 2 only latches
        // while config_mode is asserted. Outside config_mode the previous
        // ps2_mode is sticky. G56 cluster A — required so the NR 0x06
        // read handler returns the VHDL-faithful value at line 5900.
        if (nextreg_.nr_03_config_mode()) {
            nr_06_ps2_mode_ = ((v >> 2) & 1) != 0;
        }

        // NMI Source Pipeline — Wave C gate wiring. VHDL zxnext.vhd:1110
        // bit 3 = nr_06_button_m1_nmi_en (MF gate); zxnext.vhd:1109 bit 4
        // = nr_06_button_drive_nmi_en (DivMMC gate). Forward both to the
        // central NmiSource so the producer layer can gate MF / DivMMC
        // assertion per VHDL:2090-2091.
        nmi_source_.set_mf_enable((v & 0x08) != 0);
        nmi_source_.set_divmmc_enable((v & 0x10) != 0);

        // F-key FSM (G132) — VHDL zxnext.vhd:6342, :6347 — F3 (`hotkey_5060`)
        // is gated on bit 5, F8 (`hotkey_cpu_speed`) on bit 7. Push both to
        // EmuFnKeys so the side-effect callbacks fire only while the
        // host-visible NR 0x06 enable bits are set.
        emu_fnkeys_.set_nr_06_hotkey_enables(
            /* cpu_speed_en = */ (v & 0x80) != 0,
            /* _5060_en     = */ (v & 0x20) != 0);

        // G110 — refresh Mixer's exc_i gate. NR 0x06 b6 is one of the two
        // inputs to beep_spkr_excl (VHDL zxnext.vhd:6504); audio_mixer.vhd:
        // 80-81 silences EAR/MIC when exc_i='1'.
        mixer_.set_exc_i(beep_spkr_excl());
            return v;
    });

    // VHDL zxnext.vhd:5900 — read formula:
    //   port_253b_dat <= nr_06_hotkey_cpu_speed_en   (bit 7)
    //                  & nr_06_internal_speaker_beep (bit 6)
    //                  & nr_06_hotkey_5060_en        (bit 5)
    //                  & nr_06_button_drive_nmi_en   (bit 4)
    //                  & nr_06_button_m1_nmi_en      (bit 3)
    //                  & nr_06_ps2_mode              (bit 2)
    //                  & nr_06_psg_mode;             (bits 1:0)
    // G56 cluster A — without this handler, reads echo the raw shadow
    // store, which leaks bit-2 writes attempted outside config_mode
    // (VHDL :5167 gates them, but NextReg::write stores the raw byte).
    // Compose from authoritative state for bit 2 (nr_06_ps2_mode_) and
    // fall back to cached(0x06) for bits where the shadow store and the
    // authoritative state are equivalent (every other bit is updated
    // unconditionally on write, no read-side masking).
    nextreg_.set_read_handler(0x06, [this]() -> uint8_t {
        const uint8_t cached = nextreg_.cached(0x06);
        // bits 7, 6, 5, 4, 3, 1, 0 from cached; bit 2 from ps2_mode_.
        uint8_t v = cached & 0xFB;
        if (nr_06_ps2_mode_) v |= 0x04;
        return v;
    });

    // ── F-key FSM side-effect callbacks (G132) ─────────────────────────
    // VHDL zxnext.vhd:5789-5791, :5839-5841, :5849-5852, :5861-5863.
    // Each callback fires once on the rising edge of the corresponding
    // local_fnkey strobe (i.e. on entering MF_DONE) AND only if the
    // corresponding NR 0x06 gate bit is set (handled inside EmuFnKeys
    // for F3/F8). F1/F4 reset and F9/F10 NMI dispatch are owned by the
    // existing on_hotkey_f*() handlers (G152) — EmuFnKeys does not
    // re-dispatch those. F5/F6 (expbus enable/disable, G147) are
    // out-of-scope for G132.

    // F2 — toggle NR 0x05 bit 0 (`nr_05_scandouble_en`, VHDL :5849-5852).
    emu_fnkeys_.set_f2_scandouble_callback([this]() {
        const uint8_t cur = nextreg_.cached(0x05);
        nextreg_.write(0x05, cur ^ 0x01);
    });
    // F3 — toggle NR 0x05 bit 2 (`nr_05_5060`, VHDL :5839-5841).
    // Pentagon (machine_timing(2) = '1') forces 50 Hz unconditionally
    // (VHDL :5836); honour that gate so the F3 hotkey is inert in
    // Pentagon mode.
    emu_fnkeys_.set_f3_5060_callback([this]() {
        if ((nextreg_.nr_03_machine_timing() & 0x04) != 0) {
            return;  // Pentagon: nr_05_5060 hard-wired '0' (VHDL :5836)
        }
        const uint8_t cur = nextreg_.cached(0x05);
        nextreg_.write(0x05, cur ^ 0x04);
    });
    // F7 — increment NR 0x09 bits 1:0 (`nr_09_scanlines`, VHDL :5861-5863).
    emu_fnkeys_.set_f7_scanlines_callback([this]() {
        const uint8_t cur = nextreg_.cached(0x09);
        const uint8_t inc = static_cast<uint8_t>((cur & 0x03) + 1) & 0x03;
        nextreg_.write(0x09, static_cast<uint8_t>((cur & 0xFC) | inc));
    });
    // F8 — increment NR 0x07 bits 1:0 (`nr_07_cpu_speed`, VHDL :5789-5791).
    emu_fnkeys_.set_f8_cpu_speed_callback([this]() {
        const uint8_t cur = nextreg_.cached(0x07);
        const uint8_t inc = static_cast<uint8_t>((cur & 0x03) + 1) & 0x03;
        nextreg_.write(0x07, static_cast<uint8_t>((cur & 0xFC) | inc));
    });

    // Push initial NR 0x06 hotkey-enable snapshot to the FSM. The reset
    // default of NR 0x06 is 0xA0 (zxnext.vhd:1107-1108) — bits 7 and 5
    // both set, so both the F3 and F8 gates start enabled.
    {
        const uint8_t nr06 = nextreg_.cached(0x06);
        emu_fnkeys_.set_nr_06_hotkey_enables(
            (nr06 & 0x80) != 0, (nr06 & 0x20) != 0);
    }

    // Register 0x81: Expansion bus control — Wave C (TASK-NMI-SOURCE-PIPELINE-PLAN).
    //   bit 6 = expbus_ula_override         (zxnext.vhd:1223; stored, inert —
    //                                        expansion bus not emulated)
    //   bit 5 = expbus_nmi_debounce_disable (zxnext.vhd:1222) — bypass the
    //           ExpBus debounce path so /NMI asserts immediately when the
    //           pin drops. Forwarded to NmiSource.
    //   bit 4 = expbus_clken                (zxnext.vhd:1224; stored, inert)
    //   bit 3 = expbus_fdc                  (zxnext.vhd:1225; stored, inert)
    //   bits 1:0 = expbus_speed[1:0]        (zxnext.vhd:1226-1227; stored, inert)
    //
    // The inert bits are captured in `nr_81_` for VHDL-faithful read-back
    // once an ExpBus emulation arrives; no consumers today.
    // VHDL zxnext.vhd:6125 — port_253b_dat <= i_BUS_ROMCS_n &
    //   nr_81_expbus_ula_override & nr_81_expbus_nmi_debounce_disable &
    //   nr_81_expbus_clken & nr_81_expbus_fdc & '0' & nr_81_expbus_speed.
    // Bit 7 is the i_BUS_ROMCS_n input from the expansion bus — with no
    // expbus device wired in jnext today (G45 tracks expansion-bus emul),
    // ROMCS_n is hard-deasserted i.e. constant '1'.  Bit 2 is constant '0'.
    // Bits 6, 5, 4, 3, 1, 0 echo the corresponding stored fields from the
    // last NR 0x81 write (cached in `nr_81_`).  G56 Phase 2 (cluster E):
    // KEEP the read_handler — bit 7 is a runtime hardware-input signal
    // (not a static mask of the cached byte) and the cold-init read must
    // return 0x80 even with no prior write (regs_[0x81] starts at 0).
    nr_81_ = 0;
    nextreg_.set_write_handler(0x81, [this](uint8_t v) -> uint8_t {
        nr_81_ = v;
        nmi_source_.set_expbus_debounce_disable((v & 0x20) != 0);
        return v;
    });
    nextreg_.set_read_handler(0x81, [this]() -> uint8_t {
        return static_cast<uint8_t>(0x80 | (nr_81_ & 0x7B));
    });

    // Register 0x80: Expansion bus configuration — VHDL zxnext.vhd:6122
    //   port_253b_dat <= nr_80_expbus.  Full 8-bit field, no masking.
    // No expansion-bus device is wired in jnext today (G45 tracks expbus
    // emulation), so the byte is purely write-storage and round-trips
    // unchanged.  G56 Phase 2 (cluster E): no handler at all — NextReg
    // falls through to the raw stored byte, which IS the VHDL formula.

    // Register 0x08: Peripheral 3
    //   bit 7 = unlock 128K paging (one-shot: write 1 clears port_7ffd_reg(5))
    //   bit 6 = contention disable (VHDL zxnext.vhd:5176 nr_08_contention_disable)
    //   bit 5 = stereo mode (0=ABC, 1=ACB)
    //   bit 3 = DAC enable
    //   bit 1 = TurboSound enable
    // VHDL zxnext.vhd:3654-3656 — nr_08_we=1 AND nr_wr_dat(7)=1 clears
    // port_7ffd_reg(5), which drops port_7ffd_locked (zxnext.vhd:3769).
    // Bit 7 is the write-strobe only; it is not stored (read-back at
    // zxnext.vhd:5906 shows bit 7 = NOT port_7ffd_locked, not the bit
    // just written). Bit 6 IS stored (read back via eff_nr_08_contention_disable).
    nextreg_.set_write_handler(0x08, [this](uint8_t v) -> uint8_t {
        if (v & 0x80) mmu_.unlock_paging();
        mmu_.set_contention_disabled((v >> 6) & 1);
        // Mirror NR 0x08 bit 6 into the ContentionModel SHADOW only
        // (zxnext.vhd:1380 default '0', zxnext.vhd:5800-5805 stored on
        // write into nr_08_contention_disable). The VHDL latches
        // eff_nr_08_contention_disable from this shadow only on CPU
        // bus-idle cycles where hc(8)='1' (zxnext.vhd:5822-5823) — a
        // mid-line write does NOT alter the contention gate
        // combinatorially. The shadow → effective promotion is driven
        // by Emulator::run_frame() calling
        // contention_.commit_contention_disable_on_hc(current_hc())
        // once per instruction; that places the commit anywhere the
        // CPU lands inside [hc=256..455] and matches the VHDL latch
        // window for jnext's per-instruction tick granularity.
        contention_.set_contention_disable_shadow(((v >> 6) & 1) != 0);
        turbosound_.set_stereo_mode((v >> 5) & 1);
        // VHDL soundrive.vhd:69-78 + zxnext.vhd:6436:
        //   soundrive.reset_i <= reset OR NOT nr_08_dac_en;
        // While nr_08_dac_en='0' the four DAC channels are held at 0x80
        // (DC-silence midpoint). On a 1->0 transition flip channels back
        // to silence so residual non-silent levels do not persist.
        const bool dac_was_enabled = dac_enabled_;
        dac_enabled_ = (v >> 3) & 1;
        if (dac_was_enabled && !dac_enabled_) {
            dac_.reset();
        }
        turbosound_.set_enabled((v >> 1) & 1);
        // Mirror the stored bits (5/4/3/2/1/0) for NR 0x08 read-back per
        // VHDL zxnext.vhd:5906. Bit 7 is write-strobe-only (not stored),
        // and bit 6 is kept live on mmu_ (contention_disabled_). The
        // remaining bits are composed from this cache so read matches the
        // last write for those signals the emulator does not drive from
        // live subsystem state (internal speaker bit 4, port_ff_rd bit 2,
        // keyboard issue2 bit 0).
        nr_08_stored_low_ = v & 0x3F;

        // G110 — refresh Mixer's exc_i gate. NR 0x08 b4 is the other input
        // to beep_spkr_excl (VHDL zxnext.vhd:6504); audio_mixer.vhd:80-81
        // silences EAR/MIC when exc_i='1'.
        mixer_.set_exc_i(beep_spkr_excl());
        return v;
    });

    // Register 0x08 read-back composition per VHDL zxnext.vhd:5906:
    //   port_253b_dat <= (not port_7ffd_locked) & eff_nr_08_contention_disable &
    //                    nr_08_psg_stereo_mode & nr_08_internal_speaker_en &
    //                    nr_08_dac_en & nr_08_port_ff_rd_en &
    //                    nr_08_psg_turbosound_en & nr_08_keyboard_issue2;
    // Bit 7 is derived live from the paging lock; bit 6 from mmu_'s
    // contention_disabled_ (which is also what the write handler drives).
    // Bits 5..0 are served from the last-write mirror nr_08_stored_low_.
    nextreg_.set_read_handler(0x08, [this]() -> uint8_t {
        uint8_t v = 0;
        if (!mmu_.paging_locked())        v |= 0x80;
        if (mmu_.contention_disabled())   v |= 0x40;
        v |= nr_08_stored_low_ & 0x3F;
        return v;
    });

    // Register 0x09: Peripheral 4
    //   bits 7:5 = per-chip mono mode (bit 7=AY#2, 6=AY#1, 5=AY#0)
    //   bit 3 = sprites over border + DivMMC mapram-latch clear
    //           (VHDL zxnext.vhd:4184-4185 — writes bit 3=1 force
    //            port_e3_reg(6) := '0')
    nextreg_.set_write_handler(0x09, [this](uint8_t v) -> uint8_t {
        sprites_.set_over_border((v & 0x08) != 0);
        // G95: bit 4 wires nr_09_sprite_tie into the sprite mirror unit
        // (VHDL zxnext.vhd:5187 / 4352, sprites.vhd:60,594-612).
        sprites_.set_mirror_tie((v & 0x10) != 0);
        // E3-05: clear DivMMC mapram OR-latch when bit 3 is set.
        if (v & 0x08) {
            divmmc_.clear_mapram();
        }
        // Mono mode: bit 7=AY#2, bit 6=AY#1, bit 5=AY#0
        // Map to TurboSound: bit 0=AY#0, bit 1=AY#1, bit 2=AY#2
        uint8_t mono = 0;
        if (v & 0x20) mono |= 0x01;  // AY#0
        if (v & 0x40) mono |= 0x02;  // AY#1
        if (v & 0x80) mono |= 0x04;  // AY#2
        turbosound_.set_mono_mode(mono);
        return v;
    });

    // Register 0x2C/0x2D/0x2E — Soundrive NextREG mirror writes.
    // VHDL zxnext.vhd:4852-4854 generate nr_2c_we / nr_2d_we / nr_2e_we
    // per write; zxnext.vhd:6452-6454 pass these into soundrive_mod with
    //   nr_left_we_i  = nr_2c_we  → chB write (VHDL soundrive.vhd:90-94)
    //   nr_mono_we_i  = nr_2d_we  → chA+chD write (VHDL soundrive.vhd:85-89)
    //   nr_right_we_i = nr_2e_we  → chC write (VHDL soundrive.vhd:95-97)
    // Gated on the Soundrive reset which fires while `nr_08_dac_en='0'`
    // (VHDL zxnext.vhd:6436) — when the DAC is off the internal channel
    // registers are held at 0x80 (silence). Mirror that behaviour here.
    nextreg_.set_write_handler(0x2C, [this](uint8_t v) -> uint8_t {
        if (dac_enabled_) dac_.write_left(v);
        return v;
    });
    nextreg_.set_write_handler(0x2D, [this](uint8_t v) -> uint8_t {
        if (dac_enabled_) dac_.write_mono(v);
        return v;
    });
    nextreg_.set_write_handler(0x2E, [this](uint8_t v) -> uint8_t {
        if (dac_enabled_) dac_.write_right(v);
        return v;
    });

    // G112: NR 0x2C/0x2D/0x2E read handlers — Pi I2S input.
    // VHDL zxnext.vhd:6006-6015:
    //   NR 0x2C read → port_253b_dat <= pi_audio_L(9 downto 2);
    //                  nr_2d_i2s_sample <= pi_audio_L(1 downto 0);
    //   NR 0x2E read → port_253b_dat <= pi_audio_R(9 downto 2);
    //                  nr_2d_i2s_sample <= pi_audio_R(1 downto 0);
    //   NR 0x2D read → port_253b_dat <= nr_2d_i2s_sample & "000000";
    // Mutating nr_2d_i2s_sample_ from inside the read lambda is the
    // intended VHDL semantic — the latch updates ON read of NR 0x2C/0x2E.
    nextreg_.set_read_handler(0x2C, [this]() -> uint8_t {
        // G113: read the GATED pi_audio_L (10-bit), not the raw I2s
        // latch — VHDL zxnext.vhd:6007 reads `pi_audio_L`, which already
        // applies the NR 0xA2 enable/mute/ear/cross-channel-mux gate at
        // zxnext.vhd:2358. When NR 0xA2 disables I2S, pi_audio_L = 0x200
        // (silence midpoint), so the high-8 bits = 0x80 and the latched
        // low-2 bits = 0b00.
        const uint16_t L = i2s_.pi_audio_L() & 0x3FF;
        nr_2d_i2s_sample_ = static_cast<uint8_t>((L & 0x03) << 6);  // bits [1:0] → byte [7:6]
        return static_cast<uint8_t>((L >> 2) & 0xFF);               // bits [9:2] → byte [7:0]
    });
    nextreg_.set_read_handler(0x2E, [this]() -> uint8_t {
        // G113: read the GATED pi_audio_R per VHDL zxnext.vhd:6014.
        const uint16_t R = i2s_.pi_audio_R() & 0x3FF;
        nr_2d_i2s_sample_ = static_cast<uint8_t>((R & 0x03) << 6);
        return static_cast<uint8_t>((R >> 2) & 0xFF);
    });
    nextreg_.set_read_handler(0x2D, [this]() -> uint8_t {
        return nr_2d_i2s_sample_;  // already in [7:6] form, low 6 bits = 0
    });

    // G113 — NR 0xA2 Pi I2S control byte.
    // VHDL zxnext.vhd:5564 stores the raw 8 bits in nr_a2_pi_i2s_ctl;
    // zxnext.vhd:2283-2290 fans them out to pi_i2s_en[L/R], inout,
    // muteL/R, ear. The I2s class consumes those bits when computing
    // pi_audio_L/R() per zxnext.vhd:2358-2359.
    nextreg_.set_write_handler(0xA2, [this](uint8_t v) -> uint8_t {
        i2s_.set_nr_a2_ctl(v);
        return v;
    });
    // VHDL zxnext.vhd:6192:
    //   port_253b_dat <= nr_a2_pi_i2s_ctl(7 downto 6) & '0'
    //                    & nr_a2_pi_i2s_ctl(4 downto 2) & '1'
    //                    & nr_a2_pi_i2s_ctl(0);
    // Pass-through bits = b7,b6,b4,b3,b2,b0 (mask 0xDD); fixed b5=0,
    // fixed b1=1 (constant 0x02). Verify: c=0x00 → 0x02; c=0xFF → 0xDF;
    // c=0x55 (0101 0101) → (0x55 & 0xDD) | 0x02 = 0x55 | 0x02 = 0x57.
    nextreg_.set_read_handler(0xA2, [this]() -> uint8_t {
        const uint8_t c = i2s_.nr_a2_ctl();
        return static_cast<uint8_t>((c & 0xDD) | 0x02);
    });

    // G135 — NR 0xA0 Pi peripheral enable byte.
    // VHDL zxnext.vhd:5560-5561: stores raw 8 bits in nr_a0_pi_peripheral_en
    // on any NR 0xA0 write. zxnext.vhd:2278-2281 fans out:
    //   bit 5 → pi_uart_rxtx, bit 4 → pi_uart_en,
    //   bit 3 → pi_i2c1_en,   bit 0 → pi_spi0_en.
    // Reset 0x00 (zxnext.vhd:5080).
    nextreg_.set_write_handler(0xA0, [this](uint8_t v) -> uint8_t {
        nr_a0_pi_peripheral_en_ = v;
        // G138 — fan out bit 3 (pi_i2c1_en) into the I2C controller so
        // its read_scl/read_sda gate the Pi-side wired-AND inputs per
        // VHDL zxnext.vhd:2317-2318. Other bits (5/4 pi_uart, 0 pi_spi0)
        // are stored only — UART1 / SPI0 GPIO mux is not yet modelled.
        i2c_.set_pi_i2c1_en((v & 0x08) != 0);
        return v;
    });
    // VHDL zxnext.vhd:6188-6189:
    //   port_253b_dat <= "00" & nr_a0_pi_peripheral_en(5 downto 3) & "00"
    //                    & nr_a0_pi_peripheral_en(0);
    // Pass-through bits = 5,4,3,0 (mask 0x39); fixed zero bits = 7,6,2,1.
    // Verify: c=0x00 → 0x00; c=0xFF → 0x39; c=0xA5 (1010 0101) →
    //   (0xA5 & 0x39) = 0x21.
    nextreg_.set_read_handler(0xA0, [this]() -> uint8_t {
        return static_cast<uint8_t>(nr_a0_pi_peripheral_en_ & 0x39);
    });

    // --- Phase 5 peripheral port handlers ---

    // CTC channels 0-3: VHDL zxnext.vhd:2690 — cpu_a(15:11)="00011"
    // plus LSB = 0x3B. Covers 0x183B..0x1F3B; channel from bits 9:8.
    port_.register_handler(0xF8FF, 0x183B,
        [this](uint16_t p) -> uint8_t {
            return ctc_.read((p >> 8) & 3);
        },
        [this](uint16_t p, uint8_t val) {
            ctc_.write((p >> 8) & 3, val);
        });

    // DMA — port 0x6B (ZXN mode) and port 0x0B (Z80-DMA compat).
    // VHDL zxnext.vhd: port_dma_rd/wr <= port_dma_rd_raw/wr_raw AND NOT
    // dma_holds_bus — the DMA port is gated at the dispatcher layer while
    // the DMA controller holds the bus.  Matches DMA plan row 15.8.
    port_.register_handler(0x00FF, 0x006B,
        [this](uint16_t) -> uint8_t {
            return dma_.dma_holds_bus() ? 0xFF : dma_.read();
        },
        [this](uint16_t, uint8_t val) {
            if (!dma_.dma_holds_bus()) dma_.write(val, false);
        });
    port_.register_handler(0x00FF, 0x000B,
        [this](uint16_t) -> uint8_t {
            return dma_.dma_holds_bus() ? 0xFF : dma_.read();
        },
        [this](uint16_t, uint8_t val) {
            if (!dma_.dma_holds_bus()) dma_.write(val, true);
        });

    // SPI chip select (0xE7) and data (0xEB)
    port_.register_handler(0x00FF, 0x00E7,
        [this](uint16_t) -> uint8_t { return spi_.read_cs(); },
        [this](uint16_t, uint8_t val) { spi_.write_cs(val); });
    port_.register_handler(0x00FF, 0x00EB,
        [this](uint16_t) -> uint8_t { return spi_.read_data(); },
        [this](uint16_t, uint8_t val) { spi_.write_data(val); });

    // I2C SCL (0x103B) and SDA (0x113B)
    // VHDL zxnext.vhd:2418: port_i2c_io_en <= internal_port_enable(10).
    // internal_port_enable(10) = nr_83_internal_port_enable(2) per the
    // concat at zxnext.vhd:2392 (bits 0-7 = NR 0x82, bits 8-15 = NR 0x83).
    // Gate mirrors the DivMMC pattern below: when closed, reads return 0xFF
    // and writes are silently dropped.
    port_.register_handler(0xFFFF, 0x103B,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x83) & 0x04) == 0) return 0xFF;
            return i2c_.read_scl();
        },
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x83) & 0x04) == 0) return;
            i2c_.write_scl(val);
        });
    port_.register_handler(0xFFFF, 0x113B,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x83) & 0x04) == 0) return 0xFF;
            return i2c_.read_sda();
        },
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x83) & 0x04) == 0) return;
            i2c_.write_sda(val);
        });

    // UART: Tx (0x133B), Rx (0x143B), Select (0x153B), Frame (0x163B)
    // VHDL zxnext.vhd:2420: port_uart_io_en <= internal_port_enable(12).
    // internal_port_enable(12) = nr_83_internal_port_enable(4) per the
    // concat at zxnext.vhd:2392. Same gate pattern as I2C/DivMMC.
    port_.register_handler(0xFFFF, 0x133B,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x83) & 0x10) == 0) return 0xFF;
            return uart_.read(3);
        },
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x83) & 0x10) == 0) return;
            uart_.write(3, val);
        });
    port_.register_handler(0xFFFF, 0x143B,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x83) & 0x10) == 0) return 0xFF;
            return uart_.read(0);
        },
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x83) & 0x10) == 0) return;
            uart_.write(0, val);
        });
    port_.register_handler(0xFFFF, 0x153B,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x83) & 0x10) == 0) return 0xFF;
            return uart_.read(1);
        },
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x83) & 0x10) == 0) return;
            uart_.write(1, val);
        });
    port_.register_handler(0xFFFF, 0x163B,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x83) & 0x10) == 0) return 0xFF;
            return uart_.read(2);
        },
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x83) & 0x10) == 0) return;
            uart_.write(2, val);
        });

    // DivMMC control — port 0xE3.
    // VHDL zxnext.vhd:2412: port_divmmc_io_en <= internal_port_enable(8) = NR 0x83 bit 0.
    port_.register_handler(0x00FF, 0x00E3,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x83) & 0x01) == 0) return 0xFF;
            return divmmc_.read_control();
        },
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x83) & 0x01) == 0) return;
            divmmc_.write_control(val);
        });

    // Kempston joystick 1 (0x001F) and 2 (0x0037). VHDL zxnext.vhd:2674-2675.
    // Phase 1 scaffold (Task 3 Input): port reads delegate to the new
    // Joystick class. Phase 1 stubs return 0x00 (Kempston "no buttons");
    // Agent B (Phase 2) replaces Joystick::read_port_1f/37 with the
    // VHDL-faithful mux per zxnext.vhd:3470-3494.
    //
    // Kempston 1 (port 0x001F) is gated by NR 0x82 bit 6 — VHDL
    // zxnext.vhd:2392-2407 maps port_1f_io_en = internal_port_enable(6),
    // whose low byte is nr_82_internal_port_enable (expbus disabled).
    // Reset default nr_82_internal_port_enable = 0xFF (zxnext.vhd:1226,
    // 5054), so bit 6 = 1 and Kempston responds at power-on. When the
    // firmware clears bit 6 (OUT NR 0x82, 0xBF), the port decodes as
    // unhandled and the floating bus default (0xFF) must be returned.
    //
    // G129 — additional mode-conditional hw_en gate per VHDL :2674.
    // `port_1f` decodes only when port_1f_io_en = '1' AND port_1f_hw_en
    // = '1'. The hw_en signal (zxnext.vhd:2454) is true ONLY when at
    // least one connector is in Kempston1 (001) or Md3Left (101). With
    // both joys in Sinclair2/Cursor/etc. the port must un-decode to
    // floating-bus 0xFF even if NR 0x82 b6 is set.
    port_.register_handler(0x00FF, 0x001F,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x82) & 0x40) == 0) return 0xFF;
            if (!joystick_.port_1f_hw_en())            return 0xFF;
            return joystick_.read_port_1f();
        },
        nullptr);
    // Kempston 2 (port 0x0037) is gated by TWO independent signals — both
    // must hold for the port to decode:
    //   * NR 0x82 bit 7 io_en (G128) — VHDL zxnext.vhd:2408 + 2675:
    //     port_37_io_en <= internal_port_enable(7). Reset default 0xFF
    //     (zxnext.vhd:1226, 5054); firmware clears bit 7 via OUT NR 0x82,
    //     0x7F. Mirror of the NR 0x82 bit-6 / port-0x001F gate above.
    //   * port_37_hw_en mode-conditional (G129) — VHDL zxnext.vhd:2455:
    //     port_37_hw_en <= joyL_37_en or joyR_37_en, true ONLY when at
    //     least one connector is in Kempston2 (100) or Md3Right (110).
    //     With both joys in Sinclair2/Cursor/etc. the port un-decodes to
    //     floating-bus 0xFF.
    port_.register_handler(0x00FF, 0x0037,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x82) & 0x80) == 0) return 0xFF;
            if (!joystick_.port_37_hw_en())            return 0xFF;
            return joystick_.read_port_37();
        },
        nullptr);

    // Kempston mouse: buttons (0xFADF), X (0xFBDF), Y (0xFFDF).
    // VHDL zxnext.vhd:2668-2670, :3541-3562. Phase 2 Agent H wires the
    // real composition via KempstonMouse: wheel nibble + fixed bit 3 +
    // active-low buttons on 0xFADF; raw X / Y registers on 0xFBDF / 0xFFDF.
    //
    // Enable gate: VHDL zxnext.vhd:2668-2670 qualifies all three ports
    // with port_mouse_io_en = internal_port_enable(13) = NR 0x83 bit 5
    // (zxnext.vhd:2422, 2392-2393). Reset default nr_83_internal_port_enable
    // = 0xFF (zxnext.vhd:1227, 5055), so bit 5 = 1 and mouse responds at
    // power-on. When firmware clears bit 5 (OUT NR 0x83, 0xDF), the ports
    // decode as unhandled and the floating-bus default (0xFF) is returned.
    // Mirror of the NR 0x82 bit-6 / port-0x001F gate above.
    port_.register_handler(0xFFFF, 0xFADF,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x83) & 0x20) == 0) return 0xFF;
            return mouse_.read_port_fadf();
        },
        nullptr);
    port_.register_handler(0xFFFF, 0xFBDF,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x83) & 0x20) == 0) return 0xFF;
            return mouse_.read_port_fbdf();
        },
        nullptr);
    port_.register_handler(0xFFFF, 0xFFDF,
        [this](uint16_t) -> uint8_t {
            if ((nextreg_.cached(0x83) & 0x20) == 0) return 0xFF;
            return mouse_.read_port_ffdf();
        },
        nullptr);

    // ULA+ register select (0xBF3B) and data (0xFF3B). VHDL zxnext.vhd:4525-4554.
    //
    // 0xBF3B write:
    //   port_bf3b_ulap_mode <= cpu_do(7:6)   -- zxnext.vhd:4532
    //   if cpu_do(7:6) = "00" then
    //     port_bf3b_ulap_index <= cpu_do(5:0) -- zxnext.vhd:4533-4535
    //
    // The top 2 bits (`ulap_mode`) are Ula-side state because the 0xFF3B
    // enable latch is gated on them. The low 6 bits (`ulap_index`) are
    // Compositor/PaletteManager state (palette-entry select for palette
    // writes via NR stream) and are still a stub here.
    //
    // Wave C — 2026-04-23 — now gates 0xFF3B write on ulap_mode = "01" per
    // zxnext.vhd:4548: `port_ff3b_ulap_en <= cpu_do(0)` ONLY when
    // port_bf3b_ulap_mode = "01"; any other mode-group is a palette read
    // (mode "00") or a no-op for the enable latch. The Phase-1 scaffold
    // previously wrote the enable unconditionally, which the Phase 1 critic
    // flagged as VHDL-faithless.
    //
    // 0xFF3B read remains a stub (the VHDL read path at zxnext.vhd:4556-4569
    // is write-only-latch plus palette-readback; no current consumer needs
    // read semantics).
    port_.register_handler(0xFFFF, 0xBF3B,
        [](uint16_t) -> uint8_t { return 0x00; },
        [this](uint16_t, uint8_t v) {
            // VHDL zxnext.vhd:4532 — port_bf3b_ulap_mode <= cpu_do(7:6)
            // is unconditional on every bf3b write.
            renderer_.ula().set_ulap_mode(static_cast<uint8_t>((v >> 6) & 0x03));
            // VHDL zxnext.vhd:4533-4535 — the index latch only updates
            // when cpu_do(7:6) = "00" (palette-index mode).  The index is
            // consumed by the NR 0xFF palette-poke side-channel
            // (zxnext.vhd:6957-6958) and by ULA+ palette readback.
            if (((v >> 6) & 0x03) == 0x00) {
                renderer_.ula().set_ulap_index(static_cast<uint8_t>(v & 0x3F));
            }
        });
    port_.register_handler(0xFFFF, 0xFF3B,
        [](uint16_t) -> uint8_t { return 0x00; },
        [this](uint16_t, uint8_t v) {
            // Gate on ulap_mode = "01" per VHDL zxnext.vhd:4548.
            if (renderer_.ula().get_ulap_mode() == 0x01) {
                renderer_.ula().set_ulap_en((v & 0x01) != 0);
            }
        });

    // --- Magic Port (debug output) ---
    if (cfg.magic_port_enabled) {
        Log::emulator()->info("Magic port enabled at {:#06x}, mode={}",
                              cfg.magic_port_address,
                              cfg.magic_port_mode == EmulatorConfig::MagicPortMode::HEX ? "hex" :
                              cfg.magic_port_mode == EmulatorConfig::MagicPortMode::DEC ? "dec" :
                              cfg.magic_port_mode == EmulatorConfig::MagicPortMode::ASCII ? "ascii" : "line");
        port_.register_handler(0xFFFF, cfg.magic_port_address,
            nullptr,  // reads return 0xFF (default)
            [mode = cfg.magic_port_mode](uint16_t, uint8_t val) {
                switch (mode) {
                    case EmulatorConfig::MagicPortMode::HEX:
                        fprintf(stderr, "%02X\n", val);
                        break;
                    case EmulatorConfig::MagicPortMode::DEC:
                        fprintf(stderr, "%d\n", val);
                        break;
                    case EmulatorConfig::MagicPortMode::ASCII:
                        fputc(val, stderr);
                        break;
                    case EmulatorConfig::MagicPortMode::LINE: {
                        // Buffer until CR or LF, then output full line
                        static std::string line_buf;
                        if (val == '\r' || val == '\n') {
                            if (!line_buf.empty()) {
                                fprintf(stderr, "%s\n", line_buf.c_str());
                                line_buf.clear();
                            }
                        } else {
                            line_buf += static_cast<char>(val);
                        }
                        break;
                    }
                }
            });
    }

    // --- Phase 5 DMA memory/IO callbacks ---

    dma_.read_memory  = [this](uint16_t addr) -> uint8_t { return mmu_.read(addr); };
    dma_.write_memory = [this](uint16_t addr, uint8_t val) { mmu_.write(addr, val); };
    dma_.read_io      = [this](uint16_t port) -> uint8_t { return port_.read(port); };
    dma_.write_io     = [this](uint16_t port, uint8_t val) { port_.write(port, val); };

    // --- Phase 5 IM2 interrupt wiring ---
    //
    // Phase 2 Wave 2 (Agent E): migrated from legacy raise(Im2Level::*) to
    // DevIdx-based raise_req() so peripherals drive the real im2_peripheral
    // wrapper (edge detect + im2_int_req latch). The callback indirection is
    // preserved; only the target enum + API are updated.

    ctc_.on_interrupt = [this](int channel) {
        // Agent E: route CTC ZC/TO through the DevIdx-based IM2 fabric so
        // the wrapper/edge-detect layer (Agent D) and device state machine
        // (Agent B) see the interrupt request properly.
        if (channel >= 0 && channel < 4) {
            im2_.raise_req(static_cast<Im2Controller::DevIdx>(
                static_cast<int>(Im2Controller::DevIdx::CTC0) + channel));
        }
    };

    // Phase 2 Wave 2 Agent E (Input/IOMODE): route the unconditional
    // ctc_zc_to(3) pulse to the IoMode pin-7 toggle. Distinct from
    // on_interrupt because VHDL zxnext.vhd:3522 gates the toggle on the
    // raw `ctc_zc_to(3)` signal, NOT on the CTC ch3 IRQ enable bit. The
    // IoMode class internally checks the current NR 0x0B mode and only
    // acts under mode "01" with the iomode_0/pin7 guard.
    ctc_.on_zc_to = [this](int channel) {
        if (channel == 3) {
            iomode_.tick_ctc_zc3();
        }
    };

    // DMA is a "victim" of INT in the VHDL model (zxnext.vhd:2003-2008), not
    // a priority-chain source. Wave 3 Agent F: NR 0xCC/CD/CE now drive
    // Im2Controller::set_dma_int_en_mask (see the NR handlers above), and
    // dma_int_pending()/dma_delay() feed the per-frame hook in run_frame()
    // below. The legacy raise(Im2Level::DMA) is left in place as a vestigial
    // save/load-schema artifact — Im2Level::DMA maps to DevIdx::ULA via
    // to_devidx() (harmless because the legacy API path uses its own pending
    // view keyed by Im2Level index) and the legacy mask bit is unused by any
    // live code path. Candidate for removal in a future cleanup once every
    // save-state reader is rebuilt.
    dma_.on_interrupt = [this]() {
        im2_.raise(Im2Level::DMA);
    };

    uart_.on_tx_interrupt = [this](int channel) {
        im2_.raise_req(channel == 0 ? Im2Controller::DevIdx::UART0_TX
                                    : Im2Controller::DevIdx::UART1_TX);
    };

    uart_.on_rx_interrupt = [this](int channel) {
        // G134 — VHDL zxnext.vhd:1941-1944 RX request shape:
        //   uartN_rx_near_full OR (uartN_rx_avail AND NOT nr_c6_int_en_2_*(1))
        // The "near-full" path is unconditional; the per-byte avail path
        // must be masked off when NR 0xC6 bit 1 (UART0) or bit 5 (UART1)
        // is set. Without this gate every RX byte raised an int_req, even
        // when the int_en path was set up to fire only on near-full.
        const auto& ch = uart_.channel(channel);
        const bool near_full = ch.rx_near_full();
        const uint8_t mask_bit = (channel == 0) ? 0x02 : 0x20;  // C6[1] / C6[5]
        const bool avail_masked = (nr_c6_uart_int_en_ & mask_bit) != 0;
        if (!near_full && avail_masked) {
            return;  // request shape suppressed: avail blocked, near-full not yet
        }
        im2_.raise_req(channel == 0 ? Im2Controller::DevIdx::UART0_RX
                                    : Im2Controller::DevIdx::UART1_RX);
    };

    // --- Phase 5 DivMMC overlay + I2C RTC + SD card ---

    mmu_.set_divmmc(&divmmc_);
    // Wave 1 E — wire MF memory overlay into the Mmu cascade. Per VHDL
    // zxnext.vhd:2937, MF sits between bootrom and DivMMC. Mmu::read/write
    // gate on multiface_->is_mem_active() (= mf_enable_eff) and the
    // cpu_a(15:14)='00' window to honour the SRAM arbiter at :3028-3035.
    mmu_.set_multiface(&multiface_);
    mmu_.set_debug_state(&debug_state_);
    i2c_.attach_device(0x68, &rtc_);
    spi_.attach_device(0, &sd_card_);  // SD card on CS0

    // --- ROM loading ---

    // Wave 0.3 (Task 8 Multiface plan, 2026-05-04): all machine ROMs come
    // from the user-supplied SD-card image, the same way real ZX Spectrum
    // Next hardware does. The TBBlue distribution image keeps every
    // machine ROM under /MACHINES/NEXT/ at canonical names:
    //
    //   48K       /MACHINES/NEXT/48.rom     (16 KB)             -> bank 0
    //   128K      /MACHINES/NEXT/128.rom    (32 KB, combined)   -> bank 0+1
    //   +3        /MACHINES/NEXT/plus3.rom  (64 KB, combined)   -> banks 0..3
    //   Next      /MACHINES/NEXT/48.rom     (fallback for non-NextZXOS mode)
    //
    // Skipped on soft reset (preserve_memory=true) — the rom_ buffer and
    // the Next ROM-in-SRAM window already hold whatever tbblue.fw just
    // installed, and reloading from SD would overwrite NextZXOS with the
    // default 48.rom.
    //
    // Skipped also when cfg.sd_card_image is empty — unit-test fixtures
    // that build an Emulator directly without an SD image legitimately
    // run with no ROM contents (they exercise registers / paging and
    // never execute ROM code). Aligns with the boot-ROM auto-load gate
    // semantics one block down.
    if (!preserve_memory && !cfg.sd_card_image.empty()) {
        auto load_machine_rom = [&](const char* sd_path,
                                    int total_banks,
                                    size_t expected_size,
                                    const char* desc) -> bool {
            std::vector<uint8_t> bytes;
            if (!extract_sd_rom(cfg.sd_card_image, sd_path, bytes)) {
                Log::emulator()->warn("could not extract '{}' from SD image '{}' — {} will not boot",
                                      sd_path, cfg.sd_card_image, desc);
                return false;
            }
            if (bytes.size() < expected_size) {
                Log::emulator()->warn("SD ROM '{}' short ({} bytes, expected {}) — {} will not boot",
                                      sd_path, bytes.size(), expected_size, desc);
                return false;
            }
            for (int b = 0; b < total_banks; ++b) {
                if (!rom_.load_bytes(b, bytes.data() + b * 0x4000, 0x4000)) {
                    Log::emulator()->warn("could not load bank {} from SD ROM '{}' — {} will not boot",
                                          b, sd_path, desc);
                    return false;
                }
            }
            Log::emulator()->info("Machine ROM loaded from SD '{}' ({} bytes -> {} banks): {}",
                                  sd_path, bytes.size(), total_banks, desc);
            return true;
        };

        switch (cfg.type) {
            case MachineType::ZX48K:
                load_machine_rom("/MACHINES/NEXT/48.rom", 1, 0x4000, "48K BASIC");
                break;

            case MachineType::ZX128K:
                load_machine_rom("/MACHINES/NEXT/128.rom", 2, 0x8000, "128K BASIC");
                break;

            case MachineType::ZX_PLUS3:
                load_machine_rom("/MACHINES/NEXT/plus3.rom", 4, 0x10000, "+3 BASIC");
                break;

            case MachineType::ZXN_ISSUE2:
            default:
                load_machine_rom("/MACHINES/NEXT/48.rom", 1, 0x4000, "Next 48K fallback");
                break;
        }
        Log::emulator()->info("Machine type: {} (ROMs from SD '{}')",
                              machine_type_str(cfg.type), cfg.sd_card_image);
    }

    // Boot ROM loading (FPGA bootloader — highest priority overlay).
    // Skipped on soft reset: the Emulator-owned boot_rom_ vector already
    // has the contents, and Mmu::reset() preserves the set_boot_rom pointer
    // via its boot_rom_ member. VHDL bootrom_en is NOT in the soft-reset
    // domain — once nextboot.rom disables the overlay via NR 0x03, it stays
    // off across RESET_SOFT so the Z80 boots into the NewlZXOS ROM in SRAM.
    // The post-init block below explicitly clears boot_rom_en on soft reset.
    //
    // Wave 0.1 + 0.3 (Task 8 Multiface plan, 2026-05-04): the FPGA
    // bootloader (`nextboot.rom`, 8 KB) is silicon-baked into the jnext
    // binary at link time (src/core/CMakeLists.txt + embedded_nextboot_rom.h)
    // — same as the on-FPGA flash IPL of real Next hardware. There is no
    // CLI override; this is the only path. Wave 0.3 removed --boot-rom.
    //
    // Auto-load gate: Next machine + sd_card_image non-empty + load_file
    // empty. The sd_card_image gate keeps unit-test fixtures (which
    // construct Emulator with empty cfg) from overlaying the boot ROM
    // when they don't expect it. The load_file gate skips the overlay
    // for direct --load NEX/TAP invocations — the boot ROM at 0x0000-0x1FFF
    // would corrupt the demo's reset vector and break direct NEX
    // execution. (Pre-Wave-0.1 the Wave 0.1 follow-up commit added this
    // dual-gate after a 14-demo regression; semantics retained verbatim.)
    if (!preserve_memory) {
        if (cfg.type == MachineType::ZXN_ISSUE2 &&
            !cfg.sd_card_image.empty() &&
            cfg.load_file.empty()) {
            const uint8_t* embedded_data = embedded_nextboot_rom_data();
            const size_t   embedded_size = embedded_nextboot_rom_size();
            boot_rom_.assign(embedded_data, embedded_data + embedded_size);
            mmu_.set_boot_rom(boot_rom_.data(), boot_rom_.size());
            Log::emulator()->info("Boot ROM loaded from embedded nextboot.rom ({} bytes), overlay active at 0x0000-0x{:04X}",
                                  boot_rom_.size(), boot_rom_.size() - 1);
        }
    }

    // ROM-in-SRAM activation for the Next machine (Task 11 Branch 2).
    // VHDL zxnext.vhd:3052 routes normal-mode ROM reads through SRAM pages
    // 0..7 (sram_rom selects which 8 KB bank). On first init we seed those
    // pages from the file-loaded rom_ buffer so first boot runs as if the
    // flash had programmed the SRAM. tbblue.fw's load_roms() (via Branch 1
    // config_mode routing) then rewrites these pages to install the
    // selected machine's ROM before RESET_SOFT. The seed is skipped on
    // soft reset so the tbblue-loaded content survives (Branch 3). After
    // mmu_.set_rom_in_sram(true) all ROM-slot read_ptr_ entries re-point at
    // ram_ pages — Z80 never reads from rom_ directly again in Next mode.
    if (cfg.type == MachineType::ZXN_ISSUE2) {
        if (!preserve_memory) {
            for (int p = 0; p < 8; ++p) {
                const uint8_t* src = rom_.page_ptr(static_cast<uint16_t>(p));
                uint8_t*       dst = ram_.page_ptr(static_cast<uint16_t>(p));
                if (src && dst) std::memcpy(dst, src, 0x2000);
            }
        }
        mmu_.set_rom_in_sram(true);
        // Task 12 fix: force 128K bank mapping refresh so slots 6/7 land
        // on the Next-correct SRAM pages (base 0x20 + bank*2 per VHDL
        // zxnext.vhd:2964). The default Mmu::reset() seeds slots_[6]=0x00,
        // slots_[7]=0x01 which alias ROM-in-SRAM — writes to 0xC000-0xFFFF
        // would then corrupt ROM/FATFS. Calling map_128k_bank(0) re-routes
        // slot 6/7 to SRAM pages 0x20/0x21 (RAMPAGE_RAMSPECCY area).
        mmu_.map_128k_bank(0);
    }

    // Activate Next config-mode SRAM routing only when we're booting through
    // the FPGA boot ROM. VHDL zxnext.vhd:1102 defaults config_mode='1' at
    // power-on; in our emulator that state is only reachable for the boot-ROM
    // boot path (where the boot ROM overlay masks the routing until
    // nextboot.rom / tbblue.fw run). Direct --load NEX/TAP/TZX starts have no
    // firmware to drive NR 0x03, so keeping config_mode=0 here lets ROM reads
    // fall through to the normal slot path. NR 0x03 writes keep mirroring
    // config_mode_ into the Mmu live thereafter.
    if (cfg.type == MachineType::ZXN_ISSUE2 && mmu_.boot_rom_enabled()) {
        mmu_.set_config_mode(nextreg_.nr_03_config_mode());
        mmu_.set_nr_04_romram_bank(nextreg_.nr_04_romram_bank());
    }

    // Sync ROM3-selected into DivMmc (Task 7 Branch B). On hard reset the
    // MMU resets port_7ffd/port_1ffd to 0, so rom3 is false. On soft reset
    // the port state is also zeroed by mmu_.reset() — subsequent port
    // writes will re-push. Kept here so DivMmc::rom3_active_ starts in sync.
    divmmc_.set_rom3_active(mmu_.rom3_selected());

    // DivMMC ROM loading.
    //
    // Wave 0.3 (Task 8 Multiface plan, 2026-05-04): the DivMMC ROM
    // (`enNxtmmc.rom`, 8 KB) lives on the SD image at the canonical
    // TBBlue path. Like real Next hardware, jnext extracts it from the
    // mounted SD image at init time (host-side FAT32 read; runtime
    // SPI/SD path is independent and serves Z80 software).
    //
    // Skipped when cfg.sd_card_image is empty — unit-test fixtures that
    // don't supply an SD image run with DivMMC disabled, exactly as the
    // pre-Wave-0.3 default-empty divmmc_rom_path behaviour did.
    if (!cfg.sd_card_image.empty()) {
        std::vector<uint8_t> divmmc_rom_bytes;
        const char* divmmc_sd_path = "/MACHINES/NEXT/enNxtmmc.rom";
        if (extract_sd_rom(cfg.sd_card_image, divmmc_sd_path, divmmc_rom_bytes)) {
            if (divmmc_.load_rom_bytes(divmmc_rom_bytes.data(), divmmc_rom_bytes.size())) {
                divmmc_.set_enabled(true);
                Log::emulator()->info("DivMMC enabled, ROM loaded from SD '{}' ({} bytes)",
                                      divmmc_sd_path, divmmc_rom_bytes.size());
            } else {
                Log::emulator()->warn("could not install DivMMC ROM from SD '{}'", divmmc_sd_path);
            }
        } else {
            Log::emulator()->warn("could not extract DivMMC ROM from SD image '{}' (path '{}') — DivMMC disabled",
                                  cfg.sd_card_image, divmmc_sd_path);
        }

        // Wave 1 B1 (Task 8 Multiface plan, 2026-05-04): the Multiface ROM
        // (`enNextMf.rom`, 8 KB) lives on the same SD image. Same pattern
        // as DivMMC above — host-side FAT32 read at init, runtime SPI/SD
        // path is independent. `set_enabled(true)` is NOT called here:
        // the Multiface enable lever is `port_multiface_io_en` (NR 0x83
        // bit 1). VHDL zxnext.vhd:1227 declares `nr_83_internal_port_enable
        // = (others => '1')` and the reset block (line 5055) restores the
        // same all-ones pattern, so b1 is high after reset. We construct
        // `multiface_.enabled_` as false so the FFs come up in held-reset;
        // firmware drives bit 1 explicitly via NR 0x83 writes during boot,
        // and the NR 0x83 write-handler above forwards that to
        // `multiface_.set_enabled(...)`. (Behaviour matches VHDL — only
        // the rationale was inverted in B1 prose.)
        std::vector<uint8_t> mf_rom_bytes;
        const char* mf_sd_path = "/MACHINES/NEXT/enNextMf.rom";
        if (extract_sd_rom(cfg.sd_card_image, mf_sd_path, mf_rom_bytes)) {
            if (multiface_.load_rom_bytes(mf_rom_bytes.data(), mf_rom_bytes.size())) {
                Log::emulator()->info("Multiface ROM loaded from SD '{}' ({} bytes)",
                                      mf_sd_path, mf_rom_bytes.size());
            } else {
                Log::emulator()->warn("could not install Multiface ROM from SD '{}'", mf_sd_path);
            }
        } else {
            Log::emulator()->info("Multiface ROM not found on SD image '{}' (path '{}') — Multiface ROM unavailable",
                                  cfg.sd_card_image, mf_sd_path);
        }

        // AltROM (Alternate ROM) — `enAltZX.rom` (32 KB) on SD at the
        // canonical TBBlue path. The AltROM is a parallel set of system
        // ROM images that NextZXOS firmware switches in via NR 0x8C bit 7
        // (`nr_8c_altrom_en`) for short trampoline calls — e.g. the
        // canonical NextZXOS round-trip pattern enable-AltROM-then-RET-
        // into-AltROM-resident-code, mirrored by a disable-AltROM-then-
        // RET trampoline at the same address inside the AltROM image
        // (`ed 91 8c 00 c9` at offset $7B).
        //
        // Per VHDL zxnext.vhd:2981-3001/3021, ROM-slot reads with
        // `nr_8c_altrom_en=1` AND `nr_8c_altrom_rw=0` route through SRAM
        // pages 0x0C..0x0F via `Mmu::altrom_sram_page_()`:
        //   - alt_128_n=0, addr in $0000-$1FFF → page 0x0C
        //   - alt_128_n=0, addr in $2000-$3FFF → page 0x0D
        //   - alt_128_n=1, addr in $0000-$1FFF → page 0x0E
        //   - alt_128_n=1, addr in $2000-$3FFF → page 0x0F
        // i.e. pages 12-13 hold alt-ROM-0 (16 KB), pages 14-15 hold
        // alt-ROM-1 (16 KB) — together the full 32 KB of enAltZX.rom.
        //
        // Why pre-loaded by the host (and on EVERY reset, not just
        // hard reset): on real Next hardware, AltROM SRAM is populated
        // by the FPGA bitstream's flash-baked initialization — it
        // appears already-loaded the moment the Z80 starts executing,
        // and survives soft resets because a soft reset doesn't
        // re-trigger FPGA bitstream load. tbblue.fw `boot.c::load_roms()`
        // does NOT load enAltZX.rom (verified — only DivMMC+MF+main
        // ROM). NextZXOS supervisor cannot self-load it (chicken-and-
        // egg: it requires AltROM to be loaded BEFORE the first
        // wrapper-mediated AltROM trampoline call at $007B, which
        // happens within the first ~250 ms of supervisor execution).
        //
        // Therefore jnext models the FPGA-flash-pre-baked AltROM by
        // extracting enAltZX.rom from the SD on hard reset only.
        // Skipped silently if the AltROM image is absent on the SD
        // (firmware boots through with restricted feature set, the
        // documented TBBlue fallback). On soft reset (preserve_memory
        // = true) AltROM SRAM persists, mirroring how a real Next FPGA
        // bitstream is not re-loaded by a soft reset.
        if (cfg.type == MachineType::ZXN_ISSUE2 && !preserve_memory) {
            std::vector<uint8_t> alt_rom_bytes;
            const char* alt_sd_path = "/MACHINES/NEXT/enAltZX.rom";
            if (extract_sd_rom(cfg.sd_card_image, alt_sd_path, alt_rom_bytes)) {
                const size_t expected = 4 * 0x2000;  // 4 × 8 KB pages
                const size_t to_copy = std::min(alt_rom_bytes.size(), expected);
                size_t off = 0;
                for (uint16_t p = 0x0C; p <= 0x0F && off < to_copy; ++p) {
                    uint8_t* dst = ram_.page_ptr(p);
                    if (!dst) break;
                    const size_t chunk = std::min<size_t>(0x2000, to_copy - off);
                    std::memcpy(dst, alt_rom_bytes.data() + off, chunk);
                    off += chunk;
                }
                Log::emulator()->info("AltROM loaded from SD '{}' ({} bytes -> SRAM pages 0x0C..0x0F)",
                                      alt_sd_path, off);
            } else {
                Log::emulator()->info("AltROM not found on SD image '{}' (path '{}') — AltROM unavailable (NextZXOS will likely crash on first AltROM trampoline)",
                                      cfg.sd_card_image, alt_sd_path);
            }
        }
    }

    // SD card image mounting
    if (!cfg.sd_card_image.empty()) {
        if (sd_card_.mount(cfg.sd_card_image)) {
            Log::emulator()->info("SD card image mounted: '{}'", cfg.sd_card_image);
        } else {
            Log::emulator()->warn("could not mount SD card image: '{}'", cfg.sd_card_image);
        }
    }

    // Wire palette manager and RAM into ULA for enhanced palette and
    // hardware-accurate VRAM access (ULA reads directly from physical bank 5,
    // bypassing the MMU, matching the VHDL dual-port RAM architecture).
    renderer_.ula().set_palette(&palette_);
    renderer_.ula().set_ram(&ram_);

    // Default border: white (ZX colour index 7).
    renderer_.ula().set_border(7);

    // Compositor per-pixel trace (debug; configured via
    // --compositor-trace / --compositor-trace-frame). Empty path disables.
    renderer_.set_compositor_trace(cfg.compositor_trace_path,
                                   cfg.compositor_trace_frame);

    // Initialise rewind buffer.
    // Measure snapshot size by doing a dry-run in measure mode (buf=nullptr).
    if (cfg.rewind_buffer_frames > 0) {
        StateWriter measure;
        save_state(measure);
        size_t snap_bytes = measure.position();
        rewind_buffer_ = std::make_unique<RewindBuffer>(
            static_cast<size_t>(cfg.rewind_buffer_frames), snap_bytes);
        rewind_enabled_ = true;
        trace_log_.set_enabled(true);
        Log::emulator()->info("Rewind buffer: {} frames × {} bytes = {} KB",
            cfg.rewind_buffer_frames, snap_bytes,
            (static_cast<uint64_t>(cfg.rewind_buffer_frames) * snap_bytes + 512) / 1024);
    } else {
        rewind_buffer_.reset();
        rewind_enabled_ = false;
        Log::emulator()->debug("Rewind buffer: disabled");
    }

    return true;
}

bool Emulator::inject_binary(const std::string& path, uint16_t org, uint16_t pc)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        Log::emulator()->error("--inject: cannot open '{}'", path);
        return false;
    }

    auto size = f.tellg();
    if (size <= 0) {
        Log::emulator()->error("--inject: file '{}' is empty", path);
        return false;
    }

    // Guard against overflowing the 64K address space.
    if (static_cast<uint32_t>(org) + static_cast<uint32_t>(size) > 0x10000) {
        Log::emulator()->error("--inject: {} bytes at {:#06x} would exceed 64K", static_cast<int>(size), org);
        return false;
    }

    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(buf.data()), size);

    for (size_t i = 0; i < buf.size(); ++i) {
        mmu_.write(static_cast<uint16_t>(org + i), buf[i]);
    }

    // Set PC to the requested entry point.
    auto regs = cpu_.get_registers();
    regs.PC = pc;
    // Set SP to a sensible value if it hasn't been moved.
    regs.SP = 0xFFFD;
    // Disable interrupts — the injected binary can enable them itself.
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    cpu_.set_registers(regs);

    Log::emulator()->info("--inject: loaded {} bytes from '{}' at {:#06x}, PC={:#06x}",
                           static_cast<int>(size), path, org, pc);
    Log::emulator()->debug("--inject: first bytes at {:#06x}: {:02x} {:02x} {:02x} {:02x}",
                            org, mmu_.read(org), mmu_.read(org+1), mmu_.read(org+2), mmu_.read(org+3));
    return true;
}

bool Emulator::load_nex(const std::string& path)
{
    NexLoader loader;
    if (!loader.load(path)) return false;

    // Full machine reset before applying NEX data ensures clean subsystem
    // state (palette, video layers, NextREG, etc.) regardless of whether
    // the emulator was already running or freshly started.
    reset();

    return loader.apply(*this);
}

bool Emulator::load_sna(const std::string& path)
{
    SnaLoader loader;
    if (!loader.load(path)) return false;
    reset();
    return loader.apply(*this);
}

bool Emulator::load_tap(const std::string& path, bool fast_load)
{
    TapLoader loader;
    if (!loader.load(path)) return false;

    loader.set_fast_load(fast_load);
    tape_ = std::move(loader);
    Log::emulator()->info("TAP: tape attached — {} blocks, mode: {}",
                           tape_.block_count(), tape_.fast_load() ? "fast" : "realtime");

    // Auto-type LOAD "" to start tape loading.
    // In 48K BASIC keyword mode:
    //   J         = LOAD keyword  (row 6, col 3)
    //   SYM+P     = "             (row 7, col 1 + row 5, col 0)
    //   SYM+P     = "             (row 7, col 1 + row 5, col 0)
    //   ENTER     = execute       (row 6, col 0)
    std::vector<Keyboard::AutoKey> keys = {
        {6, 3,  -1, -1, 5},   // J = LOAD
        {5, 0,   7,  1, 5},   // SYM+P = "
        {5, 0,   7,  1, 5},   // SYM+P = "
        {6, 0,  -1, -1, 5},   // ENTER
    };
    keyboard_.queue_auto_type(keys);

    // For real-time mode, start tape playback immediately.
    // The leader tone will play while the ROM waits for edge detection.
    if (!tape_.fast_load()) {
        tape_.start_realtime_playback();
    }

    return true;
}

bool Emulator::load_tzx(const std::string& path, bool fast_load)
{
    TzxLoader loader;
    if (!loader.load(path)) return false;

    loader.set_fast_load(fast_load);
    tzx_tape_ = std::move(loader);
    Log::emulator()->info("TZX: tape attached, mode: {}",
                           tzx_tape_.fast_load() ? "fast" : "realtime");

    // Eject any TAP tape to avoid conflicts.
    if (tape_.is_loaded()) tape_.eject();

    // Auto-type LOAD "" to start tape loading.
    std::vector<Keyboard::AutoKey> keys = {
        {6, 3,  -1, -1, 5},   // J = LOAD
        {5, 0,   7,  1, 5},   // SYM+P = "
        {5, 0,   7,  1, 5},   // SYM+P = "
        {6, 0,  -1, -1, 5},   // ENTER
    };
    keyboard_.queue_auto_type(keys);

    // For real-time mode, start TZX playback immediately.
    if (!tzx_tape_.fast_load()) {
        uint64_t cpu_clocks = static_cast<uint64_t>(*fuse_z80_tstates_ptr());
        tzx_tape_.start_playback(cpu_clocks);
        Log::emulator()->info("TZX: playback started at T-state {}", cpu_clocks);
    }

    return true;
}

bool Emulator::load_szx(const std::string& path)
{
    SzxLoader loader;
    if (!loader.load(path)) return false;
    reset();
    return loader.apply(*this);
}

bool Emulator::load_wav(const std::string& path)
{
    WavLoader loader;
    if (!loader.load(path)) return false;

    wav_tape_ = std::move(loader);
    Log::emulator()->info("WAV: tape attached (always real-time)");

    // Eject any other tape to avoid conflicts.
    if (tape_.is_loaded()) tape_.eject();
    if (tzx_tape_.is_loaded()) tzx_tape_.eject();

    // Auto-type LOAD "" to start tape loading.
    std::vector<Keyboard::AutoKey> keys = {
        {6, 3,  -1, -1, 5},   // J = LOAD
        {5, 0,   7,  1, 5},   // SYM+P = "
        {5, 0,   7,  1, 5},   // SYM+P = "
        {6, 0,  -1, -1, 5},   // ENTER
    };
    keyboard_.queue_auto_type(keys);

    // WAV is always real-time — start playback immediately.
    uint64_t cpu_clocks = static_cast<uint64_t>(*fuse_z80_tstates_ptr());
    wav_tape_.start_playback(cpu_clocks);

    return true;
}

bool Emulator::load_rzx(const std::string& path)
{
    RzxRecording rec;
    if (!rzx::parse(path, rec)) {
        Log::emulator()->error("RZX: failed to parse '{}'", path);
        return false;
    }

    Log::emulator()->info("RZX: loaded '{}' — creator='{}' frames={} snapshot={}",
                          path, rec.creator, rec.frames.size(),
                          rec.snapshot_data.empty() ? "none" : rec.snapshot_ext);

    // Load embedded snapshot if present.
    if (!rec.snapshot_data.empty()) {
        // Write snapshot to a temporary file and load it.
        std::string tmp_path = "/tmp/jnext_rzx_snap." + rec.snapshot_ext;
        {
            std::ofstream tmp(tmp_path, std::ios::binary);
            tmp.write(reinterpret_cast<const char*>(rec.snapshot_data.data()),
                      static_cast<std::streamsize>(rec.snapshot_data.size()));
        }
        if (rec.snapshot_ext == "sna") {
            if (!load_sna(tmp_path)) return false;
        } else if (rec.snapshot_ext == "szx") {
            if (!load_szx(tmp_path)) return false;
        } else {
            Log::emulator()->warn("RZX: unsupported snapshot type '{}', skipping", rec.snapshot_ext);
        }
    }

    // Wire up port override for playback.
    rzx_player_.start(std::move(rec));
    port_.rzx_in_override = [this](uint16_t) -> uint8_t {
        return rzx_player_.next_in_value();
    };

    return true;
}

bool Emulator::start_rzx_recording(const std::string& path)
{
    // Save current state as SNA snapshot for embedding.
    auto sna_data = SnaSaver::save(*this);

    rzx_recorder_.start(path);
    if (!sna_data.empty()) {
        rzx_recorder_.set_snapshot(std::move(sna_data), "sna");
    }
    rzx_recorder_.set_initial_tstates(*fuse_z80_tstates_ptr());

    // Wire up port recording hook.
    port_.rzx_in_record = [this](uint8_t val) {
        rzx_recorder_.record_in(val);
    };

    return true;
}

void Emulator::stop_rzx_recording()
{
    port_.rzx_in_record = nullptr;
    rzx_recorder_.stop();
}

void Emulator::run_frame()
{
    // Per-frame sample of the IM2 DMA-delay latch into the DMA peripheral.
    // VHDL zxnext.vhd:2001-2010 models this as a per-CPU-clock latch, but
    // the jnext architecture runs the DMA start-gate check coarsely, so a
    // per-frame sample (user-resolved decision 2026-04-21, option A) is
    // adequate for the currently-covered test rows (DMA-01/02/03/05, NR CC/CD/CE-01).
    // Finer-grain wiring can be revisited if a specific workload requires it.
    dma_.set_dma_delay(im2_.dma_delay());

    // Handle rewind step modes set by the GUI or scripting layer.
    // These are processed before the normal snapshot so we don't take a
    // snapshot of the "current" state before rewinding away from it.
    if (debug_state_.active() && !replay_mode_) {
        if (debug_state_.step_mode() == StepMode::STEP_BACK) {
            step_back(debug_state_.step_back_count());
            return;
        }
        if (debug_state_.step_mode() == StepMode::RUN_BACK_TO_CYCLE) {
            rewind_to_cycle(debug_state_.target_cycle());
            return;
        }
    }

    // Snapshot at frame boundary — scheduler queue is empty here, which is
    // required for correct serialisation (no pending events to save).
    if (rewind_buffer_ && rewind_enabled_ && !replay_mode_) {
        rewind_buffer_->take_snapshot(*this, frame_cycle_, frame_num_++);
    }

    const uint64_t frame_end = frame_cycle_ + timing_.master_cycles_per_frame;

    // Reset FUSE tstates counter to 0 at frame start.  derive_hc_vc() in
    // z80_cpu.cpp computes (hc, vc) directly from `tstates % tstates_per_frame`,
    // so the FUSE counter must be frame-relative for ContentionModel::
    // contention_tick() to gate on the right raster window.
    //
    // VideoTiming reset alongside: derive_hc_vc() in z80_cpu.cpp computes
    // (hc, vc) directly from `tstates`, so VideoTiming is the test-side
    // observable. Reset its hc/vc at frame start so test queries
    // mid-frame match the (hc, vc) the contention path is using.
    *fuse_z80_tstates_ptr() = 0;
    frame_ts_start_ = 0;
    video_timing_.reset();

    // Schedule the ULA frame interrupt.
    //
    // G107: VHDL fires the ULA-frame pulse at (hc==c_int_h, vc==c_int_v)
    // (zxula_timing.vhd:551). Per machine these vary — 48K {116, 0},
    // 128K {128, 1}, +3 {126, 1}, Pentagon {439, 319}. We delegate the
    // master-cycle conversion to VideoTiming::frame_int_master_cycle_offset()
    // which honours the per-machine c_int_h/c_int_v.
    //
    // Routing: the ULA is priority slot 11 in the VHDL IM2 fabric
    // (zxnext.vhd:1937, 1941 — im2_int_req bit 11 = ula_int_pulse). In
    // pulse-mode (NR 0xC0 bit 0 = 0, the power-on default) the Z80 INT
    // line is driven by the legacy `request_interrupt(0xFF)` path so
    // 48K/128K frame-interrupt semantics are preserved. In IM2 mode the
    // Z80 INT is asserted through `Im2Controller::int_line_asserted()`
    // instead, so we must NOT also fire the legacy request in that case
    // (would cause a double-fire).
    if (!ula_int_disabled_) {
        const uint64_t int_fire_offset = video_timing_.frame_int_master_cycle_offset();
        scheduler_.schedule(frame_cycle_ + int_fire_offset, EventType::CPU_INT,
            [this]() {
                im2_.raise_req(Im2Controller::DevIdx::ULA);
                if (!im2_.is_im2_mode()) {
                    cpu_.request_interrupt(0xFF);
                }
                im2_int_status_[0] |= 0x01;  // ULA interrupt status
            });
    }

    // Schedule line interrupt if enabled.
    //
    // G106/G109: VHDL line-int fires when (hc_ula==255) and (cvc==int_line_num)
    // (zxula_timing.vhd:577). int_line_num maps target=0→c_max_vc and
    // target=N→N-1 (:566-570). cvc is offset-adjusted from cu_offset
    // (NR 0x64) at ula_min_vactive (:462). VideoTiming::
    // line_int_master_cycle_offset() returns the master-cycle within the
    // frame at which the firing scanline begins, accounting for all three.
    //
    // Routing: LINE is priority slot 0 in the VHDL IM2 fabric
    // (zxnext.vhd:1937, 1941 — im2_int_req bit 0 = line_int_pulse, the
    // highest-priority peripheral). Same pulse-vs-IM2 split as the ULA
    // path above.
    //
    // G163 — delegate to reschedule_line_interrupt(). Bumping the gen
    // here also invalidates any stale events left over from prior frames
    // (e.g. a target the demo wrote during the previous frame whose
    // firing line moved to the current frame's wrap region).
    reschedule_line_interrupt();

    // RZX frame management.
    if (rzx_player_.is_playing()) {
        rzx_player_.begin_frame();
        if (!rzx_player_.is_playing()) {
            // Playback just finished — remove override.
            port_.rzx_in_override = nullptr;
        }
    }
    if (rzx_recorder_.is_recording()) {
        rzx_recorder_.begin_frame();
        rzx_frame_instruction_count_ = 0;
    }

    // Notify copper of frame start (resets PC in mode 11).
    copper_.on_vsync();

    // Initialize per-line fallback array to current value.
    // The copper will update individual lines during execution.
    renderer_.init_fallback_per_line();

    // Initialize per-line ULA-enable snapshot (parallel to fallback_per_line_).
    // A Copper MOVE to NR 0x68 mid-frame will update individual lines via
    // on_scanline → renderer_.snapshot_ula_enabled_for_line.
    renderer_.init_ula_enabled_per_line();

    // Initialize per-line border colour to current value.
    // Port 0xFE writes will update individual lines during execution.
    renderer_.ula().init_border_per_line();

    // Initialize per-line tilemap scroll to current values.
    // Interrupt handlers may change scroll mid-frame for split-screen effects.
    tilemap_.init_scroll_per_line();

    // Per-scanline tilemap NR 0x6B change log (G06) — baseline snapshot
    // and reset of the per-frame log so Copper / interrupt-handler writes
    // to NR 0x6B (mode flip, textmode, 512-tile, tm_on_top, enable) take
    // effect on the very next scanline.
    tilemap_.start_frame_nr6b();

    // Per-scanline palette snapshot — baseline copy + change-log reset
    // (TASK-PER-SCANLINE-PALETTE-PLAN.md). Without this, Copper writes
    // to NR 0x41 mid-frame collapse to the LAST value at render time
    // (e.g. beast.nex sky gradient).
    palette_.start_frame();

    // Per-scanline Layer 2 scroll snapshot — same pattern as palette.
    // Required for beast.nex bottom-band parallax (Copper writes NR 0x16
    // at scanlines 163, 165, 169, 173, 179 with progressively higher
    // values to scroll each grass band at a different speed).
    layer2_.start_frame();

    // Per-scanline sprite-attribute snapshot — same pattern as palette
    // and Layer 2. Required for parallax.nex (Z80N DMA bulk-streams
    // sprite attributes via port 0x57 at ~140 DMA-LOAD per second; each
    // upload places sprites at a different Y position so without
    // per-scanline replay all 96 sprites collapse to the very last
    // upload's Y positions). VHDL sprites.vhd:327-470.
    sprites_.start_frame();

    // Per-scanline port-0xFF Timex screen-mode snapshot (G07).
    renderer_.ula().start_frame();
    // Per-scanline ULA scroll snapshot (G08).
    renderer_.ula().start_frame_scroll();
    // Per-scanline active-palette selector snapshot (G10).
    renderer_.ula().palsel_start_frame();

    // Schedule per-scanline callbacks (snapshots fallback colour for copper).
    schedule_frame_events();

    // Audio timing constants.
    // PSG clock = 28 MHz / 16 = 1.75 MHz → one PSG tick every 16 master cycles.
    static constexpr uint64_t PSG_DIVISOR = 16;
    // Sample generation: 28 MHz / 44100 Hz ≈ 634.92 master cycles per sample.
    // Use Bresenham-style accumulator: generate sample every time accum >= MASTER_CLOCK_HZ.
    static constexpr uint64_t SAMPLE_THRESHOLD = MASTER_CLOCK_HZ;

    while (clock_.get() < frame_end) {
        // Debugger breakpoint check — before executing the next instruction.
        if (debug_state_.active()) {
            // Check if an external trigger (e.g. magic breakpoint) already
            // paused the emulator during the previous instruction's execute().
            if (debug_state_.paused())
                return;
            uint16_t pc = cpu_.get_registers().PC;
            if (debug_state_.should_break(pc)) {
                debug_state_.pause();
                // Early return: leave frame_cycle_ as-is so resume continues
                // from this point.  The display shows the previous frame.
                return;
            }
            if (debug_state_.step_mode() == StepMode::INTO) {
                // Step-into: pause immediately (caller already executed one
                // instruction via execute_single_instruction()).
                debug_state_.pause();
                return;
            }
            if (debug_state_.step_mode() == StepMode::RUN_TO_CYCLE) {
                if (clock_.get() >= debug_state_.target_cycle()) {
                    debug_state_.pause();
                    return;
                }
            }
        }

        // Tape ROM traps — only when ROM is paged in at slot 0.
        if (mmu_.is_slot_rom(0)) {
            uint16_t pc = cpu_.get_registers().PC;

            // Fast-load: intercept LD-BYTES when tape is loaded and in fast mode.
            if (tape_.is_loaded() && tape_.fast_load() && !tape_.at_end() &&
                pc == TapLoader::LD_BYTES_ADDR) {
                tape_.handle_ld_bytes_trap(*this);
                uint64_t fake_cycles = 100ULL * clock_.cpu_divisor();
                clock_.tick(fake_cycles);
                scheduler_.run_until(clock_.get());
                continue;
            }

            // TZX fast-load: same ROM trap, different loader.
            if (tzx_tape_.is_loaded() && tzx_tape_.fast_load() && !tzx_tape_.at_end() &&
                pc == TzxLoader::LD_BYTES_ADDR) {
                tzx_tape_.handle_ld_bytes_trap(*this);
                uint64_t fake_cycles = 100ULL * clock_.cpu_divisor();
                clock_.tick(fake_cycles);
                scheduler_.run_until(clock_.get());
                continue;
            }

        }

        uint64_t master_cycles;

        if (dma_.is_active()) {
            // DMA takes the bus — CPU is stalled.
            // Execute a burst of transfers; each byte ≈ 2 T-states.
            int transferred = dma_.execute_burst(16);
            master_cycles = static_cast<uint64_t>(transferred * 2) * clock_.cpu_divisor();
            if (master_cycles == 0) master_cycles = clock_.cpu_divisor();  // minimum advance
        } else {
            // Record trace entry before execution (captures pre-execution state).
            // Enabled during replay so consecutive step-backs can look up target cycles.
            if (trace_log_.enabled()) {
                auto regs = cpu_.get_registers();
                TraceEntry te;
                te.cycle = clock_.get();
                te.pc = regs.PC;
                te.af = regs.AF; te.bc = regs.BC;
                te.de = regs.DE; te.hl = regs.HL;
                te.af2 = regs.AF2; te.bc2 = regs.BC2;
                te.de2 = regs.DE2; te.hl2 = regs.HL2;
                te.ix = regs.IX; te.iy = regs.IY;
                te.sp = regs.SP;
                for (int i = 0; i < 4; ++i)
                    te.opcode_bytes[i] = mmu_.read(regs.PC + i);
                te.opcode_len = z80_instruction_length(regs.PC,
                    [this](uint16_t a) { return mmu_.read(a); });
                trace_log_.record(te);
            }
            // Call stack tracking (debugger only, gated by enabled flag).
            if (call_stack_.enabled()) {
                auto regs2 = cpu_.get_registers();
                uint8_t op0 = mmu_.read(regs2.PC);
                uint8_t op1 = mmu_.read(regs2.PC + 1);
                uint8_t op2 = mmu_.read(regs2.PC + 2);
                call_stack_.on_instruction_pre(regs2.PC, regs2.SP, op0, op1, op2);
            }

            // Execute one CPU instruction; returns T-states consumed.
            // Memory contention is applied per-access via the
            // ContentionModel::contention_tick() runtime path installed by
            // z80_set_contention_runtime() in init() — the FUSE callbacks
            // derive (hc, vc) from the FUSE tstates counter and feed them
            // into contention_tick() per memory/IO bus cycle.
            //
            // G65 — set defer flag around the instruction so any port-0x253B
            // CPU NR writes go to pending_cpu_nr_writes_ instead of
            // committing synchronously. The queue is drained AFTER the
            // Copper-loop runs (below), modeling VHDL's "Copper-wins on
            // tied edge, CPU held over 1 cycle" behaviour as "CPU writes
            // apply LAST in the instruction window".
            defer_cpu_nr_writes_ = true;
            int tstates = cpu_.execute();
            defer_cpu_nr_writes_ = false;

            // Advance VideoTiming so test/debug observers see the post-
            // instruction raster position. The contention path itself
            // does NOT consult VideoTiming (it derives hc/vc from
            // tstates directly to keep per-bus-cycle precision); this
            // advance is purely the test/debug observable.
            video_timing_.advance(tstates);

            // Call stack tracking post-execution.
            if (call_stack_.enabled()) {
                auto post = cpu_.get_registers();
                call_stack_.on_instruction_post(post.SP, post.PC);
            }

            // Convert T-states to 28 MHz master cycles.
            master_cycles = static_cast<uint64_t>(tstates) * clock_.cpu_divisor();

            // Advance the IM2 controller one tick per instruction. This is
            // coarser than the VHDL per-CLK_CPU-rising-edge model but is
            // sufficient for:
            //   - device state-machine transitions (S_0→S_REQ→S_ACK→S_ISR
            //     → S_0), which react to per-M1 signals anyway;
            //   - pulse fabric counter (coarser granularity stretches the
            //     pulse duration beyond VHDL's 32/36 cycles but preserves
            //     the outcome — INT low then high);
            //   - DMA delay latch, which is sampled once per frame into
            //     Dma::set_dma_delay at the top of run_frame().
            // Test code that needs finer granularity can drive im2_.tick()
            // directly (most ctc_test rows do).
            //
            // Wave E: push the current NMI-activated sample into
            // Im2Controller before its tick so step_dma_delay() can evaluate
            // VHDL:2007's second OR term (nmi_activated AND nr_cc_dma_int_en_0_7).
            // NmiSource's own tick runs a few lines below (after the CTC /
            // UART / Md6 cluster) — so this read-samples last tick's
            // latched state, matching the VHDL synchronous-update rule where
            // im2_dma_delay and nmi_activated both settle on the same rising
            // edge of CLK_CPU.
            im2_.set_nmi_activated(nmi_source_.is_activated());
            im2_.tick(master_cycles);

            // Count instructions for RZX recording.
            if (rzx_recorder_.is_recording()) ++rzx_frame_instruction_count_;

            // Tick real-time tape playback (advances EAR bit state machine).
            if (tape_.is_playing()) {
                tape_.tick_realtime(static_cast<uint64_t>(tstates));
                beeper_.set_tape_ear(tape_.tick_realtime(0) != 0);
            } else if (tzx_tape_.is_playing()) {
                // TZX real-time: ZOT uses absolute CPU T-state clocks.
                uint64_t cpu_clocks = static_cast<uint64_t>(*fuse_z80_tstates_ptr());
                beeper_.set_tape_ear(tzx_tape_.update(cpu_clocks) != 0);
            } else if (wav_tape_.is_playing()) {
                uint64_t cpu_clocks = static_cast<uint64_t>(*fuse_z80_tstates_ptr());
                beeper_.set_tape_ear(wav_tape_.get_ear_bit(cpu_clocks) != 0);
            } else {
                beeper_.set_tape_ear(false);
            }
        }
        clock_.tick(master_cycles);

        // Tick DMA burst prescaler (counts down between burst-mode transfers).
        dma_.tick_burst_wait(master_cycles);

        // Check if a data breakpoint was hit during this instruction.
        if (debug_state_.active() && debug_state_.data_bp_hit()) {
            debug_state_.pause();
            debug_state_.set_data_bp_hit(false);
            return;
        }

        // Execute the Copper at 28 MHz granularity over the master-cycle
        // window covered by this CPU instruction (G117). Pre-G117 this
        // path stepped the Copper exactly once per Z80 instruction, so
        // dense Copper bursts (tilemap-class effects with 32 MOVEs per
        // scanline) collapsed into 1-2 effective writes per scanline.
        tick_copper_for_master_cycles(master_cycles);

        // G65 — drain CPU NR writes deferred during the instruction.
        // They commit AFTER the Copper-loop, so any same-NR collision
        // ends with the CPU's value (VHDL: cpu_req held while
        // copper_req=1, served the next cycle).
        flush_pending_cpu_nr_writes();

        // Commit ContentionModel's NR 0x08 bit-6 shadow when the live
        // raster is in the `hc(8)='1'` window (phc >= 256), per VHDL
        // zxnext.vhd:5822-5823. The VHDL latches every CPU-clock-rising
        // bus-idle cycle while hc(8)='1'; a per-instruction call here
        // approximates that — any instruction that ends with the beam
        // in the right border / hblank promotes the shadow to the
        // effective gate. CT-TURBO-06 pins this: a NR 0x08 write at
        // phc < 256 does NOT change the gate until the next time the
        // beam crosses 256.
        contention_.commit_contention_disable_on_hc(
            static_cast<uint16_t>(current_hc()));

        // Commit NR 0x07 cpu_speed shadow → effective on bus-idle, per
        // VHDL zxnext.vhd:5796-5828. The post-instruction tick is the
        // natural bus-idle moment in jnext's per-instruction granularity
        // (CPU is between bus cycles: mreq_n=1, iorq_n=1, m1_n=1).
        // dma_holds_bus is false here because dma_.tick_burst_wait()
        // above only counts down the burst-mode prescaler — it does not
        // hold the bus. Distinct from NR 0x08 b6 which has the
        // additional hc(8)='1' window gate; NR 0x07 commits on every
        // bus-idle cycle. This pair of independent commit edges (NR 0x07
        // bus-idle, NR 0x08 b6 bus-idle AND hc(8)) is the G51 / G142
        // ordering: simultaneous mid-line writes commit on their own
        // edges, not in lockstep.
        const bool bus_idle = true;
        const bool dma_holds_bus = false;
        clock_.commit_pending_cpu_speed_on_bus_idle(bus_idle, dma_holds_bus);
        contention_.commit_pending_cpu_speed_on_bus_idle(bus_idle, dma_holds_bus);

        // Tick CTC and UART at 28 MHz rate.
        ctc_.tick(static_cast<uint32_t>(master_cycles));
        uart_.tick(static_cast<uint32_t>(master_cycles));
        md6_.tick(static_cast<uint32_t>(master_cycles));

        // G72 closure — per-tick injector callback feeding IoMode from
        // the live UART TX lines. VHDL zxnext.vhd:3526-3531 wires
        // `pin7 = uart0_tx` (when NR 0x0B mode=10/11, iomode_0=0) /
        // `pin7 = uart1_tx` (iomode_0=1). The IoMode pin-7 mux is
        // unit-correct (covered by IOMODE-05/06) but inert at runtime
        // until something feeds the injectors. UART::channel(N).tx_line_out()
        // is updated by the bit-level engine inside uart_.tick() above,
        // so we sample it immediately afterwards. No-op when pin-7 is
        // not in a UART mode — IoMode::pin7() reads pin7_ instead and
        // these stored fields are consulted only under modes 10/11.
        //
        // Note (Tier 2 W1): the corresponding `set_joy_left_bit5()` /
        // `set_joy_right_bit5()` injectors (VHDL i_JOY_LEFT(5) /
        // i_JOY_RIGHT(5), zxnext.vhd:3539) are NOT yet fed because
        // Joystick currently has no public bit-5 accessor. That follow-
        // up is owned by the Joystick agent in a later wave; UART
        // injectors close G72's primary observable here.
        iomode_.set_uart0_tx(uart_.channel(0).tx_line_out());
        iomode_.set_uart1_tx(uart_.channel(1).tx_line_out());

        // Tick the NMI source pipeline (TASK-NMI-SOURCE-PIPELINE-PLAN.md
        // Phase 1 + Wave B + Wave C). Placement matches CTC / UART / Md6
        // in the per-instruction cluster; order among these is not
        // load-bearing.
        //
        // Wave B — DivMMC consumer-feedback loop (VHDL:2107, 2118, 2098):
        //   * `divmmc_nmi_hold`  = `o_disable_nmi` = automap_held OR
        //                          button_nmi  (divmmc.vhd:150). Gates
        //                          the HOLD→END transition for the
        //                          DivMMC path AND blocks the MF latch
        //                          set (VHDL:2107).
        //   * `divmmc_conmem`    = port_e3_reg(7). Blocks the MF latch
        //                          set (VHDL:2107, 2098 on the FPGA).
        // Wave C — NR 0x03 config_mode gate (VHDL zxnext.vhd:2102-2105)
        // force-clears all NMI latches. Push BEFORE tick() so the FSM
        // sees the current value.
        nmi_source_.set_divmmc_nmi_hold(divmmc_.is_nmi_hold());
        nmi_source_.set_divmmc_conmem(divmmc_.is_conmem());
        nmi_source_.set_config_mode(nextreg_.nr_03_config_mode());

        // Wave 1 F-gate — Multiface consumer-feedback (replaces always-
        // false stubs from TASK-NMI-SOURCE-PIPELINE-PLAN). VHDL refs:
        //   zxnext.vhd:2099 — mf_is_active = mf_mem_en OR mf_nmi_hold.
        //   zxnext.vhd:2105 — mf_is_active gates the DivMMC NMI latch
        //                     in the priority arbiter (when MF is
        //                     active, nmi_assert_divmmc cannot win).
        //   zxnext.vhd:2118 — mf_nmi_hold drives nmi_hold when nmi_mf=1.
        //   zxnext.vhd:4111 — divmmc_retn_seen AND-NOT mf_is_active
        //                     (handled at the M1 retn-delay site above).
        nmi_source_.set_mf_is_active(multiface_.is_active());
        nmi_source_.set_mf_nmi_hold(multiface_.is_nmi_hold());

        nmi_source_.tick(static_cast<uint32_t>(master_cycles));

        // Wave B — VHDL:2170 `nmi_divmmc_button` arbitration-strobe
        // producer: when the FSM latched the DivMMC request this tick
        // (IDLE→FETCH via the DivMMC path), pulse `set_button_nmi(true)`
        // on the DivMMC peripheral. That latches `button_nmi_` which in
        // turn re-feeds into `divmmc_nmi_hold` next tick (plus gates
        // automap_nmi_instant_on at divmmc.vhd:120). The strobe is
        // one-tick, cleared by NmiSource on its next `tick()` entry.
        if (nmi_source_.divmmc_button_strobe()) {
            divmmc_.set_button_nmi(true);
        }

        // Edge-driven Z80 /NMI assertion. The NmiSource FSM drives
        // `nmi_generate_n()` low when it wants the Z80 to take an NMI
        // (VHDL:2164-2170). FUSE's Z80 core takes the NMI on the next
        // instruction boundary via `request_nmi()` — a one-shot latch.
        // We observe the falling edge here and fire the latch.
        {
            const bool nmi_n = nmi_source_.nmi_generate_n();
            if (!nmi_n && prev_nmi_generate_n_) {
                cpu_.request_nmi();
            }
            prev_nmi_generate_n_ = nmi_n;
        }

        // Tick PSG (TurboSound) at 1.75 MHz rate.
        psg_accum_ += master_cycles;
        while (psg_accum_ >= PSG_DIVISOR) {
            psg_accum_ -= PSG_DIVISOR;
            turbosound_.tick();
        }

        // Generate audio samples at 44100 Hz using Bresenham accumulator.
        // Suppressed in replay mode (fast-forward rewind path).
        if (!replay_mode_) {
            sample_accum_ += master_cycles * Mixer::SAMPLE_RATE;
            while (sample_accum_ >= SAMPLE_THRESHOLD) {
                sample_accum_ -= SAMPLE_THRESHOLD;
                mixer_.generate_sample(beeper_, turbosound_, dac_);
            }
        }

        // Drain any scheduler events that have become due.
        scheduler_.run_until(clock_.get());
    }

    // Save end-of-frame raster before advancing frame_cycle_, so snapshot_raster()
    // can show a meaningful position when Break is pressed between frames.
    {
        uint64_t elapsed = clock_.get() - frame_cycle_;
        last_frame_vc_ = std::min(static_cast<int>(elapsed / timing_.master_cycles_per_line),
                                  timing_.lines_per_frame - 1);
        last_frame_hc_ = static_cast<int>((elapsed % timing_.master_cycles_per_line) / 4);
    }
    frame_cycle_ = frame_end;

    // Snapshot the fallback/border/ULA-enable colour and tilemap scroll
    // for the last visible framebuffer row. G164v2 — these arrays are
    // indexed by fb_row in [0, FB_HEIGHT), so the end-of-frame snapshot
    // must use FB_HEIGHT-1 (=255), not the raw last VC line.
    renderer_.snapshot_fallback_for_line(Renderer::FB_HEIGHT - 1);
    renderer_.snapshot_ula_enabled_for_line(Renderer::FB_HEIGHT - 1);
    renderer_.ula().snapshot_border_for_line(Renderer::FB_HEIGHT - 1);
    tilemap_.snapshot_scroll_for_line(Renderer::FB_HEIGHT - 1);

    // Render the completed frame into the ARGB8888 framebuffer.
    // Suppressed in replay mode (fast-forward rewind path).
    if (!replay_mode_) {
        renderer_.render_frame(framebuffer_.data(), mmu_, ram_, palette_,
                               layer2_, &sprites_, &tilemap_);
    }

    // Capture frame for video recording (if active, not in replay).
    if (!replay_mode_ && video_recorder_.is_recording()) {
        video_recorder_.capture_frame(framebuffer_.data(),
                                       FRAMEBUFFER_WIDTH, FRAMEBUFFER_HEIGHT);
    }

    // End RZX recording frame (not in replay).
    if (!replay_mode_ && rzx_recorder_.is_recording()) {
        rzx_recorder_.end_frame(static_cast<uint16_t>(
            std::min(rzx_frame_instruction_count_, uint32_t(0xFFFF))));
    }

    // Advance auto-type state machine (one step per frame).
    keyboard_.tick_auto_type();

    // G133 closure — drive Keyboard::tick_scan() once per video frame
    // so the two-scan shift hysteresis (membrane.vhd:178-191, 188-191)
    // advances in production. The VHDL membrane scans at FPGA pixel-
    // clock rate, but per-frame is the correct cadence here: tick_scan
    // only snapshots the current matrix shift bits into shift_hist_[],
    // and shift_hist_ is read by Keyboard::read_rows() to hold a
    // releasing CS/SYM bit for one extra scan. Faster cadence wouldn't
    // change the observable — Z80 software polls the membrane via
    // port 0xFE at most once per frame in normal operation, and the
    // hysteresis goal is "release lags by ~1 frame", which one tick
    // per frame matches exactly.
    keyboard_.tick_scan();
}

int Emulator::current_scanline() const
{
    uint64_t elapsed = clock_.get() - frame_cycle_;
    return static_cast<int>(elapsed / timing_.master_cycles_per_line);
}

int Emulator::current_hc() const
{
    // VHDL phc (practical horizontal count) ticks at the pixel clock.
    // 28 MHz master / 4 = 7 MHz pixel clock → 1 pixel = 4 master cycles.
    // phc runs 0..447 (48K) or 0..455 (other modes) per line.
    uint64_t elapsed = clock_.get() - frame_cycle_;
    return static_cast<int>((elapsed % timing_.master_cycles_per_line) / 4);
}

void Emulator::snapshot_raster()
{
    // Always compute VC/HC from actual elapsed cycles since the current frame
    // start.  When Break is pressed "between frames" (after run_frame() has
    // advanced frame_cycle_ but before the next frame has started), elapsed is
    // near zero and VC=0 is accurate — the CPU is at the very start of a new
    // frame.  The old "between frames" workaround that returned last_frame_vc_
    // caused the EOSL step from VC=255 to land at VC=1 instead of VC=0.
    uint64_t elapsed = clock_.get() - frame_cycle_;
    paused_vc_ = static_cast<int>(elapsed / timing_.master_cycles_per_line);
    paused_hc_ = static_cast<int>((elapsed % timing_.master_cycles_per_line) / 4);
}

int Emulator::execute_single_instruction()
{
    // Audio timing constants (same as in run_frame).
    static constexpr uint64_t PSG_DIVISOR = 16;
    static constexpr uint64_t SAMPLE_THRESHOLD = MASTER_CLOCK_HZ;

    uint64_t master_cycles;
    if (dma_.is_active()) {
        int transferred = dma_.execute_burst(16);
        master_cycles = static_cast<uint64_t>(transferred * 2) * clock_.cpu_divisor();
        if (master_cycles == 0) master_cycles = clock_.cpu_divisor();
    } else {
        // Call stack tracking (debugger single-step path).
        if (call_stack_.enabled()) {
            auto regs = cpu_.get_registers();
            uint8_t op0 = mmu_.read(regs.PC);
            uint8_t op1 = mmu_.read(regs.PC + 1);
            uint8_t op2 = mmu_.read(regs.PC + 2);
            call_stack_.on_instruction_pre(regs.PC, regs.SP, op0, op1, op2);
        }

        // G65 — see run_frame() primary path for rationale.
        defer_cpu_nr_writes_ = true;
        int tstates = cpu_.execute();
        defer_cpu_nr_writes_ = false;

        // Mirror video_timing_ advance from run_frame() — single-step
        // path needs the same observable to stay in sync.
        video_timing_.advance(tstates);

        // Call stack tracking post-execution.
        if (call_stack_.enabled()) {
            auto post = cpu_.get_registers();
            call_stack_.on_instruction_post(post.SP, post.PC);
        }

        master_cycles = static_cast<uint64_t>(tstates) * clock_.cpu_divisor();
    }
    clock_.tick(master_cycles);
    dma_.tick_burst_wait(master_cycles);

    // Copper at 28 MHz granularity (G117). See run_frame() primary site
    // for rationale; this is the debugger single-step / DMA-only path.
    tick_copper_for_master_cycles(master_cycles);

    // G65 — drain deferred CPU NR writes (after Copper).
    flush_pending_cpu_nr_writes();

    // Commit shadow → effective for NR 0x08 b6 (hc(8) gate) and NR 0x07
    // cpu_speed (bus-idle gate), per VHDL zxnext.vhd:5796-5828. Mirrors
    // the run_frame() primary tick cluster — without it, debugger
    // single-step paths would not commit deferred NR writes. NR 0x07
    // commit is unconditional bus-idle (the post-instruction tick is
    // bus-idle by construction); NR 0x08 b6 also requires hc(8)='1'.
    contention_.commit_contention_disable_on_hc(
        static_cast<uint16_t>(current_hc()));
    {
        const bool bus_idle = true;
        const bool dma_holds_bus = false;
        clock_.commit_pending_cpu_speed_on_bus_idle(bus_idle, dma_holds_bus);
        contention_.commit_pending_cpu_speed_on_bus_idle(bus_idle, dma_holds_bus);
    }

    // CTC + UART.
    ctc_.tick(static_cast<uint32_t>(master_cycles));
    uart_.tick(static_cast<uint32_t>(master_cycles));

    // G72 closure — mirror of the per-tick UART → IoMode injector
    // wire from the run_frame() primary cluster. Keeps the debugger
    // single-step path observably consistent with the bulk-tick path.
    iomode_.set_uart0_tx(uart_.channel(0).tx_line_out());
    iomode_.set_uart1_tx(uart_.channel(1).tx_line_out());

    // NMI source pipeline — mirrors the primary tick cluster above.
    // Wave B: DivMMC consumer-feedback (hold + conmem) + button_nmi strobe.
    // Wave C: config_mode gate. Wave 1 F-gate: MF consumer-feedback
    // (mf_is_active + mf_nmi_hold per VHDL :2099/:2105/:2118).
    // See primary cluster for full VHDL citations.
    nmi_source_.set_divmmc_nmi_hold(divmmc_.is_nmi_hold());
    nmi_source_.set_divmmc_conmem(divmmc_.is_conmem());
    nmi_source_.set_config_mode(nextreg_.nr_03_config_mode());
    nmi_source_.set_mf_is_active(multiface_.is_active());
    nmi_source_.set_mf_nmi_hold(multiface_.is_nmi_hold());
    nmi_source_.tick(static_cast<uint32_t>(master_cycles));
    if (nmi_source_.divmmc_button_strobe()) {
        divmmc_.set_button_nmi(true);
    }
    {
        const bool nmi_n = nmi_source_.nmi_generate_n();
        if (!nmi_n && prev_nmi_generate_n_) {
            cpu_.request_nmi();
        }
        prev_nmi_generate_n_ = nmi_n;
    }

    // PSG.
    psg_accum_ += master_cycles;
    while (psg_accum_ >= PSG_DIVISOR) {
        psg_accum_ -= PSG_DIVISOR;
        turbosound_.tick();
    }

    // Audio samples.
    sample_accum_ += master_cycles * Mixer::SAMPLE_RATE;
    while (sample_accum_ >= SAMPLE_THRESHOLD) {
        sample_accum_ -= SAMPLE_THRESHOLD;
        mixer_.generate_sample(beeper_, turbosound_, dac_);
    }

    // Scheduler.
    scheduler_.run_until(clock_.get());

    return static_cast<int>(master_cycles / clock_.cpu_divisor());
}

void Emulator::reset()
{
    // VHDL zxnext.vhd:5052-5057: on soft reset, NR 0x82-0x84 are reloaded
    // to 0xFF only when reset_type (NR 0x85 bit 7) is 1. When reset_type=0,
    // they are preserved. Save the port-enable state and reset_type before
    // init() triggers a second nextreg_.reset() that would lose them.
    const bool reset_type_1 = (nextreg_.cached(0x85) & 0x80) != 0;
    const uint8_t save_82 = nextreg_.cached(0x82);
    const uint8_t save_83 = nextreg_.cached(0x83);
    const uint8_t save_84 = nextreg_.cached(0x84);

    clock_.reset();
    scheduler_.reset();
    frame_cycle_ = 0;

    ram_.reset();
    // Emulator::reset() is a hard reset (see header — "Perform a hard
    // reset: reinitialize all subsystems, clear RAM, reload ROM").
    mmu_.reset(/*hard=*/true);
    nextreg_.reset();
    cpu_.reset();
    im2_.reset();
    keyboard_.reset();
    // Input subsystem Phase 1 scaffold (Task 3).
    joystick_.reset();
    mouse_.reset();
    md6_.reset();
    membrane_stick_.reset();
    iomode_.reset();
    // F-key FSM (G132) — VHDL emu_fnkeys.vhd:89,106,169,182 reset path:
    //   timer_count → 0, state → S_IDLE, cancel_nmi → 0, local_fnkeys → 0.
    emu_fnkeys_.reset();

    // Clear framebuffer to black.
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xFF000000u);

    // Re-run init to restore consistent state (reloads ROM, rewires handlers).
    init(config_);

    // Restore port-enable registers per reset_type semantics.
    if (!reset_type_1) {
        nextreg_.write(0x82, save_82);
        nextreg_.write(0x83, save_83);
        nextreg_.write(0x84, save_84);
    }
}

void Emulator::soft_reset()
{
    // Soft reset (tbblue RESET_SOFT / NR 0x02 bit 0).
    // Preserves: RAM (including Next ROM-in-SRAM window pages 0..7), Rom
    // buffer, boot_rom_en (see mechanism note below), NR 0x82-0x84 per NR
    // 0x85 bit 7 semantics. Resets: clock, scheduler, frame state, CPU,
    // MMU slot map, NextReg register file (except the port-enable trio),
    // peripherals (via init).
    //
    // VHDL reference for NR 0x82-0x84: zxnext.vhd:5052-5057 — on soft
    // reset, these reload to 0xFF only if NR 0x85 bit 7 (reset_type) is 1.
    // The bracketing save/restore around init() is load-bearing because
    // init() calls NextReg::reset() twice (once per subsystem reset loop
    // and again inside init()), and the second call reads regs_[0x85]=0x8F
    // set by the first and would clobber 0x82-0x84 to 0xFF.
    const bool reset_type_1 = (nextreg_.cached(0x85) & 0x80) != 0;
    const uint8_t save_82 = nextreg_.cached(0x82);
    const uint8_t save_83 = nextreg_.cached(0x83);
    const uint8_t save_84 = nextreg_.cached(0x84);

    // VHDL bootrom_en (zxnext.vhd:1101, reset logic at :5109-5111, cleared
    // by NR 0x03 write at :5122). The reset block for bootrom_en runs on
    // BOTH hard and soft reset (the `reset` signal is `reset_hard OR
    // reset_soft`) but is guarded by `if nr_03_config_mode = '1'`. Since
    // nr_03_config_mode has no reset branch itself, it holds across reset.
    // Net effect: once firmware has written NR 0x03 with bits[2:0]
    // ∈ {001..110} to clear config_mode, a subsequent soft reset leaves
    // bootrom_en at its cleared value. Our Mmu::reset() unconditionally
    // re-enables when a boot_rom_ pointer is present, so we capture and
    // restore the pre-reset value explicitly.
    const bool prev_boot_rom_en = mmu_.boot_rom_enabled();

    Log::emulator()->info("Soft reset (NR 0x02 bit 0): preserving SRAM + boot_rom_en={}",
                          prev_boot_rom_en);

    // Clear framebuffer to black (not part of emulated state).
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xFF000000u);

    // Re-run init with preserve_memory=true: skip RAM/ROM reinit and the
    // SRAM-from-rom seed so the ROM window keeps whatever tbblue.fw just
    // installed. All FF-based state (CPU, MMU slots, NextReg, peripherals)
    // is reset via init()'s subsystem-reset loop — no separate pre-init
    // reset dance needed.
    init(config_, /*preserve_memory=*/true);

    // Restore bootrom_en (see comment above for VHDL mechanism).
    mmu_.set_boot_rom_enabled(prev_boot_rom_en);

    // Restore port-enable registers per reset_type semantics.
    if (!reset_type_1) {
        nextreg_.write(0x82, save_82);
        nextreg_.write(0x83, save_83);
        nextreg_.write(0x84, save_84);
    }
}

// ---------------------------------------------------------------------------
// Host hotkey dispatchers (G152) — single seam between gui/main_window.cpp
// F-key handlers and the emulation core. Mirrors VHDL hotkey_m1 /
// hotkey_drive / hotkey_soft_reset / hotkey_hard_reset semantics
// (zxnext.vhd:6340-6371, 2089-2091).
// ---------------------------------------------------------------------------

void Emulator::on_hotkey_f9_mf_nmi()
{
    // VHDL zxnext.vhd:6348, 2090 — `hotkey_m1` is a one-cycle pulse OR'd
    // into the MF assert; the gate at NR 0x06 bit 3 determines whether
    // the producer actually fires. We strobe the NmiSource button line
    // unconditionally; the gate is honoured downstream in
    // `nmi_assert_mf()`.
    nmi_source_.strobe_mf_button();

    // Wave 1 B1 — also drive the Multiface FSM directly. VHDL
    // zxnext.vhd:4274-4310 instantiates multiface_mod with `button_i`
    // pulled from the same source. button_press() takes effect only
    // when NMI_ACTIVE='0' (multiface.vhd:135 `button_pulse <= button_i
    // AND NOT nmi_active`); subsequent presses while a Multiface NMI
    // is in flight are no-ops, exactly as on real hardware.
    multiface_.button_press();
}

void Emulator::on_hotkey_f10_divmmc_nmi()
{
    // VHDL zxnext.vhd:6349 — `hotkey_drive <= hotkeys_0(10) and not
    // hotkeys_1(10) and port_divmmc_io_en`. The `port_divmmc_io_en`
    // gate (NR 0x83 bit 0) suppresses the F10 edge entirely when the
    // DivMMC port is disabled; honour it here so the strobe never
    // fires in that state. The NR 0x06 bit 4 gate is honoured
    // downstream in `NmiSource::nmi_assert_divmmc()`.
    if (!divmmc_.port_io_enable()) {
        return;
    }
    nmi_source_.strobe_divmmc_button();
}

void Emulator::on_hotkey_f4_soft_reset()
{
    // VHDL zxnext.vhd:6370 — `nr_02_soft_reset <= (hotkey_soft_reset
    // and not nr_03_config_mode) or (nr_02_we and nr_wr_dat(0))`.
    // Honour the config_mode gate: while the firmware holds
    // `nr_03_config_mode = 1`, host F4 must NOT advance the reset_type
    // FSM nor restart the system.
    if (nextreg_.nr_03_config_mode()) {
        return;
    }
    nmi_source_.strobe_soft_reset();
    Log::emulator()->info("Soft reset triggered via host F4 (hotkey_soft_reset)");
    soft_reset();
}

void Emulator::on_hotkey_f1_hard_reset()
{
    // VHDL zxnext.vhd:6371 — `nr_02_hard_reset <= hotkey_hard_reset or
    // (nr_02_we and nr_wr_dat(1))`. No config_mode gate (hard reset is
    // unconditional). Hard reset does NOT advance the reset_type FSM
    // (FSM advance is only on the soft_reset edge per VHDL:1735).
    Log::emulator()->info("Hard reset triggered via host F1 (hotkey_hard_reset)");
    reset();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void Emulator::schedule_frame_events()
{
    // Schedule one SCANLINE event per line and a VSYNC at the frame boundary.
    for (int line = 0; line < timing_.lines_per_frame; ++line) {
        const uint64_t line_cycle =
            frame_cycle_ + static_cast<uint64_t>(line) * timing_.master_cycles_per_line;

        scheduler_.schedule(line_cycle, EventType::SCANLINE,
            [this, line]() { on_scanline(line); });
    }

    // VSYNC fires at the very end of the frame.
    scheduler_.schedule(
        frame_cycle_ + timing_.master_cycles_per_frame,
        EventType::VSYNC,
        [this]() { on_vsync(); });
}

uint8_t Emulator::floating_bus_read() const
{
    // Port 0xFF read mux per VHDL zxnext.vhd:2813:
    //   port_ff_rd_dat <= port_ff_dat_tmx                       -- Timex arm
    //                     when nr_08_port_ff_rd_en = '1'
    //                      and port_ff_io_en       = '1'
    //                      and port_ff_rd          = '1'
    //                else port_ff_dat_ula                       -- ULA arm
    //                     when port_ff_rd = '1'
    //                else X"00";
    //
    // Branch A (TASK3-FLOATING-BUS-SKIP-REDUCTION-PLAN.md): wire the
    // per-machine gate, the NR 0x08 b2 Timex override, and the NR 0x82 b0
    // `port_ff_io_en` gate that collapses the Timex arm. The default-port
    // dispatch site at init() routes ALL unmatched port reads here, which
    // is wider than VHDL's port_ff-only mux; the floating-bus content we
    // synthesise is still meaningful for those calls because the bus
    // settles to whatever ULA is driving when no decoder responds.

    // ---- 1. Timex arm: NR 0x08 bit 2 + NR 0x82 bit 0 (VHDL :2813, :2397) ----
    //
    // When `nr_08_port_ff_rd_en` is set AND `port_ff_io_en` is set, the
    // read returns `port_ff_dat_tmx` — a one-CPU-cycle delayed copy of
    // `port_ff_reg`, which is the byte last WRITTEN to port 0xFF (Timex
    // screen-mode register, VHDL :3614-3616, :3630). We mirror that byte
    // in `Ula::screen_mode_reg_` (set by the port 0xFF write handler at
    // emulator.cpp:1303 — itself gated on port_ff_io_en, matching VHDL
    // `port_ff_wr <= iowr and port_ff and port_ff_io_en` at :2714).
    //
    // VHDL reset default: `nr_08_port_ff_rd_en <= '0'` (zxnext.vhd:1118),
    // matched by NR 0x08's reset path which clears bit 2 of
    // `nr_08_stored_low_` (emulator.cpp:117-120).
    //
    // KNOWN GAP (out of scope for Branch A): VHDL :3614-3624 also lets
    // NR 0x69 bits 5:0, NR 0x22 bit 2, and NR 0xC4 bit 0 update
    // `port_ff_reg` (bits 5:0 / bit 6); we don't propagate those into
    // `screen_mode_reg_` yet. Port-0xFF writes are the dominant source.
    const uint8_t nr_08 = nr_08_stored_low_;
    const uint8_t nr_82 = nextreg_.cached(0x82);
    const bool port_ff_rd_en = (nr_08 & 0x04) != 0;   // NR 0x08 b2
    const bool port_ff_io_en = (nr_82 & 0x01) != 0;   // NR 0x82 b0
    if (port_ff_rd_en && port_ff_io_en) {
        return renderer_.ula().get_screen_mode_reg();
    }

    // ---- 2. Per-machine ULA arm gate (VHDL :4513) ----
    //
    //   port_ff_dat_ula <= ula_floating_bus
    //                       when (machine_timing_48 = '1'
    //                          or machine_timing_128 = '1')
    //                       else X"FF";
    //
    // Only 48K and 128K timings deliver the ULA floating bus. +3 keeps
    // 0xFF on this path (its floating-bus surface is port 0x0FFD, which
    // Branch B handles separately). Pentagon and the Next default also
    // collapse to 0xFF here — for Next the runtime `nr_03_machine_timing`
    // could in principle re-enable 48K/128K timing, but Branch A follows
    // the prompt's simplification (Next → 0xFF) and leaves the runtime
    // re-classification to a follow-up if the test plan demands it.
    if (config_.type != MachineType::ZX48K && config_.type != MachineType::ZX128K) {
        return 0xFF;
    }

    // ---- 3. ULA floating-bus content (48K/128K only) ----
    //
    // Compute current position within the frame. Master clock is 28 MHz;
    // T-states at 3.5 MHz = master_cycles / 8.
    uint64_t master_elapsed = clock_.get() - frame_cycle_;
    int tstates_in_frame = static_cast<int>(master_elapsed / cpu_speed_divisor(config_.cpu_speed));

    // Scanline timing:
    //   228 T-states per line (48K/128K).
    //   First 128 T-states: ULA fetches pixel/attribute data.
    //   Last 100 T-states: border (bus idle = 0xFF).
    //   Active display: lines 64-255 (192 pixel lines).
    int line = tstates_in_frame / timing_.tstates_per_line;
    int tstate_in_line = tstates_in_frame % timing_.tstates_per_line;

    // Outside active display area: border, bus is idle
    if (line < 64 || line >= 256 || tstate_in_line >= 128)
        return 0xFF;

    // Within active display: ULA fetches in 8-T-state cycles.
    // Each 8T cycle: T+0=bitmap, T+1=attr, T+2=bitmap+1, T+3=attr+1, T+4..7=idle
    // (Actually the FUSE/ZesarUX model uses: T%8: 2=pixel, 3=attr, 4=pixel+1, 5=attr+1)
    int pixel_line = line - 64;
    int char_col = tstate_in_line / 8;  // character column (0-15)

    // Compute the VRAM address the ULA would be reading.
    // Pixel address: standard ZX Spectrum display file layout
    //   addr = 0x4000 | (line[7:6] << 11) | (line[2:0] << 8) | (line[5:3] << 5) | col
    int y = pixel_line;
    uint16_t pixel_addr = 0x4000
        | ((y & 0xC0) << 5)   // bits 7:6 → bits 12:11
        | ((y & 0x07) << 8)   // bits 2:0 → bits 10:8
        | ((y & 0x38) << 2)   // bits 5:3 → bits 7:5
        | (char_col * 2);     // 2 bytes per 8T cycle

    // Attribute address: 0x5800 + (line/8)*32 + col
    uint16_t attr_addr = 0x5800 + (y / 8) * 32 + char_col * 2;

    switch (tstate_in_line % 8) {
        case 2: return ram_.read(pixel_addr - 0x4000 + 10 * 0x2000);       // pixel byte
        case 3: return ram_.read(attr_addr  - 0x4000 + 10 * 0x2000);       // attribute byte
        case 4: return ram_.read(pixel_addr - 0x4000 + 10 * 0x2000 + 1);   // pixel byte +1
        case 5: return ram_.read(attr_addr  - 0x4000 + 10 * 0x2000 + 1);   // attribute byte +1
        default: return 0xFF;  // idle T-states within the 8T cycle
    }
}

void Emulator::enqueue_cpu_nr_write(uint8_t reg, uint8_t val)
{
    pending_cpu_nr_writes_.emplace_back(reg, val);
}

void Emulator::flush_pending_cpu_nr_writes()
{
    if (pending_cpu_nr_writes_.empty()) return;
    // Drain in FIFO order. Each commit goes through the regular
    // NextReg::write path so write_handlers fire (any side effect
    // landing here is the same as if the CPU had written directly,
    // just shifted later by the per-instruction Copper window).
    for (auto& [reg, val] : pending_cpu_nr_writes_) {
        nextreg_.write(reg, val);
    }
    pending_cpu_nr_writes_.clear();
}

// ---------------------------------------------------------------------------
// G163 — line-interrupt dynamic re-evaluation.
//
// VHDL `zxula_timing.vhd:577` fires the line-int pulse every cycle when
// `(hc_ula==255 AND cvc==int_line_num)` — fully dynamic. Pre-fix jnext
// scheduled the line-int once per frame in run_frame() and the NR 0x22 /
// NR 0x23 / NR 0xC4 (bit 1 mirror) write handlers updated VideoTiming
// state without re-scheduling. Demos that chain line interrupts
// mid-frame (e.g. parallax.nex's IRQ handler at bank 6 offset 0x062E
// rewriting NR 0x23 with `target += 0x10`) silently lost ~12/13 line
// interrupts per frame.
//
// NR 0xC4 bit 1 is a hardware mirror of NR 0x22 bit 1: both write the
// SAME `nr_22_line_interrupt_en` flip-flop (VHDL zxnext.vhd:5607-5610),
// which feeds `i_inten_line` of the line-interrupt comparator at :6752.
// A demo can re-arm or disable line interrupts via either register, so
// every write site must reschedule.
//
// Strategy: bump a generation counter on every (re)schedule and capture
// it by-value into the lambda. At fire time the lambda checks the
// captured value against the current counter and no-ops if superseded.
// This is a strict superset of "compare target at fire time": same-
// target rewrites also produce a fresh schedule, so the old event still
// becomes a no-op.
// ---------------------------------------------------------------------------
void Emulator::reschedule_line_interrupt()
{
    if (!video_timing_.line_interrupt_enable()) {
        // Disable bit cleared mid-frame: bump the gen so any pending
        // event no-ops, and do not enqueue a replacement.
        ++line_int_schedule_gen_;
        return;
    }

    const uint64_t line_offset = video_timing_.line_int_master_cycle_offset();
    if (line_offset >= timing_.master_cycles_per_frame) {
        // Out-of-range target: bump the gen to invalidate any stale
        // event but do not enqueue a replacement (matches the existing
        // run_frame guard at the original schedule site).
        ++line_int_schedule_gen_;
        return;
    }

    uint64_t fire_cycle = frame_cycle_ + line_offset;
    if (fire_cycle <= clock_.get()) {
        // The firing scanline within this frame has already passed —
        // roll forward one frame. Handles the parallax 8-bit `ADD 0x10`
        // overflow case (line 244 → line 4, where line 4 belongs to the
        // NEXT frame).
        fire_cycle += timing_.master_cycles_per_frame;
    }

    ++line_int_schedule_gen_;
    const uint64_t my_gen = line_int_schedule_gen_;
    scheduler_.schedule(fire_cycle, EventType::CPU_INT,
        [this, my_gen]() {
            if (my_gen != line_int_schedule_gen_) return;  // superseded
            im2_.raise_req(Im2Controller::DevIdx::LINE);
            if (!im2_.is_im2_mode()) {
                cpu_.request_interrupt(0xFF);
            }
            im2_int_status_[0] |= 0x02;  // Line interrupt status
            ++line_int_fire_count_;       // G163 test-observable
        });
}

void Emulator::tick_copper_for_master_cycles(uint64_t master_cycles)
{
    // Hot-path early-out — most of every frame the Copper is in mode 0
    // (stopped) and the loop body would be pure overhead.
    if (!copper_.is_running()) return;
    if (master_cycles == 0) return;

    // `clock_` was already advanced by `master_cycles` before we are
    // called (see emulator.cpp run_frame / single-step paths). The
    // pre-instruction master-clock value is therefore `clock_.get() -
    // master_cycles`, and the Copper steps fire at master cycles
    // [pre, pre+1, ..., pre+master_cycles-1].
    const uint64_t post_clock = clock_.get();
    const uint64_t pre_clock  = post_clock - master_cycles;

    for (uint64_t c = 0; c < master_cycles; ++c) {
        const uint64_t at = pre_clock + c;
        const uint64_t elapsed = at - frame_cycle_;
        const int vc = static_cast<int>(elapsed / timing_.master_cycles_per_line);
        const int hc = static_cast<int>(elapsed % timing_.master_cycles_per_line);
        // Copper vc: cvc=0 at the first active display line, mirroring
        // VHDL `zxula_timing.vhd:455-472`. The first active display line
        // in VHDL is vc=min_vactive (64 for NEXT 50Hz). The previous
        // implementation used DISP_Y (32), which is the FRAMEBUFFER-ROW
        // origin for the display, NOT the VHDL VC origin — that
        // off-by-32 made the Copper fire WAITs 32 lines too early.
        // The bug was masked pre-G164v2 because the per-scanline
        // change-log was also off by 32 in the same direction, so
        // Copper-driven NR writes happened to land at the right
        // framebuffer row by accident. After the change-log fix
        // (this commit's parent) tagged writes in framebuffer-row
        // space, the Copper-side miscount became visible (parallax.nex
        // bottom-third banding).
        const int cvc = (vc - static_cast<int>(video_timing_.display_origin().vc)
                              + timing_.lines_per_frame)
                            % timing_.lines_per_frame;
        copper_.execute(hc, cvc, nextreg_);
    }
}

void Emulator::on_scanline(int line)
{
    // Snapshot the fallback colour / ULA-enable / border / tilemap scroll
    // for the previous scanline. By the time on_scanline(N) fires, the
    // copper has finished executing for vc=N-1, so the snapshotted
    // values reflect mid-frame Copper MOVE writes that landed in vc=N-1.
    //
    // G164v2 — index by FRAMEBUFFER ROW, not raw VC. The renderer reads
    // these arrays at `row` (fb_row), so the snapshot index must be in
    // the same space. Convert (line-1) raw VC → fb_row by subtracting
    // the per-machine vblank-top (= min_vactive - DISP_Y; equals 32 for
    // NEXT-family but 48 for Pentagon, 8 for 60Hz overrides). Values
    // outside [0, FB_HEIGHT) are vblank/off-frame and shouldn't be
    // snapshotted — the bounds check filters them out. (Task 13.)
    {
        const int prev_fb_row = (line - 1) - video_timing_.vblank_top();
        if (prev_fb_row >= 0 && prev_fb_row < Renderer::FB_HEIGHT) {
            renderer_.snapshot_fallback_for_line(prev_fb_row);
            renderer_.snapshot_ula_enabled_for_line(prev_fb_row);
            renderer_.ula().snapshot_border_for_line(prev_fb_row);
            tilemap_.snapshot_scroll_for_line(prev_fb_row);
        }
    }
    // G164v2 — convert raw VC scanline to framebuffer-row before tagging
    // per-scanline change-log entries. Renderer::render_frame iterates
    // `row` in framebuffer-row space (0..FB_HEIGHT-1) and calls each
    // subsystem's apply_changes_for_line(row); the change-log entry's
    // line tag must be in the SAME space.
    //
    // The offset between raw VC and framebuffer row is PER-MACHINE
    // (Task 13): `vblank_top = min_vactive - DISP_Y`. For NEXT 50Hz /
    // 48K / 128K / +3 50Hz that's 64-32=32 (numerical coincidence with
    // DISP_Y); for Pentagon 80-32=48; for 60 Hz overrides 40-32=8.
    // Pre-Task-13 callers used `Renderer::DISP_Y` directly, which is
    // correct ONLY for the NEXT family — Pentagon and 60Hz suffered a
    // 16/24-row misalignment that no current demo exercised in the
    // regression suite, but was nonetheless wrong by VHDL.
    //
    // Pre-display vblank writes (fb_row < 0): coalesce to fb_row 0 so
    // they get applied at the START of the visible frame. This matches
    // VHDL's "writes take effect immediately" semantic — a write during
    // vblank is visible from the first display line onward. Tagging
    // them with a sentinel like 0xFFFF would stall the per-scanline
    // cursor walk (`while cursor < N && log[cursor].line == line`)
    // because the sentinel sits at the front of the log and never
    // matches a visible row.
    //
    // Post-display writes (fb_row >= FB_HEIGHT): tag stays out-of-range
    // (> FB_HEIGHT-1). The visible-row apply loop never matches them;
    // flush_remaining_changes drains them at end-of-frame so they
    // become next frame's baseline. The cursor naturally reaches them
    // only after all visible entries have been applied, so no stall.
    const int fb_row = line - video_timing_.vblank_top();
    const uint16_t tag = (fb_row < 0) ? 0u : static_cast<uint16_t>(fb_row);

    palette_.set_current_line(tag);
    layer2_.set_current_line(tag);
    sprites_.set_current_line(tag);
    renderer_.ula().set_current_line(tag);
    renderer_.ula().set_current_scroll_line(tag);
    renderer_.ula().set_palsel_current_line(tag);
    tilemap_.set_current_nr6b_line(tag);
}

void Emulator::on_vsync()
{
    // Stub: nothing to do yet.
    // Real implementation:
    //   - Signal the platform layer that a new frame is ready.
    //   - Reset per-frame state (floating bus cache, sprite collision flags).
}

// ---------------------------------------------------------------------------
// State serialisation — save_state / load_state
// ---------------------------------------------------------------------------
//
// Snapshots are taken at frame boundaries (before any events are scheduled
// into the scheduler_), so the scheduler queue is always empty here.
// Subsystem order must be identical in save_state and load_state.
//
// NOT serialised (rewired by init() / not part of emulated state):
//   rom_          — loaded from file at startup, never changes
//   contention_   — rebuilt from config.type by build()
//   port_         — lambda callbacks only; rewired in init()
//   keyboard_     — user input, not CPU state
//   mixer_        — output path, discarded on rewind by design
//   debug_state_  — debugger transient state
//   trace_log_    — debug trace, not emulated state
//   call_stack_   — debug call stack, not emulated state
//   tape_/tzx_tape_/wav_tape_ — tape position independent of CPU rewind
//   video_recorder_, rzx_player_, rzx_recorder_ — recording, not state
//   sd_card_      — external device, not serialised
//   boot_rom_     — loaded from file, never changes
//   framebuffer_  — regenerated by render_frame()
//   paused_vc_, paused_hc_, last_frame_vc_, last_frame_hc_ — raster transient
//   frame_ts_start_ — FUSE tstates at frame start, set at run_frame() entry

void Emulator::save_state(StateWriter& w) const
{
    // Core subsystems.
    clock_.save_state(w);
    ram_.save_state(w);
    mmu_.save_state(w);
    nextreg_.save_state(w);
    cpu_.save_state(w);
    im2_.save_state(w);

    // Video subsystems.
    palette_.save_state(w);
    layer2_.save_state(w);
    sprites_.save_state(w);
    tilemap_.save_state(w);
    renderer_.save_state(w);   // includes ULA

    // Peripheral subsystems.
    copper_.save_state(w);
    ctc_.save_state(w);
    dma_.save_state(w);
    spi_.save_state(w);
    i2c_.save_state(w);
    rtc_.save_state(w);
    uart_.save_state(w);
    divmmc_.save_state(w);

    // Audio subsystems.
    beeper_.save_state(w);
    turbosound_.save_state(w);
    dac_.save_state(w);

    // Emulator private state.
    w.write_u64(frame_cycle_);
    w.write_u32(frame_num_);
    w.write_u64(psg_accum_);
    w.write_u64(sample_accum_);
    w.write_bool(dac_enabled_);
    // G71: line_int state owned by video_timing_; mirror through the
    // existing save layout (bool + u16) for backwards compatibility.
    w.write_bool(video_timing_.line_interrupt_enable());
    w.write_bool(ula_int_disabled_);
    w.write_u16(video_timing_.line_interrupt_target());
    w.write_bool(im2_hw_mode_);
    w.write_u8(im2_vector_base_);
    w.write_bytes(im2_int_enable_, 3);
    w.write_bytes(im2_int_status_, 3);
    w.write_bool(im2_c4_expbus_);
    w.write_u8(nr_c6_uart_int_en_);
    w.write_u8(clip_l2_idx_);
    w.write_u8(clip_spr_idx_);
    w.write_u8(clip_ula_idx_);
    w.write_u8(clip_tm_idx_);

    // IM2 DMA delay enables (NR 0xCC/0xCD/0xCE) + latched output.
    w.write_bool(nr_cc_dma_delay_on_nmi_);
    w.write_u8(nr_cc_dma_delay_en_ula_);
    w.write_u8(nr_cd_dma_delay_en_ctc_);
    w.write_u8(nr_ce_dma_delay_en_uart1_);
    w.write_u8(nr_ce_dma_delay_en_uart0_);
    w.write_bool(im2_dma_delay_latched_);

    // Branch C: NR 0x08 read mirror (bits 5..0 of last NR 0x08 write).
    w.write_u8(nr_08_stored_low_);

    // Agent H: joy_iomode_pin7 (NR 0x0B / CTC ch3) — VHDL zxnext.vhd:3516.
    // Phase 2 Wave 2 Agent E migrated this field into IoMode; we keep the
    // single-bool save-state slot for binary compatibility with prior saves
    // and just sample/restore through the IoMode accessor.
    w.write_bool(iomode_.pin7());

    // Audio Phase 1: I2s stub latched sample pair. Appended at the END
    // of the save stream (NOT interleaved with the other audio
    // subsystems) so prior saves that predate this field remain
    // readable up to the pin7 slot.
    i2s_.save_state(w);

    // NMI pipeline Phase 1 scaffold — appended after i2s for the same
    // reason (backwards-compatible append-only save layout).
    nmi_source_.save_state(w);
    w.write_bool(prev_nmi_generate_n_);

    // G108 — port_ff_reg storage. Appended at the very end so old
    // saves that predate the field deserialise cleanly (load_state
    // reads it last and tolerates EOF by leaving the reset default).
    w.write_u8(port_ff_reg_);

    // G56-B — NR 0x10 coreid shadow. Cluster B's read_handler returns
    // (nr_10_coreid_ << 2) instead of regs_[0x10], so without this slot
    // the field would be re-init()ed to 0x01 on load_state and the
    // post-load read of NR 0x10 would not match the pre-save read.
    // Append-only at the very end (after port_ff_reg_) for backwards
    // compatibility with prior saves; load_state tolerates EOF.
    w.write_u8(nr_10_coreid_);

    // G55 — IO-trap registers (NR 0xD8/0xD9/0xDA). Appended after
    // nr_10_coreid_ for backwards-compatible save layout (load tolerates
    // EOF by leaving reset defaults).
    w.write_bool(nr_d8_io_trap_fdc_en_);
    w.write_u8(nr_d9_iotrap_write_);
    w.write_u8(nr_da_iotrap_cause_);

    // G112 — NR 0x2D I2S sample latch (pre-shifted into bits [7:6]).
    // Appended at the very end so older saves remain forwards-readable;
    // load_state tolerates EOF by leaving the reset default of 0.
    w.write_u8(nr_2d_i2s_sample_);

    // G113 — NR 0xA2 Pi I2S control byte. Same end-of-snapshot append +
    // eof()-tolerant load pattern as nr_2d_i2s_sample_ above. Stored
    // outside I2s::save_state() so older snapshots whose I2s slot was
    // exactly 4 bytes (left+right) keep working without byte-shifting
    // the subsequent nmi_source_/prev_nmi_generate_n_ slots.
    w.write_u8(i2s_.nr_a2_ctl());

    // G135 — NR 0xA0 Pi peripheral enable byte. End-of-snapshot append
    // + eof()-tolerant load (same pattern as the surrounding G108 / G55 /
    // G112 / G113 slots above). Older snapshots leave the field at its
    // reset default of 0, matching VHDL zxnext.vhd:5080.
    w.write_u8(nr_a0_pi_peripheral_en_);

    // Wave 1 B1 — Multiface subsystem state (FFs + RAM, ~16 bytes header
    // + 8 KB RAM). Append-only: writes a sentinel `1` byte first so the
    // load path can detect presence. Older snapshots leave Multiface at
    // its constructor-init defaults (reset() called from constructor).
    w.write_u8(0x01);
    multiface_.save_state(w);
}

void Emulator::load_state(StateReader& r)
{
    // Core subsystems.
    clock_.load_state(r);
    ram_.load_state(r);
    mmu_.load_state(r);
    nextreg_.load_state(r);
    cpu_.load_state(r);
    im2_.load_state(r);

    // Video subsystems.
    palette_.load_state(r);
    layer2_.load_state(r);
    sprites_.load_state(r);
    tilemap_.load_state(r);
    renderer_.load_state(r);   // includes ULA

    // Peripheral subsystems.
    copper_.load_state(r);
    // G109: mirror the just-loaded NR 0x64 cu_offset onto VideoTiming so
    // the line-int comparator stays consistent with Copper across save/load.
    video_timing_.set_cu_offset(copper_.offset());
    ctc_.load_state(r);
    dma_.load_state(r);
    spi_.load_state(r);
    i2c_.load_state(r);
    rtc_.load_state(r);
    uart_.load_state(r);
    divmmc_.load_state(r);

    // Audio subsystems.
    beeper_.load_state(r);
    turbosound_.load_state(r);
    dac_.load_state(r);

    // Emulator private state.
    frame_cycle_      = r.read_u64();
    frame_num_        = r.read_u32();
    psg_accum_        = r.read_u64();
    sample_accum_     = r.read_u64();
    dac_enabled_      = r.read_bool();
    // G71: line_int state owned by video_timing_; mirror through the
    // existing save layout (bool + u16) for backwards compatibility.
    const bool     saved_line_int_en  = r.read_bool();
    ula_int_disabled_                 = r.read_bool();
    const uint16_t saved_line_int_tgt = r.read_u16();
    video_timing_.set_line_interrupt_enable(saved_line_int_en);
    video_timing_.set_line_interrupt_target(saved_line_int_tgt);
    video_timing_.set_interrupt_enable(!ula_int_disabled_);
    im2_hw_mode_      = r.read_bool();
    im2_vector_base_  = r.read_u8();
    r.read_bytes(im2_int_enable_, 3);
    r.read_bytes(im2_int_status_, 3);
    im2_c4_expbus_    = r.read_bool();
    nr_c6_uart_int_en_ = r.read_u8();
    clip_l2_idx_  = r.read_u8();
    clip_spr_idx_ = r.read_u8();
    clip_ula_idx_ = r.read_u8();
    clip_tm_idx_  = r.read_u8();

    nr_cc_dma_delay_on_nmi_    = r.read_bool();
    nr_cc_dma_delay_en_ula_    = r.read_u8();
    nr_cd_dma_delay_en_ctc_    = r.read_u8();
    nr_ce_dma_delay_en_uart1_  = r.read_u8();
    nr_ce_dma_delay_en_uart0_  = r.read_u8();
    im2_dma_delay_latched_     = r.read_bool();

    // Branch C: NR 0x08 read mirror.
    nr_08_stored_low_ = r.read_u8();

    // Agent H: joy_iomode_pin7 (NR 0x0B / CTC ch3) — VHDL zxnext.vhd:3516.
    // Phase 2 Wave 2 Agent E: pin7 now lives in IoMode; restore via the
    // dedicated save-state setter to keep the single-bool slot intact.
    iomode_.set_pin7_for_load(r.read_bool());

    // Audio Phase 1: I2s stub latched sample pair — appended at the
    // END matching save_state().
    i2s_.load_state(r);

    // NMI pipeline Phase 1 scaffold — matches the append order in
    // save_state().
    nmi_source_.load_state(r);
    prev_nmi_generate_n_ = r.read_bool();

    // G108 — port_ff_reg storage. Appended after prev_nmi_generate_n_.
    // Tolerate older saves that predate the field by checking eof()
    // first; they keep the reset default (port_ff_reg_ = 0).
    if (!r.eof()) {
        port_ff_reg_ = r.read_u8();
    }

    // G56-B — NR 0x10 coreid shadow. Appended after port_ff_reg_; saves
    // that predate this slot leave nr_10_coreid_ at its init() default
    // of 0x01 (VHDL zxnext.vhd:1133), which is the correct power-on
    // behaviour for a fresh boot.
    if (!r.eof()) {
        nr_10_coreid_ = r.read_u8();
    }

    // G55 — IO-trap registers. Same backwards-compat tolerance: older
    // saves predate these fields and we keep the reset defaults.
    if (!r.eof()) {
        nr_d8_io_trap_fdc_en_ = r.read_bool();
    }
    if (!r.eof()) {
        nr_d9_iotrap_write_ = r.read_u8();
    }
    if (!r.eof()) {
        nr_da_iotrap_cause_ = r.read_u8();
    }

    // G112 — NR 0x2D I2S sample latch. Saves predating this slot leave
    // the field at its init() default of 0.
    if (!r.eof()) {
        nr_2d_i2s_sample_ = r.read_u8();
    }

    // G113 — NR 0xA2 Pi I2S control byte. Saves predating this slot
    // leave I2s.nr_a2_ctl_ at its reset() default of 0 — which is the
    // VHDL power-on value (no Pi I2S enabled, so pi_audio_L/R are at
    // the 10-bit DC midpoint 0x200, silence).
    if (!r.eof()) {
        i2s_.set_nr_a2_ctl(r.read_u8());
    }

    // G135 — NR 0xA0 Pi peripheral enable byte. Saves predating this slot
    // leave nr_a0_pi_peripheral_en_ at its reset() default of 0 (VHDL
    // power-on, zxnext.vhd:5080 — no Pi peripherals enabled).
    if (!r.eof()) {
        nr_a0_pi_peripheral_en_ = r.read_u8();
    }
    // G138 — re-sync the I2cController gate from the loaded byte. Older
    // snapshots fall through with nr_a0_pi_peripheral_en_ == 0, matching
    // the i2c_.reset() default of pi_i2c1_en_ = false.
    i2c_.set_pi_i2c1_en((nr_a0_pi_peripheral_en_ & 0x08) != 0);

    // Wave 1 B1 — Multiface subsystem state. Sentinel byte gates the
    // block so older snapshots fall through cleanly with the constructor-
    // init defaults preserved.
    if (!r.eof()) {
        const uint8_t sentinel = r.read_u8();
        if (sentinel == 0x01) {
            multiface_.load_state(r);
        }
    }
}

// ---------------------------------------------------------------------------
// Rewind / backwards execution
// ---------------------------------------------------------------------------

void Emulator::resize_rewind_buffer(int frames)
{
    if (frames <= 0) {
        rewind_buffer_.reset();
        rewind_enabled_ = false;
        Log::emulator()->info("Rewind buffer: disabled");
        return;
    }
    StateWriter measure;
    save_state(measure);
    size_t snap_bytes = measure.position();
    rewind_buffer_ = std::make_unique<RewindBuffer>(
        static_cast<size_t>(frames), snap_bytes);
    rewind_enabled_ = true;
    trace_log_.set_enabled(true);
    Log::emulator()->info("Rewind buffer resized: {} frames × {} bytes = {} KB",
        frames, snap_bytes,
        (static_cast<uint64_t>(frames) * snap_bytes + 512) / 1024);
}


uint64_t Emulator::rewind_to_cycle(uint64_t target_cycle)
{
    if (!rewind_buffer_ || rewind_buffer_->empty()) {
        Log::emulator()->warn("rewind_to_cycle: rewind buffer is empty or disabled");
        return UINT64_MAX;
    }

    // Restore the nearest snapshot at or before target_cycle.
    uint64_t snap_cycle = rewind_buffer_->restore_nearest(target_cycle, *this);

    Log::emulator()->debug("rewind_to_cycle: target={} snap_cycle={}", target_cycle, snap_cycle);

    if (snap_cycle > target_cycle) {
        // Oldest snapshot is newer than target — clamp to oldest available.
        Log::emulator()->warn("rewind_to_cycle: target {} older than oldest snapshot {}; clamping",
                               target_cycle, snap_cycle);
        target_cycle = snap_cycle;
    }

    // Fast-forward from the snapshot to target_cycle in replay mode.
    // replay_mode_ suppresses audio mixing and video rendering.
    // The debug state runs to the target cycle, then pauses automatically.
    replay_mode_ = true;
    debug_state_.set_active(true);
    debug_state_.run_to_cycle(target_cycle);

    // Run frames until the debugger pauses (target cycle reached) or we
    // somehow overshoot (should not happen, but guard against infinite loop).
    constexpr int MAX_REPLAY_FRAMES = 100000;
    int frames = 0;
    while (!debug_state_.paused() && frames < MAX_REPLAY_FRAMES) {
        run_frame();
        ++frames;
    }

    replay_mode_ = false;

    // Re-render the frame so the main window framebuffer reflects the rewound state.
    renderer_.render_frame(framebuffer_.data(), mmu_, ram_, palette_,
                           layer2_, &sprites_, &tilemap_);

    uint64_t reached = clock_.get();
    Log::emulator()->debug("rewind_to_cycle: reached cycle {} after {} replay frames",
                            reached, frames);
    return reached;
}

bool Emulator::step_back(int n)
{
    if (n <= 0) n = 1;

    if (!rewind_buffer_ || rewind_buffer_->empty()) {
        Log::emulator()->warn("step_back: rewind buffer is empty or disabled");
        return false;
    }

    if (!trace_log_.enabled() || trace_log_.size() == 0) {
        Log::emulator()->warn("step_back: trace log not enabled or empty");
        return false;
    }

    // The trace has entries [0..size()-1] where size()-1 is the most recent.
    // The current instruction (just executed) is at size()-1.
    // "Step back 1" means go to the instruction before that: size()-2.
    // But after a rewind we want to land on that instruction's *start* cycle.
    size_t trace_size = trace_log_.size();

    // Clamp n to available trace depth.
    if (static_cast<size_t>(n) > trace_size) {
        n = static_cast<int>(trace_size);
        if (n <= 0) {
            Log::emulator()->warn("step_back: not enough trace entries");
            return false;
        }
    }

    // The trace records each instruction BEFORE it executes.
    // trace[size-1] = last executed instruction.
    // step_back(N) = land at trace[size-N] = undo the last N instructions.
    // The CPU will be positioned to execute that instruction again (PC = trace[size-N].pc).
    size_t target_idx = trace_size - static_cast<size_t>(n);
    uint64_t target_cycle = trace_log_.at(target_idx).cycle;

    Log::emulator()->debug("step_back({}): target trace idx={} cycle={}", n, target_idx, target_cycle);

    // Clear the trace before rewind: entries above target_idx are stale "future" state.
    trace_log_.clear();

    rewind_to_cycle(target_cycle);
    return true;
}

bool Emulator::rewind_to_frame(uint32_t target_frame_num)
{
    if (!rewind_buffer_ || rewind_buffer_->empty()) {
        Log::emulator()->warn("rewind_to_frame: rewind buffer is empty or disabled");
        return false;
    }

    if (target_frame_num < rewind_buffer_->oldest_frame_num() ||
        target_frame_num > rewind_buffer_->newest_frame_num()) {
        Log::emulator()->warn("rewind_to_frame: frame {} out of range [{},{}]",
                               target_frame_num,
                               rewind_buffer_->oldest_frame_num(),
                               rewind_buffer_->newest_frame_num());
        return false;
    }

    // A frame snapshot's frame_cycle is the cycle at which that frame *started*.
    // Find the snapshot whose frame_num matches and use its frame_cycle as target.
    // restore_nearest will land us at the start of that frame.
    uint64_t snap_cycle = rewind_buffer_->restore_nearest(
        rewind_buffer_->newest_frame_cycle(), *this);

    // We need the exact frame_cycle for frame target_frame_num.
    // Use restore_nearest with a cycle one frame before the target to find it.
    // Actually: since we want to land at the start of target_frame_num, and
    // frame snapshots are taken at frame_cycle_ before the frame runs, we can
    // use restore_nearest with any cycle >= target_frame_num's frame_cycle.
    // The simplest approach: restore_nearest finds the latest snapshot <=
    // target_cycle. We want exactly frame target_frame_num — search for it.
    // Re-restore using the oldest_frame_cycle as sentinel.
    (void)snap_cycle;

    // Restore: find the cycle of target_frame_num by doing a targeted restore.
    // restore_nearest(oldest_frame_cycle + target_frame * CYCLES_PER_FRAME)
    // is an approximation — instead use the actual frame_cycle from the buffer.
    uint64_t target_cycle = rewind_buffer_->frame_cycle_for(target_frame_num);
    if (target_cycle == UINT64_MAX) {
        Log::emulator()->warn("rewind_to_frame: frame {} not found in buffer", target_frame_num);
        return false;
    }

    // Restore the snapshot and pause right at frame start (no fast-forward needed —
    // frame snapshots are taken at the exact cycle before any execution).
    uint64_t snap = rewind_buffer_->restore_nearest(target_cycle, *this);
    (void)snap;

    // Re-render so the main window framebuffer reflects the restored state.
    renderer_.render_frame(framebuffer_.data(), mmu_, ram_, palette_,
                           layer2_, &sprites_, &tilemap_);

    // Pause the debugger at the current position.
    debug_state_.set_active(true);
    debug_state_.pause();
    return true;
}

bool Emulator::start_recording(const std::string& output_path)
{
    if (!video_recorder_.start(output_path))
        return false;

    // Wire up audio capture: the mixer calls our callback for every sample.
    mixer_.set_record_callback([this](const int16_t* samples, int count) {
        video_recorder_.capture_audio(samples, count);
    });

    return true;
}

bool Emulator::stop_recording()
{
    // Disconnect audio capture callback first.
    mixer_.set_record_callback(nullptr);

    return video_recorder_.stop();
}

// ─── IM2 DMA delay composition (NR 0xCC/0xCD/0xCE + zxnext.vhd:1957-2007) ──
//
// STAGED WIRING — the NR 0xCC/0xCD/0xCE handlers above store the enable bits
// and `update_im2_dma_delay()` models the VHDL composition formula, but the
// function is NOT yet called from the emulator tick/interrupt path.  The
// three real inputs (im2_dma_int, nmi_activated, dma_delay) come from:
//   - IM2 interrupt controller firing AND-gated by `compose_im2_dma_int_en()`
//   - NMI edge (Z80 nmi request)
//   - dma_delay output of the Z80 RETN/RETI decoder
// Until those signals are threaded into a tick-level call to
// update_im2_dma_delay(), Dma::set_dma_delay() only ever receives false at
// runtime, i.e. the IM2 DMA deferral feature is inert outside of tests.
// This is acceptable as a staged landing: the plumbing, VHDL-correct
// composition, and coverage are in place; final signal wiring is tracked
// as Feature E in the SKIP-reduction plan.

uint16_t Emulator::compose_im2_dma_int_en() const
{
    // VHDL zxnext.vhd:1957-1958.
    //   im2_dma_int_en(13) = nr_ce_2_654(2)   -- UART1 Tx
    //   im2_dma_int_en(12) = nr_ce_2_210(2)   -- UART0 Tx
    //   im2_dma_int_en(11) = nr_cc_0_10(0)    -- ULA frame
    //   im2_dma_int_en(10..3) = nr_cd_1(7..0) -- CTC 7..0
    //   im2_dma_int_en(2)  = nr_ce_2_654(1) OR (0)  -- UART1 Rx / Rx-error
    //   im2_dma_int_en(1)  = nr_ce_2_210(1) OR (0)  -- UART0 Rx / Rx-error
    //   im2_dma_int_en(0)  = nr_cc_0_10(1)          -- line
    const uint8_t ce1 = nr_ce_dma_delay_en_uart1_ & 0x07;
    const uint8_t ce0 = nr_ce_dma_delay_en_uart0_ & 0x07;
    const uint8_t cc  = nr_cc_dma_delay_en_ula_   & 0x03;
    uint16_t m = 0;
    if (ce1 & 0x04) m |= (1u << 13);
    if (ce0 & 0x04) m |= (1u << 12);
    if (cc  & 0x01) m |= (1u << 11);
    m |= static_cast<uint16_t>(nr_cd_dma_delay_en_ctc_) << 3;
    if (ce1 & 0x03) m |= (1u << 2);
    if (ce0 & 0x03) m |= (1u << 1);
    if (cc  & 0x02) m |= (1u << 0);
    return m;
}

bool Emulator::update_im2_dma_delay(bool im2_dma_int, bool nmi_activated, bool dma_delay)
{
    // VHDL zxnext.vhd:2005-2007:
    //   im2_dma_delay <= im2_dma_int OR (nmi AND nr_cc_7) OR (im2_dma_delay AND dma_delay)
    const bool next = im2_dma_int
                      || (nmi_activated && nr_cc_dma_delay_on_nmi_)
                      || (im2_dma_delay_latched_ && dma_delay);
    im2_dma_delay_latched_ = next;
    dma_.set_dma_delay(next);
    return next;
}
