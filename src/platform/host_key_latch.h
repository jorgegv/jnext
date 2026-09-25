#pragma once

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <deque>
#include <vector>

/// Minimum-hold latch for host key events (GitHub issue #120).
///
/// WHY THIS EXISTS — a keypress can be delivered and withdrawn without the
/// emulated machine ever getting a chance to look.
///
/// The Qt frontend writes host key events STRAIGHT into the membrane matrix:
/// `MainWindow::handle_key` -> `QtApp`'s key callback -> `Keyboard::set_key`
/// (src/gui/qt_app.cpp). Those callbacks run from the Qt event loop, which is
/// also what dispatches the frame timer — so they can only ever land BETWEEN
/// two `Emulator::run_frame()` calls, never inside one. The guest's only view
/// of the matrix is a port 0xFE read during `run_frame()`.
///
/// Consequently the matrix is, from the host's point of view, sampled exactly
/// ONCE PER EMULATED FRAME. A press and its release delivered in the same gap
/// set the bit and clear it again with no frame in between: the guest sees
/// nothing at all, and the character is silently lost.
///
/// This is not the same defect as the host being too slow. It is a race whose
/// window is the inter-frame gap, so it WIDENS as the frame rate falls — which
/// is exactly why issue #120's reporter sees dropped characters and a low frame
/// rate together. It also happens on a perfectly fast host whenever the event
/// loop is briefly blocked (a long tick handler, a modal dialog closing, a
/// window-system hiccup): the queued press and release are then both drained
/// before the next timer tick.
///
/// REAL HARDWARE DOES NOT HAVE THIS RACE. The FPGA membrane scanner runs at
/// pixel-clock rate (`membrane.vhd`), so any physical press outlasts many
/// scans. The one-frame sampling is an artefact of emulating a frame as an
/// indivisible unit, and this latch restores the hardware's behaviour: a press
/// is held down until at least one emulated frame has had the chance to see it.
///
/// WHAT IT IS NOT. It does not extend anything that already worked: a press
/// whose release arrives after a frame has run is passed through untouched
/// (`on_release` returns true, same call, same instant). The latch only ever
/// acts on a press that would otherwise have been invisible.
///
/// MERGED TAPS AND MANUFACTURED CHORDS — the residue this latch left, closed
/// by the Router's serialiser below (GitHub issue #268).
///
/// The latch alone holds a press down until a frame has seen it, but it does
/// nothing about what ARRIVES NEXT. A second key pressed while the first one's
/// release is still deferred is asserted on top of it, so the frame samples a
/// TWO-KEY CHORD the host never had. Two consequences, both reported:
///
///   * two taps of DIFFERENT keys in one gap -> the ZX ROM's KEY-SCAN rejects
///     a two-key chord and BOTH characters vanish;
///   * a tap of SYMBOL SHIFT followed by a tap of P -> the frame sees
///     SYM+P and the guest types `"` instead of PRINT. Not a dropped
///     character: the WRONG one.
///
/// Two complete taps of the SAME key in one gap likewise collapsed into one
/// continuous press, losing one keystroke.
///
/// Do NOT justify any of that by appeal to the hardware: it is NOT what a real
/// Next does. `membrane.vhd:99-155` is a bare 9-state one-hot row scanner
/// clocked by i_CLK/i_CLK_EN with NO debounce counter and NO interrupt gating —
/// the only hold in the file is the deliberate two-scan CS/SYM shift hysteresis
/// at :178, which EXTENDS a release rather than merging taps. `matrix_state` is
/// refreshed every full scan, thousands of times per physical keystroke, so
/// hardware can neither merge two taps nor synthesise a chord from two
/// sequential ones. Both were artefacts of THIS EMULATOR handing host input to
/// the guest once per emulated frame — a window that WIDENS as the frame rate
/// falls, which is why the two issues arrived together with a low-FPS report.
///
/// The fix is in `Router` (below), not here: the Latch's contract is unchanged
/// and every HKL-* row still holds. The Router SERIALISES the host event
/// stream so that a keystroke which cannot be shown yet is queued rather than
/// piled on top of the one in flight.
namespace host_key_latch {

/// SDL_SCANCODE_COUNT — still 512 in SDL 3.4.16, exactly as SDL2's
/// SDL_NUM_SCANCODES was; the constant was renamed, not renumbered (GH #57).
/// Hard-coded rather than included so this header stays pure (no SDL
/// dependency) and unit-testable on its own; the wiring in src/gui/qt_app.cpp
/// is what feeds it real SDL_Scancode values, and anything out of range is
/// passed through unlatched.
inline constexpr int MAX_KEYS = 512;

/// SDL's modifier block, 224..231 — LCTRL, LSHIFT, LALT, LGUI, RCTRL, RSHIFT,
/// RALT, RGUI, in that order and contiguous. Hard-coded for the same reason as
/// MAX_KEYS (this header stays free of SDL so it is unit-testable on its own);
/// the values are pinned against the real SDL constants by static_asserts in
/// src/input/keyboard.cpp, which is compiled in every configuration.
///
/// WHAT MAKES THESE SPECIAL, and it is not that SDL calls them modifiers.
/// Four of them ARE ZX keys — LSHIFT/RSHIFT are CAPS SHIFT (row 0 col 0) and
/// LCTRL/RCTRL are SYMBOL SHIFT (row 7 col 1), keyboard.cpp:68-69,124-125 —
/// but they are the only keys the ZX ROM expects to see held TOGETHER with
/// another key, because that is how every shifted character is typed
/// (KEY-SCAN counts them separately from the keys they shift). LALT/RALT are
/// jnext's host Alt modifier (keyboard.cpp:270) and LGUI/RGUI map to nothing.
/// None of them may start a new keystroke group, or every shifted character
/// would be split across two frames and stop working.
inline constexpr int MOD_FIRST = 224;   // SDL_SCANCODE_LCTRL
inline constexpr int MOD_LAST  = 231;   // SDL_SCANCODE_RGUI

/// True for the scancodes above. Out-of-range scancodes are NOT modifiers:
/// they are unknown keys and get the conservative treatment.
inline constexpr bool is_modifier(int sc)
{
    return sc >= MOD_FIRST && sc <= MOD_LAST;
}

/// Pending-event bound for Router (issue #268). The drain rate is one
/// keystroke per emulated frame, so even at 18 FPS it is ~18 keystrokes/s
/// against a human's 5-10: the queue only grows while the emulator is running
/// NO frames at all (debugger paused, a long stall), and filling it needs
/// ~13 s of stalled emulation with continuous typing. See Router::enqueue for
/// what happens if that ever does occur.
inline constexpr std::size_t MAX_PENDING = 256;

class Latch {
public:
    /// Host key-down. Returns true when the caller must assert the key — which
    /// is always: a press is never delayed, only a release.
    bool on_press(int sc)
    {
        if (!in_range(sc)) return true;
        // A re-press cancels any release still pending for this key: the key is
        // physically down again, so releasing it would be a lie. See "merged
        // taps are correct" above.
        deferred_.erase(std::remove(deferred_.begin(), deferred_.end(), sc),
                        deferred_.end());
        fresh_.set(static_cast<size_t>(sc));
        return true;
    }

