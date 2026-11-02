# 7.4.7 The skoolkit and Ghidra exports

The other pages of 7.4 are inputs. This one is the reverse direction (GH #389, E23-26):
`Debugger::export_skoolkit()` and `export_ghidra()` write what the disassembly shows, and for
skoolkit the code/data map that `--coverage` recorded, into a directory, as the files the two
reverse-engineering tools read. The decisions are in section 11.2 of
`doc/design/SOURCE-LEVEL-DEBUGGING.md`; this page is the code and the formats.

## 1. The consumers

- **SkoolKit 10.1**, `sna2skool.py` and the control-file parser. A comment line starts with `#`,
  `%` or `;` and anything else must be a directive (`skoolkit/ctlparser.py:254-255`), so the first
  line of every file, `# jnext space <TAG>`, is inert. A bad line is only a warning
  (`skoolkit/ctlparser.py:119`, "Ignoring line"). An address is decimal, `$hex` or `%bin`, never `0x`
  (`skoolkit/__init__.py:88-98`). With no `-c`, `sna2skool.py` reads every `<input-stem>*.ctl`
  (`skoolkit/sna2skool.py:34`); a `-c` that names a directory reads `sorted(glob(DIR/*.ctl))`
  (`skoolkit/sna2skool.py:39-40`), so the file names must sort in address order. skoolkit is Z80-only and
  drops, with no warning, a label that is not at the start of a line it writes.
- **Ghidra 12.2**, `Ghidra/Extensions/Jython/ghidra_scripts/ImportSymbolsScript.py` (the Jython extension; the release bundles it, since
  12.1 it is not installed by default). `askFile()` at `:35` (headless: the file is the script's
  argument), `line.split()` at `:38`, `toAddr(pieces[1])` at `:41`, `createFunction()` at `:57` for
  `f` and `createLabel(address, name, False)` at `:62` for anything else. A line whose address
  space the program lacks makes `toAddr()` return nothing and the script dies on it, losing every
  later line while `analyzeHeadless` still exits 0. Both tools are test-time only and provisioned
  by `make skoolkit` and `make ghidra` (section 6).

## 2. The writers

Both are pure (std, `mem_space.h`, `long_address.h`, `disasm.h`); their inputs and outputs are the
plain structs of `src/debug/reexport.h`.

- **The classifier**, `skoolkit::classify()` (`skoolkit_ctl.cpp:33`). Over a space's whole store:
  an X byte starts an instruction whose extent is `instruction_length()`, clipped at the store end
  and at the next X start; a byte is code inside an extent, else data (R or W), else unknown. A
  store with no mark is one code run.
- **The files**, `skoolkit::build()` (`skoolkit_ctl.cpp:195`): one `<WWWW>-<TAG>.ctl` and `.bin`
  (`file_name()`, `:86`) per window of each space, `0000-CPU.ctl` for the unpaged labels. The
  windows are those holding the space's keys; with none, the one that maps the space now
  (`Input::mapped_window`), else `home_window()` (`:18`). `render_window()` (`:141`) writes the
  header, `@ start` / `@ org`, the block directives (`block_letter()`, `:136`), the labels, the
  `B`/`C` sub-blocks and the closing `i`. A Z80N instruction or a clipped one is a `B` sub-block,
  and so is each piece of an instruction a key falls in.
- **Window sizes and homes.** `window_size()` (`skoolkit_ctl.cpp:16`) is the size of the space's
  store: 8 KB, or 16 KB for `Rom`, `AltRom` and `Config`. A space with neither keys nor a current
  mapping goes to `home_window()` (`skoolkit_ctl.cpp:18`): the 128K bank positions for a RAM page
  (`$4000`/`$6000` for pages `0x0A`/`0x0B`, `$8000`/`$A000` for `0x04`/`0x05`, `$C000` plus
  `(n & 1) * $2000` for any other), `$0000` for ROM, `$2000` for a DivMMC bank. The home exists
  because coverage is keyed by (memory, offset) and records no logical address: nothing says where a
  page was mapped when it ran.
- **Where the identity lives.** A control file describes one 64 KB view and skoolkit's only bank
  notion is the `@bank` directive, which names neither the Next's 224 8 KB pages nor ROM and the
  overlays. So the identity is the file name and the first line, `"# jnext space " + tag`
  (`skoolkit_ctl.cpp:185`), a control-file comment skoolkit ignores. The same
  prefix is how a later export recognises its own files (`kCtlHeader`, `debugger_reinterop.cpp:31`).
- **File order.** `build()` puts `0000-CPU.ctl` first (`skoolkit_ctl.cpp:285`) and sorts all files by
  name with `stable_sort()` (`skoolkit_ctl.cpp:287`), which is address order. Two files that disagree at a shared boundary address are
  applied in command-line order, and `sna2skool.py -c DIR` reads `sorted(glob(DIR/*.ctl))`, so a
  paged label wins over an unpaged one at the same address, as in the disassembly.
- **The `.bin`.** `build()` writes it beside each control file (`skoolkit_ctl.cpp:265`) from the
  bytes `export_skoolkit()` read with `peek()` now (`debugger_reinterop.cpp:193`). skoolkit reads no
  NEX, so a Next page's control file is useless without them, and `sna2skool.py -o 0xWWWW
  WWWW-TAG.bin` finds exactly that `.ctl` by default pickup, which happens only when no `-c` is given.
- **No map for what cannot be read.** The identity-only memories (boot ROM, Multiface, DivMMC ROM,
  alternate ROM, Layer 2, config, floating) are refused by `peek()`, so they have no instruction
  lengths and get no code/data map; `export_skoolkit()` says so in the report (`"coverage of ... is
  not mapped"`, `debugger_reinterop.cpp:201`; SRCDBG-REX-07). With coverage off the report says that
  too (`debugger_reinterop.cpp:180`).
- **Unpaged labels** are also label-less keys of every window written that covers their address
  (`skoolkit_ctl.cpp:246-254`), so the same splits keep them from being dropped by sna2skool
  (SKCTL-08d).
- **Ghidra**, `ghidra::build()` (`ghidra_symbols.cpp:37`): one file per space; the `<TAG>:` prefix
  is set in `prefix_of` (`ghidra_symbols.cpp:52`); `CPU.ghidra.txt` and `view.ghidra.txt` (the labels the disassembly shows now).
- **The line formats.** `render()` (`ghidra_symbols.cpp:27`) writes `NAME <TAG>:0xHHHH l` for a
  space's file and `NAME 0xHHHH l` for `CPU.ghidra.txt` and `view.ghidra.txt` (the format string is
  at `ghidra_symbols.cpp:31`), sorted by address then name, with no header, comment or blank line, and
  only when the file has a line (`ghidra_symbols.cpp:62`). `view.ghidra.txt` exists because on a 48K
  or 128K machine every RAM label is paged, so the per-space files alone would need one overlay per
  page to import a 48K program into flat memory.
