#include "core/emulator.h"

#include <algorithm>
#include <cstring>

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

    frame_start_cycle_ = 0;

    // TODO Phase 1: load ROM, initialize MMU, CPU, keyboard.
    // TODO Phase 2: initialize ULA, contention LUT, frame interrupt.
    // TODO Phase 3: initialize Layer 2, sprites, tilemap, copper.
    // TODO Phase 4: initialize audio subsystems.
    // TODO Phase 5: initialize DivMMC, CTC, UART, DMA, full NextREG.

    return true;
}

void Emulator::run_frame()
{
    // Schedule all events for this frame relative to frame_start_cycle_.
    schedule_frame_events();

    const uint64_t frame_end = frame_start_cycle_ + MASTER_CYCLES_PER_FRAME;

    // Run the scheduler up to the end of the frame.
    // Between events the CPU would execute instructions (stub: no-op).
    scheduler_.run_until(frame_end);

    // Advance the master clock to the end of the frame.
    clock_.tick(static_cast<int>(frame_end - clock_.get()));

    // Advance frame tracking.
    frame_start_cycle_ = frame_end;
}

void Emulator::reset()
{
    clock_.reset();
    scheduler_.reset();
    frame_start_cycle_ = 0;

    // Clear framebuffer to black.
    std::fill(framebuffer_.begin(), framebuffer_.end(), 0xFF000000u);

    // Re-run init to restore consistent state.
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
            frame_start_cycle_ + static_cast<uint64_t>(line) * MASTER_CYCLES_PER_LINE;

        // Capture `line` by value for the lambda.
        scheduler_.schedule(line_cycle, EventType::SCANLINE,
            [this, line]() { on_scanline(line); });
    }

    // VSYNC fires at the very end of the frame.
    scheduler_.schedule(
        frame_start_cycle_ + MASTER_CYCLES_PER_FRAME,
        EventType::VSYNC,
        [this]() { on_vsync(); });
}

void Emulator::on_scanline(int line)
{
    // Stub: fill the scanline with a solid colour pattern so the host can
    // verify the framebuffer is being written.
    //
    // Real implementation (Phase 2+):
    //   1. Composite ULA → LoRes → Layer2 → Tilemap → Sprites.
    //   2. Accumulate audio samples for the line period.
    //   3. Check / fire IM2 frame interrupt at line 1.

    if (line < FRAMEBUFFER_HEIGHT) {
        const uint32_t color = 0xFF000000u; // solid black stub
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
