# 7.5 Beyond screenshots

Screenshots are the general-purpose check, but they are not the only one.

- **`--magic-port`** turns a chosen I/O port into a debug channel: your
  program writes to it, JNEXT prints to stderr. With `--magic-port-mode line`
  it buffers until a CR/LF, which makes it a `printf` your test can grep. JNEXT
  uses exactly this in `test/00regression/scripts/magic-port-func.sh`.
- **`--magic-breakpoint`** lets a program halt the emulator from inside itself
  (see chapter 6).
- **`--delayed-snapshot`** saves machine state rather than a picture, at a
  frame you choose — useful when what you want to assert is memory, not
  pixels.
- **`--wav-record`** captures the mixed audio, including headless, so a sound
  change can be regression-tested too.
- **RZX** (`--rzx-record` / `--rzx-play`) replays a whole recorded session
  input-for-input.

The full option list is in [`jnext(1)`](https://github.com/jorgegv/jnext/blob/main/USAGE.md).
