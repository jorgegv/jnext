# 7.4.2 sjasmplus `--sym` and `--exp`

Two plain files sjasmplus writes for a different purpose than debugging, which
JNEXT reads as a list of labels. They carry a name and a value and nothing else:
no source line, no page, no type.

## 1. The producer

sjasmplus options `--sym=FILE` (every label) and `--exp=FILE` (only the names a
source `EXPORT`s). The fixtures were made with v1.23.1 (commit `46aae3f`):

    sjasmplus --sym=equlabels.sym --exp=equlabels.exp equlabels.asm

An older sjasmplus wrote `--sym` differently. Up to v1.17.0
`CLabelTable::DumpSymbols()` wrote `NAME: equ 0x%08X`, with the keyword in lower
case and the labels in definition order; from v1.18.0 `--sym` shares the writer of
`--exp` (`test/fixtures/sjasmequ/README.md`, "`equlabels-v1.17.sym`").
`--exp` has always said `EQU`.

## 2. The writer

In `sjasmplus` v1.23.1:

- `WriteLabelEquValue()`, `sjasm/sjio.cpp:1433-1443`, writes `NAME: EQU 0x` and
  the value as 8 upper-case hex digits (`PrintHex32`).
- `--sym`: `CLabelTable::DumpSymbols()`, `sjasm/tables.cpp:610-624`, calls it for
  every symbol in name order, skipping a name that starts with a digit (a
  temporary label) and a keyword (`:619-620`; the call is at `:621`).
- `--exp`: `WriteExp()`, `sjasm/sjio.cpp:1445-1447`, called by the `EXPORT`
  directive (`sjasm/directives.cpp:1707`). A name exported twice is written twice
  (the fixture has `counter` at both of its values).
- Both files are opened in text mode (`SJ_fopen()`, `sjasm/support.cpp:47-50`), so
  a Windows build ends every line with CR LF.

## 3. The grammar

```
<name>: EQU 0x<hex>
```

`<name>` is the label as sjasmplus spells it: module-qualified (`gfx.draw`),
with a local label's parent (`main.loop`), a struct's fields (`pos.x`), and for
`--exp` the name the `EXPORT` gave (`draw`). The value is the full 32-bit
`aint`: a label or `EQU` above `0xFFFF` is written as it is, and a negative one
as `0xFFFFFFFF`.

## 4. The parser's rules

`parse_equ_line()` in `src/debug/symbol_table.cpp` is the one definition of an
EQU line. The loader and the first-line check that picks it both use it, so they
cannot disagree about what a line is.

1. A trailing CR is dropped.
2. The line must contain `": EQU 0x"` or `": equ 0x"`, exactly, found with
   `find()`; a separator at column 0 is not one. The two spellings are the
   current and the pre-v1.18.0 writers; no other case or spacing counts.
3. The name is everything before the separator and must contain no space, tab or
   `:`.
4. The value is the whole rest of the line: at least one hex digit and nothing
   else, fitting 32 bits (`std::from_chars`).

`SymbolTable::read_sjasmplus_equ()` then keeps the line when the value is at
most `0xFFFF` (so `0x00010000` and `0xFFFFFFFF` are skipped) and adds an
*unpaged* label. The first line at an address wins for the address and, apart,
the first for a name (`add_label()`); the count it returns is the lines
accepted, duplicates included.

Every entry is a label, whatever the source declared it with: a code label, an
`EQU`, a `DEFL`, a struct's size or a field offset. The file has no type to tell
them apart.

`SymbolTable::load_sjasmplus_equ(path, why)` first asks `source_file_problem()`
([7.3](03-loaders.md)): a file that is not a regular file, or is over the size cap, is
refused with that reason before it is opened. It then opens the file, returns `-1` if
it cannot (the table untouched), `clear()`s and reads.

`SymbolTable::load_z88dk_or_sjasmplus(path, why)` is what `--map` uses: it reads the
file once into memory (at most `SOURCE_FILE_MAX_BYTES` of what arrives; more is refused,
[7.3](03-loaders.md)), asks `parse_equ_line()` of its *first line*, and passes
the whole text to `read_sjasmplus_equ()` or `read_z88dk_map()`. The first line is
enough because a real `--sym` may open with a negative or oversized value, so
the test is "is it an EQU line", whatever its value.

## 5. Space and extent

None. A label from these files is unpaged: it names its address in every memory.
Two labels at `$C000` in different `DEVICE ZXSPECTRUMNEXT` pages
(`banked` and `other_bank` in the fixture) are therefore the same address with
two names: both names resolve to it, and the address is displayed by the first
one the file lists. For per-page
labels use the SLD ([7.4.1](04-1-sld.md)), which carries the page.

## 6. Identity

None.

## 7. What is skipped

Any line that is not an EQU line; values above `0xFFFF` and negative values;
temporary labels (sjasmplus does not write them).

## 8. Excerpt

`test/fixtures/sjasmequ/equlabels.sym`, written by sjasmplus v1.23.1 (all 18
lines, byte for byte; `NEG` and `PASTTOP` are the two the loader skips):

```
NEG: EQU 0xFFFFFFFF
PASTTOP: EQU 0x00010000
SCREEN: EQU 0x00004000
SCRSIZE: EQU 0x00001B00
TOPADDR: EQU 0x0000FFFF
banked: EQU 0x0000C000
counter: EQU 0x00000006
gfx.draw: EQU 0x00008016
gfx.draw.loop: EQU 0x00008018
main: EQU 0x00008000
main.loop: EQU 0x00008006
other_bank: EQU 0x0000C000
point: EQU 0x00000002
point.x: EQU 0x00000000
point.y: EQU 0x00000001
pos: EQU 0x00008014
pos.x: EQU 0x00008014
pos.y: EQU 0x00008015
```

The `--sym` of v1.17.0 for the same source is
`test/fixtures/sjasmequ/equlabels-v1.17.sym`; its first lines are
`SCREEN: equ 0x00004000` and `SCRSIZE: equ 0x00001B00` (lower-case `equ`, and
definition order, so `NEG` is no longer first). `equlabels-win.sym` is the
Windows build's: the same bytes with a CR before every line feed.

## 9. The rows

- `debugger_backend_test`: `SYM-19` to `SYM-32`, including two rows that glue
  `equlabels.sym` to a comment, a blank line or to a z88dk map to prove that the
  first line picks the loader.
- `script_events_test`: `SCRIPT-HOST-MAP-SJASM`, `-EXP`, `-V117`, `-CONTENT`.
- `debugger_verbs_test` (Qt): `QMAP-05`, `QMAP-06`.
- Functional: `script-map-pipe-func` feeds `equlabels.sym` to `--map` through a
  pipe.
- `test/fixtures/sjasmequ/README.md` has the commands, the commit and the
  `sha256` of every file.
