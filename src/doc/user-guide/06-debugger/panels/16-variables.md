# Variables

The C variables of the call frame you choose, and the globals and statics, as a tree (Name, Type, Value,
Address), at every stop. Needs a z88dk `-debug` source map. It is the tab between **Watches** and **Breakpoints**.

Choose the frame in the selector at the top, or by clicking a row of the [Call Stack](06-call-stack.md): row N is frame
N + 1. Open a struct, an array or a pointer to see inside it. Right-click a row ▸ **Watch** adds it to the
[Watches](07-watches.md).

Like the Call Stack it shows data only while the machine is **paused**. Double-click a Value to edit it (right-click ▸ **Edit Value**).

A value the compiler's records cannot be trusted for carries a note in brackets (also in the tooltip and, after an edit, in the message above the tree): the 80cc frame-slot note, `shares its stack slot with ...`, `scope unknown`.

Everything about it is in [C variables in the debugger](../source-debugger/11-c-variables.md).
