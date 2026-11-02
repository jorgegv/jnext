# 7.4.6 The annotations project file (`.jdp`) and the user's labels

The user's own labels and comments (GH #387) are the one input of chapter 7 that
no toolchain writes. JNEXT defines the file itself: plain UTF-8 text, extension
`.jdp`, opened and saved by the user and never attached beside a program. It
fills a third store, `UserAnnotations`, which sits beside the toolchain's
`SymbolTable` and wins over it when the two disagree.

## 1. The producer

JNEXT. The Qt Map menu opens and saves it (stage 2 of #387); `--annotations FILE`
loads one at start-up (`src/main.cpp:884` parses it, `src/main.cpp:2176` puts it
in the configuration, and `ScriptHost::start()` loads it at
`src/script/script_host.cpp:201-215`, after `--map` and before the scripts, so a
script's `@name` of the file resolves). No other tool writes it yet; #389's
skoolkit and Ghidra export will read and write it through `replace_annotations()`.

## 2. The producer's writer

`write_annotations()`, `src/debug/annotations_file.cpp:69-77`: the header line
`jnext-annotations 1`, then, for each entry in `UserAnnotations::entries()` order,
the `label` record and then the `comment` record. A memory is `space_tag()`
(`src/debug/long_address.h`, [7.1](01-which-memory.md)); an address is `$` and four
upper-case hex digits (`hex4()`); a comment is escaped by `escape()`
(`src/debug/annotations_file.cpp:22-30`): `\` becomes `\\` and a line feed `\n`.
LF is the only line end written, and no `#` line, so a Save does not keep a user's
own `#` lines.

The order is the key's: `AnnotationKey::operator<`
(`src/debug/user_annotations.h:33-37`) compares the memory kind (the
`MemSpace::Kind` enumerator order: `Page`, `Rom`, `BootRom`, ... `DivMmcRam`...),
then the index, then the address, so the file diffs well in a repository.

## 3. The grammar

```
jnext-annotations 1
label   <SPACE> $<ADDR> <NAME>
comment <SPACE> $<ADDR> <TEXT>
```

One space between fields. `<NAME>` is one token (below), `<TEXT>` is the rest of
the line and is never empty. At most one label and one comment per key, and a
name at one key only.

## 4. The parser's rules

`read_annotations()`, `src/debug/annotations_file.cpp:79`, validates the whole
file into a scratch store and assigns `out` only when it is all good; every
error is `line N: reason`.

1. **Skipped:** a UTF-8 BOM at byte 0 of line 1 only (`:90`), a trailing CR
   (CRLF), a line whose first character is `#`, a blank line.
2. **The header** is the first other line and must be exactly `jnext-annotations 1`
   (`:94`); `jnext-annotations N` for another canonical N reads `version N: this
   jnext reads version 1` (`:100`); anything else is `not a jnext annotations
   file`.
3. **A record** with an unknown kind is refused (`:117`; forward compatibility is
   a version bump, not skipping). `<SPACE>` goes through `parse_space_tag()`
   (case-insensitive, strict digits, the hardware's ranges) or the file is
   refused (`:122`); `CPU` is refused (`:124`), since a user entry names a memory.
   `<ADDR>` is `$` and exactly four hex digits, either case (`parse_addr()`, `:46`,
   `:127`). A label has exactly four fields (`:130`). A second label or comment for
   a key is refused (`:132`, `:141`), a CR inside a text is refused (`:143`), and
   `unescape()` (`:33`) accepts only `\\` and `\n` (a final lone `\` is refused,
   `:37`).
4. **Names and text** are checked by the store, not the adapter, so the API and the
   file cannot disagree: `UserAnnotations::valid_label_name()`
   (`src/debug/user_annotations.cpp:29`: components `[A-Za-z_][A-Za-z0-9_]*`
   joined by single dots, exactly what the DSL's `@name` lexes,
   `src/script/lexer.cpp:217-235`), `valid_utf8()` (`:39`: no overlong form, no
   surrogate, nothing past U+10FFFF) and the uniqueness of a name
   (`set_label()`, `:62-80`).
5. **A key the file can name.** `nameable()` (`src/debug/user_annotations.cpp:15`)
   refuses any memory with `parse_space_tag(space_tag(s)) != s` (page 0xE0, ROM 4,
   DivMMC bank 16 ...), so Save never writes a file that Load then refuses whole.
   The rule is the parser's own; there is no second range table.
   `set_comment()` (`:92`) applies the same checks, turns CRLF and a lone CR into
   LF and treats empty text as "remove".

## 5. Space and extent

Every entry is keyed by a `MemSpace` and a 16-bit logical address, the key of the
page-aware symbol table ([7.4](04-formats.md), "The symbol table the formats
fill"). It is always paged: the user's entry is shown only while that memory
supplies the address, exactly as a paged SLD label is ([7.1](01-which-memory.md)).
An entry whose address its memory can never supply (`ROM0 $5000`) is legal and
never shows. There is no extent: a label names one address.

## 6. Identity

None. The file is not tied to a program, which is why it is never attached
automatically.

## 7. What is skipped

The BOM, CR, `#` lines and blank lines above. Nothing else: an unknown record,
tag, version or escape refuses the file.

## 8. Excerpt

From the writer's row `JDP-01` (`test/debug/source_map_test.cpp:1319-1324`), the
bytes of a store inserted in a shuffled order:

```
jnext-annotations 1
label P04 $8000 Start
comment P04 $8000 Entry\npoint \\ here
label ROM0 $0010 rst_print
comment DIVRAM3 $2000 x
```

The functional row writes the same shape with `printf`
(`test/00regression/scripts/annotations-func.sh:25-27`).

## 9. How the store reaches a reader

`Impl` keeps three tables (`src/debug/debugger_impl.h`): `toolchain_symbols`, the
loaded layer ([7.3](03-loaders.md)); `annotations`, the user's store; and `names`,
their composition. `recompose_names()` (`src/debug/debugger_annotations.cpp:30-36`)
adds the user labels first and then `merge()`s the toolchain table, so by address
and by name the user wins, and the C-name rule of [7.4.3](04-3-z88dk-map.md) holds
inside the one table. `symbols()`, `lookup()` and `lookup_name()` return `names`,
so every consumer reads both layers with no code of its own.

It is called after every change of either layer: the toolchain writers
(`src/debug/debugger.cpp:270` `load_map()`, `:284` `clear_symbols()`;
`src/debug/debugger_source.cpp:409` the deferred-check drop, `:542` and `:549` the
hand SLD's labels, `:773` the program-load attach and its `bind()`, `:937`
`drop_sidecars()`, `:1035` and `:1041` the ring landing) and every annotation verb
(`src/debug/debugger_annotations.cpp:68`, `:77`, `:108`). The attachment cache
copies `toolchain_symbols`, never `names` (`src/debug/debugger_source.cpp:888`),
so a ring landing cannot bring back a user label that was removed, nor drop one
that was added.

The name rule against the toolchain layer is in `set_user_label()`
(`src/debug/debugger_annotations.cpp:51-61`): a name the loaded symbols give another
address, or another paged memory, is refused; the same address (pinning an unpaged
name to its page) is allowed. A project file is not refused for it, and a toolchain
loaded later is not either: the user layer then wins by name.

`save_annotations()` (`src/debug/debugger_annotations.cpp:133-154`) writes
`PATH.part` and renames it over `PATH`; `load_annotations()` (`:113`) checks
`source_file_problem()` first (not a regular file, over 64 MiB). The verbs touch no
machine state, so they are allowed while the machine runs, take no load generation
and leave `.jns`, the rewind ring and the attachment cache alone; each success bumps
`state_revision()` ([3.9.1](../03-subsystems/09-1-the-backend-api.md), GH #428).

## 10. The rows that pin it

- `source_map_test`: `UANN-01` to `UANN-13` (the store: keys, grammar, name rule,
  comment text, order, unnameable memories) and `JDP-01` to `JDP-09` (the file's
  bytes, the leniencies, every refusal with its line, atomicity, the round trip).
- `source_debug_test`: `SRCDBG-UANN-01` to `-20` (the two layers and the one reader
  by page, the name rule, the file through the backend, no lifetime across
  sidecars, hand loads, resets, state loads and ring landings, purity with its
  control, the state revision).
- `gdb_rsp_test` `GDB-ANN-01`, `script_events_test` `SCRIPT-ANN-01`,
  `cli_options_test` `CLI-ANN-01`, `debugger_verbs_test` `QMAP-10` (the four Load
  MAP boxes count the loaded file only), and the functional row `annotations-func`
  (headless, Qt and SDL).
