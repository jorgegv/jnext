# sjasmplus `break` fixture (GH #456)

`break.asm` is ours. `break.bin` is what sjasmplus v1.23.1 (z00m128/sjasmplus commit
`46aae3fa784cae903345b87bd6b9db16a77c4bea`, the build of `../sjasmequ`) writes for it, byte for byte:

    sjasmplus break.asm      # writes break.bin (SAVEBIN)

Expected bytes: `3E 5A FD 00 18 FE` (`ld a,$5A`, `break` = FD 00, `jr $`); `brk` = $8002 when loaded
at $8000. `break` needs `--zxnext=cspect` (set in-source by `OPT --zxnext=cspect`).

Never edit the outputs; change `break.asm` and run the command again.
