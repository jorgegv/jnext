#include "core/emulator.h"

#include "core/log.h"
#include "core/nex_loader.h"
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

Emulator::Emulator() : mmu_(ram_, rom_), cpu_(mmu_, port_) {}

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
    framebuffer_.assign(FRAMEBUFFER_PIXELS_MAX, 0xFF000000u);
    last_frame_width_ = FRAMEBUFFER_WIDTH;

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
    beeper_.reset();
    turbosound_.reset();
    dac_.reset();
    mixer_.reset();
    ctc_.reset();
    dma_.reset();
    spi_.reset();
    i2c_.reset();
    uart_.reset();
    divmmc_.reset();
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

    // Reset line interrupt and IM2 hardware mode state.
    line_int_enabled_ = false;
    ula_int_disabled_ = false;
    line_int_value_ = 0;
    im2_hw_mode_ = false;
    im2_vector_base_ = 0;
    im2_int_enable_[0] = 0x81;  // soft reset default: ULA + expansion bus enabled
    im2_int_enable_[1] = 0;
    im2_int_enable_[2] = 0;
    im2_int_status_[0] = 0;
    im2_int_status_[1] = 0;
    im2_int_status_[2] = 0;

    // Build contention LUT for the selected machine type.
    // MachineType is shared between emulator_config.h and contention.h
    // (emulator_config.h now includes contention.h for this definition).
    // build() also seeds pentagon_timing_ from MachineType (VHDL
    // zxnext.vhd:4481 machine_timing_pentagon); seed cpu_speed from the
    // boot-time config so i_contention_en matches the initial NR 0x07
    // state (VHDL zxnext.vhd:1300 cpu_speed power-on "00"). NR 0x03 does
    // not currently have a runtime machine-type commit path, so the
    // pentagon_timing seeding from build() is sufficient for now.
    contention_.build(cfg.type);
    contention_.set_cpu_speed(static_cast<uint8_t>(cfg.cpu_speed) & 0x03);

    // Push machine type into Mmu so Mmu::current_sram_rom() matches the
    // VHDL zxnext.vhd:2981-3008 sram_rom selection (48K always 0, +3 uses
    // 2-bit rom bank, Next/128K/Pentagon use 1-bit). Altrom lock bits
    // override per VHDL for +3 and Next variants.
    mmu_.set_machine_type(cfg.type);

    // Build FUSE Z80 core's internal contention tables.  These provide
    // per-access contention for opcode fetches, data reads/writes, and
    // internal timing delays — matching real hardware more accurately
    // than the external callback approach.
    z80_build_contention_tables(cfg.type);

    // Clear all port dispatch handlers before re-registering them.
    // Without this, reset() → init() would duplicate every handler, causing
    // double-fired writes (breaking auto-increment ports like sprites/palette).
    port_.clear_handlers();

    // Floating bus: unmatched port reads return ULA bus value in 48K/128K modes.
    port_.set_default_read([this](uint16_t) -> uint8_t {
        return floating_bus_read();
    });

    // Memory contention is now handled by the FUSE Z80 core's built-in
    // contend_read/contend_write macros (ula_contention[] tables).
    // No external callback needed.
    cpu_.on_contention = nullptr;

    // DivMMC auto-map must fire BEFORE the opcode fetch so the memory
    // overlay is active for the same M1 read that triggered it (matching
    // the VHDL combinatorial address decode).
    cpu_.on_m1_prefetch = [this](uint16_t pc) {
        divmmc_.check_automap(pc, true);
    };

    // Install M1-cycle callback for RETI detection (ED 4D sequence) and
    // RETN detection (ED 45 + undocumented aliases). RETI notifies the
    // Im2Controller so it can clear the active interrupt level in the daisy
    // chain; RETN notifies DivMmc so it can clear automap hold/held per
    // VHDL divmmc.vhd:126,139 (i_retn_seen).
    cpu_.on_m1_cycle = [this](uint16_t pc, uint8_t opcode) {
        static bool saw_ed = false;
        if (opcode == 0xED) {
            saw_ed = true;
        } else {
            if (saw_ed) {
                if (opcode == 0x4D) {
                    im2_.on_reti();
                } else if (opcode == 0x45 || opcode == 0x55 || opcode == 0x5D
                        || opcode == 0x65 || opcode == 0x6D || opcode == 0x75
                        || opcode == 0x7D) {
                    // RETN (documented 0x45 + undocumented aliases per
                    // src/debug/disasm.cpp:890-898)
                    divmmc_.on_retn();
                }
            }
            saw_ed = false;
        }
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
    nextreg_.set_write_handler(0x07, [this](uint8_t v) {
        CpuSpeed speed = static_cast<CpuSpeed>(v & 0x03);
        Log::emulator()->info("CPU speed changed to {} (NextREG 0x07={:#04x})", cpu_speed_str(speed), v);
        clock_.set_cpu_speed(speed);
        contention_.set_cpu_speed(v & 0x03);
    });

    // Register 0x12: Layer 2 active RAM bank
    nextreg_.set_write_handler(0x12, [this](uint8_t v) {
        layer2_.set_active_bank(v);
    });

    // Register 0x13: Layer 2 shadow RAM bank
    nextreg_.set_write_handler(0x13, [this](uint8_t v) {
        layer2_.set_shadow_bank(v);
    });

    // Register 0x14: Global transparency colour (Layer2/ULA/LoRes)
    // VHDL zxnext.vhd:7100 — compared against palette RGB[8:1] for
    // ULA and Layer 2 transparency at the compositor stage.
    nextreg_.set_write_handler(0x14, [this](uint8_t v) {
        palette_.set_global_transparency(v);
        renderer_.set_transparent_rgb(v);
    });

    // Register 0x40: Palette index
    nextreg_.set_write_handler(0x40, [this](uint8_t v) {
        palette_.set_index(v);
    });

    // Register 0x41: Palette value 8-bit (RRRGGGBB)
    nextreg_.set_write_handler(0x41, [this](uint8_t v) {
        palette_.write_8bit(v);
    });
    // VHDL zxnext.vhd:6038-6039 — NR 0x41 read returns nr_palette_dat(8:1),
    // i.e. the upper 8 bits of the stored RGB333 at the currently selected
    // target palette + index. Without this handler, reads return the stale
    // regs_[0x41] last-write value, masking palette-state bugs (PAL-01/03/06).
    nextreg_.set_read_handler(0x41, [this]() -> uint8_t {
        return palette_.read_8bit();
    });

    // Register 0x43: Palette control
    nextreg_.set_write_handler(0x43, [this](uint8_t v) {
        palette_.write_control(v);
    });

    // Register 0x44: Palette value 9-bit (two consecutive writes)
    nextreg_.set_write_handler(0x44, [this](uint8_t v) {
        palette_.write_9bit(v);
    });

    // Register 0x4B: Sprite transparency index
    nextreg_.set_write_handler(0x4B, [this](uint8_t v) {
        palette_.set_sprite_transparency(v);
    });

    // Register 0x4C: Tilemap transparency index
    nextreg_.set_write_handler(0x4C, [this](uint8_t v) {
        palette_.set_tilemap_transparency(v);
    });

    // Register 0x16: Layer 2 X scroll LSB
    nextreg_.set_write_handler(0x16, [this](uint8_t v) {
        layer2_.set_scroll_x_lsb(v);
    });

    // Register 0x17: Layer 2 Y scroll
    nextreg_.set_write_handler(0x17, [this](uint8_t v) {
        layer2_.set_scroll_y(v);
    });

    // Register 0x70: Layer 2 control (resolution + palette offset)
    nextreg_.set_write_handler(0x70, [this](uint8_t v) {
        layer2_.set_control(v);
    });

    // Register 0x71: Layer 2 X scroll MSB
    nextreg_.set_write_handler(0x71, [this](uint8_t v) {
        layer2_.set_scroll_x_msb(v);
    });

    // Register 0x09: Peripheral 4 setting (bit 3 = sprites over border)
    nextreg_.set_write_handler(0x09, [this](uint8_t v) {
        sprites_.set_over_border((v & 0x08) != 0);
    });

    // Register 0x0A: Peripheral 2 / SD-card swap
    //   bit 5 = sd_swap — invert SD0/SD1 mapping on port 0xE7 writes
    // VHDL zxnext.vhd:3308-3322 (port_e7 decode uses nr_0a_sd_swap).
    // Other bits of NR 0x0A (mouse / Kempston-II config) are not wired yet.
    nextreg_.set_write_handler(0x0A, [this](uint8_t v) {
        spi_.set_sd_swap((v & 0x20) != 0);
    });

    // Register 0x15: Sprite and layer system setup
    //   bit 7 = LoRes enable (deferred)
    //   bit 6 = sprite priority (0=sprite 0 on top when zero_on_top)
    //   bit 5 = sprite border clip enable
    //   bits 4:2 = layer priority (SLU/LSU/SUL/LUS/USL/ULS)
    //   bit 1 = sprites over border
    //   bit 0 = sprites visible
    nextreg_.set_write_handler(0x15, [this](uint8_t v) {
        sprites_.set_zero_on_top((v & 0x40) != 0);
        sprites_.set_border_clip_en((v & 0x20) != 0);  // bit 5 — VHDL sprites.vhd 1044
        sprites_.set_over_border((v & 0x02) != 0);
        sprites_.set_sprites_visible((v & 0x01) != 0);
        renderer_.set_sprite_en((v & 0x01) != 0);      // VHDL 6934/7118
        renderer_.set_layer_priority((v >> 2) & 0x07);
    });

    // Registers 0x18-0x1B: Clip windows (4-write rotating: X1, X2, Y1, Y2)
    // Register 0x18: Layer 2 clip window
    nextreg_.set_write_handler(0x18, [this](uint8_t v) {
        switch (clip_l2_idx_) {
            case 0: layer2_.set_clip_x1(v); break;
            case 1: layer2_.set_clip_x2(v); break;
            case 2: layer2_.set_clip_y1(v); break;
            case 3: layer2_.set_clip_y2(v); break;
        }
        clip_l2_idx_ = (clip_l2_idx_ + 1) & 0x03;
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
    nextreg_.set_write_handler(0x19, [this](uint8_t v) {
        switch (clip_spr_idx_) {
            case 0: sprites_.set_clip_x1(v); break;
            case 1: sprites_.set_clip_x2(v); break;
            case 2: sprites_.set_clip_y1(v); break;
            case 3: sprites_.set_clip_y2(v); break;
        }
        clip_spr_idx_ = (clip_spr_idx_ + 1) & 0x03;
    });

    // Register 0x1A: ULA/LoRes clip window
    nextreg_.set_write_handler(0x1A, [this](uint8_t v) {
        switch (clip_ula_idx_) {
            case 0: renderer_.ula().set_clip_x1(v); break;
            case 1: renderer_.ula().set_clip_x2(v); break;
            case 2: renderer_.ula().set_clip_y1(v); break;
            case 3: renderer_.ula().set_clip_y2(v); break;
        }
        clip_ula_idx_ = (clip_ula_idx_ + 1) & 0x03;
    });

    // Register 0x1B: Tilemap clip window
    nextreg_.set_write_handler(0x1B, [this](uint8_t v) {
        switch (clip_tm_idx_) {
            case 0: tilemap_.set_clip_x1(v); break;
            case 1: tilemap_.set_clip_x2(v); break;
            case 2: tilemap_.set_clip_y1(v); break;
            case 3: tilemap_.set_clip_y2(v); break;
        }
        clip_tm_idx_ = (clip_tm_idx_ + 1) & 0x03;
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
    nextreg_.set_write_handler(0x1C, [this](uint8_t v) {
        if (v & 0x01) clip_l2_idx_  = 0;
        if (v & 0x02) clip_spr_idx_ = 0;
        if (v & 0x04) clip_ula_idx_ = 0;
        if (v & 0x08) clip_tm_idx_  = 0;
    });

    // Register 0x34: Sprite attribute slot select (alternative to port 0x303B)
    nextreg_.set_write_handler(0x34, [this](uint8_t v) { sprites_.set_attr_slot(v); });

    // Registers 0x75-0x79: Direct sprite attribute byte writes
    for (int i = 0; i < 5; ++i) {
        nextreg_.set_write_handler(static_cast<uint8_t>(0x75 + i),
            [this, i](uint8_t v) { sprites_.write_attr_byte(static_cast<uint8_t>(i), v); });
    }

    // Register 0x2F: Tilemap X scroll MSB (bits 1:0)
    nextreg_.set_write_handler(0x2F, [this](uint8_t v) { tilemap_.set_scroll_x_msb(v); });

    // Register 0x30: Tilemap X scroll LSB
    nextreg_.set_write_handler(0x30, [this](uint8_t v) { tilemap_.set_scroll_x_lsb(v); });

    // Register 0x31: Tilemap Y scroll
    nextreg_.set_write_handler(0x31, [this](uint8_t v) { tilemap_.set_scroll_y(v); });

    // Register 0x6B: Tilemap control
    nextreg_.set_write_handler(0x6B, [this](uint8_t v) {
        tilemap_.set_control(v);
        renderer_.set_tm_enabled((v & 0x80) != 0);  // VHDL 7130: stencil gate
        // VHDL nr_6b_tm_palette_select (bit 4) — drives the tilemap palette
        // lookup at render time.  Must come from NR 0x6B, NOT from NR 0x43.
        palette_.set_active_tilemap_palette((v & 0x10) != 0);
    });

    // Register 0x6C: Tilemap default attribute
    nextreg_.set_write_handler(0x6C, [this](uint8_t v) { tilemap_.set_default_attr(v); });

    // Register 0x6E: Tilemap base address
    nextreg_.set_write_handler(0x6E, [this](uint8_t v) { tilemap_.set_map_base(v); });

    // Register 0x6F: Tile definitions base address
    nextreg_.set_write_handler(0x6F, [this](uint8_t v) { tilemap_.set_def_base(v); });

    // Registers 0x60-0x63: Copper co-processor
    nextreg_.set_write_handler(0x60, [this](uint8_t v) { copper_.write_reg_0x60(v); });
    nextreg_.set_write_handler(0x61, [this](uint8_t v) { copper_.write_reg_0x61(v); });
    nextreg_.set_write_handler(0x62, [this](uint8_t v) { copper_.write_reg_0x62(v); });
    nextreg_.set_write_handler(0x63, [this](uint8_t v) { copper_.write_reg_0x63(v); });

    // Register 0x64: Copper vertical line offset (NR 0x64).
    // VHDL: zxnext.vhd:5442 (write), :6090 (read-back), :6723 (wired
    //       into zxula_timing.vhd i_cu_offset).
    nextreg_.set_write_handler(0x64, [this](uint8_t v) { copper_.write_reg_0x64(v); });
    nextreg_.set_read_handler (0x64, [this]() -> uint8_t { return copper_.read_reg_0x64(); });

    // Program the Copper c_max_vc wrap to match the active machine timing.
    // Per zxula_timing.vhd the wrap is (lines_per_frame - 1). This is a
    // config-time setter; Copper::reset() intentionally does not clear it.
    copper_.set_c_max_vc(timing_.lines_per_frame - 1);

    // Registers 0x50–0x57: MMU slot→page mapping (one register per slot)
    // Page 0xFF = map ROM into the slot (VHDL: mmu_A21_wr_en = '0' when page = xFF).
    for (int i = 0; i < 8; ++i) {
        nextreg_.set_write_handler(static_cast<uint8_t>(0x50 + i),
            [this, i](uint8_t v) {
                if (v == 0xFF)
                    mmu_.map_rom(i, static_cast<uint8_t>(i < 2 ? i : 0));
                else
                    mmu_.set_page(i, v);
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
    nextreg_.set_write_handler(0x22, [this](uint8_t v) {
        ula_int_disabled_ = (v & 0x04) != 0;
        line_int_enabled_ = (v & 0x02) != 0;
        line_int_value_ = (line_int_value_ & 0xFF) | ((v & 0x01) << 8);
    });

    // Register 0x23: Line interrupt value LSB
    nextreg_.set_write_handler(0x23, [this](uint8_t v) {
        line_int_value_ = (line_int_value_ & 0x100) | v;
    });

    // Register 0x02: Reset control.
    //   bit 0 = RESET_SOFT (tbblue hardware.h) → preserve SRAM, drop FFs
    //   bit 1 = RESET_HARD → full hard reset (same as power-on)
    //   bit 7 = RESET_ESPBUS → peripheral-bus ESP reset signal, no-op here
    // Hard takes priority over soft if both bits are set (unusual but
    // VHDL-faithful: a hard reset supersedes a soft reset).
    nextreg_.set_write_handler(0x02, [this](uint8_t v) {
        if (v & 0x02) {
            Log::emulator()->info("Hard reset triggered via NextREG 0x02 ({:#04x})", v);
            reset();
        } else if (v & 0x01) {
            Log::emulator()->info("Soft reset triggered via NextREG 0x02 ({:#04x})", v);
            soft_reset();
        }
        // bit 7 alone (RESET_ESPBUS) is intentionally ignored — no ESP.
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
    nextreg_.set_write_handler(0x03, [this](uint8_t v) {
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
    nextreg_.set_write_handler(0x04, [this](uint8_t v) {
        nextreg_.set_nr_04_romram_bank(v);
        mmu_.set_nr_04_romram_bank(v);
        Log::emulator()->debug("NextREG 0x04 ← {:#04x}  (romram_bank)", v);
    });

    // Registers 0x35-0x39: Sprite attribute bytes 0-4 (with auto-increment)
    for (int i = 0; i < 5; ++i) {
        nextreg_.set_write_handler(static_cast<uint8_t>(0x35 + i),
            [this, i](uint8_t v) { sprites_.write_attr_byte(static_cast<uint8_t>(i), v); });
    }

    // Register 0x4A: Fallback colour (used when all layers are transparent)
    nextreg_.set_write_handler(0x4A, [this](uint8_t v) {
        renderer_.set_fallback_colour(v);
    });

    // Register 0x68: ULA control
    //   bit 7 = ULA disable (0=enable, 1=disable)
    //   bit 6:5 = blend mode (00=normal, 01=ULA/tilemap stencil, 10=L2 forced, 11=reserved)
    //   bit 3 = ULA+ enable
    //   bits 2:0 = reserved
    nextreg_.set_write_handler(0x68, [this](uint8_t v) {
        renderer_.ula().set_ula_enabled((v & 0x80) == 0);
        // VHDL 7112: ula_blend_mode bits 6:5 (stencil when bit 0 set)
        renderer_.set_stencil_mode((v & 0x01) != 0);
    });

    // Register 0x69: Display Control 1
    //   bit 7 = Layer 2 enable
    //   bit 6 = ULA shadow display (bank 7)
    //   bits 5:4 = Timex modes (deferred)
    //   bits 3:0 = reserved
    nextreg_.set_write_handler(0x69, [this](uint8_t v) {
        layer2_.set_enabled((v & 0x80) != 0);
    });

    // --- DivMMC automap config (NextREG 0xB8-0xBB) ---

    nextreg_.set_write_handler(0xB8, [this](uint8_t v) { divmmc_.set_entry_points_0(v); });
    nextreg_.set_write_handler(0xB9, [this](uint8_t v) { divmmc_.set_entry_valid_0(v); });
    nextreg_.set_write_handler(0xBA, [this](uint8_t v) { divmmc_.set_entry_timing_0(v); });
    nextreg_.set_write_handler(0xBB, [this](uint8_t v) { divmmc_.set_entry_points_1(v); });

    // Register 0x85: Port-enable register 4 — read packing.
    // VHDL zxnext.vhd:6138: read returns reset_type & "000" & enable(3:0).
    // Bits 6:4 always read back as zero.
    nextreg_.set_read_handler(0x85, [this]() -> uint8_t {
        return nextreg_.cached(0x85) & 0x8F;
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
    nextreg_.set_write_handler(0x8C, [this](uint8_t v) {
        mmu_.set_nr_8c(v);
        rom_.set_alt_rom_config(v);
    });
    nextreg_.set_read_handler(0x8C, [this]() -> uint8_t {
        return mmu_.get_nr_8c();
    });

    // Register 0x8E: Unified paging (VHDL zxnext.vhd:3662-3671 / 3696-3704 /
    // 3726-3734 writes; read-back at zxnext.vhd:6158-6159).
    //   Write: decomposes into 7FFD/DFFD/1FFD register updates; bit 3 =
    //          bank-select enable, bit 2 = special mode, bypasses 7FFD lock.
    //   Read:  re-composes from current 7FFD/DFFD/1FFD state; bit 3 = '1'.
    nextreg_.set_write_handler(0x8E, [this](uint8_t v) {
        mmu_.write_nr_8e(v);
    });
    nextreg_.set_read_handler(0x8E, [this]() -> uint8_t {
        return mmu_.read_nr_8e();
    });

    // Register 0x8F: Mapping Mode (VHDL zxnext.vhd:3787-3794 / 6162).
    //   bits 1:0 = mapping mode — 00 standard, 10 Pentagon-512, 11
    //              Pentagon-1024. Bits 7:2 always read as 0.
    nextreg_.set_write_handler(0x8F, [this](uint8_t v) {
        mmu_.write_nr_8f(v);
    });
    nextreg_.set_read_handler(0x8F, [this]() -> uint8_t {
        return static_cast<uint8_t>(mmu_.nr_8f_mode() & 0x03);
    });

    // --- NextREG interrupt control registers (0xC0-0xCF) ---

    // Register 0xC0: Interrupt control
    //   bits 7:5 = programmable im2 vector high bits
    //   bit 0 = hw im2 mode enable
    nextreg_.set_write_handler(0xC0, [this](uint8_t v) {
        im2_vector_base_ = v & 0xE0;
        im2_hw_mode_ = (v & 0x01) != 0;
    });
    nextreg_.set_read_handler(0xC0, [this]() -> uint8_t {
        uint8_t v = im2_vector_base_ & 0xE0;
        // bits 2:1 = current interrupt mode (read-only)
        v |= (cpu_.get_registers().IM & 0x03) << 1;
        if (im2_hw_mode_) v |= 0x01;
        return v;
    });

    // Registers 0xC4-0xC6: Interrupt enable
    nextreg_.set_write_handler(0xC4, [this](uint8_t v) {
        // bit 0 = ULA, bit 1 = Line
        im2_int_enable_[0] = v;
    });
    nextreg_.set_write_handler(0xC5, [this](uint8_t v) {
        // bits 7:0 = CTC channels 7:0
        im2_int_enable_[1] = v;
    });
    nextreg_.set_write_handler(0xC6, [this](uint8_t v) {
        // bit 6=UART1 Tx, bit 4=UART1 Rx, bit 2=UART0 Tx, bit 0=UART0 Rx
        im2_int_enable_[2] = v;
    });

    // Registers 0xC8-0xCA: Interrupt status (read=status, write=clear)
    for (int i = 0; i < 3; ++i) {
        nextreg_.set_read_handler(static_cast<uint8_t>(0xC8 + i), [this, i]() -> uint8_t {
            return im2_int_status_[i];
        });
        nextreg_.set_write_handler(static_cast<uint8_t>(0xC8 + i), [this, i](uint8_t v) {
            // Writing set bits clears the corresponding status bits
            im2_int_status_[i] &= ~v;
        });
    }

    // Registers 0xCC/0xCD/0xCE: IM2 DMA delay enables
    // VHDL zxnext.vhd:5629-5637 (write), :6257-6263 (read).
    nextreg_.set_write_handler(0xCC, [this](uint8_t v) {
        nr_cc_dma_delay_on_nmi_ = (v & 0x80) != 0;
        nr_cc_dma_delay_en_ula_ = v & 0x03;
    });
    nextreg_.set_read_handler(0xCC, [this]() -> uint8_t {
        return static_cast<uint8_t>((nr_cc_dma_delay_on_nmi_ ? 0x80 : 0) |
                                    (nr_cc_dma_delay_en_ula_ & 0x03));
    });
    nextreg_.set_write_handler(0xCD, [this](uint8_t v) {
        nr_cd_dma_delay_en_ctc_ = v;
    });
    nextreg_.set_read_handler(0xCD, [this]() -> uint8_t {
        return nr_cd_dma_delay_en_ctc_;
    });
    nextreg_.set_write_handler(0xCE, [this](uint8_t v) {
        nr_ce_dma_delay_en_uart1_ = (v >> 4) & 0x07;
        nr_ce_dma_delay_en_uart0_ = v & 0x07;
    });
    nextreg_.set_read_handler(0xCE, [this]() -> uint8_t {
        return static_cast<uint8_t>(((nr_ce_dma_delay_en_uart1_ & 0x07) << 4) |
                                    (nr_ce_dma_delay_en_uart0_ & 0x07));
    });

    // Register 0x20: Generate maskable interrupt / read pending status
    nextreg_.set_read_handler(0x20, [this]() -> uint8_t {
        // bit 7=line, bit 6=ula, bits 3:0=ctc 3:0
        uint8_t v = 0;
        if (im2_int_status_[0] & 0x02) v |= 0x80;  // line
        if (im2_int_status_[0] & 0x01) v |= 0x40;  // ula
        v |= (im2_int_status_[1] & 0x0F);           // ctc 3:0
        return v;
    });
    nextreg_.set_write_handler(0x20, [this](uint8_t v) {
        // Writing set bits forces immediate interrupt generation
        if (v & 0x80) im2_.raise(Im2Level::LINE_IRQ);
        if (v & 0x40) im2_.raise(Im2Level::FRAME_IRQ);
        if (v & 0x01) im2_.raise(Im2Level::CTC_0);
        if (v & 0x02) im2_.raise(Im2Level::CTC_1);
        if (v & 0x04) im2_.raise(Im2Level::CTC_2);
        if (v & 0x08) im2_.raise(Im2Level::CTC_3);
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
    // VHDL zxnext.vhd:3904-3928. When cpu_do(4)=0:
    //   bit 0 = write-map enable (CPU writes to L2 RAM banks)
    //   bit 1 = Layer 2 visible
    //   bit 2 = read-map  enable (CPU reads from L2 RAM banks — Phase 2 D2)
    //   bit 3 = shadow-bank select (not on Mmu surface)
    //   bits 7:6 = segment select (shared read+write)
    // When cpu_do(4)=1: bits 2:0 are the Layer 2 offset register (not
    // implemented on the Mmu surface — see project docs for D2 scope).
    // Read-side port handler (port_123b_dat at VHDL:3933) is pre-existing
    // divergence and out of D2 scope; nullptr preserved.
    port_.register_handler(0xFFFF, 0x123B,
        nullptr,
        [this](uint16_t, uint8_t val) {
            layer2_.set_enabled((val & 0x02) != 0);
            mmu_.set_l2_port(val, layer2_.active_bank());
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
            // Update FUSE Z80 core's memory page contention flags for 0xC000-0xFFFF.
            z80_set_page_contended(6, slot3_contended);
            z80_set_page_contended(7, slot3_contended);
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
                for (int slot = 0; slot < 4; ++slot)
                    contention_.set_contended_slot(slot, configs[config][slot] >= 4);
            } else {
                // Normal paging: slot 0 = ROM (not contended), slot 1 = bank 5 (contended),
                // slot 2 = bank 2 (not contended), slot 3 = per 0x7FFD bank
                contention_.set_contended_slot(0, false);
                contention_.set_contended_slot(1, true);
                contention_.set_contended_slot(2, false);
                // Slot 3 contention stays as set by 0x7FFD handler
            }
        });

    // NextREG select — full 16-bit match on port 0x243B.
    port_.register_handler(0xFFFF, 0x243B,
        nullptr,
        [this](uint16_t, uint8_t v) { nextreg_.select(v); });

    // NextREG data — full 16-bit match on port 0x253B.
    port_.register_handler(0xFFFF, 0x253B,
        [this](uint16_t) -> uint8_t { return nextreg_.read_selected(); },
        [this](uint16_t, uint8_t v)  { nextreg_.write_selected(v); });

    // ULA — port 0xFE (mask 0x00FF, value 0x00FE).
    // Read: bits 7-5 always 1; bit 6 = EAR input (1 = no tape signal);
    //       bits 4-0 = keyboard rows for selected addresses (active-low).
    // Write: bits 2-0 = border colour; bit 4 = MIC; bit 3 = EAR/beeper.
    port_.register_handler(0x00FF, 0x00FE,
        [this](uint16_t port) -> uint8_t {
            uint8_t addr_high = static_cast<uint8_t>(port >> 8);
            uint8_t result = 0xE0 | (keyboard_.read_rows(addr_high) & 0x1F);
            // Bit 6 = EAR input: 1 = no signal, 0 = signal present.
            // During real-time tape playback, feed the tape EAR bit.
            if (tape_.is_playing()) {
                result = (result & ~0x40) | (tape_.tick_realtime(0) << 6);
            } else if (tzx_tape_.is_playing()) {
                // Use FUSE's live T-state counter (advances mid-instruction during I/O).
                uint64_t cpu_clocks = static_cast<uint64_t>(*fuse_z80_tstates_ptr());
                result = (result & ~0x40) | (tzx_tape_.update(cpu_clocks) << 6);
            } else if (wav_tape_.is_playing()) {
                uint64_t cpu_clocks = static_cast<uint64_t>(*fuse_z80_tstates_ptr());
                result = (result & ~0x40) | (wav_tape_.get_ear_bit(cpu_clocks) << 6);
            }
            return result;
        },
        [this](uint16_t, uint8_t val) {
            renderer_.ula().set_border(val & 0x07);
            beeper_.set_ear((val >> 4) & 1);
            beeper_.set_mic((val >> 3) & 1);
        });

    // Timex screen mode — port 0xFF (full 16-bit match).
    // Write: bits 5:3 = video mode (0=standard, 1=standard_1, 2=hi-colour, 6=hi-res).
    //        bits 2:0 = screen bank (0=primary 0x4000, 1=alternate 0x6000).
    // Read is not implemented on real hardware; omit read handler.
    // VHDL zxnext.vhd:2397: port_ff_io_en <= internal_port_enable(0) = NR 0x82 bit 0.
    port_.register_handler(0xFFFF, 0x00FF,
        nullptr,
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x82) & 0x01) == 0) return;
            renderer_.ula().set_screen_mode(val);
        });

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
    // Phase 2 A: forwards to Mmu::write_port_dffd which stores bits 4:0 and
    // re-composes MMU6/7 with the extra bank bits per VHDL:3763-3766.
    port_.register_handler(0xF003, 0xD001,
        nullptr,
        [this](uint16_t, uint8_t v) { mmu_.write_port_dffd(v); });

    // Port 0xEFF7 — Pentagon-1024 disable / RAM-at-0x0000. VHDL zxnext.vhd:2604.
    // Decode: A15:12="1110", port_f7_lsb (low byte = 0xF7) → A14:13 are
    // don't-care so decode spans 0xE0F7..0xEFF7 (mask 0xF0FF / match 0xE0F7).
    // 0xEFF7 is the canonical documented address. Write-only per VHDL:3780-3785
    // (no port_eff7_rd path). Forwards to Mmu::write_port_eff7 which stores
    // bits 2,3 and re-runs the port_memory_change_dly MMU0/1 rebuild so the
    // RAM-at-0x0000 swap (bit 3) lands immediately per VHDL:4619-4644.
    port_.register_handler(0xF0FF, 0xE0F7,
        nullptr,
        [this](uint16_t, uint8_t v) { mmu_.write_port_eff7(v); });

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
    port_.register_handler(0xC007, 0x8005,
        nullptr,
        [this](uint16_t, uint8_t val) {
            if ((nextreg_.cached(0x84) & 0x01) == 0) return;  // gated off
            turbosound_.reg_write(val);
        });

    // DAC ports — Soundrive Mode 1 (most common)
    // Channel A (left):  port 0x1F
    // Channel B (left):  port 0x0F
    // Channel C (right): port 0x4F
    // Channel D (right): port 0x5F
    port_.register_handler(0x00FF, 0x001F, nullptr,
        [this](uint16_t, uint8_t val) { if (dac_enabled_) dac_.write_channel(0, val); });
    port_.register_handler(0x00FF, 0x000F, nullptr,
        [this](uint16_t, uint8_t val) { if (dac_enabled_) dac_.write_channel(1, val); });
    port_.register_handler(0x00FF, 0x004F, nullptr,
        [this](uint16_t, uint8_t val) { if (dac_enabled_) dac_.write_channel(2, val); });

    // DAC port 0x5F conflicts with sprite patterns (port 0x5B uses 0x00FF mask).
    // Use full 16-bit match for Soundrive Mode 2 ports instead.
    // Soundrive Mode 2: 0xF1=A, 0xF3=B, 0xF9=C, 0xFB=D
    port_.register_handler(0xFFFF, 0x00F1, nullptr,
        [this](uint16_t, uint8_t val) { if (dac_enabled_) dac_.write_channel(0, val); });
    port_.register_handler(0xFFFF, 0x00F3, nullptr,
        [this](uint16_t, uint8_t val) { if (dac_enabled_) dac_.write_channel(1, val); });
    port_.register_handler(0xFFFF, 0x00F9, nullptr,
        [this](uint16_t, uint8_t val) { if (dac_enabled_) dac_.write_channel(2, val); });
    port_.register_handler(0xFFFF, 0x00FB, nullptr,
        [this](uint16_t, uint8_t val) { if (dac_enabled_) dac_.write_channel(3, val); });

    // Specdrum: port 0xDF → channels A+D.  VHDL zxnext.vhd:2674 routes
    // 0xDF reads through port_1f (joystick 1) when the combo gate fires
    // (dac_mono_AD_df_io_en AND NOT mouse_io_en AND port_1f_io_en).
    // Read returns 0x00 (no buttons) matching the Kempston 1 stub.
    port_.register_handler(0x00FF, 0x00DF,
        [](uint16_t) -> uint8_t { return 0x00; },
        [this](uint16_t, uint8_t val) {
            if (dac_enabled_) { dac_.write_channel(0, val); dac_.write_channel(3, val); }
        });

    // --- Audio NextREG handlers ---

    // Register 0x06: Peripheral 2 — bits 1:0 = PSG mode (00=YM, 01=AY)
    //   bit 6 = internal speaker beep-only (exclusive mode, not emulated yet)
    nextreg_.set_write_handler(0x06, [this](uint8_t v) {
        bool ay_mode = (v & 0x03) == 1;  // 00=YM, 01=AY, others=hold/reset
        turbosound_.set_ay_mode(ay_mode);
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
    nextreg_.set_write_handler(0x08, [this](uint8_t v) {
        if (v & 0x80) mmu_.unlock_paging();
        mmu_.set_contention_disabled((v >> 6) & 1);
        // Mirror NR 0x08 bit 6 into ContentionModel's i_contention_en gate
        // (VHDL zxnext.vhd:1380 default '0', zxnext.vhd:5823 stored on write,
        // zxnext.vhd:4481 feeds eff_nr_08_contention_disable).
        contention_.set_contention_disable(((v >> 6) & 1) != 0);
        turbosound_.set_stereo_mode((v >> 5) & 1);
        dac_enabled_ = (v >> 3) & 1;
        turbosound_.set_enabled((v >> 1) & 1);
        // Mirror the stored bits (5/4/3/2/1/0) for NR 0x08 read-back per
        // VHDL zxnext.vhd:5906. Bit 7 is write-strobe-only (not stored),
        // and bit 6 is kept live on mmu_ (contention_disabled_). The
        // remaining bits are composed from this cache so read matches the
        // last write for those signals the emulator does not drive from
        // live subsystem state (internal speaker bit 4, port_ff_rd bit 2,
        // keyboard issue2 bit 0).
        nr_08_stored_low_ = v & 0x3F;
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
    nextreg_.set_write_handler(0x09, [this](uint8_t v) {
        sprites_.set_over_border((v & 0x08) != 0);
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
    port_.register_handler(0xFFFF, 0x103B,
        [this](uint16_t) -> uint8_t { return i2c_.read_scl(); },
        [this](uint16_t, uint8_t val) { i2c_.write_scl(val); });
    port_.register_handler(0xFFFF, 0x113B,
        [this](uint16_t) -> uint8_t { return i2c_.read_sda(); },
        [this](uint16_t, uint8_t val) { i2c_.write_sda(val); });

    // UART: Tx (0x133B), Rx (0x143B), Select (0x153B), Frame (0x163B)
    port_.register_handler(0xFFFF, 0x133B,
        [this](uint16_t) -> uint8_t { return uart_.read(3); },
        [this](uint16_t, uint8_t val) { uart_.write(3, val); });
    port_.register_handler(0xFFFF, 0x143B,
        [this](uint16_t) -> uint8_t { return uart_.read(0); },
        [this](uint16_t, uint8_t val) { uart_.write(0, val); });
    port_.register_handler(0xFFFF, 0x153B,
        [this](uint16_t) -> uint8_t { return uart_.read(1); },
        [this](uint16_t, uint8_t val) { uart_.write(1, val); });
    port_.register_handler(0xFFFF, 0x163B,
        [this](uint16_t) -> uint8_t { return uart_.read(2); },
        [this](uint16_t, uint8_t val) { uart_.write(2, val); });

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
    // Reads return 0x00 (no buttons pressed). Full joystick subsystem is
    // item 17 implementation debt; these stubs satisfy the handler-exists test.
    port_.register_handler(0x00FF, 0x001F,
        [](uint16_t) -> uint8_t { return 0x00; },
        nullptr);
    port_.register_handler(0x00FF, 0x0037,
        [](uint16_t) -> uint8_t { return 0x00; },
        nullptr);

    // Kempston mouse: buttons (0xFADF), X (0xFBDF), Y (0xFFDF).
    // VHDL zxnext.vhd:2668-2670. Stub: returns 0 (no movement/buttons).
    port_.register_handler(0xFFFF, 0xFADF,
        [](uint16_t) -> uint8_t { return 0x00; },
        nullptr);
    port_.register_handler(0xFFFF, 0xFBDF,
        [](uint16_t) -> uint8_t { return 0x00; },
        nullptr);
    port_.register_handler(0xFFFF, 0xFFDF,
        [](uint16_t) -> uint8_t { return 0x00; },
        nullptr);

    // ULA+ register select (0xBF3B) and data (0xFF3B). VHDL zxnext.vhd:2685-2686.
    // Stub: accepts writes, reads back 0x00.
    port_.register_handler(0xFFFF, 0xBF3B,
        [](uint16_t) -> uint8_t { return 0x00; },
        [](uint16_t, uint8_t) {});
    port_.register_handler(0xFFFF, 0xFF3B,
        [](uint16_t) -> uint8_t { return 0x00; },
        [](uint16_t, uint8_t) {});

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

    ctc_.on_interrupt = [this](int channel) {
        Im2Level level = static_cast<Im2Level>(
            static_cast<int>(Im2Level::CTC_0) + channel);
        im2_.raise(level);
    };

    dma_.on_interrupt = [this]() {
        im2_.raise(Im2Level::DMA);
    };

    uart_.on_tx_interrupt = [this](int channel) {
        im2_.raise(channel == 0 ? Im2Level::UART_TX_0 : Im2Level::UART_TX_1);
    };

    uart_.on_rx_interrupt = [this](int channel) {
        im2_.raise(channel == 0 ? Im2Level::UART_RX_0 : Im2Level::UART_RX_1);
    };

    // --- Phase 5 DivMMC overlay + I2C RTC + SD card ---

    mmu_.set_divmmc(&divmmc_);
    mmu_.set_debug_state(&debug_state_);
    i2c_.attach_device(0x68, &rtc_);
    spi_.attach_device(0, &sd_card_);  // SD card on CS0

    // --- ROM loading ---

    // Load ROMs based on machine type. ROM files are expected in the
    // roms directory with standard names:
    //   48k:     48.rom (16K)
    //   128k:    128-0.rom + 128-1.rom (16K each) or 128.rom (32K)
    //   +3:      plus3-0.rom + plus3-1.rom + plus3-2.rom + plus3-3.rom (16K each) or plus3.rom (64K)
    //   next:    48.rom (fallback for non-NextZXOS mode)
    //   pentagon: 128-0.rom + 128-1.rom (same as 128K)
    //
    // Skipped on soft reset (preserve_memory=true) — the rom_ buffer and
    // the Next ROM-in-SRAM window already hold whatever tbblue.fw just
    // installed, and reloading from disk would overwrite NextZXOS with the
    // default 48.rom.
    if (!preserve_memory) {
        std::string dir = cfg.roms_directory;
        if (!dir.empty() && dir.back() != '/') dir += '/';

        switch (cfg.type) {
            case MachineType::ZX48K:
                if (!rom_.load(0, dir + "48.rom"))
                    Log::emulator()->warn("could not load {}48.rom — 48K BASIC will not boot", dir);
                break;

            case MachineType::ZX128K:
                if (!rom_.load(0, dir + "128-0.rom")) {
                    Log::emulator()->warn("could not load {}128-0.rom — 128K BASIC will not boot", dir);
                } else {
                    if (!rom_.load(1, dir + "128-1.rom"))
                        Log::emulator()->warn("could not load {}128-1.rom — 128K ROM 1 missing", dir);
                }
                break;

            case MachineType::PENTAGON:
                // Pentagon uses its own ROMs (128p-0/1), falling back to 128K ROMs
                if (!rom_.load(0, dir + "128p-0.rom")) {
                    if (!rom_.load(0, dir + "128-0.rom"))
                        Log::emulator()->warn("could not load {}128p-0.rom or {}128-0.rom — Pentagon will not boot", dir, dir);
                }
                if (!rom_.load(1, dir + "128p-1.rom")) {
                    if (!rom_.load(1, dir + "128-1.rom"))
                        Log::emulator()->warn("could not load {}128p-1.rom or {}128-1.rom — Pentagon ROM 1 missing", dir, dir);
                }
                break;

            case MachineType::ZX_PLUS3:
                for (int i = 0; i < 4; ++i) {
                    std::string name = dir + "plus3-" + std::to_string(i) + ".rom";
                    if (!rom_.load(i, name))
                        Log::emulator()->warn("could not load {} — +3 ROM {} missing", name, i);
                }
                break;

            case MachineType::ZXN_ISSUE2:
            default:
                if (!rom_.load(0, dir + "48.rom"))
                    Log::emulator()->warn("could not load {}48.rom — BASIC will not boot", dir);
                break;
        }
        Log::emulator()->info("Machine type: {} (ROMs from '{}')", machine_type_str(cfg.type), cfg.roms_directory);
    }

    // Boot ROM loading (FPGA bootloader — highest priority overlay).
    // Skipped on soft reset: the Emulator-owned boot_rom_ vector already
    // has the contents, and Mmu::reset() preserves the set_boot_rom pointer
    // via its boot_rom_ member. VHDL bootrom_en is NOT in the soft-reset
    // domain — once nextboot.rom disables the overlay via NR 0x03, it stays
    // off across RESET_SOFT so the Z80 boots into the NewlZXOS ROM in SRAM.
    // The post-init block below explicitly clears boot_rom_en on soft reset.
    if (!preserve_memory && !cfg.boot_rom_path.empty()) {
        std::ifstream bf(cfg.boot_rom_path, std::ios::binary | std::ios::ate);
        if (bf.is_open()) {
            auto sz = bf.tellg();
            boot_rom_.resize(static_cast<size_t>(sz));
            bf.seekg(0);
            bf.read(reinterpret_cast<char*>(boot_rom_.data()), sz);
            mmu_.set_boot_rom(boot_rom_.data(), boot_rom_.size());
            Log::emulator()->info("Boot ROM loaded: '{}' ({} bytes), overlay active at 0x0000-0x{:04X}",
                                  cfg.boot_rom_path, boot_rom_.size(), boot_rom_.size() - 1);
        } else {
            Log::emulator()->warn("could not load boot ROM from '{}'", cfg.boot_rom_path);
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

    // DivMMC ROM loading
    if (!cfg.divmmc_rom_path.empty()) {
        if (divmmc_.load_rom(cfg.divmmc_rom_path)) {
            divmmc_.set_enabled(true);
            Log::emulator()->info("DivMMC enabled, ROM loaded from '{}'", cfg.divmmc_rom_path);
        } else {
            Log::emulator()->warn("could not load DivMMC ROM from '{}'", cfg.divmmc_rom_path);
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

    // Reset FUSE tstates counter to 0 at frame start.  The FUSE Z80 core's
    // built-in contention macros index ula_contention[tstates], so tstates
    // must be relative to the frame start (0 = first T-state of frame).
    *fuse_z80_tstates_ptr() = 0;
    frame_ts_start_ = 0;

    // Schedule the ULA frame interrupt at vc=1, hc=0.
    // One line = timing_.tstates_per_line T-states; at 28 MHz that is
    // tstates_per_line * 8 master cycles.
    const uint64_t int_fire_offset = 1ULL * timing_.tstates_per_line * 8;
    if (!ula_int_disabled_) {
        scheduler_.schedule(frame_cycle_ + int_fire_offset, EventType::CPU_INT,
            [this]() {
                cpu_.request_interrupt(0xFF);
                im2_int_status_[0] |= 0x01;  // ULA interrupt status
            });
    }

    // Schedule line interrupt if enabled.
    if (line_int_enabled_ && line_int_value_ < static_cast<uint16_t>(timing_.lines_per_frame)) {
        uint64_t line_cycle = frame_cycle_ +
            static_cast<uint64_t>(line_int_value_) * timing_.master_cycles_per_line;
        scheduler_.schedule(line_cycle, EventType::CPU_INT,
            [this]() {
                im2_.raise(Im2Level::LINE_IRQ);
                im2_int_status_[0] |= 0x02;  // Line interrupt status
                cpu_.request_interrupt(0xFF);
            });
    }

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

    // Initialize per-line border colour to current value.
    // Port 0xFE writes will update individual lines during execution.
    renderer_.ula().init_border_per_line();

    // Initialize per-line tilemap scroll to current values.
    // Interrupt handlers may change scroll mid-frame for split-screen effects.
    tilemap_.init_scroll_per_line();

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
            // Memory contention is applied per-access via the on_contention
            // callback, which adds delay to the FUSE tstates counter for
            // each read/write to contended addresses.
            int tstates = cpu_.execute();

            // Call stack tracking post-execution.
            if (call_stack_.enabled()) {
                auto post = cpu_.get_registers();
                call_stack_.on_instruction_post(post.SP, post.PC);
            }

            // Convert T-states to 28 MHz master cycles.
            master_cycles = static_cast<uint64_t>(tstates) * clock_.cpu_divisor();

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

        // Execute copper at current raster position.
        // Compute raw vc/hc from cycles elapsed within frame, then derive
        // the copper vertical counter (cvc).  In the VHDL, cvc resets to 0
        // at the first active display line (c_min_vactive = 64 for 48K),
        // so copper WAIT vpos=0 means "first display line".  In our frame
        // layout the active display starts at row DISP_Y (32).
        if (copper_.is_running()) {
            uint64_t elapsed = clock_.get() - frame_cycle_;
            int vc = static_cast<int>(elapsed / timing_.master_cycles_per_line);
            int hc = static_cast<int>(elapsed % timing_.master_cycles_per_line);
            // Convert raw vc to copper vc: cvc=0 at first display line.
            int cvc = (vc - Renderer::DISP_Y + timing_.lines_per_frame) % timing_.lines_per_frame;
            copper_.execute(hc, cvc, nextreg_);
        }

        // Tick CTC and UART at 28 MHz rate.
        ctc_.tick(static_cast<uint32_t>(master_cycles));
        uart_.tick(static_cast<uint32_t>(master_cycles));

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

    // Snapshot the fallback/border colour and tilemap scroll for the last scanline.
    renderer_.snapshot_fallback_for_line(timing_.lines_per_frame - 1);
    renderer_.ula().snapshot_border_for_line(timing_.lines_per_frame - 1);
    tilemap_.snapshot_scroll_for_line(timing_.lines_per_frame - 1);

    // Render the completed frame into the ARGB8888 framebuffer.
    // Suppressed in replay mode (fast-forward rewind path).
    if (!replay_mode_) {
        last_frame_width_ = renderer_.render_frame(framebuffer_.data(), mmu_, ram_, palette_,
                                                    layer2_, &sprites_, &tilemap_);
    }

    // Capture frame for video recording (if active, not in replay).
    if (!replay_mode_ && video_recorder_.is_recording()) {
        video_recorder_.capture_frame(framebuffer_.data(),
                                       last_frame_width_, FRAMEBUFFER_HEIGHT);
    }

    // End RZX recording frame (not in replay).
    if (!replay_mode_ && rzx_recorder_.is_recording()) {
        rzx_recorder_.end_frame(static_cast<uint16_t>(
            std::min(rzx_frame_instruction_count_, uint32_t(0xFFFF))));
    }

    // Advance auto-type state machine (one step per frame).
    keyboard_.tick_auto_type();
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

        int tstates = cpu_.execute();

        // Call stack tracking post-execution.
        if (call_stack_.enabled()) {
            auto post = cpu_.get_registers();
            call_stack_.on_instruction_post(post.SP, post.PC);
        }

        master_cycles = static_cast<uint64_t>(tstates) * clock_.cpu_divisor();
    }
    clock_.tick(master_cycles);
    dma_.tick_burst_wait(master_cycles);

    // Copper.
    if (copper_.is_running()) {
        uint64_t elapsed = clock_.get() - frame_cycle_;
        int vc = static_cast<int>(elapsed / timing_.master_cycles_per_line);
        int hc = static_cast<int>(elapsed % timing_.master_cycles_per_line);
        int cvc = (vc - Renderer::DISP_Y + timing_.lines_per_frame) % timing_.lines_per_frame;
        copper_.execute(hc, cvc, nextreg_);
    }

    // CTC + UART.
    ctc_.tick(static_cast<uint32_t>(master_cycles));
    uart_.tick(static_cast<uint32_t>(master_cycles));

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
    // Floating bus: the ULA drives VRAM data onto the bus during active display.
    // The VHDL implements this for all machine types (48K/128K/+3/Pentagon/Next).
    // Pentagon returns the attribute byte only (no pixel bytes on the bus).

    // Compute current position within the frame.
    // Master clock is 28 MHz. T-states at 3.5 MHz = master_cycles / 8.
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

void Emulator::on_scanline(int line)
{
    // Snapshot the fallback colour for the previous scanline.
    // By the time on_scanline(N) fires, the copper has finished executing
    // for line N-1, so the fallback colour reflects the copper's MOVE writes.
    if (line > 0) {
        renderer_.snapshot_fallback_for_line(line - 1);
        renderer_.ula().snapshot_border_for_line(line - 1);
        tilemap_.snapshot_scroll_for_line(line - 1);
    }
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
    w.write_bool(line_int_enabled_);
    w.write_bool(ula_int_disabled_);
    w.write_u16(line_int_value_);
    w.write_bool(im2_hw_mode_);
    w.write_u8(im2_vector_base_);
    w.write_bytes(im2_int_enable_, 3);
    w.write_bytes(im2_int_status_, 3);
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
    line_int_enabled_ = r.read_bool();
    ula_int_disabled_ = r.read_bool();
    line_int_value_   = r.read_u16();
    im2_hw_mode_      = r.read_bool();
    im2_vector_base_  = r.read_u8();
    r.read_bytes(im2_int_enable_, 3);
    r.read_bytes(im2_int_status_, 3);
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
    last_frame_width_ = renderer_.render_frame(framebuffer_.data(), mmu_, ram_, palette_,
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
    last_frame_width_ = renderer_.render_frame(framebuffer_.data(), mmu_, ram_, palette_,
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
