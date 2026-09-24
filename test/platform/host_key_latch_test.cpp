// Host key minimum-hold latch + serialiser test (GitHub issues #120, #268).
//
// No VHDL oracle: this is a HOST input-sampling policy, not emulated hardware.
// Its oracle is the stated contract in src/platform/host_key_latch.h, which in
// turn derives from one fact about the frontend and one about the hardware:
//
//   * FRONTEND — Qt key callbacks and the frame timer are dispatched by the
//     same event loop, so a key event can only land BETWEEN two run_frame()
//     calls. The guest's only view of the matrix is inside run_frame(). The
//     matrix is therefore sampled exactly once per emulated frame.
//   * HARDWARE — the FPGA membrane scanner runs at pixel-clock rate
//     (membrane.vhd), so a real press outlasts many scans and can never be
//     missed. The one-frame sampling is purely an emulation artefact.
//
// The LATCH contract under test (HKL-*, unchanged by #268):
//
//   * a press is NEVER delayed;
//   * a release is deferred if and only if no frame has run since the press;
//   * a deferred release is delivered by the first tick that emulates a frame;
//   * once a frame has run, releases pass through unchanged — the latch must
//     not become a permanent one-frame lag on every key-up;
//   * a re-press cancels a pending release;
//   * a tick that emulated nothing must not discharge the latch.
//
// The ROUTER SERIALISER contract (SER-*, issue #268). "A press is never
// delayed" is a statement about the Latch and stops being one about the Router:
//
//   * a press is applied only when the queue is empty, no release is deferred,
//     and no non-modifier press is still waiting to be shown to a frame;
//     otherwise it QUEUES, and everything behind it queues too;
//   * the guest must therefore never sample a two-key state the host did not
//     have, which is the whole of #268 — two taps fused into a chord that the
//     ZX ROM either rejects (both characters lost) or decodes as a DIFFERENT
//     character (SYM tap + P tap typed `"` instead of PRINT);
//   * the SDL modifier block 224..231 — CAPS SHIFT, SYMBOL SHIFT and host Alt —
//     never starts a keystroke group, so a genuine shift+key chord still
//     reaches the guest in ONE frame. This is the regression surface: get it
//     wrong and EVERY shifted character breaks;
//   * what separates "shift held as a modifier" from "shift tapped on its own"
//     is the host's key-up, and nothing else;
//   * autorepeat is not a keystroke and never enters the queue.
//
// WHY A WHOLE-MATRIX FAKE GUEST (MatrixGuest below). The pre-#268 fixtures
// track exactly ONE key, so they cannot express a chord at all — that is
// structurally why no row caught this. #268 needs two keys down at once to be
// OBSERVABLE, so the SER-* rows sample the entire down-set once per emulated
// frame and assert on the sequence of states the guest actually saw.
//
// TWO LAYERS, DELIBERATELY. The HKL-* rows pin the Latch POLICY. The RT-* rows
// pin the Router — the GLUE that connects it to the emulated keyboard — because
// a green policy suite over unverified wiring is the exact shape of the
// v0.98.47 regression documented in test/platform/frame_sequencer_test.cpp,
// which survived 5000+ unit rows, the FUSE suite and the whole screenshot
// regression. The RT-* rows assert against the SINK (what the keyboard was
// actually told), not against latch internals, and each is written to fail on
// one specific mis-wiring: the wrong Latch method for an event, a release path
// that ignores the returned bool, a missing discharge loop, or a discharge
// mis-gated on frames_rendered.
//
// Every row derives from that contract, never from reading the implementation
// back.
//
// Run: ./build/test/host_key_latch_test

#include "platform/host_key_latch.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string got(const std::vector<int>& v)
{
    std::string s = "{";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += std::to_string(v[i]);
    }
    return s + "}";
}

// Two arbitrary in-range scancodes (SDL_SCANCODE_A = 4, SDL_SCANCODE_B = 5).
constexpr int KEY_A = 4;
constexpr int KEY_B = 5;

// --- Router fixtures ------------------------------------------------------
// A recording stand-in for Keyboard. The Router is templated on the sink and
// on the scancode type precisely so this double needs no production header:
// the suite still builds with no Qt, no SDL and no emulator core.
using Calls = std::vector<std::pair<int, bool>>;

struct FakeKeyboard {
    Calls calls;
    void set_key(int sc, bool pressed) { calls.emplace_back(sc, pressed); }
    int count(int sc, bool pressed) const
    {
        int n = 0;
        for (const auto& c : calls)
            if (c.first == sc && c.second == pressed) ++n;
        return n;
    }
};

using TestRouter = host_key_latch::Router<FakeKeyboard, int>;

std::string got(const FakeKeyboard& kb)
{
    std::string s = "sink=[";
    for (size_t i = 0; i < kb.calls.size(); ++i) {
        if (i) s += " ";
        s += std::to_string(kb.calls[i].first);
        s += kb.calls[i].second ? "v" : "^";
    }
    return s + "]";
}

/// Drive one frontend tick that emulated at least one frame, returning the
/// releases it discharged.
std::vector<int> frame_tick(host_key_latch::Latch& l)
{
    std::vector<int> out;
    l.on_frames_ran(out);
    return out;
}

// --- SDL frontend fixtures (issue #122) -----------------------------------
// A sink that also models the GUEST. The emulated machine can only observe the
// matrix from inside run_frame() (a port 0xFE read), so the sink's state
// BETWEEN frames is invisible to it. This records the state each emulated
// frame would have sampled, which is what issue #122 is actually about: "the
// key was set and cleared" and "the key was seen" are different statements,
// and the whole defect is that the first happened without the second.
struct GuestView {
    FakeKeyboard      kb;
    /// One entry per emulated frame: was KEY_A down when that frame sampled?
    std::vector<bool> samples;
    bool              down = false;

    void set_key(int sc, bool pressed)
    {
        kb.set_key(sc, pressed);
        if (sc == KEY_A) down = pressed;
    }
    /// One Emulator::run_frame() — the guest's single look at the matrix.
    void run_frame() { samples.push_back(down); }

    int frames_sampling_down() const
    {
        int n = 0;
        for (bool s : samples)
            if (s) ++n;
        return n;
    }
};

using GuestRouter = host_key_latch::Router<GuestView, int>;

// --- Whole-matrix fixtures (issue #268) -----------------------------------
// GuestView above tracks ONE key, which is exactly why nothing caught #268: a
// chord needs two. This sink tracks the WHOLE down-set and snapshots it once
// per emulated frame — the guest's real view, since the matrix is constant for
// the duration of a run_frame() (host events can only land between ticks).
struct MatrixGuest {
    FakeKeyboard          kb;
    std::set<int>         down;
    /// One sorted down-set per emulated frame.
    std::vector<std::vector<int>> samples;

