# 8. JNEXT in a web page

JNEXT also runs in a browser tab: the same emulator, built for WebAssembly, loaded
by a page you write (or by an itch.io upload).

A live demo runs at <https://jorgegv.github.io/jnext/>.

## What you need

- One script file, `jnext-VERSION.js`, with `jnext-VERSION.wasm` beside it.
- The script defines one global, `jnext`.
- `jnext.mount(container, options)` puts a player in a DOM element.

## What the release carries

Every public release ships `jnext-VERSION-web.zip`:

- the two files above;
- the ready-made widget, `jnext-widget.js`;
- `jnext-debug-bridge.mjs`, a node script that lets DeZog or `z88dk-gdb` debug a page ([8.6](06-debugging-from-the-page.md));
- four example pages: `plain.html`, `widget.html`, `itch-game.html`, `itch-host.html`;
- a `README.txt` with the link to the source of that build;
- `LICENSE`, `CREDITS.md` and a `licenses/` directory.

Host `jnext-VERSION.js` and `jnext-VERSION.wasm` together, and keep the licence files
beside them.

There is no NextZXOS in the zip: NextZXOS is SpecNext's copyrighted software, and the
player gets it from specnext.com ([8.4](04-the-image-and-what-the-browser-keeps.md)).

## Where to start

- **Just want it to work?** Read [8.1](01-a-page-with-a-game.md), then copy one of the
  two pages of [8.5](05-two-ways-to-embed.md).
- **Want to tune it?** Each page ends with an *Advanced configuration* section.

Everything the player is told is a command-line flag or a `--set` key of the desktop
emulator. The reference list is the man page section "THE BROWSER BUILD". The player's
`argv` property shows the command line your options became.

## The pages of this chapter

| page | what it covers |
|---|---|
| [8.1 A page with a game](01-a-page-with-a-game.md) | The shortest working page, what happens when the visitor clicks, the example pages |
| [8.2 Options and the player](02-options-and-the-player.md) | The options, the events, the player's controls, the keyboard, snapshots and recordings |
| [8.3 On itch.io, and what the browser build does not do](03-itch-and-limits.md) | An itch.io upload, several players on a page, the limits, the speed |
| [8.4 NextZXOS, the SD card, and what the browser keeps](04-the-image-and-what-the-browser-keeps.md) | Where NextZXOS comes from, what is stored, what to do when a page does not start |
| [8.5 Two ways to embed JNEXT](05-two-ways-to-embed.md) | Your own page or the ready-made widget, each as a complete page to copy |
| [8.6 Debugging from the page](06-debugging-from-the-page.md) | The `debug` option, the channel, the WebSocket bridge, DeZog and `z88dk-gdb` against a page |
