# Dot commands

A NextZXOS dot command (`.ls`, `.prog`) is not loaded by JNEXT: NextZXOS copies the file into
DivMMC RAM when you type the command and runs it from `$2000` there. JNEXT debugs one from
its map, built with `-debug`. The example is `dot.c` and `dutil.c` in
`test/fixtures/z88dk/src/` (a loop that adds 3 four times, then calls a function in the
second module):

```
zcc +zxn -subtype=dot -clib=new -debug -m --list -create-app -o prog dot.c dutil.c
```

This builds on z88dk 2.4 and on the 2026-10-16 master (sdcc, `-clib=sdcc_iy -debug`, builds
on master too). It writes `PROG` (the command, in capitals, whatever the `-o` name), `prog.map`,
`prog_CODE.bin` and the listings (`prog.lis`, `dot.c.lis`, `dutil.c.lis`). Keep `prog.map` and
`prog_CODE.bin` together: the binary is the identity check (see
[Loading a source map](05-loading-a-source-map.md)).

1. Put `PROG` on the card: `jnext --sdcard-file-add PROG --sdcard-file-dest /DOT/PROG`.
2. Start JNEXT with the map and **no** `--load`: `jnext --source-map prog.map` (or **Load
   Source Map...** in the debugger). The map attaches at once and says *Source map not
   verified yet: it is checked when the dot command runs*. The log line is (z88dk 2.4 and
   master alike):

        --source-map 'prog.map': 267 source lines (3 z88dk listings) (identity check pending: the dot command loads when it runs)

3. Set `dot.c:8` in the Breakpoints panel (or in a script), and type `.prog` at the NextZXOS
   prompt. The machine stops inside the command, and the Source tab, **Into / Over / Out**
   and the Call Stack work in `dot.c` as for any program.

JNEXT checks the map against the command when it starts (the CPU about to execute `$2000` in
DivMMC RAM bank 2): the log says `Verified z88dk source map ...: the dot command in DivMMC
RAM bank 2 matches its binary identity`. NextZXOS puts every dot command in that same bank,
so another command that runs there does not match: the map stays pending, nothing is
dropped, and the log says *The dot command started in DivMMC RAM bank 2 is not the one source
map ... describes; it stays pending*. A reset does not drop it either (NextZXOS resets the
machine while it boots). A breakpoint on the dot's lines is a breakpoint on that bank, so it
also stops a different dot command that runs code at the same address; the map then shows as
pending again.

`-subtype=dotx` works: the part in DivMMC RAM is the same, and the main part (`$8000`, loaded
from `extra/PROG.X` by the command) is checked when it first runs (the crt needs `CLEAR
32767` first). `-subtype=dotn` is not supported: its main bank is a set of pages NextZXOS
allocates when the command runs, so the map cannot place them; build it as a dotx to debug it
(`--source-map` of a dotn map says `a dotn map cannot place them (build it as a dotx to debug
it)` and the run exits with status 1; a dotx map gives 303 source lines).
Both subtypes build with `-clib=new -debug -m --list -create-app` on both z88dk installs
(`dotx` also writes `PROG.X` and `prog_MAIN.bin`; `dotn` writes `PROG` and `prog_MAIN.bin`).
Any other map without `--load` is refused (exit status 1): `--source-map requires --load FILE, or the map of a z88dk dot command: 'prog.map' is not one`. The same holds for an RZX replay
(`--rzx-play R` or `--load R.rzx`): a dot command's map attaches pending as above, any other map is
refused at start-up, saying so (exit status 1).

JNEXT tells a dot command's map by what the build wrote into it (a start-up offset of `$0100` for
a dot, `$0200` for a dotx, `$0300` for a dotn, and the code starting at `$2000`). The command's
lines in `$2000`-`$3FFF` are then tied to DivMMC RAM bank 2, the memory NextZXOS runs it from. A
map with a line in a `DIV_n` section is otherwise refused, since the program (or a dotn
command's allocator) chooses that bank when it runs.

The messages are in [Limits and troubleshooting](10-limits-and-troubleshooting.md).