    /// Host key-up. Returns true when the caller must clear the key NOW, false
    /// when the release has been deferred until a frame has observed the press.
    bool on_release(int sc)
    {
        if (!in_range(sc)) return true;
        // Not fresh means either a frame has already run since the press (the
        // guest has had its look) or there was no press at all — a stray
        // release, e.g. the key-up half of a chord pressed before the window
        // had focus. Both pass straight through.
        if (!fresh_.test(static_cast<size_t>(sc))) return true;
        if (std::find(deferred_.begin(), deferred_.end(), sc) == deferred_.end())
            deferred_.push_back(sc);
        return false;
    }

    /// Call after a frontend tick that emulated AT LEAST ONE frame. Appends to
    /// `releases` every scancode whose deferred release is now due; the caller
    /// clears each of them in the matrix.
    ///
    /// A tick that emulated nothing (debugger paused, an audio-pacer skip) must
    /// NOT call this: no frame looked at the matrix, so the hold has not been
    /// honoured yet and dropping it would reinstate the very race this exists
    /// to close.
    void on_frames_ran(std::vector<int>& releases)
    {
        releases.insert(releases.end(), deferred_.begin(), deferred_.end());
        deferred_.clear();
        // Every key still held has now spanned a frame, so its eventual release
        // is immediate. This is what keeps the latch a one-frame minimum rather
        // than a permanent one-frame lag on every key-up.
        fresh_.reset();
    }

