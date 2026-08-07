; Source of nmi_enable.bin — the fixture delayed-nmi-func injects (GH #209).
;
; Assembled by hand (16 bytes); the bytes are listed beside each instruction so
; the .bin can be regenerated with no toolchain:
;
;   printf '\x01\x3B\x24\x3E\x06\xED\x79\x01\x3B\x25\x3E\x18\xED\x79\x18\xFE' \
;       > nmi_enable.bin
;
; WHY IT EXISTS: an NMI button press does nothing unless its NextREG 0x06
; enable bit is set — bit 3 for the Multiface M1 button, bit 4 for the DivMMC
; DRIVE button (zxnext.vhd:2090-2091, `nmi_assert_mf` / `nmi_assert_divmmc`).
; NextZXOS leaves both clear after boot, so a test that just presses the button
; on a booted machine proves nothing: the press is correctly ignored. This
; opens both gates and then parks the CPU in a one-instruction loop, so the
; only way execution can leave 0x800E is an NMI actually being taken.
;
; It writes NextREG via the I/O ports rather than the Z80N NEXTREG opcode, so
; the same fixture would work on a machine without the Z80N extensions.

        org  $8000

        ld   bc, $243B      ; 01 3B 24 — NextREG register-select port
        ld   a, $06         ; 3E 06    — NR 0x06
        out  (c), a         ; ED 79
        ld   bc, $253B      ; 01 3B 25 — NextREG data port
        ld   a, $18         ; 3E 18    — bit 4 (DivMMC DRIVE) + bit 3 (MF M1)
        out  (c), a         ; ED 79
loop:   jr   loop           ; 18 FE    — park here; only an NMI leaves this PC
