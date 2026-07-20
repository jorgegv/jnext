# 8.3 The debugger window does not follow the emulator window

**What you see.** Move the main emulator window and the debugger window stays
where it was, so the two drift apart and have to be re-arranged by hand.

**What works today.** The debugger's position *is* remembered between
sessions, so it reopens where you last left it. It just does not track the
emulator window while that window is being moved.

**Workaround.** Place both windows once; the layout survives a restart.

Tracked as [issue #39](https://github.com/jorgegv/jnext/issues/39).
