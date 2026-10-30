# Source-level debugging

When your program comes with a **source map**, the debugger can show the line
of BASIC or assembly the CPU is running and step through the program one
source statement at a time — including over and out of calls, and backwards
with rewind.

JNEXT reads source maps in two formats: the **sjasmplus SLD** (`.sld`), which
sjasmplus writes with `--sld`, and which a Boriel ZX Basic / NextBuild build
can produce too; and the **z88dk `-debug` map** (`.map`), which z88dk writes
for a C program built with `-debug -m` (see
[Debugging z88dk C programs](#debugging-z88dk-c-programs) below). An SLD also
lists the program's **labels**, and they join the symbols. Symbols come from
them, from the **NextBuild `Memory.txt`** the NextBuild toolchain writes beside
a program, from a z88dk map, and from the MAP files in
[Symbols from MAP files](05-symbols-from-z88dk-map-files.md).

## Loading them

**Automatically.** When a program is loaded — on the command line with
`--load`, with **File ▸ Load NEX File…**, or by a remote debugger — JNEXT looks beside it
for:

- `<name>.Memory.txt`, else `Memory.txt`, beside a `.nex`: the symbols;
- `<name>.sld`, else `<name>.sld.txt`: the source map and its labels;
- `<name>.map`, a z88dk map, beside a `.nex`, `.tap` or `.sna`: its source lines
  and its symbols. Beside a tape (`.tap`, `.tzx`, `.wav`) they attach at once,
  and are checked when the program first runs (see **Tapes** below).

Each one found is loaded and named in the log (`Loaded 2 NextBuild symbols
from …`, `Loaded 12 SLD labels from …`, `Loaded 812 SLD source traces from …`,
`Loaded 19 z88dk symbols from …`, `Loaded 46 z88dk source lines from …`).
When both a `Memory.txt` and an SLD have a label, the `Memory.txt` one wins. A
z88dk map's source lines are used only when there is no SLD; its symbols come
last, after an SLD's labels, and are not used at all when a `Memory.txt` exists
(a `.map` with no symbols and no source lines is not a z88dk map, and is
ignored; a plain `-m` map gives its symbols, `-debug` adds the source lines). If the map is refused, say for a malformed line, its symbols still
attach and the log and the status bar say why the source lines did not. This happens in every
frontend, with or without the debugger window open, but only the Qt debugger
shows the source map: with `--headless`, and in the SDL-only build, it is
attached and logged and nothing reads it yet (a remote debugger can still look
up the symbols).

They belong to the program that was loaded with them. Loading the next program
replaces them, or clears them if it has none. Rewinding brings back the ones
that belonged to the point you land on (the last four programs are remembered).
When the machine is reset, or a state is restored from a snapshot, a bookmark or
a remote debugger, or a replay starts, the source map stays only if it carries
the program's SHA-256 (an SLD extension that most assemblers do not write) and
the memory still matches it; a reset is checked again at the next pause, since
the ROM clears memory a few frames later. Otherwise the program's symbols and
source map are dropped. **Back** and **Reverse Continue** never go back past
such a landing. A program the guest loads for
itself, such as one chosen in the NextZXOS browser or started with `LOAD ""`,
is not seen by JNEXT: load its source map by hand (below). With
`--esxdos-stub`, a `.RUN` of a sibling NEX is a load like any other: the
machine cold-boots with it, which drops the old ones, and the new program's
are attached. An SLD or a z88dk map built for another binary brings no labels or symbols either, and an SLD you
did not load because you had loaded a source map yourself brings no labels. A z88dk map beside the
program still brings its symbols then (the log says its source lines were not loaded).

Symbols you loaded by hand (below), or a MAP given with `--map`, are kept: they
are your choice, not the program's. A source map you loaded by hand is yours too
(a program's map never replaces it), but at each of those landings it is checked
the same way: if it has no SHA-256, or the memory no longer matches it, it goes
**stale**. The Source tab then says so and why, and the steps are off; load it
again (**Load Source Map...**) to use it.

**By hand.** The **Source** tab has **Load Source Map...** and **Load Memory.txt...**
buttons, also in **Debug ▸ Source ▸ Load Source Map…** and **Load Symbols
(Memory.txt)…**. The symbols replace whatever was loaded before, as the
**Map ▸ Load MAP File** formats do. **Load Source Map...** takes an SLD or a
z88dk `.map` (told apart by what the file holds). An SLD's labels are loaded too and
replace the symbols the same way, unless you loaded symbols yourself (**Map ▸ Load MAP
File**, **Load Memory.txt...** or `--map`), which are kept; a later SLD replaces an earlier SLD's labels.
A z88dk map loaded this way gives source lines only (the labels of an SLD loaded before it stay): load its symbols with
**Map ▸ Load MAP File ▸ Z88DK Format…**, or `--map`. One built without `-debug`
has no source lines and is refused, saying so.

**On the command line.** `--source-map FILE` does the same load of a source map
(an SLD or a z88dk map) right after the program given with `--load` is in memory,
in every frontend. It needs `--load`; a map whose binary identity does not match
the program is refused, and the run exits with status 1 (the log says why). The
map is yours, like one loaded by hand. With a tape program the map attaches at
once and is checked when the program first runs its code (see **Tapes** above); a
mismatch drops it then, and the run exits with status 1.

**Tapes.** A program loaded from tape (`.tap`, `.tzx`, `.wav`) is not in memory
when the load call returns: the machine loads it over the frames that follow. So
a tape's source map, whether it is `<name>.sld`, `<name>.map` or the map of
`--source-map`, attaches **at once** and works at once (the Source tab shows its
files, `main.c:19` can be set as a breakpoint before the tape has loaded), but
its binary identity is checked later, **the first time the CPU is about to run
inside the memory the map describes**. Until then the Source tab says *Source map
not verified yet: it is checked when the program first runs its code* and the log
line ends `(identity check pending: the program loads from tape)`. The check is
made **per memory space**: a program with code in the main page and in a bank
that the tape loads after it has started has each checked when it first runs
there, and the others stay pending. When the last one matches the log says
`Verified z88dk source map …`. If any space does not match, the map is
**dropped** with an error (`Rejected z88dk source map …: the program loaded from
tape does not match its binary identity`), and so are the symbols that came with
it; with `--source-map` the run then exits with status 1. A map with no binary
identity (no section binaries, an SLD without a hash) attaches unverified, as for
any program, and nothing is waited for. A reset before the program has arrived
drops a program's pending map, since the program will not, and makes the one you
loaded yourself stale (load it again once the program is in memory).

**Binary identity.** An SLD may record the SHA-256 of the program's bytes as it
was built (`||program.sha256:`, `||program.org:` and `||program.size:` comment
records, which other SLD readers ignore). An SLD found beside a program whose
bytes in memory do not match is **rejected** — it belongs to another build.
One loaded by hand that does not match asks first, because a running
program may have changed variables stored inside its own image since it started.

A z88dk map has no hash in the map itself. Its identity is the **section
binaries** next to it: z88dk's `-create-app` writes `<name>_CODE.bin` (and one
`<name>_<SECTION>.bin` for every banked section) beside the map, and JNEXT
compares the bytes of every line range of non-data code with the program in
memory, in the page the line was built for, whether or not that page is mapped.
Variables (data, bss) are not compared, since the program writes to them. A map
with no binaries beside it attaches unverified, and the log says so (`program
identity not verified`); when only some of the binaries are there, the log says
the identity is verified for the code they cover and how many code ranges had no
binary and were not checked. Only upper-case section names are looked for
(`_CODE`, `_PAGE_20`, `_BANK_10`).

A sidecar that is found and refused, for any reason (an SLD or a z88dk map built for another
binary, a file that is not an SLD or a map, not a regular file, or over 64 MiB), says
why: the reason is in the log, in the main window's status bar for ten
seconds, and in the Source tab in place of "No source map loaded".

## Banked code

A Next program pages code in and out, so one address can hold different code
at different times. Each source record names the 8K RAM page its code was built
for, and the debugger matches the RAM page that supplies that address *now*:
the Source tab, the Call Stack and the Breakpoints panel never show a line of
code that is not the one in memory. Code supplied by ROM or by an overlay
(DivMMC — including the instruction at which its automatic paging switches on
or off —, the Multiface, the Layer 2 read mapping, the boot ROM) is not any RAM
page, so page-tagged records never describe it; the Source tab says "ROM or
overlay" there. Labels follow the page too: a label assembled for a bank names
its address only while that bank is mapped there, in the disassembly, the
Breakpoints and Call Stack panels, and a remote debugger's `monitor sym`; a label
assembled for no bank names it always.

A statement covers every byte up to the next one: the data of a
`DB` after an instruction belongs to that instruction's statement, and so does everything after the last
statement of a page up to the end of that 8K page. Only addresses below a
page's first statement, and pages with no statement at all, are unmapped.

Code your program copies somewhere else at run time (a routine moved out of a
bank into low memory, say) has no record where it runs unless your build made
one, and shows as unmapped there.

## The Source tab

See [Source](../panels/15-source.md). Its buttons step by statement:

- **Into** — run to the next source statement, entering calls. Instructions
  with no source statement (compiler runtime, library code) are run through.
- **Over** — the same, but a call is run to its return.
- **Out** — run until a statement in the routine that called this one.
- **Back** — rewind to the start of the previous source statement that ran
  before this point (after a **Frame Back**, the history you rewound away from does not
  count).
- **Reverse** — rewind to the most recent source statement with an Execute
  breakpoint on it.

The same five are in **Debug ▸ Source**. Into, Over and Out are on **Ctrl+F6**,
**Ctrl+F7** and **Ctrl+F8** (like F6, F7 and F8 for instructions); Back and
Reverse ship unbound. All of them can be rebound ([Changing the keys](09-changing-the-keys.md)), and they
work in the debugger window only.

While a step runs, breakpoints, watchpoints, the magic breakpoint and debugger
script rules act exactly as they do when the program runs: a breakpoint stops
the step, a script's `log` rule logs and lets it carry on. A step that never
reaches another statement — a loop on one line, a `HALT` with interrupts off —
stops by itself (at a disabled-interrupt `HALT`, or after a million instructions
or 100 frames) and says so in the log.

**Over** and **Out** count calls with the call-stack tracker, which runs while
the debugger window is open. **Back** and **Reverse** need the trace log and
rewind, like **Step Back**
([Backward execution (rewind)](07-backward-execution-rewind.md)); they look for
their target in the instructions the trace has kept.

## Breakpoints on a source line

In the Breakpoints panel's **Add** dialog, the address can be a number
(`8000`, `$8000`, `0x8000`), a symbol (`PlayerTick`), or, for an Execute
breakpoint, a source line, `file:line`: `main.bas:120`, or the end of the
path (`game/main.bas:120`) when two files share a name. A line the program
compiled to several places (a routine in two banks, say) is ONE breakpoint,
armed at each place and listed as one row, "main.bas:120 (2 locations)": Enabled,
Edit and Remove act on all its places, and a click on the dot of one place in the
disassembly gutter removes only that place. A line with no code is refused with "no code at main.bas:120". A number wins over a symbol: `FE` or `B` is that
address even when the program has a variable called `FE` or `B`. Write `@FE`
to mean the symbol. A breakpoint on a line of banked code is tied to
the memory that supplies it (shown as `$C000 @2E` for a RAM page): it fires only
while that memory supplies the address, and the disassembly gutter shows it only then.

**Breakpoints ▸ Add Execute Breakpoint…** takes `file:line` too. Symbols work
in the other address fields: the other **Breakpoints ▸ Add …** dialogs,
**Watches ▸ Add** (a watch added by name is labelled with it) and the Go to
Address box of the Disassembly and Memory panels.

## Debugging z88dk C programs

Build with z88dk's `-debug` and `-m` and let it write the application files
(`-create-app`), so the map and the section binaries land next to the program:

```
zcc +zxn -clib=new -debug -m -create-app -subtype=nex -o prog main.c util.c
```

For a 48K Spectrum tape build for `+zx` instead (`zcc +zx -clib=new -debug -m
-create-app -o prog main.c util.c` writes `prog.tap`, `prog.map` and
`prog_CODE.bin`) and load `prog.tap`: the map attaches while the tape loads and is
verified when the program starts.

Load `prog.nex`. The `prog.map` beside it attaches by itself (see above): the
Source tab shows `main.c` with the current line, **Into / Over / Out** step by C
statement, and `main.c:19` works in the Breakpoints panel. Each line of the
map becomes a range that runs to the next line, the next function of another
module, the end of its section, 1 KB or the end of its 8K page, whichever comes
first. Two lines that start at the same address are one range owned by the
later line, and the earlier one is its alias if both are in the same function
(the compiler put them there); otherwise the earlier line has no code.

What z88dk and the library can do changes the result:

| compiler / library | z88dk 2.4 | z88dk master (from 2026-04) |
|---|---|---|
| sccz80 newlib | `zcc +zxn -clib=new -debug -m -create-app -subtype=nex`: works, but part of a statement can show on the next line | works |
| sdcc newlib | `-debug` fails to build: no source lines from the map | `-clib=sdcc_iy -debug -m -create-app`: works, header code may show the wrong line |
| 80cc | not available | `-compiler=80cc -clib=new -debug`: works (some lines are shared, e.g. 19 and 20 in the fixture) |
| classic | `-debug` fails | fails |

- **`-debug` changes the program** (it adds `l_debug_push_frame` and
  `l_debug_pop_frame` around functions): debug with the build that has it.
- **Optimisation moves code across lines**: with `-SO3` or `-O2` the stepping
  order follows the machine code, not the source.
- **Banked code**: a function in a `PAGE_n` or `BANK_n` section is found only
  while its 8K page is mapped where it runs (the Source tab, Call Stack and
  Breakpoints panels all follow the page). The map's address for a `BANK_n`
  section carries the bank in its high bits; JNEXT reads the page from the
  section and the address. z88dk's `__banked` on newlib `+zxn` needs
  `-pragma-define:CRT_ORG_PAGE_<n>=0x<page><org>` (the page in the top byte,
  for example `0x14C000` for `PAGE_20`) and a stack below the banking segment
  (`-pragma-define:REGISTER_SP=0xC000`): without them the program itself calls
  page 0 or loses its stack. **Over** and **Out** step across `banked_call` and
  the `-debug` helpers; **Out** lands on the calling line at the return address
  (mid-statement), as gdb's `finish` does.
- **DivMMC sections** (`DIV_n`) are not supported yet: a map with a line in one
  is refused, with the line named, rather than attached half.
- Code in a section that names no page (`CODE`, `code_compiler`, the program
  itself) is tied to the RAM page the MMU maps at that address when the program
  is in memory (page 4 at $8000 for a NEX).
