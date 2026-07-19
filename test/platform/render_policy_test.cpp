// Superseded-frame render-skip policy test (GitHub issue #9 / Task 63).
//
// Like audio_pacing_test / present_cadence_test this file has NO VHDL oracle:
// it tests a *host* presentation policy, not emulated hardware. Its oracle is
// the stated contract in src/platform/render_policy.h:
//
//   * a frontend tick presents ONCE, after its frame loops, so only the
//     tick's LAST frame can reach the screen — it MUST composite;
//   * every earlier frame of the tick is superseded before any paint, so its
//     compositor pass is skipped as pure waste;
//   * a tick with a due --delayed-screenshot composites every frame
//     (`force_all`).
//
// Every row below derives from that contract, never from reading the
// implementation back.
//
// Run: ./build/test/render_policy_test

#include "platform/render_policy.h"

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

using render_policy::composite_frame_in_tick;

}  // namespace

int main()
{
    std::printf("render_policy_test (superseded-composite skip, issue #9 / Task 63)\n");

    // --- RP-01: single-frame tick always composites ------------------------
    // The common case: one frame per tick. It is the tick's last frame, so it
    // must composite — the skip must be a no-op when no multi-frame tick
    // occurs.
    check("RP-01", "the only frame of a 1-frame tick composites",
          composite_frame_in_tick(0, 1, false));

    // --- RP-02: audio-pacing double tick -----------------------------------
    // frames_for_tick() == 2 (catch-up): frame 0 is superseded by frame 1
    // inside the same tick, so frame 0 skips and frame 1 composites.
    check("RP-02a", "frame 0 of a 2-frame tick is superseded: skip",
          !composite_frame_in_tick(0, 2, false));
    check("RP-02b", "frame 1 of a 2-frame tick is the presented one: composite",
          composite_frame_in_tick(1, 2, false));

    // --- RP-03: n-frame burst: only the last composites --------------------
    // Fastload-burst scale (up to 200 frames per tick): every frame before
    // the last skips, the last composites.
    {
        bool intermediates_skip = true;
        for (int i = 0; i < 199; i++)
            if (composite_frame_in_tick(i, 200, false)) intermediates_skip = false;
        check("RP-03a", "frames 0..198 of a 200-frame tick all skip",
              intermediates_skip);
        check("RP-03b", "frame 199 of a 200-frame tick composites",
              composite_frame_in_tick(199, 200, false));
    }

    // --- RP-04: a frame with more frames guaranteed to follow skips --------
    // The burst-loop wiring form: a loop frame is frame i of a tick with at
    // least i+2 frames (the closing frame follows by construction), so it
    // skips regardless of i.
    check("RP-04", "frame i of a tick with >= i+2 frames skips (burst loop form)",
          !composite_frame_in_tick(0, 2, false) &&
          !composite_frame_in_tick(7, 9, false) &&
          !composite_frame_in_tick(198, 200, false));

    // --- RP-05: screenshot force composites everything ---------------------
    // A due --delayed-screenshot needs the whole tick through the compositor
    // with the layer mask armed: force_all overrides every skip.
    check("RP-05a", "force_all composites a superseded frame",
          composite_frame_in_tick(0, 2, true));
    check("RP-05b", "force_all composites deep burst intermediates",
          composite_frame_in_tick(50, 200, true));
    check("RP-05c", "force_all still composites the final frame",
          composite_frame_in_tick(1, 2, true));

    // --- RP-06: the final frame composites for EVERY tick size -------------
    // The invariant the presented stream depends on: whatever n, frame n-1
    // composites — including degenerate n <= 1.
    {
        bool final_always = composite_frame_in_tick(0, 0, false) &&
                            composite_frame_in_tick(0, 1, false);
        for (int n = 2; n <= 201; n++)
            if (!composite_frame_in_tick(n - 1, n, false)) final_always = false;
        check("RP-06", "the final frame composites for every tick size (n=0..201)",
              final_always);
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
