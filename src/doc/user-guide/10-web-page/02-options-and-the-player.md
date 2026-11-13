# 10.2 Options and the player

## Options

| option | type | what it is |
|---|---|---|
| `image` | `{url, sha256}`, required | The SD-card image, as `--sdcard` and `--sdcard-sha256`. |
| `keepWrites` | boolean | `--sdcard-writes`, with a file the page manages: what the program writes to the card, and the file a NEX saves through esxDOS (saves, high scores), is kept in the visitor's browser and is there at the next visit. Off by default; what it keeps, and what it cannot, is in 10.3 and 10.4. |
| `file` | URL, `{url}` or `{data, name}` | The program to run, as `--load`. `.nex`, `.tap`, `.tzx`, `.wav`, an `.rzx` recording, or a `.jns`, `.sna`, `.szx` or `.z80` snapshot, which runs on the machine it was saved on (see "Saving the game" below; recordings: "Recording a game"). Without it the machine boots to the NextZXOS menu. |
| `machine` | string | `--machine`. |
| `touchKeyboard` | `'auto'`, `'on'` or `'off'` | `--touch-keyboard`: the on-screen keyboard. `'auto'` (default) shows it on a phone or tablet, `'on'` always, `'off'` never. See "Keyboard", below. |
| `muted` | boolean | `--set startup.silent`: no audio device for the session. |
| `set` | `{"section.key": value}` | One `--set` each, sorted by key. |
| `args` | array of strings | More command-line arguments, last, so they win. |
| `width`, `height` | number (CSS pixels) or CSS length | The box the player fills; the picture is letterboxed inside it. Default `100%`. |
| `wasmUrl` | string | Where the `.wasm` is served from. Default: beside the script. |
| `onProgress`, `onReady`, `onError` | functions | Events, below. |

`player.argv` is the equivalent command line, without the program name.

## Events

- `onProgress(phase, fraction)`: `phase` is `download`, `warm-start` or `restore`;
  `fraction` runs from 0 to 1.
- `onReady()`: after the mount's click, after `load()` and after `reset()`: the
  program is in the machine and a frame has been shown. A load the program itself
  asks for (a NEX that runs another) is not the page's: it fires no `onReady` and
  `reset()` still restarts the page's program. For a `.wav`, `onReady` comes when the
  tape is in the deck; the program then loads as the tape plays, in real time (a 48K game
  takes minutes), and the tape stops while the player is paused or the tab is hidden.
- `onError(message)`: on every failure (a refused option, a bad image, a program that
  does not load, an exit with an error). The promise of the call that failed rejects
  with the same message.

## The player

| member | what it does |
|---|---|
| `state` | `waiting` (before the click), `starting`, `running`, `paused`, `exited`, `destroyed`. |
| `ready` | A promise for the first ready; it rejects if the machine does not start, or if the startup program does not load. |
| `pause()`, `resume()` | Stop and restart the frame loop. |
| `reset(kind)` | `'program'` (default): restart the program; with no program, the same as `'hard'`. `'hard'`: power on again. `'soft'`: reset. Returns a promise. |
| `load(file)` | Load another program (same forms as `file`) while the machine runs. Resolves when it is in the machine. |
| `saveSnapshot()` | Resolves to a `Uint8Array` holding a `.jns` snapshot of the running machine (`args: ['--snapshot-compression', 'off']` stores it uncompressed). Rejects if the machine is not running. |
| `startRzxRecording()` | Starts an RZX recording of the machine as it is now. Rejects if the machine is not running, if one is already running, or if it cannot start (a ZX Spectrum Next, an RZX playing: the console says which). |
| `stopRzxRecording()` | Ends the recording and resolves to a `Uint8Array` holding the `.rzx` file. Rejects if none is running. |
| `setVolume(v)` | Master level, 0 (silent) to 1; anything else throws a `RangeError`. A player started `muted` has no audio device. |
| `resetCard()` | Forget the writes kept by `keepWrites` and restart the program, as `reset('program')` does. Needs `keepWrites` (it rejects without it). Resolves when the program is in the machine again. |
| `showKeyboard(on)` | Show (`true`) or hide (`false`) the on-screen keyboard, whatever `touchKeyboard` says: the page's call wins, before or after the start. Anything but a boolean throws a `TypeError`. |
| `keyboardVisible` | Whether the on-screen keyboard is shown now. |
| `destroy()` | Stop the machine and remove the player; with `keepWrites` it first waits until the writes are stored. Afterwards every method throws. |

## Letting a visitor clear the saves

A page that uses `keepWrites` can offer a button for `resetCard()`:

```html
<button onclick="player.resetCard()">Clear saves</button>
```

where `player` is what `jnext.mount()` resolved to (or `el.jnextPlayer` of a widget).
`args: ['--sdcard-writes-reset']` does the same for one start, when the page is
loaded.

## The CRT effect

