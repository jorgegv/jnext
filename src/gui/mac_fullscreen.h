#pragma once

// macOS-only AppKit glue for full screen (GH #404 follow-up).
//
// 1. The green title-bar button enters full screen without asking jnext, and
//    jnext's windowed size lock (contentMaxSize = size hint + 1 px, see
//    fullscreen_sync.h) is still on the NSWindow when AppKit sizes the new
//    space. Qt's own will-enter handler only makes the window resizable
//    (QCocoaWindow::windowWillEnterFullScreen, qtbase
//    src/plugins/platforms/cocoa/qcocoawindow.mm); it leaves the minimum
//    and maximum alone, so the window stays at its windowed size, centred on black.
//    View > Fullscreen is unaffected: it lifts the Qt limits before
//    showFullScreen(). This hooks NSWindowWillEnterFullScreenNotification and
//    removes the content minimum and maximum there, as View > Fullscreen already does.
//
// 2. AppKit adds an "Enter Full Screen" item to the View menu of every app on
//    its own. It duplicates View > Fullscreen (F11, Control+Command+F) and
//    competes for the same shortcut. The NSFullScreenMenuItemEverywhere user
//    default turns it off.
//
// Kept in its own Objective-C++ file so AppKit stays out of the Qt
// translation units. Built only under if(APPLE) in src/gui/CMakeLists.txt.

#ifdef __APPLE__
namespace mac_fullscreen {

/// Install both. Call once, BEFORE the QApplication is constructed (the
/// default must be set before AppKit builds the menus).
void init();

}  // namespace mac_fullscreen
#endif
