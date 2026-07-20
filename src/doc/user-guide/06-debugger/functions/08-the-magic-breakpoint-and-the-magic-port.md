# The magic breakpoint and the magic port

Two features for putting debugging hooks in your own code.

**The magic breakpoint** is an opcode that stops the emulator. JNEXT
recognises both conventions:

- `ED FF` — the ZEsarUX / Spectaculator form
- `DD 01` — the CSpect form

Enable it with `--magic-breakpoint`, or **Debug ▸ Magic Breakpoint** in the
emulator window. When one executes, the machine pauses and the debugger opens
itself if it was closed. When the feature is disabled — and on real hardware —
both sequences behave as two-byte no-ops, so you can leave them in the source.

In C with z88dk:

```c
#define MAGIC_BP()  __asm__("defb 0xED, 0xFF")
```

**The magic port** is a port that prints to the host. Writes to it are logged
to stderr, which makes it a `printf` for code that has nowhere to print. Enable
it with `--magic-port PORT` (the full 16-bit port address is decoded, so you can
pick something unused like `0xCAFE`) and choose the format with
`--magic-port-mode`:

| Mode | Output |
|---|---|
| `hex` | `41` — one hex byte per line (the default) |
| `dec` | `65` — one decimal byte per line |
| `ascii` | Raw characters |
| `line` | Buffered until CR or LF, then the whole line |

`line` is usually what you want for text messages. The magic port is
write-only: reads from it are not intercepted and follow normal port decoding.

```
jnext --magic-port 0xCAFE --magic-port-mode line --load mygame.nex
```

Working examples of both are in `demo/magic_bp_demo/` and
`demo/magic_port_demo/` in the JNEXT source tree.

---

For the full list of command-line options mentioned here, see **jnext**(1) or
[USAGE.md](https://github.com/jorgegv/jnext/blob/main/USAGE.md).
