; Source of psg_fixture.bin: the program the .psg export is checked with. Our own
; code, no third-party music. The register stream it writes was recorded with
; Fuse 1.6.0 (tools/fuse-psg-reference.sh) into psg_fixture.fuse.psg, and jnext's own
; recording of the same program must equal that file byte for byte after the lead-in
; (the audio_debug_test rows PSF-01..03 and the psg-record-func regression row).
;
; Assembled with z88dk's z80asm at org $8000. The bytes are listed after the
; assembly (see the .bin) so it can be rebuilt with no toolchain:
;   printf %b "$(sed -n 's/^;BYTES //p' psg_fixture.asm | tr -d '\n')" > psg_fixture.bin
;
; The assembled bytes (read back by the printf command above):
;BYTES \xF3\x31\xF0\xBF\x21\x00\x90\x11\x01\x90\x01\x00\x01\x36\x92\xED
;BYTES \xB0\x3E\xFB\x32\x92\x92\x3E\xED\x32\x93\x92\x3E\x4D\x32\x94\x92
;BYTES \x3E\x90\xED\x47\xED\x5E\xFB\x21\xDC\x05\x76\xAF\xDB\xFE\x2F\xE6
;BYTES \x1F\x20\x05\x2B\x7C\xB5\x20\xF2\x76\x01\x02\x03\x0B\x78\xB1\x20
;BYTES \xFB\x01\xFD\xFF\x3E\x07\xED\x79\x01\xFD\xBF\x3E\x3F\xED\x79\x01
;BYTES \x08\x00\xCD\xD9\x80\x21\x00\x00\x22\x00\xA0\x21\xE1\xAC\x22\x02
;BYTES \xA0\x2A\x02\xA0\xCB\x3C\xCB\x1D\x30\x04\x7C\xEE\xB4\x67\x22\x02
;BYTES \xA0\x7C\xE6\x3F\x67\xB5\x20\x02\x2E\x01\x3A\x01\xA0\xB7\x20\x24
;BYTES \x3A\x00\xA0\xB7\x20\x05\x21\xFF\x3F\x18\x19\xFE\x01\x20\x05\x21
;BYTES \x01\x00\x18\x10\xFE\x64\x20\x05\x21\x00\x40\x18\x07\xFE\x65\x20
;BYTES \x03\x21\x01\x80\x3A\x00\xA0\x5F\xCD\xE0\x80\x3A\x01\xA0\xB7\x20
;BYTES \x16\x3A\x00\xA0\x5F\x21\x24\x81\x7E\xB7\x28\x0B\x23\x4E\x23\x46
;BYTES \x23\xBB\x20\xF4\xCD\xD9\x80\x2A\x00\xA0\x23\x22\x00\xA0\x11\x2C
;BYTES \x01\xB7\xED\x52\x20\x8B\x76\x18\xFD\x76\x0B\x78\xB1\x20\xFA\xC9
;BYTES \x7B\x32\x04\xA0\x76\x01\x02\x03\x0B\x78\xB1\x20\xFB\x7B\x87\x87
;BYTES \x57\x87\x87\x87\x82\x83\xC6\x5A\xC6\xA5\x57\x1E\x0F\x29\x30\x1B
;BYTES \xE5\x7B\x01\xFD\xFF\xED\x79\x7B\xFE\x0D\x7A\x20\x08\x3A\x04\xA0
;BYTES \x0F\x0F\x0F\xE6\x0F\x01\xFD\xBF\xED\x79\xE1\x7A\xD6\x0B\x57\x1D
;BYTES \xF2\xFD\x80\xC9\x28\x01\x00\x50\x03\x00\x78\x04\x00\xA0\x05\x00
;BYTES \xC8\x09\x00\xFA\x4C\x04\x00
;
; Everything lives in $8000-$BFFF (bank 2: present in every 128K paging, uncontended).
;
; WHAT IT DOES
;   1. Setup: IM 2 with a $9292 handler (EI; RETI) and a 257-byte vector table of $92, so
;      whatever the bus holds during the acknowledge, the vector is $9292. No AY write.
;   2. Gate: waits for any key (the port $00FE read, bits 0-4) or 1500 frames. Fuse's
;      Record is started by hand and can be late, so nothing is written before the gate.
;   3. Sentinel: one frame writing R7 = $3F only, then 8 frames with no write.
;   4. Song: 300 frames, k = 0..299. Each writes the registers of mask(k) in DESCENDING
;      order 15..0 (the .psg file is in register order, so the reorder is part of the check):
;        mask(k)  = the low 14 bits of a 16-bit Galois LFSR (seed $ACE1, taps $B400),
;                   stepped once per song frame; 0 becomes 1;
;        k = 0    all 14 registers R0-R13
;        k = 1    R0 only
;        k = 100  R14 only        (an AY write that is an empty frame in a .psg)
;        k = 101  R15 and R0      (R15 is never written to the file)
;        value(k,r) = (k*37 + r*11 + $5A) & $FF, except R13 = (k >> 3) & $0F.
;      After song frame k = 40, 80, 120, 160, 200, 250: 1, 3, 4, 5, 9, 1100 frames with no write.
;   5. Then HALT forever.
;   Frames from the key to the last write: 1 + 8 + 300 + 1122 = 1431.
;
; Writes come about 20000 T-states after the HALT, so the frame they fall in does not depend
; on where an emulator puts its interrupt relative to its frame wrap. The program never
; selects a register of 16 or more and never sends a TurboSound command: Fuse keeps 4 bits of
; a register select where the Next drops the write, so the two would differ there.

        org  $8000

