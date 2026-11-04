# 7.9 C variables: the variable store

A z88dk `-debug` build puts the program's C debug information in the map as
`__CDBINFO__` symbols ([7.4.3](04-3-z88dk-map.md), section 7): one record per
function, module, variable and type, in the three compilers' own spellings. The
decoder turns one record into a `CdbRecord` (`cdb_decode_symbol()`,
`src/debug/cdb_record.h`) and knows nothing else. This page is the layer above
it: the **variable store** (`VariableStore`, `src/debug/variable_store.h`), which
joins those records to the map's symbols, so that a later consumer can ask where
a C variable lives, which function a local belongs to and which scope a code
address is in. Reading a value, a frame or a type is not here (last section).

## What the store holds

`VariableStore` (`class VariableStore`, `src/debug/variable_store.h:117`) is immutable once
built. Its five vectors:

| Accessor | Holds |
|---|---|
| `functions()` | One `CFunction` per `F:` record: the decoded record, its `name`, its `module` (the module of the entry symbol, never the record line's), `entry` (a `CStorage`) and its `scopes` table |
| `variables()` | One `CVariable` per `S:` record, hidden ones included (views hide, the store keeps): the module of its CDB line, the C name, `function` (an index into `functions()`, -1 for none) and `storage` |
| `types()` | Every `T:` record with its module; the lookup rules (own scope, then a unique tag) belong to the first consumer |
| `modules()` | Every `M:` record (zsdcc and 80cc only) |
| `refused()` | `"<escaped symbol>: <reason>"` of every record the decoder refused |

A `CStorage` is `resolved` or it says `why` not: a resolved one has the page-aware
address (`SourceAddress`, the same type as a line's start, so a banked variable is
`Page{20}:$C000`) and the map `symbol` it was joined to. Only E-space records are
resolved (data symbols and function entries); stack (`B`), register (`R`), code
(`C`), literal (`D`), port (`I`) and struct-member (`Z`) records are kept as they
are, and so is a record with an empty type: sccz80 and 80cc write both an enumerator
and an I/O port as `({0})` at `E,0,0`, and neither has memory there, so such a
record is never given a storage.

## Where it lives, and why

The store is a member of the `SourceMap` it was read from
(`SourceMap::variables()`, `src/debug/source_map.h:113`), held by
a `shared_ptr` to an immutable object, exactly like the embedded texts of 7.2.
`SourceMap::replace()` takes it as its last argument and stores it
(`variables_`, `src/debug/source_map.cpp:127`); every `replace()`
without one drops the previous store, and `clear()` resets it
(`variables_`, `src/debug/source_map.cpp:347`).

That placement is the design. `Debugger::Impl::source_map` is assigned, moved,
copied or cleared at about ten places (the owner, the stale flag, `drop_sidecars()`,
the tape `DeferredCheck`, the attachment cache, the ring restore). A store kept
beside it would need an edit at each, and a missed one is a store that outlives or
misses its map. Inside it, every rule of [7.6](06-lifetime.md) applies with no
code of its own: the store is attached with the map, dropped with it, made stale
with it, cached and restored with it, and the copies share one object, so the
cache costs nothing more.

The facade is `Debugger::variables()` (`variables`, `src/debug/debugger_source.cpp:578`):
it runs `recheck()` first, like every reader, and returns the live source map's
store, or null when there is no live map or it carries none (an SLD). A z88dk map
with lines but no record carries an **empty** store, so "this program has no C
information" is told from "there is no z88dk map". A map with no lines at all gets
no store, because it replaces nothing. `Load MAP` (symbols only) never has one.

## How it is filled

`load_z88dk_source_map()` reads the map once. Pass 1 keeps a `const` line that
starts `__CDBINFO__` in `cdb_lines` (`src/debug/z88dk_source_map.cpp:584`),
and the parser gained `Z88dkMapLine::is_public` (the scope field). The line records
of code sections are copied to `scope_records` right after they are placed and
**before** a listing replaces its module's lines (`scope_records`, `src/debug/z88dk_source_map.cpp:647`):
a listing's `C_LINE` keeps only the file, so the scope can come only from the map.
Just before `map.replace()`, `build_variable_store()`
(`src/debug/z88dk_source_map.cpp:444`)
decodes each record, joins, and builds the store. A symbol is placed in its
space by one lambda, `place_symbol` (`src/debug/z88dk_source_map.cpp:876`),
shared by the extent code and the store, so no rule is kept twice.

## The joins

The key is the **module**, not the file name (z88dk issue 3222, a wrong
file name in some records, does not reach JNEXT). Index: `(module, name)` and
`public name`, each to the placed symbols that carry it. Two or more matches is
ambiguous and reported as such; the first is never taken.

| Record | Joined to | Why |
|---|---|---|
| `S:G$x` | the PUBLIC `_x`, whatever its module (`join_public()`, `src/debug/z88dk_source_map.cpp:241`) | A record's module column is its first emitter, not its definer. Measured on `master/sdcc-extern` (`ext_use.c` declares `extern int g;`, `ext_def.c` defines it): zsdcc writes `S:G$g` with `ext_use_c` while `_g` is public in `ext_def_c`; sccz80 writes it in `ext_def_c`. The same collapse makes `F:G$other` say `vars_c` while `_other` is in `other_c` (`master/sccz80-cvars`) |
| `S:F<file>$x`, level 0 | `_x` in the record's module (`join_in_module()`, `src/debug/z88dk_source_map.cpp:231`) | `_s_dup` is one symbol in `vars_c` and another in `other_c` |
| `S:F<file>$st_<fn>_<x>`, level > 0 | `_st_<fn>_<x>` in the module; the local belongs to the module's function `fn`, its C name is `x` (`static_local_function()`, `src/debug/z88dk_source_map.cpp:301`) | The writers: `src/sccz80/declparse.c:1012`, `src/80cc/declparse.c:1099`. Several candidate functions (`a` and `a_b`): left unattributed, with the raw name. 80cc's `st_<fn>_<x>_<n>` keeps `_<n>` in the C name, not distinguishable from a name that ends so |
| `S:L<scope>.<fn>$x$L_S$B`, space E (a zsdcc static local) | `_<fn>_<x>_<L*10000+S>_<B>` in the module (`join_variable()`, `src/debug/z88dk_source_map.cpp:273`) | SDCC `allocLocal()`, `z88dk/src/sdcc-build/src/SDCCmem.c:876` |
| `S:L...`, any space | `function` = the one `CFunction` of that name AND module (`function_of()`, `src/debug/z88dk_source_map.cpp:288`) | A local exists only in its defining unit; `util_inc` of a header is two functions, one per module, each with its own `v` |
| `F:G$f` | the public `_f`; the function's module is that symbol's | |
| `F:F<file>$f` | `_f` in the record's module | |

A record that cannot be joined stays, with `storage.why` (or `function == -1`).
Nothing is dropped.

## Storage

A joined symbol is placed by `z88dk_section_space()` on its section and value, the
same rules as the lines ([7.4.3](04-3-z88dk-map.md)): a numbered section is paged
(`_btable` $14C000 in `PAGE_20` is `Page{20}:$C000`), a dot command's `CODE` is
`DivMmcRam{2}` (rule 5), and any other section binds to the RAM page the MMU maps
there when the map loads (rule 4; outside RAM it is unresolved with the reason).
A DIV section or an unplaceable 24-bit value is unresolved with the placement's
reason.

## The scope table

Each function has a table of the scopes its code is in, built from the map's own
`C_LINE` records (`fill_scopes()`, `src/debug/z88dk_source_map.cpp:376`). Records
at one address are grouped, and the highest line owns the address (the rule the
line ranges use). Equal-line owners that disagree make the entry unknown, so no
order of the map can change the answer. The entry is filed under the function of
the same module and section whose entry map VALUE is the greatest one at or below it (never by comparing
spaces: a function that crosses an 8 KB slot lives in two, [7.10](10-c-frames-and-values.md)), with its
`offset` from that entry.

The compilers write the scope differently:

- sccz80 and 80cc: `file::<func>::<level>::<block>:<line>`. The level is the
  compiler's, the block is the last one opened, not the current one, and 80cc has
  level 0 everywhere and no block structure. The named function must be the
  function at the address, or the entry is unknown. This is the line-44 gap: `int
  main(void)` is scoped `sum::0::2` at `_main`, and the same address holds line 45
  (`main::0::3`), which owns it; were line 44 alone there, the cross-check makes it
  unknown instead of giving `main` the block numbers of `sum`.
- zsdcc: zcc's peephole rule writes `C_LINE %2,"%1::x::%eval(%3 10000 * %4 +)::%5"`
  (`z88dk/lib/sdcc/sdcc_opt.1:313`): the function is the literal `x`, and the
  number is `level * 10000 + sub`. A location is zsdcc's when its function field is
  `x` and (a TAB follows it, or the function at the address is not named `x`);
  then `level = n / 10000`, `sub = n % 10000`, and there is no function
  cross-check. The store decomposes it so both sides of a comparison with the CDB
  records (which are in `(level, sub)`) agree.

A location that names only a file and a line, with no scope, is unknown. Block-tree reconstruction and
visibility are not decided here.

## Refusals are lenient

A record the decoder refuses goes to `refused()` with its reason; one that cannot
be joined stays with its reason. Neither refuses the lines: the strictness of 7.3
protects lines, and a new compiler spelling must not cost the user source-level
debugging. Every `__CDBINFO__` line of a map ends up in exactly one of
`functions()`, `variables()`, `types()`, `modules()` and `refused()`
(`Z88CDB-01`).

## Canonical order

The vectors are sorted by the record's text (then module), and a function's scope
table by offset, so the store is the same whatever order the map lists its lines
in (`Z88CDB-20`). No map line number is kept. `VariableStore`'s constructor
(`VariableStore::VariableStore()`, `src/debug/variable_store.cpp:20`) sorts the functions and
remaps each variable's `function` to the sorted index.

## Limits

- z80asm keeps the first of two identical symbols (`symtab1.c:117`), so a record
  that two modules both write is held once.
- 80cc's second static of one name, `st_<fn>_<x>_<n>`, keeps `_<n>` in the C name.
- A zsdcc function literally named `x`, in a map without the TAB, is read as
  sccz80's form.
- The frame helpers (`__debug_framepointer`, `l_debug_push_frame`) are not stored: a
  hand-loaded z88dk map attaches no symbols, so the next stage adds them to this same pass.

## Not here

Frames, the extents they stand on and the rule that keeps the tracker running are in
[7.10](10-c-frames-and-values.md). Values, the "location unknown" of sccz80's fastcall
parameter, the text that says why no variables are shown, and the type lookup are the next
stage of the epic. The store gives them the records, the storage and the scopes.

## Rows

(`Z88CDB-41` to `-50` and `SRCDBG-CVAL-01` to `-58`: [7.10](10-c-frames-and-values.md).)
`Z88CDB-01` to `-26` in `z88dk_map_test` (the joins, scope tables, order,
refusals, banked and dot storage, hidden records, listings), `SRCVAR-01` in
`source_map_test` (the member), and `SRCDBG-VAR-01` to `-12` in
`source_debug_test` (the lifetime). `Z88CDB-40` compares every function's scope table on 12 maps with a golden that
`test/fixtures/z88dk/derive-scopes.py` derives from the raw map lines (its header holds the rules).
The fixtures are the committed maps; the
`master/*-extern` pair exists for the one case the others lack
([7.8](08-tests-and-new-producers.md)).
