# 10.3 On itch.io, and what the browser build does not do

## An itch.io upload

Zip a page like `itch-game.html` (it is in `jnext-VERSION-web.zip`) with `jnext-VERSION.js` and
`jnext-VERSION.wasm`, upload it as an HTML game, and set the frame size on the
itch.io page. itch.io embeds your page in an `iframe` of its own site, so:

- Game controllers need `allow="gamepad"` on that `iframe`; `itch-host.html` shows the markup.
- Browsers keep storage per origin and may give an embedded page storage of its own, so
  the image may download again on the embedding site (see 10.4, "What the browser
  keeps, and where").
- The image's server must allow cross-origin reads, as for any page.
- `itch-game.html` tells the page that embeds it when the game is running
  (`jnext-ready`) or has failed (`jnext-error: <message>`), as `postMessage` text.

## Several players on a page

Each `mount()` makes its own emulator, with its own canvas and keyboard focus.
Game controllers are page-wide: give the other players
`args: ['--joy1-source', 'none']`.

## What the browser build does not do

- Flags and `--set` keys that need a process, a socket or a terminal are refused with
  a message; the list is in the man page ("Not available in the browser").
- Only `.nex`, `.tap` and `.tzx` programs load. `--headless` and RZX are refused.
- Writes the guest makes to the SD card are not kept: the card lives in memory for the
  session. Files JNEXT writes (screenshots, snapshots) live in memory too.
- Ctrl+W, Ctrl+T, Ctrl+N, Ctrl+Q and the operating system's own keys never reach the page.

## How fast it runs

Measured on one 2026 laptop (a Ryzen AI 5 340), with the 39 programs of the
project's test set (the NEX programs of its regression suite and a 3.5 MHz one),
each started on its own and left to run for 600 frames:

- In Chrome 156 and in Firefox 159 every program ran at full speed and showed every
  frame: the pictures per second reached the program's own refresh (49.36 Hz, or 58.15 Hz
  for programs that switch to 60 Hz), with no audio underrun.
- With Chrome's CPU throttled to half speed (a rough stand-in for a slow machine), 29
  of the 39 still did. The other ten kept the emulation at full speed but showed
  between 81% and 99% of the pictures (the heaviest, `blue`, 81%).

This is what one machine did, not a promise for another: a slower computer, a busy
one or another browser will differ.
