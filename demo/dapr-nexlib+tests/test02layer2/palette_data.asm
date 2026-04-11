SECTION code_user

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _kanagawa_palette
PUBLIC _kanagawa_palette_len

_kanagawa_palette:
	INCBIN "../test00assets/layer2A/kanagawa_l2_pal"
_kanagawa_palette_len: EQU $-_kanagawa_palette

