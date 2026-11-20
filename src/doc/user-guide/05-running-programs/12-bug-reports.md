# 5.12 Bug reports

When something goes wrong, the most useful thing you can send is **a bug report**:
one file with the state of the machine and of JNEXT at that moment. It is for a
developer to read, not a recording to play back: nobody can run your session from it.
Attach it to the issue you open at <https://github.com/jorgegv/jnext/issues>.

## Saving one

Start JNEXT with **`--bug-report PATH`** and quit when the problem has appeared. The
report is written as JNEXT exits (also when the run ends by itself, for example with
`--delayed-automatic-exit`):

```
jnext --bug-report ~/reports/ --load game.nex
```

If *PATH* is a folder (it exists, or the name ends in a slash) the file is named
`game.202611201530.jnext-bug.zip` inside it: the program you loaded, the date and the
time, then `.jnext-bug.zip`. With no program loaded the name starts `jnext.`. Any other
*PATH* is used as the file name exactly. If the file cannot be written JNEXT says so and
exits with status 1.

## What is in it

- The JNEXT version, your operating system and the settings in use (the command line,
  settings file is not included yet), with the folders of your computer
  left out.
- The machine, decoded: CPU registers, every NextREG with its name, the memory paging,
  the ULA, Layer 2, sprites, tilemap and Copper, the palettes, the interrupt, DivMMC
  and Multiface flags, and the sound chips.
- The screenshot, and each of the seven video layers on its own.
- The last 500 lines of the log and the debugger's state: breakpoints, the call stack
  and the recent instruction trace.
- The name, size and SHA-256 of your SD card. Never its contents.
- The program you loaded, the file as it is on disk.

## When the memory is included

The machine's memory is in the report **only when no firmware has run**: a
`--no-sdcard` session that has not run the boot ROM or loaded a saved state since
its memory was cleared. All the memory is then your program's, and the report holds it
(RAM, the 64 KB the CPU sees, the sprite patterns, a disassembly and the bytes at the
stack pointer).

In every other session (anything with an SD card, which means NextZXOS, ROMs and
DivMMC firmware sit in the memory) the report holds the machine's state and
pictures but **none of its memory**, and says why. JNEXT never puts NextZXOS, ROM or any
other part of the distribution in a report; the program file you loaded is yours (see
below).

## Privacy, and your responsibility

Your user name and every absolute path on your computer are replaced in the file
(`<user>`, `<path>/name`); the serial-port buffers are left out. Look at what you
attach anyway. The program file is yours and goes in as it is: JNEXT does not
check it against anything, and you are responsible for what you attach.
A file that you edited after loading the program is not included; the report records
its SHA-256 instead. A `.jns` snapshot holds the machine's whole memory, so it is never
included either: only its SHA-256 and size.
