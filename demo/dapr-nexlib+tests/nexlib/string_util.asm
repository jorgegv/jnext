SECTION code_user

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _chr_hex_for_u4
_chr_hex_for_u4:
    LD      A, L
    CALL    chr_hex_for_u4
    LD      L, A
    RET
    
chr_hex_for_u4:
    AND     A, $0F
    ADD     A, $30     ; + '0'
    CP      A, $3A
    RET     C
    ADD     A, 7
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _str_hex_u8
_str_hex_u8:
    DEFB 0, 0, 0

PUBLIC _str_hex_for_u8
_str_hex_for_u8:
    LD      C, L
    LD      DE, _str_hex_u8

    LD      A, C
    SWAPNIB
    CALL    chr_hex_for_u4
    LD      (DE), A

    INC     DE

    LD      A, C
    CALL    chr_hex_for_u4
    LD      (DE), A

    INC     DE
    XOR     A
    LD      (DE), A

    LD      HL, _str_hex_u8
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _str_hex_u16
_str_hex_u16:
    DEFB 0, 0, 0, 0, 0

PUBLIC _str_hex_for_u16
_str_hex_for_u16:
    LD      BC, HL
    LD      DE, _str_hex_u16

    LD      A, B
    SWAPNIB
    CALL    chr_hex_for_u4
    LD      (DE), A

    INC     DE

    LD      A, B
    CALL    chr_hex_for_u4
    LD      (DE), A

    INC     DE

    LD      A, C
    SWAPNIB
    CALL    chr_hex_for_u4
    LD      (DE), A

    INC     DE

    LD      A, C
    CALL    chr_hex_for_u4
    LD      (DE), A

    INC     DE
    XOR     A
    LD      (DE), A

    LD      HL, _str_hex_u16
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _str_bin_u8
_str_bin_u8:
    DEFB 0, 0, 0, 0, 0, 0, 0, 0, 0

PUBLIC _str_bin_for_u8
_str_bin_for_u8:
    LD      DE, _str_bin_u8
    LD      A, L
    LD      B, 8
sbfu8_loop:
    CALL    str_bin_digit_proc
    DJNZ    sbfu8_loop

    XOR     A
    LD      (DE), A

    LD      HL, _str_bin_u8
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

str_bin_digit_proc:
    LD      C, '0'
    BIT     7, A
    JR      Z, str_bin_digit_proc_branch
    INC     C
str_bin_digit_proc_branch:
    RLCA
    LD      (DE), C
    INC     DE
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _str_bin_u16
_str_bin_u16:
    DEFB 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

PUBLIC _str_bin_for_u16
_str_bin_for_u16:
    LD      DE, _str_bin_u16

    LD      A, H
    LD      B, 8
sbfu16_loop1:
    CALL    str_bin_digit_proc
    DJNZ    sbfu16_loop1

    LD      A, L
    LD      B, 8
sbfu16_loop2:
    CALL    str_bin_digit_proc
    DJNZ    sbfu16_loop2

    XOR     A
    LD      (DE), A

    LD      HL, _str_bin_u16
    RET

