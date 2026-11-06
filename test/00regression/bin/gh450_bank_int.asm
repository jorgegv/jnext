; Source of gh450_bank_int.bin - the fixture input-int-commit-func injects
; (GH #450). Assembled with z88dk's z80asm:
;
;   z80asm -b -o gh450_bank_int.bin gh450_bank_int.asm
;
; Own code, no third-party bytes. It reproduces the MECHANISM that crashed a
; shipped 128K game when a key was pressed: an IM 2 vector table and ISR in
; bank 0 at $C000-$FFFF, a second bank (6) holding a different word where the
; vector is read, and main code that pages bank 6 in WITH INTERRUPTS ENABLED
; for ~420 T as soon as it sees a key.
;
;   * vector word for I=$C0 is at $C0FF/$C100: $8181 in bank 0 (JP isr), the
;     address of `fail` in bank 6 (border red, spin);
;   * the main loop polls SPACE (port $7FFE bit 0), then pages bank 6, spends
;     ~420 T, pages bank 0 back, sets the border GREEN and spins.
;
; If the key is shown to the guest only BEFORE the frame interrupt (the old
; behaviour, 292 T ahead of it on 128K), the paged window straddles the
; interrupt, the vector is read from bank 6 and the border goes RED. Shown at
; the interrupt (FUSE spectrum.c:91-103: input after z80_interrupt()), the
; window ends long before the next one and the border is GREEN.

        org     $8000
start:  di
        ld      sp,$8F00
        ld      bc,$7FFD
        ld      a,$10
        out     (c),a                   ; bank 0 at $C000
        ld      hl,$C000
        ld      de,$C001
        ld      bc,$0100
        ld      (hl),$81
        ldir                            ; $C000-$C100 = $81 (257 bytes)
        ld      hl,$8181
        ld      (hl),$C3                ; JP isr
        inc     hl
        ld      (hl),isr % 256
        inc     hl
        ld      (hl),isr / 256
        ld      bc,$7FFD
        ld      a,$16
        out     (c),a                   ; bank 6 at $C000
        ld      a,fail % 256
        ld      ($C0FF),a
        ld      a,fail / 256
        ld      ($C100),a               ; vector word = fail in bank 6
        ld      bc,$7FFD
        ld      a,$10
        out     (c),a                   ; bank 0 back
        ld      a,$C0
        ld      i,a
        im      2
        ld      bc,$7FFE
        ei
poll:   in      a,(c)
        bit     0,a
        jr      nz,poll
        ld      bc,$7FFD
        ld      a,$16
        out     (c),a                   ; bank 6 in, interrupts enabled
        ld      b,$20
spend:  djnz    spend                   ; ~420 T
        ld      bc,$7FFD
        ld      a,$10
        out     (c),a                   ; bank 0 back
        ld      a,4
        out     ($FE),a                 ; border GREEN
hang:   jr      hang

isr:    ei
        ret

fail:   ld      a,2
        out     ($FE),a                 ; border RED
spin:   jr      spin
