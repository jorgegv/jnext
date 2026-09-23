; Source of ay_envelope_sweep.bin — the fixture audio-envelope-func injects
; (GH #201). 80 bytes, assembled with z88dk's z80asm; the bytes are listed
; below so the .bin can be regenerated with no toolchain:
;
;   printf %b \
;       '\xF3\x11\x3F\x07\xCD\x45\x80\x11\x10\x08\xCD\x45\x80\x11\x00\x09' \
;       '\xCD\x45\x80\x11\x00\x0A\xCD\x45\x80\x11\x40\x0B\xCD\x45\x80\x11' \
;       '\x00\x0C\xCD\x45\x80\x0E\x00\x16\x0D\x59\xC5\xCD\x45\x80\xC1\x21' \
;       '\x00\x80\x2B\x7C\xB5\x20\xFB\x0C\x79\xFE\x10\x20\xEA\x11\x00\x08' \
;       '\xCD\x45\x80\x18\xFE\x01\xFD\xFF\xED\x51\x01\xFD\xBF\xED\x59\xC9' \
;       > ay_envelope_sweep.bin
;
; WHY IT EXISTS: jnext had no audio-CONTENT regression row at all —
; audio-gain-func and audio-underrun-func test gain and pacing, so an AY
; envelope bug that made 8 of the 16 shapes wrong passed 140/140 and shipped
; through every release. This fixture is the workload that makes the content
; observable: it walks ALL SIXTEEN envelope shapes on channel A with tone and
; noise disabled, so the DAC output IS the envelope generator and nothing else.
;
; VHDL basis (ym2149.vhd):
;   :469  chan_mixed(0) <= (reg(7)(0) or tone) and (reg(7)(3) or noise)
;         — R7 = 0x3F sets every tone and noise disable bit, so the channel is
;           mixed ON every clock and the output follows the volume source alone
;   :490-491  R8 bit 4 routes env_vol into the channel instead of a fixed level
;   :209-211  a write to R13 pulses env_reset, which re-arms the envelope — so
;             the sweep gets a clean start on each shape without a chip reset
;   :371-391  the shape table this sweeps: \___ \\\\ \/\/ \--- //// /--- /\/\ /___
;
; The dwell is a plain counted loop, not a frame wait, and interrupts are
; disabled: the row must be deterministic to the sample, and a HALT-based
; delay would couple it to interrupt timing.

        org  $8000

start:
        di

        ld   de, $073F          ; R7  = 0x3F  all tone + noise disabled, so
        call ay_out             ;             chan_mixed(0) is '1' every clock
        ld   de, $0810          ; R8  = 0x10  channel A takes the envelope
        call ay_out
        ld   de, $0900          ; R9  = 0x00  channel B silent
        call ay_out
        ld   de, $0A00          ; R10 = 0x00  channel C silent
        call ay_out
        ld   de, $0B40          ; R11 = 0x40  envelope period low byte
        call ay_out
        ld   de, $0C00          ; R12 = 0x00  envelope period high byte
        call ay_out

        ld   c, 0               ; C = envelope shape, 0..15
shape_loop:
        ld   d, $0D             ; R13 — the write also re-arms the envelope
        ld   e, c
        push bc
        call ay_out
        pop  bc

        ld   hl, $8000          ; fixed dwell, ~851k T-states (~12 frames)
dwell:
        dec  hl
        ld   a, h
        or   l
        jr   nz, dwell

        inc  c
        ld   a, c
        cp   16
        jr   nz, shape_loop

        ld   de, $0800          ; R8 = 0x00 — channel A silent again
        call ay_out
park:
        jr   park

; ---- AY register write: D = register index, E = value -------------------
ay_out:
        ld   bc, $FFFD
        out  (c), d
        ld   bc, $BFFD
        out  (c), e
        ret
