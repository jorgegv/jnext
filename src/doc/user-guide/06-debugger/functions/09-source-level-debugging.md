# Source-level debugging

JNEXT's source debugger combines two independent sidecars:

- `Memory.txt` or a Z88DK MAP file supplies symbol names for logical addresses.
- An sjasmplus SLD v1 file maps instructions to source files, lines, columns and
  optionally physical 8K pages.

Use **Map > Load Symbols** and **Map > Load SLD Source Map…** to load them
manually. Loading a NEX also searches beside it for `game.Memory.txt` before
`Memory.txt`, and for `game.sld` or `game.sld.txt`.

An SLD may carry program-identity bytes. JNEXT checks those bytes against
memory so it does not silently attach a sidecar from another build. An
automatic mismatch is rejected. A manual mismatch asks whether to continue,
because a running program may legitimately have changed writable bytes inside
its original binary range.

Source lookup, source stepping, source-gutter breakpoints and tracked call
frames keep the physical 8K page when the SLD supplies one. This prevents a
bank switch from confusing two routines that share a logical address. Plain
numeric or symbol execution breakpoints remain logical wildcards; data
watchpoints and watches also follow the CPU's current logical mapping.

The source step family is documented under
[Run, pause, and the step family](01-run-pause-and-the-step-family.md).
