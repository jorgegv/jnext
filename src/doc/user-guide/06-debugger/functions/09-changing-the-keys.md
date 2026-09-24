# Changing the keys

Every debugger key can be rebound. Open **Settings > Preferences… > Debugger
Keys** in the emulator window, click the shortcut you want to change, and press
the combination you want. Click **Reset** on a row to put that one back, or
**Reset All to Defaults** for the lot. Nothing is written until you press OK or
Apply, and Apply takes effect immediately in both windows — you do not have to
restart JNEXT.

The twelve commands, and what they are bound to out of the box:

| Command | Default | Also reachable from |
|---|---|---|
| Run / Continue | **F5** | toolbar, **Debug** menu |
| Pause / Break | **F9** | toolbar, **Debug** menu |
| Single Step | **F6** | toolbar, **Debug** menu |
| Step Over | **F7** | toolbar, **Debug** menu |
| Step Out | **F8** | toolbar, **Debug** menu |
| Step Back | **Shift+F7** | toolbar, **Debug** menu |
| Frame Back | **Shift+F6** | toolbar, **Debug** menu |
| Run to Cursor | *unbound* | **Enter** in the disassembly, right-click ▸ Run to Here |
| Run to End of Frame | *unbound* | toolbar, **Debug** menu |
| Run to End of Scan Line | *unbound* | toolbar, **Debug** menu |
| Enable / Disable Trace | **F2** | toolbar, **Debug ▸ Trace** |
| Export Trace… | **F3** | toolbar, **Debug ▸ Trace** |

Three ship with no key at all. They are not second-class — they are reachable
from the toolbar, the menu and (for Run to Cursor) the Enter key — but giving
them a default would have meant taking a key away from something else, so the
choice is left to you. If you are coming from blueMSX or Spectaculator, Run to
Cursor is the one you will want to bind.

The toolbar captions follow the bindings, so a button never advertises a key
that no longer works.

## What you can bind

A combination is up to four modifiers and one key:

```
Ctrl + Alt + Shift + Meta + KEY
```

`KEY` is `F1`–`F12`, a letter, a digit, or one of `Space`, `Tab`, `Return`,
`Backspace`, `Escape`, `Insert`, `Delete`, `Home`, `End`, `PageUp`,
`PageDown`, `Up`, `Down`, `Left` or `Right`.

Four kinds of combination are refused, and the dialog says which and why:

- **A key with no Ctrl, Alt or Meta, unless it is F1–F12.** A plain letter,
  digit or arrow bound window-wide would be taken before the panel you are
  typing into ever sees it — the memory panel edits hex with the bare number
  and letter keys, and the disassembly has an address box. `Shift+F6` is fine:
  the key is still a function key.
- **Alt + a letter.** The debugger's menu bar owns those. `Alt+F5` is fine.
- **Ctrl+C and Ctrl+A.** The disassembly panel's Copy and Select All.
- **A combination another debugger command already has.** Rebind that one
  first. This is refused rather than silently taken over, because Qt answers
  two identical shortcuts by firing them alternately, which breaks both.

Four combinations are **not** refused but do get a warning, because the
emulator window already uses them: **Ctrl+F5** (record), **Ctrl+F6** (stop
recording), **F4** (soft reset) and **F11** (fullscreen). You can bind them —
F11 for Step Into is a popular choice — and they will work in the debugger
window. The emulator window keeps its own use of them, and the message under
the table says so when you pick one.

Two things that are *not* refused, and are worth knowing:

- **Ctrl is free here**, unlike in the emulator window, where Ctrl is the
  Spectrum's SYMBOL SHIFT. The debugger is a separate window and nothing you
  type in it reaches the emulated machine.
- **F11 is free here** too, even though it is the emulator window's fullscreen
  toggle — again because the debugger is a separate window.

## The emulator window

Run, Pause, Single Step, Step Over and Step Out also work while the *emulator*
window has focus, whenever the debugger is enabled, and they follow your
bindings there as well. The other seven do not: they work in the debugger
window only. Trace in particular stays out of it because its default, **F2**,
is the emulator window's window-scale cycler.

Two consequences of that forwarding:

- Binding one of those five to a key the emulator window uses for something of
  its own — **F1** power reset, **F4** soft reset, **F10** DivMMC, **F11**
  fullscreen, **F2** scale — takes that function away while the debugger is
  enabled. This is not new: Pause has always taken **F9** away from the
  Multiface NMI button. Rebind it if you want the other one back.
- A *modified* version of a forwarded key is no longer swallowed. With the
  default bindings, **Shift+F9** in the emulator window fires the Multiface NMI
  instead of pausing, and Shift+F5/F6/F7/F8 reach the machine's own function
  keys — the same thing they do with the debugger closed. Only the exact
  combination you bound is taken.
- The reverse happens for keys the emulator window binds as a *menu* shortcut,
  such as **Ctrl+F5** and **Ctrl+F6** (start and stop video recording): there
  the menu item wins and the debugger command is not forwarded. It still works
  normally inside the debugger window.

## Editing the file by hand

Redefinitions live in `~/.jnext/jnext.conf`:

```ini
[debugger_keys]
step_over=F10
step_into=F11
step_out=Shift+F11
run_to_cursor=Ctrl+F10
```

The action names are `run`, `pause`, `step_into`, `step_over`, `step_out`,
`step_back`, `frame_back`, `run_to_cursor`, `run_to_eof`, `run_to_eosl`,
`trace_toggle` and `trace_export`. Case does not matter in a combination, and
`none` means "no key". Only redefinitions are written, so the section holds
just the lines you changed — and is absent entirely if you changed nothing.

Nothing here fails quietly. An entry JNEXT cannot parse, one that breaks a rule
above, one naming an action it does not know, or two actions asking for the
same combination: each is printed on the console when JNEXT starts and listed
in red at the top of the **Debugger Keys** tab. A bad entry leaves its action
at the default; a clash leaves the losing action with no key at all rather than
leaving you with two half-working ones. An action name JNEXT does not
recognise is kept in the file rather than deleted, so opening an older JNEXT
cannot throw away a newer one's setting.
