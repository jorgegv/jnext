// Host key minimum-hold latch test (GitHub issue #120).
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
// The contract under test:
//
//   * a press is NEVER delayed;
//   * a release is deferred if and only if no frame has run since the press;
//   * a deferred release is delivered by the first tick that emulates a frame;
//   * once a frame has run, releases pass through unchanged — the latch must
//     not become a permanent one-frame lag on every key-up;
//   * a re-press cancels a pending release;
//   * a tick that emulated nothing must not discharge the latch.
//
// TWO LAYERS, DELIBERATELY. The HK-* rows pin the Latch POLICY. The RT-* rows
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

#include <cstdio>
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

}  // namespace

int main()
{
    using host_key_latch::Latch;

    std::printf("Host key latch test (GitHub issue #120)\n");
    std::printf("====================================================\n");

    // --- HK-01: a press is never delayed -----------------------------------
    {
        Latch l;
        check("HK-01", "on_press always asserts the key immediately",
              l.on_press(KEY_A));
    }

    // --- HK-02: THE DEFECT — press and release inside one frame gap --------
    // Without the latch this pair sets the matrix bit and clears it again with
    // no frame in between, and the guest never sees the key. The release must
    // be held back.
    {
        Latch l;
        l.on_press(KEY_A);
        check("HK-02a", "a release with no frame since the press is deferred",
              !l.on_release(KEY_A));
        check("HK-02b", "the latch reports the pending release",
              l.has_deferred());
    }

    // --- HK-03: the deferred release is delivered by the next frame --------
    // The key must come up again — a hold that never ends would jam the key
    // down, which is a worse bug than the one being fixed.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        const auto due = frame_tick(l);
        check("HK-03a", "the first tick that emulates a frame discharges it",
              due.size() == 1 && due[0] == KEY_A, got(due));
        check("HK-03b", "nothing remains pending afterwards", !l.has_deferred());
    }

    // --- HK-04: the hold lasts exactly one frame, not more -----------------
    // A second frame must not re-deliver the release.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        frame_tick(l);
        const auto second = frame_tick(l);
        check("HK-04", "a release is discharged once, not on every frame",
              second.empty(), got(second));
    }

    // --- HK-05: the normal path is untouched -------------------------------
    // A press that has already spanned a frame releases immediately, in the
    // same call. This is the case for every ordinary human keystroke, and the
    // latch must be a no-op there or it would add a frame of input lag to
    // everything.
    {
        Latch l;
        l.on_press(KEY_A);
        frame_tick(l);
        check("HK-05a", "a release after a frame passes straight through",
              l.on_release(KEY_A));
        check("HK-05b", "and defers nothing", !l.has_deferred());
    }

    // --- HK-06: a held key never becomes deferred, however long ------------
    // Ten frames of a key held down, then a release: still immediate. Pins that
    // on_frames_ran() clears the freshness of keys that are STILL DOWN, not
    // just of those being released.
    {
        Latch l;
        l.on_press(KEY_A);
        for (int i = 0; i < 10; ++i) frame_tick(l);
        check("HK-06", "a long-held key releases immediately",
              l.on_release(KEY_A) && !l.has_deferred());
    }

    // --- HK-07: a stray release passes through -----------------------------
    // The key-up half of a chord pressed before the window had focus arrives
    // with no matching press. Deferring it would hold back a release for a key
    // that was never down.
    {
        Latch l;
        check("HK-07", "a release with no preceding press is immediate",
              l.on_release(KEY_A) && !l.has_deferred());
    }

    // --- HK-08: a re-press cancels the pending release ---------------------
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
        check("HK-08a", "a re-press clears the pending release",
              !l.has_deferred());
        const auto due = frame_tick(l);
        check("HK-08b", "and no release is emitted at the next frame",
              due.empty(), got(due));
        // That frame ran with the key down, so the merged press HAS been seen:
        // its release is now immediate, exactly like any ordinary keystroke.
        check("HK-08c", "the merged press ends normally once a frame has run",
              l.on_release(KEY_A) && !l.has_deferred());
    }

    // --- HK-08d: a merged press with NO frame at all is still latched -------
    // Two taps inside one gap collapse into one press, and that press has still
    // not been seen — so its release must be deferred like any other.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        l.on_press(KEY_A);
        check("HK-08d", "a merged press with no frame between defers its release",
              !l.on_release(KEY_A) && l.has_deferred());
        const auto due = frame_tick(l);
        check("HK-08e", "and the merge discharges exactly one release",
              due.size() == 1 && due[0] == KEY_A, got(due));
    }

    // --- HK-09: independent keys do not interfere --------------------------
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_press(KEY_B);
        l.on_release(KEY_A);
        l.on_release(KEY_B);
        const auto due = frame_tick(l);
        check("HK-09", "both keys deferred in one gap are both discharged",
              due.size() == 2 &&
              ((due[0] == KEY_A && due[1] == KEY_B) ||
               (due[0] == KEY_B && due[1] == KEY_A)), got(due));
    }

    // --- HK-10: a repeated release does not queue twice --------------------
    // Duplicate key-up events (seen when a host hotkey handler and the normal
    // path both forward the same event) must not produce two clears.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        l.on_release(KEY_A);
        const auto due = frame_tick(l);
        check("HK-10", "a duplicate release is not queued twice",
              due.size() == 1 && due[0] == KEY_A, got(due));
    }

    // --- HK-11: a tick that emulated nothing must not discharge ------------
    // A paused debugger runs no frames, so the guest has still not looked at
    // the matrix. The caller is contracted not to call on_frames_ran() then;
    // this row pins that the hold genuinely survives such ticks, i.e. the state
    // lives in the latch and not in the caller's loop.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        check("HK-11a", "the hold survives ticks that emulate nothing",
              l.has_deferred());
        const auto due = frame_tick(l);
        check("HK-11b", "and is discharged by the first tick that does",
              due.size() == 1 && due[0] == KEY_A, got(due));
    }

    // --- HK-12: out-of-range scancodes pass through unlatched --------------
    // The frontend feeds real SDL_Scancode values, but an unmapped or negative
    // code must never index the bitset.
    {
        Latch l;
        check("HK-12a", "a negative scancode presses through",  l.on_press(-1));
        check("HK-12b", "a negative scancode releases through", l.on_release(-1));
        check("HK-12c", "an over-range scancode presses through",
              l.on_press(host_key_latch::MAX_KEYS));
        check("HK-12d", "an over-range scancode releases through",
              l.on_release(host_key_latch::MAX_KEYS));
        check("HK-12e", "and nothing is deferred for them", !l.has_deferred());
    }

    // --- HK-13: reset() drops everything -----------------------------------
    // Cold boot reconstructs the Keyboard with an all-released matrix; a
    // surviving pending release would clear a bit belonging to a machine that
    // no longer exists.
    {
        Latch l;
        l.on_press(KEY_A);
        l.on_release(KEY_A);
        l.reset();
        check("HK-13a", "reset clears pending releases", !l.has_deferred());
        const auto due = frame_tick(l);
        check("HK-13b", "and none are emitted afterwards", due.empty(), got(due));
        check("HK-13c", "reset also clears freshness, so a stray release passes",
              l.on_release(KEY_A));
    }

    // --- HK-14: many taps across many frames stay in step ------------------
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
        check("HK-14a", "every sub-frame tap is deferred", always_deferred);
        check("HK-14b", "200 taps discharge exactly 200 releases",
              discharged == 200, "discharged=" + std::to_string(discharged));
        check("HK-14c", "no state accumulates", !l.has_deferred());
    }

    // --- HK-15: alternating fast and slow taps ------------------------------
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
        check("HK-15", "alternating slow and fast taps stay independent", ok);
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

        FakeKeyboard kb2; TestRouter r2; r2.attach(kb2);
        r2.on_host_key(KEY_A, true);
        r2.on_host_key(KEY_B, true);
        r2.on_host_key(KEY_A, false);
        r2.on_host_key(KEY_B, false);
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

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
