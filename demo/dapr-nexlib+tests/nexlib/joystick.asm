;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
;  Celeste Classic (remake) - for ZX Spectrum Next / N-Go
;
;  (c) 2024 David Crespo - https://github.com/dcrespo3d
;                          https://davidprograma.itch.io
;                          https://www.youtube.com/@Davidprograma
;
;  Based on Celeste Classic for Pico-8 - (c) 2015 Maddy Thorson, Noel Berry
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
;  This program is free software: you can redistribute it and/or modify
;  it under the terms of the GNU General Public License as published by
;  the Free Software Foundation, either version 3 of the License, or
;  (at your option) any later version.  
;
;  This program is distributed in the hope that it will be useful,
;  but WITHOUT ANY WARRANTY; without even the implied warranty of
;  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;  GNU General Public License for more details.
; 
;  You should have received a copy of the GNU General Public License
;  along with this program.  If not, see <https://www.gnu.org/licenses/>. 
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

SECTION code_user

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _joyprev
PUBLIC _joydata
PUBLIC _joyRight
PUBLIC _joyLeft
PUBLIC _joyDown
PUBLIC _joyUp
PUBLIC _joyBut1
PUBLIC _joyBut2
PUBLIC _joyBut3
PUBLIC _joyBut4

._joyprev    defb 0
._joydata    defb 0

._joyRight   defb 0
._joyLeft    defb 0
._joyDown    defb 0
._joyUp      defb 0
._joyBut1    defb 0
._joyBut2    defb 0
._joyBut3    defb 0
._joyBut4    defb 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

JOY_RIGHT EQU $1
JOY_LEFT  EQU $2
JOY_DOWN  EQU $4
JOY_UP    EQU $8
JOY_BUT1  EQU $10
JOY_BUT2  EQU $20
JOY_BUT3  EQU $40
JOY_BUT4  EQU $80

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _joystick_init
_joystick_init:
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _joystick_update
_joystick_update:
    LD      A, (_joydata)
    LD      (_joyprev), A

    LD      C, $1F
    IN      A, (C)
    LD      (_joydata), A

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _joystick_update_flags
_joystick_update_flags:

    LD      HL, _joyRight
    LD      B, 8
    XOR     A
joystick_update_clear_loop:
    LD      (HL), A
    INC     HL
    DJNZ    joystick_update_clear_loop

    LD      A, (_joydata)
    LD      HL, _joyRight
    LD      B, 8
joystick_update_flags_loop:
    LD      E, A
    LD      C, 0
    AND     A, 1
    JR      Z, ASMPC+3
    INC     C
    LD      (HL), C
    INC     HL
    LD      A, E
    RRCA
    DJNZ    joystick_update_flags_loop

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _joystick_was_pressed
_joystick_was_pressed:
    LD      A, (_joyprev)
    JR      ASMPC + 5       ; skip to AND A, L below

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; input: joycode (u8 arg from stack)
; output: joypress state (1 or 0 in L)
; clobbers: A L

PUBLIC _joystick_is_pressed
_joystick_is_pressed:
    LD      A, (_joydata)
    AND     A, L

    LD      L, 0
    RET     Z
    INC     L
    RET
    
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; bool joystick_is_just_pressed (u8 joycode) fastcall
; return joystick_is_pressed(joycode) && !joystick_was_pressed(joycode);

PUBLIC _joystick_is_just_pressed
_joystick_is_just_pressed:
    PUSH    HL
    CALL    _joystick_is_pressed
    LD      A, L
    CP      A, 0
    JR      Z, popHLret0

    POP     HL
    CALL    _joystick_was_pressed
    LD      A, L
    CP      A, 0
    JR      NZ, ret0
    JR      ret1

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; bool joystick_is_just_released(u8 joycode) fastcall
; return !joystick_is_pressed(joycode) && joystick_was_pressed(joycode);

PUBLIC _joystick_is_just_released
_joystick_is_just_released:
    PUSH    HL
    CALL    _joystick_is_pressed
    LD      A, L
    CP      A, 0
    JR      NZ, popHLret0

    POP     HL
    CALL    _joystick_was_pressed
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

; bool joystick_is_just_pressed_any(void)
; return joystick_is_pressed_any() && !joystick_was_pressed_any();

PUBLIC _joystick_is_just_pressed_any
_joystick_is_just_pressed_any:
    CALL    _joystick_is_pressed_any
    LD      A, L
    CP      A, 0
    JR      Z, ret0

    CALL    _joystick_was_pressed_any
    LD      A, L
    CP      A, 0
    JR      NZ, ret0
    JR      ret1

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; bool joystick_is_just_released_any(void)
; return !joystick_is_pressed_any() && joystick_was_pressed_any();

PUBLIC _joystick_is_just_released_any
_joystick_is_just_released_any:
    CALL    _joystick_is_pressed_any
    LD      A, L
    CP      A, 0
    JR      NZ, ret0

    CALL    _joystick_was_pressed_any
    LD      A, L
    CP      A, 0
    JR      Z, ret0
    JR      ret1


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _joystick_was_pressed_any
_joystick_was_pressed_any:
    LD      A, (_joyprev)
    JR      ASMPC+5     ; skip to CP A, 0 below

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _joystick_is_pressed_any
_joystick_is_pressed_any:
    LD      A, (_joydata)
    CP      A, 0
    LD      L, 0
    RET     Z
    INC     L
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

EXTERN count_set_bits_in_byte

PUBLIC _joystick_count
_joystick_count:
    LD      HL ,0
    LD      A, (_joydata)
    CALL    count_set_bits_in_byte
    RET
    
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _joystick_code
_joystick_code:
    LD      H, 0
    LD      A, (_joydata)
    LD      L, A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

EXTERN  get_first_set_bit

PUBLIC _joystick_short_for_code
_joystick_short_for_code:
    LD      A, L
    LD      L, 0
    call get_first_set_bit

    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _joystick_ch4short
_joystick_ch4short:
    DEFB $13, $12, $11, $10, $0C, $0D, $0E, $0F
