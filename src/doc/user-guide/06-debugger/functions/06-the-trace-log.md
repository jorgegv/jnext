# The trace log

The trace log records the machine state *before* every instruction executed: the
master cycle count, `PC`, all main and alternate registers, `IX`, `IY`, `SP`,
the word on top of the stack, `I`, `R`, the interrupt mode and both interrupt
flip-flops, the memory page mapped in each of the eight slots and which of them
held ROM, the memory the instruction was fetched from, the decoded flags and the raw
opcode bytes. It is a ring buffer of 10 000
entries, so it always holds the last 10 000 instructions.

- **F2**, the toolbar `F2: Trace` button (its indicator turns green), or
  **Debug ▸ Trace ▸ Enable Trace** switches it on.
- **Debug ▸ Trace ▸ Clear Trace** empties it.
- **F3**, the `F3: Export Trace` button, or **Debug ▸ Trace ▸ Export Trace…**
  writes it to a text file, one instruction per line. `ROM=` is a hex mask of
  the slots that held ROM: bit 0 is slot 0, so `ROM=03` means slots 0 and 1.
  `FETCH=` names the memory the instruction itself was fetched from, which a
  slot's page number cannot say when an overlay answers; it is one of the tags
  in the table below.
- `--trace` on the command line starts with it enabled.

**F2** and **F3** are the defaults and can be rebound — see
[Changing the keys](09-changing-the-keys.md).

| `FETCH=` tag | Memory |
|---|---|
| `P0E` | RAM page 0x0E (two hex digits, 00 to DF) |
| `ROM0` to `ROM3` | ROM image 0 to 3 |
| `BOOT` | boot ROM |
| `MFROM` / `MFRAM` | Multiface ROM / RAM |
| `DIVROM` | DivMMC ROM |
| `DIVRAM3` | DivMMC RAM bank 0 to 15 (decimal) |
| `ALTROM0` / `ALTROM1` | alternate ROM: 0 = 128K, 1 = 48K |
| `L2P12` | Layer 2 read mapping, Layer 2 page 0x12 |
| `CFG17` | config mode, NR 0x04 bank 0x17 |
| `FLOAT` | floating bus (no memory answers) |

The tags are the same everywhere the debugger names a memory, so a tag in a
trace line can be pasted into a script or a project file. The spelling is
stable: capital letters and digits only, never changed between versions.

An interrupt or NMI fetches no instruction; its line names the memory the
interrupted instruction will come from.

The trace is what makes "how did we get *here*?" answerable after a breakpoint
fires. It is also a prerequisite for stepping backwards.
