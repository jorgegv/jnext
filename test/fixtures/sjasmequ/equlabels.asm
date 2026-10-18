; GH #390 fixture: the entries sjasmplus writes to its --sym and --exp files.
; Build (sjasmplus v1.23.1): see README.md in this directory.
            DEVICE ZXSPECTRUMNEXT

SCREEN      EQU $4000           ; an address constant
SCRSIZE     EQU 6912            ; a size constant (0x1B00)
TOPADDR     EQU $FFFF           ; the highest 16-bit value
PASTTOP     EQU $10000          ; one above it
NEG         EQU -1              ; written as 0xFFFFFFFF
counter     DEFL 5
            EXPORT counter      ; --exp: counter = 5 ...
counter     DEFL 6
            EXPORT counter      ; ... and counter = 6, the same name twice

            STRUCT point        ; a struct: its size and field offsets
x           BYTE 0
y           BYTE 0
            ENDS

            ORG $8000
main:       ld hl,SCREEN
            ld bc,SCRSIZE
.loop:      dec bc              ; a local label: main.loop
            ld a,b
            or c
            jr nz,.loop
1:          djnz 1B             ; a temporary label
            call gfx.draw
            call banked
            ret
pos         point               ; a struct instance: pos, pos.x, pos.y

            MODULE gfx
draw:       ld a,1              ; a module label: gfx.draw
.loop:      dec a               ; gfx.draw.loop
            jr nz,.loop
            ret
            EXPORT draw         ; inside the module: written as "draw"
            ENDMODULE

            MMU 6, 20           ; page 20 at $C000
            ORG $C000
banked:     ld a,20             ; $C000 in page 20
            ret
            MMU 6, 30           ; page 30 at $C000
            ORG $C000
other_bank: ld a,30             ; $C000 again, in page 30
            ret

            EXPORT main
            EXPORT main.loop
            EXPORT gfx.draw
            EXPORT SCREEN
            EXPORT SCRSIZE
            EXPORT TOPADDR
            EXPORT PASTTOP
            EXPORT NEG
            EXPORT pos.y
            EXPORT point.y
            EXPORT banked
            EXPORT other_bank
