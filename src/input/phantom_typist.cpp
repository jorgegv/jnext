#include "input/phantom_typist.h"
#include "input/keyboard.h"
#include "core/log.h"

#include <vector>

// ---------------------------------------------------------------------------
// PhantomTypist
//
// Task 19 (instant TAP load) — see phantom_typist.h for the architecture
// summary. The code below mirrors the FUSE phantom_typist.c WAITING-state
// trigger logic 1:1; jnext deviates only in how it delivers the keypress
// stream (Keyboard::queue_auto_type instead of FUSE's per-state row
// injection via the ULA-read intercept).
// ---------------------------------------------------------------------------

void PhantomTypist::arm(MachineType machine, bool needs_code)
{
    (void)needs_code; // Reserved for future JPPI (`LOAD ""CODE`) support.

    // Select the keypress mode per machine. Mapping is identical to
    // FUSE's `get_highlevel_mode()` (phantom_typist.c:243-282), reduced
    // to the two modes jnext currently produces:
    //
    //   48K           → JPP   (KEYWORD mode: J + SS+P + SS+P + ENTER)
    //   128K          → ENTER (MENU mode:    default option = LOAD"")
    //   +3            → ENTER (PLUS3 mode:   default option = Loader)
    //   ZXN_ISSUE2    → JPP   (best-effort; Next + TAP is unusual but
    //                           we follow the same path as 48K BASIC
    //                           kbd-keyword detect — if the caller has
    //                           reached this code path in Next mode
    //                           they have configured a 48K-style BASIC
    //                           prompt, e.g. via NextZXOS .BAS or 48K
    //                           submenu. `Emulator::load_tap()` will
    //                           normally skip arming for Next mode.)
    switch (machine) {
        case MachineType::ZX48K:
            mode_ = Mode::JPP;
            break;
        case MachineType::ZX128K:
        case MachineType::ZX_PLUS3:
            mode_ = Mode::ENTER;
            break;
        case MachineType::ZXN_ISSUE2:
        default:
            mode_ = Mode::JPP;
            break;
    }

    state_               = State::WAITING;
    keyboard_ports_read_ = 0x00;
    post_delay_frames_   = 0;

    Log::input()->info("PhantomTypist: armed for machine={} mode={}",
                       static_cast<int>(machine),
                       mode_ == Mode::JPP ? "JPP" : "ENTER");
}

void PhantomTypist::reset()
{
    state_               = State::INACTIVE;
    keyboard_ports_read_ = 0x00;
    post_delay_frames_   = 0;
}

void PhantomTypist::notice_keyboard_row_read(uint8_t port_high)
{
    // Fast-path: typist not waiting → nothing to do. Keeps the cost of
    // the port-0xFE read handler at one branch when --load isn't in
    // play. POST_DELAY also short-circuits — once we've detected the
    // first full scan, we no longer need to accumulate.
    if (state_ != State::WAITING) return;

    // Mirror FUSE phantom_typist.c:373-386 — only accumulate when EXACTLY
    // one row is selected (one bit in port_high is low, all others high).
    // This matches the ROM's polling pattern and avoids false positives
    // from non-keyboard reads on port 0xFE (which happen with addr_high
    // = 0xff or other patterns).
    //
    // FUSE uses an explicit switch over the 8 valid values; we replicate
    // it as a small lookup. Active-low: `~port_high` is the row-select
    // mask, and a valid single-row read has exactly one bit set in it.
    const uint8_t row_mask = static_cast<uint8_t>(~port_high);
    // Reject anything that isn't a single-bit row select:
    //   0xfe → 0x01,  0xfd → 0x02,  0xfb → 0x04,  0xf7 → 0x08,
    //   0xef → 0x10,  0xdf → 0x20,  0xbf → 0x40,  0x7f → 0x80
    // power-of-two test: x != 0 && (x & (x-1)) == 0.
    if (row_mask == 0 || (row_mask & (row_mask - 1)) != 0) return;

    keyboard_ports_read_ |= row_mask;
}

void PhantomTypist::tick_frame()
{
    // FUSE phantom_typist.c:532-553 frame hook, plus a jnext-specific
    // POST_DELAY hop. Three responsibilities:
    //
    //   1. WAITING + previous frame accumulator reached 0xff → enter
    //      POST_DELAY for POST_TRIGGER_DELAY_FRAMES frames. The first
    //      48K full-scan happens during copyright print, before
    //      ED-EDIT; we need a safety margin so the J keypress lands
    //      AFTER ED-EDIT starts reading (see header rationale).
    //   2. POST_DELAY → countdown; on reaching zero, fire().
    //   3. Always reset `keyboard_ports_read_` so the next frame
    //      starts a fresh accumulation. (FUSE does this too.)
    switch (state_) {
        case State::WAITING:
            if (keyboard_ports_read_ == 0xff) {
                state_              = State::POST_DELAY;
                post_delay_frames_  = POST_TRIGGER_DELAY_FRAMES;
                Log::input()->info("PhantomTypist: full scan observed; "
                                   "deferring fire for {} frames",
                                   post_delay_frames_);
            }
            break;

        case State::POST_DELAY:
            if (post_delay_frames_ > 0) {
                --post_delay_frames_;
            }
            if (post_delay_frames_ == 0) {
                fire();
            }
            break;

        case State::INACTIVE:
            break;
    }
    keyboard_ports_read_ = 0x00;
}

void PhantomTypist::fire()
{
    if (!keyboard_) {
        // Safety net for tests that arm without attach(); transition
        // to INACTIVE so we don't re-fire forever.
        state_ = State::INACTIVE;
        return;
    }

    // Build the keystroke sequence for the active mode. Frame counts
    // (5 frames per key) match the pre-Task-19 immediate path so
    // user-visible typing pace is unchanged once the trigger fires.
    // The POST_DELAY hop above ensures ED-EDIT is reading by the time
    // the first key is pressed — no per-key extension needed.
    //
    // Matrix positions per `Keyboard::s_map` (src/input/keyboard.cpp).
    std::vector<Keyboard::AutoKey> keys;
    switch (mode_) {
        case Mode::JPP:
            // J  → row 6 col 3 (= LOAD keyword in keyword mode)
            // SS+P  → row 7 col 1 + row 5 col 0 (= ")
            // SS+P  → "
            // ENTER → row 6 col 0
            keys = {
                {6, 3,  -1, -1, 5},   // J
                {5, 0,   7,  1, 5},   // SS+P = "
                {5, 0,   7,  1, 5},   // SS+P = "
                {6, 0,  -1, -1, 5},   // ENTER
            };
            break;

        case Mode::ENTER:
            keys = {
                {6, 0,  -1, -1, 5},   // ENTER — selects default menu entry
            };
            break;
    }

    keyboard_->queue_auto_type(keys);
    state_ = State::INACTIVE;
    Log::input()->info("PhantomTypist: full-scan detected, queued {} keystrokes",
                       keys.size());
}