    /// True while at least one release is being held back (diagnostics/tests).
    bool has_deferred() const { return !deferred_.empty(); }

    /// Drop all latch state. Used on cold boot, where the Keyboard is
    /// reconstructed with an all-released matrix and a pending release would
    /// refer to a key that no longer exists.
    void reset()
    {
        deferred_.clear();
        fresh_.reset();
    }

private:
    static bool in_range(int sc) { return sc >= 0 && sc < MAX_KEYS; }

    /// Pressed, with no emulated frame since. Cleared wholesale by
    /// on_frames_ran().
    std::bitset<MAX_KEYS> fresh_;
    /// Releases held back, in arrival order. Bounded in practice by the number
    /// of keys a human can press inside one frame gap.
    std::vector<int>      deferred_;
};

/// The frontend GLUE — the part that actually matters, made testable.
///
/// WHY THIS IS A CLASS AND NOT FIFTEEN LINES INSIDE QtApp. The Latch above is a
/// pure policy object and its suite proves the policy. What such a suite CANNOT
/// reach is the code that CONNECTS it: which Latch method a key event calls,
/// whether the release path honours the returned bool, whether the discharge
/// loop exists at all, and whether it is gated on the tick having emulated
/// anything. Left in the frontend, all four are reachable only through a live
/// QApplication + Emulator + SDL audio device — i.e. by nothing.
///
/// That gap is not hypothetical in THIS codebase. See the header of
/// test/platform/frame_sequencer_test.cpp: at v0.98.47 a wiring mutation
/// (constructing a fresh audio_pacing::BandState per tick instead of persisting
/// it) survived 5000+ unit rows, the FUSE suite and the entire screenshot
/// regression, and was caught only by a human watching a live cadence log. The
/// policy header was correct throughout; the glue around it was not. This is
/// the same surface, so it gets the same treatment: the ordering and the
/// dispatch move into a named unit a test can drive.
///
/// SINK CONCEPT. `Sink` provides `set_key(Scancode, bool)` — satisfied by
/// `Keyboard` in production and by a recording double in the suite. `Scancode`
/// is the sink's own scancode type (`SDL_Scancode` in production, `int` in
/// tests); it is templated rather than fixed so no adapter shim is needed at
/// the call site, since an untested adapter would reintroduce exactly the
/// unverified glue this class exists to remove.
///
/// ---------------------------------------------------------------------------
/// SERIALISATION (GitHub issue #268) — the second half of the policy.
///
/// The Latch decides WHEN A RELEASE MAY LAND. The Router decides WHEN A PRESS
/// MAY LAND, and that is what stops two keystrokes from being fused into a
/// chord the host never produced (see the header comment above for the two
/// reported symptoms). Host events are held in a FIFO and NEVER reordered; an
/// event is applied the moment it is permitted and otherwise queues, taking
/// everything behind it with it.
///
///   PRESS GATE — a press may be applied iff ALL of:
///     (a) the queue is empty                     — ordering;
///     (b) no release is deferred by the Latch    — the host has let go of a
///         key we are still holding, so anything pressed after it is a
///         SEPARATE keystroke;
///     (c) no NON-MODIFIER press has been applied since the last emulated
///         frame — the keystroke in flight has not been shown yet.
///
///   RELEASE GATE — unchanged from issue #120: deferred iff no frame has run
///     since the press.
///
///   on_tick_end(frames>0) — discharge the Latch, clear (c), then drain the
///     queue from the head while the gates permit, stopping at the first
///     event that does not.
///
/// WHAT DISTINGUISHES A MODIFIER CHORD FROM TWO TAPS, and it is not a timer.
/// It is condition (b), driven by the host's own key-up:
///
///   `v SYM  v P  ...`   SYM's key-up has not arrived, so SYM is physically
///                       down when P's press arrives. (b) holds, (c) holds
///                       (SYM is a modifier and does not set it), so both land
///                       in ONE frame -> `"`. The chord is preserved.
///
///   `v SYM  ^ SYM  v P` SYM's key-up arrived FIRST and was deferred by the
///                       Latch, so (b) fails and P queues. Frame N sees SYM
///                       alone (which types nothing, exactly as a real tap of
///                       it does); the drain then releases SYM and applies P;
///                       frame N+1 sees P alone -> PRINT.
///
/// The boundary is therefore exact and stateless: the instant the host
/// delivers key-up for a shift, that shift stops being a companion for any
/// later press. The asymmetric case `v P  v SYM` is staggered across two
/// frames rather than joined — nobody types a shifted character that way, and
/// both frames show a state the host genuinely passed through.
///
/// PRICE, STATED PLAINLY. Two presses landing in the SAME inter-frame gap are
/// staggered by one frame even when the user meant them simultaneously — up
/// and fire hit within one gap, say. One frame per extra key (20 ms at
/// 50 FPS), keys ACCUMULATE rather than replace, and nothing is lost. It is
/// unavoidable in either direction: when the second press arrives nothing
/// distinguishes rollover-while-typing from two fingers at once, and NOT
/// staggering is precisely what loses both characters today.
///
/// SIDE EFFECT ON THE LATCH, scoped precisely — the branch is NOT dead.
/// Gate (b) makes `Latch::on_press`'s "a re-press cancels a pending release"
/// branch unreachable along the GATED path: a press can no longer be applied
/// while a release is deferred. It is still reachable through `enqueue`'s
/// OVERFLOW FLUSH, which bypasses the gates by design — press A, release A
/// with no frame (so the release sits in `Latch::deferred_`), re-press A,
/// which now queues, then overflow, and the flush calls `apply_press(A)` with
/// A still deferred. That is correct behaviour there: the flush replays the
/// complete event list in order and lands on the host's true key state either
/// way.
///
/// SER-12 traverses that path but does NOT pin this branch — measured by
/// deleting the cancel, which leaves SER-12 green and fails only HKL-08a/b.
/// So the branch's behaviour is pinned at the `Latch` tier, where it belongs:
/// `Latch` is an independent policy object with its own contract and this
/// Router is one client of it.
template <typename Sink, typename Scancode>
class Router {
public:
    /// Bind (or re-bind) the key sink. Called at startup and again on every
    /// cold boot, which reconstructs the Keyboard with an all-released matrix —
    /// hence the latch reset: a release still pending refers to a key of a
    /// machine that no longer exists.
    void attach(Sink& sink)
    {
        sink_ = &sink;
        latch_.reset();
        pending_.clear();
        host_down_.reset();
        sink_down_.reset();
        key_awaiting_frame_ = false;
    }

