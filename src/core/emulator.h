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
    ///
    /// `preserve_memory` — when true, SKIP ram_.reset() / rom_.reset() /
    /// ROM-from-disk loading / boot-ROM reload / SRAM-from-rom seeding.
    /// Used by soft_reset() to keep tbblue-loaded NextZXOS content in SRAM
    /// across a RESET_SOFT (VHDL: SRAM is not in the reset domain).
    bool init(const EmulatorConfig& cfg, bool preserve_memory = false);

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

    /// Perform a soft reset (tbblue RESET_SOFT / NR 0x02 bit 0).
    /// Resets flip-flops (CPU, MMU, peripherals, NextReg) but preserves
    /// RAM contents (including the Next ROM-in-SRAM window), ROM buffer,
    /// boot-ROM overlay state, and other non-FF state. Matches VHDL
    /// zxnext_top_issue5.vhd:836-880 (soft reset domain subset).
    void soft_reset();

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

    /// Returns a pointer to the ARGB8888 framebuffer (up to 640×256).
    /// The pointer is valid for the lifetime of the Emulator object.
    /// Contents are updated by run_frame().
    uint32_t* get_framebuffer() { return framebuffer_.data(); }

    /// Framebuffer width in pixels (320 or 640, depending on last frame).
    int get_framebuffer_width()  const { return last_frame_width_; }

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
    Ula&          ula()       { return renderer_.ula(); }
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
    const MachineTiming& timing() const { return timing_; }

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
    static constexpr int FRAMEBUFFER_WIDTH      = 320;
    static constexpr int FRAMEBUFFER_WIDTH_MAX  = 640;
    static constexpr int FRAMEBUFFER_HEIGHT     = 256;
    static constexpr int FRAMEBUFFER_PIXELS_MAX = FRAMEBUFFER_WIDTH_MAX * FRAMEBUFFER_HEIGHT;

    EmulatorConfig config_;
    MachineTiming  timing_;          // per-machine timing from VHDL
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

    /// ARGB8888 framebuffer (up to 640 × 256 pixels).
    std::vector<uint32_t> framebuffer_;

    /// Actual width of the last rendered frame (320 or 640).
    int last_frame_width_ = FRAMEBUFFER_WIDTH;

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

    /// NR 0x08 bits 5..0 mirror for read-back composition per VHDL
    /// zxnext.vhd:5906. Bit 7 is write-strobe-only (derived from paging
    /// lock on read); bit 6 lives on Mmu as contention_disabled_. Bits
    /// 5..0 are stored in VHDL as individual signals (stereo, internal
    /// speaker, DAC en, port_ff_rd en, turbosound en, issue2 keyboard);
    /// we cache them collectively here so NR 0x08 reads return the last
    /// written value for signals not yet hooked to live subsystem state.
    uint8_t nr_08_stored_low_ = 0;

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

    // --- IM2 DMA delay enables (NextREG 0xCC / 0xCD / 0xCE) ---
    // VHDL zxnext.vhd:1259-1262, :5629-5637, :6257-6263.
    // NR 0xCC bit 7   = dma delay on NMI    (nr_cc_dma_int_en_0_7)
    // NR 0xCC bits 1:0 = dma delay enable for ULA/line ints (nr_cc_dma_int_en_0_10)
    // NR 0xCD bits 7:0 = dma delay enable for CTC channels 7:0 (nr_cd_dma_int_en_1)
    // NR 0xCE bits 6:4 = dma delay enable, UART1 Tx/Rx/Rx-error (nr_ce_dma_int_en_2_654)
    // NR 0xCE bits 2:0 = dma delay enable, UART0 Tx/Rx/Rx-error (nr_ce_dma_int_en_2_210)
    bool     nr_cc_dma_delay_on_nmi_   = false;  ///< NR 0xCC bit 7
    uint8_t  nr_cc_dma_delay_en_ula_   = 0;      ///< NR 0xCC bits 1:0
    uint8_t  nr_cd_dma_delay_en_ctc_   = 0;      ///< NR 0xCD bits 7:0
    uint8_t  nr_ce_dma_delay_en_uart1_ = 0;      ///< NR 0xCE bits 6:4
    uint8_t  nr_ce_dma_delay_en_uart0_ = 0;      ///< NR 0xCE bits 2:0
    bool     im2_dma_delay_latched_    = false;  ///< VHDL zxnext.vhd:2005-2007

    // --- Joystick IO-mode pin7 (NR 0x0B joy_iomode) ---
    // VHDL joy_iomode_pin7, zxnext.vhd:3512-3534. Resets to '1' on hard reset
    // (zxnext.vhd:3516). In NR 0x0B joy_iomode="01" it toggles on each CTC
    // channel-3 ZC/TO pulse (gated by iomode_0 and current pin7; see
    // zxnext.vhd:3521-3524). In joy_iomode="00" it tracks iomode_0 continuously
    // (NR 0x0B write side — handled by NR 0x0B write_handler, not this field's
    // update path). In joy_iomode="10"/"11" it is driven from UART0/UART1 Tx.
    // Consumed by the Input subsystem (out of scope at this phase).
    bool     joy_iomode_pin7_          = true;   ///< VHDL zxnext.vhd:3516 (reset '1')

public:
    /// Compose the 14-bit im2_dma_int_en mask from NR 0xCC/0xCD/0xCE bits.
    /// VHDL zxnext.vhd:1957-1958.  Returned bit layout (MSB to LSB):
    ///   bit 13 = NR CE[6]  (UART1 Tx)
    ///   bit 12 = NR CE[2]  (UART0 Tx)
    ///   bit 11 = NR CC[0]  (ULA)
    ///   bits 10:3 = NR CD[7:0]  (CTC 7..0)
    ///   bit 2  = NR CE[5] | NR CE[4]  (UART1 Rx / Rx-error)
    ///   bit 1  = NR CE[1] | NR CE[0]  (UART0 Rx / Rx-error)
    ///   bit 0  = NR CC[1]  (line)
    uint16_t compose_im2_dma_int_en() const;

    /// Compute the next im2_dma_delay output given the three inputs, per
    /// VHDL zxnext.vhd:2007:
    ///   im2_dma_delay_next = im2_dma_int OR (nmi_activated AND nr_cc_bit7)
    ///                        OR (im2_dma_delay_prev AND dma_delay)
    /// Updates the latched previous value and returns the new output.
    bool update_im2_dma_delay(bool im2_dma_int, bool nmi_activated, bool dma_delay);

    /// Read-only view of the latched im2_dma_delay (for tests).
    bool im2_dma_delay() const { return im2_dma_delay_latched_; }

private:

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
