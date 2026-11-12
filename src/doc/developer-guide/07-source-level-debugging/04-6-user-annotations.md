# 7.4.6 The annotations project file (`.jdp`) and the user's labels

The user's own labels and comments are the one input of chapter 7 that
no toolchain writes. JNEXT defines the file itself: plain UTF-8 text, extension
`.jdp`, opened and saved by the user and never attached beside a program. It
fills a third store, `UserAnnotations`, which sits beside the toolchain's
`SymbolTable` and wins over it when the two disagree.

## 1. The producer

JNEXT. The Qt Map menu opens and saves it (section 10); `--annotations FILE`
loads one at start-up (`main.cpp` stores `annotations_file` at `src/main.cpp:881` and copies
it to the configuration at `src/main.cpp:2194`, and `ScriptHost::start()` loads it (`annotations_file`,
`src/script/script_host.cpp:201-215`), after `--map` and before the scripts, so a
script's `@name` of the file resolves). `replace_annotations()` is the
primitive an import builds on.

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
(`AnnotationKey`, `src/debug/user_annotations.h:33-37`) compares the memory kind (the
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

1. **Skipped:** a UTF-8 BOM at byte 0 of line 1 only (the `"\xEF\xBB\xBF"` compare, `src/debug/annotations_file.cpp:90`), a trailing CR
   (CRLF), a line whose first character is `#`, a blank line.
2. **The header** is the first other line and must be exactly `jnext-annotations 1`
   (`kHeader`, `src/debug/annotations_file.cpp:94`); `jnext-annotations N` for another canonical N reads `version N: this
   jnext reads version 1` (the `"version "` message, `:100`); anything else is `not a jnext annotations
   file`.
3. **A record** with an unknown kind is refused (the `"unknown record '"` message, `src/debug/annotations_file.cpp:117`; forward compatibility is
   a version bump, not skipping). `<SPACE>` goes through `parse_space_tag()`
   (case-insensitive, strict digits, the hardware's ranges) or the file is
   refused (`"unknown memory '"`, `:122`); `CPU` is refused (`"memory CPU:"`, `:124`), since a user entry names a memory.
   `<ADDR>` is `$` and exactly four hex digits, either case (`parse_addr()`, `:46`;
   `"bad address '"`, `:127`). A label has exactly four fields (`"a label record has exactly"`, `:130`). A second label or comment for
   a key is refused (`"a second label"`, `:132`; `"a second comment"`, `:141`), a CR inside a text is refused
   (`"a carriage return"`, `:143`), and
   `unescape()` (`:33`) accepts only `\\` and `\n` (a final lone `\` is refused,
   `"lone backslash"`, `:37`).
4. **Names and text** are checked by the store, not the adapter, so the API and the
   file cannot disagree: `UserAnnotations::valid_label_name()`
   (`src/debug/user_annotations.cpp:29`: components `[A-Za-z_][A-Za-z0-9_]*`
   joined by single dots, exactly what the DSL's `@name` lexes,
   `lex_symbol()`, `src/script/lexer.cpp:243-261`), `valid_utf8()` (`src/debug/user_annotations.cpp:39`: no overlong form, no
   surrogate, nothing past U+10FFFF) and the uniqueness of a name
   (`set_label()`, `src/debug/user_annotations.cpp:62-80`).
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

From the writer's row `JDP-01` (`test/debug/source_map_test.cpp:1344-1350`), the
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
`names` is assigned in place, so the `const SymbolTable*` the Disassembly, Call Stack
and Breakpoints panels hold (`set_symbol_table(&dbg_.symbols())`) stays valid.

`recompose_names()` is called after every change of either layer: the toolchain writers
(`recompose_names` at `src/debug/debugger.cpp:270` in load_map and `:284` in clear_symbols; in `src/debug/debugger_source.cpp` at `:409` the deferred-check drop, `:547` and `:554` the
hand SLD's labels, `:797` the program-load attach and its bind, `:961`
drop_sidecars, `:1064` and `:1070` the ring landing) and every verb that changes a label
(`recompose_names` at `src/debug/debugger_annotations.cpp:71` in set_user_label, `:80` in remove_user_label, `:116` in replace_annotations; `set_user_comment()` changes no name and does not). The attachment cache
copies `toolchain_symbols`, never `names` (`src/debug/debugger_source.cpp:912`),
so a ring landing cannot bring back a user label that was removed, nor drop one
that was added.

The name rule against the toolchain layer is in `set_user_label()`
(the `toolchain_symbols.find_name()` test, `src/debug/debugger_annotations.cpp:53-61`): a name the loaded symbols give another
address, or another paged memory, is refused; the same address (pinning an unpaged
name to its page) is allowed. A project file is not refused for it, and a toolchain
loaded later is not either: the user layer then wins by name.

`save_annotations()` (`src/debug/debugger_annotations.cpp:141-162`) writes
`PATH.part` and renames it over `PATH`; `load_annotations()` (`src/debug/debugger_annotations.cpp:121`) checks
`source_file_problem()` first (not a regular file, over 64 MiB). The verbs touch no
machine state, so they are allowed while the machine runs, take no load generation
and leave `.jns`, the rewind ring and the attachment cache alone; each success bumps
`state_revision()` ([3.9.1](../03-subsystems/09-1-the-backend-api.md)). A verb that
changes nothing (a rename to the same name, an identical or already-normalised comment, clearing a
comment that is not there, replacing or loading the same store) bumps neither revision: the early
`return Result::Ok` of `set_user_label()` (`Ok`, `src/debug/debugger_annotations.cpp:69`), of
`set_user_comment()` (`Ok`, `:96`) and of `replace_annotations()` (`Ok`, `:113`), pinned by `SRCDBG-UANN-21`, so
an unchanged dialog never raises the unsaved-changes question.

## 10. The Qt debugger

The same two views edit the store and draw it; nothing here changes what the store holds.

- **One menu builder.** `add_annotation_actions()` (`src/debugger/annotation_menu.cpp:77`) appends, behind a
  separator, **Add Label…** / **Rename Label…**, **Remove Label** and **Add Comment…** / **Edit Comment…** to a
  code view's context menu. The Disassembly calls it with the one place of the line, its address and the
  `entry.space` it was drawn with (in `DisasmPanel::contextMenuEvent()`, `add_annotation_actions` is called at `src/debugger/disasm_panel.cpp:986`); the
  Source tab calls it with the line's `st.starts` (in `SourcePanel::fill_context_menu()`, `add_annotation_actions` is called at
  `src/debugger/source_panel.cpp:1356`), so a line that compiled to several places gets a submenu per entry,
  a place not mapped now stays editable (the key does not depend on the mapping), and a line with no code
  has the entries disabled with the tab's *no code at* tooltip. Both views therefore offer the same words in
  the same order (`QANN-11`).
- **What the entries do.** `edit_label()` (`annotation_menu.cpp:35`) is a `QInputDialog::getText` that
  calls `Debugger::set_user_label()`; `edit_comment()` (`:53`) is a `QInputDialog::getMultiLineText`
  that calls `set_user_comment()` (empty text removes); `remove_label()` (`:70`) calls
  `remove_user_label()`. A refused name or text is a `QMessageBox::warning` with the backend's reason and
  changes nothing (`QANN-09`). **Rename** on a toolchain label creates a user label that shadows it;
  **Remove** acts on user labels only, so on a toolchain-only label it is disabled and its tooltip names the
  file the toolchain layer was loaded from (`toolchain_symbols().loaded_file()` in `add_annotation_actions()`, `toolchain_note`,
  `annotation_menu.cpp:95-98`; `state_of()`, `:24`, only says whether a label is there). The panels hold a `const Debugger*` for reading; the writes go
  through the non-const pointer `DebuggerManager` hands them with `set_annotation_backend()`
  (`src/debugger/debugger_manager.cpp:392` and `:396`), and each panel's `annotations_changed()` signal makes the
  manager refresh the panels.
- **The Disassembly draws it.** `label_column_shown()` (`disasm_panel.cpp:1016`) is true while the symbol table
  (the composed reader, section 9) holds any label; then the **Label** column sits between the BP column and the
  mnemonic and every x after it moves by `label_width()` (16 characters). The columns run Mem, Addr, BP column,
  [Label], mnemonic, opcode bytes, `; comment`; `breakpoint_x()` (`:1023`) is the BP column's left edge,
  `mnemonic_x()` (`:1025`) the mnemonic's (`breakpoint_x() + 20 + 4`, plus `label_width()` with a label),
  `mnemonic_width()` (`:1030`) the mnemonic column's 20 characters, and `bytes_x(line)` (`:1035`) where that line's
  bytes start: after the column, or after the mnemonic itself plus a gap when that mnemonic is wider, so a long
  line pushes only its own bytes. The comment sits 100 px (`BYTES_WIDTH`) after the bytes. The painter and the tests
  read these accessors (`QDCOL-01`, `QDCOL-03`, `QDCOL-04`; with no label the mnemonic is at 172, `QANN-01`).
  `line_label()` (`:1056`) is the name the reader gives the line's
  own address in its own memory, shortened with `...`; `line_comment()` (`:1066`) is `; ` and the comment's first
  line, plus ` ...` when it has more. Both are what the painter draws and what the tests read. The copy
  formats do not include them (`QANN-04`). The Source tab does not draw comments (it shows the user's own
  file).
- **The Map menu.** `DebuggerWindow` adds **Open Annotations…**, **Save Annotations** and **Save Annotations As…**
  (`connect`, `src/debugger/debugger_window.cpp:899-903`), connected to `DebuggerManager::on_open_annotations()`
  (`debugger_manager.cpp:784`), `on_save_annotations()` (`:760`) and `on_save_annotations_as()` (`:750`); Save with no
  current file is Save As, which appends `.jdp` to a name without a suffix. Failures are warnings with the
  backend's `why` (an Open names the line).
- **Unsaved changes (D11).** `DebuggerManager` keeps `saved_revision_` (`debugger_manager.h`), the backend's
  `annotations_revision()` at the last Open, Save or `--annotations`; `annotations_unsaved()` compares them.
  `confirm_discard_annotations()` (`debugger_manager.cpp:766`) asks Save / Discard / Cancel when they differ, and is
  asked by `on_open_annotations()` before it opens and by `MainWindow::closeEvent()`
  (`confirm_discard_annotations`, `src/gui/main_window.cpp:3868`) before anything else, which ignores the event on Cancel or a failed Save (File ▸ Quit goes
  through `close()`, which then returns false). An unattended close (`unattended_`, the delayed automatic exit and
  SIGINT/SIGTERM, tested at `src/gui/main_window.cpp:3865`) asks nothing: it logs that the unsaved changes were
  discarded and quits (`QG-ANN-05`). With nothing changed there is no dialog. `set_annotations_file()`
  (`debugger_manager.cpp:731`), called by `QtApp` for `--annotations` (`set_annotations_file`, `src/gui/qt_app.cpp:395`), makes that file
  the Save target and the starting point of "unsaved".
- **Settings.** None: the store has no Preferences field, so `config_keys.cpp` has no row; the CLI is
  `--annotations FILE`.

## 11. The rows that pin it

- `source_map_test`: `UANN-01` to `UANN-13` (the store: keys, grammar, name rule,
  comment text, order, unnameable memories) and `JDP-01` to `JDP-09` (the file's
  bytes, the leniencies, every refusal with its line, atomicity, the round trip).
- `source_debug_test`: `SRCDBG-UANN-01` to `-21` (the two layers and the one reader
  by page, the name rule, the file through the backend, no lifetime across
  sidecars, hand loads, resets, state loads and ring landings, purity with its
  control, the state revision).
- `gdb_rsp_test` `GDB-ANN-01`, `script_events_test` `SCRIPT-ANN-01`,
  `cli_options_test` `CLI-ANN-01`, `debugger_verbs_test` `QMAP-10` (the four Load
  MAP boxes count the loaded file only), and the functional row `annotations-func`
  (headless, Qt and SDL).
- The Qt side: `debugger_panels_test` `QANN-01` to `-04` and `-06` (the column, the comment, the copies, the
  address field), `debugger_verbs_test` `QANN-05` (Call Stack and Breakpoints), `QANN-12` and `-13` (the Map
  menu, the unsaved question), `debugger_source_panel_test` `QANN-07` to `-11` (the menus, per place, the
  dialogs, a refused name, parity), `debugger_quit_gate_test` `QG-ANN-01`/`-02` and `quit_cleanup_test`
  `QG-ANN-03` (the quit question) (SRCDBG-UANN-21: a no-op verb bumps nothing, in the backend list above).
