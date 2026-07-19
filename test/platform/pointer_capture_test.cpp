// Pointer-capture motion policy test (GitHub issue #37).
//
// Like render_policy_test / audio_pacing_test this file has NO VHDL oracle: it
// tests a *host* input policy, not emulated hardware. Its oracle is the stated
// contract in src/platform/pointer_capture.h:
//
//   * a Kempston mouse is RELATIVE, so the host pointer is pinned to the
//     viewport centre and the delta measured from that centre is what the
//     guest sees — the pointer never runs out of room, which is the whole
//     point of issue #37;
//   * the re-centring warp echoes back as a motion event AT the centre: it
//     carries no motion and must not trigger another warp;
//   * capture can begin with the pointer anywhere, so the first event after
//     capture is measured against a stale pre-warp position — its delta is
//     the distance from wherever the pointer happened to be and must be
//     dropped, while still re-centring so the NEXT delta is correct.
//
// Every row below derives from that contract, never from reading the
// implementation back.
//
// Run: ./build/test/pointer_capture_test

#include "platform/pointer_capture.h"

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

std::string fmt(const pointer_capture::Motion& m)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "forward=%d dx=%d dy=%d recentre=%d",
                  m.forward ? 1 : 0, m.dx, m.dy, m.recentre ? 1 : 0);
    return buf;
}

// A capture already past its first (discarded) event — the steady state.
pointer_capture::Policy settled(int cx, int cy)
{
    pointer_capture::Policy p;
    p.begin();
    p.on_motion(cx + 1, cy + 1, cx, cy);   // consume the discarded first event
    return p;
}

}  // namespace

int main()
{
    std::printf("Pointer-capture motion policy tests (issue #37)\n");
    std::printf("==============================================\n\n");

    constexpr int CX = 320, CY = 256;

    // PC-01: steady-state motion is forwarded as the delta from the centre,
    // and the pointer is re-centred so the next delta is measured afresh.
    {
        auto p = settled(CX, CY);
        const auto m = p.on_motion(CX + 7, CY - 3, CX, CY);
        check("PC-01", "delta measured from the centre, then re-centre",
              m.forward && m.dx == 7 && m.dy == -3 && m.recentre, fmt(m));
    }

    // PC-02: the warp echo — an event exactly AT the centre — is not motion
    // and must not provoke another warp.
    {
        auto p = settled(CX, CY);
        const auto m = p.on_motion(CX, CY, CX, CY);
        check("PC-02", "event at the centre is the warp echo: no forward, no re-centre",
              !m.forward && !m.recentre, fmt(m));
    }

    // PC-03: THE issue-#37 regression row. Capture starting with the pointer
    // far from the centre must not hurl the guest pointer across the screen:
    // that first delta is the distance from the pre-warp position, not user
    // motion. It is dropped — but the pointer is still re-centred.
    {
        pointer_capture::Policy p;
        p.begin();
        const auto m = p.on_motion(CX + 400, CY - 250, CX, CY);
        check("PC-03", "first event after capture is discarded, but still re-centres",
              !m.forward && m.recentre, fmt(m));
    }

    // PC-04: and only the FIRST one is discarded — real motion right after
    // capture must get through, or the mouse would feel dead on capture.
    {
        pointer_capture::Policy p;
        p.begin();
        p.on_motion(CX + 400, CY - 250, CX, CY);      // discarded
        const auto m = p.on_motion(CX - 5, CY + 9, CX, CY);
        check("PC-04", "the second event after capture is forwarded normally",
              m.forward && m.dx == -5 && m.dy == 9 && m.recentre, fmt(m));
    }

    // PC-05: the discard is armed by begin(), not by construction — a policy
    // that was never told a capture started has nothing to drop.
    {
        pointer_capture::Policy p;
        check("PC-05a", "fresh policy has no discard armed", !p.discard_armed());
        p.begin();
        check("PC-05b", "begin() arms the discard", p.discard_armed());
        p.on_motion(CX + 2, CY, CX, CY);
        check("PC-05c", "the discard disarms after one event", !p.discard_armed());
    }

    // PC-06: re-capturing re-arms. Each capture starts from an arbitrary
    // pointer position, so every one needs its own stale first delta dropped
    // — not just the first capture of the session.
    {
        auto p = settled(CX, CY);
        p.begin();                                     // captured again
        const auto m = p.on_motion(CX + 300, CY + 300, CX, CY);
        check("PC-06", "each capture re-arms the discard",
              !m.forward && m.recentre, fmt(m));
    }

    // PC-07: a zero-delta event that is NOT the warp echo cannot occur (the
    // echo is the only way to land on the centre), so the centre check and
    // the zero-delta check must not be conflated: an off-centre event with a
    // zero component still forwards.
    {
        auto p = settled(CX, CY);
        const auto m = p.on_motion(CX + 4, CY, CX, CY);
        check("PC-07", "motion along one axis only is still forwarded",
              m.forward && m.dx == 4 && m.dy == 0, fmt(m));
    }

    // PC-08: negative travel in both axes — guards against an unsigned or
    // absolute-value slip, which would break up/left movement only.
    {
        auto p = settled(CX, CY);
        const auto m = p.on_motion(CX - 11, CY - 13, CX, CY);
        check("PC-08", "negative deltas are preserved with sign",
              m.forward && m.dx == -11 && m.dy == -13, fmt(m));
    }

    // PC-09: the centre is a parameter, not a constant — the viewport moves
    // when the window is moved or rescaled, and the delta must follow it.
    {
        pointer_capture::Policy p;
        p.begin();
        p.on_motion(50, 50, 40, 60);                   // discarded
        const auto m = p.on_motion(46, 63, 40, 60);
        check("PC-09", "delta is relative to the supplied centre, not a fixed one",
              m.forward && m.dx == 6 && m.dy == 3, fmt(m));
    }

    std::printf("\n==============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
