; dsl_demo — the program the debugger-script suite runs against (GH #26 WP7,
; dsl-frontend.md §9; the scripts are test/scripts/dsl/*.jds).
;
; ONE program exercising every kind of event a script watches, built twice:
;
;   make            dsl_demo.nex        the good build
;                   dsl_demo_buggy.nex  the same source with BUGGY defined
;                   dsl_demo.map        the MAP (--map) — both builds share it:
;                                       every BUGGY difference is an operand of
;                                       the same size, so no label moves
;                                       (`make` checks the two maps agree)
;   make install    copies the three into test/00regression/nex/
;
; Every frame (HALT on an IM 2 interrupt) the main loop:
;   0. polls Q: the frame counter at the first poll that sees Q down is
;      latched in `first_key` (never cleared). The poll is in the main loop,
;      AFTER `main_loop`, so a key a script presses at `main_loop` is seen
;      by this frame's poll only if it was applied at once, and by the next
;      frame's if it was queued for the frame edge, as §2.6 says it is;
;   1. writes the frame counter into the DATA area (`data_area`) and, masked
;      to 7 bits, into `mempoint_addr` — never 0xB7 in the good build
;      (BUGGY: 0xB7 once, at frame 20);
;   2. copies `patch_byte` to `patch_copy` (a script pokes `patch_byte`);
;   3. runs `trap_insn`, an `LD (trap_target),A` with A = 0xEE that a script
;      skips by setting PC (BUGGY: a second, unskipped copy right after);
;   4. BUGGY only: a stray write into the CODE area — its last byte,
;      `__data_crt_head - 1`, the top of the range #279's guard watches;
;   5. every 8 frames pages "level" pages into MMU0/MMU1 — NEXTREG 0x50,0x22
;      at `page_in_level`, NEXTREG 0x51,0x23 at `page_in_level_mmu1` — reads
;      them and pages the ROM back (0x50 then 0x51, both 0xFF)
;      (BUGGY: MMU1 = 0x24, inconsistent with MMU0);
;   6. every 16 frames uploads 256 bytes of sprite pattern through the zxnDMA
;      to port 0x5B (BUGGY: 128 bytes).
;
; The IM 2 handler (`isr` .. `isr_exit`) saves and restores every register it
; uses and counts frames in `frames`. It carries five FAULTS, selected by the
; byte `isr_fault` — 0 in the good build, 1 in the BUGGY one; a script may set
; it to pick another — each breaking ONE invariant of #279's interrupt-exit
; audit (span_invariants.jds):
;   1  IY returned one higher (a register the handler did not save);
;   2  MMU slot 7 left on page 0x0F (an MMU slot changed);
;   3  the return address rewritten to `main_wait` (the top of the stack);
;   4  the return made without EI (interrupts left disabled);
;   5  AF pushed once more than popped (SP two bytes deeper at the exit).
; Slot 7 is free for fault 2: the stack and the IM 2 table live in slot 5.
;
; The Copper runs a palette split every frame: MOVE NR 0x43 = 0x00 (first ULA
; palette), WAIT line 95, MOVE NR 0x43 = 0x02 (second ULA palette, set up with
; other colours), HALT (BUGGY: WAIT line 96).
;
; `magic` is 0xD5D5 once the program has initialised itself: a script arms its
; watches on `main_loop` only when `magic` says so, because before the NEX is
; loaded other code runs at these same addresses.

        SECTION code_user

        PUBLIC  _main, main_wait, main_loop, isr, isr_exit
        PUBLIC  page_in_level, page_in_level_mmu1, trap_insn, dma_upload
        EXTERN  __data_crt_head

IM2_TABLE   equ $BE00           ; 257 bytes of $BD: any vector -> $BDBD
IM2_JP      equ $BDBD           ; JP isr
STACK       equ $BD00           ; slot 5: slot 7 is fault 2's
DMA_PORT    equ $6B             ; zxnDMA (ZXN mode)
KEYS_QWERT  equ $FBFE           ; Q W E R T, Q = bit 0

IF BUGGY
MMU1_PAGE   equ $24             ; MMU1 != MMU0 + 1
WAIT_LINE   equ 96              ; the split one line late
DMA_LEN     equ 128             ; half the patterns
MP_VALUE    equ $B7             ; the forbidden MemPoint value
FAULT_DEFAULT equ 1             ; the handler's fault: IY clobbered
ELSE
MMU1_PAGE   equ $23
WAIT_LINE   equ 95
DMA_LEN     equ 256
MP_VALUE    equ $37             ; an allowed one
FAULT_DEFAULT equ 0             ; no fault
ENDIF

_main:
        di
        ld   sp, STACK
        xor  a
        out  ($FE), a           ; black border
        ld   (frames), a
        ld   (first_key), a
        ld   (patch_byte), a
        ld   (patch_copy), a
        ld   (trap_target), a
        ld   a, FAULT_DEFAULT
        ld   (isr_fault), a
        ld   hl, $4000          ; bitmap: vertical stripes
        ld   de, $4001
        ld   bc, 6143
        ld   (hl), $AA
        ldir
        ld   hl, $5800          ; attributes: white on blue
        ld   de, $5801
        ld   bc, 767
        ld   (hl), $0F
        ldir
        ; The second ULA palette, for the lower part of the split: the 16
        ; colours reversed.
        nextreg $43, $40        ; read/write the second ULA palette
        nextreg $40, 0
        ld   b, 16
        ld   a, $FF
pal:    nextreg $41, a
        sub  $11
        djnz pal
        nextreg $43, $00        ; first ULA palette active
        call copper_setup
        ; IM 2.
        ld   hl, IM2_TABLE
        ld   de, IM2_TABLE + 1
        ld   bc, 256
        ld   (hl), IM2_JP / 256
        ldir
        ld   a, $C3
        ld   (IM2_JP), a
        ld   hl, isr
        ld   (IM2_JP + 1), hl
        ld   a, IM2_TABLE / 256
        ld   i, a
        im   2
        ld   hl, $D5D5
        ld   (magic), hl
        ei

main_wait:
        halt
main_loop:
        ; 0. the keyboard poll
        ld   a, (first_key)     ; latch the frame of the first Q seen
        or   a
        jr   nz, poll_done
        ld   bc, KEYS_QWERT
        in   a, (c)
        bit  0, a
        jr   nz, poll_done
        ld   a, (frames)
        ld   (first_key), a
poll_done:
        ; 1. the data area, and the MemPoint address
        ld   a, (frames)
        ld   (data_area), a
        and  $7F
        ld   c, a
        ld   a, (frames)
        cp   20
        ld   a, c
        jr   nz, mp_ok
        ld   a, MP_VALUE        ; at frame 20 (BUGGY: the forbidden 0xB7)
mp_ok:
        ld   (mempoint_addr), a
        ; 2. a byte a script patches
        ld   a, (patch_byte)
        ld   (patch_copy), a
        ; 3. the instruction a script skips
        ld   a, $EE
trap_insn:
        ld   (trap_target), a
IF BUGGY
        ld   (trap_target), a   ; a second, unskipped write
        ; 4. a stray write into the code area
        ld   (__data_crt_head - 1), a
ELSE
        ld   (trap_sink), a     ; the same instructions, harmless targets
        ld   (data_sink), a
ENDIF
        ; 5. MMU paging, every 8 frames
        ld   a, (frames)
        and  7
        call z, paging
        ; 6. the DMA sprite upload, every 16 frames
        ld   a, (frames)
        and  15
        call z, dma_upload
        jr   main_wait

paging:
page_in_level:
        nextreg $50, $22
page_in_level_mmu1:
        nextreg $51, MMU1_PAGE
        ld   a, ($0000)         ; read the level
        nextreg $50, $FF        ; the ROM back: MMU0 first, then MMU1
        nextreg $51, $FF
        ret

dma_upload:
        ld   bc, $303B          ; sprite pattern slot 0
        xor  a
        out  (c), a
        ld   hl, dma_prog
        ld   b, dma_prog_end - dma_prog
        ld   c, DMA_PORT
        otir
        ret

; The DMA program, written with one OTIR (dma.vhd register protocol).
dma_prog:
        defb $83                ; WR6: DISABLE
        defb $7D                ; WR0: A -> B, transfer; port A + length follow
        defw sprite_patterns    ;      port A address
        defw DMA_LEN            ;      block length
        defb $14                ; WR1: port A is memory, incrementing
        defb $28                ; WR2: port B is I/O, fixed address
        defb $AD                ; WR4: continuous; port B address follows
        defw $005B              ;      port B: the sprite pattern port
        defb $82                ; WR5: stop at end of block
        defb $CF                ; WR6: LOAD
        defb $87                ; WR6: ENABLE
dma_prog_end:

; The Copper list: two MOVEs and a WAIT between them, then HALT.
copper_setup:
        nextreg $62, $00        ; stop, address 0
        nextreg $61, $00
        ld   hl, copper_list
        ld   b, copper_list_end - copper_list
cop:    ld   a, (hl)
        nextreg $60, a
        inc  hl
        djnz cop
        nextreg $62, $C0        ; run, from 0 every frame
        ret

copper_list:
        defb $43, $00                           ; MOVE NR 0x43, 0x00
        defb $80 | (WAIT_LINE >> 8), WAIT_LINE & $FF   ; WAIT line, h 0
        defb $43, $02                           ; MOVE NR 0x43, 0x02
        defb $FF, $FF                           ; HALT
copper_list_end:

isr:
        push af
        push bc
        push hl
        ld   hl, frames
        inc  (hl)
        ld   a, (isr_fault)
        cp   1
        jr   nz, isr_f2
        inc  iy                 ; 1: a register the handler did not save
isr_f2: cp   2
        jr   nz, isr_f3
        nextreg $57, $0F        ; 2: MMU slot 7 left on another page
isr_f3: cp   3
        jr   nz, isr_f4
        ld   hl, 6              ; 3: the return address rewritten
        add  hl, sp
        ld   (hl), main_wait & $FF
        inc  hl
        ld   (hl), main_wait / 256
isr_f4: cp   4                  ; the flags survive the two POPs
        pop  hl
        pop  bc
        jr   z, isr_di          ; 4: return with interrupts disabled
        cp   5                  ; A is still the fault
        jr   z, isr_sp
        pop  af
        ei
isr_exit:
        reti
isr_di: pop  af
        jp   isr_exit
isr_sp: pop  af
        push af                 ; 5: SP two bytes deeper at the exit
        ei
        jp   isr_exit

sprite_patterns:
        defs 256, $E3           ; transparent-index pattern bytes


        SECTION bss_user

        PUBLIC  frames, first_key, magic, data_area, mempoint_addr
        PUBLIC  patch_byte, patch_copy, trap_target, isr_fault

frames:         defs 1
first_key:      defs 1
magic:          defs 2
data_area:      defs 16
mempoint_addr:  defs 1
patch_byte:     defs 1
patch_copy:     defs 1
trap_target:    defs 1
isr_fault:      defs 1
trap_sink:      defs 1
data_sink:      defs 1
