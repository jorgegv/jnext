// Achieved-vs-requested speed report test (GitHub issue #120).
//
// Like audio_pacing_test / render_policy_test this file has NO VHDL oracle: it
// tests a HOST reporting policy, not emulated hardware. Its oracle is the
// stated contract in src/platform/speed_report.h plus one hardware constant
// taken from the machine timing table (src/core/emulator_config.h, quoted in
// src/platform/frame_deadline.h):
//
//     Next 50 Hz:  1824 * 311 cycles / 28 MHz = 20.259 ms  ->  49.36 Hz
//     Next 60 Hz:  1824 * 264 cycles / 28 MHz = 17.198 ms  ->  58.15 Hz
//
// The contract under test:
//
//   * 100% means "one emulated frame per machine frame period" — so a Next
//     running its true 49.36 fps at 1x scores 100, NOT 98.7. This is the point
//     of the whole exercise: the status bar's old figure was the requested
//     multiplier and measured nothing, and a user expecting a round 50 read the
//     honest 49.4 as a shortfall.
//   * achieved and requested are directly comparable at every multiplier.
//   * a window that emulated nothing is "no measurement", never 0%.
//   * a shortfall is only declared past SHORTFALL_MARGIN_PCT, which must clear
//     integer-frame quantisation and ordinary audio-pacer interventions.
//
// Every row derives from that contract, never from reading the implementation
// back.
//
// Run: ./build/test/speed_report_test

#include "platform/speed_report.h"

#include <cmath>
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

std::string got(const speed_report::Report& r)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "req=%d ach=%d valid=%d short=%d",
                  r.requested_pct, r.achieved_pct,
                  r.achieved_valid ? 1 : 0, r.shortfall ? 1 : 0);
    return buf;
}

// The two Next frame periods, in ms, from the timing table quoted above.
constexpr double NEXT50_MS = 20.259;
constexpr double NEXT60_MS = 17.198;
// Their exact refresh rates.
constexpr double NEXT50_HZ = 1000.0 / NEXT50_MS;   // 49.36
constexpr double NEXT60_HZ = 1000.0 / NEXT60_MS;   // 58.15

}  // namespace

