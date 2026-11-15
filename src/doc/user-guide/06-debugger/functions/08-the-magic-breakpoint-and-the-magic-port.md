# The magic breakpoint and the magic port

Two features for putting debugging hooks in your own code.

**The magic breakpoint** is an opcode that stops the emulator. JNEXT
recognises three conventions:

- `ED FF` — the ZEsarUX / Spectaculator form
- `FD 00` — the CSpect form since 2.19.9.1, and what the sjasmplus `break`
  instruction assembles to (sjasmplus 1.21.0 and later, with `--zxnext=cspect`)
- `DD 01` — the older CSpect and sjasmplus form

Enable it with `--magic-breakpoint`, or **Debug ▸ Magic Breakpoint** in the
emulator window; the menu item is ticked whenever it is enabled, whichever way
you did it. When one executes, the machine pauses and the debugger opens
itself if it was closed; execution then carries on right after its two bytes.

When the feature is disabled — and on real hardware — the forms differ.
`ED FF` and `FD 00` are two-byte no-ops, so you can leave them in the source.
`DD 01` is not:
the `DD` prefix is ignored and `01 nn nn` runs as `LD BC,nn`, which takes the
two bytes after it as its operand and overwrites `BC`.

In C with z88dk:

```c
#define MAGIC_BP()  __asm__("defb 0xED, 0xFF")
```

In sjasmplus assembly:

```
        OPT --zxnext=cspect
        break                   ; FD 00: stops here under --magic-breakpoint
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
