# POK trainers: banked lines below 0xC000

Why `jnext::pok::Session::locate()` (`src/platform/pok_trainers.cpp`) treats a
bank 0-7 line at an address below 0xC000 as a poke into the CURRENT memory map
and not as "byte N of that RAM bank", which is what Fuse and ZEsarUX do. Written
so that a future maintainer can revisit the decision with the evidence in hand.
External links: [REFERENCES.md](../REFERENCES.md). GH #381.

## The line format, in one paragraph

A `.pok` poke line is `M|Z bank address value original`, decimal. `address` is
16384-65535. `bank` bits 0-2 are a 128K RAM bank, bit 3 means "ignore the bank"
(always set, so 8, for 48K games; 62987 of the 63112 pokes in the corpus below).
So bank 8-15 = poke what the CPU sees at `address`. Bank 0-7 is the case at issue.
The format notes say nothing about a bank 0-7 with an address below 0xC000.

## The 128K memory map

Only 0xC000-0xFFFF is paged on a 128K/+3/Pentagon/Next in normal mode: port
0x7FFD loads `MMU6 = bank*2`, `MMU7 = bank*2+1` (`zxnext.vhd:4679-4680`, bank
bits from `:3763`). MMU2-5 keep pages 0A 0B 04 05 (`:4613-4616` reset values, kept
by `:4635-4660` outside +3 special paging and Profi mode). So a bank other than 5
and 2 can never appear below 0xC000 in a normal 128K program, and "bank 0 at
0xAEFC" cannot be a CPU-view address of bank 0. The only way the CPU sees a
bank below 0xC000 is +3 special paging (port 0x1FFD) or Profi mapping.

## The two rules

- **Rule A, Fuse / ZEsarUX / ESPeccy.** Bank 0-7: write `RAM[bank][address & 0x3FFF]`
  for EVERY address. Fuse `pokefinder/pokemem.c:407-413` (apply), `:438-441`
  (undo), `:355` (read for the "already poked" check): `if (bank == 8)` CPU poke,
  else `address &= 0x3fff; RAM[bank][address] = value`. ZEsarUX `src/utils.c`
  `util_poke`, 128K/+2/+3 branch: `if (banco > 7)` CPU poke, else
  `direccion & 16383` into `ram_mem_table[banco]`. ESPeccy `src/Cheat.cpp`:
  `ram[bank & 7][addr]`, with no mask at all, so an address over 0x3FFF indexes
  past the 16K bank (a defect). ZEsarUX on a 48K refuses every banked poke; Fuse
  writes the unmapped bank.
- **Rule D2, jnext.** Bank 0-7 and `address >= 0xC000`: NR page
  `2*bank + ((address >> 13) & 1)`, offset `address & 0x1FFF`, whatever is paged
  (identical to Rule A for those addresses). Below 0xC000 the bank field is ignored
  and the line pokes the page the map shows there when the trainer is applied. On a
  48K only bank 0 exists (port 0x7FFD is not decoded, NR 0x82 = 0xC0: `emulator.cpp`
  power-on, `zxnext.vhd:2399, :2593`), so a bank 1-7 line at 0xC000 and above is
  refused. Below 0xC000 the bank is ignored on every machine, a 48K included: a
  bank-3 line at 0x8003 pokes the map byte there and is not refused.

The rules differ only for bank 0-7 lines below 0xC000.

## Worked example: Starglider 128K, "Infinite Missiles (Bank#0)"

`M 0 44796 2 0` / `Z 0 60367 0 0` (44796 = 0xAEFC, 60367 = 0xEBCF).

| line | Rule A writes | Rule D2 writes |
|---|---|---|
| `M 0 44796 2 0` | bank 0 offset 0x2EFC (47 -> 02) | CPU 0xAEFC = bank 2 offset 0x2EFC (01 -> 02) |
| `Z 0 60367 0 0` | bank 0 offset 0x2BCF (3D -> 00) | bank 0 offset 0x2BCF (3D -> 00) |

What the game has there (real 128K tape loaded in jnext, snapshot at the title
screen, bank dump):

- bank 0 around 0x2BCF: `LD A,(0x5628); AND A; JR Z; DEC A; LD (0x5628),A;
  CALL 0x9CBE`. The `Z` line NOPs the `DEC A`: the counter never goes down. Both
  rules agree.
- CPU 0xAEFB: `LD A,1; LD (0x5628),A; CALL 0x9CBE`, the same counter's reset. The
  `M` line is the operand of that `LD A,1`, so the author meant the CPU address
  (reset to 2). Reading it as a bank offset changes an unrelated byte of bank 0
  (the byte at 0x2EFC is not part of this routine) and leaves the reset at 1.
