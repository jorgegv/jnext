# Actions

A rule's body is a list of actions, run in order. This page covers every one;
two families have their own pages: [snapshots](06-snapshots-and-span-checks.md)
(`snap`, `unsnap`, `dump_diff`) and [changing the
machine](08-changing-the-machine.md) (`set`, `out`). Input (`press`,
`release`, `joystick`) is on [Keys, joysticks and host
keys](07-keys-joysticks-and-host-keys.md).

| Action | Does |
|---|---|
| `log [indent n] "text"` | write a line to the script log |
| `stop ["reason"]` | stop the machine at this instruction — a failing verdict |
| `assert cond "message"` | if `cond` is false, log `ASSERT FAILED` and stop |
| `exit n` | end the run with status `n` (headless) |
| `dump_regs` | log every register |
| `dump_mmu` | log the eight MMU slots |
| `dump_mem addr len` | log `len` bytes from `addr`, 16 a line |
| `snap NAME`, `unsnap NAME`, `dump_diff NAME` | [snapshots](06-snapshots-and-span-checks.md) |
| `enable LABEL`, `disable LABEL` | switch a labelled rule on or off |
| `screenshot "file"` | save the screen at the next frame edge (`.png`, or `.scr`) |
| `save_snapshot "file"` | save the machine at the next frame edge |
| `compare_scr "file" "message"` | at the next frame edge, compare the screen memory with `file` |
| `press`, `release`, `joystick` | [input](07-keys-joysticks-and-host-keys.md) |
| `set`, `out` | [variables, and changing the machine](08-changing-the-machine.md) |
| `if cond then … [else …] end` | choose |

## `log`

```
log "FRAMES ${PREV} -> ${VALUE}, written by the instruction at ${PC:x4}"
log indent (depth(isr) * 2) "==> isr at ${PC:x4}"
```

One line to the script log, prefixed `[jds F:<FRAME> C:<CYCLE>]`. With
`indent n`, `n` spaces (0 to 255) go after the prefix, for nested traces.
Headless, the log goes to standard error (or `--log-file`); in the GUI, to the
Script tab.

## `stop`

```
stop
stop "write into the code area from ${PC:x4}"
```

The failing verdict. The machine stops at the boundary of the event — after the
instruction that wrote, read or accessed a port; before the instruction of an
`execute` rule — and the log says why and where:

```
[debugger] [warning] SCRIPT STOP: something wrote 00 into the top pixel row at 4000 at PC=0E59 FRAME=82 CYCLE=46325832
```

`PC` and `CYCLE` in that line are the event's: the instruction that caused it.
In the GUI the machine pauses there. Headless, the run ends with status 3.

