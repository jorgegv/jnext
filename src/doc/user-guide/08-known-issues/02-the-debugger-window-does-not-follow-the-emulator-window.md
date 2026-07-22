# 8.2 Debugger window attachment is not available on Wayland

**Fixed on X11.** The debugger window now attaches to the right-hand edge of
the emulator window and follows it as you drag or resize it. Toggle it from the
debugger's own **Window → Attach to emulator window** menu; the setting is
remembered between sessions.

**Not possible on Wayland.** Under a native Wayland session the menu item is
greyed out, with a tooltip explaining why. This is not something JNEXT can fix:
Wayland's window-management protocol (xdg-shell) has no request for an
application to position its own window. A client cannot say "put me here" —
placement belongs entirely to the compositor. Every application is in the same
position, and no amount of work on our side changes it.

**If you want attachment, run under X11:**

```console
$ QT_QPA_PLATFORM=xcb jnext
```

**XWayland is handled too.** An XWayland session reports itself as `xcb` — it
looks exactly like X11 from inside the application — but the compositor still
discards the move requests. JNEXT therefore does not trust the platform name
alone: it checks whether its moves actually land, and if several in a row are
ignored it stops trying, unticks the menu item, and says so in the status bar.
Re-tick the item to retry (after moving to a different screen, say). That
automatic stop does not change your saved preference — the next session starts
with attachment enabled again.

Originally reported as [issue #39](https://github.com/jorgegv/jnext/issues/39).
