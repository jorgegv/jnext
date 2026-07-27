#pragma once

#include <algorithm>
#include <bitset>
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
/// MERGED TAPS ARE A KNOWN LIMITATION, NOT HARDWARE EQUIVALENCE. Two complete
/// taps of the same key inside one frame gap collapse into one continuous
/// press, and one of the two keystrokes is still lost.
///
/// Do NOT justify that by appeal to the hardware: it is NOT what a real Next
/// does. `membrane.vhd:99-155` is a bare 9-state one-hot row scanner clocked by
/// i_CLK/i_CLK_EN with NO debounce counter and NO interrupt gating — the only
/// hold in the file is the deliberate two-scan CS/SYM shift hysteresis at
/// :178, which EXTENDS a release rather than merging taps. Machine code
/// polling port 0xFE fast enough therefore genuinely can observe two rapid
/// taps on real hardware, and nothing in the VHDL merges them.
///
/// The merge is an artefact of THIS EMULATOR's host-input delivery
/// granularity: jnext hands host key events to the guest at frame boundaries,
/// so a gap can carry at most one press-release edge pair. Resolving it would
/// need host-side input timestamping plus sub-frame injection into the running
/// frame — a different and much larger change, deliberately out of scope here.
///
/// It is nevertheless a strict improvement on what it replaces: before the
/// latch BOTH taps were lost (the matrix returned to released with no frame in
/// between); after it, one press-release pair is delivered. Half recovered
/// beats none, and the residue is documented rather than dressed up.
namespace host_key_latch {

/// SDL_NUM_SCANCODES. Hard-coded rather than included so this header stays
/// pure (no SDL dependency) and unit-testable on its own; the wiring in
/// src/gui/qt_app.cpp is what feeds it real SDL_Scancode values, and anything
/// out of range is passed through unlatched.
inline constexpr int MAX_KEYS = 512;

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
    }

    /// One host key event. A press is applied at once; a release is applied
    /// only if the latch says the press has already been seen by a frame.
    void on_host_key(Scancode sc, bool pressed)
    {
        if (!sink_) return;                       // events before attach()
        if (pressed) {
            latch_.on_press(static_cast<int>(sc));
            sink_->set_key(sc, true);
        } else if (latch_.on_release(static_cast<int>(sc))) {
            sink_->set_key(sc, false);
        }
    }

    /// End of a frontend tick. `frames_rendered` is the number of
    /// Emulator::run_frame() calls the tick made; a tick that emulated NOTHING
    /// (debugger paused, audio-pacer skip) must not discharge, because no frame
    /// has looked at the matrix yet.
    void on_tick_end(int frames_rendered)
    {
        if (!sink_ || frames_rendered <= 0) return;
        due_.clear();
        latch_.on_frames_ran(due_);
        for (int sc : due_) sink_->set_key(static_cast<Scancode>(sc), false);
    }

    /// Latch state, for diagnostics and tests.
    const Latch& latch() const { return latch_; }

private:
    Sink*            sink_ = nullptr;
    Latch            latch_;
    /// Discharge scratch, a member so the per-tick path does not allocate in
    /// steady state.
    std::vector<int> due_;
};

}  // namespace host_key_latch
