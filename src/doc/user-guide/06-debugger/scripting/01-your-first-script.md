# Your first script

This tour uses nothing but a 48K Spectrum booting to BASIC, so you can follow
it with any JNEXT. Each step is a complete script: save it as a `.jds` file and
run it.

## 1. Look at the machine

```
# first.jds — look at a 48K Spectrum at frame 100, then end the run
on frame 100 do
    log "frame ${FRAME}: border ${mem[0x5C48] >> 3 & 7}, interrupt mode ${IM}, FRAMES = ${mem16[0x5C78]}"
    dump_regs
    exit 0
end
```

One rule: **on** an event (`frame 100`, the end of the hundredth frame since
power-on) **do** some actions, **end**. `log` writes a line, and `${…}`
inside a string is an expression whose value is printed there: `mem[0x5C48]`
is the byte at 0x5C48 (the ROM's border colour variable, BORDCR), `IM` the
interrupt mode, `mem16[0x5C78]` the 16-bit FRAMES counter. `dump_regs` logs
every register, and `exit 0` ends the run with status 0.

Run it headless — no window, as fast as the machine can go:

```console
$ jnext --headless --machine 48k --script first.jds
```

```
[debugger] [info] SCRIPT loaded first.jds: 1 rules
[debugger] [info] [jds F:100 C:56469512] frame 100: border 7, interrupt mode 1, FRAMES = 18
[debugger] [info] [jds F:100 C:56469512] regs AF=005C BC=0000 DE=5CB9 HL=10A8 IX=0000 IY=5C3A SP=FF48 PC=10B4 AF'=0044 BC'=174B DE'=0006 HL'=107F I=3F R=1A IFF1=1 IFF2=1 IM=1
[debugger] [info] [jds F:100 C:56469512] SCRIPT EXIT 0
[platform] [info] script requested exit 0
```

```console
$ echo $?
0
```

Every line a script logs starts `[jds F:<frame> C:<cycle>]`: the frame number,
and the 28 MHz master-clock cycle the event happened at. FRAMES reads 18, not
100: the ROM only starts counting once it has finished its memory test and
enabled interrupts.

## 2. Watch an event

```
# second.jds — who writes the FRAMES system variable?
on write 0x5C78 when FRAME >= 100 and FRAME < 103 do
    log "FRAMES ${PREV} -> ${VALUE}, written by the instruction at ${PC:x4}"
end
on frame 103 do exit 0 end
```

`on write 0x5C78` fires on every write to that byte. `when` narrows it: the
debugger checks the condition itself, before it runs the body, so a rule costs
nothing on the writes it does not want. Inside the rule, `VALUE` is the byte
written, `PREV` the byte it replaced and `PC` the address of the instruction
that wrote it — these are the event's **payload**. `:x4` prints a value as
four hex digits.

```console
$ jnext --headless --machine 48k --script second.jds
```

```
[debugger] [info] SCRIPT loaded second.jds: 2 rules
[debugger] [info] [jds F:100 C:55911432] FRAMES 17 -> 18, written by the instruction at 003E
[debugger] [info] [jds F:101 C:56470520] FRAMES 18 -> 19, written by the instruction at 003E
[debugger] [info] [jds F:102 C:57029664] FRAMES 19 -> 20, written by the instruction at 003E
[debugger] [info] [jds F:103 C:58146920] SCRIPT EXIT 0
[platform] [info] script requested exit 0
```

0x003E is the `LD (FRAMES),HL` of the ROM's interrupt handler, once a frame.

## 3. Make a check that can fail

```
# third.jds — a check: stop if anything writes the top pixel row after frame 50
on write 0x4000..0x401F when FRAME > 50 do
    stop "something wrote ${VALUE:x2} into the top pixel row at ${ADDR:x4}"
end
on frame 300 do
    log "nothing touched the top pixel row"
    exit 0
end
```

`0x4000..0x401F` is an inclusive address range. `stop` is the failing verdict:
it stops the machine at the instruction that did it.

```console
$ jnext --headless --machine 48k --script third.jds
```

```
[debugger] [info] SCRIPT loaded third.jds: 2 rules
[debugger] [warning] SCRIPT STOP: something wrote 00 into the top pixel row at 4000 at PC=0E59 FRAME=82 CYCLE=46325832
[platform] [info] script requested exit 3
```

```console
$ echo $?
3
```

It caught something: at frame 82 the ROM clears the screen, and 0x0E59 is the
instruction in its line-clearing routine that wrote the byte. The `SCRIPT
STOP` line names the instruction (`PC`), the frame and the cycle. Headless, a
stop ends the run with status **3**; in the debugger window it pauses the
machine there instead.

Start the check after the clear (`when FRAME > 100`) and the run passes:

```
[debugger] [info] SCRIPT loaded third100.jds: 2 rules
[debugger] [info] [jds F:300 C:168290432] nothing touched the top pixel row
[debugger] [info] [jds F:300 C:168290432] SCRIPT EXIT 0
[platform] [info] script requested exit 0
```

That is a test: a script whose exit status says whether the program did what
it should. [Running scripts](09-running-scripts.md) has the exit codes in full.

## 4. Run it in the debugger window

The same script runs in the GUI. Start JNEXT (`jnext --machine 48k`), open the
debugger (**Alt+D**), and choose **Script ▸ Load Script...**. The **Script**
tab lists the rules, their hit counts and the log. When `third.jds` stops, the
machine pauses, and the disassembly shows the instruction that wrote the byte;
**Run** (F5) carries on. A script never ends JNEXT while it has a window: an
`exit` pauses the machine too, and the Script tab shows **PASS: exit 0**.

You can also give the script on the command line, `jnext --machine 48k
--script third.jds`: it is loaded before the machine starts and listed in the
Script tab the same way.

## Where to go next

- The building blocks, one page each: [the language](02-the-language.md),
  [events](03-events.md), [reading the machine](04-reading-the-machine.md),
  [actions](05-actions.md).
- Real bugs: [catching memory-corruption bugs](11-catching-memory-corruption-bugs.md)
  and [the demo scripts](12-the-demo-scripts.md).
- Tests from play: [recording and replaying a session](10-recording-and-replaying.md).
