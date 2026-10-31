# Memory coverage

Memory coverage records, for every byte of every memory the program touches,
whether it was **executed**, **read** or **written**. It answers "did this code
ever run?", "what is data and what is code?" and "what overwrote this buffer?"
without a single breakpoint, and it is kept per *physical* memory: the same
address in two different RAM pages, a ROM, the DivMMC ROM and a Layer 2 mapping
are different bytes with different marks.

## What is recorded

| Mark | Colour | Meaning |
|---|---|---|
| **X** | green | the first opcode byte of an instruction that ran |
| **R** | blue | any other memory read: operand bytes, data, a DMA read |
| **W** | orange-red | a write that landed, from the CPU or the DMA |

- An instruction is marked once, at its **first** opcode byte. Its operand
  bytes show as **R**, which is what tells code from data afterwards: a run of
  X bytes with R bytes between them is code, a run of R bytes alone is data.
- A write that the machine **drops** (a write to ROM, to the Multiface or DivMMC
  ROM, to an inactive Layer 2 page) marks nothing: the byte did not change.
- The ULA, Layer 2 and sprite fetches, the Copper, the debugger's own reads
  (panels, **Memory** edits, disassembly, the trace) and file loads are not the
  program's memory accesses and never mark anything.
- It is a **set**, not a count. Going back in time (rewind, Step Back) removes
  nothing and the replay marks only what it already marked, so nothing is
  counted twice. A reset, a state load and a cold boot keep the marks too.

## Switching it on

- **Debug ▸ Coverage ▸ Enable Coverage** switches recording on; switching it
  off **clears** it. **Debug ▸ Coverage ▸ Clear Coverage** empties it and
  recording goes on.
- `--coverage` on the command line starts with it recording, from the first
  instruction. (With the SDL frontend or `--headless` it records but there is no
  viewer; it logs one summary line at exit.)
- It costs a little speed while it is on, and nothing while it is off.

## Where it shows

- **Disassembly** — a green bar left of the address on every line whose
  instruction ran. The bar follows what the CPU would *fetch* at that address
  now, so after the DivMMC pages itself in at `RST 8` the line at `$0008` shows
  the DivMMC ROM's mark, not the ROM's.
- **Memory** — under each byte a three-part underline, **X | R | W** left to
  right, so a byte that was executed and read shows two parts. A "Coverage: X R
  W" key appears in the panel's top bar. The `CPU View` follows what the CPU
  reads, a `Slot` view the page mapped there, and `Page...` that page.
- **Source** — every line of the shown file that ran has a pale green
  background; the current line keeps its own highlight.

Colours adapt to a dark desktop theme. Marks are visible only while the machine
is paused, like everything else in the debugger.
