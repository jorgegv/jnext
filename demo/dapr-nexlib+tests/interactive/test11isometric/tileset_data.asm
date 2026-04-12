SECTION PAGE_40

ORG $0000

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tileset_data
PUBLIC _tileset_data_len

_tileset_data:
	INCBIN "../../test00assets/tilesetA/tileset_ts"
_tileset_data_len: EQU $-_tileset_data

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

SECTION PAGE_41

ORG $0000

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _screen01_tileset_data
PUBLIC _screen01_tileset_data_len

_screen01_tileset_data:
	INCBIN "../../test00assets/tilemapper/screen01_ts"
_screen01_tileset_data_len: EQU $-_screen01_tileset_data

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

SECTION PAGE_42

ORG $0000

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _iso_tilemap_data
PUBLIC _iso_tilemap_data_len

_iso_tilemap_data:
	INCBIN "../../test00assets/tilemapper/screen01.tilemap.bin"
_iso_tilemap_data_len: EQU $-_iso_tilemap_data
