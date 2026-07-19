# Watches and watch sizes

A watch shows the live contents of an address in one of three sizes:

| Size | Bytes | Displayed as |
|---|---|---|
| Byte | 1 | `$XX` |
| Word | 2 | `$XXXX` |
| Long | 4 | `$XXXXXXXX` |

Word and Long are assembled little-endian, low byte first, matching the Z80's
own convention — so a Word watch on a `LD (nn),HL` destination reads back `HL`.

Watches read through the CPU's current view of memory, so a watch on a banked
address follows whatever is paged in at that moment.

The address field accepts loaded symbol names as well as hexadecimal. Watches
remain logical-address only: they deliberately follow the current mapping and
are not pinned to the physical page recorded in an SLD source location.
