// Full-screen state sync and window size lock test (GitHub issue #404, macOS).
//
// No VHDL oracle: host window behaviour. The oracle is the issue's reported
// behaviour plus the documented Qt Cocoa rule it traces to:
//
//   * after leaving full screen with the green button, jnext must follow
//     (chrome back, size lock back, menu tick cleared) -- a disagreement
//     between the window's state and jnext's own is acted on, in the
//     direction the WINDOW went;
//   * a toggle jnext started itself must not be applied a second time when
//     its own WindowStateChange arrives;
//   * the green button is disabled by Qt's Cocoa plugin exactly when the
//     window's minimum size equals its maximum (QCocoaWindow::isFixedSize,
//     qtbase src/plugins/platforms/cocoa/qcocoawindow.mm), so the macOS lock
//     must keep max != min while staying within a pixel or so of the hint,
//     and size_lock(..., false) must add no slack. (No production code calls it with
//     false; the real non-macOS window is pinned by GH404-02 in window_scale_test.)
//
// Run: ./build/test/fullscreen_sync_test

#include "gui/fullscreen_sync.h"

#include <cstdio>
#include <string>
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

using fullscreen_sync::Action;
using fullscreen_sync::on_window_state_change;
using fullscreen_sync::size_lock;

}  // namespace

int main()
{
    // jnext state, window state, jnext toggle in progress
    check("FSYNC-01", "both windowed => nothing to do",
          on_window_state_change(false, false, false) == Action::None);
    check("FSYNC-02", "both full screen => nothing to do",
          on_window_state_change(true, true, false) == Action::None);
    check("FSYNC-03", "window entered full screen on its own => Enter",
          on_window_state_change(false, true, false) == Action::Enter);
    check("FSYNC-04", "window left full screen on its own (green button) => Exit",
          on_window_state_change(true, false, false) == Action::Exit);
    check("FSYNC-05", "jnext toggle in progress, window entering => nothing",
          on_window_state_change(false, true, true) == Action::None);
    check("FSYNC-06", "jnext toggle in progress, window leaving => nothing",
          on_window_state_change(true, false, true) == Action::None);
    check("FSYNC-07", "jnext toggle in progress, states agree => nothing",
          on_window_state_change(true, true, true) == Action::None);

    // The size lock.
    {
        const auto l = size_lock(1280, 1109, true);
        check("FSYNC-08", "macOS lock: maximum differs from minimum (Qt keeps the zoom button)",
              l.max_w != l.min_w && l.max_h != l.min_h);
        check("FSYNC-09", "macOS lock: minimum is exactly the hint",
              l.min_w == 1280 && l.min_h == 1109);
        check("FSYNC-10", "macOS lock: maximum within 2 px of the hint (window stays fixed)",
              l.max_w - l.min_w >= 1 && l.max_w - l.min_w <= 2 &&
              l.max_h - l.min_h >= 1 && l.max_h - l.min_h <= 2);
    }
    {
        const auto l = size_lock(1280, 1109, false);
        check("FSYNC-11", "size_lock(.., false) has no slack (helper argument only; the window itself is pinned by GH404-02)",
              l.min_w == 1280 && l.max_w == 1280 && l.min_h == 1109 && l.max_h == 1109);
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
