# Watches and watch sizes

A watch shows the live contents of an address in one of three sizes:

| Size | Bytes | Displayed as |
|---|---|---|
| Byte | 1 | `$XX (Nd)` |
| Word | 2 | `$XXXX (Nd)` |
| Long | 4 | `$XXXXXXXX (Nd)` |

Every value is shown in hex and, in parentheses, in decimal: the same value read
unsigned (Byte 0 to 255, Word 0 to 65535, Long 0 to 4294967295), for example
`$D0 (208d)`.

Word and Long are assembled little-endian, low byte first, matching the Z80's
own convention — so a Word watch on a `LD (nn),HL` destination reads back `HL`.

Watches read through the CPU's current view of memory, so a watch on a banked
address follows whatever is paged in at that moment.
