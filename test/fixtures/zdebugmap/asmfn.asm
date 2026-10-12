    SECTION code_user
    PUBLIC _asm_bump
    EXTERN _counter
; void asm_bump(void): counter += 3
_asm_bump:
    ld hl,_counter
    ld a,(hl)
    add a,3
    ld (hl),a
.bump_done
    ret
