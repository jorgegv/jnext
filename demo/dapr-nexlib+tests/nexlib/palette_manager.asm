;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
;  (c) 2025 David Crespo - https://github.com/dcrespo3d
;                          https://davidprograma.itch.io
;                          https://www.youtube.com/@Davidprograma
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

SECTION code_user

;---------------------------------------------------------------------

EXTERN _tileset_palette

PUBLIC _palette_load_tilemap
PUBLIC _palette_load_tilemap_second
PUBLIC _palette_load_sprites
PUBLIC _palette_load_layer2
PUBLIC _palette_load_layer2_16col

;---------------------------------------------------------------------
_palette_load_tilemap:
    NEXTREG $43, %00110000		    ; Auto increment, select first tilemap palette
    JR      palette_load_common

_palette_load_tilemap_second:
    NEXTREG $43, %01110000		    ; Auto increment, select second tilemap palette
    JR      palette_load_common

_palette_load_sprites:
    NEXTREG $43, %00100000		    ; Auto increment, select first sprite palette
    JR      palette_load_common

_palette_load_layer2:
    NEXTREG $43, %00010000		    ; Auto increment, select first layer2 palette
    JR      palette_load_common

_palette_load_layer2_16col:
    NEXTREG $43, %00010000		    ; Auto increment, select first layer2 palette
    JR      palette_load_common_2

palette_load_common:
    NEXTREG $40, 0

    ; retrieve parameters restoring stack
    POP     DE      ; return address
    POP     HL      ; palette address
    PUSH    HL      ; restore stack
    PUSH    DE      ; restore stack

    ; Copy palette
    LD      B,  0
    CALL    palette_load		    ; Call routine for copying

    RET

palette_load_common_2:
    NEXTREG $40, 0

    ; retrieve parameters restoring stack
    POP     DE      ; return address
    POP     HL      ; palette address
    PUSH    HL      ; restore stack
    PUSH    DE      ; restore stack

    ; Copy palette
    LD      B,  16
    CALL    palette_load		    ; Call routine for copying

    RET

;---------------------------------------------------------------------
; HL = memory location of the palette
palette_load_256:
	LD B, 0			; This variant always starts with 0
;---------------------------------------------------------------------
; HL = memory location of the palette
; B = number of colours to copy
palette_load:
	LD A, (HL)			; Load RRRGGGBB into A
	INC HL				; Increment to next entry
	NEXTREG $44, A		; Send entry to Next HW
	LD A, (HL)			; Load 0000000B into A
	INC HL				; Increment to next entry
	NEXTREG $44, A		; Send entry to Next HW
	DJNZ palette_load	; Repeat until B=0
	RET

;---------------------------------------------------------------------

