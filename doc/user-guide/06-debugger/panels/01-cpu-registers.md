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

All values are read-only here; to change memory or a NextREG use the Memory or
NextREG panels.
