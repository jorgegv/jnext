# 10.2 Options and the player

## Options

| option | type | what it is |
|---|---|---|
| `image` | `{url, sha256}`, required | The SD-card image, as `--sdcard` and `--sdcard-sha256`. |
| `keepWrites` | boolean | `--sdcard-writes`, with a file the page manages: what the program writes to the card (saves, high scores) is kept in the visitor's browser and is there at the next visit. Off by default (10.4). |
| `file` | URL, `{url}` or `{data, name}` | The program to run, as `--load`. `.nex`, `.tap` and `.tzx` only. Without it the machine boots to the NextZXOS menu. |
| `machine` | string | `--machine`. |
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
  `reset()` still restarts the page's program.
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
| `setVolume(v)` | Master level, 0 (silent) to 1; anything else throws a `RangeError`. A player started `muted` has no audio device. |
| `resetCard()` | Forget the writes kept by `keepWrites` and restart the program, as `reset('program')` does. Needs `keepWrites` (it rejects without it). Resolves when the program is in the machine again. |
| `destroy()` | Stop the machine and remove the player; with `keepWrites` it first waits until the writes are stored. Afterwards every method throws. |

## Letting a visitor clear the saves

A page that uses `keepWrites` can offer a button for `resetCard()`:

```html
<button onclick="player.resetCard()">Clear saves</button>
```

where `player` is what `jnext.mount()` resolved to (or `el.jnextPlayer` of a widget).
`args: ['--sdcard-writes-reset']` does the same for one start, when the page is
loaded.

## Keyboard

The player takes the keys while its canvas has focus (the click focuses it). The
host modifier for JNEXT's own shortcuts is Alt (Option on a Mac); Ctrl+W, Ctrl+T,
Ctrl+N and Ctrl+Q belong to the browser.
