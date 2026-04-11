SECTION INTERRUPT_FE_JUMP

ORG $FDFD

DEFB $C3    ; JP

DEFB $00    ; n
DEFB $00    ; n

SECTION INTERRUPT_FE_VECTOR

org $FE00

DEFS 257, $FD   ; FDFD vector on any FEXX address

SECTION code_user

EXTERN _im2_init_fastcall
EXTERN _on_interrupt_callback
EXTERN _update_im2
EXTERN _interrupt_isr_vector

PUBLIC _interrupt_init_FE
_interrupt_init_FE:
    DI

    ; im2_init((void *) 0xFE00);
    LD      HL, 0xFE00
    CALL    _im2_init_fastcall

    ; z80_wpoke(0xFCFD, (uint16_t) update_im2);
    LD      HL, _update_im2
    LD      (0xFDFE), HL

    LD      HL, 0xFDFD
    LD      (_interrupt_isr_vector), HL

    EI

    RET
