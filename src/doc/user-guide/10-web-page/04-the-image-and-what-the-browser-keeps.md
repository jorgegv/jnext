# 10.4 The SD-card image, and what the browser keeps

## The image

JNEXT runs NextZXOS from an SD card in the browser too. The page names one by
`image: {url, sha256}`. JNEXT does not ship, host or serve the image. The browser
build is made and tested for the small NextZXOS boot card (40 MiB), not for the 1 GB
distribution image.

The image may also be a `.zip` holding exactly one `.img` file. The page recognises it
by its content, and the `sha256` you give is the digest of the zip, not of the card
inside it.

To try a page on your own computer, make the small card from the distribution image
JNEXT downloaded for the desktop emulator (chapter 3), with
`tools/make-minimal-sdcard-image.sh` from a JNEXT source checkout. Then serve it
next to the page:

```console
$ tools/make-minimal-sdcard-image.sh ~/.jnext/sdcard/cspect-next-1gb.img card.img
$ sha256sum card.img
$ python3 -m http.server 8000
```

The directory you serve holds `jnext-VERSION.js`, `jnext-VERSION.wasm`, `plain.html`,
`card.img` and the program (`game.nex` here). Open
`http://localhost:8000/plain.html?image=card.img&sha256=<the digest>&file=game.nex`
and click the player. A page opened from disk (`file://`) does not work: see
"If it does not start" below.

## The first visit

The visitor sees these steps, in this order:

1. `download`: the image is fetched and its SHA-256 is checked. An image that does not
   match the digest is refused.
2. "Click to start".
3. For a `.nex` program only, `warm-start`: NextZXOS boots once and the machine is
   recorded.
4. `onReady`.

The download is a cross-origin read made without credentials, so the image's server
must send `Access-Control-Allow-Origin`. The page can report the progress of the
download only when the server sends `Content-Length` and does not compress the file;
otherwise `onProgress` reports only 0 and 1.

## Later visits

A later visit makes no request for the image. The cached copy is hashed again every
time, and a copy that no longer matches the digest is deleted and fetched again.

For a `.nex` program the recorded machine is brought back (`restore`) instead of booting
NextZXOS again. A corrupted recording is made again. A `.tap`, a `.tzx`, or a page with
no program records nothing: NextZXOS boots on every visit.

## What the browser keeps, and where

- The image is kept in the browser's Cache Storage, under its URL.
- The warm-start recording is kept in IndexedDB.
- Nothing else is kept: no settings, and none of the writes the program makes to the
  card, nor screenshots or snapshots (10.3).
- Several players on one page share the recording.
- If the browser's storage is full, the session still runs and the next visit downloads
  the image again.

Browsers keep each site's storage apart. Two pages of one site share the image and the
recording. The same game embedded on another site, or inside another site's `iframe`,
downloads the image and records the machine again. Clearing the site's data in the
browser makes the next visit a first visit.

## If it does not start

The page reports these messages through `onError`, and the example pages show them in
their status line.

| message | what it means |
|---|---|
| `unknown option '<name>'`, `image is required` or `option '<name>' must be ...` | A `mount()` option is misspelt, missing or of the wrong type. Nothing has been downloaded yet. |
| `no image url` | `image.url` is empty. |
| `image: no URL configured` | `plain.html` was opened with no `?image=` and no URL in its constants. |
| `the image sha256 is not 64 hex digits` | The digest is missing or mistyped. |
| `cannot fetch <url>: a network error, or the server does not allow cross-origin reads (no Access-Control-Allow-Origin header)` | The server is unreachable, or it does not send `Access-Control-Allow-Origin`. Make it send the header. |
| `cannot fetch <url>: HTTP <status>` | The server answered with an error. Check the URL. The program file is fetched by the page too, so a program on another site needs `Access-Control-Allow-Origin` as well: give `{data, name}` to avoid the network. |
| `SHA-256 mismatch for <url>: got ..., expected ...` | The file served is not the one the digest names. Recompute the digest with `sha256sum`. |
| `the SD-card image needs a secure context (https, or http://localhost): ...` | The page is not served over `https://`, `http://localhost` or `http://127.0.0.1`. |
| `--load <file>: a browser page loads only .nex, .tap, .tzx programs` | The program has another extension. |
| `<flag>: not available in the browser build (...)` or `<flag>: not available in a browser page (...)` | An option that needs a process, a socket or a terminal was given. The reason is in the brackets, and the list is in the man page ("Not available in the browser"). |
| `jnext exited with code N` | The machine stopped with an error after it had started. |
| the lines the machine printed, or `jnext: the machine did not start` | The machine stopped before its first frame. The lines say why. |