    void set_key(int sc, bool pressed)
    {
        kb.set_key(sc, pressed);
        if (pressed) down.insert(sc);
        else         down.erase(sc);
    }
    void run_frame() { samples.emplace_back(down.begin(), down.end()); }

    /// The frames in which the guest saw ANY key — i.e. what it was offered to
    /// decode, with the idle frames between keystrokes dropped.
    std::vector<std::vector<int>> keystrokes() const
    {
        std::vector<std::vector<int>> out;
        for (const auto& s : samples)
            if (!s.empty()) out.push_back(s);
        return out;
    }
    /// True if any single frame sampled BOTH of these keys down — the defect.
    bool ever_together(int a, int b) const
    {
        for (const auto& s : samples) {
            const bool ha = std::find(s.begin(), s.end(), a) != s.end();
            const bool hb = std::find(s.begin(), s.end(), b) != s.end();
            if (ha && hb) return true;
        }
        return false;
    }
};

using MatrixRouter = host_key_latch::Router<MatrixGuest, int>;

std::string got(const std::vector<std::vector<int>>& seq)
{
    std::string s = "frames=[";
    for (size_t i = 0; i < seq.size(); ++i) {
        if (i) s += " ";
        s += "{";
        for (size_t j = 0; j < seq[i].size(); ++j) {
            if (j) s += ",";
            s += std::to_string(seq[i][j]);
        }
        s += "}";
    }
    return s + "]";
}

/// One frontend tick: the gap's host events, then the frames, then the
/// discharge+drain — the order both frontends really use (sdl_app.cpp:228/276/394,
/// qt_app.cpp:279 then TickEffects::post_frames).
void mtick(MatrixRouter& r, MatrixGuest& g, const Calls& batch, int frames)
{
    for (const auto& e : batch) r.on_host_key(e.first, e.second);
    for (int i = 0; i < frames; ++i) g.run_frame();
    r.on_tick_end(frames);
}

/// Run `ticks` further empty ticks, to let a queue drain out.
void msettle(MatrixRouter& r, MatrixGuest& g, int ticks)
{
    for (int i = 0; i < ticks; ++i) mtick(r, g, Calls{}, 1);
}

/// One complete tap: press then release, back to back, inside one gap.
Calls tap(int sc) { return Calls{{sc, true}, {sc, false}}; }

Calls operator+(Calls a, const Calls& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// Scancodes used by the SER-* rows. SDL3 values, SDL_scancode.h:
//   modifier block 224..231 (LCTRL..RGUI, contiguous — the same block
//   host_key_latch.h hard-codes and src/input/keyboard.cpp static_asserts).
constexpr int SC_SYM   = 224;   // SDL_SCANCODE_LCTRL  -> SYMBOL SHIFT (7,1)
constexpr int SC_CAPS  = 225;   // SDL_SCANCODE_LSHIFT -> CAPS SHIFT   (0,0)
constexpr int SC_ALT   = 226;   // SDL_SCANCODE_LALT   -> host Alt modifier
constexpr int SC_P     = 19;    // SDL_SCANCODE_P      -> (5,0)
constexpr int SC_M     = 16;    // SDL_SCANCODE_M      -> (7,2)
constexpr int SC_N     = 17;    // SDL_SCANCODE_N      -> (7,3)
constexpr int SC_O     = 18;    // SDL_SCANCODE_O      -> (5,1)
constexpr int SC_1     = 30;    // SDL_SCANCODE_1      -> (3,0)
constexpr int SC_2     = 31;    // SDL_SCANCODE_2      -> (3,1)
constexpr int SC_3     = 32;    // SDL_SCANCODE_3      -> (3,2)
constexpr int SC_4     = 33;    // SDL_SCANCODE_4      -> (3,3)
constexpr int SC_ENTER = 40;    // SDL_SCANCODE_RETURN -> (6,0)
constexpr int SC_DOWN  = 81;    // SDL_SCANCODE_DOWN   -> extended key DOWN
constexpr int SC_UP    = 82;    // SDL_SCANCODE_UP     -> extended key UP

/// One SdlApp::run() iteration, in its real order: SDL_PollEvent() drains the
/// whole host event queue first (sdl_app.cpp:228), then the audio pacer's
/// frame count is emulated (sdl_app.cpp:276-287), then the tick discharges
/// (sdl_app.cpp:292). `frames` is what audio_pacing::frames_for_tick()
/// returned — legitimately 0 when the device queue is ahead, 2 when catching
/// up.
void sdl_tick(GuestRouter& r, GuestView& g, const Calls& poll_batch, int frames)
{
    for (const auto& e : poll_batch) r.on_host_key(e.first, e.second);
    for (int i = 0; i < frames; ++i) g.run_frame();
    r.on_tick_end(frames);
}

}  // namespace

