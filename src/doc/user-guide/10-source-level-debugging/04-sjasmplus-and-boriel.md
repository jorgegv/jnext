# 10.4 sjasmplus and Boriel ZX Basic

JNEXT reads two source map formats besides z88dk's: the **SLD** that sjasmplus
writes, and the **`Memory.txt`** that the NextBuild / Boriel ZX Basic toolchain
writes. They differ in what they carry: an SLD has the source lines and the
labels; `Memory.txt` has symbols only.

## sjasmplus: the SLD

sjasmplus writes a source map when asked for one with `--sld`. This is a
complete example, `hello.asm`:

```
        DEVICE ZXSPECTRUMNEXT
        SLDOPT COMMENT WPMEM, LOGPOINT, ASSERTION
        ORG $8000
start:  ld a,2
        out ($fe),a
loop:   inc a
        and 7
        out ($fe),a
        call delay
        jr loop
delay:  ld b,0
.wait:  djnz .wait
        ret
        SAVENEX OPEN "hello.nex", start, $BFFE
        SAVENEX CORE 3,0,0
        SAVENEX AUTO
        SAVENEX CLOSE
```

built with sjasmplus 1.23.1:

```
sjasmplus --sld=hello.sld --fullpath hello.asm
```

That writes `hello.nex` and `hello.sld` (its first line is `|SLD.data.version|1`).
Without `--fullpath`, sjasmplus warns that `--sld` "may produce incomplete file
paths". Loading `hello.nex` (with `hello.sld` beside
it) logs:

```
Loaded 4 SLD labels from 'hello.sld'
Loaded 10 SLD source traces from '/home/you/hello/hello.sld'
```

The Source tab then shows `hello.asm` with the current line, and `hello.asm:7`
works in the Breakpoints panel. The labels of the SLD join the symbols
(`start`, `loop`, `delay`, `delay.wait`). A label assembled for a bank names its
address only while that bank is mapped.

JNEXT looks for `<name>.sld`, else `<name>.sld.txt`, beside the program.

### Telling JNEXT the SLD belongs to this build

An SLD from a build that is not the one in memory would show the wrong lines. If
the SLD records the program's bytes, JNEXT checks them. Three comment records do
that; other SLD readers ignore them:

- `||program.sha256:`, the SHA-256 of the program as built;
- `||program.org:` and `||program.size:`, where it was placed and how long it is.

**sjasmplus does not write these records.** They are an extension for tools that
produce an SLD and know the program's bytes, such as a build script that adds
them after the assembler has run. Without them the SLD attaches unverified,
which is fine for a program you just built; but it also means a reset drops it
(a source map without an identity cannot be checked against the memory the ROM
clears a few frames later). A map you loaded by hand goes *stale* on a reset
instead: load it again. The full account is in
[loading a source map](../06-debugger/functions/14-source-level-debugging.md#loading-them).

## Boriel ZX Basic and NextBuild: `Memory.txt`

The Boriel compiler, `zxbc`, has an option `-M` (long form `--mmap`) that
"generates a label memory map". NextBuild, the build system around it, passes
`-M Memory.txt` on every compile, so a NextBuild project has a `Memory.txt` of
lines like

```
8000: ._Main
```

(a hex address, then the label). JNEXT loads it by itself when it finds
`<name>.Memory.txt`, else `Memory.txt`, beside a `.nex`; you can also use **Load
Memory.txt...** in the Source tab. It gives the **symbols**: names in the
disassembly, the Breakpoints and Call Stack panels and in the Add dialogs. It
does not give source lines.

Source lines for a BASIC program need an SLD. From the options of `zxbc` that are
documented in its source, Boriel's compiler does not write an SLD by itself, and
this guide does not describe a recipe for producing one. If your build produces
an SLD, name it `<program>.sld` beside the program and JNEXT reads it as above. If both a `Memory.txt` and an
SLD have a label, the `Memory.txt` one wins.

Where this page's statements about Boriel come from:

- `zxbc`'s `-M/--mmap` option: `src/zxbc/args_parser.py` in the
  [zxbasic repository](https://github.com/boriel-basic/zxbasic);
- NextBuild's use of `-M Memory.txt`: `Scripts/nextbuild.py` in the
  [NextBuild repository](https://github.com/em00k/NextBuild).

## sjasmplus symbol files

A plain `--sym` or `--exp` file from sjasmplus carries symbols but no lines. Load
it with `--map` or **Map ▸ Load MAP File**; see
[Symbols from MAP files](../06-debugger/functions/05-symbols-from-z88dk-map-files.md).
