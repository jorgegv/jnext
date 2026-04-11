;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
;  NexLib - Game Helper Library for ZX Spectrum Next
;
;  (c) 2025 David Crespo - https://github.com/dcrespo3d
;                          https://davidprograma.itch.io
;                          https://www.youtube.com/@Davidprograma
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

SECTION code_user

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

EXTERN _tmstart

PUBLIC _curr_print_symbol
PUBLIC _curr_pos_x
PUBLIC _curr_pos_y
PUBLIC _last_pos_x
PUBLIC _last_pos_y

_curr_print_posptr: DEFW    $4000

_curr_print_attr:   DEFB    0
_curr_print_symbol: DEFB    0

_curr_pos_x:        DEFB    0
_curr_pos_y:        DEFB    0

_last_pos_x:        DEFB    0
_last_pos_y:        DEFB    0

DBT_OFFSET:         EQU     0



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _print_set_pos_asm

PUBLIC PSPA_SMC

_print_set_pos_asm:
    LD      A, (_curr_pos_x)
    LD      (_last_pos_x), A
    LD      B, A
    
    LD      A, (_curr_pos_y)
    LD      (_last_pos_y), A
    LD      C, A

    LD  H, 0
    LD  L, C
PSPA_SMC:       ; self-modifying code point 1
    ADD HL, HL  ; this ADD may be changed to NOP
    ADD HL, HL
    ADD HL, HL
    ADD HL, HL
    ADD HL, HL
    LD  DE, HL
    ADD HL, HL
    ADD HL, HL
    ADD HL, DE   ; KY*y    KY may be 80, 160
    LD  DE, HL

    LD  H, 0
    LD  L, B
    ADD HL, HL
    ADD HL, DE   ; 2*x + KY*y
    LD  DE, HL

    LD  HL, (_tmstart)
    ADD HL, DE
    LD  (_curr_print_posptr), HL

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _print_set_attr

_print_set_attr:
    LD      A, L
    LD      (_curr_print_attr), A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _print_set_color
_print_set_color:
    LD      A, L
    SWAPNIB
    AND A, %11110000
    LD (_curr_print_attr), A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _print_set_symbol
_print_set_symbol:
    LD      A, L
    LD (_curr_print_symbol), A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _print_symbol
_print_symbol:
    LD  HL, (_curr_print_posptr)
    LD  A, (_curr_print_symbol)
    LD (HL), A
    INC HL

    LD  A, (_curr_print_attr)
    LD (HL), A
    INC HL

    LD (_curr_print_posptr), HL

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _print_char
_print_char:
    LD A, L

    CP A, '\n'
    JR NZ, print_char_char

    LD      A, (_last_pos_x)
    LD      (_curr_pos_x), A

    LD      A, (_last_pos_y)
    INC     A
    LD      (_curr_pos_y), A

    CALL _print_set_pos_asm

    RET
    
print_char_char:
    LD HL, (_curr_print_posptr)

    ; handle tile index
    ADD A, DBT_OFFSET
    LD (HL), A
    INC HL

    ; handle tile attribute
    LD A, (_curr_print_attr)
    LD (HL), A
    INC HL

    LD (_curr_print_posptr), HL

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; PUBLIC _print_block
; _print_block:
;     LD HL, 2
;     ADD HL, SP
;     LD L, (HL)
;     LD A, L
    
;     LD HL, (_curr_print_posptr)
;     LD (HL), A
;     INC HL

;     LD A, (_curr_print_attr)
;     LD (HL), A
;     INC HL

;     LD (_curr_print_posptr), HL

;     RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _print_str
_print_str:
    LD      DE, HL
    LD      A, (DE)
    CP      0
    JR      Z, print_str_end
    print_str_loop:
        LD H, 0
        LD L, A
        PUSH DE
        PUSH HL
        CALL _print_char
        POP HL
        POP DE
        INC DE
        LD A, (DE)
        CP 0
    JR NZ, print_str_loop

    print_str_end:
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _hex_char_for_val
_hex_char_for_val:
    LD HL, 2
    ADD HL, SP
    LD L, (HL)

    LD A, L
    SUB A, 10
    LD A, L
    JR C, hex_char_for_val_ge10
    ADD A, 7

    hex_char_for_val_ge10:
    ADD A, 0x30     ; + '0'
    LD H, 0
    LD L, A

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

EXTERN _str_hex_for_u16

PUBLIC _print_hex_word
_print_hex_word:
    CALL _str_hex_for_u16
    CALL _print_str
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

EXTERN _str_hex_for_u8

PUBLIC _print_hex_byte
_print_hex_byte:
    CALL _str_hex_for_u8
    CALL _print_str
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

EXTERN _chr_hex_for_u4

PUBLIC _print_hex_nibble
_print_hex_nibble:
    CALL _chr_hex_for_u4
    CALL _print_char
    RET

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

