#include "core/emulator.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

#include "core/embedded_nextboot_rom.h"
#include "core/log.h"
#include "core/esxdos_trace.h"
#include "core/nex_loader.h"
#include "core/sd_rom_extractor.h"
#include "core/sdcard_provisioner.h"
#include "core/warm_start_cache.h"
#include "core/sna_saver.h"
#include "core/szx_saver.h"
#include "core/saveable.h"
#include "esp01/esp_threaded.h"
#include "peripheral/esp_host_policy.h"
#include "peripheral/esp_uart_adapter.h"
#include "peripheral/joy_uart_source.h"
#include "peripheral/joy_uart_link.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>

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

    // Task 19: phantom typist gets a back-pointer to Keyboard so it can
    // queue the per-machine LOAD"" sequence via the existing auto-type
    // machinery. Lifetime-bound (both members live as long as the
    // Emulator); reset() does NOT clear this pointer.
    phantom_typist_.attach(&keyboard_);
}

// ---------------------------------------------------------------------------
// Destructor — handles JNEXT_TRACE_DUMP_AT_EXIT env-gate (G46(b) trace infra).
// Reads the env var at destruction time only; zero overhead when unset.
// ---------------------------------------------------------------------------
Emulator::~Emulator()
{
    // G46(b) EOD-30i+29: JNEXT_TRACE_DUMP_AT_EXIT=/path/to/file
    //   If set, export the full trace-log ring buffer to a text file at
    //   emulator shutdown. Allows post-mortem grep/awk walks when combined
    //   with JNEXT_TRACE_FORCE_ENABLE + JNEXT_TRACE_CAPACITY.
    //   Zero cost when env var unset (single getenv check at teardown).
    if (const char* dump_path = std::getenv("JNEXT_TRACE_DUMP_AT_EXIT")) {
        if (trace_log_.size() > 0) {
            bool ok = trace_log_.export_to_file(dump_path);
            std::fprintf(stderr, "[trace] exported %zu entries to %s%s\n",
                         trace_log_.size(), dump_path,
                         ok ? "" : " (WRITE ERROR)");
        } else {
            std::fprintf(stderr, "[trace] JNEXT_TRACE_DUMP_AT_EXIT set but trace is empty"
                                 " (enable with JNEXT_TRACE_FORCE_ENABLE=1)\n");
        }
    }

    // Task 21 — flush the CPU profile histogram if active. Path comes
    // from EmulatorConfig::profile_output_path; defaults to "profile.dat".
    if (profiler_.active()) {
        profiler_.write_to_file(config_.profile_output_path);
    }
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

bool Emulator::init(const EmulatorConfig& cfg, bool preserve_memory)
{
    config_ = cfg;
    // The effective NR 0x03 machine timing (eff_nr_03_machine_timing) is a
    // latch loaded only at video_frame_sync, with no reset clause
    // (zxnext.vhd:6696-6703): a soft reset must hand it back unchanged.
    // Captured before the subsystem resets below rebuild ContentionModel.
    const MachineTimingMode soft_eff_tim = contention_.machine_timing();
    timing_ = machine_timing(cfg.type);
    Log::emulator()->info("Initializing emulator: machine_type={}[{}] cpu_speed={}[{}] "
                          "lines={} tstates/line={}{}",
                          static_cast<int>(cfg.type), machine_type_str(cfg.type),
                          static_cast<int>(cfg.cpu_speed), cpu_speed_str(cfg.cpu_speed),
                          timing_.lines_per_frame, timing_.tstates_per_line,
                          preserve_memory ? "  [preserve_memory=1 soft-reset]" : "");

    // Apply CPU speed to the clock.
    //
    // G46(b) 2026-05-08 — clock_/scheduler_/frame_cycle_ are NOT reset on
    // soft reset (preserve_memory=true). Per VHDL, the master PLL clock
    // is free-running and not in any reset domain; soft reset only resets
    // the FF-based subsystems. Resetting clock_ mid-frame caused the
    // run_frame() inner loop's local `frame_end` to become unreachable
    // (clock_ went from ~113M back to 0, but frame_end stayed at 113M),
    // turning every supervisor soft reset into a ~28M-iteration spin
    // that lost all scheduled INT events and stalled iff1=0 forever.
    if (!preserve_memory) {
        clock_.reset();
    }
    clock_.set_cpu_speed(cfg.cpu_speed);

    // Allocate the framebuffer and fill with black (ARGB: 0xFF000000) — but
    // not on a soft reset, which does not interrupt the video output: the
    // frame on screen stays there until the next one is drawn (GH #263
    // follow-up; with the debugger paused, F4 used to show black).
    if (!preserve_memory || framebuffer_.size() != FRAMEBUFFER_PIXELS)
        framebuffer_.assign(FRAMEBUFFER_PIXELS, 0xFF000000u);

    // Clear any stale scheduler events. Preserved across soft reset so
    // events scheduled before the reset (e.g. the current frame's ULA
    // CPU_INT) continue to fire — VHDL-faithful: the PLL keeps running
    // and ULA timing counters are not in the reset domain. See clock_
    // comment above.
    if (!preserve_memory) {
        scheduler_.reset();
        irq_scheduler_.reset();
        frame_cycle_ = 0;
        frame_num_   = 0;
        // No frame is in flight after a hard reset. A soft reset keeps the
        // clock, the scheduler and frame_cycle_, so the frame it lands in
        // goes on to its end: clearing the flag made the next run_frame()
        // after a mid-frame debugger pause run begin_new_frame() in the
        // middle of it, wiping the render history rows already drawn had
        // left and re-scheduling the frame's events (GH #263).
        frame_in_progress_ = false;
        // Host-side, like the flag above: a guest soft reset that a rewind
        // re-executes must not end the replay, or the rest of it renders,
        // mixes audio, re-sends ESP traffic and pushes rewind snapshots.
        replay_mode_ = false;
    }
    boot_hold_frames_remaining_ = 0;  // G156
    cpu_parked_ = false;              // GH #164

    // NR 0x07 returns to its power-on value (3.5 MHz) on every reset, soft
    // included (zxnext.vhd:1300 `nr_07_cpu_speed` reset "00"), so the log
    // change-gate must return with it — otherwise the guest's first write
    // back to the pre-reset speed would be swallowed as "unchanged" and never
    // logged. It lived only in the since-removed in-place Emulator::reset()
    // (GH #239), so a soft reset left the gate stale.
    last_logged_cpu_speed_ = 0;

    // Task 60e: any (re)initialisation re-establishes a clean machine, so a
    // pending "corrupt after failed rewind" flag (Task 60b) is cleared — the
    // recovery path the GUI tells the user to take.
    last_state_error_.clear();

    // GH #29/#84: direct-NEX host state is execution context, not machine
    // state. Every (re)initialisation — soft reset, and the loaders that
    // re-initialise before applying a file — returns file and SD traffic to
    // the configured card/ROM. load_nex() re-arms both afterwards.
    extended_nex_host_.clear();
    sd_card_.clear_read_overlay();
    direct_nex_esxdos_ = false;   // GH #250 — same lifetime as the bridge

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
    // GH #263 — the video owners take the same hard/soft split. A soft reset
    // keeps the BRAMs (palette RAM, sprite attribute and pattern RAM — no
    // reset port, like the bank-5/7 BRAMs above) and, because it can land
    // mid-frame, keeps each owner's per-scanline render history and records
    // the register reset in it at the current line. A hard reset happens only
    // between frames, and the next begin_new_frame() rebuilds that history.
    palette_.reset(/*hard=*/!preserve_memory);
    layer2_.reset(/*hard=*/!preserve_memory);
    sprites_.reset(/*hard=*/!preserve_memory);
    tilemap_.reset(/*hard=*/!preserve_memory);
    copper_.reset();
    cpu_.reset(/*hard=*/!preserve_memory);
    im2_.reset();
    // V20-IM2-01 — reset pulse-mode edge-detect shadow (init path).
    prev_pulse_int_n_ = true;
    keyboard_.reset();
    // GH #233 — the phantom typist is host-side input state bound to the
    // keyboard it drives, so it belongs with keyboard_.reset(): a reset
    // cancels a pending TAP autostart instead of typing LOAD"" into the
    // machine that came up after it. Pre-fix the call lived ONLY in the
    // in-place Emulator::reset() (since removed, GH #239), so a soft reset —
    // which routes through init(preserve_memory=true) and never went
    // through that method — left an armed typist running.
    //
    // Unconditional (no preserve_memory guard) and safe against the one
    // ordering that could disarm a legitimate autostart: `arm()` has a
    // single caller, Emulator::load_tap(), and every load path runs it
    // strictly AFTER init(). The frontend cold boot re-inits the machine
    // first and only then schedules the load
    // (platform/emulator_boot.h:176-181), the CLI does the same via
    // set_pending_load(), and load_tap() itself calls no reset/init.
    phantom_typist_.reset();
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
    if (cfg.dac_write_callback) {
        dac_.set_write_callback([this, callback = cfg.dac_write_callback]
                                (int channel, uint8_t value) {
            callback(monotonic_tstates(), channel, value);
        });
    } else {
        dac_.set_write_callback({});
    }
    i2s_.reset();
    mixer_.reset();
    mixer_.set_output_gain_db(cfg.audio_gain_db);
    mixer_.set_beeper_gain_db(cfg.audio_gain_beeper_db);
    for (int chip = 0; chip < 3; ++chip)
        mixer_.set_ay_gain_db(chip, cfg.audio_gain_ay_db[chip]);
    mixer_.set_dac_gain_db(cfg.audio_gain_dac_db);
    mixer_.set_capture_callback(cfg.audio_capture_callback);
    ctc_.reset();
    dma_.reset();
    spi_.reset();
    i2c_.reset();
    uart_.reset();
    divmmc_.reset();
    // DivMMC ROM/RAM are physical Next SRAM, not separate immutable
    // buffers. Config-mode NR $04=$04 maps page 0x08, the same 8K page
    // exposed as the DivMMC ROM overlay. Keeping a shared view is required
    // for firmware-loaded enNxtmmc.rom and for direct-NEX loaders such as
    // Atic Atac Next, which installs its own stackless-NMI handler there.
    // Standalone machines retain DivMmc's private ROM fallback.
    divmmc_.set_rom_backing(
        cfg.type == MachineType::ZXN_ISSUE2 ? ram_.page_ptr(0x08) : nullptr);

    // NextZXOS-boot fix (2026-07-09, ZXGO-COMPARISON doc): DivMMC RAM is
    // physical SRAM pages 16-31 on real hardware — the same pages the
    // config-mode NR $04 window ($08-$0F) routes to. Back the DivMMC RAM
    // view with those pages so tbblue.fw's firmware-module installs
    // (NextZXOS ROM3 $3D00 dispatch trampoline, IRQ stub) are visible to
    // the runtime overlay. Idempotent; content survives soft reset.
    divmmc_.set_ram_backing(ram_.page_ptr(16));
    // G46(b) 2026-05-09 — Multiface RAM is not in the soft-reset domain
    // per VHDL multiface.vhd:103 (only the FFs reset on reset_i).
    // Pass hard=!preserve_memory so soft reset preserves MF RAM contents,
    // matching real hardware where the FPGA's flash-baked SRAM is not
    // wiped by NR 0x02 ← 0x01.
    multiface_.reset(/*hard=*/!preserve_memory);
    // Task 26 item 5 — back the Multiface memory window with external SRAM
    // pages 0x0A (ROM half, read-only) / 0x0B (RAM half) on the Next, per
    // VHDL zxnext.vhd:3029-3036 (sram_pre_A21_A13 <= "00000101" & cpu_a(13),
    // sram_pre_bank5 forced '0' → external SRAM, not the bank-5 VRAM). This
    // unblocked by the Task 25 bank-5 un-aliasing (pages 0x0A/0x0B are now
    // free plain SRAM). tbblue.fw loads enNextMf.rom into page 0x0A via the
    // config-mode NR $04=$05 window during boot, so the MF ROM window reads
    // the firmware-loaded content — exactly as real hardware. Mirrors the
    // DivMmc set_ram_backing(ram_.page_ptr(16)) precedent. STANDALONE
    // machines (48k/128k/plus3) modelled a real standalone Multiface with
    // its OWN RAM/ROM chip → keep the private buffers (backing null), and
    // clear any stale backing on a live machine-type switch. Idempotent;
    // set on every init so soft reset preserves it.
    if (cfg.type == MachineType::ZXN_ISSUE2) {
        multiface_.set_rom_backing(ram_.page_ptr(0x0A));
        multiface_.set_ram_backing(ram_.page_ptr(0x0B));
    } else {
        multiface_.set_rom_backing(nullptr);
        multiface_.set_ram_backing(nullptr);
    }
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
    // G46(b) 2026-05-09 — The SD card is an external SPI peripheral, NOT
    // in the FPGA reset domain. A soft reset (NR 0x02 ← 0x01) pulls CS
    // high momentarily but does NOT reset the card's internal state —
    // the `initialized_` flag (post-CMD0/ACMD41/CMD58 handshake state)
    // must survive soft reset. Pre-fix, NextZXOS supervisor's deliberate
    // soft reset (after "Loading ROM: enNextZX.rom...OK!") wiped this
    // flag, so subsequent CMD17/CMD18 reads returned R1=$01 (idle) and
    // the supervisor fell into a permanent CMD0/CMD8/ACMD41 re-init
    // storm at PC=$18FB (102 CMD0 cycles in 4 s).
    if (!preserve_memory) {
        sd_card_.reset();
    }

    renderer_.reset(/*hard=*/!preserve_memory);   // GH #263, see palette_ above

    psg_accum_ = 0;
    sample_accum_ = 0;
    dac_enabled_ = false;
    // PASS-7 NR 0x08 reset preservation. Same shape as NR 0x06 / NR 0x05 /
    // NR 0x09. VHDL zxnext.vhd:1115-1120 declares six NR 0x08 sub-signals:
    //   nr_08_contention_disable     [b6] init '0'   (RESET to '0' at :4935 —
    //                                                 NOT held in nr_08_stored_low_;
    //                                                 owned by mmu_.set_contention_disabled())
    //   nr_08_psg_stereo_mode        [b5] init '0'
    //   nr_08_internal_speaker_en    [b4] init '1'
    //   nr_08_dac_en                 [b3] init '0'
    //   nr_08_port_ff_rd_en          [b2] init '0'
    //   nr_08_psg_turbosound_en      [b1] init '0'
    //   nr_08_keyboard_issue2        [b0] init '0'
    // ONLY nr_08_contention_disable is in the reset block (line 4935); all
    // bits in `nr_08_stored_low_` (b5..b0) survive reset.  Pre-pass-7
    // jnext unconditionally re-applied 0x10 here on every soft reset,
    // wiping any user-set NR 0x08 b5/b4/b3/b2/b1/b0 (e.g. internal-speaker,
    // DAC enable, TurboSound enable).  Preserve the byte across soft reset
    // (preserve_memory=true == NR 0x02 ← 0x01); on hard reset / first boot
    // restore the power-on default 0x10 (b4=1 = internal_speaker_en).
    if (!preserve_memory) {
        nr_08_stored_low_ = 0x10;
    }
    // Note: `dac_enabled_` was unconditionally set false above. That is also
    // a same-shape divergence (nr_08_dac_en survives reset). Restore it from
    // the preserved nr_08_stored_low_ shadow so the DAC stays enabled across
    // soft reset if the user previously set b3.
    dac_enabled_ = (nr_08_stored_low_ & 0x08) != 0;

    // Reset clip window write indices.
    clip_l2_idx_ = clip_spr_idx_ = clip_ula_idx_ = clip_tm_idx_ = 0;

    // G56: NR 0x10 coreid — VHDL zxnext.vhd:1133 declares
    //   `signal nr_10_coreid : std_logic_vector(4 downto 0) := "00001";`
    // The `:= "00001"` is a power-on (initial-value) assignment; the
    // process at zxnext.vhd:5677-5687 has NO reset clause, so coreid
    // SURVIVES both hard and soft reset. Pre-V16-NMP-01 this code
    // unconditionally re-applied 0x01 here, which differed from VHDL but
    // was masked because the readback came from the canonical write-time
    // cache byte (regs_[0x10]) — and NextReg::reset preserves regs_[0x10]
    // (PASS-8 path at nextreg.cpp:187 + 261). With V16-NMP-01 the read
    // mux now recomposes from the live `nr_10_coreid_` field, so this
    // line would clobber the user-set coreid on every reset. Removed to
    // honour VHDL initial-only semantics; the constructor's
    // `nr_10_coreid_{0x01}` default delivers the power-on byte once.

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
    // GH #231 — NR 0xCC/0xCD/0xCE IM2 DMA-delay enables. VHDL
    // zxnext.vhd:5101-5105 puts all five fields in the MASTER reset block:
    //   nr_cc_dma_int_en_0_7    <= '0'
    //   nr_cc_dma_int_en_0_10   <= (others => '0')
    //   nr_cd_dma_int_en_1      <= (others => '0')
    //   nr_ce_dma_int_en_2_654  <= (others => '0')
    //   nr_ce_dma_int_en_2_210  <= (others => '0')
    // and zxnext.vhd:2003-2004 clears the im2_dma_delay flip-flop in the
    // same domain. That domain is ONE wire — `reset <= i_RESET`
    // (zxnext.vhd:1730), driven by `reset_hard or reset_soft`
    // (zxnext_top_issue2.vhd:840) — so these clear on soft reset exactly
    // as on hard reset. Hence unconditional, with no preserve_memory
    // guard, unlike the deliberately-preserved NR fields elsewhere in
    // init(). Pre-fix the only writers were the NR 0xCC/0xCD/0xCE write
    // handlers and load_state(), so a stale DMA-delay configuration
    // survived every reset while the NR read-back handlers kept
    // recomposing it from these fields.
    //
    // The mirrors these values fan out to are already cleared by the
    // subsystem resets run above: Im2Controller::reset() zeroes
    // dma_int_en_mask14_ / nr_cc_dma_int_en_0_7_ / its own im2_dma_delay
    // latch (im2.cpp:45-48), and Dma::reset() clears dma_delay_
    // (dma.cpp:74). NextReg::reset()'s regs_.fill(0) leaves the cached
    // 0xCC/0xCD/0xCE bytes at 0, which is what these cleared fields now
    // recompose to — cache and fields agree again.
    nr_cc_dma_delay_on_nmi_   = false;
    nr_cc_dma_delay_en_ula_   = 0;
    nr_cd_dma_delay_en_ctc_   = 0;
    nr_ce_dma_delay_en_uart1_ = 0;
    nr_ce_dma_delay_en_uart0_ = 0;
    im2_dma_delay_latched_    = false;
    // V19-IM2-02 init: VHDL zxnext.vhd:6711 — `ula_int_en(0) =
    // NOT port_ff_interrupt_disable` = NOT port_ff_reg(6). With
    // port_ff_reg=0 at reset, ULA int_en=1. Fan this initial value
    // into the IM2 fabric so dev_[ULA].int_en starts at true. Pre-fix
    // im2_.reset() left dev_[ULA].int_en=false (default), and no
    // subsequent code wrote it — meaning every FRAME-INT raise_req(ULA)
    // in IM2 mode silently dropped because the wrapper's edge-detect
    // would not latch im2_int_req without int_en. The runtime writers
    // (port-FF / NR 0x22 b2 / NR 0xC4 b0) now also fan into IM2 fabric
    // (V19-IM2-02 fix), so this init brings them into a coherent state
    // before the first software access.
    im2_.set_int_en(Im2Controller::DevIdx::ULA,
                    (port_ff_reg_ & 0x40) == 0);
    // V19-IM2-01 init: NR 0x22 bit 1 + NR 0xC4 bit 1 share the
    // nr_22_line_interrupt_en flip-flop (VHDL :5297, :5610), which feeds
    // dev_[LINE].int_en (line :1950). The flip-flop default is 0 (line
    // :4983). Honour that explicitly so the IM2 fabric's LINE int_en
    // matches both the line-int generation gate and the VHDL boot state.
    im2_.set_int_en(Im2Controller::DevIdx::LINE, false);
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
    //
    // GH #232 — the machine type is NextREG state, not a CLI constant.
    // There is no `machine_type` input pin in the VHDL: `nr_03_machine_type`
    // is a plain FF with an initial value only (zxnext.vhd:1103,
    // `:= "011"`), it appears NOWHERE in the master reset block
    // (:4926-5111), and the single reset wire covers hard AND soft alike
    // (`reset <= i_RESET` :1730, `reset_hard or reset_soft`
    // zxnext_top_issue2.vhd:840). So it survives a reset and only firmware
    // writing NR 0x03 under config_mode (:5137-5145) changes it. jnext
    // already models exactly that: mmu_.set_machine_type() below is
    // hard-reset-only, and the NR 0x03 commit path
    // (`contention_.rebuild_for_type(new_mt)`) and load_state()
    // (`contention_.rebuild_for_type(mmu_.machine_type())`) both take the
    // Mmu's value as canonical. Rebuilding from cfg.type on a soft reset
    // was the one path that disagreed — it unwound a supervisor-committed
    // type in the ContentionModel while leaving it committed in the Mmu,
    // with config_mode correctly cleared so the guest could not re-commit
    // NR 0x03 to reconcile them. Take the same authority the other two
    // paths take.
    const MachineType init_machine_type =
        preserve_memory ? mmu_.machine_type() : cfg.type;
    contention_.build(init_machine_type);
    contention_.set_cpu_speed(static_cast<uint8_t>(cfg.cpu_speed) & 0x03);
    // ContentionModel's port_7ffd_io_en / port_ulap_io_en gates (build()
    // clears the first) are seeded near the END of init(), with the DivMMC
    // and Multiface enables, once expbus_eff_en is known — see there.

    // GH #237 — the VideoTiming seed used to live HERE, as
    // `video_timing_.init(cfg.type, false)`. It now runs below, once
    // `init_tim_mode` (the NR 0x03 tim_sel axis) has been established, via
    // apply_video_timing(). See the comment at that call site.

    // Task 58 — seed the effective (frame-edge-latched) copy of NR 0x05
    // bit 0 from the pending cache. VHDL zxnext.vhd:6702 latches
    // `eff_nr_05_scandouble_en <= nr_05_scandouble_en` at every
    // video_frame_sync; init() models the post-boot steady state where
    // effective == pending (cold-boot default 0x41 → bit 0 = '1',
    // zxnext.vhd:1303). Kept in sync per-frame by begin_new_frame().
    eff_nr_05_scandouble_en_ = (nextreg_.cached(0x05) & 0x01) != 0;
    // Same latch, same edge, for NR 0x09 bits 1:0 (zxnext.vhd:6701).
    eff_nr_09_scanlines_ = static_cast<uint8_t>(nextreg_.cached(0x09) & 0x03);

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
    //
    // G46(b): Only push the CLI machine_type on HARD reset. On soft reset
    // (preserve_memory=true), nr_03_machine_type is NextReg state owned by
    // the NR $03 write handler — overwriting it here would unwind a
    // supervisor-committed type (e.g., NextZXOS sets +3 mode mid-boot)
    // back to the boot-time CLI default. Real Next preserves the type
    // across NR $02 soft reset; we follow.
    //
    // Review-finding-3: also push the matching typ_sel into NextReg's
    // nr_03_machine_type field. Otherwise on hard reset Mmu starts at
    // cfg.type but NextReg's field is preserved (per VHDL :1103) — a
    // divergence where NR $03 read-back disagrees with actual routing.
    //
    // Task 26 (P3, 2026-07-11): the actual ZX Spectrum Next (ZXN_ISSUE2)
    // must cold-boot NR $03 machine-type to the VHDL FPGA power-on default
    // "011" (=+3), NOT a Pentagon/128K-class value. VHDL zxnext.vhd:1103:
    //   signal nr_03_machine_type : std_logic_vector(2 downto 0) := "011";
    // This initial value is machine-agnostic silicon state and is NOT
    // re-asserted by the soft/hard reset block (zxnext.vhd:4926-5111 has no
    // nr_03_machine_type assignment) — it persists until firmware writes NR
    // $03 while config_mode=1 (:5137-5145). On real hardware the field
    // therefore reads "011" until NextZXOS commits the true type; the prior
    // 0x04 push made jnext's Next read "100" (=128K per the :5741-5757
    // decode) at cold boot, diverging from silicon. The divergence was
    // masked on the boot path because NextZXOS explicitly commits a value
    // early — a "works by luck" gap. The 48k/128k/plus3 cases keep their
    // matching typ_sel: those are jnext machine abstractions selected at
    // the CLI, and NR $03 read-back reflecting that choice is intended.
    if (!preserve_memory) {
        mmu_.set_machine_type(cfg.type);
        uint8_t typ_sel = 0x03;  // ZXN_ISSUE2 → +3 "011" (VHDL :1103 power-on)
        switch (cfg.type) {
            case MachineType::ZX48K:      typ_sel = 0x01; break;
            case MachineType::ZX128K:     typ_sel = 0x02; break;
            case MachineType::ZX_PLUS3:   typ_sel = 0x03; break;
            case MachineType::ZXN_ISSUE2: typ_sel = 0x03; break;  // +3 per VHDL power-on default
        }
        nextreg_.set_nr_03_machine_type(typ_sel);

        // V24-MEM-01 / V25-MEM-01 fix — also seed nr_03_machine_timing
        // (tim_sel axis) coherently with the CLI machine type. The
        // canonical NextZXOS boot path always writes NR 0x03 with
        // matching tim_sel == typ_sel, so init aligns both axes from a
        // single CLI choice. Without this, NextReg's
        // nr_03_machine_timing stays at the VHDL :1099 default 0x03
        // (+3 timing) regardless of cfg.type — a divergence that
        // pre-V24-MEM-01 was silently masked because the contention
        // surfaces consumed typ_sel-derived MachineType. Now that
        // contention consumes the tim_sel axis, this push keeps the
        // two axes coherent at boot.
        uint8_t tim_sel = 0x03;  // ZXN_ISSUE2 default → +3 (VHDL :1099)
        switch (cfg.type) {
            case MachineType::ZX48K:      tim_sel = 0x01; break;
            case MachineType::ZX128K:     tim_sel = 0x02; break;
            case MachineType::ZX_PLUS3:   tim_sel = 0x03; break;
            case MachineType::ZXN_ISSUE2: tim_sel = 0x03; break;  // Next → +3 timing (VHDL default)
        }
        nextreg_.set_nr_03_machine_timing(tim_sel);
    }

    // V24-MEM-01 / V25-MEM-01 fix — seed both ContentionModel and Mmu
    // machine_timing fields from the canonical NextReg cached value.
    // Re-derive from `nr_03_machine_timing` (just-set above) rather
    // than from cfg.type so the typ_sel/tim_sel pairing stays
    // single-sourced through NextReg even on soft reset
    // (preserve_memory=true), where the NR 0x03 fields are
    // intentionally NOT re-derived from cfg.type (matching the
    // machine_type preservation semantics at :333-352 above).
    //
    // After init, the two axes evolve independently via NR 0x03's two
    // gated commit paths (zxnext.vhd:5124-5135 for tim_sel vs
    // :5137-5145 for typ_sel). A user that writes NR 0x03 with
    // bits 6:4 != bits 2:0 will see them diverge at the next
    // video-frame edge — observably faithful to VHDL.
    //
    // A soft reset keeps the effective timing it found and leaves the pending
    // NR 0x03 value pending: eff_nr_03_machine_timing loads only at
    // video_frame_sync and has no reset clause (zxnext.vhd:6696-6703), so a
    // guest that writes NR 0x03 and soft-resets in the same frame gets the
    // new timing at the next frame edge, like any other NR 0x03 write
    // (GH #263 follow-up — it used to be applied at the reset).
    const MachineTimingMode pend_tim_mode =
        decode_nr_03_machine_timing(nextreg_.nr_03_machine_timing());
    const MachineTimingMode init_tim_mode =
        preserve_memory ? soft_eff_tim : pend_tim_mode;
    contention_.set_machine_timing(init_tim_mode);
    mmu_.set_machine_timing(init_tim_mode);
    contention_.set_pending_machine_timing(pend_tim_mode);
    mmu_.set_pending_machine_timing(pend_tim_mode);

    // Pulse-mode INT width gate per VHDL zxnext.vhd:2033 — 48K/+3 use 32 CPU
    // cycles (bit 5 only); 128K/Pentagon/Next use 36 (bit 5 AND bit 2).
    // Fan out to BOTH the IM2 fabric (which models the FPGA pulse counter)
    // AND Z80Cpu (which uses the same width to expire a pending /INT that
    // was never acknowledged, e.g. inside an ISR with iff1=0). Same VHDL
    // line, two consumers — must stay in lock-step.
    //
    // GH #232 — derive it from `init_tim_mode` (the tim_sel axis) rather
    // than from the CLI MachineType. VHDL :2033 reads `machine_timing_48 or
    // machine_timing_p3`, and those one-hot signals come combinationally
    // from `eff_nr_03_machine_timing` (:5761-5776) — the tim_sel axis, never
    // from `machine_type_*`. Keying on cfg.type was wrong on both counts:
    // wrong axis, and wrong source on a soft reset (where NR 0x03 is
    // preserved). It also disagreed with the two paths that already read
    // NR 0x03 for this same gate — the NR 0x03 write handler and
    // load_state() — so on the Next, whose NR 0x03 tim_sel powers on at
    // "011" (+3, zxnext.vhd:1099), a guest writing NR 0x03 back with the
    // value already in the register flipped the pulse width from 36 to 32
    // cycles. Reading the register the VHDL reads removes that seam.
    const bool is_48_or_p3_at_reset =
        (init_tim_mode == MachineTimingMode::Timing48 ||
         init_tim_mode == MachineTimingMode::TimingPlus3);
    im2_.set_machine_timing_48_or_p3(is_48_or_p3_at_reset);
    cpu_.set_machine_timing_48_or_p3(is_48_or_p3_at_reset);

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

    // GH #237 — seed ALL the raster/interrupt geometry from `init_tim_mode`
    // (the NR 0x03 tim_sel axis), not from the CLI MachineType.
    //
    // The VHDL leaves no room for interpretation here: zxula_timing's
    // `i_timing` port is driven by `eff_nr_03_machine_timing` and by nothing
    // else (zxnext.vhd:6721), that latch is loaded from
    // `nr_03_machine_timing` at every video_frame_sync (:6703), and
    // `nr_03_machine_timing` is a plain FF with an initial value only
    // (:1099 `:= "011"`), absent from the master reset block (:4926-5111).
    // `machine_type_*` (the typ_sel axis, :5741-5757) reaches the raster
    // generator through NO path at all. So the Next boots at tim_sel "011",
    // zxula_timing takes the `i_timing(1)='1'` branch and, within it, the
    // `i_timing(0)='1'` arm: `c_int_h <= 136+2-12` = 126
    // (zxula_timing.vhd:186-190), NOT the 128 of the `i_timing(0)='0'` arm.
    //
    // Pre-fix this ran ~140 lines above as `video_timing_.init(cfg.type,
    // false)`, whose `default_machine_timing_for()` maps ZXN_ISSUE2 to
    // Timing128 — contradicting the "011" init() itself seeds into NR 0x03
    // just above, and firing the Next's ULA interrupt one T-state late.
    //
    // Only `c_int_h` differs between the 128K and +3 arms: every other
    // constant in that branch (c_max_hc 455, c_max_vc 310, c_min_hactive
    // 136, c_min_vactive 64, c_int_v 1, and the blank/sync/hdmi group) sits
    // OUTSIDE the `if i_timing(0)` and is shared, at 50 Hz (:182-204) and at
    // 60 Hz (:216-238) alike.
    //
    // apply_video_timing() is the same helper the runtime frame-edge commit
    // uses, so the master-cycle frame length (`timing_`), the CPU-side frame
    // geometry and the Copper wrap can never disagree with VideoTiming — a
    // real hazard now that the mode is register-derived and no longer
    // pinned to cfg.type. It must follow z80_set_contention_runtime() above,
    // which (re)derives the CPU frame geometry from cfg.type.
    //
    // That ordering does more than avoid a clash, and the reach is worth
    // stating plainly. Before this change `z80_set_frame_geometry()` had
    // exactly ONE call site in the whole file — inside
    // repush_video_timing_from_machine_timing(), which never runs at boot
    // (begin_new_frame() only calls it when the effective tim_sel CHANGES,
    // and at boot it does not). So FUSE's frame geometry was seeded solely
    // by z80_set_contention_runtime()'s `machine_timing(cfg.type)`, i.e. by
    // the CLI machine type. Routing init() through apply_video_timing() is
    // therefore the first code to make the CPU-side frame geometry follow
    // the NR 0x03 tim_sel axis at boot, exactly as zxula_timing does
    // (zxnext.vhd:6721). It is a no-op for every hard reset — tim_sel is
    // seeded from cfg.type there and the two agree — and it is what keeps a
    // soft-reset-preserved Pentagon or 48K tim_sel from leaving the CPU
    // counting a different frame from the raster. Rows VT-GH237-05/07
    // assert the `emu.timing()` side of it.
    //
    // Task 50 — apply_video_timing() also hands the CPU side the two ULA
    // counter origins. The contention gate is written against the ULA's own
    // display-relative counters (VHDL i_hc/i_vc, reset at the start of the
    // ACTIVE DISPLAY — zxula_timing.vhd:423,441-452), but the CPU callbacks
    // derive raw frame-relative (hc, vc) from the FUSE T-state counter.
    //
    // The 60 Hz axis is deliberately NOT taken from NR 0x05 bit 2 here:
    // init() has always seeded 50 Hz, and begin_new_frame() already
    // re-pushes 60 Hz on the first frame edge when the pending bit says so
    // (`pend_60hz != video_timing_.refresh_60hz()`). Out of scope for #237.
    //
    // GH #263 — except on a soft reset, which keeps the effective 50/60 Hz
    // selection it found: `eff_nr_05_5060` has no reset clause and is only
    // loaded at video_frame_sync (zxnext.vhd:6696-6703), and zxula_timing
    // has no reset at all, so a reset mid-frame does not change the frame
    // being drawn. Seeding 50 Hz there switched a 60 Hz frame's geometry
    // (vblank_top 8 -> 32) under its remaining scanline events, misplacing
    // every row from the reset to the frame's end.
    apply_video_timing(init_tim_mode,
                       preserve_memory ? video_timing_.refresh_60hz() : false);

    // Clear all port dispatch handlers before re-registering them.
    // Without this, a second init() (a soft reset, a re-initialising load)
    // would duplicate every handler, causing double-fired writes (breaking
    // auto-increment ports like sprites/palette).
    port_.clear_handlers();
    port_.clear_io_observers();

    // Genuinely undecoded port reads return X"FF" (GH #109).
    // VHDL zxnext.vhd:1868-1878 — the IORQ arm of the cpu_di mux: when no
    // internal port decode responds (`port_internal_rd_response = '0'`,
    // :2803-2806) and no expansion-bus device drives the bus (jnext models
    // none), `cpu_di <= X"FF"` — unconditionally, in EVERY machine timing.
    // The Timex/floating-bus mux (:2813) is scoped to `port_ff_rd` only
    // (LSB-0xFF decode, :2583) and is served by the registered port-0xFF
    // read handler below — it must never leak into other unmatched ports.
    port_.set_default_read([this](uint16_t port) -> uint8_t {
        const uint8_t val = 0xFF;
        // G46(b) #102 session 3 probe (env-gated, zero cost when unset).
        if (g46b_port_trace_file_) {
            std::fprintf(g46b_port_trace_file_, "%ld,%04x,%04x,%02x\n",
                g46b_port_trace_frame_, cpu_.get_registers().PC, port, val);
        }
        return val;
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
    // the handler returns 0xFF: with mf_port_en=0 no internal decode
    // responds at these LSBs (Profi DAC owns only the WRITE half), so
    // `port_internal_rd_response=0` and the cpu_di mux falls to
    // `cpu_di <= X"FF"` (zxnext.vhd:1877; GH #109 — pre-fix these
    // fallbacks called floating_bus_read(), the port-0xFF-only mux).

    // ── MF+3 readback at LSB 0x3F (mf_type=00) — case mux on cpu_a(15:12) ──
    port_.register_handler(0x00FF, 0x003F,
        [this](uint16_t cpu_a) -> uint8_t {
            if (!multiface_.is_enabled() ||
                multiface_.mf_type() != 0x00 ||  // MF+3 only (case "00")
                multiface_.invisible_eff()) {
                return 0xFF;  // undecoded — cpu_di <= X"FF" (zxnext.vhd:1877)
            }
            const uint8_t a_high = static_cast<uint8_t>((cpu_a >> 12) & 0x0F);
            switch (a_high) {
                case 0x1: {
                    // 0x1xxx — port_1ffd readback (motor + reg(2:0)).
                    // VHDL zxnext.vhd:4312:
                    //   when "0001" => mf_port_dat <=
                    //     "0000" & (NOT port_1ffd_mtr_n) & port_1ffd_reg;
                    // VHDL zxnext.vhd:3744-3761 — the motor-N flip-flop is
                    // forced to '1' (motor off) on every clock while
                    // `nr_81_expbus_fdc = '0'` (the priority-2 elsif at
                    // :3751-3753), regardless of the most recent port 0x1FFD
                    // bit-3 write. Only when `nr_81_expbus_fdc = '1'` is the
                    // motor latch updated to `NOT cpu_do(3)` on the port
                    // 0x1FFD write at :3755-3757. Therefore:
                    //   nr_81_expbus_fdc = '1' (NR 0x81 bit 3 = 1):
                    //     readback bit 3 = NOT motor_n = cpu_do(3)
                    //   nr_81_expbus_fdc = '0' (default; FDC disabled):
                    //     readback bit 3 = NOT '1' = '0'
                    // V14-NMP-01 (Pass-14 verify-audit fix): pre-fix returned
                    // `port_1ffd_ & 0x0F` unconditionally, leaking the user's
                    // last cpu_do(3) write through bit 3 even when FDC was
                    // disabled (the jnext default — NR 0x81 power-on = 0x00).
                    // jnext doesn't model the FDC pin per se, but the motor
                    // latch is observable through this MF+3 readback path.
                    const uint8_t reg_lo3 = static_cast<uint8_t>(mmu_.port_1ffd() & 0x07);
                    const bool fdc_en = (nr_81_ & 0x08) != 0;
                    const uint8_t mtr_bit = fdc_en ? static_cast<uint8_t>(mmu_.port_1ffd() & 0x08)
                                                   : static_cast<uint8_t>(0x00);
                    return static_cast<uint8_t>(mtr_bit | reg_lo3);
                }
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
                return 0xFF;  // undecoded — cpu_di <= X"FF" (zxnext.vhd:1877)
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
                return 0xFF;  // undecoded — cpu_di <= X"FF" (zxnext.vhd:1877)
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
    // overrides in src/cpu/z80_cpu.cpp call it per cycle. Nothing is wired
    // here: the `cpu_.on_contention = nullptr` that used to sit at this point
    // was the last reference to a callback nobody called, and both it and the
    // Z80Cpu member are gone.

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
        // Task 27 C-M1 — fast-path gate: DivMmc::automap_m1_may_react()
        // proves (VHDL-cited proof in divmmc.h) that when the automap
        // FF state is quiescent AND pc is outside the static entry-point
        // candidate superset, check_automap() is a no-op — so both the
        // call and the per-M1 sram_pre_override_* computation below can
        // be skipped. The gate reads live DivMmc state; no invalidation
        // discipline is needed. The gate inputs (mf_active/config_mode/
        // override bits) are consumed ONLY by check_automap, so hoisting
        // their computation inside the gate is behaviour-preserving.
        if (divmmc_.automap_m1_may_react(pc)) {
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
                mmu_.sram_pre_override_romcs_priority(pc, mf_active,
                                                      config_mode);
            divmmc_.check_automap(pc, true,
                                  sram_pre_override_2,
                                  sram_pre_override_0);
        }
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
        ++slot_m1_count_;   // GH #265 — see Im2Controller::tick(..., m1_cycles)
        im2_.on_m1_cycle(pc, opcode);
        if (im2_.reti_seen_this_cycle()) {
            im2_.on_reti();
        }
        if (im2_.retn_seen_this_cycle()) {
            im2_.on_retn();
        }
        // G46(a) — defer the DivMMC clear until RETN has executed.
        // Im2Controller::retn_seen_this_cycle() pulses ONLY on the M1
        // that completes a canonical ED 45 (im2_control.vhd:236), so the
        // pre-G87 RETN-alias band-aid (which fired on any post-ED byte
        // matching 0x45/55/5D/65/6D/75/7D — e.g. a stray LD r,L after
        // ED) is fully retired. DivMmc latches the pulse here; the run
        // loop applies it immediately after cpu_.execute() returns.
        // This preserves RETN's own mapped fetch while ensuring JNext
        // cannot predecode the returned-to instruction through a stale
        // overlay.
        //
        // Wave 1 F-gate — VHDL zxnext.vhd:4111:
        //   `divmmc_retn_seen <= z80_retn_seen_28 AND NOT mf_is_active;`
        // When MF is active (mf_mem_en OR mf_nmi_hold per :2099), the
        // RETN pulse is suppressed for DivMMC's automap-clear path.
        // DivMMC stays in its current state until MF deactivates; MF's
        // own retn-clear path remains independently latched below.
        divmmc_.on_m1_retn_seen(im2_.retn_seen_this_cycle() && !multiface_.is_active());

        // Wave 1 B1 — latch the Multiface RETN clear too. Applying it
        // inside this pre-execution callback would make the CPU core
        // refetch RETN itself from the newly exposed underlying memory.
        if (im2_.retn_seen_this_cycle()) {
            multiface_retn_pending_ = true;
        }
    };

    // G88: latch NR 0xC2 (PC LSB) / NR 0xC3 (PC MSB) from the NMI return
    // address at NMI service time. Mirrors VHDL zxnext.vhd:2050-2085 +
    // 6232-6236 (Z80N_command_s = NMIACK_LSB/MSB & cpu_wr_n='0'). The CPU
    // hands us the post-HALT-fix PC that will be pushed to stack.
    cpu_.on_nmi_servicing = [this](uint16_t saved_pc) {
        nextreg_.set_nmi_return_address(saved_pc);
    };
    cpu_.stackless_nmi_enabled = [this]() {
        return im2_.stackless_nmi();
    };
    cpu_.stackless_nmi_return_address = [this]() -> uint16_t {
        return static_cast<uint16_t>(
            static_cast<uint16_t>(nextreg_.peek(0xC3)) << 8 |
            nextreg_.peek(0xC2));
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

    // GH #219 — --persistent-breakpoints. Latched here, once, from the config:
    // it is a run-long property of the machine, not a UI state the debugger
    // toggles. DebugState::armed() (the hot-path breakpoint gate) becomes
    // active() || this, so closing the debugger window no longer disarms.
    debug_state_.set_persistent_breakpoints(cfg.persistent_breakpoints);

    // Magic breakpoint: ED FF (ZEsarUX) / DD 01 (CSpect) trigger debugger pause.
    set_magic_breakpoint(cfg.magic_breakpoint);

    // Host-side compatibility for directly loaded NEX programs.
    //
    // Function codes (per z88dk libsrc + esxdos spec):
    //   $88 M_DOSVERSION : report a NextZXOS-compatible signature.
    //   $8F M_EXECCMD    : support "run sibling.nex" at the next frame edge.
    //   $9A F_OPEN       : open the stub's one in-memory file.
    //   $9B F_CLOSE      : close that file.
    //   $9D F_READ       : read the in-memory file.
    //   $9E F_WRITE      : write the in-memory file.
    //   $9F F_SEEK       : fail with carry set, A=5 (ENOENT).
    //   default          : not handled — RST $08 runs normal $0008 code.
    //
    // Task 85 — the hook is installed when the stub is enabled OR when esxdos
    // tracing is on. Tracing must not require the stub: the most informative
    // case is a NextZXOS-expecting program running WITHOUT the stub, where
    // every call falls through unhandled and is otherwise invisible.
    // GH #31 — --esxdos-stub-root. configure() closes every handle, which is
    // what a re-init (soft reset, re-entrant load) should do to them anyway.
    if (!cfg.esxdos_stub_root.empty()) {
        std::string why;
        if (!esxdos_hostfs_.configure(cfg.esxdos_stub_root,
                                      cfg.esxdos_stub_writable, why)) {
            Log::emulator()->error("--esxdos-stub-root: {}", why);
        } else {
            Log::emulator()->info(
                "esxdos host directory: {} ({})",
                esxdos_hostfs_.root().string(),
                cfg.esxdos_stub_writable ? "read-write" : "read-only");
            if (cfg.esxdos_stub_writable)
                Log::emulator()->warn(
                    "--esxdos-stub-writable: guest writes change host files "
                    "immediately; rewinding the emulator cannot undo them");
        }
    }
    const bool esxdos_tracing = Log::esxdos()->should_log(spdlog::level::trace);
    bool direct_nex_load = false;
    if (!cfg.load_file.empty()) {
        std::filesystem::path load_path(cfg.load_file);
        std::string extension = load_path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::error_code ec;
        direct_nex_load =
            extension == ".nex" && std::filesystem::is_regular_file(load_path, ec) && !ec;
    }
    {
        // `stub_enabled` is captured BY VALUE: this lambda is stored on the CPU
        // and long outlives init(), so a reference to cfg would dangle.
        const bool stub_enabled = cfg.esxdos_stub;
        auto handle_esxdos = [this, stub_enabled](uint8_t defb, Z80Registers& r) -> bool {
            const bool host_nex_available = !extended_nex_host_.path().empty();
            // GH #250 — a NEX loaded directly (load_nex(), armed until the
            // next reset) gets these answers without --esxdos-stub. On real
            // hardware a NEX is only ever started by NextZXOS's nexload, so
            // the esxDOS API is always there for it; with no OS behind a
            // direct load, an unanswered call ran the 48K ROM's ERROR-1 and
            // the program died (Warhawk: M_GETSETDRV, then DI + HALT).
            if (!stub_enabled && !host_nex_available && !direct_nex_esxdos_ &&
                !esxdos_hostfs_.active())
                return false;   // tracing/direct-load pre-arm only — service nothing
            // NextZXOS is reached through the DivMMC automap on $0008, which
            // only engages while ROM, not RAM, is mapped at $0000:
            // sram_divmmc_automap_rom3_en needs sram_pre_override(0), set
            // only by the ROM branch of the slot-0 decode (zxnext.vhd:3060-
            // 3066, :3138). A program with its own RAM at $0000 runs its own
            // RST $08 code there, so do not answer for it.
            if (mmu_.get_page(0) != 0xFF)
                return false;
            auto set_a_and_carry = [&](uint8_t a, bool carry_set) {
                uint8_t f = static_cast<uint8_t>(r.AF & 0xFF);
                if (carry_set) f |= 0x01; else f &= 0xFE;
                r.AF = static_cast<uint16_t>((static_cast<uint16_t>(a) << 8) | f);
            };
            // A dot command's COMMAND TAIL is "terminated by $00, $0d or ':'"
            // (tbblue src/asm/readdir/readdir.asm:78-79, describing the entry
            // convention every dot command is given). That triple belongs to
            // the command line and to nothing else.
            auto read_cmdline = [this](uint16_t address) {
                std::string value;
                for (int i = 0; i < 255; ++i) {
                    const char ch = static_cast<char>(mmu_.read(
                        static_cast<uint16_t>(address + i)));
                    if (ch == '\0' || ch == '\r' || ch == ':') break;
                    value.push_back(ch);
                }
                return value;
            };
            // A FILESPEC is NUL-terminated and nothing else — every esxDOS
            // header block says so in those words ("IX=filespec,
            // null-terminated", asm_esx_f_open.asm; likewise F_STAT, F_OPENDIR
            // and F_CHDIR). Splitting one on ':' truncates exactly the
            // drive-qualified form the API is built around: F_OPEN is
            // documented as "A=drive specifier (overridden if filespec
            // includes a drive)", so "c:/game/data.bin" is a normal argument,
            // and esxapi.def:129-130 assigns '*' and '$' as drive letters for
            // the same purpose. NextZXOS's own ROM carries literals of that
            // shape ("c:/nextzxos/autoexec.1st"). Reading a filespec with the
            // command-tail terminator set handed resolve() the single byte "c"
            // and every drive-qualified open failed ENOENT (GH #31 review).
            auto read_filespec = [this](uint16_t address) {
                std::string value;
                for (int i = 0; i < 255; ++i) {
                    const char ch = static_cast<char>(mmu_.read(
                        static_cast<uint16_t>(address + i)));
                    if (ch == '\0') break;
                    value.push_back(ch);
                }
                return value;
            };
            auto resolve_sibling = [this](const std::string& name) {
                const std::filesystem::path target(name);
                std::string extension = target.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (target.empty() || target.is_absolute() || target.has_parent_path() ||
                    extension != ".nex")
                    return std::string{};

                const std::filesystem::path active = active_nex_path_.empty()
                    ? std::filesystem::path(config_.load_file)
                    : std::filesystem::path(active_nex_path_);
                return std::filesystem::absolute(active.parent_path() / target)
                    .lexically_normal().string();
            };

            // A directly-loaded NEX may ask nexload to keep its own file open.
            // Service that read-only host handle, plus one read-only companion
            // file in the same directory, before the legacy --esxdos-stub
            // in-memory file. The companion is deliberately narrow: no
            // absolute paths, directory components, symlinks, or writes.
            if (host_nex_available) {
                const uint8_t handle = static_cast<uint8_t>(r.AF >> 8);
                auto is_host_handle = [](uint8_t value) {
                    return value == ExtendedNexHost::kHandle ||
                           value == ExtendedNexHost::kSiblingHandle;
                };
                auto handle_is_open = [&](uint8_t value) {
                    return value == ExtendedNexHost::kHandle
                        ? extended_nex_host_.is_open()
                        : extended_nex_host_.sibling_is_open();
                };
                auto set_position_result = [&](uint8_t value) {
                    const uint64_t host_position =
                        value == ExtendedNexHost::kHandle
                            ? extended_nex_host_.position()
                            : extended_nex_host_.sibling_position();
                    const uint32_t pos = static_cast<uint32_t>(
                        std::min<uint64_t>(host_position, 0xFFFFFFFFULL));
                    r.BC = static_cast<uint16_t>(pos >> 16);
                    r.DE = static_cast<uint16_t>(pos);
                };
                switch (defb) {
                    case 0x85: { // DISK_FILEMAP
                        if (handle != ExtendedNexHost::kHandle ||
                            !extended_nex_host_.is_open()) {
                            set_a_and_carry(0x0D, true); // EBADF
                            return true;
                        }
                        const std::size_t capacity = r.DE;
                        std::vector<ExtendedNexHost::FileMapEntry> entries;
                        if (!extended_nex_host_.file_map(capacity, entries)) {
                            set_a_and_carry(0x06, true); // EIO
                            return true;
                        }
                        uint16_t out = r.IX;
                        for (const auto& entry : entries) {
                            for (int i = 0; i < 4; ++i)
                                mmu_.write(static_cast<uint16_t>(out++),
                                           static_cast<uint8_t>(entry.address >> (i * 8)));
                            mmu_.write(static_cast<uint16_t>(out++),
                                       static_cast<uint8_t>(entry.blocks));
                            mmu_.write(static_cast<uint16_t>(out++),
                                       static_cast<uint8_t>(entry.blocks >> 8));
                        }
                        r.DE = static_cast<uint16_t>(capacity - entries.size());
                        r.HL = out;
                        set_a_and_carry(ExtendedNexHost::kCardFlags, false);
                        return true;
                    }
                    case 0x86: { // DISK_STRMSTART
                        const uint8_t flags = handle;
                        const uint32_t address =
                            (static_cast<uint32_t>(r.IX) << 16) | r.DE;
                        if (!extended_nex_host_.stream_start(address, flags)) {
                            set_a_and_carry(0x0E, true); // ENODEV
                            return true;
                        }
                        r.BC = 0x00EB; // B=SD/MMC protocol, C=data port
                        set_a_and_carry(flags, false);
                        return true;
                    }
                    case 0x87: { // DISK_STRMEND
                        if (!extended_nex_host_.stream_end()) {
                            set_a_and_carry(0x0E, true); // ENODEV
                            return true;
                        }
                        set_a_and_carry(handle, false);
                        return true;
                    }
                    case 0x9A: { // F_OPEN — active NEX or safe sibling, read-only
                        const std::string filename = read_filespec(r.IX);
                        Log::emulator()->debug(
                            "extended NEX F_OPEN: '{}' mode={:#04x}",
                            filename, static_cast<uint8_t>(r.BC >> 8));
                        const std::filesystem::path requested(filename);
                        const std::filesystem::path active_path(
                            extended_nex_host_.path());
                        std::string wanted = requested.filename().string();
                        std::string active = active_path.filename().string();
                        std::transform(wanted.begin(), wanted.end(), wanted.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        std::transform(active.begin(), active.end(), active.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        const uint8_t mode = static_cast<uint8_t>(r.BC >> 8);
                        // Review hardening (PR #100 finding 3): a bare ".."
                        // has no parent_path() on POSIX, so it passes the
                        // plain-name gate and resolves to the grandparent
                        // directory. Today the downstream is_regular_file()
                        // check rejects it incidentally; reject dot-names
                        // explicitly so the sandbox does not depend on that.
                        const bool plain_name =
                            !requested.empty() && !requested.is_absolute() &&
                            !requested.has_parent_path() &&
                            requested.filename() != ".." &&
                            requested.filename() != ".";
                        const bool read_only =
                            (mode & 0x01) != 0 && (mode & 0x02) == 0;
                        if (plain_name && wanted == active && read_only) {
                            if (!extended_nex_host_.reopen()) {
                                set_a_and_carry(0x06, true); // EIO
                            } else {
                                set_a_and_carry(ExtendedNexHost::kHandle, false);
                            }
                            return true;
                        }
                        // GH #31 — with --esxdos-stub-root set, the root IS
                        // the filesystem: only the NEX's own name (handled
                        // just above, which self-streaming needs) still
                        // resolves beside the NEX. Everything else goes to the
                        // root, so one name cannot mean two files.
                        if (plain_name && read_only && !esxdos_hostfs_.active()) {
                            const std::filesystem::path sibling =
                                (active_path.parent_path() / requested).lexically_normal();
                            std::error_code ec;
                            const auto status = std::filesystem::symlink_status(sibling, ec);
                            if (!ec && std::filesystem::is_regular_file(status) &&
                                !std::filesystem::is_symlink(status)) {
                                if (!extended_nex_host_.open_sibling(sibling.string())) {
                                    set_a_and_carry(0x06, true); // EIO
                                } else {
                                    set_a_and_carry(
                                        ExtendedNexHost::kSiblingHandle, false);
                                }
                                return true;
                            }
                        }
                        if (!stub_enabled) {
                            set_a_and_carry((mode & 0x02) ? 0x08 : 0x05, true);
                            return true;
                        }
                        break; // allow the in-memory compatibility file below
                    }
                    case 0x9B: // F_CLOSE
                        if (is_host_handle(handle)) {
                            if (!handle_is_open(handle)) {
                                set_a_and_carry(0x0D, true); // EBADF
                            } else {
                                if (handle == ExtendedNexHost::kHandle)
                                    extended_nex_host_.close();
                                else
                                    extended_nex_host_.close_sibling();
                                set_a_and_carry(handle, false);
                            }
                            return true;
                        }
                        break;
                    case 0x9D: { // F_READ
                        if (!is_host_handle(handle)) break;
                        if (!handle_is_open(handle)) {
                            set_a_and_carry(0x0D, true); // EBADF
                            return true;
                        }
                        const uint16_t requested = r.BC;
                        std::vector<uint8_t> bytes(requested);
                        std::size_t actual = 0;
                        const bool read_ok = handle == ExtendedNexHost::kHandle
                            ? extended_nex_host_.read(bytes.data(), requested, actual)
                            : extended_nex_host_.read_sibling(
                                  bytes.data(), requested, actual);
                        if (!read_ok) {
                            set_a_and_carry(0x06, true); // EIO
                            return true;
                        }
                        for (std::size_t i = 0; i < actual; ++i)
                            mmu_.write(static_cast<uint16_t>(r.IX + i), bytes[i]);
                        r.BC = static_cast<uint16_t>(actual);
                        r.DE = static_cast<uint16_t>(actual);
                        r.HL = static_cast<uint16_t>(r.IX + actual);
                        set_a_and_carry(handle, false);
                        return true;
                    }
                    case 0x9F: { // F_SEEK
                        if (!is_host_handle(handle)) break;
                        const uint32_t offset =
                            (static_cast<uint32_t>(r.BC) << 16) | r.DE;
                        const uint8_t mode = static_cast<uint8_t>(r.IX);
                        if ((mode & 0xFC) != 0) {
                            Log::emulator()->warn(
                                "extended NEX F_SEEK: ignoring undocumented mode bits {:#04x}",
                                mode & 0xFC);
                        }
                        const bool seek_ok = handle == ExtendedNexHost::kHandle
                            ? extended_nex_host_.seek(mode, offset)
                            : extended_nex_host_.seek_sibling(mode, offset);
                        if (!seek_ok) {
                            set_a_and_carry(0x07, true); // EINVAL
                            return true;
                        }
                        set_position_result(handle);
                        set_a_and_carry(handle, false);
                        return true;
                    }
                    case 0xA0: // F_FGETPOS
                        if (is_host_handle(handle)) {
                            if (!handle_is_open(handle)) {
                                set_a_and_carry(0x0D, true); // EBADF
                            } else {
                                set_position_result(handle);
                                set_a_and_carry(handle, false);
                            }
                            return true;
                        }
                        break;
                    case 0xA1: { // F_FSTAT — 11-byte esx_stat at IX
                        if (!is_host_handle(handle)) break;
                        if (!handle_is_open(handle)) {
                            set_a_and_carry(0x0D, true); // EBADF
                            return true;
                        }
                        const uint16_t out = r.IX;
                        mmu_.write(out + 0, '*');  // current drive
                        mmu_.write(out + 1, 0x81); // regular SD-card file
                        mmu_.write(out + 2, 0x00); // attributes
                        for (int i = 3; i < 7; ++i) mmu_.write(out + i, 0x00);
                        const uint64_t host_size =
                            handle == ExtendedNexHost::kHandle
                                ? extended_nex_host_.size()
                                : extended_nex_host_.sibling_size();
                        const uint32_t size = static_cast<uint32_t>(
                            std::min<uint64_t>(host_size, 0xFFFFFFFFULL));
                        for (int i = 0; i < 4; ++i)
                            mmu_.write(out + 7 + i,
                                       static_cast<uint8_t>(size >> (i * 8)));
                        set_a_and_carry(handle, false);
                        return true;
                    }
                    default:
                        break;
                }
            }
            // ── GH #31 — the host directory served by --esxdos-stub-root ──
            //
            // Claims the PATH-keyed calls outright: with a root there is one
            // volume and it is that directory. Claims the HANDLE-keyed ones
            // only for handles it owns, so the legacy in-memory file (handle
            // 1) and the extended-NEX host (2, 3) keep reaching their own code
            // above and below this block.
            //
            // Register conventions are transcribed from z88dk's
            // libsrc/_DEVELOPMENT/arch/zxn/esxdos/z80/asm_esx_*.asm header
            // blocks (verbatim NextZXOS/esxDOS API excerpts), cross-checked
            // against tbblue src/asm/readdir/readdir.asm for the directory
            // calls. The filespec/buffer pointer is taken from IX, which is
            // what those headers specify and what every other jnext esxDOS
            // path already uses.
            if (esxdos_hostfs_.active()) {
                const uint8_t handle = static_cast<uint8_t>(r.AF >> 8);
                auto fail = [&](uint8_t err) { set_a_and_carry(err, true); return true; };
                auto done = [&](uint8_t a) { set_a_and_carry(a, false); return true; };
                auto put_pos = [&](uint32_t pos) {   // BCDE = 32-bit position
                    r.BC = static_cast<uint16_t>(pos >> 16);
                    r.DE = static_cast<uint16_t>(pos);
                };
                auto put_stat = [&](uint16_t at, const EsxdosHostFs::StatInfo& st) {
                    // 11-byte esx_stat, asm_esx_f_fstat.asm:
                    // +0 '*' +1 $81 +2 attr +3(2) time +5(2) date +7(4) size
                    auto w8 = [&](int off, uint8_t v) {
                        mmu_.write(static_cast<uint16_t>(at + off), v);
                    };
                    w8(0, '*');
                    w8(1, 0x81);
                    w8(2, st.attr);
                    w8(3, static_cast<uint8_t>(st.time));
                    w8(4, static_cast<uint8_t>(st.time >> 8));
                    w8(5, static_cast<uint8_t>(st.date));
                    w8(6, static_cast<uint8_t>(st.date >> 8));
                    for (int i = 0; i < 4; ++i)
                        w8(7 + i, static_cast<uint8_t>(st.size >> (i * 8)));
                };
                auto put_zstr = [&](uint16_t at, const std::string& s) {
                    uint16_t p = at;
                    for (char c : s) mmu_.write(p++, static_cast<uint8_t>(c));
                    mmu_.write(p++, 0);
                    return p;
                };

                switch (defb) {
                    case 0x9A: {  // F_OPEN — A=drive, IX=filespec, B=mode
                        uint8_t h = 0;
                        const uint8_t err = esxdos_hostfs_.open(
                            read_filespec(r.IX), static_cast<uint8_t>(r.BC >> 8), h);
                        if (err) { r.HL = 0xFFFF; return fail(err); }
                        return done(h);
                    }
                    case 0x9B:    // F_CLOSE — A=handle (file OR directory)
                        if (!EsxdosHostFs::owns_handle(handle)) break;
                        if (const uint8_t err = esxdos_hostfs_.close(handle))
                            return fail(err);
                        return done(0x00);
                    case 0x9C:    // F_SYNC — A=handle
                        if (!EsxdosHostFs::owns_handle(handle)) break;
                        if (const uint8_t err = esxdos_hostfs_.sync(handle))
                            return fail(err);
                        return done(0x00);
                    case 0x9D: {  // F_READ — A=handle, IX=dest, BC=count
                        if (!EsxdosHostFs::owns_handle(handle)) break;
                        std::vector<uint8_t> bytes;
                        const uint8_t err =
                            esxdos_hostfs_.read(handle, r.BC, bytes);
                        if (err) { r.BC = 0; return fail(err); }
                        for (std::size_t i = 0; i < bytes.size(); ++i)
                            mmu_.write(static_cast<uint16_t>(r.IX + i), bytes[i]);
                        const uint16_t actual = static_cast<uint16_t>(bytes.size());
                        r.BC = actual;
                        r.DE = actual;
                        r.HL = static_cast<uint16_t>(r.IX + actual);
                        return done(handle);
                    }
                    case 0x9E: {  // F_WRITE — A=handle, IX=source, BC=count
                        if (!EsxdosHostFs::owns_handle(handle)) break;
                        const uint16_t want = r.BC;
                        std::vector<uint8_t> bytes(want);
                        for (uint16_t i = 0; i < want; ++i)
                            bytes[i] = mmu_.read(static_cast<uint16_t>(r.IX + i));
                        std::size_t written = 0;
                        const uint8_t err = esxdos_hostfs_.write(
                            handle, bytes.data(), bytes.size(), written);
                        if (err) { r.BC = 0; return fail(err); }
                        // BC is the documented answer; DE/HL follow F_READ's
                        // shape, matching the in-memory stub so the two
                        // back ends do not differ in what they leave behind.
                        const uint16_t actual = static_cast<uint16_t>(written);
                        r.BC = actual;
                        r.DE = actual;
                        r.HL = static_cast<uint16_t>(r.IX + actual);
                        return done(handle);
                    }
                    case 0x9F: {  // F_SEEK — A=handle, BCDE=distance, IXL=whence
                        if (!EsxdosHostFs::owns_handle(handle)) break;
                        const uint32_t distance =
                            (static_cast<uint32_t>(r.BC) << 16) | r.DE;
                        uint32_t pos = 0;
                        const uint8_t err = esxdos_hostfs_.seek(
                            handle, static_cast<uint8_t>(r.IX), distance, pos);
                        if (err) return fail(err);
                        put_pos(pos);
                        return done(handle);
                    }
                    case 0xA0: {  // F_FGETPOS — A=handle
                        if (!EsxdosHostFs::owns_handle(handle)) break;
                        uint32_t pos = 0;
                        const uint8_t err = esxdos_hostfs_.fgetpos(handle, pos);
                        if (err) return fail(err);
                        put_pos(pos);
                        return done(handle);
                    }
                    case 0xA1: {  // F_FSTAT — A=handle, IX=11-byte buffer
                        if (!EsxdosHostFs::owns_handle(handle)) break;
                        EsxdosHostFs::StatInfo st;
                        const uint8_t err = esxdos_hostfs_.fstat(handle, st);
                        if (err) return fail(err);
                        put_stat(r.IX, st);
                        return done(handle);
                    }
                    case 0xAC: {  // F_STAT — A=drive, IX=filespec, DE=11 bytes
                        EsxdosHostFs::StatInfo st;
                        const uint8_t err =
                            esxdos_hostfs_.stat(read_filespec(r.IX), st);
                        if (err) return fail(err);
                        put_stat(r.DE, st);
                        return done(0x00);
                    }
                    case 0xA3: {  // F_OPENDIR — A=drive, IX=path, B=mode
                        uint8_t h = 0;
                        const uint8_t err = esxdos_hostfs_.opendir(
                            read_filespec(r.IX), static_cast<uint8_t>(r.BC >> 8), h);
                        if (err) return fail(err);
                        return done(h);
                    }
                    case 0xA4: {  // F_READDIR — A=handle, IX=entry buffer
                        if (!EsxdosHostFs::owns_handle(handle)) break;
                        EsxdosHostFs::DirEntry e;
                        bool have = false;
                        const uint8_t err =
                            esxdos_hostfs_.readdir(handle, e, have);
                        if (err) return fail(err);
                        if (!have) return done(0x00);   // Fc=0, A=0: no more
                        // Entry layout (readdir.asm `showanentry`, and z88dk
                        // struct esx_dirent): attr, asciiz name(s), time word,
                        // date word, size dword — all little-endian.
                        const uint8_t name_mode =
                            esxdos_hostfs_.dir_name_mode(handle);
                        uint16_t p = r.IX;
                        mmu_.write(p++, e.attr);
                        if (name_mode == EsxdosHostFs::kDirShortOnly) {
                            p = put_zstr(p, e.sfn);
                        } else {
                            std::string lfn = e.lfn;
                            if (lfn.size() > 260) lfn.resize(260);
                            p = put_zstr(p, lfn);
                            if (name_mode == EsxdosHostFs::kDirLfnAndShort)
                                p = put_zstr(p, e.sfn);
                        }
                        mmu_.write(p++, static_cast<uint8_t>(e.time));
                        mmu_.write(p++, static_cast<uint8_t>(e.time >> 8));
                        mmu_.write(p++, static_cast<uint8_t>(e.date));
                        mmu_.write(p++, static_cast<uint8_t>(e.date >> 8));
                        for (int i = 0; i < 4; ++i)
                            mmu_.write(p++, static_cast<uint8_t>(e.size >> (i * 8)));
                        return done(0x01);              // one entry returned
                    }
                    case 0xA5: {  // F_TELLDIR — A=handle
                        if (!EsxdosHostFs::owns_handle(handle)) break;
                        uint32_t pos = 0;
                        const uint8_t err = esxdos_hostfs_.telldir(handle, pos);
                        if (err) return fail(err);
                        put_pos(pos);
                        return done(handle);
                    }
                    case 0xA6: {  // F_SEEKDIR — A=handle, BCDE=pointer
                        if (!EsxdosHostFs::owns_handle(handle)) break;
                        const uint32_t pos =
                            (static_cast<uint32_t>(r.BC) << 16) | r.DE;
                        if (const uint8_t err = esxdos_hostfs_.seekdir(handle, pos))
                            return fail(err);
                        return done(handle);
                    }
                    case 0xA7:    // F_REWINDDIR — A=handle
                        if (!EsxdosHostFs::owns_handle(handle)) break;
                        if (const uint8_t err = esxdos_hostfs_.rewinddir(handle))
                            return fail(err);
                        return done(handle);
                    case 0xA8: {  // F_GETCWD — A=drive, IX=buffer
                        std::string cwd;
                        const uint8_t err = esxdos_hostfs_.getcwd(cwd);
                        if (err) return fail(err);
                        put_zstr(r.IX, cwd);
                        return done(0x00);
                    }
                    case 0xA9:    // F_CHDIR — A=drive, IX=path
                        if (const uint8_t err =
                                esxdos_hostfs_.chdir(read_filespec(r.IX)))
                            return fail(err);
                        return done(0x00);
                    case 0xB1:    // F_GETFREE — A=drive; BCDE = 512-byte blocks
                        put_pos(esxdos_hostfs_.free_blocks());
                        return done(0x00);
                    case 0x8E: {  // M_GETDATE — BC=date, DE=time (MS-DOS)
                        std::tm now{};
                        if (!rtc_.current_time(now)) {
                            r.BC = 0;
                            r.DE = 0;
                            return fail(0x00);   // no RTC: Fc=1, BC=DE=0
                        }
                        int year = now.tm_year + 1900;
                        if (year < 1980) year = 1980;
                        if (year > 2107) year = 2107;
                        r.BC = static_cast<uint16_t>(((year - 1980) << 9) |
                                                     ((now.tm_mon + 1) << 5) |
                                                     now.tm_mday);
                        r.DE = static_cast<uint16_t>((now.tm_hour << 11) |
                                                     (now.tm_min << 5) |
                                                     (now.tm_sec / 2));
                        return done(0x00);
                    }
                    default:
                        break;   // not a host-directory call — fall through
                }
            }
            switch (defb) {
                case 0x88:  // M_DOSVERSION
                    r.BC = 0x4E58;               // B='N', C='X'
                    // The host-backed extended-NEX bridge implements the
                    // 2.01 streaming contract and the 2.02-compatible calls
                    // required by Atic Atac. The legacy opt-in stub alone
                    // retains its deliberately conservative 1.94 claim.
                    r.DE = host_nex_available ? 0x0202 : 0x0194;
                    r.HL = 0x6E65;               // H='n', L='e' (English)
                    set_a_and_carry(0x00, false);
                    r.AF = static_cast<uint16_t>(r.AF | 0x0040); // Z set
                    return true;
                case 0x89: { // M_GETSETDRV — a direct load has one drive, C:
                    // Only for a directly loaded NEX (see the default case
                    // for why bare --esxdos-stub must not answer it).
                    if (!direct_nex_esxdos_ && !esxdos_hostfs_.active())
                        return false;
                    // Encoding: bits 7..3 = drive letter (0=A), bits 2..0
                    // ignored on set and 0 on return (NextZXOS_and_esxDOS_
                    // APIs.pdf, M_GETSETDRV). Values measured on real
                    // NextZXOS (distro SD image, `.nexload` of a probe):
                    // get -> A=$10 Fc=0; set C: ($11) -> A=$10 Fc=0;
                    // set A: ($01) / D: ($19) -> Fc=1 A=$0B (esx_enodrv,
                    // esxapi.def:181).
                    constexpr uint8_t kDriveC = 2 << 3;
                    const uint8_t a = static_cast<uint8_t>(r.AF >> 8);
                    if (a == 0 || (a & 0xF8) == kDriveC)
                        set_a_and_carry(kDriveC, false);
                    else
                        set_a_and_carry(0x0B, true);
                    return true;
                }
                case 0x8F: { // M_EXECCMD — support .RUN sibling.nex
                    std::string command = read_cmdline(r.IX);
                    while (!command.empty() && command.front() == ' ') command.erase(0, 1);
                    if (command.size() < 4 ||
                        (command[0] != 'r' && command[0] != 'R') ||
                        (command[1] != 'u' && command[1] != 'U') ||
                        (command[2] != 'n' && command[2] != 'N') || command[3] != ' ') {
                        set_a_and_carry(0x05, true);
                        return true;
                    }
                    std::string target = command.substr(4);
                    while (!target.empty() && target.front() == ' ') target.erase(0, 1);
                    while (!target.empty() && target.back() == ' ') target.pop_back();
                    if (target.size() >= 2 && target.front() == '"' && target.back() == '"')
                        target = target.substr(1, target.size() - 2);
                    nex_load_request_ = resolve_sibling(target);
                    if (nex_load_request_.empty()) {
                        set_a_and_carry(0x05, true);
                        return true;
                    }
                    Log::emulator()->info("esxdos stub: requested .RUN '{}'", nex_load_request_);
                    set_a_and_carry(0x00, false);
                    return true;
                }
                case 0x9A: { // F_OPEN — one in-memory file, persistent across NEX reset
                    const std::string filename = read_filespec(r.IX);
                    const uint8_t mode = static_cast<uint8_t>(r.BC >> 8);
                    if ((mode & 0x02) != 0) {
                        esxdos_stub_filename_ = filename;
                        esxdos_stub_file_.clear();
                        esxdos_stub_file_pos_ = 0;
                        esxdos_stub_file_open_ = true;
                        esxdos_stub_file_write_ = true;
                        set_a_and_carry(esxdos_stub_handle_, false);
                    } else if ((mode & 0x01) != 0 &&
                               filename == esxdos_stub_filename_ &&
                               !esxdos_stub_filename_.empty()) {
                        esxdos_stub_file_pos_ = 0;
                        esxdos_stub_file_open_ = true;
                        esxdos_stub_file_write_ = false;
                        set_a_and_carry(esxdos_stub_handle_, false);
                    } else {
                        r.HL = 0xFFFF;
                        set_a_and_carry(0x05, true);
                    }
                    return true;
                }
                case 0x9B:  // F_CLOSE
                    esxdos_stub_file_open_ = false;
                    set_a_and_carry(0x00, false); // success
                    return true;
                case 0x9D: { // F_READ
                    const uint8_t handle = static_cast<uint8_t>(r.AF >> 8);
                    if (!esxdos_stub_file_open_ || esxdos_stub_file_write_ ||
                        handle != esxdos_stub_handle_) {
                        set_a_and_carry(0x05, true);
                        return true;
                    }
                    const uint16_t requested = r.BC;
                    uint16_t actual = 0;
                    while (actual < requested && esxdos_stub_file_pos_ < esxdos_stub_file_.size()) {
                        mmu_.write(static_cast<uint16_t>(r.IX + actual),
                                   esxdos_stub_file_[esxdos_stub_file_pos_++]);
                        ++actual;
                    }
                    r.BC = actual;
                    r.DE = actual;
                    r.HL = static_cast<uint16_t>(r.IX + actual);
                    set_a_and_carry(handle, false);
                    return true;
                }
                case 0x9E: { // F_WRITE
                    const uint8_t handle = static_cast<uint8_t>(r.AF >> 8);
                    if (!esxdos_stub_file_open_ || !esxdos_stub_file_write_ ||
                        handle != esxdos_stub_handle_) {
                        set_a_and_carry(0x05, true);
                        return true;
                    }
                    const uint16_t actual = r.BC;
                    esxdos_stub_file_.reserve(esxdos_stub_file_.size() + actual);
                    for (uint16_t i = 0; i < actual; ++i)
                        esxdos_stub_file_.push_back(mmu_.read(static_cast<uint16_t>(r.IX + i)));
                    esxdos_stub_file_pos_ = esxdos_stub_file_.size();
                    r.BC = actual;
                    r.DE = actual;
                    r.HL = static_cast<uint16_t>(r.IX + actual);
                    set_a_and_carry(handle, false);
                    return true;
                }
                case 0x9F:  // F_SEEK
                    set_a_and_carry(0x05, true);  // A=5 (ENOENT)
                    return true;
                default:
                    // A hook code jnext does not implement gets the answer
                    // NextZXOS gives for one IT does not implement: Fc=1,
                    // A=esx_enonsense (esxapi.def:172). Measured on real
                    // NextZXOS for $80, $8A, $96 and $97. This includes
                    // calls NextZXOS DOES implement (F_GETCWD, F_CHDIR,
                    // M_GETDATE, ...): for those it is a refusal, not a
                    // copy of NextZXOS's reply. Codes above $B1
                    // are not hooks — NextZXOS raises a BASIC error report
                    // for them (measured: $B2, $E0) — so they stay with the
                    // code at $0008.
                    //
                    // Only while a directly loaded NEX is running. Bare
                    // --esxdos-stub can be used with NextZXOS booted, and
                    // there this would answer in front of NextZXOS's own
                    // esxDOS: it failed M_P3DOS/M_GETHANDLE during `.ls` and
                    // left a blank screen. (The extended-NEX host bridge is
                    // only ever opened by load_nex(), which also sets
                    // direct_nex_esxdos_, so this covers it too.)
                    if (direct_nex_esxdos_ && defb <= 0xB1) {
                        set_a_and_carry(0x02, true);
                        return true;
                    }
                    return false;  // not handled — fall through to $0008 code
            }
        };

        // Wrap the handler so BOTH the call and its result are recorded. The
        // result matters as much as the call: "F_SEEK -> FAIL A=5" is the line
        // that explains a program giving up, and an unhandled call is reported
        // explicitly rather than just missing.
        //
        // The wrapper is created unconditionally, but attached only when the
        // stub, tracing, or a host-backed NEX needs it. `trace()` performs its
        // own inline level check, so once attached, raising esxdos=trace at
        // runtime starts recording without rebuilding the callback.
        //
        // Residual limitation, deliberate: with the stub and tracing off at
        // init there is no CPU hook, so merely raising the log level later
        // still requires a restart. A later GUI NEX load is different:
        // load_nex() attaches this stored wrapper for every NEX it loads
        // (GH #250). Keeping it dormant otherwise preserves ordinary RST $08
        // dispatch, pinned by the existing esxdos-stub suite.
        esxdos_bridge_handler_ =
            [handle_esxdos](uint8_t defb, Z80Registers& r) -> bool {
                // Guarded for the should_log() reason given in
                // PortDispatch::read (src/port/port_dispatch.cpp): since
                // GH #250 this wrapper runs on EVERY RST $08 call of a
                // directly loaded NEX, not only when tracing, so an unguarded
                // trace() with arguments was an out-of-line call per call with
                // tracing off (GH #244).
                const bool trace_on = Log::esxdos()->should_log(spdlog::level::trace);
                const char* name = esxdos_call_name(defb);
                // Logged BEFORE handle_esxdos runs: the stub mutates r in place, so
                // capturing the arguments afterwards would report the results as if
                // they had been the inputs. Pinned by ESXT-28.
                if (trace_on)
                    Log::esxdos()->trace(
                        "-> ${:02X} {:<12} AF={:04X} BC={:04X} DE={:04X} HL={:04X} IX={:04X}",
                        defb, name ? name : "(unknown)", r.AF, r.BC, r.DE, r.HL, r.IX);

                const bool handled = handle_esxdos(defb, r);

                if (!trace_on) return handled;
                if (handled) {
                    // esxdos convention: carry CLEAR = success, SET = error
                    // with the code in A.
                    const bool error = (r.AF & 0x0001) != 0;
                    const uint8_t a  = static_cast<uint8_t>(r.AF >> 8);
                    Log::esxdos()->trace(
                        "<- ${:02X} {:<12} {} A={:02X} BC={:04X} DE={:04X} HL={:04X}",
                        defb, name ? name : "(unknown)", error ? "ERROR" : "ok   ",
                        a, r.BC, r.DE, r.HL);
                } else {
                    Log::esxdos()->trace(
                        "<- ${:02X} {:<12} not serviced by jnext — dispatched to "
                        "the ROM at $0008", defb, name ? name : "(unknown)");
                }
                return handled;
            };
        if (cfg.esxdos_stub || esxdos_tracing || direct_nex_load ||
            esxdos_hostfs_.active()) {
            cpu_.on_esxdos_call = esxdos_bridge_handler_;
        }

        if (cfg.esxdos_stub) Log::emulator()->info("esxdos shim enabled (--esxdos-stub)");
        if (esxdos_tracing)  Log::emulator()->info("esxdos syscall tracing enabled (--log-level esxdos=trace)");
    }

    // --- NextREG write handlers ---

    // Register 0x00: Machine ID (read-only).
    // VHDL zxnext.vhd:5884-5885 — read dispatch routes nr_register=X"00"
    // unconditionally to g_machine_id; writes have no handler in the VHDL
    // write dispatch, so they are discarded. The read_handler makes any
    // stale byte written into regs_[0] invisible.
    // NextZXOS-boot fix (2026-07-09, ZXGO-COMPARISON doc): return the VHDL
    // g_machine_id = 0x0A (zxnext_top_issue{2,4,5}.vhd:35). The prior 0x08
    // (HWID_EMULATORS) deviation made NextZXOS take an emulator branch at
    // its ROM1 machine-ID check; the zx_go reference emulator documents
    // fixing this exact bug ($08 broke its boot) and boots with $0A.
    nextreg_.set_read_handler(0x00, []() -> uint8_t { return 0x0A; });

    // Register 0x07: CPU speed selector
    //   0 = 3.5 MHz, 1 = 7 MHz, 2 = 14 MHz, 3 = 28 MHz
    // VHDL zxnext.vhd:1300 nr_07_cpu_speed reset "00"; zxnext.vhd:5789-5791,5817
    // stores the 2-bit field; zxnext.vhd:4481 feeds it into i_contention_en
    // (any non-zero speed disables memory contention).
    nextreg_.set_write_handler(0x07, [this](uint8_t v) -> uint8_t {
        CpuSpeed speed = static_cast<CpuSpeed>(v & 0x03);
        // Guest-initiated state change → debug, and only when the value actually
        // CHANGES. Programs rewrite NR 0x07 every frame; logging every write —
        // at info, as this did — floods the console. See the level policy in
        // core/log.h.
        if (static_cast<uint8_t>(speed) != last_logged_cpu_speed_) {
            Log::emulator()->debug("CPU speed changed to {} (NextREG 0x07={:#04x})",
                                   cpu_speed_str(speed), v);
            last_logged_cpu_speed_ = static_cast<uint8_t>(speed);
        }
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
    // VHDL zxnext.vhd:5816-5820 sets the actual cpu_speed via:
    //   if expbus_en = '0' then cpu_speed <= nr_07_cpu_speed;
    //   else                    cpu_speed <= expbus_speed;
    // (latched on bus-idle, :5809). VHDL zxnext.vhd:5496 hard-wires
    // `nr_81_expbus_speed <= "00"` (the writable arm is commented out),
    // so the expbus-active path always reports actual speed = "00".
    //
    // V21-NMP-03 (Pass-21 verify-audit fix): pre-fix `act = req` was
    // unconditional, ignoring expbus_en. When NR 0x80 bit 7 = 1
    // (expbus_eff_en after the bus-idle latch) AND nr_07_cpu_speed != 0,
    // VHDL would surface act = 0 (= expbus_speed hard-wired) while jnext
    // surfaced act = req. Class-(c) inert divergence (jnext has no expbus
    // device wired, so the steady-state observable is unchanged for any
    // boot path that respects the default NR 0x80 = 0x00), but the
    // readback contract is part of the VHDL surface.
    //
    // The bus-idle latch (VHDL :5809) is collapsed into NR 0x80 write
    // and NmiSource::expbus_eff_en()/expbus_eff_disable_mem() as the
    // commit point — same approximation used at Pass-9 verify-audit for
    // `expbus_eff_en` propagation into the NMI gates. Re-use that here
    // so we don't introduce yet another latch shadow.
    nextreg_.set_read_handler(0x07, [this]() -> uint8_t {
        const uint8_t req = nextreg_.cached(0x07) & 0x03;
        // VHDL :5816-5820: actual cpu_speed = nr_07_cpu_speed when
        // expbus_en = '0', else expbus_speed (hard-wired "00").
        const uint8_t act = nmi_source_.expbus_eff_en() ? 0x00 : req;
        return static_cast<uint8_t>((act << 4) | req);
    });

    // NR 0x82 — Internal port enable (low 8 bits of the 32-bit
    // internal_port_enable signal at zxnext.vhd:2392). VHDL :2399 ties
    // bit 1 to `port_7ffd_io_en`, which feeds the `port_7ffd_active` OR-
    // term in `port_contend` (zxnext.vhd:4496 + 2594). Verify9-memory
    // class-(c) → class-(a) fix: pre-fix the contention model dropped
    // this term (port 0x7FFD writes on 128K/+3 missed contention).
    // Push the new bit-1 state into ContentionModel on every NR 0x82
    // write so the runtime contention gate stays in sync. NextReg's
    // bare `write()` already stores the byte into regs_[0x82] before
    // calling the handler, so other consumers reading via
    // `nextreg_.cached(0x82)` are unaffected.
    nextreg_.set_write_handler(0x82, [this](uint8_t v) -> uint8_t {
        // V16-NMP-02: when expbus_eff_en=1, port_7ffd_io_en must AND in
        // NR 0x86 b1 (zxnext.vhd:2392-2393). NextReg::write commits the
        // returned byte into regs_[0x82] AFTER this handler returns, so
        // we pass `v` to the propagate helper as an in-flight override.
        propagate_effective_port_enables(/*override_reg=*/0x82, v);
        return v;
    });
    nextreg_.set_write_handler(0x12, [this](uint8_t v) -> uint8_t {
        // GH #270 — position the change-log entry horizontally as well as
        // by scanline, so a mid-line Copper MOVE switches the bank where it
        // lands instead of repainting the whole line.
        layer2_.set_current_hpos(nr_write_hpos());
        layer2_.set_active_bank(v);
        // Verify4-memory class-(a) fix: VHDL zxnext.vhd:2968 makes
        // layer2_active_bank combinational from nr_12_layer2_active_bank
        // when port_123b_layer2_map_shadow=0. The Mmu's CPU L2 read/
        // write-over path caches the bank in `l2_bank_`, refreshed by
        // set_l2_port() on every 0x123B write. NR $12 writes between
        // two 0x123B writes were not propagated, leaving the cached
        // bank stale. NR $13 has the analogous propagation via
        // mmu_.set_l2_shadow_bank() — NR $12 was the missing seam.
        mmu_.set_l2_active_bank(layer2_.active_bank());
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

    // Register 0x11: Video timing (frame-rate / line count select).
    // VHDL zxnext.vhd:5208-5217 — write is gated on `nr_03_config_mode='1'`,
    // and on Issue-2 boards (`g_video_inc = "10"`, see
    // zxnext_top_issue2.vhd:40 — jnext defaults to ZXN_ISSUE2) only bit 0
    // is captured: nr_11_video_timing <= "00" & nr_wr_dat(0). The "111"
    // shortcut at :5210 normalises to "000". Read at zxnext.vhd:5926-5927:
    //   port_253b_dat <= "00000" & nr_11_video_timing;
    // bits 7:3 always read back as zero. VERIFY4 / pass-4 — pre-fix the
    // bare regs_[0x11] echoed the full 8-bit write and ignored the
    // config_mode gate, so firmware writes outside config_mode (and any
    // upper-bit writes) leaked into the read path.
    nextreg_.set_write_handler(0x11, [this](uint8_t v) -> uint8_t {
        if (!nextreg_.nr_03_config_mode()) {
            // Outside config_mode the latch holds — return the canonical
            // current readback so the regs_[] shadow does not drift.
            return static_cast<uint8_t>(nextreg_.cached(0x11) & 0x07);
        }
        uint8_t stored;
        if ((v & 0x07) == 0x07) {
            stored = 0x00;                           // VHDL :5210-5211
        } else {
            // Issue 2 board ("g_video_inc = 10"): only bit 0 captured.
            stored = static_cast<uint8_t>(v & 0x01);
        }
        return stored;  // bits 7:3 = 0 implicit (mask 0x07).
    });

    // Register 0x13: Layer 2 shadow RAM bank
    nextreg_.set_write_handler(0x13, [this](uint8_t v) -> uint8_t {
        layer2_.set_current_hpos(nr_write_hpos());   // GH #270
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
    // VHDL zxnext.vhd:6047-6048 — NR 0x44 read:
    //   port_253b_dat <= nr_palette_dat(10:9) & "00000" & nr_palette_dat(0);
    // i.e. priority [7:6], constants [5:1]=0, blue-LSB [0]. VERIFY4 / pass-4:
    // bare regs_[0x44] would echo the last-written byte (the second NR 0x44
    // write byte verbatim), which mismatches the VHDL composition for ULA /
    // sprite / tilemap targets where priority is forced "00", and for any
    // target where the read drops the upper-8 colour bits.
    nextreg_.set_read_handler(0x44, [this]() -> uint8_t {
        return palette_.read_9bit();
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
        // V15-NMP-02 (Pass-15 verify-audit fix): VHDL zxnext.vhd:5878-6289
        // has NO read mux entry for NR 0xFF (write-only ULA+ palette poke,
        // routed via `nr_ff_we` at :4906 / :4919 / :6957-6958 to the palette
        // dpram). Unmapped reads fall through the case statement to the
        // `when others => port_253b_dat <= (others => '0')` default at
        // :6286-6287, returning 0x00. Pre-fix the write_handler returned
        // `v`, leaving the cache holding the last-write byte — a subsequent
        // NR 0xFF read returned that byte instead of 0x00. Same shape as
        // the existing G149 (NR 0x60), V14-NMP-03 (NR 0x2B), V14-NMP-04
        // (NR 0x2A), and V15-NMP-01 (NR 0x63) canonicalisations.
        return 0;
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
        layer2_.set_current_hpos(nr_write_hpos());   // GH #270
        layer2_.set_scroll_x_lsb(v);
        return v;
    });

    // Register 0x17: Layer 2 Y scroll
    nextreg_.set_write_handler(0x17, [this](uint8_t v) -> uint8_t {
        layer2_.set_current_hpos(nr_write_hpos());   // GH #270
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
        layer2_.set_current_hpos(nr_write_hpos());   // GH #270
        layer2_.set_scroll_x_msb(v);
        return static_cast<uint8_t>(v & 0x01);
    });

    // VHDL zxnext.vhd:5909 — NR 0x09 read composes:
    //   nr_09_psg_mono [7:5] & nr_09_sprite_tie [4] & '0' [3]
    //     & (NOT nr_09_hdmi_audio_en) [2] & eff_nr_09_scanlines [1:0]
    // Bit 3 always reads 0; bit 4 sourced from sprites_.mirror_tie() (the
    // authoritative store wired through Sprites). Bits 7:5 (psg_mono) echo
    // the last-write byte through the cached regs_[0x09] (the VHDL latches
    // them too, just into a named signal). Bit 2 reads back the WRITE bit
    // 2: VHDL stores `not nr_wr_dat(2)` so the read of
    // `not nr_09_hdmi_audio_en` returns nr_wr_dat(2) — same as cached. G56.
    //
    // GH #201 — bits 1:0 are `eff_nr_09_scanlines`, the frame-edge-latched
    // copy (zxnext.vhd:6701), NOT the pending `nr_09_scanlines` the write
    // at :5859-5860 (or the F7 hotkey increment at :5861-5863) drives. They
    // used to be read from the cache, so a mid-frame NR 0x09 write became
    // visible on the very next read instead of at the next frame edge.
    nextreg_.set_read_handler(0x09, [this]() -> uint8_t {
        const uint8_t cached = nextreg_.cached(0x09);
        // Mask: drop bit 4 (sprite_tie comes from sprites_), bit 3 (always
        // 0 per VHDL) and bits 1:0 (the effective scanline latch).
        const uint8_t base = static_cast<uint8_t>(cached & 0xE4);
        const uint8_t tie  = sprites_.mirror_tie() ? 0x10 : 0x00;
        return static_cast<uint8_t>(base | tie |
                                    (eff_nr_09_scanlines_ & 0x03));
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
    // PASS-8 NR 0x0A reset propagation. NextReg::reset() preserves
    // `regs_[0x0A]` across reset (VHDL :1124-1128 initial-value-only
    // signals — no `reset` clause); KempstonMouse::reset() preserves the
    // mouse subsystem state. The Multiface / DivMMC / SpiMaster owners
    // already preserved their NR 0x0A-derived bits on their own reset
    // paths (mf_type_, nr_0a_4_enable_, sd_swap_ are not reset). The
    // mouse used to clobber `button_reverse_` / `dpi_` in pre-pass-8 —
    // even with that fix, fan out the preserved cached NR 0x0A byte here
    // to all four owners so they pick up any pre-write-to-init() state
    // change (e.g. snapshot reload, soft-reset code path that clears
    // mouse). Mirrors the NR 0x05 / NR 0x06 / NR 0x09 fan-out pattern.
    {
        const uint8_t cached_0a = nextreg_.cached(0x0A);
        // Bits 7:6 (mf_type) only commit while config_mode='1' in VHDL —
        // but here we are fanning out the *preserved* cached byte, which
        // only contains values that were committed under config_mode at
        // their original write time. Re-applying them is therefore safe
        // regardless of the current config_mode flag.
        spi_.set_sd_swap((cached_0a & 0x20) != 0);
        multiface_.set_mode(static_cast<uint8_t>((cached_0a >> 6) & 0x03));
        divmmc_.set_nr_0a_4_enable((cached_0a & 0x10) != 0);
        mouse_.set_button_reverse((cached_0a & 0x08) != 0);
        mouse_.set_dpi(static_cast<uint8_t>(cached_0a & 0x03));
    }

    nextreg_.set_write_handler(0x0A, [this](uint8_t v) -> uint8_t {
        // P0 boot probe (doc/issues/nextzxos-boot/ZXGO-COMPARISON-2026-07-09.md):
        // env-gated, zero cost when unset.
        if (std::getenv("JNEXT_BOOT_PROBE")) {
            Log::emulator()->info(
                "BOOT_PROBE NR 0x0A <= {:#04x} at PC={:#06x} (bit4 automap_en={})",
                v, cpu_.get_registers().PC, (v & 0x10) != 0);
        }
        // G131: gate bits 7:6 and bit 5 on nr_03_config_mode.
        const bool cfg = nextreg_.nr_03_config_mode();
        if (cfg) {
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
        // V11-NMP-02 (Pass-11 verify-audit fix): VHDL zxnext.vhd:5191-5198
        // gates the latches `nr_0a_mf_type` (bits 7:6) and `nr_0a_sd_swap`
        // (bit 5) behind `nr_03_config_mode = '1'`. When the gate is closed
        // the underlying signals retain their previous values. Pre-fix the
        // C++ stored the raw `v` byte verbatim in `regs_[0x0A]`, so the
        // cache could leak bits 7:5 written outside config_mode. The
        // observable consequence was the init/reset fan-out at lines 985-996
        // which reads `cached(0x0A)` and forwards bits 7:6 to
        // `multiface_.set_mode()` — a soft reset after a "cache-poisoning"
        // out-of-config_mode write would commit a Multiface mode the VHDL
        // never latched. Canonicalise the stored byte: bits 7:5 inherit the
        // pre-write cached value when config_mode is closed; bits 4:0 are
        // always written (those signals have no config_mode gate).
        if (cfg) {
            return v;
        }
        const uint8_t prev = nextreg_.cached(0x0A);
        return static_cast<uint8_t>((v & 0x1F) | (prev & 0xE0));
    });
    // VHDL zxnext.vhd:5912 — NR 0x0A read composes:
    //   nr_0a_mf_type [7:6] & nr_0a_sd_swap [5]
    //     & nr_0a_divmmc_automap_en [4] & nr_0a_mouse_button_reverse [3]
    //     & '0' [2] & nr_0a_mouse_dpi [1:0]
    // Bit 2 always reads 0. All fields use authoritative subsystem
    // accessors. G56.
    //
    // Pass-3 verify-audit (Task 2): bits 7:6 (mf_type) MUST come from
    // the authoritative Multiface subsystem state, NOT from the cached
    // last-write byte. VHDL :5191-5198 gates `nr_0a_mf_type` writes on
    // `nr_03_config_mode='1'`, but the C++ NextReg::write stores the
    // raw byte in regs_[] regardless. Reading `cached(0x0A) & 0xC0`
    // would therefore return whatever was last written to NR 0x0A,
    // ignoring the gate — diverging from VHDL when firmware writes
    // NR 0x0A bits 7:6 outside config_mode (which the write handler
    // correctly ignores via `multiface_.set_mode()` being inside the
    // `if (nr_03_config_mode())` block above).
    nextreg_.set_read_handler(0x0A, [this]() -> uint8_t {
        // Pass-3 verify-audit fix — mf_type from authoritative state.
        uint8_t v = static_cast<uint8_t>((multiface_.mf_type() & 0x03) << 6);
        if (spi_.sd_swap())          v = static_cast<uint8_t>(v | 0x20);
        if (divmmc_.nr_0a_4_enable()) v = static_cast<uint8_t>(v | 0x10);
        if (mouse_.button_reverse()) v = static_cast<uint8_t>(v | 0x08);
        // bit 2 always 0
        v = static_cast<uint8_t>(v | (mouse_.dpi() & 0x03));
        return v;
    });

    // PASS-8 NR 0x05 reset propagation. Pass-7 fixed NextReg::reset() to
    // preserve `regs_[0x05]` across reset (VHDL :1105-1106, :1302-1303
    // initial-value-only signals — no `reset` clause). Pass-8 fixed
    // Joystick::reset() to also preserve `joy0_mode_` / `joy1_mode_`
    // (the authoritative owner of the bits surfaced by the NR 0x05 read
    // handler). Re-fan-out the preserved cached byte to the joystick
    // subsystem here so that any path which calls Joystick::reset() at
    // the top of init() (pre-pass-8 it cleared the modes) is restored
    // from the cache. With both fixes in place this is now a no-op
    // round-trip; the explicit fan-out documents intent and shields
    // against future joystick-reset regressions.
    {
        const uint8_t cached_05 = nextreg_.cached(0x05);
        if (cached_05 != joystick_.nr_05_raw()) {
            joystick_.set_nr_05(cached_05);
        }
    }

    // Register 0x05: Joystick mode decoder — VHDL zxnext.vhd:5157-5158.
    // Phase 1 scaffold: Joystick::set_nr_05() is a stub that records the
    // raw byte; Agent A (Phase 2) implements the real bit-extraction.
    //
    // V13-NMP-01 (Pass-13 verify-audit fix): VHDL zxnext.vhd:5832-5841
    // forces `nr_05_5060 <= '0'` continuously every clock when
    // `nr_03_machine_timing(2) = '1'` (Pentagon mode). The IF branch is
    // priority-1, so even an explicit NR 0x05 write with bit 2 = 1 gets
    // overwritten on the very next clock edge — the FF never latches a
    // '1' value while Pentagon is active.
    //
    // Pre-fix the C++ stored the raw `v` byte in `regs_[0x05]`, so bit 2
    // could leak through the cache when Pentagon is on: with the
    // sequence "write bit 2 = 1 while Pentagon ON" then "exit Pentagon
    // mode", VHDL keeps the underlying FF at 0 (only a fresh NR 0x05
    // write or F3 can flip it post-Pentagon-exit), but jnext's cache
    // held bit 2 = 1 — and the cache is the pending FF the frame-edge
    // latch commits into the effective value (Task 56/58), so the
    // stale bit would eventually surface.
    //
    // Same shape as V11-NMP-02 (NR 0x0A bits 7:5 config_mode gate) and
    // V11-NMP-03 (NR 0x06 bit 2 ps2_mode config_mode gate): canonicalise
    // the stored byte at write time so the cache never leaks a value
    // the VHDL latch could not have stored. Mask bit 2 to 0 in the
    // returned canonical byte when Pentagon timing is active; bits
    // 7:3 and 1:0 are always written verbatim (those signals have no
    // Pentagon gate per VHDL :5157-5158, :5848-5852).
    nextreg_.set_write_handler(0x05, [this](uint8_t v) -> uint8_t {
        joystick_.set_nr_05(v);
        const bool pentagon =
            (nextreg_.nr_03_machine_timing() & 0x04) != 0;
        if (pentagon) {
            // VHDL :5836 forces `nr_05_5060 <= '0'` while Pentagon is
            // active; the bit-2 write is silently dropped. Force the
            // stored byte's bit 2 to 0 to mirror the FF state.
            return static_cast<uint8_t>(v & ~0x04);
        }
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
    // joy bits are PENDING signals in the read mux (plain `nr_05_joy0`
    // / `nr_05_joy1`, no `eff_` copy) — a write is visible on the very
    // next read.
    //
    // Task 58 (Item 2) — bits 2 and 0 are the EFFECTIVE, frame-edge-
    // latched signals: VHDL :5897 reads `eff_nr_05_5060` (bit 2) and
    // `eff_nr_05_scandouble_en` (bit 0), both latched from the pending
    // FFs only at `video_frame_sync = '1'` (zxnext.vhd:6696-6703). So
    // between a runtime NR 0x05 write and the next frame edge, real
    // hardware still reads the OLD values. Pre-fix jnext returned the
    // pending cache bits (write-through), leaking the new value early.
    //   * bit 2: the emulator's effective state is
    //     `video_timing_.refresh_60hz()` — repushed from the pending
    //     cache bit at the begin_new_frame() seam (Task 56).
    //   * bit 0: `eff_nr_05_scandouble_en_`, latched unconditionally
    //     at the same seam (no behavioural consumer — readback only).
    //
    // Pentagon (PASS-10 / V13-NMP-01 history): VHDL zxnext.vhd:
    // 5835-5836 forces the pending `nr_05_5060` FF to '0' continuously
    // while `nr_03_machine_timing(2) = '1'`. jnext models that in the
    // PENDING path only (NR 0x05 write handler + the NR 0x03 handler's
    // Pentagon-entry cache canonicalisation); the read no longer masks
    // at read time — the frame latch propagates the forced '0' into
    // the effective value at the next frame edge, exactly as :6700
    // does (`eff_nr_05_5060 <= nr_05_5060`). A read between Pentagon
    // entry and the frame edge correctly returns the OLD effective
    // value (VideoTiming::init_timing also forces refresh_60hz_ =
    // false for Pentagon, matching the eff latch of a forced-0
    // pending FF).
    nextreg_.set_read_handler(0x05, [this]() -> uint8_t {
        const auto m0 = static_cast<uint8_t>(joystick_.mode_left());   // 3 bits
        const auto m1 = static_cast<uint8_t>(joystick_.mode_right());  // 3 bits
        uint8_t v = 0;
        v |= static_cast<uint8_t>(((m0 >> 1) & 1u) << 7);  // joy0[1] → bit 7
        v |= static_cast<uint8_t>(((m0 >> 0) & 1u) << 6);  // joy0[0] → bit 6
        v |= static_cast<uint8_t>(((m1 >> 1) & 1u) << 5);  // joy1[1] → bit 5
        v |= static_cast<uint8_t>(((m1 >> 0) & 1u) << 4);  // joy1[0] → bit 4
        v |= static_cast<uint8_t>(((m0 >> 2) & 1u) << 3);  // joy0[2] → bit 3
        // bit 2 = eff_nr_05_5060 (frame-edge latched; zxnext.vhd:5897,
        // :6697-6700). The effective state lives in VideoTiming.
        if (video_timing_.refresh_60hz()) v |= 0x04;
        v |= static_cast<uint8_t>(((m1 >> 2) & 1u) << 1);  // joy1[2] → bit 1
        // bit 0 = eff_nr_05_scandouble_en (frame-edge latched; :6702).
        if (eff_nr_05_scandouble_en_) v |= 0x01;
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
    // Per VHDL entity port at zxnext.vhd:70 and `zxnext_top_issue2.vhd:2281`,
    //   `i_SPKEY_BUTTONS = (NOT btn_drive_divmmc_n) & (NOT btn_m1_multiface_n);`
    // i.e. ACTIVE-HIGH combinational held-state of the F9 (M1, bit 0) and
    // F10 (Drive, bit 1) front-panel buttons, sampled live on every NR 0x10
    // read. coreid is tracked in nr_10_coreid_ (5 bits), config_mode-gated
    // on writes per VHDL Issue 2/3 path. Reset default 0x01 ("00001"),
    // VHDL zxnext.vhd:1133. G56.
    //
    // V16-NMP-01 (Pass-16 verify-audit fix): pre-fix the write_handler
    // hardcoded bits 1:0 to 0 because jnext had no live SPKEY_BUTTONS
    // input. Fix wires the read mux through `test_hotkey_m1_` /
    // `test_hotkey_drive_` (the same active-high held-state booleans
    // already consumed by `nmi_assert_mf()` / `nmi_assert_divmmc()` at
    // emulator.h:411-416). Production sets these via the same
    // inject_hotkey_m1/drive setters the test harness uses; the GUI
    // F9/F10 path strobes them on press and clears them on release in a
    // follow-up wire-up patch (the strobe semantics for one-shot NMI
    // dispatch via `on_hotkey_f9_mf_nmi`/`on_hotkey_f10_divmmc_nmi` are
    // unchanged). The write_handler keeps its config_mode-gated coreid
    // update; the returned cache byte stores the static portion only —
    // bits 1:0 are recomposed on every read by the read_handler.
    nextreg_.set_write_handler(0x10, [this](uint8_t v) -> uint8_t {
        if (nextreg_.nr_03_config_mode()) {
            nr_10_coreid_ = static_cast<uint8_t>(v & 0x1F);
        }
        // Cache stores the static (button-less) portion. Read-handler
        // recomposes bits 1:0 live from i_SPKEY_BUTTONS at read time
        // (VHDL is purely combinational on those bits).
        return static_cast<uint8_t>((nr_10_coreid_ & 0x1F) << 2);
    });
    nextreg_.set_read_handler(0x10, [this]() -> uint8_t {
        // VHDL zxnext.vhd:5924 — bits 7:2 are static (bit 7 = '0', bits
        // 6:2 = nr_10_coreid). Bits 1:0 are i_SPKEY_BUTTONS combinational
        // from the live host F9/F10 held-state.
        const uint8_t coreid_bits =
            static_cast<uint8_t>((nr_10_coreid_ & 0x1F) << 2);
        const uint8_t spkey_bits =
            static_cast<uint8_t>((test_hotkey_m1_    ? 0x01 : 0x00) |
                                 (test_hotkey_drive_ ? 0x02 : 0x00));
        return static_cast<uint8_t>(coreid_bits | spkey_bits);
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
    // V14-NMP-02 (Pass-14 verify-audit fix): VHDL zxnext.vhd:6003-6004 —
    //   when X"28" =>  port_253b_dat <= nr_stored_palette_value;
    // The NR 0x28 read mux returns the VHDL signal `nr_stored_palette_value`
    // (zxnext.vhd:1190), which is latched on the FIRST half of every NR 0x44
    // 9-bit palette write (zxnext.vhd:5398-5399), NOT the byte last written
    // to NR 0x28. NR 0x28 writes only update `nr_keymap_sel` /
    // `nr_keymap_addr(8)` per VHDL :6301-6303 — the stored byte is NOT
    // surfaced on any read mux entry.
    //
    // Pre-fix the C++ NR 0x28 write_handler stored the raw byte in
    // `regs_[0x28]` and there was no read_handler, so a NR 0x28 read echoed
    // the last NR 0x28 write byte. PaletteManager's `nine_bit_first_byte_`
    // shadow already mirrors `nr_stored_palette_value`; route the read
    // through it.
    nextreg_.set_read_handler(0x28, [this]() -> uint8_t {
        return palette_.nine_bit_first_byte();
    });
    nextreg_.set_write_handler(0x29, [this](uint8_t v) -> uint8_t {
        membrane_stick_.write_nr_29(v);
        // G149: VHDL zxnext.vhd:5878-6289 read-mux has no entry for NR 0x29
        // (write-only keymap address LSB; auto-incremented internally and
        // not exposed). Fall-through is (others => '0'); return 0 so the
        // canonical regs_[0x29] stores 0 and reads return 0.
        return 0;
    });
    // V14-NMP-03 (Pass-14 verify-audit fix): VHDL zxnext.vhd:5878-6289 has
    // NO read mux entry for NR 0x2B (write-only keymap data — see VHDL
    // :6306-6307 where the only effect of `nr_2b_we` is the auto-increment
    // of `nr_keymap_addr`). Unmapped reads fall through the case statement
    // to the `when others => port_253b_dat <= (others => '0')` default at
    // VHDL :6286-6287, returning 0x00. Pre-fix the write_handler returned
    // `v`, leaving the cache holding the last-write byte — a subsequent
    // NR 0x2B read returned that byte instead of 0x00. Same shape as the
    // existing NR 0x29 canonicalisation directly above.
    nextreg_.set_write_handler(0x2B, [this](uint8_t v) -> uint8_t {
        membrane_stick_.write_nr_2b(v);
        return 0;
    });
    // V14-NMP-04 (Pass-14 reviewer-promoted from deferred class-c):
    // NR 0x2A is a fully dead register in VHDL — the write strobe is
    // commented out at zxnext.vhd:4850 (`-- when X"2A" => nr_2a_we ...`),
    // the write-side process at :6312-6319 is also commented out, and
    // there is NO read-mux entry in the case statement at :5878-6289.
    // Therefore real hardware:
    //   * Writes to NR 0x2A: ignored, no flip-flop state changes.
    //   * Reads of NR 0x2A: return 0x00 via the `when others =>
    //     (others => '0')` fall-through at :6286-6287.
    // Pre-fix the C++ NR 0x2A had NO write_handler and NO read_handler:
    // a write went raw to `regs_[0x2A]` (NextReg::write at nextreg.cpp:454)
    // and a read returned that cached byte (NextReg::read :411). Same
    // cache-leak shape as V14-NMP-03 (NR 0x2B), so the same canonicalisation
    // pattern applies — install a write_handler that drops the byte and
    // canonicalises the cache to 0. No subsystem dispatch, since real
    // hardware does nothing with the byte either.
    nextreg_.set_write_handler(0x2A, [](uint8_t /*v*/) -> uint8_t {
        return 0;
    });

    // Registers 0xB0 / 0xB1 / 0xB2 — extended keyboard matrix + MD6 extras.
    // VHDL zxnext.vhd:6206-6215 read compositions. Phase 1 scaffold: the
    // underlying byte getters return 0xFF / 0 until Agents G (extended
    // keys) and D (MD6) fill them in.
    nextreg_.set_read_handler(0xB0, [this]() -> uint8_t { return keyboard_.nr_b0_byte(); });
    nextreg_.set_read_handler(0xB1, [this]() -> uint8_t { return keyboard_.nr_b1_byte(); });
    // NR 0xB2 reads the live connector vectors (zxnext.vhd:6214-6215 reads
    // i_JOY_LEFT/i_JOY_RIGHT — the same signal the port 0x1F/0x37 composers
    // read). In jnext the host adapter drives those vectors straight into
    // Joystick, so Joystick is that signal; the Md6ConnectorX2 cable model
    // is not on the host input path (nothing feeds set_raw_left/right, and
    // its 6-button detect wants the physical select-pulse handshake a host
    // gamepad never performs), so sourcing here would read a constant 0.
    nextreg_.set_read_handler(0xB2, [this]() -> uint8_t { return joystick_.nr_b2_byte(); });

    // Register 0x15: Sprite and layer system setup
    //   bit 7 = LoRes enable (VHDL zxnext.vhd:5229 nr_15_lores_en)
    //   bit 6 = sprite priority (0=sprite 0 on top when zero_on_top)
    //   bit 5 = sprite border clip enable
    //   bits 4:2 = layer priority (SLU/LSU/SUL/LUS/USL/ULS)
    //   bit 1 = sprites over border
    //   bit 0 = sprites visible
    // Per-line granularity decision (G02, GH #73 — VHDL zxnext.vhd:5229-5234
    // decomposes the write into six registers, each with its own consumer):
    //   b7 lores_en        — per-line via the LoRes narrow snapshot (GH #63,
    //                        Lores::init_per_line/snapshot; VHDL 6817 latches
    //                        lores_en every CLK_14).  The NR 0x15 change log
    //                        below deliberately does NOT apply b7: exactly ONE
    //                        mechanism drives b7 per line.
    //   b6 zero_on_top, b5 border_clip_en, b1 over_border
    //                      — SpriteEngine live state, frame granularity
    //                        (unchanged; outside the renderer-owned log).
    //   b4:2 layer_priority, b0 sprite_en
    //                      — Renderer per-scanline change log (write_nr15):
    //                        VHDL latches them into the video pipeline every
    //                        pixel clock (6799 layer_priorities_0, 6819
    //                        sprite_en_0), so a mid-frame write takes effect
    //                        at raster granularity — per-line in jnext's
    //                        model.  render_frame rewinds/applies/flushes.
    //   b0 additionally mirrors into SpriteEngine::sprites_visible_ (live)
    //   for the NR read-back path and save-state; the render gate itself
    //   consumes the per-line-replayed Renderer::sprite_en_ (renderer.cpp).
    nextreg_.set_write_handler(0x15, [this](uint8_t v) -> uint8_t {
        renderer_.lores().set_enabled((v & 0x80) != 0);  // VHDL 5229, gate at 6933
        sprites_.set_zero_on_top((v & 0x40) != 0);
        sprites_.set_border_clip_en((v & 0x20) != 0);  // bit 5 — VHDL sprites.vhd 1044
        sprites_.set_over_border((v & 0x02) != 0);
        sprites_.set_sprites_visible((v & 0x01) != 0);
        renderer_.write_nr15(v);   // b0 + b4:2, per-line log — VHDL 6799/6819/6934/7216
        return v;
    });
    // VHDL zxnext.vhd:5939 — NR 0x15 read composes:
    //   nr_15_lores_en [7] & nr_15_sprite_priority [6]
    //     & nr_15_sprite_border_clip_en [5]
    //     & nr_15_layer_priority [4:2]
    //     & nr_15_sprite_over_border_en [1]
    //     & nr_15_sprite_en [0]
    // Every bit uses an authoritative subsystem accessor, bit 7 included
    // since the LoRes generator landed (GH #63). G56.
    nextreg_.set_read_handler(0x15, [this]() -> uint8_t {
        uint8_t v = renderer_.lores().enabled() ? 0x80 : 0x00;  // lores_en
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
    //
    // PASS-5 FIX: write-only — VHDL zxnext.vhd:5878-6289 read mux has no
    // entry for NR 0x75-0x79 (fall-through `(others => '0')`). Previously
    // the handler returned `v`, leaking the just-written byte through the
    // cached `regs_[]` read path. Match VHDL by returning 0 and aligning
    // with the NR 0x35-0x39 no-inc handler convention above.
    for (int i = 0; i < 5; ++i) {
        nextreg_.set_write_handler(static_cast<uint8_t>(0x75 + i),
            [this, i](uint8_t v) -> uint8_t {
                sprites_.write_attr_byte_nr_per_byte_inc(
                    static_cast<uint8_t>(i), v);
                return 0;
            });
    }

    // Register 0x2F: Tilemap X scroll MSB (bits 1:0)
    // VHDL zxnext.vhd:5330-5331 — `nr_30_tm_scrollx(9:8) <= nr_wr_dat(1:0)`:
    // only bits 1:0 are stored. Read at zxnext.vhd:6017-6018:
    //   port_253b_dat <= "000000" & nr_30_tm_scrollx(9:8);
    // bits 7:2 always read back as zero. VERIFY4 / pass-4 — pre-fix the bare
    // regs_[0x2F] echoed the full 8-bit write, so reads diverged from VHDL
    // whenever firmware wrote upper bits.
    nextreg_.set_write_handler(0x2F, [this](uint8_t v) -> uint8_t {
        tilemap_.set_scroll_x_msb(v);
        return static_cast<uint8_t>(v & 0x03);
    });

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
    nextreg_.set_write_handler(0x6A, [this](uint8_t v) -> uint8_t {
        // VHDL zxnext.vhd:5456-5458 — b5 radastan, b4 dfile XOR, b3:0 offset.
        renderer_.lores().set_nr6a(v);
        return static_cast<uint8_t>(v & 0x3F);
    });

    // Registers 0x32 / 0x33: LoRes X / Y scroll (VHDL zxnext.vhd:5340, 5343;
    // consumed at lores.vhd:82, 84).  Full 8 bits stored; read-back is the
    // plain cached byte (zxnext.vhd:6027, 6030), so no read handler.
    nextreg_.set_write_handler(0x32, [this](uint8_t v) -> uint8_t {
        renderer_.lores().set_scroll_x(v);
        return v;
    });
    nextreg_.set_write_handler(0x33, [this](uint8_t v) -> uint8_t {
        renderer_.lores().set_scroll_y(v);
        return v;
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
    // V15-NMP-01 (Pass-15 verify-audit fix): VHDL zxnext.vhd:5878-6289 has
    // NO read mux entry for NR 0x63 (write-only Copper data byte; the only
    // observable effect is the auto-increment of `nr_copper_addr` and the
    // optional latch into `nr_copper_data_stored` when `nr_copper_addr(0)='0'`
    // per zxnext.vhd:5433-5439). Unmapped reads fall through the case
    // statement to the `when others => port_253b_dat <= (others => '0')`
    // default at :6286-6287, returning 0x00. Pre-fix the C++ write_handler
    // returned `v`, leaving the cache holding the last-write byte — a
    // subsequent NR 0x63 read returned that byte instead of 0x00. Same
    // shape as the existing G149 (NR 0x60), V14-NMP-03 (NR 0x2B), and
    // V14-NMP-04 (NR 0x2A) canonicalisations.
    nextreg_.set_write_handler(0x63, [this](uint8_t v) -> uint8_t { copper_.write_reg_0x63(v); return 0; });

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
    // Page 0xFF = re-engage legacy auto-paging for the slot (VHDL:
    // mmu_A21_wr_en = '0' when page = xFF; slot then follows the
    // sram_rom-derived ROM mapping for slots 0/1, or the
    // 7FFD/DFFD-derived RAM mapping for slots 6/7).
    //
    // G46(b) Wave 8 fix: previously this hardcoded slots 0/1 to physical
    // pages 0/1 (= bank 0 ROM) when v=$FF, regardless of current
    // sram_rom. That broke the supervisor's $15A0 NEXTREG $51,$10 +
    // $15B0 NEXTREG $51,$FF pattern: with sram_rom=01 (= bank 1) at
    // $15B0, the supervisor expected slot 1 to revert to bank 1 hi
    // (page 03), but jnext set it to bank 0 hi (page 01) — making the
    // subsequent $1506 CALL $3E80 dispatcher read bank 0's NEXTREG
    // $8E,$01 instead of bank 1's $8E,$00, flipping to the wrong
    // bank, RET-ing into bank 1 NOPs at $2668, NOP-sliding through
    // 7424 bytes into the $5B00 toggle wrapper, RET to $0000, panic
    // loop. Real Next supervisor never reaches the $5B00 wrapper.
    for (int i = 0; i < 8; ++i) {
        nextreg_.set_write_handler(static_cast<uint8_t>(0x50 + i),
            [this, i](uint8_t v) -> uint8_t {
                if (v == 0xFF) {
                    if (i == 0 || i == 1) {
                        // VHDL zxnext.vhd:4611-4612, :3052 — value 0xFF on
                        // slots 0/1 re-engages legacy ROM auto-paging
                        // (sram_rom-derived ROM page). Use the per-slot
                        // helper so the OTHER slot's prior NR 0x50/0x51
                        // mapping is preserved (VHDL nr_mmu_we writes only
                        // MMU<i>, NOT both slots).
                        mmu_.engage_legacy_rom_paging_slot(i);
                    } else {
                        // Slots 2-7 with v=0xFF: VHDL zxnext.vhd:4686-4696
                        // stores 0xFF in MMU<i>; the SRAM arbiter at :3061
                        // then sees mmu_A21_A13(8)='1' and emits
                        // sram_pre_active='0' — slot becomes inactive
                        // (reads return floating-bus 0xFF, writes dropped).
                        // set_page(i, 0xFF) achieves that: nr_mmu_=0xFF,
                        // slots_=0xFF, read_only_=false → rebuild_ptr
                        // nullifies read/write pointers. The prior
                        // engage_legacy_ram_paging() (slots 6/7) and
                        // map_rom(i,0) (slots 2-5) both diverged from VHDL
                        // by forcing a non-0xFF mapping. Inert in current
                        // boot but VHDL-faithful.
                        mmu_.set_page(i, 0xFF);
                    }
                } else {
                    mmu_.set_page(i, v);
                }
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
    //
    // VHDL zxnext.vhd:5982-5986 wires BOTH read handlers to `cvc` — the
    // copper-offset counter (o_vc_cu), NOT the raw frame vc:
    //     when X"1E" => port_253b_dat <= "0000000" & cvc(8);
    //     when X"1F" => port_253b_dat <= std_logic_vector(cvc(7 downto 0));
    // `cvc` (zxula_timing.vhd:455-472) is reset to `i_cu_offset` at the first
    // active PAPER line (`ula_min_vactive`, raw vc == c_min_vactive) and
    // increments each line, wrapping at c_max_vc. So its origin is the first
    // paper row (with cu_offset=0), NOT the frame top — the same origin the
    // line-interrupt comparator uses (:577 `cvc == int_line_num`).
    //
    // Bug (GH #16 / Task 76): the old handler returned the RAW frame vc
    // (0 = frame top), 64 lines off from `cvc` for 128K/Next timing. Any
    // code polling NR 0x1E/0x1F for a raster line — e.g. z88dk's
    // waitForScanline() / getActiveVideoLineWord() — therefore matched its
    // target 64 lines (8 tile rows) too early, so a scroll write meant for
    // the top of the frame landed ~24 rows down (David Crespo's videoint
    // test: 24 fixed + 6 scrolling + 2 fixed instead of 30 scrolling + 2).
    // See the line-interrupt firing math in line_int_master_cycle_offset()
    // and begin_new_frame(), which already use this exact cvc origin.
    //
    // GH #265 — WHEN the read samples cvc. An IN from 0x253B returns
    // port_253b_dat_0 (zxnext.vhd:2819), a register reloaded from
    // port_253b_dat on EVERY CLK_CPU falling edge (:5871-5876), while
    // port_253b_dat follows cvc on CLK_28 (:5878-5882,5982-5986). The T80 latches the
    // data bus into DI_Reg on the falling edge of the I/O cycle's T3
    // (t80na.vhd:214-222), and with IOWait = 1 (t80na.vhd:184) that cycle is
    // T1, TW, T2, T3 (t80n.vhd:1781-1782 holds TState at 1 for one clock), so
    // DI_Reg takes the port_253b_dat_0 loaded on the PREVIOUS falling edge:
    // 2.5 T-states into the I/O cycle — the falling edge of its third clock
    // (index 2), after any contention stretch of the cycle so far. jnext used
    // clock_, the START of the instruction — 10.5 T-states early for
    // IN A,(C), 9.5 for IN A,(n) — so a loop polling for a line left it up
    // to one iteration late.
    static constexpr unsigned kPort253bReloadClock = 2;
    nextreg_.set_read_handler(0x1E, [this]() -> uint8_t {
        const int cvc = cvc_at(io_read_sample_cycle(kPort253bReloadClock));
        return static_cast<uint8_t>((cvc >> 8) & 0x01);
    });
    nextreg_.set_read_handler(0x1F, [this]() -> uint8_t {
        const int cvc = cvc_at(io_read_sample_cycle(kPort253bReloadClock));
        return static_cast<uint8_t>(cvc & 0xFF);
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
        // V19-IM2-01 fix: VHDL zxnext.vhd:5297 — NR 0x22 bit 1 also writes
        // the `nr_22_line_interrupt_en` flip-flop, which feeds
        // `im2_int_en[0]` (= LINE device int_en) per :1950 + :6711. Pre-fix
        // jnext only updated `video_timing_.set_line_interrupt_enable()`
        // (the line-int generation gate), but the IM2 fabric's
        // `dev_[DevIdx::LINE].int_en` was NOT updated. Result: in IM2 mode,
        // a NR 0x22 ← bit1=1 enabled line-int generation, but the IM2
        // wrapper edge-detect would NOT latch im2_int_req (because int_en
        // stayed 0), so the daisy-chain S_REQ never asserted — the CPU
        // never received the interrupt. NR 0xC4 bit 1 is a hardware mirror
        // (writes the SAME flip-flop, VHDL :5610) and DOES update IM2's
        // LINE int_en — so jnext was already self-inconsistent.
        // Discriminative: write NR 0x22 ← 0x02; verify
        // im2_.dev_[LINE].int_en == true via raise_req+tick observability.
        im2_.set_int_en(Im2Controller::DevIdx::LINE, (v & 0x02) != 0);
        const uint16_t cur = video_timing_.line_interrupt_target();
        video_timing_.set_line_interrupt_target(
            static_cast<uint16_t>((cur & 0xFF) | ((v & 0x01) << 8)));
        // VHDL zxnext.vhd:3619-3620 — `nr_22_we` fans bit 2 of the new
        // value into `port_ff_reg(6)` (the ULA-int-disable bit).
        port_ff_reg_ = static_cast<uint8_t>((port_ff_reg_ & 0xBF)
                                          | ((v & 0x04) << 4));
        // V19-IM2-02 fix: VHDL zxnext.vhd:6711 — `ula_int_en(0) =
        // NOT port_ff_interrupt_disable` (= NOT port_ff_reg(6)). That bit
        // feeds `im2_int_en[11]` (= ULA device int_en) per :1949. Pre-fix
        // jnext maintained `ula_int_disabled_` + `video_timing_.set_interrupt_enable`
        // shadows but the IM2 fabric's `dev_[DevIdx::ULA].int_en` was
        // NEVER updated — meaning every FRAME-INT raised via raise_req(ULA)
        // in IM2 mode would set int_status but NOT im2_int_req (int_en=0),
        // so the daisy-chain stayed in S_0 and no INT line was ever
        // asserted to the Z80 in IM2 mode. Mirror the port_ff_reg(6) →
        // ULA int_en propagation here so all three writers (port-FF,
        // NR 0x22 b2, NR 0xC4 b0-NOT) keep IM2 fabric in sync.
        im2_.set_int_en(Im2Controller::DevIdx::ULA,
                        (port_ff_reg_ & 0x40) == 0);
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
    //
    // GH #265 — bit 7 as the IN sees it: port_253b_dat takes NOT
    // pulse_int_n on the CLK_28 edge before the port_253b_dat_0 reload
    // (zxnext.vhd:5871-5882, :5991-5992) — the edge io_read_sample_cycle()
    // returns — and pulse_int_n changes on CLK_28 FALLING edges
    // (:2017-2031). The pulse state used to be the one at the end of the
    // previous instruction.
    nextreg_.set_read_handler(0x22, [this]() -> uint8_t {
        bool pulse_low;
        if (cpu_.executing()) {
            const uint64_t k = io_read_sample_cycle(kPort253bReloadClock);
            sync_io_devices_to(k, k);
            pulse_low = im2_.pulse_low_before(k);
        } else {
            pulse_low = !im2_.pulse_int_n();
        }
        const uint8_t bit7 = pulse_low ? 0x80 : 0x00;
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

        // VHDL zxnext.vhd:5119 — `nr_02_bus_reset <= nr_wr_dat(7)`.
        // Captured verbatim on every NR 0x02 write. Surfaced on the
        // NR 0x02 readback bit 7 (VHDL:5891) and routed to
        // `o_RESET_PERIPHERAL` (VHDL:1579) — peripheral / ESP /
        // expansion-bus reset pin not modelled in jnext. The signal
        // has no reset clause anywhere in zxnext.vhd, so the latch
        // survives both hard and soft reset.
        // Pass-3 verify-audit (Task 2): pre-fix this bit was hard-
        // coded zero on readback.
        nr_02_bus_reset_ = (v & 0x80) != 0;

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
            Log::emulator()->debug("Hard reset requested via NextREG 0x02 ({:#04x}) PC={:#06x} rom_bank={:#04x} mmu7={:#04x} - deferred to host cold boot",
                                  v, regs.PC, mmu_.current_rom_bank(), mmu_.get_page(7));
            // Task 70: a hard reset is a power-on cold boot performed by the
            // host (reconstruct + init) — it cannot run from inside run_frame().
            // Record the request; the frontend performs it after run_frame().
            request_hard_reset();
        } else if (v & 0x01) {
            const auto regs = cpu_.get_registers();
            Log::emulator()->debug("Soft reset triggered via NextREG 0x02 ({:#04x}) PC={:#06x} rom_bank={:#04x} mmu7={:#04x}",
                                  v, regs.PC, mmu_.current_rom_bank(), mmu_.get_page(7));
            soft_reset();
        }
        // bit 7 alone (RESET_ESPBUS) is intentionally ignored — no ESP.

        // Pass-8 verify-audit fix (2026-05-09): VHDL zxnext.vhd:3319 gates
        // the X"7F" Flash-CS decode on `(nr_03_config_mode='1') OR
        // (nr_02_reset_type(2)='1')`. After a soft-reset strobe (bit 0)
        // the reset_type FSM advances and may transition the bit-2 latch,
        // changing the gate. Re-fan-out to SpiMaster so a Flash-select
        // write on port 0xE7 immediately observes the post-strobe gate.
        spi_.set_flash_cs_enable(
            nextreg_.nr_03_config_mode()
            || ((nmi_source_.reset_type() & 0x04) != 0));
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
        // VHDL zxnext.vhd:5891 layout:
        //   bit 7   = nr_02_bus_reset
        //   bits 6:5 = "00"
        //   bit 4   = nr_02_iotrap = nr_da_iotrap_cause(1) OR (0)  (VHDL:3885)
        //   bit 3   = nr_02_generate_mf_nmi
        //   bit 2   = nr_02_generate_divmmc_nmi
        //   bits 1:0 = nr_02_reset_type(1:0)
        //
        // NmiSource owns bits 3, 2, 1, 0 (FSM-derived). Bit 4 is composed
        // here from the iotrap-cause shadow (`nr_da_iotrap_cause_`) so a
        // poll-loop watching NR 0x02 can see "any trap pending". Bit 7
        // (bus_reset) is composed from `nr_02_bus_reset_` (the latch
        // captured on every NR 0x02 write per VHDL:5119) — Pass-3 verify-
        // audit fix; pre-fix this bit was hard-coded zero.
        uint8_t v = nmi_source_.nr_02_read();
        if ((nr_da_iotrap_cause_ & 0x03) != 0) v |= 0x10;
        if (nr_02_bus_reset_)                  v |= 0x80;
        return v;
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

    // V19R-NMP-NIT-03 — NR 0xF0 XADC composed-read stub.
    //
    // VHDL zxnext.vhd:6273-6274 read mux:
    //   when X"F0" => port_253b_dat <= nr_f0_xdev_cmd;
    //
    // The driver of `nr_f0_xdev_cmd` is board-issue dependent:
    //   • Issue 2/3 (g_board_issue <= 1, zxnext.vhd:7420-7436):
    //     nr_f0_xdev_cmd is hard-wired to (others => '0') — always 0x00,
    //     irrespective of writes to NR 0xF0.
    //   • Issue 4/5 (g_board_issue >= 2, zxnext.vhd:7438-7484):
    //     `nr_f0_xdev_cmd` composes from XADC live state (`i_XADC_BUSY`,
    //     `nr_f0_xadc_eoc`, `nr_f0_xadc_eos`) when xadc/xdna are enabled.
    //
    // jnext seeds NR 0x0F (= g_board_issue) to 0x00 (Issue 2; see
    // src/port/nextreg.cpp). The Issue 2 VHDL path therefore applies,
    // and the VHDL-faithful readback is a constant 0x00. The XADC and
    // XDNA blocks are not modeled in jnext — there is no boot path nor
    // any NextZXOS code that depends on the XADC temperature/voltage
    // monitor, so this stub is conservative.
    //
    // Pre-V19R the read fell through to `regs_[0xF0]` which leaked the
    // raw last-written byte (Class-(c) inert divergence). The stub
    // returns the VHDL-correct constant regardless of writes.
    nextreg_.set_read_handler(0xF0, []() -> uint8_t {
        return 0x00;
    });

    // V19R-NMP-NIT-04 — NR 0xF8 XADC-daddr readback bit-7 mask.
    //
    // VHDL zxnext.vhd:6277-6278 read mux:
    //   when X"F8" => port_253b_dat <= '0' & nr_f8_xadc_daddr;
    // Bit 7 is hard-wired to '0' on read regardless of board issue.
    //
    // VHDL write path zxnext.vhd:7553-7558 (Issue 4/5):
    //   nr_f8_xadc_dwe   <= nr_wr_dat(7);     -- bit 7 stored elsewhere
    //   nr_f8_xadc_daddr <= nr_wr_dat(6:0);   -- only 7 bits land in daddr
    // For Issue 2/3 path zxnext.vhd:7425, nr_f8_xadc_daddr is (others=>'0')
    // — writes don't update it at all; the readback is therefore always 0.
    //
    // jnext is Issue 2 per NR 0x0F = 0x00, so the strictest VHDL-faithful
    // stub would return 0x00 always. The conservative-but-Issue-4/5-aware
    // stub is `regs_[0xF8] & 0x7F` — that drops the bit-7 leak and matches
    // VHDL for any board issue (jnext could conceivably be retargeted to
    // Issue 4/5 in the future).
    //
    // Pre-V19R the bare cache stored the full byte, leaking bit 7 on
    // readback (Class-(c) inert divergence). The XADC is not modeled and
    // never exercised by NextZXOS boot, so this is non-functional, but the
    // strict VHDL-faithfulness rule requires the mask.
    nextreg_.set_read_handler(0xF8, [this]() -> uint8_t {
        return static_cast<uint8_t>(nextreg_.cached(0xF8) & 0x7F);
    });

    // V20-NMP-XADC — NR 0xF9 / NR 0xFA XADC d0/d1 composed-read stubs.
    //
    // VHDL zxnext.vhd:6280-6284 read mux:
    //   when X"F9" => port_253b_dat <= nr_f9_xadc_d0;
    //   when X"FA" => port_253b_dat <= nr_fa_xadc_d1;
    //
    // The drivers of these signals are board-issue dependent:
    //   • Issue 2/3 (g_board_issue <= 1, zxnext.vhd:7420-7436):
    //     `nr_f9_xadc_d0` and `nr_fa_xadc_d1` are hard-wired to
    //     (others => '0') (lines 7428-7429), irrespective of writes to
    //     NR 0xF9/0xFA. The NR-write decoder still strobes `nr_f9_we` /
    //     `nr_fa_we` (lines 4823-4824, 4904-4905), but the register
    //     signals themselves are never updated on the Issue 2/3 path.
    //   • Issue 4/5 (g_board_issue >= 2, zxnext.vhd:7438+):
    //     The clocked processes around line 7570+ latch `nr_wr_dat` into
    //     `nr_f9_xadc_d0` / `nr_fa_xadc_d1` on `nr_f9_we` / `nr_fa_we`.
    //     The 16-bit XADC `o_XADC_DI` output (line 1719) concatenates
    //     them as `nr_fa_xadc_d1 & nr_f9_xadc_d0`.
    //
    // jnext seeds NR 0x0F (= g_board_issue) to 0x00 (Issue 2; see
    // src/port/nextreg.cpp). The Issue 2 VHDL path therefore applies,
    // and the VHDL-faithful readback is a constant 0x00 for both NR 0xF9
    // and NR 0xFA. The XADC block is not modeled in jnext — no NextZXOS
    // boot path nor any application is known to depend on the XADC
    // temperature/voltage monitor, so this stub is conservative.
    //
    // Pre-V20 the reads fell through to `regs_[0xF9]` / `regs_[0xFA]`
    // which leaked the raw last-written byte (Class-(c) inert
    // divergence). The stubs return the VHDL-correct constant
    // regardless of writes. Same shape as V19R-NMP-NIT-03 for NR 0xF0.
    nextreg_.set_read_handler(0xF9, []() -> uint8_t {
        return 0x00;
    });
    nextreg_.set_read_handler(0xFA, []() -> uint8_t {
        return 0x00;
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
            Log::emulator()->debug("Boot ROM disabled by NextREG 0x03 write ({:#04x})", v);
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

            // V13-NMP-01 (Pass-13 verify-audit fix): VHDL zxnext.vhd:5832-
            // 5841 forces `nr_05_5060 <= '0'` continuously every clock
            // when `nr_03_machine_timing(2) = '1'` (Pentagon). The IF
            // branch fires on EVERY clock edge — so the moment a NR 0x03
            // write commits a Pentagon timing, the FF is cleared on the
            // very next clock. The C++ `regs_[0x05]` cache tracks the
            // last write through NextReg; without an explicit clear here,
            // a pre-Pentagon NR 0x05 write with bit 2 = 1 would leak
            // through the cache once Pentagon is later exited (the read
            // mask at line 1166 only fires while Pentagon is live).
            // Mirror the FF behaviour by clearing cached bit 2 on the
            // timing→Pentagon transition. Same shape as the V11-NMP-02 /
            // V11-NMP-03 cache-canonicalisation pattern, applied here on
            // the gating-state-change edge instead of the gated-write
            // edge. Bits 7:3 / 1:0 are unaffected.
            if ((new_timing & 0x04) != 0) {
                const uint8_t cached_05 =
                    static_cast<uint8_t>(nextreg_.cached(0x05) & ~0x04);
                // Re-write the canonicalised value through NextReg::write,
                // which routes through the NR 0x05 write_handler (see
                // `nextreg.cpp:451-452`) and hence fans out to Joystick.
                // That is benign here — joy0/joy1 are encoded in bits 7:5
                // and 4:3 respectively; only bit 2 is being cleared, so
                // Joystick's mode selectors are unaffected and the call is
                // idempotent for the cache-canonicalisation purpose.
                nextreg_.write(0x05, cached_05);
            }

            // G121: VHDL zxnext.vhd:2033 — pulse_count_end gates on
            // `machine_timing_48 OR machine_timing_p3`, so the pulse-mode INT
            // width follows a runtime NR 0x03 timing change (32 cycles for
            // 48K/+3, 36 for 128K/Pentagon). Those two are decoded from
            // eff_nr_03_machine_timing (:5761-5776), the copy latched at
            // video_frame_sync (:6696-6703) — not from this write — so the
            // Im2Controller / Z80Cpu gates follow at the frame edge, in
            // begin_new_frame(), with contention and the raster. Setting
            // them here applied the new width up to a frame early.

            // V24-MEM-01 / V25-MEM-01 fix — push the decoded
            // MachineTimingMode (tim_sel axis) into the SHADOW field of
            // ContentionModel and Mmu. VHDL :5121-5135 writes
            // `nr_03_machine_timing` immediately on the gated NR 0x03
            // write; the latch `eff_nr_03_machine_timing` then commits
            // on the next video-frame edge (:6694-6703 —
            // `video_frame_sync='1'`). jnext models the same two-step
            // commit via set_pending_machine_timing() (immediate
            // shadow) + commit_pending_machine_timing() (frame-edge
            // promotion, called from run_frame()).
            //
            // The IM2 / CPU pulse-width gate (VHDL `pulse_count_end`,
            // :2033) reads the same effective latch, so it is deferred
            // with this axis (see G121 above).
            const MachineTimingMode tim_mode = decode_nr_03_machine_timing(new_timing);
            contention_.set_pending_machine_timing(tim_mode);
            mmu_.set_pending_machine_timing(tim_mode);
        }

        // dt_lock XOR-toggle (VHDL :5135) — unconditional XOR with bit 3.
        nextreg_.set_nr_03_user_dt_lock(nextreg_.nr_03_user_dt_lock() ^ bit3);

        // Machine-type commit (VHDL :5137-5145) — gated on CURRENT config_mode.
        // G46(b): VHDL :2981-3008 derives machine_type_48 / machine_type_p3
        // signals from nr_03_machine_type and feeds them into the sram_rom
        // selection logic. Without mirroring the new type into Mmu, our
        // current_sram_rom() stayed on the boot-time CLI value (typically
        // ZXN_ISSUE2 = 1-bit rom_bank) even after the supervisor wrote
        // NR $03,$B3 to enter +3 mode. NextZXOS's RST $28; DW $1661 chain
        // expects 4-way bank routing in +3 mode — without it, slot 0/1
        // mapped to bank 1 (BASIC string-scan loop) instead of bank 3
        // (LDDR;RET memory move), so font-blit copies never executed.
        // Verified against CSpect via DZRP at 2026-05-08: real Next runs
        // in +3 mode at this boot phase.
        if (nextreg_.nr_03_config_mode()) {
            const uint8_t typ_sel = static_cast<uint8_t>(v & 0x07);
            MachineType new_mt = mmu_.machine_type();
            bool commit = false;
            switch (typ_sel) {
                case 0x01: nextreg_.set_nr_03_machine_type(0x01);
                           new_mt = MachineType::ZX48K;       commit = true; break;
                case 0x02: nextreg_.set_nr_03_machine_type(0x02);
                           new_mt = MachineType::ZX128K;      commit = true; break;
                case 0x03: nextreg_.set_nr_03_machine_type(0x03);
                           new_mt = MachineType::ZX_PLUS3;    commit = true; break;
                case 0x04: nextreg_.set_nr_03_machine_type(0x04);
                           // Pentagon: VHDL :5751 maps typ_sel=$04 to
                           // machine_type_128, which falls into the same
                           // sram_rom 1-bit "else" branch as ZXN
                           // (VHDL :2997-3007). Our MachineType enum
                           // models that family as ZXN_ISSUE2 — Pentagon
                           // and Next share current_sram_rom() behavior.
                           new_mt = MachineType::ZXN_ISSUE2;  commit = true; break;
                default: /* VHDL :5143 others => null (no change) */ break;
            }
            if (commit && new_mt != mmu_.machine_type()) {
                Log::emulator()->debug(
                    "machine_type committed via NR 0x03: {} -> {} (typ_sel={:#04x})",
                    static_cast<int>(mmu_.machine_type()),
                    static_cast<int>(new_mt), typ_sel);
                mmu_.set_machine_type(new_mt);
                // Verify7-memory class-(a) fix: VHDL `sram_rom3`
                // (zxnext.vhd:2981-3008) is per-machine-type — switching
                // to/from 48K (which hardwires sram_rom3='1') or +3 (which
                // requires both port_1ffd_rom bits) flips the signal even
                // when ports are unchanged. Repush the new value to
                // DivMmc so the ROM3-conditional auto-map gate
                // (zxnext.vhd:3138) tracks the machine-type change.
                divmmc_.set_rom3_active(mmu_.sram_rom3());
                // Verify9-memory class-(c) → class-(a) fix: VHDL
                // mem_contend (:4490-4492) and the wait_s window
                // (zxula.vhd:582-583, +3-only `hc_adj[3:1]=000` corner)
                // are per-machine-type — a NR 0x03 commit must rebuild
                // ContentionModel's LUT + per-machine bank decode.
                // Pre-fix the LUT stayed pinned to the boot-time CLI
                // type; a Next-mode firmware that committed +3 via
                // typ_sel=$03 would miss the +3-specific corner of
                // wait_s, and Next→48K/128K transitions would miss the
                // bank-decode change. `rebuild_for_type` preserves
                // dynamic gate state (cpu_speed / contention_disable /
                // mem_active_page / shadows) so this commit doesn't
                // unwind paging.
                contention_.rebuild_for_type(new_mt);
            }
        }

        // config_mode FSM — must run AFTER machine-type commit so the commit
        // sees the PREVIOUS config_mode (VHDL :5137 reads the pre-write value).
        nextreg_.apply_nr_03_config_mode_transition(v);
        // Mirror config_mode into Mmu so the fast-path read/write routing
        // (VHDL zxnext.vhd:3044-3050) tracks the NextReg state.
        mmu_.set_config_mode(nextreg_.nr_03_config_mode());
        // Pass-8 verify-audit fix (2026-05-09): VHDL zxnext.vhd:3319 gates
        // the X"7F" Flash-CS decode on `nr_03_config_mode='1'` (OR
        // reset_type(2)='1'). Re-fan-out to SpiMaster so a Flash-select
        // write on port 0xE7 immediately observes the new gate.
        spi_.set_flash_cs_enable(
            nextreg_.nr_03_config_mode()
            || ((nmi_source_.reset_type() & 0x04) != 0));
        Log::emulator()->debug("NextREG 0x03 ← {:#04x}  (config_mode={})",
                              v, nextreg_.nr_03_config_mode() ? 1 : 0);
        return v;
    });

    // Register 0x03 read: composed per VHDL zxnext.vhd:5894 —
    //   port_253b_dat <= nr_palette_sub_idx & nr_03_machine_timing(2:0) &
    //                    nr_03_user_dt_lock & nr_03_machine_type(2:0)
    //
    // V21-NMP-01 (Pass-21 verify-audit fix): bit 7 surfaces VHDL
    // `nr_palette_sub_idx` (zxnext.vhd:1182), the same FF the NR 0x44
    // 9-bit-palette-write toggle drives (:5403) and the NR 0x40 / 0x41 /
    // 0x43 writes reset to '0' (:5376 / :5382 / :5395); the global
    // reset block (:5000) also clears it. NR 0x28 was previously
    // mis-cited as a resetter — it is the keymap-select write
    // (:6301-6303, drives nr_keymap_sel / nr_keymap_addr), unrelated
    // to nr_palette_sub_idx [V21R-NMP-NIT-01 doc fix].
    // PaletteManager owns the identical `nine_bit_first_written_` shadow
    // (palette.cpp:330-351 toggles; palette.cpp:220, palette.cpp:209,
    // palette.cpp:86 / etc. reset paths). Pre-fix bit 7 was hard-wired
    // to 0, so a poll-loop checking "second 9-bit write phase armed?"
    // via NR 0x03 read would observe a stuck-low bit while VHDL would
    // toggle it. Class-(c) inert divergence (no jnext boot path polls
    // NR 0x03 bit 7 today, but the readback contract is part of the
    // VHDL surface and a NR 0x44 + NR 0x03 sequence in any future
    // firmware would observe the bug).
    nextreg_.set_read_handler(0x03, [this]() -> uint8_t {
        const uint8_t timing = static_cast<uint8_t>(nextreg_.nr_03_machine_timing() & 0x07);
        const uint8_t mtype  = static_cast<uint8_t>(nextreg_.nr_03_machine_type()   & 0x07);
        const uint8_t dtlock = nextreg_.nr_03_user_dt_lock() ? 1 : 0;
        const uint8_t sub_idx = palette_.nine_bit_first_written() ? 1 : 0;
        return static_cast<uint8_t>((sub_idx << 7) | (timing << 4) | (dtlock << 3) | mtype);
    });

    // Register 0x04: ROM/RAM bank select used by tbblue.fw's load_roms() to
    // populate Spectrum/DivMMC/Multiface ROMs in SRAM while config_mode=1.
    // VHDL zxnext.vhd:1104,5716-5732 — we take all 8 bits (Issue-5 behaviour);
    // out-of-range banks fall back to 0xFF reads via Ram::page_ptr()==nullptr.
    nextreg_.set_write_handler(0x04, [this](uint8_t v) -> uint8_t {
        // VHDL zxnext.vhd:5709-5722 (gen_romram_234, board issue ≤ 2):
        //   nr_04_romram_bank <= '0' & nr_wr_dat(6 downto 0);
        // i.e. bit 7 is forced to zero on Issue 2/3/4 boards (jnext defaults
        // to ZXN_ISSUE2 — see emulator_config.h:67). Issue 5 stores the full
        // 8 bits (gen_romram_5, :5724-5736). VERIFY4 / pass-4: prior code
        // forwarded the raw byte verbatim to the SRAM address compose, so a
        // firmware write of NR 0x04 with bit 7 set would route slot 0/1
        // page index `(0xFF << 1) | slot` = page 0x1FE/0x1FF, out of the
        // emulated 1 MiB SRAM range (256 × 8 KiB pages = pages 0..0xFF).
        const uint8_t bank = static_cast<uint8_t>(v & 0x7F);
        nextreg_.set_nr_04_romram_bank(bank);
        mmu_.set_nr_04_romram_bank(bank);
        Log::emulator()->debug("NextREG 0x04 ← {:#04x}  (romram_bank, masked to {:#04x})", v, bank);
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
    //   bit 4 = cancel extended keys (wired below to Keyboard; issue #33)
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
        // Bit 4 = cancel extended-key entries in the 8x5 matrix
        // (zxnext.vhd:5447 -> :1584 o_KBD_CANCEL, acted on at
        // membrane.vhd:183-186). A level: while set, the extended keys stop
        // folding into the matrix but NR 0xB0/0xB1 keep reporting them.
        keyboard_.set_cancel_extended_entries((v & 0x10) != 0);
        return v;
    });
    // VHDL zxnext.vhd:6093 reads NR 0x68 by composing bit 3 from the live
    // `port_ff3b_ulap_en` rather than from a stored copy. Port 0xFF3B writes
    // mutate ulap_en_ without touching regs_[0x68], so the cached snapshot
    // diverges. Compose bit 3 from the live state at read time.
    //
    // V20-NMP-02 (Pass-20 verify-audit fix): VHDL zxnext.vhd:6093 read mux
    // composes NR 0x68 as
    //   (not nr_68_ula_en) & nr_68_blend_mode & nr_68_cancel_extended_keys
    //                      & port_ff3b_ulap_en & nr_68_ula_fine_scroll_x
    //                      & '0' & nr_68_ula_stencil_mode
    // — bit 1 is a LITERAL '0', irrespective of writes. The NR 0x68 write
    // decoder at :5444-5450 has NO `nr_68_*(1)` storage (the entire bit-1
    // field is absent from the write case). Pre-fix the C++ stored the
    // raw written byte in `regs_[0x68]` and the read mask `cached & 0xF7`
    // preserved bit 1 verbatim, leaking the last-written bit-1 value on
    // readback. Add bit 1 to the read mask: `cached & 0xF5` (= 1111 0101)
    // clears bit 1 AND bit 3 (bit 3 is recomposed from live ulap_en
    // immediately below). Class-(c) inert divergence.
    nextreg_.set_read_handler(0x68, [this]() -> uint8_t {
        uint8_t v = nextreg_.cached(0x68);
        v = (v & 0xF5) | (renderer_.ula().get_ulap_en() ? 0x08 : 0x00);
        return v;
    });

    // Register 0x69: Display Control 1
    //   bit 7 = Layer 2 enable
    //   bit 6 = ULA shadow display (bank 7)
    //   bits 5:4 = Timex modes (deferred)
    //   bits 3:0 = reserved
    nextreg_.set_write_handler(0x69, [this](uint8_t v) -> uint8_t {
        layer2_.set_enabled((v & 0x80) != 0);
        // V13-MEM-01 fix — VHDL zxnext.vhd:3924-3925 fans NR 0x69 bit 7
        // (`nr_wr_dat(7)`) into `port_123b_layer2_en` (the SAME FF that
        // port 0x123B bit 1 latches at :3916). The port 0x123B readback
        // at :3933 surfaces this FF as bit 1 of port_123b_dat, so a
        // subsequent IN A,(123B) after a `NEXTREG $69,$80` write must
        // see bit 1 = 1. Pre-fix Layer2's enabled_ shadow was updated
        // (used by the NR 0x69 read handler) but Mmu's l2_enable_
        // mirror was NOT — port 0x123B readback lagged Layer2 until the
        // next port 0x123B write. Mirror the same bit into Mmu so both
        // surfaces stay in sync.
        mmu_.set_l2_enable((v & 0x80) != 0);
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

    // Register 0x8A: Bus-port propagate (low 6 bits only).
    // VHDL zxnext.vhd:5524-5525 — `nr_8a_bus_port_propagate <= nr_wr_dat(5:0)`
    // (bits 7:6 of write are dropped). VHDL zxnext.vhd:6152-6153 read:
    //   port_253b_dat <= "00" & nr_8a_bus_port_propagate;
    // VERIFY4 / pass-4 — pre-fix the bare regs_[0x8A] echoed the full byte;
    // canonicalise on the write side so the readback matches VHDL.
    nextreg_.set_write_handler(0x8A, [](uint8_t v) -> uint8_t {
        return static_cast<uint8_t>(v & 0x3F);
    });

    // Registers 0x90 / 0x93 — Pi GPIO output enable (bit-mask gating).
    // VHDL zxnext.vhd:5537 — NR 0x90: `nr_90_pi_gpio_o_en <= nr_wr_dat(7:2) & "00"`
    //   i.e. bits 1:0 are forced 0 (jnext does not enable output on GPIO 1:0).
    // VHDL zxnext.vhd:5546 — NR 0x93: `nr_93_pi_gpio_o_en <= nr_wr_dat(3:0)`.
    // Read mux at zxnext.vhd:6164-6174 returns the stored byte verbatim
    // (with NR 0x93 prefixed by "0000"). VERIFY4 / pass-4 — pre-fix the
    // bare regs_[0x90 / 0x93] echoed the full write byte. Pi GPIO is not
    // boot-relevant but the readback contract is part of the VHDL surface.
    nextreg_.set_write_handler(0x90, [](uint8_t v) -> uint8_t {
        return static_cast<uint8_t>(v & 0xFC);
    });
    nextreg_.set_write_handler(0x93, [](uint8_t v) -> uint8_t {
        return static_cast<uint8_t>(v & 0x0F);
    });

    // Registers 0x98 / 0x99 / 0x9A / 0x9B — Pi GPIO INPUT readback.
    // VHDL zxnext.vhd:6176-6186 — reads return `i_GPIO(...)` (the live Pi
    // GPIO INPUT pins), NOT the corresponding nr_9*_pi_gpio_o write shadow.
    // Reads decode as:
    //   NR 0x98 → i_GPIO(7:0)
    //   NR 0x99 → i_GPIO(15:8)
    //   NR 0x9A → i_GPIO(23:16)
    //   NR 0x9B → "0000" & i_GPIO(27:24)
    // jnext does not model the Pi GPIO bus. Returning 0 matches the VHDL
    // contract for "no Pi attached" better than the default last-written-byte
    // shadow (which leaks the `nr_9*_pi_gpio_o` output state into the input
    // read path). VERIFY4 / pass-4.
    nextreg_.set_read_handler(0x98, []() -> uint8_t { return 0x00; });
    nextreg_.set_read_handler(0x99, []() -> uint8_t { return 0x00; });
    nextreg_.set_read_handler(0x9A, []() -> uint8_t { return 0x00; });
    nextreg_.set_read_handler(0x9B, []() -> uint8_t { return 0x00; });

    // Register 0xA8 — ESP GPIO0 output enable (bit 0 only).
    // VHDL zxnext.vhd:5570 — `nr_a8_esp_gpio0_en <= nr_wr_dat(0)`.
    // Read at zxnext.vhd:6197-6198: `"0000000" & nr_a8_esp_gpio0_en` (mask 0x01).
    nextreg_.set_write_handler(0xA8, [](uint8_t v) -> uint8_t {
        return static_cast<uint8_t>(v & 0x01);
    });

    // Register 0xA9 — ESP GPIO0 input value.
    // VHDL zxnext.vhd:6200-6201:
    //   port_253b_dat <= "00000" & i_ESP_GPIO_20(2) & '0' & i_ESP_GPIO_20(0);
    // Read returns ESP-side GPIO INPUT bits 2 and 0 (with bit 1 forced to 0),
    // NOT the nr_a9_esp_gpio0 output shadow. jnext does not model the ESP
    // GPIO; return 0 (no ESP attached) instead of the last-written shadow
    // to avoid leaking the output state into the input read.
    nextreg_.set_read_handler(0xA9, []() -> uint8_t { return 0x00; });

    // --- DivMMC automap config (NextREG 0xB8-0xBB) ---
    //
    // V17-NMP-01 (Pass-17 verify-audit fix): VHDL zxnext.vhd:5087-5090 reset
    // these registers to 0x83/0x01/0x00/0xCD respectively (the master reset
    // block at :4928-5100 fires unconditionally on `i_reset`). The DivMmc
    // subsystem stores the same values in `entry_points_0_`/`entry_valid_0_`/
    // `entry_timing_0_`/`entry_points_1_` (divmmc.h:342-345 + divmmc.cpp:43-46).
    // Pre-fix the NR 0xB8-0xBB readback path fell through to `regs_[reg]`
    // (NextReg::read at :408-414), which `regs_.fill(0)` in NextReg::reset()
    // had cleared to 0x00 — so a Z80 IN A,(0x253B) at NR 0xB8/0xB9/0xBA/0xBB
    // returned 0x00/0x00/0x00/0x00 instead of the VHDL-spec 0x83/0x01/0x00/0xCD.
    // Software that polls these registers (e.g. firmware verifying the
    // automap entry-point configuration after reset) sees stale defaults.
    //
    // Fix: register read_handlers that pull live from the DivMmc subsystem,
    // mirroring the same accessor pattern used for NR 0x12/NR 0x13 (which
    // also have authoritative subsystem state — Layer2). Read mux per
    // VHDL zxnext.vhd:6217-6227 returns each storage byte verbatim.
    nextreg_.set_write_handler(0xB8, [this](uint8_t v) -> uint8_t { divmmc_.set_entry_points_0(v); return v; });
    nextreg_.set_write_handler(0xB9, [this](uint8_t v) -> uint8_t { divmmc_.set_entry_valid_0(v); return v; });
    nextreg_.set_write_handler(0xBA, [this](uint8_t v) -> uint8_t { divmmc_.set_entry_timing_0(v); return v; });
    nextreg_.set_write_handler(0xBB, [this](uint8_t v) -> uint8_t { divmmc_.set_entry_points_1(v); return v; });
    nextreg_.set_read_handler(0xB8, [this]() -> uint8_t { return divmmc_.entry_points_0(); });
    nextreg_.set_read_handler(0xB9, [this]() -> uint8_t { return divmmc_.entry_valid_0(); });
    nextreg_.set_read_handler(0xBA, [this]() -> uint8_t { return divmmc_.entry_timing_0(); });
    nextreg_.set_read_handler(0xBB, [this]() -> uint8_t { return divmmc_.entry_points_1(); });

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
        // V16-NMP-02: route both DivMMC (b0) and Multiface (b1, VHDL
        // zxnext.vhd:2415-2416 `internal_port_enable(9)`, gates
        // multiface.vhd:64 `enable_i`) gates through
        // propagate_effective_port_enables so expbus_eff_en=1 correctly
        // ANDs in NR 0x87. The cache for NR 0x83 is committed AFTER
        // this handler returns, so pass `v` as an in-flight override.
        propagate_effective_port_enables(/*override_reg=*/0x83, v);
        return v;
    });

    // Register 0x85: Port-enable register 4 — read packing.
    // VHDL zxnext.vhd:6138: read returns reset_type & "000" & enable(3:0).
    // Bits 6:4 always read back as zero.
    nextreg_.set_read_handler(0x85, [this]() -> uint8_t {
        return nextreg_.cached(0x85) & 0x8F;
    });
    // V15-CPU-NIT-03 (reviewer-promoted): NR 0x85 bit 0 ("port_ulap_io_en")
    // gates the `port_bf3b`/`port_ff3b` ULA+ contention OR-terms
    // (VHDL zxnext.vhd:2439 + 2685-2686 + 4496). The CPU-side
    // I/O bus callbacks (fuse_z80_readport / fuse_z80_writeport in
    // src/cpu/z80_cpu.cpp) don't have access to NextReg state, so
    // we mirror the bit into ContentionModel via a shadow setter
    // (mirroring the Verify9-memory `port_7ffd_io_en_` pattern).
    // ContentionModel::contention_tick() OR-folds the shadow with
    // its parameter so the bit is consulted regardless of caller.
    nextreg_.set_write_handler(0x85, [this](uint8_t v) -> uint8_t {
        // V16-NMP-02: route through propagate_effective_port_enables so
        // expbus_eff_en=1 ANDs in NR 0x89 b0 per VHDL :2392-2393. Cache
        // for NR 0x85 is committed AFTER this handler returns, so pass
        // `v` as an in-flight override.
        propagate_effective_port_enables(/*override_reg=*/0x85, v);
        return v;
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

    // V16-NMP-02 (Pass-16 verify-audit fix): NR 0x86-0x89 are the
    // `nr_8X_bus_port_enable` AND-mask group. Per VHDL zxnext.vhd:2392-2393,
    // when `expbus_eff_en=1` the effective `internal_port_enable` formula
    // ANDs each NR 0x86-0x89 byte with the corresponding NR 0x82-0x85.
    // Pre-fix, jnext stored writes in regs_[0x86..0x89] but never
    // recomputed the downstream port-decode shadows on a NR 0x86-0x89
    // change — so a write that should have gated a port off (with
    // expbus_eff_en=1) was inert. Wire each write through
    // propagate_effective_port_enables(). The bare NextReg::write stores
    // `v` in regs_[reg] before the handler runs, so the helper sees the
    // fresh byte.
    nextreg_.set_write_handler(0x86, [this](uint8_t v) -> uint8_t {
        propagate_effective_port_enables(/*override_reg=*/0x86, v);
        return v;
    });
    nextreg_.set_write_handler(0x87, [this](uint8_t v) -> uint8_t {
        propagate_effective_port_enables(/*override_reg=*/0x87, v);
        return v;
    });
    nextreg_.set_write_handler(0x88, [this](uint8_t v) -> uint8_t {
        propagate_effective_port_enables(/*override_reg=*/0x88, v);
        return v;
    });
    nextreg_.set_write_handler(0x89, [this](uint8_t v) -> uint8_t {
        propagate_effective_port_enables(/*override_reg=*/0x89, v);
        return v;
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
        // P0 boot probe (doc/issues/nextzxos-boot/ZXGO-COMPARISON-2026-07-09.md):
        // env-gated, zero cost when unset.
        if (std::getenv("JNEXT_BOOT_PROBE")) {
            Log::emulator()->info(
                "BOOT_PROBE NR 0x8C <= {:#04x} at PC={:#06x}", v,
                cpu_.get_registers().PC);
        }
        mmu_.set_nr_8c(v);
        rom_.set_alt_rom_config(v);
        // Verify7-memory class-(a) fix: VHDL `sram_rom3` (zxnext.vhd:2990,
        // 3000) factors NR 0x8C altrom lock bits per machine type. An NR
        // 0x8C write that flips lock_rom1/lock_rom0 must repush sram_rom3
        // to DivMmc so the ROM3-conditional auto-map gate (zxnext.vhd:3138)
        // tracks the new lock state — VHDL drives it combinationally; we
        // need an explicit refresh because DivMmc caches the bit.
        divmmc_.set_rom3_active(mmu_.sram_rom3());
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
        // G46(b)-v2 / EOD-29 fix: NR $8E writes update port_7ffd /
        // port_1ffd internally (per VHDL :3662-3734), which changes the
        // sram_rom selector. The VHDL drives `sram_rom3` combinationally
        // from those ports (zxnext.vhd:2981-3008); emulator-side, DivMmc
        // caches the value, so we must re-push it after every NR $8E
        // write — same pattern as the OUT ($1FFD),A handler at
        // emulator.cpp:3399. Without this, the supervisor's
        // NEXTREG $8E,$03 boot-time bank-flip (at $3CFC during NextZXOS
        // dispatcher cycles) doesn't refresh DivMmc's ROM3 gate, so the
        // $3Dxx wildcard automap fires neither its trigger
        // (`divmmc_automap_rom3_instant_on` at zxnext.vhd:2898-2899) nor
        // its enable gate (`sram_divmmc_automap_rom3_en` at zxnext.vhd:3138)
        // in jnext while they both fire in CSpect — diverting jnext's M1
        // fetch at $3D00 into block-3 font data while CSpect's M1 fetch at
        // $3D00 routes through the DivMMC overlay (which RETs immediately,
        // popping $0448 from the stack). Regression test:
        // N8E-DIVMMC-ROM3-REFRESH in test/nextreg/nextreg_integration_test.cpp.
        divmmc_.set_rom3_active(mmu_.sram_rom3());
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
        // V12-NMP-01 (Pass-12 verify-audit fix): VHDL zxnext.vhd:3635 ties
        // `port_ff_interrupt_disable <= port_ff_reg(6)` combinationally —
        // both the NR 0x22 b2 fan-out (:3620) AND the NR 0xC4 b0 (NOT)
        // fan-out (:3622) feed the same flip-flop. VHDL :6711 then drives
        // `ula_int_en <= ... & (not port_ff_interrupt_disable)` into the
        // ULA-INT comparator. jnext maintains a separate `ula_int_disabled_`
        // shadow which the NR 0x22 write_handler at emulator.cpp:1657 sets
        // from `(v & 0x04) != 0`, but the NR 0xC4 write_handler did NOT
        // mirror that fan-out. Symptom: software writing NR 0xC4 ← 0x01 to
        // ENABLE the ULA INT correctly clears `port_ff_reg_(6)`, but the
        // C++ `ula_int_disabled_` shadow + `video_timing_.interrupt_enable()`
        // gate stay stuck at the previous state — the scheduler keeps the
        // ULA INT off, AND the NR 0xC4 read at line 2417 returns the stale
        // shadow instead of the live `port_ff_reg(6)` state. Both NR 0x22
        // and NR 0xC4 must update the same shadows; mirror the NR 0x22
        // pattern (write_handler + video_timing_ fan-out).
        ula_int_disabled_ = (port_ff_reg_ & 0x40) != 0;
        video_timing_.set_interrupt_enable(!ula_int_disabled_);
        // V19-IM2-02 fix: VHDL zxnext.vhd:6711 — `ula_int_en(0) =
        // NOT port_ff_interrupt_disable` feeds `dev_[ULA].int_en` of the
        // IM2 fabric (line :1949 bit 11). NR 0xC4 bit 0 (NOT) writes
        // port_ff_reg(6); update the IM2 fabric's ULA int_en too so that
        // a "ULA INT enable" via NR 0xC4 ← 0x01 actually allows the IM2
        // wrapper to latch im2_int_req on raise_req(ULA) for IM2-mode
        // FRAME interrupts. See V19-IM2-02 in NR 0x22 + port-FF for the
        // mirror writers.
        im2_.set_int_en(Im2Controller::DevIdx::ULA,
                        (port_ff_reg_ & 0x40) == 0);
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
    //
    // GH #265 — both directions are timed against the interrupt requests.
    // A read returns im2_int_status as port_253b_dat took it on the edge
    // before the port_253b_dat_0 reload (zxnext.vhd:5871-5882, 6247-6254):
    // the requests raised by then are resolved first (they used to reach
    // the fabric only after the instruction). A write clears on the edge it
    // commits on (nr_write_edge_), after the requests raised before it and
    // losing to one raised on that same edge (im2_peripheral.vhd:160).
    nextreg_.set_write_handler(0xC8, [this](uint8_t v) -> uint8_t {
        if (v & 0x02) clear_im2_status(Im2Controller::DevIdx::LINE);
        if (v & 0x01) clear_im2_status(Im2Controller::DevIdx::ULA);
        return v;
    });
    nextreg_.set_read_handler(0xC8, [this]() -> uint8_t {
        return im2_.int_status_mask_c8(im2_status_read_edge());
    });

    // NR 0xC9 — CTC 7..0 (each bit clears the corresponding CTC status).
    // VHDL :1953: bits 3..10 of im2_status_clear are gated by nr_c9_we
    // AND nr_wr_dat bits 0..7 respectively. CTC4..CTC7 bits are still
    // honoured here (even though those devices are hardwired 0 upstream)
    // to match VHDL literal decode.
    nextreg_.set_write_handler(0xC9, [this](uint8_t v) -> uint8_t {
        if (v & 0x01) clear_im2_status(Im2Controller::DevIdx::CTC0);
        if (v & 0x02) clear_im2_status(Im2Controller::DevIdx::CTC1);
        if (v & 0x04) clear_im2_status(Im2Controller::DevIdx::CTC2);
        if (v & 0x08) clear_im2_status(Im2Controller::DevIdx::CTC3);
        if (v & 0x10) clear_im2_status(Im2Controller::DevIdx::CTC4);
        if (v & 0x20) clear_im2_status(Im2Controller::DevIdx::CTC5);
        if (v & 0x40) clear_im2_status(Im2Controller::DevIdx::CTC6);
        if (v & 0x80) clear_im2_status(Im2Controller::DevIdx::CTC7);
        return v;
    });
    nextreg_.set_read_handler(0xC9, [this]() -> uint8_t {
        return im2_.int_status_mask_c9(im2_status_read_edge());
    });

    // NR 0xCA — UART (per VHDL :1952,:1954).
    //   bit 6       = clear UART1 TX
    //   bit 5 | b4  = clear UART1 RX (OR, either bit clears)
    //   bit 2       = clear UART0 TX
    //   bit 1 | b0  = clear UART0 RX (OR, either bit clears)
    nextreg_.set_write_handler(0xCA, [this](uint8_t v) -> uint8_t {
        if (v & 0x40)          clear_im2_status(Im2Controller::DevIdx::UART1_TX);
        if (v & 0x30)          clear_im2_status(Im2Controller::DevIdx::UART1_RX);
        if (v & 0x04)          clear_im2_status(Im2Controller::DevIdx::UART0_TX);
        if (v & 0x03)          clear_im2_status(Im2Controller::DevIdx::UART0_RX);
        return v;
    });
    nextreg_.set_read_handler(0xCA, [this]() -> uint8_t {
        return im2_.int_status_mask_ca(im2_status_read_edge());
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
    //
    // GH #265 — timed like NR 0xC8-0xCA (see there): the read sees the
    // requests raised by the IN's sample edge, and a write's unqualified
    // request is raised on the cycle nr_20_we is high, the one before the
    // commit edge (zxnext.vhd:1946-1947).
    nextreg_.set_read_handler(0x20, [this]() -> uint8_t {
        const uint64_t k = im2_status_read_edge();
        uint8_t v = 0;
        if (im2_.int_status(Im2Controller::DevIdx::LINE, k)) v |= 0x80;  // bit 7 = LINE
        if (im2_.int_status(Im2Controller::DevIdx::ULA, k))  v |= 0x40;  // bit 6 = ULA
        if (im2_.int_status(Im2Controller::DevIdx::CTC0, k)) v |= 0x01;
        if (im2_.int_status(Im2Controller::DevIdx::CTC1, k)) v |= 0x02;
        if (im2_.int_status(Im2Controller::DevIdx::CTC2, k)) v |= 0x04;
        if (im2_.int_status(Im2Controller::DevIdx::CTC3, k)) v |= 0x08;
        return v;
    });
    nextreg_.set_write_handler(0x20, [this](uint8_t v) -> uint8_t {
        // Per VHDL: each set bit drives int_unq for the matching device.
        // raise_unq() is a one-shot: sets int_status + im2_int_req bypassing
        // int_en, exactly matching UNQ-04 / UNQ-05 invariants.
        auto unq = [this](Im2Controller::DevIdx d) {
            if (nr_write_edge_ == Im2Controller::kNoTime) im2_.raise_unq(d);
            else im2_.raise_unq(d, nr_write_edge_ - 1);
        };
        if (v & 0x80) unq(Im2Controller::DevIdx::LINE);
        if (v & 0x40) unq(Im2Controller::DevIdx::ULA);
        if (v & 0x01) unq(Im2Controller::DevIdx::CTC0);
        if (v & 0x02) unq(Im2Controller::DevIdx::CTC1);
        if (v & 0x04) unq(Im2Controller::DevIdx::CTC2);
        if (v & 0x08) unq(Im2Controller::DevIdx::CTC3);
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
    // V18-NMP-NIT-01: VHDL zxnext.vhd:2635 gates port_123b on
    // `port_layer2_io_en = '1'`, where
    // `port_layer2_io_en <= internal_port_enable(15)` = NR 0x83 bit 7
    // (VHDL :2424). When cleared, the port is silenced.
    port_.register_handler(0xFFFF, 0x123B,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x80) == 0) return 0xFF;
            // VHDL zxnext.vhd:3933 read-back composition (G145).
            return mmu_.l2_port_readback();
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x80) == 0) return;  // NR 0x83 b7 gate
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
            // VHDL :2593 (port_7ffd):
            //   cpu_a(15)='0' AND (cpu_a(14)='1' OR p3_timing_hw_en='0') AND ...
            // p3_timing_hw_en (VHDL :2457) one-hot mirrors `machine_timing_p3`
            // — tim_sel axis, NOT typ_sel. D3F-NIT-01 (follow-up to D3F-01):
            // route through `mmu_.machine_timing()` so a user-written
            // NR 0x03 with `tim_sel != typ_sel` toggles the A14 gate via
            // the same tim_sel axis the VHDL keys on.
            if (mmu_.machine_timing() == MachineTimingMode::TimingPlus3
                && (port & 0x4000) == 0) return;
            // VHDL 2399: port_7ffd_io_en <= internal_port_enable(1) = NR 0x82 bit 1
            // V16-NMP-02: route through effective_internal_port_enable so
            // expbus_eff_en=1 ANDs in NR 0x86 b1 per VHDL :2392-2393.
            if ((effective_internal_port_enable(0x82) & 0x02) == 0) return;

            mmu_.map_128k_bank(v);
            // VHDL zxnext.vhd:4453 — port_7ffd_reg(3) drives i_ula_shadow_en
            // on the ULA bus. Same pattern as the NR 0x68 bit 3 → set_ulap_en
            // forward (commit a1495ba). Read through the MMU accessor so the
            // bit-3 source of truth stays in the MMU (matches VHDL where
            // port_7ffd_reg is the sole producer of i_ula_shadow_en).
            renderer_.ula().set_shadow_screen_en(mmu_.shadow_screen_en());
            // Push the new ROM3 state into DivMmc. VHDL zxnext.vhd:3138
            // composites sram_divmmc_automap_rom3_en from sram_pre_rom3
            // (derived from VHDL `sram_rom3` at zxnext.vhd:2981-3008). The
            // VHDL signal is per-machine-type and factors in NR 0x8C altrom
            // locks: 48K hardwires '1', +3 is `port_1ffd_rom(1) AND port_1ffd_rom(0)`,
            // ZXN/128K is `port_1ffd_rom(0)` (= port_7ffd(4)).
            //
            // Verify7-memory class-(a) fix: the legacy `rom3_selected()`
            // observer returned `current_rom_bank() == 3` — only correct
            // for +3 with no altrom lock. On Next mode, VHDL says ROM3 is
            // active whenever `port_7ffd(4)=1` (1-bit sram_rom), but
            // `rom3_selected()` required BOTH `port_1ffd(2)` AND
            // `port_7ffd(4)`, under-reporting ROM3-active in the standard
            // Next-mode boot path and silently suppressing DivMMC's
            // ROM3-conditional auto-map. Switch to the VHDL-faithful
            // `sram_rom3()` accessor.
            divmmc_.set_rom3_active(mmu_.sram_rom3());
            // Update 0xC000 contention based on machine timing (VHDL zxnext.vhd:4489-4493):
            //   mem_contend <= ... when machine_timing_128='1' and mem_active_page(1)='1' else  -- odd
            //                  ... when machine_timing_p3 ='1' and mem_active_page(3)='1' else  -- >=4
            // The pattern selector keys on `machine_timing_*` (tim_sel axis),
            // NOT `machine_type_` (typ_sel axis). Same family as
            // V24-MEM-01 (canonical `Mmu::mem_contend_for_` consumer) and
            // D3F-01/02/03 (port-handler gates). D3F-NIT-02 (follow-up):
            // route through `mmu_.machine_timing()` so a user-written
            // NR 0x03 with `tim_sel != typ_sel` selects the correct
            // pattern via the same tim_sel axis the VHDL keys on.
            uint8_t bank = v & 0x07;
            bool slot3_contended;
            const MachineTimingMode tim = mmu_.machine_timing();
            if (tim == MachineTimingMode::TimingPlus3)
                slot3_contended = (bank >= 4);
            else if (tim == MachineTimingMode::Timing128)
                slot3_contended = (bank & 1) != 0;
            else
                slot3_contended = false;  // 48K / Pentagon: bank-switched slot 3 not contended
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
    // Pass-3 verify-audit (Task 2): both NR 0xDA cause and NR 0xD9
    // captured-write fields update only when `nmi_accept_cause = '1'`
    // per VHDL :3871 and :3892. `nmi_accept_cause` (VHDL :2164) is
    // `nmi_state = S_NMI_IDLE OR S_NMI_FETCH`. While the FSM is in
    // HOLD or END, an iotrap event must NOT update either field —
    // pre-fix the C++ updated them unconditionally on every trapped
    // port access.
    //
    // (G162 closure — MF-G162-02 in nmi_test plan.)
    port_.register_handler(0xF003, 0x2001,
        [this](uint16_t port) -> uint8_t {
            // 0x2FFD READ — trap (VHDL:3835 port_2ffd_rd term).
            // When NR 0xD8 bit 0 is off, VHDL's `port_2ffd` decode is
            // false, no other internal decode responds at this address,
            // and the read falls to `cpu_di <= X"FF"` (zxnext.vhd:1877;
            // GH #109 — pre-fix returned floating_bus_read(), the
            // port-0xFF-only mux). Gate on: FDC unmodelled → 0xFF too.
            if (nr_d8_io_trap_fdc_en_) {
                nmi_source_.strobe_iotrap();
                // VHDL zxnext.vhd:3871-3873 — port_2ffd_rd → cause "01"
                // ONLY when nmi_accept_cause (FSM in IDLE or FETCH).
                if (nmi_accept_cause_()) {
                    nr_da_iotrap_cause_ = 0x01;
                }
                return 0xFF;  // FDC data port — open bus, FDC unmodelled.
            }
            return 0xFF;  // undecoded — cpu_di <= X"FF" (zxnext.vhd:1877)
        },
        nullptr);  // 0x2FFD WRITE: VHDL-only port_2ffd_wr is NOT an iotrap source.
    port_.register_handler(0xF003, 0x3001,
        [this](uint16_t port) -> uint8_t {
            // 0x3FFD READ — trap (VHDL:3835 port_3ffd_rd term).
            if (nr_d8_io_trap_fdc_en_) {
                nmi_source_.strobe_iotrap();
                // VHDL zxnext.vhd:3874-3875 — port_3ffd_rd → cause "10"
                // ONLY when nmi_accept_cause (FSM in IDLE or FETCH).
                if (nmi_accept_cause_()) {
                    nr_da_iotrap_cause_ = 0x02;
                }
                return 0xFF;
            }
            return 0xFF;  // undecoded — cpu_di <= X"FF" (zxnext.vhd:1877)
        },
        [this](uint16_t, uint8_t v) {
            // 0x3FFD WRITE — trap (VHDL:3835 port_3ffd_wr term).
            if (nr_d8_io_trap_fdc_en_) {
                nmi_source_.strobe_iotrap();
                // VHDL zxnext.vhd:3876-3878 — port_3ffd_wr → cause "11"
                // ONLY when nmi_accept_cause (FSM in IDLE or FETCH).
                // VHDL zxnext.vhd:3892-3893 — `nr_d9_iotrap_write <= cpu_do`
                // when port_3ffd_wr AND nmi_accept_cause. Same gate.
                if (nmi_accept_cause_()) {
                    nr_da_iotrap_cause_ = 0x03;
                    nr_d9_iotrap_write_ = v;
                }
            }
        });

    // +3 paging — port 0x1FFD.
    // VHDL zxnext.vhd:2599: port_1ffd <= A15:14="00" AND A13:12="01" AND port_fd.
    // Mask: A15:12="0001", A1=0, A0=1 → 0xF003/0x1001.
    port_.register_handler(0xF003, 0x1001,
        nullptr,
        [this](uint16_t, uint8_t v) {
            // VHDL zxnext.vhd:2599: gated by port_1ffd_io_en (NR 0x82 bit 3).
            if ((effective_internal_port_enable(0x82) & 0x08) == 0) return;
            mmu_.map_plus3_bank(v);
            // Push the new ROM3 state to DivMmc (Task 7 Branch B).
            // Verify7-memory class-(a) fix: use `sram_rom3()` per VHDL
            // zxnext.vhd:2981-3008 / :3138, NOT the legacy
            // `rom3_selected()` (== current_rom_bank == 3) observer.
            divmmc_.set_rom3_active(mmu_.sram_rom3());
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
    //
    // READ (GH #52): VHDL zxnext.vhd:4603 `port_243b_dat <= nr_register`,
    // gated into the read data bus at :2818 and declared an internally
    // served read at :2804 (`port_internal_rd_response` includes
    // `port_243b_rd`) — so IN from 0x243B returns the CURRENTLY SELECTED
    // NextREG number, NOT floating bus. jnext had no read handler, so the
    // read fell through to the unmatched-port default, floating_bus_read().
    // What that yields is CONFIGURATION-DEPENDENT, so no single wrong value
    // characterises the bug: under NextZXOS it is 0x00, because NR 0x08 b2
    // and NR 0x82 b0 are both set and port 0xFF is never written, so the
    // Timex arm (this file, ~:7962, VHDL :2813) returns a zero
    // screen-mode register. With those gates clear — a bare Emulator in a
    // unit test, say — the same read yields 0xFF instead.
    //
    // That is not a cosmetic gap: NextZXOS's frame interrupt handler
    // save/restores the caller's selected register around its own NextREG
    // use — `IN A,($243B)` … keyboard scan … `OUT ($243B),A`. Reading the
    // floating bus made it write that value back instead of the real
    // selection, so a program's selected NextREG was silently repointed
    // (to NR 0x00, the read-only machine ID, in the NextZXOS case).
    //
    // Scope: this bites during the window before a launched program
    // installs its own interrupt handler — once a program takes the
    // interrupt, this ISR stops running and the clobber stops with it.
    // It is therefore program- and phase-dependent, not universal.
    // Warhawk.nex selects NR 0x1F and busy-waits for raster line 191, which
    // it never reaches because it is still in that window: it read NR 0x00
    // = 0x0A forever and hung on its title logo, so its own engine never
    // got installed and the window never closed (GH #52).
    port_.register_handler(0xFFFF, 0x243B,
        [this](uint16_t) -> uint8_t { return nextreg_.selected(); },
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

    // Z80N NEXTREG-opcode write path (GH #54). VHDL zxnext.vhd:4739-4744:
    // the opcode's requester supplies its own register number and never
    // touches `nr_register`, the port-0x243B select latch (:4592-4603).
    // Routing the opcode through the port pair — as jnext did before —
    // clobbered that latch, so a `select 0x1F once, then IN A,(0x253B) in a
    // loop` raster wait (RevivalSurvival.nex) was permanently derailed by the
    // first `NEXTREG` executed in the game's interrupt handler: the loop then
    // read whatever register the ISR left behind and never terminated.
    // Deferral semantics stay identical to the port path (G65).
    port_.nextreg_opcode_write_cb = [this](uint8_t reg, uint8_t v) {
        if (defer_cpu_nr_writes_) {
            enqueue_cpu_nr_write(reg, v);
        } else {
            nextreg_.write(reg, v);
        }
    };

    // ULA — port 0xFE (mask 0x00FF, value 0x00FE).
    // Read (VHDL zxnext.vhd:3459):
    //   port_fe_dat_0 <= '1' & (i_AUDIO_EAR or port_fe_ear) & '1' & i_KBD_COL
    // where `i_AUDIO_EAR` is the relaxed tape-input signal (zxnext_top_issue2.vhd:
    // 662-676 `ear_relax : symmetric_relaxation`, i_relax_0 = i_relax_1 =
    // zxn_issue2_fe_mic = port_fe_mic AND nr_08_keyboard_issue2, zxnext.vhd:1636).
    //
    // symmetric_relaxation.vhd:83-92: once the input has held its value for
    // COUNTER_SIZE=6 membrane ticks (~1152 us), the output is forced to
    //   (i_relax_0 and not sig) or (i_relax_1 and sig)
    // and since relax_0 == relax_1 == zxn_issue2_fe_mic here, that collapses to
    // zxn_issue2_fe_mic REGARDLESS of the tape pin's stable level. So with no
    // tape signal the steady state is ALWAYS `port_fe_mic AND nr_08_keyboard_issue2`
    // — which is 0 whenever issue-2 keyboard mode is off. The idle pin level
    // never survives the relaxation; that is the whole point of the block
    // (see its comment "RELAX STUCK AT ONE TO ZERO AFTER SOME TIME").
    //
    //   audio_ear_eff = tape_playing ? tape_bit
    //                 : issue2_on    ? port_fe_mic  (OUT 0xFE bit 3 latch)
    //                                : 0            (relaxed steady state)
    //
    // Bit 6 is then ORed with `port_fe_ear` (OUT 0xFE bit 4 latch), so an idle
    // Next reads 0xBF from port 0xFE with no key pressed — NOT 0xFF. Programs
    // that compare a keyboard half-row against an exact byte (rather than
    // masking bits 4:0) depend on this: Night-Knight-Demo tests its O/P/SPACE
    // rows for exactly 0xBD/0xBE (GH #51), which is unreachable with bit 6
    // stuck at 1.
    // Write: bits 2-0 = border colour; bit 4 = EAR; bit 3 = MIC.
    //
    // V17-NMP-02 (Pass-17 verify-audit fix): VHDL zxnext.vhd:2582 decodes
    // `port_fe <= '1' when cpu_a(0) = '0'` — the ULA matches ANY port with
    // bit 0 = 0 (the standard Spectrum 48K decode). The TBBlue real hardware
    // and every real Spectrum follow this rule. Pre-fix the handler mask
    // was 0x00FF/0x00FE, requiring the LSB to be exactly 0xFE. Software
    // using the well-known "OUT (0xFC), A" border trick (or any other even
    // port) would update border/ear/mic on real hardware but not in jnext.
    // Mask 0x0001/0x0000 = "any port with bit 0 = 0", matching VHDL :2582.
    // Most-specific-wins dispatch keeps the more-specific 0x00FE handler
    // (none exist — REG-01 in port_test simply tests several even-LSB
    // addresses) from being shadowed by this one. The other "even-LSB"
    // handlers (e.g. NMI/MF dispatch observer) fire via a separate
    // io_observer path that is independent of this handler.
    port_.register_handler(0x0001, 0x0000,
        [this](uint16_t port) -> uint8_t {
            uint8_t addr_high = static_cast<uint8_t>(port >> 8);
            // Task 19 (instant TAP load): notify the phantom typist on
            // every port-0xFE read so it can accumulate which keyboard
            // half-rows the ROM has polled this frame. Zero-cost when
            // the typist is INACTIVE (single branch). When WAITING, it
            // OR-folds the row mask and waits for `0xff` (= all 8 rows
            // scanned), which is the canonical "ROM is ready for
            // input" signal — see src/input/phantom_typist.h.
            phantom_typist_.notice_keyboard_row_read(addr_high);
            uint8_t result = 0xE0 | (keyboard_.read_rows(addr_high) & 0x1F);
            // Default `audio_ear_eff = 0` — the relaxed steady state of
            // `ear_relax` when issue-2 keyboard mode is off. Tape playback
            // overrides it; issue-2 feedback substitutes MIC.
            //
            // GH #265 — the tape level is the one port_fe_dat_0 takes on
            // the CLK_CPU falling edge 2.5 T-states into the IN's I/O cycle
            // (zxnext.vhd:3455-3464), the T-state tape_sample_offset()
            // names: the TAP player (ticked between instructions) is
            // brought up to it, TZX/WAV evaluated at it. It used to be the
            // level at the instruction's start (TAP) or one T-state before
            // the I/O cycle's data latch (TZX/WAV, the live FUSE counter).
            uint8_t audio_ear_eff = 0;
            if (tape_.is_playing()) {
                audio_ear_eff = tap_ear_for_port_read();
            } else if (tzx_tape_.is_playing()) {
                // Monotonic T-state clock (never resets across frames — G36).
                audio_ear_eff = tzx_tape_.update(tape_sample_tstates());
            } else if (wav_tape_.is_playing()) {
                audio_ear_eff = wav_tape_.get_ear_bit(tape_sample_tstates());
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
    // Read IS decoded on real hardware: `port_ff_rd <= iord and port_ff`
    // (zxnext.vhd:2713 — NOT gated by port_ff_io_en, unlike the write at
    // :2714) always asserts `port_internal_rd_response` (:2803), and the
    // read-data mux at :2813 selects Timex reg / ULA floating bus / 0xFF.
    // That mux is implemented by Emulator::floating_bus_read(), registered
    // HERE as the port's read handler (GH #109 — pre-fix it was wired as
    // the catch-all default for every unmatched port instead, leaking the
    // Timex/floating-bus mux into genuinely undecoded ports).
    // VHDL zxnext.vhd:2397: port_ff_io_en <= internal_port_enable(0) = NR 0x82 bit 0.
    //
    // V17-NMP-03 (Pass-17 verify-audit fix): VHDL zxnext.vhd:2540-2571 +
    // 2583 decode `port_ff_lsb` from `cpu_a(7:0) = X"FF"` (LSB-only). Pre-fix
    // the handler used mask 0xFFFF / val 0x00FF — which required the FULL
    // 16-bit address to equal exactly 0x00FF. Software using OUT (0x12FF), A
    // would update Timex screen-mode register on real hardware but not in
    // jnext. Fix: change mask to 0x00FF / val 0x00FF (LSB-only match).
    port_.register_handler(0x00FF, 0x00FF,
        [this](uint16_t) -> uint8_t { return floating_bus_read(); },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x82) & 0x01) == 0) return;
            // VHDL zxnext.vhd:3615-3616 — port_ff_wr branch latches
            // the entire byte into port_ff_reg, beating any NR-side
            // partial fan-out from NR 0x69 / 0x22 / 0xC4 (G108).
            port_ff_reg_ = val;
            renderer_.ula().set_screen_mode(val);
            // V12-NMP-02 (Pass-12 fix-of-reviewer): VHDL zxnext.vhd:3614-3616
            // shows three writers feeding `port_ff_reg(6)` (the canonical
            // store for `port_ff_interrupt_disable`, :3635): port-FF write
            // (full byte, :3615-3616), NR 0x22 b2 (:3619-3620), NR 0xC4 b0
            // NOT (:3621-3622). VHDL :6711 then drives
            // `ula_int_en <= ... & (not port_ff_interrupt_disable)` into
            // the ULA-INT comparator. The NR 0x22 + NR 0xC4 paths both
            // mirror the change into the C++ `ula_int_disabled_` shadow
            // and call `video_timing_.set_interrupt_enable(...)`, but the
            // direct port-0xFF write was missing the same fan-out — a
            // direct `OUT (0xFF),A` with bit 6 set could not disable the
            // ULA INT (latent gap previously documented in
            // ctc_interrupts_test.cpp ULA-INT-02 and the V12-NMP-01 path).
            // Mirror the NR 0x22 / NR 0xC4 pattern here so all three
            // writers keep the shadow + scheduler gate in sync.
            ula_int_disabled_ = (port_ff_reg_ & 0x40) != 0;
            video_timing_.set_interrupt_enable(!ula_int_disabled_);
            // V19-IM2-02 fix: VHDL zxnext.vhd:6711 — `ula_int_en(0) =
            // NOT port_ff_interrupt_disable` (= NOT port_ff_reg(6)).
            // That bit feeds `dev_[ULA].int_en` of the IM2 fabric
            // (line :1949 bit 11). port-FF writes the entire byte, so
            // bit 6 here is the port_ff_interrupt_disable polarity.
            // Update the IM2 fabric ULA int_en in lockstep so a direct
            // OUT (0xFF),A propagates correctly to the daisy chain in
            // IM2 mode (consistent with NR 0x22 / NR 0xC4 mirrors).
            im2_.set_int_en(Im2Controller::DevIdx::ULA,
                            (port_ff_reg_ & 0x40) == 0);
        });

    // +3 floating-bus surface — port 0x0FFD.
    // VHDL zxnext.vhd:2589: port_p3_float decode = A15:12="0000" AND port_fd
    // (A1:0="01") AND p3_timing_hw_en='1' AND port_p3_floating_bus_io_en='1'.
    // Mask: A15..A12=1 + A1=1 + A0=1 → 0xF003. Match: A15:12="0000",A1=0,A0=1
    // → 0x0001. The 0x7FFD handler (mask 0x8003) overlaps but is less
    // specific; most-specific-match-wins dispatch routes 0x0FFD here.
    //
    // Read mux (zxnext.vhd:4517 + zxula.vhd:573):
    //   port_p3_floating_bus_dat <= ula_floating_bus when port_7ffd_locked='0'
    //                          else X"FF";
    //   o_ula_floating_bus       <= (floating_bus_r(7:1) & (floating_bus_r(0) or i_timing_p3))
    //                                 when (border_active_ula='0' and floating_bus_en='1')
    //                          else i_p3_floating_bus when i_timing_p3='1'
    //                          else X"FF";
    //
    // Cases consumed by this handler:
    //   - Non-+3 machines: decode blocked (p3_timing_hw_en='0') → 0xFF.
    //   - port_p3_floating_bus_io_en=0 (NR 0x82 bit 4 cleared,
    //     zxnext.vhd:2403): decode blocked → 0xFF.
    //     GH #111: with the decode blocked, port_p3_float='0' (:2589),
    //     so the strobe port_p3_float_rd <= iord and port_p3_float
    //     (:2716) never asserts, port_internal_rd_response stays low
    //     (:2803-2806 — no other listed strobe decodes 0x0FFD; the
    //     port_7ffd decode that captures 0x0FFD on non-+3 timing is
    //     write-only), and the cpu_di IORQ mux delivers its
    //     unconditional default cpu_di <= X"FF" (:1877). The X"00" at
    //     :2814 is only the wired-OR contribution into port_rd_dat
    //     (:2837), never CPU-visible without its own strobe.
    //   - +3 + io_en=1 + port_7ffd_locked='1': X"FF" (zxnext.vhd:4517).
    //   - +3 + io_en=1 + port_7ffd_locked='0': ula_floating_bus, with
    //     two sub-arms:
    //       * Active display (border_active_ula='0' AND floating_bus_en='1'):
    //         floating_bus_r (last VRAM byte the ULA fetched) with bit 0
    //         OR'd with i_timing_p3 — i.e. bit 0 forced high on +3.
    //       * Border / non-active: i_p3_floating_bus = the contended-CPU
    //         latch (Mmu::p3_floating_bus_dat()) returned RAW.
    //         GH #112: zxula.vhd:573 is a conditional signal assignment
    //         (LRM 10.5.3) — an ordered priority chain of independent
    //         waveforms. The `or i_timing_p3` term sits inside the
    //         parenthesised concatenation of the FIRST waveform only;
    //         the second waveform is the bare signal name
    //         `i_p3_floating_bus`, with no operator applied. Its sole
    //         driver (zxnext.vhd:4478 port map → the clocked process at
    //         :4499-4508) latches cpu_di / cpu_do verbatim, so bit 0 is
    //         whatever the CPU bus carried. Forcing it here was a
    //         deviation.
    //
    // Verify9-memory class-(c) → class-(a) fix: pre-fix the handler
    // unconditionally returned the contended-CPU latch (border arm).
    // The active-display arm is now wired via
    // `Emulator::ula_floating_bus_active_arm()`, which mirrors the
    // 48K/128K port 0xFF VRAM-byte computation. The +3 latch is only
    // returned when the active arm is silent (border / outside the
    // ULA's 8-T-state fetch slots).
    //
    // Write-only on real hardware in this slot — we intentionally drop
    // writes (no decoded write semantic per VHDL).
    port_.register_handler(0xF003, 0x0001,
        [this](uint16_t) -> uint8_t {
            // VHDL zxnext.vhd:2589 — p3_timing_hw_en gate.
            // D3F-01 fix: gate on `machine_timing_` (tim_sel axis) per VHDL
            // :2589 `p3_timing_hw_en = '1'`. `p3_timing_hw_en` is the
            // one-hot mirror of `machine_timing_p3` (VHDL :1283); using
            // `config_.type` would silently mis-decode when NR 0x03 is
            // written with `tim_sel != typ_sel`.
            // GH #111: blocked decode → no read strobe (:2716) → no
            // port_internal_rd_response (:2803-2806) → cpu_di default
            // X"FF" (:1877), NOT the :2814 wired-OR X"00".
            if (mmu_.machine_timing() != MachineTimingMode::TimingPlus3) return 0xFF;
            // VHDL zxnext.vhd:2403 — port_p3_floating_bus_io_en = NR 0x82 bit 4.
            if ((effective_internal_port_enable(0x82) & 0x10) == 0) return 0xFF;
            // VHDL zxnext.vhd:4517 — gates on `port_7ffd_locked` (the
            // EFFECTIVE lock signal at VHDL :3769), not the raw
            // port_7ffd_reg(5) mirror. Pentagon-1024 mode (NR 0x8F=11
            // AND EFF7(2)=0) drops port_7ffd_locked to '0' even when
            // bit 5 is set; jnext's `paging_locked()` accessor exposes
            // only the raw bit-5 mirror. Verify8-memory class-(a) fix:
            // use `effective_paging_locked()` which composes the full
            // VHDL :3769 expression.
            if (mmu_.effective_paging_locked()) return 0xFF;
            // VHDL zxula.vhd:573 active arm — return the VRAM byte the
            // ULA is currently fetching during display, with bit 0 forced
            // high (i_timing_p3='1' on +3).
            uint8_t active_byte;
            if (ula_floating_bus_active_arm(active_byte)) {
                return static_cast<uint8_t>(active_byte | 0x01);
            }
            // VHDL zxula.vhd:573 second arm — border / non-active:
            // `i_p3_floating_bus` RAW. GH #112: the `or i_timing_p3`
            // bit-0 force is scoped to the first waveform (active
            // display) only; this arm is a bare signal reference whose
            // driver (zxnext.vhd:4478 → :4499-4508) stores cpu_di /
            // cpu_do unmodified.
            return mmu_.p3_floating_bus_dat();
        },
        nullptr);

    // Sprite slot select and status — port 0x303B (full 16-bit match).
    // V18-NMP-NIT-01: VHDL zxnext.vhd:2679-2681 gates port_303b /
    // port_57 / port_5b on `port_sprite_io_en = '1'`, where
    // `port_sprite_io_en <= internal_port_enable(14)` = NR 0x83 bit 6
    // (VHDL :2423). When the gate is cleared the ports are silenced
    // (read returns the floating-bus default, writes are dropped).
    // Default at reset is 1 so all current boot paths leave the gate
    // open; software-controlled silencing was broken pre-fix.
    port_.register_handler(0xFFFF, 0x303B,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x40) == 0) return 0xFF;
            return sprites_.read_status();
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x40) == 0) return;
            sprites_.write_slot_select(val);
        });

    // Sprite attributes — port 0x57 (low byte match).
    port_.register_handler(0x00FF, 0x0057,
        nullptr,
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x40) == 0) return;  // NR 0x83 b6 gate
            sprites_.write_attribute(val);
        });

    // Sprite pattern data — port 0x5B (low byte match).
    port_.register_handler(0x00FF, 0x005B,
        nullptr,
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x40) == 0) return;  // NR 0x83 b6 gate
            sprites_.write_pattern(val);
        });

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
            if ((effective_internal_port_enable(0x82) & 0x04) == 0) return;  // NR 0x82 b2 gate
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
            if ((effective_internal_port_enable(0x85) & 0x04) == 0) return;  // NR 0x85 b2 gate
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
            if ((effective_internal_port_enable(0x84) & 0x01) == 0) return 0xFF;  // gated off
            return turbosound_.reg_read(false);
        },
        [this](uint16_t port, uint8_t val) {
            if ((effective_internal_port_enable(0x84) & 0x01) == 0) return;  // gated off
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
            if ((effective_internal_port_enable(0x84) & 0x01) == 0) return 0xFF;  // gated off
            // D3F-02 fix: gate on `machine_timing_` (tim_sel axis) per VHDL
            // :2771 `port_bffd and machine_timing_p3`; previously keyed on
            // `config_.type` and silently mis-decoded when NR 0x03 was
            // written with `tim_sel != typ_sel`.
            if (mmu_.machine_timing() != MachineTimingMode::TimingPlus3) return 0xFF;  // +3 alias only
            return turbosound_.reg_read(false);
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x84) & 0x01) == 0) return;  // gated off
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
            if ((effective_internal_port_enable(0x84) & 0x01) == 0) return 0xFF;  // gated off
            return turbosound_.reg_read(true);   // reg_mode = true
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x84) & 0x01) == 0) return;  // gated off
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
            if ((effective_internal_port_enable(0x84) & 0x02) == 0) return;   // SD1 b1 gate
            dac_.write_channel(0, val);
        });
    port_.register_handler(0x00FF, 0x000F, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            // 0x0F: SD1 ch B (b1) OR Covox ch B (b4).
            if ((effective_internal_port_enable(0x84) & 0x12) == 0) return;   // b1 | b4
            dac_.write_channel(1, val);
        });
    port_.register_handler(0x00FF, 0x004F, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            // 0x4F: SD1 ch C (b1) OR Covox ch C (b4).
            if ((effective_internal_port_enable(0x84) & 0x12) == 0) return;   // b1 | b4
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
    // V18-NMP-02 (Pass-18 verify-audit fix): VHDL zxnext.vhd:2661, :2664 decode
    // the Profi-Covox channel A (0x3F) / channel D (0x5F) writes using ONLY
    // the LSB — they OR-fold with `port_3f_lsb` / `port_5f_lsb` which the
    // LSB decoder at :2549, :2553 sets when `cpu_a(7:0) = X"3F"` / `X"5F"`
    // (A15..A8 are DON'T-CARE). Pre-fix the handlers used mask 0xFFFF / val
    // 0x003F (etc.) requiring the full 16-bit address to match exactly —
    // software using e.g. OUT (0x123F), A would write the Profi DAC ch A on
    // real hardware but not in jnext. Fix: change masks to 0x00FF / val 0x003F
    // (etc.). This makes them equally-specific (8 bits) to the MF+3 readback
    // handler at LSB 0x3F (read-only, registered earlier), so the dispatcher
    // routes reads to the MF readback and writes to the DAC — both as VHDL
    // intends since both decodes are parallel in hardware.
    port_.register_handler(0x00FF, 0x003F, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            if ((effective_internal_port_enable(0x84) & 0x08) == 0) return;   // Profi b3 gate
            dac_.write_channel(0, val);
        });
    port_.register_handler(0x00FF, 0x005F, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            // 0x5F reachable via SD1 (b1) OR Profi (b3).
            if ((effective_internal_port_enable(0x84) & 0x0A) == 0) return;   // b1 | b3
            dac_.write_channel(3, val);
        });

    // Soundrive Mode 2: 0xF1=A, 0xF3=B, 0xF9=C, 0xFB=D (vhd:2432).
    // NR 0x84 bit 2 = port_dac_sd2_ABCD_f1f3f9fb_io_en (G114).
    // Port 0xFB ALSO mirrors Pentagon mono ch A+D fan-out (vhd:2660)
    // when port_dac_mono_AD_fb_io_en is effective, so the 0xFB handler
    // below honours the VHDL gate composition instead of unconditionally
    // routing to ch D.
    //
    // V18-NMP-03 (Pass-18 verify-audit fix): VHDL zxnext.vhd:2661-2664 fold
    // these into `port_dac_A/B/C/D` via `port_f1_lsb / port_f3_lsb / port_f9_lsb
    // / port_fb_lsb` — LSB-only decode (A15..A8 don't-care). Pre-fix the
    // handlers used mask 0xFFFF, requiring full 16-bit match; software writing
    // to e.g. 0x12F1 would update DAC ch A on real hardware but not in jnext.
    // Fix: change all four to mask 0x00FF.
    // G146 — Soundrive-SD2 vs FD-family paging write conflict.
    // VHDL zxnext.vhd:2708:
    //   port_fd_conflict_wr <= (port_f1_lsb and port_dac_sd2_ABCD_f1f3f9fb_io_en)
    //                       or (port_f9_lsb and port_dac_sd2_ABCD_f1f3f9fb_io_en);
    // and zxnext.vhd:2718-2720, 2725: port_7ffd_wr / port_dffd_wr /
    // port_1ffd_wr / port_3ffd_wr are each ANDed with NOT port_fd_conflict_wr.
    // Only low bytes 0xF1 and 0xF9 can conflict: they have A1:A0="01" and so
    // also satisfy the FD-family decode (port_fd, zxnext.vhd:2578); 0xF3/0xFB
    // have A1:A0="11" and can never alias a paging port — the VHDL omits them
    // from the conflict term, and so do we.
    //
    // In jnext both halves fall out of the exclusive most-specific-wins
    // dispatch, because the SD2 handlers (mask 0x00FF, 8 bits) always beat
    // the paging handlers (0x8003: 3 bits, 0xF003: 6 bits) on a colliding
    // address such as 0x7FF1 / 0xDFF9 / 0x1FF1:
    //   * SD2 gate ON  (NR 0x84 b2, internal_port_enable(18), vhd:2429-2430):
    //     this handler wins and the paging handler never runs — full
    //     suppression of the 7FFD/DFFD/1FFD/3FFD write, including the +3
    //     motor bit (vhd:3755) and the 3FFD FDC-trap strobe (vhd:3892),
    //     which live inside those suppressed handlers. The DAC side is NOT
    //     gated by the conflict (vhd:2775-2778, 2661-2663): the byte goes to
    //     the Soundrive channel, subject only to nr_08_dac_en (which resets
    //     the soundrive module, vhd:6436 — it does NOT re-enable paging).
    //   * SD2 gate OFF: port_fd_conflict_wr = 0 and the F1/F9 decode does
    //     not exist — decline so dispatch falls through to the paging
    //     handler that also matches the address (vhd:2718-2725 fire again).
    port_.register_handler(0x00FF, 0x00F1, nullptr,
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x84) & 0x04) == 0) {   // SD2 b2 gate
                port_.decline_write();   // decode absent → FD paging may fire (G146)
                return;
            }
            if (!dac_enabled_) return;   // soundrive in reset; paging stays suppressed
            dac_.write_channel(0, val);
        });
    port_.register_handler(0x00FF, 0x00F3, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            if ((effective_internal_port_enable(0x84) & 0x04) == 0) return;   // SD2 b2 gate
            dac_.write_channel(1, val);
        });
    port_.register_handler(0x00FF, 0x00F9, nullptr,
        [this](uint16_t, uint8_t val) {
            // Same G146 conflict handling as the 0xF1 handler above
            // (port_f9_lsb term of zxnext.vhd:2708).
            if ((effective_internal_port_enable(0x84) & 0x04) == 0) {   // SD2 b2 gate
                port_.decline_write();   // decode absent → FD paging may fire (G146)
                return;
            }
            if (!dac_enabled_) return;   // soundrive in reset; paging stays suppressed
            dac_.write_channel(2, val);
        });
    port_.register_handler(0x00FF, 0x00FB, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            // VHDL zxnext.vhd:2664 — port_dac_D fires on 0xFB when either
            // SD2 mode (NR 0x84 bit 2) OR mono_AD_fb effective is set;
            // :2661 — port_dac_A fires on 0xFB only via mono_AD fan-out.
            // mono_AD_fb_eff = NR 0x84 bit 5 AND NOT bit 2 (vhd:2433).
            // V16-NMP-02: route through the effective gate so expbus_eff_en=1
            // ANDs in NR 0x88 per VHDL :2392-2393.
            const uint8_t nr84 = effective_internal_port_enable(0x84);
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
    //
    // V18-NMP-04 (Pass-18 verify-audit fix): VHDL `port_b3_lsb` is LSB-only
    // (A15..A8 don't-care, per the LSB decoder at zxnext.vhd:2559). Pre-fix
    // mask 0xFFFF required full 16-bit match — software writing to e.g.
    // 0x12B3 would update GS Covox B/C on real hardware but not in jnext.
    // Fix: change to mask 0x00FF.
    port_.register_handler(0x00FF, 0x00B3, nullptr,
        [this](uint16_t, uint8_t val) {
            if (!dac_enabled_) return;
            if ((effective_internal_port_enable(0x84) & 0x40) == 0) return;   // GS Covox b6
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
    // Otherwise nothing decodes the read and it returns X"FF"
    // (zxnext.vhd:1877) — see port_df_read(). (G130 closure; GH #262 —
    // the gated-off read used to return 0x00.)
    port_.register_handler(0x00FF, 0x00DF,
        [this](uint16_t) -> uint8_t { return port_df_read(); },
        [this](uint16_t, uint8_t val) {
            // VHDL zxnext.vhd:2435 — port_dac_mono_AD_df_io_en =
            // internal_port_enable(23) = NR 0x84 bit 7 (G114). When the
            // gate is clear the SpecDrum DAC port is not decoded and the
            // write is silently dropped.
            if (!dac_enabled_) return;
            if ((effective_internal_port_enable(0x84) & 0x80) == 0) return;
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
    // PASS-6 NR 0x06 reset preservation (VHDL zxnext.vhd:1109-1113 are
    // initial-value-only signals — NOT in the reset block at zxnext.vhd:4932-
    // 4933, which only re-asserts bits 7 and 5). NextReg::reset() now
    // preserves bits 6/4/3/2/1/0 of NR 0x06 across reset; derive the
    // Emulator-side state shadows from the preserved cached value rather
    // than unconditionally clearing them. On power-on (constructor's
    // value-init of regs_[]), cached(0x06) post-reset is 0xA0, so all four
    // shadows start at false — matching the pre-pass-6 power-on behaviour.
    // After any subsequent NR 0x06 write + soft reset, the user-written
    // bits 6/4/3/2/1/0 survive per VHDL.
    {
        const uint8_t cached_06 = nextreg_.cached(0x06);
        nr_06_button_m1_nmi_en_      = (cached_06 & 0x08) != 0;
        nr_06_button_drive_nmi_en_   = (cached_06 & 0x10) != 0;
        nr_06_internal_speaker_beep_ = (cached_06 & 0x40) != 0;
        nr_06_ps2_mode_              = (cached_06 & 0x04) != 0;
        // Forward the preserved NMI-button enables to NmiSource so the
        // gate at NmiSource::nmi_assert_mf / _divmmc reflects the live
        // VHDL state immediately after reset, before any subsequent
        // NR 0x06 write would otherwise re-arm them.
        nmi_source_.set_mf_enable(nr_06_button_m1_nmi_en_);
        nmi_source_.set_divmmc_enable(nr_06_button_drive_nmi_en_);
    }
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
        const bool cfg = nextreg_.nr_03_config_mode();
        if (cfg) {
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
        // V11-NMP-03 (Pass-11 fix-of-reviewer): VHDL zxnext.vhd:5167-5169
        // gates the `nr_06_ps2_mode` latch (bit 2) behind `nr_03_config_mode
        // = '1'`. When the gate is closed the underlying signal retains its
        // previous value. Pre-fix the C++ stored the raw `v` byte in
        // `regs_[0x06]`, so the cache could leak bit 2 written outside
        // config_mode. The observable consequence is the init/reset
        // fan-out at lines 3272-3276 which seeds `nr_06_ps2_mode_` from
        // `cached(0x06) & 0x04` — a hard reset after a "cache-poisoning"
        // out-of-config_mode write to bit 2 would clobber the live
        // `nr_06_ps2_mode_` shadow even though the VHDL latch was never
        // updated. Canonicalise the stored byte: bit 2 inherits the
        // pre-write cached value when config_mode is closed; bits 7:3 and
        // 1:0 are always written (those signals have no config_mode gate
        // per VHDL :5162-5166 and :5170). Mirrors the V11-NMP-02 pattern
        // applied to NR 0x0A bits 7:5.
        if (cfg) {
            return v;
        }
        const uint8_t prev = nextreg_.cached(0x06);
        return static_cast<uint8_t>((v & 0xFB) | (prev & 0x04));
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
    // PASS-7 NR 0x81 reset preservation. Same shape as NR 0x06 / NR 0x05 /
    // NR 0x09. VHDL zxnext.vhd:1221-1225 declare nr_81_expbus_ula_override,
    // nr_81_expbus_nmi_debounce_disable, nr_81_expbus_clken,
    // nr_81_expbus_fdc, nr_81_expbus_speed all as initial-value-only
    // signals.  None of them appear in any reset block — confirm by
    // scanning zxnext.vhd for `nr_81_` writes outside the NR 0x81 write
    // handler at :5491-5496.  The bits survive both hard and soft reset.
    // Pre-pass-7 jnext unconditionally re-applied 0x00 here on every reset,
    // wiping the user-set ExpBus NMI-debounce-disable bit on every NR 0x02
    // ← 0x01 soft reset.  Preserve across soft reset; on hard reset /
    // first boot restore the power-on default 0x00.  Re-propagate to
    // NmiSource so the ExpBus NMI gate reflects the live VHDL state
    // immediately after reset, before any subsequent NR 0x81 write would
    // otherwise re-arm it.
    if (!preserve_memory) {
        nr_81_ = 0;
    }
    nmi_source_.set_expbus_debounce_disable((nr_81_ & 0x20) != 0);
    nextreg_.set_write_handler(0x81, [this](uint8_t v) -> uint8_t {
        nr_81_ = v;
        nmi_source_.set_expbus_debounce_disable((v & 0x20) != 0);
        return v;
    });
    // V11-NMP-01 (Pass-11 verify-audit fix): VHDL zxnext.vhd:5496 hardwires
    // `nr_81_expbus_speed <= "00"` on every NR 0x81 write — the original
    // `nr_wr_dat(1 downto 0)` source is explicitly commented out. The read
    // mux at zxnext.vhd:6126 places `nr_81_expbus_speed` in bits 1:0, so
    // those bits ALWAYS read back as "00" regardless of what was written.
    // Pre-fix the read mask was `0x7B = 0b01111011` which preserved bits
    // 1:0 from the last write (e.g. write 0xFF would read 0xFB instead of
    // the VHDL-correct 0xF8). Mask 0x78 keeps only bits 6, 5, 4, 3 (the
    // four `nr_81_expbus_*` storage signals); bit 7 is forced to '1' as
    // before to model `i_BUS_ROMCS_n` idle (no expbus device wired).
    nextreg_.set_read_handler(0x81, [this]() -> uint8_t {
        return static_cast<uint8_t>(0x80 | (nr_81_ & 0x78));
    });

    // Register 0x80: Expansion bus configuration — VHDL zxnext.vhd:6122
    //   port_253b_dat <= nr_80_expbus.  Full 8-bit field, no masking.
    // No expansion-bus device is wired in jnext today (G45 tracks expbus
    // emulation), so the byte is purely write-storage and round-trips
    // unchanged.  G56 Phase 2 (cluster E): no handler at all — NextReg
    // falls through to the raw stored byte, which IS the VHDL formula.
    //
    // Pass-9 verify-audit fix (Task 2 verify9-nmi-mf-port): even though
    // jnext has no expbus device, NR 0x80 bit 7 (`expbus_en`, VHDL:2197)
    // and bit 4 (`expbus_disable_mem`, VHDL:2200) are part of the VHDL
    // `nmi_assert_expbus` gate at zxnext.vhd:2089. Fan them out to
    // NmiSource so a future setter call to `set_expbus_nmi_n(false)`
    // properly respects the enable/disable_mem mask. The VHDL latches
    // (`expbus_eff_en` / `expbus_eff_disable_mem`) commit on bus-idle
    // (zxnext.vhd:5800-5813); jnext collapses that to "commit
    // immediately on the NR 0x80 write" — same shape as the existing
    // `eff_nr_08_contention_disable` handling, which is also gated on
    // `hc(8)='1'` in VHDL but committed synchronously in jnext.
    nmi_source_.set_expbus_eff_en(((nextreg_.cached(0x80) >> 7) & 1) != 0);
    nmi_source_.set_expbus_eff_disable_mem(((nextreg_.cached(0x80) >> 4) & 1) != 0);
    nextreg_.set_write_handler(0x80, [this](uint8_t v) -> uint8_t {
        nmi_source_.set_expbus_eff_en((v & 0x80) != 0);
        nmi_source_.set_expbus_eff_disable_mem((v & 0x10) != 0);
        // V16-NMP-02: bit 7 (`expbus_eff_en`) is one of the inputs to the
        // VHDL `internal_port_enable` formula at zxnext.vhd:2392-2393.
        // Toggling it changes whether NR 0x86-0x89 AND in. NR 0x80's
        // post-write state lives in `nmi_source_.expbus_eff_en()` (set
        // above), so the helper sees the fresh state when consulted —
        // no override needed for NR 0x80 itself.
        propagate_effective_port_enables();
        return v;
    });

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
    //
    // PASS-7 NR 0x08 reset propagation. After init() resets each subsystem
    // (turbosound_, dac_, mixer_, ...) to their power-on defaults, fan out
    // the *preserved* `nr_08_stored_low_` shadow so the NMI/MF/Port-adjacent
    // subsystems whose state mirrors NR 0x08 b5/b3/b1 stay in sync with the
    // VHDL signals that survived reset.  Bit 4 (internal_speaker_en) is read
    // direct from `nr_08_stored_low_` by `beep_spkr_excl()` so no extra
    // propagation is needed for it.  Mirrors the analogous NR 0x06 fan-out
    // block earlier in init().  Bit 6 / bit 7 are dynamic (per-instruction
    // contention shadow / paging lock) — both correctly reset by their own
    // subsystems and not part of `nr_08_stored_low_`.
    turbosound_.set_stereo_mode((nr_08_stored_low_ >> 5) & 1);
    turbosound_.set_enabled((nr_08_stored_low_ >> 1) & 1);
    mixer_.set_exc_i(beep_spkr_excl());
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
        // Verify8-memory class-(a) fix: VHDL zxnext.vhd:5906 reads
        // `(not port_7ffd_locked)` — the EFFECTIVE lock signal from
        // VHDL :3769, NOT the raw port_7ffd_reg(5) mirror. Pentagon-1024
        // mode (NR 0x8F=11 AND EFF7(2)=0) drops port_7ffd_locked to '0'
        // even when bit 5 is set; jnext's `paging_locked()` accessor
        // exposes only the raw bit-5 mirror. Use `effective_paging_locked()`
        // which composes the full VHDL :3769 expression so bit 7 reflects
        // the same gate the SRAM arbiter / 128K bank-write path consults.
        if (!mmu_.effective_paging_locked())             v |= 0x80;
        // Verify8-memory class-(a) fix: VHDL zxnext.vhd:5906 reads
        // `eff_nr_08_contention_disable` (the EFFECTIVE/committed gate),
        // NOT the immediate shadow `nr_08_contention_disable`. The shadow
        // is updated synchronously on the NR 0x08 write, but the effective
        // gate only commits on the bus-idle / hc(8)='1' edge per
        // zxnext.vhd:5800-5823. Mid-line readbacks therefore return the
        // last committed value, not the freshly-written shadow. mmu_'s
        // `contention_disabled_` mirrors the shadow (immediate write) —
        // contention_'s `contention_disable_` is the effective field
        // (gated commit). Read the effective one to match VHDL :5906.
        if (contention_.contention_disable())            v |= 0x40;
        v |= nr_08_stored_low_ & 0x3F;
        return v;
    });

    // Register 0x09: Peripheral 4 (VHDL zxnext.vhd:5186-5189, 5909)
    //   bits 7:5 = nr_09_psg_mono (bit 7=AY#2, 6=AY#1, 5=AY#0)
    //   bit 4    = nr_09_sprite_tie (VHDL :5187)
    //   bit 3    = write-only DivMMC mapram-latch clear (VHDL :4184-4186 —
    //              `nr_09_we='1' and nr_wr_dat(3)='1'` forces
    //              port_e3_reg(6) := '0'). Read-back returns '0' for this
    //              bit (VHDL :5909). NOT sprites-over-border.
    //   bit 2    = nr_09_hdmi_audio_en (inverted on write, VHDL :5188)
    //   bits 1:0 = nr_09_scanlines (handled in $F7 increment helper)
    // PASS-5 FIX: previously this handler called sprites_.set_over_border
    // here too. That is wrong — sprite-over-border lives in NR $15 bit 1
    // (handled in the NR 0x15 write handler above). Removing the spurious
    // wiring restores VHDL faithfulness; software that writes bit 3 to
    // clear DivMMC mapram no longer collaterally toggles sprite drawing
    // over the border.
    //
    // PASS-7 NR 0x09 reset propagation. After init() resets each subsystem,
    // fan out the *preserved* `regs_[0x09]` (NextReg::reset() now keeps
    // bits 7:5 / 3 / 2 / 1:0 across reset; only bit 4 sprite_tie is forced
    // to 0 per VHDL :4937) so subsystem state stays in sync with the VHDL
    // signals that survived reset.
    //   bits 7:5 = psg_mono → turbosound_.set_mono_mode (VHDL :5186, :6398)
    //   bit  4   = sprite_tie reset to 0 → sprites_.set_mirror_tie(false)
    {
        const uint8_t cached_09 = nextreg_.cached(0x09);
        sprites_.set_mirror_tie((cached_09 & 0x10) != 0);
        uint8_t mono = 0;
        if (cached_09 & 0x20) mono |= 0x01;  // AY#0
        if (cached_09 & 0x40) mono |= 0x02;  // AY#1
        if (cached_09 & 0x80) mono |= 0x04;  // AY#2
        turbosound_.set_mono_mode(mono);
    }
    nextreg_.set_write_handler(0x09, [this](uint8_t v) -> uint8_t {
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
    //
    // Task 40 — these two are the ONLY read handlers in the machine that mutate
    // state, so they are installed as DESTRUCTIVE, each with a side-effect-free twin
    // for peek(). The debugger's NextREG panel sweeps all 256 registers several times
    // a second while the guest runs; through read() it would latch L, then latch R
    // over it, and the Z80's next NR 0x2D read would get a sample the debugger
    // invented. The value is computable without the latch, so the twin returns
    // exactly the same byte and touches nothing.
    nextreg_.set_destructive_read_handler(0x2C,
        [this]() -> uint8_t {
            // G113: read the GATED pi_audio_L (10-bit), not the raw I2s
            // latch — VHDL zxnext.vhd:6007 reads `pi_audio_L`, which already
            // applies the NR 0xA2 enable/mute/ear/cross-channel-mux gate at
            // zxnext.vhd:2358. When NR 0xA2 disables I2S, pi_audio_L = 0x200
            // (silence midpoint), so the high-8 bits = 0x80 and the latched
            // low-2 bits = 0b00.
            const uint16_t L = i2s_.pi_audio_L() & 0x3FF;
            nr_2d_i2s_sample_ = static_cast<uint8_t>((L & 0x03) << 6);  // bits [1:0] → byte [7:6]
            return static_cast<uint8_t>((L >> 2) & 0xFF);               // bits [9:2] → byte [7:0]
        },
        [this]() -> uint8_t {   // peek: same byte, no latch
            return static_cast<uint8_t>(((i2s_.pi_audio_L() & 0x3FF) >> 2) & 0xFF);
        });
    nextreg_.set_destructive_read_handler(0x2E,
        [this]() -> uint8_t {
            // G113: read the GATED pi_audio_R per VHDL zxnext.vhd:6014.
            const uint16_t R = i2s_.pi_audio_R() & 0x3FF;
            nr_2d_i2s_sample_ = static_cast<uint8_t>((R & 0x03) << 6);
            return static_cast<uint8_t>((R >> 2) & 0xFF);
        },
        [this]() -> uint8_t {   // peek: same byte, no latch
            return static_cast<uint8_t>(((i2s_.pi_audio_R() & 0x3FF) >> 2) & 0xFF);
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
    // V18-NMP-NIT-01: VHDL :2690 gates port_ctc on
    // `port_ctc_io_en = '1'`, where
    // `port_ctc_io_en <= internal_port_enable(27)` = NR 0x85 bit 3
    // (VHDL :2442). When cleared, the port is silenced.
    //
    // V21-NMP-02 (Pass-21 verify-audit fix): the CTC entity at
    // device/ctc.vhd:101-146 instantiates NUM_CTC=4 channels selected
    // by `i_port_ctc_sel = cpu_a(10 downto 8)` (zxnext.vhd:4076 — the
    // ZXN entity instance overrides NUM_CTC_LOG2 to 3). The select
    // process at ctc.vhd:128-137 matches `if I = unsigned(i_port_ctc_sel)`
    // for I in 0..3 only, so addresses with A10=1 (= 0x1C3B..0x1F3B)
    // produce sel(0..3) = "0000" — no channel selected. The output
    // mux at ctc.vhd:164-176 zeroes o_cpu_d when no sel bit is set
    // (each dout(I) AND sel(I), then OR-fold across I in 0..3); the
    // iowr fan-out at :141-146 stays 0 too, so writes are dropped.
    //
    // Pre-fix the C++ port handler used mask 0xF8FF, leaving A10 a
    // don't-care bit. `(p >> 8) & 3` then aliased 0x1C3B..0x1F3B to
    // channels 0..3 (because the high-nibble 0x1C/0x1D/0x1E/0x1F give
    // (0xC/0xD/0xE/0xF) & 0x03 = 0/1/2/3). Reads returned channel 0..3
    // data instead of VHDL's 0x00 (no decode); writes hit channels
    // instead of being silently dropped.
    //
    // Fix: tighten the mask to 0xFCFF — A10:8 now mask in too. The
    // value 0x183B already has A10:8 = "000", so the matched range is
    // 0x183B (channel 0), 0x193B (channel 1), 0x1A3B (channel 2),
    // 0x1B3B (channel 3) only. Addresses 0x1C3B..0x1F3B no longer match
    // this handler and fall through to the floating-bus default
    // (mirroring VHDL's no-decode behaviour).
    //
    // Note: VHDL port_ctc='1' for the full 0x183B..0x1F3B range (the
    // decode is wider than the actual channel count), but the CTC
    // entity zeroes its output when A10:8 ≥ 4 (= A10=1 here). The CPU
    // bus path for IN at 0x1C3B..0x1F3B with port_ctc_io_en='1' is
    // therefore:
    //   port_ctc_rd = iord AND port_ctc = '1'
    //   port_internal_rd_response = ... OR port_ctc_rd = '1'
    //   port_ctc_rd_dat = ctc_do = 0x00 (OR-fold of zeros)
    //   port_rd_dat = ... OR port_ctc_rd_dat = 0x00
    //   cpu_di = port_rd_dat = 0x00
    // i.e. VHDL drives 0x00, NOT the floating-bus 0xFF, for the A10=1
    // alias range when CTC IO-enable is on. When CTC IO-enable is off,
    // port_ctc='0' so port_internal_rd_response stays 0 and the bus
    // floats to 0xFF.
    // Class-(c) inert divergence in practice (no software writes
    // 0x1C3B-0x1F3B), but the readback / write-drop contract is part
    // of the VHDL surface.
    //
    // GH #265 — both directions at their bus timing, not at the instruction
    // start the CTC was last ticked to. A read returns port_ctc_dat, reloaded
    // from the channel's t_count on every CLK_CPU falling edge
    // (zxnext.vhd:4095-4100): the IN latches the one loaded 2.5 T-states into
    // its I/O cycle, i.e. t_count after the CLK_28 edge before that reload
    // (sync_for_port_read()). A write reaches the channel as iowr, high from
    // the CPU edge IORQ and WR assert on, and its registers take it on the
    // CLK_28 edge after (ctc_chan.vhd:246-254,262-285) — after the counter
    // has made its move for that edge with the old settings; the IM2 fabric
    // meets the old interrupt enable for every ZC/TO up to the edge before.
    port_.register_handler(0xFCFF, 0x183B,
        [this](uint16_t p) -> uint8_t {
            if ((effective_internal_port_enable(0x85) & 0x08) == 0) return 0xFF;
            sync_for_port_read();
            return ctc_.read((p >> 8) & 3);
        },
        [this](uint16_t p, uint8_t val) {
            if ((effective_internal_port_enable(0x85) & 0x08) == 0) return;  // NR 0x85 b3 gate
            sync_for_port_write();
            ctc_.write((p >> 8) & 3, val);
            // GH #47/#48 — a CTC control word carries the channel's
            // interrupt-enable bit, and hardware feeds THAT bit to the
            // IM2 fabric. VHDL device/ctc_chan.vhd:265-276:
            //
            //    if reset_hard = '1' then control_reg <= (others => '0');
            //    elsif iowr_cw = '1'  then control_reg <= i_cpu_d(7 downto 3);
            //    elsif i_int_en_wr='1' then control_reg(7-3) <= i_int_en;
            //    ...
            //    o_int_en <= control_reg(7-3);
            //
            // i.e. control_reg(7) is written by EITHER a control-word
            // port write (bit 7) OR an NR 0xC5 write, and `o_int_en`
            // is the single signal zxnext.vhd:1949 fans into
            // `im2_int_en`. jnext previously refreshed the fabric copy
            // only from the NR 0xC5 handler, so a control word that
            // set bit 7 updated CtcChannel::control_int_en_ but left
            // dev_[CTCn].int_en clear — the channel's ZC/TO raised
            // im2_int_req and the wrapper's int_en AND dropped it.
            // Both SD-card audio players write NR 0xC5 FIRST and then
            // program the channels with control words carrying bit 7,
            // so their engine interrupts were silently discarded.
            // Re-push the CTC's live enable mask after every CTC port
            // write; ctc_.get_int_enable() reports bits 3:0 with 7:4
            // zero, matching zxnext.vhd:4093 (`ctc_int_en(7 downto 4)
            // <= "0000"`).
            im2_.set_int_en_c5(ctc_.get_int_enable());
        });

    // V21R-NMP-NIT-02 (Pass-21 reviewer fix): complementary handler for
    // the CTC alias range 0x1C3B..0x1F3B (A10=1). Per VHDL bus
    // composition above, an IN with port_ctc_io_en='1' drives 0x00 on
    // cpu_di via the OR-fold of (dout(I) AND sel(I)) — not 0xFF.
    // Without this handler the reads fall through to the floating-bus
    // default 0xFF, enshrining a class-(c) divergence flagged by the
    // Pass-21 reviewer. The IO-enable gate stays identical (NR 0x85
    // bit 3); when cleared the bus floats. Writes are silently dropped
    // (VHDL iowr(I) = port_ctc_wr AND sel(I), and sel(0..3)=0 for
    // A10=1, so iowr stays 0 — no channel mutated).
    // Mask 0xFCFF with value 0x1C3B selects exactly the four addresses
    // 0x1C3B / 0x1D3B / 0x1E3B / 0x1F3B (A9:8 are masked in but the
    // four channels behave identically — all dropped on write, all
    // read as 0x00). Same specificity (10 bits set) as the channel
    // handler, but disjoint match conditions (A10=0 vs A10=1) so the
    // most-specific-wins dispatch routes correctly.
    port_.register_handler(0xFCFF, 0x1C3B,
        [this](uint16_t /*p*/) -> uint8_t {
            // CTC IO-enable gate: NR 0x85 bit 3 (= port_ctc_io_en,
            // VHDL :2442 internal_port_enable(27)). When cleared
            // port_ctc='0' → port_internal_rd_response=0 → floating
            // bus. When set, the OR-fold zeros the byte.
            if ((effective_internal_port_enable(0x85) & 0x08) == 0) return 0xFF;
            return 0x00;
        },
        [this](uint16_t /*p*/, uint8_t /*val*/) {
            // Writes are dropped per VHDL ctc.vhd:141-146 (iowr(I) = 0
            // when sel(I)=0; sel(0..3)=0 for A10=1). No-op even with
            // IO-enable on. The IO-enable check is kept for symmetry,
            // though functionally it doesn't matter here.
            if ((effective_internal_port_enable(0x85) & 0x08) == 0) return;
            // Silently dropped (no channel mutated).
        });

    // DMA — port 0x6B (ZXN mode) and port 0x0B (Z80-DMA compat).
    // VHDL zxnext.vhd: port_dma_rd/wr <= port_dma_rd_raw/wr_raw AND NOT
    // dma_holds_bus — the DMA port is gated at the dispatcher layer while
    // the DMA controller holds the bus.  Matches DMA plan row 15.8.
    // V18-NMP-NIT-01: VHDL zxnext.vhd:2643 also gates each port on the
    // matching `port_dma_*_io_en` enable:
    //   * port 0x6B → port_dma_6b_io_en = internal_port_enable(5)
    //                = NR 0x82 bit 5 (VHDL :2405).
    //   * port 0x0B → port_dma_0b_io_en = internal_port_enable(25)
    //                = NR 0x85 bit 1 (VHDL :2440).
    // When the enable bit is cleared, the port is silenced; this is
    // independent of (and ANDed alongside) the dma_holds_bus check.
    port_.register_handler(0x00FF, 0x006B,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x82) & 0x20) == 0) return 0xFF;
            return dma_.dma_holds_bus() ? 0xFF : dma_.read();
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x82) & 0x20) == 0) return;  // NR 0x82 b5 gate
            if (!dma_.dma_holds_bus()) dma_.write(val, false);
        });
    port_.register_handler(0x00FF, 0x000B,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x85) & 0x02) == 0) return 0xFF;
            return dma_.dma_holds_bus() ? 0xFF : dma_.read();
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x85) & 0x02) == 0) return;  // NR 0x85 b1 gate
            if (!dma_.dma_holds_bus()) dma_.write(val, true);
        });

    // SPI chip select (0xE7) and data (0xEB)
    // VHDL zxnext.vhd:2419: port_spi_io_en <= internal_port_enable(11) =
    // NR 0x83 bit 3. zxnext.vhd:2620-2621 gates BOTH port_e7 and port_eb on
    // port_spi_io_en. When NR 0x83 bit 3 is cleared, port reads return the
    // floating-bus value (0xFF in jnext) and writes are silently dropped —
    // mirroring the same pattern jnext applies to port 0xE3 above (DivMMC
    // bit 0) and the 0x103B/0x113B I2C / 0x133B-163B UART ports below.
    //
    // V16-DIVMMC-01 (Pass-16 verify-audit, 2026-05-10): port 0xE7 is
    // **write-only** in VHDL. Compare zxnext.vhd:614-622:
    //   signal port_e3_rd : std_logic;     -- (0xE3 readable)
    //   signal port_e3_wr : std_logic;
    //   signal port_e7_wr : std_logic;     -- only WR exists for 0xE7
    //   signal port_eb_rd : std_logic;     -- (0xEB readable)
    //   signal port_eb_wr : std_logic;
    // There is NO `port_e7_rd` signal anywhere in the core. Confirming:
    // zxnext.vhd:2803-2806 (`port_internal_rd_response` OR-tree) lists
    // `port_e3_rd`, `port_eb_rd`, `port_103b_rd`, `port_113b_rd`, ... but
    // NOT `port_e7_rd`. zxnext.vhd:2837-2840 (`port_rd_dat` OR-tree) lists
    // `port_e3_rd_dat`, `port_eb_rd_dat`, ... but NOT `port_e7_rd_dat`.
    // Pre-fix `read_cs()` returned the internal CS register state — info
    // the firmware has no architectural way to access on real hardware.
    // A read of 0xE7 on real Next falls through to `cpu_di <= X"FF"` at
    // zxnext.vhd:1877 (no internal response, expansion bus disabled) or
    // to `i_BUS_DI` at :1875 (expansion bus path). Class-(c) latent —
    // TBBlue/NextZXOS firmware never reads port 0xE7 on the boot path
    // (the CS register is purely host-side state, not firmware-visible),
    // but the divergence leaks an internal SPI-master register state to
    // any caller that reads the port, breaking the strict write-only
    // contract. Fix: pass `nullptr` for the read callback, mirroring the
    // pattern used for port 0x001F / 0x0037 (Kempston joystick — read-only,
    // unused-write nullptr) but inverted (write-only, unused-read nullptr).
    // Falls through to `default_read_` (undecoded default 0xFF in jnext).
    port_.register_handler(0x00FF, 0x00E7,
        nullptr,
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x08) == 0) return;
            spi_.write_cs(val);
        });
    port_.register_handler(0x00FF, 0x00EB,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x08) == 0) return 0xFF;
            if (extended_nex_host_.stream_active()) {
                const auto byte = extended_nex_host_.read_stream_byte();
                return byte.value_or(0xFF);
            }
            return spi_.read_data();
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x08) == 0) return;
            spi_.write_data(val);
        });

    // I2C SCL (0x103B) and SDA (0x113B)
    // VHDL zxnext.vhd:2418: port_i2c_io_en <= internal_port_enable(10).
    // internal_port_enable(10) = nr_83_internal_port_enable(2) per the
    // concat at zxnext.vhd:2392 (bits 0-7 = NR 0x82, bits 8-15 = NR 0x83).
    // Gate mirrors the DivMMC pattern below: when closed, reads return 0xFF
    // and writes are silently dropped.
    port_.register_handler(0xFFFF, 0x103B,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x04) == 0) return 0xFF;
            return i2c_.read_scl();
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x04) == 0) return;
            i2c_.write_scl(val);
        });
    port_.register_handler(0xFFFF, 0x113B,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x04) == 0) return 0xFF;
            return i2c_.read_sda();
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x04) == 0) return;
            i2c_.write_sda(val);
        });

    // UART: Tx (0x133B), Rx (0x143B), Select (0x153B), Frame (0x163B)
    // VHDL zxnext.vhd:2420: port_uart_io_en <= internal_port_enable(12).
    // internal_port_enable(12) = nr_83_internal_port_enable(4) per the
    // concat at zxnext.vhd:2392. Same gate pattern as I2C/DivMMC.
    port_.register_handler(0xFFFF, 0x133B,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x10) == 0) return 0xFF;
            sync_for_port_read();    // GH #265 — port_uart_dat, zxnext.vhd:3418-3423
            return uart_.read(3);
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x10) == 0) return;
            sync_for_port_write();   // GH #265
            uart_.write(3, val);
        });
    port_.register_handler(0xFFFF, 0x143B,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x10) == 0) return 0xFF;
            sync_for_port_read();    // GH #265 — port_uart_dat, zxnext.vhd:3418-3423
            return uart_.read(0);
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x10) == 0) return;
            sync_for_port_write();   // GH #265
            uart_.write(0, val);
        });
    port_.register_handler(0xFFFF, 0x153B,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x10) == 0) return 0xFF;
            sync_for_port_read();    // GH #265 — port_uart_dat, zxnext.vhd:3418-3423
            return uart_.read(1);
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x10) == 0) return;
            sync_for_port_write();   // GH #265
            uart_.write(1, val);
        });
    port_.register_handler(0xFFFF, 0x163B,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x10) == 0) return 0xFF;
            sync_for_port_read();    // GH #265 — port_uart_dat, zxnext.vhd:3418-3423
            return uart_.read(2);
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x10) == 0) return;
            sync_for_port_write();   // GH #265
            uart_.write(2, val);
        });

    // DivMMC control — port 0xE3.
    // VHDL zxnext.vhd:2412: port_divmmc_io_en <= internal_port_enable(8) = NR 0x83 bit 0.
    port_.register_handler(0x00FF, 0x00E3,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x01) == 0) return 0xFF;
            return divmmc_.read_control();
        },
        [this](uint16_t, uint8_t val) {
            if ((effective_internal_port_enable(0x83) & 0x01) == 0) return;
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
            if ((effective_internal_port_enable(0x82) & 0x40) == 0) return 0xFF;
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
            if ((effective_internal_port_enable(0x82) & 0x80) == 0) return 0xFF;
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
    // V18-NMP-01 (Pass-18 verify-audit fix): VHDL zxnext.vhd:2668-2670 decode
    // the Kempston mouse ports using ONLY 12 address-line bits — A11..A8 must
    // be 0xA / 0xB / 0xF (port_fadf / port_fbdf / port_ffdf respectively) AND
    // A7..A0 must equal 0xDF (port_df_lsb). A15..A12 are DON'T-CARE. Pre-fix
    // the handlers used mask 0xFFFF / val 0xFADF (etc.), requiring the FULL
    // 16-bit address to match exactly — software using e.g. OUT (0x2ADF), A
    // followed by IN A,(0x2ADF) would access Kempston mouse buttons on real
    // hardware but not in jnext. Fix: change masks to 0x0FFF / val 0x0ADF
    // (etc.) so the high nibble of cpu_a is ignored, matching VHDL :2668.
    //
    // GH #262: with the mouse disabled these addresses are still LSB 0xDF,
    // so the `port_1f` alias (zxnext.vhd:2674) can decode them exactly as it
    // decodes 0x??DF; the gated-off read therefore goes to port_df_read()
    // rather than straight to X"FF".
    port_.register_handler(0x0FFF, 0x0ADF,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x20) == 0) return port_df_read();
            return mouse_.read_port_fadf();
        },
        nullptr);
    port_.register_handler(0x0FFF, 0x0BDF,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x20) == 0) return port_df_read();
            return mouse_.read_port_fbdf();
        },
        nullptr);
    port_.register_handler(0x0FFF, 0x0FDF,
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x83) & 0x20) == 0) return port_df_read();
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
    // enable latch is gated on them. The low 6 bits (`ulap_index`) select
    // the ULA+ palette entry that NR 0xFF writes and palette-mode 0xFF3B
    // reads address (zxnext.vhd:6958).
    //
    // Wave C — 2026-04-23 — now gates 0xFF3B write on ulap_mode = "01" per
    // zxnext.vhd:4548: `port_ff3b_ulap_en <= cpu_do(0)` ONLY when
    // port_bf3b_ulap_mode = "01"; mode "00" is a palette write carried as
    // an NR 0xFF write (GH #258, below), and modes "10" / "11" do nothing.
    // The Phase-1 scaffold previously wrote the enable unconditionally,
    // which the Phase 1 critic flagged as VHDL-faithless.
    //
    // 0xFF3B read — VHDL zxnext.vhd:4556-4569.  `port_ff3b_rd` is one of
    // the OR-terms of `port_internal_rd_response` (zxnext.vhd:2806), so the
    // machine drives this port; it is NOT floating bus.  Two branches:
    //
    //   port_bf3b_ulap_mode /= "00"  -> "0000000" & port_ff3b_ulap_en
    //                                   (zxnext.vhd:4566) — the enable bit
    //                                   in bit 0, all other bits zero.
    //   port_bf3b_ulap_mode  = "00"  -> palette read (zxnext.vhd:4563):
    //       port_ff3b_dat <= dat(5:3) & dat(8:6) & dat(2:1)
    //     where `dat` is the 9-bit RGB333 palette word (8:6 = R, 5:3 = G,
    //     2:0 = B), so the byte is GGGRRRBB — the ULA+ colour format, with
    //     B0 dropped.  The word is read from `palette_utm` at address
    //     '0' & nr_43_palette_write_select(2) & "11" & port_bf3b_ulap_index
    //     (zxnext.vhd:6958) = ULA palette bank NR 0x43 b6, index
    //     PaletteManager::ULAP_BASE + bf3b_index.
    //
    // Modelling note: `port_ff3b_dat` is a register reloaded on the falling
    // 28 MHz edge, and in mode "00" only when `ulap_palette_rd_dly` is set
    // (zxnext.vhd:4562, 4571-4577) — one 28 MHz cycle after the read strobe.
    // A Z80 IN holds `port_ff3b_rd` asserted for the whole IORQ (>= 3 T at
    // 3.5 MHz = 24+ cycles of the 28 MHz clock), so the latch is reloaded
    // long before the CPU samples the bus and the CURRENT entry is what is
    // returned.  jnext therefore reads the palette directly, with no stale
    // -by-one modelling.  The `ulap_wait_n` handshake (zxnext.vhd:4583)
    // that stretches the read on hardware has no effect on the value.
    // V18-NMP-NIT-01: VHDL zxnext.vhd:2685-2686 gates port_bf3b /
    // port_ff3b on `port_ulap_io_en = '1'`, where
    // `port_ulap_io_en <= internal_port_enable(24)` = NR 0x85 bit 0
    // (VHDL :2439). When cleared, the ports are silenced.
    //
    // GH #262: 0xBF3B is WRITE-ONLY. The VHDL has `port_bf3b_wr <= iowr and
    // port_bf3b` (zxnext.vhd:2792) and no read strobe at all, and 0xBF3B is
    // absent from `port_internal_rd_response` (:2803-2806), so an IN from it
    // falls to `cpu_di <= X"FF"` (:1877) whether or not the port is enabled.
    // No read callback: dispatch reaches the undecoded default. The handler
    // used to return 0x00 whenever NR 0x85 b0 was set.
    port_.register_handler(0xFFFF, 0xBF3B,
        nullptr,
        [this](uint16_t, uint8_t v) {
            if ((effective_internal_port_enable(0x85) & 0x01) == 0) return;  // NR 0x85 b0 gate
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
        [this](uint16_t) -> uint8_t {
            if ((effective_internal_port_enable(0x85) & 0x01) == 0) return 0xFF;
            if (renderer_.ula().get_ulap_mode() != 0x00) {
                // zxnext.vhd:4566 — "0000000" & port_ff3b_ulap_en.
                return renderer_.ula().get_ulap_en() ? 0x01 : 0x00;
            }
            // zxnext.vhd:4563 + :6958 — palette read, GGGRRRBB.
            // Bank is nr_43_palette_write_select(2) = NR 0x43 b6, the same
            // bit the NR 0xFF poke writes through (NOT the b1 render-time
            // select), because :6958 drives the address for `ulap_palette_rd`
            // and `nr_ff_we` identically.
            const bool bank_second = (palette_.read_control() & 0x40) != 0;
            const uint16_t rgb333  =
                palette_.ulap_rgb333(bank_second, renderer_.ula().get_ulap_index());
            const uint8_t g3 = static_cast<uint8_t>((rgb333 >> 3) & 0x07);
            const uint8_t r3 = static_cast<uint8_t>((rgb333 >> 6) & 0x07);
            const uint8_t b2 = static_cast<uint8_t>((rgb333 >> 1) & 0x03);
            return static_cast<uint8_t>((g3 << 5) | (r3 << 2) | b2);
        },
        [this](uint16_t, uint8_t v) {
            if ((effective_internal_port_enable(0x85) & 0x01) == 0) return;  // NR 0x85 b0 gate
            // Gate on ulap_mode = "01" per VHDL zxnext.vhd:4548.
            if (renderer_.ula().get_ulap_mode() == 0x01) {
                renderer_.ula().set_ulap_en((v & 0x01) != 0);
            }
            // GH #258 — mode "00" is a palette write, and the VHDL carries it
            // on the NextREG write bus: the third CPU requester
            // (zxnext.vhd:4741,4743) writes register X"FF" (:4744) with the
            // ULA+ GGGRRRBB byte reordered to RRRGGGBB (:4745:
            // cpu_do(4:2) & cpu_do(7:5) & cpu_do(1:0)). The NR 0xFF handler
            // then stores it at the latched 0xBF3B index. It supplies its own
            // register number, so the 0x243B select latch is left alone, and
            // it is deferred exactly like the other two CPU requesters (G65).
            if (renderer_.ula().get_ulap_mode() == 0x00) {
                const uint8_t rrrgggbb = static_cast<uint8_t>(
                    ((v & 0x1C) << 3) | ((v & 0xE0) >> 3) | (v & 0x03));
                if (defer_cpu_nr_writes_) {
                    enqueue_cpu_nr_write(0xFF, rrrgggbb);
                } else {
                    nextreg_.write(0xFF, rrrgggbb);
                }
            }
        });

    // --- Tape SAVE (G33 Phase 1) ---
    if (!cfg.tape_save_file.empty()) {
        if (tap_saver_.set_output(cfg.tape_save_file)) {
            Log::emulator()->info("Tape SAVE: appending SAVEd blocks to '{}'",
                                  cfg.tape_save_file);
        } else {
            Log::emulator()->error("Tape SAVE: cannot open '{}' for append — SAVE trap disabled",
                                   cfg.tape_save_file);
        }
    }

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
    // GH #106 — 28 MHz SRAM read wait during DMA cycles. Same gate +
    // qualifier as the CPU path (GH #92, z80_cpu.cpp
    // sram_wait28_read_tick): VHDL zxnext.vhd:3175 requires
    // cpu_speed="11" (ContentionModel's committed NR 0x07), and during
    // dma_holds_bus the DMA drives the very bus signals the wait
    // expression reads (zxnext.vhd:1828-1835); per-target qualification
    // (bank-7 BRAM / bootrom / inactive pages exempt) is
    // Mmu::sram_read_wait28().
    dma_.read_mem_wait_tstates = [this](uint16_t addr) -> uint8_t {
        return (contention_.cpu_speed() == 3) ? mmu_.sram_read_wait28(addr)
                                              : 0;
    };

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
        //
        // GH #265 — stamped with the edge the ZC/TO happened on: o_zc_to
        // (zc_to_d, ctc_chan.vhd:173-182) is high for the cycle that edge
        // starts, which is when i_int_req is (zxnext.vhd:1941).
        if (channel >= 0 && channel < 4) {
            im2_.raise_req(static_cast<Im2Controller::DevIdx>(
                static_cast<int>(Im2Controller::DevIdx::CTC0) + channel),
                ctc_.time());
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
    // below.
    //
    // V18R-CPU-02 (Pass-18 reviewer): the on_interrupt hook is a no-op.
    // Previously this called `im2_.raise(Im2Level::DMA)` as a "vestigial
    // save/load-schema artifact", but that was buggy: raise() indexes
    // dev_[] directly by the Im2Level value (im2.cpp:135). Im2Level::DMA=10
    // collides with DevIdx::CTC7=10, so the call set dev_[10].int_req=true
    // which is CTC7's int_req in the DevIdx mapping. Phase-1 of
    // step_devices() then promoted CTC7's int_status — observable as bit 7
    // of NR 0xC9 returning 1 after every DMA completion even though the
    // VHDL hardwires CTC7's i_int_req to '0' (zxnext.vhd:4092). Worst case,
    // if software enabled CTC7 via NR 0xC5 bit 7 the spurious int_status
    // would chain into S_REQ → pulse_int_n_/im2_int_req → a phantom CTC7
    // interrupt dispatch. Net VHDL truth: CTC7 cannot light up from any
    // peripheral activity. Fix: drop the raise() call entirely.
    dma_.on_interrupt = [this]() {
        // No-op (V18R-CPU-02). DMA's INT path runs through the dedicated
        // im2_dma_delay latch (vhdl:2003-2010) which Emulator drives via
        // dma_int_pending() / dma_delay() in run_frame(); the daisy-chain
        // dev_[] slots have no place for DMA.
        (void)this;
    };

    // GH #265 — both UART requests are stamped with the edge the channel
    // reports the event on (UartChannel::event_edge()).
    uart_.on_tx_interrupt = [this](int channel) {
        im2_.raise_req(channel == 0 ? Im2Controller::DevIdx::UART0_TX
                                    : Im2Controller::DevIdx::UART1_TX,
                       uart_.channel(channel).event_edge());
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
                                    : Im2Controller::DevIdx::UART1_RX,
                       uart_.channel(channel).event_edge());
    };

    // --- Phase 5 DivMMC overlay + I2C RTC + SD card ---

    mmu_.set_divmmc(&divmmc_);
    // Wave 1 E — wire MF memory overlay into the Mmu cascade. Per VHDL
    // zxnext.vhd:2937, MF sits between bootrom and DivMMC. Mmu::read/write
    // gate on multiface_->is_mem_active() (= mf_enable_eff) and the
    // cpu_a(15:14)='00' window to honour the SRAM arbiter at :3028-3035.
    mmu_.set_multiface(&multiface_);
    mmu_.set_debug_state(&debug_state_);
    // GH #222 — the same DebugState drives I/O watchpoints on the port bus.
    port_.set_debug_state(&debug_state_);
    i2c_.attach_device(0x68, &rtc_);
    // Task 28 — pin the RTC to the --rtc fixed date/time (frozen clock,
    // deterministic boot screenshots). Survives the NextZXOS mid-boot
    // soft reset: I2cRtc::reset() preserves the fixed-time setting.
    if (config_.rtc_fixed) {
        rtc_.set_fixed_time(config_.rtc_fixed_tm);
    }
    // NextZXOS-boot fix (2026-07-09, ZXGO-COMPARISON doc): attach the
    // single SD card to socket 0 ONLY. The Next board has TWO SD sockets
    // (SD0 internal, SD1 external); VHDL zxnext.vhd:3280's shared-MISO mux
    // only describes the read-side wiring (one select active at a time),
    // not "one card answers both selects". NR 0x0A bit 5 (sd_swap) is
    // already applied at CS decode (SpiMaster::write_cs), so it selects
    // WHICH SOCKET the boot select reaches — with one card in socket 0,
    // sd_swap=1 correctly finds an empty socket. The prior Pass-9
    // dual-attach made the same initialized card answer NextZXOS's
    // slot-1 probe as a phantom second card (the zx_go reference
    // emulator documents this exact bug causing an infinite mount loop).
    spi_.attach_device(0, &sd_card_);  // SD card in socket 0; socket 1 empty

    // GH #251 — teach the UART how to ask whether the joystick-connector serial
    // mux has taken a channel over (zxnext.vhd:3340-3346, :3537). Installed
    // here rather than pushed from the tick loop because both consumers are at
    // byte boundaries, so the pull is free and never one instruction stale —
    // see UartChannel::set_device_isolated_probe. The lambda captures `this`,
    // and `uart_` is a member, so it cannot outlive the Emulator; a cold boot
    // reconstructs both and init() re-installs it.
    uart_.set_joy_uart_channel_probe([this]() -> int {
        if (!iomode_.joy_uart_en()) return -1;      // VHDL:3537 — bit 7 AND bit 5
        return iomode_.iomode_0() ? 1 : 0;          // VHDL:3340-3341 — bit 0
    });

    // GH #25 — the emulated ESP-01 on UART 0, if the user enabled it. Off by
    // default; built once and preserved across a soft reset (setup_esp()).
    setup_esp();

    // GH #251 — the serial cable in a joystick socket, if the user attached one.
    setup_joy_uart_source();

    // GH #252 — teach the UART where a transmitted byte GOES while the joystick
    // connector owns the channel (zxnext.vhd:3526-3531): out on pin 7, to the
    // host endpoint. Installed unconditionally alongside the probe above so the
    // two halves of the mux are wired in one place; it does nothing until
    // setup_joy_uart_link() below actually attaches a cable.
    uart_.set_joy_uart_tx_sink([this](uint8_t byte) {
        if (joy_uart_link_) joy_uart_link_->send_to_host(byte);
    });

    // GH #252 — the LIVE bidirectional cable, if the user attached one.
    setup_joy_uart_link();

    // Pass-8 verify-audit (2026-05-09): seed the VHDL zxnext.vhd:3319
    // composite Flash-CS gate from the post-power-on / post-init state.
    // Power-on defaults: nr_03_config_mode='1' (zxnext.vhd:1102) AND
    // reset_type='100' (zxnext.vhd:1306) — both factors true, so the
    // Flash-CS decode is enabled at boot. Subsequent NR 0x02 / NR 0x03
    // writes re-fan-out via the handlers above.
    spi_.set_flash_cs_enable(
        nextreg_.nr_03_config_mode()
        || ((nmi_source_.reset_type() & 0x04) != 0));

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
            // Task 20: when --load short-circuits the firmware boot path
            // (no tbblue.fw `load_roms()` to populate SRAM pages 2..7 with
            // the rest of the ROM image), the seed loop above only fills
            // pages 0,1 from rom_ bank 0 (= 48K BASIC, the standard
            // fallback). Any 128K-class game that selects "ROM 1" via
            // `out (0x7FFD), <bit4=1>` would then read from SRAM pages 2,3
            // — which are still 0xFF — instead of the 48K BASIC ROM. The
            // VHDL Next/128K decode at zxnext.vhd:2981-3008 + :3003
            // (`sram_rom <= '0' & port_1ffd_rom(0)`) makes ROM 1 the only
            // selectable BASIC alternative in Next mode, so 128K-class
            // games (Cesare, RAGE1-engine games, anything calling ROM
            // routines or relying on the char set at $3D00 via SP1) get
            // garbage 0xFF bytes back. Symptom: cells painted with SPACE
            // (default SP1 tile) come out as 0xFF (cyan stripe + room
            // interior corruption visible in Cesare).
            //
            // Mirror the 48K BASIC content (pages 0,1) into every
            // following ROM-bank pair (pages 2,3 / 4,5 / 6,7) so the 1-bit
            // sram_rom decode always lands on the 48K BASIC ROM regardless
            // of port_7ffd bit 4 / port_1ffd. This is firmware-faithful in
            // the sense that real tbblue.fw `load_roms()` would have
            // populated all four banks (from a multi-bank ROM image) by
            // the time the Z80 reaches a Spectrum-compatible BASIC; the
            // mirror is the minimum-cost way to keep direct --load fast
            // paths working without booting through the FPGA boot ROM.
            // Skipped when no --load was requested (the boot ROM overlay
            // engages instead).
            if (!cfg.load_file.empty()) {
                const uint8_t* page0 = ram_.page_ptr(0);
                const uint8_t* page1 = ram_.page_ptr(1);
                for (int rom_bank = 1; rom_bank < 4; ++rom_bank) {
                    uint8_t* dst0 = ram_.page_ptr(static_cast<uint16_t>(rom_bank * 2));
                    uint8_t* dst1 = ram_.page_ptr(static_cast<uint16_t>(rom_bank * 2 + 1));
                    if (page0 && dst0) std::memcpy(dst0, page0, 0x2000);
                    if (page1 && dst1) std::memcpy(dst1, page1, 0x2000);
                }
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
    } else {
        // Review follow-up (2026-07-10): rom_in_sram_ was never cleared
        // when re-init()ing the SAME Emulator with a non-Next machine
        // type (the GUI's live machine-type switch). The stale true made
        // every to_sram_page() translation — and the bank-7 BRAM gate —
        // keep behaving as Next mode on a standalone 48K/128K/+3,
        // splitting bank-7 writes away from where the (correctly
        // re-wired) ULA/tilemap read. Pre-existing bug; surfaced by the
        // bank-7 gate now depending on this flag.
        mmu_.set_rom_in_sram(false);
    }

    // Activate Next config-mode SRAM routing only when we're booting through
    // the FPGA boot ROM. VHDL zxnext.vhd:1102 defaults config_mode='1' at
    // power-on; in our emulator that state is only reachable for the boot-ROM
    // boot path (where the boot ROM overlay masks the routing until
    // nextboot.rom / tbblue.fw run). Direct --load NEX/TAP/TZX starts have no
    // firmware to drive NR 0x03, so keeping config_mode=0 here lets ROM reads
    // fall through to the normal slot path. NR 0x03 writes keep mirroring
    // config_mode_ into the Mmu live thereafter.
    // config_mode_ genuinely needs this push: the two mirrors have DIFFERENT
    // power-on defaults (NextReg true per VHDL :1102, Mmu false per mmu.h) and
    // Mmu::reset() does not touch it, so without this they start disagreeing.
    //
    // GH #195: the sibling `mmu_.set_nr_04_romram_bank(nextreg_...)` that used
    // to sit here was DELETED — it was an identity assignment on every
    // reachable path, i.e. dead state dressed as live (same shape as the NR
    // 0x8C fold in #191). Unlike config_mode the two nr_04 mirrors share the
    // same power-on default (0x00), both PRESERVE across reset (#194), and
    // their only mutators are the paired writes in the NR 0x04 handler above
    // and the paired save/load slots — so they cannot diverge by the time
    // init() runs. That is measured, not argued: a temporary probe here
    // reported zero divergences across the whole 6560-row unit suite and a
    // full firmware boot + F4 soft reset (tbblue.fw's load_roms() drives NR
    // 0x04 hard, then soft_reset() re-enters init() with a non-zero bank).
    // Rows RSTD-04-03/04 pin the invariant the deleted line pretended to
    // enforce, and RSTD-04-05 covers THIS gated block specifically: the #195
    // review showed a fresh `set_nr_04_romram_bank(0)` planted right here
    // still escaped every row, because no unit fixture had boot_rom_enabled()
    // true, so nothing entered the branch at all. RSTD-04-05 enters it and
    // asserts both mirrors it establishes.
    if (cfg.type == MachineType::ZXN_ISSUE2 && mmu_.boot_rom_enabled()) {
        mmu_.set_config_mode(nextreg_.nr_03_config_mode());
    } else if (!mmu_.boot_rom_enabled() && !preserve_memory) {
        // FIRMWARE-LESS COLD BOOT — do the IPL's own NR 0x03 commit (GH #226).
        //
        // `nr_03_config_mode` is '1' only as an FPGA-configuration-time signal
        // initialiser (VHDL zxnext.vhd:1102). It has no reset clause, and its
        // ONLY mutator is the NR 0x03 write handler at :5147-5151 — which on
        // real hardware the IPL reaches within microseconds of power-on. There
        // is no way for silicon to sit in config_mode with no firmware running.
        //
        // jnext can. The boot-ROM overlay above is gated on ZXN_ISSUE2 + an SD
        // image + an EMPTY --load, so every `--load` session and every
        // `--machine 48k/128k/plus3` session skips tbblue.fw entirely; nothing
        // writes NR 0x03, and the power-on '1' stood for the whole session.
        // Three visible consequences, all from that one bit: host F4 was
        // swallowed by the VHDL:6370 gate (`nr_02_soft_reset <=
        // hotkey_soft_reset AND NOT nr_03_config_mode`), F9/F10 were dead
        // because the same bit force-clears every NMI latch each tick
        // (NmiSource::set_config_mode), and every config-mode-only register
        // (NR 0x0A bits 7:5, 0x10, 0x11, 0x06 bit 2) stayed writable for the
        // whole run.
        //
        // Commit exactly what the skipped firmware would have: one NR 0x03
        // write of this machine's type field. `nr_03_machine_type()` is the
        // typ_sel just installed for this same cold boot at the top of init()
        // — read back rather than recomputed so the two cannot drift — and it
        // is the same bits[2:0] the VHDL state machine at :5147-5151 consumes
        // (any value except "111"/"000" clears the mode).
        //
        // Gated on !preserve_memory: a SOFT reset must keep PRESERVING
        // config_mode. VHDL has no reset clause for it (see the G62 note in
        // NextReg::reset()), so re-committing here would clobber a firmware
        // that had deliberately re-entered config mode.
        nextreg_.apply_nr_03_config_mode_transition(nextreg_.nr_03_machine_type());
    }

    // Sync ROM3-selected into DivMmc (Task 7 Branch B). On hard reset the
    // MMU resets port_7ffd/port_1ffd to 0, so rom3 is false. On soft reset
    // the port state is also zeroed by mmu_.reset() — subsequent port
    // writes will re-push. Kept here so DivMmc::rom3_active_ starts in sync.
    //
    // Verify7-memory class-(a) fix: use `sram_rom3()` per VHDL
    // zxnext.vhd:2981-3008 / :3138 (the actual signal that feeds
    // `sram_divmmc_automap_rom3_en`), NOT the simpler `rom3_selected()`
    // observer that ignores machine-type and altrom-lock state.
    // 48K mode hardwires sram_rom3='1' at boot — must be reflected.
    divmmc_.set_rom3_active(mmu_.sram_rom3());

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
    //
    // G46(b) 2026-05-09 — also skipped on soft reset. DivMMC + Multiface
    // ROMs live in flash on real hardware; FPGA bitstream init populates
    // them once at power-on, and they survive RESET_SOFT. DivMmc::reset()
    // and Multiface::reset() preserve the rom_ buffer, so re-extracting
    // is harmless but redundant — and the SD-image FAT32 walk costs
    // 50-100 ms per soft reset, slowing the supervisor handoff path.
    if (!preserve_memory && !cfg.sd_card_image.empty()) {
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
            Log::emulator()->warn("Multiface ROM not found on SD image '{}' (path '{}') — Multiface ROM unavailable",
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
                Log::emulator()->warn("AltROM not found on SD image '{}' (path '{}') — AltROM unavailable (NextZXOS will likely crash on first AltROM trampoline)",
                                      cfg.sd_card_image, alt_sd_path);
            }
        }
    }

    // SD card image mounting.
    //
    // G46(b) 2026-05-09 — skipped on soft reset. Re-mounting calls
    // SdCardDevice::mount() which clears `initialized_=false` (via the
    // ctor-style reset of state at line 46-48). The supervisor's
    // deliberate NR 0x02 ← 0x01 after "Loading ROM: enNextZX.rom...OK!"
    // would then strand the next CMD17/CMD18 at "card not initialized"
    // R1=$01, sending the supervisor into a 102-cycle CMD0/8/55/41/58/9
    // re-init storm that never recovers. Per VHDL the SD card is an
    // external SPI peripheral, not in the reset domain — its mounted
    // file and initialised state survive soft reset.
    if (!preserve_memory && !cfg.sd_card_image.empty()) {
        if (sd_card_.mount(cfg.sd_card_image, cfg.sd_card_readonly)) {
            Log::emulator()->info("SD card image mounted: '{}'", cfg.sd_card_image);
        } else {
            Log::emulator()->warn("could not mount SD card image: '{}'", cfg.sd_card_image);
        }
    }

    // Pass-6 verify-audit fix (Task 2 verify6-divmmc-sd-spi, 2026-05-09):
    // sync NR 0x83 bits 0:1 into DivMmc / Multiface after the NextReg reset
    // and handler-registration sequence. NextReg::reset() reloads regs_[0x83]
    // to 0xFF when reset_type_1=true (NR 0x85 bit 7 = 1, the FPGA power-on
    // default per VHDL zxnext.vhd:1230) or preserves the prior value when
    // reset_type_1=false (zxnext.vhd:5052-5057). Either way, the registered
    // NR 0x83 write_handler at line 2099 is NOT fired by the reset path —
    // so DivMmc::port_io_enable_ and Multiface::enabled_ keep their stale
    // pre-reset values until the next explicit `nextreg_.write(0x83, ...)`.
    //
    // VHDL zxnext.vhd:2412 defines `port_divmmc_io_en <= internal_port_enable(8)`
    // (= NR 0x83 bit 0) combinationally, and divmmc.vhd:107-114 latches
    // `button_nmi`, automap_hold/held against `i_automap_reset` derived from
    // `port_divmmc_io_en` (zxnext.vhd:4112). Without the post-reset sync, a
    // soft reset that follows a firmware sequence like "NR 0x83 ← 0xFE"
    // would leave jnext's DivMMC disabled where the VHDL would re-enable it
    // (reset_type_1=true reloads 0xFF). Symmetric for Multiface bit 1.
    //
    // Soft-reset path matters most: the explicit `divmmc_.set_enabled(true)`
    // call inside the SD-ROM-extract block above is gated on `!preserve_memory`
    // (= hard reset only), so it doesn't run on soft reset. Hard reset still
    // works correctly here since NextReg::reset() with reset_type_1=true
    // reloads regs_[0x83]=0xFF and our sync sees bit 0/1 = 1/1 → both gates
    // open, matching the explicit set_enabled(true) above.
    // V16-NMP-02: route NR 0x83 b0/b1 (port_divmmc_io_en /
    // port_multiface_io_en, VHDL :2412 + :2415) through
    // effective_internal_port_enable so expbus_eff_en=1 ANDs in NR 0x87
    // (VHDL :2392-2393).
    //
    // The same holds for ContentionModel's port_7ffd_io_en (NR 0x82 b1,
    // VHDL :2399 — port_contend term :2594/:4496) and port_ulap_io_en
    // (NR 0x85 b0, :2439 — :2685-2686), which propagate_effective_port_enables()
    // pushes along with the two above. They must be seeded HERE, after
    // expbus_eff_en has been re-loaded from the (reset-folded) NR 0x80 bit 7
    // — the VHDL reloads it inside the reset clause (:5799-5806) — and not
    // from the raw NR 0x82 / 0x85 bytes: a reset that folds NR 0x80 bit 3 into
    // bit 7 brings expbus up live with an NR 0x86-0x89 mask that survived
    // (NR 0x89 b7=1, :5061-5067), so the effective enable can be 0 where the
    // raw byte is all ones. soft_reset() also writes NR 0x86-0x89 back
    // through their handlers after init(), which re-propagates; the
    // snapshot/NEX loaders' in-place init() writes nothing back (GH #239,
    // V16-NMP-02-LOAD-REINIT).
    propagate_effective_port_enables();

    // Wire palette manager and RAM into ULA for enhanced palette and
    // hardware-accurate VRAM access (ULA reads directly from physical bank 5,
    // bypassing the MMU, matching the VHDL dual-port RAM architecture).
    renderer_.ula().set_palette(&palette_);
    renderer_.ula().set_ram(&ram_);
    // Bank-7 lower-half dual-port BRAM (VHDL bank7_ram dpram2,
    // zxnext.vhd:6670): ULA shadow screen + tilemap bank-7 fetches read
    // the same dedicated buffer the MMU serves for page 0x0E — Next
    // machine only, mirroring the rebuild_ptr rom_in_sram_ gate.
    // Standalone 48K/128K/+3 personalities keep bank 7 in flat RAM, so
    // the pointer is cleared there (init is re-entrant across machine
    // switches) and the ULA/tilemap page-14 fallback stays consistent
    // with where the MMU routes the writes.
    const bool next_machine = (cfg.type == MachineType::ZXN_ISSUE2);
    renderer_.ula().set_bank7_bram(next_machine ? mmu_.bank7_bram()
                                                : nullptr);
    tilemap_.set_bank7_bram(next_machine ? mmu_.bank7_bram() : nullptr);
    // Bank-5 dedicated 16K dual-port VRAM (VHDL bank5_ram dpram2,
    // zxnext.vhd:6558-6578): the ULA standard screen and the tilemap
    // bank-5 fetches read the same buffer the MMU serves for pages
    // 0x0A/0x0B — Next machine only, same gate rationale as bank 7
    // above. Task 25 (2026-07-10): pre-fix, tbblue.fw's config-window
    // NR $04=$05 load of enNextMf.rom painted the visible screen with
    // garbage (Task 23 mid-boot glitch).
    renderer_.ula().set_bank5_vram(next_machine ? mmu_.bank5_vram()
                                                : nullptr);
    // LoRes reads the SAME dedicated bank-5 BRAM through port A
    // (zxnext.vhd:6631, lores.vhd:56) — never bank 7, never via the MMU.
    renderer_.lores().set_bank5_vram(next_machine ? mmu_.bank5_vram() : nullptr);
    tilemap_.set_bank5_vram(next_machine ? mmu_.bank5_vram() : nullptr);

    // The border resets to black with port_fe_reg (zxnext.vhd:3587-3593),
    // in Ula::reset() above. It used to be forced to white here, a phase-1
    // placeholder with no hardware basis: the frames before a ROM or OS
    // first writes port 0xFE showed a white border the machine never has.

    // Compositor per-pixel trace (debug; configured via
    // --compositor-trace / --compositor-trace-frame). Empty path disables.
    renderer_.set_compositor_trace(cfg.compositor_trace_path,
                                   cfg.compositor_trace_frame);

    // Initialise rewind buffer.
    // Measure snapshot size by doing a dry-run in measure mode (buf=nullptr).
    //
    // Hard reset only. The rewind history, its size (the debugger resizes it
    // live) and on/off state are the host's; a soft reset (NR 0x02 bit 0,
    // F4) is an event IN that history, not the start of a new one. Rebuilding
    // it here discarded every snapshot at each soft reset (a NextZXOS boot
    // performs one) and put back the command-line size (GH #263 audit).
    if (preserve_memory) {
        // keep rewind_buffer_ / rewind_enabled_ as they are
    } else if (cfg.rewind_buffer_frames > 0) {
        StateWriter measure;
        save_state(measure);
        size_t snap_bytes = measure.position();
        rewind_buffer_ = std::make_unique<RewindBuffer>(
            static_cast<size_t>(cfg.rewind_buffer_frames), snap_bytes);
        rewind_enabled_ = true;
        // Rewind implies the instruction trace: step_back() needs it to
        // locate the target instruction's cycle within a frame (Task 27 A2).
        trace_log_.set_enabled(true);
        Log::emulator()->info("Rewind buffer: {} frames × {} bytes = {} KB",
            cfg.rewind_buffer_frames, snap_bytes,
            (static_cast<uint64_t>(cfg.rewind_buffer_frames) * snap_bytes + 512) / 1024);
    } else {
        rewind_buffer_.reset();
        rewind_enabled_ = false;
        Log::emulator()->debug("Rewind buffer: disabled");
    }

    // Instruction trace log (Task 27 A2): own opt-in, decoupled from rewind.
    // Only ever enables here — never disables — so a debugger Enable Trace
    // toggle survives a soft reset, matching the pre-A2 behaviour.
    if (cfg.trace) {
        trace_log_.set_enabled(true);
        Log::emulator()->info("Instruction trace log enabled (--trace)");
    }

    // -----------------------------------------------------------------------
    // G46(b) EOD-30i+29: JNEXT_TRACE_* env-gates for large trace capture.
    // All four have zero cost when the respective env var is unset.
    //
    // JNEXT_TRACE_CAPACITY=N
    //   Resize the trace ring buffer to N entries at init time.
    //   Clamped to [1024, 16777216] to avoid trivially small or OOM cases.
    //   Each TraceEntry is ~56 bytes; 1M entries ≈ 56 MB.
    //
    // JNEXT_TRACE_FORCE_ENABLE=1
    //   Force-enable trace recording unconditionally (default: only enabled
    //   when rewind buffer is active).
    //
    // JNEXT_TRACE_NO_WRAP=1
    //   Stop recording once the buffer is full (preserves earliest entries).
    //   Without this, the ring buffer overwrites oldest with newest.
    //   Use with JNEXT_TRACE_CAPACITY to capture early-boot instructions.
    //
    // JNEXT_TRACE_DUMP_AT_EXIT=/path/to/file
    //   Handled in ~Emulator() — exported at emulator shutdown.
    // -----------------------------------------------------------------------
    if (const char* cap_env = std::getenv("JNEXT_TRACE_CAPACITY")) {
        char* end = nullptr;
        unsigned long long cap_val = std::strtoull(cap_env, &end, 10);
        constexpr size_t kMinCap =       1024ULL;
        constexpr size_t kMaxCap = 16777216ULL;  // 16 M entries ≈ 896 MB
        if (cap_val < kMinCap) cap_val = kMinCap;
        if (cap_val > kMaxCap) cap_val = kMaxCap;
        trace_log_.resize(static_cast<size_t>(cap_val));
        Log::emulator()->info("[G46b] JNEXT_TRACE_CAPACITY: trace buffer resized to {} entries (~{} MB)",
            cap_val, (cap_val * 56 + 524288) / 1048576);
    }

    if (std::getenv("JNEXT_TRACE_FORCE_ENABLE")) {
        trace_log_.set_enabled(true);
        Log::emulator()->info("[G46b] JNEXT_TRACE_FORCE_ENABLE: trace recording forced on");
    }

    if (std::getenv("JNEXT_TRACE_NO_WRAP")) {
        trace_log_.set_no_wrap(true);
        Log::emulator()->info("[G46b] JNEXT_TRACE_NO_WRAP: trace will stop recording when full (preserves earliest entries)");
    }

    // Task 21 — allocate the CPU profiler if requested. Skipped on soft
    // reset so a re-init() within the same session preserves the
    // accumulated histogram.
    if (cfg.profile && !preserve_memory && !profiler_.active()) {
        profiler_.init();
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

    // GH #164 — see resume_from_park(). This call names an entry point and
    // sets PC to it; a parked CPU would never reach it.
    resume_from_park("a binary was injected");

    // Set PC to the requested entry point.
    auto regs = cpu_.get_registers();
    regs.PC = pc;
    // Set SP to a sensible value if it hasn't been moved.
    regs.SP = 0xFFFD;
    // Disable interrupts — the injected binary can enable them itself.
    regs.IFF1 = 0;
    regs.IFF2 = 0;
    // GH #248 — the halted latch describes the program we just displaced, not
    // the one we are entering. NextZXOS idles in a HALT waiting for its 50 Hz
    // interrupt, so at any plausible --inject-delay the CPU is halted when the
    // injection lands; leaving the latch set makes the first interrupt or NMI
    // take FUSE's halted branch (fuse_z80_core.c:140,:177,:192, mirrored at
    // z80_cpu.cpp:636) and return the injected program one byte past the
    // instruction it interrupted. sna_loader.cpp:206 and z80_loader.cpp:87
    // clear it for the same reason.
    regs.halted = false;
    cpu_.set_registers(regs);

    Log::emulator()->info("--inject: loaded {} bytes from '{}' at {:#06x}, PC={:#06x}",
                           static_cast<int>(size), path, org, pc);
    Log::emulator()->debug("--inject: first bytes at {:#06x}: {:02x} {:02x} {:02x} {:02x}",
                            org, mmu_.read(org), mmu_.read(org+1), mmu_.read(org+2), mmu_.read(org+3));
    return true;
}

// ---------------------------------------------------------------------------
// Warm start (GH #234) — run a loaded program on a machine the firmware made
// ---------------------------------------------------------------------------
//
// See the block comment on the declarations in emulator.h for what this is
// and why it is not `--bypass-tbblue-fw` (removed 2026-07-11): that modelled
// what the firmware does, this records what the firmware did.

bool Emulator::nextzxos_resident(std::string& why) const
{
    if (config_.type != MachineType::ZXN_ISSUE2) {
        why = "not a Next machine";
        return false;
    }

    // 1. The IPL ran. `nextboot.rom` masks 0x0000-0x1FFF until it writes
    //    NR 0x03, which is also what turns its own overlay off
    //    (VHDL zxnext.vhd:5147-5151 / the bootrom_en gate at :3052). An
    //    overlay still active means the boot never got past the first ROM.
    if (mmu_.boot_rom_enabled()) {
        why = "the FPGA boot-ROM overlay is still active — the IPL did not run to "
              "its NR 0x03 write";
        return false;
    }

    // 2. The machine-type commit happened. `nr_03_config_mode` powers on at
    //    '1' (VHDL zxnext.vhd:1102, no reset clause) and its ONLY mutator is
    //    the NR 0x03 write handler at :5147-5151. Still set means no firmware
    //    drove it — which is exactly the GH #226 state this mechanism exists
    //    to stop shipping to loaded programs.
    if (nextreg_.nr_03_config_mode()) {
        why = "NextREG 0x03 config mode is still set — no firmware committed a "
              "machine type";
        return false;
    }

    // 3. NextZXOS specifically, not merely "some ROM". tbblue.fw's
    //    `load_roms()` streams the selected machine's 64 KB ROM set into SRAM
    //    pages 0-7 through the config-mode NR 0x04 window; for the Next that
    //    set is `/MACHINES/NEXT/enNextZX.rom`, and NextZXOS's autoexec path
    //    literal rides in it. Searched across the whole ROM window rather
    //    than read at a fixed offset, because the offset is a property of one
    //    NextZXOS build and the string is a property of NextZXOS.
    //
    //    Checks 1 and 2 alone would pass on a machine that booted the IPL and
    //    then failed to find firmware, which is the case with a real symptom
    //    and no error: a recording of an empty machine, cached, and served to
    //    every later load.
    static const char  kMarker[]  = "nextzxos/autoexec";
    constexpr size_t   kMarkerLen = sizeof(kMarker) - 1;
    constexpr uint16_t kRomPages  = 8;          // 8 x 8 KB = the 64 KB ROM set
    constexpr size_t   kPageBytes = 0x2000;
    for (uint16_t p = 0; p < kRomPages; ++p) {
        const uint8_t* page = ram_.page_ptr(p);
        if (!page) continue;
        // Spans are searched page by page. A marker straddling a page
        // boundary would be missed, so the pages are also searched as one
        // contiguous run below — pages 0-7 ARE contiguous in Ram::data_
        // (8 KB apart by construction), which the adjacency assert covers.
        for (size_t i = 0; i + kMarkerLen <= kPageBytes; ++i) {
            if (std::memcmp(page + i, kMarker, kMarkerLen) == 0) {
                why.clear();
                return true;
            }
        }
    }
    // Straddle sweep: only the 8 boundary windows the per-page loop cannot
    // see. Cheap, and it removes a "works until the ROM is rebuilt and the
    // string moves 4 bytes" failure.
    for (uint16_t p = 0; p + 1 < kRomPages; ++p) {
        const uint8_t* a = ram_.page_ptr(p);
        const uint8_t* b = ram_.page_ptr(static_cast<uint16_t>(p + 1));
        if (!a || !b) continue;
        uint8_t window[2 * (sizeof(kMarker) - 1)];
        std::memcpy(window, a + kPageBytes - (kMarkerLen - 1), kMarkerLen - 1);
        std::memcpy(window + kMarkerLen - 1, b, kMarkerLen - 1);
        const size_t wlen = 2 * (kMarkerLen - 1);
        for (size_t i = 0; i + kMarkerLen <= wlen; ++i) {
            if (std::memcmp(window + i, kMarker, kMarkerLen) == 0) {
                why.clear();
                return true;
            }
        }
    }

    why = "no NextZXOS ROM found in SRAM pages 0-7 (searched for \"";
    why += kMarker;
    why += "\") — the firmware booted but left no NextZXOS resident";
    return false;
}

bool Emulator::record_warm_start_state(std::vector<uint8_t>& out)
{
    // A RECORDING IS A COLD BOOT, and it must be one on a machine that has
    // never been anything else.
    //
    // Re-init()ing THIS emulator is not a cold boot and cannot be made into
    // one from inside a member function (see the init() declaration, GH #239).
    // The first attempt did exactly that and produced a machine that ran for
    // 500 frames and drew nothing: `nr_03_config_mode` has no reset clause in
    // the VHDL (zxnext.vhd:1102) and NextReg::reset() faithfully preserves it,
    // so the firmware-less commit the FIRST init() had already made left config
    // mode CLEAR — and with it clear, tbblue.fw's ROM streaming through the
    // NR 0x04 window (the mmu.h config-mode routing at VHDL :3044-3050) never
    // reaches the ROM area at all. Measured: 4 RAM pages changed in 500 frames,
    // none of them the screen.
    //
    // So the recording runs on its OWN, freshly constructed Emulator, which is
    // at power-on state by construction — the same thing
    // platform/emulator_boot.h::emulator_cold_boot() does for F1. It also means
    // a failed recording cannot leave the live machine half-booted.
    auto rec = std::make_unique<Emulator>();

    // The recording boot is the ORDINARY boot with exactly the things that are
    // about THIS LOAD, or about the HOST, taken out:
    //
    //   load_file      the gate that disarms the boot-ROM overlay in init().
    //                  Clearing it is the whole point: this run must boot.
    //   inject_file    a raw binary would be written over the firmware's RAM.
    //   rewind/trace   500 frames x ~2.3 MB of snapshots nobody will rewind to.
    //   esp            a second module would open a second socket, and no part
    //                  of a NextZXOS boot sends an AT command.
    //   profile        the recorder is not the run the user is measuring.
    //   capture cbs    audio and DAC callbacks belong to the host's output
    //                  path; the boot is not something the host is playing.
    //
    // Everything else — the SD image, --rtc, the machine type, audio gains,
    // CPU speed — is left alone on purpose. A boot recorded under a different
    // configuration is a recording of a different machine, and the divergences
    // that would introduce are precisely the class this mechanism removes.
    EmulatorConfig boot_cfg       = config_;
    boot_cfg.load_file            = "";
    boot_cfg.inject_file          = "";
    boot_cfg.rewind_buffer_frames = 0;
    boot_cfg.trace                = false;
    boot_cfg.esp_enabled          = false;
    boot_cfg.profile              = false;
    boot_cfg.audio_capture_callback = nullptr;
    boot_cfg.dac_write_callback     = nullptr;

    Log::emulator()->info(
        "warm start: no usable cached state — cold-booting the firmware "
        "(nextboot.rom -> TBBLUE.FW -> NextZXOS) for {} frames to record one. "
        "This happens once per SD image.",
        kWarmStartBootFrames);

    if (!rec->init(boot_cfg)) {
        Log::emulator()->error("warm start: the recording boot could not initialise");
        return false;
    }

    for (uint32_t f = 0; f < kWarmStartBootFrames; ++f) rec->run_frame();

    std::string why;
    if (!rec->nextzxos_resident(why)) {
        Log::emulator()->error(
            "warm start: after {} frames of boot, {}. Not recording; this load "
            "falls back to the synthetic machine.",
            kWarmStartBootFrames, why);
        return false;
    }

    StateWriter measure;
    rec->save_state(measure);
    const size_t bytes = measure.position();

    out.assign(bytes, 0);
    StateWriter w(out.data(), bytes);
    rec->save_state(w);
    if (w.overflow() || w.position() != bytes) {
        Log::emulator()->error(
            "warm start: state serialisation wrote {} bytes against a measured {} "
            "(overflow={}) — not recording",
            w.position(), bytes, w.overflow());
        out.clear();
        return false;
    }

    Log::emulator()->info("warm start: recorded a NextZXOS-resident machine ({} KB)",
                          (bytes + 512) / 1024);
    return true;
}

bool Emulator::ensure_warm_start_state()
{
    if (!warm_start_state_.empty()) return true;
    if (!config_.sd_card_image.empty() &&
        warm_start_failed_image_ == config_.sd_card_image) {
        // The verdict is latched per image (see the member's doc comment), but
        // the ANNOUNCEMENT is not. Every load that silently gets the synthetic
        // machine is a load whose result the user cannot explain, and with the
        // warm start now the behaviour rather than an option there is no flag
        // on the command line to remind them it was even attempted. So the
        // latch short-circuits the 500-frame boot, not the sentence.
        Log::emulator()->error(
            "warm start: this SD image was already refused earlier in this session — "
            "this load falls back to the synthetic machine");
        return false;
    }

    if (config_.type != MachineType::ZXN_ISSUE2) {
        // NOT an error, and deliberately quiet. The warm start is the default
        // for a Next; on a 48K/128K/+3 there is no firmware to record, so
        // there is nothing being refused and nothing the user did wrong. When
        // this was a flag the user had typed, saying so on `warn` was right —
        // they had asked for something that does not exist. Now that every
        // `--load` comes through here, the same line would print on every
        // legacy-machine run forever, which is how a log stops being read.
        Log::emulator()->debug(
            "warm start: not applicable on the {} — only the Next boots firmware, so "
            "there is nothing to record",
            machine_type_str(config_.type));
        return false;
    }
    if (config_.sd_card_image.empty()) {
        Log::emulator()->error("warm start: no SD image is mounted, so there is no "
                               "firmware to boot — this load falls back to the "
                               "synthetic machine, which is NOT what hardware does");
        return false;
    }

    warm_start::Identity id;
    id.machine_type   = static_cast<uint8_t>(config_.type);
    id.format_version = warm_start::kFormatVersion;
    {
        StateWriter measure;
        save_state(measure);
        id.plain_bytes = measure.position();
    }
    // The whole-image digest, on every load, is what keeps this a recording
    // rather than a fixture — and it is the dominant cost of a warm start
    // once the recording exists (~1.2 s for a 1 GB image; the restore itself
    // is milliseconds). It is paid deliberately. The cheap alternatives all
    // ACCEPT a cached recording without reading the image — a (size, mtime)
    // key, a hashed prefix, a named subset of files — and every one of them
    // can serve a recording of a DIFFERENT card while reporting success,
    // which is the worst failure this mechanism can have because the machine
    // it produces looks booted. Within a process the digest is paid once: the
    // restored state stays in warm_start_state_ for the session.
    id.sd_image_sha256 = sdcard::sha256_file(config_.sd_card_image);
    if (id.sd_image_sha256.empty()) {
        Log::emulator()->error("warm start: cannot digest SD image '{}' — this load "
                               "falls back to the synthetic machine",
                               config_.sd_card_image);
        warm_start_failed_image_ = config_.sd_card_image;
        return false;
    }

    if (config_.warm_start_regenerate) {
        Log::emulator()->info("warm start: --warm-start-regenerate — ignoring any "
                              "cached state and taking a fresh recording");
    } else {
        std::string why;
        if (warm_start::load(id, warm_start_state_, why)) {
            Log::emulator()->info("warm start: restored a recorded NextZXOS machine "
                                  "from {} ({} KB)",
                                  warm_start::cache_path(id.machine_type),
                                  (warm_start_state_.size() + 512) / 1024);
            return true;
        }
        Log::emulator()->info("warm start: {}", why);
    }

    if (!record_warm_start_state(warm_start_state_)) {
        warm_start_state_.clear();
        warm_start_failed_image_ = config_.sd_card_image;
        return false;
    }

    // The recording came off a DIFFERENT Emulator, so the length it produced
    // is asserted here rather than assumed. It cannot differ today — every
    // Emulator constructs the same fixed-size Ram — but the length is the
    // guard that stops a foreign stream writing past that buffer
    // (Ram::load_state reads a count-prefixed blob straight into it), and a
    // guard derived from an assumption is not one.
    if (warm_start_state_.size() != id.plain_bytes) {
        Log::emulator()->error(
            "warm start: the recording is {} bytes but this machine's state stream is "
            "{} — discarding it",
            warm_start_state_.size(), id.plain_bytes);
        warm_start_state_.clear();
        warm_start_failed_image_ = config_.sd_card_image;
        return false;
    }

    std::string why;
    if (!warm_start::store(id, warm_start_state_, why)) {
        // A state that cannot be written is still a state that can be USED:
        // this run gets its warm machine, the next one pays another boot.
        Log::emulator()->warn("warm start: recorded but not cached ({}); the next run "
                              "will boot again", why);
    } else {
        // The on-disk size is read back rather than computed: it is the
        // compressed length, the plain stream above is ~2.2 MB, and printing
        // the number that is actually on the disk is the only way a reader
        // can tell the compression is doing anything.
        std::error_code sz_ec;
        const auto on_disk =
            std::filesystem::file_size(warm_start::cache_path(id.machine_type), sz_ec);
        Log::emulator()->info("warm start: cached at {} ({} KB on disk, deflated from "
                              "{} KB)",
                              warm_start::cache_path(id.machine_type),
                              sz_ec ? 0u : static_cast<unsigned>((on_disk + 512) / 1024),
                              (warm_start_state_.size() + 512) / 1024);
    }
    return true;
}

bool Emulator::init_for_load_from_file()
{
    // No flag guards this. A `--load` of a .nex on a Next gets the recorded
    // machine because that is what the program would meet on hardware; an
    // option whose default is "start on a machine the firmware cannot
    // produce" is the wrong default, and GH #226 is what that default cost.
    // Every way this can decline — a non-Next, no SD image, a card with no
    // firmware, a recording that will not deserialise, a restore that is not
    // NextZXOS-resident — falls back to exactly the machine `--load` has
    // always assembled, and says so first.
    if (!ensure_warm_start_state()) return init(config_);

    // init() FIRST, then restore on top. The state stream carries the emulated
    // machine (RAM, MMU, NextREG, CPU, every peripheral's flip-flops); it does
    // not carry the host wiring init() installs — the port-dispatch lambdas,
    // the contention LUT, the ROM buffer, the SD card, the mixer's gains. So
    // the restore must land on a freshly wired machine, exactly as a rewind
    // does on a running one.
    if (!init(config_)) return false;

    StateReader r(warm_start_state_.data(), warm_start_state_.size());
    if (!load_state(r)) {
        // load_state() names the subsystem whose sentinel failed. A state that
        // deserialises wrong leaves the machine half-restored, so the only
        // safe answer is to build a clean synthetic one and say what happened.
        Log::emulator()->error(
            "warm start: the recorded state failed to deserialise ({}) — discarding "
            "it and falling back to the synthetic machine",
            last_state_error().empty() ? std::string("unknown") : last_state_error());
        warm_start_state_.clear();
        warm_start_failed_image_ = config_.sd_card_image;
        return init(config_);
    }

    // Verify the RESTORE, not just the recording. A state that passed
    // nextzxos_resident() when it was taken must still pass it once it has
    // been through a file and back; anything else means the round trip lost
    // the very thing the recording was for.
    std::string why;
    if (!nextzxos_resident(why)) {
        Log::emulator()->error(
            "warm start: the restored machine is not NextZXOS-resident ({}) — "
            "discarding it and falling back to the synthetic machine", why);
        warm_start_state_.clear();
        warm_start_failed_image_ = config_.sd_card_image;
        return init(config_);
    }

    // Hand over the way NextZXOS hands over: with 48 BASIC paged.
    //
    // `nexload.asm` never writes 0x7FFD or 0x1FFD — its very last act is
    // `rst $20` (nexload.asm:587), NextZXOS's "leave this dot command and
    // jump to HL". THAT is what selects ROM 3, and it is OS behaviour the
    // loader inherits rather than performs, which is why NexLoader::apply()
    // does not model it and must not: it would then fire on the synthetic
    // path too.
    //
    // MEASURED, on this emulator, driving the real chain — boot NextZXOS,
    // Command Line, `.nexload b.nex`: the machine sits at 7FFD=0x00/0x07,
    // 1FFD=0x00, sram_rom=0 for the whole menu and command line, and is at
    // 7FFD=0x10, 1FFD=0x06, sram_rom=3 from the moment the program's first
    // instruction runs. The synthetic path has only the 48K image in its
    // SRAM ROM pages, so it got this right by having no alternative; the
    // recording, taken at the NextZXOS menu, carries the OS's own ROM 0 and
    // got it wrong. Two committed references caught it — magic-bp-demo and
    // magic-port-demo both read the character set straight out of ROM
    // (`ROM_CHARSET 0x3C00`), and rendered noise where glyphs belong.
    //
    // Only the two ROM-select bits are set, not the measured bytes: 7FFD's
    // low bits also choose the bank at 0xC000 (which apply() overwrites from
    // the entry bank anyway) and 1FFD bit 1 is a special-paging config bit
    // (zxnext.vhd:4623-4625), inert while bit 0 is clear — the +3 disk motor
    // is bit 3 (zxnext.vhd:3757). Neither is what any part of this is about. ROM bank = (1FFD(2), 7FFD(4)) — mmu.h
    // current_rom_bank(), VHDL zxnext.vhd:2994.
    mmu_.map_plus3_bank(static_cast<uint8_t>(mmu_.port_1ffd() | 0x04));
    mmu_.map_128k_bank(static_cast<uint8_t>(mmu_.port_7ffd() | 0x10));

    Log::emulator()->info("warm start: NextZXOS is resident; the program is applied on "
                          "top of it, as nexload does on hardware");
    return true;
}

bool Emulator::load_nex(const std::string& path)
{
    NexLoader loader;
    if (!loader.load(path)) return false;

    // GH #228 — NEX V1.3 is an experimental format jnext does not officially
    // support: refuse it here, at the ONE seam every user-facing load path
    // funnels through (CLI --load, GUI menu load, esxDOS chain-load), unless
    // the user explicitly opted in. Checked BEFORE any state is touched, so
    // a refusal leaves the running machine exactly as it was. The CLI refuses
    // earlier with exit 1 (main.cpp) and the GUI shows a warning dialog; this
    // gate is what makes skipping either of those a load FAILURE rather than
    // a silent unsupported load.
    if (nex_version_needs_v13_optin(loader.header().version_bcd) &&
        !config_.allow_experimental_nex_v13) {
        Log::emulator()->error(
            "NEX: '{}' is version '{:.4s}' — NEX V1.3 is an experimental format "
            "and is not supported; refusing to load. Pass --experimental-nex-v1.3 "
            "(or confirm the GUI warning) to load it anyway, unsupported.",
            path, loader.header().version);
        return false;
    }

    // A new directly-loaded program invalidates any host handle/stream left by
    // the previous one. The path is restored below only when this NEX asks the
    // loader to keep its own file open.
    extended_nex_host_.clear();
    sd_card_.clear_read_overlay();

    // Full machine re-initialisation before applying NEX data ensures clean
    // subsystem state (palette, video layers, NextREG, etc.) regardless of
    // whether the emulator was already running or freshly started. In place,
    // not a cold boot — see the init() declaration (GH #239).
    //
    // GH #234: with --warm-start this is the RECORDED post-firmware machine
    // instead of the synthetic one, so what follows applies the NEX on top of
    // a resident NextZXOS — which is what nexload does on hardware. Falls back
    // to plain init() whenever a recording is not available; see
    // init_for_load_from_file().
    init_for_load_from_file();

    if (!loader.apply(*this)) return false;
    active_nex_path_ = std::filesystem::absolute(path).lexically_normal().string();

    // nexload runs under NextZXOS, after its SD initialization handshake.
    // A direct load bypasses that code, so recreate the external card state
    // that the launched program would otherwise inherit.
    sd_card_.prepare_for_direct_nex();

    if (loader.keeps_file_open()) {
        if (!extended_nex_host_.open(active_nex_path_)) {
            Log::emulator()->error(
                "NEX: cannot keep '{}' open for its requested file handle",
                active_nex_path_);
            return false;
        }
        // GH #267 — the handle is the loader's own, passed on where its bank
        // loading left it: just after the last bank (nexload2.asm:390-407
        // `.passHandleToApp` and nexload.asm:547-570 hand it over without a
        // seek), so a bare F_READ reads the appended payload. That is
        // payload_offset(), the end of the header-described banks.
        // Measured under NextZXOS: a bare 4-byte F_READ returns the first
        // payload bytes and F_FGETPOS then reports payload + 4.
        extended_nex_host_.seek(0, static_cast<uint32_t>(loader.payload_offset()));
        sd_card_.set_read_overlay(
            ExtendedNexHost::kSyntheticFirstBlock,
            extended_nex_host_.block_count(),
            [this](uint32_t sector, uint8_t* dst) {
                return extended_nex_host_.read_block(sector, dst);
            });

        // file_handle 1..0x3FFF: handle in BC (B=0, C=handle); 0x4000 and
        // above: the handle byte is written at that address. See
        // NexLoader::delivers_handle_in_bc() for the loader citations.
        const uint8_t handle = ExtendedNexHost::kHandle;
        if (loader.delivers_handle_in_bc()) {
            auto regs = cpu_.get_registers();
            regs.BC = handle;
            cpu_.set_registers(regs);
            Log::emulator()->debug("NEX: delivered open file handle {} in BC", handle);
        } else {
            mmu_.write(loader.header().file_handle, handle);
            Log::emulator()->debug(
                "NEX: delivered open file handle {} at {:#06x}",
                handle, loader.header().file_handle);
        }
    }

    // BC when no handle travels in it (file_handle 0, or 0x4000 and above).
    // Both loaders load BC unconditionally just before the jump, from an
    // `ld bc,nn` whose low byte the handle-in-BC case patches, and the two
    // default immediates differ. As for NR 0x07 (GH #166), follow the loader
    // that would really run the file:
    //   - V1.3: nexload2.asm:407 `.handleInBcSMC=$+1 : ld bc,255` -> BC=$00FF
    //     ("no handle"; its v2.2 changelog, :39: "set up C to 255 in case
    //     there's no file handle (for C projects)").
    //   - V1.0-V1.2: the distro nexload.asm:582-585 `db 01` + `.regBCHandleSMC
    //     db 0` + `db 0`, i.e. `ld bc,$0000` -> BC=$0000. nexload2 would give
    //     $00FF here too, but the distro's .nexload is the loader NextZXOS
    //     ships for these versions (it refuses V1.3, nexload.asm:291,:749).
    //     BC is ALREADY $0000 at this point — the init(config_) above
    //     re-initialises the CPU, which zeroes BC — so this half of the write
    //     changes nothing; it only makes the distro loader's value explicit
    //     next to nexload2's.
    if (!loader.delivers_handle_in_bc()) {
        auto regs = cpu_.get_registers();
        regs.BC = loader.is_v13() ? 0x00FF : 0x0000;
        cpu_.set_registers(regs);
    }

    // The last thing either loader does is `rst $20` with SP already at the
    // header SP, and the NextZXOS DivMMC ROM's handler reaches the program
    // through `push hl : ... : ret` ($0071 then $1FF9), with HL = PC: so the
    // word just below the entry SP holds the entry PC, and no other byte
    // there is touched. Measured under NextZXOS with a probe NEX whose stack
    // area was filled with $A5: SP-2..SP-1 = PC, SP-8..SP-3 still $A5. After
    // the handle write above, as in both loaders. A load-only file (PC 0)
    // never gets there.
    if (loader.header().pc != 0) {
        const uint16_t sp = loader.header().sp;
        mmu_.write(static_cast<uint16_t>(sp - 2), static_cast<uint8_t>(loader.header().pc));
        mmu_.write(static_cast<uint16_t>(sp - 1), static_cast<uint8_t>(loader.header().pc >> 8));
    }

    // GH #250 — the program now runs with no NextZXOS behind it, so arm the
    // esxDOS answers nexload's OS would have provided (see the handler in
    // init()). Disarmed by init() (soft reset, re-initialising loads), like
    // the host bridge.
    // A GUI File -> Open load can occur after init(), when no command-line
    // NEX existed and the dormant bridge was deliberately not attached, so
    // attach it here too. When disarmed the handler returns false,
    // preserving the ROM's ordinary RST $08 path.
    direct_nex_esxdos_ = true;
    if (!cpu_.on_esxdos_call && esxdos_bridge_handler_) {
        cpu_.on_esxdos_call = esxdos_bridge_handler_;
    }
    return true;
}

std::string Emulator::take_nex_load_request()
{
    std::string request = std::move(nex_load_request_);
    nex_load_request_.clear();
    return request;
}

Emulator::EsxdosStubState Emulator::esxdos_stub_state() const
{
    return {esxdos_stub_filename_, esxdos_stub_file_};
}

void Emulator::restore_esxdos_stub_state(EsxdosStubState state)
{
    esxdos_stub_filename_ = std::move(state.filename);
    esxdos_stub_file_ = std::move(state.file);
    esxdos_stub_file_pos_ = 0;
    esxdos_stub_file_open_ = false;
    esxdos_stub_file_write_ = false;
}

bool Emulator::load_sna(const std::string& path)
{
    SnaLoader loader;
    if (!loader.load(path)) return false;
    init(config_);   // re-initialise in place first (see init(), GH #239)
    if (!loader.apply(*this)) return false;
    // A snapshot puts the CPU in its interrupt mode without executing an IM
    // instruction, which is what the IM latch behind NR 0xC0 bits 2:1 (and
    // the hardware-IM2 gates) is fed by; seed it from the restored CPU.
    im2_.set_im_mode(cpu_.get_registers().IM);
    return true;
}

void Emulator::resume_from_park(const char* reason)
{
    if (!cpu_parked_) return;   // silent no-op: callers call unconditionally
    cpu_parked_ = false;
    Log::emulator()->info(
        "CPU un-parked: {} — the machine was holding a load-only NEX's state "
        "(header PC=0) and would otherwise never have run it", reason);
}

void Emulator::set_magic_breakpoint(bool enabled)
{
    config_.magic_breakpoint = enabled;
    if (!enabled) {
        cpu_.on_magic_breakpoint = nullptr;
        return;
    }
    cpu_.on_magic_breakpoint = [this](uint16_t pc) -> bool {
        Log::emulator()->info("Magic breakpoint hit at PC={:#06x}", pc);
        debug_state_.set_active(true);
        debug_state_.pause();
        return true;
    };
}

bool Emulator::load_tap(const std::string& path, bool fast_load)
{
    TapLoader loader;
    if (!loader.load(path)) return false;

    // A fast (trapped) load cannot be recorded — see the trap block in run_frame.
    if (fast_load && rzx_recorder_.is_recording()) {
        Log::emulator()->info("TAP: loading in real time: an RZX recording is running");
        fast_load = false;
    }

    // GH #164 — see resume_from_park(). Neither the ROM LD-BYTES trap nor
    // the phantom typist can fire while the CPU is parked.
    resume_from_park("a TAP tape was attached");

    loader.set_fast_load(fast_load);
    tape_ = std::move(loader);
    Log::emulator()->info("TAP: tape attached — {} blocks, mode: {}",
                           tape_.block_count(), tape_.fast_load() ? "fast" : "realtime");

    // A new tape replaces whatever tape was in, whatever its format: a TZX
    // or WAV left behind would keep the status bar, Rewind and Eject on it.
    if (tzx_tape_.is_loaded()) tzx_tape_.eject();
    if (wav_tape_.is_loaded()) wav_tape_.eject();

    // Task 19 (instant TAP load): instead of immediately queuing the
    // LOAD"" keypress sequence (which fights a 100-frame ROM-boot
    // delay in main.cpp / *_app.cpp), arm the FUSE-style phantom
    // typist. The typist watches port-0xFE reads for the canonical
    // "ROM is ready for input" signal (all 8 keyboard rows polled in
    // one frame) and queues the per-machine sequence as soon as it
    // fires — instantly after BASIC reaches its main input loop.
    //
    // Mode selection mirrors FUSE's table:
    //   48K          → KEYWORD: J + SS+P + SS+P + ENTER  (LOAD "")
    //   128K / +3    → MENU:    ENTER  (default menu entry = tape loader)
    //   ZXN_ISSUE2   → MENU too: NextZXOS routes tape LOAD through the
    //                  +3 BASIC submenu (real Next behaviour — the Next
    //                  is a Spectrum superset and tape LOAD uses the
    //                  classic LD-BYTES trap in +3 BASIC ROM). The
    //                  trigger condition is the same (ROM full keyboard
    //                  scan), so the typist only fires once BASIC is
    //                  ready — tbblue.fw's targeted SPACEBAR-row poll
    //                  does NOT trip it.
    phantom_typist_.arm(config_.type, /*needs_code=*/false);

    // For real-time mode, start tape playback immediately.
    // The leader tone will play while the ROM waits for edge detection.
    if (!tape_.fast_load()) {
        tape_.start_realtime_playback();
    }

    return true;
}

void Emulator::rebase_fuse_tstates_()
{
    // See the declaration. (clock_ - frame_cycle_) is the position in the
    // current frame; at the frame start it is the previous frame's overshoot.
    uint32_t& live = *fuse_z80_tstates_ptr();
    const uint32_t old = live;
    const uint32_t want = static_cast<uint32_t>(
        (clock_.get() - frame_cycle_) / static_cast<uint64_t>(clock_.cpu_divisor()));
    tstates_frame_base_ += live;
    tstates_frame_base_ -= want;
    live = want;
    // GH #265 — a pending /INT window and FUSE's EI-just-executed stamp are
    // positions on this counter: move them with it. Without this an
    // interrupt still pending when a frame ends (a pulse straddling the frame
    // edge: Pentagon's frame interrupt, a CTC or UART request) was dropped as
    // expired at the first boundary of the new frame, and an EI in a frame's
    // last instruction lost its grace. At a CPU-speed change the window keeps
    // its distance in CPU T-states, as the pulse counts CPU clock edges
    // (zxnext.vhd:2035-2044); the IM2 fabric re-places its own pulse edges
    // (Im2Controller::set_cpu_divisor()).
    cpu_.rebase_interrupt_window(static_cast<int64_t>(old) - static_cast<int64_t>(want));
}

void Emulator::advance_fuse_tstates_(uint64_t master_cycles)
{
    // See the declaration. Callers advance the clock in whole T-states.
    *fuse_z80_tstates_ptr() += static_cast<uint32_t>(
        master_cycles / static_cast<uint64_t>(clock_.cpu_divisor()));
}

void Emulator::skip_trap_cycles_(uint64_t master_cycles)
{
    // A tape ROM trap replaced a ROM routine: charge its nominal time to the
    // clock AND to the FUSE counter (GH #265 follow-up, finding 4), then let
    // both event queues catch up (the frame and line interrupt requests live
    // in the second one, GH #265).
    clock_.tick(master_cycles);
    advance_fuse_tstates_(master_cycles);
    run_scheduled_until(clock_.get());
}

uint64_t Emulator::monotonic_tstates() const
{
    // Frame base (all completed frames) + live FUSE counter (advances
    // mid-instruction, including during I/O cycles — required so ZOT's
    // edge state machine progresses inside tight IN A,(0xFE) loops).
    return tstates_frame_base_ + static_cast<uint64_t>(*fuse_z80_tstates_ptr());
}

bool Emulator::load_tzx(const std::string& path, bool fast_load)
{
    TzxLoader loader;
    if (!loader.load(path)) return false;

    // A fast (trapped) load cannot be recorded — see the trap block in run_frame.
    if (fast_load && rzx_recorder_.is_recording()) {
        Log::emulator()->info("TZX: loading in real time: an RZX recording is running");
        fast_load = false;
    }

    // GH #164 — see resume_from_park().
    resume_from_park("a TZX tape was attached");

    loader.set_fast_load(fast_load);
    tzx_tape_ = std::move(loader);
    Log::emulator()->info("TZX: tape attached, mode: {}",
                           tzx_tape_.fast_load() ? "fast" : "realtime");

    // A new tape replaces whatever tape was in, whatever its format.
    if (tape_.is_loaded()) tape_.eject();
    if (wav_tape_.is_loaded()) wav_tape_.eject();

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
        uint64_t cpu_clocks = monotonic_tstates();
        tzx_tape_.start_playback(cpu_clocks);
        Log::emulator()->info("TZX: playback started at T-state {}", cpu_clocks);
    }

    return true;
}

bool Emulator::load_szx(const std::string& path)
{
    SzxLoader loader;
    if (!loader.load(path)) return false;
    init(config_);   // re-initialise in place first (see init(), GH #239)
    if (!loader.apply(*this)) return false;
    im2_.set_im_mode(cpu_.get_registers().IM);   // see load_sna()
    return true;
}

bool Emulator::load_z80(const std::string& path)
{
    Z80Loader loader;
    if (!loader.load(path)) return false;
    init(config_);   // re-initialise in place first (see init(), GH #239)
    if (!loader.apply(*this)) return false;
    im2_.set_im_mode(cpu_.get_registers().IM);   // see load_sna()
    return true;
}

bool Emulator::load_wav(const std::string& path)
{
    WavLoader loader;
    if (!loader.load(path)) return false;

    // GH #164 — see resume_from_park().
    resume_from_park("a WAV tape was attached");

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
    wav_tape_.start_playback(monotonic_tstates());

    return true;
}

bool Emulator::load_snapshot_from_memory(const std::vector<uint8_t>& data,
                                         const std::string& ext,
                                         const std::string& name)
{
    // Parse, then re-initialise in place with init(config_) — as load_sna/
    // szx/z80 do (GH #239) — then apply: a snapshot that does not parse leaves
    // the running machine untouched. Every playback the frontends start
    // reaches here on a freshly initialised machine anyway (see
    // emulator_start_rzx() and MainWindow::handle_rzx_play_path()).
    if (ext == "sna") {
        SnaLoader loader;
        if (!loader.load_from_buffer(data, name)) return false;
        init(config_);
        return loader.apply(*this);
    }
    if (ext == "szx") {
        SzxLoader loader;
        if (!loader.load_from_buffer(data, name)) return false;
        init(config_);
        return loader.apply(*this);
    }
    if (ext == "z80") {
        Z80Loader loader;
        if (!loader.load_from_buffer(data)) {
            Log::emulator()->error("Z80: failed to parse {}", name);
            return false;
        }
        init(config_);
        return loader.apply(*this);
    }
    if (ext == "jns") {
        // GH #274 — what a Next records. No init(config_) first: a `.jns`
        // restores the whole machine, its TYPE included, and does its own
        // container and manifest checking before it touches a subsystem.
        //
        // THE SD IDENTITY IS DELIBERATELY OVERRIDDEN HERE, and nowhere else.
        // OWNER DECISION, 2026-09-26 — sanctioned, not convenient, which is
        // what the next reader of a deliberate Tier-1 override needs to know.
        // A `.jns` FILE refuses a load against a different card (§11.3), which
        // is right for a snapshot — but an RZX is a portable artifact, offered
        // in the documentation as a good bug report, and a recording records
        // INPUT: the card is not part of what it claims to carry, and the 48K
        // SNA this replaces carried no card identity at all, so this is strictly
        // more faithful than what it replaces. Refusing to replay a recording on
        // the machine that received it would defeat the format. The override
        // still WARNS and names both cards, because a silent override of a
        // Tier-1 check is not acceptable even when it is right.
        // `--snapshot-mode strict` still governs the PROVENANCE checks (state
        // model, ROM digests): only the card identity is exempt.
        //
        // HOW MUCH THE OVERRIDE ACTUALLY CARRIES — measured, because an approved
        // decision resting on an unverified assumption still rests on one. Three
        // paths could let the card reach a replay:
        //   1. Guest reads through the CPU: CANNOT. `PortDispatch::in()` serves
        //      EVERY `IN` from the recorded log with no port filter at all
        //      (port_dispatch.cpp:187-197), so SD data read through 0xE7/0xEB
        //      comes from the log whatever card is mounted.
        //   2. Guest reads through the DMA: CAN. `dma_.read_io` is wired to
        //      `port_.read()` in init(), the PRE-override path, so a DMA port
        //      read is neither recorded nor replayed — on any machine and for
        //      any snapshot type. A pre-existing RZX limitation, not one this
        //      introduces, and the one case where the override is load-bearing
        //      rather than belt-and-braces.
        //   3. The ROMs: the boot ROMs are extracted from the card at init, so a
        //      different card can boot different ROMs. That path IS checked —
        //      the `.jns` records ROM digests, which warn here and REFUSE under
        //      `--snapshot-mode strict`.
        jnext::JnsLoadOptions opt;
        opt.strict       = config_.jns_strict;
        opt.force_sdcard = true;
        jnext::JnsLoadReport report;
        std::string          why;
        if (!load_jns(data.data(), data.size(), opt, report, why)) {
            Log::emulator()->error("JNS: {}: {}", name, why);
            return false;
        }
        for (const std::string& w : report.warnings)
            Log::emulator()->warn("JNS: {}: {}", name, w);
        return true;
    }
    // Refused rather than skipped: playing a recording's input against a
    // machine it was not recorded on reproduces nothing, so "skip the
    // snapshot and play anyway" is a silent failure, not a fallback.
    Log::emulator()->error("{}: unsupported snapshot type '{}' (supported: sna, szx, z80, jns)",
                           name, ext);
    return false;
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
    if (rec.later_snapshots > 0) {
        Log::emulator()->warn(
            "RZX: '{}' continues from {} more snapshot(s); jnext plays only the part "
            "before the second one ({} frames)",
            path, rec.later_snapshots, rec.frames.size());
    }

    if (rzx_refused_by_tape_save("play")) return false;

    // The frontends build the machine the recording was made on before they
    // get here (emulator_boot_machine()); only an explicit --machine can make
    // it differ, and then the input is played against the wrong machine.
    MachineType recorded = config_.type;
    if (!rzx::recorded_machine(rec, recorded)) {
        Log::emulator()->info("RZX: '{}' does not say which machine it was recorded on; "
                              "playing it on the {}", path, machine_type_str(config_.type));
    } else if (recorded != config_.type) {
        Log::emulator()->warn("RZX: '{}' was recorded on the {} but plays on the {} "
                              "(--machine); it will probably go out of step",
                              path, machine_type_str(recorded), machine_type_str(config_.type));
    }

    // Playback replaces the machine and answers every IN from the file, so a
    // recording still running would record nothing from here on: write it and
    // end it first (as FUSE does on opening a file), rather than leave it
    // running on empty.
    end_rzx_at_reset("playing an RZX recording");

    // Load the embedded snapshot, if present, straight from memory. It used to
    // be written to a fixed /tmp/jnext_rzx_snap.<ext> and loaded back from
    // there, unchecked: two jnext instances raced on that one file, and on
    // Windows the path does not exist at all, so the write failed silently
    // and the load then read nothing (or another instance's snapshot).
    if (!rec.snapshot_data.empty() &&
        !load_snapshot_from_memory(rec.snapshot_data, rec.snapshot_ext,
                                   "RZX '" + path + "' embedded snapshot")) {
        Log::emulator()->error("RZX: cannot load the embedded '{}' snapshot of '{}'",
                               rec.snapshot_ext, path);
        return false;
    }

    // GH #164 — see resume_from_park(). With an embedded snapshot the
    // load_sna/load_szx above already reset (which un-parks); without one,
    // playback drives whatever machine is currently loaded.
    resume_from_park("RZX playback started");

    // Wire up port override for playback.
    rzx_player_.start(std::move(rec));
    port_.rzx_in_override = [this](uint16_t) -> uint8_t {
        return rzx_player_.next_in_value();
    };

    return true;
}

bool Emulator::start_rzx_recording(const std::string& path)
{
    // Refused, never silently replaced: starting over the top of a running
    // recording used to throw that recording away unwritten.
    if (rzx_recorder_.is_recording()) {
        Log::emulator()->error("RZX: already recording to '{}'; stop that recording first",
                               rzx_recorder_.output_path());
        return false;
    }
    // During playback every IN is answered from the file being played and
    // never reaches the recorder, so the "recording" would hold no input.
    if (rzx_player_.is_playing()) {
        Log::emulator()->error("RZX: cannot record while an RZX recording is playing");
        return false;
    }
    if (rzx_refused_by_tape_save("record")) return false;

    if (!rzx_recorder_.start(path)) return false;
    rzx_suspend_tape_traps();

    // Embed the machine the recording starts from, in the richest format that
    // machine has.
    //
    // GH #274 — A NEXT EMBEDS A `.jns`. It used to embed a 48K SNA, which holds
    // registers, banks 5/2/0 and the border and NOTHING a Next adds: no
    // NextREGs, no Layer 2, no tilemap, no sprites, no Copper, no DivMMC. A
    // recording therefore replayed correctly only when the program happened to
    // redraw its whole display from scratch during the replayed frames — and
    // measurably failed when it did not: `test02layer2.nex` replayed 70183
    // pixels away from its own recording. `.jns` is the only format that can
    // represent a Next, so it is what a Next records.
    //
    // 128K and +3 keep the SZX: an SNA cannot carry their 0x1FFD paging and
    // `SzxSaver` covers both. A 48K keeps the SNA, where the CPU view IS the
    // machine, and it now goes through the checked `SnaSaver::save()` — on a
    // 48K that is the same bytes, and a 48K whose window something moved is
    // refused rather than mis-saved (see SnaSaver's class doc-comment).
    std::vector<uint8_t> snap;
    std::string          snap_ext;
    if (config_.type == MachineType::ZXN_ISSUE2) {
        jnext::JnsSaveOptions opt;
        opt.uncompressed = config_.jns_uncompressed;
        jnext::JnsLoadReport report;
        std::string          why;
        std::vector<uint8_t> jns;
        if (save_jns(opt, jns, report, why)) {
            snap     = std::move(jns);
            snap_ext = "jns";
        } else {
            // Loud, and then no snapshot at all rather than a lossy one: the
            // SnaSaver fallback below refuses a Next, which is the point.
            Log::emulator()->error("RZX: cannot embed a '.jns' snapshot of this Next: {}", why);
        }
    } else if (config_.type == MachineType::ZX128K || config_.type == MachineType::ZX_PLUS3) {
        SzxSaver::SaveResult szx = SzxSaver::save(*this);
        if (szx.ok) {
            snap     = std::move(szx.data);
            snap_ext = "szx";
        }
    }
    if (snap.empty()) {
        snap     = SnaSaver::save(*this);
        snap_ext = "sna";
    }
    if (!snap.empty()) rzx_recorder_.set_snapshot(std::move(snap), snap_ext);
    // The snapshot does not have to say which machine it is for — an SNA cannot,
    // and rzx.h's snapshot_machine() reads no `.jns` — so the file names it in
    // its creator block: playback builds that machine.
    rzx_recorder_.set_machine(config_.type);
    rzx_recorder_.set_initial_tstates(*fuse_z80_tstates_ptr());

    // Wire up port recording hook.
    port_.rzx_in_record = [this](uint8_t val) {
        rzx_recorder_.record_in(val);
    };

    return true;
}

bool Emulator::stop_rzx_recording()
{
    port_.rzx_in_record = nullptr;
    if (!rzx_recorder_.is_recording()) return true;   // nothing to write
    const std::string path = rzx_recorder_.output_path();
    const bool ok = rzx_recorder_.stop();
    if (!ok) rzx_failed_outputs_.push_back(path);
    return ok;
}

void Emulator::rzx_suspend_tape_traps()
{
    // The tape traps stand down while a recording runs (see the trap block in
    // run_frame). A fast-load tape would then never load — nothing drives its
    // EAR edges — so it is switched to real-time loading from where it is.
    if (tape_.is_loaded() && tape_.fast_load() && !tape_.at_end()) {
        tape_.set_fast_load(false);
        tape_.start_realtime_playback();
        Log::emulator()->info("TAP: switched to real-time loading: an RZX recording "
                              "cannot capture a fast (trapped) load");
    }
    if (tzx_tape_.is_loaded() && tzx_tape_.fast_load() && !tzx_tape_.at_end()) {
        tzx_tape_.set_fast_load(false);
        tzx_tape_.start_playback(monotonic_tstates());
        Log::emulator()->info("TZX: switched to real-time loading: an RZX recording "
                              "cannot capture a fast (trapped) load");
    }
}

bool Emulator::rzx_refused_by_tape_save(const char* verb) const
{
    // --tape-save captures SAVEs through a trap that skips the ROM's SA-BYTES,
    // which no recording can replay, and the traps stand down during RZX
    // anyway — so the two cannot be combined, from any route: the command
    // line refuses it at parse time, the GUI in a dialog, and this is the
    // backstop that makes both true.
    if (!tap_saver_.active()) return false;
    Log::emulator()->error("RZX: cannot {} while --tape-save is armed: its SAVE trap cannot "
                           "be replayed from a recording", verb);
    return true;
}

bool Emulator::end_rzx_at_reset(const char* what)
{
    bool ok = true;
    if (rzx_recorder_.is_recording()) {
        // Finalised, not continued: a reset the host performs cannot be
        // replayed from recorded input, so a recording carried across it
        // would replay a machine that never reset. FUSE, the reference RZX
        // implementation, stops recording on a menu reset for the same reason.
        Log::emulator()->warn(
            "RZX: {} ends the recording to '{}' after {} frames; nothing after "
            "it is recorded (a recording cannot replay a reset)",
            what, rzx_recorder_.output_path(), rzx_recorder_.recording().frames.size());
        ok = stop_rzx_recording();
    }
    if (rzx_player_.is_playing()) {
        Log::emulator()->warn("RZX: {} ends the playback", what);
        rzx_player_.stop();
        port_.rzx_in_override = nullptr;
    }
    return ok;
}

bool Emulator::rzx_output_failed(const std::string& path) const
{
    for (const auto& p : rzx_failed_outputs_)
        if (p == path) return true;
    return false;
}

void Emulator::repush_video_timing_from_machine_timing()
{
    // VideoTiming constants (VHDL zxula_timing.vhd c_*, keyed on i_timing
    // AND i_50_60).
    //
    // Task 56 — the 50/60 Hz selection is committed here from the pending
    // `nr_05_5060` FF, modelled by bit 2 of the NR 0x05 cache. VHDL
    // zxnext.vhd:6697-6700 latches `eff_nr_05_5060 <= nr_05_5060` at
    // `video_frame_sync = '1'` — the SAME frame-edge latch as
    // `eff_nr_03_machine_timing` (comment at :6692: "changes to video
    // timing occur during vsync") — and :6720 feeds it to
    // zxula_timing's `i_50_60`. The cache is a faithful model of the
    // pending FF: the NR 0x05 write handler and the NR 0x03 handler's
    // Pentagon-entry canonicalisation both force bit 2 to 0 whenever
    // `nr_03_machine_timing(2) = '1'`, mirroring the continuous FF
    // clear at zxnext.vhd:5835-5836 (Pentagon is always 50 Hz).
    // Pre-fix this preserved video_timing_.refresh_60hz(), which was
    // only ever initialised to false — a runtime NR 0x05 bit-2 write
    // (or the F3 hotkey, which routes through nextreg_.write(0x05,..))
    // never re-initialised the frame geometry.
    apply_video_timing(contention_.machine_timing(),
                       (nextreg_.cached(0x05) & 0x04) != 0);
}

// GH #237 — the body below was the tail of
// repush_video_timing_from_machine_timing(); it is now shared with
// Emulator::init(), which seeds the very same constants from the very same
// tim_sel axis. One helper, so the five things it programs cannot drift
// apart between the boot path and the runtime frame-edge commit path.
void Emulator::apply_video_timing(MachineTimingMode mode, bool refresh_60hz)
{
    video_timing_.init_timing(mode, refresh_60hz);

    // Master-cycle frame geometry (`timing_`) + CPU-side line geometry, both
    // derived from the freshly initialised VideoTiming so there is a single
    // source of truth for the new mode's line/frame lengths.
    const int pixels_per_line = video_timing_.hc_max() + 1;
    const int lines_per_frame = video_timing_.vc_max() + 1;
    timing_.pixels_per_line   = pixels_per_line;
    timing_.lines_per_frame   = lines_per_frame;
    timing_.tstates_per_line  = pixels_per_line / 2;
    timing_.tstates_per_frame = (pixels_per_line / 2) * lines_per_frame;
    timing_.master_cycles_per_line =
        static_cast<uint64_t>(pixels_per_line) * 4u;
    timing_.master_cycles_per_frame =
        timing_.master_cycles_per_line * static_cast<uint64_t>(lines_per_frame);

    z80_set_frame_geometry(timing_.tstates_per_line, timing_.tstates_per_frame);
    z80_set_ula_counter_origins(video_timing_.ula_prefetch_origin_hc(),
                                video_timing_.display_origin().vc);

    // Task 58 — the Copper's vertical wrap follows the same constants.
    // VHDL zxula_timing.vhd:457-470 wraps the copper offset counter
    // (`cvc`) at `c_max_vc`, and `c_max_vc` is re-derived from
    // i_timing / i_50_60 alongside every other c_* constant
    // (zxula_timing.vhd:168/204/238/270/298: 319 Pentagon, 310
    // 128K/+3 50 Hz, 311 48K 50 Hz, 263 any 60 Hz). Pre-fix this was
    // pushed once in init() only, so a runtime NR 0x03 tim_sel or
    // NR 0x05 50/60 change left the Copper wrapping at the stale
    // init()-time frame length. Same (lines_per_frame - 1) semantics
    // as the init()-time call.
    copper_.set_c_max_vc(timing_.lines_per_frame - 1);
}

void Emulator::begin_new_frame()
{
    // Everything that must happen exactly ONCE per frame, at its start. Called from
    // run_frame() only when a new frame is actually beginning — never when resuming a
    // frame the debugger paused mid-way. See the guard at its call site.

    // GH #102 session 4 — the once-per-frame sample of the IM2 DMA-delay
    // latch (user-resolved decision 2026-04-21, option A: "adequate...
    // finer-grain wiring can be revisited if a specific workload requires
    // it") was superseded by a per-instruction resample in
    // step_one_instruction() once TX-1696 demonstrated the coarse
    // granularity causing wild execution (see that call site for the full
    // citation). No sample is needed here: the per-instruction call runs
    // before any DMA/CPU work happens in the frame's first instruction too.
    // Snapshot at frame boundary — scheduler queue is empty here, which is
    // required for correct serialisation (no pending events to save).
    if (rewind_buffer_ && rewind_enabled_ && !replay_mode_) {
        rewind_buffer_->take_snapshot(*this, frame_cycle_, frame_num_++);
    }

    // GH #246 — the ESP's scheduled WiFi outage, anchored HERE and not in
    // service_esp_frame(), for the two reasons esp_association_at() gives:
    // this function runs exactly ONCE per logical frame (a frame the debugger
    // paused is resumed, not restarted, so run_frame() alone over-counts), and
    // it runs for REPLAYED frames too, so a rewind that re-traces frames 5..9
    // re-derives exactly the association those frames had the first time.
    // After the snapshot above, so the snapshot carries the count of the frame
    // that is beginning rather than the next one.
    advance_esp_schedule_frame();

    // V24-MEM-01 / V25-MEM-01 fix — video-frame-edge commit of the
    // NR 0x03 machine_timing latch per VHDL zxnext.vhd:6694-6703:
    //   if video_frame_sync = '1' then
    //     eff_nr_03_machine_timing <= nr_03_machine_timing;
    //   end if;
    // jnext's per-frame seam is the top of run_frame() (one commit per
    // logical video frame). Pre-fix the contention surfaces keyed on
    // `MachineType` (typ_sel axis), which silently aligned on the
    // canonical NextZXOS boot path (tim_sel == typ_sel) but diverged
    // when a user wrote NR 0x03 with bits 6:4 != bits 2:0. The
    // shadow→effective commit happens here so contention sees the new
    // tim_sel value starting at the next frame, matching VHDL.
    //
    // Task 51 — when the effective tim_sel actually CHANGES at this
    // commit edge, the video timing constants must follow: the VHDL
    // switches every zxula_timing c_* constant combinationally on
    // `eff_nr_03_machine_timing` (zxula_timing.vhd:147-280 keyed on
    // i_timing, fed from zxnext.vhd:6694-6703). Pre-fix, VideoTiming /
    // the CPU-side line geometry (derive_hc_vc) / the master-cycle
    // frame length (timing_) / the ULA counter origins were all set
    // ONCE at init() and a runtime NR 0x03 timing change left them
    // stale — the NextZXOS boot path writes NR 0x03.
    //
    // Task 56 — the 50/60 Hz selection (`nr_05_5060`, NR 0x05 bit 2)
    // commits at the SAME frame-edge latch: zxnext.vhd:6697-6700 latches
    // `eff_nr_05_5060 <= nr_05_5060` alongside `eff_nr_03_machine_timing`
    // when `video_frame_sync = '1'`. Re-push when the pending value
    // (NR 0x05 cache bit 2, Pentagon-canonicalised — see the NR 0x05
    // write handler and the NR 0x03 Pentagon-entry clear) differs from
    // the effective one (video_timing_.refresh_60hz()), so a runtime
    // NR 0x05 bit-2 write / F3 hotkey takes effect at the next frame
    // edge, never mid-frame.
    {
        const MachineTimingMode tim_before = contention_.machine_timing();
        contention_.commit_pending_machine_timing();
        mmu_.commit_pending_machine_timing();
        // G121 — the pulse-mode /INT width gate (zxnext.vhd:2033) decodes
        // the same effective latch (:5761-5776), so it moves here too.
        if (contention_.machine_timing() != tim_before) {
            const bool is_48_or_p3 =
                contention_.machine_timing() == MachineTimingMode::Timing48 ||
                contention_.machine_timing() == MachineTimingMode::TimingPlus3;
            im2_.set_machine_timing_48_or_p3(is_48_or_p3);
            cpu_.set_machine_timing_48_or_p3(is_48_or_p3);
        }
        const bool pend_60hz = (nextreg_.cached(0x05) & 0x04) != 0;
        if (contention_.machine_timing() != tim_before ||
            pend_60hz != video_timing_.refresh_60hz()) {
            repush_video_timing_from_machine_timing();
        }
        // Task 58 — NR 0x05 bit 0 (`nr_05_scandouble_en`) has an
        // effective copy latched at the SAME frame-sync edge: VHDL
        // zxnext.vhd:6702 `eff_nr_05_scandouble_en <=
        // nr_05_scandouble_en` whenever `video_frame_sync = '1'`
        // (unconditional, every frame). The NR 0x05 read mux at :5897
        // surfaces the effective copy, not the pending FF. No
        // behavioural consumer in jnext (the scan doubler is a
        // VGA-output device) — this exists for VHDL-faithful readback.
        eff_nr_05_scandouble_en_ = (nextreg_.cached(0x05) & 0x01) != 0;
        // NR 0x09 bits 1:0 (`nr_09_scanlines`) are latched in the SAME
        // `video_frame_sync` block: VHDL zxnext.vhd:6701
        // `eff_nr_09_scanlines <= nr_09_scanlines`. The NR 0x09 read mux
        // (:5909) surfaces the effective copy, so a mid-frame NR 0x09
        // write (or an F7 hotkey increment) must not be visible on a read
        // until this edge.
        eff_nr_09_scanlines_ =
            static_cast<uint8_t>(nextreg_.cached(0x09) & 0x03);
    }

    // Rebase the FUSE tstates counter onto the new frame. derive_hc_vc() in
    // z80_cpu.cpp computes (hc, vc) directly from `tstates % tstates_per_frame`,
    // so the FUSE counter must be frame-relative for ContentionModel::
    // contention_tick() to gate on the right raster window.
    //
    // GH #265 follow-up (verifier finding 4): the counter is seeded with the
    // last instruction's OVERSHOOT past frame_end, (clock_ - frame_cycle_) /
    // divisor, not 0. Zeroing it made every contention decision of the frame
    // lag the clock — and with it the floating bus, NR 0x1E/0x1F, the
    // interrupt and the display — by that overshoot, a different 0..~20 T
    // every frame. FUSE keeps it too (it subtracts the frame length).
    //
    // VideoTiming reset alongside: derive_hc_vc() in z80_cpu.cpp computes
    // (hc, vc) directly from `tstates`, so VideoTiming is the test-side
    // observable. Reset its hc/vc at frame start so test queries
    // mid-frame match the (hc, vc) the contention path is using.
    // G36/G37: rebase_fuse_tstates_() folds the outgoing frame's T-states
    // into the monotonic base, so monotonic_tstates() stays continuous. It
    // also moves the pending /INT window and FUSE's EI stamp with the counter
    // (GH #265).
    rebase_fuse_tstates_();
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
    //
    // GH #265 — the request is raised at the edge int_ula goes high: the
    // compare above is registered on CLK_7 (zxula_timing.vhd:548-557), so
    // int_ula is high for the pixel AFTER hc == c_int_h — 4 master cycles
    // (one 7 MHz pixel) after frame_int_master_cycle_offset(). It lives in
    // irq_scheduler_ so a status read inside an instruction can fire it.
    if (!ula_int_disabled_) {
        const uint64_t int_fire_offset = video_timing_.frame_int_master_cycle_offset();
        const uint64_t int_req_edge    = frame_cycle_ + int_fire_offset + 4;
        irq_scheduler_.schedule(int_req_edge, EventType::CPU_INT,
            [this, int_req_edge]() {
                im2_.raise_req(Im2Controller::DevIdx::ULA, int_req_edge);
                // V20R-CPU-NIT-02 — pulse-mode CPU /INT now driven solely
                // by the post-im2_.tick() falling-edge poll at line ~5791.
                // The legacy `cpu_.request_interrupt(0xFF)` here was a
                // redundant second stamp for the same logical pulse and
                // a latent double-INT trap if the ISR did `EI` within
                // the 32/36-cycle re-stamp window. The V20 poll captures
                // the same edge VHDL-faithfully one tick later (via
                // im2_peripheral.vhd:186-194 `o_pulse_en` → pulse_int_n
                // FSM → poll at zxnext.vhd:1840 z80_int_n composition).
                // Symmetric with the LINE-INT scheduler in
                // `reschedule_line_interrupt()` (also dropped).
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
    // frame at which hc_ula reaches 255 on the firing scanline, accounting
    // for all three (GH #257 — it used to return that scanline's raw hc 0).
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

    // Initialize per-line stencil-mode / blend-mode snapshots (NR 0x68 bit 0
    // and bits 6:5). VHDL zxnext.vhd:6810-6811,6897-6901,7064-7065 pipelines
    // ula_stencil_mode and ula_blend_mode through the exact same 3-stage
    // (stage0/1a/1/2) register chain as ula_en, so a Copper MOVE to NR 0x68
    // mid-frame must take effect on the next scanline, not the current one.
    renderer_.init_stencil_mode_per_line();
    renderer_.init_blend_mode_per_line();
    // GH #256 — NR 0x6B b7 as the stencil gate sees it, same chain.
    renderer_.init_tm_enabled_per_line();

    // Initialize per-line NR 0x14 (global transparent RGB) snapshot.
    // VHDL zxnext.vhd:1137,5226,6822,6912-6913,7078 pipeline
    // nr_14_global_transparent_rgb through the exact same stage0/1a/1/2
    // register chain as ula_en (Task 45 — the snapshot/init/getter trio
    // existed with zero call sites until now).
    renderer_.init_transparent_rgb_per_line();

    // Initialize the per-line LoRes register snapshot (NR $15 b7, $32, $33,
    // $6A).  VHDL zxnext.vhd:6768-6802 re-latches the LoRes parameters every
    // CLK_7 and :6817 latches lores_en every CLK_14 — a mid-frame Copper MOVE
    // or CPU write must therefore take effect from the following scanline,
    // not retroactively across the whole frame.
    renderer_.lores().init_per_line();

    // Initialize per-line ULA clip window (NR 0x1A) snapshot. VHDL
    // zxnext.vhd:988-991 — the clip comparators consume the live registers
    // per pixel, so a mid-frame NR 0x1A write (Copper MOVE / CPU racing
    // the beam) must only affect rows scanned after the write.
    // on_scanline → renderer_.snapshot_ula_clip_for_line refreshes
    // individual lines.
    renderer_.init_ula_clip_per_line();

    // Initialize per-line border colour to current value.
    // Port 0xFE writes will update individual lines during execution.
    renderer_.ula().init_border_per_line();
    // GH #256 — and the ULA+ / ULAnext / shadow-bank state beside it.
    renderer_.ula().init_control_per_line();

    // Initialize per-line tilemap scroll to current values.
    // Interrupt handlers may change scroll mid-frame for split-screen effects.
    tilemap_.init_scroll_per_line();
    // The tilemap map/definition bases and default attribute are live
    // inputs to the tile fetcher just like scroll. Games such as TX-1696
    // switch NR 0x6E/0x6F at raster boundaries to draw a fixed HUD and a
    // separately-scrolling playfield in one frame.
    tilemap_.init_fetch_per_line();
    // GH #256 — and the output-stage inputs (NR 0x1B clip, NR 0x4C index)
    // likewise, so a Copper MOVE to either applies from its row onwards.
    tilemap_.init_output_per_line(palette_.tilemap_transparency());

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
    // GH #256 — and its per-line render controls (NR 0x19 clip, NR 0x15
    // b6/b5/b1, NR 0x4B index), refreshed by on_scanline.
    sprites_.init_control_per_line(palette_.sprite_transparency());

    // Per-scanline port-0xFF Timex screen-mode snapshot (G07).
    renderer_.ula().start_frame();
    // Per-scanline ULA scroll snapshot (G08).
    renderer_.ula().start_frame_scroll();
    // Per-scanline active-palette selector snapshot (G10).
    renderer_.ula().palsel_start_frame();

    // G12 — Nirvana-class attribute-mux snapshot. Same pattern as the
    // per-scanline logs above. Round 4: also seed the column-accurate
    // hc-fetch origin (VideoTiming::ula_prefetch_origin_hc(), VHDL
    // zxula_timing.vhd:423) so render-time resolution can gate mid-
    // scanline writes by column, not just by scanline — see
    // attribute_mux.h's column-accurate-resolution block comment.
    mmu_.attr_mux_start_frame(video_timing_.ula_prefetch_origin_hc(),
                              video_timing_.vblank_top());

    // Per-scanline NR 0x15 change log (G02, GH #73) — baseline snapshot +
    // log reset, so a Copper/CPU mid-frame write to layer priority (b4:2)
    // or sprite enable (b0) takes effect from the following scanline
    // (VHDL zxnext.vhd:6799/6819 latch both into the video pipeline every
    // pixel clock). beast.nex toggles NR 0x15 0x80<->0x01 mid-frame.
    renderer_.start_frame_nr15();

    // Schedule per-scanline callbacks (snapshots fallback colour for copper).
    schedule_frame_events();
}

// ---------------------------------------------------------------------------
// GH #25 — the emulated ESP-01 on UART 0.
// ---------------------------------------------------------------------------

void Emulator::setup_esp()
{
    if (!config_.esp_enabled) return;

    // A SOFT reset re-runs init() with preserve_memory=true. Rebuilding the ESP
    // there would drop a live TCP connection on a reset the real module never
    // even sees: NR 0x02 bit 0 resets the Next-side UART state machine, not the
    // thing on the far end of the cable (design doc §4.3). So: build once.
    if (esp_device_) return;

    esp_events_ = std::make_unique<EspConnectionLog>();

    EspHostPolicy host_policy;
    for (const std::string& host : config_.esp_allowed_hosts)
        host_policy.add(host);

    // The DEFAULT address policy, unmodified: loopback / link-local / cloud
    // metadata / unspecified / multicast denied, RFC1918 allowed so the guest
    // can reach the user's own LAN (owner decision, design doc §8.1 item 4).
    esp_transport_ = std::make_unique<EspGatedTransport>(
        esp::make_socket_transport(esp::AddressPolicy{}), host_policy, *esp_events_);

    // GH #154 — `AT+CIPDOMAIN`. It gets BOTH of the transport's policies, and
    // that is the point rather than tidiness: a resolver the allowlist did not
    // gate would hand the guest the address of every host it may not dial, and
    // one without the address policy would disclose the loopback and
    // cloud-metadata addresses the transport is careful never to reach. One
    // rule, two commands — `AT+CIPDOMAIN` answers only for a host
    // `AT+CIPSTART` would have been allowed to try.
    esp_resolver_ = std::make_unique<EspGatedResolver>(
        esp::make_socket_resolver(esp::AddressPolicy{}), host_policy, *esp_events_);

    // GH #154 (owner Q6) — `AT+PING`, gated by the same allowlist. There is no
    // `AddressPolicy` here and its absence is deliberate: that policy judges an
    // address the module RESOLVED, and `ping` does its own resolution inside a
    // child process we do not get to inspect. The host allowlist is the gate
    // that can be enforced on this path, and it is enforced on the NAME, which
    // is what the guest actually supplied.
    esp_pinger_ = std::make_unique<EspGatedPinger>(esp::make_icmp_pinger(esp::AddressPolicy{}),
                                                   host_policy, *esp_events_);

    // GH #154 (owner Q7) — SNTP, under BOTH policies for the same reason the
    // pinger is: an NTP server is a host the guest named, and a query it may
    // not make is a query it may not make.
    esp_sntp_ = std::make_unique<EspGatedSntp>(esp::make_udp_sntp_client(esp::AddressPolicy{}),
                                               std::move(host_policy), *esp_events_);

    // GH #210 — the INBOUND half. Built here so that the bind address is fixed
    // before anything can listen: the guest chooses the PORT with
    // `AT+CIPSERVER`, never the address, which is what makes the user's
    // `--esp-listen-address` (loopback unless they widened it) the boundary
    // (design doc §13.4).
    //
    // A listener is created whenever the ESP is enabled, and it costs nothing
    // until the guest asks: no socket exists until `AT+CIPSERVER=1,<port>`.
    // There is deliberately no second host-side enable flag — the AT command IS
    // the opt-in, and a guest that can already dial out is not made more
    // dangerous by a gate it controls either way (§13.4's rejected alternative).
    esp::IpAddress bind_ip;
    if (esp::parse_ip(config_.esp_listen_address, bind_ip)) {
        esp_listener_ = esp::make_socket_listener(bind_ip);
    } else {
        // Unreachable from the CLI, which validates the address and refuses at
        // startup. Reachable from a caller that builds an EmulatorConfig by
        // hand, and the answer there is to have NO server rather than to guess
        // an address: AT+CIPSERVER then answers ERROR.
        Log::esp01()->error(
            "ESP listen address '{}' is not a numeric IP — AT+CIPSERVER will answer ERROR",
            config_.esp_listen_address);
    }

    // The THREADED wrapper, not the bare engine: it is what keeps
    // `EspTransport::poll()` off the frame loop, and its destructor joins the
    // worker (see the member declarations in emulator.h for why that matters
    // at exactly this address).
    esp_device_ = std::make_unique<esp::ThreadedEsp>(*esp_transport_, esp_listener_.get(),
                                                     esp_resolver_.get(), esp_pinger_.get(),
                                                     esp_sntp_.get());
    esp_adapter_ = std::make_unique<EspUartAdapter>(*esp_device_);
    // GH #246 — the reported station address, BEFORE the worker starts, so no
    // guest command can ever be answered with the default and then a second
    // one with the override.
    // The PRE-OUTAGE address, resolved once: the flag when given, else the
    // module's own synthetic default. `esp_station_ip_at()` returns a
    // reference to this, so the frame-by-frame derivation never has to ask the
    // engine what its default was (which would mean taking its lock).
    esp_station_ip_configured_ = config_.esp_ip_address.empty()
                                     ? std::string(esp::AtEngine::STA_IP)
                                     : config_.esp_ip_address;
    if (!config_.esp_ip_address.empty())
        esp_device_->set_station_ip(config_.esp_ip_address);
    esp_device_->start();

    uart_.attach_device(0, esp_adapter_.get());   // UART 0 is the ESP (zxnext.vhd:1611)

    // ONE info line, at startup, naming the posture in force. The user turned a
    // network capability on; telling them what it can reach is the least this
    // can do, and it is also how the "is my allowlist actually loaded?"
    // question gets answered without a debugger.
    if (config_.esp_allowed_hosts.empty()) {
        Log::esp01()->info(
            "ESP-01 enabled on UART 0 — ANY host allowed (no --esp-allow given); "
            "loopback, link-local and cloud-metadata addresses are always refused");
    } else {
        std::string list;
        for (const std::string& host : config_.esp_allowed_hosts) {
            if (!list.empty()) list += ", ";
            list += host;
        }
        Log::esp01()->info("ESP-01 enabled on UART 0 — allowed hosts: {}", list);
    }
    // The INBOUND posture, said separately because it is a separate direction
    // and the allowlist above has nothing to do with it (design doc §13.4).
    // Named at startup for the same reason the allowlist is: a user who has
    // widened it should be able to see that they did without a debugger.
    Log::esp01()->info(
        "ESP-01 server mode binds {} when the guest sends AT+CIPSERVER "
        "(nothing listens until it does)",
        config_.esp_listen_address);
    // GH #246 — and the address the guest will be TOLD it has, which is a
    // different thing from either of the two above and is worth saying so:
    // nothing binds it and nothing routes through it.
    Log::esp01()->info(
        "ESP-01 reports station address {} to the guest (AT+CIFSR / AT+CIPSTA?) — "
        "cosmetic; nothing binds or routes through it",
        esp_device_->station_ip());
}

void Emulator::setup_joy_uart_source()
{
    if (config_.joy_uart_rx_file.empty()) return;   // no cable attached
    // A SOFT reset must not unplug it, exactly as it must not rebuild the ESP:
    // NR 0x0B is reset and the guest has to set the mux up again, but the thing
    // on the other end of the cable never noticed.
    if (joy_uart_source_) return;

    std::vector<uint8_t> bytes;
    std::string          error;
    if (!read_joy_uart_source_file(config_.joy_uart_rx_file, bytes, error)) {
        // Unreachable from the CLI, which reads and validates the file at
        // startup and refuses there. Reachable from a caller that builds an
        // EmulatorConfig by hand, and the answer is NO source rather than a
        // silently empty one — see the warning at exhaustion below for why.
        Log::uart()->error("--joy-uart-rx: {} — no joystick serial source attached", error);
        return;
    }

    const std::size_t count = bytes.size();
    joy_uart_source_ = std::make_unique<JoyUartSource>(
        std::move(bytes), config_.joy_uart_connector, config_.joy_uart_rx_delay_frames);

    joy_uart_source_->set_byte_sink([this](uint8_t byte) {
        // The CONNECTOR gate, which `inject_joy_uart_rx` deliberately does not
        // apply: `joy_uart_rx` is the line AFTER the connector mux, and
        // zxnext.vhd:3538 reads it from `i_JOY_LEFT(5)` or `i_JOY_RIGHT(5)`
        // according to NR 0x0B bit 4. A cable in the other socket is a pin the
        // FPGA is not looking at, so the byte is lost on the wire — the same
        // outcome as the mux being off, and for the same physical reason.
        const bool routed = iomode_.joy_uart_en()
                         && iomode_.joy_uart_connector() == joy_uart_source_->connector();
        if (!routed) {
            joy_uart_source_->note_dropped();
            return;
        }
        inject_joy_uart_rx(byte);
        joy_uart_source_->note_delivered();
    });

    // ONE info line naming the posture, as the ESP does: which socket, how many
    // bytes, and when they start. The connector and the delay are the two
    // things that decide whether the guest hears anything at all, so they are
    // the two things worth being able to read back without a debugger.
    Log::uart()->info(
        "joystick serial source attached to joy {} — {} byte(s) from '{}', starting after "
        "{} frame(s); the guest hears them only while NR 0x0B selects UART mode "
        "(bits 7+5) on that connector (bit 4)",
        joy_uart_source_->connector() + 1, count, config_.joy_uart_rx_file,
        config_.joy_uart_rx_delay_frames);
}

void Emulator::service_joy_uart_source(uint64_t master_cycles)
{
    // Pace off the channel NR 0x0B bit 0 selects (zxnext.vhd:3340-3341). Bit 0
    // has a value whether or not the mux is enabled, so there is no special
    // case here: with the mux off the bytes are clocked out at that channel's
    // baud and dropped by the sink, which is what the cable does.
    const int ch = iomode_.iomode_0() ? 1 : 0;
    joy_uart_source_->tick(static_cast<uint32_t>(master_cycles),
                           uart_.channel(ch).byte_transfer_ticks());

    // The silent no-op guard. A stream that ran to its end without one byte
    // reaching the guest means the mux was never set the way the bench assumed,
    // and the run would otherwise report a pass for a path no byte took.
    if (!joy_uart_silence_reported_ && joy_uart_source_->exhausted()
        && joy_uart_source_->delivered() == 0) {
        joy_uart_silence_reported_ = true;
        Log::uart()->warn(
            "joystick serial source exhausted with all {} byte(s) LOST: NR 0x0B never "
            "selected UART mode (bits 7+5) with connector bit 4 = joy {}. Current "
            "NR 0x0B = {:#04x}",
            joy_uart_source_->dropped(), joy_uart_source_->connector() + 1,
            iomode_.nr_0b_raw());
    }
}

void Emulator::setup_joy_uart_link()
{
    if (config_.joy_uart_fifo.empty() && !config_.joy_uart_pty) return;  // no cable
    // A SOFT reset must not unplug it, exactly as it must not rebuild the ESP
    // or re-read the GH #251 recording: NR 0x0B resets and the guest has to set
    // the mux up again, but the host on the other end of the cable never
    // noticed, and re-opening the endpoint would drop whatever it holds.
    if (joy_uart_link_) return;

    std::string error;
    std::unique_ptr<JoyUartEndpoint> endpoint =
        config_.joy_uart_pty ? JoyUartEndpoint::open_pty(error)
                             : JoyUartEndpoint::open_fifo(config_.joy_uart_fifo, error);
    if (!endpoint) {
        // Unreachable from the CLI, which opens the endpoint at startup and
        // refuses there. Reachable from a caller that builds an EmulatorConfig
        // by hand, and the answer is NO cable rather than a silently dead one.
        Log::uart()->error("joystick serial cable: {} — no cable attached", error);
        return;
    }

    joy_uart_link_ = std::make_unique<JoyUartLink>(std::move(endpoint),
                                                   config_.joy_uart_connector);

    joy_uart_link_->set_byte_sink([this](uint8_t byte) {
        // The CONNECTOR gate, which `inject_joy_uart_rx` deliberately does not
        // apply: `joy_uart_rx` is the line AFTER the connector mux, read from
        // `i_JOY_LEFT(5)` or `i_JOY_RIGHT(5)` according to NR 0x0B bit 4
        // (zxnext.vhd:3538). A cable in the other socket is a pin the FPGA is
        // not looking at, so the byte is lost on the wire. Identical to the
        // GH #251 sink — RX is the direction the connector field governs.
        const bool routed = iomode_.joy_uart_en()
                         && iomode_.joy_uart_connector() == joy_uart_link_->connector();
        if (!routed) {
            joy_uart_link_->note_dropped();
            return;
        }
        inject_joy_uart_rx(byte);
        joy_uart_link_->note_delivered();
    });

    // ONE posture line, and for the pty form it carries the slave device path —
    // which is not a nicety but the whole interface: nothing else tells the user
    // what to point their debugger at.
    Log::uart()->info(
        "joystick serial cable attached to joy {} — {}; the guest hears it only "
        "while NR 0x0B selects UART mode (bits 7+5) on that connector (bit 4), "
        "and bit 0 picks UART 0 or UART 1. Transmit is not connector-selected "
        "(zxnext.vhd:1593, md6_joystick_connector_x2.vhd:116-117)",
        joy_uart_link_->connector() + 1, joy_uart_link_->describe());
}

void Emulator::service_joy_uart_link(uint64_t master_cycles)
{
    // Pace off the channel NR 0x0B bit 0 selects (zxnext.vhd:3340-3341). Bit 0
    // has a value whether or not the mux is enabled, so there is no special
    // case: with the mux off the bytes are clocked out at that channel's baud
    // and dropped by the sink, which is what the cable does.
    const int ch = iomode_.iomode_0() ? 1 : 0;
    joy_uart_link_->tick(static_cast<uint32_t>(master_cycles),
                         uart_.channel(ch).byte_transfer_ticks());
}

void Emulator::service_joy_uart_link_frame()
{
    // THE REPLAY GATE, the same one the ESP carries and for a stronger reason.
    // A rewind re-executes instructions the guest already ran: re-transmitting
    // their bytes would duplicate them at the peer, which is not recoverable,
    // and re-READING the endpoint would consume host bytes the post-replay
    // timeline still needs. Inert means neither, so the host's bytes simply wait
    // in the pipe. See JoyUartLink's header for why this is a gate, not a
    // teardown.
    //
    // THE GATE IS RAISED HERE AND ENFORCED IN THE LINK, not tested twice. An
    // early `return` here as well would be belt-and-braces over the check
    // `JoyUartLink::poll()` already makes, and a second copy of a rule is a
    // second thing that can be wrong while every test still passes: reverting
    // one of the two changes nothing, so neither is covered. One gate, in the
    // class that owns the descriptors and already gates `tick()` and
    // `send_to_host()` the same way.
    joy_uart_link_->set_inert(replay_mode_ || rzx_player_.is_playing());

    joy_uart_link_->poll();

    // A peer coming and going is normal and silent; anything else is a cable
    // that has stopped working, and a cable that has stopped working without
    // saying so is the silent no-op this whole feature exists to avoid — the
    // same posture as the GH #251 "exhausted with all bytes LOST" warning.
    if (!joy_uart_link_fault_reported_ && joy_uart_link_->faults() > 0) {
        joy_uart_link_fault_reported_ = true;
        Log::uart()->error(
            "joystick serial cable ({}) reported an I/O error: {}. The cable has "
            "stopped carrying traffic in at least one direction; the emulator "
            "keeps running",
            joy_uart_link_->describe(), joy_uart_link_->last_error());
    }
}

bool Emulator::esp_associated() const
{
    return esp_device_ && esp_device_->associated();
}

std::string Emulator::esp_station_ip() const
{
    return esp_device_ ? esp_device_->station_ip() : std::string{};
}

bool Emulator::esp_association_at(int frame) const
{
    // A PURE FUNCTION of the frame number, which is the whole point.
    //
    // The first version of this tracked the association as a flag that two
    // edges mutated, and that shape was wrong in two ways at once, both found
    // in review. A flag nothing serialises is STALE after a rewind: the
    // snapshot restores the machine to frame 5 and the module keeps the
    // association it had at frame 19, so a guest polling `AT+CIFSR` gets a
    // different answer the second time through the same instant — exactly the
    // determinism the rewind buffer exists to provide. And a flag advanced by
    // a counter in `service_esp_frame()` over-counts, because the debugger
    // RESUMES a paused frame by calling `run_frame()` again.
    //
    // Derived from the frame number, neither is possible: the answer at frame
    // N is the same answer however many times the emulator passes through it,
    // and `esp_frames_` travels in the snapshot with everything else.
    const int down = config_.esp_disassociate_frame;
    const int up   = config_.esp_associate_frame;
    if (down < 0 || frame < down) return true;   // nothing scheduled, or before the outage
    if (up >= 0 && frame >= up)   return true;   // after it ended
    return false;
}

const std::string& Emulator::esp_station_ip_at(int frame) const
{
    // PURE, for the same reason `esp_association_at` is, and the issue that
    // asked for this (GH #247) named the hazard itself: a second address held
    // as a flag that the re-association edge assigns would be stale after a
    // rewind, reintroducing exactly the defect review caught in GH #246 — a
    // guest polling `AT+CIFSR` getting a different answer the second time
    // through the same instant.
    //
    // The post-outage address applies from the re-association edge onward, and
    // only when one was configured. Everywhere else — including DURING the
    // outage, where the association flag makes `AT+CIFSR` answer 0.0.0.0
    // whatever this returns — the answer is the pre-outage address.
    const int up = config_.esp_associate_frame;
    if (!config_.esp_ip_address_after.empty() && up >= 0 && frame >= up)
        return config_.esp_ip_address_after;
    return esp_station_ip_configured_;
}

void Emulator::sync_esp_association(bool force)
{
    if (!esp_device_) return;
    if (config_.esp_disassociate_frame < 0 && config_.esp_associate_frame < 0) return;

    const bool         now = esp_association_at(esp_frames_);
    const std::string& ip  = esp_station_ip_at(esp_frames_);
    if (!force) {
        // The EDGES, computed rather than remembered: this frame differs from
        // the one before it. Deliberately not a comparison against the live
        // engine value, which would take the ESP core lock once per frame to
        // learn nothing 99.99% of the time. A restore is the one case where
        // the engine can disagree without an edge, and it passes force=true.
        //
        // BOTH derived values are compared (GH #247). The address changes at
        // the same edge the association does today, so comparing only the
        // association would still work — and would break silently the moment
        // the two stop coinciding, which is not a property worth depending on.
        const bool         prev_assoc =
            (esp_frames_ == 0) ? true : esp_association_at(esp_frames_ - 1);
        const std::string& prev_ip =
            (esp_frames_ == 0) ? esp_station_ip_configured_
                               : esp_station_ip_at(esp_frames_ - 1);
        if (now == prev_assoc && ip == prev_ip) return;
    }

    esp_device_->set_associated(now);
    esp_device_->set_station_ip(ip);
    if (now) {
        // "again" is only true when the address came back UNCHANGED; saying it
        // of an address that moved would misreport the very thing the run was
        // set up to observe (GH #247).
        if (ip == esp_station_ip_configured_) {
            Log::esp01()->info(
                "ESP-01 association REGAINED at frame {} (--esp-delayed-associate-frames) — "
                "AT+CIFSR reports {} again",
                esp_frames_, ip);
        } else {
            Log::esp01()->info(
                "ESP-01 association REGAINED at frame {} (--esp-delayed-associate-frames) — "
                "AT+CIFSR now reports {}, CHANGED from {} "
                "(--esp-ip-address-after)",
                esp_frames_, ip, esp_station_ip_configured_);
        }
    } else {
        Log::esp01()->info(
            "ESP-01 association LOST at frame {} (--esp-delayed-disassociate-frames) — "
            "AT+CIFSR now reports {}",
            esp_frames_, esp::AtEngine::UNASSOCIATED_IP);
    }
}

void Emulator::advance_esp_schedule_frame()
{
    if (!esp_device_) return;
    sync_esp_association(/*force=*/false);
    // SATURATES rather than wrapping. Signed overflow is undefined behaviour,
    // not a large number, and while ~497 days of continuous emulation to reach
    // it is not a run anyone makes, both edges are long past by then and there
    // is nothing left to count.
    if (esp_frames_ < std::numeric_limits<int>::max()) ++esp_frames_;
}

void Emulator::service_esp_frame()
{
    if (!esp_adapter_) return;   // ESP disabled: one predicted-not-taken branch

    // THE REPLAY GATE. Both of these re-execute instructions the guest has
    // already run, and a network connection cannot be re-executed: a replayed
    // AT+CIPSTART would open a second real socket and a replayed AT+CIPSEND
    // would deliver the payload to the peer twice. The ESP is not serialised
    // either (design doc, simplification 5), so there is nothing to rewind it
    // to. See EspUartAdapter::set_inert for why this is a gate and not a
    // detach.
    const bool inert = replay_mode_ || rzx_player_.is_playing();
    esp_adapter_->set_inert(inert);
    if (inert) return;

    esp_adapter_->poll();

    // The transport is contractually non-throwing and jnext supplies the only
    // one, so a non-zero count is a jnext bug. Say so once, loudly, in both
    // places a user looks — and keep running; see esp_host_policy.h for why
    // this does NOT disable the ESP.
    if (esp_note_transport_fault(esp_device_->pass_exceptions(), esp_fault_reported_)) {
        Log::esp01()->error(
            "the ESP transport threw ({} service passes lost) — the emulated ESP has "
            "stopped making progress and its worker is now idling. This is a jnext "
            "bug; please report it at https://github.com/jorgegv/jnext/issues",
            esp_device_->pass_exceptions());
        esp_events_->push({EspEvent::Kind::TransportFault, {}, 0,
                           "transport threw; the ESP has stopped making progress"});
    }
}

void Emulator::run_frame()
{
    // For the whole of this call, memory and port accesses are the EMULATED
    // MACHINE's, so watchpoints may fire. See DebugState::
    // GuestExecutionScope: outside these scopes they may not, which is what
    // stops the debugger's own panels from tripping the user's watchpoints on
    // the addresses they display.
    //
    // It wraps the WHOLE function, not just the instruction loop: the tape
    // ROM traps below load bytes into guest memory in place of the ROM
    // routine that would otherwise have written them, and a WRITE watchpoint
    // on the load area must see that exactly as it would without fast-load.
    DebugState::GuestExecutionScope guest_exec(debug_state_);

    // GH #25 — ESP service, deliberately FIRST. The replay gate below must be
    // applied before any instruction of a replayed frame executes, and
    // rewind_to_cycle() (called from the branch just after this) re-enters
    // run_frame() with replay_mode_ set, so each nested call gates itself here.
    service_esp_frame();

    // GH #252 — and the live joystick cable's once-per-frame half, for exactly
    // the same reason and in the same place: its replay gate has to be applied
    // before any instruction of a replayed frame runs.
    if (joy_uart_link_) service_joy_uart_link_frame();

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

    const uint64_t frame_end = frame_cycle_ + timing_.master_cycles_per_frame;

    // A frame that was PAUSED mid-way is RESUMED, not RESTARTED.
    //
    // The debugger pauses by returning from inside the loop below, leaving the frame
    // half-executed; the frontend then calls run_frame() again to continue it. Every
    // one of the actions in begin_new_frame() is a FRAME-START action, and re-running
    // them mid-frame corrupts the frame that is still in flight:
    //
    //   * copper_.on_vsync() rewinds the Copper's PC. A Copper program restarted at
    //     mid-frame re-executes its WAITs against scanlines that have already gone by,
    //     so it never matches again and writes NOTHING for the rest of the frame.
    //   * palette_/layer2_/sprites_/ula_/tilemap_.start_frame() CLEAR the per-scanline
    //     change logs and re-baseline them to the mid-frame state. Those logs are what
    //     the compositor (and the debugger's video panels) replay to reproduce raster
    //     splits, so the frame renders flat.
    //   * the interrupts get re-scheduled at offsets from frame_cycle_ that are now in
    //     the past, and the rewind buffer takes a "frame boundary" snapshot mid-frame,
    //     with a scheduler queue that is not empty — the one thing its comment promises.
    //
    // Symptom that found this (Task 40, beast.nex): Break, then "Run to EOF" — the
    // Copper's per-scanline palette gradient vanished from the ULA and the emulator
    // window rendered a flat sky, alternating between the two on each press as the
    // wiped log was consumed by one replay or the other. Stepping through a frame must
    // OBSERVE the emulation, never alter it.
    if (!frame_in_progress_) {
        begin_new_frame();
        frame_in_progress_ = true;
    }


    while (clock_.get() < frame_end) {
        // Debugger breakpoint check — before executing the next instruction.
        //
        // GH #219: armed(), not active(). armed() is active() OR
        // --persistent-breakpoints, so breakpoints survive closing the
        // debugger window; the other active() readers below (render hint,
        // per-instruction VideoTiming walk, the rewind step modes) stay on
        // active() so nothing but the check itself is switched on.
        if (debug_state_.armed()) {
            // Check if an external trigger (e.g. magic breakpoint) already
            // paused the emulator during the previous instruction's execute().
            if (debug_state_.paused())
                return;
            uint16_t pc = cpu_.pc();
            // GH #221 — step OFF the breakpoint we are standing on.
            //
            // This test runs before the instruction at `pc`. The arm is raised
            // only when a resume actually takes the machine from PAUSED to
            // running (DebugState::unpause_()), so between the arm and this
            // consumption the CPU executed nothing and `pc` is still the
            // address Run / Step Over / Step Out / Run to Here was pressed at.
            // Re-matching it pinned the machine there forever — paused_
            // cleared, PC unchanged, re-paused with no progress made — which
            // is the whole of #221, and it affected every resume path, not
            // just F5.
            //
            // consume_step_off() is true at most once per resume, so the
            // suppression is one instruction wide and skips nothing else: a
            // breakpoint at the NEXT address is a later iteration with the arm
            // already spent, and so is this same address on the next pass
            // round a loop. A redundant resume on an already-running machine
            // raises no arm at all, so it cannot swallow a later hit.
            if (!debug_state_.consume_step_off() && debug_state_.should_break(pc)) {
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

        // Tape ROM traps — only when ROM is paged in at slot 0, and never
        // while RZX records or plays: a trap does the ROM routine's work
        // without executing it, so no recording can replay it (the file holds
        // the INs the routine did not make), and a playback would skip the INs
        // the recording holds. FUSE disables its tape traps for RZX for the
        // same reason; a tape attached then loads in real time, and its
        // edges are recorded like any other input (see rzx_suspend_tape_traps()).
        if (mmu_.is_slot_rom(0) && !rzx_recorder_.is_recording() &&
            !rzx_player_.is_playing()) {
            uint16_t pc = cpu_.pc();

            // Fast-load: intercept LD-BYTES when tape is loaded and in fast mode.
            if (tape_.is_loaded() && tape_.fast_load() && !tape_.at_end() &&
                pc == TapLoader::LD_BYTES_ADDR) {
                tape_.handle_ld_bytes_trap(*this);
                skip_trap_cycles_(100ULL * clock_.cpu_divisor());
                continue;
            }

            // TZX fast-load: same ROM trap, different loader.
            if (tzx_tape_.is_loaded() && tzx_tape_.fast_load() && !tzx_tape_.at_end() &&
                pc == TzxLoader::LD_BYTES_ADDR) {
                tzx_tape_.handle_ld_bytes_trap(*this);
                skip_trap_cycles_(100ULL * clock_.cpu_divisor());
                continue;
            }

            // Tape SAVE (G33 Phase 1): intercept SA-BYTES when --tape-save
            // armed the saver. Gating: --tape-save present (intent, mirrors
            // the loaders' tape_.is_loaded() gate) + ROM paged in at slot 0
            // (outer if) + exact PC match + ROM identity — the bytes at
            // 0x04C2 must be the 48K SA-BYTES prologue (21 3F 05 E5).
            // Without the identity check this fired 10 times during a plain
            // NextZXOS boot (other ROMs execute at 0x04C2 too), appending
            // garbage blocks and corrupting the boot (Task 57 review).
            if (tap_saver_.active() && pc == TapSaver::SA_BYTES_ADDR &&
                TapSaver::sa_bytes_rom_present(mmu_)) {
                tap_saver_.handle_sa_bytes_trap(*this);
                skip_trap_cycles_(100ULL * clock_.cpu_divisor());
                continue;
            }

        }

        // Task 60a — shared per-instruction body (CPU/DMA/boot-hold
        // execute + trace + IM2 tick + /INT polls + clock advance +
        // DMA burst prescaler). Shared verbatim with
        // execute_single_instruction() so the free-running and
        // debugger single-step paths cannot drift.
        const uint64_t master_cycles = step_one_instruction();

        // Check if a data breakpoint was hit during this instruction.
        // GH #219: armed(), matching the MMU sites that raise the flag.
        if (debug_state_.armed() && debug_state_.data_bp_hit()) {
            debug_state_.pause();
            debug_state_.set_data_bp_hit(false);
            // GH #265 — the IM2 tick moved into the device cluster this exit
            // skips; the instruction's own fabric effects (an IntAck's
            // S_ACK -> S_ISR, a RETI) still belong to this slot.
            finish_slot_interrupts();
            return;
        }

        // Task 60a — shared post-instruction device-tick cluster
        // (Copper → NR drain → commits → CTC/UART/MD6 → IoMode →
        // NMI pipeline → PSG/audio → scheduler). Runs only when no
        // data breakpoint fired above (pre-existing early-return
        // semantics preserved).
        tick_devices_after_instruction(master_cycles);
    }

    end_of_frame(frame_end);
}

void Emulator::end_of_frame(uint64_t frame_end)
{
    // GH #207 — everything that must happen exactly ONCE per frame, at its
    // END. Extracted verbatim from the tail of run_frame() so the debugger's
    // single-step path (debugger_step() → step_frame_slot()) turns frames over
    // through the SAME body. Before the extraction the stepping path had no
    // frame boundary at all: begin_new_frame() was never reached again, so the
    // per-frame ULA/line interrupt and per-scanline scheduler events stopped
    // being scheduled the moment the debugger paused, and a HALTed CPU could
    // never be woken by any number of steps. Same "one shared body" rule as
    // Task 60a's step_one_instruction().
    //
    // `frame_end` is the CALLER'S OWN end-of-frame cycle, never a value
    // recomputed here, and the two callers sample it at deliberately different
    // points: run_frame() BEFORE its `if (!frame_in_progress_) begin_new_frame()`
    // block (:7324, pre-existing), step_frame_slot() AFTER it (:8366). They
    // agree except in one case — begin_new_frame() can commit a pending
    // NR 0x03 / NR 0x05 timing change and re-derive
    // `timing_.master_cycles_per_frame` through
    // repush_video_timing_from_machine_timing() (:6976), so on the ONE frame
    // that follows such a write, run_frame()'s bound is the OLD frame length
    // and step_frame_slot()'s is the new one.
    //
    // Taking the parameter rather than recomputing keeps each caller
    // self-consistent with the bound it actually ran its instructions to,
    // which is what matters for `frame_cycle_ = frame_end` here. Note for a
    // follow-up (GH #207 review): step_frame_slot()'s ordering looks like the
    // more VHDL-faithful of the two — the effective timing latch commits at
    // `video_frame_sync` (zxnext.vhd:6694-6703), i.e. at the frame edge, so
    // the frame that follows the commit should already be measured with the
    // new constants. Changing run_frame() to match is NOT done here: it moves
    // frame geometry for every consumer of a mid-session NR 0x03/0x05 write
    // and belongs in its own change with its own screenshot evidence.

    // Save end-of-frame raster before advancing frame_cycle_, so snapshot_raster()
    // can show a meaningful position when Break is pressed between frames.
    {
        uint64_t elapsed = clock_.get() - frame_cycle_;
        last_frame_vc_ = std::min(static_cast<int>(elapsed / timing_.master_cycles_per_line),
                                  timing_.lines_per_frame - 1);
        last_frame_hc_ = static_cast<int>((elapsed % timing_.master_cycles_per_line) / 4);
    }
    frame_cycle_ = frame_end;
    // The frame ran to completion, so the NEXT run_frame() call begins a new one.
    // Every early return above (breakpoint, pause, run-to-cycle) leaves this true, so
    // that call resumes this frame instead of restarting it.
    frame_in_progress_ = false;

    // G156 — one held frame has now completed in full (rendering/audio/
    // scheduler all ran normally above); count it down.
    if (boot_hold_frames_remaining_ > 0) {
        --boot_hold_frames_remaining_;
    }

    // GH #251 — and one more whole frame has gone by for the joystick serial
    // source's start delay. Anchored at this seam, not at begin_new_frame(),
    // for two reasons: it runs exactly ONCE per frame (a frame the debugger
    // pauses is resumed, not restarted), and counting COMPLETED frames is what
    // makes `--joy-uart-rx-delay-frames N` mean N whole frames of silence
    // rather than N-1. Unlike the ESP's schedule this is a countdown, not a
    // pure function of the frame number, so a REWIND does not rewind it — a
    // rewound stream would re-send bytes the guest has already consumed, the
    // same un-re-executable side effect the ESP replay gate exists for.
    if (joy_uart_source_) joy_uart_source_->end_frame();

    // Snapshot the last visible framebuffer row (FB_HEIGHT-1 = 255 — G164v2:
    // these arrays are indexed by fb_row, not by raw VC) — but ONLY when no
    // on_scanline() event followed row 255's raw line, i.e. when that line is
    // the frame's last (60 Hz: vblank_top 8 + 256 = 264 lines). Otherwise
    // on_scanline() has already captured row 255 at the end of its raw line,
    // and re-snapshotting here would overwrite it with the frame's FINAL
    // value: at 50 Hz a write in the undisplayed raw lines 288-310
    // (zxula_timing.vhd:195-204, c_max_vc 310) would repaint row 255. The
    // tilemap lane was guarded for GH #16/#257; every lane is now (GH #264).
    if (video_timing_.vblank_top() + Renderer::FB_HEIGHT
            >= timing_.lines_per_frame) {
        snapshot_row_render_state(Renderer::FB_HEIGHT - 1);
    }

    // Render the completed frame into the ARGB8888 framebuffer.
    // Suppressed in replay mode (fast-forward rewind path).
    //
    // Task 27 C6 — additionally suppressed when the frontend hinted that
    // nobody will consume this frame (render_enabled_ == false; Qt GUI whole
    // ticks at speed > 1x, and both GUI frontends' non-final frames of a
    // multi-frame tick — superseded composites, see platform/render_policy.h).
    // The hint is OVERRIDDEN — the frame is rendered anyway — when:
    //   * video recording is active: capture_frame() below consumes the
    //     framebuffer immediately after render, and a skipped render would
    //     silently freeze/corrupt the --record MP4;
    //   * the debugger is active: panels and the pause display read the
    //     framebuffer, and stepping/breakpoints must always show the frame
    //     that was actually emulated.
    // Headless mode (incl. --benchmark, which must measure the real render
    // workload) never clears render_enabled_, so its behaviour is
    // bit-identical to before.
    //
    // Enumeration of EVERY side effect of render_frame() and its callees,
    // with its guard or benign-skip argument (C6 review, 2026-07-15):
    //
    //   side effect                          | on a skipped frame
    //   -------------------------------------|--------------------------------
    //   framebuffer_ pixels                  | stale; every consumer guarded
    //                                        | (hint override for recorder +
    //                                        | debugger below; frontend forces
    //                                        | render on screenshot ticks)
    //   sprite collision_ / max_sprites_     | SOFTWARE-VISIBLE (port 0x303B,
    //   (SpriteEngine::render_scanline,      | sprites.vhd:971-995) — MUST run:
    //    mutable, sprites.h:449-450)         | run_sprite_side_effects() below
    //   ULA flash counter (advance_flash)    | emulated-time state — advanced
    //                                        | explicitly below
    //   per-scanline change-log lifecycles   | sprites + NR15: full rewind/
    //   (palette/L2/sprites/ULA×3/tilemap-   | apply/flush via the side-effect
    //    NR6B/attr-mux/NR15 rewind+apply+    | pass; all
    //    flush)                              | others dropped — benign,
    //                                        | display-only: live state was
    //                                        | mutated directly by the writers
    //                                        | and the next begin_new_frame()
    //                                        | re-baselines (replay_mode_
    //                                        | precedent, renderer.cpp:171-174)
    //   Ula::render_scanline border_colour_  | save/restore transients inside
    //   + Tilemap/Sprite debug-view enable   | one call (or debug-view-only) —
    //   save/restores                        | net-zero, nothing to do
    //   compositor trace (trace_frame_       | debug CLI tool (--compositor-
    //   counter_ / trace_fp_ / trace_active_)| trace) counting RENDERED frames;
    //                                        | headless-only workflow, benign
    //   Layer2 / Tilemap / compositor pixel  | pure reads, no member writes
    //   paths                                | (audited 2026-07-15)
    //
    // GUI "Save Screenshot" (Alt+S) at speed > 1x captures the last RENDERED
    // frame — consistent with what the window shows, at most ~20 ms stale.
    // Acknowledged benign consumer (C6 review MINOR).
    const bool render_this_frame =
        render_enabled_ || video_recorder_.is_recording() || debug_state_.active();
    if (!replay_mode_) {
        if (render_this_frame) {
            renderer_.render_frame(framebuffer_.data(), mmu_, ram_, palette_,
                                   layer2_, &sprites_, &tilemap_);
        } else {
            // C6 review BLOCKER fix: sprite collision + line-budget overtime
            // (port 0x303B) must be computed every emulated frame regardless
            // of rendering — run the sprites-only pass through the SAME code
            // path render_frame uses.
            renderer_.run_sprite_side_effects(&sprites_, palette_);
            renderer_.ula().advance_flash();
            // Debug-level witness for the regression suite
            // (render-skip-turbo-func): proves the skip engages, and that it
            // NEVER engages while recording (the line must be absent then).
            Log::video()->debug("render skipped for undisplayed frame (C6 frontend hint)");
        }
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

    // Task 19 (instant TAP load): tick the phantom typist BEFORE the
    // keyboard auto-type tick. If a full keyboard scan was observed in
    // this frame, the typist queues its keystrokes here; the very next
    // tick_auto_type() begins playing them. Frame-aligned for the same
    // reason as FUSE: the trigger condition is "all 8 rows polled in a
    // single frame".
    phantom_typist_.tick_frame();

    // Advance auto-type state machine (one step per frame).
    keyboard_.tick_auto_type();

    // G133 closure — the two-scan shift hysteresis (membrane.vhd:178-191) is
    // advanced by Keyboard::tick_scan(), which is driven from on_scanline() at
    // the REAL membrane scan rate (one complete scan every 4608 master cycles,
    // ~2.5 scanlines). It used to be called here, once per video frame; see the
    // GH #268 comment at the call site for why that was 122x too slow and what
    // it broke. run_frame() still drives it — on_scanline() is its own event.
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

int Emulator::nr_write_hpos() const
{
    // GH #270 — see the declaration in emulator.h for what the result means.
    int raw_hc;
    const int move_hc = copper_.active_move_hc();
    if (move_hc >= 0) {
        // Copper: `move_hc` is the VHDL `hc_ula` 7 MHz pixel counter
        // (zxnext.vhd:3949 hcount_i => hc, GH #181), whose zero sits at raw
        // hc = c_min_hactive - 11 (VideoTiming::hc_ula_zero_raw_hc). Rebase
        // onto the raw frame counter, because that is the counter the LINE
        // tag is in: Emulator::on_scanline fires at raw hc == 0
        // (schedule_frame_events), while an hc_ula line starts 125 pixels
        // later. Without the rebase, a Copper write in the raw hc 0..124
        // window would carry an hc_ula near the END of the line and be
        // rendered as "past the right edge" of a line it actually precedes.
        raw_hc = move_hc + video_timing_.hc_ula_zero_raw_hc();
        const int hc_span = video_timing_.hc_max() + 1;
        if (raw_hc >= hc_span) raw_hc -= hc_span;
    } else {
        raw_hc = current_hc();
    }
    return raw_hc - static_cast<int>(video_timing_.display_origin().hc);
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

uint64_t Emulator::step_one_instruction()
{
    // Task 60a — the ONE shared per-instruction body for run_frame()'s
    // inner loop and the debugger's execute_single_instruction().
    // Everything here runs exactly once per instruction slot: the CPU
    // instruction (or DMA burst / boot-hold step), the trace record,
    // the IM2 controller tick + both /INT line polls, the RZX count
    // and real-time tape tick, then the master-clock advance and the
    // DMA burst prescaler. Extracted verbatim from run_frame(); the
    // former hand-maintained copy in execute_single_instruction() had
    // drifted (missing im2_.tick(), both /INT polls, md6_.tick() and
    // the trace record), silently dropping interrupts while stepping.
    //
    // INVARIANT: every caller of this function holds a
    // DebugState::GuestExecutionScope, because everything below is the
    // emulated machine executing and its memory/port accesses must be able to
    // fire the user's watchpoints. There are exactly three callers —
    // run_frame(), step_frame_slot() and execute_single_instruction() — and
    // all three take the scope. A FOURTH must take it too, or watchpoints
    // would be silently dead on that path.
    //
    // NOT enforced by an assert: CMakeLists.txt forces RelWithDebInfo when no
    // build type is given, so NDEBUG is defined in build/ and gui-release
    // alike and an assert here would never execute in any configuration this
    // project builds — a claim with nothing behind it. It is enforced by the
    // tests instead: debugger_inspect_watchpoint_test's control rows fire a
    // watchpoint through EACH of the three roots (INSPW-10/11/12 run_frame,
    // INSPW-13 step_frame_slot, INSPW-14 execute_single_instruction), so a
    // root that loses its scope fails a named row. A capability-token
    // parameter would make it a compile error instead, and was declined only
    // because it puts an argument on this call for every instruction the
    // machine executes.
    uint64_t master_cycles;
    bool dma_stalled_cpu_this_step = false;
    bool cpu_executed = false;   // GH #265 follow-up (finding 4) — see below
    // GH #265 — per-slot state of the in-instruction device sync and the
    // end-of-slot IM2 tick; only the instruction branch below sets them.
    io_sync_valid_        = false;
    tap_sync_valid_       = false;
    slot_ran_instruction_ = false;
    slot_m1_count_        = 0;

    // GH #102 session 4 — resample the LIVE im2_.dma_delay() latch every
    // instruction, not once per video frame. VHDL zxnext.vhd:2001-2010
    // drives im2_dma_delay from a `rising_edge(i_CLK_CPU)` process — it
    // updates every CPU clock, and Im2Controller::step_dma_delay() (called
    // from im2_.tick(), itself called once per instruction below) already
    // tracks that value with per-instruction freshness. Before this fix,
    // only begin_new_frame() ever called dma_.set_dma_delay(im2_.dma_delay()),
    // once per ~70000+ T-state video frame; the GH #102 fix (2e16105f) that
    // let the CPU keep running during a deferred ENABLE made that staleness
    // directly observable for the first time: a genuinely brief real-
    // hardware defer (im2_dma_int clearing within a handful of T-states
    // once the pending device is serviced) instead persisted for up to a
    // full video frame in jnext, because dma_delay_ could not refresh mid-
    // frame. TX-1696's own interrupt-driven, per-frame "DMA memory-copy via
    // HL/DE/BC" utility runs roughly every ~2500 T-states — hundreds of
    // times inside a single video frame — so an artificially-elongated
    // defer let vastly more foreground/interrupt-driven code execute than
    // real hardware ever would while that specific transfer was nominally
    // "about to start", desynchronising a `JP (HL)` jump-table dispatch
    // from a still-pending DMA-driven update and landing PC in
    // uninitialised data — decoded by the CPU as a wild RST 38, which
    // wrote NR 0x02 bit 0 (soft reset) and rebooted to the NextZXOS
    // Welcome screen. This was flagged as a known, deliberate limitation
    // when the once-per-frame sample was introduced (`doc/design/
    // TASK3-CTC-INTERRUPTS-SKIP-REDUCTION-PLAN.md` "Resolved open
    // question — DMA delay granularity (Q4, 2026-04-21)": "If a concrete
    // regression or new software later demonstrates a divergence, we
    // upgrade to per-tick in a targeted follow-up") — TX-1696 is that
    // regression. begin_new_frame()'s once-per-frame call is now
    // superseded (this per-instruction call runs first in every frame
    // too) and has been removed.
    dma_.set_dma_delay(im2_.dma_delay());

    if (dma_.state() == Dma::State::TRANSFERRING) {
        // Progress the DMA (and its arbitration FSM, dma.cpp
        // tick_arbitration()) every step a transfer is enabled, regardless
        // of whether the bus has actually been granted yet — mirrors real
        // hardware, where BUSRQ can be asserted for multiple cycles before
        // BUSAK is granted.
        int transferred = dma_.execute_burst(16);

        // GH #102 fix (was: `if (dma_.is_active())` gating the whole
        // branch). VHDL zxnext.vhd `dma_holds_bus <= '1' when
        // z80_busak_n = '0'` — the CPU is bus-stalled ONLY once the
        // arbitration handshake has genuinely GRANTED the bus
        // (Dma::dma_holds_bus(), dma.cpp:90-96), not merely because a
        // transfer was enabled: `state_==TRANSFERRING` can sit deferred in
        // Phase::START_DMA indefinitely (dma_delay_/daisy_busy_/
        // bus_busreq_n_ gate, dma.vhd:267-269 — tested by dma_test.cpp
        // 15.4-15.6/20.1, which assert `cpu_busreq_n()==true` for exactly
        // this case). `is_active()` returned true across BOTH cases,
        // unconditionally taking this fast path and skipping the CPU
        // instruction below — which also skips im2_.tick() further down
        // (only reachable from the normal-instruction branch), so the IM2
        // device state machine that can clear the `im2_dma_delay_` latch's
        // own OR-term (`dma_int_pending()`, im2.cpp step_dma_delay()) never
        // runs either: a permanent deadlock the CPU can never resolve
        // because it is never allowed to execute the interrupt-service
        // routine that would resolve it. Root-caused via TX-1696 (GH
        // #102) — see doc/issues/g46b-102-tx1696-freeze-session2.md
        // session 3 for the full trace (CTC0's IM2 device stuck at S_REQ,
        // `im2_dma_delay_latched_` permanently true, DMA counter frozen at
        // its post-LOAD value forever). While merely deferred the CPU (and
        // im2_.tick()/CTC/UART ticking) must keep running normally below —
        // that is the only way a dma_delay_-gated defer can ever clear.
        //
        // `transferred > 0` covers the common case where the WHOLE burst
        // (up to 16 bytes) completes within this single execute_burst()
        // call: the loop's own end-of-block handling already released the
        // bus (state_=IDLE, cpu_busreq_n_=true) by the time control
        // returns here, so dma_holds_bus() alone would read false even
        // though the DMA genuinely held the bus for `transferred` bytes
        // this step (dma_test.cpp 23.1-23.3 pin the resulting T-state
        // cost). dma_holds_bus() alone still covers the two zero-byte
        // "held but stalled" cases current tests already pin: continuous/
        // byte-mode prescaler wait (in_waiting_cycles_, mode_ != 2 —
        // cpu_busreq_n_ untouched, bus stays held) and the very first
        // granted-but-empty-block edge case.
        if (transferred > 0 || dma_.dma_holds_bus()) {
            // Execute a burst of transfers; each byte ≈ 2 T-states.
            // GH #106 — add the 28 MHz SRAM read wait accumulated by the
            // burst's source memory reads (zxnext.vhd:3171-3181 via the
            // dma_.read_mem_wait_tstates lambda in init(); +1 T-state per
            // waiting read, 0 at cpu_speed != 3).
            master_cycles = (static_cast<uint64_t>(transferred) * 2
                             + dma_.last_burst_read_wait_tstates())
                            * clock_.cpu_divisor();
            if (master_cycles == 0) master_cycles = clock_.cpu_divisor();  // minimum advance
            dma_stalled_cpu_this_step = true;
        }
    }

    if (dma_stalled_cpu_this_step) {
        // master_cycles already computed above.
    } else if (cpu_parked_) {
        // GH #164 — CPU parked indefinitely by a load-only NEX (header
        // PC = 0). Same treatment as the G156 hold below — no instruction
        // fetch/execute, clock advanced in NOP-sized steps so rendering,
        // audio and the scheduler keep running and the loaded screen is
        // composited every frame — but with no countdown: nothing the NEX
        // did not ask for may ever run. See Emulator::set_cpu_parked().
        master_cycles = 4ULL * clock_.cpu_divisor();
    } else if (boot_hold_frames_remaining_ > 0) {
        // G156 — NEX loading_delay/start_delay hold: no CPU instruction
        // is fetched or executed. Advance the clock in small NOP-sized
        // steps so scanline rendering, the scheduler and audio still
        // run every frame — VRAM already holds its final post-load
        // state (bank data + any loading-bar marks), so it is composed
        // into the framebuffer exactly like any other frame. NOTE: on
        // a bare `--load file.nex` (CLI or GUI File>Load, no prior
        // NextZXOS boot context) the loading bar is NOT visible on
        // screen — nexload.asm never enables Layer 2 itself (NR 0x69
        // bit 7); on real hardware the bar is only seen because
        // whatever screen was showing before nexload.asm ran (e.g. the
        // NextZXOS file browser) already had Layer 2 on. The bytes
        // written by NexLoader::render_progress_mark() ARE correct;
        // whether they are visible depends on that pre-existing state,
        // same as real hardware.
        //
        // This branch skips this iteration's im2_.tick() / CTC / UART
        // ticking and interrupt-line polling below (same treatment the
        // pre-existing DMA-stall branch above already gets) — any
        // interrupt source that would become due during a held frame
        // is not serviced until the hold ends. This is believed to
        // match real hardware for the default (preserve_regs=0) path,
        // where nexload.asm's own unconditional `di` at its very entry
        // (nexload.asm:250, BEFORE the header — hence before
        // preserve_regs is even known) governs interrupts for the
        // *entire* loader routine, regardless of preserve_regs — real
        // nexload never re-enables interrupts before jumping to the
        // loaded PC. CAVEAT: jnext's NexLoader is a C++ shortcut that
        // does not execute nexload.asm's code, so for preserve_regs=1
        // (register-preserving chain-load) it does not independently
        // re-derive "IFF1=0 for the loader's own duration" — if the
        // live emulator's IFF1 happens to be 1 when the hold begins
        // (e.g. mid-session chain-load from a running, interrupt-
        // enabled program), no interrupts fire during the held frames
        // here either. Believed harmless (matches the DI-throughout
        // real-hardware behaviour above) but not independently proven
        // for that specific combination — flagged rather than assumed.
        // boot_hold_frames_remaining_ is decremented once per
        // completed frame, at the end of run_frame().
        master_cycles = 4ULL * clock_.cpu_divisor();
    } else {
        // Record trace entry before execution (captures pre-execution state).
        // Enabled during replay so consecutive step-backs can look up target cycles.
        if (trace_log_.enabled()) {
            // The trace record reads FOUR opcode bytes at PC, and
            // z80_instruction_length() reads up to four more, whatever the
            // instruction's real length is. For a one-byte NOP the CPU fetches
            // exactly one of them, so PC+1..PC+3 are the DEBUGGER's reads, not
            // the machine's, and a READ watchpoint on a data byte sitting
            // after a short instruction was fired by the trace log alone.
            // Which made it the user's problem precisely when the debugger was
            // open, since that (and --rewind) is what enables the trace.
            DebugState::InspectionScope inspect(debug_state_);
            const Z80Registers& regs = cpu_.registers();
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
            // Captureless lambda decays to a raw function pointer — no
            // per-instruction std::function construction (Task 27 A2).
            te.opcode_len = z80_instruction_length(
                regs.PC,
                [](void* ctx, uint16_t a) {
                    return static_cast<Mmu*>(ctx)->read(a);
                },
                &mmu_);
            trace_log_.record(te);
        }
        // P0 boot probe trap detector: when the CPU enters the
        // post-soft-reset $0000/$0001 spin, dump the trace ring while
        // pre-trap history is still in it, then exit. Env-gated.
        {
            static const char* trap_dump =
                std::getenv("JNEXT_BOOT_PROBE_TRAP_DUMP");
            if (trap_dump) {
                static int pc0_run = 0;
                const uint16_t cur_pc = cpu_.pc();
                if (cur_pc <= 0x0001) {
                    if (++pc0_run == 200) {
                        Log::emulator()->info(
                            "BOOT_PROBE trap detected (200 consecutive "
                            "PC<=1 instructions); dumping trace to {}",
                            trap_dump);
                        trace_log_.export_to_file(trap_dump);
                        std::exit(0);
                    }
                } else {
                    pc0_run = 0;
                }
            }
        }
        // Call stack tracking (debugger only, gated by enabled flag).
        if (call_stack_.enabled()) {
            // Same shape as the trace record above: three bytes are
            // read at PC whatever the instruction's length, so PC+1 and PC+2
            // are the debugger's reads and must not fire the user's
            // watchpoints. The call-stack tracker is switched on by the
            // debugger, so this too only ever hurt the user who had it open.
            DebugState::InspectionScope inspect(debug_state_);
            const Z80Registers& regs2 = cpu_.registers();
            uint8_t op0 = mmu_.read(regs2.PC);
            uint8_t op1 = mmu_.read(regs2.PC + 1);
            uint8_t op2 = mmu_.read(regs2.PC + 2);
            call_stack_.on_instruction_pre(regs2.PC, regs2.SP, op0, op1, op2);
        }

        // GH #203 — Step Out. Nothing is READ here, deliberately: only SP is
        // sampled. The opcode bytes are read after execute() and only once the
        // SP test has already passed — see the decision site below for why.
        const bool step_out_armed =
            debug_state_.active() && debug_state_.step_mode() == StepMode::OUT;
        const uint16_t step_out_sp_before =
            step_out_armed ? cpu_.registers().SP : 0;

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
        // Task 21 — capture the PC of the instruction we're about to
        // execute so the profiler can attribute the T-state cost to
        // the right address. Reading regs.PC is O(1) (register-pair
        // copy from a member array) — the cost is in the noise next
        // to the surrounding scheduler/IM2/Copper work.
        const uint16_t pc_pre_exec = cpu_.pc();
        multiface_retn_pending_ = false;
        int tstates = cpu_.execute();
        cpu_executed = true;

        // GH #203 — Step Out decision. It sits HERE, between execute() and the
        // RETN overlay clear below, and the order of the two halves matters as
        // much as the position:
        //
        //  * NOTHING IS READ unless the CPU itself read it. execute() has
        //    three early returns that complete a step without fetching the
        //    opcode at PC — NMI, accepted INT, and the esxdos shim
        //    (z80_cpu.cpp:639, :723, :813). Reading the opcode speculatively
        //    BEFORE execute() tripped a READ watchpoint on a byte the CPU
        //    never read in those slots, and the data-breakpoint branch in
        //    run_frame() then ENDED the step in the wrong place — a wrong
        //    stop, not the "keeps running" degradation this feature promises.
        //    So the CPU is ASKED (fetched_opcode_last_execute()) rather than
        //    having the answer inferred from SP arithmetic: the shim fakes a
        //    return's own SP delta (`r.SP = sp + 2`, z80_cpu.cpp:807), so an
        //    SP-shape test cannot see it. Both gaps came from the independent
        //    review of GH #203, the second one after an SP-shape test had
        //    already "fixed" the first — which is why the gate is now the
        //    direct signal and closes the whole class, including any future
        //    host-side shim wired into on_esxdos_call.
        //
        //  * BEFORE divmmc_.on_retn_instruction_complete(): a RETN leaving a
        //    DivMMC-mapped routine — the esxdos idiom — clears the overlay
        //    there, and a read taken after that point would see the underlying
        //    page instead of the ED 45 the CPU actually fetched. No return
        //    form writes a port or an NR, so the mapping here is still the one
        //    the M1 fetch used.
        //
        // PC+1 is read only behind an 0xED prefix, which is the only case the
        // CPU fetches a second M1 byte; reading it unconditionally would, for
        // a one-byte instruction at 0x3FFF, leave p3_floating_bus_dat_ holding
        // mem[0x4000] (mmu.h:393, VHDL zxnext.vhd:4498-4509).
        if (step_out_armed && cpu_.fetched_opcode_last_execute() &&
            debug_state_.step_out_sp_qualifies(step_out_sp_before,
                                               cpu_.registers().SP)) {
            const uint8_t op0 = mmu_.read(pc_pre_exec);
            const uint8_t op1 = (op0 == 0xED)
                ? mmu_.read(static_cast<uint16_t>(pc_pre_exec + 1)) : 0;
            if (debug_state_.check_step_out(step_out_sp_before,
                                            cpu_.registers().SP, op0, op1)) {
                debug_state_.pause();
            }
        }

        // RETN overlay timing — on real hardware the RETN opcode has
        // already been fetched when i_retn_seen clears the mapping
        // latches. JNext predecodes instructions before the CPU core
        // executes them, so clearing either overlay from the M1 callback
        // is too early for RETN itself, while waiting for the next M1 is
        // too late for the returned-to instruction's predecode. Apply
        // the latched clears at this instruction boundary instead.
        divmmc_.on_retn_instruction_complete();
        if (multiface_retn_pending_) {
            multiface_.on_retn_seen();
            multiface_retn_pending_ = false;
        }

        defer_cpu_nr_writes_ = false;
        if (profiler_.active()) {
            profiler_.on_instruction(mmu_, pc_pre_exec,
                                     static_cast<uint64_t>(tstates));
        }

        // Advance VideoTiming so test/debug observers see the post-
        // instruction raster position. The contention path itself
        // does NOT consult VideoTiming (it derives hc/vc from
        // tstates directly to keep per-bus-cycle precision); this
        // advance is purely the debug observable.
        //
        // Task 27 C10: gate it behind an attached debugger. No
        // production path reads the counters advance() mutates
        // (hc_/vc_/frame_done_/pulse counts): the compositor and
        // scheduler derive raster from the clock, the debugger's
        // HC/VC readout uses clock-derived paused_vc_/paused_hc_
        // (snapshot_raster()), and NR 0x1E/0x1F likewise. Only a
        // human inspecting emulator.video_timing().pos() in the
        // debugger could observe it, so the free-running production
        // hot loop (headless/GUI, debug_state_ inactive) skips the
        // per-instruction raster walk entirely.
        if (debug_state_.active())
            video_timing_.advance(tstates);

        // Call stack tracking post-execution.
        if (call_stack_.enabled()) {
            const Z80Registers& post = cpu_.registers();
            call_stack_.on_instruction_post(post.SP, post.PC);
        }

        // GH #203 — the Step Out decision was made just after cpu_.execute()
        // above (it must precede the RETN overlay clear). Both this shared
        // body's callers reach it, so run_frame()'s free-running loop and
        // execute_single_instruction() cannot disagree about where a step out
        // ends. pause() clears step_mode_ back to NONE, which consumes the
        // step; run_frame()'s loop head sees paused() on its next iteration
        // and returns, after this instruction's devices have been ticked.

        // Convert T-states to 28 MHz master cycles for the master-clock
        // advance and the CTC / UART / md6 / nmi device ticks below.
        master_cycles = static_cast<uint64_t>(tstates) * clock_.cpu_divisor();

        // GH #265 — the IM2 fabric is no longer ticked here but at the end of
        // the slot (finish_slot_interrupts()), after the devices and the
        // scheduler have raised this instruction's requests. Record what that
        // tick needs. The NMI-activated sample is taken HERE, before this
        // slot's NmiSource tick, exactly where it was read before (Wave E:
        // VHDL :2007 im2_dma_delay and nmi_activated settle on the same CPU
        // edge, so the latch sees the previous tick's value).
        slot_ran_instruction_ = true;
        slot_tstates_         = static_cast<uint32_t>(tstates);
        slot_start_           = clock_.get();
        slot_d_               = static_cast<uint32_t>(clock_.cpu_divisor());
        slot_nmi_activated_   = nmi_source_.is_activated();

        // Count instructions for RZX recording.
        if (rzx_recorder_.is_recording()) ++rzx_frame_instruction_count_;

        // Tick real-time tape playback (advances EAR bit state machine).
        if (tape_.is_playing()) {
            // GH #265 — minus what a port 0xFE read inside the instruction
            // already advanced it by (tap_ear_for_port_read()).
            const uint32_t done =
                (tap_sync_valid_ && tap_sync_serial_ == cpu_.execute_serial())
                    ? tap_sync_ts_ : 0u;
            tape_.tick_realtime(static_cast<uint64_t>(tstates) - done);
            beeper_.set_tape_ear(tape_.tick_realtime(0) != 0);
        } else if (tzx_tape_.is_playing()) {
            // TZX real-time: ZOT models an absolute tape timeline, so
            // it must see the monotonic clock, not the frame-relative
            // FUSE counter (G36).
            beeper_.set_tape_ear(tzx_tape_.update(monotonic_tstates()) != 0);
        } else if (wav_tape_.is_playing()) {
            beeper_.set_tape_ear(wav_tape_.get_ear_bit(monotonic_tstates()) != 0);
        } else {
            beeper_.set_tape_ear(false);
        }
    }
    clock_.tick(master_cycles);
    // GH #265 follow-up (finding 4): execute() advanced the FUSE counter
    // itself; the DMA / parked / boot-hold steps advance only the clock, so
    // move the counter with it or contention would lag the raster.
    if (!cpu_executed) advance_fuse_tstates_(master_cycles);

    // Tick DMA burst prescaler (counts down between burst-mode transfers).
    dma_.tick_burst_wait(master_cycles);

    return master_cycles;
}

void Emulator::finish_slot_interrupts()
{
    if (!slot_ran_instruction_) return;
    slot_ran_instruction_ = false;

    // Nearly every instruction: pulse mode, nothing pending in the fabric,
    // the pulse high (Im2Controller::slot_idle()) — all the full path below
    // would do is withdraw a leftover IM2-mode request and note the pulse
    // as high, so do exactly that.
    if (im2_.slot_idle(slot_nmi_activated_)) {
        if (cpu_.int_pending() && cpu_.int_window_last_ts() == INT64_MAX)
            cpu_.cancel_interrupt();
        prev_pulse_int_n_ = true;
        return;
    }
    finish_slot_interrupts_full();
}

// Kept out of line so the fast path above — run for nearly every
// instruction — does not pay this path's stack frame.
[[gnu::noinline]] void Emulator::finish_slot_interrupts_full()
{
    // Advance the IM2 controller one tick per instruction. The device
    // state-machine transitions (S_0→S_REQ→S_ACK→S_ISR→S_0) react to
    // per-M1 signals and are modeled one-per-instruction (coarse, by
    // design — see Im2Controller::step_devices()). The T-state count is the
    // pulse fabric's CPU-edge count (Task 60d: zxnext.vhd:2035-2044 counts
    // i_CLK_CPU rising edges, not 28 MHz master cycles).
    //
    // GH #265 — this runs once the slot's devices (CTC, UART) and the
    // frame/line interrupt events have raised their requests, each stamped
    // with the CLK_28 edge it happened on, and the timed tick resolves them
    // on that timeline. It used to run straight after the CPU instruction,
    // BEFORE the devices were ticked for it, so every request raised during
    // an instruction reached the fabric one instruction late — and the CPU
    // could only take it after the instruction after that.
    const uint32_t d   = slot_d_;
    const uint64_t now = clock_.get();
    im2_.set_nmi_activated(slot_nmi_activated_);
    im2_.tick(slot_tstates_, slot_start_, now, d, slot_m1_count_);
    // A CPU-speed change committed at this boundary: a pulse this tick
    // started is on the old divisor's edges (Im2Controller::set_cpu_divisor()).
    const uint32_t dn = static_cast<uint32_t>(clock_.cpu_divisor());
    im2_.set_cpu_divisor(now, dn);

    // /INT windows on the FUSE T-state counter, which stands at `now`. The
    // T80 samples INT_n into INT_s on every CPU rising edge and decides at
    // the edge ending an instruction with the INT_s taken on the edge
    // before, at the start of the instruction's last T-state (t80n.vhd:
    // 1664, 1742-1772): a line first sampled low on CPU edge E is
    // taken at a boundary B >= E + d.
    // From here on the CPU edges are those of the divisor now in force, dn:
    // a CPU-speed change committed at this boundary has already re-based the
    // counter (rebase_fuse_tstates_()).
    const int64_t now_ts = static_cast<int64_t>(*fuse_z80_tstates_ptr());
    auto ts_at = [&](uint64_t edge) -> int64_t {
        return now_ts + (static_cast<int64_t>(edge) - static_cast<int64_t>(now))
                        / static_cast<int64_t>(dn);
    };

    // V19-IM2-04: the IM2 fabric's line. VHDL zxnext.vhd:1840 —
    // `z80_int_n <= ((pulse_int_n AND im2_int_n) OR NOT expbus_disable_int)
    // AND ...` — the Z80 /INT pin is the AND of pulse_int_n and im2_int_n.
    // A device's o_int_n is low from the CPU edge it entered S_REQ on
    // (im2_device.vhd:150), so INT_s is set one edge later and the request
    // is taken from the boundary after that. It stays low until the IntAck
    // moves the device on, so the window is open-ended; if the line has
    // gone high without an acknowledge (NR 0xC0 back to pulse mode, the CPU
    // leaving IM 2), the request it made is withdrawn — the line is a
    // level. The vector is supplied by on_int_ack → ack_vector()
    // (`nr_c0_im2_vector & im2_vec & '0'`, zxnext.vhd:1999).
    if (im2_.is_im2_mode() && im2_.int_line_asserted()) {
        const uint64_t since = im2_.int_line_low_since();
        const int64_t first = (since == Im2Controller::kNoTime)
            ? now_ts : ts_at(since + 2ull * dn);
        cpu_.request_interrupt(0xFE, first, INT64_MAX);
    } else if (cpu_.int_pending() && cpu_.int_window_last_ts() == INT64_MAX) {
        cpu_.cancel_interrupt();
    }

    // V20-IM2-01: the pulse. pulse_int_n drops for a qualified request in
    // pulse mode (im2_peripheral.vhd:186-194), or for the ULA — the one
    // "exception" device — in IM2 mode while the CPU is not in IM 2
    // (:192). Either way it reaches the pin through the same AND, so it is
    // requested in both modes. INT_s is set on the pulse's CPU edges
    // E_1..E_N (Im2Controller::pulse_first_edge/last_edge): the request is
    // taken at a boundary in [E_1 + d, E_N + d]. Requested once per pulse:
    // on the tick that started it (take_pulse_started(), which also sees a
    // pulse that ended and restarted inside one slot), or on a falling edge
    // of pulse_int_n between ticks (a pulse restored by load_state()). The
    // vector 0xFF is the floating-bus byte of a pulse-mode IntAck.
    const bool started = im2_.take_pulse_started();
    const bool cur_pulse_int_n = im2_.pulse_int_n();
    if (started || (!cur_pulse_int_n && prev_pulse_int_n_)) {
        cpu_.request_interrupt(0xFF,
                               ts_at(im2_.pulse_first_edge() + dn),
                               ts_at(im2_.pulse_last_edge() + dn));
    }
    prev_pulse_int_n_ = cur_pulse_int_n;
}

void Emulator::tick_devices_after_instruction(uint64_t master_cycles)
{
    // Task 60a — shared post-instruction device-tick cluster; see the
    // declaration comment in emulator.h. Shared by run_frame() (after
    // its data-breakpoint early-return check) and
    // execute_single_instruction().
    //
    // Audio timing constant: PSG clock = 28 MHz / 16 = 1.75 MHz → one
    // PSG tick every 16 master cycles.
    static constexpr uint64_t PSG_DIVISOR = 16;

    // Execute the Copper at 28 MHz granularity over the master-cycle
    // window covered by this CPU instruction (G117), stopping at each
    // video-row boundary inside that window so the row a write belongs
    // to is decided by the write's own master cycle and not by where
    // the CPU's instruction boundaries happen to fall (GH #272).
    // Pre-G117 this path stepped the Copper exactly once per Z80
    // instruction, so dense Copper bursts (tilemap-class effects with
    // 32 MOVEs per scanline) collapsed into 1-2 effective writes per
    // scanline.
    advance_copper_across_row_boundaries(master_cycles);

    // G65 — drain the CPU NR writes deferred during the instruction that
    // the row-boundary walk above has not already committed (all of them
    // when no boundary fell inside the window, which is the usual case).
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
    const int divisor_before = clock_.cpu_divisor();
    clock_.commit_pending_cpu_speed_on_bus_idle(bus_idle, dma_holds_bus);
    contention_.commit_pending_cpu_speed_on_bus_idle(bus_idle, dma_holds_bus);
    // GH #265 follow-up (finding 4): the FUSE counter counts CPU T-states,
    // so its unit changes with the divisor; re-derive it from the clock.
    if (clock_.cpu_divisor() != divisor_before) {
        rebase_fuse_tstates_();
        im2_.set_cpu_divisor(clock_.get(), static_cast<uint32_t>(clock_.cpu_divisor()));
    }

    // Tick CTC and UART at 28 MHz rate — GH #265: only the part of the
    // instruction a read or write inside it has not already ticked them
    // through (sync_io_devices_to()), so every edge is ticked exactly once.
    {
        const uint64_t slot_begin = clock_.get() - master_cycles;
        uint64_t ticked = slot_begin;
        if (io_sync_valid_ && io_sync_edge_ > slot_begin
                && io_sync_edge_ <= clock_.get()) {
            ticked = io_sync_edge_;
        }
        io_sync_valid_ = false;
        const uint32_t rest = static_cast<uint32_t>(clock_.get() - ticked);
        ctc_.set_time(ticked);
        ctc_.tick(rest);
        uart_.set_time(ticked);
        uart_.tick(rest);
        md6_.tick(rest);
    }

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
    iomode_.set_uart0_tx(uart_.channel(0).tx_line_out());
    iomode_.set_uart1_tx(uart_.channel(1).tx_line_out());

    // GH #90 (G72 residual) — feed the joystick line-5 injectors from the
    // live connector vectors, closing the RX half of the iomode mux.
    // VHDL zxnext.vhd:3538:
    //   joy_uart_rx <= ((not nr_0b_joy_iomode(0)) and not i_JOY_LEFT(5))
    //               or (     nr_0b_joy_iomode(0)  and not i_JOY_RIGHT(5));
    // i_JOY_*(5) is button C, ACTIVE HIGH (zxnext.vhd:90-91), and the
    // Joystick raw 12-bit vectors are the direct i_JOY_* model
    // (zxnext.vhd:3441-3442). The VHDL assign is combinational and not
    // gated by NR 0x05/0x0B — gating lives in the consumers at
    // zxnext.vhd:3340-3341 — so the feed is unconditional, mirroring the
    // UART TX feed above.
    iomode_.set_joy_left_bit5(joystick_.joy_left_bit5());
    iomode_.set_joy_right_bit5(joystick_.joy_right_bit5());

    // GH #251 — clock the host-side joystick serial source, if one is attached.
    // Guarded here (not inside the callee) so an ordinary run pays a single
    // predicted-not-taken branch, following the `device_attached_` precedent
    // documented in peripheral/uart.h.
    if (joy_uart_source_) service_joy_uart_source(master_cycles);
    if (joy_uart_link_)   service_joy_uart_link(master_cycles);

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
    //
    // Verify3-Audit fix (2026-05-09): VHDL divmmc.vhd:107-114 gates
    // the FF set on `i_reset='0' AND i_automap_reset='0' AND
    // i_retn_seen='0'`. NmiSource doesn't gate on
    // `nr_0a_divmmc_automap_en` (= the second factor of
    // divmmc_automap_reset, zxnext.vhd:4112), so the strobe can fire
    // even when the divmmc module would suppress the latch. Mirror
    // the VHDL gate here by checking `divmmc_.is_enabled()` (the
    // composite of port_io AND nr_0a_4 — exactly what VHDL's
    // `divmmc_automap_reset='0'` requires).
    if (nmi_source_.divmmc_button_strobe() && divmmc_.is_enabled()) {
        divmmc_.set_button_nmi(true);
    }

    // Verify-audit fix — VHDL:2169 `nmi_mf_button` arbitration-strobe
    // producer (companion to :2170). VHDL line 4290 wires the
    // Multiface entity's `button_i` to `nmi_mf_button` (= `nmi_mf AND
    // nmi_state=S_NMI_IDLE`), so the Multiface only sees the press
    // AFTER NmiSource has arbitrated the request through NR 0x06 bit
    // 3, port_e3_reg(7) (CONMEM), divmmc_nmi_hold, and
    // nr_03_config_mode. We mirror that contract here.
    if (nmi_source_.mf_button_strobe()) {
        multiface_.button_press();
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

    // Task 47 (--silent): skip PSG ticking and mixer synthesis entirely —
    // nothing reads their output (no audio device is opened, and the
    // ring buffer is never drained), so doing the work is pure waste.
    // Register writes to the AY/DAC/beeper ports above are unaffected;
    // only the oscillator/mixer work that turns them into samples is
    // skipped. See EmulatorConfig::silent doc comment.
    if (!config_.silent) {
        // Tick PSG (TurboSound) at 1.75 MHz rate.
        psg_accum_ += master_cycles;
        while (psg_accum_ >= PSG_DIVISOR) {
            psg_accum_ -= PSG_DIVISOR;
            turbosound_.tick();
        }

        // Feed this instruction's span to the mixer, which INTEGRATES the source
        // levels across each output-sample interval rather than point-sampling
        // them. Suppressed in replay mode (fast-forward rewind path).
        if (!replay_mode_) {
            advance_audio(master_cycles);
        }
    }

    // Drain any scheduler events that have become due.
    run_scheduled_until(clock_.get());

    // GH #265 — the IM2 fabric sees this instruction's requests now, at its
    // end, instead of one instruction later.
    finish_slot_interrupts();
}

uint64_t Emulator::step_frame_slot()
{
    // GH #207 — one instruction slot WITH the frame bookkeeping run_frame()
    // does around its own loop: begin a frame when none is in flight, end it
    // when the clock reaches the frame's last cycle. Without those two seams
    // the clock simply runs past the end of the frame it is in, forever, and
    // everything scheduled per frame — begin_new_frame()'s ULA frame interrupt
    // (zxula_timing.vhd:551), the line interrupt, and the per-scanline
    // SCANLINE/VSYNC events — is never scheduled again.
    //
    // Called only from debugger_step(), which is the machine's sole driver
    // while the debugger holds it. See that function for why the frame loop
    // does NOT belong to execute_single_instruction().
    //
    // The third and last place the machine executes. Guarding here
    // rather than in debugger_step() keeps the invariant stated at
    // step_one_instruction() true of EVERY caller of it.
    DebugState::GuestExecutionScope guest_exec(debug_state_);

    if (!frame_in_progress_) {
        begin_new_frame();
        frame_in_progress_ = true;
    }
    const uint64_t frame_end = frame_cycle_ + timing_.master_cycles_per_frame;

    const uint64_t master_cycles = step_one_instruction();
    tick_devices_after_instruction(master_cycles);

    if (clock_.get() >= frame_end) {
        end_of_frame(frame_end);
    }
    return master_cycles;
}

int Emulator::execute_single_instruction()
{
    // Task 60a — delegate to the SAME shared per-instruction body that
    // run_frame()'s inner loop uses. This function used to be a hand-
    // maintained copy of that cluster and had drifted: it never called
    // im2_.tick(), never polled the IM2-mode / pulse-mode /INT lines
    // (so stepping through interrupt-driven code never delivered
    // CTC/UART/ULA-frame interrupts), never ticked md6_, and never
    // recorded trace entries. Sharing the body makes that drift
    // structurally impossible.
    //
    // This is the RAW one-slot primitive: exactly one instruction slot and
    // nothing around it. run_frame()'s per-instruction surroundings
    // (breakpoint checks, tape ROM traps, the data-breakpoint early return)
    // and its per-FRAME surroundings both stay outside. The debugger's Step
    // is debugger_step(), which adds the frame loop.
    //
    // GH #164 — see resume_from_park(). The shared body below honours
    // cpu_parked_, so on a parked machine every Step would be a silent
    // no-op and the debugger would look broken. A Step is the most
    // explicit request to advance the CPU there is, so it resumes. No
    // effect on any caller that is not parked, which is every test.
    resume_from_park("the debugger stepped a single instruction");

    // One of the three places the machine executes. See run_frame().
    DebugState::GuestExecutionScope guest_exec(debug_state_);

    // It does NOT consume data_bp_hit_, and that is safe only because this
    // function has no production caller: every caller in src/ is this
    // definition and its declaration, debugger_step() deliberately does not
    // route through it (GH #207, see there), and everything else under test/
    // drives it directly and reads the latch itself. A watchpoint that fires
    // here therefore leaves the latch standing for whoever looks next.
    //
    // If this ever DOES get a production caller that runs with watchpoints
    // armed, it must consume the latch — pause() + set_data_bp_hit(false), as
    // run_frame() and debugger_step() both do — or that caller's next resume
    // stops one instruction in, at an address the watchpoint has nothing to do
    // with. That is exactly the defect DebugState::GuestExecutionScope exists
    // to remove, reintroduced from the other end.
    const uint64_t master_cycles = step_one_instruction();
    tick_devices_after_instruction(master_cycles);
    return static_cast<int>(master_cycles / clock_.cpu_divisor());
}

int Emulator::debugger_step()
{
    // GH #207 — the debugger's Step, as distinct from the raw one-slot
    // primitive above. Two things separate them, and both come from the same
    // fact: while the debugger holds the machine the frontends stop calling
    // run_frame() altogether (src/platform/frame_sequencer.h — `if
    // (!fx.paused())`), so this call is the machine's ONLY driver and inherits
    // run_frame()'s job, not just its inner loop.
    //
    //   1. It runs the frame loop (step_frame_slot()). Without it the clock
    //      runs past the end of the frame it is in and no frame ever begins
    //      again, so nothing scheduled per frame — the ULA frame interrupt
    //      above all — can ever fire for the rest of the session.
    //
    //   2. A Step issued while the CPU is HALTed runs the halt out.
    //      HALT is not an instruction that completes: VHDL t80n.vhd:496
    //      freezes PC while Halt_FF is set and :502-503 forces IR to 0x00, so
    //      the core keeps issuing M1 NOP fetches at the same address, and
    //      t80n.vhd:1727 clears Halt_FF only on an accepted interrupt or NMI
    //      cycle. Stepping one of those internal NOPs is observably nothing:
    //      PC does not move, no register changes, and the user would have to
    //      press Step once per 4 T-states — 17472 times at 3.5 MHz, 139776 at
    //      28 MHz — before the frame interrupt that ends the wait arrives.
    //      That is the whole of GH #207. So the step's unit of progress here
    //      is the halt, exactly as Step Over's unit is the whole CALL.
    //      Nothing is faked or skipped: every one of those NOP slots is
    //      emulated through the same body as any other instruction, devices
    //      and all — the loop just does not hand control back until the CPU
    //      has actually left the halt state.
    //
    // Keeping both out of execute_single_instruction() is deliberate. That
    // function is used all over the test tree to advance a machine by exactly
    // one slot while the test drives its own frames, and several suites depend
    // on it not touching frame state at all — videotiming_test's G163
    // line-interrupt rows ("ULA-int still enabled but not scheduled because
    // run_frame was never called"), lores_integration_test, and step_out_test,
    // whose hand-injected INT would be dropped by begin_new_frame()'s reset of
    // the frame-relative T-state counter the /INT pulse window is measured in.
    //
    // Returns the CPU T-states the step consumed, like the primitive.
    resume_from_park("the debugger stepped a single instruction");

    // Bounded at two frames because the ULA frame interrupt fires once per
    // frame (zxula_timing.vhd:551) and the current frame may already be past
    // its firing point, so two frames always contain at least one. When the
    // budget runs out the CPU is still halted and the step reports no
    // progress — the honest answer for `DI : HALT`, which on real hardware
    // waits for an NMI or a reset and nothing else.
    const bool halted_before = cpu_.is_halted();
    uint64_t master_cycles = step_frame_slot();

    if (halted_before) {
        const uint64_t budget = 2u * timing_.master_cycles_per_frame;
        uint64_t spent = master_cycles;
        // A watchpoint that fires inside the halt (a DMA write, or a READ
        // watchpoint on the halted PC itself, which the core re-fetches every
        // slot) ends the step where it fired instead of being spent silently.
        while (cpu_.is_halted() && spent < budget && !debug_state_.data_bp_hit()) {
            const uint64_t m = step_frame_slot();
            spent += m;
            master_cycles += m;
        }
    }

    // CONSUME the data-breakpoint latch. `data_bp_hit_` is set by the MMU and
    // is NOT self-clearing: run_frame() is what consumes it, at
    // emulator.cpp:7434-7437 (`pause(); set_data_bp_hit(false);`), because
    // that is where a watchpoint ends the instruction it fired in. A step ends
    // an instruction too, so the same consume belongs here — and leaving it
    // out is not a cosmetic leak:
    //
    //   * the halt-run loop above reads the flag, so a watchpoint that fires
    //     during ANY step leaves every LATER Step with a false loop condition
    //     from its first iteration. A Step at a HALT then degrades to one
    //     4-T-state NOP slot again — PC never moves, indefinitely. That is
    //     precisely the GH #207 symptom this function exists to remove, and it
    //     would have persisted until some unrelated DebugState::resume()
    //     happened to clear the flag;
    //   * on the next Run, run_frame()'s own check fires one instruction into
    //     the resume and stops at an address the watchpoint has nothing to do
    //     with.
    //
    // Consuming it unconditionally (not only when the loop broke on it) is
    // deliberate: both leaks are the same latch, and a step that is not a
    // halt-run can set it just as easily.
    if (debug_state_.data_bp_hit()) {
        debug_state_.pause();
        debug_state_.set_data_bp_hit(false);
    }

    return static_cast<int>(master_cycles / clock_.cpu_divisor());
}

void Emulator::advance_audio(uint64_t master_cycles)
{
    // Sample generation: 28 MHz / 44100 Hz ≈ 634.92 master cycles per sample.
    // Bresenham-style accumulator: a sample completes each time accum >= MASTER_CLOCK_HZ.
    static constexpr uint64_t SAMPLE_THRESHOLD = MASTER_CLOCK_HZ;

    // Walk the slice in chunks that stop at each output-sample boundary, so the
    // mixer integrates the source levels ACROSS each sample interval instead of
    // point-sampling them at its end.
    //
    // Point-sampling was the source of the beeper "whistle": output samples are
    // ~635 master cycles (~79 T-states) apart, and a 1-bit beeper engine toggles
    // the speaker far faster than that, so most toggles were never observed at
    // all. The energy they carry does not vanish — it folds down into the
    // audible band as an inharmonic tone over the music. Averaging over the
    // interval is a box filter, and it cut jnext's energy above 6 kHz on the
    // Cesare intro from 6.2% of total to 1.8% (FUSE, which does the same, 0.5%).
    //
    // The source levels are constant between accumulate() calls (we observe the
    // machine at instruction boundaries), so weighting each level by the cycles
    // it held is exact at that granularity.
    uint64_t remaining = master_cycles;
    while (remaining > 0) {
        // Master cycles still owed before the current output sample completes.
        const uint64_t need =
            (SAMPLE_THRESHOLD - sample_accum_ + Mixer::SAMPLE_RATE - 1) / Mixer::SAMPLE_RATE;
        const uint64_t take = std::min(remaining, need);

        mixer_.accumulate(beeper_, turbosound_, dac_, static_cast<uint32_t>(take));
        sample_accum_ += take * Mixer::SAMPLE_RATE;
        remaining -= take;

        if (sample_accum_ >= SAMPLE_THRESHOLD) {
            sample_accum_ -= SAMPLE_THRESHOLD;
            mixer_.emit_sample();
        }
    }
}

// ---------------------------------------------------------------------------
// Task 79 — per-connector host input source coordination.
// ---------------------------------------------------------------------------

void Emulator::update_cursor_key_target()
{
    // The Keyboard drives at most one connector from the arrow keys. Pick the
    // connector whose source is CursorKeys (set_joystick_source guarantees at
    // most one), or -1 if neither uses cursor keys.
    int target = -1;
    for (int s = 0; s < 2; ++s) {
        if (joy_source_[s] == JoySource::CursorKeys) target = s;
    }
    keyboard_.set_cursor_key_target(target);
}

void Emulator::set_joystick_source(int connector, JoySource src)
{
    if (connector < 0 || connector > 1) return;
    const int other = connector ^ 1;
    bool changed[2] = { false, false };

    if (joy_source_[connector] != src) {
        joy_source_[connector] = src;
        changed[connector] = true;
    }
    // Mutual exclusion: at most one connector may use cursor keys.
    if (src == JoySource::CursorKeys && joy_source_[other] == JoySource::CursorKeys) {
        joy_source_[other] = JoySource::Sdl;
        changed[other] = true;
    }

    update_cursor_key_target();

    if (on_joystick_source_changed) {
        for (int s = 0; s < 2; ++s) {
            if (changed[s]) on_joystick_source_changed(s, joy_source_[s]);
        }
    }
}

void Emulator::refresh_joystick_sources()
{
    update_cursor_key_target();
    if (on_joystick_source_changed) {
        for (int s = 0; s < 2; ++s) {
            on_joystick_source_changed(s, joy_source_[s]);
        }
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
    // P0 boot probe: dump the pre-reset ring (the LAST capacity-many
    // instructions BEFORE this soft reset) — must run before init()
    // clears the trace ring. Env-gated.
    if (const char* pre_dump =
            std::getenv("JNEXT_BOOT_PROBE_DUMP_AT_RESET");
        pre_dump && trace_log_.enabled()) {
        trace_log_.export_to_file(pre_dump);
    }

    // The rewind-corruption flag (Task 60e) and the direct-NEX host state
    // (GH #29/#84/#250) are cleared by init() below, as on every
    // (re)initialisation.

    const bool reset_type_1 = (nextreg_.cached(0x85) & 0x80) != 0;
    const uint8_t save_82 = nextreg_.cached(0x82);
    const uint8_t save_83 = nextreg_.cached(0x83);
    const uint8_t save_84 = nextreg_.cached(0x84);
    // PASS-5: NR 0x86/0x87/0x88/0x89 (bus-port enable group). VHDL
    // zxnext.vhd:5061-5067 gates reset on nr_89 bit 7 = '0' (inverse
    // polarity vs nr_85). Save/restore parallels the 0x82-0x84 path.
    const bool bus_reset_type_0 = (nextreg_.cached(0x89) & 0x80) == 0;
    const uint8_t save_86 = nextreg_.cached(0x86);
    const uint8_t save_87 = nextreg_.cached(0x87);
    const uint8_t save_88 = nextreg_.cached(0x88);
    const uint8_t save_89 = nextreg_.cached(0x89);

    // VHDL bootrom_en (zxnext.vhd:1101, reset logic at :5109-5111, cleared
    // by NR 0x03 write at :5122). The reset block for bootrom_en runs on
    // BOTH hard and soft reset (the `reset` signal is `reset_hard OR
    // reset_soft`) and is guarded by `if nr_03_config_mode = '1'`:
    //   * config_mode=1 → bootrom_en <= '1' (re-enabled)
    //   * config_mode=0 → bootrom_en preserved (current FF value held)
    // nr_03_config_mode has no reset branch itself, so it holds across
    // reset. Verify5-memory pass-5 fix: this VHDL gate is now modelled
    // inside Mmu::reset() itself (mmu.cpp:131-150 reads its preserved
    // config_mode_ mirror). The earlier capture-and-restore workaround
    // here was only correct for the config_mode=0 case — for the
    // config_mode=1 case it incorrectly preserved a previously cleared
    // bootrom_en where VHDL would have re-enabled it. With Mmu::reset()
    // VHDL-faithful, no host-side capture is required.

    Log::emulator()->debug("Soft reset (NR 0x02 bit 0): preserving SRAM");

    // The framebuffer is NOT cleared: the soft reset does not touch the
    // video timing (zxula_timing.vhd has no reset input) or its output, so
    // what is on screen stays until the next frame is drawn. It used to be
    // blanked here (copied from reset()), which showed black while the
    // debugger held the machine after a reset (GH #263 follow-up).

    // Re-run init with preserve_memory=true: skip RAM/ROM reinit and the
    // SRAM-from-rom seed so the ROM window keeps whatever tbblue.fw just
    // installed. All FF-based state (CPU, MMU slots, NextReg, peripherals)
    // is reset via init()'s subsystem-reset loop — no separate pre-init
    // reset dance needed.
    init(config_, /*preserve_memory=*/true);

    // Restore port-enable registers per reset_type semantics.
    if (!reset_type_1) {
        nextreg_.write(0x82, save_82);
        nextreg_.write(0x83, save_83);
        nextreg_.write(0x84, save_84);
    }
    // PASS-5: bus-port group (0x86-0x89) survives reset when nr_89 bit 7 = 1.
    if (!bus_reset_type_0) {
        nextreg_.write(0x86, save_86);
        nextreg_.write(0x87, save_87);
        nextreg_.write(0x88, save_88);
        nextreg_.write(0x89, save_89);
    }

    // P0 boot probe: dump the pre-reset ring (the LAST capacity-many
    // instructions before this soft reset) to a file, then optionally
    // restart capture so the ring holds the FIRST capacity-many
    // post-reset instructions (combine with JNEXT_TRACE_NO_WRAP) — for
    // symmetric diff against a reference emulator trace armed at the
    // same staging reset.
    if (std::getenv("JNEXT_BOOT_PROBE_TRACE_FROM_RESET") &&
        trace_log_.enabled()) {
        trace_log_.clear();
    }

    // P0 boot probe (doc/issues/nextzxos-boot/ZXGO-COMPARISON-2026-07-09.md):
    // dump the state that discriminates the two candidate $0000-trap
    // mechanisms (A: automap disabled; B: altrom-lock ROM2 residue).
    // Env-gated, zero cost when unset.
    if (std::getenv("JNEXT_BOOT_PROBE")) {
        Log::emulator()->info(
            "BOOT_PROBE post-soft-reset: nr_0a_4_enable={} "
            "B8/B9/BA={:#04x}/{:#04x}/{:#04x} "
            "8C_lock_rom1={} 8C_lock_rom0={} machine_type={} "
            "sram_rom={} slot0_8k_rom_page={}",
            divmmc_.nr_0a_4_enable(),
            divmmc_.entry_points_0(), divmmc_.entry_valid_0(),
            divmmc_.entry_timing_0(),
            mmu_.nr_8c_altrom_lock_rom1(), mmu_.nr_8c_altrom_lock_rom0(),
            static_cast<int>(mmu_.machine_type()),
            mmu_.current_sram_rom(),
            mmu_.current_sram_rom() * 2);
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

    // Verify-audit fix (Task 2 verify-nmi-mf-port): the Multiface FSM
    // does NOT receive `hotkey_m1` directly. Per zxnext.vhd:4290 the
    // Multiface entity's `button_i` port is wired to `nmi_mf_button`
    // (= `nmi_mf AND nmi_state=S_NMI_IDLE`, line 2169) — i.e. the
    // arbiter-elected MF strobe, not the raw F9 edge. That signal is
    // gated by NR 0x06 bit 3, port_e3_reg(7) (CONMEM), divmmc_nmi_hold,
    // nr_03_config_mode (line 2107 + line 2102) before reaching the
    // Multiface. The previous direct call to `multiface_.button_press()`
    // here bypassed all four gates, so a CONMEM-blocked or
    // config-mode-blocked F9 press would still arm the Multiface FSM.
    // The corresponding consumer code in `tick_peripheral_subsystems()`
    // / `simulate_master_cycles()` now drives `multiface_.button_press()`
    // off `NmiSource::mf_button_strobe()` once the arbiter latches the
    // MF priority bit — matching VHDL exactly.
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
    // The host pressed F4: nothing in a recording can make that reset happen
    // again on playback. (A soft reset the PROGRAM asks for, by writing NR
    // 0x02, replays by itself, so it does not end a recording.)
    end_rzx_at_reset("the F4 soft reset");
    soft_reset();
}

void Emulator::on_hotkey_f1_hard_reset()
{
    // VHDL zxnext.vhd:6371 — `nr_02_hard_reset <= hotkey_hard_reset or
    // (nr_02_we and nr_wr_dat(1))`. No config_mode gate (hard reset is
    // unconditional). Hard reset does NOT advance the reset_type FSM
    // (FSM advance is only on the soft_reset edge per VHDL:1735).
    Log::emulator()->info("Hard reset requested via host F1 (hotkey_hard_reset) - deferred to host cold boot");
    // Task 70: model the physical reset button as a power-on cold boot done by
    // the host frontend (reconstruct + init). Record the request; the frontend
    // polls take_hard_reset_request() and performs it between frames.
    request_hard_reset();
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

uint8_t Emulator::effective_internal_port_enable(uint8_t reg) const
{
    return effective_internal_port_enable(reg, /*override_reg=*/0xFF, 0);
}

uint8_t Emulator::effective_internal_port_enable(uint8_t reg,
                                                 uint8_t override_reg,
                                                 uint8_t override_val) const
{
    // VHDL zxnext.vhd:2392-2393:
    //   internal_port_enable <= (nr_85 & nr_84 & nr_83 & nr_82) when
    //                           expbus_eff_en='0' else
    //                           ((nr_89 AND nr_85) & (nr_88 AND nr_84) &
    //                            (nr_87 AND nr_83) & (nr_86 AND nr_82));
    // The four NR 0x82-0x85 bytes form the low 32 bits of the
    // `internal_port_enable` vector. When `expbus_eff_en` (NR 0x80 b7)
    // is '1', each bit is the AND of the corresponding NR 0x82-0x85
    // and NR 0x86-0x89 bit. We expose the per-byte effective gate so
    // every port-decode site can read it the same way it reads the
    // raw cache today.
    if (reg < 0x82 || reg > 0x85) {
        return (reg == override_reg) ? override_val : nextreg_.cached(reg);
    }
    auto cache_or_override = [&](uint8_t r) -> uint8_t {
        return (r == override_reg) ? override_val : nextreg_.cached(r);
    };
    const uint8_t base = cache_or_override(reg);
    // nmi_source_ owns the `expbus_eff_en` shadow; mirrors NR 0x80 b7
    // (zxnext.vhd:2197 + 5800-5813). When expbus is disabled the bus-
    // port enable group has no effect. NB: NR 0x80's effect on the
    // shadow is committed in the NR 0x80 write handler BEFORE the
    // propagate helper is invoked, so the `expbus_eff_en()` accessor
    // here always reflects the post-write state.
    if (!nmi_source_.expbus_eff_en()) {
        return base;
    }
    // Pair-up: 0x82↔0x86, 0x83↔0x87, 0x84↔0x88, 0x85↔0x89.
    // VHDL widths (zxnext.vhd:1226-1234): NR 0x82/0x83/0x84/0x86/0x87/0x88
    // are 8 bits; NR 0x85 / 0x89 are 4 bits each (`bus_port_enable` /
    // `internal_port_enable` low nibble) plus bit 7 = reset_type. The
    // formula at :2392-2393 only uses the enable nibble for the 0x85↔0x89
    // pair, so mask the high nibble out of the bus-port mask to avoid
    // ANDing reset_type into bit 7 of the effective gate (no current
    // consumer reads bit 7 of NR 0x85 as a port-enable, but be precise).
    const uint8_t paired_reg = static_cast<uint8_t>(reg + 4);
    uint8_t mask = cache_or_override(paired_reg);
    if (paired_reg == 0x89) {
        mask = static_cast<uint8_t>(mask & 0x0F);
        // Preserve high nibble of `base` (reset_type at bit 7) so callers
        // that read NR 0x85 bit 7 outside the port-enable formula are
        // unaffected. Only the enable nibble (bits 3:0) participates in
        // the AND.
        const uint8_t high_nibble = static_cast<uint8_t>(base & 0xF0);
        const uint8_t and_low     = static_cast<uint8_t>((base & 0x0F) & mask);
        return static_cast<uint8_t>(high_nibble | and_low);
    }
    return static_cast<uint8_t>(base & mask);
}

void Emulator::propagate_effective_port_enables(uint8_t override_reg,
                                                uint8_t override_val)
{
    // V16-NMP-02 (Pass-16): re-push every shadow held outside NextReg
    // that mirrors a bit of the effective `internal_port_enable` vector
    // (zxnext.vhd:2392-2393). When any of NR 0x80 b7 (`expbus_eff_en`),
    // NR 0x82-0x85 (`internal_port_enable` low 32 bits), or NR 0x86-0x89
    // (`bus_port_enable` AND-mask) changes, every consumer gets the
    // freshly-computed effective gate.
    //
    // ContentionModel holds two such shadows:
    //   - port_7ffd_io_en  ← effective NR 0x82 bit 1  (VHDL :2399).
    //   - port_ulap_io_en  ← effective NR 0x85 bit 0  (VHDL :2439).
    contention_.set_port_7ffd_io_en(
        (effective_internal_port_enable(0x82, override_reg, override_val) & 0x02) != 0);
    contention_.set_port_ulap_io_en(
        (effective_internal_port_enable(0x85, override_reg, override_val) & 0x01) != 0);
    // DivMmc and Multiface gate themselves on NR 0x83 bit 0 / bit 1
    // (VHDL :2412 / :2415). When their gate falls low the FSM goes
    // inert (port-decode no longer reaches them).
    divmmc_.set_port_io_enable(
        (effective_internal_port_enable(0x83, override_reg, override_val) & 0x01) != 0);
    multiface_.set_enabled(
        (effective_internal_port_enable(0x83, override_reg, override_val) & 0x02) != 0);
}

uint8_t Emulator::port_df_read()
{
    // VHDL zxnext.vhd:2674:
    //   port_1f <= '1' when (port_1f_lsb = '1' or (port_df_lsb = '1'
    //                  and port_dac_mono_AD_df_io_en = '1'
    //                  and port_mouse_io_en = '0'))
    //                  and port_1f_io_en = '1' and port_1f_hw_en = '1'
    // port_dac_mono_AD_df_io_en = internal_port_enable(23) = NR 0x84 b7
    // (:2435), port_mouse_io_en = (13) = NR 0x83 b5 (:2422), port_1f_io_en =
    // (6) = NR 0x82 b6 (:2407), port_1f_hw_en = joyL_1f_en or joyR_1f_en
    // (:2454). The read strobe is port_1f_rd (:2784). The only other LSB-0xDF
    // decodes are the Specdrum DAC (:2658), which has write strobes alone
    // (:2775,2778), and the mouse (:2668-2670), which needs the mouse ENABLED.
    // So when port_1f is off `port_internal_rd_response` stays low
    // (:2803-2806) and the read is cpu_di's X"FF" (:1877).
    if ((effective_internal_port_enable(0x84) & 0x80) == 0) return 0xFF;
    if ((effective_internal_port_enable(0x83) & 0x20) != 0) return 0xFF;
    if ((effective_internal_port_enable(0x82) & 0x40) == 0) return 0xFF;
    if (!joystick_.port_1f_hw_en()) return 0xFF;
    return joystick_.read_port_1f();
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
    // `port_ff_io_en` gate that collapses the Timex arm.
    //
    // GH #109: this function is the read handler of the LSB-0xFF port
    // registration (mask 0x00FF / value 0x00FF at init()), matching the
    // VHDL scope exactly: `port_ff <= '1' when cpu_a(7:0) = X"FF"`
    // (zxnext.vhd:2571+2583). It is NO LONGER the catch-all default for
    // unmatched ports — a genuinely undecoded port read returns X"FF"
    // via the dispatch default (zxnext.vhd:1877).

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
    // V16-NMP-02: port_ff_io_en is internal_port_enable(0); when
    // expbus_eff_en=1 NR 0x82 b0 must be AND'd with NR 0x86 b0 per
    // VHDL :2392-2393.
    const uint8_t nr_82 = effective_internal_port_enable(0x82);
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
    // collapse to 0xFF here.
    //
    // D3F-03 fix: gate on `machine_timing_` (tim_sel axis) per VHDL :4513
    // `machine_timing_48 = '1' or machine_timing_128 = '1'`. The earlier
    // version keyed on `config_.type`, which silently mis-decoded when NR
    // 0x03 was written with `tim_sel != typ_sel` (the Next runtime path
    // flagged as the prior follow-up note). With the V24-MEM-01 split,
    // `mmu_.machine_timing()` is the authoritative tim_sel mirror.
    {
        const MachineTimingMode tim = mmu_.machine_timing();
        if (tim != MachineTimingMode::Timing48 && tim != MachineTimingMode::Timing128) {
            return 0xFF;
        }
    }

    // ---- 3. ULA floating-bus content (48K/128K only) ----
    uint8_t b;
    if (ula_floating_bus_active_arm(b)) {
        return b;
    }
    return 0xFF;
}

bool Emulator::ula_floating_bus_active_arm(uint8_t& out_byte) const
{
    // VHDL zxula.vhd:573 — the "active arm" of `o_ula_floating_bus`:
    //     floating_bus_r  when border_active_ula = '0' and floating_bus_en = '1'
    // Returns true and writes floating_bus_r into @p out_byte when the arm
    // fires; false otherwise (caller picks the fallback — i_p3_floating_bus
    // on +3, X"FF" elsewhere).
    //
    // GH #265 follow-up (verifier finding 1). Evaluated in the ULA's OWN
    // counters, the same hc_ula / vc_ula frame the contention path and NR
    // 0x1E/0x1F use (hc_ula == 0 at raw hc c_min_hactive - 11,
    // zxula_timing.vhd:423-436; vc_ula == 0 on raw line c_min_vactive,
    // :441-451). The model this replaces read "raw line 64..255, raw T 0..127"
    // of jnext's frame, 58 T (48K) / 62 T (128K) before the ULA's fetch
    // window, and ignored the Timex modes, the ULA scroll and the shadow
    // screen.
    //
    // WHEN: floating_bus_r reaches cpu_di through combinational logic only
    // (zxula.vhd:573; zxnext.vhd:4513,4517,2813-2814,2837,1872-1873), and the
    // T80 latches cpu_di into DI_Reg on the falling edge of the I/O cycle's
    // T3 (t80na.vhd:214-222) — after every contention stretch of the cycle.
    static constexpr unsigned kDiRegLatchClock = 3;       // T3's falling edge
    const uint64_t m    = io_read_sample_cycle(kDiRegLatchClock);
    const uint64_t mcpf = timing_.master_cycles_per_frame;
    const uint64_t mcpl = timing_.master_cycles_per_line;
    const uint64_t origin_mc =
        static_cast<uint64_t>(video_timing_.hc_ula_zero_raw_hc()) * 4u;
    const uint64_t shifted = ((m - frame_cycle_) % mcpf + mcpf - origin_mc) % mcpf;
    const int  lpf    = video_timing_.vc_max() + 1;
    const int  uline  = static_cast<int>(shifted / mcpl);
    const int  mc_in  = static_cast<int>(shifted % mcpl);  // master cycles since hc_ula 0
    const int  vc_ula = (uline - video_timing_.display_origin().vc + lpf) % lpf;

    // border_active_ula <= i_hc(8) or border_active_v, border_active_v <=
    // i_vc(8) or (i_vc(7) and i_vc(6)) (zxula.vhd:414-416) — in the output
    // gate at :573 with the CURRENT counters.
    const int hc_now = mc_in / 4;
    if ((hc_now & 0x100) != 0 || vc_ula >= 192) return false;

    // floating_bus_r is reloaded on the FALLING edge of CLK_7, half-way
    // through each hc_ula count (zxula.vhd:308-340); a latch sees the value
    // of the last edge before it. `mc_in` is a master cycle, and hc_ula's
    // falling edge sits 2 master cycles into its 4.
    if (mc_in < 2) return false;               // last edge: previous line's border
    const int hc_e = (mc_in - 2) / 4;          // i_hc at the last falling edge
    const int q    = hc_e & 0x0F;
    // Reload schedule (zxula.vhd:319-340): hc(3:0) = 1 -> X"FF" (en <= 0);
    // 9 -> VRAM byte (en <= 1); B, D, F -> VRAM byte; others hold. The byte
    // on i_ula_vram_d at 9/B/D/F is the fetch set up two counts earlier
    // (vram_a on the rising edges ending hc 7/9/B/D, zxula.vhd:224-258):
    // pixel(px), attribute(px), pixel(px'), attribute(px'), where px is
    // latched at hc(3:0) = 3 and px' at hc(3:0) = B (zxula.vhd:193-209).
    int block = hc_e >> 4;
    int half, kind;                            // half: px(0)/px'(1); kind: pixel(0)/attr(1)
    switch (q) {
        case 9: case 10:  half = 0; kind = 0; break;
        case 11: case 12: half = 0; kind = 1; break;
        case 13: case 14: half = 1; kind = 0; break;
        case 15:          half = 1; kind = 1; break;
        case 0:
            // Still attribute(px') of the PREVIOUS block, reloaded at its F.
            // At hc_ula 0 that edge was the previous line's border: X"FF".
            if (hc_e == 0) return false;
            block -= 1; half = 1; kind = 1; break;
        default: return false;                 // 1..8: reset to X"FF", en = 0
    }

    // px(7:3) <= i_hc(7:3) + i_ula_scroll_x(7:3) at hc(3:0) = 3 / B
    // (zxula.vhd:199): i_hc(7:3) is 2*block / 2*block+1 there.
    const auto& ula = renderer_.ula();
    const int col = ((2 * block + half) + (ula.get_ula_scroll_x_coarse() >> 3)) & 0x1F;
    // py <= i_vc + i_ula_scroll_y, folded into 0..191 (zxula.vhd:192,201-207).
    const int py_s = vc_ula + ula.get_ula_scroll_y();
    int py;
    if ((py_s & 0x180) == 0x180) {
        py = py_s & 0x7F;                                   // (not py_s(7)) & py_s(6:0)
    } else if ((py_s & 0x100) != 0 || (py_s & 0xC0) == 0xC0) {
        py = ((((py_s >> 6) + 1) & 0x03) << 6) | (py_s & 0x3F);  // (py_s(7:6)+1) & py_s(5:0)
    } else {
        py = py_s & 0xFF;
    }
    // screen_mode_s <= i_port_ff_reg(2:0) when i_ula_shadow_en = '0' else
    // "000" (zxula.vhd:191); the shadow screen is bank 7 (zxnext.vhd:6649-6656,
    // Ula::fetch_vram).
    const int mode = ula.get_shadow_screen_en() ? 0 : (ula.get_screen_mode_reg() & 0x07);
    // addr_p_spc_12_5 <= py(7:6) & py(2:0) & py(5:3); addr_a_spc_12_5 <=
    // "110" & py(7:3) (zxula.vhd:220-221); vram_a (zxula.vhd:236-252):
    //   pixel:     screen_mode(0) & addr_p & px(7:3)
    //   attribute: '1' & addr_p & px(7:3)             when screen_mode(1) = '1'
    //              screen_mode(0) & addr_a & px(7:3)  otherwise
    const int addr_p = (py & 0xC0) | ((py & 0x07) << 3) | ((py & 0x38) >> 3);
    const int addr_a = 0xC0 | (py >> 3);
    int vram_a;
    if (kind == 0) {
        vram_a = ((mode & 1) << 13) | (addr_p << 5) | col;
    } else if ((mode & 0x02) != 0) {
        vram_a = (1 << 13) | (addr_p << 5) | col;
    } else {
        vram_a = ((mode & 1) << 13) | (addr_a << 5) | col;
    }
    out_byte = ula.fetch_vram(static_cast<uint16_t>(vram_a));
    return true;
}

void Emulator::enqueue_cpu_nr_write(uint8_t reg, uint8_t val)
{
    // GH #265 — the edge the write commits on. The CPU's request (IORQ+WR
    // on 0x253B, or a NEXTREG opcode's Z80N_dout) is edge-detected into
    // cpu_req on the next CLK_28 edge, nr_*_we is high during the cycle that
    // edge starts, and the registers take the write on the edge after
    // (zxnext.vhd:4739-4777, 4786-4840).
    const uint64_t edge = io_request_edge() + 2;
    switch (reg) {
        // The registers whose write is ordered against interrupt requests:
        // NR 0x20 raises unqualified ones, NR 0x22 / 0xC4-0xC6 / 0xC0 change
        // which requests qualify, NR 0xC8-0xCA clear the status bits. Every
        // request raised before the commit edge must meet the old value
        // (im2_peripheral.vhd:154-178 sample i_int_en / i_int_status_clear
        // on the same edge as the request).
        case 0x20: case 0x22: case 0xC0: case 0xC4: case 0xC5: case 0xC6:
        case 0xC8: case 0xC9: case 0xCA:
            sync_io_devices_to(edge - 1, edge - 1);
            break;
        default:
            break;
    }
    pending_cpu_nr_writes_.push_back(PendingNrWrite{reg, val, edge});
}

void Emulator::flush_pending_cpu_nr_writes(uint64_t edge_limit)
{
    if (pending_cpu_nr_writes_.empty()) return;
    // Drain in FIFO order. Each commit goes through the regular
    // NextReg::write path so write_handlers fire (any side effect
    // landing here is the same as if the CPU had written directly,
    // just shifted later by the per-instruction Copper window).
    //
    // GH #272 — a write whose commit edge is at or after `edge_limit`
    // stays queued: the caller is standing at a video-row boundary and
    // that write belongs on the far side of it. Edges are monotonic
    // within an instruction (`clock_` is fixed and
    // `tstates_into_instruction()` only grows), so this is a prefix
    // drain; it is written as a filter anyway so it cannot silently
    // reorder if that ever stops holding.
    std::size_t kept = 0;
    for (std::size_t i = 0; i < pending_cpu_nr_writes_.size(); ++i) {
        // By value, and indexed: the compaction writes back into the vector
        // at or before `i`, and `nextreg_.write()` reaches write handlers.
        const PendingNrWrite w = pending_cpu_nr_writes_[i];
        if (w.edge >= edge_limit) {
            pending_cpu_nr_writes_[kept++] = w;
            continue;
        }
        nr_write_edge_ = w.edge;
        nextreg_.write(w.reg, w.val);
    }
    nr_write_edge_ = Im2Controller::kNoTime;
    pending_cpu_nr_writes_.resize(kept);
}

uint64_t Emulator::io_request_edge() const
{
    const uint32_t into = cpu_.tstates_into_instruction();
    if (into == 0) return clock_.get();
    return clock_.get() + static_cast<uint64_t>(into) * clock_.cpu_divisor();
}

void Emulator::sync_io_devices_to(uint64_t device_edge, uint64_t im2_edge)
{
    // Only a bus cycle of the instruction in progress can be ahead of the
    // devices: outside execute() they already stand at clock_.
    if (!cpu_.executing()) return;
    const uint64_t start  = clock_.get();   // the instruction's first edge
    const uint64_t serial = cpu_.execute_serial();
    if (!io_sync_valid_ || io_sync_serial_ != serial) {
        io_sync_valid_  = true;
        io_sync_serial_ = serial;
        io_sync_edge_   = start;
    }
    if (device_edge > io_sync_edge_) {
        const uint32_t n = static_cast<uint32_t>(device_edge - io_sync_edge_);
        ctc_.set_time(io_sync_edge_);
        ctc_.tick(n);
        uart_.set_time(io_sync_edge_);
        uart_.tick(n);
        md6_.tick(n);
        io_sync_edge_ = device_edge;
    }
    irq_scheduler_.run_until(device_edge);
    im2_.latch_edges_until(im2_edge, start, clock_.cpu_divisor());
}

void Emulator::run_scheduled_until(uint64_t cycle)
{
    irq_scheduler_.run_until(cycle);
    scheduler_.run_until(cycle);
}

uint32_t Emulator::tape_sample_offset() const
{
    // T-states from the instruction's start to the port_fe_dat_0 load: the
    // CLK_CPU falling edge in the I/O cycle's third clock (2.5 T-states in,
    // after the cycle's contention stretches) takes the level of the T-state
    // it falls in.
    static constexpr unsigned kCpuFallingReloadClock = 2;
    const uint64_t k = io_read_sample_cycle(kCpuFallingReloadClock);
    return static_cast<uint32_t>((k - clock_.get()) / clock_.cpu_divisor());
}

uint64_t Emulator::tape_sample_tstates() const
{
    if (!cpu_.executing()) return monotonic_tstates();
    return monotonic_tstates() - cpu_.tstates_into_instruction()
         + tape_sample_offset();
}

uint8_t Emulator::tap_ear_for_port_read()
{
    if (!cpu_.executing()) return tape_.tick_realtime(0);
    if (!tap_sync_valid_ || tap_sync_serial_ != cpu_.execute_serial()) {
        tap_sync_valid_  = true;
        tap_sync_serial_ = cpu_.execute_serial();
        tap_sync_ts_     = 0;
    }
    const uint32_t off = tape_sample_offset();
    if (off > tap_sync_ts_) {
        tape_.tick_realtime(off - tap_sync_ts_);
        tap_sync_ts_ = off;
    }
    return tape_.tick_realtime(0);
}

void Emulator::sync_for_port_read()
{
    // A port whose data register is reloaded on every CLK_CPU falling edge
    // (port_ctc_dat, port_uart_dat, port_fe_dat_0): the IN latches the one
    // loaded in its I/O cycle's third clock (2.5 T-states in, after the
    // cycle's contention stretches), so the device state it holds is the one
    // after the CLK_28 edge before that reload.
    if (!cpu_.executing()) return;
    static constexpr unsigned kCpuFallingReloadClock = 2;
    const uint64_t k = io_read_sample_cycle(kCpuFallingReloadClock);
    sync_io_devices_to(k, k);
}

void Emulator::sync_for_port_write()
{
    // A port write reaches the device as a strobe high from the CPU edge
    // IORQ and WR assert on; the device's CLK_28 registers take it on the
    // edge after (ctc_chan.vhd:246-254 edge-detects iowr on CLK_28). That
    // edge still counts with the old settings, and the IM2 fabric meets the
    // old settings for every request up to the edge before.
    if (!cpu_.executing()) return;
    const uint64_t w = io_request_edge() + 1;
    sync_io_devices_to(w, w - 1);
}

uint64_t Emulator::im2_status_read_edge()
{
    if (!cpu_.executing()) return Im2Controller::kNoTime;
    // The port_253b_dat_0 reload edge, in the third clock of the IN's I/O
    // cycle (zxnext.vhd:5871-5876) — the same edge NR 0x1E/0x1F sample at.
    static constexpr unsigned kPort253bReloadClock = 2;
    const uint64_t k = io_read_sample_cycle(kPort253bReloadClock);
    sync_io_devices_to(k, k);
    return k;
}

void Emulator::clear_im2_status(Im2Controller::DevIdx d)
{
    if (nr_write_edge_ == Im2Controller::kNoTime) im2_.clear_status(d);
    else im2_.clear_status(d, nr_write_edge_);
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

    // GH #265 — raised at the edge int_line goes high: the compare is
    // registered on CLK_7 (zxula_timing.vhd:574-583), one pixel (4 master
    // cycles) after the compare position above.
    const uint64_t int_req_edge = fire_cycle + 4;
    ++line_int_schedule_gen_;
    const uint64_t my_gen = line_int_schedule_gen_;
    irq_scheduler_.schedule(int_req_edge, EventType::CPU_INT,
        [this, my_gen, int_req_edge]() {
            if (my_gen != line_int_schedule_gen_) return;  // superseded
            im2_.raise_req(Im2Controller::DevIdx::LINE, int_req_edge);
            // V20R-CPU-NIT-02 — pulse-mode CPU /INT now driven solely
            // by the post-im2_.tick() falling-edge poll at line ~5791.
            // Symmetric with the FRAME-INT scheduler at line ~5443
            // (also dropped). See the V20R-CPU-NIT-02 comment there
            // for the full rationale.
            im2_int_status_[0] |= 0x02;  // Line interrupt status
            ++line_int_fire_count_;       // G163 test-observable
        });
}

void Emulator::advance_copper_across_row_boundaries(uint64_t master_cycles)
{
    // See the declaration for what this exists to prevent (GH #272).
    //
    // `clock_` was already advanced by `master_cycles` before we are
    // called (see run_frame / step_frame_slot / execute_single_instruction,
    // the three call sites of tick_devices_after_instruction), so the
    // window the instruction consumed is [post - master_cycles, post) and
    // the Copper steps at each of those master cycles.
    const uint64_t post = clock_.get();
    uint64_t       cur  = post - master_cycles;

    // `scheduler_` carries ONLY the video-row queue: one SCANLINE event
    // per raw line (schedule_frame_events()) plus the frame's VSYNC.
    // The interrupt fabric lives in `irq_scheduler_`, which is left
    // exactly where it was at the end of the cluster — moving interrupt
    // requests relative to an instruction is not what this fixes.
    //
    // An event due at exactly `post` is NOT taken here. The Copper steps
    // at cycles strictly below `post`, so it is already ordered after all
    // of them, and leaving it to the cluster's own
    // run_scheduled_until(clock_) keeps it after the deferred CPU NR
    // writes — which is right, since every CPU write of THIS instruction
    // physically precedes a boundary that sits at the instruction's end.
    // That `<` is NOT pinned by any row, and this says so rather than
    // leaving a future reader to re-derive it. The two spellings differ
    // only for an event due at exactly `post`, and nothing observable
    // separates them there: instruction N+1 has not run in either
    // ordering, so `pending_cpu_nr_writes_` and `clock_` hold the same
    // state whether the event is the tail of this cluster or the head of
    // the next. The equivalence was reasoned through, not proven across
    // every subsystem on_scanline touches, so `<` is also the
    // CONSERVATIVE spelling: a boundary landing on an instruction's end
    // fires exactly where it fired before GH #272, which keeps the fix
    // from moving CPU-write row attribution on the very common alignment
    // of an instruction ending on a line boundary.
    while (!scheduler_.empty() && scheduler_.next_cycle() < post) {
        const uint64_t boundary = scheduler_.next_cycle();
        if (boundary > cur) {
            tick_copper_for_master_cycles(cur, boundary - cur);
            cur = boundary;
        }
        // The CPU's own deferred writes that commit before this boundary
        // belong to the row that is ending, exactly as they did when the
        // whole window was one segment.
        flush_pending_cpu_nr_writes(boundary);
        scheduler_.run_until(boundary);
    }

    if (cur < post) tick_copper_for_master_cycles(cur, post - cur);
}

void Emulator::tick_copper_for_master_cycles(uint64_t begin, uint64_t master_cycles)
{
    // Hot-path early-out — most of every frame the Copper is in mode 0
    // (stopped) and the loop body would be pure overhead.
    if (!copper_.is_running()) return;
    if (master_cycles == 0) return;

    // The Copper steps fire at master cycles
    // [begin, begin+1, ..., begin+master_cycles-1]. Kept under the old
    // name so the rest of the body below is untouched by GH #272.
    const uint64_t pre_clock  = begin;

    // C9 (Task 27 Wave 2) — strength-reduce the per-master-cycle div/mod.
    // The loop steps the absolute master-cycle counter by exactly 1 each
    // iteration, so (hc, vc, cvc) advance deterministically. Establish the
    // counters from the division ONCE at loop entry (for `pre_clock`), then
    // increment: hc++ wrapping at master_cycles_per_line (on wrap, vc++),
    // and cvc = (vc - origin) mod lines_per_frame — which advances by +1
    // exactly when vc does, wrapping at lines_per_frame. This produces a
    // byte-identical (hc, cvc) sequence to the old div/mod (proven below),
    // with no divide in the hot loop.
    //
    // Copper vc origin: cvc=0 at the first active display line, mirroring
    // VHDL `zxula_timing.vhd:455-472`. The first active display line in VHDL
    // is vc=min_vactive (64 for NEXT 50Hz) — display_origin().vc. Using
    // DISP_Y (32) instead would fire WAITs 32 lines too early (parallax.nex
    // bottom-third banding); see G164v2 history.
    //
    // GH #181 — the counters the Copper compares against are the ULA PIXEL
    // counters `hc_ula` / `cvc`, NOT the raw master-cycle position:
    //
    //   zxnext.vhd:3949-3950   hcount_i => hc, vcount_i => cvc
    //   zxnext.vhd:6737-6739   o_hc_ula => hc, o_vc_cu => cvc
    //   copper.vhd:35-36       hcount_i/vcount_i : unsigned(8 downto 0)
    //   copper.vhd:94          hcount_i >= (hpos & "000") + 12
    //
    // `hc_ula` is a 7 MHz per-PIXEL counter (zxula_timing.vhd:427-436,
    // clocked on i_CLK_7) — a quarter of the 28 MHz master rate — and its
    // zero is at raw hc == c_min_hactive - 11, not at the raw line start
    // (VideoTiming::hc_ula_zero_raw_hc()). `cvc` counts lines in THAT
    // counter's frame: it steps on the same `ula_max_hc` pulse that wraps
    // hc_ula (zxula_timing.vhd:457-470), so its line boundary is the same
    // shifted one, not the raw hc==0 boundary.
    //
    // Both are therefore obtained by shifting the raw frame-relative
    // master-cycle position back by `hc_ula_zero_raw_hc()` PIXELS, then
    // splitting the result into (line, pixel-in-line) exactly as before.
    // Pre-fix jnext passed the raw 0..(mcpl-1) master-cycle offset as `hc`
    // — wrong by 4x in scale AND by the origin — so every WAIT with a
    // non-trivial hpos fired ~a quarter of the way into the line it should
    // have, up to a full row early.
    const uint64_t mcpl = timing_.master_cycles_per_line;
    const uint64_t mcpf = timing_.master_cycles_per_frame;
    const int lpf       = timing_.lines_per_frame;
    const int origin_vc = static_cast<int>(video_timing_.display_origin().vc);
    const uint64_t shift_mc =
        static_cast<uint64_t>(video_timing_.hc_ula_zero_raw_hc()) * 4u;

    // Rebase onto the hc_ula/cvc counter frame. The `+ mcpf` keeps the
    // subtraction non-negative in unsigned arithmetic (shift_mc << mcpf); the
    // `% mcpf` normalises `shifted` to one frame so `uline` lands in
    // [0, lpf) — the range the incremental loop below reads it as.
    //
    // That normalisation is NORMALISATION, not behaviour, and no test can
    // pin it: `mcpf == lpf * mcpl` exactly, so the downstream `% mcpl` and
    // `% lpf` already reduce both `line_mc` and `cvc` to the same values
    // with or without it — dropping it (either here, or as a separate
    // `elapsed0 % mcpf`, which is dead for the same reason) leaves every
    // observable identical. Verified by mutation, not assumed. It stays
    // because an out-of-range `uline` would be a trap for the next reader,
    // and it costs one division per call, outside the loop.
    //
    // What IS behaviour is the frame-boundary carry it exists to express:
    // the hc_ula/cvc line `uline == lpf-1` STRADDLES two jnext frames, and
    // its hc_ula 331..455 tail is raw line 0 of the next one. That is
    // covered — GH181-HCULA-04 in copper_integration_test places a WAIT
    // reachable only in that tail.
    const uint64_t elapsed0 = pre_clock - frame_cycle_;
    const uint64_t shifted  = (elapsed0 + mcpf - shift_mc) % mcpf;

    // `line_mc` counts master cycles since hc_ula==0; hc_ula = line_mc / 4.
    int line_mc = static_cast<int>(shifted % mcpl);
    int uline   = static_cast<int>(shifted / mcpl);
    int cvc     = (uline - origin_vc + lpf) % lpf;
    const int mcpl_i = static_cast<int>(mcpl);

    for (uint64_t c = 0; c < master_cycles; ++c) {
        copper_.execute(line_mc >> 2, cvc, nextreg_);
        if (++line_mc == mcpl_i) {
            line_mc = 0;
            if (++cvc == lpf) cvc = 0;
        }
    }
}

uint64_t Emulator::io_read_sample_cycle(unsigned io_clock) const
{
    // GH #265. clock_ holds the START of the instruction now executing (it is
    // ticked once execute() returns); the CPU says where, in T-states into
    // the instruction, clock `io_clock` of the current I/O cycle began once
    // its stretch (and every earlier one) had been charged.
    const uint64_t now = clock_.get();
    if (cpu_.tstates_into_instruction() == 0) return now;   // not inside an instruction
    const uint64_t d = clock_.cpu_divisor();
    const uint64_t clk_start =
        now + static_cast<uint64_t>(cpu_.io_clock_into_instruction(io_clock)) * d;
    // The falling edge is half a T-state into the clock. The latch takes the
    // value of the master cycle just before it: ceil(edge) - 1. At 3.5 MHz
    // the CPU's falling edges land on master-cycle boundaries (d = 8); at
    // 28 MHz (d = 1) a falling edge is mid-cycle and that cycle is the one.
    return clk_start + (d + 1) / 2 - 1;
}

int Emulator::cvc_at(uint64_t master_cycle) const
{
    // cvc = (vc - c_min_vactive + cu_offset) mod (c_max_vc + 1)
    // (zxula_timing.vhd:455-472), read back by NR 0x1E/0x1F
    // (zxnext.vhd:5982-5986). cvc steps on `ula_max_hc`, the same registered
    // pulse that zeroes hc_ula (:423-436, :457-470), so its line boundary is
    // raw hc == hc_ula_zero_raw_hc() (c_min_hactive - 11), not raw hc 0.
    // GH #257 — this used to step at raw hc 0, ~125 pixels early, so a
    // program polling NR 0x1F left its wait loop that much sooner than on
    // hardware. The raw frame position is shifted back by that origin
    // before splitting it into lines, exactly as
    // tick_copper_for_master_cycles() does for the Copper's own cvc (GH #181).
    // The `% mcpf` also folds a position past the frame end (an IN whose
    // sampling point lies beyond frame_end, GH #265) into the next frame.
    const uint64_t mcpf = timing_.master_cycles_per_frame;
    const uint64_t shift_mc =
        static_cast<uint64_t>(video_timing_.hc_ula_zero_raw_hc()) * 4u;
    const uint64_t elapsed = master_cycle - frame_cycle_;
    const uint64_t shifted = (elapsed + mcpf - shift_mc) % mcpf;
    const int raw_vc = static_cast<int>(shifted / timing_.master_cycles_per_line);
    const int lines_per_frame = video_timing_.vc_max() + 1;
    const int min_vactive     = video_timing_.display_origin().vc;
    const int cu_offset       = video_timing_.cu_offset();
    int cvc = (raw_vc - min_vactive + cu_offset) % lines_per_frame;
    if (cvc < 0)
        cvc += lines_per_frame;
    return cvc;
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
            snapshot_row_render_state(prev_fb_row);
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
    // G12 — tag subsequent attribute-plane writes with this scanline
    // (framebuffer-row space, matching every sibling log above).
    mmu_.attr_mux_set_current_line(tag);
    // G02 — tag subsequent NR 0x15 writes (layer priority / sprite enable).
    renderer_.set_current_line_nr15(tag);

    // GH #268 — advance the membrane scan at the rate the HARDWARE scans at.
    //
    // Keyboard::tick_scan() drives the two-scan CS/SYM shift hysteresis
    // (membrane.vhd:178 "advancing shift key state one scan and holding shift
    // key state an extra scan"), which HOLDS A RELEASING SHIFT FOR ONE MORE
    // SCAN. The rate therefore decides how long a released shift keeps
    // contaminating the matrix, and the VHDL states it outright:
    //
    //   CLK_28_MEMBRANE_EN <= clkdiv_8_7 and clkdiv_6_4 and clkdiv_3_0;
    //      -- complete scan every 2.5 scanlines (0.018ms per row)
    //                                     (zxnext_top_issue2.vhd:1179, :1168-70)
    //
    // i.e. one enable every 2^9 = 512 cycles of CLK_28, and nine of them per
    // full scan of the 9-state one-hot rotator (membrane.vhd:99-108) = 4608
    // master cycles ~ 165 us.
    //
    // This used to be called ONCE PER VIDEO FRAME from end_of_frame(), which
    // stretched a 165 us hysteresis to 20 ms — 122x — and that is not a
    // cosmetic inaccuracy. It means a SYMBOL SHIFT released at the end of one
    // frame is still reported pressed by read_rows() during the NEXT one, so a
    // tap of SYMBOL SHIFT followed by a tap of P one frame later decodes as
    // SYM+P and types `"` instead of PRINT. Measured, and it is exactly the
    // wrong-character half of issue #268; the router's serialiser (which puts
    // consecutive keystrokes one frame apart) cannot fix it on its own, and
    // typing a shifted character followed quickly by an unshifted one hits it
    // with no serialiser involved at all.
    //
    // The old justification — "Z80 software polls port 0xFE at most once per
    // frame in normal operation, and the hysteresis goal is a release lagging
    // by ~1 frame" — was wrong on the second count: the VHDL's goal is one
    // MEMBRANE SCAN, which is three orders of magnitude shorter than a frame.
    membrane_scan_accum_ += timing_.master_cycles_per_line;
    while (membrane_scan_accum_ >= MEMBRANE_SCAN_CYCLES) {
        membrane_scan_accum_ -= MEMBRANE_SCAN_CYCLES;
        keyboard_.tick_scan();
    }
}

void Emulator::snapshot_row_render_state(int fb_row)
{
    renderer_.snapshot_fallback_for_line(fb_row);
    renderer_.snapshot_ula_enabled_for_line(fb_row);
    renderer_.snapshot_stencil_mode_for_line(fb_row);
    renderer_.snapshot_blend_mode_for_line(fb_row);
    renderer_.snapshot_tm_enabled_for_line(fb_row);
    renderer_.snapshot_transparent_rgb_for_line(fb_row);
    renderer_.snapshot_ula_clip_for_line(fb_row);
    renderer_.lores().snapshot_for_line(fb_row);
    renderer_.ula().snapshot_border_for_line(fb_row);
    // GH #256 — sprite clip / NR 0x15 b6,b5,b1 / NR 0x4B index, and the
    // ULA's ULA+ / ULAnext / shadow-bank state.
    sprites_.snapshot_control_for_line(fb_row, palette_.sprite_transparency());
    renderer_.ula().snapshot_control_for_line(fb_row);
    // Tilemap scroll (GH #16), fetch bases (GH #53) and output-stage NR 0x1B
    // clip / NR 0x4C index (GH #256), all at the one point so a Copper split
    // of several of them switches on one row. GH #257 — these used to be
    // taken at the START of the raw line (a write in line N showed from row
    // N+1), which was one row late for a Copper WAIT(n,0) + MOVE: that
    // completes at hc_ula 12 = whc 32, the start of the paper (copper.vhd:94,
    // zxula_timing.vhd:423-436, :474-490), and the tilemap re-reads the live
    // registers at every character's S_IDLE (tilemap.vhd:309,345-350) and
    // compares NR 0x4C per pixel (:427), so hardware changes row n from x~35.
    // The start-of-line latch had been right for GH #16 only because the
    // line interrupt fired ~380 pixels early (fixed in
    // VideoTiming::line_int_master_cycle_offset()).
    tilemap_.snapshot_scroll_for_line(fb_row);
    tilemap_.snapshot_fetch_for_line(fb_row);
    tilemap_.snapshot_output_for_line(fb_row, palette_.tilemap_transparency());
}

void Emulator::on_vsync()
{
    // Stub: nothing to do yet.
    // Real implementation:
    //   - Signal the platform layer that a new frame is ready.
    //   - Reset per-frame state (floating bus cache, sprite collision flags).
}

// ── The Emulator's OWN scalars, as declarations (GH #27 S6) ──────────────
//
// Design §9.2 (the one field list), §10.1's last row. See emulator.h for why
// there are five methods rather than one, and for what deliberately stays
// hand-written beside them.

void Emulator::describe_frame_origin(jnext::save::StateDesc& d)
{
    // The frame's cycle origin. It opens the "emulator" block, and the value
    // that follows it — the folded monotonic T-state instant — is §9.5(3)
    // hand-written work the walk is split around.
    d.u64("frame_cycle", frame_cycle_);
}

void Emulator::describe_state(jnext::save::StateDesc& d)
{
    d.u32("frame_num", frame_num_);
    d.u32("boot_hold_frames_remaining", boot_hold_frames_remaining_);   // G156

    // GH #246 — the ESP outage clock. An `int` in the class, a `u32` in the
    // stream since it was added; marshalled rather than widened, because
    // widening it would move every byte after it.
    uint32_t esp_frames = static_cast<uint32_t>(esp_frames_);
    d.u32("esp_frames", esp_frames);
    esp_frames_ = static_cast<int>(esp_frames);

    d.boolean("cpu_parked", cpu_parked_);                                // GH #164
    d.u64("psg_accum", psg_accum_);
    // The Bresenham phase only. The Mixer's in-progress integration
    // accumulator is deliberately NOT snapshotted (§10.2 P6).
    d.u64("sample_accum", sample_accum_);
    d.boolean("dac_enabled", dac_enabled_);

    // G71 — the line-interrupt state is owned by `video_timing_`, and the
    // stream's layout interleaves it with `ula_int_disabled_`. An accessor
    // pair marshalled through a local: on the write path the setter stores
    // back the value it just took, which is `state_desc.h`'s write-back
    // shape (a), a no-op by construction.
    bool line_int_enable = video_timing_.line_interrupt_enable();
    d.boolean("line_interrupt_enable", line_int_enable);
    video_timing_.set_line_interrupt_enable(line_int_enable);

    d.boolean("ula_int_disabled", ula_int_disabled_);

    uint16_t line_int_target = video_timing_.line_interrupt_target();
    d.u16("line_interrupt_target", line_int_target);
    video_timing_.set_line_interrupt_target(line_int_target);

    // The deprecated IM2 shadows (the live fabric is `Im2Controller`), kept
    // because they are in the stream and the byte-identity gate freezes it.
    d.boolean("im2_hw_mode", im2_hw_mode_);
    d.u8("im2_vector_base", im2_vector_base_);
    d.bytes("im2_int_enable", im2_int_enable_, 3);
    d.bytes("im2_int_status", im2_int_status_, 3);
    d.boolean("im2_c4_expbus", im2_c4_expbus_);
    d.u8("nr_c6_uart_int_en", nr_c6_uart_int_en_);

    // The four rotating clip-window write indices (NR 0x18-0x1C).
    d.u8("clip_l2_idx", clip_l2_idx_);
    d.u8("clip_spr_idx", clip_spr_idx_);
    d.u8("clip_ula_idx", clip_ula_idx_);
    d.u8("clip_tm_idx", clip_tm_idx_);

    // IM2 DMA delay enables (NR 0xCC/0xCD/0xCE) + the latched output.
    d.boolean("nr_cc_dma_delay_on_nmi", nr_cc_dma_delay_on_nmi_);
    d.u8("nr_cc_dma_delay_en_ula", nr_cc_dma_delay_en_ula_);
    d.u8("nr_cd_dma_delay_en_ctc", nr_cd_dma_delay_en_ctc_);
    d.u8("nr_ce_dma_delay_en_uart1", nr_ce_dma_delay_en_uart1_);
    d.u8("nr_ce_dma_delay_en_uart0", nr_ce_dma_delay_en_uart0_);
    d.boolean("im2_dma_delay_latched", im2_dma_delay_latched_);

    // Branch C — the NR 0x08 read mirror (bits 5..0 of the last write).
    d.u8("nr_08_stored_low", nr_08_stored_low_);

    // Agent H — joy_iomode_pin7 (NR 0x0B / CTC ch3), VHDL zxnext.vhd:3516.
    // It lives in `IoMode` since Phase 2 Wave 2; the single-bool slot is kept
    // and sampled/restored through the accessor pair.
    bool pin7 = iomode_.pin7();
    d.boolean("joy_iomode_pin7", pin7);
    iomode_.set_pin7_for_load(pin7);
}

void Emulator::describe_nmi_tail(jnext::save::StateDesc& d)
{
    // The one field the "nmi_source" block carries beyond `NmiSource`'s own.
    d.boolean("prev_nmi_generate_n", prev_nmi_generate_n_);
}

void Emulator::describe_nextreg_appends(jnext::save::StateDesc& d)
{
    // G108 / G56-B / G55 / G112 / G113 / G135 — registers appended one at a
    // time to the end of the stream as they were implemented. They are one
    // block now, and their NAMES are what a `.jns` carries, so the
    // append-order chronology stops being load-bearing (§10.1).
    d.u8("port_ff_reg", port_ff_reg_);          // VHDL zxnext.vhd:3610-3635
    d.u8("nr_10_coreid", nr_10_coreid_);        // VHDL zxnext.vhd:1133
    d.boolean("nr_d8_io_trap_fdc_en", nr_d8_io_trap_fdc_en_);
    d.u8("nr_d9_iotrap_write", nr_d9_iotrap_write_);
    d.u8("nr_da_iotrap_cause", nr_da_iotrap_cause_);
    d.u8("nr_2d_i2s_sample", nr_2d_i2s_sample_);

    // G113 — NR 0xA2, stored OUTSIDE `I2s`'s own block so the two older
    // snapshot layouts did not shift. Accessor pair, same shape as pin7.
    uint8_t nr_a2 = i2s_.nr_a2_ctl();
    d.u8("nr_a2_pi_i2s_ctl", nr_a2);
    i2s_.set_nr_a2_ctl(nr_a2);

    d.u8("nr_a0_pi_peripheral_en", nr_a0_pi_peripheral_en_);  // zxnext.vhd:5080
}

void Emulator::describe_tail(jnext::save::StateDesc& d)
{
    // NR 0x02 bit 7. VHDL zxnext.vhd:1095 has no reset clause, so it
    // persists across resets and must round-trip.
    d.boolean("nr_02_bus_reset", nr_02_bus_reset_);
    // V20R-CPU-NIT-01 — the falling-edge shadow of the pulse-mode CPU /INT
    // poll. Without it a restore mid-pulse would see `!cur && prev` on the
    // next tick and fire a spurious request_interrupt(0xFF).
    d.boolean("prev_pulse_int_n", prev_pulse_int_n_);
}

bool Emulator::advance_to_frame_boundary()
{
    // Nothing in flight: the machine is already where a snapshot may be taken
    // from, and the ordinary running case (the GUI queueing a save to the next
    // begin_new_frame()) lands here too.
    if (!frame_in_progress_) return false;

    // Suspend the debugger for the advance. `run_frame()` returns immediately
    // while `debug_state_.paused()`, so without this nothing would move; and
    // with breakpoints still armed, one inside the remainder of the frame
    // would pause mid-frame again — the exact state this exists to leave. A
    // save is not a debugging action, and the scope restores the session
    // exactly as it found it (one-shots and the breakpoint set included).
    {
        DebugState::SuspendScope suspend(debug_state_);

        // ONE call. `run_frame()` RESUMES the half-executed frame rather than
        // restarting it (the `if (!frame_in_progress_)` guard), so the frame
        // runs to its own `frame_end` and clears the flag on the way out.
        // With the debugger suspended there is no early-return path left
        // inside it, so a loop here would be a loop that cannot iterate — and
        // a `while` around a body that can no longer fail is how a hang gets
        // written.
        run_frame();
    }
    return true;
}

// ---------------------------------------------------------------------------
// State serialisation — save_state / load_state
// ---------------------------------------------------------------------------
//
// Snapshots are taken at frame boundaries (before any events are scheduled
// into the scheduler_), so the scheduler queue is always empty here.
// Subsystem order must be identical in save_state and load_state.
//
// Input subsystem (keyboard_/joystick_/mouse_/md6_/membrane_stick_/iomode_)
// IS serialised — Task 60c — in the appended "input" sentinel block at the
// end of the stream (the MD6 FSM is ticked every instruction, so it must
// round-trip). The host-side dispatchers that FEED joystick_/mouse_
// (JoystickDispatcher/MouseDispatcher) are platform-owned, not Emulator
// members, so they are re-seeded via the on_input_state_restored callback
// rather than serialised (see load_state tail + input/*_dispatcher.cpp).
//
// NOT serialised (rewired by init() / not part of emulated state):
//   rom_          — loaded from file at startup, never changes
//   contention_   — rebuilt from config.type by build()
//   port_         — lambda callbacks only; rewired in init()
//   mixer_        — output path, discarded on rewind by design
//   debug_state_  — debugger transient state
//   trace_log_    — debug trace, not emulated state
//   call_stack_   — debug call stack, not emulated state
//   tape_/tzx_tape_/wav_tape_ — tape position independent of CPU rewind
//   video_recorder_, rzx_player_, rzx_recorder_ — recording, not state
//   sd_card_      — SERIALISED since GH #27 S6 (design §10.2 P1): the SPI
//                   FSM travels in the appended "sdcard" block at the end
//                   of the stream, so a rewind or snapshot taken mid-CMD18
//                   restores a card that is still streaming. The IMAGE
//                   itself does not travel — it is an external resource
//                   recorded by reopenable identity (§11), like the esxDOS
//                   handles below
//   boot_rom_     — loaded from file, never changes
//   framebuffer_  — regenerated by render_frame()
//   paused_vc_, paused_hc_, last_frame_vc_, last_frame_hc_ — raster transient
//   frame_ts_start_ — FUSE tstates at frame start, set at run_frame() entry

void Emulator::save_state(StateWriter& w) const
{
    // Task 60b — per-subsystem desync sentinels. A u32 of
    // kStateSentinelMagic ^ ordinal is written after every subsystem block;
    // load_state checks the same sequence (same order, same ordinals) and
    // fails loudly with the subsystem name on the first mismatch.
    //
    // Adding a subsystem to the snapshot (e.g. Task 60c input state) means:
    // append its save_state call here followed by put_sentinel(), and the
    // matching load call followed by check_sentinel("name") at the SAME
    // position in load_state — the shared ordinal counter keeps both sides
    // in lockstep automatically.
    uint32_t sentinel_ordinal = 0;
    auto put_sentinel = [&w, &sentinel_ordinal]() {
        w.write_u32(kStateSentinelMagic ^ sentinel_ordinal++);
    };

    // Core subsystems.
    clock_.save_state(w);      put_sentinel();
    ram_.save_state(w);        put_sentinel();
    mmu_.save_state(w);        put_sentinel();
    nextreg_.save_state(w);    put_sentinel();
    cpu_.save_state(w);        put_sentinel();
    im2_.save_state(w);        put_sentinel();

    // Video subsystems.
    palette_.save_state(w);    put_sentinel();
    layer2_.save_state(w);     put_sentinel();
    sprites_.save_state(w);    put_sentinel();
    tilemap_.save_state(w);    put_sentinel();
    renderer_.save_state(w);   put_sentinel();   // includes ULA

    // Peripheral subsystems.
    copper_.save_state(w);     put_sentinel();
    ctc_.save_state(w);        put_sentinel();
    dma_.save_state(w);        put_sentinel();
    spi_.save_state(w);        put_sentinel();
    i2c_.save_state(w);        put_sentinel();
    rtc_.save_state(w);        put_sentinel();
    uart_.save_state(w);       put_sentinel();
    divmmc_.save_state(w);     put_sentinel();

    // Audio subsystems.
    beeper_.save_state(w);     put_sentinel();
    turbosound_.save_state(w); put_sentinel();
    dac_.save_state(w);        put_sentinel();

    // Emulator private state — one declaration, walked (GH #27 S6, §9.2).
    // See `describe_state` above for what is here and what is not.
    jnext::save::save_via_desc_method(*this, &Emulator::describe_frame_origin,
                                      w, /*machine_level=*/true);
    // G36 review fix: the monotonic tape clock MUST travel with the
    // snapshot — without it, a rewind during --tape-realtime playback left
    // tstates_frame_base_ at its pre-rewind value and the tape clock jumped,
    // desyncing TZX/WAV playback. HAND-WRITTEN because it is §9.5(3): the
    // live FUSE tstates counter is not serialised anywhere, so the FOLDED
    // value (base + live at capture) is written and `load_state`
    // re-establishes it as the new base with a zeroed live counter —
    // monotonic_tstates() is then exactly continuous across save/load,
    // including restores taken from a mid-frame pause.
    w.write_u64(monotonic_tstates());
    jnext::save::save_via_desc_method(*this, &Emulator::describe_state, w,
                                      /*machine_level=*/true);
    put_sentinel();   // "emulator" scalar block

    // Audio Phase 1: I2s stub latched sample pair. Appended at the END
    // of the save stream (NOT interleaved with the other audio
    // subsystems) so prior saves that predate this field remain
    // readable up to the pin7 slot.
    i2s_.save_state(w);
    put_sentinel();   // "i2s"

    // NMI pipeline Phase 1 scaffold — appended after i2s for the same
    // reason (backwards-compatible append-only save layout).
    nmi_source_.save_state(w);
    jnext::save::save_via_desc_method(*this, &Emulator::describe_nmi_tail, w,
                                      /*machine_level=*/true);
    put_sentinel();   // "nmi_source"

    // The NextREG appends — one declaration, walked (GH #27 S6). They were
    // added one at a time to the end of the stream as they were implemented
    // (G108, G56-B, G55, G112, G113, G135), each with its own
    // backwards-compatibility comment; the block is one field list now.
    jnext::save::save_via_desc_method(*this, &Emulator::describe_nextreg_appends,
                                      w, /*machine_level=*/true);
    put_sentinel();   // "nextreg_appends" (G108/G56-B/G55/G112/G113/G135)

    // Wave 1 B1 — Multiface subsystem state (FFs + RAM, ~16 bytes header
    // + 8 KB RAM). Append-only: writes a sentinel `1` byte first so the
    // load path can detect presence. Older snapshots leave Multiface at
    // its constructor-init defaults (reset() called from constructor).
    w.write_u8(0x01);
    multiface_.save_state(w);
    put_sentinel();   // "multiface"

    // The tail latches — one declaration, walked (GH #27 S6).
    jnext::save::save_via_desc_method(*this, &Emulator::describe_tail, w,
                                      /*machine_level=*/true);
    put_sentinel();   // "tail" (nr_02_bus_reset_ + prev_pulse_int_n_)

    // Task 60c — input subsystem state. Previously ABSENT from snapshots,
    // so keyboard / joystick / mouse / MD6 state (including the live MD6
    // FSM counter ticked every instruction, and the in-flight keyboard
    // auto-type queue) was lost on every rewind and save/load. Appended as
    // one stable sentinel block at the very end of the stream, AFTER "tail".
    // Order here MUST match the load side exactly (paired check_sentinel).
    // Excluded: PhantomTypist belongs to the tape family (tape state is
    // excluded from snapshots by design, see header comment above); EmuFnKeys
    // is a host-transient FSM driven synchronously by host key events with no
    // cross-frame evolution. The SDL/Qt host dispatchers (JoystickDispatcher /
    // MouseDispatcher) are NOT Emulator members (they are platform-owned) so
    // they cannot travel through this stream; crucially they keep their OWN
    // shadow of the connector/wheel/button vector that would stomp a restore,
    // so they are re-seeded from the restored Joystick / KempstonMouse via the
    // on_input_state_restored callback fired at the end of load_state().
    keyboard_.save_state(w);
    joystick_.save_state(w);
    mouse_.save_state(w);
    md6_.save_state(w);
    membrane_stick_.save_state(w);
    iomode_.save_state(w);
    put_sentinel();   // "input" (keyboard/joystick/mouse/md6/membrane/iomode)

    // GH #84 / G49 — a snapshot taken inside a stackless NMI handler must
    // retain the hidden "next RETN reads NR C3:C2" latch. The public CPU
    // registers alone cannot reconstruct it: PC may be anywhere in the
    // handler and ordinary code can also run with SP two bytes below an
    // earlier value. Append after the input block so older snapshots remain
    // readable; their absent field keeps the reset default false.
    w.write_bool(cpu_.stackless_retn_active());
    put_sentinel();   // "stackless_nmi"

    // GH #251 — the joystick serial cable's CURSOR, so a rewind rewinds the
    // cable too and the replayed frames deliver exactly the bytes they
    // delivered the first time. See JoyUartSource::save_state for why this one
    // is rewound where the ESP is instead frozen. Appended last, and written
    // only when a cable is attached — the `bool` says which, so a snapshot from
    // a run without one stays two bytes long here rather than needing a version.
    w.write_bool(joy_uart_source_ != nullptr);
    if (joy_uart_source_) joy_uart_source_->save_state(w);
    put_sentinel();   // "joy_uart"

    // GH #265 — the exact interrupt timing: the CPU's /INT window (the CPU
    // block above carries only its first boundary, in the u32 the one-stamp
    // window used), the IM2 fabric's request / pulse timeline and the CTC's
    // chained triggers in flight. Appended last; an older snapshot ends
    // before it and keeps the one-stamp window and untimed defaults. The
    // window is written relative to the FUSE counter, which load_state()
    // does not restore (it re-seeds it at the next frame start); an
    // open-ended window's INT64_MAX end is written as is.
    {
        const int64_t now_ts = static_cast<int64_t>(*fuse_z80_tstates_ptr());
        const int64_t last   = cpu_.int_window_last_ts();
        w.write_u64(static_cast<uint64_t>(cpu_.int_window_first_ts() - now_ts));
        w.write_u64(static_cast<uint64_t>(last == INT64_MAX ? last : last - now_ts));
    }
    im2_.save_timing(w);
    ctc_.save_timing(w);
    put_sentinel();   // "int_timing"

    // GH #31 — --esxdos-stub-root open handles. An open host stream cannot go
    // in a snapshot, so only the reopenable (path, offset, mode) triple does,
    // plus the guest CWD; load_state reopens from it. That makes a rewind
    // across host READS exact. It cannot make one across a host WRITE exact —
    // the write already reached the disk — which is why writes are behind
    // --esxdos-stub-writable and reads are not. Appended last; a snapshot
    // taken without a root ends here with a zero count.
    {
        auto put_str = [&w](const std::string& v) {
            const uint16_t n = static_cast<uint16_t>(
                v.size() > 0xFFFF ? 0xFFFF : v.size());
            w.write_u16(n);
            w.write_bytes(reinterpret_cast<const uint8_t*>(v.data()), n);
        };
        const auto handles = esxdos_hostfs_.snapshot();
        w.write_u16(static_cast<uint16_t>(handles.size()));
        for (const auto& h : handles) {
            w.write_u8(h.handle);
            w.write_bool(h.is_dir);
            w.write_u8(h.mode);
            w.write_u64(h.position);
            put_str(h.path);
        }
        put_str(esxdos_hostfs_.cwd_for_snapshot());
    }
    put_sentinel();   // "esxdos_hostfs"

    // GH #27 S6 — the SD card's SPI FSM (design §10.2 P1, defect D1). Until
    // S6 this class had no save_state at all, so a rewind taken while a
    // loader was streaming a CMD18 multi-block read restored a card that was
    // no longer streaming and the replayed frames got R1/token framing where
    // sector bytes belonged. Appended last, after esxdos_hostfs, so every
    // byte before it is exactly where S5b left it.
    //
    // Only the PROTOCOL state travels. The mounted image is an external
    // resource: it is recorded by reopenable identity in a `.jns` manifest
    // (`media.sdcard`, §11.3) and shared in-process by a rewind, exactly as
    // the esxDOS handles above are.
    sd_card_.save_state(w);
    put_sentinel();   // "sdcard"

    // Task 60b — bounds check: a snapshot buffer smaller than the state
    // stream would previously scribble past the allocation silently; the
    // StateWriter now suppresses the write and latches a sticky flag.
    if (w.overflow()) {
        Log::emulator()->error(
            "save_state: buffer overflow — snapshot needs {} bytes but the "
            "buffer is smaller; snapshot is truncated and NOT usable",
            w.position());
    }
}

bool Emulator::load_state(StateReader& r)
{
    // Task 60b — mirror of save_state's per-subsystem sentinels. Each
    // check consumes one u32 and verifies it equals
    // kStateSentinelMagic ^ ordinal for the SAME ordinal sequence as
    // save_state. On mismatch (or an out-of-bounds read, e.g. a truncated
    // buffer) the restore is aborted with an error naming the subsystem —
    // the machine is left in a partially-restored, non-trustworthy state,
    // which is strictly better than silently loading desynced garbage
    // into every subsystem downstream.
    last_state_error_.clear();
    uint32_t sentinel_ordinal = 0;
    auto check_sentinel = [this, &r, &sentinel_ordinal](const char* name) -> bool {
        const uint32_t ordinal = sentinel_ordinal++;
        const uint32_t expect  = kStateSentinelMagic ^ ordinal;
        const uint32_t got     = r.read_u32();
        if (got != expect || r.out_of_bounds()) {
            last_state_error_ = name;
            ++state_error_generation_;   // Task 60e: fresh corruption incident
            Log::emulator()->error(
                "load_state: state sentinel mismatch after subsystem '{}' "
                "(#{}: got 0x{:08X}, expected 0x{:08X}{}) — snapshot desynced, "
                "aborting restore",
                name, ordinal, got, expect,
                r.out_of_bounds() ? ", read past end of buffer" : "");
            return false;
        }
        return true;
    };

    // Snapshots are only ever taken at a frame boundary (begin_new_frame()), so a
    // restored machine has no frame in flight — the next run_frame() must start one.
    // Without this, restoring while the debugger had a frame paused would leave the
    // flag set and the first restored frame would skip its own frame-start actions.
    frame_in_progress_ = false;

    // Core subsystems.
    clock_.load_state(r);
    if (!check_sentinel("clock")) return false;
    ram_.load_state(r);
    if (!check_sentinel("ram")) return false;
    mmu_.load_state(r);
    if (!check_sentinel("mmu")) return false;
    nextreg_.load_state(r);
    if (!check_sentinel("nextreg")) return false;
    // Task 58 — re-derive the effective NR 0x05 bit-0 latch from the
    // restored pending cache instead of serialising it (keeps the
    // snapshot format stable — same pattern as the Z80Cpu /INT-window
    // re-derive below). Valid because snapshots are taken at the frame
    // edge (begin_new_frame), where pending == effective per VHDL
    // zxnext.vhd:6696-6703.
    eff_nr_05_scandouble_en_ = (nextreg_.cached(0x05) & 0x01) != 0;
    eff_nr_09_scanlines_ = static_cast<uint8_t>(nextreg_.cached(0x09) & 0x03);
    cpu_.load_state(r);
    if (!check_sentinel("cpu")) return false;
    // Z80Cpu's /INT pulse window (zxnext.vhd:2033) is re-derived below,
    // once the effective machine timing has been restored (GH #263
    // follow-up); Im2Controller::load_state restores its own copy.
    im2_.load_state(r);
    if (!check_sentinel("im2")) return false;

    // Video subsystems.
    palette_.load_state(r);
    if (!check_sentinel("palette")) return false;
    layer2_.load_state(r);
    if (!check_sentinel("layer2")) return false;
    sprites_.load_state(r);
    if (!check_sentinel("sprites")) return false;
    tilemap_.load_state(r);
    if (!check_sentinel("tilemap")) return false;
    renderer_.load_state(r);   // includes ULA
    if (!check_sentinel("renderer")) return false;

    // Peripheral subsystems.
    copper_.load_state(r);
    if (!check_sentinel("copper")) return false;
    // G109: mirror the just-loaded NR 0x64 cu_offset onto VideoTiming so
    // the line-int comparator stays consistent with Copper across save/load.
    video_timing_.set_cu_offset(copper_.offset());
    ctc_.load_state(r);
    if (!check_sentinel("ctc")) return false;
    dma_.load_state(r);
    if (!check_sentinel("dma")) return false;
    spi_.load_state(r);
    if (!check_sentinel("spi")) return false;
    i2c_.load_state(r);
    if (!check_sentinel("i2c")) return false;
    rtc_.load_state(r);
    if (!check_sentinel("rtc")) return false;
    uart_.load_state(r);
    if (!check_sentinel("uart")) return false;
    divmmc_.load_state(r);
    if (!check_sentinel("divmmc")) return false;
    // Pass-10 verify-audit fix (2026-05-09): re-sync DivMmc::rom3_active_
    // from the canonical loaded MMU state. The flag is a feeder shadow of
    // VHDL `sram_pre_rom3` (zxnext.vhd:2981-3008,:3138) and is NOT
    // persisted by DivMmc::save_state — it is only refreshed at port-write
    // / NR-commit / machine-type-change sites that fan-out from MMU. Pre-fix
    // a load_state restoring a snapshot taken with sram_rom=3 selected
    // (e.g. ROM3 active during a CMD18 boot stream) would leave rom3_active_
    // at its constructor default `false`, breaking the ROM3-conditional
    // automap gate (sram_divmmc_automap_rom3_en, divmmc.vhd:130,148) until
    // the next MMU port write. Mirrors the i2c_.set_pi_i2c1_en /
    // spi_.set_flash_cs_enable re-sync pattern used elsewhere in
    // load_state. Class-(a) → resolved.
    divmmc_.set_rom3_active(mmu_.sram_rom3());

    // Verify12-memory class-(b) fix: re-push ContentionModel gate inputs
    // from canonical loaded state. ContentionModel itself is intentionally
    // NOT serialised — it owns derived-from-NextReg state (cpu_speed
    // shadow/effective, contention_disable shadow/effective, port_7ffd_io_en
    // gate, mem_active_page latch). Pre-fix a load_state restored snapshot
    // would leave these gates at their constructor defaults (cpu_speed=0
    // / contention_disable=false / port_7ffd_io_en=false) until the next
    // NR 0x07 / NR 0x08 / NR 0x82 write — diverging from VHDL
    // zxnext.vhd:5800-5828 where the eff_nr_08_contention_disable and
    // cpu_speed flip-flops persist across any non-reset edge. Mirrors
    // the divmmc_.set_rom3_active / spi_.set_flash_cs_enable re-sync
    // pattern used elsewhere in load_state.
    //
    // VHDL line refs:
    //   NR 0x07 cpu_speed (zxnext.vhd:5786-5828): bits[1:0] feed both
    //     pending shadow (line 5789) and effective (line 5817 commit on
    //     bus-idle). Saved in NextReg.regs_[0x07]; re-push both fields.
    //   NR 0x08 bit 6 nr_08_contention_disable (zxnext.vhd:5176, 5800-5823):
    //     Mmu's contention_disabled_ mirrors the shadow (immediate write).
    //     Re-push it as both shadow and effective for the post-load read
    //     surface (NR 0x08 read at zxnext.vhd:5906) to match VHDL.
    //   NR 0x82 bit 1 port_7ffd_io_en (zxnext.vhd:2399): Saved in
    //     NextReg.regs_[0x82]; re-push the bit-1 gate.
    //   NR 0x85 bit 0 port_ulap_io_en (zxnext.vhd:2439): Saved in
    //     NextReg.regs_[0x85]; re-push the bit-0 gate. (V16-CPU-01 — the
    //     V15-CPU-NIT-03 reviewer-promoted fix added the shadow + setter
    //     + NR 0x85 write handler + init() re-push, but missed the
    //     load_state re-push. After load, the shadow stays at whatever
    //     prior value the model held until the next NR 0x85 write —
    //     so a snapshot with NR 0x85 b0=1 restored on top of a runtime
    //     where the shadow was previously pulled to 0 leaves ULA+ port
    //     contention silent at $BF3B/$FF3B until the next NR 0x85 write.
    //     Same Verify12-memory class-(b) gap pattern; one-line completion.)
    {
        // Rebuild ContentionModel's LUT + per-machine bank decode if the
        // saved machine_type differs from what build() set up (the snapshot
        // may have been taken after an NR 0x03 commit changed the type).
        // rebuild_for_type preserves dynamic gate state, so the subsequent
        // gate re-push isn't clobbered.
        contention_.rebuild_for_type(mmu_.machine_type());

        const uint8_t cs07 = static_cast<uint8_t>(nextreg_.cached(0x07) & 0x03);
        contention_.set_cpu_speed(cs07);
        contention_.set_pending_cpu_speed(cs07);
        contention_.set_contention_disable(mmu_.contention_disabled());
        // V16-NMP-02: re-seed shadows through the effective gate so
        // restored snapshots with expbus_eff_en=1 honour the AND-mask.
        // V16-CPU-01: also re-push the ULA+ shadow (NR 0x85 b0) to mirror
        // the NR 0x85 write handler / init() path; missed pre-fix.
        contention_.set_port_7ffd_io_en(
            (effective_internal_port_enable(0x82) & 0x02) != 0);
        contention_.set_port_ulap_io_en(
            (effective_internal_port_enable(0x85) & 0x01) != 0);

        // V24-MEM-01 / V25-MEM-01 fix + V24-MEM-NIT-01 (reviewer
        // follow-up): re-sync the machine_timing axis. Two paths:
        //
        // (1) NEW-FORMAT save (Mmu schema slot present): trust the
        //     restored (effective, pending) pair. This preserves a
        //     `pending != effective` deferred-commit state captured by
        //     a snapshot taken between an NR 0x03 bits-6:4 write and
        //     the next video-frame edge (VHDL :6694-6703). Push the
        //     pair into the ContentionModel so its (effective, pending)
        //     mirrors the Mmu's. The Mmu side is already correct from
        //     load_state. Sequence: set_pending(effective) +
        //     commit_pending() promotes effective; then
        //     set_pending(pending) leaves pending != effective intact.
        //
        // (2) OLD-FORMAT save (Mmu schema slot absent, load fell
        //     through the !r.eof() guard): re-derive both fields from
        //     the NR 0x03 cached byte (forward compatibility with
        //     pre-V24 snapshots). Both shadow and effective are seeded
        //     to the tim_sel-derived MachineTimingMode — mirroring the
        //     VHDL power-on state where nr_03_machine_timing and
        //     eff_nr_03_machine_timing start equal and only diverge
        //     between an NR 0x03 write and the next frame edge (which
        //     could never have been captured by a pre-V24 save anyway).
        //
        // Mirrors the `set_cpu_speed` / `set_contention_disable` /
        // `set_port_7ffd_io_en` re-push pattern above for the
        // ContentionModel side.
        if (mmu_.machine_timing_loaded_from_schema()) {
            // Path (1): schema-restored pair is canonical.
            const MachineTimingMode eff_tim  = mmu_.machine_timing();
            const MachineTimingMode pend_tim = mmu_.pending_machine_timing();
            // Seed ContentionModel's (effective, pending) = (eff_tim,
            // pend_tim) without disturbing Mmu (already correct).
            contention_.set_pending_machine_timing(eff_tim);
            contention_.commit_pending_machine_timing();
            contention_.set_pending_machine_timing(pend_tim);
        } else {
            // Path (2): old-format fallback — re-derive from NextReg.
            const uint8_t nr_03_tim = nextreg_.nr_03_machine_timing();
            const MachineTimingMode tim_mode =
                decode_nr_03_machine_timing(nr_03_tim);
            contention_.set_machine_timing(tim_mode);
            mmu_.set_machine_timing(tim_mode);
        }
        // Task 51 — the restored effective tim_sel may differ from the
        // init()-time machine default (a rewind/load across a runtime
        // NR 0x03 timing change); re-derive the video timing constants,
        // CPU-side line geometry, master-cycle frame length and ULA
        // counter origins from it. Idempotent when nothing changed.
        //
        // Task 56 — the 50/60 Hz selection rides along: repush derives
        // refresh_60hz from the restored NR 0x05 cache bit 2 (the
        // pending `nr_05_5060` FF; NextReg::load_state ran above), so a
        // snapshot taken at 60 Hz restores 60 Hz geometry. Snapshots are
        // taken at the frame edge (begin_new_frame), where pending ==
        // effective per VHDL zxnext.vhd:6697-6700.
        repush_video_timing_from_machine_timing();
        // Z80Cpu's /INT pulse window (zxnext.vhd:2033) decodes the
        // EFFECTIVE timing (:5761-5776), which a snapshot can hold apart
        // from NR 0x03's pending value (it is taken before the frame edge's
        // commit). The flag is not serialised in Z80Cpu::save_state (it
        // would shift every later block), so re-derive it here.
        cpu_.set_machine_timing_48_or_p3(
            contention_.machine_timing() == MachineTimingMode::Timing48 ||
            contention_.machine_timing() == MachineTimingMode::TimingPlus3);
    }

    // Audio subsystems.
    beeper_.load_state(r);
    if (!check_sentinel("beeper")) return false;
    turbosound_.load_state(r);
    if (!check_sentinel("turbosound")) return false;
    dac_.load_state(r);
    if (!check_sentinel("dac")) return false;

    // Emulator private state — the same declaration, walked (GH #27 S6).
    jnext::save::load_via_desc_method(*this, &Emulator::describe_frame_origin,
                                      r, /*machine_level=*/true);
    // G36 review fix — see save_state. The saved value is the FOLDED
    // monotonic instant; re-establish it as the base and zero the live FUSE
    // counter so monotonic_tstates() is exactly the saved value. Safe:
    // restores only happen between frames (frame_in_progress_ = false above),
    // so the next run_frame() or step_frame_slot() calls begin_new_frame()
    // before any instruction consults the counter, and its
    // rebase_fuse_tstates_() re-seeds it from (clock_ - frame_cycle_) /
    // divisor while moving the same amount out of the base —
    // monotonic_tstates() stays the saved value and contention sees the
    // restored raster position.
    tstates_frame_base_ = r.read_u64();
    // GH #265 — what the CPU block restored against the counter (the /INT
    // window, the EI stamp) moves with it.
    cpu_.rebase_interrupt_window(static_cast<int64_t>(*fuse_z80_tstates_ptr()));
    *fuse_z80_tstates_ptr() = 0;

    jnext::save::load_via_desc_method(*this, &Emulator::describe_state, r,
                                      /*machine_level=*/true);
    // DERIVED, and therefore outside the declaration (§9.5(7)): the ULA
    // interrupt enable is the inverse of the NR 0x22 bit the walk just
    // restored, not a field of its own.
    video_timing_.set_interrupt_enable(!ula_int_disabled_);
    if (!check_sentinel("emulator")) return false;

    // Audio Phase 1: I2s stub latched sample pair — appended at the
    // END matching save_state().
    i2s_.load_state(r);
    if (!check_sentinel("i2s")) return false;

    // NMI pipeline Phase 1 scaffold — matches the append order in
    // save_state().
    nmi_source_.load_state(r);
    jnext::save::load_via_desc_method(*this, &Emulator::describe_nmi_tail, r,
                                      /*machine_level=*/true);
    if (!check_sentinel("nmi_source")) return false;

    // The NextREG appends — the same declaration, walked (GH #27 S6).
    //
    // The eight `if (!r.eof())` guards this replaces existed so buffers
    // written by a jnext that predated each field stayed readable. There is
    // no such buffer: this stream is the in-process `RewindBuffer`'s, whose
    // width comes from a measure pass of THIS code, and the warm-start cache
    // checks both `kFormatVersion` and an exact length before it is handed
    // here. A short stream is therefore corruption, and the walk makes it
    // LOUD — `StateReader` latches `out_of_bounds()` and the sentinel below
    // names the block — where the guards made it silently take defaults.
    jnext::save::load_via_desc_method(*this, &Emulator::describe_nextreg_appends,
                                      r, /*machine_level=*/true);
    if (!check_sentinel("nextreg_appends")) return false;
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
    if (!check_sentinel("multiface")) return false;

    // The tail latches — the same declaration, walked (GH #27 S6). Same
    // reasoning about the dropped eof() guards as the block above.
    jnext::save::load_via_desc_method(*this, &Emulator::describe_tail, r,
                                      /*machine_level=*/true);
    if (!check_sentinel("tail")) return false;

    // Task 60c — input subsystem state. Mirrors the save_state append order
    // exactly (keyboard → joystick → mouse → md6 → membrane_stick → iomode),
    // guarded by the paired "input" sentinel. See save_state() for the
    // rationale on which classes are (and are NOT) serialised.
    keyboard_.load_state(r);
    joystick_.load_state(r);
    mouse_.load_state(r);
    md6_.load_state(r);
    membrane_stick_.load_state(r);
    iomode_.load_state(r);
    if (!check_sentinel("input")) return false;

    // GH #84 / G49 — append-only hidden stackless-RETN latch. Snapshots
    // predating this field end immediately after the input sentinel and keep
    // the CPU reset default (inactive).
    if (!r.eof()) {
        cpu_.set_stackless_retn_active_for_load(r.read_bool());
        if (!check_sentinel("stackless_nmi")) return false;
    } else {
        cpu_.set_stackless_retn_active_for_load(false);
    }

    // GH #251 — the joystick serial cable's cursor. Same append-only shape as
    // the field above: a snapshot predating it ends at that sentinel and leaves
    // the cable wherever it is, which is the pre-fix behaviour and the best
    // available answer for a stream the snapshot never recorded.
    //
    // The PRESENT flag is honoured in both directions on purpose. A snapshot
    // taken without a cable must not be allowed to consume a cursor that is not
    // there (the sentinel would then fail), and a snapshot taken WITH one that
    // is restored into a machine with no cable must still step over its five
    // words. Neither mismatch can arise from a rewind — same run, same config —
    // but load_state's contract is to fail loudly rather than desynchronise, and
    // reading a cursor nobody owns would desynchronise every field after it.
    if (!r.eof()) {
        const bool had_source = r.read_bool();
        if (had_source) {
            if (joy_uart_source_) {
                joy_uart_source_->load_state(r);
            } else {
                JoyUartSource discard({0}, 0, 0);   // step over the cursor
                discard.load_state(r);
            }
        }
        if (!check_sentinel("joy_uart")) return false;
    }

    // GH #265 — exact interrupt timing (see save_state). The window comes
    // relative to the FUSE counter; put it on the counter as re-seeded
    // above, so the next frame start's rebase moves it with the counter.
    if (!r.eof()) {
        const int64_t now_ts = static_cast<int64_t>(*fuse_z80_tstates_ptr());
        const int64_t first  = static_cast<int64_t>(r.read_u64()) + now_ts;
        int64_t last         = static_cast<int64_t>(r.read_u64());
        if (last != INT64_MAX) last += now_ts;
        cpu_.set_int_window_for_load(first, last);
        im2_.load_timing(r);
        ctc_.load_timing(r);
        if (!check_sentinel("int_timing")) return false;
    }

    // GH #31 — reopen the host-directory handles (see save_state).
    if (!r.eof()) {
        auto get_str = [&r]() {
            const uint16_t n = r.read_u16();
            std::string v(n, '\0');
            if (n) r.read_bytes(reinterpret_cast<uint8_t*>(&v[0]), n);
            return v;
        };
        std::vector<EsxdosHostFs::HandleSnapshot> handles;
        const uint16_t count = r.read_u16();
        for (uint16_t i = 0; i < count && !r.out_of_bounds(); ++i) {
            EsxdosHostFs::HandleSnapshot h;
            h.handle   = r.read_u8();
            h.is_dir   = r.read_bool();
            h.mode     = r.read_u8();
            h.position = r.read_u64();
            h.path     = get_str();
            handles.push_back(std::move(h));
        }
        const std::string cwd = get_str();
        esxdos_hostfs_.restore_cwd(cwd);
        esxdos_hostfs_.restore(handles);
        if (!check_sentinel("esxdos_hostfs")) return false;
    }

    // GH #27 S6 — the SD card's SPI FSM, matching the append in save_state().
    // The mounted IMAGE is not restored from here: it is the card the host
    // has open right now, which for a rewind is the same file and for a
    // `.jns` is the one the manifest's identity checks matched (§11.3).
    if (!r.eof()) {
        sd_card_.load_state(r);
        if (!check_sentinel("sdcard")) return false;
    }

    // Pass-8 verify-audit (2026-05-09): re-sync the SpiMaster Flash-CS
    // composite gate from the canonical loaded state (nr_03_config_mode +
    // reset_type bit 2). The gate itself is NOT persisted — it's a
    // derived field — so older snapshots and current snapshots both
    // recompute it here. Mirrors the i2c_.set_pi_i2c1_en re-sync above.
    spi_.set_flash_cs_enable(
        nextreg_.nr_03_config_mode()
        || ((nmi_source_.reset_type() & 0x04) != 0));

    // Task 60b — any out-of-bounds read that slipped between sentinels
    // (all trailing fields are sentinel-guarded above, so this is a
    // belt-and-braces check) still fails the load loudly.
    if (r.out_of_bounds()) {
        last_state_error_ = "buffer-truncated";
        ++state_error_generation_;   // Task 60e: fresh corruption incident
        Log::emulator()->error(
            "load_state: read past end of snapshot buffer ({} bytes) — "
            "restore aborted", r.capacity());
        return false;
    }

    // GH #261 — cross-subsystem render history. Each video subsystem's own
    // load_state re-baselines its per-scanline logs and refills its per-line
    // snapshots from the state it just loaded; these two cannot, because
    // they need another subsystem's state. Without them the render
    // rewind_to_frame() does straight after the load (before any
    // begin_new_frame()) replays the PRE-restore frame's history.
    //
    //  * Ula's NR 0x43 b1-3 / NR 0x6B b4 selectors are mirrors of the
    //    PaletteManager's (both written by the NR 0x43 / 0x6B handlers) and
    //    are not in the Ula stream at all, so they kept their pre-restore
    //    value for good. Re-sync from the PaletteManager, then re-baseline
    //    the selector logs the setters just appended to.
    //  * The attribute mux baselines from the restored VRAM, with the same
    //    origin begin_new_frame() gives it (video timing re-pushed above).
    {
        Ula& ula = renderer_.ula();
        ula.set_active_ula_palette(palette_.active_ula_palette());
        ula.set_active_layer2_palette(palette_.active_layer2_palette());
        ula.set_active_sprite_palette(palette_.active_sprite_palette());
        ula.set_active_tilemap_palette(palette_.active_tilemap_palette());
        ula.palsel_start_frame();
        mmu_.attr_mux_start_frame(video_timing_.ula_prefetch_origin_hc(),
                                  video_timing_.vblank_top());
    }

    // Task 60c — the input subsystem (keyboard/joystick/mouse/md6/…) has now
    // been restored to the snapshot's canonical state. The host-side input
    // dispatchers (JoystickDispatcher / MouseDispatcher) keep their OWN shadow
    // of the connector/wheel/button vectors and are NOT part of this stream
    // (they are platform-owned, not Emulator members). Notify the host so it
    // can re-seed those shadows from the restored Joystick / KempstonMouse —
    // otherwise the next live controller/wheel/button event would push the
    // dispatcher's stale shadow back over the restore. No-op when unset
    // (headless / unit tests / GUI-less builds).
    if (on_input_state_restored) {
        on_input_state_restored();
    }

    // GH #246 — the one place the ESP's association can disagree with the
    // schedule WITHOUT an edge having been crossed: the restore just moved the
    // outage clock, so the module has to be told where it now is. Forced,
    // because the edge test compares frame N with frame N-1 and a restore is a
    // jump. Idempotent when nothing moved.
    sync_esp_association(/*force=*/true);
    return true;
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


bool Emulator::rzx_blocks_rewind(const char* what) const
{
    // An RZX recording replays the input of ONE continuous run; a rewind
    // makes the machine's history non-continuous, so recording on across it
    // gives a file that no longer replays, and a playback's position does not
    // rewind with the machine. Refused, and said — the debugger greys the
    // actions out too (DebuggerWindow::update_actions()).
    if (!rzx_recorder_.is_recording() && !rzx_player_.is_playing()) return false;
    Log::emulator()->error("{}: not while an RZX recording {} (stop it first)", what,
                           rzx_recorder_.is_recording() ? "is being made" : "is playing");
    return true;
}

uint64_t Emulator::rewind_to_cycle(uint64_t target_cycle)
{
    if (rzx_blocks_rewind("rewind_to_cycle")) return UINT64_MAX;
    if (!rewind_buffer_ || rewind_buffer_->empty()) {
        Log::emulator()->warn("rewind_to_cycle: rewind buffer is empty or disabled");
        return UINT64_MAX;
    }

    // Restore the nearest snapshot at or before target_cycle.
    uint64_t snap_cycle = rewind_buffer_->restore_nearest(target_cycle, *this);

    // Task 60b: the restore failed sentinel/bounds verification — do NOT
    // replay and pause-as-successful. The machine is partially restored
    // (documented limitation, no rollback): pause it so the torn state
    // cannot keep executing, and surface the failure to the caller.
    if (snap_cycle == UINT64_MAX) {
        Log::emulator()->error(
            "rewind_to_cycle: snapshot restore failed — rewind aborted, "
            "machine paused in a non-trustworthy state");
        debug_state_.set_active(true);
        debug_state_.pause();
        return UINT64_MAX;
    }

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
    if (rzx_blocks_rewind("step_back")) return false;

    if (!rewind_buffer_ || rewind_buffer_->empty()) {
        Log::emulator()->warn("step_back: rewind buffer is empty or disabled");
        return false;
    }

    if (!trace_log_.enabled() || trace_log_.size() == 0) {
        // Loud failure (Task 27 A2): rewind is on but the instruction trace
        // is not — step_back cannot locate the target instruction without
        // it. This only happens if the trace was manually disabled after
        // rewind was enabled (rewind enable always switches the trace on).
        Log::emulator()->error(
            "step_back: instruction trace log is {} — cannot locate the "
            "target instruction; re-enable it (debugger: Debug > Enable "
            "Trace) and execute forward before stepping back",
            trace_log_.enabled() ? "empty" : "disabled");
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

    // Task 60b: propagate a failed restore — pre-fix this returned true
    // unconditionally, so the debugger reported a successful step while
    // the machine held a torn, partially-restored state.
    if (rewind_to_cycle(target_cycle) == UINT64_MAX) {
        Log::emulator()->error("step_back: snapshot restore failed — step aborted");
        return false;
    }
    return true;
}

bool Emulator::rewind_to_frame(uint32_t target_frame_num)
{
    if (rzx_blocks_rewind("rewind_to_frame")) return false;
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
    // Task 60b: propagate a failed restore (pre-fix the result was
    // discarded and the function returned true regardless).
    if (snap_cycle == UINT64_MAX) {
        Log::emulator()->error(
            "rewind_to_frame: snapshot restore failed — rewind aborted, "
            "machine paused in a non-trustworthy state");
        debug_state_.set_active(true);
        debug_state_.pause();
        return false;
    }

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
    if (snap == UINT64_MAX) {
        // Task 60b: same propagation as above — never pause-as-successful
        // on a failed restore.
        Log::emulator()->error(
            "rewind_to_frame: snapshot restore failed — rewind aborted, "
            "machine paused in a non-trustworthy state");
        debug_state_.set_active(true);
        debug_state_.pause();
        return false;
    }

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
