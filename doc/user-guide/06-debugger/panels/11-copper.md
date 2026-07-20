# Copper

![Copper panel](../../img/debugger-copper.png)

Whether the Copper is running, its program counter and mode, and 64 decoded
instructions centred on that PC. Each row shows the address, the raw 16-bit
word, and the decoded instruction:

- `WAIT` with the `v` and `h` position it is waiting for
- `MOVE` as `NR xx = yy`
- `NOP` (`0000`) and `HALT` (`FFFF`)

The row at the Copper PC is highlighted yellow.
