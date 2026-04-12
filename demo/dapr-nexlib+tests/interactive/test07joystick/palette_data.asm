SECTION code_user

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tileset_palette
PUBLIC _tileset_palette_len

_tileset_palette:
	INCBIN "../../test00assets/tilesetA/tileset_ts_pal"
_tileset_palette_len: EQU $-_tileset_palette

