#pragma once

#include <cstdint>
#include <vector>

#include "core/clock.h"
#include "core/emulator_config.h"
#include "core/scheduler.h"
#include "cpu/z80_cpu.h"
#include "cpu/im2.h"
#include "memory/ram.h"
#include "memory/rom.h"
#include "memory/mmu.h"
#include "memory/contention.h"
#include "port/port_dispatch.h"
#include "port/nextreg.h"
#include "video/renderer.h"
#include "video/palette.h"
#include "video/layer2.h"
#include "video/sprites.h"
#include "video/tilemap.h"
#include "peripheral/copper.h"
#include "peripheral/ctc.h"
#include "peripheral/dma.h"
#include "peripheral/spi.h"
#include "peripheral/i2c.h"
#include "peripheral/uart.h"
#include "peripheral/divmmc.h"
#include "peripheral/sd_card.h"
#include "input/keyboard.h"
#include "debug/trace.h"
#include "audio/beeper.h"
#include "audio/turbosound.h"
#include "audio/dac.h"
#include "audio/mixer.h"
#include "debug/debug_state.h"
#include "core/tap_loader.h"

/// Top-level machine class.
///
/// Owns the Clock, Scheduler, and all core subsystems.
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
    Emulator();

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
    ///   - Runs the CPU for MASTER_CYCLES_PER_FRAME master cycles.
    ///   - Between scheduler events the CPU executes instructions.
    ///   - At each scanline boundary: renders the scanline, accumulates
    ///     audio samples, checks interrupts.
    void run_frame();

    /// Perform a hard reset: reinitialize all subsystems, clear RAM, reload ROM.
    void reset();

    /// Load a raw binary file into RAM at `org` and set PC to `pc`.
    /// Called after init() when --inject is used.  Returns true on success.
    bool inject_binary(const std::string& path, uint16_t org, uint16_t pc);

    /// Load a NEX file into the emulator.  Returns true on success.
    bool load_nex(const std::string& path);

    /// Load a TAP file and attach it as the virtual tape.
    /// The tape is played via fast-load ROM trap interception.
    /// Returns true on success.
    bool load_tap(const std::string& path);

    /// Access the tape loader (e.g. for UI tape controls).
    TapLoader& tape() { return tape_; }
    const TapLoader& tape() const { return tape_; }

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

    Clock&        clock()     { return clock_; }
    Scheduler&    scheduler() { return scheduler_; }
    Mmu&          mmu()       { return mmu_; }
    PortDispatch& port()      { return port_; }
    NextReg&      nextreg()   { return nextreg_; }
    Z80Cpu&       cpu()       { return cpu_; }
    Keyboard&     keyboard()  { return keyboard_; }
    PaletteManager& palette() { return palette_; }
    Layer2&       layer2()    { return layer2_; }
    SpriteEngine& sprites()   { return sprites_; }
    Tilemap&      tilemap()   { return tilemap_; }
    Copper&       copper()    { return copper_; }
    Ctc&          ctc()       { return ctc_; }
    Dma&          dma()       { return dma_; }
    SpiMaster&    spi()       { return spi_; }
    I2cController& i2c()     { return i2c_; }
    Uart&         uart()      { return uart_; }
    DivMmc&       divmmc()    { return divmmc_; }
    Beeper&       beeper()    { return beeper_; }
    TurboSound&   turbosound(){ return turbosound_; }
    Dac&          dac()       { return dac_; }
    Mixer&        mixer()     { return mixer_; }
    TraceLog&     trace_log() { return trace_log_; }

    DebugState& debug_state() { return debug_state_; }
    const DebugState& debug_state() const { return debug_state_; }

    /// Execute a single CPU instruction with all subsystem ticking.
    /// Returns T-states consumed. Used by debugger step operations.
    int execute_single_instruction();

    const EmulatorConfig& config() const { return config_; }

    /// Floating bus read — returns the byte the ULA is currently fetching
    /// from VRAM at this T-state position. Only active in 48K/128K modes.
    /// Returns 0xFF when outside active display or in Next/Pentagon modes.
    uint8_t floating_bus_read() const;

