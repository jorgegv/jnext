; Source of dmaloop.bin — the PGO training's DMA workload (tools/pgo-train.sh).
; Written for the GH #294 compiler evaluation (doc/analysis/CLANG-LLVM-EVALUATION.md,
; whose appendix carries the same program). 41 bytes, assembled with z88dk's
; z80asm (`z88dk-z80asm -b -o=dmaloop.bin dmaloop.asm`); the bytes are listed
; below so the .bin can be regenerated with no toolchain:
;
;   printf %b \
;       '\xF3\x31\x00\xBF\x01\x3B\x24\x3E\x07\xED\x79\x04\x3E\x03\xED\x79' \
;       '\x21\x1B\x80\x06\x0E\x0E\x6B\xED\xB3\x18\xF5\xC3\x7D\x00\x00\x00' \
;       '\x1B\x14\x10\xAD\x00\x40\x82\xCF\x87' \
;       > dmaloop.bin
;
; WHY IT IS IN THE TRAINING: it is the only memory->memory DMA the training
; runs. The games in the set use the DMA too, but only memory->port (sprite
; patterns and attributes), and rzx_dma_demo.bin covers port->memory; this one
; keeps the engine's transfer loop busy for the whole frame at 28 MHz.
;
; Back-to-back zxnDMA memory->memory block copies: 6912 bytes of ROM ($0000)
; to the screen ($4000), continuous mode, forever. Interrupts disabled.
; Run it with `jnext --machine 48k --inject dmaloop.bin` (org $8000).
        org  $8000
DMA     equ  $6B
start:
        di
        ld   sp, $BF00
        ld   bc, $243B          ; NR $07 = 3: 28 MHz, so more DMA per frame
        ld   a, $07
        out  (c), a
        inc  b
        ld   a, $03
        out  (c), a
loop:
        ld   hl, prog
        ld   b, prog_end - prog
        ld   c, DMA
        otir
        jr   loop
prog:
        defb $C3                ; WR6 reset
        defb $7D                ; WR0 A->B, A start + length follow
        defw $0000              ; port A start (ROM)
        defw $1B00              ; block length 6912 (a whole screen)
        defb $14                ; WR1 port A memory, increment
        defb $10                ; WR2 port B memory, increment
        defb $AD                ; WR4 continuous mode, B start follows
        defw $4000              ; port B start (screen)
        defb $82                ; WR5 stop at end
        defb $CF                ; WR6 LOAD
        defb $87                ; WR6 ENABLE
prog_end:
