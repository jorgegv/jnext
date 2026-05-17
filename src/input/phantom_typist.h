#pragma once

#include <cstdint>
#include "memory/contention.h"   // MachineType

class Keyboard;

/// PhantomTypist — FUSE-style "wait for ROM to be ready" auto-typer.
///
/// Task 19: instant in-memory TAP loading for 48K / 128K / +3 modes.
///
/// Direct port of the FUSE 1.5.7 `phantom_typist.c` design (Philip Kendall,
/// GPLv2+). The architecture solves a single problem cleanly: when should
/// the emulator inject the LOAD"" keystroke sequence?
///
/// The naive answer (fixed 100-frame delay) works most of the time but is
/// fragile across machine modes (128K and +3 boot through menus, +3
/// additionally pauses on disk probe). FUSE's answer is to detect a
/// hardware-level signal that proves "BASIC is ready for input":
///
///   The phantom typist hooks port 0xFE reads (the keyboard half-row scan)
///   and accumulates which of the 8 half-rows have been read in the
///   current frame. When ALL 8 half-rows have been polled (i.e.
///   `keyboard_ports_read == 0xff`), that's the canonical signal that the
///   ROM's full-matrix scan loop is running — meaning BASIC's main input
///   loop. At that moment the typist begins playing the per-machine
///   keypress sequence.
///
/// Per-machine sequences (mapped to `MachineType`):
///   48K     → KEYWORD: `J` SymShift+P SymShift+P ENTER  (= `LOAD ""` + ENTER)
///   128K    → MENU:    ENTER  (Tape Loader is the default menu entry)
///   +3      → PLUS3:   ENTER  (Loader → 48K BASIC auto-runs LOAD"")
///                       (the +3 default menu entry runs `LOAD ""` itself)
///   Next    → unchanged — Next doesn't run BASIC; tape support uses the
///             classic 48K/128K trap which isn't relevant in NextZXOS
///             flows.
///
/// Hookup:
///   - `arm()` is called from `Emulator::load_tap()` instead of an
///     immediate `keyboard_.queue_auto_type()`.
///   - `notice_keyboard_row_read()` is called from the port 0xFE read
///     handler in `emulator.cpp` on every CPU read.
///   - `tick_frame()` is called once per emulated frame (from the
///     existing per-frame tick path) — it resets the row-read bitmask
///     and, if armed and triggered, queues the per-machine sequence into
///     the existing `Keyboard::queue_auto_type()`.
///
/// Notes:
///   - This does NOT intercept the port 0xFE read response itself.
///     Jnext already has a working frame-accurate auto-type system
///     (`Keyboard::queue_auto_type()` / `tick_auto_type()`), so the
///     phantom typist only needs to gate WHEN the sequence is queued.
///   - JPPI / LOADPPCODE variants (for `LOAD ""CODE` headers without a
///     BASIC stub) are documented but not currently used by jnext's
///     test TAPs. Marked as future work.
class PhantomTypist {
public:
    /// Construct an idle, un-armed typist. Caller must wire the owning
    /// Keyboard pointer before calling `tick_frame()` (jnext does this
    /// in `Emulator` setup).
    PhantomTypist() = default;

    /// Install the Keyboard back-pointer. Called once during Emulator
    /// construction; not changed across reset.
    void attach(Keyboard* kb) { keyboard_ = kb; }

    /// Arm the typist for a given machine. After this call, the typist
    /// enters WAITING state and will begin playing its per-machine
    /// sequence as soon as a full 8-row keyboard scan is observed.
    ///
    /// `needs_code` reserved for future JPPI-mode (`LOAD ""CODE`) — not
    /// currently exercised by jnext's TAP regression set.
    ///
    /// Re-arming is supported from any state (including mid-play): the
    /// row-read bitmask is cleared and the sequence restarts from the
    /// WAITING phase.
    void arm(MachineType machine, bool needs_code = false);

    /// Force the typist back to inactive. Called on emulator reset and
    /// when load_tap() should NOT trigger autostart (e.g. Next machine
    /// or future explicit "don't auto-run" UI option).
    void reset();

