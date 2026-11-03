# Stack

![Stack panel](../../img/debugger-stack.png)

Twenty-four words from `SP` upwards, in ascending address order with the top of
stack in the first row (highlighted green). Each row shows the address, the
16-bit word in hex and decimal, and its high and low bytes separately — the
usual case being "is that a return address or two bytes of data". A last column,
**Memory**, names the memory the row's address reads from now
([which memory an address is in](../index.md#which-memory-an-address-is-in)).
