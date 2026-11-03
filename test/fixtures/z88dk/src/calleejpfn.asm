; unsigned int addtwo(unsigned int a, unsigned int b) __z88dk_callee
; A hand-written callee routine whose body jumps with JP (HL) while its
; return address sits ABOVE its CALL's slot (GH #349 R1'').
SECTION code_user
PUBLIC _addtwo
_addtwo:
    pop hl              ; the return address
    pop de              ; one argument
    pop bc              ; the other
    push hl             ; the return address back: 4 bytes above the CALL's slot
    push bc             ; one more word: the return address is at SP+2
    ld hl,stage2
    jp (hl)             ; an internal jump, not a return
stage2:
    pop bc              ; the return address is at SP
    ld hl,stage3
    jp (hl)             ; another internal jump
stage3:
    ld l,c
    ld h,b
    add hl,de
    ret
