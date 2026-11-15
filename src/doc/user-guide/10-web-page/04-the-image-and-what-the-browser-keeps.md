# 10.4 NextZXOS, the SD card, and what the browser keeps

## NextZXOS

JNEXT runs NextZXOS, the ZX Spectrum Next's operating system, from an SD card. In the
browser the page needs that card too, and this is how it gets it.

- NextZXOS is SpecNext's copyrighted software. JNEXT does not ship, host or serve it,
  and neither does a page that embeds JNEXT: the only place that offers it is
  specnext.com.
- The player makes its SD card in the visitor's browser, from the official NextZXOS
  file from specnext.com (`sn-emulator-26.11.zip`, 67 MB, for this release), unchanged.
  Another file, or a changed copy, is refused.
- The card the browser makes stays in the visitor's browser (Cache Storage, for that
  site only). Nothing is sent anywhere, and the page's author never receives it.

The card is a small boot card (40 MiB) with only what NextZXOS needs to reach its menu
and to warm-start a program, not the 1 GB distribution image. The browser build is made
and tested for that card.

## The first visit, two ways

The first visit to a site, after the visitor clicks the player (the click is what
starts anything: nothing is downloaded or built before it), goes one of two ways.

1. **specnext.com allows the page to read the file.** The page downloads the official
   file (`onProgress('download', fraction)`), makes the card in the browser
   (`onProgress('build', 1)`) and keeps the card. The downloaded file itself is not kept.
2. **specnext.com does not allow it** (the case with this release: a browser lets a page
   read a file from another site only when that site says so). The player then shows what
   to do: a link to download the official file from specnext.com, and a button to choose
   the downloaded file. `onProgress('choose', 0)` reports it. When the visitor chooses
   the file, the browser makes the card from it (`onProgress('build', fraction)`) and keeps the card.
   A file that is not the official one is refused, with the reason on the screen, and the
   visitor can choose again.

Either way the visitor sees these steps, in this order: the click, `download` or `choose`
and `build`, then for a `.nex` program only `warm-start` (NextZXOS boots once and the
machine is recorded), then `onReady`. The download's progress can be reported only when
specnext.com sends `Content-Length` and does not compress the file; otherwise
`onProgress` reports only 0 and 1.

## For page authors: `card.source`

The page does not name the file or its address, and has nothing to host. It can choose
how the first visit goes with the `card` option (`data-card-source` on the widget, 10.5):

| `card.source` | the first visit |
|---|---|
| `'auto'` (default) | The page checks whether specnext.com allows it to read the official file. If so, it downloads it (way 1); if not, or if the download fails, the player asks the visitor (way 2) and shows the reason. |
| `'fetch'` | Only way 1. Use it once specnext.com allows pages to read the file; if it cannot be read, the page reports an error (`onError`) and does not ask the visitor. |
| `'pick'` | Only way 2: the page always shows the download link and the file button, and makes no request to specnext.com. |

The widget and the example pages show the same player panel. A page that shows its own
status text can use `onProgress` (10.2): `'choose'` is the moment to say "choose the
NextZXOS file".

## Later visits

A later visit to the same site needs no download and no file: the card the browser kept
is checked against its SHA-256 every time, and a card that no longer matches is deleted
and made again, as on a first visit.

For a `.nex` program the recorded machine is brought back (`restore`) instead of booting
NextZXOS again. A corrupted recording is made again. A `.tap`, a `.tzx` or a `.wav` starts directly in 48K
BASIC, and an `.rzx` on the machine it was recorded on, without NextZXOS, so there is nothing to record. A Next page with no program boots
NextZXOS on every visit and records nothing.

## What the browser keeps, and where

- The SD card is kept in the browser's Cache Storage: one entry of about 40 MiB, named
  by the card's SHA-256 (so a JNEXT release with a newer NextZXOS replaces it). The
  official file the card was made from is not kept.
