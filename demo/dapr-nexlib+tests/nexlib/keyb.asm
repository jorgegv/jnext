SECTION code_user

; 01111111 7F --MNB
; 10111111 BF -LKJH
; 11011111 DF POIUY
; 11101111 EF 09876
; 11110111 F7 12345
; 11111011 FB QWERT
; 11111101 FD ASDFG
; 11111110 FE -ZXCV

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; keys pressed in current update

PUBLIC _zxkeyBNMys      ; 0     7F
PUBLIC _zxkeyHJKLe      ; 1     BF
PUBLIC _zxkeyYUIOP      ; 2     DF
PUBLIC _zxkey67890      ; 3     EF
PUBLIC _zxkey54321      ; 4     F7
PUBLIC _zxkeyTREWQ      ; 5     FB
PUBLIC _zxkeyGFDSA      ; 6     FD
PUBLIC _zxkeyVCXZc      ; 7     FE
PUBLIC _nxkey0          ; 8
PUBLIC _nxkey1          ; 9

._zxkeyBNMys defb 0
._zxkeyHJKLe defb 0
._zxkeyYUIOP defb 0
._zxkey67890 defb 0
._zxkey54321 defb 0
._zxkeyTREWQ defb 0
._zxkeyGFDSA defb 0
._zxkeyVCXZc defb 0
._nxkey0     defb 0
._nxkey1     defb 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; keys pressed in current update

PUBLIC _zxprvBNMys
PUBLIC _zxprvYUIOP
PUBLIC _zxprvHJKLe
PUBLIC _zxprv67890
PUBLIC _zxprv54321
PUBLIC _zxprvTREWQ
PUBLIC _zxprvGFDSA
PUBLIC _zxprvVCXZc
PUBLIC _nxprv0
PUBLIC _nxprv1

._zxprvBNMys defb 0
._zxprvHJKLe defb 0
._zxprvYUIOP defb 0
._zxprv67890 defb 0
._zxprv54321 defb 0
._zxprvTREWQ defb 0
._zxprvGFDSA defb 0
._zxprvVCXZc defb 0
._nxprv0     defb 0
._nxprv1     defb 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _keyb_init

EXTERN _disable_8x5_entries_for_extended_keys

_keyb_init:
    CALL    _disable_8x5_entries_for_extended_keys
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _keyb_update

_keyb_update:
    CALL    _keyb_copy_curr_to_prev
    CALL    _keyb_read_spectrum_std
    CALL    _keyb_read_spectrum_next
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _keyb_copy_curr_to_prev
_keyb_copy_curr_to_prev:
    LD      HL, _zxkeyBNMys
    LD      DE, _zxprvBNMys
    LD      BC, 10
    LDIR
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _keyb_read_spectrum_next

extern readNextReg

_keyb_read_spectrum_next:
    LD A, $B0
    CALL readNextReg
    LD (_nxkey0), A

    LD A, $B1
    CALL readNextReg
    LD (_nxkey1), A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _keyb_read_spectrum_std

_keyb_read_spectrum_std:
    LD      HL, _zxkeyBNMys     ; initial row variable address
    LD      B, 0x7F             ; initial row selector
    LD      D, 8                ; loop counter
krss_loop:
    LD      C, $FE
    IN      A, (C)
    XOR     $FF
    AND     A, $1F
    LD      (HL), A

    INC     HL                  ; next row variable
    LD      A, B
    RRCA                        ; next row selector
    LD      B, A

    DEC     D                   ; decrement counter
    JR      NZ, krss_loop       ; loop if not zero

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; input:    keycode (HL - H=row, L=mask)
; output:   keypress state (1 or 0 in L)
;           for PREVIOUS update
; clobbers: A C DE HL

PUBLIC _keyb_was_pressed
_keyb_was_pressed:
    LD      DE, _zxprvBNMys
    JR      ASMPC + 5           ; jump to LD C,L below

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; input:    keycode (HL - H=row, L=mask)
; output:   keypress state (1 or 0 in L)
;           for CURRENT update
; clobbers: A C DE HL

