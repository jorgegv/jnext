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
// render_policy.h / frame_deadline.h pure). SDL does not use this: its
// relative mode implements the equivalent natively.
// ---------------------------------------------------------------------------

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
// DeferredWarpPolicy — the macOS policy (issue #303).
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
// The frontend therefore warps with CGWarpMouseCursorPosition (no permission
// needed, no echo event), and this policy does not assume the warp has
// landed. Deltas are measured from the LAST position seen, which is exact
// whatever the warp does; the pointer is allowed to drift and is re-centred
// only once it is more than `margin` from the centre, and never while an
// earlier warp is still outstanding. While a warp is outstanding an event is
// read both ways — from the last position (it predates the warp) and from the
// centre (it follows it) — and the nearer reading wins: the two candidates are
// at least `margin` apart, so a single event would have to travel about
// margin/2 to be misread.
// ---------------------------------------------------------------------------
class DeferredWarpPolicy {
public:
    /// Capture started and a warp to the centre was requested. The position
    /// the pointer is measured from is unknown until the first event.
    void begin() {
        have_last_    = false;
        warp_pending_ = true;
    }

    /// Decide what to do with a motion event at host position (x, y) when the
    /// viewport centre is at (cx, cy). `margin` is how far the pointer may
    /// drift from the centre before it is warped back.
    Motion on_motion(int x, int y, int cx, int cy, int margin) {
        int dx = x - last_x_;
        int dy = y - last_y_;
        const bool first = !have_last_;
        if (warp_pending_) {
            const int wx = x - cx;
            const int wy = y - cy;
            // A first event has no previous position to compare with. Within
            // the margin it follows the begin() warp (CGWarpMouseCursorPosition
            // lands at once and sends no event of its own, so this is the
            // usual case); beyond it, it is a stale pre-capture position
            // (Policy's PCAP-03 case) and the warp is still to be seen.
            const bool near_centre = wx <= margin && wx >= -margin &&
                                     wy <= margin && wy >= -margin;
            if (first ? near_centre
                      : (wx * wx + wy * wy < dx * dx + dy * dy)) {
                dx = wx;
                dy = wy;
                warp_pending_ = false;
            }
        }
        have_last_ = true;
        last_x_    = x;
        last_y_    = y;

        Motion m;
        // The first event of a capture is never forwarded (issue #37).
        if (!first) {
            m.forward = dx != 0 || dy != 0;
            m.dx      = dx;
            m.dy      = dy;
        }
        // One warp in flight at a time. A second one requested before the
        // first is seen to land could land unseen: the first warp's evidence
        // (an event near the centre) would clear the flag for both, and the
        // second jump would then be read as motion.
        const int ox = x - cx;
        const int oy = y - cy;
        if (!warp_pending_ &&
            (ox > margin || ox < -margin || oy > margin || oy < -margin)) {
            m.recentre    = true;
            warp_pending_ = true;
        }
        return m;
    }

    /// Exposed for tests: a requested warp has not been seen to land yet.
    bool warp_pending() const { return warp_pending_; }

private:
    bool have_last_    = false;
    bool warp_pending_ = false;
    int  last_x_       = 0;
    int  last_y_       = 0;
};

}  // namespace pointer_capture
