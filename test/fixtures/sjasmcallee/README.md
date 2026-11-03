# sjasmplus SLD fixture for routines that jump by JP (r) without leaving (GH #349 R1'')

`callee.asm` is ours, written for the call-stack rows (`SRCDBG-CS-*` / `SRCSTEP-36..40` in
`source_debug_test`). `callee.sld` and `callee.bin` are what sjasmplus v1.23.1 (z00m128/sjasmplus
commit `46aae3fa784cae903345b87bd6b9db16a77c4bea`, the build of `../sjasmequ`) writes for it,
byte for byte:

    sjasmplus --sld=callee.sld callee.asm      # also writes callee.bin (SAVEBIN)

Never edit the outputs; change `callee.asm` and run the command again.

The program sets `SP=$BFFE`, calls `addone` (one argument: the callee moves its return address
up with `POP HL; POP DE; PUSH HL`, then `JP (HL)` inside the routine), `addtwo` (two arguments,
`JP (IX)` with the return address at SP+2) and `dispatch` (the hand-written `l_case` shape: it
pops its "return address", an inline `DEFW`, and `JP (HL)` into its caller), and ends in
`idle: jr idle`.
