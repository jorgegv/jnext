#include "core/emulator.h"

#include "core/log.h"
#include "core/nex_loader.h"
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

bool Emulator::init(const EmulatorConfig& cfg)
{
    config_ = cfg;
    Log::emulator()->info("Initializing emulator: machine_type={} cpu_speed={}",
                          static_cast<int>(cfg.type), cpu_speed_str(cfg.cpu_speed));

    // Apply CPU speed to the clock.
    clock_.reset();
    clock_.set_cpu_speed(cfg.cpu_speed);

    // Allocate the framebuffer and fill with black (ARGB: 0xFF000000).
    framebuffer_.assign(FRAMEBUFFER_PIXELS, 0xFF000000u);

    // Clear any stale scheduler events from a previous session.
    scheduler_.reset();

    frame_cycle_ = 0;

    // Subsystem resets.
    ram_.reset();
    rom_.reset();
    mmu_.reset();
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
    contention_.build(cfg.type);

    // Clear all port dispatch handlers before re-registering them.
    // Without this, reset() → init() would duplicate every handler, causing
    // double-fired writes (breaking auto-increment ports like sprites/palette).
    port_.clear_handlers();

    // Floating bus: unmatched port reads return ULA bus value in 48K/128K modes.
    port_.set_default_read([this](uint16_t) -> uint8_t {
        return floating_bus_read();
    });

    // Memory contention: add wait states for each read/write to contended
    // addresses during instruction execution. The delay is added directly
    // to the FUSE tstates counter so it's included in the instruction's
    // total T-state count returned by execute().
    cpu_.on_contention = [this](uint16_t addr) {
        if (contention_.is_contended_address(addr)) {
            // Compute position within frame using FUSE tstates counter.
            // tstates is a global monotonic counter; frame_ts_start_ is its
            // value at the beginning of run_frame(). The delta gives us the
            // number of T-states elapsed within the current frame.
            uint32_t ts_in_frame = *fuse_z80_tstates_ptr() - frame_ts_start_;
            uint64_t master_in_frame = static_cast<uint64_t>(ts_in_frame) * clock_.cpu_divisor();
            int vc = static_cast<int>(master_in_frame / MASTER_CYCLES_PER_LINE);
            int hc = static_cast<int>((master_in_frame % MASTER_CYCLES_PER_LINE) / 2);
            int delay = contention_.delay(static_cast<uint16_t>(hc),
                                           static_cast<uint16_t>(vc));
            if (delay > 0) *fuse_z80_tstates_ptr() += delay;
        }
    };

    // DivMMC auto-map must fire BEFORE the opcode fetch so the memory
    // overlay is active for the same M1 read that triggered it (matching
    // the VHDL combinatorial address decode).
    cpu_.on_m1_prefetch = [this](uint16_t pc) {
        divmmc_.check_automap(pc, true);
    };

    // Install M1-cycle callback for RETI detection (ED 4D sequence).
    // When RETI is executed, notify the Im2Controller so it can clear the
    // active interrupt level in the daisy chain.
    cpu_.on_m1_cycle = [this](uint16_t pc, uint8_t opcode) {
        static bool saw_ed = false;
        if (opcode == 0xED) {
            saw_ed = true;
        } else {
            if (saw_ed && opcode == 0x4D) {
                im2_.on_reti();
            }
            saw_ed = false;
        }
    };

    // --- NextREG write handlers ---

    // Register 0x07: CPU speed selector
    //   0 = 3.5 MHz, 1 = 7 MHz, 2 = 14 MHz, 3 = 28 MHz
    nextreg_.set_write_handler(0x07, [this](uint8_t v) {
        CpuSpeed speed = static_cast<CpuSpeed>(v & 0x03);
        Log::emulator()->info("CPU speed changed to {} (NextREG 0x07={:#04x})", cpu_speed_str(speed), v);
        clock_.set_cpu_speed(speed);
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
    nextreg_.set_write_handler(0x14, [this](uint8_t v) {
        palette_.set_global_transparency(v);
    });

    // Register 0x40: Palette index
    nextreg_.set_write_handler(0x40, [this](uint8_t v) {
        palette_.set_index(v);
    });

    // Register 0x41: Palette value 8-bit (RRRGGGBB)
    nextreg_.set_write_handler(0x41, [this](uint8_t v) {
        palette_.write_8bit(v);
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

    // Register 0x15: Sprite and layer system setup
    //   bit 7 = LoRes enable (deferred)
    //   bit 6 = sprite priority (0=sprite 0 on top when zero_on_top)
    //   bit 5 = sprite border clip enable
    //   bits 4:2 = layer priority (SLU/LSU/SUL/LUS/USL/ULS)
    //   bit 1 = sprites over border
    //   bit 0 = sprites visible
    nextreg_.set_write_handler(0x15, [this](uint8_t v) {
        sprites_.set_zero_on_top((v & 0x40) != 0);
        sprites_.set_over_border((v & 0x02) != 0);
        sprites_.set_sprites_visible((v & 0x01) != 0);
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

    // Register 0x2F: Tilemap X scroll LSB
    nextreg_.set_write_handler(0x2F, [this](uint8_t v) { tilemap_.set_scroll_x_lsb(v); });

    // Register 0x30: Tilemap X scroll MSB
    nextreg_.set_write_handler(0x30, [this](uint8_t v) { tilemap_.set_scroll_x_msb(v); });

    // Register 0x31: Tilemap Y scroll
    nextreg_.set_write_handler(0x31, [this](uint8_t v) { tilemap_.set_scroll_y(v); });

    // Register 0x6B: Tilemap control
    nextreg_.set_write_handler(0x6B, [this](uint8_t v) { tilemap_.set_control(v); });

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

    // Registers 0x50–0x57: MMU slot→page mapping (one register per slot)
    for (int i = 0; i < 8; ++i) {
        nextreg_.set_write_handler(static_cast<uint8_t>(0x50 + i),
            [this, i](uint8_t v) { mmu_.set_page(i, v); });
    }

    // --- NextREG read handlers (dynamic registers) ---

    // Registers 0x1E/0x1F: Active video line (read-only, computed from cycle count).
    // Returns the current raster line (vc) relative to display start.
    nextreg_.set_read_handler(0x1E, [this]() -> uint8_t {
        uint64_t elapsed = clock_.get() - frame_cycle_;
        int vc = static_cast<int>(elapsed / MASTER_CYCLES_PER_LINE);
        return static_cast<uint8_t>((vc >> 8) & 0x01);
    });
    nextreg_.set_read_handler(0x1F, [this]() -> uint8_t {
        uint64_t elapsed = clock_.get() - frame_cycle_;
        int vc = static_cast<int>(elapsed / MASTER_CYCLES_PER_LINE);
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

    // Register 0x02: Reset control (soft reset on write)
    nextreg_.set_write_handler(0x02, [this](uint8_t v) {
        if (v & 0x01) {
            Log::emulator()->info("Soft reset triggered via NextREG 0x02");
            reset();
        }
    });

    // Register 0x03: Machine type (bits 2:0 = machine timing)
    // Writing to this register also disables the boot ROM overlay
    // (VHDL: bootrom_en <= '0' on any write to nr_03).
    nextreg_.set_write_handler(0x03, [this](uint8_t v) {
        if (mmu_.boot_rom_enabled()) {
            mmu_.set_boot_rom_enabled(false);
            Log::emulator()->info("Boot ROM disabled by NextREG 0x03 write ({:#04x})", v);
        }
        Log::emulator()->info("Machine type set to {:#04x} via NextREG 0x03", v);
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

    // Register 0x8C: Alternate ROM control
    //   bit 7 = enable alt rom
    //   bit 6 = alt rom visible only during writes
    //   bits 5:4 = lock ROM1/ROM0
    nextreg_.set_write_handler(0x8C, [this](uint8_t v) {
        rom_.set_alt_rom_config(v);
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

    // --- Port dispatch handlers ---

    // Layer 2 control — port 0x123B (full 16-bit match).
    // Write: bit 0 = write-map enable (CPU writes to L2 RAM banks)
    //        bit 1 = Layer 2 visible
    //        bits 7:6 = segment select for write-mapping (00/01/10=single, 11=all)
    // Read:  returns last written value.
    port_.register_handler(0xFFFF, 0x123B,
        nullptr,
        [this](uint16_t, uint8_t val) {
            layer2_.set_enabled((val & 0x02) != 0);
            mmu_.set_l2_write_port(val, layer2_.active_bank());
        });

    // 128K bank switch — port 0x7FFD decoded by address-line masking.
    // A15=0, A1=0 → mask 0x8002, match 0x0000.
    // Port 0x7FFD = 0111 1111 1111 1101: A15=0 ✓, A1=0 ✓ → matches.
    port_.register_handler(0x8002, 0x0000,
        nullptr,
        [this](uint16_t, uint8_t v) {
            mmu_.map_128k_bank(v);
            // Update 0xC000 contention based on machine type (VHDL zxnext.vhd:4489-4493):
            //   128K: odd banks (1,3,5,7) are contended
            //   +3:   banks >= 4 (4,5,6,7) are contended
            uint8_t bank = v & 0x07;
            if (config_.type == MachineType::ZX_PLUS3)
                contention_.set_contended_slot(3, bank >= 4);
            else
                contention_.set_contended_slot(3, bank & 1);
        });

    // +3 paging — port 0x1FFD (mask 0xF002, match 0x1000).
    // Only active on +3/+2A models. Bits: 0=special mode, 2:1=config, 2=ROM high bit.
    port_.register_handler(0xF002, 0x1000,
        nullptr,
        [this](uint16_t, uint8_t v) {
            mmu_.map_plus3_bank(v);
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
    port_.register_handler(0xFFFF, 0x00FF,
        nullptr,
        [this](uint16_t, uint8_t val) {
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

    // --- Audio port handlers ---

    // AY register select — port 0xFFFD (mask 0xC002, value 0xC000).
    // Write: selects AY register or changes active AY chip / panning.
    // Read: returns selected register contents.
    port_.register_handler(0xC002, 0xC000,
        [this](uint16_t) -> uint8_t { return turbosound_.reg_read(false); },
        [this](uint16_t, uint8_t val) { turbosound_.reg_addr(val); });

    // AY data write — port 0xBFFD (mask 0xC002, value 0x8000).
    // Write: writes data to selected register on active AY.
    port_.register_handler(0xC002, 0x8000,
        nullptr,
        [this](uint16_t, uint8_t val) { turbosound_.reg_write(val); });

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

    // Specdrum: port 0xDF → channels A+D
    port_.register_handler(0x00FF, 0x00DF, nullptr,
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
    //   bit 5 = stereo mode (0=ABC, 1=ACB)
    //   bit 3 = DAC enable
    //   bit 1 = TurboSound enable
    nextreg_.set_write_handler(0x08, [this](uint8_t v) {
        turbosound_.set_stereo_mode((v >> 5) & 1);
        dac_enabled_ = (v >> 3) & 1;
        turbosound_.set_enabled((v >> 1) & 1);
    });

    // Register 0x09: Peripheral 4
    //   bits 7:5 = per-chip mono mode (bit 7=AY#2, 6=AY#1, 5=AY#0)
    //   bit 3 = sprites over border (already handled above)
    nextreg_.set_write_handler(0x09, [this](uint8_t v) {
        sprites_.set_over_border((v & 0x08) != 0);
        // Mono mode: bit 7=AY#2, bit 6=AY#1, bit 5=AY#0
        // Map to TurboSound: bit 0=AY#0, bit 1=AY#1, bit 2=AY#2
        uint8_t mono = 0;
        if (v & 0x20) mono |= 0x01;  // AY#0
        if (v & 0x40) mono |= 0x02;  // AY#1
        if (v & 0x80) mono |= 0x04;  // AY#2
        turbosound_.set_mono_mode(mono);
    });

    // --- Phase 5 peripheral port handlers ---

    // CTC channels 0-3: ports 0x183B, 0x193B, 0x1A3B, 0x1B3B
    for (int ch = 0; ch < 4; ++ch) {
        uint16_t p = static_cast<uint16_t>(0x183B + (ch << 8));
        port_.register_handler(0xFFFF, p,
            [this, ch](uint16_t) -> uint8_t { return ctc_.read(ch); },
            [this, ch](uint16_t, uint8_t val) { ctc_.write(ch, val); });
    }

    // DMA — port 0x6B (ZXN mode) and port 0x0B (Z80-DMA compat)
    port_.register_handler(0x00FF, 0x006B,
        [this](uint16_t) -> uint8_t { return dma_.read(); },
        [this](uint16_t, uint8_t val) { dma_.write(val, false); });
    port_.register_handler(0x00FF, 0x000B,
        [this](uint16_t) -> uint8_t { return dma_.read(); },
        [this](uint16_t, uint8_t val) { dma_.write(val, true); });

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

    // DivMMC control — port 0xE3
    port_.register_handler(0x00FF, 0x00E3,
        [this](uint16_t) -> uint8_t { return divmmc_.read_control(); },
        [this](uint16_t, uint8_t val) { divmmc_.write_control(val); });

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
    {
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

    // Boot ROM loading (FPGA bootloader — highest priority overlay)
    if (!cfg.boot_rom_path.empty()) {
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

void Emulator::run_frame()
{
    const uint64_t frame_end = frame_cycle_ + MASTER_CYCLES_PER_FRAME;
    frame_ts_start_ = *fuse_z80_tstates_ptr();

    // Schedule the ULA frame interrupt at vc=1 (the line immediately after
    // vsync/sync area).  In 48K timing: vc=1, hc=0 corresponds to 1 line
    // into the frame.  One line = 228 T-states; at 28 MHz that is
    // 228 * 8 = 1824 master cycles (cpu_divisor=8 at 3.5 MHz).
    // The 48K ROM runs in IM1; the vector 0xFF calls RST 0x38 which scans
    // the keyboard and drives the BASIC main loop.
    static constexpr uint64_t INT_FIRE_OFFSET = 1ULL * 228 * 8;  // vc=1, hc=0
    if (!ula_int_disabled_) {
        scheduler_.schedule(frame_cycle_ + INT_FIRE_OFFSET, EventType::CPU_INT,
            [this]() {
                cpu_.request_interrupt(0xFF);
                im2_int_status_[0] |= 0x01;  // ULA interrupt status
            });
    }

    // Schedule line interrupt if enabled.
    if (line_int_enabled_ && line_int_value_ < static_cast<uint16_t>(LINES_PER_FRAME)) {
        uint64_t line_cycle = frame_cycle_ +
            static_cast<uint64_t>(line_int_value_) * MASTER_CYCLES_PER_LINE;
        scheduler_.schedule(line_cycle, EventType::CPU_INT,
            [this]() {
                im2_.raise(Im2Level::LINE_IRQ);
                im2_int_status_[0] |= 0x02;  // Line interrupt status
                cpu_.request_interrupt(0xFF);
            });
    }

    // Notify copper of frame start (resets PC in mode 11).
    copper_.on_vsync();

    // Initialize per-line fallback array to current value.
    // The copper will update individual lines during execution.
    renderer_.init_fallback_per_line();

    // Initialize per-line border colour to current value.
    // Port 0xFE writes will update individual lines during execution.
    renderer_.ula().init_border_per_line();

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
            // Execute one CPU instruction; returns T-states consumed.
            // Memory contention is applied per-access via the on_contention
            // callback, which adds delay to the FUSE tstates counter for
            // each read/write to contended addresses.
            int tstates = cpu_.execute();
            // Convert T-states to 28 MHz master cycles.
            master_cycles = static_cast<uint64_t>(tstates) * clock_.cpu_divisor();

            // Tick real-time tape playback (advances EAR bit state machine).
            if (tape_.is_playing()) {
                tape_.tick_realtime(static_cast<uint64_t>(tstates));
                beeper_.set_tape_ear(tape_.tick_realtime(0) != 0);
            } else if (tzx_tape_.is_playing()) {
                // TZX real-time: ZOT uses absolute CPU T-state clocks.
                uint64_t cpu_clocks = static_cast<uint64_t>(*fuse_z80_tstates_ptr());
                beeper_.set_tape_ear(tzx_tape_.update(cpu_clocks) != 0);
            } else {
                beeper_.set_tape_ear(false);
            }
        }
        clock_.tick(master_cycles);

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
            int vc = static_cast<int>(elapsed / MASTER_CYCLES_PER_LINE);
            int hc = static_cast<int>(elapsed % MASTER_CYCLES_PER_LINE);
            // Convert raw vc to copper vc: cvc=0 at first display line.
            int cvc = (vc - Renderer::DISP_Y + LINES_PER_FRAME) % LINES_PER_FRAME;
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
        sample_accum_ += master_cycles * Mixer::SAMPLE_RATE;
        while (sample_accum_ >= SAMPLE_THRESHOLD) {
            sample_accum_ -= SAMPLE_THRESHOLD;
            mixer_.generate_sample(beeper_, turbosound_, dac_);
        }

        // Drain any scheduler events that have become due.
        scheduler_.run_until(clock_.get());
    }

    frame_cycle_ = frame_end;

    // Snapshot the fallback/border colour for the last scanline.
    renderer_.snapshot_fallback_for_line(LINES_PER_FRAME - 1);
    renderer_.ula().snapshot_border_for_line(LINES_PER_FRAME - 1);

    // Render the completed frame into the ARGB8888 framebuffer.
    renderer_.render_frame(framebuffer_.data(), mmu_, ram_, palette_,
                            layer2_, &sprites_, &tilemap_);

    // Advance auto-type state machine (one step per frame).
    keyboard_.tick_auto_type();
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
        int tstates = cpu_.execute();
        master_cycles = static_cast<uint64_t>(tstates) * clock_.cpu_divisor();
    }
    clock_.tick(master_cycles);

    // Copper.
    if (copper_.is_running()) {
        uint64_t elapsed = clock_.get() - frame_cycle_;
        int vc = static_cast<int>(elapsed / MASTER_CYCLES_PER_LINE);
        int hc = static_cast<int>(elapsed % MASTER_CYCLES_PER_LINE);
        int cvc = (vc - Renderer::DISP_Y + LINES_PER_FRAME) % LINES_PER_FRAME;
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
    clock_.reset();
    scheduler_.reset();
    frame_cycle_ = 0;

    ram_.reset();
    mmu_.reset();
    nextreg_.reset();
    cpu_.reset();
    im2_.reset();
    keyboard_.reset();

    // Clear framebuffer to black.
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xFF000000u);

    // Re-run init to restore consistent state (reloads ROM, rewires handlers).
    init(config_);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void Emulator::schedule_frame_events()
{
    // Schedule one SCANLINE event per line and a VSYNC at the frame boundary.
    for (int line = 0; line < LINES_PER_FRAME; ++line) {
        const uint64_t line_cycle =
            frame_cycle_ + static_cast<uint64_t>(line) * MASTER_CYCLES_PER_LINE;

        scheduler_.schedule(line_cycle, EventType::SCANLINE,
            [this, line]() { on_scanline(line); });
    }

    // VSYNC fires at the very end of the frame.
    scheduler_.schedule(
        frame_cycle_ + MASTER_CYCLES_PER_FRAME,
        EventType::VSYNC,
        [this]() { on_vsync(); });
}

uint8_t Emulator::floating_bus_read() const
{
    // Floating bus only exists on 48K/128K hardware.
    if (config_.type != MachineType::ZX48K && config_.type != MachineType::ZX128K)
        return 0xFF;

    // Compute current position within the frame.
    // Master clock is 28 MHz. T-states at 3.5 MHz = master_cycles / 8.
    uint64_t master_elapsed = clock_.get() - frame_cycle_;
    int tstates_in_frame = static_cast<int>(master_elapsed / cpu_speed_divisor(config_.cpu_speed));

    // Scanline timing:
    //   228 T-states per line (48K/128K).
    //   First 128 T-states: ULA fetches pixel/attribute data.
    //   Last 100 T-states: border (bus idle = 0xFF).
    //   Active display: lines 64-255 (192 pixel lines).
    int line = tstates_in_frame / TSTATES_PER_LINE;
    int tstate_in_line = tstates_in_frame % TSTATES_PER_LINE;

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
    }
}

void Emulator::on_vsync()
{
    // Stub: nothing to do yet.
    // Real implementation:
    //   - Signal the platform layer that a new frame is ready.
    //   - Reset per-frame state (floating bus cache, sprite collision flags).
}
