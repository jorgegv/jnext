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
// Issue #303 adds the macOS policy (DeferredWarpPolicy), whose oracle is the
// host hardware itself: a window-server model below moves the cursor, queues
// events and applies warps the way each platform does, and the guest must
// follow the hardware exactly (PCAP-11..21).
//
// Run: ./build/test/pointer_capture_test

#include "platform/pointer_capture.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
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

// ---------------------------------------------------------------------------
// Issue #303 — a model of the host window server, so the macOS failure can be
// reproduced off macOS. The hardware moves the cursor; every move queues an
// event carrying the cursor position AT GENERATION; the app drains the queue
// every `batch` moves (batch > 1 = the app is behind, so events read after a
// warp request still predate it). A warp request is handled per platform:
//
//   SyncEcho   — X11 XWarpPointer / Windows SetCursorPos: lands at once and
//                queues a motion event at the centre.
//   SyncSilent — macOS CGWarpMouseCursorPosition: lands at once, no event
//                (Apple, "Controlling the Mouse Cursor": "does not generate a
//                mouse event").
//   PostedLate — macOS QCursor::setPos = CGEventPost (qcocoacursor.mm): lands
//                `lag` moves later, and its own event is queued then.
//   Dropped    — the same CGEventPost without the Accessibility permission:
//                silently discarded (macOS >= 10.14), never lands.
//
// The oracle is the hardware itself: after each event the guest must have
// moved exactly as far as the hardware had when that event was generated.
// ---------------------------------------------------------------------------
enum class Warp { SyncEcho, SyncSilent, PostedLate, Dropped };

struct SimResult {
    long max_dev    = 0;  ///< worst |guest - hardware| over all events, any axis
    long max_offset = 0;  ///< worst |cursor - centre| once a warp has landed, any axis
    long max_guest_x = 0; ///< furthest the guest travelled right
};

using Decide = std::function<pointer_capture::Motion(int x, int y, int cx, int cy)>;

// begin() is the caller's: call it on the policy before simulate().
SimResult simulate(Warp warp, int lag, int batch, const Decide& decide, bool stale_click = true)
{
    constexpr int CX = 400, CY = 300;
    struct Ev { int x, y; long tx, ty; };
    std::deque<Ev> queue;
    std::vector<int> posted;              // move index each posted warp lands at
    int  cur_x = CX + 230, cur_y = CY - 170;   // pointer where the user clicked
    long tx = 0, ty = 0;                  // hardware motion since capture
    long gx = 0, gy = 0;                  // what the guest was fed
    int  step = 0;
    bool landed = false;
    bool first  = true;
    long base_x = 0, base_y = 0;          // motion carried by the discarded first event
    SimResult r;

    auto land = [&] {
        cur_x = CX; cur_y = CY; landed = true;
    };
    auto request_warp = [&] {
        switch (warp) {
        case Warp::SyncEcho:   land(); queue.push_back({CX, CY, tx, ty}); break;
        case Warp::SyncSilent: land(); break;
        case Warp::PostedLate: posted.push_back(step + lag); break;
        case Warp::Dropped:    break;
        }
    };
    auto drain = [&] {
        while (!queue.empty()) {
            const Ev e = queue.front();
            queue.pop_front();
            // The first event of a capture is dropped by design (issue #37):
            // whatever motion it carried is not owed to the guest.
            if (first) { base_x = e.tx; base_y = e.ty; first = false; }
            const auto m = decide(e.x, e.y, CX, CY);
            if (m.forward) { gx += m.dx; gy += m.dy; }
            if (m.recentre) request_warp();
            r.max_dev = std::max(r.max_dev, std::max(std::labs(gx - (e.tx - base_x)),
                                                     std::labs(gy - (e.ty - base_y))));
            r.max_guest_x = std::max(r.max_guest_x, gx);
        }
    };
    auto move = [&](int dx, int dy) {
        for (auto it = posted.begin(); it != posted.end();) {
            if (*it <= step) { land(); queue.push_back({CX, CY, tx, ty}); it = posted.erase(it); }
            else ++it;
        }
        cur_x += dx; cur_y += dy; tx += dx; ty += dy;
        queue.push_back({cur_x, cur_y, tx, ty});
        if (landed)
            r.max_offset = std::max(r.max_offset,
                                    (long)std::max(std::abs(cur_x - CX), std::abs(cur_y - CY)));
        if (++step % batch == 0) drain();
    };

    // The click that captured the mouse may have left a move event queued at
    // the old position (PCAP-03's case).
    if (stale_click) queue.push_back({cur_x, cur_y, 0, 0});
    request_warp();                        // MainWindow::set_mouse_captured
    struct Run { int n, dx, dy; };
    const Run runs[] = { {40, 9, 2}, {30, -12, 5}, {50, 3, -11}, {25, -7, -7},
                         {20, 11, 0}, {20, 0, -10}, {16, -1, 1}, {30, 10, 10} };
    for (const auto& run : runs)
        for (int i = 0; i < run.n; ++i) move(run.dx, run.dy);
    drain();
    return r;
}

