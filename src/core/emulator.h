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
#include "video/timing.h"
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
#include "peripheral/nmi_source.h"
#include "peripheral/sd_card.h"
#include "input/keyboard.h"
#include "input/joystick.h"
#include "input/mouse.h"
#include "input/md6_connector_x2.h"
#include "input/membrane_stick.h"
#include "input/iomode.h"
#include "input/emu_fnkeys.h"
#include "debug/trace.h"
#include "debug/call_stack.h"
#include "audio/beeper.h"
#include "audio/turbosound.h"
#include "audio/dac.h"
#include "audio/i2s.h"
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

    /// Returns a pointer to the ARGB8888 framebuffer (640×256).
    /// The pointer is valid for the lifetime of the Emulator object.
    /// Contents are updated by run_frame().
    uint32_t* get_framebuffer() { return framebuffer_.data(); }

    /// Framebuffer width in pixels (canonical 640 post-G104).
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
    ContentionModel& contention() { return contention_; }
    /// VHDL-faithful raster counter (zxula_timing). Phase-2 contention
    /// wiring (2026-04-26) makes this a production-wired observable —
    /// `pos()` returns the live (hc, vc) used by the contention tick
    /// path. Tests use it via emulator.video_timing().pos().
    VideoTiming&  video_timing() { return video_timing_; }
    const VideoTiming& video_timing() const { return video_timing_; }
    Keyboard&       keyboard()       { return keyboard_; }
    Joystick&       joystick()       { return joystick_; }
    KempstonMouse&  mouse()          { return mouse_; }
    Md6ConnectorX2& md6()            { return md6_; }
    MembraneStick&  membrane_stick() { return membrane_stick_; }
    IoMode&         iomode()         { return iomode_; }
    /// F-key FSM (G132) — VHDL input/membrane/emu_fnkeys.vhd. Tests drive
    /// it directly via press_membrane_fkey()/tick(); production code wires
    /// it from the GUI hotkey path (deferred — G152 already handles the
    /// reset/NMI keys).
    EmuFnKeys&      emu_fnkeys()     { return emu_fnkeys_; }
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
    NmiSource&    nmi_source(){ return nmi_source_; }
    Im2Controller& im2()       { return im2_; }
    Beeper&       beeper()    { return beeper_; }
    TurboSound&   turbosound(){ return turbosound_; }
    Dac&          dac()       { return dac_; }
    I2s&          i2s()       { return i2s_; }
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

    // ══════════════════════════════════════════════════════════════════════
    // === TEST-ONLY ACCESSORS (UI code must NOT call these) ==============
    //
    // Phase 2 Agent I (NR 0x06 bits 3/4 AND-gate → NMI) uses these to
    // inject signals that originate OUTSIDE the Input subsystem:
    //   - hotkey_m1 / hotkey_drive : F9 / F10 host keys (driven by the
    //     SDL event loop in production; host-key handling lives outside
    //     the current Input subsystem scope).
    //   - sw_nmi_mf / sw_nmi_divmmc : software-NMI generators strobed
    //     by NR 0x02 writes, routed to the Multiface and DivMMC NMI
    //     consumers. The NMI source/edge generator itself is not yet
    //     implemented (see memory/project_nmi_fragmented_status.md);
    //     these injects let Agent I write the NR 0x06 AND-gate tests
    //     without first building the full NMI fabric.
    //
    // Phase 2 Agent I (2026-04-21): the inject setters now feed the NMI
    // AND-gate evaluators below; the gates also depend on NR 0x06 bits
    // 3/4 (`nr_06_button_m1_nmi_en` / `nr_06_button_drive_nmi_en`),
    // which are stored from the existing NR 0x06 write handler in
    // emulator.cpp.
    // ══════════════════════════════════════════════════════════════════════
    void inject_hotkey_m1(bool on)        { test_hotkey_m1_    = on; }
    void inject_hotkey_drive(bool on)     { test_hotkey_drive_ = on; }
    void inject_sw_nmi_mf(bool on)        { test_sw_nmi_mf_    = on; }
    void inject_sw_nmi_divmmc(bool on)    { test_sw_nmi_dmmc_  = on; }

    /// G163 — Test observable: count of production line-interrupt
    /// callback fires since `reset_line_int_fire_count()` was last
    /// called. Each non-superseded `EventType::CPU_INT` lambda from
    /// `reschedule_line_interrupt()` increments this exactly once.
    /// Used by `videotiming_test` VT-G163-* rows to verify the
    /// dynamic-rescheduling fix; see also `KNOWN-FUNCTIONALITY-GAPS`
    /// G163 entry.
    uint64_t line_int_fire_count() const { return line_int_fire_count_; }
    void reset_line_int_fire_count() { line_int_fire_count_ = 0; }

    // ══════════════════════════════════════════════════════════════════════
    // Host hotkey dispatchers — VHDL `hotkey_m1` / `hotkey_drive` /
    // `hotkey_soft_reset` / `hotkey_hard_reset` (zxnext.vhd:6340-6371,
    // 2089-2091). These translate the GUI-side F1/F4/F9/F10 key edges
    // into the corresponding NmiSource / Emulator-reset paths and are
    // the single seam between gui/main_window.cpp F-key handlers and
    // the emulation core. (G152 closure.)
    // ══════════════════════════════════════════════════════════════════════

    /// F9 — VHDL `hotkey_m1` edge pulse. Asserts the Multiface NMI
    /// producer for one tick; subject to NR 0x06 bit 3
    /// (`button_m1_nmi_en`) gate per VHDL:2090.
    void on_hotkey_f9_mf_nmi();

    /// F10 — VHDL `hotkey_drive` edge pulse. Asserts the DivMMC NMI
    /// producer for one tick; subject to NR 0x06 bit 4
    /// (`button_drive_nmi_en`) gate per VHDL:2091. VHDL further gates
    /// this on `port_divmmc_io_en` (NR 0x83 bit 0); we honour that
    /// gate at the dispatcher (early-return when the DivMMC port is
    /// disabled).
    void on_hotkey_f10_divmmc_nmi();

    /// F4 — VHDL `hotkey_soft_reset` edge pulse. Strobes the
    /// `nr_02_soft_reset` path per VHDL:6370 (advances reset_type FSM
    /// and triggers `Emulator::soft_reset()`). VHDL gates this on NOT
    /// `nr_03_config_mode`; we honour that gate here.
    void on_hotkey_f4_soft_reset();

    /// F1 — VHDL `hotkey_hard_reset` edge pulse. Triggers full
    /// `Emulator::reset()` per VHDL:6371. (No config_mode gate — hard
    /// reset is unconditional.)
    void on_hotkey_f1_hard_reset();

    // ── NMI AND-gate combinational outputs (test-only readout) ────────────
    //
    // VHDL zxnext.vhd:2090-2091 (combinational, no clock):
    //   nmi_assert_mf     <= '1' when (hotkey_m1   = '1' or nmi_sw_gen_mf     = '1')
    //                                  and nr_06_button_m1_nmi_en    = '1' else '0';
    //   nmi_assert_divmmc <= '1' when (hotkey_drive = '1' or nmi_sw_gen_divmmc = '1')
    //                                  and nr_06_button_drive_nmi_en = '1' else '0';
    //
    // These accessors expose ONLY the gate combinational result. They do
    // NOT fire the Z80 /NMI line — the NMI source/edge generator and the
    // downstream nmi_state machine (zxnext.vhd:2095-2150) are still
    // unwired (see memory/project_nmi_fragmented_status.md). When that
    // fabric is built it will subscribe to these same gate signals.
    bool nmi_assert_mf() const {
        return (test_hotkey_m1_    || test_sw_nmi_mf_)   && nr_06_button_m1_nmi_en_;
    }
    bool nmi_assert_divmmc() const {
        return (test_hotkey_drive_ || test_sw_nmi_dmmc_) && nr_06_button_drive_nmi_en_;
    }

    // Read-back of NR 0x06 bits 3/4 for tests that want to assert the
    // store-side of the NextReg write path independently of the gate.
    bool nr_06_button_m1_nmi_en()    const { return nr_06_button_m1_nmi_en_; }
    bool nr_06_button_drive_nmi_en() const { return nr_06_button_drive_nmi_en_; }

    // ── Audio-side NextREG readbacks (test integration harness) ─────────
    //
    // Observables behind the NR 0x06 / NR 0x08 audio bits that VHDL
    // zxnext.vhd:5163, :5178-5182, :6436 store as standalone signals.
    // Exposed here so audio_nextreg_test can observe the post-write state
    // without reaching into private fields.

    /// NR 0x06 bit 6 — `nr_06_internal_speaker_beep` (VHDL zxnext.vhd:5163).
    bool nr_06_internal_speaker_beep() const { return nr_06_internal_speaker_beep_; }

    /// NR 0x08 bit 3 — `nr_08_dac_en` (VHDL zxnext.vhd:5179, :6436).
    /// Gates the Soundrive DAC port-write path; controls dac_.reset on low.
    bool dac_enabled() const { return dac_enabled_; }

    /// NR 0x08 bits 5..0 stored mirror, per VHDL zxnext.vhd:5906 read-back.
    ///   bit 5 = nr_08_psg_stereo_mode
    ///   bit 4 = nr_08_internal_speaker_en
    ///   bit 3 = nr_08_dac_en
    ///   bit 2 = nr_08_port_ff_rd_en
    ///   bit 1 = nr_08_psg_turbosound_en
    ///   bit 0 = nr_08_keyboard_issue2
    uint8_t nr_08_stored_low() const { return nr_08_stored_low_; }

    /// VHDL zxnext.vhd:3610-3635 `port_ff_reg`. Single store fed by four
    /// writers (port 0xFF, NR 0x69 b5:0, NR 0x22 b2, NR 0xC4 b0 inverted).
    /// Bits 2:0 = Timex screen mode (zxula.vhd:191); bits 5:3 = HI_RES
    /// paper colour (zxula.vhd:419); bit 6 = port_ff_interrupt_disable.
    uint8_t port_ff_reg() const { return port_ff_reg_; }

    /// Composite VHDL signal `beep_spkr_excl` (zxnext.vhd:6504) —
    ///   beep_spkr_excl <= nr_06_internal_speaker_beep AND
    ///                     nr_08_internal_speaker_en.
    /// Feeds `audio_mixer.exc_i` which silences EAR/MIC in the mixer.
    bool beep_spkr_excl() const {
        return nr_06_internal_speaker_beep_ && ((nr_08_stored_low_ & 0x10) != 0);
    }

    // ══════════════════════════════════════════════════════════════════════
    // Test-only: Raspberry Pi I2C bus 1 input injection.
    //
    // Per zxnext.vhd:3259, 3266, the Pi I2C bus 1 drives pi_i2c1_scl /
    // pi_i2c1_sda which are AND-ed into the SCL/SDA read values. Defaults are
    // released (1), matching the VHDL open-drain pull-up semantics: the
    // AND-gate is a no-op until a test pulls a line low.
    //
    // Task 3 UART+I2C Phase 1 scaffold — Wave E I2C-11 test uses this hook to
    // verify the VHDL AND-gate wiring end-to-end through the Emulator-level
    // public surface.
    // ══════════════════════════════════════════════════════════════════════
    void inject_pi_i2c1(bool scl, bool sda) {
        i2c_.set_pi_i2c1_scl(scl);
        i2c_.set_pi_i2c1_sda(sda);
    }

    // ══════════════════════════════════════════════════════════════════════
    // Joystick UART-RX injection (NR 0x0B iomode UART multiplex)
    //
    // Models the VHDL top-level RX mux at zxnext.vhd:3340-3341:
    //   uart0_rx <= joy_uart_rx when joy_iomode_uart_en='1'
    //               and nr_0b_joy_iomode_0='0' else i_UART0_RX;
    //   uart1_rx <= joy_uart_rx when joy_iomode_uart_en='1'
    //               and nr_0b_joy_iomode_0='1' else pi_uart_rx;
    //
    // When NR 0x0B bit 7 (iomode_en) = 1, one UART channel's RX line is
    // driven from the joystick connector's UART RX pin instead of its
    // normal source. Bit 0 (iomode_0) selects WHICH channel receives the
    // joystick feed: 0 => UART 0 (ESP path shadowed), 1 => UART 1 (Pi
    // path shadowed). When iomode_en = 0 the mux is disabled and this
    // injection path is a no-op (the physical ESP/Pi pins remain the
    // sole RX drivers, which in jnext are surfaced via `uart().inject_rx`).
    //
    // This call is intended for integration tests and future joystick-
    // connector peripherals that model a serial RX pin; production code
    // still drives the "native" ESP/Pi paths via `uart().inject_rx()`.
    // ══════════════════════════════════════════════════════════════════════
    void inject_joy_uart_rx(uint8_t byte) {
        if (!iomode_.iomode_en()) return;            // mux disabled
        const int ch = iomode_.iomode_0() ? 1 : 0;   // VHDL:3340-3341
        uart_.inject_rx(ch, byte);
    }