- A bank diff of two snapshots, with and without the trainer: Rule A gives exactly
  two bytes, bank 0 0x2BCF and bank 0 0x2EFC; Rule D2 gives bank 0 0x2BCF and
  bank 2 0x2EFC.

The visible screen is identical for the first 1500 frames of play either way; the
evidence is the code, not a screen. Reading of the author's intent: a
Multiface-style POKE list where "Bank#0" says which bank to page in for the line
at 0xC000 and above, and the lower line is simply a CPU address.

## Corpus evidence

ESPeccy MegaPack v1.0 POK Update (archive.org, see REFERENCES.md): 8710 `.pok`
files, 63112 pokes, 43767 trainers. 62987 pokes use bank 8; 125 are banked (bank 0
x36, 1 x4, 3 x70, 4 x1, 6 x10, 7 x4). Of those, **22 are below 0xC000**, and all
22 sit in a trainer that also pokes 0xC000+ with the same bank: the bank is the
paging state for the high lines, the low line is a CPU address. No banked trainer
mixes in bank-8 pokes. No bank above 8, no address below 16384. The 22 are the
Starglider trainers (a `[128K]` and a `[+3]` variant, 2 lines each, several tape
and disk copies) and Yie Ar Kung-Fu's "Don't add extra lives"
(`M 3 44732 0 0` / `Z 3 50109 0 0`).

Yie Ar Kung-Fu is **inconclusive**: it is a 48K game file naming bank 3, which a
48K lacks, and on the 48K tape checked, both addresses (0xAEBC, 0xC3BD) hold
bitmap-looking bytes, so neither reading can be confirmed. So the evidence is one
trainer (Starglider), which favours D2, and nothing for Rule A.

## Did ZEsarUX copy Fuse?

Facts, each checked in the sources:

- Fuse added POK files in 1.1.0, released 2013-05-19 (`ChangeLog`: "Add support
  for POK files (Sergio Baldovi)"). ZEsarUX added them in 3.1, 2 October 2015
  (`src/Changelog`: "Added loading pokes from .POK files" and "Added Pokeing to
  128k machine (bank, address, value)").
- Fuse, `pokefinder/pokemem.c:407-413`: `RAM[bank][address & 0x3fff]`; on a 48K it
  writes `RAM[bank]` anyway. ZEsarUX, `util_poke` (`src/utils.c:14188`):
  `direccion & 16383` into `ram_mem_table[banco]` on a 128K-class machine, but it
  REFUSES any banked line (bank < 8) on a 48K. The code is structurally different,
  so it is not a code copy.
- The World of Spectrum `POKformat.txt` defines bits 0-2 as the bank and bit 3 as
  "ignore the bank", says the POKEs are "the famous multiface POKEs", and is silent
  on how an address below 0xC000 maps into a bank. ESPeccy indexes the bank
  unmasked (a third variant).

The orchestrator's opinion (2026-10-18, not a finding): copying cannot be shown,
and the code says it was not copied. Converging on the same rule is unsurprising
either way: reading "bank + address" as "bank + offset within the bank" is the
natural reading, and Fuse was the open-source reference. Neither emulator is
wrong against the spec text, which is silent; but the spec's own origin (Multiface
pokes, which a user typed as CPU addresses with the needed page in the 0xC000
slot) fits Rule D2. The difference went unnoticed because it touches 22 of 63112
corpus pokes, all in trainers that also poke 0xC000+, and the Starglider case
still "works" under Rule A (the second line alone gives infinite missiles). So
Rule A is best seen as a reasonable but untested interpretation that two
emulators share, not as a bug ZEsarUX inherited.

## Decision

2026-10-18, owner, after seeing both rules and the Starglider evidence: **Rule D2**.
History: the plan (D2) chose it from the VHDL and the corpus pattern; the owner
first asked for Rule A "as Fuse and ZEsarUX do" (Q2); the real-game check above
showed Rule A misses the intended byte of the one verifiable trainer, and the
owner reverted to D2. Compatibility with Fuse/ZEsarUX is deliberately given up
for those 22 lines. Rows: POK-33, POK-34, POK-57 in
`test/platform/pok_trainers_test.cpp`.

## What would make us revisit

- A trainer, verified against a game's code (or in a running game), whose bank 0-7
  line below 0xC000 is only right as a bank offset (Rule A).
- A POK format document or a maintained tool that defines such a line as a bank
  offset.
- A user report where the Fuse/ZEsarUX reading makes a real trainer work and ours
  does not.
- Going back is `p.banked() && p.address >= 0xC000` in `Session::locate()` and the
  three rows above; the first design is in git history (`1a6593fe4` is the Rule A
  commit on the GH #381 branch history).
