# 10.1 A page with a game

The release's `jnext-VERSION-web.zip` holds `plain.html`, a whole page in one file. Its core is this:

```html
<div id="player"></div>
<script src="jnext-VERSION.js"></script>
<script>
  jnext.mount(document.getElementById('player'), {
    file: 'game.nex',
    width: 640, height: 512,
    poster: 'game.png',   // optional: a picture shown before the click
    onProgress: (phase, fraction) => { /* show a progress bar */ },
    onReady: () => { /* the program is running */ },
    onError: (message) => { /* tell the visitor */ }
  }).then((player) => { window.player = player; });
</script>
```

What happens, in order:

1. `mount()` checks the options. A name it does not know, or a value of the wrong
   type, rejects the promise at once and names the option; nothing is downloaded.
2. The player shows "Click to start", over the `poster` picture when the page gave one
   (without it the player area is empty until the click). Nothing has been downloaded
   or built yet.
3. The visitor clicks. Now the page gets NextZXOS's SD card (10.4): the card the browser
   kept on an earlier visit, or one it makes from the official NextZXOS file from
   specnext.com. `onProgress('download', fraction)` reports a download,
   `onProgress('build', fraction)` the making of the card from a file the visitor chose,
   and `onProgress('choose', 0)` says the visitor is being asked to choose that file.
4. The program in `file` is fetched, and the machine starts (browsers allow sound only
   after a click).
   When the program is a `.nex`, the first time the machine boots NextZXOS in a
   browser it is recorded, so that the next visit starts in a moment:
   `onProgress('warm-start', fraction)` reports it, and `onProgress('restore', 1)`
   marks a visit that used the stored recording. A `.tap`, a `.tzx`, a `.wav`, an `.rzx` recording, a snapshot or no program
   records nothing (10.4).
5. `onReady()` is called when the program is in the machine and a frame has been
   shown.

Requirements:

- A program file on another site needs that site's server to send
  `Access-Control-Allow-Origin`, because the page fetches it; `file: {data, name}`
  needs no request.
- Serve the `.wasm` file as `application/wasm`. With another type the page still
  starts, after a warning in the browser's console. No special headers are needed
  otherwise.
- The page must be a secure context: `https://`, `http://localhost` or
  `http://127.0.0.1`. The cache and the SHA-256 check need it.
- NextZXOS is not part of JNEXT and is not shipped with it. You give the player nothing
  to find it with: it gets it from specnext.com, or asks the visitor to download it from
  there (10.4).

## The example pages

`plain.html` reads its settings from the query string: `?file=` (the program) and
`&other=` (the program the "Load other" button loads). Without them it uses the
constants at the top of its script, which also choose how the page gets NextZXOS
(`card.source`, 10.2).
`itch-game.html` takes `?file=`, and posts `jnext-ready`
or `jnext-error: <message>` to the page that embeds it. `itch-host.html` embeds
`itch-game.html`, or the page named by `?game=` (an `http://` or `https://` URL only).
