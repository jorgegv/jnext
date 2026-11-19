# Watches and watch sizes

A watch shows the live contents of an address in one of three sizes:

| Size | Bytes | Displayed as |
|---|---|---|
| Byte | 1 | `$XX (Nd)` |
| Word | 2 | `$XXXX (Nd)` |
| Long | 4 | `$XXXXXXXX (Nd)` |
| Expression | the value | `$XX (Nd)`, `$XXXX (Nd)` or `$XXXXXXXX (Nd)` by size of the value |

Every value is shown in hex and, in parentheses, in decimal: the same value read
unsigned (Byte 0 to 255, Word 0 to 65535, Long 0 to 4294967295), for example
`$D0 (208d)`.

Word and Long are assembled little-endian, low byte first, matching the Z80's
own convention — so a Word watch on a `LD (nn),HL` destination reads back `HL`.

An Expression watch shows a signed 32-bit result: `$FF (255d)`, then `$0100 (256d)` once it passes 255, and `-1` reads
`$FFFFFFFF (-1d)`.

A watch on a C variable has no size to choose: it has the variable's own type
([Variables](../panels/16-variables.md)).

Watches read through the CPU's current view of memory, so a watch on a banked
address follows whatever is paged in at that moment.
