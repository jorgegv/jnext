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

Three things catch people out:

- **Shift is Caps Shift and Ctrl is Symbol Shift** — the same way round as a
  real Spectrum Next with a PC keyboard plugged into it. Versions before
  0.99.43 had these two swapped.
- **Esc is Break**, not "get me out of fullscreen". Fullscreen is F11 and only
  F11.
- **Alt is a host modifier, never a Spectrum key.** Only the four Alt
  combinations above are used, because the menu bar claims the rest.

The machine's own front-panel keys are on function keys: **F1** hard reset,
**F4** soft reset, **F9** Multiface NMI, **F10** DivMMC. (With the debugger
open, F9 belongs to the debugger.) The two resets are also in the **Machine**
menu and on the toolbar: **Power Reset** (Ctrl+R, same as F1) is a power
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
