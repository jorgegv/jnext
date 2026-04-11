SECTION code_user

EXTERN readNextReg

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _layer2_set_resolution
_layer2_set_resolution:
    LD      A, L
    NEXTREG $70, A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _layer2_set_visible
_layer2_set_visible:
    LD      BC, $123B
    LD      A, 2
    OUT     (C), A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _layer2_set_invisible
_layer2_set_invisible:
    LD      BC, $123B
    LD      A, 0
    OUT     (C), A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _layer2_starting_ram_page
_layer2_starting_ram_page:
    DEFB $09

PUBLIC _layer2_set_ram_page
_layer2_set_ram_page:
    LD      A, L
    LD      (_layer2_starting_ram_page), A
    NEXTREG $12, A
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _layer2_set_first_palette_entry
_layer2_set_first_palette_entry:
    NEXTREG $43, %00010000  ; Auto increment, select first layer2 palette
	LD      A, H            ; Load RRRGGGBB into A
	NEXTREG $44, A          ; Send entry to Next HW
	LD      A, L            ; Load 0000000B into A
	NEXTREG $44, A          ; Send entry to Next HW
    RET

PUBLIC _layer2_clear_pages
_layer2_clear_pages:
    LD      A, (_layer2_starting_ram_page)
    RLCA

    LD      B, 5

l2cp_loop:
    NEXTREG $50, A
    INC     A
    NEXTREG $51, A
    INC     A
    PUSH    BC
    CALL    _clear_4k_at_address_0
    POP     BC

    DJNZ    l2cp_loop

    RET

_clear_4k_at_address_0:
    LD      HL, 0
    LD      (HL), 0
    LD      DE, HL
    INC     DE
    LD      BC, $3FFF
    LDIR
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _system_set_global_transparency_0
_system_set_global_transparency_0:
    NEXTREG $14, 0
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PUBLIC _tilemap_set_transparent_index_0
_tilemap_set_transparent_index_0:
    NEXTREG $4C, 0
    RET

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;















;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; NOT USED

; PUBLIC _layer2_setup_320x256
; PUBLIC _layer2_setup_304x256
; PUBLIC _system_set_layers_order_SUL

; _system_set_layers_order_SUL:
;     ; Sprites over ULA/tilemap over Layer2
;     ; https://wiki.specnext.dev/Sprite_and_Layers_System_Register
;     LD      A, $15
;     CALL    readNextReg
;     AND     A, %11100011
;     OR      A, %00001000    ; ---010--
;     NEXTREG $15, A
;     RET

; _layer2_setup_320x256:
;     ; https://wiki.specnext.dev/Layer_2_Control_Register
;     NEXTREG $70, %00010000

;     ; Setup window clip for 320x256 resolution
;     NEXTREG $1C, 1              ; Reset Layer 2 clip window reg index
;     NEXTREG $18, 0              ; X1; X2 next line
;     NEXTREG $18, 159            ; 320 / 2 - 1
;     NEXTREG $18, 0              ; Y1; Y2 next line
;     NEXTREG $18, 255            ; 256 - 1

;     RET

; _layer2_setup_304x256:
;     ; https://wiki.specnext.dev/Layer_2_Control_Register
;     NEXTREG $70, %00010000

;     ; Setup window clip for 320x256 resolution
;     NEXTREG $1C, 1              ; Reset Layer 2 clip window reg index
;     NEXTREG $18, 4              ; X1; X2 next line
;     NEXTREG $18, 155            ; 320 / 2 - 1
;     NEXTREG $18, 0              ; Y1; Y2 next line
;     NEXTREG $18, 255            ; 256 - 1

;     RET
