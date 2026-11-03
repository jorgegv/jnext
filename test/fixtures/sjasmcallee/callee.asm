; GH #349 R1'': routines that leave by JP (HL)/(IX) without leaving the routine,
; and one that leaves by JP (HL) into its caller. Assembled by sjasmplus (README).
        DEVICE ZXSPECTRUMNEXT
        ORG $8000
start:  di
        ld sp,$BFFE
        ld de,5
        push de
        call addone             ; callee: return address moved up, then JP (HL) inside
        ld b,a
        ld de,7
        push de
        push de
        call addtwo             ; callee: two arguments, then JP (IX) with a word pushed
        ld c,a
        call dispatch           ; consumes its return address (an inline table), JP (HL) into the caller
        defw case_a
case_b: ld a,2
        jr idle
case_a: ld a,1
idle:   jr idle

addone: pop hl                  ; the return address
        pop de                  ; the argument
        push hl                 ; the return address back, 2 above the CALL's slot
        ld hl,stage2
        jp (hl)                 ; not a return: the routine goes on
stage2: ld a,e
        inc a
        ret

addtwo: pop hl                  ; the return address
        pop de
        pop bc
        push hl                 ; the return address back, 4 above the CALL's slot
        push bc                 ; one more word: the return address is at SP+2
        ld ix,stage3
        jp (ix)                 ; not a return
stage3: pop bc
        ld a,e
        add a,c
        ret

dispatch:
        pop hl                  ; the "return address" is the table
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        jp (hl)                 ; leaves the routine for a label in its caller
end:
        SAVEBIN "callee.bin", start, end - start
