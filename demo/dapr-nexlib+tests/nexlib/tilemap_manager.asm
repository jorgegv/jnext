;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
;  (c) 202f David Crespo - https://github.com/dcrespo3d
;                          https://davidprograma.itch.io
;                          https://www.youtube.com/@Davidprograma
;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

SECTION code_user

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tilemap_setup

_tilemap_setup:
	CALL	_tilemap_set_40col
	LD		L, $40
	CALL	_tilemap_set_tilemap_seg
	LD		L, $60
	CALL	_tilemap_set_tileset_seg
	RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tilemap_disable
_tilemap_disable:
	NEXTREG $6B, 0
	RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; tilemap starting address
PUBLIC _tmstart
_tmstart:
	DEFW    $4000

; tilemap length in bytes
_tmlen:
	DEFW	$A00

TMLEN40: EQU $0A00
TMLEN80: EQU $1400

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; self-modifying code point in print_set_pos
EXTERN PSPA_SMC

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tilemap_set_40col
_tilemap_set_40col:
	NEXTREG $6B, %10000001		; 40x32, 16-bit entries

	XOR 	A
	LD 		(PSPA_SMC), A		; Y * 1

	LD		HL, TMLEN40
	LD		(_tmlen), HL

	RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tilemap_set_80col
_tilemap_set_80col:
	NEXTREG $6B, %11000001		; 80x32, 16-bit entries

	LD		A, $29
	LD 		(PSPA_SMC), A		; Y * 2

	LD		HL, TMLEN80
	LD		(_tmlen), HL

	RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tilemap_set_tilemap_seg
_tilemap_set_tilemap_seg:
	LD		A, L
	NEXTREG $6E, A	; MSB of tilemap in bank 5
	LD		H, L
	LD		L, 0
	LD		(_tmstart), HL
	RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tilemap_set_tileset_seg
_tilemap_set_tileset_seg:
	LD		A, L
	NEXTREG $6F, A	; MSB of tilemap definitions
	RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tilemap_clear_buf_value
_tilemap_clear_buf_value:
	; save argument into DE
	LD		DE, HL
	; put tmlen-2 into BC
	LD		HL, (_tmlen)
	LD		BC, HL
	DEC		BC
	DEC		BC

	; source pointer
	LD		HL, (_tmstart)

	; copy argument memory pointed by source pointer
	LD		(HL), E
	INC		HL
	LD		(HL), D
	INC		HL
	; note: source pointer has been incremented twice
	; so now it is destination pointer

	; put destination pointer into DE
	LD		DE, HL

	; decrement twice to put source pointer into HL
	DEC     HL
	DEC		HL

	; copy and return
	LDIR
	RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tilemap_set_hoffset
_tilemap_set_hoffset:
	LD		A, H
	AND     A, %00000011
	NEXTREG $2F, A
	LD		A, L
	NEXTREG $30, A
	RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tilemap_set_voffset
_tilemap_set_voffset:
	LD		A, L
	NEXTREG $31, A
	RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

