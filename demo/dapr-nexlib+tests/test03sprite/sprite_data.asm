SECTION PAGE_30

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _spriteset_data
PUBLIC _spriteset_data_len

_spriteset_data:
	INCBIN "../test00assets/spritesetA/spriteset_sp"
_spriteset_data_len: EQU $-_spriteset_data

