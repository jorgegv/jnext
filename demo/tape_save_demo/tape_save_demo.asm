; tape_save_demo — a 48K program that saves to tape four ways (GH #89).
;
; Build: `make` in this directory (z88dk's z80asm); `make install` copies the
; binary to test/00regression/bin/, where tape-save-mic-func injects it:
;
;   jnext --machine 48k --inject tape_save_demo.bin --tape-save out.tzx
;
; WHAT IT SAVES, in this order, with a one-second pause after each block:
;   1. A BASIC program "verify" (autostart 10), through the ROM's SA-BYTES
;      (CALL $04C2) — the --tape-save trap takes it.
;        10 CLEAR 39999: LOAD ""CODE : RANDOMIZE USR 40000
;   2. CODE "verifier" (the routine at `verifier`, loaded at 40000), through
;      the ROM's SA-BYTES.
;   3. CODE "stdmic" (256 bytes, i XOR $5A), through a RAM copy of SA-BYTES:
;      the ROM's own pulses, which the trap does not see, so they reach the
;      tape only through the MIC output.
;   4. CODE "turbo" (256 bytes, i*7+3), through a RAM copy of SA-BYTES with
;      its timing constants changed: pilot ~1895, sync ~550/644, bits
;      ~700/1594 T-states. Not the ROM's timings, but inside what the ROM's
;      LD-BYTES accepts, so the ROM loads it back.
;   5. 24 MIC pulses of unrelated lengths, no pilot: data no loader reads.
; Then it writes "DONE" + newline to the magic port $BEEF and halts.
;
; Loading the tape back (LOAD "" on a 48K) runs "verify", which loads
; "verifier", which loads blocks 3 and 4 through the ROM's LD-BYTES ($0556)
; and checks every byte. It writes "OK" + newline to port $BEEF and $4F ('O')
; to address 23296 on success, "F<n>" and 'F' on failure (n = the stage).

        org  $8000

SA_BYTES    equ $04C2
SA_END      equ $053F           ; first byte after SA-BYTES (SA/LD-RET)
SA_LEN      equ SA_END - SA_BYTES
SAVR_STD    equ $A000           ; RAM copy of SA-BYTES, ROM timings
SAVR_TURBO  equ $A100           ; RAM copy, turbo timings
PAY_STD     equ $C000
PAY_TURBO   equ $C100
MAGIC       equ $BEEF

start:
        di
        ld   sp, $7F00
        ld   iy, $5C3A          ; the ROM's IM 1 handler addresses ERR-NR via IY
        xor  a
        out  ($FE), a           ; border black, MIC low

        ; payloads
        ld   hl, PAY_STD
        ld   b, 0
pstd:   ld   a, l
        xor  $5A
        ld   (hl), a
        inc  l
        djnz pstd
        ld   hl, PAY_TURBO
        ld   c, 3               ; i*7+3
ptur:   ld   (hl), c
        ld   a, c
        add  a, 7
        ld   c, a
        inc  l
        jr   nz, ptur

        ; RAM copies of SA-BYTES
        ld   de, SAVR_STD
        call copy_sa
        ld   de, SAVR_TURBO
        call copy_sa
        ; turbo constants (offsets from SA-BYTES; see the ROM listing)
        ld   ix, SAVR_TURBO
        ld   (ix + $04DF - SA_BYTES), $8F   ; pilot   ld b,$a4 -> $8f
        ld   (ix + $04E9 - SA_BYTES), $26   ; sync 1  ld b,$2f -> $26
        ld   (ix + $04F1 - SA_BYTES), $30   ; sync 2  ld b,$37 -> $30
        ld   (ix + $0519 - SA_BYTES), $45   ; bit 1   ld b,$42 -> $45
        ld   (ix + $051F - SA_BYTES), $32   ; bit     ld b,$3e -> $32
        ld   (ix + $052E - SA_BYTES), $25   ; byte    ld b,$31 -> $25
        ld   (ix + $04F8 - SA_BYTES), $2F   ; first bit  ld bc,$3b0e -> $2f0e
        ld   (ix + $053B - SA_BYTES), $2F   ; last edge  ld b,$3b -> $2f

        ei
        ; 1. BASIC "verify" through the ROM
        ld   hl, hdr_basic
        ld   bc, basic
        ld   de, basic_len
        push hl
        ld   hl, SA_BYTES
        ld   (saver), hl
        pop  hl
        call save_pair
        ; 2. CODE "verifier" through the ROM
        ld   hl, hdr_verifier
        ld   bc, verifier
        ld   de, verifier_len
        push hl
        ld   hl, SA_BYTES
        ld   (saver), hl
        pop  hl
        call save_pair
        ; 3. CODE "stdmic" through the RAM copy, ROM timings
        ld   hl, hdr_std
        ld   bc, PAY_STD
        ld   de, 256
        push hl
        ld   hl, SAVR_STD
        ld   (saver), hl
        pop  hl
        call save_pair
        ; 4. CODE "turbo" through the RAM copy, turbo timings
        ld   hl, hdr_turbo
        ld   bc, PAY_TURBO
        ld   de, 256
        push hl
        ld   hl, SAVR_TURBO
        ld   (saver), hl
        pop  hl
        call save_pair

        ; 5. 24 pulses of unrelated lengths
        di
        ld   hl, pulse_table
        ld   c, 24
        ld   a, $08             ; MIC high
pulses: out  ($FE), a
        ld   b, (hl)
pdel:   djnz pdel
        xor  $08
        inc  hl
        dec  c
        jr   nz, pulses
        xor  a
        out  ($FE), a           ; MIC low (the 25th edge)
        ei
        call delay_1s

        ld   hl, msg_done
        call magic_str
fin:    halt
        jr   fin

; copy_sa: copy SA-BYTES to DE and point its five absolute JPs into the copy.
; (No IY here: the ROM's IM 1 handler needs IY = $5C3A.)
copy_sa:
        push de
        ld   hl, SA_BYTES
        ld   bc, SA_LEN
        ldir
        pop  ix                 ; IX = copy base
        ld   hl, jp_offsets
        ld   b, 5
reloc:  push bc
        push hl
        ld   c, (hl)
        ld   b, 0
        push ix
        pop  hl
        add  hl, bc             ; HL -> operand in the copy
        ld   e, (hl)
        inc  hl
        ld   d, (hl)            ; DE = ROM target, HL -> its high byte
        push hl
        ex   de, hl
        ld   de, -SA_BYTES
        add  hl, de             ; offset into the routine
        push ix
        pop  de
        add  hl, de             ; target in the copy
        ex   de, hl
        pop  hl
        ld   (hl), d
        dec  hl
        ld   (hl), e
        pop  hl
        inc  hl
        pop  bc
        djnz reloc
        ret

jp_offsets:                     ; operand of each JP, from SA-BYTES
        defb $04E6 - SA_BYTES   ; jp p,$04d8
        defb $04FC - SA_BYTES   ; jp $0507
        defb $050C - SA_BYTES   ; jp $0525
        defb $0528 - SA_BYTES   ; jp nz,$0514
        defb $0538 - SA_BYTES   ; jp nz,$04fe

; save_pair: HL = 17-byte header, BC = data, DE = data length, (saver) =
; the SA-BYTES to call. Saves the header block and the data block, each
; followed by 1 s.
save_pair:
        push de
        push bc
        push hl
        pop  ix                 ; IX = header
        ld   de, 17
        xor  a                  ; flag $00
        call call_saver
        call delay_1s
        pop  ix                 ; IX = data
        pop  de
        ld   a, $FF
        call call_saver
        jp   delay_1s

call_saver:
        ld   hl, (saver)
        jp   (hl)

saver:  defw 0

delay_1s:
        ei
        ld   b, 50
d1s:    halt
        djnz d1s
        ret

; magic_str: write the zero-terminated string at HL to port MAGIC.
magic_str:
        ld   bc, MAGIC
ms1:    ld   a, (hl)
        or   a
        ret  z
        out  (c), a
        inc  hl
        jr   ms1

msg_done:
        defm "DONE"
        defb 10, 0

pulse_table:                    ; DJNZ counts: each pulse is about 13*n + 50 T-states
        defb 20, 90, 35, 150, 60, 200, 25, 110, 75, 180, 40, 130
        defb 95, 15, 170, 55, 120, 30, 210, 65, 140, 45, 100, 85

; ── headers ────────────────────────────────────────────────────────────
hdr_basic:
        defb 0                  ; program
        defm "verify    "
        defw basic_len
        defw 10                 ; autostart line
        defw basic_len          ; variables offset (none)
hdr_verifier:
        defb 3                  ; bytes
        defm "verifier  "
        defw verifier_len
        defw 40000
        defw 32768
hdr_std:
        defb 3
        defm "stdmic    "
        defw 256
        defw PAY_STD
        defw 32768
hdr_turbo:
        defb 3
        defm "turbo     "
        defw 256
        defw PAY_TURBO
        defw 32768

; 10 CLEAR 39999: LOAD ""CODE : RANDOMIZE USR 40000
basic:
        defb 0, 10              ; line 10
        defw basic_line_end - basic_line
basic_line:
        defb $FD                ; CLEAR
        defm "39999"
        defb $0E, 0, 0
        defw 39999
        defb 0
        defb ':', $EF, '"', '"', $AF   ; : LOAD ""CODE
        defb ':', $F9, $C0      ; : RANDOMIZE USR
        defm "40000"
        defb $0E, 0, 0
        defw 40000
        defb 0
        defb $0D
basic_line_end:
basic_len   equ basic_line_end - basic

; ── verifier: position independent (JR only), run at 40000 ─────────────
HDRBUF      equ $D000
verifier:
        ld   ix, HDRBUF         ; 3. "stdmic" header + data
        ld   de, 17
        xor  a
        scf
        call $0556
        ld   e, '1'
        jr   nc, v_fail
        ld   ix, PAY_STD
        ld   de, 256
        ld   a, $FF
        scf
        call $0556
        ld   e, '2'
        jr   nc, v_fail
        ld   ix, HDRBUF         ; 4. "turbo" header + data
        ld   de, 17
        xor  a
        scf
        call $0556
        ld   e, '3'
        jr   nc, v_fail
        ld   ix, PAY_TURBO
        ld   de, 256
        ld   a, $FF
        scf
        call $0556
        ld   e, '4'
        jr   nc, v_fail
        ; every byte
        ld   hl, PAY_STD
        ld   b, 0
v_std:  ld   a, l
        xor  $5A
        cp   (hl)
        ld   e, '5'
        jr   nz, v_fail
        inc  l
        djnz v_std
        ld   hl, PAY_TURBO
        ld   c, 3
v_tur:  ld   a, c
        cp   (hl)
        ld   e, '6'
        jr   nz, v_fail
        add  a, 7
        ld   c, a
        inc  l
        jr   nz, v_tur
        ld   a, 'O'
        ld   (23296), a
        ld   bc, MAGIC
        out  (c), a
        ld   a, 'K'
        out  (c), a
        ld   a, 10
        out  (c), a
        ld   a, 4
        out  ($FE), a           ; green border
        ret
v_fail: ld   a, 'F'
        ld   (23296), a
        ld   bc, MAGIC
        out  (c), a
        out  (c), e
        ld   a, 10
        out  (c), a
        ld   a, 2
        out  ($FE), a           ; red border
        ret
verifier_end:
verifier_len equ verifier_end - verifier
