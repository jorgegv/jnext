; rzx_dma_demo — a 48K program whose picture is driven by what the DMA reads
; from a port, every frame (GH #283).
;
; Build: `make` in this directory (z88dk's z80asm), `make install` copies the
; binary to test/00regression/bin/, where rzx-dma-func injects it:
;
;   jnext --machine 48k --inject rzx_dma_demo.bin
;
; WHY IT EXISTS: an RZX recording stores every value the machine reads from a
; port, and a playback answers those reads from the recording. The DMA's port
; reads used to bypass both, so a replay of a DMA-driven program read the live
; hardware instead of the recording and went out of step. It runs on a 48K
; because RZX recording is refused on a Next (GH #274); the zxnDMA is there on
; every machine type (zxnext.vhd:2405, :2440, :2643 — the port decode is gated
; by NR 0x82 bit 5 and NR 0x85 bit 1, never by the machine type).
;
; WHAT IT DOES, once per frame (HALT on an IM 2 interrupt):
;   1. The CPU reads the A-G keyboard half-row (IN from $FDFE).
;   2. The DMA is programmed from scratch through port $6B and reads the
;      SPACE/SYM/M/N/B half-row ($7FFE, I/O, fixed address) 32 times into
;      `buf` (memory, incrementing). So each frame's input is one CPU read
;      followed by 32 DMA reads.
;   3. Attribute row `row` is painted from `buf`: column i's PAPER is the keys
;      buf[i] shows pressed (SPACE = blue, M = green), its INK the CPU's keys
;      (A = blue, S = red, D = green). The bitmap is $0F everywhere, so each
;      cell shows both.
;   4. `row` advances by 1, or by 2 while the DMA read SPACE pressed — so what
;      the DMA read changes where every later frame draws, not just a colour.
;   5. A bright white cursor is painted on the new row.
; The cursor moves every frame whatever the input, so adjacent frames always
; differ; the painted rows are a history of the last frames' input.
;
; The DMA is reprogrammed in full each frame on purpose: no snapshot an RZX can
; embed holds the DMA's registers, so only a transfer programmed inside the
; recording can be replayed.

        org  $8000

KEYS_DMA    equ $7FFE           ; SPACE SYM M N B — the DMA's source port
KEYS_CPU    equ $FDFE           ; A S D F G       — the CPU's IN
IM2_TABLE   equ $FE00           ; 257 bytes of $FD: any vector -> $FDFD
IM2_JP      equ $FDFD           ; JP isr
STACK       equ $FD00
DMA_PORT    equ $6B             ; zxnDMA (ZXN mode)

start:
        di
        ld   sp, STACK
        xor  a
        out  ($FE), a           ; black border
        ld   hl, $4000          ; bitmap: $0F — left half PAPER, right half INK
        ld   de, $4001
        ld   bc, 6143
        ld   (hl), $0F
        ldir
        ld   hl, $5800          ; attributes: black
        ld   de, $5801
        ld   bc, 767
        ld   (hl), a
        ldir
        ld   hl, IM2_TABLE
        ld   de, IM2_TABLE + 1
        ld   bc, 256
        ld   (hl), $FD
        ldir
        ld   a, $C3
        ld   (IM2_JP), a
        ld   hl, isr
        ld   (IM2_JP + 1), hl
        ld   a, IM2_TABLE / 256
        ld   i, a
        im   2
        xor  a
        ld   (row), a

main:
        ei
        halt

        ; 1. The CPU's read.
        ld   bc, KEYS_CPU
        in   a, (c)
        cpl
        and  7
        ld   (ink), a

        ; 2. The DMA's 32 reads. Continuous mode: the CPU is stopped until the
        ;    block is done, so `buf` is complete at the next instruction.
        ld   hl, dma_prog
        ld   b, dma_prog_end - dma_prog
        ld   c, DMA_PORT
        otir

        ; 3. Paint `row` from what the DMA read.
        ld   a, (row)
        call row_addr
        ld   de, buf
        ld   b, 32
paint:
        ld   a, (de)
        cpl
        and  7
        rlca
        rlca
        rlca                    ; PAPER = DMA keys
        ld   c, a
        ld   a, (ink)           ; INK   = CPU keys
        or   c
        ld   (hl), a
        inc  hl
        inc  de
        djnz paint

        ; 4. Advance 1 row, or 2 while the DMA read SPACE pressed.
        ld   a, (buf)
        cpl
        and  1
        inc  a
        ld   c, a
        ld   a, (row)
        add  a, c
        cp   24
        jr   c, no_wrap
        sub  24
no_wrap:
        ld   (row), a

        ; 5. The cursor.
        call row_addr
        ld   b, 32
cursor:
        ld   (hl), $78          ; BRIGHT, PAPER white, INK black
        inc  hl
        djnz cursor
        jr   main

; A = row (0-23) -> HL = its first attribute byte. Clobbers DE.
row_addr:
        ld   l, a
        ld   h, 0
        add  hl, hl
        add  hl, hl
        add  hl, hl
        add  hl, hl
        add  hl, hl
        ld   de, $5800
        add  hl, de
        ret

isr:
        ei
        reti

; The whole DMA program, written with one OTIR (dma.vhd register protocol).
dma_prog:
        defb $83                ; WR6: DISABLE
        defb $7D                ; WR0: A -> B, transfer; port A + length follow
        defw KEYS_DMA           ;      port A address
        defw 32                 ;      block length
        defb $2C                ; WR1: port A is I/O, fixed address
        defb $10                ; WR2: port B is memory, incrementing
        defb $AD                ; WR4: continuous; port B address follows
        defw buf                ;      port B address
        defb $82                ; WR5: stop at end of block
        defb $CF                ; WR6: LOAD
        defb $87                ; WR6: ENABLE
dma_prog_end:

row:    defb 0
ink:    defb 0
buf:    defs 32
