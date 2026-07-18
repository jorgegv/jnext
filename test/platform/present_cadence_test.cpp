// Present-cadence accounting test (GitHub issue #9 / Task 63).
//
// Like audio_pacing_test this file has NO VHDL oracle: it tests a *host*
// diagnostic, not emulated hardware. Its oracle is the written specification
// in src/platform/present_cadence.h — what each reported figure is defined to
// MEAN. Every row below is derived from that definition, never from reading
// the implementation back.
//
// WHY IT EXISTS. Two instruments in the Qt frontend measured the wrong thing,
// and every scrap of evidence collected from the issue-#9 reporter came to us
// through them:
//
//   * the status-bar "FPS" counted EMULATED frames, so a user reporting
//     "59 fps while it judders" was reporting a number that says nothing
//     about what reached the screen;
//   * the `cadence:` log line derived `dropped` from a raw paintEvent count,
//     which made it structurally identical to the audio-pacing double-step
//     tally — it measured the pacing policy, not lost presents. It also went
//     NEGATIVE, and reported a first window of `over 0ms`.
//
// THE SEMANTICS PINNED HERE. A frame is presented when its composited image
// is actually painted. Therefore a frame overwritten inside the same tick
// before any paint IS dropped — but that drop is deliberate, so `dropped` is
// reported whole and then decomposed:
//
//     dropped = superseded + unrendered + lost
//
// with `lost` — a fresh frame handed to the widget that no paint ever served
// — as the only figure a healthy machine must hold at zero. That decomposition
// is the entire point: it separates "the emulator meant to skip this" from
// "the window system ate it", which the old instrument could not do.
//
// Run: ./build/test/present_cadence_test

#include "platform/present_cadence.h"

#include <cstdio>
#include <string>

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

std::string dump(const present_cadence::Report& r)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "emu=%llu pres=%llu drop=%llu sup=%llu unr=%llu lost=%llu",
                  (unsigned long long)r.emulated, (unsigned long long)r.presented,
                  (unsigned long long)r.dropped, (unsigned long long)r.superseded,
                  (unsigned long long)r.unrendered, (unsigned long long)r.lost);
    return std::string(buf);
}

using present_cadence::Counters;
using present_cadence::summarize;
using present_cadence::superseded_in_tick;
using present_cadence::unrendered_in_tick;

// Accumulate one frontend tick into a window, exactly as the frontend does:
// `frames` run_frame() calls, then at most ONE update_frame() when the tick
// asked for a present. `painted` says whether that present was actually
// served by a paintEvent.
void tick(Counters& c, int frames, bool present_requested, bool painted)
{
    c.emulated += static_cast<uint64_t>(frames > 0 ? frames : 0);
    c.superseded += superseded_in_tick(frames, present_requested);
    c.unrendered += unrendered_in_tick(frames, present_requested);
    if (present_requested && painted) c.presented += 1;
}

}  // namespace

