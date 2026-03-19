#pragma once

#include <cstdint>
#include <vector>

#include "core/clock.h"
#include "core/emulator_config.h"
#include "core/scheduler.h"

/// Top-level machine class.
///
/// Owns the Clock, Scheduler, and (eventually) all subsystems.
/// The host loop calls run_frame() once per display frame.
///
/// Subsystem stubs will be filled in by later phases:
///   Phase 1 — CPU (Z80N), MMU, ROM loading, keyboard
///   Phase 2 — ULA video, contention model, frame interrupt
///   Phase 3 — Layer 2, sprites, tilemap, copper
///   Phase 4 — Audio (AY × 3, DAC, beeper)
///   Phase 5 — DivMMC, CTC, UART, DMA, full NextREG file
class Emulator {
public:
    explicit Emulator() = default;

    // Non-copyable, non-movable (owns large state).
    Emulator(const Emulator&)            = delete;
    Emulator& operator=(const Emulator&) = delete;

    /// Initialize all subsystems from config.
    /// Returns true on success, false if a required resource is missing
    /// (e.g. ROM file not found).
    bool init(const EmulatorConfig& cfg);

    /// Advance emulation by exactly one video frame.
    ///
    /// Internally:
    ///   - Runs the Scheduler for MASTER_CYCLES_PER_FRAME master cycles.
    ///   - Between scheduler events the CPU executes instructions
    ///     (stub: no-op until Phase 1 CPU integration).
    ///   - At each scanline boundary: renders the scanline, accumulates
    ///     audio samples, checks interrupts.
    void run_frame();

    /// Perform a hard reset: reinitialize all subsystems, clear RAM, reload ROM.
    void reset();

    // -----------------------------------------------------------------------
    // Framebuffer access
    // -----------------------------------------------------------------------

    /// Returns a pointer to the 320×256 ARGB8888 framebuffer.
    /// The pointer is valid for the lifetime of the Emulator object.
    /// Contents are updated by run_frame().
    uint32_t* get_framebuffer() { return framebuffer_.data(); }

    /// Framebuffer width in pixels.
    int get_framebuffer_width()  const { return FRAMEBUFFER_WIDTH; }

    /// Framebuffer height in pixels.
    int get_framebuffer_height() const { return FRAMEBUFFER_HEIGHT; }

    // -----------------------------------------------------------------------
    // Accessors (used by the debugger interface)
    // -----------------------------------------------------------------------

    Clock&     clock()     { return clock_; }
    Scheduler& scheduler() { return scheduler_; }
    const EmulatorConfig& config() const { return config_; }

private:
    static constexpr int FRAMEBUFFER_WIDTH  = 320;
    static constexpr int FRAMEBUFFER_HEIGHT = 256;
    static constexpr int FRAMEBUFFER_PIXELS = FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT;

    EmulatorConfig config_;
    Clock          clock_;
    Scheduler      scheduler_;

    /// ARGB8888 framebuffer (320 × 256 pixels).
    std::vector<uint32_t> framebuffer_;

    /// Master cycle counter at which the current frame started.
    uint64_t frame_start_cycle_ = 0;

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    /// Schedule a full frame's worth of SCANLINE events into the scheduler.
    void schedule_frame_events();

    /// Called by the SCANLINE event handler for scanline `line`.
    void on_scanline(int line);

    /// Called by the VSYNC event handler.
    void on_vsync();
};
