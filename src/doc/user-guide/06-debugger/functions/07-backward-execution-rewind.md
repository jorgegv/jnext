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
| Frame Back | **Shift+F6** | Jump to the start of the frame — or, if you are already at one, of the frame before |
| Slider | — | Drag to any frame in the buffer and release to jump there |

A rewind lands you at the *start* of the target frame, paused, with the
emulator window redrawn to match. The status bar reports the buffer's size in
frames and megabytes, and when you are rewound it shows which frame you are on,
numbered as the slider numbers it. Running forward again from a rewound frame
discards the frames after it: the slider then ends at the frame you are on.
So does changing the machine from the debugger while rewound — a memory,
register or NextREG edit, a script's `set`, a state load: the frames after it
were recorded without that change, so they are dropped, and the slider can no
longer go forward into them. Press **F5** to carry on from there.

Step Back also refuses a step that would have to replay across a change you
made from the debugger, because the replay would silently lose it. A step back
to before the change is allowed, and undoes it.

### When a rewind is refused

When Step Back, Frame Back or the slider cannot do what you asked, the debugger
window's status bar says why for ten seconds, and the same line goes to the
log, for example:

```
Step Back refused: it would undo a change you made from the debugger in frame 12 — step back to before the change, or use Frame Back. See the user guide: Debugger ▸ Functions ▸ Backward execution (rewind)
```

| The message says | What to do |
|---|---|
| rewind is off | Turn on **Debug ▸ Rewind ▸ Enable Rewind**, then run forward |
| the rewind buffer holds no frames yet | Run forward first: there is nothing recorded to go back to |
| the instruction trace is off | Turn on **Debug ▸ Trace ▸ Enable Trace**, then run forward |
| the instruction trace is empty | Run forward first |
| an RZX recording is being made / is playing | Stop the recording or the playback |
| frame N is not in the rewind buffer | Pick a frame inside the slider's range |
| it would undo a change you made from the debugger in frame N | Step back to before the change, or use Frame Back to the start of that frame (both undo the change), or carry on forward |

Step Back is greyed out when the trace log is off, when the buffer is empty,
and while an RZX recording plays or is being made: a recording replays one
continuous run, so it cannot follow the machine back in time. If a snapshot ever fails to restore cleanly, JNEXT says so
loudly and pauses rather than continuing on a half-restored machine; reset the
machine (**Machine ▸ Power Reset**) to recover.
