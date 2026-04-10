#pragma once

#include <cstdint>
#include <memory>
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
#include "debug/call_stack.h"
#include "audio/beeper.h"
#include "audio/turbosound.h"
#include "audio/dac.h"
#include "audio/mixer.h"
#include "debug/debug_state.h"
#include "core/tap_loader.h"
#include "core/tzx_loader.h"
#include "core/sna_loader.h"
#include "core/szx_loader.h"
#include "core/wav_loader.h"
#include "core/video_recorder.h"
#include "core/rzx_player.h"
#include "core/rzx_recorder.h"
#include "debug/rewind_buffer.h"

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
    /// When fast_load is true (default), uses ROM trap interception.
    /// When false, uses real-time EAR bit simulation.
    /// Returns true on success.
    bool load_tap(const std::string& path, bool fast_load = true);

    /// Load a TZX file and attach it as the virtual tape.
    /// When fast_load is true (default), uses ROM trap interception.
    /// When false, uses real-time EAR bit simulation via ZOT player.
    /// Returns true on success.
    bool load_tzx(const std::string& path, bool fast_load = true);

    /// Load an SNA snapshot file into the emulator.  Returns true on success.
    bool load_sna(const std::string& path);

    /// Load an SZX (zx-state) snapshot file. Returns true on success.
    bool load_szx(const std::string& path);

    /// Load a WAV file and start real-time EAR bit playback.
    /// WAV loading is always real-time (no fast-load possible).
    /// Returns true on success.
    bool load_wav(const std::string& path);

    /// Load an RZX file and start playback.  Returns true on success.
    bool load_rzx(const std::string& path);

    /// Start recording RZX input to the given file path.
    bool start_rzx_recording(const std::string& path);

    /// Stop RZX recording and write the file.
    void stop_rzx_recording();

    /// Access the RZX player/recorder.
    RzxPlayer& rzx_player() { return rzx_player_; }
    RzxRecorder& rzx_recorder() { return rzx_recorder_; }

    /// Access the tape loader (e.g. for UI tape controls).
    TapLoader& tape() { return tape_; }
    const TapLoader& tape() const { return tape_; }

    /// Access the TZX loader.
    TzxLoader& tzx_tape() { return tzx_tape_; }
    const TzxLoader& tzx_tape() const { return tzx_tape_; }

    /// Access the WAV loader.
    WavLoader& wav_tape() { return wav_tape_; }
    const WavLoader& wav_tape() const { return wav_tape_; }

    /// Access the video recorder.
    VideoRecorder& video_recorder() { return video_recorder_; }
    const VideoRecorder& video_recorder() const { return video_recorder_; }

    /// Start recording video/audio to `output_path`.
    bool start_recording(const std::string& output_path);

    /// Stop recording and finalize the output file.
    bool stop_recording();

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
    Ram&          ram()       { return ram_; }
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
    CallStack&    call_stack(){ return call_stack_; }
    Renderer&     renderer()  { return renderer_; }

    DebugState& debug_state() { return debug_state_; }
    const DebugState& debug_state() const { return debug_state_; }

    /// Current scanline (0..LINES_PER_FRAME-1) within the frame.
    int current_scanline() const;

    /// Current horizontal counter (pixel column, 0..PIXELS_PER_LINE-1) within the current scanline.
    int current_hc() const;

    /// Snapshot raster position at the current clock value (call when pausing).
    void snapshot_raster();

    /// Raster position from the last snapshot (valid when paused).
    int paused_vc() const { return paused_vc_; }
    int paused_hc() const { return paused_hc_; }

    /// Current master cycle within the current frame.
    uint64_t current_frame_cycle() const { return frame_cycle_; }

    /// Execute a single CPU instruction with all subsystem ticking.
    /// Returns T-states consumed. Used by debugger step operations.
    int execute_single_instruction();

    const EmulatorConfig& config() const { return config_; }

    // -----------------------------------------------------------------------
    // State serialisation (used by RewindBuffer)
    // -----------------------------------------------------------------------

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    /// Access the rewind buffer (may be null if disabled).
    RewindBuffer* rewind_buffer() { return rewind_buffer_.get(); }
    const RewindBuffer* rewind_buffer() const { return rewind_buffer_.get(); }

    /// True if snapshot-taking is active (can be toggled without freeing the buffer).
    bool rewind_enabled() const { return rewind_enabled_; }
    void set_rewind_enabled(bool v) { rewind_enabled_ = v; }

    /// Resize (or create/destroy) the rewind buffer at runtime.
    /// Clears all existing snapshots. frames=0 disables rewind.
    void resize_rewind_buffer(int frames);

    /// True if replay_mode is active (suppresses audio/video during fast-forward).
    bool replay_mode() const { return replay_mode_; }
    void set_replay_mode(bool v) { replay_mode_ = v; }

    /// Logical frame counter (incremented each run_frame()).
    uint32_t frame_num() const { return frame_num_; }

    // -----------------------------------------------------------------------
    // Rewind / backwards execution
    // -----------------------------------------------------------------------

    /// Rewind to the nearest snapshot at or before target_cycle, then silently
    /// fast-forward to that exact cycle. Pauses the debugger at the target.
    /// Returns the cycle actually reached (may differ if the trace doesn't
    /// contain target_cycle exactly — lands on the nearest instruction boundary).
    /// Returns UINT64_MAX if the rewind buffer is empty or disabled.
    uint64_t rewind_to_cycle(uint64_t target_cycle);

    /// Step back N instructions using the TraceLog for target-cycle lookup.
    /// Requires TraceLog to be enabled.  Returns true on success.
    bool step_back(int n = 1);

    /// Rewind to the start of frame frame_num (must be in the rewind buffer).
    /// Returns true on success.
    bool rewind_to_frame(uint32_t frame_num);

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
    CallStack       call_stack_;
    TapLoader       tape_;
    TzxLoader       tzx_tape_;
    WavLoader       wav_tape_;
    VideoRecorder   video_recorder_;
    RzxPlayer       rzx_player_;
    RzxRecorder     rzx_recorder_;
    uint32_t        rzx_frame_instruction_count_ = 0;

    /// Rewind snapshot buffer (null when disabled).
    std::unique_ptr<RewindBuffer> rewind_buffer_;

    /// Logical frame counter — incremented each run_frame(); saved in snapshots.
    uint32_t frame_num_   = 0;

    /// When true, snapshot-taking is active (independent of buffer allocation).
    bool rewind_enabled_ = false;

    /// When true, suppresses audio/video output during fast-forward replay.
    bool replay_mode_ = false;

    /// Boot ROM (8K FPGA bootloader, loaded from --boot-rom).
    std::vector<uint8_t> boot_rom_;

    /// ARGB8888 framebuffer (320 × 256 pixels).
    std::vector<uint32_t> framebuffer_;

    /// Master cycle counter at which the current frame started.
    uint64_t frame_cycle_ = 0;

    /// Raster position snapshotted at pause time.
    int paused_vc_ = 0;
    int paused_hc_ = 0;

    /// Raster position at the end of the last completed frame (saved before frame_cycle_ advances).
    int last_frame_vc_ = 0;
    int last_frame_hc_ = 0;

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
