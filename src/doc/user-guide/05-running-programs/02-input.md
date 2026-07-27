# 5.2 Input

## Keyboard

Your PC keyboard is mapped onto the Spectrum's. Letters, digits, Enter and
Space are where you expect; the rest of the Spectrum's oddities are reachable
through modifiers:

| PC key | Spectrum key |
|---|---|
| Shift (either) | Caps Shift |
| Ctrl (either) | Symbol Shift |
| Backspace | Delete |
| Arrow keys | Cursor keys |
| Esc | **Break** |
| Tab | Extend Mode |
| Caps Lock | Caps Lock |
| Key left of `1` | True Video |
| `\` | Inverse Video |
| Alt + key left of `1` | Inverse Video |
| Alt + E | Edit |
| Alt + G | Graph |
| Alt + C | Caps Lock |
| `'` `;` `.` `,` | `"` `;` `.` `,` |
| `/` `-` `=` | `/` `-` `=` (Symbol Shift + V / J / L) |
| Shift + digit | Caps Shift + that digit |
| Shift + symbol key | Caps Shift + that symbol key |

Those last two rows are how you reach the Caps Shift layer without hunting for
it: **Shift+1** is Edit, **Shift+2** Caps Lock, **Shift+3** True Video,
**Shift+4** Inverse Video, **Shift+5** to **Shift+8** the four cursor keys,
**Shift+9** Graph and **Shift+0** Delete. JNEXT keys off the *physical* key you
pressed, not the character your host layout produces for it — the same way the
Next's own PS/2 keymap treats the shift keys as a separate overlay. Earlier
versions looked at the character instead, so all nineteen shifted digits and
symbols were thrown away before the Spectrum saw them.

Three things catch people out:

- **Shift is Caps Shift and Ctrl is Symbol Shift** — the same way round as a
  real Spectrum Next with a PC keyboard plugged into it. Earlier versions had
  these two swapped.
- **Esc is Break**, not "get me out of fullscreen". Fullscreen is F11 and only
  F11.
- **Alt is a host modifier, never a Spectrum key.** Only the four Alt
  combinations above are used, because the menu bar and the menu shortcuts
  claim the rest.

**Ctrl belongs to the program you are running.** No Ctrl+letter chord of any
kind is a JNEXT shortcut, so the Symbol Shift sequences NextBASIC leans on —
Ctrl+O `;`, Ctrl+D `STEP`, Ctrl+R `<`, Ctrl+T `>`, Ctrl+Q `<=`, Ctrl+S `|` —
all reach the guest, and so does Ctrl+Shift+S (Caps Shift + Symbol Shift + S).
Earlier versions bound those to menu commands, which meant the program never
saw them (and Ctrl+Q quit JNEXT outright). They live on **Alt** now:
Save Snapshot, the last one to move, is **Alt+Shift+S**. The only Ctrl chords
JNEXT still takes are Ctrl+F5 and Ctrl+F6 (start and stop video recording),
and function keys have no Spectrum meaning to lose.

On real Next hardware Left Alt is Extend Mode and Right Alt is Graph. JNEXT
does not do that: Alt is reserved for the host, and Extend Mode is on Tab —
the same convention FUSE and ZEsarUX use. That is a deliberate trade, made so
that Ctrl could be given back to the guest.

The machine's own front-panel keys are on function keys: **F1** hard reset,
**F4** soft reset, **F9** Multiface NMI, **F10** DivMMC. (With the debugger
open, F9 belongs to the debugger.) The two resets are also in the **Machine**
menu and on the toolbar: **Power Reset** (Alt+R, same as F1) is a power
off/on cold boot that re-runs the whole boot chain; **Soft Reset** (same as
F4) is the front-panel reset button — it returns to NextZXOS without
re-running the boot chain. Soft Reset does nothing while the firmware is
still booting, or after a direct `--load` (the firmware never ran there, so
there is no NextZXOS to return to — use Power Reset).

## Joysticks and gamepads

Up to two USB gamepads are picked up automatically, hot-plug included, and
wired to the Next's two joystick connectors. The joystick *protocol* —
Kempston, Sinclair, Cursor or MD — is chosen by the software you are running,
just as on real hardware, so a game that expects Kempston gets Kempston
without you configuring anything.

With no gamepad, point a connector at the host cursor keys instead: arrows to
move, Space to fire. Choose it in the **Input** menu, in **Settings >
Preferences > Input**, or with `--joy1-source keys` / `--joy2-source keys`;
the choice is remembered.

Only one connector can use the cursor keys at a time, and while it does, the
arrows and Space stop working as ZX keys.

## Mouse

For software that uses a Kempston mouse, JNEXT has to confine your pointer —
otherwise it hits the edge of your screen and the emulated pointer stops.

- **Capture:** click the emulator screen, or use **Input > Capture Mouse**.
- **Release:** **Ctrl+Alt**, the same combination VirtualBox and QEMU use.

Capture is off until you ask for it, so the pointer stays yours and you can
always reach the menus. While captured, the status bar reminds you how to get
out. The click that captures is not passed through to the program.

---
