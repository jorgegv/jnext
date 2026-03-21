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
        // DivMMC auto-map check on every M1 cycle.
        divmmc_.check_automap(pc, true);
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
    nextreg_.set_write_handler(0x03, [this](uint8_t v) {
        // Cache the value; timing mode changes are complex and would need
        // contention LUT rebuild — log for now.
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
    // Mask 0xE002 selects A15,A14,A1; match value 0x0000.
    port_.register_handler(0xE002, 0x0000,
        nullptr,
        [this](uint16_t, uint8_t v) { mmu_.map_128k_bank(v); });

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
            return 0xE0 | (keyboard_.read_rows(addr_high) & 0x1F);
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
    i2c_.attach_device(0x68, &rtc_);
    spi_.attach_device(0, &sd_card_);  // SD card on CS0

    // --- ROM loading ---

    // Attempt to load the 48K ROM into ROM slot 0 from the standard path.
    // Failure is non-fatal; the machine will not boot to BASIC but the
    // emulator can still run (useful for testing without a ROM file).
    if (!rom_.load(0, "roms/48.rom")) {
        Log::emulator()->warn("could not load roms/48.rom — continuing without ROM (BASIC will not boot)");
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
    return loader.apply(*this);
}

void Emulator::run_frame()
{
    const uint64_t frame_end = frame_cycle_ + MASTER_CYCLES_PER_FRAME;

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

    // Audio timing constants.
    // PSG clock = 28 MHz / 16 = 1.75 MHz → one PSG tick every 16 master cycles.
    static constexpr uint64_t PSG_DIVISOR = 16;
    // Sample generation: 28 MHz / 44100 Hz ≈ 634.92 master cycles per sample.
    // Use Bresenham-style accumulator: generate sample every time accum >= MASTER_CLOCK_HZ.
    static constexpr uint64_t SAMPLE_THRESHOLD = MASTER_CLOCK_HZ;

    while (clock_.get() < frame_end) {
        uint64_t master_cycles;

        if (dma_.is_active()) {
            // DMA takes the bus — CPU is stalled.
            // Execute a burst of transfers; each byte ≈ 2 T-states.
            int transferred = dma_.execute_burst(16);
            master_cycles = static_cast<uint64_t>(transferred * 2) * clock_.cpu_divisor();
            if (master_cycles == 0) master_cycles = clock_.cpu_divisor();  // minimum advance
        } else {
            // Execute one CPU instruction; returns T-states consumed.
            int tstates = cpu_.execute();
            // Convert T-states to 28 MHz master cycles.
            master_cycles = static_cast<uint64_t>(tstates) * clock_.cpu_divisor();
        }
        clock_.tick(master_cycles);

        // Execute copper at current raster position.
        // Approximate: compute vc/hc from cycles elapsed within frame.
        if (copper_.is_running()) {
            uint64_t elapsed = clock_.get() - frame_cycle_;
            int vc = static_cast<int>(elapsed / MASTER_CYCLES_PER_LINE);
            int hc = static_cast<int>(elapsed % MASTER_CYCLES_PER_LINE);
            // hc is in 28 MHz domain (0..MASTER_CYCLES_PER_LINE-1)
            // Copper uses hc in 28 MHz ticks.
            copper_.execute(hc, vc, nextreg_);
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

    // Render the completed frame into the ARGB8888 framebuffer.
    renderer_.render_frame(framebuffer_.data(), mmu_, ram_, palette_,
                            layer2_, &sprites_, &tilemap_);
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

void Emulator::on_scanline(int line)
{
    // Phase 2: full-frame rendering is done in run_frame() via renderer_.render_frame().
    // Per-scanline hooks reserved for future use:
    //   - Accumulate audio samples for the line period (Phase 4).
    //   - Fire IM2 frame interrupt at line 1 (Phase 5).
    (void)line;
}

void Emulator::on_vsync()
{
    // Stub: nothing to do yet.
    // Real implementation:
    //   - Signal the platform layer that a new frame is ready.
    //   - Reset per-frame state (floating bus cache, sprite collision flags).
}
