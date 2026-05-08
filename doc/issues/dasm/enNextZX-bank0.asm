                    di                                      ;[0000] f3
                    jp        $00ef                         ;[0001] c3 ef 00
                    ld        b,l                           ;[0004] 45
                    ld        b,h                           ;[0005] 44
                    add       hl,bc                         ;[0006] 09
                    ld        (bc),a                        ;[0007] 02
                    jp        $103b                         ;[0008] c3 3b 10
                    ld        hl,($2a2e)                    ;[000b] 2a 2e 2a
                    rst       $38                           ;[000e] ff
                    nop                                     ;[000f] 00
                    rst       $28                           ;[0010] ef
                    djnz      $0013                         ;[0011] 10 00
                    ret                                     ;[0013] c9

                    jp        $00ef                         ;[0014] c3 ef 00
                    nop                                     ;[0017] 00
                    jp        $3e80                         ;[0018] c3 80 3e
                    inc       a                             ;[001b] 3c
                    ld        b,h                           ;[001c] 44
                    ld        c,c                           ;[001d] 49
                    ld        d,d                           ;[001e] 52
                    cp        (hl)                          ;[001f] be
                    jp        $3e00                         ;[0020] c3 00 3e
                    inc       d                             ;[0023] 14
                    nop                                     ;[0024] 00
                    inc       b                             ;[0025] 04
                    sbc       b                             ;[0026] 98
                    nop                                     ;[0027] 00
                    ld        ($5b54),bc                    ;[0028] ed 43 54 5b
                    ex        (sp),hl                       ;[002c] e3
                    jp        $0080                         ;[002d] c3 80 00
                    jp        $1024                         ;[0030] c3 24 10
                    call      $04d7                         ;[0033] cd d7 04
                    ld        (hl),a                        ;[0036] 77
                    ret                                     ;[0037] c9

                    push      af                            ;[0038] f5
                    push      hl                            ;[0039] e5
                    ld        h,$00                         ;[003a] 26 00
                    ld        a,$80                         ;[003c] 3e 80
                    jp        $0046                         ;[003e] c3 46 00
                    inc       a                             ;[0041] 3c
                    ld        d,d                           ;[0042] 52
                    ld        d,l                           ;[0043] 55
                    ld        c,(hl)                        ;[0044] 4e
                    cp        (hl)                          ;[0045] be
                    out       ($e3),a                       ;[0046] d3 e3
                    dec       b                             ;[0048] 05
                    ld        a,(de)                        ;[0049] 1a
                    rla                                     ;[004a] 17
                    rst       $38                           ;[004b] ff
                    rlca                                    ;[004c] 07
                    add       a                             ;[004d] 87
                    nop                                     ;[004e] 00
                    nop                                     ;[004f] 00
                    nop                                     ;[0050] 00
                    nop                                     ;[0051] 00
                    nop                                     ;[0052] 00
                    nop                                     ;[0053] 00
                    nop                                     ;[0054] 00
                    nop                                     ;[0055] 00
                    nop                                     ;[0056] 00
                    nop                                     ;[0057] 00
                    nop                                     ;[0058] 00
                    nop                                     ;[0059] 00
                    nop                                     ;[005a] 00
                    nop                                     ;[005b] 00
                    nop                                     ;[005c] 00
                    nop                                     ;[005d] 00
                    nop                                     ;[005e] 00
                    nop                                     ;[005f] 00
                    pop       hl                            ;[0060] e1
                    pop       af                            ;[0061] f1
                    ei                                      ;[0062] fb
                    ret                                     ;[0063] c9

                    nop                                     ;[0064] 00
                    nop                                     ;[0065] 00
                    retn                                    ;[0066] ed 45

                    ld        ($5b54),bc                    ;[0068] ed 43 54 5b
                    ex        (sp),hl                       ;[006c] e3
                    ld        c,(hl)                        ;[006d] 4e
                    inc       hl                            ;[006e] 23
                    ld        b,(hl)                        ;[006f] 46
                    inc       hl                            ;[0070] 23
                    ex        (sp),hl                       ;[0071] e3
                    push    $007b                           ;[0072] ed 8a 00 7b
                    push      bc                            ;[0076] c5
                    ld        bc,($5b54)                    ;[0077] ed 4b 54 5b
                    nextreg $8c,$80                         ;[007b] ed 91 8c 80
                    ret                                     ;[007f] c9

                    ld        c,(hl)                        ;[0080] 4e
                    inc       hl                            ;[0081] 23
                    ld        b,(hl)                        ;[0082] 46
                    inc       hl                            ;[0083] 23
                    ex        (sp),hl                       ;[0084] e3
                    push    $5b4d                           ;[0085] ed 8a 5b 4d
                    push      bc                            ;[0089] c5
                    ld        bc,($5b54)                    ;[008a] ed 4b 54 5b
                    jp        $5b48                         ;[008e] c3 48 5b
                    push      af                            ;[0091] f5
                    push      bc                            ;[0092] c5
                    ld        bc,$7ffd                      ;[0093] 01 fd 7f
                    ld        a,($5b5c)                     ;[0096] 3a 5c 5b
                    xor       $10                           ;[0099] ee 10
                    di                                      ;[009b] f3
                    ld        ($5b5c),a                     ;[009c] 32 5c 5b
                    out       (c),a                         ;[009f] ed 79
                    ld        bc,$1ffd                      ;[00a1] 01 fd 1f
                    ld        a,($5b67)                     ;[00a4] 3a 67 5b
                    xor       $04                           ;[00a7] ee 04
                    ld        ($5b67),a                     ;[00a9] 32 67 5b
                    out       (c),a                         ;[00ac] ed 79
                    ei                                      ;[00ae] fb
                    pop       bc                            ;[00af] c1
                    pop       af                            ;[00b0] f1
                    ret                                     ;[00b1] c9

                    call      $5b00                         ;[00b2] cd 00 5b
                    push      hl                            ;[00b5] e5
                    ld        hl,($5b5a)                    ;[00b6] 2a 5a 5b
                    ex        (sp),hl                       ;[00b9] e3
                    ret                                     ;[00ba] c9

                    push      hl                            ;[00bb] e5
                    ld        hl,$5b34                      ;[00bc] 21 34 5b
                    ex        (sp),hl                       ;[00bf] e3
                    push      af                            ;[00c0] f5
                    push      bc                            ;[00c1] c5
                    jr        $00a1                         ;[00c2] 18 dd
                    nop                                     ;[00c4] 00
                    push      hl                            ;[00c5] e5
                    ld        hl,($5b5a)                    ;[00c6] 2a 5a 5b
                    ex        (sp),hl                       ;[00c9] e3
                    ret                                     ;[00ca] c9

                    push    $0a9e                           ;[00cb] ed 8a 0a 9e
                    nextreg $8e,$01                         ;[00cf] ed 91 8e 01
                    ret                                     ;[00d3] c9

                    nextreg $8e,$02                         ;[00d4] ed 91 8e 02
                    ret                                     ;[00d8] c9

                    nextreg $8e,$03                         ;[00d9] ed 91 8e 03
                    ret                                     ;[00dd] c9

                    nextreg $8e,$00                         ;[00de] ed 91 8e 00
                    ret                                     ;[00e2] c9

                    ld        hl,$0091                      ;[00e3] 21 91 00
                    ld        de,$5b00                      ;[00e6] 11 00 5b
                    ld        bc,$0052                      ;[00e9] 01 52 00
                    ldir                                    ;[00ec] ed b0
                    ret                                     ;[00ee] c9

                    nextreg $07,$03                         ;[00ef] ed 91 07 03
                    nextreg $03,$b0                         ;[00f3] ed 91 03 b0
                    nextreg $c0,$08                         ;[00f7] ed 91 c0 08
                    ld        a,$ff                         ;[00fb] 3e ff
                    nextreg $82,a                           ;[00fd] ed 92 82
                    nextreg $83,a                           ;[0100] ed 92 83
                    nextreg $84,a                           ;[0103] ed 92 84
                    nextreg $85,a                           ;[0106] ed 92 85
                    xor       a                             ;[0109] af
                    nextreg $80,a                           ;[010a] ed 92 80
                    nextreg $81,a                           ;[010d] ed 92 81
                    nextreg $8a,a                           ;[0110] ed 92 8a
                    nextreg $8f,a                           ;[0113] ed 92 8f
                    ld        bc,$243b                      ;[0116] 01 3b 24
                    ld        d,$06                         ;[0119] 16 06
                    out       (c),d                         ;[011b] ed 51
                    inc       b                             ;[011d] 04
                    in        a,(c)                         ;[011e] ed 78
                    and       $44                           ;[0120] e6 44
                    out       (c),a                         ;[0122] ed 79
                    ld        hl,$5800                      ;[0124] 21 00 58
                    ld        de,$5801                      ;[0127] 11 01 58
                    ld        (hl),l                        ;[012a] 75
                    ld        bc,$02ff                      ;[012b] 01 ff 02
                    ldir                                    ;[012e] ed b0
                    ld        bc,$7000                      ;[0130] 01 00 70
                    ld        hl,$4000                      ;[0133] 21 00 40
                    ld        a,c                           ;[0136] 79
                    exx                                     ;[0137] d9
                    add       a                             ;[0138] 87
                    nextreg $56,a                           ;[0139] ed 92 56
                    inc       a                             ;[013c] 3c
                    nextreg $57,a                           ;[013d] ed 92 57
                    srl       a                             ;[0140] cb 3f
                    ld        hl,$ffff                      ;[0142] 21 ff ff
                    ex        af,af'                        ;[0145] 08
                    ld        a,(hl)                        ;[0146] 7e
                    exx                                     ;[0147] d9
                    ld        (hl),a                        ;[0148] 77
                    ld        a,c                           ;[0149] 79
                    cp        $0c                           ;[014a] fe 0c
                    jr        nc,$0150                      ;[014c] 30 02
                    ld        (hl),$00                      ;[014e] 36 00
                    inc       hl                            ;[0150] 23
                    exx                                     ;[0151] d9
                    ex        af,af'                        ;[0152] 08
                    ld        (hl),$bb                      ;[0153] 36 bb
                    dec       hl                            ;[0155] 2b
                    ld        de,$fffd                      ;[0156] 11 fd ff
                    ld        bc,$3ffe                      ;[0159] 01 fe 3f
                    cp        $0c                           ;[015c] fe 0c
                    jr        nc,$016a                      ;[015e] 30 0a
                    cp        $08                           ;[0160] fe 08
                    jr        nz,$0166                      ;[0162] 20 02
                    ld        b,$1f                         ;[0164] 06 1f
                    ld        (hl),$00                      ;[0166] 36 00
                    lddr                                    ;[0168] ed b8
                    exx                                     ;[016a] d9
                    inc       c                             ;[016b] 0c
                    djnz      $0136                         ;[016c] 10 c8
                    jr        $018e                         ;[016e] 18 1e
                    ex        af,af'                        ;[0170] 08
                    ld        a,$08                         ;[0171] 3e 08
                    sub       e                             ;[0173] 93
                    ld        l,a                           ;[0174] 6f
                    ex        af,af'                        ;[0175] 08
                    ld        a,l                           ;[0176] 7d
                    jr        nz,$017d                      ;[0177] 20 04
                    out       ($fe),a                       ;[0179] d3 fe
                    jr        $017b                         ;[017b] 18 fe
                    xor       $07                           ;[017d] ee 07
                    ld        h,a                           ;[017f] 67
                    ld        b,$20                         ;[0180] 06 20
                    ld        a,h                           ;[0182] 7c
                    out       ($fe),a                       ;[0183] d3 fe
                    djnz      $0182                         ;[0185] 10 fb
                    ld        a,l                           ;[0187] 7d
                    out       ($fe),a                       ;[0188] d3 fe
                    djnz      $0187                         ;[018a] 10 fb
                    jr        $0180                         ;[018c] 18 f2
                    xor       a                             ;[018e] af
                    ld        bc,$4000                      ;[018f] 01 00 40
                    ld        de,$0108                      ;[0192] 11 08 01
                    add       a                             ;[0195] 87
                    nextreg $56,a                           ;[0196] ed 92 56
                    inc       a                             ;[0199] 3c
                    nextreg $57,a                           ;[019a] ed 92 57
                    srl       a                             ;[019d] cb 3f
                    ex        af,af'                        ;[019f] 08
                    ld        hl,$ffff                      ;[01a0] 21 ff ff
                    ld        a,(hl)                        ;[01a3] 7e
                    cp        $bb                           ;[01a4] fe bb
                    jr        nz,$01cc                      ;[01a6] 20 24
                    ld        a,(bc)                        ;[01a8] 0a
                    inc       bc                            ;[01a9] 03
                    ld        (hl),a                        ;[01aa] 77
                    ld        hl,$dcba                      ;[01ab] 21 ba dc
                    ld        a,(hl)                        ;[01ae] 7e
                    ld        ixl,a                         ;[01af] dd 6f
                    ld        a,d                           ;[01b1] 7a
                    ld        (hl),a                        ;[01b2] 77
                    ld        a,(hl)                        ;[01b3] 7e
                    and       d                             ;[01b4] a2
                    jr        z,$0170                       ;[01b5] 28 b9
                    cpl                                     ;[01b7] 2f
                    ld        (hl),a                        ;[01b8] 77
                    ld        a,(hl)                        ;[01b9] 7e
                    and       d                             ;[01ba] a2
                    jr        nz,$0170                      ;[01bb] 20 b3
                    rlc       d                             ;[01bd] cb 02
                    dec       e                             ;[01bf] 1d
                    jr        nz,$01b1                      ;[01c0] 20 ef
                    ld        a,ixl                         ;[01c2] dd 7d
                    ld        (hl),a                        ;[01c4] 77
                    ex        af,af'                        ;[01c5] 08
                    inc       a                             ;[01c6] 3c
                    cp        $70                           ;[01c7] fe 70
                    jr        nz,$0192                      ;[01c9] 20 c7
                    ex        af,af'                        ;[01cb] 08
                    ex        af,af'                        ;[01cc] 08
                    dec       a                             ;[01cd] 3d
                    ld        ($5b69),a                     ;[01ce] 32 69 5b
                    ld        sp,$5bff                      ;[01d1] 31 ff 5b
                    rst       $20                           ;[01d4] e7
                    ld        bc,$ed1f                      ;[01d5] 01 1f ed
                    sub       c                             ;[01d8] 91
                    adc       (hl)                          ;[01d9] 8e
                    ex        af,af'                        ;[01da] 08
                    im        1                             ;[01db] ed 56
                    call      $00e3                         ;[01dd] cd e3 00
                    ld        hl,$ffff                      ;[01e0] 21 ff ff
                    ld        ($5cb4),hl                    ;[01e3] 22 b4 5c
                    ld        de,$3eaf                      ;[01e6] 11 af 3e
                    ld        bc,$00a8                      ;[01e9] 01 a8 00
                    ex        de,hl                         ;[01ec] eb
                    rst       $28                           ;[01ed] ef
                    ld        h,c                           ;[01ee] 61
                    ld        d,$eb                         ;[01ef] 16 eb
                    inc       hl                            ;[01f1] 23
                    ld        ($5c7b),hl                    ;[01f2] 22 7b 5c
                    dec       hl                            ;[01f5] 2b
                    ld        ($5cb2),hl                    ;[01f6] 22 b2 5c
                    ld        b,$00                         ;[01f9] 06 00
                    ld        iy,$5c3a                      ;[01fb] fd 21 3a 5c
                    ld        a,($5b69)                     ;[01ff] 3a 69 5b
                    ld        c,a                           ;[0202] 4f
                    ld        hl,($5c7b)                    ;[0203] 2a 7b 5c
                    exx                                     ;[0206] d9
                    nextreg $b8,$82                         ;[0207] ed 91 b8 82
                    nextreg $b9,$00                         ;[020b] ed 91 b9 00
                    nextreg $ba,$00                         ;[020f] ed 91 ba 00
                    nextreg $bb,$f2                         ;[0213] ed 91 bb f2
                    nextreg $d8,$01                         ;[0217] ed 91 d8 01
                    ld        a,$05                         ;[021b] 3e 05
                    call      $0d6b                         ;[021d] cd 6b 0d
                    and       $05                           ;[0220] e6 05
                    or        $5a                           ;[0222] f6 5a
                    out       (c),a                         ;[0224] ed 79
                    ld        a,$08                         ;[0226] 3e 08
                    call      $0d6b                         ;[0228] cd 6b 0d
                    or        $4e                           ;[022b] f6 4e
                    out       (c),a                         ;[022d] ed 79
                    ld        a,$06                         ;[022f] 3e 06
                    call      $0d6b                         ;[0231] cd 6b 0d
                    and       $44                           ;[0234] e6 44
                    or        $ab                           ;[0236] f6 ab
                    out       (c),a                         ;[0238] ed 79
                    and       $fc                           ;[023a] e6 fc
                    out       (c),a                         ;[023c] ed 79
                    ld        a,$0a                         ;[023e] 3e 0a
                    call      $0d6b                         ;[0240] cd 6b 0d
                    or        $10                           ;[0243] f6 10
                    out       (c),a                         ;[0245] ed 79
                    call      $00e3                         ;[0247] cd e3 00
                    ld        h,d                           ;[024a] 62
                    ld        l,e                           ;[024b] 6b
                    ld        (hl),c                        ;[024c] 71
                    inc       de                            ;[024d] 13
                    ld        bc,$015f                      ;[024e] 01 5f 01
                    ldir                                    ;[0251] ed b0
                    ld        hl,$5bff                      ;[0253] 21 ff 5b
                    ld        ($5b6a),hl                    ;[0256] 22 6a 5b
                    ld        sp,hl                         ;[0259] f9
                    rst       $18                           ;[025a] df
                    add       l                             ;[025b] 85
                    inc       (hl)                          ;[025c] 34
                    ld        a,$cf                         ;[025d] 3e cf
                    ld        ($5b5d),a                     ;[025f] 32 5d 5b
                    ld        hl,$0040                      ;[0262] 21 40 00
                    ld        ($5c38),hl                    ;[0265] 22 38 5c
                    exx                                     ;[0268] d9
                    ld        a,c                           ;[0269] 79
                    ld        ($5b69),a                     ;[026a] 32 69 5b
                    ld        ($5c7b),hl                    ;[026d] 22 7b 5c
                    push      bc                            ;[0270] c5
                    call      $2341                         ;[0271] cd 41 23
                    rst       $18                           ;[0274] df
                    nop                                     ;[0275] 00
                    dec       d                             ;[0276] 15
                    ld        hl,$3c00                      ;[0277] 21 00 3c
                    ld        ($5c36),hl                    ;[027a] 22 36 5c
                    set       4,(iy+$01)                    ;[027d] fd cb 01 e6
                    ld        a,$ff                         ;[0281] 3e ff
                    ld        (iy+$00),a                    ;[0283] fd 77 00
                    ld        ($5b77),a                     ;[0286] 32 77 5b
                    ld        ($5b78),a                     ;[0289] 32 78 5b
                    ld        ($5b88),a                     ;[028c] 32 88 5b
                    ld        ($5b89),a                     ;[028f] 32 89 5b
                    ld        a,$54                         ;[0292] 3e 54
                    ld        ($5b79),a                     ;[0294] 32 79 5b
                    ld        ($5b7a),a                     ;[0297] 32 7a 5b
                    ld        hl,$5cb6                      ;[029a] 21 b6 5c
                    ld        ($5c4f),hl                    ;[029d] 22 4f 5c
                    ld        de,$15af                      ;[02a0] 11 af 15
                    ld        bc,$0015                      ;[02a3] 01 15 00
                    ex        de,hl                         ;[02a6] eb
                    rst       $28                           ;[02a7] ef
                    jp        $eb33                         ;[02a8] c3 33 eb
                    dec       hl                            ;[02ab] 2b
                    ld        ($5c57),hl                    ;[02ac] 22 57 5c
                    inc       hl                            ;[02af] 23
                    ld        ($5c53),hl                    ;[02b0] 22 53 5c
                    ld        ($5c4b),hl                    ;[02b3] 22 4b 5c
                    ld        (hl),$80                      ;[02b6] 36 80
                    inc       hl                            ;[02b8] 23
                    ld        ($5c59),hl                    ;[02b9] 22 59 5c
                    ld        (hl),$0d                      ;[02bc] 36 0d
                    inc       hl                            ;[02be] 23
                    ld        (hl),$80                      ;[02bf] 36 80
                    inc       hl                            ;[02c1] 23
                    ld        ($5c61),hl                    ;[02c2] 22 61 5c
                    ld        ($5c63),hl                    ;[02c5] 22 63 5c
                    ld        ($5c65),hl                    ;[02c8] 22 65 5c
                    ld        a,$38                         ;[02cb] 3e 38
                    ld        ($5c8d),a                     ;[02cd] 32 8d 5c
                    ld        ($5c8f),a                     ;[02d0] 32 8f 5c
                    ld        ($5b61),a                     ;[02d3] 32 61 5b
                    ld        ($5b63),a                     ;[02d6] 32 63 5b
                    ld        ($5c48),a                     ;[02d9] 32 48 5c
                    ld        a,$07                         ;[02dc] 3e 07
                    out       ($fe),a                       ;[02de] d3 fe
                    ld        hl,$0523                      ;[02e0] 21 23 05
                    ld        ($5c09),hl                    ;[02e3] 22 09 5c
                    dec       (iy-$3a)                      ;[02e6] fd 35 c6
                    dec       (iy-$36)                      ;[02e9] fd 35 ca
                    ld        hl,$15c6                      ;[02ec] 21 c6 15
                    ld        de,$5c10                      ;[02ef] 11 10 5c
                    ld        bc,$000e                      ;[02f2] 01 0e 00
                    rst       $28                           ;[02f5] ef
                    jp        $fd33                         ;[02f6] c3 33 fd
                    ld        (hl),$31                      ;[02f9] 36 31
                    ld        (bc),a                        ;[02fb] 02
                    ei                                      ;[02fc] fb
                    rst       $28                           ;[02fd] ef
                    ld        l,e                           ;[02fe] 6b
                    dec       c                             ;[02ff] 0d
                    pop       af                            ;[0300] f1
                    rst       $08                           ;[0301] cf
                    push      af                            ;[0302] f5
                    call      $0360                         ;[0303] cd 60 03
                    rst       $18                           ;[0306] df
                    ld        e,b                           ;[0307] 58
                    dec       d                             ;[0308] 15
                    ld        hl,$035a                      ;[0309] 21 5a 03
                    ld        de,$d5b8                      ;[030c] 11 b8 d5
                    ld        bc,$0006                      ;[030f] 01 06 00
                    ldir                                    ;[0312] ed b0
                    ex        de,hl                         ;[0314] eb
                    scf                                     ;[0315] 37
                    call      $09c7                         ;[0316] cd c7 09
                    ld        hl,$0020                      ;[0319] 21 20 00
                    ld        ($d73d),hl                    ;[031c] 22 3d d7
                    inc       l                             ;[031f] 2c
                    inc       l                             ;[0320] 2c
                    inc       l                             ;[0321] 2c
                    ld        ($d73f),hl                    ;[0322] 22 3f d7
                    ld        a,$03                         ;[0325] 3e 03
                    ld        ($5c81),a                     ;[0327] 32 81 5c
                    rst       $20                           ;[032a] e7
                    ld        h,l                           ;[032b] 65
                    ld        e,$21                         ;[032c] 1e 21
                    jr        nc,$033b                      ;[032e] 30 0b
                    ld        de,$d633                      ;[0330] 11 33 d6
                    ld        bc,$3700                      ;[0333] 01 00 37
                    call      $0a60                         ;[0336] cd 60 0a
                    ld        hl,$0b50                      ;[0339] 21 50 0b
                    ld        de,$d6da                      ;[033c] 11 da d6
                    ld        bc,$3760                      ;[033f] 01 60 37
                    call      $0a60                         ;[0342] cd 60 0a
                    call      $0a8e                         ;[0345] cd 8e 0a
                    call      $0a0d                         ;[0348] cd 0d 0a
                    pop       af                            ;[034b] f1
                    rst       $18                           ;[034c] df
                    pop       de                            ;[034d] d1
                    dec       hl                            ;[034e] 2b
                    set       5,(iy+$02)                    ;[034f] fd cb 02 ee
                    ld        (iy+$31),$02                  ;[0353] fd 36 31 02
                    jp        $0c49                         ;[0357] c3 49 0c
                    nop                                     ;[035a] 00
                    nop                                     ;[035b] 00
                    nop                                     ;[035c] 00
                    inc       bc                            ;[035d] 03
                    nop                                     ;[035e] 00
                    inc       a                             ;[035f] 3c
                    ld        hl,$0877                      ;[0360] 21 77 08
                    ld        de,$d754                      ;[0363] 11 54 d7
                    ld        bc,$0010                      ;[0366] 01 10 00
                    ldir                                    ;[0369] ed b0
                    ret                                     ;[036b] c9

                    add       hl,$0004                      ;[036c] ed 34 04 00
                    ld        b,a                           ;[0370] 47
                    ld        a,(hl)                        ;[0371] 7e
                    cp        $53                           ;[0372] fe 53
                    jr        nz,$0391                      ;[0374] 20 1b
                    push      ix                            ;[0376] dd e5
                    ld        a,($5c7f)                     ;[0378] 3a 7f 5c
                    and       $0f                           ;[037b] e6 0f
                    add       $f2                           ;[037d] c6 f2
                    ld        ixh,a                         ;[037f] dd 67
                    ld        ixl,$00                       ;[0381] dd 2e 00
                    ld        a,b                           ;[0384] 78
                    call      $278b                         ;[0385] cd 8b 27
                    pop       ix                            ;[0388] dd e1
                    ld        hl,$15fe                      ;[038a] 21 fe 15
                    push      hl                            ;[038d] e5
                    jp        $5b48                         ;[038e] c3 48 5b
                    cp        $4b                           ;[0391] fe 4b
                    jr        z,$0376                       ;[0393] 28 e1
                    ld        hl,($5c51)                    ;[0395] 2a 51 5c
                    ex        de,hl                         ;[0398] eb
                    and       a                             ;[0399] a7
                    sbc       hl,de                         ;[039a] ed 52
                    ld        c,l                           ;[039c] 4d
                    ex        de,hl                         ;[039d] eb
                    add       hl,$0005                      ;[039e] ed 34 05 00
                    cp        $44                           ;[03a2] fe 44
                    jr        z,$03c0                       ;[03a4] 28 1a
                    dec       c                             ;[03a6] 0d
                    jr        nz,$03c7                      ;[03a7] 20 1e
                    ld        e,(hl)                        ;[03a9] 5e
                    inc       hl                            ;[03aa] 23
                    ld        h,(hl)                        ;[03ab] 66
                    ld        l,e                           ;[03ac] 6b
                    ld        de,$15fe                      ;[03ad] 11 fe 15
                    push      de                            ;[03b0] d5
                    ld        a,b                           ;[03b1] 78
                    push      ix                            ;[03b2] dd e5
                    ld        ix,($5c51)                    ;[03b4] dd 2a 51 5c
                    call      $03bf                         ;[03b8] cd bf 03
                    pop       ix                            ;[03bb] dd e1
                    jr        $038e                         ;[03bd] 18 cf
                    jp        (hl)                          ;[03bf] e9
                    ld        a,c                           ;[03c0] 79
                    dec       a                             ;[03c1] 3d
                    call      $0481                         ;[03c2] cd 81 04
                    jr        $038a                         ;[03c5] 18 c3
                    inc       hl                            ;[03c7] 23
                    inc       hl                            ;[03c8] 23
                    ld        e,(hl)                        ;[03c9] 5e
                    inc       hl                            ;[03ca] 23
                    ld        h,(hl)                        ;[03cb] 66
                    ld        l,e                           ;[03cc] 6b
                    ld        de,$15fe                      ;[03cd] 11 fe 15
                    push      de                            ;[03d0] d5
                    push      ix                            ;[03d1] dd e5
                    ld        ix,($5c51)                    ;[03d3] dd 2a 51 5c
                    call      $03bf                         ;[03d7] cd bf 03
                    pop       ix                            ;[03da] dd e1
                    jr        c,$038e                       ;[03dc] 38 b0
                    jr        z,$038e                       ;[03de] 28 ae
                    ld        a,$07                         ;[03e0] 3e 07
                    jp        $27d4                         ;[03e2] c3 d4 27
                    ld        hl,($5c51)                    ;[03e5] 2a 51 5c
                    push      hl                            ;[03e8] e5
                    pop       ix                            ;[03e9] dd e1
                    ld        a,(hl)                        ;[03eb] 7e
                    cp        $4d                           ;[03ec] fe 4d
                    jr        nz,$0408                      ;[03ee] 20 18
                    ld        a,(ix+$01)                    ;[03f0] dd 7e 01
                    cp        $5b                           ;[03f3] fe 5b
                    jr        nz,$0408                      ;[03f5] 20 11
                    ld        a,(ix+$04)                    ;[03f7] dd 7e 04
                    cp        $53                           ;[03fa] fe 53
                    jr        z,$042e                       ;[03fc] 28 30
                    cp        $4b                           ;[03fe] fe 4b
                    jr        z,$042e                       ;[0400] 28 2c
                    cp        $44                           ;[0402] fe 44
                    jr        z,$045f                       ;[0404] 28 59
                    jr        $044a                         ;[0406] 18 42
                    ld        a,e                           ;[0408] 7b
                    cp        $04                           ;[0409] fe 04
                    jp        z,$0495                       ;[040b] ca 95 04
                    add       hl,de                         ;[040e] 19
                    ld        c,(hl)                        ;[040f] 4e
                    inc       hl                            ;[0410] 23
                    ld        b,(hl)                        ;[0411] 46
                    cp        $02                           ;[0412] fe 02
                    jr        z,$041c                       ;[0414] 28 06
                    exx                                     ;[0416] d9
                    ld        a,c                           ;[0417] 79
                    exx                                     ;[0418] d9
                    jp        $0085                         ;[0419] c3 85 00
                    res       3,(iy+$02)                    ;[041c] fd cb 02 9e
                    call      $0085                         ;[0420] cd 85 00
                    ret       c                             ;[0423] d8
                    jr        nz,$03e0                      ;[0424] 20 ba
                    rst       $18                           ;[0426] df
                    ld        d,a                           ;[0427] 57
                    dec       hl                            ;[0428] 2b
                    ld        de,$0002                      ;[0429] 11 02 00
                    jr        $03e5                         ;[042c] 18 b7
                    ld        a,($5c7f)                     ;[042e] 3a 7f 5c
                    and       $0f                           ;[0431] e6 0f
                    add       $f2                           ;[0433] c6 f2
                    ld        ixh,a                         ;[0435] dd 67
                    ld        ixl,$00                       ;[0437] dd 2e 00
                    ld        a,e                           ;[043a] 7b
                    cp        $02                           ;[043b] fe 02
                    jp        z,$0c6d                       ;[043d] ca 6d 0c
                    cp        $04                           ;[0440] fe 04
                    exx                                     ;[0442] d9
                    jp        z,$2bbe                       ;[0443] ca be 2b
                    ld        a,c                           ;[0446] 79
                    jp        $277f                         ;[0447] c3 7f 27
                    ld        a,e                           ;[044a] 7b
                    add       hl,de                         ;[044b] 19
                    ld        e,$05                         ;[044c] 1e 05
                    add       hl,de                         ;[044e] 19
                    ld        e,(hl)                        ;[044f] 5e
                    inc       hl                            ;[0450] 23
                    ld        d,(hl)                        ;[0451] 56
                    push      de                            ;[0452] d5
                    cp        $02                           ;[0453] fe 02
                    jr        z,$045a                       ;[0455] 28 03
                    exx                                     ;[0457] d9
                    ld        a,c                           ;[0458] 79
                    ret                                     ;[0459] c9

                    ld        hl,$0423                      ;[045a] 21 23 04
                    ex        (sp),hl                       ;[045d] e3
                    jp        (hl)                          ;[045e] e9
                    ld        bc,$0005                      ;[045f] 01 05 00
                    add       hl,bc                         ;[0462] 09
                    exx                                     ;[0463] d9
                    push      bc                            ;[0464] c5
                    exx                                     ;[0465] d9
                    pop       bc                            ;[0466] c1
                    ld        a,e                           ;[0467] 7b
                    cp        $04                           ;[0468] fe 04
                    jr        nz,$047b                      ;[046a] 20 0f
                    add       b                             ;[046c] 80
                    add       b                             ;[046d] 80
                    cp        $06                           ;[046e] fe 06
                    jr        nz,$047b                      ;[0470] 20 09
                    exx                                     ;[0472] d9
                    push      hl                            ;[0473] e5
                    push      de                            ;[0474] d5
                    exx                                     ;[0475] d9
                    pop       ix                            ;[0476] dd e1
                    pop       de                            ;[0478] d1
                    jr        nz,$0495                      ;[0479] 20 1a
                    ld        b,c                           ;[047b] 41
                    call      $0481                         ;[047c] cd 81 04
                    jr        $0423                         ;[047f] 18 a2
                    ld        c,(hl)                        ;[0481] 4e
                    inc       hl                            ;[0482] 23
                    ld        h,(hl)                        ;[0483] 66
                    ex        de,hl                         ;[0484] eb
                    ld        e,b                           ;[0485] 58
                    srl       a                             ;[0486] cb 3f
                    add       $fb                           ;[0488] c6 fb
                    ld        b,a                           ;[048a] 47
                    rst       $20                           ;[048b] e7
                    rst       $08                           ;[048c] cf
                    ld        bc,$3cd8                      ;[048d] 01 d8 3c
                    ret       z                             ;[0490] c8
                    inc       a                             ;[0491] 3c
                    jp        z,$03e0                       ;[0492] ca e0 03
                    ld        a,$12                         ;[0495] 3e 12
                    jp        $27d4                         ;[0497] c3 d4 27
                    call      $04d7                         ;[049a] cd d7 04
                    ld        a,(hl)                        ;[049d] 7e
                    ret                                     ;[049e] c9

                    inc       b                             ;[049f] 04
                    djnz      $04a7                         ;[04a0] 10 05
                    ld        de,$000f                      ;[04a2] 11 0f 00
                    jr        $04cb                         ;[04a5] 18 24
                    djnz      $04c8                         ;[04a7] 10 1f
                    ld        a,d                           ;[04a9] 7a
                    or        e                             ;[04aa] b3
                    jp        nz,$03e0                      ;[04ab] c2 e0 03
                    push      hl                            ;[04ae] e5
                    ld        hl,($5c51)                    ;[04af] 2a 51 5c
                    ld        de,$000d                      ;[04b2] 11 0d 00
                    add       hl,de                         ;[04b5] 19
                    ld        e,(hl)                        ;[04b6] 5e
                    inc       hl                            ;[04b7] 23
                    ld        d,(hl)                        ;[04b8] 56
                    dec       de                            ;[04b9] 1b
                    inc       hl                            ;[04ba] 23
                    ex        de,hl                         ;[04bb] eb
                    pop       bc                            ;[04bc] c1
                    and       a                             ;[04bd] a7
                    sbc       hl,bc                         ;[04be] ed 42
                    ex        de,hl                         ;[04c0] eb
                    jp        c,$03e0                       ;[04c1] da e0 03
                    ld        (hl),c                        ;[04c4] 71
                    inc       hl                            ;[04c5] 23
                    ld        (hl),b                        ;[04c6] 70
                    ret                                     ;[04c7] c9

                    ld        de,$000d                      ;[04c8] 11 0d 00
                    ld        hl,($5c51)                    ;[04cb] 2a 51 5c
                    add       hl,de                         ;[04ce] 19
                    ld        e,(hl)                        ;[04cf] 5e
                    inc       hl                            ;[04d0] 23
                    ld        d,(hl)                        ;[04d1] 56
                    ex        de,hl                         ;[04d2] eb
                    ld        de,$0000                      ;[04d3] 11 00 00
                    ret                                     ;[04d6] c9

                    ld        hl,($5c51)                    ;[04d7] 2a 51 5c
                    ld        de,$000d                      ;[04da] 11 0d 00
                    add       hl,de                         ;[04dd] 19
                    ld        c,(hl)                        ;[04de] 4e
                    inc       hl                            ;[04df] 23
                    ld        b,(hl)                        ;[04e0] 46
                    inc       hl                            ;[04e1] 23
                    ld        e,(hl)                        ;[04e2] 5e
                    inc       hl                            ;[04e3] 23
                    ld        d,(hl)                        ;[04e4] 56
                    ex        de,hl                         ;[04e5] eb
                    push      hl                            ;[04e6] e5
                    and       a                             ;[04e7] a7
                    sbc       hl,bc                         ;[04e8] ed 42
                    pop       hl                            ;[04ea] e1
                    ex        de,hl                         ;[04eb] eb
                    jp        nc,$03e0                      ;[04ec] d2 e0 03
                    inc       de                            ;[04ef] 13
                    ld        (hl),d                        ;[04f0] 72
                    dec       hl                            ;[04f1] 2b
                    ld        (hl),e                        ;[04f2] 73
                    inc       hl                            ;[04f3] 23
                    inc       hl                            ;[04f4] 23
                    ld        c,(hl)                        ;[04f5] 4e
                    inc       hl                            ;[04f6] 23
                    ld        b,(hl)                        ;[04f7] 46
                    ex        de,hl                         ;[04f8] eb
                    add       hl,bc                         ;[04f9] 09
                    dec       hl                            ;[04fa] 2b
                    scf                                     ;[04fb] 37
                    ret                                     ;[04fc] c9

                    ld        hl,($5c51)                    ;[04fd] 2a 51 5c
                    ld        de,$000d                      ;[0500] 11 0d 00
                    add       hl,de                         ;[0503] 19
                    ld        b,(hl)                        ;[0504] 46
                    pop       hl                            ;[0505] e1
                    rst       $08                           ;[0506] cf
                    jp        (hl)                          ;[0507] e9
                    exx                                     ;[0508] d9
                    call      $04fd                         ;[0509] cd fd 04
                    push      bc                            ;[050c] c5
                    exx                                     ;[050d] d9
                    ld        a,b                           ;[050e] 78
                    pop       bc                            ;[050f] c1
                    and       a                             ;[0510] a7
                    jr        z,$051b                       ;[0511] 28 08
                    dec       a                             ;[0513] 3d
                    jr        z,$0522                       ;[0514] 28 0c
                    rst       $20                           ;[0516] e7
                    add       hl,sp                         ;[0517] 39
                    ld        bc,$0318                      ;[0518] 01 18 03
                    rst       $20                           ;[051b] e7
                    inc       sp                            ;[051c] 33
                    ld        bc,$0016                      ;[051d] 01 16 00
                    jr        $0537                         ;[0520] 18 15
                    rst       $20                           ;[0522] e7
                    ld        (hl),$01                      ;[0523] 36 01
                    jr        $0537                         ;[0525] 18 10
                    call      $04fd                         ;[0527] cd fd 04
                    rst       $20                           ;[052a] e7
                    jr        $052e                         ;[052b] 18 01
                    ld        a,c                           ;[052d] 79
                    jr        $0537                         ;[052e] 18 07
                    ld        c,a                           ;[0530] 4f
                    call      $04fd                         ;[0531] cd fd 04
                    rst       $20                           ;[0534] e7
                    dec       de                            ;[0535] 1b
                    ld        bc,$d8f7                      ;[0536] 01 f7 d8
                    ld        a,$12                         ;[0539] 3e 12
                    jp        $27d4                         ;[053b] c3 d4 27
                    exx                                     ;[053e] d9
                    push      hl                            ;[053f] e5
                    push      bc                            ;[0540] c5
                    push      hl                            ;[0541] e5
                    ld        hl,($5b5a)                    ;[0542] 2a 5a 5b
                    ex        (sp),hl                       ;[0545] e3
                    rst       $28                           ;[0546] ef
                    ret       pe                            ;[0547] e8
                    add       hl,de                         ;[0548] 19
                    pop       hl                            ;[0549] e1
                    ld        ($5b5a),hl                    ;[054a] 22 5a 5b
                    pop       bc                            ;[054d] c1
                    pop       hl                            ;[054e] e1
                    ld        de,($5c4f)                    ;[054f] ed 5b 4f 5c
                    and       a                             ;[0553] a7
                    sbc       hl,de                         ;[0554] ed 52
                    push      hl                            ;[0556] e5
                    ld        a,$13                         ;[0557] 3e 13
                    ld        hl,$5c10                      ;[0559] 21 10 5c
                    ld        e,(hl)                        ;[055c] 5e
                    inc       hl                            ;[055d] 23
                    ld        d,(hl)                        ;[055e] 56
                    ex        (sp),hl                       ;[055f] e3
                    and       a                             ;[0560] a7
                    sbc       hl,de                         ;[0561] ed 52
                    add       hl,de                         ;[0563] 19
                    jr        nc,$056b                      ;[0564] 30 05
                    ex        de,hl                         ;[0566] eb
                    and       a                             ;[0567] a7
                    sbc       hl,bc                         ;[0568] ed 42
                    ex        de,hl                         ;[056a] eb
                    ex        (sp),hl                       ;[056b] e3
                    dec       hl                            ;[056c] 2b
                    ld        (hl),e                        ;[056d] 73
                    inc       hl                            ;[056e] 23
                    ld        (hl),d                        ;[056f] 72
                    inc       hl                            ;[0570] 23
                    dec       a                             ;[0571] 3d
                    jr        nz,$055c                      ;[0572] 20 e8
                    pop       hl                            ;[0574] e1
                    ret                                     ;[0575] c9

                    exx                                     ;[0576] d9
                    push      hl                            ;[0577] e5
                    push      bc                            ;[0578] c5
                    push      hl                            ;[0579] e5
                    ld        hl,($5c53)                    ;[057a] 2a 53 5c
                    add       hl,bc                         ;[057d] 09
                    ld        a,h                           ;[057e] 7c
                    cp        $c0                           ;[057f] fe c0
                    jr        nc,$05c7                      ;[0581] 30 44
                    ld        hl,($5b5a)                    ;[0583] 2a 5a 5b
                    ex        (sp),hl                       ;[0586] e3
                    rst       $28                           ;[0587] ef
                    ld        d,l                           ;[0588] 55
                    ld        d,$e1                         ;[0589] 16 e1
                    ld        ($5b5a),hl                    ;[058b] 22 5a 5b
                    ld        hl,($5c57)                    ;[058e] 2a 57 5c
                    ld        de,($5c53)                    ;[0591] ed 5b 53 5c
                    dec       de                            ;[0595] 1b
                    and       a                             ;[0596] a7
                    sbc       hl,de                         ;[0597] ed 52
                    jr        nc,$059f                      ;[0599] 30 04
                    ld        ($5c57),de                    ;[059b] ed 53 57 5c
                    pop       bc                            ;[059f] c1
                    pop       hl                            ;[05a0] e1
                    ld        de,($5c4f)                    ;[05a1] ed 5b 4f 5c
                    and       a                             ;[05a5] a7
                    sbc       hl,de                         ;[05a6] ed 52
                    push      hl                            ;[05a8] e5
                    ld        a,$13                         ;[05a9] 3e 13
                    ld        hl,$5c10                      ;[05ab] 21 10 5c
                    ld        e,(hl)                        ;[05ae] 5e
                    inc       hl                            ;[05af] 23
                    ld        d,(hl)                        ;[05b0] 56
                    ex        (sp),hl                       ;[05b1] e3
                    and       a                             ;[05b2] a7
                    sbc       hl,de                         ;[05b3] ed 52
                    add       hl,de                         ;[05b5] 19
                    jr        nc,$05bc                      ;[05b6] 30 04
                    ex        de,hl                         ;[05b8] eb
                    and       a                             ;[05b9] a7
                    add       hl,bc                         ;[05ba] 09
                    ex        de,hl                         ;[05bb] eb
                    ex        (sp),hl                       ;[05bc] e3
                    dec       hl                            ;[05bd] 2b
                    ld        (hl),e                        ;[05be] 73
                    inc       hl                            ;[05bf] 23
                    ld        (hl),d                        ;[05c0] 72
                    inc       hl                            ;[05c1] 23
                    dec       a                             ;[05c2] 3d
                    jr        nz,$05ae                      ;[05c3] 20 e9
                    pop       hl                            ;[05c5] e1
                    ret                                     ;[05c6] c9

                    rst       $28                           ;[05c7] ef
                    dec       d                             ;[05c8] 15
                    rra                                     ;[05c9] 1f
                    dec       bc                            ;[05ca] 0b
                    jp        p,$0a05                       ;[05cb] f2 05 0a
                    push      af                            ;[05ce] f5
                    dec       b                             ;[05cf] 05
                    rlca                                    ;[05d0] 07
                    ld        sp,$2006                      ;[05d1] 31 06 20
                    ld        sp,$0d06                      ;[05d4] 31 06 0d
                    inc       (hl)                          ;[05d7] 34
                    ld        b,$0e                         ;[05d8] 06 0e
                    add       hl,de                         ;[05da] 19
                    ld        b,$37                         ;[05db] 06 37
                    jp        p,$3605                       ;[05dd] f2 05 36
                    push      af                            ;[05e0] f5
                    dec       b                             ;[05e1] 05
                    ex        af,af'                        ;[05e2] 08
                    xor       e                             ;[05e3] ab
                    dec       a                             ;[05e4] 3d
                    add       hl,bc                         ;[05e5] 09
                    sbc       a                             ;[05e6] 9f
                    dec       a                             ;[05e7] 3d
                    dec       (hl)                          ;[05e8] 35
                    xor       e                             ;[05e9] ab
                    dec       a                             ;[05ea] 3d
                    jr        c,$058c                       ;[05eb] 38 9f
                    dec       a                             ;[05ed] 3d
                    jr        nc,$0624                      ;[05ee] 30 34
                    ld        b,$ff                         ;[05f0] 06 ff
                    scf                                     ;[05f2] 37
                    jr        $05f6                         ;[05f3] 18 01
                    and       a                             ;[05f5] a7
                    ld        a,($f700)                     ;[05f6] 3a 00 f7
                    call      $0844                         ;[05f9] cd 44 08
                    call      c,$060a                       ;[05fc] dc 0a 06
                    call      nc,$0610                      ;[05ff] d4 10 06
                    ld        ($f700),a                     ;[0602] 32 00 f7
                    call      $0844                         ;[0605] cd 44 08
                    scf                                     ;[0608] 37
                    ret                                     ;[0609] c9

                    dec       a                             ;[060a] 3d
                    ret       p                             ;[060b] f0
                    ld        a,($f71f)                     ;[060c] 3a 1f f7
                    ret                                     ;[060f] c9

                    inc       a                             ;[0610] 3c
                    ld        hl,($f71f)                    ;[0611] 2a 1f f7
                    inc       l                             ;[0614] 2c
                    cp        l                             ;[0615] bd
                    ret       c                             ;[0616] d8
                    xor       a                             ;[0617] af
                    ret                                     ;[0618] c9

                    ld        hl,$58e8                      ;[0619] 21 e8 58
                    ld        a,($f71f)                     ;[061c] 3a 1f f7
                    inc       a                             ;[061f] 3c
                    ld        b,a                           ;[0620] 47
                    push      bc                            ;[0621] c5
                    ld        de,$0402                      ;[0622] 11 02 04
                    call      $0857                         ;[0625] cd 57 08
                    add       hl,$0010                      ;[0628] ed 34 10 00
                    pop       bc                            ;[062c] c1
                    djnz      $0621                         ;[062d] 10 f2
                    scf                                     ;[062f] 37
                    ret                                     ;[0630] c9

                    scf                                     ;[0631] 37
                    jr        $0638                         ;[0632] 18 04
                    ld        a,($f700)                     ;[0634] 3a 00 f7
                    and       a                             ;[0637] a7
                    pop       hl                            ;[0638] e1
                    pop       hl                            ;[0639] e1
                    ld        hl,$5b68                      ;[063a] 21 68 5b
                    res       1,(hl)                        ;[063d] cb 8e
                    push      af                            ;[063f] f5
                    call      $07f9                         ;[0640] cd f9 07
                    pop       af                            ;[0643] f1
                    ret                                     ;[0644] c9

                    xor       a                             ;[0645] af
                    rst       $18                           ;[0646] df
                    ret       p                             ;[0647] f0
                    inc       d                             ;[0648] 14
                    call      $07f6                         ;[0649] cd f6 07
                    ld        a,$02                         ;[064c] 3e 02
                    rst       $28                           ;[064e] ef
                    ld        bc,$2116                      ;[064f] 01 16 21
                    inc       a                             ;[0652] 3c
                    ld        e,h                           ;[0653] 5c
                    res       0,(hl)                        ;[0654] cb 86
                    ld        hl,$5c8f                      ;[0656] 21 8f 5c
                    ld        a,($d6e0)                     ;[0659] 3a e0 d6
                    ld        (hl),a                        ;[065c] 77
                    xor       a                             ;[065d] af
                    inc       hl                            ;[065e] 23
                    ld        (hl),a                        ;[065f] 77
                    inc       hl                            ;[0660] 23
                    ld        (hl),a                        ;[0661] 77
                    ld        a,$fe                         ;[0662] 3e fe
                    call      $075b                         ;[0664] cd 5b 07
                    ld        hl,($d742)                    ;[0667] 2a 42 d7
                    call      $0768                         ;[066a] cd 68 07
                    ld        a,$20                         ;[066d] 3e 20
                    rst       $10                           ;[066f] d7
                    push      hl                            ;[0670] e5
                    call      $08af                         ;[0671] cd af 08
                    call      $3d52                         ;[0674] cd 52 3d
                    call      $08af                         ;[0677] cd af 08
                    ld        a,$20                         ;[067a] 3e 20
                    rst       $10                           ;[067c] d7
                    pop       hl                            ;[067d] e1
                    ld        a,($d6e3)                     ;[067e] 3a e3 d6
                    ld        ($5c8f),a                     ;[0681] 32 8f 5c
                    xor       a                             ;[0684] af
                    ld        bc,$f701                      ;[0685] 01 01 f7
                    ld        de,$f70b                      ;[0688] 11 0b f7
                    push      af                            ;[068b] f5
                    call      $07c4                         ;[068c] cd c4 07
                    jr        z,$06b1                       ;[068f] 28 20
                    cp        $3d                           ;[0691] fe 3d
                    jr        z,$06b1                       ;[0693] 28 1c
                    pop       af                            ;[0695] f1
                    push      af                            ;[0696] f5
                    push      bc                            ;[0697] c5
                    push      de                            ;[0698] d5
                    call      $075b                         ;[0699] cd 5b 07
                    call      $077f                         ;[069c] cd 7f 07
                    ld        a,c                           ;[069f] 79
                    pop       de                            ;[06a0] d1
                    pop       bc                            ;[06a1] c1
                    ex        de,hl                         ;[06a2] eb
                    ld        (hl),e                        ;[06a3] 73
                    inc       hl                            ;[06a4] 23
                    ld        (hl),d                        ;[06a5] 72
                    inc       hl                            ;[06a6] 23
                    ex        de,hl                         ;[06a7] eb
                    ld        (bc),a                        ;[06a8] 02
                    inc       bc                            ;[06a9] 03
                    pop       af                            ;[06aa] f1
                    inc       a                             ;[06ab] 3c
                    cp        $0a                           ;[06ac] fe 0a
                    jr        c,$068b                       ;[06ae] 38 db
                    push      af                            ;[06b0] f5
                    pop       af                            ;[06b1] f1
                    dec       a                             ;[06b2] 3d
                    ld        ($f71f),a                     ;[06b3] 32 1f f7
                    ld        hl,$0873                      ;[06b6] 21 73 08
                    jp        m,$0684                       ;[06b9] fa 84 06
                    inc       a                             ;[06bc] 3c
                    call      $075b                         ;[06bd] cd 5b 07
                    ld        b,$09                         ;[06c0] 06 09
                    call      $0777                         ;[06c2] cd 77 07
                    ld        a,($5b69)                     ;[06c5] 3a 69 5b
                    and       a                             ;[06c8] a7
                    jr        z,$06ea                       ;[06c9] 28 1f
                    inc       a                             ;[06cb] 3c
                    ld        hl,($5c36)                    ;[06cc] 2a 36 5c
                    push      hl                            ;[06cf] e5
                    inc       hl                            ;[06d0] 23
                    ld        ($5c36),hl                    ;[06d1] 22 36 5c
                    ld        l,a                           ;[06d4] 6f
                    ld        h,$00                         ;[06d5] 26 00
                    add       hl,hl                         ;[06d7] 29
                    add       hl,hl                         ;[06d8] 29
                    add       hl,hl                         ;[06d9] 29
                    add       hl,hl                         ;[06da] 29
                    ld        e,$20                         ;[06db] 1e 20
                    rst       $18                           ;[06dd] df
                    add       bc,$4b3e                      ;[06de] ed 36 3e 4b
                    rst       $10                           ;[06e2] d7
                    ld        a,$20                         ;[06e3] 3e 20
                    rst       $10                           ;[06e5] d7
                    pop       hl                            ;[06e6] e1
                    ld        ($5c36),hl                    ;[06e7] 22 36 5c
                    ld        hl,($d6e2)                    ;[06ea] 2a e2 d6
                    ld        a,h                           ;[06ed] 7c
                    xor       l                             ;[06ee] ad
                    and       $07                           ;[06ef] e6 07
                    jr        nz,$0717                      ;[06f1] 20 24
                    ld        hl,$7740                      ;[06f3] 21 40 77
                    ld        a,($f71f)                     ;[06f6] 3a 1f f7
                    inc       a                             ;[06f9] 3c
                    inc       a                             ;[06fa] 3c
                    add       a                             ;[06fb] 87
                    add       a                             ;[06fc] 87
                    add       a                             ;[06fd] 87
                    dec       a                             ;[06fe] 3d
                    ld        b,a                           ;[06ff] 47
                    push      bc                            ;[0700] c5
                    ld        de,$ff00                      ;[0701] 11 00 ff
                    call      $07e7                         ;[0704] cd e7 07
                    ld        b,$7f                         ;[0707] 06 7f
                    ld        de,$0001                      ;[0709] 11 01 00
                    call      $07e7                         ;[070c] cd e7 07
                    pop       bc                            ;[070f] c1
                    inc       b                             ;[0710] 04
                    ld        de,$0100                      ;[0711] 11 00 01
                    call      $07e7                         ;[0714] cd e7 07
                    res       5,(iy+$01)                    ;[0717] fd cb 01 ae
                    ld        hl,$5b68                      ;[071b] 21 68 5b
                    set       1,(hl)                        ;[071e] cb ce
                    ld        a,($f700)                     ;[0720] 3a 00 f7
                    call      $0844                         ;[0723] cd 44 08
                    xor       a                             ;[0726] af
                    ld        ($5c41),a                     ;[0727] 32 41 5c
                    call      $0ce2                         ;[072a] cd e2 0c
                    ld        hl,$05ca                      ;[072d] 21 ca 05
                    call      $0ffc                         ;[0730] cd fc 0f
                    jr        c,$0726                       ;[0733] 38 f1
                    call      $07ba                         ;[0735] cd ba 07
                    ld        bc,($f71e)                    ;[0738] ed 4b 1e f7
                    inc       b                             ;[073c] 04
                    ld        c,$00                         ;[073d] 0e 00
                    ld        hl,$f701                      ;[073f] 21 01 f7
                    cp        (hl)                          ;[0742] be
                    jr        z,$074c                       ;[0743] 28 07
                    inc       hl                            ;[0745] 23
                    inc       c                             ;[0746] 0c
                    djnz      $0742                         ;[0747] 10 f9
                    and       a                             ;[0749] a7
                    jr        $0756                         ;[074a] 18 0a
                    ld        a,c                           ;[074c] 79
                    ld        ($f700),a                     ;[074d] 32 00 f7
                    ld        hl,$0634                      ;[0750] 21 34 06
                    call      $101c                         ;[0753] cd 1c 10
                    call      nc,$3e18                      ;[0756] d4 18 3e
                    jr        $0726                         ;[0759] 18 cb
                    add       $07                           ;[075b] c6 07
                    ld        b,a                           ;[075d] 47
                    ld        c,$08                         ;[075e] 0e 08
                    ld        a,$16                         ;[0760] 3e 16
                    rst       $10                           ;[0762] d7
                    ld        a,b                           ;[0763] 78
                    rst       $10                           ;[0764] d7
                    ld        a,c                           ;[0765] 79
                    rst       $10                           ;[0766] d7
                    ret                                     ;[0767] c9

                    ld        b,$08                         ;[0768] 06 08
                    ld        a,$20                         ;[076a] 3e 20
                    rst       $10                           ;[076c] d7
                    ld        a,(hl)                        ;[076d] 7e
                    cp        $20                           ;[076e] fe 20
                    jr        c,$0777                       ;[0770] 38 05
                    inc       hl                            ;[0772] 23
                    rst       $10                           ;[0773] d7
                    djnz      $076d                         ;[0774] 10 f7
                    ret                                     ;[0776] c9

                    push      af                            ;[0777] f5
                    ld        a,$20                         ;[0778] 3e 20
                    rst       $10                           ;[077a] d7
                    djnz      $0778                         ;[077b] 10 fb
                    pop       af                            ;[077d] f1
                    ret                                     ;[077e] c9

                    ld        bc,$0f00                      ;[077f] 01 00 0f
                    ld        e,c                           ;[0782] 59
                    ld        a,$20                         ;[0783] 3e 20
                    rst       $10                           ;[0785] d7
                    ld        a,(hl)                        ;[0786] 7e
                    cp        $3a                           ;[0787] fe 3a
                    jr        z,$0777                       ;[0789] 28 ec
                    cp        $20                           ;[078b] fe 20
                    jr        c,$0777                       ;[078d] 38 e8
                    inc       hl                            ;[078f] 23
                    cp        $5f                           ;[0790] fe 5f
                    jr        z,$07a2                       ;[0792] 28 0e
                    ld        e,a                           ;[0794] 5f
                    rst       $10                           ;[0795] d7
                    djnz      $0786                         ;[0796] 10 ee
                    ld        a,(hl)                        ;[0798] 7e
                    cp        $3a                           ;[0799] fe 3a
                    ret       z                             ;[079b] c8
                    cp        $20                           ;[079c] fe 20
                    ret       c                             ;[079e] d8
                    inc       hl                            ;[079f] 23
                    jr        $0798                         ;[07a0] 18 f6
                    inc       c                             ;[07a2] 0c
                    dec       c                             ;[07a3] 0d
                    jr        nz,$0786                      ;[07a4] 20 e0
                    ld        a,e                           ;[07a6] 7b
                    call      $07ba                         ;[07a7] cd ba 07
                    ld        c,a                           ;[07aa] 4f
                    push      hl                            ;[07ab] e5
                    ld        hl,($5c84)                    ;[07ac] 2a 84 5c
                    dec       hl                            ;[07af] 2b
                    call      $07df                         ;[07b0] cd df 07
                    ld        a,($d6e5)                     ;[07b3] 3a e5 d6
                    ld        (hl),a                        ;[07b6] 77
                    pop       hl                            ;[07b7] e1
                    jr        $0786                         ;[07b8] 18 cc
                    cp        $41                           ;[07ba] fe 41
                    ret       c                             ;[07bc] d8
                    cp        $5b                           ;[07bd] fe 5b
                    ret       nc                            ;[07bf] d0
                    or        $20                           ;[07c0] f6 20
                    ret                                     ;[07c2] c9

                    inc       hl                            ;[07c3] 23
                    ld        a,(hl)                        ;[07c4] 7e
                    cp        $20                           ;[07c5] fe 20
                    jr        nc,$07c3                      ;[07c7] 30 fa
                    inc       hl                            ;[07c9] 23
                    ld        a,(hl)                        ;[07ca] 7e
                    cp        $ff                           ;[07cb] fe ff
                    ret       z                             ;[07cd] c8
                    cp        $0d                           ;[07ce] fe 0d
                    jr        z,$07c9                       ;[07d0] 28 f7
                    cp        $0a                           ;[07d2] fe 0a
                    ret       nz                            ;[07d4] c0
                    jr        $07c9                         ;[07d5] 18 f2
                    ld        a,(hl)                        ;[07d7] 7e
                    inc       hl                            ;[07d8] 23
                    cp        $ff                           ;[07d9] fe ff
                    ret       z                             ;[07db] c8
                    rst       $10                           ;[07dc] d7
                    jr        $07d7                         ;[07dd] 18 f8
                    ld        a,h                           ;[07df] 7c
                    rrca                                    ;[07e0] 0f
                    rrca                                    ;[07e1] 0f
                    rrca                                    ;[07e2] 0f
                    or        $50                           ;[07e3] f6 50
                    ld        h,a                           ;[07e5] 67
                    ret                                     ;[07e6] c9

                    push      bc                            ;[07e7] c5
                    push      de                            ;[07e8] d5
                    push      hl                            ;[07e9] e5
                    ld        b,h                           ;[07ea] 44
                    ld        c,l                           ;[07eb] 4d
                    rst       $28                           ;[07ec] ef
                    jp        (hl)                          ;[07ed] e9
                    ld        ($d1e1),hl                    ;[07ee] 22 e1 d1
                    pop       bc                            ;[07f1] c1
                    add       hl,de                         ;[07f2] 19
                    djnz      $07e7                         ;[07f3] 10 f2
                    ret                                     ;[07f5] c9

                    scf                                     ;[07f6] 37
                    jr        $07fa                         ;[07f7] 18 01
                    and       a                             ;[07f9] a7
                    ld        hl,$5c3c                      ;[07fa] 21 3c 5c
                    ld        de,$ed11                      ;[07fd] 11 11 ed
                    ld        bc,$0001                      ;[0800] 01 01 00
                    call      $083a                         ;[0803] cd 3a 08
                    ld        hl,$5c7d                      ;[0806] 21 7d 5c
                    ld        bc,$0015                      ;[0809] 01 15 00
                    call      $0839                         ;[080c] cd 39 08
                    ld        bc,$0e13                      ;[080f] 01 13 0e
                    push      bc                            ;[0812] c5
                    push      de                            ;[0813] d5
                    ld        b,c                           ;[0814] 41
                    rst       $28                           ;[0815] ef
                    sbc       e                             ;[0816] 9b
                    ld        c,$ed                         ;[0817] 0e ed
                    inc       (hl)                          ;[0819] 34
                    ex        af,af'                        ;[081a] 08
                    nop                                     ;[081b] 00
                    pop       de                            ;[081c] d1
                    call      $0825                         ;[081d] cd 25 08
                    pop       bc                            ;[0820] c1
                    dec       c                             ;[0821] 0d
                    djnz      $0812                         ;[0822] 10 ee
                    ret                                     ;[0824] c9

                    ld        bc,$0810                      ;[0825] 01 10 08
                    push      hl                            ;[0828] e5
                    push      bc                            ;[0829] c5
                    ld        b,$00                         ;[082a] 06 00
                    push      hl                            ;[082c] e5
                    call      $0839                         ;[082d] cd 39 08
                    pop       hl                            ;[0830] e1
                    pop       bc                            ;[0831] c1
                    inc       h                             ;[0832] 24
                    djnz      $0829                         ;[0833] 10 f4
                    pop       hl                            ;[0835] e1
                    call      $07df                         ;[0836] cd df 07
                    ex        af,af'                        ;[0839] 08
                    jr        c,$083d                       ;[083a] 38 01
                    ex        de,hl                         ;[083c] eb
                    ldir                                    ;[083d] ed b0
                    jr        c,$0842                       ;[083f] 38 01
                    ex        de,hl                         ;[0841] eb
                    ex        af,af'                        ;[0842] 08
                    ret                                     ;[0843] c9

                    push      af                            ;[0844] f5
                    ld        d,a                           ;[0845] 57
                    ld        e,$20                         ;[0846] 1e 20
                    mul       d,e                           ;[0848] ed 30
                    add       de,$58e8                      ;[084a] ed 35 e8 58
                    ex        de,hl                         ;[084e] eb
                    ld        de,$0601                      ;[084f] 11 01 06
                    call      $0857                         ;[0852] cd 57 08
                    pop       af                            ;[0855] f1
                    ret                                     ;[0856] c9

                    ld        c,$10                         ;[0857] 0e 10
                    ld        a,(hl)                        ;[0859] 7e
                    push      hl                            ;[085a] e5
                    ld        hl,$d6e7                      ;[085b] 21 e7 d6
                    ld        b,d                           ;[085e] 42
                    cp        (hl)                          ;[085f] be
                    jr        z,$086b                       ;[0860] 28 09
                    dec       hl                            ;[0862] 2b
                    djnz      $085f                         ;[0863] 10 fa
                    pop       hl                            ;[0865] e1
                    inc       hl                            ;[0866] 23
                    dec       c                             ;[0867] 0d
                    jr        nz,$0859                      ;[0868] 20 ef
                    ret                                     ;[086a] c9

                    ld        a,l                           ;[086b] 7d
                    xor       e                             ;[086c] ab
                    ld        l,a                           ;[086d] 6f
                    ld        a,(hl)                        ;[086e] 7e
                    pop       hl                            ;[086f] e1
                    ld        (hl),a                        ;[0870] 77
                    jr        $0866                         ;[0871] 18 f3
                    dec       c                             ;[0873] 0d
                    jr        nz,$0883                      ;[0874] 20 0d
                    rst       $38                           ;[0876] ff
                    ld        bc,$0703                      ;[0877] 01 03 07
                    rrca                                    ;[087a] 0f
                    rra                                     ;[087b] 1f
                    ccf                                     ;[087c] 3f
                    ld        a,a                           ;[087d] 7f
                    rst       $38                           ;[087e] ff
                    cp        $fc                           ;[087f] fe fc
                    ret       m                             ;[0881] f8
                    ret       p                             ;[0882] f0
                    ret       po                            ;[0883] e0
                    ret       nz                            ;[0884] c0
                    add       b                             ;[0885] 80
                    nop                                     ;[0886] 00
                    nop                                     ;[0887] 00
                    nop                                     ;[0888] 00
                    nop                                     ;[0889] 00
                    nop                                     ;[088a] 00
                    nop                                     ;[088b] 00
                    rst       $38                           ;[088c] ff
                    rst       $38                           ;[088d] ff
                    rst       $38                           ;[088e] ff
                    nop                                     ;[088f] 00
                    rst       $38                           ;[0890] ff
                    nop                                     ;[0891] 00
                    rst       $38                           ;[0892] ff
                    nop                                     ;[0893] 00
                    rst       $38                           ;[0894] ff
                    nop                                     ;[0895] 00
                    rst       $38                           ;[0896] ff
                    jr        c,$08d1                       ;[0897] 38 38
                    jr        c,$08d3                       ;[0899] 38 38
                    jr        c,$08d5                       ;[089b] 38 38
                    jr        c,$08d7                       ;[089d] 38 38
                    rst       $38                           ;[089f] ff
                    rst       $38                           ;[08a0] ff
                    rst       $38                           ;[08a1] ff
                    nop                                     ;[08a2] 00
                    nop                                     ;[08a3] 00
                    nop                                     ;[08a4] 00
                    nop                                     ;[08a5] 00
                    nop                                     ;[08a6] 00
                    call      m,$8484                       ;[08a7] fc 84 84
                    add       h                             ;[08aa] 84
                    add       h                             ;[08ab] 84
                    add       h                             ;[08ac] 84
                    add       h                             ;[08ad] 84
                    call      m,$d5cd                       ;[08ae] fc cd d5
                    ex        af,af'                        ;[08b1] 08
                    push      hl                            ;[08b2] e5
                    ld        bc,$0590                      ;[08b3] 01 90 05
                    ld        de,$d6db                      ;[08b6] 11 db d6
                    ld        a,(de)                        ;[08b9] 1a
                    inc       de                            ;[08ba] 13
                    ld        ($5c8f),a                     ;[08bb] 32 8f 5c
                    ld        a,c                           ;[08be] 79
                    xor       $01                           ;[08bf] ee 01
                    ld        c,a                           ;[08c1] 4f
                    rst       $10                           ;[08c2] d7
                    djnz      $08b9                         ;[08c3] 10 f4
                    ld        a,(de)                        ;[08c5] 1a
                    ld        ($5c8f),a                     ;[08c6] 32 8f 5c
                    ld        a,$20                         ;[08c9] 3e 20
                    rst       $10                           ;[08cb] d7
                    pop       de                            ;[08cc] d1
                    jp        $08d8                         ;[08cd] c3 d8 08
                    ld        de,$0877                      ;[08d0] 11 77 08
                    jr        $08d8                         ;[08d3] 18 03
                    ld        de,$d754                      ;[08d5] 11 54 d7
                    ld        hl,($5c7b)                    ;[08d8] 2a 7b 5c
                    ld        ($5c7b),de                    ;[08db] ed 53 7b 5c
                    ret                                     ;[08df] c9

                    pop       hl                            ;[08e0] e1
                    pop       hl                            ;[08e1] e1
                    ei                                      ;[08e2] fb
                    halt                                    ;[08e3] 76
                    res       5,(iy+$01)                    ;[08e4] fd cb 01 ae
                    rst       $08                           ;[08e8] cf
                    ld        a,$02                         ;[08e9] 3e 02
                    rst       $28                           ;[08eb] ef
                    ld        bc,$cd16                      ;[08ec] 01 16 cd
                    dec       c                             ;[08ef] 0d
                    ld        a,(bc)                        ;[08f0] 0a
                    ld        a,($d5b8)                     ;[08f1] 3a b8 d5
                    cp        $fe                           ;[08f4] fe fe
                    push      af                            ;[08f6] f5
                    ld        hl,($d750)                    ;[08f7] 2a 50 d7
                    ld        a,h                           ;[08fa] 7c
                    or        l                             ;[08fb] b5
                    jr        nz,$0904                      ;[08fc] 20 06
                    pop       af                            ;[08fe] f1
                    push      af                            ;[08ff] f5
                    cp        $04                           ;[0900] fe 04
                    jr        c,$0909                       ;[0902] 38 05
                    ld        a,($5c3a)                     ;[0904] 3a 3a 5c
                    cp        $ff                           ;[0907] fe ff
                    push      af                            ;[0909] f5
                    call      c,$0c6d                       ;[090a] dc 6d 0c
                    call      $0a8e                         ;[090d] cd 8e 0a
                    pop       de                            ;[0910] d1
                    pop       af                            ;[0911] f1
                    jp        z,$2c43                       ;[0912] ca 43 2c
                    jp        nc,$0c43                      ;[0915] d2 43 0c
                    call      $0068                         ;[0918] cd 68 00
                    ld        (bc),a                        ;[091b] 02
                    inc       bc                            ;[091c] 03
                    call      $0968                         ;[091d] cd 68 09
                    xor       a                             ;[0920] af
                    rst       $18                           ;[0921] df
                    nop                                     ;[0922] 00
                    dec       d                             ;[0923] 15
                    ret                                     ;[0924] c9

                    ld        a,($d73d)                     ;[0925] 3a 3d d7
                    sub       $20                           ;[0928] d6 20
                    jr        z,$092e                       ;[092a] 28 02
                    ld        a,$09                         ;[092c] 3e 09
                    push      af                            ;[092e] f5
                    rst       $18                           ;[092f] df
                    ret       p                             ;[0930] f0
                    inc       d                             ;[0931] 14
                    pop       af                            ;[0932] f1
                    ret       z                             ;[0933] c8
                    push      ix                            ;[0934] dd e5
                    ld        ix,$fb00                      ;[0936] dd 21 00 fb
                    ld        hl,$0951                      ;[093a] 21 51 09
                    call      $274b                         ;[093d] cd 4b 27
                    ld        a,($d73d)                     ;[0940] 3a 3d d7
                    cp        $40                           ;[0943] fe 40
                    ld        a,$08                         ;[0945] 3e 08
                    jr        z,$094b                       ;[0947] 28 02
                    ld        a,$06                         ;[0949] 3e 06
                    call      $277f                         ;[094b] cd 7f 27
                    pop       ix                            ;[094e] dd e1
                    ret                                     ;[0950] c9

                    ld        a,(de)                        ;[0951] 1a
                    rla                                     ;[0952] 17
                    sbc       (hl)                          ;[0953] 9e
                    ld        a,($5c7f)                     ;[0954] 3a 7f 5c
                    and       $0f                           ;[0957] e6 0f
                    jr        z,$0960                       ;[0959] 28 05
                    res       5,(iy+$02)                    ;[095b] fd cb 02 ae
                    ret                                     ;[095f] c9

                    ld        (iy+$31),$02                  ;[0960] fd 36 31 02
                    rst       $28                           ;[0964] ef
                    ld        l,(hl)                        ;[0965] 6e
                    dec       c                             ;[0966] 0d
                    ret                                     ;[0967] c9

                    ld        a,($d5b8)                     ;[0968] 3a b8 d5
                    cp        $04                           ;[096b] fe 04
                    ret       z                             ;[096d] c8
                    cp        $08                           ;[096e] fe 08
                    call      nz,$0afe                      ;[0970] c4 fe 0a
                    ld        a,($5b7b)                     ;[0973] 3a 7b 5b
                    push      af                            ;[0976] f5
                    ld        a,($5c7f)                     ;[0977] 3a 7f 5c
                    push      af                            ;[097a] f5
                    halt                                    ;[097b] 76
                    xor       a                             ;[097c] af
                    out       ($ff),a                       ;[097d] d3 ff
                    ld        hl,$d5b9                      ;[097f] 21 b9 d5
                    ld        a,(hl)                        ;[0982] 7e
                    push      hl                            ;[0983] e5
                    rst       $18                           ;[0984] df
                    ret       p                             ;[0985] f0
                    inc       d                             ;[0986] 14
                    pop       hl                            ;[0987] e1
                    pop       af                            ;[0988] f1
                    ld        (hl),a                        ;[0989] 77
                    inc       hl                            ;[098a] 23
                    ld        a,(hl)                        ;[098b] 7e
                    ld        bc,$123b                      ;[098c] 01 3b 12
                    ld        ($5b7b),a                     ;[098f] 32 7b 5b
                    out       (c),a                         ;[0992] ed 79
                    pop       af                            ;[0994] f1
                    ld        (hl),a                        ;[0995] 77
                    inc       hl                            ;[0996] 23
                    call      $3d96                         ;[0997] cd 96 3d
                    ld        e,(hl)                        ;[099a] 5e
                    ld        (hl),a                        ;[099b] 77
                    out       (c),e                         ;[099c] ed 59
                    inc       hl                            ;[099e] 23
                    ld        bc,($5c36)                    ;[099f] ed 4b 36 5c
                    ld        e,(hl)                        ;[09a3] 5e
                    ld        (hl),c                        ;[09a4] 71
                    inc       hl                            ;[09a5] 23
                    ld        d,(hl)                        ;[09a6] 56
                    ld        (hl),b                        ;[09a7] 70
                    inc       hl                            ;[09a8] 23
                    ld        ($5c36),de                    ;[09a9] ed 53 36 5c
                    call      $09c6                         ;[09ad] cd c6 09
                    ld        a,($d5b8)                     ;[09b0] 3a b8 d5
                    cp        $08                           ;[09b3] fe 08
                    ret       z                             ;[09b5] c8
                    call      $272c                         ;[09b6] cd 2c 27
                    ld        h,$f7                         ;[09b9] 26 f7
                    call      $09ea                         ;[09bb] cd ea 09
                    ld        h,$fb                         ;[09be] 26 fb
                    call      $09ea                         ;[09c0] cd ea 09
                    jp        $27bb                         ;[09c3] c3 bb 27
                    and       a                             ;[09c6] a7
                    ld        de,$0418                      ;[09c7] 11 18 04
                    nextreg $1c,$0f                         ;[09ca] ed 91 1c 0f
                    push      de                            ;[09ce] d5
                    ld        b,$04                         ;[09cf] 06 04
                    push      bc                            ;[09d1] c5
                    call      $09fb                         ;[09d2] cd fb 09
                    dec       e                             ;[09d5] 1d
                    pop       bc                            ;[09d6] c1
                    djnz      $09d1                         ;[09d7] 10 f8
                    pop       de                            ;[09d9] d1
                    inc       e                             ;[09da] 1c
                    dec       d                             ;[09db] 15
                    jr        nz,$09ce                      ;[09dc] 20 f0
                    ret                                     ;[09de] c9

                    ld        d,h                           ;[09df] 54
                    ld        a,l                           ;[09e0] 7d
                    ld        c,$30                         ;[09e1] 0e 30
                    xor       c                             ;[09e3] a9
                    ld        e,a                           ;[09e4] 5f
                    ld        b,$00                         ;[09e5] 06 00
                    ldir                                    ;[09e7] ed b0
                    ret                                     ;[09e9] c9

                    ld        l,$00                         ;[09ea] 2e 00
                    ld        d,h                           ;[09ec] 54
                    ld        e,$30                         ;[09ed] 1e 30
                    ld        b,e                           ;[09ef] 43
                    ld        c,(hl)                        ;[09f0] 4e
                    ld        a,(de)                        ;[09f1] 1a
                    ld        (hl),a                        ;[09f2] 77
                    ld        a,c                           ;[09f3] 79
                    ld        (de),a                        ;[09f4] 12
                    inc       hl                            ;[09f5] 23
                    inc       de                            ;[09f6] 13
                    djnz      $09f0                         ;[09f7] 10 f7
                    ret                                     ;[09f9] c9

                    and       a                             ;[09fa] a7
                    ld        a,(hl)                        ;[09fb] 7e
                    ld        bc,$243b                      ;[09fc] 01 3b 24
                    out       (c),e                         ;[09ff] ed 59
                    inc       b                             ;[0a01] 04
                    in        d,(c)                         ;[0a02] ed 50
                    jr        nc,$0a07                      ;[0a04] 30 01
                    ld        a,d                           ;[0a06] 7a
                    out       (c),a                         ;[0a07] ed 79
                    ld        (hl),d                        ;[0a09] 72
                    inc       hl                            ;[0a0a] 23
                    inc       e                             ;[0a0b] 1c
                    ret                                     ;[0a0c] c9

                    xor       a                             ;[0a0d] af
                    ld        ($5c41),a                     ;[0a0e] 32 41 5c
                    ld        (iy-$30),$02                  ;[0a11] fd 36 d0 02
                    ld        hl,$5c3b                      ;[0a15] 21 3b 5c
                    ld        a,(hl)                        ;[0a18] 7e
                    or        $0c                           ;[0a19] f6 0c
                    ld        (hl),a                        ;[0a1b] 77
                    ret                                     ;[0a1c] c9

                    xor       a                             ;[0a1d] af
                    ld        d,$20                         ;[0a1e] 16 20
                    ld        bc,$243b                      ;[0a20] 01 3b 24
                    nextreg $43,a                           ;[0a23] ed 92 43
                    ld        e,$00                         ;[0a26] 1e 00
                    ld        a,e                           ;[0a28] 7b
                    nextreg $40,a                           ;[0a29] ed 92 40
                    ld        a,$41                         ;[0a2c] 3e 41
                    out       (c),a                         ;[0a2e] ed 79
                    inc       b                             ;[0a30] 04
                    in        a,(c)                         ;[0a31] ed 78
                    dec       b                             ;[0a33] 05
                    ld        (hl),a                        ;[0a34] 77
                    inc       hl                            ;[0a35] 23
                    ld        a,$44                         ;[0a36] 3e 44
                    out       (c),a                         ;[0a38] ed 79
                    inc       b                             ;[0a3a] 04
                    in        a,(c)                         ;[0a3b] ed 78
                    dec       b                             ;[0a3d] 05
                    ld        (hl),a                        ;[0a3e] 77
                    inc       hl                            ;[0a3f] 23
                    inc       e                             ;[0a40] 1c
                    dec       d                             ;[0a41] 15
                    jr        nz,$0a28                      ;[0a42] 20 e4
                    ret                                     ;[0a44] c9

                    xor       a                             ;[0a45] af
                    ld        d,$20                         ;[0a46] 16 20
                    ld        e,$00                         ;[0a48] 1e 00
                    nextreg $43,a                           ;[0a4a] ed 92 43
                    ld        a,e                           ;[0a4d] 7b
                    nextreg $40,a                           ;[0a4e] ed 92 40
                    ld        a,(hl)                        ;[0a51] 7e
                    inc       hl                            ;[0a52] 23
                    nextreg $44,a                           ;[0a53] ed 92 44
                    ld        a,(hl)                        ;[0a56] 7e
                    inc       hl                            ;[0a57] 23
                    nextreg $44,a                           ;[0a58] ed 92 44
                    inc       e                             ;[0a5b] 1c
                    dec       d                             ;[0a5c] 15
                    jr        nz,$0a4d                      ;[0a5d] 20 ee
                    ret                                     ;[0a5f] c9

                    push      bc                            ;[0a60] c5
                    ld        bc,$0020                      ;[0a61] 01 20 00
                    push      de                            ;[0a64] d5
                    ldir                                    ;[0a65] ed b0
                    ex        de,hl                         ;[0a67] eb
                    call      $0a1d                         ;[0a68] cd 1d 0a
                    pop       hl                            ;[0a6b] e1
                    pop       de                            ;[0a6c] d1
                    jr        $0a86                         ;[0a6d] 18 17
                    ld        de,$3700                      ;[0a6f] 11 00 37
                    dec       a                             ;[0a72] 3d
                    jr        z,$0a85                       ;[0a73] 28 10
                    dec       a                             ;[0a75] 3d
                    jr        z,$0a86                       ;[0a76] 28 0e
                    dec       a                             ;[0a78] 3d
                    jr        z,$0a9f                       ;[0a79] 28 24
                    dec       a                             ;[0a7b] 3d
                    jp        z,$0b03                       ;[0a7c] ca 03 0b
                    ld        de,$3760                      ;[0a7f] 11 60 37
                    dec       a                             ;[0a82] 3d
                    jr        nz,$0a86                      ;[0a83] 20 01
                    ex        de,hl                         ;[0a85] eb
                    ld        bc,$0060                      ;[0a86] 01 60 00
                    rst       $20                           ;[0a89] e7
                    ld        l,b                           ;[0a8a] 68
                    nop                                     ;[0a8b] 00
                    scf                                     ;[0a8c] 37
                    ret                                     ;[0a8d] c9

                    ld        a,($d5b8)                     ;[0a8e] 3a b8 d5
                    cp        $04                           ;[0a91] fe 04
                    ret       z                             ;[0a93] c8
                    call      $0973                         ;[0a94] cd 73 09
                    ld        hl,$d694                      ;[0a97] 21 94 d6
                    ld        de,$3760                      ;[0a9a] 11 60 37
                    jr        $0aa5                         ;[0a9d] 18 06
                    ld        hl,$d5ed                      ;[0a9f] 21 ed d5
                    ld        de,$3700                      ;[0aa2] 11 00 37
                    push      de                            ;[0aa5] d5
                    ld        a,$43                         ;[0aa6] 3e 43
                    call      $0d6b                         ;[0aa8] cd 6b 0d
                    ld        (hl),a                        ;[0aab] 77
                    inc       hl                            ;[0aac] 23
                    ld        a,$15                         ;[0aad] 3e 15
                    call      $0d6b                         ;[0aaf] cd 6b 0d
                    ld        (hl),a                        ;[0ab2] 77
                    inc       hl                            ;[0ab3] 23
                    ld        a,$14                         ;[0ab4] 3e 14
                    call      $0d6b                         ;[0ab6] cd 6b 0d
                    ld        (hl),a                        ;[0ab9] 77
                    inc       hl                            ;[0aba] 23
                    ld        a,($5c8d)                     ;[0abb] 3a 8d 5c
                    ld        (hl),a                        ;[0abe] 77
                    inc       hl                            ;[0abf] 23
                    ld        a,($5b62)                     ;[0ac0] 3a 62 5b
                    ld        (hl),a                        ;[0ac3] 77
                    inc       hl                            ;[0ac4] 23
                    ld        a,($5c48)                     ;[0ac5] 3a 48 5c
                    ld        (hl),a                        ;[0ac8] 77
                    inc       hl                            ;[0ac9] 23
                    call      $0a1d                         ;[0aca] cd 1d 0a
                    pop       de                            ;[0acd] d1
                    push      hl                            ;[0ace] e5
                    call      $0a85                         ;[0acf] cd 85 0a
                    pop       hl                            ;[0ad2] e1
                    push      hl                            ;[0ad3] e5
                    add       hl,$0020                      ;[0ad4] ed 34 20 00
                    call      $0a45                         ;[0ad8] cd 45 0a
                    pop       hl                            ;[0adb] e1
                    ld        a,(hl)                        ;[0adc] 7e
                    ld        ($5c8d),a                     ;[0add] 32 8d 5c
                    ld        ($5c48),a                     ;[0ae0] 32 48 5c
                    push      af                            ;[0ae3] f5
                    rra                                     ;[0ae4] 1f
                    rra                                     ;[0ae5] 1f
                    rra                                     ;[0ae6] 1f
                    out       ($fe),a                       ;[0ae7] d3 fe
                    pop       af                            ;[0ae9] f1
                    cpl                                     ;[0aea] 2f
                    and       $38                           ;[0aeb] e6 38
                    ld        ($5b62),a                     ;[0aed] 32 62 5b
                    ld        a,$4a                         ;[0af0] 3e 4a
                    call      $0d6b                         ;[0af2] cd 6b 0d
                    nextreg $14,a                           ;[0af5] ed 92 14
                    nextreg $15,$00                         ;[0af8] ed 91 15 00
                    scf                                     ;[0afc] 37
                    ret                                     ;[0afd] c9

                    ld        hl,$d694                      ;[0afe] 21 94 d6
                    jr        $0b06                         ;[0b01] 18 03
                    ld        hl,$d5ed                      ;[0b03] 21 ed d5
                    ld        a,(hl)                        ;[0b06] 7e
                    push      af                            ;[0b07] f5
                    inc       hl                            ;[0b08] 23
                    ld        a,(hl)                        ;[0b09] 7e
                    nextreg $15,a                           ;[0b0a] ed 92 15
                    inc       hl                            ;[0b0d] 23
                    ld        a,(hl)                        ;[0b0e] 7e
                    nextreg $14,a                           ;[0b0f] ed 92 14
                    inc       hl                            ;[0b12] 23
                    ld        a,(hl)                        ;[0b13] 7e
                    ld        ($5c8d),a                     ;[0b14] 32 8d 5c
                    inc       hl                            ;[0b17] 23
                    ld        a,(hl)                        ;[0b18] 7e
                    ld        ($5b62),a                     ;[0b19] 32 62 5b
                    inc       hl                            ;[0b1c] 23
                    ld        a,(hl)                        ;[0b1d] 7e
                    ld        ($5c48),a                     ;[0b1e] 32 48 5c
                    rra                                     ;[0b21] 1f
                    rra                                     ;[0b22] 1f
                    rra                                     ;[0b23] 1f
                    out       ($fe),a                       ;[0b24] d3 fe
                    inc       hl                            ;[0b26] 23
                    call      $0a45                         ;[0b27] cd 45 0a
                    pop       af                            ;[0b2a] f1
                    nextreg $43,a                           ;[0b2b] ed 92 43
                    scf                                     ;[0b2e] 37
                    ret                                     ;[0b2f] c9

                    jr        c,$0b82                       ;[0b30] 38 50
                    ld        d,(hl)                        ;[0b32] 56
                    ld        h,(hl)                        ;[0b33] 66
                    ld        h,l                           ;[0b34] 65
                    ld        b,l                           ;[0b35] 45
                    ld        b,a                           ;[0b36] 47
                    rlca                                    ;[0b37] 07
                    jr        nc,$0b62                      ;[0b38] 30 28
                    jr        z,$0b64                       ;[0b3a] 28 28
                    jr        z,$0b3e                       ;[0b3c] 28 00
                    nop                                     ;[0b3e] 00
                    nop                                     ;[0b3f] 00
                    jr        c,$0b7a                       ;[0b40] 38 38
                    jr        c,$0b7c                       ;[0b42] 38 38
                    jr        c,$0b46                       ;[0b44] 38 00
                    nop                                     ;[0b46] 00
                    nop                                     ;[0b47] 00
                    nop                                     ;[0b48] 00
                    nop                                     ;[0b49] 00
                    nop                                     ;[0b4a] 00
                    nop                                     ;[0b4b] 00
                    nop                                     ;[0b4c] 00
                    nop                                     ;[0b4d] 00
                    nop                                     ;[0b4e] 00
                    nop                                     ;[0b4f] 00
                    jr        c,$0ba2                       ;[0b50] 38 50
                    ld        d,(hl)                        ;[0b52] 56
                    ld        h,(hl)                        ;[0b53] 66
                    ld        h,l                           ;[0b54] 65
                    ld        b,l                           ;[0b55] 45
                    ld        b,a                           ;[0b56] 47
                    rlca                                    ;[0b57] 07
                    ld        l,b                           ;[0b58] 68
                    ld        a,b                           ;[0b59] 78
                    ld        l,c                           ;[0b5a] 69
                    ld        a,c                           ;[0b5b] 79
                    add       hl,hl                         ;[0b5c] 29
                    add       hl,sp                         ;[0b5d] 39
                    rst       $08                           ;[0b5e] cf
                    rst       $20                           ;[0b5f] e7
                    rst       $18                           ;[0b60] df
                    rst       $28                           ;[0b61] ef
                    rst       $10                           ;[0b62] d7
                    dec       a                             ;[0b63] 3d
                    jr        c,$0ba1                       ;[0b64] 38 3b
                    inc       a                             ;[0b66] 3c
                    ld        a,($3d39)                     ;[0b67] 3a 39 3d
                    jr        c,$0ba4                       ;[0b6a] 38 38
                    nop                                     ;[0b6c] 00
                    nop                                     ;[0b6d] 00
                    nop                                     ;[0b6e] 00
                    nop                                     ;[0b6f] 00
                    nextreg $51,$10                         ;[0b70] ed 91 51 10
                    ld        hl,$23b7                      ;[0b74] 21 b7 23
                    scf                                     ;[0b77] 37
                    call      $09c7                         ;[0b78] cd c7 09
                    xor       a                             ;[0b7b] af
                    out       ($fe),a                       ;[0b7c] d3 fe
                    ld        bc,$243b                      ;[0b7e] 01 3b 24
                    ld        a,$15                         ;[0b81] 3e 15
                    out       (c),a                         ;[0b83] ed 79
                    inc       b                             ;[0b85] 04
                    in        h,(c)                         ;[0b86] ed 60
                    in        a,($ff)                       ;[0b88] db ff
                    ld        l,a                           ;[0b8a] 6f
                    push      hl                            ;[0b8b] e5
                    or        $38                           ;[0b8c] f6 38
                    out       ($ff),a                       ;[0b8e] d3 ff
                    res       1,h                           ;[0b90] cb 8c
                    out       (c),h                         ;[0b92] ed 61
                    ld        hl,($5c78)                    ;[0b94] 2a 78 5c
                    res       7,h                           ;[0b97] cb bc
                    res       7,l                           ;[0b99] cb bd
                    ld        a,l                           ;[0b9b] 7d
                    and       $03                           ;[0b9c] e6 03
                    inc       a                             ;[0b9e] 3c
                    ld        d,a                           ;[0b9f] 57
                    ld        a,h                           ;[0ba0] 7c
                    and       $03                           ;[0ba1] e6 03
                    inc       a                             ;[0ba3] 3c
                    ld        e,a                           ;[0ba4] 5f
                    ld        a,h                           ;[0ba5] 7c
                    add       d                             ;[0ba6] 82
                    cp        $e8                           ;[0ba7] fe e8
                    jr        c,$0bb0                       ;[0ba9] 38 05
                    ld        a,d                           ;[0bab] 7a
                    neg                                     ;[0bac] ed 44
                    ld        d,a                           ;[0bae] 57
                    add       h                             ;[0baf] 84
                    ld        h,a                           ;[0bb0] 67
                    ld        c,$c8                         ;[0bb1] 0e c8
                    call      $0bfd                         ;[0bb3] cd fd 0b
                    ld        a,l                           ;[0bb6] 7d
                    add       e                             ;[0bb7] 83
                    cp        $a8                           ;[0bb8] fe a8
                    jr        c,$0bc1                       ;[0bba] 38 05
                    ld        a,e                           ;[0bbc] 7b
                    neg                                     ;[0bbd] ed 44
                    ld        e,a                           ;[0bbf] 5f
                    add       l                             ;[0bc0] 85
                    ld        l,a                           ;[0bc1] 6f
                    ld        c,$50                         ;[0bc2] 0e 50
                    call      $0bfd                         ;[0bc4] cd fd 0b
                    halt                                    ;[0bc7] 76
                    bit       5,(iy+$01)                    ;[0bc8] fd cb 01 6e
                    jr        z,$0ba5                       ;[0bcc] 28 d7
                    xor       a                             ;[0bce] af
                    ld        bc,$ff90                      ;[0bcf] 01 90 ff
                    call      $0c07                         ;[0bd2] cd 07 0c
                    xor       a                             ;[0bd5] af
                    ld        bc,$bf20                      ;[0bd6] 01 20 bf
                    call      $0c07                         ;[0bd9] cd 07 0c
                    pop       hl                            ;[0bdc] e1
                    ld        a,h                           ;[0bdd] 7c
                    nextreg $15,a                           ;[0bde] ed 92 15
                    ld        a,l                           ;[0be1] 7d
                    out       ($ff),a                       ;[0be2] d3 ff
                    ld        a,($5c48)                     ;[0be4] 3a 48 5c
                    rrca                                    ;[0be7] 0f
                    rrca                                    ;[0be8] 0f
                    rrca                                    ;[0be9] 0f
                    out       ($fe),a                       ;[0bea] d3 fe
                    res       5,(iy+$01)                    ;[0bec] fd cb 01 ae
                    ld        hl,$23b7                      ;[0bf0] 21 b7 23
                    call      $09c6                         ;[0bf3] cd c6 09
                    nextreg $51,$ff                         ;[0bf6] ed 91 51 ff
                    jp        $0c6d                         ;[0bfa] c3 6d 0c
                    ld        b,$18                         ;[0bfd] 06 18
                    bit       7,(iy+$47)                    ;[0bff] fd cb 47 7e
                    jr        z,$0c07                       ;[0c03] 28 02
                    ld        b,$ff                         ;[0c05] 06 ff
                    push      hl                            ;[0c07] e5
                    push      de                            ;[0c08] d5
                    push      bc                            ;[0c09] c5
                    ld        e,a                           ;[0c0a] 5f
                    add       b                             ;[0c0b] 80
                    ld        d,a                           ;[0c0c] 57
                    ld        hl,$0318                      ;[0c0d] 21 18 03
                    ld        bc,$243b                      ;[0c10] 01 3b 24
                    out       (c),l                         ;[0c13] ed 69
                    inc       b                             ;[0c15] 04
                    out       (c),e                         ;[0c16] ed 59
                    out       (c),d                         ;[0c18] ed 51
                    inc       l                             ;[0c1a] 2c
                    dec       h                             ;[0c1b] 25
                    jr        nz,$0c10                      ;[0c1c] 20 f2
                    dec       b                             ;[0c1e] 05
                    out       (c),l                         ;[0c1f] ed 69
                    inc       b                             ;[0c21] 04
                    pop       hl                            ;[0c22] e1
                    sla       l                             ;[0c23] cb 25
                    push      af                            ;[0c25] f5
                    ld        a,e                           ;[0c26] 7b
                    jr        nc,$0c2b                      ;[0c27] 30 02
                    srl       a                             ;[0c29] cb 3f
                    bit       7,l                           ;[0c2b] cb 7d
                    res       7,l                           ;[0c2d] cb bd
                    jr        z,$0c32                       ;[0c2f] 28 01
                    add       l                             ;[0c31] 85
                    out       (c),a                         ;[0c32] ed 79
                    pop       af                            ;[0c34] f1
                    ld        a,d                           ;[0c35] 7a
                    jr        nc,$0c3a                      ;[0c36] 30 02
                    srl       a                             ;[0c38] cb 3f
                    add       l                             ;[0c3a] 85
                    out       (c),a                         ;[0c3b] ed 79
                    pop       de                            ;[0c3d] d1
                    pop       hl                            ;[0c3e] e1
                    ret                                     ;[0c3f] c9

                    call      $0a8e                         ;[0c40] cd 8e 0a
                    ld        bc,$0017                      ;[0c43] 01 17 00
                    call      $0d04                         ;[0c46] cd 04 0d
                    ld        a,$ff                         ;[0c49] 3e ff
                    ld        ($d5b8),a                     ;[0c4b] 32 b8 d5
                    ld        sp,$5bff                      ;[0c4e] 31 ff 5b
                    xor       a                             ;[0c51] af
                    call      $0068                         ;[0c52] cd 68 00
                    exx                                     ;[0c55] d9
                    jr        nz,$0c92                      ;[0c56] 20 3a
                    ld        b,h                           ;[0c58] 44
                    rst       $10                           ;[0c59] d7
                    and       a                             ;[0c5a] a7
                    jr        nz,$0c51                      ;[0c5b] 20 f4
                    ld        a,$01                         ;[0c5d] 3e 01
                    jr        $0c52                         ;[0c5f] 18 f1
                    ld        a,($d5b8)                     ;[0c61] 3a b8 d5
                    and       a                             ;[0c64] a7
                    ret       nz                            ;[0c65] c0
                    pop       hl                            ;[0c66] e1
                    call      $0068                         ;[0c67] cd 68 00
                    adc       (hl)                          ;[0c6a] 8e
                    inc       de                            ;[0c6b] 13
                    ret                                     ;[0c6c] c9

                    ld        a,($5c81)                     ;[0c6d] 3a 81 5c
                    and       $7f                           ;[0c70] e6 7f
                    push      af                            ;[0c72] f5
                    ld        a,($5b68)                     ;[0c73] 3a 68 5b
                    bit       1,a                           ;[0c76] cb 4f
                    jr        z,$0c7d                       ;[0c78] 28 03
                    rst       $18                           ;[0c7a] df
                    ld        l,d                           ;[0c7b] 6a
                    scf                                     ;[0c7c] 37
                    ld        bc,$0bb8                      ;[0c7d] 01 b8 0b
                    pop       af                            ;[0c80] f1
                    and       a                             ;[0c81] a7
                    jr        z,$0c8f                       ;[0c82] 28 0b
                    dec       c                             ;[0c84] 0d
                    jr        nz,$0c8f                      ;[0c85] 20 08
                    djnz      $0c8f                         ;[0c87] 10 06
                    dec       a                             ;[0c89] 3d
                    jp        z,$0b70                       ;[0c8a] ca 70 0b
                    jr        $0c72                         ;[0c8d] 18 e3
                    halt                                    ;[0c8f] 76
                    ld        hl,$5c3b                      ;[0c90] 21 3b 5c
                    bit       5,(hl)                        ;[0c93] cb 6e
                    jr        z,$0c81                       ;[0c95] 28 ea
                    res       5,(hl)                        ;[0c97] cb ae
                    ld        a,($5c08)                     ;[0c99] 3a 08 5c
                    ld        hl,$5c41                      ;[0c9c] 21 41 5c
                    cp        $0e                           ;[0c9f] fe 0e
                    jr        z,$0ca9                       ;[0ca1] 28 06
                    res       0,(hl)                        ;[0ca3] cb 86
                    cp        $10                           ;[0ca5] fe 10
                    jr        nc,$0cc8                      ;[0ca7] 30 1f
                    push      af                            ;[0ca9] f5
                    cp        $06                           ;[0caa] fe 06
                    jr        nz,$0cb7                      ;[0cac] 20 09
                    ld        hl,$5c6a                      ;[0cae] 21 6a 5c
                    ld        a,$08                         ;[0cb1] 3e 08
                    xor       (hl)                          ;[0cb3] ae
                    ld        (hl),a                        ;[0cb4] 77
                    jr        $0cc3                         ;[0cb5] 18 0c
                    cp        $0e                           ;[0cb7] fe 0e
                    jr        c,$0cc7                       ;[0cb9] 38 0c
                    sub       $0d                           ;[0cbb] d6 0d
                    cp        (hl)                          ;[0cbd] be
                    ld        (hl),a                        ;[0cbe] 77
                    jr        nz,$0cc3                      ;[0cbf] 20 02
                    ld        (hl),$00                      ;[0cc1] 36 00
                    set       3,(iy+$02)                    ;[0cc3] fd cb 02 de
                    pop       af                            ;[0cc7] f1
                    bit       7,(iy+$30)                    ;[0cc8] fd cb 30 7e
                    scf                                     ;[0ccc] 37
                    ret       nz                            ;[0ccd] c0
                    bit       1,(iy+$07)                    ;[0cce] fd cb 07 4e
                    ret       nz                            ;[0cd2] c0
                    ld        hl,$0cf1                      ;[0cd3] 21 f1 0c
                    ld        b,$08                         ;[0cd6] 06 08
                    cp        (hl)                          ;[0cd8] be
                    inc       hl                            ;[0cd9] 23
                    jr        nz,$0cdd                      ;[0cda] 20 01
                    ld        a,(hl)                        ;[0cdc] 7e
                    inc       hl                            ;[0cdd] 23
                    djnz      $0cd8                         ;[0cde] 10 f8
                    scf                                     ;[0ce0] 37
                    ret                                     ;[0ce1] c9

                    call      $0c6d                         ;[0ce2] cd 6d 0c
                    push      af                            ;[0ce5] f5
                    ld        a,($5c39)                     ;[0ce6] 3a 39 5c
                    ld        hl,$00c8                      ;[0ce9] 21 c8 00
                    call      $3e20                         ;[0cec] cd 20 3e
                    pop       af                            ;[0cef] f1
                    ret                                     ;[0cf0] c9

                    add       $5b                           ;[0cf1] c6 5b
                    push      bc                            ;[0cf3] c5
                    ld        e,l                           ;[0cf4] 5d
                    rst       $00                           ;[0cf5] c7
                    ld        a,a                           ;[0cf6] 7f
                    jp        po,$c37e                      ;[0cf7] e2 7e c3
                    ld        a,h                           ;[0cfa] 7c
                    call      $cc5c                         ;[0cfb] cd 5c cc
                    ld        a,e                           ;[0cfe] 7b
                    bit       7,l                           ;[0cff] cb 7d
                    ld        bc,$1517                      ;[0d01] 01 17 15
                    ld        a,c                           ;[0d04] 79
                    sub       b                             ;[0d05] 90
                    inc       a                             ;[0d06] 3c
                    ld        c,a                           ;[0d07] 4f
                    ld        a,$18                         ;[0d08] 3e 18
                    sub       b                             ;[0d0a] 90
                    ld        b,a                           ;[0d0b] 47
                    push      bc                            ;[0d0c] c5
                    rst       $28                           ;[0d0d] ef
                    sbc       e                             ;[0d0e] 9b
                    ld        c,$0e                         ;[0d0f] 0e 0e
                    ex        af,af'                        ;[0d11] 08
                    bit       3,(iy+$45)                    ;[0d12] fd cb 45 5e
                    jr        z,$0d3b                       ;[0d16] 28 23
                    ex        af,af'                        ;[0d18] 08
                    ld        a,$0b                         ;[0d19] 3e 0b
                    ex        af,af'                        ;[0d1b] 08
                    call      $0d5e                         ;[0d1c] cd 5e 0d
                    push      hl                            ;[0d1f] e5
                    xor       a                             ;[0d20] af
                    ld        b,$20                         ;[0d21] 06 20
                    ld        (hl),a                        ;[0d23] 77
                    inc       hl                            ;[0d24] 23
                    djnz      $0d23                         ;[0d25] 10 fc
                    pop       hl                            ;[0d27] e1
                    push      hl                            ;[0d28] e5
                    set       5,h                           ;[0d29] cb ec
                    ld        b,$20                         ;[0d2b] 06 20
                    ld        (hl),a                        ;[0d2d] 77
                    inc       hl                            ;[0d2e] 23
                    djnz      $0d2d                         ;[0d2f] 10 fc
                    pop       hl                            ;[0d31] e1
                    inc       h                             ;[0d32] 24
                    dec       c                             ;[0d33] 0d
                    jr        nz,$0d1f                      ;[0d34] 20 e9
                    call      $0d5e                         ;[0d36] cd 5e 0d
                    jr        $0d48                         ;[0d39] 18 0d
                    push      hl                            ;[0d3b] e5
                    ld        b,$20                         ;[0d3c] 06 20
                    xor       a                             ;[0d3e] af
                    ld        (hl),a                        ;[0d3f] 77
                    inc       hl                            ;[0d40] 23
                    djnz      $0d3f                         ;[0d41] 10 fc
                    pop       hl                            ;[0d43] e1
                    inc       h                             ;[0d44] 24
                    dec       c                             ;[0d45] 0d
                    jr        nz,$0d3b                      ;[0d46] 20 f3
                    ld        b,$20                         ;[0d48] 06 20
                    push      bc                            ;[0d4a] c5
                    rst       $28                           ;[0d4b] ef
                    adc       b                             ;[0d4c] 88
                    ld        c,$eb                         ;[0d4d] 0e eb
                    pop       bc                            ;[0d4f] c1
                    ld        a,($5c8d)                     ;[0d50] 3a 8d 5c
                    ld        (hl),a                        ;[0d53] 77
                    inc       hl                            ;[0d54] 23
                    djnz      $0d53                         ;[0d55] 10 fc
                    pop       bc                            ;[0d57] c1
                    dec       b                             ;[0d58] 05
                    dec       c                             ;[0d59] 0d
                    jr        nz,$0d0c                      ;[0d5a] 20 b0
                    scf                                     ;[0d5c] 37
                    ret                                     ;[0d5d] c9

                    push      af                            ;[0d5e] f5
                    push      bc                            ;[0d5f] c5
                    ld        a,$53                         ;[0d60] 3e 53
                    call      $0d6b                         ;[0d62] cd 6b 0d
                    ex        af,af'                        ;[0d65] 08
                    out       (c),a                         ;[0d66] ed 79
                    pop       bc                            ;[0d68] c1
                    pop       af                            ;[0d69] f1
                    ret                                     ;[0d6a] c9

                    ld        bc,$243b                      ;[0d6b] 01 3b 24
                    out       (c),a                         ;[0d6e] ed 79
                    inc       b                             ;[0d70] 04
                    in        a,(c)                         ;[0d71] ed 78
                    ret                                     ;[0d73] c9

                    ld        hl,$0000                      ;[0d74] 21 00 00
                    ld        ($5c49),hl                    ;[0d77] 22 49 5c
                    rst       $08                           ;[0d7a] cf
                    ld        ($d752),hl                    ;[0d7b] 22 52 d7
                    rst       $30                           ;[0d7e] f7
                    ret                                     ;[0d7f] c9

                    inc       d                             ;[0d80] 14
                    ld        bc,$2047                      ;[0d81] 01 47 20
                    inc       d                             ;[0d84] 14
                    nop                                     ;[0d85] 00
                    add       hl,de                         ;[0d86] 19
                    or        b                             ;[0d87] b0
                    ld        b,$00                         ;[0d88] 06 00
                    ld        (hl),l                        ;[0d8a] 75
                    ld        l,c                           ;[0d8b] 69
                    ld        h,h                           ;[0d8c] 64
                    ld        h,l                           ;[0d8d] 65
                    jr        nz,$0da4                      ;[0d8e] 20 14
                    ld        bc,$204c                      ;[0d90] 01 4c 20
                    inc       d                             ;[0d93] 14
                    nop                                     ;[0d94] 00
                    add       hl,de                         ;[0d95] 19
                    or        b                             ;[0d96] b0
                    dec       h                             ;[0d97] 25
                    nop                                     ;[0d98] 00
                    ld        l,c                           ;[0d99] 69
                    ld        l,(hl)                        ;[0d9a] 6e
                    ld        l,e                           ;[0d9b] 6b
                    ld        (hl),e                        ;[0d9c] 73
                    add       hl,de                         ;[0d9d] 19
                    or        b                             ;[0d9e] b0
                    ld        b,c                           ;[0d9f] 41
                    nop                                     ;[0da0] 00
                    ld        b,l                           ;[0da1] 45
                    ld        c,(hl)                        ;[0da2] 4e
                    ld        d,h                           ;[0da3] 54
                    ld        b,l                           ;[0da4] 45
                    ld        d,d                           ;[0da5] 52
                    dec       a                             ;[0da6] 3d
                    ld        (hl),e                        ;[0da7] 73
                    ld        h,l                           ;[0da8] 65
                    ld        l,h                           ;[0da9] 6c
                    ld        h,l                           ;[0daa] 65
                    ld        h,e                           ;[0dab] 63
                    ld        (hl),h                        ;[0dac] 74
                    jr        nz,$0df4                      ;[0dad] 20 45
                    ld        b,h                           ;[0daf] 44
                    ld        c,c                           ;[0db0] 49
                    ld        d,h                           ;[0db1] 54
                    dec       a                             ;[0db2] 3d
                    ld        (hl),l                        ;[0db3] 75
                    ld        (hl),b                        ;[0db4] 70
                    jr        nz,$0dfc                      ;[0db5] 20 45
                    ld        e,b                           ;[0db7] 58
                    ld        d,h                           ;[0db8] 54
                    ld        b,l                           ;[0db9] 45
                    ld        c,(hl)                        ;[0dba] 4e
                    ld        b,h                           ;[0dbb] 44
                    dec       a                             ;[0dbc] 3d
                    ld        l,l                           ;[0dbd] 6d
                    ld        l,a                           ;[0dbe] 6f
                    ld        (hl),d                        ;[0dbf] 72
                    ld        h,l                           ;[0dc0] 65
                    jr        nz,$0e05                      ;[0dc1] 20 42
                    ld        d,d                           ;[0dc3] 52
                    ld        b,l                           ;[0dc4] 45
                    ld        b,c                           ;[0dc5] 41
                    ld        c,e                           ;[0dc6] 4b
                    dec       c                             ;[0dc7] 0d
                    inc       d                             ;[0dc8] 14
                    ld        bc,$2044                      ;[0dc9] 01 44 20
                    inc       d                             ;[0dcc] 14
                    nop                                     ;[0dcd] 00
                    add       hl,de                         ;[0dce] 19
                    cp        b                             ;[0dcf] b8
                    ld        b,$00                         ;[0dd0] 06 00
                    ld        (hl),d                        ;[0dd2] 72
                    ld        l,c                           ;[0dd3] 69
                    halt                                    ;[0dd4] 76
                    ld        h,l                           ;[0dd5] 65
                    jr        nz,$0dec                      ;[0dd6] 20 14
                    ld        bc,$2043                      ;[0dd8] 01 43 20
                    inc       d                             ;[0ddb] 14
                    nop                                     ;[0ddc] 00
                    add       hl,de                         ;[0ddd] 19
                    cp        b                             ;[0dde] b8
                    dec       h                             ;[0ddf] 25
                    nop                                     ;[0de0] 00
                    ld        l,a                           ;[0de1] 6f
                    ld        (hl),b                        ;[0de2] 70
                    ld        a,c                           ;[0de3] 79
                    jr        nz,$0dff                      ;[0de4] 20 19
                    cp        b                             ;[0de6] b8
                    jr        c,$0de9                       ;[0de7] 38 00
                    ld        l,l                           ;[0de9] 6d
                    ld        l,a                           ;[0dea] 6f
                    add       hl,de                         ;[0deb] 19
                    cp        b                             ;[0dec] b8
                    ld        b,e                           ;[0ded] 43
                    nop                                     ;[0dee] 00
                    inc       d                             ;[0def] 14
                    ld        bc,$1456                      ;[0df0] 01 56 14
                    nop                                     ;[0df3] 00
                    ld        h,l                           ;[0df4] 65
                    jr        nz,$0e10                      ;[0df5] 20 19
                    cp        b                             ;[0df7] b8
                    ld        d,c                           ;[0df8] 51
                    nop                                     ;[0df9] 00
                    inc       d                             ;[0dfa] 14
                    ld        bc,$2052                      ;[0dfb] 01 52 20
                    inc       d                             ;[0dfe] 14
                    nop                                     ;[0dff] 00
                    add       hl,de                         ;[0e00] 19
                    cp        b                             ;[0e01] b8
                    ld        d,a                           ;[0e02] 57
                    nop                                     ;[0e03] 00
                    ld        h,l                           ;[0e04] 65
                    ld        l,(hl)                        ;[0e05] 6e
                    ld        h,c                           ;[0e06] 61
                    ld        l,l                           ;[0e07] 6d
                    ld        h,l                           ;[0e08] 65
                    jr        nz,$0e24                      ;[0e09] 20 19
                    cp        b                             ;[0e0b] b8
                    ld        (hl),h                        ;[0e0c] 74
                    nop                                     ;[0e0d] 00
                    inc       d                             ;[0e0e] 14
                    ld        bc,$2045                      ;[0e0f] 01 45 20
                    inc       d                             ;[0e12] 14
                    nop                                     ;[0e13] 00
                    add       hl,de                         ;[0e14] 19
                    cp        b                             ;[0e15] b8
                    ld        a,d                           ;[0e16] 7a
                    nop                                     ;[0e17] 00
                    ld        (hl),d                        ;[0e18] 72
                    ld        h,c                           ;[0e19] 61
                    ld        (hl),e                        ;[0e1a] 73
                    ld        h,l                           ;[0e1b] 65
                    jr        nz,$0e37                      ;[0e1c] 20 19
                    cp        b                             ;[0e1e] b8
                    sub       d                             ;[0e1f] 92
                    nop                                     ;[0e20] 00
                    ld        l,l                           ;[0e21] 6d
                    add       hl,de                         ;[0e22] 19
                    cp        b                             ;[0e23] b8
                    sbc       b                             ;[0e24] 98
                    nop                                     ;[0e25] 00
                    inc       d                             ;[0e26] 14
                    ld        bc,$204b                      ;[0e27] 01 4b 20
                    inc       d                             ;[0e2a] 14
                    nop                                     ;[0e2b] 00
                    add       hl,de                         ;[0e2c] 19
                    cp        b                             ;[0e2d] b8
                    sbc       (hl)                          ;[0e2e] 9e
                    nop                                     ;[0e2f] 00
                    ld        h,h                           ;[0e30] 64
                    ld        l,c                           ;[0e31] 69
                    ld        (hl),d                        ;[0e32] 72
                    jr        nz,$0e4e                      ;[0e33] 20 19
                    cp        b                             ;[0e35] b8
                    or        b                             ;[0e36] b0
                    nop                                     ;[0e37] 00
                    inc       d                             ;[0e38] 14
                    ld        bc,$2055                      ;[0e39] 01 55 20
                    inc       d                             ;[0e3c] 14
                    nop                                     ;[0e3d] 00
                    add       hl,de                         ;[0e3e] 19
                    cp        b                             ;[0e3f] b8
                    or        (hl)                          ;[0e40] b6
                    nop                                     ;[0e41] 00
                    ld        l,(hl)                        ;[0e42] 6e
                    add       hl,de                         ;[0e43] 19
                    cp        b                             ;[0e44] b8
                    cp        h                             ;[0e45] bc
                    nop                                     ;[0e46] 00
                    ld        l,l                           ;[0e47] 6d
                    ld        l,a                           ;[0e48] 6f
                    ld        (hl),l                        ;[0e49] 75
                    ld        l,(hl)                        ;[0e4a] 6e
                    ld        (hl),h                        ;[0e4b] 74
                    jr        nz,$0e67                      ;[0e4c] 20 19
                    cp        b                             ;[0e4e] b8
                    ret       c                             ;[0e4f] d8
                    nop                                     ;[0e50] 00
                    ld        (hl),d                        ;[0e51] 72
                    ld        h,l                           ;[0e52] 65
                    add       hl,de                         ;[0e53] 19
                    cp        b                             ;[0e54] b8
                    ex        (sp),hl                       ;[0e55] e3
                    nop                                     ;[0e56] 00
                    inc       d                             ;[0e57] 14
                    ld        bc,$1920                      ;[0e58] 01 20 19
                    cp        b                             ;[0e5b] b8
                    call      po,$4d00                      ;[0e5c] e4 00 4d
                    jr        nz,$0e75                      ;[0e5f] 20 14
                    nop                                     ;[0e61] 00
                    add       hl,de                         ;[0e62] 19
                    cp        b                             ;[0e63] b8
                    jp        pe,$6f00                      ;[0e64] ea 00 6f
                    ld        (hl),l                        ;[0e67] 75
                    ld        l,(hl)                        ;[0e68] 6e
                    ld        (hl),h                        ;[0e69] 74
                    rst       $38                           ;[0e6a] ff
                    ld        b,e                           ;[0e6b] 43
                    ld        l,b                           ;[0e6c] 68
                    ld        h,c                           ;[0e6d] 61
                    ld        l,(hl)                        ;[0e6e] 6e
                    ld        h,a                           ;[0e6f] 67
                    ld        h,l                           ;[0e70] 65
                    jr        nz,$0ee7                      ;[0e71] 20 74
                    ld        l,a                           ;[0e73] 6f
                    jr        nz,$0eda                      ;[0e74] 20 64
                    ld        h,l                           ;[0e76] 65
                    ld        (hl),e                        ;[0e77] 73
                    ld        (hl),h                        ;[0e78] 74
                    ld        l,c                           ;[0e79] 69
                    ld        l,(hl)                        ;[0e7a] 6e
                    ld        h,c                           ;[0e7b] 61
                    ld        (hl),h                        ;[0e7c] 74
                    ld        l,c                           ;[0e7d] 69
                    ld        l,a                           ;[0e7e] 6f
                    ld        l,(hl)                        ;[0e7f] 6e
                    jr        nz,$0ee3                      ;[0e80] 20 61
                    ld        l,(hl)                        ;[0e82] 6e
                    ld        h,h                           ;[0e83] 64
                    jr        nz,$0ef6                      ;[0e84] 20 70
                    ld        (hl),d                        ;[0e86] 72
                    ld        h,l                           ;[0e87] 65
                    ld        (hl),e                        ;[0e88] 73
                    ld        (hl),e                        ;[0e89] 73
                    jr        nz,$0edc                      ;[0e8a] 20 50
                    jr        nz,$0f02                      ;[0e8c] 20 74
                    ld        l,a                           ;[0e8e] 6f
                    jr        nz,$0f01                      ;[0e8f] 20 70
                    ld        h,c                           ;[0e91] 61
                    ld        (hl),e                        ;[0e92] 73
                    ld        (hl),h                        ;[0e93] 74
                    ld        h,l                           ;[0e94] 65
                    rst       $38                           ;[0e95] ff
                    add       hl,de                         ;[0e96] 19
                    ex        af,af'                        ;[0e97] 08
                    nop                                     ;[0e98] 00
                    nop                                     ;[0e99] 00
                    jr        nz,$0eb5                      ;[0e9a] 20 19
                    ex        af,af'                        ;[0e9c] 08
                    ld        bc,$4f00                      ;[0e9d] 01 00 4f
                    jr        nz,$0ebb                      ;[0ea0] 20 19
                    ex        af,af'                        ;[0ea2] 08
                    rlca                                    ;[0ea3] 07
                    nop                                     ;[0ea4] 00
                    ld        (hl),d                        ;[0ea5] 72
                    ld        h,h                           ;[0ea6] 64
                    ld        h,l                           ;[0ea7] 65
                    ld        (hl),d                        ;[0ea8] 72
                    ld        a,($0819)                     ;[0ea9] 3a 19 08
                    ld        a,($2b00)                     ;[0eac] 3a 00 2b
                    dec       l                             ;[0eaf] 2d
                    add       hl,de                         ;[0eb0] 19
                    ex        af,af'                        ;[0eb1] 08
                    jp        nc,$4900                      ;[0eb2] d2 00 49
                    ld        l,(hl)                        ;[0eb5] 6e
                    ld        h,(hl)                        ;[0eb6] 66
                    ld        l,a                           ;[0eb7] 6f
                    ld        a,($0819)                     ;[0eb8] 3a 19 08
                    sbc       e                             ;[0ebb] 9b
                    nop                                     ;[0ebc] 00
                    ld        c,(hl)                        ;[0ebd] 4e
                    ld        h,c                           ;[0ebe] 61
                    ld        l,l                           ;[0ebf] 6d
                    ld        h,l                           ;[0ec0] 65
                    add       hl,de                         ;[0ec1] 19
                    ex        af,af'                        ;[0ec2] 08
                    or        e                             ;[0ec3] b3
                    nop                                     ;[0ec4] 00
                    ld        b,c                           ;[0ec5] 41
                    ld        (hl),d                        ;[0ec6] 72
                    ld        h,l                           ;[0ec7] 65
                    ld        h,c                           ;[0ec8] 61
                    add       hl,de                         ;[0ec9] 19
                    ex        af,af'                        ;[0eca] 08
                    ld        c,l                           ;[0ecb] 4d
                    nop                                     ;[0ecc] 00
                    ld        l,l                           ;[0ecd] 6d
                    ld        l,c                           ;[0ece] 69
                    ld        e,b                           ;[0ecf] 58
                    jr        nz,$0f41                      ;[0ed0] 20 6f
                    rst       $38                           ;[0ed2] ff
                    add       hl,de                         ;[0ed3] 19
                    ex        af,af'                        ;[0ed4] 08
                    halt                                    ;[0ed5] 76
                    nop                                     ;[0ed6] 00
                    ld        (hl),e                        ;[0ed7] 73
                    ld        h,l                           ;[0ed8] 65
                    ld        h,c                           ;[0ed9] 61
                    ld        (hl),d                        ;[0eda] 72
                    ld        h,e                           ;[0edb] 63
                    ld        c,b                           ;[0edc] 48
                    rst       $38                           ;[0edd] ff
                    ld        d,$16                         ;[0ede] 16 16
                    nop                                     ;[0ee0] 00
                    jr        nz,$0efa                      ;[0ee1] 20 17
                    nop                                     ;[0ee3] 00
                    nop                                     ;[0ee4] 00
                    jr        nz,$0efe                      ;[0ee5] 20 17
                    nop                                     ;[0ee7] 00
                    nop                                     ;[0ee8] 00
                    ld        d,$16                         ;[0ee9] 16 16
                    nop                                     ;[0eeb] 00
                    rst       $38                           ;[0eec] ff
                    ld        l,$2e                         ;[0eed] 2e 2e
                    rst       $38                           ;[0eef] ff
                    ld        c,(hl)                        ;[0ef0] 4e
                    ld        h,l                           ;[0ef1] 65
                    ld        (hl),a                        ;[0ef2] 77
                    jr        nz,$0f63                      ;[0ef3] 20 6e
                    ld        h,c                           ;[0ef5] 61
                    ld        l,l                           ;[0ef6] 6d
                    ld        h,l                           ;[0ef7] 65
                    ld        a,($ff20)                     ;[0ef8] 3a 20 ff
                    ld        d,b                           ;[0efb] 50
                    ld        h,c                           ;[0efc] 61
                    ld        (hl),e                        ;[0efd] 73
                    ld        (hl),h                        ;[0efe] 74
                    ld        h,l                           ;[0eff] 65
                    jr        nz,$0f63                      ;[0f00] 20 61
                    ld        (hl),e                        ;[0f02] 73
                    ld        a,($ff20)                     ;[0f03] 3a 20 ff
                    ld        h,e                           ;[0f06] 63
                    ld        l,a                           ;[0f07] 6f
                    ld        (hl),b                        ;[0f08] 70
                    ld        a,c                           ;[0f09] 79
                    ld        l,c                           ;[0f0a] 69
                    ld        l,(hl)                        ;[0f0b] 6e
                    ld        h,a                           ;[0f0c] 67
                    ld        l,$2e                         ;[0f0d] 2e 2e
                    ld        l,$ff                         ;[0f0f] 2e ff
                    ld        h,l                           ;[0f11] 65
                    ld        (hl),d                        ;[0f12] 72
                    ld        h,c                           ;[0f13] 61
                    ld        (hl),e                        ;[0f14] 73
                    ld        l,c                           ;[0f15] 69
                    ld        l,(hl)                        ;[0f16] 6e
                    ld        h,a                           ;[0f17] 67
                    ld        l,$2e                         ;[0f18] 2e 2e
                    ld        l,$ff                         ;[0f1a] 2e ff
                    ld        e,$05                         ;[0f1c] 1e 05
                    ld        d,$15                         ;[0f1e] 16 15
                    dec       h                             ;[0f20] 25
                    ld        d,e                           ;[0f21] 53
                    jr        nz,$0f44                      ;[0f22] 20 20
                    ld        a,e                           ;[0f24] 7b
                    add       hl,de                         ;[0f25] 19
                    xor       b                             ;[0f26] a8
                    pop       bc                            ;[0f27] c1
                    nop                                     ;[0f28] 00
                    ld        l,$16                         ;[0f29] 2e 16
                    dec       d                             ;[0f2b] 15
                    rrca                                    ;[0f2c] 0f
                    ld        b,(hl)                        ;[0f2d] 46
                    ld        l,c                           ;[0f2e] 69
                    ld        l,h                           ;[0f2f] 6c
                    ld        (hl),h                        ;[0f30] 74
                    ld        h,l                           ;[0f31] 65
                    ld        (hl),d                        ;[0f32] 72
                    ld        a,($55ff)                     ;[0f33] 3a ff 55
                    ld        (hl),e                        ;[0f36] 73
                    ld        h,l                           ;[0f37] 65
                    ld        (hl),d                        ;[0f38] 72
                    jr        nz,$0f9c                      ;[0f39] 20 61
                    ld        (hl),d                        ;[0f3b] 72
                    ld        h,l                           ;[0f3c] 65
                    ld        h,c                           ;[0f3d] 61
                    jr        nz,$0f68                      ;[0f3e] 20 28
                    jr        nc,$0f70                      ;[0f40] 30 2e
                    ld        l,$31                         ;[0f42] 2e 31
                    dec       (hl)                          ;[0f44] 35
                    add       hl,hl                         ;[0f45] 29
                    ld        a,($ff20)                     ;[0f46] 3a 20 ff
                    ld        e,$05                         ;[0f49] 1e 05
                    ld        d,$16                         ;[0f4b] 16 16
                    nop                                     ;[0f4d] 00
                    rst       $38                           ;[0f4e] ff
                    ld        l,(hl)                        ;[0f4f] 6e
                    ld        h,c                           ;[0f50] 61
                    ld        l,l                           ;[0f51] 6d
                    ld        h,l                           ;[0f52] 65
                    rst       $38                           ;[0f53] ff
                    ld        l,(hl)                        ;[0f54] 6e
                    ld        l,a                           ;[0f55] 6f
                    ld        l,(hl)                        ;[0f56] 6e
                    ld        h,l                           ;[0f57] 65
                    rst       $38                           ;[0f58] ff
                    ld        h,h                           ;[0f59] 64
                    ld        h,c                           ;[0f5a] 61
                    ld        (hl),h                        ;[0f5b] 74
                    ld        h,l                           ;[0f5c] 65
                    rst       $38                           ;[0f5d] ff
                    ld        (hl),e                        ;[0f5e] 73
                    ld        l,c                           ;[0f5f] 69
                    ld        a,d                           ;[0f60] 7a
                    ld        h,l                           ;[0f61] 65
                    rst       $38                           ;[0f62] ff
                    ld        h,c                           ;[0f63] 61
                    ld        (hl),h                        ;[0f64] 74
                    ld        (hl),h                        ;[0f65] 74
                    ld        (hl),d                        ;[0f66] 72
                    rst       $38                           ;[0f67] ff
                    ld        c,a                           ;[0f68] 4f
                    rrca                                    ;[0f69] 0f
                    ld        d,h                           ;[0f6a] 54
                    rrca                                    ;[0f6b] 0f
                    ld        e,c                           ;[0f6c] 59
                    rrca                                    ;[0f6d] 0f
                    ld        e,(hl)                        ;[0f6e] 5e
                    rrca                                    ;[0f6f] 0f
                    add       hl,de                         ;[0f70] 19
                    ex        af,af'                        ;[0f71] 08
                    jr        nz,$0f74                      ;[0f72] 20 00
                    rst       $38                           ;[0f74] ff
                    ld        d,h                           ;[0f75] 54
                    rrca                                    ;[0f76] 0f
                    ld        e,(hl)                        ;[0f77] 5e
                    rrca                                    ;[0f78] 0f
                    ld        e,c                           ;[0f79] 59
                    rrca                                    ;[0f7a] 0f
                    ld        h,e                           ;[0f7b] 63
                    rrca                                    ;[0f7c] 0f
                    add       hl,de                         ;[0f7d] 19
                    ex        af,af'                        ;[0f7e] 08
                    ex        de,hl                         ;[0f7f] eb
                    nop                                     ;[0f80] 00
                    rst       $38                           ;[0f81] ff
                    add       hl,de                         ;[0f82] 19
                    nop                                     ;[0f83] 00
                    ld        bc,$ff00                      ;[0f84] 01 00 ff
                    ld        d,e                           ;[0f87] 53
                    ld        h,l                           ;[0f88] 65
                    ld        h,c                           ;[0f89] 61
                    ld        (hl),d                        ;[0f8a] 72
                    ld        h,e                           ;[0f8b] 63
                    ld        l,b                           ;[0f8c] 68
                    ld        a,($ff0d)                     ;[0f8d] 3a 0d ff
                    ld        b,l                           ;[0f90] 45
                    ld        e,b                           ;[0f91] 58
                    ld        d,h                           ;[0f92] 54
                    ld        b,l                           ;[0f93] 45
                    ld        c,(hl)                        ;[0f94] 4e
                    ld        b,h                           ;[0f95] 44
                    dec       a                             ;[0f96] 3d
                    ld        h,(hl)                        ;[0f97] 66
                    ld        l,c                           ;[0f98] 69
                    ld        l,h                           ;[0f99] 6c
                    ld        h,l                           ;[0f9a] 65
                    ld        (hl),e                        ;[0f9b] 73
                    cpl                                     ;[0f9c] 2f
                    ld        h,h                           ;[0f9d] 64
                    ld        l,c                           ;[0f9e] 69
                    ld        (hl),d                        ;[0f9f] 72
                    ld        (hl),e                        ;[0fa0] 73
                    rst       $38                           ;[0fa1] ff
                    ld        d,$02                         ;[0fa2] 16 02
                    nop                                     ;[0fa4] 00
                    ld        (de),a                        ;[0fa5] 12
                    ld        bc,$5020                      ;[0fa6] 01 20 50
                    ld        (hl),d                        ;[0fa9] 72
                    ld        l,a                           ;[0faa] 6f
                    ld        h,e                           ;[0fab] 63
                    ld        h,l                           ;[0fac] 65
                    ld        (hl),e                        ;[0fad] 73
                    ld        (hl),e                        ;[0fae] 73
                    ld        l,c                           ;[0faf] 69
                    ld        l,(hl)                        ;[0fb0] 6e
                    ld        h,a                           ;[0fb1] 67
                    ld        l,$2e                         ;[0fb2] 2e 2e
                    ld        l,$20                         ;[0fb4] 2e 20
                    ld        (de),a                        ;[0fb6] 12
                    nop                                     ;[0fb7] 00
                    jr        nz,$0fb9                      ;[0fb8] 20 ff
                    ld        d,b                           ;[0fba] 50
                    ld        (hl),d                        ;[0fbb] 72
                    ld        h,l                           ;[0fbc] 65
                    ld        (hl),e                        ;[0fbd] 73
                    ld        (hl),e                        ;[0fbe] 73
                    jr        nz,$1022                      ;[0fbf] 20 61
                    ld        l,(hl)                        ;[0fc1] 6e
                    ld        a,c                           ;[0fc2] 79
                    jr        nz,$1030                      ;[0fc3] 20 6b
                    ld        h,l                           ;[0fc5] 65
                    ld        a,c                           ;[0fc6] 79
                    rst       $38                           ;[0fc7] ff
                    ld        l,$67                         ;[0fc8] 2e 67
                    ld        h,h                           ;[0fca] 64
                    ld        h,l                           ;[0fcb] 65
                    rst       $38                           ;[0fcc] ff
                    ld        h,e                           ;[0fcd] 63
                    ld        a,($642f)                     ;[0fce] 3a 2f 64
                    ld        l,a                           ;[0fd1] 6f
                    ld        h,e                           ;[0fd2] 63
                    ld        (hl),e                        ;[0fd3] 73
                    cpl                                     ;[0fd4] 2f
                    ld        h,a                           ;[0fd5] 67
                    ld        (hl),l                        ;[0fd6] 75
                    ld        l,c                           ;[0fd7] 69
                    ld        h,h                           ;[0fd8] 64
                    ld        h,l                           ;[0fd9] 65
                    ld        (hl),e                        ;[0fda] 73
                    cpl                                     ;[0fdb] 2f
                    ld        h,e                           ;[0fdc] 63
                    ld        a,($63ff)                     ;[0fdd] 3a ff 63
                    ld        a,($6c2f)                     ;[0fe0] 3a 2f 6c
                    ld        l,c                           ;[0fe3] 69
                    ld        l,(hl)                        ;[0fe4] 6e
                    ld        l,e                           ;[0fe5] 6b
                    ld        (hl),e                        ;[0fe6] 73
                    rst       $38                           ;[0fe7] ff
                    ld        c,(hl)                        ;[0fe8] 4e
                    ld        h,l                           ;[0fe9] 65
                    ld        a,b                           ;[0fea] 78
                    ld        (hl),h                        ;[0feb] 74
                    ld        c,h                           ;[0fec] 4c
                    ld        l,c                           ;[0fed] 69
                    ld        l,(hl)                        ;[0fee] 6e
                    ld        l,e                           ;[0fef] 6b
                    ld        d,d                           ;[0ff0] 52
                    ld        d,l                           ;[0ff1] 55
                    ld        c,(hl)                        ;[0ff2] 4e
                    ld        c,h                           ;[0ff3] 4c
                    ld        c,(hl)                        ;[0ff4] 4e
                    ld        c,e                           ;[0ff5] 4b
                    ld        b,d                           ;[0ff6] 42
                    ld        b,c                           ;[0ff7] 41
                    ld        d,e                           ;[0ff8] 53
                    ld        d,e                           ;[0ff9] 53
                    ld        b,e                           ;[0ffa] 43
                    ld        d,d                           ;[0ffb] 52
                    push    $101f                           ;[0ffc] ed 8a 10 1f
                    push      de                            ;[1000] d5
                    push      bc                            ;[1001] c5
                    ld        b,(hl)                        ;[1002] 46
                    inc       b                             ;[1003] 04
                    jr        z,$1011                       ;[1004] 28 0b
                    dec       b                             ;[1006] 05
                    inc       hl                            ;[1007] 23
                    ld        e,(hl)                        ;[1008] 5e
                    inc       hl                            ;[1009] 23
                    ld        d,(hl)                        ;[100a] 56
                    inc       hl                            ;[100b] 23
                    cp        b                             ;[100c] b8
                    jr        z,$1017                       ;[100d] 28 08
                    jr        $1002                         ;[100f] 18 f1
                    and       a                             ;[1011] a7
                    dec       b                             ;[1012] 05
                    pop       bc                            ;[1013] c1
                    pop       de                            ;[1014] d1
                    pop       hl                            ;[1015] e1
                    ret                                     ;[1016] c9

                    pop       bc                            ;[1017] c1
                    ex        de,hl                         ;[1018] eb
                    ex        (sp),hl                       ;[1019] e3
                    ex        de,hl                         ;[101a] eb
                    ret                                     ;[101b] c9

                    call      $1023                         ;[101c] cd 23 10
                    ld        h,$01                         ;[101f] 26 01
                    dec       h                             ;[1021] 25
                    ret                                     ;[1022] c9

                    jp        (hl)                          ;[1023] e9
                    nextreg $8e,$08                         ;[1024] ed 91 8e 08
                    ex        af,af'                        ;[1028] 08
                    pop       af                            ;[1029] f1
                    ld        ($5b52),hl                    ;[102a] 22 52 5b
                    ld        hl,($5b6a)                    ;[102d] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[1030] ed 73 6a 5b
                    ld        sp,hl                         ;[1034] f9
                    ld        hl,($5b52)                    ;[1035] 2a 52 5b
                    push      af                            ;[1038] f5
                    ex        af,af'                        ;[1039] 08
                    ret                                     ;[103a] c9

                    ex        af,af'                        ;[103b] 08
                    pop       af                            ;[103c] f1
                    ld        ($5b52),hl                    ;[103d] 22 52 5b
                    ld        hl,($5b6a)                    ;[1040] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[1043] ed 73 6a 5b
                    ld        sp,hl                         ;[1047] f9
                    ld        hl,($5b52)                    ;[1048] 2a 52 5b
                    push      af                            ;[104b] f5
                    ex        af,af'                        ;[104c] 08
                    nextreg $8e,$78                         ;[104d] ed 91 8e 78
                    ret                                     ;[1051] c9

                    nop                                     ;[1052] 00
                    nop                                     ;[1053] 00
                    nop                                     ;[1054] 00
                    nop                                     ;[1055] 00
                    nop                                     ;[1056] 00
                    nop                                     ;[1057] 00
                    nop                                     ;[1058] 00
                    nop                                     ;[1059] 00
                    nop                                     ;[105a] 00
                    nop                                     ;[105b] 00
                    nop                                     ;[105c] 00
                    nop                                     ;[105d] 00
                    nop                                     ;[105e] 00
                    nop                                     ;[105f] 00
                    nop                                     ;[1060] 00
                    nop                                     ;[1061] 00
                    nop                                     ;[1062] 00
                    nop                                     ;[1063] 00
                    nop                                     ;[1064] 00
                    nop                                     ;[1065] 00
                    nop                                     ;[1066] 00
                    nop                                     ;[1067] 00
                    nop                                     ;[1068] 00
                    nop                                     ;[1069] 00
                    nop                                     ;[106a] 00
                    nop                                     ;[106b] 00
                    nop                                     ;[106c] 00
                    nop                                     ;[106d] 00
                    nop                                     ;[106e] 00
                    nop                                     ;[106f] 00
                    nop                                     ;[1070] 00
                    nop                                     ;[1071] 00
                    nop                                     ;[1072] 00
                    nop                                     ;[1073] 00
                    nop                                     ;[1074] 00
                    nop                                     ;[1075] 00
                    nop                                     ;[1076] 00
                    nop                                     ;[1077] 00
                    nop                                     ;[1078] 00
                    nop                                     ;[1079] 00
                    nop                                     ;[107a] 00
                    nop                                     ;[107b] 00
                    nop                                     ;[107c] 00
                    nop                                     ;[107d] 00
                    nop                                     ;[107e] 00
                    nop                                     ;[107f] 00
                    nop                                     ;[1080] 00
                    nop                                     ;[1081] 00
                    nop                                     ;[1082] 00
                    nop                                     ;[1083] 00
                    nop                                     ;[1084] 00
                    nop                                     ;[1085] 00
                    nop                                     ;[1086] 00
                    nop                                     ;[1087] 00
                    nop                                     ;[1088] 00
                    nop                                     ;[1089] 00
                    nop                                     ;[108a] 00
                    nop                                     ;[108b] 00
                    nop                                     ;[108c] 00
                    nop                                     ;[108d] 00
                    nop                                     ;[108e] 00
                    nop                                     ;[108f] 00
                    nop                                     ;[1090] 00
                    nop                                     ;[1091] 00
                    nop                                     ;[1092] 00
                    nop                                     ;[1093] 00
                    nop                                     ;[1094] 00
                    nop                                     ;[1095] 00
                    nop                                     ;[1096] 00
                    nop                                     ;[1097] 00
                    nop                                     ;[1098] 00
                    nop                                     ;[1099] 00
                    nop                                     ;[109a] 00
                    nop                                     ;[109b] 00
                    nop                                     ;[109c] 00
                    nop                                     ;[109d] 00
                    nop                                     ;[109e] 00
                    nop                                     ;[109f] 00
                    nop                                     ;[10a0] 00
                    nop                                     ;[10a1] 00
                    nop                                     ;[10a2] 00
                    nop                                     ;[10a3] 00
                    nop                                     ;[10a4] 00
                    nop                                     ;[10a5] 00
                    nop                                     ;[10a6] 00
                    nop                                     ;[10a7] 00
                    nop                                     ;[10a8] 00
                    nop                                     ;[10a9] 00
                    nop                                     ;[10aa] 00
                    nop                                     ;[10ab] 00
                    nop                                     ;[10ac] 00
                    nop                                     ;[10ad] 00
                    nop                                     ;[10ae] 00
                    nop                                     ;[10af] 00
                    nop                                     ;[10b0] 00
                    nop                                     ;[10b1] 00
                    nop                                     ;[10b2] 00
                    nop                                     ;[10b3] 00
                    nop                                     ;[10b4] 00
                    nop                                     ;[10b5] 00
                    nop                                     ;[10b6] 00
                    nop                                     ;[10b7] 00
                    nop                                     ;[10b8] 00
                    nop                                     ;[10b9] 00
                    nop                                     ;[10ba] 00
                    nop                                     ;[10bb] 00
                    nop                                     ;[10bc] 00
                    nop                                     ;[10bd] 00
                    nop                                     ;[10be] 00
                    nop                                     ;[10bf] 00
                    nop                                     ;[10c0] 00
                    nop                                     ;[10c1] 00
                    nop                                     ;[10c2] 00
                    nop                                     ;[10c3] 00
                    nop                                     ;[10c4] 00
                    nop                                     ;[10c5] 00
                    nop                                     ;[10c6] 00
                    nop                                     ;[10c7] 00
                    nop                                     ;[10c8] 00
                    nop                                     ;[10c9] 00
                    nop                                     ;[10ca] 00
                    nop                                     ;[10cb] 00
                    nop                                     ;[10cc] 00
                    nop                                     ;[10cd] 00
                    nop                                     ;[10ce] 00
                    nop                                     ;[10cf] 00
                    nop                                     ;[10d0] 00
                    nop                                     ;[10d1] 00
                    nop                                     ;[10d2] 00
                    nop                                     ;[10d3] 00
                    nop                                     ;[10d4] 00
                    nop                                     ;[10d5] 00
                    nop                                     ;[10d6] 00
                    nop                                     ;[10d7] 00
                    nop                                     ;[10d8] 00
                    nop                                     ;[10d9] 00
                    nop                                     ;[10da] 00
                    nop                                     ;[10db] 00
                    nop                                     ;[10dc] 00
                    nop                                     ;[10dd] 00
                    nop                                     ;[10de] 00
                    nop                                     ;[10df] 00
                    nop                                     ;[10e0] 00
                    nop                                     ;[10e1] 00
                    nop                                     ;[10e2] 00
                    nop                                     ;[10e3] 00
                    nop                                     ;[10e4] 00
                    nop                                     ;[10e5] 00
                    nop                                     ;[10e6] 00
                    nop                                     ;[10e7] 00
                    nop                                     ;[10e8] 00
                    nop                                     ;[10e9] 00
                    nop                                     ;[10ea] 00
                    nop                                     ;[10eb] 00
                    nop                                     ;[10ec] 00
                    nop                                     ;[10ed] 00
                    nop                                     ;[10ee] 00
                    nop                                     ;[10ef] 00
                    nop                                     ;[10f0] 00
                    nop                                     ;[10f1] 00
                    nop                                     ;[10f2] 00
                    nop                                     ;[10f3] 00
                    nop                                     ;[10f4] 00
                    nop                                     ;[10f5] 00
                    nop                                     ;[10f6] 00
                    nop                                     ;[10f7] 00
                    nop                                     ;[10f8] 00
                    nop                                     ;[10f9] 00
                    nop                                     ;[10fa] 00
                    nop                                     ;[10fb] 00
                    nop                                     ;[10fc] 00
                    nop                                     ;[10fd] 00
                    nop                                     ;[10fe] 00
                    nop                                     ;[10ff] 00
                    nop                                     ;[1100] 00
                    nop                                     ;[1101] 00
                    nop                                     ;[1102] 00
                    nop                                     ;[1103] 00
                    nop                                     ;[1104] 00
                    nop                                     ;[1105] 00
                    nop                                     ;[1106] 00
                    nop                                     ;[1107] 00
                    nop                                     ;[1108] 00
                    nop                                     ;[1109] 00
                    nop                                     ;[110a] 00
                    nop                                     ;[110b] 00
                    nop                                     ;[110c] 00
                    nop                                     ;[110d] 00
                    nop                                     ;[110e] 00
                    nop                                     ;[110f] 00
                    nop                                     ;[1110] 00
                    nop                                     ;[1111] 00
                    nop                                     ;[1112] 00
                    nop                                     ;[1113] 00
                    nop                                     ;[1114] 00
                    nop                                     ;[1115] 00
                    nop                                     ;[1116] 00
                    nop                                     ;[1117] 00
                    nop                                     ;[1118] 00
                    nop                                     ;[1119] 00
                    nop                                     ;[111a] 00
                    nop                                     ;[111b] 00
                    nop                                     ;[111c] 00
                    nop                                     ;[111d] 00
                    nop                                     ;[111e] 00
                    nop                                     ;[111f] 00
                    nop                                     ;[1120] 00
                    nop                                     ;[1121] 00
                    nop                                     ;[1122] 00
                    nop                                     ;[1123] 00
                    nop                                     ;[1124] 00
                    nop                                     ;[1125] 00
                    nop                                     ;[1126] 00
                    nop                                     ;[1127] 00
                    nop                                     ;[1128] 00
                    nop                                     ;[1129] 00
                    nop                                     ;[112a] 00
                    nop                                     ;[112b] 00
                    nop                                     ;[112c] 00
                    nop                                     ;[112d] 00
                    nop                                     ;[112e] 00
                    nop                                     ;[112f] 00
                    nop                                     ;[1130] 00
                    nop                                     ;[1131] 00
                    nop                                     ;[1132] 00
                    nop                                     ;[1133] 00
                    nop                                     ;[1134] 00
                    nop                                     ;[1135] 00
                    nop                                     ;[1136] 00
                    nop                                     ;[1137] 00
                    nop                                     ;[1138] 00
                    nop                                     ;[1139] 00
                    nop                                     ;[113a] 00
                    nop                                     ;[113b] 00
                    nop                                     ;[113c] 00
                    nop                                     ;[113d] 00
                    nop                                     ;[113e] 00
                    nop                                     ;[113f] 00
                    nop                                     ;[1140] 00
                    nop                                     ;[1141] 00
                    nop                                     ;[1142] 00
                    nop                                     ;[1143] 00
                    nop                                     ;[1144] 00
                    nop                                     ;[1145] 00
                    nop                                     ;[1146] 00
                    nop                                     ;[1147] 00
                    nop                                     ;[1148] 00
                    nop                                     ;[1149] 00
                    nop                                     ;[114a] 00
                    nop                                     ;[114b] 00
                    nop                                     ;[114c] 00
                    nop                                     ;[114d] 00
                    nop                                     ;[114e] 00
                    nop                                     ;[114f] 00
                    nop                                     ;[1150] 00
                    nop                                     ;[1151] 00
                    nop                                     ;[1152] 00
                    nop                                     ;[1153] 00
                    nop                                     ;[1154] 00
                    nop                                     ;[1155] 00
                    nop                                     ;[1156] 00
                    nop                                     ;[1157] 00
                    nop                                     ;[1158] 00
                    nop                                     ;[1159] 00
                    nop                                     ;[115a] 00
                    nop                                     ;[115b] 00
                    nop                                     ;[115c] 00
                    nop                                     ;[115d] 00
                    nop                                     ;[115e] 00
                    nop                                     ;[115f] 00
                    nop                                     ;[1160] 00
                    nop                                     ;[1161] 00
                    nop                                     ;[1162] 00
                    nop                                     ;[1163] 00
                    nop                                     ;[1164] 00
                    nop                                     ;[1165] 00
                    nop                                     ;[1166] 00
                    nop                                     ;[1167] 00
                    nop                                     ;[1168] 00
                    nop                                     ;[1169] 00
                    nop                                     ;[116a] 00
                    nop                                     ;[116b] 00
                    nop                                     ;[116c] 00
                    nop                                     ;[116d] 00
                    nop                                     ;[116e] 00
                    nop                                     ;[116f] 00
                    nop                                     ;[1170] 00
                    nop                                     ;[1171] 00
                    nop                                     ;[1172] 00
                    nop                                     ;[1173] 00
                    nop                                     ;[1174] 00
                    nop                                     ;[1175] 00
                    nop                                     ;[1176] 00
                    nop                                     ;[1177] 00
                    nop                                     ;[1178] 00
                    nop                                     ;[1179] 00
                    nop                                     ;[117a] 00
                    nop                                     ;[117b] 00
                    nop                                     ;[117c] 00
                    nop                                     ;[117d] 00
                    nop                                     ;[117e] 00
                    nop                                     ;[117f] 00
                    nop                                     ;[1180] 00
                    nop                                     ;[1181] 00
                    nop                                     ;[1182] 00
                    nop                                     ;[1183] 00
                    nop                                     ;[1184] 00
                    nop                                     ;[1185] 00
                    nop                                     ;[1186] 00
                    nop                                     ;[1187] 00
                    nop                                     ;[1188] 00
                    nop                                     ;[1189] 00
                    nop                                     ;[118a] 00
                    nop                                     ;[118b] 00
                    nop                                     ;[118c] 00
                    nop                                     ;[118d] 00
                    nop                                     ;[118e] 00
                    nop                                     ;[118f] 00
                    nop                                     ;[1190] 00
                    nop                                     ;[1191] 00
                    nop                                     ;[1192] 00
                    nop                                     ;[1193] 00
                    nop                                     ;[1194] 00
                    nop                                     ;[1195] 00
                    nop                                     ;[1196] 00
                    nop                                     ;[1197] 00
                    nop                                     ;[1198] 00
                    nop                                     ;[1199] 00
                    nop                                     ;[119a] 00
                    nop                                     ;[119b] 00
                    nop                                     ;[119c] 00
                    nop                                     ;[119d] 00
                    nop                                     ;[119e] 00
                    nop                                     ;[119f] 00
                    nop                                     ;[11a0] 00
                    nop                                     ;[11a1] 00
                    nop                                     ;[11a2] 00
                    nop                                     ;[11a3] 00
                    nop                                     ;[11a4] 00
                    nop                                     ;[11a5] 00
                    nop                                     ;[11a6] 00
                    nop                                     ;[11a7] 00
                    nop                                     ;[11a8] 00
                    nop                                     ;[11a9] 00
                    nop                                     ;[11aa] 00
                    nop                                     ;[11ab] 00
                    nop                                     ;[11ac] 00
                    nop                                     ;[11ad] 00
                    nop                                     ;[11ae] 00
                    nop                                     ;[11af] 00
                    nop                                     ;[11b0] 00
                    nop                                     ;[11b1] 00
                    nop                                     ;[11b2] 00
                    nop                                     ;[11b3] 00
                    nop                                     ;[11b4] 00
                    nop                                     ;[11b5] 00
                    nop                                     ;[11b6] 00
                    nop                                     ;[11b7] 00
                    nop                                     ;[11b8] 00
                    nop                                     ;[11b9] 00
                    nop                                     ;[11ba] 00
                    nop                                     ;[11bb] 00
                    nop                                     ;[11bc] 00
                    nop                                     ;[11bd] 00
                    nop                                     ;[11be] 00
                    nop                                     ;[11bf] 00
                    nop                                     ;[11c0] 00
                    nop                                     ;[11c1] 00
                    nop                                     ;[11c2] 00
                    nop                                     ;[11c3] 00
                    nop                                     ;[11c4] 00
                    nop                                     ;[11c5] 00
                    nop                                     ;[11c6] 00
                    nop                                     ;[11c7] 00
                    nop                                     ;[11c8] 00
                    nop                                     ;[11c9] 00
                    nop                                     ;[11ca] 00
                    nop                                     ;[11cb] 00
                    nop                                     ;[11cc] 00
                    nop                                     ;[11cd] 00
                    nop                                     ;[11ce] 00
                    nop                                     ;[11cf] 00
                    nop                                     ;[11d0] 00
                    nop                                     ;[11d1] 00
                    nop                                     ;[11d2] 00
                    nop                                     ;[11d3] 00
                    nop                                     ;[11d4] 00
                    nop                                     ;[11d5] 00
                    nop                                     ;[11d6] 00
                    nop                                     ;[11d7] 00
                    nop                                     ;[11d8] 00
                    nop                                     ;[11d9] 00
                    nop                                     ;[11da] 00
                    nop                                     ;[11db] 00
                    nop                                     ;[11dc] 00
                    nop                                     ;[11dd] 00
                    nop                                     ;[11de] 00
                    nop                                     ;[11df] 00
                    nop                                     ;[11e0] 00
                    nop                                     ;[11e1] 00
                    nop                                     ;[11e2] 00
                    nop                                     ;[11e3] 00
                    nop                                     ;[11e4] 00
                    nop                                     ;[11e5] 00
                    nop                                     ;[11e6] 00
                    nop                                     ;[11e7] 00
                    nop                                     ;[11e8] 00
                    nop                                     ;[11e9] 00
                    nop                                     ;[11ea] 00
                    nop                                     ;[11eb] 00
                    nop                                     ;[11ec] 00
                    nop                                     ;[11ed] 00
                    nop                                     ;[11ee] 00
                    nop                                     ;[11ef] 00
                    nop                                     ;[11f0] 00
                    nop                                     ;[11f1] 00
                    nop                                     ;[11f2] 00
                    nop                                     ;[11f3] 00
                    nop                                     ;[11f4] 00
                    nop                                     ;[11f5] 00
                    nop                                     ;[11f6] 00
                    nop                                     ;[11f7] 00
                    nop                                     ;[11f8] 00
                    nop                                     ;[11f9] 00
                    nop                                     ;[11fa] 00
                    nop                                     ;[11fb] 00
                    nop                                     ;[11fc] 00
                    nop                                     ;[11fd] 00
                    nop                                     ;[11fe] 00
                    nop                                     ;[11ff] 00
                    nop                                     ;[1200] 00
                    nop                                     ;[1201] 00
                    nop                                     ;[1202] 00
                    nop                                     ;[1203] 00
                    nop                                     ;[1204] 00
                    nop                                     ;[1205] 00
                    nop                                     ;[1206] 00
                    nop                                     ;[1207] 00
                    nop                                     ;[1208] 00
                    nop                                     ;[1209] 00
                    nop                                     ;[120a] 00
                    nop                                     ;[120b] 00
                    nop                                     ;[120c] 00
                    nop                                     ;[120d] 00
                    nop                                     ;[120e] 00
                    nop                                     ;[120f] 00
                    nop                                     ;[1210] 00
                    nop                                     ;[1211] 00
                    nop                                     ;[1212] 00
                    nop                                     ;[1213] 00
                    nop                                     ;[1214] 00
                    nop                                     ;[1215] 00
                    nop                                     ;[1216] 00
                    nop                                     ;[1217] 00
                    nop                                     ;[1218] 00
                    nop                                     ;[1219] 00
                    nop                                     ;[121a] 00
                    nop                                     ;[121b] 00
                    nop                                     ;[121c] 00
                    nop                                     ;[121d] 00
                    nop                                     ;[121e] 00
                    nop                                     ;[121f] 00
                    nop                                     ;[1220] 00
                    nop                                     ;[1221] 00
                    nop                                     ;[1222] 00
                    nop                                     ;[1223] 00
                    nop                                     ;[1224] 00
                    nop                                     ;[1225] 00
                    nop                                     ;[1226] 00
                    nop                                     ;[1227] 00
                    nop                                     ;[1228] 00
                    nop                                     ;[1229] 00
                    nop                                     ;[122a] 00
                    nop                                     ;[122b] 00
                    nop                                     ;[122c] 00
                    nop                                     ;[122d] 00
                    nop                                     ;[122e] 00
                    nop                                     ;[122f] 00
                    nop                                     ;[1230] 00
                    nop                                     ;[1231] 00
                    nop                                     ;[1232] 00
                    nop                                     ;[1233] 00
                    nop                                     ;[1234] 00
                    nop                                     ;[1235] 00
                    nop                                     ;[1236] 00
                    nop                                     ;[1237] 00
                    nop                                     ;[1238] 00
                    nop                                     ;[1239] 00
                    nop                                     ;[123a] 00
                    nop                                     ;[123b] 00
                    nop                                     ;[123c] 00
                    nop                                     ;[123d] 00
                    nop                                     ;[123e] 00
                    nop                                     ;[123f] 00
                    nop                                     ;[1240] 00
                    nop                                     ;[1241] 00
                    nop                                     ;[1242] 00
                    nop                                     ;[1243] 00
                    nop                                     ;[1244] 00
                    nop                                     ;[1245] 00
                    nop                                     ;[1246] 00
                    nop                                     ;[1247] 00
                    nop                                     ;[1248] 00
                    nop                                     ;[1249] 00
                    nop                                     ;[124a] 00
                    nop                                     ;[124b] 00
                    nop                                     ;[124c] 00
                    nop                                     ;[124d] 00
                    nop                                     ;[124e] 00
                    nop                                     ;[124f] 00
                    nop                                     ;[1250] 00
                    nop                                     ;[1251] 00
                    nop                                     ;[1252] 00
                    nop                                     ;[1253] 00
                    nop                                     ;[1254] 00
                    nop                                     ;[1255] 00
                    nop                                     ;[1256] 00
                    nop                                     ;[1257] 00
                    nop                                     ;[1258] 00
                    nop                                     ;[1259] 00
                    nop                                     ;[125a] 00
                    nop                                     ;[125b] 00
                    nop                                     ;[125c] 00
                    nop                                     ;[125d] 00
                    nop                                     ;[125e] 00
                    nop                                     ;[125f] 00
                    nop                                     ;[1260] 00
                    nop                                     ;[1261] 00
                    nop                                     ;[1262] 00
                    nop                                     ;[1263] 00
                    nop                                     ;[1264] 00
                    nop                                     ;[1265] 00
                    nop                                     ;[1266] 00
                    nop                                     ;[1267] 00
                    nop                                     ;[1268] 00
                    nop                                     ;[1269] 00
                    nop                                     ;[126a] 00
                    nop                                     ;[126b] 00
                    nop                                     ;[126c] 00
                    nop                                     ;[126d] 00
                    nop                                     ;[126e] 00
                    nop                                     ;[126f] 00
                    nop                                     ;[1270] 00
                    nop                                     ;[1271] 00
                    nop                                     ;[1272] 00
                    nop                                     ;[1273] 00
                    nop                                     ;[1274] 00
                    nop                                     ;[1275] 00
                    nop                                     ;[1276] 00
                    nop                                     ;[1277] 00
                    nop                                     ;[1278] 00
                    nop                                     ;[1279] 00
                    nop                                     ;[127a] 00
                    nop                                     ;[127b] 00
                    nop                                     ;[127c] 00
                    nop                                     ;[127d] 00
                    nop                                     ;[127e] 00
                    nop                                     ;[127f] 00
                    nop                                     ;[1280] 00
                    nop                                     ;[1281] 00
                    nop                                     ;[1282] 00
                    nop                                     ;[1283] 00
                    nop                                     ;[1284] 00
                    nop                                     ;[1285] 00
                    nop                                     ;[1286] 00
                    nop                                     ;[1287] 00
                    nop                                     ;[1288] 00
                    nop                                     ;[1289] 00
                    nop                                     ;[128a] 00
                    nop                                     ;[128b] 00
                    nop                                     ;[128c] 00
                    nop                                     ;[128d] 00
                    nop                                     ;[128e] 00
                    nop                                     ;[128f] 00
                    nop                                     ;[1290] 00
                    nop                                     ;[1291] 00
                    nop                                     ;[1292] 00
                    nop                                     ;[1293] 00
                    nop                                     ;[1294] 00
                    nop                                     ;[1295] 00
                    nop                                     ;[1296] 00
                    nop                                     ;[1297] 00
                    nop                                     ;[1298] 00
                    nop                                     ;[1299] 00
                    nop                                     ;[129a] 00
                    nop                                     ;[129b] 00
                    nop                                     ;[129c] 00
                    nop                                     ;[129d] 00
                    nop                                     ;[129e] 00
                    nop                                     ;[129f] 00
                    nop                                     ;[12a0] 00
                    nop                                     ;[12a1] 00
                    nop                                     ;[12a2] 00
                    nop                                     ;[12a3] 00
                    nop                                     ;[12a4] 00
                    nop                                     ;[12a5] 00
                    nop                                     ;[12a6] 00
                    nop                                     ;[12a7] 00
                    nop                                     ;[12a8] 00
                    nop                                     ;[12a9] 00
                    nop                                     ;[12aa] 00
                    nop                                     ;[12ab] 00
                    nop                                     ;[12ac] 00
                    nop                                     ;[12ad] 00
                    nop                                     ;[12ae] 00
                    nop                                     ;[12af] 00
                    nop                                     ;[12b0] 00
                    nop                                     ;[12b1] 00
                    nop                                     ;[12b2] 00
                    nop                                     ;[12b3] 00
                    nop                                     ;[12b4] 00
                    nop                                     ;[12b5] 00
                    nop                                     ;[12b6] 00
                    nop                                     ;[12b7] 00
                    nop                                     ;[12b8] 00
                    nop                                     ;[12b9] 00
                    nop                                     ;[12ba] 00
                    nop                                     ;[12bb] 00
                    nop                                     ;[12bc] 00
                    nop                                     ;[12bd] 00
                    nop                                     ;[12be] 00
                    nop                                     ;[12bf] 00
                    nop                                     ;[12c0] 00
                    nop                                     ;[12c1] 00
                    nop                                     ;[12c2] 00
                    nop                                     ;[12c3] 00
                    nop                                     ;[12c4] 00
                    nop                                     ;[12c5] 00
                    nop                                     ;[12c6] 00
                    nop                                     ;[12c7] 00
                    nop                                     ;[12c8] 00
                    nop                                     ;[12c9] 00
                    nop                                     ;[12ca] 00
                    nop                                     ;[12cb] 00
                    nop                                     ;[12cc] 00
                    nop                                     ;[12cd] 00
                    nop                                     ;[12ce] 00
                    nop                                     ;[12cf] 00
                    nop                                     ;[12d0] 00
                    nop                                     ;[12d1] 00
                    nop                                     ;[12d2] 00
                    nop                                     ;[12d3] 00
                    nop                                     ;[12d4] 00
                    nop                                     ;[12d5] 00
                    nop                                     ;[12d6] 00
                    nop                                     ;[12d7] 00
                    nop                                     ;[12d8] 00
                    nop                                     ;[12d9] 00
                    nop                                     ;[12da] 00
                    nop                                     ;[12db] 00
                    nop                                     ;[12dc] 00
                    nop                                     ;[12dd] 00
                    nop                                     ;[12de] 00
                    nop                                     ;[12df] 00
                    nop                                     ;[12e0] 00
                    nop                                     ;[12e1] 00
                    nop                                     ;[12e2] 00
                    nop                                     ;[12e3] 00
                    nop                                     ;[12e4] 00
                    nop                                     ;[12e5] 00
                    nop                                     ;[12e6] 00
                    nop                                     ;[12e7] 00
                    nop                                     ;[12e8] 00
                    nop                                     ;[12e9] 00
                    nop                                     ;[12ea] 00
                    nop                                     ;[12eb] 00
                    nop                                     ;[12ec] 00
                    nop                                     ;[12ed] 00
                    nop                                     ;[12ee] 00
                    nop                                     ;[12ef] 00
                    nop                                     ;[12f0] 00
                    nop                                     ;[12f1] 00
                    nop                                     ;[12f2] 00
                    nop                                     ;[12f3] 00
                    nop                                     ;[12f4] 00
                    nop                                     ;[12f5] 00
                    nop                                     ;[12f6] 00
                    nop                                     ;[12f7] 00
                    nop                                     ;[12f8] 00
                    nop                                     ;[12f9] 00
                    nop                                     ;[12fa] 00
                    nop                                     ;[12fb] 00
                    nop                                     ;[12fc] 00
                    nop                                     ;[12fd] 00
                    nop                                     ;[12fe] 00
                    nop                                     ;[12ff] 00
                    nop                                     ;[1300] 00
                    nop                                     ;[1301] 00
                    nop                                     ;[1302] 00
                    nop                                     ;[1303] 00
                    nop                                     ;[1304] 00
                    nop                                     ;[1305] 00
                    nop                                     ;[1306] 00
                    nop                                     ;[1307] 00
                    nop                                     ;[1308] 00
                    nop                                     ;[1309] 00
                    nop                                     ;[130a] 00
                    nop                                     ;[130b] 00
                    nop                                     ;[130c] 00
                    nop                                     ;[130d] 00
                    nop                                     ;[130e] 00
                    nop                                     ;[130f] 00
                    nop                                     ;[1310] 00
                    nop                                     ;[1311] 00
                    nop                                     ;[1312] 00
                    nop                                     ;[1313] 00
                    nop                                     ;[1314] 00
                    nop                                     ;[1315] 00
                    nop                                     ;[1316] 00
                    nop                                     ;[1317] 00
                    nop                                     ;[1318] 00
                    nop                                     ;[1319] 00
                    nop                                     ;[131a] 00
                    nop                                     ;[131b] 00
                    nop                                     ;[131c] 00
                    nop                                     ;[131d] 00
                    nop                                     ;[131e] 00
                    nop                                     ;[131f] 00
                    nop                                     ;[1320] 00
                    nop                                     ;[1321] 00
                    nop                                     ;[1322] 00
                    nop                                     ;[1323] 00
                    nop                                     ;[1324] 00
                    nop                                     ;[1325] 00
                    nop                                     ;[1326] 00
                    nop                                     ;[1327] 00
                    nop                                     ;[1328] 00
                    nop                                     ;[1329] 00
                    nop                                     ;[132a] 00
                    nop                                     ;[132b] 00
                    nop                                     ;[132c] 00
                    nop                                     ;[132d] 00
                    nop                                     ;[132e] 00
                    nop                                     ;[132f] 00
                    nop                                     ;[1330] 00
                    nop                                     ;[1331] 00
                    nop                                     ;[1332] 00
                    nop                                     ;[1333] 00
                    nop                                     ;[1334] 00
                    nop                                     ;[1335] 00
                    nop                                     ;[1336] 00
                    nop                                     ;[1337] 00
                    nop                                     ;[1338] 00
                    nop                                     ;[1339] 00
                    nop                                     ;[133a] 00
                    nop                                     ;[133b] 00
                    nop                                     ;[133c] 00
                    nop                                     ;[133d] 00
                    nop                                     ;[133e] 00
                    nop                                     ;[133f] 00
                    nop                                     ;[1340] 00
                    nop                                     ;[1341] 00
                    nop                                     ;[1342] 00
                    nop                                     ;[1343] 00
                    nop                                     ;[1344] 00
                    nop                                     ;[1345] 00
                    nop                                     ;[1346] 00
                    nop                                     ;[1347] 00
                    nop                                     ;[1348] 00
                    nop                                     ;[1349] 00
                    nop                                     ;[134a] 00
                    nop                                     ;[134b] 00
                    nop                                     ;[134c] 00
                    nop                                     ;[134d] 00
                    nop                                     ;[134e] 00
                    nop                                     ;[134f] 00
                    nop                                     ;[1350] 00
                    nop                                     ;[1351] 00
                    nop                                     ;[1352] 00
                    nop                                     ;[1353] 00
                    nop                                     ;[1354] 00
                    nop                                     ;[1355] 00
                    nop                                     ;[1356] 00
                    nop                                     ;[1357] 00
                    nop                                     ;[1358] 00
                    nop                                     ;[1359] 00
                    nop                                     ;[135a] 00
                    nop                                     ;[135b] 00
                    nop                                     ;[135c] 00
                    nop                                     ;[135d] 00
                    nop                                     ;[135e] 00
                    nop                                     ;[135f] 00
                    nop                                     ;[1360] 00
                    nop                                     ;[1361] 00
                    nop                                     ;[1362] 00
                    nop                                     ;[1363] 00
                    nop                                     ;[1364] 00
                    nop                                     ;[1365] 00
                    nop                                     ;[1366] 00
                    nop                                     ;[1367] 00
                    nop                                     ;[1368] 00
                    nop                                     ;[1369] 00
                    nop                                     ;[136a] 00
                    nop                                     ;[136b] 00
                    nop                                     ;[136c] 00
                    nop                                     ;[136d] 00
                    nop                                     ;[136e] 00
                    nop                                     ;[136f] 00
                    nop                                     ;[1370] 00
                    nop                                     ;[1371] 00
                    nop                                     ;[1372] 00
                    nop                                     ;[1373] 00
                    nop                                     ;[1374] 00
                    nop                                     ;[1375] 00
                    nop                                     ;[1376] 00
                    nop                                     ;[1377] 00
                    nop                                     ;[1378] 00
                    nop                                     ;[1379] 00
                    nop                                     ;[137a] 00
                    nop                                     ;[137b] 00
                    nop                                     ;[137c] 00
                    nop                                     ;[137d] 00
                    nop                                     ;[137e] 00
                    nop                                     ;[137f] 00
                    nop                                     ;[1380] 00
                    nop                                     ;[1381] 00
                    nop                                     ;[1382] 00
                    nop                                     ;[1383] 00
                    nop                                     ;[1384] 00
                    nop                                     ;[1385] 00
                    nop                                     ;[1386] 00
                    nop                                     ;[1387] 00
                    nop                                     ;[1388] 00
                    nop                                     ;[1389] 00
                    nop                                     ;[138a] 00
                    nop                                     ;[138b] 00
                    nop                                     ;[138c] 00
                    nop                                     ;[138d] 00
                    nop                                     ;[138e] 00
                    nop                                     ;[138f] 00
                    nop                                     ;[1390] 00
                    nop                                     ;[1391] 00
                    nop                                     ;[1392] 00
                    nop                                     ;[1393] 00
                    nop                                     ;[1394] 00
                    nop                                     ;[1395] 00
                    nop                                     ;[1396] 00
                    nop                                     ;[1397] 00
                    nop                                     ;[1398] 00
                    nop                                     ;[1399] 00
                    nop                                     ;[139a] 00
                    nop                                     ;[139b] 00
                    nop                                     ;[139c] 00
                    nop                                     ;[139d] 00
                    nop                                     ;[139e] 00
                    nop                                     ;[139f] 00
                    nop                                     ;[13a0] 00
                    nop                                     ;[13a1] 00
                    nop                                     ;[13a2] 00
                    nop                                     ;[13a3] 00
                    nop                                     ;[13a4] 00
                    nop                                     ;[13a5] 00
                    nop                                     ;[13a6] 00
                    nop                                     ;[13a7] 00
                    nop                                     ;[13a8] 00
                    nop                                     ;[13a9] 00
                    nop                                     ;[13aa] 00
                    nop                                     ;[13ab] 00
                    nop                                     ;[13ac] 00
                    nop                                     ;[13ad] 00
                    nop                                     ;[13ae] 00
                    nop                                     ;[13af] 00
                    nop                                     ;[13b0] 00
                    nop                                     ;[13b1] 00
                    nop                                     ;[13b2] 00
                    nop                                     ;[13b3] 00
                    nop                                     ;[13b4] 00
                    nop                                     ;[13b5] 00
                    nop                                     ;[13b6] 00
                    nop                                     ;[13b7] 00
                    nop                                     ;[13b8] 00
                    nop                                     ;[13b9] 00
                    nop                                     ;[13ba] 00
                    nop                                     ;[13bb] 00
                    nop                                     ;[13bc] 00
                    nop                                     ;[13bd] 00
                    nop                                     ;[13be] 00
                    nop                                     ;[13bf] 00
                    nop                                     ;[13c0] 00
                    nop                                     ;[13c1] 00
                    nop                                     ;[13c2] 00
                    nop                                     ;[13c3] 00
                    nop                                     ;[13c4] 00
                    nop                                     ;[13c5] 00
                    nop                                     ;[13c6] 00
                    nop                                     ;[13c7] 00
                    nop                                     ;[13c8] 00
                    nop                                     ;[13c9] 00
                    nop                                     ;[13ca] 00
                    nop                                     ;[13cb] 00
                    nop                                     ;[13cc] 00
                    nop                                     ;[13cd] 00
                    nop                                     ;[13ce] 00
                    nop                                     ;[13cf] 00
                    nop                                     ;[13d0] 00
                    nop                                     ;[13d1] 00
                    nop                                     ;[13d2] 00
                    nop                                     ;[13d3] 00
                    nop                                     ;[13d4] 00
                    nop                                     ;[13d5] 00
                    nop                                     ;[13d6] 00
                    nop                                     ;[13d7] 00
                    nop                                     ;[13d8] 00
                    nop                                     ;[13d9] 00
                    nop                                     ;[13da] 00
                    nop                                     ;[13db] 00
                    nop                                     ;[13dc] 00
                    nop                                     ;[13dd] 00
                    nop                                     ;[13de] 00
                    nop                                     ;[13df] 00
                    nop                                     ;[13e0] 00
                    nop                                     ;[13e1] 00
                    nop                                     ;[13e2] 00
                    nop                                     ;[13e3] 00
                    nop                                     ;[13e4] 00
                    nop                                     ;[13e5] 00
                    nop                                     ;[13e6] 00
                    nop                                     ;[13e7] 00
                    nop                                     ;[13e8] 00
                    nop                                     ;[13e9] 00
                    nop                                     ;[13ea] 00
                    nop                                     ;[13eb] 00
                    nop                                     ;[13ec] 00
                    nop                                     ;[13ed] 00
                    nop                                     ;[13ee] 00
                    nop                                     ;[13ef] 00
                    nop                                     ;[13f0] 00
                    nop                                     ;[13f1] 00
                    nop                                     ;[13f2] 00
                    nop                                     ;[13f3] 00
                    nop                                     ;[13f4] 00
                    nop                                     ;[13f5] 00
                    nop                                     ;[13f6] 00
                    nop                                     ;[13f7] 00
                    nop                                     ;[13f8] 00
                    nop                                     ;[13f9] 00
                    nop                                     ;[13fa] 00
                    nop                                     ;[13fb] 00
                    nop                                     ;[13fc] 00
                    nop                                     ;[13fd] 00
                    nop                                     ;[13fe] 00
                    nop                                     ;[13ff] 00
                    nop                                     ;[1400] 00
                    nop                                     ;[1401] 00
                    nop                                     ;[1402] 00
                    nop                                     ;[1403] 00
                    nop                                     ;[1404] 00
                    nop                                     ;[1405] 00
                    nop                                     ;[1406] 00
                    nop                                     ;[1407] 00
                    nop                                     ;[1408] 00
                    nop                                     ;[1409] 00
                    nop                                     ;[140a] 00
                    nop                                     ;[140b] 00
                    nop                                     ;[140c] 00
                    nop                                     ;[140d] 00
                    nop                                     ;[140e] 00
                    nop                                     ;[140f] 00
                    nop                                     ;[1410] 00
                    nop                                     ;[1411] 00
                    nop                                     ;[1412] 00
                    nop                                     ;[1413] 00
                    nop                                     ;[1414] 00
                    nop                                     ;[1415] 00
                    nop                                     ;[1416] 00
                    nop                                     ;[1417] 00
                    nop                                     ;[1418] 00
                    nop                                     ;[1419] 00
                    nop                                     ;[141a] 00
                    nop                                     ;[141b] 00
                    nop                                     ;[141c] 00
                    nop                                     ;[141d] 00
                    nop                                     ;[141e] 00
                    nop                                     ;[141f] 00
                    nop                                     ;[1420] 00
                    nop                                     ;[1421] 00
                    nop                                     ;[1422] 00
                    nop                                     ;[1423] 00
                    nop                                     ;[1424] 00
                    nop                                     ;[1425] 00
                    nop                                     ;[1426] 00
                    nop                                     ;[1427] 00
                    nop                                     ;[1428] 00
                    nop                                     ;[1429] 00
                    nop                                     ;[142a] 00
                    nop                                     ;[142b] 00
                    nop                                     ;[142c] 00
                    nop                                     ;[142d] 00
                    nop                                     ;[142e] 00
                    nop                                     ;[142f] 00
                    nop                                     ;[1430] 00
                    nop                                     ;[1431] 00
                    nop                                     ;[1432] 00
                    nop                                     ;[1433] 00
                    nop                                     ;[1434] 00
                    nop                                     ;[1435] 00
                    nop                                     ;[1436] 00
                    nop                                     ;[1437] 00
                    nop                                     ;[1438] 00
                    nop                                     ;[1439] 00
                    nop                                     ;[143a] 00
                    nop                                     ;[143b] 00
                    nop                                     ;[143c] 00
                    nop                                     ;[143d] 00
                    nop                                     ;[143e] 00
                    nop                                     ;[143f] 00
                    nop                                     ;[1440] 00
                    nop                                     ;[1441] 00
                    nop                                     ;[1442] 00
                    nop                                     ;[1443] 00
                    nop                                     ;[1444] 00
                    nop                                     ;[1445] 00
                    nop                                     ;[1446] 00
                    nop                                     ;[1447] 00
                    nop                                     ;[1448] 00
                    nop                                     ;[1449] 00
                    nop                                     ;[144a] 00
                    nop                                     ;[144b] 00
                    nop                                     ;[144c] 00
                    nop                                     ;[144d] 00
                    nop                                     ;[144e] 00
                    nop                                     ;[144f] 00
                    nop                                     ;[1450] 00
                    nop                                     ;[1451] 00
                    nop                                     ;[1452] 00
                    nop                                     ;[1453] 00
                    nop                                     ;[1454] 00
                    nop                                     ;[1455] 00
                    nop                                     ;[1456] 00
                    nop                                     ;[1457] 00
                    nop                                     ;[1458] 00
                    nop                                     ;[1459] 00
                    nop                                     ;[145a] 00
                    nop                                     ;[145b] 00
                    nop                                     ;[145c] 00
                    nop                                     ;[145d] 00
                    nop                                     ;[145e] 00
                    nop                                     ;[145f] 00
                    nop                                     ;[1460] 00
                    nop                                     ;[1461] 00
                    nop                                     ;[1462] 00
                    nop                                     ;[1463] 00
                    nop                                     ;[1464] 00
                    nop                                     ;[1465] 00
                    nop                                     ;[1466] 00
                    nop                                     ;[1467] 00
                    nop                                     ;[1468] 00
                    nop                                     ;[1469] 00
                    nop                                     ;[146a] 00
                    nop                                     ;[146b] 00
                    nop                                     ;[146c] 00
                    nop                                     ;[146d] 00
                    nop                                     ;[146e] 00
                    nop                                     ;[146f] 00
                    nop                                     ;[1470] 00
                    nop                                     ;[1471] 00
                    nop                                     ;[1472] 00
                    nop                                     ;[1473] 00
                    nop                                     ;[1474] 00
                    nop                                     ;[1475] 00
                    nop                                     ;[1476] 00
                    nop                                     ;[1477] 00
                    nop                                     ;[1478] 00
                    nop                                     ;[1479] 00
                    nop                                     ;[147a] 00
                    nop                                     ;[147b] 00
                    nop                                     ;[147c] 00
                    nop                                     ;[147d] 00
                    nop                                     ;[147e] 00
                    nop                                     ;[147f] 00
                    nop                                     ;[1480] 00
                    nop                                     ;[1481] 00
                    nop                                     ;[1482] 00
                    nop                                     ;[1483] 00
                    nop                                     ;[1484] 00
                    nop                                     ;[1485] 00
                    nop                                     ;[1486] 00
                    nop                                     ;[1487] 00
                    nop                                     ;[1488] 00
                    nop                                     ;[1489] 00
                    nop                                     ;[148a] 00
                    nop                                     ;[148b] 00
                    nop                                     ;[148c] 00
                    nop                                     ;[148d] 00
                    nop                                     ;[148e] 00
                    nop                                     ;[148f] 00
                    nop                                     ;[1490] 00
                    nop                                     ;[1491] 00
                    nop                                     ;[1492] 00
                    nop                                     ;[1493] 00
                    nop                                     ;[1494] 00
                    nop                                     ;[1495] 00
                    nop                                     ;[1496] 00
                    nop                                     ;[1497] 00
                    nop                                     ;[1498] 00
                    nop                                     ;[1499] 00
                    nop                                     ;[149a] 00
                    nop                                     ;[149b] 00
                    nop                                     ;[149c] 00
                    nop                                     ;[149d] 00
                    nop                                     ;[149e] 00
                    nop                                     ;[149f] 00
                    nop                                     ;[14a0] 00
                    nop                                     ;[14a1] 00
                    nop                                     ;[14a2] 00
                    nop                                     ;[14a3] 00
                    nop                                     ;[14a4] 00
                    nop                                     ;[14a5] 00
                    nop                                     ;[14a6] 00
                    nop                                     ;[14a7] 00
                    nop                                     ;[14a8] 00
                    nop                                     ;[14a9] 00
                    nop                                     ;[14aa] 00
                    nop                                     ;[14ab] 00
                    nop                                     ;[14ac] 00
                    nop                                     ;[14ad] 00
                    nop                                     ;[14ae] 00
                    nop                                     ;[14af] 00
                    nop                                     ;[14b0] 00
                    nop                                     ;[14b1] 00
                    nop                                     ;[14b2] 00
                    nop                                     ;[14b3] 00
                    nop                                     ;[14b4] 00
                    nop                                     ;[14b5] 00
                    nop                                     ;[14b6] 00
                    nop                                     ;[14b7] 00
                    nop                                     ;[14b8] 00
                    nop                                     ;[14b9] 00
                    nop                                     ;[14ba] 00
                    nop                                     ;[14bb] 00
                    nop                                     ;[14bc] 00
                    nop                                     ;[14bd] 00
                    nop                                     ;[14be] 00
                    nop                                     ;[14bf] 00
                    nop                                     ;[14c0] 00
                    nop                                     ;[14c1] 00
                    nop                                     ;[14c2] 00
                    nop                                     ;[14c3] 00
                    nop                                     ;[14c4] 00
                    nop                                     ;[14c5] 00
                    nop                                     ;[14c6] 00
                    nop                                     ;[14c7] 00
                    nop                                     ;[14c8] 00
                    nop                                     ;[14c9] 00
                    nop                                     ;[14ca] 00
                    nop                                     ;[14cb] 00
                    nop                                     ;[14cc] 00
                    nop                                     ;[14cd] 00
                    nop                                     ;[14ce] 00
                    nop                                     ;[14cf] 00
                    nop                                     ;[14d0] 00
                    nop                                     ;[14d1] 00
                    nop                                     ;[14d2] 00
                    nop                                     ;[14d3] 00
                    nop                                     ;[14d4] 00
                    nop                                     ;[14d5] 00
                    nop                                     ;[14d6] 00
                    nop                                     ;[14d7] 00
                    nop                                     ;[14d8] 00
                    nop                                     ;[14d9] 00
                    nop                                     ;[14da] 00
                    nop                                     ;[14db] 00
                    nop                                     ;[14dc] 00
                    nop                                     ;[14dd] 00
                    nop                                     ;[14de] 00
                    nop                                     ;[14df] 00
                    nop                                     ;[14e0] 00
                    nop                                     ;[14e1] 00
                    nop                                     ;[14e2] 00
                    nop                                     ;[14e3] 00
                    nop                                     ;[14e4] 00
                    nop                                     ;[14e5] 00
                    nop                                     ;[14e6] 00
                    nop                                     ;[14e7] 00
                    nop                                     ;[14e8] 00
                    nop                                     ;[14e9] 00
                    nop                                     ;[14ea] 00
                    nop                                     ;[14eb] 00
                    nop                                     ;[14ec] 00
                    nop                                     ;[14ed] 00
                    nop                                     ;[14ee] 00
                    nop                                     ;[14ef] 00
                    nop                                     ;[14f0] 00
                    nop                                     ;[14f1] 00
                    nop                                     ;[14f2] 00
                    nop                                     ;[14f3] 00
                    nop                                     ;[14f4] 00
                    nop                                     ;[14f5] 00
                    nop                                     ;[14f6] 00
                    nop                                     ;[14f7] 00
                    nop                                     ;[14f8] 00
                    nop                                     ;[14f9] 00
                    nop                                     ;[14fa] 00
                    nop                                     ;[14fb] 00
                    nop                                     ;[14fc] 00
                    nop                                     ;[14fd] 00
                    nop                                     ;[14fe] 00
                    nop                                     ;[14ff] 00
                    nop                                     ;[1500] 00
                    nop                                     ;[1501] 00
                    nop                                     ;[1502] 00
                    nop                                     ;[1503] 00
                    nop                                     ;[1504] 00
                    nop                                     ;[1505] 00
                    nop                                     ;[1506] 00
                    nop                                     ;[1507] 00
                    nop                                     ;[1508] 00
                    nop                                     ;[1509] 00
                    nop                                     ;[150a] 00
                    nop                                     ;[150b] 00
                    nop                                     ;[150c] 00
                    nop                                     ;[150d] 00
                    nop                                     ;[150e] 00
                    nop                                     ;[150f] 00
                    nop                                     ;[1510] 00
                    nop                                     ;[1511] 00
                    nop                                     ;[1512] 00
                    nop                                     ;[1513] 00
                    nop                                     ;[1514] 00
                    nop                                     ;[1515] 00
                    nop                                     ;[1516] 00
                    nop                                     ;[1517] 00
                    nop                                     ;[1518] 00
                    nop                                     ;[1519] 00
                    nop                                     ;[151a] 00
                    nop                                     ;[151b] 00
                    nop                                     ;[151c] 00
                    nop                                     ;[151d] 00
                    nop                                     ;[151e] 00
                    nop                                     ;[151f] 00
                    nop                                     ;[1520] 00
                    nop                                     ;[1521] 00
                    nop                                     ;[1522] 00
                    nop                                     ;[1523] 00
                    nop                                     ;[1524] 00
                    nop                                     ;[1525] 00
                    nop                                     ;[1526] 00
                    nop                                     ;[1527] 00
                    nop                                     ;[1528] 00
                    nop                                     ;[1529] 00
                    nop                                     ;[152a] 00
                    nop                                     ;[152b] 00
                    nop                                     ;[152c] 00
                    nop                                     ;[152d] 00
                    nop                                     ;[152e] 00
                    nop                                     ;[152f] 00
                    nop                                     ;[1530] 00
                    nop                                     ;[1531] 00
                    nop                                     ;[1532] 00
                    nop                                     ;[1533] 00
                    nop                                     ;[1534] 00
                    nop                                     ;[1535] 00
                    nop                                     ;[1536] 00
                    nop                                     ;[1537] 00
                    nop                                     ;[1538] 00
                    nop                                     ;[1539] 00
                    nop                                     ;[153a] 00
                    nop                                     ;[153b] 00
                    nop                                     ;[153c] 00
                    nop                                     ;[153d] 00
                    nop                                     ;[153e] 00
                    nop                                     ;[153f] 00
                    nop                                     ;[1540] 00
                    nop                                     ;[1541] 00
                    nop                                     ;[1542] 00
                    nop                                     ;[1543] 00
                    nop                                     ;[1544] 00
                    nop                                     ;[1545] 00
                    nop                                     ;[1546] 00
                    nop                                     ;[1547] 00
                    nop                                     ;[1548] 00
                    nop                                     ;[1549] 00
                    nop                                     ;[154a] 00
                    nop                                     ;[154b] 00
                    nop                                     ;[154c] 00
                    nop                                     ;[154d] 00
                    nop                                     ;[154e] 00
                    nop                                     ;[154f] 00
                    nop                                     ;[1550] 00
                    nop                                     ;[1551] 00
                    nop                                     ;[1552] 00
                    nop                                     ;[1553] 00
                    nop                                     ;[1554] 00
                    nop                                     ;[1555] 00
                    nop                                     ;[1556] 00
                    nop                                     ;[1557] 00
                    nop                                     ;[1558] 00
                    nop                                     ;[1559] 00
                    nop                                     ;[155a] 00
                    nop                                     ;[155b] 00
                    nop                                     ;[155c] 00
                    nop                                     ;[155d] 00
                    nop                                     ;[155e] 00
                    nop                                     ;[155f] 00
                    nop                                     ;[1560] 00
                    nop                                     ;[1561] 00
                    nop                                     ;[1562] 00
                    nop                                     ;[1563] 00
                    nop                                     ;[1564] 00
                    nop                                     ;[1565] 00
                    nop                                     ;[1566] 00
                    nop                                     ;[1567] 00
                    nop                                     ;[1568] 00
                    nop                                     ;[1569] 00
                    nop                                     ;[156a] 00
                    nop                                     ;[156b] 00
                    nop                                     ;[156c] 00
                    nop                                     ;[156d] 00
                    nop                                     ;[156e] 00
                    nop                                     ;[156f] 00
                    nop                                     ;[1570] 00
                    nop                                     ;[1571] 00
                    nop                                     ;[1572] 00
                    nop                                     ;[1573] 00
                    nop                                     ;[1574] 00
                    nop                                     ;[1575] 00
                    nop                                     ;[1576] 00
                    nop                                     ;[1577] 00
                    nop                                     ;[1578] 00
                    nop                                     ;[1579] 00
                    nop                                     ;[157a] 00
                    nop                                     ;[157b] 00
                    nop                                     ;[157c] 00
                    nop                                     ;[157d] 00
                    nop                                     ;[157e] 00
                    nop                                     ;[157f] 00
                    nop                                     ;[1580] 00
                    nop                                     ;[1581] 00
                    nop                                     ;[1582] 00
                    nop                                     ;[1583] 00
                    nop                                     ;[1584] 00
                    nop                                     ;[1585] 00
                    nop                                     ;[1586] 00
                    nop                                     ;[1587] 00
                    nop                                     ;[1588] 00
                    nop                                     ;[1589] 00
                    nop                                     ;[158a] 00
                    nop                                     ;[158b] 00
                    nop                                     ;[158c] 00
                    nop                                     ;[158d] 00
                    nop                                     ;[158e] 00
                    nop                                     ;[158f] 00
                    nop                                     ;[1590] 00
                    nop                                     ;[1591] 00
                    nop                                     ;[1592] 00
                    nop                                     ;[1593] 00
                    nop                                     ;[1594] 00
                    nop                                     ;[1595] 00
                    nop                                     ;[1596] 00
                    nop                                     ;[1597] 00
                    nop                                     ;[1598] 00
                    nop                                     ;[1599] 00
                    nop                                     ;[159a] 00
                    nop                                     ;[159b] 00
                    nop                                     ;[159c] 00
                    nop                                     ;[159d] 00
                    nop                                     ;[159e] 00
                    nop                                     ;[159f] 00
                    nop                                     ;[15a0] 00
                    nop                                     ;[15a1] 00
                    nop                                     ;[15a2] 00
                    nop                                     ;[15a3] 00
                    nop                                     ;[15a4] 00
                    nop                                     ;[15a5] 00
                    nop                                     ;[15a6] 00
                    nop                                     ;[15a7] 00
                    nop                                     ;[15a8] 00
                    nop                                     ;[15a9] 00
                    nop                                     ;[15aa] 00
                    nop                                     ;[15ab] 00
                    nop                                     ;[15ac] 00
                    nop                                     ;[15ad] 00
                    nop                                     ;[15ae] 00
                    nop                                     ;[15af] 00
                    nop                                     ;[15b0] 00
                    nop                                     ;[15b1] 00
                    nop                                     ;[15b2] 00
                    nop                                     ;[15b3] 00
                    nop                                     ;[15b4] 00
                    nop                                     ;[15b5] 00
                    nop                                     ;[15b6] 00
                    nop                                     ;[15b7] 00
                    nop                                     ;[15b8] 00
                    nop                                     ;[15b9] 00
                    nop                                     ;[15ba] 00
                    nop                                     ;[15bb] 00
                    nop                                     ;[15bc] 00
                    nop                                     ;[15bd] 00
                    nop                                     ;[15be] 00
                    nop                                     ;[15bf] 00
                    nop                                     ;[15c0] 00
                    nop                                     ;[15c1] 00
                    nop                                     ;[15c2] 00
                    nop                                     ;[15c3] 00
                    nop                                     ;[15c4] 00
                    nop                                     ;[15c5] 00
                    nop                                     ;[15c6] 00
                    nop                                     ;[15c7] 00
                    nop                                     ;[15c8] 00
                    nop                                     ;[15c9] 00
                    nop                                     ;[15ca] 00
                    nop                                     ;[15cb] 00
                    nop                                     ;[15cc] 00
                    nop                                     ;[15cd] 00
                    nop                                     ;[15ce] 00
                    nop                                     ;[15cf] 00
                    nop                                     ;[15d0] 00
                    nop                                     ;[15d1] 00
                    nop                                     ;[15d2] 00
                    nop                                     ;[15d3] 00
                    nop                                     ;[15d4] 00
                    nop                                     ;[15d5] 00
                    nop                                     ;[15d6] 00
                    nop                                     ;[15d7] 00
                    nop                                     ;[15d8] 00
                    nop                                     ;[15d9] 00
                    nop                                     ;[15da] 00
                    nop                                     ;[15db] 00
                    nop                                     ;[15dc] 00
                    nop                                     ;[15dd] 00
                    nop                                     ;[15de] 00
                    nop                                     ;[15df] 00
                    nop                                     ;[15e0] 00
                    nop                                     ;[15e1] 00
                    nop                                     ;[15e2] 00
                    nop                                     ;[15e3] 00
                    nop                                     ;[15e4] 00
                    nop                                     ;[15e5] 00
                    nop                                     ;[15e6] 00
                    nop                                     ;[15e7] 00
                    nop                                     ;[15e8] 00
                    nop                                     ;[15e9] 00
                    nop                                     ;[15ea] 00
                    nop                                     ;[15eb] 00
                    nop                                     ;[15ec] 00
                    nop                                     ;[15ed] 00
                    nop                                     ;[15ee] 00
                    nop                                     ;[15ef] 00
                    nop                                     ;[15f0] 00
                    nop                                     ;[15f1] 00
                    nop                                     ;[15f2] 00
                    nop                                     ;[15f3] 00
                    nop                                     ;[15f4] 00
                    nop                                     ;[15f5] 00
                    nop                                     ;[15f6] 00
                    nop                                     ;[15f7] 00
                    nop                                     ;[15f8] 00
                    nop                                     ;[15f9] 00
                    nop                                     ;[15fa] 00
                    nop                                     ;[15fb] 00
                    nop                                     ;[15fc] 00
                    nop                                     ;[15fd] 00
                    ld        hl,($5c51)                    ;[15fe] 2a 51 5c
                    jp        $036c                         ;[1601] c3 6c 03
                    ld        (hl),c                        ;[1604] 71
                    jr        $1587                         ;[1605] 18 80
                    jr        $1676                         ;[1607] 18 6d
                    add       hl,de                         ;[1609] 19
                    or        b                             ;[160a] b0
                    add       hl,de                         ;[160b] 19
                    jr        nc,$1628                      ;[160c] 30 1a
                    ld        b,l                           ;[160e] 45
                    ld        a,(de)                        ;[160f] 1a
                    ret       po                            ;[1610] e0
                    dec       de                            ;[1611] 1b
                    ld        (hl),c                        ;[1612] 71
                    jr        nz,$165f                      ;[1613] 20 4a
                    ld        a,(de)                        ;[1615] 1a
                    ld        h,(hl)                        ;[1616] 66
                    ld        a,(de)                        ;[1617] 1a
                    and       d                             ;[1618] a2
                    ld        a,(de)                        ;[1619] 1a
                    cp        a                             ;[161a] bf
                    ld        a,(de)                        ;[161b] 1a
                    add       a                             ;[161c] 87
                    dec       de                            ;[161d] 1b
                    ld        b,e                           ;[161e] 43
                    jr        nz,$15f0                      ;[161f] 20 cf
                    jr        nz,$15ed                      ;[1621] 20 ca
                    jr        nz,$1615                      ;[1623] 20 f0
                    dec       de                            ;[1625] 1b
                    ld        d,e                           ;[1626] 53
                    inc       e                             ;[1627] 1c
                    sbc       c                             ;[1628] 99
                    inc       e                             ;[1629] 1c
                    or        b                             ;[162a] b0
                    inc       e                             ;[162b] 1c
                    jp        c,$fa1c                       ;[162c] da 1c fa
                    inc       e                             ;[162f] 1c
                    jp        c,$d61a                       ;[1630] da 1a d6
                    ld        a,(de)                        ;[1633] 1a
                    rst       $00                           ;[1634] c7
                    inc       e                             ;[1635] 1c
                    rr        d                             ;[1636] cb 1a
                    jp        $0620                         ;[1638] c3 20 06
                    dec       e                             ;[163b] 1d
                    rra                                     ;[163c] 1f
                    dec       e                             ;[163d] 1d
                    ld        hl,($d11d)                    ;[163e] 2a 1d d1
                    ld        d,$01                         ;[1641] 16 01
                    add       hl,de                         ;[1643] 19
                    ld        c,d                           ;[1644] 4a
                    dec       de                            ;[1645] 1b
                    pop       bc                            ;[1646] c1
                    dec       de                            ;[1647] 1b
                    rst       $08                           ;[1648] cf
                    ld        a,(de)                        ;[1649] 1a
                    push      af                            ;[164a] f5
                    ld        a,(de)                        ;[164b] 1a
                    rst       $38                           ;[164c] ff
                    add       hl,de                         ;[164d] 19
                    ld        b,h                           ;[164e] 44
                    ld        a,(de)                        ;[164f] 1a
                    ld        l,l                           ;[1650] 6d
                    add       hl,de                         ;[1651] 19
                    or        b                             ;[1652] b0
                    add       hl,de                         ;[1653] 19
                    jr        nc,$1670                      ;[1654] 30 1a
                    ld        b,l                           ;[1656] 45
                    ld        a,(de)                        ;[1657] 1a
                    ld        b,h                           ;[1658] 44
                    ld        a,(de)                        ;[1659] 1a
                    ld        (hl),c                        ;[165a] 71
                    jr        nz,$16a1                      ;[165b] 20 44
                    ld        a,(de)                        ;[165d] 1a
                    ld        b,h                           ;[165e] 44
                    ld        a,(de)                        ;[165f] 1a
                    and       d                             ;[1660] a2
                    ld        a,(de)                        ;[1661] 1a
                    cp        a                             ;[1662] bf
                    ld        a,(de)                        ;[1663] 1a
                    ld        b,h                           ;[1664] 44
                    ld        a,(de)                        ;[1665] 1a
                    or        (hl)                          ;[1666] b6
                    jr        $1638                         ;[1667] 18 cf
                    jr        nz,$1635                      ;[1669] 20 ca
                    jr        nz,$1697                      ;[166b] 20 2a
                    rla                                     ;[166d] 17
                    ld        hl,($2a17)                    ;[166e] 2a 17 2a
                    rla                                     ;[1671] 17
                    ld        hl,($2a17)                    ;[1672] 2a 17 2a
                    rla                                     ;[1675] 17
                    ld        hl,($4417)                    ;[1676] 2a 17 44
                    ld        a,(de)                        ;[1679] 1a
                    ld        b,h                           ;[167a] 44
                    ld        a,(de)                        ;[167b] 1a
                    ld        hl,($4417)                    ;[167c] 2a 17 44
                    ld        a,(de)                        ;[167f] 1a
                    ld        hl,($2a17)                    ;[1680] 2a 17 2a
                    rla                                     ;[1683] 17
                    ld        b,h                           ;[1684] 44
                    ld        a,(de)                        ;[1685] 1a
                    ld        hl,($8c17)                    ;[1686] 2a 17 8c
                    ld        d,$44                         ;[1689] 16 44
                    ld        a,(de)                        ;[168b] 1a
                    set       6,(ix+$25)                    ;[168c] dd cb 25 f6
                    set       7,(ix+$25)                    ;[1690] dd cb 25 fe
                    ret                                     ;[1694] c9

                    push      ix                            ;[1695] dd e5
                    pop       hl                            ;[1697] e1
                    add       hl,$0030                      ;[1698] ed 34 30 00
                    ret                                     ;[169c] c9

                    call      $1695                         ;[169d] cd 95 16
                    ld        bc,$00ff                      ;[16a0] 01 ff 00
                    bit       7,(ix+$19)                    ;[16a3] dd cb 19 7e
                    ret       nz                            ;[16a7] c0
                    call      $16b5                         ;[16a8] cd b5 16
                    ld        c,a                           ;[16ab] 4f
                    inc       c                             ;[16ac] 0c
                    ret                                     ;[16ad] c9

                    ld        a,$20                         ;[16ae] 3e 20
                    inc       ixh                           ;[16b0] dd 24
                    dec       ixh                           ;[16b2] dd 25
                    ret       z                             ;[16b4] c8
                    ld        a,(ix+$1c)                    ;[16b5] dd 7e 1c
                    bit       4,(ix+$25)                    ;[16b8] dd cb 25 66
                    ret       z                             ;[16bc] c8
                    srl       a                             ;[16bd] cb 3f
                    ret                                     ;[16bf] c9

                    push      af                            ;[16c0] f5
                    push      bc                            ;[16c1] c5
                    push      de                            ;[16c2] d5
                    push      hl                            ;[16c3] e5
                    ld        e,a                           ;[16c4] 5f
                    call      $1d47                         ;[16c5] cd 47 1d
                    pop       hl                            ;[16c8] e1
                    pop       de                            ;[16c9] d1
                    pop       bc                            ;[16ca] c1
                    pop       af                            ;[16cb] f1
                    ret                                     ;[16cc] c9

                    res       7,(ix+$25)                    ;[16cd] dd cb 25 be
                    bit       4,(ix+$19)                    ;[16d1] dd cb 19 66
                    jr        z,$1708                       ;[16d5] 28 31
                    call      $22ec                         ;[16d7] cd ec 22
                    ld        (ix+$0f),a                    ;[16da] dd 77 0f
                    ld        c,a                           ;[16dd] 4f
                    ld        b,a                           ;[16de] 47
                    xor       a                             ;[16df] af
                    scf                                     ;[16e0] 37
                    rra                                     ;[16e1] 1f
                    djnz      $16e0                         ;[16e2] 10 fc
                    ld        (ix+$10),a                    ;[16e4] dd 77 10
                    ld        (ix+$0d),l                    ;[16e7] dd 75 0d
                    ld        (ix+$0e),h                    ;[16ea] dd 74 0e
                    ld        l,(ix+$11)                    ;[16ed] dd 6e 11
                    ld        h,b                           ;[16f0] 60
                    add       hl,hl                         ;[16f1] 29
                    add       hl,hl                         ;[16f2] 29
                    add       hl,hl                         ;[16f3] 29
                    xor       a                             ;[16f4] af
                    bit       4,(ix+$1d)                    ;[16f5] dd cb 1d 66
                    jr        z,$16fc                       ;[16f9] 28 01
                    add       hl,hl                         ;[16fb] 29
                    sbc       hl,bc                         ;[16fc] ed 42
                    inc       a                             ;[16fe] 3c
                    jr        nc,$16fc                      ;[16ff] 30 fb
                    dec       a                             ;[1701] 3d
                    ld        (ix+$1c),a                    ;[1702] dd 77 1c
                    jp        $2043                         ;[1705] c3 43 20
                    and       $03                           ;[1708] e6 03
                    cp        $03                           ;[170a] fe 03
                    ret       z                             ;[170c] c8
                    ld        b,a                           ;[170d] 47
                    ld        a,(ix+$19)                    ;[170e] dd 7e 19
                    and       $fc                           ;[1711] e6 fc
                    or        b                             ;[1713] b0
                    ld        (ix+$19),a                    ;[1714] dd 77 19
                    ret                                     ;[1717] c9

                    ld        hl,$164c                      ;[1718] 21 4c 16
                    jp        $1d61                         ;[171b] c3 61 1d
                    res       6,(ix+$25)                    ;[171e] dd cb 25 b6
                    bit       7,(ix+$25)                    ;[1722] dd cb 25 7e
                    jr        nz,$16cd                      ;[1726] 20 a5
                    jr        $175c                         ;[1728] 18 32
                    bit       7,(ix+$19)                    ;[172a] dd cb 19 7e
                    ret       z                             ;[172e] c8
                    set       6,(ix+$25)                    ;[172f] dd cb 25 f6
                    jr        $175c                         ;[1733] 18 27
                    ld        a,e                           ;[1735] 7b
                    bit       6,(ix+$25)                    ;[1736] dd cb 25 76
                    jr        nz,$171e                      ;[173a] 20 e2
                    cp        $20                           ;[173c] fe 20
                    jr        c,$1718                       ;[173e] 38 d8
                    jr        nz,$1756                      ;[1740] 20 14
                    ld        a,(ix+$2a)                    ;[1742] dd 7e 2a
                    and       a                             ;[1745] a7
                    ret       z                             ;[1746] c8
                    ld        (ix+$2c),a                    ;[1747] dd 77 2c
                    ld        a,(ix+$29)                    ;[174a] dd 7e 29
                    dec       a                             ;[174d] 3d
                    ld        (ix+$2b),a                    ;[174e] dd 77 2b
                    inc       (ix+$2a)                      ;[1751] dd 34 2a
                    jr        $175c                         ;[1754] 18 06
                    inc       (ix+$2a)                      ;[1756] dd 34 2a
                    call      $18d9                         ;[1759] cd d9 18
                    call      $1695                         ;[175c] cd 95 16
                    ld        a,(ix+$29)                    ;[175f] dd 7e 29
                    add       hl,a                          ;[1762] ed 31
                    ld        (hl),e                        ;[1764] 73
                    inc       a                             ;[1765] 3c
                    ld        (ix+$29),a                    ;[1766] dd 77 29
                    bit       6,(ix+$25)                    ;[1769] dd cb 25 76
                    ret       nz                            ;[176d] c0
                    cp        $fe                           ;[176e] fe fe
                    jr        nc,$1779                      ;[1770] 30 07
                    call      $16b5                         ;[1772] cd b5 16
                    cp        (ix+$2a)                      ;[1775] dd be 2a
                    ret       nc                            ;[1778] d0
                    call      $16b5                         ;[1779] cd b5 16
                    cp        (ix+$2a)                      ;[177c] dd be 2a
                    jr        nc,$17b7                      ;[177f] 30 36
                    ld        a,(ix+$2c)                    ;[1781] dd 7e 2c
                    cp        $01                           ;[1784] fe 01
                    ld        a,(ix+$2b)                    ;[1786] dd 7e 2b
                    jr        z,$178e                       ;[1789] 28 03
                    and       a                             ;[178b] a7
                    jr        z,$1795                       ;[178c] 28 07
                    ld        c,a                           ;[178e] 4f
                    inc       c                             ;[178f] 0c
                    ld        b,(ix+$2c)                    ;[1790] dd 46 2c
                    jr        $17bd                         ;[1793] 18 28
                    call      $1695                         ;[1795] cd 95 16
                    call      $16b5                         ;[1798] cd b5 16
                    ld        b,a                           ;[179b] 47
                    ld        c,$00                         ;[179c] 0e 00
                    ld        a,(hl)                        ;[179e] 7e
                    inc       hl                            ;[179f] 23
                    inc       c                             ;[17a0] 0c
                    cp        $20                           ;[17a1] fe 20
                    jr        nc,$17b0                      ;[17a3] 30 0b
                    call      $16c0                         ;[17a5] cd c0 16
                    ld        a,(hl)                        ;[17a8] 7e
                    inc       hl                            ;[17a9] 23
                    inc       c                             ;[17aa] 0c
                    call      $16c0                         ;[17ab] cd c0 16
                    jr        $179e                         ;[17ae] 18 ee
                    call      $16c0                         ;[17b0] cd c0 16
                    djnz      $179e                         ;[17b3] 10 e9
                    jr        $1833                         ;[17b5] 18 7c
                    ld        c,(ix+$29)                    ;[17b7] dd 4e 29
                    ld        b,(ix+$2a)                    ;[17ba] dd 46 2a
                    ld        a,c                           ;[17bd] 79
                    and       a                             ;[17be] a7
                    ret       z                             ;[17bf] c8
                    call      $1695                         ;[17c0] cd 95 16
                    ld        a,(ix+$19)                    ;[17c3] dd 7e 19
                    and       $03                           ;[17c6] e6 03
                    jr        z,$17fa                       ;[17c8] 28 30
                    call      $16b5                         ;[17ca] cd b5 16
                    sub       b                             ;[17cd] 90
                    ld        e,a                           ;[17ce] 5f
                    ld        a,(ix+$19)                    ;[17cf] dd 7e 19
                    and       $02                           ;[17d2] e6 02
                    jr        nz,$17e6                      ;[17d4] 20 10
                    ld        a,e                           ;[17d6] 7b
                    srl       a                             ;[17d7] cb 3f
                    and       a                             ;[17d9] a7
                    jr        z,$17fa                       ;[17da] 28 1e
                    push      af                            ;[17dc] f5
                    ld        a,$20                         ;[17dd] 3e 20
                    call      $16c0                         ;[17df] cd c0 16
                    pop       af                            ;[17e2] f1
                    dec       a                             ;[17e3] 3d
                    jr        $17d9                         ;[17e4] 18 f3
                    push      bc                            ;[17e6] c5
                    push      hl                            ;[17e7] e5
                    ld        d,$00                         ;[17e8] 16 00
                    ld        a,(hl)                        ;[17ea] 7e
                    inc       hl                            ;[17eb] 23
                    cp        $20                           ;[17ec] fe 20
                    jr        nz,$17f1                      ;[17ee] 20 01
                    inc       d                             ;[17f0] 14
                    jr        nc,$17f5                      ;[17f1] 30 02
                    inc       hl                            ;[17f3] 23
                    dec       c                             ;[17f4] 0d
                    dec       c                             ;[17f5] 0d
                    jr        nz,$17ea                      ;[17f6] 20 f2
                    pop       hl                            ;[17f8] e1
                    pop       bc                            ;[17f9] c1
                    ld        b,$00                         ;[17fa] 06 00
                    push      bc                            ;[17fc] c5
                    ld        a,(hl)                        ;[17fd] 7e
                    call      $16c0                         ;[17fe] cd c0 16
                    cp        $20                           ;[1801] fe 20
                    jr        nc,$180d                      ;[1803] 30 08
                    inc       hl                            ;[1805] 23
                    dec       c                             ;[1806] 0d
                    ld        a,(hl)                        ;[1807] 7e
                    call      $16c0                         ;[1808] cd c0 16
                    jr        $182e                         ;[180b] 18 21
                    jr        nz,$182e                      ;[180d] 20 1f
                    ld        a,(ix+$19)                    ;[180f] dd 7e 19
                    and       $02                           ;[1812] e6 02
                    jr        z,$182e                       ;[1814] 28 18
                    ld        a,e                           ;[1816] 7b
                    ld        b,$00                         ;[1817] 06 00
                    sub       d                             ;[1819] 92
                    jr        c,$181f                       ;[181a] 38 03
                    inc       b                             ;[181c] 04
                    jr        $1819                         ;[181d] 18 fa
                    dec       d                             ;[181f] 15
                    ld        a,b                           ;[1820] 78
                    and       a                             ;[1821] a7
                    jr        z,$182e                       ;[1822] 28 0a
                    ld        a,e                           ;[1824] 7b
                    sub       b                             ;[1825] 90
                    ld        e,a                           ;[1826] 5f
                    ld        a,$20                         ;[1827] 3e 20
                    call      $16c0                         ;[1829] cd c0 16
                    djnz      $1827                         ;[182c] 10 f9
                    inc       hl                            ;[182e] 23
                    dec       c                             ;[182f] 0d
                    jr        nz,$17fd                      ;[1830] 20 cb
                    pop       bc                            ;[1832] c1
                    push      bc                            ;[1833] c5
                    call      $2043                         ;[1834] cd 43 20
                    pop       bc                            ;[1837] c1
                    call      $1695                         ;[1838] cd 95 16
                    add       hl,bc                         ;[183b] 09
                    ld        a,(ix+$29)                    ;[183c] dd 7e 29
                    sub       c                             ;[183f] 91
                    ld        b,a                           ;[1840] 47
                    push      af                            ;[1841] f5
                    call      $18a8                         ;[1842] cd a8 18
                    pop       af                            ;[1845] f1
                    ret       z                             ;[1846] c8
                    ld        a,(hl)                        ;[1847] 7e
                    cp        $20                           ;[1848] fe 20
                    jr        nz,$1850                      ;[184a] 20 04
                    inc       hl                            ;[184c] 23
                    djnz      $1847                         ;[184d] 10 f8
                    ret                                     ;[184f] c9

                    push      bc                            ;[1850] c5
                    push      hl                            ;[1851] e5
                    ld        e,(hl)                        ;[1852] 5e
                    ld        a,e                           ;[1853] 7b
                    cp        $20                           ;[1854] fe 20
                    jr        c,$1861                       ;[1856] 38 09
                    call      $1740                         ;[1858] cd 40 17
                    pop       hl                            ;[185b] e1
                    pop       bc                            ;[185c] c1
                    inc       hl                            ;[185d] 23
                    djnz      $1850                         ;[185e] 10 f0
                    ret                                     ;[1860] c9

                    call      $175c                         ;[1861] cd 5c 17
                    pop       hl                            ;[1864] e1
                    pop       bc                            ;[1865] c1
                    inc       hl                            ;[1866] 23
                    dec       b                             ;[1867] 05
                    ret       z                             ;[1868] c8
                    push      bc                            ;[1869] c5
                    push      hl                            ;[186a] e5
                    ld        e,(hl)                        ;[186b] 5e
                    call      $175c                         ;[186c] cd 5c 17
                    jr        $185b                         ;[186f] 18 ea
                    bit       4,(ix+$19)                    ;[1871] dd cb 19 66
                    ret       z                             ;[1875] c8
                    ld        a,(ix+$0f)                    ;[1876] dd 7e 0f
                    inc       a                             ;[1879] 3c
                    cp        $09                           ;[187a] fe 09
                    ret       nc                            ;[187c] d0
                    jp        $16d7                         ;[187d] c3 d7 16
                    bit       4,(ix+$19)                    ;[1880] dd cb 19 66
                    jr        z,$188f                       ;[1884] 28 09
                    ld        a,(ix+$0f)                    ;[1886] dd 7e 0f
                    dec       a                             ;[1889] 3d
                    cp        $03                           ;[188a] fe 03
                    jr        nc,$187d                      ;[188c] 30 ef
                    ret                                     ;[188e] c9

                    ld        a,(ix+$24)                    ;[188f] dd 7e 24
                    and       a                             ;[1892] a7
                    call      nz,$2043                      ;[1893] c4 43 20
                    call      $169d                         ;[1896] cd 9d 16
                    call      $19e7                         ;[1899] cd e7 19
                    set       5,(ix+$19)                    ;[189c] dd cb 19 ee
                    res       6,(ix+$25)                    ;[18a0] dd cb 25 b6
                    res       7,(ix+$25)                    ;[18a4] dd cb 25 be
                    xor       a                             ;[18a8] af
                    ld        (ix+$29),a                    ;[18a9] dd 77 29
                    ld        (ix+$2a),a                    ;[18ac] dd 77 2a
                    ld        (ix+$2c),a                    ;[18af] dd 77 2c
                    ld        (ix+$2b),a                    ;[18b2] dd 77 2b
                    ret                                     ;[18b5] c9

                    ld        a,(ix+$29)                    ;[18b6] dd 7e 29
                    push      af                            ;[18b9] f5
                    ld        a,(ix+$19)                    ;[18ba] dd 7e 19
                    push      af                            ;[18bd] f5
                    res       1,(ix+$19)                    ;[18be] dd cb 19 8e
                    call      $1779                         ;[18c2] cd 79 17
                    pop       af                            ;[18c5] f1
                    ld        (ix+$19),a                    ;[18c6] dd 77 19
                    ld        a,(ix+$29)                    ;[18c9] dd 7e 29
                    and       a                             ;[18cc] a7
                    jr        nz,$18ba                      ;[18cd] 20 eb
                    pop       af                            ;[18cf] f1
                    and       a                             ;[18d0] a7
                    ret       nz                            ;[18d1] c0
                    call      $1aa2                         ;[18d2] cd a2 1a
                    ret       c                             ;[18d5] d8
                    jp        $2071                         ;[18d6] c3 71 20
                    call      $18ef                         ;[18d9] cd ef 18
                    ret       nz                            ;[18dc] c0
                    ld        d,(ix+$2a)                    ;[18dd] dd 56 2a
                    call      $16b5                         ;[18e0] cd b5 16
                    cp        d                             ;[18e3] ba
                    ret       c                             ;[18e4] d8
                    ld        (ix+$2c),d                    ;[18e5] dd 72 2c
                    ld        a,(ix+$29)                    ;[18e8] dd 7e 29
                    ld        (ix+$2b),a                    ;[18eb] dd 77 2b
                    ret                                     ;[18ee] c9

                    cp        $2c                           ;[18ef] fe 2c
                    ret       z                             ;[18f1] c8
                    cp        $2e                           ;[18f2] fe 2e
                    ret       z                             ;[18f4] c8
                    cp        $21                           ;[18f5] fe 21
                    ret       z                             ;[18f7] c8
                    cp        $3f                           ;[18f8] fe 3f
                    ret       z                             ;[18fa] c8
                    cp        $3b                           ;[18fb] fe 3b
                    ret       z                             ;[18fd] c8
                    cp        $3a                           ;[18fe] fe 3a
                    ret                                     ;[1900] c9

                    bit       4,(ix+$19)                    ;[1901] dd cb 19 66
                    jr        nz,$1914                      ;[1905] 20 0d
                    res       7,(ix+$19)                    ;[1907] dd cb 19 be
                    rra                                     ;[190b] 1f
                    ret       nc                            ;[190c] d0
                    set       7,(ix+$19)                    ;[190d] dd cb 19 fe
                    ret                                     ;[1911] c9

                    ld        a,$08                         ;[1912] 3e 08
                    call      $22ec                         ;[1914] cd ec 22
                    ex        de,hl                         ;[1917] eb
                    ld        hl,($5c36)                    ;[1918] 2a 36 5c
                    inc       d                             ;[191b] 14
                    inc       h                             ;[191c] 24
                    ld        bc,$0300                      ;[191d] 01 00 03
                    ld        a,h                           ;[1920] 7c
                    cp        $bd                           ;[1921] fe bd
                    jr        nc,$1947                      ;[1923] 30 22
                    and       $c0                           ;[1925] e6 c0
                    jr        z,$192c                       ;[1927] 28 03
                    ldir                                    ;[1929] ed b0
                    ret                                     ;[192b] c9

                    push      hl                            ;[192c] e5
                    push      de                            ;[192d] d5
                    push      bc                            ;[192e] c5
                    ld        d,$54                         ;[192f] 16 54
                    call      $27de                         ;[1931] cd de 27
                    pop       bc                            ;[1934] c1
                    pop       de                            ;[1935] d1
                    ex        (sp),hl                       ;[1936] e3
                    res       6,d                           ;[1937] cb b2
                    rst       $28                           ;[1939] ef
                    jp        $1633                         ;[193a] c3 33 16
                    ld        d,h                           ;[193d] 54
                    pop       hl                            ;[193e] e1
                    call      $27e1                         ;[193f] cd e1 27
                    ld        d,$56                         ;[1942] 16 56
                    jp        $27de                         ;[1944] c3 de 27
                    push      bc                            ;[1947] c5
                    push      de                            ;[1948] d5
                    push      hl                            ;[1949] e5
                    call      $27d7                         ;[194a] cd d7 27
                    ex        (sp),hl                       ;[194d] e3
                    ld        a,(hl)                        ;[194e] 7e
                    inc       hl                            ;[194f] 23
                    ex        (sp),hl                       ;[1950] e3
                    push      af                            ;[1951] f5
                    dec       d                             ;[1952] 15
                    call      $27e1                         ;[1953] cd e1 27
                    pop       af                            ;[1956] f1
                    pop       hl                            ;[1957] e1
                    pop       de                            ;[1958] d1
                    pop       bc                            ;[1959] c1
                    ld        (de),a                        ;[195a] 12
                    inc       de                            ;[195b] 13
                    dec       bc                            ;[195c] 0b
                    ld        a,b                           ;[195d] 78
                    or        c                             ;[195e] b1
                    jr        nz,$1947                      ;[195f] 20 e6
                    ret                                     ;[1961] c9

                    ld        d,$54                         ;[1962] 16 54
                    call      $27de                         ;[1964] cd de 27
                    push      hl                            ;[1967] e5
                    call      $23bf                         ;[1968] cd bf 23
                    jr        $193c                         ;[196b] 18 cf
                    bit       4,(ix+$19)                    ;[196d] dd cb 19 66
                    jr        nz,$1912                      ;[1971] 20 9f
                    bit       3,(ix+$25)                    ;[1973] dd cb 25 5e
                    jr        nz,$198d                      ;[1977] 20 14
                    call      $1a0b                         ;[1979] cd 0b 1a
                    push      ix                            ;[197c] dd e5
                    pop       hl                            ;[197e] e1
                    ld        e,(ix+$0b)                    ;[197f] dd 5e 0b
                    ld        d,(ix+$0c)                    ;[1982] dd 56 0c
                    add       hl,de                         ;[1985] 19
                    call      $19e7                         ;[1986] cd e7 19
                    set       3,(ix+$25)                    ;[1989] dd cb 25 de
                    ld        (iy+$58),$80                  ;[198d] fd 36 58 80
                    ld        de,$21a2                      ;[1991] 11 a2 21
                    push      ix                            ;[1994] dd e5
                    pop       hl                            ;[1996] e1
                    ld        c,(ix+$0b)                    ;[1997] dd 4e 0b
                    ld        b,(ix+$0c)                    ;[199a] dd 46 0c
                    add       hl,bc                         ;[199d] 09
                    push      hl                            ;[199e] e5
                    call      $1a0b                         ;[199f] cd 0b 1a
                    pop       hl                            ;[19a2] e1
                    and       a                             ;[19a3] a7
                    sbc       hl,bc                         ;[19a4] ed 42
                    ld        ($5b9b),hl                    ;[19a6] 22 9b 5b
                    ld        ($f358),hl                    ;[19a9] 22 58 f3
                    ex        de,hl                         ;[19ac] eb
                    jp        $20e1                         ;[19ad] c3 e1 20
                    bit       4,(ix+$19)                    ;[19b0] dd cb 19 66
                    jr        nz,$1962                      ;[19b4] 20 ac
                    bit       3,(ix+$25)                    ;[19b6] dd cb 25 5e
                    ret       z                             ;[19ba] c8
                    ld        (iy+$58),$00                  ;[19bb] fd 36 58 00
                    call      $1991                         ;[19bf] cd 91 19
                    call      $1a0b                         ;[19c2] cd 0b 1a
                    ld        hl,($f358)                    ;[19c5] 2a 58 f3
                    res       3,(ix+$25)                    ;[19c8] dd cb 25 9e
                    push      hl                            ;[19cc] e5
                    ld        l,(ix+$0b)                    ;[19cd] dd 6e 0b
                    ld        h,(ix+$0c)                    ;[19d0] dd 66 0c
                    and       a                             ;[19d3] a7
                    sbc       hl,bc                         ;[19d4] ed 42
                    ld        (ix+$0b),l                    ;[19d6] dd 75 0b
                    ld        (ix+$0c),h                    ;[19d9] dd 74 0c
                    pop       hl                            ;[19dc] e1
                    rst       $30                           ;[19dd] f7
                    call      $053f                         ;[19de] cd 3f 05
                    call      $1028                         ;[19e1] cd 28 10
                    jp        $1942                         ;[19e4] c3 42 19
                    push      hl                            ;[19e7] e5
                    ld        l,(ix+$0b)                    ;[19e8] dd 6e 0b
                    ld        h,(ix+$0c)                    ;[19eb] dd 66 0c
                    add       hl,bc                         ;[19ee] 09
                    ld        (ix+$0b),l                    ;[19ef] dd 75 0b
                    ld        (ix+$0c),h                    ;[19f2] dd 74 0c
                    pop       hl                            ;[19f5] e1
                    rst       $30                           ;[19f6] f7
                    call      $0577                         ;[19f7] cd 77 05
                    call      $1028                         ;[19fa] cd 28 10
                    jr        $19e4                         ;[19fd] 18 e5
                    call      $18b6                         ;[19ff] cd b6 18
                    call      $169d                         ;[1a02] cd 9d 16
                    res       5,(ix+$19)                    ;[1a05] dd cb 19 ae
                    jr        $19cc                         ;[1a09] 18 c1
                    ld        hl,$0000                      ;[1a0b] 21 00 00
                    ld        c,(ix+$11)                    ;[1a0e] dd 4e 11
                    ld        b,h                           ;[1a11] 44
                    ld        a,(ix+$12)                    ;[1a12] dd 7e 12
                    add       hl,bc                         ;[1a15] 09
                    dec       a                             ;[1a16] 3d
                    jr        nz,$1a15                      ;[1a17] 20 fc
                    ld        c,l                           ;[1a19] 4d
                    ld        b,h                           ;[1a1a] 44
                    add       hl,hl                         ;[1a1b] 29
                    add       hl,hl                         ;[1a1c] 29
                    add       hl,hl                         ;[1a1d] 29
                    call      $271d                         ;[1a1e] cd 1d 27
                    jr        c,$1a2b                       ;[1a21] 38 08
                    jr        z,$1a27                       ;[1a23] 28 02
                    ld        b,h                           ;[1a25] 44
                    ld        c,l                           ;[1a26] 4d
                    add       hl,bc                         ;[1a27] 09
                    ld        b,h                           ;[1a28] 44
                    ld        c,l                           ;[1a29] 4d
                    ret                                     ;[1a2a] c9

                    add       hl,hl                         ;[1a2b] 29
                    add       hl,hl                         ;[1a2c] 29
                    add       hl,hl                         ;[1a2d] 29
                    jr        $1a28                         ;[1a2e] 18 f8
                    ld        a,(ix+$17)                    ;[1a30] dd 7e 17
                    ld        (ix+$22),a                    ;[1a33] dd 77 22
                    ld        a,(ix+$13)                    ;[1a36] dd 7e 13
                    ld        (ix+$21),a                    ;[1a39] dd 77 21
                    ld        (ix+$23),$00                  ;[1a3c] dd 36 23 00
                    ld        (ix+$24),$00                  ;[1a40] dd 36 24 00
                    ret                                     ;[1a44] c9

                    ld        a,(ix+$18)                    ;[1a45] dd 7e 18
                    jr        $1a33                         ;[1a48] 18 e9
                    ld        a,(ix+$24)                    ;[1a4a] dd 7e 24
                    and       a                             ;[1a4d] a7
                    ret       z                             ;[1a4e] c8
                    dec       (ix+$24)                      ;[1a4f] dd 35 24
                    ld        a,(ix+$23)                    ;[1a52] dd 7e 23
                    sub       (ix+$0f)                      ;[1a55] dd 96 0f
                    ld        (ix+$23),a                    ;[1a58] dd 77 23
                    ret       nc                            ;[1a5b] d0
                    add       (ix+$1d)                      ;[1a5c] dd 86 1d
                    ld        (ix+$23),a                    ;[1a5f] dd 77 23
                    dec       (ix+$21)                      ;[1a62] dd 35 21
                    ret                                     ;[1a65] c9

                    ld        a,(ix+$24)                    ;[1a66] dd 7e 24
                    inc       a                             ;[1a69] 3c
                    cp        (ix+$1c)                      ;[1a6a] dd be 1c
                    ret       nc                            ;[1a6d] d0
                    ld        (ix+$24),a                    ;[1a6e] dd 77 24
                    ld        a,(ix+$23)                    ;[1a71] dd 7e 23
                    add       (ix+$0f)                      ;[1a74] dd 86 0f
                    ld        (ix+$23),a                    ;[1a77] dd 77 23
                    cp        (ix+$1d)                      ;[1a7a] dd be 1d
                    ret       c                             ;[1a7d] d8
                    sub       (ix+$1d)                      ;[1a7e] dd 96 1d
                    ld        (ix+$23),a                    ;[1a81] dd 77 23
                    inc       (ix+$21)                      ;[1a84] dd 34 21
                    ret                                     ;[1a87] c9

                    ld        a,(ix+$22)                    ;[1a88] dd 7e 22
                    add       $08                           ;[1a8b] c6 08
                    bit       0,(ix+$25)                    ;[1a8d] dd cb 25 46
                    ret       z                             ;[1a91] c8
                    dec       a                             ;[1a92] 3d
                    dec       a                             ;[1a93] 3d
                    ret                                     ;[1a94] c9

                    ld        a,(ix+$22)                    ;[1a95] dd 7e 22
                    sub       $08                           ;[1a98] d6 08
                    bit       0,(ix+$25)                    ;[1a9a] dd cb 25 46
                    ret       z                             ;[1a9e] c8
                    inc       a                             ;[1a9f] 3c
                    inc       a                             ;[1aa0] 3c
                    ret                                     ;[1aa1] c9

                    ld        a,(ix+$16)                    ;[1aa2] dd 7e 16
                    add       a                             ;[1aa5] 87
                    add       a                             ;[1aa6] 87
                    add       a                             ;[1aa7] 87
                    inc       a                             ;[1aa8] 3c
                    ld        b,a                           ;[1aa9] 47
                    ld        a,(ix+$22)                    ;[1aaa] dd 7e 22
                    add       $08                           ;[1aad] c6 08
                    bit       0,(ix+$25)                    ;[1aaf] dd cb 25 46
                    jr        z,$1ab9                       ;[1ab3] 28 04
                    dec       a                             ;[1ab5] 3d
                    dec       a                             ;[1ab6] 3d
                    inc       b                             ;[1ab7] 04
                    inc       b                             ;[1ab8] 04
                    cp        b                             ;[1ab9] b8
                    ret       nc                            ;[1aba] d0
                    ld        (ix+$22),a                    ;[1abb] dd 77 22
                    ret                                     ;[1abe] c9

                    call      $1a95                         ;[1abf] cd 95 1a
                    ret       c                             ;[1ac2] d8
                    cp        (ix+$17)                      ;[1ac3] dd be 17
                    ret       c                             ;[1ac6] d8
                    ld        (ix+$22),a                    ;[1ac7] dd 77 22
                    ret                                     ;[1aca] c9

                    ld        e,$22                         ;[1acb] 1e 22
                    jr        $1aee                         ;[1acd] 18 1f
                    ld        e,$23                         ;[1acf] 1e 23
                    ld        (ix+$2b),a                    ;[1ad1] dd 77 2b
                    jr        $1af1                         ;[1ad4] 18 1b
                    ld        e,$21                         ;[1ad6] 1e 21
                    jr        $1aee                         ;[1ad8] 18 14
                    ld        e,$20                         ;[1ada] 1e 20
                    ld        d,$ff                         ;[1adc] 16 ff
                    cp        e                             ;[1ade] bb
                    jr        nc,$1aed                      ;[1adf] 30 0c
                    add       a                             ;[1ae1] 87
                    ld        c,a                           ;[1ae2] 4f
                    add       a                             ;[1ae3] 87
                    bit       0,(ix+$25)                    ;[1ae4] dd cb 25 46
                    jr        nz,$1aeb                      ;[1ae8] 20 01
                    ld        c,a                           ;[1aea] 4f
                    add       c                             ;[1aeb] 81
                    ld        d,a                           ;[1aec] 57
                    ld        a,d                           ;[1aed] 7a
                    ld        (ix+$2d),a                    ;[1aee] dd 77 2d
                    ld        (ix+$26),e                    ;[1af1] dd 73 26
                    ret                                     ;[1af4] c9

                    ld        h,a                           ;[1af5] 67
                    ld        l,(ix+$2b)                    ;[1af6] dd 6e 2b
                    push      hl                            ;[1af9] e5
                    ld        d,$00                         ;[1afa] 16 00
                    ld        e,(ix+$0f)                    ;[1afc] dd 5e 0f
                    add       hl,de                         ;[1aff] 19
                    dec       hl                            ;[1b00] 2b
                    ld        a,h                           ;[1b01] 7c
                    ld        c,l                           ;[1b02] 4d
                    ld        h,d                           ;[1b03] 62
                    ld        l,d                           ;[1b04] 6a
                    ld        b,$10                         ;[1b05] 06 10
                    rl        c                             ;[1b07] cb 11
                    rla                                     ;[1b09] 17
                    adc       hl,hl                         ;[1b0a] ed 6a
                    sbc       hl,de                         ;[1b0c] ed 52
                    jr        nc,$1b11                      ;[1b0e] 30 01
                    add       hl,de                         ;[1b10] 19
                    djnz      $1b07                         ;[1b11] 10 f4
                    rl        c                             ;[1b13] cb 11
                    rla                                     ;[1b15] 17
                    cpl                                     ;[1b16] 2f
                    and       a                             ;[1b17] a7
                    jr        nz,$1b7e                      ;[1b18] 20 64
                    ld        a,c                           ;[1b1a] 79
                    cpl                                     ;[1b1b] 2f
                    pop       de                            ;[1b1c] d1
                    push      af                            ;[1b1d] f5
                    bit       4,(ix+$1d)                    ;[1b1e] dd cb 1d 66
                    ld        h,$00                         ;[1b22] 26 00
                    jr        z,$1b38                       ;[1b24] 28 12
                    bit       3,e                           ;[1b26] cb 5b
                    jr        z,$1b2c                       ;[1b28] 28 02
                    ld        h,$08                         ;[1b2a] 26 08
                    ld        a,e                           ;[1b2c] 7b
                    and       $07                           ;[1b2d] e6 07
                    ld        l,a                           ;[1b2f] 6f
                    ld        a,d                           ;[1b30] 7a
                    rra                                     ;[1b31] 1f
                    ld        a,e                           ;[1b32] 7b
                    rra                                     ;[1b33] 1f
                    and       $f8                           ;[1b34] e6 f8
                    or        l                             ;[1b36] b5
                    ld        e,a                           ;[1b37] 5f
                    ld        a,e                           ;[1b38] 7b
                    and       $07                           ;[1b39] e6 07
                    add       h                             ;[1b3b] 84
                    ld        h,a                           ;[1b3c] 67
                    ld        a,e                           ;[1b3d] 7b
                    rra                                     ;[1b3e] 1f
                    rra                                     ;[1b3f] 1f
                    rra                                     ;[1b40] 1f
                    and       $1f                           ;[1b41] e6 1f
                    add       (ix+$13)                      ;[1b43] dd 86 13
                    ld        l,a                           ;[1b46] 6f
                    pop       de                            ;[1b47] d1
                    jr        $1b62                         ;[1b48] 18 18
                    ld        d,a                           ;[1b4a] 57
                    ld        b,a                           ;[1b4b] 47
                    inc       b                             ;[1b4c] 04
                    ld        l,(ix+$13)                    ;[1b4d] dd 6e 13
                    xor       a                             ;[1b50] af
                    ld        e,(ix+$1d)                    ;[1b51] dd 5e 1d
                    dec       b                             ;[1b54] 05
                    jr        z,$1b61                       ;[1b55] 28 0a
                    add       (ix+$0f)                      ;[1b57] dd 86 0f
                    cp        e                             ;[1b5a] bb
                    jr        c,$1b54                       ;[1b5b] 38 f7
                    sub       e                             ;[1b5d] 93
                    inc       l                             ;[1b5e] 2c
                    jr        $1b54                         ;[1b5f] 18 f3
                    ld        h,a                           ;[1b61] 67
                    ld        a,d                           ;[1b62] 7a
                    cp        (ix+$1c)                      ;[1b63] dd be 1c
                    jr        nc,$1b7e                      ;[1b66] 30 16
                    ld        (ix+$24),a                    ;[1b68] dd 77 24
                    ld        (ix+$21),l                    ;[1b6b] dd 75 21
                    ld        (ix+$23),h                    ;[1b6e] dd 74 23
                    ld        a,(ix+$2d)                    ;[1b71] dd 7e 2d
                    add       (ix+$17)                      ;[1b74] dd 86 17
                    jr        c,$1b7e                       ;[1b77] 38 05
                    cp        (ix+$18)                      ;[1b79] dd be 18
                    jr        c,$1b83                       ;[1b7c] 38 05
                    ld        a,$04                         ;[1b7e] 3e 04
                    jp        $27d3                         ;[1b80] c3 d3 27
                    ld        (ix+$22),a                    ;[1b83] dd 77 22
                    ret                                     ;[1b86] c9

                    ld        a,(ix+$22)                    ;[1b87] dd 7e 22
                    ld        b,(ix+$17)                    ;[1b8a] dd 46 17
                    sub       b                             ;[1b8d] 90
                    ld        (ix+$2d),a                    ;[1b8e] dd 77 2d
                    ld        a,(ix+$24)                    ;[1b91] dd 7e 24
                    and       a                             ;[1b94] a7
                    jr        nz,$1ba2                      ;[1b95] 20 0b
                    call      $1a95                         ;[1b97] cd 95 1a
                    sub       b                             ;[1b9a] 90
                    ret       c                             ;[1b9b] d8
                    ld        (ix+$2d),a                    ;[1b9c] dd 77 2d
                    ld        a,(ix+$1c)                    ;[1b9f] dd 7e 1c
                    dec       a                             ;[1ba2] 3d
                    push      af                            ;[1ba3] f5
                    call      $1b4a                         ;[1ba4] cd 4a 1b
                    call      $1bad                         ;[1ba7] cd ad 1b
                    pop       af                            ;[1baa] f1
                    jr        $1b4a                         ;[1bab] 18 9d
                    push      af                            ;[1bad] f5
                    ld        a,(ix+$25)                    ;[1bae] dd 7e 25
                    push      af                            ;[1bb1] f5
                    res       4,(ix+$25)                    ;[1bb2] dd cb 25 a6
                    ld        a,$20                         ;[1bb6] 3e 20
                    call      $1d6b                         ;[1bb8] cd 6b 1d
                    pop       af                            ;[1bbb] f1
                    ld        (ix+$25),a                    ;[1bbc] dd 77 25
                    pop       af                            ;[1bbf] f1
                    ret                                     ;[1bc0] c9

                    ld        l,(ix+$2d)                    ;[1bc1] dd 6e 2d
                    ld        h,a                           ;[1bc4] 67
                    ld        c,(ix+$1c)                    ;[1bc5] dd 4e 1c
                    ld        b,$00                         ;[1bc8] 06 00
                    and       a                             ;[1bca] a7
                    sbc       hl,bc                         ;[1bcb] ed 42
                    jr        nc,$1bcb                      ;[1bcd] 30 fc
                    add       hl,bc                         ;[1bcf] 09
                    ld        a,l                           ;[1bd0] 7d
                    ld        b,(ix+$24)                    ;[1bd1] dd 46 24
                    sub       b                             ;[1bd4] 90
                    jr        nc,$1bd8                      ;[1bd5] 30 01
                    add       c                             ;[1bd7] 81
                    and       a                             ;[1bd8] a7
                    ret       z                             ;[1bd9] c8
                    call      $1bad                         ;[1bda] cd ad 1b
                    dec       a                             ;[1bdd] 3d
                    jr        $1bd8                         ;[1bde] 18 f8
                    ld        a,(ix+$1c)                    ;[1be0] dd 7e 1c
                    srl       a                             ;[1be3] cb 3f
                    cp        (ix+$24)                      ;[1be5] dd be 24
                    ld        l,a                           ;[1be8] 6f
                    ld        h,$00                         ;[1be9] 26 00
                    jr        nc,$1bc5                      ;[1beb] 30 d8
                    ld        l,h                           ;[1bed] 6c
                    jr        $1bc5                         ;[1bee] 18 d5
                    call      $271c                         ;[1bf0] cd 1c 27
                    jr        c,$1c20                       ;[1bf3] 38 2b
                    dec       a                             ;[1bf5] 3d
                    jr        z,$1c40                       ;[1bf6] 28 48
                    ld        a,($5b64)                     ;[1bf8] 3a 64 5b
                    and       a                             ;[1bfb] a7
                    jr        nz,$1c07                      ;[1bfc] 20 09
                    ld        a,e                           ;[1bfe] 7b
                    cp        $08                           ;[1bff] fe 08
                    jr        nc,$1c75                      ;[1c01] 30 72
                    ld        d,$f8                         ;[1c03] 16 f8
                    jr        $1c0c                         ;[1c05] 18 05
                    cpl                                     ;[1c07] 2f
                    ld        d,a                           ;[1c08] 57
                    and       e                             ;[1c09] a3
                    jr        nz,$1c75                      ;[1c0a] 20 69
                    ld        a,(ix+$1f)                    ;[1c0c] dd 7e 1f
                    and       d                             ;[1c0f] a2
                    or        e                             ;[1c10] b3
                    ld        (ix+$1f),a                    ;[1c11] dd 77 1f
                    ld        a,$61                         ;[1c14] 3e 61
                    bit       3,(iy+$45)                    ;[1c16] fd cb 45 5e
                    jr        z,$1c27                       ;[1c1a] 28 0b
                    ld        a,$63                         ;[1c1c] 3e 63
                    jr        $1c27                         ;[1c1e] 18 07
                    ld        (ix+$1f),e                    ;[1c20] dd 73 1f
                    ld        d,$00                         ;[1c23] 16 00
                    add       $5e                           ;[1c25] c6 5e
                    ld        hl,$5c6c                      ;[1c27] 21 6c 5c
                    ld        b,a                           ;[1c2a] 47
                    ld        a,(hl)                        ;[1c2b] 7e
                    and       d                             ;[1c2c] a2
                    or        e                             ;[1c2d] b3
                    ld        (hl),a                        ;[1c2e] 77
                    bit       5,(iy+$45)                    ;[1c2f] fd cb 45 6e
                    ret       z                             ;[1c33] c8
                    ld        l,b                           ;[1c34] 68
                    ld        h,$5b                         ;[1c35] 26 5b
                    ld        a,(hl)                        ;[1c37] 7e
                    and       d                             ;[1c38] a2
                    or        e                             ;[1c39] b3
                    ld        (hl),a                        ;[1c3a] 77
                    ret                                     ;[1c3b] c9

                    ld        a,e                           ;[1c3c] 7b
                    xor       $07                           ;[1c3d] ee 07
                    ld        e,a                           ;[1c3f] 5f
                    ld        a,e                           ;[1c40] 7b
                    cp        $08                           ;[1c41] fe 08
                    jr        nc,$1c75                      ;[1c43] 30 30
                    add       a                             ;[1c45] 87
                    add       a                             ;[1c46] 87
                    add       a                             ;[1c47] 87
                    ld        e,a                           ;[1c48] 5f
                    ld        d,$00                         ;[1c49] 16 00
                    or        $06                           ;[1c4b] f6 06
                    out       ($ff),a                       ;[1c4d] d3 ff
                    ld        a,$62                         ;[1c4f] 3e 62
                    jr        $1c27                         ;[1c51] 18 d4
                    call      $271c                         ;[1c53] cd 1c 27
                    jr        c,$1c89                       ;[1c56] 38 31
                    dec       a                             ;[1c58] 3d
                    jr        z,$1c3c                       ;[1c59] 28 e1
                    ld        a,($5b64)                     ;[1c5b] 3a 64 5b
                    and       a                             ;[1c5e] a7
                    jr        nz,$1c6e                      ;[1c5f] 20 0d
                    ld        a,e                           ;[1c61] 7b
                    cp        $08                           ;[1c62] fe 08
                    jr        nc,$1c75                      ;[1c64] 30 0f
                    add       a                             ;[1c66] 87
                    add       a                             ;[1c67] 87
                    add       a                             ;[1c68] 87
                    ld        e,a                           ;[1c69] 5f
                    ld        d,$c7                         ;[1c6a] 16 c7
                    jr        $1c0c                         ;[1c6c] 18 9e
                    cpl                                     ;[1c6e] 2f
                    and       a                             ;[1c6f] a7
                    jr        nz,$1c7a                      ;[1c70] 20 08
                    inc       e                             ;[1c72] 1c
                    dec       e                             ;[1c73] 1d
                    ret       z                             ;[1c74] c8
                    ld        a,$13                         ;[1c75] 3e 13
                    jp        $27d3                         ;[1c77] c3 d3 27
                    rra                                     ;[1c7a] 1f
                    jr        c,$1c83                       ;[1c7b] 38 06
                    sla       e                             ;[1c7d] cb 23
                    jr        nc,$1c7a                      ;[1c7f] 30 f9
                    jr        $1c75                         ;[1c81] 18 f2
                    ld        a,($5b64)                     ;[1c83] 3a 64 5b
                    ld        d,a                           ;[1c86] 57
                    jr        $1c0c                         ;[1c87] 18 83
                    ld        d,$00                         ;[1c89] 16 00
                    ld        (ix+$20),e                    ;[1c8b] dd 73 20
                    ld        hl,$5c6d                      ;[1c8e] 21 6d 5c
                    dec       a                             ;[1c91] 3d
                    ld        a,$88                         ;[1c92] 3e 88
                    jr        z,$1c2a                       ;[1c94] 28 94
                    inc       a                             ;[1c96] 3c
                    jr        $1c2a                         ;[1c97] 18 91
                    call      $271c                         ;[1c99] cd 1c 27
                    ret       c                             ;[1c9c] d8
                    dec       a                             ;[1c9d] 3d
                    ret       z                             ;[1c9e] c8
                    ld        a,($5b64)                     ;[1c9f] 3a 64 5b
                    and       a                             ;[1ca2] a7
                    ret       nz                            ;[1ca3] c0
                    ld        a,e                           ;[1ca4] 7b
                    cp        $02                           ;[1ca5] fe 02
                    jr        nc,$1c75                      ;[1ca7] 30 cc
                    rrca                                    ;[1ca9] 0f
                    ld        e,a                           ;[1caa] 5f
                    ld        d,$7f                         ;[1cab] 16 7f
                    jp        $1c0c                         ;[1cad] c3 0c 1c
                    call      $271c                         ;[1cb0] cd 1c 27
                    ret       c                             ;[1cb3] d8
                    dec       a                             ;[1cb4] 3d
                    ret       z                             ;[1cb5] c8
                    ld        a,($5b64)                     ;[1cb6] 3a 64 5b
                    and       a                             ;[1cb9] a7
                    ret       nz                            ;[1cba] c0
                    ld        a,e                           ;[1cbb] 7b
                    cp        $02                           ;[1cbc] fe 02
                    jr        nc,$1c75                      ;[1cbe] 30 b5
                    rrca                                    ;[1cc0] 0f
                    rrca                                    ;[1cc1] 0f
                    ld        e,a                           ;[1cc2] 5f
                    ld        d,$bf                         ;[1cc3] 16 bf
                    jr        $1cad                         ;[1cc5] 18 e6
                    call      $271c                         ;[1cc7] cd 1c 27
                    ret       c                             ;[1cca] d8
                    dec       a                             ;[1ccb] 3d
                    ret       z                             ;[1ccc] c8
                    ld        d,$00                         ;[1ccd] 16 00
                    jr        $1cad                         ;[1ccf] 18 dc
                    cp        $02                           ;[1cd1] fe 02
                    jr        nc,$1c75                      ;[1cd3] 30 a0
                    rra                                     ;[1cd5] 1f
                    ld        a,$00                         ;[1cd6] 3e 00
                    sbc       a                             ;[1cd8] 9f
                    ret                                     ;[1cd9] c9

                    call      $1cd1                         ;[1cda] cd d1 1c
                    ld        (ix+$27),a                    ;[1cdd] dd 77 27
                    and       $0c                           ;[1ce0] e6 0c
                    ld        d,$f3                         ;[1ce2] 16 f3
                    bit       5,(iy+$45)                    ;[1ce4] fd cb 45 6e
                    jr        nz,$1cf0                      ;[1ce8] 20 06
                    and       $05                           ;[1cea] e6 05
                    set       3,d                           ;[1cec] cb da
                    set       1,d                           ;[1cee] cb ca
                    ld        e,a                           ;[1cf0] 5f
                    ld        a,($5c91)                     ;[1cf1] 3a 91 5c
                    and       d                             ;[1cf4] a2
                    or        e                             ;[1cf5] b3
                    ld        ($5c91),a                     ;[1cf6] 32 91 5c
                    ret                                     ;[1cf9] c9

                    call      $1cd1                         ;[1cfa] cd d1 1c
                    ld        (ix+$28),a                    ;[1cfd] dd 77 28
                    and       $03                           ;[1d00] e6 03
                    ld        d,$fc                         ;[1d02] 16 fc
                    jr        $1ce4                         ;[1d04] 18 de
                    ld        e,a                           ;[1d06] 5f
                    call      $1a30                         ;[1d07] cd 30 1a
                    ld        a,(ix+$12)                    ;[1d0a] dd 7e 12
                    ld        c,a                           ;[1d0d] 4f
                    ld        a,e                           ;[1d0e] 7b
                    ld        b,(ix+$1c)                    ;[1d0f] dd 46 1c
                    push      af                            ;[1d12] f5
                    push      bc                            ;[1d13] c5
                    call      $1d6b                         ;[1d14] cd 6b 1d
                    pop       bc                            ;[1d17] c1
                    pop       af                            ;[1d18] f1
                    djnz      $1d12                         ;[1d19] 10 f7
                    dec       c                             ;[1d1b] 0d
                    jr        nz,$1d0f                      ;[1d1c] 20 f1
                    ret                                     ;[1d1e] c9

                    res       4,(ix+$25)                    ;[1d1f] dd cb 25 a6
                    rra                                     ;[1d23] 1f
                    ret       nc                            ;[1d24] d0
                    set       4,(ix+$25)                    ;[1d25] dd cb 25 e6
                    ret                                     ;[1d29] c9

                    res       5,(ix+$25)                    ;[1d2a] dd cb 25 ae
                    rra                                     ;[1d2e] 1f
                    jr        nc,$1d35                      ;[1d2f] 30 04
                    set       5,(ix+$25)                    ;[1d31] dd cb 25 ee
                    res       0,(ix+$25)                    ;[1d35] dd cb 25 86
                    rra                                     ;[1d39] 1f
                    ret       nc                            ;[1d3a] d0
                    set       0,(ix+$25)                    ;[1d3b] dd cb 25 c6
                    ret                                     ;[1d3f] c9

                    bit       5,(ix+$19)                    ;[1d40] dd cb 19 6e
                    jp        nz,$1735                      ;[1d44] c2 35 17
                    ld        a,(ix+$26)                    ;[1d47] dd 7e 26
                    and       a                             ;[1d4a] a7
                    jr        nz,$1d5a                      ;[1d4b] 20 0d
                    ld        a,e                           ;[1d4d] 7b
                    cp        $20                           ;[1d4e] fe 20
                    jr        nc,$1d6b                      ;[1d50] 30 19
                    cp        $10                           ;[1d52] fe 10
                    jr        c,$1d5e                       ;[1d54] 38 08
                    ld        (ix+$26),a                    ;[1d56] dd 77 26
                    ret                                     ;[1d59] c9

                    ld        (ix+$26),$00                  ;[1d5a] dd 36 26 00
                    ld        hl,$1604                      ;[1d5e] 21 04 16
                    add       hl,a                          ;[1d61] ed 31
                    add       hl,a                          ;[1d63] ed 31
                    ld        c,(hl)                        ;[1d65] 4e
                    inc       hl                            ;[1d66] 23
                    ld        h,(hl)                        ;[1d67] 66
                    ld        l,c                           ;[1d68] 69
                    ld        a,e                           ;[1d69] 7b
                    jp        (hl)                          ;[1d6a] e9
                    ld        h,$00                         ;[1d6b] 26 00
                    ld        l,a                           ;[1d6d] 6f
                    add       hl,hl                         ;[1d6e] 29
                    add       hl,hl                         ;[1d6f] 29
                    add       hl,hl                         ;[1d70] 29
                    cp        $80                           ;[1d71] fe 80
                    jr        c,$1dad                       ;[1d73] 38 38
                    cp        $90                           ;[1d75] fe 90
                    jr        nc,$1d93                      ;[1d77] 30 1a
                    ld        e,a                           ;[1d79] 5f
                    ld        hl,$f350                      ;[1d7a] 21 50 f3
                    push      hl                            ;[1d7d] e5
                    ld        a,(ix+$0f)                    ;[1d7e] dd 7e 0f
                    ld        c,a                           ;[1d81] 4f
                    srl       c                             ;[1d82] cb 39
                    sub       c                             ;[1d84] 91
                    ld        b,a                           ;[1d85] 47
                    dec       b                             ;[1d86] 05
                    dec       c                             ;[1d87] 0d
                    push      bc                            ;[1d88] c5
                    call      $22d5                         ;[1d89] cd d5 22
                    pop       bc                            ;[1d8c] c1
                    call      $22d5                         ;[1d8d] cd d5 22
                    pop       hl                            ;[1d90] e1
                    jr        $1de6                         ;[1d91] 18 53
                    call      $2b62                         ;[1d93] cd 62 2b
                    ld        bc,$1d6b                      ;[1d96] 01 6b 1d
                    jp        c,$2b7f                       ;[1d99] da 7f 2b
                    res       1,(ix+$25)                    ;[1d9c] dd cb 25 8e
                    ld        de,($5c7b)                    ;[1da0] ed 5b 7b 5c
                    add       hl,de                         ;[1da4] 19
                    ld        de,$0480                      ;[1da5] 11 80 04
                    and       a                             ;[1da8] a7
                    sbc       hl,de                         ;[1da9] ed 52
                    jr        $1dc6                         ;[1dab] 18 19
                    res       1,(ix+$25)                    ;[1dad] dd cb 25 8e
                    cp        $20                           ;[1db1] fe 20
                    jr        nz,$1db9                      ;[1db3] 20 04
                    set       1,(ix+$25)                    ;[1db5] dd cb 25 ce
                    ld        c,(ix+$0d)                    ;[1db9] dd 4e 0d
                    ld        b,(ix+$0e)                    ;[1dbc] dd 46 0e
                    add       hl,bc                         ;[1dbf] 09
                    bit       2,(ix+$19)                    ;[1dc0] dd cb 19 56
                    jr        z,$1de6                       ;[1dc4] 28 20
                    ld        a,h                           ;[1dc6] 7c
                    cp        $bf                           ;[1dc7] fe bf
                    jr        c,$1de6                       ;[1dc9] 38 1b
                    sub       $40                           ;[1dcb] d6 40
                    ld        h,a                           ;[1dcd] 67
                    push      hl                            ;[1dce] e5
                    ld        d,$54                         ;[1dcf] 16 54
                    call      $27d9                         ;[1dd1] cd d9 27
                    ex        (sp),hl                       ;[1dd4] e3
                    ld        de,$f350                      ;[1dd5] 11 50 f3
                    ld        bc,$0008                      ;[1dd8] 01 08 00
                    ldir                                    ;[1ddb] ed b0
                    pop       hl                            ;[1ddd] e1
                    ld        d,$54                         ;[1dde] 16 54
                    call      $27e1                         ;[1de0] cd e1 27
                    ld        hl,$f350                      ;[1de3] 21 50 f3
                    ld        a,(ix+$24)                    ;[1de6] dd 7e 24
                    bit       4,(ix+$25)                    ;[1de9] dd cb 25 66
                    jr        z,$1df0                       ;[1ded] 28 01
                    inc       a                             ;[1def] 3c
                    cp        (ix+$1c)                      ;[1df0] dd be 1c
                    call      nc,$2043                      ;[1df3] d4 43 20
                    ld        a,(ix+$25)                    ;[1df6] dd 7e 25
                    and       $30                           ;[1df9] e6 30
                    jp        z,$1eac                       ;[1dfb] ca ac 1e
                    bit       4,a                           ;[1dfe] cb 67
                    jr        z,$1e6a                       ;[1e00] 28 68
                    ex        de,hl                         ;[1e02] eb
                    ld        hl,$f330                      ;[1e03] 21 30 f3
                    ld        b,$08                         ;[1e06] 06 08
                    push      bc                            ;[1e08] c5
                    ld        b,(ix+$0f)                    ;[1e09] dd 46 0f
                    srl       b                             ;[1e0c] cb 38
                    jr        c,$1e2b                       ;[1e0e] 38 1b
                    push      bc                            ;[1e10] c5
                    ld        (hl),$01                      ;[1e11] 36 01
                    ld        a,(de)                        ;[1e13] 1a
                    rla                                     ;[1e14] 17
                    push      af                            ;[1e15] f5
                    rl        (hl)                          ;[1e16] cb 16
                    pop       af                            ;[1e18] f1
                    rl        (hl)                          ;[1e19] cb 16
                    djnz      $1e14                         ;[1e1b] 10 f7
                    jr        c,$1e23                       ;[1e1d] 38 04
                    rl        (hl)                          ;[1e1f] cb 16
                    jr        nc,$1e1f                      ;[1e21] 30 fc
                    ld        bc,$0008                      ;[1e23] 01 08 00
                    add       hl,bc                         ;[1e26] 09
                    ld        (hl),$01                      ;[1e27] 36 01
                    jr        $1e47                         ;[1e29] 18 1c
                    push      bc                            ;[1e2b] c5
                    ld        (hl),$01                      ;[1e2c] 36 01
                    ld        a,(de)                        ;[1e2e] 1a
                    rla                                     ;[1e2f] 17
                    push      af                            ;[1e30] f5
                    rl        (hl)                          ;[1e31] cb 16
                    pop       af                            ;[1e33] f1
                    rl        (hl)                          ;[1e34] cb 16
                    djnz      $1e2f                         ;[1e36] 10 f7
                    rla                                     ;[1e38] 17
                    push      af                            ;[1e39] f5
                    rl        (hl)                          ;[1e3a] cb 16
                    jr        nc,$1e3a                      ;[1e3c] 30 fc
                    ld        bc,$0008                      ;[1e3e] 01 08 00
                    add       hl,bc                         ;[1e41] 09
                    ld        (hl),$01                      ;[1e42] 36 01
                    pop       af                            ;[1e44] f1
                    rl        (hl)                          ;[1e45] cb 16
                    pop       bc                            ;[1e47] c1
                    rla                                     ;[1e48] 17
                    push      af                            ;[1e49] f5
                    rl        (hl)                          ;[1e4a] cb 16
                    pop       af                            ;[1e4c] f1
                    rl        (hl)                          ;[1e4d] cb 16
                    djnz      $1e48                         ;[1e4f] 10 f7
                    jr        c,$1e57                       ;[1e51] 38 04
                    rl        (hl)                          ;[1e53] cb 16
                    jr        nc,$1e53                      ;[1e55] 30 fc
                    ld        bc,$0007                      ;[1e57] 01 07 00
                    and       a                             ;[1e5a] a7
                    sbc       hl,bc                         ;[1e5b] ed 42
                    inc       de                            ;[1e5d] 13
                    pop       bc                            ;[1e5e] c1
                    djnz      $1e08                         ;[1e5f] 10 a7
                    ld        hl,$f330                      ;[1e61] 21 30 f3
                    call      $1e6a                         ;[1e64] cd 6a 1e
                    ld        hl,$f338                      ;[1e67] 21 38 f3
                    bit       5,(ix+$25)                    ;[1e6a] dd cb 25 6e
                    jr        z,$1eac                       ;[1e6e] 28 3c
                    ex        de,hl                         ;[1e70] eb
                    ld        hl,$f340                      ;[1e71] 21 40 f3
                    ld        b,$08                         ;[1e74] 06 08
                    ld        a,(de)                        ;[1e76] 1a
                    inc       de                            ;[1e77] 13
                    ld        (hl),a                        ;[1e78] 77
                    inc       hl                            ;[1e79] 23
                    ld        (hl),a                        ;[1e7a] 77
                    inc       hl                            ;[1e7b] 23
                    djnz      $1e76                         ;[1e7c] 10 f8
                    ld        hl,$f340                      ;[1e7e] 21 40 f3
                    bit       0,(ix+$25)                    ;[1e81] dd cb 25 46
                    jr        z,$1e88                       ;[1e85] 28 01
                    inc       hl                            ;[1e87] 23
                    call      $1eac                         ;[1e88] cd ac 1e
                    call      $1a4f                         ;[1e8b] cd 4f 1a
                    ld        hl,$f348                      ;[1e8e] 21 48 f3
                    ld        a,(ix+$22)                    ;[1e91] dd 7e 22
                    add       $08                           ;[1e94] c6 08
                    bit       0,(ix+$25)                    ;[1e96] dd cb 25 46
                    jr        z,$1e9f                       ;[1e9a] 28 03
                    dec       a                             ;[1e9c] 3d
                    dec       a                             ;[1e9d] 3d
                    dec       hl                            ;[1e9e] 2b
                    ld        (ix+$22),a                    ;[1e9f] dd 77 22
                    call      $1eac                         ;[1ea2] cd ac 1e
                    call      $1a95                         ;[1ea5] cd 95 1a
                    ld        (ix+$22),a                    ;[1ea8] dd 77 22
                    ret                                     ;[1eab] c9

                    push      hl                            ;[1eac] e5
                    ld        b,(ix+$18)                    ;[1ead] dd 46 18
                    inc       b                             ;[1eb0] 04
                    call      $1a88                         ;[1eb1] cd 88 1a
                    cp        b                             ;[1eb4] b8
                    jr        c,$1ec2                       ;[1eb5] 38 0b
                    ld        a,(ix+$22)                    ;[1eb7] dd 7e 22
                    sub       $08                           ;[1eba] d6 08
                    ld        (ix+$22),a                    ;[1ebc] dd 77 22
                    call      $2071                         ;[1ebf] cd 71 20
                    call      $271d                         ;[1ec2] cd 1d 27
                    jp        c,$1fe4                       ;[1ec5] da e4 1f
                    ld        a,(ix+$21)                    ;[1ec8] dd 7e 21
                    ld        d,(ix+$22)                    ;[1ecb] dd 56 22
                    call      $22c4                         ;[1ece] cd c4 22
                    bit       3,(ix+$1e)                    ;[1ed1] dd cb 1e 5e
                    ld        b,(ix+$1f)                    ;[1ed5] dd 46 1f
                    jr        nz,$1f0d                      ;[1ed8] 20 33
                    ex        af,af'                        ;[1eda] 08
                    ld        (hl),b                        ;[1edb] 70
                    ld        a,(ix+$23)                    ;[1edc] dd 7e 23
                    add       (ix+$0f)                      ;[1edf] dd 86 0f
                    cp        $09                           ;[1ee2] fe 09
                    jr        c,$1ee9                       ;[1ee4] 38 03
                    inc       hl                            ;[1ee6] 23
                    ld        (hl),b                        ;[1ee7] 70
                    dec       hl                            ;[1ee8] 2b
                    ld        a,(ix+$22)                    ;[1ee9] dd 7e 22
                    and       $07                           ;[1eec] e6 07
                    jr        z,$1f17                       ;[1eee] 28 27
                    bit       0,(ix+$25)                    ;[1ef0] dd cb 25 46
                    jr        z,$1efa                       ;[1ef4] 28 04
                    cp        $03                           ;[1ef6] fe 03
                    jr        c,$1f17                       ;[1ef8] 38 1d
                    add       hl,$0020                      ;[1efa] ed 34 20 00
                    ld        (hl),b                        ;[1efe] 70
                    ld        a,(ix+$23)                    ;[1eff] dd 7e 23
                    add       (ix+$0f)                      ;[1f02] dd 86 0f
                    cp        $09                           ;[1f05] fe 09
                    jr        c,$1f17                       ;[1f07] 38 0e
                    inc       hl                            ;[1f09] 23
                    ld        (hl),b                        ;[1f0a] 70
                    jr        $1f17                         ;[1f0b] 18 0a
                    ld        a,b                           ;[1f0d] 78
                    ex        af,af'                        ;[1f0e] 08
                    bit       3,(ix+$23)                    ;[1f0f] dd cb 23 5e
                    jr        z,$1f17                       ;[1f13] 28 02
                    set       7,d                           ;[1f15] cb fa
                    ex        de,hl                         ;[1f17] eb
                    exx                                     ;[1f18] d9
                    ex        (sp),hl                       ;[1f19] e3
                    push      bc                            ;[1f1a] c5
                    push      de                            ;[1f1b] d5
                    ld        b,$08                         ;[1f1c] 06 08
                    bit       0,(ix+$25)                    ;[1f1e] dd cb 25 46
                    jr        z,$1f27                       ;[1f22] 28 03
                    dec       b                             ;[1f24] 05
                    dec       b                             ;[1f25] 05
                    inc       hl                            ;[1f26] 23
                    ld        e,(ix+$27)                    ;[1f27] dd 5e 27
                    ld        d,ixl                         ;[1f2a] dd 55
                    ld        c,(ix+$10)                    ;[1f2c] dd 4e 10
                    ld        a,c                           ;[1f2f] 79
                    exx                                     ;[1f30] d9
                    cpl                                     ;[1f31] 2f
                    or        (ix+$28)                      ;[1f32] dd b6 28
                    ld        d,a                           ;[1f35] 57
                    ld        b,(ix+$23)                    ;[1f36] dd 46 23
                    res       3,b                           ;[1f39] cb 98
                    ld        a,(ix+$1e)                    ;[1f3b] dd 7e 1e
                    cp        $0d                           ;[1f3e] fe 0d
                    jr        z,$1f4b                       ;[1f40] 28 09
                    ld        a,b                           ;[1f42] 78
                    add       (ix+$0f)                      ;[1f43] dd 86 0f
                    cp        $09                           ;[1f46] fe 09
                    jp        c,$1fb3                       ;[1f48] da b3 1f
                    ld        e,$ff                         ;[1f4b] 1e ff
                    bsrf      de,b                          ;[1f4d] ed 2b
                    ld        c,d                           ;[1f4f] 4a
                    ld        ixl,e                         ;[1f50] dd 6b
                    exx                                     ;[1f52] d9
                    ld        a,(hl)                        ;[1f53] 7e
                    inc       hl                            ;[1f54] 23
                    xor       e                             ;[1f55] ab
                    and       c                             ;[1f56] a1
                    exx                                     ;[1f57] d9
                    ld        d,a                           ;[1f58] 57
                    ld        e,$00                         ;[1f59] 1e 00
                    bsrl      de,b                          ;[1f5b] ed 2a
                    ex        af,af'                        ;[1f5d] 08
                    jr        z,$1f9a                       ;[1f5e] 28 3a
                    ex        af,af'                        ;[1f60] 08
                    bit       2,(iy+$45)                    ;[1f61] fd cb 45 56
                    jr        nz,$1f7d                      ;[1f65] 20 16
                    push      hl                            ;[1f67] e5
                    ld        a,c                           ;[1f68] 79
                    and       (hl)                          ;[1f69] a6
                    xor       d                             ;[1f6a] aa
                    ld        (hl),a                        ;[1f6b] 77
                    bit       7,h                           ;[1f6c] cb 7c
                    set       7,h                           ;[1f6e] cb fc
                    jr        z,$1f75                       ;[1f70] 28 03
                    res       7,h                           ;[1f72] cb bc
                    inc       hl                            ;[1f74] 23
                    ld        a,ixl                         ;[1f75] dd 7d
                    and       (hl)                          ;[1f77] a6
                    xor       e                             ;[1f78] ab
                    ld        (hl),a                        ;[1f79] 77
                    pop       hl                            ;[1f7a] e1
                    jr        $1fa6                         ;[1f7b] 18 29
                    ld        a,c                           ;[1f7d] 79
                    and       (hl)                          ;[1f7e] a6
                    xor       d                             ;[1f7f] aa
                    ld        (hl),a                        ;[1f80] 77
                    inc       hl                            ;[1f81] 23
                    ld        a,ixl                         ;[1f82] dd 7d
                    and       (hl)                          ;[1f84] a6
                    xor       e                             ;[1f85] ab
                    ld        (hl),a                        ;[1f86] 77
                    dec       hl                            ;[1f87] 2b
                    set       7,h                           ;[1f88] cb fc
                    ex        af,af'                        ;[1f8a] 08
                    ld        (hl),a                        ;[1f8b] 77
                    ex        af,af'                        ;[1f8c] 08
                    inc       b                             ;[1f8d] 04
                    dec       b                             ;[1f8e] 05
                    jr        z,$1f96                       ;[1f8f] 28 05
                    inc       hl                            ;[1f91] 23
                    ex        af,af'                        ;[1f92] 08
                    ld        (hl),a                        ;[1f93] 77
                    ex        af,af'                        ;[1f94] 08
                    dec       hl                            ;[1f95] 2b
                    res       7,h                           ;[1f96] cb bc
                    jr        $1fa6                         ;[1f98] 18 0c
                    ex        af,af'                        ;[1f9a] 08
                    ld        a,c                           ;[1f9b] 79
                    and       (hl)                          ;[1f9c] a6
                    xor       d                             ;[1f9d] aa
                    ld        (hl),a                        ;[1f9e] 77
                    inc       hl                            ;[1f9f] 23
                    ld        a,ixl                         ;[1fa0] dd 7d
                    and       (hl)                          ;[1fa2] a6
                    xor       e                             ;[1fa3] ab
                    ld        (hl),a                        ;[1fa4] 77
                    dec       hl                            ;[1fa5] 2b
                    pixeldn                                 ;[1fa6] ed 93
                    exx                                     ;[1fa8] d9
                    djnz      $1f53                         ;[1fa9] 10 a8
                    ld        ixl,d                         ;[1fab] dd 6a
                    pop       de                            ;[1fad] d1
                    pop       bc                            ;[1fae] c1
                    pop       hl                            ;[1faf] e1
                    exx                                     ;[1fb0] d9
                    jr        $1fcc                         ;[1fb1] 18 19
                    bsrf      de,b                          ;[1fb3] ed 2b
                    ld        c,d                           ;[1fb5] 4a
                    exx                                     ;[1fb6] d9
                    ld        a,(hl)                        ;[1fb7] 7e
                    inc       hl                            ;[1fb8] 23
                    xor       e                             ;[1fb9] ab
                    and       c                             ;[1fba] a1
                    exx                                     ;[1fbb] d9
                    ld        d,a                           ;[1fbc] 57
                    bsrl      de,b                          ;[1fbd] ed 2a
                    ld        a,c                           ;[1fbf] 79
                    and       (hl)                          ;[1fc0] a6
                    xor       d                             ;[1fc1] aa
                    ld        (hl),a                        ;[1fc2] 77
                    pixeldn                                 ;[1fc3] ed 93
                    exx                                     ;[1fc5] d9
                    djnz      $1fb7                         ;[1fc6] 10 ef
                    pop       de                            ;[1fc8] d1
                    pop       bc                            ;[1fc9] c1
                    pop       hl                            ;[1fca] e1
                    exx                                     ;[1fcb] d9
                    inc       (ix+$24)                      ;[1fcc] dd 34 24
                    ld        a,(ix+$23)                    ;[1fcf] dd 7e 23
                    add       (ix+$0f)                      ;[1fd2] dd 86 0f
                    cp        (ix+$1d)                      ;[1fd5] dd be 1d
                    jr        c,$1fe0                       ;[1fd8] 38 06
                    sub       (ix+$1d)                      ;[1fda] dd 96 1d
                    inc       (ix+$21)                      ;[1fdd] dd 34 21
                    ld        (ix+$23),a                    ;[1fe0] dd 77 23
                    ret                                     ;[1fe3] c9

                    ld        a,(ix+$21)                    ;[1fe4] dd 7e 21
                    add       a                             ;[1fe7] 87
                    add       a                             ;[1fe8] 87
                    add       a                             ;[1fe9] 87
                    add       (ix+$23)                      ;[1fea] dd 86 23
                    ld        l,a                           ;[1fed] 6f
                    ld        h,(ix+$22)                    ;[1fee] dd 66 22
                    call      $26b3                         ;[1ff1] cd b3 26
                    ld        a,c                           ;[1ff4] 79
                    sub       (ix+$0f)                      ;[1ff5] dd 96 0f
                    inc       a                             ;[1ff8] 3c
                    ld        c,a                           ;[1ff9] 4f
                    ld        d,(ix+$1f)                    ;[1ffa] dd 56 1f
                    ld        e,(ix+$20)                    ;[1ffd] dd 5e 20
                    bit       0,(ix+$27)                    ;[2000] dd cb 27 46
                    jr        z,$2009                       ;[2004] 28 03
                    ld        a,d                           ;[2006] 7a
                    ld        d,e                           ;[2007] 53
                    ld        e,a                           ;[2008] 5f
                    exx                                     ;[2009] d9
                    ex        (sp),hl                       ;[200a] e3
                    push      bc                            ;[200b] c5
                    ld        b,$08                         ;[200c] 06 08
                    bit       0,(ix+$25)                    ;[200e] dd cb 25 46
                    jr        z,$2017                       ;[2012] 28 03
                    ld        b,$06                         ;[2014] 06 06
                    inc       hl                            ;[2016] 23
                    ld        a,(hl)                        ;[2017] 7e
                    inc       hl                            ;[2018] 23
                    exx                                     ;[2019] d9
                    push      bc                            ;[201a] c5
                    ld        b,(ix+$0f)                    ;[201b] dd 46 0f
                    bit       0,(ix+$28)                    ;[201e] dd cb 28 46
                    jr        nz,$203a                      ;[2022] 20 16
                    rla                                     ;[2024] 17
                    ld        (hl),d                        ;[2025] 72
                    jr        c,$2029                       ;[2026] 38 01
                    ld        (hl),e                        ;[2028] 73
                    inc       hl                            ;[2029] 23
                    djnz      $2024                         ;[202a] 10 f8
                    pop       bc                            ;[202c] c1
                    ld        a,c                           ;[202d] 79
                    add       hl,a                          ;[202e] ed 31
                    ld        a,h                           ;[2030] 7c
                    cp        b                             ;[2031] b8
                    call      nc,$26fa                      ;[2032] d4 fa 26
                    exx                                     ;[2035] d9
                    djnz      $2017                         ;[2036] 10 df
                    jr        $1fc9                         ;[2038] 18 8f
                    rla                                     ;[203a] 17
                    jr        nc,$203e                      ;[203b] 30 01
                    ld        (hl),d                        ;[203d] 72
                    inc       hl                            ;[203e] 23
                    djnz      $203a                         ;[203f] 10 f9
                    jr        $202c                         ;[2041] 18 e9
                    xor       a                             ;[2043] af
                    ld        (ix+$23),a                    ;[2044] dd 77 23
                    ld        (ix+$24),a                    ;[2047] dd 77 24
                    ld        a,(ix+$13)                    ;[204a] dd 7e 13
                    ld        (ix+$21),a                    ;[204d] dd 77 21
                    bit       5,(ix+$25)                    ;[2050] dd cb 25 6e
                    call      nz,$2057                      ;[2054] c4 57 20
                    push      hl                            ;[2057] e5
                    call      $1a88                         ;[2058] cd 88 1a
                    ld        (ix+$22),a                    ;[205b] dd 77 22
                    ld        a,(ix+$18)                    ;[205e] dd 7e 18
                    cp        (ix+$22)                      ;[2061] dd be 22
                    call      c,$2069                       ;[2064] dc 69 20
                    pop       hl                            ;[2067] e1
                    ret                                     ;[2068] c9

                    ld        a,(ix+$22)                    ;[2069] dd 7e 22
                    sub       $08                           ;[206c] d6 08
                    ld        (ix+$22),a                    ;[206e] dd 77 22
                    ld        a,(ix+$1a)                    ;[2071] dd 7e 1a
                    and       a                             ;[2074] a7
                    jr        z,$20a6                       ;[2075] 28 2f
                    dec       (ix+$1b)                      ;[2077] dd 35 1b
                    jr        nz,$20a6                      ;[207a] 20 2a
                    ld        (ix+$1b),a                    ;[207c] dd 77 1b
                    ld        hl,$2190                      ;[207f] 21 90 21
                    call      $2178                         ;[2082] cd 78 21
                    call      $2130                         ;[2085] cd 30 21
                    call      $0c6d                         ;[2088] cd 6d 0c
                    call      $2130                         ;[208b] cd 30 21
                    ld        a,$7f                         ;[208e] 3e 7f
                    in        a,($fe)                       ;[2090] db fe
                    rra                                     ;[2092] 1f
                    jr        c,$20a6                       ;[2093] 38 11
                    ld        a,$fe                         ;[2095] 3e fe
                    in        a,($fe)                       ;[2097] db fe
                    rra                                     ;[2099] 1f
                    jr        c,$20a6                       ;[209a] 38 0a
                    rst       $18                           ;[209c] df
                    ld        (hl),l                        ;[209d] 75
                    ld        a,$20                         ;[209e] 3e 20
                    dec       b                             ;[20a0] 05
                    ld        a,$0c                         ;[20a1] 3e 0c
                    jp        $27d3                         ;[20a3] c3 d3 27
                    ld        hl,$2199                      ;[20a6] 21 99 21
                    call      $2178                         ;[20a9] cd 78 21
                    ld        e,$01                         ;[20ac] 1e 01
                    call      $20e6                         ;[20ae] cd e6 20
                    call      $20b5                         ;[20b1] cd b5 20
                    ret                                     ;[20b4] c9

                    call      $2175                         ;[20b5] cd 75 21
                    ld        c,(ix+$11)                    ;[20b8] dd 4e 11
                    ld        b,$01                         ;[20bb] 06 01
                    push      bc                            ;[20bd] c5
                    ld        h,(ix+$16)                    ;[20be] dd 66 16
                    jr        $20f3                         ;[20c1] 18 30
                    ld        (ix+$1a),a                    ;[20c3] dd 77 1a
                    ld        (ix+$1b),a                    ;[20c6] dd 77 1b
                    ret                                     ;[20c9] c9

                    ld        hl,$218a                      ;[20ca] 21 8a 21
                    jr        $20e1                         ;[20cd] 18 12
                    call      $1a30                         ;[20cf] cd 30 1a
                    ld        a,$01                         ;[20d2] 3e 01
                    ld        (ix+$1b),a                    ;[20d4] dd 77 1b
                    ld        hl,$2181                      ;[20d7] 21 81 21
                    jr        $20e1                         ;[20da] 18 05
                    push      hl                            ;[20dc] e5
                    call      $1942                         ;[20dd] cd 42 19
                    pop       hl                            ;[20e0] e1
                    call      $2178                         ;[20e1] cd 78 21
                    ld        e,$00                         ;[20e4] 1e 00
                    ld        c,(ix+$11)                    ;[20e6] dd 4e 11
                    ld        a,(ix+$12)                    ;[20e9] dd 7e 12
                    sub       e                             ;[20ec] 93
                    ld        b,a                           ;[20ed] 47
                    ret       z                             ;[20ee] c8
                    push      bc                            ;[20ef] c5
                    ld        h,(ix+$14)                    ;[20f0] dd 66 14
                    ld        l,(ix+$13)                    ;[20f3] dd 6e 13
                    add       hl,hl                         ;[20f6] 29
                    add       hl,hl                         ;[20f7] 29
                    add       hl,hl                         ;[20f8] 29
                    call      $271d                         ;[20f9] cd 1d 27
                    jr        c,$213c                       ;[20fc] 38 3e
                    ex        de,hl                         ;[20fe] eb
                    call      $22c8                         ;[20ff] cd c8 22
                    pop       bc                            ;[2102] c1
                    push      bc                            ;[2103] c5
                    push      bc                            ;[2104] c5
                    ld        b,$00                         ;[2105] 06 00
                    bit       3,(iy+$45)                    ;[2107] fd cb 45 5e
                    call      z,$5b91                       ;[210b] cc 91 5b
                    pop       bc                            ;[210e] c1
                    djnz      $2104                         ;[210f] 10 f3
                    pop       bc                            ;[2111] c1
                    push      bc                            ;[2112] c5
                    ld        b,$00                         ;[2113] 06 00
                    ld        l,e                           ;[2115] 6b
                    ld        a,d                           ;[2116] 7a
                    or        $07                           ;[2117] f6 07
                    ld        h,a                           ;[2119] 67
                    pixeldn                                 ;[211a] ed 93
                    push      hl                            ;[211c] e5
                    ld        a,$08                         ;[211d] 3e 08
                    push      af                            ;[211f] f5
                    push      de                            ;[2120] d5
                    call      $5b94                         ;[2121] cd 94 5b
                    pop       de                            ;[2124] d1
                    pop       af                            ;[2125] f1
                    inc       h                             ;[2126] 24
                    inc       d                             ;[2127] 14
                    dec       a                             ;[2128] 3d
                    jr        nz,$211f                      ;[2129] 20 f4
                    pop       de                            ;[212b] d1
                    pop       bc                            ;[212c] c1
                    djnz      $2112                         ;[212d] 10 e3
                    ret                                     ;[212f] c9

                    ld        bc,$0101                      ;[2130] 01 01 01
                    push      bc                            ;[2133] c5
                    ld        h,(ix+$16)                    ;[2134] dd 66 16
                    ld        l,(ix+$15)                    ;[2137] dd 6e 15
                    jr        $20f6                         ;[213a] 18 ba
                    ld        ($5b8c),ix                    ;[213c] dd 22 8c 5b
                    ld        a,(ix+$20)                    ;[2140] dd 7e 20
                    ld        ($5b9a),a                     ;[2143] 32 9a 5b
                    call      $26b2                         ;[2146] cd b2 26
                    pop       de                            ;[2149] d1
                    ld        a,e                           ;[214a] 7b
                    add       a                             ;[214b] 87
                    add       a                             ;[214c] 87
                    ld        ixl,a                         ;[214d] dd 6f
                    ld        ixh,$00                       ;[214f] dd 26 00
                    add       ix,ix                         ;[2152] dd 29
                    ld        e,$08                         ;[2154] 1e 08
                    push      de                            ;[2156] d5
                    push      hl                            ;[2157] e5
                    call      $5b97                         ;[2158] cd 97 5b
                    pop       hl                            ;[215b] e1
                    ld        a,b                           ;[215c] 78
                    ld        b,$00                         ;[215d] 06 00
                    add       hl,bc                         ;[215f] 09
                    inc       hl                            ;[2160] 23
                    ld        b,a                           ;[2161] 47
                    pop       de                            ;[2162] d1
                    dec       e                             ;[2163] 1d
                    jr        nz,$2156                      ;[2164] 20 f0
                    ld        a,h                           ;[2166] 7c
                    cp        b                             ;[2167] b8
                    call      nc,$26f9                      ;[2168] d4 f9 26
                    dec       d                             ;[216b] 15
                    jr        nz,$2154                      ;[216c] 20 e6
                    ld        ix,($5b8c)                    ;[216e] dd 2a 8c 5b
                    jp        $1942                         ;[2172] c3 42 19
                    ld        hl,$2181                      ;[2175] 21 81 21
                    ld        de,$5b91                      ;[2178] 11 91 5b
                    ld        bc,$0009                      ;[217b] 01 09 00
                    ldir                                    ;[217e] ed b0
                    ret                                     ;[2180] c9

                    jp        $21b9                         ;[2181] c3 b9 21
                    jp        $21eb                         ;[2184] c3 eb 21
                    jp        $2253                         ;[2187] c3 53 22
                    jp        $21b9                         ;[218a] c3 b9 21
                    jp        $2209                         ;[218d] c3 09 22
                    ret                                     ;[2190] c9

                    nop                                     ;[2191] 00
                    nop                                     ;[2192] 00
                    jp        $2224                         ;[2193] c3 24 22
                    jp        $2269                         ;[2196] c3 69 22
                    jp        $21ab                         ;[2199] c3 ab 21
                    jp        $21d3                         ;[219c] c3 d3 21
                    jp        $2237                         ;[219f] c3 37 22
                    jp        $21ca                         ;[21a2] c3 ca 21
                    jp        $2213                         ;[21a5] c3 13 22
                    jp        $225e                         ;[21a8] c3 5e 22
                    push      de                            ;[21ab] d5
                    ex        de,hl                         ;[21ac] eb
                    ld        hl,$0020                      ;[21ad] 21 20 00
                    add       hl,de                         ;[21b0] 19
                    push      hl                            ;[21b1] e5
                    push      bc                            ;[21b2] c5
                    ldir                                    ;[21b3] ed b0
                    pop       bc                            ;[21b5] c1
                    pop       hl                            ;[21b6] e1
                    pop       de                            ;[21b7] d1
                    ret                                     ;[21b8] c9

                    ld        a,(ix+$1f)                    ;[21b9] dd 7e 1f
                    ld        b,c                           ;[21bc] 41
                    ld        (hl),a                        ;[21bd] 77
                    inc       hl                            ;[21be] 23
                    djnz      $21bd                         ;[21bf] 10 fc
                    ld        a,$20                         ;[21c1] 3e 20
                    sub       c                             ;[21c3] 91
                    add       l                             ;[21c4] 85
                    ld        l,a                           ;[21c5] 6f
                    ld        a,b                           ;[21c6] 78
                    adc       h                             ;[21c7] 8c
                    ld        h,a                           ;[21c8] 67
                    ret                                     ;[21c9] c9

                    push      de                            ;[21ca] d5
                    ex        de,hl                         ;[21cb] eb
                    call      $2273                         ;[21cc] cd 73 22
                    ex        de,hl                         ;[21cf] eb
                    pop       de                            ;[21d0] d1
                    jr        $21c1                         ;[21d1] 18 ee
                    push      hl                            ;[21d3] e5
                    push      de                            ;[21d4] d5
                    push      bc                            ;[21d5] c5
                    ldir                                    ;[21d6] ed b0
                    pop       bc                            ;[21d8] c1
                    pop       de                            ;[21d9] d1
                    pop       hl                            ;[21da] e1
                    bit       3,(ix+$1e)                    ;[21db] dd cb 1e 5e
                    ret       z                             ;[21df] c8
                    push      hl                            ;[21e0] e5
                    push      bc                            ;[21e1] c5
                    set       7,h                           ;[21e2] cb fc
                    set       7,d                           ;[21e4] cb fa
                    ldir                                    ;[21e6] ed b0
                    pop       bc                            ;[21e8] c1
                    pop       hl                            ;[21e9] e1
                    ret                                     ;[21ea] c9

                    push      de                            ;[21eb] d5
                    ld        b,c                           ;[21ec] 41
                    xor       a                             ;[21ed] af
                    ld        (de),a                        ;[21ee] 12
                    inc       de                            ;[21ef] 13
                    djnz      $21ee                         ;[21f0] 10 fc
                    pop       de                            ;[21f2] d1
                    bit       3,(ix+$1e)                    ;[21f3] dd cb 1e 5e
                    ret       z                             ;[21f7] c8
                    bit       2,(ix+$1e)                    ;[21f8] dd cb 1e 56
                    jr        z,$2201                       ;[21fc] 28 03
                    ld        a,(ix+$1f)                    ;[21fe] dd 7e 1f
                    ld        b,c                           ;[2201] 41
                    set       7,d                           ;[2202] cb fa
                    ld        (de),a                        ;[2204] 12
                    inc       de                            ;[2205] 13
                    djnz      $2204                         ;[2206] 10 fc
                    ret                                     ;[2208] c9

                    ld        a,(ix+$1e)                    ;[2209] dd 7e 1e
                    and       $0c                           ;[220c] e6 0c
                    cp        $0c                           ;[220e] fe 0c
                    ret       nz                            ;[2210] c0
                    jr        $21fe                         ;[2211] 18 eb
                    push      hl                            ;[2213] e5
                    push      de                            ;[2214] d5
                    call      $2273                         ;[2215] cd 73 22
                    pop       de                            ;[2218] d1
                    set       7,d                           ;[2219] cb fa
                    bit       3,(iy+$45)                    ;[221b] fd cb 45 5e
                    call      nz,$2276                      ;[221f] c4 76 22
                    pop       hl                            ;[2222] e1
                    ret                                     ;[2223] c9

                    ld        a,(ix+$1e)                    ;[2224] dd 7e 1e
                    and       $0c                           ;[2227] e6 0c
                    cp        $08                           ;[2229] fe 08
                    jr        nz,$222f                      ;[222b] 20 02
                    set       7,d                           ;[222d] cb fa
                    ld        b,c                           ;[222f] 41
                    ld        a,(de)                        ;[2230] 1a
                    cpl                                     ;[2231] 2f
                    ld        (de),a                        ;[2232] 12
                    inc       de                            ;[2233] 13
                    djnz      $2230                         ;[2234] 10 fa
                    ret                                     ;[2236] c9

                    ex        de,hl                         ;[2237] eb
                    ld        h,$00                         ;[2238] 26 00
                    ld        l,c                           ;[223a] 69
                    inc       hl                            ;[223b] 23
                    add       hl,hl                         ;[223c] 29
                    add       hl,hl                         ;[223d] 29
                    add       hl,hl                         ;[223e] 29
                    add       hl,de                         ;[223f] 19
                    bit       7,c                           ;[2240] cb 79
                    jr        nz,$224b                      ;[2242] 20 07
                    ld        a,h                           ;[2244] 7c
                    cp        b                             ;[2245] b8
                    jr        c,$224b                       ;[2246] 38 03
                    add       $68                           ;[2248] c6 68
                    ld        h,a                           ;[224a] 67
                    push      bc                            ;[224b] c5
                    push      ix                            ;[224c] dd e5
                    pop       bc                            ;[224e] c1
                    ldir                                    ;[224f] ed b0
                    pop       bc                            ;[2251] c1
                    ret                                     ;[2252] c9

                    ld        a,($5b9a)                     ;[2253] 3a 9a 5b
                    ld        e,ixl                         ;[2256] dd 5d
                    ld        (hl),a                        ;[2258] 77
                    inc       hl                            ;[2259] 23
                    dec       e                             ;[225a] 1d
                    jr        nz,$2258                      ;[225b] 20 fb
                    ret                                     ;[225d] c9

                    push      bc                            ;[225e] c5
                    ld        b,$00                         ;[225f] 06 00
                    ld        c,ixl                         ;[2261] dd 4d
                    ex        de,hl                         ;[2263] eb
                    call      $2273                         ;[2264] cd 73 22
                    pop       bc                            ;[2267] c1
                    ret                                     ;[2268] c9

                    ld        d,ixl                         ;[2269] dd 55
                    ld        a,(hl)                        ;[226b] 7e
                    cpl                                     ;[226c] 2f
                    ld        (hl),a                        ;[226d] 77
                    inc       hl                            ;[226e] 23
                    dec       d                             ;[226f] 15
                    jr        nz,$226b                      ;[2270] 20 f9
                    ret                                     ;[2272] c9

                    ld        hl,($5b9b)                    ;[2273] 2a 9b 5b
                    ld        a,(iy+$58)                    ;[2276] fd 7e 58
                    bit       7,a                           ;[2279] cb 7f
                    jr        z,$227e                       ;[227b] 28 01
                    ex        de,hl                         ;[227d] eb
                    and       $07                           ;[227e] e6 07
                    jr        z,$22b3                       ;[2280] 28 31
                    dec       a                             ;[2282] 3d
                    jr        z,$2295                       ;[2283] 28 10
                    dec       a                             ;[2285] 3d
                    jr        z,$229f                       ;[2286] 28 17
                    dec       a                             ;[2288] 3d
                    jr        z,$22a9                       ;[2289] 28 1e
                    ld        a,($5c93)                     ;[228b] 3a 93 5c
                    push      bc                            ;[228e] c5
                    dec       c                             ;[228f] 0d
                    inc       bc                            ;[2290] 03
                    ldirx                                   ;[2291] ed b4
                    jr        $22b8                         ;[2293] 18 23
                    ld        b,c                           ;[2295] 41
                    ld        a,(de)                        ;[2296] 1a
                    and       (hl)                          ;[2297] a6
                    ld        (de),a                        ;[2298] 12
                    inc       hl                            ;[2299] 23
                    inc       de                            ;[229a] 13
                    djnz      $2296                         ;[229b] 10 f9
                    jr        $22b9                         ;[229d] 18 1a
                    ld        b,c                           ;[229f] 41
                    ld        a,(de)                        ;[22a0] 1a
                    or        (hl)                          ;[22a1] b6
                    ld        (de),a                        ;[22a2] 12
                    inc       hl                            ;[22a3] 23
                    inc       de                            ;[22a4] 13
                    djnz      $22a0                         ;[22a5] 10 f9
                    jr        $22b9                         ;[22a7] 18 10
                    ld        b,c                           ;[22a9] 41
                    ld        a,(de)                        ;[22aa] 1a
                    xor       (hl)                          ;[22ab] ae
                    ld        (de),a                        ;[22ac] 12
                    inc       hl                            ;[22ad] 23
                    inc       de                            ;[22ae] 13
                    djnz      $22aa                         ;[22af] 10 f9
                    jr        $22b9                         ;[22b1] 18 06
                    push      bc                            ;[22b3] c5
                    dec       c                             ;[22b4] 0d
                    inc       bc                            ;[22b5] 03
                    ldir                                    ;[22b6] ed b0
                    pop       bc                            ;[22b8] c1
                    bit       7,(iy+$58)                    ;[22b9] fd cb 58 7e
                    jr        z,$22c0                       ;[22bd] 28 01
                    ex        de,hl                         ;[22bf] eb
                    ld        ($5b9b),hl                    ;[22c0] 22 9b 5b
                    ret                                     ;[22c3] c9

                    add       a                             ;[22c4] 87
                    add       a                             ;[22c5] 87
                    add       a                             ;[22c6] 87
                    ld        e,a                           ;[22c7] 5f
                    pixelad                                 ;[22c8] ed 94
                    ld        a,d                           ;[22ca] 7a
                    rlca                                    ;[22cb] 07
                    rlca                                    ;[22cc] 07
                    and       $03                           ;[22cd] e6 03
                    or        $58                           ;[22cf] f6 58
                    ld        d,a                           ;[22d1] 57
                    ld        e,l                           ;[22d2] 5d
                    ex        de,hl                         ;[22d3] eb
                    ret                                     ;[22d4] c9

                    xor       a                             ;[22d5] af
                    srl       e                             ;[22d6] cb 3b
                    rra                                     ;[22d8] 1f
                    sra       a                             ;[22d9] cb 2f
                    dec       c                             ;[22db] 0d
                    jr        nz,$22d9                      ;[22dc] 20 fb
                    srl       e                             ;[22de] cb 3b
                    rra                                     ;[22e0] 1f
                    sra       a                             ;[22e1] cb 2f
                    djnz      $22e1                         ;[22e3] 10 fc
                    ld        b,$04                         ;[22e5] 06 04
                    ld        (hl),a                        ;[22e7] 77
                    inc       hl                            ;[22e8] 23
                    djnz      $22e7                         ;[22e9] 10 fc
                    ret                                     ;[22eb] c9

                    cp        $03                           ;[22ec] fe 03
                    jr        c,$2305                       ;[22ee] 38 15
                    cp        $09                           ;[22f0] fe 09
                    jr        nc,$2305                      ;[22f2] 30 11
                    ld        hl,$ec00                      ;[22f4] 21 00 ec
                    cp        $05                           ;[22f7] fe 05
                    ret       c                             ;[22f9] d8
                    ld        h,$ef                         ;[22fa] 26 ef
                    cp        $07                           ;[22fc] fe 07
                    ret       c                             ;[22fe] d8
                    ld        h,$f7                         ;[22ff] 26 f7
                    ret       z                             ;[2301] c8
                    ld        h,$fb                         ;[2302] 26 fb
                    ret                                     ;[2304] c9

                    ld        a,$0a                         ;[2305] 3e 0a
                    jp        $27d3                         ;[2307] c3 d3 27
                    nop                                     ;[230a] 00
                    ei                                      ;[230b] fb
                    ex        af,af'                        ;[230c] 08
                    rst       $38                           ;[230d] ff
                    djnz      $231c                         ;[230e] 10 0c
                    nop                                     ;[2310] 00
                    nop                                     ;[2311] 00
                    rrca                                    ;[2312] 0f
                    dec       bc                            ;[2313] 0b
                    nop                                     ;[2314] 00
                    ld        h,b                           ;[2315] 60
                    djnz      $2324                         ;[2316] 10 0c
                    ld        bc,$0810                      ;[2318] 01 10 08
                    ld        bc,$ff00                      ;[231b] 01 00 ff
                    nop                                     ;[231e] 00
                    ei                                      ;[231f] fb
                    ex        af,af'                        ;[2320] 08
                    rst       $38                           ;[2321] ff
                    jr        nz,$233c                      ;[2322] 20 18
                    nop                                     ;[2324] 00
                    nop                                     ;[2325] 00
                    rra                                     ;[2326] 1f
                    rla                                     ;[2327] 17
                    nop                                     ;[2328] 00
                    ret       nz                            ;[2329] c0
                    djnz      $2344                         ;[232a] 10 18
                    ld        bc,$0820                      ;[232c] 01 20 08
                    ld        (bc),a                        ;[232f] 02
                    nop                                     ;[2330] 00
                    rst       $38                           ;[2331] ff
                    ld        b,b                           ;[2332] 40
                    djnz      $233e                         ;[2333] 10 09
                    nop                                     ;[2335] 00
                    nop                                     ;[2336] 00
                    jr        nz,$2341                      ;[2337] 20 08
                    dec       b                             ;[2339] 05
                    jr        c,$233c                       ;[233a] 38 00
                    jr        nz,$2346                      ;[233c] 20 08
                    dec       c                             ;[233e] 0d
                    jr        c,$2341                       ;[233f] 38 00
                    push    $23aa                           ;[2341] ed 8a 23 aa
                    ld        e,b                           ;[2345] 58
                    ld        d,$54                         ;[2346] 16 54
                    call      $272e                         ;[2348] cd 2e 27
                    dec       e                             ;[234b] 1d
                    jr        z,$238a                       ;[234c] 28 3c
                    ld        hl,$a000                      ;[234e] 21 00 a0
                    ld        de,$a001                      ;[2351] 11 01 a0
                    ld        bc,$0151                      ;[2354] 01 51 01
                    ld        (hl),l                        ;[2357] 75
                    ldir                                    ;[2358] ed b0
                    ld        a,($5b69)                     ;[235a] 3a 69 5b
                    inc       a                             ;[235d] 3c
                    add       a                             ;[235e] 87
                    ld        ($a050),a                     ;[235f] 32 50 a0
                    ld        a,$10                         ;[2362] 3e 10
                    ld        ($a051),a                     ;[2364] 32 51 a0
                    ld        hl,$ffff                      ;[2367] 21 ff ff
                    ld        ($a000),hl                    ;[236a] 22 00 a0
                    ld        ($a028),hl                    ;[236d] 22 28 a0
                    ld        a,l                           ;[2370] 7d
                    ld        ($a002),a                     ;[2371] 32 02 a0
                    ld        ($a02a),a                     ;[2374] 32 2a a0
                    ld        hl,$3ff7                      ;[2377] 21 f7 3f
                    ld        ($a020),hl                    ;[237a] 22 20 a0
                    ld        ($a048),hl                    ;[237d] 22 48 a0
                    ld        hl,$0001                      ;[2380] 21 01 00
                    rst       $20                           ;[2383] e7
                    cp        l                             ;[2384] bd
                    ld        bc,$327b                      ;[2385] 01 7b 32
                    inc       sp                            ;[2388] 33
                    ld        e,e                           ;[2389] 5b
                    ld        hl,$04df                      ;[238a] 21 df 04
                    ld        ($bffe),hl                    ;[238d] 22 fe bf
                    ld        hl,$2216                      ;[2390] 21 16 22
                    ld        ($a393),hl                    ;[2393] 22 93 a3
                    ld        hl,$3d00                      ;[2396] 21 00 3d
                    ld        de,$bc00                      ;[2399] 11 00 bc
                    ld        bc,$0300                      ;[239c] 01 00 03
                    rst       $28                           ;[239f] ef
                    jp        $cd33                         ;[23a0] c3 33 cd
                    cp        a                             ;[23a3] bf
                    inc       hl                            ;[23a4] 23
                    ld        d,$54                         ;[23a5] 16 54
                    jp        $27bd                         ;[23a7] c3 bd 27
                    call      $272c                         ;[23aa] cd 2c 27
                    call      $2671                         ;[23ad] cd 71 26
                    ld        hl,$f700                      ;[23b0] 21 00 f7
                    call      $09df                         ;[23b3] cd df 09
                    ld        hl,$fb00                      ;[23b6] 21 00 fb
                    call      $09df                         ;[23b9] cd df 09
                    jp        $27bb                         ;[23bc] c3 bb 27
                    ld        hl,$bc00                      ;[23bf] 21 00 bc
                    ld        de,$b800                      ;[23c2] 11 00 b8
                    ld        bc,$0003                      ;[23c5] 01 03 00
                    ld        a,(hl)                        ;[23c8] 7e
                    inc       hl                            ;[23c9] 23
                    add       a                             ;[23ca] 87
                    ld        (de),a                        ;[23cb] 12
                    inc       de                            ;[23cc] 13
                    djnz      $23c8                         ;[23cd] 10 f9
                    dec       c                             ;[23cf] 0d
                    jr        nz,$23c8                      ;[23d0] 20 f6
                    push      ix                            ;[23d2] dd e5
                    ld        ix,$2656                      ;[23d4] dd 21 56 26
                    ld        hl,$2416                      ;[23d8] 21 16 24
                    ld        de,$ad00                      ;[23db] 11 00 ad
                    ld        b,$60                         ;[23de] 06 60
                    ld        c,$08                         ;[23e0] 0e 08
                    xor       a                             ;[23e2] af
                    bit       3,c                           ;[23e3] cb 59
                    jr        nz,$23fe                      ;[23e5] 20 17
                    ld        a,c                           ;[23e7] 79
                    dec       a                             ;[23e8] 3d
                    jr        nz,$23fc                      ;[23e9] 20 11
                    ld        a,b                           ;[23eb] 78
                    cp        (ix+$00)                      ;[23ec] dd be 00
                    ld        a,$00                         ;[23ef] 3e 00
                    jr        nz,$23fe                      ;[23f1] 20 0b
                    inc       ix                            ;[23f3] dd 23
                    ld        a,(ix+$00)                    ;[23f5] dd 7e 00
                    inc       ix                            ;[23f8] dd 23
                    jr        $23fe                         ;[23fa] 18 02
                    ld        a,(hl)                        ;[23fc] 7e
                    inc       hl                            ;[23fd] 23
                    push      af                            ;[23fe] f5
                    and       $e0                           ;[23ff] e6 e0
                    ld        (de),a                        ;[2401] 12
                    inc       d                             ;[2402] 14
                    inc       d                             ;[2403] 14
                    inc       d                             ;[2404] 14
                    pop       af                            ;[2405] f1
                    add       a                             ;[2406] 87
                    add       a                             ;[2407] 87
                    add       a                             ;[2408] 87
                    ld        (de),a                        ;[2409] 12
                    dec       d                             ;[240a] 15
                    dec       d                             ;[240b] 15
                    dec       d                             ;[240c] 15
                    inc       de                            ;[240d] 13
                    dec       c                             ;[240e] 0d
                    jr        nz,$23e2                      ;[240f] 20 d1
                    djnz      $23e0                         ;[2411] 10 cd
                    pop       ix                            ;[2413] dd e1
                    ret                                     ;[2415] c9

                    nop                                     ;[2416] 00
                    nop                                     ;[2417] 00
                    nop                                     ;[2418] 00
                    nop                                     ;[2419] 00
                    nop                                     ;[241a] 00
                    nop                                     ;[241b] 00
                    ld        b,h                           ;[241c] 44
                    ld        b,h                           ;[241d] 44
                    ld        b,h                           ;[241e] 44
                    ld        b,h                           ;[241f] 44
                    nop                                     ;[2420] 00
                    ld        b,h                           ;[2421] 44
                    xor       d                             ;[2422] aa
                    xor       d                             ;[2423] aa
                    nop                                     ;[2424] 00
                    nop                                     ;[2425] 00
                    nop                                     ;[2426] 00
                    nop                                     ;[2427] 00
                    xor       d                             ;[2428] aa
                    rst       $38                           ;[2429] ff
                    xor       d                             ;[242a] aa
                    xor       d                             ;[242b] aa
                    rst       $38                           ;[242c] ff
                    xor       d                             ;[242d] aa
                    ld        b,d                           ;[242e] 42
                    rst       $28                           ;[242f] ef
                    adc       d                             ;[2430] 8a
                    rst       $28                           ;[2431] ef
                    inc       hl                            ;[2432] 23
                    rst       $28                           ;[2433] ef
                    xor       c                             ;[2434] a9
                    ld        hl,$4442                      ;[2435] 21 42 44
                    adc       b                             ;[2438] 88
                    xor       c                             ;[2439] a9
                    ld        c,b                           ;[243a] 48
                    or        h                             ;[243b] b4
                    ld        c,b                           ;[243c] 48
                    or        d                             ;[243d] dd b2
                    ld        b,d                           ;[243f] fd 42
                    add       h                             ;[2441] 84
                    nop                                     ;[2442] 00
                    nop                                     ;[2443] 00
                    nop                                     ;[2444] 00
                    nop                                     ;[2445] 00
                    ld        b,d                           ;[2446] 42
                    add       h                             ;[2447] 84
                    add       h                             ;[2448] 84
                    add       h                             ;[2449] 84
                    add       h                             ;[244a] 84
                    ld        b,d                           ;[244b] 42
                    add       h                             ;[244c] 84
                    ld        b,d                           ;[244d] 42
                    ld        b,d                           ;[244e] 42
                    ld        b,d                           ;[244f] 42
                    ld        b,d                           ;[2450] 42
                    add       h                             ;[2451] 84
                    nop                                     ;[2452] 00
                    xor       d                             ;[2453] aa
                    ld        b,h                           ;[2454] 44
                    xor       $44                           ;[2455] ee 44
                    xor       d                             ;[2457] aa
                    nop                                     ;[2458] 00
                    ld        b,h                           ;[2459] 44
                    ld        b,h                           ;[245a] 44
                    xor       $44                           ;[245b] ee 44
                    ld        b,h                           ;[245d] 44
                    nop                                     ;[245e] 00
                    nop                                     ;[245f] 00
                    nop                                     ;[2460] 00
                    nop                                     ;[2461] 00
                    ld        b,d                           ;[2462] 42
                    add       h                             ;[2463] 84
                    nop                                     ;[2464] 00
                    nop                                     ;[2465] 00
                    nop                                     ;[2466] 00
                    adc       $00                           ;[2467] ce 00
                    nop                                     ;[2469] 00
                    nop                                     ;[246a] 00
                    nop                                     ;[246b] 00
                    nop                                     ;[246c] 00
                    nop                                     ;[246d] 00
                    nop                                     ;[246e] 00
                    ld        b,h                           ;[246f] 44
                    ld        ($4422),hl                    ;[2470] 22 22 44
                    ld        b,h                           ;[2473] 44
                    adc       b                             ;[2474] 88
                    adc       b                             ;[2475] 88
                    ld        b,(hl)                        ;[2476] 46
                    xor       c                             ;[2477] a9
                    xor       c                             ;[2478] a9
                    xor       c                             ;[2479] a9
                    xor       c                             ;[247a] a9
                    ld        b,(hl)                        ;[247b] 46
                    ld        b,d                           ;[247c] 42
                    add       $42                           ;[247d] c6 42
                    ld        b,d                           ;[247f] 42
                    ld        b,d                           ;[2480] 42
                    ld        b,a                           ;[2481] 47
                    ld        b,(hl)                        ;[2482] 46
                    xor       c                             ;[2483] a9
                    ld        hl,$8846                      ;[2484] 21 46 88
                    rst       $28                           ;[2487] ef
                    add       $29                           ;[2488] c6 29
                    jp        nz,$2921                      ;[248a] c2 21 29
                    add       $22                           ;[248d] c6 22
                    ld        h,d                           ;[248f] 62
                    and       (hl)                          ;[2490] a6
                    xor       d                             ;[2491] aa
                    rst       $28                           ;[2492] ef
                    ld        ($88ef),hl                    ;[2493] 22 ef 88
                    adc       $21                           ;[2496] ce 21
                    add       hl,hl                         ;[2498] 29
                    add       $66                           ;[2499] c6 66
                    adc       b                             ;[249b] 88
                    adc       $a9                           ;[249c] ce a9
                    xor       c                             ;[249e] a9
                    ld        b,(hl)                        ;[249f] 46
                    rst       $28                           ;[24a0] ef
                    ld        hl,$4442                      ;[24a1] 21 42 44
                    add       h                             ;[24a4] 84
                    add       h                             ;[24a5] 84
                    ld        b,(hl)                        ;[24a6] 46
                    xor       c                             ;[24a7] a9
                    ld        b,(hl)                        ;[24a8] 46
                    xor       c                             ;[24a9] a9
                    xor       c                             ;[24aa] a9
                    ld        b,(hl)                        ;[24ab] 46
                    ld        b,(hl)                        ;[24ac] 46
                    xor       c                             ;[24ad] a9
                    xor       c                             ;[24ae] a9
                    ld        h,a                           ;[24af] 67
                    ld        hl,$00c6                      ;[24b0] 21 c6 00
                    ld        b,h                           ;[24b3] 44
                    nop                                     ;[24b4] 00
                    nop                                     ;[24b5] 00
                    ld        b,h                           ;[24b6] 44
                    nop                                     ;[24b7] 00
                    nop                                     ;[24b8] 00
                    ld        b,h                           ;[24b9] 44
                    nop                                     ;[24ba] 00
                    nop                                     ;[24bb] 00
                    ld        b,h                           ;[24bc] 44
                    adc       b                             ;[24bd] 88
                    nop                                     ;[24be] 00
                    ld        ($8844),hl                    ;[24bf] 22 44 88
                    ld        b,h                           ;[24c2] 44
                    ld        ($0000),hl                    ;[24c3] 22 00 00
                    adc       $00                           ;[24c6] ce 00
                    adc       $00                           ;[24c8] ce 00
                    nop                                     ;[24ca] 00
                    adc       b                             ;[24cb] 88
                    ld        b,h                           ;[24cc] 44
                    ld        ($8844),hl                    ;[24cd] 22 44 88
                    ld        b,(hl)                        ;[24d0] 46
                    xor       c                             ;[24d1] a9
                    ld        hl,$0042                      ;[24d2] 21 42 00
                    ld        b,d                           ;[24d5] 42
                    ld        b,(hl)                        ;[24d6] 46
                    rst       $28                           ;[24d7] ef
                    xor       l                             ;[24d8] ad
                    jp        pe,$e788                      ;[24d9] ea 88 e7
                    ld        b,(hl)                        ;[24dc] 46
                    xor       c                             ;[24dd] a9
                    xor       c                             ;[24de] a9
                    rst       $28                           ;[24df] ef
                    xor       c                             ;[24e0] a9
                    xor       c                             ;[24e1] a9
                    adc       $a9                           ;[24e2] ce a9
                    adc       $a9                           ;[24e4] ce a9
                    xor       c                             ;[24e6] a9
                    adc       $46                           ;[24e7] ce 46
                    xor       c                             ;[24e9] a9
                    adc       b                             ;[24ea] 88
                    adc       b                             ;[24eb] 88
                    xor       c                             ;[24ec] a9
                    ld        b,(hl)                        ;[24ed] 46
                    adc       $a9                           ;[24ee] ce a9
                    xor       c                             ;[24f0] a9
                    xor       c                             ;[24f1] a9
                    xor       c                             ;[24f2] a9
                    adc       $ef                           ;[24f3] ce ef
                    adc       b                             ;[24f5] 88
                    adc       $88                           ;[24f6] ce 88
                    adc       b                             ;[24f8] 88
                    rst       $28                           ;[24f9] ef
                    rst       $28                           ;[24fa] ef
                    adc       b                             ;[24fb] 88
                    xor       $88                           ;[24fc] ee 88
                    adc       b                             ;[24fe] 88
                    adc       b                             ;[24ff] 88
                    ld        b,(hl)                        ;[2500] 46
                    xor       c                             ;[2501] a9
                    adc       b                             ;[2502] 88
                    ex        de,hl                         ;[2503] eb
                    xor       c                             ;[2504] a9
                    ld        b,a                           ;[2505] 47
                    xor       c                             ;[2506] a9
                    xor       c                             ;[2507] a9
                    rst       $28                           ;[2508] ef
                    xor       c                             ;[2509] a9
                    xor       c                             ;[250a] a9
                    xor       c                             ;[250b] a9
                    xor       $44                           ;[250c] ee 44
                    ld        b,h                           ;[250e] 44
                    ld        b,h                           ;[250f] 44
                    ld        b,h                           ;[2510] 44
                    xor       $21                           ;[2511] ee 21
                    ld        hl,$a121                      ;[2513] 21 21 a1
                    xor       c                             ;[2516] a9
                    ld        b,(hl)                        ;[2517] 46
                    xor       c                             ;[2518] a9
                    xor       d                             ;[2519] aa
                    call      z,$a9ca                       ;[251a] cc ca a9
                    xor       c                             ;[251d] a9
                    adc       b                             ;[251e] 88
                    adc       b                             ;[251f] 88
                    adc       b                             ;[2520] 88
                    adc       b                             ;[2521] 88
                    adc       b                             ;[2522] 88
                    rst       $28                           ;[2523] ef
                    or        c                             ;[2524] b1
                    ei                                      ;[2525] fb
                    or        l                             ;[2526] b5
                    or        c                             ;[2527] b1
                    or        c                             ;[2528] b1
                    or        c                             ;[2529] b1
                    jp        (hl)                          ;[252a] e9
                    xor       l                             ;[252b] ad
                    xor       l                             ;[252c] ad
                    xor       e                             ;[252d] ab
                    xor       e                             ;[252e] ab
                    xor       c                             ;[252f] a9
                    ld        b,(hl)                        ;[2530] 46
                    xor       c                             ;[2531] a9
                    xor       c                             ;[2532] a9
                    xor       c                             ;[2533] a9
                    xor       c                             ;[2534] a9
                    ld        b,(hl)                        ;[2535] 46
                    adc       $a9                           ;[2536] ce a9
                    xor       c                             ;[2538] a9
                    adc       $88                           ;[2539] ce 88
                    adc       b                             ;[253b] 88
                    and       $a9                           ;[253c] e6 a9
                    xor       c                             ;[253e] a9
                    xor       l                             ;[253f] ad
                    ex        de,hl                         ;[2540] eb
                    rst       $20                           ;[2541] e7
                    xor       $a9                           ;[2542] ee a9
                    xor       c                             ;[2544] a9
                    adc       $c9                           ;[2545] ce c9
                    xor       c                             ;[2547] a9
                    ld        h,a                           ;[2548] 67
                    adc       b                             ;[2549] 88
                    ld        b,(hl)                        ;[254a] 46
                    ld        hl,$c629                      ;[254b] 21 29 c6
                    xor       $44                           ;[254e] ee 44
                    ld        b,h                           ;[2550] 44
                    ld        b,h                           ;[2551] 44
                    ld        b,h                           ;[2552] 44
                    ld        b,h                           ;[2553] 44
                    xor       c                             ;[2554] a9
                    xor       c                             ;[2555] a9
                    xor       c                             ;[2556] a9
                    xor       c                             ;[2557] a9
                    xor       c                             ;[2558] a9
                    and       $aa                           ;[2559] e6 aa
                    xor       d                             ;[255b] aa
                    xor       d                             ;[255c] aa
                    xor       d                             ;[255d] aa
                    xor       d                             ;[255e] aa
                    ld        b,h                           ;[255f] 44
                    or        c                             ;[2560] b1
                    or        c                             ;[2561] b1
                    pop       af                            ;[2562] f1
                    push      af                            ;[2563] f5
                    ei                                      ;[2564] fb
                    ld        d,c                           ;[2565] 51
                    xor       d                             ;[2566] aa
                    xor       d                             ;[2567] aa
                    ld        b,h                           ;[2568] 44
                    ld        b,h                           ;[2569] 44
                    xor       d                             ;[256a] aa
                    xor       d                             ;[256b] aa
                    xor       d                             ;[256c] aa
                    xor       d                             ;[256d] aa
                    xor       d                             ;[256e] aa
                    ld        b,h                           ;[256f] 44
                    ld        b,h                           ;[2570] 44
                    ld        b,h                           ;[2571] 44
                    rst       $28                           ;[2572] ef
                    ld        hl,$4442                      ;[2573] 21 42 44
                    adc       b                             ;[2576] 88
                    rst       $28                           ;[2577] ef
                    add       $84                           ;[2578] c6 84
                    add       h                             ;[257a] 84
                    add       h                             ;[257b] 84
                    add       h                             ;[257c] 84
                    add       $00                           ;[257d] c6 00
                    adc       b                             ;[257f] 88
                    ret       z                             ;[2580] c8
                    ld        b,h                           ;[2581] 44
                    ld        h,d                           ;[2582] 62
                    ld        ($2266),hl                    ;[2583] 22 66 22
                    ld        ($2222),hl                    ;[2586] 22 22 22
                    ld        h,(hl)                        ;[2589] 66
                    ld        b,h                           ;[258a] 44
                    xor       $44                           ;[258b] ee 44
                    ld        b,h                           ;[258d] 44
                    ld        b,h                           ;[258e] 44
                    ld        b,h                           ;[258f] 44
                    nop                                     ;[2590] 00
                    nop                                     ;[2591] 00
                    nop                                     ;[2592] 00
                    nop                                     ;[2593] 00
                    nop                                     ;[2594] 00
                    nop                                     ;[2595] 00
                    ld        b,(hl)                        ;[2596] 46
                    xor       c                             ;[2597] a9
                    adc       b                             ;[2598] 88
                    xor       $88                           ;[2599] ee 88
                    rst       $28                           ;[259b] ef
                    nop                                     ;[259c] 00
                    add       $21                           ;[259d] c6 21
                    ld        h,a                           ;[259f] 67
                    xor       c                             ;[25a0] a9
                    ld        b,a                           ;[25a1] 47
                    adc       b                             ;[25a2] 88
                    adc       b                             ;[25a3] 88
                    adc       $a9                           ;[25a4] ce a9
                    xor       c                             ;[25a6] a9
                    adc       $00                           ;[25a7] ce 00
                    ld        h,a                           ;[25a9] 67
                    adc       b                             ;[25aa] 88
                    adc       b                             ;[25ab] 88
                    adc       b                             ;[25ac] 88
                    ld        h,a                           ;[25ad] 67
                    ld        hl,$6721                      ;[25ae] 21 21 67
                    xor       c                             ;[25b1] a9
                    xor       c                             ;[25b2] a9
                    ld        h,a                           ;[25b3] 67
                    nop                                     ;[25b4] 00
                    ld        b,(hl)                        ;[25b5] 46
                    xor       c                             ;[25b6] a9
                    adc       $88                           ;[25b7] ce 88
                    ld        h,a                           ;[25b9] 67
                    ld        h,(hl)                        ;[25ba] 66
                    adc       b                             ;[25bb] 88
                    call      z,$8888                       ;[25bc] cc 88 88
                    adc       b                             ;[25bf] 88
                    nop                                     ;[25c0] 00
                    ld        h,a                           ;[25c1] 67
                    xor       c                             ;[25c2] a9
                    xor       c                             ;[25c3] a9
                    ld        h,a                           ;[25c4] 67
                    ld        hl,$8888                      ;[25c5] 21 88 88
                    adc       $a9                           ;[25c8] ce a9
                    xor       c                             ;[25ca] a9
                    xor       c                             ;[25cb] a9
                    ld        b,h                           ;[25cc] 44
                    nop                                     ;[25cd] 00
                    ld        c,h                           ;[25ce] 4c
                    ld        b,h                           ;[25cf] 44
                    ld        b,h                           ;[25d0] 44
                    ld        c,(hl)                        ;[25d1] 4e
                    ld        hl,$2100                      ;[25d2] 21 00 21
                    ld        hl,$a921                      ;[25d5] 21 21 a9
                    adc       b                             ;[25d8] 88
                    xor       d                             ;[25d9] aa
                    call      z,$aacc                       ;[25da] cc cc aa
                    xor       c                             ;[25dd] a9
                    ld        c,b                           ;[25de] 48
                    ld        c,b                           ;[25df] 48
                    ld        c,b                           ;[25e0] 48
                    ld        c,b                           ;[25e1] 48
                    ld        c,b                           ;[25e2] 48
                    ld        b,(hl)                        ;[25e3] 46
                    nop                                     ;[25e4] 00
                    cp        d                             ;[25e5] ba
                    push      af                            ;[25e6] f5
                    or        l                             ;[25e7] b5
                    or        l                             ;[25e8] b5
                    or        l                             ;[25e9] b5
                    nop                                     ;[25ea] 00
                    adc       $a9                           ;[25eb] ce a9
                    xor       c                             ;[25ed] a9
                    xor       c                             ;[25ee] a9
                    xor       c                             ;[25ef] a9
                    nop                                     ;[25f0] 00
                    ld        b,(hl)                        ;[25f1] 46
                    xor       c                             ;[25f2] a9
                    xor       c                             ;[25f3] a9
                    xor       c                             ;[25f4] a9
                    ld        b,(hl)                        ;[25f5] 46
                    nop                                     ;[25f6] 00
                    adc       $a9                           ;[25f7] ce a9
                    xor       c                             ;[25f9] a9
                    adc       $88                           ;[25fa] ce 88
                    nop                                     ;[25fc] 00
                    ld        h,a                           ;[25fd] 67
                    xor       c                             ;[25fe] a9
                    xor       c                             ;[25ff] a9
                    ld        h,a                           ;[2600] 67
                    ld        hl,$6700                      ;[2601] 21 00 67
                    adc       b                             ;[2604] 88
                    adc       b                             ;[2605] 88
                    adc       b                             ;[2606] 88
                    adc       b                             ;[2607] 88
                    nop                                     ;[2608] 00
                    ld        h,a                           ;[2609] 67
                    adc       b                             ;[260a] 88
                    ld        b,(hl)                        ;[260b] 46
                    ld        hl,$44ce                      ;[260c] 21 ce 44
                    xor       $44                           ;[260f] ee 44
                    ld        b,h                           ;[2611] 44
                    ld        b,h                           ;[2612] 44
                    ld        b,e                           ;[2613] 43
                    nop                                     ;[2614] 00
                    xor       c                             ;[2615] a9
                    xor       c                             ;[2616] a9
                    xor       c                             ;[2617] a9
                    xor       c                             ;[2618] a9
                    and       $00                           ;[2619] e6 00
                    xor       d                             ;[261b] aa
                    xor       d                             ;[261c] aa
                    xor       d                             ;[261d] aa
                    xor       d                             ;[261e] aa
                    ld        b,h                           ;[261f] 44
                    nop                                     ;[2620] 00
                    or        c                             ;[2621] b1
                    or        l                             ;[2622] b5
                    push      af                            ;[2623] f5
                    push      af                            ;[2624] f5
                    ld        c,d                           ;[2625] 4a
                    nop                                     ;[2626] 00
                    xor       d                             ;[2627] aa
                    xor       d                             ;[2628] aa
                    ld        b,h                           ;[2629] 44
                    xor       d                             ;[262a] aa
                    xor       d                             ;[262b] aa
                    nop                                     ;[262c] 00
                    xor       c                             ;[262d] a9
                    xor       c                             ;[262e] a9
                    xor       c                             ;[262f] a9
                    ld        h,a                           ;[2630] 67
                    ld        hl,$ef00                      ;[2631] 21 00 ef
                    ld        hl,$8442                      ;[2634] 21 42 84
                    rst       $28                           ;[2637] ef
                    ld        h,(hl)                        ;[2638] 66
                    ld        b,h                           ;[2639] 44
                    adc       b                             ;[263a] 88
                    ld        b,h                           ;[263b] 44
                    ld        b,h                           ;[263c] 44
                    ld        h,(hl)                        ;[263d] 66
                    ld        b,h                           ;[263e] 44
                    ld        b,h                           ;[263f] 44
                    ld        b,h                           ;[2640] 44
                    ld        b,h                           ;[2641] 44
                    ld        b,h                           ;[2642] 44
                    ld        b,h                           ;[2643] 44
                    call      z,$2244                       ;[2644] cc 44 22
                    ld        b,h                           ;[2647] 44
                    ld        b,h                           ;[2648] 44
                    call      z,$aa45                       ;[2649] cc 45 aa
                    nop                                     ;[264c] 00
                    nop                                     ;[264d] 00
                    nop                                     ;[264e] 00
                    nop                                     ;[264f] 00
                    xor       $b1                           ;[2650] ee b1
                    di                                      ;[2652] f3
                    or        l                             ;[2653] b5
                    di                                      ;[2654] f3
                    or        c                             ;[2655] b1
                    ld        e,h                           ;[2656] 5c
                    ld        b,d                           ;[2657] 42
                    cpl                                     ;[2658] 2f
                    jr        nz,$267c                      ;[2659] 20 21
                    rst       $38                           ;[265b] ff
                    add       hl,de                         ;[265c] 19
                    add       $16                           ;[265d] c6 16
                    ld        b,(hl)                        ;[265f] 46
                    djnz      $25ea                         ;[2660] 10 88
                    rrca                                    ;[2662] 0f
                    ld        hl,$c607                      ;[2663] 21 07 c6
                    ld        bc,$cdee                      ;[2666] 01 ee cd
                    inc       l                             ;[2669] 2c
                    daa                                     ;[266a] 27
                    call      $2671                         ;[266b] cd 71 26
                    jp        $27bb                         ;[266e] c3 bb 27
                    ld        hl,$230a                      ;[2671] 21 0a 23
                    ld        de,$f30d                      ;[2674] 11 0d f3
                    ld        bc,$0014                      ;[2677] 01 14 00
                    ldir                                    ;[267a] ed b0
                    call      $26aa                         ;[267c] cd aa 26
                    ld        de,$f40d                      ;[267f] 11 0d f4
                    ld        bc,$232d                      ;[2682] 01 2d 23
                    call      $2697                         ;[2685] cd 97 26
                    ld        de,$fb0d                      ;[2688] 11 0d fb
                    call      $2697                         ;[268b] cd 97 26
                    ld        de,$f70d                      ;[268e] 11 0d f7
                    call      $2697                         ;[2691] cd 97 26
                    ld        de,$ff0d                      ;[2694] 11 0d ff
                    push      hl                            ;[2697] e5
                    push      bc                            ;[2698] c5
                    ld        bc,$000f                      ;[2699] 01 0f 00
                    ldir                                    ;[269c] ed b0
                    pop       hl                            ;[269e] e1
                    ld        c,$05                         ;[269f] 0e 05
                    ldir                                    ;[26a1] ed b0
                    call      $26aa                         ;[26a3] cd aa 26
                    ld        b,h                           ;[26a6] 44
                    ld        c,l                           ;[26a7] 4d
                    pop       hl                            ;[26a8] e1
                    ret                                     ;[26a9] c9

                    ld        b,$0f                         ;[26aa] 06 0f
                    xor       a                             ;[26ac] af
                    ld        (de),a                        ;[26ad] 12
                    inc       de                            ;[26ae] 13
                    djnz      $26ad                         ;[26af] 10 fc
                    ret                                     ;[26b1] c9

                    scf                                     ;[26b2] 37
                    bit       1,(iy+$45)                    ;[26b3] fd cb 45 4e
                    jr        z,$26e5                       ;[26b7] 28 2c
                    push      de                            ;[26b9] d5
                    push      af                            ;[26ba] f5
                    ld        a,h                           ;[26bb] 7c
                    rlca                                    ;[26bc] 07
                    rlca                                    ;[26bd] 07
                    rlca                                    ;[26be] 07
                    and       $07                           ;[26bf] e6 07
                    ld        e,a                           ;[26c1] 5f
                    ld        a,h                           ;[26c2] 7c
                    and       $1f                           ;[26c3] e6 1f
                    or        $c0                           ;[26c5] f6 c0
                    ld        h,a                           ;[26c7] 67
                    ld        bc,$243b                      ;[26c8] 01 3b 24
                    ld        d,$13                         ;[26cb] 16 13
                    out       (c),d                         ;[26cd] ed 51
                    inc       b                             ;[26cf] 04
                    in        a,(c)                         ;[26d0] ed 78
                    add       a                             ;[26d2] 87
                    add       e                             ;[26d3] 83
                    ld        e,a                           ;[26d4] 5f
                    nextreg $56,a                           ;[26d5] ed 92 56
                    pop       af                            ;[26d8] f1
                    jr        nc,$26e0                      ;[26d9] 30 05
                    ld        a,e                           ;[26db] 7b
                    inc       a                             ;[26dc] 3c
                    nextreg $57,a                           ;[26dd] ed 92 57
                    pop       de                            ;[26e0] d1
                    ld        bc,$e0ff                      ;[26e1] 01 ff e0
                    ret                                     ;[26e4] c9

                    srl       h                             ;[26e5] cb 3c
                    jr        nc,$26eb                      ;[26e7] 30 02
                    set       7,l                           ;[26e9] cb fd
                    set       6,h                           ;[26eb] cb f4
                    ld        bc,$587f                      ;[26ed] 01 7f 58
                    ld        a,h                           ;[26f0] 7c
                    cp        b                             ;[26f1] b8
                    ret       c                             ;[26f2] d8
                    add       $68                           ;[26f3] c6 68
                    ld        h,a                           ;[26f5] 67
                    ld        b,$d8                         ;[26f6] 06 d8
                    ret                                     ;[26f8] c9

                    scf                                     ;[26f9] 37
                    bit       7,c                           ;[26fa] cb 79
                    jr        z,$2715                       ;[26fc] 28 17
                    res       5,h                           ;[26fe] cb ac
                    ld        a,$56                         ;[2700] 3e 56
                    push      bc                            ;[2702] c5
                    ld        bc,$243b                      ;[2703] 01 3b 24
                    out       (c),a                         ;[2706] ed 79
                    inc       b                             ;[2708] 04
                    in        a,(c)                         ;[2709] ed 78
                    inc       a                             ;[270b] 3c
                    out       (c),a                         ;[270c] ed 79
                    pop       bc                            ;[270e] c1
                    ret       nc                            ;[270f] d0
                    ld        a,$57                         ;[2710] 3e 57
                    and       a                             ;[2712] a7
                    jr        $2702                         ;[2713] 18 ed
                    ld        a,h                           ;[2715] 7c
                    add       $68                           ;[2716] c6 68
                    ld        h,a                           ;[2718] 67
                    ld        b,$d8                         ;[2719] 06 d8
                    ret                                     ;[271b] c9

                    ld        e,a                           ;[271c] 5f
                    ld        a,($5c7f)                     ;[271d] 3a 7f 5c
                    and       $0f                           ;[2720] e6 0f
                    ret       z                             ;[2722] c8
                    cp        $03                           ;[2723] fe 03
                    ret       c                             ;[2725] d8
                    srl       a                             ;[2726] cb 3f
                    srl       a                             ;[2728] cb 3f
                    dec       a                             ;[272a] 3d
                    ret                                     ;[272b] c9

                    ld        d,$56                         ;[272c] 16 56
                    pop       bc                            ;[272e] c1
                    ld        hl,$0000                      ;[272f] 21 00 00
                    add       hl,sp                         ;[2732] 39
                    ld        a,h                           ;[2733] 7c
                    cp        $5c                           ;[2734] fe 5c
                    jr        c,$2740                       ;[2736] 38 08
                    ld        hl,($5b6a)                    ;[2738] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[273b] ed 73 6a 5b
                    ld        sp,hl                         ;[273f] f9
                    ld        ($5b8e),a                     ;[2740] 32 8e 5b
                    push      bc                            ;[2743] c5
                    call      $27de                         ;[2744] cd de 27
                    ld        ($5b8a),hl                    ;[2747] 22 8a 5b
                    ret                                     ;[274a] c9

                    ld        e,$80                         ;[274b] 1e 80
                    jr        $2751                         ;[274d] 18 02
                    ld        e,$ff                         ;[274f] 1e ff
                    ld        ($5b8f),hl                    ;[2751] 22 8f 5b
                    call      $272c                         ;[2754] cd 2c 27
                    call      $27c2                         ;[2757] cd c2 27
                    ld        hl,($5b8f)                    ;[275a] 2a 8f 5b
                    ld        d,$ff                         ;[275d] 16 ff
                    bit       7,e                           ;[275f] cb 7b
                    jr        nz,$2766                      ;[2761] 20 03
                    ld        d,e                           ;[2763] 53
                    ld        e,$ff                         ;[2764] 1e ff
                    ld        a,(hl)                        ;[2766] 7e
                    inc       hl                            ;[2767] 23
                    cp        e                             ;[2768] bb
                    jr        c,$2772                       ;[2769] 38 07
                    ld        d,$01                         ;[276b] 16 01
                    inc       e                             ;[276d] 1c
                    jr        z,$2792                       ;[276e] 28 22
                    res       7,a                           ;[2770] cb bf
                    push      hl                            ;[2772] e5
                    push      de                            ;[2773] d5
                    ld        e,a                           ;[2774] 5f
                    call      $1d40                         ;[2775] cd 40 1d
                    pop       de                            ;[2778] d1
                    pop       hl                            ;[2779] e1
                    dec       d                             ;[277a] 15
                    jr        nz,$2766                      ;[277b] 20 e9
                    jr        $2792                         ;[277d] 18 13
                    ld        e,a                           ;[277f] 5f
                    call      $272c                         ;[2780] cd 2c 27
                    call      $27c2                         ;[2783] cd c2 27
                    call      $1d40                         ;[2786] cd 40 1d
                    jr        $2792                         ;[2789] 18 07
                    ld        e,a                           ;[278b] 5f
                    call      $272c                         ;[278c] cd 2c 27
                    call      $1d47                         ;[278f] cd 47 1d
                    ld        hl,($5b8a)                    ;[2792] 2a 8a 5b
                    ld        a,l                           ;[2795] 7d
                    nextreg $56,a                           ;[2796] ed 92 56
                    ld        a,h                           ;[2799] 7c
                    nextreg $57,a                           ;[279a] ed 92 57
                    ld        a,($5b8e)                     ;[279d] 3a 8e 5b
                    cp        $5c                           ;[27a0] fe 5c
                    ret       c                             ;[27a2] d8
                    ld        hl,($5b6a)                    ;[27a3] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[27a6] ed 73 6a 5b
                    ld        sp,hl                         ;[27aa] f9
                    ret                                     ;[27ab] c9

                    ld        a,b                           ;[27ac] 78
                    or        c                             ;[27ad] b1
                    ret       z                             ;[27ae] c8
                    ld        a,(de)                        ;[27af] 1a
                    push      bc                            ;[27b0] c5
                    push      de                            ;[27b1] d5
                    call      $277f                         ;[27b2] cd 7f 27
                    pop       de                            ;[27b5] d1
                    pop       bc                            ;[27b6] c1
                    inc       de                            ;[27b7] 13
                    dec       bc                            ;[27b8] 0b
                    jr        $27ac                         ;[27b9] 18 f1
                    ld        d,$56                         ;[27bb] 16 56
                    call      $27d9                         ;[27bd] cd d9 27
                    jr        $279d                         ;[27c0] 18 db
                    ld        a,(iy+$45)                    ;[27c2] fd 7e 45
                    and       $0f                           ;[27c5] e6 0f
                    cp        (ix+$1e)                      ;[27c7] dd be 1e
                    ret       z                             ;[27ca] c8
                    xor       (ix+$1e)                      ;[27cb] dd ae 1e
                    cp        $05                           ;[27ce] fe 05
                    ret       z                             ;[27d0] c8
                    ld        a,$5e                         ;[27d1] 3e 5e
                    rst       $30                           ;[27d3] f7
                    rst       $18                           ;[27d4] df
                    sub       $0d                           ;[27d5] d6 0d
                    ld        d,$56                         ;[27d7] 16 56
                    ld        hl,($5b8a)                    ;[27d9] 2a 8a 5b
                    jr        $27e1                         ;[27dc] 18 03
                    ld        hl,$100b                      ;[27de] 21 0b 10
                    push      bc                            ;[27e1] c5
                    ld        bc,$243b                      ;[27e2] 01 3b 24
                    out       (c),d                         ;[27e5] ed 51
                    inc       b                             ;[27e7] 04
                    in        a,(c)                         ;[27e8] ed 78
                    out       (c),l                         ;[27ea] ed 69
                    ld        l,a                           ;[27ec] 6f
                    dec       b                             ;[27ed] 05
                    inc       d                             ;[27ee] 14
                    out       (c),d                         ;[27ef] ed 51
                    inc       b                             ;[27f1] 04
                    in        a,(c)                         ;[27f2] ed 78
                    out       (c),h                         ;[27f4] ed 61
                    ld        h,a                           ;[27f6] 67
                    pop       bc                            ;[27f7] c1
                    ret                                     ;[27f8] c9

                    set       7,(iy+$37)                    ;[27f9] fd cb 37 fe
                    and       a                             ;[27fd] a7
                    jr        z,$2803                       ;[27fe] 28 03
                    ld        c,a                           ;[2800] 4f
                    ld        b,$00                         ;[2801] 06 00
                    bit       7,b                           ;[2803] cb 78
                    jr        z,$2816                       ;[2805] 28 0f
                    ld        ix,$0000                      ;[2807] dd 21 00 00
                    ld        a,(iy+$45)                    ;[280b] fd 7e 45
                    and       $0f                           ;[280e] e6 0f
                    jr        z,$2816                       ;[2810] 28 04
                    add       $f2                           ;[2812] c6 f2
                    ld        ixh,a                         ;[2814] dd 67
                    ld        a,c                           ;[2816] 79
                    and       a                             ;[2817] a7
                    ret       z                             ;[2818] c8
                    ld        a,e                           ;[2819] 7b
                    bit       2,b                           ;[281a] cb 50
                    jr        z,$281f                       ;[281c] 28 01
                    ld        a,d                           ;[281e] 7a
                    ld        ($5b9a),a                     ;[281f] 32 9a 5b
                    ld        ($5b8f),hl                    ;[2822] 22 8f 5b
                    ld        hl,$5c71                      ;[2825] 21 71 5c
                    ld        a,(hl)                        ;[2828] 7e
                    and       $e3                           ;[2829] e6 e3
                    ld        (hl),a                        ;[282b] 77
                    ld        a,b                           ;[282c] 78
                    and       $10                           ;[282d] e6 10
                    or        (hl)                          ;[282f] b6
                    ld        (hl),a                        ;[2830] 77
                    ld        a,b                           ;[2831] 78
                    add       a                             ;[2832] 87
                    add       a                             ;[2833] 87
                    and       $0c                           ;[2834] e6 0c
                    or        (hl)                          ;[2836] b6
                    ld        (hl),a                        ;[2837] 77
                    ld        a,b                           ;[2838] 78
                    swapnib                                 ;[2839] ed 23
                    and       $06                           ;[283b] e6 06
                    ld        b,a                           ;[283d] 47
                    ld        hl,$5c3c                      ;[283e] 21 3c 5c
                    ld        a,(hl)                        ;[2841] 7e
                    and       $f9                           ;[2842] e6 f9
                    or        b                             ;[2844] b0
                    ld        (hl),a                        ;[2845] 77
                    ld        b,e                           ;[2846] 43
                    ld        a,c                           ;[2847] 79
                    cp        b                             ;[2848] b8
                    jr        nc,$284c                      ;[2849] 30 01
                    ld        b,c                           ;[284b] 41
                    ld        ($5b9b),bc                    ;[284c] ed 43 9b 5b
                    call      $272c                         ;[2850] cd 2c 27
                    inc       ixh                           ;[2853] dd 24
                    dec       ixh                           ;[2855] dd 25
                    call      nz,$27c2                      ;[2857] c4 c2 27
                    ld        (ix+$1b),$00                  ;[285a] dd 36 1b 00
                    ld        a,($5b9a)                     ;[285e] 3a 9a 5b
                    ld        d,a                           ;[2861] 57
                    ld        hl,$5c71                      ;[2862] 21 71 5c
                    bit       4,(hl)                        ;[2865] cb 66
                    res       4,(hl)                        ;[2867] cb a6
                    ld        bc,($5b9b)                    ;[2869] ed 4b 9b 5b
                    ld        hl,($5b8f)                    ;[286d] 2a 8f 5b
                    jr        z,$2878                       ;[2870] 28 06
                    call      $2b1d                         ;[2872] cd 1d 2b
                    call      nz,$2aa9                      ;[2875] c4 a9 2a
                    ld        a,$0d                         ;[2878] 3e 0d
                    ld        ($5b9b),a                     ;[287a] 32 9b 5b
                    inc       b                             ;[287d] 04
                    dec       b                             ;[287e] 05
                    jr        nz,$2891                      ;[287f] 20 10
                    ld        a,($5c71)                     ;[2881] 3a 71 5c
                    and       $c0                           ;[2884] e6 c0
                    jr        nz,$2891                      ;[2886] 20 09
                    ld        (hl),$22                      ;[2888] 36 22
                    inc       hl                            ;[288a] 23
                    ld        (hl),$22                      ;[288b] 36 22
                    dec       hl                            ;[288d] 2b
                    inc       d                             ;[288e] 14
                    ld        b,$02                         ;[288f] 06 02
                    xor       a                             ;[2891] af
                    push      af                            ;[2892] f5
                    ld        hl,($5b8f)                    ;[2893] 2a 8f 5b
                    ld        e,$00                         ;[2896] 1e 00
                    ld        a,d                           ;[2898] 7a
                    call      $2b0f                         ;[2899] cd 0f 2b
                    bit       4,(iy+$37)                    ;[289c] fd cb 37 66
                    call      z,$2acd                       ;[28a0] cc cd 2a
                    ld        a,b                           ;[28a3] 78
                    sub       d                             ;[28a4] 92
                    call      $2b0f                         ;[28a5] cd 0f 2b
                    pop       af                            ;[28a8] f1
                    sub       e                             ;[28a9] 93
                    call      nc,$2a9a                      ;[28aa] d4 9a 2a
                    bit       4,(iy+$37)                    ;[28ad] fd cb 37 66
                    jr        nz,$28e2                      ;[28b1] 20 2f
                    push      bc                            ;[28b3] c5
                    push      de                            ;[28b4] d5
                    call      $0ce2                         ;[28b5] cd e2 0c
                    pop       de                            ;[28b8] d1
                    pop       bc                            ;[28b9] c1
                    ld        hl,$2965                      ;[28ba] 21 65 29
                    call      $0ffc                         ;[28bd] cd fc 0f
                    jr        z,$28d4                       ;[28c0] 28 12
                    cp        $20                           ;[28c2] fe 20
                    jr        nc,$290e                      ;[28c4] 30 48
                    ld        hl,$5c71                      ;[28c6] 21 71 5c
                    bit       3,(hl)                        ;[28c9] cb 5e
                    jr        z,$28db                       ;[28cb] 28 0e
                    ld        ($5b9b),a                     ;[28cd] 32 9b 5b
                    set       4,(hl)                        ;[28d0] cb e6
                    res       2,(hl)                        ;[28d2] cb 96
                    push      bc                            ;[28d4] c5
                    push      de                            ;[28d5] d5
                    call      nc,$3e18                      ;[28d6] d4 18 3e
                    pop       de                            ;[28d9] d1
                    pop       bc                            ;[28da] c1
                    ld        a,e                           ;[28db] 7b
                    push      af                            ;[28dc] f5
                    call      $2aa9                         ;[28dd] cd a9 2a
                    jr        $2893                         ;[28e0] 18 b1
                    ld        e,b                           ;[28e2] 58
                    ld        a,(ix+$18)                    ;[28e3] dd 7e 18
                    sub       (ix+$22)                      ;[28e6] dd 96 22
                    srl       a                             ;[28e9] cb 3f
                    srl       a                             ;[28eb] cb 3f
                    srl       a                             ;[28ed] cb 3f
                    ld        b,a                           ;[28ef] 47
                    ld        a,(ix+$1a)                    ;[28f0] dd 7e 1a
                    sub       b                             ;[28f3] 90
                    inc       a                             ;[28f4] 3c
                    ld        (ix+$1b),a                    ;[28f5] dd 77 1b
                    ld        a,(iy+$37)                    ;[28f8] fd 7e 37
                    rra                                     ;[28fb] 1f
                    rra                                     ;[28fc] 1f
                    and       $03                           ;[28fd] e6 03
                    ld        b,a                           ;[28ff] 47
                    ld        a,($5b9b)                     ;[2900] 3a 9b 5b
                    ld        c,a                           ;[2903] 4f
                    push      de                            ;[2904] d5
                    ld        d,$56                         ;[2905] 16 56
                    call      $27d9                         ;[2907] cd d9 27
                    pop       de                            ;[290a] d1
                    jp        $279d                         ;[290b] c3 9d 27
                    push      af                            ;[290e] f5
                    cp        $20                           ;[290f] fe 20
                    jr        nz,$291b                      ;[2911] 20 08
                    ld        a,$fe                         ;[2913] 3e fe
                    in        a,($fe)                       ;[2915] db fe
                    rra                                     ;[2917] 1f
                    call      nc,$294e                      ;[2918] d4 4e 29
                    pop       af                            ;[291b] f1
                    ld        h,a                           ;[291c] 67
                    cp        $80                           ;[291d] fe 80
                    jr        c,$2927                       ;[291f] 38 06
                    bit       1,(iy+$02)                    ;[2921] fd cb 02 4e
                    jr        z,$28db                       ;[2925] 28 b4
                    ld        a,b                           ;[2927] 78
                    cp        c                             ;[2928] b9
                    jr        nc,$28d4                      ;[2929] 30 a9
                    push      hl                            ;[292b] e5
                    ld        hl,($5b8f)                    ;[292c] 2a 8f 5b
                    ld        a,d                           ;[292f] 7a
                    add       hl,a                          ;[2930] ed 31
                    inc       b                             ;[2932] 04
                    inc       d                             ;[2933] 14
                    ld        a,c                           ;[2934] 79
                    sub       d                             ;[2935] 92
                    jr        z,$2947                       ;[2936] 28 0f
                    push      bc                            ;[2938] c5
                    push      de                            ;[2939] d5
                    push      hl                            ;[293a] e5
                    ld        c,a                           ;[293b] 4f
                    ld        b,$00                         ;[293c] 06 00
                    add       hl,bc                         ;[293e] 09
                    ld        d,h                           ;[293f] 54
                    ld        e,l                           ;[2940] 5d
                    dec       hl                            ;[2941] 2b
                    lddr                                    ;[2942] ed b8
                    pop       hl                            ;[2944] e1
                    pop       de                            ;[2945] d1
                    pop       bc                            ;[2946] c1
                    pop       af                            ;[2947] f1
                    ld        (hl),a                        ;[2948] 77
                    call      $29f6                         ;[2949] cd f6 29
                    jr        $28db                         ;[294c] 18 8d
                    bit       2,(iy+$02)                    ;[294e] fd cb 02 56
                    ret       z                             ;[2952] c8
                    rst       $18                           ;[2953] df
                    ld        (hl),l                        ;[2954] 75
                    ld        a,$c0                         ;[2955] 3e c0
                    call      $27d7                         ;[2957] cd d7 27
                    ld        hl,($5b6a)                    ;[295a] 2a 6a 5b
                    ld        ($5b6a),sp                    ;[295d] ed 73 6a 5b
                    ld        sp,hl                         ;[2961] f9
                    rst       $18                           ;[2962] df
                    ld        e,$00                         ;[2963] 1e 00
                    nop                                     ;[2965] 00
                    jp        z,$0329                       ;[2966] ca 29 03
                    adc       d                             ;[2969] 8a
                    ld        hl,($1e04)                    ;[296a] 2a 04 1e
                    ld        hl,($3905)                    ;[296d] 2a 05 39
                    ld        hl,($9907)                    ;[2970] 2a 07 99
                    add       hl,hl                         ;[2973] 29
                    ex        af,af'                        ;[2974] 08
                    sbc       (hl)                          ;[2975] 9e
                    add       hl,hl                         ;[2976] 29
                    add       hl,bc                         ;[2977] 09
                    and       h                             ;[2978] a4
                    add       hl,hl                         ;[2979] 29
                    ld        a,(bc)                        ;[297a] 0a
                    cp        d                             ;[297b] ba
                    add       hl,hl                         ;[297c] 29
                    dec       bc                            ;[297d] 0b
                    xor       d                             ;[297e] aa
                    add       hl,hl                         ;[297f] 29
                    inc       c                             ;[2980] 0c
                    in        a,($29)                       ;[2981] db 29
                    dec       c                             ;[2983] 0d
                    ld        bc,$102a                      ;[2984] 01 2a 10
                    sub       c                             ;[2987] 91
                    ld        hl,($9411)                    ;[2988] 2a 11 94
                    ld        hl,($d718)                    ;[298b] 2a 18 d7
                    add       hl,hl                         ;[298e] 29
                    dec       de                            ;[298f] 1b
                    ld        d,h                           ;[2990] 54
                    ld        hl,($6f1c)                    ;[2991] 2a 1c 6f
                    ld        hl,($d01d)                    ;[2994] 2a 1d d0
                    add       hl,hl                         ;[2997] 29
                    rst       $38                           ;[2998] ff
                    ld        b,$00                         ;[2999] 06 00
                    ld        d,b                           ;[299b] 50
                    scf                                     ;[299c] 37
                    ret                                     ;[299d] c9

                    dec       d                             ;[299e] 15
                    scf                                     ;[299f] 37
                    ret       p                             ;[29a0] f0
                    inc       d                             ;[29a1] 14
                    and       a                             ;[29a2] a7
                    ret                                     ;[29a3] c9

                    ld        a,d                           ;[29a4] 7a
                    cp        b                             ;[29a5] b8
                    ret       z                             ;[29a6] c8
                    inc       d                             ;[29a7] 14
                    scf                                     ;[29a8] 37
                    ret                                     ;[29a9] c9

                    call      $299e                         ;[29aa] cd 9e 29
                    ret       nc                            ;[29ad] d0
                    call      $16ae                         ;[29ae] cd ae 16
                    sbc       d                             ;[29b1] 9a
                    neg                                     ;[29b2] ed 44
                    ld        d,$00                         ;[29b4] 16 00
                    scf                                     ;[29b6] 37
                    ret       m                             ;[29b7] f8
                    ld        d,a                           ;[29b8] 57
                    ret                                     ;[29b9] c9

                    call      $29a4                         ;[29ba] cd a4 29
                    ret       nc                            ;[29bd] d0
                    call      $16ae                         ;[29be] cd ae 16
                    add       d                             ;[29c1] 82
                    dec       a                             ;[29c2] 3d
                    ld        d,b                           ;[29c3] 50
                    cp        b                             ;[29c4] b8
                    ccf                                     ;[29c5] 3f
                    ret       c                             ;[29c6] d8
                    scf                                     ;[29c7] 37
                    ld        d,a                           ;[29c8] 57
                    ret                                     ;[29c9] c9

                    ld        a,d                           ;[29ca] 7a
                    cp        b                             ;[29cb] b8
                    ret       z                             ;[29cc] c8
                    ld        d,b                           ;[29cd] 50
                    scf                                     ;[29ce] 37
                    ret                                     ;[29cf] c9

                    ld        a,d                           ;[29d0] 7a
                    and       a                             ;[29d1] a7
                    ret       z                             ;[29d2] c8
                    ld        d,$00                         ;[29d3] 16 00
                    scf                                     ;[29d5] 37
                    ret                                     ;[29d6] c9

                    ld        a,d                           ;[29d7] 7a
                    cp        b                             ;[29d8] b8
                    ret       z                             ;[29d9] c8
                    inc       d                             ;[29da] 14
                    ld        a,d                           ;[29db] 7a
                    and       a                             ;[29dc] a7
                    ret       z                             ;[29dd] c8
                    push      bc                            ;[29de] c5
                    push      de                            ;[29df] d5
                    ld        hl,($5b8f)                    ;[29e0] 2a 8f 5b
                    add       hl,a                          ;[29e3] ed 31
                    ld        d,h                           ;[29e5] 54
                    ld        e,l                           ;[29e6] 5d
                    dec       de                            ;[29e7] 1b
                    ld        a,b                           ;[29e8] 78
                    sub       d                             ;[29e9] 92
                    jr        z,$29f1                       ;[29ea] 28 05
                    ld        c,b                           ;[29ec] 48
                    ld        b,$00                         ;[29ed] 06 00
                    ldir                                    ;[29ef] ed b0
                    pop       de                            ;[29f1] d1
                    pop       bc                            ;[29f2] c1
                    dec       b                             ;[29f3] 05
                    dec       d                             ;[29f4] 15
                    scf                                     ;[29f5] 37
                    ld        hl,$5c71                      ;[29f6] 21 71 5c
                    bit       2,(hl)                        ;[29f9] cb 56
                    ret       z                             ;[29fb] c8
                    set       4,(hl)                        ;[29fc] cb e6
                    res       3,(hl)                        ;[29fe] cb 9e
                    ret                                     ;[2a00] c9

                    ld        hl,$5c71                      ;[2a01] 21 71 5c
                    res       2,(hl)                        ;[2a04] cb 96
                    scf                                     ;[2a06] 37
                    jr        $29fc                         ;[2a07] 18 f3
                    ld        hl,($5b8f)                    ;[2a09] 2a 8f 5b
                    ld        a,d                           ;[2a0c] 7a
                    add       hl,a                          ;[2a0d] ed 31
                    dec       hl                            ;[2a0f] 2b
                    ld        a,(hl)                        ;[2a10] 7e
                    cp        $20                           ;[2a11] fe 20
                    ret                                     ;[2a13] c9

                    ld        hl,($5b8f)                    ;[2a14] 2a 8f 5b
                    ld        a,d                           ;[2a17] 7a
                    add       hl,a                          ;[2a18] ed 31
                    ld        a,(hl)                        ;[2a1a] 7e
                    cp        $20                           ;[2a1b] fe 20
                    ret                                     ;[2a1d] c9

                    ld        a,d                           ;[2a1e] 7a
                    and       a                             ;[2a1f] a7
                    ret       z                             ;[2a20] c8
                    call      $2a09                         ;[2a21] cd 09 2a
                    jr        nz,$2a2d                      ;[2a24] 20 07
                    call      $299e                         ;[2a26] cd 9e 29
                    jr        c,$2a21                       ;[2a29] 38 f6
                    scf                                     ;[2a2b] 37
                    ret                                     ;[2a2c] c9

                    call      $2a09                         ;[2a2d] cd 09 2a
                    scf                                     ;[2a30] 37
                    ret       z                             ;[2a31] c8
                    call      $299e                         ;[2a32] cd 9e 29
                    jr        c,$2a2d                       ;[2a35] 38 f6
                    scf                                     ;[2a37] 37
                    ret                                     ;[2a38] c9

                    ld        a,d                           ;[2a39] 7a
                    cp        b                             ;[2a3a] b8
                    ret       z                             ;[2a3b] c8
                    call      $2a14                         ;[2a3c] cd 14 2a
                    jr        z,$2a48                       ;[2a3f] 28 07
                    call      $29a4                         ;[2a41] cd a4 29
                    jr        c,$2a3c                       ;[2a44] 38 f6
                    scf                                     ;[2a46] 37
                    ret                                     ;[2a47] c9

                    call      $2a14                         ;[2a48] cd 14 2a
                    scf                                     ;[2a4b] 37
                    ret       nz                            ;[2a4c] c0
                    call      $29a4                         ;[2a4d] cd a4 29
                    jr        c,$2a48                       ;[2a50] 38 f6
                    scf                                     ;[2a52] 37
                    ret                                     ;[2a53] c9

                    ld        a,d                           ;[2a54] 7a
                    and       a                             ;[2a55] a7
                    ret       z                             ;[2a56] c8
                    call      $2a09                         ;[2a57] cd 09 2a
                    jr        nz,$2a63                      ;[2a5a] 20 07
                    call      $29db                         ;[2a5c] cd db 29
                    jr        c,$2a57                       ;[2a5f] 38 f6
                    scf                                     ;[2a61] 37
                    ret                                     ;[2a62] c9

                    call      $2a09                         ;[2a63] cd 09 2a
                    scf                                     ;[2a66] 37
                    ret       z                             ;[2a67] c8
                    call      $29db                         ;[2a68] cd db 29
                    jr        c,$2a63                       ;[2a6b] 38 f6
                    scf                                     ;[2a6d] 37
                    ret                                     ;[2a6e] c9

                    ld        a,d                           ;[2a6f] 7a
                    cp        b                             ;[2a70] b8
                    ret       z                             ;[2a71] c8
                    call      $2a14                         ;[2a72] cd 14 2a
                    jr        z,$2a7e                       ;[2a75] 28 07
                    call      $29d7                         ;[2a77] cd d7 29
                    jr        c,$2a72                       ;[2a7a] 38 f6
                    scf                                     ;[2a7c] 37
                    ret                                     ;[2a7d] c9

                    call      $2a14                         ;[2a7e] cd 14 2a
                    scf                                     ;[2a81] 37
                    ret       nz                            ;[2a82] c0
                    call      $29d7                         ;[2a83] cd d7 29
                    jr        c,$2a7e                       ;[2a86] 38 f6
                    scf                                     ;[2a88] 37
                    ret                                     ;[2a89] c9

                    call      $29db                         ;[2a8a] cd db 29
                    ccf                                     ;[2a8d] 3f
                    ret       c                             ;[2a8e] d8
                    jr        $2a8a                         ;[2a8f] 18 f9
                    ld        b,d                           ;[2a91] 42
                    scf                                     ;[2a92] 37
                    ret                                     ;[2a93] c9

                    call      $0068                         ;[2a94] cd 68 00
                    rst       $38                           ;[2a97] ff
                    inc       de                            ;[2a98] 13
                    ret                                     ;[2a99] c9

                    ret       z                             ;[2a9a] c8
                    push      de                            ;[2a9b] d5
                    ld        e,a                           ;[2a9c] 5f
                    push      de                            ;[2a9d] d5
                    ld        a,$20                         ;[2a9e] 3e 20
                    call      $2ac2                         ;[2aa0] cd c2 2a
                    pop       de                            ;[2aa3] d1
                    call      $2aa9                         ;[2aa4] cd a9 2a
                    pop       de                            ;[2aa7] d1
                    ret                                     ;[2aa8] c9

                    inc       e                             ;[2aa9] 1c
                    dec       e                             ;[2aaa] 1d
                    ret       z                             ;[2aab] c8
                    inc       ixh                           ;[2aac] dd 24
                    dec       ixh                           ;[2aae] dd 25
                    ld        a,$08                         ;[2ab0] 3e 08
                    jr        z,$2ac2                       ;[2ab2] 28 0e
                    ld        (ix+$28),$ff                  ;[2ab4] dd 36 28 ff
                    ld        a,$0c                         ;[2ab8] 3e 0c
                    call      $2ac2                         ;[2aba] cd c2 2a
                    ld        (ix+$28),$00                  ;[2abd] dd 36 28 00
                    ret                                     ;[2ac1] c9

                    push      af                            ;[2ac2] f5
                    push      de                            ;[2ac3] d5
                    call      $2b36                         ;[2ac4] cd 36 2b
                    pop       de                            ;[2ac7] d1
                    pop       af                            ;[2ac8] f1
                    dec       e                             ;[2ac9] 1d
                    jr        nz,$2ac2                      ;[2aca] 20 f6
                    ret                                     ;[2acc] c9

                    push      hl                            ;[2acd] e5
                    push      de                            ;[2ace] d5
                    push      bc                            ;[2acf] c5
                    call      $2afd                         ;[2ad0] cd fd 2a
                    call      $2b0c                         ;[2ad3] cd 0c 2b
                    sub       $90                           ;[2ad6] d6 90
                    add       a                             ;[2ad8] 87
                    add       a                             ;[2ad9] 87
                    add       a                             ;[2ada] 87
                    ld        bc,$0008                      ;[2adb] 01 08 00
                    ld        hl,$0877                      ;[2ade] 21 77 08
                    ld        e,a                           ;[2ae1] 5f
                    ld        d,b                           ;[2ae2] 50
                    add       hl,de                         ;[2ae3] 19
                    ld        de,$5b9d                      ;[2ae4] 11 9d 5b
                    push      de                            ;[2ae7] d5
                    ldir                                    ;[2ae8] ed b0
                    pop       de                            ;[2aea] d1
                    call      $08d8                         ;[2aeb] cd d8 08
                    push      hl                            ;[2aee] e5
                    ld        a,$90                         ;[2aef] 3e 90
                    call      $2b4a                         ;[2af1] cd 4a 2b
                    pop       de                            ;[2af4] d1
                    call      $08d8                         ;[2af5] cd d8 08
                    pop       bc                            ;[2af8] c1
                    pop       de                            ;[2af9] d1
                    pop       hl                            ;[2afa] e1
                    inc       e                             ;[2afb] 1c
                    ret                                     ;[2afc] c9

                    ld        a,($5c41)                     ;[2afd] 3a 41 5c
                    and       $03                           ;[2b00] e6 03
                    ret       nz                            ;[2b02] c0
                    ld        a,($5c6a)                     ;[2b03] 3a 6a 5c
                    and       $08                           ;[2b06] e6 08
                    ret       z                             ;[2b08] c8
                    ld        a,$03                         ;[2b09] 3e 03
                    ret                                     ;[2b0b] c9

                    add       $92                           ;[2b0c] c6 92
                    ret                                     ;[2b0e] c9

                    push      bc                            ;[2b0f] c5
                    ld        b,a                           ;[2b10] 47
                    inc       b                             ;[2b11] 04
                    jr        $2b19                         ;[2b12] 18 05
                    ld        a,(hl)                        ;[2b14] 7e
                    inc       hl                            ;[2b15] 23
                    call      $2b36                         ;[2b16] cd 36 2b
                    djnz      $2b14                         ;[2b19] 10 f9
                    pop       bc                            ;[2b1b] c1
                    ret                                     ;[2b1c] c9

                    push      bc                            ;[2b1d] c5
                    push      hl                            ;[2b1e] e5
                    ld        e,$00                         ;[2b1f] 1e 00
                    inc       b                             ;[2b21] 04
                    jr        $2b29                         ;[2b22] 18 05
                    ld        a,(hl)                        ;[2b24] 7e
                    inc       hl                            ;[2b25] 23
                    call      $2b2e                         ;[2b26] cd 2e 2b
                    djnz      $2b24                         ;[2b29] 10 f9
                    pop       hl                            ;[2b2b] e1
                    pop       bc                            ;[2b2c] c1
                    ret                                     ;[2b2d] c9

                    push      hl                            ;[2b2e] e5
                    ld        hl,$2b34                      ;[2b2f] 21 34 2b
                    jr        $2b3a                         ;[2b32] 18 06
                    inc       e                             ;[2b34] 1c
                    ret                                     ;[2b35] c9

                    push      hl                            ;[2b36] e5
                    ld        hl,$2b4a                      ;[2b37] 21 4a 2b
                    call      $2b62                         ;[2b3a] cd 62 2b
                    jr        nc,$2b48                      ;[2b3d] 30 09
                    push      bc                            ;[2b3f] c5
                    ld        b,h                           ;[2b40] 44
                    ld        c,l                           ;[2b41] 4d
                    call      $2b7f                         ;[2b42] cd 7f 2b
                    pop       bc                            ;[2b45] c1
                    pop       hl                            ;[2b46] e1
                    ret                                     ;[2b47] c9

                    ex        (sp),hl                       ;[2b48] e3
                    ret                                     ;[2b49] c9

                    inc       e                             ;[2b4a] 1c
                    inc       ixh                           ;[2b4b] dd 24
                    dec       ixh                           ;[2b4d] dd 25
                    jp        nz,$16c0                      ;[2b4f] c2 c0 16
                    push      bc                            ;[2b52] c5
                    push      de                            ;[2b53] d5
                    push      hl                            ;[2b54] e5
                    ld        e,a                           ;[2b55] 5f
                    call      $27d7                         ;[2b56] cd d7 27
                    ld        a,e                           ;[2b59] 7b
                    rst       $10                           ;[2b5a] d7
                    call      $1942                         ;[2b5b] cd 42 19
                    pop       hl                            ;[2b5e] e1
                    pop       de                            ;[2b5f] d1
                    pop       bc                            ;[2b60] c1
                    ret                                     ;[2b61] c9

                    cp        $a5                           ;[2b62] fe a5
                    ccf                                     ;[2b64] 3f
                    ret       nc                            ;[2b65] d0
                    inc       ixh                           ;[2b66] dd 24
                    dec       ixh                           ;[2b68] dd 25
                    ret       z                             ;[2b6a] c8
                    push      de                            ;[2b6b] d5
                    push      hl                            ;[2b6c] e5
                    ld        h,$00                         ;[2b6d] 26 00
                    ld        l,a                           ;[2b6f] 6f
                    add       hl,$ff70                      ;[2b70] ed 34 70 ff
                    add       hl,hl                         ;[2b74] 29
                    add       hl,hl                         ;[2b75] 29
                    add       hl,hl                         ;[2b76] 29
                    ld        de,($5c7b)                    ;[2b77] ed 5b 7b 5c
                    add       hl,de                         ;[2b7b] 19
                    pop       hl                            ;[2b7c] e1
                    pop       de                            ;[2b7d] d1
                    ret                                     ;[2b7e] c9

                    push      de                            ;[2b7f] d5
                    sub       $a5                           ;[2b80] d6 a5
                    ld        de,$0095                      ;[2b82] 11 95 00
                    rst       $28                           ;[2b85] ef
                    ld        b,c                           ;[2b86] 41
                    inc       c                             ;[2b87] 0c
                    ex        de,hl                         ;[2b88] eb
                    pop       de                            ;[2b89] d1
                    jr        c,$2b9f                       ;[2b8a] 38 13
                    inc       ixh                           ;[2b8c] dd 24
                    dec       ixh                           ;[2b8e] dd 25
                    jr        z,$2b98                       ;[2b90] 28 06
                    bit       1,(ix+$25)                    ;[2b92] dd cb 25 4e
                    jr        $2b9c                         ;[2b96] 18 04
                    bit       0,(iy+$01)                    ;[2b98] fd cb 01 46
                    call      z,$2bb8                       ;[2b9c] cc b8 2b
                    rst       $28                           ;[2b9f] ef
                    ld        a,e                           ;[2ba0] 7b
                    nop                                     ;[2ba1] 00
                    inc       hl                            ;[2ba2] 23
                    push      af                            ;[2ba3] f5
                    push      bc                            ;[2ba4] c5
                    push      hl                            ;[2ba5] e5
                    and       $7f                           ;[2ba6] e6 7f
                    call      $2bbc                         ;[2ba8] cd bc 2b
                    pop       hl                            ;[2bab] e1
                    pop       bc                            ;[2bac] c1
                    pop       af                            ;[2bad] f1
                    add       a                             ;[2bae] 87
                    jr        nc,$2b9f                      ;[2baf] 30 ee
                    cp        $48                           ;[2bb1] fe 48
                    jr        z,$2bb8                       ;[2bb3] 28 03
                    cp        $82                           ;[2bb5] fe 82
                    ret       c                             ;[2bb7] d8
                    ld        a,$a0                         ;[2bb8] 3e a0
                    jr        $2ba3                         ;[2bba] 18 e7
                    push      bc                            ;[2bbc] c5
                    ret                                     ;[2bbd] c9

                    exx                                     ;[2bbe] d9
                    call      $2bc4                         ;[2bbf] cd c4 2b
                    exx                                     ;[2bc2] d9
                    ret                                     ;[2bc3] c9

                    call      $272c                         ;[2bc4] cd 2c 27
                    exx                                     ;[2bc7] d9
                    inc       b                             ;[2bc8] 04
                    djnz      $2bd6                         ;[2bc9] 10 0b
                    ld        a,(ix+$21)                    ;[2bcb] dd 7e 21
                    ld        l,(ix+$23)                    ;[2bce] dd 6e 23
                    ld        h,(ix+$22)                    ;[2bd1] dd 66 22
                    jr        $2bee                         ;[2bd4] 18 18
                    djnz      $2be5                         ;[2bd6] 10 0d
                    ld        a,d                           ;[2bd8] 7a
                    and       a                             ;[2bd9] a7
                    jp        nz,$1b7e                      ;[2bda] c2 7e 1b
                    ld        (ix+$2d),e                    ;[2bdd] dd 73 2d
                    call      $1af9                         ;[2be0] cd f9 1a
                    jr        $2c00                         ;[2be3] 18 1b
                    ld        a,(ix+$15)                    ;[2be5] dd 7e 15
                    inc       a                             ;[2be8] 3c
                    ld        l,$00                         ;[2be9] 2e 00
                    ld        h,(ix+$18)                    ;[2beb] dd 66 18
                    sub       (ix+$13)                      ;[2bee] dd 96 13
                    ld        e,a                           ;[2bf1] 5f
                    ld        d,(ix+$1d)                    ;[2bf2] dd 56 1d
                    mul       d,e                           ;[2bf5] ed 30
                    ld        a,h                           ;[2bf7] 7c
                    ld        h,$00                         ;[2bf8] 26 00
                    ex        de,hl                         ;[2bfa] eb
                    add       hl,de                         ;[2bfb] 19
                    sub       (ix+$17)                      ;[2bfc] dd 96 17
                    ld        e,a                           ;[2bff] 5f
                    exx                                     ;[2c00] d9
                    jp        $2792                         ;[2c01] c3 92 27
                    nextreg $57,$10                         ;[2c04] ed 91 57 10
                    ld        hl,$e3c7                      ;[2c08] 21 c7 e3
                    ld        e,(hl)                        ;[2c0b] 5e
                    ld        (hl),$00                      ;[2c0c] 36 00
                    inc       hl                            ;[2c0e] 23
                    ld        d,(hl)                        ;[2c0f] 56
                    nextreg $57,$0f                         ;[2c10] ed 91 57 0f
                    ld        hl,$5245                      ;[2c14] 21 45 52
                    and       a                             ;[2c17] a7
                    sbc       hl,de                         ;[2c18] ed 52
                    jr        nz,$2c27                      ;[2c1a] 20 0b
                    ld        hl,$0eed                      ;[2c1c] 21 ed 0e
                    call      $2cca                         ;[2c1f] cd ca 2c
                    ld        a,$00                         ;[2c22] 3e 00
                    rst       $20                           ;[2c24] e7
                    or        c                             ;[2c25] b1
                    ld        bc,$68cd                      ;[2c26] 01 cd 68
                    add       hl,bc                         ;[2c29] 09
                    rst       $18                           ;[2c2a] df
                    nop                                     ;[2c2b] 00
                    dec       d                             ;[2c2c] 15
                    call      $0a8e                         ;[2c2d] cd 8e 0a
                    call      $3bc3                         ;[2c30] cd c3 3b
                    ld        hl,$f700                      ;[2c33] 21 00 f7
                    ld        de,$0d80                      ;[2c36] 11 80 0d
                    ld        a,$ff                         ;[2c39] 3e ff
                    ld        b,$0f                         ;[2c3b] 06 0f
                    call      $2c52                         ;[2c3d] cd 52 2c
                    jp        $0c43                         ;[2c40] c3 43 0c
                    ld        a,($d5e9)                     ;[2c43] 3a e9 d5
                    cp        $3b                           ;[2c46] fe 3b
                    jr        c,$2c40                       ;[2c48] 38 f6
                    jr        nz,$2c04                      ;[2c4a] 20 b8
                    xor       a                             ;[2c4c] af
                    call      $0068                         ;[2c4d] cd 68 00
                    ld        e,$03                         ;[2c50] 1e 03
                    push      af                            ;[2c52] f5
                    ld        a,d                           ;[2c53] 7a
                    or        e                             ;[2c54] b3
                    jp        z,$3368                       ;[2c55] ca 68 33
                    pop       af                            ;[2c58] f1
                    ld        ($d5d4),a                     ;[2c59] 32 d4 d5
                    and       $40                           ;[2c5c] e6 40
                    jr        z,$2c61                       ;[2c5e] 28 01
                    ld        a,b                           ;[2c60] 78
                    ld        ($d5d5),a                     ;[2c61] 32 d5 d5
                    ld        ($d5ce),hl                    ;[2c64] 22 ce d5
                    ld        ($d5d0),de                    ;[2c67] ed 53 d0 d5
                    xor       a                             ;[2c6b] af
                    ld        ($d5dc),a                     ;[2c6c] 32 dc d5
                    call      $3253                         ;[2c6f] cd 53 32
                    call      $3291                         ;[2c72] cd 91 32
                    call      $3327                         ;[2c75] cd 27 33
                    ld        a,$7f                         ;[2c78] 3e 7f
                    in        a,($fe)                       ;[2c7a] db fe
                    rra                                     ;[2c7c] 1f
                    jr        nc,$2c78                      ;[2c7d] 30 f9
                    res       5,(iy+$01)                    ;[2c7f] fd cb 01 ae
                    ld        ($d5d6),sp                    ;[2c83] ed 73 d6 d5
                    call      $34fe                         ;[2c87] cd fe 34
                    res       3,(iy+$30)                    ;[2c8a] fd cb 30 9e
                    call      $0ce2                         ;[2c8e] cd e2 0c
                    push      af                            ;[2c91] f5
                    ld        a,($d5e2)                     ;[2c92] 3a e2 d5
                    and       a                             ;[2c95] a7
                    ld        hl,$3d06                      ;[2c96] 21 06 3d
                    jr        z,$2c9e                       ;[2c99] 28 03
                    ld        hl,$3cdf                      ;[2c9b] 21 df 3c
                    pop       af                            ;[2c9e] f1
                    call      $2ca6                         ;[2c9f] cd a6 2c
                    jr        nz,$2c75                      ;[2ca2] 20 d1
                    jr        $2c78                         ;[2ca4] 18 d2
                    call      $0ffc                         ;[2ca6] cd fc 0f
                    call      nc,$3e18                      ;[2ca9] d4 18 3e
                    xor       a                             ;[2cac] af
                    ld        ($5c41),a                     ;[2cad] 32 41 5c
                    bit       5,(iy+$30)                    ;[2cb0] fd cb 30 6e
                    ret                                     ;[2cb4] c9

                    ld        ix,$f700                      ;[2cb5] dd 21 00 f7
                    push      de                            ;[2cb9] d5
                    ld        a,$16                         ;[2cba] 3e 16
                    call      $277f                         ;[2cbc] cd 7f 27
                    pop       de                            ;[2cbf] d1
                    push      de                            ;[2cc0] d5
                    ld        a,e                           ;[2cc1] 7b
                    call      $277f                         ;[2cc2] cd 7f 27
                    pop       de                            ;[2cc5] d1
                    ld        a,d                           ;[2cc6] 7a
                    jp        $277f                         ;[2cc7] c3 7f 27
                    ld        b,h                           ;[2cca] 44
                    ld        c,l                           ;[2ccb] 4d
                    ld        hl,$d82b                      ;[2ccc] 21 2b d8
                    push      hl                            ;[2ccf] e5
                    ld        a,(bc)                        ;[2cd0] 0a
                    inc       bc                            ;[2cd1] 03
                    ld        (hl),a                        ;[2cd2] 77
                    inc       hl                            ;[2cd3] 23
                    inc       a                             ;[2cd4] 3c
                    jr        nz,$2cd0                      ;[2cd5] 20 f9
                    pop       hl                            ;[2cd7] e1
                    ret                                     ;[2cd8] c9

                    ld        hl,$d5e4                      ;[2cd9] 21 e4 d5
                    inc       (hl)                          ;[2cdc] 34
                    ld        hl,$d3c3                      ;[2cdd] 21 c3 d3
                    ld        de,$c738                      ;[2ce0] 11 38 c7
                    push      de                            ;[2ce3] d5
                    ld        bc,$000d                      ;[2ce4] 01 0d 00
                    ldir                                    ;[2ce7] ed b0
                    jr        $2d00                         ;[2ce9] 18 15
                    ld        hl,$d5ea                      ;[2ceb] 21 ea d5
                    res       3,(hl)                        ;[2cee] cb 9e
                    xor       a                             ;[2cf0] af
                    ld        ($d5e4),a                     ;[2cf1] 32 e4 d5
                    ld        a,($d5ea)                     ;[2cf4] 3a ea d5
                    and       $40                           ;[2cf7] e6 40
                    ld        ($d5e1),a                     ;[2cf9] 32 e1 d5
                    call      $2d56                         ;[2cfc] cd 56 2d
                    push      de                            ;[2cff] d5
                    ld        hl,$0fa2                      ;[2d00] 21 a2 0f
                    call      $3409                         ;[2d03] cd 09 34
                    pop       de                            ;[2d06] d1
                    xor       a                             ;[2d07] af
                    push      af                            ;[2d08] f5
                    ld        a,($d5e1)                     ;[2d09] 3a e1 d5
                    and       a                             ;[2d0c] a7
                    jr        z,$2d2e                       ;[2d0d] 28 1f
                    ld        hl,$000b                      ;[2d0f] 21 0b 00
                    call      $2cca                         ;[2d12] cd ca 2c
                    ld        bc,$00a7                      ;[2d15] 01 a7 00
                    call      $2d74                         ;[2d18] cd 74 2d
                    ld        ($d5df),hl                    ;[2d1b] 22 df d5
                    and       a                             ;[2d1e] a7
                    jr        z,$2d2e                       ;[2d1f] 28 0d
                    pop       bc                            ;[2d21] c1
                    cp        $f7                           ;[2d22] fe f7
                    jr        nc,$2d4d                      ;[2d24] 30 27
                    push      af                            ;[2d26] f5
                    call      $2d63                         ;[2d27] cd 63 2d
                    xor       a                             ;[2d2a] af
                    ld        ($d5e1),a                     ;[2d2b] 32 e1 d5
                    xor       a                             ;[2d2e] af
                    push      de                            ;[2d2f] d5
                    call      $33c8                         ;[2d30] cd c8 33
                    pop       de                            ;[2d33] d1
                    pop       bc                            ;[2d34] c1
                    push      bc                            ;[2d35] c5
                    ld        a,($d5ea)                     ;[2d36] 3a ea d5
                    and       $40                           ;[2d39] e6 40
                    or        $27                           ;[2d3b] f6 27
                    ld        c,a                           ;[2d3d] 4f
                    push      de                            ;[2d3e] d5
                    call      $2d74                         ;[2d3f] cd 74 2d
                    ld        ($d5dd),hl                    ;[2d42] 22 dd d5
                    pop       de                            ;[2d45] d1
                    pop       bc                            ;[2d46] c1
                    add       b                             ;[2d47] 80
                    push      af                            ;[2d48] f5
                    call      $2d63                         ;[2d49] cd 63 2d
                    pop       af                            ;[2d4c] f1
                    ld        hl,$d5ea                      ;[2d4d] 21 ea d5
                    set       3,(hl)                        ;[2d50] cb de
                    ld        ($d5e2),a                     ;[2d52] 32 e2 d5
                    ret                                     ;[2d55] c9

                    ld        de,$c738                      ;[2d56] 11 38 c7
                    push      de                            ;[2d59] d5
                    xor       a                             ;[2d5a] af
                    ld        b,$0d                         ;[2d5b] 06 0d
                    ld        (de),a                        ;[2d5d] 12
                    inc       de                            ;[2d5e] 13
                    djnz      $2d5d                         ;[2d5f] 10 fc
                    pop       de                            ;[2d61] d1
                    ret                                     ;[2d62] c9

                    push      de                            ;[2d63] d5
                    ld        hl,$c738                      ;[2d64] 21 38 c7
                    ld        b,$0d                         ;[2d67] 06 0d
                    ld        a,(de)                        ;[2d69] 1a
                    ld        c,(hl)                        ;[2d6a] 4e
                    ld        (hl),a                        ;[2d6b] 77
                    ld        a,c                           ;[2d6c] 79
                    ld        (de),a                        ;[2d6d] 12
                    inc       de                            ;[2d6e] 13
                    inc       hl                            ;[2d6f] 23
                    djnz      $2d69                         ;[2d70] 10 f7
                    pop       de                            ;[2d72] d1
                    ret                                     ;[2d73] c9

                    ld        a,$f8                         ;[2d74] 3e f8
                    sub       b                             ;[2d76] 90
                    ld        b,a                           ;[2d77] 47
                    ld        a,($d5ea)                     ;[2d78] 3a ea d5
                    bit       3,a                           ;[2d7b] cb 5f
                    jr        z,$2d81                       ;[2d7d] 28 02
                    set       4,c                           ;[2d7f] cb e1
                    xor       $10                           ;[2d81] ee 10
                    and       $30                           ;[2d83] e6 30
                    ld        h,a                           ;[2d85] 67
                    ld        a,c                           ;[2d86] 79
                    and       $c0                           ;[2d87] e6 c0
                    or        h                             ;[2d89] b4
                    ld        h,a                           ;[2d8a] 67
                    push      bc                            ;[2d8b] c5
                    push      de                            ;[2d8c] d5
                    push      hl                            ;[2d8d] e5
                    call      $35ab                         ;[2d8e] cd ab 35
                    pop       hl                            ;[2d91] e1
                    pop       de                            ;[2d92] d1
                    pop       bc                            ;[2d93] c1
                    and       $07                           ;[2d94] e6 07
                    or        h                             ;[2d96] b4
                    ld        h,a                           ;[2d97] 67
                    and       $03                           ;[2d98] e6 03
                    cp        $01                           ;[2d9a] fe 01
                    jr        z,$2da0                       ;[2d9c] 28 02
                    set       3,h                           ;[2d9e] cb dc
                    ld        a,h                           ;[2da0] 7c
                    res       6,c                           ;[2da1] cb b1
                    ld        hl,$d82b                      ;[2da3] 21 2b d8
                    rst       $20                           ;[2da6] e7
                    ld        e,$01                         ;[2da7] 1e 01
                    ld        a,$00                         ;[2da9] 3e 00
                    ld        ($d5e3),a                     ;[2dab] 32 e3 d5
                    jr        nc,$2db2                      ;[2dae] 30 02
                    ld        a,b                           ;[2db0] 78
                    dec       a                             ;[2db1] 3d
                    push      af                            ;[2db2] f5
                    ld        d,a                           ;[2db3] 57
                    ld        e,$0d                         ;[2db4] 1e 0d
                    mul       d,e                           ;[2db6] ed 30
                    add       de,$c738                      ;[2db8] ed 35 38 c7
                    pop       af                            ;[2dbc] f1
                    ret                                     ;[2dbd] c9

                    ld        c,$01                         ;[2dbe] 0e 01
                    ld        b,$00                         ;[2dc0] 06 00
                    push      bc                            ;[2dc2] c5
                    push      hl                            ;[2dc3] e5
                    ld        de,$0ff3                      ;[2dc4] 11 f3 0f
                    call      $31f7                         ;[2dc7] cd f7 31
                    pop       hl                            ;[2dca] e1
                    pop       bc                            ;[2dcb] c1
                    scf                                     ;[2dcc] 37
                    ret       z                             ;[2dcd] c8
                    and       a                             ;[2dce] a7
                    dec       b                             ;[2dcf] 05
                    ret       z                             ;[2dd0] c8
                    inc       hl                            ;[2dd1] 23
                    ex        de,hl                         ;[2dd2] eb
                    ld        hl,($d5ce)                    ;[2dd3] 2a ce d5
                    ld        a,(hl)                        ;[2dd6] 7e
                    inc       hl                            ;[2dd7] 23
                    inc       a                             ;[2dd8] 3c
                    jr        z,$2e08                       ;[2dd9] 28 2d
                    dec       a                             ;[2ddb] 3d
                    jr        z,$2dd6                       ;[2ddc] 28 f8
                    push      de                            ;[2dde] d5
                    push      hl                            ;[2ddf] e5
                    push      af                            ;[2de0] f5
                    call      $2e55                         ;[2de1] cd 55 2e
                    jr        nz,$2e01                      ;[2de4] 20 1b
                    ld        a,(hl)                        ;[2de6] 7e
                    call      $2eb0                         ;[2de7] cd b0 2e
                    jr        nz,$2e01                      ;[2dea] 20 15
                    dec       c                             ;[2dec] 0d
                    jr        nz,$2e01                      ;[2ded] 20 12
                    pop       bc                            ;[2def] c1
                    pop       hl                            ;[2df0] e1
                    ld        a,(hl)                        ;[2df1] 7e
                    inc       hl                            ;[2df2] 23
                    dec       b                             ;[2df3] 05
                    call      $2eb0                         ;[2df4] cd b0 2e
                    jr        nz,$2df1                      ;[2df7] 20 f8
                    call      $3141                         ;[2df9] cd 41 31
                    and       a                             ;[2dfc] a7
                    ld        a,b                           ;[2dfd] 78
                    pop       de                            ;[2dfe] d1
                    scf                                     ;[2dff] 37
                    ret                                     ;[2e00] c9

                    pop       af                            ;[2e01] f1
                    pop       hl                            ;[2e02] e1
                    add       hl,a                          ;[2e03] ed 31
                    pop       de                            ;[2e05] d1
                    jr        $2dd6                         ;[2e06] 18 ce
                    ld        a,(de)                        ;[2e08] 1a
                    cp        $20                           ;[2e09] fe 20
                    jr        z,$2e0f                       ;[2e0b] 28 02
                    and       a                             ;[2e0d] a7
                    ret                                     ;[2e0e] c9

                    push      bc                            ;[2e0f] c5
                    ex        de,hl                         ;[2e10] eb
                    add       hl,$fff8                      ;[2e11] ed 34 f8 ff
                    ld        de,$d82b                      ;[2e15] 11 2b d8
                    push      de                            ;[2e18] d5
                    call      $343a                         ;[2e19] cd 3a 34
                    pop       hl                            ;[2e1c] e1
                    ld        de,$0001                      ;[2e1d] 11 01 00
                    ld        b,d                           ;[2e20] 42
                    ld        c,e                           ;[2e21] 4b
                    rst       $20                           ;[2e22] e7
                    ld        b,$01                         ;[2e23] 06 01
                    pop       bc                            ;[2e25] c1
                    ret       nc                            ;[2e26] d0
                    push      bc                            ;[2e27] c5
                    ld        b,$00                         ;[2e28] 06 00
                    rst       $20                           ;[2e2a] e7
                    rrca                                    ;[2e2b] 0f
                    ld        bc,$ddf5                      ;[2e2c] 01 f5 dd
                    push      hl                            ;[2e2f] e5
                    ld        b,$00                         ;[2e30] 06 00
                    rst       $20                           ;[2e32] e7
                    add       hl,bc                         ;[2e33] 09
                    ld        bc,$f1e1                      ;[2e34] 01 e1 f1
                    pop       bc                            ;[2e37] c1
                    ret       nc                            ;[2e38] d0
                    ld        a,(hl)                        ;[2e39] 7e
                    inc       hl                            ;[2e3a] 23
                    ld        e,(hl)                        ;[2e3b] 5e
                    inc       hl                            ;[2e3c] 23
                    ld        d,(hl)                        ;[2e3d] 56
                    and       a                             ;[2e3e] a7
                    ld        hl,$0ff5                      ;[2e3f] 21 f5 0f
                    jp        z,$2dc0                       ;[2e42] ca c0 2d
                    cp        $03                           ;[2e45] fe 03
                    jr        nz,$2e0d                      ;[2e47] 20 c4
                    ld        hl,$1b00                      ;[2e49] 21 00 1b
                    sbc       hl,de                         ;[2e4c] ed 52
                    jr        nz,$2e0d                      ;[2e4e] 20 bd
                    ld        hl,$0ff8                      ;[2e50] 21 f8 0f
                    jr        $2e42                         ;[2e53] 18 ed
                    push      de                            ;[2e55] d5
                    ld        b,$03                         ;[2e56] 06 03
                    ld        a,(hl)                        ;[2e58] 7e
                    cp        $2a                           ;[2e59] fe 2a
                    inc       hl                            ;[2e5b] 23
                    jr        z,$2e7f                       ;[2e5c] 28 21
                    dec       hl                            ;[2e5e] 2b
                    ld        a,(de)                        ;[2e5f] 1a
                    inc       de                            ;[2e60] 13
                    and       $7f                           ;[2e61] e6 7f
                    cp        $20                           ;[2e63] fe 20
                    jr        z,$2e93                       ;[2e65] 28 2c
                    cp        (hl)                          ;[2e67] be
                    jr        z,$2e7c                       ;[2e68] 28 12
                    cp        $41                           ;[2e6a] fe 41
                    jr        c,$2e77                       ;[2e6c] 38 09
                    cp        $5b                           ;[2e6e] fe 5b
                    jr        nc,$2e77                      ;[2e70] 30 05
                    set       5,a                           ;[2e72] cb ef
                    cp        (hl)                          ;[2e74] be
                    jr        z,$2e7c                       ;[2e75] 28 05
                    ld        a,(hl)                        ;[2e77] 7e
                    cp        $3f                           ;[2e78] fe 3f
                    jr        nz,$2e9d                      ;[2e7a] 20 21
                    inc       hl                            ;[2e7c] 23
                    djnz      $2e58                         ;[2e7d] 10 d9
                    pop       de                            ;[2e7f] d1
                    ld        a,(hl)                        ;[2e80] 7e
                    cp        $2c                           ;[2e81] fe 2c
                    jr        nz,$2eb0                      ;[2e83] 20 2b
                    inc       hl                            ;[2e85] 23
                    ld        a,(hl)                        ;[2e86] 7e
                    call      $2eb0                         ;[2e87] cd b0 2e
                    ret       z                             ;[2e8a] c8
                    call      $2eb9                         ;[2e8b] cd b9 2e
                    jr        nz,$2e85                      ;[2e8e] 20 f5
                    xor       a                             ;[2e90] af
                    inc       a                             ;[2e91] 3c
                    ret                                     ;[2e92] c9

                    ld        a,(hl)                        ;[2e93] 7e
                    cp        $2c                           ;[2e94] fe 2c
                    jr        z,$2e7f                       ;[2e96] 28 e7
                    call      $2eb0                         ;[2e98] cd b0 2e
                    jr        z,$2e7f                       ;[2e9b] 28 e2
                    pop       de                            ;[2e9d] d1
                    ld        a,(hl)                        ;[2e9e] 7e
                    cp        $2c                           ;[2e9f] fe 2c
                    inc       hl                            ;[2ea1] 23
                    jr        z,$2e55                       ;[2ea2] 28 b1
                    call      $2eb0                         ;[2ea4] cd b0 2e
                    jr        z,$2e90                       ;[2ea7] 28 e7
                    call      $2eb9                         ;[2ea9] cd b9 2e
                    jr        z,$2e90                       ;[2eac] 28 e2
                    jr        $2e9e                         ;[2eae] 18 ee
                    cp        $3a                           ;[2eb0] fe 3a
                    ret       z                             ;[2eb2] c8
                    cp        $3b                           ;[2eb3] fe 3b
                    ret       z                             ;[2eb5] c8
                    cp        $3c                           ;[2eb6] fe 3c
                    ret                                     ;[2eb8] c9

                    cp        $ff                           ;[2eb9] fe ff
                    ret       z                             ;[2ebb] c8
                    cp        $0d                           ;[2ebc] fe 0d
                    ret       z                             ;[2ebe] c8
                    cp        $0a                           ;[2ebf] fe 0a
                    ret                                     ;[2ec1] c9

                    xor       a                             ;[2ec2] af
                    ld        ($5c41),a                     ;[2ec3] 32 41 5c
                    ld        a,($d5d5)                     ;[2ec6] 3a d5 d5
                    cpl                                     ;[2ec9] 2f
                    and       $04                           ;[2eca] e6 04
                    ret       nz                            ;[2ecc] c0
                    ld        ($d5d8),sp                    ;[2ecd] ed 73 d8 d5
                    ld        de,$2699                      ;[2ed1] 11 99 26
                    call      $0068                         ;[2ed4] cd 68 00
                    ld        c,l                           ;[2ed7] 4d
                    ld        ($21e5),hl                    ;[2ed8] 22 e5 21
                    ld        c,h                           ;[2edb] 4c
                    dec       a                             ;[2edc] 3d
                    ld        bc,$0006                      ;[2edd] 01 06 00
                    ld        de,$d82b                      ;[2ee0] 11 2b d8
                    ldir                                    ;[2ee3] ed b0
                    pop       hl                            ;[2ee5] e1
                    push      de                            ;[2ee6] d5
                    ex        de,hl                         ;[2ee7] eb
                    ld        hl,$e090                      ;[2ee8] 21 90 e0
                    ld        c,$00                         ;[2eeb] 0e 00
                    push      hl                            ;[2eed] e5
                    pop       ix                            ;[2eee] dd e1
                    res       7,c                           ;[2ef0] cb b9
                    ld        (hl),$16                      ;[2ef2] 36 16
                    inc       hl                            ;[2ef4] 23
                    ld        a,c                           ;[2ef5] 79
                    srl       a                             ;[2ef6] cb 3f
                    srl       a                             ;[2ef8] cb 3f
                    add       $16                           ;[2efa] c6 16
                    ld        (hl),a                        ;[2efc] 77
                    inc       hl                            ;[2efd] 23
                    push      de                            ;[2efe] d5
                    ld        a,c                           ;[2eff] 79
                    and       $03                           ;[2f00] e6 03
                    ld        d,a                           ;[2f02] 57
                    ld        e,$0d                         ;[2f03] 1e 0d
                    mul       d,e                           ;[2f05] ed 30
                    ld        (hl),e                        ;[2f07] 73
                    inc       hl                            ;[2f08] 23
                    pop       de                            ;[2f09] d1
                    ld        b,$0d                         ;[2f0a] 06 0d
                    ld        a,(de)                        ;[2f0c] 1a
                    inc       de                            ;[2f0d] 13
                    cp        $3a                           ;[2f0e] fe 3a
                    jr        z,$2f42                       ;[2f10] 28 30
                    call      $2eb9                         ;[2f12] cd b9 2e
                    jr        z,$2f1f                       ;[2f15] 28 08
                    bit       5,a                           ;[2f17] cb 6f
                    jr        z,$2f24                       ;[2f19] 28 09
                    ld        (hl),a                        ;[2f1b] 77
                    inc       hl                            ;[2f1c] 23
                    djnz      $2f0c                         ;[2f1d] 10 ed
                    push      ix                            ;[2f1f] dd e5
                    pop       hl                            ;[2f21] e1
                    jr        $2f87                         ;[2f22] 18 63
                    bit       7,c                           ;[2f24] cb 79
                    jr        nz,$2f1b                      ;[2f26] 20 f3
                    set       7,c                           ;[2f28] cb f9
                    ld        (hl),$14                      ;[2f2a] 36 14
                    inc       hl                            ;[2f2c] 23
                    ld        (hl),$01                      ;[2f2d] 36 01
                    inc       hl                            ;[2f2f] 23
                    ld        (hl),a                        ;[2f30] 77
                    inc       hl                            ;[2f31] 23
                    ld        (hl),$14                      ;[2f32] 36 14
                    inc       hl                            ;[2f34] 23
                    ld        (hl),$00                      ;[2f35] 36 00
                    inc       hl                            ;[2f37] 23
                    set       5,a                           ;[2f38] cb ef
                    ex        (sp),hl                       ;[2f3a] e3
                    ld        (hl),a                        ;[2f3b] 77
                    ex        (sp),hl                       ;[2f3c] e3
                    dec       b                             ;[2f3d] 05
                    jr        z,$2f1f                       ;[2f3e] 28 df
                    jr        $2f0c                         ;[2f40] 18 ca
                    push      hl                            ;[2f42] e5
                    call      $342c                         ;[2f43] cd 2c 34
                    ld        a,(de)                        ;[2f46] 1a
                    set       5,a                           ;[2f47] cb ef
                    cp        $66                           ;[2f49] fe 66
                    jr        nz,$2f52                      ;[2f4b] 20 05
                    bit       7,(hl)                        ;[2f4d] cb 7e
                    jr        nz,$2f69                      ;[2f4f] 20 18
                    inc       de                            ;[2f51] 13
                    cp        $64                           ;[2f52] fe 64
                    jr        nz,$2f5b                      ;[2f54] 20 05
                    bit       7,(hl)                        ;[2f56] cb 7e
                    jr        z,$2f69                       ;[2f58] 28 0f
                    inc       de                            ;[2f5a] 13
                    inc       hl                            ;[2f5b] 23
                    ld        a,(de)                        ;[2f5c] 1a
                    cp        $2e                           ;[2f5d] fe 2e
                    jr        nz,$2f6c                      ;[2f5f] 20 0b
                    inc       de                            ;[2f61] 13
                    ex        de,hl                         ;[2f62] eb
                    call      $2e55                         ;[2f63] cd 55 2e
                    ex        de,hl                         ;[2f66] eb
                    jr        z,$2f6c                       ;[2f67] 28 03
                    pop       hl                            ;[2f69] e1
                    jr        $2f1f                         ;[2f6a] 18 b3
                    ld        a,(de)                        ;[2f6c] 1a
                    inc       de                            ;[2f6d] 13
                    cp        $3a                           ;[2f6e] fe 3a
                    jr        nz,$2f69                      ;[2f70] 20 f7
                    pop       hl                            ;[2f72] e1
                    ex        (sp),hl                       ;[2f73] e3
                    inc       hl                            ;[2f74] 23
                    ld        (hl),$f6                      ;[2f75] 36 f6
                    inc       hl                            ;[2f77] 23
                    ld        (hl),$2f                      ;[2f78] 36 2f
                    inc       hl                            ;[2f7a] 23
                    inc       h                             ;[2f7b] 24
                    ld        (hl),e                        ;[2f7c] 73
                    inc       hl                            ;[2f7d] 23
                    ld        (hl),d                        ;[2f7e] 72
                    dec       hl                            ;[2f7f] 2b
                    dec       h                             ;[2f80] 25
                    ex        (sp),hl                       ;[2f81] e3
                    inc       c                             ;[2f82] 0c
                    bit       3,c                           ;[2f83] cb 59
                    jr        nz,$2f96                      ;[2f85] 20 0f
                    call      $2ebc                         ;[2f87] cd bc 2e
                    jp        z,$2eed                       ;[2f8a] ca ed 2e
                    inc       a                             ;[2f8d] 3c
                    jr        z,$2f94                       ;[2f8e] 28 04
                    ld        a,(de)                        ;[2f90] 1a
                    inc       de                            ;[2f91] 13
                    jr        $2f87                         ;[2f92] 18 f3
                    ld        d,$00                         ;[2f94] 16 00
                    ld        ($d5da),de                    ;[2f96] ed 53 da d5
                    ld        a,$ff                         ;[2f9a] 3e ff
                    ld        (hl),a                        ;[2f9c] 77
                    pop       hl                            ;[2f9d] e1
                    ld        (hl),a                        ;[2f9e] 77
                    ld        a,c                           ;[2f9f] 79
                    and       $7f                           ;[2fa0] e6 7f
                    jr        z,$2fc1                       ;[2fa2] 28 1d
                    call      $3401                         ;[2fa4] cd 01 34
                    call      $3406                         ;[2fa7] cd 06 34
                    ld        hl,$e090                      ;[2faa] 21 90 e0
                    call      $2fea                         ;[2fad] cd ea 2f
                    res       3,(iy+$30)                    ;[2fb0] fd cb 30 9e
                    call      $0ce2                         ;[2fb4] cd e2 0c
                    ld        hl,$d82b                      ;[2fb7] 21 2b d8
                    call      $2ca6                         ;[2fba] cd a6 2c
                    jr        nz,$2fa4                      ;[2fbd] 20 e5
                    jr        $2fb0                         ;[2fbf] 18 ef
                    ld        sp,($d5d8)                    ;[2fc1] ed 7b d8 d5
                    xor       a                             ;[2fc5] af
                    ld        ($d5dc),a                     ;[2fc6] 32 dc d5
                    set       5,(iy+$30)                    ;[2fc9] fd cb 30 ee
                    scf                                     ;[2fcd] 37
                    ret                                     ;[2fce] c9

                    ld        sp,($d5d8)                    ;[2fcf] ed 7b d8 d5
                    call      $2fc5                         ;[2fd3] cd c5 2f
                    ld        hl,($d5da)                    ;[2fd6] 2a da d5
                    ld        a,h                           ;[2fd9] 7c
                    and       a                             ;[2fda] a7
                    jr        z,$2fc1                       ;[2fdb] 28 e4
                    ld        a,(hl)                        ;[2fdd] 7e
                    inc       hl                            ;[2fde] 23
                    call      $2ebc                         ;[2fdf] cd bc 2e
                    jp        z,$2ed9                       ;[2fe2] ca d9 2e
                    inc       a                             ;[2fe5] 3c
                    jr        z,$2fc1                       ;[2fe6] 28 d9
                    jr        $2fdd                         ;[2fe8] 18 f3
                    ld        a,(hl)                        ;[2fea] 7e
                    inc       hl                            ;[2feb] 23
                    cp        $ff                           ;[2fec] fe ff
                    ret       z                             ;[2fee] c8
                    push      hl                            ;[2fef] e5
                    call      $277f                         ;[2ff0] cd 7f 27
                    pop       hl                            ;[2ff3] e1
                    jr        $2fea                         ;[2ff4] 18 f4
                    inc       h                             ;[2ff6] 24
                    ld        e,(hl)                        ;[2ff7] 5e
                    inc       hl                            ;[2ff8] 23
                    ld        d,(hl)                        ;[2ff9] 56
                    ex        de,hl                         ;[2ffa] eb
                    ld        a,(hl)                        ;[2ffb] 7e
                    call      $2eb0                         ;[2ffc] cd b0 2e
                    jr        z,$3027                       ;[2fff] 28 26
                    ld        de,$da34                      ;[3001] 11 34 da
                    ld        a,(hl)                        ;[3004] 7e
                    inc       de                            ;[3005] 13
                    inc       hl                            ;[3006] 23
                    ld        (de),a                        ;[3007] 12
                    call      $2eb9                         ;[3008] cd b9 2e
                    ret       z                             ;[300b] c8
                    call      $2eb0                         ;[300c] cd b0 2e
                    jr        nz,$3004                      ;[300f] 20 f3
                    xor       a                             ;[3011] af
                    ld        (de),a                        ;[3012] 12
                    dec       hl                            ;[3013] 2b
                    push      hl                            ;[3014] e5
                    call      $3401                         ;[3015] cd 01 34
                    call      $3406                         ;[3018] cd 06 34
                    set       5,(iy+$30)                    ;[301b] fd cb 30 ee
                    ld        hl,$da35                      ;[301f] 21 35 da
                    rst       $18                           ;[3022] df
                    ld        (hl),e                        ;[3023] 73
                    dec       hl                            ;[3024] 2b
                    pop       hl                            ;[3025] e1
                    ret       nz                            ;[3026] c0
                    ld        a,(hl)                        ;[3027] 7e
                    inc       hl                            ;[3028] 23
                    call      $2eb0                         ;[3029] cd b0 2e
                    scf                                     ;[302c] 37
                    ccf                                     ;[302d] 3f
                    ret       nz                            ;[302e] c0
                    ld        c,a                           ;[302f] 4f
                    ld        b,$ff                         ;[3030] 06 ff
                    ld        de,$f700                      ;[3032] 11 00 f7
                    push      de                            ;[3035] d5
                    inc       b                             ;[3036] 04
                    ld        a,(hl)                        ;[3037] 7e
                    ld        (de),a                        ;[3038] 12
                    inc       hl                            ;[3039] 23
                    inc       de                            ;[303a] 13
                    call      $2eb9                         ;[303b] cd b9 2e
                    jr        nz,$3036                      ;[303e] 20 f6
                    push      bc                            ;[3040] c5
                    call      $3426                         ;[3041] cd 26 34
                    pop       bc                            ;[3044] c1
                    pop       hl                            ;[3045] e1
                    ld        a,c                           ;[3046] 79
                    jp        $30fc                         ;[3047] c3 fc 30
                    ld        a,($d5d5)                     ;[304a] 3a d5 d5
                    cpl                                     ;[304d] 2f
                    and       $02                           ;[304e] e6 02
                    ret       nz                            ;[3050] c0
                    ld        a,$3c                         ;[3051] 3e 3c
                    call      $3141                         ;[3053] cd 41 31
                    call      $3426                         ;[3056] cd 26 34
                    jr        z,$3068                       ;[3059] 28 0d
                    call      $31f4                         ;[305b] cd f4 31
                    jr        nz,$3068                      ;[305e] 20 08
                    call      $314a                         ;[3060] cd 4a 31
                    ld        hl,$26d6                      ;[3063] 21 d6 26
                    jr        $30bd                         ;[3066] 18 55
                    call      $38d7                         ;[3068] cd d7 38
                    jr        nc,$30b8                      ;[306b] 30 4b
                    ld        hl,$da35                      ;[306d] 21 35 da
                    push      hl                            ;[3070] e5
                    ld        bc,$0001                      ;[3071] 01 01 00
                    ld        a,(hl)                        ;[3074] 7e
                    inc       hl                            ;[3075] 23
                    inc       a                             ;[3076] 3c
                    jr        z,$3084                       ;[3077] 28 0b
                    inc       b                             ;[3079] 04
                    jr        z,$3083                       ;[307a] 28 07
                    cp        $2f                           ;[307c] fe 2f
                    jr        nz,$3074                      ;[307e] 20 f4
                    ld        c,b                           ;[3080] 48
                    jr        $3074                         ;[3081] 18 f1
                    dec       b                             ;[3083] 05
                    dec       c                             ;[3084] 0d
                    jr        z,$3088                       ;[3085] 28 01
                    ld        b,c                           ;[3087] 41
                    ld        a,b                           ;[3088] 78
                    pop       de                            ;[3089] d1
                    push      de                            ;[308a] d5
                    add       de,a                          ;[308b] ed 32
                    ld        hl,$0fc8                      ;[308d] 21 c8 0f
                    ld        bc,$0005                      ;[3090] 01 05 00
                    ldir                                    ;[3093] ed b0
                    pop       hl                            ;[3095] e1
                    push      hl                            ;[3096] e5
                    call      $30c3                         ;[3097] cd c3 30
                    jr        c,$30bc                       ;[309a] 38 20
                    ld        hl,$0fcd                      ;[309c] 21 cd 0f
                    ld        de,$f700                      ;[309f] 11 00 f7
                    ld        bc,$000f                      ;[30a2] 01 0f 00
                    ldir                                    ;[30a5] ed b0
                    pop       hl                            ;[30a7] e1
                    ld        a,(hl)                        ;[30a8] 7e
                    ldi                                     ;[30a9] ed a0
                    inc       a                             ;[30ab] 3c
                    jr        nz,$30a8                      ;[30ac] 20 fa
                    ld        hl,$f700                      ;[30ae] 21 00 f7
                    push      hl                            ;[30b1] e5
                    call      $30c3                         ;[30b2] cd c3 30
                    jr        c,$30bc                       ;[30b5] 38 05
                    pop       hl                            ;[30b7] e1
                    push    $26a9                           ;[30b8] ed 8a 26 a9
                    pop       hl                            ;[30bc] e1
                    call      $0068                         ;[30bd] cd 68 00
                    sbc       c                             ;[30c0] 99
                    ld        hl,$11c9                      ;[30c1] 21 c9 11
                    ld        bc,$4200                      ;[30c4] 01 00 42
                    ld        c,e                           ;[30c7] 4b
                    rst       $20                           ;[30c8] e7
                    ld        b,$01                         ;[30c9] 06 01
                    ret       nc                            ;[30cb] d0
                    ld        b,$00                         ;[30cc] 06 00
                    rst       $20                           ;[30ce] e7
                    add       hl,bc                         ;[30cf] 09
                    ld        bc,$c937                      ;[30d0] 01 37 c9
                    ld        c,$02                         ;[30d3] 0e 02
                    ld        a,$7f                         ;[30d5] 3e 7f
                    in        a,($fe)                       ;[30d7] db fe
                    and       c                             ;[30d9] a1
                    jr        z,$30dd                       ;[30da] 28 01
                    dec       c                             ;[30dc] 0d
                    call      $3426                         ;[30dd] cd 26 34
                    jr        z,$3101                       ;[30e0] 28 1f
                    dec       c                             ;[30e2] 0d
                    jr        nz,$30f0                      ;[30e3] 20 0b
                    ld        a,($d5d5)                     ;[30e5] 3a d5 d5
                    cpl                                     ;[30e8] 2f
                    and       $01                           ;[30e9] e6 01
                    jr        nz,$30f0                      ;[30eb] 20 03
                    call      $31f4                         ;[30ed] cd f4 31
                    jp        nz,$328a                      ;[30f0] c2 8a 32
                    call      $314a                         ;[30f3] cd 4a 31
                    ret       nc                            ;[30f6] d0
                    ld        hl,$26c4                      ;[30f7] 21 c4 26
                    ld        a,$3c                         ;[30fa] 3e 3c
                    call      $3141                         ;[30fc] cd 41 31
                    jr        $3116                         ;[30ff] 18 15
                    call      $2dc0                         ;[3101] cd c0 2d
                    ret       nc                            ;[3104] d0
                    jr        nz,$3116                      ;[3105] 20 0f
                    push      bc                            ;[3107] c5
                    ld        de,$d82b                      ;[3108] 11 2b d8
                    ld        bc,$03ff                      ;[310b] 01 ff 03
                    call      $0068                         ;[310e] cd 68 00
                    ld        d,b                           ;[3111] 50
                    ld        ($18c1),hl                    ;[3112] 22 c1 18
                    ld        c,h                           ;[3115] 4c
                    and       a                             ;[3116] a7
                    push      hl                            ;[3117] e5
                    push      af                            ;[3118] f5
                    ld        bc,$d82b                      ;[3119] 01 2b d8
                    ld        hl,$e090                      ;[311c] 21 90 e0
                    call      $2ccf                         ;[311f] cd cf 2c
                    call      $38d7                         ;[3122] cd d7 38
                    pop       af                            ;[3125] f1
                    push      af                            ;[3126] f5
                    call      nz,$3213                      ;[3127] c4 13 32
                    pop       af                            ;[312a] f1
                    pop       hl                            ;[312b] e1
                    jp        z,$3208                       ;[312c] ca 08 32
                    push      hl                            ;[312f] e5
                    add       hl,a                          ;[3130] ed 31
                    ld        a,(hl)                        ;[3132] 7e
                    ld        (hl),$0d                      ;[3133] 36 0d
                    ex        (sp),hl                       ;[3135] e3
                    push      af                            ;[3136] f5
                    call      $0068                         ;[3137] cd 68 00
                    or        d                             ;[313a] b2
                    ld        hl,$e1f1                      ;[313b] 21 f1 e1
                    ld        (hl),a                        ;[313e] 77
                    and       a                             ;[313f] a7
                    ret                                     ;[3140] c9

                    ld        ($d5e9),a                     ;[3141] 32 e9 d5
                    ld        a,$fe                         ;[3144] 3e fe
                    ld        ($d5b8),a                     ;[3146] 32 b8 d5
                    ret                                     ;[3149] c9

                    ld        hl,$d82b                      ;[314a] 21 2b d8
                    ld        a,$00                         ;[314d] 3e 00
                    rst       $20                           ;[314f] e7
                    or        c                             ;[3150] b1
                    ld        bc,$91ed                      ;[3151] 01 ed 91
                    ld        d,a                           ;[3154] 57
                    djnz      $3168                         ;[3155] 10 11
                    ld        b,l                           ;[3157] 45
                    ld        d,d                           ;[3158] 52
                    ld        ($e3c7),de                    ;[3159] ed 53 c7 e3
                    nextreg $57,$0f                         ;[315d] ed 91 57 0f
                    ret                                     ;[3161] c9

                    ld        de,$0fe8                      ;[3162] 11 e8 0f
                    ld        b,$08                         ;[3165] 06 08
                    ld        a,(de)                        ;[3167] 1a
                    cp        (hl)                          ;[3168] be
                    inc       de                            ;[3169] 13
                    inc       hl                            ;[316a] 23
                    scf                                     ;[316b] 37
                    ccf                                     ;[316c] 3f
                    ret       nz                            ;[316d] c0
                    djnz      $3167                         ;[316e] 10 f7
                    ld        a,(hl)                        ;[3170] 7e
                    call      $2ebc                         ;[3171] cd bc 2e
                    jr        nz,$316b                      ;[3174] 20 f5
                    push      bc                            ;[3176] c5
                    call      $31cf                         ;[3177] cd cf 31
                    jr        nc,$3181                      ;[317a] 30 05
                    push      hl                            ;[317c] e5
                    call      $328a                         ;[317d] cd 8a 32
                    pop       hl                            ;[3180] e1
                    pop       bc                            ;[3181] c1
                    ret       nc                            ;[3182] d0
                    push      bc                            ;[3183] c5
                    call      $31cf                         ;[3184] cd cf 31
                    pop       bc                            ;[3187] c1
                    ccf                                     ;[3188] 3f
                    ret       c                             ;[3189] d8
                    push      bc                            ;[318a] c5
                    ld        hl,$d82b                      ;[318b] 21 2b d8
                    push      hl                            ;[318e] e5
                    call      $3874                         ;[318f] cd 74 38
                    pop       hl                            ;[3192] e1
                    ld        de,$c400                      ;[3193] 11 00 c4
                    ld        a,(hl)                        ;[3196] 7e
                    ldi                                     ;[3197] ed a0
                    inc       a                             ;[3199] 3c
                    jr        nz,$3196                      ;[319a] 20 fa
                    dec       de                            ;[319c] 1b
                    ld        (de),a                        ;[319d] 12
                    ld        hl,$d5ea                      ;[319e] 21 ea d5
                    res       7,(hl)                        ;[31a1] cb be
                    call      $3507                         ;[31a3] cd 07 35
                    ld        d,$ff                         ;[31a6] 16 ff
                    inc       d                             ;[31a8] 14
                    ld        a,($d5e2)                     ;[31a9] 3a e2 d5
                    ld        e,a                           ;[31ac] 5f
                    ld        a,d                           ;[31ad] 7a
                    cp        e                             ;[31ae] bb
                    jr        c,$31be                       ;[31af] 38 0d
                    push      de                            ;[31b1] d5
                    call      $3732                         ;[31b2] cd 32 37
                    pop       de                            ;[31b5] d1
                    jr        c,$31a3                       ;[31b6] 38 eb
                    call      $3317                         ;[31b8] cd 17 33
                    and       a                             ;[31bb] a7
                    pop       bc                            ;[31bc] c1
                    ret                                     ;[31bd] c9

                    call      $3691                         ;[31be] cd 91 36
                    jr        nz,$31a8                      ;[31c1] 20 e5
                    ld        a,d                           ;[31c3] 7a
                    call      $377e                         ;[31c4] cd 7e 37
                    pop       bc                            ;[31c7] c1
                    ld        a,c                           ;[31c8] 79
                    dec       a                             ;[31c9] 3d
                    jp        z,$30dd                       ;[31ca] ca dd 30
                    scf                                     ;[31cd] 37
                    ret                                     ;[31ce] c9

                    ld        de,$d82b                      ;[31cf] 11 2b d8
                    ld        bc,$fe02                      ;[31d2] 01 02 fe
                    ld        a,(hl)                        ;[31d5] 7e
                    cp        $ff                           ;[31d6] fe ff
                    ret       z                             ;[31d8] c8
                    inc       hl                            ;[31d9] 23
                    call      $2ebc                         ;[31da] cd bc 2e
                    jr        z,$31d5                       ;[31dd] 28 f6
                    ld        (de),a                        ;[31df] 12
                    inc       de                            ;[31e0] 13
                    ld        a,(hl)                        ;[31e1] 7e
                    call      $2eb9                         ;[31e2] cd b9 2e
                    jr        z,$31ef                       ;[31e5] 28 08
                    inc       hl                            ;[31e7] 23
                    djnz      $31df                         ;[31e8] 10 f5
                    dec       c                             ;[31ea] 0d
                    jr        nz,$31df                      ;[31eb] 20 f2
                    and       a                             ;[31ed] a7
                    ret                                     ;[31ee] c9

                    ld        a,$ff                         ;[31ef] 3e ff
                    ld        (de),a                        ;[31f1] 12
                    scf                                     ;[31f2] 37
                    ret                                     ;[31f3] c9

                    ld        de,$0ff0                      ;[31f4] 11 f0 0f
                    ex        de,hl                         ;[31f7] eb
                    ld        b,$03                         ;[31f8] 06 03
                    inc       de                            ;[31fa] 13
                    ld        a,(de)                        ;[31fb] 1a
                    and       $7f                           ;[31fc] e6 7f
                    cp        (hl)                          ;[31fe] be
                    inc       hl                            ;[31ff] 23
                    ret       nz                            ;[3200] c0
                    djnz      $31fa                         ;[3201] 10 f7
                    ret                                     ;[3203] c9

                    xor       a                             ;[3204] af
                    inc       a                             ;[3205] 3c
                    jr        $320f                         ;[3206] 18 07
                    ld        de,$da35                      ;[3208] 11 35 da
                    ld        hl,$e090                      ;[320b] 21 90 e0
                    xor       a                             ;[320e] af
                    ld        sp,($d5d6)                    ;[320f] ed 7b d6 d5
                    scf                                     ;[3213] 37
                    push      af                            ;[3214] f5
                    push      hl                            ;[3215] e5
                    push      de                            ;[3216] d5
                    ld        bc,$0fdc                      ;[3217] 01 dc 0f
                    ld        hl,$c752                      ;[321a] 21 52 c7
                    push      hl                            ;[321d] e5
                    call      $2ccf                         ;[321e] cd cf 2c
                    call      $2d56                         ;[3221] cd 56 2d
                    pop       hl                            ;[3224] e1
                    ld        bc,$02a0                      ;[3225] 01 a0 02
                    xor       a                             ;[3228] af
                    push      de                            ;[3229] d5
                    push      hl                            ;[322a] e5
                    rst       $20                           ;[322b] e7
                    ld        e,$01                         ;[322c] 1e 01
                    pop       hl                            ;[322e] e1
                    pop       de                            ;[322f] d1
                    ld        bc,$0220                      ;[3230] 01 20 02
                    xor       a                             ;[3233] af
                    rst       $20                           ;[3234] e7
                    ld        e,$01                         ;[3235] 1e 01
                    call      $0b03                         ;[3237] cd 03 0b
                    call      $33ea                         ;[323a] cd ea 33
                    call      $3401                         ;[323d] cd 01 34
                    ld        a,($d693)                     ;[3240] 3a 93 d6
                    rst       $18                           ;[3243] df
                    ret       p                             ;[3244] f0
                    inc       d                             ;[3245] 14
                    ld        hl,($d73a)                    ;[3246] 2a 3a d7
                    rst       $28                           ;[3249] ef
                    dec       d                             ;[324a] 15
                    ld        d,$cd                         ;[324b] 16 cd
                    ld        d,h                           ;[324d] 54
                    add       hl,bc                         ;[324e] 09
                    pop       de                            ;[324f] d1
                    pop       hl                            ;[3250] e1
                    pop       af                            ;[3251] f1
                    ret                                     ;[3252] c9

                    and       a                             ;[3253] a7
                    call      $33ea                         ;[3254] cd ea 33
                    ld        a,($5c7f)                     ;[3257] 3a 7f 5c
                    ld        ($d693),a                     ;[325a] 32 93 d6
                    ld        hl,($5c51)                    ;[325d] 2a 51 5c
                    ld        ($d73a),hl                    ;[3260] 22 3a d7
                    ld        a,$05                         ;[3263] 3e 05
                    rst       $18                           ;[3265] df
                    ret       p                             ;[3266] f0
                    inc       d                             ;[3267] 14
                    ld        a,$02                         ;[3268] 3e 02
                    rst       $28                           ;[326a] ef
                    ld        bc,$c316                      ;[326b] 01 16 c3
                    sbc       a                             ;[326e] 9f
                    ld        a,(bc)                        ;[326f] 0a
                    ld        a,$43                         ;[3270] 3e 43
                    rst       $20                           ;[3272] e7
                    dec       l                             ;[3273] 2d
                    ld        bc,$df21                      ;[3274] 01 21 df
                    rrca                                    ;[3277] 0f
                    call      $2cca                         ;[3278] cd ca 2c
                    ld        a,$02                         ;[327b] 3e 02
                    rst       $20                           ;[327d] e7
                    or        c                             ;[327e] b1
                    ld        bc,$0818                      ;[327f] 01 18 08
                    ld        hl,$0eed                      ;[3282] 21 ed 0e
                    call      $2cca                         ;[3285] cd ca 2c
                    jr        $328d                         ;[3288] 18 03
                    ld        hl,$d82b                      ;[328a] 21 2b d8
                    rst       $20                           ;[328d] e7
                    ld        b,$05                         ;[328e] 06 05
                    ret       nc                            ;[3290] d0
                    ld        hl,$3f4b                      ;[3291] 21 4b 3f
                    ld        de,$d633                      ;[3294] 11 33 d6
                    call      $3f64                         ;[3297] cd 64 3f
                    ld        ix,$f700                      ;[329a] dd 21 00 f7
                    ld        a,($d63a)                     ;[329e] 3a 3a d6
                    call      $359c                         ;[32a1] cd 9c 35
                    ld        hl,$0f1c                      ;[32a4] 21 1c 0f
                    call      $3409                         ;[32a7] cd 09 34
                    call      $3342                         ;[32aa] cd 42 33
                    ld        hl,$e4c9                      ;[32ad] 21 c9 e4
                    call      $3409                         ;[32b0] cd 09 34
                    ld        a,($d639)                     ;[32b3] 3a 39 d6
                    ld        e,a                           ;[32b6] 5f
                    ld        hl,$5aa9                      ;[32b7] 21 a9 5a
                    ld        (hl),e                        ;[32ba] 73
                    ld        hl,$5ab7                      ;[32bb] 21 b7 5a
                    ld        a,($d5ea)                     ;[32be] 3a ea d5
                    bit       4,a                           ;[32c1] cb 67
                    jr        z,$32c6                       ;[32c3] 28 01
                    ld        (hl),e                        ;[32c5] 73
                    inc       hl                            ;[32c6] 23
                    bit       5,a                           ;[32c7] cb 6f
                    jr        nz,$32cc                      ;[32c9] 20 01
                    ld        (hl),e                        ;[32cb] 73
                    inc       hl                            ;[32cc] 23
                    bit       2,a                           ;[32cd] cb 57
                    jr        z,$32d2                       ;[32cf] 28 01
                    ld        (hl),e                        ;[32d1] 73
                    call      $3490                         ;[32d2] cd 90 34
                    call      $3532                         ;[32d5] cd 32 35
                    call      $3327                         ;[32d8] cd 27 33
                    call      $2ceb                         ;[32db] cd eb 2c
                    nextreg $57,$10                         ;[32de] ed 91 57 10
                    ld        hl,$d930                      ;[32e2] 21 30 d9
                    ld        de,$e3c9                      ;[32e5] 11 c9 e3
                    ld        ixl,$00                       ;[32e8] dd 2e 00
                    ld        a,(de)                        ;[32eb] 1a
                    cp        (hl)                          ;[32ec] be
                    jr        z,$32f3                       ;[32ed] 28 04
                    ld        ixl,$01                       ;[32ef] dd 2e 01
                    ld        a,(hl)                        ;[32f2] 7e
                    ldi                                     ;[32f3] ed a0
                    inc       a                             ;[32f5] 3c
                    jr        nz,$32eb                      ;[32f6] 20 f3
                    dec       ixl                           ;[32f8] dd 2d
                    jr        z,$3324                       ;[32fa] 28 28
                    ld        hl,($e4dd)                    ;[32fc] 2a dd e4
                    nextreg $57,$0f                         ;[32ff] ed 91 57 0f
                    ld        a,($d5e4)                     ;[3303] 3a e4 d5
                    cp        l                             ;[3306] bd
                    jr        z,$3319                       ;[3307] 28 10
                    ld        a,($d5e2)                     ;[3309] 3a e2 d5
                    cp        $f7                           ;[330c] fe f7
                    jr        nz,$3317                      ;[330e] 20 07
                    push      hl                            ;[3310] e5
                    call      $2cd9                         ;[3311] cd d9 2c
                    pop       hl                            ;[3314] e1
                    jr        $3303                         ;[3315] 18 ec
                    ld        h,$ff                         ;[3317] 26 ff
                    ld        a,($d5e2)                     ;[3319] 3a e2 d5
                    and       a                             ;[331c] a7
                    jr        z,$3324                       ;[331d] 28 05
                    dec       a                             ;[331f] 3d
                    cp        h                             ;[3320] bc
                    jr        c,$3324                       ;[3321] 38 01
                    ld        a,h                           ;[3323] 7c
                    jp        $3779                         ;[3324] c3 79 37
                    ld        hl,($d5d0)                    ;[3327] 2a d0 d5
                    ld        a,($d5dc)                     ;[332a] 3a dc d5
                    add       a                             ;[332d] 87
                    jr        nc,$3333                      ;[332e] 30 03
                    ld        hl,$0e6b                      ;[3330] 21 6b 0e
                    res       5,(iy+$30)                    ;[3333] fd cb 30 ae
                    push      hl                            ;[3337] e5
                    call      $3401                         ;[3338] cd 01 34
                    call      $3406                         ;[333b] cd 06 34
                    pop       hl                            ;[333e] e1
                    jp        $274f                         ;[333f] c3 4f 27
                    xor       a                             ;[3342] af
                    call      $33c8                         ;[3343] cd c8 33
                    jr        nz,$335c                      ;[3346] 20 14
                    ld        hl,$d83a                      ;[3348] 21 3a d8
                    ld        de,$d5ea                      ;[334b] 11 ea d5
                    ldi                                     ;[334e] ed a0
                    ldi                                     ;[3350] ed a0
                    ld        c,(hl)                        ;[3352] 4e
                    inc       hl                            ;[3353] 23
                    ld        b,(hl)                        ;[3354] 46
                    ld        hl,$62ff                      ;[3355] 21 ff 62
                    and       a                             ;[3358] a7
                    sbc       hl,bc                         ;[3359] ed 42
                    ret       z                             ;[335b] c8
                    xor       a                             ;[335c] af
                    dec       de                            ;[335d] 1b
                    ld        (de),a                        ;[335e] 12
                    dec       de                            ;[335f] 1b
                    ld        (de),a                        ;[3360] 12
                    ld        hl,$000b                      ;[3361] 21 0b 00
                    call      $33a3                         ;[3364] cd a3 33
                    ret                                     ;[3367] c9

                    pop       af                            ;[3368] f1
                    cp        $08                           ;[3369] fe 08
                    jr        c,$3370                       ;[336b] 38 03
                    ld        a,$15                         ;[336d] 3e 15
                    ret                                     ;[336f] c9

                    and       a                             ;[3370] a7
                    jr        z,$337d                       ;[3371] 28 0a
                    dec       a                             ;[3373] 3d
                    jp        nz,$0a6f                      ;[3374] c2 6f 0a
                    ld        ($d5ea),bc                    ;[3377] ed 43 ea d5
                    jr        $33a3                         ;[337b] 18 26
                    push      hl                            ;[337d] e5
                    call      $3342                         ;[337e] cd 42 33
                    pop       de                            ;[3381] d1
                    ld        hl,$d82b                      ;[3382] 21 2b d8
                    ld        bc,$000f                      ;[3385] 01 0f 00
                    ldir                                    ;[3388] ed b0
                    ld        bc,($d5ea)                    ;[338a] ed 4b ea d5
                    scf                                     ;[338e] 37
                    ret                                     ;[338f] c9

                    push      bc                            ;[3390] c5
                    xor       a                             ;[3391] af
                    call      $33c8                         ;[3392] cd c8 33
                    pop       bc                            ;[3395] c1
                    ld        hl,$d5ea                      ;[3396] 21 ea d5
                    ld        a,(hl)                        ;[3399] 7e
                    xor       b                             ;[339a] a8
                    ld        (hl),a                        ;[339b] 77
                    inc       hl                            ;[339c] 23
                    ld        a,(hl)                        ;[339d] 7e
                    xor       c                             ;[339e] a9
                    ld        (hl),a                        ;[339f] 77
                    ld        hl,$d82b                      ;[33a0] 21 2b d8
                    nextreg $57,$10                         ;[33a3] ed 91 57 10
                    ld        de,$0000                      ;[33a7] 11 00 00
                    ld        ($e4dd),de                    ;[33aa] ed 53 dd e4
                    nextreg $57,$0f                         ;[33ae] ed 91 57 0f
                    ld        de,$d82b                      ;[33b2] 11 2b d8
                    ld        bc,$000f                      ;[33b5] 01 0f 00
                    ldir                                    ;[33b8] ed b0
                    ld        hl,$d5ea                      ;[33ba] 21 ea d5
                    ld        c,$02                         ;[33bd] 0e 02
                    ldir                                    ;[33bf] ed b0
                    ex        de,hl                         ;[33c1] eb
                    ld        (hl),$ff                      ;[33c2] 36 ff
                    inc       hl                            ;[33c4] 23
                    ld        (hl),$62                      ;[33c5] 36 62
                    scf                                     ;[33c7] 37
                    ld        hl,$d82b                      ;[33c8] 21 2b d8
                    ld        de,$e4c9                      ;[33cb] 11 c9 e4
                    ld        b,$13                         ;[33ce] 06 13
                    jr        c,$33d3                       ;[33d0] 38 01
                    ex        de,hl                         ;[33d2] eb
                    nextreg $57,$10                         ;[33d3] ed 91 57 10
                    ld        c,$00                         ;[33d7] 0e 00
                    ld        a,(hl)                        ;[33d9] 7e
                    inc       hl                            ;[33da] 23
                    ld        (de),a                        ;[33db] 12
                    inc       de                            ;[33dc] 13
                    add       c                             ;[33dd] 81
                    ld        c,a                           ;[33de] 4f
                    djnz      $33d9                         ;[33df] 10 f8
                    ld        a,c                           ;[33e1] 79
                    cp        (hl)                          ;[33e2] be
                    ld        (de),a                        ;[33e3] 12
                    nextreg $57,$0f                         ;[33e4] ed 91 57 0f
                    scf                                     ;[33e8] 37
                    ret                                     ;[33e9] c9

                    ld        hl,$e3b6                      ;[33ea] 21 b6 e3
                    ld        de,$5b89                      ;[33ed] 11 89 5b
                    ld        bc,$0022                      ;[33f0] 01 22 00
                    jr        c,$33f6                       ;[33f3] 38 01
                    ex        de,hl                         ;[33f5] eb
                    nextreg $57,$10                         ;[33f6] ed 91 57 10
                    lddr                                    ;[33fa] ed b8
                    nextreg $57,$0f                         ;[33fc] ed 91 57 0f
                    ret                                     ;[3400] c9

                    ld        hl,$0ede                      ;[3401] 21 de 0e
                    jr        $3409                         ;[3404] 18 03
                    ld        hl,$0f49                      ;[3406] 21 49 0f
                    ld        ix,$f700                      ;[3409] dd 21 00 f7
                    jp        $333f                         ;[340d] c3 3f 33
                    push      af                            ;[3410] f5
                    push      hl                            ;[3411] e5
                    add       hl,$0008                      ;[3412] ed 34 08 00
                    call      $3409                         ;[3416] cd 09 34
                    pop       hl                            ;[3419] e1
                    pop       af                            ;[341a] f1
                    and       $03                           ;[341b] e6 03
                    add       a                             ;[341d] 87
                    add       hl,a                          ;[341e] ed 31
                    ld        a,(hl)                        ;[3420] 7e
                    inc       hl                            ;[3421] 23
                    ld        h,(hl)                        ;[3422] 66
                    ld        l,a                           ;[3423] 6f
                    jr        $3409                         ;[3424] 18 e3
                    ld        de,$d82b                      ;[3426] 11 2b d8
                    call      $3437                         ;[3429] cd 37 34
                    ld        hl,($d5e6)                    ;[342c] 2a e6 d5
                    add       hl,$0007                      ;[342f] ed 34 07 00
                    and       a                             ;[3433] a7
                    bit       7,(hl)                        ;[3434] cb 7e
                    ret                                     ;[3436] c9

                    ld        hl,($d5e6)                    ;[3437] 2a e6 d5
                    push      hl                            ;[343a] e5
                    ex        (sp),hl                       ;[343b] e3
                    ld        b,$08                         ;[343c] 06 08
                    call      $344f                         ;[343e] cd 4f 34
                    ex        (sp),hl                       ;[3441] e3
                    ld        a,(hl)                        ;[3442] 7e
                    pop       hl                            ;[3443] e1
                    and       $7f                           ;[3444] e6 7f
                    cp        $2e                           ;[3446] fe 2e
                    ret       z                             ;[3448] c8
                    ld        a,$2e                         ;[3449] 3e 2e
                    ld        (de),a                        ;[344b] 12
                    inc       de                            ;[344c] 13
                    ld        b,$03                         ;[344d] 06 03
                    ld        a,(hl)                        ;[344f] 7e
                    inc       hl                            ;[3450] 23
                    and       $7f                           ;[3451] e6 7f
                    cp        $20                           ;[3453] fe 20
                    jr        z,$3459                       ;[3455] 28 02
                    ld        (de),a                        ;[3457] 12
                    inc       de                            ;[3458] 13
                    djnz      $344f                         ;[3459] 10 f4
                    ld        a,$ff                         ;[345b] 3e ff
                    ld        (de),a                        ;[345d] 12
                    ret                                     ;[345e] c9

                    ld        a,($d5d4)                     ;[345f] 3a d4 d5
                    and       $20                           ;[3462] e6 20
                    ret       z                             ;[3464] c8
                    ld        a,$ff                         ;[3465] 3e ff
                    rst       $20                           ;[3467] e7
                    dec       l                             ;[3468] 2d
                    ld        bc,$43fe                      ;[3469] 01 fe 43
                    ret       z                             ;[346c] c8
                    ld        l,a                           ;[346d] 6f
                    rst       $20                           ;[346e] e7
                    call      p,$d000                       ;[346f] f4 00 d0
                    call      $3bbd                         ;[3472] cd bd 3b
                    ld        a,$ff                         ;[3475] 3e ff
                    rst       $20                           ;[3477] e7
                    dec       l                             ;[3478] 2d
                    ld        bc,$3c4f                      ;[3479] 01 4f 3c
                    cp        $51                           ;[347c] fe 51
                    jr        c,$3482                       ;[347e] 38 02
                    ld        a,$41                         ;[3480] 3e 41
                    cp        c                             ;[3482] b9
                    ret       z                             ;[3483] c8
                    ld        b,a                           ;[3484] 47
                    push      bc                            ;[3485] c5
                    rst       $20                           ;[3486] e7
                    dec       l                             ;[3487] 2d
                    ld        bc,$78c1                      ;[3488] 01 c1 78
                    jp        c,$3291                       ;[348b] da 91 32
                    jr        $347b                         ;[348e] 18 eb
                    ld        bc,$0000                      ;[3490] 01 00 00
                    call      $0d04                         ;[3493] cd 04 0d
                    ld        hl,$d930                      ;[3496] 21 30 d9
                    push      hl                            ;[3499] e5
                    ld        a,$ff                         ;[349a] 3e ff
                    rst       $20                           ;[349c] e7
                    dec       l                             ;[349d] 2d
                    ld        bc,$7932                      ;[349e] 01 32 79
                    ld        e,e                           ;[34a1] 5b
                    ld        ($5b7a),a                     ;[34a2] 32 7a 5b
                    pop       hl                            ;[34a5] e1
                    push      hl                            ;[34a6] e5
                    ld        (hl),a                        ;[34a7] 77
                    inc       hl                            ;[34a8] 23
                    ld        (hl),$3a                      ;[34a9] 36 3a
                    inc       hl                            ;[34ab] 23
                    ld        (hl),$ff                      ;[34ac] 36 ff
                    pop       hl                            ;[34ae] e1
                    push      hl                            ;[34af] e5
                    ld        a,$01                         ;[34b0] 3e 01
                    rst       $20                           ;[34b2] e7
                    or        c                             ;[34b3] b1
                    ld        bc,$8221                      ;[34b4] 01 21 82
                    rrca                                    ;[34b7] 0f
                    call      $3409                         ;[34b8] cd 09 34
                    pop       hl                            ;[34bb] e1
                    xor       a                             ;[34bc] af
                    ld        b,(hl)                        ;[34bd] 46
                    inc       hl                            ;[34be] 23
                    inc       a                             ;[34bf] 3c
                    inc       b                             ;[34c0] 04
                    jr        nz,$34bd                      ;[34c1] 20 fa
                    dec       hl                            ;[34c3] 2b
                    dec       a                             ;[34c4] 3d
                    ld        bc,$0033                      ;[34c5] 01 33 00
                    cp        c                             ;[34c8] b9
                    jr        nc,$34cc                      ;[34c9] 30 01
                    ld        c,a                           ;[34cb] 4f
                    ld        de,$da35                      ;[34cc] 11 35 da
                    ld        a,c                           ;[34cf] 79
                    add       de,a                          ;[34d0] ed 32
                    lddr                                    ;[34d2] ed b8
                    ldd                                     ;[34d4] ed a8
                    ld        c,$33                         ;[34d6] 0e 33
                    call      $3b8c                         ;[34d8] cd 8c 3b
                    ld        l,$00                         ;[34db] 2e 00
                    ld        a,($d63a)                     ;[34dd] 3a 3a d6
                    jr        $3520                         ;[34e0] 18 3e
                    push      af                            ;[34e2] f5
                    call      $342c                         ;[34e3] cd 2c 34
                    pop       bc                            ;[34e6] c1
                    jr        nz,$34f6                      ;[34e7] 20 0d
                    ld        c,$01                         ;[34e9] 0e 01
                    call      $2dc2                         ;[34eb] cd c2 2d
                    ld        a,$00                         ;[34ee] 3e 00
                    ret       nc                            ;[34f0] d0
                    ld        a,$01                         ;[34f1] 3e 01
                    ret       z                             ;[34f3] c8
                    inc       a                             ;[34f4] 3c
                    ret                                     ;[34f5] c9

                    call      $31f4                         ;[34f6] cd f4 31
                    ld        a,$03                         ;[34f9] 3e 03
                    ret       z                             ;[34fb] c8
                    inc       a                             ;[34fc] 3c
                    ret                                     ;[34fd] c9

                    xor       a                             ;[34fe] af
                    call      $34e2                         ;[34ff] cd e2 34
                    ld        hl,$d63b                      ;[3502] 21 3b d6
                    jr        $3516                         ;[3505] 18 0f
                    ld        hl,$d645                      ;[3507] 21 45 d6
                    ld        a,(hl)                        ;[350a] 7e
                    dec       hl                            ;[350b] 2b
                    dec       hl                            ;[350c] 2b
                    push      hl                            ;[350d] e5
                    sub       (hl)                          ;[350e] 96
                    inc       a                             ;[350f] 3c
                    push      de                            ;[3510] d5
                    call      $34e2                         ;[3511] cd e2 34
                    pop       de                            ;[3514] d1
                    pop       hl                            ;[3515] e1
                    add       hl,a                          ;[3516] ed 31
                    ld        a,(hl)                        ;[3518] 7e
                    ld        hl,($d5e8)                    ;[3519] 2a e8 d5
                    add       hl,$0002                      ;[351c] ed 34 02 00
                    ld        bc,$5800                      ;[3520] 01 00 58
                    ld        h,c                           ;[3523] 61
                    add       hl,hl                         ;[3524] 29
                    add       hl,hl                         ;[3525] 29
                    add       hl,hl                         ;[3526] 29
                    add       hl,hl                         ;[3527] 29
                    add       hl,hl                         ;[3528] 29
                    add       hl,bc                         ;[3529] 09
                    ld        c,$20                         ;[352a] 0e 20
                    ld        (hl),a                        ;[352c] 77
                    inc       hl                            ;[352d] 23
                    dec       c                             ;[352e] 0d
                    jr        nz,$352c                      ;[352f] 20 fb
                    ret                                     ;[3531] c9

                    ld        bc,$0101                      ;[3532] 01 01 01
                    call      $0d04                         ;[3535] cd 04 0d
                    ld        l,$01                         ;[3538] 2e 01
                    call      $34dd                         ;[353a] cd dd 34
                    ld        a,($d63a)                     ;[353d] 3a 3a d6
                    call      $359c                         ;[3540] cd 9c 35
                    ld        hl,$0e96                      ;[3543] 21 96 0e
                    call      $3409                         ;[3546] cd 09 34
                    ld        a,($d5ea)                     ;[3549] 3a ea d5
                    and       $40                           ;[354c] e6 40
                    ld        a,$6e                         ;[354e] 3e 6e
                    jr        z,$3559                       ;[3550] 28 07
                    ld        a,$66                         ;[3552] 3e 66
                    call      $277f                         ;[3554] cd 7f 27
                    ld        a,$66                         ;[3557] 3e 66
                    call      $277f                         ;[3559] cd 7f 27
                    call      $35ab                         ;[355c] cd ab 35
                    push      af                            ;[355f] f5
                    ld        hl,$0ed3                      ;[3560] 21 d3 0e
                    and       $07                           ;[3563] e6 07
                    call      z,$3409                       ;[3565] cc 09 34
                    pop       af                            ;[3568] f1
                    push      af                            ;[3569] f5
                    ld        hl,$0f68                      ;[356a] 21 68 0f
                    call      $3410                         ;[356d] cd 10 34
                    ld        hl,$0f75                      ;[3570] 21 75 0f
                    ld        a,($d5ea)                     ;[3573] 3a ea d5
                    call      $3410                         ;[3576] cd 10 34
                    ld        a,($d639)                     ;[3579] 3a 39 d6
                    ld        c,a                           ;[357c] 4f
                    ld        hl,$5820                      ;[357d] 21 20 58
                    ld        (hl),c                        ;[3580] 71
                    ld        l,$2b                         ;[3581] 2e 2b
                    ld        (hl),c                        ;[3583] 71
                    ld        l,$32                         ;[3584] 2e 32
                    ld        (hl),c                        ;[3586] 71
                    ld        l,$33                         ;[3587] 2e 33
                    ld        (hl),c                        ;[3589] 71
                    ld        l,$36                         ;[358a] 2e 36
                    ld        (hl),c                        ;[358c] 71
                    ld        l,$3a                         ;[358d] 2e 3a
                    ld        (hl),c                        ;[358f] 71
                    pop       af                            ;[3590] f1
                    rra                                     ;[3591] 1f
                    rra                                     ;[3592] 1f
                    and       $01                           ;[3593] e6 01
                    add       $27                           ;[3595] c6 27
                    ld        l,a                           ;[3597] 6f
                    ld        (hl),c                        ;[3598] 71
                    ld        a,($d633)                     ;[3599] 3a 33 d6
                    ld        ix,$f700                      ;[359c] dd 21 00 f7
                    push      af                            ;[35a0] f5
                    ld        a,$18                         ;[35a1] 3e 18
                    call      $277f                         ;[35a3] cd 7f 27
                    pop       af                            ;[35a6] f1
                    call      $277f                         ;[35a7] cd 7f 27
                    ret                                     ;[35aa] c9

                    ld        a,($d5d5)                     ;[35ab] 3a d5 d5
                    and       $08                           ;[35ae] e6 08
                    ld        a,$01                         ;[35b0] 3e 01
                    ret       z                             ;[35b2] c8
                    ld        a,($d5eb)                     ;[35b3] 3a eb d5
                    scf                                     ;[35b6] 37
                    ret                                     ;[35b7] c9

                    call      $35ab                         ;[35b8] cd ab 35
                    and       $07                           ;[35bb] e6 07
                    ret       nz                            ;[35bd] c0
                    ld        hl,$d5ea                      ;[35be] 21 ea d5
                    res       7,(hl)                        ;[35c1] cb be
                    ld        hl,$0f87                      ;[35c3] 21 87 0f
                    call      $3333                         ;[35c6] cd 33 33
                    ld        hl,$0f90                      ;[35c9] 21 90 0f
                    call      $3409                         ;[35cc] cd 09 34
                    ld        de,$0000                      ;[35cf] 11 00 00
                    ld        b,$03                         ;[35d2] 06 03
                    push      bc                            ;[35d4] c5
                    push      de                            ;[35d5] d5
                    ld        b,$00                         ;[35d6] 06 00
                    ld        c,e                           ;[35d8] 4b
                    ld        hl,$100b                      ;[35d9] 21 0b 10
                    ld        ($5b8a),hl                    ;[35dc] 22 8a 5b
                    ld        hl,$e1d2                      ;[35df] 21 d2 e1
                    ld        de,$c400                      ;[35e2] 11 00 c4
                    ld        a,c                           ;[35e5] 79
                    and       a                             ;[35e6] a7
                    call      nz,$1947                      ;[35e7] c4 47 19
                    xor       a                             ;[35ea] af
                    ld        (de),a                        ;[35eb] 12
                    call      $363a                         ;[35ec] cd 3a 36
                    ld        a,b                           ;[35ef] 78
                    and       a                             ;[35f0] a7
                    jr        nz,$35fc                      ;[35f1] 20 09
                    call      $3507                         ;[35f3] cd 07 35
                    ld        a,d                           ;[35f6] 7a
                    call      $377e                         ;[35f7] cd 7e 37
                    jr        $3600                         ;[35fa] 18 04
                    ld        a,d                           ;[35fc] 7a
                    call      $3779                         ;[35fd] cd 79 37
                    call      $34fe                         ;[3600] cd fe 34
                    ld        de,$0816                      ;[3603] 11 16 08
                    call      $2cb5                         ;[3606] cd b5 2c
                    pop       de                            ;[3609] d1
                    pop       bc                            ;[360a] c1
                    ld        a,b                           ;[360b] 78
                    and       a                             ;[360c] a7
                    jr        z,$3634                       ;[360d] 28 25
                    xor       a                             ;[360f] af
                    ld        b,$07                         ;[3610] 06 07
                    ld        c,$29                         ;[3612] 0e 29
                    ld        hl,$e1d2                      ;[3614] 21 d2 e1
                    call      $27f9                         ;[3617] cd f9 27
                    bit       1,b                           ;[361a] cb 48
                    jr        z,$35d4                       ;[361c] 28 b6
                    xor       a                             ;[361e] af
                    ld        ($5c41),a                     ;[361f] 32 41 5c
                    push      bc                            ;[3622] c5
                    push      de                            ;[3623] d5
                    ld        a,c                           ;[3624] 79
                    cp        $0e                           ;[3625] fe 0e
                    jr        nz,$3603                      ;[3627] 20 da
                    ld        hl,$d5ea                      ;[3629] 21 ea d5
                    ld        a,(hl)                        ;[362c] 7e
                    xor       $80                           ;[362d] ee 80
                    ld        (hl),a                        ;[362f] 77
                    pop       de                            ;[3630] d1
                    pop       bc                            ;[3631] c1
                    jr        $35d4                         ;[3632] 18 a0
                    set       5,(iy+$30)                    ;[3634] fd cb 30 ee
                    scf                                     ;[3638] 37
                    ret                                     ;[3639] c9

                    ld        bc,$0000                      ;[363a] 01 00 00
                    ld        a,($d5e4)                     ;[363d] 3a e4 d5
                    and       a                             ;[3640] a7
                    jr        nz,$3645                      ;[3641] 20 02
                    ld        c,$01                         ;[3643] 0e 01
                    ld        de,$0000                      ;[3645] 11 00 00
                    call      $3691                         ;[3648] cd 91 36
                    ret       z                             ;[364b] c8
                    jr        c,$3676                       ;[364c] 38 28
                    ld        a,($d5e2)                     ;[364e] 3a e2 d5
                    ld        d,a                           ;[3651] 57
                    dec       d                             ;[3652] 15
                    call      $3691                         ;[3653] cd 91 36
                    ret       z                             ;[3656] c8
                    jr        nc,$3685                      ;[3657] 30 2c
                    ld        a,e                           ;[3659] 7b
                    cp        d                             ;[365a] ba
                    ret       z                             ;[365b] c8
                    inc       a                             ;[365c] 3c
                    cp        d                             ;[365d] ba
                    ret       z                             ;[365e] c8
                    add       d                             ;[365f] 82
                    rra                                     ;[3660] 1f
                    push      af                            ;[3661] f5
                    push      de                            ;[3662] d5
                    ld        d,a                           ;[3663] 57
                    call      $3691                         ;[3664] cd 91 36
                    pop       hl                            ;[3667] e1
                    pop       ix                            ;[3668] dd e1
                    ret       z                             ;[366a] c8
                    ex        de,hl                         ;[366b] eb
                    jr        c,$3672                       ;[366c] 38 04
                    ld        e,ixh                         ;[366e] dd 5c
                    jr        $3659                         ;[3670] 18 e7
                    ld        d,ixh                         ;[3672] dd 54
                    jr        $3659                         ;[3674] 18 e3
                    bit       0,c                           ;[3676] cb 41
                    ret       nz                            ;[3678] c0
                    push      bc                            ;[3679] c5
                    call      $371b                         ;[367a] cd 1b 37
                    pop       bc                            ;[367d] c1
                    set       0,b                           ;[367e] cb c0
                    jr        c,$363d                       ;[3680] 38 bb
                    ld        d,$00                         ;[3682] 16 00
                    ret                                     ;[3684] c9

                    push      bc                            ;[3685] c5
                    push      de                            ;[3686] d5
                    call      $3732                         ;[3687] cd 32 37
                    pop       de                            ;[368a] d1
                    pop       bc                            ;[368b] c1
                    set       0,b                           ;[368c] cb c0
                    jr        c,$363d                       ;[368e] 38 ad
                    ret                                     ;[3690] c9

                    push      de                            ;[3691] d5
                    inc       d                             ;[3692] 14
                    ld        e,$0d                         ;[3693] 1e 0d
                    mul       d,e                           ;[3695] ed 30
                    add       de,$c738                      ;[3697] ed 35 38 c7
                    ex        de,hl                         ;[369b] eb
                    ld        a,($d5ea)                     ;[369c] 3a ea d5
                    bit       6,a                           ;[369f] cb 77
                    jr        z,$36bc                       ;[36a1] 28 19
                    push      hl                            ;[36a3] e5
                    add       hl,$0007                      ;[36a4] ed 34 07 00
                    bit       7,(hl)                        ;[36a8] cb 7e
                    pop       hl                            ;[36aa] e1
                    jr        nz,$36b5                      ;[36ab] 20 08
                    add       a                             ;[36ad] 87
                    jr        nc,$36bc                      ;[36ae] 30 0c
                    xor       a                             ;[36b0] af
                    inc       a                             ;[36b1] 3c
                    scf                                     ;[36b2] 37
                    pop       de                            ;[36b3] d1
                    ret                                     ;[36b4] c9

                    add       a                             ;[36b5] 87
                    jr        c,$36bc                       ;[36b6] 38 04
                    xor       a                             ;[36b8] af
                    inc       a                             ;[36b9] 3c
                    pop       de                            ;[36ba] d1
                    ret                                     ;[36bb] c9

                    push      bc                            ;[36bc] c5
                    call      $38da                         ;[36bd] cd da 38
                    ld        hl,$da35                      ;[36c0] 21 35 da
                    call      $3874                         ;[36c3] cd 74 38
                    pop       bc                            ;[36c6] c1
                    ld        hl,$da34                      ;[36c7] 21 34 da
                    ld        de,$c3ff                      ;[36ca] 11 ff c3
                    inc       de                            ;[36cd] 13
                    inc       hl                            ;[36ce] 23
                    ld        a,(de)                        ;[36cf] 1a
                    and       a                             ;[36d0] a7
                    jr        z,$36e7                       ;[36d1] 28 14
                    cp        (hl)                          ;[36d3] be
                    jr        z,$36cd                       ;[36d4] 28 f7
                    push      bc                            ;[36d6] c5
                    call      $36ec                         ;[36d7] cd ec 36
                    ld        c,a                           ;[36da] 4f
                    ld        a,(hl)                        ;[36db] 7e
                    call      $36ec                         ;[36dc] cd ec 36
                    ld        b,a                           ;[36df] 47
                    ld        a,c                           ;[36e0] 79
                    cp        b                             ;[36e1] b8
                    pop       bc                            ;[36e2] c1
                    jr        z,$36cd                       ;[36e3] 28 e8
                    pop       de                            ;[36e5] d1
                    ret                                     ;[36e6] c9

                    ld        d,(hl)                        ;[36e7] 56
                    inc       d                             ;[36e8] 14
                    cp        d                             ;[36e9] ba
                    pop       de                            ;[36ea] d1
                    ret                                     ;[36eb] c9

                    cp        $61                           ;[36ec] fe 61
                    ret       c                             ;[36ee] d8
                    cp        $7b                           ;[36ef] fe 7b
                    ret       nc                            ;[36f1] d0
                    and       $df                           ;[36f2] e6 df
                    ret                                     ;[36f4] c9

                    ld        e,$01                         ;[36f5] 1e 01
                    jr        $36fb                         ;[36f7] 18 02
                    ld        e,$13                         ;[36f9] 1e 13
                    call      $3507                         ;[36fb] cd 07 35
                    ld        a,($d5e5)                     ;[36fe] 3a e5 d5
                    sub       e                             ;[3701] 93
                    ld        d,a                           ;[3702] 57
                    jr        nc,$377e                      ;[3703] 30 79
                    ld        a,($d5e4)                     ;[3705] 3a e4 d5
                    and       a                             ;[3708] a7
                    jr        nz,$3712                      ;[3709] 20 07
                    ld        a,d                           ;[370b] 7a
                    add       e                             ;[370c] 83
                    and       a                             ;[370d] a7
                    ret       z                             ;[370e] c8
                    xor       a                             ;[370f] af
                    jr        $377e                         ;[3710] 18 6c
                    push      de                            ;[3712] d5
                    call      $371b                         ;[3713] cd 1b 37
                    pop       af                            ;[3716] f1
                    add       $f7                           ;[3717] c6 f7
                    jr        $3779                         ;[3719] 18 5e
                    ld        a,($d5e4)                     ;[371b] 3a e4 d5
                    and       a                             ;[371e] a7
                    ret       z                             ;[371f] c8
                    dec       a                             ;[3720] 3d
                    push      af                            ;[3721] f5
                    call      $2cf0                         ;[3722] cd f0 2c
                    pop       af                            ;[3725] f1
                    scf                                     ;[3726] 37
                    ret       z                             ;[3727] c8
                    push      af                            ;[3728] f5
                    call      $2cd9                         ;[3729] cd d9 2c
                    pop       af                            ;[372c] f1
                    dec       a                             ;[372d] 3d
                    jr        nz,$3728                      ;[372e] 20 f8
                    scf                                     ;[3730] 37
                    ret                                     ;[3731] c9

                    ld        a,($d5e2)                     ;[3732] 3a e2 d5
                    cp        $f7                           ;[3735] fe f7
                    ccf                                     ;[3737] 3f
                    ret       nz                            ;[3738] c0
                    call      $2cd9                         ;[3739] cd d9 2c
                    ld        a,($d5e2)                     ;[373c] 3a e2 d5
                    and       a                             ;[373f] a7
                    scf                                     ;[3740] 37
                    ret       nz                            ;[3741] c0
                    call      $371b                         ;[3742] cd 1b 37
                    and       a                             ;[3745] a7
                    ret                                     ;[3746] c9

                    ld        e,$01                         ;[3747] 1e 01
                    jr        $374d                         ;[3749] 18 02
                    ld        e,$13                         ;[374b] 1e 13
                    call      $3507                         ;[374d] cd 07 35
                    ld        a,($d5e2)                     ;[3750] 3a e2 d5
                    ld        d,a                           ;[3753] 57
                    ld        a,($d5e5)                     ;[3754] 3a e5 d5
                    add       e                             ;[3757] 83
                    jr        c,$375d                       ;[3758] 38 03
                    cp        d                             ;[375a] ba
                    jr        c,$377e                       ;[375b] 38 21
                    sub       $f7                           ;[375d] d6 f7
                    ld        b,a                           ;[375f] 47
                    push      bc                            ;[3760] c5
                    call      $3732                         ;[3761] cd 32 37
                    pop       bc                            ;[3764] c1
                    jr        c,$3771                       ;[3765] 38 0a
                    ld        d,a                           ;[3767] 57
                    dec       d                             ;[3768] 15
                    ld        a,($d5e5)                     ;[3769] 3a e5 d5
                    cp        d                             ;[376c] ba
                    ret       z                             ;[376d] c8
                    ld        a,d                           ;[376e] 7a
                    jr        $377e                         ;[376f] 18 0d
                    ld        a,($d5e2)                     ;[3771] 3a e2 d5
                    dec       a                             ;[3774] 3d
                    cp        b                             ;[3775] b8
                    jr        c,$3779                       ;[3776] 38 01
                    ld        a,b                           ;[3778] 78
                    ld        hl,$d5e3                      ;[3779] 21 e3 d5
                    ld        (hl),$ff                      ;[377c] 36 ff
                    ld        ($d5e5),a                     ;[377e] 32 e5 d5
                    ld        hl,($d5e4)                    ;[3781] 2a e4 d5
                    ld        h,a                           ;[3784] 67
                    nextreg $57,$10                         ;[3785] ed 91 57 10
                    ld        ($e4dd),hl                    ;[3789] 22 dd e4
                    nextreg $57,$0f                         ;[378c] ed 91 57 0f
                    ld        b,$ff                         ;[3790] 06 ff
                    ld        hl,$c64e                      ;[3792] 21 4e c6
                    add       hl,$00f7                      ;[3795] ed 34 f7 00
                    inc       b                             ;[3799] 04
                    sub       $13                           ;[379a] d6 13
                    jr        nc,$3795                      ;[379c] 30 f7
                    add       $13                           ;[379e] c6 13
                    push      af                            ;[37a0] f5
                    push      hl                            ;[37a1] e5
                    ld        a,($d5e3)                     ;[37a2] 3a e3 d5
                    cp        b                             ;[37a5] b8
                    jp        z,$3860                       ;[37a6] ca 60 38
                    ld        a,b                           ;[37a9] 78
                    ld        ($d5e3),a                     ;[37aa] 32 e3 d5
                    ld        c,$13                         ;[37ad] 0e 13
                    ld        d,a                           ;[37af] 57
                    ld        e,c                           ;[37b0] 59
                    mul       d,e                           ;[37b1] ed 30
                    ld        a,($d5e2)                     ;[37b3] 3a e2 d5
                    sub       e                             ;[37b6] 93
                    cp        c                             ;[37b7] b9
                    jr        c,$37bb                       ;[37b8] 38 01
                    ld        a,c                           ;[37ba] 79
                    push      hl                            ;[37bb] e5
                    push      af                            ;[37bc] f5
                    ld        b,$02                         ;[37bd] 06 02
                    ld        c,$14                         ;[37bf] 0e 14
                    call      $0d04                         ;[37c1] cd 04 0d
                    pop       bc                            ;[37c4] c1
                    inc       b                             ;[37c5] 04
                    ld        c,$02                         ;[37c6] 0e 02
                    pop       hl                            ;[37c8] e1
                    xor       a                             ;[37c9] af
                    jp        $385c                         ;[37ca] c3 5c 38
                    ld        ($d5e8),a                     ;[37cd] 32 e8 d5
                    ld        ($d5e6),hl                    ;[37d0] 22 e6 d5
                    push      bc                            ;[37d3] c5
                    call      $38da                         ;[37d4] cd da 38
                    push      af                            ;[37d7] f5
                    call      $3bf5                         ;[37d8] cd f5 3b
                    ld        hl,$da35                      ;[37db] 21 35 da
                    ld        a,($d5ea)                     ;[37de] 3a ea d5
                    bit       2,a                           ;[37e1] cb 57
                    call      z,$3874                       ;[37e3] cc 74 38
                    pop       af                            ;[37e6] f1
                    pop       de                            ;[37e7] d1
                    push      de                            ;[37e8] d5
                    ld        d,$00                         ;[37e9] 16 00
                    call      $2cb5                         ;[37eb] cd b5 2c
                    ld        a,($d5ea)                     ;[37ee] 3a ea d5
                    and       $03                           ;[37f1] e6 03
                    ld        hl,$3870                      ;[37f3] 21 70 38
                    add       hl,a                          ;[37f6] ed 31
                    ld        c,(hl)                        ;[37f8] 4e
                    push      bc                            ;[37f9] c5
                    call      $3b8c                         ;[37fa] cd 8c 3b
                    pop       bc                            ;[37fd] c1
                    pop       de                            ;[37fe] d1
                    push      de                            ;[37ff] d5
                    ld        d,c                           ;[3800] 51
                    inc       d                             ;[3801] 14
                    call      $342c                         ;[3802] cd 2c 34
                    push      hl                            ;[3805] e5
                    jr        z,$381e                       ;[3806] 28 16
                    ld        d,$2e                         ;[3808] 16 2e
                    call      $2cb5                         ;[380a] cd b5 2c
                    pop       hl                            ;[380d] e1
                    call      $31f4                         ;[380e] cd f4 31
                    ld        hl,$001b                      ;[3811] 21 1b 00
                    jr        nz,$3819                      ;[3814] 20 03
                    ld        hl,$0041                      ;[3816] 21 41 00
                    call      $274b                         ;[3819] cd 4b 27
                    jr        $384c                         ;[381c] 18 2e
                    call      $2cb5                         ;[381e] cd b5 2c
                    pop       hl                            ;[3821] e1
                    ld        a,($d5ea)                     ;[3822] 3a ea d5
                    and       $03                           ;[3825] e6 03
                    jr        z,$384c                       ;[3827] 28 23
                    dec       a                             ;[3829] 3d
                    jr        nz,$3831                      ;[382a] 20 05
                    call      $3c05                         ;[382c] cd 05 3c
                    jr        $384c                         ;[382f] 18 1b
                    dec       a                             ;[3831] 3d
                    jr        nz,$3841                      ;[3832] 20 0d
                    ld        de,($5c3f)                    ;[3834] ed 5b 3f 5c
                    ld        bc,($5c74)                    ;[3838] ed 4b 74 5c
                    rst       $18                           ;[383c] df
                    jr        nz,$3876                      ;[383d] 20 37
                    jr        $384c                         ;[383f] 18 0b
                    call      $3c93                         ;[3841] cd 93 3c
                    ex        de,hl                         ;[3844] eb
                    inc       hl                            ;[3845] 23
                    inc       hl                            ;[3846] 23
                    ld        e,$04                         ;[3847] 1e 04
                    call      $2751                         ;[3849] cd 51 27
                    call      $3507                         ;[384c] cd 07 35
                    pop       bc                            ;[384f] c1
                    ld        hl,($d5e6)                    ;[3850] 2a e6 d5
                    add       hl,$000d                      ;[3853] ed 34 0d 00
                    ld        a,($d5e8)                     ;[3857] 3a e8 d5
                    inc       a                             ;[385a] 3c
                    inc       c                             ;[385b] 0c
                    dec       b                             ;[385c] 05
                    jp        nz,$37cd                      ;[385d] c2 cd 37
                    pop       hl                            ;[3860] e1
                    pop       af                            ;[3861] f1
                    ld        ($d5e8),a                     ;[3862] 32 e8 d5
                    ld        e,a                           ;[3865] 5f
                    ld        d,$0d                         ;[3866] 16 0d
                    mul       d,e                           ;[3868] ed 30
                    add       hl,de                         ;[386a] 19
                    ld        ($d5e6),hl                    ;[386b] 22 e6 d5
                    scf                                     ;[386e] 37
                    ret                                     ;[386f] c9

                    dec       l                             ;[3870] 2d
                    dec       h                             ;[3871] 25
                    ld        ($cd2d),hl                    ;[3872] 22 2d cd
                    add       b                             ;[3875] 80
                    jr        c,$3850                       ;[3876] 38 d8
                    ld        a,(hl)                        ;[3878] 7e
                    inc       hl                            ;[3879] 23
                    ld        (de),a                        ;[387a] 12
                    inc       de                            ;[387b] 13
                    inc       a                             ;[387c] 3c
                    jr        nz,$3878                      ;[387d] 20 f9
                    ret                                     ;[387f] c9

                    ld        bc,$0000                      ;[3880] 01 00 00
                    ld        a,$ff                         ;[3883] 3e ff
                    cpir                                    ;[3885] ed b1
                    dec       hl                            ;[3887] 2b
                    ld        d,h                           ;[3888] 54
                    ld        e,l                           ;[3889] 5d
                    push      bc                            ;[388a] c5
                    dec       hl                            ;[388b] 2b
                    ld        a,(hl)                        ;[388c] 7e
                    cp        $2e                           ;[388d] fe 2e
                    jr        z,$3899                       ;[388f] 28 08
                    inc       bc                            ;[3891] 03
                    bit       7,b                           ;[3892] cb 78
                    jr        nz,$388b                      ;[3894] 20 f5
                    ex        de,hl                         ;[3896] eb
                    pop       bc                            ;[3897] c1
                    push      bc                            ;[3898] c5
                    pop       de                            ;[3899] d1
                    ld        d,h                           ;[389a] 54
                    ld        e,l                           ;[389b] 5d
                    dec       hl                            ;[389c] 2b
                    ld        a,(hl)                        ;[389d] 7e
                    cp        $7d                           ;[389e] fe 7d
                    scf                                     ;[38a0] 37
                    ret       nz                            ;[38a1] c0
                    call      $38c4                         ;[38a2] cd c4 38
                    push      de                            ;[38a5] d5
                    ld        e,a                           ;[38a6] 5f
                    call      $38c4                         ;[38a7] cd c4 38
                    swapnib                                 ;[38aa] ed 23
                    or        e                             ;[38ac] b3
                    pop       de                            ;[38ad] d1
                    add       bc,a                          ;[38ae] ed 33
                    bit       7,b                           ;[38b0] cb 78
                    scf                                     ;[38b2] 37
                    ret       z                             ;[38b3] c8
                    ld        b,$ff                         ;[38b4] 06 ff
                    neg                                     ;[38b6] ed 44
                    add       $03                           ;[38b8] c6 03
                    ld        c,a                           ;[38ba] 4f
                    add       hl,bc                         ;[38bb] 09
                    ld        a,(hl)                        ;[38bc] 7e
                    cp        $7b                           ;[38bd] fe 7b
                    scf                                     ;[38bf] 37
                    ret       nz                            ;[38c0] c0
                    ex        de,hl                         ;[38c1] eb
                    and       a                             ;[38c2] a7
                    ret                                     ;[38c3] c9

                    dec       hl                            ;[38c4] 2b
                    ld        a,(hl)                        ;[38c5] 7e
                    sub       $30                           ;[38c6] d6 30
                    cp        $0a                           ;[38c8] fe 0a
                    ret       c                             ;[38ca] d8
                    sub       $07                           ;[38cb] d6 07
                    cp        $10                           ;[38cd] fe 10
                    ret       c                             ;[38cf] d8
                    sub       $20                           ;[38d0] d6 20
                    cp        $10                           ;[38d2] fe 10
                    ret       c                             ;[38d4] d8
                    xor       a                             ;[38d5] af
                    ret                                     ;[38d6] c9

                    ld        hl,($d5e6)                    ;[38d7] 2a e6 d5
                    push      hl                            ;[38da] e5
                    ld        ix,($d5dd)                    ;[38db] dd 2a dd d5
                    add       hl,$0007                      ;[38df] ed 34 07 00
                    bit       7,(hl)                        ;[38e3] cb 7e
                    jr        z,$38f2                       ;[38e5] 28 0b
                    ld        a,($d5ea)                     ;[38e7] 3a ea d5
                    and       $40                           ;[38ea] e6 40
                    jr        z,$38f2                       ;[38ec] 28 04
                    ld        ix,($d5df)                    ;[38ee] dd 2a df d5
                    pop       de                            ;[38f2] d1
                    ld        hl,$000b                      ;[38f3] 21 0b 00
                    call      $2cca                         ;[38f6] cd ca 2c
                    ld        bc,$da35                      ;[38f9] 01 35 da
                    rst       $20                           ;[38fc] e7
                    or        a                             ;[38fd] b7
                    ld        bc,$cdc9                      ;[38fe] 01 c9 cd
                    rst       $10                           ;[3901] d7
                    jr        c,$38d4                       ;[3902] 38 d0
                    ld        hl,$da35                      ;[3904] 21 35 da
                    ld        d,$5a                         ;[3907] 16 5a
                    call      $3919                         ;[3909] cd 19 39
                    ld        d,$02                         ;[390c] 16 02
                    call      $3941                         ;[390e] cd 41 39
                    ret       nc                            ;[3911] d0
                    ex        de,hl                         ;[3912] eb
                    ld        hl,$da35                      ;[3913] 21 35 da
                    jp        $3aed                         ;[3916] c3 ed 3a
                    push      de                            ;[3919] d5
                    push      hl                            ;[391a] e5
                    call      $3b45                         ;[391b] cd 45 3b
                    pop       hl                            ;[391e] e1
                    pop       bc                            ;[391f] c1
                    ld        d,b                           ;[3920] 50
                    push      de                            ;[3921] d5
                    push      hl                            ;[3922] e5
                    call      $3880                         ;[3923] cd 80 38
                    pop       hl                            ;[3926] e1
                    ex        de,hl                         ;[3927] eb
                    and       a                             ;[3928] a7
                    sbc       hl,de                         ;[3929] ed 52
                    ld        b,l                           ;[392b] 45
                    pop       de                            ;[392c] d1
                    ld        a,b                           ;[392d] 78
                    cp        e                             ;[392e] bb
                    ret       c                             ;[392f] d8
                    ld        b,e                           ;[3930] 43
                    ret                                     ;[3931] c9

                    ld        de,$0400                      ;[3932] 11 00 04
                    call      $3940                         ;[3935] cd 40 39
                    ret       nc                            ;[3938] d0
                    ld        a,$02                         ;[3939] 3e 02
                    rst       $20                           ;[393b] e7
                    or        c                             ;[393c] b1
                    ld        bc,$6018                      ;[393d] 01 18 60
                    ld        b,e                           ;[3940] 43
                    ld        a,($d5d4)                     ;[3941] 3a d4 d5
                    and       d                             ;[3944] a2
                    ret       z                             ;[3945] c8
                    ld        d,$5a                         ;[3946] 16 5a
                    ld        hl,$0ef0                      ;[3948] 21 f0 0e
                    jr        $394e                         ;[394b] 18 01
                    ld        b,e                           ;[394d] 43
                    push      bc                            ;[394e] c5
                    push      de                            ;[394f] d5
                    call      $3333                         ;[3950] cd 33 33
                    pop       de                            ;[3953] d1
                    pop       bc                            ;[3954] c1
                    ld        c,d                           ;[3955] 4a
                    ld        d,b                           ;[3956] 50
                    ld        b,$04                         ;[3957] 06 04
                    xor       a                             ;[3959] af
                    ld        hl,$e152                      ;[395a] 21 52 e1
                    call      $27f9                         ;[395d] cd f9 27
                    set       5,(iy+$30)                    ;[3960] fd cb 30 ee
                    ld        a,e                           ;[3964] 7b
                    and       a                             ;[3965] a7
                    ret       z                             ;[3966] c8
                    push      de                            ;[3967] d5
                    ld        hl,$100b                      ;[3968] 21 0b 10
                    ld        ($5b8a),hl                    ;[396b] 22 8a 5b
                    ld        hl,$e152                      ;[396e] 21 52 e1
                    ld        de,$d82b                      ;[3971] 11 2b d8
                    ld        bc,$005a                      ;[3974] 01 5a 00
                    push      de                            ;[3977] d5
                    call      $1947                         ;[3978] cd 47 19
                    pop       de                            ;[397b] d1
                    pop       hl                            ;[397c] e1
                    ld        h,$00                         ;[397d] 26 00
                    add       hl,de                         ;[397f] 19
                    ld        (hl),$ff                      ;[3980] 36 ff
                    dec       hl                            ;[3982] 2b
                    ld        a,(hl)                        ;[3983] 7e
                    cp        $20                           ;[3984] fe 20
                    jr        z,$3980                       ;[3986] 28 f8
                    ex        de,hl                         ;[3988] eb
                    ld        a,(hl)                        ;[3989] 7e
                    cp        $20                           ;[398a] fe 20
                    scf                                     ;[398c] 37
                    ret       nz                            ;[398d] c0
                    inc       hl                            ;[398e] 23
                    jr        $3989                         ;[398f] 18 f8
                    ld        hl,$2a7c                      ;[3991] 21 7c 2a
                    ld        e,$08                         ;[3994] 1e 08
                    call      $3b6f                         ;[3996] cd 6f 3b
                    jr        z,$39a6                       ;[3999] 28 0b
                    ld        a,$03                         ;[399b] 3e 03
                    rst       $20                           ;[399d] e7
                    or        c                             ;[399e] b1
                    ld        bc,$0bd4                      ;[399f] 01 d4 0b
                    dec       sp                            ;[39a2] 3b
                    jp        $3291                         ;[39a3] c3 91 32
                    push      hl                            ;[39a6] e5
                    ld        hl,$0f11                      ;[39a7] 21 11 0f
                    call      $3337                         ;[39aa] cd 37 33
                    pop       hl                            ;[39ad] e1
                    rst       $20                           ;[39ae] e7
                    inc       h                             ;[39af] 24
                    ld        bc,$00c3                      ;[39b0] 01 c3 00
                    dec       sp                            ;[39b3] 3b
                    ld        de,$0e00                      ;[39b4] 11 00 0e
                    ld        hl,$0f2d                      ;[39b7] 21 2d 0f
                    call      $394d                         ;[39ba] cd 4d 39
                    ld        hl,$d82b                      ;[39bd] 21 2b d8
                    jr        c,$39c5                       ;[39c0] 38 03
                    ld        hl,$000b                      ;[39c2] 21 0b 00
                    call      $33a3                         ;[39c5] cd a3 33
                    jr        $39a3                         ;[39c8] 18 d9
                    ld        de,$0200                      ;[39ca] 11 00 02
                    ld        hl,$0f35                      ;[39cd] 21 35 0f
                    call      $394d                         ;[39d0] cd 4d 39
                    ld        e,$00                         ;[39d3] 1e 00
                    jr        nc,$39ee                      ;[39d5] 30 17
                    ld        hl,$d82b                      ;[39d7] 21 2b d8
                    ld        a,(hl)                        ;[39da] 7e
                    inc       hl                            ;[39db] 23
                    cp        $ff                           ;[39dc] fe ff
                    jr        z,$39ee                       ;[39de] 28 0e
                    sub       $30                           ;[39e0] d6 30
                    ccf                                     ;[39e2] 3f
                    ret       nc                            ;[39e3] d0
                    ld        d,$0a                         ;[39e4] 16 0a
                    cp        d                             ;[39e6] ba
                    ret       nc                            ;[39e7] d0
                    mul       d,e                           ;[39e8] ed 30
                    add       de,a                          ;[39ea] ed 32
                    jr        $39da                         ;[39ec] 18 ec
                    ld        a,e                           ;[39ee] 7b
                    rst       $20                           ;[39ef] e7
                    jr        nc,$39f3                      ;[39f0] 30 01
                    ret       nc                            ;[39f2] d0
                    jr        $39a3                         ;[39f3] 18 ae
                    ld        a,($d5eb)                     ;[39f5] 3a eb d5
                    ld        c,a                           ;[39f8] 4f
                    dec       a                             ;[39f9] 3d
                    xor       c                             ;[39fa] a9
                    and       $03                           ;[39fb] e6 03
                    ld        c,a                           ;[39fd] 4f
                    jr        $3a02                         ;[39fe] 18 02
                    ld        c,$04                         ;[3a00] 0e 04
                    ld        b,$00                         ;[3a02] 06 00
                    push      bc                            ;[3a04] c5
                    call      $35ab                         ;[3a05] cd ab 35
                    pop       bc                            ;[3a08] c1
                    ret       nc                            ;[3a09] d0
                    jr        $3a10                         ;[3a0a] 18 04
                    ld        b,$20                         ;[3a0c] 06 20
                    ld        c,$00                         ;[3a0e] 0e 00
                    call      $3390                         ;[3a10] cd 90 33
                    jr        $39a3                         ;[3a13] 18 8e
                    ld        b,$04                         ;[3a15] 06 04
                    jr        $3a0e                         ;[3a17] 18 f5
                    ld        b,$10                         ;[3a19] 06 10
                    jr        $3a0e                         ;[3a1b] 18 f1
                    ld        b,$40                         ;[3a1d] 06 40
                    jr        $3a0e                         ;[3a1f] 18 ed
                    ld        a,($d5ea)                     ;[3a21] 3a ea d5
                    ld        b,a                           ;[3a24] 47
                    inc       a                             ;[3a25] 3c
                    xor       b                             ;[3a26] a8
                    and       $03                           ;[3a27] e6 03
                    ld        b,a                           ;[3a29] 47
                    ld        c,$00                         ;[3a2a] 0e 00
                    call      $3390                         ;[3a2c] cd 90 33
                    call      $3532                         ;[3a2f] cd 32 35
                    ld        a,($d5e5)                     ;[3a32] 3a e5 d5
                    ld        d,a                           ;[3a35] 57
                    jp        $3779                         ;[3a36] c3 79 37
                    call      $38d7                         ;[3a39] cd d7 38
                    ret       nc                            ;[3a3c] d0
                    ld        b,$02                         ;[3a3d] 06 02
                    ld        c,$14                         ;[3a3f] 0e 14
                    call      $0d04                         ;[3a41] cd 04 0d
                    ld        de,$000a                      ;[3a44] 11 0a 00
                    call      $2cb5                         ;[3a47] cd b5 2c
                    ld        c,$ff                         ;[3a4a] 0e ff
                    call      $3b8c                         ;[3a4c] cd 8c 3b
                    ld        hl,$0fba                      ;[3a4f] 21 ba 0f
                    call      $3333                         ;[3a52] cd 33 33
                    set       5,(iy+$30)                    ;[3a55] fd cb 30 ee
                    call      $0ce2                         ;[3a59] cd e2 0c
                    jr        $3a32                         ;[3a5c] 18 d4
                    ld        a,$02                         ;[3a5e] 3e 02
                    ld        hl,$2a95                      ;[3a60] 21 95 2a
                    jr        $3a6a                         ;[3a63] 18 05
                    ld        a,$01                         ;[3a65] 3e 01
                    ld        hl,$2a90                      ;[3a67] 21 90 2a
                    ld        ($d5dc),a                     ;[3a6a] 32 dc d5
                    ld        e,$01                         ;[3a6d] 1e 01
                    call      $3b6f                         ;[3a6f] cd 6f 3b
                    ld        hl,$d5dc                      ;[3a72] 21 dc d5
                    jr        z,$3a7b                       ;[3a75] 28 04
                    ld        a,(hl)                        ;[3a77] 7e
                    and       $01                           ;[3a78] e6 01
                    ret       nz                            ;[3a7a] c0
                    set       7,(hl)                        ;[3a7b] cb fe
                    call      $38d7                         ;[3a7d] cd d7 38
                    ld        de,$e090                      ;[3a80] 11 90 e0
                    push      de                            ;[3a83] d5
                    call      $3bea                         ;[3a84] cd ea 3b
                    pop       de                            ;[3a87] d1
                    ld        a,(de)                        ;[3a88] 1a
                    inc       de                            ;[3a89] 13
                    inc       a                             ;[3a8a] 3c
                    jr        nz,$3a88                      ;[3a8b] 20 fb
                    dec       de                            ;[3a8d] 1b
                    ld        h,d                           ;[3a8e] 62
                    ld        l,e                           ;[3a8f] 6b
                    ld        bc,$e090                      ;[3a90] 01 90 e0
                    and       a                             ;[3a93] a7
                    sbc       hl,bc                         ;[3a94] ed 42
                    ld        ($d5d2),hl                    ;[3a96] 22 d2 d5
                    ld        hl,$da35                      ;[3a99] 21 35 da
                    call      $3bed                         ;[3a9c] cd ed 3b
                    set       5,(iy+$30)                    ;[3a9f] fd cb 30 ee
                    scf                                     ;[3aa3] 37
                    ret                                     ;[3aa4] c9

                    ld        hl,$d5dc                      ;[3aa5] 21 dc d5
                    ld        a,(hl)                        ;[3aa8] 7e
                    add       a                             ;[3aa9] 87
                    res       7,(hl)                        ;[3aaa] cb be
                    ret       nc                            ;[3aac] d0
                    bit       0,(hl)                        ;[3aad] cb 46
                    ld        hl,$2a85                      ;[3aaf] 21 85 2a
                    jr        nz,$3ab7                      ;[3ab2] 20 03
                    ld        hl,$2a9a                      ;[3ab4] 21 9a 2a
                    call      $3b76                         ;[3ab7] cd 76 3b
                    ld        hl,$e090                      ;[3aba] 21 90 e0
                    ld        bc,($d5d2)                    ;[3abd] ed 4b d2 d5
                    add       hl,bc                         ;[3ac1] 09
                    ld        d,$5a                         ;[3ac2] 16 5a
                    call      $3919                         ;[3ac4] cd 19 39
                    ld        hl,$0efb                      ;[3ac7] 21 fb 0e
                    call      $394e                         ;[3aca] cd 4e 39
                    jr        nc,$3b00                      ;[3acd] 30 31
                    ld        de,$da35                      ;[3acf] 11 35 da
                    push      de                            ;[3ad2] d5
                    push      hl                            ;[3ad3] e5
                    call      $3bea                         ;[3ad4] cd ea 3b
                    pop       hl                            ;[3ad7] e1
                    call      $3bed                         ;[3ad8] cd ed 3b
                    pop       hl                            ;[3adb] e1
                    ld        de,$d82b                      ;[3adc] 11 2b d8
                    push      de                            ;[3adf] d5
                    call      $3bed                         ;[3ae0] cd ed 3b
                    pop       de                            ;[3ae3] d1
                    ld        a,($d5dc)                     ;[3ae4] 3a dc d5
                    rra                                     ;[3ae7] 1f
                    jr        c,$3af2                       ;[3ae8] 38 08
                    ld        hl,$e090                      ;[3aea] 21 90 e0
                    rst       $20                           ;[3aed] e7
                    daa                                     ;[3aee] 27
                    ld        bc,$0e18                      ;[3aef] 01 18 0e
                    ld        a,$80                         ;[3af2] 3e 80
                    ld        ($d801),a                     ;[3af4] 32 01 d8
                    ld        hl,$0f06                      ;[3af7] 21 06 0f
                    call      $3337                         ;[3afa] cd 37 33
                    rst       $18                           ;[3afd] df
                    or        c                             ;[3afe] b1
                    ld        a,$f5                         ;[3aff] 3e f5
                    call      $3bbd                         ;[3b01] cd bd 3b
                    call      $3401                         ;[3b04] cd 01 34
                    pop       af                            ;[3b07] f1
                    jp        $39a0                         ;[3b08] c3 a0 39
                    cp        $0a                           ;[3b0b] fe 0a
                    jr        nc,$3b13                      ;[3b0d] 30 04
                    add       $3e                           ;[3b0f] c6 3e
                    jr        $3b15                         ;[3b11] 18 02
                    add       $19                           ;[3b13] c6 19
                    push      af                            ;[3b15] f5
                    call      $3401                         ;[3b16] cd 01 34
                    ld        a,$12                         ;[3b19] 3e 12
                    rst       $10                           ;[3b1b] d7
                    ld        a,$01                         ;[3b1c] 3e 01
                    rst       $10                           ;[3b1e] d7
                    pop       af                            ;[3b1f] f1
                    rst       $18                           ;[3b20] df
                    inc       (hl)                          ;[3b21] 34
                    dec       c                             ;[3b22] 0d
                    ld        a,$12                         ;[3b23] 3e 12
                    rst       $10                           ;[3b25] d7
                    ld        a,$00                         ;[3b26] 3e 00
                    rst       $10                           ;[3b28] d7
                    call      $3e18                         ;[3b29] cd 18 3e
                    call      $0ce2                         ;[3b2c] cd e2 0c
                    ret                                     ;[3b2f] c9

                    ld        a,($d5d4)                     ;[3b30] 3a d4 d5
                    and       $10                           ;[3b33] e6 10
                    ret       z                             ;[3b35] c8
                    call      $3401                         ;[3b36] cd 01 34
                    set       5,(iy+$30)                    ;[3b39] fd cb 30 ee
                    ld        c,$03                         ;[3b3d] 0e 03
                    rst       $18                           ;[3b3f] df
                    xor       b                             ;[3b40] a8
                    dec       hl                            ;[3b41] 2b
                    scf                                     ;[3b42] 37
                    jr        $3b00                         ;[3b43] 18 bb
                    push      hl                            ;[3b45] e5
                    ld        bc,$ffff                      ;[3b46] 01 ff ff
                    ld        a,(hl)                        ;[3b49] 7e
                    inc       hl                            ;[3b4a] 23
                    inc       bc                            ;[3b4b] 03
                    inc       a                             ;[3b4c] 3c
                    jr        nz,$3b49                      ;[3b4d] 20 fa
                    ld        a,b                           ;[3b4f] 78
                    and       a                             ;[3b50] a7
                    jr        nz,$3b57                      ;[3b51] 20 04
                    ld        a,c                           ;[3b53] 79
                    cp        d                             ;[3b54] ba
                    jr        c,$3b5a                       ;[3b55] 38 03
                    ld        b,$00                         ;[3b57] 06 00
                    ld        c,d                           ;[3b59] 4a
                    push      bc                            ;[3b5a] c5
                    call      $1942                         ;[3b5b] cd 42 19
                    ld        ($5b8a),hl                    ;[3b5e] 22 8a 5b
                    ld        de,$e152                      ;[3b61] 11 52 e1
                    pop       bc                            ;[3b64] c1
                    pop       hl                            ;[3b65] e1
                    push      bc                            ;[3b66] c5
                    call      $1947                         ;[3b67] cd 47 19
                    call      $27d7                         ;[3b6a] cd d7 27
                    pop       de                            ;[3b6d] d1
                    ret                                     ;[3b6e] c9

                    ld        a,($d5d4)                     ;[3b6f] 3a d4 d5
                    and       e                             ;[3b72] a3
                    pop       de                            ;[3b73] d1
                    ret       z                             ;[3b74] c8
                    push      de                            ;[3b75] d5
                    push      hl                            ;[3b76] e5
                    call      $3401                         ;[3b77] cd 01 34
                    pop       hl                            ;[3b7a] e1
                    rst       $18                           ;[3b7b] df
                    ld        (hl),e                        ;[3b7c] 73
                    dec       hl                            ;[3b7d] 2b
                    set       5,(iy+$30)                    ;[3b7e] fd cb 30 ee
                    pop       de                            ;[3b82] d1
                    ret       nz                            ;[3b83] c0
                    push      de                            ;[3b84] d5
                    call      $3426                         ;[3b85] cd 26 34
                    ld        hl,$d82b                      ;[3b88] 21 2b d8
                    ret                                     ;[3b8b] c9

                    nextreg $57,$10                         ;[3b8c] ed 91 57 10
                    ld        b,c                           ;[3b90] 41
                    inc       b                             ;[3b91] 04
                    ld        hl,$da35                      ;[3b92] 21 35 da
                    ld        de,$e152                      ;[3b95] 11 52 e1
                    push      de                            ;[3b98] d5
                    push      bc                            ;[3b99] c5
                    ld        a,(hl)                        ;[3b9a] 7e
                    inc       hl                            ;[3b9b] 23
                    ld        (de),a                        ;[3b9c] 12
                    inc       de                            ;[3b9d] 13
                    inc       a                             ;[3b9e] 3c
                    jr        z,$3bb4                       ;[3b9f] 28 13
                    djnz      $3b9a                         ;[3ba1] 10 f7
                    ld        a,(hl)                        ;[3ba3] 7e
                    inc       hl                            ;[3ba4] 23
                    inc       a                             ;[3ba5] 3c
                    jr        nz,$3ba3                      ;[3ba6] 20 fb
                    dec       hl                            ;[3ba8] 2b
                    dec       hl                            ;[3ba9] 2b
                    dec       de                            ;[3baa] 1b
                    dec       de                            ;[3bab] 1b
                    ld        bc,$0010                      ;[3bac] 01 10 00
                    lddr                                    ;[3baf] ed b8
                    ld        a,$7e                         ;[3bb1] 3e 7e
                    ld        (de),a                        ;[3bb3] 12
                    nextreg $57,$0f                         ;[3bb4] ed 91 57 0f
                    pop       de                            ;[3bb8] d1
                    pop       hl                            ;[3bb9] e1
                    jp        $2751                         ;[3bba] c3 51 27
                    ld        a,($d5d4)                     ;[3bbd] 3a d4 d5
                    and       $80                           ;[3bc0] e6 80
                    ret       z                             ;[3bc2] c8
                    ld        hl,$f701                      ;[3bc3] 21 01 f7
                    push      hl                            ;[3bc6] e5
                    ld        bc,$07fe                      ;[3bc7] 01 fe 07
                    ld        de,$268c                      ;[3bca] 11 8c 26
                    call      $0068                         ;[3bcd] cd 68 00
                    ld        e,e                           ;[3bd0] 5b
                    ld        ($54e1),hl                    ;[3bd1] 22 e1 54
                    ld        e,l                           ;[3bd4] 5d
                    dec       hl                            ;[3bd5] 2b
                    ld        b,$00                         ;[3bd6] 06 00
                    ld        a,(de)                        ;[3bd8] 1a
                    inc       de                            ;[3bd9] 13
                    call      $2eb9                         ;[3bda] cd b9 2e
                    jr        z,$3be2                       ;[3bdd] 28 03
                    inc       b                             ;[3bdf] 04
                    jr        nz,$3bd8                      ;[3be0] 20 f6
                    ld        (hl),b                        ;[3be2] 70
                    ex        de,hl                         ;[3be3] eb
                    inc       a                             ;[3be4] 3c
                    jr        nz,$3bd3                      ;[3be5] 20 ec
                    dec       hl                            ;[3be7] 2b
                    ld        (hl),a                        ;[3be8] 77
                    ret                                     ;[3be9] c9

                    ld        hl,$d930                      ;[3bea] 21 30 d9
                    ld        a,(hl)                        ;[3bed] 7e
                    ld        (de),a                        ;[3bee] 12
                    inc       a                             ;[3bef] 3c
                    ret       z                             ;[3bf0] c8
                    inc       hl                            ;[3bf1] 23
                    inc       de                            ;[3bf2] 13
                    jr        $3bed                         ;[3bf3] 18 f8
                    ld        ($5c74),bc                    ;[3bf5] ed 43 74 5c
                    ld        ($5c3f),de                    ;[3bf9] ed 53 3f 5c
                    ld        ($5b6e),ix                    ;[3bfd] dd 22 6e 5b
                    ld        ($5b70),hl                    ;[3c01] 22 70 5b
                    ret                                     ;[3c04] c9

                    ld        hl,($5b6e)                    ;[3c05] 2a 6e 5b
                    ld        de,($5b70)                    ;[3c08] ed 5b 70 5b
                    ld        ix,$3c6f                      ;[3c0c] dd 21 6f 3c
                    ld        a,$20                         ;[3c10] 3e 20
                    ld        b,$09                         ;[3c12] 06 09
                    push      bc                            ;[3c14] c5
                    bit       7,(ix+$03)                    ;[3c15] dd cb 03 7e
                    jr        z,$3c26                       ;[3c19] 28 0b
                    cp        $20                           ;[3c1b] fe 20
                    ld        c,a                           ;[3c1d] 4f
                    jr        z,$3c22                       ;[3c1e] 28 02
                    ld        c,$2c                         ;[3c20] 0e 2c
                    push      af                            ;[3c22] f5
                    ld        a,c                           ;[3c23] 79
                    rst       $10                           ;[3c24] d7
                    pop       af                            ;[3c25] f1
                    ld        c,(ix+$00)                    ;[3c26] dd 4e 00
                    ld        b,(ix+$01)                    ;[3c29] dd 46 01
                    and       a                             ;[3c2c] a7
                    sbc       hl,bc                         ;[3c2d] ed 42
                    ex        de,hl                         ;[3c2f] eb
                    ld        c,(ix+$02)                    ;[3c30] dd 4e 02
                    ld        b,(ix+$03)                    ;[3c33] dd 46 03
                    res       7,b                           ;[3c36] cb b8
                    sbc       hl,bc                         ;[3c38] ed 42
                    ex        de,hl                         ;[3c3a] eb
                    jr        c,$3c46                       ;[3c3b] 38 09
                    inc       a                             ;[3c3d] 3c
                    cp        $21                           ;[3c3e] fe 21
                    jr        nz,$3c26                      ;[3c40] 20 e4
                    ld        a,$31                         ;[3c42] 3e 31
                    jr        $3c26                         ;[3c44] 18 e0
                    ld        c,(ix+$00)                    ;[3c46] dd 4e 00
                    ld        b,(ix+$01)                    ;[3c49] dd 46 01
                    add       hl,bc                         ;[3c4c] 09
                    ex        de,hl                         ;[3c4d] eb
                    ld        c,(ix+$02)                    ;[3c4e] dd 4e 02
                    ld        b,(ix+$03)                    ;[3c51] dd 46 03
                    res       7,b                           ;[3c54] cb b8
                    adc       hl,bc                         ;[3c56] ed 4a
                    ex        de,hl                         ;[3c58] eb
                    push      af                            ;[3c59] f5
                    rst       $10                           ;[3c5a] d7
                    pop       af                            ;[3c5b] f1
                    cp        $20                           ;[3c5c] fe 20
                    jr        z,$3c62                       ;[3c5e] 28 02
                    ld        a,$30                         ;[3c60] 3e 30
                    ld        bc,$0004                      ;[3c62] 01 04 00
                    add       ix,bc                         ;[3c65] dd 09
                    pop       bc                            ;[3c67] c1
                    djnz      $3c14                         ;[3c68] 10 aa
                    ld        a,l                           ;[3c6a] 7d
                    add       $30                           ;[3c6b] c6 30
                    rst       $10                           ;[3c6d] d7
                    ret                                     ;[3c6e] c9

                    nop                                     ;[3c6f] 00
                    jp        z,$3b9a                       ;[3c70] ca 9a 3b
                    nop                                     ;[3c73] 00
                    pop       hl                            ;[3c74] e1
                    push      af                            ;[3c75] f5
                    add       l                             ;[3c76] 85
                    add       b                             ;[3c77] 80
                    sub       (hl)                          ;[3c78] 96
                    sbc       b                             ;[3c79] 98
                    nop                                     ;[3c7a] 00
                    ld        b,b                           ;[3c7b] 40
                    ld        b,d                           ;[3c7c] 42
                    rrca                                    ;[3c7d] 0f
                    nop                                     ;[3c7e] 00
                    and       b                             ;[3c7f] a0
                    add       (hl)                          ;[3c80] 86
                    ld        bc,$1080                      ;[3c81] 01 80 10
                    daa                                     ;[3c84] 27
                    nop                                     ;[3c85] 00
                    nop                                     ;[3c86] 00
                    ret       pe                            ;[3c87] e8
                    inc       bc                            ;[3c88] 03
                    nop                                     ;[3c89] 00
                    nop                                     ;[3c8a] 00
                    ld        h,h                           ;[3c8b] 64
                    nop                                     ;[3c8c] 00
                    nop                                     ;[3c8d] 00
                    add       b                             ;[3c8e] 80
                    ld        a,(bc)                        ;[3c8f] 0a
                    nop                                     ;[3c90] 00
                    nop                                     ;[3c91] 00
                    nop                                     ;[3c92] 00
                    xor       a                             ;[3c93] af
                    bit       7,(hl)                        ;[3c94] cb 7e
                    inc       hl                            ;[3c96] 23
                    jr        z,$3c9a                       ;[3c97] 28 01
                    inc       a                             ;[3c99] 3c
                    bit       7,(hl)                        ;[3c9a] cb 7e
                    inc       hl                            ;[3c9c] 23
                    jr        z,$3ca1                       ;[3c9d] 28 02
                    set       3,a                           ;[3c9f] cb df
                    bit       7,(hl)                        ;[3ca1] cb 7e
                    inc       hl                            ;[3ca3] 23
                    jr        z,$3ca8                       ;[3ca4] 28 02
                    set       2,a                           ;[3ca6] cb d7
                    bit       7,(hl)                        ;[3ca8] cb 7e
                    inc       hl                            ;[3caa] 23
                    jr        z,$3caf                       ;[3cab] 28 02
                    set       1,a                           ;[3cad] cb cf
                    ld        b,a                           ;[3caf] 47
                    ld        c,a                           ;[3cb0] 4f
                    ld        de,$5b9d                      ;[3cb1] 11 9d 5b
                    push      de                            ;[3cb4] d5
                    ld        a,$20                         ;[3cb5] 3e 20
                    ld        (de),a                        ;[3cb7] 12
                    inc       de                            ;[3cb8] 13
                    ld        (de),a                        ;[3cb9] 12
                    inc       de                            ;[3cba] 13
                    ld        a,$64                         ;[3cbb] 3e 64
                    call      $3cd6                         ;[3cbd] cd d6 3c
                    ld        a,$61                         ;[3cc0] 3e 61
                    call      $3cd6                         ;[3cc2] cd d6 3c
                    ld        a,$73                         ;[3cc5] 3e 73
                    call      $3cd6                         ;[3cc7] cd d6 3c
                    ld        a,$70                         ;[3cca] 3e 70
                    call      $3cd6                         ;[3ccc] cd d6 3c
                    ld        a,$20                         ;[3ccf] 3e 20
                    ld        (de),a                        ;[3cd1] 12
                    inc       de                            ;[3cd2] 13
                    ld        (de),a                        ;[3cd3] 12
                    pop       de                            ;[3cd4] d1
                    ret                                     ;[3cd5] c9

                    srl       c                             ;[3cd6] cb 39
                    jr        c,$3cdc                       ;[3cd8] 38 02
                    ld        a,$2d                         ;[3cda] 3e 2d
                    ld        (de),a                        ;[3cdc] 12
                    inc       de                            ;[3cdd] 13
                    ret                                     ;[3cde] c9

                    dec       c                             ;[3cdf] 0d
                    out       ($30),a                       ;[3ce0] d3 30
                    jr        nc,$3cb7                      ;[3ce2] 30 d3
                    jr        nc,$3d4b                      ;[3ce4] 30 65
                    sub       c                             ;[3ce6] 91
                    add       hl,sp                         ;[3ce7] 39
                    ld        h,e                           ;[3ce8] 63
                    ld        h,l                           ;[3ce9] 65
                    ld        a,($0072)                     ;[3cea] 3a 72 00
                    add       hl,sp                         ;[3ced] 39
                    halt                                    ;[3cee] 76
                    ld        e,(hl)                        ;[3cef] 5e
                    ld        a,($c20e)                     ;[3cf0] 3a 0e c2
                    ld        l,$0a                         ;[3cf3] 2e 0a
                    ld        b,a                           ;[3cf5] 47
                    scf                                     ;[3cf6] 37
                    ld        (hl),$47                      ;[3cf7] 36 47
                    scf                                     ;[3cf9] 37
                    add       hl,bc                         ;[3cfa] 09
                    ld        c,e                           ;[3cfb] 4b
                    scf                                     ;[3cfc] 37
                    jr        c,$3d4a                       ;[3cfd] 38 4b
                    scf                                     ;[3cff] 37
                    ld        l,b                           ;[3d00] 68
                    cp        b                             ;[3d01] b8
                    dec       (hl)                          ;[3d02] 35
                    ld        l,(hl)                        ;[3d03] 6e
                    add       hl,sp                         ;[3d04] 39
                    ld        a,($f50b)                     ;[3d05] 3a 0b f5
                    ld        (hl),$37                      ;[3d08] 36 37
                    push      af                            ;[3d0a] f5
                    ld        (hl),$08                      ;[3d0b] 36 08
                    ld        sp,hl                         ;[3d0d] f9
                    ld        (hl),$35                      ;[3d0e] 36 35
                    ld        sp,hl                         ;[3d10] f9
                    ld        (hl),$20                      ;[3d11] 36 20
                    inc       b                             ;[3d13] 04
                    ld        ($7564),a                     ;[3d14] 32 64 75
                    inc       (hl)                          ;[3d17] 34
                    ld        l,l                           ;[3d18] 6d
                    jr        nc,$3d56                      ;[3d19] 30 3b
                    ld        l,e                           ;[3d1b] 6b
                    ld        ($7539),a                     ;[3d1c] 32 39 75
                    ld        e,a                           ;[3d1f] 5f
                    inc       (hl)                          ;[3d20] 34
                    ld        h,(hl)                        ;[3d21] 66
                    or        h                             ;[3d22] b4
                    add       hl,sp                         ;[3d23] 39
                    ld        (hl),b                        ;[3d24] 70
                    and       l                             ;[3d25] a5
                    ld        a,($8207)                     ;[3d26] 3a 07 82
                    ld        ($706c),a                     ;[3d29] 32 6c 70
                    ld        ($0c2e),a                     ;[3d2c] 32 2e 0c
                    ld        a,($1973)                     ;[3d2f] 3a 73 19
                    ld        a,($2169)                     ;[3d32] 3a 69 21
                    ld        a,($1d78)                     ;[3d35] 3a 78 1d
                    ld        a,($f56f)                     ;[3d38] 3a 6f f5
                    add       hl,sp                         ;[3d3b] 39
                    dec       l                             ;[3d3c] 2d
                    nop                                     ;[3d3d] 00
                    ld        a,($002b)                     ;[3d3e] 3a 2b 00
                    ld        a,($ca61)                     ;[3d41] 3a 61 ca
                    add       hl,sp                         ;[3d44] 39
                    ld        h,a                           ;[3d45] 67
                    ld        c,d                           ;[3d46] 4a
                    jr        nc,$3dc4                      ;[3d47] 30 7b
                    dec       d                             ;[3d49] 15
                    ld        a,($20ff)                     ;[3d4a] 3a ff 20
                    pop       bc                            ;[3d4d] c1
                    cpl                                     ;[3d4e] 2f
                    ld        c,$cf                         ;[3d4f] 0e cf
                    cpl                                     ;[3d51] 2f
                    ld        a,$ff                         ;[3d52] 3e ff
                    call      $075b                         ;[3d54] cd 5b 07
                    ld        a,($d5bb)                     ;[3d57] 3a bb d5
                    add       $6a                           ;[3d5a] c6 6a
                    ld        l,a                           ;[3d5c] 6f
                    ld        a,$00                         ;[3d5d] 3e 00
                    ld        b,a                           ;[3d5f] 47
                    adc       $3d                           ;[3d60] ce 3d
                    ld        h,a                           ;[3d62] 67
                    ld        c,(hl)                        ;[3d63] 4e
                    add       hl,bc                         ;[3d64] 09
                    call      $07d7                         ;[3d65] cd d7 07
                    scf                                     ;[3d68] 37
                    ret                                     ;[3d69] c9

                    inc       b                             ;[3d6a] 04
                    dec       c                             ;[3d6b] 0d
                    ld        d,$1f                         ;[3d6c] 16 1f
                    jr        nz,$3da3                      ;[3d6e] 20 33
                    ld        l,$35                         ;[3d70] 2e 35
                    ld        c,l                           ;[3d72] 4d
                    ld        c,b                           ;[3d73] 48
                    ld        a,d                           ;[3d74] 7a
                    jr        nz,$3db5                      ;[3d75] 20 3e
                    rst       $38                           ;[3d77] ff
                    inc       a                             ;[3d78] 3c
                    jr        nz,$3d9b                      ;[3d79] 20 20
                    scf                                     ;[3d7b] 37
                    ld        c,l                           ;[3d7c] 4d
                    ld        c,b                           ;[3d7d] 48
                    ld        a,d                           ;[3d7e] 7a
                    jr        nz,$3dbf                      ;[3d7f] 20 3e
                    rst       $38                           ;[3d81] ff
                    inc       a                             ;[3d82] 3c
                    jr        nz,$3db6                      ;[3d83] 20 31
                    inc       (hl)                          ;[3d85] 34
                    ld        c,l                           ;[3d86] 4d
                    ld        c,b                           ;[3d87] 48
                    ld        a,d                           ;[3d88] 7a
                    jr        nz,$3dc9                      ;[3d89] 20 3e
                    rst       $38                           ;[3d8b] ff
                    inc       a                             ;[3d8c] 3c
                    jr        nz,$3dc1                      ;[3d8d] 20 32
                    jr        c,$3dde                       ;[3d8f] 38 4d
                    ld        c,b                           ;[3d91] 48
                    ld        a,d                           ;[3d92] 7a
                    jr        nz,$3db5                      ;[3d93] 20 20
                    rst       $38                           ;[3d95] ff
                    halt                                    ;[3d96] 76
                    ld        a,$07                         ;[3d97] 3e 07
                    call      $0d6b                         ;[3d99] cd 6b 0d
                    and       $03                           ;[3d9c] e6 03
                    ret                                     ;[3d9e] c9

                    ld        a,($d5bb)                     ;[3d9f] 3a bb d5
                    inc       a                             ;[3da2] 3c
                    cp        $04                           ;[3da3] fe 04
                    ret       nc                            ;[3da5] d0
                    ld        ($d5bb),a                     ;[3da6] 32 bb d5
                    jr        $3d52                         ;[3da9] 18 a7
                    ld        a,($d5bb)                     ;[3dab] 3a bb d5
                    dec       a                             ;[3dae] 3d
                    ret       m                             ;[3daf] f8
                    jr        $3da6                         ;[3db0] 18 f4
                    call      $3d96                         ;[3db2] cd 96 3d
                    push      af                            ;[3db5] f5
                    xor       a                             ;[3db6] af
                    out       (c),a                         ;[3db7] ed 79
                    rst       $28                           ;[3db9] ef
                    or        l                             ;[3dba] b5
                    inc       bc                            ;[3dbb] 03
                    pop       af                            ;[3dbc] f1
                    nextreg $07,a                           ;[3dbd] ed 92 07
                    ret                                     ;[3dc0] c9

                    ld        ($5b54),bc                    ;[3dc1] ed 43 54 5b
                    ex        (sp),hl                       ;[3dc5] e3
                    ld        c,(hl)                        ;[3dc6] 4e
                    inc       hl                            ;[3dc7] 23
                    ld        b,(hl)                        ;[3dc8] 46
                    inc       hl                            ;[3dc9] 23
                    ex        (sp),hl                       ;[3dca] e3
                    push    $3e93                           ;[3dcb] ed 8a 3e 93
                    push    $007b                           ;[3dcf] ed 8a 00 7b
                    push      bc                            ;[3dd3] c5
                    push    $007b                           ;[3dd4] ed 8a 00 7b
                    jp        $3e8f                         ;[3dd8] c3 8f 3e
                    nop                                     ;[3ddb] 00
                    nop                                     ;[3ddc] 00
                    nop                                     ;[3ddd] 00
                    nop                                     ;[3dde] 00
                    nop                                     ;[3ddf] 00
                    nop                                     ;[3de0] 00
                    nop                                     ;[3de1] 00
                    nop                                     ;[3de2] 00
                    nop                                     ;[3de3] 00
                    nop                                     ;[3de4] 00
                    nop                                     ;[3de5] 00
                    nop                                     ;[3de6] 00
                    nop                                     ;[3de7] 00
                    nop                                     ;[3de8] 00
                    nop                                     ;[3de9] 00
                    nop                                     ;[3dea] 00
                    nop                                     ;[3deb] 00
                    nop                                     ;[3dec] 00
                    nop                                     ;[3ded] 00
                    nop                                     ;[3dee] 00
                    nop                                     ;[3def] 00
                    nop                                     ;[3df0] 00
                    nop                                     ;[3df1] 00
                    nop                                     ;[3df2] 00
                    nop                                     ;[3df3] 00
                    nop                                     ;[3df4] 00
                    nop                                     ;[3df5] 00
                    nop                                     ;[3df6] 00
                    nop                                     ;[3df7] 00
                    nop                                     ;[3df8] 00
                    nop                                     ;[3df9] 00
                    nop                                     ;[3dfa] 00
                    nop                                     ;[3dfb] 00
                    nop                                     ;[3dfc] 00
                    nop                                     ;[3dfd] 00
                    nop                                     ;[3dfe] 00
                    nop                                     ;[3dff] 00
                    ld        ($5b54),bc                    ;[3e00] ed 43 54 5b
                    ex        (sp),hl                       ;[3e04] e3
                    ld        c,(hl)                        ;[3e05] 4e
                    inc       hl                            ;[3e06] 23
                    ld        b,(hl)                        ;[3e07] 46
                    inc       hl                            ;[3e08] 23
                    ex        (sp),hl                       ;[3e09] e3
                    push    $3e13                           ;[3e0a] ed 8a 3e 13
                    push      bc                            ;[3e0e] c5
                    ld        bc,($5b54)                    ;[3e0f] ed 4b 54 5b
                    nextreg $8e,$02                         ;[3e13] ed 91 8e 02
                    ret                                     ;[3e17] c9

                    ld        a,($5c38)                     ;[3e18] 3a 38 5c
                    srl       a                             ;[3e1b] cb 3f
                    ld        hl,$0c80                      ;[3e1d] 21 80 0c
                    push      ix                            ;[3e20] dd e5
                    ld        d,$00                         ;[3e22] 16 00
                    ld        e,a                           ;[3e24] 5f
                    call      $3db2                         ;[3e25] cd b2 3d
                    pop       ix                            ;[3e28] dd e1
                    ret                                     ;[3e2a] c9

                    push      ix                            ;[3e2b] dd e5
                    ld        de,$0030                      ;[3e2d] 11 30 00
                    ld        hl,$0300                      ;[3e30] 21 00 03
                    jr        $3e25                         ;[3e33] 18 f0
                    push      af                            ;[3e35] f5
                    push      hl                            ;[3e36] e5
                    ld        a,($5c8d)                     ;[3e37] 3a 8d 5c
                    push      af                            ;[3e3a] f5
                    ld        a,($5c48)                     ;[3e3b] 3a 48 5c
                    ld        ($5c8d),a                     ;[3e3e] 32 8d 5c
                    call      $0954                         ;[3e41] cd 54 09
                    ld        a,$fd                         ;[3e44] 3e fd
                    rst       $28                           ;[3e46] ef
                    ld        bc,$f116                      ;[3e47] 01 16 f1
                    ld        ($5c8d),a                     ;[3e4a] 32 8d 5c
                    pop       hl                            ;[3e4d] e1
                    ld        a,(hl)                        ;[3e4e] 7e
                    inc       hl                            ;[3e4f] 23
                    cp        $ff                           ;[3e50] fe ff
                    jr        z,$3e57                       ;[3e52] 28 03
                    rst       $10                           ;[3e54] d7
                    jr        $3e4e                         ;[3e55] 18 f7
                    pop       af                            ;[3e57] f1
                    jr        z,$3e69                       ;[3e58] 28 0f
                    call      $0c6d                         ;[3e5a] cd 6d 0c
                    push      de                            ;[3e5d] d5
                    call      $0954                         ;[3e5e] cd 54 09
                    ld        a,$fe                         ;[3e61] 3e fe
                    rst       $28                           ;[3e63] ef
                    ld        bc,$d116                      ;[3e64] 01 16 d1
                    ld        a,e                           ;[3e67] 7b
                    ret                                     ;[3e68] c9

                    call      $0c6d                         ;[3e69] cd 6d 0c
                    and       $df                           ;[3e6c] e6 df
                    ld        e,$00                         ;[3e6e] 1e 00
                    cp        $43                           ;[3e70] fe 43
                    jr        z,$3e5d                       ;[3e72] 28 e9
                    inc       e                             ;[3e74] 1c
                    cp        $52                           ;[3e75] fe 52
                    jr        z,$3e5d                       ;[3e77] 28 e4
                    inc       e                             ;[3e79] 1c
                    cp        $49                           ;[3e7a] fe 49
                    jr        z,$3e5d                       ;[3e7c] 28 df
                    jr        $3e69                         ;[3e7e] 18 e9
                    ld        ($5b54),bc                    ;[3e80] ed 43 54 5b
                    ex        (sp),hl                       ;[3e84] e3
                    ld        c,(hl)                        ;[3e85] 4e
                    inc       hl                            ;[3e86] 23
                    ld        b,(hl)                        ;[3e87] 46
                    inc       hl                            ;[3e88] 23
                    ex        (sp),hl                       ;[3e89] e3
                    push    $3e93                           ;[3e8a] ed 8a 3e 93
                    push      bc                            ;[3e8e] c5
                    ld        bc,($5b54)                    ;[3e8f] ed 4b 54 5b
                    nextreg $8e,$01                         ;[3e93] ed 91 8e 01
                    ret                                     ;[3e97] c9

                    ld        hl,($5c5d)                    ;[3e98] 2a 5d 5c
                    push      hl                            ;[3e9b] e5
                    ld        hl,($5c51)                    ;[3e9c] 2a 51 5c
                    add       hl,$000f                      ;[3e9f] ed 34 0f 00
                    ld        ($5c5d),hl                    ;[3ea3] 22 5d 5c
                    ex        (sp),hl                       ;[3ea6] e3
                    push      hl                            ;[3ea7] e5
                    exx                                     ;[3ea8] d9
                    push      bc                            ;[3ea9] c5
                    push      de                            ;[3eaa] d5
                    push      hl                            ;[3eab] e5
                    exx                                     ;[3eac] d9
                    rst       $28                           ;[3ead] ef
                    jr        $3eb0                         ;[3eae] 18 00
                    rst       $28                           ;[3eb0] ef
                    cp        e                             ;[3eb1] bb
                    jr        z,$3e8d                       ;[3eb2] 28 d9
                    pop       hl                            ;[3eb4] e1
                    pop       de                            ;[3eb5] d1
                    pop       bc                            ;[3eb6] c1
                    exx                                     ;[3eb7] d9
                    pop       de                            ;[3eb8] d1
                    ld        ($5c5d),de                    ;[3eb9] ed 53 5d 5c
                    jp        c,$0495                       ;[3ebd] da 95 04
                    ld        a,b                           ;[3ec0] 78
                    and       $e0                           ;[3ec1] e6 e0
                    cp        $c0                           ;[3ec3] fe c0
                    jr        nz,$3ecd                      ;[3ec5] 20 06
                    inc       hl                            ;[3ec7] 23
                    inc       hl                            ;[3ec8] 23
                    inc       hl                            ;[3ec9] 23
                    ld        a,(hl)                        ;[3eca] 7e
                    inc       hl                            ;[3ecb] 23
                    dec       a                             ;[3ecc] 3d
                    jp        nz,$0495                      ;[3ecd] c2 95 04
                    ld        c,(hl)                        ;[3ed0] 4e
                    inc       hl                            ;[3ed1] 23
                    ld        b,(hl)                        ;[3ed2] 46
                    inc       hl                            ;[3ed3] 23
                    ex        de,hl                         ;[3ed4] eb
                    pop       hl                            ;[3ed5] e1
                    dec       hl                            ;[3ed6] 2b
                    ld        a,(hl)                        ;[3ed7] 7e
                    dec       hl                            ;[3ed8] 2b
                    ld        l,(hl)                        ;[3ed9] 6e
                    ld        h,a                           ;[3eda] 67
                    push      hl                            ;[3edb] e5
                    sbc       hl,bc                         ;[3edc] ed 42
                    pop       hl                            ;[3ede] e1
                    ret                                     ;[3edf] c9

                    call      $3e98                         ;[3ee0] cd 98 3e
                    jr        nc,$3efb                      ;[3ee3] 30 16
                    add       hl,de                         ;[3ee5] 19
                    ld        a,(hl)                        ;[3ee6] 7e
                    ld        hl,($5c51)                    ;[3ee7] 2a 51 5c
                    add       hl,$000d                      ;[3eea] ed 34 0d 00
                    ld        e,(hl)                        ;[3eee] 5e
                    inc       hl                            ;[3eef] 23
                    ld        d,(hl)                        ;[3ef0] 56
                    inc       de                            ;[3ef1] 13
                    ld        (hl),d                        ;[3ef2] 72
                    dec       hl                            ;[3ef3] 2b
                    ld        (hl),e                        ;[3ef4] 73
                    scf                                     ;[3ef5] 37
                    ret                                     ;[3ef6] c9

                    push      af                            ;[3ef7] f5
                    call      $3e98                         ;[3ef8] cd 98 3e
                    jp        nc,$03e0                      ;[3efb] d2 e0 03
                    add       hl,de                         ;[3efe] 19
                    pop       af                            ;[3eff] f1
                    ld        (hl),a                        ;[3f00] 77
                    jr        $3ee7                         ;[3f01] 18 e4
                    push      hl                            ;[3f03] e5
                    push      de                            ;[3f04] d5
                    ld        a,b                           ;[3f05] 78
                    push      af                            ;[3f06] f5
                    call      $3e98                         ;[3f07] cd 98 3e
                    pop       af                            ;[3f0a] f1
                    and       a                             ;[3f0b] a7
                    jr        nz,$3f14                      ;[3f0c] 20 06
                    pop       bc                            ;[3f0e] c1
                    pop       bc                            ;[3f0f] c1
                    ld        de,$0000                      ;[3f10] 11 00 00
                    ret                                     ;[3f13] c9

                    dec       a                             ;[3f14] 3d
                    jr        nz,$3f30                      ;[3f15] 20 19
                    pop       hl                            ;[3f17] e1
                    ld        a,h                           ;[3f18] 7c
                    or        l                             ;[3f19] b5
                    jp        nz,$03e0                      ;[3f1a] c2 e0 03
                    pop       hl                            ;[3f1d] e1
                    and       a                             ;[3f1e] a7
                    sbc       hl,bc                         ;[3f1f] ed 42
                    jr        nc,$3efb                      ;[3f21] 30 d8
                    add       hl,bc                         ;[3f23] 09
                    ex        de,hl                         ;[3f24] eb
                    ld        hl,($5c51)                    ;[3f25] 2a 51 5c
                    ld        bc,$000d                      ;[3f28] 01 0d 00
                    add       hl,bc                         ;[3f2b] 09
                    ld        (hl),e                        ;[3f2c] 73
                    inc       hl                            ;[3f2d] 23
                    ld        (hl),d                        ;[3f2e] 72
                    ret                                     ;[3f2f] c9

                    pop       hl                            ;[3f30] e1
                    pop       hl                            ;[3f31] e1
                    ld        h,b                           ;[3f32] 60
                    ld        l,c                           ;[3f33] 69
                    ld        de,$0000                      ;[3f34] 11 00 00
                    ret                                     ;[3f37] c9

                    ld        c,(hl)                        ;[3f38] 4e
                    ld        h,l                           ;[3f39] 65
                    ld        a,b                           ;[3f3a] 78
                    ld        (hl),h                        ;[3f3b] 74
                    ld        b,d                           ;[3f3c] 42
                    ld        b,c                           ;[3f3d] 41
                    ld        d,e                           ;[3f3e] 53
                    ld        c,c                           ;[3f3f] 49
                    jp        $6143                         ;[3f40] c3 43 61
                    ld        l,h                           ;[3f43] 6c
                    ld        h,e                           ;[3f44] 63
                    ld        (hl),l                        ;[3f45] 75
                    ld        l,h                           ;[3f46] 6c
                    ld        h,c                           ;[3f47] 61
                    ld        (hl),h                        ;[3f48] 74
                    ld        l,a                           ;[3f49] 6f
                    jp        p,$7242                       ;[3f4a] f2 42 72
                    ld        l,a                           ;[3f4d] 6f
                    ld        (hl),a                        ;[3f4e] 77
                    ld        (hl),e                        ;[3f4f] 73
                    ld        h,l                           ;[3f50] 65
                    jp        p,$b83a                       ;[3f51] f2 3a b8
                    push      de                            ;[3f54] d5
                    cp        $08                           ;[3f55] fe 08
                    ret       z                             ;[3f57] c8
                    ld        hl,$3f38                      ;[3f58] 21 38 3f
                    and       a                             ;[3f5b] a7
                    jr        z,$3f61                       ;[3f5c] 28 03
                    ld        hl,$3f41                      ;[3f5e] 21 41 3f
                    ld        de,$d6da                      ;[3f61] 11 da d6
                    push      ix                            ;[3f64] dd e5
                    push      hl                            ;[3f66] e5
                    push      de                            ;[3f67] d5
                    call      $08d0                         ;[3f68] cd d0 08
                    ex        (sp),hl                       ;[3f6b] e3
                    push      hl                            ;[3f6c] e5
                    call      $0d01                         ;[3f6d] cd 01 0d
                    ld        a,(iy+$45)                    ;[3f70] fd 7e 45
                    and       $0f                           ;[3f73] e6 0f
                    jr        nz,$3f79                      ;[3f75] 20 02
                    ld        a,$05                         ;[3f77] 3e 05
                    add       $f2                           ;[3f79] c6 f2
                    ld        ixh,a                         ;[3f7b] dd 67
                    ld        ixl,$00                       ;[3f7d] dd 2e 00
                    ld        hl,$3fef                      ;[3f80] 21 ef 3f
                    call      $274f                         ;[3f83] cd 4f 27
                    pop       hl                            ;[3f86] e1
                    push      hl                            ;[3f87] e5
                    ld        a,$07                         ;[3f88] 3e 07
                    add       hl,a                          ;[3f8a] ed 31
                    ld        a,(hl)                        ;[3f8c] 7e
                    call      $277f                         ;[3f8d] cd 7f 27
                    ld        a,$14                         ;[3f90] 3e 14
                    call      $277f                         ;[3f92] cd 7f 27
                    bit       3,(iy+$45)                    ;[3f95] fd cb 45 5e
                    ld        bc,$1a00                      ;[3f99] 01 00 1a
                    jr        z,$3fa1                       ;[3f9c] 28 03
                    ld        bc,$3901                      ;[3f9e] 01 01 39
                    push      bc                            ;[3fa1] c5
                    ld        a,c                           ;[3fa2] 79
                    call      $277f                         ;[3fa3] cd 7f 27
                    pop       bc                            ;[3fa6] c1
                    push      bc                            ;[3fa7] c5
                    ld        a,$20                         ;[3fa8] 3e 20
                    call      $277f                         ;[3faa] cd 7f 27
                    pop       bc                            ;[3fad] c1
                    djnz      $3fa7                         ;[3fae] 10 f7
                    pop       de                            ;[3fb0] d1
                    ld        a,(de)                        ;[3fb1] 1a
                    push      af                            ;[3fb2] f5
                    dec       c                             ;[3fb3] 0d
                    ld        bc,$0690                      ;[3fb4] 01 90 06
                    push      bc                            ;[3fb7] c5
                    push      de                            ;[3fb8] d5
                    ld        a,c                           ;[3fb9] 79
                    call      z,$277f                       ;[3fba] cc 7f 27
                    ld        a,$18                         ;[3fbd] 3e 18
                    call      $277f                         ;[3fbf] cd 7f 27
                    pop       de                            ;[3fc2] d1
                    inc       de                            ;[3fc3] 13
                    push      de                            ;[3fc4] d5
                    ld        a,(de)                        ;[3fc5] 1a
                    call      $277f                         ;[3fc6] cd 7f 27
                    pop       de                            ;[3fc9] d1
                    pop       bc                            ;[3fca] c1
                    ld        a,c                           ;[3fcb] 79
                    xor       $01                           ;[3fcc] ee 01
                    ld        c,a                           ;[3fce] 4f
                    xor       a                             ;[3fcf] af
                    djnz      $3fb7                         ;[3fd0] 10 e5
                    ld        hl,$3ff6                      ;[3fd2] 21 f6 3f
                    call      $274f                         ;[3fd5] cd 4f 27
                    pop       af                            ;[3fd8] f1
                    pop       de                            ;[3fd9] d1
                    call      $08d8                         ;[3fda] cd d8 08
                    pop       hl                            ;[3fdd] e1
                    push      af                            ;[3fde] f5
                    call      $274b                         ;[3fdf] cd 4b 27
                    ld        hl,$0023                      ;[3fe2] 21 23 00
                    call      $274b                         ;[3fe5] cd 4b 27
                    pop       af                            ;[3fe8] f1
                    call      $277f                         ;[3fe9] cd 7f 27
                    pop       ix                            ;[3fec] dd e1
                    ret                                     ;[3fee] c9

                    ld        e,$08                         ;[3fef] 1e 08
                    ld        d,$15                         ;[3ff1] 16 15
                    nop                                     ;[3ff3] 00
                    jr        $3ff5                         ;[3ff4] 18 ff
                    jr        nz,$400e                      ;[3ff6] 20 16
                    dec       d                             ;[3ff8] 15
                    nop                                     ;[3ff9] 00
                    rst       $38                           ;[3ffa] ff
                    nop                                     ;[3ffb] 00
                    nop                                     ;[3ffc] 00
                    nop                                     ;[3ffd] 00
                    nop                                     ;[3ffe] 00
                    nop                                     ;[3fff] 00