private:
    static constexpr int FRAMEBUFFER_WIDTH  = 320;
    static constexpr int FRAMEBUFFER_HEIGHT = 256;
    static constexpr int FRAMEBUFFER_PIXELS = FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT;

    EmulatorConfig config_;
    Clock          clock_;
    Scheduler      scheduler_;

    // Subsystem members — declaration order matters for initializer list:
    // ram_ and rom_ must come before mmu_, and mmu_+port_ before cpu_.
    Ram            ram_;
    Rom            rom_;
    Mmu            mmu_;
    PortDispatch   port_;
    NextReg        nextreg_;
    Z80Cpu          cpu_;
    Im2Controller   im2_;
    ContentionModel contention_;
    PaletteManager  palette_;
    Layer2          layer2_;
    SpriteEngine    sprites_;
    Tilemap         tilemap_;
    Copper          copper_;
    Ctc             ctc_;
    Dma             dma_;
    SpiMaster       spi_;
    I2cController   i2c_;
    I2cRtc          rtc_;
    Uart            uart_;
    DivMmc          divmmc_;
    SdCardDevice    sd_card_;
    Renderer        renderer_;
    Keyboard        keyboard_;
    Beeper          beeper_;
    TurboSound      turbosound_;
    Dac             dac_;
    Mixer           mixer_;
    DebugState      debug_state_;
    TraceLog        trace_log_;
    TapLoader       tape_;

    /// Boot ROM (8K FPGA bootloader, loaded from --boot-rom).
    std::vector<uint8_t> boot_rom_;

    /// ARGB8888 framebuffer (320 × 256 pixels).
    std::vector<uint32_t> framebuffer_;

    /// Master cycle counter at which the current frame started.
    uint64_t frame_cycle_ = 0;

    /// FUSE tstates value at frame start (for contention position calc).
    uint32_t frame_ts_start_ = 0;

    /// Audio timing: fractional accumulators for PSG ticking and sample generation.
    /// PSG clock = 28 MHz / 16 = 1.75 MHz.
    uint64_t psg_accum_ = 0;      ///< Accumulates master cycles for PSG tick timing
    uint64_t sample_accum_ = 0;   ///< Accumulates master cycles for sample generation

    /// DAC enable flag (NextREG 0x08 bit 3).
    bool dac_enabled_ = false;

    // --- Line interrupt state (NextREG 0x22/0x23) ---
    bool     line_int_enabled_   = false;  ///< NextREG 0x22 bit 1
    bool     ula_int_disabled_   = false;  ///< NextREG 0x22 bit 2
    uint16_t line_int_value_     = 0;      ///< 9-bit line number (0x22 bit0 + 0x23)

    // --- Clip window rotating write indices (NextREG 0x18/0x19/0x1A/0x1B) ---
    // Each clip register cycles through X1,X2,Y1,Y2 on successive writes.
    uint8_t clip_l2_idx_   = 0;   ///< Layer 2 clip write index (0-3)
    uint8_t clip_spr_idx_  = 0;   ///< Sprite clip write index (0-3)
    uint8_t clip_ula_idx_  = 0;   ///< ULA/LoRes clip write index (0-3)
    uint8_t clip_tm_idx_   = 0;   ///< Tilemap clip write index (0-3)

    // --- IM2 hardware mode state (NextREG 0xC0–0xCF) ---
    bool     im2_hw_mode_        = false;  ///< NextREG 0xC0 bit 0
    uint8_t  im2_vector_base_    = 0;      ///< NextREG 0xC0 bits 7:5
    uint8_t  im2_int_enable_[3]  = {0x81, 0, 0};  ///< 0xC4-0xC6 (soft reset defaults)
    uint8_t  im2_int_status_[3]  = {};     ///< 0xC8-0xCA

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
