# Scripts and remote debuggers

The Source tab is the Qt debugger's. With `--headless`, in the SDL-only build, in a
script and over a remote debugger the same source map is attached (see [Loading a
source map](05-loading-a-source-map.md)) and read, but there is no tab to show it.
This page is what each of them can do with it.

## Scripts: `on execute main.c:19`

A script breaks on a source line with `on execute main.c:19` (see
[Events](../scripting/03-events.md)): it fires at every place the line's code
starts, each in the memory it was built for. With `--load` and `--script`
together the script waits for the program and its map. A line that is not in the map, or that
has no code, is a load error that names it (see [Running scripts](../scripting/09-running-scripts.md)).

With the program of [A first session](01-a-first-session.md) and a script file `line.jds`:

```
on execute main.c:19 once do
    log "main.c:19 at ${PC:x4}, counter = ${mem[@counter]:d}"
end
```

```
jnext --headless --machine next --load prog.nex --script line.jds --delayed-automatic-exit-frames 30
```

the log says (z88dk 2.4 and the 2026-10-16 master alike, built as in the first session):

```
SCRIPT line.jds: names a source line; it loads when the --load program has loaded
Loaded 332 z88dk symbols from 'prog.map'
Loaded 281 z88dk source lines from '/home/you/prog/prog.map' and 4 listings (program identity verified)
SCRIPT loaded line.jds: 1 rules [client 2]
[jds F:500 C:283633231] main.c:19 at 8CB5, counter = 0 [client 2]
```

(333 symbols on the master build. The timestamps and the `[debugger] [info]` prefixes of the log are left out.) `@counter` is the C name of `_counter`. The rule fires at
frame 500 here because that is when the program starts after the boot.

## z88dk-gdb

`z88dk-gdb` does its own source mapping, from the map you give it with `-x`:
build the program with `-m -debug` and `break main.c:19` and `next`
work in the client. `monitor src` shows what JNEXT's own map (the one it
attaches when it loads the program) says about an address, so the two can be
compared. A session on the program of the first session, with JNEXT started as
`jnext --headless --machine next --load prog.nex --gdb-port 3333` (the setup is
in [Debugging with z88dk-gdb](../functions/11-debugging-with-z88dk-gdb.md)):

```
set pc 0x8000
break main.c:19
cont
monitor src
```

printed, with the `z88dk-gdb` client of the 2026-10-16 master (the 2.4 client has no `monitor`
command), `Adding breakpoint at 'main.c:19' $8cb5 (_main+35)`, `Hit breakpoint 1: @8cb5
(_main+35)` and `$8CB5 main.c:19 P04`. The `set pc 0x8000` restarts the program, which JNEXT
starts as soon as it loads and which is usually past `main` when you connect. The same
`break main.c:19` and `cont` stopped at `$8CB5` with the 2.4 and the master clients against the
2.4 and the master maps. `P04` is the 8K RAM page the line is in, as in the Source tab
([banked code](07-banked-code.md)).

Two limits of `z88dk-gdb` itself. With a map built by the 2026-10-16 master's sdcc
(`+zxn -clib=sdcc_iy -debug -m --list`), both clients answer `break main.c:19` with `Cannot
break on 'main.c:19'`: use an address or a symbol there, or build with sccz80. And with banked
programs the client's `next` is known to hang in banked code and to crash on a 24-bit line symbol
(the fixtures' README, Z4, and the design notes). z88dk PR #3218, merged 2026-10-31, fixes these
according to its description; it is newer than both installs used here, and no case was run with
it. The `next` hang and the crash were not re-run for this page.

## DeZog

DeZog reads the source map itself, from its own launch configuration (`"sjasmplus": [ { "path":
"mygame.sld" } ]`, see [Remote debugging with DeZog](../functions/10-remote-debugging-with-dezog.md)):
JNEXT does not pass it its map. The two maps can differ, and a mismatch shows as a line in one
tool that the other puts elsewhere.

## Whose breakpoints the Source tab shows

The gutter of the Source tab (as the Disassembly's) shows and changes only the debugger window's own
breakpoints. Those of a remote debugger (GDB, ZRCP, DeZog) or a script are in the Breakpoints panel,
read-only, and a click in the gutter never removes them: see
[Whose breakpoints the gutters show and change](../panels/08-breakpoints.md#whose-breakpoints-the-gutters-show-and-change).
