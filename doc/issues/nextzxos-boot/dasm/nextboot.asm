                    di                                      ;[0000] f3
                    im        1                             ;[0001] ed 56
                    jp        $0080                         ;[0003] c3 80 00
                    rst       $38                           ;[0006] ff
                    rst       $38                           ;[0007] ff
                    reti                                    ;[0008] ed 4d

                    rst       $38                           ;[000a] ff
                    rst       $38                           ;[000b] ff
                    rst       $38                           ;[000c] ff
                    rst       $38                           ;[000d] ff
                    rst       $38                           ;[000e] ff
                    rst       $38                           ;[000f] ff
                    reti                                    ;[0010] ed 4d

                    rst       $38                           ;[0012] ff
                    rst       $38                           ;[0013] ff
                    rst       $38                           ;[0014] ff
                    rst       $38                           ;[0015] ff
                    rst       $38                           ;[0016] ff
                    rst       $38                           ;[0017] ff
                    reti                                    ;[0018] ed 4d

                    rst       $38                           ;[001a] ff
                    rst       $38                           ;[001b] ff
                    rst       $38                           ;[001c] ff
                    rst       $38                           ;[001d] ff
                    rst       $38                           ;[001e] ff
                    rst       $38                           ;[001f] ff
                    reti                                    ;[0020] ed 4d

                    rst       $38                           ;[0022] ff
                    rst       $38                           ;[0023] ff
                    rst       $38                           ;[0024] ff
                    rst       $38                           ;[0025] ff
                    rst       $38                           ;[0026] ff
                    rst       $38                           ;[0027] ff
                    reti                                    ;[0028] ed 4d

                    rst       $38                           ;[002a] ff
                    rst       $38                           ;[002b] ff
                    rst       $38                           ;[002c] ff
                    rst       $38                           ;[002d] ff
                    rst       $38                           ;[002e] ff
                    rst       $38                           ;[002f] ff
                    reti                                    ;[0030] ed 4d

                    rst       $38                           ;[0032] ff
                    rst       $38                           ;[0033] ff
                    rst       $38                           ;[0034] ff
                    rst       $38                           ;[0035] ff
                    rst       $38                           ;[0036] ff
                    rst       $38                           ;[0037] ff
                    reti                                    ;[0038] ed 4d

                    rst       $38                           ;[003a] ff
                    rst       $38                           ;[003b] ff
                    rst       $38                           ;[003c] ff
                    rst       $38                           ;[003d] ff
                    rst       $38                           ;[003e] ff
                    rst       $38                           ;[003f] ff
                    rst       $38                           ;[0040] ff
                    rst       $38                           ;[0041] ff
                    rst       $38                           ;[0042] ff
                    rst       $38                           ;[0043] ff
                    rst       $38                           ;[0044] ff
                    rst       $38                           ;[0045] ff
                    rst       $38                           ;[0046] ff
                    rst       $38                           ;[0047] ff
                    rst       $38                           ;[0048] ff
                    rst       $38                           ;[0049] ff
                    rst       $38                           ;[004a] ff
                    rst       $38                           ;[004b] ff
                    rst       $38                           ;[004c] ff
                    rst       $38                           ;[004d] ff
                    rst       $38                           ;[004e] ff
                    rst       $38                           ;[004f] ff
                    rst       $38                           ;[0050] ff
                    rst       $38                           ;[0051] ff
                    rst       $38                           ;[0052] ff
                    rst       $38                           ;[0053] ff
                    rst       $38                           ;[0054] ff
                    rst       $38                           ;[0055] ff
                    rst       $38                           ;[0056] ff
                    rst       $38                           ;[0057] ff
                    rst       $38                           ;[0058] ff
                    rst       $38                           ;[0059] ff
                    rst       $38                           ;[005a] ff
                    rst       $38                           ;[005b] ff
                    rst       $38                           ;[005c] ff
                    rst       $38                           ;[005d] ff
                    rst       $38                           ;[005e] ff
                    rst       $38                           ;[005f] ff
                    rst       $38                           ;[0060] ff
                    rst       $38                           ;[0061] ff
                    rst       $38                           ;[0062] ff
                    rst       $38                           ;[0063] ff
                    rst       $38                           ;[0064] ff
                    rst       $38                           ;[0065] ff
                    retn                                    ;[0066] ed 45

                    rst       $38                           ;[0068] ff
                    rst       $38                           ;[0069] ff
                    rst       $38                           ;[006a] ff
                    rst       $38                           ;[006b] ff
                    rst       $38                           ;[006c] ff
                    rst       $38                           ;[006d] ff
                    rst       $38                           ;[006e] ff
                    rst       $38                           ;[006f] ff
                    rst       $38                           ;[0070] ff
                    rst       $38                           ;[0071] ff
                    rst       $38                           ;[0072] ff
                    rst       $38                           ;[0073] ff
                    rst       $38                           ;[0074] ff
                    rst       $38                           ;[0075] ff
                    rst       $38                           ;[0076] ff
                    rst       $38                           ;[0077] ff
                    rst       $38                           ;[0078] ff
                    rst       $38                           ;[0079] ff
                    rst       $38                           ;[007a] ff
                    rst       $38                           ;[007b] ff
                    rst       $38                           ;[007c] ff
                    rst       $38                           ;[007d] ff
                    rst       $38                           ;[007e] ff
                    rst       $38                           ;[007f] ff
                    ld        sp,$ffff                      ;[0080] 31 ff ff
                    call      $1cc7                         ;[0083] cd c7 1c
                    call      $0145                         ;[0086] cd 45 01
                    jp        $0100                         ;[0089] c3 00 01
                    rst       $38                           ;[008c] ff
                    rst       $38                           ;[008d] ff
                    rst       $38                           ;[008e] ff
                    rst       $38                           ;[008f] ff
                    rst       $38                           ;[0090] ff
                    rst       $38                           ;[0091] ff
                    rst       $38                           ;[0092] ff
                    rst       $38                           ;[0093] ff
                    rst       $38                           ;[0094] ff
                    rst       $38                           ;[0095] ff
                    rst       $38                           ;[0096] ff
                    rst       $38                           ;[0097] ff
                    rst       $38                           ;[0098] ff
                    rst       $38                           ;[0099] ff
                    rst       $38                           ;[009a] ff
                    rst       $38                           ;[009b] ff
                    rst       $38                           ;[009c] ff
                    rst       $38                           ;[009d] ff
                    rst       $38                           ;[009e] ff
                    rst       $38                           ;[009f] ff
                    rst       $38                           ;[00a0] ff
                    rst       $38                           ;[00a1] ff
                    rst       $38                           ;[00a2] ff
                    rst       $38                           ;[00a3] ff
                    rst       $38                           ;[00a4] ff
                    rst       $38                           ;[00a5] ff
                    rst       $38                           ;[00a6] ff
                    rst       $38                           ;[00a7] ff
                    rst       $38                           ;[00a8] ff
                    rst       $38                           ;[00a9] ff
                    rst       $38                           ;[00aa] ff
                    rst       $38                           ;[00ab] ff
                    rst       $38                           ;[00ac] ff
                    rst       $38                           ;[00ad] ff
                    rst       $38                           ;[00ae] ff
                    rst       $38                           ;[00af] ff
                    rst       $38                           ;[00b0] ff
                    rst       $38                           ;[00b1] ff
                    rst       $38                           ;[00b2] ff
                    rst       $38                           ;[00b3] ff
                    rst       $38                           ;[00b4] ff
                    rst       $38                           ;[00b5] ff
                    rst       $38                           ;[00b6] ff
                    rst       $38                           ;[00b7] ff
                    rst       $38                           ;[00b8] ff
                    rst       $38                           ;[00b9] ff
                    rst       $38                           ;[00ba] ff
                    rst       $38                           ;[00bb] ff
                    rst       $38                           ;[00bc] ff
                    rst       $38                           ;[00bd] ff
                    rst       $38                           ;[00be] ff
                    rst       $38                           ;[00bf] ff
                    rst       $38                           ;[00c0] ff
                    rst       $38                           ;[00c1] ff
                    rst       $38                           ;[00c2] ff
                    rst       $38                           ;[00c3] ff
                    rst       $38                           ;[00c4] ff
                    rst       $38                           ;[00c5] ff
                    rst       $38                           ;[00c6] ff
                    rst       $38                           ;[00c7] ff
                    rst       $38                           ;[00c8] ff
                    rst       $38                           ;[00c9] ff
                    rst       $38                           ;[00ca] ff
                    rst       $38                           ;[00cb] ff
                    rst       $38                           ;[00cc] ff
                    rst       $38                           ;[00cd] ff
                    rst       $38                           ;[00ce] ff
                    rst       $38                           ;[00cf] ff
                    rst       $38                           ;[00d0] ff
                    rst       $38                           ;[00d1] ff
                    rst       $38                           ;[00d2] ff
                    rst       $38                           ;[00d3] ff
                    rst       $38                           ;[00d4] ff
                    rst       $38                           ;[00d5] ff
                    rst       $38                           ;[00d6] ff
                    rst       $38                           ;[00d7] ff
                    rst       $38                           ;[00d8] ff
                    rst       $38                           ;[00d9] ff
                    rst       $38                           ;[00da] ff
                    rst       $38                           ;[00db] ff
                    rst       $38                           ;[00dc] ff
                    rst       $38                           ;[00dd] ff
                    rst       $38                           ;[00de] ff
                    rst       $38                           ;[00df] ff
                    rst       $38                           ;[00e0] ff
                    rst       $38                           ;[00e1] ff
                    rst       $38                           ;[00e2] ff
                    rst       $38                           ;[00e3] ff
                    rst       $38                           ;[00e4] ff
                    rst       $38                           ;[00e5] ff
                    rst       $38                           ;[00e6] ff
                    rst       $38                           ;[00e7] ff
                    rst       $38                           ;[00e8] ff
                    rst       $38                           ;[00e9] ff
                    rst       $38                           ;[00ea] ff
                    rst       $38                           ;[00eb] ff
                    rst       $38                           ;[00ec] ff
                    rst       $38                           ;[00ed] ff
                    rst       $38                           ;[00ee] ff
                    rst       $38                           ;[00ef] ff
                    rst       $38                           ;[00f0] ff
                    rst       $38                           ;[00f1] ff
                    rst       $38                           ;[00f2] ff
                    rst       $38                           ;[00f3] ff
                    rst       $38                           ;[00f4] ff
                    rst       $38                           ;[00f5] ff
                    rst       $38                           ;[00f6] ff
                    rst       $38                           ;[00f7] ff
                    rst       $38                           ;[00f8] ff
                    rst       $38                           ;[00f9] ff
                    rst       $38                           ;[00fa] ff
                    rst       $38                           ;[00fb] ff
                    rst       $38                           ;[00fc] ff
                    rst       $38                           ;[00fd] ff
                    rst       $38                           ;[00fe] ff
                    rst       $38                           ;[00ff] ff
                    halt                                    ;[0100] 76
                    jr        $0100                         ;[0101] 18 fd
                    ld        bc,$5800                      ;[0103] 01 00 58
                    ld        l,c                           ;[0106] 69
                    ld        h,b                           ;[0107] 60
                    ld        (hl),$47                      ;[0108] 36 47
                    inc       bc                            ;[010a] 03
                    ld        a,b                           ;[010b] 78
                    sub       $5b                           ;[010c] d6 5b
                    jr        c,$0106                       ;[010e] 38 f6
                    ld        a,$ff                         ;[0110] 3e ff
                    out       ($e7),a                       ;[0112] d3 e7
                    pop       bc                            ;[0114] c1
                    pop       hl                            ;[0115] e1
                    push      hl                            ;[0116] e5
                    push      bc                            ;[0117] c5
                    push      hl                            ;[0118] e5
                    call      $1b18                         ;[0119] cd 18 1b
                    pop       af                            ;[011c] f1
                    ld        c,l                           ;[011d] 4d
                    srl       h                             ;[011e] cb 3c
                    rr        c                             ;[0120] cb 19
                    ld        hl,$f801                      ;[0122] 21 01 f8
                    ld        a,$10                         ;[0125] 3e 10
                    sub       c                             ;[0127] 91
                    ld        (hl),a                        ;[0128] 77
                    ld        a,$02                         ;[0129] 3e 02
                    out       ($fe),a                       ;[012b] d3 fe
                    ld        a,$0c                         ;[012d] 3e 0c
                    push      af                            ;[012f] f5
                    inc       sp                            ;[0130] 33
                    ld        a,($f801)                     ;[0131] 3a 01 f8
                    push      af                            ;[0134] f5
                    inc       sp                            ;[0135] 33
                    call      $19f5                         ;[0136] cd f5 19
                    pop       af                            ;[0139] f1
                    pop       bc                            ;[013a] c1
                    pop       hl                            ;[013b] e1
                    push      hl                            ;[013c] e5
                    push      bc                            ;[013d] c5
                    push      hl                            ;[013e] e5
                    call      $1b05                         ;[013f] cd 05 1b
                    pop       af                            ;[0142] f1
                    jr        $0143                         ;[0143] 18 fe
                    ld        hl,$ffe5                      ;[0145] 21 e5 ff
                    add       hl,sp                         ;[0148] 39
                    ld        sp,hl                         ;[0149] f9
                    ld        hl,$0009                      ;[014a] 21 09 00
                    add       hl,sp                         ;[014d] 39
                    xor       a                             ;[014e] af
                    ld        (hl),a                        ;[014f] 77
                    inc       hl                            ;[0150] 23
                    ld        (hl),a                        ;[0151] 77
                    ld        a,$00                         ;[0152] 3e 00
                    out       ($fe),a                       ;[0154] d3 fe
                    ld        a,$00                         ;[0156] 3e 00
                    ld        bc,$243b                      ;[0158] 01 3b 24
                    out       (c),a                         ;[015b] ed 79
                    ld        a,$25                         ;[015d] 3e 25
                    in        a,($3b)                       ;[015f] db 3b
                    ld        iy,$0003                      ;[0161] fd 21 03 00
                    add       iy,sp                         ;[0165] fd 39
                    ld        (iy+$00),a                    ;[0167] fd 77 00
                    ld        a,$01                         ;[016a] 3e 01
                    ld        bc,$243b                      ;[016c] 01 3b 24
                    out       (c),a                         ;[016f] ed 79
                    ld        a,$25                         ;[0171] 3e 25
                    in        a,($3b)                       ;[0173] db 3b
                    ld        a,$02                         ;[0175] 3e 02
                    ld        bc,$243b                      ;[0177] 01 3b 24
                    out       (c),a                         ;[017a] ed 79
                    ld        a,$25                         ;[017c] 3e 25
                    in        a,($3b)                       ;[017e] db 3b
                    ld        a,$10                         ;[0180] 3e 10
                    ld        bc,$243b                      ;[0182] 01 3b 24
                    out       (c),a                         ;[0185] ed 79
                    ld        a,$25                         ;[0187] 3e 25
                    in        a,($3b)                       ;[0189] db 3b
                    and       $03                           ;[018b] e6 03
                    ld        iy,$0002                      ;[018d] fd 21 02 00
                    add       iy,sp                         ;[0191] fd 39
                    ld        (iy+$00),a                    ;[0193] fd 77 00
                    ld        iy,$0006                      ;[0196] fd 21 06 00
                    add       iy,sp                         ;[019a] fd 39
                    ld        (iy+$00),$00                  ;[019c] fd 36 00 00
                    ld        a,$7f                         ;[01a0] 3e 7f
                    in        a,($fe)                       ;[01a2] db fe
                    rrca                                    ;[01a4] 0f
                    jr        c,$01b1                       ;[01a5] 38 0a
                    ld        iy,$0006                      ;[01a7] fd 21 06 00
                    add       iy,sp                         ;[01ab] fd 39
                    ld        (iy+$00),$01                  ;[01ad] fd 36 00 01
                    call      $1884                         ;[01b1] cd 84 18
                    ld        hl,$f800                      ;[01b4] 21 00 f8
                    ld        (hl),$0a                      ;[01b7] 36 0a
                    ld        a,($f800)                     ;[01b9] 3a 00 f8
                    or        a                             ;[01bc] b7
                    jr        z,$01ec                       ;[01bd] 28 2d
                    call      $045f                         ;[01bf] cd 5f 04
                    ld        a,l                           ;[01c2] 7d
                    or        a                             ;[01c3] b7
                    jr        nz,$01ce                      ;[01c4] 20 08
                    ld        hl,$03d9                      ;[01c6] 21 d9 03
                    push      hl                            ;[01c9] e5
                    call      $0103                         ;[01ca] cd 03 01
                    pop       af                            ;[01cd] f1
                    call      $07dd                         ;[01ce] cd dd 07
                    ld        a,l                           ;[01d1] 7d
                    or        a                             ;[01d2] b7
                    jr        nz,$01ec                      ;[01d3] 20 17
                    ld        hl,$f800                      ;[01d5] 21 00 f8
                    dec       (hl)                          ;[01d8] 35
                    ld        bc,$fde8                      ;[01d9] 01 e8 fd
                    dec       bc                            ;[01dc] 0b
                    ld        a,b                           ;[01dd] 78
                    or        c                             ;[01de] b1
                    jr        nz,$01dc                      ;[01df] 20 fb
                    ld        hl,$0009                      ;[01e1] 21 09 00
                    add       hl,sp                         ;[01e4] 39
                    ld        (hl),$e8                      ;[01e5] 36 e8
                    inc       hl                            ;[01e7] 23
                    ld        (hl),$fd                      ;[01e8] 36 fd
                    jr        $01b9                         ;[01ea] 18 cd
                    ld        a,($f800)                     ;[01ec] 3a 00 f8
                    or        a                             ;[01ef] b7
                    jr        nz,$01fa                      ;[01f0] 20 08
                    ld        hl,$03f5                      ;[01f2] 21 f5 03
                    push      hl                            ;[01f5] e5
                    call      $0103                         ;[01f6] cd 03 01
                    pop       af                            ;[01f9] f1
                    ld        iy,$0005                      ;[01fa] fd 21 05 00
                    add       iy,sp                         ;[01fe] fd 39
                    ld        (iy+$00),$00                  ;[0200] fd 36 00 00
                    ld        iy,$0004                      ;[0204] fd 21 04 00
                    add       iy,sp                         ;[0208] fd 39
                    ld        (iy+$00),$00                  ;[020a] fd 36 00 00
                    ld        hl,$000b                      ;[020e] 21 0b 00
                    add       hl,sp                         ;[0211] 39
                    ld        iy,$0019                      ;[0212] fd 21 19 00
                    add       iy,sp                         ;[0216] fd 39
                    ld        (iy+$00),l                    ;[0218] fd 75 00
                    ld        (iy+$01),h                    ;[021b] fd 74 01
                    ld        c,(iy+$00)                    ;[021e] fd 4e 00
                    ld        b,(iy+$01)                    ;[0221] fd 46 01
                    ld        hl,($fc39)                    ;[0224] 2a 39 fc
                    push      hl                            ;[0227] e5
                    push      bc                            ;[0228] c5
                    call      $103d                         ;[0229] cd 3d 10
                    pop       af                            ;[022c] f1
                    pop       af                            ;[022d] f1
                    ld        a,l                           ;[022e] 7d
                    or        a                             ;[022f] b7
                    jr        z,$023c                       ;[0230] 28 0a
                    ld        iy,$0005                      ;[0232] fd 21 05 00
                    add       iy,sp                         ;[0236] fd 39
                    ld        (iy+$00),$01                  ;[0238] fd 36 00 01
                    ld        a,$df                         ;[023c] 3e df
                    in        a,($fe)                       ;[023e] db fe
                    and       $08                           ;[0240] e6 08
                    jr        nz,$024e                      ;[0242] 20 0a
                    ld        iy,$0004                      ;[0244] fd 21 04 00
                    add       iy,sp                         ;[0248] fd 39
                    ld        (iy+$00),$01                  ;[024a] fd 36 00 01
                    ld        hl,$0003                      ;[024e] 21 03 00
                    add       hl,sp                         ;[0251] 39
                    ld        a,(hl)                        ;[0252] 7e
                    sub       $fa                           ;[0253] d6 fa
                    jr        nz,$027c                      ;[0255] 20 25
                    ld        hl,$0002                      ;[0257] 21 02 00
                    add       hl,sp                         ;[025a] 39
                    ld        a,(hl)                        ;[025b] 7e
                    sub       $03                           ;[025c] d6 03
                    jr        nz,$026c                      ;[025e] 20 0c
                    ld        iy,$0004                      ;[0260] fd 21 04 00
                    add       iy,sp                         ;[0264] fd 39
                    ld        (iy+$00),$01                  ;[0266] fd 36 00 01
                    jr        $027c                         ;[026a] 18 10
                    ld        a,$10                         ;[026c] 3e 10
                    ld        bc,$243b                      ;[026e] 01 3b 24
                    out       (c),a                         ;[0271] ed 79
                    ld        a,$80                         ;[0273] 3e 80
                    ld        bc,$253b                      ;[0275] 01 3b 25
                    out       (c),a                         ;[0278] ed 79
                    jr        $027a                         ;[027a] 18 fe
                    ld        hl,$0019                      ;[027c] 21 19 00
                    add       hl,sp                         ;[027f] 39
                    ld        c,(hl)                        ;[0280] 4e
                    inc       hl                            ;[0281] 23
                    ld        b,(hl)                        ;[0282] 46
                    ld        hl,($fc3b)                    ;[0283] 2a 3b fc
                    push      hl                            ;[0286] e5
                    push      bc                            ;[0287] c5
                    call      $103d                         ;[0288] cd 3d 10
                    pop       af                            ;[028b] f1
                    pop       af                            ;[028c] f1
                    ld        a,l                           ;[028d] 7d
                    or        a                             ;[028e] b7
                    jr        nz,$0299                      ;[028f] 20 08
                    ld        hl,$040d                      ;[0291] 21 0d 04
                    push      hl                            ;[0294] e5
                    call      $0103                         ;[0295] cd 03 01
                    pop       af                            ;[0298] f1
                    ld        de,$f802                      ;[0299] 11 02 f8
                    ld        hl,$0019                      ;[029c] 21 19 00
                    add       hl,sp                         ;[029f] 39
                    ld        c,(hl)                        ;[02a0] 4e
                    inc       hl                            ;[02a1] 23
                    ld        b,(hl)                        ;[02a2] 46
                    push      de                            ;[02a3] d5
                    push      bc                            ;[02a4] c5
                    call      $13ef                         ;[02a5] cd ef 13
                    pop       af                            ;[02a8] f1
                    pop       af                            ;[02a9] f1
                    ld        a,l                           ;[02aa] 7d
                    or        a                             ;[02ab] b7
                    jr        nz,$02b6                      ;[02ac] 20 08
                    ld        hl,$042a                      ;[02ae] 21 2a 04
                    push      hl                            ;[02b1] e5
                    call      $0103                         ;[02b2] cd 03 01
                    pop       af                            ;[02b5] f1
                    ld        hl,$0005                      ;[02b6] 21 05 00
                    add       hl,sp                         ;[02b9] 39
                    ld        a,(hl)                        ;[02ba] 7e
                    dec       a                             ;[02bb] 3d
                    jr        nz,$02d0                      ;[02bc] 20 12
                    ld        hl,$0004                      ;[02be] 21 04 00
                    add       hl,sp                         ;[02c1] 39
                    ld        a,(hl)                        ;[02c2] 7e
                    dec       a                             ;[02c3] 3d
                    jr        nz,$02d0                      ;[02c4] 20 0a
                    ld        iy,$0006                      ;[02c6] fd 21 06 00
                    add       iy,sp                         ;[02ca] fd 39
                    ld        (iy+$00),$02                  ;[02cc] fd 36 00 02
                    ld        a,$fe                         ;[02d0] 3e fe
                    in        a,($fe)                       ;[02d2] db fe
                    and       $08                           ;[02d4] e6 08
                    jr        nz,$02e2                      ;[02d6] 20 0a
                    ld        iy,$0006                      ;[02d8] fd 21 06 00
                    add       iy,sp                         ;[02dc] fd 39
                    ld        (iy+$00),$03                  ;[02de] fd 36 00 03
                    ld        iy,$0006                      ;[02e2] fd 21 06 00
                    add       iy,sp                         ;[02e6] fd 39
                    ld        l,(iy+$00)                    ;[02e8] fd 6e 00
                    ld        h,$00                         ;[02eb] 26 00
                    add       hl,hl                         ;[02ed] 29
                    add       hl,hl                         ;[02ee] 29
                    ex        de,hl                         ;[02ef] eb
                    ld        hl,$f802                      ;[02f0] 21 02 f8
                    add       hl,de                         ;[02f3] 19
                    ld        c,(hl)                        ;[02f4] 4e
                    ld        b,$00                         ;[02f5] 06 00
                    ld        l,e                           ;[02f7] 6b
                    ld        h,d                           ;[02f8] 62
                    inc       hl                            ;[02f9] 23
                    ld        a,$02                         ;[02fa] 3e 02
                    add       l                             ;[02fc] 85
                    ld        l,a                           ;[02fd] 6f
                    ld        a,$f8                         ;[02fe] 3e f8
                    adc       h                             ;[0300] 8c
                    ld        h,a                           ;[0301] 67
                    ld        h,(hl)                        ;[0302] 66
                    ld        l,$00                         ;[0303] 2e 00
                    add       hl,bc                         ;[0305] 09
                    ld        iy,$0017                      ;[0306] fd 21 17 00
                    add       iy,sp                         ;[030a] fd 39
                    ld        (iy+$00),l                    ;[030c] fd 75 00
                    ld        (iy+$01),h                    ;[030f] fd 74 01
                    ld        c,e                           ;[0312] 4b
                    ld        b,d                           ;[0313] 42
                    inc       bc                            ;[0314] 03
                    inc       bc                            ;[0315] 03
                    ld        hl,$f802                      ;[0316] 21 02 f8
                    add       hl,bc                         ;[0319] 09
                    ld        c,(hl)                        ;[031a] 4e
                    ld        b,$00                         ;[031b] 06 00
                    inc       de                            ;[031d] 13
                    inc       de                            ;[031e] 13
                    inc       de                            ;[031f] 13
                    ld        hl,$f802                      ;[0320] 21 02 f8
                    add       hl,de                         ;[0323] 19
                    ld        h,(hl)                        ;[0324] 66
                    ld        l,$00                         ;[0325] 2e 00
                    add       hl,bc                         ;[0327] 09
                    ld        iy,$0007                      ;[0328] fd 21 07 00
                    add       iy,sp                         ;[032c] fd 39
                    ld        (iy+$00),l                    ;[032e] fd 75 00
                    ld        (iy+$01),h                    ;[0331] fd 74 01
                    ld        hl,$0019                      ;[0334] 21 19 00
                    add       hl,sp                         ;[0337] 39
                    ld        c,(hl)                        ;[0338] 4e
                    inc       hl                            ;[0339] 23
                    ld        b,(hl)                        ;[033a] 46
                    ld        hl,$0009                      ;[033b] 21 09 00
                    add       hl,sp                         ;[033e] 39
                    ld        e,(hl)                        ;[033f] 5e
                    inc       hl                            ;[0340] 23
                    ld        d,(hl)                        ;[0341] 56
                    ld        hl,$0017                      ;[0342] 21 17 00
                    add       hl,sp                         ;[0345] 39
                    ld        a,e                           ;[0346] 7b
                    sub       (hl)                          ;[0347] 96
                    ld        a,d                           ;[0348] 7a
                    inc       hl                            ;[0349] 23
                    sbc       (hl)                          ;[034a] 9e
                    jr        nc,$0372                      ;[034b] 30 25
                    ld        hl,$f802                      ;[034d] 21 02 f8
                    push      bc                            ;[0350] c5
                    pop       iy                            ;[0351] fd e1
                    push      bc                            ;[0353] c5
                    push      de                            ;[0354] d5
                    push      hl                            ;[0355] e5
                    push      iy                            ;[0356] fd e5
                    call      $13ef                         ;[0358] cd ef 13
                    pop       af                            ;[035b] f1
                    pop       af                            ;[035c] f1
                    pop       de                            ;[035d] d1
                    pop       bc                            ;[035e] c1
                    ld        a,l                           ;[035f] 7d
                    or        a                             ;[0360] b7
                    jr        nz,$036f                      ;[0361] 20 0c
                    push      bc                            ;[0363] c5
                    push      de                            ;[0364] d5
                    ld        hl,$042a                      ;[0365] 21 2a 04
                    push      hl                            ;[0368] e5
                    call      $0103                         ;[0369] cd 03 01
                    pop       af                            ;[036c] f1
                    pop       de                            ;[036d] d1
                    pop       bc                            ;[036e] c1
                    inc       de                            ;[036f] 13
                    jr        $0342                         ;[0370] 18 d0
                    ld        hl,$0009                      ;[0372] 21 09 00
                    add       hl,sp                         ;[0375] 39
                    xor       a                             ;[0376] af
                    ld        (hl),a                        ;[0377] 77
                    inc       hl                            ;[0378] 23
                    ld        (hl),a                        ;[0379] 77
                    ld        hl,$6000                      ;[037a] 21 00 60
                    ex        (sp),hl                       ;[037d] e3
                    ld        hl,$0007                      ;[037e] 21 07 00
                    add       hl,sp                         ;[0381] 39
                    ld        iy,$0009                      ;[0382] fd 21 09 00
                    add       iy,sp                         ;[0386] fd 39
                    ld        a,(iy+$00)                    ;[0388] fd 7e 00
                    sub       (hl)                          ;[038b] 96
                    ld        a,(iy+$01)                    ;[038c] fd 7e 01
                    inc       hl                            ;[038f] 23
                    sbc       (hl)                          ;[0390] 9e
                    jr        nc,$03cc                      ;[0391] 30 39
                    ld        hl,$0019                      ;[0393] 21 19 00
                    add       hl,sp                         ;[0396] 39
                    ld        c,(hl)                        ;[0397] 4e
                    inc       hl                            ;[0398] 23
                    ld        b,(hl)                        ;[0399] 46
                    pop       hl                            ;[039a] e1
                    push      hl                            ;[039b] e5
                    push      hl                            ;[039c] e5
                    push      bc                            ;[039d] c5
                    call      $13ef                         ;[039e] cd ef 13
                    pop       af                            ;[03a1] f1
                    pop       af                            ;[03a2] f1
                    ld        a,l                           ;[03a3] 7d
                    or        a                             ;[03a4] b7
                    jr        nz,$03af                      ;[03a5] 20 08
                    ld        hl,$042a                      ;[03a7] 21 2a 04
                    push      hl                            ;[03aa] e5
                    call      $0103                         ;[03ab] cd 03 01
                    pop       af                            ;[03ae] f1
                    ld        iy,$0009                      ;[03af] fd 21 09 00
                    add       iy,sp                         ;[03b3] fd 39
                    inc       (iy+$00)                      ;[03b5] fd 34 00
                    jr        nz,$03bd                      ;[03b8] 20 03
                    inc       (iy+$01)                      ;[03ba] fd 34 01
                    ld        hl,$0000                      ;[03bd] 21 00 00
                    add       hl,sp                         ;[03c0] 39
                    ld        a,(hl)                        ;[03c1] 7e
                    add       $00                           ;[03c2] c6 00
                    ld        (hl),a                        ;[03c4] 77
                    inc       hl                            ;[03c5] 23
                    ld        a,(hl)                        ;[03c6] 7e
                    adc       $02                           ;[03c7] ce 02
                    ld        (hl),a                        ;[03c9] 77
                    jr        $037e                         ;[03ca] 18 b2
                    ld        a,$ff                         ;[03cc] 3e ff
                    out       ($e7),a                       ;[03ce] d3 e7
                    jp        $6000                         ;[03d0] c3 00 60
                    ld        hl,$001b                      ;[03d3] 21 1b 00
                    add       hl,sp                         ;[03d6] 39
                    ld        sp,hl                         ;[03d7] f9
                    ret                                     ;[03d8] c9

                    ld        b,l                           ;[03d9] 45
                    ld        (hl),d                        ;[03da] 72
                    ld        (hl),d                        ;[03db] 72
                    ld        l,a                           ;[03dc] 6f
                    ld        (hl),d                        ;[03dd] 72
                    jr        nz,$0449                      ;[03de] 20 69
                    ld        l,(hl)                        ;[03e0] 6e
                    ld        l,c                           ;[03e1] 69
                    ld        (hl),h                        ;[03e2] 74
                    ld        l,c                           ;[03e3] 69
                    ld        h,c                           ;[03e4] 61
                    ld        l,h                           ;[03e5] 6c
                    ld        l,c                           ;[03e6] 69
                    ld        a,d                           ;[03e7] 7a
                    ld        l,c                           ;[03e8] 69
                    ld        l,(hl)                        ;[03e9] 6e
                    ld        h,a                           ;[03ea] 67
                    jr        nz,$0440                      ;[03eb] 20 53
                    ld        b,h                           ;[03ed] 44
                    jr        nz,$0453                      ;[03ee] 20 63
                    ld        h,c                           ;[03f0] 61
                    ld        (hl),d                        ;[03f1] 72
                    ld        h,h                           ;[03f2] 64
                    ld        hl,$4500                      ;[03f3] 21 00 45
                    ld        (hl),d                        ;[03f6] 72
                    ld        (hl),d                        ;[03f7] 72
                    ld        l,a                           ;[03f8] 6f
                    ld        (hl),d                        ;[03f9] 72
                    jr        nz,$0469                      ;[03fa] 20 6d
                    ld        l,a                           ;[03fc] 6f
                    ld        (hl),l                        ;[03fd] 75
                    ld        l,(hl)                        ;[03fe] 6e
                    ld        (hl),h                        ;[03ff] 74
                    ld        l,c                           ;[0400] 69
                    ld        l,(hl)                        ;[0401] 6e
                    ld        h,a                           ;[0402] 67
                    jr        nz,$0458                      ;[0403] 20 53
                    ld        b,h                           ;[0405] 44
                    jr        nz,$046b                      ;[0406] 20 63
                    ld        h,c                           ;[0408] 61
                    ld        (hl),d                        ;[0409] 72
                    ld        h,h                           ;[040a] 64
                    ld        hl,$4500                      ;[040b] 21 00 45
                    ld        (hl),d                        ;[040e] 72
                    ld        (hl),d                        ;[040f] 72
                    ld        l,a                           ;[0410] 6f
                    ld        (hl),d                        ;[0411] 72
                    jr        nz,$0483                      ;[0412] 20 6f
                    ld        (hl),b                        ;[0414] 70
                    ld        h,l                           ;[0415] 65
                    ld        l,(hl)                        ;[0416] 6e
                    ld        l,c                           ;[0417] 69
                    ld        l,(hl)                        ;[0418] 6e
                    ld        h,a                           ;[0419] 67
                    jr        nz,$0470                      ;[041a] 20 54
                    ld        b,d                           ;[041c] 42
                    ld        b,d                           ;[041d] 42
                    ld        c,h                           ;[041e] 4c
                    ld        d,l                           ;[041f] 55
                    ld        b,l                           ;[0420] 45
                    ld        l,$46                         ;[0421] 2e 46
                    ld        d,a                           ;[0423] 57
                    jr        nz,$048c                      ;[0424] 20 66
                    ld        l,c                           ;[0426] 69
                    ld        l,h                           ;[0427] 6c
                    ld        h,l                           ;[0428] 65
                    nop                                     ;[0429] 00
                    ld        b,l                           ;[042a] 45
                    ld        (hl),d                        ;[042b] 72
                    ld        (hl),d                        ;[042c] 72
                    ld        l,a                           ;[042d] 6f
                    ld        (hl),d                        ;[042e] 72
                    jr        nz,$04a3                      ;[042f] 20 72
                    ld        h,l                           ;[0431] 65
                    ld        h,c                           ;[0432] 61
                    ld        h,h                           ;[0433] 64
                    ld        l,c                           ;[0434] 69
                    ld        l,(hl)                        ;[0435] 6e
                    ld        h,a                           ;[0436] 67
                    jr        nz,$048d                      ;[0437] 20 54
                    ld        b,d                           ;[0439] 42
                    ld        b,d                           ;[043a] 42
                    ld        c,h                           ;[043b] 4c
                    ld        d,l                           ;[043c] 55
                    ld        b,l                           ;[043d] 45
                    ld        l,$46                         ;[043e] 2e 46
                    ld        d,a                           ;[0440] 57
                    jr        nz,$04a9                      ;[0441] 20 66
                    ld        l,c                           ;[0443] 69
                    ld        l,h                           ;[0444] 6c
                    ld        h,l                           ;[0445] 65
                    nop                                     ;[0446] 00
                    ld        d,h                           ;[0447] 54
                    ld        b,d                           ;[0448] 42
                    ld        b,d                           ;[0449] 42
                    ld        c,h                           ;[044a] 4c
                    ld        d,l                           ;[044b] 55
                    ld        b,l                           ;[044c] 45
                    jr        nz,$046f                      ;[044d] 20 20
                    ld        d,h                           ;[044f] 54
                    ld        b,d                           ;[0450] 42
                    ld        d,l                           ;[0451] 55
                    nop                                     ;[0452] 00
                    ld        d,h                           ;[0453] 54
                    ld        b,d                           ;[0454] 42
                    ld        b,d                           ;[0455] 42
                    ld        c,h                           ;[0456] 4c
                    ld        d,l                           ;[0457] 55
                    ld        b,l                           ;[0458] 45
                    jr        nz,$047b                      ;[0459] 20 20
                    ld        b,(hl)                        ;[045b] 46
                    ld        d,a                           ;[045c] 57
                    jr        nz,$045f                      ;[045d] 20 00
                    ld        a,$ff                         ;[045f] 3e ff
                    out       ($e7),a                       ;[0461] d3 e7
                    ld        b,$0a                         ;[0463] 06 0a
                    ld        a,$ff                         ;[0465] 3e ff
                    out       ($eb),a                       ;[0467] d3 eb
                    djnz      $0465                         ;[0469] 10 fa
                    ld        a,$fe                         ;[046b] 3e fe
                    out       ($e7),a                       ;[046d] d3 e7
                    ld        a,$4c                         ;[046f] 3e 4c
                    call      $0578                         ;[0471] cd 78 05
                    ld        b,$09                         ;[0474] 06 09
                    in        a,($eb)                       ;[0476] db eb
                    djnz      $0476                         ;[0478] 10 fc
                    ld        b,$10                         ;[047a] 06 10
                    ld        a,$40                         ;[047c] 3e 40
                    ld        de,$0000                      ;[047e] 11 00 00
                    push      bc                            ;[0481] c5
                    call      $0552                         ;[0482] cd 52 05
                    pop       bc                            ;[0485] c1
                    jp        nc,$0492                      ;[0486] d2 92 04
                    djnz      $047c                         ;[0489] 10 f1
                    ld        l,$00                         ;[048b] 2e 00
                    ld        a,$ff                         ;[048d] 3e ff
                    out       ($e7),a                       ;[048f] d3 e7
                    ret                                     ;[0491] c9

                    ld        a,$48                         ;[0492] 3e 48
                    ld        de,$01aa                      ;[0494] 11 aa 01
                    call      $055f                         ;[0497] cd 5f 05
                    ld        hl,$0536                      ;[049a] 21 36 05
                    jr        c,$04a2                       ;[049d] 38 03
                    ld        hl,$0544                      ;[049f] 21 44 05
                    ld        bc,$0078                      ;[04a2] 01 78 00
                    push      bc                            ;[04a5] c5
                    call      $04b9                         ;[04a6] cd b9 04
                    pop       bc                            ;[04a9] c1
                    jp        nc,$04ba                      ;[04aa] d2 ba 04
                    djnz      $04a5                         ;[04ad] 10 f6
                    dec       c                             ;[04af] 0d
                    jr        nz,$04a5                      ;[04b0] 20 f3
                    ld        l,$00                         ;[04b2] 2e 00
                    ld        a,$ff                         ;[04b4] 3e ff
                    out       ($e7),a                       ;[04b6] d3 e7
                    ret                                     ;[04b8] c9

                    jp        (hl)                          ;[04b9] e9
                    ld        a,$7a                         ;[04ba] 3e 7a
                    ld        de,$0000                      ;[04bc] 11 00 00
                    call      $055f                         ;[04bf] cd 5f 05
                    jp        c,$04b2                       ;[04c2] da b2 04
                    ld        a,b                           ;[04c5] 78
                    and       $40                           ;[04c6] e6 40
                    ld        ($fa02),a                     ;[04c8] 32 02 fa
                    call      z,$04dd                       ;[04cb] cc dd 04
                    ld        a,$ff                         ;[04ce] 3e ff
                    out       ($e7),a                       ;[04d0] d3 e7
                    ld        a,($fa02)                     ;[04d2] 3a 02 fa
                    ld        l,$03                         ;[04d5] 2e 03
                    cp        $40                           ;[04d7] fe 40
                    ret       z                             ;[04d9] c8
                    ld        l,$02                         ;[04da] 2e 02
                    ret                                     ;[04dc] c9

                    ld        a,$50                         ;[04dd] 3e 50
                    ld        bc,$0000                      ;[04df] 01 00 00
                    ld        de,$0200                      ;[04e2] 11 00 02
                    jp        $053d                         ;[04e5] c3 3d 05
                    ld        iy,$0000                      ;[04e8] fd 21 00 00
                    add       iy,sp                         ;[04ec] fd 39
                    ld        e,(iy+$02)                    ;[04ee] fd 5e 02
                    ld        d,(iy+$03)                    ;[04f1] fd 56 03
                    ld        c,(iy+$04)                    ;[04f4] fd 4e 04
                    ld        b,(iy+$05)                    ;[04f7] fd 46 05
                    ld        l,(iy+$06)                    ;[04fa] fd 6e 06
                    ld        h,(iy+$07)                    ;[04fd] fd 66 07
                    ld        a,$fe                         ;[0500] 3e fe
                    out       ($e7),a                       ;[0502] d3 e7
                    ld        a,($fa02)                     ;[0504] 3a 02 fa
                    or        a                             ;[0507] b7
                    call      z,$052a                       ;[0508] cc 2a 05
                    ld        a,$51                         ;[050b] 3e 51
                    call      $053d                         ;[050d] cd 3d 05
                    jr        nc,$0515                      ;[0510] 30 03
                    ld        l,$00                         ;[0512] 2e 00
                    ret                                     ;[0514] c9

                    call      $059f                         ;[0515] cd 9f 05
                    jr        c,$0512                       ;[0518] 38 f8
                    ld        bc,$00eb                      ;[051a] 01 eb 00
                    inir                                    ;[051d] ed b2
                    inir                                    ;[051f] ed b2
                    nop                                     ;[0521] 00
                    in        a,($eb)                       ;[0522] db eb
                    nop                                     ;[0524] 00
                    in        a,($eb)                       ;[0525] db eb
                    ld        l,$01                         ;[0527] 2e 01
                    ret                                     ;[0529] c9

                    ld        b,c                           ;[052a] 41
                    ld        c,d                           ;[052b] 4a
                    ld        d,e                           ;[052c] 53
                    ld        e,$00                         ;[052d] 1e 00
                    sla       d                             ;[052f] cb 22
                    rl        c                             ;[0531] cb 11
                    rl        b                             ;[0533] cb 10
                    ret                                     ;[0535] c9

                    ld        a,$41                         ;[0536] 3e 41
                    ld        bc,$0000                      ;[0538] 01 00 00
                    ld        d,b                           ;[053b] 50
                    ld        e,c                           ;[053c] 59
                    call      $0578                         ;[053d] cd 78 05
                    or        a                             ;[0540] b7
                    ret       z                             ;[0541] c8
                    scf                                     ;[0542] 37
                    ret                                     ;[0543] c9

                    ld        a,$77                         ;[0544] 3e 77
                    call      $0538                         ;[0546] cd 38 05
                    ld        a,$69                         ;[0549] 3e 69
                    ld        bc,$4000                      ;[054b] 01 00 40
                    ld        d,c                           ;[054e] 51
                    ld        e,c                           ;[054f] 59
                    jr        $053d                         ;[0550] 18 eb
                    ld        bc,$0000                      ;[0552] 01 00 00
                    call      $0578                         ;[0555] cd 78 05
                    ld        b,a                           ;[0558] 47
                    and       $fe                           ;[0559] e6 fe
                    ld        a,b                           ;[055b] 78
                    jr        nz,$0542                      ;[055c] 20 e4
                    ret                                     ;[055e] c9

                    call      $0552                         ;[055f] cd 52 05
                    ret       c                             ;[0562] d8
                    push      af                            ;[0563] f5
                    call      $05ad                         ;[0564] cd ad 05
                    ld        h,a                           ;[0567] 67
                    call      $05ad                         ;[0568] cd ad 05
                    ld        l,a                           ;[056b] 6f
                    call      $05ad                         ;[056c] cd ad 05
                    ld        d,a                           ;[056f] 57
                    call      $05ad                         ;[0570] cd ad 05
                    ld        e,a                           ;[0573] 5f
                    ld        b,h                           ;[0574] 44
                    ld        c,l                           ;[0575] 4d
                    pop       af                            ;[0576] f1
                    ret                                     ;[0577] c9

                    out       ($eb),a                       ;[0578] d3 eb
                    push      af                            ;[057a] f5
                    ld        a,b                           ;[057b] 78
                    nop                                     ;[057c] 00
                    out       ($eb),a                       ;[057d] d3 eb
                    ld        a,c                           ;[057f] 79
                    nop                                     ;[0580] 00
                    out       ($eb),a                       ;[0581] d3 eb
                    ld        a,d                           ;[0583] 7a
                    nop                                     ;[0584] 00
                    out       ($eb),a                       ;[0585] d3 eb
                    ld        a,e                           ;[0587] 7b
                    nop                                     ;[0588] 00
                    out       ($eb),a                       ;[0589] d3 eb
                    pop       af                            ;[058b] f1
                    cp        $40                           ;[058c] fe 40
                    ld        b,$95                         ;[058e] 06 95
                    jr        z,$059a                       ;[0590] 28 08
                    cp        $48                           ;[0592] fe 48
                    ld        b,$87                         ;[0594] 06 87
                    jr        z,$059a                       ;[0596] 28 02
                    ld        b,$ff                         ;[0598] 06 ff
                    ld        a,b                           ;[059a] 78
                    out       ($eb),a                       ;[059b] d3 eb
                    jr        $05ad                         ;[059d] 18 0e
                    ld        b,$0a                         ;[059f] 06 0a
                    push      bc                            ;[05a1] c5
                    call      $05ad                         ;[05a2] cd ad 05
                    pop       bc                            ;[05a5] c1
                    cp        $fe                           ;[05a6] fe fe
                    ret       z                             ;[05a8] c8
                    djnz      $05a1                         ;[05a9] 10 f6
                    scf                                     ;[05ab] 37
                    ret                                     ;[05ac] c9

                    ld        bc,$0064                      ;[05ad] 01 64 00
                    in        a,($eb)                       ;[05b0] db eb
                    cp        $ff                           ;[05b2] fe ff
                    ret       nz                            ;[05b4] c0
                    djnz      $05b0                         ;[05b5] 10 f9
                    dec       c                             ;[05b7] 0d
                    jr        nz,$05b0                      ;[05b8] 20 f6
                    ret                                     ;[05ba] c9

                    push      af                            ;[05bb] f5
                    push      af                            ;[05bc] f5
                    push      af                            ;[05bd] f5
                    push      af                            ;[05be] f5
                    ld        iy,$fa03                      ;[05bf] fd 21 03 fa
                    ld        a,(iy+$01)                    ;[05c3] fd 7e 01
                    or        (iy+$00)                      ;[05c6] fd b6 00
                    jr        z,$0627                       ;[05c9] 28 5c
                    push      af                            ;[05cb] f5
                    ld        hl,$000c                      ;[05cc] 21 0c 00
                    add       hl,sp                         ;[05cf] 39
                    ld        a,(hl)                        ;[05d0] 7e
                    ld        iy,$0002                      ;[05d1] fd 21 02 00
                    add       iy,sp                         ;[05d5] fd 39
                    ld        (iy+$00),a                    ;[05d7] fd 77 00
                    ld        hl,$000d                      ;[05da] 21 0d 00
                    add       hl,sp                         ;[05dd] 39
                    ld        a,(hl)                        ;[05de] 7e
                    ld        iy,$0002                      ;[05df] fd 21 02 00
                    add       iy,sp                         ;[05e3] fd 39
                    ld        (iy+$01),a                    ;[05e5] fd 77 01
                    ld        hl,$000e                      ;[05e8] 21 0e 00
                    add       hl,sp                         ;[05eb] 39
                    ld        a,(hl)                        ;[05ec] 7e
                    ld        iy,$0002                      ;[05ed] fd 21 02 00
                    add       iy,sp                         ;[05f1] fd 39
                    ld        (iy+$02),a                    ;[05f3] fd 77 02
                    ld        hl,$000f                      ;[05f6] 21 0f 00
                    add       hl,sp                         ;[05f9] 39
                    ld        a,(hl)                        ;[05fa] 7e
                    ld        iy,$0002                      ;[05fb] fd 21 02 00
                    add       iy,sp                         ;[05ff] fd 39
                    ld        (iy+$03),a                    ;[0601] fd 77 03
                    pop       af                            ;[0604] f1
                    ld        b,$07                         ;[0605] 06 07
                    srl       (iy+$03)                      ;[0607] fd cb 03 3e
                    rr        (iy+$02)                      ;[060b] fd cb 02 1e
                    rr        (iy+$01)                      ;[060f] fd cb 01 1e
                    rr        (iy+$00)                      ;[0613] fd cb 00 1e
                    djnz      $0607                         ;[0617] 10 ee
                    ld        hl,$000a                      ;[0619] 21 0a 00
                    add       hl,sp                         ;[061c] 39
                    ld        e,(hl)                        ;[061d] 5e
                    res       7,e                           ;[061e] cb bb
                    ld        d,$00                         ;[0620] 16 00
                    ld        bc,$0000                      ;[0622] 01 00 00
                    jr        $067f                         ;[0625] 18 58
                    push      af                            ;[0627] f5
                    ld        hl,$000c                      ;[0628] 21 0c 00
                    add       hl,sp                         ;[062b] 39
                    ld        a,(hl)                        ;[062c] 7e
                    ld        iy,$0002                      ;[062d] fd 21 02 00
                    add       iy,sp                         ;[0631] fd 39
                    ld        (iy+$00),a                    ;[0633] fd 77 00
                    ld        hl,$000d                      ;[0636] 21 0d 00
                    add       hl,sp                         ;[0639] 39
                    ld        a,(hl)                        ;[063a] 7e
                    ld        iy,$0002                      ;[063b] fd 21 02 00
                    add       iy,sp                         ;[063f] fd 39
                    ld        (iy+$01),a                    ;[0641] fd 77 01
                    ld        hl,$000e                      ;[0644] 21 0e 00
                    add       hl,sp                         ;[0647] 39
                    ld        a,(hl)                        ;[0648] 7e
                    ld        iy,$0002                      ;[0649] fd 21 02 00
                    add       iy,sp                         ;[064d] fd 39
                    ld        (iy+$02),a                    ;[064f] fd 77 02
                    ld        hl,$000f                      ;[0652] 21 0f 00
                    add       hl,sp                         ;[0655] 39
                    ld        a,(hl)                        ;[0656] 7e
                    ld        iy,$0002                      ;[0657] fd 21 02 00
                    add       iy,sp                         ;[065b] fd 39
                    ld        (iy+$03),a                    ;[065d] fd 77 03
                    pop       af                            ;[0660] f1
                    ld        b,$08                         ;[0661] 06 08
                    srl       (iy+$03)                      ;[0663] fd cb 03 3e
                    rr        (iy+$02)                      ;[0667] fd cb 02 1e
                    rr        (iy+$01)                      ;[066b] fd cb 01 1e
                    rr        (iy+$00)                      ;[066f] fd cb 00 1e
                    djnz      $0663                         ;[0673] 10 ee
                    ld        hl,$000a                      ;[0675] 21 0a 00
                    add       hl,sp                         ;[0678] 39
                    ld        e,(hl)                        ;[0679] 5e
                    ld        d,$00                         ;[067a] 16 00
                    ld        bc,$0000                      ;[067c] 01 00 00
                    ld        hl,$0000                      ;[067f] 21 00 00
                    add       hl,sp                         ;[0682] 39
                    ld        a,(hl)                        ;[0683] 7e
                    ld        iy,$fc2d                      ;[0684] fd 21 2d fc
                    sub       (iy+$00)                      ;[0688] fd 96 00
                    jr        nz,$06b7                      ;[068b] 20 2a
                    ld        hl,$0001                      ;[068d] 21 01 00
                    add       hl,sp                         ;[0690] 39
                    ld        a,(hl)                        ;[0691] 7e
                    ld        iy,$fc2d                      ;[0692] fd 21 2d fc
                    sub       (iy+$01)                      ;[0696] fd 96 01
                    jr        nz,$06b7                      ;[0699] 20 1c
                    ld        hl,$0002                      ;[069b] 21 02 00
                    add       hl,sp                         ;[069e] 39
                    ld        a,(hl)                        ;[069f] 7e
                    ld        iy,$fc2d                      ;[06a0] fd 21 2d fc
                    sub       (iy+$02)                      ;[06a4] fd 96 02
                    jr        nz,$06b7                      ;[06a7] 20 0e
                    ld        hl,$0003                      ;[06a9] 21 03 00
                    add       hl,sp                         ;[06ac] 39
                    ld        a,(hl)                        ;[06ad] 7e
                    ld        iy,$fc2d                      ;[06ae] fd 21 2d fc
                    sub       (iy+$03)                      ;[06b2] fd 96 03
                    jr        z,$0721                       ;[06b5] 28 6a
                    ld        hl,$0000                      ;[06b7] 21 00 00
                    add       hl,sp                         ;[06ba] 39
                    push      de                            ;[06bb] d5
                    ld        iy,$0006                      ;[06bc] fd 21 06 00
                    add       iy,sp                         ;[06c0] fd 39
                    push      iy                            ;[06c2] fd e5
                    pop       de                            ;[06c4] d1
                    ld        iy,$fa05                      ;[06c5] fd 21 05 fa
                    ld        a,(iy+$00)                    ;[06c9] fd 7e 00
                    add       (hl)                          ;[06cc] 86
                    ld        (de),a                        ;[06cd] 12
                    ld        a,(iy+$01)                    ;[06ce] fd 7e 01
                    inc       hl                            ;[06d1] 23
                    adc       (hl)                          ;[06d2] 8e
                    inc       de                            ;[06d3] 13
                    ld        (de),a                        ;[06d4] 12
                    ld        a,(iy+$02)                    ;[06d5] fd 7e 02
                    inc       hl                            ;[06d8] 23
                    adc       (hl)                          ;[06d9] 8e
                    inc       de                            ;[06da] 13
                    ld        (de),a                        ;[06db] 12
                    ld        a,(iy+$03)                    ;[06dc] fd 7e 03
                    inc       hl                            ;[06df] 23
                    adc       (hl)                          ;[06e0] 8e
                    inc       de                            ;[06e1] 13
                    ld        (de),a                        ;[06e2] 12
                    pop       de                            ;[06e3] d1
                    push      bc                            ;[06e4] c5
                    push      de                            ;[06e5] d5
                    ld        hl,$fa2b                      ;[06e6] 21 2b fa
                    push      hl                            ;[06e9] e5
                    ld        iy,$000a                      ;[06ea] fd 21 0a 00
                    add       iy,sp                         ;[06ee] fd 39
                    ld        l,(iy+$02)                    ;[06f0] fd 6e 02
                    ld        h,(iy+$03)                    ;[06f3] fd 66 03
                    push      hl                            ;[06f6] e5
                    ld        l,(iy+$00)                    ;[06f7] fd 6e 00
                    ld        h,(iy+$01)                    ;[06fa] fd 66 01
                    push      hl                            ;[06fd] e5
                    call      $04e8                         ;[06fe] cd e8 04
                    pop       af                            ;[0701] f1
                    pop       af                            ;[0702] f1
                    pop       af                            ;[0703] f1
                    pop       de                            ;[0704] d1
                    pop       bc                            ;[0705] c1
                    ld        a,l                           ;[0706] 7d
                    or        a                             ;[0707] b7
                    jr        nz,$0711                      ;[0708] 20 07
                    ld        hl,$0000                      ;[070a] 21 00 00
                    ld        e,l                           ;[070d] 5d
                    ld        d,h                           ;[070e] 54
                    jr        $075e                         ;[070f] 18 4d
                    push      de                            ;[0711] d5
                    push      bc                            ;[0712] c5
                    ld        de,$fc2d                      ;[0713] 11 2d fc
                    ld        hl,$0004                      ;[0716] 21 04 00
                    add       hl,sp                         ;[0719] 39
                    ld        bc,$0004                      ;[071a] 01 04 00
                    ldir                                    ;[071d] ed b0
                    pop       bc                            ;[071f] c1
                    pop       de                            ;[0720] d1
                    ld        iy,$fa03                      ;[0721] fd 21 03 fa
                    ld        a,(iy+$01)                    ;[0725] fd 7e 01
                    or        (iy+$00)                      ;[0728] fd b6 00
                    jr        z,$074a                       ;[072b] 28 1d
                    ld        hl,$fa2b                      ;[072d] 21 2b fa
                    ld        a,$02                         ;[0730] 3e 02
                    sla       e                             ;[0732] cb 23
                    rl        d                             ;[0734] cb 12
                    rl        c                             ;[0736] cb 11
                    rl        b                             ;[0738] cb 10
                    dec       a                             ;[073a] 3d
                    jr        nz,$0732                      ;[073b] 20 f5
                    add       hl,de                         ;[073d] 19
                    ld        c,(hl)                        ;[073e] 4e
                    inc       hl                            ;[073f] 23
                    ld        b,(hl)                        ;[0740] 46
                    inc       hl                            ;[0741] 23
                    ld        e,(hl)                        ;[0742] 5e
                    inc       hl                            ;[0743] 23
                    ld        a,(hl)                        ;[0744] 7e
                    and       $0f                           ;[0745] e6 0f
                    ld        d,a                           ;[0747] 57
                    jr        $075c                         ;[0748] 18 12
                    ld        hl,$fa2b                      ;[074a] 21 2b fa
                    sla       e                             ;[074d] cb 23
                    rl        d                             ;[074f] cb 12
                    rl        c                             ;[0751] cb 11
                    rl        b                             ;[0753] cb 10
                    add       hl,de                         ;[0755] 19
                    ld        c,(hl)                        ;[0756] 4e
                    inc       hl                            ;[0757] 23
                    ld        b,(hl)                        ;[0758] 46
                    ld        de,$0000                      ;[0759] 11 00 00
                    ld        l,c                           ;[075c] 69
                    ld        h,b                           ;[075d] 60
                    pop       af                            ;[075e] f1
                    pop       af                            ;[075f] f1
                    pop       af                            ;[0760] f1
                    pop       af                            ;[0761] f1
                    ret                                     ;[0762] c9

                    push      af                            ;[0763] f5
                    push      af                            ;[0764] f5
                    ld        hl,$0006                      ;[0765] 21 06 00
                    add       hl,sp                         ;[0768] 39
                    ld        c,(hl)                        ;[0769] 4e
                    inc       hl                            ;[076a] 23
                    ld        b,(hl)                        ;[076b] 46
                    ld        hl,$0008                      ;[076c] 21 08 00
                    add       hl,sp                         ;[076f] 39
                    ld        a,(hl)                        ;[0770] 7e
                    ld        iy,$0002                      ;[0771] fd 21 02 00
                    add       iy,sp                         ;[0775] fd 39
                    ld        (iy+$00),a                    ;[0777] fd 77 00
                    ld        hl,$0009                      ;[077a] 21 09 00
                    add       hl,sp                         ;[077d] 39
                    ld        a,(hl)                        ;[077e] 7e
                    ld        iy,$0002                      ;[077f] fd 21 02 00
                    add       iy,sp                         ;[0783] fd 39
                    ld        (iy+$01),a                    ;[0785] fd 77 01
                    ld        de,$0000                      ;[0788] 11 00 00
                    ld        hl,$000a                      ;[078b] 21 0a 00
                    add       hl,sp                         ;[078e] 39
                    ld        a,e                           ;[078f] 7b
                    sub       (hl)                          ;[0790] 96
                    ld        a,d                           ;[0791] 7a
                    inc       hl                            ;[0792] 23
                    sbc       (hl)                          ;[0793] 9e
                    jp        po,$0799                      ;[0794] e2 99 07
                    xor       $80                           ;[0797] ee 80
                    jp        p,$07d7                       ;[0799] f2 d7 07
                    ld        a,(bc)                        ;[079c] 0a
                    ld        iy,$0001                      ;[079d] fd 21 01 00
                    add       iy,sp                         ;[07a1] fd 39
                    ld        (iy+$00),a                    ;[07a3] fd 77 00
                    inc       bc                            ;[07a6] 03
                    ld        iy,$0002                      ;[07a7] fd 21 02 00
                    add       iy,sp                         ;[07ab] fd 39
                    ld        l,(iy+$00)                    ;[07ad] fd 6e 00
                    ld        h,(iy+$01)                    ;[07b0] fd 66 01
                    ld        a,(hl)                        ;[07b3] 7e
                    inc       sp                            ;[07b4] 33
                    push      af                            ;[07b5] f5
                    inc       sp                            ;[07b6] 33
                    inc       (iy+$00)                      ;[07b7] fd 34 00
                    jr        nz,$07bf                      ;[07ba] 20 03
                    inc       (iy+$01)                      ;[07bc] fd 34 01
                    ld        hl,$0001                      ;[07bf] 21 01 00
                    add       hl,sp                         ;[07c2] 39
                    ld        a,(hl)                        ;[07c3] 7e
                    ld        iy,$0000                      ;[07c4] fd 21 00 00
                    add       iy,sp                         ;[07c8] fd 39
                    sub       (iy+$00)                      ;[07ca] fd 96 00
                    jr        z,$07d4                       ;[07cd] 28 05
                    ld        hl,$0001                      ;[07cf] 21 01 00
                    jr        $07da                         ;[07d2] 18 06
                    inc       de                            ;[07d4] 13
                    jr        $078b                         ;[07d5] 18 b4
                    ld        hl,$0000                      ;[07d7] 21 00 00
                    pop       af                            ;[07da] f1
                    pop       af                            ;[07db] f1
                    ret                                     ;[07dc] c9

                    ld        hl,$fff5                      ;[07dd] 21 f5 ff
                    add       hl,sp                         ;[07e0] 39
                    ld        sp,hl                         ;[07e1] f9
                    ld        iy,$fc2d                      ;[07e2] fd 21 2d fc
                    ld        (iy+$00),$ff                  ;[07e6] fd 36 00 ff
                    ld        (iy+$01),$ff                  ;[07ea] fd 36 01 ff
                    ld        (iy+$02),$ff                  ;[07ee] fd 36 02 ff
                    ld        (iy+$03),$ff                  ;[07f2] fd 36 03 ff
                    ld        hl,$0000                      ;[07f6] 21 00 00
                    ld        ($fa03),hl                    ;[07f9] 22 03 fa
                    ld        hl,$fa2b                      ;[07fc] 21 2b fa
                    push      hl                            ;[07ff] e5
                    ld        hl,$0000                      ;[0800] 21 00 00
                    push      hl                            ;[0803] e5
                    ld        hl,$0000                      ;[0804] 21 00 00
                    push      hl                            ;[0807] e5
                    call      $04e8                         ;[0808] cd e8 04
                    pop       af                            ;[080b] f1
                    pop       af                            ;[080c] f1
                    pop       af                            ;[080d] f1
                    ld        iy,$000a                      ;[080e] fd 21 0a 00
                    add       iy,sp                         ;[0812] fd 39
                    ld        (iy+$00),l                    ;[0814] fd 75 00
                    ld        hl,$000a                      ;[0817] 21 0a 00
                    add       hl,sp                         ;[081a] 39
                    ld        a,(hl)                        ;[081b] 7e
                    or        a                             ;[081c] b7
                    jr        nz,$0823                      ;[081d] 20 04
                    ld        l,a                           ;[081f] 6f
                    jp        $1022                         ;[0820] c3 22 10
                    xor       a                             ;[0823] af
                    ld        iy,$0006                      ;[0824] fd 21 06 00
                    add       iy,sp                         ;[0828] fd 39
                    ld        (iy+$00),a                    ;[082a] fd 77 00
                    ld        (iy+$01),a                    ;[082d] fd 77 01
                    ld        (iy+$02),a                    ;[0830] fd 77 02
                    ld        (iy+$03),a                    ;[0833] fd 77 03
                    ld        hl,$0001                      ;[0836] 21 01 00
                    ld        ($fc2b),hl                    ;[0839] 22 2b fc
                    ld        l,$08                         ;[083c] 2e 08
                    push      hl                            ;[083e] e5
                    ld        hl,$102b                      ;[083f] 21 2b 10
                    push      hl                            ;[0842] e5
                    ld        hl,$fa61                      ;[0843] 21 61 fa
                    push      hl                            ;[0846] e5
                    call      $0763                         ;[0847] cd 63 07
                    pop       af                            ;[084a] f1
                    pop       af                            ;[084b] f1
                    pop       af                            ;[084c] f1
                    ld        a,h                           ;[084d] 7c
                    or        l                             ;[084e] b5
                    jr        nz,$0857                      ;[084f] 20 06
                    ld        hl,$0000                      ;[0851] 21 00 00
                    ld        ($fc2b),hl                    ;[0854] 22 2b fc
                    ld        hl,$0008                      ;[0857] 21 08 00
                    push      hl                            ;[085a] e5
                    ld        hl,$1034                      ;[085b] 21 34 10
                    push      hl                            ;[085e] e5
                    ld        hl,$fa7d                      ;[085f] 21 7d fa
                    push      hl                            ;[0862] e5
                    call      $0763                         ;[0863] cd 63 07
                    pop       af                            ;[0866] f1
                    pop       af                            ;[0867] f1
                    pop       af                            ;[0868] f1
                    ld        a,h                           ;[0869] 7c
                    or        l                             ;[086a] b5
                    jr        nz,$0873                      ;[086b] 20 06
                    ld        hl,$0000                      ;[086d] 21 00 00
                    ld        ($fc2b),hl                    ;[0870] 22 2b fc
                    ld        iy,$fc2b                      ;[0873] fd 21 2b fc
                    ld        a,(iy+$01)                    ;[0877] fd 7e 01
                    or        (iy+$00)                      ;[087a] fd b6 00
                    jp        z,$08fd                       ;[087d] ca fd 08
                    ld        bc,$fa2b                      ;[0880] 01 2b fa
                    inc       sp                            ;[0883] 33
                    inc       sp                            ;[0884] 33
                    push      bc                            ;[0885] c5
                    pop       hl                            ;[0886] e1
                    push      hl                            ;[0887] e5
                    ld        de,$01c6                      ;[0888] 11 c6 01
                    add       hl,de                         ;[088b] 19
                    ld        c,(hl)                        ;[088c] 4e
                    inc       hl                            ;[088d] 23
                    ld        b,(hl)                        ;[088e] 46
                    inc       hl                            ;[088f] 23
                    ld        e,(hl)                        ;[0890] 5e
                    inc       hl                            ;[0891] 23
                    ld        d,(hl)                        ;[0892] 56
                    ld        iy,$0006                      ;[0893] fd 21 06 00
                    add       iy,sp                         ;[0897] fd 39
                    ld        (iy+$00),c                    ;[0899] fd 71 00
                    ld        (iy+$01),b                    ;[089c] fd 70 01
                    ld        (iy+$02),e                    ;[089f] fd 73 02
                    ld        (iy+$03),d                    ;[08a2] fd 72 03
                    pop       hl                            ;[08a5] e1
                    push      hl                            ;[08a6] e5
                    push      bc                            ;[08a7] c5
                    ld        bc,$01fe                      ;[08a8] 01 fe 01
                    add       hl,bc                         ;[08ab] 09
                    pop       bc                            ;[08ac] c1
                    ld        a,(hl)                        ;[08ad] 7e
                    inc       hl                            ;[08ae] 23
                    ld        h,(hl)                        ;[08af] 66
                    ld        l,a                           ;[08b0] 6f
                    sub       $aa                           ;[08b1] d6 aa
                    jr        nz,$08c8                      ;[08b3] 20 13
                    ld        a,h                           ;[08b5] 7c
                    sub       $55                           ;[08b6] d6 55
                    jr        nz,$08c8                      ;[08b8] 20 0e
                    ld        (iy+$00),c                    ;[08ba] fd 71 00
                    ld        (iy+$01),b                    ;[08bd] fd 70 01
                    ld        (iy+$02),e                    ;[08c0] fd 73 02
                    ld        (iy+$03),d                    ;[08c3] fd 72 03
                    jr        $08d7                         ;[08c6] 18 0f
                    ld        a,l                           ;[08c8] 7d
                    sub       $55                           ;[08c9] d6 55
                    jr        nz,$08d2                      ;[08cb] 20 05
                    ld        a,h                           ;[08cd] 7c
                    sub       $aa                           ;[08ce] d6 aa
                    jr        z,$08d7                       ;[08d0] 28 05
                    ld        l,$00                         ;[08d2] 2e 00
                    jp        $1022                         ;[08d4] c3 22 10
                    ld        hl,$fa2b                      ;[08d7] 21 2b fa
                    push      hl                            ;[08da] e5
                    ld        iy,$0008                      ;[08db] fd 21 08 00
                    add       iy,sp                         ;[08df] fd 39
                    ld        l,(iy+$02)                    ;[08e1] fd 6e 02
                    ld        h,(iy+$03)                    ;[08e4] fd 66 03
                    push      hl                            ;[08e7] e5
                    ld        l,(iy+$00)                    ;[08e8] fd 6e 00
                    ld        h,(iy+$01)                    ;[08eb] fd 66 01
                    push      hl                            ;[08ee] e5
                    call      $04e8                         ;[08ef] cd e8 04
                    pop       af                            ;[08f2] f1
                    pop       af                            ;[08f3] f1
                    pop       af                            ;[08f4] f1
                    ld        a,l                           ;[08f5] 7d
                    or        a                             ;[08f6] b7
                    jr        nz,$08fd                      ;[08f7] 20 04
                    ld        l,a                           ;[08f9] 6f
                    jp        $1022                         ;[08fa] c3 22 10
                    ld        hl,$0008                      ;[08fd] 21 08 00
                    push      hl                            ;[0900] e5
                    ld        hl,$1034                      ;[0901] 21 34 10
                    push      hl                            ;[0904] e5
                    ld        hl,$fa7d                      ;[0905] 21 7d fa
                    push      hl                            ;[0908] e5
                    call      $0763                         ;[0909] cd 63 07
                    pop       af                            ;[090c] f1
                    pop       af                            ;[090d] f1
                    pop       af                            ;[090e] f1
                    ld        a,h                           ;[090f] 7c
                    or        l                             ;[0910] b5
                    jr        nz,$091b                      ;[0911] 20 08
                    ld        hl,$0001                      ;[0913] 21 01 00
                    ld        ($fa03),hl                    ;[0916] 22 03 fa
                    jr        $0936                         ;[0919] 18 1b
                    ld        hl,$0008                      ;[091b] 21 08 00
                    push      hl                            ;[091e] e5
                    ld        hl,$102b                      ;[091f] 21 2b 10
                    push      hl                            ;[0922] e5
                    ld        hl,$fa61                      ;[0923] 21 61 fa
                    push      hl                            ;[0926] e5
                    call      $0763                         ;[0927] cd 63 07
                    pop       af                            ;[092a] f1
                    pop       af                            ;[092b] f1
                    pop       af                            ;[092c] f1
                    ld        a,h                           ;[092d] 7c
                    or        l                             ;[092e] b5
                    jr        z,$0936                       ;[092f] 28 05
                    ld        l,$00                         ;[0931] 2e 00
                    jp        $1022                         ;[0933] c3 22 10
                    ld        a,($fc29)                     ;[0936] 3a 29 fc
                    sub       $55                           ;[0939] d6 55
                    jr        nz,$0944                      ;[093b] 20 07
                    ld        a,($fc2a)                     ;[093d] 3a 2a fc
                    sub       $aa                           ;[0940] d6 aa
                    jr        z,$0949                       ;[0942] 28 05
                    ld        l,$00                         ;[0944] 2e 00
                    jp        $1022                         ;[0946] c3 22 10
                    ld        hl,$fa2b                      ;[0949] 21 2b fa
                    ld        c,(hl)                        ;[094c] 4e
                    ld        a,c                           ;[094d] 79
                    cp        $e9                           ;[094e] fe e9
                    jr        z,$095b                       ;[0950] 28 09
                    sub       $eb                           ;[0952] d6 eb
                    jr        z,$095b                       ;[0954] 28 05
                    ld        l,$00                         ;[0956] 2e 00
                    jp        $1022                         ;[0958] c3 22 10
                    ld        a,($fa36)                     ;[095b] 3a 36 fa
                    or        a                             ;[095e] b7
                    jr        nz,$0968                      ;[095f] 20 07
                    ld        a,($fa37)                     ;[0961] 3a 37 fa
                    sub       $02                           ;[0964] d6 02
                    jr        z,$096d                       ;[0966] 28 05
                    ld        l,$00                         ;[0968] 2e 00
                    jp        $1022                         ;[096a] c3 22 10
                    ld        a,($fa38)                     ;[096d] 3a 38 fa
                    ld        iy,$fa1b                      ;[0970] fd 21 1b fa
                    ld        (iy+$00),a                    ;[0974] fd 77 00
                    ld        (iy+$01),$00                  ;[0977] fd 36 01 00
                    ld        (iy+$02),$00                  ;[097b] fd 36 02 00
                    ld        (iy+$03),$00                  ;[097f] fd 36 03 00
                    ld        hl,$fa1f                      ;[0983] 21 1f fa
                    ld        a,(iy+$00)                    ;[0986] fd 7e 00
                    add       $ff                           ;[0989] c6 ff
                    ld        (hl),a                        ;[098b] 77
                    ld        a,(iy+$01)                    ;[098c] fd 7e 01
                    adc       $ff                           ;[098f] ce ff
                    inc       hl                            ;[0991] 23
                    ld        (hl),a                        ;[0992] 77
                    ld        a,(iy+$02)                    ;[0993] fd 7e 02
                    adc       $ff                           ;[0996] ce ff
                    inc       hl                            ;[0998] 23
                    ld        (hl),a                        ;[0999] 77
                    ld        a,(iy+$03)                    ;[099a] fd 7e 03
                    adc       $ff                           ;[099d] ce ff
                    inc       hl                            ;[099f] 23
                    ld        (hl),a                        ;[09a0] 77
                    ld        a,($fa39)                     ;[09a1] 3a 39 fa
                    ld        iy,$000a                      ;[09a4] fd 21 0a 00
                    add       iy,sp                         ;[09a8] fd 39
                    ld        (iy+$00),a                    ;[09aa] fd 77 00
                    ld        a,(iy+$00)                    ;[09ad] fd 7e 00
                    ld        iy,$0002                      ;[09b0] fd 21 02 00
                    add       iy,sp                         ;[09b4] fd 39
                    ld        (iy+$00),a                    ;[09b6] fd 77 00
                    ld        (iy+$01),$00                  ;[09b9] fd 36 01 00
                    ld        (iy+$02),$00                  ;[09bd] fd 36 02 00
                    ld        (iy+$03),$00                  ;[09c1] fd 36 03 00
                    ld        hl,$0002                      ;[09c5] 21 02 00
                    add       hl,sp                         ;[09c8] 39
                    ld        iy,$0006                      ;[09c9] fd 21 06 00
                    add       iy,sp                         ;[09cd] fd 39
                    ld        a,(iy+$00)                    ;[09cf] fd 7e 00
                    add       (hl)                          ;[09d2] 86
                    ld        (hl),a                        ;[09d3] 77
                    ld        a,(iy+$01)                    ;[09d4] fd 7e 01
                    inc       hl                            ;[09d7] 23
                    adc       (hl)                          ;[09d8] 8e
                    ld        (hl),a                        ;[09d9] 77
                    ld        a,(iy+$02)                    ;[09da] fd 7e 02
                    inc       hl                            ;[09dd] 23
                    adc       (hl)                          ;[09de] 8e
                    ld        (hl),a                        ;[09df] 77
                    ld        a,(iy+$03)                    ;[09e0] fd 7e 03
                    inc       hl                            ;[09e3] 23
                    adc       (hl)                          ;[09e4] 8e
                    ld        (hl),a                        ;[09e5] 77
                    ld        a,($fa3a)                     ;[09e6] 3a 3a fa
                    ld        (iy+$00),a                    ;[09e9] fd 77 00
                    ld        a,(iy+$00)                    ;[09ec] fd 7e 00
                    ld        (iy+$00),a                    ;[09ef] fd 77 00
                    ld        (iy+$01),$00                  ;[09f2] fd 36 01 00
                    ld        a,(iy+$00)                    ;[09f6] fd 7e 00
                    ld        (iy+$01),a                    ;[09f9] fd 77 01
                    ld        (iy+$00),$00                  ;[09fc] fd 36 00 00
                    ld        a,(iy+$00)                    ;[0a00] fd 7e 00
                    ld        (iy+$00),a                    ;[0a03] fd 77 00
                    ld        a,(iy+$01)                    ;[0a06] fd 7e 01
                    ld        (iy+$01),a                    ;[0a09] fd 77 01
                    ld        a,(iy+$01)                    ;[0a0c] fd 7e 01
                    rla                                     ;[0a0f] 17
                    sbc       a                             ;[0a10] 9f
                    ld        (iy+$02),a                    ;[0a11] fd 77 02
                    ld        (iy+$03),a                    ;[0a14] fd 77 03
                    ld        hl,$0006                      ;[0a17] 21 06 00
                    add       hl,sp                         ;[0a1a] 39
                    push      de                            ;[0a1b] d5
                    ld        de,$fa05                      ;[0a1c] 11 05 fa
                    ld        iy,$0004                      ;[0a1f] fd 21 04 00
                    add       iy,sp                         ;[0a23] fd 39
                    ld        a,(iy+$00)                    ;[0a25] fd 7e 00
                    add       (hl)                          ;[0a28] 86
                    ld        (de),a                        ;[0a29] 12
                    ld        a,(iy+$01)                    ;[0a2a] fd 7e 01
                    inc       hl                            ;[0a2d] 23
                    adc       (hl)                          ;[0a2e] 8e
                    inc       de                            ;[0a2f] 13
                    ld        (de),a                        ;[0a30] 12
                    ld        a,(iy+$02)                    ;[0a31] fd 7e 02
                    inc       hl                            ;[0a34] 23
                    adc       (hl)                          ;[0a35] 8e
                    inc       de                            ;[0a36] 13
                    ld        (de),a                        ;[0a37] 12
                    ld        a,(iy+$03)                    ;[0a38] fd 7e 03
                    inc       hl                            ;[0a3b] 23
                    adc       (hl)                          ;[0a3c] 8e
                    inc       de                            ;[0a3d] 13
                    ld        (de),a                        ;[0a3e] 12
                    pop       de                            ;[0a3f] d1
                    ld        a,($fa3b)                     ;[0a40] 3a 3b fa
                    ld        iy,$fa19                      ;[0a43] fd 21 19 fa
                    ld        (iy+$00),a                    ;[0a47] fd 77 00
                    ld        (iy+$01),$00                  ;[0a4a] fd 36 01 00
                    ld        iy,$fa03                      ;[0a4e] fd 21 03 fa
                    ld        a,(iy+$01)                    ;[0a52] fd 7e 01
                    or        (iy+$00)                      ;[0a55] fd b6 00
                    jp        z,$0e0a                       ;[0a58] ca 0a 0e
                    ld        hl,$0008                      ;[0a5b] 21 08 00
                    push      hl                            ;[0a5e] e5
                    ld        hl,$1034                      ;[0a5f] 21 34 10
                    push      hl                            ;[0a62] e5
                    ld        hl,$fa7d                      ;[0a63] 21 7d fa
                    push      hl                            ;[0a66] e5
                    call      $0763                         ;[0a67] cd 63 07
                    pop       af                            ;[0a6a] f1
                    pop       af                            ;[0a6b] f1
                    pop       af                            ;[0a6c] f1
                    ld        a,h                           ;[0a6d] 7c
                    or        l                             ;[0a6e] b5
                    jr        z,$0a76                       ;[0a6f] 28 05
                    ld        l,$00                         ;[0a71] 2e 00
                    jp        $1022                         ;[0a73] c3 22 10
                    push      af                            ;[0a76] f5
                    ld        a,($fa1b)                     ;[0a77] 3a 1b fa
                    ld        iy,$fa23                      ;[0a7a] fd 21 23 fa
                    ld        (iy+$00),a                    ;[0a7e] fd 77 00
                    ld        a,($fa1c)                     ;[0a81] 3a 1c fa
                    ld        iy,$fa23                      ;[0a84] fd 21 23 fa
                    ld        (iy+$01),a                    ;[0a88] fd 77 01
                    ld        a,($fa1d)                     ;[0a8b] 3a 1d fa
                    ld        iy,$fa23                      ;[0a8e] fd 21 23 fa
                    ld        (iy+$02),a                    ;[0a92] fd 77 02
                    ld        a,($fa1e)                     ;[0a95] 3a 1e fa
                    ld        iy,$fa23                      ;[0a98] fd 21 23 fa
                    ld        (iy+$03),a                    ;[0a9c] fd 77 03
                    pop       af                            ;[0a9f] f1
                    ld        b,$04                         ;[0aa0] 06 04
                    sla       (iy+$00)                      ;[0aa2] fd cb 00 26
                    rl        (iy+$01)                      ;[0aa6] fd cb 01 16
                    rl        (iy+$02)                      ;[0aaa] fd cb 02 16
                    rl        (iy+$03)                      ;[0aae] fd cb 03 16
                    djnz      $0aa2                         ;[0ab2] 10 ee
                    ld        de,$fa15                      ;[0ab4] 11 15 fa
                    ld        hl,$fa1b                      ;[0ab7] 21 1b fa
                    ld        bc,$0004                      ;[0aba] 01 04 00
                    ldir                                    ;[0abd] ed b0
                    ld        a,($fa4f)                     ;[0abf] 3a 4f fa
                    ld        iy,$0002                      ;[0ac2] fd 21 02 00
                    add       iy,sp                         ;[0ac6] fd 39
                    ld        (iy+$00),a                    ;[0ac8] fd 77 00
                    ld        a,($fa50)                     ;[0acb] 3a 50 fa
                    ld        iy,$0006                      ;[0ace] fd 21 06 00
                    add       iy,sp                         ;[0ad2] fd 39
                    ld        (iy+$00),a                    ;[0ad4] fd 77 00
                    ld        a,(iy+$00)                    ;[0ad7] fd 7e 00
                    ld        (iy+$00),a                    ;[0ada] fd 77 00
                    ld        (iy+$01),$00                  ;[0add] fd 36 01 00
                    ld        (iy+$02),$00                  ;[0ae1] fd 36 02 00
                    ld        (iy+$03),$00                  ;[0ae5] fd 36 03 00
                    push      af                            ;[0ae9] f5
                    pop       af                            ;[0aea] f1
                    ld        b,$08                         ;[0aeb] 06 08
                    sla       (iy+$00)                      ;[0aed] fd cb 00 26
                    rl        (iy+$01)                      ;[0af1] fd cb 01 16
                    rl        (iy+$02)                      ;[0af5] fd cb 02 16
                    rl        (iy+$03)                      ;[0af9] fd cb 03 16
                    djnz      $0aed                         ;[0afd] 10 ee
                    ld        iy,$0002                      ;[0aff] fd 21 02 00
                    add       iy,sp                         ;[0b03] fd 39
                    ld        a,(iy+$00)                    ;[0b05] fd 7e 00
                    ld        (iy+$00),a                    ;[0b08] fd 77 00
                    ld        (iy+$01),$00                  ;[0b0b] fd 36 01 00
                    ld        (iy+$02),$00                  ;[0b0f] fd 36 02 00
                    ld        (iy+$03),$00                  ;[0b13] fd 36 03 00
                    ld        hl,$0006                      ;[0b17] 21 06 00
                    add       hl,sp                         ;[0b1a] 39
                    push      de                            ;[0b1b] d5
                    push      iy                            ;[0b1c] fd e5
                    pop       de                            ;[0b1e] d1
                    ld        a,(de)                        ;[0b1f] 1a
                    add       (hl)                          ;[0b20] 86
                    ld        (de),a                        ;[0b21] 12
                    inc       de                            ;[0b22] 13
                    ld        a,(de)                        ;[0b23] 1a
                    inc       hl                            ;[0b24] 23
                    adc       (hl)                          ;[0b25] 8e
                    ld        (de),a                        ;[0b26] 12
                    inc       de                            ;[0b27] 13
                    ld        a,(de)                        ;[0b28] 1a
                    inc       hl                            ;[0b29] 23
                    adc       (hl)                          ;[0b2a] 8e
                    ld        (de),a                        ;[0b2b] 12
                    inc       de                            ;[0b2c] 13
                    ld        a,(de)                        ;[0b2d] 1a
                    inc       hl                            ;[0b2e] 23
                    adc       (hl)                          ;[0b2f] 8e
                    ld        (de),a                        ;[0b30] 12
                    pop       de                            ;[0b31] d1
                    ld        a,($fa51)                     ;[0b32] 3a 51 fa
                    ld        iy,$0006                      ;[0b35] fd 21 06 00
                    add       iy,sp                         ;[0b39] fd 39
                    ld        (iy+$00),a                    ;[0b3b] fd 77 00
                    ld        a,(iy+$00)                    ;[0b3e] fd 7e 00
                    ld        (iy+$00),a                    ;[0b41] fd 77 00
                    ld        (iy+$01),$00                  ;[0b44] fd 36 01 00
                    ld        (iy+$02),$00                  ;[0b48] fd 36 02 00
                    ld        (iy+$03),$00                  ;[0b4c] fd 36 03 00
                    push      af                            ;[0b50] f5
                    pop       af                            ;[0b51] f1
                    ld        b,$10                         ;[0b52] 06 10
                    sla       (iy+$00)                      ;[0b54] fd cb 00 26
                    rl        (iy+$01)                      ;[0b58] fd cb 01 16
                    rl        (iy+$02)                      ;[0b5c] fd cb 02 16
                    rl        (iy+$03)                      ;[0b60] fd cb 03 16
                    djnz      $0b54                         ;[0b64] 10 ee
                    ld        hl,$0006                      ;[0b66] 21 06 00
                    add       hl,sp                         ;[0b69] 39
                    push      de                            ;[0b6a] d5
                    ld        iy,$0004                      ;[0b6b] fd 21 04 00
                    add       iy,sp                         ;[0b6f] fd 39
                    push      iy                            ;[0b71] fd e5
                    pop       de                            ;[0b73] d1
                    ld        a,(de)                        ;[0b74] 1a
                    add       (hl)                          ;[0b75] 86
                    ld        (de),a                        ;[0b76] 12
                    inc       de                            ;[0b77] 13
                    ld        a,(de)                        ;[0b78] 1a
                    inc       hl                            ;[0b79] 23
                    adc       (hl)                          ;[0b7a] 8e
                    ld        (de),a                        ;[0b7b] 12
                    inc       de                            ;[0b7c] 13
                    ld        a,(de)                        ;[0b7d] 1a
                    inc       hl                            ;[0b7e] 23
                    adc       (hl)                          ;[0b7f] 8e
                    ld        (de),a                        ;[0b80] 12
                    inc       de                            ;[0b81] 13
                    ld        a,(de)                        ;[0b82] 1a
                    inc       hl                            ;[0b83] 23
                    adc       (hl)                          ;[0b84] 8e
                    ld        (de),a                        ;[0b85] 12
                    pop       de                            ;[0b86] d1
                    ld        a,($fa52)                     ;[0b87] 3a 52 fa
                    ld        iy,$0006                      ;[0b8a] fd 21 06 00
                    add       iy,sp                         ;[0b8e] fd 39
                    ld        (iy+$00),a                    ;[0b90] fd 77 00
                    ld        a,(iy+$00)                    ;[0b93] fd 7e 00
                    ld        (iy+$00),a                    ;[0b96] fd 77 00
                    ld        (iy+$01),$00                  ;[0b99] fd 36 01 00
                    ld        (iy+$02),$00                  ;[0b9d] fd 36 02 00
                    ld        (iy+$03),$00                  ;[0ba1] fd 36 03 00
                    push      af                            ;[0ba5] f5
                    pop       af                            ;[0ba6] f1
                    ld        b,$18                         ;[0ba7] 06 18
                    sla       (iy+$00)                      ;[0ba9] fd cb 00 26
                    rl        (iy+$01)                      ;[0bad] fd cb 01 16
                    rl        (iy+$02)                      ;[0bb1] fd cb 02 16
                    rl        (iy+$03)                      ;[0bb5] fd cb 03 16
                    djnz      $0ba9                         ;[0bb9] 10 ee
                    ld        hl,$0006                      ;[0bbb] 21 06 00
                    add       hl,sp                         ;[0bbe] 39
                    push      de                            ;[0bbf] d5
                    ld        de,$fa27                      ;[0bc0] 11 27 fa
                    ld        iy,$0004                      ;[0bc3] fd 21 04 00
                    add       iy,sp                         ;[0bc7] fd 39
                    ld        a,(iy+$00)                    ;[0bc9] fd 7e 00
                    add       (hl)                          ;[0bcc] 86
                    ld        (de),a                        ;[0bcd] 12
                    ld        a,(iy+$01)                    ;[0bce] fd 7e 01
                    inc       hl                            ;[0bd1] 23
                    adc       (hl)                          ;[0bd2] 8e
                    inc       de                            ;[0bd3] 13
                    ld        (de),a                        ;[0bd4] 12
                    ld        a,(iy+$02)                    ;[0bd5] fd 7e 02
                    inc       hl                            ;[0bd8] 23
                    adc       (hl)                          ;[0bd9] 8e
                    inc       de                            ;[0bda] 13
                    ld        (de),a                        ;[0bdb] 12
                    ld        a,(iy+$03)                    ;[0bdc] fd 7e 03
                    inc       hl                            ;[0bdf] 23
                    adc       (hl)                          ;[0be0] 8e
                    inc       de                            ;[0be1] 13
                    ld        (de),a                        ;[0be2] 12
                    pop       de                            ;[0be3] d1
                    ld        a,($fa19)                     ;[0be4] 3a 19 fa
                    ld        iy,$0002                      ;[0be7] fd 21 02 00
                    add       iy,sp                         ;[0beb] fd 39
                    ld        (iy+$00),a                    ;[0bed] fd 77 00
                    ld        a,($fa1a)                     ;[0bf0] 3a 1a fa
                    ld        iy,$0002                      ;[0bf3] fd 21 02 00
                    add       iy,sp                         ;[0bf7] fd 39
                    ld        (iy+$01),a                    ;[0bf9] fd 77 01
                    ld        (iy+$02),$00                  ;[0bfc] fd 36 02 00
                    ld        (iy+$03),$00                  ;[0c00] fd 36 03 00
                    ld        hl,($fa29)                    ;[0c04] 2a 29 fa
                    push      hl                            ;[0c07] e5
                    ld        hl,($fa27)                    ;[0c08] 2a 27 fa
                    push      hl                            ;[0c0b] e5
                    ld        l,(iy+$02)                    ;[0c0c] fd 6e 02
                    ld        h,(iy+$03)                    ;[0c0f] fd 66 03
                    push      hl                            ;[0c12] e5
                    ld        l,(iy+$00)                    ;[0c13] fd 6e 00
                    ld        h,(iy+$01)                    ;[0c16] fd 66 01
                    push      hl                            ;[0c19] e5
                    call      $1b41                         ;[0c1a] cd 41 1b
                    pop       af                            ;[0c1d] f1
                    pop       af                            ;[0c1e] f1
                    pop       af                            ;[0c1f] f1
                    pop       af                            ;[0c20] f1
                    ld        iy,$0002                      ;[0c21] fd 21 02 00
                    add       iy,sp                         ;[0c25] fd 39
                    ld        (iy+$03),d                    ;[0c27] fd 72 03
                    ld        (iy+$02),e                    ;[0c2a] fd 73 02
                    ld        (iy+$01),h                    ;[0c2d] fd 74 01
                    ld        (iy+$00),l                    ;[0c30] fd 75 00
                    ld        hl,$0002                      ;[0c33] 21 02 00
                    add       hl,sp                         ;[0c36] 39
                    push      de                            ;[0c37] d5
                    ld        de,$fa09                      ;[0c38] 11 09 fa
                    ld        iy,$fa05                      ;[0c3b] fd 21 05 fa
                    ld        a,(iy+$00)                    ;[0c3f] fd 7e 00
                    add       (hl)                          ;[0c42] 86
                    ld        (de),a                        ;[0c43] 12
                    ld        a,(iy+$01)                    ;[0c44] fd 7e 01
                    inc       hl                            ;[0c47] 23
                    adc       (hl)                          ;[0c48] 8e
                    inc       de                            ;[0c49] 13
                    ld        (de),a                        ;[0c4a] 12
                    ld        a,(iy+$02)                    ;[0c4b] fd 7e 02
                    inc       hl                            ;[0c4e] 23
                    adc       (hl)                          ;[0c4f] 8e
                    inc       de                            ;[0c50] 13
                    ld        (de),a                        ;[0c51] 12
                    ld        a,(iy+$03)                    ;[0c52] fd 7e 03
                    inc       hl                            ;[0c55] 23
                    adc       (hl)                          ;[0c56] 8e
                    inc       de                            ;[0c57] 13
                    ld        (de),a                        ;[0c58] 12
                    pop       de                            ;[0c59] d1
                    ld        a,($fa57)                     ;[0c5a] 3a 57 fa
                    ld        iy,$0002                      ;[0c5d] fd 21 02 00
                    add       iy,sp                         ;[0c61] fd 39
                    ld        (iy+$00),a                    ;[0c63] fd 77 00
                    ld        a,($fa58)                     ;[0c66] 3a 58 fa
                    ld        iy,$0006                      ;[0c69] fd 21 06 00
                    add       iy,sp                         ;[0c6d] fd 39
                    ld        (iy+$00),a                    ;[0c6f] fd 77 00
                    ld        a,(iy+$00)                    ;[0c72] fd 7e 00
                    ld        (iy+$00),a                    ;[0c75] fd 77 00
                    ld        (iy+$01),$00                  ;[0c78] fd 36 01 00
                    ld        (iy+$02),$00                  ;[0c7c] fd 36 02 00
                    ld        (iy+$03),$00                  ;[0c80] fd 36 03 00
                    push      af                            ;[0c84] f5
                    pop       af                            ;[0c85] f1
                    ld        b,$08                         ;[0c86] 06 08
                    sla       (iy+$00)                      ;[0c88] fd cb 00 26
                    rl        (iy+$01)                      ;[0c8c] fd cb 01 16
                    rl        (iy+$02)                      ;[0c90] fd cb 02 16
                    rl        (iy+$03)                      ;[0c94] fd cb 03 16
                    djnz      $0c88                         ;[0c98] 10 ee
                    ld        iy,$0002                      ;[0c9a] fd 21 02 00
                    add       iy,sp                         ;[0c9e] fd 39
                    ld        a,(iy+$00)                    ;[0ca0] fd 7e 00
                    ld        (iy+$00),a                    ;[0ca3] fd 77 00
                    ld        (iy+$01),$00                  ;[0ca6] fd 36 01 00
                    ld        (iy+$02),$00                  ;[0caa] fd 36 02 00
                    ld        (iy+$03),$00                  ;[0cae] fd 36 03 00
                    ld        hl,$0006                      ;[0cb2] 21 06 00
                    add       hl,sp                         ;[0cb5] 39
                    push      de                            ;[0cb6] d5
                    push      iy                            ;[0cb7] fd e5
                    pop       de                            ;[0cb9] d1
                    ld        a,(de)                        ;[0cba] 1a
                    add       (hl)                          ;[0cbb] 86
                    ld        (de),a                        ;[0cbc] 12
                    inc       de                            ;[0cbd] 13
                    ld        a,(de)                        ;[0cbe] 1a
                    inc       hl                            ;[0cbf] 23
                    adc       (hl)                          ;[0cc0] 8e
                    ld        (de),a                        ;[0cc1] 12
                    inc       de                            ;[0cc2] 13
                    ld        a,(de)                        ;[0cc3] 1a
                    inc       hl                            ;[0cc4] 23
                    adc       (hl)                          ;[0cc5] 8e
                    ld        (de),a                        ;[0cc6] 12
                    inc       de                            ;[0cc7] 13
                    ld        a,(de)                        ;[0cc8] 1a
                    inc       hl                            ;[0cc9] 23
                    adc       (hl)                          ;[0cca] 8e
                    ld        (de),a                        ;[0ccb] 12
                    pop       de                            ;[0ccc] d1
                    ld        a,($fa59)                     ;[0ccd] 3a 59 fa
                    ld        iy,$0006                      ;[0cd0] fd 21 06 00
                    add       iy,sp                         ;[0cd4] fd 39
                    ld        (iy+$00),a                    ;[0cd6] fd 77 00
                    ld        a,(iy+$00)                    ;[0cd9] fd 7e 00
                    ld        (iy+$00),a                    ;[0cdc] fd 77 00
                    ld        (iy+$01),$00                  ;[0cdf] fd 36 01 00
                    ld        (iy+$02),$00                  ;[0ce3] fd 36 02 00
                    ld        (iy+$03),$00                  ;[0ce7] fd 36 03 00
                    push      af                            ;[0ceb] f5
                    pop       af                            ;[0cec] f1
                    ld        b,$10                         ;[0ced] 06 10
                    sla       (iy+$00)                      ;[0cef] fd cb 00 26
                    rl        (iy+$01)                      ;[0cf3] fd cb 01 16
                    rl        (iy+$02)                      ;[0cf7] fd cb 02 16
                    rl        (iy+$03)                      ;[0cfb] fd cb 03 16
                    djnz      $0cef                         ;[0cff] 10 ee
                    ld        hl,$0006                      ;[0d01] 21 06 00
                    add       hl,sp                         ;[0d04] 39
                    push      de                            ;[0d05] d5
                    ld        iy,$0004                      ;[0d06] fd 21 04 00
                    add       iy,sp                         ;[0d0a] fd 39
                    push      iy                            ;[0d0c] fd e5
                    pop       de                            ;[0d0e] d1
                    ld        a,(de)                        ;[0d0f] 1a
                    add       (hl)                          ;[0d10] 86
                    ld        (de),a                        ;[0d11] 12
                    inc       de                            ;[0d12] 13
                    ld        a,(de)                        ;[0d13] 1a
                    inc       hl                            ;[0d14] 23
                    adc       (hl)                          ;[0d15] 8e
                    ld        (de),a                        ;[0d16] 12
                    inc       de                            ;[0d17] 13
                    ld        a,(de)                        ;[0d18] 1a
                    inc       hl                            ;[0d19] 23
                    adc       (hl)                          ;[0d1a] 8e
                    ld        (de),a                        ;[0d1b] 12
                    inc       de                            ;[0d1c] 13
                    ld        a,(de)                        ;[0d1d] 1a
                    inc       hl                            ;[0d1e] 23
                    adc       (hl)                          ;[0d1f] 8e
                    ld        (de),a                        ;[0d20] 12
                    pop       de                            ;[0d21] d1
                    ld        a,($fa5a)                     ;[0d22] 3a 5a fa
                    ld        iy,$0006                      ;[0d25] fd 21 06 00
                    add       iy,sp                         ;[0d29] fd 39
                    ld        (iy+$00),a                    ;[0d2b] fd 77 00
                    ld        a,(iy+$00)                    ;[0d2e] fd 7e 00
                    and       $0f                           ;[0d31] e6 0f
                    ld        (iy+$00),a                    ;[0d33] fd 77 00
                    ld        a,(iy+$00)                    ;[0d36] fd 7e 00
                    ld        (iy+$00),a                    ;[0d39] fd 77 00
                    ld        (iy+$01),$00                  ;[0d3c] fd 36 01 00
                    ld        (iy+$02),$00                  ;[0d40] fd 36 02 00
                    ld        (iy+$03),$00                  ;[0d44] fd 36 03 00
                    push      af                            ;[0d48] f5
                    pop       af                            ;[0d49] f1
                    ld        b,$18                         ;[0d4a] 06 18
                    sla       (iy+$00)                      ;[0d4c] fd cb 00 26
                    rl        (iy+$01)                      ;[0d50] fd cb 01 16
                    rl        (iy+$02)                      ;[0d54] fd cb 02 16
                    rl        (iy+$03)                      ;[0d58] fd cb 03 16
                    djnz      $0d4c                         ;[0d5c] 10 ee
                    ld        hl,$0006                      ;[0d5e] 21 06 00
                    add       hl,sp                         ;[0d61] 39
                    push      de                            ;[0d62] d5
                    ld        de,$fa0d                      ;[0d63] 11 0d fa
                    ld        iy,$0004                      ;[0d66] fd 21 04 00
                    add       iy,sp                         ;[0d6a] fd 39
                    ld        a,(iy+$00)                    ;[0d6c] fd 7e 00
                    add       (hl)                          ;[0d6f] 86
                    ld        (de),a                        ;[0d70] 12
                    ld        a,(iy+$01)                    ;[0d71] fd 7e 01
                    inc       hl                            ;[0d74] 23
                    adc       (hl)                          ;[0d75] 8e
                    inc       de                            ;[0d76] 13
                    ld        (de),a                        ;[0d77] 12
                    ld        a,(iy+$02)                    ;[0d78] fd 7e 02
                    inc       hl                            ;[0d7b] 23
                    adc       (hl)                          ;[0d7c] 8e
                    inc       de                            ;[0d7d] 13
                    ld        (de),a                        ;[0d7e] 12
                    ld        a,(iy+$03)                    ;[0d7f] fd 7e 03
                    inc       hl                            ;[0d82] 23
                    adc       (hl)                          ;[0d83] 8e
                    inc       de                            ;[0d84] 13
                    ld        (de),a                        ;[0d85] 12
                    pop       de                            ;[0d86] d1
                    ld        hl,$0002                      ;[0d87] 21 02 00
                    add       hl,sp                         ;[0d8a] 39
                    ld        iy,$fa0d                      ;[0d8b] fd 21 0d fa
                    ld        a,(iy+$00)                    ;[0d8f] fd 7e 00
                    add       $fe                           ;[0d92] c6 fe
                    ld        (hl),a                        ;[0d94] 77
                    ld        a,(iy+$01)                    ;[0d95] fd 7e 01
                    adc       $ff                           ;[0d98] ce ff
                    inc       hl                            ;[0d9a] 23
                    ld        (hl),a                        ;[0d9b] 77
                    ld        a,(iy+$02)                    ;[0d9c] fd 7e 02
                    adc       $ff                           ;[0d9f] ce ff
                    inc       hl                            ;[0da1] 23
                    ld        (hl),a                        ;[0da2] 77
                    ld        a,(iy+$03)                    ;[0da3] fd 7e 03
                    adc       $ff                           ;[0da6] ce ff
                    inc       hl                            ;[0da8] 23
                    ld        (hl),a                        ;[0da9] 77
                    ld        hl,($fa1d)                    ;[0daa] 2a 1d fa
                    push      hl                            ;[0dad] e5
                    ld        hl,($fa1b)                    ;[0dae] 2a 1b fa
                    push      hl                            ;[0db1] e5
                    ld        iy,$0006                      ;[0db2] fd 21 06 00
                    add       iy,sp                         ;[0db6] fd 39
                    ld        l,(iy+$02)                    ;[0db8] fd 6e 02
                    ld        h,(iy+$03)                    ;[0dbb] fd 66 03
                    push      hl                            ;[0dbe] e5
                    ld        l,(iy+$00)                    ;[0dbf] fd 6e 00
                    ld        h,(iy+$01)                    ;[0dc2] fd 66 01
                    push      hl                            ;[0dc5] e5
                    call      $1b41                         ;[0dc6] cd 41 1b
                    pop       af                            ;[0dc9] f1
                    pop       af                            ;[0dca] f1
                    pop       af                            ;[0dcb] f1
                    pop       af                            ;[0dcc] f1
                    ld        iy,$0002                      ;[0dcd] fd 21 02 00
                    add       iy,sp                         ;[0dd1] fd 39
                    ld        (iy+$03),d                    ;[0dd3] fd 72 03
                    ld        (iy+$02),e                    ;[0dd6] fd 73 02
                    ld        (iy+$01),h                    ;[0dd9] fd 74 01
                    ld        (iy+$00),l                    ;[0ddc] fd 75 00
                    ld        hl,$fa09                      ;[0ddf] 21 09 fa
                    push      de                            ;[0de2] d5
                    ld        de,$fa11                      ;[0de3] 11 11 fa
                    ld        iy,$0004                      ;[0de6] fd 21 04 00
                    add       iy,sp                         ;[0dea] fd 39
                    ld        a,(iy+$00)                    ;[0dec] fd 7e 00
                    add       (hl)                          ;[0def] 86
                    ld        (de),a                        ;[0df0] 12
                    ld        a,(iy+$01)                    ;[0df1] fd 7e 01
                    inc       hl                            ;[0df4] 23
                    adc       (hl)                          ;[0df5] 8e
                    inc       de                            ;[0df6] 13
                    ld        (de),a                        ;[0df7] 12
                    ld        a,(iy+$02)                    ;[0df8] fd 7e 02
                    inc       hl                            ;[0dfb] 23
                    adc       (hl)                          ;[0dfc] 8e
                    inc       de                            ;[0dfd] 13
                    ld        (de),a                        ;[0dfe] 12
                    ld        a,(iy+$03)                    ;[0dff] fd 7e 03
                    inc       hl                            ;[0e02] 23
                    adc       (hl)                          ;[0e03] 8e
                    inc       de                            ;[0e04] 13
                    ld        (de),a                        ;[0e05] 12
                    pop       de                            ;[0e06] d1
                    jp        $1020                         ;[0e07] c3 20 10
                    ld        a,($fa3c)                     ;[0e0a] 3a 3c fa
                    ld        iy,$0002                      ;[0e0d] fd 21 02 00
                    add       iy,sp                         ;[0e11] fd 39
                    ld        (iy+$00),a                    ;[0e13] fd 77 00
                    ld        (iy+$01),$00                  ;[0e16] fd 36 01 00
                    ld        a,($fa3d)                     ;[0e1a] 3a 3d fa
                    ld        iy,$0006                      ;[0e1d] fd 21 06 00
                    add       iy,sp                         ;[0e21] fd 39
                    ld        (iy+$00),a                    ;[0e23] fd 77 00
                    ld        a,(iy+$00)                    ;[0e26] fd 7e 00
                    ld        (iy+$00),a                    ;[0e29] fd 77 00
                    ld        (iy+$01),$00                  ;[0e2c] fd 36 01 00
                    ld        a,(iy+$00)                    ;[0e30] fd 7e 00
                    ld        (iy+$01),a                    ;[0e33] fd 77 01
                    ld        (iy+$00),$00                  ;[0e36] fd 36 00 00
                    ld        hl,$0006                      ;[0e3a] 21 06 00
                    add       hl,sp                         ;[0e3d] 39
                    push      de                            ;[0e3e] d5
                    ld        iy,$0004                      ;[0e3f] fd 21 04 00
                    add       iy,sp                         ;[0e43] fd 39
                    push      iy                            ;[0e45] fd e5
                    pop       de                            ;[0e47] d1
                    ld        a,(iy+$00)                    ;[0e48] fd 7e 00
                    add       (hl)                          ;[0e4b] 86
                    ld        (de),a                        ;[0e4c] 12
                    ld        a,(iy+$01)                    ;[0e4d] fd 7e 01
                    inc       hl                            ;[0e50] 23
                    adc       (hl)                          ;[0e51] 8e
                    inc       de                            ;[0e52] 13
                    ld        (de),a                        ;[0e53] 12
                    pop       de                            ;[0e54] d1
                    ld        a,(iy+$00)                    ;[0e55] fd 7e 00
                    ld        ($fa23),a                     ;[0e58] 32 23 fa
                    ld        hl,$0003                      ;[0e5b] 21 03 00
                    add       hl,sp                         ;[0e5e] 39
                    ld        a,(hl)                        ;[0e5f] 7e
                    ld        ($fa24),a                     ;[0e60] 32 24 fa
                    ld        hl,$0003                      ;[0e63] 21 03 00
                    add       hl,sp                         ;[0e66] 39
                    ld        a,(hl)                        ;[0e67] 7e
                    rla                                     ;[0e68] 17
                    sbc       a                             ;[0e69] 9f
                    ld        iy,$fa23                      ;[0e6a] fd 21 23 fa
                    ld        (iy+$02),a                    ;[0e6e] fd 77 02
                    ld        (iy+$03),a                    ;[0e71] fd 77 03
                    push      af                            ;[0e74] f5
                    ld        a,(iy+$00)                    ;[0e75] fd 7e 00
                    ld        iy,$0004                      ;[0e78] fd 21 04 00
                    add       iy,sp                         ;[0e7c] fd 39
                    ld        (iy+$00),a                    ;[0e7e] fd 77 00
                    ld        a,($fa24)                     ;[0e81] 3a 24 fa
                    ld        iy,$0004                      ;[0e84] fd 21 04 00
                    add       iy,sp                         ;[0e88] fd 39
                    ld        (iy+$01),a                    ;[0e8a] fd 77 01
                    ld        a,($fa25)                     ;[0e8d] 3a 25 fa
                    ld        iy,$0004                      ;[0e90] fd 21 04 00
                    add       iy,sp                         ;[0e94] fd 39
                    ld        (iy+$02),a                    ;[0e96] fd 77 02
                    ld        a,($fa26)                     ;[0e99] 3a 26 fa
                    ld        iy,$0004                      ;[0e9c] fd 21 04 00
                    add       iy,sp                         ;[0ea0] fd 39
                    ld        (iy+$03),a                    ;[0ea2] fd 77 03
                    pop       af                            ;[0ea5] f1
                    ld        b,$05                         ;[0ea6] 06 05
                    sla       (iy+$00)                      ;[0ea8] fd cb 00 26
                    rl        (iy+$01)                      ;[0eac] fd cb 01 16
                    rl        (iy+$02)                      ;[0eb0] fd cb 02 16
                    rl        (iy+$03)                      ;[0eb4] fd cb 03 16
                    djnz      $0ea8                         ;[0eb8] 10 ee
                    ld        hl,$0002                      ;[0eba] 21 02 00
                    add       hl,sp                         ;[0ebd] 39
                    ld        a,(hl)                        ;[0ebe] 7e
                    add       $ff                           ;[0ebf] c6 ff
                    ld        (hl),a                        ;[0ec1] 77
                    inc       hl                            ;[0ec2] 23
                    ld        a,(hl)                        ;[0ec3] 7e
                    adc       $01                           ;[0ec4] ce 01
                    ld        (hl),a                        ;[0ec6] 77
                    inc       hl                            ;[0ec7] 23
                    ld        a,(hl)                        ;[0ec8] 7e
                    adc       $00                           ;[0ec9] ce 00
                    ld        (hl),a                        ;[0ecb] 77
                    inc       hl                            ;[0ecc] 23
                    ld        a,(hl)                        ;[0ecd] 7e
                    adc       $00                           ;[0ece] ce 00
                    ld        (hl),a                        ;[0ed0] 77
                    push      af                            ;[0ed1] f5
                    ld        a,(iy+$00)                    ;[0ed2] fd 7e 00
                    ld        ($fa15),a                     ;[0ed5] 32 15 fa
                    ld        hl,$0005                      ;[0ed8] 21 05 00
                    add       hl,sp                         ;[0edb] 39
                    ld        a,(hl)                        ;[0edc] 7e
                    ld        ($fa16),a                     ;[0edd] 32 16 fa
                    ld        hl,$0006                      ;[0ee0] 21 06 00
                    add       hl,sp                         ;[0ee3] 39
                    ld        a,(hl)                        ;[0ee4] 7e
                    ld        ($fa17),a                     ;[0ee5] 32 17 fa
                    ld        hl,$0007                      ;[0ee8] 21 07 00
                    add       hl,sp                         ;[0eeb] 39
                    ld        a,(hl)                        ;[0eec] 7e
                    ld        iy,$fa15                      ;[0eed] fd 21 15 fa
                    ld        (iy+$03),a                    ;[0ef1] fd 77 03
                    pop       af                            ;[0ef4] f1
                    ld        b,$09                         ;[0ef5] 06 09
                    srl       (iy+$03)                      ;[0ef7] fd cb 03 3e
                    rr        (iy+$02)                      ;[0efb] fd cb 02 1e
                    rr        (iy+$01)                      ;[0eff] fd cb 01 1e
                    rr        (iy+$00)                      ;[0f03] fd cb 00 1e
                    djnz      $0ef7                         ;[0f07] 10 ee
                    ld        a,($fa41)                     ;[0f09] 3a 41 fa
                    ld        iy,$0002                      ;[0f0c] fd 21 02 00
                    add       iy,sp                         ;[0f10] fd 39
                    ld        (iy+$00),a                    ;[0f12] fd 77 00
                    ld        (iy+$01),$00                  ;[0f15] fd 36 01 00
                    ld        a,($fa42)                     ;[0f19] 3a 42 fa
                    ld        iy,$0006                      ;[0f1c] fd 21 06 00
                    add       iy,sp                         ;[0f20] fd 39
                    ld        (iy+$00),a                    ;[0f22] fd 77 00
                    ld        a,(iy+$00)                    ;[0f25] fd 7e 00
                    ld        (iy+$00),a                    ;[0f28] fd 77 00
                    ld        (iy+$01),$00                  ;[0f2b] fd 36 01 00
                    ld        a,(iy+$00)                    ;[0f2f] fd 7e 00
                    ld        (iy+$01),a                    ;[0f32] fd 77 01
                    ld        (iy+$00),$00                  ;[0f35] fd 36 00 00
                    ld        hl,$0006                      ;[0f39] 21 06 00
                    add       hl,sp                         ;[0f3c] 39
                    push      de                            ;[0f3d] d5
                    ld        iy,$0004                      ;[0f3e] fd 21 04 00
                    add       iy,sp                         ;[0f42] fd 39
                    push      iy                            ;[0f44] fd e5
                    pop       de                            ;[0f46] d1
                    ld        a,(iy+$00)                    ;[0f47] fd 7e 00
                    add       (hl)                          ;[0f4a] 86
                    ld        (de),a                        ;[0f4b] 12
                    ld        a,(iy+$01)                    ;[0f4c] fd 7e 01
                    inc       hl                            ;[0f4f] 23
                    adc       (hl)                          ;[0f50] 8e
                    inc       de                            ;[0f51] 13
                    ld        (de),a                        ;[0f52] 12
                    pop       de                            ;[0f53] d1
                    ld        a,(iy+$00)                    ;[0f54] fd 7e 00
                    ld        ($fa27),a                     ;[0f57] 32 27 fa
                    ld        hl,$0003                      ;[0f5a] 21 03 00
                    add       hl,sp                         ;[0f5d] 39
                    ld        a,(hl)                        ;[0f5e] 7e
                    ld        ($fa28),a                     ;[0f5f] 32 28 fa
                    ld        hl,$0003                      ;[0f62] 21 03 00
                    add       hl,sp                         ;[0f65] 39
                    ld        a,(hl)                        ;[0f66] 7e
                    rla                                     ;[0f67] 17
                    sbc       a                             ;[0f68] 9f
                    ld        iy,$fa27                      ;[0f69] fd 21 27 fa
                    ld        (iy+$02),a                    ;[0f6d] fd 77 02
                    ld        (iy+$03),a                    ;[0f70] fd 77 03
                    ld        a,($fa19)                     ;[0f73] 3a 19 fa
                    ld        iy,$0002                      ;[0f76] fd 21 02 00
                    add       iy,sp                         ;[0f7a] fd 39
                    ld        (iy+$00),a                    ;[0f7c] fd 77 00
                    ld        a,($fa1a)                     ;[0f7f] 3a 1a fa
                    ld        iy,$0002                      ;[0f82] fd 21 02 00
                    add       iy,sp                         ;[0f86] fd 39
                    ld        (iy+$01),a                    ;[0f88] fd 77 01
                    ld        (iy+$02),$00                  ;[0f8b] fd 36 02 00
                    ld        (iy+$03),$00                  ;[0f8f] fd 36 03 00
                    ld        hl,($fa29)                    ;[0f93] 2a 29 fa
                    push      hl                            ;[0f96] e5
                    ld        hl,($fa27)                    ;[0f97] 2a 27 fa
                    push      hl                            ;[0f9a] e5
                    ld        l,(iy+$02)                    ;[0f9b] fd 6e 02
                    ld        h,(iy+$03)                    ;[0f9e] fd 66 03
                    push      hl                            ;[0fa1] e5
                    ld        l,(iy+$00)                    ;[0fa2] fd 6e 00
                    ld        h,(iy+$01)                    ;[0fa5] fd 66 01
                    push      hl                            ;[0fa8] e5
                    call      $1b41                         ;[0fa9] cd 41 1b
                    pop       af                            ;[0fac] f1
                    pop       af                            ;[0fad] f1
                    pop       af                            ;[0fae] f1
                    pop       af                            ;[0faf] f1
                    ld        iy,$0002                      ;[0fb0] fd 21 02 00
                    add       iy,sp                         ;[0fb4] fd 39
                    ld        (iy+$03),d                    ;[0fb6] fd 72 03
                    ld        (iy+$02),e                    ;[0fb9] fd 73 02
                    ld        (iy+$01),h                    ;[0fbc] fd 74 01
                    ld        (iy+$00),l                    ;[0fbf] fd 75 00
                    ld        hl,$0002                      ;[0fc2] 21 02 00
                    add       hl,sp                         ;[0fc5] 39
                    push      de                            ;[0fc6] d5
                    ld        de,$fa11                      ;[0fc7] 11 11 fa
                    ld        iy,$fa05                      ;[0fca] fd 21 05 fa
                    ld        a,(iy+$00)                    ;[0fce] fd 7e 00
                    add       (hl)                          ;[0fd1] 86
                    ld        (de),a                        ;[0fd2] 12
                    ld        a,(iy+$01)                    ;[0fd3] fd 7e 01
                    inc       hl                            ;[0fd6] 23
                    adc       (hl)                          ;[0fd7] 8e
                    inc       de                            ;[0fd8] 13
                    ld        (de),a                        ;[0fd9] 12
                    ld        a,(iy+$02)                    ;[0fda] fd 7e 02
                    inc       hl                            ;[0fdd] 23
                    adc       (hl)                          ;[0fde] 8e
                    inc       de                            ;[0fdf] 13
                    ld        (de),a                        ;[0fe0] 12
                    ld        a,(iy+$03)                    ;[0fe1] fd 7e 03
                    inc       hl                            ;[0fe4] 23
                    adc       (hl)                          ;[0fe5] 8e
                    inc       de                            ;[0fe6] 13
                    ld        (de),a                        ;[0fe7] 12
                    pop       de                            ;[0fe8] d1
                    xor       a                             ;[0fe9] af
                    ld        iy,$fa0d                      ;[0fea] fd 21 0d fa
                    ld        (iy+$00),a                    ;[0fee] fd 77 00
                    ld        (iy+$01),a                    ;[0ff1] fd 77 01
                    ld        (iy+$02),a                    ;[0ff4] fd 77 02
                    ld        (iy+$03),a                    ;[0ff7] fd 77 03
                    ld        hl,$fa15                      ;[0ffa] 21 15 fa
                    push      de                            ;[0ffd] d5
                    ld        de,$fa09                      ;[0ffe] 11 09 fa
                    ld        iy,$fa11                      ;[1001] fd 21 11 fa
                    ld        a,(iy+$00)                    ;[1005] fd 7e 00
                    add       (hl)                          ;[1008] 86
                    ld        (de),a                        ;[1009] 12
                    ld        a,(iy+$01)                    ;[100a] fd 7e 01
                    inc       hl                            ;[100d] 23
                    adc       (hl)                          ;[100e] 8e
                    inc       de                            ;[100f] 13
                    ld        (de),a                        ;[1010] 12
                    ld        a,(iy+$02)                    ;[1011] fd 7e 02
                    inc       hl                            ;[1014] 23
                    adc       (hl)                          ;[1015] 8e
                    inc       de                            ;[1016] 13
                    ld        (de),a                        ;[1017] 12
                    ld        a,(iy+$03)                    ;[1018] fd 7e 03
                    inc       hl                            ;[101b] 23
                    adc       (hl)                          ;[101c] 8e
                    inc       de                            ;[101d] 13
                    ld        (de),a                        ;[101e] 12
                    pop       de                            ;[101f] d1
                    ld        l,$01                         ;[1020] 2e 01
                    ld        iy,$000b                      ;[1022] fd 21 0b 00
                    add       iy,sp                         ;[1026] fd 39
                    ld        sp,iy                         ;[1028] fd f9
                    ret                                     ;[102a] c9

                    ld        b,(hl)                        ;[102b] 46
                    ld        b,c                           ;[102c] 41
                    ld        d,h                           ;[102d] 54
                    ld        sp,$2036                      ;[102e] 31 36 20
                    jr        nz,$1053                      ;[1031] 20 20
                    nop                                     ;[1033] 00
                    ld        b,(hl)                        ;[1034] 46
                    ld        b,c                           ;[1035] 41
                    ld        d,h                           ;[1036] 54
                    inc       sp                            ;[1037] 33
                    ld        ($2020),a                     ;[1038] 32 20 20
                    jr        nz,$103d                      ;[103b] 20 00
                    ld        hl,$ffe0                      ;[103d] 21 e0 ff
                    add       hl,sp                         ;[1040] 39
                    ld        sp,hl                         ;[1041] f9
                    ld        hl,$0004                      ;[1042] 21 04 00
                    add       hl,sp                         ;[1045] 39
                    ld        (hl),$00                      ;[1046] 36 00
                    inc       hl                            ;[1048] 23
                    ld        (hl),$00                      ;[1049] 36 00
                    ld        iy,$fc2d                      ;[104b] fd 21 2d fc
                    ld        (iy+$00),$ff                  ;[104f] fd 36 00 ff
                    ld        (iy+$01),$ff                  ;[1053] fd 36 01 ff
                    ld        (iy+$02),$ff                  ;[1057] fd 36 02 ff
                    ld        (iy+$03),$ff                  ;[105b] fd 36 03 ff
                    ld        hl,$000a                      ;[105f] 21 0a 00
                    add       hl,sp                         ;[1062] 39
                    ex        de,hl                         ;[1063] eb
                    ld        hl,$fa0d                      ;[1064] 21 0d fa
                    ld        bc,$0004                      ;[1067] 01 04 00
                    ldir                                    ;[106a] ed b0
                    ld        hl,$001a                      ;[106c] 21 1a 00
                    add       hl,sp                         ;[106f] 39
                    ex        de,hl                         ;[1070] eb
                    ld        hl,$fa11                      ;[1071] 21 11 fa
                    ld        bc,$0004                      ;[1074] 01 04 00
                    ldir                                    ;[1077] ed b0
                    ld        iy,$fa03                      ;[1079] fd 21 03 fa
                    ld        a,(iy+$01)                    ;[107d] fd 7e 01
                    or        (iy+$00)                      ;[1080] fd b6 00
                    jr        z,$10a3                       ;[1083] 28 1e
                    push      af                            ;[1085] f5
                    ld        iy,$fa1b                      ;[1086] fd 21 1b fa
                    ld        e,(iy+$00)                    ;[108a] fd 5e 00
                    ld        d,(iy+$01)                    ;[108d] fd 56 01
                    ld        l,(iy+$02)                    ;[1090] fd 6e 02
                    ld        h,(iy+$03)                    ;[1093] fd 66 03
                    pop       af                            ;[1096] f1
                    ld        b,$04                         ;[1097] 06 04
                    sla       e                             ;[1099] cb 23
                    rl        d                             ;[109b] cb 12
                    adc       hl,hl                         ;[109d] ed 6a
                    djnz      $1099                         ;[109f] 10 f8
                    jr        $10bf                         ;[10a1] 18 1c
                    push      af                            ;[10a3] f5
                    ld        iy,$fa15                      ;[10a4] fd 21 15 fa
                    ld        e,(iy+$00)                    ;[10a8] fd 5e 00
                    ld        d,(iy+$01)                    ;[10ab] fd 56 01
                    ld        l,(iy+$02)                    ;[10ae] fd 6e 02
                    ld        h,(iy+$03)                    ;[10b1] fd 66 03
                    pop       af                            ;[10b4] f1
                    ld        b,$04                         ;[10b5] 06 04
                    sla       e                             ;[10b7] cb 23
                    rl        d                             ;[10b9] cb 12
                    adc       hl,hl                         ;[10bb] ed 6a
                    djnz      $10b7                         ;[10bd] 10 f8
                    ld        iy,$0000                      ;[10bf] fd 21 00 00
                    add       iy,sp                         ;[10c3] fd 39
                    ld        (iy+$00),e                    ;[10c5] fd 73 00
                    ld        (iy+$01),d                    ;[10c8] fd 72 01
                    ld        (iy+$02),l                    ;[10cb] fd 75 02
                    ld        (iy+$03),h                    ;[10ce] fd 74 03
                    ld        hl,$0014                      ;[10d1] 21 14 00
                    add       hl,sp                         ;[10d4] 39
                    ex        de,hl                         ;[10d5] eb
                    ld        hl,$001a                      ;[10d6] 21 1a 00
                    add       hl,sp                         ;[10d9] 39
                    ld        bc,$0004                      ;[10da] 01 04 00
                    ldir                                    ;[10dd] ed b0
                    xor       a                             ;[10df] af
                    ld        iy,$0006                      ;[10e0] fd 21 06 00
                    add       iy,sp                         ;[10e4] fd 39
                    ld        (iy+$00),a                    ;[10e6] fd 77 00
                    ld        (iy+$01),a                    ;[10e9] fd 77 01
                    ld        (iy+$02),a                    ;[10ec] fd 77 02
                    ld        (iy+$03),a                    ;[10ef] fd 77 03
                    ld        hl,$0000                      ;[10f2] 21 00 00
                    add       hl,sp                         ;[10f5] 39
                    ld        iy,$0006                      ;[10f6] fd 21 06 00
                    add       iy,sp                         ;[10fa] fd 39
                    ld        a,(iy+$00)                    ;[10fc] fd 7e 00
                    sub       (hl)                          ;[10ff] 96
                    ld        a,(iy+$01)                    ;[1100] fd 7e 01
                    inc       hl                            ;[1103] 23
                    sbc       (hl)                          ;[1104] 9e
                    ld        a,(iy+$02)                    ;[1105] fd 7e 02
                    inc       hl                            ;[1108] 23
                    sbc       (hl)                          ;[1109] 9e
                    ld        a,(iy+$03)                    ;[110a] fd 7e 03
                    inc       hl                            ;[110d] 23
                    sbc       (hl)                          ;[110e] 9e
                    jp        nc,$12ea                      ;[110f] d2 ea 12
                    ld        a,(iy+$00)                    ;[1112] fd 7e 00
                    and       $0f                           ;[1115] e6 0f
                    jr        nz,$1154                      ;[1117] 20 3b
                    ld        iy,$0014                      ;[1119] fd 21 14 00
                    add       iy,sp                         ;[111d] fd 39
                    ld        c,(iy+$00)                    ;[111f] fd 4e 00
                    ld        b,(iy+$01)                    ;[1122] fd 46 01
                    ld        e,(iy+$02)                    ;[1125] fd 5e 02
                    ld        d,(iy+$03)                    ;[1128] fd 56 03
                    inc       (iy+$00)                      ;[112b] fd 34 00
                    jr        nz,$113d                      ;[112e] 20 0d
                    inc       (iy+$01)                      ;[1130] fd 34 01
                    jr        nz,$113d                      ;[1133] 20 08
                    inc       (iy+$02)                      ;[1135] fd 34 02
                    jr        nz,$113d                      ;[1138] 20 03
                    inc       (iy+$03)                      ;[113a] fd 34 03
                    ld        hl,$fa2b                      ;[113d] 21 2b fa
                    push      hl                            ;[1140] e5
                    push      de                            ;[1141] d5
                    push      bc                            ;[1142] c5
                    call      $04e8                         ;[1143] cd e8 04
                    pop       af                            ;[1146] f1
                    pop       af                            ;[1147] f1
                    pop       af                            ;[1148] f1
                    ld        hl,$0004                      ;[1149] 21 04 00
                    add       hl,sp                         ;[114c] 39
                    ld        (hl),$2b                      ;[114d] 36 2b
                    inc       hl                            ;[114f] 23
                    ld        (hl),$fa                      ;[1150] 36 fa
                    jr        $1161                         ;[1152] 18 0d
                    ld        hl,$0004                      ;[1154] 21 04 00
                    add       hl,sp                         ;[1157] 39
                    ld        a,(hl)                        ;[1158] 7e
                    add       $20                           ;[1159] c6 20
                    ld        (hl),a                        ;[115b] 77
                    inc       hl                            ;[115c] 23
                    ld        a,(hl)                        ;[115d] 7e
                    adc       $00                           ;[115e] ce 00
                    ld        (hl),a                        ;[1160] 77
                    ld        iy,$0004                      ;[1161] fd 21 04 00
                    add       iy,sp                         ;[1165] fd 39
                    ld        l,(iy+$00)                    ;[1167] fd 6e 00
                    ld        h,(iy+$01)                    ;[116a] fd 66 01
                    ld        a,(hl)                        ;[116d] 7e
                    or        a                             ;[116e] b7
                    jp        z,$12cc                       ;[116f] ca cc 12
                    sub       $e5                           ;[1172] d6 e5
                    jp        z,$12cc                       ;[1174] ca cc 12
                    ld        l,(iy+$00)                    ;[1177] fd 6e 00
                    ld        h,(iy+$01)                    ;[117a] fd 66 01
                    ld        de,$000b                      ;[117d] 11 0b 00
                    add       hl,de                         ;[1180] 19
                    ld        a,(hl)                        ;[1181] 7e
                    and       $18                           ;[1182] e6 18
                    jp        nz,$12cc                      ;[1184] c2 cc 12
                    ld        hl,$000b                      ;[1187] 21 0b 00
                    push      hl                            ;[118a] e5
                    ld        hl,$0026                      ;[118b] 21 26 00
                    add       hl,sp                         ;[118e] 39
                    ld        c,(hl)                        ;[118f] 4e
                    inc       hl                            ;[1190] 23
                    ld        b,(hl)                        ;[1191] 46
                    push      bc                            ;[1192] c5
                    ld        hl,$0008                      ;[1193] 21 08 00
                    add       hl,sp                         ;[1196] 39
                    ld        c,(hl)                        ;[1197] 4e
                    inc       hl                            ;[1198] 23
                    ld        b,(hl)                        ;[1199] 46
                    push      bc                            ;[119a] c5
                    call      $0763                         ;[119b] cd 63 07
                    pop       af                            ;[119e] f1
                    pop       af                            ;[119f] f1
                    pop       af                            ;[11a0] f1
                    ld        a,h                           ;[11a1] 7c
                    or        l                             ;[11a2] b5
                    jp        nz,$12cc                      ;[11a3] c2 cc 12
                    ld        hl,$0022                      ;[11a6] 21 22 00
                    add       hl,sp                         ;[11a9] 39
                    ld        a,(hl)                        ;[11aa] 7e
                    ld        iy,$001e                      ;[11ab] fd 21 1e 00
                    add       iy,sp                         ;[11af] fd 39
                    ld        (iy+$00),a                    ;[11b1] fd 77 00
                    ld        hl,$0023                      ;[11b4] 21 23 00
                    add       hl,sp                         ;[11b7] 39
                    ld        a,(hl)                        ;[11b8] 7e
                    ld        iy,$001e                      ;[11b9] fd 21 1e 00
                    add       iy,sp                         ;[11bd] fd 39
                    ld        (iy+$01),a                    ;[11bf] fd 77 01
                    ld        hl,$000e                      ;[11c2] 21 0e 00
                    add       hl,sp                         ;[11c5] 39
                    ld        a,(iy+$00)                    ;[11c6] fd 7e 00
                    add       $04                           ;[11c9] c6 04
                    ld        (hl),a                        ;[11cb] 77
                    ld        a,(iy+$01)                    ;[11cc] fd 7e 01
                    adc       $00                           ;[11cf] ce 00
                    inc       hl                            ;[11d1] 23
                    ld        (hl),a                        ;[11d2] 77
                    ld        hl,$0004                      ;[11d3] 21 04 00
                    add       hl,sp                         ;[11d6] 39
                    ld        a,(hl)                        ;[11d7] 7e
                    inc       hl                            ;[11d8] 23
                    ld        h,(hl)                        ;[11d9] 66
                    ld        l,a                           ;[11da] 6f
                    ld        de,$001c                      ;[11db] 11 1c 00
                    add       hl,de                         ;[11de] 19
                    ld        c,(hl)                        ;[11df] 4e
                    inc       hl                            ;[11e0] 23
                    ld        b,(hl)                        ;[11e1] 46
                    inc       hl                            ;[11e2] 23
                    ld        e,(hl)                        ;[11e3] 5e
                    inc       hl                            ;[11e4] 23
                    ld        d,(hl)                        ;[11e5] 56
                    ld        hl,$000e                      ;[11e6] 21 0e 00
                    add       hl,sp                         ;[11e9] 39
                    ld        a,(hl)                        ;[11ea] 7e
                    inc       hl                            ;[11eb] 23
                    ld        h,(hl)                        ;[11ec] 66
                    ld        l,a                           ;[11ed] 6f
                    ld        (hl),c                        ;[11ee] 71
                    inc       hl                            ;[11ef] 23
                    ld        (hl),b                        ;[11f0] 70
                    inc       hl                            ;[11f1] 23
                    ld        (hl),e                        ;[11f2] 73
                    inc       hl                            ;[11f3] 23
                    ld        (hl),d                        ;[11f4] 72
                    ld        hl,$000e                      ;[11f5] 21 0e 00
                    add       hl,sp                         ;[11f8] 39
                    ld        iy,$001e                      ;[11f9] fd 21 1e 00
                    add       iy,sp                         ;[11fd] fd 39
                    ld        a,(iy+$00)                    ;[11ff] fd 7e 00
                    add       $08                           ;[1202] c6 08
                    ld        (hl),a                        ;[1204] 77
                    ld        a,(iy+$01)                    ;[1205] fd 7e 01
                    adc       $00                           ;[1208] ce 00
                    inc       hl                            ;[120a] 23
                    ld        (hl),a                        ;[120b] 77
                    ld        iy,$0004                      ;[120c] fd 21 04 00
                    add       iy,sp                         ;[1210] fd 39
                    ld        l,(iy+$00)                    ;[1212] fd 6e 00
                    ld        h,(iy+$01)                    ;[1215] fd 66 01
                    ld        de,$001a                      ;[1218] 11 1a 00
                    add       hl,de                         ;[121b] 19
                    ld        a,(hl)                        ;[121c] 7e
                    ld        iy,$0018                      ;[121d] fd 21 18 00
                    add       iy,sp                         ;[1221] fd 39
                    ld        (iy+$00),a                    ;[1223] fd 77 00
                    inc       hl                            ;[1226] 23
                    ld        a,(hl)                        ;[1227] 7e
                    ld        (iy+$01),a                    ;[1228] fd 77 01
                    ld        iy,$fa03                      ;[122b] fd 21 03 fa
                    ld        a,(iy+$01)                    ;[122f] fd 7e 01
                    or        (iy+$00)                      ;[1232] fd b6 00
                    jr        z,$1277                       ;[1235] 28 40
                    ld        hl,$0004                      ;[1237] 21 04 00
                    add       hl,sp                         ;[123a] 39
                    ld        a,(hl)                        ;[123b] 7e
                    inc       hl                            ;[123c] 23
                    ld        h,(hl)                        ;[123d] 66
                    ld        l,a                           ;[123e] 6f
                    ld        de,$0014                      ;[123f] 11 14 00
                    add       hl,de                         ;[1242] 19
                    ld        c,(hl)                        ;[1243] 4e
                    inc       hl                            ;[1244] 23
                    ld        a,(hl)                        ;[1245] 7e
                    and       $0f                           ;[1246] e6 0f
                    ld        b,a                           ;[1248] 47
                    ld        de,$0000                      ;[1249] 11 00 00
                    push      af                            ;[124c] f5
                    ld        iy,$0012                      ;[124d] fd 21 12 00
                    add       iy,sp                         ;[1251] fd 39
                    ld        (iy+$00),c                    ;[1253] fd 71 00
                    ld        (iy+$01),b                    ;[1256] fd 70 01
                    ld        (iy+$02),e                    ;[1259] fd 73 02
                    ld        (iy+$03),d                    ;[125c] fd 72 03
                    pop       af                            ;[125f] f1
                    ld        a,$10                         ;[1260] 3e 10
                    sla       (iy+$00)                      ;[1262] fd cb 00 26
                    rl        (iy+$01)                      ;[1266] fd cb 01 16
                    rl        (iy+$02)                      ;[126a] fd cb 02 16
                    rl        (iy+$03)                      ;[126e] fd cb 03 16
                    dec       a                             ;[1272] 3d
                    jr        nz,$1262                      ;[1273] 20 ed
                    jr        $128a                         ;[1275] 18 13
                    xor       a                             ;[1277] af
                    ld        iy,$0010                      ;[1278] fd 21 10 00
                    add       iy,sp                         ;[127c] fd 39
                    ld        (iy+$00),a                    ;[127e] fd 77 00
                    ld        (iy+$01),a                    ;[1281] fd 77 01
                    ld        (iy+$02),a                    ;[1284] fd 77 02
                    ld        (iy+$03),a                    ;[1287] fd 77 03
                    ld        hl,$0018                      ;[128a] 21 18 00
                    add       hl,sp                         ;[128d] 39
                    ld        c,(hl)                        ;[128e] 4e
                    inc       hl                            ;[128f] 23
                    ld        b,(hl)                        ;[1290] 46
                    ld        de,$0000                      ;[1291] 11 00 00
                    ld        a,c                           ;[1294] 79
                    ld        hl,$0010                      ;[1295] 21 10 00
                    add       hl,sp                         ;[1298] 39
                    add       (hl)                          ;[1299] 86
                    ld        c,a                           ;[129a] 4f
                    ld        a,b                           ;[129b] 78
                    inc       hl                            ;[129c] 23
                    adc       (hl)                          ;[129d] 8e
                    ld        b,a                           ;[129e] 47
                    ld        a,e                           ;[129f] 7b
                    inc       hl                            ;[12a0] 23
                    adc       (hl)                          ;[12a1] 8e
                    ld        e,a                           ;[12a2] 5f
                    ld        a,d                           ;[12a3] 7a
                    inc       hl                            ;[12a4] 23
                    adc       (hl)                          ;[12a5] 8e
                    ld        d,a                           ;[12a6] 57
                    ld        hl,$000e                      ;[12a7] 21 0e 00
                    add       hl,sp                         ;[12aa] 39
                    ld        a,(hl)                        ;[12ab] 7e
                    inc       hl                            ;[12ac] 23
                    ld        h,(hl)                        ;[12ad] 66
                    ld        l,a                           ;[12ae] 6f
                    ld        (hl),c                        ;[12af] 71
                    inc       hl                            ;[12b0] 23
                    ld        (hl),b                        ;[12b1] 70
                    inc       hl                            ;[12b2] 23
                    ld        (hl),e                        ;[12b3] 73
                    inc       hl                            ;[12b4] 23
                    ld        (hl),d                        ;[12b5] 72
                    ld        hl,$001e                      ;[12b6] 21 1e 00
                    add       hl,sp                         ;[12b9] 39
                    ld        a,(hl)                        ;[12ba] 7e
                    inc       hl                            ;[12bb] 23
                    ld        h,(hl)                        ;[12bc] 66
                    ld        l,a                           ;[12bd] 6f
                    xor       a                             ;[12be] af
                    ld        (hl),a                        ;[12bf] 77
                    inc       hl                            ;[12c0] 23
                    ld        (hl),a                        ;[12c1] 77
                    inc       hl                            ;[12c2] 23
                    xor       a                             ;[12c3] af
                    ld        (hl),a                        ;[12c4] 77
                    inc       hl                            ;[12c5] 23
                    ld        (hl),a                        ;[12c6] 77
                    ld        l,$01                         ;[12c7] 2e 01
                    jp        $13e6                         ;[12c9] c3 e6 13
                    ld        iy,$0006                      ;[12cc] fd 21 06 00
                    add       iy,sp                         ;[12d0] fd 39
                    inc       (iy+$00)                      ;[12d2] fd 34 00
                    jp        nz,$10f2                      ;[12d5] c2 f2 10
                    inc       (iy+$01)                      ;[12d8] fd 34 01
                    jp        nz,$10f2                      ;[12db] c2 f2 10
                    inc       (iy+$02)                      ;[12de] fd 34 02
                    jp        nz,$10f2                      ;[12e1] c2 f2 10
                    inc       (iy+$03)                      ;[12e4] fd 34 03
                    jp        $10f2                         ;[12e7] c3 f2 10
                    ld        iy,$fa03                      ;[12ea] fd 21 03 fa
                    ld        a,(iy+$01)                    ;[12ee] fd 7e 01
                    or        (iy+$00)                      ;[12f1] fd b6 00
                    jp        z,$13e4                       ;[12f4] ca e4 13
                    ld        iy,$000a                      ;[12f7] fd 21 0a 00
                    add       iy,sp                         ;[12fb] fd 39
                    ld        l,(iy+$02)                    ;[12fd] fd 6e 02
                    ld        h,(iy+$03)                    ;[1300] fd 66 03
                    push      hl                            ;[1303] e5
                    ld        l,(iy+$00)                    ;[1304] fd 6e 00
                    ld        h,(iy+$01)                    ;[1307] fd 66 01
                    push      hl                            ;[130a] e5
                    call      $05bb                         ;[130b] cd bb 05
                    pop       af                            ;[130e] f1
                    pop       af                            ;[130f] f1
                    ld        iy,$0010                      ;[1310] fd 21 10 00
                    add       iy,sp                         ;[1314] fd 39
                    ld        (iy+$03),d                    ;[1316] fd 72 03
                    ld        (iy+$02),e                    ;[1319] fd 73 02
                    ld        (iy+$01),h                    ;[131c] fd 74 01
                    ld        (iy+$00),l                    ;[131f] fd 75 00
                    ld        hl,$000a                      ;[1322] 21 0a 00
                    add       hl,sp                         ;[1325] 39
                    ex        de,hl                         ;[1326] eb
                    ld        hl,$0010                      ;[1327] 21 10 00
                    add       hl,sp                         ;[132a] 39
                    ld        bc,$0004                      ;[132b] 01 04 00
                    ldir                                    ;[132e] ed b0
                    ld        hl,$000a                      ;[1330] 21 0a 00
                    add       hl,sp                         ;[1333] 39
                    ld        a,(hl)                        ;[1334] 7e
                    and       $f8                           ;[1335] e6 f8
                    ld        iy,$0010                      ;[1337] fd 21 10 00
                    add       iy,sp                         ;[133b] fd 39
                    ld        (iy+$00),a                    ;[133d] fd 77 00
                    ld        hl,$000b                      ;[1340] 21 0b 00
                    add       hl,sp                         ;[1343] 39
                    ld        a,(hl)                        ;[1344] 7e
                    ld        iy,$0010                      ;[1345] fd 21 10 00
                    add       iy,sp                         ;[1349] fd 39
                    ld        (iy+$01),a                    ;[134b] fd 77 01
                    ld        hl,$000c                      ;[134e] 21 0c 00
                    add       hl,sp                         ;[1351] 39
                    ld        a,(hl)                        ;[1352] 7e
                    ld        iy,$0010                      ;[1353] fd 21 10 00
                    add       iy,sp                         ;[1357] fd 39
                    ld        (iy+$02),a                    ;[1359] fd 77 02
                    ld        hl,$000d                      ;[135c] 21 0d 00
                    add       hl,sp                         ;[135f] 39
                    ld        a,(hl)                        ;[1360] 7e
                    and       $0f                           ;[1361] e6 0f
                    ld        iy,$0010                      ;[1363] fd 21 10 00
                    add       iy,sp                         ;[1367] fd 39
                    ld        (iy+$03),a                    ;[1369] fd 77 03
                    ld        a,(iy+$00)                    ;[136c] fd 7e 00
                    sub       $f8                           ;[136f] d6 f8
                    jr        nz,$1386                      ;[1371] 20 13
                    ld        a,(iy+$01)                    ;[1373] fd 7e 01
                    inc       a                             ;[1376] 3c
                    jr        nz,$1386                      ;[1377] 20 0d
                    ld        a,(iy+$02)                    ;[1379] fd 7e 02
                    inc       a                             ;[137c] 3c
                    jr        nz,$1386                      ;[137d] 20 07
                    ld        a,(iy+$03)                    ;[137f] fd 7e 03
                    sub       $0f                           ;[1382] d6 0f
                    jr        z,$13e4                       ;[1384] 28 5e
                    ld        iy,$000a                      ;[1386] fd 21 0a 00
                    add       iy,sp                         ;[138a] fd 39
                    ld        a,(iy+$00)                    ;[138c] fd 7e 00
                    add       $fe                           ;[138f] c6 fe
                    ld        c,a                           ;[1391] 4f
                    ld        a,(iy+$01)                    ;[1392] fd 7e 01
                    adc       $ff                           ;[1395] ce ff
                    ld        b,a                           ;[1397] 47
                    ld        a,(iy+$02)                    ;[1398] fd 7e 02
                    adc       $ff                           ;[139b] ce ff
                    ld        e,a                           ;[139d] 5f
                    ld        a,(iy+$03)                    ;[139e] fd 7e 03
                    adc       $ff                           ;[13a1] ce ff
                    ld        d,a                           ;[13a3] 57
                    push      de                            ;[13a4] d5
                    push      bc                            ;[13a5] c5
                    ld        hl,($fa1d)                    ;[13a6] 2a 1d fa
                    push      hl                            ;[13a9] e5
                    ld        hl,($fa1b)                    ;[13aa] 2a 1b fa
                    push      hl                            ;[13ad] e5
                    call      $1b41                         ;[13ae] cd 41 1b
                    pop       af                            ;[13b1] f1
                    pop       af                            ;[13b2] f1
                    pop       af                            ;[13b3] f1
                    pop       af                            ;[13b4] f1
                    ld        c,l                           ;[13b5] 4d
                    ld        b,h                           ;[13b6] 44
                    ld        iy,$fa09                      ;[13b7] fd 21 09 fa
                    ld        a,(iy+$00)                    ;[13bb] fd 7e 00
                    add       c                             ;[13be] 81
                    ld        c,a                           ;[13bf] 4f
                    ld        a,(iy+$01)                    ;[13c0] fd 7e 01
                    adc       b                             ;[13c3] 88
                    ld        b,a                           ;[13c4] 47
                    ld        a,(iy+$02)                    ;[13c5] fd 7e 02
                    adc       e                             ;[13c8] 8b
                    ld        e,a                           ;[13c9] 5f
                    ld        a,(iy+$03)                    ;[13ca] fd 7e 03
                    adc       d                             ;[13cd] 8a
                    ld        d,a                           ;[13ce] 57
                    ld        iy,$001a                      ;[13cf] fd 21 1a 00
                    add       iy,sp                         ;[13d3] fd 39
                    ld        (iy+$00),c                    ;[13d5] fd 71 00
                    ld        (iy+$01),b                    ;[13d8] fd 70 01
                    ld        (iy+$02),e                    ;[13db] fd 73 02
                    ld        (iy+$03),d                    ;[13de] fd 72 03
                    jp        $10d1                         ;[13e1] c3 d1 10
                    ld        l,$00                         ;[13e4] 2e 00
                    ld        iy,$0020                      ;[13e6] fd 21 20 00
                    add       iy,sp                         ;[13ea] fd 39
                    ld        sp,iy                         ;[13ec] fd f9
                    ret                                     ;[13ee] c9

                    ld        hl,$fff4                      ;[13ef] 21 f4 ff
                    add       hl,sp                         ;[13f2] 39
                    ld        sp,hl                         ;[13f3] f9
                    ld        hl,$0008                      ;[13f4] 21 08 00
                    add       hl,sp                         ;[13f7] 39
                    ex        de,hl                         ;[13f8] eb
                    ld        hl,$fa09                      ;[13f9] 21 09 fa
                    ld        bc,$0004                      ;[13fc] 01 04 00
                    ldir                                    ;[13ff] ed b0
                    ld        hl,$000e                      ;[1401] 21 0e 00
                    add       hl,sp                         ;[1404] 39
                    ld        a,(hl)                        ;[1405] 7e
                    ld        iy,$0004                      ;[1406] fd 21 04 00
                    add       iy,sp                         ;[140a] fd 39
                    ld        (iy+$00),a                    ;[140c] fd 77 00
                    ld        hl,$000f                      ;[140f] 21 0f 00
                    add       hl,sp                         ;[1412] 39
                    ld        a,(hl)                        ;[1413] 7e
                    ld        iy,$0004                      ;[1414] fd 21 04 00
                    add       iy,sp                         ;[1418] fd 39
                    ld        (iy+$01),a                    ;[141a] fd 77 01
                    ld        hl,$0006                      ;[141d] 21 06 00
                    add       hl,sp                         ;[1420] 39
                    ld        a,(iy+$00)                    ;[1421] fd 7e 00
                    add       $08                           ;[1424] c6 08
                    ld        (hl),a                        ;[1426] 77
                    ld        a,(iy+$01)                    ;[1427] fd 7e 01
                    adc       $00                           ;[142a] ce 00
                    inc       hl                            ;[142c] 23
                    ld        (hl),a                        ;[142d] 77
                    ld        hl,$0006                      ;[142e] 21 06 00
                    add       hl,sp                         ;[1431] 39
                    ld        a,(hl)                        ;[1432] 7e
                    inc       hl                            ;[1433] 23
                    ld        h,(hl)                        ;[1434] 66
                    ld        l,a                           ;[1435] 6f
                    ld        c,(hl)                        ;[1436] 4e
                    inc       hl                            ;[1437] 23
                    ld        b,(hl)                        ;[1438] 46
                    inc       hl                            ;[1439] 23
                    ld        e,(hl)                        ;[143a] 5e
                    inc       hl                            ;[143b] 23
                    ld        d,(hl)                        ;[143c] 56
                    ld        a,c                           ;[143d] 79
                    add       $fe                           ;[143e] c6 fe
                    ld        c,a                           ;[1440] 4f
                    ld        a,b                           ;[1441] 78
                    adc       $ff                           ;[1442] ce ff
                    ld        b,a                           ;[1444] 47
                    ld        a,e                           ;[1445] 7b
                    adc       $ff                           ;[1446] ce ff
                    ld        e,a                           ;[1448] 5f
                    ld        a,d                           ;[1449] 7a
                    adc       $ff                           ;[144a] ce ff
                    ld        d,a                           ;[144c] 57
                    push      de                            ;[144d] d5
                    push      bc                            ;[144e] c5
                    ld        hl,($fa1d)                    ;[144f] 2a 1d fa
                    push      hl                            ;[1452] e5
                    ld        hl,($fa1b)                    ;[1453] 2a 1b fa
                    push      hl                            ;[1456] e5
                    call      $1b41                         ;[1457] cd 41 1b
                    pop       af                            ;[145a] f1
                    pop       af                            ;[145b] f1
                    pop       af                            ;[145c] f1
                    pop       af                            ;[145d] f1
                    ld        c,l                           ;[145e] 4d
                    ld        b,h                           ;[145f] 44
                    ld        iy,$0008                      ;[1460] fd 21 08 00
                    add       iy,sp                         ;[1464] fd 39
                    ld        a,(iy+$00)                    ;[1466] fd 7e 00
                    add       c                             ;[1469] 81
                    ld        c,a                           ;[146a] 4f
                    ld        a,(iy+$01)                    ;[146b] fd 7e 01
                    adc       b                             ;[146e] 88
                    ld        b,a                           ;[146f] 47
                    ld        a,(iy+$02)                    ;[1470] fd 7e 02
                    adc       e                             ;[1473] 8b
                    ld        e,a                           ;[1474] 5f
                    ld        a,(iy+$03)                    ;[1475] fd 7e 03
                    adc       d                             ;[1478] 8a
                    ld        d,a                           ;[1479] 57
                    ld        iy,$0000                      ;[147a] fd 21 00 00
                    add       iy,sp                         ;[147e] fd 39
                    ld        (iy+$00),c                    ;[1480] fd 71 00
                    ld        (iy+$01),b                    ;[1483] fd 70 01
                    ld        (iy+$02),e                    ;[1486] fd 73 02
                    ld        (iy+$03),d                    ;[1489] fd 72 03
                    ld        hl,$0004                      ;[148c] 21 04 00
                    add       hl,sp                         ;[148f] 39
                    ld        a,(hl)                        ;[1490] 7e
                    inc       hl                            ;[1491] 23
                    ld        h,(hl)                        ;[1492] 66
                    ld        l,a                           ;[1493] 6f
                    ld        c,(hl)                        ;[1494] 4e
                    inc       hl                            ;[1495] 23
                    ld        b,(hl)                        ;[1496] 46
                    inc       hl                            ;[1497] 23
                    ld        e,(hl)                        ;[1498] 5e
                    inc       hl                            ;[1499] 23
                    ld        d,(hl)                        ;[149a] 56
                    ld        a,c                           ;[149b] 79
                    ld        iy,$fa1f                      ;[149c] fd 21 1f fa
                    and       (iy+$00)                      ;[14a0] fd a6 00
                    ld        c,a                           ;[14a3] 4f
                    ld        a,b                           ;[14a4] 78
                    and       (iy+$01)                      ;[14a5] fd a6 01
                    ld        b,a                           ;[14a8] 47
                    ld        a,e                           ;[14a9] 7b
                    and       (iy+$02)                      ;[14aa] fd a6 02
                    ld        e,a                           ;[14ad] 5f
                    ld        a,d                           ;[14ae] 7a
                    and       (iy+$03)                      ;[14af] fd a6 03
                    ld        d,a                           ;[14b2] 57
                    ld        iy,$0000                      ;[14b3] fd 21 00 00
                    add       iy,sp                         ;[14b7] fd 39
                    ld        a,(iy+$00)                    ;[14b9] fd 7e 00
                    add       c                             ;[14bc] 81
                    ld        c,a                           ;[14bd] 4f
                    ld        a,(iy+$01)                    ;[14be] fd 7e 01
                    adc       b                             ;[14c1] 88
                    ld        b,a                           ;[14c2] 47
                    ld        a,(iy+$02)                    ;[14c3] fd 7e 02
                    adc       e                             ;[14c6] 8b
                    ld        e,a                           ;[14c7] 5f
                    ld        a,(iy+$03)                    ;[14c8] fd 7e 03
                    adc       d                             ;[14cb] 8a
                    ld        d,a                           ;[14cc] 57
                    ld        hl,$0010                      ;[14cd] 21 10 00
                    add       hl,sp                         ;[14d0] 39
                    ld        a,(hl)                        ;[14d1] 7e
                    inc       hl                            ;[14d2] 23
                    ld        h,(hl)                        ;[14d3] 66
                    ld        l,a                           ;[14d4] 6f
                    push      hl                            ;[14d5] e5
                    push      de                            ;[14d6] d5
                    push      bc                            ;[14d7] c5
                    call      $04e8                         ;[14d8] cd e8 04
                    pop       af                            ;[14db] f1
                    pop       af                            ;[14dc] f1
                    pop       af                            ;[14dd] f1
                    ld        a,l                           ;[14de] 7d
                    or        a                             ;[14df] b7
                    jr        nz,$14e5                      ;[14e0] 20 03
                    ld        l,a                           ;[14e2] 6f
                    jr        $1554                         ;[14e3] 18 6f
                    ld        iy,$0004                      ;[14e5] fd 21 04 00
                    add       iy,sp                         ;[14e9] fd 39
                    ld        l,(iy+$00)                    ;[14eb] fd 6e 00
                    ld        h,(iy+$01)                    ;[14ee] fd 66 01
                    ld        c,(hl)                        ;[14f1] 4e
                    inc       hl                            ;[14f2] 23
                    ld        b,(hl)                        ;[14f3] 46
                    inc       hl                            ;[14f4] 23
                    ld        e,(hl)                        ;[14f5] 5e
                    inc       hl                            ;[14f6] 23
                    ld        d,(hl)                        ;[14f7] 56
                    inc       c                             ;[14f8] 0c
                    jr        nz,$1502                      ;[14f9] 20 07
                    inc       b                             ;[14fb] 04
                    jr        nz,$1502                      ;[14fc] 20 04
                    inc       e                             ;[14fe] 1c
                    jr        nz,$1502                      ;[14ff] 20 01
                    inc       d                             ;[1501] 14
                    ld        l,(iy+$00)                    ;[1502] fd 6e 00
                    ld        h,(iy+$01)                    ;[1505] fd 66 01
                    ld        (hl),c                        ;[1508] 71
                    inc       hl                            ;[1509] 23
                    ld        (hl),b                        ;[150a] 70
                    inc       hl                            ;[150b] 23
                    ld        (hl),e                        ;[150c] 73
                    inc       hl                            ;[150d] 23
                    ld        (hl),d                        ;[150e] 72
                    ld        a,c                           ;[150f] 79
                    ld        iy,$fa1f                      ;[1510] fd 21 1f fa
                    and       (iy+$00)                      ;[1514] fd a6 00
                    ld        c,a                           ;[1517] 4f
                    ld        a,b                           ;[1518] 78
                    and       (iy+$01)                      ;[1519] fd a6 01
                    ld        b,a                           ;[151c] 47
                    ld        a,e                           ;[151d] 7b
                    and       (iy+$02)                      ;[151e] fd a6 02
                    ld        e,a                           ;[1521] 5f
                    ld        a,d                           ;[1522] 7a
                    and       (iy+$03)                      ;[1523] fd a6 03
                    or        e                             ;[1526] b3
                    or        b                             ;[1527] b0
                    or        c                             ;[1528] b1
                    jr        nz,$1552                      ;[1529] 20 27
                    ld        hl,$0006                      ;[152b] 21 06 00
                    add       hl,sp                         ;[152e] 39
                    ld        a,(hl)                        ;[152f] 7e
                    inc       hl                            ;[1530] 23
                    ld        h,(hl)                        ;[1531] 66
                    ld        l,a                           ;[1532] 6f
                    ld        c,(hl)                        ;[1533] 4e
                    inc       hl                            ;[1534] 23
                    ld        b,(hl)                        ;[1535] 46
                    inc       hl                            ;[1536] 23
                    ld        e,(hl)                        ;[1537] 5e
                    inc       hl                            ;[1538] 23
                    ld        d,(hl)                        ;[1539] 56
                    push      de                            ;[153a] d5
                    push      bc                            ;[153b] c5
                    call      $05bb                         ;[153c] cd bb 05
                    pop       af                            ;[153f] f1
                    pop       af                            ;[1540] f1
                    ld        c,l                           ;[1541] 4d
                    ld        b,h                           ;[1542] 44
                    ld        hl,$0006                      ;[1543] 21 06 00
                    add       hl,sp                         ;[1546] 39
                    ld        a,(hl)                        ;[1547] 7e
                    inc       hl                            ;[1548] 23
                    ld        h,(hl)                        ;[1549] 66
                    ld        l,a                           ;[154a] 6f
                    ld        (hl),c                        ;[154b] 71
                    inc       hl                            ;[154c] 23
                    ld        (hl),b                        ;[154d] 70
                    inc       hl                            ;[154e] 23
                    ld        (hl),e                        ;[154f] 73
                    inc       hl                            ;[1550] 23
                    ld        (hl),d                        ;[1551] 72
                    ld        l,$01                         ;[1552] 2e 01
                    ld        iy,$000c                      ;[1554] fd 21 0c 00
                    add       iy,sp                         ;[1558] fd 39
                    ld        sp,iy                         ;[155a] fd f9
                    ret                                     ;[155c] c9

                    ld        a,$40                         ;[155d] 3e 40
                    ld        bc,$243b                      ;[155f] 01 3b 24
                    out       (c),a                         ;[1562] ed 79
                    ld        a,$00                         ;[1564] 3e 00
                    ld        bc,$253b                      ;[1566] 01 3b 25
                    out       (c),a                         ;[1569] ed 79
                    ld        a,$41                         ;[156b] 3e 41
                    ld        bc,$243b                      ;[156d] 01 3b 24
                    out       (c),a                         ;[1570] ed 79
                    ld        bc,$0000                      ;[1572] 01 00 00
                    push      bc                            ;[1575] c5
                    ld        a,c                           ;[1576] 79
                    ld        bc,$253b                      ;[1577] 01 3b 25
                    out       (c),a                         ;[157a] ed 79
                    pop       bc                            ;[157c] c1
                    inc       bc                            ;[157d] 03
                    ld        a,b                           ;[157e] 78
                    sub       $01                           ;[157f] d6 01
                    jr        c,$1575                       ;[1581] 38 f2
                    ret                                     ;[1583] c9

                    nop                                     ;[1584] 00
                    nop                                     ;[1585] 00
                    nop                                     ;[1586] 00
                    nop                                     ;[1587] 00
                    nop                                     ;[1588] 00
                    nop                                     ;[1589] 00
                    nop                                     ;[158a] 00
                    nop                                     ;[158b] 00
                    nop                                     ;[158c] 00
                    djnz      $159f                         ;[158d] 10 10
                    djnz      $15a1                         ;[158f] 10 10
                    nop                                     ;[1591] 00
                    djnz      $1594                         ;[1592] 10 00
                    nop                                     ;[1594] 00
                    inc       h                             ;[1595] 24
                    inc       h                             ;[1596] 24
                    nop                                     ;[1597] 00
                    nop                                     ;[1598] 00
                    nop                                     ;[1599] 00
                    nop                                     ;[159a] 00
                    nop                                     ;[159b] 00
                    nop                                     ;[159c] 00
                    inc       h                             ;[159d] 24
                    ld        a,(hl)                        ;[159e] 7e
                    inc       h                             ;[159f] 24
                    inc       h                             ;[15a0] 24
                    ld        a,(hl)                        ;[15a1] 7e
                    inc       h                             ;[15a2] 24
                    nop                                     ;[15a3] 00
                    nop                                     ;[15a4] 00
                    ex        af,af'                        ;[15a5] 08
                    ld        a,$28                         ;[15a6] 3e 28
                    ld        a,$0a                         ;[15a8] 3e 0a
                    ld        a,$08                         ;[15aa] 3e 08
                    nop                                     ;[15ac] 00
                    ld        h,d                           ;[15ad] 62
                    ld        h,h                           ;[15ae] 64
                    ex        af,af'                        ;[15af] 08
                    djnz      $15d8                         ;[15b0] 10 26
                    ld        b,(hl)                        ;[15b2] 46
                    nop                                     ;[15b3] 00
                    nop                                     ;[15b4] 00
                    djnz      $15df                         ;[15b5] 10 28
                    djnz      $15e3                         ;[15b7] 10 2a
                    ld        b,h                           ;[15b9] 44
                    ld        a,($0000)                     ;[15ba] 3a 00 00
                    ex        af,af'                        ;[15bd] 08
                    djnz      $15c0                         ;[15be] 10 00
                    nop                                     ;[15c0] 00
                    nop                                     ;[15c1] 00
                    nop                                     ;[15c2] 00
                    nop                                     ;[15c3] 00
                    nop                                     ;[15c4] 00
                    inc       b                             ;[15c5] 04
                    ex        af,af'                        ;[15c6] 08
                    ex        af,af'                        ;[15c7] 08
                    ex        af,af'                        ;[15c8] 08
                    ex        af,af'                        ;[15c9] 08
                    inc       b                             ;[15ca] 04
                    nop                                     ;[15cb] 00
                    nop                                     ;[15cc] 00
                    jr        nz,$15df                      ;[15cd] 20 10
                    djnz      $15e1                         ;[15cf] 10 10
                    djnz      $15f3                         ;[15d1] 10 20
                    nop                                     ;[15d3] 00
                    nop                                     ;[15d4] 00
                    nop                                     ;[15d5] 00
                    inc       d                             ;[15d6] 14
                    ex        af,af'                        ;[15d7] 08
                    ld        a,$08                         ;[15d8] 3e 08
                    inc       d                             ;[15da] 14
                    nop                                     ;[15db] 00
                    nop                                     ;[15dc] 00
                    nop                                     ;[15dd] 00
                    ex        af,af'                        ;[15de] 08
                    ex        af,af'                        ;[15df] 08
                    ld        a,$08                         ;[15e0] 3e 08
                    ex        af,af'                        ;[15e2] 08
                    nop                                     ;[15e3] 00
                    nop                                     ;[15e4] 00
                    nop                                     ;[15e5] 00
                    nop                                     ;[15e6] 00
                    nop                                     ;[15e7] 00
                    nop                                     ;[15e8] 00
                    ex        af,af'                        ;[15e9] 08
                    ex        af,af'                        ;[15ea] 08
                    djnz      $15ed                         ;[15eb] 10 00
                    nop                                     ;[15ed] 00
                    nop                                     ;[15ee] 00
                    nop                                     ;[15ef] 00
                    ld        a,$00                         ;[15f0] 3e 00
                    nop                                     ;[15f2] 00
                    nop                                     ;[15f3] 00
                    nop                                     ;[15f4] 00
                    nop                                     ;[15f5] 00
                    nop                                     ;[15f6] 00
                    nop                                     ;[15f7] 00
                    nop                                     ;[15f8] 00
                    jr        $1613                         ;[15f9] 18 18
                    nop                                     ;[15fb] 00
                    nop                                     ;[15fc] 00
                    nop                                     ;[15fd] 00
                    ld        (bc),a                        ;[15fe] 02
                    inc       b                             ;[15ff] 04
                    ex        af,af'                        ;[1600] 08
                    djnz      $1623                         ;[1601] 10 20
                    nop                                     ;[1603] 00
                    nop                                     ;[1604] 00
                    inc       a                             ;[1605] 3c
                    ld        b,(hl)                        ;[1606] 46
                    ld        c,d                           ;[1607] 4a
                    ld        d,d                           ;[1608] 52
                    ld        h,d                           ;[1609] 62
                    inc       a                             ;[160a] 3c
                    nop                                     ;[160b] 00
                    nop                                     ;[160c] 00
                    jr        $1637                         ;[160d] 18 28
                    ex        af,af'                        ;[160f] 08
                    ex        af,af'                        ;[1610] 08
                    ex        af,af'                        ;[1611] 08
                    ld        a,$00                         ;[1612] 3e 00
                    nop                                     ;[1614] 00
                    inc       a                             ;[1615] 3c
                    ld        b,d                           ;[1616] 42
                    ld        (bc),a                        ;[1617] 02
                    inc       a                             ;[1618] 3c
                    ld        b,b                           ;[1619] 40
                    ld        a,(hl)                        ;[161a] 7e
                    nop                                     ;[161b] 00
                    nop                                     ;[161c] 00
                    inc       a                             ;[161d] 3c
                    ld        b,d                           ;[161e] 42
                    inc       c                             ;[161f] 0c
                    ld        (bc),a                        ;[1620] 02
                    ld        b,d                           ;[1621] 42
                    inc       a                             ;[1622] 3c
                    nop                                     ;[1623] 00
                    nop                                     ;[1624] 00
                    ex        af,af'                        ;[1625] 08
                    jr        $1650                         ;[1626] 18 28
                    ld        c,b                           ;[1628] 48
                    ld        a,(hl)                        ;[1629] 7e
                    ex        af,af'                        ;[162a] 08
                    nop                                     ;[162b] 00
                    nop                                     ;[162c] 00
                    ld        a,(hl)                        ;[162d] 7e
                    ld        b,b                           ;[162e] 40
                    ld        a,h                           ;[162f] 7c
                    ld        (bc),a                        ;[1630] 02
                    ld        b,d                           ;[1631] 42
                    inc       a                             ;[1632] 3c
                    nop                                     ;[1633] 00
                    nop                                     ;[1634] 00
                    inc       a                             ;[1635] 3c
                    ld        b,b                           ;[1636] 40
                    ld        a,h                           ;[1637] 7c
                    ld        b,d                           ;[1638] 42
                    ld        b,d                           ;[1639] 42
                    inc       a                             ;[163a] 3c
                    nop                                     ;[163b] 00
                    nop                                     ;[163c] 00
                    ld        a,(hl)                        ;[163d] 7e
                    ld        (bc),a                        ;[163e] 02
                    inc       b                             ;[163f] 04
                    ex        af,af'                        ;[1640] 08
                    djnz      $1653                         ;[1641] 10 10
                    nop                                     ;[1643] 00
                    nop                                     ;[1644] 00
                    inc       a                             ;[1645] 3c
                    ld        b,d                           ;[1646] 42
                    inc       a                             ;[1647] 3c
                    ld        b,d                           ;[1648] 42
                    ld        b,d                           ;[1649] 42
                    inc       a                             ;[164a] 3c
                    nop                                     ;[164b] 00
                    nop                                     ;[164c] 00
                    inc       a                             ;[164d] 3c
                    ld        b,d                           ;[164e] 42
                    ld        b,d                           ;[164f] 42
                    ld        a,$02                         ;[1650] 3e 02
                    inc       a                             ;[1652] 3c
                    nop                                     ;[1653] 00
                    nop                                     ;[1654] 00
                    nop                                     ;[1655] 00
                    nop                                     ;[1656] 00
                    djnz      $1659                         ;[1657] 10 00
                    nop                                     ;[1659] 00
                    djnz      $165c                         ;[165a] 10 00
                    nop                                     ;[165c] 00
                    nop                                     ;[165d] 00
                    djnz      $1660                         ;[165e] 10 00
                    nop                                     ;[1660] 00
                    djnz      $1673                         ;[1661] 10 10
                    jr        nz,$1665                      ;[1663] 20 00
                    nop                                     ;[1665] 00
                    inc       b                             ;[1666] 04
                    ex        af,af'                        ;[1667] 08
                    djnz      $1672                         ;[1668] 10 08
                    inc       b                             ;[166a] 04
                    nop                                     ;[166b] 00
                    nop                                     ;[166c] 00
                    nop                                     ;[166d] 00
                    nop                                     ;[166e] 00
                    ld        a,$00                         ;[166f] 3e 00
                    ld        a,$00                         ;[1671] 3e 00
                    nop                                     ;[1673] 00
                    nop                                     ;[1674] 00
                    nop                                     ;[1675] 00
                    jr        nz,$1688                      ;[1676] 20 10
                    ex        af,af'                        ;[1678] 08
                    djnz      $169b                         ;[1679] 10 20
                    nop                                     ;[167b] 00
                    nop                                     ;[167c] 00
                    inc       a                             ;[167d] 3c
                    ld        b,d                           ;[167e] 42
                    inc       b                             ;[167f] 04
                    ex        af,af'                        ;[1680] 08
                    nop                                     ;[1681] 00
                    ex        af,af'                        ;[1682] 08
                    nop                                     ;[1683] 00
                    nop                                     ;[1684] 00
                    inc       a                             ;[1685] 3c
                    ld        c,d                           ;[1686] 4a
                    ld        d,(hl)                        ;[1687] 56
                    ld        e,(hl)                        ;[1688] 5e
                    ld        b,b                           ;[1689] 40
                    inc       a                             ;[168a] 3c
                    nop                                     ;[168b] 00
                    nop                                     ;[168c] 00
                    inc       a                             ;[168d] 3c
                    ld        b,d                           ;[168e] 42
                    ld        b,d                           ;[168f] 42
                    ld        a,(hl)                        ;[1690] 7e
                    ld        b,d                           ;[1691] 42
                    ld        b,d                           ;[1692] 42
                    nop                                     ;[1693] 00
                    nop                                     ;[1694] 00
                    ld        a,h                           ;[1695] 7c
                    ld        b,d                           ;[1696] 42
                    ld        a,h                           ;[1697] 7c
                    ld        b,d                           ;[1698] 42
                    ld        b,d                           ;[1699] 42
                    ld        a,h                           ;[169a] 7c
                    nop                                     ;[169b] 00
                    nop                                     ;[169c] 00
                    inc       a                             ;[169d] 3c
                    ld        b,d                           ;[169e] 42
                    ld        b,b                           ;[169f] 40
                    ld        b,b                           ;[16a0] 40
                    ld        b,d                           ;[16a1] 42
                    inc       a                             ;[16a2] 3c
                    nop                                     ;[16a3] 00
                    nop                                     ;[16a4] 00
                    ld        a,b                           ;[16a5] 78
                    ld        b,h                           ;[16a6] 44
                    ld        b,d                           ;[16a7] 42
                    ld        b,d                           ;[16a8] 42
                    ld        b,h                           ;[16a9] 44
                    ld        a,b                           ;[16aa] 78
                    nop                                     ;[16ab] 00
                    nop                                     ;[16ac] 00
                    ld        a,(hl)                        ;[16ad] 7e
                    ld        b,b                           ;[16ae] 40
                    ld        a,h                           ;[16af] 7c
                    ld        b,b                           ;[16b0] 40
                    ld        b,b                           ;[16b1] 40
                    ld        a,(hl)                        ;[16b2] 7e
                    nop                                     ;[16b3] 00
                    nop                                     ;[16b4] 00
                    ld        a,(hl)                        ;[16b5] 7e
                    ld        b,b                           ;[16b6] 40
                    ld        a,h                           ;[16b7] 7c
                    ld        b,b                           ;[16b8] 40
                    ld        b,b                           ;[16b9] 40
                    ld        b,b                           ;[16ba] 40
                    nop                                     ;[16bb] 00
                    nop                                     ;[16bc] 00
                    inc       a                             ;[16bd] 3c
                    ld        b,d                           ;[16be] 42
                    ld        b,b                           ;[16bf] 40
                    ld        c,(hl)                        ;[16c0] 4e
                    ld        b,d                           ;[16c1] 42
                    inc       a                             ;[16c2] 3c
                    nop                                     ;[16c3] 00
                    nop                                     ;[16c4] 00
                    ld        b,d                           ;[16c5] 42
                    ld        b,d                           ;[16c6] 42
                    ld        a,(hl)                        ;[16c7] 7e
                    ld        b,d                           ;[16c8] 42
                    ld        b,d                           ;[16c9] 42
                    ld        b,d                           ;[16ca] 42
                    nop                                     ;[16cb] 00
                    nop                                     ;[16cc] 00
                    ld        a,$08                         ;[16cd] 3e 08
                    ex        af,af'                        ;[16cf] 08
                    ex        af,af'                        ;[16d0] 08
                    ex        af,af'                        ;[16d1] 08
                    ld        a,$00                         ;[16d2] 3e 00
                    nop                                     ;[16d4] 00
                    ld        (bc),a                        ;[16d5] 02
                    ld        (bc),a                        ;[16d6] 02
                    ld        (bc),a                        ;[16d7] 02
                    ld        b,d                           ;[16d8] 42
                    ld        b,d                           ;[16d9] 42
                    inc       a                             ;[16da] 3c
                    nop                                     ;[16db] 00
                    nop                                     ;[16dc] 00
                    ld        b,h                           ;[16dd] 44
                    ld        c,b                           ;[16de] 48
                    ld        (hl),b                        ;[16df] 70
                    ld        c,b                           ;[16e0] 48
                    ld        b,h                           ;[16e1] 44
                    ld        b,d                           ;[16e2] 42
                    nop                                     ;[16e3] 00
                    nop                                     ;[16e4] 00
                    ld        b,b                           ;[16e5] 40
                    ld        b,b                           ;[16e6] 40
                    ld        b,b                           ;[16e7] 40
                    ld        b,b                           ;[16e8] 40
                    ld        b,b                           ;[16e9] 40
                    ld        a,(hl)                        ;[16ea] 7e
                    nop                                     ;[16eb] 00
                    nop                                     ;[16ec] 00
                    ld        b,d                           ;[16ed] 42
                    ld        h,(hl)                        ;[16ee] 66
                    ld        e,d                           ;[16ef] 5a
                    ld        b,d                           ;[16f0] 42
                    ld        b,d                           ;[16f1] 42
                    ld        b,d                           ;[16f2] 42
                    nop                                     ;[16f3] 00
                    nop                                     ;[16f4] 00
                    ld        b,d                           ;[16f5] 42
                    ld        h,d                           ;[16f6] 62
                    ld        d,d                           ;[16f7] 52
                    ld        c,d                           ;[16f8] 4a
                    ld        b,(hl)                        ;[16f9] 46
                    ld        b,d                           ;[16fa] 42
                    nop                                     ;[16fb] 00
                    nop                                     ;[16fc] 00
                    inc       a                             ;[16fd] 3c
                    ld        b,d                           ;[16fe] 42
                    ld        b,d                           ;[16ff] 42
                    ld        b,d                           ;[1700] 42
                    ld        b,d                           ;[1701] 42
                    inc       a                             ;[1702] 3c
                    nop                                     ;[1703] 00
                    nop                                     ;[1704] 00
                    ld        a,h                           ;[1705] 7c
                    ld        b,d                           ;[1706] 42
                    ld        b,d                           ;[1707] 42
                    ld        a,h                           ;[1708] 7c
                    ld        b,b                           ;[1709] 40
                    ld        b,b                           ;[170a] 40
                    nop                                     ;[170b] 00
                    nop                                     ;[170c] 00
                    inc       a                             ;[170d] 3c
                    ld        b,d                           ;[170e] 42
                    ld        b,d                           ;[170f] 42
                    ld        d,d                           ;[1710] 52
                    ld        c,d                           ;[1711] 4a
                    inc       a                             ;[1712] 3c
                    nop                                     ;[1713] 00
                    nop                                     ;[1714] 00
                    ld        a,h                           ;[1715] 7c
                    ld        b,d                           ;[1716] 42
                    ld        b,d                           ;[1717] 42
                    ld        a,h                           ;[1718] 7c
                    ld        b,h                           ;[1719] 44
                    ld        b,d                           ;[171a] 42
                    nop                                     ;[171b] 00
                    nop                                     ;[171c] 00
                    inc       a                             ;[171d] 3c
                    ld        b,b                           ;[171e] 40
                    inc       a                             ;[171f] 3c
                    ld        (bc),a                        ;[1720] 02
                    ld        b,d                           ;[1721] 42
                    inc       a                             ;[1722] 3c
                    nop                                     ;[1723] 00
                    nop                                     ;[1724] 00
                    cp        $10                           ;[1725] fe 10
                    djnz      $1739                         ;[1727] 10 10
                    djnz      $173b                         ;[1729] 10 10
                    nop                                     ;[172b] 00
                    nop                                     ;[172c] 00
                    ld        b,d                           ;[172d] 42
                    ld        b,d                           ;[172e] 42
                    ld        b,d                           ;[172f] 42
                    ld        b,d                           ;[1730] 42
                    ld        b,d                           ;[1731] 42
                    inc       a                             ;[1732] 3c
                    nop                                     ;[1733] 00
                    nop                                     ;[1734] 00
                    ld        b,d                           ;[1735] 42
                    ld        b,d                           ;[1736] 42
                    ld        b,d                           ;[1737] 42
                    ld        b,d                           ;[1738] 42
                    inc       h                             ;[1739] 24
                    jr        $173c                         ;[173a] 18 00
                    nop                                     ;[173c] 00
                    ld        b,d                           ;[173d] 42
                    ld        b,d                           ;[173e] 42
                    ld        b,d                           ;[173f] 42
                    ld        b,d                           ;[1740] 42
                    ld        e,d                           ;[1741] 5a
                    inc       h                             ;[1742] 24
                    nop                                     ;[1743] 00
                    nop                                     ;[1744] 00
                    ld        b,d                           ;[1745] 42
                    inc       h                             ;[1746] 24
                    jr        $1761                         ;[1747] 18 18
                    inc       h                             ;[1749] 24
                    ld        b,d                           ;[174a] 42
                    nop                                     ;[174b] 00
                    nop                                     ;[174c] 00
                    add       d                             ;[174d] 82
                    ld        b,h                           ;[174e] 44
                    jr        z,$1761                       ;[174f] 28 10
                    djnz      $1763                         ;[1751] 10 10
                    nop                                     ;[1753] 00
                    nop                                     ;[1754] 00
                    ld        a,(hl)                        ;[1755] 7e
                    inc       b                             ;[1756] 04
                    ex        af,af'                        ;[1757] 08
                    djnz      $177a                         ;[1758] 10 20
                    ld        a,(hl)                        ;[175a] 7e
                    nop                                     ;[175b] 00
                    nop                                     ;[175c] 00
                    ld        c,$08                         ;[175d] 0e 08
                    ex        af,af'                        ;[175f] 08
                    ex        af,af'                        ;[1760] 08
                    ex        af,af'                        ;[1761] 08
                    ld        c,$00                         ;[1762] 0e 00
                    nop                                     ;[1764] 00
                    nop                                     ;[1765] 00
                    ld        b,b                           ;[1766] 40
                    jr        nz,$1779                      ;[1767] 20 10
                    ex        af,af'                        ;[1769] 08
                    inc       b                             ;[176a] 04
                    nop                                     ;[176b] 00
                    nop                                     ;[176c] 00
                    ld        (hl),b                        ;[176d] 70
                    djnz      $1780                         ;[176e] 10 10
                    djnz      $1782                         ;[1770] 10 10
                    ld        (hl),b                        ;[1772] 70
                    nop                                     ;[1773] 00
                    nop                                     ;[1774] 00
                    djnz      $17af                         ;[1775] 10 38
                    ld        d,h                           ;[1777] 54
                    djnz      $178a                         ;[1778] 10 10
                    djnz      $177c                         ;[177a] 10 00
                    nop                                     ;[177c] 00
                    nop                                     ;[177d] 00
                    nop                                     ;[177e] 00
                    nop                                     ;[177f] 00
                    nop                                     ;[1780] 00
                    nop                                     ;[1781] 00
                    nop                                     ;[1782] 00
                    rst       $38                           ;[1783] ff
                    nop                                     ;[1784] 00
                    inc       e                             ;[1785] 1c
                    ld        ($2078),hl                    ;[1786] 22 78 20
                    jr        nz,$1809                      ;[1789] 20 7e
                    nop                                     ;[178b] 00
                    nop                                     ;[178c] 00
                    nop                                     ;[178d] 00
                    jr        c,$1794                       ;[178e] 38 04
                    inc       a                             ;[1790] 3c
                    ld        b,h                           ;[1791] 44
                    inc       a                             ;[1792] 3c
                    nop                                     ;[1793] 00
                    nop                                     ;[1794] 00
                    jr        nz,$17b7                      ;[1795] 20 20
                    inc       a                             ;[1797] 3c
                    ld        ($3c22),hl                    ;[1798] 22 22 3c
                    nop                                     ;[179b] 00
                    nop                                     ;[179c] 00
                    nop                                     ;[179d] 00
                    inc       e                             ;[179e] 1c
                    jr        nz,$17c1                      ;[179f] 20 20
                    jr        nz,$17bf                      ;[17a1] 20 1c
                    nop                                     ;[17a3] 00
                    nop                                     ;[17a4] 00
                    inc       b                             ;[17a5] 04
                    inc       b                             ;[17a6] 04
                    inc       a                             ;[17a7] 3c
                    ld        b,h                           ;[17a8] 44
                    ld        b,h                           ;[17a9] 44
                    inc       a                             ;[17aa] 3c
                    nop                                     ;[17ab] 00
                    nop                                     ;[17ac] 00
                    nop                                     ;[17ad] 00
                    jr        c,$17f4                       ;[17ae] 38 44
                    ld        a,b                           ;[17b0] 78
                    ld        b,b                           ;[17b1] 40
                    inc       a                             ;[17b2] 3c
                    nop                                     ;[17b3] 00
                    nop                                     ;[17b4] 00
                    inc       c                             ;[17b5] 0c
                    djnz      $17d0                         ;[17b6] 10 18
                    djnz      $17ca                         ;[17b8] 10 10
                    djnz      $17bc                         ;[17ba] 10 00
                    nop                                     ;[17bc] 00
                    nop                                     ;[17bd] 00
                    inc       a                             ;[17be] 3c
                    ld        b,h                           ;[17bf] 44
                    ld        b,h                           ;[17c0] 44
                    inc       a                             ;[17c1] 3c
                    inc       b                             ;[17c2] 04
                    jr        c,$17c5                       ;[17c3] 38 00
                    ld        b,b                           ;[17c5] 40
                    ld        b,b                           ;[17c6] 40
                    ld        a,b                           ;[17c7] 78
                    ld        b,h                           ;[17c8] 44
                    ld        b,h                           ;[17c9] 44
                    ld        b,h                           ;[17ca] 44
                    nop                                     ;[17cb] 00
                    nop                                     ;[17cc] 00
                    djnz      $17cf                         ;[17cd] 10 00
                    jr        nc,$17e1                      ;[17cf] 30 10
                    djnz      $180b                         ;[17d1] 10 38
                    nop                                     ;[17d3] 00
                    nop                                     ;[17d4] 00
                    inc       b                             ;[17d5] 04
                    nop                                     ;[17d6] 00
                    inc       b                             ;[17d7] 04
                    inc       b                             ;[17d8] 04
                    inc       b                             ;[17d9] 04
                    inc       h                             ;[17da] 24
                    jr        $17dd                         ;[17db] 18 00
                    jr        nz,$1807                      ;[17dd] 20 28
                    jr        nc,$1811                      ;[17df] 30 30
                    jr        z,$1807                       ;[17e1] 28 24
                    nop                                     ;[17e3] 00
                    nop                                     ;[17e4] 00
                    djnz      $17f7                         ;[17e5] 10 10
                    djnz      $17f9                         ;[17e7] 10 10
                    djnz      $17f7                         ;[17e9] 10 0c
                    nop                                     ;[17eb] 00
                    nop                                     ;[17ec] 00
                    nop                                     ;[17ed] 00
                    ld        l,b                           ;[17ee] 68
                    ld        d,h                           ;[17ef] 54
                    ld        d,h                           ;[17f0] 54
                    ld        d,h                           ;[17f1] 54
                    ld        d,h                           ;[17f2] 54
                    nop                                     ;[17f3] 00
                    nop                                     ;[17f4] 00
                    nop                                     ;[17f5] 00
                    ld        a,b                           ;[17f6] 78
                    ld        b,h                           ;[17f7] 44
                    ld        b,h                           ;[17f8] 44
                    ld        b,h                           ;[17f9] 44
                    ld        b,h                           ;[17fa] 44
                    nop                                     ;[17fb] 00
                    nop                                     ;[17fc] 00
                    nop                                     ;[17fd] 00
                    jr        c,$1844                       ;[17fe] 38 44
                    ld        b,h                           ;[1800] 44
                    ld        b,h                           ;[1801] 44
                    jr        c,$1804                       ;[1802] 38 00
                    nop                                     ;[1804] 00
                    nop                                     ;[1805] 00
                    ld        a,b                           ;[1806] 78
                    ld        b,h                           ;[1807] 44
                    ld        b,h                           ;[1808] 44
                    ld        a,b                           ;[1809] 78
                    ld        b,b                           ;[180a] 40
                    ld        b,b                           ;[180b] 40
                    nop                                     ;[180c] 00
                    nop                                     ;[180d] 00
                    inc       a                             ;[180e] 3c
                    ld        b,h                           ;[180f] 44
                    ld        b,h                           ;[1810] 44
                    inc       a                             ;[1811] 3c
                    inc       b                             ;[1812] 04
                    ld        b,$00                         ;[1813] 06 00
                    nop                                     ;[1815] 00
                    inc       e                             ;[1816] 1c
                    jr        nz,$1839                      ;[1817] 20 20
                    jr        nz,$183b                      ;[1819] 20 20
                    nop                                     ;[181b] 00
                    nop                                     ;[181c] 00
                    nop                                     ;[181d] 00
                    jr        c,$1860                       ;[181e] 38 40
                    jr        c,$1826                       ;[1820] 38 04
                    ld        a,b                           ;[1822] 78
                    nop                                     ;[1823] 00
                    nop                                     ;[1824] 00
                    djnz      $185f                         ;[1825] 10 38
                    djnz      $1839                         ;[1827] 10 10
                    djnz      $1837                         ;[1829] 10 0c
                    nop                                     ;[182b] 00
                    nop                                     ;[182c] 00
                    nop                                     ;[182d] 00
                    ld        b,h                           ;[182e] 44
                    ld        b,h                           ;[182f] 44
                    ld        b,h                           ;[1830] 44
                    ld        b,h                           ;[1831] 44
                    jr        c,$1834                       ;[1832] 38 00
                    nop                                     ;[1834] 00
                    nop                                     ;[1835] 00
                    ld        b,h                           ;[1836] 44
                    ld        b,h                           ;[1837] 44
                    jr        z,$1862                       ;[1838] 28 28
                    djnz      $183c                         ;[183a] 10 00
                    nop                                     ;[183c] 00
                    nop                                     ;[183d] 00
                    ld        b,h                           ;[183e] 44
                    ld        d,h                           ;[183f] 54
                    ld        d,h                           ;[1840] 54
                    ld        d,h                           ;[1841] 54
                    jr        z,$1844                       ;[1842] 28 00
                    nop                                     ;[1844] 00
                    nop                                     ;[1845] 00
                    ld        b,h                           ;[1846] 44
                    jr        z,$1859                       ;[1847] 28 10
                    jr        z,$188f                       ;[1849] 28 44
                    nop                                     ;[184b] 00
                    nop                                     ;[184c] 00
                    nop                                     ;[184d] 00
                    ld        b,h                           ;[184e] 44
                    ld        b,h                           ;[184f] 44
                    ld        b,h                           ;[1850] 44
                    inc       a                             ;[1851] 3c
                    inc       b                             ;[1852] 04
                    jr        c,$1855                       ;[1853] 38 00
                    nop                                     ;[1855] 00
                    ld        a,h                           ;[1856] 7c
                    ex        af,af'                        ;[1857] 08
                    djnz      $187a                         ;[1858] 10 20
                    ld        a,h                           ;[185a] 7c
                    nop                                     ;[185b] 00
                    nop                                     ;[185c] 00
                    ld        c,$08                         ;[185d] 0e 08
                    jr        nc,$1869                      ;[185f] 30 08
                    ex        af,af'                        ;[1861] 08
                    ld        c,$00                         ;[1862] 0e 00
                    nop                                     ;[1864] 00
                    ex        af,af'                        ;[1865] 08
                    ex        af,af'                        ;[1866] 08
                    ex        af,af'                        ;[1867] 08
                    ex        af,af'                        ;[1868] 08
                    ex        af,af'                        ;[1869] 08
                    ex        af,af'                        ;[186a] 08
                    nop                                     ;[186b] 00
                    nop                                     ;[186c] 00
                    ld        (hl),b                        ;[186d] 70
                    djnz      $187c                         ;[186e] 10 0c
                    djnz      $1882                         ;[1870] 10 10
                    ld        (hl),b                        ;[1872] 70
                    nop                                     ;[1873] 00
                    nop                                     ;[1874] 00
                    inc       d                             ;[1875] 14
                    jr        z,$1878                       ;[1876] 28 00
                    nop                                     ;[1878] 00
                    nop                                     ;[1879] 00
                    nop                                     ;[187a] 00
                    nop                                     ;[187b] 00
                    inc       a                             ;[187c] 3c
                    ld        b,d                           ;[187d] 42
                    sbc       c                             ;[187e] 99
                    and       c                             ;[187f] a1
                    and       c                             ;[1880] a1
                    sbc       c                             ;[1881] 99
                    ld        b,d                           ;[1882] 42
                    inc       a                             ;[1883] 3c
                    ld        a,$40                         ;[1884] 3e 40
                    ld        bc,$243b                      ;[1886] 01 3b 24
                    out       (c),a                         ;[1889] ed 79
                    ld        a,$00                         ;[188b] 3e 00
                    ld        bc,$253b                      ;[188d] 01 3b 25
                    out       (c),a                         ;[1890] ed 79
                    ld        a,$41                         ;[1892] 3e 41
                    ld        bc,$243b                      ;[1894] 01 3b 24
                    out       (c),a                         ;[1897] ed 79
                    ld        bc,$0000                      ;[1899] 01 00 00
                    push      bc                            ;[189c] c5
                    ld        a,$00                         ;[189d] 3e 00
                    ld        bc,$253b                      ;[189f] 01 3b 25
                    out       (c),a                         ;[18a2] ed 79
                    ld        a,$02                         ;[18a4] 3e 02
                    ld        bc,$253b                      ;[18a6] 01 3b 25
                    out       (c),a                         ;[18a9] ed 79
                    ld        a,$a0                         ;[18ab] 3e a0
                    ld        bc,$253b                      ;[18ad] 01 3b 25
                    out       (c),a                         ;[18b0] ed 79
                    ld        a,$a2                         ;[18b2] 3e a2
                    ld        bc,$253b                      ;[18b4] 01 3b 25
                    out       (c),a                         ;[18b7] ed 79
                    ld        a,$14                         ;[18b9] 3e 14
                    ld        bc,$253b                      ;[18bb] 01 3b 25
                    out       (c),a                         ;[18be] ed 79
                    ld        a,$16                         ;[18c0] 3e 16
                    ld        bc,$253b                      ;[18c2] 01 3b 25
                    out       (c),a                         ;[18c5] ed 79
                    ld        a,$b4                         ;[18c7] 3e b4
                    ld        bc,$253b                      ;[18c9] 01 3b 25
                    out       (c),a                         ;[18cc] ed 79
                    ld        a,$b6                         ;[18ce] 3e b6
                    ld        bc,$253b                      ;[18d0] 01 3b 25
                    out       (c),a                         ;[18d3] ed 79
                    ld        a,$00                         ;[18d5] 3e 00
                    ld        bc,$253b                      ;[18d7] 01 3b 25
                    out       (c),a                         ;[18da] ed 79
                    ld        a,$03                         ;[18dc] 3e 03
                    ld        bc,$253b                      ;[18de] 01 3b 25
                    out       (c),a                         ;[18e1] ed 79
                    ld        a,$e0                         ;[18e3] 3e e0
                    ld        bc,$253b                      ;[18e5] 01 3b 25
                    out       (c),a                         ;[18e8] ed 79
                    ld        a,$e7                         ;[18ea] 3e e7
                    ld        bc,$253b                      ;[18ec] 01 3b 25
                    out       (c),a                         ;[18ef] ed 79
                    ld        a,$1c                         ;[18f1] 3e 1c
                    ld        bc,$253b                      ;[18f3] 01 3b 25
                    out       (c),a                         ;[18f6] ed 79
                    ld        a,$1f                         ;[18f8] 3e 1f
                    ld        bc,$253b                      ;[18fa] 01 3b 25
                    out       (c),a                         ;[18fd] ed 79
                    ld        a,$fc                         ;[18ff] 3e fc
                    ld        bc,$253b                      ;[1901] 01 3b 25
                    out       (c),a                         ;[1904] ed 79
                    ld        a,$ff                         ;[1906] 3e ff
                    ld        bc,$253b                      ;[1908] 01 3b 25
                    out       (c),a                         ;[190b] ed 79
                    pop       bc                            ;[190d] c1
                    inc       bc                            ;[190e] 03
                    ld        a,c                           ;[190f] 79
                    sub       $10                           ;[1910] d6 10
                    ld        a,b                           ;[1912] 78
                    sbc       $00                           ;[1913] de 00
                    jr        c,$189c                       ;[1915] 38 85
                    ld        a,$40                         ;[1917] 3e 40
                    ld        bc,$243b                      ;[1919] 01 3b 24
                    out       (c),a                         ;[191c] ed 79
                    ld        a,$80                         ;[191e] 3e 80
                    ld        bc,$253b                      ;[1920] 01 3b 25
                    out       (c),a                         ;[1923] ed 79
                    ld        a,$41                         ;[1925] 3e 41
                    ld        bc,$243b                      ;[1927] 01 3b 24
                    out       (c),a                         ;[192a] ed 79
                    ld        bc,$0000                      ;[192c] 01 00 00
                    push      bc                            ;[192f] c5
                    ld        a,$00                         ;[1930] 3e 00
                    ld        bc,$253b                      ;[1932] 01 3b 25
                    out       (c),a                         ;[1935] ed 79
                    ld        a,$02                         ;[1937] 3e 02
                    ld        bc,$253b                      ;[1939] 01 3b 25
                    out       (c),a                         ;[193c] ed 79
                    ld        a,$a0                         ;[193e] 3e a0
                    ld        bc,$253b                      ;[1940] 01 3b 25
                    out       (c),a                         ;[1943] ed 79
                    ld        a,$a2                         ;[1945] 3e a2
                    ld        bc,$253b                      ;[1947] 01 3b 25
                    out       (c),a                         ;[194a] ed 79
                    ld        a,$14                         ;[194c] 3e 14
                    ld        bc,$253b                      ;[194e] 01 3b 25
                    out       (c),a                         ;[1951] ed 79
                    ld        a,$16                         ;[1953] 3e 16
                    ld        bc,$253b                      ;[1955] 01 3b 25
                    out       (c),a                         ;[1958] ed 79
                    ld        a,$b4                         ;[195a] 3e b4
                    ld        bc,$253b                      ;[195c] 01 3b 25
                    out       (c),a                         ;[195f] ed 79
                    ld        a,$b6                         ;[1961] 3e b6
                    ld        bc,$253b                      ;[1963] 01 3b 25
                    out       (c),a                         ;[1966] ed 79
                    ld        a,$00                         ;[1968] 3e 00
                    ld        bc,$253b                      ;[196a] 01 3b 25
                    out       (c),a                         ;[196d] ed 79
                    ld        a,$03                         ;[196f] 3e 03
                    ld        bc,$253b                      ;[1971] 01 3b 25
                    out       (c),a                         ;[1974] ed 79
                    ld        a,$e0                         ;[1976] 3e e0
                    ld        bc,$253b                      ;[1978] 01 3b 25
                    out       (c),a                         ;[197b] ed 79
                    ld        a,$e7                         ;[197d] 3e e7
                    ld        bc,$253b                      ;[197f] 01 3b 25
                    out       (c),a                         ;[1982] ed 79
                    ld        a,$1c                         ;[1984] 3e 1c
                    ld        bc,$253b                      ;[1986] 01 3b 25
                    out       (c),a                         ;[1989] ed 79
                    ld        a,$1f                         ;[198b] 3e 1f
                    ld        bc,$253b                      ;[198d] 01 3b 25
                    out       (c),a                         ;[1990] ed 79
                    ld        a,$fc                         ;[1992] 3e fc
                    ld        bc,$253b                      ;[1994] 01 3b 25
                    out       (c),a                         ;[1997] ed 79
                    ld        a,$ff                         ;[1999] 3e ff
                    ld        bc,$253b                      ;[199b] 01 3b 25
                    out       (c),a                         ;[199e] ed 79
                    pop       bc                            ;[19a0] c1
                    inc       bc                            ;[19a1] 03
                    ld        a,c                           ;[19a2] 79
                    sub       $10                           ;[19a3] d6 10
                    ld        a,b                           ;[19a5] 78
                    sbc       $00                           ;[19a6] de 00
                    jr        c,$192f                       ;[19a8] 38 85
                    ld        a,$43                         ;[19aa] 3e 43
                    ld        bc,$243b                      ;[19ac] 01 3b 24
                    out       (c),a                         ;[19af] ed 79
                    ld        a,$10                         ;[19b1] 3e 10
                    ld        bc,$253b                      ;[19b3] 01 3b 25
                    out       (c),a                         ;[19b6] ed 79
                    call      $155d                         ;[19b8] cd 5d 15
                    ld        a,$43                         ;[19bb] 3e 43
                    ld        bc,$243b                      ;[19bd] 01 3b 24
                    out       (c),a                         ;[19c0] ed 79
                    ld        a,$20                         ;[19c2] 3e 20
                    ld        bc,$253b                      ;[19c4] 01 3b 25
                    out       (c),a                         ;[19c7] ed 79
                    call      $155d                         ;[19c9] cd 5d 15
                    ld        hl,$fc38                      ;[19cc] 21 38 fc
                    ld        (hl),$00                      ;[19cf] 36 00
                    ld        hl,$fc37                      ;[19d1] 21 37 fc
                    ld        (hl),$00                      ;[19d4] 36 00
                    ld        a,$00                         ;[19d6] 3e 00
                    out       ($fe),a                       ;[19d8] d3 fe
                    ld        bc,$4000                      ;[19da] 01 00 40
                    ld        e,c                           ;[19dd] 59
                    ld        d,b                           ;[19de] 50
                    xor       a                             ;[19df] af
                    ld        (de),a                        ;[19e0] 12
                    inc       bc                            ;[19e1] 03
                    ld        a,b                           ;[19e2] 78
                    sub       $58                           ;[19e3] d6 58
                    jr        c,$19dd                       ;[19e5] 38 f6
                    ld        bc,$5800                      ;[19e7] 01 00 58
                    ld        e,c                           ;[19ea] 59
                    ld        d,b                           ;[19eb] 50
                    xor       a                             ;[19ec] af
                    ld        (de),a                        ;[19ed] 12
                    inc       bc                            ;[19ee] 03
                    ld        a,b                           ;[19ef] 78
                    sub       $5b                           ;[19f0] d6 5b
                    jr        c,$19ea                       ;[19f2] 38 f6
                    ret                                     ;[19f4] c9

                    ld        hl,$0002                      ;[19f5] 21 02 00
                    add       hl,sp                         ;[19f8] 39
                    ld        a,(hl)                        ;[19f9] 7e
                    and       $1f                           ;[19fa] e6 1f
                    ld        ($fc37),a                     ;[19fc] 32 37 fc
                    ld        hl,$0003                      ;[19ff] 21 03 00
                    add       hl,sp                         ;[1a02] 39
                    ld        a,(hl)                        ;[1a03] 7e
                    ld        iy,$fc38                      ;[1a04] fd 21 38 fc
                    ld        (iy+$00),a                    ;[1a08] fd 77 00
                    ld        a,$17                         ;[1a0b] 3e 17
                    sub       (iy+$00)                      ;[1a0d] fd 96 00
                    ret       nc                            ;[1a10] d0
                    ld        (iy+$00),$17                  ;[1a11] fd 36 00 17
                    ret                                     ;[1a15] c9

                    push      af                            ;[1a16] f5
                    push      af                            ;[1a17] f5
                    ld        hl,$0006                      ;[1a18] 21 06 00
                    add       hl,sp                         ;[1a1b] 39
                    ld        a,(hl)                        ;[1a1c] 7e
                    ld        b,$00                         ;[1a1d] 06 00
                    add       $e0                           ;[1a1f] c6 e0
                    ld        c,a                           ;[1a21] 4f
                    ld        a,b                           ;[1a22] 78
                    adc       $ff                           ;[1a23] ce ff
                    ld        b,a                           ;[1a25] 47
                    sla       c                             ;[1a26] cb 21
                    rl        b                             ;[1a28] cb 10
                    sla       c                             ;[1a2a] cb 21
                    rl        b                             ;[1a2c] cb 10
                    sla       c                             ;[1a2e] cb 21
                    rl        b                             ;[1a30] cb 10
                    ld        ($fc31),bc                    ;[1a32] ed 43 31 fc
                    ld        a,($fc38)                     ;[1a36] 3a 38 fc
                    ld        iy,$0002                      ;[1a39] fd 21 02 00
                    add       iy,sp                         ;[1a3d] fd 39
                    ld        (iy+$00),a                    ;[1a3f] fd 77 00
                    ld        (iy+$01),$00                  ;[1a42] fd 36 01 00
                    ld        a,(iy+$00)                    ;[1a46] fd 7e 00
                    ld        iy,$fc33                      ;[1a49] fd 21 33 fc
                    ld        (iy+$01),a                    ;[1a4d] fd 77 01
                    ld        (iy+$00),$00                  ;[1a50] fd 36 00 00
                    ld        c,$00                         ;[1a54] 0e 00
                    ld        a,(iy+$01)                    ;[1a56] fd 7e 01
                    and       $18                           ;[1a59] e6 18
                    ld        b,a                           ;[1a5b] 47
                    ld        a,(iy+$00)                    ;[1a5c] fd 7e 00
                    and       $e0                           ;[1a5f] e6 e0
                    ld        l,a                           ;[1a61] 6f
                    ld        h,$00                         ;[1a62] 26 00
                    add       hl,hl                         ;[1a64] 29
                    add       hl,hl                         ;[1a65] 29
                    add       hl,hl                         ;[1a66] 29
                    ld        a,c                           ;[1a67] 79
                    or        l                             ;[1a68] b5
                    ld        c,a                           ;[1a69] 4f
                    ld        a,b                           ;[1a6a] 78
                    or        h                             ;[1a6b] b4
                    ld        b,a                           ;[1a6c] 47
                    ld        e,$00                         ;[1a6d] 1e 00
                    ld        a,(iy+$01)                    ;[1a6f] fd 7e 01
                    and       $07                           ;[1a72] e6 07
                    ld        d,a                           ;[1a74] 57
                    ld        a,$03                         ;[1a75] 3e 03
                    srl       d                             ;[1a77] cb 3a
                    rr        e                             ;[1a79] cb 1b
                    dec       a                             ;[1a7b] 3d
                    jr        nz,$1a77                      ;[1a7c] 20 f9
                    ld        a,c                           ;[1a7e] 79
                    or        e                             ;[1a7f] b3
                    ld        (iy+$00),a                    ;[1a80] fd 77 00
                    ld        a,b                           ;[1a83] 78
                    or        d                             ;[1a84] b2
                    ld        (iy+$01),a                    ;[1a85] fd 77 01
                    ld        a,(iy+$00)                    ;[1a88] fd 7e 00
                    add       $00                           ;[1a8b] c6 00
                    ld        e,a                           ;[1a8d] 5f
                    ld        a,(iy+$01)                    ;[1a8e] fd 7e 01
                    adc       $40                           ;[1a91] ce 40
                    ld        d,a                           ;[1a93] 57
                    ld        hl,$fc37                      ;[1a94] 21 37 fc
                    ld        c,(hl)                        ;[1a97] 4e
                    ld        b,$00                         ;[1a98] 06 00
                    inc       sp                            ;[1a9a] 33
                    inc       sp                            ;[1a9b] 33
                    push      bc                            ;[1a9c] c5
                    ld        a,e                           ;[1a9d] 7b
                    ld        hl,$0000                      ;[1a9e] 21 00 00
                    add       hl,sp                         ;[1aa1] 39
                    ld        iy,$fc33                      ;[1aa2] fd 21 33 fc
                    add       (hl)                          ;[1aa6] 86
                    ld        (iy+$00),a                    ;[1aa7] fd 77 00
                    ld        a,d                           ;[1aaa] 7a
                    inc       hl                            ;[1aab] 23
                    adc       (hl)                          ;[1aac] 8e
                    inc       iy                            ;[1aad] fd 23
                    ld        (iy+$00),a                    ;[1aaf] fd 77 00
                    pop       de                            ;[1ab2] d1
                    pop       hl                            ;[1ab3] e1
                    push      hl                            ;[1ab4] e5
                    push      de                            ;[1ab5] d5
                    add       hl,hl                         ;[1ab6] 29
                    add       hl,hl                         ;[1ab7] 29
                    add       hl,hl                         ;[1ab8] 29
                    add       hl,hl                         ;[1ab9] 29
                    add       hl,hl                         ;[1aba] 29
                    ld        e,l                           ;[1abb] 5d
                    ld        a,h                           ;[1abc] 7c
                    add       $58                           ;[1abd] c6 58
                    ld        d,a                           ;[1abf] 57
                    ld        a,e                           ;[1ac0] 7b
                    ld        hl,$fc35                      ;[1ac1] 21 35 fc
                    add       c                             ;[1ac4] 81
                    ld        (hl),a                        ;[1ac5] 77
                    ld        a,d                           ;[1ac6] 7a
                    adc       b                             ;[1ac7] 88
                    inc       hl                            ;[1ac8] 23
                    ld        (hl),a                        ;[1ac9] 77
                    ld        c,$00                         ;[1aca] 0e 00
                    ld        de,($fc33)                    ;[1acc] ed 5b 33 fc
                    ld        iy,$1584                      ;[1ad0] fd 21 84 15
                    push      bc                            ;[1ad4] c5
                    ld        bc,($fc31)                    ;[1ad5] ed 4b 31 fc
                    add       iy,bc                         ;[1ad9] fd 09
                    pop       bc                            ;[1adb] c1
                    ld        a,(iy+$00)                    ;[1adc] fd 7e 00
                    ld        (de),a                        ;[1adf] 12
                    ld        hl,$fc33                      ;[1ae0] 21 33 fc
                    ld        a,(hl)                        ;[1ae3] 7e
                    add       $00                           ;[1ae4] c6 00
                    ld        (hl),a                        ;[1ae6] 77
                    inc       hl                            ;[1ae7] 23
                    ld        a,(hl)                        ;[1ae8] 7e
                    adc       $01                           ;[1ae9] ce 01
                    ld        (hl),a                        ;[1aeb] 77
                    ld        iy,$fc31                      ;[1aec] fd 21 31 fc
                    inc       (iy+$00)                      ;[1af0] fd 34 00
                    jr        nz,$1af8                      ;[1af3] 20 03
                    inc       (iy+$01)                      ;[1af5] fd 34 01
                    inc       c                             ;[1af8] 0c
                    ld        a,c                           ;[1af9] 79
                    sub       $08                           ;[1afa] d6 08
                    jr        c,$1acc                       ;[1afc] 38 ce
                    ld        hl,$fc37                      ;[1afe] 21 37 fc
                    inc       (hl)                          ;[1b01] 34
                    pop       af                            ;[1b02] f1
                    pop       af                            ;[1b03] f1
                    ret                                     ;[1b04] c9

                    pop       de                            ;[1b05] d1
                    pop       bc                            ;[1b06] c1
                    push      bc                            ;[1b07] c5
                    push      de                            ;[1b08] d5
                    ld        a,(bc)                        ;[1b09] 0a
                    inc       bc                            ;[1b0a] 03
                    ld        d,a                           ;[1b0b] 57
                    or        a                             ;[1b0c] b7
                    ret       z                             ;[1b0d] c8
                    push      bc                            ;[1b0e] c5
                    push      de                            ;[1b0f] d5
                    inc       sp                            ;[1b10] 33
                    call      $1a16                         ;[1b11] cd 16 1a
                    inc       sp                            ;[1b14] 33
                    pop       bc                            ;[1b15] c1
                    jr        $1b09                         ;[1b16] 18 f1
                    pop       bc                            ;[1b18] c1
                    pop       hl                            ;[1b19] e1
                    push      hl                            ;[1b1a] e5
                    push      bc                            ;[1b1b] c5
                    xor       a                             ;[1b1c] af
                    ld        b,a                           ;[1b1d] 47
                    ld        c,a                           ;[1b1e] 4f
                    cpir                                    ;[1b1f] ed b1
                    ld        hl,$ffff                      ;[1b21] 21 ff ff
                    sbc       hl,bc                         ;[1b24] ed 42
                    ret                                     ;[1b26] c9

                    pop       af                            ;[1b27] f1
                    pop       bc                            ;[1b28] c1
                    pop       de                            ;[1b29] d1
                    push      de                            ;[1b2a] d5
                    push      bc                            ;[1b2b] c5
                    push      af                            ;[1b2c] f5
                    xor       a                             ;[1b2d] af
                    ld        l,a                           ;[1b2e] 6f
                    or        b                             ;[1b2f] b0
                    ld        b,$10                         ;[1b30] 06 10
                    jr        nz,$1b38                      ;[1b32] 20 04
                    ld        b,$08                         ;[1b34] 06 08
                    ld        a,c                           ;[1b36] 79
                    add       hl,hl                         ;[1b37] 29
                    rl        c                             ;[1b38] cb 11
                    rla                                     ;[1b3a] 17
                    jr        nc,$1b3e                      ;[1b3b] 30 01
                    add       hl,de                         ;[1b3d] 19
                    djnz      $1b37                         ;[1b3e] 10 f7
                    ret                                     ;[1b40] c9

                    push      ix                            ;[1b41] dd e5
                    ld        ix,$0000                      ;[1b43] dd 21 00 00
                    add       ix,sp                         ;[1b47] dd 39
                    ld        hl,$fffa                      ;[1b49] 21 fa ff
                    add       hl,sp                         ;[1b4c] 39
                    ld        sp,hl                         ;[1b4d] f9
                    ld        hl,$000a                      ;[1b4e] 21 0a 00
                    add       hl,sp                         ;[1b51] 39
                    ex        de,hl                         ;[1b52] eb
                    ld        c,e                           ;[1b53] 4b
                    ld        b,d                           ;[1b54] 42
                    inc       bc                            ;[1b55] 03
                    inc       bc                            ;[1b56] 03
                    ld        (ix-$02),c                    ;[1b57] dd 71 fe
                    ld        (ix-$01),b                    ;[1b5a] dd 70 ff
                    ld        l,e                           ;[1b5d] 6b
                    ld        h,d                           ;[1b5e] 62
                    inc       hl                            ;[1b5f] 23
                    inc       hl                            ;[1b60] 23
                    ld        c,(hl)                        ;[1b61] 4e
                    inc       hl                            ;[1b62] 23
                    ld        b,(hl)                        ;[1b63] 46
                    ld        hl,$000e                      ;[1b64] 21 0e 00
                    add       hl,sp                         ;[1b67] 39
                    ld        (ix-$04),l                    ;[1b68] dd 75 fc
                    ld        (ix-$03),h                    ;[1b6b] dd 74 fd
                    ld        l,(ix-$04)                    ;[1b6e] dd 6e fc
                    ld        h,(ix-$03)                    ;[1b71] dd 66 fd
                    ld        a,(hl)                        ;[1b74] 7e
                    inc       hl                            ;[1b75] 23
                    ld        h,(hl)                        ;[1b76] 66
                    ld        l,a                           ;[1b77] 6f
                    push      de                            ;[1b78] d5
                    push      hl                            ;[1b79] e5
                    push      bc                            ;[1b7a] c5
                    call      $1b27                         ;[1b7b] cd 27 1b
                    pop       af                            ;[1b7e] f1
                    pop       af                            ;[1b7f] f1
                    ld        c,l                           ;[1b80] 4d
                    ld        b,h                           ;[1b81] 44
                    pop       de                            ;[1b82] d1
                    ld        l,(ix-$02)                    ;[1b83] dd 6e fe
                    ld        h,(ix-$01)                    ;[1b86] dd 66 ff
                    ld        (hl),c                        ;[1b89] 71
                    inc       hl                            ;[1b8a] 23
                    ld        (hl),b                        ;[1b8b] 70
                    ld        c,e                           ;[1b8c] 4b
                    ld        b,d                           ;[1b8d] 42
                    inc       bc                            ;[1b8e] 03
                    inc       bc                            ;[1b8f] 03
                    ld        (ix-$02),c                    ;[1b90] dd 71 fe
                    ld        (ix-$01),b                    ;[1b93] dd 70 ff
                    ld        l,e                           ;[1b96] 6b
                    ld        h,d                           ;[1b97] 62
                    inc       hl                            ;[1b98] 23
                    inc       hl                            ;[1b99] 23
                    ld        a,(hl)                        ;[1b9a] 7e
                    ld        (ix-$06),a                    ;[1b9b] dd 77 fa
                    inc       hl                            ;[1b9e] 23
                    ld        a,(hl)                        ;[1b9f] 7e
                    ld        (ix-$05),a                    ;[1ba0] dd 77 fb
                    pop       bc                            ;[1ba3] c1
                    pop       hl                            ;[1ba4] e1
                    push      hl                            ;[1ba5] e5
                    push      bc                            ;[1ba6] c5
                    inc       hl                            ;[1ba7] 23
                    inc       hl                            ;[1ba8] 23
                    ld        c,(hl)                        ;[1ba9] 4e
                    inc       hl                            ;[1baa] 23
                    ld        b,(hl)                        ;[1bab] 46
                    ld        l,e                           ;[1bac] 6b
                    ld        h,d                           ;[1bad] 62
                    ld        a,(hl)                        ;[1bae] 7e
                    inc       hl                            ;[1baf] 23
                    ld        h,(hl)                        ;[1bb0] 66
                    ld        l,a                           ;[1bb1] 6f
                    push      de                            ;[1bb2] d5
                    push      hl                            ;[1bb3] e5
                    push      bc                            ;[1bb4] c5
                    call      $1b27                         ;[1bb5] cd 27 1b
                    pop       af                            ;[1bb8] f1
                    pop       af                            ;[1bb9] f1
                    pop       de                            ;[1bba] d1
                    ld        a,(ix-$06)                    ;[1bbb] dd 7e fa
                    add       l                             ;[1bbe] 85
                    ld        c,a                           ;[1bbf] 4f
                    ld        a,(ix-$05)                    ;[1bc0] dd 7e fb
                    adc       h                             ;[1bc3] 8c
                    ld        b,a                           ;[1bc4] 47
                    ld        l,(ix-$02)                    ;[1bc5] dd 6e fe
                    ld        h,(ix-$01)                    ;[1bc8] dd 66 ff
                    ld        (hl),c                        ;[1bcb] 71
                    inc       hl                            ;[1bcc] 23
                    ld        (hl),b                        ;[1bcd] 70
                    ld        c,e                           ;[1bce] 4b
                    ld        b,d                           ;[1bcf] 42
                    inc       bc                            ;[1bd0] 03
                    inc       bc                            ;[1bd1] 03
                    inc       sp                            ;[1bd2] 33
                    inc       sp                            ;[1bd3] 33
                    push      bc                            ;[1bd4] c5
                    ld        l,e                           ;[1bd5] 6b
                    ld        h,d                           ;[1bd6] 62
                    inc       hl                            ;[1bd7] 23
                    inc       hl                            ;[1bd8] 23
                    ld        c,(hl)                        ;[1bd9] 4e
                    inc       hl                            ;[1bda] 23
                    ld        b,(hl)                        ;[1bdb] 46
                    ld        l,e                           ;[1bdc] 6b
                    ld        h,d                           ;[1bdd] 62
                    inc       hl                            ;[1bde] 23
                    ld        a,(hl)                        ;[1bdf] 7e
                    ld        (ix-$02),a                    ;[1be0] dd 77 fe
                    ld        l,(ix-$04)                    ;[1be3] dd 6e fc
                    ld        h,(ix-$03)                    ;[1be6] dd 66 fd
                    inc       hl                            ;[1be9] 23
                    ld        h,(hl)                        ;[1bea] 66
                    push      de                            ;[1beb] d5
                    push      bc                            ;[1bec] c5
                    ld        e,(ix-$02)                    ;[1bed] dd 5e fe
                    ld        l,$00                         ;[1bf0] 2e 00
                    ld        d,l                           ;[1bf2] 55
                    ld        b,$08                         ;[1bf3] 06 08
                    add       hl,hl                         ;[1bf5] 29
                    jr        nc,$1bf9                      ;[1bf6] 30 01
                    add       hl,de                         ;[1bf8] 19
                    djnz      $1bf5                         ;[1bf9] 10 fa
                    pop       bc                            ;[1bfb] c1
                    pop       de                            ;[1bfc] d1
                    add       hl,bc                         ;[1bfd] 09
                    ld        c,l                           ;[1bfe] 4d
                    ld        b,h                           ;[1bff] 44
                    pop       hl                            ;[1c00] e1
                    push      hl                            ;[1c01] e5
                    ld        (hl),c                        ;[1c02] 71
                    inc       hl                            ;[1c03] 23
                    ld        (hl),b                        ;[1c04] 70
                    pop       bc                            ;[1c05] c1
                    pop       hl                            ;[1c06] e1
                    push      hl                            ;[1c07] e5
                    push      bc                            ;[1c08] c5
                    ld        c,(hl)                        ;[1c09] 4e
                    ld        l,e                           ;[1c0a] 6b
                    ld        h,d                           ;[1c0b] 62
                    inc       hl                            ;[1c0c] 23
                    ld        h,(hl)                        ;[1c0d] 66
                    push      de                            ;[1c0e] d5
                    ld        e,c                           ;[1c0f] 59
                    ld        l,$00                         ;[1c10] 2e 00
                    ld        d,l                           ;[1c12] 55
                    ld        b,$08                         ;[1c13] 06 08
                    add       hl,hl                         ;[1c15] 29
                    jr        nc,$1c19                      ;[1c16] 30 01
                    add       hl,de                         ;[1c18] 19
                    djnz      $1c15                         ;[1c19] 10 fa
                    pop       de                            ;[1c1b] d1
                    ld        c,l                           ;[1c1c] 4d
                    ld        b,h                           ;[1c1d] 44
                    ld        l,(ix-$04)                    ;[1c1e] dd 6e fc
                    ld        h,(ix-$03)                    ;[1c21] dd 66 fd
                    inc       hl                            ;[1c24] 23
                    push      hl                            ;[1c25] e5
                    pop       iy                            ;[1c26] fd e1
                    ld        l,e                           ;[1c28] 6b
                    ld        h,d                           ;[1c29] 62
                    ld        a,(hl)                        ;[1c2a] 7e
                    ld        (ix-$06),a                    ;[1c2b] dd 77 fa
                    ld        l,(ix-$04)                    ;[1c2e] dd 6e fc
                    ld        h,(ix-$03)                    ;[1c31] dd 66 fd
                    inc       hl                            ;[1c34] 23
                    ld        l,(hl)                        ;[1c35] 6e
                    push      de                            ;[1c36] d5
                    push      bc                            ;[1c37] c5
                    ld        e,l                           ;[1c38] 5d
                    ld        h,(ix-$06)                    ;[1c39] dd 66 fa
                    ld        l,$00                         ;[1c3c] 2e 00
                    ld        d,l                           ;[1c3e] 55
                    ld        b,$08                         ;[1c3f] 06 08
                    add       hl,hl                         ;[1c41] 29
                    jr        nc,$1c45                      ;[1c42] 30 01
                    add       hl,de                         ;[1c44] 19
                    djnz      $1c41                         ;[1c45] 10 fa
                    pop       bc                            ;[1c47] c1
                    pop       de                            ;[1c48] d1
                    ld        (iy+$00),l                    ;[1c49] fd 75 00
                    ld        (iy+$01),h                    ;[1c4c] fd 74 01
                    ld        l,(ix-$04)                    ;[1c4f] dd 6e fc
                    ld        h,(ix-$03)                    ;[1c52] dd 66 fd
                    inc       hl                            ;[1c55] 23
                    inc       hl                            ;[1c56] 23
                    inc       hl                            ;[1c57] 23
                    ex        (sp),hl                       ;[1c58] e3
                    ld        l,(ix-$04)                    ;[1c59] dd 6e fc
                    ld        h,(ix-$03)                    ;[1c5c] dd 66 fd
                    inc       hl                            ;[1c5f] 23
                    push      hl                            ;[1c60] e5
                    pop       iy                            ;[1c61] fd e1
                    ld        l,(ix-$04)                    ;[1c63] dd 6e fc
                    ld        h,(ix-$03)                    ;[1c66] dd 66 fd
                    inc       hl                            ;[1c69] 23
                    ld        a,(hl)                        ;[1c6a] 7e
                    inc       hl                            ;[1c6b] 23
                    ld        h,(hl)                        ;[1c6c] 66
                    ld        l,a                           ;[1c6d] 6f
                    add       hl,bc                         ;[1c6e] 09
                    ld        (iy+$00),l                    ;[1c6f] fd 75 00
                    ld        (iy+$01),h                    ;[1c72] fd 74 01
                    cp        a                             ;[1c75] bf
                    sbc       hl,bc                         ;[1c76] ed 42
                    ld        a,$00                         ;[1c78] 3e 00
                    rla                                     ;[1c7a] 17
                    pop       hl                            ;[1c7b] e1
                    push      hl                            ;[1c7c] e5
                    ld        (hl),a                        ;[1c7d] 77
                    ld        c,e                           ;[1c7e] 4b
                    ld        b,d                           ;[1c7f] 42
                    ld        a,(de)                        ;[1c80] 1a
                    ld        e,a                           ;[1c81] 5f
                    ld        l,(ix-$04)                    ;[1c82] dd 6e fc
                    ld        h,(ix-$03)                    ;[1c85] dd 66 fd
                    ld        h,(hl)                        ;[1c88] 66
                    push      bc                            ;[1c89] c5
                    ld        l,$00                         ;[1c8a] 2e 00
                    ld        d,l                           ;[1c8c] 55
                    ld        b,$08                         ;[1c8d] 06 08
                    add       hl,hl                         ;[1c8f] 29
                    jr        nc,$1c93                      ;[1c90] 30 01
                    add       hl,de                         ;[1c92] 19
                    djnz      $1c8f                         ;[1c93] 10 fa
                    pop       bc                            ;[1c95] c1
                    ex        de,hl                         ;[1c96] eb
                    ld        a,e                           ;[1c97] 7b
                    ld        (bc),a                        ;[1c98] 02
                    inc       bc                            ;[1c99] 03
                    ld        a,d                           ;[1c9a] 7a
                    ld        (bc),a                        ;[1c9b] 02
                    pop       de                            ;[1c9c] d1
                    pop       bc                            ;[1c9d] c1
                    push      bc                            ;[1c9e] c5
                    push      de                            ;[1c9f] d5
                    xor       a                             ;[1ca0] af
                    ld        (bc),a                        ;[1ca1] 02
                    ld        a,(ix+$04)                    ;[1ca2] dd 7e 04
                    add       (ix+$08)                      ;[1ca5] dd 86 08
                    ld        l,a                           ;[1ca8] 6f
                    ld        a,(ix+$05)                    ;[1ca9] dd 7e 05
                    adc       (ix+$09)                      ;[1cac] dd 8e 09
                    ld        h,a                           ;[1caf] 67
                    ld        a,(ix+$06)                    ;[1cb0] dd 7e 06
                    adc       (ix+$0a)                      ;[1cb3] dd 8e 0a
                    ld        e,a                           ;[1cb6] 5f
                    ld        a,(ix+$07)                    ;[1cb7] dd 7e 07
                    adc       (ix+$0b)                      ;[1cba] dd 8e 0b
                    ld        d,a                           ;[1cbd] 57
                    ld        sp,ix                         ;[1cbe] dd f9
                    pop       ix                            ;[1cc0] dd e1
                    ret                                     ;[1cc2] c9

                    ld        b,a                           ;[1cc3] 47
                    inc       b                             ;[1cc4] 04
                    ld        d,e                           ;[1cc5] 53
                    inc       b                             ;[1cc6] 04
                    ld        bc,$0004                      ;[1cc7] 01 04 00
                    ld        a,b                           ;[1cca] 78
                    or        c                             ;[1ccb] b1
                    jr        z,$1cd6                       ;[1ccc] 28 08
                    ld        de,$fc39                      ;[1cce] 11 39 fc
                    ld        hl,$1cc3                      ;[1cd1] 21 c3 1c
                    ldir                                    ;[1cd4] ed b0
                    ret                                     ;[1cd6] c9

                    rst       $38                           ;[1cd7] ff
                    rst       $38                           ;[1cd8] ff
                    rst       $38                           ;[1cd9] ff
                    rst       $38                           ;[1cda] ff
                    rst       $38                           ;[1cdb] ff
                    rst       $38                           ;[1cdc] ff
                    rst       $38                           ;[1cdd] ff
                    rst       $38                           ;[1cde] ff
                    rst       $38                           ;[1cdf] ff
                    rst       $38                           ;[1ce0] ff
                    rst       $38                           ;[1ce1] ff
                    rst       $38                           ;[1ce2] ff
                    rst       $38                           ;[1ce3] ff
                    rst       $38                           ;[1ce4] ff
                    rst       $38                           ;[1ce5] ff
                    rst       $38                           ;[1ce6] ff
                    rst       $38                           ;[1ce7] ff
                    rst       $38                           ;[1ce8] ff
                    rst       $38                           ;[1ce9] ff
                    rst       $38                           ;[1cea] ff
                    rst       $38                           ;[1ceb] ff
                    rst       $38                           ;[1cec] ff
                    rst       $38                           ;[1ced] ff
                    rst       $38                           ;[1cee] ff
                    rst       $38                           ;[1cef] ff
                    rst       $38                           ;[1cf0] ff
                    rst       $38                           ;[1cf1] ff
                    rst       $38                           ;[1cf2] ff
                    rst       $38                           ;[1cf3] ff
                    rst       $38                           ;[1cf4] ff
                    rst       $38                           ;[1cf5] ff
                    rst       $38                           ;[1cf6] ff
                    rst       $38                           ;[1cf7] ff
                    rst       $38                           ;[1cf8] ff
                    rst       $38                           ;[1cf9] ff
                    rst       $38                           ;[1cfa] ff
                    rst       $38                           ;[1cfb] ff
                    rst       $38                           ;[1cfc] ff
                    rst       $38                           ;[1cfd] ff
                    rst       $38                           ;[1cfe] ff
                    rst       $38                           ;[1cff] ff
                    rst       $38                           ;[1d00] ff
                    rst       $38                           ;[1d01] ff
                    rst       $38                           ;[1d02] ff
                    rst       $38                           ;[1d03] ff
                    rst       $38                           ;[1d04] ff
                    rst       $38                           ;[1d05] ff
                    rst       $38                           ;[1d06] ff
                    rst       $38                           ;[1d07] ff
                    rst       $38                           ;[1d08] ff
                    rst       $38                           ;[1d09] ff
                    rst       $38                           ;[1d0a] ff
                    rst       $38                           ;[1d0b] ff
                    rst       $38                           ;[1d0c] ff
                    rst       $38                           ;[1d0d] ff
                    rst       $38                           ;[1d0e] ff
                    rst       $38                           ;[1d0f] ff
                    rst       $38                           ;[1d10] ff
                    rst       $38                           ;[1d11] ff
                    rst       $38                           ;[1d12] ff
                    rst       $38                           ;[1d13] ff
                    rst       $38                           ;[1d14] ff
                    rst       $38                           ;[1d15] ff
                    rst       $38                           ;[1d16] ff
                    rst       $38                           ;[1d17] ff
                    rst       $38                           ;[1d18] ff
                    rst       $38                           ;[1d19] ff
                    rst       $38                           ;[1d1a] ff
                    rst       $38                           ;[1d1b] ff
                    rst       $38                           ;[1d1c] ff
                    rst       $38                           ;[1d1d] ff
                    rst       $38                           ;[1d1e] ff
                    rst       $38                           ;[1d1f] ff
                    rst       $38                           ;[1d20] ff
                    rst       $38                           ;[1d21] ff
                    rst       $38                           ;[1d22] ff
                    rst       $38                           ;[1d23] ff
                    rst       $38                           ;[1d24] ff
                    rst       $38                           ;[1d25] ff
                    rst       $38                           ;[1d26] ff
                    rst       $38                           ;[1d27] ff
                    rst       $38                           ;[1d28] ff
                    rst       $38                           ;[1d29] ff
                    rst       $38                           ;[1d2a] ff
                    rst       $38                           ;[1d2b] ff
                    rst       $38                           ;[1d2c] ff
                    rst       $38                           ;[1d2d] ff
                    rst       $38                           ;[1d2e] ff
                    rst       $38                           ;[1d2f] ff
                    rst       $38                           ;[1d30] ff
                    rst       $38                           ;[1d31] ff
                    rst       $38                           ;[1d32] ff
                    rst       $38                           ;[1d33] ff
                    rst       $38                           ;[1d34] ff
                    rst       $38                           ;[1d35] ff
                    rst       $38                           ;[1d36] ff
                    rst       $38                           ;[1d37] ff
                    rst       $38                           ;[1d38] ff
                    rst       $38                           ;[1d39] ff
                    rst       $38                           ;[1d3a] ff
                    rst       $38                           ;[1d3b] ff
                    rst       $38                           ;[1d3c] ff
                    rst       $38                           ;[1d3d] ff
                    rst       $38                           ;[1d3e] ff
                    rst       $38                           ;[1d3f] ff
                    rst       $38                           ;[1d40] ff
                    rst       $38                           ;[1d41] ff
                    rst       $38                           ;[1d42] ff
                    rst       $38                           ;[1d43] ff
                    rst       $38                           ;[1d44] ff
                    rst       $38                           ;[1d45] ff
                    rst       $38                           ;[1d46] ff
                    rst       $38                           ;[1d47] ff
                    rst       $38                           ;[1d48] ff
                    rst       $38                           ;[1d49] ff
                    rst       $38                           ;[1d4a] ff
                    rst       $38                           ;[1d4b] ff
                    rst       $38                           ;[1d4c] ff
                    rst       $38                           ;[1d4d] ff
                    rst       $38                           ;[1d4e] ff
                    rst       $38                           ;[1d4f] ff
                    rst       $38                           ;[1d50] ff
                    rst       $38                           ;[1d51] ff
                    rst       $38                           ;[1d52] ff
                    rst       $38                           ;[1d53] ff
                    rst       $38                           ;[1d54] ff
                    rst       $38                           ;[1d55] ff
                    rst       $38                           ;[1d56] ff
                    rst       $38                           ;[1d57] ff
                    rst       $38                           ;[1d58] ff
                    rst       $38                           ;[1d59] ff
                    rst       $38                           ;[1d5a] ff
                    rst       $38                           ;[1d5b] ff
                    rst       $38                           ;[1d5c] ff
                    rst       $38                           ;[1d5d] ff
                    rst       $38                           ;[1d5e] ff
                    rst       $38                           ;[1d5f] ff
                    rst       $38                           ;[1d60] ff
                    rst       $38                           ;[1d61] ff
                    rst       $38                           ;[1d62] ff
                    rst       $38                           ;[1d63] ff
                    rst       $38                           ;[1d64] ff
                    rst       $38                           ;[1d65] ff
                    rst       $38                           ;[1d66] ff
                    rst       $38                           ;[1d67] ff
                    rst       $38                           ;[1d68] ff
                    rst       $38                           ;[1d69] ff
                    rst       $38                           ;[1d6a] ff
                    rst       $38                           ;[1d6b] ff
                    rst       $38                           ;[1d6c] ff
                    rst       $38                           ;[1d6d] ff
                    rst       $38                           ;[1d6e] ff
                    rst       $38                           ;[1d6f] ff
                    rst       $38                           ;[1d70] ff
                    rst       $38                           ;[1d71] ff
                    rst       $38                           ;[1d72] ff
                    rst       $38                           ;[1d73] ff
                    rst       $38                           ;[1d74] ff
                    rst       $38                           ;[1d75] ff
                    rst       $38                           ;[1d76] ff
                    rst       $38                           ;[1d77] ff
                    rst       $38                           ;[1d78] ff
                    rst       $38                           ;[1d79] ff
                    rst       $38                           ;[1d7a] ff
                    rst       $38                           ;[1d7b] ff
                    rst       $38                           ;[1d7c] ff
                    rst       $38                           ;[1d7d] ff
                    rst       $38                           ;[1d7e] ff
                    rst       $38                           ;[1d7f] ff
                    rst       $38                           ;[1d80] ff
                    rst       $38                           ;[1d81] ff
                    rst       $38                           ;[1d82] ff
                    rst       $38                           ;[1d83] ff
                    rst       $38                           ;[1d84] ff
                    rst       $38                           ;[1d85] ff
                    rst       $38                           ;[1d86] ff
                    rst       $38                           ;[1d87] ff
                    rst       $38                           ;[1d88] ff
                    rst       $38                           ;[1d89] ff
                    rst       $38                           ;[1d8a] ff
                    rst       $38                           ;[1d8b] ff
                    rst       $38                           ;[1d8c] ff
                    rst       $38                           ;[1d8d] ff
                    rst       $38                           ;[1d8e] ff
                    rst       $38                           ;[1d8f] ff
                    rst       $38                           ;[1d90] ff
                    rst       $38                           ;[1d91] ff
                    rst       $38                           ;[1d92] ff
                    rst       $38                           ;[1d93] ff
                    rst       $38                           ;[1d94] ff
                    rst       $38                           ;[1d95] ff
                    rst       $38                           ;[1d96] ff
                    rst       $38                           ;[1d97] ff
                    rst       $38                           ;[1d98] ff
                    rst       $38                           ;[1d99] ff
                    rst       $38                           ;[1d9a] ff
                    rst       $38                           ;[1d9b] ff
                    rst       $38                           ;[1d9c] ff
                    rst       $38                           ;[1d9d] ff
                    rst       $38                           ;[1d9e] ff
                    rst       $38                           ;[1d9f] ff
                    rst       $38                           ;[1da0] ff
                    rst       $38                           ;[1da1] ff
                    rst       $38                           ;[1da2] ff
                    rst       $38                           ;[1da3] ff
                    rst       $38                           ;[1da4] ff
                    rst       $38                           ;[1da5] ff
                    rst       $38                           ;[1da6] ff
                    rst       $38                           ;[1da7] ff
                    rst       $38                           ;[1da8] ff
                    rst       $38                           ;[1da9] ff
                    rst       $38                           ;[1daa] ff
                    rst       $38                           ;[1dab] ff
                    rst       $38                           ;[1dac] ff
                    rst       $38                           ;[1dad] ff
                    rst       $38                           ;[1dae] ff
                    rst       $38                           ;[1daf] ff
                    rst       $38                           ;[1db0] ff
                    rst       $38                           ;[1db1] ff
                    rst       $38                           ;[1db2] ff
                    rst       $38                           ;[1db3] ff
                    rst       $38                           ;[1db4] ff
                    rst       $38                           ;[1db5] ff
                    rst       $38                           ;[1db6] ff
                    rst       $38                           ;[1db7] ff
                    rst       $38                           ;[1db8] ff
                    rst       $38                           ;[1db9] ff
                    rst       $38                           ;[1dba] ff
                    rst       $38                           ;[1dbb] ff
                    rst       $38                           ;[1dbc] ff
                    rst       $38                           ;[1dbd] ff
                    rst       $38                           ;[1dbe] ff
                    rst       $38                           ;[1dbf] ff
                    rst       $38                           ;[1dc0] ff
                    rst       $38                           ;[1dc1] ff
                    rst       $38                           ;[1dc2] ff
                    rst       $38                           ;[1dc3] ff
                    rst       $38                           ;[1dc4] ff
                    rst       $38                           ;[1dc5] ff
                    rst       $38                           ;[1dc6] ff
                    rst       $38                           ;[1dc7] ff
                    rst       $38                           ;[1dc8] ff
                    rst       $38                           ;[1dc9] ff
                    rst       $38                           ;[1dca] ff
                    rst       $38                           ;[1dcb] ff
                    rst       $38                           ;[1dcc] ff
                    rst       $38                           ;[1dcd] ff
                    rst       $38                           ;[1dce] ff
                    rst       $38                           ;[1dcf] ff
                    rst       $38                           ;[1dd0] ff
                    rst       $38                           ;[1dd1] ff
                    rst       $38                           ;[1dd2] ff
                    rst       $38                           ;[1dd3] ff
                    rst       $38                           ;[1dd4] ff
                    rst       $38                           ;[1dd5] ff
                    rst       $38                           ;[1dd6] ff
                    rst       $38                           ;[1dd7] ff
                    rst       $38                           ;[1dd8] ff
                    rst       $38                           ;[1dd9] ff
                    rst       $38                           ;[1dda] ff
                    rst       $38                           ;[1ddb] ff
                    rst       $38                           ;[1ddc] ff
                    rst       $38                           ;[1ddd] ff
                    rst       $38                           ;[1dde] ff
                    rst       $38                           ;[1ddf] ff
                    rst       $38                           ;[1de0] ff
                    rst       $38                           ;[1de1] ff
                    rst       $38                           ;[1de2] ff
                    rst       $38                           ;[1de3] ff
                    rst       $38                           ;[1de4] ff
                    rst       $38                           ;[1de5] ff
                    rst       $38                           ;[1de6] ff
                    rst       $38                           ;[1de7] ff
                    rst       $38                           ;[1de8] ff
                    rst       $38                           ;[1de9] ff
                    rst       $38                           ;[1dea] ff
                    rst       $38                           ;[1deb] ff
                    rst       $38                           ;[1dec] ff
                    rst       $38                           ;[1ded] ff
                    rst       $38                           ;[1dee] ff
                    rst       $38                           ;[1def] ff
                    rst       $38                           ;[1df0] ff
                    rst       $38                           ;[1df1] ff
                    rst       $38                           ;[1df2] ff
                    rst       $38                           ;[1df3] ff
                    rst       $38                           ;[1df4] ff
                    rst       $38                           ;[1df5] ff
                    rst       $38                           ;[1df6] ff
                    rst       $38                           ;[1df7] ff
                    rst       $38                           ;[1df8] ff
                    rst       $38                           ;[1df9] ff
                    rst       $38                           ;[1dfa] ff
                    rst       $38                           ;[1dfb] ff
                    rst       $38                           ;[1dfc] ff
                    rst       $38                           ;[1dfd] ff
                    rst       $38                           ;[1dfe] ff
                    rst       $38                           ;[1dff] ff
                    rst       $38                           ;[1e00] ff
                    rst       $38                           ;[1e01] ff
                    rst       $38                           ;[1e02] ff
                    rst       $38                           ;[1e03] ff
                    rst       $38                           ;[1e04] ff
                    rst       $38                           ;[1e05] ff
                    rst       $38                           ;[1e06] ff
                    rst       $38                           ;[1e07] ff
                    rst       $38                           ;[1e08] ff
                    rst       $38                           ;[1e09] ff
                    rst       $38                           ;[1e0a] ff
                    rst       $38                           ;[1e0b] ff
                    rst       $38                           ;[1e0c] ff
                    rst       $38                           ;[1e0d] ff
                    rst       $38                           ;[1e0e] ff
                    rst       $38                           ;[1e0f] ff
                    rst       $38                           ;[1e10] ff
                    rst       $38                           ;[1e11] ff
                    rst       $38                           ;[1e12] ff
                    rst       $38                           ;[1e13] ff
                    rst       $38                           ;[1e14] ff
                    rst       $38                           ;[1e15] ff
                    rst       $38                           ;[1e16] ff
                    rst       $38                           ;[1e17] ff
                    rst       $38                           ;[1e18] ff
                    rst       $38                           ;[1e19] ff
                    rst       $38                           ;[1e1a] ff
                    rst       $38                           ;[1e1b] ff
                    rst       $38                           ;[1e1c] ff
                    rst       $38                           ;[1e1d] ff
                    rst       $38                           ;[1e1e] ff
                    rst       $38                           ;[1e1f] ff
                    rst       $38                           ;[1e20] ff
                    rst       $38                           ;[1e21] ff
                    rst       $38                           ;[1e22] ff
                    rst       $38                           ;[1e23] ff
                    rst       $38                           ;[1e24] ff
                    rst       $38                           ;[1e25] ff
                    rst       $38                           ;[1e26] ff
                    rst       $38                           ;[1e27] ff
                    rst       $38                           ;[1e28] ff
                    rst       $38                           ;[1e29] ff
                    rst       $38                           ;[1e2a] ff
                    rst       $38                           ;[1e2b] ff
                    rst       $38                           ;[1e2c] ff
                    rst       $38                           ;[1e2d] ff
                    rst       $38                           ;[1e2e] ff
                    rst       $38                           ;[1e2f] ff
                    rst       $38                           ;[1e30] ff
                    rst       $38                           ;[1e31] ff
                    rst       $38                           ;[1e32] ff
                    rst       $38                           ;[1e33] ff
                    rst       $38                           ;[1e34] ff
                    rst       $38                           ;[1e35] ff
                    rst       $38                           ;[1e36] ff
                    rst       $38                           ;[1e37] ff
                    rst       $38                           ;[1e38] ff
                    rst       $38                           ;[1e39] ff
                    rst       $38                           ;[1e3a] ff
                    rst       $38                           ;[1e3b] ff
                    rst       $38                           ;[1e3c] ff
                    rst       $38                           ;[1e3d] ff
                    rst       $38                           ;[1e3e] ff
                    rst       $38                           ;[1e3f] ff
                    rst       $38                           ;[1e40] ff
                    rst       $38                           ;[1e41] ff
                    rst       $38                           ;[1e42] ff
                    rst       $38                           ;[1e43] ff
                    rst       $38                           ;[1e44] ff
                    rst       $38                           ;[1e45] ff
                    rst       $38                           ;[1e46] ff
                    rst       $38                           ;[1e47] ff
                    rst       $38                           ;[1e48] ff
                    rst       $38                           ;[1e49] ff
                    rst       $38                           ;[1e4a] ff
                    rst       $38                           ;[1e4b] ff
                    rst       $38                           ;[1e4c] ff
                    rst       $38                           ;[1e4d] ff
                    rst       $38                           ;[1e4e] ff
                    rst       $38                           ;[1e4f] ff
                    rst       $38                           ;[1e50] ff
                    rst       $38                           ;[1e51] ff
                    rst       $38                           ;[1e52] ff
                    rst       $38                           ;[1e53] ff
                    rst       $38                           ;[1e54] ff
                    rst       $38                           ;[1e55] ff
                    rst       $38                           ;[1e56] ff
                    rst       $38                           ;[1e57] ff
                    rst       $38                           ;[1e58] ff
                    rst       $38                           ;[1e59] ff
                    rst       $38                           ;[1e5a] ff
                    rst       $38                           ;[1e5b] ff
                    rst       $38                           ;[1e5c] ff
                    rst       $38                           ;[1e5d] ff
                    rst       $38                           ;[1e5e] ff
                    rst       $38                           ;[1e5f] ff
                    rst       $38                           ;[1e60] ff
                    rst       $38                           ;[1e61] ff
                    rst       $38                           ;[1e62] ff
                    rst       $38                           ;[1e63] ff
                    rst       $38                           ;[1e64] ff
                    rst       $38                           ;[1e65] ff
                    rst       $38                           ;[1e66] ff
                    rst       $38                           ;[1e67] ff
                    rst       $38                           ;[1e68] ff
                    rst       $38                           ;[1e69] ff
                    rst       $38                           ;[1e6a] ff
                    rst       $38                           ;[1e6b] ff
                    rst       $38                           ;[1e6c] ff
                    rst       $38                           ;[1e6d] ff
                    rst       $38                           ;[1e6e] ff
                    rst       $38                           ;[1e6f] ff
                    rst       $38                           ;[1e70] ff
                    rst       $38                           ;[1e71] ff
                    rst       $38                           ;[1e72] ff
                    rst       $38                           ;[1e73] ff
                    rst       $38                           ;[1e74] ff
                    rst       $38                           ;[1e75] ff
                    rst       $38                           ;[1e76] ff
                    rst       $38                           ;[1e77] ff
                    rst       $38                           ;[1e78] ff
                    rst       $38                           ;[1e79] ff
                    rst       $38                           ;[1e7a] ff
                    rst       $38                           ;[1e7b] ff
                    rst       $38                           ;[1e7c] ff
                    rst       $38                           ;[1e7d] ff
                    rst       $38                           ;[1e7e] ff
                    rst       $38                           ;[1e7f] ff
                    rst       $38                           ;[1e80] ff
                    rst       $38                           ;[1e81] ff
                    rst       $38                           ;[1e82] ff
                    rst       $38                           ;[1e83] ff
                    rst       $38                           ;[1e84] ff
                    rst       $38                           ;[1e85] ff
                    rst       $38                           ;[1e86] ff
                    rst       $38                           ;[1e87] ff
                    rst       $38                           ;[1e88] ff
                    rst       $38                           ;[1e89] ff
                    rst       $38                           ;[1e8a] ff
                    rst       $38                           ;[1e8b] ff
                    rst       $38                           ;[1e8c] ff
                    rst       $38                           ;[1e8d] ff
                    rst       $38                           ;[1e8e] ff
                    rst       $38                           ;[1e8f] ff
                    rst       $38                           ;[1e90] ff
                    rst       $38                           ;[1e91] ff
                    rst       $38                           ;[1e92] ff
                    rst       $38                           ;[1e93] ff
                    rst       $38                           ;[1e94] ff
                    rst       $38                           ;[1e95] ff
                    rst       $38                           ;[1e96] ff
                    rst       $38                           ;[1e97] ff
                    rst       $38                           ;[1e98] ff
                    rst       $38                           ;[1e99] ff
                    rst       $38                           ;[1e9a] ff
                    rst       $38                           ;[1e9b] ff
                    rst       $38                           ;[1e9c] ff
                    rst       $38                           ;[1e9d] ff
                    rst       $38                           ;[1e9e] ff
                    rst       $38                           ;[1e9f] ff
                    rst       $38                           ;[1ea0] ff
                    rst       $38                           ;[1ea1] ff
                    rst       $38                           ;[1ea2] ff
                    rst       $38                           ;[1ea3] ff
                    rst       $38                           ;[1ea4] ff
                    rst       $38                           ;[1ea5] ff
                    rst       $38                           ;[1ea6] ff
                    rst       $38                           ;[1ea7] ff
                    rst       $38                           ;[1ea8] ff
                    rst       $38                           ;[1ea9] ff
                    rst       $38                           ;[1eaa] ff
                    rst       $38                           ;[1eab] ff
                    rst       $38                           ;[1eac] ff
                    rst       $38                           ;[1ead] ff
                    rst       $38                           ;[1eae] ff
                    rst       $38                           ;[1eaf] ff
                    rst       $38                           ;[1eb0] ff
                    rst       $38                           ;[1eb1] ff
                    rst       $38                           ;[1eb2] ff
                    rst       $38                           ;[1eb3] ff
                    rst       $38                           ;[1eb4] ff
                    rst       $38                           ;[1eb5] ff
                    rst       $38                           ;[1eb6] ff
                    rst       $38                           ;[1eb7] ff
                    rst       $38                           ;[1eb8] ff
                    rst       $38                           ;[1eb9] ff
                    rst       $38                           ;[1eba] ff
                    rst       $38                           ;[1ebb] ff
                    rst       $38                           ;[1ebc] ff
                    rst       $38                           ;[1ebd] ff
                    rst       $38                           ;[1ebe] ff
                    rst       $38                           ;[1ebf] ff
                    rst       $38                           ;[1ec0] ff
                    rst       $38                           ;[1ec1] ff
                    rst       $38                           ;[1ec2] ff
                    rst       $38                           ;[1ec3] ff
                    rst       $38                           ;[1ec4] ff
                    rst       $38                           ;[1ec5] ff
                    rst       $38                           ;[1ec6] ff
                    rst       $38                           ;[1ec7] ff
                    rst       $38                           ;[1ec8] ff
                    rst       $38                           ;[1ec9] ff
                    rst       $38                           ;[1eca] ff
                    rst       $38                           ;[1ecb] ff
                    rst       $38                           ;[1ecc] ff
                    rst       $38                           ;[1ecd] ff
                    rst       $38                           ;[1ece] ff
                    rst       $38                           ;[1ecf] ff
                    rst       $38                           ;[1ed0] ff
                    rst       $38                           ;[1ed1] ff
                    rst       $38                           ;[1ed2] ff
                    rst       $38                           ;[1ed3] ff
                    rst       $38                           ;[1ed4] ff
                    rst       $38                           ;[1ed5] ff
                    rst       $38                           ;[1ed6] ff
                    rst       $38                           ;[1ed7] ff
                    rst       $38                           ;[1ed8] ff
                    rst       $38                           ;[1ed9] ff
                    rst       $38                           ;[1eda] ff
                    rst       $38                           ;[1edb] ff
                    rst       $38                           ;[1edc] ff
                    rst       $38                           ;[1edd] ff
                    rst       $38                           ;[1ede] ff
                    rst       $38                           ;[1edf] ff
                    rst       $38                           ;[1ee0] ff
                    rst       $38                           ;[1ee1] ff
                    rst       $38                           ;[1ee2] ff
                    rst       $38                           ;[1ee3] ff
                    rst       $38                           ;[1ee4] ff
                    rst       $38                           ;[1ee5] ff
                    rst       $38                           ;[1ee6] ff
                    rst       $38                           ;[1ee7] ff
                    rst       $38                           ;[1ee8] ff
                    rst       $38                           ;[1ee9] ff
                    rst       $38                           ;[1eea] ff
                    rst       $38                           ;[1eeb] ff
                    rst       $38                           ;[1eec] ff
                    rst       $38                           ;[1eed] ff
                    rst       $38                           ;[1eee] ff
                    rst       $38                           ;[1eef] ff
                    rst       $38                           ;[1ef0] ff
                    rst       $38                           ;[1ef1] ff
                    rst       $38                           ;[1ef2] ff
                    rst       $38                           ;[1ef3] ff
                    rst       $38                           ;[1ef4] ff
                    rst       $38                           ;[1ef5] ff
                    rst       $38                           ;[1ef6] ff
                    rst       $38                           ;[1ef7] ff
                    rst       $38                           ;[1ef8] ff
                    rst       $38                           ;[1ef9] ff
                    rst       $38                           ;[1efa] ff
                    rst       $38                           ;[1efb] ff
                    rst       $38                           ;[1efc] ff
                    rst       $38                           ;[1efd] ff
                    rst       $38                           ;[1efe] ff
                    rst       $38                           ;[1eff] ff
                    rst       $38                           ;[1f00] ff
                    rst       $38                           ;[1f01] ff
                    rst       $38                           ;[1f02] ff
                    rst       $38                           ;[1f03] ff
                    rst       $38                           ;[1f04] ff
                    rst       $38                           ;[1f05] ff
                    rst       $38                           ;[1f06] ff
                    rst       $38                           ;[1f07] ff
                    rst       $38                           ;[1f08] ff
                    rst       $38                           ;[1f09] ff
                    rst       $38                           ;[1f0a] ff
                    rst       $38                           ;[1f0b] ff
                    rst       $38                           ;[1f0c] ff
                    rst       $38                           ;[1f0d] ff
                    rst       $38                           ;[1f0e] ff
                    rst       $38                           ;[1f0f] ff
                    rst       $38                           ;[1f10] ff
                    rst       $38                           ;[1f11] ff
                    rst       $38                           ;[1f12] ff
                    rst       $38                           ;[1f13] ff
                    rst       $38                           ;[1f14] ff
                    rst       $38                           ;[1f15] ff
                    rst       $38                           ;[1f16] ff
                    rst       $38                           ;[1f17] ff
                    rst       $38                           ;[1f18] ff
                    rst       $38                           ;[1f19] ff
                    rst       $38                           ;[1f1a] ff
                    rst       $38                           ;[1f1b] ff
                    rst       $38                           ;[1f1c] ff
                    rst       $38                           ;[1f1d] ff
                    rst       $38                           ;[1f1e] ff
                    rst       $38                           ;[1f1f] ff
                    rst       $38                           ;[1f20] ff
                    rst       $38                           ;[1f21] ff
                    rst       $38                           ;[1f22] ff
                    rst       $38                           ;[1f23] ff
                    rst       $38                           ;[1f24] ff
                    rst       $38                           ;[1f25] ff
                    rst       $38                           ;[1f26] ff
                    rst       $38                           ;[1f27] ff
                    rst       $38                           ;[1f28] ff
                    rst       $38                           ;[1f29] ff
                    rst       $38                           ;[1f2a] ff
                    rst       $38                           ;[1f2b] ff
                    rst       $38                           ;[1f2c] ff
                    rst       $38                           ;[1f2d] ff
                    rst       $38                           ;[1f2e] ff
                    rst       $38                           ;[1f2f] ff
                    rst       $38                           ;[1f30] ff
                    rst       $38                           ;[1f31] ff
                    rst       $38                           ;[1f32] ff
                    rst       $38                           ;[1f33] ff
                    rst       $38                           ;[1f34] ff
                    rst       $38                           ;[1f35] ff
                    rst       $38                           ;[1f36] ff
                    rst       $38                           ;[1f37] ff
                    rst       $38                           ;[1f38] ff
                    rst       $38                           ;[1f39] ff
                    rst       $38                           ;[1f3a] ff
                    rst       $38                           ;[1f3b] ff
                    rst       $38                           ;[1f3c] ff
                    rst       $38                           ;[1f3d] ff
                    rst       $38                           ;[1f3e] ff
                    rst       $38                           ;[1f3f] ff
                    rst       $38                           ;[1f40] ff
                    rst       $38                           ;[1f41] ff
                    rst       $38                           ;[1f42] ff
                    rst       $38                           ;[1f43] ff
                    rst       $38                           ;[1f44] ff
                    rst       $38                           ;[1f45] ff
                    rst       $38                           ;[1f46] ff
                    rst       $38                           ;[1f47] ff
                    rst       $38                           ;[1f48] ff
                    rst       $38                           ;[1f49] ff
                    rst       $38                           ;[1f4a] ff
                    rst       $38                           ;[1f4b] ff
                    rst       $38                           ;[1f4c] ff
                    rst       $38                           ;[1f4d] ff
                    rst       $38                           ;[1f4e] ff
                    rst       $38                           ;[1f4f] ff
                    rst       $38                           ;[1f50] ff
                    rst       $38                           ;[1f51] ff
                    rst       $38                           ;[1f52] ff
                    rst       $38                           ;[1f53] ff
                    rst       $38                           ;[1f54] ff
                    rst       $38                           ;[1f55] ff
                    rst       $38                           ;[1f56] ff
                    rst       $38                           ;[1f57] ff
                    rst       $38                           ;[1f58] ff
                    rst       $38                           ;[1f59] ff
                    rst       $38                           ;[1f5a] ff
                    rst       $38                           ;[1f5b] ff
                    rst       $38                           ;[1f5c] ff
                    rst       $38                           ;[1f5d] ff
                    rst       $38                           ;[1f5e] ff
                    rst       $38                           ;[1f5f] ff
                    rst       $38                           ;[1f60] ff
                    rst       $38                           ;[1f61] ff
                    rst       $38                           ;[1f62] ff
                    rst       $38                           ;[1f63] ff
                    rst       $38                           ;[1f64] ff
                    rst       $38                           ;[1f65] ff
                    rst       $38                           ;[1f66] ff
                    rst       $38                           ;[1f67] ff
                    rst       $38                           ;[1f68] ff
                    rst       $38                           ;[1f69] ff
                    rst       $38                           ;[1f6a] ff
                    rst       $38                           ;[1f6b] ff
                    rst       $38                           ;[1f6c] ff
                    rst       $38                           ;[1f6d] ff
                    rst       $38                           ;[1f6e] ff
                    rst       $38                           ;[1f6f] ff
                    rst       $38                           ;[1f70] ff
                    rst       $38                           ;[1f71] ff
                    rst       $38                           ;[1f72] ff
                    rst       $38                           ;[1f73] ff
                    rst       $38                           ;[1f74] ff
                    rst       $38                           ;[1f75] ff
                    rst       $38                           ;[1f76] ff
                    rst       $38                           ;[1f77] ff
                    rst       $38                           ;[1f78] ff
                    rst       $38                           ;[1f79] ff
                    rst       $38                           ;[1f7a] ff
                    rst       $38                           ;[1f7b] ff
                    rst       $38                           ;[1f7c] ff
                    rst       $38                           ;[1f7d] ff
                    rst       $38                           ;[1f7e] ff
                    rst       $38                           ;[1f7f] ff
                    rst       $38                           ;[1f80] ff
                    rst       $38                           ;[1f81] ff
                    rst       $38                           ;[1f82] ff
                    rst       $38                           ;[1f83] ff
                    rst       $38                           ;[1f84] ff
                    rst       $38                           ;[1f85] ff
                    rst       $38                           ;[1f86] ff
                    rst       $38                           ;[1f87] ff
                    rst       $38                           ;[1f88] ff
                    rst       $38                           ;[1f89] ff
                    rst       $38                           ;[1f8a] ff
                    rst       $38                           ;[1f8b] ff
                    rst       $38                           ;[1f8c] ff
                    rst       $38                           ;[1f8d] ff
                    rst       $38                           ;[1f8e] ff
                    rst       $38                           ;[1f8f] ff
                    rst       $38                           ;[1f90] ff
                    rst       $38                           ;[1f91] ff
                    rst       $38                           ;[1f92] ff
                    rst       $38                           ;[1f93] ff
                    rst       $38                           ;[1f94] ff
                    rst       $38                           ;[1f95] ff
                    rst       $38                           ;[1f96] ff
                    rst       $38                           ;[1f97] ff
                    rst       $38                           ;[1f98] ff
                    rst       $38                           ;[1f99] ff
                    rst       $38                           ;[1f9a] ff
                    rst       $38                           ;[1f9b] ff
                    rst       $38                           ;[1f9c] ff
                    rst       $38                           ;[1f9d] ff
                    rst       $38                           ;[1f9e] ff
                    rst       $38                           ;[1f9f] ff
                    rst       $38                           ;[1fa0] ff
                    rst       $38                           ;[1fa1] ff
                    rst       $38                           ;[1fa2] ff
                    rst       $38                           ;[1fa3] ff
                    rst       $38                           ;[1fa4] ff
                    rst       $38                           ;[1fa5] ff
                    rst       $38                           ;[1fa6] ff
                    rst       $38                           ;[1fa7] ff
                    rst       $38                           ;[1fa8] ff
                    rst       $38                           ;[1fa9] ff
                    rst       $38                           ;[1faa] ff
                    rst       $38                           ;[1fab] ff
                    rst       $38                           ;[1fac] ff
                    rst       $38                           ;[1fad] ff
                    rst       $38                           ;[1fae] ff
                    rst       $38                           ;[1faf] ff
                    rst       $38                           ;[1fb0] ff
                    rst       $38                           ;[1fb1] ff
                    rst       $38                           ;[1fb2] ff
                    rst       $38                           ;[1fb3] ff
                    rst       $38                           ;[1fb4] ff
                    rst       $38                           ;[1fb5] ff
                    rst       $38                           ;[1fb6] ff
                    rst       $38                           ;[1fb7] ff
                    rst       $38                           ;[1fb8] ff
                    rst       $38                           ;[1fb9] ff
                    rst       $38                           ;[1fba] ff
                    rst       $38                           ;[1fbb] ff
                    rst       $38                           ;[1fbc] ff
                    rst       $38                           ;[1fbd] ff
                    rst       $38                           ;[1fbe] ff
                    rst       $38                           ;[1fbf] ff
                    rst       $38                           ;[1fc0] ff
                    rst       $38                           ;[1fc1] ff
                    rst       $38                           ;[1fc2] ff
                    rst       $38                           ;[1fc3] ff
                    rst       $38                           ;[1fc4] ff
                    rst       $38                           ;[1fc5] ff
                    rst       $38                           ;[1fc6] ff
                    rst       $38                           ;[1fc7] ff
                    rst       $38                           ;[1fc8] ff
                    rst       $38                           ;[1fc9] ff
                    rst       $38                           ;[1fca] ff
                    rst       $38                           ;[1fcb] ff
                    rst       $38                           ;[1fcc] ff
                    rst       $38                           ;[1fcd] ff
                    rst       $38                           ;[1fce] ff
                    rst       $38                           ;[1fcf] ff
                    rst       $38                           ;[1fd0] ff
                    rst       $38                           ;[1fd1] ff
                    rst       $38                           ;[1fd2] ff
                    rst       $38                           ;[1fd3] ff
                    rst       $38                           ;[1fd4] ff
                    rst       $38                           ;[1fd5] ff
                    rst       $38                           ;[1fd6] ff
                    rst       $38                           ;[1fd7] ff
                    rst       $38                           ;[1fd8] ff
                    rst       $38                           ;[1fd9] ff
                    rst       $38                           ;[1fda] ff
                    rst       $38                           ;[1fdb] ff
                    rst       $38                           ;[1fdc] ff
                    rst       $38                           ;[1fdd] ff
                    rst       $38                           ;[1fde] ff
                    rst       $38                           ;[1fdf] ff
                    rst       $38                           ;[1fe0] ff
                    rst       $38                           ;[1fe1] ff
                    rst       $38                           ;[1fe2] ff
                    rst       $38                           ;[1fe3] ff
                    rst       $38                           ;[1fe4] ff
                    rst       $38                           ;[1fe5] ff
                    rst       $38                           ;[1fe6] ff
                    rst       $38                           ;[1fe7] ff
                    rst       $38                           ;[1fe8] ff
                    rst       $38                           ;[1fe9] ff
                    rst       $38                           ;[1fea] ff
                    rst       $38                           ;[1feb] ff
                    rst       $38                           ;[1fec] ff
                    rst       $38                           ;[1fed] ff
                    rst       $38                           ;[1fee] ff
                    rst       $38                           ;[1fef] ff
                    rst       $38                           ;[1ff0] ff
                    rst       $38                           ;[1ff1] ff
                    rst       $38                           ;[1ff2] ff
                    rst       $38                           ;[1ff3] ff
                    rst       $38                           ;[1ff4] ff
                    rst       $38                           ;[1ff5] ff
                    rst       $38                           ;[1ff6] ff
                    rst       $38                           ;[1ff7] ff
                    rst       $38                           ;[1ff8] ff
                    rst       $38                           ;[1ff9] ff
                    rst       $38                           ;[1ffa] ff
                    rst       $38                           ;[1ffb] ff
                    rst       $38                           ;[1ffc] ff
                    rst       $38                           ;[1ffd] ff
                    rst       $38                           ;[1ffe] ff
                    rst       $38                           ;[1fff] ff
