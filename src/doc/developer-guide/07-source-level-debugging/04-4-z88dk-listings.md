# 7.4.4 z88dk listings

A z88dk map says where a C line *starts*. A listing says what every line of a
source file compiled to, byte by byte. JNEXT reads the listings that sit beside
a map to fill what the map cannot: every assembly line (the map has only the
first `__ASM_LINE_` of a label), sdcc's C lines on z88dk 2.4 (whose map has no
`__C_LINE_` at all), and the text of the generated start-up code. The listing
adapter is `src/debug/z88dk_lis.*`; `z88dk_source_map.cpp` calls it.

## 1. The producer

`z80asm --list`, requested through `zcc --list`. It writes `<file>.lis` for every
user source (`main.c.lis`, `asmfn.asm.lis`) and `<program>.lis` for zcc's
start-up code (`prog.lis`, 12 to 18 thousand lines; the fixtures keep it for
four builds only). A listing is the assembler's view: addresses are
**section-relative**, and a module is listed under its own name, not the path the
map uses (`MODULE util_c` in `game/util.c.lis`, module `game_util_c` in the map;
README finding G18).

## 2. The producer's writer

`z80asm` `src/z80asm/src/cpp/lstfile.cpp` (`list_open()`: 2.4 `src/z80asm/src/cpp/lstfile.cpp:202`, master
`src/z80asm/src/cpp/lstfile.cpp:213`). The layout is `LstFile::out_line()` (master `src/z80asm/src/cpp/lstfile.cpp:131-177`, 2.4 `src/z80asm/src/cpp/lstfile.cpp:126-168`) with the widths at
`src/z80asm/src/cpp/lstfile.cpp:26-29` (identical in both trees): a 6-wide line-number column, 2 blanks, a
4-digit hex address (the section-relative or phased pc), 2 blanks, 16 characters
for at most 8 bytes, 2 blanks, and the source text from column 32. Bytes beyond the
eighth continue on rows that repeat only the bytes. `LstFile::out_filename()`
(master `src/z80asm/src/cpp/lstfile.cpp:122-129`, 2.4 `src/z80asm/src/cpp/lstfile.cpp:117-124`) writes `<name>:` on its own line whenever the file changes, which
includes the files `z80asm` is reading through `INCLUDE`, a `C_LINE`'s file operand
and a macro's source (the `.m4` text of the start-up code).

## 3. The grammar

```
<file>:                                 a file header, no leading blank
nnnnnn  AAAA  bb bb ...   <text>        a row: line number, address, bytes, text
                    bb bb               continuation of the bytes of the row above
nnnnnn                    <text>        a row with no bytes (a label, a directive, a comment)
```

The line-number column holds the line of the file the header names (for a C
module, the C line a `C_LINE` set). Row texts that matter here:

- `MODULE name`, `LINE n,"file"`, `C_LINE n,"file::func::level::block"` and
  `SECTION name`, `PHASE`, `DEPHASE`, `REPT`...`ENDR`, `MACRO`...`ENDM`.
- For sdcc with `--c-code-in-asm`, comments `;file:line: text`.
- Labels, either `name:` or sccz80's `.name`.

## 4. The parser's rules

### Reading a listing: `parse_z88dk_listing()`

A strict tokenizer. It refuses, naming the physical line (`"line N: ..."`), a file
that is not a z80asm listing: a header not ending in `:`, a row before the first
header, a line-number column holding something other than digits and blanks,
columns 6-7 not blank, a malformed address or byte field, and a byte continuation
with no row to continue. The result is a list of rows (`phys`, `col`, `header`,
`has_addr`, `addr`, `bytes`, `text`) and the headers.

### Placing it: `place_z88dk_listing()`