- **The Z80N table**, `kZ80nEd` (`disasm.cpp:362`) read by `z80n_ed_length()` (`:371`), by
  `instruction_length()` and by `is_z80n_extension()` (`:813`).

## 3. The verbs and the directory

`debugger_reinterop.cpp`: `gather_labels()` (`:73`) takes one entry per key of `symbols()` named by
`lookup()`; `Debugger::export_skoolkit()` (`:174`) adds the stores (`peek()` and `mem_coverage()`,
a note for coverage it cannot read) and `export_ghidra()` (`:218`) the view. `write_directory()`
(`:91`) checks every file in the way before writing (`Unsupported`), writes `<name>.part`
(`debugger_reinterop.cpp:125`) and renames it with `fs::rename` (`:135`), then removes a previous export's files that this one did not
write: a `*.ctl` whose first line is `# jnext space ` (`has_jnext_header()`, `:149`) and the `.bin` of its `WWWW-TAG` stem
(`is_ctl_stem()`, defined at `:57` and applied at `:156`), or the Ghidra names (`is_ghidra_export_name()`, `:47`). Anything
else in the directory is never touched. A path that is a regular file (`refuse()` at `:96`) or a directory that
cannot be created (`refuse()` at `:98`) is `RefusedUnavailable`; a `.ctl` or `.bin` in the way that is not jnext's
is `Unsupported` and nothing is written (`refuse()` at `:111`).

Names are exported as `lookup()` shows them (`gather_labels()`, `debugger_reinterop.cpp:82`), so a
z88dk C symbol goes out under its C name. The verbs are reads: they call only `peek()`,
`mem_coverage()`, `symbols()`, `lookup()` and `source_page()`, so no machine state changes and no
generation moves (SRCDBG-REX-03, with REX-04 as its control).

## 4. The frontends

`write_re_exports_at_exit()` (`src/platform/re_export_at_exit.h:39`) is called from the three loop
owners' `shutdown()`: `headless_app.cpp:1072`, `sdl_app.cpp:819`, `qt_app.cpp:513`. A failure logs
and sets the exit code to 1. `--export-skoolkit` and `--export-ghidra` are in
`src/core/cli_options.h`.

## 5. A real excerpt

The `F1` fixture of `reexport_test` (SKCTL-03) writes `C000-P10.ctl` as below; skoolkit 10.1 reads
it with no warning and `skool2asm.py` emits `banked:`, `table:` and `smc:`.

```
# jnext space P10
@ $C000 start
@ $C000 org
c $C000
@ $C000 label=banked
b $C003
@ $C003 label=table
B $C003
. two
. lines
u $C004
c $C010
B $C010,1 Load the first byte
@ $C011 label=smc
B $C011,2
b $C014
u $C015
c $C020
B $C020,4
u $C025
i $E000
```

## 6. Tests and the foreign readers

`reexport_test` (SKCTL, GHSYM and SRCDBG-REX rows) proves the formats and the verbs;
`cli_options_test` CLI-REX-01 the options. The writers are proved by the tools themselves, never
by a reader of ours: `skoolkit-foreign-func` runs `sna2skool.py` and `skool2asm.py` (a 48K
snapshot with `-c DIR`, a Next page by default pickup, the Qt and SDL owners, a failing export)
and `ghidra-foreign-func` runs `analyzeHeadless` with `ImportSymbolsScript.py` (and pins the abort
above as its control). `make skoolkit` (`test/provision-skoolkit.sh`, SkoolKit 10.1's PyPI sdist,
1.3 MB) and `make ghidra` (`test/provision-ghidra.sh`, Ghidra 12.2 with its Jython extension,
570 MB download, 954 MB on disk, a JDK 25 to run it: `java-25-openjdk-devel` is in the CI dnf
list) are sha256-pinned, verified before unpacking, and prerequisites of `regression`,
`regression-rows` and `regression-confirm`; the rows are `os=linux`. HS-100 and HS-101 prove a
wrong checksum is refused.
