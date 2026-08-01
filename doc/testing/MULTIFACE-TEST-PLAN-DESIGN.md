# Multiface — test plan design

> Written 2026-08-01 (GH #196 phase 2). The Multiface suite was implemented in
> the Task 8 waves of 2026-05 directly against the VHDL, without a plan doc of
> its own; its coverage notes lived in the traceability matrix instead. The
> matrix is generated now and carries no prose, so they are recorded here —
> the place the generator links to and the place a plan row can live.
>
> Oracle: `cores/zxnext/src/device/multiface.vhd`, as for every subsystem.

## Coverage notes (moved from the traceability matrix, GH #196)

The Multiface peripheral (`src/peripheral/multiface.{h,cpp}`), implemented in
the Task 8 waves of 2026-05 against `device/multiface.vhd` and the `zxnext.vhd`
glue that wires it to the port decoder, the NMI fabric and the memory map. The
suite has no separate plan doc: it was written row-by-row from the VHDL, and
each `check()` carries its own citation in the detail argument, which is what
the `VHDL file:line` column below is read from.
**Read from, not necessarily all of.** The extractor stops a citation's line
list at the first interrupting prose, so a detail spelled
`multiface.vhd:158 (clear), :165 (eff)` publishes `:158` alone — the `:165`
sits behind `(clear)`, and reaching across that means consuming English, whose
failure mode is a confidently WRONG citation. 11 of this section's cells are
short of their source that way (and 2 more elsewhere in this document); each
still names the row's primary evidence, and none names anything the source
does not. Closing them means re-spelling the detail as a plain list
(`:158,165`), which is an edit in the test source.
Four groups: `MF-CORE-*` the state machine (NMI arm, invisible latch, the
`0x0066` fetch that raises `mf_enable`, RETN teardown), `MF-PORT-*` the four
`mf_type` port-pair decodes and their negative cases, `MF-MUX-*` the read
multiplexer priority `zxnext.vhd:4310-4322`, `MF-OVL-*` the 0x0000-0x1FFF
overlay and its SRAM pages, and `MF-M1G-*` the M1 gating.
Several rows are asserted twice — once in the real `check()` and once in a
fixture-init guard reusing the same ID, which is textually FIRST. Both the
citation and the `Test file:line` are taken from the first call that carries a
citation, so these uncited guards shadow neither, and the two columns of a row
always name one and the same call.
The rule is "first CITED call", not "not a guard" — nothing row-local can tell
a guard from an assertion, and guessing from its wording is the kind of
inference this document refuses. A guard that carries a citation of its own
therefore does win, and 12 rows of `## Contention` are in exactly that shape:
their citation is right and their `Test file:line` names the guard. Untouched
here, and the fix is to drop the ID reuse in that suite.
