#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "audio/audio_mute.h"
#include "core/clock.h"
#include "core/emulator_config.h"
#include "core/extended_nex_host.h"
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
#include "peripheral/multiface.h"
#include "peripheral/nmi_source.h"
#include "peripheral/sd_card.h"
#include "input/keyboard.h"
#include "input/joystick.h"
#include "input/joy_source.h"
#include "input/mouse.h"
#include "input/md6_connector_x2.h"
#include "input/membrane_stick.h"
#include "input/iomode.h"
#include "input/emu_fnkeys.h"
#include "input/phantom_typist.h"
#include "debug/trace.h"
#include "debug/call_stack.h"
#include "audio/beeper.h"
#include "audio/turbosound.h"
#include "audio/dac.h"
#include "audio/i2s.h"
#include "audio/mixer.h"
#include "debug/debug_state.h"
#include "core/tap_loader.h"
#include "core/tap_saver.h"
#include "core/tzx_loader.h"
#include "core/sna_loader.h"
#include "core/szx_loader.h"
#include "core/z80_loader.h"
#include "core/wav_loader.h"
#include "core/video_recorder.h"
#include "core/rzx_player.h"
#include "core/rzx_recorder.h"
#include "debug/rewind_buffer.h"
#include "profiler/profiler.h"

// GH #25 — the emulated ESP-01 is held by pointer and forward-declared on
// purpose. `emulator.h` is included by most of the tree; the ESP module's
// headers pull in <thread>, <condition_variable> and the socket seam, and none
// of that belongs in every translation unit for a peripheral that is off by
// default. `~Emulator()` is defined out of line, which is what lets these stay
// incomplete here.
namespace esp {
class EspTransport;
class EspListener;
class ThreadedEsp;
}  // namespace esp
class EspUartAdapter;
class EspConnectionLog;

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
    struct EsxdosStubState {
        std::string filename;
        std::vector<uint8_t> file;
    };

    Emulator();
    ~Emulator();

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

    /// In-place hard reinit: reinitialize all subsystems, clear RAM, reload ROM
    /// (calls init(config_)). NOTE (Task 70): this is NOT the machine's
    /// power-on "reset button" behaviour. A booted Next reset in place does not
    /// re-engage the FPGA boot ROM (config_mode is preserved), so it does NOT
    /// re-boot NextZXOS — it would fall through to 48K BASIC. The physical reset
    /// button / F1 / a program's NR 0x02 bit 1 are modelled as a power-on COLD
    /// BOOT the host frontend performs by RECONSTRUCTING the emulator and
    /// re-running init() (platform/emulator_boot.h::emulator_cold_boot). This
    /// method survives only as the reinit used by the file loaders
    /// (load_nex/load_sna/...) on a fresh-at-startup machine and by unit-test
    /// setup. Do not call it expecting a NextZXOS re-boot.
    void reset();

    /// Perform a soft reset (tbblue RESET_SOFT / NR 0x02 bit 0).
    /// Resets flip-flops (CPU, MMU, peripherals, NextReg) but preserves
    /// RAM contents (including the Next ROM-in-SRAM window), ROM buffer,
    /// boot-ROM overlay state, and other non-FF state. Matches VHDL
    /// zxnext_top_issue5.vhd:836-880 (soft reset domain subset).
    void soft_reset();

    /// Task 70 — a HARD reset (physical reset button / F1 / NR 0x02 bit 1) is
    /// modelled by the host frontend as a power-on cold boot: it reconstructs
    /// the emulator and re-runs init() (the proven startup path). That cannot
    /// happen from inside run_frame() (a program's own NR 0x02 write), so the
    /// emulator only RECORDS the request here; the frontend polls
    /// take_hard_reset_request() after each run_frame() and performs the cold
    /// boot. Soft reset (bit 0) is unaffected — the NextZXOS boot uses it.
    void request_hard_reset() { hard_reset_requested_ = true; }
    bool take_hard_reset_request() {
        bool r = hard_reset_requested_;
        hard_reset_requested_ = false;
        return r;
    }

    /// Consume a sibling-NEX request raised by the esxDOS callback.
    std::string take_nex_load_request();

    /// Preserve the stub's in-memory file across a frontend cold boot.
    EsxdosStubState esxdos_stub_state() const;
    void restore_esxdos_stub_state(EsxdosStubState state);

    /// Load a raw binary file into RAM at `org` and set PC to `pc`.
    /// Called after init() when --inject is used.  Returns true on success.
    bool inject_binary(const std::string& path, uint16_t org, uint16_t pc);

    /// Load a NEX file into the emulator.  Returns true on success.
    bool load_nex(const std::string& path);

    /// G156 — NEX `loading_delay`/`start_delay` (tbblue nexload.asm:541,
    /// 575-577). NexLoader::apply() computes the total frame count via
    /// NexLoader::boot_hold_frames() and calls this. run_frame() then
    /// holds the CPU idle (no instruction fetch/execute — matches
    /// nexload's own DI raster-wait) for that many frames before the
    /// already-injected PC starts executing, while scanline rendering /
    /// audio / scheduler continue ticking normally so VRAM's post-load
    /// state (including any loading-bar marks) is composited every held
    /// frame exactly as it would be for any other frame. NOTE: on a bare
    /// `--load file.nex` the loading bar is written to VRAM correctly but
    /// is NOT observable on screen, because Layer 2 starts disabled and
    /// neither nexload.asm nor NexLoader::apply() enables it — see the
    /// long comment at the call site in run_frame() for the full caveat
    /// (including the interrupt-servicing gap for preserve_regs=1 chain
    /// loads).
    void set_boot_hold_frames(uint32_t frames) { boot_hold_frames_remaining_ = frames; }
    uint32_t boot_hold_frames_remaining() const { return boot_hold_frames_remaining_; }

    /// GH #164 — park the CPU: run_frame() keeps rendering, audio and the
    /// scheduler ticking but fetches and executes no instruction, so the
    /// machine holds whatever state it was left in, indefinitely.
    ///
    /// Set by NexLoader::apply() for a NEX whose header PC is 0 — the
    /// format's "load only, do not execute" encoding (nexload.asm:581
    /// `ld a,h:or l:jr z,.returnToBasic`, nexload2.asm:410-412). On real
    /// hardware that path RETURNS to the OS that invoked the loader, with
    /// the load's banks/paging/NextREGs/palette/border intact. jnext's
    /// `--load` has no OS to return to — it resets and applies the NEX
    /// before a single instruction runs — so the faithful equivalent of
    /// "run nothing the NEX did not ask for, keep the state the load
    /// produced" is to run nothing at all. Parking is jnext's stand-in for
    /// the oracle's `ret`, NOT a claim that real hardware halts: there the
    /// OS keeps running its idle loop (interrupts, FLASH, keyboard scan),
    /// which a parked jnext does not.
    ///
    /// Cleared by init(), hence by both reset() and soft_reset(), and by
    /// resume_from_park() below.
    void set_cpu_parked(bool parked) { cpu_parked_ = parked; }
    bool cpu_parked() const { return cpu_parked_; }

    /// GH #164 — clear a park because the caller is an action that
    /// unambiguously asks the machine to RUN. Logs when it actually
    /// un-parks; a no-op (and silent) otherwise, so callers may call it
    /// unconditionally.
    ///
    /// A park stops instruction fetch indefinitely, so any entry point that
    /// hands the machine content it is expected to execute would otherwise
    /// succeed, report success, and then never do anything — a silent,
    /// permanent hang with no error and no indication. That is strictly
    /// worse than the wrong-but-visible cold boot this whole change
    /// replaces, so those entry points resume instead of refusing:
    /// a user who just opened a tape or injected a binary has already said
    /// what they want, and being told "no" would be a puzzle.
    ///
    /// There are SIX call sites, listed one per line and numbered on
    /// purpose: the first version of this comment grouped the three tape
    /// loaders onto one bullet, and both the author and the reviewer then
    /// read the list as five. The suite shipped with five rows for six
    /// sites, and nothing failed — `load_rzx` had no coverage anywhere.
    /// Each site is pinned by exactly one row: NEXPC0-12, -13, -14, -15,
    /// -16 and -18 (NEXPC0-17 is the unrelated G156 boot-hold row). If you
    /// add a seventh site, add its row and fix this list.
    ///
    ///   1. load_tap  — a tape can only load if the guest runs. BOTH start
    ///      mechanisms (the ROM LD-BYTES trap and the phantom typist's
    ///      keyboard-scan trigger) are only tested while the CPU is
    ///      fetching, so neither can ever fire on a parked machine.
    ///   2. load_tzx  — same two mechanisms.
    ///   3. load_wav  — always real-time, so it depends on the guest
    ///      running even more directly than the fast-load formats.
    ///   4. inject_binary — names an entry point and sets PC to it.
    ///   5. load_rzx with no embedded snapshot — playback drives the
    ///      CURRENT machine (the snapshot path resets, which un-parks by
    ///      itself, and needs no call).
    ///   6. execute_single_instruction — the debugger's Step is the most
    ///      explicit possible request to advance the CPU, and leaving it
    ///      dead makes the debugger look broken rather than the machine
    ///      look parked (there is no "Parked" affordance in the UI yet).
    ///      Debugger RUN deliberately does NOT resume: unlike Step it
    ///      promises nothing about advancing, so staying parked is correct.
    ///
    /// Loads that reset first (load_nex/sna/szx/z80, RZX-with-snapshot) need
    /// no call: reset() re-runs init(), which clears the flag.
    void resume_from_park(const char* reason);

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

    /// Load a `.z80` snapshot file (v1/v2/v3). Returns true on success.
    bool load_z80(const std::string& path);

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

    /// Access the tape saver (G33 Phase 1 — trap-based SAVE→TAP).
    TapSaver& tap_saver() { return tap_saver_; }
    const TapSaver& tap_saver() const { return tap_saver_; }

    /// Access the TZX loader.
    TzxLoader& tzx_tape() { return tzx_tape_; }
    const TzxLoader& tzx_tape() const { return tzx_tape_; }

    /// Access the WAV loader.
    WavLoader& wav_tape() { return wav_tape_; }
    const WavLoader& wav_tape() const { return wav_tape_; }

    /// Monotonic CPU T-state clock for real-time tape playback (G36/G37).
    ///
    /// The FUSE `tstates` counter is FRAME-RELATIVE: begin_new_frame()
    /// resets it to 0 every frame so derive_hc_vc()/contention can gate on
    /// raster position. ZOT's TZX player and the WAV loader, however, model
    /// the tape as an ABSOLUTE timeline (`edge_clock += pulse`,
    /// `elapsed = now - start`). Feeding them the frame-relative counter
    /// froze all real-time TZX/WAV playback the moment an edge crossed a
    /// frame boundary: `cpu_clocks >= edge_clock` could never become true
    /// again (regression introduced by the f3665f25 frame-relative reset,
    /// 2026-04-13). This accessor restores a monotonic clock that still
    /// advances MID-instruction (the property the original ZOT clock fix
    /// needed for tight IN A,(0xFE) loops): frame base + live FUSE counter.
    uint64_t monotonic_tstates() const;

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
    /// Task 19 (instant TAP load): the FUSE-style phantom typist that
    /// waits for the ROM's first full keyboard scan, then queues a
    /// per-machine LOAD"" sequence. See src/input/phantom_typist.h.
    PhantomTypist&  phantom_typist() { return phantom_typist_; }

    /// Task 19 fastload follow-up — true when the emulator should
    /// run uncapped (no frame-pacing sleep, audio paused) to make
    /// TAP loading visually "instant" like FUSE. Mirrors FUSE's
    /// `timer_fastloading_active()` (timer/timer.c:178): asserted
    /// while EITHER the phantom typist is armed/firing OR a
    /// fast-load tape (TAP or TZX) is loaded and not yet at end —
    /// the load itself races through the ROM trap so a brief
    /// `tape_is_playing()` window also benefits from uncapped pacing.
    /// The platform layer (SdlApp::run, QtApp::on_frame_tick) reads
    /// this and skips its frame-rate delay while true; once both
    /// signals clear the platform re-engages normal 50 Hz pacing
    /// (and re-resumes audio output).
    bool fastload_active() const {
        // Phantom-typist armed/firing OR fast-load tape with data
        // still to deliver. The tape side uses `is_loaded() &&
        // fast_load() && !at_end()` to mirror FUSE's signal — a
        // fast-load TAP/TZX trap fires at ROM-load address and
        // empties at most a few KB per second of wall-clock time
        // when frame pacing is uncapped, so this window is brief
        // (sub-second in practice).
        if (phantom_typist_.is_active()) return true;
        if (tape_.is_loaded()     && tape_.fast_load()     && !tape_.at_end())     return true;
        // TZX path note: load_tzx() does NOT arm the phantom typist (Task
        // 19 scope is .tap only). For TZX the tape-side condition is the
        // sole signal, true from the moment load_tzx() runs (covers the
        // legacy ~26-frame auto-type window too).
        if (tzx_tape_.is_loaded() && tzx_tape_.fast_load() && !tzx_tape_.at_end()) return true;
        return false;
    }
    Joystick&       joystick()       { return joystick_; }

    // --- Task 79: per-connector host input source ---------------------------
    // Which host source drives each Next joystick connector (0 = Joy 1,
    // 1 = Joy 2). See input/joy_source.h. The Emulator is the single
    // coordinator: it enforces the "at most one CursorKeys connector" rule,
    // keeps the Keyboard's cursor-key target in sync, and notifies the host
    // frontend (which owns the JoystickDispatcher) via
    // on_joystick_source_changed. No SDL/Qt type crosses this boundary.
    JoySource joystick_source(int connector) const {
        if (connector < 0 || connector > 1) return JoySource::Sdl;
        return joy_source_[connector];
    }
    /// Set connector `connector` (0/1) to `src`. If `src` is CursorKeys and
    /// the other connector was also CursorKeys, the other reverts to Sdl
    /// (mutually exclusive). Updates the Keyboard target and fires
    /// on_joystick_source_changed for every connector whose source changed.
    void set_joystick_source(int connector, JoySource src);
    /// Re-push the current sources to the Keyboard target and the frontend
    /// callback unconditionally. The frontend calls this once, after it has
    /// wired the dispatcher + on_joystick_source_changed, so CLI/config
    /// sources set before that wiring take effect.
    void refresh_joystick_sources();

    /// Set by the host frontend that owns the JoystickDispatcher; the
    /// Emulator calls it with (connector, new-source) whenever a source
    /// changes so the dispatcher's per-slot gate stays in sync. Empty by
    /// default (headless / SDL builds without a dispatcher). No SDL/Qt type
    /// crosses this boundary.
    std::function<void(int connector, JoySource src)> on_joystick_source_changed;

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
    SdCardDevice& sd_card()   { return sd_card_; }
    I2cController& i2c()     { return i2c_; }
    Uart&         uart()      { return uart_; }

    // ── Emulated ESP-01 (GH #25) ──────────────────────────────────────────
    /// True when `EmulatorConfig::esp_enabled` built one. Everything below is
    /// null / empty when it did not, which is the default.
    bool esp_enabled() const { return esp_device_ != nullptr; }
    /// User-visible connection events (made / refused / closed / faulted).
    /// Null when the ESP is disabled — so "no cell in the status bar" and "no
    /// ESP" are the same condition, and a user cannot have the feature on
    /// without seeing that they do. Thread-safe; produced on the ESP worker.
    const EspConnectionLog* esp_events() const { return esp_events_.get(); }
    /// The hostname allowlist actually in force, as the user wrote it.
    const std::vector<std::string>& esp_allowed_hosts() const {
        return config_.esp_allowed_hosts;
    }
    /// GH #246 — whether the module is currently on its network. True whenever
    /// the ESP is enabled and no scheduled outage is in force; false only
    /// between `esp_disassociate_frame` and `esp_associate_frame`. Reports
    /// false when the ESP is disabled, since there is no module to be
    /// associated. Reads the ENGINE's own state, not the schedule's intent.
    bool esp_associated() const;
    /// GH #246 — the station address the module reports to the guest
    /// (`--esp-ip-address`, else the module's synthetic default). Empty when
    /// the ESP is disabled. Reads the ENGINE, not the config.
    std::string esp_station_ip() const;
    DivMmc&       divmmc()    { return divmmc_; }
    Multiface&    multiface() { return multiface_; }
    NmiSource&    nmi_source(){ return nmi_source_; }
    Im2Controller& im2()       { return im2_; }
    Beeper&       beeper()    { return beeper_; }
    TurboSound&   turbosound(){ return turbosound_; }
    Dac&          dac()       { return dac_; }
    I2s&          i2s()       { return i2s_; }
    Mixer&        mixer()     { return mixer_; }

    /// Debugger-only per-source audio mute (AudioMute bits, audio/audio_mute.h).
    /// Single owner of the 5-bit mask; fans the AY bits out to TurboSound (which
    /// sums the 3 PSGs) and the DAC/Beeper bits to the Mixer. No hardware
    /// analogue and no CPU-visible effect: it gates output stages only, never
    /// register files or timing. Not machine state — a rewind or a reset does
    /// not change it.
    void set_audio_mute_mask(uint8_t mask) {
        audio_mute_mask_ = mask & AudioMute::ALL;
        turbosound_.set_chip_mute_mask(audio_mute_mask_);
        mixer_.set_mute_mask(audio_mute_mask_);
    }
    uint8_t audio_mute_mask() const { return audio_mute_mask_; }

    TraceLog&     trace_log() { return trace_log_; }
    CallStack&    call_stack(){ return call_stack_; }
    Renderer&     renderer()  { return renderer_; }
    Profiler&     profiler()  { return profiler_; }
    const Profiler& profiler() const { return profiler_; }

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

    /// Execute a single CPU instruction slot with all subsystem ticking, and
    /// nothing around it. Returns T-states consumed.
    ///
    /// This is the RAW primitive. It is what a test uses to advance a machine
    /// whose frames the test drives itself; it deliberately does not touch
    /// frame state. **The debugger's Step is debugger_step()** — see there.
    int execute_single_instruction();

    /// GH #207 — the debugger's Step: one instruction slot PLUS the per-frame
    /// bookkeeping run_frame() performs, because while the debugger holds the
    /// machine the frontends stop calling run_frame() and this becomes its only
    /// driver. A Step issued at a HALT additionally runs the halt out (bounded
    /// to two frames): the CPU leaves a halt only on an accepted interrupt or
    /// NMI (t80n.vhd:1727), so stepping its internal NOP slots one at a time
    /// shows the user nothing and never terminates in practice.
    ///
    /// Returns T-states consumed — the whole halt, when it ran one out.
    int debugger_step();

    const EmulatorConfig& config() const { return config_; }
    const MachineTiming& timing() const { return timing_; }

    /// Real-time duration of one emulated frame, in milliseconds. Follows the
    /// current video refresh (50 Hz → ~20.26 ms, 60 Hz → ~17.20 ms). Frontends
    /// pace their frame timer from this so a 60 Hz demo (NR 0x05 bit 2) runs at
    /// 60 fps instead of a hardcoded 50 Hz (GitHub issue #9).
    double frame_period_ms() const {
        return static_cast<double>(timing_.master_cycles_per_frame) * 1000.0 /
               static_cast<double>(MASTER_CLOCK_HZ);
    }

    // -----------------------------------------------------------------------
    // State serialisation (used by RewindBuffer)
    // -----------------------------------------------------------------------

    /// Per-subsystem desync sentinel (Task 60b): save_state writes
    /// `kStateSentinelMagic ^ ordinal` (u32) after every subsystem block;
    /// load_state verifies each one and fails loudly (error log naming the
    /// subsystem + returns false) on mismatch, instead of silently feeding
    /// desynced bytes into every subsystem downstream of an asymmetric
    /// save/load edit. The snapshot byte stream is in-process only (the
    /// rewind ring is its sole consumer); it is never written to disk.
    static constexpr uint32_t kStateSentinelMagic = 0x4A4E5354; // "JNST"

    void save_state(class StateWriter& w) const;

    /// Returns false (and logs an error naming the subsystem) on sentinel
    /// mismatch or out-of-bounds read; the restore is aborted at that point
    /// and the machine state is NOT trustworthy.
    bool load_state(class StateReader& r);

    /// Name of the subsystem whose sentinel failed in the last load_state
    /// (empty if the last load succeeded). Non-empty means the machine is
    /// currently in a torn, partially-restored state after a failed
    /// rewind/step-back (Task 60b) and is NOT safe to resume — the GUI
    /// (Task 60e) uses this to warn before letting the user run again.
    /// The flag is cleared by a hard/soft reset (the recovery path) and is
    /// NOT cleared by merely acknowledging it: acknowledging does not heal
    /// the desync, so the breadcrumb survives until the machine is actually
    /// re-established clean.
    const std::string& last_state_error() const { return last_state_error_; }

    /// Monotonic counter bumped every time a *fresh* corruption is latched
    /// in load_state (Task 60e). Lets the GUI distinguish an incident the
    /// user already acknowledged from a brand-new one that must be
    /// re-confirmed, without clearing last_state_error(). Never decreases.
    uint64_t state_error_generation() const { return state_error_generation_; }

    /// Task 60c — fired at the end of every SUCCESSFUL load_state() (which is
    /// the single funnel for all state restores: step_back, rewind_to_frame,
    /// rewind_to_cycle via RewindBuffer::restore_nearest, and any direct
    /// snapshot load). The host input layer registers a callback here to
    /// re-seed its host-side dispatchers (JoystickDispatcher / MouseDispatcher)
    /// from the just-restored canonical Joystick / KempstonMouse state — those
    /// dispatchers hold their OWN shadow of the connector/wheel/button vector,
    /// so without the resync the next live host event would stomp the restore.
    /// Empty by default (headless / unit tests / SDL builds without a GUI);
    /// invoked only when set. No SDL/Qt type crosses this boundary.
    std::function<void()> on_input_state_restored;

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

    /// Task 27 C6 — frontend render hint. When the frontend knows nobody will
    /// consume the framebuffer produced by the NEXT run_frame() (the Qt GUI
    /// at --speed 400 emulates 200 frames/s while the display presents
    /// ~50-60; both GUI frontends' non-final frames of a multi-frame tick are
    /// superseded before any paint — see platform/render_policy.h), it may
    /// clear this to skip the end-of-frame compositing pass.
    /// It is a HINT, not a command: run_frame() still renders regardless when
    /// a consumer the frontend cannot see needs the frame — video recording
    /// (capture_frame() reads the framebuffer immediately after render) or an
    /// active debugger (panels/pause display read it). Default TRUE: headless
    /// (including --benchmark, which must measure the real render workload)
    /// never touches this and keeps rendering every frame.
    bool render_enabled() const { return render_enabled_; }
    void set_render_enabled(bool v) { render_enabled_ = v; }

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

    /// Port 0xFF read mux (VHDL zxnext.vhd:2813) — Timex register when
    /// NR 0x08 b2 + NR 0x82 b0 are set, else the ULA floating bus in
    /// 48K/128K timing (byte being fetched from VRAM at this T-state),
    /// else 0xFF (border / +3 / Pentagon / Next timing). Registered as
    /// the READ handler of the LSB-only 0x00FF port decode (zxnext.vhd:
    /// 2583) — GH #109: NOT a default for unmatched ports; those return
    /// 0xFF per zxnext.vhd:1877.
    uint8_t floating_bus_read() const;

    /// G46(b) #102 session 3 probe (env-gated, zero cost when unset): log
    /// every port read served by PortDispatch's DEFAULT (no handler
    /// matched) path — PC, port, returned value, frame. Frame number is
    /// supplied by the caller once per frame via set_g46b_port_trace_frame()
    /// since Emulator has no frame counter of its own.
    void set_g46b_port_trace(std::FILE* f) { g46b_port_trace_file_ = f; }
    void set_g46b_port_trace_frame(long frame) { g46b_port_trace_frame_ = frame; }

    /// VHDL-faithful `ula_floating_bus` active-arm helper (zxula.vhd:573).
    /// Computes the VRAM byte the ULA is fetching at the current raster
    /// position when `border_active_ula='0' AND floating_bus_en='1'`.
    /// Returns true and writes the byte into @p out_byte when the active
    /// arm fires; returns false otherwise (caller picks the fallback —
    /// `i_p3_floating_bus` for +3, X"FF" for 48K/128K). Verify9-memory
    /// class-(c) → class-(a) fix (+3 floating-bus active-display path):
    /// previously the +3 0x0FFD handler unconditionally returned the
    /// contended-CPU latch (border arm), missing the active-display
    /// VRAM byte path. This helper exposes the active arm so both port
    /// 0xFF (48K/128K) and port 0x0FFD (+3) can share the decode.
    bool ula_floating_bus_active_arm(uint8_t& out_byte) const;

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

    /// V20R-CPU-NIT-01 — Test accessors for the Pass-20 falling-edge
    /// shadow used by the pulse-mode CPU /INT poll (emulator.cpp line
    /// ~5791). Saved/loaded by `Emulator::{save,load}_state` so a
    /// snapshot taken mid-pulse round-trips faithfully. Used by the
    /// V20R-CPU-NIT-01-PREV-PULSE-PERSIST regression to assert the
    /// shadow survives save/load. The setter is test-only: it is the
    /// minimum interface required to drive `prev_pulse_int_n_` to a
    /// non-default value without depending on the precise tick window
    /// where the V20 poll's falling-edge happens to fire mid-frame.
    bool prev_pulse_int_n_for_test() const { return prev_pulse_int_n_; }
    void set_prev_pulse_int_n_for_test(bool v) { prev_pulse_int_n_ = v; }

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

    /// V16-NMP-02 (Pass-16 verify-audit fix): effective `internal_port_enable`
    /// byte for one of the NR 0x82..0x85 enable registers, AND-masked with
    /// the corresponding NR 0x86..0x89 bus-port enable when
    /// `expbus_eff_en = 1` (NR 0x80 bit 7). Mirrors the VHDL formula at
    /// `zxnext.vhd:2392-2393`:
    ///   internal_port_enable <= (nr_85 & nr_84 & nr_83 & nr_82) when
    ///                           expbus_eff_en='0' else
    ///                           ((nr_89 AND nr_85) & (nr_88 AND nr_84) &
    ///                            (nr_87 AND nr_83) & (nr_86 AND nr_82));
    /// `reg` must be one of 0x82, 0x83, 0x84, 0x85; any other value
    /// returns the raw cached byte unchanged. Pre-fix (and historically),
    /// every port-decode gate consulted `nextreg_.cached(0x82..0x85)`
    /// directly, ignoring the bus-port enable AND-mask.
    uint8_t effective_internal_port_enable(uint8_t reg) const;

    /// V16-NMP-02: re-push the persistent shadow of every effective
    /// port-decode gate that downstream subsystems hold a copy of
    /// (Contention's port_7ffd_io_en + port_ulap_io_en, DivMmc's
    /// port_io_enable, Multiface's enable). Called from NR 0x80,
    /// NR 0x82-0x85, and NR 0x86-0x89 write handlers so any change to
    /// the AND-formula's inputs immediately re-propagates.
    ///
    /// `override_reg` / `override_val`: when an NR 0x82-0x85 / 0x86-0x89
    /// write handler runs, the byte being written is NOT yet in
    /// `nextreg_.cached(reg)` — NextReg::write writes the cache from
    /// the handler's return value AFTER the handler returns. To let
    /// the helper see the in-flight write, pass `(reg, val)`; the
    /// helper substitutes `val` for the cached byte at `reg`. Pass
    /// `override_reg = 0xFF` (any non-NR-byte) to use the cache verbatim.
    void propagate_effective_port_enables(uint8_t override_reg = 0xFF,
                                          uint8_t override_val = 0);

    /// V16-NMP-02: same as `effective_internal_port_enable(reg)` but with
    /// an in-flight override for the cache at `override_reg`. The
    /// override applies only when `reg == override_reg` (the
    /// internal-port byte being written) or `reg+4 == override_reg`
    /// (the bus-port byte being written).
    uint8_t effective_internal_port_enable(uint8_t reg,
                                           uint8_t override_reg,
                                           uint8_t override_val) const;

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
    // Task 79 — recompute the Keyboard cursor-key target from joy_source_:
    // the connector (0/1) whose source is CursorKeys, or -1 for none.
    void update_cursor_key_target();

    // Canonical 640×256 ARGB framebuffer (G104). Vertical 2× scaling for
    // square-pixel display (640×512) is applied at the GUI/screenshot layer
    // (Phase 7).
    static constexpr int FRAMEBUFFER_WIDTH  = 640;
    static constexpr int FRAMEBUFFER_HEIGHT = 256;
    static constexpr int FRAMEBUFFER_PIXELS = FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT;

    EmulatorConfig config_;
    // Host-side state used by --esxdos-stub and direct extended-NEX loading.
    std::string nex_load_request_;
    std::string active_nex_path_;
    std::string esxdos_stub_filename_;
    std::vector<uint8_t> esxdos_stub_file_;
    std::size_t esxdos_stub_file_pos_ = 0;
    uint8_t esxdos_stub_handle_ = 1;
    bool esxdos_stub_file_open_ = false;
    bool esxdos_stub_file_write_ = false;
    std::function<bool(uint8_t, Z80Registers&)> esxdos_bridge_handler_;
    ExtendedNexHost extended_nex_host_;
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

    // ── Emulated ESP-01 on UART 0 (GH #25 branch 4) ──────────────────────
    //
    // DECLARATION ORDER IS THE LIFETIME CONTRACT, and it is load-bearing three
    // times over. Members are destroyed in REVERSE declaration order, so:
    //
    //   esp_adapter_    dies FIRST  -> clears the ESP's ByteSink, which
    //                                  captured `this`, before the ESP can
    //                                  call it again.
    //   esp_device_     dies SECOND -> ~ThreadedEsp JOINS the worker, so no
    //                                  thread survives into anything below.
    //   esp_listener_   dies THIRD  -> the wrapper drove it too (GH #210), so
    //                                  it carries the transport's obligation:
    //                                  outlive the thing that polls it.
    //   esp_transport_  dies FOURTH -> the transport outlived the wrapper that
    //                                  was driving it, as esp_threaded.h
    //                                  requires.
    //   esp_events_     dies FIFTH  -> the transport wrote into it from the
    //                                  worker; it must outlive both.
    //
    // ...and ALL of them die before `uart_`, which is declared above, so the
    // RxSink capturing `&uart_` can never be called against a dead UART.
    //
    // WHY THE ESP LIVES IN `Emulator` AT ALL rather than in a frontend. A cold
    // boot is `emu.~Emulator(); new (&emu) Emulator();` — placement-new at the
    // SAME address (platform/emulator_boot.h). A worker thread that survived
    // that would service a core whose sink points into the NEWLY BOOTED
    // machine: silent corruption, not a crash, invisible to every sanitiser
    // (uart_device.h documents the same hazard for the RxSink). Owning the ESP
    // here makes the join part of `~Emulator()` itself, so the hazard cannot
    // exist — and none of the three frontends can get it wrong, which is the
    // failure mode `ColdBootHooks` was created to stop repeating.
    //
    // The other side of that coin, stated rather than hidden: a hard reset
    // therefore POWER-CYCLES the emulated ESP, dropping any live connection.
    // Real hardware resets the ESP only from NR 0x02 bit 7 (design doc §4.2),
    // so this is a modelling consequence of Task 70's whole-machine cold boot,
    // not an ESP decision. A soft reset does NOT rebuild it (see setup_esp()).
    std::unique_ptr<EspConnectionLog>   esp_events_;
    std::unique_ptr<esp::EspTransport>  esp_transport_;
    /// GH #210. Null when the configured listen address would not parse, which
    /// makes `AT+CIPSERVER` answer ERROR — see setup_esp().
    std::unique_ptr<esp::EspListener>   esp_listener_;
    std::unique_ptr<esp::ThreadedEsp>   esp_device_;
    std::unique_ptr<EspUartAdapter>     esp_adapter_;
    /// One-shot latch for esp_note_transport_fault(); see its header comment.
    bool esp_fault_reported_ = false;

    DivMmc          divmmc_;
    Multiface       multiface_;   // Wave 1 B1 (TASK-8-MULTIFACE-PLAN.md).
    NmiSource       nmi_source_;  // Phase 1 scaffold (TASK-NMI-SOURCE-PIPELINE-PLAN).

    // NR 0xD8 bit 0 — `nr_d8_io_trap_fdc_en` (VHDL zxnext.vhd:1263).
    // Gates the port 0x2FFD / 0x3FFD trap decode that strobes the
    // Multiface NMI path via `nmi_gen_iotrap` (VHDL:2598-2602, 3835).
    // Power-on default '0' per VHDL:5107.
    bool nr_d8_io_trap_fdc_en_ = false;

    // VHDL zxnext.vhd:2164 — `nmi_accept_cause = '1' when nmi_state =
    // S_NMI_IDLE OR nmi_state = S_NMI_FETCH else '0'`.
    // Pass-3 verify-audit (Task 2) helper: gates iotrap event capture
    // into `nr_da_iotrap_cause_` (VHDL:3871) and `nr_d9_iotrap_write_`
    // (VHDL:3892). While the FSM is in HOLD or END, an iotrap event
    // must NOT update either field.
    bool nmi_accept_cause_() const {
        const auto s = nmi_source_.state();
        return s == NmiSource::State::Idle || s == NmiSource::State::Fetch;
    }

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

    // NR 0x02 bit 7 — `nr_02_bus_reset` (VHDL zxnext.vhd:1095, :5119, :1579).
    // Captured from NR 0x02 writes verbatim (`nr_02_bus_reset <= nr_wr_dat(7)`)
    // and surfaced verbatim on NR 0x02 readback bit 7. Drives the FPGA's
    // `o_RESET_PERIPHERAL` output (line 1579) — peripheral / ESP / expansion-
    // bus reset signal not modelled in jnext. The signal has NO reset clause
    // anywhere in zxnext.vhd, so the latch survives both hard and soft reset
    // — only the signal initializer at line 1095 (FPGA power-on) sets it
    // to '0'. The C++ member initializer here handles power-on; reset()
    // intentionally does NOT clear it (mirrors VHDL).
    // Pass-3 verify-audit (Task 2): added so NR 0x02 readback bit 7 reflects
    // the last firmware write, not a hard-coded zero.
    bool    nr_02_bus_reset_ = false;
    SdCardDevice    sd_card_;
    Renderer        renderer_;
    Keyboard        keyboard_;
    // Task 19 (instant TAP load) — FUSE-style phantom typist. Idle by
    // default; armed by Emulator::load_tap() for 48K/128K/+3 machines.
    PhantomTypist   phantom_typist_;
    // Input subsystem — Phase 1 scaffold (Task 3). See src/input/*.
    Joystick        joystick_;
    // Task 79 — per-connector host input source. Default Sdl/Sdl reproduces
    // the historical behaviour (SDL pads → slots, arrows → ZX cursor keys).
    JoySource       joy_source_[2] = { JoySource::Sdl, JoySource::Sdl };
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

    // Debugger-only source mute; NOT machine state (not reset, not serialised).
    uint8_t         audio_mute_mask_ = AudioMute::NONE;
    DebugState      debug_state_;
    TraceLog        trace_log_;
    CallStack       call_stack_;
    TapLoader       tape_;
    // G33 Phase 1 — trap-based SAVE→TAP. Inactive unless --tape-save
    // supplied (EmulatorConfig::tape_save_file); armed via Emulator::init().
    TapSaver        tap_saver_;
    TzxLoader       tzx_tape_;
    WavLoader       wav_tape_;
    VideoRecorder   video_recorder_;
    RzxPlayer       rzx_player_;
    RzxRecorder     rzx_recorder_;
    uint32_t        rzx_frame_instruction_count_ = 0;

    /// Rewind snapshot buffer (null when disabled).
    std::unique_ptr<RewindBuffer> rewind_buffer_;

    /// Task 21 (2026-05-29) — per-physical-address T-state profiler.
    /// Inactive (no allocation) unless EmulatorConfig::profile is true.
    /// Hot path: the CPU per-instruction loop in run_frame() checks
    /// `profiler_.active()` (an `if (!nullptr)` branch). When inactive,
    /// it's a single always-false predicted branch — zero cost.
    Profiler profiler_;

    /// Logical frame counter — incremented each run_frame(); saved in snapshots.
    uint32_t frame_num_   = 0;

    /// When true, snapshot-taking is active (independent of buffer allocation).
    bool rewind_enabled_ = false;

    /// When true, suppresses audio/video output during fast-forward replay.
    bool replay_mode_ = false;

    /// Task 70 — set by request_hard_reset() (NR 0x02 bit 1 / F1); consumed by
    /// the host frontend after run_frame() to perform a cold boot.
    bool hard_reset_requested_ = false;

    /// Task 27 C6 — frontend render hint (see set_render_enabled()). NOT
    /// serialised: it is host-presentation state, not machine state.
    bool render_enabled_ = true;

    /// G156 — remaining frames the CPU is held idle after a NEX load with
    /// non-zero loading_delay/start_delay. See set_boot_hold_frames().
    uint32_t boot_hold_frames_remaining_ = 0;

    /// GH #164 — CPU parked indefinitely (no instruction fetch/execute).
    /// See set_cpu_parked(). Serialised with the rest of the machine state
    /// so a rewind cannot silently un-park a load-only NEX.
    bool cpu_parked_ = false;

    /// Everything that must happen exactly once per frame, at its start (Copper vsync,
    /// per-scanline change-log baselines, interrupt scheduling, rewind snapshot). Called
    /// from run_frame() ONLY when a new frame actually begins — never when resuming a
    /// frame the debugger paused mid-way, which would corrupt the frame still in flight
    /// (Task 40: it rewinds the Copper and clears the change logs the compositor replays).
    void begin_new_frame();

    /// GH #25 — build the emulated ESP-01 and attach it to UART 0. Called from
    /// init(); a no-op when `EmulatorConfig::esp_enabled` is false, and a no-op
    /// on a SOFT reset that finds one already built (real hardware's soft reset
    /// does not touch the module on the far end of the cable — design doc §4.3).
    void setup_esp();

    /// GH #25 — once-per-`run_frame()` ESP service, and the ONLY place jnext
    /// drives the device outside the per-instruction `tick()`.
    ///
    /// DELIBERATELY NOT IN begin_new_frame(), per the owner decision recorded
    /// in design doc §7.1: socket work lives on the `ThreadedEsp` worker so a
    /// slow name lookup or socket read can never stall the frame loop. Nothing
    /// this function calls does socket work — `ThreadedEsp::poll()` is a
    /// documented no-op while the worker is running. What it actually does is:
    ///
    ///   1. apply the replay gate (`EspUartAdapter::set_inert`);
    ///   2. mirror the ESP's `wants_tick()` into the hot-path gate, WITHOUT
    ///      which a byte that arrives from the network on the worker thread is
    ///      never noticed and never reaches the guest;
    ///   3. re-read the `esp01` log level, so `--log-level` takes effect within
    ///      one frame;
    ///   4. surface a throwing transport exactly once.
    ///
    /// It sits at the top of run_frame() rather than inside the
    /// `!frame_in_progress_` branch so that the replay gate is applied before
    /// the FIRST instruction of a replayed frame, including the nested
    /// run_frame() calls rewind_to_cycle() makes with `replay_mode_` set.
    void service_esp_frame();

    /// GH #246 — what the scheduled WiFi outage says the association should be
    /// at frame `frame`. PURE: same answer however often the emulator passes
    /// through that frame, which is what makes a rewind reproduce it instead of
    /// carrying a stale flag back through time. See its definition.
    bool esp_association_at(int frame) const;

    /// Apply `esp_association_at(esp_frames_)` to the module, logging a real
    /// change. `force` skips the edge test — used after a state restore, where
    /// the clock JUMPED and no edge was crossed.
    void sync_esp_association(bool force);

    /// One logical frame of the outage clock: sync, then advance. Called from
    /// begin_new_frame(), which runs exactly once per frame and runs for
    /// replayed frames too.
    void advance_esp_schedule_frame();

    /// Logical frames since the ESP was built — the unit `--esp-delayed-*-frames`
    /// counts in. TRAVELS IN THE SNAPSHOT (save_state/load_state), so a rewind
    /// puts the outage back where it was. Not reset by a soft reset, for the
    /// same reason the module is not rebuilt by one (design doc §4.3): the AP
    /// outage is happening out there, and the guest resetting the Next has no
    /// bearing on when it ends.
    int esp_frames_ = 0;

    /// Task 51 — re-derive every timing surface that depends on the EFFECTIVE
    /// machine-timing mode (tim_sel axis, `contention_.machine_timing()`):
    /// VideoTiming constants, the master-cycle frame geometry (`timing_`),
    /// the CPU-side line geometry (`z80_set_frame_geometry`) and the ULA
    /// counter origins. Called from the frame-edge NR 0x03 commit in
    /// begin_new_frame() when the effective mode changes, and from
    /// load_state() (a rewind/load may cross a timing change). VHDL: all
    /// zxula_timing c_* constants switch combinationally on
    /// `eff_nr_03_machine_timing` (zxula_timing.vhd:147-280 via
    /// zxnext.vhd:6694-6703).
    void repush_video_timing_from_machine_timing();

    /// GH #237 — program VideoTiming and everything derived from it (the
    /// master-cycle frame geometry `timing_`, the CPU-side frame geometry
    /// and ULA counter origins, and the Copper's vertical wrap) from ONE
    /// (tim_sel, 50/60 Hz) pair. Shared by Emulator::init() and by
    /// repush_video_timing_from_machine_timing(), so the boot seed and the
    /// runtime frame-edge commit cannot program different constants.
    void apply_video_timing(MachineTimingMode mode, bool refresh_60hz);

    /// True between the start of a frame and its completion. The debugger pauses by
    /// returning from inside run_frame()'s loop, so the next call must RESUME that frame,
    /// not restart it.
    bool frame_in_progress_ = false;

    /// Boot ROM (8K FPGA bootloader, embedded into the jnext binary at
    /// link time — see core/embedded_nextboot_rom.h).
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

    /// Task 60b — subsystem name of the sentinel that failed in the last
    /// load_state (empty when the last load succeeded). Not serialised.
    std::string last_state_error_;

    /// Task 60e — monotonic corruption-incident counter, bumped whenever a
    /// fresh corruption is latched into last_state_error_. Not serialised.
    uint64_t state_error_generation_ = 0;

    /// Raster position snapshotted at pause time.
    int paused_vc_ = 0;
    int paused_hc_ = 0;

    /// Raster position at the end of the last completed frame (saved before frame_cycle_ advances).
    int last_frame_vc_ = 0;
    int last_frame_hc_ = 0;

    /// FUSE tstates value at frame start (for contention position calc).
    uint32_t frame_ts_start_ = 0;

    /// Accumulated FUSE tstates of all completed frames (G36/G37).
    /// begin_new_frame() folds the outgoing frame's final counter value
    /// (including any end-of-frame overshoot) into this base before
    /// zeroing the FUSE counter, so `monotonic_tstates()` never goes
    /// backwards across the per-frame reset.
    uint64_t tstates_frame_base_ = 0;

    /// Audio timing: fractional accumulators for PSG ticking and sample generation.
    /// PSG clock = 28 MHz / 16 = 1.75 MHz.
    uint64_t psg_accum_ = 0;      ///< Accumulates master cycles for PSG tick timing
    uint64_t sample_accum_ = 0;   ///< Accumulates master cycles for sample generation

    /// Feed `master_cycles` of emulated time to the audio mixer, emitting output
    /// samples as their intervals complete. The mixer INTEGRATES the source
    /// levels over each interval rather than point-sampling them at its end —
    /// see Mixer::emit_sample() for why (aliasing / the beeper whistle).
    void advance_audio(uint64_t master_cycles);

    /// Last CPU speed we emitted a log line for. Guest state changes are logged
    /// only when the value actually changes (core/log.h level policy); programs
    /// rewrite NR 0x07 every frame. 0 = 3.5 MHz, the power-on value.
    uint8_t last_logged_cpu_speed_ = 0;

    /// DAC enable flag (NextREG 0x08 bit 3).
    bool dac_enabled_ = false;

    /// Phase-1 NmiSource seam: previous cycle's `nmi_generate_n` level,
    /// used to detect the active-low falling edge that drives
    /// `Z80Cpu::request_nmi()`. Starts `true` (line inactive) so the
    /// first cycle does not spuriously latch an NMI. See
    /// TASK-NMI-SOURCE-PIPELINE-PLAN.md §Phase 1.
    bool prev_nmi_generate_n_ = true;

    /// G46(b) #102 session 3 probe state (env-gated via
    /// set_g46b_port_trace()/set_g46b_port_trace_frame(); zero cost when
    /// the FILE* is null — single pointer check at the call site).
    std::FILE* g46b_port_trace_file_ = nullptr;
    long       g46b_port_trace_frame_ = 0;

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

    /// G112: NR 0x2D I2S sample latch (VHDL zxnext.vhd:6006-6015).
    /// Stores pi_audio_L(1:0) or pi_audio_R(1:0) — captured on the read of
    /// NR 0x2C / 0x2E — pre-shifted into bits [7:6] of the byte; bits [5:0]
    /// are always zero. NR 0x2D read returns this byte verbatim.
    uint8_t nr_2d_i2s_sample_ = 0;

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

    // V20-IM2-01 — pulse-mode INT-line edge tracking. Per VHDL
    // zxnext.vhd:2017-2031 the `pulse_int_n` line drops to '0' for
    // 32/36 cycles when any peripheral's `o_pulse_en` fires, then
    // returns to '1' via `pulse_count_end`. The Z80 /INT pin is the
    // AND of `pulse_int_n AND im2_int_n` (line :1840, default expbus
    // disabled scenario). To assert CPU INT exactly ONCE per pulse,
    // we track the previous-tick `pulse_int_n` and call
    // `cpu_.request_interrupt(0xFF)` only on the falling edge — NOT
    // on every tick while pulse_int_n stays low (which would re-stamp
    // `int_requested_at_` each tick, EXTENDING the effective 32/36-
    // cycle window indefinitely and causing extra INT acceptances
    // when the CPU exits an ISR via EI within the window). Initial
    // value true matches `Im2Controller::reset()` default.
    bool     prev_pulse_int_n_   = true;

    // NR 0xC6 raw readback shadow — VHDL read (zxnext.vhd:6245):
    //   port_253b_dat <= '0' & nr_c6_int_en_2_654 & '0' & nr_c6_int_en_2_210
    // Bits 7 and 3 always read as 0. We store the 6 meaningful bits verbatim
    // so the read handler returns exactly what was written.
    uint8_t  nr_c6_uart_int_en_  = 0;

    // NR 0xA0 — Pi peripheral enable byte (G135). VHDL zxnext.vhd:1241,
    // 2278-2281, 5080, 5561, 6189. Reset 0x00 (zxnext.vhd:5080). Bit
    // fan-out:
    //   bit 5 = pi_uart_rxtx (UART1 RX/TX cross on Pi GPIO 14/15 mux)
    //   bit 4 = pi_uart_en   (UART1 GPIO 14/15 mux enable)
    //   bit 3 = pi_i2c1_en   (I2C1 GPIO 2/3 mux enable)
    //   bit 0 = pi_spi0_en   (SPI0 GPIO 7..11 mux enable)
    // Read mask per VHDL :6189:
    //   port_253b_dat <= "00" & nr_a0(5 downto 3) & "00" & nr_a0(0)
    // → mask 0x39, bits 7,6,2,1 read as 0.
    uint8_t  nr_a0_pi_peripheral_en_ = 0;

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

    // NR 0x05 bit 0 — effective (frame-edge-latched) copy of
    // `nr_05_scandouble_en`. VHDL zxnext.vhd:6702 latches
    // `eff_nr_05_scandouble_en <= nr_05_scandouble_en` at every
    // `video_frame_sync`; the NR 0x05 read mux (:5897) surfaces the
    // effective copy, not the pending FF (Task 58). Latched in
    // begin_new_frame(), seeded in init(), re-derived in load_state()
    // (snapshots are frame-edge, pending == effective). Not
    // serialised. The bit-2 analogue (`eff_nr_05_5060`) lives in
    // VideoTiming::refresh_60hz().
    bool     eff_nr_05_scandouble_en_ = false;

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

    /// G135 — NR 0xA0 Pi peripheral enable raw byte (zxnext.vhd:1241).
    /// Test-only readback of the stored byte (read handler returns the
    /// VHDL-masked composition, not this raw value).
    uint8_t  nr_a0_pi_peripheral_en() const { return nr_a0_pi_peripheral_en_; }

    /// G135 / VHDL zxnext.vhd:2278-2281 — bit fan-out accessors.
    bool pi_uart_rxtx() const { return (nr_a0_pi_peripheral_en_ & 0x20) != 0; }  ///< b5
    bool pi_uart_en()   const { return (nr_a0_pi_peripheral_en_ & 0x10) != 0; }  ///< b4
    bool pi_i2c1_en()   const { return (nr_a0_pi_peripheral_en_ & 0x08) != 0; }  ///< b3
    bool pi_spi0_en()   const { return (nr_a0_pi_peripheral_en_ & 0x01) != 0; }  ///< b0

    /// G135 — direct NR 0xA0 setter (test-only path; the production path
    /// goes through the NR 0xA0 write handler installed in init()).
    void set_nr_a0_pi_peripheral_en(uint8_t v) { nr_a0_pi_peripheral_en_ = v; }