PUBLIC _keyb_is_pressed
_keyb_is_pressed:
    LD      DE, _zxkeyBNMys     ; load pointer to first row

    LD      C, L                ; store mask in C
    LD      L, H                ; store row index in L
    LD      H, 0                ; set HL to just row index

    ADD     HL, DE              ; set pointer to wanted row
    LD      A, (HL)             ; row data in A
    AND     A, C                ; check against mask

    LD      L, 0
    RET     Z
    INC     L
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _keyb_was_pressed_any
_keyb_was_pressed_any:
    LD      HL, _zxprvBNMys
    JR      ASMPC+5             ; to XOR A below

PUBLIC _keyb_is_pressed_any
_keyb_is_pressed_any:
    LD      HL, _zxkeyBNMys
    XOR     A
    LD      B, 10
kipa_loop:
    OR      (HL)
    INC     HL
    DJNZ    kipa_loop

    CP      A, 0
    LD      L, 0
    RET     Z
    INC     L
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; bool keyb_is_just_pressed (u16 keycode) fastcall
; return keyb_is_pressed(keycode) && !keyb_was_pressed(keycode);

PUBLIC _keyb_is_just_pressed
_keyb_is_just_pressed:
    PUSH    HL
    CALL    _keyb_is_pressed
    LD      A, L
    CP      A, 0
    JR      Z, popHLret0

    POP     HL
    CALL    _keyb_was_pressed
    LD      A, L
    CP      A, 0
    JR      NZ, ret0
    JR      ret1

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; bool keyb_is_just_released(u16 keycode) fastcall
; return !keyb_is_pressed(keycode) && keyb_was_pressed(keycode);

PUBLIC _keyb_is_just_released
_keyb_is_just_released:
    PUSH    HL
    CALL    _keyb_is_pressed
    LD      A, L
    CP      A, 0
    JR      NZ, popHLret0

    POP     HL
    CALL    _keyb_was_pressed
    LD      A, L
    CP      A, 0
    JR      Z, ret0
    JR      ret1

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

popHLret0:
    POP     HL
ret0:
    LD      L, 0    
    RET

popHLret1:
    POP     HL
ret1:
    LD      L, 1    
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; bool keyb_is_just_pressed_any(void)
; return keyb_is_pressed_any() && !keyb_was_pressed_any();

PUBLIC _keyb_is_just_pressed_any
_keyb_is_just_pressed_any:
    CALL    _keyb_is_pressed_any
    LD      A, L
    CP      A, 0
    JR      Z, ret0

    CALL    _keyb_was_pressed_any
    LD      A, L
    CP      A, 0
    JR      NZ, ret0
    JR      ret1

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; bool keyb_is_just_released_any(void)
; return !keyb_is_pressed_any() && keyb_was_pressed_any();

PUBLIC _keyb_is_just_released_any
_keyb_is_just_released_any:
    CALL    _keyb_is_pressed_any
    LD      A, L
    CP      A, 0
    JR      NZ, ret0

    CALL    _keyb_was_pressed_any
    LD      A, L
    CP      A, 0
    JR      Z, ret0
    JR      ret1


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _keyb_code
_keyb_code:
    LD      DE, _zxkeyBNMys
    LD      HL, 0

    LD      B, 10
keyb_code_loop:
    LD      A, (DE)
    AND     A, $1F
    JR      Z, ASMPC+4
    LD      L, A
    RET
    INC     DE
    INC     H

    DJNZ    keyb_code_loop

    LD      H, 0
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; counts bits in number (up to 8)
; input: A - number to count bits
; input: HL - previous count of "1" bits
; output: HL - added count of "1" bits in number
; clobbers: A B HL
PUBLIC count_set_bits_in_byte
count_set_bits_in_byte:
    LD      B, 8
cbu8_loop:
    RRCA
    JR      NC, ASMPC + 3
    INC     HL
    DJNZ    cbu8_loop
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; counts number of pressed keys (standard spectrum + next)
; output: HL - number of pressed keys
; clobbers: A BC HL DE
PUBLIC _keyb_count
_keyb_count:
    LD      HL, 0
    LD      DE, _zxkeyBNMys
    LD      C, 10
