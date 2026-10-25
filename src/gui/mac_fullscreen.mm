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

    // Remove the content size limit as the window is about to enter full
    // screen, so AppKit sizes it to the screen. Registered for every window
    // (object:nil): only a window with the full-screen collection behaviour
    // can post this, i.e. the main window. The observer lives for the whole
    // process and captures nothing, so there is no token to keep and the
    // block is correct under both manual retain/release and ARC.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowWillEnterFullScreenNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification* note) {
                    NSWindow* window = (NSWindow*)note.object;
                    window.contentMaxSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
                }];
}

}  // namespace mac_fullscreen
