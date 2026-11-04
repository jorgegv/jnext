# 7.8 Tests, fixtures and adding a producer

Source-level debugging reads files other people's tools write, so its tests have
a second job beyond checking the code: the expected values must come from the
tools' real output, not from the code under test. This page lists the suites,
explains where the fixtures come from and how they stay honest, and gives the
checklist for supporting another toolchain.

The test counts are not written here, on purpose. `test/unit-tests.conf` is the
only source ([4.2](../04-testing/02-declared-suites-and-pinned-counts.md)), and a
number copied into prose goes stale on the next branch. The process for writing
rows, with the VHDL as the oracle where hardware is involved, is in
`doc/testing/UNIT-TEST-PLAN-EXECUTION.md`.

## The suites

| Suite | What it covers |
|---|---|
| `source_map_test` | The std-only layer, with no emulator: `SourceMap` (`SRCRNG-*`, `SRCMAP-*`, `SRCPATH-*`, `SRCTXT-01`, `SRCVAR-01`), the SLD adapter (`SLD-*`, `SLDL-*`), the symbol table's pages (`SYMPG-*`), the NextBuild adapter (`NBSYM-*`), and the fixture manifest (`Z88FIX-01`, `Z88FIX-02`) |
| `z88dk_map_test` | The z88dk map: the source half (`Z88MAP-*`), the symbol half and binding (`Z88SYM-*`), C names (`Z88CN-*`), dot commands (`Z88DOT-*`, [7.4.3](04-3-z88dk-map.md) rule 5), the C variable store (`Z88CDB-*`, [7.9](09-c-variables.md)) and the extents and frame helpers (`Z88CDB-41`..`-50`, [7.10](10-c-frames-and-values.md)), and the size cap and BOM of the symbol loaders (`MAPCAP-*`, `MAPBOM-*`) |
| `z88dk_lis_test` | The listing adapter (`Z88LIS-*`), including the placement of a label-less piece by its bytes (`Z88LIS-36` to `-47`, [7.4.4](04-4-z88dk-listings.md) rule 5b) and the BOM (`Z88LIS-35`, `-44`) |
| `c_value_test` | The pure C value layer (`c_value.cpp`, [7.10](10-c-frames-and-values.md)): the 48-bit and IEEE floats, integers, pointers, arrays, structs, bitfields, the struct type lookup and type text, the path grammar and its evaluation, visible locals, register locals (`CVALF-*`), the children of an object (`CVCH-*`), the encoder of a written value (`CVW-*`), and the int32 rule of the script atom and ZRCP (`CVINT-*`) |
| `cdb_record_test` | The z88dk C debug records (`__CDBINFO__`, `cdb_decode()` in `src/debug/cdb_record.cpp`; epic #353): un-escaping (`cdb_unescape()`), the scope split at the last `.`, the type chain, storage, `T:` members, what views hide (`CdbHidden`), refusals with their reasons, and every record of every committed map (`CDB-*`). Records measured from the z88dk master install are quoted in the rows with the probe and command |
| `source_debug_test` | The backend on a real machine: identity of fetches (`SRCDBG-ID-*`), sidecars and ownership (`SRCDBG-SIDE-*`, `-OWN-*`), symbols (`SRCDBG-SYM-*`), z88dk (`SRCDBG-Z88-*`, `-LIS-*`), tapes (`SRCDBG-TAPE-*`), the variable store's lifetime (`SRCDBG-VAR-*`), the tracker policy and the C frames (`SRCDBG-CVAL-*`, [7.10](10-c-frames-and-values.md)), writing a C variable (`SRCDBG-CVW-*`), the children of a C variable on a running machine (`SRCDBG-CVCH-*`), lifetime (`SRCLIFE-*`), dot commands (`SRCDOT-*`, [7.6](06-lifetime.md)), breakpoints (`SRCBP-*`), run to line (`SRCRUN-*`), steps (`SRCSTEP-*`, `SRCDBG-STEP-*`, `-BACK-*`, `-HIST-*`), call stack (`SRCDBG-CS-*`), coverage (`SRCDBG-COV-*`), the long address through a machine (`LADDR-17` to `-20`; the pure formatter's `LADDR-01` to `-16` are in `debugger_backend_test`) |
| `source_debug_posix_test` | The rows whose fixtures are POSIX file types (a looping link, a FIFO); gated by OS |
| `mmu_test`, `mmu_integration_test` | The arbiter against the VHDL's memory map (`FSRC-*`) |
| `step_out_test`, `rewind_call_stack_test` | Step Out's `RET` predicate, and the call-stack tracker restored across a rewind (the frame-ending rule itself is `SRCDBG-CS-08` to `-15` in `source_debug_test`) |
| `debugger_backend_test` | Symbols across formats (`SYM-*`), and the pure long-address formatter (`LADDR-01` to `-16`) |
| `gdb_rsp_test`, `zrcp_adapter_test`, `dzrp_adapter_test` | `monitor src` and `monitor sym` (`GDB-MON-*`); `monitor locals`, `globals` and `print` on the cvars fixtures (`GDB-CVAR-*`); ZRCP `CVAR(path)` in `evaluate` and conditions (`ZRCP-CVAR-*`); the C writes `monitor set var` (`GDB-CVW-*`) and `set-register CVAR(path)=value` (`ZRCP-CVW-*`); ZRCP `get-memory-pages` names and `cpu-step-over` at a DivMMC entry (`LADDR-21`, `-22`); DZRP's `0xFF` for a ROM slot (`DZRP-REG-08`) |
| `script_events_test` | The DSL's `on execute FILE:LINE` and `--map` |
| `debugger_variables_panel_test` | The Qt Variables tab on a real Next running the z88dk fixtures: frames, locals, globals, lazy expansion, the Call Stack selection both ways, addresses, refresh, idle (`QVAR-*`, and editing a value `QVARW-*`, the C rows of the Watches tab included, [7.7](07-frontends.md)); gated on Qt and the debugger |
| `script_eval_test` | The DSL evaluator; its `EVAL-CVAR-*` rows are the `cvar(PATH)` atom over a real Next machine and the committed cvars fixtures (scalars, members, elements, qualified names, banked globals, every refusal, no C variables, interpolation, a `when`, and a tape program's verdict applied inside a delivery) |
| `debugger_source_panel_test`, `debugger_verbs_test`, `debugger_panels_test`, `debugger_disasm_copy_test`, `debugger_menu_test` | The Qt Source tab (`QSRC-*`: the highlighter and schemes `QSRC-190` to `-200`, the selection sync `QSRC-176` to `-189` and `-201` to `-208`, the dot command's tab `QDOT-*`), the Map menu, and the long address in every panel (`QLADDR-*`); gated on Qt and the debugger |

A suite marked `?` in `test/unit-tests.conf` is gated; the harness checks that its
absence matches the gate for the build ([4.2](../04-testing/02-declared-suites-and-pinned-counts.md)).

Functional rows (`test/00regression/functional_tests.conf`):

| Row | Covers |
|---|---|
| `sld-labels-func` | An SLD sidecar's labels reach the symbol table a remote client reads, by page |
| `source-map-func` | A `<stem>.sld` beside a program attaches as its source map in every frontend; one whose identity does not match is rejected, with the reason |
| `z88dk-source-map-func` | A z88dk `-debug` map attaches as symbols and source lines, from beside the program and from `--source-map`, in every frontend |
| `z88dk-source-func` | The remote and script surface of the source map: `on execute main.c:19` rules against the committed z88dk fixture |
| `z88dk-tape-source-func` | A z88dk map attaches to a tape program at once and its identity is judged where the CPU first runs the code |
| `gdb-z88dk-func` | The real `z88dk-gdb` client against the GDB server, with a program and its map |
| `nextbuild-symbols-func` | `Memory.txt` in headless, Qt and SDL |
| `script-map-pipe-func` | `--map <(cmd)`, read once |
| `z88dk-dot-source-func` | A z88dk dot command's map with `--source-map` and no `--load`: attached pending, judged at the command's entry; any other map refused (exit 1) |
| `z88dk-dot-source-rzx-func` | `--source-map` with an RZX replay (`--rzx-play`, `--load R.rzx`): a dot command's map attaches, any other is refused naming the RZX |
| `coverage-frontends-func` | `--coverage` starts memory coverage in every loop owner (headless, Qt, SDL) |

## Fixtures

**`test/fixtures/z88dk/`** is what the two z88dk installs actually produce for one
small program, committed so that CI, which has no z88dk, can check the adapters
against ground truth. Its `README.md` is the reference: the two installs and
their commits, the build matrix (one row per fixture directory with its flags and
what it covers), the numbered findings (G1...G24, Z1...Z4) that rows cite, and
the rules that normalise and trim the output.

- **Regenerate with `bash test/fixtures/z88dk/generate.sh`.** Overrides:
  `Z88DK_24` and `Z88DK_MASTER` (the install roots; defaults
  `~/src/spectrum/z88dk-original-2.4` and `...-latest`). The output is
  deterministic: `git diff --stat` must be empty unless an install changed. When
  one did, commit the new outputs and `MANIFEST.sha256` together and say which rows'
  values moved.
- **The script exits non-zero** when a build that should succeed fails, and when
  one recorded as not producible starts to build (`EXPECT_FAIL=1`). The failures
  are recorded too: `v2.4/sdcc-debug` and `v2.4/classic-debug` keep only
  a `build.log`.
- **`MANIFEST.sha256`** lists every file the rows hash: sources, the script, the
  outputs (not `README.md`). `Z88FIX-01` checks every entry's hash; `Z88FIX-02`
  checks the manifest lists exactly the files present.
- **Never hand-edit a fixture.** A normalisation or trim that is wrong is fixed in
  `generate.sh` ("widen the rule") and the fixtures regenerated. The generated
  files are normalised so they do not depend on the machine: install paths become
  `z88dk/`, temporary names `tmpXXXXXXXX`, the compile date `(normalised)`.
- **Why committed.** CI has no z88dk, and the values are the oracle: every expected
  address, line and section in a z88dk row comes from these files, never from the
  code under test.

The dot-command builds (GH #351) are `dot:<subtype>` lines in `generate.sh`, compiled from `dot.c dutil.c`
with `-subtype=dot`, `dotx` or `dotn` instead of `nex`: `v2.4/sccz80-dot`, `sccz80-dotx`, `sdcc-dot`,
`v2.4-dotn/sccz80` and `master/sccz80-dot`, `sccz80-dotx`, `sdcc-dot`, `80cc-dot`. The dotn lives in its own
directory so that `Z88CN-10`'s walk over `v2.4/` and `master/` (every map shows a C name) is not tripped by
its refused C symbols. The banked-CRT builds (`sccz80-banked-crt`, both trees) are the fixtures of the
placement by bytes (GH #426 F4).

**`test/fixtures/sjasmequ/`** is made by hand with an exact command, sjasmplus
commit and `sha256` per file (its `README.md`): `equlabels.asm` is ours, and
`equlabels.sym`, `equlabels.exp`, `equlabels-win.sym` (a MinGW build run under
wine), `equlabels-v1.17.sym` (a build of the v1.17.0 tag) and `equlabels.sld` are what
sjasmplus writes for it, byte for byte.

**`test/fixtures/zdebugmap/`** is the trimmed 2.4 maps (`debug.map`, `nodebug.map`)
of a small program, kept for the symbol rows `SYM-14`..`SYM-18` (GH #352); its
README records the trimming.

There is no committed fixture for a `Memory.txt` or for the SLD identity records;
[7.4.5](04-5-nextbuild-memory-txt.md) and [7.4.1](04-1-sld.md) say so and label
those excerpts synthetic.

## Adding a producer

A "producer" is a tool whose output could feed the stores: another assembler, a
compiler, a linker. Support is a contract, and the contract is
`doc/design/SOURCE-LEVEL-DEBUGGING.md` section 9.1: "A new format is
therefore supported only once this section states its extent and space rules". The checklist, with
the file each step touches:

1. **Commit ground truth first.** Real output from the real tool, with its version
   and commit, the exact command, a README and `sha256` sums, as `test/fixtures/sjasmequ/`
   does; or a generator script under the rules of `test/fixtures/z88dk/`
   (`generate.sh`, a manifest, deterministic output). If the tool cannot be run on a
   CI machine, the output is committed and a manifest row guards it.
2. **State the two rules before writing code.** The **extent** rule: how many bytes
   a record covers. The **space** rule: which memory a record belongs to. Write them as
   the first two parts of a new [7.4](04-formats.md) page, with the producer's code
   cited by tree and line, and quote the grammar from that code.
3. **Emit only qualified ranges.** Every `SourceRange` carries a `FetchSource`; parse
   the whole input before anything is replaced; replace a store only on success
   ([7.3](03-loaders.md)). A source half is strict and a symbol half lenient.
4. **Look before you open.** Call `source_file_problem()` ([7.3](03-loaders.md)) on every
   file the adapter opens, unless it must read a pipe.
5. **Dispatch by content.** Extend `load_source_map()` (`looks_like_sld()` and its
   neighbours in `debugger_source.cpp`) and/or `MapFormat`/`Debugger::load_map()` so the
   format is told apart by its contents, never by its extension.
6. **Add it to the sidecar search** in `load_program_sidecars()` if the format sits
   beside a program, and decide its precedence against the others, in the order and
   with the owner rules of [7.3](03-loaders.md). A new extension for a program format
   goes in the table in `source_file.cpp`.
7. **Identity.** If the format can carry or be given a program identity, decide
   its form ([7.2](02-the-source-map.md)) and what it does on a landing and for a tape
   ([7.6](06-lifetime.md)).
8. **Rows.** Literal IDs, in `source_map_test` for the std-only adapter and in
   `source_debug_test` for the backend, with expected values that come from the
   fixtures; a functional row if a frontend path is new; and the `# expect:` count
   updated in `test/unit-tests.conf` and `functional_tests.conf`. The IDs must be
   string literals (the harness refuses a built-up ID), and unique across suites
   ([4.4](../04-testing/04-traceability.md)).
9. **Documentation.** The user guide's chapter 10 (the user's side), a new page in
   [7.4](04-formats.md) with the nine parts, a row in the design document's section 9
   (the contract), and, if a flag is added, the man page and `make cli-check`.