**The rest of the rule still runs**, and so do the other rules on the same
event: a `stop` takes effect when they have all finished. That keeps the
bookkeeping right (a span check's `unsnap` after a `stop` still happens), but
it also means a `log "PASS …"` after a failed check is still printed: see
[`mutation.jds`](12-the-demo-scripts.md#mutationjds-changing-the-machine).

A rule whose body is nothing but `stop` in an `execute` rule is a
**breakpoint** other debugger clients can see: a remote debugger that runs the
machine with its own stepping loop — ZRCP's `run`, for one — stops there as it
would at its own breakpoint. If the condition matters, put it in `when` — `on
execute @f when A == 0 do stop end` — rather than in an `if` inside the body,
which only the script can evaluate.

## `assert`

```
assert mem[@trap_target] == 0 "the trapped write never happened"
```

If the condition is 0, logs `ASSERT FAILED: message` and stops, exactly like
`stop "message"`. If it is true, it does nothing.

## `exit`

```
exit 0
exit failures
```

Ends a headless run with that exit status, 0 to 255; any other value is a
run-time error (exit 1), because the shell would see `exit 256` as 0. The
machine stops at this boundary first, so nothing runs after it. In the GUI a
script never ends JNEXT: `exit` pauses the machine and the Script tab shows
**PASS: exit 0** or **FAIL: exit n**.

`exit` is fail-safe:

- **A failure at the same event wins over `exit 0`.** If a `stop`, a failed
  `assert` or `compare_scr`, or a run-time error happens at the same event — in
  this rule or another, before the `exit` or after it — the run exits 3 (1 for
  the run-time error), and the log says `SCRIPT EXIT 0 not taken`. A non-zero
  `exit n` is kept.
- **An `exit` waits for a `compare_scr` still pending** (see below), and is
  taken after it.
- **An `exit` inside an `on stop` rule never changes the status.** The pause
  that ran the rule has already decided it: after a `stop` the run exits 3,
  whatever the `on stop` rule's `exit` says (it is logged, and ignored).
- **An `exit` does not wait for a `screenshot` or a `save_snapshot`.** Both
  happen at the next frame edge; an `exit` in the same frame ends the run
  before it, and the run exits **1** rather than pretend it saved:

```
on frame 100 do
    screenshot "shot.png"
    exit 0
end
```

```
[debugger] [info] [jds F:100 C:56469512] SCRIPT EXIT 0
[debugger] [error] SCREENSHOT "shot.png" never taken: flushed before a frame was rendered for it
[debugger] [error] [jds F:100 C:56469512] SCRIPT: a screenshot was not written (no_frame)
[platform] [info] script requested exit 1
```

Exit a frame or two later instead (`on frame 102 do exit 0 end`). [Running
scripts](09-running-scripts.md#exit-codes) has every exit status.

## `dump_regs`, `dump_mmu`, `dump_mem`

```
dump_regs
dump_mmu
dump_mem 0x5C78 3
```

```
[debugger] [info] [jds F:100 C:56469512] regs AF=005C BC=0000 DE=5CB9 HL=10A8 IX=0000 IY=5C3A SP=FF48 PC=10B4 AF'=0044 BC'=174B DE'=0006 HL'=107F I=3F R=1A IFF1=1 IFF2=1 IM=1
[debugger] [info] [jds F:120 C:67093160] mmu 0:FF/00 1:FF/01 2:0A/0A 3:0B/0B 4:04/04 5:05/05 6:00/00 7:01/01
[debugger] [info] [jds F:120 C:67093160] 5C78: 25 00 00
```

`dump_regs` shows the live registers. `dump_mmu` shows each slot as
`slot:mmu/page`: `mmu[s]` (0xFF for ROM) and the physical page. `dump_mem`
logs at most 4096 bytes, 16 to a line, read as the CPU sees them.

## `enable`, `disable`

```
disabled guard: on write 0x8000..0xBFFF do stop "write into the code area" end
on hostkey 1 do enable guard end
on hostkey 2 do disable guard end
```

Switch a labelled rule of the same file on or off. `enable` also re-arms a
`once` rule that has fired.

## `screenshot`

```
screenshot "print.png"
screenshot "print.scr"
```

Saved at the **next frame edge** — the frame that has just been drawn. A name
ending in `.scr` saves the ULA screen memory (6912 bytes, the Spectrum's own
format); anything else saves a PNG of the whole display, every layer. The same
path as `--delayed-screenshot`.

## `save_snapshot`

```
save_snapshot "basic.szx"
```

Saved at the **next frame boundary**. The format follows the extension: `.jns`
(JNEXT's own, the whole machine), `.szx`, `.nex`; **any other name gets a
`.sna`** — so call it `.sna`. `.szx` and `.sna` describe classic Spectrums, and
refuse a machine they cannot represent.

```
[debugger] [info] SNAPSHOT "basic.szx" saved (49254 bytes)
```

Saving a `.sna` pushes the PC onto the guest stack, as the format requires; the
log shows it as a `MUTATE` line.

## `compare_scr`

```
compare_scr "print.scr" "PRINT 2+2 still prints 4"
```

At the next frame edge (at once in a `frame` rule, which already runs at the
edge), compares the ULA screen memory — 6912 bytes of pixels and attributes —
with the file, byte for byte. A match does nothing. A mismatch logs the first
differing offset and both bytes, then behaves as a failed `assert`.

A complete check — type `PRINT 2+2` into 48K BASIC and compare the screen with
one saved from an earlier run (`screenshot "print.scr"`):

```
on frame 150 do
    press "p" for 5
    press "2" for 5
    press "sym+k" for 5
    press "2" for 5
    press "enter" for 5
end
on frame 250 do
    compare_scr "print.scr" "PRINT 2+2 still prints 4"
    exit 0
end
```

It passes. Typed as `PRINT 2+3` instead, it fails:

```
[debugger] [warning] [jds F:250 C:140335208] compare_scr print.scr: first difference at offset 256 (file 08, screen 7E)
[debugger] [warning] [jds F:250 C:140335208] ASSERT FAILED: PRINT 2+2 still prints 4
[debugger] [info] [jds F:250 C:140335208] SCRIPT EXIT 0
[debugger] [warning] SCRIPT EXIT 0 not taken: "PRINT 2+2 still prints 4" failed at the same boundary (exit 3)
[platform] [info] script requested exit 3
```

Offset 256 is the second pixel line of the top character row, where the 4 and
the 5 differ. The `exit 0` in the same rule is not taken.

`compare_scr` compares the ULA's memory, not the picture: it is exact and
independent of the palette, the border and the other layers. For a Next program
drawing on Layer 2, sprites or the tilemap, save a PNG (`screenshot`) and
compare the files outside JNEXT.

## `if`

```
if VALUE == 0x02 then
    assert CVC == 95 "the palette split lands on line 95"
    set splits = splits + 1
end

if presses == 1 then
    enable watch_frames
else
    disable watch_frames
end
```

`then` actions run when the condition is true, `else` actions otherwise. `if`
nests up to 64 deep. There are no loops: a rule runs once per event, and the
events are the loop.