    /// One host key event. Applied at once when the gates permit, queued
    /// otherwise — see the SERIALISATION block above for the gates.
    void on_host_key(Scancode sc, bool pressed)
    {
        if (!sink_) return;                       // events before attach()
        const int i = static_cast<int>(sc);

        if (in_range(i)) {
            // AUTOREPEAT IS NOT A KEYSTROKE. The host says a key it already
            // holds went down again; the guest's matrix bit is already set, so
            // re-applying it is a no-op — but QUEUING it would not be. Qt
            // filters repeats itself (main_window.cpp:2250); SDL does NOT
            // (sdl_input.cpp:12 forwards every SDL_EVENT_KEY_DOWN, repeat flag
            // and all), so a key held down at 18 FPS would enqueue repeats
            // faster than they drain and stay pressed for seconds after the
            // user let go. Dropped here rather than in one frontend, so both
            // get it and a test can reach it.
            if (pressed && host_down_.test(static_cast<size_t>(i))) return;
            host_down_.set(static_cast<size_t>(i), pressed);
        }

        // Ordering is absolute: once anything is queued, everything queues.
        if (!pending_.empty()) { enqueue(i, pressed); return; }

        if (pressed) {
            if (!gates_open()) { enqueue(i, true); return; }
            apply_press(i);
        } else {
            apply_release(i);
        }
    }

