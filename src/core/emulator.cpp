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

    psg_accum_ = 0;
    sample_accum_ = 0;
    dac_enabled_ = false;

    // Build contention LUT for the selected machine type.
    // MachineType is shared between emulator_config.h and contention.h
    // (emulator_config.h now includes contention.h for this definition).
    contention_.build(cfg.type);

    // Install M1-cycle callback for RETI detection (ED 4D sequence).
    // When RETI is executed, notify the Im2Controller so it can clear the
    // active interrupt level in the daisy chain.
    cpu_.on_m1_cycle = [this](uint16_t /*pc*/, uint8_t opcode) {
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

    // Registers 0x19-0x1C: Sprite clip window
    nextreg_.set_write_handler(0x19, [this](uint8_t v) { sprites_.set_clip_x1(v); });
    nextreg_.set_write_handler(0x1A, [this](uint8_t v) { sprites_.set_clip_x2(v); });
    nextreg_.set_write_handler(0x1B, [this](uint8_t v) { sprites_.set_clip_y1(v); });
    nextreg_.set_write_handler(0x1C, [this](uint8_t v) { sprites_.set_clip_y2(v); });

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

    // --- ROM loading ---

    // Attempt to load the 48K ROM into ROM slot 0 from the standard path.
    // Failure is non-fatal; the machine will not boot to BASIC but the
    // emulator can still run (useful for testing without a ROM file).
    if (!rom_.load(0, "roms/48.rom")) {
        Log::emulator()->warn("could not load roms/48.rom — continuing without ROM (BASIC will not boot)");
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
    scheduler_.schedule(frame_cycle_ + INT_FIRE_OFFSET, EventType::CPU_INT,
        [this]() { cpu_.request_interrupt(0xFF); });

    // Notify copper of frame start (resets PC in mode 11).
    copper_.on_vsync();

    // Audio timing constants.
    // PSG clock = 28 MHz / 16 = 1.75 MHz → one PSG tick every 16 master cycles.
    static constexpr uint64_t PSG_DIVISOR = 16;
    // Sample generation: 28 MHz / 44100 Hz ≈ 634.92 master cycles per sample.
    // Use Bresenham-style accumulator: generate sample every time accum >= MASTER_CLOCK_HZ.
    static constexpr uint64_t SAMPLE_THRESHOLD = MASTER_CLOCK_HZ;

    while (clock_.get() < frame_end) {
        // Execute one CPU instruction; returns T-states consumed.
        int tstates = cpu_.execute();
        // Convert T-states to 28 MHz master cycles.
        // cpu_divisor() returns 8 at 3.5 MHz, 4 at 7 MHz, 2 at 14 MHz, 1 at 28 MHz.
        uint64_t master_cycles = static_cast<uint64_t>(tstates) * clock_.cpu_divisor();
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