The desktop window's CRT effect (5.3) runs in the page too, off by default, with the same five
looks and the same parameters. There is no extra option: use `args` with `--crt` and one of
`off`, `scanlines`, `soft`, `tv` or `pc`, or the three settings it stands for in `set`:

```js
jnext.mount(el, { image: img, args: ['--crt', 'tv'] });

jnext.mount(el, { image: img, set: { 'startup.crt_shader': true, 'startup.crt_shader_style': 1 } });
// style 0 is tv, 1 is pc, 2 is soft; 'startup.crt_filter': true is the plain scanlines
```

- The effect is chosen for the whole run: there is no call to switch it while the player runs.
- It draws on the page's WebGL, so it needs a browser with WebGL.
- *Soft scanlines* show their lines only when the picture is at least twice 640x512 device pixels
  (a larger box or a high-density screen), as on the desktop; at 640x512 they only dim the picture.
- If the shader cannot run, the player writes one line starting `CRT effect:` to the browser console
  and carries on: with the plain scanlines for *Soft scanlines* (or when `scanlines` was also asked),
  with the plain picture otherwise.

## Saving the game

`saveSnapshot()` gives the page a snapshot of the whole machine, to offer as a download:

```js
const bytes = await player.saveSnapshot();
const a = document.createElement('a');
a.href = URL.createObjectURL(new Blob([bytes]));
a.download = 'game.jns';
a.click();
```

and `load()` takes it back, for instance from an `<input type="file">`:

```js
await player.load({ data: new Uint8Array(await file.arrayBuffer()), name: file.name });
```

A snapshot runs on the machine it was saved on, whatever the player's `machine` option says,
and that machine stays for later `reset('hard')`, `reset('program')` and
`load()` of a program; a page that mixes snapshots of other machines with NEX programs gives
each its own player. A `.jns` restores only on the SD card it was taken on: the same card
image gives a snapshot that loads, another one is refused (`failed to load '<file>'`, the
reason in the browser console). `args: ['--snapshot-mode', 'force']` loads it anyway, at the
page's risk. A `.jns` that had a tape attached remembers the tape by its path; one saved in a
page names a file of the page's memory, so on another JNEXT it restores without the tape.
A snapshot never uses the stored NextZXOS recording (10.4): it starts at once.

## Recording a game

`startRzxRecording()` records the machine from the moment it is called, input included, and
`stopRzxRecording()` gives the page the `.rzx` file, to offer as a download:

```js
await player.startRzxRecording();
// ... the visitor plays ...
const bytes = await player.stopRzxRecording();
const a = document.createElement('a');
a.href = URL.createObjectURL(new Blob([bytes]));
a.download = 'game.rzx';
a.click();
```

A recording is refused on a ZX Spectrum Next, which is the default machine: give the player
`machine: '48k'`, `'128k'`, `'plus3'` or `'pentagon'`. While recording, a tape loads in real
time. A `reset()` or a `load()` ends the recording early; `stopRzxRecording()` still returns it.
A recording plays on the machine it was made on: given as `file`, an explicit `machine` wins at
the start, and later `load()` and `reset()` use the recording's. While it plays, every port read
comes from the file, so the visitor's keys do nothing until the console says `RZX: playback
complete`; then the machine runs on, live. `load()` takes a recording back like any program.

## Keyboard

The player takes the keys while its canvas has focus (the click focuses it). The
host modifier for JNEXT's own shortcuts is Alt (Option on a Mac); Ctrl+W, Ctrl+T,
Ctrl+N and Ctrl+Q belong to the browser.

### Phones and tablets

On a device whose main pointer is a finger (`touchKeyboard: 'auto'`, the default) the player
shows an on-screen Spectrum Next keyboard at the bottom of its box, and the screen above it
shrinks by the keyboard's height; `'on'` shows it everywhere, `'off'` nowhere, and
`player.showKeyboard(true)` or `false` decides while the page runs. Its keys type exactly what
the keys of a PC keyboard do, and the screen keeps the focus while you type.

- A key goes down when a finger lands on it and up when the finger lifts, so a held key is held;
  two fingers hold two keys, for chords and for games.
- Below 504 CSS pixels of width it shows the 40 keys of a 48K keyboard, in four rows; every
  other key of the Next is Caps Shift or Symbol Shift plus a key, as on a 48K. From 504 pixels
  on (a tablet, or a phone turned sideways) it shows the Next's 58 keys, in five rows, with
  the extra keys: Delete, Edit, Break, Graph, Extend, True and Inverse video, Caps Lock, the
  cursor keys, and the semicolon, quote, period and comma.
- Caps Shift and Symbol Shift latch. A quick tap (under half a second, with no other key
  pressed meanwhile) leaves the shift waiting, drawn highlighted, and the next key is typed with
  it; tapping the waiting shift cancels it. A shift held longer, or held while another key goes
  down, is an ordinary hold (Symbol Shift as a fire button works).
