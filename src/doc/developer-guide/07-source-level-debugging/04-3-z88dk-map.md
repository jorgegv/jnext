# 7.4.3 The z88dk map

The z88dk linker map is the one file that feeds both stores, by two separate
loads. Its `addr` lines are the symbols. A build with `-debug` adds two kinds of
marker symbol, `__C_LINE_` and `__ASM_LINE_`, whose names and addresses are the
source lines, and a third kind, `__CDBINFO__`, which JNEXT does not interpret.
The map carries no length for a line and no page for an address: both have to be
derived from the section a symbol lives in.

## 1. The producer

z88dk's assembler, `z80asm`, writes the map when `zcc` is given `-m`; the line
markers appear only with `-debug`. The fixtures were made with two installs, built
for the Next with `zcc +zxn ... -m --list -subtype=nex -o prog -create-app` (the
exact commands are the `+ zcc ...` lines of each `build.log`):

| Fixtures | Tree | Commit |
|---|---|---|
| `test/fixtures/z88dk/v2.4/` | `z88dk 2.4`, `~/src/spectrum/z88dk-original-2.4` | `4d530b6eb779ad0f2a1d13c0bb670cf717477501` (v2.4) |
| `test/fixtures/z88dk/master/` | `z88dk master`, `~/src/spectrum/z88dk-original-latest` | `39421a01cdfc9cb2011dbe3da38ef8fd1f5500b7` (v2.4-2695-g39421a01cd) |

A map built without `-debug` has symbols and no source lines. Several
`-debug` builds do not exist: 2.4's sdcc `-debug` fails to assemble
(`defc Gcounter0_00==.` syntax errors, z88dk issue 3172, fixed on master) and the
classic `+zxn` banked `-debug` is not producible (`Unknown option2
--hardware-keyboard`). `test/fixtures/z88dk/README.md` records both.

## 2. The producer's writer

In z88dk:

- **The map line.** `z80asm` `src/z80asm/src/c/symtab1.c:598-616` (master; the
  same line is `:641` in 2.4): `name` padded, then ` = $%04lX `, then
  `; type, scope, def, module, section, file:line`, then `strstrip()`. The `def`
  field is the word `def` or empty. An empty section shows as `, ,`, and a symbol
  with no file ends after the section: the trailing blanks are stripped
  (`const, public, def, , ,`).
- **`__C_LINE_<n>_<enc>`.** `Preproc::do_c_line()`, `src/z80asm/src/cpp/preproc.cpp`
  (master `:1721-1755`, the symbol built at `:1746-1747`; 2.4 `:1309-1310`). Only
  `if (g_options.debug)`. `<n>` is the directive's line operand (`C_LINE 17,...` gives `__C_LINE_17_`) and `<enc>`
  is `url_encode()` of the *string operand* (`main.c::main::1::3`): master
  `preproc.cpp:1706`, 2.4 `:1272`. It keeps `isalnum()` characters and writes
  every other byte as `_` and two lower-case hex digits.
- **One symbol per (line, scope).** `if (!find_local_symbol(...))`
  (master `preproc.cpp:1748`, 2.4 `:1311`): only the first occurrence of an
  identical name is defined. That is why sccz80 emits one `__C_LINE_18_` where
  `main.c.lis` has four `C_LINE 18,` (README finding G1).
- **`__ASM_LINE_<n>_<enc>`.** `asm_cond_LABEL()`, `src/z80asm/src/c/directives.c`
  (master `:76-95`; the encoder `url_encode()` is at `:35-53`; 2.4 `:88` and
  `:35`). Defined at a label, `if (option_debug() && !sfile_is_c_source())`, so a
  label in assembler that came from C gets no `ASM_LINE`. In sdcc builds these
  numbers are lines of the generated assembly, not of `main.c` (README G14).

## 3. The grammar

```
NAME = $HEX ; type, scope, def, module, section, location
```

| Field | Values |
|---|---|
| `type` | `addr` (a place in memory) or `const` (a number: a size, a section bound, a `defc`); the parser recognises only these two |
| `scope` | `local` or `public` in the fixtures; not interpreted |
| `def` | `def` or empty |
| `module` | The module name; for a `.c` file `<file>_c` (zcc heads each file with `MODULE <file, non-alphanumerics to _>`) |
| `section` | The section the symbol lives in, empty for a symbol in none |
| `location` | `file:line`, the rest of the line (it can hold commas and a TAB) |

The location in a `-debug` line record takes one of three forms: `file:line`
(assembler), `file::func::level::block:line` (sccz80, 80cc), and sdcc's
`file::x::<level><TAB>:line`, where the TAB marks a function it did not give.

