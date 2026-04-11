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

;---------------------------------------------------------------------

PUBLIC _sprite_load_defs

EXTERN sprite_defs
EXTERN sprite_defs_len

_sprite_load_defs:
	; load bank/page for game sprite data
 	; NEXTREG $50, 74             ; page must match sprite_defs.asm
	; NEXTREG $51, 75

	; ; Load sprite data using DMA
	; LD HL, sprite_defs              ; sprite data source
    ;                                 ; SEE sprite_defs.asm!
	; LD BC, sprite_defs_len          ; Copy sprites, each 16x16 pixels
	; LD A, 0						    ; Start with first sprite
	; CALL sprite_load_def_to_fpga	; Load sprites to FPGA
    RET

;---------------------------------------------------------------------

PUBLIC _sprite_setup

_sprite_setup:
  	; NEXTREG $15, %01000011		; sprite 0 on top, SLU, over border, sprites visible
  	; NEXTREG $15, %01001011		; sprite 0 on top, SUL, over border, sprites visible
	; NEXTREG $15, %00100011	 ; sprite 0 on bottom, SLU, over border, sprites visible

	; Setup sprite transparency index
	NEXTREG $4B, %00001111
    RET

;---------------------------------------------------------------------
; DMA FIX FOR SPRITE UPLOAD
; Initially sprite upload works correctly, but after a dma_transfer_sample,
; subsequents uploads cause sprite corruption (on CSpect) and slow transfer (~2s).
; A simple "reset dma" command does the trick.
;---------------------------------------------------------------------
; SPRITE UPLOAD TO FPGA PATTERN MEMORY
; HL = address of sprite sheet in memory
; BC = number of bytes to load
; A  = index of first sprite to load
sprite_load_def_to_fpga:
	LD (dma_source), HL	        ; Copy sprite sheet address from HL
	LD (dma_length), BC	        ; Copy length in bytes from BC
	LD BC, $303B		        ; Prepare port for sprite index
	OUT (C), A		            ; Load index of first sprite
	LD HL, dma_program	        ; Setup source for OTIR
	LD B, dma_program_length	; Setup length for OTIR
	LD C, $6B		            ; Setup DMA port
	OTIR			            ; Invoke DMA code
	RET
dma_program:
	DB %10000011		        ; WR6 - Disable DMA
	DB 0xc3                     ; r6-reset dma - NEEDED TO FIX SPRITE CORRUPTION AFTER PLAYING DMA SAMPLES
	; DB 0xc7                   ; r6-reset port a timing
	; DB 0xcb                   ; r6-reset port b timing
	DB %01111101		        ; WR0 - append length + port A address, A->B
dma_source:
	DW 0			            ; WR0 par 1&2 - port A start address
dma_length:
	DW 0			            ; WR0 par 3&4 - transfer length
	DB %00010100		        ; WR1 - A incr, A=memory
	DB %00101000		        ; WR2 - B fixed, B=I/O
	DB %10101101		        ; WR4 - continuous, append port B address
	DW $005B		            ; WR4 par 1&2 - port B address
	DB %10000010		        ; WR5 - stop on end of block, CE only
	DB %11001111		        ; WR6 - load addresses into DMA counters
	DB %10000111		        ; WR6 - enable DMA
dma_program_length = $-dma_program

;---------------------------------------------------------------------

PUBLIC _sprite_clear_all_slots
_sprite_clear_all_slots:
	LD		L, 0
_sprite_clear_all_slots_loop:
	LD      A, L
	NEXTREG $34, A
	NEXTREG $38, 0
	INC     L
	LD		A, L
	CP      A, 128
	JR		NZ, _sprite_clear_all_slots_loop
	RET

;---------------------------------------------------------------------

PUBLIC _sprite_clear_all_from
_sprite_clear_all_from:
    ; retrieve parameter restoring stack
    POP     BC      ; return address
    POP     HL      ; initial sprite slot
    PUSH    HL      ; restore stack
    PUSH    BC      ; restore stack

_sprite_clear_all_from_loop:
	LD      A, L
	NEXTREG $34, A
	NEXTREG $38, 0
	INC     L
	LD		A, L
	CP      A, 128
	JR		NZ, _sprite_clear_all_from_loop
	RET

;---------------------------------------------------------------------

PUBLIC _sprite_clear_slot
_sprite_clear_slot:
    ; retrieve parameter restoring stack
    POP     BC      ; return address
    POP     HL      ; sprite slot
    PUSH    HL      ; restore stack
    PUSH    BC      ; restore stack

	LD      A, L
	NEXTREG $34, A
	NEXTREG $38, 0
	RET


;---------------------------------------------------------------------

; tightly coupled with SpriteDef structure

PUBLIC _sprite_update_asm
_sprite_update_asm:
    ; retrieve parameter restoring stack
    POP     BC      ; return address
    POP     HL      ; sprite address
    PUSH    HL      ; restore stack
    PUSH    BC      ; restore stack

    LD      A, (HL) ; slot
    NEXTREG $34, A

    INC     HL
    LD      A, (HL) ; lo x
    NEXTREG $35, A

    INC     HL
    LD      A, (HL) ; hi x
    AND     A, 1
    LD      B, A    ; B := (x >> 8) & 1

    INC     HL
    LD      A, (HL) ; lo y
    NEXTREG $36, A

    INC     HL
    LD      A, (HL) ; hi y
    AND     A, 1
    LD      D, A    ; D := (y >> 8) & 1

    INC     HL
    LD      A, (HL) ; pal
    AND     A, $0F
    SWAPNIB
    LD      C, A    ; C := (pal & 0x0F) << 4

    INC     HL
    LD      A, (HL) ; mirrot
    AND     A, $0E  ; A := (mirrot & 0x0E)

    OR      A, C
    OR      A, B
    NEXTREG $37, A

    INC     HL
    LD      A, (HL) ; pat
    LD      C, A    ; C := pat
    RRCA
    OR      A, 0xC0 ; A := 0xC0 & (pat >> 1)
    NEXTREG $38, A

    LD      A, C
    AND     A, 1
    SWAPNIB
    RLCA
    RLCA
    OR      A, $A0
    LD      C, A    ; C := 0x80 | ((pat & 1) << 6)

    INC     HL
    LD      A, (HL)
    AND     A, $1E
    OR      A, C
    NEXTREG $39, A

	RET

    ; ZXN_WRITE_REG(0x34, sprdef->slot);
    ; ZXN_WRITE_REG(0x35, sprdef->x);
    ; ZXN_WRITE_REG(0x36, sprdef->y);
    ; u8 val37 = JOINIB(sprdef->pal, sprdef->mirrot);
    ; val37 |= (sprdef->x >> 8) & 1;
    ; ZXN_WRITE_REG(0x37, val37);
    ; ZXN_WRITE_REG(0x38, 0xC0 | (sprdef->pat >> 1));
    ; u8 val39 = 0x80 | ((sprdef->pat & 1) << 6);
    ; val39 |= (sprdef->y >> 8) & 1;
    ; val39 |= sprdef->scale;
    ; ZXN_WRITE_REG(0x39, val39);

; PUBLIC _sprite_list
; _sprite_list:
;     DEFS 768, 0

; PUBLIC _sprite_draw_list
; _sprite_draw_list:
;     LD      HL, _sprite_list
;     LD      DE, 6
;     LD      C, 0
; _sdl_loop:
;     LD      A, (HL)
    
;     DJNZ    _sdl_loop

