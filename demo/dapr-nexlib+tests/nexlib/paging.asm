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

EXTERN readNextReg

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

_prevPageAtSlot0:
    DB 0
_prevPageAtSlot1:
    DB 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _storePrevPagesAtSlots0and1
_storePrevPagesAtSlots0and1:
    ; store page at slot 0
    LD      A, $50
    CALL    readNextReg
    LD      (_prevPageAtSlot0), A
    ; store page at slot 1
    LD      A, $51
    CALL    readNextReg
    LD      (_prevPageAtSlot1), A
    ;
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _restorePrevPagesAtSlots0and1
_restorePrevPagesAtSlots0and1:
    ; store page at slot 0
    LD      A, (_prevPageAtSlot0)
    NEXTREG $50, A
    ; store page at slot 1
    LD      A, (_prevPageAtSlot1)
    NEXTREG $51, A
    ;
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _putPagesAtSlots0and1
_putPagesAtSlots0and1:
    POP     BC      ; return address
    POP     HL      ; page number
    PUSH    HL      ; restore stack
    PUSH    BC      ; restore stack
    ; put page at slot 0
    LD      A, L
    NEXTREG $50, A
    ; put page at slot 1
    INC     A
    NEXTREG $51, A
    ;
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _putRomPagesAtSlots0and1
_putRomPagesAtSlots0and1:
    ; put ROM page at slot 0
    NEXTREG $50, $FF
    ; put ROM page at slot 1
    INC     A
    NEXTREG $51, $FF
    ;
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

_prevPageAtSlot2:
    DB 0
_prevPageAtSlot3:
    DB 0

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _storePrevPagesAtSlots2and3
_storePrevPagesAtSlots2and3:
    ; store page at slot 2
    LD      A, $52
    CALL    readNextReg
    LD      (_prevPageAtSlot2), A
    ; store page at slot 3
    LD      A, $53
    CALL    readNextReg
    LD      (_prevPageAtSlot3), A
    ;
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _restorePrevPagesAtSlots2and3
_restorePrevPagesAtSlots2and3:
    ; store page at slot 2
    LD      A, (_prevPageAtSlot2)
    NEXTREG $52, A

    ; store page at slot 3
    LD      A, (_prevPageAtSlot3)
    NEXTREG $53, A
    ;
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _putPagesAtSlots2and3
_putPagesAtSlots2and3:
    POP     BC      ; return address
    POP     HL      ; page number
    PUSH    HL      ; restore stack
    PUSH    BC      ; restore stack
    ; put page at slots 2
    LD      A, L
    NEXTREG $52, A
    ; put page at slots 3
    INC     A
    NEXTREG $53, A
    ;
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
