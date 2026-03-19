#include "core/emulator.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

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

    // Apply CPU speed to the clock.
    clock_.reset();
    clock_.set_cpu_speed(cfg.cpu_speed_mhz);

    // Allocate the framebuffer and fill with black (ARGB: 0xFF000000).
    framebuffer_.assign(FRAMEBUFFER_PIXELS, 0xFF000000u);

    // Clear any stale scheduler events from a previous session.
    scheduler_.reset();

    frame_cycle_ = 0;

    // Subsystem resets.
    ram_.reset();
    mmu_.reset();
    nextreg_.reset();
    cpu_.reset();

    // Build contention LUT for the selected machine type.
    // MachineType is shared between emulator_config.h and contention.h
    // (emulator_config.h now includes contention.h for this definition).
    contention_.build(cfg.type);

    // --- NextREG write handlers ---

    // Register 0x07: CPU speed selector
    //   0 = 3.5 MHz, 1 = 7 MHz, 2 = 14 MHz, 3 = 28 MHz
    nextreg_.set_write_handler(0x07, [this](uint8_t v) {
        clock_.set_cpu_speed(v == 0 ? 4 : v == 1 ? 7 : v == 2 ? 14 : 28);
    });

    // Registers 0x50–0x57: MMU slot→page mapping (one register per slot)
    for (int i = 0; i < 8; ++i) {
        nextreg_.set_write_handler(static_cast<uint8_t>(0x50 + i),
            [this, i](uint8_t v) { mmu_.set_page(i, v); });
    }

    // --- Port dispatch handlers ---

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

    // --- ROM loading ---

    // Attempt to load the 48K ROM into ROM slot 0 from the standard path.
    // Failure is non-fatal; the machine will not boot to BASIC but the
    // emulator can still run (useful for testing without a ROM file).
    if (!rom_.load(0, "roms/48.rom")) {
        std::fprintf(stderr,
            "[emulator] Warning: could not load roms/48.rom — "
            "continuing without ROM (BASIC will not boot)\n");
    }

    return true;
}

void Emulator::run_frame()
{
    const uint64_t frame_end = frame_cycle_ + MASTER_CYCLES_PER_FRAME;

    while (clock_.get() < frame_end) {
        // Execute one CPU instruction; returns T-states consumed.
        int tstates = cpu_.execute();
        // Convert T-states to 28 MHz master cycles.
        // cpu_divisor() returns 8 at 3.5 MHz, 4 at 7 MHz, 2 at 14 MHz, 1 at 28 MHz.
        clock_.tick(tstates * clock_.cpu_divisor());
        // Drain any scheduler events that have become due.
        scheduler_.run_until(clock_.get());
    }

    frame_cycle_ = frame_end;
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
    // Stub: fill the scanline with solid black so the host can verify the
    // framebuffer is being written.
    //
    // Real implementation (Phase 2+):
    //   1. Composite ULA → LoRes → Layer2 → Tilemap → Sprites.
    //   2. Accumulate audio samples for the line period.
    //   3. Check / fire IM2 frame interrupt at line 1.

    if (line < FRAMEBUFFER_HEIGHT) {
        const uint32_t color = 0xFF000000u;
        uint32_t* row = framebuffer_.data() + line * FRAMEBUFFER_WIDTH;
        std::fill(row, row + FRAMEBUFFER_WIDTH, color);
    }
}

void Emulator::on_vsync()
{
    // Stub: nothing to do yet.
    // Real implementation:
    //   - Signal the platform layer that a new frame is ready.
    //   - Reset per-frame state (floating bus cache, sprite collision flags).
}
