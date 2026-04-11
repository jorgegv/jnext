SECTION INTERRUPT_FD_JUMP

ORG $FCFC

DEFB $C3    ; JP

DEFB $00    ; n
DEFB $00    ; n

SECTION INTERRUPT_FD_VECTOR

org $FD00

DEFS 257, $FC   ; FCFC vector on any FDXX address

SECTION code_user

EXTERN _im2_init_fastcall
EXTERN _on_interrupt_callback
EXTERN _update_im2
EXTERN _interrupt_isr_vector

PUBLIC _interrupt_init_FD
_interrupt_init_FD:
    DI

    ; im2_init((void *) 0xFD00);
    LD      HL, 0xFD00
    CALL    _im2_init_fastcall

    ; z80_wpoke(0xFCFD, (uint16_t) update_im2);
    LD      HL, _update_im2
    LD      (0xFCFD), HL

    LD      HL, 0xFCFC
    LD      (_interrupt_isr_vector), HL

    EI

    RET
