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

- In Chrome 156, in Firefox 159 and in Chrome with its CPU throttled to half speed (a
  rough stand-in for a slow machine) every program kept full emulation speed, with no
  audio underrun. On an otherwise idle machine every program also showed every picture
  (the lowest figure was 99.5% of the pictures). With some other load on the machine the
  three heaviest programs at half speed (`shift`, `blue` and `menu`) showed 95% to 98% of
  the pictures.

The CRT effect (10.2) is extra drawing work on the graphics chip of every frame, which is why it is off
by default; the figures above are without it.

This is what one machine did, not a promise for another: a slower computer, a busy
one or another browser will differ.