    /// The host can no longer see this window's key-ups — release EVERYTHING
    /// the sink is holding (GitHub issue #268, third symptom).
    ///
    /// WHY A KEY-UP CAN GO MISSING AT ALL. A Qt popup (a menu, a combo
    /// drop-down) and a focus change both take the keyboard away mid-keystroke,
    /// and the key-up that follows is delivered to whatever took it, never to
    /// the emulator window. Measured on the product: opening the File menu with
    /// Alt+F consumes both the `F` key-up AND the `Alt` key-up, so the guest is
    /// left with LALT asserted forever — after which `Keyboard::set_key` reads
    /// every later E/G/C as the ALT variant (EDIT / GRAPH / CAPS LOCK,
    /// keyboard.cpp:248-250) and those letters silently stop appearing, while
    /// every other letter still types. That is exactly the reported
    /// "press A I got A, press G nothing is shown".
    ///
    /// The invariant this restores: jnext never holds a guest key down whose
    /// release it cannot observe. Anything the host really still holds is
    /// re-asserted by its next key-down, because `host_down_` is cleared too.
    ///
    /// PRICE, STATED PLAINLY: a keystroke pressed but not yet shown to a frame
    /// is dropped rather than held, so the #120 minimum-hold does not apply
    /// across a focus loss. That is the right trade — the user has moved to a
    /// menu, so the keystroke was not for the guest, and the alternative is a
    /// key stuck down for the rest of the session.
    void release_all()
    {
        if (!sink_) return;
        pending_.clear();
        for (std::size_t i = 0; i < MAX_KEYS; ++i) {
            if (!sink_down_.test(i)) continue;
            sink_->set_key(static_cast<Scancode>(i), false);
        }
        sink_down_.reset();
        latch_.reset();
        host_down_.reset();
        key_awaiting_frame_ = false;
    }

    /// End of a frontend tick. `frames_rendered` is the number of
    /// Emulator::run_frame() calls the tick made; a tick that emulated NOTHING
    /// (debugger paused, audio-pacer skip) must not discharge, because no frame
    /// has looked at the matrix yet — and for the same reason must not drain,
    /// or the queued keystroke would land on top of the one still waiting to be
    /// shown, which is the very defect being fixed.
    void on_tick_end(int frames_rendered)
    {
        if (!sink_ || frames_rendered <= 0) return;
        due_.clear();
        latch_.on_frames_ran(due_);
        for (int sc : due_) {
            sink_->set_key(static_cast<Scancode>(sc), false);
            mark_sink(sc, false);
        }
        key_awaiting_frame_ = false;
        drain();
    }

    /// Latch state, for diagnostics and tests.
    const Latch& latch() const { return latch_; }

    /// Number of host events waiting for a frame (diagnostics/tests).
    std::size_t pending() const { return pending_.size(); }

private:
    struct Event {
        int  sc;
        bool pressed;
    };

    static bool in_range(int sc) { return sc >= 0 && sc < MAX_KEYS; }

    /// Gates (b) and (c). Gate (a) — the queue being empty — is checked by the
    /// caller, because the drain deliberately violates it: it IS the head.
    bool gates_open() const
    {
        return !latch_.has_deferred() && !key_awaiting_frame_;
    }

