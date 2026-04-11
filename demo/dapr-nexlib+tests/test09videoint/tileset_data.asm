SECTION TILESET_TS

ORG $6000

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tilemap_data
PUBLIC _tilemap_data_len

_tilemap_data:
	INCBIN "../test00assets/tilesetA/tileset_ts"
_tilemap_data_len: EQU $-_tilemap_data

