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
`tools/make-minimal-sdcard-image.sh` from a JNEXT source checkout. The script runs on Linux
only and needs `mtools`, `sfdisk` (util-linux; the `fdisk` package on Debian and
Ubuntu), coreutils, grep and sed; it says which are missing before it starts. It also
needs a destination directory on a file system that supports hard links. Then serve it next to the page:

```console
$ tools/make-minimal-sdcard-image.sh ~/.jnext/sdcard/cspect-next-1gb.img card.img
$ sha256sum card.img
$ python3 -m http.server 8000
```

The directory you serve holds `jnext-VERSION.js`, `jnext-VERSION.wasm`, `plain.html`,
`card.img` and the program (`game.nex` here). Open
`http://localhost:8000/plain.html?image=card.img&sha256=<the digest>&file=game.nex`
and click the player. A page opened from disk (`file://`) does not work: the browser
refuses to load the `.wasm` file from a `file://` address and the page's status line
reads `error: Aborted(both async and sync fetching of the wasm failed ...)`.

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
NextZXOS again. A corrupted recording is made again. A `.tap`, a `.tzx` or a `.wav` starts directly in 48K
BASIC, and an `.rzx` on the machine it was recorded on, without NextZXOS, so there is nothing to record. A Next page with no program boots
NextZXOS on every visit and records nothing.

## What the browser keeps, and where

- The image is kept in the browser's Cache Storage, under its URL.
- The warm-start recording is kept in IndexedDB.
- With `keepWrites`, the sectors the program writes to the card are kept in IndexedDB, in
  a database of their own (`jnext-card-writes`) that exists only for a page that uses it.
  There is one record per image and page: its name is the image's SHA-256 and a short
  digest of the page's path, so another image, or another page of the same site, has
  its own saves, and a changed image never reuses the old ones. Only the written sectors
  are stored (516 bytes each; the card itself is never written), and they stay in the
  visitor's browser: nothing is sent anywhere.
- Nothing else is kept: no settings, nor screenshots or snapshots (10.3).
- Several players on one page share the recording.
- If the browser's storage is full, the session still runs and the next visit downloads
  the image again.

### Keeping the saves

```js
jnext.mount(el, { image: {...}, file: 'game.nex', keepWrites: true });
```

Whatever the program writes to the card is there at the next visit, and so is the one file a
NEX loaded with `file` saves through esxDOS (10.3). `player.resetCard()` clears both (10.2). A save is stored a moment after the program makes it: a reload within moments of a save can lose it (the store on leaving the page is best effort).
The saves belong to the image: a page that now names another image, even one that
differs in one byte, starts with none. A record made for another image is refused with a message naming
`--sdcard-writes-reset`: mount once with `args: ['--sdcard-writes-reset']` to discard it. A
record whose header is not a valid one (damaged, or in a format this version does not
read) is refused as "not a jnext SD-writes file" and the reset does not remove it: clear the
site's data in the browser, or delete the record from the `jnext-card-writes` database
(store `journals`) with the browser's developer tools.
A `.jns` snapshot (10.2) restored over a card that has newer saves leaves NextZXOS's in-memory
picture of the card out of step with the card, as on the desktop (5.9).

Browsers keep the storage of each origin (scheme, host and port) apart. Two pages of one
origin share the image and the recording. A game inside an `iframe` of another site may
or may not see the storage its own origin has: that depends on the browser, its version
and its privacy settings (in the Chromium used to test JNEXT it did), so do not rely on
either. Clearing the site's data in the browser makes the next visit a first visit.

## If it does not start

The page reports these messages through `onError`, and the example pages show them in
their status line.

| message | what it means |
|---|---|
| `unknown option '<name>'`, `image is required` or `option '<name>' must be ...` | A `mount()` option is misspelt, missing or of the wrong type. Nothing has been downloaded yet. |
| `no image url` | `image.url` is empty. |
| `image: no URL configured` | `plain.html` was opened with no `?image=` and no URL in its constants. |
| `the image sha256 is not 64 hex digits` | The digest is a string, but not 64 hex digits: it is mistyped. |
| `option 'image' must be an object {path, sha256}` | The digest is missing or is not a string. |
| `option 'image': the sha256 is all zeros` | The digest is 64 zeros, which never names a real image. |
| `cannot fetch <url>: a network error, or the server does not allow cross-origin reads (no Access-Control-Allow-Origin header)` | The server is unreachable, or it does not send `Access-Control-Allow-Origin`. Make it send the header. |
| `cannot fetch <url>: HTTP <status>` | The server answered with an error, for the image or for the program file: check the URL. |
| `Failed to fetch` | The browser's own text (Chromium's is shown; Firefox words it differently) for a program file it could not read: the program is on another site whose server does not send `Access-Control-Allow-Origin` (make it send the header, or give `{data, name}` to avoid the network), or its server cannot be reached. |
| `SHA-256 mismatch for <url>: got ..., expected ...` | The file served is not the one the digest names. Recompute the digest with `sha256sum`. |
| `the SD-card image needs a secure context (https, or http://localhost): ...` | The page is not served over `https://`, `http://localhost` or `http://127.0.0.1`. |
| `--load <file>: a browser page loads only .nex, .tap, .tzx, .jns, .sna, .szx, .z80, .wav, .rzx programs` | The program has another extension. |
| `failed to load '<file>'`, and in the console `the mounted SD card is not the one the snapshot was taken on` | A `.jns` was taken on another SD card. Mount the card it was taken on, or pass `args: ['--snapshot-mode', 'force']`. |
| `--rzx-record: not available in a browser page (record from JavaScript: ...)` | A page cannot start a recording from the command line: record with `startRzxRecording()` and `stopRzxRecording()`. |
| `the RZX recording did not start (the log says why)` | `startRzxRecording()` was refused: the machine is a ZX Spectrum Next, an RZX is playing, or `--tape-save` is on. The console line before it says which. |
| `<flag>: not available in the browser build (...)` or `<flag>: not available in a browser page (...)` | An option that needs a process, a socket or a terminal was given. The reason is in the brackets, and the list is in the man page ("Not available in the browser"). |
| `<file> is not a jnext SD-writes file; not touching it` | The stored record's header is damaged, or in a format this version does not read; `--sdcard-writes-reset` does not remove it. Clear the site's data, or delete the record from the `jnext-card-writes` database. (A record with a valid header whose saved-file entry is inconsistent is refused the same way, but `--sdcard-writes-reset` does discard it.) |
| `<file> was made for another image ...; run with --sdcard-writes-reset to discard them` | The record under this key is for another image. Mount once with `args: ['--sdcard-writes-reset']`. |
| `jnext: resetCard() needs the keepWrites option` | `resetCard()` was called on a player mounted without `keepWrites`. |
| `option 'keepWrites' must be a boolean` | `keepWrites` is not `true` or `false`. Nothing has been downloaded yet. |
| `jnext exited with code N` | The machine stopped with an error after it had started. |
| the lines the machine printed, or `jnext: the machine did not start` | The machine stopped before its first frame. The lines say why. |
