// EmulatorWidget present-count gate test (GitHub issue #9 / Task 63).
//
// This suite covers the load-bearing half of the present-cadence diagnostic:
// the widget-side rule that decides what counts as a frame reaching the
// screen. The arithmetic built on top of it is tested separately in
// test/platform/present_cadence_test.cpp; that arithmetic is only as honest
// as the `presented` input this file pins.
//
// WHAT IS UNDER TEST. EmulatorWidget::present_count() must count paintEvents
// that serve a framebuffer HANDED TO IT AND NOT YET PAINTED — not raw
// paintEvents. Qt also paints on expose, resize, move and explicit repaint(),
// and counting those as presented frames is what drove the old `dropped`
// figure NEGATIVE (presented could exceed emulated, and the subtraction
// wrapped a uint64). The `frame_pending_` gate in update_frame()/paintEvent()
// is what makes the distinction, and it is what this suite discriminates:
// replace the gate with an unconditional `++present_count_;` and PC-G03
// fails immediately.
//
// A NOTE ON WHY THIS SUITE EXISTS AT ALL. It was first claimed that this gate
// could not be tested on the dev host, because driving a real window manager
// (Xvfb, a real X display, 400 scripted xdotool window moves) produced no
// spontaneous expose paints and therefore could not tell the gate from a raw
// count. That conclusion was wrong: a bare repaint() call is a paintEvent that
// serves no new frame, which is exactly the discriminative stimulus, and it
// needs no window manager at all — only the offscreen QPA platform already
// used by the debugger panel suites. One failed approach is not a proof of
// impossibility.
//
// Qt is required (EmulatorWidget is a real QWidget), but no display is:
// main() forces the offscreen QPA platform.
// Run: ./build/test/present_count_test

#include "gui/emulator_widget.h"

#include <QApplication>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
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

std::string got(uint64_t v)
{
    return "present_count=" + std::to_string(v);
}

// A framebuffer of the emulator's native size, filled with a solid colour so
// successive frames can be made identical or different at will.
constexpr int FB_W = 640;
constexpr int FB_H = 256;

std::vector<uint32_t> make_fb(uint32_t argb)
{
    return std::vector<uint32_t>(static_cast<size_t>(FB_W) * FB_H, argb);
}

// Let the offscreen platform deliver any pending paintEvent.
void settle(int rounds = 3)
{
    for (int i = 0; i < rounds; i++) QApplication::processEvents();
}

}  // namespace