_print_dec_word_flag: DEFB 0

PUBLIC _print_dec_word
_print_dec_word:
    POP BC
    POP HL      ; HL <- val
    PUSH HL
    PUSH BC

    XOR A
    CP H
    JR NZ, print_dec_word_0
    CP L
    JR NZ, print_dec_word_0
    LD L, '0'
    PUSH HL
    CALL _print_char
    POP HL
    RET

print_dec_word_0:
    XOR A
    LD (_print_dec_word_flag), A

    LD BC, -10000
    CALL print_dec_word_1
    LD BC, -1000
    CALL print_dec_word_1
    LD BC, -100
    CALL print_dec_word_1
    LD C, -10
    CALL print_dec_word_1
    LD C, B

print_dec_word_1:
    LD A, '0' - 1
print_dec_word_2:
    INC A
    ADD HL, BC
    JR  C, print_dec_word_2
    SBC HL, BC

    LD D, A
    LD A, (_print_dec_word_flag)
    CP A, 1
    LD A, D
    JR Z, print_dec_word_noskip

    CP A, '0'
    JR Z, print_dec_word_skip

print_dec_word_noskip:
    LD D,0
    LD E,A
    PUSH HL
    ; PUSH BC     ; not needed as called routines do not clobber BC
    PUSH DE
    LD      L, A
    CALL _print_char
    POP DE
    ; POP BC      ; not needed as called routines do not clobber BC
    POP HL

    LD A, 1
    LD (_print_dec_word_flag), A

print_dec_word_skip:    
    
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _print_dec_byte
_print_dec_byte:
    LD  HL, 2
    ADD HL, SP
    LD  A, (HL)

    CP  A, 0
    JR  NZ, print_dec_byte_0
    LD  H, 0
    LD  L, '0'
    CALL _print_char
    RET

print_dec_byte_0:
    LD H, 0

    LD C, -100
    CALL print_dec_byte_1
    LD C, -10
    CALL print_dec_byte_1
    LD C, -1

print_dec_byte_1:
    LD B, '0' - 1
print_dec_byte_2:
    INC B
    ADD A, C
    JR  C, print_dec_byte_2
    SBC A, C

    LD  D, A
    LD  A, H
    CP  A, 1
    LD  A, D
    JR  Z, print_dec_byte_noskip

    LD  D, A
    LD  A, B
    CP  A, '0'
    LD  A, D
    JR  Z, print_dec_byte_skip

print_dec_byte_noskip:
    LD  L, A
    LD  A, B
    LD  D, 0
    LD  E, A
    PUSH HL
    PUSH DE
    LD  L, A
    CALL _print_char
    POP DE
    POP HL
    LD  A, L

    LD  H, 1

print_dec_byte_skip:

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _print_frame
_print_frame:
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _str_dec_u16
_str_dec_u16: DEFS 6, 0

PUBLIC _str_dec_for_u16
_str_dec_for_u16:
    POP BC
    POP HL      ; HL <- val
    PUSH HL
    PUSH BC

    LD      DE, _str_dec_u16
    LD      A, 0

    LD      (DE), A
    INC     DE
    LD      (DE), A
    INC     DE
    LD      (DE), A
    INC     DE
    LD      (DE), A
    INC     DE
    LD      (DE), A
    INC     DE

    LD      DE, _str_dec_u16

    LD BC, -10000
    CALL str_dec_for_u16_1
    LD BC, -1000
    CALL str_dec_for_u16_1
    LD BC, -100
    CALL str_dec_for_u16_1
    LD C, -10
    CALL str_dec_for_u16_1
    LD C, B

str_dec_for_u16_1:
    LD A, -1
str_dec_for_u16_2:
    INC A
    ADD HL, BC
    JR  C, str_dec_for_u16_2
    SBC HL, BC

    LD      (DE), A
    INC     DE

    LD      HL, _str_dec_u8

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _str_dec_u8
_str_dec_u8: DEFS 3, 0

PUBLIC _str_dec_for_u8
_str_dec_for_u8:
    LD      DE, _str_dec_u8
    LD      A, 0

    LD      (DE), A
    INC     DE
    LD      (DE), A
    INC     DE
    LD      (DE), A
    INC     DE

    LD      DE, _str_dec_u8

    LD      HL, 2
    ADD     HL, SP
    LD      A, (HL)
    ; LD      A, L

str_dec_for_u8_0:
    LD      C, -100
    CALL    str_dec_for_u8_1
    LD      C, -10
    CALL    str_dec_for_u8_1
    LD      C, -1

str_dec_for_u8_1:
    LD      B, -1
str_dec_for_u8_2:
    INC     B
    ADD     A, C
    JR      C, str_dec_for_u8_2
    SBC     A, C

    PUSH    AF
    LD      A, B
    LD      (DE), A
    INC     DE
    POP     AF

    LD      HL, _str_dec_u8

    RET