int main()
{
    using speed_report::summarize;
    using speed_report::SHORTFALL_MARGIN_PCT;

    std::printf("Speed report test (GitHub issue #120)\n");
    std::printf("====================================================\n");

    // --- SR-01: a healthy Next at 50 Hz scores exactly 100% ----------------
    // THE HEADLINE ROW. 49.36 fps is the machine's true rate, so it is 100% of
    // it. An implementation that measured against a round 50 Hz would report 99
    // here and teach every user that jnext runs slow.
    {
        const auto r = summarize(NEXT50_HZ, NEXT50_MS, 1.0);
        check("SR-01a", "Next 50 Hz on target reports achieved 100%",
              r.achieved_pct == 100, got(r));
        check("SR-01b", "Next 50 Hz on target is not a shortfall",
              !r.shortfall && r.achieved_valid, got(r));
        check("SR-01c", "requested is the multiplier, unchanged",
              r.requested_pct == 100, got(r));
    }

    // --- SR-02: same for the 60 Hz mode ------------------------------------
    // 58.15 fps, not 60. The percentage must be period-relative, so the same
    // machine keeping up scores 100 in both video modes.
    {
        const auto r = summarize(NEXT60_HZ, NEXT60_MS, 1.0);
        check("SR-02", "Next 60 Hz on target also reports 100%",
              r.achieved_pct == 100 && !r.shortfall, got(r));
    }

    // --- SR-03: integer-frame quantisation must not cry wolf ---------------
    // A 1 s window of a 49.36 Hz machine reports 49 or 50 WHOLE frames. Both
    // are a machine that is keeping up; neither may be flagged.
    {
        const auto lo = summarize(49.0, NEXT50_MS, 1.0);
        const auto hi = summarize(50.0, NEXT50_MS, 1.0);
        check("SR-03a", "49 frames in a 1s window is not a shortfall",
              !lo.shortfall, got(lo));
        check("SR-03b", "50 frames in a 1s window is not a shortfall",
              !hi.shortfall, got(hi));
        check("SR-03c", "quantisation stays within a point of 100%",
              lo.achieved_pct == 99 && hi.achieved_pct == 101,
              got(lo) + " / " + got(hi));
    }

    // --- SR-04: an audio-pacer intervention must not cry wolf either -------
    // audio_pacing::frames_for_tick legitimately runs a double or skips a frame
    // to track the sound card. Over a 1 s window that is one frame either way,
    // on top of quantisation: still not a fault.
    {
        const auto r = summarize(48.0, NEXT50_MS, 1.0);
        check("SR-04", "a one-frame audio-pacer skip is not a shortfall",
              !r.shortfall, got(r));
    }

    // --- SR-05: a genuine shortfall IS reported ----------------------------
    // The issue-#120 scenario: the host delivers 30 fps while the user asked
    // for normal speed. The old indicator said "100%" here.
    {
        const auto r = summarize(30.0, NEXT50_MS, 1.0);
        check("SR-05a", "30 fps at 1x is flagged as a shortfall",
              r.shortfall && r.achieved_valid, got(r));
        check("SR-05b", "30 fps at 1x reports ~61% achieved",
              r.achieved_pct == 61, got(r));
        check("SR-05c", "the requested figure is still reported alongside",
              r.requested_pct == 100, got(r));
    }

    // --- SR-06: the margin boundary, both sides ----------------------------
    // Exactly SHORTFALL_MARGIN_PCT below is NOT a shortfall (strict <); one
    // point further down is. Pinning both sides stops the comparison silently
    // becoming <= or the margin drifting.
    {
        // Choose fps so achieved lands exactly on 100 - margin, then one lower.
        const double at_edge = (100 - SHORTFALL_MARGIN_PCT) * 10.0 / NEXT50_MS;
        const double past    = (100 - SHORTFALL_MARGIN_PCT - 1) * 10.0 / NEXT50_MS;
        const auto e = summarize(at_edge, NEXT50_MS, 1.0);
        const auto p = summarize(past, NEXT50_MS, 1.0);
        check("SR-06a", "exactly at the margin is not yet a shortfall",
              e.achieved_pct == 100 - SHORTFALL_MARGIN_PCT && !e.shortfall, got(e));
        check("SR-06b", "one point past the margin is a shortfall",
              p.achieved_pct == 100 - SHORTFALL_MARGIN_PCT - 1 && p.shortfall, got(p));
    }

    // --- SR-07: the margin is the DERIVED value, bounded on both sides -----
    // The header derives the margin as 3 frames of one-second-window error at
    // the longest machine period (1 quantisation + 2 audio-pacer), converted at
    // period/10 points per frame. Assert that derivation directly, and bound it
    // ABOVE as well: a lower bound alone is what let the old hand-picked 10
    // stand unchallenged, and a margin free to drift upwards is a margin that
    // silently stops firing. The upper bound is the same budget rounded up to
    // the next whole point — i.e. the margin must be the tightest whole number
    // that still clears the noise, not merely some number above it.
    {
        const double one_frame_pts = NEXT50_MS / 10.0;          // 2.03
        const double budget        = 3.0 * one_frame_pts;       // 6.08
        const int    tightest      = static_cast<int>(budget) + 1;
        check("SR-07a", "margin clears 3 frames of window error at the worst period",
              SHORTFALL_MARGIN_PCT > budget,
              "margin=" + std::to_string(SHORTFALL_MARGIN_PCT) +
              " budget=" + std::to_string(budget));
        check("SR-07b", "and is the TIGHTEST whole point that does",
              SHORTFALL_MARGIN_PCT == tightest,
              "margin=" + std::to_string(SHORTFALL_MARGIN_PCT) +
              " tightest=" + std::to_string(tightest));
    }

    // --- SR-08: the noise budget really is quiet, and 3 frames is enough ----
    // The rows above assert the ARITHMETIC of the budget; this one asserts its
    // PURPOSE — that every window a healthy host can produce stays unflagged.
    // Sweep the whole healthy range: the true rate displaced by up to 3 frames
    // in either direction, which is quantisation plus the two pacer
    // interventions the budget pays for.
    {
        const double target = NEXT50_HZ;
        bool quiet = true;
        for (int d = -3; d <= 3; ++d) {
            const auto r = summarize(target + d, NEXT50_MS, 1.0);
            if (r.shortfall) quiet = false;
        }
        check("SR-08a", "no window within the 3-frame noise budget is flagged",
              quiet);
        // And the very next frame down IS caught — the margin is a threshold,
        // not a shrug. (4 frames low = 45.4 fps on a 49.36 Hz machine.)
        const auto beyond = summarize(target - 4, NEXT50_MS, 1.0);
        check("SR-08b", "a window one frame past the budget is flagged",
              beyond.shortfall, got(beyond));
    }

    // --- SR-09: the complaint that drove the retune -------------------------
    // A host delivering 91% of the true rate is a real, borderline-visible
    // shortfall. At the old hand-picked margin of 10 it displayed a clean
    // "100%"; it must now be reported.
    {
        const auto r = summarize(NEXT50_HZ * 0.91, NEXT50_MS, 1.0);
        check("SR-09", "a host at 91% of true rate is reported, not hidden",
              r.shortfall && r.achieved_pct == 91, got(r));
    }

    // --- SR-10: multipliers are comparable on the same scale ---------------
    // At 2x the machine must run at twice its refresh to be "keeping up". A
    // report that measured achieved against 1x would flag every fast-forward.
    {
        const auto ok    = summarize(NEXT50_HZ * 2.0, NEXT50_MS, 2.0);
        const auto short_ = summarize(NEXT50_HZ, NEXT50_MS, 2.0);
        check("SR-10a", "2x actually achieving 2x reports 200% and no shortfall",
              ok.requested_pct == 200 && ok.achieved_pct == 200 && !ok.shortfall,
              got(ok));
        check("SR-10b", "2x achieving only 1x is a shortfall at 100%",
              short_.requested_pct == 200 && short_.achieved_pct == 100 &&
              short_.shortfall, got(short_));
    }

    // --- SR-11: half speed is a first-class target -------------------------
    // --speed 50 asks for half the refresh; delivering it is 50%, not 100%,
    // and is not a shortfall.
    {
        const auto r = summarize(NEXT50_HZ / 2.0, NEXT50_MS, 0.5);
        check("SR-11", "0.5x delivering half the refresh is on target",
              r.requested_pct == 50 && r.achieved_pct == 50 && !r.shortfall,
              got(r));
    }

    // --- SR-12: a paused machine is "no measurement", never 0% -------------
    // The debugger pauses emulation; the window then emulates nothing. Printing
    // "0%" there would be a false alarm on a deliberate state.
    {
        const auto r = summarize(0.0, NEXT50_MS, 1.0);
        check("SR-12a", "zero emulated frames yields no achieved figure",
              !r.achieved_valid, got(r));
        check("SR-12b", "zero emulated frames is not reported as a shortfall",
              !r.shortfall, got(r));
        check("SR-12c", "the requested figure survives a paused window",
              r.requested_pct == 100, got(r));
    }

    // --- SR-13: an unknown frame period yields no measurement --------------
    // Before the machine's timing is known there is no rate to be a percentage
    // of. Refuse rather than invent one (a division by zero would otherwise
    // produce inf/NaN and a garbage label).
    {
        const auto z = summarize(50.0, 0.0, 1.0);
        const auto n = summarize(50.0, -1.0, 1.0);
        check("SR-13a", "zero frame period yields no achieved figure",
              !z.achieved_valid && !z.shortfall, got(z));
        check("SR-13b", "negative frame period yields no achieved figure",
              !n.achieved_valid && !n.shortfall, got(n));
    }

    // --- SR-14: overachieving is reported, never clamped -------------------
    // A host running slightly ahead of the audio clock (or a mid-window speed
    // change) can exceed 100. Clamping would hide a real disagreement between
    // the timer and the machine's period.
    {
        const auto r = summarize(NEXT50_HZ * 1.2, NEXT50_MS, 1.0);
        check("SR-14", "achieved above the target is reported as-is",
              r.achieved_pct == 120 && !r.shortfall, got(r));
    }

    // --- SR-15: the requested figure never depends on the measurement ------
    // Whatever the host does, the cell must still be able to say what was
    // asked for — that is the one thing the old indicator got right and the
    // fix must not regress.
    {
        const auto a = summarize(0.0, NEXT50_MS, 4.0);
        const auto b = summarize(3.0, NEXT50_MS, 4.0);
        check("SR-15", "requested_pct tracks the multiplier in every case",
              a.requested_pct == 400 && b.requested_pct == 400,
              got(a) + " / " + got(b));
    }

    // --- SR-16: shortfall implies a valid measurement ----------------------
    // A caller may reasonably branch on `shortfall` alone; it must never be
    // true while `achieved_pct` is meaningless.
    {
        bool implied = true;
        for (double fps = 0.0; fps <= 120.0; fps += 0.5) {
            for (double mult : {0.5, 1.0, 2.0, 4.0}) {
                const auto r = summarize(fps, NEXT50_MS, mult);
                if (r.shortfall && !r.achieved_valid) implied = false;
            }
        }
        check("SR-16", "shortfall is never set without a valid measurement",
              implied);
    }

    // --- SR-17: rate_from_window removes the coarse-timer error ------------
    // The status timer is a Qt::CoarseTimer (5% tolerance), so the SAME healthy
    // machine produces frame counts that differ by several percent purely
    // because the window was short or long. Normalising by the window actually
    // timed must collapse those to the same rate — that is what buys the tight
    // margin, so it is asserted, not assumed.
    {
        using speed_report::rate_from_window;
        // A 49.36 Hz machine, perfectly healthy, measured over three windows a
        // coarse timer could plausibly deliver. The frame counts differ.
        const double r_short = rate_from_window(47.0, 950);   // 950 ms window
        const double r_exact = rate_from_window(49.0, 1000);
        const double r_long  = rate_from_window(52.0, 1050);
        const int a_short = summarize(r_short, NEXT50_MS, 1.0).achieved_pct;
        const int a_exact = summarize(r_exact, NEXT50_MS, 1.0).achieved_pct;
        const int a_long  = summarize(r_long,  NEXT50_MS, 1.0).achieved_pct;
        check("SR-17a", "a short window is scaled up, a long one down",
              r_short > 47.0 && r_long < 52.0,
              std::to_string(r_short) + "/" + std::to_string(r_long));
        check("SR-17b", "the three windows agree to within a point",
              std::abs(a_short - a_exact) <= 1 && std::abs(a_long - a_exact) <= 1,
              std::to_string(a_short) + "/" + std::to_string(a_exact) + "/" +
              std::to_string(a_long));
        check("SR-17c", "and none of them is flagged as a shortfall",
              !summarize(r_short, NEXT50_MS, 1.0).shortfall &&
              !summarize(r_long,  NEXT50_MS, 1.0).shortfall);
        // Without normalisation the 950 ms window reads 47 frames -> 95%, which
        // at the tightened margin is within 2 points of being flagged on a
        // machine doing nothing wrong. This is the row that justifies the call.
        check("SR-17d", "the un-normalised count is materially further off",
              summarize(47.0, NEXT50_MS, 1.0).achieved_pct <
              a_short - 3,
              std::to_string(summarize(47.0, NEXT50_MS, 1.0).achieved_pct));
    }

    // --- SR-18: an unmeasurable window falls back, never divides by zero ----
    {
        using speed_report::rate_from_window;
        check("SR-18a", "a 1000 ms window returns the count unchanged",
              rate_from_window(49.0, 1000) == 49.0);
        check("SR-18b", "a zero window returns the count, not inf/NaN",
              rate_from_window(49.0, 0) == 49.0);
        check("SR-18c", "a negative window returns the count too",
              rate_from_window(49.0, -5) == 49.0);
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