kcur    equ  $A000              ; 16-bit song frame k
lfsr    equ  $A002              ; 16-bit LFSR state
kvar    equ  $A004              ; k, low byte, for the R13 value

start:
        di
        ld   sp, $BFF0
        ld   hl, $9000          ; table: $9000-$9100 = $92
        ld   de, $9001
        ld   bc, $0100
        ld   (hl), $92
        ldir
        ld   a, $FB             ; $9292: EI
        ld   ($9292), a
        ld   a, $ED             ;        RETI
        ld   ($9293), a
        ld   a, $4D
        ld   ($9294), a
        ld   a, $90
        ld   i, a
        im   2
        ei

        ld   hl, 1500           ; the gate: any key, or 1500 frames
gate:   halt
        xor  a
        in   a, ($FE)
        cpl
        and  $1F
        jr   nz, go
        dec  hl
        ld   a, h
        or   l
        jr   nz, gate

go:     halt                    ; sentinel: R7 = $3F only
        ld   bc, 770
sdly:   dec  bc
        ld   a, b
        or   c
        jr   nz, sdly
        ld   bc, $FFFD
        ld   a, 7
        out  (c), a
        ld   bc, $BFFD
        ld   a, $3F
        out  (c), a
        ld   bc, 8
        call wait_frames

        ld   hl, 0
        ld   (kcur), hl
        ld   hl, $ACE1
        ld   (lfsr), hl

song:   ld   hl, (lfsr)         ; one Galois step
        srl  h
        rr   l
        jr   nc, nofb
        ld   a, h
        xor  $B4
        ld   h, a
nofb:   ld   (lfsr), hl
        ld   a, h
        and  $3F
        ld   h, a
        or   l
        jr   nz, havemask
        ld   l, 1
havemask:
        ld   a, (kcur + 1)      ; the overrides apply to k < 256 only
        or   a
        jr   nz, noov
        ld   a, (kcur)
        or   a
        jr   nz, o1
        ld   hl, $3FFF          ; k = 0: R0-R13
        jr   noov
o1:     cp   1
        jr   nz, o100
        ld   hl, $0001          ; k = 1: R0
        jr   noov
o100:   cp   100
        jr   nz, o101
        ld   hl, $4000          ; k = 100: R14
        jr   noov
o101:   cp   101
        jr   nz, noov
        ld   hl, $8001          ; k = 101: R15, R0
noov:   ld   a, (kcur)
        ld   e, a
        call emit_frame

        ld   a, (kcur + 1)      ; the gaps
        or   a
        jr   nz, nogap
        ld   a, (kcur)
        ld   e, a
        ld   hl, gaptab
gl:     ld   a, (hl)
        or   a
        jr   z, nogap
        inc  hl
        ld   c, (hl)
        inc  hl
        ld   b, (hl)
        inc  hl
        cp   e
        jr   nz, gl
        call wait_frames
nogap:  ld   hl, (kcur)
        inc  hl
        ld   (kcur), hl
        ld   de, 300
        or   a
        sbc  hl, de
        jr   nz, song

forever: halt
        jr   forever

; BC = number of frames (>= 1)
wait_frames:
wf:     halt
        dec  bc
        ld   a, b
        or   c
        jr   nz, wf
        ret

; HL = register mask (bit r = register r), E = k (low byte). One frame: wait for the
; interrupt, wait about 20000 T-states, then write the registers 15..0.
emit_frame:
        ld   a, e
        ld   (kvar), a
        halt
        ld   bc, 770
edly:   dec  bc
        ld   a, b
        or   c
        jr   nz, edly
        ld   a, e               ; D = value of R15 = k*37 + $5A + 15*11
        add  a, a
        add  a, a
        ld   d, a               ; 4k
        add  a, a
        add  a, a
        add  a, a               ; 32k
        add  a, d               ; 36k
        add  a, e               ; 37k
        add  a, $5A
        add  a, 165
        ld   d, a
        ld   e, 15              ; E = r
rloop:  add  hl, hl             ; bit 15 -> carry: register r
        jr   nc, skip
        push hl
        ld   a, e
        ld   bc, $FFFD
        out  (c), a
        ld   a, e
        cp   13
        ld   a, d
        jr   nz, wv
        ld   a, (kvar)
        rrca
        rrca
        rrca
        and  $0F
wv:     ld   bc, $BFFD
        out  (c), a
        pop  hl
skip:   ld   a, d
        sub  11
        ld   d, a
        dec  e
        jp   p, rloop
        ret

gaptab: defb 40
        defw 1
        defb 80
        defw 3
        defb 120
        defw 4
        defb 160
        defw 5
        defb 200
        defw 9
        defb 250
        defw 1100
        defb 0
