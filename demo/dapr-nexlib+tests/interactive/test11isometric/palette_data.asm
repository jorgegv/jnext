SECTION code_user

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tileset_palette
PUBLIC _tileset_palette_len

_tileset_palette:
	INCBIN "../../test00assets/tilesetA/tileset_ts_pal"
_tileset_palette_len: EQU $-_tileset_palette

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _screen01_palette
PUBLIC _screen01_palette_len

_screen01_palette:
	INCBIN "../../test00assets/tilemapper/screen01_ts_pal"
_screen01_palette_len: EQU $-_screen01_palette

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _spriteset_palette
PUBLIC _spriteset_palette_len

_spriteset_palette:
	INCBIN "../../test00assets/spriteset_boy/boy32_sp_pal"
_spriteset_palette_len: EQU $-_spriteset_palette