## 4. The parser's rules

`parse_z88dk_map_line()` (`debug/z88dk_space.cpp`) is the one tokenizer. A line
needs an `=`, then a `;` after it; the value is `$` and hex digits up to 64 bits
(`__clib_malloc_heap_size = $FFFFFFFFFFFFFFFF` exists); the fields after the `;`
are split at the first five commas and the sixth is taken whole. A line that
does not parse is not a symbol record; the caller decides if that is fatal.

### The symbol half

`SymbolTable::read_z88dk_map()` (`debug/symbol_table.cpp`) reads the whole file
first, because the map is sorted by name and a section's head may come after its
symbols.

1. Names starting `__C_LINE_`, `__ASM_LINE_` or `__CDBINFO__` are skipped: they
   are not symbols, and sorted by name they would otherwise win the address of the
   function label (GH #352).
2. A `const` line goes to a table of constants (first wins, 64-bit; the page heads
   come from it) and, when the value fits 16 bits, becomes a **name-only value**
   (`add_value()`): `lookup_name()` and `@name` resolve it, but it never names an
   address, so a size or a section bound is never shown in a disassembly.
3. An `addr` line is placed in the second pass, in file order, first occurrence
   wins. A value above `0xFFFFFF` is skipped. Otherwise `z88dk_section_space()`
   (below) decides: a *paged* label; a label bound at load; an *unpaged* label
   (only if the value fits 16 bits); or nothing.
4. **C symbols (GH #424).** An `addr` symbol whose name starts with `_` and a
   second character other than `_`, and whose module ends in `_c`, is a C
   symbol: zcc's compilers write every C identifier with a leading `_`, and
   `__x` names are runtime internals. `add_label(..., c_symbol=true)` records its
   key under the C name (the name without the first `_`, `c_name_to_key_`) with the
   first C symbol winning. `find_name()` tries that after every exact name and
   constant, so a real `counter` always wins; `lookup()` and `nearest()` show the
   C name only when nothing else holds it and the label's own key is the one it
   resolves to (otherwise the assembler name, which is the case of a second C
   module's local `_util_inc`); `lookup_assembler()` keeps the assembler name for
   text that must assemble.

### The source half

`load_z88dk_source_map()` (`debug/z88dk_source_map.cpp`) is strict and reads the
file twice over: pass 1 collects line records, symbols, and the `__<group>_head`
and `_tail` constants; pass 2 places the line records in file order, so the first
refusal is the first bad line.

1. A line record whose section is empty is dropped: `= $0000 ; addr, local, ,
   main_c, ,` is "no code" (README G2; the 2.4 sccz80 `-debug` map has 80 of
   them).
2. A record whose location is line `0` is dropped (`is_line_zero()`): sccz80 and
   80cc write `C_LINE 0,"<file>::<func>::0::1"` on entering a file, and "no source
   line is numbered 0" (G22).
3. A value above `0xFFFFFF` refuses the half (`"address $... does not fit 24 bits"`).
4. The location is parsed (`parse_location()`: the text after the last `:` is the
   line, at least 1; a TAB before it marks an unknown function); an unparsable one
   refuses the half.
5. The space is decided by `z88dk_section_space()` (with the dot verdict, rule 5 of the space rules
   below); `Unpaged` and `Refused`
   refuse the half with a reason naming the map line; a *bind-at-load* record
   asks the `space_of` function the caller supplies (`Emulator::predict_fetch`),
   and an address that is not in a RAM page refuses it too
   (`"address $... of section '...' is not in a RAM page"`).
6. The records are grouped by `(space, address)` for ownership and extent, below.

## 5. Space and extent

### Space

`z88dk_section_space(section, value, page_head, dot)` (`debug/z88dk_space.cpp`) maps a
section name and a (possibly 24-bit) value to a `Z88dkSpace`. Page numbers are
0..223, banks 0..111; the numbers in a section name are 1 to 3 decimal digits.
The rules, in order of the section name:

1. **`PAGE_<n>`** and `<KIND>_PAGE_<n>` (KIND is `CODE`, `RODATA`, `DATA` or
   `BSS`): a section that starts in page `n` and may run into the next ones. The
   head is `z88dk_page_head()`: the *low 16 bits* of the constant `__PAGE_<n>_head`
   (newlib puts the page in bits 16-23, which are not read; GH #345). The page is
   `n + (((head & 0x1FFF) + (addr16 - head)) >> 13)`. With no such constant, an
   address below the head, or a result past page 223, the value is *unpaged*.
2. **`BANK_<n>`** and `<KIND>_<n>`, with `_L` or `_H`, and for `RODATA`/`DATA` also
   `<n>_align_<m>`: a 16 KB bank, which is the two pages `2n` and `2n + 1`. `_L` is
   page `2n`, `_H` is `2n + 1`, and a plain or aligned name uses bit 13 of the low
   16 bits of the address.
3. **An unnumbered section with a value above `0xFFFF`** is *refused*: nothing
   says which bank it is in.
4. **Any other section**, and a value of 16 bits: *bind at load*. The memory is the
   RAM page the MMU maps at that address with the program in memory, found with the
   same `predict_fetch` the source lookups use. In the symbol half this is the
   lenient form: `SymbolTable::bind(space_of)` gives the page only when
   `space_of(addr)` is a RAM `Page`; ROM, DivMMC and the other overlays leave the
   symbol unpaged; an empty function unbinds all. It runs at `load_map()`, at the
   sidecar `<stem>.map` (a tape's too, at the attach) and for the user's store at
   every program load.

A number above the limit of its family (223 for a page, 111 for a bank, 15 for
DivMMC) makes the value unpaged when it fits 16 bits and refused otherwise.

A `DIV_<n>` or `<KIND>_DIV_<n>` section (DivMMC memory) is unpaged with the reason "a DIV_n
bank is the one the program, or a dotn command's allocator, chooses when it runs": the bank is
the program's to choose, so the map cannot say it. The symbol half keeps a 16-bit symbol of it
as an unpaged label, and the source half refuses a line record in it.

5. **A dot command (GH #351).** NextZXOS copies a dot command's file into a DivMMC RAM bank and
   runs it from `$2000` there; the map names no bank. `z88dk_dot_command(consts, &why)`
   (`z88dk_space.cpp:136`) says whether a map is one: the constant `startupoffset` is `$0100` (dot),
   `$0200` (dotx) or `$0300` (dotn) and `__CODE_head` is `$2000`. No `startupoffset`, or another
   value, is not a dot and has no reason; a dot whose `__CODE_head` is not `$2000` is not a dot
   and its reason ("a dot command's CODE must start at $2000") refuses the source half
   (`dot_why`, `z88dk_source_map.cpp:201-203`), while the symbol half places the map as any other
   (`z88dk_dot_command()`, `symbol_table.cpp:82`, no reason asked). Both halves read the
   `__<s>_head`/`_tail` constants and `startupoffset` (`z88dk_source_map.cpp:187`; the symbol half keeps every constant). For a dot,
   `z88dk_section_space()` takes it as a last argument, after rules 1 to 3 (a value above `0xFFFF` in
   an unnumbered section is still refused) and before rule 4, for unnumbered sections only:
   - an unnumbered section at `$2000`-`$3FFF` is `DivMmcRam{kNextZxosDotBank}`, bank 2
     (`kNextZxosDotBank`, `z88dk_space.cpp:176-181`), whatever the MMU maps there when the map is loaded;
   - a dotx's other unnumbered sections are rule 4: its main part (`PROG.X`, `prog_MAIN.bin`
     at `CRT_ORG_MAIN` `$8000`) is main RAM;
   - in a dotn those sections, and every `PAGE_<n>` / `DIV_<n>` section, are *refused*
     (`refused_dotn()`: NextZXOS allocates those pages when the command runs, `__z_page_table`; the
     reason says to build it as a dotx to debug it);
   - a dot's or dotx's `PAGE`/`DIV` sections follow the rules above, and `DIV_<n>` stays unpaged.

   The bank is measured, not documented: booting NextZXOS in JNEXT and typing a dot, a dotx and a dotn,
   the file lands in bank 2, `E3` becomes `$82` (conmem and bank 2) and the CPU executes `$2000`
   (fixtures README, G24; `kNextZxosDotBank`, `z88dk_space.h:46`). `SourceMap::replace(..., dot)`
   remembers the verdict (`SourceMap::dot()`), which [7.6](06-lifetime.md) uses for the identity.

In a numbered section the bits of `value` above 15 are not read.

### Extent and ownership

A line record has a start and no end. `extent()` in `load_z88dk_source_map()`
gives a record the **first** of:

- the next line start in the same memory (any placed record, owners and aliases);
- the next `addr` symbol of **another module** in that memory;
- the section's `__<section>_tail` (the distance `tail - value`, when the tail is
  above the value and under `0x10000`);
- the start plus `0x400` (`kMaxLineSize`, DeZog's `MAX_C_LINE_SIZE`);
- the end of the 8 KB window, `(lo & 0xE000) + 0x2000`.

Records at the same `(memory, address)` are a group: the **highest line owns**
the range. A lower line of the group is an **alias** (it resolves through the
owner's range) if it is in the same module *and* the same function: both
functions are known and equal (the `::func::` part of the location), or, when
either is unknown (sdcc), when no `_`-prefixed symbol of that module at that
address is located at a later line of the same file (`later_function_entry()`: a
function whose entry is at that address and whose line is after the lower line's). A duplicate `(file, line)` in a group
is dropped. Any other lower line has **no code**: `resolve()` says so
([7.2](02-the-source-map.md)). In the fixture, `main.c:16` is the alias of
`main.c:17` at `$8C95` and `main.c:14` (in `square`) has no code at the `$8C92`
that `main.c:15` (in `main`) owns.

## 6. Identity

The section binaries z88dk writes beside the map, `<map stem>_<GROUP>.bin`, are the
identity. A group comes from a constant `__<GROUP>_head` and must have no
lower-case letter (`__code_compiler_head` is a section, not a group; on a
case-insensitive file system it would name the same file). The file must be a
regular file under the size cap, and unreadable ones refuse the half. For each
range of a section that is not data (`data*`, `bss*`, `smc*`, case-insensitive),
the binary that holds both its first and last byte, with both ends in the range's
memory, supplies a `SourceByteCheck { space, lo, bytes }`. A range no binary covers
is counted in `SourceMapLoadResult::unchecked`, and no binary at all means no
identity. This is the byte-check form of [7.2](02-the-source-map.md).

## 7. What is skipped

`__C_LINE_`, `__ASM_LINE_` and `__CDBINFO__` symbols in the symbol half; line
records in an empty section and line-0 records in the source half; any symbol
a space rule refuses or leaves unpaged beyond 16 bits; `const` values wider than 16
bits as names; lines that do not parse as map lines.

### `__CDBINFO__`

These records are z88dk's C debug information, in a different encoding from the
line markers:

- **Encoding.** Only `isalpha()` characters are kept; every other character,
  digits included, is `_` and two lower-case hex digits. Writers: sccz80
  `src/sccz80/cdbfile.c:13-24` (both trees, `defc __CDBINFO__<enc> = 1` at
  `:85-87`), 80cc `src/80cc/cdbfile.c:7-18` (master only). For sdcc, zcc folds the
  temporary `.adb` file into the assembly (`zsdcc_embed_adb()`, master
  `src/zcc/zcc.c:2041-2072`, called when `peepholeopt` is non-zero: zcc's `-O`,
  `:588`, `:1551`), with a variant encoder that
  also keeps whitespace (`zcc.c:2026-2038`). The value is always `1`, so the line
  is `const`.
- **Content.** After decoding `_hh`, a record starts `M:` (module), `F:`
  (function), `S:` (symbol) or `T:` (type). The reference decoder is z88dk's own
  `debug_add_info_encoded()` (`src/ticks/debug.c:656`), called from `src/ticks/syms.c:173`.
- **Duplicates.** `z80asm` ignores a second definition (`symtab1.c:117`).
- **In JNEXT.** The symbol loader skips them (`symbol_table.cpp`, rule 1 above) and
  so does the source half (`z88dk_source_map.cpp`, pass 1, `!starts_with(name,
  "__CDBINFO__")`). Nothing else in `src/` reads them. Only a test does: it decodes
  the names of the C identifiers to check the C names above (`Z88CN-01`).

## 8. Excerpts

From `test/fixtures/z88dk/v2.4/sccz80-debug/prog.map` (z88dk 2.4, built with `zcc
+zxn -clib=new -debug`; the file is trimmed to the fixture's modules, lines kept
verbatim; the number is the line in the file):

```
6:__ASM_LINE_10_asmfn_2easm       = $8D6C ; addr, local, , asmfn_asm, code_user, asmfn.asm:10
51:__C_LINE_0_main_2ec             = $0000 ; addr, local, , main_c, , main.c:0
71:__C_LINE_16_main_2ec_3a_3amain_3a_3a1_3a_3a3 = $8C95 ; addr, local, , main_c, code_compiler, main.c::main::1::3:16
74:__C_LINE_17_main_2ec_3a_3amain_3a_3a1_3a_3a3 = $8C95 ; addr, local, , main_c, code_compiler, main.c::main::1::3:17
77:__C_LINE_18_main_2ec_3a_3amain_3a_3a1_3a_3a3 = $8C9D ; addr, local, , main_c, code_compiler, main.c::main::1::3:18
269:_main                           = $8C92 ; addr, public, , main_c, code_compiler, main.c::main::0::2:15
```

Line 51 has an empty section (dropped); lines 71 and 74 share `$8C95`, so 17 owns the
range `[$8C95, $8C9D)` (the next line start is line 18 at `$8C9D`) and 16 is its
alias; `_main` is a C symbol (`main`) in module `main_c`.

From `test/fixtures/z88dk/master/sdcc-debug/prog.map` (z88dk master, `-clib=sdcc_iy
-debug`; line 68), the sdcc form, where `<TAB>` is a literal TAB:

```
__C_LINE_11_main_2ec_3a_3ax_3a_3a10000_3a_3a11_09 = $8C5F ; addr, local, , main_c, code_compiler, main.c::x::10000::11<TAB>:11
```

From `test/fixtures/z88dk/v2.4/sccz80-page20/prog.map` (a `PAGE_20` build; lines 4,
39, 113 and 134):

```
CRT_ORG_PAGE_20                 = $C000 ; const, local, , zxn_crt_asm_m4, , z88dk/lib/config/../..//libsrc/_DEVELOPMENT/target/zxn/zxn_crt.asm.m4:2210
__CDBINFO__S_3aLbanked_2ec_2ebanked_5fadd_24a_24_30_5f_30_24_30_28_7b_31_7dSC_3aU_29_2cB_2c_31_2c_36 = $0001 ; const, public, , banked_c, PAGE_20, banked.c::banked_add::1::1:9
__C_LINE_6_banked_2ec_3a_3abanked_5fadd_3a_3a0_3a_3a0 = $C000 ; addr, local, , banked_c, PAGE_20, banked.c::banked_add::0::0:6
__PAGE_20_head                  = $C000 ; const, public, def, , ,
```

The `PAGE_20` section with head `$C000` puts `$C000` in page 20. The `__CDBINFO__`
line decodes to `S:Lbanked.c.banked_add$a$0_0$0({1}SC:U),B,1,6`; note the digits
encoded as `_30`, `_31`, `_36`.

From `test/fixtures/z88dk/v2.4/sccz80-dot/prog.map` (a plain dot command, built with
`-subtype=dot`; lines 49, 63 and 262): `startupoffset` is the dot's signature, `__CODE_head` is
`$2000`, and the line record at `$2D5F`, in `code_compiler`, is in `$2000`-`$3FFF`: rule 5 puts it
in `DivMmcRam{2}`.

```
__CODE_head                     = $2000 ; const, public, def, , ,
__C_LINE_12_dot_2ec_3a_3amain_3a_3a0_3a_3a2 = $2D5F ; addr, local, , dot_c, code_compiler, dot.c::main::0::2:12
startupoffset                   = $0100 ; const, local, , zxn_crt_asm_m4, , z88dk/lib/config/../..//libsrc/_DEVELOPMENT/target/zxn/zxn_crt.asm.m4:9
```

## 9. The rows

- `z88dk_map_test`: `Z88MAP-*` (the source half, with the oracle values from the
  fixtures: owners, aliases, extents), `Z88SYM-*` (the symbol half and the
  binding), `Z88CN-*` (C names), `Z88DOT-*` (dot commands: the `dot`, `dotx`, `dotn`
  and master fixtures in DivMMC bank 2, detection by the map, the symbol half, the identity
  units, the `.lis` route, the dotn refusal, the extent bound of another module's symbol in
  bank 2), `MAPCAP-*` and `MAPBOM-*` (the size cap and the UTF-8 BOM of the symbol loaders).
- `z88dk_lis_test`: `Z88LIS-*` ([7.4.4](04-4-z88dk-listings.md)).
- `source_map_test`: `Z88FIX-01`/`02` (the fixture manifest).
- `source_debug_test`: `SRCDBG-Z88-*`, `SRCDBG-SYM-*`, `SRCDBG-TAPE-*`, `SRCDOT-*` (a dot command through
  the backend: breakpoint and steps in bank 2, judged at the entry, landings).
- `debugger_backend_test`: `SYM-14` to `SYM-18` (the `-debug` markers, on
  `test/fixtures/zdebugmap/`).
- Functional: `z88dk-source-map-func`, `z88dk-source-func`, `z88dk-tape-source-func`,
  `gdb-z88dk-func`, `z88dk-dot-source-func`, `z88dk-dot-source-rzx-func`.
