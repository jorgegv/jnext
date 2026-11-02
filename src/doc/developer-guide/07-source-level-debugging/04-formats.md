# 7.4 File formats and records

Five inputs feed the stores. Each gets a page, laid out the same way so that a
reader can compare formats and a maintainer adding one ([7.8](08-tests-and-new-producers.md))
knows what to write.

| Page | Input | Produced by | Fills |
|---|---|---|---|
| [7.4.1 sjasmplus SLD](04-1-sld.md) | `--sld` file, version 1 | sjasmplus | source map, labels, identity (by convention) |
| [7.4.2 sjasmplus `--sym` / `--exp`](04-2-sjasmplus-equ.md) | `NAME: EQU 0x0000ABCD` lines | sjasmplus | labels |
| [7.4.3 The z88dk map](04-3-z88dk-map.md) | `<stem>.map`, with `-debug` line records | z88dk (`z80asm`) | labels and source map, identity from section binaries |
| [7.4.4 z88dk listings](04-4-z88dk-listings.md) | `<file>.lis` | z88dk (`z80asm --list`) | source map (replaces a module's map records), embedded texts |
| [7.4.5 NextBuild `Memory.txt`](04-5-nextbuild-memory-txt.md) | `8000: ._Main` lines | Boriel ZX Basic `zxbc -M`, via NextBuild | labels |

Boriel ZX Basic writes **no** SLD. Its only output jnext reads is `Memory.txt`
(7.4.5), a list of labels with no source positions, so a BASIC program built
with NextBuild has symbols and no source lines unless an SLD is produced by some
other tool.

## The symbol table the formats fill

There is one `SymbolTable` (`src/debug/symbol_table.*`), owned by the backend. The Qt panels, the DSL's
`@name`, the GDB server's `monitor sym` and `--map` all read it, so a symbol loaded by one client shows
in every other. How the formats differ is only in what they put in it.

- **Keyed by memory and address (GH #375).** `add_label(name, addr, space, bind_at_load, c_symbol)`
  stores a label that is *paged* (assembled for one `FetchSource` such as `Page{20}`, naming the address
  only while that memory supplies it) or *unpaged* (naming it in every memory: the sjasmplus EQU,
  Simple and NextBuild loaders make these). There is no address-only lookup:
  `lookup(space, addr)` finds the paged label with exactly that key, else the unpaged one at `addr`
  (`MemSpace::cpu()` is "no memory known" and reaches unpaged labels only). The backend's
  `lookup(addr)` asks with `source_page(addr)`, the same identity the source map and the gutter use
  ([7.1](01-which-memory.md)), so every consumer (the Disassembly, the Call Stack, the Breakpoints,
  `monitor sym ADDR`) names an address by what is mapped there now; `disasm_text::symbol_namer()` is
  that rule as a function for the panels. The first label of a key wins and, apart, the first of a
  name.
- **A name** resolves to an address and, through `find_name()` and `resolve_key()`, to the memory it
  was assembled for; the breakpoint dialogs keep that page. `SymbolTable::resolve()` turns an address
  field's text into an address: hex first (`4000`, `$4000`, `0x4000`), else a symbol; `@name` always
  means the symbol, so a program variable named `FE` is still reachable.
- **Name-only values.** `add_value()` stores a size, an offset or a section bound: `find_name()` and
  `lookup_name()` resolve it, but it never names an address, so a constant is never shown in a
  disassembly (a z88dk `; const` line, an SLD `+struct_def` record).
- **Binding at load.** A z88dk symbol of a section that names no page is stored with `bind_at_load`
  and no memory; `SymbolTable::bind(space_of)` gives it the RAM page `space_of(addr)` names (ROM,
  DivMMC and the other overlays leave it unpaged; an empty function unbinds it). The backend binds
  with the one function the source half uses, `emu.predict_fetch`: at `load_map()` (every success), at
  the sidecar `<stem>.map` (a tape included, at the attach), and for the user's store at every
  program load ([7.4.3](04-3-z88dk-map.md), rule 4).
- **Combining.** `SymbolTable::merge(other)` is the one way to combine two tables: it adds the other's
  labels whose key is still free, its names and name-only values not yet taken, so the first table wins
  throughout (row `SYMPG-12`). The sidecar store is assembled with it ([7.3](03-loaders.md)).
- **C names.** A z88dk C symbol also has its C name (`c_name_to_key_`, `shown_name()`,
  `lookup_assembler()`; [7.4.3](04-3-z88dk-map.md)); `merge()` carries the C names for the labels the
  receiving table does not own, and `clear()` empties them (rows `Z88CN-01` to `-16`).
- **Rows.** `SYMPG-*` and `SLDL-*` (`source_map_test`: pages, SLD labels), `Z88SYM-01` to `-09`
  and `SRCDBG-SYM-18` to `-22` (the z88dk placement), `SYM-19` to `-32` (`debugger_backend_test`:
  the EQU and `--map` loaders).

## The layout of a format page

1. **The producer**, its version and the command.
2. **The producer's writer**, cited by tree and `file:line`, so a change in the
   tool can be checked against it.
3. **The grammar** of a record.
4. **The parser's rules**, each with the jnext function that applies it.
5. **Space and extent**: which memory a record belongs to and how many bytes it
   covers. These two decide whether a format can be supported at all.
6. **Identity**, if the format can carry a program identity.
7. **What is skipped**, and why.
8. **A real excerpt** from a committed fixture, with its path. The fixtures are
   normalised (install paths, temporary names and compile dates are replaced:
   `test/fixtures/z88dk/README.md`, "Normalisation and trimming"). Where no
   public producer writes a record and no fixture exists, the excerpt is
   labelled *synthetic* and says which test it comes from.
9. **The rows that pin it.**

## Which citations are where

Producer sources are cited as `<tree> <path>:<line>`:

- `sjasmplus` is `~/src/spectrum/sjasmplus` at v1.23.1, commit `46aae3f`.
- `z88dk 2.4` is `~/src/spectrum/z88dk-original-2.4` at `4d530b6eb7` (v2.4) and
  `z88dk master` is `~/src/spectrum/z88dk-original-latest` at `39421a01cd`
  (`v2.4-2695-g39421a01cd`). These are the two installs the z88dk fixtures were
  made with (`test/fixtures/z88dk/PROVENANCE` files).
- Boriel ZX Basic and NextBuild are read on GitHub: `boriel-basic/zxbasic` at
  `29f0b2f0ce` and `em00k/NextBuild` at `364b2d13c5`, both on 2026-11-01. The
  URLs are in `doc/REFERENCES.md`.
