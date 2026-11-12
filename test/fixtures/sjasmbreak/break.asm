; GH #456: sjasmplus `break` (FD 00 since sjasmplus 1.21.0) for the magic-breakpoint rows.
; Assembled by sjasmplus (README).
        DEVICE ZXSPECTRUMNEXT
        OPT --zxnext=cspect
        ORG $8000
start:  ld a,$5A
brk:    break                   ; FD 00
idle:   jr idle
end:
        SAVEBIN "break.bin", start, end - start
