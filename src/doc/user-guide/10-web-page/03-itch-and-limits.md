# 10.3 On itch.io, and what the browser build does not do

## An itch.io upload

Zip a page like `itch-game.html` (the release ships it) with `jnext-VERSION.js` and
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
