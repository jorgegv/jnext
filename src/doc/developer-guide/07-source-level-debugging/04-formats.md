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