    /// Called from the port 0xFE read handler — one call per CPU read.
    /// `port_high` is the upper byte of the 16-bit port address (the
    /// row-select byte, active-low). The typist OR-folds the low (=
    /// selected) bits of `port_high`'s complement to detect when all 8
    /// rows have been scanned in the current frame.
    ///
    /// Hot path: this is called frequently (every BASIC main-loop iter
    /// reads all 8 rows), so the implementation is intentionally tiny.
    /// Zero-cost when the typist is INACTIVE.
    void notice_keyboard_row_read(uint8_t port_high);

    /// Called once per emulated frame. Three jobs:
    ///   (1) Reset `keyboard_ports_read_` (FUSE: matches frame-boundary
    ///       semantics — a "full scan" must complete within one frame).
    ///   (2) If WAITING and the previous frame saw a full scan
    ///       (`keyboard_ports_read_ == 0xff`), queue the per-machine
    ///       keypress sequence into the Keyboard auto-type queue and
    ///       transition to INACTIVE (the auto-type queue takes over from
    ///       here — jnext doesn't need FUSE's per-state row-injection
    ///       because `Keyboard::queue_auto_type()` already plays frames
    ///       at the matrix level).
    ///   (3) Idle otherwise.
    void tick_frame();

    /// True if the typist is armed (waiting or currently delivering).
    /// Useful for UI status display.
    bool is_active() const { return state_ != State::INACTIVE; }

private:
    enum class State : uint8_t {
        INACTIVE,    // Idle. No further work.
        WAITING,     // Armed; watching for full keyboard scan.
        POST_DELAY,  // Full scan seen; counting down to ED-EDIT readiness.
    };

    enum class Mode : uint8_t {
        JPP,      // 48K KEYWORD: J + SymShift+P + SymShift+P + ENTER
        ENTER,    // 128K/+3 MENU: ENTER only
        // JPPI / LOADPPCODE / PLUS3_CODE_BLOCK: future work for CODE-only TAPs.
    };

    /// Queue the per-machine keystroke sequence onto Keyboard auto-type
    /// and return to INACTIVE.
    void fire();

    Keyboard* keyboard_ = nullptr;
    State    state_     = State::INACTIVE;
    Mode     mode_      = Mode::JPP;
    /// Per-frame OR-accumulator of row indices read by port 0xFE.
    /// Each of bits 0..7 = "row N has been read this frame".
    /// FUSE accumulates exactly this and tests for 0xff.
    uint8_t  keyboard_ports_read_ = 0x00;

    /// Frames remaining in POST_DELAY before fire(). Why a fixed delay
    /// after the first full keyboard scan rather than firing
    /// immediately (FUSE's choice):
    ///
    ///   On 48K BASIC the very first IM-1 interrupt after IM 1 is set
    ///   up calls KEY-INT/KEY-SCAN which scans all 8 rows — but this
    ///   happens DURING the copyright-message print phase, before
    ///   ED-EDIT (the main BASIC input loop) is running. Any
    ///   keystroke injected then is processed by KEY-INT (which sets
    ///   FLAGS bit 5 + LAST-K), then CONS-IN entry to ED-EDIT clears
    ///   FLAGS bit 5 and waits for it to be set again — so the
    ///   keystroke is "consumed" by the wrong consumer and ED-EDIT
    ///   never sees it.
    ///
    ///   FUSE works around this in two ways: (1) a fastloading timer
    ///   that runs the emulator at max speed until the typist finishes
    ///   (timer_start_fastloading), and (2) per-state delays
    ///   (state_info[LOAD].delay_before_state = 8) that defer the
    ///   first injection by 8 frames after entering the LOAD state.
    ///
    ///   jnext implements BOTH FUSE-style mechanisms now (Task 19
    ///   fastload follow-up): a fastload pacing bypass in the
    ///   platform layer (see `Emulator::fastload_active()`) skips
    ///   the 20 ms-per-frame `SDL_Delay`/`QTimer` wait while the
    ///   typist is armed or fast-load tape data is in flight, AND
    ///   the per-trigger countdown below is now POST_DELAY = 8
    ///   frames matching FUSE's `state_info[LOAD].delay_before_state`.
    ///   The combination makes the entire ROM-boot → LOAD"" → first
    ///   data-byte path complete in well under 100 ms wall-clock on a
    ///   modern host — visually "instant" like FUSE.
    static constexpr uint8_t POST_TRIGGER_DELAY_FRAMES = 8;
    uint8_t  post_delay_frames_ = 0;
};
