#pragma once

// Issue #39 — where the debugger window goes when it is "attached" to the
// emulator window.
//
// The debugger is meant to behave as though docked to the right-hand edge of
// the emulator window: move the emulator window and the debugger follows,
// keeping the two edges adjacent. The wiring for that (an event filter on the
// main window reacting to Move/Resize) has existed since Phase 7, but it called
// move() unconditionally — and on Wayland a client cannot position its own
// toplevel at all. xdg-shell has no request for it, and Qt answers a later
// pos() query with the value you asked for rather than the truth, so the code
// believed it had worked while the windows drifted apart. That is the bug.
//
// Hence `positioning_supported`: the caller passes false on a backend where a
// client may not place its own windows, and this returns "do not reposition"
// instead of issuing a move that will be quietly ignored.
//
// The rest of the rules come from the issue's own list of things to decide:
//
//   * attachment is a TOGGLE, not always-on — a user who deliberately put the
//     debugger on a second monitor must be able to keep it there;
//   * a fullscreen emulator window has no free edge to attach to (the debugger
//     would be shoved off-screen or hidden behind it), so attachment stands
//     down until fullscreen ends;
//   * the result is clamped to the screen so the debugger can never be parked
//     somewhere the user cannot reach it — including flipping to the LEFT of
//     the emulator window when there is no room on the right.
//
// Pure by design: no Qt types, no I/O, no window system. Task 91 established
// that GUI wiring is invisible to the automated gate, so the arithmetic that
// decides where the window lands lives here where it can be pinned row by row.

