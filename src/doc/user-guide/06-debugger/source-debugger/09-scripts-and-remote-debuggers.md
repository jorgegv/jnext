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
programs the master client crashed (segmentation fault) loading the map of the `-run` banked
example built with both installs, which has a 24-bit line symbol; the client's `next` is also
reported to hang in banked code, which was not re-run for this page. z88dk master from
2026-10-31 fixes these according to the change's description; it is newer than the two installs this section used
(2.4 and the 2026-10-16 master), and no banked case was run with a newer client.

## C variables

A z88dk `-debug` map carries the program's C variables (see [Building with
z88dk](02-building-with-z88dk.md)), and JNEXT reads them for scripts, for GDB and for
ZRCP. The three read the same values: each calls the same reader, which knows where
every variable lives, in which module, and in which memory page.

**In a script**, the `cvar(PATH)` atom: `on execute main.c:21 when cvar(i) == 3 ...`. The path
grammar, what is refused and why are in [Reading the machine](../scripting/04-reading-the-machine.md#c-variables-cvar).

**Over GDB**, three `monitor` commands (only a `z88dk-gdb` that has `monitor`, newer than 2.4):
`monitor locals [FRAME]`, `monitor globals` and `monitor print [FRAME] PATH`. They print their reason when
they cannot read something (`monitor set var` writes: see [Changing a C variable](#changing-a-c-variable)). A session on the `vars.c` / `other.c` program of `test/fixtures/z88dk/src` (built as the
`master/sccz80-cvars` fixture, whose `build.log` has the `zcc` line), with the `z88dk-gdb` client built from
z88dk checkout 97731e4e29 (2026-11-03), and JNEXT started as `jnext --headless --machine next --load prog.nex
--gdb-port 3333`:

```
set pc 0x8d54
break vars.c:41
cont
print s_dup
monitor print s_dup
print g_float
monitor print g_float
monitor locals
up
info locals
monitor locals 1
```

printed (trimmed to the answers; `b` is `g_int`, which the program's final loop keeps incrementing, so its value
depends on how long the program ran before the client connected):

```
$1 = <uint8_t> 4
(unsigned char) s_dup = 178
$2 = <> 0.000000
(float) g_float = 1.5
frame 0: sum at vars.c:41, fp $FF1A (tracked)
  (unsigned char) k = 120 'x'
  (struct point *) p = $FF38
  (int) b = 25180
  (int) a = 0
  (long) acc = 75900
  (int) t = 25307
  (unsigned char) calls = 5
OK
 * function main+133 (<struct point*>pp = 0xff38, <struct point>lp = {x=3, y=4, tag=5}, <uint8_t>i = 0, <char>c = 120, <int32_t>big = 100000, <int16_t[]>arr = 0xff30 [8] = { [0] = 21389, ... })
frame 1: main at vars.c:56, fp $FF3D (chain)
  (int [4]) arr = {21389, -9843, 16269, 20366}
  (long) big = 100000
  (char) c = 120 'x'
  (unsigned char) i = 0
  (struct point) lp = {x = 3, y = 4, tag = 5}
  (struct point *) pp = $FF38
OK
```

The client computes `info locals`, `print` and `up` itself, from the map it was given with `-x`, and for
most values it agrees with `monitor`. Where it does not, `monitor` is right. Measured in this session, and again
with the 2.4 client (which has no `monitor`, and printed the same answers for `print s_dup`, `print g_float`
and `info locals`): `print s_dup` is 4, the `s_dup` of `other.c`, where `vars.c`'s is 178 (the client picks the
first `s_dup` of the map by name, `monitor print` reads the one of the frame's own file); `print g_float` is
`<> 0.000000` for z88dk's 48-bit float, which `monitor print` decodes (1.5); `arr` is `int16_t arr[4]`, which the
client shows with 8 elements, four of them past the end of the array. By its source the client also asks for
memory with 16-bit addresses (not measured here), so a global in a banked page is read from whatever is mapped
there now, where `monitor print` reads it from its own page and says when that page is not mapped. `FRAME` counts
from 0, where the CPU is, as `up` does.

**Over ZRCP**, `CVAR(path)` in `evaluate` and in a breakpoint condition. It is a JNEXT extension of
ZEsarUX's condition dialect (ZEsarUX answers it `Error parsing`), so put a `PC=` term first in a condition: any other
is checked at every instruction. With the same program:

```
enter-cpu-step
enable-breakpoints
set-register PC=8D54H
set-breakpoint 1 PC=8D3EH AND CVAR(a)=2
get-breakpoints 1
run
evaluate CVAR(a)
evaluate CVAR(p->x)+CVAR(p->y)
evaluate CVAR(g_matrix[1][2])=6
evaluate CVAR(inner)
evaluate CVAR(p->x
```

answered (the register line of the stop and the command prompts are left out)

```
Enabled 1: PC=8D3EH AND CVAR(a)=2
Breakpoint fired: PC=8D3EH AND CVAR(a)=2
2
7
1
Error evaluating parsed string: CVAR(inner)
Error parsing
```

The breakpoint stopped at the third call of `sum` (`a` is 2), `CVAR(inner)` failed because `inner` is out of scope on
that line, and an unclosed `CVAR(` does not parse. A failed read has no reason in ZRCP, as ZEsarUX's reply has none;
`monitor print` and a script show it. `CVAR` takes the same paths as `cvar()` and gives integers and pointers.

### Changing a C variable

A C variable can be written from a script, over GDB and over ZRCP, in the frame the CPU is in or (over GDB) in one of its callers:

| Over | Form | Frame |
|---|---|---|
| a script | `set cvar(PATH) = v` (a 32-bit integer; [details](../scripting/08-changing-the-machine.md#set-cvarpath)) | 0 |
| GDB | `monitor set var [FRAME] PATH = VALUE` | `FRAME`, 0 by default |
| ZRCP | `set-register CVAR(path)=value` | 0 |

`PATH` is the same path as for reading. `VALUE` is an integer (decimal, `0x..` or `$..`; ZRCP also takes its own `nnnnH`),
a character (`'x'`), a decimal number for a float variable, or an address for a pointer. A float is rounded to the nearest
value its format holds (z88dk's 48-bit format, or IEEE 754 for zsdcc). A value that does not fit the variable's type is
refused, never truncated: `monitor set var g_uint = -1` is `refused: ... is out of range for unsigned int (0..65535)`.

```
monitor set var g_int = 10
monitor set var 1 c = 100
monitor set var p -> x = 10
```

`cvar()` and ZRCP carry no [notes](11-c-variables.md#notes-on-a-value); GDB's `monitor locals`, `print` and `set var` print each note in brackets on a line after the value (the 80cc frame-slot note, for example, also after a successful `set var`).

GDB answers each with the value read back (`(int) g_int = 10`), ZRCP with the register line of the stop. A refusal is
`refused: ` and the reason, and the reply `E01`, over GDB, and `Error changing register` over ZRCP, which has no room
for a reason. A write is a change of the machine like a `poke`: it is logged as a `MUTATE` line naming the client,
and it is refused while an RZX recording or playback is running. The program sees the new value when it next reads it.

What cannot be written, and why, is in [Limits and troubleshooting](10-limits-and-troubleshooting.md#limits).

## DeZog

DeZog has no C variables from JNEXT: the DZRP protocol has no command that could carry a variable, and DeZog evaluates its
own breakpoint conditions. It reads the source map itself, from its own launch configuration (`"sjasmplus": [ { "path":
"mygame.sld" } ]`, see [Remote debugging with DeZog](../functions/10-remote-debugging-with-dezog.md)):
JNEXT does not pass it its map. The two maps can differ, and a mismatch shows as a line in one
tool that the other puts elsewhere.

## Whose breakpoints the Source tab shows

The gutter of the Source tab (as the Disassembly's) shows and changes only the debugger window's own
breakpoints. Those of a remote debugger (GDB, ZRCP, DeZog) or a script are in the Breakpoints panel,
read-only, and a click in the gutter never removes them: see
[Whose breakpoints the gutters show and change](../panels/08-breakpoints.md#whose-breakpoints-the-gutters-show-and-change).
