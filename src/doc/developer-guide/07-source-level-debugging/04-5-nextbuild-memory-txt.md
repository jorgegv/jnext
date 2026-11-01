# 7.4.5 NextBuild `Memory.txt`

`Memory.txt` is the label list that the Boriel ZX Basic compiler can write and
that NextBuild asks for on every build. It is the only thing from the BASIC
toolchain that JNEXT reads. It has an address and a name per line and nothing
else, so a program built this way has symbols and **no source lines**.

Boriel ZX Basic writes no SLD, and no tool in this chain writes the `||program.*`
identity records either. Where a BASIC project does have an SLD, some other tool
produced it.

## 1. The producer

The Boriel ZX Basic compiler, `zxbc`, option `-M FILE` / `--mmap FILE`, "Generate
label memory map" (`boriel-basic/zxbasic`, `src/zxbc/args_parser.py:151`, read on
`main` at `29f0b2f0ce`, 2026-11-01). NextBuild (`em00k/NextBuild`, read at
`364b2d13c5`) runs `zxbc` in-process from `Scripts/nextbuild.py`, after
`os.chdir()` to the program's source folder (`:70`), with the argument pair
`'-M','Memory.txt'` in each of its three compile calls: headerless (`:400`), tape
(`:404`) and the default `.bin` (`:406`). The call that writes assembly
(`'-A'`, `:409`) has no `-M`. So the file lands as `Memory.txt` in the source
folder, next to the `.nex` NextBuild goes on to make.

The URLs of both are in `doc/REFERENCES.md`.

## 2. The producer's writer

`zxbc.main()` writes the file at `src/zxbc/zxbc.py:230-233`:
`f.write(asmparse.MEMORY.memory_map)`. The property is
`src/zxbasm/memory.py:301-306`:

```python
return "\n".join(sorted("%04X: %s" % (x.value, x.name) for x in self.global_labels.values() if x.is_address))
```

The global labels that are addresses, `HHHH: name`, sorted as strings (so by
address, since the hex is upper-case and four digits), joined with line feeds and
no trailing one. The leading `.` of a name is how the assembler spells the root
namespace; the compiler prefixes every user identifier with `_`.

## 3. The grammar

```
HHHH: <label>
```

`HHHH` is the 16-bit address in hex; `<label>` is the assembler label, such as
`._Main`, or a runtime label such as `.core.__LABEL`.

## 4. The parser's rules

`SymbolTable::load_nextbuild_memory(path, why)` in `src/debug/symbol_table.cpp`:

1. `source_file_problem()` first (not a regular file, or over 64 MiB, returns `-1`
   with `why` set and the table untouched); a file that does not open sets `why`
   to `"could not open Memory.txt file"`. Only then is the table `clear()`ed.
2. Per line: the text before the **first** `:` is the address, trimmed, and must be
   a whole field of hex digits that fits 16 bits (`parse_hex16()`); the rest,
   trimmed (blanks, CR and LF), is the raw label and must not be empty. Any other
   line is skipped.
3. **The shown name** drops a leading `.`, and then one leading `_` if the next
   character is not `_`: `._Main` is shown as `Main`. A name that begins `__` keeps
   its underscores (`.core.__LABEL` is shown as `core.__LABEL`). A name that is
   empty after that is skipped.
4. `add_label(display, addr, nullopt)`: an unpaged label (first wins, by address
   and apart by name), then `add_name()` for the raw text and for the raw text
   without its leading `.`, so `Main`, `._Main` and `_Main` all resolve to the
   address. The return value counts the lines accepted.

The count is what `load_program_sidecars()` logs: `Loaded N NextBuild symbols from
'<file>'`. The same loader serves `MapFormat::NextBuild`
(`Debugger::load_map()`).

## 5. Space and extent

None. A label from `Memory.txt` is unpaged and names its address in every
memory. The file carries no page, so a BASIC program in a banked page needs its
SLD (if there is one) or a reader that knows the bank. There is no extent: this
file feeds only the symbol store.

## 6. Identity

None.

## 7. What is skipped

Lines with no `:`, an address field that is not entirely hex digits or whose value
does not fit 16 bits, an empty label, and a label that is empty once its leading
`.` is dropped.

## 8. Excerpt

**Synthetic.** There is no committed `Memory.txt` produced by `zxbc`, and no
Boriel install on the development machine to make one. The format above is read
from the producer's code, and the tests use hand-written files in that format:

`test/debug/source_debug_test.cpp:476`, beside a `game.nex`:

```
8000: ._Main
9000: ._Sub
```

and `test/00regression/scripts/nextbuild-symbols-func.sh:24`, which writes the file
with `printf` for the functional row:

```
8000: ._Main
8123: ._PlayerTick
```

## 9. The rows

- `source_map_test`: `NBSYM-*` (the adapter: the cap, the refusals and the names).
- `source_debug_test`: `SRCDBG-SIDE-*` (the `<stem>.Memory.txt` and `Memory.txt`
  sidecars beside a `.nex`).
- Functional: `nextbuild-symbols-func` runs every frontend (headless, Qt, SDL) and
  asks `monitor sym` of the loaded name.
