; ZX Spectrum Next -- HALT-idle bench workloads (GH #452)
;
; One source, three workloads for `make bench` (test/bench/bench.sh). Each NEX
; writes its OWN CPU speed (NR 0x07) after load, because the warm-started NEX
; path ignores `--cpu-speed` (NextZXOS sets the speed it boots with), so the
; workload would otherwise run at whatever NextZXOS left.
;
;   halt_idle_35   3.5 MHz  EI;HALT, an AY tone ISR every frame, a Copper list
;                           that spends the frame in unmet WAITs
;   halt_idle_28   28 MHz   the same
;   ctc_dac_28     28 MHz   EI;HALT, CTC channel 0 timer interrupts at ~16 kHz,
;                           the ISR writes a DAC sample (CTC sample playback)
;
; Build (defines select the workload; see the Makefile):
;   zcc +zxn -vn -startup=31 -clib=sdcc_ix halt_bench.asm -subtype=nex \
;       -create-app -Ca-DSPEED=3 -Ca-DCTCDAC=1 -o ctc_dac_28.bin
;
; The Copper list, fallback colour NR 0x4A at three lines (WAIT/MOVE pairs, then
; HALT = WAIT with vpos 511, never matches), mode 11 = restart every frame:
;   copper.vhd:80 (restart), :94 (the WAIT comparison).
; CTC: control word 0x87 = interrupt enable, timer mode, prescaler 16, auto
; trigger, time constant follows, software reset, control word. 28 MHz / (16 * 109)
; = 16.06 kHz. Ports 0x183B.. and the interrupt enable NR 0xC5 b0: ctc.vhd.
; DAC: NR 0x08 b3 enables it; port 0xDF is the Specdrum mono port (zxnext.vhd:2661).

        IFNDEF SPEED
        defc SPEED = 0
        ENDIF
        IFNDEF CTCDAC
        defc CTCDAC = 0
        ENDIF

        PUBLIC _main
        SECTION code_user

_main:
        di
        nextreg $07, SPEED              ; CPU speed: 0 = 3.5 MHz, 3 = 28 MHz

        ; IM 2 vector table at $D000..$D100, every entry $D1; handler at $D1D1
        ld hl, $D000
        ld de, $D001
        ld bc, 256
        ld (hl), $D1
        ldir
        ld a, $C3                       ; JP isr
        ld ($D1D1), a
        ld hl, isr
        ld ($D1D2), hl

        IF CTCDAC
        nextreg $22, $04                ; the ULA frame interrupt off: the CTC is the only source
        nextreg $C5, $01                ; CTC channel 0 may interrupt
        ld bc, $243B                    ; DAC on: NR 0x08 b3
        ld a, $08
        out (c), a
        ld bc, $253B
        in a, (c)
        or $08
        out (c), a
        ld bc, $183B
        ld a, $87
        out (c), a
        ld a, 109
        out (c), a
        ELSE
        ; AY: tone A on, volume 15
        ld bc, $FFFD
        ld a, 7
        out (c), a
        ld bc, $BFFD
        ld a, $3E
        out (c), a
        ld bc, $FFFD
        ld a, 8
        out (c), a
        ld bc, $BFFD
        ld a, $0F
        out (c), a
        ; Copper list
        nextreg $62, $00                ; stop, address high = 0
        nextreg $61, $00                ; address low = 0
        ld hl, copper_list
        ld b, copper_list_end - copper_list
cl:     ld a, (hl)
        inc hl
        nextreg $63, a
        djnz cl
        nextreg $62, $C0                ; mode 11: restart every frame
        ENDIF

        ld a, $D0
        ld i, a
        im 2
        ei
idle:   halt
        jr idle

isr:
        push af
        push bc
        ld a, (tick)
        inc a
        ld (tick), a
        IF CTCDAC
        out ($DF), a                    ; a DAC sample
        ELSE
        ld bc, $FFFD                    ; AY register 0 = tone A period low
        push af
        xor a
        out (c), a
        ld bc, $BFFD
        pop af
        out (c), a
        ENDIF
        pop bc
        pop af
        ei
        reti

        IF CTCDAC = 0
copper_list:
        defb $80, $3C, $4A, $E0         ; WAIT line 60;  MOVE NR 0x4A, $E0
        defb $80, $82, $4A, $1C         ; WAIT line 130; MOVE NR 0x4A, $1C
        defb $80, $C8, $4A, $03         ; WAIT line 200; MOVE NR 0x4A, $03
        defb $FF, $FF                   ; HALT
copper_list_end:
        ENDIF

tick:   defb 0
