// Debugger window attachment geometry test (GitHub issue #39).
//
// No VHDL oracle: this is host window-manager ergonomics, not emulated
// hardware. The oracle is the contract stated in issue #39 and restated in
// src/debugger/window_attach.h:
//
//   * attached means the debugger's LEFT edge is flush against the emulator
//     window's frame RIGHT edge, tops aligned, so dragging the emulator window
//     drags the debugger with it;
//   * attachment is a toggle — off means the debugger is never moved, so a
//     user who put it on a second monitor keeps it there;
//   * a fullscreen emulator window has no edge to attach to, so attachment
//     stands down rather than shoving the debugger off-screen;
//   * on a window system where a client may not position its own toplevels
//     (Wayland) the answer is "do not move", NOT a move that gets silently
//     dropped — that silent drop is the bug this issue reports;
//   * a check of whether a move landed must never be applied to a SUPERSEDED
//     request: a drag issues moves far faster than any deferred check can
//     settle, so scoring a stale target as a miss would let ordinary dragging
//     disable the feature on a platform where every move landed (this is a real
//     defect found in review, not a hypothetical);
//   * a give-up must be recoverable without restarting the application;
//   * and because the platform NAME turned out not to settle that question —
//     an XWayland session reporting "xcb" dropped moves just as native Wayland
//     did, measured on the development machine — attachment also verifies that
//     the moves it issues actually land, and gives up VISIBLY when they do not;
//   * the user must never end up with a debugger window they cannot reach, so
//     the result is clamped into the screen work area, flipping to the left of
//     the emulator window when the right has no room.
//
// Every row below derives from that contract, never from reading the
// implementation back.
//
// Run: ./build/test/window_attach_test

#include "debugger/window_attach.h"

#include <cstdio>
#include <string>

using jnext::AttachRect;
using jnext::AttachStatus;
using jnext::AttachDecision;
using jnext::compute_attached_placement;
using jnext::attach_status_reason;
using jnext::platform_supports_window_positioning;
using jnext::move_landed;
using jnext::should_give_up_on_attach;
using jnext::kAttachFailuresBeforeGivingUp;
using jnext::AttachMoveTracker;

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

std::string place_str(const AttachDecision& d)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "reposition=%d x=%d y=%d status=%d",
                  d.placement.reposition ? 1 : 0, d.placement.x, d.placement.y,
                  static_cast<int>(d.status));
    return buf;
}

// A roomy work area with space for the emulator window AND the debugger side
// by side, and an emulator window sitting well inside it. It has to be this
// wide: the debugger's minimum width alone is 1170, so on a 1920 screen the two
// windows cannot be adjacent at all and every row would exercise the fallback
// paths instead of the behaviour it means to pin.
const AttachRect kScreen{0, 0, 3840, 2160};
// A screen too small to hold both windows side by side, for the rows that
// deliberately test the no-room fallbacks.
const AttachRect kSmallScreen{0, 0, 1920, 1080};
const AttachRect kMain{100, 50, 660, 560};
constexpr int kDbgW = 1170;   // DebuggerWindow's minimum width
constexpr int kDbgH = 900;

// Attached, everything permitted, nothing fullscreen.
AttachDecision attached(const AttachRect& main = kMain,
                        const AttachRect& screen = kScreen,
                        int w = kDbgW, int h = kDbgH)
{
    return compute_attached_placement(main, w, h, screen, true, false, true);
}

} // namespace