private:

    // -----------------------------------------------------------------------
    // G65 — deferred CPU NR-write queue (see public method docs).
    // -----------------------------------------------------------------------
    bool                                     defer_cpu_nr_writes_ = false;
    std::vector<std::pair<uint8_t, uint8_t>> pending_cpu_nr_writes_;

    // Canonical ED 45 was decoded during the current instruction.
    // Multiface remains mapped while RETN executes, then unmaps at the
    // instruction boundary before the returned-to opcode is inspected.
    bool multiface_retn_pending_ = false;

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

    /// Schedule a full frame's worth of SCANLINE events into the scheduler.
    void schedule_frame_events();

    /// Current VHDL `cvc` counter (o_vc_cu) at the present master cycle:
    /// the copper-offset raster line, origin = first paper line, wrapping at
    /// c_max_vc, shifted by NR 0x64 cu_offset (zxula_timing.vhd:455-472).
    /// Read back by NR 0x1E/0x1F and used by the line-interrupt comparator.
    int current_cvc() const;

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

    /// Task 60a — the SINGLE shared per-instruction body: executes one
    /// CPU instruction (or one DMA burst when the DMA holds the bus, or
    /// one NOP-sized boot-hold step), including the trace record, the
    /// IM2-controller tick + set_nmi_activated push, the IM2-mode and
    /// pulse-mode /INT line polls, the RZX instruction count and the
    /// real-time tape tick, then advances the master clock and the DMA
    /// burst prescaler. Returns the 28 MHz master cycles consumed.
    ///
    /// Called by BOTH run_frame()'s inner loop and
    /// execute_single_instruction() so the two paths cannot drift again:
    /// the debugger single-step path used to hand-maintain a copy of
    /// this cluster and had silently lost im2_.tick(), both /INT polls
    /// and md6_.tick() — stepping through interrupt-driven code never
    /// delivered CTC/UART/ULA-frame interrupts.
    uint64_t step_one_instruction();

    /// Task 60a — the shared post-instruction device-tick cluster:
    /// Copper (G117), deferred CPU NR-write drain (G65), contention /
    /// CPU-speed shadow commits, CTC + UART + MD6 ticks, IoMode UART
    /// injectors (G72), the NMI source pipeline + /NMI edge, PSG +
    /// audio, and the scheduler drain. run_frame() calls this AFTER its
    /// data-breakpoint check (which early-returns without running this
    /// tail — pre-existing behaviour, preserved).
    void tick_devices_after_instruction(uint64_t master_cycles);

    /// GH #207 — the SINGLE shared end-of-frame body: end-of-frame raster
    /// snapshot, `frame_cycle_` advance to `frame_end`, the boot-hold
    /// countdown, the last-row display-state snapshots, the frame render
    /// (or the sprite-side-effects-only pass), video/RZX capture, and the
    /// phantom-typist / auto-type / membrane-scan per-frame ticks.
    ///
    /// Called by BOTH run_frame() (once its loop reaches `frame_end`) and
    /// step_frame_slot(), for the same reason step_one_instruction() is
    /// shared: the debugger's stepping path had NO frame boundary at all, so
    /// begin_new_frame() was never reached again once the debugger paused and
    /// nothing scheduled per frame — the ULA frame interrupt above all — ever
    /// fired again.
    void end_of_frame(uint64_t frame_end);

    /// GH #207 — one instruction slot plus the per-FRAME bookkeeping that
    /// run_frame() performs around its own loop: begin a frame if none is in
    /// flight, run the shared per-instruction body, and end the frame once
    /// the clock reaches its last cycle. Returns the master cycles consumed.
    /// Only debugger_step() calls it.
    uint64_t step_frame_slot();

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
