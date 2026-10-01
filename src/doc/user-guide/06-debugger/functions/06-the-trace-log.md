# The trace log

The trace log records the machine state *before* every instruction executed: the
master cycle count, `PC`, all main and alternate registers, `IX`, `IY`, `SP`,
the word on top of the stack, `I`, `R`, the interrupt mode and both interrupt
flip-flops, the memory page mapped in each of the eight slots and which of them
held ROM, the decoded flags and the raw opcode bytes. It is a ring buffer of 10 000
entries, so it always holds the last 10 000 instructions.

- **F2**, the toolbar `F2: Trace` button (its indicator turns green), or
  **Debug ▸ Trace ▸ Enable Trace** switches it on.
- **Debug ▸ Trace ▸ Clear Trace** empties it.
- **F3**, the `F3: Export Trace` button, or **Debug ▸ Trace ▸ Export Trace…**
  writes it to a text file, one instruction per line. `ROM=` is a hex mask of
  the slots that held ROM: bit 0 is slot 0, so `ROM=03` means slots 0 and 1.
- `--trace` on the command line starts with it enabled.

**F2** and **F3** are the defaults and can be rebound — see
[Changing the keys](09-changing-the-keys.md).

The trace is what makes "how did we get *here*?" answerable after a breakpoint
fires. It is also a prerequisite for stepping backwards.
