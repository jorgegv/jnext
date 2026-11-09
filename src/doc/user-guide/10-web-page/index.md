# 10. JNEXT in a web page

JNEXT also runs in a browser tab: the same emulator, built for WebAssembly, loaded
by a page you write (or by an itch.io upload). A page needs one script file,
`jnext-VERSION.js`, with `jnext-VERSION.wasm` beside it. The script defines one
global, `jnext`, and `jnext.mount(container, options)` puts a player in a DOM
element.

Every public release carries `jnext-VERSION-web.zip`: the two files, the three
example pages (`plain.html`, `itch-game.html`, `itch-host.html`), a `README.txt`
with the link to the source of that build, `LICENSE`, `CREDITS.md` and a
`licenses/` directory. Host `jnext-VERSION.js` and `jnext-VERSION.wasm` together,
and keep the licence files beside them. There is no NextZXOS in the zip: the
page fetches the system image from the address you give it.

Everything the player is told is a command-line flag or a `--set` key of the
desktop emulator; the reference list is in the man page section "THE BROWSER
BUILD". The player's `argv` property shows the command line your options became.

This chapter has four pages: a page with a game (10.1), the options and the
player's controls (10.2), putting it on itch.io, plus what the browser build does
not do (10.3), and the SD-card image, what the browser keeps and what to do when a
page does not start (10.4).
