// macOS-only AppKit full-screen glue (GH #404 follow-up); see the header.

#include "gui/mac_fullscreen.h"

#import <AppKit/AppKit.h>

namespace mac_fullscreen {

void init()
{
    // Drop AppKit's automatic "Enter Full Screen" View-menu item; jnext has
    // its own View > Fullscreen. Same call as wxWidgets (src/osx/cocoa/utils.mm),
    // Wireshark (ui/macosx/cocoa_bridge.mm), Sigil, calibre, mpv and others.
    [[NSUserDefaults standardUserDefaults] setBool:NO
                                            forKey:@"NSFullScreenMenuItemEverywhere"];

    // Remove the content size limits (minimum AND maximum) as a window is
    // about to enter full screen, so AppKit sizes it to the screen: the
    // windowed lock's minimum is the size hint, which can be taller than the
    // screen at 2x/3x. Registered for every window (object:nil) because the
    // main window does not exist yet; Qt makes every non-dialog top-level
    // window full-screen capable (the debugger window too), and the handler
    // touches only the window that posted the notification, so a window
    // without limits is unaffected. The observer lives for the whole process
    // and captures nothing, so there is no token to keep and the block is
    // correct under both manual retain/release and ARC.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowWillEnterFullScreenNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification* note) {
                    if (![note.object isKindOfClass:[NSWindow class]]) return;
                    NSWindow* window = (NSWindow*)note.object;
                    window.contentMinSize = NSMakeSize(0, 0);
                    window.contentMaxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
                }];
}

}  // namespace mac_fullscreen
