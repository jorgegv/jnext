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
// follow the hardware exactly (PCAP-11..32; Wayland selection PCAP-22).
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
//   PostedSilent — Wayland wp_pointer_warp_v1 (Qt >= 6.10): a request the
//                compositor honours later or not at all ("implementation
//                defined"), and the protocol promises no motion event for it.
enum class Warp { SyncEcho, SyncSilent, PostedLate, Dropped, PostedSilent };

struct Run { int n, dx, dy; };

// The default path: a mixed walk that keeps moving away and drifting.
const std::vector<Run> WALK = { {40, 9, 2}, {30, -12, 5}, {50, 3, -11}, {25, -7, -7},
                                {20, 11, 0}, {20, 0, -10}, {16, -1, 1}, {30, 10, 10} };
// Back and forth THROUGH the centre: with a warp that never lands (Wayland,
// Qt <= 6.9) the pointer keeps passing near the centre, where a pre-warp and
// a post-warp reading of the same event are closest together.
const std::vector<Run> CROSSING = { {30, 10, 3}, {60, -10, -3}, {60, 10, 3}, {60, -10, -2},
                                    {45, 9, 9}, {90, -9, -9}, {45, 9, 9}, {40, 0, 11},
                                    {80, 0, -11}, {40, 1, 11} };

struct SimResult {
    long max_dev    = 0;  ///< worst |guest - hardware| over all events, any axis
    long max_offset = 0;  ///< worst |cursor - centre| once a warp has landed, any axis
    long max_guest_x = 0; ///< furthest the guest travelled right
};

using Decide = std::function<pointer_capture::Motion(int x, int y, int cx, int cy)>;
using Begin  = std::function<bool(int x, int y, int cx, int cy)>;  ///< true = warp to the centre

SimResult simulate(Warp warp, int lag, int batch, const Begin& begin, const Decide& decide,
                   bool stale_click = true,
                   const std::vector<Run>& path = WALK, int click_dx = 230, int click_dy = -170)
{
    constexpr int CX = 400, CY = 300;
    struct Ev { int x, y; long tx, ty; };
    std::deque<Ev> queue;
    std::vector<int> posted;              // move index each posted warp lands at
    int  cur_x = CX + click_dx, cur_y = CY + click_dy;   // pointer where the user clicked
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
        case Warp::PostedSilent: posted.push_back(step + lag); break;
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
            if (*it <= step) {
                land();
                if (warp == Warp::PostedLate) queue.push_back({CX, CY, tx, ty});
                it = posted.erase(it);
            }
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
    if (begin(cur_x, cur_y, CX, CY)) request_warp();   // MainWindow::set_mouse_captured
    for (const auto& run : path)
        for (int i = 0; i < run.n; ++i) move(run.dx, run.dy);
    drain();
    return r;
}

// A DeferredWarpPolicy past its first (discarded) event, captured AT the
// centre so no warp is pending — the steady state.
pointer_capture::DeferredWarpPolicy settled_deferred(int cx, int cy)
{
    pointer_capture::DeferredWarpPolicy p;
    p.begin(cx, cy, cx, cy, 64);
    p.on_motion(cx, cy, cx, cy, 64);
    return p;
}

constexpr int SIM_MARGIN = 64;   // a 256-px-high viewport (MainWindow: min(w,h)/4)

SimResult sim_deferred(Warp warp, int lag, int batch, bool stale_click = true,
                       const std::vector<Run>& path = WALK, int click_dx = 230, int click_dy = -170)
{
    pointer_capture::DeferredWarpPolicy p;
    return simulate(warp, lag, batch,
        [&](int x, int y, int cx, int cy) { return p.begin(x, y, cx, cy, SIM_MARGIN); },
        [&](int x, int y, int cx, int cy) { return p.on_motion(x, y, cx, cy, SIM_MARGIN); },
        stale_click, path, click_dx, click_dy);
}

SimResult sim_centre(Warp warp, int lag, int batch, const std::vector<Run>& path = WALK)
{
    pointer_capture::Policy p;
    return simulate(warp, lag, batch,
        [&](int, int, int, int) { p.begin(); return true; },
        [&](int x, int y, int cx, int cy) { return p.on_motion(x, y, cx, cy); },
        true, path);
}

