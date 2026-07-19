#pragma once

// ---------------------------------------------------------------------------
// Pointer-capture motion policy (issue #37).
//
// A Kempston mouse is a RELATIVE device: it reports motion, never position.
// The Qt frontend therefore confines the host pointer by warping it back to
// the viewport centre after every motion event and forwarding the delta
// measured from that centre — so the pointer never reaches a screen edge and
// the guest can travel without limit.
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

}  // namespace pointer_capture
