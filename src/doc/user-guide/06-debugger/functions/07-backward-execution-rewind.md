# Backward execution (rewind)

Rewind lets you go back to an earlier instruction or an earlier frame. It works
by snapshotting the whole machine at frame boundaries into a ring buffer, so it
is **off by default** — the snapshots are large, and a few hundred frames run
to hundreds of megabytes.

Turn it on either way:

- `--rewind-buffer-size N` on the command line, where *N* is the number of
  frames to keep (`0`, the default, means off). This also switches the trace log
  on, because stepping back needs it.
- **Debug ▸ Rewind ▸ Enable Rewind** in the debugger, with
  **Debug ▸ Rewind ▸ Rewind Buffer Size…** to set the depth. Resizing clears
  the snapshots already recorded.

Once there is history, a rewind toolbar appears at the bottom of the window:

| Action | Key | Effect |
|---|---|---|
| Step Back | **Shift+F7** | Undo the last instruction |
| Frame Back | **Shift+F6** | Jump to the start of the previous frame |
| Slider | — | Drag to any frame in the buffer and release to jump there |

A rewind lands you at the *start* of the target frame, paused, with the
emulator window redrawn to match. The status bar reports the buffer's size in
frames and megabytes, and when you are rewound it shows which frame you are on.
Press **F5** to carry on from there.

Step Back is greyed out when the trace log is off, when the buffer is empty, or
during RZX playback. If a snapshot ever fails to restore cleanly, JNEXT says so
loudly and pauses rather than continuing on a half-restored machine; reset the
machine (**Machine ▸ Reset**) to recover.
