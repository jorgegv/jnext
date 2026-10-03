; dma_all — a 48K raw binary that drives every feature of jnext's zxnDMA
; (src/peripheral/dma.cpp, device/dma.vhd) and checks every result.
;
; Build: `make` in this directory (z88dk's z80asm); `make install` copies the
; binary to test/00regression/bin/, where the PGO training runs it:
;
;   jnext --machine 48k --inject dma_all.bin
;
; WHY IT EXISTS: the PGO training (tools/pgo-train.sh) needs ONE program that
; takes the DMA engine through all its paths, so that the profile is not
; trained on one transfer shape only. It also proves its own results: a
; training run of a program that silently stopped working would train the
; wrong code, so each pass checks every transfer and the snapshot proof only
; finds the signature (built at run time, see `sig_xor`) after a fully
; checked pass.
;
; Each PASS, at 28 MHz, interrupts off:
;   T1 port $6B (ZXN), mem -> mem, A inc -> B inc, continuous, 2048 bytes,
;      with WR1/WR2 timing bytes. Checks the copy, the status (0x1A: end of
;      block, then IDLE clears "at least one", dma.vhd:471, :265), and the
;      counter and both addresses through a read mask (0x7E).
;   T2 port $0B (Z80-DMA), B -> A, both sides decrementing, length 255: a
;      Z80-mode block moves length + 1 = 256 bytes (counter loads -1,
;      dma.vhd:667, :426). Checks the copy, a sentinel either side, the
;      status and the counter / addresses read through port $0B.
;   T3 fill: port A memory FIXED -> B inc, 512 bytes; then CONTINUE (R6
;      $D3: counter reset, addresses kept, dma.vhd:670-676) with a new fill
;      value fills the next 512. Checks both halves, status, counter, B addr.
;   T4 port -> memory: port A I/O fixed = $253B with NextREG $7F selected,
;      256 reads into memory. Checks every byte and both addresses.
;   T5 memory -> port: port B I/O fixed = $253B (NextREG $7F), 256 writes.
;      Checks NR $7F holds the last byte, and both addresses.
;   T6 burst mode with the port-B prescaler and auto-restart to the DAC port
;      $DF (sampled audio, as nexlib's pcm player does), after R6 resets of
;      both port timings and with the interrupt commands and force ready
;      (no-ops in dma.vhd). While it loops: status 0x1B (end of block stays
;      set across restarts, :471-488; never IDLE), and port A's address moves.
;      Then DISABLE: 0x1A; reinit status ($8B): 0x3A; RESET ($C3): 0x3A.
;   T7 byte mode (R4 mode 00): dma.vhd's block-length test is not gated by
;      the mode (:426), so the whole 128-byte block moves. Checks the copy and
;      a status + port-B-high read mask ($41), including its wrap.
; After a pass with every check matched, the signature is written to SIG and
; PASS is incremented; the next pass uses new data. Any failed check stores
; the failing step in FAILID, wipes SIG and stops.
;
; DMA interrupts are not covered: dma.vhd does not implement them (R3 int_en
; and the R4 interrupt bytes are commented out; R6 $AF/$AB/$A3/$B7 do nothing).

        org  $8000

SRC     equ $A000               ; 2048 bytes of pattern
DST     equ $A800               ; T1, 2048
DSTB    equ $B001               ; T2, 256 (sentinels at $B000 and $B101)
DST3    equ $B200               ; T3, 1024
DST4    equ $B600               ; T4, 256
DST7    equ $B700               ; T7, 128
FILL    equ $B7F0               ; T3 fill byte
PASS    equ $9F00
STEP    equ $9F01
FAILID  equ $9F02
SIG     equ $9F10               ; 12 bytes, written only after a checked pass
DMA     equ $6B
ZDMA    equ $0B
NR_SEL  equ $243B
NR_DAT  equ $253B

start:
        di
        ld   sp, $FF00
        ld   a, 7               ; NR $07 = 3: 28 MHz
        ld   e, 3
        call nr_write
        ld   a, 8               ; NR $08 bit 3: enable the DACs (bit 7 kept 0:
        call nr_read            ; writing 1 there would unlock port $7FFD)
        or   $08
        and  $7F
        ld   e, a
        ld   a, 8
        call nr_write
        xor  a
        ld   (PASS), a
        ld   (FAILID), a
        call sig_clear

pass_loop:
        ; ---- the pattern: SRC[i] = PASS + 7*i ----
        ld   hl, SRC
        ld   bc, 2048
        ld   a, (PASS)
        ld   d, a
pat:    ld   (hl), d
        ld   a, d
        add  a, 7
        ld   d, a
        inc  hl
        dec  bc
        ld   a, b
        or   c
        jr   nz, pat

        ; ---- T1: ZXN, mem -> mem, inc/inc, continuous, 2048 ----
        ld   hl, DST
        ld   bc, 2048
        ld   e, 0
        call memset
        ld   hl, t1
        ld   b, t1_end - t1
        call dma_prog
        ld   a, 1
        ld   (STEP), a
        ld   hl, SRC
        ld   de, DST
        ld   bc, 2048
        call memcmp
        jp   nz, fail
        ld   a, 2
        ld   (STEP), a
        ld   c, DMA
        call read_status
        cp   $1A
        jp   nz, fail
        ld   a, 3
        ld   (STEP), a
        ld   c, DMA
        ld   e, $7E             ; counter lo/hi, A lo/hi, B lo/hi
        call set_mask
        ld   hl, t1_expect
        ld   b, 6
        call expect_reads
        jp   nz, fail

        ; ---- T2: Z80-DMA port, B -> A, dec/dec, length 255 (moves 256) ----
        ld   hl, DSTB
        ld   bc, 256
        ld   e, 0
        call memset
        ld   a, $5A
        ld   (DSTB - 1), a
        ld   (DSTB + 256), a
        ld   hl, t2
        ld   b, t2_end - t2
        ld   c, ZDMA
        otir
        ld   a, 4
        ld   (STEP), a
        ld   hl, SRC
        ld   de, DSTB
        ld   bc, 256
        call memcmp
        jp   nz, fail
        ld   a, 5
        ld   (STEP), a
        ld   a, (DSTB - 1)
        cp   $5A
        jp   nz, fail
        ld   a, (DSTB + 256)
        cp   $5A
        jp   nz, fail
        ld   a, 6
        ld   (STEP), a
        ld   c, ZDMA
        call read_status
        cp   $1A
        jp   nz, fail
        ld   a, 7
        ld   (STEP), a
        ld   c, ZDMA
        ld   e, $7E
        call set_mask
        ld   hl, t2_expect
        ld   b, 6
        call expect_reads_c
        jp   nz, fail

        ; ---- T3: fill (port A memory fixed), then CONTINUE ----
        ld   a, (PASS)
        xor  $A5
        ld   (FILL), a
        xor  $55                ; a value that is neither V nor V+1
        ld   e, a
        ld   hl, DST3
        ld   bc, 1024
        call memset
        ld   hl, t3
        ld   b, t3_end - t3
        call dma_prog
        ld   a, 8
        ld   (STEP), a
        ld   a, (FILL)
        ld   e, a
        ld   hl, DST3
        ld   bc, 512
        call memchk
        jp   nz, fail
        ld   a, 9
        ld   (STEP), a
        ld   c, DMA
        call read_status
        cp   $1A
        jp   nz, fail
        ld   a, (FILL)          ; second half: V + 1, through CONTINUE
        inc  a
        ld   (FILL), a
        ld   a, $D3             ; CONTINUE
        out  (DMA), a
        ld   a, $87             ; ENABLE
        out  (DMA), a
        ld   a, 10
        ld   (STEP), a
        ld   a, (FILL)
        ld   e, a
        ld   hl, DST3 + 512
        ld   bc, 512
        call memchk
        jp   nz, fail
        ld   a, 11
        ld   (STEP), a
        ld   c, DMA
        call read_status
        cp   $1A
        jp   nz, fail
        ld   a, 12
        ld   (STEP), a
        ld   c, DMA
        ld   e, $66             ; counter lo/hi, B lo/hi
        call set_mask
        ld   hl, t3_expect
        ld   b, 4
        call expect_reads
        jp   nz, fail

        ; ---- T4: port -> memory (NextREG $7F through $253B) ----
        ld   a, (PASS)
        xor  $3C
        ld   e, a
        ld   a, $7F
        call nr_write           ; leaves $7F selected on $243B
        ld   a, e
        xor  $55
        ld   e, a
        ld   hl, DST4
        ld   bc, 256
        call memset
        ld   hl, t4
        ld   b, t4_end - t4
        call dma_prog
        ld   a, 13
        ld   (STEP), a
        ld   a, (PASS)
        xor  $3C
        ld   e, a
        ld   hl, DST4
        ld   bc, 256
        call memchk
        jp   nz, fail
        ld   a, 14
        ld   (STEP), a
        ld   c, DMA
        ld   e, $78             ; A lo/hi, B lo/hi
        call set_mask
        ld   hl, t4_expect
        ld   b, 4
        call expect_reads
        jp   nz, fail

        ; ---- T5: memory -> port (NextREG $7F through $253B) ----
        ld   bc, NR_SEL
        ld   a, $7F
        out  (c), a
        ld   hl, t5
        ld   b, t5_end - t5
        call dma_prog
        ld   a, 15
        ld   (STEP), a
        ld   a, $7F
        call nr_read
        ld   hl, SRC + 255
        cp   (hl)
        jp   nz, fail
        ld   a, 16
        ld   (STEP), a
        ld   c, DMA
        ld   e, $78
        call set_mask
        ld   hl, t5_expect
        ld   b, 4
        call expect_reads
        jp   nz, fail

        ; ---- T6: burst + prescaler + auto-restart -> DAC $DF ----
        ld   hl, t6
        ld   b, t6_end - t6
        call dma_prog
        ld   bc, 20000          ; ~0.5 M T-states: many 64-byte passes
        call delay
        ld   a, 17
        ld   (STEP), a
        ld   c, DMA
        call read_status
        cp   $1B                ; end of block set, at least one, never IDLE
        jp   nz, fail
        ld   a, 18
        ld   (STEP), a
        ld   c, DMA
        ld   e, $08             ; port A address, low byte only
        call set_mask
        in   a, (c)
        ld   d, a
        ld   bc, 500
        call delay
        ld   c, DMA
        in   a, (c)
        cp   d                  ; the transfer is still running
        jp   z, fail
        ld   a, $83             ; DISABLE
        out  (DMA), a
        ld   a, 19
        ld   (STEP), a
        ld   c, DMA
        call read_status
        cp   $1A                ; IDLE cleared "at least one"
        jp   nz, fail
        ld   a, $8B             ; reinitialise status
        out  (DMA), a
        ld   a, 20
        ld   (STEP), a
        ld   c, DMA
        call read_status
        cp   $3A
        jp   nz, fail
        ld   a, $C3             ; RESET
        out  (DMA), a
        ld   a, 21
        ld   (STEP), a
        ld   c, DMA
        call read_status
        cp   $3A
        jp   nz, fail

        ; ---- T7: byte mode (R4 mode 00), 128 bytes ----
        ld   hl, DST7
        ld   bc, 128
        ld   e, 0
        call memset
        ld   hl, t7
        ld   b, t7_end - t7
        call dma_prog
        ld   a, 22
        ld   (STEP), a
        ld   hl, SRC + 512
        ld   de, DST7
        ld   bc, 128
        call memcmp
        jp   nz, fail
        ld   a, 23
        ld   (STEP), a
        ld   c, DMA
        ld   e, $41             ; status + port B high: status, B hi, status
        call set_mask
        ld   hl, t7_expect
        ld   b, 3
        call expect_reads
        jp   nz, fail

        ; ---- every check matched: the signature, then the next pass ----
        ld   hl, sig_xor
        ld   de, SIG
        ld   b, sig_xor_end - sig_xor
sig_w:  ld   a, (hl)
        xor  $5A
        ld   (de), a
        inc  hl
        inc  de
        djnz sig_w
        ld   a, (PASS)
        inc  a
        ld   (PASS), a
        jp   pass_loop

fail:
        ld   a, (STEP)
        ld   (FAILID), a
        call sig_clear
fail_stop:
        jr   fail_stop

; ---- helpers -------------------------------------------------------------

; HL = program, B = length: OTIR to port $6B.
dma_prog:
        ld   c, DMA
        otir
        ret

; C = DMA port: status byte (R6 $BF, then a read). A = status.
read_status:
        ld   a, $BF
        out  (c), a
        in   a, (c)
        ret

; C = DMA port, E = read mask: R6 $BB + mask, then $A7 (initialise reads).
set_mask:
        ld   a, $BB
        out  (c), a
        out  (c), e
        ld   a, $A7
        out  (c), a
        ret

; B reads of port $6B against the bytes at HL. NZ on the first mismatch.
expect_reads:
        ld   c, DMA
; ... of port C.
expect_reads_c:
er:     in   a, (c)
        cp   (hl)
        ret  nz
        inc  hl
        djnz er
        ret                     ; Z (the last cp matched)

; A = NextREG number, E = value.
nr_write:
        ld   bc, NR_SEL
        out  (c), a
        inc  b
        out  (c), e
        ret

; A = NextREG number -> A = value.
nr_read:
        ld   bc, NR_SEL
        out  (c), a
        inc  b
        in   a, (c)
        ret

; HL = address, BC = length, E = value.
memset:
        ld   (hl), e
        inc  hl
        dec  bc
        ld   a, b
        or   c
        jr   nz, memset
        ret

; HL vs DE, BC bytes. Z if equal.
memcmp:
        ld   a, (de)
        cp   (hl)
        ret  nz
        inc  hl
        inc  de
        dec  bc
        ld   a, b
        or   c
        jr   nz, memcmp
        ret

; HL, BC bytes, all equal to E? Z if so.
memchk:
        ld   a, (hl)
        cp   e
        ret  nz
        inc  hl
        dec  bc
        ld   a, b
        or   c
        jr   nz, memchk
        ret

; BC iterations of a fixed loop.
delay:
        dec  bc
        ld   a, b
        or   c
        jr   nz, delay
        ret

sig_clear:
        ld   hl, SIG
        ld   b, 12
sc:     ld   (hl), 0
        inc  hl
        djnz sc
        ret

; ---- DMA programs (dma.vhd register protocol) -----------------------------

t1:     defb $C3                ; R6 RESET
        defb $7D                ; R0 A->B; A address + length follow
        defw SRC, 2048
        defb $54, $02           ; R1 A memory inc; timing byte: "10" (2 cycles)
        defb $50, $01           ; R2 B memory inc; timing byte: "01" (3 cycles)
        defb $AD                ; R4 continuous; B address follows
        defw DST
        defb $82                ; R5 no restart
        defb $CF, $87           ; LOAD, ENABLE
t1_end:
t1_expect:
        defb $00, $08           ; counter 2048
        defb $00, $A8           ; port A = SRC + 2048
        defb $00, $B0           ; port B = DST + 2048

t2:     defb $C3
        defb $79                ; R0 B->A; A address (the destination) + length
        defw DSTB + 255, 255
        defb $04                ; R1 A memory dec
        defb $00                ; R2 B memory dec
        defb $AD                ; R4 continuous; B address (the source)
        defw SRC + 255
        defb $82, $CF, $87
t2_end:
t2_expect:
        defb $FF, $00           ; counter: -1 + 256 = 255
        defb $00, $B0           ; port A (dest) = DSTB - 1
        defb $FF, $9F           ; port B (source) = SRC - 1

t3:     defb $C3
        defb $7D
        defw FILL, 512
        defb $24                ; R1 A memory FIXED
        defb $10                ; R2 B memory inc
        defb $AD
        defw DST3
        defb $82, $CF, $87
t3_end:
t3_expect:
        defb $00, $02           ; counter 512 (the CONTINUEd block)
        defb $00, $B6           ; port B = DST3 + 1024

t4:     defb $C3
        defb $7D
        defw NR_DAT, 256
        defb $2C                ; R1 A I/O fixed
        defb $10                ; R2 B memory inc
        defb $AD
        defw DST4
        defb $82, $CF, $87
t4_end:
t4_expect:
        defb $3B, $25           ; port A = $253B (fixed)
        defb $00, $B7           ; port B = DST4 + 256

t5:     defb $C3
        defb $7D
        defw SRC, 256
        defb $14                ; R1 A memory inc
        defb $28                ; R2 B I/O fixed
        defb $AD
        defw NR_DAT
        defb $82, $CF, $87
t5_end:
t5_expect:
        defb $00, $A1           ; port A = SRC + 256
        defb $3B, $25           ; port B = $253B (fixed)

t6:     defb $C3, $C7, $CB      ; RESET, reset port A / port B timing
        defb $7D
        defw SRC, 64
        defb $54, $01           ; R1 A memory inc; timing "01"
        defb $68, $21, $10      ; R2 B I/O fixed; timing "01" + prescaler 16
        defb $CD                ; R4 BURST; B address follows
        defw $00DF              ; the SpecDrum DAC (channels A and D)
        defb $A2                ; R5 auto-restart
        defb $AF, $AB, $A3, $B7 ; interrupt commands: no-ops in dma.vhd
        defb $CF, $B3, $87      ; LOAD, force ready, ENABLE
t6_end:

t7:     defb $C3
        defb $7D
        defw SRC + 512, 128
        defb $14, $10
        defb $8D                ; R4 BYTE mode; B address follows
        defw DST7
        defb $82, $CF, $87
t7_end:
t7_expect:
        defb $1A                ; status
        defb $B7                ; port B high = (DST7 + 128) >> 8
        defb $1A                ; wrapped back to status

; "DMA-ALL-OK!!" XOR $5A: the plain text never appears in the binary.
sig_xor:
        defb 'D' ^ $5A, 'M' ^ $5A, 'A' ^ $5A, '-' ^ $5A
        defb 'A' ^ $5A, 'L' ^ $5A, 'L' ^ $5A, '-' ^ $5A
        defb 'O' ^ $5A, 'K' ^ $5A, '!' ^ $5A, '!' ^ $5A
sig_xor_end:
