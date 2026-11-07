# 7.4.7 The skoolkit and Ghidra exports and imports

The other pages of 7.4 are inputs. This one is the reverse direction:
`Debugger::export_skoolkit()` and `export_ghidra()` write what the disassembly shows, and for
skoolkit the code/data map that `--coverage` recorded, into a directory, as the files the two
reverse-engineering tools read; `import_skoolkit()` and `import_ghidra()` (stage B) read such files back
into the user's labels and comments (7.4.6). The decisions are in section 11.2 of
`doc/design/SOURCE-LEVEL-DEBUGGING.md`; this page is the code and the formats.

## 1. The consumers

- **SkoolKit 10.1**, `sna2skool.py` and the control-file parser. A comment line starts with `#`,
  `%` or `;` and anything else must be a directive (`skoolkit/ctlparser.py:254-255`), so the first
  line of every file, `# jnext space <TAG>`, is inert. A bad line is only a warning
  (`skoolkit/ctlparser.py:119`, "Ignoring line"). An address is decimal, `$hex` or `%bin`, never `0x`
  (`get_int_param()`, `skoolkit/__init__.py:88-98`). With no `-c`, `sna2skool.py` reads every `<input-stem>*.ctl`
  (`skoolkit/sna2skool.py:34`); a `-c` that names a directory reads `sorted(glob(DIR/*.ctl))`
  (`skoolkit/sna2skool.py:39-40`), so the file names must sort in address order. skoolkit is Z80-only and
  drops, with no warning, a label that is not at the start of a line it writes.
- **Ghidra 12.2**, `Ghidra/Extensions/Jython/ghidra_scripts/ImportSymbolsScript.py` (the Jython extension; the release bundles it, since
  12.1 it is not installed by default). `askFile()` (`ImportSymbolsScript.py:35`; headless: the file is the script's
  argument), `line.split()` (`ImportSymbolsScript.py:38`),
  `toAddr()` (`ImportSymbolsScript.py:41`),
  `createFunction()` (`ImportSymbolsScript.py:57`) for
  `f` and `createLabel()` (`ImportSymbolsScript.py:62`) for anything else. A line whose address
  space the program lacks makes `toAddr()` return nothing and the script dies on it, losing every
  later line while `analyzeHeadless` still exits 0. Both tools are test-time only and provisioned
  by `make skoolkit` and `make ghidra` (section 6).

## 2. The writers

Both are pure (std, `mem_space.h`, `long_address.h`, `disasm.h`); their inputs and outputs are the
plain structs of `src/debug/reexport.h`.

- **The classifier**, `classify()` (`skoolkit_ctl.cpp:34`).
  Over a space's whole store: an X byte starts an instruction whose extent is `instruction_length()`,
  clipped at the store end and at the next X start; a byte is code inside an extent, else data (R or W),
  else unknown. A store with no mark is one code run.
- **The files**, `build()` (`skoolkit_ctl.cpp:196`):
  one `<WWWW>-<TAG>.ctl` and `.bin` per window of each space, named by `file_name()` (`skoolkit_ctl.cpp:87`),
  and `0000-0CPU.ctl` for the unpaged labels. The windows are those holding the space's keys; with
  none, the one that maps the space now (`Input::mapped_window`), else `home_window()` (`skoolkit_ctl.cpp:19`).
  The text of a window is written by `render_window()` (`skoolkit_ctl.cpp:142`):
  the header, `@ start` / `@ org`, the block directives (`block_letter()` (`skoolkit_ctl.cpp:137`)),
  the labels, the `B`/`C` sub-blocks and the closing `i`. A Z80N instruction or a clipped one is a `B`
  sub-block, and so is each piece of an instruction a key falls in.
- **One name, one key**. z88dk's module-local labels repeat across routines (`end_x`
  at `$80CA` and `$8103` in `asm_zx_cls_wc_attr` / `asm_zx_cls_wc_pix`); `skool2asm.py` refuses a label
  defined twice, and both importers (and `UserAnnotations`, one key per name) refuse a name at two keys.
  So `unique_names()` (`reexport.cpp`) runs first in `build()` of each writer: of the exportable labels sharing
  a name at different (memory, address) keys it keeps the first by (unpaged, memory kind and index,
  address) and notes each other (`label 'N' at P04:$8103 skipped: the name is already used at
  P04:$80CA (one name, one address)`). The rule is global across one export, since an import reads
  several files into one store; the Ghidra `view.ghidra.txt` is its own import, so it is made unique
  among its own lines. The alternatives were rejected: qualifying a name with its module (the label
  would come back under a name nobody typed, and the module is not in the label) and refusing the
  export (the common z88dk newlib program would export nothing). Rows `SKCTL-18`, `GHSYM-07`; the
  foreign readers run it on `skool2asm.py`, `skool2ctl.py` and the importers (`skoolkit-foreign-func` A5,
  `ghidra-foreign-func`).
- **Window sizes and homes.** `window_size()` (`skoolkit_ctl.cpp:17`)
  is the size of the space's store: 8 KB, or 16 KB for `Rom`, `AltRom` and `Config`. A space with
  neither keys nor a current mapping goes to `home_window()` (`skoolkit_ctl.cpp:19`):
  the 128K bank positions for a RAM page (`$4000`/`$6000` for pages `0x0A`/`0x0B`, `$8000`/`$A000` for
  `0x04`/`0x05`, `$C000` plus `(n & 1) * $2000` for any other), `$0000` for ROM, `$2000` for a DivMMC
  bank. The home exists because coverage is keyed by (memory, offset) and records no logical address:
  nothing says where a page was mapped when it ran.
- **Where the identity lives.** A control file describes one 64 KB view and skoolkit's only bank
  notion is the `@bank` directive, which names neither the Next's 224 8 KB pages nor ROM and the
  overlays. So the identity is the file name and the first line, written as the string
  `"# jnext space "` (`skoolkit_ctl.cpp:186`) (a control-file comment
  skoolkit ignores). The same prefix is how a later export recognises its own files:
  `kCtlHeader` (`debugger_reinterop.cpp:33`).
- **File order.** `out.files` (`skoolkit_ctl.cpp:288`)
  puts the unpaged file first; it is named `0000-0CPU.ctl`, not `0000-CPU.ctl`, because the `0` sorts
  before the letter every `0000-<TAG>` starts with (`0000-ALTROM0`, `0000-BOOT` and `0000-CFG00` would
  otherwise sort ahead of it). All files are then ordered by name with
  `stable_sort()` (`skoolkit_ctl.cpp:290`), which is address order.
  Two files that disagree at a shared boundary address are applied in command-line order, and
  `sna2skool.py -c DIR` reads `sorted(glob(DIR/*.ctl))`, so a paged label wins over an unpaged one at
  the same address, as in the disassembly.
- **The `.bin`.** `build()` writes it beside each control file with the `out.files` push
  (`out.files` at `skoolkit_ctl.cpp:266`; `file_name()` only names it), from the bytes the export read with
  `peek()` (`debugger_reinterop.cpp:200`) now. skoolkit reads no NEX, so a Next
  page's control file is useless without them, and `sna2skool.py -o 0xWWWW WWWW-TAG.bin` finds exactly
  that `.ctl` by default pickup, which happens only when no `-c` is given.
- **No map for what cannot be read.** The identity-only memories (boot ROM, Multiface, DivMMC ROM,
  alternate ROM, Layer 2, config, floating) are refused by `peek()`, so they have no instruction
  lengths and get no code/data map; the report says so with the note
  `space_tag()` (`debugger_reinterop.cpp:208`)
  (SRCDBG-REX-07). With coverage off the report says that too, behind
  `mem_coverage_enabled()` (`debugger_reinterop.cpp:184`).
- **Unpaged labels** are also label-less keys of every window written that covers their address, added
  by the loop over `by_window` (`skoolkit_ctl.cpp:253`), so the same
  splits keep them from being dropped by sna2skool (SKCTL-08d).
- **Ghidra**, `build()` (`ghidra_symbols.cpp:39`):
  one file per space; the `<TAG>:` prefix is set in `prefix_of` (`ghidra_symbols.cpp:55`);
  `CPU.ghidra.txt` and `view.ghidra.txt` (the labels the disassembly shows now).
- **The line formats.** `render()` (`ghidra_symbols.cpp:29`)
  writes `NAME <TAG>:0xHHHH l` for a space's file and `NAME 0xHHHH l` for `CPU.ghidra.txt` and
  `view.ghidra.txt`, sorted by address then name (the line is built by the format string `" l\n"` (`ghidra_symbols.cpp:33`)),
  with no header, comment or blank line, and only when the file has a line, as
  `render()` (`ghidra_symbols.cpp:68`) shows.
  `view.ghidra.txt` exists because on a 48K or 128K machine every RAM label is paged, so the per-space
  files alone would need one overlay per page to import a 48K program into flat memory.
- **The Z80N table**, `kZ80nEd` (`disasm.cpp:362`), is read by
  `z80n_ed_length()` (`disasm.cpp:371`), by `instruction_length()` and by
  `is_z80n_extension()` (`disasm.cpp:813`).

## 3. The verbs and the directory

All in `debugger_reinterop.cpp`. `gather_labels()` (`debugger_reinterop.cpp:75`)
takes one entry per key of `symbols()` named by `lookup()`.
`Debugger::export_skoolkit` (`debugger_reinterop.cpp:176`)
adds the stores (`peek()` and `mem_coverage()`, a note for coverage it cannot read) and
`Debugger::export_ghidra` (`debugger_reinterop.cpp:225`) the view.

`write_directory()` (`debugger_reinterop.cpp:93`)
checks every file in the way before writing (`Unsupported`), writes `<name>.part`
(`part` (`debugger_reinterop.cpp:127`)) and renames it with
`fs::rename` (`debugger_reinterop.cpp:137`). Then it removes a previous
export's files that this one did not write: a `*.ctl` whose first line is `# jnext space `
(`has_jnext_header()` (`debugger_reinterop.cpp:151`))
and the `.bin` of its `WWWW-TAG` stem (`is_ctl_stem()` (`debugger_reinterop.cpp:59`),
applied at `is_ctl_stem()` (`debugger_reinterop.cpp:158`)), or the Ghidra names
(`is_ghidra_export_name()` (`debugger_reinterop.cpp:49`)). Anything else in
the directory is never touched. A path that is a regular file
(`refuse()` (`debugger_reinterop.cpp:98`)) or a directory that cannot be created
(`refuse()` (`debugger_reinterop.cpp:100`)) is `RefusedUnavailable`; a `.ctl` or `.bin`
in the way that is not jnext's is `Unsupported` and nothing is written
(`refuse()` (`debugger_reinterop.cpp:113`)).

Names are exported as `lookup()` shows them (`lookup()` (`debugger_reinterop.cpp:84`)),
so a z88dk C symbol goes out under its C name. The verbs are reads: they call only `peek()`,
`mem_coverage()`, `mem_coverage_enabled()`, `mem_coverage_spaces()`, `symbols()`, `lookup()` and
`source_page()`, so no machine state changes and no generation moves (SRCDBG-REX-03, with REX-04 as its
control).

### Import

`Debugger::import_skoolkit` (`debugger_reinterop.cpp:254`) and
`Debugger::import_ghidra` (`debugger_reinterop.cpp:300`) read ALL their
files into a scratch copy of `annotations()` and apply it with one `replace_annotations()`, so a refusal
anywhere changes nothing and a success is one revision bump (SRCDBG-REX-12, -13). The readers are pure,
in the writers' files:

- **skoolkit**, `parse_ctl()` (`skoolkit_ctl.cpp:475`). It mirrors the grammar of
  `_parse_ctl_line()` (`skoolkit/ctlparser.py:208`) but is strict where skoolkit only warns
  (`skoolkit/ctlparser.py:119`, "Ignoring line"). The parameters go through `parse_params()`
  (`skoolkit_ctl.cpp:447`), a port of skoolkit's own `parse_params()`
  (`skoolkit/ctlparser.py:39`): base letters, `:` sublengths, `*N` repeats, characters like `"A"`, and the
  `L` loop rules; the numbers are read by `get_int_param()` (`skoolkit_ctl.cpp:349`),
  skoolkit's own `get_int_param()` (`skoolkit/__init__.py:88`) without its `0x` form, in `py_int()` (`skoolkit_ctl.cpp:318`)
  which also refuses a value past 2^40, far beyond any address or length. A line skoolkit would drop (a
  bad integer, extra parameters on a lowercase block, `L` without a length or count, an unknown
  directive, a blank directive before any block, a bad `@ignoreua` suffix) refuses the file with
  `line N: reason`, as do an address outside 0..65535 (and, in a length, a number past 2^40), a label name outside
  `UserAnnotations::valid_label_name()`, one name at two addresses and text that is not UTF-8
  (SKCTL-12, SKCTL-15, SKCTL-17; measured against sna2skool 10.1). It takes `@ ADDR label=NAME` (a
  leading `*` dropped; two labels at one address keep the last, as skoolkit applies it) and the text of
  a `B C S T W` or blank-lead sub-block with its `.` / `:` continuation lines, joined with line feeds,
  as the comment at that address. An `@` line leaves a comment open, as in skoolkit (SKCTL-16). Block
  titles (a lowercase directive with text, or the first `.` line after one without), `D` descriptions
  and the other directives are counted in `Imported` (`reexport.h:71`) and not
  imported; no store holds a code/data map.
- **Ghidra**, `parse_symbols()` (`ghidra_symbols.cpp:85`), the script's grammar
  (`line.split()`, `ImportSymbolsScript.py:38`; `toAddr()`, `ImportSymbolsScript.py:41`): whitespace
  split, name, address, then anything; blank lines skipped. The address is `0xHHHH` or `TAG:0xHHHH`
  (`CPU:` is untagged); a prefix that is no jnext memory tag (`ram:0x8000`), an expression, a `#` line
  and a bad name refuse; two labels at one address keep the first (Ghidra's primary is the first
  created).
- **Which memory.** A control file's memory is its `# jnext space TAG` line (only before the first
  directive), else its name (`TAG.ctl`, `WWWW-TAG.ctl`), else none; none and `CPU` are untagged, and each
  untagged entry takes `source_page(addr)` when the file is read
  through `source_page` (`debugger_reinterop.cpp:276`),
  the identity the disassembly looks labels up with. A command-line import runs in
  `ScriptHost::start` (`script_host.cpp:171`), in
  `import_all()` (`script_host.cpp:218`), after `--annotations` and before
  `--load`, so an untagged file is placed by the mapping at start.
- **Export carries comments.** `export_skoolkit()` passes `annotations().entries()` with a comment as
  `Input::comments` from the loop over `annotations` (`debugger_reinterop.cpp:180`),
  and `symbols()` is the composed table, so a user label already shadows the toolchain's in both exports
  (SRCDBG-REX-14).

### Qt: Map ▸ Import and Export

The Qt debugger offers the four verbs in its Map menu, after the annotation entries of 7.4.6:
`import_menu` (`debugger_window.cpp:910`) and
`export_menu` (`debugger_window.cpp:915`), each with
`&skoolkit Control Files...` and `&Ghidra Symbols...` (`I` and `E` are free in the Map popup, `S` and `G`
are unique in each submenu; `DACC-05` pins the new popups and mnemonics). The slots are
`on_import_skoolkit` (`debugger_manager.cpp:858`),
`on_import_ghidra` (`debugger_manager.cpp:859`),
`on_export_skoolkit` (`debugger_manager.cpp:860`) and
`on_export_ghidra` (`debugger_manager.cpp:861`), which share
`import_re_files` (`debugger_manager.cpp:818`) and
`export_re_files` (`debugger_manager.cpp:841`):

- **Import** asks for one or more files with `QFileDialog::getOpenFileNames` (`*.ctl` or `*.txt`), calls
  the backend verb once with all of them, and shows the report (counts, then each note) in an
  `Import Done` box, or an `Import Failed` warning with the verb's `why` (the file and the line) and
  nothing changed. A success refreshes the panels, so the Disassembly's Label column shows the new names
  at once; the verb's one revision bump is what makes the quit question of 7.4.6 ask about unsaved
  annotations.
- **Export** asks for a directory with `QFileDialog::getExistingDirectory`, calls the verb and shows the
  report in an `Export Done` box, or an `Export Failed` warning with `why`.
- **Tests:** `QREX-01..04` in `debugger_verbs_test` drive the real file dialogs through `Modals`
  (a quoted file list or a directory typed into the dialog's name field) and compare the exported files
  with what the verb writes.

## 4. The frontends

`write_re_exports_at_exit()` (`re_export_at_exit.h:39`)
is called from the three loop owners' `shutdown()`:
`write_re_exports_at_exit()` (`headless_app.cpp:1085`),
`write_re_exports_at_exit()` (`src/platform/sdl_app.cpp:882`) and
`write_re_exports_at_exit()` (`qt_app.cpp:522`).
A failure logs and sets the exit code to 1. `--export-skoolkit`, `--export-ghidra`, `--import-skoolkit`
and `--import-ghidra` are in `src/core/cli_options.h`.

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