namespace jnext {

/// A screen-space rectangle, in the same coordinate space throughout (Qt
/// logical pixels, as reported by QWidget::frameGeometry / QScreen::availableGeometry).
struct AttachRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

/// Where the debugger window should be moved to, if anywhere.
struct AttachPlacement {
    /// false => leave the debugger window exactly where it is. `x`/`y` are
    /// meaningless in that case and are left at 0.
    bool reposition = false;
    int  x = 0;
    int  y = 0;
};

/// Why attachment stood down. Reported so the UI can tell the user rather than
/// silently doing nothing (the failure mode this issue is about).
enum class AttachStatus {
    /// Attached and tracking; the returned placement is live.
    Attached,
    /// The user turned attachment off.
    Disabled,
    /// The window system does not let a client position its own toplevels
    /// (Wayland). Attachment is impossible, not merely off.
    Unsupported,
    /// The emulator window is fullscreen; there is no edge to attach to.
    MainFullscreen,
};

/// Result of one attachment computation.
struct AttachDecision {
    AttachStatus    status = AttachStatus::Attached;
    AttachPlacement placement;
};

/// Compute where the debugger window's top-left corner belongs so that its left
/// edge sits flush against the right edge of `main_frame`.
///
/// `main_frame` is the emulator window's FRAME geometry (including decorations)
/// — using the client rect instead would tuck the debugger under the emulator's
/// window border by the frame width.
///
/// `screen` is the available geometry (work area) of the screen the emulator
/// window is on, i.e. excluding panels/docks.
///
/// Degenerate inputs (a zero-sized screen, a zero-sized debugger window) yield
/// no reposition: there is nothing meaningful to compute, and moving to a
/// guessed coordinate is worse than staying put.
inline AttachDecision compute_attached_placement(const AttachRect& main_frame,
                                                 int dbg_w,
                                                 int dbg_h,
                                                 const AttachRect& screen,
                                                 bool attach_enabled,
                                                 bool main_fullscreen,
                                                 bool positioning_supported)
{
    AttachDecision d;

    // Order matters only for what the UI is told; every branch below is a
    // "leave it alone". Unsupported is checked first because it is a property
    // of the platform that the user cannot override with the toggle.
    if (!positioning_supported) {
        d.status = AttachStatus::Unsupported;
        return d;
    }
    if (!attach_enabled) {
        d.status = AttachStatus::Disabled;
        return d;
    }
    if (main_fullscreen) {
        d.status = AttachStatus::MainFullscreen;
        return d;
    }
    if (screen.w <= 0 || screen.h <= 0 || dbg_w <= 0 || dbg_h <= 0) {
        d.status = AttachStatus::Disabled;
        return d;
    }

    const int screen_right  = screen.x + screen.w;
    const int screen_bottom = screen.y + screen.h;

    // Preferred spot: immediately right of the emulator window, tops aligned.
    int x = main_frame.x + main_frame.w;
    int y = main_frame.y;

    // No room on the right? Try the mirror position on the left before giving
    // up and clamping — on a maximised or right-docked emulator window the left
    // side is usually where the space actually is.
    if (x + dbg_w > screen_right) {
        const int left_x = main_frame.x - dbg_w;
        if (left_x >= screen.x)
            x = left_x;
        else
            x = screen_right - dbg_w;   // neither side fits: hug the right edge
    }

    // Clamp into the work area. The x clamp runs after the flip so that a
    // debugger wider than the whole screen still lands at the screen origin
    // rather than off to the left of it.
    if (x < screen.x) x = screen.x;
    if (y < screen.y) y = screen.y;
    if (y + dbg_h > screen_bottom) {
        y = screen_bottom - dbg_h;
        if (y < screen.y) y = screen.y;   // taller than the screen: pin to top
    }

    d.status = AttachStatus::Attached;
    d.placement.reposition = true;
    d.placement.x = x;
    d.placement.y = y;
    return d;
}

/// Human-readable reason attachment is not tracking, for the UI to show.
/// Returns nullptr when it IS tracking.
inline const char* attach_status_reason(AttachStatus s)
{
    switch (s) {
    case AttachStatus::Attached:       return nullptr;
    case AttachStatus::Disabled:       return "attachment is off";
    case AttachStatus::Unsupported:    return "this window system does not allow "
                                              "applications to position their own windows";
    case AttachStatus::MainFullscreen: return "the emulator window is fullscreen";
    }
    return nullptr;
}

/// Does `platform_name` (QGuiApplication::platformName()) name a backend on
/// which a client may position its own toplevel windows?
///
/// Wayland deliberately does not expose global window coordinates to clients:
/// xdg-shell has no "set position" request, so a move() is dropped. Every other
/// backend jnext runs on (xcb, windows, cocoa, offscreen) honours it *in
/// principle*.
///
/// "In principle" is doing real work in that sentence, which is why this
/// predicate is only the FIRST of two gates — see move_landed() below. Measured
/// on the development machine (a Wayland session with XWayland): forcing
/// QT_QPA_PLATFORM=xcb reports "xcb", yet a move() on the debugger window was
/// dropped just as thoroughly as on native Wayland. Trusting the platform name
/// alone would have shipped exactly the silent misbehaviour this issue reports.
inline bool platform_supports_window_positioning(const char* platform_name)
{
    if (!platform_name) return true;
    // Matches "wayland" and "wayland-egl".
    const char* p = platform_name;
    const char* w = "wayland";
    for (int i = 0; w[i]; ++i) {
        if (p[i] != w[i]) return true;
    }
    return false;
}

/// How far the window may end up from where it was asked to go and still count
/// as having obeyed. Window managers legitimately nudge a placement by a few
/// pixels (frame extents, snapping, work-area edges); a backend that ignores
/// the request outright misses by hundreds.
constexpr int kAttachMoveTolerancePx = 8;

/// Did an issued move actually take effect? Compare the requested position
/// against the window's position read back AFTER the window system has had a
/// chance to apply it (not synchronously — X11 needs a round trip).
inline bool move_landed(int want_x, int want_y, int got_x, int got_y,
                        int tolerance = kAttachMoveTolerancePx)
{
    const int dx = got_x > want_x ? got_x - want_x : want_x - got_x;
    const int dy = got_y > want_y ? got_y - want_y : want_y - got_y;
    return dx <= tolerance && dy <= tolerance;
}

/// Consecutive ignored moves after which attachment gives up for the session.
/// More than one, because a single miss can be a transient (a move issued while
/// the window was still being mapped); small, because the user should not have
/// to drag the emulator window ten times before being told it will not work.
constexpr int kAttachFailuresBeforeGivingUp = 3;

/// Should attachment stop trying and report itself unsupported?
inline bool should_give_up_on_attach(int consecutive_failures)
{
    return consecutive_failures >= kAttachFailuresBeforeGivingUp;
}

/// Sequences the deferred landing checks so a burst of moves cannot be mistaken
/// for a broken window system.
///
/// This exists because of a defect found in review, and the defect is worth
/// stating plainly because the naive version looks obviously correct:
///
///   A drag delivers Move events every ~30 ms. Each one issued a move and armed
///   its OWN deferred check that captured that call's target. By the time a
///   check fired 250 ms later, ~8 further moves had legitimately happened, so it
///   compared a stale target against a correct current position, scored a miss,
///   and three of those in a row permanently disabled the feature. Dragging the
///   window — the only way anyone uses this — was the most reliable way to break
///   it, on a platform where every single move landed perfectly.
///
/// The rule, therefore: **only the newest request in a burst is ever checked.**
/// Each issued move takes a generation token; a deferred check presents its
/// token and is discarded unless it is still the current one. What it then
/// compares against is the tracker's own latest target, not a captured copy, so
/// there is no way for a stale coordinate to reach the comparison at all.
///
/// Pure: no Qt, no timers, no window system. The sequencing IS the bug surface,
/// so it lives here where a test can drive it, rather than in the widget where
/// nothing automated can see it.
struct AttachMoveTracker {
    /// Bumped by every issued move; a deferred check is current only if its
    /// token still equals this.
    unsigned long long generation = 0;
    /// Consecutive checks that found the window somewhere other than asked.
    int consecutive_failures = 0;
    /// The most recently requested position — what a landing check compares
    /// against. Deliberately read live rather than captured at schedule time.
    int want_x = 0;
    int want_y = 0;

    /// Record that a move to (x, y) has just been issued. Supersedes any check
    /// still in flight. Returns the token the deferred check must present.
    unsigned long long issue_move(int x, int y)
    {
        want_x = x;
        want_y = y;
        return ++generation;
    }

    /// May a deferred landing check bearing `token` proceed? False once a newer
    /// move has superseded it — which is the whole point.
    bool check_is_current(unsigned long long token) const
    {
        return token == generation;
    }

    /// Feed in the outcome of a check that WAS current. Returns true when
    /// attachment should give up and report itself unsupported.
    bool record_landing(bool landed)
    {
        if (landed) {
            consecutive_failures = 0;
            return false;
        }
        return should_give_up_on_attach(++consecutive_failures);
    }

    /// Clear the failure history — used when the user explicitly re-enables
    /// attachment, so a give-up is recoverable without restarting the app.
    void reset_failures() { consecutive_failures = 0; }
};

} // namespace jnext
