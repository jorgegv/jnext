# Optional: video recording

JNEXT can record a session to an MP4 (chapter 5.5). That feature — and only
that feature — needs **ffmpeg** installed and on your `PATH`. Everything else
works without it.

JNEXT does not bundle ffmpeg: it runs whichever `ffmpeg` it finds on your
`PATH`, so you install it the same way you install anything else on your
system.

| Platform | Install |
|---|---|
| Fedora / RHEL | `sudo dnf install ffmpeg` |
| Debian / Ubuntu | `sudo apt install ffmpeg` |
| macOS | `brew install ffmpeg` |
| Windows | **Recording is not available in the Windows build** — see [issue #56](https://github.com/jorgegv/jnext/issues/56). Installing ffmpeg will not enable it. |

Restart JNEXT after installing — it checks for ffmpeg once at startup, so a
running instance still shows recording as unavailable.
