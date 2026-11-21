# Symbols

Every label the debugger knows: the loaded [MAP file](../functions/05-symbols-from-z88dk-map-files.md)
or SLD, and [your own labels](../functions/16-user-labels-and-comments.md). One row each, in
address order, with the **Name** (a z88dk C symbol by its C name, `counter` for `_counter`), the
**Address** and the **Memory** it was assembled for: `any` for a label that names its address in
every memory, or the page (`P20`) for one that belongs to a banked page.

- The **filter** box keeps the rows whose name contains the text, in any case; the count beside it
  reads `N of M`.
- **Double-click** a row, or press **Enter**, to show it in the Disassembly.
- **Go > Symbols** in the debugger window brings this tab to the front.
- A label of a banked page whose page is **not paged in** at that address now is greyed, and its
  tooltip says which page is missing (`not mapped at $C000 now: P20 is not paged in`). The
  Disassembly shows what the CPU sees, so jumping there would show other code under the label's
  name: nothing happens, and the panel says why. It lights up again as soon as the page is paged
  in.
- The right-click menu offers **Show in Disassembly**, **Show in Source** (with a source map; disabled
  where no line covers the label) and **Show in Memory**. All three are disabled for a
  label that is not paged in.

The list is rebuilt when a map is loaded or a label changes, not on every step.
