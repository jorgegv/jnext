#pragma once

// ---------------------------------------------------------------------------
// Pointer-capture motion policy (issue #37).
//
// A Kempston mouse is a RELATIVE device: it reports motion, never position.
// The Qt frontend therefore confines the host pointer by warping it back to
// the viewport centre after every motion event and forwarding the delta
// measured from that centre — so the pointer never reaches a screen edge and
// the guest can travel without limit. (macOS cannot: see DeferredWarpPolicy
// below, issue #303.)
//
// Two cases make that more than a subtraction, and both produced real bugs:
//
//   * the re-centring warp echoes back as a motion event AT the centre. It
//     carries no motion of its own and must not trigger another warp.
//   * capture can begin with the pointer anywhere (menu item, or a click near
//     an edge). A motion event queued at that pre-warp position may still be
//     delivered afterwards, and its delta — measured from the centre — is the
//     whole distance from wherever the pointer was. Forwarding it threw the
//     guest pointer across the screen.
//
// The decision is kept here, free of Qt and SDL, because the frontends'
// event handlers are reachable by no test (the same constraint that keeps
// render_policy.h / frame_deadline.h pure). SDL does not use the motion policy:
// its relative mode implements the equivalent natively. Both frontends do use
// release_chord() below for the release hint.
// ---------------------------------------------------------------------------

#include <string_view>

namespace pointer_capture {

/// What the frontend should do with one motion event.
struct Motion {
    bool forward  = false;  ///< feed dx/dy to the Kempston mouse
    int  dx       = 0;
    int  dy       = 0;
    bool recentre = false;  ///< warp the pointer back to the centre
};

/// Per-capture state machine. `begin()` on capture, then `on_motion()` for
/// every motion event while captured.
class Policy {
public:
    /// Capture started. The pointer is about to be warped to the centre, so
    /// the next event's delta is measured against a stale position.
    void begin() { discard_next_ = true; }

    /// Decide what to do with a motion event at host position (x, y) when the
    /// viewport centre is at (cx, cy).
    Motion on_motion(int x, int y, int cx, int cy) {
        // Our own warp: no motion, and re-warping would be pointless work.
        if (x == cx && y == cy) return Motion{};

        const int dx = x - cx;
        const int dy = y - cy;

        // Always re-centre from here on: even a discarded event has left the
        // pointer off-centre, and leaving it there would make the NEXT delta
        // wrong too.
        if (discard_next_) {
            discard_next_ = false;
            return Motion{false, 0, 0, true};
        }
        return Motion{dx != 0 || dy != 0, dx, dy, true};
    }

    /// Exposed for tests; true while the next event is still to be dropped.
    bool discard_armed() const { return discard_next_; }

private:
    bool discard_next_ = false;
};

// ---------------------------------------------------------------------------
// DeferredWarpPolicy — the macOS and Wayland policy (issue #303).
//
// `Policy` above measures every delta from the centre, which is only right if
// the warp has ALREADY landed when the next event is read. macOS breaks that:
//
//   * Qt's QCursor::setPos is CGEventPost of a synthetic mouse-moved event
//     (qtbase src/plugins/platforms/cocoa/qcocoacursor.mm). It is applied
//     LATER, in the window server's event stream, so moves already queued
//     still carry pre-warp positions. Measured from the centre, each of those
//     re-counts all the travel since the last warp landed: the guest pointer
//     overshoots and lurches.
//   * Since macOS 10.14 CGEventPost is silently dropped unless the app holds
//     the Accessibility permission. Then the warp never happens at all, and
//     every event forwards the pointer's whole offset from the centre.
//
// Wayland has the same two cases (see deferred_warp_platform below). On macOS
// the frontend warps with CGWarpMouseCursorPosition (no permission needed, no
// echo event); on Wayland QCursor::setPos is all there is.
//
// So this policy does not assume the warp has landed. Deltas are measured
// from the LAST position seen, which is exact whatever the warp does; the
// pointer is allowed to drift and is re-centred only once it is more than
// `margin` from the centre, and never while an earlier warp is still
// outstanding. While one is, an event is read both ways — from the last
// position (it predates the warp) and from the centre (it follows it) — and
// counts as the landing only if the centre reading is the nearer AND the event
// jumped at least margin/2 from the last position.
//
// Documented limits (pinned by PCAP-31 / PCAP-32):
//   * a PRE-warp event that genuinely moves toward the centre by more than half
//     its offset (and by at least margin/2) is taken for the landing. With the
//     frontend's margin of min(w,h)/4 that takes one event of min(w,h)/8 points
//     or more straight at the centre while a warp is in flight;
//   * a warp that lands while the pointer has itself come back to within
//     margin/2 of the centre is not recognised: its jump (under margin/2) is
//     forwarded as motion, and after kWarpGiveUpEvents events the warp is given
//     up on so re-centring resumes. Only a warp in flight for several events
//     can meet this (a late Wayland warp); CGWarpMouseCursorPosition lands at
//     once.
// Where no warp works at all (Wayland without pointer warp), motion stays exact
// but the pointer is not confined: it can leave the window, and motion stops
// there — plain Wayland has no way to hold it without pointer constraints.
// ---------------------------------------------------------------------------
class DeferredWarpPolicy {
public:
    /// Capture starts with the host pointer at (x, y). Returns whether the
    /// frontend must warp it to the centre (cx, cy): not when it is already
    /// within `margin`, which also avoids an in-flight warp at the one moment
    /// there is no earlier event to tell its landing from.
    bool begin(int x, int y, int cx, int cy, int margin) {
        last_x_       = x;
        last_y_       = y;
        first_          = true;
        warp_pending_   = beyond(x - cx, y - cy, margin);
        pending_events_ = 0;
        return warp_pending_;
    }