    /// Record what the SINK is holding, so release_all() can undo exactly that
    /// and nothing else. Releasing a key the sink never had would be wrong, not
    /// merely wasteful: the three `s_compound[]` keys write TWO matrix bits
    /// directly — `/` is Symbol Shift + V, `-` is SS + J, `=` is SS + L
    /// (keyboard.cpp:280-282) — and all three share the Symbol Shift cell
    /// {7,1}, so clearing an unpressed `/` would clear a Symbol Shift the user
    /// really is holding. (DELETE is NOT an example: it goes through the
    /// extended-key register and is folded against Caps Shift only at
    /// read_rows() time, keyboard.cpp:227.)
    void mark_sink(int sc, bool down)
    {
        if (!in_range(sc)) return;
        sink_down_.set(static_cast<std::size_t>(sc), down);
    }

    void apply_press(int sc)
    {
        latch_.on_press(sc);
        sink_->set_key(static_cast<Scancode>(sc), true);
        mark_sink(sc, true);
        // Gate (c). A modifier never starts a keystroke group, so it does not
        // arm it — that is what keeps SYM+P in one frame.
        if (!is_modifier(sc)) key_awaiting_frame_ = true;
    }

    /// Returns false when the Latch deferred it, i.e. the release has NOT
    /// landed. The drain needs that answer (nothing behind an undelivered
    /// release may pass it); the arrival path ignores it. Shared rather than
    /// written out twice so the two callers cannot drift.
    bool apply_release(int sc)
    {
        if (!latch_.on_release(sc)) return false;
        sink_->set_key(static_cast<Scancode>(sc), false);
        mark_sink(sc, false);
        return true;
    }

    void enqueue(int sc, bool pressed)
    {
        pending_.push_back({sc, pressed});
        if (pending_.size() <= MAX_PENDING) return;

        // OVERFLOW. Apply everything, in order, ignoring the gates, and start
        // again empty. Replaying the COMPLETE event list in order lands on
        // exactly the host's current key state, so this can never strand a key
        // down; any release the Latch still defers is discharged by the next
        // tick that emulates a frame. It degrades to the pre-#268 behaviour for
        // this one batch and nothing worse, and reaching it at all needs the
        // emulator to have stopped running frames entirely (see MAX_PENDING).
        for (const Event& ev : pending_) {
            if (ev.pressed) apply_press(ev.sc);
            else            apply_release(ev.sc);
        }
        pending_.clear();
    }

    /// Head-first, stopping at the first event that cannot land yet. A drain
    /// legitimately applies SEVERAL events — releasing the keystroke that has
    /// been seen and applying the next one are both needed before the next
    /// frame — but at most one press, because that press arms gate (c).
    void drain()
    {
        while (!pending_.empty()) {
            const Event ev = pending_.front();
            if (ev.pressed) {
                if (!gates_open()) break;
                pending_.pop_front();
                apply_press(ev.sc);
            } else {
                pending_.pop_front();
                // A release the Latch defers has not landed, so nothing behind
                // it may pass it.
                if (!apply_release(ev.sc)) break;
            }
        }
    }

    Sink*            sink_ = nullptr;
    Latch            latch_;
    /// Discharge scratch, a member so the per-tick path does not allocate in
    /// steady state.
    std::vector<int> due_;
    /// Host events that could not land yet, in arrival order.
    std::deque<Event> pending_;
    /// Scancodes the HOST currently holds — press seen, key-up not. Maintained
    /// at ARRIVAL, not at application, because it answers a question about the
    /// user's fingers, not about the matrix.
    std::bitset<MAX_KEYS> host_down_;
    /// Scancodes the SINK currently has asserted — maintained at APPLICATION,
    /// which is the complement of `host_down_`: it answers what the guest
    /// matrix holds. The two differ whenever the queue or the Latch is holding
    /// something. Only release_all() reads it.
    std::bitset<MAX_KEYS> sink_down_;
    /// Gate (c): a non-modifier press has been applied that no emulated frame
    /// has sampled yet.
    bool key_awaiting_frame_ = false;
};

}  // namespace host_key_latch
