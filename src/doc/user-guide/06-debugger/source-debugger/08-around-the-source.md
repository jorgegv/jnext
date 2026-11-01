# In the Source tab: names, watches and coverage

Three things that work from the Source tab beyond stepping. The tab's controls
are in [its panel page](../panels/15-source.md).

## C names

A z88dk C variable or function has an underscore in front of its name in the
map (`_counter`). JNEXT shows and accepts the C name:

- `counter` works wherever `_counter` does: the **Add** dialogs of the
  Breakpoints and Watches panels, **Go to Address** in the Disassembly and Memory
  panels, and a script's `@counter`;
- the views show `counter`.

A number wins over a symbol in an address field: `FE` or `B` is that address even
when the program has a variable of that name. Write `@FE` to mean the symbol.

See [Symbols from MAP files](../functions/05-symbols-from-z88dk-map-files.md).

## Watching and breaking from the source

In the example of [A first session](01-a-first-session.md), right-click `counter` on line 20
and choose **Watch**: it joins the Watches panel under its C name. **Break on
Write** from the same menu stops the machine whenever the program writes it. What
the menu offers, and for what under the pointer, is described in the panel page's
[right-click menu](../panels/15-source.md#right-click-menu).

## Which lines have run

Switch on memory coverage and every line of the shown file that has run gets a
pale green background; the current line keeps its own highlight. Use **Debug ▸
Coverage ▸ Enable Coverage**, or start JNEXT with `--coverage` to record from the
first instruction. Marks are visible while the machine is paused, like the rest
of the debugger.

It answers "did this code ever run?" without a breakpoint. In the example,
enable coverage while the machine is paused at the start, add a breakpoint on
`main.c:24` and Continue: the lines of `main` before it are green, and `counter++` in the final
`while (1)` is not. Coverage is kept per physical
memory, so the same address in two RAM pages has separate marks. All of it is in
[Memory coverage](../functions/15-memory-coverage.md).
