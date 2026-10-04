// macOS-only cursor warp for pointer capture (issue #303); see the header.
// The whole file is empty on every other platform (the gui sources are globbed).

#ifdef __APPLE__
#include "gui/mac_cursor_warp.h"
#include "core/log.h"

#include <CoreGraphics/CoreGraphics.h>

namespace mac_cursor {

void warp(int x, int y)
{
    const CGError err = CGWarpMouseCursorPosition(CGPointMake(x, y));
    // A warp leaves the mouse disassociated from the cursor for a short
    // interval (~0.25 s), during which motion is lost. Re-associating at once
    // cancels it — the same call SDL3 makes after its own warp
    // (src/video/cocoa/SDL_cocoamouse.m, Cocoa_WarpMouseGlobal).
    CGAssociateMouseAndMouseCursorPosition(true);
    if (err != kCGErrorSuccess)
        Log::input()->warn("pointer capture: CGWarpMouseCursorPosition({}, {}) failed: {}",
                           x, y, static_cast<int>(err));
    else
        Log::input()->debug("pointer capture: warped to ({}, {})", x, y);
}

}  // namespace mac_cursor
#endif
