; Source of l2_midline_bank.bin — the fixture layer2-midline-bank-func injects
; (GH #270). Assembled with z88dk's z80asm:
;
;   z80asm -b -o l2_midline_bank.bin l2_midline_bank.asm
;
; WHAT IT IS: a straight re-implementation, in this project's own idiom, of
; the reporter's repro at https://github.com/vmorilla/jnext-copper-bug
; (cases/layer2-bank-midline). It paints two Layer 2 screens in flat colour,
; turns the ULA off, and runs a Copper list that switches the Layer 2 active
; bank (NR 0x12) TWICE on each of 144 consecutive scanlines:
;
;   line   0        : MOVE NR 0x12 = 8        (once, from the list head)
;   lines 48..191   : WAIT(line, hpos=8)  MOVE NR 0x12 = 8
;                     WAIT(line, hpos=32) MOVE NR 0x12 = 11
;
; Lines 0..47 are the control band — same register, same Copper, one write
; per frame — and prove the per-LINE half of the mechanism still works.
;
; WHAT IT SHOWS: on hardware (MAME 0.289 `tbblue`, the reporter's reference)
; each banded line is bank 11 up to display column 66 and bank 8 from column
; 67 on. hpos 32 resolves to column 259, past the right edge, so it is
; invisible on its own line and only leaves bank 11 selected for the start of
; the next one — which is why hardware shows two regions per line, not three.
;
; Before GH #270 jnext applied every write tagged with a scanline BEFORE
; drawing it, so the second write won and the whole band rendered from
; bank 11 alone: not one pixel of bank 8, on any of the 144 lines.
;
; VHDL basis:
;   copper.vhd:94        WAIT fires on hcount_i >= (hpos & "000") + 12, and
;                        hcount_i IS hc_ula (zxnext.vhd:3949, GH #181)
;   layer2.vhd:110-122   layer2_active_bank_q resamples NR 0x12 per CLK_7
;   layer2.vhd:145-148   hc_eff = (phc|whc) + 1 is the source column whose
;                        address that period generates
;   zxnext.vhd:4709-4731 the Copper -> NextREG write takes three further
;                        28 MHz cycles (copper_dout_s, copper_requester_d,
;                        copper_req)
;   zxula_timing.vhd:476-520  phc/whc reload from the raw frame counter
; The full derivation, and the arithmetic that lands the boundary on column
; 67, is in seg_first_col() in src/video/layer2.cpp.
;
; Colours are chosen to be unambiguous and non-transparent: 0xE0 is pure red
; and 0x1C pure green in the default Layer 2 RRRGGGBB palette, and neither
; equals the NR 0x14 default transparency index 0xE3 (renderer.h:117), so
; every pixel of both banks is opaque.

RED     equ  $E0
GREEN   equ  $1C

        org  $8000

start:
        di

        ; ---- paint bank 8 (NR 0x12, the active bank) solid red ----------
        ; Port 0x123B bit 0 maps a 16K third of the Layer 2 bitmap over
        ; 0x0000-0x3FFF for WRITES; bits 7:6 pick the third; bit 3 selects
        ; the shadow bank (NR 0x13) instead of the active one.
        ld   a, RED
        ld   d, $01                 ; third 0, write-over, active bank
        call fill_third
        ld   a, RED
        ld   d, $41                 ; third 1
        call fill_third
        ld   a, RED
        ld   d, $81                 ; third 2
        call fill_third

        ; ---- paint bank 11 (NR 0x13, the shadow bank) solid green -------
        ld   a, GREEN
        ld   d, $09                 ; third 0, write-over, shadow bank
        call fill_third
        ld   a, GREEN
        ld   d, $49
        call fill_third
        ld   a, GREEN
        ld   d, $89
        call fill_third

        ; ---- drop the write-over, switch the Layer 2 display on ---------
        ld   bc, $123B
        ld   a, $02                 ; bit 1 = Layer 2 visible
        out  (c), a

        ; ---- video registers -------------------------------------------
        ld   de, $6880              ; NR 0x68 b7 = 1: ULA off, nothing but L2
        call nreg
        ld   de, $7000              ; NR 0x70 = 256x192 8bpp, palette offset 0
        call nreg
        ld   de, $1600              ; NR 0x16 = 0: no X scroll
        call nreg
        ld   de, $1700              ; NR 0x17 = 0: no Y scroll
        call nreg
        ld   de, $1208              ; NR 0x12 = 8  (active bank)
        call nreg
        ld   de, $130B              ; NR 0x13 = 11 (shadow bank)
        call nreg

        ; ---- upload the Copper list ------------------------------------
        ld   de, $6100              ; NR 0x61 = 0: write pointer low
        call nreg
        ld   de, $6200              ; NR 0x62 = 0: mode 00 (stopped), ptr high
        call nreg

        ld   hl, $1208              ; MOVE NR 0x12, 8
        call cu_word

        ld   a, 48
line_loop:
        ld   (cur_line), a
        ld   h, $90                 ; WAIT: 1 & hpos(6)=8 & vpos(9)
        ld   l, a
        call cu_word
        ld   hl, $1208              ; MOVE NR 0x12, 8
        call cu_word
        ld   a, (cur_line)
        ld   h, $C0                 ; WAIT: 1 & hpos(6)=32 & vpos(9)
        ld   l, a
        call cu_word
        ld   hl, $120B              ; MOVE NR 0x12, 11
        call cu_word
        ld   a, (cur_line)
        inc  a
        cp   192
        jr   nz, line_loop

        ld   hl, $81FF              ; HALT (WAIT vpos=511, never matches)
        call cu_word

        ld   de, $62C0              ; NR 0x62 = mode 11: restart each frame
        call nreg

park:
        jr   park

; ---- fill the mapped 16K third with A ----------------------------------
; A = byte, D = port 0x123B value
fill_third:
        ld   e, a
        ld   bc, $123B
        out  (c), d
        ld   a, e
        ld   hl, $0000
        ld   b, 64                  ; 64 x 256 = 16384
ft_outer:
        ld   c, 0                   ; 0 -> 256 iterations via DEC C
ft_inner:
        ld   (hl), a
        inc  hl
        dec  c
        jr   nz, ft_inner
        djnz ft_outer
        ret

; ---- push one 16-bit Copper instruction (MSB first) --------------------
; HL = instruction word
cu_word:
        ld   d, $63
        ld   e, h
        call nreg
        ld   d, $63
        ld   e, l
        jp   nreg

; ---- NextREG write: D = register, E = value ----------------------------
nreg:
        ld   bc, $243B
        out  (c), d
        ld   b, $25                 ; BC = 0x253B
        out  (c), e
        ret

cur_line:
        defb 0
