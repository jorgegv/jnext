# 10.3 On itch.io, and what the browser build does not do

## An itch.io upload

Zip a page like `itch-game.html` (the release ships it) with `jnext-VERSION.js` and
`jnext-VERSION.wasm`, upload it as an HTML game, and set the frame size on the
itch.io page. itch.io embeds your page in an `iframe` of its own site, so:

- Game controllers need `allow="gamepad"` on that `iframe` (itch.io sets it);
  `itch-host.html` shows the markup.
- Browsers keep storage per embedding site: the first visit on each site downloads
  the image again, and the stored boot recording does not carry over.
- The image's server must allow cross-origin reads, as for any page.

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
