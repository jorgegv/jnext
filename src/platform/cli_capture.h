#pragma once

// ---------------------------------------------------------------------------
// The `--delayed-screenshot` ACTION, through the debugger backend (GH #276 B4,
// owner decision O2): the three loop owners keep their own loop-tick countdown
// for the flag — a different time domain from the backend's `Frame` tags, and
// the one that lets `--delayed-automatic-exit` stay a hard bound while paused
// and lets a countdown survive a cold boot — and when it comes due they hand the
// capture itself to the backend's CAP-01 `screenshot()`, so the CLI and every
// debugger client take screenshots one way.
//
// Queued as the backend's own (`CLIENT_NONE`): the flag is the loop owner's, not
// an attached client's, and attaching would arm the machine. The format is the
// path's extension — the one table, `screenshot_format_for_path()`.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>

#include "core/screenshot.h"
#include "debug/debugger.h"

inline jnext::dbg::Result queue_cli_screenshot(jnext::dbg::Debugger& dbg,
                                               const std::string& path,
                                               uint8_t layer_mask) {
    const jnext::dbg::ScreenshotFormat fmt =
        screenshot_format_for_path(path) == ::ScreenshotFormat::Scr
            ? jnext::dbg::ScreenshotFormat::Scr
            : jnext::dbg::ScreenshotFormat::Png;
    return dbg.screenshot(jnext::dbg::CLIENT_NONE, path, layer_mask, fmt);
}