constexpr int SIM_MARGIN = 64;   // a 256-px-high viewport (MainWindow: min(w,h)/4)

SimResult sim_deferred(Warp warp, int lag, int batch, bool stale_click = true)
{
    pointer_capture::DeferredWarpPolicy p;
    p.begin();
    return simulate(warp, lag, batch, [&](int x, int y, int cx, int cy) {
        return p.on_motion(x, y, cx, cy, SIM_MARGIN);
    }, stale_click);
}

SimResult sim_centre(Warp warp, int lag, int batch)
{
    pointer_capture::Policy p;
    p.begin();
    return simulate(warp, lag, batch, [&](int x, int y, int cx, int cy) {
        return p.on_motion(x, y, cx, cy);
    });
}

std::string fmt_sim(const SimResult& r)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "max_dev=%ld max_offset=%ld max_guest_x=%ld",
                  r.max_dev, r.max_offset, r.max_guest_x);
    return buf;
}

}  // namespace

int main()
{
    std::printf("Pointer-capture motion policy tests (issues #37, #303)\n");
    std::printf("==============================================\n\n");

    constexpr int CX = 320, CY = 256;

    // PCAP-01: steady-state motion is forwarded as the delta from the centre,
    // and the pointer is re-centred so the next delta is measured afresh.
    {
        auto p = settled(CX, CY);
        const auto m = p.on_motion(CX + 7, CY - 3, CX, CY);
        check("PCAP-01", "delta measured from the centre, then re-centre",
              m.forward && m.dx == 7 && m.dy == -3 && m.recentre, fmt(m));
    }

    // PCAP-02: the warp echo — an event exactly AT the centre — is not motion
    // and must not provoke another warp.
    {
        auto p = settled(CX, CY);
        const auto m = p.on_motion(CX, CY, CX, CY);
        check("PCAP-02", "event at the centre is the warp echo: no forward, no re-centre",
              !m.forward && !m.recentre, fmt(m));
    }

    // PCAP-03: THE issue-#37 regression row. Capture starting with the pointer
    // far from the centre must not hurl the guest pointer across the screen:
    // that first delta is the distance from the pre-warp position, not user
    // motion. It is dropped — but the pointer is still re-centred.
    {
        pointer_capture::Policy p;
        p.begin();
        const auto m = p.on_motion(CX + 400, CY - 250, CX, CY);
        check("PCAP-03", "first event after capture is discarded, but still re-centres",
              !m.forward && m.recentre, fmt(m));
    }

    // PCAP-04: and only the FIRST one is discarded — real motion right after
    // capture must get through, or the mouse would feel dead on capture.
    {
        pointer_capture::Policy p;
        p.begin();
        p.on_motion(CX + 400, CY - 250, CX, CY);      // discarded
        const auto m = p.on_motion(CX - 5, CY + 9, CX, CY);
        check("PCAP-04", "the second event after capture is forwarded normally",
              m.forward && m.dx == -5 && m.dy == 9 && m.recentre, fmt(m));
    }

    // PCAP-05: the discard is armed by begin(), not by construction — a policy
    // that was never told a capture started has nothing to drop.
    {
        pointer_capture::Policy p;
        check("PCAP-05a", "fresh policy has no discard armed", !p.discard_armed());
        p.begin();
        check("PCAP-05b", "begin() arms the discard", p.discard_armed());
        p.on_motion(CX + 2, CY, CX, CY);
        check("PCAP-05c", "the discard disarms after one event", !p.discard_armed());
    }

    // PCAP-06: re-capturing re-arms. Each capture starts from an arbitrary
    // pointer position, so every one needs its own stale first delta dropped
    // — not just the first capture of the session.
    {
        auto p = settled(CX, CY);
        p.begin();                                     // captured again
        const auto m = p.on_motion(CX + 300, CY + 300, CX, CY);
        check("PCAP-06", "each capture re-arms the discard",
              !m.forward && m.recentre, fmt(m));
    }

    // PCAP-07: a zero-delta event that is NOT the warp echo cannot occur (the
    // echo is the only way to land on the centre), so the centre check and
    // the zero-delta check must not be conflated: an off-centre event with a
    // zero component still forwards.
    {
        auto p = settled(CX, CY);
        const auto m = p.on_motion(CX + 4, CY, CX, CY);
        check("PCAP-07", "motion along one axis only is still forwarded",
              m.forward && m.dx == 4 && m.dy == 0, fmt(m));
    }

    // PCAP-08: negative travel in both axes — guards against an unsigned or
    // absolute-value slip, which would break up/left movement only.
    {
        auto p = settled(CX, CY);
        const auto m = p.on_motion(CX - 11, CY - 13, CX, CY);
        check("PCAP-08", "negative deltas are preserved with sign",
              m.forward && m.dx == -11 && m.dy == -13, fmt(m));
    }

    // PCAP-09: the centre is a parameter, not a constant — the viewport moves
    // when the window is moved or rescaled, and the delta must follow it.
    {
        pointer_capture::Policy p;
        p.begin();
        p.on_motion(50, 50, 40, 60);                   // discarded
        const auto m = p.on_motion(46, 63, 40, 60);
        check("PCAP-09", "delta is relative to the supplied centre, not a fixed one",
              m.forward && m.dx == 6 && m.dy == 3, fmt(m));
    }

    // PCAP-10: the echo check must compare BOTH axes. PCAP-07 covers horizontal
    // motion at the vertical centre; this is its mirror — vertical motion
    // while x happens to equal the centre's x. Without it, an echo check that
    // tested only x would treat every such event as the warp echo and drop
    // real vertical motion. (Found by review mutation: that exact defect
    // passed all previous rows.)
    {
        auto p = settled(CX, CY);
        const auto m = p.on_motion(CX, CY - 6, CX, CY);
        check("PCAP-10", "vertical motion at the centre's x is real motion, not the echo",
              m.forward && m.dx == 0 && m.dy == -6 && m.recentre, fmt(m));
    }

    // ── Issue #303: macOS (DeferredWarpPolicy, the policy MainWindow uses
    //    there). The simulated scenarios are first shown to reproduce the
    //    reported erratic motion with the centre-referenced Policy (PCAP-14*),
    //    so the exact-tracking rows below cannot pass vacuously.

    // PCAP-14a/b/c: the reproduction. Measuring every event from the centre
    // over-counts whenever an event predates the warp — QCursor::setPos landing
    // late, never landing, or an app that is behind its queue.
    {
        const auto a = sim_centre(Warp::PostedLate, 3, 1);
        check("PCAP-14a", "#303 reproduced: centre-referenced deltas drift when setPos lands late",
              a.max_dev > SIM_MARGIN, fmt_sim(a));
        const auto b = sim_centre(Warp::Dropped, 0, 1);
        check("PCAP-14b", "#303 reproduced: centre-referenced deltas run away when setPos is dropped",
              b.max_dev > SIM_MARGIN, fmt_sim(b));
        const auto c = sim_centre(Warp::SyncSilent, 0, 4);
        check("PCAP-14c", "#303 reproduced: centre-referenced deltas re-count stale pre-warp events",
              c.max_dev > SIM_MARGIN, fmt_sim(c));
    }

    // PCAP-11/12/13: the fix. Under every one of those macOS behaviours the
    // guest follows the hardware EXACTLY, at every event.
    {
        const auto a = sim_deferred(Warp::PostedLate, 3, 1);
        check("PCAP-11", "deferred policy tracks the hardware exactly when the warp lands late",
              a.max_dev == 0, fmt_sim(a));
        const auto b = sim_deferred(Warp::Dropped, 0, 1);
        check("PCAP-12", "deferred policy tracks the hardware exactly when the warp never lands",
              b.max_dev == 0, fmt_sim(b));
        const auto c = sim_deferred(Warp::SyncSilent, 0, 4);
        check("PCAP-13", "deferred policy tracks the hardware exactly across stale pre-warp events",
              c.max_dev == 0, fmt_sim(c));
    }

    // PCAP-15: and it still re-centres — with the production warp
    // (CGWarpMouseCursorPosition) the host pointer stays within margin plus one
    // batch of travel of the centre while the guest travels far beyond it.
    // Without this, "never warp" would satisfy PCAP-11..13 and leave the
    // pointer to hit the screen edge (the issue #37 symptom).
    {
        const auto c = sim_deferred(Warp::SyncSilent, 0, 4);
        check("PCAP-15", "deferred policy keeps the pointer near the centre; guest travel unbounded",
              c.max_offset <= SIM_MARGIN + 4 * 12 &&
              c.max_guest_x > 3 * SIM_MARGIN, fmt_sim(c));
    }

    // PCAP-16: inside the margin the pointer is left alone — warps are rare,
    // which is what keeps a pre-warp and a post-warp event far apart.
    {
        pointer_capture::DeferredWarpPolicy p;
        p.begin();
        p.on_motion(CX, CY, CX, CY, 64);              // the warp landed
        const auto m = p.on_motion(CX + 10, CY - 4, CX, CY, 64);
        check("PCAP-16", "motion inside the margin is forwarded without a re-centre",
              m.forward && m.dx == 10 && m.dy == -4 && !m.recentre, fmt(m));
    }

    // PCAP-17: capture start, as in PCAP-03: the first event is never forwarded
    // (its position may be wherever the pointer was). Beyond the margin it is
    // that stale position, so the begin() warp is still awaited and no second
    // warp is requested — two in flight cannot be told apart (PCAP-13 fails
    // with it). Within the margin it follows the warp.
    {
        pointer_capture::DeferredWarpPolicy p;
        p.begin();
        const auto m = p.on_motion(CX + 300, CY + 200, CX, CY, 64);
        check("PCAP-17a", "first event after capture is not forwarded; the begin() warp is still awaited",
              !m.forward && !m.recentre && p.warp_pending(), fmt(m));
        pointer_capture::DeferredWarpPolicy q;
        q.begin();
        const auto e = q.on_motion(CX + 9, CY - 2, CX, CY, 64);
        check("PCAP-17b", "a first event within the margin shows the warp landed",
              !e.forward && !e.recentre && !q.warp_pending(), fmt(e));
    }

    // PCAP-18: while a warp is outstanding, an event near the last position
    // predates it (delta from the last position, still pending); an event near
    // the centre follows it (delta from the centre, no longer pending).
    {
        pointer_capture::DeferredWarpPolicy p;
        p.begin();
        p.on_motion(CX, CY, CX, CY, 64);              // landed
        p.on_motion(CX + 60, CY, CX, CY, 64);         // inside the margin
        const auto w = p.on_motion(CX + 70, CY, CX, CY, 64);   // beyond: warp
        const auto s = p.on_motion(CX + 75, CY + 1, CX, CY, 64);
        check("PCAP-18a", "a pre-warp event is measured from the last position",
              w.recentre && s.forward && s.dx == 5 && s.dy == 1 && p.warp_pending(), fmt(s));
        const auto a = p.on_motion(CX - 3, CY + 2, CX, CY, 64);
        check("PCAP-18b", "a post-warp event is measured from the centre",
              a.forward && a.dx == -3 && a.dy == 2 && !a.recentre && !p.warp_pending(), fmt(a));
    }

    // PCAP-19: every capture re-arms the first-event discard.
    {
        pointer_capture::DeferredWarpPolicy p;
        p.begin();
        p.on_motion(CX, CY, CX, CY, 64);
        p.on_motion(CX + 5, CY, CX, CY, 64);
        p.begin();                                     // captured again
        const auto m = p.on_motion(CX - 250, CY + 40, CX, CY, 64);
        check("PCAP-19", "each capture re-arms the first-event discard",
              !m.forward, fmt(m));
    }

    // PCAP-21: the production macOS case at its plainest — the
    // CGWarpMouseCursorPosition warp lands before the first move and sends no
    // event, and nothing stale is queued. The first move is then already
    // post-warp; if it were taken for a stale one, the warp would never be
    // seen to land, re-centring would stop, and the pointer would wander off
    // the viewport (max_offset unbounded).
    {
        const auto c = sim_deferred(Warp::SyncSilent, 0, 1, /*stale_click=*/false);
        check("PCAP-21", "deferred policy re-centres when the begin() warp landed silently",
              c.max_dev == 0 && c.max_offset <= SIM_MARGIN + 12, fmt_sim(c));
    }

    // PCAP-20: why X11 and Windows keep the centre-referenced Policy: their
    // warp lands at once and echoes, so with an app that keeps up it tracks
    // the hardware exactly.
    {
        const auto d = sim_centre(Warp::SyncEcho, 0, 1);
        check("PCAP-20", "centre-referenced policy is exact for a synchronous, echoing warp",
              d.max_dev == 0, fmt_sim(d));
    }

    std::printf("\n==============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
