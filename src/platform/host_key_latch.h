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
/// MERGED TAPS ARE CORRECT. Two complete taps of the same key inside one frame
/// gap collapse into one continuous press. That is what real hardware does too:
/// the ROM's KEY-SCAN runs from the 50 Hz interrupt, so two taps 10 ms apart are
/// indistinguishable there as well. Preserving them would require queueing
/// keystrokes the machine has no way to consume.
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

}  // namespace host_key_latch