private:
    // Canonical 640×256 ARGB framebuffer (G104). Vertical 2× scaling for
    // square-pixel display (640×512) is applied at the GUI/screenshot layer
    // (Phase 7).
    static constexpr int FRAMEBUFFER_WIDTH  = 640;
    static constexpr int FRAMEBUFFER_HEIGHT = 256;
    static constexpr int FRAMEBUFFER_PIXELS = FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT;

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
    VideoTiming     video_timing_;
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
    NmiSource       nmi_source_;  // Phase 1 scaffold (TASK-NMI-SOURCE-PIPELINE-PLAN).

    // NR 0xD8 bit 0 — `nr_d8_io_trap_fdc_en` (VHDL zxnext.vhd:1263).
    // Gates the port 0x2FFD / 0x3FFD trap decode that strobes the
    // Multiface NMI path via `nmi_gen_iotrap` (VHDL:2598-2602, 3835).
    // Power-on default '0' per VHDL:5107.
    bool nr_d8_io_trap_fdc_en_ = false;

    // NR 0xD9 — `nr_d9_iotrap_write` (VHDL zxnext.vhd:1264, 3887-3898).
    // Captures the CPU write byte that triggered an IO trap on
    // port_3ffd_wr (the VHDL clause `nr_d9_iotrap_write <= cpu_do`
    // when `port_3ffd_wr = '1' AND nmi_accept_cause = '1'`). Also
    // accepts direct firmware writes via NR 0xD9 (`nr_d9_we`,
    // VHDL:4901, :3894-3895). Reset value '0' (VHDL:3891).
    uint8_t nr_d9_iotrap_write_ = 0;

    // NR 0xDA — `nr_da_iotrap_cause` (VHDL zxnext.vhd:1265, 3866-3883).
    // 2-bit encoding of which FDC port event triggered the most
    // recent IO trap:
    //   "01" = port_2ffd READ
    //   "10" = port_3ffd READ
    //   "11" = port_3ffd WRITE
    // Read-only from NR 0xDA itself (VHDL:5645-5646 commented out).
    // Cleared by NR 0x02 write with bit 4 = 0 (VHDL:3879-3880).
    // Reset value '0' (VHDL:3870).
    uint8_t nr_da_iotrap_cause_ = 0;
    SdCardDevice    sd_card_;
    Renderer        renderer_;
    Keyboard        keyboard_;
    // Input subsystem — Phase 1 scaffold (Task 3). See src/input/*.
    Joystick        joystick_;
    KempstonMouse   mouse_;
    Md6ConnectorX2  md6_;
    MembraneStick   membrane_stick_;
    IoMode          iomode_;
    // F-key FSM (G132) — VHDL input/membrane/emu_fnkeys.vhd:53-202.
    // Drives F2/F3/F7/F8 side-effect strobes (NR 0x07 cpu_speed inc,
    // NR 0x05 5060/scandouble toggle, NR 0x09 scanlines inc) per
    // zxnext.vhd:5789-5791, :5839-5841, :5849-5852, :5861-5863. F1/F4/F9/F10
    // reset/NMI dispatch is handled by Emulator::on_hotkey_f*() (G152).
    EmuFnKeys       emu_fnkeys_;
    Beeper          beeper_;
    TurboSound      turbosound_;
    Dac             dac_;
    I2s             i2s_;
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

    /// ARGB8888 framebuffer (canonical 640 × 256 pixels post-G104).
    std::vector<uint32_t> framebuffer_;

    /// Master cycle counter at which the current frame started.
    uint64_t frame_cycle_ = 0;

    /// G163 — generation counter bumped on every line-interrupt
    /// (re)schedule. The scheduled `EventType::CPU_INT` lambda captures
    /// this counter by-value; at fire time it no-ops if the captured
    /// generation differs from the current one (i.e. a later schedule
    /// has superseded this event). This avoids needing a Scheduler
    /// `cancel()` API and correctly handles same-target rewrites.
    uint64_t line_int_schedule_gen_ = 0;

    /// G163 — test-observable counter incremented inside the
    /// non-superseded line-int callback. Public accessor
    /// `line_int_fire_count()` is used by VT-G163-* rows.
    uint64_t line_int_fire_count_ = 0;

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

    /// Phase-1 NmiSource seam: previous cycle's `nmi_generate_n` level,
    /// used to detect the active-low falling edge that drives
    /// `Z80Cpu::request_nmi()`. Starts `true` (line inactive) so the
    /// first cycle does not spuriously latch an NMI. See
    /// TASK-NMI-SOURCE-PIPELINE-PLAN.md §Phase 1.
    bool prev_nmi_generate_n_ = true;

    /// NR 0x08 bits 5..0 mirror for read-back composition per VHDL
    /// zxnext.vhd:5906. Bit 7 is write-strobe-only (derived from paging
    /// lock on read); bit 6 lives on Mmu as contention_disabled_. Bits
    /// 5..0 are stored in VHDL as individual signals (stereo, internal
    /// speaker, DAC en, port_ff_rd en, turbosound en, issue2 keyboard);
    /// we cache them collectively here so NR 0x08 reads return the last
    /// written value for signals not yet hooked to live subsystem state.
    uint8_t nr_08_stored_low_ = 0;

    // --- Line interrupt state (NextREG 0x22/0x23) ---
    // G71 (2026-04-28): line-int enable + 9-bit target are owned by
    // video_timing_ (see set_line_interrupt_enable / _target). Only the
    // ULA-int disable flag (NR 0x22 bit 2) lives here — it is a separate
    // gate from the line-int path and predates VideoTiming.
    bool     ula_int_disabled_   = false;  ///< NextREG 0x22 bit 2

    // --- port_ff_reg storage (VHDL zxnext.vhd:3610-3635) ---
    //
    // The Next collapses Timex screen-mode (bits 5:0) and ULA-int-disable
    // (bit 6) into a single 8-bit `port_ff_reg`. Four writers share it:
    //   * port 0xFF write       -> entire byte (port_ff_wr branch :3616)
    //   * NR 0x69 b5:0          -> bits 5:0 (:3618)
    //   * NR 0x22 b2            -> bit 6 (:3620)
    //   * NR 0xC4 b0 (inverted) -> bit 6 = NOT nr_wr_dat(0) (:3622)
    //
    // Public storage so the compositor / interrupt-disable path can read
    // a single source of truth, and so unit/integration tests can pin
    // the fan-out (gap doc G108).
    uint8_t  port_ff_reg_        = 0;      ///< VHDL zxnext.vhd:3610-3635

    // --- Clip window rotating write indices (NextREG 0x18/0x19/0x1A/0x1B) ---
    // Each clip register cycles through X1,X2,Y1,Y2 on successive writes.
    uint8_t clip_l2_idx_   = 0;   ///< Layer 2 clip write index (0-3)
    uint8_t clip_spr_idx_  = 0;   ///< Sprite clip write index (0-3)
    uint8_t clip_ula_idx_  = 0;   ///< ULA/LoRes clip write index (0-3)
    uint8_t clip_tm_idx_   = 0;   ///< Tilemap clip write index (0-3)

    /// G56: NR 0x10 coreid (5-bit), VHDL zxnext.vhd:1133 default "00001".
    /// Updated only when nr_03_config_mode = 1 (zxnext.vhd:5682). Composed
    /// into NR 0x10 read at bits 6:2 (zxnext.vhd:5924).
    uint8_t nr_10_coreid_ = 0x01;

    // --- IM2 hardware mode state (NextREG 0xC0–0xCF) ---
    //
    // Phase 2 Wave 2 (Agent E): authoritative state now lives inside
    // Im2Controller. These shadow fields are retained (a) for save/load
    // wire-format compatibility and (b) because Agent G's ULA/line scheduler
    // still writes im2_int_status_ pending migration.
    bool     im2_hw_mode_        = false;  ///< Legacy shadow of NR 0xC0 bit 0 (deprecated; see Im2Controller::is_im2_mode)
    uint8_t  im2_vector_base_    = 0;      ///< Legacy shadow of NR 0xC0 bits 7:5 (deprecated; see Im2Controller::vector_base)
    uint8_t  im2_int_enable_[3]  = {0x81, 0, 0};  ///< Legacy shadow of 0xC4-0xC6 (deprecated; see Im2Controller::set_int_en_c4/c5/c6)
    uint8_t  im2_int_status_[3]  = {};     ///< Legacy shadow of 0xC8-0xCA (deprecated; see Im2Controller::int_status_mask_c8/c9/ca)

    // NR 0xC4 bit 7 (expansion-bus INT enable) — non-IM2 state, not held by
    // Im2Controller. VHDL: nr_c4_int_en_0_expbus defaults '1' at reset
    // (zxnext.vhd:5096). Stored here for NR 0xC4 readback.
    bool     im2_c4_expbus_      = true;

    // NR 0xC6 raw readback shadow — VHDL read (zxnext.vhd:6245):
    //   port_253b_dat <= '0' & nr_c6_int_en_2_654 & '0' & nr_c6_int_en_2_210
    // Bits 7 and 3 always read as 0. We store the 6 meaningful bits verbatim
    // so the read handler returns exactly what was written.
    uint8_t  nr_c6_uart_int_en_  = 0;

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
    // Now owned by `iomode_` (member above). Phase 2 Wave 2 Agent E
    // migrated the standalone `joy_iomode_pin7_` shadow into the IoMode
    // class — read it via iomode_.pin7() and update it via the NR 0x0B
    // write handler + the CTC ch3 ZC/TO callback (see emulator.cpp init).

    // --- Phase 1 scaffold: test-only NMI/hotkey injection fields ---
    // Set by inject_hotkey_m1/drive/sw_nmi_mf/sw_nmi_divmmc. Phase 2
    // Agent I (2026-04-21) ANDs them with NR 0x06 bits 3/4 in the
    // nmi_assert_mf() / nmi_assert_divmmc() accessors above. Production
    // code never touches these — the real hotkey source is the host SDL
    // event loop and the real software-NMI source is NR 0x02 strobe
    // (both still unwired, see memory/project_nmi_fragmented_status.md).
    bool     test_hotkey_m1_           = false;
    bool     test_hotkey_drive_        = false;
    bool     test_sw_nmi_mf_           = false;
    bool     test_sw_nmi_dmmc_         = false;

    // --- Phase 2 Agent I: NR 0x06 bits 3/4 storage ---
    // VHDL zxnext.vhd:5165-5166 — written by the NR 0x06 write handler
    //   nr_06_button_drive_nmi_en <= nr_wr_dat(4);
    //   nr_06_button_m1_nmi_en    <= nr_wr_dat(3);
    // Reset value is '0' (VHDL signal default; the soft/hard reset block
    // does not re-clear them but they start at '0' at power-on, matching
    // the fact that NextZXOS firmware explicitly enables the buttons).
    bool     nr_06_button_m1_nmi_en_    = false;  ///< VHDL nr_06_button_m1_nmi_en
    bool     nr_06_button_drive_nmi_en_ = false;  ///< VHDL nr_06_button_drive_nmi_en

    // NR 0x06 bit 6 — `nr_06_internal_speaker_beep` (VHDL zxnext.vhd:5163).
    // Combined with NR 0x08 bit 4 (`nr_08_internal_speaker_en`) it forms
    // the `beep_spkr_excl` wire feeding `audio_mixer.exc_i`
    // (VHDL zxnext.vhd:6504). Power-on default '0' (VHDL signal default;
    // reset block does not re-clear it explicitly but it starts at '0').
    bool     nr_06_internal_speaker_beep_ = false;

    // NR 0x06 bit 2 — `nr_06_ps2_mode` (VHDL zxnext.vhd:1111, :5167-5169).
    // Power-on default '0'.  Authoritative storage so that the NR 0x06
    // read handler (G56 cluster A — VHDL zxnext.vhd:5900 read formula)
    // can compose bit 2 without going through the raw NextReg shadow
    // store (which would echo every write blindly, including writes
    // outside config_mode that VHDL gates out at line 5167).
    bool     nr_06_ps2_mode_ = false;

    // NR 0x81 — Expansion bus control (VHDL zxnext.vhd:1222-1227).
    // Wave C (TASK-NMI-SOURCE-PIPELINE-PLAN) stores the raw byte for
    // VHDL-faithful read-back; bit 5 (`expbus_nmi_debounce_disable`) is
    // forwarded live to NmiSource. Other bits are inert until expansion
    // bus emulation arrives. Power-on default '0' (VHDL signal defaults).
    uint8_t  nr_81_ = 0;  ///< VHDL expbus_ula_override / nmi_debounce_disable / clken / fdc / speed

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
    // G65 — deferred CPU NR-write queue (see public method docs).
    // -----------------------------------------------------------------------
    bool                                     defer_cpu_nr_writes_ = false;
    std::vector<std::pair<uint8_t, uint8_t>> pending_cpu_nr_writes_;

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    /// Schedule a full frame's worth of SCANLINE events into the scheduler.
    void schedule_frame_events();

    /// Called by the SCANLINE event handler for scanline `line`.
    void on_scanline(int line);

    /// Called by the VSYNC event handler.
    void on_vsync();

    /// Step the Copper for `master_cycles` 28-MHz cycles ending at the
    /// current `clock_.get()`. VHDL `device/copper.vhd:54-119` runs at
    /// the i_CLK_28 rising edge — at most one MOVE / WAIT-compare per
    /// 28 MHz cycle. Pre-G117 jnext stepped the Copper exactly once per
    /// Z80 instruction, collapsing dense Copper bursts (e.g. tilemap
    /// effects with 32 MOVEs/scanline) into 1-2 effective writes per
    /// scanline. This helper restores per-cycle granularity.
    void tick_copper_for_master_cycles(uint64_t master_cycles);

    /// G65 — deferred CPU NR-write commit. VHDL `zxnext.vhd:4769-4777`
    /// edge-detects CPU write requests (cpu_requester → cpu_requester_d
    /// → cpu_req latch), so CPU NR writes reach the bus 1+ cycles after
    /// the OUT instruction asserts the request. When a Copper MOVE rises
    /// on the same 28 MHz cycle as the CPU's request edge, the priority
    /// mux selects Copper, then CPU's held request commits the next
    /// cycle — final NR value is CPU's, not Copper's.
    ///
    /// jnext models this as: while inside the per-instruction tick
    /// (set_defer_cpu_nr_writes(true) below), CPU NR writes via port
    /// 0x253B enqueue here instead of committing through NextReg.
    /// After tick_copper_for_master_cycles runs, the queue is drained
    /// so CPU writes apply LAST in the instruction window. If both
    /// Copper and CPU touched the same NR, the CPU value wins —
    /// matching VHDL's "Copper writes first, CPU held over by one
    /// cycle, both writes happen, CPU value persists" semantic.
    void enqueue_cpu_nr_write(uint8_t reg, uint8_t val);
    void flush_pending_cpu_nr_writes();
    void set_defer_cpu_nr_writes(bool v) { defer_cpu_nr_writes_ = v; }
    bool defer_cpu_nr_writes() const { return defer_cpu_nr_writes_; }

    /// G163 — re-evaluate line-interrupt schedule on every
    /// NR 0x22 / NR 0x23 / NR 0xC4 (bit 1 mirror) write and at frame
    /// start. VHDL `zxula_timing.vhd:577` fires every cycle when
    /// `(hc_ula==255 AND cvc==int_line_num)` — fully dynamic.
    /// Mid-frame retargeting (e.g. parallax.nex's chained line-IRQ
    /// chain rewriting NR 0x23 to schedule the next firing line) was
    /// silently swallowed pre-fix because the line-int event was
    /// scheduled ONCE per frame in `run_frame()` and the
    /// NR 0x22 / NR 0x23 / NR 0xC4 write handlers only updated
    /// `VideoTiming` state.
    ///
    /// NR 0xC4 bit 1 is a hardware mirror of NR 0x22 bit 1: both
    /// write the same `nr_22_line_interrupt_en` flip-flop
    /// (zxnext.vhd:5607-5610) which feeds `i_inten_line` of the
    /// line-int comparator (:6752), so the same reschedule
    /// requirement applies to every NR 0xC4 write.
    ///
    /// Shape: bumps `line_int_schedule_gen_` and schedules a fresh
    /// `EventType::CPU_INT` for the new target's master-cycle offset
    /// within the current frame; if that offset has already passed,
    /// rolls forward one frame (handles parallax's 8-bit `ADD 0x10`
    /// wrap from line 244 → 4). The lambda captures the gen by-value
    /// and no-ops at fire time if a later (re)schedule has bumped the
    /// counter — strict superset of "compare target at fire time"
    /// because it correctly handles same-target rewrites too.
    void reschedule_line_interrupt();
};
