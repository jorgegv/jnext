# 10. JNEXT in a web page

JNEXT also runs in a browser tab: the same emulator, built for WebAssembly, loaded
by a page you write (or by an itch.io upload). A page needs one script file,
`jnext-VERSION.js`, with `jnext-VERSION.wasm` beside it. The script defines one
global, `jnext`, and `jnext.mount(container, options)` puts a player in a DOM
element.

Everything the player is told is a command-line flag or a `--set` key of the
desktop emulator; the reference list is in the man page section "THE BROWSER
BUILD". The player's `argv` property shows the command line your options became.

This chapter has three pages: a page with a game (10.1), the options and the
player's controls (10.2), and putting it on itch.io, plus what the browser build
does not do (10.3).
