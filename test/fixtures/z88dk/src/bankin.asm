    SECTION code_user
    PUBLIC _bank_in
; void bank_in(void): MMU6/MMU7 = 8K pages 20/21 (NextREG 0x56/0x57)
_bank_in:
    nextreg 0x56,20
    nextreg 0x57,21
    ret
