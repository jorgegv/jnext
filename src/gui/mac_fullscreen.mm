// macOS-only AppKit full-screen glue (GH #404 follow-up); see the header.

#include "gui/mac_fullscreen.h"

#import <AppKit/AppKit.h>

namespace mac_fullscreen {

namespace {
// The main window's QNSView (Qt's WId). Deliberately NOT retained: the view
// lives as long as the main window, which lives as long as the process, and
// is looked up through .window so it follows Qt moving it to another NSWindow.
// Only read and written on the main thread (GUI thread).
NSView* g_main_view = nil;
}

void init()
{
    // Drop AppKit's automatic "Enter Full Screen" View-menu item; jnext has
    // its own View > Fullscreen. Same call as wxWidgets (src/osx/cocoa/utils.mm),
    // Wireshark (ui/macosx/cocoa_bridge.mm), Sigil, calibre, mpv and others.
    [[NSUserDefaults standardUserDefaults] setBool:NO
                                            forKey:@"NSFullScreenMenuItemEverywhere"];

    // Remove the content size limits (minimum AND maximum) of jnext's MAIN
    // window as it is about to enter full screen, so AppKit sizes it to the
    // screen: the windowed lock's minimum is the size hint, which can be taller
    // than the screen at 2x/3x. Registered for every window (object:nil)
    // because the main window does not exist yet at init, but the handler
    // returns unless the notifying window is the main window's current NSWindow
    // (g_main_view.window). Other full-screen capable windows (the debugger)
    // keep their own limits. The observer lives for the whole process and
    // captures nothing but a global, so there is no token to keep and the block
    // is correct under both manual retain/release and ARC.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowWillEnterFullScreenNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification* note) {
                    if (g_main_view == nil || note.object != g_main_view.window) return;
                    NSWindow* window = (NSWindow*)note.object;
                    window.contentMinSize = NSMakeSize(0, 0);
                    window.contentMaxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
                }];
}

void set_main_window(std::uintptr_t view)
{
    g_main_view = (NSView*)view;
}

}  // namespace mac_fullscreen