int main()
{
    // --- WA-01..03: the core promise — flush against the right edge, tops aligned.
    {
        AttachDecision d = attached();
        check("WA-01", "attached => a reposition is requested",
              d.placement.reposition && d.status == AttachStatus::Attached, place_str(d));
        check("WA-02", "debugger left edge == emulator frame right edge",
              d.placement.x == kMain.x + kMain.w, place_str(d));
        check("WA-03", "debugger top == emulator frame top",
              d.placement.y == kMain.y, place_str(d));
    }

    // --- WA-04/05: tracking. The whole point of the issue is that MOVING the
    // emulator window moves the debugger by the same delta.
    {
        AttachRect moved = kMain;
        moved.x += 220;
        moved.y += 130;
        AttachDecision before = attached();
        AttachDecision after  = attached(moved);
        check("WA-04", "moving the emulator window moves the debugger by the same dx",
              after.placement.x - before.placement.x == 220,
              place_str(before) + " -> " + place_str(after));
        check("WA-05", "moving the emulator window moves the debugger by the same dy",
              after.placement.y - before.placement.y == 130,
              place_str(before) + " -> " + place_str(after));
    }

    // --- WA-06: resizing the emulator window (a scale change) keeps the edges
    // adjacent — the debugger slides right by the width the emulator gained.
    {
        AttachRect wider = kMain;
        wider.w += 320;
        AttachDecision before = attached();
        AttachDecision after  = attached(wider);
        check("WA-06", "widening the emulator window pushes the debugger right by the same amount",
              after.placement.x - before.placement.x == 320,
              place_str(before) + " -> " + place_str(after));
    }

    // --- WA-07: toggle off => never moved. A debugger deliberately placed on a
    // second monitor must not be yanked back.
    {
        AttachDecision d = compute_attached_placement(kMain, kDbgW, kDbgH, kScreen,
                                                      /*attach_enabled=*/false, false, true);
        check("WA-07", "attachment off => no reposition, reported as Disabled",
              !d.placement.reposition && d.status == AttachStatus::Disabled, place_str(d));
    }

    // --- WA-08: Wayland (or any backend that forbids self-positioning) => no
    // move is issued, and the caller is told WHY. Issuing a move here is the
    // exact silent-misbehaviour this issue reports.
    {
        AttachDecision d = compute_attached_placement(kMain, kDbgW, kDbgH, kScreen,
                                                      true, false, /*positioning_supported=*/false);
        check("WA-08", "positioning unsupported => no reposition, reported as Unsupported",
              !d.placement.reposition && d.status == AttachStatus::Unsupported, place_str(d));
    }

    // --- WA-09: Unsupported outranks the toggle — a user cannot switch
    // attachment on and have it silently do nothing on Wayland.
    {
        AttachDecision d = compute_attached_placement(kMain, kDbgW, kDbgH, kScreen,
                                                      /*attach_enabled=*/false, false, false);
        check("WA-09", "unsupported platform reports Unsupported even when the toggle is off",
              d.status == AttachStatus::Unsupported, place_str(d));
    }

    // --- WA-10: fullscreen emulator => stand down.
    {
        AttachDecision d = compute_attached_placement(kScreen, kDbgW, kDbgH, kScreen,
                                                      true, /*main_fullscreen=*/true, true);
        check("WA-10", "fullscreen emulator window => no reposition, reported as MainFullscreen",
              !d.placement.reposition && d.status == AttachStatus::MainFullscreen, place_str(d));
    }

    // --- WA-11/12: no room on the right => flip to the LEFT of the emulator
    // window rather than hanging off the screen edge.
    {
        // Emulator window pushed right: 1200 + 660 = 1860, leaving 60px — far
        // less than the debugger's 1170. There IS room on the left (1200 >= 1170).
        AttachRect right_side{1200, 50, 660, 560};
        AttachDecision d = attached(right_side, kSmallScreen);
        check("WA-11", "no room on the right => debugger flips to the left of the emulator",
              d.placement.reposition && d.placement.x == right_side.x - kDbgW, place_str(d));
        check("WA-12", "flipped placement still keeps the two windows edge-adjacent",
              d.placement.x + kDbgW == right_side.x, place_str(d));
    }

    // --- WA-13: neither side fits => clamp to the screen so the window stays
    // reachable, never placed off-screen.
    {
        // Emulator at x=700 on a 1920 screen: right gap 1920-1360=560, left gap
        // 700. Debugger is 1170 wide — neither fits.
        AttachRect middle{700, 50, 660, 560};
        AttachDecision d = attached(middle, kSmallScreen);
        check("WA-13", "neither side fits => clamped fully on-screen",
              d.placement.reposition
                  && d.placement.x >= kSmallScreen.x
                  && d.placement.x + kDbgW <= kSmallScreen.x + kSmallScreen.w,
              place_str(d));
    }

    // --- WA-14: a debugger taller than the space below the emulator's top is
    // pulled up so its bottom stays on-screen (its title bar stays reachable).
    {
        AttachRect low{100, 900, 660, 160};   // top at y=900, screen ends at 1080
        AttachDecision d = attached(low, kSmallScreen);
        check("WA-14", "debugger is pulled up so its bottom stays within the work area",
              d.placement.reposition
                  && d.placement.y + kDbgH <= kSmallScreen.y + kSmallScreen.h,
              place_str(d));
    }

    // --- WA-15: the work area origin is respected, not assumed to be (0,0) —
    // a top panel or a second monitor to the left both produce a non-zero origin.
    {
        AttachRect offset_screen{1920, 40, 1920, 1040};
        AttachRect main_on_it{1920, 40, 660, 560};
        AttachDecision d = compute_attached_placement(main_on_it, kDbgW, kDbgH,
                                                      offset_screen, true, false, true);
        check("WA-15", "placement respects a non-zero work-area origin",
              d.placement.reposition
                  && d.placement.x >= offset_screen.x
                  && d.placement.y >= offset_screen.y,
              place_str(d));
    }

    // --- WA-16: a debugger window taller than the whole work area is pinned to
    // the top rather than pushed above it (clamping must not overshoot).
    {
        AttachDecision d = attached(kMain, kSmallScreen, kDbgW, /*h=*/2000);
        check("WA-16", "a debugger taller than the screen is pinned to the work-area top",
              d.placement.reposition && d.placement.y == kSmallScreen.y, place_str(d));
    }

    // --- WA-17: degenerate inputs produce no move. Guessing a coordinate from
    // a zero-sized screen is worse than staying put.
    {
        AttachDecision d = compute_attached_placement(kMain, kDbgW, kDbgH,
                                                      AttachRect{0, 0, 0, 0}, true, false, true);
        check("WA-17", "a zero-sized work area yields no reposition",
              !d.placement.reposition, place_str(d));
    }

    // --- WA-18/19: the platform predicate. This is what decides whether the
    // whole feature engages, so it is asserted directly.
    {
        check("WA-18", "wayland backends are reported as not supporting self-positioning",
              !platform_supports_window_positioning("wayland")
                  && !platform_supports_window_positioning("wayland-egl"));
        check("WA-19", "xcb/windows/cocoa/offscreen are reported as supporting it",
              platform_supports_window_positioning("xcb")
                  && platform_supports_window_positioning("windows")
                  && platform_supports_window_positioning("cocoa")
                  && platform_supports_window_positioning("offscreen"));
    }

    // --- WA-21..23: the empirical gate. The platform name is a hint, not
    // proof, so an issued move is checked against where the window actually
    // ended up. A WM nudging a placement a few pixels is obedience; a backend
    // ignoring it entirely misses by hundreds.
    {
        check("WA-21", "a move that lands within tolerance counts as honoured",
              move_landed(500, 300, 500, 300)
                  && move_landed(500, 300, 503, 297)
                  && move_landed(500, 300, 508, 308));
        check("WA-22", "a move the window system ignored is detected as not landed",
              !move_landed(500, 300, 3384, 622)
                  && !move_landed(500, 300, 500, 900)
                  && !move_landed(500, 300, 509, 300));
    }

    // --- WA-23: give up only after repeated failures — one miss can be a
    // transient during window mapping — but soon enough that the user is told
    // rather than left dragging a window that never follows.
    {
        bool early_ok = true;
        for (int i = 0; i < kAttachFailuresBeforeGivingUp; ++i)
            if (should_give_up_on_attach(i)) early_ok = false;
        check("WA-23", "attachment gives up only at the failure threshold, not before",
              early_ok
                  && should_give_up_on_attach(kAttachFailuresBeforeGivingUp)
                  && should_give_up_on_attach(kAttachFailuresBeforeGivingUp + 1)
                  && kAttachFailuresBeforeGivingUp > 1
                  && kAttachFailuresBeforeGivingUp <= 5);
    }

    // --- WA-24: THE REGRESSION ROW for the review defect. A burst of moves
    // (a drag) leaves several checks in flight; every one but the newest must
    // be discarded unheard. Without this, a drag on a perfectly working
    // platform permanently disables attachment.
    {
        AttachMoveTracker t;
        unsigned long long first  = t.issue_move(100, 100);
        unsigned long long second = t.issue_move(130, 100);
        unsigned long long third  = t.issue_move(160, 100);
        check("WA-24", "only the newest request in a burst is checked; older ones are discarded",
              !t.check_is_current(first)
                  && !t.check_is_current(second)
                  && t.check_is_current(third));
    }

    // --- WA-25: and what the surviving check compares against is the LATEST
    // target, not a copy captured when it was scheduled. Capturing is what
    // made the stale comparison possible in the first place.
    {
        AttachMoveTracker t;
        t.issue_move(100, 100);
        t.issue_move(400, 250);
        check("WA-25", "the tracker's comparison target is the most recent request",
              t.want_x == 400 && t.want_y == 250);
    }

    // --- WA-26: a realistic drag — many moves, each landing correctly, with
    // only the final one ever checked — must NOT accumulate any failures. This
    // is the reviewer's reproduction expressed as an assertion.
    {
        AttachMoveTracker t;
        unsigned long long token = 0;
        for (int i = 0; i < 40; ++i)
            token = t.issue_move(100 + i * 30, 200);
        // Only the last check is current; it finds the window where asked.
        bool gave_up = false;
        if (t.check_is_current(token))
            gave_up = t.record_landing(move_landed(t.want_x, t.want_y, t.want_x, t.want_y));
        check("WA-26", "a 40-move drag on a working platform accumulates no failures",
              !gave_up && t.consecutive_failures == 0,
              "failures=" + std::to_string(t.consecutive_failures));
    }

    // --- WA-27: a genuinely broken platform is still caught — the guard above
    // must not have made the detection unreachable.
    {
        AttachMoveTracker t;
        bool gave_up = false;
        for (int i = 0; i < kAttachFailuresBeforeGivingUp; ++i) {
            unsigned long long tok = t.issue_move(500, 300);
            if (t.check_is_current(tok))
                gave_up = t.record_landing(move_landed(500, 300, 3384, 622));
        }
        check("WA-27", "a platform that really ignores every move is still detected",
              gave_up && t.consecutive_failures == kAttachFailuresBeforeGivingUp,
              "failures=" + std::to_string(t.consecutive_failures));
    }

    // --- WA-28: one landing clears the history, so intermittent misses never
    // add up to a give-up.
    {
        AttachMoveTracker t;
        t.issue_move(10, 10);
        t.record_landing(false);
        t.record_landing(false);
        bool gave_up_after_recovery = t.record_landing(true);
        bool gave_up_next = t.record_landing(false);
        check("WA-28", "a successful landing resets the consecutive-failure count",
              !gave_up_after_recovery && !gave_up_next && t.consecutive_failures == 1,
              "failures=" + std::to_string(t.consecutive_failures));
    }

    // --- WA-29: a give-up is recoverable. The user re-enabling attachment
    // clears the history, so the feature is not dead until the app restarts.
    {
        AttachMoveTracker t;
        t.issue_move(10, 10);
        // Bounded, deliberately: an unbounded "drive it until it gives up" loop
        // HANGS instead of failing under a mutation that stops record_landing()
        // ever reporting a give-up, and a test that hangs reports nothing at all.
        for (int i = 0; i < kAttachFailuresBeforeGivingUp * 4; ++i)
            if (t.record_landing(false)) break;
        t.reset_failures();
        check("WA-29", "resetting the failure history makes a give-up recoverable",
              t.consecutive_failures == 0 && !should_give_up_on_attach(t.consecutive_failures));
    }

    // --- WA-30: the post-flip clamp. When the debugger is WIDER than the whole
    // work area, neither side fits and the right-edge fallback itself goes
    // negative; the final clamp is what keeps the window reachable.
    {
        const AttachRect tiny{0, 0, 1000, 800};
        const AttachRect main{100, 50, 300, 200};
        AttachDecision d = compute_attached_placement(main, /*dbg_w=*/1200, /*dbg_h=*/600,
                                                      tiny, true, false, true);
        check("WA-30", "a debugger wider than the work area is clamped to its left edge",
              d.placement.reposition && d.placement.x == tiny.x, place_str(d));
    }

    // --- WA-31: same clamp, non-zero work-area origin — the clamp must use the
    // screen's origin, not a hard-coded zero.
    {
        const AttachRect tiny{2000, 100, 1000, 800};
        const AttachRect main{2100, 150, 300, 200};
        AttachDecision d = compute_attached_placement(main, /*dbg_w=*/1200, /*dbg_h=*/600,
                                                      tiny, true, false, true);
        check("WA-31", "the post-flip clamp respects a non-zero work-area origin",
              d.placement.reposition && d.placement.x == tiny.x, place_str(d));
    }

    // --- WA-20: every non-tracking status carries a reason to show the user;
    // the tracking one carries none. This is what keeps the degradation visible
    // instead of silent.
    {
        check("WA-20", "non-tracking statuses carry a user-visible reason, Attached does not",
              attach_status_reason(AttachStatus::Attached) == nullptr
                  && attach_status_reason(AttachStatus::Disabled) != nullptr
                  && attach_status_reason(AttachStatus::Unsupported) != nullptr
                  && attach_status_reason(AttachStatus::MainFullscreen) != nullptr);
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
