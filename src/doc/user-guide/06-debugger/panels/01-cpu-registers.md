# CPU registers

![CPU registers and MMU](../../img/debugger-cpu-mmu.png)

The full Z80 state at the paused instruction:

- `AF` `BC` `DE` `HL` and the alternate set `AF'` `BC'` `DE'` `HL'`
- `IX` `IY` `SP` `PC`
- `I`, `R`, `IFF` (shown as `IFF1/IFF2`) and the interrupt mode `IM`
- **Flags** — `S Z H PV N C`, green and bold when set, grey when clear
- **State** — `Running`, or `HALTED` in red when the CPU is in a `HALT`
- **Screen** — which bank the ULA is displaying (`Bank 5` or `Bank 7`), plus the
  Timex mode when one is active (`Alt`, `HiCol`, `HiRes`)
- **T-states** — the CPU T-states executed since the last stop. After a single
  step it is what that instruction took; after a run to a breakpoint, a Step Over
  or a Pause, what the whole run took. Tick **Accumulate** to add the stops up
  instead: the total starts at 0 when you tick it and grows across steps and
  runs until you untick it. The count starts again at 0 at the next stop whenever time was moved without
  running: Step Back, Frame Back, the rewind bar, Reverse Continue, a snapshot or
  state load (also by a remote debugger), a program load or a hard reset; and when
  the debugger window opens. A soft reset does not restart it. At 7, 14 or 28 MHz each instruction counts its own T-states at the
  speed in force (a NOP is 4 at any speed).

## Editing

While the machine is paused you can change any value here in place:

- **Double-click** a register value, type the new value and press **Enter**.
  **Escape**, or clicking anywhere else, cancels; so does resuming the machine.
  A right-click offers **Edit Value...** as well.
- The 16-bit registers take a hexadecimal number (`1234`, `$1234`, `0x1234`) or a
  symbol, as the address boxes do: a number is read first, and `@name` always means
  the symbol. Typing `main` into `PC` sets it to the label's address.
- `I` and `R` take one or two hex digits, `IFF` takes `IFF1/IFF2` as `1/0`, and
  `IM` takes 0, 1 or 2.
- A single click on a flag (`S Z H PV N C`) toggles it. The two undocumented bits
  of F are kept; change them through `AF`.
- Entering the value a register already has changes nothing. A value that does not
  parse is reported in the status bar and nothing is written.
- Editing `PC` takes the CPU out of a `HALT`: **State** then shows `Running`.
- An edit is refused, with a status-bar message, while an RZX recording is being
  made or played.
- Editing does not change the **T-states** value: it counts executed time, and an
  edit executes nothing; the next step counts the instruction at the new `PC`.
- With the rewind buffer on, an edit discards the frames after it, like any other
  change from the debugger. **Step Back** across the edit is then refused;
  **Frame Back** to the start of the frame undoes it. See
  [When a rewind is refused](../functions/07-backward-execution-rewind.md#when-a-rewind-is-refused).

While the machine runs the panel is greyed and nothing can be edited; to change
memory or a NextREG use the Memory or NextREG panels.
