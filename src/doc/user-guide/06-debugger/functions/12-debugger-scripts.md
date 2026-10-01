# Debugger scripts

A debugger script (`.jds`) watches the running machine and acts on what it
sees: stop when a range of memory is written, log every NextREG write, check
an invariant at the end of every frame, press a key at a given frame. The
language is described in the **SCRIPTING** section of the man page.

## Loading scripts

- **From the command line**: `--script FILE` (repeatable) and `--map FILE`
  for the symbols the scripts name as `@symbol`. A script with an error stops
  JNEXT at startup with `file:line:column: message`.
- **From the debugger window**: **Script > Load Script...** loads one more
  script while the machine runs; **Reload Scripts** loads the same files again;
  **Unload Scripts** removes them all. The [Script](../panels/14-script.md)
  tab shows the rules, the verdict and the log. A script with an error is not
  loaded, and the message box names the line and column.

A script loaded from the menu starts at once. `FRAME`, and `on frame N`, count
the machine's own frames, not frames since you loaded the script.

## Host keys: Alt+1 to Alt+8

**Alt+1** to **Alt+8** are the script host keys: Alt+*N* runs the scripts'
`on hostkey N` rules. They work in the emulator window — with the debugger
window open or closed — and in the debugger window; in a headless run
`--script-key FRAME N` presses one. Holding a key down counts as one press.

They are taken from the Spectrum: Alt+1 to Alt+8 never type their digit into
the program you are running, even with no script loaded, so what they do never
depends on what is loaded. **Alt+9** and **Alt+0** still type 9 and 0. A
debugger key binding cannot use Alt+1 to Alt+8
([Changing the keys](09-changing-the-keys.md) refuses them).

A typical use: start with the guards disabled, so booting and loading do not
trip them, and arm them with a key once the program is running.

```
disabled guard: on write 0x8000..0x9FFF do
    stop "write into the code area from ${PC:x4}"
end
on hostkey 1 do enable guard  log "guard armed" end
on hostkey 2 do disable guard log "guard disarmed" end
```

## Stops and exits in the GUI

A `stop`, a failed `assert` or an `exit` pauses the machine and the debugger
window shows where; JNEXT itself never exits because of a script while it has
a window. In `--headless` (and the SDL-only build) the same script ends the
run instead, and its exit status is the verdict — see
[Automation and CI](../../07-automation-and-ci/index.md).