int main()
{
    using host_key_latch::Latch;

    std::printf("Host key latch + serialiser test (GitHub issues #120, #268)\n");
    std::printf("====================================================\n");

    // --- HKL-01: a press is never delayed ----------------------------------
    {
        Latch l;
        check("HKL-01", "on_press always asserts the key immediately",
              l.on_press(KEY_A));
    }

    // --- HKL-02: THE DEFECT — press and release inside one frame gap -------
    // Without the latch this pair sets the matrix bit and clears it again with
    // no frame in between, and the guest never sees the key. The release must
    // be held back.
    {
        Latch l;
        l.on_press(KEY_A);
        check("HKL-02a", "a release with no frame since the press is deferred",
              !l.on_release(KEY_A));
        check("HKL-02b", "the latch reports the pending release",
              l.has_deferred());
    }

    // --- HKL-03: the deferred release is delivered by the next frame -------
    // The key must come up again — a hold that never ends would jam the key
    // down, which is a worse bug than the one being fixed.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        const auto due = frame_tick(l);
        check("HKL-03a", "the first tick that emulates a frame discharges it",
              due.size() == 1 && due[0] == KEY_A, got(due));
        check("HKL-03b", "nothing remains pending afterwards", !l.has_deferred());
    }

    // --- HKL-04: the hold lasts exactly one frame, not more ----------------
    // A second frame must not re-deliver the release.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        frame_tick(l);
        const auto second = frame_tick(l);
        check("HKL-04", "a release is discharged once, not on every frame",
              second.empty(), got(second));
    }

    // --- HKL-05: the normal path is untouched ------------------------------
    // A press that has already spanned a frame releases immediately, in the
    // same call. This is the case for every ordinary human keystroke, and the
    // latch must be a no-op there or it would add a frame of input lag to
    // everything.
    {
        Latch l;
        l.on_press(KEY_A);
        frame_tick(l);
        check("HKL-05a", "a release after a frame passes straight through",
              l.on_release(KEY_A));
        check("HKL-05b", "and defers nothing", !l.has_deferred());
    }

    // --- HKL-06: a held key never becomes deferred, however long -----------
    // Ten frames of a key held down, then a release: still immediate. Pins that
    // on_frames_ran() clears the freshness of keys that are STILL DOWN, not
    // just of those being released.
    {
        Latch l;
        l.on_press(KEY_A);
        for (int i = 0; i < 10; ++i) frame_tick(l);
        check("HKL-06", "a long-held key releases immediately",
              l.on_release(KEY_A) && !l.has_deferred());
    }

    // --- HKL-07: a stray release passes through ----------------------------
    // The key-up half of a chord pressed before the window had focus arrives
    // with no matching press. Deferring it would hold back a release for a key
    // that was never down.
    {
        Latch l;
        check("HKL-07", "a release with no preceding press is immediate",
              l.on_release(KEY_A) && !l.has_deferred());
    }

    // --- HKL-08: a re-press cancels the pending release --------------------
    // The key is physically down again; releasing it at the next frame would be
    // a lie. The two taps merge into one press. That merge is a documented
    // LIMITATION of jnext's frame-granular host-input delivery, NOT hardware
    // behaviour — membrane.vhd:99-155 has no debounce and no interrupt gating,
    // so real hardware can resolve two rapid taps. See the header.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);          // deferred
        l.on_press(KEY_A);            // down again before any frame
        check("HKL-08a", "a re-press clears the pending release",
              !l.has_deferred());
        const auto due = frame_tick(l);
        check("HKL-08b", "and no release is emitted at the next frame",
              due.empty(), got(due));
        // That frame ran with the key down, so the merged press HAS been seen:
        // its release is now immediate, exactly like any ordinary keystroke.
        check("HKL-08c", "the merged press ends normally once a frame has run",
              l.on_release(KEY_A) && !l.has_deferred());
    }

    // --- HKL-08d: a merged press with NO frame at all is still latched ------
    // Two taps inside one gap collapse into one press, and that press has still
    // not been seen — so its release must be deferred like any other.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        l.on_press(KEY_A);
        check("HKL-08d", "a merged press with no frame between defers its release",
              !l.on_release(KEY_A) && l.has_deferred());
        const auto due = frame_tick(l);
        check("HKL-08e", "and the merge discharges exactly one release",
              due.size() == 1 && due[0] == KEY_A, got(due));
    }

    // --- HKL-09: independent keys do not interfere -------------------------
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_press(KEY_B);
        l.on_release(KEY_A);
        l.on_release(KEY_B);
        const auto due = frame_tick(l);
        check("HKL-09", "both keys deferred in one gap are both discharged",
              due.size() == 2 &&
              ((due[0] == KEY_A && due[1] == KEY_B) ||
               (due[0] == KEY_B && due[1] == KEY_A)), got(due));
    }

    // --- HKL-10: a repeated release does not queue twice -------------------
    // Duplicate key-up events (seen when a host hotkey handler and the normal
    // path both forward the same event) must not produce two clears.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        l.on_release(KEY_A);
        const auto due = frame_tick(l);
        check("HKL-10", "a duplicate release is not queued twice",
              due.size() == 1 && due[0] == KEY_A, got(due));
    }

    // --- HKL-11: a tick that emulated nothing must not discharge -----------
    // A paused debugger runs no frames, so the guest has still not looked at
    // the matrix. The caller is contracted not to call on_frames_ran() then;
    // this row pins that the hold genuinely survives such ticks, i.e. the state
    // lives in the latch and not in the caller's loop.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        check("HKL-11a", "the hold survives ticks that emulate nothing",
              l.has_deferred());
        const auto due = frame_tick(l);
        check("HKL-11b", "and is discharged by the first tick that does",
              due.size() == 1 && due[0] == KEY_A, got(due));
    }

    // --- HKL-12: out-of-range scancodes pass through unlatched -------------
    // The frontend feeds real SDL_Scancode values, but an unmapped or negative
    // code must never index the bitset.
    {
        Latch l;
        check("HKL-12a", "a negative scancode presses through",  l.on_press(-1));
        check("HKL-12b", "a negative scancode releases through", l.on_release(-1));
        check("HKL-12c", "an over-range scancode presses through",
              l.on_press(host_key_latch::MAX_KEYS));
        check("HKL-12d", "an over-range scancode releases through",
              l.on_release(host_key_latch::MAX_KEYS));
        check("HKL-12e", "and nothing is deferred for them", !l.has_deferred());
    }

    // --- HKL-13: reset() drops everything ----------------------------------
    // Cold boot reconstructs the Keyboard with an all-released matrix; a
    // surviving pending release would clear a bit belonging to a machine that
    // no longer exists.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        l.reset();
        check("HKL-13a", "reset clears pending releases", !l.has_deferred());
        const auto due = frame_tick(l);
        check("HKL-13b", "and none are emitted afterwards", due.empty(), got(due));
        check("HKL-13c", "reset also clears freshness, so a stray release passes",
              l.on_release(KEY_A));
    }

    // --- HKL-14: many taps across many frames stay in step -----------------
    // A soak over the realistic pattern (every tap shorter than a frame gap):
    // each tap must produce exactly one press and one release, and the latch
    // must never accumulate.
    {
        Latch l;
        int  discharged = 0;
        bool always_deferred = true;
        for (int i = 0; i < 200; ++i) {
            l.on_press(KEY_A);
            if (l.on_release(KEY_A)) always_deferred = false;
            discharged += static_cast<int>(frame_tick(l).size());
        }
        check("HKL-14a", "every sub-frame tap is deferred", always_deferred);
        check("HKL-14b", "200 taps discharge exactly 200 releases",
              discharged == 200, "discharged=" + std::to_string(discharged));
        check("HKL-14c", "no state accumulates", !l.has_deferred());
    }

    // --- HKL-15: alternating fast and slow taps -----------------------------
    // Mixed traffic: the latch must switch between deferring and passing
    // through without leaking state from one tap into the next.
    {
        Latch l;
        bool ok = true;
        for (int i = 0; i < 50; ++i) {
            // Slow tap: a frame runs while the key is down -> immediate.
            l.on_press(KEY_A);
            frame_tick(l);
            if (!l.on_release(KEY_A)) ok = false;
            if (!frame_tick(l).empty()) ok = false;
            // Fast tap: no frame while down -> deferred, then discharged.
            l.on_press(KEY_A);
            if (l.on_release(KEY_A)) ok = false;
            if (frame_tick(l).size() != 1) ok = false;
        }
        check("HKL-15", "alternating slow and fast taps stay independent", ok);
    }

    // =======================================================================
    // Router — THE GLUE. Rows above prove the policy; these prove the wiring.
    //
    // Every row here is written to fail on one specific mis-wiring that the
    // Latch suite alone cannot see: calling the wrong Latch method for an
    // event, ignoring on_release()'s return value, omitting the discharge loop,
    // or mis-gating the discharge on frames_rendered. Assertions are made
    // against the SINK — what the emulated keyboard was actually told — because
    // that, not the latch's internal state, is what the guest experiences.
    // =======================================================================

    // --- RT-01: a press reaches the keyboard, immediately and exactly once --
    // Fails if the press path calls on_release, or forgets the sink call.
    {
        FakeKeyboard kb; TestRouter r; r.attach(kb);
        r.on_host_key(KEY_A, true);
        check("RT-01", "a press is passed to the keyboard at once",
              kb.calls == Calls{{KEY_A, true}}, got(kb));
    }

    // --- RT-02: a sub-frame release is NOT passed on -------------------------
    // THE DEFECT, at the wiring level. Fails if the release path ignores what
    // on_release() returned and clears the key regardless — which is exactly
    // the pre-fix code, and exactly what a careless edit would restore.
    {
        FakeKeyboard kb; TestRouter r; r.attach(kb);
        r.on_host_key(KEY_A, true);
        r.on_host_key(KEY_A, false);
        check("RT-02", "a release with no frame since the press is withheld",
              kb.calls == Calls{{KEY_A, true}}, got(kb));
    }

    // --- RT-03: the discharge loop exists ------------------------------------
    // Fails outright if on_tick_end() drops the loop, or never calls
    // on_frames_ran() — the key would then stay jammed down forever.
    {
        FakeKeyboard kb; TestRouter r; r.attach(kb);
        r.on_host_key(KEY_A, true);
        r.on_host_key(KEY_A, false);
        r.on_tick_end(1);
        check("RT-03", "a tick that emulated a frame releases the held key",
              kb.calls == Calls{{KEY_A, true}, {KEY_A, false}}, got(kb));
    }

    // --- RT-04: the discharge is gated on frames_rendered --------------------
    // A paused tick must NOT discharge: no frame has looked at the matrix, so
    // releasing there reinstates the race. Fails if the gate is dropped, or
    // inverted, or written `>= 0` instead of `> 0`.
    {
        FakeKeyboard kb; TestRouter r; r.attach(kb);
        r.on_host_key(KEY_A, true);
        r.on_host_key(KEY_A, false);
        r.on_tick_end(0);
        check("RT-04a", "a tick that emulated nothing does not discharge",
              kb.calls == Calls{{KEY_A, true}}, got(kb));
        r.on_tick_end(-1);
        check("RT-04b", "a negative frame count does not discharge either",
              kb.calls == Calls{{KEY_A, true}}, got(kb));
        r.on_tick_end(1);
        check("RT-04c", "the first tick that does emulate discharges it",
              kb.calls == Calls{{KEY_A, true}, {KEY_A, false}}, got(kb));
    }

    // --- RT-05: an ordinary keystroke is completely unaffected ---------------
    // The no-regression row: press, a frame runs, release. The keyboard must
    // see exactly the two calls it saw before this change existed, in order and
    // at the same moments — no added latency, no duplicate.
    {
        FakeKeyboard kb; TestRouter r; r.attach(kb);
        r.on_host_key(KEY_A, true);
        r.on_tick_end(1);
        check("RT-05a", "a frame tick alone tells the keyboard nothing",
              kb.calls == Calls{{KEY_A, true}}, got(kb));
        r.on_host_key(KEY_A, false);
        check("RT-05b", "the release reaches the keyboard in the same call",
              kb.calls == Calls{{KEY_A, true}, {KEY_A, false}}, got(kb));
        r.on_tick_end(1);
        check("RT-05c", "and the next tick adds nothing",
              kb.calls == Calls{{KEY_A, true}, {KEY_A, false}}, got(kb));
    }

    // --- RT-06: a held key is never spuriously released ----------------------
    // Fails if the discharge releases everything it knows about rather than
    // only what was deferred — which would drop keys out from under the user
    // during gameplay, a far worse bug than the one being fixed.
    {
        FakeKeyboard kb; TestRouter r; r.attach(kb);
        r.on_host_key(KEY_A, true);
        for (int i = 0; i < 20; ++i) r.on_tick_end(1);
        check("RT-06", "a key held across 20 frames is never released",
              kb.calls == Calls{{KEY_A, true}}, got(kb));
    }

    // --- RT-07: the scancode survives the round trip -------------------------
    // The deferred release travels through the latch as an int and comes back
    // out cast to the sink's scancode type. Fails on a cast typo or an
    // index/value mix-up — with two keys deferred, a bug that emitted the
    // wrong one would be invisible with a single key.
    {
        FakeKeyboard kb; TestRouter r; r.attach(kb);
        r.on_host_key(KEY_B, true);
        r.on_host_key(KEY_B, false);
        r.on_tick_end(1);
        check("RT-07a", "the deferred release names the key that was pressed",
              kb.calls == Calls{{KEY_B, true}, {KEY_B, false}}, got(kb));

        // TWO TICKS, NOT ONE — changed for issue #268, and the change IS the
        // fix. B is a second non-modifier press inside the gap that A has not
        // been shown in yet, so the router now queues it instead of piling it
        // on top of A: the serialisation costs one more tick before the traffic
        // is complete. The property this row exists for is untouched — both
        // keys are released, once each, by name — and the SER-* rows below
        // pin the ordering itself.
        FakeKeyboard kb2; TestRouter r2; r2.attach(kb2);
        r2.on_host_key(KEY_A, true);
        r2.on_host_key(KEY_B, true);
        r2.on_host_key(KEY_A, false);
        r2.on_host_key(KEY_B, false);
        r2.on_tick_end(1);
        r2.on_tick_end(1);
        check("RT-07b", "two keys deferred together are both released, once each",
              kb2.count(KEY_A, false) == 1 && kb2.count(KEY_B, false) == 1 &&
              kb2.calls.size() == 4, got(kb2));
    }

    // --- RT-08: attach() re-binds and clears -------------------------------
    // Cold boot reconstructs the Keyboard. A release still pending must not be
    // delivered to the NEW machine, and must not be delivered to the old one
    // either. Fails if attach() forgets the reset, or does not re-point the
    // sink.
    {
        FakeKeyboard old_kb, new_kb; TestRouter r; r.attach(old_kb);
        r.on_host_key(KEY_A, true);
        r.on_host_key(KEY_A, false);      // deferred against the old machine
        r.attach(new_kb);                 // cold boot
        r.on_tick_end(1);
        check("RT-08a", "a pending release does not reach the new machine",
              new_kb.calls.empty(), got(new_kb));
        check("RT-08b", "nor is it retro-delivered to the old one",
              old_kb.calls == Calls{{KEY_A, true}}, got(old_kb));
        r.on_host_key(KEY_B, true);
        check("RT-08c", "and later events go to the new machine",
              new_kb.calls == Calls{{KEY_B, true}}, got(new_kb));
    }

    // --- RT-09: events arriving before attach() are DROPPED, not banked ------
    // QtApp registers the Qt key callback before the first attach(), so events
    // can reach the router with no sink bound. "Survives" is not a testable
    // property and must not be asserted as one (the original row here did
    // exactly that — `check(..., true)` — and was caught by
    // test/lint-assertions.sh, which exists because a 2026-04-14 audit found
    // whole subsystems passing on assertions that could not fail).
    //
    // The checkable property is what the machine sees. An unattached router
    // must DISCARD those events, not bank them: a router that queued them
    // would replay a keystroke into the emulated keyboard the moment it was
    // attached — a phantom key at boot, and again after every cold boot, since
    // attach() runs on both paths.
    {
        FakeKeyboard kb;
        TestRouter   r;
        // A full sub-frame tap plus a still-held key, all with no sink bound —
        // i.e. state the latch would be carrying if it had seen them.
        r.on_host_key(KEY_A, true);
        r.on_host_key(KEY_A, false);
        r.on_host_key(KEY_B, true);
        r.on_tick_end(1);

        r.attach(kb);
        check("RT-09a", "attaching does not replay anything from before it",
              kb.calls.empty(), got(kb));
        r.on_tick_end(1);
        check("RT-09b", "nor does the first tick after attaching",
              kb.calls.empty(), got(kb));

        // ...and the router is now indistinguishable from one that was never
        // used. Drive it and a fresh router through the SAME script — one
        // sub-frame tap (deferred) and one ordinary keystroke (immediate) — and
        // require identical sink traffic. This is what "survives" actually
        // means: leaked freshness or a stale deferred entry would make the
        // recovered router treat one of these two differently.
        const auto script = [](TestRouter& rr) {
            rr.on_host_key(KEY_A, true);
            rr.on_host_key(KEY_A, false);   // sub-frame tap -> release deferred
            rr.on_tick_end(1);              // ...and discharged here
            rr.on_host_key(KEY_B, true);
            rr.on_tick_end(1);              // B has now been seen by a frame
            rr.on_host_key(KEY_B, false);   // ordinary keystroke -> immediate
        };
        FakeKeyboard fresh_kb;
        TestRouter   fresh;
        fresh.attach(fresh_kb);
        script(r);
        script(fresh);
        check("RT-09c", "a recovered router matches a never-used one exactly",
              kb.calls == fresh_kb.calls, got(kb) + " vs " + got(fresh_kb));
        // Equality alone could be satisfied by both being wrong in the same
        // way, so pin the absolute traffic too.
        const Calls expected{{KEY_A, true},  {KEY_A, false},
                             {KEY_B, true},  {KEY_B, false}};
        check("RT-09d", "and that shared behaviour is the CORRECT one",
              kb.calls == expected, got(kb));
    }

    // --- RT-10: soak — the realistic pattern, end to end ---------------------
    // 200 sub-frame taps, one per tick: exactly 200 presses and 200 releases,
    // strictly alternating. Catches anything that accumulates, duplicates, or
    // drops one event in N — none of which a handful of scripted rows would.
    {
        FakeKeyboard kb; TestRouter r; r.attach(kb);
        for (int i = 0; i < 200; ++i) {
            r.on_host_key(KEY_A, true);
            r.on_host_key(KEY_A, false);
            r.on_tick_end(1);
        }
        bool alternating = kb.calls.size() == 400;
        for (size_t i = 0; alternating && i < kb.calls.size(); ++i) {
            if (kb.calls[i].first != KEY_A) alternating = false;
            if (kb.calls[i].second != (i % 2 == 0)) alternating = false;
        }
        check("RT-10", "200 sub-frame taps yield 200 clean press/release pairs",
              alternating, "n=" + std::to_string(kb.calls.size()));
    }

    // --- RT-11: the guest-visible invariant, over mixed traffic --------------
    // Whatever the mix of fast taps, slow taps and paused ticks, the sink must
    // never see two presses of a key with no release between them, nor a
    // release with no press — i.e. the router can never corrupt the matrix into
    // a stuck or double-pressed state. This is the property a user would feel.
    {
        FakeKeyboard kb; TestRouter r; r.attach(kb);
        for (int i = 0; i < 300; ++i) {
            r.on_host_key(KEY_A, true);
            if (i % 3 == 0) r.on_tick_end(1);      // slow tap
            r.on_host_key(KEY_A, false);
            if (i % 5 == 0) r.on_tick_end(0);      // a paused tick
            r.on_tick_end(1);
        }
        bool down = false, sane = true;
        for (const auto& c : kb.calls) {
            if (c.second == down) sane = false;    // repeat press or repeat release
            down = c.second;
        }
        check("RT-11a", "the sink never sees a repeated press or release", sane);
        check("RT-11b", "and the key is left released at the end", !down);
    }

    // =======================================================================
    // SDL frontend tick shape (GitHub issue #122).
    //
    // src/platform/sdl_app.cpp had the SAME defect as the Qt frontend and now
    // uses the SAME Router — but its tick is a DIFFERENT machine
    // (frame_sequencer.h:388-412), so the sequence of Router calls it produces
    // is not the one the RT-* rows above drive:
    //
    //   * SDL_PollEvent() drains the WHOLE host event queue at the top of one
    //     run() iteration (sdl_app.cpp:228), so a press and its release can
    //     arrive as one batch with no frame between them — this is the race;
    //   * the audio pacer then decides how many frames that iteration
    //     emulates: ZERO when the device queue is ahead of the card, TWO when
    //     it is catching up (sdl_app.cpp:272-287, audio_pacing.h);
    //   * only then does the tick discharge (sdl_app.cpp:292).
    //
    // These rows also assert something the RT-* rows deliberately do not: not
    // what the SINK was told, but what the GUEST could observe. See GuestView.
    // =======================================================================

    // --- SDL-01: THE DEFECT, as the SDL loop actually produces it ------------
    // One poll batch carrying a complete tap, then the tick's frame. Before the
    // fix the matrix bit was set and cleared inside that batch and the frame
    // sampled a released key: the keystroke was silently lost.
    {
        GuestView g; GuestRouter r; r.attach(g);
        sdl_tick(r, g, Calls{{KEY_A, true}, {KEY_A, false}}, 1);
        check("SDL-01a", "a tap delivered in one poll batch is still seen by a frame",
              g.frames_sampling_down() == 1,
              "frames_down=" + std::to_string(g.frames_sampling_down()));
        check("SDL-01b", "and the guest saw a press, not just a sink write",
              g.samples == std::vector<bool>{true}, got(g.kb));
    }

    // --- SDL-02: the key does not jam down -----------------------------------
    // The hold must end. A tap that never comes up is a worse bug than a tap
    // that is lost, and on this path nothing else would ever clear it.
    {
        GuestView g; GuestRouter r; r.attach(g);
        sdl_tick(r, g, Calls{{KEY_A, true}, {KEY_A, false}}, 1);
        sdl_tick(r, g, Calls{}, 1);
        check("SDL-02a", "the deferred release lands after exactly one frame",
              g.samples == std::vector<bool>{true, false}, got(g.kb));
        check("SDL-02b", "and the sink saw one press and one release",
              g.kb.calls == Calls{{KEY_A, true}, {KEY_A, false}}, got(g.kb));
    }

    // --- SDL-03: the audio pacer's ZERO-frame tick ---------------------------
    // frames_for_tick() returns 0 whenever the device queue is ahead of the
    // card, so an SDL iteration can legitimately emulate nothing. That tick has
    // not honoured the hold, and discharging on it would reinstate the race in
    // exactly the situation the pacer makes common. This is the SDL-specific
    // shape of RT-04a: here the zero comes from the audio pacer, not a pause.
    {
        GuestView g; GuestRouter r; r.attach(g);
        sdl_tick(r, g, Calls{{KEY_A, true}, {KEY_A, false}}, 0);
        check("SDL-03a", "a tick the pacer gave no frames sees nothing and holds",
              g.samples.empty() && g.kb.calls == Calls{{KEY_A, true}}, got(g.kb));
        sdl_tick(r, g, Calls{}, 1);
        check("SDL-03b", "the next tick that emulates finally shows the guest the key",
              g.samples == std::vector<bool>{true}, got(g.kb));
        sdl_tick(r, g, Calls{}, 1);
        check("SDL-03c", "and only then does it come up",
              g.samples == std::vector<bool>{true, false}, got(g.kb));
    }

    // --- SDL-04: the catch-up tick that emulates TWO frames ------------------
    // The other end of the pacer's range. Both frames must see the key, and the
    // release must be discharged ONCE — a discharge per frame rather than per
    // tick would clear a key the user is still holding.
    {
        GuestView g; GuestRouter r; r.attach(g);
        sdl_tick(r, g, Calls{{KEY_A, true}, {KEY_A, false}}, 2);
        check("SDL-04a", "both frames of a catch-up tick sample the key down",
              g.samples == std::vector<bool>{true, true}, got(g.kb));
        check("SDL-04b", "and the release is delivered exactly once",
              g.kb.count(KEY_A, false) == 1 && g.kb.calls.size() == 2, got(g.kb));
    }

    // --- SDL-05: an ordinary keystroke gains no latency ----------------------
    // The no-regression row for this path: a key held across a tick boundary
    // must come up in the very tick its release is polled, not one later. Fails
    // if the release path defers unconditionally instead of only when no frame
    // has run since the press.
    {
        GuestView g; GuestRouter r; r.attach(g);
        sdl_tick(r, g, Calls{{KEY_A, true}},  1);
        sdl_tick(r, g, Calls{{KEY_A, false}}, 1);
        check("SDL-05a", "the release polled in tick 2 is applied before its frame",
              g.samples == std::vector<bool>{true, false}, got(g.kb));
        check("SDL-05b", "no extra sink traffic was generated",
              g.kb.calls == Calls{{KEY_A, true}, {KEY_A, false}}, got(g.kb));
    }

    // --- SDL-06: a held key is not stolen by the pacer -----------------------
    // Keys are HELD in games. Across a run of ticks of every shape the pacer
    // produces, a key the user has not released must never come up.
    {
        GuestView g; GuestRouter r; r.attach(g);
        sdl_tick(r, g, Calls{{KEY_A, true}}, 1);
        for (int i = 0; i < 30; ++i) sdl_tick(r, g, Calls{}, i % 3);   // 0,1,2,...
        check("SDL-06a", "a held key is never released by any tick shape",
              g.kb.calls == Calls{{KEY_A, true}}, got(g.kb));
        check("SDL-06b", "and every emulated frame sampled it down",
              !g.samples.empty() &&
                  g.frames_sampling_down() == static_cast<int>(g.samples.size()),
              "down=" + std::to_string(g.frames_sampling_down()) + "/" +
                  std::to_string(g.samples.size()));
    }

    // --- SDL-07: soak — the property, over the pacer's whole tick mix --------
    // 300 taps, each delivered as one poll batch, across ticks that emulate 0,
    // 1 or 2 frames. THE invariant of issue #122: every tap is observed by at
    // least one frame. Nothing may be lost, and the key must be left up.
    {
        GuestView g; GuestRouter r; r.attach(g);
        int  taps = 0;
        bool every_tap_seen = true;
        for (int i = 0; i < 300; ++i) {
            const int before = g.frames_sampling_down();
            sdl_tick(r, g, Calls{{KEY_A, true}, {KEY_A, false}}, i % 3);
            ++taps;
            // A tap on a zero-frame tick is not lost, only postponed: the hold
            // survives, so give it the tick that follows before judging.
            if (i % 3 == 0) sdl_tick(r, g, Calls{}, 1);
            if (g.frames_sampling_down() <= before) every_tap_seen = false;
            sdl_tick(r, g, Calls{}, 1);            // let the release land
        }
        check("SDL-07a", "every one of 300 poll-batch taps is seen by a frame",
              every_tap_seen && taps == 300, "taps=" + std::to_string(taps));
        check("SDL-07b", "the sink saw exactly 300 presses and 300 releases",
              g.kb.count(KEY_A, true) == 300 && g.kb.count(KEY_A, false) == 300,
              got(g.kb));
        check("SDL-07c", "and the key is left up",
              !g.down && !r.latch().has_deferred());
    }

    // =======================================================================
    // Keystroke serialisation (GitHub issue #268).
    //
    // Every row below needs TWO keys to be expressible at all, which is
    // precisely why the 69 rows above could not see this defect: their fake
    // guest tracks one key. These assert on the SEQUENCE OF STATES the guest
    // sampled, one per emulated frame — the only thing the ZX ROM ever sees.
    // =======================================================================

    // --- SER-01: THE DEFECT — two taps in one gap must not fuse into a chord -
    // Measured against v1.0.30 under Xvfb: `xdotool key --delay 0 1 2` typed
    // NOTHING into 48K BASIC. Both digits were asserted at once, KEY-SCAN
    // (ROM 0x028E) returns "more than one key" for a two-key chord, and the
    // ROM discarded them.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        mtick(r, g, tap(SC_1) + tap(SC_2), 1);
        msettle(r, g, 4);
        check("SER-01a", "no frame ever samples two tapped keys together",
              !g.ever_together(SC_1, SC_2), got(g.samples));
        check("SER-01b", "the guest sees them sequentially, one per frame",
              g.keystrokes() == std::vector<std::vector<int>>{{SC_1}, {SC_2}},
              got(g.keystrokes()));
    }

    // --- SER-02: the WRONG-CHARACTER case, verbatim from the issue ----------
    // `xdotool key --delay 0 ctrl p` typed `"` — SYMBOL SHIFT + P — instead of
    // PRINT. Two independent taps decoded as one chord. A tap of SYMBOL SHIFT
    // alone types nothing (K-TEST returns no character for a shift on its own),
    // exactly as on hardware, so the correct outcome is one dead frame then P.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        mtick(r, g, tap(SC_SYM) + tap(SC_P), 1);
        msettle(r, g, 4);
        check("SER-02a", "a SYM tap and a P tap never coincide in one frame",
              !g.ever_together(SC_SYM, SC_P), got(g.samples));
        check("SER-02b", "the guest sees SYM alone, then P alone",
              g.keystrokes() == std::vector<std::vector<int>>{{SC_SYM}, {SC_P}},
              got(g.keystrokes()));
    }

    // --- SER-03: the genuine chord MUST survive -----------------------------
    // The mirror image of SER-02 and the reason this needed a design pass:
    // SYMBOL SHIFT still HELD when P goes down is how `"` is typed, and it must
    // reach the guest in ONE frame. The only difference from SER-02 is where
    // the shift's key-up sits in the stream.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        mtick(r, g, Calls{{SC_SYM, true}, {SC_P, true},
                          {SC_P, false},  {SC_SYM, false}}, 1);
        msettle(r, g, 4);
        check("SER-03a", "a held-shift chord occupies exactly one frame",
              g.keystrokes().size() == 1, got(g.keystrokes()));
        check("SER-03b", "and that frame has both the shift and the key down",
              g.keystrokes() == std::vector<std::vector<int>>{{SC_P, SC_SYM}},
              got(g.keystrokes()));
    }

    // --- SER-04: EVERY shifted character — the regression surface -----------
    // If the modifier set is wrong in either direction, every shifted character
    // breaks: a shift treated as an ordinary keystroke splits the chord across
    // two frames and the guest types the unshifted key instead. Both ZX shifts
    // against ten keys, each chord inside one gap. One literal row ID per
    // shift; the detail names the first key that failed.
    {
        const int keys[] = {SC_P, SC_M, SC_N, SC_O, SC_1,
                            SC_2, SC_3, SC_4, KEY_A, KEY_B};
        for (const int shift : {SC_CAPS, SC_SYM}) {
            bool        all_ok = true;
            std::string first_bad;
            for (const int k : keys) {
                MatrixGuest g; MatrixRouter r; r.attach(g);
                mtick(r, g, Calls{{shift, true}, {k, true},
                                  {k, false},    {shift, false}}, 1);
                msettle(r, g, 3);
                const std::vector<std::vector<int>> want{{k, shift}};   // k < 224
                if (g.keystrokes() != want) {
                    all_ok = false;
                    if (first_bad.empty())
                        first_bad = "key=" + std::to_string(k) + " " +
                                    got(g.keystrokes());
                }
            }
            if (shift == SC_CAPS)
                check("SER-04a", "CAPS SHIFT + every key lands in ONE frame",
                      all_ok, first_bad);
            else
                check("SER-04b", "SYMBOL SHIFT + every key lands in ONE frame",
                      all_ok, first_bad);
        }
    }

    // --- SER-05: a shift HELD across two keys shifts BOTH of them -----------
    // The serialiser splits the two keys into separate frames; the shift must
    // travel into both, or the second character comes out unshifted. Fails on
    // any design that treats the shift as one queue entry consumed by the first
    // key.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        mtick(r, g, Calls{{SC_SYM, true},
                          {SC_P, true}, {SC_P, false},
                          {SC_O, true}, {SC_O, false},
                          {SC_SYM, false}}, 1);
        msettle(r, g, 4);
        check("SER-05", "a held shift accompanies every key it was held across",
              g.keystrokes() == std::vector<std::vector<int>>{{SC_P, SC_SYM},
                                                             {SC_O, SC_SYM}},
              got(g.keystrokes()));
    }

    // --- SER-06: ROLLOVER — the next key pressed before the last is released -
    // The normal fast-typing pattern, and the one the issue's reporter would
    // actually hit: v1 v2 ^1 ^2. Before the fix the only frame in the gap
    // sampled {1,2} and both characters were lost.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        mtick(r, g, Calls{{SC_1, true}, {SC_2, true},
                          {SC_1, false}, {SC_2, false}}, 1);
        msettle(r, g, 4);
        check("SER-06a", "rollover never presents the two keys in one frame",
              !g.ever_together(SC_1, SC_2), got(g.samples));
        check("SER-06b", "and both keys reach the guest, in order",
              g.keystrokes() == std::vector<std::vector<int>>{{SC_1}, {SC_2}},
              got(g.keystrokes()));
    }

    // --- SER-07: host Alt is a modifier too ---------------------------------
    // Keyboard::set_key latches the Alt VARIANT of a scancode at the key's
    // PRESS edge (keyboard.cpp:270-290), so Alt must still be down in the same
    // frame as the key it modifies. Split them and Alt+E stops being EDIT.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        mtick(r, g, Calls{{SC_ALT, true}, {SC_P, true},
                          {SC_P, false},  {SC_ALT, false}}, 1);
        msettle(r, g, 3);
        check("SER-07", "host Alt stays in the same frame as the key it modifies",
              g.keystrokes() == std::vector<std::vector<int>>{{SC_P, SC_ALT}},
              got(g.keystrokes()));
    }

    // --- SER-08: a shift tapped ALONE is still a keystroke ------------------
    // It must be shown to a frame, not swallowed as "only a modifier". On
    // hardware a tap of SYMBOL SHIFT is a real matrix event that the ROM sees
    // and decodes to nothing; software that polls port 0xFE directly can read
    // it, so the emulator may not discard it.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        mtick(r, g, tap(SC_SYM), 1);
        msettle(r, g, 2);
        check("SER-08", "a modifier tapped on its own is shown to a frame",
              g.keystrokes() == std::vector<std::vector<int>>{{SC_SYM}},
              got(g.keystrokes()));
    }

    // --- SER-09: NextZXOS menu navigation (the second symptom of #120) ------
    // Menu input is arrows + ENTER, all ordinary non-modifier scancodes (the
    // arrows via the extended-key register, keyboard.cpp:146-149). An arrow and
    // ENTER fused into {DOWN,ENTER} is not a menu input at all, and two
    // different arrows fused is a diagonal the menu never asked for.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        mtick(r, g, tap(SC_DOWN) + tap(SC_ENTER), 1);
        msettle(r, g, 4);
        check("SER-09a", "an arrow tap and an ENTER tap never coincide",
              !g.ever_together(SC_DOWN, SC_ENTER) &&
                  g.keystrokes() == std::vector<std::vector<int>>{{SC_DOWN},
                                                                 {SC_ENTER}},
              got(g.keystrokes()));

        MatrixGuest g2; MatrixRouter r2; r2.attach(g2);
        mtick(r2, g2, tap(SC_DOWN) + tap(SC_UP), 1);
        msettle(r2, g2, 4);
        check("SER-09b", "two different arrows in one gap stay two moves",
              !g2.ever_together(SC_DOWN, SC_UP) &&
                  g2.keystrokes() == std::vector<std::vector<int>>{{SC_DOWN},
                                                                  {SC_UP}},
              got(g2.keystrokes()));
    }

    // --- SER-10: FRAME-RATE INDEPENDENCE ------------------------------------
    // The whole point of the issue: severity was controlled entirely by the
    // frame rate. Six keystrokes delivered one per gap (a host keeping up) and
    // the SAME six crammed into ONE gap (a host at 18 FPS, or a paste) must
    // reach the guest identically. Nothing may be lost or reordered by
    // bunching — only delayed.
    {
        const int seq[] = {SC_1, SC_2, SC_3, SC_4, SC_P, SC_O};

        MatrixGuest ga; MatrixRouter ra; ra.attach(ga);
        for (const int k : seq) mtick(ra, ga, tap(k), 1);
        msettle(ra, ga, 10);

        MatrixGuest gb; MatrixRouter rb; rb.attach(gb);
        Calls all;
        for (const int k : seq) all = all + tap(k);
        mtick(rb, gb, all, 1);
        msettle(rb, gb, 10);

        check("SER-10a", "bunching six keystrokes into one gap loses nothing",
              ga.keystrokes() == gb.keystrokes(),
              got(ga.keystrokes()) + " vs " + got(gb.keystrokes()));
        check("SER-10b", "and that shared sequence is the CORRECT one",
              gb.keystrokes() == std::vector<std::vector<int>>{
                  {SC_1}, {SC_2}, {SC_3}, {SC_4}, {SC_P}, {SC_O}},
              got(gb.keystrokes()));
    }

    // --- SER-11: autorepeat is not a keystroke ------------------------------
    // sdl_input.cpp:12 forwards every SDL_EVENT_KEY_DOWN, repeat flag and all
    // (Qt filters them itself, main_window.cpp:2250). A repeat enqueued as a
    // keystroke would drain at one per frame — slower than the host emits them
    // on a slow machine — and the key would stay down for seconds after the
    // user let go. It must be dropped outright.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        Calls held{{KEY_A, true}};
        for (int i = 0; i < 100; ++i) held.push_back({KEY_A, true});
        mtick(r, g, held, 1);
        check("SER-11a", "100 autorepeats of a held key are one press",
              g.kb.calls == Calls{{KEY_A, true}}, got(g.kb));
        check("SER-11b", "and none of them are queued",
              r.pending() == 0, "pending=" + std::to_string(r.pending()));
        mtick(r, g, Calls{{KEY_A, false}}, 1);
        msettle(r, g, 2);
        check("SER-11c", "the key still comes up when the host releases it",
              g.down.empty() && g.kb.count(KEY_A, false) == 1, got(g.kb));
    }

    // --- SER-12: the queue is BOUNDED and can never strand a key ------------
    // 800 events with no frame at all — the debugger-paused-and-mashing case,
    // the only way to outrun a one-keystroke-per-frame drain. The bound must
    // hold, and the overflow path must leave the matrix agreeing with the
    // host's real fingers: a stuck key is a far worse bug than a lost one.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        for (int i = 0; i < 200; ++i)
            for (const int k : {SC_1, SC_2}) {
                r.on_host_key(k, true);
                r.on_host_key(k, false);
            }
        check("SER-12a", "the pending queue never exceeds its bound",
              r.pending() <= host_key_latch::MAX_PENDING,
              "pending=" + std::to_string(r.pending()));
        msettle(r, g, 400);
        check("SER-12b", "after the flood no key is left stranded down",
              g.down.empty() && r.pending() == 0, got(g.samples));
        r.on_host_key(SC_1, true);
        msettle(r, g, 3);
        check("SER-12c", "and the host's next real keypress still arrives",
              g.down == std::set<int>{SC_1}, got(g.kb));
    }

    // --- SER-13: the SCOPE BOUNDARY, asserted rather than assumed -----------
    // Two taps of the SAME key inside one gap still reach the guest as one
    // continuous press across two frames. That is issue #120's documented
    // residue and is deliberately NOT in #268's scope: separating them needs a
    // released frame between the two presses, which halves the drain rate and
    // makes a sustained tap stream grow the queue without bound. A human cannot
    // tap one key twice inside 20 ms (or even 55 ms at 18 FPS), whereas two
    // DIFFERENT keys in one gap is ordinary rollover — which is why the two
    // cases are treated differently. This row exists so the limit is visible to
    // the next reader instead of being rediscovered.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        mtick(r, g, tap(KEY_A) + tap(KEY_A), 1);
        msettle(r, g, 3);
        check("SER-13a", "same-key double tap is still ONE continuous press",
              g.keystrokes() == std::vector<std::vector<int>>{{KEY_A}, {KEY_A}},
              got(g.keystrokes()));
        check("SER-13b", "with no released frame between the two presses",
              g.samples.size() >= 2 && g.samples[0] == std::vector<int>{KEY_A} &&
                  g.samples[1] == std::vector<int>{KEY_A},
              got(g.samples));
    }

    // --- SER-14: a tick that emulated NOTHING must not drain ----------------
    // The #120 gate, extended to the queue. A paused tick has shown the guest
    // nothing, so releasing the keystroke in flight and applying the next one
    // would put two keystrokes into the same frame — the very defect.
    {
        MatrixGuest g; MatrixRouter r; r.attach(g);
        mtick(r, g, tap(SC_1) + tap(SC_2), 0);       // debugger paused
        check("SER-14a", "a zero-frame tick drains nothing",
              g.down == std::set<int>{SC_1} && r.pending() == 2,
              got(g.kb) + " pending=" + std::to_string(r.pending()));
        mtick(r, g, Calls{}, 1);
        mtick(r, g, Calls{}, 1);
        msettle(r, g, 3);
        check("SER-14b", "and the first tick that emulates resumes the order",
              g.keystrokes() == std::vector<std::vector<int>>{{SC_1}, {SC_2}},
              got(g.keystrokes()));
    }

    // --- SER-15: attach() clears the queue ----------------------------------
    // Cold boot reconstructs the Keyboard. RT-08/RT-09 pin that for the latch's
    // deferred release; the queue is a second thing that could be replayed into
    // a machine that never saw the keypress — a phantom keystroke at boot, and
    // again after every cold boot.
    {
        MatrixGuest old_g, new_g; MatrixRouter r; r.attach(old_g);
        mtick(r, old_g, tap(SC_1) + tap(SC_2), 0);   // queue loaded, nothing drained
        check("SER-15a", "the queue really is loaded before the cold boot",
              r.pending() == 2, "pending=" + std::to_string(r.pending()));
        r.attach(new_g);
        for (int i = 0; i < 5; ++i) { new_g.run_frame(); r.on_tick_end(1); }
        check("SER-15b", "no queued keystroke is replayed into the new machine",
              new_g.kb.calls.empty() && r.pending() == 0, got(new_g.kb));
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
