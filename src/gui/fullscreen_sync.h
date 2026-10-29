#pragma once

// ---------------------------------------------------------------------------
// Full-screen state sync and window size lock (GH #404, macOS).
//
// Two decisions the Qt main window makes on macOS, kept here free of Qt so a
// Linux unit test reaches them (the macOS-only glue in MainWindow is the
// only part that cannot be compiled off a Mac).
//
// 1. Who changed the full-screen state. On macOS the window's title-bar green
//    button (and the system menu) enters and leaves full screen without
//    asking jnext. MainWindow then sees a WindowStateChange that disagrees
//    with its own is_fullscreen_ and must apply the same chrome / size work
//    View > Fullscreen does, without calling showFullScreen()/showNormal()
//    again. A jnext-initiated toggle must NOT be applied twice: it sets
//    `applying` for its duration, and its own state change finds the two
//    states already agreeing.
//
// 2. The windowed size lock. Qt's Cocoa plugin disables the green button on
//    a window whose minimum size equals its maximum size
//    (QCocoaWindow::isFixedSize, qtbase src/plugins/platforms/cocoa/
//    qcocoawindow.mm, re-evaluated on every size-constraint change), and no
//    window flag overrides it. apply_fixed_window_size() used to pin both to
//    sizeHint(). On macOS the maximum is now one pixel larger than the
//    minimum: the window is still fixed for every practical purpose, but is
//    no longer "fixed size" to Qt, so the button stays enabled.
// ---------------------------------------------------------------------------

namespace fullscreen_sync {

enum class Action { None, Enter, Exit };

/// What MainWindow must do on a WindowStateChange.
///   jnext_fullscreen    MainWindow::is_fullscreen_
///   window_fullscreen   windowState() & Qt::WindowFullScreen
///   applying            a jnext-initiated toggle_fullscreen() is running
inline Action on_window_state_change(bool jnext_fullscreen, bool window_fullscreen,
                                     bool applying)
{
    if (applying || jnext_fullscreen == window_fullscreen) return Action::None;
    return window_fullscreen ? Action::Enter : Action::Exit;
}

/// Extra pixels between the minimum and maximum window size on macOS.
inline constexpr int kZoomSlackPx = 1;

struct SizeLock {
    int min_w, min_h, max_w, max_h;
};

/// Min / max size of the windowed main window for a layout size hint.
/// `keep_zoom_button` is true on macOS, false elsewhere (exact fixed size).
inline SizeLock size_lock(int hint_w, int hint_h, bool keep_zoom_button)
{
    const int s = keep_zoom_button ? kZoomSlackPx : 0;
    return SizeLock{hint_w, hint_h, hint_w + s, hint_h + s};
}

}  // namespace fullscreen_sync