keyb_count_loop:
    LD      A, (DE)
    CALL    count_set_bits_in_byte
    INC     DE

    DEC     C
    JR      NZ, keyb_count_loop

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC get_first_set_bit
get_first_set_bit:
    LD      L, 0
    LD      B, 8
gfsb_loop:
    RRCA
    JR      NC, ASMPC+3
    RET
    INC     L
    DJNZ    gfsb_loop
    LD      L, $FF
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _keyb_short_for_code
_keyb_short_for_code:
    LD      DE, HL

    LD      HL, 0
    LD      A, E
    CALL    get_first_set_bit

    LD      A, D
    RLCA
    RLCA
    RLCA
    OR      A, L
    LD      L, A

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _keyb_ch4short
_keyb_ch4short:
    DEFB $1c, $1f
    DEFM "MNB---"

    DEFB $1d
    DEFM "LKJH---"

    DEFM "POIUY---"

    DEFM "09876---"

    DEFM "12345---"

    DEFM "QWERT---"

    DEFM "ASDFG---"

    DEFB $1e
    DEFM "ZXCV---"

    DEFB $13, $12, $11, $10
    DEFM ".,"
    DEFB $22
    DEFM ";"

    DEFB $1b, $1a, $19, $18, $17, $16, $15, $14

    DEFB $00


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; PUBLIC _zxkey1
; _zxkey1:
;     LD      A, (_zxkey54321)
;     AND     A, 0x01
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkey2
; _zxkey2:
;     LD      A, (_zxkey54321)
;     AND     A, 0x02
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkey3
; _zxkey3:
;     LD      A, (_zxkey54321)
;     AND     A, 0x04
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkey4
; _zxkey4:
;     LD      A, (_zxkey54321)
;     AND     A, 0x08
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkey5
; _zxkey5:
;     LD      A, (_zxkey54321)
;     AND     A, 0x10
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkey6
; _zxkey6:
;     LD      A, (_zxkey67890)
;     AND     A, 0x10
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkey7
; _zxkey7:
;     LD      A, (_zxkey67890)
;     AND     A, 0x08
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkey8
; _zxkey8:
;     LD      A, (_zxkey67890)
;     AND     A, 0x04
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkey9
; _zxkey9:
;     LD      A, (_zxkey67890)
;     AND     A, 0x02
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkey0
; _zxkey0:
;     LD      A, (_zxkey67890)
;     AND     A, 0x01
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyQ
; _zxkeyQ:
;     LD      A, (_zxkeyTREWQ)
;     AND     A, 0x01
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyW
; _zxkeyW:
;     LD      A, (_zxkeyTREWQ)
;     AND     A, 0x02
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyE
; _zxkeyE:
;     LD      A, (_zxkeyTREWQ)
;     AND     A, 0x04
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyR
; _zxkeyR:
;     LD      A, (_zxkeyTREWQ)
;     AND     A, 0x08
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyT
; _zxkeyT:
;     LD      A, (_zxkeyTREWQ)
;     AND     A, 0x10
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyY
; _zxkeyY:
;     LD      A, (_zxkeyYUIOP)
;     AND     A, 0x10
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyU
; _zxkeyU:
;     LD      A, (_zxkeyYUIOP)
;     AND     A, 0x08
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyI
; _zxkeyI:
;     LD      A, (_zxkeyYUIOP)
;     AND     A, 0x04
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyO
; _zxkeyO:
;     LD      A, (_zxkeyYUIOP)
;     AND     A, 0x02
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyP
; _zxkeyP:
;     LD      A, (_zxkeyYUIOP)
;     AND     A, 0x01
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyA
; _zxkeyA:
;     LD      A, (_zxkeyGFDSA)
;     AND     A, 0x01
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyS
; _zxkeyS:
;     LD      A, (_zxkeyGFDSA)
;     AND     A, 0x02
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyD
; _zxkeyD:
;     LD      A, (_zxkeyGFDSA)
;     AND     A, 0x04
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyF
; _zxkeyF:
;     LD      A, (_zxkeyGFDSA)
;     AND     A, 0x08
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyG
; _zxkeyG:
;     LD      A, (_zxkeyGFDSA)
;     AND     A, 0x10
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyH
; _zxkeyH:
;     LD      A, (_zxkeyHJKLe)
;     AND     A, 0x10
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyJ
; _zxkeyJ:
;     LD      A, (_zxkeyHJKLe)
;     AND     A, 0x08
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyK
; _zxkeyK:
;     LD      A, (_zxkeyHJKLe)
;     AND     A, 0x04
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyL
; _zxkeyL:
;     LD      A, (_zxkeyHJKLe)
;     AND     A, 0x02
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyENT
; _zxkeyENT:
;     LD      A, (_zxkeyHJKLe)
;     AND     A, 0x01
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyCAP
; _zxkeyCAP:
;     LD      A, (_zxkeyVCXZc)
;     AND     A, 0x01
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyZ
; _zxkeyZ:
;     LD      A, (_zxkeyVCXZc)
;     AND     A, 0x02
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyX
; _zxkeyX:
;     LD      A, (_zxkeyVCXZc)
;     AND     A, 0x04
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyC
; _zxkeyC:
;     LD      A, (_zxkeyVCXZc)
;     AND     A, 0x08
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyV
; _zxkeyV:
;     LD      A, (_zxkeyVCXZc)
;     AND     A, 0x10
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyB
; _zxkeyB:
;     LD      A, (_zxkeyBNMys)
;     AND     A, 0x10
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyN
; _zxkeyN:
;     LD      A, (_zxkeyBNMys)
;     AND     A, 0x08
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeyM
; _zxkeyM:
;     LD      A, (_zxkeyBNMys)
;     AND     A, 0x04
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeySYM
; _zxkeySYM:
;     LD      A, (_zxkeyBNMys)
;     AND     A, 0x02
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _zxkeySPC
; _zxkeySPC:
;     LD      A, (_zxkeyBNMys)
;     AND     A, 0x01
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeySemicolon
; _nxkeySemicolon:
;     LD      A, (_nxkey0)
;     AND     A, 0x80
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyQuote
; _nxkeyQuote:
;     LD      A, (_nxkey0)
;     AND     A, 0x40
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyComma
; _nxkeyComma:
;     LD      A, (_nxkey0)
;     AND     A, 0x20
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyPeriod
; _nxkeyPeriod:
;     LD      A, (_nxkey0)
;     AND     A, 0x10
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyUp
; _nxkeyUp:
;     LD      A, (_nxkey0)
;     AND     A, 0x08
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyDown
; _nxkeyDown:
;     LD      A, (_nxkey0)
;     AND     A, 0x04
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyLeft
; _nxkeyLeft:
;     LD      A, (_nxkey0)
;     AND     A, 0x02
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyRight
; _nxkeyRight:
;     LD      A, (_nxkey0)
;     AND     A, 0x01
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyDel
; _nxkeyDel:
;     LD      A, (_nxkey1)
;     AND     A, 0x80
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyEdit
; _nxkeyEdit:
;     LD      A, (_nxkey1)
;     AND     A, 0x40
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyBreak
; _nxkeyBreak:
;     LD      A, (_nxkey1)
;     AND     A, 0x20
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyInvVideo
; _nxkeyInvVideo:
;     LD      A, (_nxkey1)
;     AND     A, 0x10
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyTruVideo
; _nxkeyTruVideo:
;     LD      A, (_nxkey1)
;     AND     A, 0x08
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyGraph
; _nxkeyGraph:
;     LD      A, (_nxkey1)
;     AND     A, 0x04
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyCapsLock
; _nxkeyCapsLock:
;     LD      A, (_nxkey1)
;     AND     A, 0x02
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

; PUBLIC _nxkeyExtend
; _nxkeyExtend:
;     LD      A, (_nxkey1)
;     AND     A, 0x01
;     LD      HL, 0
;     JR      Z, ASMPC+3
;     INC     HL
;     RET