int main()
{
    std::printf("Present-cadence accounting tests (issue #9 / Task 63)\n");
    std::printf("====================================================\n");

    // --- PC-01: the healthy steady state --------------------------------------
    // 50 ticks, one frame each, every one painted. Nothing is dropped, and in
    // particular `lost` — the issue-#9 signal — is zero.
    {
        Counters c;
        for (int i = 0; i < 50; i++) tick(c, 1, true, true);
        const auto r = summarize(c, 1000);
        check("PC-01a", "steady state: presented equals emulated",
              r.emulated == 50 && r.presented == 50, dump(r));
        check("PC-01b", "steady state: nothing dropped", r.dropped == 0, dump(r));
        check("PC-01c", "steady state: no unexplained loss", r.lost == 0, dump(r));
    }

    // --- PC-02: THE REGRESSION ROW --------------------------------------------
    // A tick that emulates TWO frames and presents once (audio catch-up).
    //
    // This is the case the old instrument got wrong: it reported `dropped` as
    // emulated-minus-paintEvents, which made `dropped` a pure restatement of
    // the double-step count and told us nothing about the display.
    //
    // Decided from first principles: the FIRST frame's image was overwritten
    // in the framebuffer before any paint could serve it, so it genuinely
    // never reached the screen and IS dropped. What must be true is that this
    // drop is attributed to `superseded` (deliberate) and leaves `lost` at
    // ZERO — because the frame that WAS handed to the widget did get shown.
    {
        Counters c;
        tick(c, 2, true, true);
        const auto r = summarize(c, 1000);
        check("PC-02a", "2-frame tick, 1 present: one frame dropped",
              r.emulated == 2 && r.presented == 1 && r.dropped == 1, dump(r));
        check("PC-02b", "2-frame tick: the drop is attributed to superseded",
              r.superseded == 1, dump(r));
        check("PC-02c", "2-frame tick: nothing LOST — the present was served",
              r.lost == 0, dump(r));
    }

    // --- PC-03: the same tick when the present is NOT served -------------------
    // Identical emulation, but the window system swallowed the paint. Now one
    // frame is superseded (expected) AND one is genuinely lost (the defect).
    // PC-02 and PC-03 differ ONLY in whether the frame reached the screen, so
    // any accounting that cannot separate them is the bug this file guards.
    {
        Counters c;
        tick(c, 2, true, /*painted=*/false);
        const auto r = summarize(c, 1000);
        check("PC-03a", "2-frame tick, 0 presents: both frames dropped",
              r.emulated == 2 && r.presented == 0 && r.dropped == 2, dump(r));
        check("PC-03b", "2-frame tick, 0 presents: exactly one is LOST",
              r.lost == 1 && r.superseded == 1, dump(r));
    }

    // --- PC-04: never negative -------------------------------------------------
    // Qt paints on expose/resize, and a debugger-paused frontend keeps calling
    // update_frame() with no run_frame() at all. Presented can therefore exceed
    // emulated. `dropped` must floor at 0 — the old line printed negatives.
    {
        Counters c;
        c.emulated = 0;
        c.presented = 50;          // paused: presents with no emulation
        const auto r = summarize(c, 1000);
        check("PC-04a", "presented > emulated: dropped floors at 0, never negative",
              r.dropped == 0, dump(r));
        check("PC-04b", "presented > emulated: nothing is reported as lost",
              r.lost == 0 && r.superseded == 0 && r.unrendered == 0, dump(r));
    }

    // --- PC-05: the render throttle is explained, not blamed -------------------
    // At speed > 1x the frontend deliberately skips compositing on most ticks.
    // Those frames are dropped, but they belong in `unrendered`, not `lost`.
    {
        Counters c;
        for (int i = 0; i < 10; i++) {
            tick(c, 1, /*present_requested=*/true, true);
            for (int j = 0; j < 3; j++) tick(c, 1, /*present_requested=*/false, false);
        }
        const auto r = summarize(c, 1000);
        check("PC-05a", "throttled ticks: 40 emulated, 10 presented",
              r.emulated == 40 && r.presented == 10, dump(r));
        check("PC-05b", "throttled ticks: the 30 drops are unrendered, not lost",
              r.dropped == 30 && r.unrendered == 30 && r.lost == 0, dump(r));
    }

    // --- PC-06: a skipped tick (audio ahead) drops nothing ---------------------
    // frames_for_tick() returns 0 when the device queue is high. No frame was
    // emulated, so no frame can have been dropped.
    {
        Counters c;
        tick(c, 0, /*present_requested=*/true, true);
        const auto r = summarize(c, 1000);
        check("PC-06", "0-frame tick: nothing emulated, nothing dropped",
              r.emulated == 0 && r.dropped == 0 && r.lost == 0, dump(r));
    }

    // --- PC-07: the decomposition is exact ------------------------------------
    // The identity that makes the report trustworthy: no drop may be silently
    // dropped from the breakdown, and none double-counted. Swept over a mixed
    // workload covering every tick shape.
    {
        bool identity_holds = true, presented_accounted = true;
        for (int frames = 0; frames <= 4; frames++) {
            for (int req = 0; req <= 1; req++) {
                for (int painted = 0; painted <= 1; painted++) {
                    Counters c;
                    tick(c, frames, req != 0, painted != 0);
                    const auto r = summarize(c, 1000);
                    if (r.superseded + r.unrendered + r.lost != r.dropped)
                        identity_holds = false;
                    // Every emulated frame either reached the screen or is
                    // counted as dropped — none may vanish from the books.
                    if (r.presented <= r.emulated &&
                        r.presented + r.dropped != r.emulated)
                        presented_accounted = false;
                }
            }
        }
        check("PC-07a", "dropped always decomposes exactly into its three buckets",
              identity_holds);
        check("PC-07b", "every emulated frame is either presented or dropped",
              presented_accounted);
    }

    // --- PC-08: an over-tally cannot manufacture a negative `lost` -------------
    // Defensive: the explained buckets are clamped against `dropped` before the
    // remainder is taken, so inconsistent inputs degrade gracefully instead of
    // wrapping a uint64 to ~1.8e19.
    {
        Counters c;
        c.emulated = 2;
        c.presented = 1;
        c.superseded = 99;   // absurd over-tally
        c.unrendered = 99;
        const auto r = summarize(c, 1000);
        check("PC-08a", "over-tallied buckets clamp to dropped, no wraparound",
              r.dropped == 1 && r.superseded == 1 && r.unrendered == 0 && r.lost == 0,
              dump(r));
        check("PC-08b", "identity still holds under an over-tally",
              r.superseded + r.unrendered + r.lost == r.dropped, dump(r));
    }

    // --- PC-09: a window with no duration is not reportable --------------------
    // The old first log line said `over 0ms`, which is not a measurement.
    // summarize() must mark such a window unreportable so callers suppress it.
    {
        Counters c;
        c.emulated = 50;
        c.presented = 50;
        check("PC-09a", "a 0ms window is not reportable",
              !summarize(c, 0).reportable);
        check("PC-09b", "a negative window (clock anomaly) is not reportable",
              !summarize(c, -5).reportable);
        check("PC-09c", "a positive window is reportable and carries its duration",
              summarize(c, 1000).reportable && summarize(c, 1000).elapsed_ms == 1000);
    }

    // --- PC-10: the per-tick derivations --------------------------------------
    // superseded/unrendered are mutually exclusive: a tick either asked for a
    // present (and superseded all but its last frame) or did not (and rendered
    // none of them).
    {
        check("PC-10a", "a 1-frame presenting tick supersedes nothing",
              superseded_in_tick(1, true) == 0 && unrendered_in_tick(1, true) == 0);
        check("PC-10b", "an N-frame presenting tick supersedes N-1",
              superseded_in_tick(2, true) == 1 && superseded_in_tick(5, true) == 4);
        check("PC-10c", "a non-presenting tick supersedes nothing, renders none",
              superseded_in_tick(3, false) == 0 && unrendered_in_tick(3, false) == 3);
        check("PC-10d", "a 0-frame tick contributes nothing either way",
              superseded_in_tick(0, true) == 0 && unrendered_in_tick(0, true) == 0 &&
              superseded_in_tick(0, false) == 0 && unrendered_in_tick(0, false) == 0);
    }

    // --- PC-11: a fastload burst is fully explained ---------------------------
    // 200 frames in one tick, one present. 199 superseded, zero lost: a burst
    // must not look like a display fault.
    {
        Counters c;
        tick(c, 200, true, true);
        const auto r = summarize(c, 1000);
        check("PC-11", "200-frame burst: 199 superseded, none lost",
              r.emulated == 200 && r.presented == 1 && r.dropped == 199 &&
              r.superseded == 199 && r.lost == 0, dump(r));
    }

    // --- PC-12: the discriminative case — pacing must not masquerade as loss ---
    // THE defect being guarded. Two windows with IDENTICAL emulated counts and
    // IDENTICAL audio double-step counts, differing only in whether presents
    // were served. The old instrument reported the same `dropped` for both,
    // which is exactly why the reporter's logs could not be interpreted.
    {
        Counters healthy, broken;
        for (int i = 0; i < 25; i++) tick(healthy, 2, true, /*painted=*/true);
        for (int i = 0; i < 25; i++) tick(broken, 2, true, /*painted=*/false);
        const auto h = summarize(healthy, 1000);
        const auto b = summarize(broken, 1000);
        check("PC-12a", "both windows emulate identically",
              h.emulated == 50 && b.emulated == 50);
        check("PC-12b", "healthy window: all drops explained as pacing",
              h.superseded == 25 && h.lost == 0, dump(h));
        check("PC-12c", "broken window: 25 presents genuinely LOST",
              b.superseded == 25 && b.lost == 25, dump(b));
        check("PC-12d", "`lost` separates the two; `dropped` alone would not",
              h.lost != b.lost);
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
