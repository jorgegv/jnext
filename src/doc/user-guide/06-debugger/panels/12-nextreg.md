# NextREG

![NextREG panel](../../img/debugger-nextreg.png)

All 256 NextREG registers with a name for the ones JNEXT knows about, and the
value in both hex and binary.

The **Hex** column is editable: type a new value and it is written to the
register exactly as `NEXTREG nn,n` would write it, with all the side effects
that implies. The values shown are the *live* values composed by the hardware,
not merely the last byte written — so a Layer 2 enable set through port
`0x123B` correctly shows up in NR `0x69`.
