# 10.1 A page with a game

The release ships `plain.html`, a whole page in one file. Its core is this:

```html
<div id="player"></div>
<script src="jnext-VERSION.js"></script>
<script>
  jnext.mount(document.getElementById('player'), {
    image: { url: 'https://example.com/card.img', sha256: '...64 hex digits...' },
    file: 'game.nex',
    width: 640, height: 512,
    onProgress: (phase, fraction) => { /* show a progress bar */ },
    onReady: () => { /* the program is running */ },
    onError: (message) => { /* tell the visitor */ }
  }).then((player) => { window.player = player; });
</script>
```

What happens, in order:

1. `mount()` checks the options. A name it does not know, or a value of the wrong
   type, rejects the promise at once and names the option; the SD-card image is not downloaded.
2. The page fetches the SD-card image from `image.url`, checks its SHA-256 and keeps
   it in the browser's cache. The next visit checks the cached copy and does not
   download it again. `onProgress('download', fraction)` reports the download.
3. The program in `file` is fetched too, and the promise resolves with the player.
   The overlay says "Click to start".
4. The visitor clicks. The machine starts (browsers allow sound only after a click).
   The first time a machine boots NextZXOS in a browser it is recorded, so that the
   next visit starts in a moment: `onProgress('warm-start', fraction)` reports it,
   and `onProgress('restore', 1)` marks a visit that used the stored recording.
5. `onReady()` is called when the program is in the machine and a frame has been
   shown.

Requirements:

- The image's server must send `Access-Control-Allow-Origin` (the page reads the
  card from another origin), and the `.wasm` file must be served as
  `application/wasm`.
- The page must be a secure context: `https://`, `http://localhost` or
  `http://127.0.0.1`. The cache and the SHA-256 check need it.
- The SD-card image is not part of JNEXT and is not shipped with it: you supply
  its URL and digest.
