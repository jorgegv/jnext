                    di                                      ;[0000] f3
                    ld        bc,$692b                      ;[0001] 01 2b 69
                    dec       bc                            ;[0004] 0b
                    ld        a,b                           ;[0005] 78
                    or        c                             ;[0006] b1
                    jr        nz,$0004                      ;[0007] 20 fb
                    jp        $00c7                         ;[0009] c3 c7 00
                    nop                                     ;[000c] 00
                    nop                                     ;[000d] 00
                    nop                                     ;[000e] 00
                    nop                                     ;[000f] 00
                    rst       $28                           ;[0010] ef
                    djnz      $0013                         ;[0011] 10 00
                    ret                                     ;[0013] c9

                    nop                                     ;[0014] 00
                    nop                                     ;[0015] 00
                    nop                                     ;[0016] 00
                    nop                                     ;[0017] 00
                    rst       $28                           ;[0018] ef
                    jr        $001b                         ;[0019] 18 00
                    ret                                     ;[001b] c9

                    nop                                     ;[001c] 00
                    nop                                     ;[001d] 00
                    nop                                     ;[001e] 00
                    nop                                     ;[001f] 00
                    rst       $28                           ;[0020] ef
                    jr        nz,$0023                      ;[0021] 20 00
                    ret                                     ;[0023] c9

                    nop                                     ;[0024] 00
                    nop                                     ;[0025] 00
                    nop                                     ;[0026] 00
                    nop                                     ;[0027] 00
                    ex        (sp),hl                       ;[0028] e3
                    push      af                            ;[0029] f5
                    ld        a,(hl)                        ;[002a] 7e
                    inc       hl                            ;[002b] 23
                    inc       hl                            ;[002c] 23
                    ld        ($5b5a),hl                    ;[002d] 22 5a 5b
                    dec       hl                            ;[0030] 2b
                    ld        h,(hl)                        ;[0031] 66
                    ld        l,a                           ;[0032] 6f
                    pop       af                            ;[0033] f1
                    jp        $005c                         ;[0034] c3 5c 00
                    nop                                     ;[0037] 00
                    push      hl                            ;[0038] e5
                    ld        hl,$0048                      ;[0039] 21 48 00
                    push      hl                            ;[003c] e5
                    ld        hl,$5b00                      ;[003d] 21 00 5b
                    push      hl                            ;[0040] e5
                    ld        hl,$0038                      ;[0041] 21 38 00
                    push      hl                            ;[0044] e5
                    jp        $5b00                         ;[0045] c3 00 5b
                    pop       hl                            ;[0048] e1
                    ret                                     ;[0049] c9

                    ld        bc,$7ffd                      ;[004a] 01 fd 7f
                    xor       a                             ;[004d] af
                    di                                      ;[004e] f3
                    out       (c),a                         ;[004f] ed 79
                    ld        ($5b5c),a                     ;[0051] 32 5c 5b
                    ei                                      ;[0054] fb
                    dec       a                             ;[0055] 3d
                    ld        (iy+$00),a                    ;[0056] fd 77 00
                    jp        $0321                         ;[0059] c3 21 03
                    ld        ($5b58),hl                    ;[005c] 22 58 5b
                    ld        hl,$5b14                      ;[005f] 21 14 5b
                    ex        (sp),hl                       ;[0062] e3
                    push      hl                            ;[0063] e5
                    ld        hl,($5b58)                    ;[0064] 2a 58 5b
                    ex        (sp),hl                       ;[0067] e3
                    jp        $5b00                         ;[0068] c3 00 5b
                    push      af                            ;[006b] f5
                    push      bc                            ;[006c] c5
                    ld        bc,$7ffd                      ;[006d] 01 fd 7f
                    ld        a,($5b5c)                     ;[0070] 3a 5c 5b
                    xor       $10                           ;[0073] ee 10
                    di                                      ;[0075] f3
                    ld        ($5b5c),a                     ;[0076] 32 5c 5b
                    out       (c),a                         ;[0079] ed 79
                    ei                                      ;[007b] fb
                    pop       bc                            ;[007c] c1
                    pop       af                            ;[007d] f1
                    ret                                     ;[007e] c9

                    call      $5b00                         ;[007f] cd 00 5b
                    push      hl                            ;[0082] e5
                    ld        hl,($5b5a)                    ;[0083] 2a 5a 5b
                    ex        (sp),hl                       ;[0086] e3
                    ret                                     ;[0087] c9

                    di                                      ;[0088] f3
                    ld        a,($5b5c)                     ;[0089] 3a 5c 5b
                    and       $ef                           ;[008c] e6 ef
                    ld        ($5b5c),a                     ;[008e] 32 5c 5b
                    ld        bc,$7ffd                      ;[0091] 01 fd 7f
                    out       (c),a                         ;[0094] ed 79
                    ei                                      ;[0096] fb
                    jp        $00c3                         ;[0097] c3 c3 00
                    ld        hl,$06d8                      ;[009a] 21 d8 06
                    jr        $00a2                         ;[009d] 18 03
                    ld        hl,$07ca                      ;[009f] 21 ca 07
                    ex        af,af'                        ;[00a2] 08
                    ld        bc,$7ffd                      ;[00a3] 01 fd 7f
                    ld        a,($5b5c)                     ;[00a6] 3a 5c 5b
                    push      af                            ;[00a9] f5
                    and       $ef                           ;[00aa] e6 ef
                    di                                      ;[00ac] f3
                    ld        ($5b5c),a                     ;[00ad] 32 5c 5b
                    out       (c),a                         ;[00b0] ed 79
                    jp        $05e6                         ;[00b2] c3 e6 05
                    ex        af,af'                        ;[00b5] 08
                    pop       af                            ;[00b6] f1
                    ld        bc,$7ffd                      ;[00b7] 01 fd 7f
                    di                                      ;[00ba] f3
                    ld        ($5b5c),a                     ;[00bb] 32 5c 5b
                    out       (c),a                         ;[00be] ed 79
                    ei                                      ;[00c0] fb
                    ex        af,af'                        ;[00c1] 08
                    ret                                     ;[00c2] c9

                    ld        hl,($5b8b)                    ;[00c3] 2a 8b 5b
                    jp        (hl)                          ;[00c6] e9
                    ld        b,$08                         ;[00c7] 06 08
                    ld        a,b                           ;[00c9] 78
                    exx                                     ;[00ca] d9
                    dec       a                             ;[00cb] 3d
                    ld        bc,$7ffd                      ;[00cc] 01 fd 7f
                    out       (c),a                         ;[00cf] ed 79
                    ld        hl,$c000                      ;[00d1] 21 00 c0
                    ld        de,$c001                      ;[00d4] 11 01 c0
                    ld        bc,$3fff                      ;[00d7] 01 ff 3f
                    ld        a,$ff                         ;[00da] 3e ff
                    ld        (hl),a                        ;[00dc] 77
                    cp        (hl)                          ;[00dd] be
                    jr        nz,$0131                      ;[00de] 20 51
                    xor       a                             ;[00e0] af
                    ld        (hl),a                        ;[00e1] 77
                    cp        (hl)                          ;[00e2] be
                    jr        nz,$0131                      ;[00e3] 20 4c
                    ldir                                    ;[00e5] ed b0
                    exx                                     ;[00e7] d9
                    djnz      $00c9                         ;[00e8] 10 df
                    ld        ($5b88),a                     ;[00ea] 32 88 5b
                    ld        c,$fd                         ;[00ed] 0e fd
                    ld        d,$ff                         ;[00ef] 16 ff
                    ld        e,$bf                         ;[00f1] 1e bf
                    ld        b,d                           ;[00f3] 42
                    ld        a,$0e                         ;[00f4] 3e 0e
                    out       (c),a                         ;[00f6] ed 79
                    ld        b,e                           ;[00f8] 43
                    ld        a,$ff                         ;[00f9] 3e ff
                    out       (c),a                         ;[00fb] ed 79
                    jr        $0137                         ;[00fd] 18 38
                    nop                                     ;[00ff] 00
                    jp        $17af                         ;[0100] c3 af 17
                    jp        $1838                         ;[0103] c3 38 18
                    jp        $1ecf                         ;[0106] c3 cf 1e
                    jp        $1f04                         ;[0109] c3 04 1f
                    jp        $004a                         ;[010c] c3 4a 00
                    jp        $03a2                         ;[010f] c3 a2 03
                    jp        $182a                         ;[0112] c3 2a 18
                    jp        $18a8                         ;[0115] c3 a8 18
                    jp        $012d                         ;[0118] c3 2d 01
                    jp        $0a05                         ;[011b] c3 05 0a
                    jp        $11a3                         ;[011e] c3 a3 11
                    jp        $06d8                         ;[0121] c3 d8 06
                    jp        $07ca                         ;[0124] c3 ca 07
                    jp        $08a3                         ;[0127] c3 a3 08
                    jp        $08f0                         ;[012a] c3 f0 08
                    rst       $28                           ;[012d] ef
                    ld        bc,$c93b                      ;[012e] 01 3b c9
                    exx                                     ;[0131] d9
                    ld        a,b                           ;[0132] 78
                    out       ($fe),a                       ;[0133] d3 fe
                    jr        $0135                         ;[0135] 18 fe
                    ld        b,d                           ;[0137] 42
                    ld        a,$07                         ;[0138] 3e 07
                    out       (c),a                         ;[013a] ed 79
                    ld        b,e                           ;[013c] 43
                    ld        a,$ff                         ;[013d] 3e ff
                    out       (c),a                         ;[013f] ed 79
                    ld        de,$5b00                      ;[0141] 11 00 5b
                    ld        hl,$006b                      ;[0144] 21 6b 00
                    ld        bc,$0058                      ;[0147] 01 58 00
                    ldir                                    ;[014a] ed b0
                    ld        a,$cf                         ;[014c] 3e cf
                    ld        ($5b5d),a                     ;[014e] 32 5d 5b
                    ld        sp,$5bff                      ;[0151] 31 ff 5b
                    ld        a,$04                         ;[0154] 3e 04
                    call      $1c64                         ;[0156] cd 64 1c
                    ld        ix,$ebec                      ;[0159] dd 21 ec eb
                    ld        ($5b83),ix                    ;[015d] dd 22 83 5b
                    ld        (ix+$0a),$00                  ;[0161] dd 36 0a 00
                    ld        (ix+$0b),$c0                  ;[0165] dd 36 0b c0
                    ld        (ix+$0c),$00                  ;[0169] dd 36 0c 00
                    ld        hl,$2bec                      ;[016d] 21 ec 2b
                    ld        a,$01                         ;[0170] 3e 01
                    ld        ($5b85),hl                    ;[0172] 22 85 5b
                    ld        ($5b87),a                     ;[0175] 32 87 5b
                    ld        a,$05                         ;[0178] 3e 05
                    call      $1c64                         ;[017a] cd 64 1c
                    ld        hl,$ffff                      ;[017d] 21 ff ff
                    ld        ($5cb4),hl                    ;[0180] 22 b4 5c
                    ld        de,$3eaf                      ;[0183] 11 af 3e
                    ld        bc,$00a8                      ;[0186] 01 a8 00
                    ex        de,hl                         ;[0189] eb
                    rst       $28                           ;[018a] ef
                    ld        h,c                           ;[018b] 61
                    ld        d,$eb                         ;[018c] 16 eb
                    inc       hl                            ;[018e] 23
                    ld        ($5c7b),hl                    ;[018f] 22 7b 5c
                    dec       hl                            ;[0192] 2b
                    ld        bc,$0040                      ;[0193] 01 40 00
                    ld        ($5c38),bc                    ;[0196] ed 43 38 5c
                    ld        ($5cb2),hl                    ;[019a] 22 b2 5c
                    ld        hl,$3c00                      ;[019d] 21 00 3c
                    ld        ($5c36),hl                    ;[01a0] 22 36 5c
                    ld        hl,($5cb2)                    ;[01a3] 2a b2 5c
                    inc       hl                            ;[01a6] 23
                    ld        sp,hl                         ;[01a7] f9
                    im        1                             ;[01a8] ed 56
                    ld        iy,$5c3a                      ;[01aa] fd 21 3a 5c
                    set       4,(iy+$01)                    ;[01ae] fd cb 01 e6
                    ei                                      ;[01b2] fb
                    ld        hl,$000b                      ;[01b3] 21 0b 00
                    ld        ($5b5f),hl                    ;[01b6] 22 5f 5b
                    xor       a                             ;[01b9] af
                    ld        ($5b61),a                     ;[01ba] 32 61 5b
                    ld        ($5b63),a                     ;[01bd] 32 63 5b
                    ld        ($5b65),a                     ;[01c0] 32 65 5b
                    ld        hl,$ec00                      ;[01c3] 21 00 ec
                    ld        ($ff24),hl                    ;[01c6] 22 24 ff
                    ld        a,$50                         ;[01c9] 3e 50
                    ld        ($5b64),a                     ;[01cb] 32 64 5b
                    ld        hl,$000a                      ;[01ce] 21 0a 00
                    ld        ($5b94),hl                    ;[01d1] 22 94 5b
                    ld        ($5b96),hl                    ;[01d4] 22 96 5b
                    ld        hl,$5cb6                      ;[01d7] 21 b6 5c
                    ld        ($5c4f),hl                    ;[01da] 22 4f 5c
                    ld        de,$0589                      ;[01dd] 11 89 05
                    ld        bc,$0015                      ;[01e0] 01 15 00
                    ex        de,hl                         ;[01e3] eb
                    ldir                                    ;[01e4] ed b0
                    ex        de,hl                         ;[01e6] eb
                    dec       hl                            ;[01e7] 2b
                    ld        ($5c57),hl                    ;[01e8] 22 57 5c
                    inc       hl                            ;[01eb] 23
                    ld        ($5c53),hl                    ;[01ec] 22 53 5c
                    ld        ($5c4b),hl                    ;[01ef] 22 4b 5c
                    ld        (hl),$80                      ;[01f2] 36 80
                    inc       hl                            ;[01f4] 23
                    ld        ($5c59),hl                    ;[01f5] 22 59 5c
                    ld        (hl),$0d                      ;[01f8] 36 0d
                    inc       hl                            ;[01fa] 23
                    ld        (hl),$80                      ;[01fb] 36 80
                    inc       hl                            ;[01fd] 23
                    ld        ($5c61),hl                    ;[01fe] 22 61 5c
                    ld        ($5c63),hl                    ;[0201] 22 63 5c
                    ld        ($5c65),hl                    ;[0204] 22 65 5c
                    ld        a,$38                         ;[0207] 3e 38
                    ld        ($5c8d),a                     ;[0209] 32 8d 5c
                    ld        ($5c8f),a                     ;[020c] 32 8f 5c
                    ld        ($5c48),a                     ;[020f] 32 48 5c
                    xor       a                             ;[0212] af
                    ld        ($ec13),a                     ;[0213] 32 13 ec
                    ld        a,$07                         ;[0216] 3e 07
                    out       ($fe),a                       ;[0218] d3 fe
                    ld        hl,$0523                      ;[021a] 21 23 05
                    ld        ($5c09),hl                    ;[021d] 22 09 5c
                    dec       (iy-$3a)                      ;[0220] fd 35 c6
                    dec       (iy-$36)                      ;[0223] fd 35 ca
                    ld        hl,$059e                      ;[0226] 21 9e 05
                    ld        de,$5c10                      ;[0229] 11 10 5c
                    ld        bc,$000e                      ;[022c] 01 0e 00
                    ldir                                    ;[022f] ed b0
                    res       1,(iy+$01)                    ;[0231] fd cb 01 8e
                    ld        (iy+$00),$ff                  ;[0235] fd 36 00 ff
                    ld        (iy+$31),$02                  ;[0239] fd 36 31 02
                    rst       $28                           ;[023d] ef
                    ld        l,e                           ;[023e] 6b
                    dec       c                             ;[023f] 0d
                    rst       $28                           ;[0240] ef
                    inc       b                             ;[0241] 04
                    inc       a                             ;[0242] 3c
                    ld        de,$0561                      ;[0243] 11 61 05
                    call      $057d                         ;[0246] cd 7d 05
                    ld        (iy+$31),$02                  ;[0249] fd 36 31 02
                    set       5,(iy+$02)                    ;[024d] fd cb 02 ee
                    ld        hl,$5bff                      ;[0251] 21 ff 5b
                    ld        ($5b81),hl                    ;[0254] 22 81 5b
                    call      $1f45                         ;[0257] cd 45 1f
                    ld        a,$38                         ;[025a] 3e 38
                    ld        ($ec11),a                     ;[025c] 32 11 ec
                    ld        ($ec0f),a                     ;[025f] 32 0f ec
                    call      $2584                         ;[0262] cd 84 25
                    call      $1f20                         ;[0265] cd 20 1f
                    jp        $259f                         ;[0268] c3 9f 25
                    ld        hl,$5b66                      ;[026b] 21 66 5b
                    set       0,(hl)                        ;[026e] cb c6
                    ld        (iy+$00),$ff                  ;[0270] fd 36 00 ff
                    ld        (iy+$31),$02                  ;[0274] fd 36 31 02
                    ld        hl,$5b1d                      ;[0278] 21 1d 5b
                    push      hl                            ;[027b] e5
                    ld        ($5c3d),sp                    ;[027c] ed 73 3d 5c
                    ld        hl,$02ba                      ;[0280] 21 ba 02
                    ld        ($5b8b),hl                    ;[0283] 22 8b 5b
                    call      $228e                         ;[0286] cd 8e 22
                    call      $22cb                         ;[0289] cd cb 22
                    jp        z,$21f8                       ;[028c] ca f8 21
                    cp        $28                           ;[028f] fe 28
                    jp        z,$21f8                       ;[0291] ca f8 21
                    cp        $2d                           ;[0294] fe 2d
                    jp        z,$21f8                       ;[0296] ca f8 21
                    cp        $2b                           ;[0299] fe 2b
                    jp        z,$21f8                       ;[029b] ca f8 21
                    call      $22e0                         ;[029e] cd e0 22
                    jp        z,$21f8                       ;[02a1] ca f8 21
                    call      $1f45                         ;[02a4] cd 45 1f
                    ld        a,($ec0e)                     ;[02a7] 3a 0e ec
                    call      $1f20                         ;[02aa] cd 20 1f
                    cp        $04                           ;[02ad] fe 04
                    jp        nz,$17af                      ;[02af] c2 af 17
                    call      $2297                         ;[02b2] cd 97 22
                    jp        z,$17af                       ;[02b5] ca af 17
                    pop       hl                            ;[02b8] e1
                    ret                                     ;[02b9] c9

                    bit       7,(iy+$00)                    ;[02ba] fd cb 00 7e
                    jr        nz,$02c1                      ;[02be] 20 01
                    ret                                     ;[02c0] c9

                    ld        hl,($5c59)                    ;[02c1] 2a 59 5c
                    ld        ($5c5d),hl                    ;[02c4] 22 5d 5c
                    rst       $28                           ;[02c7] ef
                    ei                                      ;[02c8] fb
                    add       hl,de                         ;[02c9] 19
                    ld        a,b                           ;[02ca] 78
                    or        c                             ;[02cb] b1
                    jp        nz,$03f7                      ;[02cc] c2 f7 03
                    rst       $18                           ;[02cf] df
                    cp        $0d                           ;[02d0] fe 0d
                    ret       z                             ;[02d2] c8
                    call      $21ef                         ;[02d3] cd ef 21
                    bit       6,(iy+$02)                    ;[02d6] fd cb 02 76
                    jr        nz,$02df                      ;[02da] 20 03
                    rst       $28                           ;[02dc] ef
                    ld        l,(hl)                        ;[02dd] 6e
                    dec       c                             ;[02de] 0d
                    res       6,(iy+$02)                    ;[02df] fd cb 02 b6
                    call      $1f45                         ;[02e3] cd 45 1f
                    ld        hl,$ec0d                      ;[02e6] 21 0d ec
                    bit       6,(hl)                        ;[02e9] cb 76
                    jr        nz,$02f4                      ;[02eb] 20 07
                    inc       hl                            ;[02ed] 23
                    ld        a,(hl)                        ;[02ee] 7e
                    cp        $00                           ;[02ef] fe 00
                    call      z,$3881                       ;[02f1] cc 81 38
                    call      $1f20                         ;[02f4] cd 20 1f
                    ld        hl,$5c3c                      ;[02f7] 21 3c 5c
                    res       3,(hl)                        ;[02fa] cb 9e
                    ld        a,$19                         ;[02fc] 3e 19
                    sub       (iy+$4f)                      ;[02fe] fd 96 4f
                    ld        ($5c8c),a                     ;[0301] 32 8c 5c
                    set       7,(iy+$01)                    ;[0304] fd cb 01 fe
                    ld        (iy+$0a),$01                  ;[0308] fd 36 0a 01
                    ld        hl,$3e00                      ;[030c] 21 00 3e
                    push      hl                            ;[030f] e5
                    ld        hl,$5b1d                      ;[0310] 21 1d 5b
                    push      hl                            ;[0313] e5
                    ld        ($5c3d),sp                    ;[0314] ed 73 3d 5c
                    ld        hl,$0321                      ;[0318] 21 21 03
                    ld        ($5b8b),hl                    ;[031b] 22 8b 5b
                    jp        $1838                         ;[031e] c3 38 18
                    ld        sp,($5cb2)                    ;[0321] ed 7b b2 5c
                    inc       sp                            ;[0325] 33
                    ld        hl,$5bff                      ;[0326] 21 ff 5b
                    ld        ($5b81),hl                    ;[0329] 22 81 5b
                    halt                                    ;[032c] 76
                    res       5,(iy+$01)                    ;[032d] fd cb 01 ae
                    ld        hl,$5b66                      ;[0331] 21 66 5b
                    bit       2,(hl)                        ;[0334] cb 56
                    jr        z,$034a                       ;[0336] 28 12
                    call      $1f45                         ;[0338] cd 45 1f
                    ld        ix,($5b83)                    ;[033b] dd 2a 83 5b
                    ld        bc,$0014                      ;[033f] 01 14 00
                    add       ix,bc                         ;[0342] dd 09
                    call      $1d56                         ;[0344] cd 56 1d
                    call      $1f20                         ;[0347] cd 20 1f
                    ld        a,($5c3a)                     ;[034a] 3a 3a 5c
                    inc       a                             ;[034d] 3c
                    push      af                            ;[034e] f5
                    ld        hl,$0000                      ;[034f] 21 00 00
                    ld        (iy+$37),h                    ;[0352] fd 74 37
                    ld        (iy+$26),h                    ;[0355] fd 74 26
                    ld        ($5c0b),hl                    ;[0358] 22 0b 5c
                    ld        hl,$0001                      ;[035b] 21 01 00
                    ld        ($5c16),hl                    ;[035e] 22 16 5c
                    rst       $28                           ;[0361] ef
                    or        b                             ;[0362] b0
                    ld        d,$fd                         ;[0363] 16 fd
                    sll       a                             ;[0365] cb 37
                    xor       (hl)                          ;[0367] ae
                    rst       $28                           ;[0368] ef
                    ld        l,(hl)                        ;[0369] 6e
                    dec       c                             ;[036a] 0d
                    set       5,(iy+$02)                    ;[036b] fd cb 02 ee
                    pop       af                            ;[036f] f1
                    ld        b,a                           ;[0370] 47
                    cp        $0a                           ;[0371] fe 0a
                    jr        c,$037f                       ;[0373] 38 0a
                    cp        $1d                           ;[0375] fe 1d
                    jr        c,$037d                       ;[0377] 38 04
                    add       $14                           ;[0379] c6 14
                    jr        $037f                         ;[037b] 18 02
                    add       $07                           ;[037d] c6 07
                    rst       $28                           ;[037f] ef
                    rst       $28                           ;[0380] ef
                    dec       d                             ;[0381] 15
                    ld        a,$20                         ;[0382] 3e 20
                    rst       $10                           ;[0384] d7
                    ld        a,b                           ;[0385] 78
                    cp        $1d                           ;[0386] fe 1d
                    jr        c,$039c                       ;[0388] 38 12
                    sub       $1d                           ;[038a] d6 1d
                    ld        b,$00                         ;[038c] 06 00
                    ld        c,a                           ;[038e] 4f
                    ld        hl,$046c                      ;[038f] 21 6c 04
                    add       hl,bc                         ;[0392] 09
                    add       hl,bc                         ;[0393] 09
                    ld        e,(hl)                        ;[0394] 5e
                    inc       hl                            ;[0395] 23
                    ld        d,(hl)                        ;[0396] 56
                    call      $057d                         ;[0397] cd 7d 05
                    jr        $03a2                         ;[039a] 18 06
                    ld        de,$1391                      ;[039c] 11 91 13
                    rst       $28                           ;[039f] ef
                    ld        a,(bc)                        ;[03a0] 0a
                    inc       c                             ;[03a1] 0c
                    xor       a                             ;[03a2] af
                    ld        de,$1536                      ;[03a3] 11 36 15
                    rst       $28                           ;[03a6] ef
                    ld        a,(bc)                        ;[03a7] 0a
                    inc       c                             ;[03a8] 0c
                    ld        bc,($5c45)                    ;[03a9] ed 4b 45 5c
                    rst       $28                           ;[03ad] ef
                    dec       de                            ;[03ae] 1b
                    ld        a,(de)                        ;[03af] 1a
                    ld        a,$3a                         ;[03b0] 3e 3a
                    rst       $10                           ;[03b2] d7
                    ld        c,(iy+$0d)                    ;[03b3] fd 4e 0d
                    ld        b,$00                         ;[03b6] 06 00
                    rst       $28                           ;[03b8] ef
                    dec       de                            ;[03b9] 1b
                    ld        a,(de)                        ;[03ba] 1a
                    rst       $28                           ;[03bb] ef
                    sub       a                             ;[03bc] 97
                    djnz      $03f9                         ;[03bd] 10 3a
                    ld        a,($3c5c)                     ;[03bf] 3a 5c 3c
                    jr        z,$03df                       ;[03c2] 28 1b
                    cp        $09                           ;[03c4] fe 09
                    jr        z,$03cc                       ;[03c6] 28 04
                    cp        $15                           ;[03c8] fe 15
                    jr        nz,$03cf                      ;[03ca] 20 03
                    inc       (iy+$0d)                      ;[03cc] fd 34 0d
                    ld        bc,$0003                      ;[03cf] 01 03 00
                    ld        de,$5c70                      ;[03d2] 11 70 5c
                    ld        hl,$5c44                      ;[03d5] 21 44 5c
                    bit       7,(hl)                        ;[03d8] cb 7e
                    jr        z,$03dd                       ;[03da] 28 01
                    add       hl,bc                         ;[03dc] 09
                    lddr                                    ;[03dd] ed b8
                    ld        (iy+$0a),$ff                  ;[03df] fd 36 0a ff
                    res       3,(iy+$01)                    ;[03e3] fd cb 01 9e
                    ld        hl,$5b66                      ;[03e7] 21 66 5b
                    res       0,(hl)                        ;[03ea] cb 86
                    jp        $25cb                         ;[03ec] c3 cb 25
                    ld        a,$10                         ;[03ef] 3e 10
                    ld        bc,$0000                      ;[03f1] 01 00 00
                    jp        $034e                         ;[03f4] c3 4e 03
                    ld        ($5c49),bc                    ;[03f7] ed 43 49 5c
                    call      $1f45                         ;[03fb] cd 45 1f
                    ld        a,b                           ;[03fe] 78
                    or        c                             ;[03ff] b1
                    jr        z,$040a                       ;[0400] 28 08
                    ld        ($5c49),bc                    ;[0402] ed 43 49 5c
                    ld        ($ec08),bc                    ;[0406] ed 43 08 ec
                    call      $1f20                         ;[040a] cd 20 1f
                    ld        hl,($5c5d)                    ;[040d] 2a 5d 5c
                    ex        de,hl                         ;[0410] eb
                    ld        hl,$03ef                      ;[0411] 21 ef 03
                    push      hl                            ;[0414] e5
                    ld        hl,($5c61)                    ;[0415] 2a 61 5c
                    scf                                     ;[0418] 37
                    sbc       hl,de                         ;[0419] ed 52
                    push      hl                            ;[041b] e5
                    ld        h,b                           ;[041c] 60
                    ld        l,c                           ;[041d] 69
                    rst       $28                           ;[041e] ef
                    ld        l,(hl)                        ;[041f] 6e
                    add       hl,de                         ;[0420] 19
                    jr        nz,$0429                      ;[0421] 20 06
                    rst       $28                           ;[0423] ef
                    cp        b                             ;[0424] b8
                    add       hl,de                         ;[0425] 19
                    rst       $28                           ;[0426] ef
                    ret       pe                            ;[0427] e8
                    add       hl,de                         ;[0428] 19
                    pop       bc                            ;[0429] c1
                    ld        a,c                           ;[042a] 79
                    dec       a                             ;[042b] 3d
                    or        b                             ;[042c] b0
                    jr        nz,$0442                      ;[042d] 20 13
                    call      $1f45                         ;[042f] cd 45 1f
                    push      hl                            ;[0432] e5
                    ld        hl,($5c49)                    ;[0433] 2a 49 5c
                    call      $334a                         ;[0436] cd 4a 33
                    ld        ($5c49),hl                    ;[0439] 22 49 5c
                    pop       hl                            ;[043c] e1
                    call      $1f20                         ;[043d] cd 20 1f
                    jr        $046a                         ;[0440] 18 28
                    push      bc                            ;[0442] c5
                    inc       bc                            ;[0443] 03
                    inc       bc                            ;[0444] 03
                    inc       bc                            ;[0445] 03
                    inc       bc                            ;[0446] 03
                    dec       hl                            ;[0447] 2b
                    ld        de,($5c53)                    ;[0448] ed 5b 53 5c
                    push      de                            ;[044c] d5
                    rst       $28                           ;[044d] ef
                    ld        d,l                           ;[044e] 55
                    ld        d,$e1                         ;[044f] 16 e1
                    ld        ($5c53),hl                    ;[0451] 22 53 5c
                    pop       bc                            ;[0454] c1
                    push      bc                            ;[0455] c5
                    inc       de                            ;[0456] 13
                    ld        hl,($5c61)                    ;[0457] 2a 61 5c
                    dec       hl                            ;[045a] 2b
                    dec       hl                            ;[045b] 2b
                    lddr                                    ;[045c] ed b8
                    ld        hl,($5c49)                    ;[045e] 2a 49 5c
                    ex        de,hl                         ;[0461] eb
                    pop       bc                            ;[0462] c1
                    ld        (hl),b                        ;[0463] 70
                    dec       hl                            ;[0464] 2b
                    ld        (hl),c                        ;[0465] 71
                    dec       hl                            ;[0466] 2b
                    ld        (hl),e                        ;[0467] 73
                    dec       hl                            ;[0468] 2b
                    ld        (hl),d                        ;[0469] 72
                    pop       af                            ;[046a] f1
                    ret                                     ;[046b] c9

                    adc       h                             ;[046c] 8c
                    inc       b                             ;[046d] 04
                    sub       a                             ;[046e] 97
                    inc       b                             ;[046f] 04
                    and       (hl)                          ;[0470] a6
                    inc       b                             ;[0471] 04
                    or        b                             ;[0472] b0
                    inc       b                             ;[0473] 04
                    pop       bc                            ;[0474] c1
                    inc       b                             ;[0475] 04
                    call      nc,$e004                      ;[0476] d4 04 e0
                    inc       b                             ;[0479] 04
                    ret       po                            ;[047a] e0
                    inc       b                             ;[047b] 04
                    di                                      ;[047c] f3
                    inc       b                             ;[047d] 04
                    ld        bc,$1205                      ;[047e] 01 05 12
                    dec       b                             ;[0481] 05
                    inc       hl                            ;[0482] 23
                    dec       b                             ;[0483] 05
                    ld        sp,$4205                      ;[0484] 31 05 42
                    dec       b                             ;[0487] 05
                    ld        c,(hl)                        ;[0488] 4e
                    dec       b                             ;[0489] 05
                    ld        h,c                           ;[048a] 61
                    dec       b                             ;[048b] 05
                    ld        c,l                           ;[048c] 4d
                    ld        b,l                           ;[048d] 45
                    ld        d,d                           ;[048e] 52
                    ld        b,a                           ;[048f] 47
                    ld        b,l                           ;[0490] 45
                    jr        nz,$04f8                      ;[0491] 20 65
                    ld        (hl),d                        ;[0493] 72
                    ld        (hl),d                        ;[0494] 72
                    ld        l,a                           ;[0495] 6f
                    jp        p,$7257                       ;[0496] f2 57 72
                    ld        l,a                           ;[0499] 6f
                    ld        l,(hl)                        ;[049a] 6e
                    ld        h,a                           ;[049b] 67
                    jr        nz,$0504                      ;[049c] 20 66
                    ld        l,c                           ;[049e] 69
                    ld        l,h                           ;[049f] 6c
                    ld        h,l                           ;[04a0] 65
                    jr        nz,$0517                      ;[04a1] 20 74
                    ld        a,c                           ;[04a3] 79
                    ld        (hl),b                        ;[04a4] 70
                    push      hl                            ;[04a5] e5
                    ld        b,e                           ;[04a6] 43
                    ld        c,a                           ;[04a7] 4f
                    ld        b,h                           ;[04a8] 44
                    ld        b,l                           ;[04a9] 45
                    jr        nz,$0511                      ;[04aa] 20 65
                    ld        (hl),d                        ;[04ac] 72
                    ld        (hl),d                        ;[04ad] 72
                    ld        l,a                           ;[04ae] 6f
                    jp        p,$6f54                       ;[04af] f2 54 6f
                    ld        l,a                           ;[04b2] 6f
                    jr        nz,$0522                      ;[04b3] 20 6d
                    ld        h,c                           ;[04b5] 61
                    ld        l,(hl)                        ;[04b6] 6e
                    ld        a,c                           ;[04b7] 79
                    jr        nz,$051c                      ;[04b8] 20 62
                    ld        (hl),d                        ;[04ba] 72
                    ld        h,c                           ;[04bb] 61
                    ld        h,e                           ;[04bc] 63
                    ld        l,e                           ;[04bd] 6b
                    ld        h,l                           ;[04be] 65
                    ld        (hl),h                        ;[04bf] 74
                    di                                      ;[04c0] f3
                    ld        b,(hl)                        ;[04c1] 46
                    ld        l,c                           ;[04c2] 69
                    ld        l,h                           ;[04c3] 6c
                    ld        h,l                           ;[04c4] 65
                    jr        nz,$0528                      ;[04c5] 20 61
                    ld        l,h                           ;[04c7] 6c
                    ld        (hl),d                        ;[04c8] 72
                    ld        h,l                           ;[04c9] 65
                    ld        h,c                           ;[04ca] 61
                    ld        h,h                           ;[04cb] 64
                    ld        a,c                           ;[04cc] 79
                    jr        nz,$0534                      ;[04cd] 20 65
                    ld        a,b                           ;[04cf] 78
                    ld        l,c                           ;[04d0] 69
                    ld        (hl),e                        ;[04d1] 73
                    ld        (hl),h                        ;[04d2] 74
                    di                                      ;[04d3] f3
                    ld        c,c                           ;[04d4] 49
                    ld        l,(hl)                        ;[04d5] 6e
                    halt                                    ;[04d6] 76
                    ld        h,c                           ;[04d7] 61
                    ld        l,h                           ;[04d8] 6c
                    ld        l,c                           ;[04d9] 69
                    ld        h,h                           ;[04da] 64
                    jr        nz,$054b                      ;[04db] 20 6e
                    ld        h,c                           ;[04dd] 61
                    ld        l,l                           ;[04de] 6d
                    push      hl                            ;[04df] e5
                    ld        b,(hl)                        ;[04e0] 46
                    ld        l,c                           ;[04e1] 69
                    ld        l,h                           ;[04e2] 6c
                    ld        h,l                           ;[04e3] 65
                    jr        nz,$054a                      ;[04e4] 20 64
                    ld        l,a                           ;[04e6] 6f
                    ld        h,l                           ;[04e7] 65
                    ld        (hl),e                        ;[04e8] 73
                    jr        nz,$0559                      ;[04e9] 20 6e
                    ld        l,a                           ;[04eb] 6f
                    ld        (hl),h                        ;[04ec] 74
                    jr        nz,$0554                      ;[04ed] 20 65
                    ld        a,b                           ;[04ef] 78
                    ld        l,c                           ;[04f0] 69
                    ld        (hl),e                        ;[04f1] 73
                    call      p,$6e49                       ;[04f2] f4 49 6e
                    halt                                    ;[04f5] 76
                    ld        h,c                           ;[04f6] 61
                    ld        l,h                           ;[04f7] 6c
                    ld        l,c                           ;[04f8] 69
                    ld        h,h                           ;[04f9] 64
                    jr        nz,$0560                      ;[04fa] 20 64
                    ld        h,l                           ;[04fc] 65
                    halt                                    ;[04fd] 76
                    ld        l,c                           ;[04fe] 69
                    ld        h,e                           ;[04ff] 63
                    push      hl                            ;[0500] e5
                    ld        c,c                           ;[0501] 49
                    ld        l,(hl)                        ;[0502] 6e
                    halt                                    ;[0503] 76
                    ld        h,c                           ;[0504] 61
                    ld        l,h                           ;[0505] 6c
                    ld        l,c                           ;[0506] 69
                    ld        h,h                           ;[0507] 64
                    jr        nz,$056c                      ;[0508] 20 62
                    ld        h,c                           ;[050a] 61
                    ld        (hl),l                        ;[050b] 75
                    ld        h,h                           ;[050c] 64
                    jr        nz,$0581                      ;[050d] 20 72
                    ld        h,c                           ;[050f] 61
                    ld        (hl),h                        ;[0510] 74
                    push      hl                            ;[0511] e5
                    ld        c,c                           ;[0512] 49
                    ld        l,(hl)                        ;[0513] 6e
                    halt                                    ;[0514] 76
                    ld        h,c                           ;[0515] 61
                    ld        l,h                           ;[0516] 6c
                    ld        l,c                           ;[0517] 69
                    ld        h,h                           ;[0518] 64
                    jr        nz,$0589                      ;[0519] 20 6e
                    ld        l,a                           ;[051b] 6f
                    ld        (hl),h                        ;[051c] 74
                    ld        h,l                           ;[051d] 65
                    jr        nz,$058e                      ;[051e] 20 6e
                    ld        h,c                           ;[0520] 61
                    ld        l,l                           ;[0521] 6d
                    push      hl                            ;[0522] e5
                    ld        c,(hl)                        ;[0523] 4e
                    ld        (hl),l                        ;[0524] 75
                    ld        l,l                           ;[0525] 6d
                    ld        h,d                           ;[0526] 62
                    ld        h,l                           ;[0527] 65
                    ld        (hl),d                        ;[0528] 72
                    jr        nz,$059f                      ;[0529] 20 74
                    ld        l,a                           ;[052b] 6f
                    ld        l,a                           ;[052c] 6f
                    jr        nz,$0591                      ;[052d] 20 62
                    ld        l,c                           ;[052f] 69
                    rst       $20                           ;[0530] e7
                    ld        c,(hl)                        ;[0531] 4e
                    ld        l,a                           ;[0532] 6f
                    ld        (hl),h                        ;[0533] 74
                    ld        h,l                           ;[0534] 65
                    jr        nz,$05a6                      ;[0535] 20 6f
                    ld        (hl),l                        ;[0537] 75
                    ld        (hl),h                        ;[0538] 74
                    jr        nz,$05aa                      ;[0539] 20 6f
                    ld        h,(hl)                        ;[053b] 66
                    jr        nz,$05b0                      ;[053c] 20 72
                    ld        h,c                           ;[053e] 61
                    ld        l,(hl)                        ;[053f] 6e
                    ld        h,a                           ;[0540] 67
                    push      hl                            ;[0541] e5
                    ld        c,a                           ;[0542] 4f
                    ld        (hl),l                        ;[0543] 75
                    ld        (hl),h                        ;[0544] 74
                    jr        nz,$05b6                      ;[0545] 20 6f
                    ld        h,(hl)                        ;[0547] 66
                    jr        nz,$05bc                      ;[0548] 20 72
                    ld        h,c                           ;[054a] 61
                    ld        l,(hl)                        ;[054b] 6e
                    ld        h,a                           ;[054c] 67
                    push      hl                            ;[054d] e5
                    ld        d,h                           ;[054e] 54
                    ld        l,a                           ;[054f] 6f
                    ld        l,a                           ;[0550] 6f
                    jr        nz,$05c0                      ;[0551] 20 6d
                    ld        h,c                           ;[0553] 61
                    ld        l,(hl)                        ;[0554] 6e
                    ld        a,c                           ;[0555] 79
                    jr        nz,$05cc                      ;[0556] 20 74
                    ld        l,c                           ;[0558] 69
                    ld        h,l                           ;[0559] 65
                    ld        h,h                           ;[055a] 64
                    jr        nz,$05cb                      ;[055b] 20 6e
                    ld        l,a                           ;[055d] 6f
                    ld        (hl),h                        ;[055e] 74
                    ld        h,l                           ;[055f] 65
                    di                                      ;[0560] f3
                    ld        a,a                           ;[0561] 7f
                    jr        nz,$0595                      ;[0562] 20 31
                    add       hl,sp                         ;[0564] 39
                    jr        c,$059d                       ;[0565] 38 36
                    jr        nz,$05bc                      ;[0567] 20 53
                    ld        l,c                           ;[0569] 69
                    ld        l,(hl)                        ;[056a] 6e
                    ld        h,e                           ;[056b] 63
                    ld        l,h                           ;[056c] 6c
                    ld        h,c                           ;[056d] 61
                    ld        l,c                           ;[056e] 69
                    ld        (hl),d                        ;[056f] 72
                    jr        nz,$05c4                      ;[0570] 20 52
                    ld        h,l                           ;[0572] 65
                    ld        (hl),e                        ;[0573] 73
                    ld        h,l                           ;[0574] 65
                    ld        h,c                           ;[0575] 61
                    ld        (hl),d                        ;[0576] 72
                    ld        h,e                           ;[0577] 63
                    ld        l,b                           ;[0578] 68
                    jr        nz,$05c7                      ;[0579] 20 4c
                    ld        (hl),h                        ;[057b] 74
                    call      po,$e61a                      ;[057c] e4 1a e6
                    ld        a,a                           ;[057f] 7f
                    push      de                            ;[0580] d5
                    rst       $10                           ;[0581] d7
                    pop       de                            ;[0582] d1
                    ld        a,(de)                        ;[0583] 1a
                    inc       de                            ;[0584] 13
                    add       a                             ;[0585] 87
                    jr        nc,$057d                      ;[0586] 30 f5
                    ret                                     ;[0588] c9

                    call      p,$a809                       ;[0589] f4 09 a8
                    djnz      $05d9                         ;[058c] 10 4b
                    call      p,$c409                       ;[058e] f4 09 c4
                    dec       d                             ;[0591] 15
                    ld        d,e                           ;[0592] 53
                    add       c                             ;[0593] 81
                    rrca                                    ;[0594] 0f
                    call      nz,$5215                      ;[0595] c4 15 52
                    inc       (hl)                          ;[0598] 34
                    ld        e,e                           ;[0599] 5b
                    cpl                                     ;[059a] 2f
                    ld        e,e                           ;[059b] 5b
                    ld        d,b                           ;[059c] 50
                    add       b                             ;[059d] 80
                    ld        bc,$0600                      ;[059e] 01 00 06
                    nop                                     ;[05a1] 00
                    dec       bc                            ;[05a2] 0b
                    nop                                     ;[05a3] 00
                    ld        bc,$0100                      ;[05a4] 01 00 01
                    nop                                     ;[05a7] 00
                    ld        b,$00                         ;[05a8] 06 00
                    djnz      $05ac                         ;[05aa] 10 00
                    pop       hl                            ;[05ac] e1
                    ld        bc,$7ffd                      ;[05ad] 01 fd 7f
                    xor       a                             ;[05b0] af
                    di                                      ;[05b1] f3
                    ld        ($5b5c),a                     ;[05b2] 32 5c 5b
                    out       (c),a                         ;[05b5] ed 79
                    ei                                      ;[05b7] fb
                    ld        sp,($5c3d)                    ;[05b8] ed 7b 3d 5c
                    ld        a,(hl)                        ;[05bc] 7e
                    ld        ($5b5e),a                     ;[05bd] 32 5e 5b
                    inc       a                             ;[05c0] 3c
                    cp        $1e                           ;[05c1] fe 1e
                    jr        nc,$05c8                      ;[05c3] 30 03
                    rst       $28                           ;[05c5] ef
                    ld        e,l                           ;[05c6] 5d
                    ld        e,e                           ;[05c7] 5b
                    dec       a                             ;[05c8] 3d
                    ld        (iy+$00),a                    ;[05c9] fd 77 00
                    ld        hl,($5c5d)                    ;[05cc] 2a 5d 5c
                    ld        ($5c5f),hl                    ;[05cf] 22 5f 5c
                    rst       $28                           ;[05d2] ef
                    push      bc                            ;[05d3] c5
                    ld        d,$c9                         ;[05d4] 16 c9
                    ld        a,$7f                         ;[05d6] 3e 7f
                    in        a,($fe)                       ;[05d8] db fe
                    rra                                     ;[05da] 1f
                    ret       c                             ;[05db] d8
                    ld        a,$fe                         ;[05dc] 3e fe
                    in        a,($fe)                       ;[05de] db fe
                    rra                                     ;[05e0] 1f
                    ret       c                             ;[05e1] d8
                    call      $05ac                         ;[05e2] cd ac 05
                    inc       d                             ;[05e5] 14
                    ei                                      ;[05e6] fb
                    ex        af,af'                        ;[05e7] 08
                    ld        de,$5b4a                      ;[05e8] 11 4a 5b
                    push      de                            ;[05eb] d5
                    res       3,(iy+$02)                    ;[05ec] fd cb 02 9e
                    push      hl                            ;[05f0] e5
                    ld        hl,($5c3d)                    ;[05f1] 2a 3d 5c
                    ld        e,(hl)                        ;[05f4] 5e
                    inc       hl                            ;[05f5] 23
                    ld        d,(hl)                        ;[05f6] 56
                    and       a                             ;[05f7] a7
                    ld        hl,$107f                      ;[05f8] 21 7f 10
                    sbc       hl,de                         ;[05fb] ed 52
                    jr        nz,$0637                      ;[05fd] 20 38
                    pop       hl                            ;[05ff] e1
                    ld        sp,($5c3d)                    ;[0600] ed 7b 3d 5c
                    pop       de                            ;[0604] d1
                    pop       de                            ;[0605] d1
                    ld        ($5c3d),de                    ;[0606] ed 53 3d 5c
                    push      hl                            ;[060a] e5
                    ld        de,$0610                      ;[060b] 11 10 06
                    push      de                            ;[060e] d5
                    jp        (hl)                          ;[060f] e9
                    jr        c,$061b                       ;[0610] 38 09
                    jr        z,$0618                       ;[0612] 28 04
                    call      $05ac                         ;[0614] cd ac 05
                    rlca                                    ;[0617] 07
                    pop       hl                            ;[0618] e1
                    jr        $060a                         ;[0619] 18 ef
                    cp        $0d                           ;[061b] fe 0d
                    jr        z,$062d                       ;[061d] 28 0e
                    ld        hl,($5b5a)                    ;[061f] 2a 5a 5b
                    push      hl                            ;[0622] e5
                    rst       $28                           ;[0623] ef
                    add       l                             ;[0624] 85
                    rrca                                    ;[0625] 0f
                    pop       hl                            ;[0626] e1
                    ld        ($5b5a),hl                    ;[0627] 22 5a 5b
                    pop       hl                            ;[062a] e1
                    jr        $060a                         ;[062b] 18 dd
                    pop       hl                            ;[062d] e1
                    ld        a,($5b5c)                     ;[062e] 3a 5c 5b
                    or        $10                           ;[0631] f6 10
                    push      af                            ;[0633] f5
                    jp        $5b4a                         ;[0634] c3 4a 5b
                    pop       hl                            ;[0637] e1
                    ld        de,$063d                      ;[0638] 11 3d 06
                    push      de                            ;[063b] d5
                    jp        (hl)                          ;[063c] e9
                    ret       c                             ;[063d] d8
                    ret       z                             ;[063e] c8
                    jr        $0614                         ;[063f] 18 d3
                    rst       $28                           ;[0641] ef
                    jr        $0644                         ;[0642] 18 00
                    rst       $28                           ;[0644] ef
                    adc       h                             ;[0645] 8c
                    inc       e                             ;[0646] 1c
                    bit       7,(iy+$01)                    ;[0647] fd cb 01 7e
                    jr        z,$0661                       ;[064b] 28 14
                    rst       $28                           ;[064d] ef
                    pop       af                            ;[064e] f1
                    dec       hl                            ;[064f] 2b
                    ld        a,c                           ;[0650] 79
                    dec       a                             ;[0651] 3d
                    or        b                             ;[0652] b0
                    jr        z,$0659                       ;[0653] 28 04
                    call      $05ac                         ;[0655] cd ac 05
                    inc       h                             ;[0658] 24
                    ld        a,(de)                        ;[0659] 1a
                    and       $df                           ;[065a] e6 df
                    cp        $50                           ;[065c] fe 50
                    jp        nz,$1912                      ;[065e] c2 12 19
                    ld        hl,($5c5d)                    ;[0661] 2a 5d 5c
                    ld        a,(hl)                        ;[0664] 7e
                    cp        $3b                           ;[0665] fe 3b
                    jp        nz,$1912                      ;[0667] c2 12 19
                    rst       $28                           ;[066a] ef
                    jr        nz,$066d                      ;[066b] 20 00
                    rst       $28                           ;[066d] ef
                    add       d                             ;[066e] 82
                    inc       e                             ;[066f] 1c
                    bit       7,(iy+$01)                    ;[0670] fd cb 01 7e
                    jr        z,$067d                       ;[0674] 28 07
                    rst       $28                           ;[0676] ef
                    sbc       c                             ;[0677] 99
                    ld        e,$ed                         ;[0678] 1e ed
                    ld        b,e                           ;[067a] 43
                    ld        (hl),c                        ;[067b] 71
                    ld        e,e                           ;[067c] 5b
                    rst       $28                           ;[067d] ef
                    jr        $0680                         ;[067e] 18 00
                    cp        $0d                           ;[0680] fe 0d
                    jr        z,$0689                       ;[0682] 28 05
                    cp        $3a                           ;[0684] fe 3a
                    jp        nz,$1912                      ;[0686] c2 12 19
                    call      $18a1                         ;[0689] cd a1 18
                    ld        bc,($5b71)                    ;[068c] ed 4b 71 5b
                    ld        a,b                           ;[0690] 78
                    or        c                             ;[0691] b1
                    jr        nz,$0698                      ;[0692] 20 04
                    call      $05ac                         ;[0694] cd ac 05
                    dec       h                             ;[0697] 25
                    ld        hl,$06b8                      ;[0698] 21 b8 06
                    ld        e,(hl)                        ;[069b] 5e
                    inc       hl                            ;[069c] 23
                    ld        d,(hl)                        ;[069d] 56
                    inc       hl                            ;[069e] 23
                    ex        de,hl                         ;[069f] eb
                    ld        a,h                           ;[06a0] 7c
                    cp        $25                           ;[06a1] fe 25
                    jr        nc,$06af                      ;[06a3] 30 0a
                    and       a                             ;[06a5] a7
                    sbc       hl,bc                         ;[06a6] ed 42
                    jr        nc,$06af                      ;[06a8] 30 05
                    ex        de,hl                         ;[06aa] eb
                    inc       hl                            ;[06ab] 23
                    inc       hl                            ;[06ac] 23
                    jr        $069b                         ;[06ad] 18 ec
                    ex        de,hl                         ;[06af] eb
                    ld        e,(hl)                        ;[06b0] 5e
                    inc       hl                            ;[06b1] 23
                    ld        d,(hl)                        ;[06b2] 56
                    ld        ($5b5f),de                    ;[06b3] ed 53 5f 5b
                    ret                                     ;[06b7] c9

                    ld        ($a500),a                     ;[06b8] 32 00 a5
                    ld        a,(bc)                        ;[06bb] 0a
                    ld        l,(hl)                        ;[06bc] 6e
                    nop                                     ;[06bd] 00
                    call      nc,$2c04                      ;[06be] d4 04 2c
                    ld        bc,$01c3                      ;[06c1] 01 c3 01
                    ld        e,b                           ;[06c4] 58
                    ld        (bc),a                        ;[06c5] 02
                    ret       po                            ;[06c6] e0
                    nop                                     ;[06c7] 00
                    or        b                             ;[06c8] b0
                    inc       b                             ;[06c9] 04
                    ld        l,(hl)                        ;[06ca] 6e
                    nop                                     ;[06cb] 00
                    ld        h,b                           ;[06cc] 60
                    add       hl,bc                         ;[06cd] 09
                    ld        (hl),$00                      ;[06ce] 36 00
                    ret       nz                            ;[06d0] c0
                    ld        (de),a                        ;[06d1] 12
                    add       hl,de                         ;[06d2] 19
                    nop                                     ;[06d3] 00
                    add       b                             ;[06d4] 80
                    dec       h                             ;[06d5] 25
                    dec       bc                            ;[06d6] 0b
                    nop                                     ;[06d7] 00
                    ld        hl,$5b61                      ;[06d8] 21 61 5b
                    ld        a,(hl)                        ;[06db] 7e
                    and       a                             ;[06dc] a7
                    jr        z,$06e5                       ;[06dd] 28 06
                    ld        (hl),$00                      ;[06df] 36 00
                    inc       hl                            ;[06e1] 23
                    ld        a,(hl)                        ;[06e2] 7e
                    scf                                     ;[06e3] 37
                    ret                                     ;[06e4] c9

                    call      $05d6                         ;[06e5] cd d6 05
                    di                                      ;[06e8] f3
                    exx                                     ;[06e9] d9
                    ld        de,($5b5f)                    ;[06ea] ed 5b 5f 5b
                    ld        hl,($5b5f)                    ;[06ee] 2a 5f 5b
                    srl       h                             ;[06f1] cb 3c
                    rr        l                             ;[06f3] cb 1d
                    or        a                             ;[06f5] b7
                    ld        b,$fa                         ;[06f6] 06 fa
                    exx                                     ;[06f8] d9
                    ld        c,$fd                         ;[06f9] 0e fd
                    ld        d,$ff                         ;[06fb] 16 ff
                    ld        e,$bf                         ;[06fd] 1e bf
                    ld        b,d                           ;[06ff] 42
                    ld        a,$0e                         ;[0700] 3e 0e
                    out       (c),a                         ;[0702] ed 79
                    in        a,(c)                         ;[0704] ed 78
                    or        $f0                           ;[0706] f6 f0
                    and       $fb                           ;[0708] e6 fb
                    ld        b,e                           ;[070a] 43
                    out       (c),a                         ;[070b] ed 79
                    ld        h,a                           ;[070d] 67
                    ld        b,d                           ;[070e] 42
                    in        a,(c)                         ;[070f] ed 78
                    and       $80                           ;[0711] e6 80
                    jr        z,$071e                       ;[0713] 28 09
                    exx                                     ;[0715] d9
                    dec       b                             ;[0716] 05
                    exx                                     ;[0717] d9
                    jr        nz,$070e                      ;[0718] 20 f4
                    xor       a                             ;[071a] af
                    push      af                            ;[071b] f5
                    jr        $0757                         ;[071c] 18 39
                    in        a,(c)                         ;[071e] ed 78
                    and       $80                           ;[0720] e6 80
                    jr        nz,$0715                      ;[0722] 20 f1
                    in        a,(c)                         ;[0724] ed 78
                    and       $80                           ;[0726] e6 80
                    jr        nz,$0715                      ;[0728] 20 eb
                    exx                                     ;[072a] d9
                    ld        bc,$fffd                      ;[072b] 01 fd ff
                    ld        a,$80                         ;[072e] 3e 80
                    ex        af,af'                        ;[0730] 08
                    add       hl,de                         ;[0731] 19
                    nop                                     ;[0732] 00
                    nop                                     ;[0733] 00
                    nop                                     ;[0734] 00
                    nop                                     ;[0735] 00
                    dec       hl                            ;[0736] 2b
                    ld        a,h                           ;[0737] 7c
                    or        l                             ;[0738] b5
                    jr        nz,$0736                      ;[0739] 20 fb
                    in        a,(c)                         ;[073b] ed 78
                    and       $80                           ;[073d] e6 80
                    jp        z,$074b                       ;[073f] ca 4b 07
                    ex        af,af'                        ;[0742] 08
                    scf                                     ;[0743] 37
                    rra                                     ;[0744] 1f
                    jr        c,$0754                       ;[0745] 38 0d
                    ex        af,af'                        ;[0747] 08
                    jp        $0731                         ;[0748] c3 31 07
                    ex        af,af'                        ;[074b] 08
                    or        a                             ;[074c] b7
                    rra                                     ;[074d] 1f
                    jr        c,$0754                       ;[074e] 38 04
                    ex        af,af'                        ;[0750] 08
                    jp        $0731                         ;[0751] c3 31 07
                    scf                                     ;[0754] 37
                    push      af                            ;[0755] f5
                    exx                                     ;[0756] d9
                    ld        a,h                           ;[0757] 7c
                    or        $04                           ;[0758] f6 04
                    ld        b,e                           ;[075a] 43
                    out       (c),a                         ;[075b] ed 79
                    exx                                     ;[075d] d9
                    ld        h,d                           ;[075e] 62
                    ld        l,e                           ;[075f] 6b
                    ld        bc,$0007                      ;[0760] 01 07 00
                    or        a                             ;[0763] b7
                    sbc       hl,bc                         ;[0764] ed 42
                    dec       hl                            ;[0766] 2b
                    ld        a,h                           ;[0767] 7c
                    or        l                             ;[0768] b5
                    jr        nz,$0766                      ;[0769] 20 fb
                    ld        bc,$fffd                      ;[076b] 01 fd ff
                    add       hl,de                         ;[076e] 19
                    add       hl,de                         ;[076f] 19
                    add       hl,de                         ;[0770] 19
                    in        a,(c)                         ;[0771] ed 78
                    and       $80                           ;[0773] e6 80
                    jr        z,$077f                       ;[0775] 28 08
                    dec       hl                            ;[0777] 2b
                    ld        a,h                           ;[0778] 7c
                    or        l                             ;[0779] b5
                    jr        nz,$0771                      ;[077a] 20 f5
                    pop       af                            ;[077c] f1
                    ei                                      ;[077d] fb
                    ret                                     ;[077e] c9

                    in        a,(c)                         ;[077f] ed 78
                    and       $80                           ;[0781] e6 80
                    jr        nz,$0771                      ;[0783] 20 ec
                    in        a,(c)                         ;[0785] ed 78
                    and       $80                           ;[0787] e6 80
                    jr        nz,$0771                      ;[0789] 20 e6
                    ld        h,d                           ;[078b] 62
                    ld        l,e                           ;[078c] 6b
                    ld        bc,$0002                      ;[078d] 01 02 00
                    srl       h                             ;[0790] cb 3c
                    rr        l                             ;[0792] cb 1d
                    or        a                             ;[0794] b7
                    sbc       hl,bc                         ;[0795] ed 42
                    ld        bc,$fffd                      ;[0797] 01 fd ff
                    ld        a,$80                         ;[079a] 3e 80
                    ex        af,af'                        ;[079c] 08
                    nop                                     ;[079d] 00
                    nop                                     ;[079e] 00
                    nop                                     ;[079f] 00
                    nop                                     ;[07a0] 00
                    add       hl,de                         ;[07a1] 19
                    dec       hl                            ;[07a2] 2b
                    ld        a,h                           ;[07a3] 7c
                    or        l                             ;[07a4] b5
                    jr        nz,$07a2                      ;[07a5] 20 fb
                    in        a,(c)                         ;[07a7] ed 78
                    and       $80                           ;[07a9] e6 80
                    jp        z,$07b7                       ;[07ab] ca b7 07
                    ex        af,af'                        ;[07ae] 08
                    scf                                     ;[07af] 37
                    rra                                     ;[07b0] 1f
                    jr        c,$07c0                       ;[07b1] 38 0d
                    ex        af,af'                        ;[07b3] 08
                    jp        $079d                         ;[07b4] c3 9d 07
                    ex        af,af'                        ;[07b7] 08
                    or        a                             ;[07b8] b7
                    rra                                     ;[07b9] 1f
                    jr        c,$07c0                       ;[07ba] 38 04
                    ex        af,af'                        ;[07bc] 08
                    jp        $079d                         ;[07bd] c3 9d 07
                    ld        hl,$5b61                      ;[07c0] 21 61 5b
                    ld        (hl),$01                      ;[07c3] 36 01
                    inc       hl                            ;[07c5] 23
                    ld        (hl),a                        ;[07c6] 77
                    pop       af                            ;[07c7] f1
                    ei                                      ;[07c8] fb
                    ret                                     ;[07c9] c9

                    push      af                            ;[07ca] f5
                    ld        a,($5b65)                     ;[07cb] 3a 65 5b
                    or        a                             ;[07ce] b7
                    jr        z,$07e0                       ;[07cf] 28 0f
                    dec       a                             ;[07d1] 3d
                    ld        ($5b65),a                     ;[07d2] 32 65 5b
                    jr        nz,$07db                      ;[07d5] 20 04
                    pop       af                            ;[07d7] f1
                    jp        $0872                         ;[07d8] c3 72 08
                    pop       af                            ;[07db] f1
                    ld        ($5c0f),a                     ;[07dc] 32 0f 5c
                    ret                                     ;[07df] c9

                    pop       af                            ;[07e0] f1
                    cp        $a3                           ;[07e1] fe a3
                    jr        c,$07f2                       ;[07e3] 38 0d
                    ld        hl,($5b5a)                    ;[07e5] 2a 5a 5b
                    push      hl                            ;[07e8] e5
                    rst       $28                           ;[07e9] ef
                    ld        d,d                           ;[07ea] 52
                    dec       bc                            ;[07eb] 0b
                    pop       hl                            ;[07ec] e1
                    ld        ($5b5a),hl                    ;[07ed] 22 5a 5b
                    scf                                     ;[07f0] 37
                    ret                                     ;[07f1] c9

                    ld        hl,$5c3b                      ;[07f2] 21 3b 5c
                    res       0,(hl)                        ;[07f5] cb 86
                    cp        $20                           ;[07f7] fe 20
                    jr        nz,$07fd                      ;[07f9] 20 02
                    set       0,(hl)                        ;[07fb] cb c6
                    cp        $7f                           ;[07fd] fe 7f
                    jr        c,$0803                       ;[07ff] 38 02
                    ld        a,$3f                         ;[0801] 3e 3f
                    cp        $20                           ;[0803] fe 20
                    jr        c,$081e                       ;[0805] 38 17
                    push      af                            ;[0807] f5
                    ld        hl,$5b63                      ;[0808] 21 63 5b
                    inc       (hl)                          ;[080b] 34
                    ld        a,($5b64)                     ;[080c] 3a 64 5b
                    cp        (hl)                          ;[080f] be
                    jr        nc,$081a                      ;[0810] 30 08
                    call      $0822                         ;[0812] cd 22 08
                    ld        a,$01                         ;[0815] 3e 01
                    ld        ($5b63),a                     ;[0817] 32 63 5b
                    pop       af                            ;[081a] f1
                    jp        $08a3                         ;[081b] c3 a3 08
                    cp        $0d                           ;[081e] fe 0d
                    jr        nz,$0830                      ;[0820] 20 0e
                    xor       a                             ;[0822] af
                    ld        ($5b63),a                     ;[0823] 32 63 5b
                    ld        a,$0d                         ;[0826] 3e 0d
                    call      $08a3                         ;[0828] cd a3 08
                    ld        a,$0a                         ;[082b] 3e 0a
                    jp        $08a3                         ;[082d] c3 a3 08
                    cp        $06                           ;[0830] fe 06
                    jr        nz,$0853                      ;[0832] 20 1f
                    ld        bc,($5b63)                    ;[0834] ed 4b 63 5b
                    ld        e,$00                         ;[0838] 1e 00
                    inc       e                             ;[083a] 1c
                    inc       c                             ;[083b] 0c
                    ld        a,c                           ;[083c] 79
                    cp        b                             ;[083d] b8
                    jr        z,$0848                       ;[083e] 28 08
                    sub       $08                           ;[0840] d6 08
                    jr        z,$0848                       ;[0842] 28 04
                    jr        nc,$0840                      ;[0844] 30 fa
                    jr        $083a                         ;[0846] 18 f2
                    push      de                            ;[0848] d5
                    ld        a,$20                         ;[0849] 3e 20
                    call      $07ca                         ;[084b] cd ca 07
                    pop       de                            ;[084e] d1
                    dec       e                             ;[084f] 1d
                    ret       z                             ;[0850] c8
                    jr        $0848                         ;[0851] 18 f5
                    cp        $16                           ;[0853] fe 16
                    jr        z,$0860                       ;[0855] 28 09
                    cp        $17                           ;[0857] fe 17
                    jr        z,$0860                       ;[0859] 28 05
                    cp        $10                           ;[085b] fe 10
                    ret       c                             ;[085d] d8
                    jr        $0869                         ;[085e] 18 09
                    ld        ($5c0e),a                     ;[0860] 32 0e 5c
                    ld        a,$02                         ;[0863] 3e 02
                    ld        ($5b65),a                     ;[0865] 32 65 5b
                    ret                                     ;[0868] c9

                    ld        ($5c0e),a                     ;[0869] 32 0e 5c
                    ld        a,$02                         ;[086c] 3e 02
                    ld        ($5b65),a                     ;[086e] 32 65 5b
                    ret                                     ;[0871] c9

                    ld        d,a                           ;[0872] 57
                    ld        a,($5c0e)                     ;[0873] 3a 0e 5c
                    cp        $16                           ;[0876] fe 16
                    jr        z,$0882                       ;[0878] 28 08
                    cp        $17                           ;[087a] fe 17
                    ccf                                     ;[087c] 3f
                    ret       nz                            ;[087d] c0
                    ld        a,($5c0f)                     ;[087e] 3a 0f 5c
                    ld        d,a                           ;[0881] 57
                    ld        a,($5b64)                     ;[0882] 3a 64 5b
                    cp        d                             ;[0885] ba
                    jr        z,$088a                       ;[0886] 28 02
                    jr        nc,$0890                      ;[0888] 30 06
                    ld        b,a                           ;[088a] 47
                    ld        a,d                           ;[088b] 7a
                    sub       b                             ;[088c] 90
                    ld        d,a                           ;[088d] 57
                    jr        $0882                         ;[088e] 18 f2
                    ld        a,d                           ;[0890] 7a
                    or        a                             ;[0891] b7
                    jp        z,$0822                       ;[0892] ca 22 08
                    ld        a,($5b63)                     ;[0895] 3a 63 5b
                    cp        d                             ;[0898] ba
                    ret       z                             ;[0899] c8
                    push      de                            ;[089a] d5
                    ld        a,$20                         ;[089b] 3e 20
                    call      $07ca                         ;[089d] cd ca 07
                    pop       de                            ;[08a0] d1
                    jr        $0895                         ;[08a1] 18 f2
                    push      af                            ;[08a3] f5
                    ld        c,$fd                         ;[08a4] 0e fd
                    ld        d,$ff                         ;[08a6] 16 ff
                    ld        e,$bf                         ;[08a8] 1e bf
                    ld        b,d                           ;[08aa] 42
                    ld        a,$0e                         ;[08ab] 3e 0e
                    out       (c),a                         ;[08ad] ed 79
                    call      $05d6                         ;[08af] cd d6 05
                    in        a,(c)                         ;[08b2] ed 78
                    and       $40                           ;[08b4] e6 40
                    jr        nz,$08af                      ;[08b6] 20 f7
                    ld        hl,($5b5f)                    ;[08b8] 2a 5f 5b
                    ld        de,$0002                      ;[08bb] 11 02 00
                    or        a                             ;[08be] b7
                    sbc       hl,de                         ;[08bf] ed 52
                    ex        de,hl                         ;[08c1] eb
                    pop       af                            ;[08c2] f1
                    cpl                                     ;[08c3] 2f
                    scf                                     ;[08c4] 37
                    ld        b,$0b                         ;[08c5] 06 0b
                    di                                      ;[08c7] f3
                    push      bc                            ;[08c8] c5
                    push      af                            ;[08c9] f5
                    ld        a,$fe                         ;[08ca] 3e fe
                    ld        h,d                           ;[08cc] 62
                    ld        l,e                           ;[08cd] 6b
                    ld        bc,$bffd                      ;[08ce] 01 fd bf
                    jp        nc,$08da                      ;[08d1] d2 da 08
                    and       $f7                           ;[08d4] e6 f7
                    out       (c),a                         ;[08d6] ed 79
                    jr        $08e0                         ;[08d8] 18 06
                    or        $08                           ;[08da] f6 08
                    out       (c),a                         ;[08dc] ed 79
                    jr        $08e0                         ;[08de] 18 00
                    dec       hl                            ;[08e0] 2b
                    ld        a,h                           ;[08e1] 7c
                    or        l                             ;[08e2] b5
                    jr        nz,$08e0                      ;[08e3] 20 fb
                    nop                                     ;[08e5] 00
                    nop                                     ;[08e6] 00
                    nop                                     ;[08e7] 00
                    pop       af                            ;[08e8] f1
                    pop       bc                            ;[08e9] c1
                    or        a                             ;[08ea] b7
                    rra                                     ;[08eb] 1f
                    djnz      $08c8                         ;[08ec] 10 da
                    ei                                      ;[08ee] fb
                    ret                                     ;[08ef] c9

                    ld        hl,$5b72                      ;[08f0] 21 72 5b
                    ld        (hl),$2b                      ;[08f3] 36 2b
                    ld        hl,$0979                      ;[08f5] 21 79 09
                    call      $095f                         ;[08f8] cd 5f 09
                    call      $0915                         ;[08fb] cd 15 09
                    ld        hl,$0980                      ;[08fe] 21 80 09
                    call      $095f                         ;[0901] cd 5f 09
                    ld        hl,$5b72                      ;[0904] 21 72 5b
                    xor       a                             ;[0907] af
                    cp        (hl)                          ;[0908] be
                    jr        z,$090e                       ;[0909] 28 03
                    dec       (hl)                          ;[090b] 35
                    jr        $08f5                         ;[090c] 18 e7
                    ld        hl,$0982                      ;[090e] 21 82 09
                    call      $095f                         ;[0911] cd 5f 09
                    ret                                     ;[0914] c9

                    ld        hl,$5b71                      ;[0915] 21 71 5b
                    ld        (hl),$ff                      ;[0918] 36 ff
                    call      $0926                         ;[091a] cd 26 09
                    ld        hl,$5b71                      ;[091d] 21 71 5b
                    xor       a                             ;[0920] af
                    cp        (hl)                          ;[0921] be
                    ret       z                             ;[0922] c8
                    dec       (hl)                          ;[0923] 35
                    jr        $091a                         ;[0924] 18 f4
                    ld        de,$c000                      ;[0926] 11 00 c0
                    ld        bc,($5b71)                    ;[0929] ed 4b 71 5b
                    scf                                     ;[092d] 37
                    rl        b                             ;[092e] cb 10
                    scf                                     ;[0930] 37
                    rl        b                             ;[0931] cb 10
                    ld        a,c                           ;[0933] 79
                    cpl                                     ;[0934] 2f
                    ld        c,a                           ;[0935] 4f
                    xor       a                             ;[0936] af
                    push      af                            ;[0937] f5
                    push      de                            ;[0938] d5
                    push      bc                            ;[0939] c5
                    call      $096d                         ;[093a] cd 6d 09
                    pop       bc                            ;[093d] c1
                    pop       de                            ;[093e] d1
                    ld        e,$00                         ;[093f] 1e 00
                    jr        z,$0944                       ;[0941] 28 01
                    ld        e,d                           ;[0943] 5a
                    pop       af                            ;[0944] f1
                    or        e                             ;[0945] b3
                    push      af                            ;[0946] f5
                    dec       b                             ;[0947] 05
                    srl       d                             ;[0948] cb 3a
                    srl       d                             ;[094a] cb 3a
                    push      de                            ;[094c] d5
                    push      bc                            ;[094d] c5
                    jr        nc,$093a                      ;[094e] 30 ea
                    pop       bc                            ;[0950] c1
                    pop       de                            ;[0951] d1
                    pop       af                            ;[0952] f1
                    ld        b,$03                         ;[0953] 06 03
                    push      bc                            ;[0955] c5
                    push      af                            ;[0956] f5
                    call      $08a3                         ;[0957] cd a3 08
                    pop       af                            ;[095a] f1
                    pop       bc                            ;[095b] c1
                    djnz      $0955                         ;[095c] 10 f7
                    ret                                     ;[095e] c9

                    ld        b,(hl)                        ;[095f] 46
                    inc       hl                            ;[0960] 23
                    ld        a,(hl)                        ;[0961] 7e
                    push      hl                            ;[0962] e5
                    push      bc                            ;[0963] c5
                    call      $08a3                         ;[0964] cd a3 08
                    pop       bc                            ;[0967] c1
                    pop       hl                            ;[0968] e1
                    inc       hl                            ;[0969] 23
                    djnz      $0961                         ;[096a] 10 f5
                    ret                                     ;[096c] c9

                    rst       $28                           ;[096d] ef
                    xor       d                             ;[096e] aa
                    ld        ($0447),hl                    ;[096f] 22 47 04
                    xor       a                             ;[0972] af
                    scf                                     ;[0973] 37
                    rra                                     ;[0974] 1f
                    djnz      $0974                         ;[0975] 10 fd
                    and       (hl)                          ;[0977] a6
                    ret                                     ;[0978] c9

                    ld        b,$1b                         ;[0979] 06 1b
                    ld        sp,$4c1b                      ;[097b] 31 1b 4c
                    nop                                     ;[097e] 00
                    inc       bc                            ;[097f] 03
                    ld        bc,$020a                      ;[0980] 01 0a 02
                    dec       de                            ;[0983] 1b
                    ld        ($c5f3),a                     ;[0984] 32 f3 c5
                    ld        de,$0037                      ;[0987] 11 37 00
                    ld        hl,$003c                      ;[098a] 21 3c 00
                    add       hl,de                         ;[098d] 19
                    djnz      $098d                         ;[098e] 10 fd
                    ld        c,l                           ;[0990] 4d
                    ld        b,h                           ;[0991] 44
                    rst       $28                           ;[0992] ef
                    jr        nc,$0995                      ;[0993] 30 00
                    di                                      ;[0995] f3
                    push      de                            ;[0996] d5
                    pop       iy                            ;[0997] fd e1
                    push      hl                            ;[0999] e5
                    pop       ix                            ;[099a] dd e1
                    ld        (iy+$10),$ff                  ;[099c] fd 36 10 ff
                    ld        bc,$ffc9                      ;[09a0] 01 c9 ff
                    add       ix,bc                         ;[09a3] dd 09
                    ld        (ix+$03),$3c                  ;[09a5] dd 36 03 3c
                    ld        (ix+$01),$ff                  ;[09a9] dd 36 01 ff
                    ld        (ix+$04),$0f                  ;[09ad] dd 36 04 0f
                    ld        (ix+$05),$05                  ;[09b1] dd 36 05 05
                    ld        (ix+$21),$00                  ;[09b5] dd 36 21 00
                    ld        (ix+$0a),$00                  ;[09b9] dd 36 0a 00
                    ld        (ix+$0b),$00                  ;[09bd] dd 36 0b 00
                    ld        (ix+$16),$ff                  ;[09c1] dd 36 16 ff
                    ld        (ix+$17),$00                  ;[09c5] dd 36 17 00
                    ld        (ix+$18),$00                  ;[09c9] dd 36 18 00
                    rst       $28                           ;[09cd] ef
                    pop       af                            ;[09ce] f1
                    dec       hl                            ;[09cf] 2b
                    di                                      ;[09d0] f3
                    ld        (ix+$06),e                    ;[09d1] dd 73 06
                    ld        (ix+$07),d                    ;[09d4] dd 72 07
                    ld        (ix+$0c),e                    ;[09d7] dd 73 0c
                    ld        (ix+$0d),d                    ;[09da] dd 72 0d
                    ex        de,hl                         ;[09dd] eb
                    add       hl,bc                         ;[09de] 09
                    ld        (ix+$08),l                    ;[09df] dd 75 08
                    ld        (ix+$09),h                    ;[09e2] dd 74 09
                    pop       bc                            ;[09e5] c1
                    push      bc                            ;[09e6] c5
                    dec       b                             ;[09e7] 05
                    ld        c,b                           ;[09e8] 48
                    ld        b,$00                         ;[09e9] 06 00
                    sla       c                             ;[09eb] cb 21
                    push      iy                            ;[09ed] fd e5
                    pop       hl                            ;[09ef] e1
                    add       hl,bc                         ;[09f0] 09
                    push      ix                            ;[09f1] dd e5
                    pop       bc                            ;[09f3] c1
                    ld        (hl),c                        ;[09f4] 71
                    inc       hl                            ;[09f5] 23
                    ld        (hl),b                        ;[09f6] 70
                    or        a                             ;[09f7] b7
                    rl        (iy+$10)                      ;[09f8] fd cb 10 16
                    pop       bc                            ;[09fc] c1
                    dec       b                             ;[09fd] 05
                    push      bc                            ;[09fe] c5
                    ld        (ix+$02),b                    ;[09ff] dd 70 02
                    jr        nz,$09a0                      ;[0a02] 20 9c
                    pop       bc                            ;[0a04] c1
                    ld        (iy+$27),$1a                  ;[0a05] fd 36 27 1a
                    ld        (iy+$28),$0b                  ;[0a09] fd 36 28 0b
                    push      iy                            ;[0a0d] fd e5
                    pop       hl                            ;[0a0f] e1
                    ld        bc,$002b                      ;[0a10] 01 2b 00
                    add       hl,bc                         ;[0a13] 09
                    ex        de,hl                         ;[0a14] eb
                    ld        hl,$0a31                      ;[0a15] 21 31 0a
                    ld        bc,$000d                      ;[0a18] 01 0d 00
                    ldir                                    ;[0a1b] ed b0
                    ld        d,$07                         ;[0a1d] 16 07
                    ld        e,$f8                         ;[0a1f] 1e f8
                    call      $0e7c                         ;[0a21] cd 7c 0e
                    ld        d,$0b                         ;[0a24] 16 0b
                    ld        e,$ff                         ;[0a26] 1e ff
                    call      $0e7c                         ;[0a28] cd 7c 0e
                    inc       d                             ;[0a2b] 14
                    call      $0e7c                         ;[0a2c] cd 7c 0e
                    jr        $0a7d                         ;[0a2f] 18 4c
                    rst       $28                           ;[0a31] ef
                    and       h                             ;[0a32] a4
                    ld        bc,$3405                      ;[0a33] 01 05 34
                    rst       $18                           ;[0a36] df
                    ld        (hl),l                        ;[0a37] 75
                    call      p,$7538                       ;[0a38] f4 38 75
                    dec       b                             ;[0a3b] 05
                    jr        c,$0a07                       ;[0a3c] 38 c9
                    ld        a,$7f                         ;[0a3e] 3e 7f
                    in        a,($fe)                       ;[0a40] db fe
                    rra                                     ;[0a42] 1f
                    ret       c                             ;[0a43] d8
                    ld        a,$fe                         ;[0a44] 3e fe
                    in        a,($fe)                       ;[0a46] db fe
                    rra                                     ;[0a48] 1f
                    ret                                     ;[0a49] c9

                    ld        bc,$0011                      ;[0a4a] 01 11 00
                    jr        $0a52                         ;[0a4d] 18 03
                    ld        bc,$0000                      ;[0a4f] 01 00 00
                    push      iy                            ;[0a52] fd e5
                    pop       hl                            ;[0a54] e1
                    add       hl,bc                         ;[0a55] 09
                    ld        (iy+$23),l                    ;[0a56] fd 75 23
                    ld        (iy+$24),h                    ;[0a59] fd 74 24
                    ld        a,(iy+$10)                    ;[0a5c] fd 7e 10
                    ld        (iy+$22),a                    ;[0a5f] fd 77 22
                    ld        (iy+$21),$01                  ;[0a62] fd 36 21 01
                    ret                                     ;[0a66] c9

                    ld        e,(hl)                        ;[0a67] 5e
                    inc       hl                            ;[0a68] 23
                    ld        d,(hl)                        ;[0a69] 56
                    push      de                            ;[0a6a] d5
                    pop       ix                            ;[0a6b] dd e1
                    ret                                     ;[0a6d] c9

                    ld        l,(iy+$23)                    ;[0a6e] fd 6e 23
                    ld        h,(iy+$24)                    ;[0a71] fd 66 24
                    inc       hl                            ;[0a74] 23
                    inc       hl                            ;[0a75] 23
                    ld        (iy+$23),l                    ;[0a76] fd 75 23
                    ld        (iy+$24),h                    ;[0a79] fd 74 24
                    ret                                     ;[0a7c] c9

                    call      $0a4f                         ;[0a7d] cd 4f 0a
                    rr        (iy+$22)                      ;[0a80] fd cb 22 1e
                    jr        c,$0a8c                       ;[0a84] 38 06
                    call      $0a67                         ;[0a86] cd 67 0a
                    call      $0b5c                         ;[0a89] cd 5c 0b
                    sla       (iy+$21)                      ;[0a8c] fd cb 21 26
                    jr        c,$0a97                       ;[0a90] 38 05
                    call      $0a6e                         ;[0a92] cd 6e 0a
                    jr        $0a80                         ;[0a95] 18 e9
                    call      $0f91                         ;[0a97] cd 91 0f
                    push      de                            ;[0a9a] d5
                    call      $0f42                         ;[0a9b] cd 42 0f
                    pop       de                            ;[0a9e] d1
                    ld        a,(iy+$10)                    ;[0a9f] fd 7e 10
                    cp        $ff                           ;[0aa2] fe ff
                    jr        nz,$0aab                      ;[0aa4] 20 05
                    call      $0e93                         ;[0aa6] cd 93 0e
                    ei                                      ;[0aa9] fb
                    ret                                     ;[0aaa] c9

                    dec       de                            ;[0aab] 1b
                    call      $0f76                         ;[0aac] cd 76 0f
                    call      $0fc1                         ;[0aaf] cd c1 0f
                    call      $0f91                         ;[0ab2] cd 91 0f
                    jr        $0a9f                         ;[0ab5] 18 e8
                    ld        c,b                           ;[0ab7] 48
                    ld        e,d                           ;[0ab8] 5a
                    ld        e,c                           ;[0ab9] 59
                    ld        e,b                           ;[0aba] 58
                    ld        d,a                           ;[0abb] 57
                    ld        d,l                           ;[0abc] 55
                    ld        d,(hl)                        ;[0abd] 56
                    ld        c,l                           ;[0abe] 4d
                    ld        d,h                           ;[0abf] 54
                    add       hl,hl                         ;[0ac0] 29
                    jr        z,$0b11                       ;[0ac1] 28 4e
                    ld        c,a                           ;[0ac3] 4f
                    ld        hl,$e3cd                      ;[0ac4] 21 cd e3
                    ld        c,$d8                         ;[0ac7] 0e d8
                    inc       (ix+$06)                      ;[0ac9] dd 34 06
                    ret       nz                            ;[0acc] c0
                    inc       (ix+$07)                      ;[0acd] dd 34 07
                    ret                                     ;[0ad0] c9

                    push      hl                            ;[0ad1] e5
                    ld        c,$00                         ;[0ad2] 0e 00
                    call      $0ac5                         ;[0ad4] cd c5 0a
                    jr        c,$0ae1                       ;[0ad7] 38 08
                    cp        $26                           ;[0ad9] fe 26
                    jr        nz,$0aec                      ;[0adb] 20 0f
                    ld        a,$80                         ;[0add] 3e 80
                    pop       hl                            ;[0adf] e1
                    ret                                     ;[0ae0] c9

                    ld        a,(iy+$21)                    ;[0ae1] fd 7e 21
                    or        (iy+$10)                      ;[0ae4] fd b6 10
                    ld        (iy+$10),a                    ;[0ae7] fd 77 10
                    jr        $0adf                         ;[0aea] 18 f3
                    cp        $23                           ;[0aec] fe 23
                    jr        nz,$0af3                      ;[0aee] 20 03
                    inc       c                             ;[0af0] 0c
                    jr        $0ad4                         ;[0af1] 18 e1
                    cp        $24                           ;[0af3] fe 24
                    jr        nz,$0afa                      ;[0af5] 20 03
                    dec       c                             ;[0af7] 0d
                    jr        $0ad4                         ;[0af8] 18 da
                    bit       5,a                           ;[0afa] cb 6f
                    jr        nz,$0b04                      ;[0afc] 20 06
                    push      af                            ;[0afe] f5
                    ld        a,$0c                         ;[0aff] 3e 0c
                    add       c                             ;[0b01] 81
                    ld        c,a                           ;[0b02] 4f
                    pop       af                            ;[0b03] f1
                    and       $df                           ;[0b04] e6 df
                    sub       $41                           ;[0b06] d6 41
                    jp        c,$0f22                       ;[0b08] da 22 0f
                    cp        $07                           ;[0b0b] fe 07
                    jp        nc,$0f22                      ;[0b0d] d2 22 0f
                    push      bc                            ;[0b10] c5
                    ld        b,$00                         ;[0b11] 06 00
                    ld        c,a                           ;[0b13] 4f
                    ld        hl,$0df9                      ;[0b14] 21 f9 0d
                    add       hl,bc                         ;[0b17] 09
                    ld        a,(hl)                        ;[0b18] 7e
                    pop       bc                            ;[0b19] c1
                    add       c                             ;[0b1a] 81
                    pop       hl                            ;[0b1b] e1
                    ret                                     ;[0b1c] c9

                    push      hl                            ;[0b1d] e5
                    push      de                            ;[0b1e] d5
                    ld        l,(ix+$06)                    ;[0b1f] dd 6e 06
                    ld        h,(ix+$07)                    ;[0b22] dd 66 07
                    ld        de,$0000                      ;[0b25] 11 00 00
                    ld        a,(hl)                        ;[0b28] 7e
                    cp        $30                           ;[0b29] fe 30
                    jr        c,$0b45                       ;[0b2b] 38 18
                    cp        $3a                           ;[0b2d] fe 3a
                    jr        nc,$0b45                      ;[0b2f] 30 14
                    inc       hl                            ;[0b31] 23
                    push      hl                            ;[0b32] e5
                    call      $0b50                         ;[0b33] cd 50 0b
                    sub       $30                           ;[0b36] d6 30
                    ld        h,$00                         ;[0b38] 26 00
                    ld        l,a                           ;[0b3a] 6f
                    add       hl,de                         ;[0b3b] 19
                    jr        c,$0b42                       ;[0b3c] 38 04
                    ex        de,hl                         ;[0b3e] eb
                    pop       hl                            ;[0b3f] e1
                    jr        $0b28                         ;[0b40] 18 e6
                    jp        $0f1a                         ;[0b42] c3 1a 0f
                    ld        (ix+$06),l                    ;[0b45] dd 75 06
                    ld        (ix+$07),h                    ;[0b48] dd 74 07
                    push      de                            ;[0b4b] d5
                    pop       bc                            ;[0b4c] c1
                    pop       de                            ;[0b4d] d1
                    pop       hl                            ;[0b4e] e1
                    ret                                     ;[0b4f] c9

                    ld        hl,$0000                      ;[0b50] 21 00 00
                    ld        b,$0a                         ;[0b53] 06 0a
                    add       hl,de                         ;[0b55] 19
                    jr        c,$0b42                       ;[0b56] 38 ea
                    djnz      $0b55                         ;[0b58] 10 fb
                    ex        de,hl                         ;[0b5a] eb
                    ret                                     ;[0b5b] c9

                    call      $0a3e                         ;[0b5c] cd 3e 0a
                    jr        c,$0b69                       ;[0b5f] 38 08
                    call      $0e93                         ;[0b61] cd 93 0e
                    ei                                      ;[0b64] fb
                    call      $05ac                         ;[0b65] cd ac 05
                    inc       d                             ;[0b68] 14
                    call      $0ac5                         ;[0b69] cd c5 0a
                    jp        c,$0da2                       ;[0b6c] da a2 0d
                    call      $0df0                         ;[0b6f] cd f0 0d
                    ld        b,$00                         ;[0b72] 06 00
                    sla       c                             ;[0b74] cb 21
                    ld        hl,$0dca                      ;[0b76] 21 ca 0d
                    add       hl,bc                         ;[0b79] 09
                    ld        e,(hl)                        ;[0b7a] 5e
                    inc       hl                            ;[0b7b] 23
                    ld        d,(hl)                        ;[0b7c] 56
                    ex        de,hl                         ;[0b7d] eb
                    call      $0b84                         ;[0b7e] cd 84 0b
                    jr        $0b5c                         ;[0b81] 18 d9
                    ret                                     ;[0b83] c9

                    jp        (hl)                          ;[0b84] e9
                    call      $0ac5                         ;[0b85] cd c5 0a
                    jp        c,$0da1                       ;[0b88] da a1 0d
                    cp        $21                           ;[0b8b] fe 21
                    ret       z                             ;[0b8d] c8
                    jr        $0b85                         ;[0b8e] 18 f5
                    call      $0b1d                         ;[0b90] cd 1d 0b
                    ld        a,c                           ;[0b93] 79
                    cp        $09                           ;[0b94] fe 09
                    jp        nc,$0f12                      ;[0b96] d2 12 0f
                    sla       a                             ;[0b99] cb 27
                    sla       a                             ;[0b9b] cb 27
                    ld        b,a                           ;[0b9d] 47
                    sla       a                             ;[0b9e] cb 27
                    add       b                             ;[0ba0] 80
                    ld        (ix+$03),a                    ;[0ba1] dd 77 03
                    ret                                     ;[0ba4] c9

                    ret                                     ;[0ba5] c9

                    ld        a,(ix+$0b)                    ;[0ba6] dd 7e 0b
                    inc       a                             ;[0ba9] 3c
                    cp        $05                           ;[0baa] fe 05
                    jp        z,$0f2a                       ;[0bac] ca 2a 0f
                    ld        (ix+$0b),a                    ;[0baf] dd 77 0b
                    ld        de,$000c                      ;[0bb2] 11 0c 00
                    call      $0c27                         ;[0bb5] cd 27 0c
                    ld        a,(ix+$06)                    ;[0bb8] dd 7e 06
                    ld        (hl),a                        ;[0bbb] 77
                    inc       hl                            ;[0bbc] 23
                    ld        a,(ix+$07)                    ;[0bbd] dd 7e 07
                    ld        (hl),a                        ;[0bc0] 77
                    ret                                     ;[0bc1] c9

                    ld        a,(ix+$16)                    ;[0bc2] dd 7e 16
                    ld        de,$0017                      ;[0bc5] 11 17 00
                    or        a                             ;[0bc8] b7
                    jp        m,$0bf0                       ;[0bc9] fa f0 0b
                    call      $0c27                         ;[0bcc] cd 27 0c
                    ld        a,(ix+$06)                    ;[0bcf] dd 7e 06
                    cp        (hl)                          ;[0bd2] be
                    jr        nz,$0bf0                      ;[0bd3] 20 1b
                    inc       hl                            ;[0bd5] 23
                    ld        a,(ix+$07)                    ;[0bd6] dd 7e 07
                    cp        (hl)                          ;[0bd9] be
                    jr        nz,$0bf0                      ;[0bda] 20 14
                    dec       (ix+$16)                      ;[0bdc] dd 35 16
                    ld        a,(ix+$16)                    ;[0bdf] dd 7e 16
                    or        a                             ;[0be2] b7
                    ret       p                             ;[0be3] f0
                    bit       0,(ix+$0a)                    ;[0be4] dd cb 0a 46
                    ret       z                             ;[0be8] c8
                    ld        (ix+$16),$00                  ;[0be9] dd 36 16 00
                    xor       a                             ;[0bed] af
                    jr        $0c0b                         ;[0bee] 18 1b
                    ld        a,(ix+$16)                    ;[0bf0] dd 7e 16
                    inc       a                             ;[0bf3] 3c
                    cp        $05                           ;[0bf4] fe 05
                    jp        z,$0f2a                       ;[0bf6] ca 2a 0f
                    ld        (ix+$16),a                    ;[0bf9] dd 77 16
                    call      $0c27                         ;[0bfc] cd 27 0c
                    ld        a,(ix+$06)                    ;[0bff] dd 7e 06
                    ld        (hl),a                        ;[0c02] 77
                    inc       hl                            ;[0c03] 23
                    ld        a,(ix+$07)                    ;[0c04] dd 7e 07
                    ld        (hl),a                        ;[0c07] 77
                    ld        a,(ix+$0b)                    ;[0c08] dd 7e 0b
                    ld        de,$000c                      ;[0c0b] 11 0c 00
                    call      $0c27                         ;[0c0e] cd 27 0c
                    ld        a,(hl)                        ;[0c11] 7e
                    ld        (ix+$06),a                    ;[0c12] dd 77 06
                    inc       hl                            ;[0c15] 23
                    ld        a,(hl)                        ;[0c16] 7e
                    ld        (ix+$07),a                    ;[0c17] dd 77 07
                    dec       (ix+$0b)                      ;[0c1a] dd 35 0b
                    ret       p                             ;[0c1d] f0
                    ld        (ix+$0b),$00                  ;[0c1e] dd 36 0b 00
                    set       0,(ix+$0a)                    ;[0c22] dd cb 0a c6
                    ret                                     ;[0c26] c9

                    push      ix                            ;[0c27] dd e5
                    pop       hl                            ;[0c29] e1
                    add       hl,de                         ;[0c2a] 19
                    ld        b,$00                         ;[0c2b] 06 00
                    ld        c,a                           ;[0c2d] 4f
                    sla       c                             ;[0c2e] cb 21
                    add       hl,bc                         ;[0c30] 09
                    ret                                     ;[0c31] c9

                    call      $0b1d                         ;[0c32] cd 1d 0b
                    ld        a,b                           ;[0c35] 78
                    or        a                             ;[0c36] b7
                    jp        nz,$0f12                      ;[0c37] c2 12 0f
                    ld        a,c                           ;[0c3a] 79
                    cp        $3c                           ;[0c3b] fe 3c
                    jp        c,$0f12                       ;[0c3d] da 12 0f
                    cp        $f1                           ;[0c40] fe f1
                    jp        nc,$0f12                      ;[0c42] d2 12 0f
                    ld        a,(ix+$02)                    ;[0c45] dd 7e 02
                    or        a                             ;[0c48] b7
                    ret       nz                            ;[0c49] c0
                    ld        b,$00                         ;[0c4a] 06 00
                    push      bc                            ;[0c4c] c5
                    pop       hl                            ;[0c4d] e1
                    add       hl,hl                         ;[0c4e] 29
                    add       hl,hl                         ;[0c4f] 29
                    push      hl                            ;[0c50] e5
                    pop       bc                            ;[0c51] c1
                    push      iy                            ;[0c52] fd e5
                    rst       $28                           ;[0c54] ef
                    dec       hl                            ;[0c55] 2b
                    dec       l                             ;[0c56] 2d
                    di                                      ;[0c57] f3
                    pop       iy                            ;[0c58] fd e1
                    push      iy                            ;[0c5a] fd e5
                    push      iy                            ;[0c5c] fd e5
                    pop       hl                            ;[0c5e] e1
                    ld        bc,$002b                      ;[0c5f] 01 2b 00
                    add       hl,bc                         ;[0c62] 09
                    ld        iy,$5c3a                      ;[0c63] fd 21 3a 5c
                    push      hl                            ;[0c67] e5
                    ld        hl,$0c76                      ;[0c68] 21 76 0c
                    ld        ($5b5a),hl                    ;[0c6b] 22 5a 5b
                    ld        hl,$5b14                      ;[0c6e] 21 14 5b
                    ex        (sp),hl                       ;[0c71] e3
                    push      hl                            ;[0c72] e5
                    jp        $5b00                         ;[0c73] c3 00 5b
                    di                                      ;[0c76] f3
                    rst       $28                           ;[0c77] ef
                    and       d                             ;[0c78] a2
                    dec       l                             ;[0c79] 2d
                    di                                      ;[0c7a] f3
                    pop       iy                            ;[0c7b] fd e1
                    ld        (iy+$27),c                    ;[0c7d] fd 71 27
                    ld        (iy+$28),b                    ;[0c80] fd 70 28
                    ret                                     ;[0c83] c9

                    call      $0b1d                         ;[0c84] cd 1d 0b
                    ld        a,c                           ;[0c87] 79
                    cp        $40                           ;[0c88] fe 40
                    jp        nc,$0f12                      ;[0c8a] d2 12 0f
                    cpl                                     ;[0c8d] 2f
                    ld        e,a                           ;[0c8e] 5f
                    ld        d,$07                         ;[0c8f] 16 07
                    call      $0e7c                         ;[0c91] cd 7c 0e
                    ret                                     ;[0c94] c9

                    call      $0b1d                         ;[0c95] cd 1d 0b
                    ld        a,c                           ;[0c98] 79
                    cp        $10                           ;[0c99] fe 10
                    jp        nc,$0f12                      ;[0c9b] d2 12 0f
                    ld        (ix+$04),a                    ;[0c9e] dd 77 04
                    ld        e,(ix+$02)                    ;[0ca1] dd 5e 02
                    ld        a,$08                         ;[0ca4] 3e 08
                    add       e                             ;[0ca6] 83
                    ld        d,a                           ;[0ca7] 57
                    ld        e,c                           ;[0ca8] 59
                    call      $0e7c                         ;[0ca9] cd 7c 0e
                    ret                                     ;[0cac] c9

                    ld        e,(ix+$02)                    ;[0cad] dd 5e 02
                    ld        a,$08                         ;[0cb0] 3e 08
                    add       e                             ;[0cb2] 83
                    ld        d,a                           ;[0cb3] 57
                    ld        e,$1f                         ;[0cb4] 1e 1f
                    ld        (ix+$04),e                    ;[0cb6] dd 73 04
                    ret                                     ;[0cb9] c9

                    call      $0b1d                         ;[0cba] cd 1d 0b
                    ld        a,c                           ;[0cbd] 79
                    cp        $08                           ;[0cbe] fe 08
                    jp        nc,$0f12                      ;[0cc0] d2 12 0f
                    ld        b,$00                         ;[0cc3] 06 00
                    ld        hl,$0de8                      ;[0cc5] 21 e8 0d
                    add       hl,bc                         ;[0cc8] 09
                    ld        a,(hl)                        ;[0cc9] 7e
                    ld        (iy+$29),a                    ;[0cca] fd 77 29
                    ret                                     ;[0ccd] c9

                    call      $0b1d                         ;[0cce] cd 1d 0b
                    ld        d,$0b                         ;[0cd1] 16 0b
                    ld        e,c                           ;[0cd3] 59
                    call      $0e7c                         ;[0cd4] cd 7c 0e
                    inc       d                             ;[0cd7] 14
                    ld        e,b                           ;[0cd8] 58
                    call      $0e7c                         ;[0cd9] cd 7c 0e
                    ret                                     ;[0cdc] c9

                    call      $0b1d                         ;[0cdd] cd 1d 0b
                    ld        a,c                           ;[0ce0] 79
                    dec       a                             ;[0ce1] 3d
                    jp        m,$0f12                       ;[0ce2] fa 12 0f
                    cp        $10                           ;[0ce5] fe 10
                    jp        nc,$0f12                      ;[0ce7] d2 12 0f
                    ld        (ix+$01),a                    ;[0cea] dd 77 01
                    ret                                     ;[0ced] c9

                    call      $0b1d                         ;[0cee] cd 1d 0b
                    ld        a,c                           ;[0cf1] 79
                    call      $11a3                         ;[0cf2] cd a3 11
                    ret                                     ;[0cf5] c9

                    ld        (iy+$10),$ff                  ;[0cf6] fd 36 10 ff
                    ret                                     ;[0cfa] c9

                    call      $0e19                         ;[0cfb] cd 19 0e
                    jp        c,$0d81                       ;[0cfe] da 81 0d
                    call      $0dac                         ;[0d01] cd ac 0d
                    call      $0db4                         ;[0d04] cd b4 0d
                    xor       a                             ;[0d07] af
                    ld        (ix+$21),a                    ;[0d08] dd 77 21
                    call      $0ec8                         ;[0d0b] cd c8 0e
                    call      $0b1d                         ;[0d0e] cd 1d 0b
                    ld        a,c                           ;[0d11] 79
                    or        a                             ;[0d12] b7
                    jp        z,$0f12                       ;[0d13] ca 12 0f
                    cp        $0d                           ;[0d16] fe 0d
                    jp        nc,$0f12                      ;[0d18] d2 12 0f
                    cp        $0a                           ;[0d1b] fe 0a
                    jr        c,$0d32                       ;[0d1d] 38 13
                    call      $0e00                         ;[0d1f] cd 00 0e
                    call      $0d74                         ;[0d22] cd 74 0d
                    ld        (hl),e                        ;[0d25] 73
                    inc       hl                            ;[0d26] 23
                    ld        (hl),d                        ;[0d27] 72
                    call      $0d74                         ;[0d28] cd 74 0d
                    inc       hl                            ;[0d2b] 23
                    ld        (hl),e                        ;[0d2c] 73
                    inc       hl                            ;[0d2d] 23
                    ld        (hl),d                        ;[0d2e] 72
                    inc       hl                            ;[0d2f] 23
                    jr        $0d38                         ;[0d30] 18 06
                    ld        (ix+$05),c                    ;[0d32] dd 71 05
                    call      $0e00                         ;[0d35] cd 00 0e
                    call      $0d74                         ;[0d38] cd 74 0d
                    call      $0ee3                         ;[0d3b] cd e3 0e
                    cp        $5f                           ;[0d3e] fe 5f
                    jr        nz,$0d6e                      ;[0d40] 20 2c
                    call      $0ac5                         ;[0d42] cd c5 0a
                    call      $0b1d                         ;[0d45] cd 1d 0b
                    ld        a,c                           ;[0d48] 79
                    cp        $0a                           ;[0d49] fe 0a
                    jr        c,$0d5f                       ;[0d4b] 38 12
                    push      hl                            ;[0d4d] e5
                    push      de                            ;[0d4e] d5
                    call      $0e00                         ;[0d4f] cd 00 0e
                    pop       hl                            ;[0d52] e1
                    add       hl,de                         ;[0d53] 19
                    ld        c,e                           ;[0d54] 4b
                    ld        b,d                           ;[0d55] 42
                    ex        de,hl                         ;[0d56] eb
                    pop       hl                            ;[0d57] e1
                    ld        (hl),e                        ;[0d58] 73
                    inc       hl                            ;[0d59] 23
                    ld        (hl),d                        ;[0d5a] 72
                    ld        e,c                           ;[0d5b] 59
                    ld        d,b                           ;[0d5c] 50
                    jr        $0d28                         ;[0d5d] 18 c9
                    ld        (ix+$05),c                    ;[0d5f] dd 71 05
                    push      hl                            ;[0d62] e5
                    push      de                            ;[0d63] d5
                    call      $0e00                         ;[0d64] cd 00 0e
                    pop       hl                            ;[0d67] e1
                    add       hl,de                         ;[0d68] 19
                    ex        de,hl                         ;[0d69] eb
                    pop       hl                            ;[0d6a] e1
                    jp        $0d3b                         ;[0d6b] c3 3b 0d
                    ld        (hl),e                        ;[0d6e] 73
                    inc       hl                            ;[0d6f] 23
                    ld        (hl),d                        ;[0d70] 72
                    jp        $0d9c                         ;[0d71] c3 9c 0d
                    ld        a,(ix+$21)                    ;[0d74] dd 7e 21
                    inc       a                             ;[0d77] 3c
                    cp        $0b                           ;[0d78] fe 0b
                    jp        z,$0f3a                       ;[0d7a] ca 3a 0f
                    ld        (ix+$21),a                    ;[0d7d] dd 77 21
                    ret                                     ;[0d80] c9

                    call      $0ec8                         ;[0d81] cd c8 0e
                    ld        (ix+$21),$01                  ;[0d84] dd 36 21 01
                    call      $0dac                         ;[0d88] cd ac 0d
                    call      $0db4                         ;[0d8b] cd b4 0d
                    ld        c,(ix+$05)                    ;[0d8e] dd 4e 05
                    push      hl                            ;[0d91] e5
                    call      $0e00                         ;[0d92] cd 00 0e
                    pop       hl                            ;[0d95] e1
                    ld        (hl),e                        ;[0d96] 73
                    inc       hl                            ;[0d97] 23
                    ld        (hl),d                        ;[0d98] 72
                    jp        $0d9c                         ;[0d99] c3 9c 0d
                    pop       hl                            ;[0d9c] e1
                    inc       hl                            ;[0d9d] 23
                    inc       hl                            ;[0d9e] 23
                    push      hl                            ;[0d9f] e5
                    ret                                     ;[0da0] c9

                    pop       hl                            ;[0da1] e1
                    ld        a,(iy+$21)                    ;[0da2] fd 7e 21
                    or        (iy+$10)                      ;[0da5] fd b6 10
                    ld        (iy+$10),a                    ;[0da8] fd 77 10
                    ret                                     ;[0dab] c9

                    push      ix                            ;[0dac] dd e5
                    pop       hl                            ;[0dae] e1
                    ld        bc,$0022                      ;[0daf] 01 22 00
                    add       hl,bc                         ;[0db2] 09
                    ret                                     ;[0db3] c9

                    push      hl                            ;[0db4] e5
                    push      iy                            ;[0db5] fd e5
                    pop       hl                            ;[0db7] e1
                    ld        bc,$0011                      ;[0db8] 01 11 00
                    add       hl,bc                         ;[0dbb] 09
                    ld        b,$00                         ;[0dbc] 06 00
                    ld        c,(ix+$02)                    ;[0dbe] dd 4e 02
                    sla       c                             ;[0dc1] cb 21
                    add       hl,bc                         ;[0dc3] 09
                    pop       de                            ;[0dc4] d1
                    ld        (hl),e                        ;[0dc5] 73
                    inc       hl                            ;[0dc6] 23
                    ld        (hl),d                        ;[0dc7] 72
                    ex        de,hl                         ;[0dc8] eb
                    ret                                     ;[0dc9] c9

                    ei                                      ;[0dca] fb
                    inc       c                             ;[0dcb] 0c
                    add       l                             ;[0dcc] 85
                    dec       bc                            ;[0dcd] 0b
                    sub       b                             ;[0dce] 90
                    dec       bc                            ;[0dcf] 0b
                    and       l                             ;[0dd0] a5
                    dec       bc                            ;[0dd1] 0b
                    and       (hl)                          ;[0dd2] a6
                    dec       bc                            ;[0dd3] 0b
                    jp        nz,$320b                      ;[0dd4] c2 0b 32
                    inc       c                             ;[0dd7] 0c
                    add       h                             ;[0dd8] 84
                    inc       c                             ;[0dd9] 0c
                    sub       l                             ;[0dda] 95
                    inc       c                             ;[0ddb] 0c
                    xor       l                             ;[0ddc] ad
                    inc       c                             ;[0ddd] 0c
                    cp        d                             ;[0dde] ba
                    inc       c                             ;[0ddf] 0c
                    adc       $0c                           ;[0de0] ce 0c
                    inc       c                             ;[0de2] dd 0c
                    xor       $0c                           ;[0de4] ee 0c
                    or        $0c                           ;[0de6] f6 0c
                    nop                                     ;[0de8] 00
                    inc       b                             ;[0de9] 04
                    dec       bc                            ;[0dea] 0b
                    dec       c                             ;[0deb] 0d
                    ex        af,af'                        ;[0dec] 08
                    inc       c                             ;[0ded] 0c
                    ld        c,$0a                         ;[0dee] 0e 0a
                    ld        bc,$000f                      ;[0df0] 01 0f 00
                    ld        hl,$0ab7                      ;[0df3] 21 b7 0a
                    cpir                                    ;[0df6] ed b1
                    ret                                     ;[0df8] c9

                    add       hl,bc                         ;[0df9] 09
                    dec       bc                            ;[0dfa] 0b
                    nop                                     ;[0dfb] 00
                    ld        (bc),a                        ;[0dfc] 02
                    inc       b                             ;[0dfd] 04
                    dec       b                             ;[0dfe] 05
                    rlca                                    ;[0dff] 07
                    push      hl                            ;[0e00] e5
                    ld        b,$00                         ;[0e01] 06 00
                    ld        hl,$0e0c                      ;[0e03] 21 0c 0e
                    add       hl,bc                         ;[0e06] 09
                    ld        d,$00                         ;[0e07] 16 00
                    ld        e,(hl)                        ;[0e09] 5e
                    pop       hl                            ;[0e0a] e1
                    ret                                     ;[0e0b] c9

                    add       b                             ;[0e0c] 80
                    ld        b,$09                         ;[0e0d] 06 09
                    inc       c                             ;[0e0f] 0c
                    ld        (de),a                        ;[0e10] 12
                    jr        $0e37                         ;[0e11] 18 24
                    jr        nc,$0e5d                      ;[0e13] 30 48
                    ld        h,b                           ;[0e15] 60
                    inc       b                             ;[0e16] 04
                    ex        af,af'                        ;[0e17] 08
                    djnz      $0e18                         ;[0e18] 10 fe
                    jr        nc,$0df4                      ;[0e1a] 30 d8
                    cp        $3a                           ;[0e1c] fe 3a
                    ccf                                     ;[0e1e] 3f
                    ret                                     ;[0e1f] c9

                    ld        c,a                           ;[0e20] 4f
                    ld        a,(ix+$03)                    ;[0e21] dd 7e 03
                    add       c                             ;[0e24] 81
                    cp        $80                           ;[0e25] fe 80
                    jp        nc,$0f32                      ;[0e27] d2 32 0f
                    ld        c,a                           ;[0e2a] 4f
                    ld        a,(ix+$02)                    ;[0e2b] dd 7e 02
                    or        a                             ;[0e2e] b7
                    jr        nz,$0e3f                      ;[0e2f] 20 0e
                    ld        a,c                           ;[0e31] 79
                    cpl                                     ;[0e32] 2f
                    and       $7f                           ;[0e33] e6 7f
                    srl       a                             ;[0e35] cb 3f
                    srl       a                             ;[0e37] cb 3f
                    ld        d,$06                         ;[0e39] 16 06
                    ld        e,a                           ;[0e3b] 5f
                    call      $0e7c                         ;[0e3c] cd 7c 0e
                    ld        (ix+$00),c                    ;[0e3f] dd 71 00
                    ld        a,(ix+$02)                    ;[0e42] dd 7e 02
                    cp        $03                           ;[0e45] fe 03
                    ret       nc                            ;[0e47] d0
                    ld        hl,$1096                      ;[0e48] 21 96 10
                    ld        b,$00                         ;[0e4b] 06 00
                    ld        a,c                           ;[0e4d] 79
                    sub       $15                           ;[0e4e] d6 15
                    jr        nc,$0e57                      ;[0e50] 30 05
                    ld        de,$0fbf                      ;[0e52] 11 bf 0f
                    jr        $0e5e                         ;[0e55] 18 07
                    ld        c,a                           ;[0e57] 4f
                    sla       c                             ;[0e58] cb 21
                    add       hl,bc                         ;[0e5a] 09
                    ld        e,(hl)                        ;[0e5b] 5e
                    inc       hl                            ;[0e5c] 23
                    ld        d,(hl)                        ;[0e5d] 56
                    ex        de,hl                         ;[0e5e] eb
                    ld        d,(ix+$02)                    ;[0e5f] dd 56 02
                    sla       d                             ;[0e62] cb 22
                    ld        e,l                           ;[0e64] 5d
                    call      $0e7c                         ;[0e65] cd 7c 0e
                    inc       d                             ;[0e68] 14
                    ld        e,h                           ;[0e69] 5c
                    call      $0e7c                         ;[0e6a] cd 7c 0e
                    bit       4,(ix+$04)                    ;[0e6d] dd cb 04 66
                    ret       z                             ;[0e71] c8
                    ld        d,$0d                         ;[0e72] 16 0d
                    ld        a,(iy+$29)                    ;[0e74] fd 7e 29
                    ld        e,a                           ;[0e77] 5f
                    call      $0e7c                         ;[0e78] cd 7c 0e
                    ret                                     ;[0e7b] c9

                    push      bc                            ;[0e7c] c5
                    ld        bc,$fffd                      ;[0e7d] 01 fd ff
                    out       (c),d                         ;[0e80] ed 51
                    ld        bc,$bffd                      ;[0e82] 01 fd bf
                    out       (c),e                         ;[0e85] ed 59
                    pop       bc                            ;[0e87] c1
                    ret                                     ;[0e88] c9

                    push      bc                            ;[0e89] c5
                    ld        bc,$fffd                      ;[0e8a] 01 fd ff
                    out       (c),a                         ;[0e8d] ed 79
                    in        a,(c)                         ;[0e8f] ed 78
                    pop       bc                            ;[0e91] c1
                    ret                                     ;[0e92] c9

                    ld        d,$07                         ;[0e93] 16 07
                    ld        e,$ff                         ;[0e95] 1e ff
                    call      $0e7c                         ;[0e97] cd 7c 0e
                    ld        d,$08                         ;[0e9a] 16 08
                    ld        e,$00                         ;[0e9c] 1e 00
                    call      $0e7c                         ;[0e9e] cd 7c 0e
                    inc       d                             ;[0ea1] 14
                    call      $0e7c                         ;[0ea2] cd 7c 0e
                    inc       d                             ;[0ea5] 14
                    call      $0e7c                         ;[0ea6] cd 7c 0e
                    call      $0a4f                         ;[0ea9] cd 4f 0a
                    rr        (iy+$22)                      ;[0eac] fd cb 22 1e
                    jr        c,$0eb8                       ;[0eb0] 38 06
                    call      $0a67                         ;[0eb2] cd 67 0a
                    call      $118d                         ;[0eb5] cd 8d 11
                    sla       (iy+$21)                      ;[0eb8] fd cb 21 26
                    jr        c,$0ec3                       ;[0ebc] 38 05
                    call      $0a6e                         ;[0ebe] cd 6e 0a
                    jr        $0eac                         ;[0ec1] 18 e9
                    ld        iy,$5c3a                      ;[0ec3] fd 21 3a 5c
                    ret                                     ;[0ec7] c9

                    push      hl                            ;[0ec8] e5
                    push      de                            ;[0ec9] d5
                    ld        l,(ix+$06)                    ;[0eca] dd 6e 06
                    ld        h,(ix+$07)                    ;[0ecd] dd 66 07
                    dec       hl                            ;[0ed0] 2b
                    ld        a,(hl)                        ;[0ed1] 7e
                    cp        $20                           ;[0ed2] fe 20
                    jr        z,$0ed0                       ;[0ed4] 28 fa
                    cp        $0d                           ;[0ed6] fe 0d
                    jr        z,$0ed0                       ;[0ed8] 28 f6
                    ld        (ix+$06),l                    ;[0eda] dd 75 06
                    ld        (ix+$07),h                    ;[0edd] dd 74 07
                    pop       de                            ;[0ee0] d1
                    pop       hl                            ;[0ee1] e1
                    ret                                     ;[0ee2] c9

                    push      hl                            ;[0ee3] e5
                    push      de                            ;[0ee4] d5
                    push      bc                            ;[0ee5] c5
                    ld        l,(ix+$06)                    ;[0ee6] dd 6e 06
                    ld        h,(ix+$07)                    ;[0ee9] dd 66 07
                    ld        a,h                           ;[0eec] 7c
                    cp        (ix+$09)                      ;[0eed] dd be 09
                    jr        nz,$0efb                      ;[0ef0] 20 09
                    ld        a,l                           ;[0ef2] 7d
                    cp        (ix+$08)                      ;[0ef3] dd be 08
                    jr        nz,$0efb                      ;[0ef6] 20 03
                    scf                                     ;[0ef8] 37
                    jr        $0f05                         ;[0ef9] 18 0a
                    ld        a,(hl)                        ;[0efb] 7e
                    cp        $20                           ;[0efc] fe 20
                    jr        z,$0f09                       ;[0efe] 28 09
                    cp        $0d                           ;[0f00] fe 0d
                    jr        z,$0f09                       ;[0f02] 28 05
                    or        a                             ;[0f04] b7
                    pop       bc                            ;[0f05] c1
                    pop       de                            ;[0f06] d1
                    pop       hl                            ;[0f07] e1
                    ret                                     ;[0f08] c9

                    inc       hl                            ;[0f09] 23
                    ld        (ix+$06),l                    ;[0f0a] dd 75 06
                    ld        (ix+$07),h                    ;[0f0d] dd 74 07
                    jr        $0eec                         ;[0f10] 18 da
                    call      $0e93                         ;[0f12] cd 93 0e
                    ei                                      ;[0f15] fb
                    call      $05ac                         ;[0f16] cd ac 05
                    add       hl,hl                         ;[0f19] 29
                    call      $0e93                         ;[0f1a] cd 93 0e
                    ei                                      ;[0f1d] fb
                    call      $05ac                         ;[0f1e] cd ac 05
                    daa                                     ;[0f21] 27
                    call      $0e93                         ;[0f22] cd 93 0e
                    ei                                      ;[0f25] fb
                    call      $05ac                         ;[0f26] cd ac 05
                    ld        h,$cd                         ;[0f29] 26 cd
                    sub       e                             ;[0f2b] 93
                    ld        c,$fb                         ;[0f2c] 0e fb
                    call      $05ac                         ;[0f2e] cd ac 05
                    rra                                     ;[0f31] 1f
                    call      $0e93                         ;[0f32] cd 93 0e
                    ei                                      ;[0f35] fb
                    call      $05ac                         ;[0f36] cd ac 05
                    jr        z,$0f08                       ;[0f39] 28 cd
                    sub       e                             ;[0f3b] 93
                    ld        c,$fb                         ;[0f3c] 0e fb
                    call      $05ac                         ;[0f3e] cd ac 05
                    ld        hl,($4fcd)                    ;[0f41] 2a cd 4f
                    ld        a,(bc)                        ;[0f44] 0a
                    rr        (iy+$22)                      ;[0f45] fd cb 22 1e
                    jr        c,$0f6c                       ;[0f49] 38 21
                    call      $0a67                         ;[0f4b] cd 67 0a
                    call      $0ad1                         ;[0f4e] cd d1 0a
                    cp        $80                           ;[0f51] fe 80
                    jr        z,$0f6c                       ;[0f53] 28 17
                    call      $0e20                         ;[0f55] cd 20 0e
                    ld        a,(ix+$02)                    ;[0f58] dd 7e 02
                    cp        $03                           ;[0f5b] fe 03
                    jr        nc,$0f69                      ;[0f5d] 30 0a
                    ld        d,$08                         ;[0f5f] 16 08
                    add       d                             ;[0f61] 82
                    ld        d,a                           ;[0f62] 57
                    ld        e,(ix+$04)                    ;[0f63] dd 5e 04
                    call      $0e7c                         ;[0f66] cd 7c 0e
                    call      $116e                         ;[0f69] cd 6e 11
                    sla       (iy+$21)                      ;[0f6c] fd cb 21 26
                    ret       c                             ;[0f70] d8
                    call      $0a6e                         ;[0f71] cd 6e 0a
                    jr        $0f45                         ;[0f74] 18 cf
                    push      hl                            ;[0f76] e5
                    ld        l,(iy+$27)                    ;[0f77] fd 6e 27
                    ld        h,(iy+$28)                    ;[0f7a] fd 66 28
                    ld        bc,$0064                      ;[0f7d] 01 64 00
                    or        a                             ;[0f80] b7
                    sbc       hl,bc                         ;[0f81] ed 42
                    push      hl                            ;[0f83] e5
                    pop       bc                            ;[0f84] c1
                    pop       hl                            ;[0f85] e1
                    dec       bc                            ;[0f86] 0b
                    ld        a,b                           ;[0f87] 78
                    or        c                             ;[0f88] b1
                    jr        nz,$0f86                      ;[0f89] 20 fb
                    dec       de                            ;[0f8b] 1b
                    ld        a,d                           ;[0f8c] 7a
                    or        e                             ;[0f8d] b3
                    jr        nz,$0f76                      ;[0f8e] 20 e6
                    ret                                     ;[0f90] c9

                    ld        de,$ffff                      ;[0f91] 11 ff ff
                    call      $0a4a                         ;[0f94] cd 4a 0a
                    rr        (iy+$22)                      ;[0f97] fd cb 22 1e
                    jr        c,$0faf                       ;[0f9b] 38 12
                    push      de                            ;[0f9d] d5
                    ld        e,(hl)                        ;[0f9e] 5e
                    inc       hl                            ;[0f9f] 23
                    ld        d,(hl)                        ;[0fa0] 56
                    ex        de,hl                         ;[0fa1] eb
                    ld        e,(hl)                        ;[0fa2] 5e
                    inc       hl                            ;[0fa3] 23
                    ld        d,(hl)                        ;[0fa4] 56
                    push      de                            ;[0fa5] d5
                    pop       hl                            ;[0fa6] e1
                    pop       bc                            ;[0fa7] c1
                    or        a                             ;[0fa8] b7
                    sbc       hl,bc                         ;[0fa9] ed 42
                    jr        c,$0faf                       ;[0fab] 38 02
                    push      bc                            ;[0fad] c5
                    pop       de                            ;[0fae] d1
                    sla       (iy+$21)                      ;[0faf] fd cb 21 26
                    jr        c,$0fba                       ;[0fb3] 38 05
                    call      $0a6e                         ;[0fb5] cd 6e 0a
                    jr        $0f97                         ;[0fb8] 18 dd
                    ld        (iy+$25),e                    ;[0fba] fd 73 25
                    ld        (iy+$26),d                    ;[0fbd] fd 72 26
                    ret                                     ;[0fc0] c9

                    xor       a                             ;[0fc1] af
                    ld        (iy+$2a),a                    ;[0fc2] fd 77 2a
                    call      $0a4f                         ;[0fc5] cd 4f 0a
                    rr        (iy+$22)                      ;[0fc8] fd cb 22 1e
                    jp        c,$105a                       ;[0fcc] da 5a 10
                    call      $0a67                         ;[0fcf] cd 67 0a
                    push      iy                            ;[0fd2] fd e5
                    pop       hl                            ;[0fd4] e1
                    ld        bc,$0011                      ;[0fd5] 01 11 00
                    add       hl,bc                         ;[0fd8] 09
                    ld        b,$00                         ;[0fd9] 06 00
                    ld        c,(ix+$02)                    ;[0fdb] dd 4e 02
                    sla       c                             ;[0fde] cb 21
                    add       hl,bc                         ;[0fe0] 09
                    ld        e,(hl)                        ;[0fe1] 5e
                    inc       hl                            ;[0fe2] 23
                    ld        d,(hl)                        ;[0fe3] 56
                    ex        de,hl                         ;[0fe4] eb
                    push      hl                            ;[0fe5] e5
                    ld        e,(hl)                        ;[0fe6] 5e
                    inc       hl                            ;[0fe7] 23
                    ld        d,(hl)                        ;[0fe8] 56
                    ex        de,hl                         ;[0fe9] eb
                    ld        e,(iy+$25)                    ;[0fea] fd 5e 25
                    ld        d,(iy+$26)                    ;[0fed] fd 56 26
                    or        a                             ;[0ff0] b7
                    sbc       hl,de                         ;[0ff1] ed 52
                    ex        de,hl                         ;[0ff3] eb
                    pop       hl                            ;[0ff4] e1
                    jr        z,$0ffc                       ;[0ff5] 28 05
                    ld        (hl),e                        ;[0ff7] 73
                    inc       hl                            ;[0ff8] 23
                    ld        (hl),d                        ;[0ff9] 72
                    jr        $105a                         ;[0ffa] 18 5e
                    ld        a,(ix+$02)                    ;[0ffc] dd 7e 02
                    cp        $03                           ;[0fff] fe 03
                    jr        nc,$100c                      ;[1001] 30 09
                    ld        d,$08                         ;[1003] 16 08
                    add       d                             ;[1005] 82
                    ld        d,a                           ;[1006] 57
                    ld        e,$00                         ;[1007] 1e 00
                    call      $0e7c                         ;[1009] cd 7c 0e
                    call      $118d                         ;[100c] cd 8d 11
                    push      ix                            ;[100f] dd e5
                    pop       hl                            ;[1011] e1
                    ld        bc,$0021                      ;[1012] 01 21 00
                    add       hl,bc                         ;[1015] 09
                    dec       (hl)                          ;[1016] 35
                    jr        nz,$1026                      ;[1017] 20 0d
                    call      $0b5c                         ;[1019] cd 5c 0b
                    ld        a,(iy+$21)                    ;[101c] fd 7e 21
                    and       (iy+$10)                      ;[101f] fd a6 10
                    jr        nz,$105a                      ;[1022] 20 36
                    jr        $103d                         ;[1024] 18 17
                    push      iy                            ;[1026] fd e5
                    pop       hl                            ;[1028] e1
                    ld        bc,$0011                      ;[1029] 01 11 00
                    add       hl,bc                         ;[102c] 09
                    ld        b,$00                         ;[102d] 06 00
                    ld        c,(ix+$02)                    ;[102f] dd 4e 02
                    sla       c                             ;[1032] cb 21
                    add       hl,bc                         ;[1034] 09
                    ld        e,(hl)                        ;[1035] 5e
                    inc       hl                            ;[1036] 23
                    ld        d,(hl)                        ;[1037] 56
                    inc       de                            ;[1038] 13
                    inc       de                            ;[1039] 13
                    ld        (hl),d                        ;[103a] 72
                    dec       hl                            ;[103b] 2b
                    ld        (hl),e                        ;[103c] 73
                    call      $0ad1                         ;[103d] cd d1 0a
                    ld        c,a                           ;[1040] 4f
                    ld        a,(iy+$21)                    ;[1041] fd 7e 21
                    and       (iy+$10)                      ;[1044] fd a6 10
                    jr        nz,$105a                      ;[1047] 20 11
                    ld        a,c                           ;[1049] 79
                    cp        $80                           ;[104a] fe 80
                    jr        z,$105a                       ;[104c] 28 0c
                    call      $0e20                         ;[104e] cd 20 0e
                    ld        a,(iy+$21)                    ;[1051] fd 7e 21
                    or        (iy+$2a)                      ;[1054] fd b6 2a
                    ld        (iy+$2a),a                    ;[1057] fd 77 2a
                    sla       (iy+$21)                      ;[105a] fd cb 21 26
                    jr        c,$1066                       ;[105e] 38 06
                    call      $0a6e                         ;[1060] cd 6e 0a
                    jp        $0fc8                         ;[1063] c3 c8 0f
                    ld        de,$0001                      ;[1066] 11 01 00
                    call      $0f76                         ;[1069] cd 76 0f
                    call      $0a4f                         ;[106c] cd 4f 0a
                    rr        (iy+$2a)                      ;[106f] fd cb 2a 1e
                    jr        nc,$108c                      ;[1073] 30 17
                    call      $0a67                         ;[1075] cd 67 0a
                    ld        a,(ix+$02)                    ;[1078] dd 7e 02
                    cp        $03                           ;[107b] fe 03
                    jr        nc,$1089                      ;[107d] 30 0a
                    ld        d,$08                         ;[107f] 16 08
                    add       d                             ;[1081] 82
                    ld        d,a                           ;[1082] 57
                    ld        e,(ix+$04)                    ;[1083] dd 5e 04
                    call      $0e7c                         ;[1086] cd 7c 0e
                    call      $116e                         ;[1089] cd 6e 11
                    sla       (iy+$21)                      ;[108c] fd cb 21 26
                    ret       c                             ;[1090] d8
                    call      $0a6e                         ;[1091] cd 6e 0a
                    jr        $106f                         ;[1094] 18 d9
                    cp        a                             ;[1096] bf
                    rrca                                    ;[1097] 0f
                    call      c,$070e                       ;[1098] dc 0e 07
                    ld        c,$3d                         ;[109b] 0e 3d
                    dec       c                             ;[109d] 0d
                    ld        a,a                           ;[109e] 7f
                    inc       c                             ;[109f] 0c
                    call      z,$220b                       ;[10a0] cc 0b 22
                    dec       bc                            ;[10a3] 0b
                    add       d                             ;[10a4] 82
                    ld        a,(bc)                        ;[10a5] 0a
                    ex        de,hl                         ;[10a6] eb
                    add       hl,bc                         ;[10a7] 09
                    ld        e,l                           ;[10a8] 5d
                    add       hl,bc                         ;[10a9] 09
                    sub       $08                           ;[10aa] d6 08
                    ld        d,a                           ;[10ac] 57
                    ex        af,af'                        ;[10ad] 08
                    rst       $18                           ;[10ae] df
                    rlca                                    ;[10af] 07
                    ld        l,(hl)                        ;[10b0] 6e
                    rlca                                    ;[10b1] 07
                    inc       bc                            ;[10b2] 03
                    rlca                                    ;[10b3] 07
                    sbc       a                             ;[10b4] 9f
                    ld        b,$40                         ;[10b5] 06 40
                    ld        b,$e6                         ;[10b7] 06 e6
                    dec       b                             ;[10b9] 05
                    sub       c                             ;[10ba] 91
                    dec       b                             ;[10bb] 05
                    ld        b,c                           ;[10bc] 41
                    dec       b                             ;[10bd] 05
                    or        $04                           ;[10be] f6 04
                    xor       (hl)                          ;[10c0] ae
                    inc       b                             ;[10c1] 04
                    ld        l,e                           ;[10c2] 6b
                    inc       b                             ;[10c3] 04
                    inc       l                             ;[10c4] 2c
                    inc       b                             ;[10c5] 04
                    ret       p                             ;[10c6] f0
                    inc       bc                            ;[10c7] 03
                    or        a                             ;[10c8] b7
                    inc       bc                            ;[10c9] 03
                    add       d                             ;[10ca] 82
                    inc       bc                            ;[10cb] 03
                    ld        c,a                           ;[10cc] 4f
                    inc       bc                            ;[10cd] 03
                    jr        nz,$10d3                      ;[10ce] 20 03
                    di                                      ;[10d0] f3
                    ld        (bc),a                        ;[10d1] 02
                    ret       z                             ;[10d2] c8
                    ld        (bc),a                        ;[10d3] 02
                    and       c                             ;[10d4] a1
                    ld        (bc),a                        ;[10d5] 02
                    ld        a,e                           ;[10d6] 7b
                    ld        (bc),a                        ;[10d7] 02
                    ld        d,a                           ;[10d8] 57
                    ld        (bc),a                        ;[10d9] 02
                    ld        (hl),$02                      ;[10da] 36 02
                    ld        d,$02                         ;[10dc] 16 02
                    ret       m                             ;[10de] f8
                    ld        bc,$01dc                      ;[10df] 01 dc 01
                    pop       bc                            ;[10e2] c1
                    ld        bc,$01a8                      ;[10e3] 01 a8 01
                    sub       b                             ;[10e6] 90
                    ld        bc,$0179                      ;[10e7] 01 79 01
                    ld        h,h                           ;[10ea] 64
                    ld        bc,$0150                      ;[10eb] 01 50 01
                    dec       a                             ;[10ee] 3d
                    ld        bc,$012c                      ;[10ef] 01 2c 01
                    dec       de                            ;[10f2] 1b
                    ld        bc,$010b                      ;[10f3] 01 0b 01
                    call      m,$ee00                       ;[10f6] fc 00 ee
                    nop                                     ;[10f9] 00
                    ret       po                            ;[10fa] e0
                    nop                                     ;[10fb] 00
                    call      nc,$c800                      ;[10fc] d4 00 c8
                    nop                                     ;[10ff] 00
                    cp        l                             ;[1100] bd
                    nop                                     ;[1101] 00
                    or        d                             ;[1102] b2
                    nop                                     ;[1103] 00
                    xor       b                             ;[1104] a8
                    nop                                     ;[1105] 00
                    sbc       a                             ;[1106] 9f
                    nop                                     ;[1107] 00
                    sub       (hl)                          ;[1108] 96
                    nop                                     ;[1109] 00
                    adc       l                             ;[110a] 8d
                    nop                                     ;[110b] 00
                    add       l                             ;[110c] 85
                    nop                                     ;[110d] 00
                    ld        a,(hl)                        ;[110e] 7e
                    nop                                     ;[110f] 00
                    ld        (hl),a                        ;[1110] 77
                    nop                                     ;[1111] 00
                    ld        (hl),b                        ;[1112] 70
                    nop                                     ;[1113] 00
                    ld        l,d                           ;[1114] 6a
                    nop                                     ;[1115] 00
                    ld        h,h                           ;[1116] 64
                    nop                                     ;[1117] 00
                    ld        e,(hl)                        ;[1118] 5e
                    nop                                     ;[1119] 00
                    ld        e,c                           ;[111a] 59
                    nop                                     ;[111b] 00
                    ld        d,h                           ;[111c] 54
                    nop                                     ;[111d] 00
                    ld        c,a                           ;[111e] 4f
                    nop                                     ;[111f] 00
                    ld        c,e                           ;[1120] 4b
                    nop                                     ;[1121] 00
                    ld        b,a                           ;[1122] 47
                    nop                                     ;[1123] 00
                    ld        b,e                           ;[1124] 43
                    nop                                     ;[1125] 00
                    ccf                                     ;[1126] 3f
                    nop                                     ;[1127] 00
                    dec       sp                            ;[1128] 3b
                    nop                                     ;[1129] 00
                    jr        c,$112c                       ;[112a] 38 00
                    dec       (hl)                          ;[112c] 35
                    nop                                     ;[112d] 00
                    ld        ($2f00),a                     ;[112e] 32 00 2f
                    nop                                     ;[1131] 00
                    dec       l                             ;[1132] 2d
                    nop                                     ;[1133] 00
                    ld        hl,($2800)                    ;[1134] 2a 00 28
                    nop                                     ;[1137] 00
                    dec       h                             ;[1138] 25
                    nop                                     ;[1139] 00
                    inc       hl                            ;[113a] 23
                    nop                                     ;[113b] 00
                    ld        hl,$1f00                      ;[113c] 21 00 1f
                    nop                                     ;[113f] 00
                    ld        e,$00                         ;[1140] 1e 00
                    inc       e                             ;[1142] 1c
                    nop                                     ;[1143] 00
                    ld        a,(de)                        ;[1144] 1a
                    nop                                     ;[1145] 00
                    add       hl,de                         ;[1146] 19
                    nop                                     ;[1147] 00
                    jr        $114a                         ;[1148] 18 00
                    ld        d,$00                         ;[114a] 16 00
                    dec       d                             ;[114c] 15
                    nop                                     ;[114d] 00
                    inc       d                             ;[114e] 14
                    nop                                     ;[114f] 00
                    inc       de                            ;[1150] 13
                    nop                                     ;[1151] 00
                    ld        (de),a                        ;[1152] 12
                    nop                                     ;[1153] 00
                    ld        de,$1000                      ;[1154] 11 00 10
                    nop                                     ;[1157] 00
                    rrca                                    ;[1158] 0f
                    nop                                     ;[1159] 00
                    ld        c,$00                         ;[115a] 0e 00
                    dec       c                             ;[115c] 0d
                    nop                                     ;[115d] 00
                    inc       c                             ;[115e] 0c
                    nop                                     ;[115f] 00
                    inc       c                             ;[1160] 0c
                    nop                                     ;[1161] 00
                    dec       bc                            ;[1162] 0b
                    nop                                     ;[1163] 00
                    dec       bc                            ;[1164] 0b
                    nop                                     ;[1165] 00
                    ld        a,(bc)                        ;[1166] 0a
                    nop                                     ;[1167] 00
                    add       hl,bc                         ;[1168] 09
                    nop                                     ;[1169] 00
                    add       hl,bc                         ;[116a] 09
                    nop                                     ;[116b] 00
                    ex        af,af'                        ;[116c] 08
                    nop                                     ;[116d] 00
                    ld        a,(ix+$01)                    ;[116e] dd 7e 01
                    or        a                             ;[1171] b7
                    ret       m                             ;[1172] f8
                    or        $90                           ;[1173] f6 90
                    call      $11a3                         ;[1175] cd a3 11
                    ld        a,(ix+$00)                    ;[1178] dd 7e 00
                    call      $11a3                         ;[117b] cd a3 11
                    ld        a,(ix+$04)                    ;[117e] dd 7e 04
                    res       4,a                           ;[1181] cb a7
                    sla       a                             ;[1183] cb 27
                    sla       a                             ;[1185] cb 27
                    sla       a                             ;[1187] cb 27
                    call      $11a3                         ;[1189] cd a3 11
                    ret                                     ;[118c] c9

                    ld        a,(ix+$01)                    ;[118d] dd 7e 01
                    or        a                             ;[1190] b7
                    ret       m                             ;[1191] f8
                    or        $80                           ;[1192] f6 80
                    call      $11a3                         ;[1194] cd a3 11
                    ld        a,(ix+$00)                    ;[1197] dd 7e 00
                    call      $11a3                         ;[119a] cd a3 11
                    ld        a,$40                         ;[119d] 3e 40
                    call      $11a3                         ;[119f] cd a3 11
                    ret                                     ;[11a2] c9

                    ld        l,a                           ;[11a3] 6f
                    ld        bc,$fffd                      ;[11a4] 01 fd ff
                    ld        a,$0e                         ;[11a7] 3e 0e
                    out       (c),a                         ;[11a9] ed 79
                    ld        bc,$bffd                      ;[11ab] 01 fd bf
                    ld        a,$fa                         ;[11ae] 3e fa
                    out       (c),a                         ;[11b0] ed 79
                    ld        e,$03                         ;[11b2] 1e 03
                    dec       e                             ;[11b4] 1d
                    jr        nz,$11b4                      ;[11b5] 20 fd
                    nop                                     ;[11b7] 00
                    nop                                     ;[11b8] 00
                    nop                                     ;[11b9] 00
                    nop                                     ;[11ba] 00
                    ld        a,l                           ;[11bb] 7d
                    ld        d,$08                         ;[11bc] 16 08
                    rra                                     ;[11be] 1f
                    ld        l,a                           ;[11bf] 6f
                    jp        nc,$11c9                      ;[11c0] d2 c9 11
                    ld        a,$fe                         ;[11c3] 3e fe
                    out       (c),a                         ;[11c5] ed 79
                    jr        $11cf                         ;[11c7] 18 06
                    ld        a,$fa                         ;[11c9] 3e fa
                    out       (c),a                         ;[11cb] ed 79
                    jr        $11cf                         ;[11cd] 18 00
                    ld        e,$02                         ;[11cf] 1e 02
                    dec       e                             ;[11d1] 1d
                    jr        nz,$11d1                      ;[11d2] 20 fd
                    nop                                     ;[11d4] 00
                    add       $00                           ;[11d5] c6 00
                    ld        a,l                           ;[11d7] 7d
                    dec       d                             ;[11d8] 15
                    jr        nz,$11be                      ;[11d9] 20 e3
                    nop                                     ;[11db] 00
                    nop                                     ;[11dc] 00
                    add       $00                           ;[11dd] c6 00
                    nop                                     ;[11df] 00
                    nop                                     ;[11e0] 00
                    ld        a,$fe                         ;[11e1] 3e fe
                    out       (c),a                         ;[11e3] ed 79
                    ld        e,$06                         ;[11e5] 1e 06
                    dec       e                             ;[11e7] 1d
                    jr        nz,$11e7                      ;[11e8] 20 fd
                    ret                                     ;[11ea] c9

                    ld        hl,$5b66                      ;[11eb] 21 66 5b
                    set       5,(hl)                        ;[11ee] cb ee
                    jr        $1205                         ;[11f0] 18 13
                    ld        hl,$5b66                      ;[11f2] 21 66 5b
                    set       4,(hl)                        ;[11f5] cb e6
                    jr        $1205                         ;[11f7] 18 0c
                    ld        hl,$5b66                      ;[11f9] 21 66 5b
                    set       7,(hl)                        ;[11fc] cb fe
                    jr        $1205                         ;[11fe] 18 05
                    ld        hl,$5b66                      ;[1200] 21 66 5b
                    set       6,(hl)                        ;[1203] cb f6
                    ld        hl,$5b66                      ;[1205] 21 66 5b
                    res       3,(hl)                        ;[1208] cb 9e
                    rst       $18                           ;[120a] df
                    cp        $21                           ;[120b] fe 21
                    jp        nz,$13be                      ;[120d] c2 be 13
                    ld        hl,$5b66                      ;[1210] 21 66 5b
                    set       3,(hl)                        ;[1213] cb de
                    rst       $20                           ;[1215] e7
                    jp        $13be                         ;[1216] c3 be 13
                    call      $05ac                         ;[1219] cd ac 05
                    dec       bc                            ;[121c] 0b
                    ld        ($5b74),hl                    ;[121d] 22 74 5b
                    ld        a,(ix+$00)                    ;[1220] dd 7e 00
                    ld        ($5b71),a                     ;[1223] 32 71 5b
                    ld        l,(ix+$0b)                    ;[1226] dd 6e 0b
                    ld        h,(ix+$0c)                    ;[1229] dd 66 0c
                    ld        ($5b72),hl                    ;[122c] 22 72 5b
                    ld        l,(ix+$0d)                    ;[122f] dd 6e 0d
                    ld        h,(ix+$0e)                    ;[1232] dd 66 0e
                    ld        ($5b78),hl                    ;[1235] 22 78 5b
                    ld        l,(ix+$0f)                    ;[1238] dd 6e 0f
                    ld        h,(ix+$10)                    ;[123b] dd 66 10
                    ld        ($5b76),hl                    ;[123e] 22 76 5b
                    or        a                             ;[1241] b7
                    jr        z,$124e                       ;[1242] 28 0a
                    cp        $03                           ;[1244] fe 03
                    jr        z,$124e                       ;[1246] 28 06
                    ld        a,(ix+$0e)                    ;[1248] dd 7e 0e
                    ld        ($5b76),a                     ;[124b] 32 76 5b
                    push      ix                            ;[124e] dd e5
                    pop       hl                            ;[1250] e1
                    inc       hl                            ;[1251] 23
                    ld        de,$5b67                      ;[1252] 11 67 5b
                    ld        bc,$000a                      ;[1255] 01 0a 00
                    ldir                                    ;[1258] ed b0
                    ld        hl,$5b66                      ;[125a] 21 66 5b
                    bit       5,(hl)                        ;[125d] cb 6e
                    jp        nz,$1bad                      ;[125f] c2 ad 1b
                    ld        hl,$5b71                      ;[1262] 21 71 5b
                    ld        de,$5b7a                      ;[1265] 11 7a 5b
                    ld        bc,$0007                      ;[1268] 01 07 00
                    ldir                                    ;[126b] ed b0
                    call      $1c2e                         ;[126d] cd 2e 1c
                    ld        a,($5b7a)                     ;[1270] 3a 7a 5b
                    ld        b,a                           ;[1273] 47
                    ld        a,($5b71)                     ;[1274] 3a 71 5b
                    cp        b                             ;[1277] b8
                    jr        nz,$1280                      ;[1278] 20 06
                    cp        $03                           ;[127a] fe 03
                    jr        z,$1290                       ;[127c] 28 12
                    jr        c,$1284                       ;[127e] 38 04
                    call      $05ac                         ;[1280] cd ac 05
                    dec       e                             ;[1283] 1d
                    ld        a,($5b66)                     ;[1284] 3a 66 5b
                    bit       6,a                           ;[1287] cb 77
                    jr        nz,$12c5                      ;[1289] 20 3a
                    bit       7,a                           ;[128b] cb 7f
                    jp        z,$12db                       ;[128d] ca db 12
                    ld        a,($5b66)                     ;[1290] 3a 66 5b
                    bit       6,a                           ;[1293] cb 77
                    jr        z,$129b                       ;[1295] 28 04
                    call      $05ac                         ;[1297] cd ac 05
                    inc       e                             ;[129a] 1c
                    ld        hl,($5b7b)                    ;[129b] 2a 7b 5b
                    ld        de,($5b72)                    ;[129e] ed 5b 72 5b
                    ld        a,h                           ;[12a2] 7c
                    or        l                             ;[12a3] b5
                    jr        z,$12ae                       ;[12a4] 28 08
                    sbc       hl,de                         ;[12a6] ed 52
                    jr        nc,$12ae                      ;[12a8] 30 04
                    call      $05ac                         ;[12aa] cd ac 05
                    ld        e,$2a                         ;[12ad] 1e 2a
                    ld        a,l                           ;[12af] 7d
                    ld        e,e                           ;[12b0] 5b
                    ld        a,h                           ;[12b1] 7c
                    or        l                             ;[12b2] b5
                    jr        nz,$12b8                      ;[12b3] 20 03
                    ld        hl,($5b74)                    ;[12b5] 2a 74 5b
                    ld        a,($5b71)                     ;[12b8] 3a 71 5b
                    and       a                             ;[12bb] a7
                    jr        nz,$12c1                      ;[12bc] 20 03
                    ld        hl,($5c53)                    ;[12be] 2a 53 5c
                    call      $137e                         ;[12c1] cd 7e 13
                    ret                                     ;[12c4] c9

                    ld        bc,($5b72)                    ;[12c5] ed 4b 72 5b
                    push      bc                            ;[12c9] c5
                    inc       bc                            ;[12ca] 03
                    rst       $28                           ;[12cb] ef
                    jr        nc,$12ce                      ;[12cc] 30 00
                    ld        (hl),$80                      ;[12ce] 36 80
                    ex        de,hl                         ;[12d0] eb
                    pop       de                            ;[12d1] d1
                    push      hl                            ;[12d2] e5
                    call      $137e                         ;[12d3] cd 7e 13
                    pop       hl                            ;[12d6] e1
                    rst       $28                           ;[12d7] ef
                    adc       $08                           ;[12d8] ce 08
                    ret                                     ;[12da] c9

                    ld        de,($5b72)                    ;[12db] ed 5b 72 5b
                    ld        hl,($5b7d)                    ;[12df] 2a 7d 5b
                    push      hl                            ;[12e2] e5
                    ld        a,h                           ;[12e3] 7c
                    or        l                             ;[12e4] b5
                    jr        nz,$12ed                      ;[12e5] 20 06
                    inc       de                            ;[12e7] 13
                    inc       de                            ;[12e8] 13
                    inc       de                            ;[12e9] 13
                    ex        de,hl                         ;[12ea] eb
                    jr        $12f6                         ;[12eb] 18 09
                    ld        hl,($5b7b)                    ;[12ed] 2a 7b 5b
                    ex        de,hl                         ;[12f0] eb
                    scf                                     ;[12f1] 37
                    sbc       hl,de                         ;[12f2] ed 52
                    jr        c,$12ff                       ;[12f4] 38 09
                    ld        de,$0005                      ;[12f6] 11 05 00
                    add       hl,de                         ;[12f9] 19
                    ld        b,h                           ;[12fa] 44
                    ld        c,l                           ;[12fb] 4d
                    rst       $28                           ;[12fc] ef
                    dec       b                             ;[12fd] 05
                    rra                                     ;[12fe] 1f
                    pop       hl                            ;[12ff] e1
                    ld        a,($5b71)                     ;[1300] 3a 71 5b
                    and       a                             ;[1303] a7
                    jr        z,$1335                       ;[1304] 28 2f
                    ld        a,h                           ;[1306] 7c
                    or        l                             ;[1307] b5
                    jr        z,$1315                       ;[1308] 28 0b
                    dec       hl                            ;[130a] 2b
                    ld        b,(hl)                        ;[130b] 46
                    dec       hl                            ;[130c] 2b
                    ld        c,(hl)                        ;[130d] 4e
                    dec       hl                            ;[130e] 2b
                    inc       bc                            ;[130f] 03
                    inc       bc                            ;[1310] 03
                    inc       bc                            ;[1311] 03
                    rst       $28                           ;[1312] ef
                    ret       pe                            ;[1313] e8
                    add       hl,de                         ;[1314] 19
                    ld        hl,($5c59)                    ;[1315] 2a 59 5c
                    dec       hl                            ;[1318] 2b
                    ld        bc,($5b72)                    ;[1319] ed 4b 72 5b
                    push      bc                            ;[131d] c5
                    inc       bc                            ;[131e] 03
                    inc       bc                            ;[131f] 03
                    inc       bc                            ;[1320] 03
                    ld        a,($5b7f)                     ;[1321] 3a 7f 5b
                    push      af                            ;[1324] f5
                    rst       $28                           ;[1325] ef
                    ld        d,l                           ;[1326] 55
                    ld        d,$23                         ;[1327] 16 23
                    pop       af                            ;[1329] f1
                    ld        (hl),a                        ;[132a] 77
                    pop       de                            ;[132b] d1
                    inc       hl                            ;[132c] 23
                    ld        (hl),e                        ;[132d] 73
                    inc       hl                            ;[132e] 23
                    ld        (hl),d                        ;[132f] 72
                    inc       hl                            ;[1330] 23
                    call      $137e                         ;[1331] cd 7e 13
                    ret                                     ;[1334] c9

                    ld        hl,$5b66                      ;[1335] 21 66 5b
                    res       1,(hl)                        ;[1338] cb 8e
                    ld        de,($5c53)                    ;[133a] ed 5b 53 5c
                    ld        hl,($5c59)                    ;[133e] 2a 59 5c
                    dec       hl                            ;[1341] 2b
                    rst       $28                           ;[1342] ef
                    push      hl                            ;[1343] e5
                    add       hl,de                         ;[1344] 19
                    ld        bc,($5b72)                    ;[1345] ed 4b 72 5b
                    ld        hl,($5c53)                    ;[1349] 2a 53 5c
                    rst       $28                           ;[134c] ef
                    ld        d,l                           ;[134d] 55
                    ld        d,$23                         ;[134e] 16 23
                    ld        bc,($5b76)                    ;[1350] ed 4b 76 5b
                    add       hl,bc                         ;[1354] 09
                    ld        ($5c4b),hl                    ;[1355] 22 4b 5c
                    ld        a,($5b79)                     ;[1358] 3a 79 5b
                    ld        h,a                           ;[135b] 67
                    and       $c0                           ;[135c] e6 c0
                    jr        nz,$1370                      ;[135e] 20 10
                    ld        a,($5b78)                     ;[1360] 3a 78 5b
                    ld        l,a                           ;[1363] 6f
                    ld        ($5c42),hl                    ;[1364] 22 42 5c
                    ld        (iy+$0a),$00                  ;[1367] fd 36 0a 00
                    ld        hl,$5b66                      ;[136b] 21 66 5b
                    set       1,(hl)                        ;[136e] cb ce
                    ld        hl,($5c53)                    ;[1370] 2a 53 5c
                    ld        de,($5b72)                    ;[1373] ed 5b 72 5b
                    dec       hl                            ;[1377] 2b
                    ld        ($5c57),hl                    ;[1378] 22 57 5c
                    inc       hl                            ;[137b] 23
                    jr        $1331                         ;[137c] 18 b3
                    ld        a,d                           ;[137e] 7a
                    or        e                             ;[137f] b3
                    ret       z                             ;[1380] c8
                    call      $1c4b                         ;[1381] cd 4b 1c
                    ret                                     ;[1384] c9

                    rst       $28                           ;[1385] ef
                    adc       h                             ;[1386] 8c
                    inc       e                             ;[1387] 1c
                    bit       7,(iy+$01)                    ;[1388] fd cb 01 7e
                    ret       z                             ;[138c] c8
                    push      af                            ;[138d] f5
                    rst       $28                           ;[138e] ef
                    pop       af                            ;[138f] f1
                    dec       hl                            ;[1390] 2b
                    pop       af                            ;[1391] f1
                    ret                                     ;[1392] c9

                    rst       $20                           ;[1393] e7
                    call      $1385                         ;[1394] cd 85 13
                    ret       z                             ;[1397] c8
                    push      af                            ;[1398] f5
                    ld        a,c                           ;[1399] 79
                    or        b                             ;[139a] b0
                    jr        z,$13ba                       ;[139b] 28 1d
                    ld        hl,$000a                      ;[139d] 21 0a 00
                    sbc       hl,bc                         ;[13a0] ed 42
                    jr        c,$13ba                       ;[13a2] 38 16
                    push      de                            ;[13a4] d5
                    push      bc                            ;[13a5] c5
                    ld        hl,$5b67                      ;[13a6] 21 67 5b
                    ld        b,$0a                         ;[13a9] 06 0a
                    ld        a,$20                         ;[13ab] 3e 20
                    ld        (hl),a                        ;[13ad] 77
                    inc       hl                            ;[13ae] 23
                    djnz      $13ad                         ;[13af] 10 fc
                    pop       bc                            ;[13b1] c1
                    pop       hl                            ;[13b2] e1
                    ld        de,$5b67                      ;[13b3] 11 67 5b
                    ldir                                    ;[13b6] ed b0
                    pop       af                            ;[13b8] f1
                    ret                                     ;[13b9] c9

                    call      $05ac                         ;[13ba] cd ac 05
                    ld        hl,$8cef                      ;[13bd] 21 ef 8c
                    inc       e                             ;[13c0] 1c
                    bit       7,(iy+$01)                    ;[13c1] fd cb 01 7e
                    jr        z,$1407                       ;[13c5] 28 40
                    ld        bc,$0011                      ;[13c7] 01 11 00
                    ld        a,($5c74)                     ;[13ca] 3a 74 5c
                    and       a                             ;[13cd] a7
                    jr        z,$13d2                       ;[13ce] 28 02
                    ld        c,$22                         ;[13d0] 0e 22
                    rst       $28                           ;[13d2] ef
                    jr        nc,$13d5                      ;[13d3] 30 00
                    push      de                            ;[13d5] d5
                    pop       ix                            ;[13d6] dd e1
                    ld        b,$0b                         ;[13d8] 06 0b
                    ld        a,$20                         ;[13da] 3e 20
                    ld        (de),a                        ;[13dc] 12
                    inc       de                            ;[13dd] 13
                    djnz      $13dc                         ;[13de] 10 fc
                    ld        (ix+$01),$ff                  ;[13e0] dd 36 01 ff
                    rst       $28                           ;[13e4] ef
                    pop       af                            ;[13e5] f1
                    dec       hl                            ;[13e6] 2b
                    ld        hl,$fff6                      ;[13e7] 21 f6 ff
                    dec       bc                            ;[13ea] 0b
                    add       hl,bc                         ;[13eb] 09
                    inc       bc                            ;[13ec] 03
                    jr        nc,$1400                      ;[13ed] 30 11
                    ld        a,($5c74)                     ;[13ef] 3a 74 5c
                    and       a                             ;[13f2] a7
                    jr        nz,$13f9                      ;[13f3] 20 04
                    call      $05ac                         ;[13f5] cd ac 05
                    ld        c,$78                         ;[13f8] 0e 78
                    or        c                             ;[13fa] b1
                    jr        z,$1407                       ;[13fb] 28 0a
                    ld        bc,$000a                      ;[13fd] 01 0a 00
                    push      ix                            ;[1400] dd e5
                    pop       hl                            ;[1402] e1
                    inc       hl                            ;[1403] 23
                    ex        de,hl                         ;[1404] eb
                    ldir                                    ;[1405] ed b0
                    rst       $18                           ;[1407] df
                    cp        $e4                           ;[1408] fe e4
                    jr        nz,$145f                      ;[140a] 20 53
                    ld        a,($5c74)                     ;[140c] 3a 74 5c
                    cp        $03                           ;[140f] fe 03
                    jp        z,$1219                       ;[1411] ca 19 12
                    rst       $20                           ;[1414] e7
                    rst       $28                           ;[1415] ef
                    or        d                             ;[1416] b2
                    jr        z,$1449                       ;[1417] 28 30
                    dec       d                             ;[1419] 15
                    ld        hl,$0000                      ;[141a] 21 00 00
                    bit       6,(iy+$01)                    ;[141d] fd cb 01 76
                    jr        z,$1425                       ;[1421] 28 02
                    set       7,c                           ;[1423] cb f9
                    ld        a,($5c74)                     ;[1425] 3a 74 5c
                    dec       a                             ;[1428] 3d
                    jr        z,$1444                       ;[1429] 28 19
                    call      $05ac                         ;[142b] cd ac 05
                    ld        bc,$19c2                      ;[142e] 01 c2 19
                    ld        (de),a                        ;[1431] 12
                    bit       7,(iy+$01)                    ;[1432] fd cb 01 7e
                    jr        z,$1451                       ;[1436] 28 19
                    ld        c,(hl)                        ;[1438] 4e
                    inc       hl                            ;[1439] 23
                    ld        a,(hl)                        ;[143a] 7e
                    ld        (ix+$0b),a                    ;[143b] dd 77 0b
                    inc       hl                            ;[143e] 23
                    ld        a,(hl)                        ;[143f] 7e
                    ld        (ix+$0c),a                    ;[1440] dd 77 0c
                    inc       hl                            ;[1443] 23
                    ld        (ix+$0e),c                    ;[1444] dd 71 0e
                    ld        a,$01                         ;[1447] 3e 01
                    bit       6,c                           ;[1449] cb 71
                    jr        z,$144e                       ;[144b] 28 01
                    inc       a                             ;[144d] 3c
                    ld        (ix+$00),a                    ;[144e] dd 77 00
                    ex        de,hl                         ;[1451] eb
                    rst       $20                           ;[1452] e7
                    cp        $29                           ;[1453] fe 29
                    jr        nz,$142f                      ;[1455] 20 d8
                    rst       $20                           ;[1457] e7
                    call      $18a1                         ;[1458] cd a1 18
                    ex        de,hl                         ;[145b] eb
                    jp        $1519                         ;[145c] c3 19 15
                    cp        $aa                           ;[145f] fe aa
                    jr        nz,$1482                      ;[1461] 20 1f
                    ld        a,($5c74)                     ;[1463] 3a 74 5c
                    cp        $03                           ;[1466] fe 03
                    jp        z,$1219                       ;[1468] ca 19 12
                    rst       $20                           ;[146b] e7
                    call      $18a1                         ;[146c] cd a1 18
                    ld        (ix+$0b),$00                  ;[146f] dd 36 0b 00
                    ld        (ix+$0c),$1b                  ;[1473] dd 36 0c 1b
                    ld        hl,$4000                      ;[1477] 21 00 40
                    ld        (ix+$0d),l                    ;[147a] dd 75 0d
                    ld        (ix+$0e),h                    ;[147d] dd 74 0e
                    jr        $14cf                         ;[1480] 18 4d
                    cp        $af                           ;[1482] fe af
                    jr        nz,$14d5                      ;[1484] 20 4f
                    ld        a,($5c74)                     ;[1486] 3a 74 5c
                    cp        $03                           ;[1489] fe 03
                    jp        z,$1219                       ;[148b] ca 19 12
                    rst       $20                           ;[148e] e7
                    rst       $28                           ;[148f] ef
                    ld        c,b                           ;[1490] 48
                    jr        nz,$14b3                      ;[1491] 20 20
                    inc       c                             ;[1493] 0c
                    ld        a,($5c74)                     ;[1494] 3a 74 5c
                    and       a                             ;[1497] a7
                    jp        z,$1219                       ;[1498] ca 19 12
                    rst       $28                           ;[149b] ef
                    and       $1c                           ;[149c] e6 1c
                    jr        $14af                         ;[149e] 18 0f
                    rst       $28                           ;[14a0] ef
                    add       d                             ;[14a1] 82
                    inc       e                             ;[14a2] 1c
                    rst       $18                           ;[14a3] df
                    cp        $2c                           ;[14a4] fe 2c
                    jr        z,$14b4                       ;[14a6] 28 0c
                    ld        a,($5c74)                     ;[14a8] 3a 74 5c
                    and       a                             ;[14ab] a7
                    jp        z,$1219                       ;[14ac] ca 19 12
                    rst       $28                           ;[14af] ef
                    and       $1c                           ;[14b0] e6 1c
                    jr        $14b8                         ;[14b2] 18 04
                    rst       $20                           ;[14b4] e7
                    rst       $28                           ;[14b5] ef
                    add       d                             ;[14b6] 82
                    inc       e                             ;[14b7] 1c
                    call      $18a1                         ;[14b8] cd a1 18
                    rst       $28                           ;[14bb] ef
                    sbc       c                             ;[14bc] 99
                    ld        e,$dd                         ;[14bd] 1e dd
                    ld        (hl),c                        ;[14bf] 71
                    dec       bc                            ;[14c0] 0b
                    ld        (ix+$0c),b                    ;[14c1] dd 70 0c
                    rst       $28                           ;[14c4] ef
                    sbc       c                             ;[14c5] 99
                    ld        e,$dd                         ;[14c6] 1e dd
                    ld        (hl),c                        ;[14c8] 71
                    dec       c                             ;[14c9] 0d
                    ld        (ix+$0e),b                    ;[14ca] dd 70 0e
                    ld        h,b                           ;[14cd] 60
                    ld        l,c                           ;[14ce] 69
                    ld        (ix+$00),$03                  ;[14cf] dd 36 00 03
                    jr        $1519                         ;[14d3] 18 44
                    cp        $ca                           ;[14d5] fe ca
                    jr        z,$14e2                       ;[14d7] 28 09
                    call      $18a1                         ;[14d9] cd a1 18
                    ld        (ix+$0e),$80                  ;[14dc] dd 36 0e 80
                    jr        $14f9                         ;[14e0] 18 17
                    ld        a,($5c74)                     ;[14e2] 3a 74 5c
                    and       a                             ;[14e5] a7
                    jp        nz,$1219                      ;[14e6] c2 19 12
                    rst       $20                           ;[14e9] e7
                    rst       $28                           ;[14ea] ef
                    add       d                             ;[14eb] 82
                    inc       e                             ;[14ec] 1c
                    call      $18a1                         ;[14ed] cd a1 18
                    rst       $28                           ;[14f0] ef
                    sbc       c                             ;[14f1] 99
                    ld        e,$dd                         ;[14f2] 1e dd
                    ld        (hl),c                        ;[14f4] 71
                    dec       c                             ;[14f5] 0d
                    ld        (ix+$0e),b                    ;[14f6] dd 70 0e
                    ld        (ix+$00),$00                  ;[14f9] dd 36 00 00
                    ld        hl,($5c59)                    ;[14fd] 2a 59 5c
                    ld        de,($5c53)                    ;[1500] ed 5b 53 5c
                    scf                                     ;[1504] 37
                    sbc       hl,de                         ;[1505] ed 52
                    ld        (ix+$0b),l                    ;[1507] dd 75 0b
                    ld        (ix+$0c),h                    ;[150a] dd 74 0c
                    ld        hl,($5c4b)                    ;[150d] 2a 4b 5c
                    sbc       hl,de                         ;[1510] ed 52
                    ld        (ix+$0f),l                    ;[1512] dd 75 0f
                    ld        (ix+$10),h                    ;[1515] dd 74 10
                    ex        de,hl                         ;[1518] eb
                    ld        a,($5b66)                     ;[1519] 3a 66 5b
                    bit       3,a                           ;[151c] cb 5f
                    jp        nz,$121d                      ;[151e] c2 1d 12
                    ld        a,($5c74)                     ;[1521] 3a 74 5c
                    and       a                             ;[1524] a7
                    jr        nz,$152b                      ;[1525] 20 04
                    rst       $28                           ;[1527] ef
                    ld        (hl),b                        ;[1528] 70
                    add       hl,bc                         ;[1529] 09
                    ret                                     ;[152a] c9

                    rst       $28                           ;[152b] ef
                    ld        h,c                           ;[152c] 61
                    rlca                                    ;[152d] 07
                    ret                                     ;[152e] c9

                    ld        hl,$eef5                      ;[152f] 21 f5 ee
                    res       0,(hl)                        ;[1532] cb 86
                    set       1,(hl)                        ;[1534] cb ce
                    ld        hl,($5c49)                    ;[1536] 2a 49 5c
                    ld        a,h                           ;[1539] 7c
                    or        l                             ;[153a] b5
                    jr        nz,$1540                      ;[153b] 20 03
                    ld        ($ec06),hl                    ;[153d] 22 06 ec
                    ld        a,($f9db)                     ;[1540] 3a db f9
                    push      af                            ;[1543] f5
                    ld        hl,($fc9a)                    ;[1544] 2a 9a fc
                    call      $334a                         ;[1547] cd 4a 33
                    ld        ($f9d7),hl                    ;[154a] 22 d7 f9
                    call      $3222                         ;[154d] cd 22 32
                    call      $30d6                         ;[1550] cd d6 30
                    pop       af                            ;[1553] f1
                    or        a                             ;[1554] b7
                    jr        z,$1563                       ;[1555] 28 0c
                    push      af                            ;[1557] f5
                    call      $30df                         ;[1558] cd df 30
                    ex        de,hl                         ;[155b] eb
                    call      $326a                         ;[155c] cd 6a 32
                    pop       af                            ;[155f] f1
                    dec       a                             ;[1560] 3d
                    jr        $1554                         ;[1561] 18 f1
                    ld        c,$00                         ;[1563] 0e 00
                    call      $30b4                         ;[1565] cd b4 30
                    ld        b,c                           ;[1568] 41
                    ld        a,($ec15)                     ;[1569] 3a 15 ec
                    ld        c,a                           ;[156c] 4f
                    push      bc                            ;[156d] c5
                    push      de                            ;[156e] d5
                    call      $30df                         ;[156f] cd df 30
                    ld        a,($eef5)                     ;[1572] 3a f5 ee
                    bit       1,a                           ;[1575] cb 4f
                    jr        z,$1596                       ;[1577] 28 1d
                    push      de                            ;[1579] d5
                    push      hl                            ;[157a] e5
                    ld        de,$0020                      ;[157b] 11 20 00
                    add       hl,de                         ;[157e] 19
                    bit       0,(hl)                        ;[157f] cb 46
                    jr        z,$1594                       ;[1581] 28 11
                    inc       hl                            ;[1583] 23
                    ld        d,(hl)                        ;[1584] 56
                    inc       hl                            ;[1585] 23
                    ld        e,(hl)                        ;[1586] 5e
                    or        a                             ;[1587] b7
                    ld        hl,($5c49)                    ;[1588] 2a 49 5c
                    sbc       hl,de                         ;[158b] ed 52
                    jr        nz,$1594                      ;[158d] 20 05
                    ld        hl,$eef5                      ;[158f] 21 f5 ee
                    set       0,(hl)                        ;[1592] cb c6
                    pop       hl                            ;[1594] e1
                    pop       de                            ;[1595] d1
                    push      bc                            ;[1596] c5
                    push      hl                            ;[1597] e5
                    ld        bc,$0023                      ;[1598] 01 23 00
                    ldir                                    ;[159b] ed b0
                    pop       hl                            ;[159d] e1
                    pop       bc                            ;[159e] c1
                    push      de                            ;[159f] d5
                    push      bc                            ;[15a0] c5
                    ex        de,hl                         ;[15a1] eb
                    ld        hl,$eef5                      ;[15a2] 21 f5 ee
                    bit       0,(hl)                        ;[15a5] cb 46
                    jr        z,$15d3                       ;[15a7] 28 2a
                    ld        b,$00                         ;[15a9] 06 00
                    ld        hl,($ec06)                    ;[15ab] 2a 06 ec
                    ld        a,h                           ;[15ae] 7c
                    or        l                             ;[15af] b5
                    jr        z,$15c0                       ;[15b0] 28 0e
                    push      hl                            ;[15b2] e5
                    call      $2e41                         ;[15b3] cd 41 2e
                    pop       hl                            ;[15b6] e1
                    jr        nc,$15cb                      ;[15b7] 30 12
                    dec       hl                            ;[15b9] 2b
                    inc       b                             ;[15ba] 04
                    ld        ($ec06),hl                    ;[15bb] 22 06 ec
                    jr        $15ab                         ;[15be] 18 eb
                    call      $2e41                         ;[15c0] cd 41 2e
                    call      nc,$2e63                      ;[15c3] d4 63 2e
                    ld        hl,$eef5                      ;[15c6] 21 f5 ee
                    ld        (hl),$00                      ;[15c9] 36 00
                    ld        a,b                           ;[15cb] 78
                    pop       bc                            ;[15cc] c1
                    push      bc                            ;[15cd] c5
                    ld        c,b                           ;[15ce] 48
                    ld        b,a                           ;[15cf] 47
                    call      $2a11                         ;[15d0] cd 11 2a
                    pop       bc                            ;[15d3] c1
                    pop       de                            ;[15d4] d1
                    ld        a,c                           ;[15d5] 79
                    inc       b                             ;[15d6] 04
                    cp        b                             ;[15d7] b8
                    jr        nc,$156f                      ;[15d8] 30 95
                    ld        a,($eef5)                     ;[15da] 3a f5 ee
                    bit       1,a                           ;[15dd] cb 4f
                    jr        z,$1602                       ;[15df] 28 21
                    bit       0,a                           ;[15e1] cb 47
                    jr        nz,$1602                      ;[15e3] 20 1d
                    ld        hl,($5c49)                    ;[15e5] 2a 49 5c
                    ld        a,h                           ;[15e8] 7c
                    or        l                             ;[15e9] b5
                    jr        z,$15f4                       ;[15ea] 28 08
                    ld        ($fc9a),hl                    ;[15ec] 22 9a fc
                    call      $3222                         ;[15ef] cd 22 32
                    jr        $15fd                         ;[15f2] 18 09
                    ld        ($fc9a),hl                    ;[15f4] 22 9a fc
                    call      $3352                         ;[15f7] cd 52 33
                    ld        ($5c49),hl                    ;[15fa] 22 49 5c
                    pop       de                            ;[15fd] d1
                    pop       bc                            ;[15fe] c1
                    jp        $1536                         ;[15ff] c3 36 15
                    pop       de                            ;[1602] d1
                    pop       bc                            ;[1603] c1
                    cp        a                             ;[1604] bf
                    push      af                            ;[1605] f5
                    ld        a,c                           ;[1606] 79
                    ld        c,b                           ;[1607] 48
                    call      $30b4                         ;[1608] cd b4 30
                    ex        de,hl                         ;[160b] eb
                    push      af                            ;[160c] f5
                    call      $3604                         ;[160d] cd 04 36
                    pop       af                            ;[1610] f1
                    ld        de,$0023                      ;[1611] 11 23 00
                    add       hl,de                         ;[1614] 19
                    inc       c                             ;[1615] 0c
                    cp        c                             ;[1616] b9
                    jr        nc,$160c                      ;[1617] 30 f3
                    pop       af                            ;[1619] f1
                    ret       z                             ;[161a] c8
                    call      $2a07                         ;[161b] cd 07 2a
                    call      $2b78                         ;[161e] cd 78 2b
                    ld        hl,($ec06)                    ;[1621] 2a 06 ec
                    dec       hl                            ;[1624] 2b
                    ld        a,h                           ;[1625] 7c
                    or        l                             ;[1626] b5
                    ld        ($ec06),hl                    ;[1627] 22 06 ec
                    jr        nz,$161e                      ;[162a] 20 f2
                    jp        $2a11                         ;[162c] c3 11 2a
                    ret                                     ;[162f] c9

                    ld        b,$00                         ;[1630] 06 00
                    ld        a,($ec15)                     ;[1632] 3a 15 ec
                    ld        d,a                           ;[1635] 57
                    jp        $3b5e                         ;[1636] c3 5e 3b
                    ld        b,$00                         ;[1639] 06 00
                    push      hl                            ;[163b] e5
                    ld        c,b                           ;[163c] 48
                    call      $30b4                         ;[163d] cd b4 30
                    call      $326a                         ;[1640] cd 6a 32
                    pop       hl                            ;[1643] e1
                    ret       nc                            ;[1644] d0
                    call      $30df                         ;[1645] cd df 30
                    push      bc                            ;[1648] c5
                    push      hl                            ;[1649] e5
                    ld        hl,$0023                      ;[164a] 21 23 00
                    add       hl,de                         ;[164d] 19
                    ld        a,($ec15)                     ;[164e] 3a 15 ec
                    ld        c,a                           ;[1651] 4f
                    cp        b                             ;[1652] b8
                    jr        z,$1663                       ;[1653] 28 0e
                    push      bc                            ;[1655] c5
                    push      bc                            ;[1656] c5
                    ld        bc,$0023                      ;[1657] 01 23 00
                    ldir                                    ;[165a] ed b0
                    pop       bc                            ;[165c] c1
                    ld        a,c                           ;[165d] 79
                    inc       b                             ;[165e] 04
                    cp        b                             ;[165f] b8
                    jr        nz,$1656                      ;[1660] 20 f4
                    pop       bc                            ;[1662] c1
                    pop       hl                            ;[1663] e1
                    call      $3618                         ;[1664] cd 18 36
                    ld        bc,$0023                      ;[1667] 01 23 00
                    ldir                                    ;[166a] ed b0
                    scf                                     ;[166c] 37
                    pop       bc                            ;[166d] c1
                    ret                                     ;[166e] c9

                    ld        b,$00                         ;[166f] 06 00
                    call      $322b                         ;[1671] cd 2b 32
                    ret       nc                            ;[1674] d0
                    push      bc                            ;[1675] c5
                    push      hl                            ;[1676] e5
                    ld        a,($ec15)                     ;[1677] 3a 15 ec
                    ld        c,a                           ;[167a] 4f
                    call      $30b4                         ;[167b] cd b4 30
                    call      $311e                         ;[167e] cd 1e 31
                    jr        nc,$16a9                      ;[1681] 30 26
                    dec       de                            ;[1683] 1b
                    ld        hl,$0023                      ;[1684] 21 23 00
                    add       hl,de                         ;[1687] 19
                    ex        de,hl                         ;[1688] eb
                    push      bc                            ;[1689] c5
                    ld        a,b                           ;[168a] 78
                    cp        c                             ;[168b] b9
                    jr        z,$169a                       ;[168c] 28 0c
                    push      bc                            ;[168e] c5
                    ld        bc,$0023                      ;[168f] 01 23 00
                    lddr                                    ;[1692] ed b8
                    pop       bc                            ;[1694] c1
                    ld        a,b                           ;[1695] 78
                    dec       c                             ;[1696] 0d
                    cp        c                             ;[1697] b9
                    jr        c,$168e                       ;[1698] 38 f4
                    ex        de,hl                         ;[169a] eb
                    inc       de                            ;[169b] 13
                    pop       bc                            ;[169c] c1
                    pop       hl                            ;[169d] e1
                    call      $362c                         ;[169e] cd 2c 36
                    ld        bc,$0023                      ;[16a1] 01 23 00
                    ldir                                    ;[16a4] ed b0
                    scf                                     ;[16a6] 37
                    pop       bc                            ;[16a7] c1
                    ret                                     ;[16a8] c9

                    pop       hl                            ;[16a9] e1
                    pop       bc                            ;[16aa] c1
                    ret                                     ;[16ab] c9

                    push      de                            ;[16ac] d5
                    ld        h,$00                         ;[16ad] 26 00
                    ld        l,b                           ;[16af] 68
                    add       hl,de                         ;[16b0] 19
                    ld        d,a                           ;[16b1] 57
                    ld        a,b                           ;[16b2] 78
                    ld        e,(hl)                        ;[16b3] 5e
                    ld        (hl),d                        ;[16b4] 72
                    ld        d,e                           ;[16b5] 53
                    inc       hl                            ;[16b6] 23
                    inc       a                             ;[16b7] 3c
                    cp        $20                           ;[16b8] fe 20
                    jr        c,$16b3                       ;[16ba] 38 f7
                    ld        a,e                           ;[16bc] 7b
                    cp        $00                           ;[16bd] fe 00
                    pop       de                            ;[16bf] d1
                    ret                                     ;[16c0] c9

                    push      de                            ;[16c1] d5
                    ld        hl,$0020                      ;[16c2] 21 20 00
                    add       hl,de                         ;[16c5] 19
                    push      hl                            ;[16c6] e5
                    ld        d,a                           ;[16c7] 57
                    ld        a,$1f                         ;[16c8] 3e 1f
                    jr        $16d3                         ;[16ca] 18 07
                    ld        e,(hl)                        ;[16cc] 5e
                    ld        (hl),d                        ;[16cd] 72
                    ld        d,e                           ;[16ce] 53
                    cp        b                             ;[16cf] b8
                    jr        z,$16d6                       ;[16d0] 28 04
                    dec       a                             ;[16d2] 3d
                    dec       hl                            ;[16d3] 2b
                    jr        $16cc                         ;[16d4] 18 f6
                    ld        a,e                           ;[16d6] 7b
                    cp        $00                           ;[16d7] fe 00
                    pop       hl                            ;[16d9] e1
                    pop       de                            ;[16da] d1
                    ret                                     ;[16db] c9

                    or        c                             ;[16dc] b1
                    ret                                     ;[16dd] c9

                    cp        h                             ;[16de] bc
                    cp        (hl)                          ;[16df] be
                    jp        $b4af                         ;[16e0] c3 af b4
                    sub       e                             ;[16e3] 93
                    sub       c                             ;[16e4] 91
                    sub       d                             ;[16e5] 92
                    sub       l                             ;[16e6] 95
                    sbc       b                             ;[16e7] 98
                    sbc       b                             ;[16e8] 98
                    sbc       b                             ;[16e9] 98
                    sbc       b                             ;[16ea] 98
                    sbc       b                             ;[16eb] 98
                    sbc       b                             ;[16ec] 98
                    sbc       b                             ;[16ed] 98
                    ld        a,a                           ;[16ee] 7f
                    add       c                             ;[16ef] 81
                    ld        l,$6c                         ;[16f0] 2e 6c
                    ld        l,(hl)                        ;[16f2] 6e
                    ld        (hl),b                        ;[16f3] 70
                    ld        c,b                           ;[16f4] 48
                    sub       h                             ;[16f5] 94
                    ld        d,(hl)                        ;[16f6] 56
                    ccf                                     ;[16f7] 3f
                    ld        b,c                           ;[16f8] 41
                    dec       hl                            ;[16f9] 2b
                    rla                                     ;[16fa] 17
                    rra                                     ;[16fb] 1f
                    scf                                     ;[16fc] 37
                    ld        (hl),a                        ;[16fd] 77
                    ld        b,h                           ;[16fe] 44
                    rrca                                    ;[16ff] 0f
                    ld        e,c                           ;[1700] 59
                    dec       hl                            ;[1701] 2b
                    ld        b,e                           ;[1702] 43
                    dec       l                             ;[1703] 2d
                    ld        d,c                           ;[1704] 51
                    ld        a,($426d)                     ;[1705] 3a 6d 42
                    dec       c                             ;[1708] 0d
                    ld        c,c                           ;[1709] 49
                    ld        e,h                           ;[170a] 5c
                    ld        b,h                           ;[170b] 44
                    dec       d                             ;[170c] 15
                    ld        e,l                           ;[170d] 5d
                    ld        bc,$023d                      ;[170e] 01 3d 02
                    ld        b,$00                         ;[1711] 06 00
                    ld        h,a                           ;[1713] 67
                    ld        e,$06                         ;[1714] 1e 06
                    rrc       (hl)                          ;[1716] cb 0e
                    ld        h,a                           ;[1718] 67
                    add       hl,de                         ;[1719] 19
                    ld        b,$0c                         ;[171a] 06 0c
                    ld        d,e                           ;[171c] 53
                    ld        a,(de)                        ;[171d] 1a
                    nop                                     ;[171e] 00
                    xor       $1c                           ;[171f] ee 1c
                    inc       c                             ;[1721] 0c
                    ld        l,a                           ;[1722] 6f
                    ld        a,(de)                        ;[1723] 1a
                    inc       b                             ;[1724] 04
                    dec       a                             ;[1725] 3d
                    ld        b,$cc                         ;[1726] 06 cc
                    ld        b,$0e                         ;[1728] 06 0e
                    add       c                             ;[172a] 81
                    add       hl,de                         ;[172b] 19
                    inc       b                             ;[172c] 04
                    nop                                     ;[172d] 00
                    xor       e                             ;[172e] ab
                    dec       e                             ;[172f] 1d
                    ld        c,$78                         ;[1730] 0e 78
                    ld        hl,$8c0e                      ;[1732] 21 0e 8c
                    ld        hl,$d50e                      ;[1735] 21 0e d5
                    ld        hl,$620e                      ;[1738] 21 0e 62
                    jr        $1749                         ;[173b] 18 0c
                    xor       d                             ;[173d] aa
                    ld        hl,$020d                      ;[173e] 21 0d 02
                    ld        a,(de)                        ;[1741] 1a
                    ld        c,$75                         ;[1742] 0e 75
                    dec       de                            ;[1744] 1b
                    ex        af,af'                        ;[1745] 08
                    nop                                     ;[1746] 00
                    add       b                             ;[1747] 80
                    ld        e,$03                         ;[1748] 1e 03
                    ld        c,a                           ;[174a] 4f
                    ld        e,$00                         ;[174b] 1e 00
                    ld        e,a                           ;[174d] 5f
                    ld        e,$0d                         ;[174e] 1e 0d
                    dec       c                             ;[1750] 0d
                    ld        a,(de)                        ;[1751] 1a
                    nop                                     ;[1752] 00
                    ld        l,e                           ;[1753] 6b
                    dec       c                             ;[1754] 0d
                    add       hl,bc                         ;[1755] 09
                    nop                                     ;[1756] 00
                    call      c,$0622                       ;[1757] dc 22 06
                    nop                                     ;[175a] 00
                    ld        a,($0e1f)                     ;[175b] 3a 1f 0e
                    xor       e                             ;[175e] ab
                    add       hl,de                         ;[175f] 19
                    ld        c,$eb                         ;[1760] 0e eb
                    add       hl,de                         ;[1762] 19
                    inc       bc                            ;[1763] 03
                    ld        b,d                           ;[1764] 42
                    ld        e,$09                         ;[1765] 1e 09
                    ld        c,$be                         ;[1767] 0e be
                    ld        hl,$a70c                      ;[1769] 21 0c a7
                    ld        hl,$740e                      ;[176c] 21 0e 74
                    ld        hl,$710e                      ;[176f] 21 0e 71
                    dec       de                            ;[1772] 1b
                    dec       bc                            ;[1773] 0b
                    dec       bc                            ;[1774] 0b
                    dec       bc                            ;[1775] 0b
                    dec       bc                            ;[1776] 0b
                    ex        af,af'                        ;[1777] 08
                    nop                                     ;[1778] 00
                    ret       m                             ;[1779] f8
                    inc       bc                            ;[177a] 03
                    add       hl,bc                         ;[177b] 09
                    ld        c,$ae                         ;[177c] 0e ae
                    ld        hl,$0707                      ;[177e] 21 07 07
                    rlca                                    ;[1781] 07
                    rlca                                    ;[1782] 07
                    rlca                                    ;[1783] 07
                    rlca                                    ;[1784] 07
                    ex        af,af'                        ;[1785] 08
                    nop                                     ;[1786] 00
                    ld        a,d                           ;[1787] 7a
                    ld        e,$06                         ;[1788] 1e 06
                    nop                                     ;[178a] 00
                    sub       h                             ;[178b] 94
                    ld        ($8c0e),hl                    ;[178c] 22 0e 8c
                    ld        a,(de)                        ;[178f] 1a
                    ld        b,$2c                         ;[1790] 06 2c
                    ld        a,(bc)                        ;[1792] 0a
                    nop                                     ;[1793] 00
                    ld        (hl),$17                      ;[1794] 36 17
                    ld        b,$00                         ;[1796] 06 00
                    push      hl                            ;[1798] e5
                    ld        d,$0e                         ;[1799] 16 0e
                    ld        b,c                           ;[179b] 41
                    ld        b,$0a                         ;[179c] 06 0a
                    inc       l                             ;[179e] 2c
                    ld        a,(bc)                        ;[179f] 0a
                    inc       c                             ;[17a0] 0c
                    ret       p                             ;[17a1] f0
                    ld        a,(de)                        ;[17a2] 1a
                    ld        c,$0c                         ;[17a3] 0e 0c
                    inc       e                             ;[17a5] 1c
                    ld        c,$e5                         ;[17a6] 0e e5
                    dec       de                            ;[17a8] 1b
                    inc       c                             ;[17a9] 0c
                    dec       hl                            ;[17aa] 2b
                    dec       de                            ;[17ab] 1b
                    ld        c,$17                         ;[17ac] 0e 17
                    inc       hl                            ;[17ae] 23
                    res       7,(iy+$01)                    ;[17af] fd cb 01 be
                    rst       $28                           ;[17b3] ef
                    ei                                      ;[17b4] fb
                    add       hl,de                         ;[17b5] 19
                    xor       a                             ;[17b6] af
                    ld        ($5c47),a                     ;[17b7] 32 47 5c
                    dec       a                             ;[17ba] 3d
                    ld        ($5c3a),a                     ;[17bb] 32 3a 5c
                    jr        $17c1                         ;[17be] 18 01
                    rst       $20                           ;[17c0] e7
                    rst       $28                           ;[17c1] ef
                    cp        a                             ;[17c2] bf
                    ld        d,$fd                         ;[17c3] 16 fd
                    inc       (hl)                          ;[17c5] 34
                    dec       c                             ;[17c6] 0d
                    jp        m,$1912                       ;[17c7] fa 12 19
                    rst       $18                           ;[17ca] df
                    ld        b,$00                         ;[17cb] 06 00
                    cp        $0d                           ;[17cd] fe 0d
                    jp        z,$1863                       ;[17cf] ca 63 18
                    cp        $3a                           ;[17d2] fe 3a
                    jr        z,$17c0                       ;[17d4] 28 ea
                    ld        hl,$1821                      ;[17d6] 21 21 18
                    push      hl                            ;[17d9] e5
                    ld        c,a                           ;[17da] 4f
                    rst       $20                           ;[17db] e7
                    ld        a,c                           ;[17dc] 79
                    sub       $ce                           ;[17dd] d6 ce
                    jr        nc,$17f4                      ;[17df] 30 13
                    add       $ce                           ;[17e1] c6 ce
                    ld        hl,$17a9                      ;[17e3] 21 a9 17
                    cp        $a3                           ;[17e6] fe a3
                    jr        z,$1800                       ;[17e8] 28 16
                    ld        hl,$17ac                      ;[17ea] 21 ac 17
                    cp        $a4                           ;[17ed] fe a4
                    jr        z,$1800                       ;[17ef] 28 0f
                    jp        $1912                         ;[17f1] c3 12 19
                    ld        c,a                           ;[17f4] 4f
                    ld        hl,$16dc                      ;[17f5] 21 dc 16
                    add       hl,bc                         ;[17f8] 09
                    ld        c,(hl)                        ;[17f9] 4e
                    add       hl,bc                         ;[17fa] 09
                    jr        $1800                         ;[17fb] 18 03
                    ld        hl,($5c74)                    ;[17fd] 2a 74 5c
                    ld        a,(hl)                        ;[1800] 7e
                    inc       hl                            ;[1801] 23
                    ld        ($5c74),hl                    ;[1802] 22 74 5c
                    ld        bc,$17fd                      ;[1805] 01 fd 17
                    push      bc                            ;[1808] c5
                    ld        c,a                           ;[1809] 4f
                    cp        $20                           ;[180a] fe 20
                    jr        nc,$181a                      ;[180c] 30 0c
                    ld        hl,$18b5                      ;[180e] 21 b5 18
                    ld        b,$00                         ;[1811] 06 00
                    add       hl,bc                         ;[1813] 09
                    ld        c,(hl)                        ;[1814] 4e
                    add       hl,bc                         ;[1815] 09
                    push      hl                            ;[1816] e5
                    rst       $18                           ;[1817] df
                    dec       b                             ;[1818] 05
                    ret                                     ;[1819] c9

                    rst       $18                           ;[181a] df
                    cp        c                             ;[181b] b9
                    jp        nz,$1912                      ;[181c] c2 12 19
                    rst       $20                           ;[181f] e7
                    ret                                     ;[1820] c9

                    call      $05d6                         ;[1821] cd d6 05
                    jr        c,$182a                       ;[1824] 38 04
                    call      $05ac                         ;[1826] cd ac 05
                    inc       d                             ;[1829] 14
                    bit       7,(iy+$0a)                    ;[182a] fd cb 0a 7e
                    jp        nz,$18a8                      ;[182e] c2 a8 18
                    ld        hl,($5c42)                    ;[1831] 2a 42 5c
                    bit       7,h                           ;[1834] cb 7c
                    jr        z,$184c                       ;[1836] 28 14
                    ld        hl,$fffe                      ;[1838] 21 fe ff
                    ld        ($5c45),hl                    ;[183b] 22 45 5c
                    ld        hl,($5c61)                    ;[183e] 2a 61 5c
                    dec       hl                            ;[1841] 2b
                    ld        de,($5c59)                    ;[1842] ed 5b 59 5c
                    dec       de                            ;[1846] 1b
                    ld        a,($5c44)                     ;[1847] 3a 44 5c
                    jr        $1882                         ;[184a] 18 36
                    rst       $28                           ;[184c] ef
                    ld        l,(hl)                        ;[184d] 6e
                    add       hl,de                         ;[184e] 19
                    ld        a,($5c44)                     ;[184f] 3a 44 5c
                    jr        z,$1870                       ;[1852] 28 1c
                    and       a                             ;[1854] a7
                    jr        nz,$189d                      ;[1855] 20 46
                    ld        b,a                           ;[1857] 47
                    ld        a,(hl)                        ;[1858] 7e
                    and       $c0                           ;[1859] e6 c0
                    ld        a,b                           ;[185b] 78
                    jr        z,$1870                       ;[185c] 28 12
                    call      $05ac                         ;[185e] cd ac 05
                    rst       $38                           ;[1861] ff
                    pop       bc                            ;[1862] c1
                    bit       7,(iy+$01)                    ;[1863] fd cb 01 7e
                    ret       z                             ;[1867] c8
                    ld        hl,($5c55)                    ;[1868] 2a 55 5c
                    ld        a,$c0                         ;[186b] 3e c0
                    and       (hl)                          ;[186d] a6
                    ret       nz                            ;[186e] c0
                    xor       a                             ;[186f] af
                    cp        $01                           ;[1870] fe 01
                    adc       $00                           ;[1872] ce 00
                    ld        d,(hl)                        ;[1874] 56
                    inc       hl                            ;[1875] 23
                    ld        e,(hl)                        ;[1876] 5e
                    ld        ($5c45),de                    ;[1877] ed 53 45 5c
                    inc       hl                            ;[187b] 23
                    ld        e,(hl)                        ;[187c] 5e
                    inc       hl                            ;[187d] 23
                    ld        d,(hl)                        ;[187e] 56
                    ex        de,hl                         ;[187f] eb
                    add       hl,de                         ;[1880] 19
                    inc       hl                            ;[1881] 23
                    ld        ($5c55),hl                    ;[1882] 22 55 5c
                    ex        de,hl                         ;[1885] eb
                    ld        ($5c5d),hl                    ;[1886] 22 5d 5c
                    ld        d,a                           ;[1889] 57
                    ld        e,$00                         ;[188a] 1e 00
                    ld        (iy+$0a),$ff                  ;[188c] fd 36 0a ff
                    dec       d                             ;[1890] 15
                    ld        (iy+$0d),d                    ;[1891] fd 72 0d
                    jp        z,$17c0                       ;[1894] ca c0 17
                    inc       d                             ;[1897] 14
                    rst       $28                           ;[1898] ef
                    adc       e                             ;[1899] 8b
                    add       hl,de                         ;[189a] 19
                    jr        z,$18a8                       ;[189b] 28 0b
                    call      $05ac                         ;[189d] cd ac 05
                    ld        d,$fd                         ;[18a0] 16 fd
                    rlc       c                             ;[18a2] cb 01
                    ld        a,(hl)                        ;[18a4] 7e
                    ret       nz                            ;[18a5] c0
                    pop       bc                            ;[18a6] c1
                    pop       bc                            ;[18a7] c1
                    rst       $18                           ;[18a8] df
                    cp        $0d                           ;[18a9] fe 0d
                    jr        z,$1863                       ;[18ab] 28 b6
                    cp        $3a                           ;[18ad] fe 3a
                    jp        z,$17c0                       ;[18af] ca c0 17
                    jp        $1912                         ;[18b2] c3 12 19
                    inc       h                             ;[18b5] 24
                    ld        b,e                           ;[18b6] 43
                    ld        b,(hl)                        ;[18b7] 46
                    ld        e,$4c                         ;[18b8] 1e 4c
                    jr        nz,$190f                      ;[18ba] 20 53
                    ld        e,(hl)                        ;[18bc] 5e
                    ld        c,l                           ;[18bd] 4d
                    add       (hl)                          ;[18be] 86
                    ld        d,a                           ;[18bf] 57
                    adc       b                             ;[18c0] 88
                    ld        b,$02                         ;[18c1] 06 02
                    dec       b                             ;[18c3] 05
                    rst       $28                           ;[18c4] ef
                    sbc       $1c                           ;[18c5] de 1c
                    cp        a                             ;[18c7] bf
                    pop       bc                            ;[18c8] c1
                    call      z,$18a1                       ;[18c9] cc a1 18
                    ex        de,hl                         ;[18cc] eb
                    ld        hl,($5c74)                    ;[18cd] 2a 74 5c
                    ld        c,(hl)                        ;[18d0] 4e
                    inc       hl                            ;[18d1] 23
                    ld        b,(hl)                        ;[18d2] 46
                    ex        de,hl                         ;[18d3] eb
                    push      bc                            ;[18d4] c5
                    ret                                     ;[18d5] c9

                    rst       $28                           ;[18d6] ef
                    sbc       $1c                           ;[18d7] de 1c
                    cp        a                             ;[18d9] bf
                    pop       bc                            ;[18da] c1
                    call      z,$18a1                       ;[18db] cc a1 18
                    ex        de,hl                         ;[18de] eb
                    ld        hl,($5c74)                    ;[18df] 2a 74 5c
                    ld        c,(hl)                        ;[18e2] 4e
                    inc       hl                            ;[18e3] 23
                    ld        b,(hl)                        ;[18e4] 46
                    ex        de,hl                         ;[18e5] eb
                    push      hl                            ;[18e6] e5
                    ld        hl,$18f8                      ;[18e7] 21 f8 18
                    ld        ($5b5a),hl                    ;[18ea] 22 5a 5b
                    ld        hl,$5b14                      ;[18ed] 21 14 5b
                    ex        (sp),hl                       ;[18f0] e3
                    push      hl                            ;[18f1] e5
                    ld        h,b                           ;[18f2] 60
                    ld        l,c                           ;[18f3] 69
                    ex        (sp),hl                       ;[18f4] e3
                    jp        $5b00                         ;[18f5] c3 00 5b
                    ret                                     ;[18f8] c9

                    rst       $28                           ;[18f9] ef
                    rra                                     ;[18fa] 1f
                    inc       e                             ;[18fb] 1c
                    ret                                     ;[18fc] c9

                    pop       bc                            ;[18fd] c1
                    rst       $28                           ;[18fe] ef
                    ld        d,(hl)                        ;[18ff] 56
                    inc       e                             ;[1900] 1c
                    call      $18a1                         ;[1901] cd a1 18
                    ret                                     ;[1904] c9

                    rst       $28                           ;[1905] ef
                    ld        l,h                           ;[1906] 6c
                    inc       e                             ;[1907] 1c
                    ret                                     ;[1908] c9

                    rst       $20                           ;[1909] e7
                    rst       $28                           ;[190a] ef
                    ld        a,d                           ;[190b] 7a
                    inc       e                             ;[190c] 1c
                    ret                                     ;[190d] c9

                    rst       $28                           ;[190e] ef
                    add       d                             ;[190f] 82
                    inc       e                             ;[1910] 1c
                    ret                                     ;[1911] c9

                    call      $05ac                         ;[1912] cd ac 05
                    dec       bc                            ;[1915] 0b
                    rst       $28                           ;[1916] ef
                    adc       h                             ;[1917] 8c
                    inc       e                             ;[1918] 1c
                    ret                                     ;[1919] c9

                    bit       7,(iy+$01)                    ;[191a] fd cb 01 7e
                    res       0,(iy+$02)                    ;[191e] fd cb 02 86
                    jr        z,$1927                       ;[1922] 28 03
                    rst       $28                           ;[1924] ef
                    ld        c,l                           ;[1925] 4d
                    dec       c                             ;[1926] 0d
                    pop       af                            ;[1927] f1
                    ld        a,($5c74)                     ;[1928] 3a 74 5c
                    sub       $a7                           ;[192b] d6 a7
                    rst       $28                           ;[192d] ef
                    call      m,$cd21                       ;[192e] fc 21 cd
                    and       c                             ;[1931] a1
                    jr        $195e                         ;[1932] 18 2a
                    adc       a                             ;[1934] 8f
                    ld        e,h                           ;[1935] 5c
                    ld        ($5c8d),hl                    ;[1936] 22 8d 5c
                    ld        hl,$5c91                      ;[1939] 21 91 5c
                    ld        a,(hl)                        ;[193c] 7e
                    rlca                                    ;[193d] 07
                    xor       (hl)                          ;[193e] ae
                    and       $aa                           ;[193f] e6 aa
                    xor       (hl)                          ;[1941] ae
                    ld        (hl),a                        ;[1942] 77
                    ret                                     ;[1943] c9

                    rst       $28                           ;[1944] ef
                    cp        (hl)                          ;[1945] be
                    inc       e                             ;[1946] 1c
                    ret                                     ;[1947] c9

                    pop       af                            ;[1948] f1
                    ld        a,($5b66)                     ;[1949] 3a 66 5b
                    and       $0f                           ;[194c] e6 0f
                    ld        ($5b66),a                     ;[194e] 32 66 5b
                    ld        a,($5c74)                     ;[1951] 3a 74 5c
                    sub       $74                           ;[1954] d6 74
                    ld        ($5c74),a                     ;[1956] 32 74 5c
                    jp        z,$11eb                       ;[1959] ca eb 11
                    dec       a                             ;[195c] 3d
                    jp        z,$11f2                       ;[195d] ca f2 11
                    dec       a                             ;[1960] 3d
                    jp        z,$11f9                       ;[1961] ca f9 11
                    jp        $1200                         ;[1964] c3 00 12
                    pop       bc                            ;[1967] c1
                    bit       7,(iy+$01)                    ;[1968] fd cb 01 7e
                    jr        z,$197e                       ;[196c] 28 10
                    ld        hl,($5c65)                    ;[196e] 2a 65 5c
                    ld        de,$fffb                      ;[1971] 11 fb ff
                    add       hl,de                         ;[1974] 19
                    ld        ($5c65),hl                    ;[1975] 22 65 5c
                    rst       $28                           ;[1978] ef
                    jp        (hl)                          ;[1979] e9
                    inc       (hl)                          ;[197a] 34
                    jp        c,$1863                       ;[197b] da 63 18
                    jp        $17c1                         ;[197e] c3 c1 17
                    cp        $cd                           ;[1981] fe cd
                    jr        nz,$198e                      ;[1983] 20 09
                    rst       $20                           ;[1985] e7
                    call      $190e                         ;[1986] cd 0e 19
                    call      $18a1                         ;[1989] cd a1 18
                    jr        $19a6                         ;[198c] 18 18
                    call      $18a1                         ;[198e] cd a1 18
                    ld        hl,($5c65)                    ;[1991] 2a 65 5c
                    ld        (hl),$00                      ;[1994] 36 00
                    inc       hl                            ;[1996] 23
                    ld        (hl),$00                      ;[1997] 36 00
                    inc       hl                            ;[1999] 23
                    ld        (hl),$01                      ;[199a] 36 01
                    inc       hl                            ;[199c] 23
                    ld        (hl),$00                      ;[199d] 36 00
                    inc       hl                            ;[199f] 23
                    ld        (hl),$00                      ;[19a0] 36 00
                    inc       hl                            ;[19a2] 23
                    ld        ($5c65),hl                    ;[19a3] 22 65 5c
                    rst       $28                           ;[19a6] ef
                    ld        d,$1d                         ;[19a7] 16 1d
                    ret                                     ;[19a9] c9

                    rst       $20                           ;[19aa] e7
                    call      $18f9                         ;[19ab] cd f9 18
                    bit       7,(iy+$01)                    ;[19ae] fd cb 01 7e
                    jr        z,$19e2                       ;[19b2] 28 2e
                    rst       $18                           ;[19b4] df
                    ld        ($5c5f),hl                    ;[19b5] 22 5f 5c
                    ld        hl,($5c57)                    ;[19b8] 2a 57 5c
                    ld        a,(hl)                        ;[19bb] 7e
                    cp        $2c                           ;[19bc] fe 2c
                    jr        z,$19cb                       ;[19be] 28 0b
                    ld        e,$e4                         ;[19c0] 1e e4
                    rst       $28                           ;[19c2] ef
                    add       (hl)                          ;[19c3] 86
                    dec       e                             ;[19c4] 1d
                    jr        nc,$19cb                      ;[19c5] 30 04
                    call      $05ac                         ;[19c7] cd ac 05
                    dec       c                             ;[19ca] 0d
                    inc       hl                            ;[19cb] 23
                    ld        ($5c5d),hl                    ;[19cc] 22 5d 5c
                    ld        a,(hl)                        ;[19cf] 7e
                    rst       $28                           ;[19d0] ef
                    ld        d,(hl)                        ;[19d1] 56
                    inc       e                             ;[19d2] 1c
                    rst       $18                           ;[19d3] df
                    ld        ($5c57),hl                    ;[19d4] 22 57 5c
                    ld        hl,($5c5f)                    ;[19d7] 2a 5f 5c
                    ld        (iy+$26),$00                  ;[19da] fd 36 26 00
                    ld        ($5c5d),hl                    ;[19de] 22 5d 5c
                    ld        a,(hl)                        ;[19e1] 7e
                    rst       $18                           ;[19e2] df
                    cp        $2c                           ;[19e3] fe 2c
                    jr        z,$19aa                       ;[19e5] 28 c3
                    call      $18a1                         ;[19e7] cd a1 18
                    ret                                     ;[19ea] c9

                    bit       7,(iy+$01)                    ;[19eb] fd cb 01 7e
                    jr        nz,$19fc                      ;[19ef] 20 0b
                    rst       $28                           ;[19f1] ef
                    ei                                      ;[19f2] fb
                    inc       h                             ;[19f3] 24
                    cp        $2c                           ;[19f4] fe 2c
                    call      nz,$18a1                      ;[19f6] c4 a1 18
                    rst       $20                           ;[19f9] e7
                    jr        $19f1                         ;[19fa] 18 f5
                    ld        a,$e4                         ;[19fc] 3e e4
                    rst       $28                           ;[19fe] ef
                    add       hl,sp                         ;[19ff] 39
                    ld        e,$c9                         ;[1a00] 1e c9
                    rst       $28                           ;[1a02] ef
                    ld        h,a                           ;[1a03] 67
                    ld        e,$01                         ;[1a04] 1e 01
                    nop                                     ;[1a06] 00
                    nop                                     ;[1a07] 00
                    rst       $28                           ;[1a08] ef
                    ld        b,l                           ;[1a09] 45
                    ld        e,$18                         ;[1a0a] 1e 18
                    inc       bc                            ;[1a0c] 03
                    rst       $28                           ;[1a0d] ef
                    sbc       c                             ;[1a0e] 99
                    ld        e,$78                         ;[1a0f] 1e 78
                    or        c                             ;[1a11] b1
                    jr        nz,$1a18                      ;[1a12] 20 04
                    ld        bc,($5cb2)                    ;[1a14] ed 4b b2 5c
                    push      bc                            ;[1a18] c5
                    ld        de,($5c4b)                    ;[1a19] ed 5b 4b 5c
                    ld        hl,($5c59)                    ;[1a1d] 2a 59 5c
                    dec       hl                            ;[1a20] 2b
                    rst       $28                           ;[1a21] ef
                    push      hl                            ;[1a22] e5
                    add       hl,de                         ;[1a23] 19
                    rst       $28                           ;[1a24] ef
                    ld        l,e                           ;[1a25] 6b
                    dec       c                             ;[1a26] 0d
                    ld        hl,($5c65)                    ;[1a27] 2a 65 5c
                    ld        de,$0032                      ;[1a2a] 11 32 00
                    add       hl,de                         ;[1a2d] 19
                    pop       de                            ;[1a2e] d1
                    sbc       hl,de                         ;[1a2f] ed 52
                    jr        nc,$1a3b                      ;[1a31] 30 08
                    ld        hl,($5cb4)                    ;[1a33] 2a b4 5c
                    and       a                             ;[1a36] a7
                    sbc       hl,de                         ;[1a37] ed 52
                    jr        nc,$1a3f                      ;[1a39] 30 04
                    call      $05ac                         ;[1a3b] cd ac 05
                    dec       d                             ;[1a3e] 15
                    ld        ($5cb2),de                    ;[1a3f] ed 53 b2 5c
                    pop       de                            ;[1a43] d1
                    pop       hl                            ;[1a44] e1
                    pop       bc                            ;[1a45] c1
                    ld        sp,($5cb2)                    ;[1a46] ed 7b b2 5c
                    inc       sp                            ;[1a4a] 33
                    push      bc                            ;[1a4b] c5
                    push      hl                            ;[1a4c] e5
                    ld        ($5c3d),sp                    ;[1a4d] ed 73 3d 5c
                    push      de                            ;[1a51] d5
                    ret                                     ;[1a52] c9

                    pop       de                            ;[1a53] d1
                    ld        h,(iy+$0d)                    ;[1a54] fd 66 0d
                    inc       h                             ;[1a57] 24
                    ex        (sp),hl                       ;[1a58] e3
                    inc       sp                            ;[1a59] 33
                    ld        bc,($5c45)                    ;[1a5a] ed 4b 45 5c
                    push      bc                            ;[1a5e] c5
                    push      hl                            ;[1a5f] e5
                    ld        ($5c3d),sp                    ;[1a60] ed 73 3d 5c
                    push      de                            ;[1a64] d5
                    rst       $28                           ;[1a65] ef
                    ld        h,a                           ;[1a66] 67
                    ld        e,$01                         ;[1a67] 1e 01
                    inc       d                             ;[1a69] 14
                    nop                                     ;[1a6a] 00
                    rst       $28                           ;[1a6b] ef
                    dec       b                             ;[1a6c] 05
                    rra                                     ;[1a6d] 1f
                    ret                                     ;[1a6e] c9

                    pop       bc                            ;[1a6f] c1
                    pop       hl                            ;[1a70] e1
                    pop       de                            ;[1a71] d1
                    ld        a,d                           ;[1a72] 7a
                    cp        $3e                           ;[1a73] fe 3e
                    jr        z,$1a86                       ;[1a75] 28 0f
                    dec       sp                            ;[1a77] 3b
                    ex        (sp),hl                       ;[1a78] e3
                    ex        de,hl                         ;[1a79] eb
                    ld        ($5c3d),sp                    ;[1a7a] ed 73 3d 5c
                    push      bc                            ;[1a7e] c5
                    ld        ($5c42),hl                    ;[1a7f] 22 42 5c
                    ld        (iy+$0a),d                    ;[1a82] fd 72 0a
                    ret                                     ;[1a85] c9

                    push      de                            ;[1a86] d5
                    push      hl                            ;[1a87] e5
                    call      $05ac                         ;[1a88] cd ac 05
                    ld        b,$fd                         ;[1a8b] 06 fd
                    rlc       c                             ;[1a8d] cb 01
                    ld        a,(hl)                        ;[1a8f] 7e
                    jr        z,$1a97                       ;[1a90] 28 05
                    ld        a,$ce                         ;[1a92] 3e ce
                    jp        $19fe                         ;[1a94] c3 fe 19
                    set       6,(iy+$01)                    ;[1a97] fd cb 01 f6
                    rst       $28                           ;[1a9b] ef
                    adc       l                             ;[1a9c] 8d
                    inc       l                             ;[1a9d] 2c
                    jr        nc,$1ab6                      ;[1a9e] 30 16
                    rst       $20                           ;[1aa0] e7
                    cp        $24                           ;[1aa1] fe 24
                    jr        nz,$1aaa                      ;[1aa3] 20 05
                    res       6,(iy+$01)                    ;[1aa5] fd cb 01 b6
                    rst       $20                           ;[1aa9] e7
                    cp        $28                           ;[1aaa] fe 28
                    jr        nz,$1aea                      ;[1aac] 20 3c
                    rst       $20                           ;[1aae] e7
                    cp        $29                           ;[1aaf] fe 29
                    jr        z,$1ad3                       ;[1ab1] 28 20
                    rst       $28                           ;[1ab3] ef
                    adc       l                             ;[1ab4] 8d
                    inc       l                             ;[1ab5] 2c
                    jp        nc,$1912                      ;[1ab6] d2 12 19
                    ex        de,hl                         ;[1ab9] eb
                    rst       $20                           ;[1aba] e7
                    cp        $24                           ;[1abb] fe 24
                    jr        nz,$1ac1                      ;[1abd] 20 02
                    ex        de,hl                         ;[1abf] eb
                    rst       $20                           ;[1ac0] e7
                    ex        de,hl                         ;[1ac1] eb
                    ld        bc,$0006                      ;[1ac2] 01 06 00
                    rst       $28                           ;[1ac5] ef
                    ld        d,l                           ;[1ac6] 55
                    ld        d,$23                         ;[1ac7] 16 23
                    inc       hl                            ;[1ac9] 23
                    ld        (hl),$0e                      ;[1aca] 36 0e
                    cp        $2c                           ;[1acc] fe 2c
                    jr        nz,$1ad3                      ;[1ace] 20 03
                    rst       $20                           ;[1ad0] e7
                    jr        $1ab3                         ;[1ad1] 18 e0
                    cp        $29                           ;[1ad3] fe 29
                    jr        nz,$1aea                      ;[1ad5] 20 13
                    rst       $20                           ;[1ad7] e7
                    cp        $3d                           ;[1ad8] fe 3d
                    jr        nz,$1aea                      ;[1ada] 20 0e
                    rst       $20                           ;[1adc] e7
                    ld        a,($5c3b)                     ;[1add] 3a 3b 5c
                    push      af                            ;[1ae0] f5
                    rst       $28                           ;[1ae1] ef
                    ei                                      ;[1ae2] fb
                    inc       h                             ;[1ae3] 24
                    pop       af                            ;[1ae4] f1
                    xor       (iy+$01)                      ;[1ae5] fd ae 01
                    and       $40                           ;[1ae8] e6 40
                    jp        nz,$1912                      ;[1aea] c2 12 19
                    call      $18a1                         ;[1aed] cd a1 18
                    ret                                     ;[1af0] c9

                    ld        hl,$ec0e                      ;[1af1] 21 0e ec
                    ld        (hl),$ff                      ;[1af4] 36 ff
                    call      $1f20                         ;[1af6] cd 20 1f
                    rst       $28                           ;[1af9] ef
                    or        b                             ;[1afa] b0
                    ld        d,$2a                         ;[1afb] 16 2a
                    ld        e,c                           ;[1afd] 59
                    ld        e,h                           ;[1afe] 5c
                    ld        bc,$0003                      ;[1aff] 01 03 00
                    rst       $28                           ;[1b02] ef
                    ld        d,l                           ;[1b03] 55
                    ld        d,$21                         ;[1b04] 16 21
                    ld        l,(hl)                        ;[1b06] 6e
                    dec       de                            ;[1b07] 1b
                    ld        de,($5c59)                    ;[1b08] ed 5b 59 5c
                    ld        bc,$0003                      ;[1b0c] 01 03 00
                    ldir                                    ;[1b0f] ed b0
                    call      $026b                         ;[1b11] cd 6b 02
                    call      $1f20                         ;[1b14] cd 20 1f
                    rst       $28                           ;[1b17] ef
                    or        b                             ;[1b18] b0
                    ld        d,$2a                         ;[1b19] 16 2a
                    ld        e,c                           ;[1b1b] 59
                    ld        e,h                           ;[1b1c] 5c
                    ld        bc,$0001                      ;[1b1d] 01 01 00
                    rst       $28                           ;[1b20] ef
                    ld        d,l                           ;[1b21] 55
                    ld        d,$2a                         ;[1b22] 16 2a
                    ld        e,c                           ;[1b24] 59
                    ld        e,h                           ;[1b25] 5c
                    ld        (hl),$e1                      ;[1b26] 36 e1
                    call      $026b                         ;[1b28] cd 6b 02
                    call      $1b53                         ;[1b2b] cd 53 1b
                    ld        sp,($5c3d)                    ;[1b2e] ed 7b 3d 5c
                    pop       hl                            ;[1b32] e1
                    ld        hl,$1303                      ;[1b33] 21 03 13
                    push      hl                            ;[1b36] e5
                    ld        hl,$0013                      ;[1b37] 21 13 00
                    push      hl                            ;[1b3a] e5
                    ld        hl,$0008                      ;[1b3b] 21 08 00
                    push      hl                            ;[1b3e] e5
                    ld        a,$20                         ;[1b3f] 3e 20
                    ld        ($5b5c),a                     ;[1b41] 32 5c 5b
                    jp        $5b00                         ;[1b44] c3 00 5b
                    ld        hl,$0000                      ;[1b47] 21 00 00
                    push      hl                            ;[1b4a] e5
                    ld        a,$20                         ;[1b4b] 3e 20
                    ld        ($5b5c),a                     ;[1b4d] 32 5c 5b
                    jp        $5b00                         ;[1b50] c3 00 5b
                    ld        hl,($5c4f)                    ;[1b53] 2a 4f 5c
                    ld        de,$0005                      ;[1b56] 11 05 00
                    add       hl,de                         ;[1b59] 19
                    ld        de,$000a                      ;[1b5a] 11 0a 00
                    ex        de,hl                         ;[1b5d] eb
                    add       hl,de                         ;[1b5e] 19
                    ex        de,hl                         ;[1b5f] eb
                    ld        bc,$0004                      ;[1b60] 01 04 00
                    ldir                                    ;[1b63] ed b0
                    res       3,(iy+$30)                    ;[1b65] fd cb 30 9e
                    res       4,(iy+$01)                    ;[1b69] fd cb 01 a6
                    ret                                     ;[1b6d] c9

                    rst       $28                           ;[1b6e] ef
                    ld        ($3e22),hl                    ;[1b6f] 22 22 3e
                    inc       bc                            ;[1b72] 03
                    jr        $1b77                         ;[1b73] 18 02
                    ld        a,$02                         ;[1b75] 3e 02
                    ld        (iy+$02),$00                  ;[1b77] fd 36 02 00
                    rst       $28                           ;[1b7b] ef
                    jr        nc,$1ba3                      ;[1b7c] 30 25
                    jr        z,$1b83                       ;[1b7e] 28 03
                    rst       $28                           ;[1b80] ef
                    ld        bc,$ef16                      ;[1b81] 01 16 ef
                    jr        $1b86                         ;[1b84] 18 00
                    rst       $28                           ;[1b86] ef
                    ld        (hl),b                        ;[1b87] 70
                    jr        nz,$1bc2                      ;[1b88] 20 38
                    jr        $1b7b                         ;[1b8a] 18 ef
                    jr        $1b8e                         ;[1b8c] 18 00
                    cp        $3b                           ;[1b8e] fe 3b
                    jr        z,$1b96                       ;[1b90] 28 04
                    cp        $2c                           ;[1b92] fe 2c
                    jr        nz,$1b9e                      ;[1b94] 20 08
                    rst       $28                           ;[1b96] ef
                    jr        nz,$1b99                      ;[1b97] 20 00
                    call      $190e                         ;[1b99] cd 0e 19
                    jr        $1ba6                         ;[1b9c] 18 08
                    rst       $28                           ;[1b9e] ef
                    and       $1c                           ;[1b9f] e6 1c
                    jr        $1ba6                         ;[1ba1] 18 03
                    rst       $28                           ;[1ba3] ef
                    sbc       $1c                           ;[1ba4] de 1c
                    call      $18a1                         ;[1ba6] cd a1 18
                    rst       $28                           ;[1ba9] ef
                    dec       h                             ;[1baa] 25
                    jr        $1b76                         ;[1bab] 18 c9
                    ld        ($5b81),sp                    ;[1bad] ed 73 81 5b
                    ld        sp,$5bff                      ;[1bb1] 31 ff 5b
                    call      $1c97                         ;[1bb4] cd 97 1c
                    ld        bc,($5b72)                    ;[1bb7] ed 4b 72 5b
                    ld        hl,$fff7                      ;[1bbb] 21 f7 ff
                    or        $ff                           ;[1bbe] f6 ff
                    sbc       hl,bc                         ;[1bc0] ed 42
                    call      $1cf3                         ;[1bc2] cd f3 1c
                    ld        bc,$0009                      ;[1bc5] 01 09 00
                    ld        hl,$5b71                      ;[1bc8] 21 71 5b
                    call      $1dac                         ;[1bcb] cd ac 1d
                    ld        hl,($5b74)                    ;[1bce] 2a 74 5b
                    ld        bc,($5b72)                    ;[1bd1] ed 4b 72 5b
                    call      $1dac                         ;[1bd5] cd ac 1d
                    call      $1d56                         ;[1bd8] cd 56 1d
                    ld        a,$05                         ;[1bdb] 3e 05
                    call      $1c64                         ;[1bdd] cd 64 1c
                    ld        sp,($5b81)                    ;[1be0] ed 7b 81 5b
                    ret                                     ;[1be4] c9

                    rst       $28                           ;[1be5] ef
                    jr        $1be8                         ;[1be6] 18 00
                    cp        $21                           ;[1be8] fe 21
                    jp        nz,$1912                      ;[1bea] c2 12 19
                    rst       $28                           ;[1bed] ef
                    jr        nz,$1bf0                      ;[1bee] 20 00
                    call      $18a1                         ;[1bf0] cd a1 18
                    ld        a,$02                         ;[1bf3] 3e 02
                    rst       $28                           ;[1bf5] ef
                    ld        bc,$ed16                      ;[1bf6] 01 16 ed
                    ld        (hl),e                        ;[1bf9] 73
                    add       c                             ;[1bfa] 81
                    ld        e,e                           ;[1bfb] 5b
                    ld        sp,$5bff                      ;[1bfc] 31 ff 5b
                    call      $20d2                         ;[1bff] cd d2 20
                    ld        a,$05                         ;[1c02] 3e 05
                    call      $1c64                         ;[1c04] cd 64 1c
                    ld        sp,($5b81)                    ;[1c07] ed 7b 81 5b
                    ret                                     ;[1c0b] c9

                    rst       $28                           ;[1c0c] ef
                    jr        $1c0f                         ;[1c0d] 18 00
                    cp        $21                           ;[1c0f] fe 21
                    jp        nz,$1912                      ;[1c11] c2 12 19
                    call      $1393                         ;[1c14] cd 93 13
                    call      $18a1                         ;[1c17] cd a1 18
                    ld        ($5b81),sp                    ;[1c1a] ed 73 81 5b
                    ld        sp,$5bff                      ;[1c1e] 31 ff 5b
                    call      $1f5f                         ;[1c21] cd 5f 1f
                    ld        a,$05                         ;[1c24] 3e 05
                    call      $1c64                         ;[1c26] cd 64 1c
                    ld        sp,($5b81)                    ;[1c29] ed 7b 81 5b
                    ret                                     ;[1c2d] c9

                    ld        ($5b81),sp                    ;[1c2e] ed 73 81 5b
                    ld        sp,$5bff                      ;[1c32] 31 ff 5b
                    call      $1d35                         ;[1c35] cd 35 1d
                    ld        hl,$5b71                      ;[1c38] 21 71 5b
                    ld        bc,$0009                      ;[1c3b] 01 09 00
                    call      $1e37                         ;[1c3e] cd 37 1e
                    ld        a,$05                         ;[1c41] 3e 05
                    call      $1c64                         ;[1c43] cd 64 1c
                    ld        sp,($5b81)                    ;[1c46] ed 7b 81 5b
                    ret                                     ;[1c4a] c9

                    ld        ($5b81),sp                    ;[1c4b] ed 73 81 5b
                    ld        sp,$5bff                      ;[1c4f] 31 ff 5b
                    ld        b,d                           ;[1c52] 42
                    ld        c,e                           ;[1c53] 4b
                    call      $1e37                         ;[1c54] cd 37 1e
                    call      $1d56                         ;[1c57] cd 56 1d
                    ld        a,$05                         ;[1c5a] 3e 05
                    call      $1c64                         ;[1c5c] cd 64 1c
                    ld        sp,($5b81)                    ;[1c5f] ed 7b 81 5b
                    ret                                     ;[1c63] c9

                    push      hl                            ;[1c64] e5
                    push      bc                            ;[1c65] c5
                    ld        hl,$1c81                      ;[1c66] 21 81 1c
                    ld        b,$00                         ;[1c69] 06 00
                    ld        c,a                           ;[1c6b] 4f
                    add       hl,bc                         ;[1c6c] 09
                    ld        c,(hl)                        ;[1c6d] 4e
                    di                                      ;[1c6e] f3
                    ld        a,($5b5c)                     ;[1c6f] 3a 5c 5b
                    and       $f8                           ;[1c72] e6 f8
                    or        c                             ;[1c74] b1
                    ld        ($5b5c),a                     ;[1c75] 32 5c 5b
                    ld        bc,$7ffd                      ;[1c78] 01 fd 7f
                    out       (c),a                         ;[1c7b] ed 79
                    ei                                      ;[1c7d] fb
                    pop       bc                            ;[1c7e] c1
                    pop       hl                            ;[1c7f] e1
                    ret                                     ;[1c80] c9

                    ld        bc,$0403                      ;[1c81] 01 03 04
                    ld        b,$07                         ;[1c84] 06 07
                    nop                                     ;[1c86] 00
                    ld        de,$5b67                      ;[1c87] 11 67 5b
                    push      ix                            ;[1c8a] dd e5
                    pop       hl                            ;[1c8c] e1
                    ld        b,$0a                         ;[1c8d] 06 0a
                    ld        a,(de)                        ;[1c8f] 1a
                    inc       de                            ;[1c90] 13
                    cp        (hl)                          ;[1c91] be
                    inc       hl                            ;[1c92] 23
                    ret       nz                            ;[1c93] c0
                    djnz      $1c8f                         ;[1c94] 10 f9
                    ret                                     ;[1c96] c9

                    call      $1d12                         ;[1c97] cd 12 1d
                    jr        z,$1ca0                       ;[1c9a] 28 04
                    call      $05ac                         ;[1c9c] cd ac 05
                    jr        nz,$1c7e                      ;[1c9f] 20 dd
                    push      hl                            ;[1ca1] e5
                    ld        bc,$3fec                      ;[1ca2] 01 ec 3f
                    add       ix,bc                         ;[1ca5] dd 09
                    pop       ix                            ;[1ca7] dd e1
                    jr        nc,$1d0e                      ;[1ca9] 30 63
                    ld        hl,$ffec                      ;[1cab] 21 ec ff
                    ld        a,$ff                         ;[1cae] 3e ff
                    call      $1cf3                         ;[1cb0] cd f3 1c
                    ld        hl,$5b66                      ;[1cb3] 21 66 5b
                    set       2,(hl)                        ;[1cb6] cb d6
                    push      ix                            ;[1cb8] dd e5
                    pop       de                            ;[1cba] d1
                    ld        hl,$5b67                      ;[1cbb] 21 67 5b
                    ld        bc,$000a                      ;[1cbe] 01 0a 00
                    ldir                                    ;[1cc1] ed b0
                    set       0,(ix+$13)                    ;[1cc3] dd cb 13 c6
                    ld        a,(ix+$0a)                    ;[1cc7] dd 7e 0a
                    ld        (ix+$10),a                    ;[1cca] dd 77 10
                    ld        a,(ix+$0b)                    ;[1ccd] dd 7e 0b
                    ld        (ix+$11),a                    ;[1cd0] dd 77 11
                    ld        a,(ix+$0c)                    ;[1cd3] dd 7e 0c
                    ld        (ix+$12),a                    ;[1cd6] dd 77 12
                    xor       a                             ;[1cd9] af
                    ld        (ix+$0d),a                    ;[1cda] dd 77 0d
                    ld        (ix+$0e),a                    ;[1cdd] dd 77 0e
                    ld        (ix+$0f),a                    ;[1ce0] dd 77 0f
                    ld        a,$05                         ;[1ce3] 3e 05
                    call      $1c64                         ;[1ce5] cd 64 1c
                    push      ix                            ;[1ce8] dd e5
                    pop       hl                            ;[1cea] e1
                    ld        bc,$ffec                      ;[1ceb] 01 ec ff
                    add       hl,bc                         ;[1cee] 09
                    ld        ($5b83),hl                    ;[1cef] 22 83 5b
                    ret                                     ;[1cf2] c9

                    ld        de,($5b85)                    ;[1cf3] ed 5b 85 5b
                    ex        af,af'                        ;[1cf7] 08
                    ld        a,($5b87)                     ;[1cf8] 3a 87 5b
                    ld        c,a                           ;[1cfb] 4f
                    ex        af,af'                        ;[1cfc] 08
                    bit       7,a                           ;[1cfd] cb 7f
                    jr        nz,$1d0a                      ;[1cff] 20 09
                    add       hl,de                         ;[1d01] 19
                    adc       c                             ;[1d02] 89
                    ld        ($5b85),hl                    ;[1d03] 22 85 5b
                    ld        ($5b87),a                     ;[1d06] 32 87 5b
                    ret                                     ;[1d09] c9

                    add       hl,de                         ;[1d0a] 19
                    adc       c                             ;[1d0b] 89
                    jr        c,$1d03                       ;[1d0c] 38 f5
                    call      $05ac                         ;[1d0e] cd ac 05
                    inc       bc                            ;[1d11] 03
                    ld        a,$04                         ;[1d12] 3e 04
                    call      $1c64                         ;[1d14] cd 64 1c
                    ld        ix,$ebec                      ;[1d17] dd 21 ec eb
                    ld        de,($5b83)                    ;[1d1b] ed 5b 83 5b
                    or        a                             ;[1d1f] b7
                    push      ix                            ;[1d20] dd e5
                    pop       hl                            ;[1d22] e1
                    sbc       hl,de                         ;[1d23] ed 52
                    ret       z                             ;[1d25] c8
                    call      $1c87                         ;[1d26] cd 87 1c
                    jr        nz,$1d2e                      ;[1d29] 20 03
                    or        $ff                           ;[1d2b] f6 ff
                    ret                                     ;[1d2d] c9

                    ld        bc,$ffec                      ;[1d2e] 01 ec ff
                    add       ix,bc                         ;[1d31] dd 09
                    jr        $1d1b                         ;[1d33] 18 e6
                    call      $1d12                         ;[1d35] cd 12 1d
                    jr        nz,$1d3e                      ;[1d38] 20 04
                    call      $05ac                         ;[1d3a] cd ac 05
                    inc       hl                            ;[1d3d] 23
                    ld        a,(ix+$0a)                    ;[1d3e] dd 7e 0a
                    ld        (ix+$10),a                    ;[1d41] dd 77 10
                    ld        a,(ix+$0b)                    ;[1d44] dd 7e 0b
                    ld        (ix+$11),a                    ;[1d47] dd 77 11
                    ld        a,(ix+$0c)                    ;[1d4a] dd 7e 0c
                    ld        (ix+$12),a                    ;[1d4d] dd 77 12
                    ld        a,$05                         ;[1d50] 3e 05
                    call      $1c64                         ;[1d52] cd 64 1c
                    ret                                     ;[1d55] c9

                    ld        a,$04                         ;[1d56] 3e 04
                    call      $1c64                         ;[1d58] cd 64 1c
                    bit       0,(ix+$13)                    ;[1d5b] dd cb 13 46
                    ret       z                             ;[1d5f] c8
                    res       0,(ix+$13)                    ;[1d60] dd cb 13 86
                    ld        hl,$5b66                      ;[1d64] 21 66 5b
                    res       2,(hl)                        ;[1d67] cb 96
                    ld        l,(ix+$10)                    ;[1d69] dd 6e 10
                    ld        h,(ix+$11)                    ;[1d6c] dd 66 11
                    ld        a,(ix+$12)                    ;[1d6f] dd 7e 12
                    ld        e,(ix+$0a)                    ;[1d72] dd 5e 0a
                    ld        d,(ix+$0b)                    ;[1d75] dd 56 0b
                    ld        b,(ix+$0c)                    ;[1d78] dd 46 0c
                    or        a                             ;[1d7b] b7
                    sbc       hl,de                         ;[1d7c] ed 52
                    sbc       b                             ;[1d7e] 98
                    rl        h                             ;[1d7f] cb 14
                    rl        h                             ;[1d81] cb 14
                    sra       a                             ;[1d83] cb 2f
                    rr        h                             ;[1d85] cb 1c
                    sra       a                             ;[1d87] cb 2f
                    rr        h                             ;[1d89] cb 1c
                    ld        (ix+$0d),l                    ;[1d8b] dd 75 0d
                    ld        (ix+$0e),h                    ;[1d8e] dd 74 0e
                    ld        (ix+$0f),a                    ;[1d91] dd 77 0f
                    ld        l,(ix+$10)                    ;[1d94] dd 6e 10
                    ld        h,(ix+$11)                    ;[1d97] dd 66 11
                    ld        a,(ix+$12)                    ;[1d9a] dd 7e 12
                    ld        bc,$ffec                      ;[1d9d] 01 ec ff
                    add       ix,bc                         ;[1da0] dd 09
                    ld        (ix+$0a),l                    ;[1da2] dd 75 0a
                    ld        (ix+$0b),h                    ;[1da5] dd 74 0b
                    ld        (ix+$0c),a                    ;[1da8] dd 77 0c
                    ret                                     ;[1dab] c9

                    ld        a,b                           ;[1dac] 78
                    or        c                             ;[1dad] b1
                    ret       z                             ;[1dae] c8
                    push      hl                            ;[1daf] e5
                    ld        de,$c000                      ;[1db0] 11 00 c0
                    ex        de,hl                         ;[1db3] eb
                    sbc       hl,de                         ;[1db4] ed 52
                    jr        z,$1dd5                       ;[1db6] 28 1d
                    jr        c,$1dd5                       ;[1db8] 38 1b
                    push      hl                            ;[1dba] e5
                    sbc       hl,bc                         ;[1dbb] ed 42
                    jr        nc,$1dcc                      ;[1dbd] 30 0d
                    ld        h,b                           ;[1dbf] 60
                    ld        l,c                           ;[1dc0] 69
                    pop       bc                            ;[1dc1] c1
                    or        a                             ;[1dc2] b7
                    sbc       hl,bc                         ;[1dc3] ed 42
                    ex        (sp),hl                       ;[1dc5] e3
                    ld        de,$c000                      ;[1dc6] 11 00 c0
                    push      de                            ;[1dc9] d5
                    jr        $1df4                         ;[1dca] 18 28
                    pop       hl                            ;[1dcc] e1
                    pop       hl                            ;[1dcd] e1
                    ld        de,$0000                      ;[1dce] 11 00 00
                    push      de                            ;[1dd1] d5
                    push      de                            ;[1dd2] d5
                    jr        $1df4                         ;[1dd3] 18 1f
                    ld        h,b                           ;[1dd5] 60
                    ld        l,c                           ;[1dd6] 69
                    ld        de,$0020                      ;[1dd7] 11 20 00
                    or        a                             ;[1dda] b7
                    sbc       hl,de                         ;[1ddb] ed 52
                    jr        c,$1de4                       ;[1ddd] 38 05
                    ex        (sp),hl                       ;[1ddf] e3
                    ld        b,d                           ;[1de0] 42
                    ld        c,e                           ;[1de1] 4b
                    jr        $1de9                         ;[1de2] 18 05
                    pop       hl                            ;[1de4] e1
                    ld        de,$0000                      ;[1de5] 11 00 00
                    push      de                            ;[1de8] d5
                    push      bc                            ;[1de9] c5
                    ld        de,$5b98                      ;[1dea] 11 98 5b
                    ldir                                    ;[1ded] ed b0
                    pop       bc                            ;[1def] c1
                    push      hl                            ;[1df0] e5
                    ld        hl,$5b98                      ;[1df1] 21 98 5b
                    ld        a,$04                         ;[1df4] 3e 04
                    call      $1c64                         ;[1df6] cd 64 1c
                    ld        e,(ix+$10)                    ;[1df9] dd 5e 10
                    ld        d,(ix+$11)                    ;[1dfc] dd 56 11
                    ld        a,(ix+$12)                    ;[1dff] dd 7e 12
                    call      $1c64                         ;[1e02] cd 64 1c
                    ldi                                     ;[1e05] ed a0
                    ld        a,d                           ;[1e07] 7a
                    or        e                             ;[1e08] b3
                    jr        z,$1e24                       ;[1e09] 28 19
                    ld        a,b                           ;[1e0b] 78
                    or        c                             ;[1e0c] b1
                    jp        nz,$1e05                      ;[1e0d] c2 05 1e
                    ld        a,$04                         ;[1e10] 3e 04
                    call      $1c64                         ;[1e12] cd 64 1c
                    ld        (ix+$10),e                    ;[1e15] dd 73 10
                    ld        (ix+$11),d                    ;[1e18] dd 72 11
                    ld        a,$05                         ;[1e1b] 3e 05
                    call      $1c64                         ;[1e1d] cd 64 1c
                    pop       hl                            ;[1e20] e1
                    pop       bc                            ;[1e21] c1
                    jr        $1dac                         ;[1e22] 18 88
                    ld        a,$04                         ;[1e24] 3e 04
                    call      $1c64                         ;[1e26] cd 64 1c
                    inc       (ix+$12)                      ;[1e29] dd 34 12
                    ld        a,(ix+$12)                    ;[1e2c] dd 7e 12
                    ld        de,$c000                      ;[1e2f] 11 00 c0
                    call      $1c64                         ;[1e32] cd 64 1c
                    jr        $1e0b                         ;[1e35] 18 d4
                    ld        a,b                           ;[1e37] 78
                    or        c                             ;[1e38] b1
                    ret       z                             ;[1e39] c8
                    push      hl                            ;[1e3a] e5
                    ld        de,$c000                      ;[1e3b] 11 00 c0
                    ex        de,hl                         ;[1e3e] eb
                    sbc       hl,de                         ;[1e3f] ed 52
                    jr        z,$1e67                       ;[1e41] 28 24
                    jr        c,$1e67                       ;[1e43] 38 22
                    push      hl                            ;[1e45] e5
                    sbc       hl,bc                         ;[1e46] ed 42
                    jr        nc,$1e5c                      ;[1e48] 30 12
                    ld        h,b                           ;[1e4a] 60
                    ld        l,c                           ;[1e4b] 69
                    pop       bc                            ;[1e4c] c1
                    or        a                             ;[1e4d] b7
                    sbc       hl,bc                         ;[1e4e] ed 42
                    ex        (sp),hl                       ;[1e50] e3
                    ld        de,$0000                      ;[1e51] 11 00 00
                    push      de                            ;[1e54] d5
                    ld        de,$c000                      ;[1e55] 11 00 c0
                    push      de                            ;[1e58] d5
                    ex        de,hl                         ;[1e59] eb
                    jr        $1e80                         ;[1e5a] 18 24
                    pop       hl                            ;[1e5c] e1
                    pop       hl                            ;[1e5d] e1
                    ld        de,$0000                      ;[1e5e] 11 00 00
                    push      de                            ;[1e61] d5
                    push      de                            ;[1e62] d5
                    push      de                            ;[1e63] d5
                    ex        de,hl                         ;[1e64] eb
                    jr        $1e80                         ;[1e65] 18 19
                    ld        h,b                           ;[1e67] 60
                    ld        l,c                           ;[1e68] 69
                    ld        de,$0020                      ;[1e69] 11 20 00
                    or        a                             ;[1e6c] b7
                    sbc       hl,de                         ;[1e6d] ed 52
                    jr        c,$1e76                       ;[1e6f] 38 05
                    ex        (sp),hl                       ;[1e71] e3
                    ld        b,d                           ;[1e72] 42
                    ld        c,e                           ;[1e73] 4b
                    jr        $1e7b                         ;[1e74] 18 05
                    pop       hl                            ;[1e76] e1
                    ld        de,$0000                      ;[1e77] 11 00 00
                    push      de                            ;[1e7a] d5
                    push      bc                            ;[1e7b] c5
                    push      hl                            ;[1e7c] e5
                    ld        de,$5b98                      ;[1e7d] 11 98 5b
                    ld        a,$04                         ;[1e80] 3e 04
                    call      $1c64                         ;[1e82] cd 64 1c
                    ld        l,(ix+$10)                    ;[1e85] dd 6e 10
                    ld        h,(ix+$11)                    ;[1e88] dd 66 11
                    ld        a,(ix+$12)                    ;[1e8b] dd 7e 12
                    call      $1c64                         ;[1e8e] cd 64 1c
                    ldi                                     ;[1e91] ed a0
                    ld        a,h                           ;[1e93] 7c
                    or        l                             ;[1e94] b5
                    jr        z,$1ebc                       ;[1e95] 28 25
                    ld        a,b                           ;[1e97] 78
                    or        c                             ;[1e98] b1
                    jp        nz,$1e91                      ;[1e99] c2 91 1e
                    ld        a,$04                         ;[1e9c] 3e 04
                    call      $1c64                         ;[1e9e] cd 64 1c
                    ld        (ix+$10),l                    ;[1ea1] dd 75 10
                    ld        (ix+$11),h                    ;[1ea4] dd 74 11
                    ld        a,$05                         ;[1ea7] 3e 05
                    call      $1c64                         ;[1ea9] cd 64 1c
                    pop       de                            ;[1eac] d1
                    pop       bc                            ;[1ead] c1
                    ld        hl,$5b98                      ;[1eae] 21 98 5b
                    ld        a,b                           ;[1eb1] 78
                    or        c                             ;[1eb2] b1
                    jr        z,$1eb7                       ;[1eb3] 28 02
                    ldir                                    ;[1eb5] ed b0
                    ex        de,hl                         ;[1eb7] eb
                    pop       bc                            ;[1eb8] c1
                    jp        $1e37                         ;[1eb9] c3 37 1e
                    ld        a,$04                         ;[1ebc] 3e 04
                    call      $1c64                         ;[1ebe] cd 64 1c
                    inc       (ix+$12)                      ;[1ec1] dd 34 12
                    ld        a,(ix+$12)                    ;[1ec4] dd 7e 12
                    ld        hl,$c000                      ;[1ec7] 21 00 c0
                    call      $1c64                         ;[1eca] cd 64 1c
                    jr        $1e97                         ;[1ecd] 18 c8
                    push      af                            ;[1ecf] f5
                    ld        a,($5b5c)                     ;[1ed0] 3a 5c 5b
                    push      af                            ;[1ed3] f5
                    push      hl                            ;[1ed4] e5
                    push      de                            ;[1ed5] d5
                    push      bc                            ;[1ed6] c5
                    ld        ix,$5b6a                      ;[1ed7] dd 21 6a 5b
                    ld        (ix+$10),e                    ;[1edb] dd 73 10
                    ld        (ix+$11),d                    ;[1ede] dd 72 11
                    ld        (ix+$12),$04                  ;[1ee1] dd 36 12 04
                    call      $1dac                         ;[1ee5] cd ac 1d
                    ld        a,$05                         ;[1ee8] 3e 05
                    call      $1c64                         ;[1eea] cd 64 1c
                    pop       bc                            ;[1eed] c1
                    pop       de                            ;[1eee] d1
                    pop       hl                            ;[1eef] e1
                    add       hl,bc                         ;[1ef0] 09
                    ex        de,hl                         ;[1ef1] eb
                    add       hl,bc                         ;[1ef2] 09
                    ex        de,hl                         ;[1ef3] eb
                    pop       af                            ;[1ef4] f1
                    ld        bc,$7ffd                      ;[1ef5] 01 fd 7f
                    di                                      ;[1ef8] f3
                    out       (c),a                         ;[1ef9] ed 79
                    ld        ($5b5c),a                     ;[1efb] 32 5c 5b
                    ei                                      ;[1efe] fb
                    ld        bc,$0000                      ;[1eff] 01 00 00
                    pop       af                            ;[1f02] f1
                    ret                                     ;[1f03] c9

                    push      af                            ;[1f04] f5
                    ld        a,($5b5c)                     ;[1f05] 3a 5c 5b
                    push      af                            ;[1f08] f5
                    push      hl                            ;[1f09] e5
                    push      de                            ;[1f0a] d5
                    push      bc                            ;[1f0b] c5
                    ld        ix,$5b6a                      ;[1f0c] dd 21 6a 5b
                    ld        (ix+$10),l                    ;[1f10] dd 75 10
                    ld        (ix+$11),h                    ;[1f13] dd 74 11
                    ld        (ix+$12),$04                  ;[1f16] dd 36 12 04
                    ex        de,hl                         ;[1f1a] eb
                    call      $1e37                         ;[1f1b] cd 37 1e
                    jr        $1ee8                         ;[1f1e] 18 c8
                    ex        af,af'                        ;[1f20] 08
                    ld        a,$00                         ;[1f21] 3e 00
                    di                                      ;[1f23] f3
                    call      $1f3a                         ;[1f24] cd 3a 1f
                    pop       af                            ;[1f27] f1
                    ld        ($5b58),hl                    ;[1f28] 22 58 5b
                    ld        hl,($5b81)                    ;[1f2b] 2a 81 5b
                    ld        ($5b81),sp                    ;[1f2e] ed 73 81 5b
                    ld        sp,hl                         ;[1f32] f9
                    ei                                      ;[1f33] fb
                    ld        hl,($5b58)                    ;[1f34] 2a 58 5b
                    push      af                            ;[1f37] f5
                    ex        af,af'                        ;[1f38] 08
                    ret                                     ;[1f39] c9

                    push      bc                            ;[1f3a] c5
                    ld        bc,$7ffd                      ;[1f3b] 01 fd 7f
                    out       (c),a                         ;[1f3e] ed 79
                    ld        ($5b5c),a                     ;[1f40] 32 5c 5b
                    pop       bc                            ;[1f43] c1
                    ret                                     ;[1f44] c9

                    ex        af,af'                        ;[1f45] 08
                    di                                      ;[1f46] f3
                    pop       af                            ;[1f47] f1
                    ld        ($5b58),hl                    ;[1f48] 22 58 5b
                    ld        hl,($5b81)                    ;[1f4b] 2a 81 5b
                    ld        ($5b81),sp                    ;[1f4e] ed 73 81 5b
                    ld        sp,hl                         ;[1f52] f9
                    ld        hl,($5b58)                    ;[1f53] 2a 58 5b
                    push      af                            ;[1f56] f5
                    ld        a,$07                         ;[1f57] 3e 07
                    call      $1f3a                         ;[1f59] cd 3a 1f
                    ei                                      ;[1f5c] fb
                    ex        af,af'                        ;[1f5d] 08
                    ret                                     ;[1f5e] c9

                    call      $1d12                         ;[1f5f] cd 12 1d
                    jr        nz,$1f68                      ;[1f62] 20 04
                    call      $05ac                         ;[1f64] cd ac 05
                    inc       hl                            ;[1f67] 23
                    ld        l,(ix+$0d)                    ;[1f68] dd 6e 0d
                    ld        h,(ix+$0e)                    ;[1f6b] dd 66 0e
                    ld        a,(ix+$0f)                    ;[1f6e] dd 7e 0f
                    call      $1cf3                         ;[1f71] cd f3 1c
                    push      iy                            ;[1f74] fd e5
                    ld        iy,($5b83)                    ;[1f76] fd 2a 83 5b
                    ld        bc,$ffec                      ;[1f7a] 01 ec ff
                    add       ix,bc                         ;[1f7d] dd 09
                    ld        l,(iy+$0a)                    ;[1f7f] fd 6e 0a
                    ld        h,(iy+$0b)                    ;[1f82] fd 66 0b
                    ld        a,(iy+$0c)                    ;[1f85] fd 7e 0c
                    pop       iy                            ;[1f88] fd e1
                    ld        e,(ix+$0a)                    ;[1f8a] dd 5e 0a
                    ld        d,(ix+$0b)                    ;[1f8d] dd 56 0b
                    ld        b,(ix+$0c)                    ;[1f90] dd 46 0c
                    or        a                             ;[1f93] b7
                    sbc       hl,de                         ;[1f94] ed 52
                    sbc       b                             ;[1f96] 98
                    rl        h                             ;[1f97] cb 14
                    rl        h                             ;[1f99] cb 14
                    sra       a                             ;[1f9b] cb 2f
                    rr        h                             ;[1f9d] cb 1c
                    sra       a                             ;[1f9f] cb 2f
                    rr        h                             ;[1fa1] cb 1c
                    ld        bc,$0014                      ;[1fa3] 01 14 00
                    add       ix,bc                         ;[1fa6] dd 09
                    ld        (ix+$10),l                    ;[1fa8] dd 75 10
                    ld        (ix+$11),h                    ;[1fab] dd 74 11
                    ld        (ix+$12),a                    ;[1fae] dd 77 12
                    ld        bc,$ffec                      ;[1fb1] 01 ec ff
                    add       ix,bc                         ;[1fb4] dd 09
                    ld        l,(ix+$0a)                    ;[1fb6] dd 6e 0a
                    ld        h,(ix+$0b)                    ;[1fb9] dd 66 0b
                    ld        d,(ix+$0c)                    ;[1fbc] dd 56 0c
                    ld        bc,$0014                      ;[1fbf] 01 14 00
                    add       ix,bc                         ;[1fc2] dd 09
                    ld        a,d                           ;[1fc4] 7a
                    call      $1c64                         ;[1fc5] cd 64 1c
                    ld        a,($5b5c)                     ;[1fc8] 3a 5c 5b
                    ld        e,a                           ;[1fcb] 5f
                    ld        bc,$7ffd                      ;[1fcc] 01 fd 7f
                    ld        a,$07                         ;[1fcf] 3e 07
                    di                                      ;[1fd1] f3
                    out       (c),a                         ;[1fd2] ed 79
                    exx                                     ;[1fd4] d9
                    ld        l,(ix+$0a)                    ;[1fd5] dd 6e 0a
                    ld        h,(ix+$0b)                    ;[1fd8] dd 66 0b
                    ld        d,(ix+$0c)                    ;[1fdb] dd 56 0c
                    ld        a,d                           ;[1fde] 7a
                    call      $1c64                         ;[1fdf] cd 64 1c
                    ld        a,($5b5c)                     ;[1fe2] 3a 5c 5b
                    ld        e,a                           ;[1fe5] 5f
                    ld        bc,$7ffd                      ;[1fe6] 01 fd 7f
                    exx                                     ;[1fe9] d9
                    ld        a,$07                         ;[1fea] 3e 07
                    di                                      ;[1fec] f3
                    out       (c),a                         ;[1fed] ed 79
                    ld        a,(ix+$10)                    ;[1fef] dd 7e 10
                    sub       $01                           ;[1ff2] d6 01
                    ld        (ix+$10),a                    ;[1ff4] dd 77 10
                    jr        nc,$200d                      ;[1ff7] 30 14
                    ld        a,(ix+$11)                    ;[1ff9] dd 7e 11
                    sub       $01                           ;[1ffc] d6 01
                    ld        (ix+$11),a                    ;[1ffe] dd 77 11
                    jr        nc,$200d                      ;[2001] 30 0a
                    ld        a,(ix+$12)                    ;[2003] dd 7e 12
                    sub       $01                           ;[2006] d6 01
                    ld        (ix+$12),a                    ;[2008] dd 77 12
                    jr        c,$203e                       ;[200b] 38 31
                    out       (c),e                         ;[200d] ed 59
                    ld        a,(hl)                        ;[200f] 7e
                    inc       l                             ;[2010] 2c
                    jr        nz,$2024                      ;[2011] 20 11
                    inc       h                             ;[2013] 24
                    jr        nz,$2024                      ;[2014] 20 0e
                    ex        af,af'                        ;[2016] 08
                    inc       d                             ;[2017] 14
                    ld        a,d                           ;[2018] 7a
                    call      $1c64                         ;[2019] cd 64 1c
                    ld        a,($5b5c)                     ;[201c] 3a 5c 5b
                    ld        e,a                           ;[201f] 5f
                    ld        hl,$c000                      ;[2020] 21 00 c0
                    ex        af,af'                        ;[2023] 08
                    exx                                     ;[2024] d9
                    di                                      ;[2025] f3
                    out       (c),e                         ;[2026] ed 59
                    ld        (hl),a                        ;[2028] 77
                    inc       l                             ;[2029] 2c
                    jr        nz,$203b                      ;[202a] 20 0f
                    inc       h                             ;[202c] 24
                    jr        nz,$203b                      ;[202d] 20 0c
                    inc       d                             ;[202f] 14
                    ld        a,d                           ;[2030] 7a
                    call      $1c64                         ;[2031] cd 64 1c
                    ld        a,($5b5c)                     ;[2034] 3a 5c 5b
                    ld        e,a                           ;[2037] 5f
                    ld        hl,$c000                      ;[2038] 21 00 c0
                    exx                                     ;[203b] d9
                    jr        $1fea                         ;[203c] 18 ac
                    ld        a,$04                         ;[203e] 3e 04
                    call      $1c64                         ;[2040] cd 64 1c
                    ld        a,$00                         ;[2043] 3e 00
                    ld        hl,$0014                      ;[2045] 21 14 00
                    call      $1cf3                         ;[2048] cd f3 1c
                    ld        e,(ix+$0d)                    ;[204b] dd 5e 0d
                    ld        d,(ix+$0e)                    ;[204e] dd 56 0e
                    ld        c,(ix+$0f)                    ;[2051] dd 4e 0f
                    ld        a,d                           ;[2054] 7a
                    rlca                                    ;[2055] 07
                    rl        c                             ;[2056] cb 11
                    rlca                                    ;[2058] 07
                    rl        c                             ;[2059] cb 11
                    ld        a,d                           ;[205b] 7a
                    and       $3f                           ;[205c] e6 3f
                    ld        d,a                           ;[205e] 57
                    push      ix                            ;[205f] dd e5
                    push      de                            ;[2061] d5
                    ld        de,$ffec                      ;[2062] 11 ec ff
                    add       ix,de                         ;[2065] dd 19
                    pop       de                            ;[2067] d1
                    ld        l,(ix+$0a)                    ;[2068] dd 6e 0a
                    ld        h,(ix+$0b)                    ;[206b] dd 66 0b
                    ld        a,(ix+$0c)                    ;[206e] dd 7e 0c
                    or        a                             ;[2071] b7
                    sbc       hl,de                         ;[2072] ed 52
                    sub       c                             ;[2074] 91
                    bit       6,h                           ;[2075] cb 74
                    jr        nz,$207c                      ;[2077] 20 03
                    set       6,h                           ;[2079] cb f4
                    dec       a                             ;[207b] 3d
                    ld        (ix+$0a),l                    ;[207c] dd 75 0a
                    ld        (ix+$0b),h                    ;[207f] dd 74 0b
                    ld        (ix+$0c),a                    ;[2082] dd 77 0c
                    ld        l,(ix+$10)                    ;[2085] dd 6e 10
                    ld        h,(ix+$11)                    ;[2088] dd 66 11
                    ld        a,(ix+$12)                    ;[208b] dd 7e 12
                    or        a                             ;[208e] b7
                    sbc       hl,de                         ;[208f] ed 52
                    sub       c                             ;[2091] 91
                    bit       6,h                           ;[2092] cb 74
                    jr        nz,$2099                      ;[2094] 20 03
                    set       6,h                           ;[2096] cb f4
                    dec       a                             ;[2098] 3d
                    ld        (ix+$10),l                    ;[2099] dd 75 10
                    ld        (ix+$11),h                    ;[209c] dd 74 11
                    ld        (ix+$12),a                    ;[209f] dd 77 12
                    push      ix                            ;[20a2] dd e5
                    pop       hl                            ;[20a4] e1
                    push      de                            ;[20a5] d5
                    ld        de,($5b83)                    ;[20a6] ed 5b 83 5b
                    or        a                             ;[20aa] b7
                    sbc       hl,de                         ;[20ab] ed 52
                    pop       de                            ;[20ad] d1
                    jr        nz,$2061                      ;[20ae] 20 b1
                    ld        de,($5b83)                    ;[20b0] ed 5b 83 5b
                    pop       hl                            ;[20b4] e1
                    push      hl                            ;[20b5] e5
                    or        a                             ;[20b6] b7
                    sbc       hl,de                         ;[20b7] ed 52
                    ld        b,h                           ;[20b9] 44
                    ld        c,l                           ;[20ba] 4d
                    pop       hl                            ;[20bb] e1
                    push      hl                            ;[20bc] e5
                    ld        de,$0014                      ;[20bd] 11 14 00
                    add       hl,de                         ;[20c0] 19
                    ex        de,hl                         ;[20c1] eb
                    pop       hl                            ;[20c2] e1
                    dec       de                            ;[20c3] 1b
                    dec       hl                            ;[20c4] 2b
                    lddr                                    ;[20c5] ed b8
                    ld        hl,($5b83)                    ;[20c7] 2a 83 5b
                    ld        de,$0014                      ;[20ca] 11 14 00
                    add       hl,de                         ;[20cd] 19
                    ld        ($5b83),hl                    ;[20ce] 22 83 5b
                    ret                                     ;[20d1] c9

                    ld        a,$04                         ;[20d2] 3e 04
                    call      $1c64                         ;[20d4] cd 64 1c
                    ld        hl,$2121                      ;[20d7] 21 21 21
                    ld        bc,$212b                      ;[20da] 01 2b 21
                    ld        ix,$ebec                      ;[20dd] dd 21 ec eb
                    call      $05d6                         ;[20e1] cd d6 05
                    push      ix                            ;[20e4] dd e5
                    ex        (sp),hl                       ;[20e6] e3
                    ld        de,($5b83)                    ;[20e7] ed 5b 83 5b
                    or        a                             ;[20eb] b7
                    sbc       hl,de                         ;[20ec] ed 52
                    pop       hl                            ;[20ee] e1
                    jr        z,$2111                       ;[20ef] 28 20
                    ld        d,h                           ;[20f1] 54
                    ld        e,l                           ;[20f2] 5d
                    push      hl                            ;[20f3] e5
                    push      bc                            ;[20f4] c5
                    call      $1c8a                         ;[20f5] cd 8a 1c
                    pop       bc                            ;[20f8] c1
                    pop       hl                            ;[20f9] e1
                    jr        nc,$210a                      ;[20fa] 30 0e
                    ld        d,b                           ;[20fc] 50
                    ld        e,c                           ;[20fd] 59
                    push      hl                            ;[20fe] e5
                    push      bc                            ;[20ff] c5
                    call      $1c8a                         ;[2100] cd 8a 1c
                    pop       bc                            ;[2103] c1
                    pop       hl                            ;[2104] e1
                    jr        c,$210a                       ;[2105] 38 03
                    push      ix                            ;[2107] dd e5
                    pop       bc                            ;[2109] c1
                    ld        de,$ffec                      ;[210a] 11 ec ff
                    add       ix,de                         ;[210d] dd 19
                    jr        $20e1                         ;[210f] 18 d0
                    push      hl                            ;[2111] e5
                    ld        hl,$212b                      ;[2112] 21 2b 21
                    or        a                             ;[2115] b7
                    sbc       hl,bc                         ;[2116] ed 42
                    pop       hl                            ;[2118] e1
                    ret       z                             ;[2119] c8
                    ld        h,b                           ;[211a] 60
                    ld        l,c                           ;[211b] 69
                    call      $2135                         ;[211c] cd 35 21
                    jr        $20da                         ;[211f] 18 b9
                    nop                                     ;[2121] 00
                    nop                                     ;[2122] 00
                    nop                                     ;[2123] 00
                    nop                                     ;[2124] 00
                    nop                                     ;[2125] 00
                    nop                                     ;[2126] 00
                    nop                                     ;[2127] 00
                    nop                                     ;[2128] 00
                    nop                                     ;[2129] 00
                    nop                                     ;[212a] 00
                    rst       $38                           ;[212b] ff
                    rst       $38                           ;[212c] ff
                    rst       $38                           ;[212d] ff
                    rst       $38                           ;[212e] ff
                    rst       $38                           ;[212f] ff
                    rst       $38                           ;[2130] ff
                    rst       $38                           ;[2131] ff
                    rst       $38                           ;[2132] ff
                    rst       $38                           ;[2133] ff
                    rst       $38                           ;[2134] ff
                    push      hl                            ;[2135] e5
                    push      bc                            ;[2136] c5
                    pop       hl                            ;[2137] e1
                    ld        de,$5b67                      ;[2138] 11 67 5b
                    ld        bc,$000a                      ;[213b] 01 0a 00
                    ldir                                    ;[213e] ed b0
                    ld        a,$05                         ;[2140] 3e 05
                    call      $1c64                         ;[2142] cd 64 1c
                    ld        hl,($5b81)                    ;[2145] 2a 81 5b
                    ld        ($5b81),sp                    ;[2148] ed 73 81 5b
                    ld        sp,hl                         ;[214c] f9
                    ld        hl,$5b67                      ;[214d] 21 67 5b
                    ld        b,$0a                         ;[2150] 06 0a
                    ld        a,(hl)                        ;[2152] 7e
                    push      hl                            ;[2153] e5
                    push      bc                            ;[2154] c5
                    rst       $28                           ;[2155] ef
                    djnz      $2158                         ;[2156] 10 00
                    pop       bc                            ;[2158] c1
                    pop       hl                            ;[2159] e1
                    inc       hl                            ;[215a] 23
                    djnz      $2152                         ;[215b] 10 f5
                    ld        a,$0d                         ;[215d] 3e 0d
                    rst       $28                           ;[215f] ef
                    djnz      $2162                         ;[2160] 10 00
                    rst       $28                           ;[2162] ef
                    ld        c,l                           ;[2163] 4d
                    dec       c                             ;[2164] 0d
                    ld        hl,($5b81)                    ;[2165] 2a 81 5b
                    ld        ($5b81),sp                    ;[2168] ed 73 81 5b
                    ld        sp,hl                         ;[216c] f9
                    ld        a,$04                         ;[216d] 3e 04
                    call      $1c64                         ;[216f] cd 64 1c
                    pop       hl                            ;[2172] e1
                    ret                                     ;[2173] c9

                    ld        a,$03                         ;[2174] 3e 03
                    jr        $217a                         ;[2176] 18 02
                    ld        a,$02                         ;[2178] 3e 02
                    rst       $28                           ;[217a] ef
                    jr        nc,$21a2                      ;[217b] 30 25
                    jr        z,$2182                       ;[217d] 28 03
                    rst       $28                           ;[217f] ef
                    ld        bc,$ef16                      ;[2180] 01 16 ef
                    ld        c,l                           ;[2183] 4d
                    dec       c                             ;[2184] 0d
                    rst       $28                           ;[2185] ef
                    rst       $18                           ;[2186] df
                    rra                                     ;[2187] 1f
                    call      $18a1                         ;[2188] cd a1 18
                    ret                                     ;[218b] c9

                    rst       $28                           ;[218c] ef
                    jr        nc,$21b4                      ;[218d] 30 25
                    jr        z,$2199                       ;[218f] 28 08
                    ld        a,$01                         ;[2191] 3e 01
                    rst       $28                           ;[2193] ef
                    ld        bc,$ef16                      ;[2194] 01 16 ef
                    ld        l,(hl)                        ;[2197] 6e
                    dec       c                             ;[2198] 0d
                    ld        (iy+$02),$01                  ;[2199] fd 36 02 01
                    rst       $28                           ;[219d] ef
                    pop       bc                            ;[219e] c1
                    jr        nz,$216e                      ;[219f] 20 cd
                    and       c                             ;[21a1] a1
                    jr        $2193                         ;[21a2] 18 ef
                    and       b                             ;[21a4] a0
                    jr        nz,$2170                      ;[21a5] 20 c9
                    jp        $08f0                         ;[21a7] c3 f0 08
                    di                                      ;[21aa] f3
                    jp        $019d                         ;[21ab] c3 9d 01
                    rst       $18                           ;[21ae] df
                    cp        $2c                           ;[21af] fe 2c
                    jr        nz,$21eb                      ;[21b1] 20 38
                    rst       $20                           ;[21b3] e7
                    rst       $28                           ;[21b4] ef
                    add       d                             ;[21b5] 82
                    inc       e                             ;[21b6] 1c
                    call      $18a1                         ;[21b7] cd a1 18
                    rst       $28                           ;[21ba] ef
                    dec       l                             ;[21bb] 2d
                    inc       hl                            ;[21bc] 23
                    ret                                     ;[21bd] c9

                    rst       $18                           ;[21be] df
                    cp        $2c                           ;[21bf] fe 2c
                    jr        z,$21ca                       ;[21c1] 28 07
                    call      $18a1                         ;[21c3] cd a1 18
                    rst       $28                           ;[21c6] ef
                    ld        (hl),a                        ;[21c7] 77
                    inc       h                             ;[21c8] 24
                    ret                                     ;[21c9] c9

                    rst       $20                           ;[21ca] e7
                    rst       $28                           ;[21cb] ef
                    add       d                             ;[21cc] 82
                    inc       e                             ;[21cd] 1c
                    call      $18a1                         ;[21ce] cd a1 18
                    rst       $28                           ;[21d1] ef
                    sub       h                             ;[21d2] 94
                    inc       hl                            ;[21d3] 23
                    ret                                     ;[21d4] c9

                    rst       $28                           ;[21d5] ef
                    or        d                             ;[21d6] b2
                    jr        z,$21f9                       ;[21d7] 28 20
                    ld        de,$30ef                      ;[21d9] 11 ef 30
                    dec       h                             ;[21dc] 25
                    jr        nz,$21e7                      ;[21dd] 20 08
                    res       6,c                           ;[21df] cb b1
                    rst       $28                           ;[21e1] ef
                    sub       (hl)                          ;[21e2] 96
                    add       hl,hl                         ;[21e3] 29
                    call      $18a1                         ;[21e4] cd a1 18
                    rst       $28                           ;[21e7] ef
                    dec       d                             ;[21e8] 15
                    inc       l                             ;[21e9] 2c
                    ret                                     ;[21ea] c9

                    call      $05ac                         ;[21eb] cd ac 05
                    dec       bc                            ;[21ee] 0b
                    bit       0,(iy+$30)                    ;[21ef] fd cb 30 46
                    ret       z                             ;[21f3] c8
                    rst       $28                           ;[21f4] ef
                    xor       a                             ;[21f5] af
                    dec       c                             ;[21f6] 0d
                    ret                                     ;[21f7] c9

                    ld        hl,$fffe                      ;[21f8] 21 fe ff
                    ld        ($5c45),hl                    ;[21fb] 22 45 5c
                    res       7,(iy+$01)                    ;[21fe] fd cb 01 be
                    call      $228e                         ;[2202] cd 8e 22
                    rst       $28                           ;[2205] ef
                    ei                                      ;[2206] fb
                    inc       h                             ;[2207] 24
                    bit       6,(iy+$01)                    ;[2208] fd cb 01 76
                    jr        z,$223a                       ;[220c] 28 2c
                    rst       $18                           ;[220e] df
                    cp        $0d                           ;[220f] fe 0d
                    jr        nz,$223a                      ;[2211] 20 27
                    set       7,(iy+$01)                    ;[2213] fd cb 01 fe
                    call      $228e                         ;[2217] cd 8e 22
                    ld        hl,$0321                      ;[221a] 21 21 03
                    ld        ($5b8b),hl                    ;[221d] 22 8b 5b
                    rst       $28                           ;[2220] ef
                    ei                                      ;[2221] fb
                    inc       h                             ;[2222] 24
                    bit       6,(iy+$01)                    ;[2223] fd cb 01 76
                    jr        z,$223a                       ;[2227] 28 11
                    ld        de,$5b8d                      ;[2229] 11 8d 5b
                    ld        hl,($5c65)                    ;[222c] 2a 65 5c
                    ld        bc,$0005                      ;[222f] 01 05 00
                    or        a                             ;[2232] b7
                    sbc       hl,bc                         ;[2233] ed 42
                    ldir                                    ;[2235] ed b0
                    jp        $223e                         ;[2237] c3 3e 22
                    call      $05ac                         ;[223a] cd ac 05
                    add       hl,de                         ;[223d] 19
                    ld        a,$0d                         ;[223e] 3e 0d
                    call      $226f                         ;[2240] cd 6f 22
                    ld        bc,$0001                      ;[2243] 01 01 00
                    rst       $28                           ;[2246] ef
                    jr        nc,$2249                      ;[2247] 30 00
                    ld        ($5c5b),hl                    ;[2249] 22 5b 5c
                    push      hl                            ;[224c] e5
                    ld        hl,($5c51)                    ;[224d] 2a 51 5c
                    push      hl                            ;[2250] e5
                    ld        a,$ff                         ;[2251] 3e ff
                    rst       $28                           ;[2253] ef
                    ld        bc,$ef16                      ;[2254] 01 16 ef
                    ex        (sp),hl                       ;[2257] e3
                    dec       l                             ;[2258] 2d
                    pop       hl                            ;[2259] e1
                    rst       $28                           ;[225a] ef
                    dec       d                             ;[225b] 15
                    ld        d,$d1                         ;[225c] 16 d1
                    ld        hl,($5c5b)                    ;[225e] 2a 5b 5c
                    and       a                             ;[2261] a7
                    sbc       hl,de                         ;[2262] ed 52
                    ld        a,(de)                        ;[2264] 1a
                    call      $226f                         ;[2265] cd 6f 22
                    inc       de                            ;[2268] 13
                    dec       hl                            ;[2269] 2b
                    ld        a,h                           ;[226a] 7c
                    or        l                             ;[226b] b5
                    jr        nz,$2264                      ;[226c] 20 f6
                    ret                                     ;[226e] c9

                    push      hl                            ;[226f] e5
                    push      de                            ;[2270] d5
                    call      $1f45                         ;[2271] cd 45 1f
                    ld        hl,$ec0d                      ;[2274] 21 0d ec
                    res       3,(hl)                        ;[2277] cb 9e
                    push      af                            ;[2279] f5
                    ld        a,$02                         ;[227a] 3e 02
                    rst       $28                           ;[227c] ef
                    ld        bc,$f116                      ;[227d] 01 16 f1
                    call      $2669                         ;[2280] cd 69 26
                    ld        hl,$ec0d                      ;[2283] 21 0d ec
                    res       3,(hl)                        ;[2286] cb 9e
                    call      $1f20                         ;[2288] cd 20 1f
                    pop       de                            ;[228b] d1
                    pop       hl                            ;[228c] e1
                    ret                                     ;[228d] c9

                    ld        hl,($5c59)                    ;[228e] 2a 59 5c
                    dec       hl                            ;[2291] 2b
                    ld        ($5c5d),hl                    ;[2292] 22 5d 5c
                    rst       $20                           ;[2295] e7
                    ret                                     ;[2296] c9

                    call      $228e                         ;[2297] cd 8e 22
                    cp        $f1                           ;[229a] fe f1
                    ret       nz                            ;[229c] c0
                    ld        hl,($5c5d)                    ;[229d] 2a 5d 5c
                    ld        a,(hl)                        ;[22a0] 7e
                    inc       hl                            ;[22a1] 23
                    cp        $0d                           ;[22a2] fe 0d
                    ret       z                             ;[22a4] c8
                    cp        $3a                           ;[22a5] fe 3a
                    jr        nz,$22a0                      ;[22a7] 20 f7
                    or        a                             ;[22a9] b7
                    ret                                     ;[22aa] c9

                    ld        b,a                           ;[22ab] 47
                    ld        hl,$22bd                      ;[22ac] 21 bd 22
                    ld        a,(hl)                        ;[22af] 7e
                    inc       hl                            ;[22b0] 23
                    or        a                             ;[22b1] b7
                    jr        z,$22b9                       ;[22b2] 28 05
                    cp        b                             ;[22b4] b8
                    jr        nz,$22af                      ;[22b5] 20 f8
                    ld        a,b                           ;[22b7] 78
                    ret                                     ;[22b8] c9

                    or        $ff                           ;[22b9] f6 ff
                    ld        a,b                           ;[22bb] 78
                    ret                                     ;[22bc] c9

                    dec       hl                            ;[22bd] 2b
                    dec       l                             ;[22be] 2d
                    ld        hl,($5e2f)                    ;[22bf] 2a 2f 5e
                    dec       a                             ;[22c2] 3d
                    ld        a,$3c                         ;[22c3] 3e 3c
                    rst       $00                           ;[22c5] c7
                    ret       z                             ;[22c6] c8
                    ret                                     ;[22c7] c9

                    push      bc                            ;[22c8] c5
                    add       $00                           ;[22c9] c6 00
                    cp        $a5                           ;[22cb] fe a5
                    jr        c,$22dd                       ;[22cd] 38 0e
                    cp        $c4                           ;[22cf] fe c4
                    jr        nc,$22dd                      ;[22d1] 30 0a
                    cp        $ac                           ;[22d3] fe ac
                    jr        z,$22dd                       ;[22d5] 28 06
                    cp        $ad                           ;[22d7] fe ad
                    jr        z,$22dd                       ;[22d9] 28 02
                    cp        a                             ;[22db] bf
                    ret                                     ;[22dc] c9

                    cp        $a5                           ;[22dd] fe a5
                    ret                                     ;[22df] c9

                    ld        b,a                           ;[22e0] 47
                    or        $20                           ;[22e1] f6 20
                    cp        $61                           ;[22e3] fe 61
                    jr        c,$22ed                       ;[22e5] 38 06
                    cp        $7b                           ;[22e7] fe 7b
                    jr        nc,$22ed                      ;[22e9] 30 02
                    cp        a                             ;[22eb] bf
                    ret                                     ;[22ec] c9

                    ld        a,b                           ;[22ed] 78
                    cp        $2e                           ;[22ee] fe 2e
                    ret       z                             ;[22f0] c8
                    call      $230a                         ;[22f1] cd 0a 23
                    jr        nz,$2307                      ;[22f4] 20 11
                    rst       $20                           ;[22f6] e7
                    call      $230a                         ;[22f7] cd 0a 23
                    jr        z,$22f6                       ;[22fa] 28 fa
                    cp        $2e                           ;[22fc] fe 2e
                    ret       z                             ;[22fe] c8
                    cp        $45                           ;[22ff] fe 45
                    ret       z                             ;[2301] c8
                    cp        $65                           ;[2302] fe 65
                    ret       z                             ;[2304] c8
                    jr        $22ab                         ;[2305] 18 a4
                    or        $ff                           ;[2307] f6 ff
                    ret                                     ;[2309] c9

                    cp        $30                           ;[230a] fe 30
                    jr        c,$2314                       ;[230c] 38 06
                    cp        $3a                           ;[230e] fe 3a
                    jr        nc,$2314                      ;[2310] 30 02
                    cp        a                             ;[2312] bf
                    ret                                     ;[2313] c9

                    cp        $30                           ;[2314] fe 30
                    ret                                     ;[2316] c9

                    ld        b,$00                         ;[2317] 06 00
                    rst       $18                           ;[2319] df
                    push      bc                            ;[231a] c5
                    rst       $28                           ;[231b] ef
                    adc       h                             ;[231c] 8c
                    inc       e                             ;[231d] 1c
                    pop       bc                            ;[231e] c1
                    inc       b                             ;[231f] 04
                    cp        $2c                           ;[2320] fe 2c
                    jr        nz,$2327                      ;[2322] 20 03
                    rst       $20                           ;[2324] e7
                    jr        $231a                         ;[2325] 18 f3
                    ld        a,b                           ;[2327] 78
                    cp        $09                           ;[2328] fe 09
                    jr        c,$2330                       ;[232a] 38 04
                    call      $05ac                         ;[232c] cd ac 05
                    dec       hl                            ;[232f] 2b
                    call      $18a1                         ;[2330] cd a1 18
                    jp        $0985                         ;[2333] c3 85 09
                    ld        hl,$5bff                      ;[2336] 21 ff 5b
                    ld        ($5b81),hl                    ;[2339] 22 81 5b
                    call      $1f45                         ;[233c] cd 45 1f
                    jp        $25cb                         ;[233f] c3 cb 25
                    and       a                             ;[2342] a7
                    sbc       hl,de                         ;[2343] ed 52
                    ld        b,h                           ;[2345] 44
                    ld        c,l                           ;[2346] 4d
                    add       hl,de                         ;[2347] 19
                    ex        de,hl                         ;[2348] eb
                    ret                                     ;[2349] c9

                    ld        bc,$0001                      ;[234a] 01 01 00
                    push      hl                            ;[234d] e5
                    push      de                            ;[234e] d5
                    call      $2358                         ;[234f] cd 58 23
                    pop       de                            ;[2352] d1
                    pop       hl                            ;[2353] e1
                    rst       $28                           ;[2354] ef
                    ld        d,l                           ;[2355] 55
                    ld        d,$c9                         ;[2356] 16 c9
                    ld        hl,($5c65)                    ;[2358] 2a 65 5c
                    add       hl,bc                         ;[235b] 09
                    jr        c,$2368                       ;[235c] 38 0a
                    ex        de,hl                         ;[235e] eb
                    ld        hl,$0082                      ;[235f] 21 82 00
                    add       hl,de                         ;[2362] 19
                    jr        c,$2368                       ;[2363] 38 03
                    sbc       hl,sp                         ;[2365] ed 72
                    ret       c                             ;[2367] d8
                    ld        (iy+$00),$03                  ;[2368] fd 36 00 03
                    jp        $0321                         ;[236c] c3 21 03
                    add       a                             ;[236f] 87
                    add       a                             ;[2370] 87
                    ld        l,a                           ;[2371] 6f
                    ld        h,$00                         ;[2372] 26 00
                    add       hl,hl                         ;[2374] 29
                    add       hl,hl                         ;[2375] 29
                    add       hl,hl                         ;[2376] 29
                    ret                                     ;[2377] c9

                    ld        hl,$0000                      ;[2378] 21 00 00
                    add       hl,sp                         ;[237b] 39
                    ld        de,($5c65)                    ;[237c] ed 5b 65 5c
                    or        a                             ;[2380] b7
                    sbc       hl,de                         ;[2381] ed 52
                    ret                                     ;[2383] c9

                    res       0,(iy-$39)                    ;[2384] fd cb c7 86
                    call      $236f                         ;[2388] cd 6f 23
                    push      hl                            ;[238b] e5
                    ld        de,($ff24)                    ;[238c] ed 5b 24 ff
                    add       hl,de                         ;[2390] 19
                    ld        d,h                           ;[2391] 54
                    ld        e,l                           ;[2392] 5d
                    ex        (sp),hl                       ;[2393] e3
                    push      hl                            ;[2394] e5
                    push      de                            ;[2395] d5
                    ld        de,$5800                      ;[2396] 11 00 58
                    add       hl,de                         ;[2399] 19
                    ex        de,hl                         ;[239a] eb
                    pop       hl                            ;[239b] e1
                    ld        bc,$0020                      ;[239c] 01 20 00
                    ld        a,($5c8f)                     ;[239f] 3a 8f 5c
                    call      $249b                         ;[23a2] cd 9b 24
                    pop       hl                            ;[23a5] e1
                    ld        a,h                           ;[23a6] 7c
                    ld        h,$00                         ;[23a7] 26 00
                    add       a                             ;[23a9] 87
                    add       a                             ;[23aa] 87
                    add       a                             ;[23ab] 87
                    add       $40                           ;[23ac] c6 40
                    ld        d,a                           ;[23ae] 57
                    ld        e,h                           ;[23af] 5c
                    add       hl,de                         ;[23b0] 19
                    ex        de,hl                         ;[23b1] eb
                    pop       hl                            ;[23b2] e1
                    ld        b,$20                         ;[23b3] 06 20
                    jp        $23e1                         ;[23b5] c3 e1 23
                    ld        d,$ff                         ;[23b8] 16 ff
                    call      $236f                         ;[23ba] cd 6f 23
                    ld        a,d                           ;[23bd] 7a
                    ld        de,($ff24)                    ;[23be] ed 5b 24 ff
                    add       hl,de                         ;[23c2] 19
                    ld        e,l                           ;[23c3] 5d
                    ld        d,h                           ;[23c4] 54
                    inc       de                            ;[23c5] 13
                    ld        (hl),a                        ;[23c6] 77
                    dec       bc                            ;[23c7] 0b
                    ldir                                    ;[23c8] ed b0
                    ret                                     ;[23ca] c9

                    call      $2488                         ;[23cb] cd 88 24
                    ld        de,$4000                      ;[23ce] 11 00 40
                    ld        hl,($ff24)                    ;[23d1] 2a 24 ff
                    ld        b,e                           ;[23d4] 43
                    call      $23e1                         ;[23d5] cd e1 23
                    ld        d,$48                         ;[23d8] 16 48
                    call      $23e1                         ;[23da] cd e1 23
                    ld        d,$50                         ;[23dd] 16 50
                    ld        b,$c0                         ;[23df] 06 c0
                    ld        a,(hl)                        ;[23e1] 7e
                    push      hl                            ;[23e2] e5
                    push      de                            ;[23e3] d5
                    cp        $fe                           ;[23e4] fe fe
                    jr        c,$23ec                       ;[23e6] 38 04
                    sub       $fe                           ;[23e8] d6 fe
                    jr        $2422                         ;[23ea] 18 36
                    cp        $20                           ;[23ec] fe 20
                    jr        nc,$23f7                      ;[23ee] 30 07
                    ld        hl,$2527                      ;[23f0] 21 27 25
                    and       a                             ;[23f3] a7
                    ex        af,af'                        ;[23f4] 08
                    jr        $242b                         ;[23f5] 18 34
                    cp        $80                           ;[23f7] fe 80
                    jr        nc,$2409                      ;[23f9] 30 0e
                    call      $2371                         ;[23fb] cd 71 23
                    ld        de,($5c36)                    ;[23fe] ed 5b 36 5c
                    add       hl,de                         ;[2402] 19
                    pop       de                            ;[2403] d1
                    call      $ff28                         ;[2404] cd 28 ff
                    jr        $2450                         ;[2407] 18 47
                    cp        $90                           ;[2409] fe 90
                    jr        nc,$2411                      ;[240b] 30 04
                    sub       $7f                           ;[240d] d6 7f
                    jr        $2422                         ;[240f] 18 11
                    sub       $90                           ;[2411] d6 90
                    call      $2371                         ;[2413] cd 71 23
                    pop       de                            ;[2416] d1
                    call      $1f20                         ;[2417] cd 20 1f
                    push      de                            ;[241a] d5
                    ld        de,($5c7b)                    ;[241b] ed 5b 7b 5c
                    scf                                     ;[241f] 37
                    jr        $2429                         ;[2420] 18 07
                    ld        de,$252f                      ;[2422] 11 2f 25
                    call      $2371                         ;[2425] cd 71 23
                    and       a                             ;[2428] a7
                    ex        af,af'                        ;[2429] 08
                    add       hl,de                         ;[242a] 19
                    pop       de                            ;[242b] d1
                    ld        c,d                           ;[242c] 4a
                    ld        a,(hl)                        ;[242d] 7e
                    ld        (de),a                        ;[242e] 12
                    inc       hl                            ;[242f] 23
                    inc       d                             ;[2430] 14
                    ld        a,(hl)                        ;[2431] 7e
                    ld        (de),a                        ;[2432] 12
                    inc       hl                            ;[2433] 23
                    inc       d                             ;[2434] 14
                    ld        a,(hl)                        ;[2435] 7e
                    ld        (de),a                        ;[2436] 12
                    inc       hl                            ;[2437] 23
                    inc       d                             ;[2438] 14
                    ld        a,(hl)                        ;[2439] 7e
                    ld        (de),a                        ;[243a] 12
                    inc       hl                            ;[243b] 23
                    inc       d                             ;[243c] 14
                    ld        a,(hl)                        ;[243d] 7e
                    ld        (de),a                        ;[243e] 12
                    inc       hl                            ;[243f] 23
                    inc       d                             ;[2440] 14
                    ld        a,(hl)                        ;[2441] 7e
                    ld        (de),a                        ;[2442] 12
                    inc       hl                            ;[2443] 23
                    inc       d                             ;[2444] 14
                    ld        a,(hl)                        ;[2445] 7e
                    ld        (de),a                        ;[2446] 12
                    inc       hl                            ;[2447] 23
                    inc       d                             ;[2448] 14
                    ld        a,(hl)                        ;[2449] 7e
                    ld        (de),a                        ;[244a] 12
                    ld        d,c                           ;[244b] 51
                    ex        af,af'                        ;[244c] 08
                    call      c,$1f45                       ;[244d] dc 45 1f
                    pop       hl                            ;[2450] e1
                    inc       hl                            ;[2451] 23
                    inc       de                            ;[2452] 13
                    djnz      $23e1                         ;[2453] 10 8c
                    ret                                     ;[2455] c9

                    push      bc                            ;[2456] c5
                    di                                      ;[2457] f3
                    ld        bc,$7ffd                      ;[2458] 01 fd 7f
                    ld        a,($5b5c)                     ;[245b] 3a 5c 5b
                    xor       $10                           ;[245e] ee 10
                    out       (c),a                         ;[2460] ed 79
                    ei                                      ;[2462] fb
                    ex        af,af'                        ;[2463] 08
                    ex        af,af'                        ;[2464] 08
                    di                                      ;[2465] f3
                    ld        c,$fd                         ;[2466] 0e fd
                    xor       $10                           ;[2468] ee 10
                    out       (c),a                         ;[246a] ed 79
                    ei                                      ;[246c] fb
                    pop       bc                            ;[246d] c1
                    ret                                     ;[246e] c9

                    ld        hl,$2456                      ;[246f] 21 56 24
                    ld        de,$ff28                      ;[2472] 11 28 ff
                    ld        bc,$000e                      ;[2475] 01 0e 00
                    ldir                                    ;[2478] ed b0
                    push      hl                            ;[247a] e5
                    ld        hl,$242c                      ;[247b] 21 2c 24
                    ld        c,$20                         ;[247e] 0e 20
                    ldir                                    ;[2480] ed b0
                    pop       hl                            ;[2482] e1
                    ld        c,$0b                         ;[2483] 0e 0b
                    ldir                                    ;[2485] ed b0
                    ret                                     ;[2487] c9

                    res       0,(iy-$39)                    ;[2488] fd cb c7 86
                    ld        de,$5800                      ;[248c] 11 00 58
                    ld        bc,$02c0                      ;[248f] 01 c0 02
                    ld        hl,($ff24)                    ;[2492] 2a 24 ff
                    ld        a,($5c8d)                     ;[2495] 3a 8d 5c
                    ld        ($5c8f),a                     ;[2498] 32 8f 5c
                    ex        af,af'                        ;[249b] 08
                    push      bc                            ;[249c] c5
                    ld        a,(hl)                        ;[249d] 7e
                    cp        $ff                           ;[249e] fe ff
                    jr        nz,$24aa                      ;[24a0] 20 08
                    ld        a,($5c8d)                     ;[24a2] 3a 8d 5c
                    ld        (de),a                        ;[24a5] 12
                    inc       hl                            ;[24a6] 23
                    inc       de                            ;[24a7] 13
                    jr        $2507                         ;[24a8] 18 5d
                    ex        af,af'                        ;[24aa] 08
                    ld        (de),a                        ;[24ab] 12
                    inc       de                            ;[24ac] 13
                    ex        af,af'                        ;[24ad] 08
                    inc       hl                            ;[24ae] 23
                    cp        $15                           ;[24af] fe 15
                    jr        nc,$2507                      ;[24b1] 30 54
                    cp        $10                           ;[24b3] fe 10
                    jr        c,$2507                       ;[24b5] 38 50
                    dec       hl                            ;[24b7] 2b
                    jr        nz,$24c2                      ;[24b8] 20 08
                    inc       hl                            ;[24ba] 23
                    ld        a,(hl)                        ;[24bb] 7e
                    ld        c,a                           ;[24bc] 4f
                    ex        af,af'                        ;[24bd] 08
                    and       $f8                           ;[24be] e6 f8
                    jr        $2505                         ;[24c0] 18 43
                    cp        $11                           ;[24c2] fe 11
                    jr        nz,$24d1                      ;[24c4] 20 0b
                    inc       hl                            ;[24c6] 23
                    ld        a,(hl)                        ;[24c7] 7e
                    add       a                             ;[24c8] 87
                    add       a                             ;[24c9] 87
                    add       a                             ;[24ca] 87
                    ld        c,a                           ;[24cb] 4f
                    ex        af,af'                        ;[24cc] 08
                    and       $c7                           ;[24cd] e6 c7
                    jr        $2505                         ;[24cf] 18 34
                    cp        $12                           ;[24d1] fe 12
                    jr        nz,$24de                      ;[24d3] 20 09
                    inc       hl                            ;[24d5] 23
                    ld        a,(hl)                        ;[24d6] 7e
                    rrca                                    ;[24d7] 0f
                    ld        c,a                           ;[24d8] 4f
                    ex        af,af'                        ;[24d9] 08
                    and       $7f                           ;[24da] e6 7f
                    jr        $2505                         ;[24dc] 18 27
                    cp        $13                           ;[24de] fe 13
                    jr        nz,$24ec                      ;[24e0] 20 0a
                    inc       hl                            ;[24e2] 23
                    ld        a,(hl)                        ;[24e3] 7e
                    rrca                                    ;[24e4] 0f
                    rrca                                    ;[24e5] 0f
                    ld        c,a                           ;[24e6] 4f
                    ex        af,af'                        ;[24e7] 08
                    and       $bf                           ;[24e8] e6 bf
                    jr        $2505                         ;[24ea] 18 19
                    cp        $14                           ;[24ec] fe 14
                    inc       hl                            ;[24ee] 23
                    jr        nz,$2507                      ;[24ef] 20 16
                    ld        c,(hl)                        ;[24f1] 4e
                    ld        a,($5c01)                     ;[24f2] 3a 01 5c
                    xor       c                             ;[24f5] a9
                    rra                                     ;[24f6] 1f
                    jr        nc,$2507                      ;[24f7] 30 0e
                    ld        a,$01                         ;[24f9] 3e 01
                    xor       (iy-$39)                      ;[24fb] fd ae c7
                    ld        ($5c01),a                     ;[24fe] 32 01 5c
                    ex        af,af'                        ;[2501] 08
                    call      $2513                         ;[2502] cd 13 25
                    or        c                             ;[2505] b1
                    ex        af,af'                        ;[2506] 08
                    pop       bc                            ;[2507] c1
                    dec       bc                            ;[2508] 0b
                    ld        a,b                           ;[2509] 78
                    or        c                             ;[250a] b1
                    jp        nz,$249c                      ;[250b] c2 9c 24
                    ex        af,af'                        ;[250e] 08
                    ld        ($5c8f),a                     ;[250f] 32 8f 5c
                    ret                                     ;[2512] c9

                    ld        b,a                           ;[2513] 47
                    and       $c0                           ;[2514] e6 c0
                    ld        c,a                           ;[2516] 4f
                    ld        a,b                           ;[2517] 78
                    add       a                             ;[2518] 87
                    add       a                             ;[2519] 87
                    add       a                             ;[251a] 87
                    and       $38                           ;[251b] e6 38
                    or        c                             ;[251d] b1
                    ld        c,a                           ;[251e] 4f
                    ld        a,b                           ;[251f] 78
                    rra                                     ;[2520] 1f
                    rra                                     ;[2521] 1f
                    rra                                     ;[2522] 1f
                    and       $07                           ;[2523] e6 07
                    or        c                             ;[2525] b1
                    ret                                     ;[2526] c9

                    nop                                     ;[2527] 00
                    inc       a                             ;[2528] 3c
                    ld        h,d                           ;[2529] 62
                    ld        h,b                           ;[252a] 60
                    ld        l,(hl)                        ;[252b] 6e
                    ld        h,d                           ;[252c] 62
                    ld        a,$00                         ;[252d] 3e 00
                    nop                                     ;[252f] 00
                    ld        l,h                           ;[2530] 6c
                    djnz      $2587                         ;[2531] 10 54
                    cp        d                             ;[2533] ba
                    jr        c,$258a                       ;[2534] 38 54
                    add       d                             ;[2536] 82
                    dec       d                             ;[2537] 15
                    dec       bc                            ;[2538] 0b
                    sub       h                             ;[2539] 94
                    ld        hl,($b50a)                    ;[253a] 2a 0a b5
                    ld        hl,($d708)                    ;[253d] 2a 08 d7
                    ld        hl,($e309)                    ;[2540] 2a 09 e3
                    ld        hl,($4fad)                    ;[2543] 2a ad 4f
                    ld        hl,($25ac)                    ;[2546] 2a ac 25
                    ld        hl,($d4af)                    ;[2549] 2a af d4
                    add       hl,hl                         ;[254c] 29
                    xor       (hl)                          ;[254d] ae
                    pop       hl                            ;[254e] e1
                    add       hl,hl                         ;[254f] 29
                    and       (hl)                          ;[2550] a6
                    add       e                             ;[2551] 83
                    add       hl,hl                         ;[2552] 29
                    and       l                             ;[2553] a5
                    xor       e                             ;[2554] ab
                    add       hl,hl                         ;[2555] 29
                    xor       b                             ;[2556] a8
                    add       a                             ;[2557] 87
                    ld        hl,($7aa7)                    ;[2558] 2a a7 7a
                    ld        hl,($1baa)                    ;[255b] 2a aa 1b
                    add       hl,hl                         ;[255e] 29
                    inc       c                             ;[255f] 0c
                    dec       hl                            ;[2560] 2b
                    add       hl,hl                         ;[2561] 29
                    or        e                             ;[2562] b3
                    rla                                     ;[2563] 17
                    jr        nc,$251a                      ;[2564] 30 b4
                    cp        h                             ;[2566] bc
                    cpl                                     ;[2567] 2f
                    or        b                             ;[2568] b0
                    ld        (hl),d                        ;[2569] 72
                    jr        nc,$251d                      ;[256a] 30 b1
                    ld        a,$30                         ;[256c] 3e 30
                    dec       c                             ;[256e] 0d
                    ld        b,h                           ;[256f] 44
                    add       hl,hl                         ;[2570] 29
                    xor       c                             ;[2571] a9
                    sbc       e                             ;[2572] 9b
                    ld        h,$07                         ;[2573] 26 07
                    inc       b                             ;[2575] 04
                    daa                                     ;[2576] 27
                    inc       b                             ;[2577] 04
                    dec       bc                            ;[2578] 0b
                    ld        l,$27                         ;[2579] 2e 27
                    ld        a,(bc)                        ;[257b] 0a
                    ld        sp,$0727                      ;[257c] 31 27 07
                    rla                                     ;[257f] 17
                    daa                                     ;[2580] 27
                    dec       c                             ;[2581] 0d
                    rla                                     ;[2582] 17
                    daa                                     ;[2583] 27
                    call      $28be                         ;[2584] cd be 28
                    ld        hl,$0000                      ;[2587] 21 00 00
                    ld        ($fc9a),hl                    ;[258a] 22 9a fc
                    ld        a,$82                         ;[258d] 3e 82
                    ld        ($ec0d),a                     ;[258f] 32 0d ec
                    ld        hl,$0000                      ;[2592] 21 00 00
                    ld        ($5c49),hl                    ;[2595] 22 49 5c
                    call      $35bc                         ;[2598] cd bc 35
                    call      $365e                         ;[259b] cd 5e 36
                    ret                                     ;[259e] c9

                    ld        hl,$5bff                      ;[259f] 21 ff 5b
                    ld        ($5b81),hl                    ;[25a2] 22 81 5b
                    call      $1f45                         ;[25a5] cd 45 1f
                    ld        a,$02                         ;[25a8] 3e 02
                    rst       $28                           ;[25aa] ef
                    ld        bc,$2116                      ;[25ab] 01 16 21
                    ld        b,h                           ;[25ae] 44
                    daa                                     ;[25af] 27
                    ld        ($f6ea),hl                    ;[25b0] 22 ea f6
                    ld        hl,$2754                      ;[25b3] 21 54 27
                    ld        ($f6ec),hl                    ;[25b6] 22 ec f6
                    push      hl                            ;[25b9] e5
                    ld        hl,$ec0d                      ;[25ba] 21 0d ec
                    set       1,(hl)                        ;[25bd] cb ce
                    res       4,(hl)                        ;[25bf] cb a6
                    dec       hl                            ;[25c1] 2b
                    ld        (hl),$00                      ;[25c2] 36 00
                    pop       hl                            ;[25c4] e1
                    call      $36a8                         ;[25c5] cd a8 36
                    jp        $2653                         ;[25c8] c3 53 26
                    ld        ix,$fd6c                      ;[25cb] dd 21 6c fd
                    ld        hl,$5bff                      ;[25cf] 21 ff 5b
                    ld        ($5b81),hl                    ;[25d2] 22 81 5b
                    call      $1f45                         ;[25d5] cd 45 1f
                    ld        a,$02                         ;[25d8] 3e 02
                    rst       $28                           ;[25da] ef
                    ld        bc,$cd16                      ;[25db] 01 16 cd
                    ld        l,b                           ;[25de] 68
                    ld        (hl),$21                      ;[25df] 36 21
                    dec       sp                            ;[25e1] 3b
                    ld        e,h                           ;[25e2] 5c
                    bit       5,(hl)                        ;[25e3] cb 6e
                    jr        z,$25e3                       ;[25e5] 28 fc
                    ld        hl,$ec0d                      ;[25e7] 21 0d ec
                    res       3,(hl)                        ;[25ea] cb 9e
                    bit       6,(hl)                        ;[25ec] cb 76
                    jr        nz,$2604                      ;[25ee] 20 14
                    ld        a,($ec0e)                     ;[25f0] 3a 0e ec
                    cp        $04                           ;[25f3] fe 04
                    jr        z,$2601                       ;[25f5] 28 0a
                    cp        $00                           ;[25f7] fe 00
                    jp        nz,$28c7                      ;[25f9] c2 c7 28
                    call      $3848                         ;[25fc] cd 48 38
                    jr        $2604                         ;[25ff] 18 03
                    call      $384d                         ;[2601] cd 4d 38
                    call      $30d6                         ;[2604] cd d6 30
                    call      $3222                         ;[2607] cd 22 32
                    ld        a,($ec0e)                     ;[260a] 3a 0e ec
                    cp        $04                           ;[260d] fe 04
                    jr        z,$2653                       ;[260f] 28 42
                    ld        hl,($5c49)                    ;[2611] 2a 49 5c
                    ld        a,h                           ;[2614] 7c
                    or        l                             ;[2615] b5
                    jr        nz,$262d                      ;[2616] 20 15
                    ld        hl,($5c53)                    ;[2618] 2a 53 5c
                    ld        bc,($5c4b)                    ;[261b] ed 4b 4b 5c
                    and       a                             ;[261f] a7
                    sbc       hl,bc                         ;[2620] ed 42
                    jr        nz,$262a                      ;[2622] 20 06
                    ld        hl,$0000                      ;[2624] 21 00 00
                    ld        ($ec08),hl                    ;[2627] 22 08 ec
                    ld        hl,($ec08)                    ;[262a] 2a 08 ec
                    call      $1f20                         ;[262d] cd 20 1f
                    rst       $28                           ;[2630] ef
                    ld        l,(hl)                        ;[2631] 6e
                    add       hl,de                         ;[2632] 19
                    rst       $28                           ;[2633] ef
                    sub       l                             ;[2634] 95
                    ld        d,$cd                         ;[2635] 16 cd
                    ld        b,l                           ;[2637] 45
                    rra                                     ;[2638] 1f
                    ld        ($5c49),de                    ;[2639] ed 53 49 5c
                    ld        hl,$ec0d                      ;[263d] 21 0d ec
                    bit       5,(hl)                        ;[2640] cb 6e
                    jr        nz,$2653                      ;[2642] 20 0f
                    ld        hl,$0000                      ;[2644] 21 00 00
                    ld        ($ec06),hl                    ;[2647] 22 06 ec
                    call      $152f                         ;[264a] cd 2f 15
                    call      $29f2                         ;[264d] cd f2 29
                    call      $2944                         ;[2650] cd 44 29
                    ld        sp,$5bff                      ;[2653] 31 ff 5b
                    call      $3668                         ;[2656] cd 68 36
                    call      $367f                         ;[2659] cd 7f 36
                    push      af                            ;[265c] f5
                    ld        a,($5c39)                     ;[265d] 3a 39 5c
                    call      $26ec                         ;[2660] cd ec 26
                    pop       af                            ;[2663] f1
                    call      $2669                         ;[2664] cd 69 26
                    jr        $2653                         ;[2667] 18 ea
                    ld        hl,$ec0d                      ;[2669] 21 0d ec
                    bit       1,(hl)                        ;[266c] cb 4e
                    push      af                            ;[266e] f5
                    ld        hl,$2577                      ;[266f] 21 77 25
                    jr        nz,$2677                      ;[2672] 20 03
                    ld        hl,$2537                      ;[2674] 21 37 25
                    call      $3fce                         ;[2677] cd ce 3f
                    jr        nz,$2681                      ;[267a] 20 05
                    call      nc,$26e7                      ;[267c] d4 e7 26
                    pop       af                            ;[267f] f1
                    ret                                     ;[2680] c9

                    pop       af                            ;[2681] f1
                    jr        z,$2689                       ;[2682] 28 05
                    xor       a                             ;[2684] af
                    ld        ($5c41),a                     ;[2685] 32 41 5c
                    ret                                     ;[2688] c9

                    ld        hl,$ec0d                      ;[2689] 21 0d ec
                    bit       0,(hl)                        ;[268c] cb 46
                    jr        z,$2694                       ;[268e] 28 04
                    call      $26e7                         ;[2690] cd e7 26
                    ret                                     ;[2693] c9

                    cp        $a3                           ;[2694] fe a3
                    jr        nc,$2653                      ;[2696] 30 bb
                    jp        $28f1                         ;[2698] c3 f1 28
                    ld        a,($ec0e)                     ;[269b] 3a 0e ec
                    cp        $04                           ;[269e] fe 04
                    ret       z                             ;[26a0] c8
                    call      $1630                         ;[26a1] cd 30 16
                    ld        hl,$ec0d                      ;[26a4] 21 0d ec
                    res       3,(hl)                        ;[26a7] cb 9e
                    ld        a,(hl)                        ;[26a9] 7e
                    xor       $40                           ;[26aa] ee 40
                    ld        (hl),a                        ;[26ac] 77
                    and       $40                           ;[26ad] e6 40
                    jr        z,$26b6                       ;[26af] 28 05
                    call      $26bb                         ;[26b1] cd bb 26
                    jr        $26b9                         ;[26b4] 18 03
                    call      $26ce                         ;[26b6] cd ce 26
                    scf                                     ;[26b9] 37
                    ret                                     ;[26ba] c9

                    call      $3881                         ;[26bb] cd 81 38
                    ld        hl,$ec0d                      ;[26be] 21 0d ec
                    set       6,(hl)                        ;[26c1] cb f6
                    call      $2e2d                         ;[26c3] cd 2d 2e
                    call      $3a88                         ;[26c6] cd 88 3a
                    call      $28df                         ;[26c9] cd df 28
                    jr        $26d9                         ;[26cc] 18 0b
                    ld        hl,$ec0d                      ;[26ce] 21 0d ec
                    res       6,(hl)                        ;[26d1] cb b6
                    call      $28be                         ;[26d3] cd be 28
                    call      $3848                         ;[26d6] cd 48 38
                    ld        hl,($fc9a)                    ;[26d9] 2a 9a fc
                    ld        a,h                           ;[26dc] 7c
                    or        l                             ;[26dd] b5
                    call      nz,$334a                      ;[26de] c4 4a 33
                    call      $152f                         ;[26e1] cd 2f 15
                    jp        $29f2                         ;[26e4] c3 f2 29
                    ld        a,($5c38)                     ;[26e7] 3a 38 5c
                    srl       a                             ;[26ea] cb 3f
                    push      ix                            ;[26ec] dd e5
                    ld        d,$00                         ;[26ee] 16 00
                    ld        e,a                           ;[26f0] 5f
                    ld        hl,$0c80                      ;[26f1] 21 80 0c
                    rst       $28                           ;[26f4] ef
                    or        l                             ;[26f5] b5
                    inc       bc                            ;[26f6] 03
                    pop       ix                            ;[26f7] dd e1
                    ret                                     ;[26f9] c9

                    push      ix                            ;[26fa] dd e5
                    ld        de,$0030                      ;[26fc] 11 30 00
                    ld        hl,$0300                      ;[26ff] 21 00 03
                    jr        $26f4                         ;[2702] 18 f0
                    call      $29ec                         ;[2704] cd ec 29
                    ld        hl,$ec0d                      ;[2707] 21 0d ec
                    set       1,(hl)                        ;[270a] cb ce
                    dec       hl                            ;[270c] 2b
                    ld        (hl),$00                      ;[270d] 36 00
                    ld        hl,($f6ec)                    ;[270f] 2a ec f6
                    call      $36a8                         ;[2712] cd a8 36
                    scf                                     ;[2715] 37
                    ret                                     ;[2716] c9

                    ld        hl,$ec0d                      ;[2717] 21 0d ec
                    res       1,(hl)                        ;[271a] cb 8e
                    dec       hl                            ;[271c] 2b
                    ld        a,(hl)                        ;[271d] 7e
                    ld        hl,($f6ea)                    ;[271e] 2a ea f6
                    push      hl                            ;[2721] e5
                    push      af                            ;[2722] f5
                    call      $373e                         ;[2723] cd 3e 37
                    pop       af                            ;[2726] f1
                    pop       hl                            ;[2727] e1
                    call      $3fce                         ;[2728] cd ce 3f
                    jp        $29f2                         ;[272b] c3 f2 29
                    scf                                     ;[272e] 37
                    jr        $2732                         ;[272f] 18 01
                    and       a                             ;[2731] a7
                    ld        hl,$ec0c                      ;[2732] 21 0c ec
                    ld        a,(hl)                        ;[2735] 7e
                    push      hl                            ;[2736] e5
                    ld        hl,($f6ec)                    ;[2737] 2a ec f6
                    call      c,$37a7                       ;[273a] dc a7 37
                    call      nc,$37b6                      ;[273d] d4 b6 37
                    pop       hl                            ;[2740] e1
                    ld        (hl),a                        ;[2741] 77
                    scf                                     ;[2742] 37
                    ret                                     ;[2743] c9

                    dec       b                             ;[2744] 05
                    nop                                     ;[2745] 00
                    ld        sp,$0128                      ;[2746] 31 28 01
                    ld        l,h                           ;[2749] 6c
                    jr        z,$274e                       ;[274a] 28 02
                    add       l                             ;[274c] 85
                    jr        z,$2752                       ;[274d] 28 03
                    ld        b,a                           ;[274f] 47
                    dec       de                            ;[2750] 1b
                    inc       b                             ;[2751] 04
                    ld        d,$28                         ;[2752] 16 28
                    ld        b,$31                         ;[2754] 06 31
                    ld        ($2038),a                     ;[2756] 32 38 20
                    jr        nz,$277b                      ;[2759] 20 20
                    jr        nz,$277d                      ;[275b] 20 20
                    rst       $38                           ;[275d] ff
                    ld        d,h                           ;[275e] 54
                    ld        h,c                           ;[275f] 61
                    ld        (hl),b                        ;[2760] 70
                    ld        h,l                           ;[2761] 65
                    jr        nz,$27b0                      ;[2762] 20 4c
                    ld        l,a                           ;[2764] 6f
                    ld        h,c                           ;[2765] 61
                    ld        h,h                           ;[2766] 64
                    ld        h,l                           ;[2767] 65
                    jp        p,$3231                       ;[2768] f2 31 32
                    jr        c,$278d                       ;[276b] 38 20
                    ld        b,d                           ;[276d] 42
                    ld        b,c                           ;[276e] 41
                    ld        d,e                           ;[276f] 53
                    ld        c,c                           ;[2770] 49
                    jp        $6143                         ;[2771] c3 43 61
                    ld        l,h                           ;[2774] 6c
                    ld        h,e                           ;[2775] 63
                    ld        (hl),l                        ;[2776] 75
                    ld        l,h                           ;[2777] 6c
                    ld        h,c                           ;[2778] 61
                    ld        (hl),h                        ;[2779] 74
                    ld        l,a                           ;[277a] 6f
                    jp        p,$3834                       ;[277b] f2 34 38
                    jr        nz,$27c2                      ;[277e] 20 42
                    ld        b,c                           ;[2780] 41
                    ld        d,e                           ;[2781] 53
                    ld        c,c                           ;[2782] 49
                    jp        $6154                         ;[2783] c3 54 61
                    ld        (hl),b                        ;[2786] 70
                    ld        h,l                           ;[2787] 65
                    jr        nz,$27de                      ;[2788] 20 54
                    ld        h,l                           ;[278a] 65
                    ld        (hl),e                        ;[278b] 73
                    ld        (hl),h                        ;[278c] 74
                    ld        h,l                           ;[278d] 65
                    jp        p,$05a0                       ;[278e] f2 a0 05
                    nop                                     ;[2791] 00
                    ld        b,d                           ;[2792] 42
                    daa                                     ;[2793] 27
                    ld        bc,$2851                      ;[2794] 01 51 28
                    ld        (bc),a                        ;[2797] 02
                    ld        de,$0328                      ;[2798] 11 28 03
                    ld        h,d                           ;[279b] 62
                    jr        z,$27a2                       ;[279c] 28 04
                    inc       e                             ;[279e] 1c
                    jr        z,$27a7                       ;[279f] 28 06
                    ld        c,a                           ;[27a1] 4f
                    ld        (hl),b                        ;[27a2] 70
                    ld        (hl),h                        ;[27a3] 74
                    ld        l,c                           ;[27a4] 69
                    ld        l,a                           ;[27a5] 6f
                    ld        l,(hl)                        ;[27a6] 6e
                    ld        (hl),e                        ;[27a7] 73
                    jr        nz,$27a9                      ;[27a8] 20 ff
                    ld        sp,$3832                      ;[27aa] 31 32 38
                    jr        nz,$27f1                      ;[27ad] 20 42
                    ld        b,c                           ;[27af] 41
                    ld        d,e                           ;[27b0] 53
                    ld        c,c                           ;[27b1] 49
                    jp        $6552                         ;[27b2] c3 52 65
                    ld        l,(hl)                        ;[27b5] 6e
                    ld        (hl),l                        ;[27b6] 75
                    ld        l,l                           ;[27b7] 6d
                    ld        h,d                           ;[27b8] 62
                    ld        h,l                           ;[27b9] 65
                    jp        p,$6353                       ;[27ba] f2 53 63
                    ld        (hl),d                        ;[27bd] 72
                    ld        h,l                           ;[27be] 65
                    ld        h,l                           ;[27bf] 65
                    xor       $50                           ;[27c0] ee 50
                    ld        (hl),d                        ;[27c2] 72
                    ld        l,c                           ;[27c3] 69
                    ld        l,(hl)                        ;[27c4] 6e
                    call      p,$7845                       ;[27c5] f4 45 78
                    ld        l,c                           ;[27c8] 69
                    call      p,$02a0                       ;[27c9] f4 a0 02
                    nop                                     ;[27cc] 00
                    ld        b,d                           ;[27cd] 42
                    daa                                     ;[27ce] 27
                    ld        bc,$281c                      ;[27cf] 01 1c 28
                    inc       bc                            ;[27d2] 03
                    ld        c,a                           ;[27d3] 4f
                    ld        (hl),b                        ;[27d4] 70
                    ld        (hl),h                        ;[27d5] 74
                    ld        l,c                           ;[27d6] 69
                    ld        l,a                           ;[27d7] 6f
                    ld        l,(hl)                        ;[27d8] 6e
                    ld        (hl),e                        ;[27d9] 73
                    jr        nz,$27db                      ;[27da] 20 ff
                    ld        b,e                           ;[27dc] 43
                    ld        h,c                           ;[27dd] 61
                    ld        l,h                           ;[27de] 6c
                    ld        h,e                           ;[27df] 63
                    ld        (hl),l                        ;[27e0] 75
                    ld        l,h                           ;[27e1] 6c
                    ld        h,c                           ;[27e2] 61
                    ld        (hl),h                        ;[27e3] 74
                    ld        l,a                           ;[27e4] 6f
                    jp        p,$7845                       ;[27e5] f2 45 78
                    ld        l,c                           ;[27e8] 69
                    call      p,$16a0                       ;[27e9] f4 a0 16
                    ld        bc,$1000                      ;[27ec] 01 00 10
                    nop                                     ;[27ef] 00
                    ld        de,$1307                      ;[27f0] 11 07 13
                    nop                                     ;[27f3] 00
                    ld        d,h                           ;[27f4] 54
                    ld        l,a                           ;[27f5] 6f
                    jr        nz,$285b                      ;[27f6] 20 63
                    ld        h,c                           ;[27f8] 61
                    ld        l,(hl)                        ;[27f9] 6e
                    ld        h,e                           ;[27fa] 63
                    ld        h,l                           ;[27fb] 65
                    ld        l,h                           ;[27fc] 6c
                    jr        nz,$282c                      ;[27fd] 20 2d
                    jr        nz,$2871                      ;[27ff] 20 70
                    ld        (hl),d                        ;[2801] 72
                    ld        h,l                           ;[2802] 65
                    ld        (hl),e                        ;[2803] 73
                    ld        (hl),e                        ;[2804] 73
                    jr        nz,$2849                      ;[2805] 20 42
                    ld        d,d                           ;[2807] 52
                    ld        b,l                           ;[2808] 45
                    ld        b,c                           ;[2809] 41
                    ld        c,e                           ;[280a] 4b
                    jr        nz,$2881                      ;[280b] 20 74
                    ld        (hl),a                        ;[280d] 77
                    ld        l,c                           ;[280e] 69
                    ld        h,e                           ;[280f] 63
                    push      hl                            ;[2810] e5
                    call      $269b                         ;[2811] cd 9b 26
                    jr        $2874                         ;[2814] 18 5e
                    call      $3857                         ;[2816] cd 57 38
                    call      $3be9                         ;[2819] cd e9 3b
                    ld        hl,$ec0d                      ;[281c] 21 0d ec
                    res       6,(hl)                        ;[281f] cb b6
                    call      $28be                         ;[2821] cd be 28
                    ld        b,$00                         ;[2824] 06 00
                    ld        d,$17                         ;[2826] 16 17
                    call      $3b5e                         ;[2828] cd 5e 3b
                    call      $1f20                         ;[282b] cd 20 1f
                    jp        $259f                         ;[282e] c3 9f 25
                    call      $3852                         ;[2831] cd 52 38
                    ld        hl,$5c3c                      ;[2834] 21 3c 5c
                    set       0,(hl)                        ;[2837] cb c6
                    ld        de,$27eb                      ;[2839] 11 eb 27
                    call      $057d                         ;[283c] cd 7d 05
                    res       0,(hl)                        ;[283f] cb 86
                    set       6,(hl)                        ;[2841] cb f6
                    ld        a,$07                         ;[2843] 3e 07
                    ld        ($ec0e),a                     ;[2845] 32 0e ec
                    ld        bc,$0000                      ;[2848] 01 00 00
                    call      $372b                         ;[284b] cd 2b 37
                    jp        $1af1                         ;[284e] c3 f1 1a
                    call      $3888                         ;[2851] cd 88 38
                    call      nc,$26e7                      ;[2854] d4 e7 26
                    ld        hl,$0000                      ;[2857] 21 00 00
                    ld        ($5c49),hl                    ;[285a] 22 49 5c
                    ld        ($ec08),hl                    ;[285d] 22 08 ec
                    jr        $2865                         ;[2860] 18 03
                    call      $1b14                         ;[2862] cd 14 1b
                    ld        hl,$ec0d                      ;[2865] 21 0d ec
                    bit       6,(hl)                        ;[2868] cb 76
                    jr        nz,$2874                      ;[286a] 20 08
                    ld        hl,$5c3c                      ;[286c] 21 3c 5c
                    res       0,(hl)                        ;[286f] cb 86
                    call      $3848                         ;[2871] cd 48 38
                    ld        hl,$ec0d                      ;[2874] 21 0d ec
                    res       5,(hl)                        ;[2877] cb ae
                    res       4,(hl)                        ;[2879] cb a6
                    ld        a,$00                         ;[287b] 3e 00
                    ld        hl,$2790                      ;[287d] 21 90 27
                    ld        de,$27a0                      ;[2880] 11 a0 27
                    jr        $28b1                         ;[2883] 18 2c
                    ld        hl,$ec0d                      ;[2885] 21 0d ec
                    set       5,(hl)                        ;[2888] cb ee
                    set       4,(hl)                        ;[288a] cb e6
                    res       6,(hl)                        ;[288c] cb b6
                    call      $28be                         ;[288e] cd be 28
                    call      $384d                         ;[2891] cd 4d 38
                    ld        a,$04                         ;[2894] 3e 04
                    ld        ($ec0e),a                     ;[2896] 32 0e ec
                    ld        hl,$0000                      ;[2899] 21 00 00
                    ld        ($5c49),hl                    ;[289c] 22 49 5c
                    call      $152f                         ;[289f] cd 2f 15
                    ld        bc,$0000                      ;[28a2] 01 00 00
                    ld        a,b                           ;[28a5] 78
                    call      $29f8                         ;[28a6] cd f8 29
                    ld        a,$04                         ;[28a9] 3e 04
                    ld        hl,$27cb                      ;[28ab] 21 cb 27
                    ld        de,$27d2                      ;[28ae] 11 d2 27
                    ld        ($ec0e),a                     ;[28b1] 32 0e ec
                    ld        ($f6ea),hl                    ;[28b4] 22 ea f6
                    ld        ($f6ec),de                    ;[28b7] ed 53 ec f6
                    jp        $2604                         ;[28bb] c3 04 26
                    call      $2e1f                         ;[28be] cd 1f 2e
                    call      $3a7f                         ;[28c1] cd 7f 3a
                    jp        $28e8                         ;[28c4] c3 e8 28
                    ld        b,$00                         ;[28c7] 06 00
                    ld        d,$17                         ;[28c9] 16 17
                    call      $3b5e                         ;[28cb] cd 5e 3b
                    jp        $25ad                         ;[28ce] c3 ad 25
                    ld        b,$00                         ;[28d1] 06 00
                    nop                                     ;[28d3] 00
                    nop                                     ;[28d4] 00
                    inc       b                             ;[28d5] 04
                    djnz      $28ec                         ;[28d6] 10 14
                    ld        b,$00                         ;[28d8] 06 00
                    nop                                     ;[28da] 00
                    nop                                     ;[28db] 00
                    nop                                     ;[28dc] 00
                    ld        bc,$2101                      ;[28dd] 01 01 21
                    ret       c                             ;[28e0] d8
                    jr        z,$28f4                       ;[28e1] 28 11
                    xor       $f6                           ;[28e3] ee f6
                    jp        $3fba                         ;[28e5] c3 ba 3f
                    ld        hl,$28d1                      ;[28e8] 21 d1 28
                    ld        de,$f6ee                      ;[28eb] 11 ee f6
                    jp        $3fba                         ;[28ee] c3 ba 3f
                    ld        hl,$ec0d                      ;[28f1] 21 0d ec
                    or        a                             ;[28f4] b7
                    or        a                             ;[28f5] b7
                    bit       0,(hl)                        ;[28f6] cb 46
                    jp        nz,$29f2                      ;[28f8] c2 f2 29
                    res       7,(hl)                        ;[28fb] cb be
                    set       3,(hl)                        ;[28fd] cb de
                    push      hl                            ;[28ff] e5
                    push      af                            ;[2900] f5
                    call      $29ec                         ;[2901] cd ec 29
                    pop       af                            ;[2904] f1
                    push      af                            ;[2905] f5
                    call      $2e81                         ;[2906] cd 81 2e
                    pop       af                            ;[2909] f1
                    ld        a,b                           ;[290a] 78
                    call      $2b78                         ;[290b] cd 78 2b
                    pop       hl                            ;[290e] e1
                    set       7,(hl)                        ;[290f] cb fe
                    jp        nc,$29f2                      ;[2911] d2 f2 29
                    ld        a,b                           ;[2914] 78
                    jp        c,$29f8                       ;[2915] da f8 29
                    jp        $29f2                         ;[2918] c3 f2 29
                    ld        hl,$ec0d                      ;[291b] 21 0d ec
                    set       3,(hl)                        ;[291e] cb de
                    call      $29ec                         ;[2920] cd ec 29
                    call      $2f12                         ;[2923] cd 12 2f
                    scf                                     ;[2926] 37
                    ld        a,b                           ;[2927] 78
                    jp        $29f8                         ;[2928] c3 f8 29
                    ld        hl,$ec0d                      ;[292b] 21 0d ec
                    res       0,(hl)                        ;[292e] cb 86
                    set       3,(hl)                        ;[2930] cb de
                    call      $29ec                         ;[2932] cd ec 29
                    call      $2b5b                         ;[2935] cd 5b 2b
                    ccf                                     ;[2938] 3f
                    jp        c,$29f2                       ;[2939] da f2 29
                    call      $2f12                         ;[293c] cd 12 2f
                    scf                                     ;[293f] 37
                    ld        a,b                           ;[2940] 78
                    jp        $29f8                         ;[2941] c3 f8 29
                    call      $29ec                         ;[2944] cd ec 29
                    push      af                            ;[2947] f5
                    call      $30b4                         ;[2948] cd b4 30
                    push      bc                            ;[294b] c5
                    ld        b,$00                         ;[294c] 06 00
                    call      $2e41                         ;[294e] cd 41 2e
                    pop       bc                            ;[2951] c1
                    jr        c,$295e                       ;[2952] 38 0a
                    ld        hl,$0020                      ;[2954] 21 20 00
                    add       hl,de                         ;[2957] 19
                    ld        a,(hl)                        ;[2958] 7e
                    cpl                                     ;[2959] 2f
                    and       $09                           ;[295a] e6 09
                    jr        z,$297a                       ;[295c] 28 1c
                    ld        a,($ec0d)                     ;[295e] 3a 0d ec
                    bit       3,a                           ;[2961] cb 5f
                    jr        z,$296a                       ;[2963] 28 05
                    call      $2c8e                         ;[2965] cd 8e 2c
                    jr        nc,$297f                      ;[2968] 30 15
                    call      $2c4c                         ;[296a] cd 4c 2c
                    call      $2b78                         ;[296d] cd 78 2b
                    call      $2ece                         ;[2970] cd ce 2e
                    ld        b,$00                         ;[2973] 06 00
                    pop       af                            ;[2975] f1
                    scf                                     ;[2976] 37
                    jp        $29f8                         ;[2977] c3 f8 29
                    pop       af                            ;[297a] f1
                    scf                                     ;[297b] 37
                    jp        $29f2                         ;[297c] c3 f2 29
                    pop       af                            ;[297f] f1
                    jp        $29f2                         ;[2980] c3 f2 29
                    ld        a,($ec0e)                     ;[2983] 3a 0e ec
                    cp        $04                           ;[2986] fe 04
                    ret       z                             ;[2988] c8
                    call      $29ec                         ;[2989] cd ec 29
                    ld        hl,$0000                      ;[298c] 21 00 00
                    call      $1f20                         ;[298f] cd 20 1f
                    rst       $28                           ;[2992] ef
                    ld        l,(hl)                        ;[2993] 6e
                    add       hl,de                         ;[2994] 19
                    rst       $28                           ;[2995] ef
                    sub       l                             ;[2996] 95
                    ld        d,$cd                         ;[2997] 16 cd
                    ld        b,l                           ;[2999] 45
                    rra                                     ;[299a] 1f
                    ld        ($5c49),de                    ;[299b] ed 53 49 5c
                    ld        a,$0f                         ;[299f] 3e 0f
                    call      $3a96                         ;[29a1] cd 96 3a
                    call      $152f                         ;[29a4] cd 2f 15
                    scf                                     ;[29a7] 37
                    jp        $29f2                         ;[29a8] c3 f2 29
                    ld        a,($ec0e)                     ;[29ab] 3a 0e ec
                    cp        $04                           ;[29ae] fe 04
                    ret       z                             ;[29b0] c8
                    call      $29ec                         ;[29b1] cd ec 29
                    ld        hl,$270f                      ;[29b4] 21 0f 27
                    call      $1f20                         ;[29b7] cd 20 1f
                    rst       $28                           ;[29ba] ef
                    ld        l,(hl)                        ;[29bb] 6e
                    add       hl,de                         ;[29bc] 19
                    ex        de,hl                         ;[29bd] eb
                    rst       $28                           ;[29be] ef
                    sub       l                             ;[29bf] 95
                    ld        d,$cd                         ;[29c0] 16 cd
                    ld        b,l                           ;[29c2] 45
                    rra                                     ;[29c3] 1f
                    ld        ($5c49),de                    ;[29c4] ed 53 49 5c
                    ld        a,$0f                         ;[29c8] 3e 0f
                    call      $3a96                         ;[29ca] cd 96 3a
                    call      $152f                         ;[29cd] cd 2f 15
                    scf                                     ;[29d0] 37
                    jp        $29f2                         ;[29d1] c3 f2 29
                    call      $29ec                         ;[29d4] cd ec 29
                    call      $2bea                         ;[29d7] cd ea 2b
                    jp        nc,$29f2                      ;[29da] d2 f2 29
                    ld        a,b                           ;[29dd] 78
                    jp        $29f8                         ;[29de] c3 f8 29
                    call      $29ec                         ;[29e1] cd ec 29
                    call      $2c09                         ;[29e4] cd 09 2c
                    jr        nc,$29f2                      ;[29e7] 30 09
                    ld        a,b                           ;[29e9] 78
                    jr        $29f8                         ;[29ea] 18 0c
                    call      $2a07                         ;[29ec] cd 07 2a
                    jp        $364f                         ;[29ef] c3 4f 36
                    call      $2a07                         ;[29f2] cd 07 2a
                    jp        $3640                         ;[29f5] c3 40 36
                    call      $2a11                         ;[29f8] cd 11 2a
                    push      af                            ;[29fb] f5
                    push      bc                            ;[29fc] c5
                    ld        a,$0f                         ;[29fd] 3e 0f
                    call      $3a96                         ;[29ff] cd 96 3a
                    pop       bc                            ;[2a02] c1
                    pop       af                            ;[2a03] f1
                    jp        $3640                         ;[2a04] c3 40 36
                    ld        hl,$f6ee                      ;[2a07] 21 ee f6
                    ld        c,(hl)                        ;[2a0a] 4e
                    inc       hl                            ;[2a0b] 23
                    ld        b,(hl)                        ;[2a0c] 46
                    inc       hl                            ;[2a0d] 23
                    ld        a,(hl)                        ;[2a0e] 7e
                    inc       hl                            ;[2a0f] 23
                    ret                                     ;[2a10] c9

                    ld        hl,$f6ee                      ;[2a11] 21 ee f6
                    ld        (hl),c                        ;[2a14] 71
                    inc       hl                            ;[2a15] 23
                    ld        (hl),b                        ;[2a16] 70
                    inc       hl                            ;[2a17] 23
                    ld        (hl),a                        ;[2a18] 77
                    ret                                     ;[2a19] c9

                    push      hl                            ;[2a1a] e5
                    call      $30b4                         ;[2a1b] cd b4 30
                    ld        h,$00                         ;[2a1e] 26 00
                    ld        l,b                           ;[2a20] 68
                    add       hl,de                         ;[2a21] 19
                    ld        a,(hl)                        ;[2a22] 7e
                    pop       hl                            ;[2a23] e1
                    ret                                     ;[2a24] c9

                    call      $29ec                         ;[2a25] cd ec 29
                    ld        e,a                           ;[2a28] 5f
                    ld        d,$0a                         ;[2a29] 16 0a
                    push      de                            ;[2a2b] d5
                    call      $2b30                         ;[2a2c] cd 30 2b
                    pop       de                            ;[2a2f] d1
                    jr        nc,$29f2                      ;[2a30] 30 c0
                    ld        a,e                           ;[2a32] 7b
                    call      $2a11                         ;[2a33] cd 11 2a
                    ld        b,e                           ;[2a36] 43
                    call      $2af9                         ;[2a37] cd f9 2a
                    jr        nc,$2a42                      ;[2a3a] 30 06
                    dec       d                             ;[2a3c] 15
                    jr        nz,$2a2b                      ;[2a3d] 20 ec
                    ld        a,e                           ;[2a3f] 7b
                    jr        c,$29f8                       ;[2a40] 38 b6
                    push      de                            ;[2a42] d5
                    call      $2b0b                         ;[2a43] cd 0b 2b
                    pop       de                            ;[2a46] d1
                    ld        b,e                           ;[2a47] 43
                    call      $2af9                         ;[2a48] cd f9 2a
                    ld        a,e                           ;[2a4b] 7b
                    or        a                             ;[2a4c] b7
                    jr        $29f8                         ;[2a4d] 18 a9
                    call      $29ec                         ;[2a4f] cd ec 29
                    ld        e,a                           ;[2a52] 5f
                    ld        d,$0a                         ;[2a53] 16 0a
                    push      de                            ;[2a55] d5
                    call      $2b0b                         ;[2a56] cd 0b 2b
                    pop       de                            ;[2a59] d1
                    jr        nc,$29f2                      ;[2a5a] 30 96
                    ld        a,e                           ;[2a5c] 7b
                    call      $2a11                         ;[2a5d] cd 11 2a
                    ld        b,e                           ;[2a60] 43
                    call      $2b02                         ;[2a61] cd 02 2b
                    jr        nc,$2a6d                      ;[2a64] 30 07
                    dec       d                             ;[2a66] 15
                    jr        nz,$2a55                      ;[2a67] 20 ec
                    ld        a,e                           ;[2a69] 7b
                    jp        c,$29f8                       ;[2a6a] da f8 29
                    push      af                            ;[2a6d] f5
                    call      $2b30                         ;[2a6e] cd 30 2b
                    ld        b,$00                         ;[2a71] 06 00
                    call      $2bd4                         ;[2a73] cd d4 2b
                    pop       af                            ;[2a76] f1
                    jp        $29f8                         ;[2a77] c3 f8 29
                    call      $29ec                         ;[2a7a] cd ec 29
                    call      $2c4c                         ;[2a7d] cd 4c 2c
                    jp        nc,$29f2                      ;[2a80] d2 f2 29
                    ld        a,b                           ;[2a83] 78
                    jp        $29f8                         ;[2a84] c3 f8 29
                    call      $29ec                         ;[2a87] cd ec 29
                    call      $2c31                         ;[2a8a] cd 31 2c
                    jp        nc,$29f2                      ;[2a8d] d2 f2 29
                    ld        a,b                           ;[2a90] 78
                    jp        $29f8                         ;[2a91] c3 f8 29
                    call      $29ec                         ;[2a94] cd ec 29
                    ld        e,a                           ;[2a97] 5f
                    push      de                            ;[2a98] d5
                    call      $2b0b                         ;[2a99] cd 0b 2b
                    pop       de                            ;[2a9c] d1
                    jp        nc,$29f2                      ;[2a9d] d2 f2 29
                    ld        b,e                           ;[2aa0] 43
                    call      $2b02                         ;[2aa1] cd 02 2b
                    ld        a,e                           ;[2aa4] 7b
                    jp        c,$29f8                       ;[2aa5] da f8 29
                    push      af                            ;[2aa8] f5
                    call      $2b30                         ;[2aa9] cd 30 2b
                    ld        b,$00                         ;[2aac] 06 00
                    call      $2af9                         ;[2aae] cd f9 2a
                    pop       af                            ;[2ab1] f1
                    jp        $29f8                         ;[2ab2] c3 f8 29
                    call      $29ec                         ;[2ab5] cd ec 29
                    ld        e,a                           ;[2ab8] 5f
                    push      de                            ;[2ab9] d5
                    call      $2b30                         ;[2aba] cd 30 2b
                    pop       de                            ;[2abd] d1
                    jp        nc,$29f2                      ;[2abe] d2 f2 29
                    ld        b,e                           ;[2ac1] 43
                    call      $2b02                         ;[2ac2] cd 02 2b
                    ld        a,e                           ;[2ac5] 7b
                    jp        c,$29f8                       ;[2ac6] da f8 29
                    push      de                            ;[2ac9] d5
                    call      $2b0b                         ;[2aca] cd 0b 2b
                    pop       de                            ;[2acd] d1
                    ld        b,e                           ;[2ace] 43
                    call      $2af9                         ;[2acf] cd f9 2a
                    ld        a,e                           ;[2ad2] 7b
                    or        a                             ;[2ad3] b7
                    jp        $29f8                         ;[2ad4] c3 f8 29
                    call      $29ec                         ;[2ad7] cd ec 29
                    call      $2b5b                         ;[2ada] cd 5b 2b
                    jp        c,$29f8                       ;[2add] da f8 29
                    jp        $29f2                         ;[2ae0] c3 f2 29
                    call      $29ec                         ;[2ae3] cd ec 29
                    call      $2b78                         ;[2ae6] cd 78 2b
                    jp        c,$29f8                       ;[2ae9] da f8 29
                    push      af                            ;[2aec] f5
                    call      $2b0b                         ;[2aed] cd 0b 2b
                    ld        b,$1f                         ;[2af0] 06 1f
                    call      $2bdf                         ;[2af2] cd df 2b
                    pop       af                            ;[2af5] f1
                    jp        $29f8                         ;[2af6] c3 f8 29
                    push      de                            ;[2af9] d5
                    call      $2bd4                         ;[2afa] cd d4 2b
                    call      nc,$2bdf                      ;[2afd] d4 df 2b
                    pop       de                            ;[2b00] d1
                    ret                                     ;[2b01] c9

                    push      de                            ;[2b02] d5
                    call      $2bdf                         ;[2b03] cd df 2b
                    call      nc,$2bd4                      ;[2b06] d4 d4 2b
                    pop       de                            ;[2b09] d1
                    ret                                     ;[2b0a] c9

                    call      $2c7c                         ;[2b0b] cd 7c 2c
                    jr        nc,$2b2f                      ;[2b0e] 30 1f
                    push      bc                            ;[2b10] c5
                    call      $30b4                         ;[2b11] cd b4 30
                    ld        b,$00                         ;[2b14] 06 00
                    call      $2e41                         ;[2b16] cd 41 2e
                    call      nc,$2f80                      ;[2b19] d4 80 2f
                    pop       bc                            ;[2b1c] c1
                    ld        hl,$f6f1                      ;[2b1d] 21 f1 f6
                    ld        a,(hl)                        ;[2b20] 7e
                    cp        c                             ;[2b21] b9
                    jr        c,$2b2d                       ;[2b22] 38 09
                    push      bc                            ;[2b24] c5
                    call      $166f                         ;[2b25] cd 6f 16
                    pop       bc                            ;[2b28] c1
                    ret       c                             ;[2b29] d8
                    ld        a,c                           ;[2b2a] 79
                    or        a                             ;[2b2b] b7
                    ret       z                             ;[2b2c] c8
                    dec       c                             ;[2b2d] 0d
                    scf                                     ;[2b2e] 37
                    ret                                     ;[2b2f] c9

                    push      bc                            ;[2b30] c5
                    call      $30b4                         ;[2b31] cd b4 30
                    ld        b,$00                         ;[2b34] 06 00
                    call      $2e41                         ;[2b36] cd 41 2e
                    pop       bc                            ;[2b39] c1
                    jr        c,$2b3f                       ;[2b3a] 38 03
                    jp        $2f80                         ;[2b3c] c3 80 2f
                    call      $2c68                         ;[2b3f] cd 68 2c
                    jr        nc,$2b5a                      ;[2b42] 30 16
                    ld        hl,$f6f1                      ;[2b44] 21 f1 f6
                    inc       hl                            ;[2b47] 23
                    ld        a,c                           ;[2b48] 79
                    cp        (hl)                          ;[2b49] be
                    jr        c,$2b58                       ;[2b4a] 38 0c
                    push      bc                            ;[2b4c] c5
                    push      hl                            ;[2b4d] e5
                    call      $1639                         ;[2b4e] cd 39 16
                    pop       hl                            ;[2b51] e1
                    pop       bc                            ;[2b52] c1
                    ret       c                             ;[2b53] d8
                    inc       hl                            ;[2b54] 23
                    ld        a,(hl)                        ;[2b55] 7e
                    cp        c                             ;[2b56] b9
                    ret       z                             ;[2b57] c8
                    inc       c                             ;[2b58] 0c
                    scf                                     ;[2b59] 37
                    ret                                     ;[2b5a] c9

                    ld        d,a                           ;[2b5b] 57
                    dec       b                             ;[2b5c] 05
                    jp        m,$2b66                       ;[2b5d] fa 66 2b
                    ld        e,b                           ;[2b60] 58
                    call      $2bdf                         ;[2b61] cd df 2b
                    ld        a,e                           ;[2b64] 7b
                    ret       c                             ;[2b65] d8
                    push      de                            ;[2b66] d5
                    call      $2b0b                         ;[2b67] cd 0b 2b
                    pop       de                            ;[2b6a] d1
                    ld        a,e                           ;[2b6b] 7b
                    ret       nc                            ;[2b6c] d0
                    ld        b,$1f                         ;[2b6d] 06 1f
                    call      $2bdf                         ;[2b6f] cd df 2b
                    ld        a,b                           ;[2b72] 78
                    ret       c                             ;[2b73] d8
                    ld        a,d                           ;[2b74] 7a
                    ld        b,$00                         ;[2b75] 06 00
                    ret                                     ;[2b77] c9

                    ld        d,a                           ;[2b78] 57
                    inc       b                             ;[2b79] 04
                    ld        a,$1f                         ;[2b7a] 3e 1f
                    cp        b                             ;[2b7c] b8
                    jr        c,$2b85                       ;[2b7d] 38 06
                    ld        e,b                           ;[2b7f] 58
                    call      $2bd4                         ;[2b80] cd d4 2b
                    ld        a,e                           ;[2b83] 7b
                    ret       c                             ;[2b84] d8
                    dec       b                             ;[2b85] 05
                    push      bc                            ;[2b86] c5
                    push      hl                            ;[2b87] e5
                    ld        hl,$ec0d                      ;[2b88] 21 0d ec
                    bit       7,(hl)                        ;[2b8b] cb 7e
                    jr        nz,$2bc0                      ;[2b8d] 20 31
                    call      $30b4                         ;[2b8f] cd b4 30
                    ld        hl,$0020                      ;[2b92] 21 20 00
                    add       hl,de                         ;[2b95] 19
                    ld        a,(hl)                        ;[2b96] 7e
                    bit       1,a                           ;[2b97] cb 4f
                    jr        nz,$2bc0                      ;[2b99] 20 25
                    set       1,(hl)                        ;[2b9b] cb ce
                    res       3,(hl)                        ;[2b9d] cb 9e
                    ld        hl,$0023                      ;[2b9f] 21 23 00
                    add       hl,de                         ;[2ba2] 19
                    ex        de,hl                         ;[2ba3] eb
                    pop       hl                            ;[2ba4] e1
                    pop       bc                            ;[2ba5] c1
                    push      af                            ;[2ba6] f5
                    call      $2b30                         ;[2ba7] cd 30 2b
                    pop       af                            ;[2baa] f1
                    call      $30b4                         ;[2bab] cd b4 30
                    ld        hl,$0023                      ;[2bae] 21 23 00
                    add       hl,de                         ;[2bb1] 19
                    ex        de,hl                         ;[2bb2] eb
                    res       0,a                           ;[2bb3] cb 87
                    set       3,a                           ;[2bb5] cb df
                    call      $2ed3                         ;[2bb7] cd d3 2e
                    call      $35f4                         ;[2bba] cd f4 35
                    ld        a,b                           ;[2bbd] 78
                    scf                                     ;[2bbe] 37
                    ret                                     ;[2bbf] c9

                    pop       hl                            ;[2bc0] e1
                    pop       bc                            ;[2bc1] c1
                    push      de                            ;[2bc2] d5
                    call      $2b30                         ;[2bc3] cd 30 2b
                    pop       de                            ;[2bc6] d1
                    ld        a,b                           ;[2bc7] 78
                    ret       nc                            ;[2bc8] d0
                    ld        b,$00                         ;[2bc9] 06 00
                    call      $2bd4                         ;[2bcb] cd d4 2b
                    ld        a,b                           ;[2bce] 78
                    ret       c                             ;[2bcf] d8
                    ld        a,e                           ;[2bd0] 7b
                    ld        b,$00                         ;[2bd1] 06 00
                    ret                                     ;[2bd3] c9

                    push      de                            ;[2bd4] d5
                    push      hl                            ;[2bd5] e5
                    call      $30b4                         ;[2bd6] cd b4 30
                    call      $2e41                         ;[2bd9] cd 41 2e
                    jp        $2c65                         ;[2bdc] c3 65 2c
                    push      de                            ;[2bdf] d5
                    push      hl                            ;[2be0] e5
                    call      $30b4                         ;[2be1] cd b4 30
                    call      $2e63                         ;[2be4] cd 63 2e
                    jp        $2c65                         ;[2be7] c3 65 2c
                    push      de                            ;[2bea] d5
                    push      hl                            ;[2beb] e5
                    call      $2b5b                         ;[2bec] cd 5b 2b
                    jr        nc,$2c07                      ;[2bef] 30 16
                    call      $2a1a                         ;[2bf1] cd 1a 2a
                    cp        $20                           ;[2bf4] fe 20
                    jr        z,$2bec                       ;[2bf6] 28 f4
                    call      $2b5b                         ;[2bf8] cd 5b 2b
                    jr        nc,$2c07                      ;[2bfb] 30 0a
                    call      $2a1a                         ;[2bfd] cd 1a 2a
                    cp        $20                           ;[2c00] fe 20
                    jr        nz,$2bf8                      ;[2c02] 20 f4
                    call      $2b78                         ;[2c04] cd 78 2b
                    jr        $2c65                         ;[2c07] 18 5c
                    push      de                            ;[2c09] d5
                    push      hl                            ;[2c0a] e5
                    call      $2b78                         ;[2c0b] cd 78 2b
                    jr        nc,$2c2b                      ;[2c0e] 30 1b
                    call      $2a1a                         ;[2c10] cd 1a 2a
                    cp        $20                           ;[2c13] fe 20
                    jr        nz,$2c0b                      ;[2c15] 20 f4
                    call      $2b78                         ;[2c17] cd 78 2b
                    jr        nc,$2c2b                      ;[2c1a] 30 0f
                    call      $2e41                         ;[2c1c] cd 41 2e
                    jr        nc,$2c2b                      ;[2c1f] 30 0a
                    call      $2a1a                         ;[2c21] cd 1a 2a
                    cp        $20                           ;[2c24] fe 20
                    jr        z,$2c17                       ;[2c26] 28 ef
                    scf                                     ;[2c28] 37
                    jr        $2c65                         ;[2c29] 18 3a
                    call      nc,$2b5b                      ;[2c2b] d4 5b 2b
                    or        a                             ;[2c2e] b7
                    jr        $2c65                         ;[2c2f] 18 34
                    push      de                            ;[2c31] d5
                    push      hl                            ;[2c32] e5
                    call      $30b4                         ;[2c33] cd b4 30
                    ld        hl,$0020                      ;[2c36] 21 20 00
                    add       hl,de                         ;[2c39] 19
                    bit       0,(hl)                        ;[2c3a] cb 46
                    jr        nz,$2c45                      ;[2c3c] 20 07
                    call      $2b0b                         ;[2c3e] cd 0b 2b
                    jr        c,$2c33                       ;[2c41] 38 f0
                    jr        $2c65                         ;[2c43] 18 20
                    ld        b,$00                         ;[2c45] 06 00
                    call      $2bd4                         ;[2c47] cd d4 2b
                    jr        $2c65                         ;[2c4a] 18 19
                    push      de                            ;[2c4c] d5
                    push      hl                            ;[2c4d] e5
                    call      $30b4                         ;[2c4e] cd b4 30
                    ld        hl,$0020                      ;[2c51] 21 20 00
                    add       hl,de                         ;[2c54] 19
                    bit       3,(hl)                        ;[2c55] cb 5e
                    jr        nz,$2c60                      ;[2c57] 20 07
                    call      $2b30                         ;[2c59] cd 30 2b
                    jr        c,$2c4e                       ;[2c5c] 38 f0
                    jr        $2c65                         ;[2c5e] 18 05
                    ld        b,$1f                         ;[2c60] 06 1f
                    call      $2bdf                         ;[2c62] cd df 2b
                    pop       hl                            ;[2c65] e1
                    pop       de                            ;[2c66] d1
                    ret                                     ;[2c67] c9

                    ld        a,($ec0d)                     ;[2c68] 3a 0d ec
                    bit       3,a                           ;[2c6b] cb 5f
                    scf                                     ;[2c6d] 37
                    ret       z                             ;[2c6e] c8
                    call      $30b4                         ;[2c6f] cd b4 30
                    ld        hl,$0020                      ;[2c72] 21 20 00
                    add       hl,de                         ;[2c75] 19
                    bit       3,(hl)                        ;[2c76] cb 5e
                    scf                                     ;[2c78] 37
                    ret       z                             ;[2c79] c8
                    jr        $2c8e                         ;[2c7a] 18 12
                    ld        a,($ec0d)                     ;[2c7c] 3a 0d ec
                    bit       3,a                           ;[2c7f] cb 5f
                    scf                                     ;[2c81] 37
                    ret       z                             ;[2c82] c8
                    call      $30b4                         ;[2c83] cd b4 30
                    ld        hl,$0020                      ;[2c86] 21 20 00
                    add       hl,de                         ;[2c89] 19
                    bit       0,(hl)                        ;[2c8a] cb 46
                    scf                                     ;[2c8c] 37
                    ret       z                             ;[2c8d] c8
                    ld        a,$02                         ;[2c8e] 3e 02
                    call      $30b4                         ;[2c90] cd b4 30
                    ld        hl,$0020                      ;[2c93] 21 20 00
                    add       hl,de                         ;[2c96] 19
                    bit       0,(hl)                        ;[2c97] cb 46
                    jr        nz,$2ca3                      ;[2c99] 20 08
                    dec       c                             ;[2c9b] 0d
                    jp        p,$2c90                       ;[2c9c] f2 90 2c
                    ld        c,$00                         ;[2c9f] 0e 00
                    ld        a,$01                         ;[2ca1] 3e 01
                    ld        hl,$ec00                      ;[2ca3] 21 00 ec
                    ld        de,$ec03                      ;[2ca6] 11 03 ec
                    or        $80                           ;[2ca9] f6 80
                    ld        (hl),a                        ;[2cab] 77
                    ld        (de),a                        ;[2cac] 12
                    inc       hl                            ;[2cad] 23
                    inc       de                            ;[2cae] 13
                    ld        a,$00                         ;[2caf] 3e 00
                    ld        (hl),a                        ;[2cb1] 77
                    ld        (de),a                        ;[2cb2] 12
                    inc       hl                            ;[2cb3] 23
                    inc       de                            ;[2cb4] 13
                    ld        a,c                           ;[2cb5] 79
                    ld        (hl),a                        ;[2cb6] 77
                    ld        (de),a                        ;[2cb7] 12
                    ld        hl,$0000                      ;[2cb8] 21 00 00
                    ld        ($ec06),hl                    ;[2cbb] 22 06 ec
                    call      $335f                         ;[2cbe] cd 5f 33
                    call      $3c67                         ;[2cc1] cd 67 3c
                    push      ix                            ;[2cc4] dd e5
                    call      $1f20                         ;[2cc6] cd 20 1f
                    call      $026b                         ;[2cc9] cd 6b 02
                    call      $1f45                         ;[2ccc] cd 45 1f
                    pop       ix                            ;[2ccf] dd e1
                    ld        a,($5c3a)                     ;[2cd1] 3a 3a 5c
                    inc       a                             ;[2cd4] 3c
                    jr        nz,$2cef                      ;[2cd5] 20 18
                    ld        hl,$ec0d                      ;[2cd7] 21 0d ec
                    res       3,(hl)                        ;[2cda] cb 9e
                    call      $365e                         ;[2cdc] cd 5e 36
                    ld        a,($ec0e)                     ;[2cdf] 3a 0e ec
                    cp        $04                           ;[2ce2] fe 04
                    call      nz,$152f                      ;[2ce4] c4 2f 15
                    call      $26fa                         ;[2ce7] cd fa 26
                    call      $2a07                         ;[2cea] cd 07 2a
                    scf                                     ;[2ced] 37
                    ret                                     ;[2cee] c9

                    ld        hl,$ec00                      ;[2cef] 21 00 ec
                    ld        de,$ec03                      ;[2cf2] 11 03 ec
                    ld        a,(de)                        ;[2cf5] 1a
                    res       7,a                           ;[2cf6] cb bf
                    ld        (hl),a                        ;[2cf8] 77
                    inc       hl                            ;[2cf9] 23
                    inc       de                            ;[2cfa] 13
                    ld        a,(de)                        ;[2cfb] 1a
                    ld        (hl),a                        ;[2cfc] 77
                    inc       hl                            ;[2cfd] 23
                    inc       de                            ;[2cfe] 13
                    ld        a,(de)                        ;[2cff] 1a
                    ld        (hl),a                        ;[2d00] 77
                    call      $3c63                         ;[2d01] cd 63 3c
                    jr        c,$2d0a                       ;[2d04] 38 04
                    ld        bc,($ec06)                    ;[2d06] ed 4b 06 ec
                    ld        hl,($ec06)                    ;[2d0a] 2a 06 ec
                    or        a                             ;[2d0d] b7
                    sbc       hl,bc                         ;[2d0e] ed 42
                    push      af                            ;[2d10] f5
                    push      hl                            ;[2d11] e5
                    call      $2a07                         ;[2d12] cd 07 2a
                    pop       hl                            ;[2d15] e1
                    pop       af                            ;[2d16] f1
                    jr        c,$2d2a                       ;[2d17] 38 11
                    jr        z,$2d45                       ;[2d19] 28 2a
                    push      hl                            ;[2d1b] e5
                    ld        a,b                           ;[2d1c] 78
                    call      $2b5b                         ;[2d1d] cd 5b 2b
                    pop       hl                            ;[2d20] e1
                    jr        nc,$2d45                      ;[2d21] 30 22
                    dec       hl                            ;[2d23] 2b
                    ld        a,h                           ;[2d24] 7c
                    or        l                             ;[2d25] b5
                    jr        nz,$2d1b                      ;[2d26] 20 f3
                    jr        $2d45                         ;[2d28] 18 1b
                    push      hl                            ;[2d2a] e5
                    ld        hl,$ec0d                      ;[2d2b] 21 0d ec
                    res       7,(hl)                        ;[2d2e] cb be
                    pop       hl                            ;[2d30] e1
                    ex        de,hl                         ;[2d31] eb
                    ld        hl,$0000                      ;[2d32] 21 00 00
                    or        a                             ;[2d35] b7
                    sbc       hl,de                         ;[2d36] ed 52
                    push      hl                            ;[2d38] e5
                    ld        a,b                           ;[2d39] 78
                    call      $2b78                         ;[2d3a] cd 78 2b
                    pop       hl                            ;[2d3d] e1
                    jr        nc,$2d45                      ;[2d3e] 30 05
                    dec       hl                            ;[2d40] 2b
                    ld        a,h                           ;[2d41] 7c
                    or        l                             ;[2d42] b5
                    jr        nz,$2d38                      ;[2d43] 20 f3
                    ld        hl,$ec0d                      ;[2d45] 21 0d ec
                    set       7,(hl)                        ;[2d48] cb fe
                    call      $2a11                         ;[2d4a] cd 11 2a
                    ld        a,$17                         ;[2d4d] 3e 17
                    call      $3a96                         ;[2d4f] cd 96 3a
                    or        a                             ;[2d52] b7
                    ret                                     ;[2d53] c9

                    ld        hl,$ec00                      ;[2d54] 21 00 ec
                    bit       7,(hl)                        ;[2d57] cb 7e
                    jr        z,$2d62                       ;[2d59] 28 07
                    ld        hl,($ec06)                    ;[2d5b] 2a 06 ec
                    inc       hl                            ;[2d5e] 23
                    ld        ($ec06),hl                    ;[2d5f] 22 06 ec
                    ld        hl,$ec00                      ;[2d62] 21 00 ec
                    ld        a,(hl)                        ;[2d65] 7e
                    inc       hl                            ;[2d66] 23
                    ld        b,(hl)                        ;[2d67] 46
                    inc       hl                            ;[2d68] 23
                    ld        c,(hl)                        ;[2d69] 4e
                    push      hl                            ;[2d6a] e5
                    and       $0f                           ;[2d6b] e6 0f
                    ld        hl,$2d85                      ;[2d6d] 21 85 2d
                    call      $3fce                         ;[2d70] cd ce 3f
                    ld        e,l                           ;[2d73] 5d
                    pop       hl                            ;[2d74] e1
                    jr        z,$2d79                       ;[2d75] 28 02
                    ld        a,$0d                         ;[2d77] 3e 0d
                    ld        (hl),c                        ;[2d79] 71
                    dec       hl                            ;[2d7a] 2b
                    ld        (hl),b                        ;[2d7b] 70
                    dec       hl                            ;[2d7c] 2b
                    push      af                            ;[2d7d] f5
                    ld        a,(hl)                        ;[2d7e] 7e
                    and       $f0                           ;[2d7f] e6 f0
                    or        e                             ;[2d81] b3
                    ld        (hl),a                        ;[2d82] 77
                    pop       af                            ;[2d83] f1
                    ret                                     ;[2d84] c9

                    inc       bc                            ;[2d85] 03
                    ld        (bc),a                        ;[2d86] 02
                    xor       h                             ;[2d87] ac
                    dec       l                             ;[2d88] 2d
                    inc       b                             ;[2d89] 04
                    jp        (hl)                          ;[2d8a] e9
                    dec       l                             ;[2d8b] 2d
                    ld        bc,$2d8f                      ;[2d8c] 01 8f 2d
                    call      $32b7                         ;[2d8f] cd b7 32
                    call      $2e0e                         ;[2d92] cd 0e 2e
                    jr        nc,$2d9e                      ;[2d95] 30 07
                    cp        $00                           ;[2d97] fe 00
                    jr        z,$2d92                       ;[2d99] 28 f7
                    ld        l,$01                         ;[2d9b] 2e 01
                    ret                                     ;[2d9d] c9

                    inc       c                             ;[2d9e] 0c
                    ld        b,$00                         ;[2d9f] 06 00
                    ld        hl,($f9db)                    ;[2da1] 2a db f9
                    ld        a,c                           ;[2da4] 79
                    cp        (hl)                          ;[2da5] be
                    jr        c,$2d8f                       ;[2da6] 38 e7
                    ld        b,$00                         ;[2da8] 06 00
                    ld        c,$00                         ;[2daa] 0e 00
                    push      hl                            ;[2dac] e5
                    ld        hl,$f6ee                      ;[2dad] 21 ee f6
                    ld        a,(hl)                        ;[2db0] 7e
                    cp        c                             ;[2db1] b9
                    jr        nz,$2dbe                      ;[2db2] 20 0a
                    inc       hl                            ;[2db4] 23
                    ld        a,(hl)                        ;[2db5] 7e
                    cp        b                             ;[2db6] b8
                    jr        nz,$2dbe                      ;[2db7] 20 05
                    ld        hl,$ec00                      ;[2db9] 21 00 ec
                    res       7,(hl)                        ;[2dbc] cb be
                    pop       hl                            ;[2dbe] e1
                    call      $30b4                         ;[2dbf] cd b4 30
                    call      $2e0e                         ;[2dc2] cd 0e 2e
                    jr        nc,$2dce                      ;[2dc5] 30 07
                    cp        $00                           ;[2dc7] fe 00
                    jr        z,$2dac                       ;[2dc9] 28 e1
                    ld        l,$02                         ;[2dcb] 2e 02
                    ret                                     ;[2dcd] c9

                    ld        hl,$0020                      ;[2dce] 21 20 00
                    add       hl,de                         ;[2dd1] 19
                    bit       3,(hl)                        ;[2dd2] cb 5e
                    jr        z,$2ddb                       ;[2dd4] 28 05
                    ld        l,$08                         ;[2dd6] 2e 08
                    ld        a,$0d                         ;[2dd8] 3e 0d
                    ret                                     ;[2dda] c9

                    ld        hl,$f6f3                      ;[2ddb] 21 f3 f6
                    inc       c                             ;[2dde] 0c
                    ld        a,(hl)                        ;[2ddf] 7e
                    cp        c                             ;[2de0] b9
                    ld        b,$00                         ;[2de1] 06 00
                    jr        nc,$2dbf                      ;[2de3] 30 da
                    ld        b,$00                         ;[2de5] 06 00
                    ld        c,$01                         ;[2de7] 0e 01
                    call      $31c3                         ;[2de9] cd c3 31
                    call      $2e0e                         ;[2dec] cd 0e 2e
                    jr        nc,$2df8                      ;[2def] 30 07
                    cp        $00                           ;[2df1] fe 00
                    jr        z,$2dec                       ;[2df3] 28 f7
                    ld        l,$04                         ;[2df5] 2e 04
                    ret                                     ;[2df7] c9

                    ld        hl,$0020                      ;[2df8] 21 20 00
                    add       hl,de                         ;[2dfb] 19
                    bit       3,(hl)                        ;[2dfc] cb 5e
                    jr        nz,$2e09                      ;[2dfe] 20 09
                    inc       c                             ;[2e00] 0c
                    ld        b,$00                         ;[2e01] 06 00
                    ld        a,($f6f5)                     ;[2e03] 3a f5 f6
                    cp        c                             ;[2e06] b9
                    jr        nc,$2de9                      ;[2e07] 30 e0
                    ld        l,$08                         ;[2e09] 2e 08
                    ld        a,$0d                         ;[2e0b] 3e 0d
                    ret                                     ;[2e0d] c9

                    ld        a,$1f                         ;[2e0e] 3e 1f
                    cp        b                             ;[2e10] b8
                    ccf                                     ;[2e11] 3f
                    ret       nc                            ;[2e12] d0
                    ld        l,b                           ;[2e13] 68
                    ld        h,$00                         ;[2e14] 26 00
                    add       hl,de                         ;[2e16] 19
                    ld        a,(hl)                        ;[2e17] 7e
                    inc       b                             ;[2e18] 04
                    scf                                     ;[2e19] 37
                    ret                                     ;[2e1a] c9

                    ld        bc,$0114                      ;[2e1b] 01 14 01
                    ld        bc,$3c21                      ;[2e1e] 01 21 3c
                    ld        e,h                           ;[2e21] 5c
                    res       0,(hl)                        ;[2e22] cb 86
                    ld        hl,$2e1b                      ;[2e24] 21 1b 2e
                    ld        de,$ec15                      ;[2e27] 11 15 ec
                    jp        $3fba                         ;[2e2a] c3 ba 3f
                    ld        hl,$5c3c                      ;[2e2d] 21 3c 5c
                    set       0,(hl)                        ;[2e30] cb c6
                    ld        bc,$0000                      ;[2e32] 01 00 00
                    call      $372b                         ;[2e35] cd 2b 37
                    ld        hl,$2e1d                      ;[2e38] 21 1d 2e
                    ld        de,$ec15                      ;[2e3b] 11 15 ec
                    jp        $3fba                         ;[2e3e] c3 ba 3f
                    ld        h,$00                         ;[2e41] 26 00
                    ld        l,b                           ;[2e43] 68
                    add       hl,de                         ;[2e44] 19
                    ld        a,(hl)                        ;[2e45] 7e
                    cp        $00                           ;[2e46] fe 00
                    scf                                     ;[2e48] 37
                    ret       nz                            ;[2e49] c0
                    ld        a,b                           ;[2e4a] 78
                    or        a                             ;[2e4b] b7
                    jr        z,$2e5b                       ;[2e4c] 28 0d
                    push      hl                            ;[2e4e] e5
                    dec       hl                            ;[2e4f] 2b
                    ld        a,(hl)                        ;[2e50] 7e
                    cp        $00                           ;[2e51] fe 00
                    scf                                     ;[2e53] 37
                    pop       hl                            ;[2e54] e1
                    ret       nz                            ;[2e55] c0
                    ld        a,(hl)                        ;[2e56] 7e
                    cp        $00                           ;[2e57] fe 00
                    scf                                     ;[2e59] 37
                    ret       nz                            ;[2e5a] c0
                    inc       hl                            ;[2e5b] 23
                    inc       b                             ;[2e5c] 04
                    ld        a,b                           ;[2e5d] 78
                    cp        $1f                           ;[2e5e] fe 1f
                    jr        c,$2e56                       ;[2e60] 38 f4
                    ret                                     ;[2e62] c9

                    ld        h,$00                         ;[2e63] 26 00
                    ld        l,b                           ;[2e65] 68
                    add       hl,de                         ;[2e66] 19
                    ld        a,(hl)                        ;[2e67] 7e
                    cp        $00                           ;[2e68] fe 00
                    scf                                     ;[2e6a] 37
                    ret       nz                            ;[2e6b] c0
                    ld        a,(hl)                        ;[2e6c] 7e
                    cp        $00                           ;[2e6d] fe 00
                    jr        nz,$2e78                      ;[2e6f] 20 07
                    ld        a,b                           ;[2e71] 78
                    or        a                             ;[2e72] b7
                    ret       z                             ;[2e73] c8
                    dec       hl                            ;[2e74] 2b
                    dec       b                             ;[2e75] 05
                    jr        $2e6c                         ;[2e76] 18 f4
                    inc       b                             ;[2e78] 04
                    scf                                     ;[2e79] 37
                    ret                                     ;[2e7a] c9

                    ld        h,$00                         ;[2e7b] 26 00
                    ld        l,b                           ;[2e7d] 68
                    add       hl,de                         ;[2e7e] 19
                    ld        a,(hl)                        ;[2e7f] 7e
                    ret                                     ;[2e80] c9

                    ld        hl,$ec0d                      ;[2e81] 21 0d ec
                    or        a                             ;[2e84] b7
                    bit       0,(hl)                        ;[2e85] cb 46
                    ret       nz                            ;[2e87] c0
                    push      bc                            ;[2e88] c5
                    push      af                            ;[2e89] f5
                    call      $30b4                         ;[2e8a] cd b4 30
                    pop       af                            ;[2e8d] f1
                    call      $16ac                         ;[2e8e] cd ac 16
                    push      af                            ;[2e91] f5
                    ex        de,hl                         ;[2e92] eb
                    call      $3604                         ;[2e93] cd 04 36
                    ex        de,hl                         ;[2e96] eb
                    pop       af                            ;[2e97] f1
                    ccf                                     ;[2e98] 3f
                    jr        z,$2ecc                       ;[2e99] 28 31
                    push      af                            ;[2e9b] f5
                    ld        b,$00                         ;[2e9c] 06 00
                    inc       c                             ;[2e9e] 0c
                    ld        a,($ec15)                     ;[2e9f] 3a 15 ec
                    cp        c                             ;[2ea2] b9
                    jr        c,$2ec8                       ;[2ea3] 38 23
                    ld        a,(hl)                        ;[2ea5] 7e
                    ld        e,a                           ;[2ea6] 5f
                    and       $d7                           ;[2ea7] e6 d7
                    cp        (hl)                          ;[2ea9] be
                    ld        (hl),a                        ;[2eaa] 77
                    ld        a,e                           ;[2eab] 7b
                    set       1,(hl)                        ;[2eac] cb ce
                    push      af                            ;[2eae] f5
                    call      $30b4                         ;[2eaf] cd b4 30
                    pop       af                            ;[2eb2] f1
                    jr        z,$2ec2                       ;[2eb3] 28 0d
                    res       0,a                           ;[2eb5] cb 87
                    call      $2ed3                         ;[2eb7] cd d3 2e
                    jr        nc,$2ecc                      ;[2eba] 30 10
                    call      $35f4                         ;[2ebc] cd f4 35
                    pop       af                            ;[2ebf] f1
                    jr        $2e8e                         ;[2ec0] 18 cc
                    call      $2e41                         ;[2ec2] cd 41 2e
                    pop       af                            ;[2ec5] f1
                    jr        $2e8e                         ;[2ec6] 18 c6
                    pop       af                            ;[2ec8] f1
                    call      $316e                         ;[2ec9] cd 6e 31
                    pop       bc                            ;[2ecc] c1
                    ret                                     ;[2ecd] c9

                    call      $30b4                         ;[2ece] cd b4 30
                    ld        a,$09                         ;[2ed1] 3e 09
                    push      bc                            ;[2ed3] c5
                    push      de                            ;[2ed4] d5
                    ld        b,c                           ;[2ed5] 41
                    ld        hl,$2eef                      ;[2ed6] 21 ef 2e
                    ld        c,a                           ;[2ed9] 4f
                    push      bc                            ;[2eda] c5
                    call      $1675                         ;[2edb] cd 75 16
                    pop       bc                            ;[2ede] c1
                    ld        a,c                           ;[2edf] 79
                    jr        nc,$2eec                      ;[2ee0] 30 0a
                    ld        c,b                           ;[2ee2] 48
                    call      $30b4                         ;[2ee3] cd b4 30
                    ld        hl,$0020                      ;[2ee6] 21 20 00
                    add       hl,de                         ;[2ee9] 19
                    ld        (hl),a                        ;[2eea] 77
                    scf                                     ;[2eeb] 37
                    pop       de                            ;[2eec] d1
                    pop       bc                            ;[2eed] c1
                    ret                                     ;[2eee] c9

                    nop                                     ;[2eef] 00
                    nop                                     ;[2ef0] 00
                    nop                                     ;[2ef1] 00
                    nop                                     ;[2ef2] 00
                    nop                                     ;[2ef3] 00
                    nop                                     ;[2ef4] 00
                    nop                                     ;[2ef5] 00
                    nop                                     ;[2ef6] 00
                    nop                                     ;[2ef7] 00
                    nop                                     ;[2ef8] 00
                    nop                                     ;[2ef9] 00
                    nop                                     ;[2efa] 00
                    nop                                     ;[2efb] 00
                    nop                                     ;[2efc] 00
                    nop                                     ;[2efd] 00
                    nop                                     ;[2efe] 00
                    nop                                     ;[2eff] 00
                    nop                                     ;[2f00] 00
                    nop                                     ;[2f01] 00
                    nop                                     ;[2f02] 00
                    nop                                     ;[2f03] 00
                    nop                                     ;[2f04] 00
                    nop                                     ;[2f05] 00
                    nop                                     ;[2f06] 00
                    nop                                     ;[2f07] 00
                    nop                                     ;[2f08] 00
                    nop                                     ;[2f09] 00
                    nop                                     ;[2f0a] 00
                    nop                                     ;[2f0b] 00
                    nop                                     ;[2f0c] 00
                    nop                                     ;[2f0d] 00
                    nop                                     ;[2f0e] 00
                    add       hl,bc                         ;[2f0f] 09
                    nop                                     ;[2f10] 00
                    nop                                     ;[2f11] 00
                    push      bc                            ;[2f12] c5
                    call      $30b4                         ;[2f13] cd b4 30
                    push      bc                            ;[2f16] c5
                    ld        hl,$0020                      ;[2f17] 21 20 00
                    add       hl,de                         ;[2f1a] 19
                    bit       1,(hl)                        ;[2f1b] cb 4e
                    ld        a,$00                         ;[2f1d] 3e 00
                    jr        z,$2f31                       ;[2f1f] 28 10
                    inc       c                             ;[2f21] 0c
                    ld        hl,$0023                      ;[2f22] 21 23 00
                    add       hl,de                         ;[2f25] 19
                    ex        de,hl                         ;[2f26] eb
                    ld        a,($ec15)                     ;[2f27] 3a 15 ec
                    cp        c                             ;[2f2a] b9
                    jr        nc,$2f17                      ;[2f2b] 30 ea
                    dec       c                             ;[2f2d] 0d
                    call      $31c9                         ;[2f2e] cd c9 31
                    pop       hl                            ;[2f31] e1
                    push      hl                            ;[2f32] e5
                    call      $30b4                         ;[2f33] cd b4 30
                    pop       hl                            ;[2f36] e1
                    ld        b,a                           ;[2f37] 47
                    ld        a,c                           ;[2f38] 79
                    cp        l                             ;[2f39] bd
                    ld        a,b                           ;[2f3a] 78
                    push      af                            ;[2f3b] f5
                    jr        nz,$2f41                      ;[2f3c] 20 03
                    ld        b,h                           ;[2f3e] 44
                    jr        $2f4a                         ;[2f3f] 18 09
                    push      af                            ;[2f41] f5
                    push      hl                            ;[2f42] e5
                    ld        b,$00                         ;[2f43] 06 00
                    call      $2e41                         ;[2f45] cd 41 2e
                    pop       hl                            ;[2f48] e1
                    pop       af                            ;[2f49] f1
                    push      hl                            ;[2f4a] e5
                    ld        hl,$f6f4                      ;[2f4b] 21 f4 f6
                    set       0,(hl)                        ;[2f4e] cb c6
                    jr        z,$2f54                       ;[2f50] 28 02
                    res       0,(hl)                        ;[2f52] cb 86
                    call      $16c1                         ;[2f54] cd c1 16
                    push      af                            ;[2f57] f5
                    push      bc                            ;[2f58] c5
                    push      de                            ;[2f59] d5
                    ld        hl,$f6f4                      ;[2f5a] 21 f4 f6
                    bit       0,(hl)                        ;[2f5d] cb 46
                    jr        nz,$2f6f                      ;[2f5f] 20 0e
                    ld        b,$00                         ;[2f61] 06 00
                    call      $2bd4                         ;[2f63] cd d4 2b
                    jr        c,$2f6f                       ;[2f66] 38 07
                    call      $2f80                         ;[2f68] cd 80 2f
                    pop       de                            ;[2f6b] d1
                    pop       bc                            ;[2f6c] c1
                    jr        $2f74                         ;[2f6d] 18 05
                    pop       hl                            ;[2f6f] e1
                    pop       bc                            ;[2f70] c1
                    call      $3604                         ;[2f71] cd 04 36
                    pop       af                            ;[2f74] f1
                    dec       c                             ;[2f75] 0d
                    ld        b,a                           ;[2f76] 47
                    pop       hl                            ;[2f77] e1
                    pop       af                            ;[2f78] f1
                    ld        a,b                           ;[2f79] 78
                    jp        nz,$2f32                      ;[2f7a] c2 32 2f
                    scf                                     ;[2f7d] 37
                    pop       bc                            ;[2f7e] c1
                    ret                                     ;[2f7f] c9

                    ld        hl,$0020                      ;[2f80] 21 20 00
                    add       hl,de                         ;[2f83] 19
                    ld        a,(hl)                        ;[2f84] 7e
                    bit       0,(hl)                        ;[2f85] cb 46
                    jr        nz,$2fb2                      ;[2f87] 20 29
                    push      af                            ;[2f89] f5
                    push      bc                            ;[2f8a] c5
                    ld        a,c                           ;[2f8b] 79
                    or        a                             ;[2f8c] b7
                    jr        nz,$2fa4                      ;[2f8d] 20 15
                    push      bc                            ;[2f8f] c5
                    ld        hl,($fc9a)                    ;[2f90] 2a 9a fc
                    call      $334a                         ;[2f93] cd 4a 33
                    ld        ($fc9a),hl                    ;[2f96] 22 9a fc
                    ld        a,($f9db)                     ;[2f99] 3a db f9
                    ld        c,a                           ;[2f9c] 4f
                    dec       c                             ;[2f9d] 0d
                    call      $32b7                         ;[2f9e] cd b7 32
                    pop       bc                            ;[2fa1] c1
                    jr        $2fa8                         ;[2fa2] 18 04
                    dec       c                             ;[2fa4] 0d
                    call      $30b4                         ;[2fa5] cd b4 30
                    pop       bc                            ;[2fa8] c1
                    pop       af                            ;[2fa9] f1
                    ld        hl,$0020                      ;[2faa] 21 20 00
                    add       hl,de                         ;[2fad] 19
                    res       1,(hl)                        ;[2fae] cb 8e
                    or        (hl)                          ;[2fb0] b6
                    ld        (hl),a                        ;[2fb1] 77
                    ld        b,c                           ;[2fb2] 41
                    call      $30b4                         ;[2fb3] cd b4 30
                    call      $30df                         ;[2fb6] cd df 30
                    jp        $1648                         ;[2fb9] c3 48 16
                    call      $3084                         ;[2fbc] cd 84 30
                    push      hl                            ;[2fbf] e5
                    call      $3095                         ;[2fc0] cd 95 30
                    jr        z,$2ff7                       ;[2fc3] 28 32
                    call      $2b5b                         ;[2fc5] cd 5b 2b
                    pop       hl                            ;[2fc8] e1
                    jr        nc,$2ff8                      ;[2fc9] 30 2d
                    call      $2a1a                         ;[2fcb] cd 1a 2a
                    push      af                            ;[2fce] f5
                    push      hl                            ;[2fcf] e5
                    call      $2f12                         ;[2fd0] cd 12 2f
                    pop       hl                            ;[2fd3] e1
                    pop       af                            ;[2fd4] f1
                    cp        $20                           ;[2fd5] fe 20
                    jr        z,$2fbf                       ;[2fd7] 28 e6
                    push      hl                            ;[2fd9] e5
                    call      $3095                         ;[2fda] cd 95 30
                    jr        z,$2ff7                       ;[2fdd] 28 18
                    call      $2b5b                         ;[2fdf] cd 5b 2b
                    pop       hl                            ;[2fe2] e1
                    jr        nc,$2ff8                      ;[2fe3] 30 13
                    call      $2a1a                         ;[2fe5] cd 1a 2a
                    cp        $20                           ;[2fe8] fe 20
                    jr        z,$2ff3                       ;[2fea] 28 07
                    push      hl                            ;[2fec] e5
                    call      $2f12                         ;[2fed] cd 12 2f
                    pop       hl                            ;[2ff0] e1
                    jr        $2fd9                         ;[2ff1] 18 e6
                    push      hl                            ;[2ff3] e5
                    call      $2b78                         ;[2ff4] cd 78 2b
                    pop       hl                            ;[2ff7] e1
                    ld        a,b                           ;[2ff8] 78
                    push      af                            ;[2ff9] f5
                    push      hl                            ;[2ffa] e5
                    ld        hl,$eef5                      ;[2ffb] 21 f5 ee
                    res       2,(hl)                        ;[2ffe] cb 96
                    ld        a,($ec15)                     ;[3000] 3a 15 ec
                    push      bc                            ;[3003] c5
                    ld        b,$00                         ;[3004] 06 00
                    ld        c,a                           ;[3006] 4f
                    cp        a                             ;[3007] bf
                    call      $1605                         ;[3008] cd 05 16
                    pop       bc                            ;[300b] c1
                    ld        hl,$ec0d                      ;[300c] 21 0d ec
                    set       3,(hl)                        ;[300f] cb de
                    pop       hl                            ;[3011] e1
                    call      $29f8                         ;[3012] cd f8 29
                    pop       af                            ;[3015] f1
                    ret                                     ;[3016] c9

                    call      $3084                         ;[3017] cd 84 30
                    push      hl                            ;[301a] e5
                    call      $2a1a                         ;[301b] cd 1a 2a
                    pop       hl                            ;[301e] e1
                    cp        $00                           ;[301f] fe 00
                    scf                                     ;[3021] 37
                    jr        z,$2ff8                       ;[3022] 28 d4
                    push      af                            ;[3024] f5
                    push      hl                            ;[3025] e5
                    call      $2f12                         ;[3026] cd 12 2f
                    pop       hl                            ;[3029] e1
                    pop       af                            ;[302a] f1
                    cp        $20                           ;[302b] fe 20
                    jr        nz,$301a                      ;[302d] 20 eb
                    call      $2a1a                         ;[302f] cd 1a 2a
                    cp        $20                           ;[3032] fe 20
                    scf                                     ;[3034] 37
                    jr        nz,$2ff8                      ;[3035] 20 c1
                    push      hl                            ;[3037] e5
                    call      $2f12                         ;[3038] cd 12 2f
                    pop       hl                            ;[303b] e1
                    jr        $302f                         ;[303c] 18 f1
                    call      $3084                         ;[303e] cd 84 30
                    push      hl                            ;[3041] e5
                    call      $30b4                         ;[3042] cd b4 30
                    ld        hl,$0020                      ;[3045] 21 20 00
                    add       hl,de                         ;[3048] 19
                    bit       0,(hl)                        ;[3049] cb 46
                    jr        nz,$3059                      ;[304b] 20 0c
                    call      $2b5b                         ;[304d] cd 5b 2b
                    jr        nc,$306d                      ;[3050] 30 1b
                    call      $2f12                         ;[3052] cd 12 2f
                    pop       hl                            ;[3055] e1
                    jr        $3041                         ;[3056] 18 e9
                    push      hl                            ;[3058] e5
                    ld        a,b                           ;[3059] 78
                    cp        $00                           ;[305a] fe 00
                    jr        z,$306d                       ;[305c] 28 0f
                    dec       b                             ;[305e] 05
                    call      $2a1a                         ;[305f] cd 1a 2a
                    inc       b                             ;[3062] 04
                    cp        $00                           ;[3063] fe 00
                    jr        z,$306d                       ;[3065] 28 06
                    dec       b                             ;[3067] 05
                    call      $2f12                         ;[3068] cd 12 2f
                    jr        $3059                         ;[306b] 18 ec
                    pop       hl                            ;[306d] e1
                    scf                                     ;[306e] 37
                    jp        $2ff8                         ;[306f] c3 f8 2f
                    call      $3084                         ;[3072] cd 84 30
                    call      $2a1a                         ;[3075] cd 1a 2a
                    cp        $00                           ;[3078] fe 00
                    scf                                     ;[307a] 37
                    jr        z,$306e                       ;[307b] 28 f1
                    push      hl                            ;[307d] e5
                    call      $2f12                         ;[307e] cd 12 2f
                    pop       hl                            ;[3081] e1
                    jr        $3075                         ;[3082] 18 f1
                    ld        hl,$ec0d                      ;[3084] 21 0d ec
                    res       0,(hl)                        ;[3087] cb 86
                    call      $29ec                         ;[3089] cd ec 29
                    ld        hl,$eef5                      ;[308c] 21 f5 ee
                    set       2,(hl)                        ;[308f] cb d6
                    ld        hl,$f6f1                      ;[3091] 21 f1 f6
                    ret                                     ;[3094] c9

                    call      $30b4                         ;[3095] cd b4 30
                    ld        hl,$0020                      ;[3098] 21 20 00
                    add       hl,de                         ;[309b] 19
                    bit       0,(hl)                        ;[309c] cb 46
                    jr        z,$30ae                       ;[309e] 28 0e
                    ld        a,b                           ;[30a0] 78
                    cp        $00                           ;[30a1] fe 00
                    jr        z,$30b2                       ;[30a3] 28 0d
                    dec       b                             ;[30a5] 05
                    call      $2a1a                         ;[30a6] cd 1a 2a
                    inc       b                             ;[30a9] 04
                    cp        $00                           ;[30aa] fe 00
                    jr        z,$30b2                       ;[30ac] 28 04
                    ld        a,$01                         ;[30ae] 3e 01
                    or        a                             ;[30b0] b7
                    ret                                     ;[30b1] c9

                    xor       a                             ;[30b2] af
                    ret                                     ;[30b3] c9

                    ld        hl,$ec16                      ;[30b4] 21 16 ec
                    push      af                            ;[30b7] f5
                    ld        a,c                           ;[30b8] 79
                    ld        de,$0023                      ;[30b9] 11 23 00
                    or        a                             ;[30bc] b7
                    jr        z,$30c3                       ;[30bd] 28 04
                    add       hl,de                         ;[30bf] 19
                    dec       a                             ;[30c0] 3d
                    jr        $30bc                         ;[30c1] 18 f9
                    ex        de,hl                         ;[30c3] eb
                    pop       af                            ;[30c4] f1
                    ret                                     ;[30c5] c9

                    push      de                            ;[30c6] d5
                    call      $30b4                         ;[30c7] cd b4 30
                    ld        h,$00                         ;[30ca] 26 00
                    ld        l,b                           ;[30cc] 68
                    add       hl,de                         ;[30cd] 19
                    pop       de                            ;[30ce] d1
                    ret                                     ;[30cf] c9

                    dec       b                             ;[30d0] 05
                    nop                                     ;[30d1] 00
                    nop                                     ;[30d2] 00
                    nop                                     ;[30d3] 00
                    ret       m                             ;[30d4] f8
                    or        $21                           ;[30d5] f6 21
                    ret       nc                            ;[30d7] d0
                    jr        nc,$30eb                      ;[30d8] 30 11
                    push      af                            ;[30da] f5
                    or        $c3                           ;[30db] f6 c3
                    cp        d                             ;[30dd] ba
                    ccf                                     ;[30de] 3f
                    push      bc                            ;[30df] c5
                    push      de                            ;[30e0] d5
                    ld        hl,$f6f5                      ;[30e1] 21 f5 f6
                    push      hl                            ;[30e4] e5
                    ld        a,(hl)                        ;[30e5] 7e
                    or        a                             ;[30e6] b7
                    jr        nz,$3101                      ;[30e7] 20 18
                    push      hl                            ;[30e9] e5
                    call      $335f                         ;[30ea] cd 5f 33
                    ld        hl,($f9d7)                    ;[30ed] 2a d7 f9
                    call      $3352                         ;[30f0] cd 52 33
                    jr        nc,$30f8                      ;[30f3] 30 03
                    ld        ($f9d7),hl                    ;[30f5] 22 d7 f9
                    ld        b,h                           ;[30f8] 44
                    ld        c,l                           ;[30f9] 4d
                    pop       hl                            ;[30fa] e1
                    call      $32d6                         ;[30fb] cd d6 32
                    dec       a                             ;[30fe] 3d
                    jr        $3116                         ;[30ff] 18 15
                    ld        hl,$ec0d                      ;[3101] 21 0d ec
                    res       0,(hl)                        ;[3104] cb 86
                    ld        hl,$f6f8                      ;[3106] 21 f8 f6
                    ld        d,h                           ;[3109] 54
                    ld        e,l                           ;[310a] 5d
                    ld        bc,$0023                      ;[310b] 01 23 00
                    add       hl,bc                         ;[310e] 09
                    ld        bc,$02bc                      ;[310f] 01 bc 02
                    ldir                                    ;[3112] ed b0
                    dec       a                             ;[3114] 3d
                    scf                                     ;[3115] 37
                    pop       de                            ;[3116] d1
                    ld        (de),a                        ;[3117] 12
                    ld        hl,$f6f8                      ;[3118] 21 f8 f6
                    pop       de                            ;[311b] d1
                    pop       bc                            ;[311c] c1
                    ret                                     ;[311d] c9

                    push      bc                            ;[311e] c5
                    push      de                            ;[311f] d5
                    ld        hl,$0020                      ;[3120] 21 20 00
                    add       hl,de                         ;[3123] 19
                    ld        a,(hl)                        ;[3124] 7e
                    cpl                                     ;[3125] 2f
                    and       $11                           ;[3126] e6 11
                    jr        nz,$313f                      ;[3128] 20 15
                    push      hl                            ;[312a] e5
                    push      de                            ;[312b] d5
                    inc       hl                            ;[312c] 23
                    ld        d,(hl)                        ;[312d] 56
                    inc       hl                            ;[312e] 23
                    ld        e,(hl)                        ;[312f] 5e
                    push      de                            ;[3130] d5
                    call      $335f                         ;[3131] cd 5f 33
                    pop       hl                            ;[3134] e1
                    call      $334a                         ;[3135] cd 4a 33
                    jr        nc,$313d                      ;[3138] 30 03
                    ld        ($f9d7),hl                    ;[313a] 22 d7 f9
                    pop       de                            ;[313d] d1
                    pop       hl                            ;[313e] e1
                    bit       0,(hl)                        ;[313f] cb 46
                    ld        hl,$f6f5                      ;[3141] 21 f5 f6
                    push      hl                            ;[3144] e5
                    jr        z,$314c                       ;[3145] 28 05
                    ld        a,$00                         ;[3147] 3e 00
                    scf                                     ;[3149] 37
                    jr        $3116                         ;[314a] 18 ca
                    ld        a,(hl)                        ;[314c] 7e
                    cp        $14                           ;[314d] fe 14
                    jr        z,$3116                       ;[314f] 28 c5
                    ld        bc,$0023                      ;[3151] 01 23 00
                    ld        hl,$f6f8                      ;[3154] 21 f8 f6
                    ex        de,hl                         ;[3157] eb
                    ldir                                    ;[3158] ed b0
                    ld        hl,$f9d6                      ;[315a] 21 d6 f9
                    ld        d,h                           ;[315d] 54
                    ld        e,l                           ;[315e] 5d
                    ld        bc,$0023                      ;[315f] 01 23 00
                    or        a                             ;[3162] b7
                    sbc       hl,bc                         ;[3163] ed 42
                    ld        bc,$02bc                      ;[3165] 01 bc 02
                    lddr                                    ;[3168] ed b8
                    inc       a                             ;[316a] 3c
                    scf                                     ;[316b] 37
                    jr        $3116                         ;[316c] 18 a8
                    push      bc                            ;[316e] c5
                    push      de                            ;[316f] d5
                    push      af                            ;[3170] f5
                    ld        b,$00                         ;[3171] 06 00
                    ld        c,$01                         ;[3173] 0e 01
                    push      hl                            ;[3175] e5
                    call      $31c3                         ;[3176] cd c3 31
                    pop       hl                            ;[3179] e1
                    bit       3,(hl)                        ;[317a] cb 5e
                    res       3,(hl)                        ;[317c] cb 9e
                    jr        nz,$31a0                      ;[317e] 20 20
                    call      $2e41                         ;[3180] cd 41 2e
                    pop       af                            ;[3183] f1
                    call      $16ac                         ;[3184] cd ac 16
                    jr        z,$31ba                       ;[3187] 28 31
                    push      af                            ;[3189] f5
                    ld        b,$00                         ;[318a] 06 00
                    inc       c                             ;[318c] 0c
                    ld        a,c                           ;[318d] 79
                    cp        $15                           ;[318e] fe 15
                    jr        c,$31a0                       ;[3190] 38 0e
                    dec       hl                            ;[3192] 2b
                    ld        a,(hl)                        ;[3193] 7e
                    inc       hl                            ;[3194] 23
                    cp        $00                           ;[3195] fe 00
                    jr        z,$31a0                       ;[3197] 28 07
                    push      hl                            ;[3199] e5
                    ld        hl,$ec0d                      ;[319a] 21 0d ec
                    set       0,(hl)                        ;[319d] cb c6
                    pop       hl                            ;[319f] e1
                    bit       1,(hl)                        ;[31a0] cb 4e
                    set       1,(hl)                        ;[31a2] cb ce
                    res       3,(hl)                        ;[31a4] cb 9e
                    call      $31c3                         ;[31a6] cd c3 31
                    jr        nz,$3180                      ;[31a9] 20 d5
                    push      bc                            ;[31ab] c5
                    push      de                            ;[31ac] d5
                    call      $35e6                         ;[31ad] cd e6 35
                    ld        (hl),$08                      ;[31b0] 36 08
                    pop       de                            ;[31b2] d1
                    pop       bc                            ;[31b3] c1
                    call      $35f4                         ;[31b4] cd f4 35
                    pop       af                            ;[31b7] f1
                    jr        $3184                         ;[31b8] 18 ca
                    ld        a,c                           ;[31ba] 79
                    ld        ($f6f5),a                     ;[31bb] 32 f5 f6
                    set       3,(hl)                        ;[31be] cb de
                    pop       de                            ;[31c0] d1
                    pop       bc                            ;[31c1] c1
                    ret                                     ;[31c2] c9

                    ld        hl,$f6f8                      ;[31c3] 21 f8 f6
                    jp        $30b7                         ;[31c6] c3 b7 30
                    push      bc                            ;[31c9] c5
                    push      de                            ;[31ca] d5
                    ld        hl,$ec0d                      ;[31cb] 21 0d ec
                    res       0,(hl)                        ;[31ce] cb 86
                    ld        a,($f6f5)                     ;[31d0] 3a f5 f6
                    ld        c,a                           ;[31d3] 4f
                    or        a                             ;[31d4] b7
                    ld        a,$00                         ;[31d5] 3e 00
                    jr        z,$321b                       ;[31d7] 28 42
                    call      $31c3                         ;[31d9] cd c3 31
                    push      af                            ;[31dc] f5
                    ld        b,$00                         ;[31dd] 06 00
                    call      $2e41                         ;[31df] cd 41 2e
                    jr        nc,$31f2                      ;[31e2] 30 0e
                    pop       af                            ;[31e4] f1
                    call      $16c1                         ;[31e5] cd c1 16
                    push      af                            ;[31e8] f5
                    push      bc                            ;[31e9] c5
                    ld        b,$00                         ;[31ea] 06 00
                    call      $2e41                         ;[31ec] cd 41 2e
                    pop       bc                            ;[31ef] c1
                    jr        c,$3216                       ;[31f0] 38 24
                    inc       hl                            ;[31f2] 23
                    ld        a,(hl)                        ;[31f3] 7e
                    push      af                            ;[31f4] f5
                    push      bc                            ;[31f5] c5
                    ld        a,c                           ;[31f6] 79
                    cp        $01                           ;[31f7] fe 01
                    jr        nz,$3204                      ;[31f9] 20 09
                    ld        a,($ec15)                     ;[31fb] 3a 15 ec
                    ld        c,a                           ;[31fe] 4f
                    call      $30b4                         ;[31ff] cd b4 30
                    jr        $3208                         ;[3202] 18 04
                    dec       c                             ;[3204] 0d
                    call      $31c3                         ;[3205] cd c3 31
                    pop       bc                            ;[3208] c1
                    pop       af                            ;[3209] f1
                    ld        hl,$0020                      ;[320a] 21 20 00
                    add       hl,de                         ;[320d] 19
                    res       1,(hl)                        ;[320e] cb 8e
                    or        (hl)                          ;[3210] b6
                    ld        (hl),a                        ;[3211] 77
                    ld        hl,$f6f5                      ;[3212] 21 f5 f6
                    dec       (hl)                          ;[3215] 35
                    pop       af                            ;[3216] f1
                    dec       c                             ;[3217] 0d
                    jr        nz,$31d9                      ;[3218] 20 bf
                    scf                                     ;[321a] 37
                    pop       de                            ;[321b] d1
                    pop       bc                            ;[321c] c1
                    ret                                     ;[321d] c9

                    inc       bc                            ;[321e] 03
                    nop                                     ;[321f] 00
                    sbc       $f9                           ;[3220] de f9
                    ld        hl,$321e                      ;[3222] 21 1e 32
                    ld        de,$f9db                      ;[3225] 11 db f9
                    jp        $3fba                         ;[3228] c3 ba 3f
                    push      bc                            ;[322b] c5
                    push      de                            ;[322c] d5
                    ld        hl,$f9db                      ;[322d] 21 db f9
                    push      hl                            ;[3230] e5
                    ld        a,(hl)                        ;[3231] 7e
                    or        a                             ;[3232] b7
                    jr        nz,$3253                      ;[3233] 20 1e
                    push      hl                            ;[3235] e5
                    call      $335f                         ;[3236] cd 5f 33
                    ld        hl,($fc9a)                    ;[3239] 2a 9a fc
                    call      $334a                         ;[323c] cd 4a 33
                    jr        nc,$3244                      ;[323f] 30 03
                    ld        ($fc9a),hl                    ;[3241] 22 9a fc
                    ld        b,h                           ;[3244] 44
                    ld        c,l                           ;[3245] 4d
                    pop       hl                            ;[3246] e1
                    inc       hl                            ;[3247] 23
                    inc       hl                            ;[3248] 23
                    inc       hl                            ;[3249] 23
                    jr        nc,$325d                      ;[324a] 30 11
                    call      $32d6                         ;[324c] cd d6 32
                    dec       a                             ;[324f] 3d
                    ex        de,hl                         ;[3250] eb
                    jr        $325d                         ;[3251] 18 0a
                    ld        hl,($f9dc)                    ;[3253] 2a dc f9
                    ld        bc,$0023                      ;[3256] 01 23 00
                    sbc       hl,bc                         ;[3259] ed 42
                    scf                                     ;[325b] 37
                    dec       a                             ;[325c] 3d
                    ex        de,hl                         ;[325d] eb
                    pop       hl                            ;[325e] e1
                    jr        nc,$3262                      ;[325f] 30 01
                    ld        (hl),a                        ;[3261] 77
                    inc       hl                            ;[3262] 23
                    ld        (hl),e                        ;[3263] 73
                    inc       hl                            ;[3264] 23
                    ld        (hl),d                        ;[3265] 72
                    ex        de,hl                         ;[3266] eb
                    pop       de                            ;[3267] d1
                    pop       bc                            ;[3268] c1
                    ret                                     ;[3269] c9

                    push      bc                            ;[326a] c5
                    push      de                            ;[326b] d5
                    ld        hl,$0020                      ;[326c] 21 20 00
                    add       hl,de                         ;[326f] 19
                    ld        a,(hl)                        ;[3270] 7e
                    cpl                                     ;[3271] 2f
                    and       $11                           ;[3272] e6 11
                    jr        nz,$3282                      ;[3274] 20 0c
                    push      de                            ;[3276] d5
                    push      hl                            ;[3277] e5
                    inc       hl                            ;[3278] 23
                    ld        d,(hl)                        ;[3279] 56
                    inc       hl                            ;[327a] 23
                    ld        e,(hl)                        ;[327b] 5e
                    ld        ($fc9a),de                    ;[327c] ed 53 9a fc
                    pop       hl                            ;[3280] e1
                    pop       de                            ;[3281] d1
                    bit       3,(hl)                        ;[3282] cb 5e
                    ld        hl,$f9db                      ;[3284] 21 db f9
                    push      hl                            ;[3287] e5
                    jr        z,$32a0                       ;[3288] 28 16
                    push      hl                            ;[328a] e5
                    call      $335f                         ;[328b] cd 5f 33
                    ld        hl,($fc9a)                    ;[328e] 2a 9a fc
                    call      $3352                         ;[3291] cd 52 33
                    ld        ($fc9a),hl                    ;[3294] 22 9a fc
                    pop       hl                            ;[3297] e1
                    inc       hl                            ;[3298] 23
                    inc       hl                            ;[3299] 23
                    inc       hl                            ;[329a] 23
                    ld        a,$00                         ;[329b] 3e 00
                    scf                                     ;[329d] 37
                    jr        $325d                         ;[329e] 18 bd
                    ld        a,(hl)                        ;[32a0] 7e
                    cp        $14                           ;[32a1] fe 14
                    jr        z,$32b3                       ;[32a3] 28 0e
                    inc       a                             ;[32a5] 3c
                    ld        hl,($f9dc)                    ;[32a6] 2a dc f9
                    ld        bc,$0023                      ;[32a9] 01 23 00
                    ex        de,hl                         ;[32ac] eb
                    ldir                                    ;[32ad] ed b0
                    ex        de,hl                         ;[32af] eb
                    scf                                     ;[32b0] 37
                    jr        $325d                         ;[32b1] 18 aa
                    pop       hl                            ;[32b3] e1
                    pop       de                            ;[32b4] d1
                    pop       bc                            ;[32b5] c1
                    ret                                     ;[32b6] c9

                    ld        hl,$f9de                      ;[32b7] 21 de f9
                    jp        $30b7                         ;[32ba] c3 b7 30
                    ex        af,af'                        ;[32bd] 08
                    dec       c                             ;[32be] 0d
                    call      z,$0135                       ;[32bf] cc 35 01
                    jp        c,$1235                       ;[32c2] da 35 12
                    ld        e,d                           ;[32c5] 5a
                    inc       sp                            ;[32c6] 33
                    inc       de                            ;[32c7] 13
                    ld        e,d                           ;[32c8] 5a
                    inc       sp                            ;[32c9] 33
                    inc       d                             ;[32ca] 14
                    ld        e,d                           ;[32cb] 5a
                    inc       sp                            ;[32cc] 33
                    dec       d                             ;[32cd] 15
                    ld        e,d                           ;[32ce] 5a
                    inc       sp                            ;[32cf] 33
                    djnz      $332c                         ;[32d0] 10 5a
                    inc       sp                            ;[32d2] 33
                    ld        de,$335a                      ;[32d3] 11 5a 33
                    ld        d,h                           ;[32d6] 54
                    ld        e,l                           ;[32d7] 5d
                    inc       de                            ;[32d8] 13
                    inc       de                            ;[32d9] 13
                    inc       de                            ;[32da] 13
                    push      de                            ;[32db] d5
                    ld        hl,$0020                      ;[32dc] 21 20 00
                    add       hl,de                         ;[32df] 19
                    ld        (hl),$01                      ;[32e0] 36 01
                    inc       hl                            ;[32e2] 23
                    ld        (hl),b                        ;[32e3] 70
                    inc       hl                            ;[32e4] 23
                    ld        (hl),c                        ;[32e5] 71
                    ld        c,$01                         ;[32e6] 0e 01
                    ld        b,$00                         ;[32e8] 06 00
                    push      bc                            ;[32ea] c5
                    push      de                            ;[32eb] d5
                    ld        a,($ec0e)                     ;[32ec] 3a 0e ec
                    cp        $04                           ;[32ef] fe 04
                    call      nz,$3517                      ;[32f1] c4 17 35
                    pop       de                            ;[32f4] d1
                    pop       bc                            ;[32f5] c1
                    jr        c,$3307                       ;[32f6] 38 0f
                    ld        a,c                           ;[32f8] 79
                    cp        $01                           ;[32f9] fe 01
                    ld        a,$0d                         ;[32fb] 3e 0d
                    jr        nz,$3307                      ;[32fd] 20 08
                    ld        a,b                           ;[32ff] 78
                    or        a                             ;[3300] b7
                    ld        a,$01                         ;[3301] 3e 01
                    jr        z,$3307                       ;[3303] 28 02
                    ld        a,$0d                         ;[3305] 3e 0d
                    ld        hl,$32bd                      ;[3307] 21 bd 32
                    call      $3fce                         ;[330a] cd ce 3f
                    jr        c,$332c                       ;[330d] 38 1d
                    jr        z,$32ea                       ;[330f] 28 d9
                    push      af                            ;[3311] f5
                    ld        a,$1f                         ;[3312] 3e 1f
                    cp        b                             ;[3314] b8
                    jr        nc,$3326                      ;[3315] 30 0f
                    ld        a,$12                         ;[3317] 3e 12
                    call      $3331                         ;[3319] cd 31 33
                    jr        c,$3323                       ;[331c] 38 05
                    pop       af                            ;[331e] f1
                    ld        a,$0d                         ;[331f] 3e 0d
                    jr        $3307                         ;[3321] 18 e4
                    call      $35f4                         ;[3323] cd f4 35
                    pop       af                            ;[3326] f1
                    call      $35c5                         ;[3327] cd c5 35
                    jr        $32ea                         ;[332a] 18 be
                    pop       hl                            ;[332c] e1
                    ld        a,c                           ;[332d] 79
                    ret       z                             ;[332e] c8
                    scf                                     ;[332f] 37
                    ret                                     ;[3330] c9

                    push      af                            ;[3331] f5
                    call      $35e6                         ;[3332] cd e6 35
                    pop       af                            ;[3335] f1
                    xor       (hl)                          ;[3336] ae
                    ld        (hl),a                        ;[3337] 77
                    ld        a,c                           ;[3338] 79
                    cp        $14                           ;[3339] fe 14
                    ret       nc                            ;[333b] d0
                    inc       c                             ;[333c] 0c
                    ld        hl,$0023                      ;[333d] 21 23 00
                    add       hl,de                         ;[3340] 19
                    ex        de,hl                         ;[3341] eb
                    ld        hl,$0020                      ;[3342] 21 20 00
                    add       hl,de                         ;[3345] 19
                    ld        (hl),$00                      ;[3346] 36 00
                    scf                                     ;[3348] 37
                    ret                                     ;[3349] c9

                    call      $34b6                         ;[334a] cd b6 34
                    ret       c                             ;[334d] d8
                    ld        hl,$0000                      ;[334e] 21 00 00
                    ret                                     ;[3351] c9

                    call      $3430                         ;[3352] cd 30 34
                    ret       c                             ;[3355] d8
                    ld        hl,$0000                      ;[3356] 21 00 00
                    ret                                     ;[3359] c9

                    call      $3517                         ;[335a] cd 17 35
                    ccf                                     ;[335d] 3f
                    ret       nc                            ;[335e] d0
                    ld        hl,$0000                      ;[335f] 21 00 00
                    ld        ($fc9f),hl                    ;[3362] 22 9f fc
                    ld        ($fca1),hl                    ;[3365] 22 a1 fc
                    ld        hl,$3374                      ;[3368] 21 74 33
                    ld        de,$fcae                      ;[336b] 11 ae fc
                    ld        bc,$00bc                      ;[336e] 01 bc 00
                    ldir                                    ;[3371] ed b0
                    ret                                     ;[3373] c9

                    di                                      ;[3374] f3
                    ld        bc,$7ffd                      ;[3375] 01 fd 7f
                    ld        d,$17                         ;[3378] 16 17
                    out       (c),d                         ;[337a] ed 51
                    cp        $50                           ;[337c] fe 50
                    jr        nc,$33b1                      ;[337e] 30 31
                    cp        $40                           ;[3380] fe 40
                    jr        nc,$33aa                      ;[3382] 30 26
                    cp        $30                           ;[3384] fe 30
                    jr        nc,$33a3                      ;[3386] 30 1b
                    cp        $20                           ;[3388] fe 20
                    jr        nc,$339c                      ;[338a] 30 10
                    cp        $10                           ;[338c] fe 10
                    jr        nc,$3395                      ;[338e] 30 05
                    ld        hl,$0096                      ;[3390] 21 96 00
                    jr        $33b6                         ;[3393] 18 21
                    sub       $10                           ;[3395] d6 10
                    ld        hl,$00cf                      ;[3397] 21 cf 00
                    jr        $33b6                         ;[339a] 18 1a
                    sub       $20                           ;[339c] d6 20
                    ld        hl,$0100                      ;[339e] 21 00 01
                    jr        $33b6                         ;[33a1] 18 13
                    sub       $30                           ;[33a3] d6 30
                    ld        hl,$013e                      ;[33a5] 21 3e 01
                    jr        $33b6                         ;[33a8] 18 0c
                    sub       $40                           ;[33aa] d6 40
                    ld        hl,$018b                      ;[33ac] 21 8b 01
                    jr        $33b6                         ;[33af] 18 05
                    sub       $50                           ;[33b1] d6 50
                    ld        hl,$01d4                      ;[33b3] 21 d4 01
                    ld        b,a                           ;[33b6] 47
                    or        a                             ;[33b7] b7
                    jr        z,$33c3                       ;[33b8] 28 09
                    ld        a,(hl)                        ;[33ba] 7e
                    inc       hl                            ;[33bb] 23
                    and       $80                           ;[33bc] e6 80
                    jr        z,$33ba                       ;[33be] 28 fa
                    dec       b                             ;[33c0] 05
                    jr        $33b8                         ;[33c1] 18 f5
                    ld        de,$fca3                      ;[33c3] 11 a3 fc
                    ld        ($fca1),de                    ;[33c6] ed 53 a1 fc
                    ld        a,($fc9e)                     ;[33ca] 3a 9e fc
                    or        a                             ;[33cd] b7
                    ld        a,$00                         ;[33ce] 3e 00
                    ld        ($fc9e),a                     ;[33d0] 32 9e fc
                    jr        nz,$33d9                      ;[33d3] 20 04
                    ld        a,$20                         ;[33d5] 3e 20
                    ld        (de),a                        ;[33d7] 12
                    inc       de                            ;[33d8] 13
                    ld        a,(hl)                        ;[33d9] 7e
                    ld        b,a                           ;[33da] 47
                    inc       hl                            ;[33db] 23
                    ld        (de),a                        ;[33dc] 12
                    inc       de                            ;[33dd] 13
                    and       $80                           ;[33de] e6 80
                    jr        z,$33d9                       ;[33e0] 28 f7
                    ld        a,b                           ;[33e2] 78
                    and       $7f                           ;[33e3] e6 7f
                    dec       de                            ;[33e5] 1b
                    ld        (de),a                        ;[33e6] 12
                    inc       de                            ;[33e7] 13
                    ld        a,$a0                         ;[33e8] 3e a0
                    ld        (de),a                        ;[33ea] 12
                    ld        a,$07                         ;[33eb] 3e 07
                    ld        bc,$7ffd                      ;[33ed] 01 fd 7f
                    out       (c),a                         ;[33f0] ed 79
                    ei                                      ;[33f2] fb
                    ret                                     ;[33f3] c9

                    di                                      ;[33f4] f3
                    ld        bc,$7ffd                      ;[33f5] 01 fd 7f
                    ld        d,$17                         ;[33f8] 16 17
                    out       (c),d                         ;[33fa] ed 51
                    ld        hl,$0096                      ;[33fc] 21 96 00
                    ld        b,$a5                         ;[33ff] 06 a5
                    ld        de,$fd74                      ;[3401] 11 74 fd
                    ld        a,(de)                        ;[3404] 1a
                    and       $7f                           ;[3405] e6 7f
                    cp        $61                           ;[3407] fe 61
                    ld        a,(de)                        ;[3409] 1a
                    jr        c,$340e                       ;[340a] 38 02
                    and       $df                           ;[340c] e6 df
                    cp        (hl)                          ;[340e] be
                    jr        nz,$341a                      ;[340f] 20 09
                    inc       hl                            ;[3411] 23
                    inc       de                            ;[3412] 13
                    and       $80                           ;[3413] e6 80
                    jr        z,$3404                       ;[3415] 28 ed
                    scf                                     ;[3417] 37
                    jr        $3426                         ;[3418] 18 0c
                    inc       b                             ;[341a] 04
                    jr        z,$3425                       ;[341b] 28 08
                    ld        a,(hl)                        ;[341d] 7e
                    and       $80                           ;[341e] e6 80
                    inc       hl                            ;[3420] 23
                    jr        z,$341d                       ;[3421] 28 fa
                    jr        $3401                         ;[3423] 18 dc
                    or        a                             ;[3425] b7
                    ld        a,b                           ;[3426] 78
                    ld        d,$07                         ;[3427] 16 07
                    ld        bc,$7ffd                      ;[3429] 01 fd 7f
                    out       (c),d                         ;[342c] ed 51
                    ei                                      ;[342e] fb
                    ret                                     ;[342f] c9

                    call      $34ea                         ;[3430] cd ea 34
                    or        a                             ;[3433] b7
                    ld        ($fc9e),a                     ;[3434] 32 9e fc
                    call      $1f20                         ;[3437] cd 20 1f
                    call      $34f6                         ;[343a] cd f6 34
                    jr        nc,$3491                      ;[343d] 30 52
                    jr        nz,$344d                      ;[343f] 20 0c
                    ld        a,b                           ;[3441] 78
                    or        c                             ;[3442] b1
                    jr        z,$344d                       ;[3443] 28 08
                    call      $34cf                         ;[3445] cd cf 34
                    call      $34d9                         ;[3448] cd d9 34
                    jr        nc,$3491                      ;[344b] 30 44
                    ld        d,(hl)                        ;[344d] 56
                    inc       hl                            ;[344e] 23
                    ld        e,(hl)                        ;[344f] 5e
                    call      $1f45                         ;[3450] cd 45 1f
                    push      de                            ;[3453] d5
                    push      hl                            ;[3454] e5
                    push      ix                            ;[3455] dd e5
                    ld        ix,$fca3                      ;[3457] dd 21 a3 fc
                    ld        ($fca1),ix                    ;[345b] dd 22 a1 fc
                    ex        de,hl                         ;[345f] eb
                    ld        b,$00                         ;[3460] 06 00
                    ld        de,$fc18                      ;[3462] 11 18 fc
                    call      $3495                         ;[3465] cd 95 34
                    ld        de,$ff9c                      ;[3468] 11 9c ff
                    call      $3495                         ;[346b] cd 95 34
                    ld        de,$fff6                      ;[346e] 11 f6 ff
                    call      $3495                         ;[3471] cd 95 34
                    ld        de,$ffff                      ;[3474] 11 ff ff
                    call      $3495                         ;[3477] cd 95 34
                    dec       ix                            ;[347a] dd 2b
                    ld        a,(ix+$00)                    ;[347c] dd 7e 00
                    or        $80                           ;[347f] f6 80
                    ld        (ix+$00),a                    ;[3481] dd 77 00
                    pop       ix                            ;[3484] dd e1
                    pop       hl                            ;[3486] e1
                    pop       de                            ;[3487] d1
                    inc       hl                            ;[3488] 23
                    inc       hl                            ;[3489] 23
                    inc       hl                            ;[348a] 23
                    ld        ($fc9f),hl                    ;[348b] 22 9f fc
                    ex        de,hl                         ;[348e] eb
                    scf                                     ;[348f] 37
                    ret                                     ;[3490] c9

                    call      $1f45                         ;[3491] cd 45 1f
                    ret                                     ;[3494] c9

                    xor       a                             ;[3495] af
                    add       hl,de                         ;[3496] 19
                    inc       a                             ;[3497] 3c
                    jr        c,$3496                       ;[3498] 38 fc
                    sbc       hl,de                         ;[349a] ed 52
                    dec       a                             ;[349c] 3d
                    add       $30                           ;[349d] c6 30
                    ld        (ix+$00),a                    ;[349f] dd 77 00
                    cp        $30                           ;[34a2] fe 30
                    jr        nz,$34b1                      ;[34a4] 20 0b
                    ld        a,b                           ;[34a6] 78
                    or        a                             ;[34a7] b7
                    jr        nz,$34b3                      ;[34a8] 20 09
                    ld        a,$00                         ;[34aa] 3e 00
                    ld        (ix+$00),a                    ;[34ac] dd 77 00
                    jr        $34b3                         ;[34af] 18 02
                    ld        b,$01                         ;[34b1] 06 01
                    inc       ix                            ;[34b3] dd 23
                    ret                                     ;[34b5] c9

                    call      $34ea                         ;[34b6] cd ea 34
                    or        a                             ;[34b9] b7
                    ld        ($fc9e),a                     ;[34ba] 32 9e fc
                    call      $1f20                         ;[34bd] cd 20 1f
                    call      $34f6                         ;[34c0] cd f6 34
                    jr        nc,$3491                      ;[34c3] 30 cc
                    ex        de,hl                         ;[34c5] eb
                    ld        a,l                           ;[34c6] 7d
                    or        h                             ;[34c7] b4
                    scf                                     ;[34c8] 37
                    jp        nz,$344d                      ;[34c9] c2 4d 34
                    ccf                                     ;[34cc] 3f
                    jr        $3491                         ;[34cd] 18 c2
                    push      hl                            ;[34cf] e5
                    inc       hl                            ;[34d0] 23
                    inc       hl                            ;[34d1] 23
                    ld        e,(hl)                        ;[34d2] 5e
                    inc       hl                            ;[34d3] 23
                    ld        d,(hl)                        ;[34d4] 56
                    inc       hl                            ;[34d5] 23
                    add       hl,de                         ;[34d6] 19
                    pop       de                            ;[34d7] d1
                    ret                                     ;[34d8] c9

                    ld        a,(hl)                        ;[34d9] 7e
                    and       $c0                           ;[34da] e6 c0
                    scf                                     ;[34dc] 37
                    ret       z                             ;[34dd] c8
                    ccf                                     ;[34de] 3f
                    ret                                     ;[34df] c9

                    ld        a,b                           ;[34e0] 78
                    cp        (hl)                          ;[34e1] be
                    ret       nz                            ;[34e2] c0
                    ld        a,c                           ;[34e3] 79
                    inc       hl                            ;[34e4] 23
                    cp        (hl)                          ;[34e5] be
                    dec       hl                            ;[34e6] 2b
                    ret       nz                            ;[34e7] c0
                    scf                                     ;[34e8] 37
                    ret                                     ;[34e9] c9

                    push      hl                            ;[34ea] e5
                    ld        hl,$0000                      ;[34eb] 21 00 00
                    ld        ($fca1),hl                    ;[34ee] 22 a1 fc
                    ld        ($fc9f),hl                    ;[34f1] 22 9f fc
                    pop       hl                            ;[34f4] e1
                    ret                                     ;[34f5] c9

                    push      hl                            ;[34f6] e5
                    pop       bc                            ;[34f7] c1
                    ld        de,$0000                      ;[34f8] 11 00 00
                    ld        hl,($5c53)                    ;[34fb] 2a 53 5c
                    call      $34d9                         ;[34fe] cd d9 34
                    ret       nc                            ;[3501] d0
                    call      $34e0                         ;[3502] cd e0 34
                    ret       c                             ;[3505] d8
                    ld        a,b                           ;[3506] 78
                    or        c                             ;[3507] b1
                    scf                                     ;[3508] 37
                    ret       z                             ;[3509] c8
                    call      $34cf                         ;[350a] cd cf 34
                    call      $34d9                         ;[350d] cd d9 34
                    ret       nc                            ;[3510] d0
                    call      $34e0                         ;[3511] cd e0 34
                    jr        nc,$350a                      ;[3514] 30 f4
                    ret                                     ;[3516] c9

                    ld        hl,($fca1)                    ;[3517] 2a a1 fc
                    ld        a,l                           ;[351a] 7d
                    or        h                             ;[351b] b4
                    jr        z,$353c                       ;[351c] 28 1e
                    ld        a,(hl)                        ;[351e] 7e
                    inc       hl                            ;[351f] 23
                    cp        $a0                           ;[3520] fe a0
                    ld        b,a                           ;[3522] 47
                    ld        a,$00                         ;[3523] 3e 00
                    jr        nz,$3529                      ;[3525] 20 02
                    ld        a,$ff                         ;[3527] 3e ff
                    ld        ($fc9e),a                     ;[3529] 32 9e fc
                    ld        a,b                           ;[352c] 78
                    bit       7,a                           ;[352d] cb 7f
                    jr        z,$3534                       ;[352f] 28 03
                    ld        hl,$0000                      ;[3531] 21 00 00
                    ld        ($fca1),hl                    ;[3534] 22 a1 fc
                    and       $7f                           ;[3537] e6 7f
                    jp        $358f                         ;[3539] c3 8f 35
                    ld        hl,($fc9f)                    ;[353c] 2a 9f fc
                    ld        a,l                           ;[353f] 7d
                    or        h                             ;[3540] b4
                    jp        z,$3591                       ;[3541] ca 91 35
                    call      $1f20                         ;[3544] cd 20 1f
                    ld        a,(hl)                        ;[3547] 7e
                    cp        $0e                           ;[3548] fe 0e
                    jr        nz,$3554                      ;[354a] 20 08
                    inc       hl                            ;[354c] 23
                    inc       hl                            ;[354d] 23
                    inc       hl                            ;[354e] 23
                    inc       hl                            ;[354f] 23
                    inc       hl                            ;[3550] 23
                    inc       hl                            ;[3551] 23
                    jr        $3547                         ;[3552] 18 f3
                    call      $1f45                         ;[3554] cd 45 1f
                    inc       hl                            ;[3557] 23
                    ld        ($fc9f),hl                    ;[3558] 22 9f fc
                    cp        $a5                           ;[355b] fe a5
                    jr        c,$3567                       ;[355d] 38 08
                    sub       $a5                           ;[355f] d6 a5
                    call      $fcae                         ;[3561] cd ae fc
                    jp        $3517                         ;[3564] c3 17 35
                    cp        $a3                           ;[3567] fe a3
                    jr        c,$357b                       ;[3569] 38 10
                    jr        nz,$3572                      ;[356b] 20 05
                    ld        hl,$3594                      ;[356d] 21 94 35
                    jr        $3575                         ;[3570] 18 03
                    ld        hl,$359c                      ;[3572] 21 9c 35
                    call      $fcfd                         ;[3575] cd fd fc
                    jp        $3517                         ;[3578] c3 17 35
                    push      af                            ;[357b] f5
                    ld        a,$00                         ;[357c] 3e 00
                    ld        ($fc9e),a                     ;[357e] 32 9e fc
                    pop       af                            ;[3581] f1
                    cp        $0d                           ;[3582] fe 0d
                    jr        nz,$358f                      ;[3584] 20 09
                    ld        hl,$0000                      ;[3586] 21 00 00
                    ld        ($fca1),hl                    ;[3589] 22 a1 fc
                    ld        ($fc9f),hl                    ;[358c] 22 9f fc
                    scf                                     ;[358f] 37
                    ret                                     ;[3590] c9

                    scf                                     ;[3591] 37
                    ccf                                     ;[3592] 3f
                    ret                                     ;[3593] c9

                    ld        d,e                           ;[3594] 53
                    ld        d,b                           ;[3595] 50
                    ld        b,l                           ;[3596] 45
                    ld        b,e                           ;[3597] 43
                    ld        d,h                           ;[3598] 54
                    ld        d,d                           ;[3599] 52
                    ld        d,l                           ;[359a] 55
                    call      $4c50                         ;[359b] cd 50 4c
                    ld        b,c                           ;[359e] 41
                    exx                                     ;[359f] d9
                    ld        b,a                           ;[35a0] 47
                    ld        c,a                           ;[35a1] 4f
                    ld        d,h                           ;[35a2] 54
                    rst       $08                           ;[35a3] cf
                    ld        b,a                           ;[35a4] 47
                    ld        c,a                           ;[35a5] 4f
                    ld        d,e                           ;[35a6] 53
                    ld        d,l                           ;[35a7] 55
                    jp        nz,$4544                      ;[35a8] c2 44 45
                    ld        b,(hl)                        ;[35ab] 46
                    ld        b,(hl)                        ;[35ac] 46
                    adc       $4f                           ;[35ad] ce 4f
                    ld        d,b                           ;[35af] 50
                    ld        b,l                           ;[35b0] 45
                    ld        c,(hl)                        ;[35b1] 4e
                    and       e                             ;[35b2] a3
                    ld        b,e                           ;[35b3] 43
                    ld        c,h                           ;[35b4] 4c
                    ld        c,a                           ;[35b5] 4f
                    ld        d,e                           ;[35b6] 53
                    ld        b,l                           ;[35b7] 45
                    and       e                             ;[35b8] a3
                    ld        (bc),a                        ;[35b9] 02
                    ld        bc,$2105                      ;[35ba] 01 05 21
                    cp        c                             ;[35bd] b9
                    dec       (hl)                          ;[35be] 35
                    ld        de,$fd6a                      ;[35bf] 11 6a fd
                    jp        $3fba                         ;[35c2] c3 ba 3f
                    ld        l,b                           ;[35c5] 68
                    ld        h,$00                         ;[35c6] 26 00
                    add       hl,de                         ;[35c8] 19
                    ld        (hl),a                        ;[35c9] 77
                    inc       b                             ;[35ca] 04
                    ret                                     ;[35cb] c9

                    call      $35e6                         ;[35cc] cd e6 35
                    ld        a,(hl)                        ;[35cf] 7e
                    or        $18                           ;[35d0] f6 18
                    ld        (hl),a                        ;[35d2] 77
                    ld        hl,$fd6a                      ;[35d3] 21 6a fd
                    set       0,(hl)                        ;[35d6] cb c6
                    scf                                     ;[35d8] 37
                    ret                                     ;[35d9] c9

                    call      $35e6                         ;[35da] cd e6 35
                    set       3,(hl)                        ;[35dd] cb de
                    ld        hl,$fd6a                      ;[35df] 21 6a fd
                    set       0,(hl)                        ;[35e2] cb c6
                    scf                                     ;[35e4] 37
                    ret                                     ;[35e5] c9

                    ld        l,b                           ;[35e6] 68
                    ld        h,$00                         ;[35e7] 26 00
                    add       hl,de                         ;[35e9] 19
                    ld        a,$20                         ;[35ea] 3e 20
                    cp        b                             ;[35ec] b8
                    ret       z                             ;[35ed] c8
                    ld        (hl),$00                      ;[35ee] 36 00
                    inc       hl                            ;[35f0] 23
                    inc       b                             ;[35f1] 04
                    jr        $35ec                         ;[35f2] 18 f8
                    ld        a,($fd6b)                     ;[35f4] 3a 6b fd
                    ld        b,$00                         ;[35f7] 06 00
                    ld        h,$00                         ;[35f9] 26 00
                    ld        l,b                           ;[35fb] 68
                    add       hl,de                         ;[35fc] 19
                    ld        (hl),$00                      ;[35fd] 36 00
                    inc       b                             ;[35ff] 04
                    dec       a                             ;[3600] 3d
                    jr        nz,$35f9                      ;[3601] 20 f6
                    ret                                     ;[3603] c9

                    push      bc                            ;[3604] c5
                    push      de                            ;[3605] d5
                    push      hl                            ;[3606] e5
                    push      hl                            ;[3607] e5
                    ld        hl,$eef5                      ;[3608] 21 f5 ee
                    bit       2,(hl)                        ;[360b] cb 56
                    pop       hl                            ;[360d] e1
                    jr        nz,$3614                      ;[360e] 20 04
                    ld        b,c                           ;[3610] 41
                    call      $3b1e                         ;[3611] cd 1e 3b
                    pop       hl                            ;[3614] e1
                    pop       de                            ;[3615] d1
                    pop       bc                            ;[3616] c1
                    ret                                     ;[3617] c9

                    push      bc                            ;[3618] c5
                    push      de                            ;[3619] d5
                    push      hl                            ;[361a] e5
                    push      hl                            ;[361b] e5
                    ld        hl,$eef5                      ;[361c] 21 f5 ee
                    bit       2,(hl)                        ;[361f] cb 56
                    pop       hl                            ;[3621] e1
                    jr        nz,$3628                      ;[3622] 20 04
                    ld        e,c                           ;[3624] 59
                    call      $3abf                         ;[3625] cd bf 3a
                    pop       hl                            ;[3628] e1
                    pop       de                            ;[3629] d1
                    pop       bc                            ;[362a] c1
                    ret                                     ;[362b] c9

                    push      bc                            ;[362c] c5
                    push      de                            ;[362d] d5
                    push      hl                            ;[362e] e5
                    push      hl                            ;[362f] e5
                    ld        hl,$eef5                      ;[3630] 21 f5 ee
                    bit       2,(hl)                        ;[3633] cb 56
                    pop       hl                            ;[3635] e1
                    jr        nz,$363c                      ;[3636] 20 04
                    ld        e,c                           ;[3638] 59
                    call      $3ac6                         ;[3639] cd c6 3a
                    pop       hl                            ;[363c] e1
                    pop       de                            ;[363d] d1
                    pop       bc                            ;[363e] c1
                    ret                                     ;[363f] c9

                    push      af                            ;[3640] f5
                    push      bc                            ;[3641] c5
                    push      de                            ;[3642] d5
                    push      hl                            ;[3643] e5
                    ld        a,b                           ;[3644] 78
                    ld        b,c                           ;[3645] 41
                    ld        c,a                           ;[3646] 4f
                    call      $3a9d                         ;[3647] cd 9d 3a
                    pop       hl                            ;[364a] e1
                    pop       de                            ;[364b] d1
                    pop       bc                            ;[364c] c1
                    pop       af                            ;[364d] f1
                    ret                                     ;[364e] c9

                    push      af                            ;[364f] f5
                    push      bc                            ;[3650] c5
                    push      de                            ;[3651] d5
                    push      hl                            ;[3652] e5
                    ld        a,b                           ;[3653] 78
                    ld        b,c                           ;[3654] 41
                    ld        c,a                           ;[3655] 4f
                    call      $3ab2                         ;[3656] cd b2 3a
                    pop       hl                            ;[3659] e1
                    pop       de                            ;[365a] d1
                    pop       bc                            ;[365b] c1
                    pop       af                            ;[365c] f1
                    ret                                     ;[365d] c9

                    ld        a,$00                         ;[365e] 3e 00
                    ld        ($5c41),a                     ;[3660] 32 41 5c
                    ld        a,$02                         ;[3663] 3e 02
                    ld        ($5c0a),a                     ;[3665] 32 0a 5c
                    ld        hl,$5c3b                      ;[3668] 21 3b 5c
                    ld        a,(hl)                        ;[366b] 7e
                    or        $0c                           ;[366c] f6 0c
                    ld        (hl),a                        ;[366e] 77
                    ld        hl,$ec0d                      ;[366f] 21 0d ec
                    bit       4,(hl)                        ;[3672] cb 66
                    ld        hl,$5b66                      ;[3674] 21 66 5b
                    jr        nz,$367c                      ;[3677] 20 03
                    res       0,(hl)                        ;[3679] cb 86
                    ret                                     ;[367b] c9

                    set       0,(hl)                        ;[367c] cb c6
                    ret                                     ;[367e] c9

                    push      hl                            ;[367f] e5
                    ld        hl,$5c3b                      ;[3680] 21 3b 5c
                    bit       5,(hl)                        ;[3683] cb 6e
                    jr        z,$3683                       ;[3685] 28 fc
                    res       5,(hl)                        ;[3687] cb ae
                    ld        a,($5c08)                     ;[3689] 3a 08 5c
                    ld        hl,$5c41                      ;[368c] 21 41 5c
                    res       0,(hl)                        ;[368f] cb 86
                    cp        $20                           ;[3691] fe 20
                    jr        nc,$36a2                      ;[3693] 30 0d
                    cp        $10                           ;[3695] fe 10
                    jr        nc,$3680                      ;[3697] 30 e7
                    cp        $06                           ;[3699] fe 06
                    jr        c,$3680                       ;[369b] 38 e3
                    call      $36a4                         ;[369d] cd a4 36
                    jr        nc,$3680                      ;[36a0] 30 de
                    pop       hl                            ;[36a2] e1
                    ret                                     ;[36a3] c9

                    rst       $28                           ;[36a4] ef
                    in        a,($10)                       ;[36a5] db 10
                    ret                                     ;[36a7] c9

                    push      hl                            ;[36a8] e5
                    call      $373b                         ;[36a9] cd 3b 37
                    ld        hl,$5c3c                      ;[36ac] 21 3c 5c
                    res       0,(hl)                        ;[36af] cb 86
                    pop       hl                            ;[36b1] e1
                    ld        e,(hl)                        ;[36b2] 5e
                    inc       hl                            ;[36b3] 23
                    push      hl                            ;[36b4] e5
                    ld        hl,$37ec                      ;[36b5] 21 ec 37
                    call      $3733                         ;[36b8] cd 33 37
                    pop       hl                            ;[36bb] e1
                    call      $3733                         ;[36bc] cd 33 37
                    push      hl                            ;[36bf] e5
                    call      $3822                         ;[36c0] cd 22 38
                    ld        hl,$37fa                      ;[36c3] 21 fa 37
                    call      $3733                         ;[36c6] cd 33 37
                    pop       hl                            ;[36c9] e1
                    push      de                            ;[36ca] d5
                    ld        bc,$0807                      ;[36cb] 01 07 08
                    call      $372b                         ;[36ce] cd 2b 37
                    push      bc                            ;[36d1] c5
                    ld        b,$0c                         ;[36d2] 06 0c
                    ld        a,$20                         ;[36d4] 3e 20
                    rst       $10                           ;[36d6] d7
                    ld        a,(hl)                        ;[36d7] 7e
                    inc       hl                            ;[36d8] 23
                    cp        $80                           ;[36d9] fe 80
                    jr        nc,$36e0                      ;[36db] 30 03
                    rst       $10                           ;[36dd] d7
                    djnz      $36d7                         ;[36de] 10 f7
                    and       $7f                           ;[36e0] e6 7f
                    rst       $10                           ;[36e2] d7
                    ld        a,$20                         ;[36e3] 3e 20
                    rst       $10                           ;[36e5] d7
                    djnz      $36e3                         ;[36e6] 10 fb
                    pop       bc                            ;[36e8] c1
                    inc       b                             ;[36e9] 04
                    call      $372b                         ;[36ea] cd 2b 37
                    dec       e                             ;[36ed] 1d
                    jr        nz,$36d1                      ;[36ee] 20 e1
                    ld        hl,$6f38                      ;[36f0] 21 38 6f
                    pop       de                            ;[36f3] d1
                    sla       e                             ;[36f4] cb 23
                    sla       e                             ;[36f6] cb 23
                    sla       e                             ;[36f8] cb 23
                    ld        d,e                           ;[36fa] 53
                    dec       d                             ;[36fb] 15
                    ld        e,$6f                         ;[36fc] 1e 6f
                    ld        bc,$ff00                      ;[36fe] 01 00 ff
                    ld        a,d                           ;[3701] 7a
                    call      $3719                         ;[3702] cd 19 37
                    ld        bc,$0001                      ;[3705] 01 01 00
                    ld        a,e                           ;[3708] 7b
                    call      $3719                         ;[3709] cd 19 37
                    ld        bc,$0100                      ;[370c] 01 00 01
                    ld        a,d                           ;[370f] 7a
                    inc       a                             ;[3710] 3c
                    call      $3719                         ;[3711] cd 19 37
                    xor       a                             ;[3714] af
                    call      $37ca                         ;[3715] cd ca 37
                    ret                                     ;[3718] c9

                    push      af                            ;[3719] f5
                    push      hl                            ;[371a] e5
                    push      de                            ;[371b] d5
                    push      bc                            ;[371c] c5
                    ld        b,h                           ;[371d] 44
                    ld        c,l                           ;[371e] 4d
                    rst       $28                           ;[371f] ef
                    jp        (hl)                          ;[3720] e9
                    ld        ($d1c1),hl                    ;[3721] 22 c1 d1
                    pop       hl                            ;[3724] e1
                    pop       af                            ;[3725] f1
                    add       hl,bc                         ;[3726] 09
                    dec       a                             ;[3727] 3d
                    jr        nz,$3719                      ;[3728] 20 ef
                    ret                                     ;[372a] c9

                    ld        a,$16                         ;[372b] 3e 16
                    rst       $10                           ;[372d] d7
                    ld        a,b                           ;[372e] 78
                    rst       $10                           ;[372f] d7
                    ld        a,c                           ;[3730] 79
                    rst       $10                           ;[3731] d7
                    ret                                     ;[3732] c9

                    ld        a,(hl)                        ;[3733] 7e
                    inc       hl                            ;[3734] 23
                    cp        $ff                           ;[3735] fe ff
                    ret       z                             ;[3737] c8
                    rst       $10                           ;[3738] d7
                    jr        $3733                         ;[3739] 18 f8
                    scf                                     ;[373b] 37
                    jr        $373f                         ;[373c] 18 01
                    and       a                             ;[373e] a7
                    ld        de,$eef6                      ;[373f] 11 f6 ee
                    ld        hl,$5c3c                      ;[3742] 21 3c 5c
                    jr        c,$3748                       ;[3745] 38 01
                    ex        de,hl                         ;[3747] eb
                    ldi                                     ;[3748] ed a0
                    jr        c,$374d                       ;[374a] 38 01
                    ex        de,hl                         ;[374c] eb
                    ld        hl,$5c7d                      ;[374d] 21 7d 5c
                    jr        c,$3753                       ;[3750] 38 01
                    ex        de,hl                         ;[3752] eb
                    ld        bc,$0014                      ;[3753] 01 14 00
                    ldir                                    ;[3756] ed b0
                    jr        c,$375b                       ;[3758] 38 01
                    ex        de,hl                         ;[375a] eb
                    ex        af,af'                        ;[375b] 08
                    ld        bc,$0707                      ;[375c] 01 07 07
                    call      $3b94                         ;[375f] cd 94 3b
                    ld        a,(ix+$01)                    ;[3762] dd 7e 01
                    add       b                             ;[3765] 80
                    ld        b,a                           ;[3766] 47
                    ld        a,$0c                         ;[3767] 3e 0c
                    push      bc                            ;[3769] c5
                    push      af                            ;[376a] f5
                    push      de                            ;[376b] d5
                    rst       $28                           ;[376c] ef
                    sbc       e                             ;[376d] 9b
                    ld        c,$01                         ;[376e] 0e 01
                    rlca                                    ;[3770] 07
                    nop                                     ;[3771] 00
                    add       hl,bc                         ;[3772] 09
                    pop       de                            ;[3773] d1
                    call      $377e                         ;[3774] cd 7e 37
                    pop       af                            ;[3777] f1
                    pop       bc                            ;[3778] c1
                    dec       b                             ;[3779] 05
                    dec       a                             ;[377a] 3d
                    jr        nz,$3769                      ;[377b] 20 ec
                    ret                                     ;[377d] c9

                    ld        bc,$080e                      ;[377e] 01 0e 08
                    push      bc                            ;[3781] c5
                    ld        b,$00                         ;[3782] 06 00
                    push      hl                            ;[3784] e5
                    ex        af,af'                        ;[3785] 08
                    jr        c,$3789                       ;[3786] 38 01
                    ex        de,hl                         ;[3788] eb
                    ldir                                    ;[3789] ed b0
                    jr        c,$378e                       ;[378b] 38 01
                    ex        de,hl                         ;[378d] eb
                    ex        af,af'                        ;[378e] 08
                    pop       hl                            ;[378f] e1
                    inc       h                             ;[3790] 24
                    pop       bc                            ;[3791] c1
                    djnz      $3781                         ;[3792] 10 ed
                    push      bc                            ;[3794] c5
                    push      de                            ;[3795] d5
                    rst       $28                           ;[3796] ef
                    adc       b                             ;[3797] 88
                    ld        c,$eb                         ;[3798] 0e eb
                    pop       de                            ;[379a] d1
                    pop       bc                            ;[379b] c1
                    ex        af,af'                        ;[379c] 08
                    jr        c,$37a0                       ;[379d] 38 01
                    ex        de,hl                         ;[379f] eb
                    ldir                                    ;[37a0] ed b0
                    jr        c,$37a5                       ;[37a2] 38 01
                    ex        de,hl                         ;[37a4] eb
                    ex        af,af'                        ;[37a5] 08
                    ret                                     ;[37a6] c9

                    call      $37ca                         ;[37a7] cd ca 37
                    dec       a                             ;[37aa] 3d
                    jp        p,$37b1                       ;[37ab] f2 b1 37
                    ld        a,(hl)                        ;[37ae] 7e
                    dec       a                             ;[37af] 3d
                    dec       a                             ;[37b0] 3d
                    call      $37ca                         ;[37b1] cd ca 37
                    scf                                     ;[37b4] 37
                    ret                                     ;[37b5] c9

                    push      de                            ;[37b6] d5
                    call      $37ca                         ;[37b7] cd ca 37
                    inc       a                             ;[37ba] 3c
                    ld        d,a                           ;[37bb] 57
                    ld        a,(hl)                        ;[37bc] 7e
                    dec       a                             ;[37bd] 3d
                    dec       a                             ;[37be] 3d
                    cp        d                             ;[37bf] ba
                    ld        a,d                           ;[37c0] 7a
                    jp        p,$37c5                       ;[37c1] f2 c5 37
                    xor       a                             ;[37c4] af
                    call      $37ca                         ;[37c5] cd ca 37
                    pop       de                            ;[37c8] d1
                    ret                                     ;[37c9] c9

                    push      af                            ;[37ca] f5
                    push      hl                            ;[37cb] e5
                    push      de                            ;[37cc] d5
                    ld        hl,$5907                      ;[37cd] 21 07 59
                    ld        de,$0020                      ;[37d0] 11 20 00
                    and       a                             ;[37d3] a7
                    jr        z,$37da                       ;[37d4] 28 04
                    add       hl,de                         ;[37d6] 19
                    dec       a                             ;[37d7] 3d
                    jr        nz,$37d6                      ;[37d8] 20 fc
                    ld        a,$78                         ;[37da] 3e 78
                    cp        (hl)                          ;[37dc] be
                    jr        nz,$37e1                      ;[37dd] 20 02
                    ld        a,$68                         ;[37df] 3e 68
                    ld        d,$0e                         ;[37e1] 16 0e
                    ld        (hl),a                        ;[37e3] 77
                    inc       hl                            ;[37e4] 23
                    dec       d                             ;[37e5] 15
                    jr        nz,$37e3                      ;[37e6] 20 fb
                    pop       de                            ;[37e8] d1
                    pop       hl                            ;[37e9] e1
                    pop       af                            ;[37ea] f1
                    ret                                     ;[37eb] c9

                    ld        d,$07                         ;[37ec] 16 07
                    rlca                                    ;[37ee] 07
                    dec       d                             ;[37ef] 15
                    nop                                     ;[37f0] 00
                    inc       d                             ;[37f1] 14
                    nop                                     ;[37f2] 00
                    djnz      $37fc                         ;[37f3] 10 07
                    ld        de,$1300                      ;[37f5] 11 00 13
                    ld        bc,$11ff                      ;[37f8] 01 ff 11
                    nop                                     ;[37fb] 00
                    jr        nz,$380f                      ;[37fc] 20 11
                    rlca                                    ;[37fe] 07
                    djnz      $3801                         ;[37ff] 10 00
                    rst       $38                           ;[3801] ff
                    ld        bc,$0703                      ;[3802] 01 03 07
                    rrca                                    ;[3805] 0f
                    rra                                     ;[3806] 1f
                    ccf                                     ;[3807] 3f
                    ld        a,a                           ;[3808] 7f
                    rst       $38                           ;[3809] ff
                    cp        $fc                           ;[380a] fe fc
                    ret       m                             ;[380c] f8
                    ret       p                             ;[380d] f0
                    ret       po                            ;[380e] e0
                    ret       nz                            ;[380f] c0
                    add       b                             ;[3810] 80
                    nop                                     ;[3811] 00
                    djnz      $3816                         ;[3812] 10 02
                    jr        nz,$3827                      ;[3814] 20 11
                    ld        b,$21                         ;[3816] 06 21
                    djnz      $381e                         ;[3818] 10 04
                    jr        nz,$382d                      ;[381a] 20 11
                    dec       b                             ;[381c] 05
                    ld        hl,$0010                      ;[381d] 21 10 00
                    jr        nz,$3821                      ;[3820] 20 ff
                    push      bc                            ;[3822] c5
                    push      de                            ;[3823] d5
                    push      hl                            ;[3824] e5
                    ld        hl,$3802                      ;[3825] 21 02 38
                    ld        de,$5b98                      ;[3828] 11 98 5b
                    ld        bc,$0010                      ;[382b] 01 10 00
                    ldir                                    ;[382e] ed b0
                    ld        hl,($5c36)                    ;[3830] 2a 36 5c
                    push      hl                            ;[3833] e5
                    ld        hl,$5a98                      ;[3834] 21 98 5a
                    ld        ($5c36),hl                    ;[3837] 22 36 5c
                    ld        hl,$3812                      ;[383a] 21 12 38
                    call      $3733                         ;[383d] cd 33 37
                    pop       hl                            ;[3840] e1
                    ld        ($5c36),hl                    ;[3841] 22 36 5c
                    pop       hl                            ;[3844] e1
                    pop       de                            ;[3845] d1
                    pop       bc                            ;[3846] c1
                    ret                                     ;[3847] c9

                    ld        hl,$2769                      ;[3848] 21 69 27
                    jr        $385a                         ;[384b] 18 0d
                    ld        hl,$2772                      ;[384d] 21 72 27
                    jr        $385a                         ;[3850] 18 08
                    ld        hl,$275e                      ;[3852] 21 5e 27
                    jr        $385a                         ;[3855] 18 03
                    ld        hl,$2784                      ;[3857] 21 84 27
                    push      hl                            ;[385a] e5
                    call      $3881                         ;[385b] cd 81 38
                    ld        hl,$5aa0                      ;[385e] 21 a0 5a
                    ld        b,$20                         ;[3861] 06 20
                    ld        a,$40                         ;[3863] 3e 40
                    ld        (hl),a                        ;[3865] 77
                    inc       hl                            ;[3866] 23
                    djnz      $3865                         ;[3867] 10 fc
                    ld        hl,$37ec                      ;[3869] 21 ec 37
                    call      $3733                         ;[386c] cd 33 37
                    ld        bc,$1500                      ;[386f] 01 00 15
                    call      $372b                         ;[3872] cd 2b 37
                    pop       de                            ;[3875] d1
                    call      $057d                         ;[3876] cd 7d 05
                    ld        c,$1a                         ;[3879] 0e 1a
                    call      $372b                         ;[387b] cd 2b 37
                    jp        $3822                         ;[387e] c3 22 38
                    ld        b,$15                         ;[3881] 06 15
                    ld        d,$17                         ;[3883] 16 17
                    jp        $3b5e                         ;[3885] c3 5e 3b
                    call      $1f20                         ;[3888] cd 20 1f
                    call      $3a05                         ;[388b] cd 05 3a
                    ld        a,d                           ;[388e] 7a
                    or        e                             ;[388f] b3
                    jp        z,$39c0                       ;[3890] ca c0 39
                    ld        hl,($5b96)                    ;[3893] 2a 96 5b
                    rst       $28                           ;[3896] ef
                    xor       c                             ;[3897] a9
                    jr        nc,$3885                      ;[3898] 30 eb
                    ld        hl,($5b94)                    ;[389a] 2a 94 5b
                    add       hl,de                         ;[389d] 19
                    ld        de,$2710                      ;[389e] 11 10 27
                    or        a                             ;[38a1] b7
                    sbc       hl,de                         ;[38a2] ed 52
                    jp        nc,$39c0                      ;[38a4] d2 c0 39
                    ld        hl,($5c53)                    ;[38a7] 2a 53 5c
                    rst       $28                           ;[38aa] ef
                    cp        b                             ;[38ab] b8
                    add       hl,de                         ;[38ac] 19
                    inc       hl                            ;[38ad] 23
                    inc       hl                            ;[38ae] 23
                    ld        ($5b92),hl                    ;[38af] 22 92 5b
                    inc       hl                            ;[38b2] 23
                    inc       hl                            ;[38b3] 23
                    ld        ($5b6b),de                    ;[38b4] ed 53 6b 5b
                    ld        a,(hl)                        ;[38b8] 7e
                    rst       $28                           ;[38b9] ef
                    or        (hl)                          ;[38ba] b6
                    jr        $38bb                         ;[38bb] 18 fe
                    dec       c                             ;[38bd] 0d
                    jr        z,$38c5                       ;[38be] 28 05
                    call      $390e                         ;[38c0] cd 0e 39
                    jr        $38b8                         ;[38c3] 18 f3
                    ld        de,($5b6b)                    ;[38c5] ed 5b 6b 5b
                    ld        hl,($5c4b)                    ;[38c9] 2a 4b 5c
                    and       a                             ;[38cc] a7
                    sbc       hl,de                         ;[38cd] ed 52
                    ex        de,hl                         ;[38cf] eb
                    jr        nz,$38aa                      ;[38d0] 20 d8
                    call      $3a05                         ;[38d2] cd 05 3a
                    ld        b,d                           ;[38d5] 42
                    ld        c,e                           ;[38d6] 4b
                    ld        de,$0000                      ;[38d7] 11 00 00
                    ld        hl,($5c53)                    ;[38da] 2a 53 5c
                    push      bc                            ;[38dd] c5
                    push      de                            ;[38de] d5
                    push      hl                            ;[38df] e5
                    ld        hl,($5b96)                    ;[38e0] 2a 96 5b
                    rst       $28                           ;[38e3] ef
                    xor       c                             ;[38e4] a9
                    jr        nc,$38d4                      ;[38e5] 30 ed
                    ld        e,e                           ;[38e7] 5b
                    sub       h                             ;[38e8] 94
                    ld        e,e                           ;[38e9] 5b
                    add       hl,de                         ;[38ea] 19
                    ex        de,hl                         ;[38eb] eb
                    pop       hl                            ;[38ec] e1
                    ld        (hl),d                        ;[38ed] 72
                    inc       hl                            ;[38ee] 23
                    ld        (hl),e                        ;[38ef] 73
                    inc       hl                            ;[38f0] 23
                    ld        c,(hl)                        ;[38f1] 4e
                    inc       hl                            ;[38f2] 23
                    ld        b,(hl)                        ;[38f3] 46
                    inc       hl                            ;[38f4] 23
                    add       hl,bc                         ;[38f5] 09
                    pop       de                            ;[38f6] d1
                    inc       de                            ;[38f7] 13
                    pop       bc                            ;[38f8] c1
                    dec       bc                            ;[38f9] 0b
                    ld        a,b                           ;[38fa] 78
                    or        c                             ;[38fb] b1
                    jr        nz,$38dd                      ;[38fc] 20 df
                    call      $1f45                         ;[38fe] cd 45 1f
                    ld        ($5b92),bc                    ;[3901] ed 43 92 5b
                    scf                                     ;[3905] 37
                    ret                                     ;[3906] c9

                    jp        z,$e1f0                       ;[3907] ca f0 e1
                    call      pe,$e5ed                      ;[390a] ec ed e5
                    rst       $30                           ;[390d] f7
                    inc       hl                            ;[390e] 23
                    ld        ($5b79),hl                    ;[390f] 22 79 5b
                    ex        de,hl                         ;[3912] eb
                    ld        bc,$0007                      ;[3913] 01 07 00
                    ld        hl,$3907                      ;[3916] 21 07 39
                    cpir                                    ;[3919] ed b1
                    ex        de,hl                         ;[391b] eb
                    ret       nz                            ;[391c] c0
                    ld        c,$00                         ;[391d] 0e 00
                    ld        a,(hl)                        ;[391f] 7e
                    cp        $20                           ;[3920] fe 20
                    jr        z,$393f                       ;[3922] 28 1b
                    rst       $28                           ;[3924] ef
                    dec       de                            ;[3925] 1b
                    dec       l                             ;[3926] 2d
                    jr        nc,$393f                      ;[3927] 30 16
                    cp        $2e                           ;[3929] fe 2e
                    jr        z,$393f                       ;[392b] 28 12
                    cp        $0e                           ;[392d] fe 0e
                    jr        z,$3943                       ;[392f] 28 12
                    or        $20                           ;[3931] f6 20
                    cp        $65                           ;[3933] fe 65
                    jr        nz,$393b                      ;[3935] 20 04
                    ld        a,b                           ;[3937] 78
                    or        c                             ;[3938] b1
                    jr        nz,$393f                      ;[3939] 20 04
                    ld        hl,($5b79)                    ;[393b] 2a 79 5b
                    ret                                     ;[393e] c9

                    inc       bc                            ;[393f] 03
                    inc       hl                            ;[3940] 23
                    jr        $391f                         ;[3941] 18 dc
                    ld        ($5b71),bc                    ;[3943] ed 43 71 5b
                    push      hl                            ;[3947] e5
                    rst       $28                           ;[3948] ef
                    or        (hl)                          ;[3949] b6
                    jr        $3919                         ;[394a] 18 cd
                    ld        (hl),$3a                      ;[394c] 36 3a
                    ld        a,(hl)                        ;[394e] 7e
                    pop       hl                            ;[394f] e1
                    cp        $3a                           ;[3950] fe 3a
                    jr        z,$3957                       ;[3952] 28 03
                    cp        $0d                           ;[3954] fe 0d
                    ret       nz                            ;[3956] c0
                    inc       hl                            ;[3957] 23
                    rst       $28                           ;[3958] ef
                    or        h                             ;[3959] b4
                    inc       sp                            ;[395a] 33
                    rst       $28                           ;[395b] ef
                    and       d                             ;[395c] a2
                    dec       l                             ;[395d] 2d
                    ld        h,b                           ;[395e] 60
                    ld        l,c                           ;[395f] 69
                    rst       $28                           ;[3960] ef
                    ld        l,(hl)                        ;[3961] 6e
                    add       hl,de                         ;[3962] 19
                    jr        z,$396f                       ;[3963] 28 0a
                    ld        a,(hl)                        ;[3965] 7e
                    cp        $80                           ;[3966] fe 80
                    jr        nz,$396f                      ;[3968] 20 05
                    ld        hl,$270f                      ;[396a] 21 0f 27
                    jr        $3980                         ;[396d] 18 11
                    ld        ($5b77),hl                    ;[396f] 22 77 5b
                    call      $3a0b                         ;[3972] cd 0b 3a
                    ld        hl,($5b96)                    ;[3975] 2a 96 5b
                    rst       $28                           ;[3978] ef
                    xor       c                             ;[3979] a9
                    jr        nc,$3969                      ;[397a] 30 ed
                    ld        e,e                           ;[397c] 5b
                    sub       h                             ;[397d] 94
                    ld        e,e                           ;[397e] 5b
                    add       hl,de                         ;[397f] 19
                    ld        de,$5b73                      ;[3980] 11 73 5b
                    push      hl                            ;[3983] e5
                    call      $3a3c                         ;[3984] cd 3c 3a
                    ld        e,b                           ;[3987] 58
                    inc       e                             ;[3988] 1c
                    ld        d,$00                         ;[3989] 16 00
                    push      de                            ;[398b] d5
                    push      hl                            ;[398c] e5
                    ld        l,e                           ;[398d] 6b
                    ld        h,$00                         ;[398e] 26 00
                    ld        bc,($5b71)                    ;[3990] ed 4b 71 5b
                    or        a                             ;[3994] b7
                    sbc       hl,bc                         ;[3995] ed 42
                    ld        ($5b71),hl                    ;[3997] 22 71 5b
                    jr        z,$39cf                       ;[399a] 28 33
                    jr        c,$39c5                       ;[399c] 38 27
                    ld        b,h                           ;[399e] 44
                    ld        c,l                           ;[399f] 4d
                    ld        hl,($5b79)                    ;[39a0] 2a 79 5b
                    push      hl                            ;[39a3] e5
                    push      de                            ;[39a4] d5
                    ld        hl,($5c65)                    ;[39a5] 2a 65 5c
                    add       hl,bc                         ;[39a8] 09
                    jr        c,$39be                       ;[39a9] 38 13
                    ex        de,hl                         ;[39ab] eb
                    ld        hl,$0082                      ;[39ac] 21 82 00
                    add       hl,de                         ;[39af] 19
                    jr        c,$39be                       ;[39b0] 38 0c
                    sbc       hl,sp                         ;[39b2] ed 72
                    ccf                                     ;[39b4] 3f
                    jr        c,$39be                       ;[39b5] 38 07
                    pop       de                            ;[39b7] d1
                    pop       hl                            ;[39b8] e1
                    rst       $28                           ;[39b9] ef
                    ld        d,l                           ;[39ba] 55
                    ld        d,$18                         ;[39bb] 16 18
                    ld        de,$e1d1                      ;[39bd] 11 d1 e1
                    call      $1f45                         ;[39c0] cd 45 1f
                    and       a                             ;[39c3] a7
                    ret                                     ;[39c4] c9

                    dec       bc                            ;[39c5] 0b
                    dec       e                             ;[39c6] 1d
                    jr        nz,$39c5                      ;[39c7] 20 fc
                    ld        hl,($5b79)                    ;[39c9] 2a 79 5b
                    rst       $28                           ;[39cc] ef
                    ret       pe                            ;[39cd] e8
                    add       hl,de                         ;[39ce] 19
                    ld        de,($5b79)                    ;[39cf] ed 5b 79 5b
                    pop       hl                            ;[39d3] e1
                    pop       bc                            ;[39d4] c1
                    ldir                                    ;[39d5] ed b0
                    ex        de,hl                         ;[39d7] eb
                    ld        (hl),$0e                      ;[39d8] 36 0e
                    pop       bc                            ;[39da] c1
                    inc       hl                            ;[39db] 23
                    push      hl                            ;[39dc] e5
                    rst       $28                           ;[39dd] ef
                    dec       hl                            ;[39de] 2b
                    dec       l                             ;[39df] 2d
                    pop       de                            ;[39e0] d1
                    ld        bc,$0005                      ;[39e1] 01 05 00
                    ldir                                    ;[39e4] ed b0
                    ex        de,hl                         ;[39e6] eb
                    push      hl                            ;[39e7] e5
                    ld        hl,($5b92)                    ;[39e8] 2a 92 5b
                    push      hl                            ;[39eb] e5
                    ld        e,(hl)                        ;[39ec] 5e
                    inc       hl                            ;[39ed] 23
                    ld        d,(hl)                        ;[39ee] 56
                    ld        hl,($5b71)                    ;[39ef] 2a 71 5b
                    add       hl,de                         ;[39f2] 19
                    ex        de,hl                         ;[39f3] eb
                    pop       hl                            ;[39f4] e1
                    ld        (hl),e                        ;[39f5] 73
                    inc       hl                            ;[39f6] 23
                    ld        (hl),d                        ;[39f7] 72
                    ld        hl,($5b6b)                    ;[39f8] 2a 6b 5b
                    ld        de,($5b71)                    ;[39fb] ed 5b 71 5b
                    add       hl,de                         ;[39ff] 19
                    ld        ($5b6b),hl                    ;[3a00] 22 6b 5b
                    pop       hl                            ;[3a03] e1
                    ret                                     ;[3a04] c9

                    ld        hl,($5c4b)                    ;[3a05] 2a 4b 5c
                    ld        ($5b77),hl                    ;[3a08] 22 77 5b
                    ld        hl,($5c53)                    ;[3a0b] 2a 53 5c
                    ld        de,($5b77)                    ;[3a0e] ed 5b 77 5b
                    or        a                             ;[3a12] b7
                    sbc       hl,de                         ;[3a13] ed 52
                    jr        z,$3a31                       ;[3a15] 28 1a
                    ld        hl,($5c53)                    ;[3a17] 2a 53 5c
                    ld        bc,$0000                      ;[3a1a] 01 00 00
                    push      bc                            ;[3a1d] c5
                    rst       $28                           ;[3a1e] ef
                    cp        b                             ;[3a1f] b8
                    add       hl,de                         ;[3a20] 19
                    ld        hl,($5b77)                    ;[3a21] 2a 77 5b
                    and       a                             ;[3a24] a7
                    sbc       hl,de                         ;[3a25] ed 52
                    jr        z,$3a2e                       ;[3a27] 28 05
                    ex        de,hl                         ;[3a29] eb
                    pop       bc                            ;[3a2a] c1
                    inc       bc                            ;[3a2b] 03
                    jr        $3a1d                         ;[3a2c] 18 ef
                    pop       de                            ;[3a2e] d1
                    inc       de                            ;[3a2f] 13
                    ret                                     ;[3a30] c9

                    ld        de,$0000                      ;[3a31] 11 00 00
                    ret                                     ;[3a34] c9

                    inc       hl                            ;[3a35] 23
                    ld        a,(hl)                        ;[3a36] 7e
                    cp        $20                           ;[3a37] fe 20
                    jr        z,$3a35                       ;[3a39] 28 fa
                    ret                                     ;[3a3b] c9

                    push      de                            ;[3a3c] d5
                    ld        bc,$fc18                      ;[3a3d] 01 18 fc
                    call      $3a60                         ;[3a40] cd 60 3a
                    ld        bc,$ff9c                      ;[3a43] 01 9c ff
                    call      $3a60                         ;[3a46] cd 60 3a
                    ld        c,$f6                         ;[3a49] 0e f6
                    call      $3a60                         ;[3a4b] cd 60 3a
                    ld        a,l                           ;[3a4e] 7d
                    add       $30                           ;[3a4f] c6 30
                    ld        (de),a                        ;[3a51] 12
                    inc       de                            ;[3a52] 13
                    ld        b,$03                         ;[3a53] 06 03
                    pop       hl                            ;[3a55] e1
                    ld        a,(hl)                        ;[3a56] 7e
                    cp        $30                           ;[3a57] fe 30
                    ret       nz                            ;[3a59] c0
                    ld        (hl),$20                      ;[3a5a] 36 20
                    inc       hl                            ;[3a5c] 23
                    djnz      $3a56                         ;[3a5d] 10 f7
                    ret                                     ;[3a5f] c9

                    xor       a                             ;[3a60] af
                    add       hl,bc                         ;[3a61] 09
                    inc       a                             ;[3a62] 3c
                    jr        c,$3a61                       ;[3a63] 38 fc
                    sbc       hl,bc                         ;[3a65] ed 42
                    dec       a                             ;[3a67] 3d
                    add       $30                           ;[3a68] c6 30
                    ld        (de),a                        ;[3a6a] 12
                    inc       de                            ;[3a6b] 13
                    ret                                     ;[3a6c] c9

                    ex        af,af'                        ;[3a6d] 08
                    nop                                     ;[3a6e] 00
                    nop                                     ;[3a6f] 00
                    inc       d                             ;[3a70] 14
                    nop                                     ;[3a71] 00
                    nop                                     ;[3a72] 00
                    nop                                     ;[3a73] 00
                    rrca                                    ;[3a74] 0f
                    nop                                     ;[3a75] 00
                    ex        af,af'                        ;[3a76] 08
                    nop                                     ;[3a77] 00
                    ld        d,$01                         ;[3a78] 16 01
                    nop                                     ;[3a7a] 00
                    nop                                     ;[3a7b] 00
                    nop                                     ;[3a7c] 00
                    rrca                                    ;[3a7d] 0f
                    nop                                     ;[3a7e] 00
                    ld        ix,$fd6c                      ;[3a7f] dd 21 6c fd
                    ld        hl,$3a6d                      ;[3a83] 21 6d 3a
                    jr        $3a8b                         ;[3a86] 18 03
                    ld        hl,$3a76                      ;[3a88] 21 76 3a
                    ld        de,$fd6c                      ;[3a8b] 11 6c fd
                    jp        $3fba                         ;[3a8e] c3 ba 3f
                    rst       $10                           ;[3a91] d7
                    ld        a,d                           ;[3a92] 7a
                    rst       $10                           ;[3a93] d7
                    scf                                     ;[3a94] 37
                    ret                                     ;[3a95] c9

                    and       $3f                           ;[3a96] e6 3f
                    ld        (ix+$06),a                    ;[3a98] dd 77 06
                    scf                                     ;[3a9b] 37
                    ret                                     ;[3a9c] c9

                    ld        a,(ix+$01)                    ;[3a9d] dd 7e 01
                    add       b                             ;[3aa0] 80
                    ld        b,a                           ;[3aa1] 47
                    call      $3ba0                         ;[3aa2] cd a0 3b
                    ld        a,(hl)                        ;[3aa5] 7e
                    ld        (ix+$07),a                    ;[3aa6] dd 77 07
                    cpl                                     ;[3aa9] 2f
                    and       $c0                           ;[3aaa] e6 c0
                    or        (ix+$06)                      ;[3aac] dd b6 06
                    ld        (hl),a                        ;[3aaf] 77
                    scf                                     ;[3ab0] 37
                    ret                                     ;[3ab1] c9

                    ld        a,(ix+$01)                    ;[3ab2] dd 7e 01
                    add       b                             ;[3ab5] 80
                    ld        b,a                           ;[3ab6] 47
                    call      $3ba0                         ;[3ab7] cd a0 3b
                    ld        a,(ix+$07)                    ;[3aba] dd 7e 07
                    ld        (hl),a                        ;[3abd] 77
                    ret                                     ;[3abe] c9

                    push      hl                            ;[3abf] e5
                    ld        h,$00                         ;[3ac0] 26 00
                    ld        a,e                           ;[3ac2] 7b
                    sub       b                             ;[3ac3] 90
                    jr        $3acd                         ;[3ac4] 18 07
                    push      hl                            ;[3ac6] e5
                    ld        a,e                           ;[3ac7] 7b
                    ld        e,b                           ;[3ac8] 58
                    ld        b,a                           ;[3ac9] 47
                    sub       e                             ;[3aca] 93
                    ld        h,$ff                         ;[3acb] 26 ff
                    ld        c,a                           ;[3acd] 4f
                    ld        a,b                           ;[3ace] 78
                    cp        e                             ;[3acf] bb
                    jr        z,$3b1d                       ;[3ad0] 28 4b
                    push      de                            ;[3ad2] d5
                    call      $3b98                         ;[3ad3] cd 98 3b
                    push      bc                            ;[3ad6] c5
                    ld        c,h                           ;[3ad7] 4c
                    rst       $28                           ;[3ad8] ef
                    sbc       e                             ;[3ad9] 9b
                    ld        c,$eb                         ;[3ada] 0e eb
                    xor       a                             ;[3adc] af
                    or        c                             ;[3add] b1
                    jr        z,$3ae3                       ;[3ade] 28 03
                    inc       b                             ;[3ae0] 04
                    jr        $3ae4                         ;[3ae1] 18 01
                    dec       b                             ;[3ae3] 05
                    push      de                            ;[3ae4] d5
                    rst       $28                           ;[3ae5] ef
                    sbc       e                             ;[3ae6] 9b
                    ld        c,$d1                         ;[3ae7] 0e d1
                    ld        a,c                           ;[3ae9] 79
                    ld        c,$20                         ;[3aea] 0e 20
                    ld        b,$08                         ;[3aec] 06 08
                    push      bc                            ;[3aee] c5
                    push      hl                            ;[3aef] e5
                    push      de                            ;[3af0] d5
                    ld        b,$00                         ;[3af1] 06 00
                    ldir                                    ;[3af3] ed b0
                    pop       de                            ;[3af5] d1
                    pop       hl                            ;[3af6] e1
                    pop       bc                            ;[3af7] c1
                    inc       h                             ;[3af8] 24
                    inc       d                             ;[3af9] 14
                    djnz      $3aee                         ;[3afa] 10 f2
                    push      af                            ;[3afc] f5
                    push      de                            ;[3afd] d5
                    rst       $28                           ;[3afe] ef
                    adc       b                             ;[3aff] 88
                    ld        c,$eb                         ;[3b00] 0e eb
                    ex        (sp),hl                       ;[3b02] e3
                    rst       $28                           ;[3b03] ef
                    adc       b                             ;[3b04] 88
                    ld        c,$eb                         ;[3b05] 0e eb
                    ex        (sp),hl                       ;[3b07] e3
                    pop       de                            ;[3b08] d1
                    ld        bc,$0020                      ;[3b09] 01 20 00
                    ldir                                    ;[3b0c] ed b0
                    pop       af                            ;[3b0e] f1
                    pop       bc                            ;[3b0f] c1
                    and       a                             ;[3b10] a7
                    jr        z,$3b16                       ;[3b11] 28 03
                    inc       b                             ;[3b13] 04
                    jr        $3b17                         ;[3b14] 18 01
                    dec       b                             ;[3b16] 05
                    dec       c                             ;[3b17] 0d
                    ld        h,a                           ;[3b18] 67
                    jr        nz,$3ad6                      ;[3b19] 20 bb
                    pop       de                            ;[3b1b] d1
                    ld        b,e                           ;[3b1c] 43
                    pop       hl                            ;[3b1d] e1
                    call      $3bb8                         ;[3b1e] cd b8 3b
                    ex        de,hl                         ;[3b21] eb
                    ld        a,($5c3c)                     ;[3b22] 3a 3c 5c
                    push      af                            ;[3b25] f5
                    ld        hl,$ec0d                      ;[3b26] 21 0d ec
                    bit       6,(hl)                        ;[3b29] cb 76
                    res       0,a                           ;[3b2b] cb 87
                    jr        z,$3b31                       ;[3b2d] 28 02
                    set       0,a                           ;[3b2f] cb c7
                    ld        ($5c3c),a                     ;[3b31] 32 3c 5c
                    ld        c,$00                         ;[3b34] 0e 00
                    call      $372b                         ;[3b36] cd 2b 37
                    ex        de,hl                         ;[3b39] eb
                    ld        b,$20                         ;[3b3a] 06 20
                    ld        a,(hl)                        ;[3b3c] 7e
                    and       a                             ;[3b3d] a7
                    jr        nz,$3b42                      ;[3b3e] 20 02
                    ld        a,$20                         ;[3b40] 3e 20
                    cp        $90                           ;[3b42] fe 90
                    jr        nc,$3b55                      ;[3b44] 30 0f
                    rst       $28                           ;[3b46] ef
                    djnz      $3b49                         ;[3b47] 10 00
                    inc       hl                            ;[3b49] 23
                    djnz      $3b3c                         ;[3b4a] 10 f0
                    pop       af                            ;[3b4c] f1
                    ld        ($5c3c),a                     ;[3b4d] 32 3c 5c
                    call      $3bb8                         ;[3b50] cd b8 3b
                    scf                                     ;[3b53] 37
                    ret                                     ;[3b54] c9

                    call      $1f20                         ;[3b55] cd 20 1f
                    rst       $10                           ;[3b58] d7
                    call      $1f45                         ;[3b59] cd 45 1f
                    jr        $3b49                         ;[3b5c] 18 eb
                    call      $3bb8                         ;[3b5e] cd b8 3b
                    ld        a,d                           ;[3b61] 7a
                    sub       b                             ;[3b62] 90
                    inc       a                             ;[3b63] 3c
                    ld        c,a                           ;[3b64] 4f
                    call      $3b98                         ;[3b65] cd 98 3b
                    push      bc                            ;[3b68] c5
                    rst       $28                           ;[3b69] ef
                    sbc       e                             ;[3b6a] 9b
                    ld        c,$0e                         ;[3b6b] 0e 0e
                    ex        af,af'                        ;[3b6d] 08
                    push      hl                            ;[3b6e] e5
                    ld        b,$20                         ;[3b6f] 06 20
                    xor       a                             ;[3b71] af
                    ld        (hl),a                        ;[3b72] 77
                    inc       hl                            ;[3b73] 23
                    djnz      $3b72                         ;[3b74] 10 fc
                    pop       hl                            ;[3b76] e1
                    inc       h                             ;[3b77] 24
                    dec       c                             ;[3b78] 0d
                    jr        nz,$3b6e                      ;[3b79] 20 f3
                    ld        b,$20                         ;[3b7b] 06 20
                    push      bc                            ;[3b7d] c5
                    rst       $28                           ;[3b7e] ef
                    adc       b                             ;[3b7f] 88
                    ld        c,$eb                         ;[3b80] 0e eb
                    pop       bc                            ;[3b82] c1
                    ld        a,($5c8d)                     ;[3b83] 3a 8d 5c
                    ld        (hl),a                        ;[3b86] 77
                    inc       hl                            ;[3b87] 23
                    djnz      $3b86                         ;[3b88] 10 fc
                    pop       bc                            ;[3b8a] c1
                    dec       b                             ;[3b8b] 05
                    dec       c                             ;[3b8c] 0d
                    jr        nz,$3b68                      ;[3b8d] 20 d9
                    call      $3bb8                         ;[3b8f] cd b8 3b
                    scf                                     ;[3b92] 37
                    ret                                     ;[3b93] c9

                    ld        a,$21                         ;[3b94] 3e 21
                    sub       c                             ;[3b96] 91
                    ld        c,a                           ;[3b97] 4f
                    ld        a,$18                         ;[3b98] 3e 18
                    sub       b                             ;[3b9a] 90
                    sub       (ix+$01)                      ;[3b9b] dd 96 01
                    ld        b,a                           ;[3b9e] 47
                    ret                                     ;[3b9f] c9

                    push      bc                            ;[3ba0] c5
                    xor       a                             ;[3ba1] af
                    ld        d,b                           ;[3ba2] 50
                    ld        e,a                           ;[3ba3] 5f
                    rr        d                             ;[3ba4] cb 1a
                    rr        e                             ;[3ba6] cb 1b
                    rr        d                             ;[3ba8] cb 1a
                    rr        e                             ;[3baa] cb 1b
                    rr        d                             ;[3bac] cb 1a
                    rr        e                             ;[3bae] cb 1b
                    ld        hl,$5800                      ;[3bb0] 21 00 58
                    ld        b,a                           ;[3bb3] 47
                    add       hl,bc                         ;[3bb4] 09
                    add       hl,de                         ;[3bb5] 19
                    pop       bc                            ;[3bb6] c1
                    ret                                     ;[3bb7] c9

                    push      af                            ;[3bb8] f5
                    push      hl                            ;[3bb9] e5
                    push      de                            ;[3bba] d5
                    ld        hl,($5c8d)                    ;[3bbb] 2a 8d 5c
                    ld        de,($5c8f)                    ;[3bbe] ed 5b 8f 5c
                    exx                                     ;[3bc2] d9
                    ld        hl,($ec0f)                    ;[3bc3] 2a 0f ec
                    ld        de,($ec11)                    ;[3bc6] ed 5b 11 ec
                    ld        ($5c8d),hl                    ;[3bca] 22 8d 5c
                    ld        ($5c8f),de                    ;[3bcd] ed 53 8f 5c
                    exx                                     ;[3bd1] d9
                    ld        ($ec0f),hl                    ;[3bd2] 22 0f ec
                    ld        ($ec11),de                    ;[3bd5] ed 53 11 ec
                    ld        hl,$ec13                      ;[3bd9] 21 13 ec
                    ld        a,($5c91)                     ;[3bdc] 3a 91 5c
                    ld        d,(hl)                        ;[3bdf] 56
                    ld        (hl),a                        ;[3be0] 77
                    ld        a,d                           ;[3be1] 7a
                    ld        ($5c91),a                     ;[3be2] 32 91 5c
                    pop       de                            ;[3be5] d1
                    pop       hl                            ;[3be6] e1
                    pop       af                            ;[3be7] f1
                    ret                                     ;[3be8] c9

                    call      $3c56                         ;[3be9] cd 56 3c
                    di                                      ;[3bec] f3
                    in        a,($fe)                       ;[3bed] db fe
                    and       $40                           ;[3bef] e6 40
                    ex        af,af'                        ;[3bf1] 08
                    ld        hl,$58e1                      ;[3bf2] 21 e1 58
                    ld        de,$0006                      ;[3bf5] 11 06 00
                    ld        b,e                           ;[3bf8] 43
                    ld        a,d                           ;[3bf9] 7a
                    ld        (hl),a                        ;[3bfa] 77
                    add       hl,de                         ;[3bfb] 19
                    djnz      $3bfa                         ;[3bfc] 10 fc
                    ld        hl,$0000                      ;[3bfe] 21 00 00
                    ld        de,$0800                      ;[3c01] 11 00 08
                    ld        bc,$bffe                      ;[3c04] 01 fe bf
                    in        a,(c)                         ;[3c07] ed 78
                    bit       0,a                           ;[3c09] cb 47
                    jr        z,$3c56                       ;[3c0b] 28 49
                    ld        b,$7f                         ;[3c0d] 06 7f
                    in        a,(c)                         ;[3c0f] ed 78
                    bit       0,a                           ;[3c11] cb 47
                    jr        z,$3c56                       ;[3c13] 28 41
                    ld        b,$f7                         ;[3c15] 06 f7
                    in        a,(c)                         ;[3c17] ed 78
                    bit       0,a                           ;[3c19] cb 47
                    jr        z,$3c56                       ;[3c1b] 28 39
                    dec       de                            ;[3c1d] 1b
                    ld        a,d                           ;[3c1e] 7a
                    or        e                             ;[3c1f] b3
                    jr        z,$3c2b                       ;[3c20] 28 09
                    in        a,($fe)                       ;[3c22] db fe
                    and       $40                           ;[3c24] e6 40
                    jr        z,$3c1d                       ;[3c26] 28 f5
                    inc       hl                            ;[3c28] 23
                    jr        $3c1d                         ;[3c29] 18 f2
                    rl        l                             ;[3c2b] cb 15
                    rl        h                             ;[3c2d] cb 14
                    rl        l                             ;[3c2f] cb 15
                    rl        h                             ;[3c31] cb 14
                    ex        af,af'                        ;[3c33] 08
                    jr        z,$3c3d                       ;[3c34] 28 07
                    ex        af,af'                        ;[3c36] 08
                    ld        a,$20                         ;[3c37] 3e 20
                    sub       h                             ;[3c39] 94
                    ld        l,a                           ;[3c3a] 6f
                    jr        $3c3f                         ;[3c3b] 18 02
                    ex        af,af'                        ;[3c3d] 08
                    ld        l,h                           ;[3c3e] 6c
                    xor       a                             ;[3c3f] af
                    ld        h,a                           ;[3c40] 67
                    ld        de,$591f                      ;[3c41] 11 1f 59
                    ld        b,$20                         ;[3c44] 06 20
                    ld        a,$48                         ;[3c46] 3e 48
                    ei                                      ;[3c48] fb
                    halt                                    ;[3c49] 76
                    di                                      ;[3c4a] f3
                    ld        (de),a                        ;[3c4b] 12
                    dec       de                            ;[3c4c] 1b
                    djnz      $3c4b                         ;[3c4d] 10 fc
                    inc       de                            ;[3c4f] 13
                    add       hl,de                         ;[3c50] 19
                    ld        a,$68                         ;[3c51] 3e 68
                    ld        (hl),a                        ;[3c53] 77
                    jr        $3bfe                         ;[3c54] 18 a8
                    ei                                      ;[3c56] fb
                    ld        b,$19                         ;[3c57] 06 19
                    halt                                    ;[3c59] 76
                    djnz      $3c59                         ;[3c5a] 10 fd
                    ld        hl,$5c3b                      ;[3c5c] 21 3b 5c
                    res       5,(hl)                        ;[3c5f] cb ae
                    scf                                     ;[3c61] 37
                    ret                                     ;[3c62] c9

                    ld        a,$01                         ;[3c63] 3e 01
                    jr        $3c69                         ;[3c65] 18 02
                    ld        a,$00                         ;[3c67] 3e 00
                    ld        ($fd8a),a                     ;[3c69] 32 8a fd
                    ld        hl,$0000                      ;[3c6c] 21 00 00
                    ld        ($fd85),hl                    ;[3c6f] 22 85 fd
                    ld        ($fd87),hl                    ;[3c72] 22 87 fd
                    add       hl,sp                         ;[3c75] 39
                    ld        ($fd8b),hl                    ;[3c76] 22 8b fd
                    call      $34ea                         ;[3c79] cd ea 34
                    ld        a,$00                         ;[3c7c] 3e 00
                    ld        ($fd84),a                     ;[3c7e] 32 84 fd
                    ld        hl,$fd74                      ;[3c81] 21 74 fd
                    ld        ($fd7d),hl                    ;[3c84] 22 7d fd
                    call      $1f20                         ;[3c87] cd 20 1f
                    rst       $28                           ;[3c8a] ef
                    or        b                             ;[3c8b] b0
                    ld        d,$cd                         ;[3c8c] 16 cd
                    ld        b,l                           ;[3c8e] 45
                    rra                                     ;[3c8f] 1f
                    ld        a,$00                         ;[3c90] 3e 00
                    ld        ($fd81),a                     ;[3c92] 32 81 fd
                    ld        hl,($5c59)                    ;[3c95] 2a 59 5c
                    ld        ($fd82),hl                    ;[3c98] 22 82 fd
                    ld        hl,$0000                      ;[3c9b] 21 00 00
                    ld        ($fd7f),hl                    ;[3c9e] 22 7f fd
                    ld        hl,($fd85)                    ;[3ca1] 2a 85 fd
                    inc       hl                            ;[3ca4] 23
                    ld        ($fd85),hl                    ;[3ca5] 22 85 fd
                    call      $3d9d                         ;[3ca8] cd 9d 3d
                    ld        c,a                           ;[3cab] 4f
                    ld        a,($fd81)                     ;[3cac] 3a 81 fd
                    cp        $00                           ;[3caf] fe 00
                    jr        nz,$3cf4                      ;[3cb1] 20 41
                    ld        a,c                           ;[3cb3] 79
                    and       $04                           ;[3cb4] e6 04
                    jr        z,$3ced                       ;[3cb6] 28 35
                    call      $3de9                         ;[3cb8] cd e9 3d
                    jr        nc,$3cc4                      ;[3cbb] 30 07
                    ld        a,$01                         ;[3cbd] 3e 01
                    ld        ($fd81),a                     ;[3cbf] 32 81 fd
                    jr        $3ca1                         ;[3cc2] 18 dd
                    ld        hl,($fd7f)                    ;[3cc4] 2a 7f fd
                    ld        a,l                           ;[3cc7] 7d
                    or        h                             ;[3cc8] b4
                    jp        nz,$3d1e                      ;[3cc9] c2 1e 3d
                    push      bc                            ;[3ccc] c5
                    call      $3dcd                         ;[3ccd] cd cd 3d
                    pop       bc                            ;[3cd0] c1
                    ld        a,$00                         ;[3cd1] 3e 00
                    ld        ($fd81),a                     ;[3cd3] 32 81 fd
                    ld        a,c                           ;[3cd6] 79
                    and       $01                           ;[3cd7] e6 01
                    jr        nz,$3cb3                      ;[3cd9] 20 d8
                    ld        a,b                           ;[3cdb] 78
                    call      $3e16                         ;[3cdc] cd 16 3e
                    ret       nc                            ;[3cdf] d0
                    ld        hl,($fd85)                    ;[3ce0] 2a 85 fd
                    inc       hl                            ;[3ce3] 23
                    ld        ($fd85),hl                    ;[3ce4] 22 85 fd
                    call      $3d9d                         ;[3ce7] cd 9d 3d
                    ld        c,a                           ;[3cea] 4f
                    jr        $3cd6                         ;[3ceb] 18 e9
                    ld        a,b                           ;[3ced] 78
                    call      $3e16                         ;[3cee] cd 16 3e
                    ret       nc                            ;[3cf1] d0
                    jr        $3ca1                         ;[3cf2] 18 ad
                    cp        $01                           ;[3cf4] fe 01
                    jr        nz,$3ced                      ;[3cf6] 20 f5
                    ld        a,c                           ;[3cf8] 79
                    and       $01                           ;[3cf9] e6 01
                    jr        z,$3cb8                       ;[3cfb] 28 bb
                    push      bc                            ;[3cfd] c5
                    call      $3f7e                         ;[3cfe] cd 7e 3f
                    pop       bc                            ;[3d01] c1
                    jr        c,$3d7d                       ;[3d02] 38 79
                    ld        hl,($fd7f)                    ;[3d04] 2a 7f fd
                    ld        a,h                           ;[3d07] 7c
                    or        l                             ;[3d08] b5
                    jr        nz,$3d1e                      ;[3d09] 20 13
                    ld        a,c                           ;[3d0b] 79
                    and       $02                           ;[3d0c] e6 02
                    jr        z,$3ccc                       ;[3d0e] 28 bc
                    call      $3de9                         ;[3d10] cd e9 3d
                    jr        nc,$3cc4                      ;[3d13] 30 af
                    ld        hl,($fd7d)                    ;[3d15] 2a 7d fd
                    dec       hl                            ;[3d18] 2b
                    ld        ($fd7f),hl                    ;[3d19] 22 7f fd
                    jr        $3ca1                         ;[3d1c] 18 83
                    push      bc                            ;[3d1e] c5
                    ld        hl,$fd74                      ;[3d1f] 21 74 fd
                    ld        de,($fd7f)                    ;[3d22] ed 5b 7f fd
                    ld        a,d                           ;[3d26] 7a
                    cp        h                             ;[3d27] bc
                    jr        nz,$3d2f                      ;[3d28] 20 05
                    ld        a,e                           ;[3d2a] 7b
                    cp        l                             ;[3d2b] bd
                    jr        nz,$3d2f                      ;[3d2c] 20 01
                    inc       de                            ;[3d2e] 13
                    dec       de                            ;[3d2f] 1b
                    jr        $3d33                         ;[3d30] 18 01
                    inc       hl                            ;[3d32] 23
                    ld        a,(hl)                        ;[3d33] 7e
                    and       $7f                           ;[3d34] e6 7f
                    push      hl                            ;[3d36] e5
                    push      de                            ;[3d37] d5
                    call      $3e16                         ;[3d38] cd 16 3e
                    pop       de                            ;[3d3b] d1
                    pop       hl                            ;[3d3c] e1
                    ld        a,h                           ;[3d3d] 7c
                    cp        d                             ;[3d3e] ba
                    jr        nz,$3d32                      ;[3d3f] 20 f1
                    ld        a,l                           ;[3d41] 7d
                    cp        e                             ;[3d42] bb
                    jr        nz,$3d32                      ;[3d43] 20 ed
                    ld        de,($fd7f)                    ;[3d45] ed 5b 7f fd
                    ld        hl,$fd74                      ;[3d49] 21 74 fd
                    ld        ($fd7f),hl                    ;[3d4c] 22 7f fd
                    ld        bc,($fd7d)                    ;[3d4f] ed 4b 7d fd
                    dec       bc                            ;[3d53] 0b
                    ld        a,d                           ;[3d54] 7a
                    cp        h                             ;[3d55] bc
                    jr        nz,$3d70                      ;[3d56] 20 18
                    ld        a,e                           ;[3d58] 7b
                    cp        l                             ;[3d59] bd
                    jr        nz,$3d70                      ;[3d5a] 20 14
                    inc       de                            ;[3d5c] 13
                    push      hl                            ;[3d5d] e5
                    ld        hl,$0000                      ;[3d5e] 21 00 00
                    ld        ($fd7f),hl                    ;[3d61] 22 7f fd
                    pop       hl                            ;[3d64] e1
                    ld        a,b                           ;[3d65] 78
                    cp        h                             ;[3d66] bc
                    jr        nz,$3d70                      ;[3d67] 20 07
                    ld        a,c                           ;[3d69] 79
                    cp        l                             ;[3d6a] bd
                    jr        nz,$3d70                      ;[3d6b] 20 03
                    pop       bc                            ;[3d6d] c1
                    jr        $3d8f                         ;[3d6e] 18 1f
                    ld        a,(de)                        ;[3d70] 1a
                    ld        (hl),a                        ;[3d71] 77
                    inc       hl                            ;[3d72] 23
                    inc       de                            ;[3d73] 13
                    and       $80                           ;[3d74] e6 80
                    jr        z,$3d70                       ;[3d76] 28 f8
                    ld        ($fd7d),hl                    ;[3d78] 22 7d fd
                    jr        $3cfe                         ;[3d7b] 18 81
                    push      bc                            ;[3d7d] c5
                    call      $3e16                         ;[3d7e] cd 16 3e
                    pop       bc                            ;[3d81] c1
                    ld        hl,$0000                      ;[3d82] 21 00 00
                    ld        ($fd7f),hl                    ;[3d85] 22 7f fd
                    ld        a,($fd81)                     ;[3d88] 3a 81 fd
                    cp        $04                           ;[3d8b] fe 04
                    jr        z,$3d94                       ;[3d8d] 28 05
                    ld        a,$00                         ;[3d8f] 3e 00
                    ld        ($fd81),a                     ;[3d91] 32 81 fd
                    ld        hl,$fd74                      ;[3d94] 21 74 fd
                    ld        ($fd7d),hl                    ;[3d97] 22 7d fd
                    jp        $3cb3                         ;[3d9a] c3 b3 3c
                    call      $2d54                         ;[3d9d] cd 54 2d
                    ld        b,a                           ;[3da0] 47
                    cp        $3f                           ;[3da1] fe 3f
                    jr        c,$3daf                       ;[3da3] 38 0a
                    or        $20                           ;[3da5] f6 20
                    call      $3dc6                         ;[3da7] cd c6 3d
                    jr        c,$3dc3                       ;[3daa] 38 17
                    ld        a,$01                         ;[3dac] 3e 01
                    ret                                     ;[3dae] c9

                    cp        $20                           ;[3daf] fe 20
                    jr        z,$3dc0                       ;[3db1] 28 0d
                    cp        $23                           ;[3db3] fe 23
                    jr        z,$3dbd                       ;[3db5] 28 06
                    jr        c,$3dac                       ;[3db7] 38 f3
                    cp        $24                           ;[3db9] fe 24
                    jr        nz,$3dac                      ;[3dbb] 20 ef
                    ld        a,$02                         ;[3dbd] 3e 02
                    ret                                     ;[3dbf] c9

                    ld        a,$03                         ;[3dc0] 3e 03
                    ret                                     ;[3dc2] c9

                    ld        a,$06                         ;[3dc3] 3e 06
                    ret                                     ;[3dc5] c9

                    cp        $7b                           ;[3dc6] fe 7b
                    ret       nc                            ;[3dc8] d0
                    cp        $61                           ;[3dc9] fe 61
                    ccf                                     ;[3dcb] 3f
                    ret                                     ;[3dcc] c9

                    ld        hl,$fd74                      ;[3dcd] 21 74 fd
                    ld        ($fd7d),hl                    ;[3dd0] 22 7d fd
                    sub       a                             ;[3dd3] 97
                    ld        ($fd7f),a                     ;[3dd4] 32 7f fd
                    ld        ($fd80),a                     ;[3dd7] 32 80 fd
                    ld        a,(hl)                        ;[3dda] 7e
                    and       $7f                           ;[3ddb] e6 7f
                    push      hl                            ;[3ddd] e5
                    call      $3e9c                         ;[3dde] cd 9c 3e
                    pop       hl                            ;[3de1] e1
                    ld        a,(hl)                        ;[3de2] 7e
                    and       $80                           ;[3de3] e6 80
                    ret       nz                            ;[3de5] c0
                    inc       hl                            ;[3de6] 23
                    jr        $3dda                         ;[3de7] 18 f1
                    ld        hl,($fd7d)                    ;[3de9] 2a 7d fd
                    ld        de,$fd7d                      ;[3dec] 11 7d fd
                    ld        a,d                           ;[3def] 7a
                    cp        h                             ;[3df0] bc
                    jr        nz,$3df8                      ;[3df1] 20 05
                    ld        a,e                           ;[3df3] 7b
                    cp        l                             ;[3df4] bd
                    jp        z,$3e13                       ;[3df5] ca 13 3e
                    ld        de,$fd74                      ;[3df8] 11 74 fd
                    ld        a,d                           ;[3dfb] 7a
                    cp        h                             ;[3dfc] bc
                    jr        nz,$3e03                      ;[3dfd] 20 04
                    ld        a,e                           ;[3dff] 7b
                    cp        l                             ;[3e00] bd
                    jr        z,$3e09                       ;[3e01] 28 06
                    dec       hl                            ;[3e03] 2b
                    ld        a,(hl)                        ;[3e04] 7e
                    and       $7f                           ;[3e05] e6 7f
                    ld        (hl),a                        ;[3e07] 77
                    inc       hl                            ;[3e08] 23
                    ld        a,b                           ;[3e09] 78
                    or        $80                           ;[3e0a] f6 80
                    ld        (hl),a                        ;[3e0c] 77
                    inc       hl                            ;[3e0d] 23
                    ld        ($fd7d),hl                    ;[3e0e] 22 7d fd
                    scf                                     ;[3e11] 37
                    ret                                     ;[3e12] c9

                    scf                                     ;[3e13] 37
                    ccf                                     ;[3e14] 3f
                    ret                                     ;[3e15] c9

                    push      af                            ;[3e16] f5
                    ld        a,($fd89)                     ;[3e17] 3a 89 fd
                    or        a                             ;[3e1a] b7
                    jr        nz,$3e2f                      ;[3e1b] 20 12
                    pop       af                            ;[3e1d] f1
                    cp        $3e                           ;[3e1e] fe 3e
                    jr        z,$3e2a                       ;[3e20] 28 08
                    cp        $3c                           ;[3e22] fe 3c
                    jr        z,$3e2a                       ;[3e24] 28 04
                    call      $3e64                         ;[3e26] cd 64 3e
                    ret                                     ;[3e29] c9

                    ld        ($fd89),a                     ;[3e2a] 32 89 fd
                    scf                                     ;[3e2d] 37
                    ret                                     ;[3e2e] c9

                    cp        $3c                           ;[3e2f] fe 3c
                    ld        a,$00                         ;[3e31] 3e 00
                    ld        ($fd89),a                     ;[3e33] 32 89 fd
                    jr        nz,$3e52                      ;[3e36] 20 1a
                    pop       af                            ;[3e38] f1
                    cp        $3e                           ;[3e39] fe 3e
                    jr        nz,$3e41                      ;[3e3b] 20 04
                    ld        a,$c9                         ;[3e3d] 3e c9
                    jr        $3e26                         ;[3e3f] 18 e5
                    cp        $3d                           ;[3e41] fe 3d
                    jr        nz,$3e49                      ;[3e43] 20 04
                    ld        a,$c7                         ;[3e45] 3e c7
                    jr        $3e26                         ;[3e47] 18 dd
                    push      af                            ;[3e49] f5
                    ld        a,$3c                         ;[3e4a] 3e 3c
                    call      $3e64                         ;[3e4c] cd 64 3e
                    pop       af                            ;[3e4f] f1
                    jr        $3e26                         ;[3e50] 18 d4
                    pop       af                            ;[3e52] f1
                    cp        $3d                           ;[3e53] fe 3d
                    jr        nz,$3e5b                      ;[3e55] 20 04
                    ld        a,$c8                         ;[3e57] 3e c8
                    jr        $3e26                         ;[3e59] 18 cb
                    push      af                            ;[3e5b] f5
                    ld        a,$3e                         ;[3e5c] 3e 3e
                    call      $3e64                         ;[3e5e] cd 64 3e
                    pop       af                            ;[3e61] f1
                    jr        $3e26                         ;[3e62] 18 c2
                    cp        $0d                           ;[3e64] fe 0d
                    jr        z,$3e88                       ;[3e66] 28 20
                    cp        $ea                           ;[3e68] fe ea
                    ld        b,a                           ;[3e6a] 47
                    jr        nz,$3e74                      ;[3e6b] 20 07
                    ld        a,$04                         ;[3e6d] 3e 04
                    ld        ($fd81),a                     ;[3e6f] 32 81 fd
                    jr        $3e82                         ;[3e72] 18 0e
                    cp        $22                           ;[3e74] fe 22
                    jr        nz,$3e82                      ;[3e76] 20 0a
                    ld        a,($fd81)                     ;[3e78] 3a 81 fd
                    and       $fe                           ;[3e7b] e6 fe
                    xor       $02                           ;[3e7d] ee 02
                    ld        ($fd81),a                     ;[3e7f] 32 81 fd
                    ld        a,b                           ;[3e82] 78
                    call      $3e9c                         ;[3e83] cd 9c 3e
                    scf                                     ;[3e86] 37
                    ret                                     ;[3e87] c9

                    ld        a,($fd8a)                     ;[3e88] 3a 8a fd
                    cp        $00                           ;[3e8b] fe 00
                    jr        z,$3e99                       ;[3e8d] 28 0a
                    ld        bc,($fd85)                    ;[3e8f] ed 4b 85 fd
                    ld        hl,($fd8b)                    ;[3e93] 2a 8b fd
                    ld        sp,hl                         ;[3e96] f9
                    scf                                     ;[3e97] 37
                    ret                                     ;[3e98] c9

                    scf                                     ;[3e99] 37
                    ccf                                     ;[3e9a] 3f
                    ret                                     ;[3e9b] c9

                    ld        e,a                           ;[3e9c] 5f
                    ld        a,($fd84)                     ;[3e9d] 3a 84 fd
                    ld        d,a                           ;[3ea0] 57
                    ld        a,e                           ;[3ea1] 7b
                    cp        $20                           ;[3ea2] fe 20
                    jr        nz,$3ec6                      ;[3ea4] 20 20
                    ld        a,d                           ;[3ea6] 7a
                    and       $01                           ;[3ea7] e6 01
                    jr        nz,$3ebf                      ;[3ea9] 20 14
                    ld        a,d                           ;[3eab] 7a
                    and       $02                           ;[3eac] e6 02
                    jr        nz,$3eb7                      ;[3eae] 20 07
                    ld        a,d                           ;[3eb0] 7a
                    or        $02                           ;[3eb1] f6 02
                    ld        ($fd84),a                     ;[3eb3] 32 84 fd
                    ret                                     ;[3eb6] c9

                    ld        a,e                           ;[3eb7] 7b
                    call      $3efb                         ;[3eb8] cd fb 3e
                    ld        a,($fd84)                     ;[3ebb] 3a 84 fd
                    ret                                     ;[3ebe] c9

                    ld        a,d                           ;[3ebf] 7a
                    and       $fe                           ;[3ec0] e6 fe
                    ld        ($fd84),a                     ;[3ec2] 32 84 fd
                    ret                                     ;[3ec5] c9

                    cp        $a3                           ;[3ec6] fe a3
                    jr        nc,$3eee                      ;[3ec8] 30 24
                    ld        a,d                           ;[3eca] 7a
                    and       $02                           ;[3ecb] e6 02
                    jr        nz,$3eda                      ;[3ecd] 20 0b
                    ld        a,d                           ;[3ecf] 7a
                    and       $fe                           ;[3ed0] e6 fe
                    ld        ($fd84),a                     ;[3ed2] 32 84 fd
                    ld        a,e                           ;[3ed5] 7b
                    call      $3efb                         ;[3ed6] cd fb 3e
                    ret                                     ;[3ed9] c9

                    push      de                            ;[3eda] d5
                    ld        a,$20                         ;[3edb] 3e 20
                    call      $3efb                         ;[3edd] cd fb 3e
                    pop       de                            ;[3ee0] d1
                    ld        a,d                           ;[3ee1] 7a
                    and       $fe                           ;[3ee2] e6 fe
                    and       $fd                           ;[3ee4] e6 fd
                    ld        ($fd84),a                     ;[3ee6] 32 84 fd
                    ld        a,e                           ;[3ee9] 7b
                    call      $3efb                         ;[3eea] cd fb 3e
                    ret                                     ;[3eed] c9

                    ld        a,d                           ;[3eee] 7a
                    and       $fd                           ;[3eef] e6 fd
                    or        $01                           ;[3ef1] f6 01
                    ld        ($fd84),a                     ;[3ef3] 32 84 fd
                    ld        a,e                           ;[3ef6] 7b
                    call      $3efb                         ;[3ef7] cd fb 3e
                    ret                                     ;[3efa] c9

                    ld        hl,($fd87)                    ;[3efb] 2a 87 fd
                    inc       hl                            ;[3efe] 23
                    ld        ($fd87),hl                    ;[3eff] 22 87 fd
                    ld        hl,($fd82)                    ;[3f02] 2a 82 fd
                    ld        b,a                           ;[3f05] 47
                    ld        a,($fd8a)                     ;[3f06] 3a 8a fd
                    cp        $00                           ;[3f09] fe 00
                    ld        a,b                           ;[3f0b] 78
                    jr        z,$3f33                       ;[3f0c] 28 25
                    ld        de,($5c5f)                    ;[3f0e] ed 5b 5f 5c
                    ld        a,h                           ;[3f12] 7c
                    cp        d                             ;[3f13] ba
                    jr        nz,$3f30                      ;[3f14] 20 1a
                    ld        a,l                           ;[3f16] 7d
                    cp        e                             ;[3f17] bb
                    jr        nz,$3f30                      ;[3f18] 20 16
                    ld        bc,($fd85)                    ;[3f1a] ed 4b 85 fd
                    ld        hl,($fd87)                    ;[3f1e] 2a 87 fd
                    and       a                             ;[3f21] a7
                    sbc       hl,bc                         ;[3f22] ed 42
                    jr        nc,$3f2a                      ;[3f24] 30 04
                    ld        bc,($fd87)                    ;[3f26] ed 4b 87 fd
                    ld        hl,($fd8b)                    ;[3f2a] 2a 8b fd
                    ld        sp,hl                         ;[3f2d] f9
                    scf                                     ;[3f2e] 37
                    ret                                     ;[3f2f] c9

                    scf                                     ;[3f30] 37
                    jr        $3f35                         ;[3f31] 18 02
                    scf                                     ;[3f33] 37
                    ccf                                     ;[3f34] 3f
                    call      $1f20                         ;[3f35] cd 20 1f
                    jr        nc,$3f47                      ;[3f38] 30 0d
                    ld        a,(hl)                        ;[3f3a] 7e
                    ex        de,hl                         ;[3f3b] eb
                    cp        $0e                           ;[3f3c] fe 0e
                    jr        nz,$3f5d                      ;[3f3e] 20 1d
                    inc       de                            ;[3f40] 13
                    inc       de                            ;[3f41] 13
                    inc       de                            ;[3f42] 13
                    inc       de                            ;[3f43] 13
                    inc       de                            ;[3f44] 13
                    jr        $3f5d                         ;[3f45] 18 16
                    push      af                            ;[3f47] f5
                    ld        bc,$0001                      ;[3f48] 01 01 00
                    push      hl                            ;[3f4b] e5
                    push      de                            ;[3f4c] d5
                    call      $3f66                         ;[3f4d] cd 66 3f
                    pop       de                            ;[3f50] d1
                    pop       hl                            ;[3f51] e1
                    rst       $28                           ;[3f52] ef
                    ld        h,h                           ;[3f53] 64
                    ld        d,$2a                         ;[3f54] 16 2a
                    ld        h,l                           ;[3f56] 65
                    ld        e,h                           ;[3f57] 5c
                    ex        de,hl                         ;[3f58] eb
                    lddr                                    ;[3f59] ed b8
                    pop       af                            ;[3f5b] f1
                    ld        (de),a                        ;[3f5c] 12
                    inc       de                            ;[3f5d] 13
                    call      $1f45                         ;[3f5e] cd 45 1f
                    ld        ($fd82),de                    ;[3f61] ed 53 82 fd
                    ret                                     ;[3f65] c9

                    ld        hl,($5c65)                    ;[3f66] 2a 65 5c
                    add       hl,bc                         ;[3f69] 09
                    jr        c,$3f76                       ;[3f6a] 38 0a
                    ex        de,hl                         ;[3f6c] eb
                    ld        hl,$0082                      ;[3f6d] 21 82 00
                    add       hl,de                         ;[3f70] 19
                    jr        c,$3f76                       ;[3f71] 38 03
                    sbc       hl,sp                         ;[3f73] ed 72
                    ret       c                             ;[3f75] d8
                    ld        a,$03                         ;[3f76] 3e 03
                    ld        ($5c3a),a                     ;[3f78] 32 3a 5c
                    jp        $0321                         ;[3f7b] c3 21 03
                    call      $fd2e                         ;[3f7e] cd 2e fd
                    ret       c                             ;[3f81] d8
                    ld        b,$f9                         ;[3f82] 06 f9
                    ld        de,$fd74                      ;[3f84] 11 74 fd
                    ld        hl,$3594                      ;[3f87] 21 94 35
                    call      $fd3b                         ;[3f8a] cd 3b fd
                    ret       nc                            ;[3f8d] d0
                    cp        $ff                           ;[3f8e] fe ff
                    jr        nz,$3f96                      ;[3f90] 20 04
                    ld        a,$d4                         ;[3f92] 3e d4
                    jr        $3fb8                         ;[3f94] 18 22
                    cp        $fe                           ;[3f96] fe fe
                    jr        nz,$3f9e                      ;[3f98] 20 04
                    ld        a,$d3                         ;[3f9a] 3e d3
                    jr        $3fb8                         ;[3f9c] 18 1a
                    cp        $fd                           ;[3f9e] fe fd
                    jr        nz,$3fa6                      ;[3fa0] 20 04
                    ld        a,$ce                         ;[3fa2] 3e ce
                    jr        $3fb8                         ;[3fa4] 18 12
                    cp        $fc                           ;[3fa6] fe fc
                    jr        nz,$3fae                      ;[3fa8] 20 04
                    ld        a,$ed                         ;[3faa] 3e ed
                    jr        $3fb8                         ;[3fac] 18 0a
                    cp        $fb                           ;[3fae] fe fb
                    jr        nz,$3fb6                      ;[3fb0] 20 04
                    ld        a,$ec                         ;[3fb2] 3e ec
                    jr        $3fb8                         ;[3fb4] 18 02
                    sub       $56                           ;[3fb6] d6 56
                    scf                                     ;[3fb8] 37
                    ret                                     ;[3fb9] c9

                    ld        b,(hl)                        ;[3fba] 46
                    inc       hl                            ;[3fbb] 23
                    ld        a,(hl)                        ;[3fbc] 7e
                    ld        (de),a                        ;[3fbd] 12
                    inc       de                            ;[3fbe] 13
                    inc       hl                            ;[3fbf] 23
                    djnz      $3fbc                         ;[3fc0] 10 fa
                    ret                                     ;[3fc2] c9

                    cp        $30                           ;[3fc3] fe 30
                    ccf                                     ;[3fc5] 3f
                    ret       nc                            ;[3fc6] d0
                    cp        $3a                           ;[3fc7] fe 3a
                    ret       nc                            ;[3fc9] d0
                    sub       $30                           ;[3fca] d6 30
                    scf                                     ;[3fcc] 37
                    ret                                     ;[3fcd] c9

                    push      bc                            ;[3fce] c5
                    push      de                            ;[3fcf] d5
                    ld        b,(hl)                        ;[3fd0] 46
                    inc       hl                            ;[3fd1] 23
                    cp        (hl)                          ;[3fd2] be
                    inc       hl                            ;[3fd3] 23
                    ld        e,(hl)                        ;[3fd4] 5e
                    inc       hl                            ;[3fd5] 23
                    ld        d,(hl)                        ;[3fd6] 56
                    jr        z,$3fe1                       ;[3fd7] 28 08
                    inc       hl                            ;[3fd9] 23
                    djnz      $3fd2                         ;[3fda] 10 f6
                    scf                                     ;[3fdc] 37
                    ccf                                     ;[3fdd] 3f
                    pop       de                            ;[3fde] d1
                    pop       bc                            ;[3fdf] c1
                    ret                                     ;[3fe0] c9

                    ex        de,hl                         ;[3fe1] eb
                    pop       de                            ;[3fe2] d1
                    pop       bc                            ;[3fe3] c1
                    call      $3fee                         ;[3fe4] cd ee 3f
                    jr        c,$3feb                       ;[3fe7] 38 02
                    cp        a                             ;[3fe9] bf
                    ret                                     ;[3fea] c9

                    cp        a                             ;[3feb] bf
                    scf                                     ;[3fec] 37
                    ret                                     ;[3fed] c9

                    jp        (hl)                          ;[3fee] e9
                    nop                                     ;[3fef] 00
                    ld        c,l                           ;[3ff0] 4d
                    ld        b,d                           ;[3ff1] 42
                    nop                                     ;[3ff2] 00
                    ld        d,e                           ;[3ff3] 53
                    ld        b,d                           ;[3ff4] 42
                    nop                                     ;[3ff5] 00
                    ld        b,c                           ;[3ff6] 41
                    ld        b,e                           ;[3ff7] 43
                    nop                                     ;[3ff8] 00
                    ld        d,d                           ;[3ff9] 52
                    ld        b,a                           ;[3ffa] 47
                    nop                                     ;[3ffb] 00
                    ld        c,e                           ;[3ffc] 4b
                    ld        c,l                           ;[3ffd] 4d
                    nop                                     ;[3ffe] 00
                    ld        bc,$0000                      ;[3fff] 01 00 00