- The warm-start recording is kept in IndexedDB.
- With `keepWrites`, the sectors the program writes to the card are kept in IndexedDB, in
  a database of their own (`jnext-card-writes`) that exists only for a page that uses it.
  There is one record per card and page: its name is the card's SHA-256 and a short
  digest of the page's path, so another card, or another page of the same site, has
  its own saves, and a changed card never reuses the old ones. Only the written sectors
  are stored (516 bytes each; the card itself is never written), and they stay in the
  visitor's browser: nothing is sent anywhere.
- Nothing else is kept: no settings, nor screenshots or snapshots (10.3).
- Several players on one page share the recording.
- If the browser's storage is full, the session still runs and the next visit makes the
  card again.

### Keeping the saves

```js
jnext.mount(el, { file: 'game.nex', keepWrites: true });
```

Whatever the program writes to the card is there at the next visit, and so is the one file a
NEX loaded with `file` saves through esxDOS (10.3). `player.resetCard()` clears both (10.2). A save is stored a moment after the program makes it: a reload within moments of a save can lose it (the store on leaving the page is best effort).
The saves belong to the card: a visit whose card differs, even in one byte (a JNEXT
release that moves to a newer NextZXOS makes a different card), starts with none. A record made for another card is refused with a message naming
`--sdcard-writes-reset`: mount once with `args: ['--sdcard-writes-reset']` to discard it. A
record whose header is not a valid one (damaged, or in a format this version does not
read) is refused as "not a jnext SD-writes file" and the reset does not remove it: clear the
site's data in the browser, or delete the record from the `jnext-card-writes` database
(store `journals`) with the browser's developer tools.
A `.jns` snapshot (10.2) restored over a card that has newer saves leaves NextZXOS's in-memory
picture of the card out of step with the card, as on the desktop (5.9).

Browsers keep the storage of each origin (scheme, host and port) apart. Two pages of one
origin share the card and the recording. A game inside an `iframe` of another site may
or may not see the storage its own origin has: that depends on the browser, its version
and its privacy settings (in the Chromium used to test JNEXT it did), so do not rely on
either. Clearing the site's data in the browser makes the next visit a first visit: the card is made again.

## If it does not start

The page reports these messages through `onError`, and the example pages show them in
their status line. A message about the visitor's NextZXOS file also appears on the
player's panel.

| message | what it means |
|---|---|
| `unknown option '<name>'` or `option '<name>' must be ...` | A `mount()` option is misspelt or of the wrong type. Nothing has been downloaded yet. |
| `option 'card' must be an object {source}`, `option 'card': unknown key '<key>'` or `option 'card': source must be 'auto', 'fetch' or 'pick'` | The `card` option is not what 10.2 and this page describe. |
| `this file is not sn-emulator-26.11.zip from specnext.com ...` | The visitor chose a different file: the message gives its size, or says its SHA-256 differs. The visitor has to download the official file from specnext.com (the link is on the panel) and choose that one. |
| `cannot read the NextZXOS distribution from specnext.com: ...` | `card.source` is `'fetch'` and the file could not be read: specnext.com does not allow the page to read it, the visitor is offline, or the download failed. Use `'auto'` or `'pick'`. |
| `the card built from the distribution is not the expected one ...` | The card the browser made does not have the SHA-256 this JNEXT release expects. The message gives both digests. It is a fault of the build or of the browser: report it. |
| `cannot fetch <url>: a network error, or the server does not allow cross-origin reads (no Access-Control-Allow-Origin header)` | A program file on another site: its server is unreachable, or it does not send `Access-Control-Allow-Origin`. Make it send the header. |
| `cannot fetch <url>: HTTP <status>` | The server answered with an error for the program file: check the URL. |
| `Failed to fetch` | The browser's own text (Chromium's is shown; Firefox words it differently) for a program file it could not read: the program is on another site whose server does not send `Access-Control-Allow-Origin` (make it send the header, or give `{data, name}` to avoid the network), or its server cannot be reached. |
| `the SD card needs a secure context (https, or http://localhost): ...` | The page is not served over `https://`, `http://localhost` or `http://127.0.0.1`. |
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
