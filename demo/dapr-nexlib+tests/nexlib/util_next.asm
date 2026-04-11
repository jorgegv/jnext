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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC readNextReg

readNextReg:
; Input
;       A = nextreg to read
; Output:
;       A = value in nextreg
; Uses:
;       A, [currently selected NextReg on I/O port $243B]
    push    bc
    ld      bc, $243B   ; TBBLUE_REGISTER_SELECT_P_243B
    out     (c),a
    inc     b       ; bc = TBBLUE_REGISTER_ACCESS_P_253B
    in      a,(c)   ; read desired NextReg state
    pop     bc
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _set_cpu_speed_28

_set_cpu_speed_28:
; CPU SPEED
; register $07, bits 1-0
; %00 = 3.5MHz
; %01 = 7MHz
; %10 = 14MHz
; %11 = 28MHz (works since core 3.0)
    ld      a, $07
    call    readNextReg
    and     a, %11111100
    or      a, %00000011
    nextreg $07, a
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Peripheral 3 $08
; Bit Effect
;  7   1 unlock / 0 lock port Memory Paging Control $7FFD (page 41) paging
;  6   1 to disable RAM and I/O port contention (0 after soft reset)
;  5   AY stereo mode (0 = ABC, 1 = ACB) (0 after hard reset)
;  4   Enable internal speaker (1 after hard reset)
;  3   Enable 8-bit DACs (A,B,C,D) (0 after hard reset)
;  2   Enable port $FF Timex video mode read (0 after hard reset)
;  1   Enable Turbosound (currently selected AY is frozen when disabled) (0 after hard reset)
;  0   Implement Issue 2 keyboard (port $FE reads as early ZX boards) (0 after hard reset

PUBLIC _disable_contention

_disable_contention:
    LD      A, $08
    CALL    readNextReg
    OR      A, %01000000
    NEXTREG $08, A
    RET

PUBLIC _enable_dacs

_enable_dacs:
    LD      A, $08
    CALL    readNextReg
    OR      A, %00001000
    NEXTREG $08, A
    RET

PUBLIC _switchSlotPage8k

_switchSlotPage8k:
    POP     DE      ; return address
    POP     HL
    PUSH    HL
    PUSH    DE
    LD      (ASMPC+5), HL
    ; NEXTREG $12, $34 -> ED 91 12 34
    DEFB    $ED, $91, $00, $00

    RET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _selectTilemapPaletteFirst
PUBLIC _selectTilemapPaletteSecond

_selectTilemapPaletteFirst:
    LD      A, $6B
    CALL    readNextReg
    AND     A, %11101111
    NEXTREG $6B, A
    RET

_selectTilemapPaletteSecond:
    LD      A, $6B
    CALL    readNextReg
    OR      A, %00010000
    NEXTREG $6B, A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _disableULA
_disableULA:
    LD      A, $68
    CALL    readNextReg
    OR      A, %10000000
    NEXTREG $68, A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _disable_8x5_entries_for_extended_keys
_disable_8x5_entries_for_extended_keys:
    LD      A, $68
    CALL    readNextReg
    OR      A, %00010000
    NEXTREG $68, A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _getActiveVideoLineWord

_getActiveVideoLineWord:
    LD      A, $1E
    CALL    readNextReg
    AND     A, 1
    LD      H, A
    LD      A, $1F
    CALL    readNextReg
    LD      L, A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _fillBitmapArea
_fillBitmapArea:
    POP     HL      ; return address
    POP     BC      ; value to fill
    PUSH    BC
    PUSH    HL
    LD      HL, $4000
    LD      DE, HL
    LD      A, B
    LD      (DE), A
    INC     DE
    LD      A, C
    LD      (DE), A
    INC     DE
    LD      BC, $17FE
    LDIR
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _fillAttributeArea

_fillAttributeArea:
    POP     HL      ; return address
    POP     BC      ; value to fill
    PUSH    BC
    PUSH    HL
    LD      HL, $5800
    LD      DE, HL
    LD      A, B
    LD      (DE), A
    INC     DE
    LD      A, C
    LD      (DE), A
    INC     DE
    LD      BC, $2FE
    LDIR
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _fillTilemapArea
_fillTilemapArea:
    POP     HL      ; return address
    POP     BC      ; value to fill
    PUSH    BC
    PUSH    HL
    LD      HL, $4000
    LD      DE, HL
    LD      A, B
    LD      (DE), A
    INC     DE
    LD      A, C
    LD      (DE), A
    INC     DE
    LD      BC, $0FFE
    LDIR
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _get_sp

_get_sp:
    LD      HL, SP
    RET

PUBLIC _set_sp

_set_sp:
    POP     BC
    POP     HL
    LD      SP, HL
    LD      HL, BC
    DEC     HL
    DEC     HL
    JP      (HL)