SECTION code_user

PUBLIC _clip_ctr_all
PUBLIC _clip_ctr_layer2
PUBLIC _clip_ctr_sprites
PUBLIC _clip_ula
PUBLIC _clip_ctr_tilemap

_clip_ctr_all:
    POP     BC      ; return address
    POP     DE      ; DE: width
    POP     HL      ; HL: height
    PUSH    HL      ; restore stack
    PUSH    DE      ; restore stack
    PUSH    BC      ; restore stack
    CALL    _clip_calc_dehl
    CALL    _clip_layer2_dehl
    CALL    _clip_sprites_dehl
    CALL    _clip_tilemap_dehl
    RET

_clip_ctr_layer2:
    POP     BC      ; return address
    POP     DE      ; DE: width
    POP     HL      ; HL: height
    PUSH    HL      ; restore stack
    PUSH    DE      ; restore stack
    PUSH    BC      ; restore stack
    CALL    _clip_calc_dehl
    CALL    _clip_layer2_dehl
    RET

_clip_ctr_sprites:
    POP     BC      ; return address
    POP     DE      ; DE: width
    POP     HL      ; HL: height
    PUSH    HL      ; restore stack
    PUSH    DE      ; restore stack
    PUSH    BC      ; restore stack
    CALL    _clip_calc_dehl
    CALL    _clip_sprites_dehl
    RET

_clip_ula:
    POP     BC      ; return address
    POP     DE      ; DE: width
    POP     HL      ; HL: height
    PUSH    HL      ; restore stack
    PUSH    DE      ; restore stack
    PUSH    BC      ; restore stack
    LD      A, D
    LD      D, E
    LD      E, A
    LD      A, H
    LD      H, L
    LD      L, A
    CALL    _clip_ula_dehl
    RET

_clip_ctr_tilemap:
    POP     BC      ; return address
    POP     DE      ; DE: width
    POP     HL      ; HL: height
    PUSH    HL      ; restore stack
    PUSH    DE      ; restore stack
    PUSH    BC      ; restore stack
    CALL    _clip_calc_dehl
    CALL    _clip_tilemap_dehl
    RET

_clip_calc_dehl:
    ; D <- ((320 - WID) / 4)
    ; E <- ((320 - WID) / 4) + (WID / 2)
    RR      D
    RR      E
    LD      A, 160
    SUB     A, E
    LD      D, A
    SRL     D
    LD      A, E
    ADD     A, D
    SUB     A, 1
    LD      E, A
    ; H <- ((256 - HEI) / 2)
    ; L <= ((256 - HEI) / 2) + (HEI)
    RR      H
    RR      L
    LD      A, 128
    SUB     A, L
    LD      H, A
    SRL     A
    ADD     A, L
    SLA     A
    SUB     A, 1
    LD      L, A
    RET

_clip_layer2_dehl:
    NEXTREG $1C, 1              ; Reset Layer 2 clip window reg index
    LD      A, D
    NEXTREG $18, A              ; X1
    LD      A, E
    NEXTREG $18, A              ; (X2 / 2) - 1
    LD      A, H
    NEXTREG $18, A              ; Y1
    LD      A, L
    NEXTREG $18, A              ; Y2 - 1
    RET

_clip_sprites_dehl:
    NEXTREG $1C, 2              ; Reset sprites clip window reg index
    LD      A, D
    NEXTREG $19, A               ; X1
    LD      A, E
    NEXTREG $19, A              ; (X2 / 2) - 1
    LD      A, H
    NEXTREG $19, A              ; Y1
    LD      A, L
    NEXTREG $19, A              ; Y2 - 1
    RET

_clip_ula_dehl:
    NEXTREG $1C, 4              ; Reset ULA clip window reg index
    LD      A, D
    NEXTREG $1A, A              ; X1
    LD      A, E
    NEXTREG $1A, A              ; (X2 / 2) - 1
    LD      A, H
    NEXTREG $1A, A              ; Y1
    LD      A, L
    NEXTREG $1A, A              ; Y2 - 1
    RET

_clip_tilemap_dehl:
    NEXTREG $1C, 8              ; Reset Tilemap clip window reg index
    LD      A, D
    NEXTREG $1B, A              ; X1
    LD      A, E
    NEXTREG $1B, A              ; (X2 / 2) - 1
    LD      A, H
    NEXTREG $1B, A              ; Y1
    LD      A, L
    NEXTREG $1B, A              ; Y2 - 1
    RET