It interprets one parsed listing against the map, seen through a
`Z88dkMapView` (the map's symbols and line records, its `__<group>_head`/`_tail`
constants, a `place` function that is `z88dk_section_space()`, and the section
binaries' bytes). In order:

1. **Macros.** A `MACRO` definition (`MACRO name`, `name MACRO`, `name: MACRO`, to
   `ENDM`) is listed once, at its definition, and not assembled there, so its rows
   are ignored in the walk (README G20). A macro whose body, transitively, switches
   section (`SECTION`, `PHASE`, `DEPHASE`), or a `SECTION`/`PHASE` inside a `REPT`
   body, makes the bytes of its expansion impossible to attribute: the **whole
   listing is dropped** with a note.
2. **The module.** The last `MODULE` directive wins (z80asm keeps the last one;
   zcc's own `MODULE zxn_crt0_asm` comes first, README G19). With none, the stem of
   the first `LINE` operand, else the listing's own file name without `.lis`. If
   the caller named the modules this listing may declare (`accepted`, built from the
   map's module symbols: a file `game/util.c` is `game_util_c` in the map and `util_c`
   in its listing), a listing of another is dropped; the module must also be one the
   map has.
3. **The mode.** If any `C_LINE` directive is present the listing is `CLine` (sccz80,
   80cc, and master's sdcc): the source lines are the `C_LINE` markers. Otherwise, if
   the `LINE` operand names a `.c` file, `Comment` (sdcc on 2.4): the `;file:line:`
   comments. Otherwise `Asm`: the listing's own file and line-number column.
   A `Comment` listing without a single such comment is dropped ("it has no line
   information (sdcc writes it with --c-code-in-asm)").
4. **The walk.** Per row: `SECTION` sets the current section, `PHASE`/`DEPHASE` the
   phased flag. A byte row outside `PHASE` must sit at the section's running pc
   (the address column continues where the section left off, "a section's listed
   addresses start at 0 and are continuous", README G21); a break means the walk lost
   the section (a `SECTION` in a false `IF` branch, for one), and the listing is
   **dropped** (`"expected $X, the listing has $Y; the listing cannot be followed"`).
   A byte row is an *item*: its section, address, byte count, whether its first token
   is a Z80/Z80N mnemonic, and its source location (in `Asm` mode its own file and
   column; otherwise the current `C_LINE` or comment marker of its section).
5. **Anchors and the offset.** A label row of the walk is an *anchor* when the map
   has an `addr` symbol of that name in this module, in the same section, whose map
   location is *this very row* (`<header>:<line-number column>`). The anchor gives
   `offset = map value - listed address` for its section. All anchors of one section
   piece must agree, or the listing is dropped ("this is not the listing of this
   build"). A piece with no anchor falls back to `__<section>_head` when
   `tail - head` equals the piece's byte count (`p.pc`, `z88dk_lis.cpp:603-612`). A smaller piece is
   placed by its bytes (5b below). A piece with none of these, and anything inside `PHASE`,
   cannot be placed; those rows are counted and reported as "N listed lines could not be
   placed".
   **5b. A label-less piece smaller than its section is placed by its bytes** (GH #426 F4;
   design 9.5). Another module can put code in the same section: the banking driver shares
   `code_crt_init` with the CRT in the banked-CRT fixtures, so the CRT's own piece has no label in
   the map and is smaller than the section. `place_z88dk_listing()` (`z88dk_lis.cpp:396`) tries
   every start `c` from `__<s>_head` to `__<s>_tail` minus the piece's size and excludes it when,
   at any *checked row*, the section binary holds a byte other than the listing's first byte. A
   checked row is the first byte of a mnemonic row, outside `PHASE`, in a non-data section; the
   rest of an instruction is never compared, because the linker patches operands and z80asm never
   patches an opcode, so a listing of this build is never excluded at its true start. The byte at
   `c + row address` comes from `view.byte_at(value, space)`, where `place()` gave the space; it is
   *unknown* (`nullopt`) when no section binary covers the byte, **when two or more do** (overlay
   sections at one ORG: `bin_for(..., &more)`, `z88dk_source_map.cpp:288`, and the `byte_at` lambda, `:358-363`), and when `place()` fails (a ROM area: the memory is not a RAM
   page, so no byte can be read). The outcome is one of three:
   - **no start survives** (`survivors`, `z88dk_lis.cpp:655`; the search loop over `survivors`, `z88dk_lis.cpp:614-664`): the listing is dropped, with "section '<s>': its label-less lines match no
     place in the section binary; this is not the listing of this build" (row `Z88LIS-42`);
   - **exactly one survives and every checked byte was known:** the piece starts there (`Z88LIS-36`
     and `-37` on the banked-CRT fixtures: the CRT piece is placed and the driver's bytes after it
     are left without a line; `Z88LIS-38` with a patched `defw` and a `PHASE` row);
   - **several survive, or the one left has an unknown byte:** nothing is guessed. The piece's rows
     are not placed and are counted in the "could not be placed ... do not fit exactly one place in
     the section binary" note, while the listing's other pieces stay (`Z88LIS-39`, `-40`, `-41`,
     `-45`, `-46`; a data section has no checked rows, so both starts survive, `Z88LIS-43`).

   The loop stops at the second survivor (`survivors < 2`). This rule replaced "the size must be
   equal", which left 17 lines of the CRT unplaced when the map and the listing disagreed by one byte
   (`Z88LIS-19`, re-specified).
6. **Placement and the first-byte check.** An item's map value is `offset + address`
   (a value above `0xFFFFFF` drops the listing); `place()` supplies the memory and
   16-bit address, or a reason that drops the listing. For a mnemonic row in a
   non-data section the section binary's byte at that value must equal the listing's
   first byte, else the listing is dropped ("holds $X in the section binary, the
   listing says $Y"): operands are module-relative in a listing, the opcode is not.
   The check abstains where the byte is unknown (no binary covers it, or two do): a labelled
   piece over two overlapping section binaries is kept (`Z88LIS-47`).
   An item with a location gets a `SourceRange` of `[lo, min(lo + bytes, 8K window
   end))`. One range per byte *row*, so the several rows of one line are adjacent
   ranges of the same line, which `resolve()` collapses into one start
   ([7.2](02-the-source-map.md), "continuation").
7. **Zero-width lines.** A marker with no bytes of its own resolves through the
   next byte row of its section, as an alias. In `Asm` mode a marker is a label row
   with no bytes that the map names (an anchor); in the C modes a marker directly followed by bytes owns them
   (no alias), and one followed by more markers aliases the next byte row, except
   that when a function-entry label (one starting `_`) intervenes only the last marker
   before it does. One alias per `(file, line)`.
8. **The cross-check.** Every map line record of this module is compared with the
   listing: a `__C_LINE_` record with where the first `C_LINE` of the same operand
   string sits, an `__ASM_LINE_` record with the first anchor label row at that
   `header:line`. It agrees when the section matches and `position + offset ==
   value`. Disagreements do **not** drop the listing; they are counted and the
   first three named in one note ("the listings disagree with the map at N of M line
   records (first: ...); the listings were used").
9. **Embedded text.** Every row under a header ending `.m4`, with a line-number
   column from 1 to 1,000,000, is kept as that line of that file; `lines[i]` is line
   `i + 1`.

### Choosing listings: `load_z88dk_source_map(..., Z88dkListings::Beside)`

The candidates are `<map stem>.lis` (the start-up code; it may be a listing of any
module of the map) and `<file>.lis` for every source file named by the location
of the map's module symbols and line records, path relative to the map. A
candidate that is not a regular file is ignored without a word. One that fails
`source_file_problem()` or `parse_z88dk_listing()`, or is dropped by a rule above,
adds a note and the module keeps the map's records. A second listing of an
already-listed module is ignored with a note. A module that does get a listing
**takes its lines from it**: its map records are erased and the listing's ranges
and aliases follow the map's. The notes are logged one Warn each, the listings
counted in the attach line ("and N listings"), and carried to the frontends in
`SourceMapLoad::listing_note` (hand load) or the log (sidecar); a map with no line
record can still supply ranges from a listing.

## 5. Space and extent

**Space.** The same as the map: `z88dk_section_space(section, value, page_head)`
([7.4.3](04-3-z88dk-map.md)) on `offset + address`, through `Z88dkMapView::place`.
**Extent.** The bytes of the row: `[lo, lo + n)`, cut at the 8 KB window end. There
is no guess, unlike a map record's extent.

## 6. Identity

A listing carries none. Its ranges join the map's when the section binaries'
identity is built, so they are byte-checked like the map's own
([7.4.3](04-3-z88dk-map.md)).

## 7. What is skipped

Rows inside a macro definition; the first-byte check of rows that do not start with
a mnemonic (data) and of data sections; bytes inside `PHASE`; rows of a section piece with no offset (no anchor, no head fallback, and no single verified start by its bytes); and, per listing, every
reason in rules 1-8 that drops it whole.

## 8. Excerpts

`test/fixtures/z88dk/v2.4/sccz80-debug/main.c.lis` (z88dk 2.4, sccz80 `-debug`;
file lines 1-4 and 611-622, tabs as written). The header, then `main`: the first
byte row, `3b` at section address `001d`, is line 17:

```
/tmp/tmpXXXXXXXX.asm:
     1                          MODULE main_c
     2                          LINE 0, "main.c"
main.c:
```
```
    14                          	C_LINE	15,"main.c::main::0::2"
main.c::main::0::2:
    15                          ._main
    15  001a  cd0000            	call	l_debug_push_frame
    15                          	C_LINE	15,"main.c::main::0::2"
    15                          	C_LINE	16,"main.c::main::1::3"
main.c::main::1::3:
    16                          	C_LINE	16,"main.c::main::1::3"
    16                          	C_LINE	17,"main.c::main::1::3"
    17                          	C_LINE	17,"main.c::main::1::3"
    17  001d  3b                	dec	sp
    17  001e  210000            	ld	hl,0	;const
```

`_main` (a label on line 15, section address `001a`) is the anchor: the map says
`_main = $8C92` in section `code_compiler`, whose head is `$8C78`, so the offset is
`$8C78` and `3b` at `001d` is `$8C95`, which is `__C_LINE_17_...` in the map
(7.4.3's excerpt). Line 16 has `C_LINE` markers and no bytes of its own, so it
resolves through line 17's first byte row.

`test/fixtures/z88dk/v2.4/sdcc-SO2/main.c.lis` (z88dk 2.4, sdcc `-SO2
--c-code-in-asm`; file lines 288-300), the `Comment` mode, where the C line is a
`;file:line:` comment:

```
   283                          ;main.c:14: int main(void)
   284                          ;	---------------------------------
   285                          ; Function main
   286                          ; ---------------------------------
   287                          _main:
   288                          ;main.c:17: counter = 0;
   289  0023  af                	xor	a, a
   290  0024  320000            	ld	(_counter),a
   291                          ;main.c:18: for (i = 0; i < 8; i++) {
   292  0027  0e00              	ld	c,0x00
   293                          l_main_00107:
   294                          ;main.c:19: table[i] = square(i);
   295  0029  210100            	ld	hl,_table
```

The `prog.lis` of the same build has a header `z88dk/lib/config/../..//libsrc/_DEVELOPMENT/target/zxn/zxn_crt.asm.m4:`
(the `zxn_crt` header, `test/fixtures/z88dk/v2.4/sdcc-SO2/prog.lis:4`): the text kept as an embedded file, so `zxn_crt.asm.m4:9462`
resolves to `$8000` (row `Z88LIS-10`).

## 9. The rows

- `z88dk_lis_test`: `Z88LIS-*`, with the oracle values in the fixtures'
  README (for instance `util.h:7` has three starts in `v2.4/sdcc-SO2`).
- `source_map_test`: `SRCTXT-01` (embedded texts in the store).
- `source_debug_test`: `SRCDBG-LIS-*`.
- Functional: `z88dk-source-func`.
