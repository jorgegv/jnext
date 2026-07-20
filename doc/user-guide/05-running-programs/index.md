# 5. Running programs

The short version: give JNEXT a file.

```
jnext game.nex
jnext game.tap
jnext game.sna
```

The format is recognised from the extension — `.nex`, `.sna`, `.szx`, `.z80`,
`.tap`, `.tzx`, `.wav` and `.rzx` are all understood. A bare filename is
exactly the same as `--load FILE`; you cannot use both at once.

In the window, **File > Load NEX File…** (Ctrl+O) opens the same loader and
accepts every one of those formats despite its name. Tapes have their own
entry, **Tape > Open Tape File…** (Ctrl+T), covered in
[5.5](05-recording-and-playback.md).

![JNEXT running, with NextZXOS booted](../img/gui-main-window.png)

The status bar along the bottom tracks the session: frame rate, the emulated
CPU clock, the emulator speed, tape state, and the current machine.

Everything else in this chapter is about shaping that session.

---
