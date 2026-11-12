# 10.2 Options and the player

## Options

| option | type | what it is |
|---|---|---|
| `image` | `{url, sha256}`, required | The SD-card image, as `--sdcard` and `--sdcard-sha256`. |
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
| `destroy()` | Stop the machine and remove the player. Afterwards every method throws. |

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

## Keyboard

The player takes the keys while its canvas has focus (the click focuses it). The
host modifier for JNEXT's own shortcuts is Alt (Option on a Mac); Ctrl+W, Ctrl+T,
Ctrl+N and Ctrl+Q belong to the browser.
