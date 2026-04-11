SECTION code_user

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _spriteset_palette
PUBLIC _spriteset_palette_len

_spriteset_palette:
	INCBIN "../test00assets/spritesetA/spriteset_sp_pal"
_spriteset_palette_len: EQU $-_spriteset_palette