    /// Decide what to do with a motion event at host position (x, y) when the
    /// viewport centre is at (cx, cy). `margin` is how far the pointer may
    /// drift from the centre before it is warped back.
    Motion on_motion(int x, int y, int cx, int cy, int margin) {
        int dx = x - last_x_;
        int dy = y - last_y_;
        if (warp_pending_) {
            const int wx = x - cx;
            const int wy = y - cy;
            // Landed: nearer the centre than the last position, AND a jump of
            // at least margin/2 from it. The second half matters when the warp
            // is late or dropped (Wayland) and the pointer itself wanders back
            // through the centre: a plain step there is near the centre too,
            // but it is not a jump.
            const int jump = margin / 2;
            const int d2   = dx * dx + dy * dy;
            if (wx * wx + wy * wy < d2 && d2 >= jump * jump) {
                dx = wx;
                dy = wy;
                warp_pending_ = false;
            } else if (++pending_events_ > kWarpGiveUpEvents) {
                // Never seen to land: dropped (Wayland without pointer warp),
                // or landed while the pointer was itself near the centre. Stop
                // waiting, so a later drift past the margin can warp again.
                warp_pending_ = false;
            }
        }
        last_x_ = x;
        last_y_ = y;

        Motion m;
        // The first event of a capture is never forwarded (issue #37).
        if (!first_) {
            m.forward = dx != 0 || dy != 0;
            m.dx      = dx;
            m.dy      = dy;
        }
        first_ = false;
        // One warp in flight at a time. A second one requested before the
        // first is seen to land could land unseen: the first warp's evidence
        // would clear the flag for both, and the second jump would then be
        // read as motion.
        if (!warp_pending_ && beyond(x - cx, y - cy, margin)) {
            m.recentre      = true;
            warp_pending_   = true;
            pending_events_ = 0;
        }
        return m;
    }

    /// Exposed for tests: a requested warp has not been seen to land yet.
    bool warp_pending() const { return warp_pending_; }

    /// Events a warp may stay unseen before it is given up on.
    static constexpr int kWarpGiveUpEvents = 16;

private:
    static bool beyond(int ox, int oy, int margin) {
        return ox > margin || ox < -margin || oy > margin || oy < -margin;
    }

    bool first_          = false;
    bool warp_pending_   = false;
    int  pending_events_ = 0;
    int  last_x_       = 0;
    int  last_y_       = 0;
};

/// Whether a NON-macOS Qt frontend needs DeferredWarpPolicy, from
/// QGuiApplication::platformName() (macOS always does; decided at compile
/// time). True for every Wayland plugin ("wayland", "wayland-egl", ...):
///
///   * Qt <= 6.9 cannot warp on Wayland at all — QWaylandCursor::setPos only
///     logs "Setting cursor position is not possible on wayland" (qtwayland
///     6.9 src/client/qwaylandcursor.cpp:336-340) — so Policy would forward
///     the pointer's whole offset from the centre on every event (PCAP-14b).
///   * Qt >= 6.10 warps through wp_pointer_warp_v1 when the compositor has it,
///     but that is a request the compositor handles later and may reject
///     ("whether or not the compositor honors the request is implementation
///     defined", pointer-warp-v1.xml), with no event promised for it: the
///     late, silent case Policy cannot follow (PCAP-26).
///
/// Telling those apart is not needed — DeferredWarpPolicy is exact for a warp
/// that lands at once as well (PCAP-25) — and not possible through public Qt
/// API anyway. X11 ("xcb"), Windows and "offscreen" keep Policy.
inline bool deferred_warp_platform(std::string_view qpa_platform) {
    return qpa_platform.substr(0, 7) == "wayland";
}

/// The pointer-release chord as the user's keyboard names it (issue #307).
/// Both frontends release on their toolkit's Ctrl + Alt. Qt on macOS reads the
/// Command key as Ctrl (Qt::Key_Control / ControlModifier), so there the keys
/// pressed are Alt (Option) + Cmd. SDL never swaps (Command is its GUI key).
inline const char* release_chord(bool ctrl_is_command) {
    return ctrl_is_command ? "Alt+Cmd" : "Ctrl+Alt";
}

}  // namespace pointer_capture
