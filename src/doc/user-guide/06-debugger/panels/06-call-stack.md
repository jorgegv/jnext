# Call stack

The chain of calls that led to the current instruction: depth number, the type
(`CALL`, `RST`, `INT` or `NMI`), the caller address, and the target. Most
recent call first. With a MAP file loaded the target column shows the symbol
name instead of the address. With a source map loaded, **Called from** shows
the source line each call was made from
([Source-level debugging](../functions/14-source-level-debugging.md)); for an
`INT` or `NMI` frame that is the line the interrupt interrupted. Code fetched
from a ROM or an overlay has a line only if the source map names that memory.

A frame ends when a `RET` returns to where its call (or interrupt) came from;
any other `RET` ends the frames whose return address it has passed on the stack;
an esxDOS call JNEXT services itself ends its `RST` frame. A routine that
discards its return address and leaves by a jump keeps its frame until a later
`RET` ends it.

Call tracking only runs while the debugger is open, so the stack is built from
the moment you open it — not retroactively.