std::string fmt_sim(const SimResult& r)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "max_dev=%ld max_offset=%ld max_guest_x=%ld",
                  r.max_dev, r.max_offset, r.max_guest_x);
    return buf;
}

// Every capture position, path, queue state and batch size a model is run
// under in the matrix rows (PCAP-23..27). Returns the worst deviation.
long matrix_deferred(Warp warp, int lag, std::string* where)
{
    long worst = 0;
    for (int stale = 0; stale < 2; ++stale)
        for (int batch : {1, 4})
            for (int click : {0, 5, 30, 60, 70, 230})
                for (const auto* path : {&WALK, &CROSSING}) {
                    const auto r = sim_deferred(warp, lag, batch, stale != 0, *path,
                                                click, -click / 2);
                    if (r.max_dev > worst) {
                        worst = r.max_dev;
                        char buf[128];
                        std::snprintf(buf, sizeof(buf), "stale=%d batch=%d click=%d path=%s %s",
                                      stale, batch, click, path == &WALK ? "walk" : "crossing",
                                      fmt_sim(r).c_str());
                        *where = buf;
                    }
                }
    return worst;
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
        auto p = settled_deferred(CX, CY);
        const auto m = p.on_motion(CX + 10, CY - 4, CX, CY, 64);
        check("PCAP-16", "motion inside the margin is forwarded without a re-centre",
              m.forward && m.dx == 10 && m.dy == -4 && !m.recentre, fmt(m));
    }

    // PCAP-17: capture start, as in PCAP-03. begin() is given the pointer
    // position: beyond the margin the frontend must warp and the warp is
    // awaited; the first event is never forwarded (it may be the stale
    // pre-capture position), and no second warp is requested while the first
    // is in flight — two cannot be told apart (PCAP-13 fails with it). Within
    // the margin no warp is needed at all.
    {
        pointer_capture::DeferredWarpPolicy p;
        const bool warp = p.begin(CX + 300, CY + 200, CX, CY, 64);
        const auto m = p.on_motion(CX + 302, CY + 199, CX, CY, 64);
        check("PCAP-17a", "capture far from the centre warps; first event not forwarded, warp still awaited",
              warp && !m.forward && !m.recentre && p.warp_pending(), fmt(m));
        pointer_capture::DeferredWarpPolicy q;
        const bool qwarp = q.begin(CX + 9, CY - 2, CX, CY, 64);
        const auto e = q.on_motion(CX + 12, CY - 2, CX, CY, 64);
        check("PCAP-17b", "capture within the margin needs no warp; first event not forwarded",
              !qwarp && !e.forward && !e.recentre && !q.warp_pending(), fmt(e));
        pointer_capture::DeferredWarpPolicy r;
        r.begin(CX + 300, CY + 200, CX, CY, 64);
        const auto f = r.on_motion(CX + 4, CY - 1, CX, CY, 64);
        check("PCAP-17c", "a first event near the centre after a far capture is the warp landing",
              !f.forward && !f.recentre && !r.warp_pending(), fmt(f));
    }

    // PCAP-18: while a warp is outstanding, an event near the last position
    // predates it (delta from the last position, still pending); an event near
    // the centre follows it (delta from the centre, no longer pending).
    {
        auto p = settled_deferred(CX, CY);
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
        auto p = settled_deferred(CX, CY);
        p.on_motion(CX + 5, CY, CX, CY, 64);
        p.begin(CX - 250, CY + 40, CX, CY, 64);        // captured again
        const auto m = p.on_motion(CX - 251, CY + 40, CX, CY, 64);
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

    // ── Issue #303, Wayland: which platforms get DeferredWarpPolicy. ──

    // PCAP-22: the selector. Every Qt Wayland plugin name starts "wayland";
    // X11, Windows and the offscreen plugin the regression rows run under keep
    // the centre-referenced Policy. The prefix must be the whole word.
    {
        using pointer_capture::deferred_warp_platform;
        check("PCAP-22a", "QPA 'wayland' selects the deferred policy", deferred_warp_platform("wayland"));
        check("PCAP-22b", "QPA 'wayland-egl' selects the deferred policy", deferred_warp_platform("wayland-egl"));
        check("PCAP-22c", "QPA 'wayland-xcomposite-glx' selects the deferred policy",
              deferred_warp_platform("wayland-xcomposite-glx"));
        check("PCAP-22d", "QPA 'xcb' (X11) keeps Policy", !deferred_warp_platform("xcb"));
        check("PCAP-22e", "QPA 'windows' keeps Policy", !deferred_warp_platform("windows"));
        check("PCAP-22f", "QPA 'offscreen' (regression rows) keeps Policy", !deferred_warp_platform("offscreen"));
        check("PCAP-22g", "a shorter name ('waylan') is not Wayland", !deferred_warp_platform("waylan"));
        check("PCAP-22h", "an empty platform name keeps Policy", !deferred_warp_platform(""));
        check("PCAP-22i", "'wayland' must be the PREFIX: 'xwayland' keeps Policy",
              !deferred_warp_platform("xwayland"));
    }

    // PCAP-23..27: the deferred policy across every capture position (at the
    // centre, near it, at and beyond the margin, far), both paths, with and
    // without a stale queued event, an app that keeps up and one 4 events
    // behind. The guest must follow the hardware exactly in each.
    {
        std::string where;
        const long d = matrix_deferred(Warp::Dropped, 0, &where);
        check("PCAP-23", "Wayland Qt <= 6.9 (setPos never warps): deferred policy exact everywhere",
              d == 0, where);
    }
    {
        std::string where;
        const long d = matrix_deferred(Warp::PostedSilent, 1, &where);
        check("PCAP-24", "Wayland pointer-warp landing one move late, silently: deferred policy exact everywhere",
              d == 0, where);
    }
    {
        // Why switching Wayland WITH a working warp costs nothing: the deferred
        // policy is exact for a warp that lands at once and echoes, too.
        std::string where;
        const long d = matrix_deferred(Warp::SyncEcho, 0, &where);
        check("PCAP-25", "synchronous echoing warp: deferred policy exact everywhere", d == 0, where);
    }
    {
        // ... while the centre-referenced Policy cannot follow a late warp.
        const auto c = sim_centre(Warp::PostedSilent, 1, 1);
        check("PCAP-26", "Wayland pointer-warp one move late: centre-referenced Policy drifts",
              c.max_dev > SIM_MARGIN, fmt_sim(c));
    }
    {
        std::string where;
        const long d = matrix_deferred(Warp::SyncSilent, 0, &where);
        check("PCAP-27", "macOS CGWarpMouseCursorPosition: deferred policy exact everywhere", d == 0, where);
    }

    // ── Review round 1: the re-centre condition, side by side. ──

    // PCAP-28: one past the margin on each side re-centres; exactly at it does
    // not (the margin is inclusive).
    {
        struct Side { const char* id; const char* desc; int ox, oy; bool recentre; };
        const Side sides[] = {
            {"PCAP-28a", "one past the margin to the right re-centres",  65,   0, true},
            {"PCAP-28b", "one past the margin to the left re-centres",  -65,   0, true},
            {"PCAP-28c", "one past the margin downward re-centres",       0,  65, true},
            {"PCAP-28d", "one past the margin upward re-centres",         0, -65, true},
            {"PCAP-28e", "exactly at the margin to the right does not",  64,   0, false},
            {"PCAP-28f", "exactly at the margin to the left does not",  -64,   0, false},
            {"PCAP-28g", "exactly at the margin downward does not",       0,  64, false},
            {"PCAP-28h", "exactly at the margin upward does not",         0, -64, false},
        };
        for (const auto& sd : sides) {
            auto p = settled_deferred(CX, CY);
            // Two half steps, so no single event approaches the misread limit.
            p.on_motion(CX + sd.ox / 2, CY + sd.oy / 2, CX, CY, 64);
            const auto m = p.on_motion(CX + sd.ox, CY + sd.oy, CX, CY, 64);
            check(sd.id, sd.desc, m.recentre == sd.recentre && p.warp_pending() == sd.recentre, fmt(m));
        }
    }

    // PCAP-29: a capture beyond the margin warps whichever quadrant it is in
    // (PCAP-17a is +x+y), and its first event is the stale position: not
    // forwarded, warp still awaited.
    {
        struct Quad { const char* id; const char* desc; int ox, oy; };
        const Quad quads[] = {
            {"PCAP-29a", "capture far left-below warps; first event stale",  -300,  200},
            {"PCAP-29b", "capture far right-above warps; first event stale",  300, -200},
            {"PCAP-29c", "capture far left-above warps; first event stale",  -300, -200},
            {"PCAP-29d", "capture just past the margin, left only, warps",    -65,    0},
        };
        for (const auto& q : quads) {
            pointer_capture::DeferredWarpPolicy p;
            const bool warp = p.begin(CX + q.ox, CY + q.oy, CX, CY, 64);
            const auto m = p.on_motion(CX + q.ox - 1, CY + q.oy, CX, CY, 64);
            check(q.id, q.desc, warp && !m.forward && !m.recentre && p.warp_pending(), fmt(m));
        }
    }

    // PCAP-30: an event at the last position carries no motion and is not
    // forwarded (PCAP-02's rule, for this policy).
    {
        auto p = settled_deferred(CX, CY);
        p.on_motion(CX + 7, CY + 3, CX, CY, 64);
        const auto m = p.on_motion(CX + 7, CY + 3, CX, CY, 64);
        check("PCAP-30", "a zero-delta event is not forwarded",
              !m.forward && m.dx == 0 && m.dy == 0 && !m.recentre, fmt(m));
    }

    // PCAP-31: documented limit 1, pinned at its boundary. A warp is pending
    // with the pointer 70 px right of the centre (margin 64). A PRE-warp event
    // moving 35 px back is read correctly (centre reading 35 is not nearer);
    // one moving 36 px back is taken for the landing and forwarded as +34
    // instead of -36. It needs one event of more than half the offset straight
    // at the centre — min(w,h)/8 points or more in production.
    {
        auto pending_at_70 = [] {
            auto p = settled_deferred(CX, CY);
            p.on_motion(CX + 35, CY, CX, CY, 64);
            p.on_motion(CX + 70, CY, CX, CY, 64);     // beyond: warp requested
            return p;
        };
        auto p = pending_at_70();
        const auto a = p.on_motion(CX + 35, CY, CX, CY, 64);
        check("PCAP-31a", "limit: a pre-warp move back of half the offset is read from the last position",
              a.dx == -35 && p.warp_pending(), fmt(a));
        auto q = pending_at_70();
        const auto b = q.on_motion(CX + 34, CY, CX, CY, 64);
        check("PCAP-31b", "limit: a pre-warp move back of more than half the offset is misread as the landing",
              b.dx == 34 && !q.warp_pending(), fmt(b));
        // The jump threshold itself: drift back to +40 in small (stale) steps,
        // then a jump of exactly margin/2 = 32 to +8 counts as the landing.
        auto r = pending_at_70();
        for (int x = CX + 60; x >= CX + 40; x -= 10) r.on_motion(x, CY, CX, CY, 64);
        const auto c = r.on_motion(CX + 8, CY, CX, CY, 64);
        check("PCAP-31c", "a jump of exactly margin/2 nearer the centre counts as the landing",
              c.dx == 8 && !r.warp_pending(), fmt(c));
    }

    // PCAP-32: documented limit 2. With a warp pending, the pointer coming back
    // to the centre in ordinary steps is NOT a landing (no jump of margin/2),
    // and a warp never seen to land is given up on after kWarpGiveUpEvents
    // events, so re-centring can resume.
    {
        auto p = settled_deferred(CX, CY);
        p.on_motion(CX + 35, CY, CX, CY, 64);
        p.on_motion(CX + 70, CY, CX, CY, 64);         // warp requested (dropped)
        int x = CX + 70;
        pointer_capture::Motion m;
        for (int i = 0; i < 7; ++i) { x -= 10; m = p.on_motion(x, CY, CX, CY, 64); }
        check("PCAP-32a", "steps back to the centre are motion, not a landing",
              x == CX && m.dx == -10 && p.warp_pending(), fmt(m));
        for (int i = 7; i < pointer_capture::DeferredWarpPolicy::kWarpGiveUpEvents; ++i)
            p.on_motion(x, CY, CX, CY, 64);
        const bool still = p.warp_pending();
        p.on_motion(x, CY, CX, CY, 64);
        check("PCAP-32b", "a warp unseen for kWarpGiveUpEvents events is given up on",
              still && !p.warp_pending());
    }

    std::printf("\n==============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