int main(int argc, char* argv[])
{
    // No display required — same idiom as test/debugger/quit_gate_test.cpp
    // and the debugger panel suites.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("EmulatorWidget present-count gate tests (issue #9 / Task 63)\n");
    std::printf("============================================================\n");

    EmulatorWidget w;
    w.show();
    settle();

    const auto frame_a = make_fb(0xFF204060);
    const auto frame_b = make_fb(0xFF901030);

    // --- PC-G01: no frame handed over yet -------------------------------------
    // show() may itself provoke paintEvents. None of them served an emulator
    // frame, so none may be counted.
    check("PC-G01", "a shown widget that has received no frame has presented none",
          w.present_count() == 0, got(w.present_count()));

    // --- PC-G02: one frame in, one present out --------------------------------
    {
        w.update_frame(frame_a.data(), FB_W, FB_H);
        settle();
        check("PC-G02", "one update_frame(), once painted, counts exactly one present",
              w.present_count() == 1, got(w.present_count()));
    }

    // --- PC-G03: THE DISCRIMINATIVE ROW ---------------------------------------
    // A bare repaint() is a real, immediate paintEvent that serves NO new
    // frame. Counting it would be counting a re-blit of an image already
    // shown — the over-count that drove `dropped` negative.
    //
    // Mutate the gate in EmulatorWidget::paintEvent to an unconditional
    // `++present_count_;` and this row fails (count reaches 3, not 1).
    {
        const uint64_t before = w.present_count();
        w.repaint();
        w.repaint();
        settle();
        check("PC-G03", "repaint() with no new frame presents nothing",
              w.present_count() == before, got(w.present_count()));
    }

    // --- PC-G04: the counter keeps advancing, one per served frame -------------
    {
        w.update_frame(frame_b.data(), FB_W, FB_H);
        settle();
        const uint64_t after_first = w.present_count();
        w.update_frame(frame_a.data(), FB_W, FB_H);
        settle();
        check("PC-G04", "each subsequent frame, once painted, adds exactly one",
              after_first == 2 && w.present_count() == 3,
              got(w.present_count()) + " after_first=" + std::to_string(after_first));
    }

    // --- PC-G05: frames superseded before any paint are NOT presented ---------
    // Several update_frame() calls without letting Qt paint: update() requests
    // are coalesced, so only the last frame's image is ever shown. The count
    // must rise by ONE, not by three. This is the widget-level expression of
    // the `superseded` bucket, and it is why a 2-frame audio-catch-up tick
    // reports one present rather than two.
    {
        const uint64_t before = w.present_count();
        w.update_frame(frame_b.data(), FB_W, FB_H);
        w.update_frame(frame_a.data(), FB_W, FB_H);
        w.update_frame(frame_b.data(), FB_W, FB_H);
        settle();
        check("PC-G05", "three frames handed over but painted once count as one present",
              w.present_count() == before + 1, got(w.present_count()));
    }

    // --- PC-G06: freshness is DECLARED by the caller, not inferred -------------
    // The widget does not compare pixels (a per-frame memcmp would cost more
    // than the diagnostic is worth), so two byte-identical frames declared as
    // new content count twice. That is the contract: the caller knows whether
    // it composited anything, and says so.
    {
        const uint64_t before = w.present_count();
        w.update_frame(frame_a.data(), FB_W, FB_H, /*new_content=*/true);
        settle();
        w.update_frame(frame_a.data(), FB_W, FB_H, /*new_content=*/true);
        settle();
        check("PC-G06", "identical frames declared new both count (no pixel compare)",
              w.present_count() == before + 2, got(w.present_count()));
    }

    // --- PC-G08: a STALE re-push is painted but not counted --------------------
    // THE ROW THAT CLOSES THE PAUSE/RESUME HOLE. While the debugger is paused
    // (or the audio pacer skips a tick) the frontend re-pushes the SAME
    // framebuffer every tick. Counting those inflates `presented`, and a
    // reporting window straddling pause->resume then cancels real drops
    // against that inflation — `lost` reads 0 while frames are being lost.
    // See the arithmetic-side reconstruction in PC-13.
    //
    // Mutate update_frame() to arm frame_pending_ unconditionally (i.e. ignore
    // new_content) and this row fails.
    {
        const uint64_t before = w.present_count();
        for (int i = 0; i < 5; i++) {
            w.update_frame(frame_a.data(), FB_W, FB_H, /*new_content=*/false);
            settle();
        }
        check("PC-G08a", "stale re-pushes present nothing, however many",
              w.present_count() == before, got(w.present_count()));

        // ...and the presentation itself is NOT suppressed: a genuinely new
        // frame straight after a stale run still reaches the screen normally.
        w.update_frame(frame_b.data(), FB_W, FB_H, /*new_content=*/true);
        settle();
        check("PC-G08b", "a new frame after stale re-pushes still presents",
              w.present_count() == before + 1, got(w.present_count()));
    }

    // --- PC-G09: a stale re-push must not swallow a frame already pending -----
    // The flag is sticky until served. A frame pushed at the end of one tick
    // may still be unpainted when the next tick re-pushes it as stale; that
    // pending frame must still be counted when its paint finally arrives.
    // Guards against "clear the flag" rather than "don't set it".
    {
        const uint64_t before = w.present_count();
        w.update_frame(frame_a.data(), FB_W, FB_H, /*new_content=*/true);
        w.update_frame(frame_a.data(), FB_W, FB_H, /*new_content=*/false);
        settle();
        check("PC-G09", "a stale re-push does not cancel a frame already pending",
              w.present_count() == before + 1, got(w.present_count()));
    }

    // --- PC-G10..G14: the paint-cost samples (Task 63 tick-stats) --------------
    // Spec (tick-delivery diagnostics): the widget accumulates the duration of
    // each paintEvent that CONSUMES a pending new frame, drained via
    // take_paint_stats(). Spontaneous repaints of an already-shown frame and
    // stale re-pushes must NOT pollute the stats — mechanism (c) of issue #9
    // is "presenting a NEW frame is too slow", and re-blit costs of frames the
    // emulator is not producing would dilute exactly the ticks under
    // suspicion.

    // --- PC-G10: one served frame, one sample; draining resets ----------------
    {
        w.take_paint_stats();  // discard everything the rows above accumulated
        w.update_frame(frame_a.data(), FB_W, FB_H, /*new_content=*/true);
        settle();
        const tick_stats::Stat s = w.take_paint_stats();
        check("PC-G10a", "one served frame yields exactly one paint sample",
              s.count == 1, "count=" + std::to_string(s.count));
        check("PC-G10b", "the sample is a sane duration (0 <= min <= max)",
              s.min >= 0 && s.min <= s.max,
              "min=" + std::to_string(s.min) + " max=" + std::to_string(s.max));
        const tick_stats::Stat drained = w.take_paint_stats();
        check("PC-G10c", "take_paint_stats() drains: a second take is empty",
              drained.count == 0, "count=" + std::to_string(drained.count));
    }

    // --- PC-G11: spontaneous repaints add no sample ---------------------------
    // A bare repaint() is a real paintEvent that serves no new frame — the
    // discriminative stimulus of PC-G03, applied to the paint stats.
    {
        w.repaint();
        w.repaint();
        settle();
        const tick_stats::Stat s = w.take_paint_stats();
        check("PC-G11", "repaint() of an already-shown frame samples nothing",
              s.count == 0, "count=" + std::to_string(s.count));
    }

    // --- PC-G12: stale re-pushes add no sample --------------------------------
    // A paused frontend re-pushes the same framebuffer every tick
    // (new_content=false). Those paints happen, but they present nothing new
    // and must not be sampled.
    {
        for (int i = 0; i < 5; i++) {
            w.update_frame(frame_a.data(), FB_W, FB_H, /*new_content=*/false);
            settle();
        }
        const tick_stats::Stat s = w.take_paint_stats();
        check("PC-G12", "stale re-pushes are painted but never sampled",
              s.count == 0, "count=" + std::to_string(s.count));
    }

    // --- PC-G13: one sample per served frame ----------------------------------
    {
        for (int i = 0; i < 3; i++) {
            w.update_frame(i % 2 ? frame_a.data() : frame_b.data(), FB_W, FB_H,
                           /*new_content=*/true);
            settle();
        }
        const tick_stats::Stat s = w.take_paint_stats();
        check("PC-G13", "three frames served one by one yield three samples",
              s.count == 3, "count=" + std::to_string(s.count));
    }

    // --- PC-G14: coalesced pushes yield one sample ----------------------------
    // Three pushes, one paint (Qt coalesces update()): only the frame that is
    // actually served is sampled — the widget-level mirror of PC-G05.
    {
        w.update_frame(frame_a.data(), FB_W, FB_H, /*new_content=*/true);
        w.update_frame(frame_b.data(), FB_W, FB_H, /*new_content=*/true);
        w.update_frame(frame_a.data(), FB_W, FB_H, /*new_content=*/true);
        settle();
        const tick_stats::Stat s = w.take_paint_stats();
        check("PC-G14", "three coalesced pushes painted once sample once",
              s.count == 1, "count=" + std::to_string(s.count));
    }

    // --- PC-G07: a rejected frame presents nothing ----------------------------
    // update_frame() ignores a null/degenerate framebuffer. Nothing was handed
    // over, so nothing may be counted — and no stale pending flag may leak
    // into the next genuine paint.
    {
        const uint64_t before = w.present_count();
        w.update_frame(nullptr, FB_W, FB_H);
        w.update_frame(frame_a.data(), 0, FB_H);
        w.update_frame(frame_a.data(), FB_W, -1);
        settle();
        check("PC-G07", "a rejected frame is never counted as presented",
              w.present_count() == before, got(w.present_count()));
    }

    std::printf("\n============================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
